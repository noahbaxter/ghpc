#include "ps2_runtime.h"
#include "ps2_log.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"
#include "game_overrides.h"
#include "ps2_runtime_macros.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/ghpc_native_draw.h"
#include "runtime/ee_scheduler.h"
#include "ThreadNaming.h"
#include "Kernel/Stubs/Audio.h"
#include "Kernel/Stubs/GS.h"
#include "Kernel/Stubs/MPEG.h"
#include "ps2_host_backend.h"
#include "ps2_iop_host.h"
#include "ps2x/iop/iop_subsystem.h"

#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <chrono>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <sstream>

namespace ps2_stubs
{
    void resetSifState();
}

#define ELF_MAGIC 0x464C457F // "\x7FELF" in little endian
#define ET_EXEC 2            // Executable file
#define EM_MIPS 8            // MIPS architecture
#define PT_LOAD 1            // Loadable segment

static constexpr int FB_WIDTH = 640;
static constexpr int FB_HEIGHT = 512;
static constexpr int DEFAULT_DISPLAY_HEIGHT = 448;
static constexpr uint32_t DEFAULT_FB_SIZE = FB_WIDTH * FB_HEIGHT * 4;
static constexpr uint32_t DEFAULT_FB_ADDR = (PS2_RAM_SIZE - DEFAULT_FB_SIZE - 0x10000u);
#if defined(PLATFORM_VITA)
static constexpr int HOST_WINDOW_WIDTH = 960;
static constexpr int HOST_WINDOW_HEIGHT = 544;
#else
static constexpr int HOST_WINDOW_WIDTH = FB_WIDTH;
static constexpr int HOST_WINDOW_HEIGHT = DEFAULT_DISPLAY_HEIGHT;
#endif
struct ElfHeader
{
    uint32_t magic;
    uint8_t elf_class;
    uint8_t endianness;
    uint8_t version;
    uint8_t os_abi;
    uint8_t abi_version;
    uint8_t padding[7];
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct ProgramHeader
{
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
};

namespace
{
    constexpr uint32_t kGuestHeapDefaultBase = 0x00100000u;
    constexpr uint32_t kGuestHeapDefaultAlignment = 16u;
    constexpr uint32_t kGuestHeapSafetyPad = 0x1000u;
    constexpr uint32_t kGuestHeapHardLimit = PS2_RAM_SIZE - 0x00100000u;

    // The runtime arena and the guest allocator have to live in separate
    // regions once the game runs real newlib malloc, because sbrk grows the
    // guest heap upward from the ELF's end with no idea the runtime is also
    // handing out addresses. Fixed block, so EndOfHeap can name the ceiling
    // during boot without waiting for the arena to be configured.
    // Sized off PS2_RAM_SIZE, so the split follows the map instead of pinning
    // itself to where the top of a 32MB one used to be. At 128MB:
    //   game    [ELF end .... 0x07D00000)   sbrk, ceiling = EndOfHeap
    //   runtime [0x07D00000 .. 0x07F00000)  guestMalloc only
    //   stacks  [0x07F00000 .. 0x08000000)  RPC/TLS pools, callback stacks
    constexpr uint32_t kRuntimeArenaBase = PS2_RAM_SIZE - 0x00300000u;

    // Guest memory the host layer keeps for itself. A PS2 game may take all
    // of RAM; a recompiled one may not, because the runtime needs guest
    // addressable memory the real console never had: GIF packets in GS.cpp,
    // the glyph and kerning tables in Font.cpp, MPEG callback data, IOP host
    // buffers, and a stack per guest thread in Thread.cpp. GH2 sizes its own
    // pools by asking for a huge block and halving until one fits, so without
    // a reserve it swallows the arena whole and the next allocation of any
    // size fails. Overridable with GHPC_HEAP_RESERVE for bisecting.
    constexpr uint32_t kGuestHeapRuntimeReserve = 0x00100000u;

    constexpr uint32_t COP0_CAUSE_EXCCODE_MASK = 0x0000007Cu;
    constexpr uint32_t COP0_CAUSE_BD = 0x80000000u;
    constexpr uint32_t COP0_STATUS_EXL = 0x00000002u;
    constexpr uint32_t COP0_STATUS_BEV = 0x00400000u;
    constexpr uint32_t EXCEPTION_VECTOR_GENERAL = 0x80000080u;
    constexpr uint32_t EXCEPTION_VECTOR_TLB_REFILL = 0x80000000u;
    constexpr uint32_t EXCEPTION_VECTOR_BOOT = 0xBFC00200u;

    struct DispatchHistory
    {
        std::array<uint32_t, 64> pcs{};
        uint32_t next = 0u;
        bool wrapped = false;
    };

    thread_local DispatchHistory g_dispatchHistory;

    bool computeFileCrc32(const std::string &path, uint32_t &crcOut)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        static const std::array<uint32_t, 256> table = []
        {
            std::array<uint32_t, 256> values{};
            for (uint32_t i = 0; i < values.size(); ++i)
            {
                uint32_t value = i;
                for (uint32_t bit = 0; bit < 8; ++bit)
                {
                    value = (value & 1u) ? (0xEDB88320u ^ (value >> 1u)) : (value >> 1u);
                }
                values[i] = value;
            }
            return values;
        }();

        uint32_t crc = 0xFFFFFFFFu;
        std::array<uint8_t, 16 * 1024> buffer{};
        while (file.good())
        {
            file.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = file.gcount();
            for (std::streamsize i = 0; i < count; ++i)
            {
                crc = table[(crc ^ buffer[static_cast<size_t>(i)]) & 0xFFu] ^ (crc >> 8u);
            }
        }
        if (file.bad())
        {
            return false;
        }
        crcOut = ~crc;
        return true;
    }

    void pushDispatchPc(uint32_t pc)
    {
        DispatchHistory &h = g_dispatchHistory;
        h.pcs[h.next] = pc;
        h.next = (h.next + 1u) % static_cast<uint32_t>(h.pcs.size());
        if (h.next == 0u)
        {
            h.wrapped = true;
        }
    }

    std::string formatDispatchHistory()
    {
        const DispatchHistory &h = g_dispatchHistory;
        const uint32_t count = h.wrapped ? static_cast<uint32_t>(h.pcs.size()) : h.next;
        if (count == 0u)
        {
            return "(empty)";
        }

        std::ostringstream oss;
        bool first = true;
        for (uint32_t i = 0u; i < count; ++i)
        {
            const uint32_t idx = (h.next + h.pcs.size() - count + i) % static_cast<uint32_t>(h.pcs.size());
            if (!first)
            {
                oss << " -> ";
            }
            first = false;
            oss << "0x" << std::hex << h.pcs[idx];
        }
        return oss.str();
    }

    uint32_t selectExceptionVector(const R5900Context *ctx, bool tlbRefill)
    {
        if (ctx->cop0_status & COP0_STATUS_BEV)
        {
            return EXCEPTION_VECTOR_BOOT;
        }
        return tlbRefill ? EXCEPTION_VECTOR_TLB_REFILL : EXCEPTION_VECTOR_GENERAL;
    }

    void seedVu0IdleSuccess(R5900Context *ctx)
    {
        if (!ctx)
        {
            return;
        }

        ctx->vu0_clip_flags = 0;
        ctx->vu0_clip_flags2 = 0;
        ctx->vu0_mac_flags = 0;
        ctx->vu0_status = 0;
        ctx->vu0_q = 1.0f;
        ctx->vu0_r = _mm_castsi128_ps(_mm_set1_epi32(0x3F800000));
        ctx->vu0_vpu_stat = 0;
        ctx->vu0_vpu_stat2 = 0;
    }

    void copyVu0ContextToState(const R5900Context *ctx, VU1State &state)
    {
        std::memset(&state, 0, sizeof(state));

        for (uint32_t i = 0; i < 32u; ++i)
        {
            _mm_storeu_ps(state.vf[i], ctx->vu0_vf[i]);
        }
        for (uint32_t i = 0; i < 16u; ++i)
        {
            state.vi[i] = static_cast<int16_t>(ctx->vi[i]);
        }

        _mm_storeu_ps(state.acc, ctx->vu0_acc);
        state.q = ctx->vu0_q;
        state.p = ctx->vu0_p;
        state.i = ctx->vu0_i;
        alignas(16) uint32_t rWords[4]{};
        _mm_storeu_si128(reinterpret_cast<__m128i *>(rWords), _mm_castps_si128(ctx->vu0_r));
        state.r = 0x3F800000u | (rWords[0] & 0x007FFFFFu);
        state.pc = ctx->vu0_pc;
        state.mac = ctx->vu0_mac_flags;
        state.clip = ctx->vu0_clip_flags;
        state.status = ctx->vu0_status;
        state.itop = ctx->vu0_itop;
        state.dBitEnabled = (ctx->vu0_fbrst & (1u << 2)) != 0u;
        state.tBitEnabled = (ctx->vu0_fbrst & (1u << 3)) != 0u;

        state.vf[0][0] = 0.0f;
        state.vf[0][1] = 0.0f;
        state.vf[0][2] = 0.0f;
        state.vf[0][3] = 1.0f;
        state.vi[0] = 0;
    }

    void copyVu0StateToContext(const VU1State &state, R5900Context *ctx)
    {
        for (uint32_t i = 0; i < 32u; ++i)
        {
            ctx->vu0_vf[i] = _mm_loadu_ps(state.vf[i]);
        }
        for (uint32_t i = 0; i < 16u; ++i)
        {
            ctx->vi[i] = static_cast<uint16_t>(state.vi[i]);
        }

        ctx->vu0_acc = _mm_loadu_ps(state.acc);
        ctx->vu0_q = state.q;
        ctx->vu0_p = state.p;
        ctx->vu0_i = state.i;
        ctx->vu0_r = _mm_castsi128_ps(_mm_set1_epi32(static_cast<int32_t>(state.r)));
        ctx->vu0_mac_flags = state.mac;
        ctx->vu0_clip_flags = state.clip;
        ctx->vu0_clip_flags2 = state.clip;
        ctx->vu0_status = static_cast<uint16_t>(state.status);
        ctx->vu0_itop = state.itop;
        ctx->vu0_pc = state.pc;
        ctx->vu0_tpc = state.pc;
        ctx->vu0_vpu_stat = (ctx->vu0_vpu_stat & 0xFF00u) | (state.stoppedByD ? (1u << 1) : 0u) | (state.stoppedByT ? (1u << 2) : 0u);
        ctx->vu0_vpu_stat2 = 0;

        ctx->vu0_vf[0] = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f);
        ctx->vi[0] = 0;
    }

    void raiseCop0Exception(R5900Context *ctx, uint32_t exceptionCode, bool tlbRefill = false)
    {
        if (ctx->in_delay_slot)
        {
            ctx->cop0_epc = ctx->branch_pc;
            ctx->cop0_cause = (ctx->cop0_cause & ~COP0_CAUSE_EXCCODE_MASK) |
                              ((exceptionCode << 2) & COP0_CAUSE_EXCCODE_MASK) |
                              COP0_CAUSE_BD;
        }
        else
        {
            ctx->cop0_epc = ctx->pc;
            ctx->cop0_cause = (ctx->cop0_cause & ~(COP0_CAUSE_EXCCODE_MASK | COP0_CAUSE_BD)) |
                              ((exceptionCode << 2) & COP0_CAUSE_EXCCODE_MASK);
        }

        ctx->cop0_status |= COP0_STATUS_EXL;
        ctx->pc = selectExceptionVector(ctx, tlbRefill);
        ctx->in_delay_slot = false;
    }

    std::filesystem::path normalizeAbsolutePath(const std::filesystem::path &path)
    {
        if (path.empty())
        {
            return {};
        }

#if defined(PLATFORM_VITA)
        const std::string generic = path.generic_string();
        const std::size_t colon = generic.find(':');
        if (colon != std::string::npos && colon != 0u)
        {
            const std::size_t slash = generic.find_first_of("/\\");
            if (slash == std::string::npos || colon < slash)
            {
                return path.lexically_normal();
            }
        }
#endif

        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        if (ec)
        {
            return path.lexically_normal();
        }
        return absolute.lexically_normal();
    }

    PS2Runtime::IoPaths &runtimeIoPaths()
    {
        static PS2Runtime::IoPaths paths = []()
        {
            PS2Runtime::IoPaths defaults;
            std::error_code ec;
            const std::filesystem::path cwd = std::filesystem::current_path(ec);
            defaults.elfDirectory = ec ? std::filesystem::path(".") : cwd.lexically_normal();
            defaults.hostRoot = defaults.elfDirectory;
            defaults.cdRoot = defaults.elfDirectory;
            defaults.mcRoot = defaults.elfDirectory / "mc0";
            return defaults;
        }();

        return paths;
    }

    std::string readGuestPrintableString(const uint8_t *rdram, uint32_t addr, size_t maxLen)
    {
        std::string out;
        if (!rdram || maxLen == 0)
        {
            return out;
        }

        out.reserve(std::min<size_t>(maxLen, 64));
        for (size_t i = 0; i < maxLen; ++i)
        {
            const char ch = static_cast<char>(rdram[(addr + static_cast<uint32_t>(i)) & PS2_RAM_MASK]);
            if (ch == '\0')
            {
                break;
            }
            if (ch >= 0x20 && ch < 0x7F)
            {
                out.push_back(ch);
            }
            else
            {
                out.push_back('.');
            }
        }
        return out;
    }
}

// GHPCSTART: has the chart actually started, as opposed to a screen appearing.
//
// GamePanel::StartGame (0x1070f8) is the single instant that separates the
// count-in from gameplay: it is what SetRealtime(false) runs from, and nothing
// else calls it. Latching it here gives the frame dumper a gate, which is the
// difference between twenty pictures of the boot sequence and one picture of
// the game. The dumper caps at 20 and had always spent all of them before
// game_screen, which is why no picture of this game in gameplay existed.
std::atomic<bool> g_ghpcGameStarted{false};

