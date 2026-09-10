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

## Settled: the stand-in causes the transition, and the transition is not gameplay

`GHPC_STREAM_READY=1` (`ps2_runtime.cpp`, in the `GHPCSTRM` hook, labelled a
probe like `GHPC_SYNTH_ACK`) writes 3 to `+0x4c` after the word has been pinned
at 2 for 120 consecutive `IsReady` calls on the same object. The delay is there
so state 2's own one-shot send runs first. It writes the word rather than
pushing the op because that is the op's only observable effect.

**This section used to say the result was contested and that no control had ever
been run. Both are now closed.** 2026-09-10, `build-debug`, same binary, same
`bootskip.py --on`, 300s cap, 60s hold, one attempt each, `GHPC_PROBE` set
identically on both arms so the only variable is the stand-in:

| arm | verdict | held rung | `GamePanel::Poll` calls | `PlayerMatcher::Poll` calls |
|---|---|---|---|---|
| `GHPC_STREAM_READY=1` | PROGRESSED | 9 `game_screen`, no bounce | 2 | 0 |
| probe off (control) | SAME | 8 `loading_screen`, no bounce | 0 | 0 |

So the causal claim holds: the stand-in is what moves rung 8 to rung 9, and the
screen sticks for the full 300s rather than bouncing. The earlier
`./ghpc/scripts/run.sh` contradiction is not evidence against it; that was a
release build with no `[drive]` tracing, so it was never a measurement of the
same thing.

**But rung 9 is not gameplay, and now there is a number that says so.** Under
the stand-in, at `game_screen`, for 300 seconds:

- `Poll__13PlayerMatcherfRC7SongPos` (0x117dd0): **0 calls**
- `Poll__6PlayerfRC7SongPos` (0x112fb0): **0 calls**
- `Poll__9BeatMatch` (0x1259c0): **0 calls**
- `Poll__11BeatMatcherf` (0x2727c0): **0 calls**
- `Poll__9GamePanel` (0x107140): **2 calls in 300s**
- `Poll__8StreamEE` (0x26cf58): hot, the probe's 40 line cap reached

The entire beatmatch and player chain never runs. Nothing reads a `SongPos`,
so no chart exists to advance. `GamePanel::Poll` firing twice and then never
again is answered two sections down: the main EE thread dies in
`SynthEE::Terminate` right after the game screen's first frame.

The state word does not stick either. Same run, one object, three lines apart:

    [ghpc/strm] #309 this=0x00b80540 STAND-IN wrote state=3 (IOP CTL cmd 2 never arrived)
    [ghpc/strm] #309 this=0x00b80540 state=3 ready=1 CHANGED
    [ghpc/strm] #311 this=0x00b80540 state=2 ready=0 CHANGED

`ready=1` lasts long enough for `GamePanel::IsLoaded` to pass once, then the
game puts the word back to 2. That is consistent with the op never having been
pushed: the stand-in forges the op's result and not the op, so whatever else
draining a type 2 `StreamOp` sets up never happens. There are also several live
`StreamEE` objects in one run (0x00e88c70, 0x00ec1900, 0x00b80540, 0x00fa31e0),
which the hook's single pinned-object tracker was not written for.

**This is a probe, not a fix, and it is now clear it could never have been one.**
The real work is an IOP-side synth stream module that answers CTL 0x190 with CTL
command 2.

## What actually stops rung 9: SynthEE::Terminate spins forever

Measured 2026-09-10, `build-debug`, `GHPC_STREAM_READY=1`, `bootskip.py --on`,
300s, `GHPC_PROBE_EVERY=25`. The sequence at the top of the ladder, read off one
log by line number:

    22336  [probe] 0x24abd8 #600   UIScreen::Poll, still running normally
    25275  [drive] loading_screen -> game_screen (t=66.7)
    25278  [probe] 0x107140 #2     GamePanel::Poll, the game screen's one frame
    25305  [ee/stall] thread id=1 pc=0x268458 ra=0x268458 status=running
    ...    the same census 23 more times, count still climbing, for ~230s

`0x268458` is `Terminate__7SynthEE + 0x40`. The main EE thread enters
`SynthEE::Terminate` right after the game screen's first frame and never comes
out, which is why `GamePanel::Poll` runs exactly twice and the whole UI poll
stops: `UIManager::Poll` (0x24c5c0, the caller at 0x24c710) never returns.

