#include "Hooks_Package.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
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

void* AtomicLoadPtr(std::atomic<void*>& slot) {
    return slot.load(std::memory_order_acquire);
}

void* CaptureOnce(std::atomic<void*>& slot, void* value) {
    void* expected = nullptr;
    if (value && slot.compare_exchange_strong(expected, value, std::memory_order_acq_rel)) {
        return value;
    }
    return slot.load(std::memory_order_acquire);
}

std::atomic<void*> g_pCUser{nullptr};
std::atomic<void*> g_pCPackageInfo{nullptr};
std::atomic<bool> g_licenseInitialized{false};
std::atomic<bool> g_licenseRefreshPending{false};
std::atomic<bool> g_inBroadcast{false};
std::atomic<bool> g_injectInProgress{false};

PackageInfo* g_pInjectedPackage = nullptr;
std::vector<AppId_t> g_currentInjectedAppIds;
std::vector<DepotId_t> g_currentInjectedDepotIds;

std::mutex g_injectMutex;

constexpr PackageId_t kInjectedPackageId = kSteamDefaultBasePackageId;
constexpr uint64_t kInjectedPkgAccessToken = kSteamDefaultBasePackageAccessToken;

RESOLVE_FUNC(CUtlMemoryGrow, void*, void*, int);
RESOLVE_FUNC(MarkLicenseAsChanged, int64_t, void*, uint32_t, bool);
RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool, void*);

template <typename T> bool GrowAndAppend(CUtlVector<T>* pVec, const std::vector<T>& values) {
    if (values.empty())
        return true;
    if (!oCUtlMemoryGrow) {
        spdlog::warn("Hooks_Package: oCUtlMemoryGrow not ready, cannot grow");
        return false;
    }
    const int oldSize = pVec->m_Size;
    const int numToAdd = static_cast<int>(values.size());
    if (!oCUtlMemoryGrow(pVec, numToAdd)) {
        spdlog::warn("Hooks_Package: CUtlMemoryGrow returned null, cannot append {} entries", numToAdd);
        return false;
    }
    for (int i = 0; i < numToAdd; ++i) {
        pVec->m_Memory.m_pMemory[oldSize + i] = values[i];
    }
    pVec->m_Size = static_cast<int>(oldSize + numToAdd);
    return true;
}

template <typename T> bool FastRemoveValue(CUtlVector<T>* pVec, T value) {
    for (int i = 0; i < pVec->m_Size; ++i) {
        if (pVec->m_Memory.m_pMemory[i] == value) {
            pVec->m_Memory.m_pMemory[i] = pVec->m_Memory.m_pMemory[pVec->m_Size - 1];
            pVec->m_Size--;
            return true;
        }
    }
    return false;
}

bool MarkLicenseAsChangedAndProcessUpdates() {
    if (!AtomicLoadPtr(g_pCUser) || !oMarkLicenseAsChanged || !oProcessPendingLicenseUpdates || g_inBroadcast.load()) {
        return false;
    }
    g_inBroadcast.store(true);
    oMarkLicenseAsChanged(AtomicLoadPtr(g_pCUser), kInjectedPackageId, true);
    oProcessPendingLicenseUpdates(AtomicLoadPtr(g_pCUser));
    g_inBroadcast.store(false);
    spdlog::info("Hooks_Package: Dispatched AppLicensesChanged notification to Steam UI");
    return true;
}

void TryProcessPendingLicenseRefresh() {
    if (!g_licenseRefreshPending.load())
        return;
    if (MarkLicenseAsChangedAndProcessUpdates()) {
        g_licenseRefreshPending.store(false);
    }
}

std::vector<AppId_t> CollectDesiredAppIds() {
    auto unlockedApps = LuaConfig::GetUnlockedApps();
    std::set<AppId_t> allApps(unlockedApps.begin(), unlockedApps.end());
    if (Config::IsAutoUnlockDlcEnabled()) {
        auto autoDlcs = Metadata::DlcStore::GetAllKnownDlcs();
        allApps.insert(autoDlcs.begin(), autoDlcs.end());
    }
    return std::vector<AppId_t>(allApps.begin(), allApps.end());
}

std::vector<DepotId_t> CollectDesiredDepotIds() {
    auto depotKeys = LuaConfig::GetDepotKeys();
    std::set<DepotId_t> allDepots;
    for (const auto& [depotId, _] : depotKeys) {
        allDepots.insert(depotId);
    }
    return std::vector<DepotId_t>(allDepots.begin(), allDepots.end());
}

