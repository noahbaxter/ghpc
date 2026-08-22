// MicNull, the Mic implementation used when there is no microphone.
//
// Source addresses 0x3e9328..0x3e93d4. Every body is one or two instructions,
// so the whole class reads straight off the disassembly with no ambiguity.
// GH2 never shipped mic support, which is why the whole class is inert.
//
// Confidence for every function in this file, with the evidence behind it,
// is in docs/confidence.md.

#include "gh2/inferred_types.h"

void MicNull::Start() {} // 0x3e9328, jr $ra with an empty delay slot
void MicNull::Stop() {}  // 0x3e9330

bool MicNull::IsRunning() const { return false; } // 0x3e9338, daddu $v0, $zero, $zero

// The one override that does not return a neutral value. Reported connected so
// callers take the normal path and then get silence.
bool MicNull::IsConnected() const { return true; } // 0x3e9340, addiu $v0, $zero, 1

void *MicNull::GetDMA() const { return 0; } // 0x3e9350
void MicNull::SetDMA(bool) {}               // 0x3e9348

void MicNull::SetGain(float) {}         // 0x3e9358
float MicNull::GetGain() const { return 0.0f; } // 0x3e9360, mtc1 $zero, $f0

void MicNull::SetEarpiece(bool) {}          // 0x3e9370
bool MicNull::GetEarpiece() const { return false; } // 0x3e9378

void MicNull::SetEarpieceVolume(float) {}           // 0x3e9380
float MicNull::GetEarpieceVolume() const { return 0.0f; } // 0x3e9388

void MicNull::SetCompressor(bool) {}            // 0x3e9398
bool MicNull::GetCompressor() const { return false; } // 0x3e93a0

void MicNull::SetCompressorParam(float) {}              // 0x3e93a8
float MicNull::GetCompressorParam() const { return 0.0f; } // 0x3e93b0

short *MicNull::GetBuf() { return 0; }        // 0x3e93c0
int MicNull::GetBufSamples() const { return 0; } // 0x3e93c8

// ori $v0, $zero, 0xBB80. 0xBB80 is 48000, the PS2 SPU2 sample rate, so this
// reports the hardware rate even though no samples ever arrive.
int MicNull::GetSampleRate() const { return 48000; } // 0x3e93d0
