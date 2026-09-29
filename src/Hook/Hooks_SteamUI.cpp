#include "Hooks_SteamUI.h"

#include <cstdint>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookMacros.h"

namespace {

HOOK_FUNC(FillInAppOverview, void*, void* pThis, void* pAppOverview, CSteamApp* pApp) {
    if (pApp) {
        uint32_t appId = pApp->nAppID;
        if (appId > 0 && (LuaConfig::HasApp(appId) || LuaConfig::HasDepot(appId))) {
            pApp->PurchasedTime = kSteamSyntheticPurchasedTime;
            spdlog::info("Hooks_SteamUI: Injected PurchasedTime for AppID {} (PurchasedTime={})", appId,
                         pApp->PurchasedTime);
        }
    }
    return oFillInAppOverview ? oFillInAppOverview(pThis, pAppOverview, pApp) : nullptr;
}

} // namespace

namespace Hooks_SteamUI {

void Install() {
    uintptr_t fnAddress = PatternLoader::GetFunctionAddress("FillInAppOverview");
    if (fnAddress != 0) {
        ATTACH_HOOK(fnAddress, FillInAppOverview);
        spdlog::info("Hooks_SteamUI: Successfully installed FillInAppOverview hook at {:p}",
                     reinterpret_cast<void*>(fnAddress));
    } else {
        spdlog::warn("Hooks_SteamUI: FillInAppOverview signature not resolved (may be on non-UI process)");
    }
}

void Uninstall() {
    uintptr_t fnAddress = PatternLoader::GetFunctionAddress("FillInAppOverview");
    if (fnAddress != 0) {
        DETACH_HOOK(fnAddress, FillInAppOverview);
    }
}

} // namespace Hooks_SteamUI
