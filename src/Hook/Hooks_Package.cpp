#include "Hooks_Package.h"

#include <cstdint>
#include <cstring>
#include <set>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_set>
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
PackageInfo* g_pInjectedPackage = nullptr;
bool g_licenseInitialized = false;
bool g_licenseRefreshPending = false;
bool g_inBroadcast = false;
constexpr PackageId_t kInjectedPackageId = kSteamDefaultBasePackageId;
constexpr uint64_t kInjectedPkgAccessToken = kSteamDefaultBasePackageAccessToken;

RESOLVE_FUNC(CUtlMemoryGrow, void*, void*, int);
RESOLVE_FUNC(MarkLicenseAsChanged, int64_t, void*, uint32_t, bool);
RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool, void*);

std::vector<AppId_t> g_injectedAppIds;
std::vector<DepotId_t> g_injectedDepotIds;

bool CUtlMemoryGrowWrap(CUtlVector<AppId_t>* pVec, int grow_size) {
    if (!oCUtlMemoryGrow) {
        spdlog::warn("Hooks_Package: oCUtlMemoryGrow not ready, cannot grow");
        return false;
    }
    return oCUtlMemoryGrow(pVec, grow_size) != nullptr;
}

bool MarkLicenseAsChangedAndProcessUpdates() {
    if (!g_pCUser || !oMarkLicenseAsChanged || !oProcessPendingLicenseUpdates || g_inBroadcast) {
        return false;
    }
    g_inBroadcast = true;
    oMarkLicenseAsChanged(g_pCUser, kInjectedPackageId, true);
    oProcessPendingLicenseUpdates(g_pCUser);
    g_inBroadcast = false;
    spdlog::info("Hooks_Package: Dispatched AppLicensesChanged notification to Steam UI");
    return true;
}

void TryProcessPendingLicenseRefresh() {
    if (!g_licenseRefreshPending)
        return;
    if (MarkLicenseAsChangedAndProcessUpdates()) {
        g_licenseRefreshPending = false;
    }
}

bool InitFakeLicenseOnce(PackageInfo* pPkg) {
    if (!pPkg)
        return false;

    // Check package status
    if (pPkg->Status != EPackageStatus::Available) {
        spdlog::warn("Hooks_Package: Package 0 status is not Available ({}), skipping injection",
                     static_cast<int>(pPkg->Status));
        return false;
    }

    // 1. Inject pure AppIDs (Base Games & DLCs) into AppIdVec
    auto unlockedApps = LuaConfig::GetUnlockedApps();
    std::vector<AppId_t> appIds(unlockedApps.begin(), unlockedApps.end());
    if (Config::IsAutoUnlockDlcEnabled()) {
        auto autoDlcs = Metadata::DlcStore::GetAllKnownDlcs();
        appIds.insert(appIds.end(), autoDlcs.begin(), autoDlcs.end());
        std::unordered_set<AppId_t> dedup(appIds.begin(), appIds.end());
        appIds.assign(dedup.begin(), dedup.end());
    }

    if (!appIds.empty()) {
        uint32_t oldSize = static_cast<uint32_t>(pPkg->AppIdVec.m_Size);
        uint32_t numToAdd = static_cast<uint32_t>(appIds.size());
        spdlog::info("Hooks_Package: InitFakeLicense(PackageId=0): injecting {} apps into AppIdVec, oldSize={}",
                     numToAdd, oldSize);

        if (CUtlMemoryGrowWrap(&pPkg->AppIdVec, static_cast<int>(numToAdd))) {
            for (uint32_t i = 0; i < numToAdd; ++i) {
                pPkg->AppIdVec.m_Memory.m_pMemory[oldSize + i] = appIds[i];
            }
            pPkg->AppIdVec.m_Size = static_cast<int>(oldSize + numToAdd);
        } else {
            spdlog::warn("Hooks_Package: Failed to grow Package 0 AppId vector via CUtlMemoryGrow");
        }
    }

    // 2. Inject DepotIDs into DepotIdVec
    auto depotKeys = LuaConfig::GetDepotKeys();
    std::vector<DepotId_t> depotIds;
    for (const auto& [depotId, _] : depotKeys) {
        depotIds.push_back(depotId);
    }

    if (!depotIds.empty()) {
        uint32_t oldDepotSize = static_cast<uint32_t>(pPkg->DepotIdVec.m_Size);
        uint32_t numDepotsToAdd = static_cast<uint32_t>(depotIds.size());
        spdlog::info("Hooks_Package: InitFakeLicense(PackageId=0): injecting {} depots into DepotIdVec, oldSize={}",
                     numDepotsToAdd, oldDepotSize);

        if (CUtlMemoryGrowWrap(&pPkg->DepotIdVec, static_cast<int>(numDepotsToAdd))) {
            for (uint32_t i = 0; i < numDepotsToAdd; ++i) {
                pPkg->DepotIdVec.m_Memory.m_pMemory[oldDepotSize + i] = depotIds[i];
            }
            pPkg->DepotIdVec.m_Size = static_cast<int>(oldDepotSize + numDepotsToAdd);
        } else {
            spdlog::warn("Hooks_Package: Failed to grow Package 0 DepotId vector via CUtlMemoryGrow");
        }
    }

    g_pInjectedPackage = pPkg;
    g_licenseInitialized = true;
    g_licenseRefreshPending = true;
    spdlog::info(
        "Hooks_Package: Injected {} apps and {} depots into Package 0 (AppIdVec Size: {}, DepotIdVec Size: {})",
        appIds.size(), depotIds.size(), pPkg->AppIdVec.m_Size, pPkg->DepotIdVec.m_Size);
    TryProcessPendingLicenseRefresh();
    return true;
}

