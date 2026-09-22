#pragma once

#include <cstdint>

struct R5900Context;
class PS2Runtime;

namespace ghpc::synth
{
    // Called on fresh entry to SynthEE::Poll (0x2685a8). If the IOP synth has
    // calls queued for CtlDispatch__7SynthEE, it copies their payloads into
    // guest memory, points ctx back at `reentryPc` and runs them as one
    // invocation sequence, after which the thread re-enters Poll. That throws
    // and does not return. Returns normally when there is nothing to deliver,
    // including on the re-entry straight after a delivery.
    //
    // Also starts the host audio device the first time it runs.
    void deliverReplies(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t reentryPc);
}
