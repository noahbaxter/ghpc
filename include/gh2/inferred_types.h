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
class Performer;

// PlayerConfig's only pinned member is the Performer it owns, which three
// Performer methods reach through to consult player 0.
class PlayerConfig {
public:
    unsigned char mUnk00[0xb4];
    Performer *mPerformer; // 0xb4
};

class GameConfig {
public:
    bool IsMultiplayerVs();          // 0x12aa98
    int GetNumPlayers();             // 0x12aaf0
    int GetTrackNum(int player);     // 0x12ac08
    PlayerConfig *GetPlayerConfig(int player); // 0x12b070
};

// Scoring tables, loaded from data.
class Scoring {
public:
    int GetStreakMult(int streak); // 0x11fcc8
};
Scoring *GetScoring(); // 0x11f6b0
extern GameConfig *TheGameConfig; // global at 0x440c10

class SongDB {
public:
    int GetTotalGems(int trackNum, int player); // 0x121670
};
extern SongDB *TheSongDB;         // global at 0x440c00

// Scores a single player's performance for one song.
//
// Offsets pinned by the accessors at 0x110d48..0x111028. The gaps are real
// members that no easy function touches, so they are left unnamed.
class Performer {
public:
    // Virtual, with the vtable byte offset from tools/vtable.py. _vt$9Performer
    // lives at 0x449e10 and is 192 bytes, so the class has 23 slots. The eight
    // not listed here have not been decompiled.
    virtual bool GetSolo() const;           // +0x008
    virtual int GetScore() const;           // +0x010
    virtual int GetBaseMultiplier() const;  // +0x018
    virtual int GetMultiplier() const;      // +0x028
    virtual int GetCurrentStreak() const;   // +0x030
    virtual float GetCrowdRating() const;   // +0x038
    virtual bool IsUsingStarPower() const;  // +0x040
    virtual bool IsInCrowdWarning() const;  // +0x088
    virtual int GetTotalHits() const;       // +0x090
    virtual bool CanGameOver() const;       // +0x0a0
    virtual float GetCrowdBoost() const;    // +0x0a8
    virtual int StarPowerMultiplier() const; // +0x0b0

    // Not in the vtable.
    void SetCrowdRating(float);
    float PollMs() const;
    int GetPercentHit() const;

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


// Star power state for one player. mEnabled at 0x28 gates every mutator and
// every query, which is what makes the whole class read cleanly: the same
// "lw 0x28 / branch" prologue opens eleven different bodies.
class StarPowerPool {
public:
    void SetTargetValue(float); // 0x121720
};

// The tuning block StarPower reads its numbers out of while star power is
// active. Three offsets are pinned, the rest is unexplored.
class StarPowerParams {
public:
    float mDownbeatGain; // 0x00, added once per downbeat when not deployed
    unsigned char mUnk04[0x10];
    int mMultiplier;     // 0x14, score multiplier while deployed
    float mCrowdBoost;   // 0x18, crowd meter boost while deployed
};

class StarPower {
public:
    int GetMultiplier() const;
    float GetCrowdBoost() const;
    bool IsReady() const;
    void SetTrack(int);
    void SetWhammyBar(bool);
    void SetDeployRate(float);
    void SetPhraseBoost(float);
    void SetValue(float);
    void AddValue(float);
    void Jump(float);
    void OnDownbeat();

    unsigned char mUnk00[0x28];
    int mEnabled;               // 0x28
    int mUsing;                 // 0x2c, star power is currently deployed
    unsigned char mUnk30[0x18];
    int mTrack;                 // 0x48
    int mWhammyBar;             // 0x4c
    float mDeployRate;          // 0x50
    float mPhraseBoost;         // 0x54
    int mMissed;                // 0x58, set by EnterMissedState, cleared by Jump
    int mLastSeenGem;           // 0x5c, reset to -1 by Jump
    unsigned char mUnk60[0x08];
    int mReady;                 // 0x68
    StarPowerParams *mParams;   // 0x6c
    StarPowerPool *mPool;       // 0x70, holds the current value at its offset 0
};

// Watches one player's track and drives the sinks. Only the leaf accessors are
// decompiled; the gem bookkeeping past 0x287e70 is not.
class GameGemInfoList {
public:
    void Reset(); // 0x28d208
};

class TrackWatcherImpl {
public:
    void Enable(bool);
    void SetIsCurrentTrack(bool);
    bool IsCheating() const;
    void SetCheating(bool);
    void SetSyncOffset(float);
    void SetAllGemsUnplayed();
    void ResetFill();
    bool GemCanBePassed(int) const;

