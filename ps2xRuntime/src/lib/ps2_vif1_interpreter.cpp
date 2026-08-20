// Based on Blackline Interactive implementation
#include <chrono>
#include <iomanip>
#include <iostream>
#include "runtime/ps2_memory.h"
#include <cstring>
#include <cstdlib>

enum VIFCmd : uint8_t
{
    VIF_NOP = 0x00,
    VIF_STCYCL = 0x01,
    VIF_OFFSET = 0x02,
    VIF_BASE = 0x03,
    VIF_ITOP = 0x04,
    VIF_STMOD = 0x05,
    VIF_MSKPATH3 = 0x06,
    VIF_MARK = 0x07,
    VIF_FLUSHE = 0x10,
    VIF_FLUSH = 0x11,
    VIF_FLUSHA = 0x13,
    VIF_MSCAL = 0x14,
    VIF_MSCALF = 0x15,
    VIF_MSCNT = 0x17,
    VIF_STMASK = 0x20,
    VIF_STROW = 0x30,
    VIF_STCOL = 0x31,
    VIF_MPG = 0x4A,
    VIF_DIRECT = 0x50,
    VIF_DIRECTHL = 0x51,
};

namespace
{
    constexpr uint8_t kGifFmtImage = 2u;

    // Walk the whole GIFtag chain inside a DIRECT payload and report how many
    // quadwords of a trailing IMAGE transfer spill past the end of that payload.
    // A texture upload is PACKED first (the A+D writes that set BITBLTBUF,
    // TRXPOS, TRXREG and TRXDIR) and only then IMAGE, so looking at the first
    // tag alone always reported 0 and left the pixel data to be parsed as
    // VIFcode.
    uint32_t gifImageQwcFromTag(const uint8_t *data, uint32_t sizeBytes)
    {
        if (!data || sizeBytes < 16u)
            return 0u;

        uint32_t off = 0u;
        while (off + 16u <= sizeBytes)
        {
            uint64_t tagLo = 0u;
            std::memcpy(&tagLo, data + off, sizeof(tagLo));
            const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
            const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
            uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;
            off += 16u;

            uint64_t payload = 0ull;
            if (flg == 0u) // PACKED
                payload = static_cast<uint64_t>(nloop) * nreg * 16ull;
            else if (flg == 1u) // REGLIST, 8 bytes per entry, padded to a qword
                payload = ((static_cast<uint64_t>(nloop) * nreg * 8ull) + 15ull) & ~15ull;
            else // IMAGE or DISABLE
                payload = static_cast<uint64_t>(nloop) * 16ull;

            if (static_cast<uint64_t>(off) + payload > static_cast<uint64_t>(sizeBytes))
            {
                if (flg == kGifFmtImage)
                    return static_cast<uint32_t>(
                        ((static_cast<uint64_t>(off) + payload) - sizeBytes) / 16ull);
                return 0u;
            }
            off += static_cast<uint32_t>(payload);
        }
        return 0u;
    }
}

void PS2Memory::processVIF0Data(uint32_t srcPhys, uint32_t sizeBytes)
{
    if (sizeBytes == 0u || srcPhys >= PS2_RAM_SIZE)
        return;

    const uint64_t requestedEnd = static_cast<uint64_t>(srcPhys) + static_cast<uint64_t>(sizeBytes);
    if (requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE))
        sizeBytes = PS2_RAM_SIZE - srcPhys;

    processVIF0Data(m_rdram + srcPhys, sizeBytes);
}

