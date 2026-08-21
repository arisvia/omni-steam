#include <cstdint>
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
PackageInfo* g_pInjectedPackageInfo = nullptr;
bool g_userNotified = false;
constexpr PackageId_t kInjectedPackageId = kSteamDefaultBasePackageId;
constexpr uint64_t kInjectedPkgAccessToken = kSteamDefaultBasePackageAccessToken;

static std::vector<AppId_t> g_injectedAppIds;
static std::vector<DepotId_t> g_injectedDepotIds;

RESOLVE_FUNC(MarkLicenseAsChanged, int64_t, void*, uint32_t, bool);
RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool, void*);

void InjectPackage0(PackageInfo* pPkg) {
    if (!pPkg)
        return;

    auto unlockedApps = LuaConfig::GetUnlockedApps();
    std::set<AppId_t> allApps;
    for (uint32_t id : unlockedApps) {
        allApps.insert(id);
    }
    g_injectedAppIds.assign(allApps.begin(), allApps.end());

    pPkg->AppIdVec.m_Memory.m_pMemory = g_injectedAppIds.data();
    pPkg->AppIdVec.m_Memory.m_nAllocationCount = static_cast<int>(g_injectedAppIds.size());
    pPkg->AppIdVec.m_Memory.m_nGrowSize = 0;
    pPkg->AppIdVec.m_Size = static_cast<int>(g_injectedAppIds.size());
    pPkg->AppIdVec.m_pElements = g_injectedAppIds.data();

    auto depotKeys = LuaConfig::GetDepotKeys();
    std::set<DepotId_t> allDepots;
    for (const auto& [depotId, _] : depotKeys) {
        allDepots.insert(depotId);
    }
    g_injectedDepotIds.assign(allDepots.begin(), allDepots.end());

    pPkg->DepotIdVec.m_Memory.m_pMemory = g_injectedDepotIds.data();
    pPkg->DepotIdVec.m_Memory.m_nAllocationCount = static_cast<int>(g_injectedDepotIds.size());
    pPkg->DepotIdVec.m_Memory.m_nGrowSize = 0;
    pPkg->DepotIdVec.m_Size = static_cast<int>(g_injectedDepotIds.size());
    pPkg->DepotIdVec.m_pElements = g_injectedDepotIds.data();

    spdlog::info("Hooks_Package: Successfully injected {} apps and {} depots into Package 0", g_injectedAppIds.size(),
                 g_injectedDepotIds.size());
}

void NotifyLicensesChanged() {
    if (g_pCUser && oMarkLicenseAsChanged && oProcessPendingLicenseUpdates && !g_userNotified) {
        oMarkLicenseAsChanged(g_pCUser, kInjectedPackageId, true);
        oProcessPendingLicenseUpdates(g_pCUser);
        g_userNotified = true;
        spdlog::info("Hooks_Package: Notified Steam of Package {} license update", kInjectedPackageId);
    }
}

HOOK_FUNC(GetPackageInfo, PackageInfo*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    if (!g_pCPackageInfo) {
        g_pCPackageInfo = pThis;
        spdlog::info("Hooks_Package: Captured CPackageInfo instance at {:p}", g_pCPackageInfo);
    }

    PackageInfo* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
    if (packageId == kInjectedPackageId && pPkg) {
        g_pInjectedPackageInfo = pPkg;
        InjectPackage0(pPkg);
    }
    return pPkg;
}

HOOK_FUNC(CheckAppOwnership, bool, void* pObj, uint32_t appId, AppOwnership* pOwn) {
    if (!g_pCUser) {
        g_pCUser = pObj;
        spdlog::info("Hooks_Package: Captured CUser instance at {:p}", g_pCUser);
        NotifyLicensesChanged();
    }

    if (!g_pInjectedPackageInfo && g_pCPackageInfo && oGetPackageInfo) {
        PackageInfo* pPkg = oGetPackageInfo(g_pCPackageInfo, kInjectedPackageId, kInjectedPkgAccessToken);
        if (pPkg) {
            g_pInjectedPackageInfo = pPkg;
            InjectPackage0(pPkg);
        }
    }

    bool result = oCheckAppOwnership ? oCheckAppOwnership(pObj, appId, pOwn) : false;

    if (LuaConfig::HasApp(appId) || LuaConfig::HasDepot(appId)) {
        if (pOwn) {
            pOwn->bOwnsLicense = true;
            pOwn->bFreeLicense = false;
            pOwn->ReleaseState = EAppReleaseState::Released;
            if (pOwn->ExistInPackageNums == 0) {
                pOwn->ExistInPackageNums = kSteamDefaultInjectedPackageCount;
                pOwn->PackageId = kInjectedPackageId;
            }
        }
        return true;
    }
    return result;
}
} // namespace
namespace Hooks_Package {
void Install() {
    // 1. Resolve license management function pointers
    uintptr_t fnMark = PatternLoader::GetFunctionAddress("MarkLicenseAsChanged");
    if (fnMark)
        oMarkLicenseAsChanged = reinterpret_cast<MarkLicenseAsChanged_t>(fnMark);

    uintptr_t fnProc = PatternLoader::GetFunctionAddress("ProcessPendingLicenseUpdates");
    if (fnProc)
        oProcessPendingLicenseUpdates = reinterpret_cast<ProcessPendingLicenseUpdates_t>(fnProc);

    // 2. Attach hooks
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
    if (g_pInjectedPackageInfo) {
        InjectPackage0(g_pInjectedPackageInfo);
    } else if (g_pCPackageInfo && oGetPackageInfo) {
        PackageInfo* pPkg = oGetPackageInfo(g_pCPackageInfo, kInjectedPackageId, kInjectedPkgAccessToken);
        if (pPkg) {
            g_pInjectedPackageInfo = pPkg;
            InjectPackage0(pPkg);
        }
    }
    g_userNotified = false;
    NotifyLicensesChanged();
}
} // namespace Hooks_Package
