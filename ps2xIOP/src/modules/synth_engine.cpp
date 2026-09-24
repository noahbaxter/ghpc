#include "ps2x/iop/synth_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

namespace ps2x::iop::synth
{
    namespace
    {
        // CtlDispatch_impl__7SynthEE's jump table at 0x4EB680, the entries
        // this engine sends:
        //   0 -> StreamEE::DispatchStreamData({id, channel, bytes}), which
        //        queues a request SendToIOP answers with 0x198 + 0x199 and
        //        closes with 0x19A. SendToIOP clamps it to 0x1000 bytes and to
        //        what its own buffer holds, and skips a channel at or past the
        //        reader's channel count, so a request can go unanswered.
        //   3 -> StreamEE::Dispatch(id, 3, pos): Poll converts pos * 28 samples
        //        to ms against channel 0's rate, adds the start offset and
        //        snaps the stream's timer if it is more than 100 ms out.
        //   4 -> StreamEE::DispatchSlipOffset({id, short channel, int samples})
        //        stores a slip channel's offset from the song, which
        //        GetSlipOffset reads for ChannelData::SetSlipTrackSpeed's
        //        max_slip clamp.
        constexpr uint32_t kReplyStreamData = 0u;
        constexpr uint32_t kReplyStreamPos = 3u;
        constexpr uint32_t kReplySlipOffset = 4u;

        const auto kProcessStart = std::chrono::steady_clock::now();

        constexpr uint32_t kAdpcmBlock = 16u;
        constexpr uint32_t kAdpcmSamples = 28u;
        constexpr uint32_t kRequestBytes = 0x1000u;
        // About 1.8 s of a 32 kHz channel. Enough to ride out a slow frame;
        // small enough that a stream prepared and never played costs little.
        constexpr uint32_t kTargetBytes = 0x8000u;
        constexpr uint32_t kMaxRequestsPerPoll = 4u;
        // Stereo pairs and virtual channels are fed by duplicates of a real
        // channel's data, so a request for one is skipped. After this many
        // batches where it got nothing and a sibling got data, stop asking.
        constexpr uint32_t kDeadStrikes = 3u;
        // Samples a channel must hold before a stream starts consuming.
        constexpr size_t kStartSamples = kAdpcmSamples * 64u;
        constexpr uint32_t kPosReportEvery = 8u;

        struct Channel
        {
            std::deque<int16_t> pcm;
            int32_t hist1 = 0;
            int32_t hist2 = 0;
            double phase = 0.0;
            uint32_t freq = 0;
            uint32_t inflight = 0;
            uint32_t strikes = 0;
            bool gotData = false;
            bool gotDataThisBatch = false;
            bool dead = false;
            float volume = 1.0f;
            float pan = 0.0f;
            int fxCore = -1;
            bool fx = false;
            // Whammy. A slip channel plays at its own speed on top of the
            // stream's, which bends its pitch and lets it fall behind the
            // song; `pos` is its own count of source samples consumed, so
            // pos - Stream::consumed is its offset. It does not stall the
            // stream when short, it only goes quiet.
            bool slipping = false;
            float slip = 1.0f;
            int64_t pos = 0;
            int64_t reportedOffset = 0;

            size_t bufferedBytes() const { return pcm.size() / kAdpcmSamples * kAdpcmBlock; }
        };

        struct Stream
        {
            uint32_t id = 0;
            std::vector<Channel> channels;
            bool playing = false;
            bool started = false;
            float speed = 1.0f;
            uint64_t consumed = 0;
            uint32_t polls = 0;
        };

        constexpr uint32_t kSpuRamBytes = 2u * 1024u * 1024u;
        constexpr uint32_t kReplySampleDone = 11u; // SampleInstEE::SetDone(handle)

        // SPU2 block flags, byte 1 of each ADPCM block.
        constexpr uint8_t kFlagLoopEnd = 0x01u;
        constexpr uint8_t kFlagRepeat = 0x02u;
        constexpr uint8_t kFlagLoopStart = 0x04u;

        struct Voice
        {
            uint32_t spuAddr = 0;
            uint32_t rate = 0;
            uint32_t startSamples = 0;
            float volume = 1.0f;
            float pan = 0.0f;
            float speed = 1.0f;
            int fxCore = -1;
            bool playing = false;
            bool paused = false;

            uint32_t addr = 0;
            uint32_t loopAddr = 0;
            bool ending = false;
            int32_t hist1 = 0;
            int32_t hist2 = 0;
            int16_t buf[kAdpcmSamples]{};
            uint32_t bufPos = kAdpcmSamples;
            float prev = 0.0f;
            float cur = 0.0f;
            double phase = 0.0;
        };

