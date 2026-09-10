# The song load crash

Where the build actually stops as of 2026-09-09. Supersedes anything in
`ui-draw-hang.md` about the game being stuck in a menu: it is not, it walks the
menus fine and dies loading a song.

## How to reproduce it

**Check `ghpc/scripts/bootskip.py --status` first.** When it is `on` the game
boots straight to `main_screen`, skipping `bootup_load`, the intro video and both
logo screens. That halves the time to `loading_screen` (t=41.6 vs t=86.3) and is
why it is usually left on, but it means anything `bootup_load` initialises is
skipped. The stall below is in the audio chain, which is exactly the sort of
thing `bootup_load` might have set up, so **re-run under `--off` before trusting
any new finding here.** Nothing has been attributed to it yet; it is a suspect.


The harness drives the game off live UI state, so this is one command:

    ./ghpc/scripts/build.sh --from=build
    cd work && GHPC_HIDE_WINDOW=1 GHPC_PAD_DRIVE=cross \
      ../build-debug/ps2xRuntime/ps2EntryRunner GH2_debug.elf

`GHPC_PAD_DRIVE` presses once per settled UI screen and logs every transition,
so the run reads as a path rather than a guess. Use `build-debug`, not
`build-calls`: the call histogram slows it enough to matter over minutes.

Expect this. Before the 128MB fix it then crashed about 6 minutes into
`loading_screen`; now it reaches `loading_screen` and loops there forever:

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

## The crash phase was not a hang and not a loop, which took proving

This section is about the **crash** phase, before the 128MB fix. The stall that
replaced it *is* a loop, see "Where it stops now" below.

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

Past the crash, the song still does not load. It sits in `loading_screen`
indefinitely, with no fatal and no `game_screen`.

### Settled: it is a loop, not slow progress

Two runs on `build-calls` with `GHPC_SAMPLE_MS=20000`. `call_hist_dump` already
prints a delta vector and resets its baseline, so every `[ghpc/calls]` line is
one 20 second window of work rather than a running total. Metric fixed before
the run: a loop repeats its delta vector, progress does not.

Baseline run: **15 of 15 consecutive sample pairs at cosine 1.0000 and Jaccard
1.0000**, with `active` pinned at exactly 199 functions for about five minutes.
The threshold was cosine >= 0.98 across three or more pairs. It is a loop.

Every count in the load phase is an exact multiple of the window's frame count
(278), so the whole steady state is per-frame polling.

### The read path is idle, not slow

| function | boot and menus | load phase |
|---|---:|---:|
| `ArkFile::ReadAsync` | 341652 | 0 |
| `BlockMgr::Poll` | 683121 | 0 |
| `BinStream::Read` | 3685292 | 0 |
| `BlockMgr::AddTask`, `ArkFile::ReadDone`, `Block::Matches` | live | 0 |

Nothing is queued and nothing is in flight. The storage stack is not the
problem here.

**Two claims in the previous version of this note were wrong**, both from
sampling the transition instead of the steady state:

- "It is doing real work the whole time, `BlockMgr::AddTask` climbed 216 -> 398"
- "`LoadMgr::AddLoader` does fire during `loading_screen`"

`AddLoader` is absent from all 20 steady-state samples. Both observations were
the transition into the screen, not the stall.

### The loader subsystem believes it already finished

`LoadMgr::Poll` (0x31da48) runs once per frame. Its loop body's first act is a
virtual call to vtable +0x1c, `Loader::PollLoading`:

    lw   $5, 0x8($2)     # loader out of the list node
    lw   $3, 0x14($5)    # vptr
    lw   $2, 0x1c($3)    # vtable +0x1c = PollLoading
    jalr $2              # unconditional, before IsLoaded

No `PollLoading` variant fires anywhere in either run, though all three
(`FileLoader`, `DirLoader`, `CharsysPanel::CacheModel`) are generated and
instrumented. So the loop body never runs and LoadMgr's list is empty.

`PollUntilLoaded` (0x31d928) is `while (!loader->IsLoaded()) loader->PollLoading()`,
with the test at 0x31da0c branching to the body on `beqz`. It is entered once
per frame and never reaches the body, so `IsLoaded` is returning **true**.

`FileLoader::IsLoaded` (0x31e2c8) is three instructions: `return *(this+0x18) == 0`.
`FileLoader::PollLoading` (0x31e2d8) bails on that same word being null and
otherwise drives vtable +0x6c (`ArkFile::ReadDone`).

Read together: the file loaders report loaded, LoadMgr has nothing queued, and
the stall is downstream of loading. Do not chase the loader path again.