The loop is a bare wait on a word the IOP is supposed to write:

    268430  jal  CtlClientCall(1)          ; ask the IOP for CTL command 1
    268438  lw   $2, 0x4130($18)           ; SynthEE+0x4130, the done flag
    26843c  bnez $2, 0x268464              ; already done, skip the wait
    268448  jal  CtlClientPoll()
    268450  jal  Timer::Sleep(10)
    268458  lw   $2, 0x4130($18)
    26845c  beqz $2, 0x268448              ; spin until the IOP sets it

Confirmed from two directions in the same censuses: `pc` alternates between
0x268458 (the flag reload) and 0x2f2cf8 (`Timer::Sleep + 0x50`) while `ra` stays
0x268458 throughout, so it is one loop and not a coincidence of sampling.

**This is the same missing-module class as CTL 0x190, but a different command
and a worse failure.** 0x190 leaves the state word pinned and the game polling;
`Terminate`'s CTL command 1 takes the main thread out entirely. Servicing 0x190
alone would not have fixed this, because the game only reaches `Terminate` after
the screen it was waiting for.

`SynthEE+0x4130` is the flag to satisfy, `CtlClientCall` is 0x269b38 and
`CtlClientPoll` is 0x269bd0.

**Control arm, same build, same probe set, stand-in off.** `SynthEE::Terminate`
is entered **zero** times, the thread is never pinned at 0x268458, and
`UIScreen::Poll` reaches **#4375** against #600 in the stand-in arm. The control
run emits 118,972 log lines to the stand-in run's 26,709. So the spin is not a
pre-existing condition that the stand-in merely reveals: the forged state word
is what leads the game into `Terminate`.

That inverts how `GHPC_STREAM_READY` should be read. It does not just fail to be
a fix. Rung 9 under the probe is a **worse** guest state than rung 8 without it:
one screen frame, then a dead main thread, against a game that is still polling
its UI seven times as much. Any future round tempted to leave the probe on for
convenience should read that line twice.

## Answered: GamePanel::Poll runs twice because the thread dies, not because the panel is paused

`UIScreen::Poll` (0x24abd8) walks its panel list at 0x24ad28 and skips any panel
whose word at **+0x0c** is nonzero:

    24ad2c  lw    $5, 0x8($2)      ; panel = node->data
    24ad30  lw    $3, 0xc($5)      ; mPaused
    24ad34  bnezl $3, 0x24ad58     ; nonzero, skip the Poll
    24ad3c  lw    $2, 0x38($5)     ; vptr, slot +0x30 pair
    24ad48  jalr  $3               ; panel->Poll()

+0x0c is `mPaused`, from the decomp's `UIPanel` layout, and the only writers in
the whole `.text` are `UIPanel`'s constructor (0x245708, writes 0) and
`GamePanel::SetPaused` (0x108090). Both `SetPaused` overrides are reachable only
through vtable slot +0x38; there is no `jal` to either anywhere in `.text`.

**Measured, and it is not the pause.** `GamePanel::SetPaused` fires exactly once
in a 300s run, `a1=0x0`, from 0x106f78 inside `GamePanel::Enter`. It is never
called with true. `mPaused` is 0 for the entire life of the panel, so the skip
branch is not what stops the Poll. Ruled out before the `Terminate` spin was
found, and worth keeping ruled out: "the panel is paused" is the obvious first
guess and it is wrong.

## Done: the IOP synth service, and the assert behind it

Built 2026-09-10 as `ps2xIOP/src/modules/synth.cpp`. It answers the CTL link and
the song load gate opens for real. The whole protocol was read off the debug ELF
rather than guessed.

**Both sids, from the ELF.**

    SynthEE::SynthEE        0x268204  lui $4,0x7543 / ori $4,0x3178
                                      -> CtlClientInit(0x75433178)   EE -> IOP
    SynthEE::ServerThreadEntry
                            0x26857c  lui $5,0x7543 / ori $5,0x3179
                            0x268584  $6 = 0x268760
                                      -> CtlServerInit(_, 0x75433179,
                                         CtlDispatch__7SynthEE)       IOP -> EE

**The EE half is a queue, not a call.** `CtlClientCall(cmd, data, len)` appends a
packed `{cmd:u32, len:u32, data[len]}` record, no padding, to a buffer at
0x4FA6B0 with its cursor at 0x444D70. `CtlClientPoll` flushes the whole queue
with a single `sceSifCallRpc(rpc=0, mode=NOWAIT)` and **no receive buffer**. So
nothing written into a reply buffer is ever read. The answer has to arrive as a
call into the EE's own handler, which is why a "reply with the right bytes"
approach cannot work here.

