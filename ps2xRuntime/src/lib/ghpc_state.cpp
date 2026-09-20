#include "runtime/ghpc_state.h"

#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sys/stat.h>

namespace
{
    std::mutex g_pendingMutex;
    std::string g_pendingSlot;
    std::atomic<bool> g_savePending{false};

    // How many consecutive frames a pending save has been refused. A save that
    // never lands must say so rather than sit silent, because "nothing
    // happened" and "the frame was never quiescent" look identical from the
    // outside and only one of them is a bug.
    unsigned g_refusedFrames = 0;

    bool writeWholeFile(const std::string &path, const std::vector<uint8_t> &buf)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            return false;
        }
        f.write(reinterpret_cast<const char *>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
        return static_cast<bool>(f);
    }

    bool readWholeFile(const std::string &path, std::vector<uint8_t> &out)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f)
        {
            return false;
        }
        const std::streamsize n = f.tellg();
        if (n <= 0)
        {
            return false;
        }
        out.resize(static_cast<size_t>(n));
        f.seekg(0);
        f.read(reinterpret_cast<char *>(out.data()), n);
        return static_cast<bool>(f);
    }
}

void GhpcStateWriter::raw(const void *data, size_t bytes)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    m_buf.insert(m_buf.end(), p, p + bytes);
}

void GhpcStateWriter::beginChunk(const char tag[4])
{
    m_buf.insert(m_buf.end(), tag, tag + 4);
    // Reserve the size word and remember where it went; endChunk backfills it
    // once the payload length is known.
    m_chunkSizeAt = m_buf.size();
    const uint64_t placeholder = 0;
    raw(&placeholder, sizeof(placeholder));
    m_inChunk = true;
}

void GhpcStateWriter::endChunk()
{
    if (!m_inChunk)
    {
        return;
    }
    const uint64_t size = m_buf.size() - m_chunkSizeAt - sizeof(uint64_t);
    std::memcpy(m_buf.data() + m_chunkSizeAt, &size, sizeof(size));
    m_inChunk = false;
}

void GhpcStateWriter::str(const std::string &s)
{
    u64(static_cast<uint64_t>(s.size()));
    raw(s.data(), s.size());
}

bool GhpcStateReader::seekChunk(const char tag[4])
{
    // Always restart from the header: chunk order is not part of the contract,
    // so a caller must not have to know it.
    size_t pos = sizeof(uint64_t) + sizeof(uint32_t);
    while (pos + 12 <= m_size)
    {
        char here[4];
        std::memcpy(here, m_data + pos, 4);
        uint64_t size = 0;
        std::memcpy(&size, m_data + pos + 4, sizeof(size));
        const size_t payload = pos + 12;
        if (payload + size > m_size)
        {
            fail("chunk runs past end of file");
            return false;
        }
        if (std::memcmp(here, tag, 4) == 0)
        {
            m_pos = payload;
            m_chunkEnd = payload + size;
            return true;
        }
        pos = payload + size;
    }
    return false;
}

bool GhpcStateReader::raw(void *out, size_t bytes)
{
    if (m_pos + bytes > m_chunkEnd)
    {
        fail("read past end of chunk");
        return false;
    }
    std::memcpy(out, m_data + m_pos, bytes);
    m_pos += bytes;
    return true;
}

bool GhpcStateReader::str(std::string &s)
{
    uint64_t n = 0;
    if (!u64(n))
    {
        return false;
    }
    if (m_pos + n > m_chunkEnd)
    {
        fail("string runs past end of chunk");
        return false;
    }
    s.assign(reinterpret_cast<const char *>(m_data + m_pos), static_cast<size_t>(n));
    m_pos += static_cast<size_t>(n);
    return true;
}

std::string ghpcStateSlotPath(const std::string &slot)
{
    // Relative to the working directory, which run.sh and play.sh both set to
    // work/ so the game resolves GEN/. Keeping states there means they sit
    // beside the data they describe and stay out of the repo.
    ::mkdir("states", 0755);
    return "states/" + slot + ".ghpcstate";
}

const std::string &ghpcStateLoadSlotFromEnv()
{
    static const std::string slot = []() {
        const char *e = std::getenv("GHPC_STATE_LOAD");
        return e ? std::string(e) : std::string();
    }();
    return slot;
}

void ghpcStateRequestSave(const std::string &slot)
{
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    g_pendingSlot = slot;
    g_refusedFrames = 0;
    g_savePending.store(true, std::memory_order_release);
}