HOOK_FUNC(GetPackageInfo, void*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    if (!g_pCPackageInfo) {
        g_pCPackageInfo = pThis;
        spdlog::info("Hooks_Package: Captured CPackageInfo instance at {:p}", g_pCPackageInfo);
    }

    void* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
    if (packageId == kInjectedPackageId && pPkg && !g_licenseInitialized) {
        InitFakeLicenseOnce(reinterpret_cast<PackageInfo*>(pPkg));
    }
    return pPkg;
}

bool TryInitFakeLicenseOnce() {
    if (g_licenseInitialized)
        return true;
    if (g_pCPackageInfo && oGetPackageInfo) {
        PackageInfo* pPkg = reinterpret_cast<PackageInfo*>(
            oGetPackageInfo(g_pCPackageInfo, kInjectedPackageId, kInjectedPkgAccessToken));
        if (!pPkg) {
            spdlog::warn("Hooks_Package: GetPackageInfo returned null for injected package");
            return false;
        }
        if (!g_pInjectedPackage)
            g_pInjectedPackage = pPkg;
        return InitFakeLicenseOnce(pPkg);
    }
    return false;
}

void UpdateInjectedPackages() {
    auto unlockedApps = LuaConfig::GetUnlockedApps();
    std::set<AppId_t> allApps(unlockedApps.begin(), unlockedApps.end());
    if (Config::IsAutoUnlockDlcEnabled()) {
        auto autoDlcs = Metadata::DlcStore::GetAllKnownDlcs();
        allApps.insert(autoDlcs.begin(), autoDlcs.end());
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

    // If Package 0 is already captured, trigger hot re-injection
    if (g_pInjectedPackage) {
        InitFakeLicenseOnce(g_pInjectedPackage);
    } else if (g_pCPackageInfo && oGetPackageInfo) {
        oGetPackageInfo(g_pCPackageInfo, kInjectedPackageId, kInjectedPkgAccessToken);
    }

    g_licenseRefreshPending = true;
    TryProcessPendingLicenseRefresh();
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
    }

    bool result = oCheckAppOwnership ? oCheckAppOwnership(pObj, appId, pOwn) : false;
    TryInitFakeLicenseOnce();
    TryProcessPendingLicenseRefresh();

    // 1. Anti-Cheat Protected Game Check -> Silent bypass to native logic to guarantee account safety
    if (Security::AntiCheatGuard::IsProtectedApp(appId)) {
        spdlog::debug("Hooks_Package: AppID {} is protected by anti-cheat whitelist; executing native check", appId);
        return result;
    }

    if (result) {
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
            auto* pOwnership = reinterpret_cast<AppOwnership*>(pOwn);
            pOwnership->PackageId = kInjectedPackageId;
            pOwnership->ReleaseState = EAppReleaseState::Released;
            if (g_pCUser) {
                // Steam CUser has AccountID at offset 0x1E4
                pOwnership->SteamId32 =
                    *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(g_pCUser) + 0x1E4);
            }
            pOwnership->ExistInPackageNums = kSteamDefaultInjectedPackageCount;
            pOwnership->bOwnsLicense = true;
            pOwnership->bFreeLicense = true;
            pOwnership->bBorrowed = false;
            pOwnership->bFamilyShared = false;
        }
        spdlog::info("Hooks_Package: CheckAppOwnership(appId={}) -> unlocked via OmniSteam", appId);
        return true;
    }

    return false;
}
} // namespace

