# The UI draw hang

Written after a session that spent most of its time aimed at problems the
build had already moved past. The first section exists so that does not happen
again.

## Corrections from the 2026-09-09 session. Read these first.

**The answer: there was no hang.** `GHPC_PAD_AUTO=cross` held CROSS down
instead of pulsing it, so the game got one press edge for the whole run. Fixing
the duty cycle walks the build into Quick Play. Everything else in this file is
either a genuine second bug or a wrong turn, and both are labelled below.

Three claims below are wrong and each of them cost time. They are left in
place with the evidence that killed them, not deleted, so nobody re-derives
them.

1. **The stuck screen is not the memory card load screen.** It is the "Using
   the Guitar Controller" instruction screen, the one with the guitar diagram
   and a single `CONTINUE` help bar button. Captured with
   `GHPC_FRAME_ONCHANGE=1`, which dumps only when the picture changes and so
   costs one frame on a static screen. Everything below reasoning about a
   memory card wait is history. The help bar, `ButtonHBElement`,
   `LabelHBElement` and `BandLabel` in the steady state should have been the
   tell: a modal load screen has no help bar with a button on it.

2. **There is no hang inside `UIManager::Draw`.** `App::Run` runs 64 times per
   census in the steady state, and `BeginDrawing`/`SwapBuffers`/`vblank_e_handler`
   all run 32 times. The main loop iterates freely at full rate. The saved-context
   chain in "Where the hang is" names where the thread last yielded, which is
   just wherever the census happened to land inside a healthy draw. Both loop
   candidates in "The only two loops in that chain" are dead, as that section
   already suspected for a different reason.

3. **The frame is not byte-identical.** It alternates between two buffers,
   `nonBlack=200646` at `displayFbp=0` and `nonBlack=179748` at `displayFbp=56`.
   The second is a torn, half-drawn picture. That is a separate presentation
   bug, the runtime presenting a buffer mid-draw, and it is not the blocker.

### A real stall, but not the blocker: the SPU send handshake

Confirmed, and confirmed fixed, this session. The chain, all resolved out of
`GH2_debug.elf`:

    SPUStartSend(src,size,dst)   0x26e3f0   asserts !SPUSendBusy, sets
                                            gSpuPending = 0x444d90
    SPUSendPoll                  0x26e4c8   if !pending return; if inflight
                                            return; inflight = 1; ship one
                                            chunk of at most 0x5000 bytes
    SPUSendBusy                  0x26e478   return pending || inflight
    SPUSetSendDone               0x26e4b8   inflight = 0

    gSpuPending   0x444d90
    gSpuInFlight  0x444d94

`SPUSetSendDone` has exactly one caller, `SynthEE::CtlDispatch_impl`
(0x3eb828) at +0x1d8. That is the inbound IOP to EE dispatcher: a 15 entry
jump table at **0x4eb680**, and **message id 13** is the arm that calls it.
The EE receives those messages on **its own RPC server, sid 0x75433179**,
registered by `SynthEE::ServerThreadEntry` (0x268568) via `CtlServerInit`,
which then parks in `sceSifRpcLoop`. Outbound is the other half of the pair,
sid **0x75433178**, the one already noted below as unhandled.

So with no SYNTH_R module on the IOP, nothing ever calls EE server
0x75433179, message 13 never arrives, `gSpuInFlight` latches at 1 and
`SPUSendBusy` is busy forever. Every `SynthSamplePs::SynthPoll` then blocks,
which is why it and `SPUSendBusy` each burn 1472 calls per census in the
steady state.

Measured with a `[ghpc/spu]` census probe:

    early boot   pending=0          inflight=0
    line 2310    pending=0x1990410  inflight=1     latches here
    line 10245   pending=0          inflight=1     pending drained, still busy
    to the end   pending=0          inflight=1

Note it latches at line 2310, long before the load burst at 10169. Audio has
been dead since early boot.

