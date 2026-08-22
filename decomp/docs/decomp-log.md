# Decompilation log

One entry per class group, in the order it was worked. Each entry records the
evidence, the confidence, and the struct offsets it pinned. Confidence is my own
reasoned assessment. There is no verification harness, nothing here has been
executed, and no claim below rests on running anything.

Confidence means:

- **high** - the disassembly admits one reading, and at least one independent
  check agrees (a mirrored function, a serialization order, a recovered assert
  string, a vtable entry, or a matching RB3 class).
- **medium** - the mechanism is certain but a name or a semantic reading is
  inferred.
- **low** - not written down. Anything that would have landed here was dropped.

## Tooling that moved the line

Three small tools did most of the work, and they are the reason the wall ended
up so much further out than the ranking predicted.

- `tools/disasm.py` prints the disassembly a recompiled file carries in its
  comments and rewrites every `func_ADDR` placeholder with the real symbol from
  the debug ELF. This alone turns most leaf functions into plain reading.
- `tools/vtable.py` reads the 892 `_vt$` symbols out of the ELF and turns a
  vtable byte offset into a function name. Virtual dispatch in this build is
  `lw vptr / lh delta / lw fn / jalr`, so a call site by itself tells you only
  an offset. Without this, every forwarder through a virtual is a dead end.
- `tools/rodata.py` reads strings and tables out of the loaded segments. Milo
  bakes the source filename, the line number and the literal text of every
  assert into the binary, so this recovers original parameter names and assert
  conditions verbatim.

## synth/ADSR - high

16 functions, 0x257398..0x2576d0.

Layout is pinned twice over. `ADSR::Save` (0x2576d8) and `ADSR::Load` (0x2577e0)
serialize five floats then three ints in ascending offset order through
`BinStream::WriteEndian`/`ReadEndian` with an explicit size of 4 each. Every
setter clears +0x24 on the way out.

Then `tools/rodata.py` recovered the assert strings, which name the original
parameters: `( 0) <= (ar) && (ar) <= ( 60.0f)`, and the same for `dr`, `sr`,
`rr`, plus `( 0.0f) <= (sl) && (sl) <= ( 1.0f)`. The file string is `ADSR.cpp`.
So the parameter names in `src/synth/ADSR.cpp` are the originals, and the range
bounds are quoted, not deduced from float bit patterns.

RB3's `ADSR` has byte-identical offsets, which is a third confirmation.

Offsets: 0x00 mAttackRate, 0x04 mDecayRate, 0x08 mSustainRate, 0x0c
mReleaseRate, 0x10 mSustainLevel, 0x14 mAttackMode, 0x18 mSustainMode, 0x1c
mReleaseMode, 0x20 mPacked (Ps2ADSR, 4 bytes), 0x24 mSynced.

## synth/MicNull - high

19 functions, 0x3e9328..0x3e93d4. Every body is one or two instructions and the
class holds no state. `GetSampleRate` returns 0xbb80, which is 48000, the SPU2
rate. `IsConnected` returns true while `IsRunning` returns false, which is the
only asymmetry and is clearly deliberate.

## synth/StreamNull - high

24 functions in two blocks. 0x3eb098..0x3eb170 is the inert overrides.
0x267de0..0x267ea8 is the five that drive a VarTimer, each a tail call with
`this + 8` as the receiver, which is what pins the timer to +0x08.
`GetFXCore` returns -1, the "unassigned" sentinel.

Offsets: 0x04 a pointer written by the ctor, 0x08 VarTimer, 0x40 a FaderGroup,
0x58/0x5c a `std::vector<Fader *>`, 0x64 an int.

## synth/Synth - high

24 functions. The 0x3e90e0..0x3e91a8 block is the abstract base's neutral
defaults. `GetNumBankSlots` does `(end - begin) >> 3`, so the bank slot element
is 8 bytes. `GetMasterVolume` reads +0x28 of the object at +0x48 while
`SetMasterVolume` hands the same object to `Fader::SetVal` (0x257ae8), which
names both the member and the field inside it.

RB3's `Synth` declares the same virtual set with the same neutral returns.

## synth/Submix - high

