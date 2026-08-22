// PlayerMatcher leaf forwarders. Addresses 0x117b08..0x1198ac.
//
// Each of these is six or seven instructions that hand the work to one of the
// three objects PlayerMatcher wires together. The constructor at 0x117338 is
// what identifies them: it zeroes 0x28 and lets ResetController fill it, stores
// the player index at 0x24, and installs the BeatMatcher at 0x30 and the
// BeatMatchAudio at 0x34.
//
// Confidence for every function in this file, with the evidence behind it,
// is in docs/confidence.md.

#include "gh2/inferred_types.h"

bool PlayerMatcher::IsReady() { return mBeatMatcher->IsReady(); } // 0x117b08
float PlayerMatcher::GetSongMs() { return mAudio->GetTime(); }    // 0x117db0

// Note the argument register is never touched, so the bool passes straight
// through to the callee.
//
// The call site only says "vtable slot +0x78 on the object at this + 0x28".
// BeatMatchController's own vtable has __pure_virtual there, and all three
// concrete controllers, GuitarController, JoypadController and
// JoypadGuitarController, put Disable(bool) in that slot. That names both the
// slot and the member's type.
void PlayerMatcher::SetRealtime(bool realtime) { // 0x1180f8
    mController->Disable(realtime);
}

// Reads the current track number straight out of player 0's PlayerConfig at
// +0xa8 rather than out of this matcher.
int PlayerMatcher::GetTrack() { // 0x119888
    return TheGameConfig->GetPlayerConfig(mPlayer)->mTrackNum;
}
