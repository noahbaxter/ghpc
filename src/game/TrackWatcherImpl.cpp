// TrackWatcherImpl leaf accessors. Addresses 0x287b28..0x287e88 and 0x3f57a0.
//
// The 0x3f5xxx addresses are the empty virtual defaults, which the linker
// grouped away from the rest of the class.
//
// Confidence for every function in this file, with the evidence behind it,
// is in docs/confidence.md.

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

// --- gem window queries ----------------------------------------------------
// The gem list at +0x04 is an STL vector of 16 byte entries; NextGemAfter
// computes its length by shifting the byte difference of the begin and end
// pointers right by four, which is what fixes the element size.

// movn writes the conditional result without a branch: -1 unless the next index
// is still inside the list.
int TrackWatcherImpl::NextGemAfter(int gem) const { // 0x287b60
    int next = gem + 1;
    int count = ((char *)mGems->mEnd - (char *)mGems->mBegin) / 16;
    return next < count ? next : -1;
}

// The sync offset shifts the gem's nominal time before the comparison, and the
// window is symmetric because the difference goes through abs.s.
bool TrackWatcherImpl::InSlopWindow(float nowMs, float gemMs) const { // 0x287c78
    float delta = gemMs + mSyncOffset - nowMs;
    if (delta < 0.0f)
        delta = -delta;
    return delta <= mSlop;
}

// The base window, narrowed by however far the track is offset. The int
// argument is the gem index; the base implementation ignores it, and
// GuitarTrackWatcherImpl's override at 0x28e700 uses it to widen the window for
// hammer-ons.
float TrackWatcherImpl::Slop(int) const { // 0x3f57a8
    return mSlop - mSyncOffset;
}