`GHPC_SYNTH_ACK=1` (in `RPC.cpp`, an explicitly labelled probe, not a fix)
clears `gSpuInFlight` after an unhandled call to sid 0x75433178, standing in
for the missing module. With it on, `inflight` stays 0 for the whole run and
`SynthPoll` and `SPUSendBusy` **disappear from the steady state entirely**,
active count 175 -> 167. The sample pipeline drains. That is the audio stall
solved in principle, and it confirms the mechanism.

### It did not unstick the screen

Same run, same instruction screen, same two alternating buffers, same content
frame count. So the SPU stall is a real bug on the M5 audio path and worth
fixing properly with a module, but it is **not** what pins this screen.

### The screen blocker was the test harness, not the game

`GHPC_PAD_AUTO=cross` held CROSS down for the entire run. In `Pad.cpp`:

    const double period = (s_auto == 2) ? 0.33 : 1.0;
    const double phase  = span - (cycle * period);   // phase in [0, 0.33)
    if (phase < 0.35)                                // never false

`phase` cannot reach 0.35 when the period is 0.33, so the press was a level,
not an edge. The UI reacts to a press edge, so the game saw exactly one CROSS
at t=6.0s and none after. Every screen past the first stopped advancing. The
function's own comment says "Pulsed rather than held, because the UI reacts to
a press edge, not a level", which is what the `cross` mode broke.

Fix is a duty cycle expressed as a fraction of the period:

    const double duty = period * 0.5;
    if (phase < duty)

