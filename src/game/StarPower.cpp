// StarPower, one player's star power meter. Addresses 0x121dd8..0x122c64.
//
// Everything except SetTrack opens with the same three instructions: load
// mEnabled from +0x28 and branch on it. The compiler used branch-likely for the
// two-instruction cases, so the store sits in an annulled delay slot and the
// whole body is just "if enabled, store".

#include "gh2/inferred_types.h"

// The one mutator with no enable gate.
void StarPower::SetTrack(int track) { mTrack = track; } // 0x121dd8

void StarPower::SetWhammyBar(bool on) { // 0x122ba0
    if (mEnabled)
        mWhammyBar = on;
}

void StarPower::SetDeployRate(float rate) { // 0x122c28
    if (mEnabled)
        mDeployRate = rate;
}

void StarPower::SetPhraseBoost(float boost) { // 0x122c40
    if (mEnabled)
        mPhraseBoost = boost;
}

// No enable gate and no use of the argument. The float is accepted so this can
// sit in the same jump-handling interface as everything else.
void StarPower::Jump(float) { // 0x122c58
    mMissed = 0;
    mLastSeenGem = -1;
}

// Clamped to 0..1 before it reaches the pool. The two comparisons are against
// the literals 0x3f800000 and 0x00000000, so the bounds are 1.0f and 0.0f.
void StarPower::SetValue(float value) { // 0x122b40
    if (!mEnabled)
        return;
    if (value > 1.0f)
        value = 1.0f;
    else if (value < 0.0f)
        value = 0.0f;
    mPool->SetTargetValue(value);
}

// The pool's current value is the first word of the pool object, which is how
// AddValue reads it back without an accessor.
void StarPower::AddValue(float delta) { // 0x122b10
    if (!mEnabled)
        return;
    SetValue(*(float *)mPool + delta);
}

// Star power fills on downbeats only while it is not being spent.
void StarPower::OnDownbeat() { // 0x121de0
    if (!mEnabled)
        return;
    if (mUsing)
        return;
    AddValue(mParams->mDownbeatGain);
}

// --- queries ---------------------------------------------------------------
// All three share a shape: neutral value when disabled, neutral value when not
// deployed, otherwise a field out of the params block.

bool StarPower::IsReady() const { // 0x122658
    if (!mEnabled)
        return false;
    return mReady != 0;
}

int StarPower::GetMultiplier() const { // 0x122bb8
    if (!mEnabled)
        return 1;
    if (!mUsing)
        return 1;
    return mParams->mMultiplier;
}

float StarPower::GetCrowdBoost() const { // 0x122be8
    if (!mEnabled)
        return 1.0f;
    if (!mUsing)
        return 1.0f;
    return mParams->mCrowdBoost;
}
