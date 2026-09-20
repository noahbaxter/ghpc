#include "../module_factories.h"

#include <array>
#include <cstdint>
#include <cstdlib>
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
        // several answerable records the highest rank wins. Terminate outranks
        // everything because the EE is already spinning in it. StreamInfo
        // outranks the SPU ack because it is a one-shot latch that gates the
        // song load, while a missed SPU ack only leaves gSpuInFlight set and
        // the next poll retries.
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

        constexpr uint32_t kRecordHeaderBytes = 8u;
        constexpr uint32_t kMaxPayloadLogs = 8u;
        constexpr uint32_t kMaxUnknownLogs = 16u;
        constexpr uint32_t kSpuChunkLogEvery = 32u;

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
                // carries several answerable records keeps the highest ranked
                // one and counts the rest rather than hiding them.
                uint32_t replyCmd = 0u;
                uint32_t replyData = 0u;
                uint32_t replyLen = 0u;
                int replyRank = kRankNone;
                unsigned unreplied = 0u;

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
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        ++m_recordsSeen;
                    }

                    if (cmd == kCmdStreamInfo)
                    {
                        noteStreamInfo(payload, len);
                        if (replyRank >= kRankStreamOp)
                        {
                            ++unreplied;
                        }
                        else
                        {
                            if (replyRank != kRankNone)
                            {
                                ++unreplied;
                            }
                            // Entry 2's handler reads the stream id straight out
                            // of the buffer it is handed, so the EE's own
                            // payload can be passed through rather than copied
                            // into a scratch allocation. If the id is not the
                            // leading word, StreamEE::Dispatch fails to resolve
                            // it and returns without pushing an op: a silent
                            // no-op, which is why the payload is logged.
                            replyCmd = kReplyStreamOp;
                            replyData = payload;
                            replyLen = len;
                            replyRank = kRankStreamOp;
                        }
                    }
                    else if (cmd == kCmdTerminate)
                    {
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            ++m_terminateSeen;
                        }
                        // Terminate outranks everything: the EE is already
                        // spinning in it with the main thread, so nothing else
                        // it asked for can still matter.
                        if (replyRank != kRankNone)
                        {
                            ++unreplied;
                        }
                        replyCmd = kReplyTerminated;
                        replyData = 0u;
                        replyLen = 0u;
                        replyRank = kRankTerminated;
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
                        if (!spuAckEnabled())
                        {
                            // Counted, deliberately unanswered.
                        }
                        else if (replyRank >= kRankSpuSendDone)
                        {
                            ++unreplied;
                        }
                        else
                        {
                            replyCmd = kReplySpuSendDone;
                            replyData = 0u;
                            replyLen = 0u;
                            replyRank = kRankSpuSendDone;
                        }
                    }
                    else
                    {
                        noteUnknown(cmd, len);
                    }

                    offset += kRecordHeaderBytes + len;
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
                (void)payload;
                if (chunks != 1u && (chunks % kSpuChunkLogEvery) != 0u)
                {
                    return;
                }
                std::ostringstream message;
                message << "[ghpc/spu2] chunks=" << chunks << " bytes=" << bytes
                        << " records=" << records << " dest=0x" << std::hex << dest
                        << std::dec;
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
        };
    }

    std::unique_ptr<IopService> createSynthService(IopHost &host)
    {
        return std::make_unique<SynthService>(host);
    }
}
