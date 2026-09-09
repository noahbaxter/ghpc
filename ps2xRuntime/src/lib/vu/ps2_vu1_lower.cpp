#include <map>
#include <cstdio>
#include "runtime/ps2_vu1.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "ps2_vu1_detail.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace
{
    float vuEatan(float value)
    {
        constexpr float coefficients[] = {
            0.999999344348907f,
            -0.333298563957214f,
            0.199465364217758f,
            -0.13085337519646f,
            0.096420042216778f,
            -0.055909886956215f,
            0.021861229091883f,
            -0.004054057877511f};
        constexpr float quarterPi = 0.785398185253143f;

        const float squared = value * value;
        float polynomial = coefficients[7];
        for (int index = 6; index >= 0; --index)
            polynomial = coefficients[index] + squared * polynomial;
        return quarterPi + value * polynomial;
    }

    float vuEsin(float value)
    {
        constexpr float coefficients[] = {
            1.0f,
            -0.166666567325592f,
            0.008333025500178f,
            -0.000198074136279f,
            0.000002601886990f};

        const float squared = value * value;
        float polynomial = coefficients[4];
        for (int index = 3; index >= 0; --index)
            polynomial = coefficients[index] + squared * polynomial;
        return value * polynomial;
    }

    float vuEexp(float value)
    {
        constexpr float coefficients[] = {
            0.249998688697815f,
            0.031257584691048f,
            0.002591371303424f,
            0.000171562001924f,
            0.000005430199963f,
            0.000000690600018f};

        float polynomial = coefficients[5];
        for (int index = 4; index >= 0; --index)
            polynomial = coefficients[index] + value * polynomial;
        polynomial = 1.0f + value * polynomial;
        polynomial *= polynomial;
        polynomial *= polynomial;
        return polynomial != 0.0f ? 1.0f / polynomial : std::numeric_limits<float>::max();
    }
}

// ============================================================================
// Lower instructions
// ============================================================================
void VU1Interpreter::execLower(uint32_t instr, uint8_t *vuData, uint32_t dataSize, GS &gs, PS2Memory *memory, uint32_t upperInstr)
{
    (void)upperInstr;
    if (instr == 0x00000000 || instr == 0x8000033C) // NOP
        return;

    uint8_t opHi = (instr >> 25) & 0x7F;
    const uint32_t pcMask = microAddressMask();

    // The lower instruction encoding uses bits 31:25 for the primary opcode
    switch (opHi)
    {
    case 0x00: // LQ (Load Quadword from VU data memory)
    {
        uint8_t it = FT(instr);  // VF destination
        uint8_t is = VIS(instr); // VI base
        uint8_t dest = (instr >> 21) & 0xF;
        int16_t imm = IMM11(instr);
        uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[is] + imm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize)
        {
            float tmp[4];
            std::memcpy(tmp, vuData + addr, 16);
            applyDest(m_state.vf[it], tmp, dest);
            #if GHPC_DIAG
            // Reads on the same ordered log as the stores, so a quadword
            // clobbered before it was read is distinguishable from one
            // legitimately written after.
            {
                extern unsigned long long g_ghpcVu1Mscals;
                extern void ghpcLogQwWrite(unsigned long long, int, unsigned, const unsigned *, const unsigned *);
                unsigned rv[4];
                std::memcpy(rv, vuData + addr, sizeof(rv));
                ghpcLogQwWrite(g_ghpcVu1Mscals, 2 + (int)(m_state.pc << 4), (unsigned)(addr / 16u), rv, rv);
            }
            #endif
#if GHPC_DIAG
            { extern unsigned int g_ghpcVfSrcAddr[32]; g_ghpcVfSrcAddr[it] = addr | 0x80000000u; }
            if (addr >= 0x2bc0u && addr < 0x2c00u)
            {
                static int logs = 0;
                if (logs < 12)
                {
                    ++logs;
                    std::fprintf(stderr,
                        "[mtx/lq] pc=0x%04x vf%02u <- addr=0x%04x (qw=%u) viBase[%u]=%d imm=%d"
                        " instr=0x%08x dest=0x%x (x=%u y=%u z=%u w=%u)\n",
                        (unsigned)m_state.pc, (unsigned)it, (unsigned)addr,
                        (unsigned)(addr / 16u), (unsigned)is, (int)m_state.vi[is], (int)imm,
                        (unsigned)instr, (unsigned)dest,
                        (dest >> 3) & 1u, (dest >> 2) & 1u, (dest >> 1) & 1u, dest & 1u);
                }
            }
#endif
        }
        return;
    }
    case 0x01: // SQ (Store Quadword to VU data memory)
    {
#if GHPC_DIAG
        {
            // Where do VU stores land? A GIF packet at address 0 would have to
            // be built by these.
            extern void ghpcNoteVuStore(uint32_t qw);
            ghpcNoteVuStore((uint32_t)((uint16_t)m_state.vi[VIT(instr)] + (int32_t)IMM11(instr)));
        }
#endif
        uint8_t is = FS(instr);  // VF source
        uint8_t it = VIT(instr); // VI base
        uint8_t dest = (instr >> 21) & 0xF;
        int16_t imm = IMM11(instr);
        uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[it] + imm)) * 16u;
        addr &= (dataSize - 1);