    unsigned char mUnk00[0x08];
    GameGemInfoList *mGems;     // 0x08
    unsigned char mUnk0c[0x10];
    int mIsCurrentTrack;        // 0x1c
    unsigned char mUnk20[0x08];
    float mSyncOffset;          // 0x28
    unsigned char mUnk2c[0x1c];
    int mEnabled;               // 0x48
    unsigned char mUnk4c[0x04];
    int mUnk50;                 // 0x50, source of the value stashed at 0x64
    unsigned char mUnk54[0x04];
    int mCheating;              // 0x58
    unsigned char mUnk5c[0x08];
    int mCheatStartGem;         // 0x64
};

// ===========================================================================
// ui
// ===========================================================================

// UIList is a thin shell. Its scroll position lives in a ListState at +0x150
// and its geometry in a ListDisplay at +0x1c8, and almost every accessor is a
// one line forward to one of the two.
class ListState {
public:
    int Selected() const;        // 0x243160
    int SelectedDisplay() const; // 0x2431c0
    bool IsScrolling() const;    // 0x243240
    float Speed() const;         // 0x2432a8
    void SetSpeed(float);        // 0x243618
};

class ListDisplay {
public:
    float Spacing() const;     // 0x241128
    float ArrowOffset() const; // 0x241130
    int FadeOffset() const;    // 0x241140
    void SetArrowOffset(float); // 0x241150
    void SetFadeOffset(int);    // 0x241160
};

class UIList {
public:
    virtual void Enter();
    virtual void Exit();
    int NumData() const;
    bool IsCircular() const;
    int NumDisplay() const;
    int Selected() const;
    int SelectedDisplay() const;
    bool IsScrolling() const;
    float Speed() const;
    void SetSpeed(float);
    float Spacing() const;
    float ArrowOffset() const;
    int FadeOffset() const;
    void SetArrowOffset(float);
    void SetFadeOffset(int);

    unsigned char mUnk00[0x150];
    ListState mState;     // 0x150
    unsigned char mUnk15c[0x10];
    int mCircular;        // 0x16c
    int mNumDisplay;      // 0x170
    unsigned char mUnk174[0x54];
    ListDisplay mDisplay; // 0x1c8
};


// ===========================================================================
// system/utl, second pass
// ===========================================================================

// A circular byte buffer. BytesReadable and BytesWriteable are exact mirrors of
// each other, which cross-checks the whole layout: both read 0x04 and 0x08 as
// the two cursors, fall back on 0x00 as the capacity when they wrap, and settle
// the read == write tie with the flag at 0x0c.
class StreamingBuffer {
public:
    int BytesReadable() const;  // 0x326640
    int BytesWriteable() const; // 0x326688

    int mSize;     // 0x00 capacity
    int mReadPos;  // 0x04
    int mWritePos; // 0x08
    int mFull;     // 0x0c breaks the read == write tie
};

// One file inside the ARK archive. Eof compares 0x1c against 0x0c, which is what
// names both: 0x0c is the length and 0x1c is the cursor.
class ArkFile {
public:
    enum SeekType { kSeekSet = 0, kSeekCur = 1, kSeekEnd = 2 };

    int Seek(int offset, SeekType type); // 0x2f8070
    bool Eof();                          // 0x2f80c8
    bool Fail();                         // 0x2f80e0
    void Flush();                        // 0x427a30

