#include "Hooks_SteamUI.h"

#include <cstdint>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookMacros.h"

namespace {

#pragma pack(push, 1)
struct CSteamApp {
    uint32_t nAppID;
    uint32_t AppStateFlags;
    uint32_t OwnershipFlags;
    uint32_t PurchasedTime;
};
#pragma pack(pop)

HOOK_FUNC(FillInAppOverview, void*, void* pThis, void* pAppOverview, CSteamApp* pApp) {
    if (pApp && (LuaConfig::HasApp(pApp->nAppID) || LuaConfig::HasDepot(pApp->nAppID))) {
        if (pApp->PurchasedTime == 0) {
            pApp->PurchasedTime = 1600000000; // Provide synthetic timestamp to ensure library visibility
            spdlog::debug("Hooks_SteamUI: Set synthetic PurchasedTime for uninstalled AppID {}", pApp->nAppID);
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

void Uninstall() {}

} // namespace Hooks_SteamUI
