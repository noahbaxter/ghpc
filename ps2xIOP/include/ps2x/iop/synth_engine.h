#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The IOP half of GH2's synth, which on hardware is an IRX this port does not
// have. SynthService (src/modules/synth.cpp) feeds it the EE's commands; the
// host audio device pulls mixed samples out of it; and the EE side drains the
// calls it wants to make back into CtlDispatch__7SynthEE.
//
// Two kinds of sound. Streams: StreamEE reads the .vgs, deinterleaves it and
// ships each channel's raw PS-ADPCM on request, so the decode, the resample
// and the mix happen here. Samples: the EE uploads PS-ADPCM into SPU RAM once
// and then plays instances of it by address, as SPU2 voices would.
namespace ps2x::iop::synth
{
    // One call into CtlDispatch__7SynthEE(cmd, data, len). `data` is copied
    // into guest memory by whoever delivers it; `len` is its size in bytes.
    struct Reply
    {
        uint32_t cmd = 0;
        uint32_t len = 0;
        std::array<uint32_t, 16> data{};
    };

    // EE -> IOP, called by SynthService with the record payload already read.
    void streamInfo(uint32_t id, uint32_t channels, const uint32_t *freqs);
    void streamPlay(uint32_t id);
    void streamStop(uint32_t id);
    void streamDestroy(uint32_t id);
    void streamVolumePan(uint32_t id, uint32_t channel, int16_t volume, int16_t pan);
    void streamSpeed(uint32_t id, int16_t speedMilli);
    void streamDataTarget(uint32_t id, uint32_t channel);
    void streamData(const uint8_t *bytes, uint32_t len);
    void streamDataBatchEnd(uint32_t id);

    // SPU RAM, filled by SPUSendPoll's 0xC8/0xC9 pairs.
    void spuWrite(uint32_t dest, const uint8_t *bytes, uint32_t len);

    // Sample instances (SampleInstEE), keyed on the EE's handle.
    void sampleCreate(uint32_t handle, uint32_t spuAddr, uint32_t rate);
    void sampleDestroy(uint32_t handle);
    void sampleStart(uint32_t handle);
    void sampleStop(uint32_t handle);
    void samplePause(uint32_t handle, bool pause);
    void sampleVolumePan(uint32_t handle, int16_t volume, int16_t pan);
    void sampleSpeed(uint32_t handle, int32_t speedMilli);
    void sampleStartOffset(uint32_t handle, uint32_t samples);

    // SPU2 reverb, one unit per core. `mode` is libsd's SD_REV_MODE_*, depth
    // the core's reverb output volume per side.
    void fxMode(uint32_t core, uint32_t mode, int16_t depthL, int16_t depthR);
    // Which core a stream channel or sample sends to (-1 none), and whether
    // the channel's send is on. Samples send whenever they have a core.
    void streamFxCore(uint32_t id, uint32_t channel, int16_t core);
    void streamFx(uint32_t id, uint32_t channel, bool on);
    void sampleFxCore(uint32_t handle, int32_t core);

    // A reply that could not ride the RPC result it was earned on.
    void queueReply(const Reply &reply);

    // EE side, once per SynthEE::Poll: data requests, position reports and
    // anything queued. Returns the number appended.
    size_t takeReplies(std::vector<Reply> &out);

    // Host audio thread: interleaved stereo float, `frames` frames at `rate`.
    void render(float *out, size_t frames, uint32_t rate);

    struct Stats
    {
        uint64_t streams = 0;
        uint64_t playing = 0;
        uint64_t bytesIn = 0;
        uint64_t requests = 0;
        uint64_t underruns = 0;
        uint64_t framesOut = 0;
        uint64_t samples = 0;
        uint64_t voicesPlaying = 0;
        uint64_t voiceStarts = 0;
    };
    Stats stats();

    // One line per stream and per sounding voice, for GHPC_SYNTH_DUMP.
    std::string dump();
}
