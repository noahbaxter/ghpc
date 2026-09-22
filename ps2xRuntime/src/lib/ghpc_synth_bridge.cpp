#include "ghpc_synth_bridge.h"

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"
#include "ps2x/iop/synth_engine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#if defined(GHPC_ENABLE_VULKAN)
#include <SDL3/SDL.h>
#endif

namespace ghpc::synth
{
    namespace
    {
        constexpr uint32_t kCtlDispatch = 0x00268760u; // CtlDispatch__7SynthEE(cmd, data, len)
        constexpr uint32_t kOutputRate = 48000u;
        // Every payload of one delivery lives here until CtlDispatch has read
        // it. Each handler copies its argument out before returning, so the
        // ring only has to outlast one sequence.
        constexpr uint32_t kRingBytes = 0x4000u;
        constexpr uint32_t kSlotBytes = 0x40u;

        uint32_t g_ring = 0u;
        bool g_justDelivered = false;

        // GHPC_AUDIO_WAV=path writes the mixed output as 16-bit stereo WAV,
        // which is how a run with nobody listening proves there was sound.
        struct WavTap
        {
            std::FILE *file = nullptr;
            uint32_t dataBytes = 0;
            uint32_t sinceHeader = 0;

            void open(const char *path)
            {
                file = std::fopen(path, "wb");
                if (file)
                {
                    writeHeader();
                }
            }

            void writeHeader()
            {
                auto u32 = [this](uint32_t v) { std::fwrite(&v, 4, 1, file); };
                auto u16 = [this](uint16_t v) { std::fwrite(&v, 2, 1, file); };
                std::fseek(file, 0, SEEK_SET);
                std::fwrite("RIFF", 1, 4, file);
                u32(36u + dataBytes);
                std::fwrite("WAVEfmt ", 1, 8, file);
                u32(16u);
                u16(1u);
                u16(2u);
                u32(kOutputRate);
                u32(kOutputRate * 4u);
                u16(4u);
                u16(16u);
                std::fwrite("data", 1, 4, file);
                u32(dataBytes);
                std::fseek(file, 0, SEEK_END);
            }

            void write(const float *samples, size_t count)
            {
                if (!file)
                {
                    return;
                }
                std::vector<int16_t> pcm(count);
                for (size_t i = 0; i < count; ++i)
                {
                    pcm[i] = static_cast<int16_t>(samples[i] * 32767.0f);
                }
                std::fwrite(pcm.data(), 2, count, file);
                dataBytes += static_cast<uint32_t>(count * 2u);
                sinceHeader += static_cast<uint32_t>(count * 2u);
                if (sinceHeader >= kOutputRate * 4u)
                {
                    sinceHeader = 0;
                    writeHeader();
                    std::fflush(file);
                }
            }
        };

        WavTap g_wav;

#if defined(GHPC_ENABLE_VULKAN)
        void SDLCALL feed(void *, SDL_AudioStream *stream, int additional, int)
        {
            static std::vector<float> buffer;
            const size_t frames = static_cast<size_t>(additional) / (2u * sizeof(float));
            if (frames == 0u)
            {
                return;
            }
            buffer.resize(frames * 2u);
            ps2x::iop::synth::render(buffer.data(), frames, kOutputRate);
            g_wav.write(buffer.data(), buffer.size());
            SDL_PutAudioStreamData(stream, buffer.data(), static_cast<int>(buffer.size() * sizeof(float)));
        }
#endif

