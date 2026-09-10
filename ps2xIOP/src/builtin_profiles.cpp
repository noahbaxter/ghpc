#include "iop_service.h"
#include "module_factories.h"

#include <cstdlib>

#include <utility>

namespace ps2x::iop::detail
{
    namespace
    {
        TsnddrvBindings recvxTsnddrvBindings()
        {
            return {
                .serviceName = "TSNDDRV",
                .protocol = TsnddrvProtocolVariant::SndQueueV1,
                .arena = {
                    .base = 0x00120000u,
                    .limit = 0x00200000u,
                    .statusAlignment = 0x100u,
                    .tableAlignment = 0x100u,
                    .storageAlignment = 0x1000u,
                    .hdBytes = 0x4000u,
                    .sqBytes = 0x18000u,
                    .dataBytes = 0x40000u,
                },
                .checksumCandidates = {
                    {0x01E0EF10u, 0x01E0EF20u},
                    {0x01E1EF10u, 0x01E1EF20u},
                },
                .busyFlagAddress = 0x01E212C8u,
                .completionRules = {
                    {0x002EAC20u, true, true, false},
                    {0x002EAC30u, true, true, true},
                    {0x002FAC20u, true, true, false},
                    {0x002FAC30u, true, true, true},
                },
            };
        }

        CriDtxBindings recvxCriDtxBindings()
        {
            return {
                .serviceName = "CRI DTX",
                .sid = 0x7D000000u,
                .urpcObjectBase = 0x01F18000u,
                .urpcObjectLimit = 0x01F1FF00u,
                .urpcObjectStride = 0x20u,
                .urpcFunctionTableBase = 0x0033FED0u,
                .urpcObjectTableBase = 0x0033FFD0u,
                .dispatcherFunctionAddress = 0x002FABC0u,
                .rpcServerPoolBase = 0x01F10000u,
                .rpcServerStride = 0x80u,
            };
        }

        ClFileBindings lotrClFileBindings()
        {
            return {
                .serviceName = "CLFILE",
                .sid = 0x0000FF01u,
                .rpc = {},
            };
        }

        SoundUpdateStubBindings lotrSoundBindings()
        {
            return {
                .serviceName = "SOUND update compatibility stub",
                .sid = 0x00012345u,
                .activeStreamCountOffset = 0u,
                .responseCounterOffset = 4u,
                .zeroReceiveBuffer = true,
                .signalNowaitCompletion = true,
                .completeQueuedPlayStreams = true,
                .suppressedCompletionCallbacks = {},
            };
        }

        SdrdrvBindings fatalFrameSdrdrvBindings()
        {
            return {
                .serviceName = "SDRDRV",
                .sid = 0x19740512u,
                .imageHeaderAddress = 0x012F0000u,
                .sectorSize = 2048u,
                .statusOffset = 0x6Cu,
                .statusStride = 8u,
                .statusSlotMask = 0x1Fu,
                .completeValue = 0u,
                .imageHeaderLowerName = "img_hd.bin",
                .imageHeaderUpperName = "IMG_HD.BIN",
                .imageBodyLowerName = "img_bd.bin",
                .imageBodyUpperName = "IMG_BD.BIN",
            };
        }
    }

    ServiceList createCoreServices(IopHost &host)
    {
        ServiceList services;
        services.emplace_back(createMcservService(host));
        services.emplace_back(createDbcmanService(host));
        services.emplace_back(createLibSdService(host));
        return services;
    }

    std::vector<ProfileDefinition> createBuiltinProfiles()
    {
        std::vector<ProfileDefinition> profiles;

        profiles.push_back({
            "guitar-hero-ps2",
            "builtin",
            {.entryPoint = 0x00100D68u},
            [](IopHost &host, const GameIdentity &)
            {
                ServiceList services;
                services.emplace_back(createFileioService(host));
                services.emplace_back(createUsbKbService(host));
                // Opt-in, and the switch is this way round deliberately.
                // The service is correct: it answers CTL 0x190 and drives
                // StreamEE 2 -> 3 on a stock build with no probes, which is
                // what the queue asked for. But it lands the game on a
                // CharBonesSamples assert that calls exit(1) at about 94s,
                // where the unserviced build sat at loading_screen forever, so
                // measured on held rung it is a regression from 8 to 0.
                //
                // Default-on would hand every later round a floor that dies
                // before it can hold anything, and the floor is what stops an
                // unattended loop from wandering. So it stays off until the
                // assert is fixed, and then this flips back and gets scored
                // properly. Evidence in notes/song-load-crash.md.
                if (std::getenv("GHPC_SYNTH_IOP") != nullptr)
                {
                    services.emplace_back(createSynthService(host));
                }
                return services;
            },
        });

        profiles.push_back({
            "recvx-us",
            "builtin",
            {.elfName = "slus_201.84"},
            [](IopHost &host, const GameIdentity &)
            {
                ServiceList services;
                services.emplace_back(createTsnddrvService(host, recvxTsnddrvBindings()));
                services.emplace_back(createCriDtxService(host, recvxCriDtxBindings()));
                return services;
            },
        });

        profiles.push_back({
            "lotr-two-towers-us",
            "builtin",
            {.elfName = "SLUS_205.78"},
            [](IopHost &host, const GameIdentity &)
            {
                ServiceList services;
                services.emplace_back(createClFileService(host, lotrClFileBindings()));
                services.emplace_back(createSoundUpdateStubService(host, lotrSoundBindings()));
                return services;
            },
        });

        profiles.push_back({
            "fatal-frame-us",
            "builtin",
            {.elfName = "SLUS_203.88"},
            [](IopHost &host, const GameIdentity &)
            {
                ServiceList services;
                services.emplace_back(createSdrdrvService(host, fatalFrameSdrdrvBindings()));
                return services;
            },
        });

        return profiles;
    }
}
