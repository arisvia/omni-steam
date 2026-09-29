#include "Hooks_IPC.h"

#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Metadata/SteamIPC.h"
#include "Utils/Tickets/AppTicket.h"

#include "Hook/HookMacros.h"
#include "Hook/Hooks_Misc.h"

namespace {

struct ValveBuffer {
    CUtlMemory<uint8_t> m_Memory;
    int32_t m_Get;
    int32_t m_Put;
    int32_t m_nOffset;
    int32_t m_flags;

    uint8_t* Base() { return m_Memory.m_pMemory; }
    int32_t TellPut() const { return m_Put; }
};

HOOK_FUNC(IPCProcessMessage, bool, void* pServer, int32_t hSteamPipe, void* pRead, void* pWrite) {
    bool result = oIPCProcessMessage ? oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite) : false;

    if (result && pWrite && pRead) {
        AppId_t activeAppId = Hooks_Misc::GetActiveAppId();
        if (activeAppId != 0) {
            uint64_t spoofed = AppTicket::GetSpoofSteamID(activeAppId);
            if (spoofed != 0) {
                auto* writeBuf = reinterpret_cast<ValveBuffer*>(pWrite);
                auto* readBuf = reinterpret_cast<ValveBuffer*>(pRead);
                if (writeBuf && writeBuf->Base() && readBuf && readBuf->Base()) {
                    constexpr size_t kMinReq = sizeof(SteamIPC::IPCHeader) + sizeof(SteamIPC::InterfaceCallHeader);
                    if (readBuf->TellPut() >= static_cast<int32_t>(kMinReq)) {
                        const auto* reqHdr = reinterpret_cast<const SteamIPC::IPCHeader*>(readBuf->Base());
                        if (reqHdr->command == SteamIPC::EIPCCommand::InterfaceCall) {
                            const auto* callHdr = reinterpret_cast<const SteamIPC::InterfaceCallHeader*>(
                                readBuf->Base() + sizeof(SteamIPC::IPCHeader));
                            if (callHdr->interfaceID == SteamIPC::EIPCInterface::IClientUser) {
                                constexpr size_t kMinResp = sizeof(SteamIPC::IPCHeader) + sizeof(uint64_t);
                                if (writeBuf->TellPut() >= static_cast<int32_t>(kMinResp)) {
                                    uint64_t* pRespSteamId =
                                        reinterpret_cast<uint64_t*>(writeBuf->Base() + sizeof(SteamIPC::IPCHeader));
                                    if (*pRespSteamId != 0 && *pRespSteamId != spoofed) {
                                        spdlog::info("Hooks_IPC: Spoofed IClientUser::GetSteamID for AppID {} (0x{:X} "
                                                     "-> 0x{:X})",
                                                     activeAppId, *pRespSteamId, spoofed);
                                        *pRespSteamId = spoofed;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return result;
}

} // namespace

namespace Hooks_IPC {

void Install() {
    uintptr_t fnIPC = PatternLoader::GetFunctionAddress("IPCProcessMessage");
    if (fnIPC) {
        ATTACH_HOOK(fnIPC, IPCProcessMessage);
        spdlog::info("Hooks_IPC: Successfully installed IPCProcessMessage hook at {:p}",
                     reinterpret_cast<void*>(fnIPC));
    } else {
        spdlog::warn("Hooks_IPC: IPCProcessMessage signature not resolved");
    }
}

void Uninstall() {
    uintptr_t fnIPC = PatternLoader::GetFunctionAddress("IPCProcessMessage");
    if (fnIPC) {
        DETACH_HOOK(fnIPC, IPCProcessMessage);
    }
}

} // namespace Hooks_IPC
