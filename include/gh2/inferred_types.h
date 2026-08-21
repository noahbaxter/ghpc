// Struct layouts inferred while decompiling GH2 (PS2, debug build).
//
// Every offset here was read off a load or store in the recompiled output, so
// the offsets are facts. The field names and types are inference, guided by the
// mangled symbol table and by the Rock Band 3 decompilation, which shares the
// Harmonix Milo engine lineage. Anything unconfirmed keeps an unkNN name.
//
// Nothing here is copied from rb3-decomp. Where a layout matches RB3's, that is
// noted as corroborating evidence and the code was still written from the GH2
// disassembly.

#ifndef GH2_INFERRED_TYPES_H
#define GH2_INFERRED_TYPES_H

#include "gh2/milo.h"

// ===========================================================================
// system/utl
// ===========================================================================

// Not decompiled yet. Declared because members hold one.
// Interface pinned by the symbols at 0x327390..0x327640: ctor, Start, Stop,
// Reset(float), SetSpeed(float), Ms(). Size 0x38 is inferred from StreamNull,
// which places it at +0x08 and puts its next member at +0x40.
class VarTimer {
public:
    VarTimer();
    void Start();
    void Stop();
    void Reset(float ms);
    void SetSpeed(float);
    float Ms() const;

private:
    unsigned char mOpaque[0x38];
};

// ===========================================================================
// synth
// ===========================================================================

// FXMode and FXCore live in milo.h, next to the Stream interface that uses
// them.

// Only the value slot is pinned, by Synth::GetMasterVolume reading +0x28 out of
// the master fader while Synth::SetMasterVolume hands the same object to
// Fader::SetVal (0x257ae8).
class Fader {
public:
    void SetVal(float);

    unsigned char mObjectBase[0x28]; // 0x00 Hmx::Object plus intrusive links
    float mVal;                      // 0x28
};

// PS2 hardware ADSR register pair. Layout comes from ADSR::mPacked sitting at
// +0x20 with ADSR::mSynced at +0x24, so the packed form is exactly 4 bytes.
class Ps2ADSR {
public:
    enum AttackMode { kAttackLinear = 0, kAttackExp = 1 };
    enum SustainMode {
        kSustainLinInc = 0,
        kSustainLinDec = 2,
        kSustainExpInc = 4,
        kSustainExpDec = 6,
    };
    enum ReleaseMode { kReleaseLinear = 0, kReleaseExp = 1 };

    unsigned short mReg1; // 0x00
    unsigned short mReg2; // 0x02
};

// Full layout pinned by ADSR::Save (0x2576d8) and ADSR::Load (0x2577e0), which
// serialize the five floats then the three modes in exactly this order, and by
// every setter clearing +0x24.
class ADSR {
public:
    float GetAttackRate() const;
    float GetDecayRate() const;
    float GetSustainRate() const;
    float GetReleaseRate() const;
    float GetSustainLevel() const;
    Ps2ADSR::AttackMode GetAttackMode() const;
    Ps2ADSR::SustainMode GetSustainMode() const;
    Ps2ADSR::ReleaseMode GetReleaseMode() const;

    void SetAttackRate(float);
    void SetDecayRate(float);
    void SetSustainRate(float);
    void SetReleaseRate(float);
    void SetSustainLevel(float);
    void SetAttackMode(Ps2ADSR::AttackMode);
    void SetSustainMode(Ps2ADSR::SustainMode);
    void SetReleaseMode(Ps2ADSR::ReleaseMode);

    float mAttackRate;                 // 0x00 seconds
    float mDecayRate;                  // 0x04 seconds
    float mSustainRate;                // 0x08 seconds
    float mReleaseRate;                // 0x0c seconds
    float mSustainLevel;               // 0x10 0..1
    Ps2ADSR::AttackMode mAttackMode;   // 0x14
    Ps2ADSR::SustainMode mSustainMode; // 0x18
    Ps2ADSR::ReleaseMode mReleaseMode; // 0x1c
    Ps2ADSR mPacked;                   // 0x20
    int mSynced;                       // 0x24 cleared by every setter
};

class Mic;