static void UploadFrame(Texture2D &tex, PS2Runtime *rt, uint32_t &outWidth, uint32_t &outHeight)
{
    static uint64_t s_lastPresentationTick = std::numeric_limits<uint64_t>::max();
    static bool s_hasLatchedInitialFrame = false;
    static uint32_t s_lastDisplayFbp = std::numeric_limits<uint32_t>::max();
    static uint32_t s_lastSourceFbp = std::numeric_limits<uint32_t>::max();
    static bool s_lastPreferred = false;
    static uint32_t s_lastWidth = 0u;
    static uint32_t s_lastHeight = 0u;
    static bool s_hasUploadedFrame = false;
    static std::vector<uint8_t> s_scratch;
    static std::vector<uint8_t> s_uploadBuffer(DEFAULT_FB_SIZE, 0u);

    const uint64_t currentTick = rt->eeScheduler().currentVSyncTick();
    const bool needsLatch = !s_hasLatchedInitialFrame || currentTick != s_lastPresentationTick;
    if (needsLatch)
    {
        rt->gs().latchHostPresentationFrame();
        s_lastPresentationTick = currentTick;
        s_hasLatchedInitialFrame = true;
    }
    else if (s_hasUploadedFrame)
    {
        outWidth = (s_lastWidth != 0u) ? s_lastWidth : FB_WIDTH;
        outHeight = (s_lastHeight != 0u) ? s_lastHeight : DEFAULT_DISPLAY_HEIGHT;
        return;
    }

    s_scratch.clear();
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t displayFbp = 0u;
    uint32_t sourceFbp = 0u;
    bool usedPreferredDisplaySource = false;
    if (!rt->gs().copyLatchedHostPresentationFrame(s_scratch,
                                                   width,
                                                   height,
                                                   &displayFbp,
                                                   &sourceFbp,
                                                   &usedPreferredDisplaySource))
    {
        Image blank = GenImageColor(FB_WIDTH, FB_HEIGHT, MAGENTA);
        UpdateTexture(tex, blank.data);
        UnloadImage(blank);
        outWidth = FB_WIDTH;
        outHeight = DEFAULT_DISPLAY_HEIGHT;
        s_lastWidth = outWidth;
        s_lastHeight = outHeight;
        s_hasUploadedFrame = true;
        return;
    }

    PS2_IF_AGRESSIVE_LOGS({
        static uint32_t s_uploadDebugCount = 0u;
        if (s_uploadDebugCount < 128u ||
            displayFbp != s_lastDisplayFbp ||
            sourceFbp != s_lastSourceFbp ||
            usedPreferredDisplaySource != s_lastPreferred ||
            width != s_lastWidth ||
            height != s_lastHeight)
        {
            std::cout << "[frame:upload] idx=" << s_uploadDebugCount
                      << " tick=" << currentTick
                      << " displayFbp=" << displayFbp
                      << " sourceFbp=" << sourceFbp
                      << " size=" << width << "x" << height
                      << " preferred=" << static_cast<uint32_t>(usedPreferredDisplaySource ? 1u : 0u)
                      << std::endl;
        }
        ++s_uploadDebugCount;
    });
#if GHPC_DIAG
    {
        static uint32_t contentLogs = 0u;
        static size_t lastNonBlack = SIZE_MAX;
        size_t nonBlack = 0u;
        size_t distinct = 0u;
        uint32_t firstColor = 0u;
        for (size_t i = 0; i + 4 <= s_scratch.size(); i += 4)
        {
            const uint32_t px = (uint32_t)s_scratch[i] | ((uint32_t)s_scratch[i+1] << 8) |
                                ((uint32_t)s_scratch[i+2] << 16);
            if (px != 0u)
            {
                if (nonBlack == 0u) firstColor = px;
                else if (px != firstColor) ++distinct;
                ++nonBlack;
            }
        }
        // Dump the first few frames that actually contain image content, so there
        // is a real picture to look at rather than just a pixel count.
        // The loading screen is the dense one (~211k non-black of 229376); the
        // splashes are far sparser. GHPC_FRAME_MIN aims the burst at it without
        // having to guess a frame index.
        static const size_t frameMin = []() -> size_t {
            const char *e = std::getenv("GHPC_FRAME_MIN");
            return e ? (size_t)std::strtoull(e, nullptr, 0) : 1000u;
        }();
        // GHPC_FRAME_AFTER_START holds every dump until StartGame has run, so
        // the budget lands on gameplay instead of on the splashes. It decides
        // whether a file is written and touches no guest state.
        static const bool afterStart = std::getenv("GHPC_FRAME_AFTER_START") != nullptr;
        if (nonBlack > frameMin && (!afterStart || g_ghpcGameStarted.load(std::memory_order_relaxed)))
        {
            static int dumps = 0;
            static auto lastDump = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            const auto nowDump = std::chrono::steady_clock::now();
            // A 2 second gap between dumps cannot show flicker: consecutive
            // presents are exactly what has to be compared. GHPC_FRAME_BURST=N
            // dumps N presents back to back instead, and skips the first
            // GHPC_FRAME_SKIP content frames so the burst lands on the screen
            // being studied rather than on the splash.
            static const int burst = []() {
                const char *e = std::getenv("GHPC_FRAME_BURST");
                return e ? std::atoi(e) : 0;
            }();
            static const int skip = []() {
                const char *e = std::getenv("GHPC_FRAME_SKIP");
                return e ? std::atoi(e) : 0;
            }();
            static int contentFrames = 0;
            ++contentFrames;
            const int limit = burst > 0 ? burst : 20;
            // GHPC_FRAME_ONCHANGE dumps only when the picture differs from the
            // last dumped one. A static screen then costs a single frame, and
            // the moment it starts alternating every state gets captured.
            static const bool onChange = std::getenv("GHPC_FRAME_ONCHANGE") != nullptr;
            static unsigned long long lastDumpSig = 0ull;
            unsigned long long sig = 1469598103934665603ull;
            if (onChange)
            {
                for (size_t i = 0; i + 4 <= s_scratch.size(); i += 4)
                {
                    const uint32_t p0 = (uint32_t)s_scratch[i] | ((uint32_t)s_scratch[i+1] << 8) |
                                        ((uint32_t)s_scratch[i+2] << 16);
                    if (p0 != 0u) { sig = (sig ^ p0) * 1099511628211ull; sig ^= (unsigned long long)i; }
                }
            }
            const bool dueDump = onChange ? (sig != lastDumpSig)
                               : burst > 0 ? (contentFrames > skip)
                                           : (std::chrono::duration<double>(nowDump - lastDump).count() >= 2.0);
            if (onChange) lastDumpSig = sig;
            if (dumps < limit && dueDump)
            {
                lastDump = nowDump;
                char path[256];
                std::snprintf(path, sizeof(path), "/tmp/ghpc_frame_%d.ppm", dumps);
                if (FILE *f = std::fopen(path, "wb"))
                {
                    std::fprintf(f, "P6\n%u %u\n255\n", width, height);
                    for (uint32_t y = 0; y < height; ++y)
                        for (uint32_t x = 0; x < width; ++x)
                        {
                            const size_t i = ((size_t)y * width + x) * 4u;
                            unsigned char rgb[3] = {0,0,0};
                            if (i + 3 <= s_scratch.size())
                            { rgb[0]=s_scratch[i]; rgb[1]=s_scratch[i+1]; rgb[2]=s_scratch[i+2]; }
                            std::fwrite(rgb, 1, 3, f);
                        }
                    std::fclose(f);
                    std::cerr << "[frame] wrote " << path << " nonBlack=" << nonBlack
                              << " displayFbp=" << displayFbp
                              << " sourceFbp=" << sourceFbp
                              << " preferred=" << (usedPreferredDisplaySource ? 1u : 0u)
                              << " contentFrame=" << contentFrames << std::endl;
                    ++dumps;
                }
            }
        }
        if (contentLogs < 8u || nonBlack != lastNonBlack)
        {
            ++contentLogs;
            lastNonBlack = nonBlack;
            std::cerr << "[frame] " << width << "x" << height
                      << " nonBlack=" << nonBlack << "/" << (s_scratch.size()/4)
                      << " firstColor=0x" << std::hex << firstColor << std::dec
                      << " otherColors=" << distinct << std::endl;
        }
    }
#endif
    // Frame pacing, available in every build. GHPC_FPS=N reports every N frames.
    {
        static const int fpsEvery = []() {
            const char *e = std::getenv("GHPC_FPS");
            return e ? std::atoi(e) : 0;
        }();
        if (fpsEvery > 0)
        {
            static auto last = std::chrono::steady_clock::now();
            static int frames = 0;
            if (++frames >= fpsEvery)
            {
                const auto now = std::chrono::steady_clock::now();
                const double secs = std::chrono::duration<double>(now - last).count();
                std::fprintf(stderr, "[fps] %.2f frames/sec  %.2f ms/frame  over %d frames\n",
                             frames / secs, (secs * 1000.0) / frames, frames);
                last = now;
                frames = 0;
            }
        }
    }

    s_lastDisplayFbp = displayFbp;
    s_lastSourceFbp = sourceFbp;
    s_lastPreferred = usedPreferredDisplaySource;
    s_lastWidth = width;
    s_lastHeight = height;

    std::fill(s_uploadBuffer.begin(), s_uploadBuffer.end(), 0u);
    if (!s_scratch.empty() && width != 0u && height != 0u)
    {
        const uint32_t copyWidth = std::min<uint32_t>(width, FB_WIDTH);
        const uint32_t copyHeight = std::min<uint32_t>(height, FB_HEIGHT);
        const size_t srcRowBytes = static_cast<size_t>(width) * 4u;
        const size_t dstRowBytes = static_cast<size_t>(FB_WIDTH) * 4u;
        const size_t copyRowBytes = static_cast<size_t>(copyWidth) * 4u;
        for (uint32_t y = 0; y < copyHeight; ++y)
        {
            const size_t srcOffset = static_cast<size_t>(y) * srcRowBytes;
            const size_t dstOffset = static_cast<size_t>(y) * dstRowBytes;
            if (srcOffset + copyRowBytes > s_scratch.size() ||
                dstOffset + copyRowBytes > s_uploadBuffer.size())
            {
                break;
            }
            std::memcpy(s_uploadBuffer.data() + dstOffset, s_scratch.data() + srcOffset, copyRowBytes);
        }
    }

    UpdateTexture(tex, s_uploadBuffer.data());
    outWidth = width;
    outHeight = height;
    s_hasUploadedFrame = true;
}

PS2Runtime::PS2Runtime()
{
    m_iopHost = std::make_unique<PS2IopHostAdapter>(*this);
    m_iopSubsystem = std::make_unique<ps2x::iop::IopSubsystem>(*m_iopHost);
    m_eeScheduler = std::make_unique<EeScheduler>(*this);
#if defined(PS2X_IOP_ENABLE_PLUGINS) && PS2X_IOP_ENABLE_PLUGINS && \
    !defined(PLATFORM_VITA) && (defined(_WIN32) || defined(__linux__))
    if (const char *applicationDirectory = GetApplicationDirectory();
        applicationDirectory && applicationDirectory[0] != '\0')
    {
        m_iopSubsystem->setPluginSearchPaths({std::filesystem::path(applicationDirectory) / "iop_plugins"});
    }
#endif

    // Assign rather than memset: R5900Context's constructor zeroes itself and
    // then applies the COP0 reset values, which a memset here would discard.
    m_cpuContext = R5900Context{};

    // R0 is always zero in MIPS
    m_cpuContext.r[0] = _mm_set1_epi32(0);
    m_cpuContext.vu0_vf[0] = _mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f);
    m_cpuContext.vu0_q = 1.0f;
    m_cpuContext.vu0_r = _mm_castsi128_ps(_mm_set1_epi32(0x3F800000));

    // Stack pointer (SP) and global pointer (GP) will be set by the loaded ELF

    m_loadedModules.clear();
    m_guestHeapBlocks.clear();
    m_guestHeapBase = kRuntimeArenaBase;
    m_guestHeapEnd = kRuntimeArenaBase;
    m_guestHeapLimit = std::min(kGuestHeapHardLimit, PS2_RAM_SIZE);
    m_guestHeapSuggestedBase = kRuntimeArenaBase;
    m_guestHeapConfigured = false;
    m_asyncCallbackStackFloor = std::min(kGuestHeapHardLimit, PS2_RAM_SIZE);
    m_asyncCallbackStackTop = PS2_RAM_SIZE;
}

void PS2Runtime::setDebugUiCallbacks(DebugUiCallback initCallback,
                                     DebugUiCallback drawCallback,
                                     DebugUiCallback shutdownCallback,
                                     void *userData)
{
    if (m_debugUiInitialized && m_debugUiShutdownCallback)
    {
        m_debugUiShutdownCallback(*this, m_debugUiUserData);
        m_debugUiInitialized = false;
    }

    m_debugUiInitCallback = initCallback;
    m_debugUiDrawCallback = drawCallback;
    m_debugUiShutdownCallback = shutdownCallback;
    m_debugUiUserData = userData;
}

PS2Runtime::~PS2Runtime()
{
    try
    {
        requestStop();
        m_iopSubsystem.reset();
        m_iopHost.reset();
#if defined(PLATFORM_VITA)
        m_audioBackend.stopAll();
        m_audioBackend.setAudioReady(false);
#else
        if (IsAudioDeviceReady())
        {
            CloseAudioDevice();
            m_audioBackend.setAudioReady(false);
        }
#endif
        if (m_debugUiInitialized && m_debugUiShutdownCallback)
        {
            m_debugUiShutdownCallback(*this, m_debugUiUserData);
            m_debugUiInitialized = false;
        }

        if (IsWindowReady())
        {
            CloseWindow();
        }

        m_loadedModules.clear();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[~PS2Runtime] cleanup exception: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[~PS2Runtime] cleanup exception: unknown" << std::endl;
    }
}

void PS2Runtime::setIopPluginSearchPaths(std::vector<std::filesystem::path> paths)
{
    m_iopSubsystem->setPluginSearchPaths(std::move(paths));
}

ps2x::iop::RpcAbi PS2Runtime::selectIopRpcAbi(const ps2x::iop::RpcAbiRequest &request) const
{
    return m_iopSubsystem->selectRpcAbi(request);
}

ps2x::iop::RpcResult PS2Runtime::handleIopRpc(uint8_t *rdram, R5900Context *ctx, ps2x::iop::RpcRequest request)
{
    auto scope = m_iopHost->enterCall(ctx, rdram);
    request.callToken = scope.token();
    return m_iopSubsystem->handleRpc(request);
}

void PS2Runtime::notifyIopSifTransfer(uint8_t *rdram, const ps2x::iop::SifTransfer &transfer)
{
    auto scope = m_iopHost->enterCall(nullptr, rdram);
    m_iopSubsystem->onSifTransfer(transfer);
}

void PS2Runtime::resetIop()
{
    m_iopSubsystem->reset();
}

ps2x::iop::DebugSnapshot PS2Runtime::iopDebugSnapshot() const
{
    return m_iopSubsystem->debugSnapshot();
}

bool PS2Runtime::syncCoreSubsystems()
{
    uint8_t *const rdram = m_memory.getRDRAM();
    uint8_t *const gsVram = m_memory.getGSVRAM();
    if (!rdram || !gsVram)
    {
        return false;
    }

    if (m_boundRdram == rdram && m_boundGSVram == gsVram)
    {
        return true;
    }

    m_gs.init(gsVram, static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &m_memory.gs());
    m_gifArbiter.setProcessPacketFn([this](const uint8_t *data, uint32_t size)
                                    { m_gs.processGIFPacket(data, size); });
    m_memory.setGifArbiter(&m_gifArbiter);
    m_memory.setVu1MscalCallback([this](uint32_t startPC, uint32_t top, uint32_t itop)
                                 {
                                     R5900Context *cpuContext = m_eeScheduler ? m_eeScheduler->currentContext() : nullptr;
                                     if (!cpuContext)
                                     {
                                         cpuContext = &m_cpuContext;
                                     }
                                     m_vu1.state().dBitEnabled =
                                         (cpuContext->vu0_fbrst & (1u << 10)) != 0u;
                                     m_vu1.state().tBitEnabled =
                                         (cpuContext->vu0_fbrst & (1u << 11)) != 0u;
                                     // The 65536 was a literal here, and the VU1
                                     // census shows a third of GH2's microprograms
                                     // hitting it exactly. That leaves two readings
                                     // apart: an infinite loop, or a budget too small
                                     // for honest work. Raising it separates them,
                                     // because a real runaway does not start
                                     // terminating when handed more rope.
                                     static const uint32_t s_vu1Budget = []() -> uint32_t {
                                         const char *e = std::getenv("GHPC_VU1_BUDGET");
                                         return e ? (uint32_t)std::strtoul(e, nullptr, 0) : 65536u;
                                     }();
                                     // The Rnd seam. When PsMesh::DrawFaces has
                                     // armed a mesh the host cache holds, the
                                     // transform runs here instead of the
                                     // microprogram. Every register this draw
                                     // needs is already latched: they went out as
                                     // GIF A+D through VIF1 DIRECT ahead of this
                                     // MSCAL. Off unless GHPC_NATIVE_DRAW is set.
                                     if (ghpcNativeDrawMscal(m_gs))
                                     {
                                         // No microprogram ran, so neither stop
                                         // bit can be set. Clear them rather than
                                         // leaving the previous MSCAL's.
                                         cpuContext->vu0_vpu_stat &= ~0x0600u;
                                         return;
                                     }
                                     m_vu1.execute(m_memory.getVU1Code(), PS2_VU1_CODE_SIZE,
                                                   m_memory.getVU1Data(), PS2_VU1_DATA_SIZE,
                                                   m_gs, &m_memory, startPC, top, itop, s_vu1Budget);
                                     cpuContext->vu0_vpu_stat =
                                         (cpuContext->vu0_vpu_stat & ~0x0600u) |
                                         (m_vu1.state().stoppedByD ? 0x0200u : 0u) |
                                         (m_vu1.state().stoppedByT ? 0x0400u : 0u); });
    m_memory.setVu1MscntCallback([this](uint32_t top, uint32_t itop)
                                 {
                                     R5900Context *cpuContext = m_eeScheduler ? m_eeScheduler->currentContext() : nullptr;
                                     if (!cpuContext)
                                     {
                                         cpuContext = &m_cpuContext;
                                     }
                                     m_vu1.state().dBitEnabled =
                                         (cpuContext->vu0_fbrst & (1u << 10)) != 0u;
                                     m_vu1.state().tBitEnabled =
                                         (cpuContext->vu0_fbrst & (1u << 11)) != 0u;
                                     m_vu1.resume(m_memory.getVU1Code(), PS2_VU1_CODE_SIZE,
                                                  m_memory.getVU1Data(), PS2_VU1_DATA_SIZE,
                                                  m_gs, &m_memory, top, itop, 65536);
                                     cpuContext->vu0_vpu_stat =
                                         (cpuContext->vu0_vpu_stat & ~0x0600u) |
                                         (m_vu1.state().stoppedByD ? 0x0200u : 0u) |
                                         (m_vu1.state().stoppedByT ? 0x0400u : 0u); });
    resetIop();
    m_vu0.reset();
    m_vu1.reset();

    m_boundRdram = rdram;
    m_boundGSVram = gsVram;
    return true;
}

bool PS2Runtime::initialize(const char *title)
{
    try
    {
        if (!m_memory.initialize())
        {
            std::cerr << "Failed to initialize PS2 memory" << std::endl;
            return false;
        }

        if (!syncCoreSubsystems())
        {
            std::cerr << "Failed to bind runtime core subsystems" << std::endl;
            return false;
        }
#if defined(PS2X_IOP_ENABLE_PLUGINS) && PS2X_IOP_ENABLE_PLUGINS && \
    !defined(PLATFORM_VITA) && (defined(_WIN32) || defined(__linux__))
        std::string pluginError;
        if (!m_iopSubsystem->loadPlugins(&pluginError))
        {
            std::cerr << "Failed to load IOP plugins: " << pluginError << std::endl;
            return false;
        }
#endif
#if defined(PLATFORM_VITA)
        InitWindow(HOST_WINDOW_WIDTH, HOST_WINDOW_HEIGHT, title); // raylib vita does not support audio
#else
        unsigned int windowFlags = FLAG_WINDOW_RESIZABLE;
        // GHPC_NO_FOCUS keeps keyboard focus on the terminal. It does NOT stop
        // the raise: GLFW's cocoa show path calls orderFront unconditionally and
        // offers no hint to suppress it, so a visible window always stacks on top.
        // GHPC_HIDE_WINDOW is the only way to keep an unattended run off screen.
        if (std::getenv("GHPC_NO_FOCUS") != nullptr)
            windowFlags |= FLAG_WINDOW_UNFOCUSED;
        if (std::getenv("GHPC_HIDE_WINDOW") != nullptr)
            windowFlags |= FLAG_WINDOW_HIDDEN;
        SetConfigFlags(windowFlags);
        InitWindow(HOST_WINDOW_WIDTH, HOST_WINDOW_HEIGHT, title);
        InitAudioDevice();
        m_audioBackend.setAudioReady(IsAudioDeviceReady());
#endif
        SetTargetFPS(60);
        if (m_debugUiInitCallback)
        {
            m_debugUiInitCallback(*this, m_debugUiUserData);
            m_debugUiInitialized = true;
        }

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to initialize PS2 runtime: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Failed to initialize PS2 runtime: unknown exception" << std::endl;
    }

    return false;
}

