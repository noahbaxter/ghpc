#include "Common.h"
#include "System.h"

namespace ps2_stubs
{
    void builtin_set_imask(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int logCount = 0;
        if (logCount < 8)
        {
            RUNTIME_LOG("ps2_stub builtin_set_imask");
            ++logCount;
        }
        setReturnS32(ctx, 0);
    }

    void sceIDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceSDC(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void exit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // A guest exit or abort used to stop the run in complete silence, which
        // reads as a crash with no message and leaves no host crash report.
        // Say who called it: abort() reaches here via _exit with code 1, so the
        // return address is the only handle on where the game gave up.
        std::fprintf(stderr,
                     "[guest] exit(%d) called from ra=0x%08x pc=0x%08x. Run is stopping.\n",
                     (int)getRegU32(ctx, 4), (unsigned)GPR_U32(ctx, 31), (unsigned)ctx->pc);
        // ra alone only names the immediate caller, and funnels like
        // yy_fatal_error look identical from every site that reaches them.
        // The caller saved its own return address in its frame, so the top of
        // the guest stack still says which site gave up. Feed these to
        // ghpc/scripts/whereis.sh.
        {
            const uint32_t sp = GPR_U32(ctx, 29);
            std::fprintf(stderr, "[guest] sp=0x%08x, stack words:", sp);
            for (uint32_t i = 0; i < 8u; ++i)
            {
                uint32_t word = 0;
                if (const uint8_t *p = getConstMemPtr(rdram, sp + i * 4u))
                    std::memcpy(&word, p, sizeof(word));
                std::fprintf(stderr, " %08x", word);
            }
            std::fprintf(stderr, "\n");
        }
        std::fflush(stderr);
        if (runtime)
        {
            runtime->requestStop();
        }
        setReturnS32(ctx, 0);
    }

    void getpid(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceSetBrokenLink(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSetBrokenLink", rdram, ctx, runtime);
    }

    void sceSetPtm(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceSetPtm", rdram, ctx, runtime);
    }

    void sceDevVif0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceDevVu0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

}
