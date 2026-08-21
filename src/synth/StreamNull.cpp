// StreamNull, the Stream implementation used when audio is unavailable.
//
// Two address ranges. The 0x3eb0xx..0x3eb1xx block is the inert overrides. The
// 0x267dxx..0x267exx block is the handful that still drive an owned VarTimer, so
// a caller polling GetTime sees time advance at the requested speed even though
// nothing is being decoded.

#include "gh2/inferred_types.h"

// --- inert overrides -------------------------------------------------------

void StreamNull::Fill() {}                     // 0x3eb098
bool StreamNull::FillDone() const { return false; } // 0x3eb0a0
void StreamNull::EnableReads(bool) {}          // 0x3eb0a8
void StreamNull::SetVolume(int, float) {}      // 0x3eb0d0
void StreamNull::SetPan(int, float) {}         // 0x3eb0e8
void StreamNull::SetFX(int, bool) {}           // 0x3eb100
bool StreamNull::GetFX(int) const { return false; } // 0x3eb108
void StreamNull::SetFXCore(int, FXCore) {}     // 0x3eb110
void StreamNull::SetJump(float, float, const char *) {} // 0x3eb120
void StreamNull::ClearJump() {}                // 0x3eb128
void StreamNull::EnableSlipStreaming(int) {}   // 0x3eb130
void StreamNull::SetSlipOffset(int, float) {}  // 0x3eb138
void StreamNull::SlipStop(int) {}              // 0x3eb140
void StreamNull::SetSlipSpeed(int, float) {}   // 0x3eb158

// addiu $v0, $zero, -1. -1 is the "not assigned to a core" sentinel.
FXCore StreamNull::GetFXCore(int) const { return kFXCoreNone; } // 0x3eb118

float StreamNull::GetFilePos() const { return 0.0f; }    // 0x3eb0b0
float StreamNull::GetFileLength() const { return 0.0f; } // 0x3eb0c0
float StreamNull::GetSlipOffset(int) const { return 0.0f; } // 0x3eb148

// lw the vector base, scale the index by 4, load through it. No bounds check.
Fader *StreamNull::ChannelFaders(int channel) { // 0x3eb160
    return mFaders[channel];
}

// --- the timer-backed overrides --------------------------------------------
// Each one is a tail call into VarTimer with this + 8 as the receiver, which is
// what pins mTimer to offset 0x08.

void StreamNull::Play() { mTimer.Start(); }   // 0x267de0 -> VarTimer::Start 0x3273d0
void StreamNull::Stop() { mTimer.Stop(); }    // 0x267e00 -> VarTimer::Stop 0x3273f8
float StreamNull::GetTime() { return mTimer.Ms(); } // 0x267e70 -> VarTimer::Ms 0x327590
void StreamNull::SetSpeed(float s) { mTimer.SetSpeed(s); } // 0x267e90 -> 0x327538

// The float argument survives in $f20 across the first call, so the order is
// Stop then Reset, not the other way around.
void StreamNull::Resync(float ms) { // 0x267e30
    mTimer.Stop();
    mTimer.Reset(ms);
}
