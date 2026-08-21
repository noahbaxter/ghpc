// Performer, the per-player scoring object. Addresses 0x110d48..0x111028.
//
// Only the bodies that resolve without guessing at a virtual slot are here. The
// rest of the class dispatches through the g++ 2.x vtable at +0x7c and is left
// for later.

#include "gh2/inferred_types.h"

// --- plain reads -----------------------------------------------------------

int Performer::GetTotalHits() const { return mTotalHits; }         // 0x111018
int Performer::GetCurrentStreak() const { return mCurrentStreak; } // 0x110eb8
float Performer::PollMs() const { return mPollMs; }                // 0x111020

// The running score is kept as a float and truncated on the way out.
// lwc1 / cvt.w.s / mfc1 is a plain C float to int conversion, which on MIPS
// rounds toward zero because the FPU rounding mode is set once at startup.
int Performer::GetScore() const { // 0x110d50
    return (int)mScore;
}

// The base implementation is unconditional. Something else decides whether a
// game-over is actually reachable.
bool Performer::CanGameOver() const { return true; } // 0x110fd0

// Solo tracking exists in the class but is never true in the shipped build.
bool Performer::GetSolo() const { return false; } // 0x110d48

// --- crowd meter -----------------------------------------------------------
// SetCrowdRating tail calls CrowdRating::SetValue with this + 0x40, and
// GetCrowdRating reads +0x4c directly, so the crowd meter is an inline member
// at 0x40 whose value sits 0x0c into it.

float Performer::GetCrowdRating() const { return mCrowdRating.mValue; } // 0x110ec0

void Performer::SetCrowdRating(float rating) { // 0x110ec8
    mCrowdRating.SetValue(rating);
}

// Short-circuits: the multiplayer-versus check only runs when the meter is
// already in warning, and its result is negated with sltiu $s0, $v0, 1.
// Versus mode suppresses the warning entirely.
bool Performer::IsInCrowdWarning() const { // 0x110fd8
    if (!mCrowdRating.IsInWarning())
        return false;
    return !TheGameConfig->IsMultiplayerVs();
}