        // SPU reverb presets, register order
        //   dAPF1 dAPF2 vIIR vCOMB1 vCOMB2 vCOMB3 vCOMB4 vWALL
        //   vAPF1 vAPF2 mLSAME mRSAME mLCOMB1 mRCOMB1 mLCOMB2 mRCOMB2
        //   dLSAME dRSAME mLDIFF mRDIFF mLCOMB3 mRCOMB3 mLCOMB4 mRCOMB4
        //   dLDIFF dRDIFF mLAPF1 mRAPF1 mLAPF2 mRAPF2 vLIN vRIN
        // copied from psx-spx, "SPU Reverb Examples", with the work area size
        // in bytes. SPU2 keeps the PS1 algorithm and libsd's modes line up with
        // these for ROOM through HALL (1-5); SPACE, ECHO and DELAY (6-8) are
        // psx-spx's Space Echo, Chaos Echo and Delay, and PIPE (9) is taken to
        // be Half Echo, the one preset left over. libsd's delay and feedback
        // adjustments for ECHO and DELAY are not applied.
        struct ReverbPreset
        {
            uint32_t bytes;
            uint16_t r[32];
        };
        constexpr ReverbPreset kReverbPresets[10] = {
            {0x10, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
                    0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0}},
            {0x26C0, {0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80,
                      0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF,
                      0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
                      0x0000, 0x0000, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000}},
            {0x1F40, {0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00,
                      0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF, 0x0372, 0x0266,
                      0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7,
                      0x018F, 0x00B5, 0x00B4, 0x0080, 0x004C, 0x0026, 0x8000, 0x8000}},
            {0x4840, {0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0,
                      0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F, 0x07A2, 0x0616,
                      0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7,
                      0x042F, 0x0265, 0x0264, 0x01B2, 0x0100, 0x0080, 0x8000, 0x8000}},
            {0x6FE0, {0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680,
                      0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C, 0x0BD9, 0x0973,
                      0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2,
                      0x05EA, 0x031D, 0x031C, 0x0238, 0x0154, 0x00AA, 0x8000, 0x8000}},
            {0xADE0, {0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000,
                      0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD, 0x11BC, 0x0DC1,
                      0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD,
                      0x09C2, 0x05C1, 0x05C0, 0x041A, 0x0274, 0x013A, 0x8000, 0x8000}},
            {0xF6C0, {0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000,
                      0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B, 0x1BC2, 0x16B2,
                      0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D,
                      0x1056, 0x0AE1, 0x0AE0, 0x07A2, 0x0464, 0x0232, 0x8000, 0x8000}},
            {0x18040, {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100,
                       0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
                       0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
                       0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000}},
            {0x18040, {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000,
                       0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
                       0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
                       0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000}},
            {0x3C00, {0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500,
                      0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF, 0x02B0, 0x01D7,
                      0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059,
                      0x01A0, 0x00E3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000}},
        };

        // psx-spx, "SPU Reverb Formula": one left and one right step per
        // two output samples, over a circular buffer of signed 16-bit samples.
        // Offsets are in 8-byte units; [m-2] is one sample behind m.
        struct Reverb
        {
            uint32_t mode = 0;
            float depthL = 0.0f;
            float depthR = 0.0f;
            std::vector<float> buf;
            uint32_t pos = 0;
            float accL = 0.0f, accR = 0.0f;
            bool half = false;
            float prevL = 0.0f, prevR = 0.0f, curL = 0.0f, curR = 0.0f;

            void setMode(uint32_t m)
            {
                m &= 0xffu; // SD_REV_MODE_CLEAR_WA is 0x100
                if (m >= 10u)
                {
                    m = 0u;
                }
                if (m == mode && !buf.empty())
                {
                    return;
                }
                mode = m;
                buf.assign(std::max<uint32_t>(kReverbPresets[m].bytes / 2u, 8u), 0.0f);
                pos = 0;
                prevL = prevR = curL = curR = 0.0f;
            }

            bool active() const { return mode != 0u && (depthL != 0.0f || depthR != 0.0f); }

            float &at(int32_t off)
            {
                const int32_t n = static_cast<int32_t>(buf.size());
                int32_t i = (static_cast<int32_t>(pos) + off) % n;
                if (i < 0)
                {
                    i += n;
                }
                return buf[static_cast<size_t>(i)];
            }

            static float sat(float v) { return std::clamp(v, -32768.0f, 32767.0f); }

