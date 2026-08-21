// Engine primitives the decompiled bodies lean on.
//
// These are placeholders. Their job is to let the decompiled sources name real
// types and compile, not to reproduce the PS2 ABI. The authoritative record of
// where a field actually lives is the offset comment in inferred_types.h.

#ifndef GH2_MILO_H
#define GH2_MILO_H

#include <vector>

namespace Hmx {

// Base of everything in the Milo object system. Not decompiled yet.
class Object {
public:
    virtual ~Object() {}
};

} // namespace Hmx

class DataArray;
// Two words wide: RndDrawable::OnZeroSphere returns an empty one by clearing
// exactly 0x00 and 0x04 of the caller-provided return slot.
class DataNode { public: int mType; int mValue; };
class File;
class Symbol { public: void *mStr; };
class String;
class BinStream;
class ObjectDir;
class StreamReader;
class StandardStream;
class PlayerConfig;

namespace Debug {
void Fail(const char *);   // 0x2ebda8
void Notify(const char *); // 0x2ebd88
} // namespace Debug

// GH2 builds these asserts with the source file and line baked into the message
// through MakeString (0x3680e8), so a failing assert names the exact line of the
// original .cpp. Every occurrence in a decompiled body carries that line number,
// which is how the original file layout gets recovered.
#define MILO_ASSERT(cond, line) \
    do { if (!(cond)) Debug::Fail("assert failed, line " #line); } while (0)

#define MILO_ASSERT_RANGE(val, lo, hi, line) \
    MILO_ASSERT((val) >= (lo) && (val) <= (hi), line)

// --- abstract bases, declared only so the null implementations have something
// --- to override. None of these has been decompiled.

// StreamNull::GetFXCore returns -1 for "no core", which is the only value GH2
// pins directly.
enum FXCore {
    kFXCoreNone = -1,
    kFXCore0 = 0,
    kFXCore1 = 1,
};

// Order taken from RB3. GH2 evidence only pins the zero entry, because the base
// Synth::GetFXMode returns 0.
enum FXMode {
    kFXModeOff = 0,
    kFXModeRoom,
    kFXModeSmallStudio,
    kFXModeMedStudio,
    kFXModeLargeStudio,
    kFXModeHall,
    kFXModeSpace,
    kFXModeEcho,
    kFXModeDelay,
    kFXModePipe,
    kFXModeChorus,
    kFXModeWah,
    kFXModeFlanger,
};

class Mic {
public:
    virtual ~Mic() {}
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual bool IsConnected() const = 0;
    virtual void *GetDMA() const = 0;
    virtual void SetDMA(bool) = 0;
    virtual void SetGain(float) = 0;
    virtual float GetGain() const = 0;
    virtual void SetEarpiece(bool) = 0;
    virtual bool GetEarpiece() const = 0;
    virtual void SetEarpieceVolume(float) = 0;
    virtual float GetEarpieceVolume() const = 0;
    virtual void SetCompressor(bool) = 0;
    virtual bool GetCompressor() const = 0;
    virtual void SetCompressorParam(float) = 0;
    virtual float GetCompressorParam() const = 0;
    virtual short *GetBuf() = 0;
    virtual int GetBufSamples() const = 0;
    virtual int GetSampleRate() const = 0;
};

class Fader;

class Stream {
public:
    virtual ~Stream() {}
    virtual void Play() = 0;
    virtual void Stop() = 0;
    virtual float GetTime() = 0;
    virtual void SetSpeed(float) = 0;
    virtual void Resync(float) = 0;
    virtual void Fill() = 0;
    virtual bool FillDone() const = 0;
    virtual void EnableReads(bool) = 0;
    virtual void SetVolume(int, float) = 0;
    virtual void SetPan(int, float) = 0;
    virtual void SetFX(int, bool) = 0;
    virtual bool GetFX(int) const = 0;
    virtual void SetFXCore(int, FXCore) = 0;
    virtual FXCore GetFXCore(int) const = 0;
    virtual float GetFilePos() const = 0;
    virtual float GetFileLength() const = 0;
    virtual void SetJump(float, float, const char *) = 0;
    virtual void ClearJump() = 0;
    virtual void EnableSlipStreaming(int) = 0;
    virtual void SetSlipOffset(int, float) = 0;
    virtual void SlipStop(int) = 0;
    virtual float GetSlipOffset(int) const = 0;
    virtual void SetSlipSpeed(int, float) = 0;
    virtual Fader *ChannelFaders(int) = 0;
};

#endif // GH2_MILO_H
