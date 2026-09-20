# ghpc

Native PC port of Guitar Hero 1, 2, and Rocks the 80s, by static recompilation
of the PS2 binaries, with machine-translated output progressively replaced by
source.

Requires a user-supplied disc. No game data or recompiled output is committed.

**`ghpc/NEXT.md` is the standing handoff. Read it first.** It carries the
current state, the phase order and the working rules. Nothing about where the
port stands is repeated here, because a second copy drifts: this file claimed
"menus ~100% of realtime" for weeks after it stopped being true.

    ghpc/NEXT.md            where things stand, what to do next, the rules
    ghpc/BACKLOG.md         the queue
    ghpc/notes/gotchas.md   symptoms that are not faults, tooling traps
    ghpc/notes/decisions.md settled choices, so they are not re-litigated
    ghpc/notes/             topic notes, which supersede both on their subject

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
stays runnable throughout. Why PS2 and not the 360 release: `notes/decisions.md`.

## Layout

```
ghpc/               everything specific to this port
  scripts/build.sh  ELF -> C++ -> native binary. --from/--to run a slice.
  scripts/play.sh   play it by hand. --ingame stops in a song.
  scripts/run.sh    launch (cwd must be work/ so the game resolves GEN/)
  scripts/checkrun.sh  run the debug build and assert boot still progresses
  scripts/fixreplay.sh replay captured draws against VU1's own output, ~1s
  scripts/dtb.py    decrypt and inspect any DTB straight out of the ARK
  config/           stub-denylist.txt, symbols excluded from handler binding
  notes/            findings, gotchas, decisions, evidence/
  reference/        gitignored: stills captured from the game
ps2xRecomp/         upstream: the recompiler, ELF -> C++
ps2xRuntime/        upstream: PS2 hardware emulation and host layer
ps2xIOP/            upstream: IOP modules
work/               gitignored: ELFs, generated C++, extracted game data
third_party/        gitignored: shallow clones of reference projects
```

## Constraints

Both of these break things silently when ignored. Everything else that bites is
in `notes/gotchas.md`.

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

## Build

```sh
./ghpc/scripts/build.sh              # full pipeline, ~5 min cold
./ghpc/scripts/build.sh --from=build # rebuild only
./ghpc/scripts/play.sh               # main menu at t=6
```

Builds on arm64 macOS with no upstream changes. Toolchain and host
requirements are in `notes/gotchas.md`.
