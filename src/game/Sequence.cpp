// Sequence accessors. Addresses 0x259de8..0x259e38.
//
// Twelve one-instruction bodies over six floats. Sequence::Save at 0x259e48
// writes the same six offsets in ascending order through BinStream::WriteEndian
// with a size of 4 each, which independently confirms both the order and that
// every one of them really is a 4 byte float.
//
// Confidence for every function in this file, with the evidence behind it,
// is in docs/confidence.md.

#include "gh2/inferred_types.h"

float Sequence::GetAvgVolume() const { return mAvgVolume; }             // 0x259df0
float Sequence::GetVolSpread() const { return mVolSpread; }             // 0x259e00
float Sequence::GetAvgTranspose() const { return mAvgTranspose; }       // 0x259e10
float Sequence::GetTransposeSpread() const { return mTransposeSpread; } // 0x259e18
float Sequence::GetAvgPan() const { return mAvgPan; }                   // 0x259e28
float Sequence::GetPanSpread() const { return mPanSpread; }             // 0x259e38

void Sequence::SetAvgVolume(float v) { mAvgVolume = v; }       // 0x259de8
void Sequence::SetVolSpread(float v) { mVolSpread = v; }       // 0x259df8
void Sequence::SetAvgTranspose(float v) { mAvgTranspose = v; } // 0x259e08
void Sequence::SetAvgPan(float v) { mAvgPan = v; }             // 0x259e20
void Sequence::SetPanSpread(float v) { mPanSpread = v; }       // 0x259e30

// There is no SetTransposeSpread in the symbol table. mTransposeSpread at 0x50
// is written only by Sequence::Load, so it is read-only from code.