bool PS2Runtime::loadELF(const std::string &elfPath)
{
    configureIoPathsFromElf(elfPath);

    std::ifstream file(elfPath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Failed to open ELF file: " << elfPath << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    if (fileSize < static_cast<std::streamoff>(sizeof(ElfHeader)))
    {
        std::cerr << "ELF file is too small: " << elfPath << std::endl;
        return false;
    }
    file.seekg(0, std::ios::beg);

    ElfHeader header{};
    if (!file.read(reinterpret_cast<char *>(&header), sizeof(header)))
    {
        std::cerr << "Failed to read ELF header from: " << elfPath << std::endl;
        return false;
    }

    if (header.magic != ELF_MAGIC)
    {
        std::cerr << "Invalid ELF magic number" << std::endl;
        return false;
    }

    if (header.elf_class != 1u || header.endianness != 1u)
    {
        std::cerr << "Unsupported ELF format (expected 32-bit little-endian)." << std::endl;
        return false;
    }

    if (header.machine != EM_MIPS || header.type != ET_EXEC)
    {
        std::cerr << "Not a MIPS executable ELF file" << std::endl;
        return false;
    }

    if (header.phnum != 0u && header.phentsize < sizeof(ProgramHeader))
    {
        std::cerr << "Unsupported ELF program-header entry size: " << header.phentsize << std::endl;
        return false;
    }

    const uint64_t programHeaderTableEnd =
        static_cast<uint64_t>(header.phoff) +
        static_cast<uint64_t>(header.phnum) * static_cast<uint64_t>(header.phentsize);
    if (programHeaderTableEnd > static_cast<uint64_t>(fileSize))
    {
        std::cerr << "ELF program-header table is out of range." << std::endl;
        return false;
    }

    m_cpuContext.pc = header.entry;
    m_debugPc.store(m_cpuContext.pc, std::memory_order_relaxed);

    uint32_t maxLoadedRdramEnd = kGuestHeapDefaultBase;
    uint32_t moduleBase = std::numeric_limits<uint32_t>::max();
    uint32_t moduleEnd = 0u;
    bool loadedAnySegment = false;

    for (uint16_t i = 0; i < header.phnum; i++)
    {
        const uint64_t phOffset =
            static_cast<uint64_t>(header.phoff) +
            static_cast<uint64_t>(i) * static_cast<uint64_t>(header.phentsize);
        if (phOffset + sizeof(ProgramHeader) > static_cast<uint64_t>(fileSize))
        {
            std::cerr << "ELF program header " << i << " is out of range." << std::endl;
            return false;
        }

        ProgramHeader ph{};
        file.seekg(static_cast<std::streamoff>(phOffset), std::ios::beg);
        if (!file.read(reinterpret_cast<char *>(&ph), sizeof(ph)))
        {
            std::cerr << "Failed to read ELF program header " << i << std::endl;
            return false;
        }

        if (ph.type != PT_LOAD || ph.memsz == 0u)
        {
            continue;
        }

        if (ph.filesz > ph.memsz)
        {
            std::cerr << "ELF segment " << i << " has filesz > memsz." << std::endl;
            return false;
        }

        const uint64_t segmentFileEnd = static_cast<uint64_t>(ph.offset) + static_cast<uint64_t>(ph.filesz);
        if (segmentFileEnd > static_cast<uint64_t>(fileSize))
        {
            std::cerr << "ELF segment " << i << " exceeds file bounds." << std::endl;
            return false;
        }

        const bool scratch =
            ph.vaddr >= PS2_SCRATCHPAD_BASE &&
            ph.vaddr < (PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE);

        uint32_t physAddr = 0u;
        try
        {
            physAddr = m_memory.translateAddress(ph.vaddr);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to translate ELF segment " << i
                      << " virtual address 0x" << std::hex << ph.vaddr
                      << std::dec << ": " << e.what() << std::endl;
            return false;
        }
        const uint64_t regionSize = scratch ? static_cast<uint64_t>(PS2_SCRATCHPAD_SIZE)
                                            : static_cast<uint64_t>(PS2_RAM_SIZE);
        const uint64_t segmentMemEnd = static_cast<uint64_t>(physAddr) + static_cast<uint64_t>(ph.memsz);
        if (segmentMemEnd > regionSize)
        {
            std::cerr << "ELF segment " << i << " exceeds "
                      << (scratch ? "scratchpad" : "RDRAM")
                      << " bounds (vaddr=0x" << std::hex << ph.vaddr
                      << " memsz=0x" << ph.memsz << std::dec << ")." << std::endl;
            return false;
        }

        uint8_t *destBase = scratch ? m_memory.getScratchpad() : m_memory.getRDRAM();
        if (!destBase)
        {
            std::cerr << "ELF segment " << i << " has no destination memory backing." << std::endl;
            return false;
        }

        uint8_t *dest = destBase + physAddr;
        if (ph.filesz > 0u)
        {
            file.seekg(static_cast<std::streamoff>(ph.offset), std::ios::beg);
            if (!file.read(reinterpret_cast<char *>(dest), ph.filesz))
            {
                std::cerr << "Failed to read ELF segment " << i << " payload." << std::endl;
                return false;
            }
        }

        if (ph.memsz > ph.filesz)
        {
            std::memset(dest + ph.filesz, 0, ph.memsz - ph.filesz);
        }

        RUNTIME_LOG("Loading segment: 0x" << std::hex << ph.vaddr
                                          << " - 0x" << (static_cast<uint64_t>(ph.vaddr) + static_cast<uint64_t>(ph.memsz))
                                          << " (filesz: 0x" << ph.filesz
                                          << ", memsz: 0x" << ph.memsz << ")"
                                          << std::dec << std::endl);

        if (!scratch)
        {
            maxLoadedRdramEnd = std::max(maxLoadedRdramEnd, static_cast<uint32_t>(segmentMemEnd));
        }

        if (ph.flags & 0x1u) // PF_X
        {
            const uint64_t execEnd = static_cast<uint64_t>(ph.vaddr) + static_cast<uint64_t>(ph.filesz);
            if (execEnd <= std::numeric_limits<uint32_t>::max())
            {
                m_memory.registerCodeRegion(ph.vaddr, static_cast<uint32_t>(execEnd));
            }
        }

        loadedAnySegment = true;
        moduleBase = std::min(moduleBase, ph.vaddr);
        const uint64_t segmentVirtualEnd = static_cast<uint64_t>(ph.vaddr) + static_cast<uint64_t>(ph.memsz);
        const uint32_t clampedVirtualEnd =
            (segmentVirtualEnd > std::numeric_limits<uint32_t>::max())
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(segmentVirtualEnd);
        moduleEnd = std::max(moduleEnd, clampedVirtualEnd);
    }

    if (!loadedAnySegment)
    {
        std::cerr << "ELF contains no loadable PT_LOAD segments." << std::endl;
        return false;
    }

    if (maxLoadedRdramEnd > PS2_RAM_SIZE)
    {
        maxLoadedRdramEnd = PS2_RAM_SIZE;
    }

    const uint32_t paddedEnd = (maxLoadedRdramEnd > (PS2_RAM_SIZE - kGuestHeapSafetyPad))
                                   ? PS2_RAM_SIZE
                                   : (maxLoadedRdramEnd + kGuestHeapSafetyPad);
    const uint32_t suggestedHeapBase = alignGuestHeapValue(paddedEnd, kGuestHeapDefaultAlignment);
    {
        std::lock_guard<std::mutex> lock(m_guestHeapMutex);
        if (!m_guestHeapConfigured)
        {
            // The ELF derived base belongs to the guest allocator, not to us.
            // Anchoring the arena there puts it directly on top of the region
            // sbrk is about to grow into.
            const uint32_t hardLimit = std::min(kGuestHeapHardLimit, PS2_RAM_SIZE);
            m_guestHeapSuggestedBase = kRuntimeArenaBase;
            m_guestHeapBase = kRuntimeArenaBase;
            m_guestHeapEnd = kRuntimeArenaBase;
            m_guestHeapLimit = hardLimit;
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_asyncCallbackStackMutex);
        const uint32_t hardLimit = std::min(kGuestHeapHardLimit, PS2_RAM_SIZE);
        m_asyncCallbackStackFloor = std::min(std::max(hardLimit, suggestedHeapBase), PS2_RAM_SIZE);
        m_asyncCallbackStackTop = PS2_RAM_SIZE;
    }

    LoadedModule module;
    module.name = elfPath.substr(elfPath.find_last_of("/\\") + 1);
    module.baseAddress = (moduleBase == std::numeric_limits<uint32_t>::max()) ? 0x00100000u : moduleBase;
    module.size = (moduleEnd > module.baseAddress) ? static_cast<size_t>(moduleEnd - module.baseAddress) : 0u;
    module.active = true;

    m_loadedModules.push_back(module);

    uint32_t elfCrc32 = 0u;
    const bool elfCrc32Valid = computeFileCrc32(elfPath, elfCrc32);
    if (!elfCrc32Valid)
    {
        std::cerr << "[ps2xIOP] failed to compute ELF CRC32 for '" << elfPath << "'" << std::endl;
    }
    ps2x::iop::GameIdentity identity;
    identity.elfName = module.name;
    identity.entryPoint = m_cpuContext.pc;
    identity.crc32 = elfCrc32;
    std::string iopError;
    if (!m_iopSubsystem->configure(identity, &iopError))
    {
        std::cerr << "[ps2xIOP] failed to configure profile: " << iopError << std::endl;
        return false;
    }

    ps2_game_overrides::applyMatching(*this,
                                      elfPath,
                                      m_cpuContext.pc,
                                      elfCrc32,
                                      elfCrc32Valid);

    RUNTIME_LOG("ELF file loaded successfully. Entry point: 0x" << std::hex << m_cpuContext.pc << std::dec);
    return true;
}

const PS2Runtime::IoPaths &PS2Runtime::getIoPaths()
{
    return runtimeIoPaths();
}

void PS2Runtime::setIoPaths(const IoPaths &paths)
{
    IoPaths normalized = paths;
    normalized.elfPath = normalizeAbsolutePath(normalized.elfPath);
    normalized.elfDirectory = normalizeAbsolutePath(normalized.elfDirectory);
    normalized.hostRoot = normalizeAbsolutePath(normalized.hostRoot);
    normalized.cdRoot = normalizeAbsolutePath(normalized.cdRoot);
    normalized.mcRoot = normalizeAbsolutePath(normalized.mcRoot);
    normalized.cdImage = normalizeAbsolutePath(normalized.cdImage);

    if (normalized.elfDirectory.empty() && !normalized.elfPath.empty())
    {
        normalized.elfDirectory = normalized.elfPath.parent_path();
    }

    if (normalized.hostRoot.empty())
    {
        normalized.hostRoot = normalized.elfDirectory;
    }
    if (normalized.cdRoot.empty())
    {
        normalized.cdRoot = normalized.elfDirectory;
    }
    if (normalized.mcRoot.empty())
    {
        normalized.mcRoot = normalized.elfDirectory / "mc0";
    }

    runtimeIoPaths() = normalized;
}

void PS2Runtime::configureIoPathsFromElf(const std::string &elfPath)
{
    IoPaths paths = runtimeIoPaths();
    paths.elfPath = normalizeAbsolutePath(std::filesystem::path(elfPath));
    if (!paths.elfPath.empty())
    {
        paths.elfDirectory = paths.elfPath.parent_path();
    }

    if (!paths.elfDirectory.empty())
    {
        paths.hostRoot = paths.elfDirectory;
        paths.cdRoot = paths.elfDirectory;
        paths.mcRoot = paths.elfDirectory / "mc0";
    }

    setIoPaths(paths);
}

namespace
{
    bool generatedFunctionTableSlot(uint32_t address, uint32_t &slot)
    {
        if ((address & 3u) != 0u || g_ps2RecompiledFunctionTableSlotCount == 0u)
        {
            return false;
        }

        if (address < g_ps2RecompiledFunctionTableBase || address >= g_ps2RecompiledFunctionTableEnd)
        {
            return false;
        }

        const uint32_t offset = address - g_ps2RecompiledFunctionTableBase;
        slot = offset >> 2;
        return slot < g_ps2RecompiledFunctionTableSlotCount;
    }
}

bool PS2Runtime::replaceFunction(uint32_t address, RecompiledFunction func)
{
    uint32_t slot = 0u;
    if (!generatedFunctionTableSlot(address, slot))
    {
        std::cerr << "[function-table] cannot replace guest PC 0x" << std::hex << address
                  << ": outside generated dense table [0x" << g_ps2RecompiledFunctionTableBase
                  << ", 0x" << g_ps2RecompiledFunctionTableEnd << ")"
                  << std::dec << std::endl;
        return false;
    }

    g_ps2RecompiledFunctionTable[slot] = func;
    return true;
}

bool PS2Runtime::registerFunction(uint32_t address, RecompiledFunction func)
{
    return replaceFunction(address, func);
}

#if GHPC_DIAG
void PS2Runtime::noteProbeEntry(R5900Context *ctx, uint32_t targetPc,
                                uint32_t sourcePc, const char *via)
{
    // Watch list comes from the environment, so changing it needs no rebuild:
    //   GHPC_PROBE=0x2fcf98,0x24b970 ./scripts/run.sh --quiet --debug
    static uint32_t s_watch[16];
    static unsigned s_count = 0u;
    static uint32_t s_lo = 0xFFFFFFFFu, s_hi = 0u;
    static unsigned long long s_hits[16] = {0};
    // After the first 40 the watch prints every Nth call. The default of 1000
    // is too coarse for the question this probe is most often asked, which is
    // not "is it called" but "did it stop being called": at 1000 a function
    // that ran a few hundred times and then died looks the same as one that ran
    // a few hundred times and kept going. GHPC_PROBE_EVERY=50 turns the same
    // watch into a timeline without a rebuild.
    static unsigned long long s_every = 1000ull;
    static bool s_init = false;
    if (!s_init)
    {
        s_init = true;
        if (const char *e = std::getenv("GHPC_PROBE_EVERY"))
        {
            const unsigned long long v = std::strtoull(e, nullptr, 0);
            if (v > 0ull) s_every = v;
        }
        if (const char *env = std::getenv("GHPC_PROBE"))
        {
            const char *p = env;
            while (*p && s_count < 16u)
            {
                char *end = nullptr;
                const unsigned long v = std::strtoul(p, &end, 0);
                if (end == p) break;
                const uint32_t a = static_cast<uint32_t>(v);
                s_watch[s_count++] = a;
                if (a < s_lo) s_lo = a;
                if (a > s_hi) s_hi = a;
                p = end;
                while (*p == ',' || *p == ' ') ++p;
            }
            std::fprintf(stderr, "[probe] GHPCPROBE watching %u address(es)\n", s_count);
        }
    }
    // Common case with no watch list is two compares.
    if (s_count == 0u || targetPc < s_lo || targetPc > s_hi)
    {
        return;
    }
    for (unsigned i = 0; i < s_count; ++i)
    {
        if (s_watch[i] != targetPc) continue;
        const unsigned long long n = ++s_hits[i];
        if (n <= 40ull || (n % s_every) == 0ull)
        {
            std::fprintf(stderr,
                         "[probe] 0x%x #%llu via=%s a0=0x%x a1=0x%x a2=0x%x a3=0x%x ra=0x%x from=0x%x\n",
                         targetPc, n, via,
                         (unsigned)getRegU32(ctx, 4), (unsigned)getRegU32(ctx, 5),
                         (unsigned)getRegU32(ctx, 6), (unsigned)getRegU32(ctx, 7),
                         (unsigned)getRegU32(ctx, 31), sourcePc);
        }
        return;
    }
}
// GHPCHEAP: the newlib allocator state, read straight out of guest memory.
//
// GH2 links newlib's dlmalloc, so __malloc_av_ (0x449188, 1032 bytes = the
// classic mbinptr av_[NAV*2+2] with NAV=128) is the whole allocator: av_[2] is
// the top chunk and each bin i is a circular list rooted at a fake chunk at
// av_ + 8*i. A chunk keeps prev_size at +0, size at +4 (low bits are flags),
// fd at +8, bk at +12.
//
// Addresses are GH2 PS2 Final Debug specific, like the Debug::Fail probe above.
namespace
{
    constexpr uint32_t kMallocAvBase = 0x00449188u;   // __malloc_av_
    constexpr uint32_t kMallocSbrkBase = 0x004495A0u; // __malloc_sbrk_base
    constexpr uint32_t kMallocEntry = 0x0035C778u;    // malloc
    constexpr uint32_t kFlexFatalError = 0x00307CC0u; // yy_fatal_error
    constexpr unsigned kMallocBinCount = 128u;

    std::atomic<uint32_t> g_lastMallocSize{0u};
    std::atomic<unsigned long long> g_mallocCalls{0ull};
    std::atomic<uint32_t> g_lastSbrkOld{0u};
    std::atomic<uint32_t> g_lastSbrkNew{0u};
    std::atomic<int32_t> g_lastSbrkIncrement{0};
    std::atomic<unsigned long long> g_sbrkCalls{0ull};
    std::atomic<unsigned long long> g_sbrkRefusals{0ull};

    bool guestRead32(const uint8_t *rdram, uint32_t addr, uint32_t &out)
    {
        // Strip the kseg segment bits, do NOT mask to a fixed width: a 25 bit
        // mask silently truncates every address above 32MB, which is most of
        // the map now that PS2_RAM_SIZE is 128MB.
        addr &= 0x1FFFFFFFu;
        if (addr < 0x00100000u || (addr + 4u) > PS2_RAM_SIZE || (addr & 3u) != 0u)
        {
            return false;
        }
        std::memcpy(&out, rdram + addr, sizeof(out));
        return true;
    }
}

void PS2Runtime::dumpGuestHeapCensus(uint8_t *rdram, const char *why)
{
    uint32_t top = 0u;
    uint32_t sbrkBase = 0u;
    if (!guestRead32(rdram, kMallocAvBase + 8u, top) ||
        !guestRead32(rdram, kMallocSbrkBase, sbrkBase))
    {
        std::fprintf(stderr, "[ghpc/heap] %s: __malloc_av_ unreadable\n", why);
        return;
    }

    uint32_t topSize = 0u;
    guestRead32(rdram, top + 4u, topSize);
    topSize &= ~0x3u;

    // Bin 0 is the top chunk, so the free lists start at 1.
    uint64_t binFree = 0u;
    uint32_t largest = 0u;
    unsigned chunks = 0u;
    bool truncated = false;
    for (unsigned bin = 1u; bin < kMallocBinCount; ++bin)
    {
        const uint32_t root = kMallocAvBase + 8u * bin;
        uint32_t p = 0u;
        if (!guestRead32(rdram, root + 8u, p))
        {
            continue;
        }
        for (unsigned guard = 0u; p != root && guard < 4096u; ++guard)
        {
            uint32_t size = 0u;
            uint32_t next = 0u;
            if (!guestRead32(rdram, p + 4u, size) || !guestRead32(rdram, p + 8u, next))
            {
                truncated = true;
                break;
            }
            size &= ~0x3u;
            binFree += size;
            largest = std::max(largest, size);
            ++chunks;
            p = next;
        }
    }

    // Linear walk of every chunk between sbrk_base and top. A chunk is in use
    // when the NEXT chunk's size field carries PREV_INUSE, which is the only
    // record dlmalloc keeps of who is holding memory. This separates the two
    // ways a full heap gets that way: one huge live chunk is a pool the game
    // reserved and is not allocating out of, while a flood of small ones means
    // the memory is genuinely spent.
    //
    // newlib nudges the first chunk so the user pointer lands 8 aligned, and
    // the exact nudge depends on where the ELF ended, so rather than assume it
    // the walk tries each offset and keeps the one whose chain lands exactly on
    // top. Landing on top is what proves the walk was reading real chunks.
    uint32_t walkStart = 0u;
    uint64_t liveBytes = 0u;
    uint64_t freeBytes = 0u;
    unsigned liveCount = 0u;
    unsigned freeCount = 0u;
    uint32_t biggestLive = 0u;
    uint32_t biggestLiveAt = 0u;
    unsigned liveByBucket[32] = {};
    for (uint32_t nudge = 0u; nudge < 32u && walkStart == 0u; nudge += 4u)
    {
        const uint32_t start = ((sbrkBase + 7u) & ~7u) + nudge;
        uint32_t p = start;
        uint64_t live = 0u, dead = 0u;
        unsigned liveN = 0u, deadN = 0u, big = 0u, bigAt = 0u;
        unsigned buckets[32] = {};
        bool sane = true;
        while (p < top)
        {
            uint32_t sizeRaw = 0u;
            uint32_t nextRaw = 0u;
            const uint32_t size = (guestRead32(rdram, p + 4u, sizeRaw)) ? (sizeRaw & ~0x3u) : 0u;
            if (size < 16u || (p + size) > top || !guestRead32(rdram, p + size + 4u, nextRaw))
            {
                sane = false;
                break;
            }
            if ((nextRaw & 1u) != 0u)
            {
                live += size;
                ++liveN;
                unsigned bucket = 0u;
                while (bucket < 31u && (size >> bucket) > 1u)
                {
                    ++bucket;
                }
                ++buckets[bucket];
                if (size > big)
                {
                    big = size;
                    bigAt = p;
                }
            }
            else
            {
                dead += size;
                ++deadN;
            }
            p += size;
        }
        if (sane && p == top)
        {
            walkStart = start;
            liveBytes = live;
            freeBytes = dead;
            liveCount = liveN;
            freeCount = deadN;
            biggestLive = big;
            biggestLiveAt = bigAt;
            std::memcpy(liveByBucket, buckets, sizeof(buckets));
        }
    }

    const uint32_t ceiling = runtimeArenaBase();
    const uint32_t brk = g_lastSbrkNew.load();
    std::fprintf(stderr,
                 "[ghpc/heap] %s req=%u (malloc #%llu)\n"
                 "[ghpc/heap]   top=0x%08x size=%u (%.1f KB), headroom to ceiling 0x%08x = %d KB\n"
                 "[ghpc/heap]   bins: %u free chunks, %llu bytes total, largest %u\n"
                 "[ghpc/heap]   sbrk: base=0x%08x brk=0x%08x calls=%llu refused=%llu last=%+d%s\n",
                 why, (unsigned)g_lastMallocSize.load(), g_mallocCalls.load(),
                 top, topSize, (double)topSize / 1024.0, ceiling,
                 (int)((int64_t)ceiling - (int64_t)(top + topSize)) / 1024,
                 chunks, (unsigned long long)binFree, largest,
                 sbrkBase, brk, g_sbrkCalls.load(), g_sbrkRefusals.load(),
                 (int)g_lastSbrkIncrement.load(), truncated ? " (bin walk truncated)" : "");

    if (walkStart == 0u)
    {
        std::fprintf(stderr, "[ghpc/heap]   walk: no chunk chain from 0x%08x lands on top, "
                             "cannot say who holds the heap\n", sbrkBase);
        return;
    }

    std::fprintf(stderr,
                 "[ghpc/heap]   walk from 0x%08x: %u live chunks holding %llu bytes (%.1f MB), "
                 "%u free holding %llu\n"
                 "[ghpc/heap]   biggest live chunk 0x%08x = %u bytes (%.1f MB), %.1f%% of the heap\n",
                 walkStart, liveCount, (unsigned long long)liveBytes,
                 (double)liveBytes / (1024.0 * 1024.0), freeCount, (unsigned long long)freeBytes,
                 biggestLiveAt, biggestLive, (double)biggestLive / (1024.0 * 1024.0),
                 (liveBytes > 0u) ? (100.0 * (double)biggestLive / (double)liveBytes) : 0.0);

    std::fprintf(stderr, "[ghpc/heap]   live chunks by size:");
    for (unsigned bucket = 0u; bucket < 32u; ++bucket)
    {
        if (liveByBucket[bucket] != 0u)
        {
            std::fprintf(stderr, " %u:%u", 1u << bucket, liveByBucket[bucket]);
        }
    }
    std::fprintf(stderr, "\n");
}

void PS2Runtime::noteHeapCall(uint8_t *rdram, R5900Context *ctx, uint32_t targetPc)
{
    if (targetPc == kMallocEntry)
    {
        g_lastMallocSize.store(getRegU32(ctx, 4));
        g_mallocCalls.fetch_add(1ull);
        return;
    }

    // flex prints "out of dynamic memory" and exits, so this is the last place
    // the failing request and the heap that refused it are both still around.
    dumpGuestHeapCensus(rdram, "yy_fatal_error");
}

// sbrk computes the new break in the delay slot of its jal to EndOfHeap, so at
// syscall entry $s0 is that break and $a0 is still the increment. That makes
// this the exact point where "the heap is full" becomes true or does not.
void PS2Runtime::noteHeapCeilingCheck(R5900Context *ctx)
{
    // Only sbrk sets up those registers; anyone else asking for the ceiling
    // would report garbage.
    if (getRegU32(ctx, 31) != 0x0034BA08u)
    {
        return;
    }

    const uint32_t newBreak = getRegU32(ctx, 16);
    const int32_t increment = (int32_t)getRegU32(ctx, 4);
    g_lastSbrkNew.store(newBreak);
    g_lastSbrkOld.store(newBreak - (uint32_t)increment);
    g_lastSbrkIncrement.store(increment);
    g_sbrkCalls.fetch_add(1ull);

    const uint32_t ceiling = runtimeArenaBase();
    const bool refused = newBreak > ceiling;
    if (refused)
    {
        g_sbrkRefusals.fetch_add(1ull);
    }
    std::fprintf(stderr, "[ghpc/heap] sbrk %+d: 0x%08x -> 0x%08x ceiling 0x%08x %s\n",
                 increment, newBreak - (uint32_t)increment, newBreak, ceiling,
                 refused ? "REFUSED" : "ok");
}

// GHPCLOAD: name the file the song load is waiting on.
//
// LoadMgr::PollUntilLoaded walks the loader list at TheLoadMgr+0x38 and spins
// until the head reports IsLoaded. FileLoader::IsLoaded is only "the stream at
// +0x18 is null", and PollLoading nulls it only when the stream's readiness
// virtual (vtable +0x6c) returns nonzero. So a load that never finishes shows
// up here as a stream pointer that never clears, and the useful fact is which
// file it belongs to.
namespace
{
    constexpr uint32_t kLoadMgrGetLoader = 0x0031D6C0u;
    constexpr uint32_t kLoadMgrAddLoader = 0x0031D7F0u;
    constexpr uint32_t kFileLoaderPoll = 0x0031E2D8u;

    // FilePath is a String-alike, so the text is either inline or one pointer
    // away. Try both rather than guessing the layout.
    void describeGuestPath(const uint8_t *rdram, uint32_t addr, char *out, size_t outSize)
    {
        out[0] = '\0';
        auto readText = [&](uint32_t at) -> bool {
            if (at < 0x00100000u || at >= PS2_RAM_SIZE) return false;
            size_t n = 0;
            while (n + 1 < outSize && (at + n) < PS2_RAM_SIZE)
            {
                const uint8_t c = rdram[at + n];
                if (c == 0u) break;
                if (c < 0x20u || c >= 0x7Fu) return false;
                out[n++] = static_cast<char>(c);
            }
            out[n] = '\0';
            return n > 1;
        };
        // FilePath derives from String, and String::operator=(const char*)
        // does strcpy(this + 0x10, src), so the text buffer pointer is at
        // +0x10. Confirmed against __as__6StringPCc at 0x325a40, not guessed.
        uint32_t text = 0u;
        if (guestRead32(rdram, addr + 0x10u, text) && readText(text))
        {
            return;
        }
        // Layout is a guess, so when the guess fails say so with the bytes
        // rather than printing whatever printable noise happened to be near.
        char hex[64];
        int n = 0;
        for (uint32_t i = 0u; i < 16u && n < (int)sizeof(hex) - 4; ++i)
        {
            const uint32_t at = (addr & 0x1FFFFFFFu) + i;
            n += std::snprintf(hex + n, sizeof(hex) - n, "%02x",
                               (at < PS2_RAM_SIZE) ? rdram[at] : 0u);
        }
        std::snprintf(out, outSize, "<no text at 0x%08x, bytes %s>", addr, hex);
    }
}

void PS2Runtime::noteLoaderCall(uint8_t *rdram, R5900Context *ctx, uint32_t targetPc)
{
    if (targetPc == kFileLoaderPoll)
    {
        // Report only when the stream pointer changes, so a loader that is
        // simply pending does not drown the log.
        static uint32_t s_lastLoader = 0xFFFFFFFFu;
        static uint32_t s_lastStream = 0xFFFFFFFFu;
        static unsigned long long s_polls = 0ull;
        ++s_polls;
        const uint32_t loader = getRegU32(ctx, 4);
        uint32_t stream = 0u;
        guestRead32(rdram, loader + 0x18u, stream);
        if (loader == s_lastLoader && stream == s_lastStream)
        {
            return;
        }
        s_lastLoader = loader;
        s_lastStream = stream;
        uint32_t vtable = 0u;
        if (stream != 0u) guestRead32(rdram, stream, vtable);
        std::fprintf(stderr,
                     "[ghpc/load] FileLoader 0x%08x stream=0x%08x vt=0x%08x (poll #%llu)%s\n",
                     loader, stream, vtable, s_polls,
                     (stream == 0u) ? " LOADED" : "");
        return;
    }

    static unsigned long long s_requests = 0ull;
    const uint32_t pathAddr = getRegU32(ctx, 5);
    char path[128];
    describeGuestPath(rdram, pathAddr, path, sizeof(path));
    if (++s_requests <= 200ull)
    {
        std::fprintf(stderr, "[ghpc/load] %s \"%s\" ra=0x%x\n",
                     (targetPc == kLoadMgrAddLoader) ? "AddLoader" : "GetLoader",
                     path, (unsigned)getRegU32(ctx, 31));
    }
}
// GHPCBLK: where the ARK read chain stops.
//
// ArkFile::ReadDone returns (this+0x14 == 0) and is the only caller of
// BlockMgr::Poll on the load path, so a loader that never finishes means
// +0x14 never drains. Either the read was never queued (AddTask never runs)
// or it was queued and Poll cannot retire it. Counting all four points and
// watching +0x14 separates those without guessing.
namespace
{
    constexpr uint32_t kArkReadAsync = 0x002F7DE8u;  // ArkFile::ReadAsync
    constexpr uint32_t kBlockAddTask = 0x002F92D0u;  // BlockMgr::AddTask
    constexpr uint32_t kBlockPoll = 0x002F9848u;     // BlockMgr::Poll
    constexpr uint32_t kArkReadDone = 0x002F8128u;   // ArkFile::ReadDone
    constexpr uint32_t kTheBlockMgr = 0x00524B50u;   // TheBlockMgr, 48 bytes

    std::atomic<unsigned long long> g_readAsync{0ull};
    std::atomic<unsigned long long> g_addTask{0ull};
    std::atomic<unsigned long long> g_blockPoll{0ull};
    std::atomic<unsigned long long> g_readDone{0ull};
}

void PS2Runtime::noteBlockCall(uint8_t *rdram, R5900Context *ctx, uint32_t targetPc)
{
    if (targetPc == kBlockPoll)
    {
        g_blockPoll.fetch_add(1ull);
        return;
    }
    if (targetPc == kArkReadAsync)
    {
        const unsigned long long n = g_readAsync.fetch_add(1ull) + 1ull;
        if (n <= 20ull)
        {
            std::fprintf(stderr, "[ghpc/blk] ReadAsync #%llu ark=0x%08x buf=0x%08x bytes=%d\n",
                         n, getRegU32(ctx, 4), getRegU32(ctx, 5), (int)getRegU32(ctx, 6));
        }
        return;
    }
    if (targetPc == kBlockAddTask)
    {
        const unsigned long long n = g_addTask.fetch_add(1ull) + 1ull;
        if (n <= 20ull)
        {
            std::fprintf(stderr, "[ghpc/blk] AddTask #%llu mgr=0x%08x task=0x%08x\n",
                         n, getRegU32(ctx, 4), getRegU32(ctx, 5));
        }
        return;
    }

    // ReadDone: the pending count is the number that has to reach zero.
    const unsigned long long n = g_readDone.fetch_add(1ull) + 1ull;
    const uint32_t ark = getRegU32(ctx, 4);
    uint32_t pending = 0u;
    uint32_t got = 0u;
    guestRead32(rdram, ark + 0x14u, pending);
    guestRead32(rdram, ark + 0x18u, got);

    static uint32_t s_lastArk = 0xFFFFFFFFu;
    static uint32_t s_lastPending = 0xFFFFFFFFu;
    const bool changed = (ark != s_lastArk || pending != s_lastPending);
    if (!changed && (n % 2000ull) != 0ull)
    {
        return;
    }
    s_lastArk = ark;
    s_lastPending = pending;

    char mgr[128];
    int at = 0;
    for (uint32_t off = 0u; off < 48u && at < (int)sizeof(mgr) - 10; off += 4u)
    {
        uint32_t word = 0u;
        guestRead32(rdram, kTheBlockMgr + off, word);
        at += std::snprintf(mgr + at, sizeof(mgr) - at, "%s%x", off ? " " : "", word);
    }
    std::fprintf(stderr,
                 "[ghpc/blk] ReadDone #%llu ark=0x%08x pending=%u got=%u | "
                 "readAsync=%llu addTask=%llu poll=%llu\n"
                 "[ghpc/blk]   TheBlockMgr: %s\n",
                 n, ark, pending, got, g_readAsync.load(), g_addTask.load(),
                 g_blockPoll.load(), mgr);
}

// GHPCSTRM: the one word the song load waits on.
//
// GamePanel::IsLoaded gates the loading_screen -> game_screen transition and
// its last gate resolves, through BeatMatch and MasterAudio, to
// StreamEE::IsReady (0x26ba28), which is:
//
//     lw $2, 0x4c($4); addiu $2, $2, -3; sltiu $2, $2, 3
//
// so "ready" means the state word at StreamEE+0x4c is 3, 4 or 5. The stall
// leaves it outside that range forever. IsReady is called once per frame with
// the object in $a0, so hooking it names both the object and the state without
// having to find TheStreamEE.
//
// Addresses are GH2 PS2 Final Debug specific, like the probes above.
namespace
{
    constexpr uint32_t kStreamIsReady = 0x0026BA28u; // StreamEE::IsReady
    constexpr uint32_t kStreamState = 0x4Cu;         // StreamEE+0x4c

    std::atomic<unsigned long long> g_strmCalls{0ull};

    bool guestWrite32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        addr &= 0x1FFFFFFFu;
        if (addr < 0x00100000u || (addr + 4u) > PS2_RAM_SIZE || (addr & 3u) != 0u)
        {
            return false;
        }
        std::memcpy(rdram + addr, &value, sizeof(value));
        return true;
    }

    // EXPERIMENT (GHPC_STREAM_READY): stand in for the missing IOP synth module,
    // the same way GHPC_SYNTH_ACK stands in for SYNTH_R's SPU handshake.
    //
    // Nothing on the EE advances the state word 2 -> 3. The only writer of 3 is
    // Poll__8StreamEE at 0x26d054, reached only by draining a StreamOp of type 2
    // off the vector at StreamEE+0x2c. That op is pushed by
    // Dispatch__8StreamEEiiUi (0x26d5b8), whose only caller is
    // CtlDispatch_impl__7SynthEE (0x3eb828) via its jump table at 0x4eb680,
    // entry 2. That is an IOP -> EE RPC callback: the EE sends StreamInfoArg out
    // as CTL 0x190 from the state 2 handler and waits for the IOP to answer.
    // With no IOP synth module the answer never comes.
    //
    // A probe, not a fix. It writes the word rather than pushing the op, which
    // is the op's only observable effect. The delay is there so state 2's own
    // handler gets to run its one-shot (the 0x190 send, latched at +0x128)
    // before the state moves on.
    constexpr unsigned kStreamReadyDelay = 120u; // frames pinned at 2 before standing in
    constexpr uint32_t kStreamStatePrepared = 3u;
}

