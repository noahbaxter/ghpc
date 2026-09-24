#include "../module_factories.h"
#include "ps2x/iop/synth_engine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

namespace ps2x::iop::detail
{
    namespace
    {
        // GH2's synth control link, both halves read off the debug ELF rather
        // than inferred:
        //
        //   SynthEE::SynthEE      0x268204  lui $4,0x7543 / ori $4,0x3178
        //     -> CtlClientInit(0x75433178), the EE -> IOP direction
        //   SynthEE::ServerThreadEntry
        //                         0x26857c  lui $5,0x7543 / ori $5,0x3179
        //                         0x268584  $6 = 0x268760
        //     -> CtlServerInit(_, 0x75433179, CtlDispatch__7SynthEE), the
        //        IOP -> EE direction
        //
        // The EE half is a queue, not a call. CtlClientCall(cmd, data, len)
        // appends a packed {cmd:u32, len:u32, data[len]} record to a buffer at
        // 0x4FA6B0 with its cursor at 0x444D70, and CtlClientPoll flushes the
        // whole queue with one sceSifCallRpc(rpc=0, mode=NOWAIT) and no receive
        // buffer. So nothing the IOP writes into a reply buffer is ever read:
        // the answer has to come back as a call into the EE's own handler.
        constexpr uint32_t kSynthCtlSid = 0x75433178u;
        constexpr uint32_t kCtlDispatch = 0x00268760u; // CtlDispatch__7SynthEE

        // EE -> IOP commands. Only the two that block are handled; the rest are
        // counted and logged so an unimplemented one is visible rather than
        // silently dropped.
        constexpr uint32_t kCmdTerminate = 1u;      // SynthEE::Terminate
        constexpr uint32_t kCmdStreamInfo = 0x190u; // StreamEE Poll, state 2
        // The SPU upload pair, read off SPUSendPoll 0x26e4c8 rather than
        // inferred from the 0x5000 length:
        //
        //   SPUStartSend(src, len, dst)  0x26e3f0 stores src at 0x444D90,
        //     len at 0x51A9A0 and the SPU RAM destination at 0x51A99C.
        //   SPUSendPoll  0x26e4c8 returns early unless 0x444D90 is set and
        //     0x444D94 (gSpuInFlight) is clear, then sets inflight, takes
        //     min(0x5000, remaining) and issues
        //       CtlClientCall<int>(0xC8, *(int*)0x51A99C)   the destination
        //       CtlClientCall(0xC9, src, chunkLen)          the bytes
        //     and advances src, dst and remaining by the chunk regardless of
        //     any answer. So the ack paces the upload; it never gates data.
        constexpr uint32_t kCmdSpuSetAddr = 0xc8u;
        constexpr uint32_t kCmdSpuChunk = 0xc9u;

        // The stream commands this service acts on, each number read off the
        // `addiu $a0, $zero, N` ahead of the CtlClientCall in the named body:
        //   0x192 Play__8StreamEE       {id}
        //   0x193 Stop__8StreamEE       {id}
        //   0x194 Destroy__8StreamEE    {id}
        //   0x195 UpdateVolumePan       StreamVolumePanArg
        //   0x197 UpdateSpeed           StreamSpeedArg
        //   0x198 SendToIOP             StreamDataInfoArg, then
        //   0x199 SendToIOP             the channel's raw PS-ADPCM, then
        //   0x19a SendToIOP             {id}, once the request queue is drained
        constexpr uint32_t kCmdStreamPlay = 0x192u;
        constexpr uint32_t kCmdStreamStop = 0x193u;
        constexpr uint32_t kCmdStreamDestroy = 0x194u;
        constexpr uint32_t kCmdStreamVolumePan = 0x195u;
        constexpr uint32_t kCmdStreamSpeed = 0x197u;
        constexpr uint32_t kCmdStreamDataInfo = 0x198u;
        constexpr uint32_t kCmdStreamData = 0x199u;
        constexpr uint32_t kCmdStreamDataEnd = 0x19au;
        // StreamEE's slip calls, the whammy's pitch bend on the guitar
        // channels: SetSlipOffset {id, short ch, int samples}, StopSlip
        // {id, int ch}, SetSlipSpeed {id, int ch, int speed * 1000}.
        constexpr uint32_t kCmdStreamSlipOffset = 0x19bu;
        constexpr uint32_t kCmdStreamSlipStop = 0x19cu;
        constexpr uint32_t kCmdStreamSlipSpeed = 0x19du;

