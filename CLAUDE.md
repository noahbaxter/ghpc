# ghpc

Native PC port of Guitar Hero 1, 2, and Rocks the 80s, by static recompilation
of the PS2 binaries, with machine-translated output progressively replaced by
source.

Requires a user-supplied disc. No game data or recompiled output is committed.

## Target

One native GH1+2+80s build with mod support and HID guitar controller input.

GH1, GH2 and 80s never shipped on PC, so no PC binary exists to mod. Producing
one is the scope of this project.

## Approach

1. **Data.** Consume GH2DX-Unified. MiloHax Deluxe projects are data-layer mods
   (DTA scripts, ARK repacking), so their features attach to the data rather
   than the executable and apply to any host that runs GH2's binary.
2. **Executable.** Recompile the PS2 ELFs with PS2Recomp, then replace
   machine-translated functions with C++ incrementally.
3. **Input.** HID guitar support and calibration.

Recomp provides the scaffold; decomp replaces it function by function. The build
stays runnable throughout.

### PS2 rather than Xbox 360

GH2 also shipped on 360, and XenonRecomp is more mature than PS2Recomp
(Unleashed Recompiled shipped on it; PS2Recomp has no completed game). PS2 was
chosen on:

- All three titles have symbolized debug builds. Only GH2 exists on 360.
- One pipeline covers all three. Each reports 18 VU overlays, indicating a
  shared engine and VU path.
- GS and VU1 are the usual PS2 obstacles. GH2's VU microcode is ~28 KB across 18
  named indexed overlays, under 1% of `.text`, and the game has a focus mode
  that disables world rendering.

A 360 copy is useful as a second build to diff against.

## Stage

M0 (symbols), M1 (recompile and compile), M2 (boot), M3 (RPC bind), M4 (fileio
service) complete. Details in `ghpc/notes/`.

The storage stack works end to end: the ARK header parses, `GetFileInfo`
resolves, and `BlockMgr` streams 64 KB blocks, verified at a 2.94 GB offset.
The GS presents a real 512x448 frame, so the magenta sentinel is gone, but the
drawing path (VIF1 -> VU1 -> GIF) delivers nothing into it.

An earlier build reached the main menu and wrote a memory card save. That
depended on a `DataArray` workaround which existed only as a stale object file
with no source, so it was never reproducible. After rebuilding from clean
source the game dies earlier, at `Debug::Fail msg="Data ("`, preceded by
`[FILEIO] fn=0xc status=-2 path="host0:"`. `host0:` is the devkit host
filesystem, which is untested as a lead.

## Layout

```
ghpc/               everything specific to this port
  scripts/build.sh  ELF -> C++ -> native binary. --from/--to run a slice.
  scripts/run.sh    launch (cwd must be work/ so the game resolves GEN/)
  scripts/checkrun.sh  run the debug build and assert boot still progresses
  scripts/dtb.py    decrypt and inspect any DTB straight out of the ARK
  config/           stub-denylist.txt, symbols excluded from handler binding
  notes/            milestone findings, m0..m5
  reference/        gitignored: stills captured from the game
ps2xRecomp/         upstream: the recompiler, ELF -> C++
ps2xRuntime/        upstream: PS2 hardware emulation and host layer
ps2xIOP/            upstream: IOP modules
work/               gitignored: ELFs, generated C++, extracted game data
third_party/        gitignored: shallow clones of reference projects
```

## Constraints

**Name-collision binding.** PS2Recomp binds runtime handlers by symbol name. The
ELF is symbolized and GH2 statically links Sony's libraries, so those names
resolve to real function bodies rather than imports. Four mechanisms did this;
`ghpc/config/stub-denylist.txt` plus commit 977a01f override all four. Add
denylist entries with a stated reason.

**Fork, not a dependency.** This repo is a fork of `ran-j/PS2Recomp` with the
port built on top. Engine changes are ordinary commits. Take upstream with
`git fetch upstream && git merge upstream/main`, never rebase, since rebasing
rewrites the port's commits on every pull. Keep upstream directory names so
rename detection stays cheap. Nothing has been submitted upstream yet.

**Disk.** The ARK is 3.1 GB; reference clones reached 9 GB. `third_party/`
clones are all re-clonable.

**CHD format.** `chdman info` reports 2448-byte units, so `extractdvd` produces
raw sectors rather than an ISO. Convert by taking the first 2048 bytes of each
2448-byte unit. `CD001` then lands at 0x8000.

**Magenta framebuffer** is the runtime's "GS never wrote a pixel" sentinel from
`GenImageColor(..., MAGENTA)`, not a rendering fault.

**Debug UI.** The runtime ships an ImGui panel with IOP/SIF, RPC History and
File/CD tabs exposing IOP and file state.

**Logging.** `PS2X_ENABLE_RUNTIME_LOGS=ON`, or `PS2X_ENABLE_AGRESSIVE_LOGS=ON`
for a per-function call trace using symbol names. The latter covers 12,664
functions.

## Build

```sh
./ghpc/scripts/build.sh              # full pipeline, ~5 min cold
./ghpc/scripts/build.sh --from=build # rebuild only
./ghpc/scripts/run.sh --quiet
```

Requires cmake, ninja, ccache, pkg-config, llvm (`llvm-readelf`,
`llvm-objdump`), chdman, 7z. Builds on arm64 macOS with no upstream changes.

Linux is required for the decomp toolchain (32-bit GCC, i386 multiarch), and is
the better host for rendering work since macOS OpenGL is capped at 4.1.
