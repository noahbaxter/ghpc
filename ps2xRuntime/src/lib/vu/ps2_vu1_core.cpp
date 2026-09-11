#include "runtime/ps2_vu1.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "ps2_vu1_detail.h"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>
#include <cstdlib>
#include <cstring>
#if GHPC_DIAG
// GHPC_QUIET silences the periodic diagnostic censuses.
static bool ghpcQuietLogs()
{
    static const bool quiet = std::getenv("GHPC_QUIET") != nullptr;
    return quiet;
}
#endif

#include <filesystem>
#include <limits>
#include <ps2_log.h>

namespace
{
    constexpr uint8_t laneForComponent(uint32_t component)
    {
        return static_cast<uint8_t>(1u << (3u - component));
    }
}

void VU1Interpreter::addVfRead(InstructionUsage &usage, uint8_t reg, uint8_t lanes)
{
    if (lanes == 0u)
        return;
    for (uint32_t index = 0; index < usage.vfReadCount; ++index)
    {
        if (usage.vfRead[index].reg == reg)
        {
            usage.vfRead[index].lanes |= lanes;
            return;
        }
    }
    if (usage.vfReadCount < usage.vfRead.size())
        usage.vfRead[usage.vfReadCount++] = {reg, lanes};
}

void VU1Interpreter::addVfWrite(InstructionUsage &usage, uint8_t reg, uint8_t lanes)
{
    if (reg == 0u || lanes == 0u)
        return;
    if (usage.vfWrite.reg == 0u)
        usage.vfWrite = {reg, lanes};
    else if (usage.vfWrite.reg == reg)
        usage.vfWrite.lanes |= lanes;
}

uint8_t VU1Interpreter::vfReadLanes(const InstructionUsage &usage, uint8_t reg)
{
    for (uint32_t index = 0; index < usage.vfReadCount; ++index)
    {
        if (usage.vfRead[index].reg == reg)
            return usage.vfRead[index].lanes;
    }
    return 0u;
}

VU1Interpreter::VU1Interpreter(Unit unit)
    : m_unit(unit)
{
    reset();
}

void VU1Interpreter::resetScheduler()
{
    m_flagPipeline = {};
    m_fdiv = {};
    m_efu = {};
    m_storePipeline = {};
    m_vfWritePipeline = {};
    m_viWritePipeline = {};
    m_accWritePipeline = {};
    m_xgkick = {};
    m_vfReady = {};
    m_viReady = {};
    m_accReady = {};
    m_vfLatestWrite = {};
    m_viLatestWrite = {};
    m_accLatestWrite = {};
    m_nextWriteSequence = 0;
    m_efuResourceReady = 0;
    m_workingClip = m_state.clip;
    m_viBranchBackupValue = 0;
    m_viBranchBackupReg = 0;
    m_viBranchBackupValid = false;
    m_stopRequested = false;
    m_pendingHaltD = false;
    m_pendingHaltT = false;
}

void VU1Interpreter::reset()
{
    std::memset(&m_state, 0, sizeof(m_state));
    m_state.vf[0][3] = 1.0f;
    m_state.q = 1.0f;
    m_state.r = 0x3F800000u;
    m_cycle = 0;
    resetScheduler();
}

float VU1Interpreter::broadcast(const float *vf, uint8_t bc)
{
    return normalizeOperand(vf[bc & 3u]);
}

float VU1Interpreter::normalizeOperand(float value) const
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t exponent = (bits >> 23) & 0xFFu;
    if (exponent == 0u)
    {
        bits &= 0x80000000u;
    }
    else if (exponent == 0xFFu)
    {
        bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

float VU1Interpreter::normalizeResult(float value, uint32_t &laneFlags) const
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = bits & 0x80000000u;
    const uint32_t magnitude = bits & 0x7FFFFFFFu;
    const uint32_t exponent = (bits >> 23) & 0xFFu;

    laneFlags = sign != 0u ? 0x2u : 0u;
    if (magnitude == 0u)
    {
        laneFlags |= 0x1u;
    }
    else if (exponent == 0u)
    {
        laneFlags |= 0x5u;
        bits = sign;
    }
    else if (exponent == 0xFFu)
    {
        laneFlags |= 0x8u;
        bits = sign | 0x7F7FFFFFu;
    }

    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t VU1Interpreter::microAddressMask() const
{
    return m_unit == Unit::VU1 ? 0x3FFFu : 0x0FFFu;
}

int32_t VU1Interpreter::readBranchVi(uint8_t reg) const
{
    if (reg == 0u)
        return 0;
    if (m_viBranchBackupValid &&
        m_viBranchBackupReg == reg)
    {
        return m_viBranchBackupValue;
    }
    return m_state.vi[reg];
}

void VU1Interpreter::recordViWriteForBranch(uint8_t reg, int32_t oldValue)
{
    if (reg == 0u)
        return;
    m_viBranchBackupValue = oldValue;
    m_viBranchBackupReg = reg;
    m_viBranchBackupValid = true;
}

#if GHPC_DIAG
// Shadow map of "which PC last wrote this vf lane", so a corrupt lane can be
// traced back to the instruction that produced it instead of guessed at.
unsigned int g_ghpcVfWriter[32][4];
// And, for register loads, which VU data-memory address the value came from.
unsigned int g_ghpcVfSrcAddr[32];

// Same idea for VI: which pc last wrote each VI register, and if that write
// was an ILW/ILWR (a load from VU data memory rather than an arithmetic
// result), the address it loaded from. VI0 is hardwired zero and never
// tracked. This is what answers "the back-edge branch tests vi9 -- who wrote
// vi9, and did the value come from memory or from computation".
struct ViWrite { uint32_t pc; uint32_t lo; uint32_t addr; int32_t val; bool isLoad; };
// first: first write this MSCAL. prev: the last write from a pc other than
// last's. In a runaway, last is always the loop's own increment, so entry,
// first and prev are what name the value the loop was entered with.
struct ViProv { ViWrite first, prev, last; int32_t entry; bool seen; };
ViProv g_ghpcViProv[16];
// Set by ILW/ILWR when they compute the loaded value, consumed and cleared by
// the generic post-execLower VI write tracker below. Cleared at the top of
// every execLower call so a write that isn't a load never inherits a stale
// address from a previous instruction.
bool g_ghpcPendingIntLoadValid = false;
uint32_t g_ghpcPendingIntLoadAddr = 0;

// One VI writer snapshot per microprogram (keyed by startPc), overwritten on
// every normal termination so it always holds the most recent healthy run.
struct ViSnapshot { ViProv v[16]; uint64_t atMscal; };
std::map<uint32_t, ViSnapshot> g_ghpcHealthyVi;
#endif

void VU1Interpreter::applyDest(float *dst, const float *result, uint8_t dest)
{
#if GHPC_DIAG
    {
        const ptrdiff_t reg = (float (*)[4])dst - m_state.vf;
        if (reg >= 0 && reg < 32)
            for (int c = 0; c < 4; ++c)
                if (dest & (0x8u >> c))
                    g_ghpcVfWriter[reg][c] = m_state.pc;
    }
#endif
    if (dest & 0x8u)
        dst[0] = result[0];
    if (dest & 0x4u)
        dst[1] = result[1];
    if (dest & 0x2u)
        dst[2] = result[2];
    if (dest & 0x1u)
        dst[3] = result[3];
}

void VU1Interpreter::applyDestAcc(const float *result, uint8_t dest)
{
    applyDest(m_state.acc, result, dest);
}

void VU1Interpreter::normalizeFmacResult(float *result, uint8_t dest,
                                         uint8_t laneFlags[4])
{
    for (uint32_t component = 0; component < 4u; ++component)
    {
        laneFlags[component] = 0u;
        if ((dest & laneForComponent(component)) == 0u)
            continue;

        long double exactResult = 0.0L;
        if (calculateFmacExactResult(component, exactResult))
        {
            laneFlags[component] = normalizeFmacExactResult(result[component], exactResult);
            continue;
        }

        uint32_t flags = 0u;
        result[component] = normalizeResult(result[component], flags);
        laneFlags[component] = static_cast<uint8_t>(flags);
    }
}

bool VU1Interpreter::calculateFmacExactResult(uint32_t component,
                                               long double &result) const
{
    const uint32_t upper = m_currentUpperInstruction;
    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t special = op >= 0x3Cu
                                ? static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu))
                                : 0xFFu;
    const uint8_t fs = FS(upper);
    const uint8_t ft = FT(upper);

    const auto operand = [this](float value)
    {
        return static_cast<long double>(normalizeOperand(value));
    };
    const auto vs = [&](uint32_t lane)
    {
        return operand(m_state.vf[fs][lane]);
    };
    const auto vt = [&](uint32_t lane)
    {
        return operand(m_state.vf[ft][lane]);
    };
    const auto acc = [&](uint32_t lane)
    {
        return operand(m_state.acc[lane]);
    };

    const long double q = operand(m_state.q);
    const long double i = operand(m_state.i);

    if (op < 0x3Cu)
    {
        if (op <= 0x03u)
            result = vs(component) + vt(op & 3u);
        else if (op <= 0x07u)
            result = vs(component) - vt(op & 3u);
        else if (op <= 0x0Bu)
            result = acc(component) + vs(component) * vt(op & 3u);
        else if (op <= 0x0Fu)
            result = acc(component) - vs(component) * vt(op & 3u);
        else if (op >= 0x18u && op <= 0x1Bu)
            result = vs(component) * vt(op & 3u);
        else
        {
            switch (op)
            {
            case 0x1Cu:
                result = vs(component) * q;
                break;
            case 0x1Eu:
                result = vs(component) * i;
                break;
            case 0x20u:
                result = vs(component) + q;
                break;
            case 0x21u:
                result = acc(component) + vs(component) * q;
                break;
            case 0x22u:
                result = vs(component) + i;
                break;
            case 0x23u:
                result = acc(component) + vs(component) * i;
                break;
            case 0x24u:
                result = vs(component) - q;
                break;
            case 0x25u:
                result = acc(component) - vs(component) * q;
                break;
            case 0x26u:
                result = vs(component) - i;
                break;
            case 0x27u:
                result = acc(component) - vs(component) * i;
                break;
            case 0x28u:
                result = vs(component) + vt(component);
                break;
            case 0x29u:
                result = acc(component) + vs(component) * vt(component);
                break;
            case 0x2Au:
                result = vs(component) * vt(component);
                break;
            case 0x2Cu:
                result = vs(component) - vt(component);
                break;
            case 0x2Du:
                result = acc(component) - vs(component) * vt(component);
                break;
            case 0x2Eu:
            {
                static constexpr uint8_t left[4] = {1u, 2u, 0u, 3u};
                static constexpr uint8_t right[4] = {2u, 0u, 1u, 3u};
                result = component == 3u
                             ? 0.0L
                             : acc(component) - vs(left[component]) * vt(right[component]);
                break;
            }
            default:
                return false;
            }
        }
        return true;
    }

    if (special <= 0x03u)
        result = vs(component) + vt(special & 3u);
    else if (special <= 0x07u)
        result = vs(component) - vt(special & 3u);
    else if (special <= 0x0Bu)
        result = acc(component) + vs(component) * vt(special & 3u);
    else if (special <= 0x0Fu)
        result = acc(component) - vs(component) * vt(special & 3u);
    else if (special >= 0x18u && special <= 0x1Bu)
        result = vs(component) * vt(special & 3u);
    else
    {
        switch (special)
        {
        case 0x1Cu:
            result = vs(component) * q;
            break;
        case 0x1Eu:
            result = vs(component) * i;
            break;
        case 0x20u:
            result = vs(component) + q;
            break;
        case 0x21u:
            result = acc(component) + vs(component) * q;
            break;
        case 0x22u:
            result = vs(component) + i;
            break;
        case 0x23u:
            result = acc(component) + vs(component) * i;
            break;
        case 0x24u:
            result = vs(component) - q;
            break;
        case 0x25u:
            result = acc(component) - vs(component) * q;
            break;
        case 0x26u:
            result = vs(component) - i;
            break;
        case 0x27u:
            result = acc(component) - vs(component) * i;
            break;
        case 0x28u:
            result = vs(component) + vt(component);
            break;
        case 0x29u:
            result = acc(component) + vs(component) * vt(component);
            break;
        case 0x2Au:
            result = vs(component) * vt(component);
            break;
        case 0x2Cu:
            result = vs(component) - vt(component);
            break;
        case 0x2Du:
            result = acc(component) - vs(component) * vt(component);
            break;
        case 0x2Eu:
        {
            static constexpr uint8_t left[4] = {1u, 2u, 0u, 3u};
            static constexpr uint8_t right[4] = {2u, 0u, 1u, 3u};
            result = component == 3u
                         ? 0.0L
                         : vs(left[component]) * vt(right[component]);
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

uint8_t VU1Interpreter::normalizeFmacExactResult(float &value,
                                                  long double exactResult) const
{
    const bool negative = std::signbit(exactResult);
    const long double magnitude = std::fabs(exactResult);
    const long double maximum = static_cast<long double>(std::numeric_limits<float>::max());
    const long double minimum = static_cast<long double>(std::numeric_limits<float>::min());
    uint8_t flags = negative ? 0x2u : 0u;

    uint32_t bits = negative ? 0x80000000u : 0u;
    if (magnitude == 0.0L)
    {
        flags |= 0x1u;
        std::memcpy(&value, &bits, sizeof(value));
    }
    else if (magnitude > maximum)
    {
        flags |= 0x8u;
        bits |= 0x7F7FFFFFu;
        std::memcpy(&value, &bits, sizeof(value));
    }
    else if (magnitude < minimum)
    {
        flags |= 0x5u;
        std::memcpy(&value, &bits, sizeof(value));
    }

    return flags;
}

uint32_t VU1Interpreter::calculateFmacProductSticky(uint8_t dest) const
{
    uint32_t extraSticky = 0u;
    const uint32_t upper = m_currentUpperInstruction;
    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t special = op >= 0x3Cu ? static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu)) : 0xFFu;
    const bool productSum =
        (op >= 0x08u && op <= 0x0Fu) ||
        op == 0x21u || op == 0x23u || op == 0x25u || op == 0x27u ||
        op == 0x29u || op == 0x2Du || op == 0x2Eu ||
        (special >= 0x08u && special <= 0x0Fu) ||
        special == 0x21u || special == 0x23u || special == 0x25u ||
        special == 0x27u || special == 0x29u || special == 0x2Du;
    if (!productSum)
        return 0u;

    const uint8_t fs = FS(upper);
    const uint8_t ft = FT(upper);
    for (uint32_t component = 0; component < 4u; ++component)
    {
        if ((dest & laneForComponent(component)) == 0u)
            continue;
        static constexpr uint8_t crossLeft[4] = {1u, 2u, 0u, 3u};
        static constexpr uint8_t crossRight[4] = {2u, 0u, 1u, 3u};
        const uint8_t leftComponent = op == 0x2Eu ? crossLeft[component] : static_cast<uint8_t>(component);
        const float left = normalizeOperand(m_state.vf[fs][leftComponent]);
        float right = 0.0f;
        if ((op >= 0x08u && op <= 0x0Fu) || (special >= 0x08u && special <= 0x0Fu))
        {
            right = normalizeOperand(m_state.vf[ft][(op >= 0x08u && op <= 0x0Fu ? op : special) & 3u]);
        }
        else if (op == 0x21u || op == 0x25u || special == 0x21u || special == 0x25u)
        {
            right = normalizeOperand(m_state.q);
        }
        else if (op == 0x23u || op == 0x27u || special == 0x23u || special == 0x27u)
        {
            right = normalizeOperand(m_state.i);
        }
        else if (op == 0x2Eu)
        {
            right = normalizeOperand(m_state.vf[ft][crossRight[component]]);
        }
        else
        {
            right = normalizeOperand(m_state.vf[ft][component]);
        }

        float product = left * right;
        const long double exactProduct = static_cast<long double>(left) * static_cast<long double>(right);
        const uint8_t productFlags = normalizeFmacExactResult(product, exactProduct);
        // Product-sum instructions report Z/S/U/O from the add/subtract result
        // as current flags, while every product condition accumulates into the
        // corresponding sticky flag.
        extraSticky |= productFlags & 0xFu;
    }
    return extraSticky;
}

void VU1Interpreter::updateFmacFlags(const uint8_t laneFlags[4], uint8_t dest,
                                     uint32_t extraSticky)
{
    if (dest == 0u)
        return;

    uint32_t mac = 0u;
    uint32_t status = 0u;
    for (uint32_t component = 0; component < 4u; ++component)
    {
        const uint8_t lane = laneForComponent(component);
        if ((dest & lane) == 0u)
            continue;

        const uint32_t flags = laneFlags[component];
        if ((flags & 0x1u) != 0u)
            mac |= lane;
        if ((flags & 0x2u) != 0u)
            mac |= static_cast<uint32_t>(lane) << 4;
        if ((flags & 0x4u) != 0u)
            mac |= static_cast<uint32_t>(lane) << 8;
        if ((flags & 0x8u) != 0u)
            mac |= static_cast<uint32_t>(lane) << 12;
        status |= flags;
    }

    FlagPipelineEntry *entry = nullptr;
    for (FlagPipelineEntry &candidate : m_flagPipeline)
    {
        if (!candidate.valid)
        {
            entry = &candidate;
            break;
        }
    }
    if (!entry)
    {
        reportReservedInstruction(true, 0xFFFFFFFFu);
        return;
    }

    *entry = {};
    entry->valid = true;
    entry->issueCycle = m_cycle;
    entry->readyCycle = m_cycle + kFmacLatency;
    entry->mac = mac;
    entry->status = status;
    entry->extraSticky = extraSticky;
    entry->writesMac = true;
    entry->writesStatus = true;
}

void VU1Interpreter::applyFmacDest(float *dst, float *result, uint8_t dest)
{
    uint8_t laneFlags[4]{};
    normalizeFmacResult(result, dest, laneFlags);
    updateFmacFlags(laneFlags, dest, calculateFmacProductSticky(dest));
    applyDest(dst, result, dest);
}

void VU1Interpreter::applyFmacDestAcc(float *result, uint8_t dest)
{
    uint8_t laneFlags[4]{};
    normalizeFmacResult(result, dest, laneFlags);
    updateFmacFlags(laneFlags, dest, calculateFmacProductSticky(dest));
    applyDestAcc(result, dest);
}

void VU1Interpreter::queueFsset(uint16_t immediate)
{
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (entry.valid && entry.issueCycle == m_cycle)
            entry.writesStatus = false;
    }

    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (!entry.valid)
        {
            entry = {};
            entry.valid = true;
            entry.issueCycle = m_cycle;
            entry.readyCycle = m_cycle + kFmacLatency;
            entry.status = static_cast<uint32_t>(immediate) & 0xFC0u;
            entry.writesSticky = true;
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFFEu);
}