            void step(float inL, float inR)
            {
                const uint16_t *r = kReverbPresets[mode].r;
                auto v = [r](int i) { return static_cast<int16_t>(r[i]) / 32768.0f; };
                auto m = [r](int i) { return static_cast<int32_t>(r[i]) * 4; };
                const int32_t dAPF1 = m(0), dAPF2 = m(1);
                const float vIIR = v(2), vC1 = v(3), vC2 = v(4), vC3 = v(5), vC4 = v(6), vWALL = v(7);
                const float vAPF1 = v(8), vAPF2 = v(9);
                const int32_t mLSAME = m(10), mRSAME = m(11), mLC1 = m(12), mRC1 = m(13);
                const int32_t mLC2 = m(14), mRC2 = m(15), dLSAME = m(16), dRSAME = m(17);
                const int32_t mLDIFF = m(18), mRDIFF = m(19), mLC3 = m(20), mRC3 = m(21);
                const int32_t mLC4 = m(22), mRC4 = m(23), dLDIFF = m(24), dRDIFF = m(25);
                const int32_t mLAPF1 = m(26), mRAPF1 = m(27), mLAPF2 = m(28), mRAPF2 = m(29);
                const float vLIN = v(30), vRIN = v(31);

                const float lin = vLIN * inL;
                const float rin = vRIN * inR;
                at(mLSAME) = sat((lin + at(dLSAME) * vWALL - at(mLSAME - 1)) * vIIR + at(mLSAME - 1));
                at(mRSAME) = sat((rin + at(dRSAME) * vWALL - at(mRSAME - 1)) * vIIR + at(mRSAME - 1));
                at(mLDIFF) = sat((lin + at(dRDIFF) * vWALL - at(mLDIFF - 1)) * vIIR + at(mLDIFF - 1));
                at(mRDIFF) = sat((rin + at(dLDIFF) * vWALL - at(mRDIFF - 1)) * vIIR + at(mRDIFF - 1));
                float lout = vC1 * at(mLC1) + vC2 * at(mLC2) + vC3 * at(mLC3) + vC4 * at(mLC4);
                float rout = vC1 * at(mRC1) + vC2 * at(mRC2) + vC3 * at(mRC3) + vC4 * at(mRC4);
                lout = lout - vAPF1 * at(mLAPF1 - dAPF1);
                at(mLAPF1) = sat(lout);
                lout = lout * vAPF1 + at(mLAPF1 - dAPF1);
                rout = rout - vAPF1 * at(mRAPF1 - dAPF1);
                at(mRAPF1) = sat(rout);
                rout = rout * vAPF1 + at(mRAPF1 - dAPF1);
                lout = lout - vAPF2 * at(mLAPF2 - dAPF2);
                at(mLAPF2) = sat(lout);
                lout = lout * vAPF2 + at(mLAPF2 - dAPF2);
                rout = rout - vAPF2 * at(mRAPF2 - dAPF2);
                at(mRAPF2) = sat(rout);
                rout = rout * vAPF2 + at(mRAPF2 - dAPF2);
                prevL = curL;
                prevR = curR;
                curL = lout * depthL;
                curR = rout * depthR;
                pos = (pos + 1u) % static_cast<uint32_t>(buf.size());
            }

            // In and out are the mix's float scale. Output rate is twice the
            // reverb's, so each step's input is the mean of two samples, and
            // the output runs one step behind: the older step on the sample
            // that ran a step, the midpoint to the newer one on the next.
            void process(const float *send, float *out, size_t frames)
            {
                for (size_t f = 0; f < frames; ++f)
                {
                    accL += send[f * 2u] * 32768.0f;
                    accR += send[f * 2u + 1u] * 32768.0f;
                    float t = 0.5f;
                    if (half)
                    {
                        step(accL * 0.5f, accR * 0.5f);
                        accL = accR = 0.0f;
                        t = 0.0f;
                    }
                    out[f * 2u] += (prevL + (curL - prevL) * t) / 32768.0f;
                    out[f * 2u + 1u] += (prevR + (curR - prevR) * t) / 32768.0f;
                    half = !half;
                }
            }
        };

        struct Engine
        {
            std::mutex mutex;
            std::map<uint32_t, std::unique_ptr<Stream>> streams;
            std::map<uint32_t, Voice> voices;
            Reverb reverb[2];
            std::vector<float> send[2];
            std::vector<uint8_t> spuRam = std::vector<uint8_t>(kSpuRamBytes, 0u);
            std::vector<Reply> queued;
            uint32_t dataId = 0;
            uint32_t dataChannel = 0;
            bool dataTargetValid = false;
            uint64_t framesAtLastPoll = 0;
            Stats stats;
        };

