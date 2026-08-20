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
bool g_licenseRefreshPending = false;

RESOLVE_FUNC(CUtlMemoryGrow, void*, CUtlVector<AppId_t>*, int);
RESOLVE_FUNC(MarkLicenseAsChanged, int64_t, void*, uint32_t, bool);
RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool, void*);
bool SyncInjectedLicenses(PackageInfo* pPkg, PackageId_t pkgId);
bool MarkLicenseAsChangedAndProcessUpdates();

CUtlVector<AppId_t>* FindAppIdVector(void* pPkg) {
    if (!pPkg)
        return nullptr;
    auto* base = reinterpret_cast<uint8_t*>(pPkg);

    // Check known candidate offsets for AppIdVec in 64-bit Steamclient PackageInfo
    for (size_t offset : {0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x10, 0x08}) {
        auto* vec = reinterpret_cast<CUtlVector<AppId_t>*>(base + offset);
        if (vec->m_Size >= 0 && vec->m_Size < 100000 && vec->m_Memory.m_nAllocationCount >= 0 &&
            vec->m_Memory.m_nAllocationCount < 100000 && vec->m_Size <= vec->m_Memory.m_nAllocationCount) {
            spdlog::info("Hooks_Package: Resolved AppIdVec at offset 0x{:X} (Size: {}, Capacity: {})", offset,
                         vec->m_Size, vec->m_Memory.m_nAllocationCount);
            return vec;
        }
        if (vec->m_Size == 0 && vec->m_Memory.m_nAllocationCount == 0 && vec->m_Memory.m_pMemory == nullptr) {
            spdlog::info("Hooks_Package: Resolved empty AppIdVec at offset 0x{:X}", offset);
            return vec;
        }
    }
    return nullptr;
}

HOOK_FUNC(GetPackageInfo, PackageInfo*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    if (!g_pCPackageInfo) {
        g_pCPackageInfo = pThis;
        spdlog::info("Hooks_Package: Captured CPackageInfo instance at {:p}", g_pCPackageInfo);
    }

    PackageInfo* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
    if (pPkg && packageId != 0) {
        if (g_activePackageId == 0) {
            g_activePackageId = packageId;
            g_pInjectedPackageInfo = pPkg;
            spdlog::info("Hooks_Package: Selected active Package {} for direct license injection", g_activePackageId);
        }
        SyncInjectedLicenses(pPkg, packageId);
    }
    return pPkg;
}

bool MarkLicenseAsChangedAndProcessUpdates() {
    if (!g_pCUser || !oMarkLicenseAsChanged || !oProcessPendingLicenseUpdates) {
        return false;
    }
    PackageId_t targetPkg = (g_activePackageId != 0) ? g_activePackageId : 21458;
    oMarkLicenseAsChanged(g_pCUser, targetPkg, true);
    oProcessPendingLicenseUpdates(g_pCUser);
    spdlog::info("Hooks_Package: Notified Steam of Package {} license update", targetPkg);
    return true;
}

bool SyncInjectedLicenses(PackageInfo* pPkg, PackageId_t pkgId) {
    if (!pPkg) {
        return false;
    }

    auto* pAppIdVec = FindAppIdVector(pPkg);
    if (!pAppIdVec) {
        spdlog::warn("Hooks_Package: Could not resolve AppIdVec offset in Package {}", pkgId);
        return false;
    }

    auto unlockedApps = LuaConfig::GetUnlockedApps();
    std::vector<uint32_t> newApps;

    std::set<uint32_t> existingInVec;
    if (pAppIdVec->m_Memory.m_pMemory && pAppIdVec->m_Size > 0) {
        for (int i = 0; i < pAppIdVec->m_Size; ++i) {
            existingInVec.insert(pAppIdVec->m_Memory.m_pMemory[i]);
        }
    }

    for (uint32_t appId : unlockedApps) {
        if (!existingInVec.contains(appId)) {
            newApps.push_back(appId);
        }
    }

    if (newApps.empty()) {
        return true;
    }

    int oldSize = pAppIdVec->m_Size;
    uint32_t numToAdd = static_cast<uint32_t>(newApps.size());
    PackageId_t targetId = (pkgId != 0) ? pkgId : g_activePackageId;
    spdlog::info("Hooks_Package: Injecting {} new apps/DLCs into Package {} (oldSize: {}, total now: {})", numToAdd,
                 targetId, oldSize, oldSize + numToAdd);

    if (oCUtlMemoryGrow) {
        oCUtlMemoryGrow(pAppIdVec, static_cast<int>(numToAdd));
    }

    if (pAppIdVec->m_Memory.m_pMemory) {
        uint32_t idx = 0;
        for (uint32_t appId : newApps) {
            pAppIdVec->m_Memory.m_pMemory[oldSize + idx] = appId;
            idx++;
        }
        pAppIdVec->m_Size = static_cast<int>(oldSize + numToAdd);
    }

    g_licenseRefreshPending = true;
    if (MarkLicenseAsChangedAndProcessUpdates()) {
        g_licenseRefreshPending = false;
    }
    return true;
}

bool TryInitFakeLicenseOnce() {
    if (g_pInjectedPackageInfo) {
        return SyncInjectedLicenses(g_pInjectedPackageInfo, g_activePackageId);
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
            pOwn->ReleaseState = EAppReleaseState::Released; // 4: Released / Launchable
            pOwn->ExistInPackageNums = 0;                    // 0: Direct Owner (not borrower/shared library)
            pOwn->bFreeLicense = false;
            pOwn->PackageId = (g_activePackageId != 0) ? g_activePackageId : 21458;
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