    unsigned char mUnk00[0x0c];
    int mSize;                 // 0x0c
    unsigned char mUnk10[0x0c];
    int mPos;                  // 0x1c
    int mError;                // 0x20
};

// R249, a lagged Fibonacci XOR generator. The table is 249 entries starting at
// +0x08, which is pinned by the two wrap comparisons against 0xf9.
class Rand {
public:
    int Int();               // 0x32da88
    int Int(int lo, int hi); // 0x32d980

    int mI;             // 0x00
    int mJ;             // 0x04
    int mTable[249];    // 0x08
};

// Hash table growth. The prime table is a zero-terminated array of ints in the
// data segment at 0x445240.
int NextHashPrime(int atLeast); // 0x32f2e0

// LEB128 decode. Returns the pointer just past the last byte consumed.
const unsigned char *decode_uleb128(const unsigned char *p, unsigned int *out); // 0x104ba8


// ===========================================================================
// math
// ===========================================================================
//
// PS2 Milo pads its vectors to a 16 byte quadword so VU0 can move them with
// lqc2 / sqc2. Every function below either loads a whole quadword or reads
// x at +0x00, y at +0x04 and z at +0x08, so the padded layout is not in doubt.

struct Vector2 {
    float x; // 0x00
    float y; // 0x04
};

struct Vector3 {
    float x; // 0x00
    float y; // 0x04
    float z; // 0x08
    float w; // 0x0c, quadword padding, carried but not used arithmetically
};

namespace Hmx {

struct Quat {
    float x, y, z, w; // 0x00, 0x04, 0x08, 0x0c
};

// Three rows of a rotation matrix, each padded to a quadword.
struct Matrix3 {
    Vector3 x; // 0x00
    Vector3 y; // 0x10
    Vector3 z; // 0x20
};

} // namespace Hmx

// An affine transform. Multiply loops three times over the rotation rows then
// handles the translation row separately, which is what pins the translation to
// 0x30 and the whole thing to four quadwords.
struct Transform {
    Hmx::Matrix3 m; // 0x00
    Vector3 v;      // 0x30 translation
};

// A bounding sphere, written as a single quadword by the VU0 zero idiom.
struct Sphere {
    Vector3 center; // 0x00
    float radius;   // 0x10
};

void Add(const Vector3 &a, const Vector2 &b, Vector3 &out);      // 0x1e41e0
void Subtract(const Vector3 &a, const Vector2 &b, Vector3 &out); // 0x1e4210
void Normalize(const Hmx::Quat &q, Hmx::Quat &out);              // 0x32dbf0
void Multiply(const Vector3 &v, const Hmx::Quat &q, Vector3 &out); // 0x32ece8
void Multiply(const Transform &m, const Transform &n, Transform &out);  // 0x32eec8
void Multiply2(const Transform &m, const Transform &n, Transform &out); // 0x32f188
void MakeRotMatrix(const Hmx::Quat &q, Hmx::Matrix3 &out);       // 0x32e858


// ===========================================================================
// rndobj
// ===========================================================================

// Only the cached bounding sphere is pinned, by the three functions that clear
// it. The centre is a padded Vector3 at +0x10 and the radius the word right
// after it at +0x20.
class RndDrawable {
public:
    void UpdateSphere();                     // 0x3b5860
    DataNode OnZeroSphere(const DataArray *); // 0x1dccd8

    unsigned char mUnk00[0x10];
    Sphere mSphere; // 0x10, centre at 0x10 and radius at 0x20
};


// One mixer submix. GetNumSlots forwards to a ChannelMapping held at +0x04
// through vtable slot +0x20, which every ChannelMapping subclass fills with its
// own GetNumSlots. That is what identifies the member's type.
class ChannelMapping {
public:
    virtual int GetNumSlots() const = 0; // vtable slot +0x20
};

class Submix {
public:
    int GetNumSlots() const; // 0x2869c0

    unsigned char mUnk00[0x04];
    ChannelMapping *mMapping; // 0x04
};


// RndShader carries no state that any decompiled function touches.
class RndShader {
public:
    void Copy(const Hmx::Object *, Hmx::Object::CopyType); // 0x205c28
    DataNode Handle(DataArray *, bool);                    // 0x205c30
};

#endif // GH2_INFERRED_TYPES_H