#if GHPC_DIAG
        // Where does the program think its output buffer is? If the base VI is
        // wrong the packet lands on top of the input double buffer.
        if (std::getenv("GHPC_STORE_PC"))
        {
            extern unsigned long long g_ghpcVu1Mscals;
            const unsigned long long from = std::strtoull(std::getenv("GHPC_STORE_PC"), nullptr, 0);
            if (g_ghpcVu1Mscals >= from && g_ghpcVu1Mscals <= from + 2ull)
                std::fprintf(stderr, "[vu1/storeaddr] ms=%llu pc=0x%04x vi%u=%d imm=%d -> qw=%u top=%u\n",
                             g_ghpcVu1Mscals, (unsigned)m_state.pc, (unsigned)it,
                             (int)m_state.vi[it], (int)imm, (unsigned)(addr / 16u),
                             (unsigned)m_state.top);
        }
#endif
        if (addr + 16 <= dataSize)
        {
            uint32_t words[4]{};
            std::memcpy(words, m_state.vf[is], sizeof(words));
            queueStore(addr, words, dest);
        }
        return;
    }
    case 0x04: // ILW (Integer Load Word from VU data memory)
    {
        uint8_t it = VIT(instr); // VI destination
        uint8_t is = VIS(instr); // VI base
        uint8_t dest = (instr >> 21) & 0xF;
        int16_t imm = IMM11(instr);
        uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[is] + imm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize)
        {
            int comp = 0;
            if (dest & 0x8)
                comp = 0;
            else if (dest & 0x4)
                comp = 1;
            else if (dest & 0x2)
                comp = 2;
            else
                comp = 3;
            uint32_t v;
            std::memcpy(&v, vuData + addr + comp * 4, 4);
            if (it != 0)
                m_state.vi[it] = (int32_t)(int16_t)(v & 0xFFFF);
        }
        return;
    }
    case 0x05: // ISW (Integer Store Word to VU data memory)
    {
        uint8_t it = VIT(instr); // VI source
        uint8_t is = VIS(instr); // VI base
        uint8_t dest = (instr >> 21) & 0xF;
        int16_t imm = IMM11(instr);
        uint32_t addr = ((uint32_t)(int32_t)(m_state.vi[is] + imm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize)
        {
            const uint32_t val = static_cast<uint32_t>(static_cast<uint16_t>(m_state.vi[it] & 0xFFFF));
            const uint32_t words[4] = {val, val, val, val};
            queueStore(addr, words, dest);
        }
        return;
    }
    case 0x08: // IADDIU
    {
        uint8_t it = VIT(instr);
        uint8_t is = VIS(instr);
        int16_t imm = (int16_t)(instr & 0x7FF) | ((instr >> 10) & 0x7800);
        if (it != 0)
            m_state.vi[it] = (int16_t)(m_state.vi[is] + imm);
        return;
    }
    case 0x09: // ISUBIU
    {
        uint8_t it = VIT(instr);
        uint8_t is = VIS(instr);
        int16_t imm = (int16_t)(instr & 0x7FF) | ((instr >> 10) & 0x7800);
        if (it != 0)
            m_state.vi[it] = (int16_t)(m_state.vi[is] - imm);
        return;
    }
    case 0x10: // FCEQ
    {
        uint32_t imm24 = instr & 0xFFFFFF;
        if (1 != 0)
            m_state.vi[1] = ((m_state.clip & 0xFFFFFF) == imm24) ? 1 : 0;
        return;
    }
    case 0x11: // FCSET
    {
        queueFcset(instr & 0xFFFFFFu);
        return;
    }
    case 0x12: // FCAND
    {
        uint32_t imm24 = instr & 0xFFFFFF;
        if (1 != 0)
            m_state.vi[1] = ((m_state.clip & imm24) != 0) ? 1 : 0;
        return;
    }
    case 0x13: // FCOR
    {
        uint32_t imm24 = instr & 0xFFFFFF;
        if (1 != 0)
            m_state.vi[1] = ((m_state.clip | imm24) == 0xFFFFFF) ? 1 : 0;
        return;
    }
    case 0x14: // FSEQ
    {
        const uint8_t it = VIT(instr);
        const uint16_t imm12 = static_cast<uint16_t>((((instr >> 21) & 0x1u) << 11) | (instr & 0x7FFu));
        if (it != 0)
            m_state.vi[it] = ((m_state.status & 0xFFFu) == imm12) ? 1 : 0;
        return;
    }
    case 0x15: // FSSET
    {
        const uint16_t imm12 = static_cast<uint16_t>((((instr >> 21) & 0x1u) << 11) | (instr & 0x7FFu));
        queueFsset(imm12);
        return;
    }
    case 0x16: // FSAND
    {
        const uint8_t it = VIT(instr);
        const uint16_t imm12 = static_cast<uint16_t>((((instr >> 21) & 0x1u) << 11) | (instr & 0x7FFu));
        if (it != 0)
            m_state.vi[it] = static_cast<int32_t>((m_state.status & 0xFFFu) & imm12);
        return;
    }
    case 0x17: // FSOR
    {
        const uint8_t it = VIT(instr);
        const uint16_t imm12 = static_cast<uint16_t>((((instr >> 21) & 0x1u) << 11) | (instr & 0x7FFu));
        if (it != 0)
            m_state.vi[it] = static_cast<int32_t>((m_state.status & 0xFFFu) | imm12);
        return;
    }
    case 0x18: // FMEQ
    {
        uint8_t it = VIT(instr);
        uint8_t is = VIS(instr);
        if (it != 0)
            m_state.vi[it] = ((m_state.mac & 0xFFFF) == (uint32_t)(uint16_t)m_state.vi[is]) ? 1 : 0;
        return;
    }
    case 0x1A: // FMAND
    {
        uint8_t it = VIT(instr);
        uint8_t is = VIS(instr);
        if (it != 0)
            m_state.vi[it] = (int32_t)(m_state.mac & (uint32_t)(uint16_t)m_state.vi[is]);
        return;
    }
    case 0x1B: // FMOR
    {
        uint8_t it = VIT(instr);
        uint8_t is = VIS(instr);
        if (it != 0)
            m_state.vi[it] = (int32_t)(m_state.mac | (uint32_t)(uint16_t)m_state.vi[is]);
        return;
    }
    case 0x1C: // FCGET
    {
        const uint8_t it = VIT(instr);
        if (it != 0)
            m_state.vi[it] = static_cast<int32_t>(m_state.clip & 0x0FFFu);
        return;
    }
    case 0x20: // B (unconditional branch)
    {
        int16_t imm = IMM11(instr);
        uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
        m_state.branchPending = true;
        m_state.branchTarget = target;
        m_state.branchDelay = 1;
        return;
    }
    case 0x21: // BAL (Branch and link)
    {
        uint8_t it = VIT(instr);
        int16_t imm = IMM11(instr);
        uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
        if (it != 0)
            m_state.vi[it] = (int32_t)((m_state.pc + 16) / 8);
        m_state.branchPending = true;
        m_state.branchTarget = target;
        m_state.branchDelay = 1;
        return;
    }
    case 0x24: // JR
    {
        uint8_t is = VIS(instr);
        uint32_t target = ((uint32_t)(uint16_t)readBranchVi(is) * 8u) & pcMask;
#if GHPC_DIAG
        {
            static std::map<uint32_t, unsigned long long> tg;
            ++tg[target];
            static unsigned long long n = 0;
            if ((++n % 20000ull) == 0ull) {
                std::fprintf(stderr, "[vu1] JR targets:");
                int k = 0;
                for (const auto &kv : tg) if (k++ < 16) std::fprintf(stderr, " 0x%x=%llu", kv.first, kv.second);
                std::fprintf(stderr, "\n");
            }
        }
#endif
        m_state.branchPending = true;
        m_state.branchTarget = target;
        m_state.branchDelay = 1;
        return;
    }
    case 0x25: // JALR
    {
        uint8_t it = VIT(instr);
        uint8_t is = VIS(instr);
        uint32_t target = ((uint32_t)(uint16_t)readBranchVi(is) * 8u) & pcMask;
        if (it != 0)
            m_state.vi[it] = (int32_t)((m_state.pc + 16) / 8);
        m_state.branchPending = true;
        m_state.branchTarget = target;
        m_state.branchDelay = 1;
        return;
    }
    case 0x28: // IBEQ
    {
#if GHPC_DIAG
        if (m_state.pc == 0x1198u)
        {
            // The conditional immediately before the bad kick at 0x11a0.
            static unsigned long long taken = 0, fell = 0;
            static int32_t lastA = 0, lastB = 0;
            const int32_t a = m_state.vi[VIT(instr)];
            const int32_t b = m_state.vi[VIS(instr)];
            lastA = a; lastB = b;
            if (a == b) ++taken; else ++fell;
            static unsigned long long n = 0;
            if ((++n % 3000ull) == 0ull)
                std::fprintf(stderr,
                    "[vu1] 0x1198 IBEQ vi%u=%d vi%u=%d | equal(branch)=%llu notEqual(fallthrough)=%llu\n",
                    (unsigned)VIT(instr), a, (unsigned)VIS(instr), b, taken, fell);
        }
#endif
        uint8_t it = VIT(instr);
        uint8_t is = VIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)readBranchVi(is) == (int16_t)readBranchVi(it))
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
            m_state.branchPending = true;
            m_state.branchTarget = target;
            m_state.branchDelay = 1;
        }
        return;
    }
    case 0x29: // IBNE
    {
        uint8_t it = VIT(instr);
        uint8_t is = VIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)readBranchVi(is) != (int16_t)readBranchVi(it))
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
            m_state.branchPending = true;
            m_state.branchTarget = target;
            m_state.branchDelay = 1;
        }
        return;
    }
    case 0x2C: // IBLTZ
    {
        uint8_t is = VIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)readBranchVi(is) < 0)
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
            m_state.branchPending = true;
            m_state.branchTarget = target;
            m_state.branchDelay = 1;
        }
        return;
    }
    case 0x2D: // IBGTZ
    {
        uint8_t is = VIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)readBranchVi(is) > 0)
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
            m_state.branchPending = true;
            m_state.branchTarget = target;
            m_state.branchDelay = 1;
        }
        return;
    }
    case 0x2E: // IBLEZ
    {
        uint8_t is = VIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)readBranchVi(is) <= 0)
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
            m_state.branchPending = true;
            m_state.branchTarget = target;
            m_state.branchDelay = 1;
        }
        return;
    }
    case 0x2F: // IBGEZ
    {
        uint8_t is = VIS(instr);
        int16_t imm = IMM11(instr);
        if ((int16_t)readBranchVi(is) >= 0)
        {
            uint32_t target = (m_state.pc + 8 + imm * 8) & pcMask;
            m_state.branchPending = true;
            m_state.branchTarget = target;
            m_state.branchDelay = 1;
        }
        return;
    }

    case 0x40: // Lower1 / lower special. Bit31 set; low 6 bits select integer or special op.
    {
        const uint8_t funct = instr & 0x3Fu;
        const uint8_t vfT = FT(instr);
        const uint8_t vfS = FS(instr);
        const uint8_t viT = VIT(instr);
        const uint8_t viS = VIS(instr);
        const uint8_t viD = VID(instr);
        const uint8_t dest = (instr >> 21) & 0xF;

        switch (funct)
        {
        case 0x30: // IADD
            if (viD != 0)
                m_state.vi[viD] = (int16_t)(m_state.vi[viS] + m_state.vi[viT]);
            return;
        case 0x31: // ISUB
            if (viD != 0)
                m_state.vi[viD] = (int16_t)(m_state.vi[viS] - m_state.vi[viT]);
            return;
        case 0x32: // IADDI
        {
            int16_t imm5 = (int16_t)((int32_t)((instr >> 6) & 0x1F) << 27 >> 27);
            if (viT != 0)
                m_state.vi[viT] = (int16_t)(m_state.vi[viS] + imm5);
            return;
        }
        case 0x34: // IAND
            if (viD != 0)
                m_state.vi[viD] = m_state.vi[viS] & m_state.vi[viT];
            return;
        case 0x35: // IOR
            if (viD != 0)
                m_state.vi[viD] = m_state.vi[viS] | m_state.vi[viT];
            return;

        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F: // Lower1 special. Dobie decodes this as (instr & 3) | ((instr >> 4) & 0x7C).
        {
            const uint8_t funct2 = (uint8_t)((instr & 0x3u) | ((instr >> 4) & 0x7Cu));
            switch (funct2)
            {
            case 0x30: // MOVE
            {
                float tmp[4];
                std::memcpy(tmp, m_state.vf[vfS], 16);
                applyDest(m_state.vf[vfT], tmp, dest);
                return;
            }
            case 0x31: // MR32 (rotate right by 32 bits = shift xyzw -> yzwx)
            {
                float tmp[4] = {m_state.vf[vfS][1], m_state.vf[vfS][2], m_state.vf[vfS][3], m_state.vf[vfS][0]};
                applyDest(m_state.vf[vfT], tmp, dest);
                return;
            }
            case 0x34: // LQI (Load Quadword, post-increment)
            {
                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[viS]) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                {
                    float tmp[4];
                    std::memcpy(tmp, vuData + addr, 16);
                    applyDest(m_state.vf[vfT], tmp, dest);
                    #if GHPC_DIAG
                    // Reads on the same ordered log as the stores, so a quadword
                    // clobbered before it was read is distinguishable from one
                    // legitimately written after.
                    {
                        extern unsigned long long g_ghpcVu1Mscals;
                        extern void ghpcLogQwWrite(unsigned long long, int, unsigned, const unsigned *, const unsigned *);
                        unsigned rv[4];
                        std::memcpy(rv, vuData + addr, sizeof(rv));
                        ghpcLogQwWrite(g_ghpcVu1Mscals, 2 + (int)(m_state.pc << 4), (unsigned)(addr / 16u), rv, rv);
                    }
                    #endif
#if GHPC_DIAG
                    { extern unsigned int g_ghpcVfSrcAddr[32]; g_ghpcVfSrcAddr[vfT] = addr | 0x80000000u; }
#endif
                }
                if (viS != 0)
                    m_state.vi[viS] = (int16_t)(m_state.vi[viS] + 1);
                return;
            }
            case 0x35: // SQI (Store Quadword, post-increment)
            {
                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[viT]) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                {
                    uint32_t words[4]{};
                    std::memcpy(words, m_state.vf[vfS], sizeof(words));
                    queueStore(addr, words, dest);
                }
                if (viT != 0)
                    m_state.vi[viT] = (int16_t)(m_state.vi[viT] + 1);
                return;
            }
            case 0x36: // LQD (Load Quadword, pre-decrement)
            {
                if (viS != 0)
                    m_state.vi[viS] = (int16_t)(m_state.vi[viS] - 1);
                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[viS]) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                {
                    float tmp[4];
                    std::memcpy(tmp, vuData + addr, 16);
                    applyDest(m_state.vf[vfT], tmp, dest);
                    #if GHPC_DIAG
                    // Reads on the same ordered log as the stores, so a quadword
                    // clobbered before it was read is distinguishable from one
                    // legitimately written after.
                    {
                        extern unsigned long long g_ghpcVu1Mscals;
                        extern void ghpcLogQwWrite(unsigned long long, int, unsigned, const unsigned *, const unsigned *);
                        unsigned rv[4];
                        std::memcpy(rv, vuData + addr, sizeof(rv));
                        ghpcLogQwWrite(g_ghpcVu1Mscals, 2 + (int)(m_state.pc << 4), (unsigned)(addr / 16u), rv, rv);
                    }
                    #endif
                }
                return;
            }
            case 0x37: // SQD (Store Quadword, pre-decrement)
            {
                if (viT != 0)
                    m_state.vi[viT] = (int16_t)(m_state.vi[viT] - 1);
                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[viT]) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                {
                    uint32_t words[4]{};
                    std::memcpy(words, m_state.vf[vfS], sizeof(words));
                    queueStore(addr, words, dest);
                }
                return;
            }
            case 0x38: // DIV
            {
                int fsf = (instr >> 21) & 0x3;
                int ftf = (instr >> 23) & 0x3;
                const float num = normalizeOperand(m_state.vf[vfS][fsf]);
                const float den = normalizeOperand(m_state.vf[vfT][ftf]);
                uint32_t statusDi = 0u;
                float result = 0.0f;
                if (den == 0.0f)
                {
                    statusDi = num == 0.0f ? 0x10u : 0x20u;
                    result = std::signbit(num) != std::signbit(den)
                                 ? -std::numeric_limits<float>::max()
                                 : std::numeric_limits<float>::max();
                }
                else
                {
                    result = num / den;
                }
                uint32_t ignoredFlags = 0u;
                result = normalizeResult(result, ignoredFlags);
                queueQ(result, 7u, statusDi);
                return;
            }
            case 0x39: // SQRT
            {
                int ftf = (instr >> 23) & 0x3;
                const float val = normalizeOperand(m_state.vf[vfT][ftf]);
                queueQ(std::sqrt(std::fabs(val)), 7u,
                       val < 0.0f ? 0x10u : 0u);
                return;
            }
            case 0x3A: // RSQRT
            {
                int fsf = (instr >> 21) & 0x3;
                int ftf = (instr >> 23) & 0x3;
                const float num = normalizeOperand(m_state.vf[vfS][fsf]);
                const float radicand = normalizeOperand(m_state.vf[vfT][ftf]);
                const float den = std::sqrt(std::fabs(radicand));
                uint32_t statusDi = radicand < 0.0f ? 0x10u : 0u;
                float result = 0.0f;
                if (den != 0.0f)
                    result = num / den;
                else
                {
                    statusDi = num == 0.0f ? 0x10u : 0x20u;
                    result = std::signbit(num)
                                 ? -std::numeric_limits<float>::max()
                                 : std::numeric_limits<float>::max();
                }
                uint32_t ignoredFlags = 0u;
                result = normalizeResult(result, ignoredFlags);
                queueQ(result, 13u, statusDi);
                return;
            }
            case 0x3B: // WAITQ
                return;
            case 0x3C: // MTIR (Move To Integer Register)
            {
#if GHPC_DIAG
                if (m_state.pc == 0x1160u)
                {
                    // Let the interpreter report its own decode, no hand maths.
                    const uint32_t cc = (instr >> 21) & 0x3u;
                    uint32_t fb = 0; std::memcpy(&fb, &m_state.vf[vfS][cc], 4);
                    static unsigned long long z = 0, nz = 0; static uint32_t lastnz = 0;
                    if ((fb & 0xFFFFu) == 0u) ++z; else { ++nz; lastnz = fb; }
                    static unsigned long long t = 0;
                    if ((++t % 3000ull) == 0ull)
                        std::fprintf(stderr,
                            "[vu1] MTIR@0x1160 viT=%u vfS=%u comp=%u | low16 zero=%llu nonzero=%llu lastNZbits=0x%x\n",
                            (unsigned)viT, (unsigned)vfS, cc, z, nz, lastnz);
                }
#endif
                // MTIR encodes a two-bit fsf component selector in bits
                // 22:21. It is not a four-bit destination mask.
                const uint32_t comp = (instr >> 21) & 0x3u;
                uint32_t fval;
                std::memcpy(&fval, &m_state.vf[vfS][comp], 4);
                if (viT != 0)
                    m_state.vi[viT] = (int32_t)(int16_t)(fval & 0xFFFF);
                return;
            }
            case 0x3D: // MFIR (Move From Integer Register)
            {
                float result[4];
                int32_t val = (int32_t)(int16_t)(m_state.vi[viS] & 0xFFFF);
                std::memcpy(&result[0], &val, 4);
                result[1] = result[0];
                result[2] = result[0];
                result[3] = result[0];
                applyDest(m_state.vf[vfT], result, dest);
                return;
            }
            case 0x3E: // ILWR - integer load word from address in VI[is]
            {
                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[viS]) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                {
                    int comp = 0;
                    if (dest & 0x8)
                        comp = 0;
                    else if (dest & 0x4)
                        comp = 1;
                    else if (dest & 0x2)
                        comp = 2;
                    else
                        comp = 3;
                    uint32_t v;
                    std::memcpy(&v, vuData + addr + comp * 4, 4);
                    if (viT != 0)
                        m_state.vi[viT] = (int32_t)(int16_t)(v & 0xFFFF);
                }
                return;
            }
            case 0x3F: // ISWR - integer store word to address in VI[is]
            {
                uint32_t addr = ((uint32_t)(uint16_t)m_state.vi[viS]) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                {
                    const uint32_t val =
                        static_cast<uint32_t>(static_cast<uint16_t>(m_state.vi[viT] & 0xFFFF));
                    const uint32_t words[4] = {val, val, val, val};
                    queueStore(addr, words, dest);
                }
                return;
            }
            case 0x40: // RNEXT
            {
                const uint32_t x = (m_state.r >> 4) & 1u;
                const uint32_t y = (m_state.r >> 22) & 1u;
                m_state.r = ((m_state.r << 1) ^ x ^ y) & 0x007FFFFFu;
                m_state.r |= 0x3F800000u;
                float value = 0.0f;
                std::memcpy(&value, &m_state.r, sizeof(value));
                const float result[4] = {value, value, value, value};
                applyDest(m_state.vf[vfT], result, dest);
                return;
            }
            case 0x41: // RGET
            {
                float value = 0.0f;
                std::memcpy(&value, &m_state.r, sizeof(value));
                const float result[4] = {value, value, value, value};
                applyDest(m_state.vf[vfT], result, dest);
                return;
            }
            case 0x42: // RINIT
            {
                const uint32_t component = (instr >> 21) & 3u;
                uint32_t bits = 0u;
                std::memcpy(&bits, &m_state.vf[vfS][component], sizeof(bits));
                m_state.r = 0x3F800000u | (bits & 0x007FFFFFu);
                return;
            }
            case 0x43: // RXOR
            {
                const uint32_t component = (instr >> 21) & 3u;
                uint32_t bits = 0u;
                std::memcpy(&bits, &m_state.vf[vfS][component], sizeof(bits));
                m_state.r = 0x3F800000u | ((m_state.r ^ bits) & 0x007FFFFFu);
                return;
            }
            case 0x64: // MFP (Move From P register)
            {
                float result[4] = {m_state.p, m_state.p, m_state.p, m_state.p};
                applyDest(m_state.vf[vfT], result, dest);
                return;
            }
            case 0x68: // XTOP - move current VIF1 TOP into VI register
            {
                if (viT != 0)
                    m_state.vi[viT] = (int32_t)(m_state.top & 0x3FFu);
                return;
            }
            case 0x69: // XITOP - move current VIF1 ITOP into VI register
            {
                if (viT != 0)
                    m_state.vi[viT] = (int32_t)(m_state.itop & 0x3FFu);
                return;
            }
            case 0x6C: // XGKICK - send GIF packet from VU1 data memory
