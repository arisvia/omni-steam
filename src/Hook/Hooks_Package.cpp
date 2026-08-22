#include <cstdint>
#include <cstring>
#include <set>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookMacros.h"

namespace {

void* g_pCUser = nullptr;
void* g_pCPackageInfo = nullptr;
constexpr PackageId_t kInjectedPackageId = kSteamDefaultBasePackageId;

std::vector<AppId_t> g_injectedAppIds;
std::vector<DepotId_t> g_injectedDepotIds;
void UpdateInjectedPackages() {
    auto unlockedApps = LuaConfig::GetUnlockedApps();
    std::set<AppId_t> allApps;
    for (uint32_t id : unlockedApps) {
        allApps.insert(id);
    }
    g_injectedAppIds.assign(allApps.begin(), allApps.end());

    auto depotKeys = LuaConfig::GetDepotKeys();
    std::set<DepotId_t> allDepots;
    for (const auto& [depotId, _] : depotKeys) {
        allDepots.insert(depotId);
    }
    g_injectedDepotIds.assign(allDepots.begin(), allDepots.end());

    spdlog::info("Hooks_Package: Prepared injected package payload with {} apps and {} depots", g_injectedAppIds.size(),
                 g_injectedDepotIds.size());
}

void NotifyLicensesChanged() {
    UpdateInjectedPackages();
    spdlog::info("Hooks_Package: Injected licenses synchronized ({} apps, {} depots)", g_injectedAppIds.size(),
                 g_injectedDepotIds.size());
}
HOOK_FUNC(GetPackageInfo, PackageInfo*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    if (!g_pCPackageInfo) {
        g_pCPackageInfo = pThis;
        spdlog::info("Hooks_Package: Captured CPackageInfo instance at {:p}", g_pCPackageInfo);
    }

    PackageInfo* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;

    if (packageId == kInjectedPackageId && pPkg) {
        if (g_injectedAppIds.empty()) {
            UpdateInjectedPackages();
        }
        uint8_t* raw = reinterpret_cast<uint8_t*>(pPkg);
        if constexpr (sizeof(void*) == 8) {
            // 64-bit SteamClient verified offsets (Windows x64, Linux x86_64, macOS)
            *reinterpret_cast<uint32_t*>(raw + 0x18) = 3; // Status = Available (3)
            *reinterpret_cast<uintptr_t*>(raw + 0x40) = reinterpret_cast<uintptr_t>(g_injectedAppIds.data());
            *reinterpret_cast<int32_t*>(raw + 0x50) = static_cast<int32_t>(g_injectedAppIds.size());
            *reinterpret_cast<uintptr_t*>(raw + 0x60) = reinterpret_cast<uintptr_t>(g_injectedDepotIds.data());
            *reinterpret_cast<int32_t*>(raw + 0x70) = static_cast<int32_t>(g_injectedDepotIds.size());
        } else {
            // 32-bit SteamClient verified offsets (Linux i386)
            *reinterpret_cast<uint32_t*>(raw + 0x0C) = 3; // Status = Available (3)
            *reinterpret_cast<uint32_t*>(raw + 0x20) =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_injectedAppIds.data()));
            *reinterpret_cast<int32_t*>(raw + 0x1C) = static_cast<int32_t>(g_injectedAppIds.size());
            *reinterpret_cast<uint32_t*>(raw + 0x28) =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_injectedDepotIds.data()));
            *reinterpret_cast<int32_t*>(raw + 0x30) = static_cast<int32_t>(g_injectedDepotIds.size());
        }
        spdlog::info("Hooks_Package: Populated Package 0 with {} apps and {} depots at {:p}", g_injectedAppIds.size(),
                     g_injectedDepotIds.size(), reinterpret_cast<void*>(pPkg));
    }

    return pPkg;
}
HOOK_FUNC(CheckAppOwnership, bool, void* pObj, uint32_t appId, void* pOwn) {
    if (!g_pCUser) {
        g_pCUser = pObj;
        spdlog::info("Hooks_Package: Captured CUser instance at {:p}", g_pCUser);
        NotifyLicensesChanged();
    }

    bool originalResult = oCheckAppOwnership ? oCheckAppOwnership(pObj, appId, pOwn) : false;
    if (originalResult) {
        // App is natively owned on Steam account; preserve legitimate package ID and license state
        return true;
    }

    if (LuaConfig::HasApp(appId) || LuaConfig::HasDepot(appId)) {
        if (pOwn) {
            uint8_t* raw = reinterpret_cast<uint8_t*>(pOwn);
            if constexpr (sizeof(void*) == 8) {
                // 64-bit SteamClient verified memory offsets (Windows x64, Linux x86_64, macOS)
                *reinterpret_cast<uint32_t*>(raw + 0x1C) =
                    static_cast<uint32_t>(EAppReleaseState::Released); // ReleaseState = Released (4)
                *reinterpret_cast<uint32_t*>(raw + 0x20) = 1;          // ExistInPackageNums = 1
                raw[0x28] = 1;                                         // bOwnsLicense = true
                raw[0x30] = 1;                                         // bIsSubscribed = true
                raw[0x33] = 1;
                raw[0x34] = 1;
            } else {
                // 32-bit SteamClient verified memory offsets (Linux i386)
                *reinterpret_cast<uint32_t*>(raw + 0x04) = static_cast<uint32_t>(EAppReleaseState::Released);
                *reinterpret_cast<uint32_t*>(raw + 0x08) = 1;
                raw[0x0C] = 1; // bOwnsLicense
                raw[0x0D] = 0; // bFreeLicense
            }
        }
        spdlog::info("Hooks_Package: CheckAppOwnership(appId={}) -> unlocked via OmniSteam", appId);
        return true;
    }

    return false;
}
} // namespace
namespace Hooks_Package {
void Install() {
    // Attach hooks
    uintptr_t fnGetPkg = PatternLoader::GetFunctionAddress("GetPackageInfo");
    if (fnGetPkg) {
        ATTACH_HOOK(fnGetPkg, GetPackageInfo);
        spdlog::info("Hooks_Package: Successfully installed GetPackageInfo hook at {:p}",
                     reinterpret_cast<void*>(fnGetPkg));
    }

    uintptr_t fnCheck = PatternLoader::GetFunctionAddress("CheckAppOwnership");
    if (fnCheck) {
        ATTACH_HOOK(fnCheck, CheckAppOwnership);
        spdlog::info("Hooks_Package: Successfully installed CheckAppOwnership hook at {:p}",
                     reinterpret_cast<void*>(fnCheck));
    } else {
        spdlog::warn("Hooks_Package: CheckAppOwnership signature not resolved");
    }
}

void Uninstall() {}

void SyncInjectedLicenses() {
    NotifyLicensesChanged();
}
} // namespace Hooks_Package