        Engine &engine()
        {
            static Engine e;
            return e;
        }

        bool syncEnabled()
        {
            static const bool on = []() {
                const char *v = std::getenv("GHPC_SYNTH_SYNC");
                return v == nullptr || v[0] != '0';
            }();
            return on;
        }

        // PS-ADPCM: a shift and filter byte, a flag byte, then 28 nibbles.
        void decodeAdpcm(const uint8_t *block, int32_t &hist1, int32_t &hist2, int16_t *out)
        {
            static constexpr int32_t kF0[5] = {0, 60, 115, 98, 122};
            static constexpr int32_t kF1[5] = {0, 0, -52, -55, -60};
            const int32_t shift = block[0] & 0x0f;
            int32_t filter = (block[0] >> 4) & 0x0f;
            if (filter > 4)
            {
                filter = 0;
            }
            for (uint32_t i = 0; i < kAdpcmSamples; ++i)
            {
                const uint8_t byte = block[2 + i / 2];
                const int32_t nibble = (i & 1u) ? (byte >> 4) : (byte & 0x0f);
                int32_t s = static_cast<int16_t>(static_cast<uint16_t>(nibble << 12)) >> shift;
                s += (hist1 * kF0[filter] + hist2 * kF1[filter] + 32) >> 6;
                s = std::clamp(s, -32768, 32767);
                hist2 = hist1;
                hist1 = s;
                out[i] = static_cast<int16_t>(s);
            }
        }

        // The loop flags are ignored for streams; the EE's reader only ever
        // hands over stream data, never a looped sample.
        void decodeBlock(Channel &ch, const uint8_t *block)
        {
            int16_t out[kAdpcmSamples];
            decodeAdpcm(block, ch.hist1, ch.hist2, out);
            ch.pcm.insert(ch.pcm.end(), out, out + kAdpcmSamples);
        }


        Stream *find(Engine &e, uint32_t id)
        {
            auto it = e.streams.find(id);
            return it == e.streams.end() ? nullptr : it->second.get();
        }

        void voiceRestart(Voice &v)
        {
            v.addr = (v.spuAddr + v.startSamples / kAdpcmSamples * kAdpcmBlock) % kSpuRamBytes;
            v.loopAddr = v.spuAddr;
            v.ending = false;
            v.hist1 = v.hist2 = 0;
            v.bufPos = kAdpcmSamples;
            v.prev = v.cur = 0.0f;
            v.phase = 0.0;
        }

        // One source sample, walking SPU RAM the way an SPU2 voice does: a
        // loop-start flag marks the repeat point, loop-end with repeat jumps
        // back to it, loop-end alone finishes the voice after that block.
        // Returns false once the voice has run off its last block.
        bool voiceNext(Engine &e, Voice &v, float &sample)
        {
            if (v.bufPos >= kAdpcmSamples)
            {
                if (v.ending)
                {
                    return false;
                }
                const uint8_t *block = e.spuRam.data() + (v.addr & ~(kAdpcmBlock - 1u));
                const uint8_t flags = block[1];
                if (flags & kFlagLoopStart)
                {
                    v.loopAddr = v.addr;
                }
                decodeAdpcm(block, v.hist1, v.hist2, v.buf);
                v.bufPos = 0;
                if (flags & kFlagLoopEnd)
                {
                    if (flags & kFlagRepeat)
                    {
                        v.addr = v.loopAddr;
                    }
                    else
                    {
                        v.ending = true;
                    }
                }
                else
                {
                    v.addr = (v.addr + kAdpcmBlock) % kSpuRamBytes;
                }
            }
            sample = v.buf[v.bufPos++] / 32768.0f;
            return true;
        }

        void mixVoices(Engine &e, float *out, size_t frames, uint32_t rate)
        {
            uint64_t playing = 0;
            for (auto &entry : e.voices)
            {
                Voice &v = entry.second;
                if (!v.playing || v.paused)
                {
                    continue;
                }
                ++playing;
                const double step = v.rate * static_cast<double>(v.speed) / rate;
                const float gl = v.volume * std::min(1.0f, 1.0f - v.pan);
                const float gr = v.volume * std::min(1.0f, 1.0f + v.pan);
                for (size_t f = 0; f < frames && v.playing; ++f)
                {
                    const float s = v.prev + (v.cur - v.prev) * static_cast<float>(v.phase);
                    out[f * 2u] += s * gl;
                    out[f * 2u + 1u] += s * gr;
                    if (v.fxCore == 0 || v.fxCore == 1)
                    {
                        e.send[v.fxCore][f * 2u] += s * gl;
                        e.send[v.fxCore][f * 2u + 1u] += s * gr;
                    }
                    v.phase += step;
                    while (v.phase >= 1.0)
                    {
                        v.phase -= 1.0;
                        v.prev = v.cur;
                        float next = 0.0f;
                        if (!voiceNext(e, v, next))
                        {
                            v.playing = false;
                            Reply r;
                            r.cmd = kReplySampleDone;
                            r.len = 4u;
                            r.data[0] = entry.first;
                            e.queued.push_back(r);
                            break;
                        }
                        v.cur = next;
                    }
                }
            }
            e.stats.voicesPlaying = playing;
        }

