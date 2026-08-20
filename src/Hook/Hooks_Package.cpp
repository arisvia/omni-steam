#include <cstdint>
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
bool g_licenseInitialized = false;
bool g_licenseRefreshPending = false;

constexpr PackageId_t kInjectedPackageId = 0;
constexpr uint64_t kInjectedPkgAccessToken = 10660652434190618804ull;

RESOLVE_FUNC(CUtlMemoryGrow, void*, CUtlVector<AppId_t>*, int);
RESOLVE_FUNC(MarkLicenseAsChanged, int64_t, void*, uint32_t, bool);
RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool, void*);
bool InitFakeLicenseOnce(PackageInfo* pPkg);
bool MarkLicenseAsChangedAndProcessUpdates();

HOOK_FUNC(GetPackageInfo, PackageInfo*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    if (!g_pCPackageInfo) {
        g_pCPackageInfo = pThis;
        spdlog::info("Hooks_Package: Captured CPackageInfo instance at {:p}", g_pCPackageInfo);
    }

    PackageInfo* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
    if (pPkg && packageId == kInjectedPackageId) {
        if (!g_pInjectedPackageInfo) {
            g_pInjectedPackageInfo = pPkg;
        }
        InitFakeLicenseOnce(pPkg);
    }
    return pPkg;
}

bool MarkLicenseAsChangedAndProcessUpdates() {
    if (!g_pCUser || !oMarkLicenseAsChanged || !oProcessPendingLicenseUpdates) {
        return false;
    }
    oMarkLicenseAsChanged(g_pCUser, kInjectedPackageId, true);
    oProcessPendingLicenseUpdates(g_pCUser);
    spdlog::info("Hooks_Package: Notified Steam of Package {} license update", kInjectedPackageId);
    return true;
}

bool InitFakeLicenseOnce(PackageInfo* pPkg) {
    if (g_licenseInitialized || !pPkg) {
        return true;
    }

    auto unlockedApps = LuaConfig::GetUnlockedApps();
    if (!unlockedApps.empty()) {
        uint32_t oldSize = pPkg->AppIdVec.m_Size;
        uint32_t numToAdd = static_cast<uint32_t>(unlockedApps.size());
        spdlog::info("Hooks_Package: Injecting {} apps into Package {} (oldSize: {})", numToAdd, kInjectedPackageId,
                     oldSize);

        if (oCUtlMemoryGrow) {
            oCUtlMemoryGrow(&pPkg->AppIdVec, static_cast<int>(numToAdd));
        }

        if (pPkg->AppIdVec.m_Memory.m_pMemory) {
            uint32_t idx = 0;
            for (uint32_t appId : unlockedApps) {
                pPkg->AppIdVec.m_Memory.m_pMemory[oldSize + idx] = appId;
                idx++;
            }
            pPkg->AppIdVec.m_Size = oldSize + numToAdd;
        }
    }

    g_licenseInitialized = true;
    g_licenseRefreshPending = true;

    if (MarkLicenseAsChangedAndProcessUpdates()) {
        g_licenseRefreshPending = false;
    }
    return true;
}

bool TryInitFakeLicenseOnce() {
    if (g_licenseInitialized)
        return true;

    if (g_pInjectedPackageInfo) {
        return InitFakeLicenseOnce(g_pInjectedPackageInfo);
    }

    if (g_pCPackageInfo && oGetPackageInfo) {
        PackageInfo* pPkg = oGetPackageInfo(g_pCPackageInfo, kInjectedPackageId, kInjectedPkgAccessToken);
        if (pPkg) {
            g_pInjectedPackageInfo = pPkg;
            return InitFakeLicenseOnce(pPkg);
        }
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
            pOwn->PackageId = kInjectedPackageId;
            pOwn->ReleaseState = EAppReleaseState::Released;
            pOwn->bOwnsLicense = true;
            pOwn->bFreeLicense = false;
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
