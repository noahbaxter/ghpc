// Based on Blackline Interactive implementation
#include <chrono>
#include <iomanip>
#include <iostream>
#include "runtime/ps2_memory.h"
#include <cstring>
#if GHPC_DIAG
// GHPC_QUIET silences the periodic diagnostic censuses. They are useful when
// hunting a specific defect and pure noise when playing the game.
static bool ghpcQuietLogs()
{
    static const bool quiet = std::getenv("GHPC_QUIET") != nullptr;
    return quiet;
}
#endif

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
#if GHPC_DIAG
    // Same scan as VIF1: did the vf2 init call go down the wrong channel?
    for (uint32_t a = 0u; a + 4u <= sizeBytes; a += 4u)
    {
        uint32_t w = 0u;
        std::memcpy(&w, data + a, 4);
        if ((w & 0x7FFFFFFFu) == 0x140006E6u || (w & 0x7FFFFFFFu) == 0x150006E6u)
        {
            static int initScanLogs0 = 0;
            if (initScanLogs0++ < 8)
                std::fprintf(stderr, "[vif0/initscan] word 0x%08x at pos=%u of %u\n", w, a, sizeBytes);
        }
    }
#endif
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
            // Same STCYCL decode as VIF1: WL=0 means 256, CL=0 reads nothing.
            static const bool s_cycleLegacy0 = std::getenv("GHPC_VIF_STCYCL_LEGACY") != nullptr;
            uint32_t cl = vif0_regs.cycle & 0xFFu;
            uint32_t wl = (vif0_regs.cycle >> 8) & 0xFFu;
            if (s_cycleLegacy0)
            {
                if (cl == 0u)
                    cl = 1u;
                if (wl == 0u)
                    wl = 1u;
            }
            else if (wl == 0u)
            {
                wl = 256u;
            }
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

#if GHPC_DIAG
    extern unsigned int g_ghpcVif1SrcPhys;
    extern int g_ghpcVif1SrcKnown;
    g_ghpcVif1SrcPhys = srcPhys;
    g_ghpcVif1SrcKnown = 1;
#endif
    processVIF1Data(m_rdram + srcPhys, sizeBytes);
#if GHPC_DIAG
    g_ghpcVif1SrcKnown = 0;
#endif
}

#if GHPC_DIAG
static int g_vif1TraceRemaining = 0;
unsigned int g_ghpcVif1ChunkSource = 0u;
unsigned int g_ghpcVif1SrcPhys = 0u;
int g_ghpcVif1SrcKnown = 0;
char g_ghpcCamPre[2][4096] = {};
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
unsigned long long g_ghpcBadBaseRejects = 0ull;
unsigned long long g_ghpcMscalRejected = 0ull;
#if GHPC_DIAG
// Map flattened-chain offsets back to the EE addresses they were copied from.
static unsigned g_chainOff[256], g_chainEe[256], g_chainLen[256];
static int g_chainN = 0;
void ghpcNoteChainChunk(unsigned chainOff, unsigned eeAddr, unsigned bytes, bool reset)
{
    if (reset) { g_chainN = 0; return; }
    if (chainOff == 0u)
        g_chainN = 0;
    if (g_chainN < 256)
    {
        g_chainOff[g_chainN] = chainOff;
        g_chainEe[g_chainN] = eeAddr;
        g_chainLen[g_chainN] = bytes;
        ++g_chainN;
    }
}
unsigned ghpcChainOffsetToEe(unsigned off)
{
    for (int k = 0; k < g_chainN; ++k)
        if (off >= g_chainOff[k] && off < g_chainOff[k] + g_chainLen[k])
            return g_chainEe[k] + (off - g_chainOff[k]);
    return 0xFFFFFFFFu;
}
// GHPC_VIF1_DUMPCHUNK=<dir>: write a chunk that goes wrong to disk, raw, with
// the DMA tags that built it and every command parsed so far, so the walk can
// be redone offline against PCSX2's sizing rules. The tag table is built at
// kick time and consumed at parse time, which can be several kicks later, so
// tables queue up in kick order.
#include <deque>
#include <string>
#include <cstdio>
extern uint32_t g_curChunkSource;
namespace
{
    struct ChainTag { unsigned chainOff, tagAddr, id, qwc, addr; unsigned long long lo, hi; };
    std::vector<ChainTag> g_tagBuild;
    std::deque<std::vector<ChainTag>> g_tagQueue;
    std::vector<ChainTag> g_curTags;
    struct VifTrace { uint32_t pos, cmd; };
    std::vector<VifTrace> g_chunkTrace;
    struct VifEntry { uint32_t cycle, base, ofst, tops, stat, residual, pendingImg; } g_chunkEntry;
    int g_chunkDumps = 0;
    bool g_chunkDumped = false;
    const char *ghpcDumpDir()
    {
        static const char *d = std::getenv("GHPC_VIF1_DUMPCHUNK");
        return d;
    }
}
void ghpcNoteChainTag(unsigned chainOff, unsigned tagAddr, unsigned id, unsigned qwc, unsigned addr,
                      unsigned long long lo, unsigned long long hi)
{
    if (!ghpcDumpDir()) return;
    if (chainOff == 0u && !g_tagBuild.empty() && g_tagBuild.back().chainOff != 0u)
        g_tagBuild.clear();
    if (g_tagBuild.size() < 4096u)
        g_tagBuild.push_back(ChainTag{chainOff, tagAddr, id, qwc, addr, lo, hi});
}
void ghpcChainKickCommit(bool pushed)
{
    if (!ghpcDumpDir()) return;
    if (pushed && g_tagQueue.size() < 64u)
        g_tagQueue.push_back(g_tagBuild);
    g_tagBuild.clear();
}
static void ghpcChunkBegin(uint32_t src, uint32_t residual, uint32_t pendingImg,
                           uint32_t cycle, uint32_t base, uint32_t ofst, uint32_t tops, uint32_t stat)
{
    if (!ghpcDumpDir()) return;
    g_chunkTrace.clear();
    g_curTags.clear();
    g_chunkDumped = false;
    if (src == 0u && !g_tagQueue.empty())
    {
        g_curTags = std::move(g_tagQueue.front());
        g_tagQueue.pop_front();
    }
    g_chunkEntry = VifEntry{cycle, base, ofst, tops, stat, residual, pendingImg};
}
static void ghpcDumpChunk(const char *reason, const uint8_t *data, uint32_t sizeBytes, uint32_t pos)
{
    const char *dir = ghpcDumpDir();
    if (!dir || g_chunkDumps >= 8 || g_chunkDumped) return;
    ++g_chunkDumps;
    g_chunkDumped = true;
    extern unsigned long long g_ghpcVu1Mscals;
    char base[512];
    std::snprintf(base, sizeof(base), "%s/vif1chunk-%d-mscal%llu", dir, g_chunkDumps, g_ghpcVu1Mscals);
    std::string bin = std::string(base) + ".bin", txt = std::string(base) + ".txt";
    if (FILE *f = std::fopen(bin.c_str(), "wb")) { std::fwrite(data, 1, sizeBytes, f); std::fclose(f); }
    FILE *f = std::fopen(txt.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "reason=%s pos=%u size=%u mscals=%llu src=%u\n", reason, pos, sizeBytes,
                 g_ghpcVu1Mscals, g_curChunkSource);
    std::fprintf(f, "entry cycle=0x%04x base=%u ofst=%u tops=%u dbf=%u residual=%u pendingImgQw=%u\n",
                 g_chunkEntry.cycle & 0xFFFFu, g_chunkEntry.base, g_chunkEntry.ofst, g_chunkEntry.tops,
                 (g_chunkEntry.stat >> 7) & 1u, g_chunkEntry.residual, g_chunkEntry.pendingImg);
    std::fprintf(f, "tags (chainOff is before the residual prefix)\n");
    for (const ChainTag &t : g_curTags)
        std::fprintf(f, "tag chainOff=%u tag@0x%x id=%u qwc=%u addr=0x%x lo=0x%016llx hi=0x%016llx\n",
                     t.chainOff, t.tagAddr, t.id, t.qwc, t.addr, t.lo, t.hi);
    std::fprintf(f, "pieces\n");
    for (int k = 0; k < g_chainN; ++k)
        std::fprintf(f, "piece chainOff=%u ee=0x%x len=%u\n", g_chainOff[k], g_chainEe[k], g_chainLen[k]);
    std::fprintf(f, "trace (pos cmd), parsed before the trigger\n");
    for (const VifTrace &t : g_chunkTrace)
        std::fprintf(f, "cmd pos=%u 0x%08x\n", t.pos, t.cmd);
    std::fclose(f);
    std::fprintf(stderr, "[vif1/dumpchunk] #%d %s wrote %s (pos=%u size=%u mscals=%llu)\n",
                 g_chunkDumps, reason, bin.c_str(), pos, sizeBytes, g_ghpcVu1Mscals);
}
#endif