        void startAudio()
        {
            const char *off = std::getenv("GHPC_AUDIO");
            if (off && off[0] == '0')
            {
                std::fprintf(stderr, "[ghpc/audio] off (GHPC_AUDIO=0)\n");
                return;
            }
            if (const char *wav = std::getenv("GHPC_AUDIO_WAV"))
            {
                g_wav.open(wav);
            }
#if defined(GHPC_ENABLE_VULKAN)
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            {
                std::fprintf(stderr, "[ghpc/audio] SDL audio init failed: %s\n", SDL_GetError());
                return;
            }
            SDL_AudioSpec spec{};
            spec.format = SDL_AUDIO_F32;
            spec.channels = 2;
            spec.freq = static_cast<int>(kOutputRate);
            SDL_AudioStream *stream =
                SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, nullptr);
            if (!stream)
            {
                std::fprintf(stderr, "[ghpc/audio] no output device: %s\n", SDL_GetError());
                return;
            }
            SDL_ResumeAudioStreamDevice(stream);
            std::fprintf(stderr, "[ghpc/audio] SDL3 output, %u Hz stereo%s\n", kOutputRate,
                         g_wav.file ? ", tapped to GHPC_AUDIO_WAV" : "");
#else
            std::fprintf(stderr, "[ghpc/audio] built without SDL3, no output\n");
#endif
        }
    }

    void deliverReplies(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t reentryPc)
    {
        static std::once_flag started;
        std::call_once(started, startAudio);

        if (g_justDelivered)
        {
            g_justDelivered = false;
            return;
        }

        static uint32_t s_polls = 0;
        if ((++s_polls % 300u) == 0u)
        {
            const ps2x::iop::synth::Stats s = ps2x::iop::synth::stats();
            std::fprintf(stderr,
                         "[ghpc/audio] streams=%llu playing=%llu bytesIn=%llu requests=%llu "
                         "underruns=%llu framesOut=%llu samples=%llu voices=%llu starts=%llu\n",
                         (unsigned long long)s.streams, (unsigned long long)s.playing,
                         (unsigned long long)s.bytesIn, (unsigned long long)s.requests,
                         (unsigned long long)s.underruns, (unsigned long long)s.framesOut,
                         (unsigned long long)s.samples, (unsigned long long)s.voicesPlaying,
                         (unsigned long long)s.voiceStarts);
            static const bool dump = std::getenv("GHPC_SYNTH_DUMP") != nullptr;
            if (dump)
            {
                std::fprintf(stderr, "%s", ps2x::iop::synth::dump().c_str());
            }
        }

        std::vector<ps2x::iop::synth::Reply> replies;
        if (ps2x::iop::synth::takeReplies(replies) == 0u)
        {
            return;
        }
        if (g_ring == 0u)
        {
            g_ring = runtime->guestMalloc(kRingBytes, 64u);
            if (g_ring == 0u)
            {
                std::fprintf(stderr, "[ghpc/audio] no guest memory for replies\n");
                return;
            }
        }
        const size_t maxReplies = kRingBytes / kSlotBytes;
        if (replies.size() > maxReplies)
        {
            // What does not fit is regenerated next poll: data requests are
            // recomputed from buffer levels, reports from the play position.
            replies.resize(maxReplies);
        }

        std::vector<GuestInvocation> calls;
        calls.reserve(replies.size());
        for (size_t i = 0; i < replies.size(); ++i)
        {
            const ps2x::iop::synth::Reply &r = replies[i];
            const uint32_t slot = g_ring + static_cast<uint32_t>(i) * kSlotBytes;
            std::memcpy(getMemPtr(rdram, slot), r.data.data(), std::min<size_t>(r.len, kSlotBytes));
            GuestInvocation call{};
            call.kind = GuestInvocationKind::HleCall;
            call.context = *ctx;
            call.context.pc = kCtlDispatch;
            SET_GPR_U32(&call.context, 4, r.cmd);
            SET_GPR_U32(&call.context, 5, r.len ? slot : 0u);
            SET_GPR_U32(&call.context, 6, r.len);
            SET_GPR_U32(&call.context, 7, 0u);
            SET_GPR_U32(&call.context, 29, 0u);
            SET_GPR_U32(&call.context, 31, 0u);
            calls.push_back(std::move(call));
        }
        g_justDelivered = true;
        ctx->pc = reentryPc;
        runtime->eeScheduler().invokeCurrentSequence(std::move(calls));
    }
}
