// TrackConfig, the per-player fretboard layout. Addresses 0x15b960..0x15bbc0.
//
// Assert line numbers are the ones GH2 baked into the failure strings, so they
// are line numbers in the original TrackConfig.cpp.

#include "gh2/inferred_types.h"

// mul.s against the literal 0x3d75c28f, which is 0.06f exactly as a float
// constant would round. The stored spacing is in "gems", this converts to the
// world units the track is drawn in.
float TrackConfig::GemSpacing() const { // 0x15b960
    return mGemSpacing * 0.06f;
}

// addiu -2, sll 2, cvt.s.w. The five fret slots are numbered 0..4 and centred
// on slot 2, four units apart.
float TrackConfig::GetRawSlotCenter(int slot) const { // 0x15b9c8
    return (float)((slot - 2) * 4);
}

// Lefty mode mirrors the board, which is a straight 4 - slot reflection of the
// same 0..4 range.
float TrackConfig::GetSlotCenter(int slot) const { // 0x15b998
    if (mLefty)
        slot = 4 - slot;
    return GetRawSlotCenter(slot);
}

void TrackConfig::SetGemSpacing(float spacing) { mGemSpacing = spacing; } // 0x15bbb0
void TrackConfig::SetLefty(bool lefty) { mLefty = lefty; }                // 0x15bb40

// Two stores, the second in the delay slot, so the arguments land at 0x1c and
// 0x20 in argument order.
void TrackConfig::SetGemsRange(int first, int last) { // 0x15bbb8
    mGemsRangeFirst = first;
    mGemsRangeLast = last;
}

// The assert string at 0x474790 reads "trackNum >= 0" and the file string at
// 0x474760 reads "TrackConfig.cpp", so both the condition and the parameter name
// here are the originals rather than reconstructions.
void TrackConfig::SetTrackNum(int trackNum) { // 0x15bb48
    MILO_ASSERT(trackNum >= 0, 121);
    mTrackNum = trackNum;
}
