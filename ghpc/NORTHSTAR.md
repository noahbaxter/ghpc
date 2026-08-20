# Northstar

The target, not a status report. Several things below are not true yet. Closing
those gaps is the work, and each gap is marked.

## The aim

One command turns a disc the user already owns into a native PC build of Guitar
Hero 1, 2 and Rocks the 80s, with mod support and HID guitar input, running
without an emulator.

```sh
./scripts/build.sh /path/to/disc.chd
```

No game data and no game-derived code is ever committed or distributed. The
tool is the product. The user brings the disc. This is the same shape as
N64Recomp and Unleashed Recompiled.

## Why this is not just an emulator

A PS2 game is roughly 30% EE CPU code. PS2Recomp machine-translates that part
to C++ and it works. The other 70% is hardware that has to be implemented by
hand: the IOP, the GS rasterizer, VU0 and VU1, the DMAC/VIF1/GIF/MFIFO
plumbing, COP0, timers, kernel syscalls.

So the near-term work is maturing a PS2 recompiler and runtime that has never
completed a game, with GH2 as its only test case. The long-term work is
replacing machine-translated output with real source, function by function,
until the result is a port rather than a translation.

The strategic lever is that Milo's renderer sits on a portable `Rnd*` API over a
thin PS2 backend. Replacing at the `Rnd` layer with a native GL or Vulkan
backend makes VIF1, VU1, GIF, MFIFO and the GS rasterizer dead code that is
never executed, instead of subsystems that have to be finished. Measured: no
gameplay or UI class touches DMA or VIF directly, so that seam is clean. Video
is a second seam it does not cover, see below.

## Where the aim is currently false

**The build does not start from a disc.** `scripts/build.sh` builds from the
symbolized debug executable (`SLUS_214.47`, "PS2 Final Debug"), not the retail
executable on a disc. That ELF is not on a retail disc and a user cannot produce
it from their own copy. This is the single biggest gap between this document and
reality.

Resolving it means picking one:

- Retarget the retail ELF. Costs all 12663 symbols, which the current handler
  binding and `config/stub-denylist.txt` both depend on. Changes how every
  stage works.
- Support both, debug for development and retail for distribution.
- Accept that the tool targets the debug ELF and the one-command-from-a-disc
  story is not the product.

Undecided. Until it is decided, do not write the disc story into a README, a
release note, or anything user-facing.

**It does not boot to gameplay.** Current state is the main menu. See
`notes/m5-progress.md` for where it actually is, which is the file to trust over
this one.

**Only GH2 is exercised.** GH1 and 80s are believed to share the engine and VU
path, and `scripts/run.sh` accepts them, but neither is a real test case yet.

**No HID guitar support and no mod support.** Both are in the aim, neither is
started.

**Video is a second hardware seam.** `PsMovie`, `Video` and `PlayMovie` drive
the IPU and DMA directly, outside `Rnd`. An `Rnd`-layer replacement does not
cover intro or cutscene video. That needs a working IPU or a native decoder
substituted at `PsMovie`/`Video`.

## Rules that hold regardless

- Nothing derived from the disc is ever committed. That output lands in two
  ignored places, not one: `work/` and `ps2xRuntime/src/runner/`. Note that
  gitignore does not apply to files git already tracks, which is how a 270880
  line generated function table stayed tracked for months.
- Reference clones under `third_party/` are read-only maps of what code is
  supposed to do. rb3-decomp is a different console, a different endianness and
  four years newer. Never a drop-in.
- Measurement before theory. This project has lost multiple sessions to
  reasoning from a symptom that turned out to be manufactured by the runtime
  rather than produced by the game.