void VU1Interpreter::queueClip(uint32_t clip)
{
    m_workingClip = ((m_workingClip << 6) | (clip & 0x3Fu)) & 0xFFFFFFu;
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (!entry.valid)
        {
            entry = {};
            entry.valid = true;
            entry.issueCycle = m_cycle;
            entry.readyCycle = m_cycle + kFmacLatency;
            entry.clip = m_workingClip;
            entry.writesClip = true;
            return;
        }
    }
    reportReservedInstruction(true, 0xFFFFFFFDu);
}

void VU1Interpreter::queueFcset(uint32_t clip)
{
    m_workingClip = clip & 0xFFFFFFu;
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (entry.valid && entry.issueCycle == m_cycle)
            entry.writesClip = false;
    }
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (!entry.valid)
        {
            entry = {};
            entry.valid = true;
            entry.issueCycle = m_cycle;
            entry.readyCycle = m_cycle + kFmacLatency;
            entry.clip = m_workingClip;
            entry.writesClip = true;
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFFAu);
}

void VU1Interpreter::queueQ(float value, uint32_t latency, uint32_t statusDi)
{
    uint32_t ignoredFlags = 0u;
    value = normalizeResult(value, ignoredFlags);
    m_fdiv.valid = true;
    m_fdiv.readyCycle = m_cycle + latency;
    m_fdiv.value = value;
    m_fdiv.statusDi = statusDi & 0x30u;
}

