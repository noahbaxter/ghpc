#include "../iop_service.h"
#include "../module_factories.h"

#include <cstdint>
#include <span>
#include <string>

namespace ps2x::iop::detail
{
    namespace
    {
        // usbkb.irx. GH2's debug build calls sceUsbKbInit during startup and
        // asserts on the result (Keyboard_EE.cpp line 75, "ret == USBKB_OK"),
        // so the bind has to succeed even though no keyboard is attached.
        // sceUsbKbGetInfo (0x00345350) issues function 1 with a 16 byte request
        // and a 144 byte reply, and only checks that the call itself succeeded.
        constexpr uint32_t kUsbKbSid = 0x80000211u;
        constexpr uint32_t kGetInfoFunction = 1u;
        constexpr uint32_t kSidList[] = {kUsbKbSid};

        class UsbKbService final : public IopService
        {
        public:
            explicit UsbKbService(IopHost &host)
                : m_host(host)
            {
            }

            [[nodiscard]] std::string_view name() const override
            {
                return "USBKB";
            }

            [[nodiscard]] std::span<const uint32_t> sids() const override
            {
                return std::span<const uint32_t>(kSidList);
            }

            void reset() override
            {
                m_calls = 0u;
            }

            [[nodiscard]] RpcResult handleRpc(const RpcRequest &request) override
            {
                RpcResult result;
                if (request.sid != kUsbKbSid)
                {
                    return result;
                }

                result.handled = true;
                result.resultAddress = request.receive.address;
                ++m_calls;

                if (request.receive.address != 0u && request.receive.size != 0u)
                {
                    (void)m_host.zeroGuest(request.receive.address, request.receive.size);
                }

                // sceUsbKbInit+0xd8 reads the device count out of this reply and
                // bails with -1 when it is zero or >= 0x80, which fails the
                // build's "ret == USBKB_OK" assert. Report exactly one device;
                // reads then simply never produce keystrokes.
                if (request.function == kGetInfoFunction &&
                    request.receive.address != 0u &&
                    request.receive.size >= sizeof(uint32_t))
                {
                    const uint32_t deviceCount = 1u;
                    (void)m_host.writeGuest(request.receive.address, &deviceCount, sizeof(deviceCount));
                }
                return result;
            }

            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                metrics.push_back({"calls", m_calls, false});
            }

        private:
            IopHost &m_host;
            uint64_t m_calls = 0u;
        };
    }

    std::unique_ptr<IopService> createUsbKbService(IopHost &host)
    {
        return std::make_unique<UsbKbService>(host);
    }
}