**The reply command space is 0..14, and is unrelated to the request space.**
`CtlDispatch_impl` bounds checks with `sltiu $2, $5, 0xf` and indexes a 15 entry
table of raw addresses at 0x4EB680. Requests run much larger (0x190, 0x136,
0x133 all observed). The two entries that matter:

    entry  2 -> 0x3EB888  lw $4, 0($8); StreamEE::Dispatch(id, 2, 0)
                          the only thing that makes Poll write state 3
    entry 14 -> 0x3EBA10  sw 1, 0x4130(this)
                          the only writer, outside the constructor, of the word
                          SynthEE::Terminate spins on

**`data[0]` is the stream id, confirmed by the data rather than assumed.** The
service logs each `StreamInfoArg`, and the leading words across one run are
`0x0, 0x1, 0x2, 0x3, 0x4` on `len=84` payloads. Sequential ids are not something
a wrong offset produces. Entry 2's handler reads that word straight out of the
buffer it is handed, so the EE's own payload is passed through rather than
copied into a scratch allocation.

**Observed EE -> IOP commands** in one boot, for whoever implements the rest:
`0x0` (len 24), `0x3` (20), `0x4` (20), `0x5` (4), `0x133` (4), `0x136` (4),
`0x190` (84).

**Result, stock build, no probes, against a control arm on the same build:**

| arm | verdict | held rung | peak | unhandled 0x190 | strm state | CharBones assert |
|---|---|---|---|---|---|---|
| service on | REGRESSED | 0, died at 94.6s | 9 `game_screen` | **0** | **3, no STAND-IN** | exit(1) |
| service off | SAME | 8 `loading_screen` | 8 | 4 | 2 | none |

Both mechanism checks pass on the arm that matters: the `[IOP/RPC
trace:unhandled]` line for 0x190 is gone, and `[ghpc/strm]` reaches state 3 with
no STAND-IN line, on a build with no `GHPC_*` set. `GHPC_STREAM_READY` is
retired by this; it was never needed, only misleading.

**The new blocker, which is a different bug:**

    [assert] Fail depth=0 latch=0 from=0x1b7b60
             msg="File: CharBonesSamples.cpp Line: 114 Error: *frac >= 0?"
    [guest] exit(1) called from ra=0x002ebf74. Run is stopping.

Character bone animation sampling with a negative interpolation fraction. It was
always there and simply unreachable, because the game never got far enough to
animate a character. It does not appear in the control arm at all.

**Why the service ships opt-in behind `GHPC_SYNTH_IOP=1`.** Measured on held
rung this is 8 -> 0, because a run that exits at 94s cannot hold anything for
the 60s window. Default-on would hand every later round a floor that dies before
it can score, and the floor is what keeps an unattended loop honest. So it is
off by default until the assert is fixed, then the switch flips and it gets
scored properly. The floor was re-measured after flipping the switch: rung 8,
held 300s, no probes, `SAME`.

## Done: CVT.W.S was rounding, not truncating

`ps2_runtime_macros.h` had:

    #define FPU_CVT_W_S(a) ((int32_t)nearbyintf((float)(a)))

`nearbyintf` rounds to nearest. The R5900 rounds CVT.W.S toward zero, so every
`(int)someFloat` in all 12,664 recompiled functions was rounding instead of
truncating. Now `((int32_t)(float)(a))`.

**Three independent reasons it was wrong, none of them "it felt off".**

1. The R5900 rounds CVT.W.S toward zero.
2. The game proves it. `FracToSample`'s other path adds 0.5 (the `0x3f00`
   literal at 0x1b7ab4) before its `cvt.w.s` to get round-to-nearest. That idiom
   is only correct against a conversion that truncates; against a rounding one
   it double-rounds.
3. `FPU_CVT_L_S` in the same table was already `((int64_t)(float)(a))`, a
   truncation. Two members of one instruction family disagreeing is the tell.

**How it surfaced.** `CharBonesSamples::FracToSample` (0x1b7a28) clamps `*frac`
to [0,1] at 0x1b7a60, multiplies by `n-1`, truncates to get the sample index,
then subtracts: `f = frac*(n-1) - (int)(frac*(n-1))`. That remainder is
non-negative by construction and the function asserts it
(`CharBonesSamples.cpp:114 "*frac >= 0?"`). With a rounding conversion, 2.7
becomes 3 and the remainder is -0.3, so the assert fires and `Debug::Fail` calls
`exit(1)`. It only ever fired once the synth service let the game reach a screen
that animates a character.

## Rung 9, stock build, no probes