#if GHPC_DIAG
                {
                    // Which VI register feeds XGKICK, and when is it zero?
                    static std::map<uint32_t, std::pair<unsigned long long, unsigned long long>> byReg;
                    const uint32_t v = (uint32_t)(uint16_t)m_state.vi[viS];
                    {
                        // PC of the kick itself, split by whether it reads zero.
                        static std::map<uint32_t, std::pair<unsigned long long, unsigned long long>> byPc;
                        auto &pe = byPc[m_state.pc];
                        if (v == 0u) ++pe.second; else ++pe.first;
                        static unsigned long long k = 0;
                        if ((++k % 6000ull) == 0ull) {
                            std::fprintf(stderr, "[vu1] XGKICK pc nonzero/zero:");
                            for (const auto &kv : byPc)
                                std::fprintf(stderr, " 0x%x=%llu/%llu", kv.first, kv.second.first, kv.second.second);
                            std::fprintf(stderr, "\n");
                        }
                    }
                    auto &e = byReg[viS];
                    if (v == 0u) ++e.second; else ++e.first;
                    if (v == 0u)
                    {
                        // Is a write to this register still sitting in the
                        // pipeline when XGKICK reads it?
                        static unsigned long long zeroWithPending = 0, zeroNoPending = 0;
                        bool pending = false;
                        int32_t pendVal = 0;
                        for (const auto &w : m_viWritePipeline)
                            if (w.valid && w.reg == viS) { pending = true; pendVal = w.value; break; }
                        if (pending) ++zeroWithPending; else ++zeroNoPending;
                        static unsigned long long m = 0;
                        if ((++m % 2000ull) == 0ull)
                            std::fprintf(stderr,
                                "[vu1] XGKICK reads 0: pendingWrite=%llu noPending=%llu lastPendVal=%d\n",
                                zeroWithPending, zeroNoPending, (int)pendVal);
                    }
                    static unsigned long long n = 0;
                    if ((++n % 4000ull) == 0ull)
                    {
                        std::fprintf(stderr, "[vu1] XGKICK by VI reg (nonzero/zero), pc=0x%x:", (unsigned)m_state.pc);
                        for (const auto &kv : byReg)
                            std::fprintf(stderr, " vi%u=%llu/%llu", kv.first, kv.second.first, kv.second.second);
                        std::fprintf(stderr, "\n");
                    }
                }