void VU1Interpreter::queueP(float value, uint32_t latency)
{
    uint32_t ignoredFlags = 0u;
    value = normalizeResult(value, ignoredFlags);
    for (ScalarPipelineEntry &entry : m_efu)
    {
        if (!entry.valid)
        {
            entry.valid = true;
            entry.readyCycle = m_cycle + latency;
            entry.value = value;
            // EFU throughput is one cycle shorter than result visibility.
            m_efuResourceReady = m_cycle + (latency > 0u ? latency - 1u : 0u);
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFF9u);
}

void VU1Interpreter::queueStore(uint32_t address, const uint32_t words[4], uint8_t laneMask)
{
    for (PendingStore &store : m_storePipeline)
    {
        if (!store.valid)
        {
#if GHPC_DIAG
            if (m_unit == Unit::VU1)
            {
                // At issue, m_state.pc is the storing instruction itself; by the
                // time the store lands the pc has moved on.
                extern void ghpcNoteTopStoreIssue(uint32_t, uint32_t, const int32_t *, const float *);
                int32_t vi[16];
                for (int r = 0; r < 16; ++r)
                    vi[r] = (int32_t)m_state.vi[r];
                ghpcNoteTopStoreIssue(address, m_state.pc, vi, m_state.vf[2]);
            }
#endif
            store.valid = true;
            store.readyCycle = m_cycle + 1u;
            store.address = address;
            store.laneMask = laneMask;
            std::copy(words, words + 4, store.words.begin());
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFFCu);
}

void VU1Interpreter::queueVfWrite(uint8_t reg, uint8_t laneMask,
                                  const float value[4], uint32_t latency)
{
    if (reg == 0u || laneMask == 0u)
        return;
    for (PendingVfWrite &write : m_vfWritePipeline)
    {
        if (!write.valid)
        {
            write = {};
            write.valid = true;
            write.readyCycle = m_cycle + latency;
            write.sequence = ++m_nextWriteSequence;
            write.reg = reg;
            write.laneMask = laneMask;
            std::copy(value, value + 4, write.value.begin());
            for (uint32_t component = 0; component < 4u; ++component)
            {
                if ((laneMask & laneForComponent(component)) != 0u)
                    m_vfLatestWrite[reg][component] = write.sequence;
            }
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFF7u);
}

#if GHPC_DIAG
uint32_t g_ghpcLastVf7WriterPc = 0;
void ghpcNoteVuStore(uint32_t qw)
{
    // Does the program store over the batch buffer it is about to be fed?
    if (const char *w = std::getenv("GHPC_QW_WATCH"))
    {
        extern unsigned long long g_ghpcVu1Mscals;
        // Aim the window at the mscal that actually diverges. A plain count cap
        // fills at the first mscal past the threshold and never gets there.
        const char *fromEnv = std::getenv("GHPC_QW_FROM");
        const unsigned long long from = fromEnv ? std::strtoull(fromEnv, nullptr, 0) : 20000ull;
        const uint32_t want = (uint32_t)std::strtoul((w[0] == '+') ? w + 1 : w, nullptr, 0);
        if ((qw & 0x3FFu) == want && g_ghpcVu1Mscals >= from && g_ghpcVu1Mscals <= from + 40ull)
            std::fprintf(stderr, "[vu1/qwstore] VU store to qw=%u during mscal %llu\n",
                         (unsigned)want, g_ghpcVu1Mscals);
    }

    static std::map<uint32_t, unsigned long long> dest;
    ++dest[(qw & 0x3FFu) / 64u];
    static unsigned long long n = 0;
    if ((++n % 20000ull) == 0ull && !ghpcQuietLogs())
    {
        unsigned long long low = 0, tot = 0;
        for (const auto &kv : dest) { tot += kv.second; if (kv.first == 0u) low = kv.second; }
        std::fprintf(stderr, "[vu1] VU stores total=%llu intoQw0-63=%llu |", tot, low);
        int k = 0;
        for (const auto &kv : dest) if (k++ < 10) std::fprintf(stderr, " qw%u=%llu", kv.first * 64u, kv.second);
        std::fprintf(stderr, "\n");
    }
}
#endif

void VU1Interpreter::queueViWrite(uint8_t reg, int32_t value, uint32_t latency)
{
    if (reg == 0u)
        return;
#if GHPC_DIAG
    // Which instruction sets the output pointer, and from what? If it is not
    // derived from TOP the packet cannot follow the double buffer.
    if (const char *w = std::getenv("GHPC_VI_WATCH"))
    {
        extern unsigned long long g_ghpcVu1Mscals;
        const unsigned want = (unsigned)std::strtoul(w, nullptr, 0);
        const char *fromEnv = std::getenv("GHPC_STORE_PC");
        const unsigned long long from = fromEnv ? std::strtoull(fromEnv, nullptr, 0) : 20000ull;
        if (reg == want && g_ghpcVu1Mscals >= from && g_ghpcVu1Mscals <= from + 1ull)
            std::fprintf(stderr, "[vu1/viwrite] ms=%llu pc=0x%04x vi%u <- %d (top=%u itop=%u)\n",
                         g_ghpcVu1Mscals, (unsigned)m_state.pc, (unsigned)reg, (int)value,
                         (unsigned)m_state.top, (unsigned)m_state.itop);
    }
#endif
    for (PendingViWrite &write : m_viWritePipeline)
    {
        if (!write.valid)
        {
            write = {};
            write.valid = true;
            write.readyCycle = m_cycle + latency;
            write.sequence = ++m_nextWriteSequence;
            write.reg = reg;
            write.value = value;
            m_viLatestWrite[reg] = write.sequence;
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFF6u);
}

void VU1Interpreter::queueAccWrite(uint8_t laneMask, const float value[4], uint32_t latency)
{
    if (laneMask == 0u)
        return;
    for (PendingAccWrite &write : m_accWritePipeline)
    {
        if (!write.valid)
        {
            write = {};
            write.valid = true;
            write.readyCycle = m_cycle + latency;
            write.sequence = ++m_nextWriteSequence;
            write.laneMask = laneMask;
            std::copy(value, value + 4, write.value.begin());
            for (uint32_t component = 0; component < 4u; ++component)
            {
                if ((laneMask & laneForComponent(component)) != 0u)
                    m_accLatestWrite[component] = write.sequence;
            }
            return;
        }
    }
    reportReservedInstruction(true, 0xFFFFFFF5u);
}

void VU1Interpreter::commitReadyPipelines()
{
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (!entry.valid || entry.readyCycle > m_cycle)
            continue;

        if (entry.writesMac)
            m_state.mac = entry.mac;
        if (entry.writesStatus)
        {
            const uint32_t current = entry.status & 0xFu;
            m_state.status = (m_state.status & 0xFF0u) | current | ((current | entry.extraSticky) << 6);
        }
        if (entry.writesSticky)
        {
            m_state.status = (m_state.status & 0x03Fu) | (entry.status & 0xFC0u);
        }
        if (entry.writesClip)
            m_state.clip = entry.clip;
        entry = {};
    }

    if (m_fdiv.valid && m_fdiv.readyCycle <= m_cycle)
    {
        m_state.q = m_fdiv.value;
        const uint32_t currentDi = m_fdiv.statusDi & 0x30u;
        m_state.status = (m_state.status & 0xFCFu) | currentDi | (currentDi << 6);
        m_fdiv = {};
    }

    for (ScalarPipelineEntry &entry : m_efu)
    {
        if (entry.valid && entry.readyCycle <= m_cycle)
        {
            m_state.p = entry.value;
            entry = {};
        }
    }

    for (PendingStore &store : m_storePipeline)
    {
        if (!store.valid || store.readyCycle > m_cycle)
            continue;
        if (m_activeVuData && store.address + 16u <= m_activeVuDataSize)
        {
            uint32_t oldWords[4]{};
            std::memcpy(oldWords, m_activeVuData + store.address, sizeof(oldWords));
            for (uint32_t component = 0; component < 4u; ++component)
            {
                if ((store.laneMask & laneForComponent(component)) != 0u)
                    oldWords[component] = store.words[component];
            }
#if GHPC_DIAG
            {
                extern unsigned long long g_ghpcVu1Mscals;
                extern void ghpcLogQwWrite(unsigned long long, int, unsigned, const unsigned *, const unsigned *);
                unsigned bef[4];
                std::memcpy(bef, m_activeVuData + store.address, sizeof(bef));
                ghpcLogQwWrite(g_ghpcVu1Mscals, 1 + (int)(m_state.pc << 4), (unsigned)(store.address / 16u), bef, oldWords);
                if (m_unit == Unit::VU1)
                {
                    extern void ghpcNoteTopStore(unsigned, unsigned, const unsigned *, const unsigned *);
                    ghpcNoteTopStore((unsigned)(store.address / 16u), m_state.pc, bef, oldWords);
                }
            }
#endif
            std::memcpy(m_activeVuData + store.address, oldWords, sizeof(oldWords));
        }
        store = {};
    }

    for (PendingVfWrite &write : m_vfWritePipeline)
    {
        if (!write.valid || write.readyCycle > m_cycle)
            continue;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((write.laneMask & laneForComponent(component)) != 0u &&
                m_vfLatestWrite[write.reg][component] == write.sequence)
            {
                m_state.vf[write.reg][component] = write.value[component];
            }
        }
        write = {};
    }

    for (PendingViWrite &write : m_viWritePipeline)
    {
        if (!write.valid || write.readyCycle > m_cycle)
            continue;
        if (m_viLatestWrite[write.reg] == write.sequence)
            m_state.vi[write.reg] = static_cast<int16_t>(write.value);
        write = {};
    }

    for (PendingAccWrite &write : m_accWritePipeline)
    {
        if (!write.valid || write.readyCycle > m_cycle)
            continue;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((write.laneMask & laneForComponent(component)) != 0u &&
                m_accLatestWrite[component] == write.sequence)
            {
                m_state.acc[component] = write.value[component];
            }
        }
        write = {};
    }
}

void VU1Interpreter::progressXgkick()
{
    if (!m_xgkick.active || !m_activeVuData || m_activeVuDataSize == 0u)
        return;

    ++m_xgkick.cycleCredit;
    while (m_xgkick.active && m_xgkick.cycleCredit >= 2u)
    {
        m_xgkick.cycleCredit -= 2u;
        if (m_xgkick.copiedBytes > XgkickPipeline::kBufferSize - 16u)
        {
            reportReservedInstruction(false, 0xFFFFFFFBu);
            m_xgkick.active = false;
            return;
        }

        const uint32_t qwordOffset = m_xgkick.copiedBytes;
        for (uint32_t i = 0; i < 16u; ++i)
        {
            const uint32_t source = (m_xgkick.sourceAddress + m_xgkick.copiedBytes + i) % m_activeVuDataSize;
#if GHPC_DIAG
            extern unsigned char g_ghpcXgkickSnap[16384];
            extern bool g_ghpcXgkickSnapActive;
            if (g_ghpcXgkickSnapActive)
            {
                m_xgkick.packet[m_xgkick.copiedBytes + i] = g_ghpcXgkickSnap[source];
                continue;
            }
#endif
            m_xgkick.packet[m_xgkick.copiedBytes + i] = m_activeVuData[source];
        }
        m_xgkick.copiedBytes += 16u;

        if (m_xgkick.currentTagEnd == 0u)
        {
            uint64_t tagLo = 0;
            std::memcpy(&tagLo, m_xgkick.packet.data() + qwordOffset, sizeof(tagLo));
            const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
            const uint32_t format = static_cast<uint32_t>((tagLo >> 58) & 0x3u);
            uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;

            uint64_t tagBytes = 16u;
            if (format == 0u)
                tagBytes += static_cast<uint64_t>(nloop) * nreg * 16u;
            else if (format == 1u)
                tagBytes += ((static_cast<uint64_t>(nloop) * nreg + 1u) & ~1ull) * 8u;
            else
            {
                // FLG 2 is IMAGE and FLG 3 is DISABLE. DISABLE is a legal mode
                // with the same qwordcount as IMAGE; the data is simply not
                // handed to the GS (gs_frontend has no branch for it, so it
                // falls through and is discarded, which is the correct
                // behaviour). Treating it as a fault used to abort the XGKICK
                // and set m_stopRequested, which killed the whole VU1
                // microprogram partway through and dropped every register
                // write and primitive that would have followed it.
                tagBytes += static_cast<uint64_t>(nloop) * 16u;
            }

            if (tagBytes > XgkickPipeline::kBufferSize - qwordOffset)
            {
                reportReservedInstruction(false, 0xFFFFFFFBu);
                m_xgkick.active = false;
                return;
            }
            m_xgkick.currentTagEnd = qwordOffset + static_cast<uint32_t>(tagBytes);
            m_xgkick.currentTagEop = ((tagLo >> 15) & 1u) != 0u;
            if (m_xgkick.currentTagEop)
                m_xgkick.totalBytes = m_xgkick.currentTagEnd;
        }

        if (m_xgkick.copiedBytes >= m_xgkick.currentTagEnd)
        {
            if (m_xgkick.currentTagEop)
                finishXgkick();
            else
            {
                // The next transferred qword is another GIFtag.
                m_xgkick.currentTagEnd = 0u;
                m_xgkick.currentTagEop = false;
            }
        }
    }
}

void VU1Interpreter::finishXgkick()
{
    if (!m_xgkick.active)
        return;

#if GHPC_DIAG
    {
        // What geometry does VU1 actually emit? Dump the GIFtag and the
        // first few data qwords of the first packets.
        static int kicks = 0;
        static int bad = 0, good = 0;
        {
            // A real packet's REGS field holds small nibble descriptors. A kick
            // aimed at vertex data shows float constants there (0x437f0000 is
            // 255.0f). Split kick source addresses by which kind we got.
            uint64_t tl = 0, th = 0;
            std::memcpy(&tl, m_xgkick.packet.data(), 8);
            std::memcpy(&th, m_xgkick.packet.data() + 8, 8);
            const uint32_t nregField = (uint32_t)((tl >> 60) & 0xFu);
            const bool looksBogus = (nregField == 0u) && (th > 0xFFFFFFFFull);
            // Direct test for the exact signature seen downstream, no heuristic.
            {
                static unsigned long long exact = 0, nreg0 = 0, tot = 0;
                ++tot;
                if (th == 0x437f000000000000ull) ++exact;
                if (nregField == 0u) ++nreg0;
                if ((tot % 4000ull) == 0ull)
                    std::fprintf(stderr,
                        "[vu1] submit tags=%llu nreg0=%llu regs437f=%llu firstTagLo=0x%llx\n",
                        tot, nreg0, exact, (unsigned long long)tl);
            }
            static std::map<uint32_t, std::pair<int,int>> bySrc;
            auto &e = bySrc[m_xgkick.sourceAddress / 16u];
            if (looksBogus) ++e.second; else ++e.first;
            // Does the emitted packet length match what the first tag declares?
            {
                const uint32_t nl = (uint32_t)(tl & 0x7FFFu);
                uint32_t nr = (uint32_t)((tl >> 60) & 0xFu); if (nr == 0u) nr = 16u;
                const uint32_t fl = (uint32_t)((tl >> 58) & 3u);
                const bool eop = ((tl >> 15) & 1u) != 0u;
                uint32_t want = 16u;
                if (fl == 0u) want += nl * nr * 16u;
                else if (fl == 1u) want += ((nl * nr * 8u) + 15u) & ~15u;
                else want += nl * 16u;
                static unsigned long long okLen = 0, longLen = 0, shortLen = 0;
                if (eop) {
                    if (m_xgkick.totalBytes == want) ++okLen;
                    else if (m_xgkick.totalBytes > want) ++longLen;
                    else ++shortLen;
                    static int lr = 0;
                    if (m_xgkick.totalBytes != want && lr < 5) { ++lr;
                        std::fprintf(stderr,
                          "[vu1] LEN MISMATCH firstTag nloop=%u nreg=%u flg=%u eop=1 want=%u got=%u\n",
                          nl, nr, fl, want, (unsigned)m_xgkick.totalBytes); }
                    if (((okLen + longLen + shortLen) % 2000ull) == 0ull)
                        std::fprintf(stderr, "[vu1] pktLen ok=%llu tooLong=%llu tooShort=%llu\n",
                                     okLen, longLen, shortLen);
                }
            }
            static int reps = 0;
            if (((good + bad) % 1500) == 0 && reps < 40)
            {
                ++reps;
                std::fprintf(stderr, "[vu1] XGKICK src good/bogus:");
                for (const auto &kv : bySrc)
                    std::fprintf(stderr, " qw%u=%d/%d", kv.first, kv.second.first, kv.second.second);
                std::fprintf(stderr, "\n");
            }
        }
        // Visible box in 12.4 fixed point for ofx=1792 ofy=1824, 512x448.
        if (m_xgkick.totalBytes >= 64u)
        {
            // Scan EVERY vertex, not just the first: a strip legitimately
            // starts off-screen. Only count a packet bad if no vertex lands in
            // the visible box.
            uint64_t tagLo = 0;
            std::memcpy(&tagLo, m_xgkick.packet.data(), 8);
            uint32_t nloop = (uint32_t)(tagLo & 0x7FFFu);
            uint32_t nreg = (uint32_t)((tagLo >> 60) & 0xFu);
            if (nreg == 0u) nreg = 16u;
            const bool packed = ((tagLo >> 58) & 3u) == 0u;
            uint64_t tagHi = 0;
            std::memcpy(&tagHi, m_xgkick.packet.data() + 8, 8);

            // Everything drawn after splash 1 rasterises black with zero
            // reject counts, so read the colours the packets actually carry
            // instead of inferring from the framebuffer. Also log A+D (reg
            // 0xE) destination registers: TEX0 is programmed that way, and it
            // stops being programmed after splash 1.
            {
                static std::map<uint32_t, unsigned long long> colHist;
                static std::map<uint32_t, unsigned long long> adHist;
                static unsigned long long pkts = 0ull, withRgbaq = 0ull, withAd = 0ull;
                ++pkts;
                if (packed)
                {
                    bool sawRgbaq = false, sawAd = false;
                    for (uint32_t loop = 0u; loop < nloop; ++loop)
                    {
                        for (uint32_t r = 0u; r < nreg; ++r)
                        {
                            const uint32_t reg = (uint32_t)((tagHi >> (r * 4u)) & 0xFu);
                            const uint32_t off = 16u + (loop * nreg + r) * 16u;
                            if (off + 16u > m_xgkick.totalBytes) break;
                            const uint8_t *q = m_xgkick.packet.data() + off;
                            if (reg == 1u)
                            {
                                sawRgbaq = true;
                                const uint32_t rgba = (uint32_t)q[0] | ((uint32_t)q[4] << 8) |
                                                      ((uint32_t)q[8] << 16) | ((uint32_t)q[12] << 24);
                                if (colHist.size() < 512u) ++colHist[rgba];
                            }
                            else if (reg == 0xEu)
                            {
                                sawAd = true;
                                uint64_t ad = 0; std::memcpy(&ad, q + 8, 8);
                                if (adHist.size() < 256u) ++adHist[(uint32_t)(ad & 0xFFu)];
                            }
                        }
                    }
                    if (sawRgbaq) ++withRgbaq;
                    if (sawAd) ++withAd;
                }
                // A fan collapsing to one point means either consecutive
                // vertices share an XYZ (positions not advancing) or they
                // differ but the perspective divide flattened them. Dump raw
                // consecutive vertices from a few packets so the two cases can
                // be told apart instead of guessed at.
                if (packed && nloop >= 4u)
                {
                    static int vtxDumps = 0;
                    if (vtxDumps < 4)
                    {
                        ++vtxDumps;
                        std::fprintf(stderr, "[vu1/xyz] pkt nloop=%u nreg=%u regs=0x%016llx:\n",
                                     (unsigned)nloop, (unsigned)nreg,
                                     (unsigned long long)tagHi);
                        uint32_t shown = 0u;
                        for (uint32_t loop = 0u; loop < nloop && shown < 10u; ++loop)
                        {
                            for (uint32_t r = 0u; r < nreg; ++r)
                            {
                                const uint32_t reg = (uint32_t)((tagHi >> (r * 4u)) & 0xFu);
                                if (reg != 4u && reg != 5u) continue;
                                const uint32_t off = 16u + (loop * nreg + r) * 16u;
                                if (off + 16u > m_xgkick.totalBytes) break;
                                uint64_t lo = 0, hi = 0;
                                std::memcpy(&lo, m_xgkick.packet.data() + off, 8);
                                std::memcpy(&hi, m_xgkick.packet.data() + off + 8, 8);
                                const uint32_t X = (uint32_t)(lo & 0xFFFFu);
                                const uint32_t Y = (uint32_t)((lo >> 32) & 0xFFFFu);
                                const uint32_t Z = (uint32_t)(hi & 0xFFFFFFFFu);
                                std::fprintf(stderr,
                                    "    v%u reg=%u X=%u(%.1f) Y=%u(%.1f) Z=%u\n",
                                    shown, reg, X, X / 16.0, Y, Y / 16.0, Z);
                                ++shown;
                                break;
                            }
                        }
                    }
                }
                // Walk EVERY chained GIFtag in the packet, not just the
                // first. TEX0 is programmed through A+D (reg 0xE) writes that
                // live in their own tag, so a first-tag-only scan reports
                // withAD=0 no matter what is really there.
                {
                    static std::map<uint32_t, unsigned long long> adAll;
                    static unsigned long long tagsWalked = 0ull, adWrites = 0ull;
                    uint32_t off = 0u;
                    for (int guard = 0; guard < 64 && off + 16u <= m_xgkick.totalBytes; ++guard)
                    {
                        uint64_t tl = 0, th = 0;
                        std::memcpy(&tl, m_xgkick.packet.data() + off, 8);
                        std::memcpy(&th, m_xgkick.packet.data() + off + 8, 8);
                        const uint32_t nl = (uint32_t)(tl & 0x7FFFu);
                        const uint32_t fl = (uint32_t)((tl >> 58) & 3u);
                        uint32_t nr = (uint32_t)((tl >> 60) & 0xFu); if (nr == 0u) nr = 16u;
                        const bool eop = ((tl >> 15) & 1u) != 0u;
                        ++tagsWalked;
                        uint32_t dataBytes = 0u;
                        if (fl == 0u) dataBytes = nl * nr * 16u;
                        else if (fl == 1u) dataBytes = ((nl * nr * 8u) + 15u) & ~15u;
                        else dataBytes = nl * 16u;
                        if (fl == 0u)
                        {
                            for (uint32_t loop = 0u; loop < nl; ++loop)
                                for (uint32_t r = 0u; r < nr; ++r)
                                {
                                    if ((uint32_t)((th >> (r * 4u)) & 0xFu) != 0xEu) continue;
                                    const uint32_t o = off + 16u + (loop * nr + r) * 16u;
                                    if (o + 16u > m_xgkick.totalBytes) break;
                                    uint64_t ad = 0;
                                    std::memcpy(&ad, m_xgkick.packet.data() + o + 8, 8);
                                    ++adWrites;
                                    if (adAll.size() < 128u) ++adAll[(uint32_t)(ad & 0xFFu)];
                                }
                        }
                        off += 16u + dataBytes;
                        if (eop) break;
                    }
                    if ((pkts % 20000ull) == 0ull)
                    {
                        std::fprintf(stderr, "[vu1/ad] tagsWalked=%llu adWrites=%llu destRegs:",
                                     tagsWalked, adWrites);
                        for (const auto &kv : adAll)
                            std::fprintf(stderr, " 0x%02x x%llu", (unsigned)kv.first, kv.second);
                        std::fprintf(stderr, "\n");
                    }
                }
                if ((pkts % 20000ull) == 0ull)
                {
                    std::fprintf(stderr, "[vu1/col] pkts=%llu withRGBAQ=%llu withAD=%llu topColors(ABGR):",
                                 pkts, withRgbaq, withAd);
                    for (int pick = 0; pick < 8; ++pick)
                    {
                        uint32_t bestK = 0u; unsigned long long bestV = 0ull; bool any = false;
                        static std::set<uint32_t> taken;
                        if (pick == 0) taken.clear();
                        for (const auto &kv : colHist)
                            if (!taken.count(kv.first) && kv.second > bestV) { bestV = kv.second; bestK = kv.first; any = true; }
                        if (!any) break;
                        taken.insert(bestK);
                        std::fprintf(stderr, " 0x%08x x%llu", (unsigned)bestK, bestV);
                    }
                    std::fprintf(stderr, "\n[vu1/col] AD dest regs:");
                    for (const auto &kv : adHist)
                        std::fprintf(stderr, " 0x%02x x%llu", (unsigned)kv.first, kv.second);
                    std::fprintf(stderr, "\n");
                }
            }
            bool anyVisible = false;
            uint32_t X = 0u, Y = 0u;
            if (packed)
            {
                for (uint32_t loop = 0u; loop < nloop && !anyVisible; ++loop)
                {
                    for (uint32_t r = 0u; r < nreg; ++r)
                    {
                        const uint32_t reg = (uint32_t)((tagHi >> (r * 4u)) & 0xFu);
                        if (reg != 4u && reg != 5u) continue;
                        const uint32_t off = 16u + (loop * nreg + r) * 16u;
                        if (off + 16u > m_xgkick.totalBytes) break;
                        uint64_t v = 0;
                        std::memcpy(&v, m_xgkick.packet.data() + off, 8);
                        X = (uint32_t)(v & 0xFFFFu);
                        Y = (uint32_t)((v >> 32) & 0xFFFFu);
                        if (X >= 28672u && X <= 36864u && Y >= 29184u && Y <= 36352u)
                        { anyVisible = true; break; }
                    }
                }
            }
            const bool offscreen = packed && !anyVisible;
            if (offscreen) ++bad; else ++good;
            {
                static std::map<uint32_t, std::pair<int,int>> byPc;
                auto &e = byPc[m_ghpcStartPc];
                if (offscreen) ++e.second; else ++e.first;
                // Split the same tally by the VIF TOP the run used. VU1 double
                // buffers: TOPS alternates base and base+ofst on every MSCAL,
                // so a run keyed to one half reads a different block of unpacked
                // vertex data than the other. A good/bad split that lands on one
                // TOP means the unpack is not filling that half, which is a
                // completely different bug from a wrong transform.
                static std::map<uint32_t, std::pair<int,int>> byTop;
                auto &t = byTop[m_state.top & 0x3FFu];
                if (offscreen) ++t.second; else ++t.first;
                if (((good + bad) % 2000) == 0)
                {
                    std::fprintf(stderr, "[vu1] byStartPC good/bad:");
                    for (const auto &kv : byPc)
                        std::fprintf(stderr, " pc0x%x=%d/%d", kv.first, kv.second.first, kv.second.second);
                    std::fprintf(stderr, "\n[vu1] byTOP good/bad:");
                    for (const auto &kv : byTop)
                        std::fprintf(stderr, " top%u=%d/%d", kv.first, kv.second.first, kv.second.second);
                    std::fprintf(stderr, "\n");
                }
            }
            // Dump the actual vertices of the worst failing program.
            if (m_ghpcStartPc == 0x3fc8u)
            {
                static int nd = 0;
                if (nd < 4)
                {
                    ++nd;
                    std::fprintf(stderr, "[vu1] pc0x3fc8 bytes=%u nloop=%u nreg=%u flg=%u prim=0x%llx regs=0x%llx\n",
                                 (unsigned)m_xgkick.totalBytes, nloop, nreg,
                                 (unsigned)((tagLo >> 58) & 3u),
                                 (unsigned long long)((tagLo >> 47) & 0x7FFu),
                                 (unsigned long long)tagHi);
                    for (uint32_t q = 1u; q <= 6u && q * 16u + 16u <= m_xgkick.totalBytes; ++q)
                    {
                        uint64_t a = 0, b = 0;
                        std::memcpy(&a, m_xgkick.packet.data() + q * 16u, 8);
                        std::memcpy(&b, m_xgkick.packet.data() + q * 16u + 8, 8);
                        std::fprintf(stderr, "[vu1]   qw%u lo=0x%016llx hi=0x%016llx\n",
                                     q, (unsigned long long)a, (unsigned long long)b);
                    }
                }
            }
            if (offscreen && bad <= 6)
                std::fprintf(stderr, "[vu1] OFFSCREEN X=%u(%.1f) Y=%u(%.1f) good=%d bad=%d\n",
                             X, X / 16.0, Y, Y / 16.0, good, bad);
            if (((good + bad) % 500) == 0)
                std::fprintf(stderr, "[vu1] tally good=%d bad=%d\n", good, bad);
        }
        if (kicks < 0)
        {
            ++kicks;
            const uint8_t *pk = m_xgkick.packet.data();
            uint64_t lo = 0, hi = 0;
            std::memcpy(&lo, pk, 8);
            std::memcpy(&hi, pk + 8, 8);
            const uint32_t nloop = (uint32_t)(lo & 0x7FFF);
            const uint32_t nreg = (uint32_t)((lo >> 60) & 0xF);
            std::fprintf(stderr,
                "[vu1] XGKICK bytes=%u nloop=%u flg=%u nreg=%u regs=0x%llx pre=%u prim=0x%llx\n",
                (unsigned)m_xgkick.totalBytes, nloop,
                (unsigned)((lo >> 58) & 3), nreg,
                (unsigned long long)hi,
                (unsigned)((lo >> 46) & 1),
                (unsigned long long)((lo >> 47) & 0x7FF));
            for (uint32_t q = 1; q <= 4 && q * 16u + 16u <= m_xgkick.totalBytes; ++q)
            {
                uint64_t a = 0, b = 0;
                std::memcpy(&a, pk + q * 16u, 8);
                std::memcpy(&b, pk + q * 16u + 8, 8);
                std::fprintf(stderr, "[vu1]   qw%u lo=0x%016llx hi=0x%016llx\n",
                             q, (unsigned long long)a, (unsigned long long)b);
            }
        }
    }
#endif
    if (m_activeMemory)
        m_activeMemory->submitGifPacket(GifPathId::Path1, m_xgkick.packet.data(), m_xgkick.totalBytes);
    else if (m_activeGs)
        m_activeGs->processGIFPacket(m_xgkick.packet.data(), m_xgkick.totalBytes);
    m_xgkick.active = false;
}

#if GHPC_DIAG
// Probe. PATH1 streams the packet out of live VU memory while the program keeps
// storing into it, so a packet can be read back after the program or the next
// unpack has already overwritten its source. Snapshot the source at kick time
// to test whether that is what corrupts alternate frames.
unsigned char g_ghpcXgkickSnap[16384];
bool g_ghpcXgkickSnapActive = false;
static const bool g_ghpcXgkickSnapEnabled = std::getenv("GHPC_XGKICK_SNAPSHOT") != nullptr;
#endif

void VU1Interpreter::startXgkick(uint32_t qwordAddress)
{
    if (m_unit != Unit::VU1 || !m_activeVuData || m_activeVuDataSize < 16u)
        return;

    const uint32_t sourceAddress = (qwordAddress * 16u) % m_activeVuDataSize;
#if GHPC_DIAG
    if (g_ghpcXgkickSnapEnabled && m_activeVuDataSize <= sizeof(g_ghpcXgkickSnap))
    {
        std::memcpy(g_ghpcXgkickSnap, m_activeVuData, m_activeVuDataSize);
        g_ghpcXgkickSnapActive = true;
    }
#endif
    m_xgkick = {};
    m_xgkick.active = true;
    m_xgkick.sourceAddress = sourceAddress;
    m_xgkick.cycleCredit = 1u; // XGKICK's issue cycle counts toward PATH1.
    m_xgkick.issueCycle = m_cycle;
}

void VU1Interpreter::advanceOneCycle()
{
    ++m_cycle;
    m_state.cycles = m_cycle;
    // LSU commits become visible at the cycle boundary before PATH1 consumes
    // its next qword from VU memory.
    commitReadyPipelines();
    progressXgkick();
}

void VU1Interpreter::advanceTo(uint64_t targetCycle)
{
    while (m_cycle < targetCycle)
        advanceOneCycle();
}

bool VU1Interpreter::pipelinesPending() const
{
    if (m_fdiv.valid || m_xgkick.active)
        return true;
    for (const ScalarPipelineEntry &entry : m_efu)
        if (entry.valid)
            return true;
    for (const FlagPipelineEntry &entry : m_flagPipeline)
        if (entry.valid)
            return true;
    for (const PendingStore &store : m_storePipeline)
        if (store.valid)
            return true;
    for (const PendingVfWrite &write : m_vfWritePipeline)
        if (write.valid)
            return true;
    for (const PendingViWrite &write : m_viWritePipeline)
        if (write.valid)
            return true;
    for (const PendingAccWrite &write : m_accWritePipeline)
        if (write.valid)
            return true;
    return false;
}

void VU1Interpreter::flushPipelines()
{
    while (pipelinesPending())
        advanceOneCycle();
}

uint64_t VU1Interpreter::calculatePairReadyCycle(const DecodedInstructionPair &decoded) const
{
    uint64_t ready = m_cycle;
    const InstructionUsage *usages[2] = {
        &decoded.upperUsage,
        &decoded.lowerUsage};
    for (const InstructionUsage *usage : usages)
    {
        if (!usage)
            continue;
        for (uint32_t index = 0; index < usage->vfReadCount; ++index)
        {
            const VfAccess &access = usage->vfRead[index];
            for (uint32_t component = 0; component < 4u; ++component)
            {
                if ((access.lanes & laneForComponent(component)) != 0u)
                    ready = std::max(ready, m_vfReady[access.reg][component]);
            }
        }
        for (uint32_t reg = 1; reg < m_viReady.size(); ++reg)
        {
            if ((usage->viRead & (1u << reg)) != 0u)
                ready = std::max(ready, m_viReady[reg]);
        }
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((usage->accRead & laneForComponent(component)) != 0u)
                ready = std::max(ready, m_accReady[component]);
        }
    }

    if (decoded.lowerUsage.pipeline == PipelineFdiv && m_fdiv.valid)
        ready = std::max(ready, m_fdiv.readyCycle);
    if (decoded.lowerUsage.pipeline == PipelineEfu)
        ready = std::max(ready, m_efuResourceReady);
    if (decoded.lowerUsage.waitQ && m_fdiv.valid)
        ready = std::max(ready, m_fdiv.readyCycle);
    if (decoded.lowerUsage.waitP)
    {
        for (const ScalarPipelineEntry &entry : m_efu)
            if (entry.valid)
                ready = std::max(ready, entry.readyCycle);
    }
    if (decoded.lowerUsage.pipeline == PipelineXgkick && m_xgkick.active)
        ready = std::max(ready, m_cycle + 1u);
    return ready;
}

