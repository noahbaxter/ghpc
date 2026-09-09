# The song load crash

Where the build actually stops as of 2026-09-09. Supersedes anything in
`ui-draw-hang.md` about the game being stuck in a menu: it is not, it walks the
menus fine and dies loading a song.

## How to reproduce it

The harness drives the game off live UI state, so this is one command:

    ./ghpc/scripts/build.sh --from=build
    cd work && GHPC_HIDE_WINDOW=1 GHPC_PAD_DRIVE=cross \
      ../build-debug/ps2xRuntime/ps2EntryRunner GH2_debug.elf

`GHPC_PAD_DRIVE` presses once per settled UI screen and logs every transition,
so the run reads as a path rather than a guess. Use `build-debug`, not
`build-calls`: the call histogram slows it enough to matter over minutes.

Expect this, then a crash roughly 6 minutes into `loading_screen`:

    [drive] bootup_load -> cut_scene_screen after 37 presses (t=32)
    [drive] cut_scene_screen -> guitar_help_screen after 0 presses
    [drive] guitar_help_screen -> splash_screen after 1 press
    [drive] splash_screen -> main_screen after 0 presses
    [drive] main_screen -> qp_selsong_screen after 2 presses
    [drive] qp_selsong_screen -> qp_diff_screen after 1 press
    [drive] qp_diff_screen -> loading_screen after 1 press (t=72)

`bootup_load` taking 37 presses is not a stall, a load screen ignores input by
design, which is why the driver does not warn on a screen whose name contains
"load".

## The crash

    [libc] guest FILE* 0x00448d8c has fd 0, struct: 00 00 00 ... all zero
    out of dynamic memory in yy_create_buffer()
    [guest] exit(2) called from ra=0x00307cec

Resolved against ELF symbol boundaries, all of it guest code:

    yylex             0x306f40  +0xa0
    yy_create_buffer  0x307b80  +0x5c
                                -> flex fatal error -> exit(2)

That is the flex generated lexer GH2 parses DTA with. `yy_create_buffer` is
recompiled guest code (`ps2xRuntime/src/runner/yy_create_buffer_0x307b80.cpp`),
so its allocation runs through the guest allocator, which returned NULL. flex
prints that exact string and calls `exit(YY_EXIT_FAILURE)`, which is 2.

**This is the `fix/real-newlib-allocator` branch's problem**, arrived at from
the other end.

## It is not a hang and not a loop, which took proving

Three things were ruled out with evidence. Do not re-chase them.

- **Not a loop.** Sampled the call histogram across the load. Two samples
  during the phase differ in character rather than repeating: `BinStream::Read`
  goes 256785 -> 1129591 while `Heap::InsertFreeBlock` falls 39891 -> 3704. A
  loop repeats its delta vector; this does not.
- **Not slow.** A 20 minute run on `build-debug` with no histogram died at
  about 7 minutes rather than finishing. More wall time does not help.
- **Not the audio service.** `GHPC_SYNTH_ACK=1` clears the stuck SPU handshake
  (`gSpuInFlight` at 0x444d94 stays 0, `SynthPoll` and `SPUSendBusy` leave the
  steady state entirely) and the load fails in exactly the same place. The SPU
  stall described in `ui-draw-hang.md` is real and still unfixed, but it blocks
  neither the menus nor the song load. Re-tested after the 128MB fix, on a
  build that gets past the crash: `[ghpc/spu] pending=0 inflight=0` and the
  loader queue is byte identical to a run without it.

## Answered: the heap was genuinely full, and 128MB fixed it

The census at the failure, from `dumpGuestHeapCensus`:

    [ghpc/heap] yy_fatal_error req=16386 (malloc #766)
    [ghpc/heap]   top=0x01cffdd8 size=552, headroom to ceiling 0x01d00000 = 0 KB
    [ghpc/heap]   bins: 33 free chunks, 5088 bytes total, largest 1136
    [ghpc/heap]   sbrk: base=0x005be2ac calls=158 refused=29 last=+20480

flex asks for 16386 (`YY_BUF_SIZE` + 2, so the buffer, not the 40 byte struct)
against 5.6KB free anywhere. Not an allocator giving up early: `_malloc_r` went
to `malloc_extend_top`, sbrk asked for +20480, and the ceiling refused it.

A linear walk of the heap says why it was full. 225 live chunks hold 23.3MB,
and two of them are 94% of that: 18.7MB at 0x00737628 plus one in the 2-4MB
bucket. The game reserved the memory, it did not spend it in small pieces.

**The fix is `PS2_RAM_SIZE` at 128MB** (commit `6babe38`). GH2 wants roughly a
24MB pool over a 5.7MB ELF, which barely fits in a 32MB map, so its
probe-and-halve sizing lands the break exactly on the ceiling. At 128MB it
takes the pool it actually wants and leaves ~93MB spare, the `+20480` sbrk
succeeds, and `out of dynamic memory` is gone.

A prediction worth recording because it was wrong: the probe asks looked like
"take all of RAM by design", which would have failed identically at any size.
It is not. The asks are bounded (24.2MB and 5.5MB, grabbed and released across
screens), so the shortfall was real and about 20KB wide.

The `[libc] guest FILE*` line is **not** a lead. 0x448d8c is inside
`impure_data` (0x448af8, 748 bytes), so it is a newlib `_sf` stream, and a
`fopen`ed FILE would be on the heap. It is `yy_fatal_error`'s own
`fprintf(stderr, ...)`, printed one call before the message it carries.

## Where it stops now

Past the crash, the song still does not load. The main thread renders the
loading screen forever (`App::Run` -> `RndTransformable::WorldXfm` ->
`RndGroup::DrawShowing` -> `PsRnd::VSync`, 21 of 25 stall samples pinned at
0x43f8b4) and the heap goes silent for 35+ minutes.

Two dead ends already ruled out, so do not re-chase them:

- **Semaphore 16 is not a deadlock.** Thread 2 is the async file worker
  (`gThreadSema` 0x445044, ring of 5 at 0x523ec0). `curCall == freeCall` with
  `curFunc=0x0` is an empty queue, which is what that thread does when idle.
- **`LoadMgr::AddLoader` (0x31d7f0) does fire during `loading_screen`**,
  repeatedly, so loading is being requested. `GHPC_PROBE=0x31d7f0,0x31da48`
  reproduces that with no rebuild. Whether `LoadMgr::Poll` advances them is
  the open question.

## Instrumentation this needed

Uncommitted at time of writing, all diagnostic:

- `GHPC_SAMPLE_MS` dumps the call histogram from a host thread on a real clock.
  The census in `EeScheduler::run` cannot do this job: a guest that stops
  yielding starves it, and a 300s run produced one census in the 229s that
  mattered. The host sampler is starved too under load, just less badly.
- `[ghpc/ui]` prints the live screen, from `TheUI` (0x4f9be8): `+0x40` current
  `UIScreen*`, `+0x44` transition target, `+0x28` transition state, and `+0x14`
  of the screen is its name Symbol.
- `[ghpc/spu]` prints the SPU send handshake globals.
- `GHPC_SYNTH_ACK` stands in for the missing SYNTH_R module. A probe, not a fix.