Vtable layout for reference, 8 byte entries of (delta, pointer):
`_vt$10FileLoader` 0x45a578, `_vt$9DirLoader` 0x459f28, `_vt$6Loader` 0x45a5a0.
Slot +0x14 is `IsLoaded`, slot +0x1c is `PollLoading`.

### The SPU handshake is not the blocker either

`SPUSendBusy` and `SynthPoll` spin at exactly 66 per frame inside a once-per-frame
`SPUSendPoll`, which made the SPU look like the obvious suspect.

`GHPC_SYNTH_ACK=1` re-run, compared name-for-name against the baseline active set:

- gone: `SPUSendBusy`, `SynthPoll` (199 -> 197 functions)
- new: nothing
- the other 197 are identical, and it is still a loop (7 of 7 pairs at cosine 1.0000)

So the acknowledgement does clear the spin, and the game stays stuck in exactly
the same place. This retests the earlier `ui-draw-hang.md` conclusion against the
post-128MB stall rather than against the crash, and it still holds: the SPU stall
is real, unfixed, and not what blocks the song load.

### What is left: the gate is StreamEE, proven by control flow

The UI is not waiting to decide, it is stuck **mid-transition**. All 203
`[ghpc/ui]` samples in the stall are byte identical:

    cur=0xad1c20 next=0xad4b00 xition=1 name="loading_screen" nextName="game_screen"

So `game_screen` is already the committed target and the transition never
completes, which means `UIScreen::ArePanelsLoaded` on `game_screen` returns
false. It is `for each panel: if (!CheckIsLoaded()) return 0; return 1`
(0x24b4b0).

`GamePanel::IsLoaded` (0x1067c0) is a sequential gate chain, each gate reached
only if the previous returned true:

    1 UIPanel::IsLoaded          4 PicsAreLoaded__9GamePanel
    2 sub-panel CheckIsLoaded    5 IsReady__9BeatMatch
    3 IsLoaded__10BankLoader

`IsLoaded__10BankLoader`, `PicsAreLoaded__9GamePanel` **and** `IsReady__9BeatMatch`
all sit at 278 per window, exactly one per frame, so gates 1 through 4 pass every
frame and the last gate is reached every frame. `CreateBeatMatch__9GamePanel` and
`StartLoadingPics__9GamePanel` are absent from the set, so both had already run.
This is derived from control flow, not from assuming the audio chain because
audio is loudest in the histogram.

The tail below gate 5, every one at exactly one per frame, is a single chain:

    IsReady__9BeatMatch -> IsReady__11BeatMatcher -> IsReady__13PlayerMatcher
    -> IsReady__14BeatMatchAudio -> IsReady__11MasterAudio -> IsReady__C8StreamEE

`MasterAudio::IsReady` (0x277168) delegates through `this+0xc`, vtable +0x14.
`StreamEE::IsReady` (0x26ba28) is four instructions:

    lw    $2, 0x4c($4)
    addiu $2, $2, -3
    sltiu $2, $2, 3        # return (unsigned)(state - 3) < 3

So the entire song load hangs on one word, `StreamEE+0x4c`, never reaching a
value in {3, 4, 5}.

Two stores to `+0x4c` inside `Poll__8StreamEE` (0x26cf58, 1632 bytes):

- 0x26d400 writes **2**, reached after `VAGFileReader::SetBuffer` and a backward
  branch at 0x26d3f0. State 2 is not ready.
- 0x26d054 writes **3**, but it sits immediately after a `Debug::Fail` call at
  0x26d040 whose message is built by `MakeString`. Worth knowing whether that
  path is actually taken and whether `Debug::Fail` returns here.

### Measured: the state word is pinned at 2

`GHPCSTRM` hooks `StreamEE::IsReady` (0x26ba28), which is called once per frame
with the object in `$a0`, and reads `+0x4c` out of guest memory. It prints on
change and on a 600 call heartbeat, so a stuck value costs one line and "still
stuck" stays distinguishable from "probe stopped firing".

Three `StreamEE` objects are built over one run. Every one went 1 -> 2 and froze
there, the live one holding state 2 across 3600 calls and six heartbeats while
the screen never advanced. That matches the `0x26d400` store, which writes 2
after `VAGFileReader::SetBuffer`.

## Answered: nothing on the EE advances 2 -> 3, it is an IOP callback

Traced 2026-09-10 by disassembly, each link proven by control flow rather than
inferred.

**Only StreamEE writes `StreamEE+0x4c`.** A whole-`.text` scan for `sw $r, 0x4c($r)`
resolved against symbol boundaries gives 162 functions, of which exactly six are
StreamEE: `Init`, `Prepare(float)`, `Destroy`, `Play`, `Stop`, `Poll`. Every other
hit is a different class's own `+0x4c`. That closes the "does anything outside
`Poll__8StreamEE` write it" question: no.

