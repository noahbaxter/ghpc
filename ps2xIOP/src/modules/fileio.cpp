#include "../iop_service.h"
#include "../module_factories.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <iostream>
#include <sstream>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kFileioSid = 0x80000001u;

        // Function numbers and packet offsets below are read out of the game's
        // own statically linked libfileio, not inferred from ps2sdk. Every call
        // site builds one packet at 0x005b4dc0 and hands it to sceFsSifCallRpc
        // with the function number in $a1. GH2 debug build addresses:
        //
        //   sceOpen     0x0034fb10  fn 0x00  send 0x418
        //   sceClose    0x0034fda0  fn 0x01  send 0x14
        //   sceRead     0x00350158  fn 0x02  send 0x20
        //   sceWrite    0x003503c8  fn 0x03  send 0x30
        //   sceLseek    0x0034ff18  fn 0x04  send 0x1c
        //   sceIoctl    0x00350688  fn 0x05  send 0x420
        //   sceMkdir    0x00350a18  fn 0x07  send strlen+0x11
        //   sceGetstat  0x00350bf8  fn 0x0c  send strlen+0x11
        //   sceLseek64  0x00350dd0  fn 0x16  send 0x20
        enum Function : uint32_t
        {
            kOpen = 0x00u,
            kClose = 0x01u,
            kRead = 0x02u,
            kWrite = 0x03u,
            kLseek = 0x04u,
            kIoctl = 0x05u,
            kMkdir = 0x07u,
            kGetstat = 0x0Cu,
            kLseek64 = 0x16u,
            kInit = 0xFFu,
        };

        // Header shared by every function, from sceGetstat+0xf4..0x100.
        constexpr uint32_t kHeaderSemaphore = 0x00u;
        constexpr uint32_t kHeaderResultAddress = 0x04u;
        constexpr uint32_t kHeaderResultSize = 0x08u;
        constexpr uint32_t kPayload = 0x0Cu;

        // sceOpen packs flags, the fifth-argument mode, then the path.
        constexpr uint32_t kOpenFlags = 0x0Cu;
        constexpr uint32_t kOpenPath = 0x14u;
        constexpr uint32_t kOpenPathBytes = 0x400u;

        // sceRead/sceWrite: descriptor, guest buffer, length.
        constexpr uint32_t kIoDescriptor = 0x0Cu;
        constexpr uint32_t kIoBuffer = 0x10u;
        constexpr uint32_t kIoLength = 0x14u;

        // sceLseek stores a 32 bit offset; sceLseek64 stores a doubleword and
        // pushes whence out to 0x18.
        constexpr uint32_t kSeekOffset = 0x10u;
        constexpr uint32_t kSeekWhence = 0x14u;
        constexpr uint32_t kSeek64Whence = 0x18u;

        // sceGetstat: sceStat pointer then the inline path.
        constexpr uint32_t kStatBuffer = 0x0Cu;
        constexpr uint32_t kStatPath = 0x10u;

        // Sony open flags. Only the access mode matters here.
        constexpr uint32_t kOpenAccessMask = 0x0003u;
        constexpr uint32_t kOpenReadOnly = 0x0001u;

        // sceStat, 40 bytes: mode, attr, size, ctime[8], atime[8], mtime[8], hisize.
        constexpr uint32_t kStatBytes = 0x28u;
        constexpr uint32_t kStatModeFile = 0x2000u;
        constexpr uint32_t kStatModeDirectory = 0x1000u;
        constexpr uint32_t kStatModeReadable = 0x0124u;

        // sceFsInit+0x154 issues fn 0xff with two EE receive-buffer pointers and
        // reads an 8 byte reply: an interface version word, then a flag the
        // library turns into _fs_rcv_bufdbl when it equals 2.
        //
        // _fs_version (0x0034fa48) memcmps that version against the "3000" field
        // of __ps2_klibinfo__ (0x00447420, "PsIIlibkernl3000") and against the
        // "...." wildcard at *_fswildcard. sceOpen+0x74 bails with 0xfffefffc
        // before sending anything if neither matches, which is why open used to
        // fail silently while getstat, which has no version check, worked.
        constexpr uint8_t kInterfaceVersion[4] = {'3', '0', '0', '0'};

        // Errors the EE side already understands: -ENOENT and -EINVAL.
        constexpr int32_t kErrorNotFound = -2;
        constexpr int32_t kErrorInvalid = -22;

        // Asynchronous completion. When the file was opened with SCE_NOWAIT,
        // sceRead+0xe0 parks its semaphore id in _sceFs_q and negates the
        // semaphore field of the packet to mark the request async, then returns
        // 0 without waiting. sceIoctl(fd, 1, argp) is the poll: it sends no RPC
        // at all, it just scans the queue and reports *argp = 1 while any slot
        // is occupied. On hardware _sceFs_Rcv_Intr retires the slot when the
        // IOP's reply lands. This service completes synchronously, so it has to
        // retire the slot itself or AsyncFile::ReadDone polls forever.
        //
        // _sceFs_q is a 32 entry table of semaphore ids, -1 meaning free. The
        // address is specific to the GH2 debug build and must be rechecked for
        // GH1 and 80s.
        constexpr uint32_t kFsQueueAddress = 0x00447448u;
        constexpr uint32_t kFsQueueEntries = 32u;
        constexpr int32_t kFsQueueFree = -1;

        constexpr uint32_t kFirstDescriptor = 3u;
        constexpr uint32_t kMaximumReadBytes = 1u * 1024u * 1024u;

        constexpr uint32_t kSidList[] = {kFileioSid};

        class FileioService final : public IopService
        {
        public:
            explicit FileioService(IopHost &host)
                : m_host(host)
            {
            }

            ~FileioService() override
            {
                reset();
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "FILEIO";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return std::span<const uint32_t>(kSidList);
            }

            void reset() override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto &[descriptor, file] : m_files)
                {
                    (void)descriptor;
                    if (file.handle != 0u)
                    {
                        m_host.closeHostFile(file.handle);
                    }
                }
                m_files.clear();
                m_nextDescriptor = kFirstDescriptor;
                m_opens = 0u;
                m_reads = 0u;
                m_bytesRead = 0u;
                m_unhandled = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                RpcResult result;
                if (request.sid != kFileioSid)
                {
                    return result;
                }

                result.handled = true;
                result.resultAddress = request.receive.address;

                if (request.function == kInit)
                {
                    if (request.receive.address != 0u && request.receive.size != 0u)
                    {
                        (void)m_host.zeroGuest(request.receive.address, request.receive.size);
                    }
                    if (request.receive.size >= sizeof(kInterfaceVersion))
                    {
                        (void)m_host.writeGuest(request.receive.address,
                                                kInterfaceVersion,
                                                sizeof(kInterfaceVersion));
                    }
#if GHPC_DIAG
                    logCall(request, 0);
#endif
                    return result;
                }

                uint32_t semaphore = 0u;
                uint32_t resultAddress = 0u;
                const bool hasHeader = request.send.address != 0u &&
                                       request.send.size >= kPayload &&
                                       readU32(request.send.address + kHeaderSemaphore, semaphore) &&
                                       readU32(request.send.address + kHeaderResultAddress, resultAddress);
                if (!hasHeader)
                {
                    // Not a libfileio packet. Acknowledge without inventing a
                    // reply so the failure stays visible rather than silent.
                    ++m_unhandled;
                    if (request.receive.address != 0u && request.receive.size != 0u)
                    {
                        (void)m_host.zeroGuest(request.receive.address, request.receive.size);
                    }
                    return result;
                }

                const int32_t status = dispatch(request);
