# gh2-decomp

A decompilation attempt at Guitar Hero 2 (PS2, SLUS-21343 debug build).

Nothing here is game data, and nothing here is a binary. The sources are
hand-written C++ reconstructed from the machine translation that
[`ghpc`](../ghpc) produces, read against the debug ELF's symbol table.

## State

179 functions decompiled across 19 files, 168 at high confidence and 11 at
medium. Nothing at low: anything that would have landed there was dropped rather
than written down. Per-function evidence is in `docs/confidence.md`.

That is 1.4% of the 12,422 functions in the binary. 130 of the 179 sit inside
the ranking's easiest 1,000.

All 19 files pass `tools/check.sh`, which is a syntax check and nothing more.
Nothing here has been executed. See `docs/methodology.md` for what that does and
does not prove.

## Layout

```
tools/     ranking, demangler, and the ELF readers that made the work possible
src/       decompiled sources, grouped by engine area
include/   inferred struct layouts, offsets pinned by evidence
data/      the ranking output, the symbol table, a full vtable dump
docs/      methodology, per-function confidence, the 50 easiest, the wall
```

## Docs

- `docs/methodology.md` - how difficulty is scored, the distribution, the tools,
  and the limits of `check.sh`
- `docs/confidence.md` - every decompiled function with a confidence level and
  the evidence behind it
- `docs/decomp-log.md` - per class group, what was found and what pinned it
- `docs/50-easiest.md` - the easy end of the ranking
- `docs/the-wall.md` - where this stopped and why, plus what I got wrong about
  which parts would be hard

## Rebuilding the ranking

```sh
python3 tools/rank.py       # rewrites data/ranked.tsv and data/ranked.json
python3 tools/confidence.py # rewrites docs/confidence.md
./tools/check.sh            # syntax-checks src/ against include/
```

`tools/rank.py` reads `ghpc/work/output/` and `tools/vtable.py` and
`tools/rodata.py` read `ghpc/work/GH2_debug.elf`. Both paths are absolute
constants at the top of those files. Everything is read-only.

## Relationship to rb3-decomp

The Rock Band 3 decompilation shares the Harmonix Milo engine lineage and was
used as reference for engine class names and shapes. It has no license file, so
nothing was copied from it. Where a GH2 layout matches RB3's, that is recorded as
corroborating evidence and the code was still written from the GH2 disassembly.