        // SampleInstEE's, the same way:
        //   0x2bc ctor 0x269fe0      CreateSampleArg {handle, spuAddr, rate}
        //   0x2bd dtor 0x26a0d0      {handle}
        //   0x2be StartImpl          {handle}
        //   0x2bf StopImpl           {handle}
        //   0x2c0 Pause              SfxPauseArg {handle, bool}
        //   0x2c1 UpdateVolPan       {handle, short volume, short pan}
        //   0x2c2 SetSpeedImpl       {handle, speed * 1000}
        //   0x2c3 SetADSR            {handle, packed ADSR}
        //   0x2c4 SetFXCore          {handle, core}
        //   0x2c5 SetStartProgress   {handle, offset in samples}
        // The create's two words are GetSPUAddr (0x269628, +0x64) and
        // GetSampleRate (0x264698, +0x4C) in that order; the decomp header
        // has SampleInstEE's +0x2c and +0x30 names the other way round.
        constexpr uint32_t kCmdSampleCreate = 0x2bcu;
        constexpr uint32_t kCmdSampleDestroy = 0x2bdu;
        constexpr uint32_t kCmdSampleStart = 0x2beu;
        constexpr uint32_t kCmdSampleStop = 0x2bfu;
        constexpr uint32_t kCmdSamplePause = 0x2c0u;
        constexpr uint32_t kCmdSampleVolumePan = 0x2c1u;
        constexpr uint32_t kCmdSampleSpeed = 0x2c2u;
        constexpr uint32_t kCmdSampleFxCore = 0x2c4u;
        constexpr uint32_t kCmdSampleStartOffset = 0x2c5u;

        // Reverb. 0x3 (SetFXMode) and 0x4 (SetFXDelay, SetFXFeedback,
        // SetFXVolume) all carry a whole sceSdEffectAttr {int core, int mode,
        // short depthL, short depthR, int delay, int feedback}. Streams pick a
        // core per channel with 0x191 {id, short ch, short core} and switch
        // the send with 0x196 {id, short ch, short on}. The only thing that
        // switches one on in a song is PlayerMatcher::SetReverb, from
        // OnStartStarpower: the guitar channels, core 1.
        constexpr uint32_t kCmdFxMode = 0x3u;
        constexpr uint32_t kCmdFxAttr = 0x4u;
        constexpr uint32_t kCmdStreamFxCore = 0x191u;
        constexpr uint32_t kCmdStreamFx = 0x196u;

        // IOP -> EE replies. These are indexes into CtlDispatch_impl's jump
        // table at 0x4EB680, which is bounds checked with `sltiu $2, $5, 0xf`,
        // so the reply space is 0..14 and has nothing to do with the much
        // larger EE -> IOP command numbers.
        //
        //   entry 2  -> 0x3EB888  lw $4, 0($8); StreamEE::Dispatch(id, 2, 0)
        //                         which pushes StreamOp type 2, the only thing
        //                         that makes StreamEE::Poll write state 3.
        //   entry 14 -> 0x3EBA10  sw 1, 0x4130(this), the only writer of the
        //                         word SynthEE::Terminate spins on outside the
        //                         constructor.
        //   entry 13 -> 0x3EBA00  jal SPUSetSendDone 0x26e4b8, which is the
        //                         only writer that clears gSpuInFlight
        //                         (0x444D94). Read out of the table at
        //                         0x4EB680 (.rodata, file offset 0x3EC680),
        //                         not inferred.
        constexpr uint32_t kReplyStreamOp = 2u;
        constexpr uint32_t kReplySpuSendDone = 13u;
        constexpr uint32_t kReplyTerminated = 14u;

