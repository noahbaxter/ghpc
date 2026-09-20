#ifndef GHPC_STATE_H
#define GHPC_STATE_H

// Save states for the recompiled game.
//
// The runtime has no native snapshot, and driving to gameplay costs 25s even
// with boot skip and the pad driver, so render work pays that on every
// iteration. This captures a running game and restores it into a fresh
// process.
//
// WHY SAVING IS ONLY LEGAL AT A FRAME BOUNDARY
//
// Almost all state is flat and copyable: RDRAM, the scratchpad, GS VRAM, the
// VU code and data banks, the EE contexts, the kernel tables. What is not
// copyable is a std::function, and `GuestThread` carries three kinds:
// `wait.completion` for a thread parked on something the host will finish,
// `resumeCompletion`, and one `onComplete` per queued `GuestInvocation`.
// A state written while any of those is live would restore a thread waiting
// on a callback that no longer exists. It would load, look correct, and then
// hang or diverge later, which is the worst possible failure for a tool whose
// entire job is reproducing a known situation.
//
// So a save is attempted only at `VBlankStart`, and only when the scheduler
// reports every one of those empty. If they are not, the save is refused and
// retried on the next frame rather than written anyway. Correctness comes from
// that check, not from an assumption: `GHPC_STATE_PROBE` measured 61 samples
// to and on `game_screen` with all of them quiescent and nothing queued, but
// the gate is what makes a written state trustworthy.
//
// In-flight DMA is covered by the same boundary. VIF1, the MFIFO ring and the
// GIF are quiescent at vblank because the guest kicks them inside a store and
// they run to completion inline (`ps2_memory.cpp`), so there is no partially
// drained packet to represent.

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

class PS2Runtime;

// A chunked container, so a state written by an older build fails loudly on
// the chunk it is missing instead of silently restoring a partial machine.
// Layout: magic, version, then repeating { FourCC, uint64 size, payload }.
constexpr uint64_t kGhpcStateMagic = 0x5441545343504847ull; // "GHPCSTAT"
constexpr uint32_t kGhpcStateVersion = 1u;

class GhpcStateWriter
{
public:
    void beginChunk(const char tag[4]);
    void endChunk();
    void raw(const void *data, size_t bytes);
    template <typename T> void pod(const T &value)
    {
        static_assert(std::is_trivially_copyable<T>::value, "pod() needs a trivially copyable type");
        raw(&value, sizeof(T));
    }
    void u32(uint32_t v) { pod(v); }
    void u64(uint64_t v) { pod(v); }
    void str(const std::string &s);
    [[nodiscard]] const std::vector<uint8_t> &buffer() const { return m_buf; }
    [[nodiscard]] std::vector<uint8_t> &buffer() { return m_buf; }

private:
    std::vector<uint8_t> m_buf;
    size_t m_chunkSizeAt = 0;
    bool m_inChunk = false;
};

class GhpcStateReader
{
public:
    GhpcStateReader(const uint8_t *data, size_t bytes) : m_data(data), m_size(bytes) {}
    // Seeks the named chunk. Returns false when it is absent, which the caller
    // must treat as a failed load rather than a default-initialised subsystem.
    bool seekChunk(const char tag[4]);
    bool raw(void *out, size_t bytes);
    template <typename T> bool pod(T &value)
    {
        static_assert(std::is_trivially_copyable<T>::value, "pod() needs a trivially copyable type");
        return raw(&value, sizeof(T));
    }
    bool u32(uint32_t &v) { return pod(v); }
    bool u64(uint64_t &v) { return pod(v); }
    bool str(std::string &s);
    [[nodiscard]] const std::string &error() const { return m_error; }
    void fail(const std::string &why) { if (m_error.empty()) m_error = why; }
    [[nodiscard]] bool ok() const { return m_error.empty(); }

private:
    const uint8_t *m_data = nullptr;
    size_t m_size = 0;
    size_t m_pos = 0;
    size_t m_chunkEnd = 0;
    std::string m_error;
};

// Ask for a save at the next quiescent frame boundary. Safe to call from any
// thread; the write happens on the EE executor. Slots live in work/states/.
void ghpcStateRequestSave(const std::string &slot);

// Called from the VBlankStart handler. Does nothing unless a save is pending.
// `quiescent` is the scheduler's own answer, not a guess made here.
void ghpcStateServicePendingSave(PS2Runtime &runtime, bool quiescent);

// Load a slot into a freshly started runtime. Returns false and leaves the
// runtime untouched on any failure.
bool ghpcStateLoad(PS2Runtime &runtime, const std::string &slot);

// Resolve a slot name to its path under work/states/, creating the directory.
std::string ghpcStateSlotPath(const std::string &slot);

// GHPC_STATE_LOAD, read once. Empty when unset.
const std::string &ghpcStateLoadSlotFromEnv();

#endif