2026-09-10. Three arms, all on the same build, 300s cap, 60s hold:

| arm | verdict | held rung | assert | guest exit | song_tick |
|---|---|---|---|---|---|
| synth on, rounding fixed | PROGRESSED | 9 `game_screen` | none | none | never fires |
| synth off, rounding fixed | SAME | 8 `loading_screen` | none | none | never fires |
| stock, synth now default on | PROGRESSED | **9 `game_screen`** | none | none | never fires |

The stock arm is the one that counts: `env: {}`, no `GHPC_*` at all, rung 9 held
for the full 300s with no bounce, recorded as the mark. Mechanism checks both
pass: **zero** `[IOP/RPC trace:unhandled]` lines and **zero** `STAND-IN` lines,
with `[ghpc/strm]` reaching `state=3 ready=1` on its own.

The middle arm is the control that matters for the rounding change, because that
macro is used by every float-to-int conversion in the binary. The floor is
undisturbed with the service off, so the blast radius did not cost anything
visible.

**This is not the goal.** `song_tick` has never fired in any run of this
session, with or without probes, before or after these fixes.
`PlayerMatcher::Poll` (0x117dd0) is not called even at a `game_screen` that
holds for 300 seconds. A held screen is still not a chart advancing, which is
the whole reason that sub-rung exists.

## The chart is gated by GamePanel+0x70, and round two was measuring the wrong world

Measured 2026-09-10 on the rung 9 build with `GHPC_PROBE` and
`GHPC_PROBE_EVERY=25`, so no rebuild and the binary is exactly the one that
recorded the mark.

**Round two's poll census does not transfer and is now superseded.** It found
`GamePanel::Poll` at 2 calls and concluded the panel was barely polled. That was
under `GHPC_STREAM_READY`, where the main thread died in `SynthEE::Terminate`
seconds after the transition. Reaching `game_screen` legitimately gives a
completely different picture:

    GamePanel::Poll        31
    GetGameExcitement      31
    SetExcitementLevel     31
    BeatMatch::Poll         0

All three at exactly 31, one per `GamePanel::Poll`, every call arriving from
0x24ad48 (the `UIScreen::Poll` panel loop). The panel is polled once per frame
and takes the normal path. `mPaused` was never the gate.

**The gate is `GamePanel+0x70`.**

    1071ac  lw   $16, 0x58($17)     ; the BeatMatch
    1071b0  lw   $2,  0x70($17)
    1071b8  beqz $2, 0x1071fc       ; zero -> BeatMatch::Poll, the chart runs
    1071c0  ...                     ; non-zero -> a TaskMgr::UISeconds block
    1071f4  b    0x107220           ; ...which branches PAST BeatMatch::Poll
    1071fc  jal  Poll__9BeatMatch

So `+0x70` is non-zero and the chart is skipped every single frame. Past
0x107220 there are two more gates before the game can start: `+0x88` must be
zero (`bnezl` at 0x107224 skips ahead) and the accumulated time in `$f20` must
exceed the `0xbccccccd` threshold at 0x10723c, at which point `StartGame`
(0x1070f8) runs and `BeatMatch::Poll` is called at 0x107254. Neither happens.
`StartGame` is never called, which is independently consistent with
`BeatMatch::Poll` sitting at zero.

**A hypothesis this round killed by measuring it.** The obvious read of the top
of `GamePanel::Poll` is that `+0x84` being non-zero makes it call `StartIntro`
and return early every frame, which would explain everything. It does not:
`StartIntro` (0x1074e8) fires **exactly once** in a 240s run. The early return is
not the path, and the disassembly alone would have sent the next round after the
wrong flag.

## The frame rate collapses about 50x on entering game_screen

Same runs, counting the runtime's own `[frame]` lines against the `[drive]`
transition at t=58.9:

    before game_screen   493 frames in  58.9s   ~8.4/s
    after  game_screen    39 frames in 241.1s   ~0.16/s

`UIScreen::Poll`, `GamePanel::Poll` and `StreamEE::Poll` all track that rate, so
it is the whole guest slowing down rather than one subsystem stopping. The
`[vif1]` histogram reaches 1.66M opcodes and `[vu1] exec 0x1160` reaches 193k,
so the cost is the venue and characters actually being drawn through the
software rasteriser. `[drive] STUCK on game_screen, 12 presses had no effect` is
the pad driver noticing the same thing.

