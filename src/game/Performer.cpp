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

// --- scoring, resolved through the vtable dump -----------------------------
//
// These four all dispatch through the g++ 2.x vtable, so the disassembly only
// gives a byte offset. tools/vtable.py reads _vt$9Performer out of the ELF and
// turns those offsets into names, which is what makes them readable at all:
//   +0x018 GetBaseMultiplier   +0x028 GetMultiplier   +0x040 IsUsingStarPower
//   +0x0a8 GetCrowdBoost       +0x0b0 StarPowerMultiplier
//   +0x090 GetTotalHits

int Performer::GetBaseMultiplier() const { // 0x110e30
    return GetScoring()->GetStreakMult(mCurrentStreak);
}

// Slot 0x0b0 runs first and its result is held in $s1 across the second call,
// so the star power factor is the right operand of the multiply.
int Performer::GetMultiplier() const { // 0x110e60
    return GetBaseMultiplier() * StarPowerMultiplier();
}

// The next three consult player 0's Performer rather than this one, each time
// through the slot for the very method being defined. That is a deliberate
// "ask the band" indirection: the concrete class sitting in player 0's
// PlayerConfig overrides the slot, so this base body never re-enters itself in
// practice. Worth flagging, because read literally it looks like unbounded
// recursion.
int Performer::StarPowerMultiplier() const { // 0x111068
    return TheGameConfig->GetPlayerConfig(0)->mPerformer->StarPowerMultiplier();
}

float Performer::GetCrowdBoost() const { // 0x111028
    return TheGameConfig->GetPlayerConfig(0)->mPerformer->GetCrowdBoost();
}

bool Performer::IsUsingStarPower() const { // 0x110f90
    return TheGameConfig->GetPlayerConfig(0)->mPerformer->IsUsingStarPower();
}

// Sums every player's gem count for the track that player is on, then reports
// this performer's hits as a whole-number percentage of it. The loop tests
// GetNumPlayers on every iteration, exactly as written here, because the branch
// target is the call rather than the body.
int Performer::GetPercentHit() const { // 0x110d68
    int totalGems = 0;
    for (int player = 0; player < TheGameConfig->GetNumPlayers(); ++player) {
        int track = TheGameConfig->GetTrackNum(player);
        totalGems += TheSongDB->GetTotalGems(track, player);
    }
    return (int)((float)GetTotalHits() / (float)totalGems * 100.0f);
}