#endif
                startXgkick(static_cast<uint32_t>(static_cast<uint16_t>(m_state.vi[viS])));
                return;
            case 0x70: // ESADD
            {
                const float x = normalizeOperand(m_state.vf[vfS][0]);
                const float y = normalizeOperand(m_state.vf[vfS][1]);
                const float z = normalizeOperand(m_state.vf[vfS][2]);
                queueP(x * x + y * y + z * z, 11u);
                return;
            }
            case 0x71: // ERSADD
            {
                const float x = normalizeOperand(m_state.vf[vfS][0]);
                const float y = normalizeOperand(m_state.vf[vfS][1]);
                const float z = normalizeOperand(m_state.vf[vfS][2]);
                const float sum = x * x + y * y + z * z;
                queueP(sum != 0.0f ? 1.0f / sum : sum, 18u);
                return;
            }
            case 0x72: // ELENG
            {
                const float x = normalizeOperand(m_state.vf[vfS][0]);
                const float y = normalizeOperand(m_state.vf[vfS][1]);
                const float z = normalizeOperand(m_state.vf[vfS][2]);
                queueP(std::sqrt(x * x + y * y + z * z), 18u);
                return;
            }
            case 0x73: // ERLENG
            {
                const float x = normalizeOperand(m_state.vf[vfS][0]);
                const float y = normalizeOperand(m_state.vf[vfS][1]);
                const float z = normalizeOperand(m_state.vf[vfS][2]);
                const float len = std::sqrt(x * x + y * y + z * z);
                queueP(len != 0.0f ? 1.0f / len : len, 24u);
                return;
            }
            case 0x74: // EATANxy
            {
                const float x = normalizeOperand(m_state.vf[vfS][0]);
                const float y = normalizeOperand(m_state.vf[vfS][1]);
                queueP(x != 0.0f ? vuEatan(y / x) : 0.0f, 54u);
                return;
            }
            case 0x75: // EATANxz
            {
                const float x = normalizeOperand(m_state.vf[vfS][0]);
                const float z = normalizeOperand(m_state.vf[vfS][2]);
                queueP(x != 0.0f ? vuEatan(z / x) : 0.0f, 54u);
                return;
            }
            case 0x76: // ESUM
            {
                float sum = 0.0f;
                for (uint32_t component = 0; component < 4u; ++component)
                    sum += normalizeOperand(m_state.vf[vfS][component]);
                queueP(sum, 12u);
                return;
            }
            case 0x77: // ERSQRT
            {
                const uint32_t component = (instr >> 21) & 3u;
                const float value = normalizeOperand(m_state.vf[vfS][component]);
                float result = value;
                if (result >= 0.0f)
                {
                    result = std::sqrt(result);
                    if (result != 0.0f)
                        result = 1.0f / result;
                }
                queueP(result, 18u);
                return;
            }
            case 0x78: // ESQRT
            {
                const uint32_t component = (instr >> 21) & 3u;
                const float value = normalizeOperand(m_state.vf[vfS][component]);
                queueP(value >= 0.0f ? std::sqrt(value) : value, 12u);
                return;
            }
            case 0x79: // ESIN
            {
                const uint32_t component = (instr >> 21) & 3u;
                const float value = normalizeOperand(m_state.vf[vfS][component]);
                queueP(vuEsin(value), 29u);
                return;
            }
            case 0x7A: // ERCPR
            {
                const uint32_t component = (instr >> 21) & 3u;
                const float value = normalizeOperand(m_state.vf[vfS][component]);
                queueP(value != 0.0f ? 1.0f / value : value, 12u);
                return;
            }
            case 0x7B: // WAITP
                return;
            case 0x7C: // EATAN
            {
                const uint32_t component = (instr >> 21) & 3u;
                queueP(vuEatan(normalizeOperand(m_state.vf[vfS][component])), 54u);
                return;
            }
            case 0x7D: // EEXP
            {
                const uint32_t component = (instr >> 21) & 3u;
                queueP(vuEexp(normalizeOperand(m_state.vf[vfS][component])), 44u);
                return;
            }
            default:
                reportReservedInstruction(false, instr);
                return;
            }
        }
        default:
            reportReservedInstruction(false, instr);
            return;
        }
    }
    default:
        reportReservedInstruction(false, instr);
        break;
    }
}
