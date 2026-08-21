// ADSR, the game-facing envelope description in seconds.
//
// Source addresses 0x2572c8..0x2578xx. The whole class is a plain struct with
// checked setters. Every setter clears mSynced so the packed Ps2ADSR form is
// rebuilt on the next use.
//
// Assert line numbers below are the ones GH2 baked into the failure strings, so
// they are the line numbers of the original ADSR.cpp. The strings themselves are
// in the ELF too, and tools/rodata.py reads them back, which is where the
// parameter names ar, dr, sr, rr and sl come from. They are the original names,
// not guesses.

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

void ADSR::SetAttackRate(float ar) { // 0x2573a0, bound 0x42700000 = 60.0f
    // "( 0) <= (ar) && (ar) <= ( 60.0f)" at 0x4aa738
    MILO_ASSERT_RANGE(ar, 0, 60.0f, 405);
    mAttackRate = ar;
    mSynced = 0;
}

void ADSR::SetDecayRate(float dr) { // 0x257438
    // "( 0) <= (dr) && (dr) <= ( 60.0f)" at 0x4aa760
    MILO_ASSERT_RANGE(dr, 0, 60.0f, 419);
    mDecayRate = dr;
    mSynced = 0;
}

void ADSR::SetSustainRate(float sr) { // 0x2574d0
    // "( 0) <= (sr) && (sr) <= ( 60.0f)" at 0x4aa788
    MILO_ASSERT_RANGE(sr, 0, 60.0f, 433);
    mSustainRate = sr;
    mSynced = 0;
}

void ADSR::SetReleaseRate(float rr) { // 0x257600
    // "( 0) <= (rr) && (rr) <= ( 60.0f)" at 0x4aa7d8
    MILO_ASSERT_RANGE(rr, 0, 60.0f, 461);
    mReleaseRate = rr;
    mSynced = 0;
}

void ADSR::SetSustainLevel(float sl) { // 0x257568, bound 0x3f800000 = 1.0f
    // "( 0.0f) <= (sl) && (sl) <= ( 1.0f)" at 0x4aa7b0
    MILO_ASSERT_RANGE(sl, 0.0f, 1.0f, 447);
    mSustainLevel = sl;
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