void PS2Runtime::noteStreamCall(uint8_t *rdram, R5900Context *ctx, uint32_t targetPc)
{
    (void)targetPc;
    const unsigned long long n = g_strmCalls.fetch_add(1ull) + 1ull;
    const uint32_t self = getRegU32(ctx, 4);
    uint32_t state = 0xFFFFFFFFu;
    guestRead32(rdram, self + kStreamState, state);

    static const bool standIn = std::getenv("GHPC_STREAM_READY") != nullptr;
    if (standIn)
    {
        static uint32_t s_pinnedSelf = 0xFFFFFFFFu;
        static unsigned s_pinnedFor = 0u;
        if (self == s_pinnedSelf && state == 2u)
        {
            ++s_pinnedFor;
        }
        else
        {
            s_pinnedSelf = self;
            s_pinnedFor = (state == 2u) ? 1u : 0u;
        }
        if (s_pinnedFor == kStreamReadyDelay &&
            guestWrite32(rdram, self + kStreamState, kStreamStatePrepared))
        {
            state = kStreamStatePrepared;
            std::fprintf(stderr,
                         "[ghpc/strm] #%llu this=0x%08x STAND-IN wrote state=3 "
                         "(IOP CTL cmd 2 never arrived)\n",
                         n, self);
        }
    }

    // Print on change so a stuck value costs one line, and on a slow heartbeat
    // so "still stuck" is distinguishable from "probe stopped firing".
    static uint32_t s_lastSelf = 0xFFFFFFFFu;
    static uint32_t s_lastState = 0xFFFFFFFEu;
    const bool changed = (self != s_lastSelf || state != s_lastState);
    if (!changed && (n % 600ull) != 0ull)
    {
        return;
    }
    s_lastSelf = self;
    s_lastState = state;
    std::fprintf(stderr,
                 "[ghpc/strm] #%llu this=0x%08x state=%u ready=%d%s\n",
                 n, self, state, (state - 3u) < 3u ? 1 : 0,
                 changed ? " CHANGED" : "");
}