void PS2Memory::processVIF0Data(const uint8_t *data, uint32_t sizeBytes)
{
    if (sizeBytes == 0u)
        return;

    uint32_t pos = 0;
    while (pos + 4 <= sizeBytes)
    {
        uint32_t cmd = 0u;
        std::memcpy(&cmd, data + pos, sizeof(cmd));
        pos += 4u;

        const uint8_t opcode = static_cast<uint8_t>((cmd >> 24) & 0x7Fu);
        const uint16_t imm = static_cast<uint16_t>(cmd & 0xFFFFu);
        const uint8_t num = static_cast<uint8_t>((cmd >> 16) & 0xFFu);
        const bool irq = (cmd & 0x80000000u) != 0u;

        vif0_regs.code = cmd;
        vif0_regs.num = num;
        if (irq)
            vif0_regs.stat |= (1u << 11);

        if (opcode == VIF_NOP)
        {
            continue;
        }
        else if (opcode == VIF_STCYCL)
        {
            vif0_regs.cycle = imm;
            continue;
        }
        else if (opcode == VIF_ITOP)
        {
            vif0_regs.itops = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_STMOD)
        {
            vif0_regs.mode = imm & 3u;
            continue;
        }
        else if (opcode == VIF_MARK)
        {
            vif0_regs.mark = imm;
            vif0_regs.stat |= (1u << 6);
            continue;
        }
        else if (opcode == VIF_FLUSHE || opcode == VIF_FLUSH || opcode == VIF_FLUSHA)
        {
            continue;
        }
        else if (opcode == VIF_STMASK)
        {
            if (pos + 4u > sizeBytes)
                break;
            std::memcpy(&vif0_regs.mask, data + pos, sizeof(vif0_regs.mask));
            pos += 4u;
            continue;
        }
        else if (opcode == VIF_STROW)
        {
            if (pos + 16u > sizeBytes)
                break;
            std::memcpy(vif0_regs.row, data + pos, 16u);
            pos += 16u;
            continue;
        }
        else if (opcode == VIF_STCOL)
        {
            if (pos + 16u > sizeBytes)
                break;
            std::memcpy(vif0_regs.col, data + pos, 16u);
            pos += 16u;
            continue;
        }
        else if (opcode == VIF_MPG)
        {
            const uint32_t destAddr = static_cast<uint32_t>(imm & 0x1FFu) * 8u;
            const uint32_t instructionCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            const uint32_t mpgBytes = instructionCount * 8u;
            uint32_t copyBytes = 0u;
            if (m_vu0Code && destAddr < PS2_VU0_CODE_SIZE && mpgBytes > 0u)
            {
                copyBytes = mpgBytes;
                if (destAddr + copyBytes > PS2_VU0_CODE_SIZE)
                    copyBytes = PS2_VU0_CODE_SIZE - destAddr;
                if (pos + copyBytes <= sizeBytes)
                {
                    std::memcpy(m_vu0Code + destAddr, data + pos, copyBytes);
                    markVU0CodeModified();
                }
            }

            pos += mpgBytes;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if ((opcode & 0x60u) == 0x60u)
        {
            const uint8_t vn = static_cast<uint8_t>((opcode >> 2) & 0x3u);
            const uint8_t vl = static_cast<uint8_t>(opcode & 0x3u);
            const int components = static_cast<int>(vn) + 1;
            int bitsPerComponent = 32;
            switch (vl)
            {
            case 0:
                bitsPerComponent = 32;
                break;
            case 1:
                bitsPerComponent = 16;
                break;
            case 2:
                bitsPerComponent = 8;
                break;
            case 3:
                bitsPerComponent = (vn == 3u) ? 4 : 16;
                break;
            default:
                break;
            }
            const int bitsPerVector = (vl == 3u && vn == 3u) ? 16 : (components * bitsPerComponent);
            uint32_t bytesPerVector = static_cast<uint32_t>((bitsPerVector + 7) / 8);
            const uint32_t writeVectorCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            uint32_t cl = vif0_regs.cycle & 0xFFu;
            uint32_t wl = (vif0_regs.cycle >> 8) & 0xFFu;
            if (cl == 0u)
                cl = 1u;
            if (wl == 0u)
                wl = 1u;
            uint32_t sourceVectorCount = writeVectorCount;
            if (cl < wl)
            {
                const uint32_t fullBlocks = writeVectorCount / wl;
                uint32_t remainder = writeVectorCount % wl;
                if (remainder > cl)
                    remainder = cl;
                sourceVectorCount = fullBlocks * cl + remainder;
            }
            uint32_t totalBytes = sourceVectorCount * bytesPerVector;
            totalBytes = (totalBytes + 3u) & ~3u;

            if (m_vu0Data && pos + totalBytes <= sizeBytes && vl == 0u)
            {
                uint32_t vuAddr = static_cast<uint32_t>(imm & 0x3FFu);
                if ((imm & 0x8000u) != 0u)
                    vuAddr = (vuAddr + (vif0_regs.tops & 0x3FFu)) & 0x3FFu;
                const uint8_t *srcBase = data + pos;
                uint32_t srcIndex = 0u;
                for (uint32_t writeIndex = 0; writeIndex < writeVectorCount; ++writeIndex)
                {
                    const uint32_t cyclePos = writeIndex % wl;
                    const bool sourceAvailable = (cl >= wl) || (cyclePos < cl);
                    uint32_t destVec = (cl >= wl) ? ((vuAddr + (writeIndex / wl) * cl + cyclePos) & 0x3FFu)
                                                  : ((vuAddr + writeIndex) & 0x3FFu);
                    const uint32_t destOff = destVec * 16u;
                    if (destOff + 16u > PS2_VU0_DATA_SIZE)
                    {
                        if (sourceAvailable && srcIndex < sourceVectorCount)
                            ++srcIndex;
                        continue;
                    }
                    if (!sourceAvailable || srcIndex >= sourceVectorCount)
                        continue;
                    const uint8_t *srcVec = srcBase + srcIndex * bytesPerVector;
                    ++srcIndex;
                    uint32_t lanes[4] = {0u, 0u, 0u, 0u};
                    std::memcpy(lanes, m_vu0Data + destOff, sizeof(lanes));
                    const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                    for (uint32_t c = 0; c < limit; ++c)
                    {
                        uint32_t scalar = 0u;
                        std::memcpy(&scalar, srcVec + c * 4u, sizeof(scalar));
                        lanes[c] = scalar;
                    }
                    _mm_storeu_si128(reinterpret_cast<__m128i *>(m_vu0Data + destOff), _mm_loadu_si128(reinterpret_cast<const __m128i *>(lanes)));
                }
            }
            pos += totalBytes;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else
        {
            break;
        }
    }
}

void PS2Memory::processVIF1Data(uint32_t srcPhys, uint32_t sizeBytes)
{
    if (sizeBytes == 0u || srcPhys >= PS2_RAM_SIZE)
        return;

    const uint64_t requestedEnd = static_cast<uint64_t>(srcPhys) + static_cast<uint64_t>(sizeBytes);
    if (requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE))
        sizeBytes = PS2_RAM_SIZE - srcPhys;

    processVIF1Data(m_rdram + srcPhys, sizeBytes);
}

#if GHPC_DIAG
static int g_vif1TraceRemaining = 0;
unsigned int g_ghpcVif1ChunkSource = 0u;
#include <map>
#include <set>
static std::map<uint32_t,uint32_t> g_mpgRanges;   // dest -> bytes
static std::map<uint32_t,unsigned long long> g_mscalCovered, g_mscalUncovered;
void ghpcNoteMpg(uint32_t dest, uint32_t bytes)
{
    uint32_t &b = g_mpgRanges[dest];
    if (bytes > b) b = bytes;
}
unsigned long long g_ghpcUnpackBytesSinceMscal = 0ull;
unsigned long long g_ghpcMscalRejected = 0ull;
static std::map<uint32_t, unsigned long long> g_unpackDest;
void ghpcNoteUnpackDest(uint32_t vuAddrQw, uint32_t bytes)
{
    ++g_unpackDest[vuAddrQw / 64u];   // bucket by 64-qword regions
    static unsigned long long n = 0;
    if ((++n % 5000ull) == 0ull)
    {
        unsigned long long low = 0, tot = 0;
        for (const auto &kv : g_unpackDest) { tot += kv.second; if (kv.first == 0u) low += kv.second; }
        std::cerr << "[vu1] UNPACK dest regions=" << g_unpackDest.size()
                  << " total=" << tot << " intoQw0-63=" << low << " |";
        int k = 0;
        for (const auto &kv : g_unpackDest) if (k++ < 10) std::cerr << " qw" << (kv.first * 64u) << "=" << kv.second;
        std::cerr << std::endl;
    }
}
unsigned long long g_ghpcImpossibleTag = 0ull;
unsigned long long g_ghpcDirectScanned = 0ull;
unsigned long long g_ghpcDirectSpill = 0ull;
#include <vector>
namespace { struct Fill { uint32_t start, qwc, end; }; std::vector<Fill> g_fills; }
void ghpcNoteRingFill(uint32_t start, uint32_t qwc, uint32_t end)
{
    if (g_fills.size() < 4096u) g_fills.push_back(Fill{start, qwc, end});
}
void ghpcCheckFillTiling(const uint8_t *ring, uint32_t startPhys, uint32_t bytes, uint32_t startAddr)
{
    static unsigned long long checked = 0, tiled = 0, ragged = 0;
    static int shown = 0;
    uint32_t off = 0u;
    bool ok = true;
    while (off + 16u <= bytes)
    {
        uint64_t tag = 0;
        std::memcpy(&tag, ring + startPhys + off, 8);
        const uint32_t qwc = (uint32_t)(tag & 0xFFFFu);
        const uint32_t id  = (uint32_t)((tag >> 28) & 0x7u);
        uint32_t step = 16u;
        if (id == 1u || id == 2u || id == 5u || id == 6u || id == 7u)
            step = 16u + qwc * 16u;   // data follows the tag inside the ring
        if (step == 0u || off + step > bytes) { ok = (off + step == bytes); break; }
        off += step;
    }
    ++checked;
    if (ok && off == bytes) ++tiled; else ++ragged;
    if (!ok && shown < 1)
    {
        ++shown;
        std::cerr << "[vif1] FILL NOT TILED start=0x" << std::hex << startAddr
                  << " bytes=0x" << bytes << " stoppedAt=0x" << off << std::dec << std::endl;
        // Raw content from the last good tag boundary onward.
        for (uint32_t k = 0u; k < bytes && k + 16u <= bytes; k += 16u)
        {
            uint64_t a = 0, b = 0;
            std::memcpy(&a, ring + startPhys + k, 8);
            std::memcpy(&b, ring + startPhys + k + 8, 8);
            std::cerr << "      +0x" << std::hex << k << " " << std::setw(16) << std::setfill('0') << a
                      << " " << std::setw(16) << std::setfill('0') << b
                      << std::dec << std::setfill(' ') << (k == off ? "   <== here" : "") << std::endl;
        }
    }
    if ((checked % 400ull) == 0ull)
        std::cerr << "[vif1] fill tiling: checked=" << checked
                  << " tiled=" << tiled << " ragged=" << ragged << std::endl;
}

void ghpcNoteFillVsTadr(uint32_t fillStart, uint32_t tadr)
{
    static unsigned long long total = 0, aligned = 0, behind = 0, ahead = 0;
    ++total;
    if (fillStart == tadr) ++aligned;
    else if (fillStart > tadr) ++ahead;   // fill starts past where the chain will read
    else ++behind;
    if ((total % 400ull) == 0ull)
        std::cerr << "[vif1] fill vs TADR: total=" << total
                  << " equal=" << aligned << " fillAhead=" << ahead
                  << " fillBehind=" << behind << std::endl;
}

void ghpcReportFillFor(uint32_t addr)
{
    // Which ring fill covered the address the walker expected a tag at, and
    // where did that fill start relative to it?
    for (size_t i = g_fills.size(); i-- > 0;)
    {
        const Fill &f = g_fills[i];
        const uint32_t bytes = f.qwc * 16u;
        if (addr >= f.start && addr < f.start + bytes)
        {
            std::cerr << "    covered by fill #" << i << " start=0x" << std::hex << f.start
                      << " qwc=" << std::dec << f.qwc
                      << " end=0x" << std::hex << f.end
                      << " offsetIntoFill=" << std::dec << (addr - f.start) << std::endl;
            return;
        }
    }
    std::cerr << "    NOT covered by any recorded fill (fills=" << g_fills.size() << ")" << std::endl;
    for (size_t i = g_fills.size(); i-- > 0 && i + 4 >= g_fills.size();)
        std::cerr << "      recent fill start=0x" << std::hex << g_fills[i].start
                  << " qwc=" << std::dec << g_fills[i].qwc
                  << " end=0x" << std::hex << g_fills[i].end << std::dec << std::endl;
}
static std::map<uint32_t, std::pair<unsigned long long, unsigned long long>> g_feed; // pc -> (kicks, bytes)
void ghpcNoteMscalFeed(uint32_t pc)
{
    auto &e = g_feed[pc];
    ++e.first;
    e.second += g_ghpcUnpackBytesSinceMscal;
    g_ghpcUnpackBytesSinceMscal = 0ull;
    static unsigned long long m = 0;
    if ((++m % 400ull) == 0ull)
    {
        std::cerr << "[vif1] impossible tags ended: " << g_ghpcImpossibleTag << std::endl;
        std::cerr << "[vu1] MSCAL rejected=" << g_ghpcMscalRejected
                  << " DIRECT scanned=" << g_ghpcDirectScanned
                  << " withImageSpill=" << g_ghpcDirectSpill << std::endl;
        std::cerr << "[vu1] UNPACK bytes fed per MSCAL (pc: kicks avgBytes):";
        for (const auto &kv : g_feed)
            std::cerr << " 0x" << std::hex << kv.first << std::dec
                      << ":" << kv.second.first
                      << "/" << (kv.second.first ? kv.second.second / kv.second.first : 0ull);
        std::cerr << std::endl;
    }
}
void ghpcNoteMscal(uint32_t pc)
{
    bool covered = false;
    for (const auto &kv : g_mpgRanges)
        if (pc >= kv.first && pc < kv.first + kv.second) { covered = true; break; }
    if (covered) ++g_mscalCovered[pc]; else ++g_mscalUncovered[pc];
    static unsigned long long n = 0;
    if ((++n % 200ull) == 0ull)
    {
        std::cerr << "[vu1] MPG ranges:";
        for (const auto &kv : g_mpgRanges)
            std::cerr << " 0x" << std::hex << kv.first << "+0x" << kv.second << std::dec;
        std::cerr << std::endl << "[vu1] MSCAL into LOADED:";
        for (const auto &kv : g_mscalCovered) std::cerr << " 0x" << std::hex << kv.first << std::dec << "x" << kv.second;
        std::cerr << std::endl << "[vu1] MSCAL into UNLOADED:";
        for (const auto &kv : g_mscalUncovered) std::cerr << " 0x" << std::hex << kv.first << std::dec << "x" << kv.second;
        std::cerr << std::endl;
    }
}
unsigned long long g_ghpcStalls = 0ull;
unsigned long long g_ghpcStallSameAddr = 0ull;
unsigned long long g_ghpcStallResumed = 0ull;
unsigned int g_ghpcLastStallAddr = 0xFFFFFFFFu;
unsigned int g_ghpcLastStallNeed = 0u;
unsigned int g_ghpcLastStallHave = 0u;
unsigned long long g_ghpcMfifoTagPastFill = 0ull;
unsigned long long g_ghpcMfifoTagsChecked = 0ull;
unsigned long long g_ghpcMfifoPayloadPastFill = 0ull;
unsigned long long g_ghpcMfifoPayloadBytesPastFill = 0ull;
unsigned long long g_ghpcTteTagsCovered = 0ull;
unsigned long long g_ghpcTteTagsSkipped = 0ull;
unsigned long long g_ghpcTteSkippedById[8] = {0,0,0,0,0,0,0,0};
unsigned long long g_ghpcSprBytes = 0ull;
unsigned long long g_ghpcSprBytesToVif = 0ull;
unsigned long long g_ghpcSprFills = 0ull;
unsigned long long g_ghpcVifKicks = 0ull;
unsigned long long g_ghpcVif1Bytes = 0ull;
unsigned long long g_ghpcMscalCount = 0ull;
// A command whose payload runs past the end of the chunk is discarded here and
// no residual is carried to the next processVIF1Data call, so the next chunk
// starts parsing payload as commands. Count each truncation by kind.
unsigned long long g_truncDirect = 0ull;
unsigned long long g_truncUnpack = 0ull;
unsigned long long g_truncMpg = 0ull;
unsigned long long g_chunks = 0ull;
unsigned long long g_chunksEndingTruncated = 0ull;
unsigned long long g_bytesDiscarded = 0ull;
// Per-chunk desync accounting: does a truncated tail actually poison the NEXT
// chunk, or is the garbage arriving some other way?
unsigned long long g_dirtyChunks = 0ull;       // chunks containing >=1 invalid opcode
unsigned long long g_dirtyAfterTrunc = 0ull;   // ...of those, ones right after a truncation
unsigned long long g_invalidOpcodes = 0ull;
unsigned long long g_firstBadPosZero = 0ull;   // desync starting at offset 0 of the chunk
bool g_prevChunkTruncated = false;
uint32_t g_curChunkSource = 0u;
uint32_t g_curChunkBytes = 0u;
unsigned long long g_lastTruncTotal = 0ull;
bool g_chunkDirty = false;
int g_chunkFirstBadPos = -1;
// Ring of the last few commands parsed, so a mid-chunk desync can name the
// command whose consumed length was wrong.
struct VifHist { uint32_t pos, cmd; uint8_t op, num; uint16_t imm; uint32_t consumed; };
VifHist g_hist[32] = {};
uint32_t g_histIdx = 0u;
uint32_t g_midChunkDumps = 0u;
// Which feed delivered this chunk: 0 = flattened DMA chain (chainData),
// 1 = scratchpad qwc, 2 = RAM qwc. Set by the caller in ps2_memory.cpp.
unsigned long long g_dirtyBySource[3] = {0,0,0};
unsigned long long g_chunksBySource[3] = {0,0,0};
unsigned long long g_dirtyChunkBytes = 0ull;
inline void pushHist(uint32_t pos, uint32_t cmd, uint8_t op, uint8_t num, uint16_t imm)
{
    if (g_histIdx != 0u)
    {
        VifHist &prev = g_hist[(g_histIdx - 1u) & 31u];
        if (pos >= prev.pos)
            prev.consumed = pos - prev.pos;
    }
    g_hist[g_histIdx & 31u] = VifHist{pos, cmd, op, num, imm, 0u};
    ++g_histIdx;
}
inline void noteConsumed(uint32_t) {}
inline void dumpHist(uint32_t badPos, uint32_t sizeBytes)
{
    if (g_midChunkDumps >= 6u)
        return;
    ++g_midChunkDumps;
    std::cerr << "[vif1] MID-CHUNK DESYNC at pos=0x" << std::hex << badPos
              << " of 0x" << sizeBytes << std::dec
              << " (no preceding truncation). Preceding commands:" << std::endl;
    for (uint32_t i = 0u; i < 32u; ++i)
    {
        const VifHist &h = g_hist[(g_histIdx + i) & 31u];
        if (h.consumed == 0u && h.pos == 0u && h.cmd == 0u)
            continue;
        std::cerr << "    pos=0x" << std::hex << h.pos << " cmd=0x" << h.cmd
                  << " op=0x" << (unsigned)h.op << std::dec
                  << " num=" << (unsigned)h.num
                  << " imm=0x" << std::hex << h.imm << std::dec
                  << " consumed=" << h.consumed
                  << " -> next=0x" << std::hex << (h.pos + h.consumed) << std::dec << std::endl;
    }
}

inline bool vifOpcodeValid(uint8_t op)
{
    if ((op & 0x60u) == 0x60u)
        return true; // UNPACK
    switch (op)
    {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x10: case 0x11: case 0x13: case 0x14: case 0x15: case 0x17:
    case 0x20: case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x4A: case 0x50: case 0x51:
        return true;
    default:
        return false;
    }
}
#endif

void PS2Memory::processVIF1Data(const uint8_t *data, uint32_t sizeBytes)
{
    if (sizeBytes == 0u)
        return;

#if GHPC_DIAG
    {
        g_ghpcVif1Bytes += sizeBytes;
        ++g_chunks;
        g_curChunkSource = g_ghpcVif1ChunkSource;
        g_curChunkBytes = sizeBytes;
        ++g_chunksBySource[g_curChunkSource % 3u];
        if (g_chunkDirty)
        {
            ++g_dirtyChunks;
            ++g_dirtyBySource[g_curChunkSource % 3u];
            g_dirtyChunkBytes += g_curChunkBytes;
            if (g_prevChunkTruncated)
                ++g_dirtyAfterTrunc;
            if (g_chunkFirstBadPos == 0)
                ++g_firstBadPosZero;
        }
        g_prevChunkTruncated = (g_truncDirect + g_truncUnpack + g_truncMpg) != g_lastTruncTotal;
        g_lastTruncTotal = g_truncDirect + g_truncUnpack + g_truncMpg;
        g_chunkDirty = false;
        g_chunkFirstBadPos = -1;
        static auto tRep = std::chrono::steady_clock::now();
        auto nowR = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(nowR - tRep).count() >= 5.0)
        {
            tRep = nowR;
            std::cerr << "[vif1] TOTALS sprAll=" << g_ghpcSprBytes
                      << " sprToVif=" << g_ghpcSprBytesToVif
                      << " fills=" << g_ghpcSprFills
                      << " kicks=" << g_ghpcVifKicks
                      << " vif1Bytes=" << g_ghpcVif1Bytes
                      << " | chunks=" << g_chunks
                      << " truncDirect=" << g_truncDirect
                      << " truncUnpack=" << g_truncUnpack
                      << " truncMpg=" << g_truncMpg
                      << " bytesDiscarded=" << g_bytesDiscarded
                      << " | dirtyChunks=" << g_dirtyChunks
                      << " dirtyAfterTrunc=" << g_dirtyAfterTrunc
                      << " firstBadAtPos0=" << g_firstBadPosZero
                      << " invalidOps=" << g_invalidOpcodes
                      << " | bySource chain=" << g_dirtyBySource[0] << "/" << g_chunksBySource[0]
                      << " spr=" << g_dirtyBySource[1] << "/" << g_chunksBySource[1]
                      << " ram=" << g_dirtyBySource[2] << "/" << g_chunksBySource[2]
                      << " dirtyBytes=" << g_dirtyChunkBytes << std::endl;
            std::cerr << "[vif1] STALL count=" << g_ghpcStalls
                      << " sameAddrRepeat=" << g_ghpcStallSameAddr
                      << " resumed=" << g_ghpcStallResumed
                      << " lastNeed=" << g_ghpcLastStallNeed
                      << " lastHave=" << g_ghpcLastStallHave << std::endl;
            std::cerr << "[vif1] MFIFO tagsChecked=" << g_ghpcMfifoTagsChecked
                      << " tagPastFill=" << g_ghpcMfifoTagPastFill
                      << " payloadPastFill=" << g_ghpcMfifoPayloadPastFill
                      << " payloadBytesPastFill=" << g_ghpcMfifoPayloadBytesPastFill << std::endl;
            std::cerr << "[vif1] TTE tags covered=" << g_ghpcTteTagsCovered
                      << " skipped=" << g_ghpcTteTagsSkipped << " byId:";
            for (uint32_t i = 0u; i < 8u; ++i)
                if (g_ghpcTteSkippedById[i]) std::cerr << " id" << i << "=" << g_ghpcTteSkippedById[i];
            std::cerr << std::endl;
        }
    }
    if (sizeBytes == 0x9a0u)
    {
        static int bigPackets = 0;
        if (bigPackets < 1) { ++bigPackets; g_vif1TraceRemaining = 400;
            std::cerr << "[vif1] TRACE begin packet bytes=" << sizeBytes << std::endl; }
    }
    {
        static uint32_t calls = 0u;
        if (calls < 8u)
        {
            ++calls;
            std::cerr << "[vif1] data bytes=" << sizeBytes << " first=";
            for (uint32_t b = 0u; b < 16u && b < sizeBytes; ++b)
                std::cerr << std::hex << std::setw(2) << std::setfill('0')
                          << (uint32_t)data[b] << ' ';
            std::cerr << std::dec << std::endl;
        }
    }

#endif
    uint32_t pos = 0;

    while (pos + 4 <= sizeBytes)
    {
        if (m_vif1PendingPath2ImageQwc != 0u)
        {
            const uint32_t availableQw = (sizeBytes - pos) / 16u;
            if (availableQw == 0u)
            {
                break;
            }

            const uint32_t chunkQw = std::min<uint32_t>(m_vif1PendingPath2ImageQwc, availableQw);
            std::vector<uint8_t> imagePacket(16u + static_cast<size_t>(chunkQw) * 16u, 0u);
            const uint64_t imageTag =
                static_cast<uint64_t>(chunkQw & 0x7FFFu) |
                ((m_vif1PendingPath2ImageQwc == chunkQw) ? (1ull << 15) : 0ull) |
                (static_cast<uint64_t>(kGifFmtImage) << 58);
            std::memcpy(imagePacket.data(), &imageTag, sizeof(imageTag));
            std::memcpy(imagePacket.data() + 16u, data + pos, static_cast<size_t>(chunkQw) * 16u);
            submitGifPacket(GifPathId::Path2,
                            imagePacket.data(),
                            static_cast<uint32_t>(imagePacket.size()),
                            true,
                            m_vif1PendingPath2DirectHl);

            pos += chunkQw * 16u;
            m_vif1PendingPath2ImageQwc -= chunkQw;
            if (m_vif1PendingPath2ImageQwc == 0u)
            {
                m_vif1PendingPath2DirectHl = false;
            }
            continue;
        }

        uint32_t cmd;
        memcpy(&cmd, data + pos, 4);
        pos += 4;

        uint8_t opcode = (cmd >> 24) & 0x7F;
#if GHPC_DIAG
        {
            static uint32_t histogram[128] = {0};
            static uint32_t total = 0u;
            ++histogram[opcode & 0x7Fu];
            pushHist(pos - 4u, cmd, opcode,
                     static_cast<uint8_t>((cmd >> 16) & 0xFFu),
                     static_cast<uint16_t>(cmd & 0xFFFFu));
            if (!vifOpcodeValid(opcode))
            {
                ++g_invalidOpcodes;
                if (!g_chunkDirty)
                {
                    g_chunkDirty = true;
                    g_chunkFirstBadPos = static_cast<int>(pos - 4u);
                    if (!g_prevChunkTruncated && (pos - 4u) != 0u)
                        dumpHist(pos - 4u, sizeBytes);
                }
            }
            if (++total % 50u == 0u)
            {
                std::cerr << "[vif1] opcode histogram after " << total << ":";
                for (uint32_t o = 0u; o < 128u; ++o)
                {
                    if (histogram[o] != 0u)
                        std::cerr << " 0x" << std::hex << o << std::dec << "=" << histogram[o];
                }
                std::cerr << std::endl;
            }
        }
#endif
        uint16_t imm = cmd & 0xFFFF;
        uint8_t num = (cmd >> 16) & 0xFF;
        const bool irq = (cmd & 0x80000000u) != 0u;
#if GHPC_DIAG
        if (g_vif1TraceRemaining > 0)
        {
            --g_vif1TraceRemaining;
            std::cerr << "[vif1]  @0x" << std::hex << (pos - 4u)
                      << " cmd=0x" << cmd << " op=0x" << (unsigned)opcode
                      << std::dec << " num=" << (unsigned)num
                      << " imm=0x" << std::hex << imm << std::dec << std::endl;
        }
#endif

        // Track most-recent command for VIFn_CODE emulation.
        vif1_regs.code = cmd;
        vif1_regs.num = num;
        if (irq)
            vif1_regs.stat |= (1u << 11); // INT

        if (opcode == VIF_NOP)
        {
            continue;
        }
        else if (opcode == VIF_STCYCL)
        {
            vif1_regs.cycle = imm;
            continue;
        }
        else if (opcode == VIF_OFFSET)
        {
            // VIF double-buffer setup. OFFSET clears DBF and resets TOPS to BASE.
            // Do not rewrite BASE from the previous TOPS value.
            vif1_regs.ofst = imm & 0x3FFu;
            vif1_regs.tops = vif1_regs.base & 0x3FFu;
            vif1_regs.stat &= ~(1u << 7); // clear DBF
            continue;
        }
        else if (opcode == VIF_BASE)
        {
            // BASE only updates the base register. TOPS changes on OFFSET/MSCAL.
            vif1_regs.base = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_ITOP)
        {
            // ITOP VIFcode writes pending ITOPS; VU XITOP observes it after MSCAL/MSCNT.
            vif1_regs.itops = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_STMOD)
        {
            vif1_regs.mode = imm & 3u;
            continue;
        }
        else if (opcode == VIF_MSKPATH3)
        {
            // VIF command docs: MSKPATH3 uses IMMEDIATE bit 15.
            const bool wasMasked = m_path3Masked;
            m_path3Masked = (imm & 0x8000u) != 0u;
            if (wasMasked && !m_path3Masked)
                flushMaskedPath3Packets();
            continue;
        }
        else if (opcode == VIF_MARK)
        {
            vif1_regs.mark = imm;
            vif1_regs.stat |= (1u << 6); // MRK
            continue;
        }
        else if (opcode == VIF_FLUSHE || opcode == VIF_FLUSH || opcode == VIF_FLUSHA)
        {
            continue;
        }
        else if (opcode == VIF_MSCAL || opcode == VIF_MSCALF)
        {
            uint32_t startPC = (uint32_t)imm * 8u;
            // VU1 micro memory is 16 KB, i.e. 2048 instruction pairs, so a real
            // MSCAL always has imm < 2048. Larger values only come from a
            // desynced stream being read as VIFcode; masking them into range
            // (microAddressMask) runs whatever garbage happens to live there.
            if (startPC >= PS2_VU1_CODE_SIZE && !getenv("GHPC_ALLOW_MASKED_MSCAL"))
            {
#if GHPC_DIAG
                extern unsigned long long g_ghpcMscalRejected;
                ++g_ghpcMscalRejected;
#endif
                continue;
            }

            // Values visible to the VU program for this MSCAL.
            // DobieStation semantics: ITOP = ITOPS; TOP = current TOPS;
            // then TOPS/DBF are prepared for the next buffer.
            const uint32_t runTop = vif1_regs.tops & 0x3FFu;
            const uint32_t runItop = vif1_regs.itops & 0x3FFu;
            vif1_regs.top = runTop;
            vif1_regs.itop = runItop;

            const bool dbf = (vif1_regs.stat & (1u << 7)) != 0u;
            if (dbf)
                vif1_regs.tops = vif1_regs.base & 0x3FFu;
            else
                vif1_regs.tops = (vif1_regs.base + vif1_regs.ofst) & 0x3FFu;
            vif1_regs.stat ^= (1u << 7); // toggle DBF

#if GHPC_DIAG
            {
                {
                    // Are legitimate MSCALs to other overlays being lost in
                    // corrupted chunks? Split targets by chunk cleanliness.
                    static std::map<uint32_t, unsigned long long> cleanT, dirtyT;
                    if (g_chunkDirty) ++dirtyT[startPC]; else ++cleanT[startPC];
                    static unsigned long long n = 0;
                    if ((++n % 2000ull) == 0ull)
                    {
                        std::cerr << "[vu1] MSCAL targets in CLEAN chunks:";
                        for (const auto &kv : cleanT) std::cerr << " 0x" << std::hex << kv.first << std::dec << "x" << kv.second;
                        std::cerr << std::endl << "[vu1] MSCAL targets in DIRTY chunks:";
                        for (const auto &kv : dirtyT) std::cerr << " 0x" << std::hex << kv.first << std::dec << "x" << kv.second;
                        std::cerr << std::endl;
                    }
                }
                extern void ghpcNoteMscal(uint32_t pc);
                extern void ghpcNoteMscalFeed(uint32_t pc);
                ghpcNoteMscal(startPC);
                ghpcNoteMscalFeed(startPC);
                // Double-buffer setup decides which VU1 memory the program reads.
                static int n = 0;
                if (n < 10)
                {
                    ++n;
                    std::cerr << "[vu1] MSCAL pc=0x" << std::hex << startPC << std::dec
                              << " top=" << runTop << " itop=" << runItop
                              << " base=" << (vif1_regs.base & 0x3FFu)
                              << " ofst=" << (vif1_regs.ofst & 0x3FFu)
                              << " nextTops=" << (vif1_regs.tops & 0x3FFu)
                              << " dbf=" << ((vif1_regs.stat >> 7) & 1u) << std::endl;
                }
            }
#endif
            if (m_vu1MscalCallback)
                m_vu1MscalCallback(startPC, runTop, runItop);
            continue;
        }
        else if (opcode == VIF_MSCNT)
        {
            const uint32_t runTop = vif1_regs.tops & 0x3FFu;
            const uint32_t runItop = vif1_regs.itops & 0x3FFu;
            vif1_regs.top = runTop;
            vif1_regs.itop = runItop;

            const bool dbf = (vif1_regs.stat & (1u << 7)) != 0u;
            if (dbf)
                vif1_regs.tops = vif1_regs.base & 0x3FFu;
            else
                vif1_regs.tops = (vif1_regs.base + vif1_regs.ofst) & 0x3FFu;
            vif1_regs.stat ^= (1u << 7); // toggle DBF

            if (m_vu1MscntCallback)
                m_vu1MscntCallback(runTop, runItop);
            continue;
        }
        else if (opcode == VIF_STMASK)
        {
            if (pos + 4 > sizeBytes)
                break;
            uint32_t maskValue = 0;
            std::memcpy(&maskValue, data + pos, sizeof(maskValue));
            vif1_regs.mask = maskValue;
            pos += 4;
            continue;
        }
        else if (opcode == VIF_STROW)
        {
            if (pos + 16 > sizeBytes)
                break;
            std::memcpy(vif1_regs.row, data + pos, 16);
            pos += 16;
            continue;
        }
        else if (opcode == VIF_STCOL)
        {
            if (pos + 16 > sizeBytes)
                break;
            std::memcpy(vif1_regs.col, data + pos, 16);
            pos += 16;
            continue;
        }
        else if (opcode == VIF_MPG)
        {
            uint32_t destAddr = (uint32_t)imm * 8u;
            // VIF MPG semantics: NUM==0 means 256 instructions (2048 bytes).
            // MPG payload is instruction-packed and should not be QW-aligned.
            const uint32_t instructionCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);
            const uint32_t mpgBytes = instructionCount * 8u;
#if GHPC_DIAG
            {
                // Track which micro memory ranges actually received code, so an
                // MSCAL into never-loaded memory can be named.
                extern void ghpcNoteMpg(uint32_t dest, uint32_t bytes);
                ghpcNoteMpg(destAddr, mpgBytes);
            }
#endif
            if (m_vu1Code && destAddr < PS2_VU1_CODE_SIZE && mpgBytes > 0)
            {
                uint32_t copyBytes = mpgBytes;
                if (destAddr + copyBytes > PS2_VU1_CODE_SIZE)
                    copyBytes = PS2_VU1_CODE_SIZE - destAddr;
                if (pos + copyBytes <= sizeBytes)
                {
                    std::memcpy(m_vu1Code + destAddr, data + pos, copyBytes);
                    markVU1CodeModified();
                }
            }
            pos += mpgBytes;
            if (pos > sizeBytes)
            {
#if GHPC_DIAG
                ++g_truncMpg;
                g_bytesDiscarded += (pos - sizeBytes);
#endif
                break;
            }
            continue;
        }
        else if (opcode == VIF_DIRECT || opcode == VIF_DIRECTHL)
        {
            uint32_t qwCount = imm;
            if (qwCount == 0)
                qwCount = 65536;
            const uint32_t availableQw = (sizeBytes - pos) / 16u;
            const bool truncated = qwCount > availableQw;
            if (qwCount > availableQw)
                qwCount = availableQw;

            if (qwCount > 0)
            {
                const bool directHl = (opcode == VIF_DIRECTHL);
                submitGifPacket(GifPathId::Path2, data + pos, qwCount * 16, true, directHl);

                // The helper now returns the spill directly, so no inline
                // adjustment is needed.
                const uint32_t spillQw = gifImageQwcFromTag(data + pos, qwCount * 16u);
#if GHPC_DIAG
                {
                    extern unsigned long long g_ghpcDirectScanned, g_ghpcDirectSpill;
                    ++g_ghpcDirectScanned;
                    if (spillQw != 0u) ++g_ghpcDirectSpill;
                }
#endif
                if (spillQw != 0u)
                {
                    m_vif1PendingPath2ImageQwc = spillQw;
                    m_vif1PendingPath2DirectHl = directHl;
                }
            }

            pos += qwCount * 16;
#if GHPC_DIAG
            noteConsumed(pos);
#endif
            if (truncated)
            {
#if GHPC_DIAG
                ++g_truncDirect;
                g_bytesDiscarded += (sizeBytes - pos);
#endif
                pos = sizeBytes;
                break;
            }
            continue;
        }
        else if ((opcode & 0x60) == 0x60)
        {
            uint8_t vn = (opcode >> 2) & 0x3;
            uint8_t vl = opcode & 0x3;
            const bool maskEnable = (opcode & 0x10u) != 0u;
            int components = vn + 1;
            int bitsPerComponent = 32;
            switch (vl)
            {
            case 0:
                bitsPerComponent = 32;
                break;
            case 1:
                bitsPerComponent = 16;
                break;
            case 2:
                bitsPerComponent = 8;
                break;
            case 3:
                bitsPerComponent = (vn == 3) ? 4 : 16;
                break;
            default:
                break;
            }
            int bitsPerVector = (vl == 3 && vn == 3) ? 16 : (components * bitsPerComponent);
            uint32_t bytesPerVector = (bitsPerVector + 7) / 8;
            // UNPACK semantics: NUM is 8-bit and NUM==0 means 256 vectors (writes).
            const uint32_t writeVectorCount = (num == 0u) ? 256u : static_cast<uint32_t>(num);

            // STCYCL controls write cycles for UNPACK.
            uint32_t cl = vif1_regs.cycle & 0xFFu;
            uint32_t wl = (vif1_regs.cycle >> 8) & 0xFFu;
            if (cl == 0u)
                cl = 1u;
            if (wl == 0u)
                wl = 1u;

            uint32_t sourceVectorCount = writeVectorCount;
            if (cl < wl)
            {
                const uint32_t fullBlocks = writeVectorCount / wl;
                uint32_t remainder = writeVectorCount % wl;
                if (remainder > cl)
                    remainder = cl;
                sourceVectorCount = fullBlocks * cl + remainder;
            }

            uint32_t totalBytes = sourceVectorCount * bytesPerVector;
            totalBytes = (totalBytes + 3) & ~3u;

            uint32_t vuAddr = (uint32_t)imm & 0x3FFu;
            if ((imm & 0x8000u) != 0u)
                vuAddr = (vuAddr + (vif1_regs.tops & 0x3FFu)) & 0x3FFu;

            const bool zeroExtend = (imm & 0x4000u) != 0u;

#if GHPC_DIAG
            {
                extern unsigned long long g_ghpcUnpackBytesSinceMscal;
                g_ghpcUnpackBytesSinceMscal += totalBytes;
            }
#endif
#if GHPC_DIAG
            {
                // Where do UNPACKs actually land in VU1 data memory?
                extern void ghpcNoteUnpackDest(uint32_t vuAddrQw, uint32_t bytes);
                ghpcNoteUnpackDest(vuAddr, totalBytes);
            }
#endif
            if (m_vu1Data && totalBytes > 0 && pos + totalBytes <= sizeBytes)
            {
                const uint8_t *srcBase = data + pos;
                uint32_t srcIndex = 0u;
                for (uint32_t writeIndex = 0; writeIndex < writeVectorCount; ++writeIndex)
                {
                    const uint32_t cyclePos = writeIndex % wl;
                    const bool sourceAvailable = (cl >= wl) || (cyclePos < cl);

                    uint32_t destVec = 0;
                    if (cl >= wl)
                    {
                        destVec = (vuAddr + (writeIndex / wl) * cl + cyclePos) & 0x3FFu;
                    }
                    else
                    {
                        destVec = (vuAddr + writeIndex) & 0x3FFu;
                    }

                    uint32_t destOff = destVec * 16u;
                    if (destOff + 16u > PS2_VU1_DATA_SIZE)
                    {
                        if (sourceAvailable && srcIndex < sourceVectorCount)
                            ++srcIndex;
                        continue;
                    }

                    uint32_t lanes[4] = {0u, 0u, 0u, 0u};
                    std::memcpy(lanes, m_vu1Data + destOff, sizeof(lanes));
                    uint32_t decompressed[4] = {lanes[0], lanes[1], lanes[2], lanes[3]};
                    bool decoded = false;

                    const uint8_t *srcVec = nullptr;
                    if (sourceAvailable && srcIndex < sourceVectorCount)
                    {
                        srcVec = srcBase + srcIndex * bytesPerVector;
                        ++srcIndex;
                        decoded = true;
                    }

                    auto extend16 = [&](uint16_t raw) -> uint32_t
                    {
                        if (zeroExtend)
                            return static_cast<uint32_t>(raw);
                        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(raw)));
                    };

                    auto extend8 = [&](uint8_t raw) -> uint32_t
                    {
                        if (zeroExtend)
                            return static_cast<uint32_t>(raw);
                        return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(raw)));
                    };

                    bool handledFormat = true;
                    if (!decoded)
                    {
                        handledFormat = false;
                    }
                    else if (vl == 0u)
                    {
                        if (components == 1)
                        {
                            uint32_t scalar = 0;
                            std::memcpy(&scalar, srcVec, sizeof(scalar));
                            decompressed[0] = scalar;
                            decompressed[1] = scalar;
                            decompressed[2] = scalar;
                            decompressed[3] = scalar;
                        }
                        else
                        {
                            const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                            for (uint32_t c = 0; c < limit; ++c)
                            {
                                uint32_t scalar = 0;
                                std::memcpy(&scalar, srcVec + c * 4u, sizeof(scalar));
                                decompressed[c] = scalar;
                            }
                        }
                    }
                    else if (vl == 1u)
                    {
                        if (components == 1)
                        {
                            uint16_t raw = 0;
                            std::memcpy(&raw, srcVec, sizeof(raw));
                            const uint32_t scalar = extend16(raw);
                            decompressed[0] = scalar;
                            decompressed[1] = scalar;
                            decompressed[2] = scalar;
                            decompressed[3] = scalar;
                        }
                        else
                        {
                            const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                            for (uint32_t c = 0; c < limit; ++c)
                            {
                                uint16_t raw = 0;
                                std::memcpy(&raw, srcVec + c * 2u, sizeof(raw));
                                decompressed[c] = extend16(raw);
                            }
                        }
                    }
                    else if (vl == 2u)
                    {
                        if (components == 1)
                        {
                            const uint32_t scalar = extend8(srcVec[0]);
                            decompressed[0] = scalar;
                            decompressed[1] = scalar;
                            decompressed[2] = scalar;
                            decompressed[3] = scalar;
                        }
                        else
                        {
                            const uint32_t limit = (components > 4) ? 4u : static_cast<uint32_t>(components);
                            for (uint32_t c = 0; c < limit; ++c)
                            {
                                decompressed[c] = extend8(srcVec[c]);
                            }
                        }
                    }
                    else if (vl == 3u && vn == 3u)
                    {
                        // V4-5: packed color-like format in a single 16-bit value.
                        uint16_t packed = 0;
                        std::memcpy(&packed, srcVec, sizeof(packed));
                        decompressed[0] = packed & 0x1Fu;
                        decompressed[1] = (packed >> 5) & 0x1Fu;
                        decompressed[2] = (packed >> 10) & 0x1Fu;
                        decompressed[3] = (packed >> 15) & 0x01u;
                    }
                    else
                    {
                        handledFormat = false;
                    }

                    // Unknown compressed format fallback: preserve legacy raw-copy behavior.
                    if (!handledFormat && decoded && !maskEnable && (vif1_regs.mode == 0u || vif1_regs.mode == 3u))
                    {
                        uint32_t copyBytes = (bytesPerVector < 16u) ? bytesPerVector : 16u;
                        std::memcpy(m_vu1Data + destOff, srcVec, copyBytes);
                        continue;
                    }

                    const bool canAdd = (vl != 3u || vn != 3u);
                    const uint32_t mode = vif1_regs.mode & 3u;
                    const uint32_t colIdx = (cyclePos > 3u) ? 3u : cyclePos;
                    const uint32_t maskCycle = (cyclePos > 3u) ? 3u : cyclePos;

                    for (uint32_t field = 0u; field < 4u; ++field)
                    {
                        uint32_t maskSpec = 0u;
                        if (maskEnable)
                        {
                            const uint32_t shift = ((maskCycle * 4u) + field) * 2u;
                            maskSpec = (vif1_regs.mask >> shift) & 0x3u;
                        }

                        // In fill-write cycles with suspended source reads, treat raw-data selections as row-fill.
                        if (!decoded && maskSpec == 0u)
                            maskSpec = 1u;

                        uint32_t writeVal = lanes[field];
                        if (maskSpec == 0u)
                        {
                            if (handledFormat)
                            {
                                writeVal = decompressed[field];
                                if (canAdd && (mode == 1u || mode == 2u))
                                {
                                    writeVal = writeVal + vif1_regs.row[field];
                                    if (mode == 2u)
                                        vif1_regs.row[field] = writeVal;
                                }
                            }
                        }
                        else if (maskSpec == 1u)
                        {
                            writeVal = vif1_regs.row[field];
                        }
                        else if (maskSpec == 2u)
                        {
                            writeVal = vif1_regs.col[colIdx];
                        }
                        else
                        {
                            continue; // write-protect
                        }

                        lanes[field] = writeVal;
                    }

                    std::memcpy(m_vu1Data + destOff, lanes, sizeof(lanes));
                }
            }