One function, and a good demonstration of the vtable tool. `GetNumSlots`
(0x2869c0) calls slot +0x20 on the object at `this + 0x04`. Scanning every
vtable for that slot turns up exactly three, `MassChannelMapping`,
`MultiChannelMapping` and `SingleSlotChannelMapping`, all of them `GetNumSlots`.
That names the slot and types the member in one step.

## game/Sequence - high

11 accessors, 0x259de8..0x259e38. `Sequence::Save` (0x259e48) writes the same
six offsets in ascending order at 4 bytes each, confirming both order and type.
There is no `SetTransposeSpread` in the symbol table, so +0x50 is written only
by `Load`.

Offsets: 0x44 mAvgVolume, 0x48 mVolSpread, 0x4c mAvgTranspose, 0x50
mTransposeSpread, 0x54 mAvgPan, 0x58 mPanSpread.

## game/TrackConfig - high

7 functions, 0x15b960..0x15bbc0. `GemSpacing` multiplies by 0x3d75c28f, which is
0.06f. `GetRawSlotCenter(i)` is `(i - 2) * 4`, so the five fret slots are
numbered 0..4 and centred on 2. `GetSlotCenter` mirrors the index as `4 - i`
when lefty is set.

`SetTrackNum`'s assert strings recovered as `TrackConfig.cpp` and
`trackNum >= 0` at line 121, so the parameter name is the original.

Offsets: 0x10 mTrackNum, 0x14 mLefty, 0x18 mGemSpacing, 0x1c/0x20 gems range.

## game/Performer - high for the direct reads, medium for the band indirection

15 functions, 0x110d48..0x111068.

The plain reads are unambiguous: 0x04 mTotalHits, 0x08 mCurrentStreak, 0x68
mPollMs, 0x6c mScore held as a float and truncated by `cvt.w.s` on the way out.
`SetCrowdRating` tail calls `CrowdRating::SetValue` with `this + 0x40` while
`GetCrowdRating` reads +0x4c directly, so the crowd meter is inline at 0x40 with
its value 0x0c into it. Those are high confidence.

The scoring group needed the vtable dump. `_vt$9Performer` at 0x449e10 is 192
bytes over 23 slots, and it resolves +0x018 GetBaseMultiplier, +0x028
GetMultiplier, +0x040 IsUsingStarPower, +0x088 IsInCrowdWarning, +0x090
GetTotalHits, +0x0a8 GetCrowdBoost, +0x0b0 StarPowerMultiplier.

`StarPowerMultiplier`, `GetCrowdBoost` and `IsUsingStarPower` each call their own
slot on `TheGameConfig->GetPlayerConfig(0)->mPerformer`. Read literally that is
unbounded recursion, and clang says so. My reading is that these base bodies
delegate to the band aggregate sitting in player 0's slot, whose concrete class
overrides them. **Medium**: the mechanism is certain, that explanation is not.
It is flagged in the source rather than smoothed over.

Also pinned: PlayerConfig +0xa8 mTrackNum, +0xb4 mPerformer.

## game/StarPower - high

11 functions, 0x121dd8..0x122c64. Eleven bodies open with the same three
instructions, `lw 0x28` and branch, which makes +0x28 the enable gate beyond
doubt. `SetValue` clamps to [0, 1] against 0x3f800000 and zero. `AddValue` reads
the pool's current value out of the pool object's first word.

Offsets: 0x28 mEnabled, 0x2c mUsing, 0x48 mTrack, 0x4c mWhammyBar, 0x50
mDeployRate, 0x54 mPhraseBoost, 0x58 mMissed, 0x5c mLastSeenGem, 0x68 mReady,
0x6c a params block (0x00 downbeat gain, 0x14 multiplier, 0x18 crowd boost),
0x70 StarPowerPool.

The params block field names are **medium**: the offsets and types are certain,
the roles are read off how the three query functions use them.

## game/TrackWatcherImpl - high

11 functions. `NextGemAfter` computes the gem count as `(end - begin) >> 4`, so
gem entries are 16 bytes. `InSlopWindow` takes `abs.s` of the difference, so the
window is symmetric. `SetCheating` stashes `mUnk50 + 1` only when turning
cheating on, which reads as a "cheating started at this gem" marker rather than
a counter, and that reading is **medium**.

Offsets: 0x04 gem list, 0x10 mSlop, 0x1c mIsCurrentTrack, 0x28 mSyncOffset,
0x48 mEnabled, 0x58 mCheating, 0x64 mCheatStartGem.