**The state enum, read off the writers:**

    0  idle          Poll, op type 1
    1  preparing     Prepare(float) 0x26b7fc
    2  buffered      Poll 0x26d400, after VAGFileReader::SetBuffer
    3  ready         Poll 0x26d054, op type 2 ONLY
    4  playing       Play 0x26bb54
    5  stopped       Stop 0x26bb0

`IsReady` is `(unsigned)(state - 3) < 3`, so {3, 4, 5}. `Play` refuses to move
2 -> 4: it early-exits on 4 and only runs its channel update loop on 3.

**`Poll__8StreamEE` is two stages.** First it drains a `StreamOp` vector at
`this+0x2c` (8 byte elements, type at `+0x0`); then it dispatches on the state
word through a jump table at **0x4b1060**:

    0 -> 0x26d54c  fall through to the tail
    1 -> 0x26d1ac  VAGReaderFactory::IsReady, GetReader, allocate StreamingBuffers,
                   SetBuffer, then write 2
    2 -> 0x26d404  send StreamInfoArg out as CTL 0x190, latch this+0x128, return
    3 -> 0x26d54c
    4 -> 0x26d508  start the VarTimer
    5 -> 0x26d54c

**State 2's own handler never writes `+0x4c`.** It sends and waits. The 2 -> 3
step lives in the op drain: op type 2 at 0x26d020 checks `state == 2` (and
`Debug::Fail`s otherwise) then writes 3 at 0x26d054.

**The op comes from the IOP.** The only pusher is `Dispatch__8StreamEEiiUi`
(0x26d5b8): it resolves a stream id to a `StreamEE*` through the global list at
0x51a860, returns early on op type 3 (that one just stores the position to
`+0x170`), and otherwise pushes `StreamOp{op, arg}`. Its only caller is
`CtlDispatch_impl__7SynthEE` (0x3eb828), whose jump table at **0x4eb680** maps
CTL command -> handler, and entries 1 and 2 both land on 0x3eb888:

    $4 = data[0]      the stream id
    $5 = cmd          still live from the dispatcher, so the CTL command number
                      IS the StreamEE op type
    $6 = 0

`CtlDispatch__7SynthEE` is registered as the EE-side RPC server handler
(`CtlServerInit`, handler slot 0x444d74) and reached from the packed
`{cmd, len, data}` record walker at 0x269c58.

So the shape is: **the EE's state 2 handler sends `StreamInfoArg` to the IOP as
CTL 0x190 and blocks until the IOP answers with CTL command 2.** The runtime has
no IOP synth module, so the answer never comes and the state word sits at 2
forever. This is the same missing-module class as the SPU handshake, not a
separate bug.

## CONTESTED: standing in for that callback, result not established

`GHPC_STREAM_READY=1` (`ps2_runtime.cpp`, in the `GHPCSTRM` hook, labelled a
probe like `GHPC_SYNTH_ACK`) writes 3 to `+0x4c` after the word has been pinned
at 2 for 120 consecutive `IsReady` calls on the same object. The delay is there
so state 2's own one-shot send runs first. It writes the word rather than
pushing the op because that is the op's only observable effect.

**Read the caveats below before using this.** The oracle scored `PROGRESSED`,
rung 8 -> 9, three times. That verdict is close to worthless on its own:
`progress.py` breaks out of its read loop the instant it sees the top rung, so
all three runs were killed on the transition line and observed zero frames
afterwards. The run below is **`bootskip.py --off`**, the full boot:

    [ghpc/strm] #309 this=0x00b926c0 state=1 ready=0 CHANGED
    [ghpc/strm] #310 this=0x00b926c0 state=2 ready=0 CHANGED
    [ghpc/strm] #429 this=0x00b926c0 STAND-IN wrote state=3 (IOP CTL cmd 2 never arrived)
    [ghpc/strm] #429 this=0x00b926c0 state=3 ready=1 CHANGED
    [drive] loading_screen -> game_screen after 0 presses (t=107.9)

A later uncapped run on `build-debug` (330s, probe on) does hold the screen:
reached at t=73.6, then `cur=0xad4b00 next=0x0 xition=0 name="game_screen"` for
the remaining ~256s, frames still advancing (contentFrame 1097 -> 1234, presents
4200), zero `Debug::Fail`, zero exits.

**Open contradiction, do not treat this as settled.** Running vanilla
`./ghpc/scripts/run.sh` (release, where `GHPC_STREAM_READY` is compiled out:
`strings` finds it in `build-debug` and not in `build`) alternates forever
between the loading screen and a mostly dark frame. Either the screen is
reachable without the probe, which would break the causal claim entirely, or
what alternates is the present path rather than a UI transition. Release carries
no `[ghpc/ui]` or `[drive]` tracing, so this is unresolved.