#if GHPC_DIAG
                logCall(request, status);
#endif

                // sceGetstat+0x150 reads the four byte reply and treats zero as
                // "the IOP never took the request", returning -11 without ever
                // waiting. A non-zero reply is what arms the WaitSema below.
                if (request.receive.size >= sizeof(uint32_t))
                {
                    writeU32(request.receive.address, 1u);
                }
                writeU32(resultAddress, static_cast<uint32_t>(status));

                const int32_t semaphoreField = static_cast<int32_t>(semaphore);
                if (semaphoreField < 0)
                {
                    // Async request. The caller deletes its semaphore as soon as
                    // it returns, so signalling would be pointless; retiring the
                    // queue slot is what unblocks the poll.
                    retireQueuedRequest(-semaphoreField);
                }
                else
                {
                    (void)m_host.signalGuestSemaphore(semaphore);
                }
                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                metrics.push_back({"open_files", m_files.size(), false});
                metrics.push_back({"opens", m_opens, false});
                metrics.push_back({"reads", m_reads, false});
                metrics.push_back({"bytes_read", m_bytesRead, false});
                metrics.push_back({"unhandled", m_unhandled.load(), false});
                metrics.push_back({"async_retired", m_retired.load(), false});
                metrics.push_back({"async_orphaned", m_orphaned.load(), false});
            }

        private:
            struct OpenFile
            {
                uint64_t handle = 0u;
                uint64_t size = 0u;
                uint64_t position = 0u;
            };

