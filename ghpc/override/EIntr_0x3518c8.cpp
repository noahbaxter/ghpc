// Override of work/output/EIntr_0x3518c8.cpp (EIntr, 0x3518c8 - 0x3518e0).
//
// Why: proof body for the override layer. Behaviour is identical to the
// generated translation; the only addition is one stderr line on first call so
// a run log can show the override was linked instead of the generated file.
// Evidence: EIntr has 8 direct call sites including VSync_0x34b770 and
// SyncDCache_0x34bb20, both on the boot path, so the marker fires every run.
//
// Semantics from the disassembly: v0 = (Status & 0x10000) != 0, then `ei`.
// Returns whether interrupts were already enabled, and enables them.
#include <cstdio>
#include <type_traits>
#include "ps2_runtime_macros.h"
#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"

#ifdef PS2_FUNCTION_LOG_TRACKER
#include "ps2_log.h"
#endif

void EIntr_0x3518c8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
#ifdef PS2_FUNCTION_LOG_TRACKER
    PS_LOG_ENTRY("EIntr_0x3518c8");
#endif
    static bool s_announced = false;
    if (!s_announced) {
        s_announced = true;
        std::fprintf(stderr, "[override] EIntr_0x3518c8 active\n");
    }

    const uint32_t wasEnabled = (ctx->cop0_status & 0x10000u) ? 1u : 0u;
    ctx->cop0_status |= 0x10000u;
    SET_GPR_U64(ctx, 2, (uint64_t)wasEnabled);

    const uint32_t jumpTarget = GPR_U32(ctx, 31);
    ctx->pc = jumpTarget;
#if defined(PS2X_STRICT_RETURN_DIAGNOSTICS) && PS2X_STRICT_RETURN_DIAGNOSTICS
    (void)runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x3518D8u, 0u, PS2Runtime::GuestBranchKind::Return, "JR $ra");
#else
    (void)rdram; (void)runtime;
#endif
}

// If the recompiler ever changes the function ABI, the header declaration and
// this definition become two overloads and taking the address is ambiguous.
static_assert(std::is_same<decltype(&EIntr_0x3518c8), PS2Runtime::RecompiledFunction>::value,
              "EIntr_0x3518c8 override no longer matches the recompiled signature");
