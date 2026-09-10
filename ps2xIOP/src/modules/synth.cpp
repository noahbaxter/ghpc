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
        constexpr uint32_t kReplyStreamOp = 2u;
        constexpr uint32_t kReplyTerminated = 14u;

        constexpr uint32_t kRecordHeaderBytes = 8u;
        constexpr uint32_t kMaxPayloadLogs = 8u;
        constexpr uint32_t kMaxUnknownLogs = 16u;

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
                // a single GuestInvocation from guestFunction). Both blocking
                // commands are latched one-shots on the EE side, so in practice
                // a flush carries at most one record that needs an answer, but
                // when it carries more the extra is counted rather than hidden.
                uint32_t replyCmd = 0u;
                uint32_t replyData = 0u;
                uint32_t replyLen = 0u;
                bool haveReply = false;
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

                    if (cmd == kCmdStreamInfo)
                    {
                        noteStreamInfo(payload, len);
                        if (haveReply)
                        {
                            ++unreplied;
                        }
                        else
                        {
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
                            haveReply = true;
                        }
                    }
                    else if (cmd == kCmdTerminate)
                    {
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            ++m_terminateSeen;
                        }
                        // Terminate outranks a stream answer: the EE is already
                        // spinning in it with the main thread, so nothing else
                        // it asked for can still matter.
                        if (haveReply)
                        {
                            ++unreplied;
                        }
                        replyCmd = kReplyTerminated;
                        replyData = 0u;
                        replyLen = 0u;
                        haveReply = true;
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

                if (haveReply)
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
        };
    }

    std::unique_ptr<IopService> createSynthService(IopHost &host)
    {
        return std::make_unique<SynthService>(host);
    }
}