With that alone, no other change, the build walks the title screen ("PRESS ANY
BUTTON TO BEGIN", rendering correctly), past the guitar instruction screen, and
into **Quick Play**. The last census is 903914 `BinStream::Read`, 23824
`RndMesh::Vert` and 23224 `RndMesh::Face` deserialisations and
`Hmx::Object::Load`, with the active-function count swinging 204 to 1494. That
is scene loading, not a hang.

**The lesson, and it is the same one this file already teaches.** Two sessions
were spent on the game when the harness driving it was broken. Before
attributing a stall to guest code, verify the input actually being delivered is
the shape the game expects. A counter that says a button is "pressed" says
nothing about whether it was ever released.

### Two real bugs found on the way, neither of them the blocker

Both were confirmed, and both were wrongly promoted to "the cause" before the
harness bug turned up. They are worth fixing on their own merits.

**The SPU send handshake never completes**, as described above. Real, still
unfixed, gates all sample upload. It is not what pinned the screen: with
`GHPC_SYNTH_ACK=1` the screen sat exactly where it had before.

**The pad does not present as a guitar.** `system/run/config/gen/joypad.dtb`
classifies controllers under `HX_EE` as:

    ro_guitar  (detect (type kJoypadAnalog) (button kPad_DLeft))
    digital    (detect (type kJoypadDigital))
    analog     (detect (type kJoypadAnalog))
    dualshock  (detect (type kJoypadDualShock))

So the real RedOctane PS2 guitar is an analog pad with no actuators that holds
D-pad Left forever, and GH2 uses that quirk as its signature.
`TutorialPanel::IsMissingGuitar` (0x146e78) needs port 0 connected and
`JoypadIsGuitar(0)`, and it is exposed as a script handler out of
`TutorialPanel::Handle` (+0x334), so DTA branches on it. Our stub reports
DualShock, `scePadInfoAct` returning 2 motors, so it classifies as `dualshock`.
`GHPC_PAD_GUITAR=1` presents the guitar shape and is the starting point for
real HID guitar support. It did **not** unblock the screen either, tested on
its own.

### Reading the joypad globals correctly

`gJoypadIdx` at **0x51ddb0**, one word per port, is an **index**, not a
pointer: `JoypadGetData` (0x2ef750) asserts it is under 8, and the entry is
`gJoypadData = 0x51dbc8 + idx * 0x30`. A probe that dereferenced the word as an
address printed convincing nonsense for a while. `+0x20` of the entry is the
connected flag. `JoypadDetectionIsStable(i)` (0x2f3f68) is a different array,
`0x51e000 + i*0x180 + 0x11c`, and wants **0x63**. It reached 0x63 the whole
time, so detection was never the problem.

### Tooling: dtb.py could not dump a tree

`dump()` in `ghpc/scripts/dtb.py` printed the header and returned. The node
encoding, validated by consuming `joypad.dtb` to exactly 5467 of 5467 bytes:

    header  byte flag, u16 count, u32 id            (7 bytes)
    node    u32 type
              type 16/17/19 (array/command/property): u16 count, u32 id, children
              type 5/18/7   (symbol/string/ifdef):    u32 len, len bytes
              everything else:                       u32 value

Type 7 (`ifdef`) being length prefixed is the one that is easy to miss; sizing
it as a plain u32 desyncs the parse and it then dies 668 bytes in.

## What the build actually does, measured

Driven with `GHPC_PAD_AUTO=cross`, three separate runs:

- It renders. `[frame] 512x448 nonBlack=193382/229376 otherColors=191818`, so
  84% of the screen is non-black with 191k distinct colours, sustained.
- It reaches a screen with a help bar and stops progressing. The frame content
  then repeats byte-identically (`nonBlack=182793` every sample).
- It is not frozen. VIF1 opcode totals keep climbing and frames keep drawing
  while it is hung.
- Rasterisation is the cost: `[gs/logo] frags=102120000` in 60 seconds, about
  1.7M fragments/sec, all through `gs_cpu_backend.cpp` on the CPU. That is the
  slowness. It is architecture, not a bug to find.

`BACKLOG.md` and `notes/m5-progress.md` still describe a build that dies at a
black screen after the RedOctane logo with 99.99% of tristrip geometry
suppressed. That is out of date and it cost a full session. Anything in those
files about the splash rendering blocker, the qw701 camera matrix or ADC
suppression should be read as history.

## Instruments in this repo that do not mean what they look like

Three cost real time in one session. Read the emit condition before believing
any counter, every time.

- **`[ee/stall]` is not a stall detector.** `EeScheduler::run` calls
  `reportEeStall(m_snapshot, true, m_rdram)` whenever `eeCensusDue()` fires,
  which is a periodic timer, and the `idle` argument is hardcoded `true`. A
  thread shown waiting there is usually just idle.
- **`pc`, `ra`, `sp` and `gpr17` in the thread census are last-yield values,
  not live.** They come from the thread's saved context, which is written when
  the thread yields. `cop0Count` is live. Proof: across 60 consecutive censuses
  during the hang, `cop0Count` had 60 distinct increasing values while `pc`,
  `sp`, `ra`, the stack frames and `$s1` were byte-identical. A frozen `pc`
  therefore means "has not yielded since here", never "is spinning here".
- A worker thread parked in `WaitSema` is its normal idle state. `wakeups=0`
  means no work was posted, not that a signal was lost.

## Where the hang is

**Superseded, see correction 2.** There is no hang here; the main loop runs
at full rate. This chain is only where the census caught the thread.

The main thread last yielded here, and has not yielded since. Resolved from
the live stack via `[ghpc/frames]` and `ghpc/scripts/whereis.sh`:

    App::Run                      +0x138
    UIManager::Draw               +0x88
    GHScreen::Draw                +0x8c
    HelpBarPanel::DrawHelpBar     +0x58
    ButtonHBElement::Draw         +0x2c
    PanelDir::DrawShowing         +0xbc
    HelpBarElement::DrawMidBG     +0x100
    RndTransformable::WorldXfm    +0x14c   (0x43f8b4)

A main loop that stops reaching vsync is consistent with an infinite loop in
guest code somewhere in this path. The saved context cannot say where in it.

**Resolve addresses against ELF symbol boundaries, not `data/ranked.tsv`.** Its
instruction count for `UIManager::Poll` overruns into `UIManager::Draw`, so a
containing-function lookup driven by it named `Poll +0x7d0` for an address that
is really `Draw +0x88`, putting a function in this chain that is not in it.
`llvm-objdump -d` prints the real boundaries.

## The only two loops in that chain

Every other function in the chain has no backward branch at all: `DrawMidBG`,
`PanelDir::DrawShowing`, `ButtonHBElement::Draw`, `GHScreen::Draw` and
`WorldXfm`. Two remain, and they are the same shape. Walk a container, dispatch
a virtual per element, stop on pointer equality with an end marker.

`HelpBarPanel::DrawHelpBar` (0x152940, 36 insns) walks an
`stlpmtx_std::vector<HelpBarElement *>`, `_M_start` at `this+0x90` and
`_M_finish` at `this+0x94`, calling `vtable+0x34` per element:

    it = *(this+0x90); end = *(this+0x94)
    do { ++it; call (*it)->vtable[0x34]; end = *(this+0x94); } while (it != end)

`UIManager::Draw` (0x24cd08) walks a linked list, `next` at `+0x0`, payload at
`+0x8`, calling `vtable+0x84` per node until `next` equals a sentinel held in a
register.

Both terminate on equality only, so either runs forever if the end marker is
not exactly reachable by the step.

**Reallocation during iteration is ruled out for the vector.** It was the
obvious candidate, since reallocating mid-walk leaves `it` in the freed buffer
and the reloaded `end` in the new one, never equal. But `_M_insert_overflow` is
called only from `HelpBarPanel::AddElement`, `AddElement` only from
`HelpBarPanel::SetDisplay`, and `SetDisplay` only from
`GHScreen::UpdateHelpText` and `HelpBarPanel::Handle`. None is reachable from
`GHScreen::Draw`, so nothing on the draw path can mutate that vector.

## Ruled out, with evidence. Do not re-chase these.

**`RndTransformable::WorldXfm` (0x43f768) is not the loop.** Every branch in
the function is forward; it contains no backward branch at all. It recurses on
`*(this+0x10)`, the parent pointer, and at the hang that chain is
`0x7c65b0 -> 0xe88480 -> 0xaa8220 -> 0x0`, three deep and NULL terminated in
all 60 frozen censuses. `+0xa0` is a validity flag checked on entry, `+0xa4` a
constraint mode switched on against 2, then 1, then `< 5`. Each arm calls
`WorldXfm` once and every arm returns to `0x43f8b4`, which is why that address
looks pinned.

**The ThreadCall worker queue is not involved.** Decompiled in full from
`GH2_debug.elf`, see below. At the hang it is idle with the queue drained:
`sema=16 thread=2 callDone=0 signalled=0 curCall=1 freeCall=1 curFunc=0x0`.
Exactly one job was posted across a whole run and it completed.

**The camera projection matrix upload is clean.** `PsCam::Select` (0x1c1770)
is the only guest writer of VU1 qw696-703. It appends two VIF commands,
`0x7C0202B8` (UNPACK V4-32 masked, NUM=2, ADDR=696) and `0x6C0602BA` (UNPACK
V4-32 unmasked, NUM=6, ADDR=698), FLG clear on both so no TOPS is added. Dumped
straight out of the packet before unpacking, lane x of qw701/702/703 is
`00000000`, exactly as expected. The port's unpack is also correct: mask does
not affect source consumption (`bytesPerVector` from vn/vl only, `srcIndex`
advanced at 1405/1418, mask first read at 1539), matching PCSX2, whose
`Vif_Unpack.cpp:219` sizes with `nVifT[cmd & 0x0f]` and so cannot distinguish
masked from unmasked. FLG/TOPS gating at 1301-1303 matches PCSX2 too.

**`_5PsCam$sGuardBand` (0x4f3038) reads 0.0** in every run. It is in .bss and
`PsCam::Init` loads it from `SystemConfig("rnd")->FindData("guard_band", ...)`.
Whether that matters is untested, but the values it feeds come out finite, so
the computed fallback branch is being taken.

## ThreadCall_EE, decompiled

Non-matching, from `GH2_debug.elf`. `ThreadCall` itself was already matched in
gh2-decomp and its disassembly validates the addresses below.

Globals. `gSema` is new, absent from the decomp's recovered list, and sits four
bytes below `gThreadID`:

    gSema      0x445044   semaphore id
    gThreadID  0x445048
    gCallDone  0x44504c
    gSignalled 0x445050
    gCurCall   0x445054
    gFreeCall  0x445058
    gData      0x523ec0   5 entries x 12 bytes

`ThreadCallData.mUnk08` is the **result slot**: `MyThreadFunc` stores
`mFunc()`'s return value at `+0x08` and `ThreadCallPoll` passes it to
`mDone(int)`. That names the member by use.

    ThreadCallInit  0x2f6028   memset(gData); CreateSema -> gSema;
                               CreateThread(entry=MyThreadFunc, stackSize=0x4000)
                               -> gThreadID; StartThread; on failure DeleteThread
                               and gThreadID = 0.

    MyThreadFunc    0x2f61b0   for (;;) { WaitSema(gSema);
                               assert(gData[gCurCall].mFunc);
                               r = gData[gCurCall].mFunc();
                               gCallDone = 1;
                               gData[gCurCall].mResult = r; }

    ThreadCallPoll  0x2f6348   if (gCallDone && gData[gCurCall].mFunc) {
                                 gSignalled = 0; gCallDone = 0;
                                 if (++gCurCall >= 5) gCurCall = 0;
                                 retire: mDone(mResult), mFunc = NULL; }
                               if (!gSignalled && gData[gCurCall].mFunc) {
                                 gSignalled = 1; SignalSema(gSema); }

`ThreadCallPoll` is the **only** signaller of `gSema`. `ThreadCall` posts into
a slot and returns without signalling, so the worker does not wake until the
main loop polls.

## What the hung screen actually is

**Superseded, see correction 1.** The screen is the guitar controller
instruction screen, not the memory card load screen. The advice in this
section is still right, the conclusion it reached is not.

**Look at the framebuffer before theorising.** The runtime dumps frames to
`/tmp/ghpc_frame_NN.ppm`; convert one with ffmpeg and open it. The hung frame
is the memory card load screen, the one reading "LOADING... Loading Guitar
Hero II data. Do not remove memory card in MEMORY CARD slot 1".

So it is not a song load and it is not a menu. Two full sessions were spent
reasoning about a "song load hang" that is a memory card load screen.

**The memory card read itself completes.** The trace, all of it before log line
5395 of 54596:

    sceMcInit / sceMcGetInfo / sceMcGetDir
    sceMcOpen  a2=0x523d70
    sceMcSync  a1=0x523cd0 a2=0x523cd4
    sceMcRead  a1=0xf29e70 a2=0x21c00      138240 bytes
    sceMcSync  a1=0x523cf0 a2=0x523cf4
    sceMcSync  a1=0x523d00 a2=0x523d04

`work/mc0/BASLUS-21447/data` is exactly 138240 bytes, so the `fread` in
`sceMcRead` returns the full count and is not a short read. `sceMcSync` printed
7 times against its own cap of 10, so it really was called 7 times and the game
is not polling it. After line 5395 there is no memory card traffic at all for
the remaining 49000 lines. The card path did its job and the game moved on.

Note `sceMcSync` in `MemoryCard.cpp` is one-shot: it consumes
`g_mcCommandPending`, returns 1 with the result, and returns -1 on every later
call. It also ignores its `mode` argument. That is worth remembering, but it is
not what is stalling this screen, because the game stops calling it.

**Both `[mc]` probes are capped** at 10 lines each (`static int c=0; if(c++<10)`),
so absence of later lines would not have proven absence of calls. Here the
counts stayed under the cap, which is the only reason the conclusion holds.

## It is not a spin loop. It is waiting.

Settled with `build.sh --calls`, which counts guest function entries per census
instead of logging them. The count of distinct functions running between
censuses over one run:

    boot:      1520, 885, 448, 1008, 681, 825   variable, real work
    from #20:  175, 169, 169, 175, 169, 169, ... exact 3-cycle to the end

Roughly 170 functions run every census with byte-identical per-function counts
(`SetRegister=6324`, `CloseGifTag=4862`, `CloseDmaTag=4488`, `WorldXfm=1428`
every single time). That is a full render loop redrawing an unchanging screen,
not a spin. **Both loop candidates above are dead as explanations.** Nothing
overruns a container; the game is idle and waiting for something.

The two censuses before the steady state are the song load, and it looks
healthy:

    11925  Heap::InsertFreeBlock
     9226  BinStream::Read / ChunkStream::ReadImpl
     8350  FreeBlock::AttemptMerge
     8005  BinStream::ReadEndian
     7044  MemTrackAlloc      6554  MemTrackFree
     6018  Heap::Alloc        6018  MemAlloc

It reads and allocates hard, then stops, and no file function appears in the
steady state at all. `[FILEIO]` lines occur only at lines 15-26 of the log, ie
at boot, so that burst is `BinStream`/`ChunkStream` working in memory rather
than fresh file traffic.

## The audio service is unimplemented, and that is the best remaining lead

**Resolved, see the SPU send handshake section.** The caveat below about
ordering was the right worry, and the answer is that the unhandled sid does
stall audio but does not pin the screen.

Exactly four RPCs in a whole run go unanswered, all to the same service, and
there are no other unhandled sids:

    [IOP/RPC trace:unhandled] sid=0x75433178 pc=0x269c3c recv=0x0/0
    loadedModules=[cdrom0:/iop/synth_r.irx; cdvdstm.irx; sdrdrv.irx;
                   libsd.irx; mtapman.irx; msifrpc.irx]

`0x269c3c` is `CtlClientPoll +0x6c`, so the caller is a poll function, and
`SynthPoll` and `SPUSendBusy` both keep ticking 1564 times per census forever
in the steady state. So the EE polls the SYNTH_R control service continuously
and never gets a reply. That would explain the missing audio and a song that
never starts with one cause.

**Caveat, do not skip it.** Those four RPCs land at log line 10163, which is
*before* the load burst at 10169-11440, not after it. So the ordering does not
by itself prove the audio service is what the game is finally waiting on. What
is established is that sid `0x75433178` is unhandled, that it is the only
unhandled service, and that the audio poll path runs forever in the steady
state. Confirming it means finding what the steady state is actually polling
for, which needs the histogram widened past the top 12.

### What implementing it would involve

There is no SYNTH_R module. `ps2xIOP/src/modules/` holds clfile, cri_dtx,
dbcman, fileio, libsd, mcserv, sdrdrv, sound_update_stub, tsnddrv and usbkb,
and every service id they register is Sony standard:

    kFileioSid  0x80000001    kMcservSid  0x80000400
    kLibSdSid   0x80000701    kUsbKbSid   0x80000211
    kDbcManSid  0x80001300

`0x75433178` is nowhere near that range, which fits it being Harmonix's own
service rather than a Sony one, and matches the note in BACKLOG that GH2's IOP
audio is `LGAUD` plus Harmonix `SYNTH_R`/`SYNTH_S` and undocumented. So this is
the M5 audio work, entered from a known point: bind sid `0x75433178` in a new
module under `ps2xIOP/src/modules/`, modelled on `sdrdrv.cpp` and `tsnddrv.cpp`,
which are the closest audio analogues already in the tree. The request payloads
are visible in the `[IOP/RPC trace:unhandled]` lines (`send=` sizes 304, 20500,
92 and 12 in one run) and are the first thing to decode.

## Tooling added for this

- `GHPC_NO_FOCUS` keeps keyboard focus on the terminal. It does not stop the
  window being raised: GLFW's cocoa show path calls `orderFront`
  unconditionally and offers no hint to suppress it.
- `GHPC_HIDE_WINDOW` is the only way to keep an unattended run off screen. The
  game runs within 1.6% of the same rate with no window.
- `GHPC_PAD_AUTO=cross` presses CROSS only at 3 Hz. The mixed CROSS/START mode
  never reaches a song because START backs out of the menu CROSS just entered.
- `[ghpc/tcall]` dumps the ThreadCall globals on the census tick.
- `[ghpc/frames]` walks return addresses off the live stack, which is what
  produced the call chain above.
- `build.sh --calls` builds with `PS2X_ENABLE_CALL_HISTOGRAM`, which reuses the
  per-function `PS_LOG_ENTRY` hook to count entries instead of printing them,
  and dumps the top callers by delta each census. Aggressive logs answer the
  same question but cost gigabytes. It builds into `build-calls` so toggling it
  does not force `build-debug` through a full rebuild.
