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
  neither the menus nor the song load.

## Open, and the cheap next probe

Unknown whether the allocator failed because the heap is genuinely full or
because it gives up early. Those want opposite fixes. One run separates them:
log the requested size and the guest heap free total at the failing allocation.

Also unresolved, and easy to over-read: the `[libc] guest FILE*` line
immediately before the failure reports fd 0 and an all zero struct
(`Support.h:617`). It may mean the lexer was handed an unopened file, or it may
be unrelated noise from the same probe. No causal link was established.

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
