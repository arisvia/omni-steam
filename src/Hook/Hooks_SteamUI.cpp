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
        if (appId > 0 && LuaConfig::Has(appId)) {
            // SteamUI builds the library snapshot from these CSteamApp fields;
            // ShouldShowAppInLibrary() gates on OwnsLicense. The controller
            // derives them from licenses the server never issued for injected
            // ids, so force the owned+released view here (upstream parity).
            pApp->PurchasedTime = kSteamSyntheticPurchasedTime;
            pApp->ReleaseState = EAppReleaseState::Released;
            pApp->OwnershipFlags = k_EAppOwnershipFlags_OwnsLicense | k_EAppOwnershipFlags_LicensePermanent;
            spdlog::info("Hooks_SteamUI: Injected overview fields for AppID {} (PurchasedTime={}, Flags=0x{:X})", appId,
                         pApp->PurchasedTime, pApp->OwnershipFlags);
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
