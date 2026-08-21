// Synth, the abstract audio device. The bodies here are the base class
// defaults, which SynthEE overrides on PS2.
//
// The 0x3e90xx..0x3e91xx block is the defaults, all neutral values. The 0x260xxx
// and 0x262xxx addresses are the non-virtual helpers that are implemented once
// on the base and reach real members.

#include "gh2/inferred_types.h"

// --- base class defaults ---------------------------------------------------

bool Synth::Fail() { return false; } // 0x3e90e0

void Synth::SetFXMode(int, FXMode) {}                 // 0x3e90e8
FXMode Synth::GetFXMode(int) const { return kFXModeOff; } // 0x3e90f0, returns 0

void Synth::SetFXVolume(int, float) {}                  // 0x3e90f8
float Synth::GetFXVolume(int) const { return 0.0f; }    // 0x3e9100

void Synth::SetFXDelay(int, float) {}                   // 0x3e9110
float Synth::GetFXDelay(int) const { return 0.0f; }     // 0x3e9118

void Synth::SetFXFeedback(int, float) {}                // 0x3e9128
float Synth::GetFXFeedback(int) const { return 0.0f; }  // 0x3e9130

void Synth::SetFXChain(bool) {}                         // 0x3e9140
void *Synth::GetFXChain() const { return 0; }           // 0x3e9148

void Synth::SetMicFX(bool) {}                           // 0x3e9150
bool Synth::GetMicFX() const { return false; }          // 0x3e9158
float Synth::GetMicVolume() const { return 0.0f; }      // 0x3e9168
void Synth::ResumeMics() {}                             // 0x3e9180
int Synth::GetNumConnectedMics() { return 0; }          // 0x3e9188

void Synth::EnableLevels(bool) {}                       // 0x3e9198
bool Synth::LevelsEnabled() const { return false; }     // 0x3e91a0
float Synth::GetLevel(int) const { return 0.0f; }       // 0x3e91a8

// The base build has no decoder, so streams that reach it stay silent.
StreamReader *Synth::NewStreamDecoder(File *, StandardStream *, Symbol) { // 0x3e9190
    return 0;
}

// --- shared helpers --------------------------------------------------------

int Synth::GetNumMics() const { return mNumMics; } // 0x262548, lw $v0, 0x28($a0)

// subu then sra by 3, so the element is 8 bytes wide. This is the usual STL
// vector size idiom, (end - begin) / sizeof(T).
int Synth::GetNumBankSlots() const { // 0x260c98
    return (int)((char *)mBankSlotsEnd - (char *)mBankSlotsBegin) / 8;
}

float Synth::GetMasterVolume() { // 0x260900
    return mMasterFader->mVal;
}

void Synth::SetMasterVolume(float vol) { // 0x2608e0 -> Fader::SetVal 0x257ae8
    mMasterFader->SetVal(vol);
}