        // Only one guest call fits in an RPC result, so when a flush carries
        // several answerable records the highest rank rides it and the rest
        // are queued. Terminate outranks everything because the EE is already
        // spinning in it. A dropped SPU ack is not retried by anything:
        // gSpuInFlight stays set and SPUSendPoll never ships again.
        constexpr int kRankNone = 0;
        constexpr int kRankSpuSendDone = 1;
        constexpr int kRankStreamOp = 2;
        constexpr int kRankTerminated = 3;

        // Control arm on one binary: GHPC_SPU_ACK=0 restores the behaviour
        // where nothing answers a chunk, so gSpuInFlight latches and the
        // upload stops after the first one. Counting still runs, so both arms
        // report the same line.
        bool spuAckEnabled()
        {
            static const bool enabled = []() {
                const char *value = std::getenv("GHPC_SPU_ACK");
                return value == nullptr || value[0] != '0';
            }();
            return enabled;
        }

        const auto kProcessStart = std::chrono::steady_clock::now();

        constexpr uint32_t kRecordHeaderBytes = 8u;
        constexpr uint32_t kMaxPayloadLogs = 8u;
        constexpr uint32_t kMaxUnknownLogs = 16u;
        constexpr uint32_t kSpuChunkLogEvery = 32u;
        constexpr uint32_t kSpuChunkLogFirst = 64u;
        // SPUStartSend's own state, read back when a chunk arrives. By then
        // SPUSendPoll has already advanced it past the chunk, so pending
        // clear means the transfer is complete.
        constexpr uint32_t kGuestSpuPending = 0x444D90u;
        constexpr uint32_t kGuestSpuInFlight = 0x444D94u;
        constexpr uint32_t kGuestSpuRemaining = 0x51A9A0u;

        class SynthService final : public IopService
        {
        public:
            explicit SynthService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override { return "SYNTH"; }

            [[nodiscard]] std::span<const uint32_t> sids() const override { return kSids; }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_terminateSeen = 0u;
                m_streamInfoSeen = 0u;
                m_repliesSent = 0u;
                m_unrepliedRecords = 0u;
                m_payloadLogs = 0u;
                m_unknownLogs = 0u;
                m_recordsSeen = 0u;
                m_spuChunks = 0u;
                m_spuBytes = 0u;
                m_spuSetAddrSeen = 0u;
                m_spuDest = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                RpcResult result;
                if (request.sid != kSynthCtlSid)
                {
                    return result;
                }

                result.handled = true;
                result.resultAddress = request.receive.address;
                // The EE flushes this queue with SIF_RPCM_NOWAIT and then asks
                // sceSifCheckStatRpc before the next flush, so the completion
                // has to be signalled or the second flush never happens.
                result.signalNowaitCompletion = true;

                // Only one guest call can be dispatched per RPC (RPC.cpp builds
                // a single GuestInvocation from guestFunction). A flush that
                // carries several answerable records sends the highest ranked
                // one here and queues the rest for the next SynthEE::Poll,
                // which delivers them (ghpc/override/Poll__7SynthEE_0x2685a8).
                uint32_t replyCmd = 0u;
                uint32_t replyData = 0u;
                uint32_t replyLen = 0u;
                int replyRank = kRankNone;
                unsigned unreplied = 0u;
                std::vector<synth::Reply> deferred;
                auto offer = [&](int rank, uint32_t cmd, uint32_t data, uint32_t dataLen,
                                 uint32_t word0) {
                    if (rank <= replyRank)
                    {
                        synth::Reply r;
                        r.cmd = cmd;
                        r.len = data ? 4u : 0u;
                        r.data[0] = word0;
                        deferred.push_back(r);
                        return;
                    }
                    if (replyRank != kRankNone)
                    {
                        synth::Reply r;
                        r.cmd = replyCmd;
                        r.len = replyData ? 4u : 0u;
                        uint32_t w = 0u;
                        if (replyData)
                        {
                            m_host.readGuest(replyData, &w, sizeof(w));
                        }
                        r.data[0] = w;
                        deferred.push_back(r);
                    }
                    replyCmd = cmd;
                    replyData = data;
                    replyLen = dataLen;
                    replyRank = rank;
                };