#if GHPC_DIAG
            // Capped so a read loop over a 3 GB ARK does not drown the log.
            void logCall(const RpcRequest &request, int32_t status)
            {
                if (m_logCounts[request.function]++ >= 64u)
                {
                    return;
                }

                std::ostringstream message;
                message << "[FILEIO] fn=0x" << std::hex << request.function << std::dec
                        << " status=" << status;
                if (request.function == kOpen)
                {
                    uint32_t flags = 0u;
                    uint32_t mode = 0u;
                    (void)readU32(request.send.address + kOpenFlags, flags);
                    (void)readU32(request.send.address + 0x10u, mode);
                    message << " flags=0x" << std::hex << flags
                            << " mode=0x" << mode << std::dec
                            << (((flags & 0x8000u) != 0u) ? " NOWAIT" : "")
                            << " path=\"" << readString(request.send.address + kOpenPath, kOpenPathBytes) << '"';
                }
                else if (request.function == kGetstat)
                {
                    const uint32_t pathBytes = request.send.size > kStatPath
                                                   ? request.send.size - kStatPath
                                                   : 0u;
                    message << " path=\"" << readString(request.send.address + kStatPath, pathBytes) << '"';
                }
                else if (request.function == kInit)
                {
                    message << " version=\"3000\"";
                }
                else
                {
                    uint32_t descriptor = 0u;
                    (void)readU32(request.send.address + kIoDescriptor, descriptor);
                    message << " fd=" << descriptor;
                    if (request.function == kRead)
                    {
                        uint32_t length = 0u;
                        (void)readU32(request.send.address + kIoLength, length);
                        message << " len=" << length;
                    }
                }
                std::cerr << message.str() << std::endl;
            }

