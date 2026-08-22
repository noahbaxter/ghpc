# Methodology

## Input

`ghpc` statically recompiles the GH2 PS2 ELF and emits one C++ file per original
function into `work/output/`, 12,663 of them, named `<MangledSymbol>_0x<addr>.cpp`.
Each is a machine translation: one C++ statement per MIPS instruction, with the
original address, instruction word and disassembly preserved in a comment above
every statement.

Everything in this repo reads that tree and the debug ELF. Nothing writes to
them.

241 of the 12,663 are not translations at all. PS2Recomp replaced their bodies
with a call into a runtime syscall shim, so they carry no disassembly and there
is nothing to decompile. They are listed in `data/syscall_stubs.txt` and excluded
from the ranking, leaving 12,422 ranked functions.

## Ranking

`tools/rank.py` parses only the disassembly comments, not the emitted C++. The
comments are the ground truth for what each function does and they are 20x
cheaper to parse.

One wrinkle: the recompiler's pretty-printer does not know every COP1 opcode. It
emits correct C++ but prints the instruction as `.word 0x46000064  # cvt.w.s ...`
with the real disassembly in a trailing comment. 134 files are affected. The
parser reads the trailing comment so those instructions still count.

Score is a weighted sum. Lower is easier.

| signal | weight | why |
|--------|--------|-----|
| instruction count | 0.35 each | size, the crudest proxy |
| `switch (ctx->pc)` dispatch cases | 2.0 each | one per basic-block entry point, so a direct measure of control-flow complexity |
| `label_` targets | 1.5 each | same thing from the other side |
| branches | 2.5 each | loops and conditionals to reconstruct |
| `jal` calls | 6.0 each | every callee is a type and a name to work out |
| tail calls (`j` to a function) | 4.0 each | same, but the shape is usually obvious |
| indirect calls (`jalr`, computed `jr`) | 14.0 each | the call site names no target |
| COP1 instructions | 1.2 each | FPU, readable but adds float typing |
| COP2 instructions | 9.0 each | VU0 macro mode, assumed hardest |
| distinct memory offsets | 1.6 each | each `off($reg)` pair is a candidate struct field |
| mult/div | 1.0 each | HI/LO handling |

Then symbol-shape credits, from the demangled name:

| shape | credit |
|-------|--------|
| accessor (`Get`, `Set`, `Is`, `Num`, ...) | -8.0 |
| destructor | -6.0 |
| constructor | -5.0 |
| has a class name at all | -2.0 |
| per known parameter type, up to 4 | -0.5 |
| symbol will not demangle | +4.0 |

The credits are what let the score go negative. A negative score just means a
one-instruction body on a well-named accessor.

GNU v2 mangling carries class and parameter types but not return types, so a
one-instruction `jr $ra` body has to be read as `void` unless the name says
otherwise. That is the one systematic assumption in the easiest tier.

### Score distribution

12,422 functions.

| percentile | score |
|-----------|-------|
| p0 | -11.7 |
| p1 | -8.2 |
| p5 | 0.3 |
| p10 | 9.1 |
| p25 | 26.5 |
| p50 | 58.5 |
| p75 | 172.6 |
| p90 | 422.5 |
| p95 | 649.0 |
| p99 | 1781.9 |
| max | 27730.3 |

| band | functions | share |
|------|-----------|-------|
| < 0 | 575 | 4.6% |
| 0 to 10 | 711 | 5.7% |
| 10 to 25 | 1483 | 11.9% |
| 25 to 50 | 2975 | 23.9% |
| 50 to 100 | 2089 | 16.8% |
| 100 to 250 | 2478 | 19.9% |
| 250 to 500 | 1237 | 10.0% |
| 500 to 1000 | 528 | 4.3% |
| >= 1000 | 346 | 2.8% |

The distribution has a long thin tail. The median function is 58.5 and the top
one is 27,730: `SpliceKeys(RndTransAnim *, ...)` at 6,913 instructions with 6,011
dispatch-table cases.

Population facts worth having:

- 1,636 leaf functions, no call of any kind.
- 2,336 functions of 12 instructions or fewer.
- 203 functions that are a single `jr $ra`, which is to say empty.
- 2,140 touch COP1.
- 219 touch COP2.
- 1,729 have an indirect call, almost all of them ordinary virtual dispatch.
- Zero have a computed `jr` into a jump table. That whole class of difficulty
  does not exist in this binary.

### Where the score is wrong

The COP2 weight of 9.0 is too high and I would lower it. See "The wall" below.
The indirect-call weight of 14.0 is also too high now that `tools/vtable.py`
exists: a virtual forwarder is usually as easy as a direct one. I left both
weights alone so the ranking in `data/ranked.tsv` is the one the work was
actually driven by, rather than a retrofit.