                uint32_t offset = 0u;
                while (offset + kRecordHeaderBytes <= request.send.size)
                {
                    uint32_t cmd = 0u;
                    uint32_t len = 0u;
                    const uint32_t base = request.send.address + offset;
                    if (!m_host.readGuest(base, &cmd, sizeof(cmd)) ||
                        !m_host.readGuest(base + 4u, &len, sizeof(len)))
                    {
                        break;
                    }
                    // A length that runs past the flush is a desynchronised
                    // walk, not a short record. Stop rather than reinterpret
                    // whatever follows as another header.
                    if (len > request.send.size - offset - kRecordHeaderBytes)
                    {
                        break;
                    }
                    const uint32_t payload = base + kRecordHeaderBytes;
                    traceRecord(cmd, payload, len);
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        ++m_recordsSeen;
                    }

                    if (cmd == kCmdStreamInfo)
                    {
                        noteStreamInfo(payload, len);
                        // StreamInfoArg, from StreamEE::Poll 0x26d410: +0 id,
                        // +4 a short channel count, +8 fifteen int sample
                        // rates, +0x44 fifteen FX bytes.
                        std::array<uint32_t, 17> words{};
                        m_host.readGuest(payload, words.data(),
                                         std::min<size_t>(len, sizeof(words)));
                        synth::streamInfo(words[0], words[1] & 0xffffu, &words[2]);
                        // Entry 2's handler reads the stream id straight out
                        // of the buffer it is handed, so the EE's own
                        // payload can be passed through rather than copied
                        // into a scratch allocation.
                        offer(kRankStreamOp, kReplyStreamOp, payload, len, words[0]);
                    }
                    else if (cmd == kCmdTerminate)
                    {
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            ++m_terminateSeen;
                        }
                        // Terminate outranks everything: the EE is already
                        // spinning in it with the main thread.
                        offer(kRankTerminated, kReplyTerminated, 0u, 0u, 0u);
                    }
                    else if (cmd == kCmdSpuSetAddr)
                    {
                        // A bare int payload holding the SPU RAM destination
                        // for the chunk that follows in the same flush. It
                        // needs no answer; it is kept so the upload can be
                        // reassembled in address order once there is something
                        // to play it.
                        noteSpuSetAddr(payload, len);
                    }
                    else if (cmd == kCmdSpuChunk)
                    {
                        noteSpuChunk(payload, len);
                        // GHPC_SPU_ACK=0 counts it and deliberately leaves it
                        // unanswered.
                        if (spuAckEnabled())
                        {
                            offer(kRankSpuSendDone, kReplySpuSendDone, 0u, 0u, 0u);
                        }
                    }
                    else if (cmd >= kCmdSampleCreate && cmd <= kCmdSampleStartOffset)
                    {
                        // Every sample record leads with the handle; the
                        // create is {handle, spuAddr, rate}, the rest one more
                        // word at most (see the kCmdSample* table).
                        uint32_t w[3]{};
                        m_host.readGuest(payload, w, std::min<size_t>(len, sizeof(w)));
                        switch (cmd)
                        {
                        case kCmdSampleCreate: synth::sampleCreate(w[0], w[1], w[2]); break;
                        case kCmdSampleDestroy: synth::sampleDestroy(w[0]); break;
                        case kCmdSampleStart: synth::sampleStart(w[0]); break;
                        case kCmdSampleStop: synth::sampleStop(w[0]); break;
                        case kCmdSamplePause: synth::samplePause(w[0], (w[1] & 0xffu) != 0u); break;
                        case kCmdSampleVolumePan:
                            synth::sampleVolumePan(w[0], static_cast<int16_t>(w[1] & 0xffffu),
                                                   static_cast<int16_t>(w[1] >> 16));
                            break;
                        case kCmdSampleSpeed: synth::sampleSpeed(w[0], static_cast<int32_t>(w[1])); break;
                        case kCmdSampleStartOffset: synth::sampleStartOffset(w[0], w[1]); break;
                        case kCmdSampleFxCore: synth::sampleFxCore(w[0], static_cast<int32_t>(w[1])); break;
                        default: break; // 0x2c3 ADSR: not modelled
                        }
                    }
                    else if (cmd == kCmdFxMode || cmd == kCmdFxAttr)
                    {
                        uint32_t w[5]{};
                        m_host.readGuest(payload, w, std::min<size_t>(len, sizeof(w)));
                        synth::fxMode(w[0], w[1], static_cast<int16_t>(w[2] & 0xffffu),
                                      static_cast<int16_t>(w[2] >> 16));
                    }
                    else if (cmd == kCmdStreamFxCore || cmd == kCmdStreamFx)
                    {
                        uint32_t w[2]{};
                        m_host.readGuest(payload, w, std::min<size_t>(len, sizeof(w)));
                        const uint32_t ch = w[1] & 0xffffu;
                        const int16_t value = static_cast<int16_t>(w[1] >> 16);
                        if (cmd == kCmdStreamFxCore)
                            synth::streamFxCore(w[0], ch, value);
                        else
                            synth::streamFx(w[0], ch, value != 0);
                    }
                    else if (cmd == kCmdStreamPlay || cmd == kCmdStreamStop ||
                             cmd == kCmdStreamDestroy || cmd == kCmdStreamDataEnd)
                    {
                        uint32_t id = 0u;
                        m_host.readGuest(payload, &id, sizeof(id));
                        if (cmd == kCmdStreamPlay)
                            synth::streamPlay(id);
                        else if (cmd == kCmdStreamStop)
                            synth::streamStop(id);
                        else if (cmd == kCmdStreamDestroy)
                            synth::streamDestroy(id);
                        else
                            synth::streamDataBatchEnd(id);
                    }
                    else if (cmd == kCmdStreamVolumePan)
                    {
                        // StreamVolumePanArg, packed: +0 id, +4 short channel,
                        // +6 short volume, +8 short pan.
                        uint8_t raw[10]{};
                        m_host.readGuest(payload, raw, std::min<size_t>(len, sizeof(raw)));
                        uint32_t id = 0u;
                        int16_t ch = 0, vol = 0, pan = 0;
                        std::memcpy(&id, raw, 4);
                        std::memcpy(&ch, raw + 4, 2);
                        std::memcpy(&vol, raw + 6, 2);
                        std::memcpy(&pan, raw + 8, 2);
                        synth::streamVolumePan(id, static_cast<uint16_t>(ch), vol, pan);
                    }
                    else if (cmd == kCmdStreamSpeed)
                    {
                        // StreamSpeedArg: +0 id, +4 short speed in thousandths.
                        uint8_t raw[6]{};
                        m_host.readGuest(payload, raw, std::min<size_t>(len, sizeof(raw)));
                        uint32_t id = 0u;
                        int16_t speed = 0;
                        std::memcpy(&id, raw, 4);
                        std::memcpy(&speed, raw + 4, 2);
                        synth::streamSpeed(id, speed);
                    }
                    else if (cmd == kCmdStreamDataInfo)
                    {
                        // StreamDataInfoArg: +0 id, +4 short channel. The bytes
                        // follow as the next record, 0x199.
                        uint8_t raw[6]{};
                        m_host.readGuest(payload, raw, std::min<size_t>(len, sizeof(raw)));
                        uint32_t id = 0u;
                        int16_t ch = 0;
                        std::memcpy(&id, raw, 4);
                        std::memcpy(&ch, raw + 4, 2);
                        synth::streamDataTarget(id, static_cast<uint16_t>(ch));
                    }
                    else if (cmd == kCmdStreamSlipOffset || cmd == kCmdStreamSlipStop ||
                             cmd == kCmdStreamSlipSpeed)
                    {
                        uint32_t w[3]{};
                        m_host.readGuest(payload, w, std::min<size_t>(len, sizeof(w)));
                        if (cmd == kCmdStreamSlipOffset)
                            synth::streamSlipOffset(w[0], w[1] & 0xffffu, static_cast<int32_t>(w[2]));
                        else if (cmd == kCmdStreamSlipStop)
                            synth::streamSlipStop(w[0], w[1]);
                        else
                            synth::streamSlipSpeed(w[0], w[1], static_cast<int32_t>(w[2]));
                    }
                    else if (cmd == kCmdStreamData)
                    {
                        m_dataScratch.resize(len);
                        if (m_host.readGuest(payload, m_dataScratch.data(), len))
                        {
                            synth::streamData(m_dataScratch.data(), len);
                        }
                    }
                    else
                    {
                        noteUnknown(cmd, len);
                    }