// GHPCSONG: is the chart actually advancing, or is game_screen just a screen?
//
// The ladder in scripts/progress.py tops out at game_screen, so reaching it
// scores a win whether or not a single note ever moves. PlayerMatcher::Poll
// (0x117dd0) is called once per player per frame with the current SongPos,
// which is the value the whole chart is drawn from, so its leading float
// advancing over a run is the difference between "a screen appeared" and
// "the song is playing".
//
// ABI, read off the prologue rather than assumed: `move $16, $4` takes this,
// `move $17, $5` takes the SongPos reference and `mov.s $f20, $f12` takes the
// float, so it is Poll(this=$a0, ms=$f12, pos=$a1) with the float in an FPR
// and the pointer keeping its integer slot.
//
// SongPos is 0x14 bytes and only its leading float is ever read back
// (BeatMatcher::GetTick, InSoloNow), so that word is the position.
namespace
{
    std::atomic<unsigned long long> g_songCalls{0ull};
}

void PS2Runtime::noteSongCall(uint8_t *rdram, R5900Context *ctx, uint32_t targetPc)
{
    (void)targetPc;
    const unsigned long long n = g_songCalls.fetch_add(1ull) + 1ull;
    const uint32_t self = getRegU32(ctx, 4);
    const uint32_t posPtr = getRegU32(ctx, 5);
    const float ms = ctx->f[12];

    uint32_t raw = 0u;
    if (!guestRead32(rdram, posPtr, raw))
    {
        return;
    }
    float tick = 0.0f;
    std::memcpy(&tick, &raw, sizeof(tick));

    // Print the first call, then on a heartbeat. A stuck chart and a chart that
    // never got polled at all are different failures, and only the heartbeat
    // tells them apart: the line keeps arriving with the number standing still.
    // GHPC_SONG_HEARTBEAT=N overrides the cadence; N<=0 falls back to 120.
    static const unsigned long long every = []() -> unsigned long long {
        const char *env = std::getenv("GHPC_SONG_HEARTBEAT");
        const long v = env ? std::strtol(env, nullptr, 10) : 0L;
        return v > 0L ? (unsigned long long)v : 120ull;
    }();
    if (n != 1ull && (n % every) != 0ull)
    {
        return;
    }
    std::fprintf(stderr,
                 "[ghpc/song] #%llu this=0x%08x ms=%.3f tick=%.3f\n",
                 n, self, (double)ms, (double)tick);
}

// GHPCGAME: why the chart never starts.
//
// GamePanel::Enter calls SetRealtime(true) (0x106d04, $a1=1), so mUnk70 at +0x70
// is 1 by design on entry: realtime mode IS the count-in. While it is set,
// GamePanel::Poll takes the TaskMgr::UISeconds branch at 0x1071b8 and the `b`
// at 0x1071f4 jumps past BeatMatch::Poll, so no chart runs. It ends when
// StartGame (0x1070f8) flips it, and StartGame sits behind two gates:
//
//   107220  lw    $2, 0x88($17)      mUnk88, non-zero skips the check entirely
//   10723c  c.olt.s $f0, $f20        -0.025 < $f20, the count-in reaching zero
//   10724c  jal   StartGame
//
// $f20 is UISeconds() + mUnk78, and SetTimeOffset stores Seconds()-UISeconds()
// into mUnk78, so $f20 reconstructs TaskMgr seconds and counts up toward 0.
// Reading the fields is the only way to tell "the gate is shut" from "the clock
// is stopped" from "the clock is fine but the start time is absurd"; the
// disassembly cannot distinguish them and guessing between them is how a round
// gets wasted.
//
// Offsets are the decomp's GamePanel layout. Addresses are GH2 PS2 Final Debug.
namespace
{
    std::atomic<unsigned long long> g_gameCalls{0ull};

    float guestFloat(const uint8_t *rdram, uint32_t base, uint32_t off)
    {
        uint32_t raw = 0u;
        float value = 0.0f;
        if (guestRead32(const_cast<uint8_t *>(rdram), base + off, raw))
        {
            std::memcpy(&value, &raw, sizeof(value));
        }
        return value;
    }

    uint32_t guestWord(const uint8_t *rdram, uint32_t base, uint32_t off)
    {
        uint32_t raw = 0u;
        guestRead32(const_cast<uint8_t *>(rdram), base + off, raw);
        return raw;
    }
}

void PS2Runtime::noteGameCall(uint8_t *rdram, R5900Context *ctx, uint32_t targetPc)
{
    (void)targetPc;
    const unsigned long long n = g_gameCalls.fetch_add(1ull) + 1ull;
    const uint32_t self = getRegU32(ctx, 4);

    // Every call is printed. GamePanel::Poll runs about 30 times in a whole run
    // at the frame rate game_screen currently manages, so a heartbeat would
    // throw away most of the evidence rather than save noise.
    // UISeconds__C7TaskMgr (0x316448) is
    //   *(float *)(*(uint32_t *)(TheTaskMgr + 0x28) + 0x34)
    // and TheTaskMgr is 0x5A5870. countIn is the value the StartGame branch at
    // 0x10723c actually compares: $f20 = UISeconds() + mUnk78. StartGame runs
    // once it exceeds -0.025, so watching it move (or not) across a run is what
    // separates a shut gate from a stopped clock from a slow one.
    constexpr uint32_t kTheTaskMgr = 0x005A5870u;
    const uint32_t taskState = guestWord(rdram, kTheTaskMgr, 0x28u);
    const float uiSeconds = taskState ? guestFloat(rdram, taskState, 0x34u) : 0.0f;
    const float offset = guestFloat(rdram, self, 0x78u);

    std::fprintf(stderr,
                 "[ghpc/game] #%llu this=0x%08x realtime=%u startGate88=%u "
                 "unk84=%u unk36c=%u tempo=%.3f offset78=%.3f secs7c=%.3f "
                 "beat80=%.3f uiSecs=%.4f countIn=%.4f\n",
                 n, self,
                 guestWord(rdram, self, 0x70u),
                 guestWord(rdram, self, 0x88u),
                 guestWord(rdram, self, 0x84u),
                 guestWord(rdram, self, 0x36Cu),
                 (double)guestFloat(rdram, self, 0x74u),
                 (double)offset,
                 (double)guestFloat(rdram, self, 0x7Cu),
                 (double)guestFloat(rdram, self, 0x80u),
                 (double)uiSeconds,
                 (double)(uiSeconds + offset));
}
#endif

bool PS2Runtime::hasFunction(uint32_t address) const
{
    uint32_t slot = 0u;
    return generatedFunctionTableSlot(address, slot) && g_ps2RecompiledFunctionTable[slot] != nullptr;
}

const char *describeGuestBranchKind(PS2Runtime::GuestBranchKind kind)
{
    switch (kind)
    {
    case PS2Runtime::GuestBranchKind::DirectJump:
        return "DirectJump";
    case PS2Runtime::GuestBranchKind::DirectCall:
        return "DirectCall";
    case PS2Runtime::GuestBranchKind::IndirectJump:
        return "IndirectJump";
    case PS2Runtime::GuestBranchKind::IndirectCall:
        return "IndirectCall";
    case PS2Runtime::GuestBranchKind::Return:
        return "Return";
    default:
        return "Unknown";
    }
}

PS2Runtime::RecompiledFunction PS2Runtime::lookupFunction(uint32_t address)
{
    pushDispatchPc(address);

    uint32_t slot = 0u;
    if (generatedFunctionTableSlot(address, slot))
    {
        RecompiledFunction fn = g_ps2RecompiledFunctionTable[slot];
        if (fn != nullptr)
        {
            return fn;
        }
    }

    std::cerr << "Error: No exact recompiled function for guest PC 0x" << std::hex << address
              << " tableBase=0x" << g_ps2RecompiledFunctionTableBase
              << " tableEnd=0x" << g_ps2RecompiledFunctionTableEnd
              << " codeRegion=" << (m_memory.isCodeAddress(address) ? "yes" : "no")
              << " trace=" << formatDispatchHistory()
              << std::dec << std::endl;

    static RecompiledFunction missingFunction = [](uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t badPc = ctx->pc;
        runtime->reportMissingFunction(rdram,
                                       ctx,
                                       badPc,
                                       0u,
                                       PS2Runtime::GuestBranchKind::IndirectJump,
                                       "dispatch");
    };

    return missingFunction;
}

void PS2Runtime::setMissingFunctionPolicy(MissingFunctionPolicy policy)
{
    m_missingFunctionPolicy.store(static_cast<uint32_t>(policy), std::memory_order_release);
}

PS2Runtime::MissingFunctionPolicy PS2Runtime::missingFunctionPolicy() const
{
    return static_cast<MissingFunctionPolicy>(m_missingFunctionPolicy.load(std::memory_order_acquire));
}

void PS2Runtime::resetMissingFunctionReportOnce()
{
    m_missingFunctionReported.store(false, std::memory_order_release);
}

void PS2Runtime::reportMissingFunction(uint8_t *rdram,
                                       R5900Context *ctx,
                                       uint32_t targetPc,
                                       uint32_t sourcePc,
                                       GuestBranchKind kind,
                                       const char *debugName)
{
    const MissingFunctionPolicy policy = missingFunctionPolicy();
    const bool firstReport = !m_missingFunctionReported.exchange(true, std::memory_order_acq_rel);

    const uint32_t pc = ctx->pc;
    const uint32_t ra = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[31], 0));
    const uint32_t sp = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[29], 0));
    const uint32_t gp = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[28], 0));
    const uint32_t a0 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[4], 0));
    const uint32_t a1 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[5], 0));
    const uint32_t a2 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[6], 0));
    const uint32_t a3 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[7], 0));
    const uint32_t s0 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[16], 0));
    const uint32_t s1 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[17], 0));
    const uint32_t v0 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[2], 0));
    const uint32_t v1 = static_cast<uint32_t>(_mm_extract_epi32(ctx->r[3], 0));

    auto readGuestU32At = [rdram](uint32_t addr, uint32_t &out) -> bool
    {
        // TODO this !rdram exist only because of test fix those test later
        if (!rdram || addr > PS2_RAM_SIZE - sizeof(uint32_t))
        {
            out = 0u;
            return false;
        }

        std::memcpy(&out, rdram + addr, sizeof(uint32_t));
        return true;
    };

    auto readGuestU32Offset = [&readGuestU32At](uint32_t base, uint32_t offset, uint32_t &out) -> bool
    {
        if (base > PS2_RAM_SIZE - sizeof(uint32_t) || offset > PS2_RAM_SIZE - sizeof(uint32_t) - base)
        {
            out = 0u;
            return false;
        }

        return readGuestU32At(base + offset, out);
    };

    uint32_t a0Word0 = 0u;
    uint32_t a0Word4 = 0u;
    uint32_t a0Word8 = 0u;
    uint32_t a0WordC = 0u;
    const bool a0Readable =
        readGuestU32Offset(a0, 0x00u, a0Word0) &&
        readGuestU32Offset(a0, 0x04u, a0Word4) &&
        readGuestU32Offset(a0, 0x08u, a0Word8) &&
        readGuestU32Offset(a0, 0x0cu, a0WordC);

    uint32_t s0Word0 = 0u;
    uint32_t s0Word4 = 0u;
    uint32_t s0Word8 = 0u;
    uint32_t s0WordC = 0u;
    const bool s0Readable =
        readGuestU32Offset(s0, 0x00u, s0Word0) &&
        readGuestU32Offset(s0, 0x04u, s0Word4) &&
        readGuestU32Offset(s0, 0x08u, s0Word8) &&
        readGuestU32Offset(s0, 0x0cu, s0WordC);

    uint32_t recordWord0 = 0u;
    uint32_t recordWord4 = 0u;
    uint32_t recordWord8 = 0u;
    uint32_t recordWordC = 0u;
    const bool recordReadable =
        s0Readable && s0Word4 != 0u &&
        readGuestU32Offset(s0Word4, 0x00u, recordWord0) &&
        readGuestU32Offset(s0Word4, 0x04u, recordWord4) &&
        readGuestU32Offset(s0Word4, 0x08u, recordWord8) &&
        readGuestU32Offset(s0Word4, 0x0cu, recordWordC);

    uint32_t vtableSlot0 = 0u;
    uint32_t vtableSlot4 = 0u;
    uint32_t vtableSlot8 = 0u;
    uint32_t vtableSlotC = 0u;
    const bool vtableReadable =
        a0Readable && a0Word0 != 0u &&
        readGuestU32Offset(a0Word0, 0x00u, vtableSlot0) &&
        readGuestU32Offset(a0Word0, 0x04u, vtableSlot4) &&
        readGuestU32Offset(a0Word0, 0x08u, vtableSlot8) &&
        readGuestU32Offset(a0Word0, 0x0cu, vtableSlotC);

    // firstReport is one global latch for the whole run, across every site, so
    // a single line can hide something firing every frame. Frequency has been
    // the deciding question repeatedly here, and a first-only line cannot
    // answer it. Count, rate limited.
    {
        static std::mutex branchCensusMutex;
        static std::unordered_map<uint32_t, unsigned long long> branchBySource;
        static unsigned long long branchTotal = 0ull;
        std::lock_guard<std::mutex> lock(branchCensusMutex);
        ++branchBySource[sourcePc];
        if ((++branchTotal % 200ull) == 0ull)
        {
            std::fprintf(stderr, "[guest-branch census] total=%llu", branchTotal);
            for (const auto &entry : branchBySource)
            {
                std::fprintf(stderr, " src=0x%08x:%llu",
                             (unsigned)entry.first, (unsigned long long)entry.second);
            }
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
    }

    if (firstReport)
    {
        std::ostringstream oss;
        oss << "[guest-branch:missing-target] kind=" << describeGuestBranchKind(kind)
            << " op=" << (debugName ? debugName : "<unknown>")
            << " source=0x" << std::hex << sourcePc
            << " target=0x" << targetPc
            << " pc=0x" << pc
            << " ra=0x" << ra
            << " sp=0x" << sp
            << " gp=0x" << gp
            << " a0=0x" << a0
            << " a1=0x" << a1
            << " a2=0x" << a2
            << " a3=0x" << a3
            << " s0=0x" << s0
            << " s1=0x" << s1
            << " v0=0x" << v0
            << " v1=0x" << v1
            << " a0Readable=" << (a0Readable ? "yes" : "no")
            << " a0[0]=0x" << a0Word0
            << " a0[4]=0x" << a0Word4
            << " a0[8]=0x" << a0Word8
            << " a0[c]=0x" << a0WordC
            << " s0Readable=" << (s0Readable ? "yes" : "no")
            << " s0[0]=0x" << s0Word0
            << " s0[4]=0x" << s0Word4
            << " s0[8]=0x" << s0Word8
            << " s0[c]=0x" << s0WordC
            << " recordReadable=" << (recordReadable ? "yes" : "no")
            << " record[0]=0x" << recordWord0
            << " record[4]=0x" << recordWord4
            << " record[8]=0x" << recordWord8
            << " record[c]=0x" << recordWordC
            << " vtableReadable=" << (vtableReadable ? "yes" : "no")
            << " vtbl[0]=0x" << vtableSlot0
            << " vtbl[4]=0x" << vtableSlot4
            << " vtbl[8]=0x" << vtableSlot8
            << " vtbl[c]=0x" << vtableSlotC
            << " codeRegion=" << (m_memory.isCodeAddress(targetPc) ? "yes" : "no")
            << " policy=" << static_cast<uint32_t>(policy)
            << " trace=" << formatDispatchHistory()
            << std::dec;

        static std::mutex s_missingFunctionLogMutex;
        {
            std::lock_guard<std::mutex> lock(s_missingFunctionLogMutex);
            std::cerr << oss.str() << std::endl;
        }
    }

    if (firstReport && policy == MissingFunctionPolicy::BreakOnce)
    {
#if defined(_MSC_VER)
        __debugbreak();
#endif // TODO others breakpoints
    }

    if (ctx)
    {
        ctx->pc = targetPc;
    }

    if (policy == MissingFunctionPolicy::Stop)
    {
        requestStop();
    }
}