## Tools

| tool | what it does |
|------|--------------|
| `tools/gnuv2.py` | GNU v2 (g++ 2.x) demangler. `c++filt` and `llvm-cxxfilt` both refuse these names, so this is written from scratch. Covers what the GH2 symbol table uses, returns None on anything it cannot parse. |
| `tools/rank.py` | Scores all 12,422 and writes `data/ranked.tsv` and `data/ranked.json`. |
| `tools/disasm.py` | Prints the disassembly a recompiled file carries in its comments, and rewrites every `func_ADDR` placeholder with the real symbol. This is the workhorse. |
| `tools/vtable.py` | Reads the 892 `_vt$` symbols out of the ELF and turns a vtable byte offset into a function name. `data/vtables.txt` is a full dump. |
| `tools/rodata.py` | Reads C strings and word tables out of the ELF's loaded segments. |
| `tools/confidence.py` | Holds the per-function confidence assessments and emits `docs/confidence.md`. |
| `tools/count.py` | Counts decompiled functions and reports where they sit in the ranking. |
| `tools/check.sh` | Syntax-checks every decompiled source. |

### The g++ 2.x calling details that keep coming up

Written down once so they do not have to be re-derived.

- **Vtables.** Two words of header, then 8 byte entries of
  `{ short delta, short pad, void *fn }`. A call site does
  `lw vptr / lh delta / lw fn / jalr fn` with `this + delta` in `$a0`, so it
  quotes the offset of the *delta*, not of the pointer. `tools/vtable.py` uses
  the same convention.
- **Struct return.** A function returning a struct by value takes a hidden
  pointer to the return slot in `$a0`, which pushes `this` into `$a1` and the
  first real argument into `$a2`. Every `Handle(DataArray *, bool)` in this tree
  looks wrong until that is applied.
- **Branch-likely.** `beql` / `bnel` annul the delay slot when the branch is not
  taken. The compiler uses this to write two-instruction conditional stores with
  no join block, which is why so many small setters look like a bare store
  followed by `jr $ra`.
- **Milo asserts.** `MILO_ASSERT` bakes the source filename, the line number and
  the literal text of the condition into the binary via `MakeString` (0x3680e8)
  and `Debug::Fail` (0x2ebda8). `tools/rodata.py` reads them back, which
  recovers original parameter names verbatim.

## What check.sh proves, and what it does not

`tools/check.sh` runs `clang++ -std=c++11 -fsyntax-only -Wall` over every file in
`src/` against the headers in `include/`. All 19 files pass with no warnings.

That proves the sources parse, that every type they name exists, and that the
member accesses and signatures are mutually consistent. It is a real check
against typos and against writing down a field that the header does not have.

It proves nothing about behaviour. Specifically:

- **Nothing here has been executed.** There is no verification harness, no test,
  no comparison against the recompiled output at runtime.
- **The headers are not the PS2 ABI.** `include/gh2/inferred_types.h` is written
  to document offsets in comments, not to reproduce them in a host compiler's
  layout. Padding members are inserted only where a later real field pins the
  offset, base classes are placeholders, and the host compiler's alignment rules
  are not the PS2's. Do not `offsetof` anything in there and expect a match.
- **Confidence is judgement, not measurement.** See `docs/confidence.md`.

## Order of work, and where I departed from it

The brief was easiest-first in ranked order. I worked easiest-first *by class
cluster* rather than strictly row by row, because types propagate: doing all 24
StreamNull functions together settles the layout once, where doing them scattered
across the ranking would mean rediscovering it repeatedly. The clusters were
picked in rank order (the top 900 rows are dominated by Synth, StreamNull,
MicNull, ADSR, TrackConfig, Sequence, UIList, and the BeatMatch null sinks) and
that is where most of the 179 came from: 130 of them sit inside the easiest
1,000.

Three deliberate departures, all of them probes rather than harvesting:

1. **`src/math/Vector.cpp`**, ranks 2,368 to 8,946. VU0 macro mode carries the
   heaviest weight in my own score, so I went and tried it specifically to find
   out whether the weight was justified. It was not. Recording that is worth
   more than four more accessors.
2. **`src/rndobj/RndShader.cpp`** and **`src/game/BankLoader.cpp`**, ranks 6,216
   and 8,007. These are the smallest complete examples of the Milo message
   handler shape, and that shape is where the wall turned out to be. I wanted
   the easy end of it documented before hitting the hard end.
3. **`Performer::GetPercentHit`**, rank 10,346, and the Performer virtual group
   around ranks 7,300 to 8,500. These came along with the vtable tool. Once
   `_vt$9Performer` was dumped they stopped being hard, which is itself the
   finding.