## game/PlayerMatcher - high

4 forwarders. The constructor at 0x117338 identifies the members: it zeroes 0x28
and lets `ResetController` fill it, stores the player index at 0x24, and
installs the BeatMatcher at 0x30 and the BeatMatchAudio at 0x34.

`SetRealtime` calls slot +0x78 on the object at +0x28. `BeatMatchController`
itself has `__pure_virtual` there, and all three concrete controllers,
`GuitarController`, `JoypadController` and `JoypadGuitarController`, put
`Disable(bool)` in that slot. The argument register is never touched, so the
bool passes straight through.

## game/BankLoader - high

One function, 0x124e68, chosen because it is the smallest complete Milo message
handler. Message name is node 1 of the array, compared against a function-local
static Symbol cached at 0x4edac0 behind a guard at 0x4edac4. The literal reads
`reset`, the warning format reads `Unhandled msg: %s`.

Worth recording: the two returns differ. The handled path returns tag 0
(kDataUnhandled), the fallback returns value 0 with tag 6 (kDataInt).

## system/StreamingBuffer - high

Two functions, 0x326640 and 0x326688, and the strongest single piece of evidence
in the tree. They are exact mirrors: same three-way branch on the same two
cursors, same wrap fallback on +0x00, same tie-break on +0x0c, with the read and
write roles swapped and the empty/full answer inverted. A wrong layout would
break the symmetry and it does not.

Offsets: 0x00 mSize, 0x04 mReadPos, 0x08 mWritePos, 0x0c mFull.

## system/ArkFile - high

4 functions. `Eof` xors +0x1c against +0x0c and tests for zero, which names both
as the cursor and the length. `Seek` folds three cases into one shared tail by
entering it at different instructions with different values preloaded; an
out-of-range type falls through every test and leaves the cursor alone.

## system/Rand - high

4 functions. `Rand::Int()` is R249: xor two table entries, keep the result in
place, advance both cursors modulo 249. The wrap constant 0xf9 appears twice,
once per cursor, which fixes the table length. `Rand::Int(low, high)` recovered
its parameter names from the assert string `high > low` in `Rand.cpp` line 40.

`NextHashPrime` walks a zero-terminated table at 0x445240, dumped as 29, 37, 41,
47, 53, 67, 79, 97, 107, 131, 157, 181, ... which is the expected shape.

`decode_uleb128` is a textbook ULEB128 with the first byte peeled out.

One inherited quirk is called out in the source rather than fixed: MIPS `div`
puts the remainder in HI with the sign of the dividend, and `Int()` spans the
full signed range, so `Int(low, high)` can return below `low`.

## math/Vector, math/Transform - high

6 functions including four in VU0 macro mode, which the ranking scored as the
hardest thing in the binary and which turned out to be among the easiest to
read. See the wall notes below.

`Add(Vector3, Vector2, Vector3)` adds into the x and z lanes and copies y
through untouched, so the Vector2 is a ground-plane offset. That is not a
reading, it is what the loads and stores do.

`Multiply(Transform, Transform, Transform)` is affine composition: three
rotation rows through an unrolled countdown loop, then the translation row
outside it with an extra `vmaddw` against `vf0w`. `Multiply2` is the identical
code with every field mask widened from `xyz` to `xyzw`.

## rndobj/RndDrawable, rndobj/RndShader - high

4 functions. `sqc2 $vf0, 0(reg)` stores VU0's hardwired vf00, which reads as
(0, 0, 0, 1), as one quadword. It is the engine's fastest padded-Vector3 clear.

`RndDrawable::OnZeroSphere` looks wrong at a glance because it clears through
`$a1`, not `$a0`. It returns a DataNode by value and the g++ 2.x MIPS ABI puts
a hidden return-slot pointer in `$a0`, which pushes `this` into `$a1`. Recording
that once here saves re-deriving it on every other by-value return.

## ui/UIList - high

15 functions, 0x23f838..0x23fff4. The structural finding is that UIList is a
shell: a ListState at +0x150 owns scroll position and speed, a ListDisplay at
+0x1c8 owns geometry, and eleven of the fifteen are one line forwards. Every
forwarder is the same six instructions with the constant offset in the delay
slot, which pins both offsets. `NumData` returns a hard 100, not a member read.