bool PS2Runtime::dispatchGuestBranch(uint8_t *rdram,
                                     R5900Context *ctx,
                                     uint32_t targetPc,
                                     uint32_t sourcePc,
                                     uint32_t fallthroughPc,
                                     GuestBranchKind kind,
                                     const char *debugName)
{
    ctx->pc = targetPc;
    const bool isCall = (kind == GuestBranchKind::DirectCall || kind == GuestBranchKind::IndirectCall);

#if GHPC_DIAG
    // GHPCPROBE: durable entry probe at the one seam every guest call crosses.
    // Lives here, in a real source file, so it can be exported as a patch.
    // Probes written into work/output cannot be: --from=recomp regenerates that
    // tree and --from=stage rsyncs over it, so there is no source of truth.
    // Watch list comes from the environment, so changing it needs no rebuild:
    //   GHPC_PROBE=0x2fcf98,0x24b970 ./scripts/run.sh --quiet --debug
    if (isCall)
    {
        noteProbeEntry(ctx, targetPc, sourcePc, "call");
    }

    // GHPCHEAP: remember every malloc request so the flex fatal error can name
    // the one that failed, and dump the allocator when it does.
    if (isCall && (targetPc == 0x0035C778u || targetPc == 0x00307CC0u))
    {
        noteHeapCall(rdram, ctx, targetPc);
    }

    // GHPCLOAD: which file the loader list is stuck on.
    if (isCall && (targetPc == 0x0031D6C0u || targetPc == 0x0031D7F0u ||
                   targetPc == 0x0031E2D8u))
    {
        noteLoaderCall(rdram, ctx, targetPc);
    }

    // GHPCBLK: read issued, queued, polled, retired.
    if (isCall && (targetPc == 0x002F7DE8u || targetPc == 0x002F92D0u ||
                   targetPc == 0x002F9848u || targetPc == 0x002F8128u))
    {
        noteBlockCall(rdram, ctx, targetPc);
    }

    // GHPCSTRM: the StreamEE state word that gates the song load.
    if (isCall && targetPc == 0x0026BA28u)
    {
        noteStreamCall(rdram, ctx, targetPc);
    }

    // GHPCSONG: PlayerMatcher::Poll, the per-frame song position.
    if (isCall && targetPc == 0x00117DD0u)
    {
        noteSongCall(rdram, ctx, targetPc);
    }

    // GHPCGAME: GamePanel::Poll, the count-in gate.
    if (isCall && targetPc == 0x00107140u)
    {
        noteGameCall(rdram, ctx, targetPc);
    }

    // GHPCASSERT: name the guest assert at the call, not after the fact.
    // EeScheduler::dumpStackStrings scavenges the stalled stack for printable
    // runs, which is why the text has been arriving as a fragment ("Data (").
    // Debug::Fail takes the already formatted message in $a1, so reading it
    // here gets the whole string with its file and line still attached.
    //
    // Notify (0x2ebd88) is covered too. It is the non-fatal spelling and it is
    // currently invisible, which hides every warning the game raises before the
    // one that stops it.
    //
    // +0x0c is the already-failing latch and +0x1c the try nesting depth, both
    // pinned by the decomp's Debug::Fail and Debug::SetTry. Depth is read here
    // because it is the value Fail is about to test: at depth 0 it goes modal,
    // above 0 it longjmps back out and the game carries on. That is what
    // separates an assert that kills the boot from one the game handles.
    if (isCall && (targetPc == 0x2EBDA8u || targetPc == 0x2EBD88u))
    {
        const uint32_t self = getRegU32(ctx, 4) & 0x01FFFFFFu;
        const uint32_t msg = getRegU32(ctx, 5) & 0x01FFFFFFu;

        char text[192];
        unsigned n = 0u;
        if (msg >= 0x00100000u && msg < 0x02000000u)
        {
            for (; n + 1u < sizeof(text); ++n)
            {
                const uint8_t c = rdram[msg + n];
                if (c == 0u)
                {
                    break;
                }
                text[n] = (c >= 0x20u && c < 0x7Fu) ? static_cast<char>(c) : '?';
            }
        }
        text[n] = '\0';

        uint32_t latch = 0u;
        uint32_t depth = 0u;
        if (self >= 0x00100000u && self + 0x20u < 0x02000000u)
        {
            std::memcpy(&latch, rdram + self + 0x0Cu, sizeof(latch));
            std::memcpy(&depth, rdram + self + 0x1Cu, sizeof(depth));
        }

        std::fprintf(stderr, "[assert] %s depth=%u latch=%u from=0x%x msg=\"%s\"\n",
                     targetPc == 0x2EBDA8u ? "Fail" : "Notify",
                     depth, latch, sourcePc, text);
    }
#endif

    // GHPCCOUNTIN and GHPCSTART live OUTSIDE the diagnostics block on purpose.
    //
    // Every throughput number this project has on record comes from
    // build-debug, and the run that produced the last one wrote 140,350 lines
    // of formatted stderr in 600 seconds. Sizing the Rnd seam against that is
    // sizing it against the logging. Measuring a release build instead needs
    // exactly one thing the release build did not have: a way to reach
    // gameplay without waiting out the ten second count-in. GHPC_PAD_DRIVE and
    // the [drive], [fps] and [eerate] reporters were already unconditional, so
    // this is the last piece.
    //
    // The cost when the knob is unset is one load and test of a cached bool
    // that never changes, which the branch predictor will not miss twice.
    // Calling getenv here instead would put a lock on the hottest path in the
    // runtime.
    //
    // GHPCCOUNTIN shortens the count-in at its source rather than at the gate.
    // StartIntro (0x1074e8) works out how long the intro camera shot runs
    //   1078f8  div.s $f20, $f0, $f1     $f1 = 41f00000 = 30.0, frames -> secs
    // and hands it straight to SetStartTime
    //   10799c  jal 0x107e88             SetStartTime(this, $f20)
    // which is the only writer of the clock the StartGame gate reads: it calls
    // SetSecondsBeat(TheTaskMgr, $f12, $f12 * 1000 / GetTempo()) and then
    // SetRealtime(true), whose SetTimeOffset is what fills mUnk78. So every
    // derived value, the beat included, comes out of this one float.
    //
    // That is why the clamp goes here and not on mUnk78. Poking mUnk78 later
    // moves the gate while leaving TaskMgr's seconds and the beat where the ten
    // second shot put them, which is a guest in two minds about what time it
    // is. Clamping the argument leaves the game to compute all of it, and it is
    // the same lever the game pulls itself: mFastIntro (+0x68) takes the branch
    // at 0x1077fc that skips FindCameraShot, which is to say it feeds
    // SetStartTime a smaller number by exactly this route.
    //
    // A probe, not a fix. The count-in is real and the camera shot is real;
    // this is a harness for looking at what comes after them, and it stays out
    // of any run that touches the recorded mark.
    {
        static const float s_countInFloor = []() -> float {
            const char *e = std::getenv("GHPC_COUNTIN");
            return e ? (float)std::atof(e) : 0.0f;
        }();
        static const bool s_countInOn = (s_countInFloor > 0.0f);
        if (s_countInOn && isCall)
        {
            // GHPCSTART: latch the one instant the count-in ends, so the frame
            // dumper can spend its budget on gameplay instead of on boot.
            if (targetPc == 0x001070F8u && !g_ghpcGameStarted.exchange(true))
            {
                std::fprintf(stderr, "[ghpc/start] StartGame entered, count-in over\n");
            }
            if (targetPc == 0x00107E88u && ctx->f[12] < -s_countInFloor)
            {
                std::fprintf(stderr, "[ghpc/countin] %.3f -> %.3f\n",
                             (double)ctx->f[12], (double)-s_countInFloor);
                ctx->f[12] = -s_countInFloor;
            }
        }
    }

    // Every inter-function transfer is also a deterministic EE safe point.
    // Backward edges inside generated functions use eeCheckpointDue(), while
    // this charge bounds straight-line call chains that have no local loop.
    if (m_eeScheduler && m_eeScheduler->checkpointDue(EeScheduler::kGuestDispatchCycles))
    {
        return false;
    }

    if (!isCall)
    {
        if (!hasFunction(targetPc))
        {
            reportMissingFunction(rdram, ctx, targetPc, sourcePc, kind, debugName);
        }

        ctx->pc = targetPc;
        return false;
    }

    if (!hasFunction(targetPc))
    {
        reportMissingFunction(rdram, ctx, targetPc, sourcePc, kind, debugName);

        const MissingFunctionPolicy policy = missingFunctionPolicy();

        if (policy == MissingFunctionPolicy::SkipCallDebug && isCall)
        {
            ctx->pc = fallthroughPc;
            return true;
        }

        if (policy == MissingFunctionPolicy::ContinueToTarget)
        {
            ctx->pc = targetPc;
            return true;
        }

        return false;
    }

    RecompiledFunction targetFn = lookupFunction(targetPc);
    const uint32_t entryPc = ctx->pc;
    targetFn(rdram, ctx, this);

    if (isStopRequested() || ctx->pc == 0u)
    {
        return false;
    }

    // A checkpoint can fire before the callee runs a single instruction, which
    // unwinds the host stack with ctx->pc parked on the callee entry so the
    // scheduler can resume it. That is indistinguishable by pc alone from a
    // stub that returned without touching pc, and the two collide whenever the
    // parked address equals this frame's own target -- the recursive
    // DataArray::Load / DataNode::Load parser hits it. Rewriting pc there
    // discards the resume point and swallows the call, leaving the element
    // unwritten. Let the unwind through untouched instead.
    if (m_eeScheduler && m_eeScheduler->yieldInFlight())
    {
        return false;
    }

    if (ctx->pc == entryPc)
    {
        ctx->pc = fallthroughPc;
    }

    return ctx->pc == fallthroughPc;
}

void PS2Runtime::SignalException(R5900Context *ctx, PS2Exception exception)
{
    if (exception == EXCEPTION_INTEGER_OVERFLOW)
    {
        HandleIntegerOverflow(ctx);
        return;
    }

    raiseCop0Exception(ctx, static_cast<uint32_t>(exception),
                       exception == EXCEPTION_TLB_REFILL);
}

void PS2Runtime::executeVU0Microprogram(uint8_t *rdram, R5900Context *ctx, uint32_t address)
{
    (void)rdram;

    uint8_t *const vu0Code = m_memory.getVU0Code();
    uint8_t *const vu0Data = m_memory.getVU0Data();
    const uint32_t startPC = address & ~0x7u;

    if (!vu0Code || !vu0Data || startPC + 8u > PS2_VU0_CODE_SIZE)
    {
        seedVu0IdleSuccess(ctx);
        return;
    }

    m_vu0.reset();
    copyVu0ContextToState(ctx, m_vu0.state());
    m_vu0.execute(vu0Code, PS2_VU0_CODE_SIZE,
                  vu0Data, PS2_VU0_DATA_SIZE,
                  m_gs, &m_memory,
                  startPC, 0u, ctx->vu0_itop, 4096);
    copyVu0StateToContext(m_vu0.state(), ctx);
}

void PS2Runtime::vu0StartMicroProgram(uint8_t *rdram, R5900Context *ctx, uint32_t address)
{
    // VCALLMS and VCALLMSR both route here.
    executeVU0Microprogram(rdram, ctx, address);
}

void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx)
{
    handleSyscall(rdram, ctx, 0);
}

void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx, uint32_t encodedSyscallId)
{
    if (ctx->in_delay_slot)
    {
        throw std::runtime_error("Attempted to execute a syscall inside a branch delay slot! "
                                 "This breaks the atomic basic block model and is structurally unsupported by the emulator.");
    }

    const uint32_t syscallId = (encodedSyscallId != 0u)
                                   ? encodedSyscallId
                                   : getRegU32(ctx, 3); // $v1 / $3 is the EE kernel syscall number

    if (ps2_syscalls::dispatchNumericSyscall(syscallId, rdram, ctx, this))
    {
        return;
    }

    // God help you
    ps2_syscalls::TODO(rdram, ctx, this, encodedSyscallId);
}

void PS2Runtime::handleBreak(uint8_t *rdram, R5900Context *ctx)
{
    raiseCop0Exception(ctx, EXCEPTION_BREAKPOINT);
}

void PS2Runtime::drainCompletedDmacHandlers(uint8_t *rdram)
{
    for (uint32_t cause : m_memory.consumeCompletedDmacCauses())
    {
        ps2_syscalls::dispatchDmacHandlersForCause(rdram, this, cause);
    }
}

void PS2Runtime::handleTrap(uint8_t *rdram, R5900Context *ctx)
{
    raiseCop0Exception(ctx, EXCEPTION_TRAP);
}

void PS2Runtime::handleTLBR(uint8_t *rdram, R5900Context *ctx)
{
    uint32_t vpn = 0;
    uint32_t pfn = 0;
    uint32_t mask = 0;
    bool valid = false;

    const uint32_t index = ctx->cop0_index & 0x3Fu;
    if (!m_memory.tlbRead(index, vpn, pfn, mask, valid))
    {
        raiseCop0Exception(ctx, EXCEPTION_RESERVED_INSTRUCTION);
        return;
    }

    // Preserve low ASID bits in EntryHi.
    ctx->cop0_entryhi = (ctx->cop0_entryhi & 0x00000FFFu) | (vpn & 0xFFFFF000u);
    ctx->cop0_entrylo0 = (ctx->cop0_entrylo0 & ~0x03FFFFC2u) |
                         ((pfn & 0x000FFFFFu) << 6) |
                         (valid ? 0x2u : 0u);
    ctx->cop0_pagemask = mask & 0x01FFE000u;
}