This is the `Rnd` seam already in `BACKLOG.md` under Next, and it is much worse
here than the 39% of realtime measured on menus. It is not what stops the chart:
even at 0.16 fps a running chart would move `song_tick`. But it means "playable"
needs the seam regardless of the gate.

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
- **Not a measurement gap in the ladder any more, and not gameplay.** The
  oracle now carries a `song_tick` sub-rung from `PlayerMatcher::Poll`
  (0x117dd0), so `game_screen` no longer scores a win by itself. Measured
  2026-09-10: the hook is wired correctly and the function is simply never
  called. Proven by a control that does not touch the new code at all,
  `GHPC_PROBE=0x117dd0,0x26ba28` in the same run: 0x26ba28 (`StreamEE::IsReady`)
  hit its 40 line cap while 0x117dd0 logged zero. "The probe is broken" and "the
  chart never polls" are different claims and this separates them.
- **Not the release/debug contradiction.** The `run.sh` alternating-frames
  observation was a release build with no `[drive]` or `[ghpc/ui]` tracing, so
  it never measured the screen at all. The same-build A/B above supersedes it.

- **Not `mPaused`, and not the `UIScreen::Poll` panel skip.** Measured above:
  one `SetPaused(false)` call in 300s and no `SetPaused(true)` at all.
- **Not the GamePanel vtable.** `_vt$9GamePanel` (0x449868) slot +0x30 holds
  `Poll__9GamePanel` (0x107140), not the `UIPanel` base version, and the
  runtime probe confirms the call arrives with `a0` = the GamePanel. The
  1000-plus `UIPanel::Poll` calls are other panels, not a mis-slotted GamePanel.

- **`IopHost::invokeGuestFunction` is a stub. Do not build on it.**
  `ps2xRuntime/src/lib/ps2_iop_host.cpp:485` ignores every argument, sets the
  result to 0 and returns false. The working path for an IOP service that must
  run EE code is `RpcResult::guestFunction` plus `guestArguments`, consumed at
  `Kernel/Syscalls/RPC.cpp:572` and dispatched at 722 as a single
  `GuestInvocation`. That is **one guest call per RPC**, which is why the synth
  service picks one record per flush to answer and counts the rest.
- **The IOP cannot answer this link through a reply buffer.** `CtlClientPoll`
  calls with no receive buffer at all, so there is nothing to write into.

- **Not a game bug: `CharBonesSamples.cpp:114`.** It was the runtime's CVT.W.S
  rounding mode, above. The assert was correct and the guest code was correct.
- **Not the synth service's blast radius.** With the rounding fixed and the
  service off, the same build still holds rung 8 for 300s.

- **Not `mPaused`, and not the panel being unpolled.** Superseded measurement:
  `GamePanel::Poll` runs once per frame at a legitimate `game_screen`, 31 calls
  against 31 `GetGameExcitement` and 31 `SetExcitementLevel`.
- **Not `GamePanel::Poll` returning early into `StartIntro`.** `StartIntro`
  fires exactly once in 240s. Measured, not reasoned.
- **Not the frame rate, for the chart specifically.** The guest drops to about
  0.16 fps at `game_screen`, which makes it unplayable but would not hold
  `song_tick` at zero; a running chart advances at any frame rate.

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
- `GHPC_PROBE_EVERY` sets the watch's print cadence after the first 40 hits.
  The old fixed 1000 could not answer the question this probe is usually asked,
  which is not "is it called" but "did it stop being called": at 1000 a function
  that ran 600 times and died looks identical to one that ran 600 times and kept
  going. At 25 the same watch is a timeline, and it needs no rebuild.
- `[ghpc/song]` prints `PlayerMatcher::Poll`'s `SongPos`, which is what makes
  "the chart is advancing" measurable rather than assumed. The ABI was read off
  the prologue rather than assumed: `move $16, $4` / `mov.s $f20, $f12` /
  `move $17, $5` gives `Poll(this=$a0, ms=$f12, pos=$a1)`, the float in an FPR
  with the pointer keeping its integer slot. `SongPos` is 0x14 bytes and only
  its leading float is read back, so that word is the position.
  `scripts/progress.py` turns it into three sub-rung fields, `song_tick`,
  `song_tick_from` and `song_tick_advanced`, because "polled once and never
  again" and "polled 4000 times at a standstill" are the same verdict but
  different bugs.

One more macro in the same table looks wrong and was deliberately left alone,
because a round changes one thing: `FPU_ROUND_L_S` uses `roundf` (half away from
zero) while `FPU_ROUND_W_S` uses `nearbyintf` (half to even). MIPS ROUND.x.S is
half-to-even, so the 64 bit one is the odd one out. Nothing is known to depend
on it yet.

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