void ghpcStateServicePendingSave(PS2Runtime &runtime, bool quiescent)
{
    if (!g_savePending.load(std::memory_order_acquire))
    {
        return;
    }
    if (!quiescent)
    {
        // Refused, not failed. The next frame is very likely to be clean, and
        // writing anyway is precisely the bug this whole design exists to
        // avoid. Say so periodically so a save that never lands is visible.
        if (++g_refusedFrames % 120u == 0u)
        {
            std::fprintf(stderr,
                         "[ghpc/state] still waiting for a quiescent frame after %u frames\n",
                         g_refusedFrames);
        }
        return;
    }

    std::string slot;
    {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        slot = g_pendingSlot;
    }

    GhpcStateWriter w;
    w.u64(kGhpcStateMagic);
    w.u32(kGhpcStateVersion);

    PS2Memory &mem = runtime.memory();

    w.beginChunk("RDRM");
    w.raw(mem.getRDRAM(), PS2_RAM_SIZE);
    w.endChunk();

    w.beginChunk("SPAD");
    w.raw(mem.getScratchpad(), PS2_SCRATCHPAD_SIZE);
    w.endChunk();

    w.beginChunk("VRAM");
    w.raw(mem.getGSVRAM(), PS2_GS_VRAM_SIZE);
    w.endChunk();

    w.beginChunk("VUMM");
    w.raw(mem.getVU0Code(), PS2_VU0_CODE_SIZE);
    w.raw(mem.getVU0Data(), PS2_VU0_DATA_SIZE);
    w.raw(mem.getVU1Code(), PS2_VU1_CODE_SIZE);
    w.raw(mem.getVU1Data(), PS2_VU1_DATA_SIZE);
    w.endChunk();

    w.beginChunk("EECX");
    w.pod(runtime.cpu());
    w.endChunk();

    const std::string path = ghpcStateSlotPath(slot);
    const bool wrote = writeWholeFile(path, w.buffer());
    std::fprintf(stderr, "[ghpc/state] %s %s (%.1f MB)%s\n",
                 wrote ? "saved" : "FAILED to write", path.c_str(),
                 (double)w.buffer().size() / (1024.0 * 1024.0),
                 wrote ? "" : ", check the path is writable");

    g_savePending.store(false, std::memory_order_release);
    g_refusedFrames = 0;
}

bool ghpcStateLoad(PS2Runtime &runtime, const std::string &slot)
{
    const std::string path = ghpcStateSlotPath(slot);
    std::vector<uint8_t> buf;
    if (!readWholeFile(path, buf))
    {
        std::fprintf(stderr, "[ghpc/state] cannot read %s\n", path.c_str());
        return false;
    }
    if (buf.size() < sizeof(uint64_t) + sizeof(uint32_t))
    {
        std::fprintf(stderr, "[ghpc/state] %s is truncated\n", path.c_str());
        return false;
    }
    uint64_t magic = 0;
    uint32_t version = 0;
    std::memcpy(&magic, buf.data(), sizeof(magic));
    std::memcpy(&version, buf.data() + sizeof(magic), sizeof(version));
    if (magic != kGhpcStateMagic)
    {
        std::fprintf(stderr, "[ghpc/state] %s is not a save state\n", path.c_str());
        return false;
    }
    if (version != kGhpcStateVersion)
    {
        // Refuse rather than guess. A state from another layout would restore a
        // machine that is subtly wrong, which is worse than no state at all.
        std::fprintf(stderr, "[ghpc/state] %s is version %u, this build writes %u\n",
                     path.c_str(), version, kGhpcStateVersion);
        return false;
    }

    GhpcStateReader r(buf.data(), buf.size());
    PS2Memory &mem = runtime.memory();

    const auto need = [&](const char tag[4]) {
        if (!r.seekChunk(tag))
        {
            char name[5] = {tag[0], tag[1], tag[2], tag[3], 0};
            std::fprintf(stderr, "[ghpc/state] %s has no %s chunk\n", path.c_str(), name);
            return false;
        }
        return true;
    };

    if (!need("RDRM") || !r.raw(mem.getRDRAM(), PS2_RAM_SIZE)) return false;
    if (!need("SPAD") || !r.raw(mem.getScratchpad(), PS2_SCRATCHPAD_SIZE)) return false;
    if (!need("VRAM") || !r.raw(mem.getGSVRAM(), PS2_GS_VRAM_SIZE)) return false;
    if (!need("VUMM")) return false;
    if (!r.raw(mem.getVU0Code(), PS2_VU0_CODE_SIZE)) return false;
    if (!r.raw(mem.getVU0Data(), PS2_VU0_DATA_SIZE)) return false;
    if (!r.raw(mem.getVU1Code(), PS2_VU1_CODE_SIZE)) return false;
    if (!r.raw(mem.getVU1Data(), PS2_VU1_DATA_SIZE)) return false;
    if (!need("EECX") || !r.pod(runtime.cpu())) return false;

    if (!r.ok())
    {
        std::fprintf(stderr, "[ghpc/state] %s: %s\n", path.c_str(), r.error().c_str());
        return false;
    }
    std::fprintf(stderr, "[ghpc/state] loaded %s\n", path.c_str());
    return true;
}