                    offset += kRecordHeaderBytes + len;
                }

                for (const synth::Reply &r : deferred)
                {
                    synth::queueReply(r);
                    ++unreplied;
                }
                if (unreplied != 0u)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_unrepliedRecords += unreplied;
                }

                if (replyRank != kRankNone)
                {
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        ++m_repliesSent;
                    }
                    result.guestFunction = kCtlDispatch;
                    result.guestArguments[0] = replyCmd;
                    result.guestArguments[1] = replyData;
                    result.guestArguments[2] = replyLen;
                    result.guestArguments[3] = 0u;
                }
                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"terminate_requests", m_terminateSeen, false});
                metrics.push_back({"stream_info_requests", m_streamInfoSeen, false});
                metrics.push_back({"replies_dispatched", m_repliesSent, false});
                metrics.push_back({"records_left_unanswered", m_unrepliedRecords, false});
                metrics.push_back({"spu_set_addr", m_spuSetAddrSeen, false});
                metrics.push_back({"spu_chunks", m_spuChunks, false});
            }

        private:
            // GHPC_SYNTH_TRACE=1: every record but the bulk ones (SPU and
            // stream bytes, data headers, volume updates), with its payload
            // words and the wall time since the first record.
            void traceRecord(uint32_t cmd, uint32_t payload, uint32_t len)
            {
                static const bool on = std::getenv("GHPC_SYNTH_TRACE") != nullptr;
                if (!on || cmd == kCmdSpuSetAddr || cmd == kCmdSpuChunk ||
                    cmd == kCmdStreamDataInfo || cmd == kCmdStreamData ||
                    cmd == kCmdStreamDataEnd ||
                    ((cmd == kCmdStreamVolumePan || cmd == kCmdSampleVolumePan) &&
                     std::getenv("GHPC_SYNTH_TRACE")[0] != '2'))
                {
                    return;
                }
                const double t = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - kProcessStart).count();
                std::ostringstream message;
                message << "[synth/trace] t=" << t << " cmd=0x" << std::hex << cmd
                        << std::dec << " len=" << len << " words=";
                for (uint32_t i = 0; i < len / 4u && i < 8u; ++i)
                {
                    uint32_t w = 0u;
                    m_host.readGuest(payload + i * 4u, &w, sizeof(w));
                    message << (i ? " " : "") << "0x" << std::hex << w << std::dec;
                }
                m_host.log(LogLevel::Info, message.str());
            }

            void noteStreamInfo(uint32_t payload, uint32_t len)
            {
                bool shouldLog = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_streamInfoSeen;
                    if (m_payloadLogs < kMaxPayloadLogs)
                    {
                        ++m_payloadLogs;
                        shouldLog = true;
                    }
                }
                if (!shouldLog)
                {
                    return;
                }
                // The leading words are what entry 2 indexes into, so they are
                // printed as words rather than bytes: reading the stream id off
                // this line is the point.
                std::ostringstream message;
                message << "[SYNTH] StreamInfoArg len=" << len << " words=";
                const uint32_t words = len / sizeof(uint32_t);
                for (uint32_t i = 0u; i < words && i < 8u; ++i)
                {
                    uint32_t word = 0u;
                    if (!m_host.readGuest(payload + i * sizeof(uint32_t), &word, sizeof(word)))
                    {
                        break;
                    }
                    message << (i ? " " : "") << "0x" << std::hex << word << std::dec;
                }
                m_host.log(LogLevel::Info, message.str());
            }

            void noteSpuSetAddr(uint32_t payload, uint32_t len)
            {
                if (len < sizeof(uint32_t))
                {
                    return;
                }
                uint32_t dest = 0u;
                if (!m_host.readGuest(payload, &dest, sizeof(dest)))
                {
                    return;
                }
                std::lock_guard<std::mutex> lock(m_mutex);
                m_spuDest = dest;
                ++m_spuSetAddrSeen;
            }

            // Uncapped on purpose: the chunk count is the progress signal the
            // oracle reads, and a capped line stops moving exactly when it
            // matters. Logged every kSpuChunkLogEvery now that the count
            // climbs, so a song's upload does not bury the rest of the log.
            void noteSpuChunk(uint32_t payload, uint32_t len)
            {
                uint32_t chunks = 0u;
                uint64_t bytes = 0u;
                uint32_t records = 0u;
                uint32_t dest = 0u;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    ++m_spuChunks;
                    m_spuBytes += len;
                    chunks = m_spuChunks;
                    bytes = m_spuBytes;
                    records = m_recordsSeen;
                    dest = m_spuDest;
                    m_spuDest += len;
                }
                m_dataScratch.resize(len);
                if (m_host.readGuest(payload, m_dataScratch.data(), len))
                {
                    synth::spuWrite(dest, m_dataScratch.data(), len);
                }
                if (chunks > kSpuChunkLogFirst && (chunks % kSpuChunkLogEvery) != 0u)
                {
                    return;
                }
                uint32_t pending = 0u;
                uint32_t inflight = 0u;
                uint32_t remaining = 0u;
                m_host.readGuest(kGuestSpuPending, &pending, sizeof(pending));
                m_host.readGuest(kGuestSpuInFlight, &inflight, sizeof(inflight));
                m_host.readGuest(kGuestSpuRemaining, &remaining, sizeof(remaining));
                std::ostringstream message;
                message << "[ghpc/spu2] chunks=" << chunks << " bytes=" << bytes
                        << " records=" << records << " dest=0x" << std::hex << dest
                        << " len=0x" << len << " pending=0x" << pending
                        << " remaining=0x" << remaining << std::dec
                        << " inflight=" << inflight;
                m_host.log(LogLevel::Info, message.str());
            }

            void noteUnknown(uint32_t cmd, uint32_t len)
            {
                bool shouldLog = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_unknownLogs < kMaxUnknownLogs)
                    {
                        ++m_unknownLogs;
                        shouldLog = true;
                    }
                }
                if (!shouldLog)
                {
                    return;
                }
                std::ostringstream message;
                message << "[SYNTH:unhandled] cmd=0x" << std::hex << cmd << std::dec
                        << " len=" << len;
                m_host.log(LogLevel::Info, message.str());
            }

            inline static constexpr std::array<uint32_t, 1> kSids{kSynthCtlSid};

            IopHost &m_host;
            mutable std::mutex m_mutex;
            uint32_t m_terminateSeen = 0u;
            uint32_t m_streamInfoSeen = 0u;
            uint32_t m_repliesSent = 0u;
            uint32_t m_unrepliedRecords = 0u;
            uint32_t m_payloadLogs = 0u;
            uint32_t m_unknownLogs = 0u;
            uint32_t m_recordsSeen = 0u;
            uint32_t m_spuChunks = 0u;
            uint64_t m_spuBytes = 0u;
            uint32_t m_spuSetAddrSeen = 0u;
            uint32_t m_spuDest = 0u;
            std::vector<uint8_t> m_dataScratch;
        };
    }

    std::unique_ptr<IopService> createSynthService(IopHost &host)
    {
        return std::make_unique<SynthService>(host);
    }
}