**No control was ever run.** There is no same-build A/B with the probe off. The
`PROGRESSED` verdicts compare against a mark recorded from an earlier build, not
against a control arm.

**This is a probe, not a fix.** The real work is an IOP-side synth stream module
that answers CTL 0x190 with CTL command 2.

## Ruled out, with evidence. Do not re-chase these.

- **Not a loop, in the crash phase.** Two call-histogram samples across the load
  differ in character rather than repeating: `BinStream::Read` 256785 ->
  1129591 while `Heap::InsertFreeBlock` falls 39891 -> 3704.
- **Not slow.** A 20 minute `build-debug` run with no histogram died at about 7
  minutes rather than finishing.
- **Not the heap, since 128MB.** `PS2_RAM_SIZE` at 128MB (commit `6babe38`)
  retired `out of dynamic memory in yy_create_buffer()`. The census proved the
  shortfall was real and about 20KB wide, not an allocator giving up early.
- **Not the `[libc] guest FILE*` line.** 0x448d8c is inside `impure_data`
  (0x448af8, 748 bytes), so it is newlib's `_sf` stderr, printed by
  `yy_fatal_error`'s own `fprintf` one call before the message it carries.
- **Not the SPU handshake.** `GHPC_SYNTH_ACK=1` removes exactly `SPUSendBusy`
  and `SynthPoll` from the 199 function working set, adds nothing, leaves the
  other 197 identical, and the stall stayed byte for byte where it was. Real and
  unfixed, but it blocks neither the menus nor the song load.
- **Not the read path.** In the stall `ArkFile::ReadAsync`, `BlockMgr::Poll`,
  `BinStream::Read`, `BlockMgr::AddTask`, `ArkFile::ReadDone` and
  `Block::Matches` are all at **0** per window. Nothing queued, nothing in
  flight.
- **Not the loader subsystem.** No `PollLoading` variant fires in any
  steady-state sample though all three are generated and instrumented, so
  `LoadMgr`'s list is empty and `FileLoader::IsLoaded` is returning true.
  Vtable slots for reference, 8 byte (delta, pointer) entries: `_vt$10FileLoader`
  0x45a578, `_vt$9DirLoader` 0x459f28, `_vt$6Loader` 0x45a5a0, `+0x14` is
  `IsLoaded` and `+0x1c` is `PollLoading`.
- **Not a UI decision.** All 203 `[ghpc/ui]` samples in the stall are byte
  identical at `xition=1 name="loading_screen" nextName="game_screen"`, so
  `game_screen` was already the committed target.
- **Not gates 1 to 4 of `GamePanel::IsLoaded`.** `IsLoaded__10BankLoader`,
  `PicsAreLoaded__9GamePanel` and `IsReady__9BeatMatch` all sit at exactly one
  per frame, so the chain reaches the last gate every frame.
- **Not anything outside `Poll__8StreamEE` writing `+0x4c`.** Whole-`.text` scan,
  above.
- **Not `Debug::Fail` returning into the state 3 store.** 0x26d054 is a separate
  jump target reached by the op-type-2 branch at 0x26d048; the `Fail` above it
  at 0x26d040 is the `state != 2` assert, not something the success path walks
  through. This was listed as open and it is now closed.

Useful reference facts found the hard way:

- `String` keeps its text buffer pointer at **+0x10** (`__as__6StringPCc` at
  0x325a40 does `strcpy(this + 0x10, src)`). `FilePath` derives from it.
- The ARK stream class is `ArkFile`, vtable `_vt$7ArkFile` at 0x459a88.
- Every 1, 2 and 4 byte read goes through `ReadAsync` plus a `ReadDone` spin.
  ~341k of each during boot and menus alone. Normal for this engine, wasteful.

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
- `GHPC_STREAM_READY` stands in for the missing IOP CTL command 2. Also a probe.

Two defects in the diagnostics themselves, found while using them:

- **`call_hist_dump` corrupts its own output.** It streams field by field with
  `<<` from a detached host thread, so guest stderr splices into the middle of a
  line. 7 of 27 samples in one run were hit, and one produced a fabricated
  function named `[vif1] opcode histogram after 3184400: 0x0` with 1.79M hits,
  which is enough to swing a cosine comparison. Until it formats into one buffer
  and emits a single write, drop any `[ghpc/calls]` line containing more than one
  `[`. The analysis script in `.planning/histdelta.py` does this and refuses to
  give a verdict on fewer than 5 clean samples.
- **`EeScheduler.cpp:102,109,146,185` still mask guest addresses with
  `0x01FFFFFFu`**, which is 25 bits, so 32MB. Same bug class as the heap census
  truncation fixed in 350741d, and now wrong under the 128MB map.