// The do-nothing Mic used when no hardware mic is present. It carries no state
// of its own past the vtable slot the destructor writes at +0x00.
class MicNull : public Mic {
public:
    virtual ~MicNull();
    virtual void Start();
    virtual void Stop();
    virtual bool IsRunning() const;
    virtual bool IsConnected() const;
    virtual void *GetDMA() const;
    virtual void SetDMA(bool);
    virtual void SetGain(float);
    virtual float GetGain() const;
    virtual void SetEarpiece(bool);
    virtual bool GetEarpiece() const;
    virtual void SetEarpieceVolume(float);
    virtual float GetEarpieceVolume() const;
    virtual void SetCompressor(bool);
    virtual bool GetCompressor() const;
    virtual void SetCompressorParam(float);
    virtual float GetCompressorParam() const;
    virtual short *GetBuf();
    virtual int GetBufSamples() const;
    virtual int GetSampleRate() const;
};

class Stream;

// The do-nothing Stream. Almost every override is empty, but it does own a
// VarTimer so that GetTime keeps advancing for callers that poll it.
//
// Offsets pinned by the 0x267xxx bodies:
//   0x04 a pointer to 0x455240 written by the ctor, almost certainly a vtable
//   0x08 VarTimer, handed to VarTimer::Start/Stop/Reset/SetSpeed/Ms
//   0x40 FaderGroup, passed to a group init at 0x258080
//   0x58 std::vector<Fader *> begin
//   0x5c std::vector<Fader *> end
//   0x64 an int cleared by the ctor
class StreamNull : public Stream {
public:
    virtual ~StreamNull();
    virtual void Play();
    virtual void Stop();
    virtual float GetTime();
    virtual void SetSpeed(float);
    virtual void Resync(float);
    virtual void Fill();
    virtual bool FillDone() const;
    virtual void EnableReads(bool);
    virtual void SetVolume(int, float);
    virtual void SetPan(int, float);
    virtual void SetFX(int, bool);
    virtual bool GetFX(int) const;
    virtual void SetFXCore(int, FXCore);
    virtual FXCore GetFXCore(int) const;
    virtual float GetFilePos() const;
    virtual float GetFileLength() const;
    virtual void SetJump(float, float, const char *);
    virtual void ClearJump();
    virtual void EnableSlipStreaming(int);
    virtual void SetSlipOffset(int, float);
    virtual void SlipStop(int);
    virtual float GetSlipOffset(int) const;
    virtual void SetSlipSpeed(int, float);
    virtual Fader *ChannelFaders(int);

    unsigned char mStreamBase[0x04]; // 0x00 Stream vtable
    void *mUnk04;                    // 0x04 set to 0x455240 by the ctor
    VarTimer mTimer;                 // 0x08
    unsigned char mUnk40[0x18];      // 0x40 FaderGroup
    std::vector<Fader *> mFaders;    // 0x58
    int mUnk64;                      // 0x64
};

// The base audio device. GH2's Synth is the same class RB3 has, with a PS2
// layout of its own. Only the members GH2 code actually touches are named.
class Synth : public Hmx::Object {
public:
    virtual bool Fail();
    virtual void SetFXMode(int, FXMode);
    virtual FXMode GetFXMode(int) const;
    virtual void SetFXVolume(int, float);
    virtual float GetFXVolume(int) const;
    virtual void SetFXDelay(int, float);
    virtual float GetFXDelay(int) const;
    virtual void SetFXFeedback(int, float);
    virtual float GetFXFeedback(int) const;
    virtual void SetFXChain(bool);
    virtual void *GetFXChain() const;
    virtual void SetMicFX(bool);
    virtual bool GetMicFX() const;
    virtual void SetMicVolume(float);
    virtual float GetMicVolume() const;
    virtual void ResumeMics();
    virtual int GetNumConnectedMics();
    virtual void EnableLevels(bool);
    virtual bool LevelsEnabled() const;
    virtual float GetLevel(int) const;
    virtual StreamReader *NewStreamDecoder(File *, StandardStream *, Symbol);

    int GetNumMics() const;
    int GetNumBankSlots() const;
    float GetMasterVolume();
    void SetMasterVolume(float);

    unsigned char mObjectBase[0x28]; // 0x00 Hmx::Object
    int mNumMics;                    // 0x28
    unsigned char mUnk2c[0x1c];      // 0x2c
    Fader *mMasterFader;             // 0x48
    unsigned char mUnk4c[0x04];      // 0x4c
    // 0x50 / 0x54 are the begin and end pointers of a vector whose element is
    // 8 bytes wide (GetNumBankSlots does an arithmetic shift right by 3).
    void *mBankSlotsBegin;           // 0x50
    void *mBankSlotsEnd;             // 0x54
};

// SynthEE is the concrete PS2 Synth. Only the one member GH2's easy accessors
// reach is named.
class SynthEE : public Synth {
public:
    virtual void *GetFXChain() const;