void PS2Runtime::handleTLBWI(uint8_t *rdram, R5900Context *ctx)
{
    const uint32_t index = ctx->cop0_index & 0x3Fu;
    const uint32_t vpn = ctx->cop0_entryhi & 0xFFFFF000u;
    const uint32_t pfn = (ctx->cop0_entrylo0 >> 6) & 0x000FFFFFu;
    const uint32_t mask = ctx->cop0_pagemask & 0x01FFE000u;
    const bool valid = (ctx->cop0_entrylo0 & 0x2u) != 0u;

    if (!m_memory.tlbWrite(index, vpn, pfn, mask, valid))
    {
        raiseCop0Exception(ctx, EXCEPTION_RESERVED_INSTRUCTION);
    }
}

void PS2Runtime::handleTLBWR(uint8_t *rdram, R5900Context *ctx)
{
    const uint32_t entryCount = static_cast<uint32_t>(m_memory.tlbEntryCount());
    if (entryCount == 0)
    {
        raiseCop0Exception(ctx, EXCEPTION_RESERVED_INSTRUCTION);
        return;
    }

    const uint32_t wired = std::min(ctx->cop0_wired, entryCount - 1);
    uint32_t random = ctx->cop0_random % entryCount;
    if (random < wired)
    {
        random = wired;
    }

    const uint32_t vpn = ctx->cop0_entryhi & 0xFFFFF000u;
    const uint32_t pfn = (ctx->cop0_entrylo0 >> 6) & 0x000FFFFFu;
    const uint32_t mask = ctx->cop0_pagemask & 0x01FFE000u;
    const bool valid = (ctx->cop0_entrylo0 & 0x2u) != 0u;

    if (!m_memory.tlbWrite(random, vpn, pfn, mask, valid))
    {
        raiseCop0Exception(ctx, EXCEPTION_RESERVED_INSTRUCTION);
        return;
    }

    // Keep COP0 bookkeeping in sync with the selected slot.
    ctx->cop0_index = (ctx->cop0_index & ~0x3Fu) | (random & 0x3Fu);
    ctx->cop0_random = (random <= wired) ? (entryCount - 1) : (random - 1);
}

void PS2Runtime::handleTLBP(uint8_t *rdram, R5900Context *ctx)
{
    const int32_t index = m_memory.tlbProbe(ctx->cop0_entryhi & 0xFFFFF000u);
    if (index >= 0)
    {
        ctx->cop0_index = (ctx->cop0_index & ~0x8000003Fu) |
                          (static_cast<uint32_t>(index) & 0x3Fu);
    }
    else
    {
        // MIPS sets probe failure bit (P) in Index[31].
        ctx->cop0_index |= 0x80000000u;
    }
}

void PS2Runtime::clearLLBit(R5900Context *ctx)
{
    // LL/SC reservation is tracked separately from COP0 Status.
    ctx->llbit = 0;
    ctx->lladdr = 0;
}

uint32_t PS2Runtime::alignGuestHeapValue(uint32_t value, uint32_t alignment)
{
    if (alignment == 0)
    {
        return value;
    }

    const uint32_t mask = alignment - 1u;
    if (value > (std::numeric_limits<uint32_t>::max() - mask))
    {
        return std::numeric_limits<uint32_t>::max();
    }
    return (value + mask) & ~mask;
}

bool PS2Runtime::isGuestHeapAlignmentValid(uint32_t alignment)
{
    return alignment != 0u && (alignment & (alignment - 1u)) == 0u;
}

uint32_t PS2Runtime::normalizeGuestHeapAlignment(uint32_t alignment)
{
    if (!isGuestHeapAlignmentValid(alignment))
    {
        return kGuestHeapDefaultAlignment;
    }
    return std::max(alignment, kGuestHeapDefaultAlignment);
}

uint32_t PS2Runtime::clampGuestHeapBase(uint32_t guestBase) const
{
    uint32_t normalized = guestBase;
    if (normalized >= PS2_RAM_SIZE)
    {
        normalized &= PS2_RAM_MASK;
    }
    const uint32_t hardLimit = std::min(kGuestHeapHardLimit, PS2_RAM_SIZE);
    return std::min(normalized, hardLimit);
}

uint32_t PS2Runtime::clampGuestHeapLimit(uint32_t guestLimit) const
{
    const uint32_t hardLimit = std::min(kGuestHeapHardLimit, PS2_RAM_SIZE);
    if (guestLimit == 0u || guestLimit > hardLimit)
    {
        return hardLimit;
    }
    return guestLimit;
}

void PS2Runtime::resetGuestHeapLocked(uint32_t guestBase, uint32_t guestLimit)
{
    uint32_t base = alignGuestHeapValue(clampGuestHeapBase(guestBase), kGuestHeapDefaultAlignment);
    uint32_t limit = clampGuestHeapLimit(guestLimit);
    if (base == 0u)
    {
        const uint32_t fallbackBase = (m_guestHeapSuggestedBase != 0u) ? m_guestHeapSuggestedBase : kGuestHeapDefaultBase;
        base = alignGuestHeapValue(clampGuestHeapBase(fallbackBase), kGuestHeapDefaultAlignment);
    }

    if (limit <= base)
    {
        base = alignGuestHeapValue(clampGuestHeapBase(m_guestHeapSuggestedBase), kGuestHeapDefaultAlignment);
        limit = clampGuestHeapLimit(0u);
    }

    if (limit <= base)
    {
        base = 0u;
        limit = 0u;
    }

    m_guestHeapBlocks.clear();
    if (limit > base)
    {
        m_guestHeapBlocks.push_back({base, limit - base, true});
    }

    m_guestHeapBase = base;
    m_guestHeapEnd = base;
    m_guestHeapLimit = limit;
    m_guestHeapConfigured = true;
}

void PS2Runtime::ensureGuestHeapInitializedLocked()
{
    if (m_guestHeapConfigured)
    {
        return;
    }

    const uint32_t suggested = (m_guestHeapSuggestedBase == 0u) ? kGuestHeapDefaultBase : m_guestHeapSuggestedBase;
    resetGuestHeapLocked(suggested, clampGuestHeapLimit(0u));
}

int32_t PS2Runtime::findGuestHeapBlockIndexLocked(uint32_t guestAddr) const
{
    const uint32_t normalizedAddr = guestAddr & PS2_RAM_MASK;
    for (size_t i = 0; i < m_guestHeapBlocks.size(); ++i)
    {
        const GuestHeapBlock &block = m_guestHeapBlocks[i];
        if (!block.free && block.addr == normalizedAddr)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

uint32_t PS2Runtime::allocateGuestBlockLocked(uint32_t size, uint32_t alignment)
{
    if (size == 0u)
    {
        return 0u;
    }

    const uint32_t normalizedAlignment = normalizeGuestHeapAlignment(alignment);
    if (size > (std::numeric_limits<uint32_t>::max() - (kGuestHeapDefaultAlignment - 1u)))
    {
        return 0u;
    }

    const uint32_t allocSize = alignGuestHeapValue(size, kGuestHeapDefaultAlignment);
    if (allocSize == 0u)
    {
        return 0u;
    }

    // Keep the runtime's reserve out of reach of bulk grabs. Only allocations
    // larger than the reserve are held back, so ordinary small ones still get
    // served once the game has taken its pools.
    const uint32_t reserve = guestHeapRuntimeReserve();
    if (reserve != 0u && allocSize > reserve)
    {
        uint64_t totalFree = 0;
        for (const GuestHeapBlock &b : m_guestHeapBlocks)
        {
            if (b.free)
            {
                totalFree += b.size;
            }
        }
        if (totalFree < static_cast<uint64_t>(allocSize) + static_cast<uint64_t>(reserve))
        {
            return 0u;
        }
    }

    for (size_t i = 0; i < m_guestHeapBlocks.size(); ++i)
    {
        const GuestHeapBlock block = m_guestHeapBlocks[i];
        if (!block.free)
        {
            continue;
        }

        const uint64_t blockStart = block.addr;
        const uint64_t blockEnd = blockStart + static_cast<uint64_t>(block.size);
        const uint32_t alignedAddr = alignGuestHeapValue(block.addr, normalizedAlignment);
        if (alignedAddr < block.addr)
        {
            continue;
        }

        const uint64_t alignedStart = alignedAddr;
        if (alignedStart > blockEnd)
        {
            continue;
        }

        const uint64_t allocEnd = alignedStart + static_cast<uint64_t>(allocSize);
        if (allocEnd > blockEnd)
        {
            continue;
        }

        const uint32_t prefixSize = static_cast<uint32_t>(alignedStart - blockStart);
        const uint32_t suffixSize = static_cast<uint32_t>(blockEnd - allocEnd);

        std::vector<GuestHeapBlock> replacement;
        replacement.reserve(3);
        if (prefixSize > 0u)
        {
            replacement.push_back({block.addr, prefixSize, true});
        }
        replacement.push_back({alignedAddr, allocSize, false});
        if (suffixSize > 0u)
        {
            replacement.push_back({static_cast<uint32_t>(allocEnd), suffixSize, true});
        }

        m_guestHeapBlocks.erase(m_guestHeapBlocks.begin() + static_cast<std::ptrdiff_t>(i));
        m_guestHeapBlocks.insert(m_guestHeapBlocks.begin() + static_cast<std::ptrdiff_t>(i),
                                 replacement.begin(),
                                 replacement.end());

        m_guestHeapEnd = std::max(m_guestHeapEnd, static_cast<uint32_t>(allocEnd));
        return alignedAddr;
    }

    return 0u;
}

void PS2Runtime::coalesceGuestHeapLocked()
{
    if (m_guestHeapBlocks.empty())
    {
        return;
    }

    size_t i = 1;
    while (i < m_guestHeapBlocks.size())
    {
        GuestHeapBlock &prev = m_guestHeapBlocks[i - 1];
        GuestHeapBlock &curr = m_guestHeapBlocks[i];
        const uint64_t prevEnd = static_cast<uint64_t>(prev.addr) + static_cast<uint64_t>(prev.size);
        if (prev.free && curr.free && prevEnd == curr.addr)
        {
            prev.size += curr.size;
            m_guestHeapBlocks.erase(m_guestHeapBlocks.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void PS2Runtime::freeGuestBlockLocked(uint32_t guestAddr)
{
    const int32_t index = findGuestHeapBlockIndexLocked(guestAddr);
    if (index < 0)
    {
        return;
    }

    m_guestHeapBlocks[static_cast<size_t>(index)].free = true;
    coalesceGuestHeapLocked();
}

void PS2Runtime::configureGuestHeap(uint32_t guestBase, uint32_t guestLimit)
{
    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    uint32_t normalizedBase = alignGuestHeapValue(clampGuestHeapBase(guestBase), kGuestHeapDefaultAlignment);
    if (normalizedBase == 0u)
    {
        normalizedBase = (m_guestHeapSuggestedBase != 0u) ? m_guestHeapSuggestedBase : kGuestHeapDefaultBase;
    }
    m_guestHeapSuggestedBase = normalizedBase;
    resetGuestHeapLocked(normalizedBase, guestLimit);
}

uint32_t PS2Runtime::runtimeArenaBase()
{
    return kRuntimeArenaBase;
}

uint32_t PS2Runtime::guestHeapRuntimeReserve()
{
    static const uint32_t value = []() -> uint32_t {
        if (const char *env = std::getenv("GHPC_HEAP_RESERVE"))
        {
            return static_cast<uint32_t>(std::strtoul(env, nullptr, 0));
        }
        return kGuestHeapRuntimeReserve;
    }();
    return value;
}

uint32_t PS2Runtime::guestMalloc(uint32_t size, uint32_t alignment)
{
    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    ensureGuestHeapInitializedLocked();
    const uint32_t addr = allocateGuestBlockLocked(size, alignment);

    // The game's malloc is bound to a stub that lands here, so a refusal is
    // what the guest sees as a NULL. Bulk grabs being refused is how the game
    // finds its pool size and is not worth reporting; a small request failing
    // means the arena is actually gone, which is what killed the DTA lexer.
    if (addr == 0u && size != 0u && size <= guestHeapRuntimeReserve())
    {
        uint64_t totalFree = 0, largestFree = 0;
        size_t freeCount = 0, usedCount = 0;
        for (const GuestHeapBlock &b : m_guestHeapBlocks)
        {
            if (b.free)
            {
                ++freeCount;
                totalFree += b.size;
                largestFree = std::max<uint64_t>(largestFree, b.size);
            }
            else
            {
                ++usedCount;
            }
        }
        std::fprintf(stderr,
                     "[heap] guestMalloc failed size=%u align=%u base=0x%08x limit=0x%08x"
                     " blocks=%zu used=%zu free=%zu freeBytes=%llu largestFree=%llu reserve=%u\n",
                     (unsigned)size, (unsigned)alignment, m_guestHeapBase, m_guestHeapLimit,
                     m_guestHeapBlocks.size(), usedCount, freeCount,
                     (unsigned long long)totalFree, (unsigned long long)largestFree,
                     (unsigned)guestHeapRuntimeReserve());
        std::fflush(stderr);
    }

    return addr;
}

uint32_t PS2Runtime::guestCalloc(uint32_t count, uint32_t size, uint32_t alignment)
{
    if (count == 0u || size == 0u)
    {
        return 0u;
    }
    if (count > (std::numeric_limits<uint32_t>::max() / size))
    {
        return 0u;
    }

    const uint32_t totalSize = count * size;
    const uint32_t guestAddr = guestMalloc(totalSize, alignment);
    if (guestAddr != 0u)
    {
        uint8_t *rdram = m_memory.getRDRAM();
        if (rdram)
        {
            uint32_t physAddr = guestAddr & PS2_RAM_MASK;
            if (physAddr + totalSize <= PS2_RAM_SIZE)
                std::memset(rdram + physAddr, 0, totalSize);
        }
    }

    return guestAddr;
}

uint32_t PS2Runtime::guestRealloc(uint32_t guestAddr, uint32_t newSize, uint32_t alignment)
{
    if (guestAddr == 0u)
    {
        return guestMalloc(newSize, alignment);
    }
    if (newSize == 0u)
    {
        guestFree(guestAddr);
        return 0u;
    }

    if (newSize > (std::numeric_limits<uint32_t>::max() - (kGuestHeapDefaultAlignment - 1u)))
    {
        return 0u;
    }

    const uint32_t normalizedAlignment = normalizeGuestHeapAlignment(alignment);
    const uint32_t requestedSize = alignGuestHeapValue(newSize, kGuestHeapDefaultAlignment);

    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    ensureGuestHeapInitializedLocked();

    const int32_t index = findGuestHeapBlockIndexLocked(guestAddr);
    if (index < 0)
    {
        return 0u;
    }

    const size_t blockIndex = static_cast<size_t>(index);
    const uint32_t oldAddr = m_guestHeapBlocks[blockIndex].addr;
    const uint32_t oldSize = m_guestHeapBlocks[blockIndex].size;

    if (requestedSize <= oldSize)
    {
        if (requestedSize < oldSize)
        {
            const uint32_t tailAddr = oldAddr + requestedSize;
            const uint32_t tailSize = oldSize - requestedSize;
            m_guestHeapBlocks[blockIndex].size = requestedSize;
            m_guestHeapBlocks.insert(m_guestHeapBlocks.begin() + static_cast<std::ptrdiff_t>(blockIndex + 1u),
                                     GuestHeapBlock{tailAddr, tailSize, true});
            coalesceGuestHeapLocked();
        }
        return oldAddr;
    }

    if (blockIndex + 1u < m_guestHeapBlocks.size())
    {
        GuestHeapBlock &next = m_guestHeapBlocks[blockIndex + 1u];
        const uint64_t blockEnd = static_cast<uint64_t>(m_guestHeapBlocks[blockIndex].addr) +
                                  static_cast<uint64_t>(m_guestHeapBlocks[blockIndex].size);
        if (next.free && blockEnd == next.addr)
        {
            const uint64_t combined = static_cast<uint64_t>(m_guestHeapBlocks[blockIndex].size) +
                                      static_cast<uint64_t>(next.size);
            if (combined >= requestedSize)
            {
                const uint32_t extraNeeded = requestedSize - m_guestHeapBlocks[blockIndex].size;
                m_guestHeapBlocks[blockIndex].size = requestedSize;
                if (next.size == extraNeeded)
                {
                    m_guestHeapBlocks.erase(m_guestHeapBlocks.begin() + static_cast<std::ptrdiff_t>(blockIndex + 1u));
                }
                else
                {
                    next.addr += extraNeeded;
                    next.size -= extraNeeded;
                }
                m_guestHeapEnd = std::max(m_guestHeapEnd, oldAddr + requestedSize);
                return oldAddr;
            }
        }
    }

    const uint32_t newAddr = allocateGuestBlockLocked(newSize, normalizedAlignment);
    if (newAddr == 0u)
    {
        return 0u;
    }

    uint8_t *rdram = m_memory.getRDRAM();
    if (rdram)
    {
        const uint32_t copyBytes = std::min(oldSize, newSize);
        uint32_t dstPhys = newAddr & PS2_RAM_MASK;
        uint32_t srcPhys = oldAddr & PS2_RAM_MASK;
        if (dstPhys + copyBytes <= PS2_RAM_SIZE && srcPhys + copyBytes <= PS2_RAM_SIZE)
            std::memmove(rdram + dstPhys, rdram + srcPhys, copyBytes);
    }

    freeGuestBlockLocked(oldAddr);
    return newAddr;
}

void PS2Runtime::guestFree(uint32_t guestAddr)
{
    if (guestAddr == 0u)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    ensureGuestHeapInitializedLocked();
    freeGuestBlockLocked(guestAddr);
}

uint32_t PS2Runtime::guestHeapBase() const
{
    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    return m_guestHeapConfigured ? m_guestHeapBase : m_guestHeapSuggestedBase;
}

uint32_t PS2Runtime::guestHeapEnd() const
{
    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    return m_guestHeapConfigured ? m_guestHeapEnd : m_guestHeapSuggestedBase;
}

uint32_t PS2Runtime::guestHeapLimit() const
{
    std::lock_guard<std::mutex> lock(m_guestHeapMutex);
    return m_guestHeapConfigured ? m_guestHeapLimit : m_guestHeapSuggestedBase;
}

uint32_t PS2Runtime::reserveAsyncCallbackStack(uint32_t size, uint32_t alignment)
{
    if (size == 0u)
    {
        return 0u;
    }

    const uint32_t normalizedAlignment = normalizeGuestHeapAlignment(alignment);
    const uint32_t allocSize = alignGuestHeapValue(size, kGuestHeapDefaultAlignment);
    if (allocSize == 0u)
    {
        return 0u;
    }

    std::lock_guard<std::mutex> lock(m_asyncCallbackStackMutex);
    uint32_t top = m_asyncCallbackStackTop;
    if (top > PS2_RAM_SIZE)
    {
        top = PS2_RAM_SIZE;
    }
    top &= ~(kGuestHeapDefaultAlignment - 1u);

    if (top <= allocSize)
    {
        return 0u;
    }

    uint32_t base = top - allocSize;
    base &= ~(normalizedAlignment - 1u);
    if (base < m_asyncCallbackStackFloor || base >= top)
    {
        return 0u;
    }

    m_asyncCallbackStackTop = base;
#if GHPC_DIAG
    {
        static int n = 0;
        if (n++ < 64)
            std::fprintf(stderr, "[astack] reserve #%d base=0x%x top=0x%x size=0x%x floor=0x%x\n",
                         n, (unsigned)base, (unsigned)top, (unsigned)allocSize,
                         (unsigned)m_asyncCallbackStackFloor);
    }
#endif
    return top - 0x10u;
}

uint8_t PS2Runtime::Load8(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr)
{
    try
    {
        return m_memory.read8(vaddr);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_LOAD);
        return 0;
    }
}

uint16_t PS2Runtime::Load16(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr)
{
    try
    {
        return m_memory.read16(vaddr);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_LOAD);
        return 0;
    }
}

uint32_t PS2Runtime::Load32(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr)
{
    try
    {
        return m_memory.read32(vaddr);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_LOAD);
        return 0;
    }
}

uint64_t PS2Runtime::Load64(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr)
{
    try
    {
        return m_memory.read64(vaddr);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_LOAD);
        return 0;
    }
}

__m128i PS2Runtime::Load128(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr)
{
    try
    {
        return m_memory.read128(vaddr);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_LOAD);
        return _mm_setzero_si128();
    }
}

void PS2Runtime::Store8(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint8_t value)
{
    ps2TraceGuestWrite(rdram, vaddr, 1u, value, 0u, "WRITE8", ctx);
    try
    {
        m_memory.write8(vaddr, value);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_STORE);
    }
}