        bool readyToStart(const Stream &s)
        {
            bool any = false;
            for (const Channel &ch : s.channels)
            {
                if (ch.dead)
                {
                    continue;
                }
                if (ch.pcm.size() < kStartSamples)
                {
                    return false;
                }
                any = true;
            }
            return any;
        }
    }

    void streamInfo(uint32_t id, uint32_t channels, const uint32_t *freqs)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        auto s = std::make_unique<Stream>();
        s->id = id;
        s->channels.resize(std::min<uint32_t>(channels, 15u));
        for (size_t i = 0; i < s->channels.size(); ++i)
        {
            s->channels[i].freq = freqs[i];
            // A zero rate is a channel the reader has not described; it can
            // never be played, so it is never asked for.
            s->channels[i].dead = freqs[i] == 0u;
        }
        e.streams[id] = std::move(s);
        ++e.stats.streams;
    }

    void streamPlay(uint32_t id)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        if (Stream *s = find(e, id))
        {
            s->playing = true;
        }
    }

    void streamStop(uint32_t id)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        if (Stream *s = find(e, id))
        {
            s->playing = false;
        }
    }

    void streamDestroy(uint32_t id)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        e.streams.erase(id);
    }

    void streamVolumePan(uint32_t id, uint32_t channel, int16_t volume, int16_t pan)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        Stream *s = find(e, id);
        if (!s || channel >= s->channels.size())
        {
            return;
        }
        // UpdateVolumePan sends DbToRatio(fader) * 32767. VolumePan::
        // SetFloatPan stores the pan as a signed count of 1/127ths; past
        // +-127 it runs into the rear positions, which fold back to the sides
        // on a stereo host.
        Channel &ch = s->channels[channel];
        ch.volume = std::clamp(volume / 32767.0f, 0.0f, 1.0f);
        ch.pan = std::clamp(pan / 127.0f, -1.0f, 1.0f);
    }

    void streamSpeed(uint32_t id, int16_t speedMilli)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        if (Stream *s = find(e, id))
        {
            s->speed = std::max(0.0f, speedMilli / 1000.0f);
        }
    }

    void streamSlipSpeed(uint32_t id, uint32_t channel, int32_t speedMilli)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        Stream *s = find(e, id);
        if (s && channel < s->channels.size())
        {
            Channel &ch = s->channels[channel];
            ch.slipping = true;
            ch.slip = std::max(0.0f, speedMilli / 1000.0f);
        }
    }

    void streamSlipOffset(uint32_t id, uint32_t channel, int32_t samples)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        Stream *s = find(e, id);
        if (!s || channel >= s->channels.size())
        {
            return;
        }
        // Moves the channel to `samples` from the song. Forward drops what it
        // has buffered ahead, which a lagging channel always holds; backward
        // would need what it already played, so it is held in silence
        // instead, which keeps it in time.
        Channel &ch = s->channels[channel];
        const int64_t delta = samples - (ch.pos - static_cast<int64_t>(s->consumed));
        if (delta > 0)
        {
            const size_t drop = std::min<size_t>(static_cast<size_t>(delta), ch.pcm.size());
            ch.pcm.erase(ch.pcm.begin(), ch.pcm.begin() + static_cast<std::ptrdiff_t>(drop));
            ch.pos += static_cast<int64_t>(drop);
        }
        else if (delta < 0)
        {
            ch.pcm.insert(ch.pcm.begin(), static_cast<size_t>(-delta), int16_t{0});
            ch.pos += delta;
        }
    }

    void streamSlipStop(uint32_t id, uint32_t channel)
    {
        Engine &e = engine();
        {
            std::lock_guard<std::mutex> lock(e.mutex);
            Stream *s = find(e, id);
            if (!s || channel >= s->channels.size())
            {
                return;
            }
            s->channels[channel].slipping = false;
            s->channels[channel].slip = 1.0f;
        }
        streamSlipOffset(id, channel, 0);
    }

    void streamDataTarget(uint32_t id, uint32_t channel)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        e.dataId = id;
        e.dataChannel = channel;
        e.dataTargetValid = true;
    }

    void streamData(const uint8_t *bytes, uint32_t len)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        if (!e.dataTargetValid)
        {
            return;
        }
        e.dataTargetValid = false;
        Stream *s = find(e, e.dataId);
        if (!s || e.dataChannel >= s->channels.size())
        {
            return;
        }
        Channel &ch = s->channels[e.dataChannel];
        for (uint32_t off = 0; off + kAdpcmBlock <= len; off += kAdpcmBlock)
        {
            decodeBlock(ch, bytes + off);
        }
        ch.inflight = ch.inflight > len ? ch.inflight - len : 0u;
        ch.gotData = true;
        ch.gotDataThisBatch = true;
        ch.dead = false;
        ch.strikes = 0;
        e.stats.bytesIn += len;
    }

    void streamDataBatchEnd(uint32_t id)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        Stream *s = find(e, id);
        if (!s)
        {
            return;
        }
        bool anyGot = false;
        for (const Channel &ch : s->channels)
        {
            anyGot = anyGot || ch.gotDataThisBatch;
        }
        for (Channel &ch : s->channels)
        {
            if (anyGot && !ch.gotDataThisBatch && ch.inflight != 0u && !ch.gotData &&
                ++ch.strikes >= kDeadStrikes)
            {
                ch.dead = true;
            }
            // Whatever was asked for and not sent is not coming: SendToIOP
            // drains its whole request queue before this record.
            ch.inflight = 0u;
            ch.gotDataThisBatch = false;
        }
    }

    void spuWrite(uint32_t dest, const uint8_t *bytes, uint32_t len)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        for (uint32_t i = 0; i < len; ++i)
        {
            e.spuRam[(dest + i) % kSpuRamBytes] = bytes[i];
        }
    }

    void sampleCreate(uint32_t handle, uint32_t spuAddr, uint32_t rate)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        Voice v;
        v.spuAddr = spuAddr % kSpuRamBytes;
        v.rate = rate;
        e.voices[handle] = v;
        ++e.stats.samples;
    }

    void sampleDestroy(uint32_t handle)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        e.voices.erase(handle);
    }

    void sampleStart(uint32_t handle)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        auto it = e.voices.find(handle);
        if (it == e.voices.end())
        {
            return;
        }
        voiceRestart(it->second);
        it->second.playing = true;
        it->second.paused = false;
        ++e.stats.voiceStarts;
    }

    void sampleStop(uint32_t handle)
    {
        // StopImpl clears mPlaying on the EE itself, so a stop needs no
        // SetDone. There is no release envelope: the voice cuts.
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        auto it = e.voices.find(handle);
        if (it != e.voices.end())
        {
            it->second.playing = false;
        }
    }

    void samplePause(uint32_t handle, bool pause)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        auto it = e.voices.find(handle);
        if (it != e.voices.end())
        {
            it->second.paused = pause;
        }
    }

    void sampleVolumePan(uint32_t handle, int16_t volume, int16_t pan)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        auto it = e.voices.find(handle);
        if (it != e.voices.end())
        {
            // Same encoding as a stream channel's: SetVolumeImpl stores the
            // ratio * 32767, SetPanImpl goes through VolumePan::SetFloatPan.
            it->second.volume = std::clamp(volume / 32767.0f, 0.0f, 1.0f);
            it->second.pan = std::clamp(pan / 127.0f, -1.0f, 1.0f);
        }
    }

    void sampleSpeed(uint32_t handle, int32_t speedMilli)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        auto it = e.voices.find(handle);
        if (it != e.voices.end())
        {
            it->second.speed = std::max(0.0f, speedMilli / 1000.0f);
        }
    }

    void sampleStartOffset(uint32_t handle, uint32_t samples)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        auto it = e.voices.find(handle);
        if (it != e.voices.end())
        {
            it->second.startSamples = samples;
        }
    }

    void fxMode(uint32_t core, uint32_t mode, int16_t depthL, int16_t depthR)
    {
        if (core > 1u)
        {
            return;
        }
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        e.reverb[core].setMode(mode);
        e.reverb[core].depthL = depthL / 32768.0f;
        e.reverb[core].depthR = depthR / 32768.0f;
    }

    void streamFxCore(uint32_t id, uint32_t channel, int16_t core)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        Stream *s = find(e, id);
        if (s && channel < s->channels.size())
        {
            s->channels[channel].fxCore = core;
        }
    }

    void streamFx(uint32_t id, uint32_t channel, bool on)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        Stream *s = find(e, id);
        if (s && channel < s->channels.size())
        {
            s->channels[channel].fx = on;
        }
    }

    void sampleFxCore(uint32_t handle, int32_t core)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        auto it = e.voices.find(handle);
        if (it != e.voices.end())
        {
            it->second.fxCore = core;
        }
    }

    void queueReply(const Reply &reply)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        e.queued.push_back(reply);
    }

    size_t takeReplies(std::vector<Reply> &out)
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        const size_t before = out.size();
        for (const Reply &r : e.queued)
        {
            out.push_back(r);
        }
        e.queued.clear();
        const bool outputLive = e.stats.framesOut != e.framesAtLastPoll;
        e.framesAtLastPoll = e.stats.framesOut;

        for (auto &entry : e.streams)
        {
            Stream &s = *entry.second;
            for (uint32_t c = 0; c < s.channels.size(); ++c)
            {
                Channel &ch = s.channels[c];
                if (ch.dead)
                {
                    continue;
                }
                uint32_t issued = 0;
                while (issued < kMaxRequestsPerPoll &&
                       ch.bufferedBytes() + ch.inflight < kTargetBytes)
                {
                    Reply r;
                    r.cmd = kReplyStreamData;
                    r.len = 12u;
                    r.data[0] = s.id;
                    r.data[1] = c;
                    r.data[2] = kRequestBytes;
                    out.push_back(r);
                    ch.inflight += kRequestBytes;
                    ++issued;
                    ++e.stats.requests;
                }
            }

            // Only while something is pulling samples. With no output device
            // the position never moves, and reporting it would hold the
            // chart at the start forever.
            if (syncEnabled() && s.playing && s.started && outputLive &&
                (++s.polls % kPosReportEvery) == 0u)
            {
                Reply r;
                r.cmd = kReplyStreamPos;
                r.len = 8u;
                r.data[0] = s.id;
                r.data[1] = static_cast<uint32_t>(s.consumed / kAdpcmSamples);
                out.push_back(r);
            }

            for (uint32_t c = 0; c < s.channels.size(); ++c)
            {
                Channel &ch = s.channels[c];
                const int64_t offset = ch.pos - static_cast<int64_t>(s.consumed);
                if (!ch.slipping || offset == ch.reportedOffset)
                {
                    continue;
                }
                ch.reportedOffset = offset;
                Reply r;
                r.cmd = kReplySlipOffset;
                r.len = 12u;
                r.data[0] = s.id;
                r.data[1] = c; // a short at +4, low half of the word
                r.data[2] = static_cast<uint32_t>(static_cast<int32_t>(offset));
                out.push_back(r);
            }
        }
        return out.size() - before;
    }

    void render(float *out, size_t frames, uint32_t rate)
    {
        std::fill(out, out + frames * 2u, 0.0f);
        // GHPC_SYNTH_REVERB_ALL=0|1 sends every stream channel to that core,
        // to hear a reverb without the gameplay that switches it on.
        static const int forcedCore = []() {
            const char *v = std::getenv("GHPC_SYNTH_REVERB_ALL");
            return v ? std::atoi(v) : -1;
        }();
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        e.stats.framesOut += frames;
        for (auto &s : e.send)
        {
            s.assign(frames * 2u, 0.0f);
        }
        uint64_t playing = 0;
        // GHPC_SYNTH_SOLO=song|crowd|voices keeps one part of the mix, to
        // measure or hear it alone. "song" is any stream of more than two
        // channels; "crowd" the two-channel streams.
        static const std::string solo = []() {
            const char *v = std::getenv("GHPC_SYNTH_SOLO");
            return std::string(v ? v : "");
        }();
        for (auto &entry : e.streams)
        {
            Stream &s = *entry.second;
            if (!s.playing)
            {
                continue;
            }
            if (!solo.empty() && solo != (s.channels.size() > 2u ? "song" : "crowd"))
            {
                continue;
            }
            if (!s.started)
            {
                if (!readyToStart(s))
                {
                    continue;
                }
                s.started = true;
                if (std::getenv("GHPC_SYNTH_TRACE"))
                {
                    std::fprintf(stderr, "[synth/trace] t=%.3f stream %u sounding\n",
                                 std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                               kProcessStart).count(), s.id);
                }
            }
            ++playing;
            for (size_t f = 0; f < frames; ++f)
            {
                // Every live channel moves together or not at all, so an
                // underrun stalls the stream instead of skewing its channels
                // against each other.
                bool starved = false;
                for (Channel &ch : s.channels)
                {
                    if (ch.dead || !ch.gotData || ch.slipping)
                    {
                        continue;
                    }
                    const double step = ch.freq * static_cast<double>(s.speed) / rate;
                    if (ch.pcm.size() < static_cast<size_t>(ch.phase + step) + 2u)
                    {
                        starved = true;
                        break;
                    }
                }
                if (starved)
                {
                    ++e.stats.underruns;
                    break;
                }
                float l = 0.0f;
                float r = 0.0f;
                bool first = true;
                for (Channel &ch : s.channels)
                {
                    if (ch.dead || !ch.gotData)
                    {
                        continue;
                    }
                    const double step = ch.freq * static_cast<double>(s.speed * ch.slip) / rate;
                    if (ch.slipping && ch.pcm.size() < static_cast<size_t>(ch.phase + step) + 2u)
                    {
                        continue;
                    }
                    const double frac = ch.phase;
                    const float a = ch.pcm[0];
                    const float b = ch.pcm[1];
                    const float v = (a + (b - a) * static_cast<float>(frac)) / 32768.0f * ch.volume;
                    const float vl = v * std::min(1.0f, 1.0f - ch.pan);
                    const float vr = v * std::min(1.0f, 1.0f + ch.pan);
                    l += vl;
                    r += vr;
                    const int core = forcedCore >= 0 ? forcedCore : (ch.fx ? ch.fxCore : -1);
                    if (core == 0 || core == 1)
                    {
                        e.send[core][f * 2u] += vl;
                        e.send[core][f * 2u + 1u] += vr;
                    }
                    ch.phase += step;
                    const size_t whole = static_cast<size_t>(ch.phase);
                    ch.phase -= static_cast<double>(whole);
                    ch.pcm.erase(ch.pcm.begin(), ch.pcm.begin() + static_cast<std::ptrdiff_t>(whole));
                    ch.pos += static_cast<int64_t>(whole);
                    if (first && !ch.slipping)
                    {
                        s.consumed += whole;
                        first = false;
                    }
                }
                out[f * 2u] += l;
                out[f * 2u + 1u] += r;
            }
        }
        e.stats.playing = playing;
        if (solo.empty() || solo == "voices")
        {
            mixVoices(e, out, frames, rate);
        }
        for (int c = 0; c < 2; ++c)
        {
            if (e.reverb[c].active())
            {
                e.reverb[c].process(e.send[c].data(), out, frames);
            }
        }
        for (size_t i = 0; i < frames * 2u; ++i)
        {
            out[i] = std::clamp(out[i], -1.0f, 1.0f);
        }
    }

    std::string dump()
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        std::string out;
        char line[256];
        for (int c = 0; c < 2; ++c)
        {
            std::snprintf(line, sizeof(line), "  reverb core%d mode=%u depth=%.3f/%.3f\n", c,
                          e.reverb[c].mode, e.reverb[c].depthL, e.reverb[c].depthR);
            out += line;
        }
        for (auto &entry : e.streams)
        {
            const Stream &s = *entry.second;
            std::snprintf(line, sizeof(line), "  stream %u play=%d started=%d speed=%.2f consumed=%llu\n",
                          s.id, s.playing, s.started, s.speed, (unsigned long long)s.consumed);
            out += line;
            for (size_t c = 0; c < s.channels.size(); ++c)
            {
                const Channel &ch = s.channels[c];
                std::snprintf(line, sizeof(line),
                              "    ch%zu freq=%u vol=%.3f pan=%.2f buf=%zu got=%d dead=%d fx=%d/%d slip=%d/%.3f/%lld\n",
                              c, ch.freq, ch.volume, ch.pan, ch.pcm.size(), ch.gotData, ch.dead,
                              ch.fx, ch.fxCore, ch.slipping, ch.slip,
                              (long long)(ch.pos - static_cast<int64_t>(s.consumed)));
                out += line;
            }
        }
        for (auto &entry : e.voices)
        {
            const Voice &v = entry.second;
            if (!v.playing)
            {
                continue;
            }
            std::snprintf(line, sizeof(line),
                          "  voice %u addr=0x%x rate=%u vol=%.3f pan=%.2f speed=%.2f paused=%d\n",
                          entry.first, v.spuAddr, v.rate, v.volume, v.pan, v.speed, v.paused);
            out += line;
        }
        return out;
    }

    Stats stats()
    {
        Engine &e = engine();
        std::lock_guard<std::mutex> lock(e.mutex);
        return e.stats;
    }
}