static std::map<uint32_t, unsigned long long> g_unpackDest;
unsigned long long g_unpackWrapped = 0ull;
unsigned long long g_unpackMaxEndQw = 0ull;
void ghpcNoteUnpackDest(uint32_t vuAddrQw, uint32_t bytes)
{
    ++g_unpackDest[vuAddrQw / 64u];   // bucket by 64-qword regions
    // VU1 data memory is 1024 quadwords and the unpack write loop masks each
    // destination with 0x3FF, so a transfer starting near the top silently
    // wraps to address 0 and overwrites whatever constants live there. Count
    // transfers whose end passes 1024 so that stops being a guess.
    {
        const unsigned long long endQw =
            (unsigned long long)vuAddrQw + (unsigned long long)((bytes + 15u) / 16u);
        if (endQw > g_unpackMaxEndQw) g_unpackMaxEndQw = endQw;
        if (endQw > 1024ull) ++g_unpackWrapped;
    }
    static unsigned long long n = 0;
    if ((++n % 5000ull) == 0ull)
    {
        unsigned long long low = 0, tot = 0;
        for (const auto &kv : g_unpackDest) { tot += kv.second; if (kv.first == 0u) low += kv.second; }
        std::cerr << "[vu1] UNPACK dest regions=" << g_unpackDest.size()
                  << " total=" << tot << " intoQw0-63=" << low << " |";
        for (const auto &kv : g_unpackDest) std::cerr << " qw" << (kv.first * 64u) << "=" << kv.second;
        std::cerr << " | wrapped=" << g_unpackWrapped << " maxEndQw=" << g_unpackMaxEndQw;
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
unsigned long long g_vif1Carried = 0ull;
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
    if (ghpcDumpDir() && g_chunkTrace.size() < 65536u)
        g_chunkTrace.push_back(VifTrace{pos, cmd});
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
        if (std::chrono::duration<double>(nowR - tRep).count() >= 5.0 && !ghpcQuietLogs())
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
                      << " carried=" << g_vif1Carried
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
    // VIF state persists across DMA transfers, so a command whose payload runs
    // past the end of this chunk continues in the next one. Its bytes, from the
    // VIFcode on, are kept and parsed ahead of the next chunk. Dropping them made
    // the next chunk parse from mid-payload, which is where the junk OFFSETs that
    // wreck TOP at gameplay came from.
    static const bool s_noResidual = std::getenv("GHPC_VIF1_NO_RESIDUAL") != nullptr;
    std::vector<uint8_t> joined;
#if GHPC_DIAG
    ghpcChunkBegin(g_curChunkSource, static_cast<uint32_t>(m_vif1Residual.size()),
                   m_vif1PendingPath2ImageQwc, vif1_regs.cycle, vif1_regs.base, vif1_regs.ofst,
                   vif1_regs.tops, vif1_regs.stat);
#endif
    if (!m_vif1Residual.empty())
    {
        joined.reserve(m_vif1Residual.size() + sizeBytes);
        joined.assign(m_vif1Residual.begin(), m_vif1Residual.end());
        joined.insert(joined.end(), data, data + sizeBytes);
        m_vif1Residual.clear();
        data = joined.data();
        sizeBytes = static_cast<uint32_t>(joined.size());
    }
#if GHPC_DIAG
    // PsRnd::Reset writes MSCAL 0x6e6 (the vf2 init at VU 0x3730) into a
    // packet and sends it, yet no MSCAL to 0x3730 is ever parsed. Scan every
    // chunk for the raw word, so "never delivered" and "delivered but read as
    // payload" are told apart.
    for (uint32_t a = 0u; a + 4u <= sizeBytes; a += 4u)
    {
        uint32_t w = 0u;
        std::memcpy(&w, data + a, 4);
        if ((w & 0x7FFFFFFFu) == 0x140006E6u || (w & 0x7FFFFFFFu) == 0x150006E6u)
        {
            static int initScanLogs = 0;
            extern unsigned long long g_ghpcVu1Mscals;
            if (initScanLogs++ < 8)
                std::fprintf(stderr, "[vif1/initscan] word 0x%08x at pos=%u of %u src=%u mscals=%llu\n",
                             w, a, sizeBytes, g_curChunkSource, g_ghpcVu1Mscals);
        }
    }
#endif
    auto carry = [&](uint32_t from)
    {
#if GHPC_DIAG
        ++g_vif1Carried;
#endif
        m_vif1Residual.assign(data + from, data + sizeBytes);
    };

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

        const uint32_t cmdStart = pos;
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
                    ghpcDumpChunk("invalid-opcode", data, sizeBytes, pos - 4u);
                }
            }
            if (++total % 50u == 0u && !ghpcQuietLogs())
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
#if GHPC_DIAG
            {
                extern unsigned long long g_ghpcVu1Mscals;
                std::fprintf(stderr, "[vif1/dbuf] OFFSET %u -> %u  (base=%u, mscals=%llu, num=%u, immhi=0x%04x, codePos=%u)\n",
                             (unsigned)(vif1_regs.ofst & 0x3FFu), (unsigned)(imm & 0x3FFu),
                             (unsigned)(vif1_regs.base & 0x3FFu), g_ghpcVu1Mscals,
                             (unsigned)num, (unsigned)(imm & ~0x3FFu),
                             (unsigned)(pos >= 4u ? pos - 4u : 0u));
            }