#endif
            [[nodiscard]] int32_t dispatch(const RpcRequest &request)
            {
                switch (request.function)
                {
                case kOpen:
                    return doOpen(request);
                case kClose:
                    return doClose(request);
                case kRead:
                    return doRead(request);
                case kLseek:
                    return doSeek(request, false);
                case kLseek64:
                    return doSeek(request, true);
                case kGetstat:
                    return doGetstat(request);
                case kIoctl:
                    // sceIoctl resolves requests 1, 2 and 3 entirely on the EE
                    // side and only reaches the IOP for anything else.
                    ++m_unhandled;
                    return kErrorInvalid;
                case kWrite:
                case kMkdir:
                    // The disc is read-only here, so these have no correct
                    // behaviour to emulate yet. Fail loudly rather than
                    // reporting a success the caller would act on.
                    ++m_unhandled;
                    return kErrorInvalid;
                default:
                    ++m_unhandled;
                    return kErrorInvalid;
                }
            }

            [[nodiscard]] int32_t doOpen(const RpcRequest &request)
            {
                uint32_t flags = 0u;
                if (!readU32(request.send.address + kOpenFlags, flags))
                {
                    return kErrorInvalid;
                }
                if ((flags & kOpenAccessMask) != kOpenReadOnly)
                {
                    return kErrorInvalid;
                }

                const std::string guestPath = readString(request.send.address + kOpenPath, kOpenPathBytes);
                const std::string hostPath = m_host.translateGuestPath(guestPath);
                if (hostPath.empty())
                {
                    return kErrorNotFound;
                }

                const uint64_t handle = m_host.openHostFile(hostPath);
                if (handle == 0u)
                {
                    return kErrorNotFound;
                }

                uint64_t size = 0u;
                if (!m_host.hostFileSize(handle, size))
                {
                    m_host.closeHostFile(handle);
                    return kErrorNotFound;
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                const uint32_t descriptor = m_nextDescriptor++;
                m_files.emplace(descriptor, OpenFile{handle, size, 0u});
                ++m_opens;
                return static_cast<int32_t>(descriptor);
            }

            [[nodiscard]] int32_t doClose(const RpcRequest &request)
            {
                uint32_t descriptor = 0u;
                if (!readU32(request.send.address + kIoDescriptor, descriptor))
                {
                    return kErrorInvalid;
                }

                uint64_t handle = 0u;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto it = m_files.find(descriptor);
                    if (it == m_files.end())
                    {
                        return kErrorInvalid;
                    }
                    handle = it->second.handle;
                    m_files.erase(it);
                }
                if (handle != 0u)
                {
                    m_host.closeHostFile(handle);
                }
                return 0;
            }

            [[nodiscard]] int32_t doRead(const RpcRequest &request)
            {
                uint32_t descriptor = 0u;
                uint32_t destination = 0u;
                uint32_t length = 0u;
                if (!readU32(request.send.address + kIoDescriptor, descriptor) ||
                    !readU32(request.send.address + kIoBuffer, destination) ||
                    !readU32(request.send.address + kIoLength, length))
                {
                    return kErrorInvalid;
                }
                if (destination == 0u)
                {
                    return kErrorInvalid;
                }
                if (length == 0u)
                {
                    return 0;
                }

                uint64_t handle = 0u;
                uint64_t position = 0u;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto it = m_files.find(descriptor);
                    if (it == m_files.end() || it->second.handle == 0u)
                    {
                        return kErrorInvalid;
                    }
                    handle = it->second.handle;
                    position = it->second.position;
                }

                std::vector<uint8_t> bytes(std::min(length, kMaximumReadBytes), 0u);
                size_t bytesRead = 0u;
                if (!m_host.readHostFile(handle, position, bytes.data(), bytes.size(), bytesRead))
                {
                    return kErrorInvalid;
                }
                if (bytesRead != 0u && !m_host.writeGuest(destination, bytes.data(), bytesRead))
                {
                    return kErrorInvalid;
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                const auto it = m_files.find(descriptor);
                if (it != m_files.end())
                {
                    it->second.position = position + bytesRead;
                }
                ++m_reads;
                m_bytesRead += bytesRead;
                return static_cast<int32_t>(bytesRead);
            }

            [[nodiscard]] int32_t doSeek(const RpcRequest &request, bool wide)
            {
                uint32_t descriptor = 0u;
                uint32_t whence = 0u;
                if (!readU32(request.send.address + kIoDescriptor, descriptor) ||
                    !readU32(request.send.address + (wide ? kSeek64Whence : kSeekWhence), whence))
                {
                    return kErrorInvalid;
                }

                int64_t offset = 0;
                if (wide)
                {
                    uint64_t raw = 0u;
                    if (!m_host.readGuest(request.send.address + kSeekOffset, &raw, sizeof(raw)))
                    {
                        return kErrorInvalid;
                    }
                    offset = static_cast<int64_t>(raw);
                }
                else
                {
                    uint32_t raw = 0u;
                    if (!readU32(request.send.address + kSeekOffset, raw))
                    {
                        return kErrorInvalid;
                    }
                    offset = static_cast<int32_t>(raw);
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                const auto it = m_files.find(descriptor);
                if (it == m_files.end())
                {
                    return kErrorInvalid;
                }

                int64_t base = 0;
                switch (whence)
                {
                case 0u:
                    base = 0;
                    break;
                case 1u:
                    base = static_cast<int64_t>(it->second.position);
                    break;
                case 2u:
                    base = static_cast<int64_t>(it->second.size);
                    break;
                default:
                    return kErrorInvalid;
                }

                const int64_t target = base + offset;
                if (target < 0)
                {
                    return kErrorInvalid;
                }
                it->second.position = static_cast<uint64_t>(target);
                return static_cast<int32_t>(target);
            }

            [[nodiscard]] int32_t doGetstat(const RpcRequest &request)
            {
                uint32_t statAddress = 0u;
                if (!readU32(request.send.address + kStatBuffer, statAddress) || statAddress == 0u)
                {
                    return kErrorInvalid;
                }

                const uint32_t pathBytes = request.send.size > kStatPath
                                               ? request.send.size - kStatPath
                                               : 0u;
                const std::string guestPath = readString(request.send.address + kStatPath, pathBytes);
                const std::string hostPath = m_host.translateGuestPath(guestPath);
                if (hostPath.empty())
                {
                    return kErrorNotFound;
                }

                // IopHost exposes no stat, so size comes from opening the file.
                // A directory will not open, which is also how it is told apart.
                const uint64_t handle = m_host.openHostFile(hostPath);
                uint64_t size = 0u;
                const bool isFile = handle != 0u && m_host.hostFileSize(handle, size);
                if (handle != 0u)
                {
                    m_host.closeHostFile(handle);
                }
                if (!isFile)
                {
                    return kErrorNotFound;
                }

                std::array<uint8_t, kStatBytes> stat{};
                writeLittle(stat.data() + 0x00, kStatModeFile | kStatModeReadable);
                writeLittle(stat.data() + 0x04, 0u);
                writeLittle(stat.data() + 0x08, static_cast<uint32_t>(size & 0xFFFFFFFFu));
                writeLittle(stat.data() + 0x24, static_cast<uint32_t>(size >> 32));
                if (!m_host.writeGuest(statAddress, stat.data(), stat.size()))
                {
                    return kErrorInvalid;
                }
                return 0;
            }

            void retireQueuedRequest(int32_t semaphoreId)
            {
                for (uint32_t index = 0u; index < kFsQueueEntries; ++index)
                {
                    const uint32_t address = kFsQueueAddress + index * sizeof(uint32_t);
                    uint32_t entry = 0u;
                    if (!readU32(address, entry))
                    {
                        return;
                    }
                    if (static_cast<int32_t>(entry) == semaphoreId)
                    {
                        writeU32(address, static_cast<uint32_t>(kFsQueueFree));
                        ++m_retired;
                        return;
                    }
                }
                ++m_orphaned;
            }

            [[nodiscard]] bool readU32(uint32_t address, uint32_t &value) const
            {
                value = 0u;
                return m_host.readGuest(address, &value, sizeof(value));
            }

            void writeU32(uint32_t address, uint32_t value)
            {
                if (address != 0u)
                {
                    (void)m_host.writeGuest(address, &value, sizeof(value));
                }
            }

            static void writeLittle(uint8_t *destination, uint32_t value)
            {
                destination[0] = static_cast<uint8_t>(value);
                destination[1] = static_cast<uint8_t>(value >> 8);
                destination[2] = static_cast<uint8_t>(value >> 16);
                destination[3] = static_cast<uint8_t>(value >> 24);
            }

            [[nodiscard]] std::string readString(uint32_t address, uint32_t maxBytes) const
            {
                if (address == 0u || maxBytes == 0u)
                {
                    return {};
                }
                std::vector<char> bytes(maxBytes, '\0');
                if (!m_host.readGuest(address, bytes.data(), bytes.size()))
                {
                    return {};
                }
                size_t length = 0u;
                while (length < bytes.size() && bytes[length] != '\0')
                {
                    ++length;
                }
                return std::string(bytes.data(), length);
            }

            IopHost &m_host;
            mutable std::mutex m_mutex;
            std::unordered_map<uint32_t, OpenFile> m_files;
            uint32_t m_nextDescriptor = kFirstDescriptor;
            uint64_t m_opens = 0u;
            uint64_t m_reads = 0u;
            uint64_t m_bytesRead = 0u;
            std::atomic<uint64_t> m_unhandled{0u};
            std::atomic<uint64_t> m_retired{0u};
            std::atomic<uint64_t> m_orphaned{0u};
            std::unordered_map<uint32_t, std::atomic<uint32_t>> m_logCounts;
        };
    }

    std::unique_ptr<IopService> createFileioService(IopHost &host)
    {
        return std::make_unique<FileioService>(host);
    }
}