void VU1Interpreter::markPairWrites(const DecodedInstructionPair &decoded)
{
    const VfAccess lowerWrite = decoded.lowerUsage.vfWrite;
    if (lowerWrite.reg != 0u &&
        decoded.suppressedLowerVf != lowerWrite.reg)
    {
        const uint32_t latency = decoded.lowerUsage.vfLatency != 0u
                                     ? decoded.lowerUsage.vfLatency
                                     : decoded.lowerUsage.latency;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((lowerWrite.lanes & laneForComponent(component)) != 0u)
                m_vfReady[lowerWrite.reg][component] = m_cycle + latency;
        }
    }

    const VfAccess upperWrite = decoded.upperUsage.vfWrite;
    if (upperWrite.reg != 0u)
    {
        const uint32_t latency = decoded.upperUsage.vfLatency != 0u
                                     ? decoded.upperUsage.vfLatency
                                     : decoded.upperUsage.latency;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((upperWrite.lanes & laneForComponent(component)) != 0u)
                m_vfReady[upperWrite.reg][component] = m_cycle + latency;
        }
    }

    for (uint32_t reg = 1; reg < m_viReady.size(); ++reg)
    {
        if ((decoded.lowerUsage.viWrite & (1u << reg)) != 0u)
            m_viReady[reg] = m_cycle + (decoded.lowerUsage.viLatency != 0u ? decoded.lowerUsage.viLatency : decoded.lowerUsage.latency);
    }
    for (uint32_t component = 0; component < 4u; ++component)
    {
        if ((decoded.upperUsage.accWrite & laneForComponent(component)) != 0u)
            m_accReady[component] = m_cycle + kAccForwardLatency;
    }
}

VU1Interpreter::InstructionUsage VU1Interpreter::decodeUpperUsage(uint32_t upper) const
{
    InstructionUsage usage;
    usage.pipeline = PipelineFmac;
    usage.latency = kFmacLatency;

    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t dest = DEST(upper);
    const uint8_t fs = FS(upper);
    const uint8_t ft = FT(upper);
    const uint8_t fd = FD(upper);

    if (op <= 0x2Fu)
    {
        addVfRead(usage, fs, dest);
        addVfWrite(usage, fd, dest);
        if (op <= 0x1Bu)
            addVfRead(usage, ft, laneForComponent(op & 3u));
        else if (op >= 0x28u)
            addVfRead(usage, ft, op == 0x2Eu ? 0xEu : dest);
        if (op == 0x08u || op == 0x09u || op == 0x0Au || op == 0x0Bu ||
            op == 0x0Cu || op == 0x0Du || op == 0x0Eu || op == 0x0Fu ||
            op == 0x21u || op == 0x23u || op == 0x25u || op == 0x27u ||
            op == 0x29u || op == 0x2Du || op == 0x2Eu)
        {
            usage.accRead = dest;
        }
        return usage;
    }

    if (op >= 0x3Cu)
    {
        const uint8_t special = static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu));
        const bool writesAcc =
            special <= 0x0Fu ||
            (special >= 0x18u && special <= 0x1Cu) ||
            special == 0x1Eu ||
            (special >= 0x20u && special <= 0x2Au) ||
            (special >= 0x2Cu && special <= 0x2Eu);
        if (writesAcc)
        {
            addVfRead(usage, fs, dest);
            if (special <= 0x1Bu)
                addVfRead(usage, ft, laneForComponent(special & 3u));
            else if ((special >= 0x28u && special <= 0x2Eu))
                addVfRead(usage, ft, special == 0x2Eu ? 0xEu : dest);
            usage.accWrite = dest;
            if ((special >= 0x08u && special <= 0x0Fu) ||
                special == 0x21u || special == 0x23u || special == 0x25u ||
                special == 0x27u || special == 0x29u || special == 0x2Du)
            {
                usage.accRead = dest;
            }
        }
        else if (special >= 0x10u && special <= 0x17u)
        {
            addVfRead(usage, fs, dest);
            addVfWrite(usage, ft, dest);
        }
        else if (special == 0x1Du)
        {
            addVfRead(usage, fs, dest);
            addVfWrite(usage, ft, dest);
        }
        else if (special == 0x1Fu)
        {
            addVfRead(usage, fs, 0xEu);
            addVfRead(usage, ft, 0x1u);
            usage.writesClip = true;
        }
        else if (special != 0x2Fu && special != 0x30u)
        {
            usage.reserved = true;
        }
        return usage;
    }

    usage.reserved = true;
    return usage;
}

VU1Interpreter::InstructionUsage VU1Interpreter::decodeLowerUsage(uint32_t lower) const
{
    InstructionUsage usage;
    if (lower == 0u || lower == 0x8000033Cu)
        return usage;

    const uint8_t opHi = static_cast<uint8_t>((lower >> 25) & 0x7Fu);
    const uint8_t vfT = FT(lower);
    const uint8_t vfS = FS(lower);
    const uint8_t viT = VIT(lower);
    const uint8_t viS = VIS(lower);
    const uint8_t viD = VID(lower);
    const uint8_t dest = DEST(lower);
    auto readVi = [&](uint8_t reg)
    {
        if (reg != 0u)
            usage.viRead |= static_cast<uint16_t>(1u << reg);
    };
    auto writeVi = [&](uint8_t reg)
    {
        if (reg != 0u)
            usage.viWrite |= static_cast<uint16_t>(1u << reg);
    };

    switch (opHi)
    {
    case 0x00:
        usage.pipeline = PipelineLsu;
        usage.latency = 4u;
        readVi(viS);
        addVfWrite(usage, vfT, dest);
        return usage;
    case 0x01:
        usage.pipeline = PipelineLsu;
        usage.latency = 1u;
        readVi(viT);
        addVfRead(usage, vfS, dest);
        return usage;
    case 0x04:
        usage.pipeline = PipelineLsu;
        usage.latency = 4u;
        readVi(viS);
        writeVi(viT);
        return usage;
    case 0x05:
        usage.pipeline = PipelineLsu;
        usage.latency = 1u;
        readVi(viS);
        readVi(viT);
        return usage;
    case 0x08:
    case 0x09:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.delaysNextBranchRead = true;
        readVi(viS);
        writeVi(viT);
        return usage;
    case 0x10:
    case 0x12:
    case 0x13:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.readsClip = true;
        writeVi(1u);
        return usage;
    case 0x11:
        usage.pipeline = PipelineFmac;
        usage.latency = kFmacLatency;
        usage.writesClip = true;
        return usage;
    case 0x14:
    case 0x16:
    case 0x17:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        writeVi(viT);
        return usage;
    case 0x15:
        usage.pipeline = PipelineFmac;
        usage.latency = kFmacLatency;
        return usage;
    case 0x18:
    case 0x1A:
    case 0x1B:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        readVi(viS);
        writeVi(viT);
        return usage;
    case 0x1C:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.readsClip = true;
        writeVi(viT);
        return usage;
    case 0x20:
        usage.pipeline = PipelineBranch;
        return usage;
    case 0x21:
        usage.pipeline = PipelineBranch;
        usage.latency = 1u;
        writeVi(viT);
        return usage;
    case 0x24:
        usage.pipeline = PipelineBranch;
        readVi(viS);
        return usage;
    case 0x25:
        usage.pipeline = PipelineBranch;
        usage.latency = 1u;
        readVi(viS);
        writeVi(viT);
        return usage;
    case 0x28:
    case 0x29:
        usage.pipeline = PipelineBranch;
        readVi(viS);
        readVi(viT);
        return usage;
    case 0x2C:
    case 0x2D:
    case 0x2E:
    case 0x2F:
        usage.pipeline = PipelineBranch;
        readVi(viS);
        return usage;
    case 0x40:
        break;
    default:
        usage.reserved = true;
        return usage;
    }

    const uint8_t direct = static_cast<uint8_t>(lower & 0x3Fu);
    if (direct == 0x30u || direct == 0x31u || direct == 0x34u || direct == 0x35u)
    {
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.delaysNextBranchRead = true;
        readVi(viS);
        readVi(viT);
        writeVi(viD);
        return usage;
    }
    if (direct == 0x32u)
    {
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.delaysNextBranchRead = true;
        readVi(viS);
        writeVi(viT);
        return usage;
    }
    if (direct < 0x3Cu)
    {
        usage.reserved = true;
        return usage;
    }

    const uint8_t special = static_cast<uint8_t>((lower & 3u) | ((lower >> 4) & 0x7Cu));
    switch (special)
    {
    case 0x30:
    case 0x31:
        usage.pipeline = PipelineFmac;
        usage.latency = 4u;
        addVfRead(usage, vfS, special == 0x31u ? 0xFu : dest);
        addVfWrite(usage, vfT, dest);
        break;
    case 0x34:
    case 0x36:
        usage.pipeline = PipelineLsu;
        usage.latency = 4u;
        usage.viLatency = 1u;
        usage.delaysNextBranchRead = true;
        readVi(viS);
        writeVi(viS);
        addVfWrite(usage, vfT, dest);
        break;
    case 0x35:
    case 0x37:
        usage.pipeline = PipelineLsu;
        usage.latency = 1u;
        usage.delaysNextBranchRead = true;
        readVi(viT);
        writeVi(viT);
        addVfRead(usage, vfS, dest);
        break;
    case 0x38:
        usage.pipeline = PipelineFdiv;
        usage.latency = 7u;
        addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
        addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
        break;
    case 0x39:
        usage.pipeline = PipelineFdiv;
        usage.latency = 7u;
        addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
        break;
    case 0x3A:
        usage.pipeline = PipelineFdiv;
        usage.latency = 13u;
        addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
        addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
        break;
    case 0x3B:
        usage.pipeline = PipelineFdiv;
        usage.waitQ = true;
        break;
    case 0x3C:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.delaysNextBranchRead = true;
        addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
        writeVi(viT);
        break;
    case 0x3D:
        usage.pipeline = PipelineFmac;
        usage.latency = 4u;
        readVi(viS);
        addVfWrite(usage, vfT, dest);
        break;
    case 0x3E:
        usage.pipeline = PipelineLsu;
        usage.latency = 4u;
        readVi(viS);
        writeVi(viT);
        break;
    case 0x3F:
        usage.pipeline = PipelineLsu;
        usage.latency = 1u;
        readVi(viS);
        readVi(viT);
        break;
    case 0x40:
    case 0x41:
        usage.pipeline = PipelineFmac;
        usage.latency = 4u;
        addVfWrite(usage, vfT, dest);
        break;
    case 0x42:
    case 0x43:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
        break;
    case 0x64:
        if (m_unit == Unit::VU0)
        {
            usage.reserved = true;
            break;
        }
        usage.pipeline = PipelineFmac;
        usage.latency = 4u;
        addVfWrite(usage, vfT, dest);
        break;
    case 0x68:
    case 0x69:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        writeVi(viT);
        break;
    case 0x6C:
        if (m_unit == Unit::VU0)
        {
            usage.reserved = true;
            break;
        }
        usage.pipeline = PipelineXgkick;
        usage.latency = 2u;
        readVi(viS);
        break;
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7A:
    case 0x7C:
    case 0x7D:
        if (m_unit == Unit::VU0)
        {
            usage.reserved = true;
            break;
        }
        usage.pipeline = PipelineEfu;
        switch (special)
        {
        case 0x70:
            usage.latency = 11u;
            break;
        case 0x71:
        case 0x72:
        case 0x77:
            usage.latency = 18u;
            break;
        case 0x73:
            usage.latency = 24u;
            break;
        case 0x74:
        case 0x75:
        case 0x7C:
            usage.latency = 54u;
            break;
        case 0x76:
        case 0x78:
        case 0x7A:
            usage.latency = 12u;
            break;
        case 0x79:
            usage.latency = 29u;
            break;
        case 0x7D:
            usage.latency = 44u;
            break;
        default:
            break;
        }
        if (special >= 0x70u && special <= 0x73u)
            addVfRead(usage, vfS, 0xEu);
        else if (special == 0x74u)
            addVfRead(usage, vfS, 0xCu);
        else if (special == 0x75u)
            addVfRead(usage, vfS, 0xAu);
        else if (special == 0x76u)
            addVfRead(usage, vfS, 0xFu);
        else
            addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
        break;
    case 0x7B:
        if (m_unit == Unit::VU0)
        {
            usage.reserved = true;
            break;
        }
        usage.pipeline = PipelineEfu;
        usage.waitP = true;
        break;
    default:
        usage.reserved = true;
        break;
    }
    return usage;
}