#endif
#if GHPC_DIAG
            // Are the gameplay OFFSETs (NUM 2 or 3, values 514 and 515) real, or
            // payload read as a VIFcode? The parser's own step decides where the
            // next "command" starts, so how far each advanced proves nothing by
            // itself. The words around the OFFSET are the independent evidence:
            // real ones sit among VIFcodes, misparsed ones among data.
            if (num != 0u)
            {
                static int offsetCtx = 0;
                if (!g_chunkDirty)
                    ghpcDumpChunk("offset-num", data, sizeBytes, pos - 4u);
                if (offsetCtx < 6)
                {
                    ++offsetCtx;
                    extern unsigned long long g_ghpcVu1Mscals;
                    const uint32_t codePos = pos - 4u;
                    std::string s;
                    char b[200];
                    std::snprintf(b, sizeof(b),
                                  "[vif1/offsetctx] #%d OFFSET->%u num=%u codePos=%u size=%u mscals=%llu src=%u\n",
                                  offsetCtx, (unsigned)(imm & 0x3FFu), (unsigned)num, (unsigned)codePos,
                                  (unsigned)sizeBytes, g_ghpcVu1Mscals, (unsigned)g_curChunkSource);
                    s += b;
                    // The last 12 entries of the ring, oldest first; the newest is
                    // this OFFSET, and the one before it now has its consumed set.
                    for (uint32_t i = 20u; i < 32u; ++i)
                    {
                        const VifHist &h = g_hist[(g_histIdx + i) & 31u];
                        std::snprintf(b, sizeof(b),
                                      "[vif1/offsetctx]   pos=%u cmd=0x%08x op=0x%02x num=%u imm=0x%04x consumed=%u\n",
                                      (unsigned)h.pos, (unsigned)h.cmd, (unsigned)h.op, (unsigned)h.num,
                                      (unsigned)h.imm, (unsigned)h.consumed);
                        s += b;
                    }
                    const uint32_t from = (codePos >= 48u) ? codePos - 48u : 0u;
                    const uint32_t to = (codePos + 24u < sizeBytes) ? codePos + 24u : sizeBytes;
                    for (uint32_t a = from; a + 4u <= to; a += 4u)
                    {
                        uint32_t w = 0u;
                        std::memcpy(&w, data + a, 4);
                        std::snprintf(b, sizeof(b), "[vif1/offsetctx]   +%u 0x%08x%s\n",
                                      (unsigned)a, (unsigned)w, a == codePos ? "   <== OFFSET" : "");
                        s += b;
                    }
                    std::fwrite(s.data(), 1, s.size(), stderr);
                }
            }
#endif
            vif1_regs.ofst = imm & 0x3FFu;
#if GHPC_DIAG
            if (const char *fb = std::getenv("GHPC_FORCE_DBUF"))
            {
                vif1_regs.base = (uint32_t)std::strtoul(fb, nullptr, 0) & 0x3FFu;
                const char *c = std::strchr(fb, ':');
                if (c) vif1_regs.ofst = (uint32_t)std::strtoul(c + 1, nullptr, 0) & 0x3FFu;
            }