void PS2Runtime::Store16(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint16_t value)
{
    ps2TraceGuestWrite(rdram, vaddr, 2u, value, 0u, "WRITE16", ctx);
    try
    {
        m_memory.write16(vaddr, value);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_STORE);
    }
}

void PS2Runtime::Store32(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint32_t value)
{
    ps2TraceGuestWrite(rdram, vaddr, 4u, value, 0u, "WRITE32", ctx);
    try
    {
        m_memory.write32(vaddr, value);
        drainCompletedDmacHandlers(rdram);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_STORE);
    }
}

void PS2Runtime::Store64(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint64_t value)
{
    ps2TraceGuestWrite(rdram, vaddr, 8u, value, 0u, "WRITE64", ctx);
    try
    {
        m_memory.write64(vaddr, value);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_STORE);
    }
}

void PS2Runtime::Store128(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, __m128i value)
{
    alignas(16) uint64_t _parts[2];
    _mm_storeu_si128(reinterpret_cast<__m128i *>(_parts), value);
    ps2TraceGuestWrite(rdram, vaddr, 16u, _parts[0], _parts[1], "WRITE128", ctx);
    try
    {
        m_memory.write128(vaddr, value);
    }
    catch (const std::exception &)
    {
        SignalException(ctx, EXCEPTION_ADDRESS_ERROR_STORE);
    }
}

void PS2Runtime::kickGifDmaChainFromMMIO(uint8_t *rdram,
                                         R5900Context *ctx,
                                         uint32_t dPcrValue,
                                         uint32_t dStatValue,
                                         uint32_t tadr,
                                         uint32_t chcr)
{
    constexpr uint32_t D_PCR = 0x1000E020u;
    constexpr uint32_t D_STAT = 0x1000E010u;
    constexpr uint32_t GIF_TADR = 0x1000A030u;
    constexpr uint32_t GIF_CHCR = 0x1000A000u;

    ps2TraceGuestWrite(rdram, D_PCR, 4u, dPcrValue, 0u, "WRITE32", ctx);
    m_memory.writeIORegister(D_PCR, dPcrValue);
    ps2TraceGuestWrite(rdram, D_STAT, 4u, dStatValue, 0u, "WRITE32", ctx);
    m_memory.writeIORegister(D_STAT, dStatValue);
    ps2TraceGuestWrite(rdram, GIF_TADR, 4u, tadr, 0u, "WRITE32", ctx);
    m_memory.writeIORegister(GIF_TADR, tadr);
    ps2TraceGuestWrite(rdram, GIF_CHCR, 4u, chcr, 0u, "WRITE32", ctx);
    if (m_memory.tryProcessNativeGifImageUploadChain(m_gs, tadr, chcr))
    {
        drainCompletedDmacHandlers(rdram);
        return;
    }
    if (m_memory.tryProcessNativeGifPackedChain(m_gs, tadr, chcr))
    {
        drainCompletedDmacHandlers(rdram);
        return;
    }
    m_memory.writeIORegister(GIF_CHCR, chcr);
    m_memory.processPendingTransfers();
    drainCompletedDmacHandlers(rdram);
}

void PS2Runtime::requestStop()
{
    m_stopRequested.store(true, std::memory_order_relaxed);
    if (m_eeScheduler)
    {
        m_eeScheduler->requestStop();
    }
}

bool PS2Runtime::isStopRequested() const
{
    return m_stopRequested.load(std::memory_order_relaxed);
}

EeScheduler &PS2Runtime::eeScheduler()
{
    return *m_eeScheduler;
}

const EeScheduler &PS2Runtime::eeScheduler() const
{
    return *m_eeScheduler;
}

void PS2Runtime::postEeEvent(EeEvent event)
{
    m_eeScheduler->postEvent(event);
}

bool PS2Runtime::eeCheckpointDue(uint32_t cycles) noexcept
{
    return m_eeScheduler->checkpointDue(cycles);
}

[[noreturn]] void PS2Runtime::eeWaitVSyncTicks(uint32_t ticks, uint32_t resumePc)
{
    const uint64_t currentTick = m_eeScheduler->currentVSyncTick();
    const uint64_t waitTicks = std::max<uint64_t>(1u, ticks);
    m_eeScheduler->waitVSync(currentTick + waitTicks - 1u,
                             0,
                             [resumePc](R5900Context &context)
                             {
                                 context.pc = resumePc;
                             });
}

void PS2Runtime::addEeExitHandler(int threadId, uint32_t function, uint32_t argument)
{
    std::lock_guard lock(m_eeKernelStateMutex);
    m_eeExitHandlers[threadId].push_back({function, argument});
}

std::vector<PS2Runtime::EeExitHandlerRegistration> PS2Runtime::takeEeExitHandlers(int threadId)
{
    std::lock_guard lock(m_eeKernelStateMutex);
    auto it = m_eeExitHandlers.find(threadId);
    if (it == m_eeExitHandlers.end())
    {
        return {};
    }
    auto handlers = std::move(it->second);
    m_eeExitHandlers.erase(it);
    return handlers;
}

void PS2Runtime::removeEeExitHandlers(int threadId)
{
    std::lock_guard lock(m_eeKernelStateMutex);
    m_eeExitHandlers.erase(threadId);
}

bool PS2Runtime::findEeSyscallOverride(uint32_t syscallNumber, uint32_t &handler) const
{
    std::lock_guard lock(m_eeKernelStateMutex);
    const auto it = m_eeSyscallOverrides.find(syscallNumber);
    if (it == m_eeSyscallOverrides.end())
    {
        return false;
    }
    handler = it->second;
    return true;
}

void PS2Runtime::setEeSyscallOverride(uint8_t *rdram, uint32_t syscallNumber, uint32_t handler)
{
    constexpr uint32_t kTableBase = 0x80011F80u & 0x1FFFFFFFu;
    constexpr uint32_t kMirrorLimit = 0x00080000u;
    const int64_t offset = static_cast<int64_t>(static_cast<int32_t>(syscallNumber)) * 4;
    const int64_t address = static_cast<int64_t>(kTableBase) + offset;

    std::lock_guard lock(m_eeKernelStateMutex);
    if (handler == 0u)
    {
        m_eeSyscallOverrides.erase(syscallNumber);
    }
    else
    {
        m_eeSyscallOverrides[syscallNumber] = handler;
    }
    if (!rdram || address < 0 || address + 4 > kMirrorLimit)
    {
        return;
    }
    const uint32_t guestAddress = static_cast<uint32_t>(address);
    std::memcpy(rdram + guestAddress, &handler, sizeof(handler));
    if (handler == 0u)
    {
        m_eeSyscallMirrorAddresses.erase(guestAddress);
    }
    else
    {
        m_eeSyscallMirrorAddresses.insert(guestAddress);
    }
}

void PS2Runtime::initializeEeKernelState(uint8_t *rdram)
{
    if (!rdram)
    {
        return;
    }
    constexpr uint32_t kTableGuestBase = 0x80011F80u;
    constexpr uint32_t kTableBase = kTableGuestBase & 0x1FFFFFFFu;
    constexpr uint32_t kMirrorLimit = 0x00080000u;
    constexpr uint32_t kProbeBase = 0x000002F0u;

    std::lock_guard lock(m_eeKernelStateMutex);
    for (const uint32_t address : m_eeSyscallMirrorAddresses)
    {
        const uint32_t zero = 0u;
        std::memcpy(rdram + address, &zero, sizeof(zero));
    }
    m_eeSyscallMirrorAddresses.clear();
    const uint32_t high = kTableGuestBase >> 16;
    const uint32_t low = kTableGuestBase & 0xFFFFu;
    std::memcpy(rdram + kProbeBase, &high, sizeof(high));
    std::memcpy(rdram + kProbeBase + 8u, &low, sizeof(low));
    m_eeSyscallMirrorAddresses.insert(kProbeBase);
    m_eeSyscallMirrorAddresses.insert(kProbeBase + 8u);

    for (const auto &[syscallNumber, handler] : m_eeSyscallOverrides)
    {
        const int64_t offset = static_cast<int64_t>(static_cast<int32_t>(syscallNumber)) * 4;
        const int64_t address = static_cast<int64_t>(kTableBase) + offset;
        if (address < 0 || address + 4 > kMirrorLimit)
        {
            continue;
        }
        const uint32_t guestAddress = static_cast<uint32_t>(address);
        std::memcpy(rdram + guestAddress, &handler, sizeof(handler));
        m_eeSyscallMirrorAddresses.insert(guestAddress);
    }
}

void PS2Runtime::HandleIntegerOverflow(R5900Context *ctx)
{
    raiseCop0Exception(ctx, EXCEPTION_INTEGER_OVERFLOW);
}

void PS2Runtime::run()
{
    m_stopRequested.store(false, std::memory_order_relaxed);
    ps2_stubs::resetSifState();
    resetIop();
    ps2_stubs::resetAudioStubState();
    ps2_stubs::resetMpegStubState();
    initializeEeKernelState(m_memory.getRDRAM());
    m_cpuContext.r[4] = _mm_setzero_si128();
    m_cpuContext.r[5] = _mm_setzero_si128();
    m_cpuContext.r[29] = _mm_set_epi64x(0, static_cast<int64_t>(PS2_RAM_SIZE - 0x10u));
    m_debugPc.store(m_cpuContext.pc, std::memory_order_relaxed);
    m_debugRa.store(static_cast<uint32_t>(_mm_extract_epi32(m_cpuContext.r[31], 0)), std::memory_order_relaxed);
    m_debugSp.store(static_cast<uint32_t>(_mm_extract_epi32(m_cpuContext.r[29], 0)), std::memory_order_relaxed);
    m_debugGp.store(static_cast<uint32_t>(_mm_extract_epi32(m_cpuContext.r[28], 0)), std::memory_order_relaxed);

    RUNTIME_LOG("Starting execution at address 0x" << std::hex << m_cpuContext.pc << std::dec);

    // A blank image to use as a framebuffer
    Image blank = GenImageColor(FB_WIDTH, FB_HEIGHT, BLANK);
    Texture2D frameTex = LoadTextureFromImage(blank);
    UnloadImage(blank);

    std::atomic<bool> gameThreadFinished{false};

    std::thread gameThread([&]()
                           {
        ThreadNaming::SetCurrentThreadName("GameThread");
        try
        {
            m_eeScheduler->reset(m_memory.getRDRAM(), m_cpuContext);
            m_eeScheduler->run();
            uint32_t pc = m_debugPc.load(std::memory_order_relaxed);
            RUNTIME_LOG("Game thread returned. PC=0x" << std::hex << pc
                      << " RA=0x" << static_cast<uint32_t>(_mm_extract_epi32(m_cpuContext.r[31], 0)) << std::dec << std::endl);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error during program execution: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Error during program execution: unknown exception" << std::endl;
        }
        gameThreadFinished.store(true, std::memory_order_release); });

    uint64_t tick = 0;
    while (!isStopRequested() && !gameThreadFinished.load(std::memory_order_acquire))
    {
        PS2_IF_AGRESSIVE_LOGS({
            tick++;
            if ((tick % 120) == 0)
            {
                uint64_t curDma = m_memory.dmaStartCount();
                uint64_t curGif = m_memory.gifCopyCount();
                uint64_t curGs = m_memory.gsWriteCount();
                uint64_t curVif = m_memory.vifWriteCount();
                const GSRegisters &gs = m_memory.gs();
                const uint32_t dbgPc = m_debugPc.load(std::memory_order_relaxed);
                const uint32_t dbgRa = m_debugRa.load(std::memory_order_relaxed);
                const uint32_t dbgSp = m_debugSp.load(std::memory_order_relaxed);
                const uint32_t dbgGp = m_debugGp.load(std::memory_order_relaxed);
                const auto eeSnapshot = m_eeScheduler->snapshot();

                RUNTIME_LOG("[run:tick] tick=" << tick
                                               << " pc=0x" << std::hex << dbgPc
                                               << " ra=0x" << dbgRa
                                               << " sp=0x" << dbgSp
                                               << " gp=0x" << dbgGp
                                               << " dispfb1=0x" << gs.dispfb1
                                               << " display1=0x" << gs.display1
                                               << std::dec
                                               << " activeThreads=" << eeSnapshot.threads.size()
                                               << " dma=" << curDma
                                               << " gif=" << curGif
                                               << " gsw=" << curGs
                                               << " vif=" << curVif
                                               << std::endl);
            }
        });
        uint32_t presentWidth = FB_WIDTH;
        uint32_t presentHeight = DEFAULT_DISPLAY_HEIGHT;
        UploadFrame(frameTex, this, presentWidth, presentHeight);

        BeginDrawing();
        ClearBackground(BLACK);
        const float srcWidth = static_cast<float>(std::max<uint32_t>(1u, presentWidth));
        const float srcHeight = static_cast<float>(std::max<uint32_t>(1u, presentHeight));
        const float screenWidth = static_cast<float>(GetScreenWidth());
        const float screenHeight = static_cast<float>(GetScreenHeight());
        const float scale = std::min(screenWidth / srcWidth, screenHeight / srcHeight);
        const float dstWidth = srcWidth * scale;
        const float dstHeight = srcHeight * scale;
        const Rectangle srcRect{0.0f, 0.0f, srcWidth, srcHeight};
        const Rectangle dstRect{
            (screenWidth - dstWidth) * 0.5f,
            (screenHeight - dstHeight) * 0.5f,
            dstWidth,
            dstHeight};
        DrawTexturePro(frameTex, srcRect, dstRect, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        if (m_debugUiInitialized && m_debugUiDrawCallback)
        {
            m_debugUiDrawCallback(*this, m_debugUiUserData);
        }
        EndDrawing();

        if (WindowShouldClose())
        {
            RUNTIME_LOG("[run] window close requested, breaking out of loop");
            requestStop();
            break;
        }
    }

    requestStop();
    if (gameThread.joinable())
    {
        gameThread.join();
    }

    if (m_debugUiInitialized && m_debugUiShutdownCallback)
    {
        m_debugUiShutdownCallback(*this, m_debugUiUserData);
        m_debugUiInitialized = false;
    }
    UnloadTexture(frameTex);
    CloseWindow();

    RUNTIME_LOG("[run] exiting loop");
}