VU1Interpreter::DecodedInstructionPair VU1Interpreter::decodeInstructionPair(const uint8_t *vuCode, uint32_t pc) const
{
    DecodedInstructionPair decoded;
    std::memcpy(&decoded.lower, vuCode + pc, sizeof(decoded.lower));
    std::memcpy(&decoded.upper, vuCode + pc + sizeof(decoded.lower), sizeof(decoded.upper));
    decoded.iBit = (decoded.upper & 0x80000000u) != 0u;
    decoded.eBit = (decoded.upper & 0x40000000u) != 0u;
    decoded.mBit = (decoded.upper & 0x20000000u) != 0u;
    decoded.dBit = (decoded.upper & 0x10000000u) != 0u;
    decoded.tBit = (decoded.upper & 0x08000000u) != 0u;
    decoded.upperUsage = decodeUpperUsage(decoded.upper);
    if (!decoded.iBit)
        decoded.lowerUsage = decodeLowerUsage(decoded.lower);

    const uint8_t upperWriteReg = decoded.upperUsage.vfWrite.reg;
    if (upperWriteReg != 0u && (vfReadLanes(decoded.lowerUsage, upperWriteReg) != 0u || decoded.lowerUsage.vfWrite.reg == upperWriteReg))
    {
        decoded.upperVfShadowReg = upperWriteReg;
        if (decoded.lowerUsage.vfWrite.reg == upperWriteReg)
            decoded.suppressedLowerVf = upperWriteReg;
    }
    return decoded;
}

void VU1Interpreter::rebuildDecodedCodeCache(const uint8_t *vuCode, uint32_t codeSize,
                                             const PS2Memory *memory, uint64_t generation)
{
    const uint32_t pairCount = std::min<uint32_t>(codeSize / 8u, kMaxDecodedPairs);
    for (uint32_t i = 0; i < pairCount; ++i)
        m_decodedCodeCache[i] = decodeInstructionPair(vuCode, i * 8u);

    m_cachedVuCode = vuCode;
    m_cachedMemory = memory;
    m_cachedCodeSize = codeSize;
    m_cachedCodeGeneration = generation;
    m_decodedCodeCacheValid = true;
}

VU1Interpreter::DecodedInstructionPair VU1Interpreter::getDecodedInstructionPairForPc(
    const uint8_t *vuCode, uint32_t codeSize, PS2Memory *memory, uint32_t pc)
{
    if ((pc & 7u) != 0u)
        return decodeInstructionPair(vuCode, pc);

    const bool trackedVu1Code = memory != nullptr &&
                                ((m_unit == Unit::VU1 && vuCode == memory->getVU1Code()) ||
                                 (m_unit == Unit::VU0 && vuCode == memory->getVU0Code()));
    if (!trackedVu1Code)
        return decodeInstructionPair(vuCode, pc);

    const uint64_t generation = m_unit == Unit::VU1 ? memory->getVU1CodeGeneration() : memory->getVU0CodeGeneration();
    if (!m_decodedCodeCacheValid ||
        m_cachedVuCode != vuCode ||
        m_cachedMemory != memory ||
        m_cachedCodeSize != codeSize ||
        m_cachedCodeGeneration != generation)
    {
        rebuildDecodedCodeCache(vuCode, codeSize, memory, generation);
    }
    const uint32_t pairIndex = pc / 8u;
    if (pairIndex >= kMaxDecodedPairs)
        return decodeInstructionPair(vuCode, pc);
    return m_decodedCodeCache[pairIndex];
}

void VU1Interpreter::reportReservedInstruction(bool upper, uint32_t instruction)
{
    RUNTIME_ERROR(
        "[VU" << (m_unit == Unit::VU1 ? "1" : "0")
              << " reserved " << (upper ? "upper" : "lower")
              << "] cycle=" << m_cycle
              << " pc=0x" << std::hex << m_state.pc
              << " instruction=0x" << instruction
              << std::dec << '\n');
    m_stopRequested = true;
}

void VU1Interpreter::execute(uint8_t *vuCode, uint32_t codeSize,
                             uint8_t *vuData, uint32_t dataSize,
                             GS &gs, PS2Memory *memory,
                             uint32_t startPC, uint32_t top, uint32_t itop,
                             uint32_t maxCycles)
{
    resetScheduler();
    m_state.pc = startPC & microAddressMask();
#if GHPC_DIAG
    m_ghpcStartPc = startPC & microAddressMask();
    { extern int g_ghpcVu1Entry; g_ghpcVu1Entry = 0; }
    // One-shot raw microcode dump. The matrix load and compose at 0x0d30..0x0d70
    // were read off a partial trace; dumping the words lets the dest fields be
    // decoded directly instead of inferred. GHPC_VU1_DUMP=<startByte>:<count>.
    {
        static bool dumped = false;
        const char *spec = std::getenv("GHPC_VU1_DUMP");
        if (spec && !dumped)
        {
            dumped = true;
            char *end = nullptr;
            uint32_t from = (uint32_t)std::strtoul(spec, &end, 0);
            uint32_t count = (end && *end == ':') ? (uint32_t)std::strtoul(end + 1, nullptr, 0) : 32u;
            from &= ~7u;
            for (uint32_t i = 0u; i < count; ++i)
            {
                const uint32_t off = from + i * 8u;
                if (off + 8u > codeSize) break;
                uint32_t lo, hi;
                std::memcpy(&lo, vuCode + off, 4);
                std::memcpy(&hi, vuCode + off + 4, 4);
                std::fprintf(stderr, "[vu1/dump] 0x%04x lo=0x%08x hi=0x%08x\n", off, lo, hi);
            }
        }
    }
#endif
    m_state.ebit = false;
    m_state.haltAfterDelaySlot = false;
    m_state.stoppedByD = false;
    m_state.stoppedByT = false;
    m_state.top = top;
    m_state.itop = itop;
    m_state.branchPending = false;
    m_state.branchTarget = 0;
    m_state.branchDelay = 0;
    m_state.vf[0][0] = 0.0f;
    m_state.vf[0][1] = 0.0f;
    m_state.vf[0][2] = 0.0f;
    m_state.vf[0][3] = 1.0f;
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
}

void VU1Interpreter::resume(uint8_t *vuCode, uint32_t codeSize,
                            uint8_t *vuData, uint32_t dataSize,
                            GS &gs, PS2Memory *memory,
                            uint32_t top, uint32_t itop, uint32_t maxCycles)
{
    m_state.top = top;
    m_state.itop = itop;
    m_state.stoppedByD = false;
    m_state.stoppedByT = false;
#if GHPC_DIAG
    { extern int g_ghpcVu1Entry; g_ghpcVu1Entry = 1; }
#endif
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
}

#if GHPC_DIAG
// Polling watchpoint on the transform matrix at VU1 data 0x2a40..0x2a7f.
// Comparing the bytes rather than instrumenting each writer catches every
// path that can touch it, including ones not enumerated (EE stores, DMA).
// `where` says whether the change happened inside VU execution (with the pc)
// or between runs, which is enough to identify the writer.
void ghpcMtxWatch(const uint8_t *data, uint32_t size, const char *where, unsigned pc)
{
    if (!data || size < 0x2c00u)
        return;
    static uint32_t last[16];
    static bool primed = false;
    static int logs = 0;
    uint32_t cur[16];
    std::memcpy(cur, data + 0x2bc0u, sizeof(cur));
    if (!primed)
    {
        primed = true;
        std::memcpy(last, cur, sizeof(last));
        return;
    }
    if (std::memcmp(cur, last, sizeof(cur)) == 0)
        return;
    float f[16], pf[16];
    std::memcpy(f, cur, sizeof(f));
    std::memcpy(pf, last, sizeof(pf));
    // Only the corrupt shape is interesting: in a sane matrix lane x of rows 1
    // and 2 is ~0 and the row 3 translation is small. Trigger on that so the
    // transition into corruption is captured rather than early boot traffic.
    // Projection matrix at qwords 700..703. Lane x of rows 1 and 2 must be 0;
    // it comes out as 400.17, so trigger the moment that appears.
    const bool corrupt = (f[4] > 10.0f || f[4] < -10.0f) ||
                         (f[8] > 10.0f || f[8] < -10.0f);
    if (corrupt && logs < 24)
    {
        ++logs;
        // Stamped with the MSCAL counter so this can be ordered against the
        // runaway onset. Without a shared clock the two events can only be
        // compared by log line, and the census prints every 2000 MSCALs, which
        // is far too coarse to say which came first.
        extern unsigned long long g_ghpcVu1Mscals;
        std::fprintf(stderr, "[projw] %-10s pc=0x%04x mscal=%llu\n            NEW:",
                     where, pc, g_ghpcVu1Mscals);
        for (int r = 0; r < 4; ++r)
            std::fprintf(stderr, " (%g,%g,%g,%g)",
                         (double)f[r * 4 + 0], (double)f[r * 4 + 1],
                         (double)f[r * 4 + 2], (double)f[r * 4 + 3]);
        std::fprintf(stderr, "\n           OLD:");
        for (int r = 0; r < 4; ++r)
            std::fprintf(stderr, " (%g,%g,%g,%g)",
                         (double)pf[r * 4 + 0], (double)pf[r * 4 + 1],
                         (double)pf[r * 4 + 2], (double)pf[r * 4 + 3]);
        std::fprintf(stderr, "\n");
    }
    std::memcpy(last, cur, sizeof(cur));
}

// Cut-off and ended runs of 0x30b0 start with the same header at qw[TOP], so
// whatever separates them happens after entry. Per run: the entry value, the
// first VU store to land on qw[TOP], the first integer load of it, and the
// first such load that returns something other than the entry value.
int g_ghpcVu1Entry = 0;  // 0 = MSCAL (execute), 1 = MSCNT (resume)
struct TwStore { bool seen; uint32_t pc; uint32_t before[4], after[4]; };
struct TwRead { bool seen; uint32_t pc; int comp; uint32_t val; bool afterStore; };
static uint32_t g_twTop = 0u;
static uint32_t g_twEntry[4] = {};
static TwStore g_twStore = {};
static TwRead g_twFirstRead = {}, g_twDiffRead = {};
// The first store issued at qw[TOP] this run: its own pc, every VI at that
// moment, and who wrote each VI so far this run.
struct TwIssue { bool seen; uint32_t pc; int32_t vi[16]; ViProv prov[16]; };
static TwIssue g_twIssue = {};
// The output pointer is MTIR vi4, vf2.x. vf2 at entry, its first and last
// writer this run, and its value when the qw[TOP] store issued.
struct TwVf { bool seen; uint32_t pc, lo, hi; bool upper; uint8_t lanes; uint32_t v[4]; };
static uint32_t g_twVf2Entry[4] = {};
static uint32_t g_twVf2AtIssue[4] = {};
static TwVf g_twVf2First = {}, g_twVf2Last = {};
static void ghpcTopWatchBegin(const uint8_t *vuData, uint32_t top, const float *vf2)
{
    g_twTop = top & 0x3FFu;
    std::memcpy(g_twEntry, vuData + g_twTop * 16u, sizeof(g_twEntry));
    g_twStore = {};
    g_twFirstRead = {};
    g_twDiffRead = {};
    g_twIssue = {};
    std::memcpy(g_twVf2Entry, vf2, sizeof(g_twVf2Entry));
    std::memset(g_twVf2AtIssue, 0, sizeof(g_twVf2AtIssue));
    g_twVf2First = {};
    g_twVf2Last = {};
}
static void ghpcNoteVf2Write(uint32_t pc, uint32_t lo, uint32_t hi, bool upper,
                             uint8_t lanes, const float *v)
{
    TwVf w{true, pc, lo, hi, upper, lanes, {}};
    std::memcpy(w.v, v, sizeof(w.v));
    if (!g_twVf2First.seen)
        g_twVf2First = w;
    g_twVf2Last = w;
}
void ghpcNoteTopStoreIssue(uint32_t address, uint32_t pc, const int32_t *vi, const float *vf2)
{
    if (address / 16u != g_twTop || g_twIssue.seen)
        return;
    std::memcpy(g_twVf2AtIssue, vf2, sizeof(g_twVf2AtIssue));
    g_twIssue.seen = true;
    g_twIssue.pc = pc;
    std::memcpy(g_twIssue.vi, vi, sizeof(g_twIssue.vi));
    std::memcpy(g_twIssue.prov, g_ghpcViProv, sizeof(g_twIssue.prov));
}
void ghpcNoteTopStore(unsigned qw, unsigned pc, const unsigned *before, const unsigned *after)
{
    if (qw != g_twTop || g_twStore.seen)
        return;
    g_twStore.seen = true;
    g_twStore.pc = pc;
    std::memcpy(g_twStore.before, before, sizeof(g_twStore.before));
    std::memcpy(g_twStore.after, after, sizeof(g_twStore.after));
}
void ghpcNoteTopRead(uint32_t addr, int comp, uint32_t v, uint32_t pc)
{
    if (addr / 16u != g_twTop)
        return;
    const TwRead r{true, pc, comp, v, g_twStore.seen};
    if (!g_twFirstRead.seen)
        g_twFirstRead = r;
    if (!g_twDiffRead.seen && v != g_twEntry[comp & 3])
        g_twDiffRead = r;
}
#endif

