// ADSR, the game-facing envelope description in seconds.
//
// Source addresses 0x2572c8..0x2578xx. The whole class is a plain struct with
// checked setters. Every setter clears mSynced so the packed Ps2ADSR form is
// rebuilt on the next use.
//
// Assert line numbers below are the ones GH2 baked into the failure strings, so
// they are the line numbers of the original ADSR.cpp.

#include "gh2/inferred_types.h"

// --- getters ---------------------------------------------------------------
// Each of these is a single load in the branch delay slot of jr $ra.

float ADSR::GetAttackRate() const { return mAttackRate; }   // 0x257398
float ADSR::GetDecayRate() const { return mDecayRate; }     // 0x257430
float ADSR::GetSustainRate() const { return mSustainRate; } // 0x2574c8
float ADSR::GetReleaseRate() const { return mReleaseRate; } // 0x2575f8
float ADSR::GetSustainLevel() const { return mSustainLevel; } // 0x257560

Ps2ADSR::AttackMode ADSR::GetAttackMode() const { return mAttackMode; }   // 0x257690
Ps2ADSR::SustainMode ADSR::GetSustainMode() const { return mSustainMode; } // 0x2576a8
Ps2ADSR::ReleaseMode ADSR::GetReleaseMode() const { return mReleaseMode; } // 0x2576c0

// --- rate setters ----------------------------------------------------------
// All four share one shape: a two-sided inclusive range check that calls
// Debug::Fail on the way through, then the store, then mSynced = 0. The upper
// bound is the literal loaded by the lui/mtc1 pair.

void ADSR::SetAttackRate(float rate) { // 0x2573a0, bound 0x4270 0000 = 60.0f
    MILO_ASSERT_RANGE(rate, 0.0f, 60.0f, 405);
    mAttackRate = rate;
    mSynced = 0;
}

void ADSR::SetDecayRate(float rate) { // 0x257438
    MILO_ASSERT_RANGE(rate, 0.0f, 60.0f, 419);
    mDecayRate = rate;
    mSynced = 0;
}

void ADSR::SetSustainRate(float rate) { // 0x2574d0
    MILO_ASSERT_RANGE(rate, 0.0f, 60.0f, 433);
    mSustainRate = rate;
    mSynced = 0;
}

void ADSR::SetReleaseRate(float rate) { // 0x257600
    MILO_ASSERT_RANGE(rate, 0.0f, 60.0f, 461);
    mReleaseRate = rate;
    mSynced = 0;
}

void ADSR::SetSustainLevel(float level) { // 0x257568, bound 0x3F80 0000 = 1.0f
    MILO_ASSERT_RANGE(level, 0.0f, 1.0f, 447);
    mSustainLevel = level;
    mSynced = 0;
}

// --- mode setters ----------------------------------------------------------
// Unchecked. Two stores and a return, with the mSynced clear in the delay slot.

void ADSR::SetAttackMode(Ps2ADSR::AttackMode mode) { // 0x257698
    mAttackMode = mode;
    mSynced = 0;
}

void ADSR::SetSustainMode(Ps2ADSR::SustainMode mode) { // 0x2576b0
    mSustainMode = mode;
    mSynced = 0;
}

void ADSR::SetReleaseMode(Ps2ADSR::ReleaseMode mode) { // 0x2576c8
    mReleaseMode = mode;
    mSynced = 0;
}
