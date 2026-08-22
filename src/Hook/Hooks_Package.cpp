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
void* g_pInjectedPackage = nullptr;
bool g_licenseInitialized = false;
bool g_licenseRefreshPending = false;
bool g_inBroadcast = false;
constexpr PackageId_t kInjectedPackageId = kSteamDefaultBasePackageId;
constexpr uint64_t kInjectedPkgAccessToken = kSteamDefaultBasePackageAccessToken;
RESOLVE_FUNC(MarkLicenseAsChanged, int64_t, void*, uint32_t, bool);
RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool, void*);

std::vector<AppId_t> g_injectedAppIds;
std::vector<DepotId_t> g_injectedDepotIds;
static std::vector<AppId_t> g_staticAppBuffer;

CUtlVector<AppId_t>* FindAppIdVector(void* pPkg) {
    if (!pPkg)
        return nullptr;
    auto* base = reinterpret_cast<uint8_t*>(pPkg);

    // Check known candidate offsets for AppIdVec in 64-bit/32-bit Steamclient PackageInfo
    for (size_t offset : {0x10, 0x40, 0x20, 0x18, 0x38, 0x28, 0x30, 0x08}) {
        auto* vec = reinterpret_cast<CUtlVector<AppId_t>*>(base + offset);
        if (vec->m_Size >= 0 && vec->m_Size < 100000 && vec->m_Memory.m_nAllocationCount >= 0 &&
            vec->m_Memory.m_nAllocationCount < 100000 && vec->m_Size <= vec->m_Memory.m_nAllocationCount) {
            if (vec->m_Size > 0 && vec->m_Memory.m_pMemory == nullptr)
                continue;
            spdlog::info("Hooks_Package: Resolved AppIdVec at offset 0x{:X} (Size: {}, Capacity: {})", offset,
                         vec->m_Size, vec->m_Memory.m_nAllocationCount);
            return vec;
        }
    }
    return nullptr;
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

bool InjectPackage0Apps(void* pPkg) {
    if (!pPkg)
        return false;

    auto* pAppIdVec = FindAppIdVector(pPkg);
    if (!pAppIdVec) {
        spdlog::warn("Hooks_Package: Could not resolve AppIdVec in Package 0");
        return false;
    }

    auto unlockedApps = LuaConfig::GetUnlockedApps();
    std::set<AppId_t> allApps(unlockedApps.begin(), unlockedApps.end());

    if (Config::IsAutoUnlockDlcEnabled()) {
        auto autoDlcs = Metadata::DlcStore::GetAllKnownDlcs();
        allApps.insert(autoDlcs.begin(), autoDlcs.end());
    }

    // Preserve existing AppIDs in Package 0
    if (pAppIdVec->m_Memory.m_pMemory && pAppIdVec->m_Size > 0) {
        for (int i = 0; i < pAppIdVec->m_Size; ++i) {
            allApps.insert(pAppIdVec->m_Memory.m_pMemory[i]);
        }
    }

    g_staticAppBuffer.assign(allApps.begin(), allApps.end());
    pAppIdVec->m_Memory.m_pMemory = g_staticAppBuffer.data();
    pAppIdVec->m_Memory.m_nAllocationCount = static_cast<int>(g_staticAppBuffer.size());
    pAppIdVec->m_Memory.m_nGrowSize = 0;
    pAppIdVec->m_Size = static_cast<int>(g_staticAppBuffer.size());
    pAppIdVec->m_pElements = g_staticAppBuffer.data();

    g_pInjectedPackage = pPkg;
    g_licenseInitialized = true;
    g_licenseRefreshPending = true;
    spdlog::info("Hooks_Package: Injected {} total apps/DLCs into Package 0 memory structure",
                 g_staticAppBuffer.size());
    TryProcessPendingLicenseRefresh();
    return true;
}

void BroadcastLicenseUpdates() {
    g_licenseRefreshPending = true;
    TryProcessPendingLicenseRefresh();
}

HOOK_FUNC(GetPackageInfo, void*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    if (!g_pCPackageInfo) {
        g_pCPackageInfo = pThis;
        spdlog::info("Hooks_Package: Captured CPackageInfo instance at {:p}", g_pCPackageInfo);
    }

    void* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
    if (packageId == kInjectedPackageId && pPkg && !g_licenseInitialized) {
        InjectPackage0Apps(pPkg);
    }
    return pPkg;
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
        InjectPackage0Apps(g_pInjectedPackage);
    } else if (g_pCPackageInfo && oGetPackageInfo) {
        oGetPackageInfo(g_pCPackageInfo, kInjectedPackageId, kInjectedPkgAccessToken);
    }

    BroadcastLicenseUpdates();
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

    if (!g_licenseInitialized && g_pCPackageInfo && oGetPackageInfo) {
        oGetPackageInfo(g_pCPackageInfo, kInjectedPackageId, kInjectedPkgAccessToken);
    }

    TryProcessPendingLicenseRefresh();

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
            auto* pOwnership = reinterpret_cast<AppOwnership*>(pOwn);
            pOwnership->PackageId = kInjectedPackageId;
            pOwnership->ReleaseState = EAppReleaseState::Released;
            pOwnership->ExistInPackageNums = kSteamDefaultInjectedPackageCount;
            pOwnership->bOwnsLicense = true;
            pOwnership->bFreeLicense = false;
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

void SyncInjectedLicenses() {
    NotifyLicensesChanged();
}
} // namespace Hooks_Package