void VU1Interpreter::run(uint8_t *vuCode, uint32_t codeSize,
                         uint8_t *vuData, uint32_t dataSize,
                         GS &gs, PS2Memory *memory, uint32_t maxCycles)
{
    m_activeVuData = vuData;
    m_activeVuDataSize = dataSize;
    m_activeGs = &gs;
    m_activeMemory = memory;
#if GHPC_DIAG
    if (m_unit == Unit::VU1)
    {
        extern void ghpcMtxWatch(const uint8_t *, uint32_t, const char *, unsigned);
        ghpcMtxWatch(vuData, dataSize, "outside-vu", 0u);
        for (int r = 0; r < 16; ++r)
        {
            g_ghpcViProv[r].seen = false;
            g_ghpcViProv[r].entry = m_state.vi[r];
        }
        ghpcTopWatchBegin(vuData, m_state.top, m_state.vf[2]);
    }
#endif

    const int previousRoundingMode = std::fegetround();
    const bool useVuRounding = std::fesetround(FE_TOWARDZERO) == 0;
    const uint64_t budgetEnd = m_cycle + maxCycles;
    bool programEnded = false;
#if GHPC_DIAG
    // GHPCVUEND: why did this microprogram stop?
    //
    // One MSCAL is one run() call, and the budget is 65536 cycles handed down
    // from the callback in ps2_runtime.cpp. A program that ends on its E-bit
    // did what it was written to do. A program that ends because the budget ran
    // out did NOT: it was cut off mid-flight, so whatever it had half written
    // into VU1 memory is what gets kicked to the GS, and the next MSCAL starts
    // over from the top and does the same work again.
    //
    // That single distinction separates the two explanations for a 4 second
    // frame. If these programs terminate normally, the cost is honest work and
    // the answer is a native backend at the Rnd layer. If they are being cut
    // off, the frame rate and the corrupted picture are one bug with one fix,
    // and no amount of backend rewriting would have helped.
    //
    // Counted rather than sampled, because a runaway that happens on 1% of
    // MSCALs and a runaway that happens on all of them need completely
    // different responses and a sampled probe cannot tell them apart.
    uint64_t ghpcInstrs = 0ull;
    const bool ghpcCensus = (m_unit == Unit::VU1);

    // GHPCVULOOP: make the runaway name itself.
    //
    // The census says a third of these programs burn the whole budget with
    // offend=0, which means the pc never leaves code memory: it is a loop that
    // does not exit, not a jump into garbage. Counting visits per pc turns that
    // into an address. The tight cycle shows up as a handful of slots with tens
    // of thousands of hits each while the rest of the program has one or two,
    // and the branch sitting at the bottom of that cycle is the instruction
    // that was supposed to end it.
    //
    // Micro memory is 16KB of 8 byte instruction pairs, so 2048 slots. The
    // buffer is a member rather than a local so it is not reallocated per
    // MSCAL, and the whole thing is off unless GHPC_VU1_LOOP is set, because
    // this counter sits in the hottest loop in the interpreter.
    static const int ghpcLoopDumps = []() {
        const char *e = std::getenv("GHPC_VU1_LOOP");
        return e ? std::atoi(e) : 0;
    }();
    static int ghpcLoopDumped = 0;
    // The first N runaways are always menu ones. GHPC_VU1_LOOP_FROM holds the
    // probe off until the MSCAL counter passes a gameplay point, so the dumps
    // describe the runaways that actually cost the frame.
    static const unsigned long long ghpcLoopFrom = []() -> unsigned long long {
        const char *e = std::getenv("GHPC_VU1_LOOP_FROM");
        return e ? std::strtoull(e, nullptr, 0) : 0ull;
    }();
    extern unsigned long long g_ghpcVu1Mscals;
    const bool ghpcLoopProbe = (ghpcLoopDumps > 0 && m_unit == Unit::VU1 &&
                                ghpcLoopDumped < ghpcLoopDumps &&
                                g_ghpcVu1Mscals >= ghpcLoopFrom);
    if (ghpcLoopProbe)
    {
        m_ghpcPcHits.assign(PS2_VU1_CODE_SIZE / 8u, 0u);
    }
    // Back edges taken during this MSCAL, keyed (fromPc << 16) | toPc. Read off
    // the interpreter's own branch resolution rather than decoded by hand: the
    // tree has no VU disassembler, and a hand rolled opcode reading is exactly
    // the kind of guess that produces a confident wrong answer. Whatever the
    // interpreter actually jumped on is what the loop actually is.
    std::map<uint32_t, uint32_t> ghpcBackEdges;
#endif
    while (m_cycle < budgetEnd && !m_stopRequested)
    {
#if GHPC_DIAG
        ++ghpcInstrs;
        if (ghpcLoopProbe)
        {
            const uint32_t slot = m_state.pc >> 3;
            if (slot < m_ghpcPcHits.size())
                ++m_ghpcPcHits[slot];
        }
#endif
        commitReadyPipelines();
#if GHPC_DIAG
        if (m_unit == Unit::VU1)
        {
            extern void ghpcMtxWatch(const uint8_t *, uint32_t, const char *, unsigned);
            ghpcMtxWatch(vuData, dataSize, "in-vu", m_state.pc);
        }
#endif
        if (m_state.pc + 8u > codeSize)
            break;

        const DecodedInstructionPair decoded = getDecodedInstructionPairForPc(vuCode, codeSize, memory, m_state.pc);
        if (decoded.upperUsage.reserved || decoded.lowerUsage.reserved)
        {
            reportReservedInstruction(decoded.upperUsage.reserved, decoded.upperUsage.reserved ? decoded.upper : decoded.lower);
            break;
        }

        uint64_t readyCycle = calculatePairReadyCycle(decoded);
        while (readyCycle > m_cycle)
        {
            if (readyCycle >= budgetEnd)
            {
                advanceTo(budgetEnd);
                break;
            }
            advanceTo(readyCycle);
            readyCycle = calculatePairReadyCycle(decoded);
        }
        if (m_cycle >= budgetEnd)
            break;

        uint8_t writtenVi = 0u;
        int32_t oldVi = 0;
        for (uint32_t reg = 1; reg < 16u; ++reg)
        {
            if ((decoded.lowerUsage.viWrite & (1u << reg)) != 0u)
            {
                writtenVi = static_cast<uint8_t>(reg);
                oldVi = m_state.vi[reg];
                break;
            }
        }
#if GHPC_DIAG
        {
            // Execution census for the two addresses that decide the bad kick.
            static unsigned long long at1160 = 0, at11a0 = 0, vi2zero = 0, vi2nz = 0;
            static int32_t lastVi2 = 0;
            if (m_state.pc == 0x1160u) {
                ++at1160;
                if (m_state.vi[2] == 0) ++vi2zero; else { ++vi2nz; lastVi2 = m_state.vi[2]; }
            }
            if (m_state.pc == 0x11a0u) ++at11a0;
            {
                // One-shot static scan: every address whose decode writes VF2,
                // versus the addresses actually executed.
                static bool scanned = false;
                static std::set<uint32_t> staticWriters, executedWriters;
                if (!scanned && vuCode)
                {
                    scanned = true;
                    for (uint32_t a = 0; a + 8u <= codeSize; a += 8u)
                    {
                        const DecodedInstructionPair d =
                            getDecodedInstructionPairForPc(vuCode, codeSize, memory, a);
                        if (d.lowerUsage.vfWrite.reg == 2u || d.upperUsage.vfWrite.reg == 2u)
                            staticWriters.insert(a);
                    }
                    std::fprintf(stderr, "[vu1] static VF2 writers: %zu addresses\n", staticWriters.size());
                }
                if (decoded.lowerUsage.vfWrite.reg == 2u || decoded.upperUsage.vfWrite.reg == 2u)
                    executedWriters.insert(m_state.pc);
                static unsigned long long r = 0;
                if ((++r % 2000000ull) == 0ull)
                {
                    std::fprintf(stderr, "[vu1] VF2 writers static=%zu executed=%zu | NEVER executed:",
                                 staticWriters.size(), executedWriters.size());
                    int shown = 0;
                    for (uint32_t a : staticWriters)
                        if (!executedWriters.count(a) && shown < 24) { std::fprintf(stderr, " 0x%x", a); ++shown; }
                    std::fprintf(stderr, "\n");
                }
            }
            {
                // Independent of decoder metadata: is VF2 ever non-zero at all,
                // and if so where? Sampled on every instruction.
                static unsigned long long nz = 0, z = 0;
                static uint32_t firstNzPc = 0xFFFFFFFFu, lastNzBits = 0;
                uint32_t bb[4];
                for (int c = 0; c < 4; ++c) std::memcpy(&bb[c], &m_state.vf[2][c], 4);
                static unsigned long long lane[4] = {0,0,0,0};
                for (int c = 0; c < 4; ++c) if (bb[c] != 0u) ++lane[c];
                if (bb[0] != 0u) { ++nz; lastNzBits = bb[0]; if (firstNzPc == 0xFFFFFFFFu) firstNzPc = m_state.pc; }
                else ++z;
                static unsigned long long q = 0;
                if ((++q % 400000ull) == 0ull)
                    std::fprintf(stderr,
                        "[vu1] vf2 lanes nonzero x=%llu y=%llu z=%llu w=%llu | lastBits x=0x%x y=0x%x z=0x%x w=0x%x\n",
                        lane[0], lane[1], lane[2], lane[3], bb[0], bb[1], bb[2], bb[3]);
            }
            if (m_state.pc == 0x1188u)
            {
                // Let the decoder describe 0x1188 and report what it produces.
                static unsigned long long t = 0;
                if ((++t % 3000ull) == 0ull)
                {
                    uint32_t before = 0; std::memcpy(&before, &m_state.vf[2][0], 4);
                    std::fprintf(stderr,
                        "[vu1] 0x1188 lower=0x%08x | writesVf=%u readCount=%u src0=vf%u src1=vf%u viReadMask=0x%x | vf2.x before=0x%x src0.x=0x%x\n",
                        decoded.lower,
                        (unsigned)decoded.lowerUsage.vfWrite.reg,
                        (unsigned)decoded.lowerUsage.vfReadCount,
                        (unsigned)decoded.lowerUsage.vfRead[0].reg,
                        (unsigned)decoded.lowerUsage.vfRead[1].reg,
                        (unsigned)decoded.lowerUsage.viRead,
                        before,
                        [&]{ uint32_t b=0; std::memcpy(&b, &m_state.vf[decoded.lowerUsage.vfRead[0].reg][0], 4); return b; }());
                }
            }
            if (m_state.pc == 0x1160u)
            {
                // MTIR vi11, vf7.x -- what is VF7.x, and who wrote VF7 last?
                extern uint32_t g_ghpcLastVf7WriterPc;
                uint32_t bits = 0; std::memcpy(&bits, &m_state.vf[2][0], 4);
                static std::map<uint32_t, unsigned long long> byWriter;
                static unsigned long long zb = 0, nzb = 0; static uint32_t lastBits = 0;
                if (bits == 0u) ++zb; else { ++nzb; lastBits = bits; }
                ++byWriter[g_ghpcLastVf7WriterPc];
                static unsigned long long t = 0;
                if ((++t % 4000ull) == 0ull) {
                    std::fprintf(stderr, "[vu1] vf2.x at 0x1160: rawZero=%llu rawNonzero=%llu lastRaw=0x%x | lastVF2 writer pc:",
                                 zb, nzb, lastBits);
                    for (const auto &kv : byWriter)
                        std::fprintf(stderr, " 0x%x=%llu", kv.first, kv.second);
                    std::fprintf(stderr, "\n");
                }
            }
            static unsigned long long c = 0;
            if ((++c % 400000ull) == 0ull)
                std::fprintf(stderr,
                    "[vu1] exec 0x1160=%llu 0x11a0=%llu | vi2 at 0x1160: zero=%llu nonzero=%llu last=%d\n",
                    at1160, at11a0, vi2zero, vi2nz, (int)lastVi2);
        }
        {
            // Which VI registers does the decoder ever claim are written?
            static unsigned long long wr[16] = {0};
            static unsigned long long n = 0;
            if (writtenVi) ++wr[writtenVi & 15u];
            if (writtenVi == 11u)
            {
                // What values does VI11 actually receive?
                static unsigned long long zero = 0, nonzero = 0; static int32_t lastNz = 0;
                const int32_t nv = m_state.vi[11];
                if (nv == 0) {
                    ++zero;
                    // Which instruction and PC produced the zero?
                    static std::map<uint64_t, unsigned long long> byOp;
                    const uint32_t lo = decoded.lower;
                    const uint32_t op = (lo >> 25) & 0x7Fu;
                    byOp[((uint64_t)m_state.pc << 32) | lo] += 1;
                    static unsigned long long z = 0;
                    if ((++z % 3000ull) == 0ull) {
                        std::fprintf(stderr, "[vu1] vi11:=0 sites (pc/lowerOp)xN:");
                        for (const auto &kv : byOp)
                            std::fprintf(stderr, " pc0x%x/op0x%x=%llu",
                                         (unsigned)(kv.first >> 32),
                                         (unsigned)((uint32_t)kv.first),
                                         kv.second);
                        std::fprintf(stderr, " [op6=0x%x]\n", (unsigned)op);
                        // Dump the microcode across the loop body once, marking
                        // any lower instruction whose `it` field is VI11.
                        static bool dumped = false;
                        if (!dumped && vuCode)
                        {
                            dumped = true;
                            for (uint32_t a = 0x1140u; a <= 0x11b8u && a + 8u <= codeSize; a += 8u)
                            {
                                uint32_t lw = 0, uw = 0;
                                std::memcpy(&lw, vuCode + a, 4);
                                std::memcpy(&uw, vuCode + a + 4, 4);
                                const uint32_t itf = (lw >> 16) & 0x1Fu;
                                const uint32_t isf = (lw >> 11) & 0x1Fu;
                                const uint32_t opf = (lw >> 25) & 0x7Fu;
                                std::fprintf(stderr,
                                    "[vu1] code 0x%x lower=0x%08x (op=0x%02x it=%u is=%u)%s upper=0x%08x\n",
                                    a, lw, opf, itf, isf, (itf == 11u ? "  <== writes VI11" : ""), uw);
                            }
                        }
                    }
                } else { ++nonzero; lastNz = nv; }
                static unsigned long long q = 0;
                if ((++q % 100000ull) == 0ull)
                    std::fprintf(stderr, "[vu1] vi11 writes: zero=%llu nonzero=%llu lastNonzero=%d\n",
                                 zero, nonzero, (int)lastNz);
            }
            if ((++n % 400000ull) == 0ull)
            {
                std::fprintf(stderr, "[vu1] VI writes:");
                for (uint32_t k = 1; k < 16u; ++k)
                    if (wr[k]) std::fprintf(stderr, " vi%u=%llu", k, wr[k]);
                std::fprintf(stderr, "\n");
            }
        }
#endif

        const VfAccess upperWrite = decoded.upperUsage.vfWrite;
        const VfAccess lowerWrite = decoded.lowerUsage.vfWrite;
        const bool hasUpperWrite = upperWrite.reg != 0u;
        const bool hasLowerWrite = lowerWrite.reg != 0u && decoded.suppressedLowerVf != lowerWrite.reg;
        const bool hasDistinctLowerWrite = hasLowerWrite && (!hasUpperWrite || lowerWrite.reg != upperWrite.reg);
        float oldUpperVf[4]{};
        float newUpperVf[4]{};
        float oldLowerVf[4]{};
        float newLowerVf[4]{};
        float oldAcc[4]{};
        float newAcc[4]{};
#if GHPC_DIAG
        if ((hasUpperWrite && upperWrite.reg == 2u) || (hasLowerWrite && lowerWrite.reg == 2u))
        { extern uint32_t g_ghpcLastVf7WriterPc; g_ghpcLastVf7WriterPc = m_state.pc; }
#endif
        if (hasUpperWrite)
            std::memcpy(oldUpperVf, m_state.vf[upperWrite.reg], sizeof(oldUpperVf));
        if (hasDistinctLowerWrite)
            std::memcpy(oldLowerVf, m_state.vf[lowerWrite.reg], sizeof(oldLowerVf));
        if (decoded.upperUsage.accWrite != 0u)
            std::memcpy(oldAcc, m_state.acc, sizeof(oldAcc));

        if (decoded.iBit)
        {
            execUpper(decoded.upper);
            float immediate = 0.0f;
            std::memcpy(&immediate, &decoded.lower, sizeof(immediate));
            m_state.i = normalizeOperand(immediate);
        }
        else if (decoded.upperVfShadowReg != 0u)
        {
            float oldVf[4]{};
            float upperVf[4]{};
            std::memcpy(oldVf,
                        m_state.vf[decoded.upperVfShadowReg],
                        sizeof(oldVf));
            execUpper(decoded.upper);
            std::memcpy(upperVf,
                        m_state.vf[decoded.upperVfShadowReg],
                        sizeof(upperVf));
            std::memcpy(m_state.vf[decoded.upperVfShadowReg],
                        oldVf,
                        sizeof(oldVf));
            execLower(decoded.lower, vuData, dataSize, gs, memory, decoded.upper);
            std::memcpy(m_state.vf[decoded.upperVfShadowReg],
                        upperVf,
                        sizeof(upperVf));
        }
        else
        {
            execUpper(decoded.upper);
            execLower(decoded.lower, vuData, dataSize, gs, memory, decoded.upper);
        }

        m_viBranchBackupValid = false;

        if (hasUpperWrite)
        {
            std::memcpy(newUpperVf, m_state.vf[upperWrite.reg], sizeof(newUpperVf));
            std::memcpy(m_state.vf[upperWrite.reg], oldUpperVf, sizeof(oldUpperVf));
            const uint32_t latency =
                decoded.upperUsage.vfLatency != 0u
                    ? decoded.upperUsage.vfLatency
                    : decoded.upperUsage.latency;
            queueVfWrite(upperWrite.reg, upperWrite.lanes, newUpperVf, latency);
#if GHPC_DIAG
            if (m_unit == Unit::VU1 && upperWrite.reg == 2u)
                ghpcNoteVf2Write(m_state.pc, decoded.lower, decoded.upper, true,
                                 (uint8_t)upperWrite.lanes, newUpperVf);
#endif
        }
        if (hasDistinctLowerWrite)
        {
            std::memcpy(newLowerVf, m_state.vf[lowerWrite.reg], sizeof(newLowerVf));
            std::memcpy(m_state.vf[lowerWrite.reg], oldLowerVf, sizeof(oldLowerVf));
            const uint32_t latency = decoded.lowerUsage.vfLatency != 0u
                                         ? decoded.lowerUsage.vfLatency
                                         : decoded.lowerUsage.latency;
            queueVfWrite(lowerWrite.reg, lowerWrite.lanes, newLowerVf, latency);
#if GHPC_DIAG
            if (m_unit == Unit::VU1 && lowerWrite.reg == 2u)
                ghpcNoteVf2Write(m_state.pc, decoded.lower, decoded.upper, false,
                                 (uint8_t)lowerWrite.lanes, newLowerVf);
#endif
        }
        if (decoded.upperUsage.accWrite != 0u)
        {
            std::memcpy(newAcc, m_state.acc, sizeof(newAcc));
            std::memcpy(m_state.acc, oldAcc, sizeof(oldAcc));
            // ACC is forwarded to the next upper instruction. Its arithmetic
            // flags still use the normal four-cycle FMAC timeline.
            queueAccWrite(decoded.upperUsage.accWrite, newAcc,
                          kAccForwardLatency);
        }
        if (writtenVi != 0u)
        {
            const int32_t newVi = m_state.vi[writtenVi];
            m_state.vi[writtenVi] = oldVi;
#if GHPC_DIAG
            if (m_unit == Unit::VU1)
            {
                const ViWrite w{m_state.pc, decoded.lower,
                                g_ghpcPendingIntLoadValid ? g_ghpcPendingIntLoadAddr : 0u,
                                newVi, g_ghpcPendingIntLoadValid};
                ViProv &p = g_ghpcViProv[writtenVi];
                if (!p.seen) { p.first = w; p.seen = true; }
                if (w.pc != p.last.pc) p.prev = p.last;
                p.last = w;
            }
            g_ghpcPendingIntLoadValid = false;
#endif
            const uint32_t latency =
                decoded.lowerUsage.viLatency != 0u
                    ? decoded.lowerUsage.viLatency
                    : decoded.lowerUsage.latency;
            queueViWrite(writtenVi, newVi, latency);
        }

        markPairWrites(decoded);
        if (writtenVi != 0u && decoded.lowerUsage.delaysNextBranchRead)
            recordViWriteForBranch(writtenVi, oldVi);

        m_state.vf[0][0] = 0.0f;
        m_state.vf[0][1] = 0.0f;
        m_state.vf[0][2] = 0.0f;
        m_state.vf[0][3] = 1.0f;
        m_state.vi[0] = 0;

        uint32_t nextPc = m_state.pc + 8u;
        if (nextPc >= codeSize)
            nextPc = 0u;
        m_state.pc = nextPc;

        if (m_state.branchPending)
        {
            if (m_state.branchDelay == 0u)
            {
                const uint32_t ghpcFrom = m_state.pc;
                m_state.pc = m_state.branchTarget & microAddressMask();
                m_state.branchPending = false;
#if GHPC_DIAG
                if (ghpcLoopProbe && m_state.pc < ghpcFrom)
                    ++ghpcBackEdges[((ghpcFrom & 0xFFFFu) << 16) | (m_state.pc & 0xFFFFu)];
#endif
            }
            else
            {
                --m_state.branchDelay;
            }
        }

        const bool dHalt = decoded.dBit && m_state.dBitEnabled;
        const bool tHalt = decoded.tBit && m_state.tBitEnabled;
        const bool haltBit = dHalt || tHalt;
        const bool haltBranch = haltBit && decoded.lowerUsage.pipeline == PipelineBranch;

        if (m_state.haltAfterDelaySlot)
        {
            m_state.stoppedByD = m_pendingHaltD;
            m_state.stoppedByT = m_pendingHaltT;
            programEnded = true;
        }
        else if (m_state.ebit)
            programEnded = true;
        else if (haltBit && !haltBranch)
        {
            m_state.stoppedByD = dHalt;
            m_state.stoppedByT = tHalt;
            programEnded = true;
        }
        else if (decoded.eBit)
            m_state.ebit = true;
        else if (haltBranch)
        {
            m_state.haltAfterDelaySlot = true;
            m_pendingHaltD = dHalt;
            m_pendingHaltT = tHalt;
        }

        advanceOneCycle();
        if (programEnded)
            break;
    }

    if (programEnded)
    {
        flushPipelines();
        m_state.ebit = false;
        m_state.haltAfterDelaySlot = false;
        m_pendingHaltD = false;
        m_pendingHaltT = false;
    }
#if GHPC_DIAG
    if (ghpcCensus)
    {
        // Four outcomes, kept apart. "Ran off the end of code memory" is not
        // the same failure as "ran out of budget", and folding them together
        // would hide which one is happening.
        enum { kEnded = 0, kBudget = 1, kOffEnd = 2, kStopped = 3 };
        int why = kEnded;
        if (programEnded)          why = kEnded;
        else if (m_stopRequested)  why = kStopped;
        else if (m_state.pc + 8u > codeSize) why = kOffEnd;
        else                       why = kBudget;

        // VI provenance for a healthy run of this same microprogram, kept as
        // the most recent normal termination seen so far. When this startPc
        // later runs away, this is what "the same loop bound register, but
        // from a run that actually ended" looked like, so the two can be
        // diffed instead of theorized about.
        if (why == kEnded)
        {
            ViSnapshot &snap = g_ghpcHealthyVi[m_ghpcStartPc];
            std::memcpy(snap.v, g_ghpcViProv, sizeof(snap.v));
            extern unsigned long long g_ghpcVu1Mscals;
            snap.atMscal = g_ghpcVu1Mscals;
        }

        // Instructions are counted PER TERMINATION REASON, not just per
        // program. The first version of this census recorded only a total, and
        // the cost of the cut-off runs was then derived as
        // (budget runs x maxCycles), which is wrong: maxCycles is a CYCLE
        // budget and a stalled run burns cycles without retiring instructions.
        // That produced a confidently wrong "89% of VU1 time" figure. Splitting
        // the tally is the only way the attribution is a measurement rather
        // than an assumption.
        struct Bucket { uint64_t n[4]; uint64_t instrsBy[4]; uint64_t instrs; uint64_t maxInstrs; };
        static std::map<uint32_t, Bucket> byPc;
        static uint64_t total = 0ull;
        Bucket &b = byPc[m_ghpcStartPc];
        ++b.n[why];
        b.instrsBy[why] += ghpcInstrs;
        b.instrs += ghpcInstrs;
        if (ghpcInstrs > b.maxInstrs) b.maxInstrs = ghpcInstrs;
        ++total;

        // Split by TOP, the double-buffer half the program reads, and by
        // whether qw[TOP] was unpacked for this MSCAL (fresh) or is left over.
        // Kind 0 is an unpack, anything else a VU store.
        extern int g_ghpcMscalInKind;
        extern unsigned long long g_ghpcMscalInAge;
        extern unsigned g_ghpcMscalInWords[4];
        struct TopBucket { uint64_t n[2][2]; };  // [fresh][cut off]
        static std::map<uint64_t, TopBucket> byPcTop;
        const bool inFresh = (g_ghpcMscalInKind == 0 && g_ghpcMscalInAge == 0ull);
        if (why == kEnded || why == kBudget)
        {
            const uint64_t key = ((uint64_t)m_ghpcStartPc << 16) | (m_state.top & 0x3FFu);
            ++byPcTop[key].n[inFresh ? 1 : 0][why == kBudget ? 1 : 0];
            static std::map<uint64_t, int> shown;
            static uint64_t cut = 0ull;
            const bool isCut = (why == kBudget);
            if (isCut) ++cut;
            const bool show = isCut ? (cut <= 40ull || (cut % 500ull) == 0ull)
                                    : (shown[key]++ < 2);
            if (show)
            {
                extern unsigned long long g_ghpcVu1Mscals;
                std::fprintf(stderr,
                             "[vu1/topin] %s mscal=%llu startPc=0x%x top=%u in=%s age=%llu"
                             " qw[top]=%08x %08x %08x %08x\n",
                             isCut ? "CUT " : "ebit", g_ghpcVu1Mscals, m_ghpcStartPc,
                             (unsigned)(m_state.top & 0x3FFu),
                             g_ghpcMscalInKind == 0 ? "unpack" : "vustore",
                             g_ghpcMscalInAge,
                             g_ghpcMscalInWords[0], g_ghpcMscalInWords[1],
                             g_ghpcMscalInWords[2], g_ghpcMscalInWords[3]);
            }
        }

        // [cut][entered by MSCNT][VU store hit qw[TOP]][a header load got a
        // value other than the entry one], per startPc.
        struct TwBucket { uint64_t n[2][2][2][2]; };
        static std::map<uint32_t, TwBucket> twByPc;
        // [cut][TOP==0][vf2 written this run][vf2.x low 16 bits 0 at entry].
        // Counted over every run, so carried-in and written-in-run are told
        // apart on the population, not on ten samples.
        struct VfBucket { uint64_t n[2][2][2][2]; };
        static std::map<uint32_t, VfBucket> vfByPc;
        if (why == kEnded || why == kBudget)
        {
            const int cutI = (why == kBudget) ? 1 : 0;
            ++twByPc[m_ghpcStartPc].n[cutI][g_ghpcVu1Entry ? 1 : 0]
                                     [g_twStore.seen ? 1 : 0][g_twDiffRead.seen ? 1 : 0];
            ++vfByPc[m_ghpcStartPc].n[cutI][g_twTop == 0u ? 1 : 0]
                                     [g_twVf2First.seen ? 1 : 0][(g_twVf2Entry[0] & 0xFFFFu) == 0u ? 1 : 0];
            static std::map<uint32_t, int> twShown[2];
            if ((m_ghpcStartPc == 0x30b0u || m_ghpcStartPc == 0xcd8u) &&
                twShown[cutI][m_ghpcStartPc]++ < (cutI ? 10 : 4))
            {
                extern unsigned long long g_ghpcVu1Mscals;
                std::fprintf(stderr,
                             "[vu1/topwatch] %s pc0x%x mscal=%llu by=%s top=%u entry=%08x %08x %08x %08x"
                             " | store=%s pc=0x%x after=%08x %08x %08x %08x"
                             " | read0=%s pc=0x%x c%d val=%u"
                             " | diff=%s pc=0x%x c%d val=%u entryval=%u afterStore=%d\n",
                             cutI ? "CUT " : "ebit", m_ghpcStartPc, g_ghpcVu1Mscals,
                             g_ghpcVu1Entry ? "mscnt" : "mscal", (unsigned)g_twTop,
                             g_twEntry[0], g_twEntry[1], g_twEntry[2], g_twEntry[3],
                             g_twStore.seen ? "yes" : "no", (unsigned)g_twStore.pc,
                             g_twStore.after[0], g_twStore.after[1], g_twStore.after[2], g_twStore.after[3],
                             g_twFirstRead.seen ? "yes" : "no", (unsigned)g_twFirstRead.pc,
                             g_twFirstRead.comp, (unsigned)g_twFirstRead.val,
                             g_twDiffRead.seen ? "yes" : "no", (unsigned)g_twDiffRead.pc,
                             g_twDiffRead.comp, (unsigned)g_twDiffRead.val,
                             (unsigned)g_twEntry[g_twDiffRead.comp & 3], (int)g_twDiffRead.afterStore);
                {
                    // Where the output pointer comes from. qw688.x and qw689.x
                    // are the jump table 0x30b0 calls through; the second
                    // callee is the one place left in its path that could set
                    // vf2, so its code is dumped once per program.
                    uint32_t q688 = 0u, q689 = 0u;
                    std::memcpy(&q688, vuData + 688u * 16u, 4);
                    std::memcpy(&q689, vuData + 689u * 16u, 4);
                    char b2[400];
                    std::snprintf(b2, sizeof(b2),
                                  "[vu1/topwatch]   vf2 entry=%08x %08x %08x %08x atIssue.x=%08x"
                                  " | first=%s pc=0x%x %s lo=0x%08x hi=0x%08x lanes=%x x=%08x"
                                  " | last=%s pc=0x%x x=%08x | qw688.x=%u qw689.x=%u\n",
                                  g_twVf2Entry[0], g_twVf2Entry[1], g_twVf2Entry[2], g_twVf2Entry[3],
                                  g_twVf2AtIssue[0],
                                  g_twVf2First.seen ? "yes" : "no", g_twVf2First.pc,
                                  g_twVf2First.upper ? "upper" : "lower",
                                  g_twVf2First.lo, g_twVf2First.hi, (unsigned)g_twVf2First.lanes,
                                  g_twVf2First.v[0],
                                  g_twVf2Last.seen ? "yes" : "no", g_twVf2Last.pc, g_twVf2Last.v[0],
                                  q688 & 0xFFFFu, q689 & 0xFFFFu);
                    std::string s2 = b2;
                    static std::map<uint32_t, bool> calleeDumped;
                    const uint32_t c2 = (q689 & 0xFFFFu) * 8u;
                    if (!calleeDumped[m_ghpcStartPc] && c2 + 8u <= codeSize)
                    {
                        calleeDumped[m_ghpcStartPc] = true;
                        for (uint32_t a = c2; a + 8u <= codeSize && a - c2 < 40u * 8u; a += 8u)
                        {
                            uint32_t l = 0u, h = 0u;
                            std::memcpy(&l, vuCode + a, 4);
                            std::memcpy(&h, vuCode + a + 4, 4);
                            std::snprintf(b2, sizeof(b2), "[vu1/topwatch]   callee2 0x%04x lo=0x%08x hi=0x%08x\n", a, l, h);
                            s2 += b2;
                        }
                    }
                    static std::map<uint32_t, bool> writerDumped;
                    if (g_twVf2First.seen && !writerDumped[m_ghpcStartPc])
                    {
                        writerDumped[m_ghpcStartPc] = true;
                        const uint32_t wpc = g_twVf2First.pc;
                        const uint32_t from = (wpc >= 0x30u) ? wpc - 0x30u : 0u;
                        for (uint32_t a = from; a <= wpc + 0x10u && a + 8u <= codeSize; a += 8u)
                        {
                            uint32_t l = 0u, h = 0u;
                            std::memcpy(&l, vuCode + a, 4);
                            std::memcpy(&h, vuCode + a + 4, 4);
                            std::snprintf(b2, sizeof(b2), "[vu1/topwatch]   vf2writer 0x%04x lo=0x%08x hi=0x%08x%s\n",
                                          a, l, h, a == wpc ? "  <== first vf2 write" : "");
                            s2 += b2;
                        }
                    }
                    std::fwrite(s2.data(), 1, s2.size(), stderr);
                }
                if (g_twIssue.seen)
                {
                    // Name the store and the register its address came from.
                    // No VU disassembler exists here, so both VI fields are
                    // reported (VIS is the base for ISW, VIT for SQ) and the
                    // code around it is dumped raw once per program.
                    const uint32_t ipc = g_twIssue.pc;
                    uint32_t ilo = 0u;
                    if (ipc + 8u <= codeSize)
                        std::memcpy(&ilo, vuCode + ipc, 4);
                    const unsigned fs = VIS(ilo), ft = VIT(ilo);
                    std::string s;
                    char b[256];
                    std::snprintf(b, sizeof(b),
                                  "[vu1/topwatch]   issue pc=0x%x lo=0x%08x vis=vi%u(%d) vit=vi%u(%d)\n",
                                  ipc, ilo, fs, (int)(int16_t)g_twIssue.vi[fs],
                                  ft, (int)(int16_t)g_twIssue.vi[ft]);
                    s += b;
                    const unsigned regs[2] = {fs, ft};
                    for (unsigned r : regs)
                    {
                        if (r == 0u)
                            continue;
                        const ViProv &p = g_twIssue.prov[r];
                        if (p.seen)
                            std::snprintf(b, sizeof(b),
                                          "[vu1/topwatch]   vi%u entry=%d first pc=0x%x lo=0x%08x val=%d load=%d addr=0x%x"
                                          " | last pc=0x%x lo=0x%08x val=%d\n",
                                          r, (int)(int16_t)p.entry, p.first.pc, p.first.lo,
                                          (int)(int16_t)p.first.val, (int)p.first.isLoad, p.first.addr,
                                          p.last.pc, p.last.lo, (int)(int16_t)p.last.val);
                        else
                            std::snprintf(b, sizeof(b),
                                          "[vu1/topwatch]   vi%u entry=%d, not written this run before the store\n",
                                          r, (int)(int16_t)p.entry);
                        s += b;
                    }
                    static std::map<uint32_t, bool> codeDumped;
                    if (cutI && !codeDumped[m_ghpcStartPc] && codeSize >= 8u)
                    {
                        codeDumped[m_ghpcStartPc] = true;
                        uint32_t lo2 = ipc, hi2 = ipc;
                        if (g_twDiffRead.seen)
                        {
                            lo2 = std::min(lo2, g_twDiffRead.pc);
                            hi2 = std::max(hi2, g_twDiffRead.pc);
                        }
                        const uint32_t from = ((lo2 >= 0x40u) ? lo2 - 0x40u : 0u) & ~7u;
                        const uint32_t to = std::min<uint32_t>(hi2 + 0x18u, codeSize - 8u);
                        for (uint32_t a = from; a <= to && a - from < 64u * 8u; a += 8u)
                        {
                            uint32_t l = 0u, h = 0u;
                            std::memcpy(&l, vuCode + a, 4);
                            std::memcpy(&h, vuCode + a + 4, 4);
                            std::snprintf(b, sizeof(b), "[vu1/topwatch]   code 0x%04x lo=0x%08x hi=0x%08x%s\n",
                                          a, l, h,
                                          a == ipc ? "  <== store"
                                          : (g_twDiffRead.seen && a == g_twDiffRead.pc) ? "  <== diff read" : "");
                            s += b;
                        }
                    }
                    std::fwrite(s.data(), 1, s.size(), stderr);
                }
            }
        }

        // The first cut-off run of the whole session, on the same clock as
        // [projw]. This is what orders "the data went bad" against "the loop
        // stopped ending", which is the difference between the runaway being
        // the cause and it being a symptom. Unconditional: it costs one line
        // per session and it is the line the whole question turns on.
        if (why == kBudget)
        {
            static bool announced = false;
            if (!announced)
            {
                announced = true;
                extern unsigned long long g_ghpcVu1Mscals;
                std::fprintf(stderr,
                             "[vu1/first-runaway] mscal=%llu startPc=0x%x instrs=%llu\n",
                             g_ghpcVu1Mscals, m_ghpcStartPc,
                             (unsigned long long)ghpcInstrs);
            }
        }

        // Dump the visit histogram of a run that was cut off, plus the raw
        // instruction words at the hot addresses so the branch can be decoded
        // without needing a second run to fetch them.
        if (ghpcLoopProbe && why == kBudget && ghpcLoopDumped < ghpcLoopDumps)
        {
            ++ghpcLoopDumped;
            std::vector<std::pair<uint32_t, uint32_t>> hot;  // (hits, slot)
            for (uint32_t i = 0u; i < m_ghpcPcHits.size(); ++i)
                if (m_ghpcPcHits[i] != 0u)
                    hot.emplace_back(m_ghpcPcHits[i], i);
            std::sort(hot.begin(), hot.end(),
                      [](const std::pair<uint32_t, uint32_t> &a,
                         const std::pair<uint32_t, uint32_t> &b) { return a.first > b.first; });

            std::string line = "[vu1/loop] mscal cut off at startPc=0x" ;
            char hdr[128];
            std::snprintf(hdr, sizeof(hdr), "%x instrs=%llu distinctPcs=%zu\n",
                          m_ghpcStartPc, (unsigned long long)ghpcInstrs, hot.size());
            line += hdr;
            for (size_t i = 0u; i < hot.size() && i < 12u; ++i)
            {
                const uint32_t off = hot[i].second * 8u;
                uint32_t lo = 0u, hi = 0u;
                if (off + 8u <= codeSize)
                {
                    std::memcpy(&lo, vuCode + off, 4);
                    std::memcpy(&hi, vuCode + off + 4, 4);
                }
                char row[160];
                std::snprintf(row, sizeof(row),
                              "[vu1/loop]   pc=0x%04x hits=%u lo=0x%08x hi=0x%08x\n",
                              off, hot[i].first, lo, hi);
                line += row;
            }

            // The back edge with the most repeats is the loop. Its target is
            // the top of the body and its source is the branch that was
            // supposed to fall through and did not.
            std::vector<std::pair<uint32_t, uint32_t>> edges;  // (count, key)
            for (const auto &kv : ghpcBackEdges)
                edges.emplace_back(kv.second, kv.first);
            std::sort(edges.begin(), edges.end(),
                      [](const std::pair<uint32_t, uint32_t> &a,
                         const std::pair<uint32_t, uint32_t> &b) { return a.first > b.first; });
            for (size_t i = 0u; i < edges.size() && i < 4u; ++i)
            {
                char row[128];
                std::snprintf(row, sizeof(row),
                              "[vu1/loop]   backedge 0x%04x -> 0x%04x taken=%u\n",
                              (edges[i].second >> 16) & 0xFFFFu,
                              edges[i].second & 0xFFFFu, edges[i].first);
                line += row;
            }

            // Every VI register at the moment it was cut off. The loop counter
            // is one of these, and whether it is stuck, negative, or absurd is
            // the difference between a wrong compare and a wrong load.
            line += "[vu1/loop]   vi:";
            for (int r = 0; r < 16; ++r)
            {
                char row[24];
                std::snprintf(row, sizeof(row), " %d=%d", r, (int)(int16_t)m_state.vi[r]);
                line += row;
            }
            line += "\n";

            // VI provenance: decode the back-edge branch itself (not just a
            // hot pc) to find which VI register(s) the test that fails to
            // exit actually reads, then name the instruction that put the
            // value there, and diff it against the last time this same
            // microprogram terminated normally.
            //
            // The edge key's "from" is NOT the branch's own pc. It is
            // recorded as m_state.pc at the moment the jump actually takes
            // effect, which is one branch-delay slot AFTER the branch
            // instruction executed: pc advances past the branch (+8) to the
            // delay slot, the delay slot then executes and advances again
            // (+8) before the pending jump is applied. So the branch that
            // produced this edge sits 16 bytes before the recorded "from".
            // Verified against the disassembly: the recorded "from" decoded
            // as a JR reading a register that stayed constant across every
            // cutoff, while "from - 16" decoded as the IBNE whose vi operand
            // matched the register climbing without bound.
            if (!edges.empty())
            {
                const uint32_t edgeFromPc = (edges[0].second >> 16) & 0xFFFFu;
                const uint32_t fromPc = (edgeFromPc - 16u) & 0xFFFFu;
                uint32_t lo = 0u;
                if (fromPc + 4u <= codeSize)
                    std::memcpy(&lo, vuCode + fromPc, 4);
                const uint8_t opHi = (lo >> 25) & 0x7Fu;
                const uint8_t vis = VIS(lo);
                const uint8_t vit = VIT(lo);
                uint8_t regs[2] = {0, 0};
                int nRegs = 0;
                // IBEQ/IBNE test two VI regs; the four sign-compares test one.
                if (opHi == 0x28u || opHi == 0x29u) { regs[0] = vis; regs[1] = vit; nRegs = 2; }
                else if (opHi == 0x2Cu || opHi == 0x2Du || opHi == 0x2Eu || opHi == 0x2Fu) { regs[0] = vis; nRegs = 1; }

                char row[256];
                std::snprintf(row, sizeof(row),
                              "[vu1/loop]   branch@0x%04x lo=0x%08x op=0x%02x tests vi%u%s%u\n",
                              fromPc, (unsigned)lo, (unsigned)opHi, (unsigned)regs[0],
                              nRegs == 2 ? ",vi" : "",
                              nRegs == 2 ? (unsigned)regs[1] : 0u);
                line += row;

                auto emit = [&](const char *tag, unsigned r, const ViProv &p) {
                    char b[256];
                    if (p.seen)
                        std::snprintf(b, sizeof(b),
                                      "[vu1/loop]   vi%u %s entry=%d first pc=0x%04x lo=0x%08x val=%d load=%d addr=0x%x\n",
                                      r, tag, (int)(int16_t)p.entry, (unsigned)p.first.pc,
                                      (unsigned)p.first.lo, (int)(int16_t)p.first.val,
                                      (int)p.first.isLoad, (unsigned)p.first.addr);
                    else
                        std::snprintf(b, sizeof(b),
                                      "[vu1/loop]   vi%u %s entry=%d first NONE (never written this mscal)\n",
                                      r, tag, (int)(int16_t)p.entry);
                    line += b;
                    std::snprintf(b, sizeof(b),
                                  "[vu1/loop]   vi%u %s prev pc=0x%04x lo=0x%08x val=%d load=%d addr=0x%x"
                                  " | last pc=0x%04x lo=0x%08x val=%d\n",
                                  r, tag, (unsigned)p.prev.pc, (unsigned)p.prev.lo,
                                  (int)(int16_t)p.prev.val, (int)p.prev.isLoad, (unsigned)p.prev.addr,
                                  (unsigned)p.last.pc, (unsigned)p.last.lo, (int)(int16_t)p.last.val);
                    line += b;
                };
                for (int i = 0; i < nRegs; ++i)
                {
                    const unsigned r = regs[i];
                    if (r == 0u) continue;
                    emit("runaway", r, g_ghpcViProv[r]);
                    const auto it2 = g_ghpcHealthyVi.find(m_ghpcStartPc);
                    if (it2 != g_ghpcHealthyVi.end())
                    {
                        std::snprintf(row, sizeof(row), "[vu1/loop]   vi%u healthy snapshot atMscal=%llu\n",
                                      r, (unsigned long long)it2->second.atMscal);
                        line += row;
                        emit("healthy", r, it2->second.v[r]);
                    }
                    else
                        line += "[vu1/loop]   (no healthy termination of this startPc recorded yet)\n";
                }
            }

            std::fwrite(line.data(), 1, line.size(), stderr);
        }

        static const uint64_t every = []() -> uint64_t {
            const char *e = std::getenv("GHPC_VU1_CENSUS");
            return e ? std::strtoull(e, nullptr, 0) : 0ull;
        }();
        if (every && (total % every) == 0ull)
        {
            // One buffer, one write. call_hist_dump splices concurrent guest
            // stderr into its own lines because it streams field by field, and
            // a census that cannot be parsed is not a census.
            std::string line = "[vu1/census] total=" + std::to_string(total);
            for (const auto &kv : byPc)
            {
                const Bucket &v = kv.second;
                const uint64_t runs = v.n[0] + v.n[1] + v.n[2] + v.n[3];
                char buf[224];
                std::snprintf(buf, sizeof(buf),
                              " pc0x%x{ebit=%llu/%lluM budget=%llu/%lluM offend=%llu"
                              " stop=%llu avgEbit=%llu avgBudget=%llu maxInstr=%llu}",
                              kv.first,
                              (unsigned long long)v.n[0],
                              (unsigned long long)(v.instrsBy[0] / 1000000ull),
                              (unsigned long long)v.n[1],
                              (unsigned long long)(v.instrsBy[1] / 1000000ull),
                              (unsigned long long)v.n[2], (unsigned long long)v.n[3],
                              (unsigned long long)(v.n[0] ? v.instrsBy[0] / v.n[0] : 0ull),
                              (unsigned long long)(v.n[1] ? v.instrsBy[1] / v.n[1] : 0ull),
                              (unsigned long long)v.maxInstrs);
                (void)runs;
                line += buf;
            }
            line += "\n[vu1/topcensus] total=" + std::to_string(total);
            for (const auto &kv : byPcTop)
            {
                const TopBucket &t = kv.second;
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              " pc0x%x/top%u{fresh ebit=%llu cut=%llu | stale ebit=%llu cut=%llu}",
                              (unsigned)(kv.first >> 16), (unsigned)(kv.first & 0xFFFFu),
                              (unsigned long long)t.n[1][0], (unsigned long long)t.n[1][1],
                              (unsigned long long)t.n[0][0], (unsigned long long)t.n[0][1]);
                line += buf;
            }
            // s = a VU store hit qw[TOP], d = a header load differed from entry.
            line += "\n[vu1/topwatch] total=" + std::to_string(total);
            for (const auto &kv : twByPc)
            {
                const TwBucket &t = kv.second;
                char buf[640];
                int w = std::snprintf(buf, sizeof(buf), " pc0x%x{", (unsigned)kv.first);
                for (int c = 0; c < 2; ++c)
                    for (int e = 0; e < 2; ++e)
                        w += std::snprintf(buf + w, sizeof(buf) - (size_t)w,
                                           "%s/%s s0d0=%llu s0d1=%llu s1d0=%llu s1d1=%llu%s",
                                           c ? "cut" : "end", e ? "mscnt" : "mscal",
                                           (unsigned long long)t.n[c][e][0][0], (unsigned long long)t.n[c][e][0][1],
                                           (unsigned long long)t.n[c][e][1][0], (unsigned long long)t.n[c][e][1][1],
                                           (c == 1 && e == 1) ? "}" : " | ");
                line += buf;
            }
            // w = vf2 written this run, z = vf2.x low 16 bits were 0 at entry.
            line += "\n[vu1/vf2] total=" + std::to_string(total);
            for (const auto &kv : vfByPc)
            {
                const VfBucket &t = kv.second;
                char buf[640];
                int w = std::snprintf(buf, sizeof(buf), " pc0x%x{", (unsigned)kv.first);
                for (int c = 0; c < 2; ++c)
                    for (int z = 0; z < 2; ++z)
                        w += std::snprintf(buf + w, sizeof(buf) - (size_t)w,
                                           "%s/%s w0z0=%llu w0z1=%llu w1z0=%llu w1z1=%llu%s",
                                           c ? "cut" : "end", z ? "top0" : "topN",
                                           (unsigned long long)t.n[c][z][0][0], (unsigned long long)t.n[c][z][0][1],
                                           (unsigned long long)t.n[c][z][1][0], (unsigned long long)t.n[c][z][1][1],
                                           (c == 1 && z == 1) ? "}" : " | ");
                line += buf;
            }
            line += "\n";
            std::fwrite(line.data(), 1, line.size(), stderr);
        }
    }
#endif
    m_state.cycles = m_cycle;
    if (useVuRounding && previousRoundingMode != -1)
        std::fesetround(previousRoundingMode);
}
