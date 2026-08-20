#include <cstdint>
#include <set>
#include <spdlog/spdlog.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookMacros.h"

namespace {

void* g_pCUser = nullptr;
void* g_pCPackageInfo = nullptr;
PackageInfo* g_pInjectedPackageInfo = nullptr;
PackageId_t g_activePackageId = 0;
std::set<uint32_t> g_injectedAppIds;
bool g_licenseRefreshPending = false;

RESOLVE_FUNC(CUtlMemoryGrow, void*, CUtlVector<AppId_t>*, int);
RESOLVE_FUNC(MarkLicenseAsChanged, int64_t, void*, uint32_t, bool);
RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool, void*);
bool SyncInjectedLicenses(PackageInfo* pPkg);
bool MarkLicenseAsChangedAndProcessUpdates();

HOOK_FUNC(GetPackageInfo, PackageInfo*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    if (!g_pCPackageInfo) {
        g_pCPackageInfo = pThis;
        spdlog::info("Hooks_Package: Captured CPackageInfo instance at {:p}", g_pCPackageInfo);
    }

    PackageInfo* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
    if (pPkg) {
        if (!g_pInjectedPackageInfo) {
            g_pInjectedPackageInfo = pPkg;
            g_activePackageId = packageId;
            spdlog::info("Hooks_Package: Selected active Package {} for direct license injection", g_activePackageId);
            SyncInjectedLicenses(pPkg);
        } else if (pPkg == g_pInjectedPackageInfo) {
            SyncInjectedLicenses(pPkg);
        }
    }
    return pPkg;
}

bool MarkLicenseAsChangedAndProcessUpdates() {
    if (!g_pCUser || !oMarkLicenseAsChanged || !oProcessPendingLicenseUpdates) {
        return false;
    }
    PackageId_t targetPkg = (g_activePackageId != 0) ? g_activePackageId : 0;
    oMarkLicenseAsChanged(g_pCUser, targetPkg, true);
    oProcessPendingLicenseUpdates(g_pCUser);
    spdlog::info("Hooks_Package: Notified Steam of Package {} license update", targetPkg);
    return true;
}

bool SyncInjectedLicenses(PackageInfo* pPkg) {
    if (!pPkg) {
        return false;
    }

    auto unlockedApps = LuaConfig::GetUnlockedApps();
    std::vector<uint32_t> newApps;
    for (uint32_t appId : unlockedApps) {
        if (!g_injectedAppIds.contains(appId)) {
            newApps.push_back(appId);
        }
    }

    if (newApps.empty()) {
        return true;
    }

    int oldSize = pPkg->AppIdVec.m_Size;
    if (oldSize < 0 || oldSize > 500000) {
        spdlog::warn("Hooks_Package: Invalid Package {} AppIdVec size ({}), skipping direct memory modification",
                     g_activePackageId, oldSize);
        return false;
    }

    uint32_t numToAdd = static_cast<uint32_t>(newApps.size());
    spdlog::info("Hooks_Package: Injecting {} new apps into Package {} (total now: {})", numToAdd, g_activePackageId,
                 oldSize + numToAdd);

    if (oCUtlMemoryGrow) {
        oCUtlMemoryGrow(&pPkg->AppIdVec, static_cast<int>(numToAdd));
    }

    if (pPkg->AppIdVec.m_Memory.m_pMemory) {
        uint32_t idx = 0;
        for (uint32_t appId : newApps) {
            pPkg->AppIdVec.m_Memory.m_pMemory[oldSize + idx] = appId;
            g_injectedAppIds.insert(appId);
            idx++;
        }
        pPkg->AppIdVec.m_Size = static_cast<int>(oldSize + numToAdd);
    }

    g_licenseRefreshPending = true;
    if (MarkLicenseAsChangedAndProcessUpdates()) {
        g_licenseRefreshPending = false;
    }
    return true;
}

bool TryInitFakeLicenseOnce() {
    if (g_pInjectedPackageInfo) {
        return SyncInjectedLicenses(g_pInjectedPackageInfo);
    }
    return false;
}

HOOK_FUNC(CheckAppOwnership, bool, void* pObj, uint32_t appId, AppOwnership* pOwn) {
    if (!g_pCUser) {
        g_pCUser = pObj;
        spdlog::info("Hooks_Package: Captured CUser instance at {:p}", g_pCUser);
        if (g_licenseRefreshPending) {
            if (MarkLicenseAsChangedAndProcessUpdates()) {
                g_licenseRefreshPending = false;
            }
        }
    }

    bool result = oCheckAppOwnership ? oCheckAppOwnership(pObj, appId, pOwn) : false;
    TryInitFakeLicenseOnce();
    if (LuaConfig::HasApp(appId) || LuaConfig::HasDepot(appId)) {
        if (pOwn) {
            pOwn->bOwnsLicense = true;
            pOwn->ReleaseState = EAppReleaseState::Released;
            pOwn->ExistInPackageNums = 1;
            pOwn->bFreeLicense = false;
            if (g_activePackageId != 0) {
                pOwn->PackageId = g_activePackageId;
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
    uintptr_t fnGrow = PatternLoader::GetFunctionAddress("CUtlMemoryGrow");
    if (fnGrow)
        oCUtlMemoryGrow = reinterpret_cast<CUtlMemoryGrow_t>(fnGrow);

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

} // namespace Hooks_Package
