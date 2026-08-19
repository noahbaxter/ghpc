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

Recompiled GH2 boots, initializes video and audio, runs `SystemInit`, and binds
its filesystem RPC to the Sony fileio service (SID `0x80000001`). It cannot read
the ARK because no service answers that SID, so it remains in the game's retry
loop and the framebuffer stays magenta.

M0 (symbols), M1 (recompile and compile), M2 (boot), M3 (RPC bind) complete.
Details in `notes/`. M4 is the fileio service.

## Layout

```
scripts/build.sh    ELF -> C++ -> native binary. --from/--to run a slice.
scripts/run.sh      launch (cwd must be work/ so the game resolves GEN/)
config/             stub-denylist.txt, symbols excluded from handler binding
notes/              milestone findings, m0..m3
patches/            local changes to PS2Recomp, apply over a pristine clone
work/               gitignored: ELFs, generated C++, extracted game data
third_party/        gitignored: shallow clones of reference projects
```

## Constraints

**Name-collision binding.** PS2Recomp binds runtime handlers by symbol name. The
ELF is symbolized and GH2 statically links Sony's libraries, so those names
resolve to real function bodies rather than imports. Four mechanisms did this;
`config/stub-denylist.txt` plus patch 0003 override all four. Add denylist
entries with a stated reason.

**Patches stay local.** Nothing is submitted upstream.

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
./scripts/build.sh              # full pipeline, ~5 min cold
./scripts/build.sh --from=build # rebuild only
./scripts/run.sh --quiet
```

Requires cmake, ninja, ccache, pkg-config, llvm (`llvm-readelf`,
`llvm-objdump`), chdman, 7z. Builds on arm64 macOS with no upstream changes.

Linux is required for the decomp toolchain (32-bit GCC, i386 multiarch), and is
the better host for rendering work since macOS OpenGL is capped at 4.1.