#endif
            vif1_regs.tops = vif1_regs.base & 0x3FFu;
            vif1_regs.stat &= ~(1u << 7); // clear DBF
            continue;
        }
        else if (opcode == VIF_BASE)
        {
#if GHPC_DIAG
            // Is BASE=932 a real VIFcode the game emitted, or a misparsed
            // stream byte read as one? A genuine BASE is cmd 0x03 with NUM=0.
            // Log the whole word plus the two codes either side of it so a
            // desync is obvious from context rather than inferred.
            {
                static int baseLogs = 0;
                const uint32_t curImm = imm & 0x3FFu;
                // A real BASE has NUM==0 and no immediate bits above bit 9.
                // If either is set we are probably reading a data payload as a
                // VIFcode, so dump the surrounding stream: the word before this
                // one tells us which command mis-advanced pos.
                const bool malformed = (num != 0u) || ((imm & ~0x3FFu) != 0u);
                if (malformed && baseLogs < 6)
                {
                    ++baseLogs;
                    const uint32_t codePos = (pos >= 4u) ? (pos - 4u) : 0u;
                    std::fprintf(stderr,
                        "[vif1/base] MALFORMED imm=0x%04x num=%u codePos=%u size=%u context:\n",
                        (unsigned)(imm & 0xFFFFu), (unsigned)num,
                        (unsigned)codePos, (unsigned)sizeBytes);
                    const uint32_t from = (codePos >= 32u) ? (codePos - 32u) : 0u;
                    const uint32_t to = (codePos + 20u < sizeBytes) ? (codePos + 20u) : sizeBytes;
                    for (uint32_t a = from; a + 4u <= to; a += 4u)
                    {
                        uint32_t w = 0u;
                        std::memcpy(&w, data + a, 4);
                        std::fprintf(stderr, "    +%04u 0x%08x%s\n", (unsigned)a,
                                     (unsigned)w, (a == codePos) ? "   <== read as BASE" : "");
                    }
                }
            }
#endif
            // A real BASE VIFcode carries NUM==0 and uses only immediate bits
            // 9-0. A VIF1 chain-flatten desync walks the parser into payload
            // data, where a word whose top byte happens to be 0x03 reads as
            // BASE and poisons the register (observed: 0x030b73a4 -> BASE=932,
            // NUM=11, immediate bits 12-14 set). BASE has exactly one writer
            // and is sticky, so one bad word corrupts every later MSCAL: TOPS
            // becomes 932/238, the UNPACK at TOPS+1 spans 285 quadwords, and
            // ~194 of them wrap onto VU1 addresses 0-194 where the transform
            // constants live. Ignore words that cannot be a real BASE.
            // NOTE: this is a guard, not the cure. The desync upstream is the
            // actual defect and still needs fixing.
            if (num != 0u || (imm & ~0x3FFu) != 0u)
            {
#if GHPC_DIAG
                // The loading screen alternates a complete picture with one
                // missing the poster. If a dropped chunk is what loses the
                // poster geometry, these rejections should track the bad
                // frames, so count them where the frame census can see it.
                extern unsigned long long g_ghpcBadBaseRejects;
                ++g_ghpcBadBaseRejects;
#endif
                continue;
            }

#if GHPC_DIAG
            {
                extern unsigned long long g_ghpcVu1Mscals;
                std::fprintf(stderr, "[vif1/dbuf] BASE %u -> %u  (ofst=%u, mscals=%llu, codePos=%u, size=%u)\n",
                             (unsigned)(vif1_regs.base & 0x3FFu), (unsigned)(imm & 0x3FFu),
                             (unsigned)(vif1_regs.ofst & 0x3FFu), g_ghpcVu1Mscals,
                             (unsigned)(pos >= 4u ? pos - 4u : 0u), (unsigned)sizeBytes);
            }
#endif
            // BASE only updates the base register. TOPS changes on OFFSET/MSCAL.
            vif1_regs.base = imm & 0x3FFu;
#if GHPC_DIAG
            // Probe only. Force a double-buffer layout whose halves are far
            // enough apart that a 289 quadword batch cannot reach the other
            // half, to test whether the overlap is what corrupts a frame.
            if (const char *fb = std::getenv("GHPC_FORCE_DBUF"))
            {
                vif1_regs.base = (uint32_t)std::strtoul(fb, nullptr, 0) & 0x3FFu;
                const char *c = std::strchr(fb, ':');
                if (c) vif1_regs.ofst = (uint32_t)std::strtoul(c + 1, nullptr, 0) & 0x3FFu;
            }
#endif
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
#if GHPC_DIAG
            if (startPC == 0x3730u)
                std::fprintf(stderr, "[vif1/initscan] PARSED MSCAL 0x3730 at pos=%u of %u\n",
                             (unsigned)(pos - 4u), (unsigned)sizeBytes);
#endif
            // VU1 micro memory is 16 KB, i.e. 2048 instruction pairs, so a real
            // MSCAL always has imm < 2048. Larger values only come from a
            // desynced stream being read as VIFcode; masking them into range
            // (microAddressMask) runs whatever garbage happens to live there.
            static const bool s_allowMaskedMscal = std::getenv("GHPC_ALLOW_MASKED_MSCAL") != nullptr;
            if (startPC >= PS2_VU1_CODE_SIZE && !s_allowMaskedMscal)
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
            { extern unsigned long long g_ghpcDbfToggles; ++g_ghpcDbfToggles; }
#endif

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
                static uint32_t lastBase = 0xFFFFFFFFu, lastOfst = 0xFFFFFFFFu;
                const uint32_t curBase = vif1_regs.base & 0x3FFu;
                const uint32_t curOfst = vif1_regs.ofst & 0x3FFu;
                const bool pairChanged = (curBase != lastBase) || (curOfst != lastOfst);
                if (pairChanged) { lastBase = curBase; lastOfst = curOfst; }
                if (n < 10 || pairChanged)
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
#if GHPC_DIAG
            // Same batch, seen again later, same input? The screen is static so
            // every batch should read identical data every time it runs. Batches
            // are identified by their header quadword at TOP+0, not by their
            // index in the frame, because the batch count per frame varies and
            // index keying compares unrelated objects.
            if (std::getenv("GHPC_BATCHDIFF") && m_vu1Data)
            {
                extern unsigned long long g_ghpcVu1Mscals;
                static const uint32_t kQw = 300u;
                struct Slot { uint32_t hdr[4]; unsigned char data[300 * 16]; unsigned long long top; bool used; };
                static Slot slots[24] = {};
                static int used = 0;
                static int reported = 0;
                if (g_ghpcVu1Mscals > 20000ull && reported < 3)
                {
                    unsigned char cur[300 * 16];
                    for (uint32_t i = 0u; i < kQw; ++i)
                        std::memcpy(cur + i * 16u, m_vu1Data + (((runTop + i) & 0x3FFu) * 16u), 16u);
                    uint32_t hdr[4];
                    std::memcpy(hdr, cur, sizeof(hdr));
                    int slot = -1;
                    for (int i = 0; i < used; ++i)
                        if (std::memcmp(slots[i].hdr, hdr, sizeof(hdr)) == 0) { slot = i; break; }
                    if (slot < 0)
                    {
                        if (used < 24)
                        {
                            slot = used++;
                            std::memcpy(slots[slot].hdr, hdr, sizeof(hdr));
                            std::memcpy(slots[slot].data, cur, sizeof(cur));
                            slots[slot].top = runTop;
                            slots[slot].used = true;
                        }
                    }
                    else if (std::memcmp(slots[slot].data, cur, sizeof(cur)) != 0)
                    {
                        ++reported;
                        unsigned diffs = 0u;
                        std::fprintf(stderr,
                            "[vu1/batchdiff] batch hdr=%08x,%08x,%08x,%08x seen at top=%llu then top=%u, input differs\n",
                            hdr[0], hdr[1], hdr[2], hdr[3], slots[slot].top, (unsigned)runTop);
                        for (uint32_t i = 0u; i < kQw && diffs < 12u; ++i)
                            if (std::memcmp(cur + i * 16u, slots[slot].data + i * 16u, 16u) != 0)
                            {
                                ++diffs;
                                const uint32_t *p = (const uint32_t *)(slots[slot].data + i * 16u);
                                const uint32_t *n = (const uint32_t *)(cur + i * 16u);
                                std::fprintf(stderr, "  +%3u then %08x %08x %08x %08x   now %08x %08x %08x %08x\n",
                                             i, p[0], p[1], p[2], p[3], n[0], n[1], n[2], n[3]);
                            }
                        if (diffs == 0u)
                            std::fprintf(stderr, "  (no quadword differs)\n");
                        std::memcpy(slots[slot].data, cur, sizeof(cur));
                        slots[slot].top = runTop;
                    }
                }
            }

            // Keep the input window of the last 64 MSCALs so the two runs that
            // produced a diverging draw can be compared exactly, by MSCAL
            // index. Batch index within a frame and header contents are both
            // ambiguous, MSCAL index is not.
            if (m_vu1Data)
            {
                extern unsigned char g_ghpcMscalRing[64][1024 * 16];
                extern unsigned long long g_ghpcMscalRingIdx[64];
                extern unsigned long long g_ghpcVu1Mscals;
                const unsigned long long thisMscal = g_ghpcVu1Mscals + 1ull;
                const unsigned slot = (unsigned)(thisMscal % 64ull);
                // Whole data memory, not a TOP relative window: the program also
                // reads the fixed region below BASE where the transforms live.
                std::memcpy(g_ghpcMscalRing[slot], m_vu1Data, 1024u * 16u);
                g_ghpcMscalRingIdx[slot] = thisMscal;
            }

            // Tag every draw with the VU1 double-buffer half that produced it.
            extern unsigned long long g_ghpcVu1Top, g_ghpcVu1Mscals;
            g_ghpcVu1Top = runTop;
            ++g_ghpcVu1Mscals;
            // The runaways read their loop bound from qw[TOP]. Was it unpacked
            // for this MSCAL? Unpacks are stamped with the count before this
            // increment, so age 0 means written since the previous MSCAL.
            {
                extern int g_ghpcLastWriter[1024];
                extern unsigned long long g_ghpcLastWriterMs[1024];
                extern int g_ghpcMscalInKind;
                extern unsigned long long g_ghpcMscalInAge;
                extern unsigned g_ghpcMscalInWords[4];
                g_ghpcMscalInKind = g_ghpcLastWriter[runTop];
                g_ghpcMscalInAge = (g_ghpcVu1Mscals - 1ull) - g_ghpcLastWriterMs[runTop];
                if (m_vu1Data)
                    std::memcpy(g_ghpcMscalInWords, m_vu1Data + runTop * 16u, 16u);
            }
            // One frame's geometry pass reads the buffer at TOP=49 and comes
            // out corrupt; the next reads TOP=192 and is correct. Map which
            // quadwords of each half actually hold data at MSCAL time.
            if (std::getenv("GHPC_TOPMAP") && m_vu1Data && g_ghpcVu1Mscals > 20000ull)
            {
                static int dumped[2] = {0, 0};
                const uint32_t ofst = vif1_regs.ofst & 0x3FFu;
                const uint32_t base = vif1_regs.base & 0x3FFu;
                const int which = (runTop == base) ? 0 : 1;
                if (ofst > 0u && ofst < 512u && !dumped[which])
                {
                    dumped[which] = 1;
                    std::fprintf(stderr, "[vu1/topmap] pc=0x%x top=%u base=%u ofst=%u map:",
                                 (unsigned)startPC, (unsigned)runTop, (unsigned)base, (unsigned)ofst);
                    for (uint32_t i = 0u; i < ofst; ++i)
                    {
                        const uint32_t qw = (runTop + i) & 0x3FFu;
                        uint32_t w[4];
                        std::memcpy(w, m_vu1Data + qw * 16u, sizeof(w));
                        const bool zero = !(w[0] | w[1] | w[2] | w[3]);
                        std::fputc(zero ? '.' : '#', stderr);
                    }
                    std::fputc('\n', stderr);
                    for (uint32_t i = 0u; i < 16u; ++i)
                    {
                        const uint32_t qw = (runTop + i) & 0x3FFu;
                        uint32_t w[4];
                        std::memcpy(w, m_vu1Data + qw * 16u, sizeof(w));
                        std::fprintf(stderr, "[vu1/topmap]   +%u qw=%u %08x %08x %08x %08x\n",
                                     (unsigned)i, (unsigned)qw, w[0], w[1], w[2], w[3]);
                    }
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
#if GHPC_DIAG
            { extern unsigned long long g_ghpcDbfToggles; ++g_ghpcDbfToggles; }
#endif

            if (m_vu1MscntCallback)
                m_vu1MscntCallback(runTop, runItop);
            continue;
        }
        else if (opcode == VIF_STMASK)
        {
            if (pos + 4 > sizeBytes)
            {
                if (!s_noResidual)
                    carry(cmdStart);
                break;
            }
            uint32_t maskValue = 0;
            std::memcpy(&maskValue, data + pos, sizeof(maskValue));
            vif1_regs.mask = maskValue;
            pos += 4;
            continue;
        }
        else if (opcode == VIF_STROW)
        {
            if (pos + 16 > sizeBytes)
            {
                if (!s_noResidual)
                    carry(cmdStart);
                break;
            }
            std::memcpy(vif1_regs.row, data + pos, 16);
            pos += 16;
            continue;
        }
        else if (opcode == VIF_STCOL)
        {
            if (pos + 16 > sizeBytes)
            {
                if (!s_noResidual)
                    carry(cmdStart);
                break;
            }
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
            if (!s_noResidual && pos + mpgBytes > sizeBytes)
            {
                carry(cmdStart);
                break;
            }
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
            if (truncated && !s_noResidual)
            {
                carry(cmdStart);
                break;
            }
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
            // WL=0 means 256, and CL=0 reads nothing in a filling write
            // (PCSX2 vifUnpackSetup). GH2 sends STCYCL 0x0011 and 0x0016, both
            // WL=0. Forcing WL to 1 made those skipping writes that read every
            // vector from the stream, overran the real payload, and parsed the
            // rest of the chunk as VIFcodes.
            static const bool s_cycleLegacy = std::getenv("GHPC_VIF_STCYCL_LEGACY") != nullptr;
            uint32_t cl = vif1_regs.cycle & 0xFFu;
            uint32_t wl = (vif1_regs.cycle >> 8) & 0xFFu;
            if (s_cycleLegacy)
            {
                if (cl == 0u)
                    cl = 1u;
                if (wl == 0u)
                    wl = 1u;
            }
            else if (wl == 0u)
            {
                wl = 256u;
            }

            uint32_t sourceVectorCount = writeVectorCount;
            if (cl < wl)
            {
                const uint32_t fullBlocks = writeVectorCount / wl;
                uint32_t remainder = writeVectorCount % wl;
                if (remainder > cl)
                    remainder = cl;
                sourceVectorCount = fullBlocks * cl + remainder;
            }

#if GHPC_DIAG
            // Proof the WL=0 path runs: how many vectors an UNPACK reads from
            // the stream against how many it writes.
            if ((vif1_regs.cycle & 0xFF00u) == 0u || (vif1_regs.cycle & 0xFFu) == 0u)
            {
                static int cycleLogs = 0;
                if (cycleLogs++ < 8)
                    std::fprintf(stderr,
                                 "[vif1/stcycl] UNPACK cmd=0x%08x cycle=0x%04x cl=%u wl=%u writes=%u reads=%u\n",
                                 (unsigned)cmd, (unsigned)(vif1_regs.cycle & 0xFFFFu), (unsigned)cl,
                                 (unsigned)wl, (unsigned)writeVectorCount, (unsigned)sourceVectorCount);
            }
#endif
            uint32_t totalBytes = sourceVectorCount * bytesPerVector;
            totalBytes = (totalBytes + 3) & ~3u;
            if (!s_noResidual && pos + totalBytes > sizeBytes)
            {
                carry(cmdStart);
                break;
            }

            uint32_t vuAddr = (uint32_t)imm & 0x3FFu;
            if ((imm & 0x8000u) != 0u)
                vuAddr = (vuAddr + (vif1_regs.tops & 0x3FFu)) & 0x3FFu;

            const bool zeroExtend = (imm & 0x4000u) != 0u;

#if GHPC_DIAG
            // PsCam::Select camera/projection upload. The two VIFcodes below are
            // the only writers of VU1 qw696..703. Dump what VIF1 is about to
            // consume, then (after the write loop) what landed in VU1 memory, so
            // "packet already bad" and "unpack corrupts it" can be told apart.
            const bool ghpcCamCode = (cmd == 0x7C0202B8u || cmd == 0x6C0602BAu);
            if (ghpcCamCode)
            {
                static int camLogs = 0;
                static bool guardBandDone = false;
                if (!guardBandDone)
                {
                    guardBandDone = true;
                    uint32_t gb = 0u;
                    std::memcpy(&gb, m_rdram + (0x4f3038u & (PS2_RAM_SIZE - 1u)), 4);
                    float gbf;
                    std::memcpy(&gbf, &gb, 4);
                    std::fprintf(stderr, "[ghpc/cam] sGuardBand@0x4f3038 = 0x%08x (%g)\n",
                                 (unsigned)gb, (double)gbf);
                }
                {
                    ++camLogs;
                    extern char g_ghpcCamPre[2][4096];
                    char *pb = g_ghpcCamPre[(cmd == 0x7C0202B8u) ? 0 : 1];
                    int pn = 0;
                    const uint32_t avail = sizeBytes - pos;
                    const uint32_t dumpBytes = (totalBytes < avail) ? totalBytes : avail;
                    pn += std::snprintf(pb + pn, (size_t)(4096 - pn),
                        "[ghpc/cam] PRE  #%d cmd=0x%08x dest=%u writeVecs=%u srcVecs=%u bpv=%u "
                        "totalBytes=%u avail=%u dumping=%u%s | srcPhys=%s0x%08x chunkBytes=%u pos=0x%x "
                        "cl=%u wl=%u mask=%u maskbits=0x%08x mode=%u tops=%u flg=%u\n",
                        camLogs, (unsigned)cmd, (unsigned)vuAddr, (unsigned)writeVectorCount,
                        (unsigned)sourceVectorCount, (unsigned)bytesPerVector,
                        (unsigned)totalBytes, (unsigned)avail, (unsigned)dumpBytes,
                        (dumpBytes < totalBytes) ? " CAPPED-BY-CHUNK-END" : "",
                        g_ghpcVif1SrcKnown ? "" : "unknown:", (unsigned)g_ghpcVif1SrcPhys,
                        (unsigned)sizeBytes, (unsigned)pos,
                        (unsigned)cl, (unsigned)wl, (unsigned)maskEnable,
                        (unsigned)vif1_regs.mask, (unsigned)(vif1_regs.mode & 3u),
                        (unsigned)(vif1_regs.tops & 0x3FFu), (unsigned)((imm >> 15) & 1u));
                    for (uint32_t q = 0u; q * 16u + 16u <= dumpBytes; ++q)
                    {
                        uint32_t w[4];
                        std::memcpy(w, data + pos + q * 16u, sizeof(w));
                        float f[4];
                        std::memcpy(f, w, sizeof(f));
                        pn += std::snprintf(pb + pn, (size_t)(4096 - pn),
                            "[ghpc/cam] PRE  #%d src[%u] -> qw%u  %08x %08x %08x %08x  (%g, %g, %g, %g)\n",
                            camLogs, (unsigned)q, (unsigned)(vuAddr + q),
                            w[0], w[1], w[2], w[3],
                            (double)f[0], (double)f[1], (double)f[2], (double)f[3]);
                    }
                    (void)pn;
                }
            }
#endif
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
                // A transfer whose end passes quadword 1024 wraps to address 0
                // and overwrites low VU1 memory. Print the inputs for the first
                // few so it is clear whether the destination or the length is
                // the wrong one: addrField is the raw ADDR out of the VIFcode,
                // tops is what got added to it when the FLG bit is set.
                if ((uint64_t)vuAddr + (uint64_t)writeVectorCount > 1024ull)
                {
                    static int wrapLogs = 0;
                    if (wrapLogs < 12)
                    {
                        ++wrapLogs;
                        std::fprintf(stderr,
                            "[vu1/wrap] dest=%u writeVecs=%u end=%u | addrField=%u flg=%u tops=%u "
                            "base=%u ofst=%u | num=%u cl=%u wl=%u bpv=%u opcode=0x%02x\n",
                            (unsigned)vuAddr, (unsigned)writeVectorCount,
                            (unsigned)(vuAddr + writeVectorCount),
                            (unsigned)(imm & 0x3FFu), (unsigned)((imm >> 15) & 1u),
                            (unsigned)(vif1_regs.tops & 0x3FFu),
                            (unsigned)(vif1_regs.base & 0x3FFu), (unsigned)(vif1_regs.ofst & 0x3FFu),
                            (unsigned)num, (unsigned)cl, (unsigned)wl,
                            (unsigned)bytesPerVector, (unsigned)opcode);
                    }
                }
            }
#endif
#if GHPC_DIAG
            // The geometry buffer at TOP=49 comes out holding integer junk in
            // lanes where the TOP=192 buffer holds floats. Log every UNPACK
            // that lands on a watched quadword, in both halves, with the full
            // cycle and mask state, so the two can be compared directly.
            if (const char *w = std::getenv("GHPC_UNPACK_WATCH"))
            {
                extern unsigned long long g_ghpcVu1Mscals;
                // A leading '+' watches an offset inside whichever double-buffer
                // half is current, so both halves can be compared in one run.
                const uint32_t watch = (w[0] == '+')
                    ? ((vif1_regs.tops & 0x3FFu) + (uint32_t)std::strtoul(w + 1, nullptr, 0))
                    : (uint32_t)std::strtoul(w, nullptr, 0);
                const uint32_t span = (cl >= wl && wl) ? ((writeVectorCount / wl) * cl + writeVectorCount % wl)
                                                       : writeVectorCount;
                static int logs = 0;
                if (g_ghpcVu1Mscals > 20000ull && logs < 24 &&
                    watch >= vuAddr && watch < vuAddr + span)
                {
                    ++logs;
                    std::fprintf(stderr,
                        "[vif1/unpackwatch] #%d code=0x%08x qw=%u dest=%u span=%u | vn=%u vl=%u num=%u cl=%u wl=%u "
                        "mask=%u maskbits=0x%08x mode=%u | addrField=%u flg=%u tops=%u base=%u ofst=%u "
                        "bpv=%u payload=%u fits=%d row=%08x,%08x,%08x,%08x\n",
                        logs, [&]{ uint32_t cw = 0u; const uint32_t cp = (pos >= 4u) ? (pos - 4u) : 0u;
                                   if (cp + 4u <= sizeBytes) std::memcpy(&cw, data + cp, 4); return cw; }(),
                        (unsigned)watch, (unsigned)vuAddr, (unsigned)span,
                        (unsigned)vn, (unsigned)vl, (unsigned)writeVectorCount,
                        (unsigned)cl, (unsigned)wl, (unsigned)maskEnable, (unsigned)vif1_regs.mask,
                        (unsigned)(vif1_regs.mode & 3u),
                        (unsigned)(imm & 0x3FFu), (unsigned)((imm >> 15) & 1u),
                        (unsigned)(vif1_regs.tops & 0x3FFu), (unsigned)(vif1_regs.base & 0x3FFu),
                        (unsigned)(vif1_regs.ofst & 0x3FFu),
                        (unsigned)bytesPerVector, (unsigned)totalBytes,
                        (int)(pos + totalBytes <= sizeBytes),
                        vif1_regs.row[0], vif1_regs.row[1], vif1_regs.row[2], vif1_regs.row[3]);
                }
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

#if GHPC_DIAG
                    // Full write history of one VU1 quadword. The position
                    // quadword ends up with a sane w in one double-buffer half
                    // and integer junk in the other, so the question is which
                    // write put it there.
                    if (const char *ww = std::getenv("GHPC_QW_WATCH"))
                    {
                        extern unsigned long long g_ghpcVu1Mscals;
                        const char *fromEnv = std::getenv("GHPC_QW_FROM");
                        const unsigned long long from = fromEnv ? std::strtoull(fromEnv, nullptr, 0) : 20000ull;
                        const uint32_t want = (uint32_t)std::strtoul(ww, nullptr, 0);
                        if (destVec == want && g_ghpcVu1Mscals >= from && g_ghpcVu1Mscals <= from + 40ull)
                        {
                            uint32_t before[4];
                            std::memcpy(before, m_vu1Data + destOff, sizeof(before));
                            std::fprintf(stderr,
                                "[vu1/qwwatch] qw=%u ms=%llu vn=%u vl=%u num=%u cl=%u wl=%u cyc=%u "
                                "mask=%u maskbits=0x%08x mode=%u dec=%d fmt=%d tops=%u | %08x %08x %08x %08x -> %08x %08x %08x %08x\n",
                                (unsigned)destVec, g_ghpcVu1Mscals, (unsigned)vn, (unsigned)vl,
                                (unsigned)writeVectorCount, (unsigned)cl, (unsigned)wl, (unsigned)cyclePos,
                                (unsigned)maskEnable, (unsigned)vif1_regs.mask, (unsigned)(vif1_regs.mode & 3u),
                                (int)decoded, (int)handledFormat, (unsigned)(vif1_regs.tops & 0x3FFu),
                                before[0], before[1], before[2], before[3],
                                lanes[0], lanes[1], lanes[2], lanes[3]);
                        }
                    }
#endif
#if GHPC_DIAG
                    {
                        extern unsigned long long g_ghpcVu1Mscals;
                        extern void ghpcLogQwWrite(unsigned long long, int, unsigned, const unsigned *, const unsigned *);
                        unsigned bef[4];
                        std::memcpy(bef, m_vu1Data + destOff, sizeof(bef));
                        ghpcLogQwWrite(g_ghpcVu1Mscals, 0, (unsigned)destVec, bef, (const unsigned *)lanes);
                    }
#endif
                    std::memcpy(m_vu1Data + destOff, lanes, sizeof(lanes));
                }
            }
#if GHPC_DIAG
            // Widened watch: after ANY unpack, is lane x of qw701/702/703
            // nonzero? If it flips without the two PsCam codes touching it,
            // something other than PsCam::Select is writing there.
            if (m_vu1Data)
            {
                static bool prevBad = false;
                static int flips = 0;
                uint32_t vx[3];
                std::memcpy(&vx[0], m_vu1Data + 701u * 16u, 4);
                std::memcpy(&vx[1], m_vu1Data + 702u * 16u, 4);
                std::memcpy(&vx[2], m_vu1Data + 703u * 16u, 4);
                const bool nowBad = (vx[0] | vx[1] | vx[2]) != 0u;
                if (nowBad != prevBad)
                {
                    prevBad = nowBad;
                    if (flips < 30)
                    {
                        ++flips;
                        const bool destCovers = (vuAddr <= 703u) &&
                                                (vuAddr + writeVectorCount > 701u);
                        std::fprintf(stderr,
                            "[ghpc/camflip] #%d -> %s by cmd=0x%08x dest=%u writeVecs=%u "
                            "destCovers701_703=%d | x701=%08x x702=%08x x703=%08x\n",
                            flips, nowBad ? "NONZERO" : "zero", (unsigned)cmd,
                            (unsigned)vuAddr, (unsigned)writeVectorCount, (int)destCovers,
                            vx[0], vx[1], vx[2]);
                        if (flips == 30)
                            std::fprintf(stderr, "[ghpc/camflip] CAPPED at 30 flips\n");
                    }
                }
            }
            if (ghpcCamCode && m_vu1Data)
            {
                extern char g_ghpcCamPre[2][4096];
                static int camPost = 0;
                static int camGood = 0;
                // Lane x of qw701/702/703 must be zero in a sane projection.
                // Log every event whose result violates that, plus the first
                // few sane ones as a baseline.
                uint32_t lx[3];
                std::memcpy(&lx[0], m_vu1Data + 701u * 16u, 4);
                std::memcpy(&lx[1], m_vu1Data + 702u * 16u, 4);
                std::memcpy(&lx[2], m_vu1Data + 703u * 16u, 4);
                const bool bad = (lx[0] | lx[1] | lx[2]) != 0u;
                bool emit = false;
                if (bad && camPost < 40) { ++camPost; emit = true; }
                else if (!bad && camGood < 4) { ++camGood; emit = true; }
                else if (bad && camPost == 40) { ++camPost;
                    std::fprintf(stderr, "[ghpc/cam] CAPPED: 40 bad events logged, suppressing further\n"); }
                if (emit)
                {
                    std::fprintf(stderr, "[ghpc/cam] --- event verdict=%s ---\n%s%s",
                                 bad ? "BAD" : "ok",
                                 g_ghpcCamPre[0], g_ghpcCamPre[1]);
                    for (uint32_t q = 696u; q <= 703u; ++q)
                    {
                        uint32_t w[4];
                        std::memcpy(w, m_vu1Data + q * 16u, sizeof(w));
                        float f[4];
                        std::memcpy(f, w, sizeof(f));
                        std::fprintf(stderr,
                            "[ghpc/cam] POST #%d cmd=0x%08x qw%u  %08x %08x %08x %08x  (%g, %g, %g, %g)\n",
                            camPost, (unsigned)cmd, (unsigned)q,
                            w[0], w[1], w[2], w[3],
                            (double)f[0], (double)f[1], (double)f[2], (double)f[3]);
                    }
                }
            }
#endif
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