#if GHPC_DIAG
            if (g_vif1TraceRemaining > 0)
                std::cerr << "[vif1]   UNPACK vn=" << (unsigned)vn << " vl=" << (unsigned)vl
                          << " num=" << writeVectorCount
                          << " cl=" << cl << " wl=" << wl
                          << " srcVecs=" << sourceVectorCount
                          << " bytesPerVec=" << bytesPerVector
                          << " payload=" << totalBytes
                          << " pos=0x" << std::hex << pos << "->0x" << (pos + totalBytes)
                          << std::dec << std::endl;
#endif
            pos += totalBytes;
#if GHPC_DIAG
            noteConsumed(pos);
#endif

            if (pos > sizeBytes)
            {
#if GHPC_DIAG
                ++g_truncUnpack;
                g_bytesDiscarded += (pos - sizeBytes);
#endif
                break;
            }
            continue;
        }
        else
        {
#if GHPC_DIAG
            // An unrecognised CMD means the stream is desynced: we skip 4 bytes and
            // keep churning, which hides the real cause. Name it.
            {
                static uint32_t unknownLogs = 0u;
                if (unknownLogs++ < 24u)
                    std::cerr << "[vif1] UNKNOWN cmd=0x" << std::hex << cmd
                              << " opcode=0x" << (unsigned)opcode
                              << " at pos=0x" << (pos - 4u)
                              << "/0x" << sizeBytes << std::dec << std::endl;
            }
#endif
            continue;
        }
    }
}