// Applies the desired app/depot sets to Package 0 as a delta so repeated
// invocations (hot reload, racing hook threads) never duplicate entries.
// Removals only ever touch IDs previously injected by us.
void ApplyDesiredLicenseDelta(PackageInfo* pPkg, const std::vector<AppId_t>& desiredApps,
                              const std::vector<DepotId_t>& desiredDepots) {
    std::set<AppId_t> desiredAppComponent(desiredApps.begin(), desiredApps.end());
    std::set<DepotId_t> desiredDepotComponent(desiredDepots.begin(), desiredDepots.end());

    size_t removedApps = 0;
    size_t removedDepots = 0;
    for (AppId_t id : g_currentInjectedAppIds) {
        if (!desiredAppComponent.count(id) && FastRemoveValue(&pPkg->AppIdVec, id)) {
            ++removedApps;
        }
    }
    for (DepotId_t id : g_currentInjectedDepotIds) {
        if (!desiredDepotComponent.count(id) && FastRemoveValue(&pPkg->DepotIdVec, id)) {
            ++removedDepots;
        }
    }

    std::unordered_set<AppId_t> presentInVec;
    presentInVec.reserve(static_cast<size_t>(pPkg->AppIdVec.m_Size));
    for (int i = 0; i < pPkg->AppIdVec.m_Size; ++i) {
        presentInVec.insert(pPkg->AppIdVec.m_Memory.m_pMemory[i]);
    }
    std::vector<AppId_t> appAdditions;
    for (AppId_t id : desiredApps) {
        if (!presentInVec.count(id)) {
            appAdditions.push_back(id);
        }
    }

    std::unordered_set<DepotId_t> presentDepotsInVec;
    presentDepotsInVec.reserve(static_cast<size_t>(pPkg->DepotIdVec.m_Size));
    for (int i = 0; i < pPkg->DepotIdVec.m_Size; ++i) {
        presentDepotsInVec.insert(pPkg->DepotIdVec.m_Memory.m_pMemory[i]);
    }
    std::vector<DepotId_t> depotAdditions;
    for (DepotId_t id : desiredDepots) {
        if (!presentDepotsInVec.count(id)) {
            depotAdditions.push_back(id);
        }
    }

    bool appsOk = GrowAndAppend(&pPkg->AppIdVec, appAdditions);
    bool depotsOk = GrowAndAppend(&pPkg->DepotIdVec, depotAdditions);

    if (appsOk) {
        g_currentInjectedAppIds.assign(desiredApps.begin(), desiredApps.end());
    }
    if (depotsOk) {
        g_currentInjectedDepotIds.assign(desiredDepots.begin(), desiredDepots.end());
    }

    spdlog::info("Hooks_Package: License delta applied (+{} apps, +{} depots, -{} apps, -{} depots; AppIdVec: {}, "
                 "DepotIdVec: {})",
                 appAdditions.size(), depotAdditions.size(), removedApps, removedDepots, pPkg->AppIdVec.m_Size,
                 pPkg->DepotIdVec.m_Size);
}

bool InitFakeLicenseOnce(PackageInfo* pPkg) {
    if (!pPkg)
        return false;

    std::lock_guard<std::mutex> lock(g_injectMutex);
    auto desiredApps = CollectDesiredAppIds();
    auto desiredDepots = CollectDesiredDepotIds();

    ApplyDesiredLicenseDelta(pPkg, desiredApps, desiredDepots);

    g_pInjectedPackage = pPkg;
    g_licenseInitialized.store(true);
    g_licenseRefreshPending.store(true);
    TryProcessPendingLicenseRefresh();
    return true;
}

HOOK_FUNC(GetPackageInfo, void*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    CaptureOnce(g_pCPackageInfo, pThis);

    void* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
    if (packageId == kInjectedPackageId && pPkg && !g_licenseInitialized.load()) {
        InitFakeLicenseOnce(reinterpret_cast<PackageInfo*>(pPkg));
    }
    return pPkg;
}

bool TryInitFakeLicenseOnce() {
    if (g_licenseInitialized.load())
        return true;
    void* cpi = AtomicLoadPtr(g_pCPackageInfo);
    if (cpi && oGetPackageInfo) {
        PackageInfo* pPkg =
            reinterpret_cast<PackageInfo*>(oGetPackageInfo(cpi, kInjectedPackageId, kInjectedPkgAccessToken));
        if (!pPkg) {
            spdlog::warn("Hooks_Package: GetPackageInfo returned null for injected package");
            return false;
        }
        return InitFakeLicenseOnce(pPkg);
    }
    return false;
}

void UpdateInjectedPackages() {
    auto desiredApps = CollectDesiredAppIds();
    auto desiredDepots = CollectDesiredDepotIds();

    {
        std::lock_guard<std::mutex> lock(g_injectMutex);
        if (g_pInjectedPackage) {
            ApplyDesiredLicenseDelta(g_pInjectedPackage, desiredApps, desiredDepots);
        } else {
            void* cpi = AtomicLoadPtr(g_pCPackageInfo);
            if (cpi && oGetPackageInfo) {
                oGetPackageInfo(cpi, kInjectedPackageId, kInjectedPkgAccessToken);
            }
        }
    }

    spdlog::info("Hooks_Package: Prepared injected package payload with {} apps and {} depots", desiredApps.size(),
                 desiredDepots.size());

    g_licenseRefreshPending.store(true);
    TryProcessPendingLicenseRefresh();
}

HOOK_FUNC(CheckAppOwnership, bool, void* pObj, uint32_t appId, void* pOwn) {
    CaptureOnce(g_pCUser, pObj);

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
    UpdateInjectedPackages();
}

void Uninstall() {}
void SyncInjectedLicenses() {
    NotifyLicenseChanged();
}
} // namespace Hooks_Package
