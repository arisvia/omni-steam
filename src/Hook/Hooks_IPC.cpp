#include <cstdint>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Metadata/SteamIPC.h"

#include "Hook/HookMacros.h"

namespace Hooks_Package {
void SyncInjectedLicenses();
}

namespace {

#pragma pack(push, 8)
struct CallbackMsg_t {
    int32_t m_hSteamUser;
    int32_t m_iCallback;
    uint8_t* m_pubParam;
    int32_t m_cubParam;
};
#pragma pack(pop)

HOOK_FUNC(BGetCallback, bool, int32_t hSteamPipe, CallbackMsg_t* pCallbackMsg) {
    if (!oBGetCallback)
        return false;

    bool result = oBGetCallback(hSteamPipe, pCallbackMsg);
    if (result && pCallbackMsg) {
        // Intercept and trigger license sync when Steam broadcasts LicensesUpdated
        if (pCallbackMsg->m_iCallback == k_iCallback_LicensesUpdated) {
            spdlog::info("Hooks_IPC: Received LicensesUpdated callback ({}), triggering package sync",
                         k_iCallback_LicensesUpdated);
            Hooks_Package::SyncInjectedLicenses();
        }
    }
    return result;
}
HOOK_FUNC(IPCProcessMessage, bool, void* pServer, int32_t hSteamPipe, void* pRead, void* pWrite) {
    if (!oIPCProcessMessage)
        return false;

    bool result = oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite);
    return result;
}

} // namespace

namespace Hooks_IPC {

void Install() {
    uintptr_t fnAddress = PatternLoader::GetFunctionAddress("IPCProcessMessage");
    if (fnAddress != 0) {
        ATTACH_HOOK(fnAddress, IPCProcessMessage);
        spdlog::info("Hooks_IPC: Successfully installed IPCProcessMessage hook at {:p}",
                     reinterpret_cast<void*>(fnAddress));
    } else {
        spdlog::warn("Hooks_IPC: IPCProcessMessage signature not resolved");
    }

    uintptr_t fnCallback = PatternLoader::GetFunctionAddress("BGetCallback");
    if (fnCallback != 0) {
        ATTACH_HOOK(fnCallback, BGetCallback);
        spdlog::info("Hooks_IPC: Successfully installed BGetCallback hook at {:p}",
                     reinterpret_cast<void*>(fnCallback));
    } else {
        spdlog::warn("Hooks_IPC: BGetCallback signature not resolved");
    }
}

void Uninstall() {}

} // namespace Hooks_IPC
