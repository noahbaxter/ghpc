// TrackWatcherImpl leaf accessors. Addresses 0x287b28..0x287e88 and 0x3f57a0.
//
// The 0x3f5xxx addresses are the empty virtual defaults, which the linker
// grouped away from the rest of the class.

#include "gh2/inferred_types.h"

void TrackWatcherImpl::Enable(bool on) { mEnabled = on; }              // 0x287b28
void TrackWatcherImpl::SetIsCurrentTrack(bool cur) { mIsCurrentTrack = cur; } // 0x2878f8
void TrackWatcherImpl::SetSyncOffset(float ms) { mSyncOffset = ms; }   // 0x287b58
bool TrackWatcherImpl::IsCheating() const { return mCheating != 0; }   // 0x287b30

// Turning cheating on stashes mUnk50 + 1. Turning it off leaves 0x64 alone, so
// the stash is a "cheating started here" marker rather than a running counter.
void TrackWatcherImpl::SetCheating(bool cheating) { // 0x287b38
    mCheating = cheating;
    if (cheating)
        mCheatStartGem = mUnk50 + 1;
}

// Tail call. The gem list owns the played/unplayed bits.
void TrackWatcherImpl::SetAllGemsUnplayed() { // 0x287e70
    mGems->Reset();
}

// Empty defaults.
void TrackWatcherImpl::ResetFill() {} // 0x3f5768

// The base build lets any gem be passed. A subclass presumably narrows this.
bool TrackWatcherImpl::GemCanBePassed(int) const { return true; } // 0x3f57a0