    unsigned char mSynthBase[0x8c];
    void *mFXChain; // 0x8c
};


// ===========================================================================
// game
// ===========================================================================

// The soft "crowd meter". Performer owns one inline at +0x40 and reads its
// value straight out at +0x4c, which puts the value 0x0c into the CrowdRating.
class CrowdRating {
public:
    void SetValue(float);   // 0x129708
    bool IsInWarning() const; // 0x1296b8

    unsigned char mUnk00[0x0c]; // 0x00
    float mValue;               // 0x0c
};

// Only the entry points the decompiled bodies call are declared. Addresses are
// the GH2 function addresses.
class GameConfig {
public:
    bool IsMultiplayerVs();          // 0x12aa98
    int GetNumPlayers();             // 0x12aaf0
    int GetTrackNum(int player);     // 0x12ac08
    class PlayerConfig *GetPlayerConfig(int player); // 0x12b070
};
extern GameConfig *TheGameConfig; // global at 0x440c10

class SongDB {
public:
    int GetTotalGems(int song, int track); // 0x121670
};
extern SongDB *TheSongDB;         // global at 0x440c00

// Scores a single player's performance for one song.
//
// Offsets pinned by the accessors at 0x110d48..0x111028. The gaps are real
// members that no easy function touches, so they are left unnamed.
class Performer {
public:
    int GetTotalHits() const;
    int GetCurrentStreak() const;
    float GetCrowdRating() const;
    void SetCrowdRating(float);
    int GetScore() const;
    float PollMs() const;
    bool CanGameOver() const;
    bool GetSolo() const;
    bool IsInCrowdWarning() const;

    unsigned char mUnk00[0x04];  // 0x00 (vtable pointer lives in mVTable, see below)
    int mTotalHits;              // 0x04
    int mCurrentStreak;          // 0x08
    unsigned char mUnk0c[0x34];  // 0x0c
    CrowdRating mCrowdRating;    // 0x40, value visible at 0x4c
    unsigned char mUnk50[0x18];  // 0x50
    float mPollMs;               // 0x68
    float mScore;                // 0x6c, kept as a float and truncated on read
    unsigned char mUnk70[0x0c];  // 0x70
    void *mVTable;               // 0x7c, see the note on g++ 2.x vtables below
};

// g++ 2.x vtable note. Virtual calls in this build look like
//     lw  $v1, 0x7c($this)   ; vtable pointer
//     lh  $a0, 0xb0($v1)     ; this-adjustment delta, a halfword
//     lw  $v0, 0xb4($v1)     ; function pointer
//     jalr $v0 ; addu $a0, $this, $a0
// so each slot is 8 bytes: a 16 bit delta, 16 bits of padding, then the
// pointer. Slot indices quoted elsewhere in this tree are byte offsets into
// that table, not entry numbers.

// Per-track presentation settings for one player's fretboard.
// Offsets pinned by the setters at 0x15bb40..0x15bbc0.
class TrackConfig {
public:
    float GemSpacing() const;
    float GetRawSlotCenter(int slot) const;
    float GetSlotCenter(int slot) const;
    void SetGemSpacing(float);
    void SetLefty(bool);
    void SetTrackNum(int);
    void SetGemsRange(int first, int last);

    unsigned char mUnk00[0x10]; // 0x00
    int mTrackNum;              // 0x10
    int mLefty;                 // 0x14, stored as a word even though it is a bool
    float mGemSpacing;          // 0x18
    int mGemsRangeFirst;        // 0x1c
    int mGemsRangeLast;         // 0x20
};

// A playable MIDI-ish sequence asset. The six floats are pinned twice over: by
// the accessor pairs at 0x259de8..0x259e38 and by Sequence::Save (0x259e48),
// which writes them in exactly this order.
class Sequence {
public:
    float GetAvgVolume() const;
    float GetVolSpread() const;
    float GetAvgTranspose() const;
    float GetTransposeSpread() const;
    float GetAvgPan() const;
    float GetPanSpread() const;
    void SetAvgVolume(float);
    void SetVolSpread(float);
    void SetAvgTranspose(float);
    void SetAvgPan(float);
    void SetPanSpread(float);

    unsigned char mObjectBase[0x44]; // 0x00
    float mAvgVolume;                // 0x44
    float mVolSpread;                // 0x48
    float mAvgTranspose;             // 0x4c
    float mTransposeSpread;          // 0x50
    float mAvgPan;                   // 0x54
    float mPanSpread;                // 0x58
};

#endif // GH2_INFERRED_TYPES_H
