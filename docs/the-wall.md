# The wall

## What I expected, and what actually happened

My own score says the hardest thing in this binary is VU0 macro mode, at 9.0 per
instruction. That was wrong. VU0 macro mode is the *easiest* dense code in the
tree.

Macro mode is not a separate processor. It is extra instructions in the ordinary
MIPS stream, each one operating on a whole 16 byte quadword with an explicit
field mask baked into the mnemonic (`vmulax.xyz`, `vmadday.xyz`, `vmaddw.xyz`).
There are 32 vector registers and one accumulator, and a typical body uses five
of them. `Multiply(Transform, Transform, Transform)` at 0x32eec8 is 21
instructions and is a completely unambiguous 4x3 affine composition.
`Multiply2` at 0x32f188 is the same 21 instructions with every mask widened from
`xyz` to `xyzw`, which cross-checks both readings for free. `Normalize(Quat)` at
0x32dbf0 is ten instructions and reads straight off.

That weight should be nearer 2.0. I left it as it is so the ranking in
`data/ranked.tsv` remains the one the work was driven by.

Two other expected walls did not exist either:

- **Jump tables.** Zero functions in the whole 12,663 have a computed `jr`. That
  entire class of difficulty is absent from this binary.
- **Virtual dispatch.** 1,729 functions have an indirect call, and I weighted
  those at 14.0 each on the reasoning that the call site names no target. That
  was true until I dumped the 892 `_vt$` vtable symbols out of the ELF. After
  that, a virtual forwarder is about as easy as a direct one.

## Where the wall actually is

`StarPower::SetUsing(bool)` at 0x122718. 313 instructions, rank 12,200 of
12,422, score 1313.

Roughly 20 of those instructions are StarPower logic. The other ~290 are two
inlined expansions of Milo's "build a static message array and export it"
macro. Per expansion:

- a function-local `static Symbol`, with an "is it built yet" bool guard in .bss
  and the constructor call behind it,
- `PoolAlloc(0x10, 0x10, "DataArray")` and a `DataArray(int)` constructor,
- `DataArray::Node(i)` and `DataNode::operator=` for each of three elements,
- a 16 bit refcount at `+0x0a` of the DataArray, decremented, tested, and a
  conditional `~DataArray` call,
- `atexit` registration so the static array is destroyed at shutdown.

In `SetUsing` alone that is 28 calls, of which 8 are `~DataArray`, 6 are
`DataNode::operator=` and 6 are `DataArray::Node`. The messages are
`start_using` (0x462180) and `stop_using` (0x462190), both recovered from
rodata.

### Why that stopped me

Not because I cannot follow it. I can follow it. The problem is that following
it does not produce a decompilation.

The deliverable is readable, idiomatic C++. Writing out 290 lines of refcount
juggling and pool allocation is a transliteration, which is exactly the thing the
recompiled output already is and exactly the thing this exercise is supposed to
replace. The only honest output is the macro invocation the original source
contained, something on the order of one line.

To write that line I would have to know the macro's exact shape, and a macro
leaves no trace in a binary. It is preprocessor text. GH2's own binary contains
the expansion, never the definition. RB3's decompilation has equivalents, but the
GH1/GH2-era Milo message macros are not the same as RB3's, and I have no way from
GH2 evidence alone to tell which shape produced this particular expansion. Every
version I could write would be a guess dressed as source.

So I stopped. That is a naming and provenance wall, not a comprehension wall,
and it is the honest place to stop.

### How much of the binary is behind it

- 142 functions contain the inlined `DataArray(int)` construction.
- 228 contain the `PoolAlloc` call that goes with it.

So roughly 1 to 2 percent of the binary sits behind this specific wall. Getting
past it needs one of: the GH1/GH2-era Milo headers, a second GH2-era Harmonix
title to diff the expansion against, or a decision to accept the transliteration
for these and clean them up later.

## The wall I did not hit

I want to be plain about this: with `tools/disasm.py`, `tools/vtable.py` and
`tools/rodata.py` in place, I did not hit a general comprehension wall anywhere
in the ranges I worked. Everything I attempted below rank ~10,500 came out at
high or medium confidence. I did not fail on a function and then write it down
anyway.

What I have not done is attempt the genuinely large end. `SpliceKeys` at 6,913
instructions with 6,011 dispatch cases, `CleanupMesh` at 2,880, `RndMesh::Load`
at 1,700. I expect those to be hard for ordinary reasons, volume and state, but I
have not tried them and I am not going to claim a wall I have not walked into.

## Suspected problems in the recompiled reference

Two recompiler bugs were found and fixed in `ghpc` on the same day this work was
done: `SQRT.S` read the wrong source register, and VU0's `vf00`, which is
hardwired to (0, 0, 0, 1) on real hardware, was writable and got clobbered. So
the reference is known to have been wrong before, and it is worth saying what I
noticed.

**One thing I flagged and then cleared.** `Performer::GetScore` at 0x110d50
disassembles as `.word 0x46000064  # cvt.w.s $f1, $f0 # <InstrIdType: CPU_COP1_FPUS>`,
which looks like the recompiler failed to translate a COP1 opcode. It did not.
Reading the emitted C++ shows it produces a correct `FPU_CVT_W_S(ctx->f[0])`.
Only the disassembly pretty-printer does not know the mnemonic. 134 files are
affected, cosmetically. It is worth fixing because it makes the comments
misleading and it silently hides COP1 instructions from anything that parses
them, including my own ranking until I worked around it.

**Nothing else.** I found no function whose translated behaviour looked
nonsensical. Every body I worked on made sense as original code.

**What I would use as regression targets for the vf00 fix**, since these three
depend on `vf00` reading as (0, 0, 0, 1) and would silently produce garbage
against the unfixed runtime:

- `RndDrawable::UpdateSphere` (0x3b5860) and `RndDrawable::OnZeroSphere`
  (0x1dccd8) and `CharLookAt::Enter` (0x18d570), which use `sqc2 $vf0` as a fast
  padded-Vector3 clear. Wrong `vf00` means every bounding sphere clears to
  garbage.
- `Normalize(Hmx::Quat)` (0x32dbf0), where `vrsqrt $Q, $vf0w, $vf5x` uses
  `vf00.w` as the literal 1.0 that makes it a *reciprocal* square root. Wrong
  `vf00` and every quaternion normalize is scaled wrong.
- `Multiply(Transform, Transform, Transform)` (0x32eec8) and `Multiply2`
  (0x32f188), where `vmaddw.xyz $vf9, $vf7, $vf0w` is the implicit 1.0 in the
  homogeneous fourth component. Wrong `vf00` and every composed transform's
  translation is wrong.

These are cheap and precise: each has a hand-checkable expected output and none
of them needs the game running.