namespace Hooks_Package {
void Install() {
    uintptr_t fnGrow = PatternLoader::GetFunctionAddress("CUtlMemoryGrow");
    if (fnGrow) {
        oCUtlMemoryGrow = reinterpret_cast<CUtlMemoryGrow_t>(fnGrow);
        spdlog::info("Hooks_Package: Resolved CUtlMemoryGrow at {:p}", reinterpret_cast<void*>(fnGrow));
    } else {
        spdlog::warn("Hooks_Package: CUtlMemoryGrow signature not resolved");
    }

    uintptr_t fnCheck = PatternLoader::GetFunctionAddress("CheckAppOwnership");
    if (fnCheck) {
        ATTACH_HOOK(fnCheck, CheckAppOwnership);
        spdlog::info("Hooks_Package: Successfully installed CheckAppOwnership hook at {:p}",
                     reinterpret_cast<void*>(fnCheck));
    } else {
        spdlog::warn("Hooks_Package: CheckAppOwnership signature not resolved");
    }

    uintptr_t fnGetPkg = PatternLoader::GetFunctionAddress("GetPackageInfo");
    if (fnGetPkg) {
        ATTACH_HOOK(fnGetPkg, GetPackageInfo);
        spdlog::info("Hooks_Package: Successfully installed GetPackageInfo hook at {:p}",
                     reinterpret_cast<void*>(fnGetPkg));
    } else {
        spdlog::warn("Hooks_Package: GetPackageInfo signature not resolved");
    }

    uintptr_t fnMark = PatternLoader::GetFunctionAddress("MarkLicenseAsChanged");
    if (fnMark) {
        oMarkLicenseAsChanged = reinterpret_cast<MarkLicenseAsChanged_t>(fnMark);
        spdlog::info("Hooks_Package: Resolved MarkLicenseAsChanged at {:p}", reinterpret_cast<void*>(fnMark));
    } else {
        spdlog::warn("Hooks_Package: MarkLicenseAsChanged signature not resolved");
    }

    uintptr_t fnProcess = PatternLoader::GetFunctionAddress("ProcessPendingLicenseUpdates");
    if (fnProcess) {
        oProcessPendingLicenseUpdates = reinterpret_cast<ProcessPendingLicenseUpdates_t>(fnProcess);
        spdlog::info("Hooks_Package: Resolved ProcessPendingLicenseUpdates at {:p}",
                     reinterpret_cast<void*>(fnProcess));
    } else {
        spdlog::warn("Hooks_Package: ProcessPendingLicenseUpdates signature not resolved");
    }
}

void Uninstall() {}

void NotifyLicenseChanged() {
    MarkLicenseAsChangedAndProcessUpdates();
}

void SyncInjectedLicenses() {
    NotifyLicenseChanged();
}
} // namespace Hooks_Package
