# The UI draw hang

Written after a session that spent most of its time aimed at problems the
build had already moved past. The first section exists so that does not happen
again.

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

The main thread last yielded here, and has not yielded since. Resolved from
the live stack via `[ghpc/frames]` and `ghpc/scripts/whereis.sh`:

    App::Run                      +0x138
    UIManager::Poll               +0x7d0
    GHScreen::Draw                +0x8c
    HelpBarPanel::DrawHelpBar     +0x58
    ButtonHBElement::Draw         +0x2c
    PanelDir::DrawShowing         +0xbc
    HelpBarElement::DrawMidBG     +0x100
    RndTransformable::WorldXfm    +0x14c   (0x43f8b4)

A main loop that stops reaching vsync is consistent with an infinite loop in
guest code somewhere in this path. The saved context cannot say where in it.

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
