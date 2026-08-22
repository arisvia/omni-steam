#include <cstdint>
#include <cstring>
#include <set>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/DlcStore.h"
#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Security/AntiCheatGuard.h"

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

HOOK_FUNC(CheckAppOwnership, bool, void* pObj, uint32_t appId, void* pOwn) {
    if (!g_pCUser) {
        g_pCUser = pObj;
        spdlog::info("Hooks_Package: Captured CUser instance at {:p}", g_pCUser);
        NotifyLicensesChanged();
    }

    // 1. Anti-Cheat Protected Game Check -> Silent bypass to native logic to guarantee account safety
    if (Security::AntiCheatGuard::IsProtectedApp(appId)) {
        spdlog::debug("Hooks_Package: AppID {} is protected by anti-cheat whitelist; executing native check", appId);
        return oCheckAppOwnership ? oCheckAppOwnership(pObj, appId, pOwn) : false;
    }

    bool originalResult = oCheckAppOwnership ? oCheckAppOwnership(pObj, appId, pOwn) : false;
    if (originalResult) {
        // App is natively owned on Steam account; trigger async DLC discovery for base game
        if (Config::IsAutoUnlockDlcEnabled()) {
            Metadata::DlcStore::AsyncFetchAppDlcs(appId);
        }
        return true;
    }
    bool isUnlocked = LuaConfig::HasApp(appId) || LuaConfig::HasDepot(appId) ||
                      (Config::IsAutoUnlockDlcEnabled() && Metadata::DlcStore::IsKnownDlc(appId));

    if (isUnlocked) {
        if (pOwn) {
            uint8_t* raw = reinterpret_cast<uint8_t*>(pOwn);
            if constexpr (sizeof(void*) == 8) {
                // 64-bit SteamClient verified memory offsets (Windows x64, Linux x86_64, macOS)
                *reinterpret_cast<uint32_t*>(raw + SteamOffsets::Ownership64::kExistInPackageNums) =
                    kSteamDefaultInjectedPackageCount;
                *reinterpret_cast<uint32_t*>(raw + SteamOffsets::Ownership64::kReleaseState) =
                    static_cast<uint32_t>(EAppReleaseState::Released);
                *reinterpret_cast<uint32_t*>(raw + SteamOffsets::Ownership64::kExistInPackageNumsFallback) =
                    kSteamDefaultInjectedPackageCount;
                raw[SteamOffsets::Ownership64::kOwnsLicense] = 1;
                raw[SteamOffsets::Ownership64::kIsSubscribed] = 1;
                raw[SteamOffsets::Ownership64::kActiveFlag1] = 1;
                raw[SteamOffsets::Ownership64::kActiveFlag2] = 1;
                raw[SteamOffsets::Ownership64::kActiveFlag3] = 1;
            } else {
                // 32-bit SteamClient verified memory offsets (Linux i386)
                *reinterpret_cast<uint32_t*>(raw + SteamOffsets::Ownership32::kReleaseState) =
                    static_cast<uint32_t>(EAppReleaseState::Released);
                *reinterpret_cast<uint32_t*>(raw + SteamOffsets::Ownership32::kExistInPackageNums) =
                    kSteamDefaultInjectedPackageCount;
                raw[SteamOffsets::Ownership32::kOwnsLicense] = 1;
                raw[SteamOffsets::Ownership32::kFreeLicense] = 0;
                raw[SteamOffsets::Ownership32::kIsSubscribed] = 1;
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
