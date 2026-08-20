# ghpc

Native PC port of Guitar Hero 1, 2, and Rocks the 80s, by static recompilation
of the PS2 binaries, with machine-translated output progressively replaced by
source.

Requires a user-supplied disc. No game data or recompiled output is committed.

## Fork notice

This is a fork of [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp), which
provides the recompiler (`ps2xRecomp`), the runtime (`ps2xRuntime`) and the IOP
layer (`ps2xIOP`). Upstream's README, wiki and Discord are linked from that
repo. GPL, inherited from upstream.

Everything specific to this port lives in `ghpc/`. Everything else is upstream's
tree with the port's changes applied as ordinary commits, concentrated in
`ps2xRuntime`. Upstream is taken by merge, never rebase, and upstream directory
names are kept so rename detection stays cheap.

## Status

M0 (symbols), M1 (recompile and compile), M2 (boot), M3 (RPC bind) and M4
(fileio service) are complete. Findings are recorded per milestone in
`ghpc/notes/`.

The storage stack works end to end: the ARK header parses, `GetFileInfo`
resolves, and `BlockMgr` streams 64 KB blocks, verified at a 2.94 GB offset.
The GS presents a real 512x448 frame every frame. The drawing path
(VIF1 to VU1 to GIF) delivers nothing into it.

An earlier build reached the main menu and wrote a memory card save. That
depended on a `DataArray` workaround which existed only as a stale object file
with no source, so it was never reproducible. Rebuilt from clean source, the
game dies earlier at `Debug::Fail msg="Data ("`.

The build currently consumes the symbolized debug ELF, which is not on a retail
disc. Retargeting the retail executable is an open question, tracked in
`ghpc/BACKLOG.md`.

## Layout

```
ghpc/          everything specific to this port
  scripts/     build, run, checkrun, dtb dumper
  config/      stub-denylist.txt, symbols excluded from handler binding
  notes/       milestone findings, m0..m5
ps2xRecomp/    upstream: the recompiler, ELF -> C++
ps2xRuntime/   upstream: PS2 hardware emulation and host layer
ps2xIOP/       upstream: IOP modules
work/          gitignored: ELFs, generated C++, extracted game data
third_party/   gitignored: reference clones
```

## Build

```sh
./ghpc/scripts/build.sh              # full pipeline, ~5 min cold
./ghpc/scripts/build.sh --from=build # rebuild only
./ghpc/scripts/run.sh --quiet
```

Requires cmake, ninja, ccache, pkg-config, llvm (`llvm-readelf`,
`llvm-objdump`), chdman, 7z. Builds on arm64 macOS with no upstream changes.

Diagnostics are behind the `PS2X_GHPC_DIAG` CMake option, off by default.
`--debug` enables them and uses a separate build tree so both binaries coexist.
