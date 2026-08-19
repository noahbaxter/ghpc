# ghpc

Native PC port of Guitar Hero 1, 2, and Rocks the 80s, built by statically
recompiling the PS2 binaries and progressively replacing the machine-translated
output with real source.

Bring your own disc. No game data or recompiled output is ever committed.

## Goal

One native, moddable, low-latency GH1+2+80s game with real guitar controller
support. Think GHWT Deluxe, except GHWT shipped on PC and had a binary to mod,
and these three never did. That missing binary is the whole project.

Authenticity and moddability are the point, not raw latency. Clone Hero and YARG
already play this content natively today. What does not exist anywhere is the
actual GH2 game, its venues, characters, career and menus, running natively and
open to modification.

## Approach

Three layers, only one of which is new work.

1. **Data.** Consume GH2DX-Unified. The MiloHax Deluxe projects are data-layer
   mods (DTA scripts and ARK repacking), so the whole Deluxe feature set is a
   property of the data, not the executable. Anything that runs GH2's binary
   inherits it. We contribute here, we do not rebuild it.
2. **Executable.** Recompile the PS2 ELFs with PS2Recomp, then progressively
   swap machine-translated functions for real C++. This is the project.
3. **Input.** Native HID guitar support and honest calibration. Small next to
   layer 2, and it is the differentiator.

Recomp is the scaffold, decomp is the incremental replacement. The game runs at
every step and converges on readable source, which recomp alone never gives you.

### Why PS2 and not Xbox 360

GH2 also shipped on 360, and XenonRecomp is a more mature toolchain with a real
shipped result (Unleashed Recompiled), while PS2Recomp has none. We chose PS2
anyway:

- All three games have symbolized debug builds. On 360 only GH2 exists at all.
- One pipeline covers GH1, GH2 and 80s. All three report 18 VU overlays, so the
  engine and VU path are shared and the work amortizes.
- The usual reason to avoid PS2 is the GS and VU1. GH2's VU microcode is ~28 KB
  across 18 named, indexed overlays, under 1% of `.text`, and the game ships a
  focus mode that disables world rendering outright.

Keep a 360 copy around as a hedge and as a second build to diff against.

## Current stage

The recompiled GH2 boots, initializes video and audio, runs `SystemInit`, and
gets its filesystem RPC bound to the Sony fileio service (SID `0x80000001`).
It cannot read the ARK yet because nothing answers that service, so it spins in
the game's own retry loop and the screen stays magenta.

Milestones: M0 (symbols) M1 (recompile + compile) M2 (boot) M3 (RPC bind) all
passed. See `notes/` for what each one found. Next is M4, the fileio service.

## Layout

```
scripts/build.sh    ELF -> C++ -> native binary. --from/--to to run a slice.
scripts/run.sh      launch it (cwd must be work/ so the game finds GEN/)
config/             stub-denylist.txt, symbols we refuse to let bind
notes/              milestone findings, m0..m3
patches/            local changes to PS2Recomp, applied over a pristine clone
work/               gitignored: ELFs, generated C++, extracted game data
third_party/        gitignored: shallow clones of reference projects
```

## Gotchas

**Name-collision binding is the recurring hazard.** PS2Recomp binds runtime
handlers by matching symbol names. Our ELF is symbolized and GH2 statically
links Sony's libraries, so those names are real functions with real bodies, not
imports. Four separate mechanisms did this; `config/stub-denylist.txt` plus
patch 0003 now override all four. Expect more as deeper code runs, and add
denylist entries with a reason rather than working around symptoms.

**Do not upstream anything yet.** Patches stay local until the thing works.

**Disk.** The ARK alone is 3.1 GB and reference clones ran to 9 GB. Keep an eye
on free space and prune `third_party/` clones, they are all re-clonable.

**The CHD is CD-format.** `chdman info` reports 2448-byte units, so `extractdvd`
yields raw sectors, not an ISO. Convert by taking the first 2048 bytes of each
2448-byte unit; `CD001` should land at 0x8000.

**Magenta screen** is the runtime's "GS never wrote a pixel" sentinel, not a
rendering bug.

**Debug UI.** The runtime ships an ImGui panel with IOP/SIF, RPC History and
File/CD tabs. It is the fastest way to see IOP state and it has repeatedly beaten
reasoning from first principles. Screenshots of it are genuinely useful.

**More logging** via `PS2X_ENABLE_RUNTIME_LOGS=ON`, or
`PS2X_ENABLE_AGRESSIVE_LOGS=ON` for a full per-function call trace using real
symbol names. The latter is a firehose across 12,664 functions, so use it narrowly.

## Build

```sh
./scripts/build.sh              # full pipeline, ~5 min cold
./scripts/build.sh --from=build # rebuild only
./scripts/run.sh --quiet
```

Needs cmake, ninja, ccache, pkg-config, llvm (for `llvm-readelf`/`llvm-objdump`),
chdman, 7z. Builds clean on arm64 macOS with no upstream changes needed.

Linux (bazzite) becomes the better host once we reach rendering, since macOS
OpenGL is capped at 4.1, and it is required for the decomp toolchain, which needs
32-bit GCC with i386 multiarch.
