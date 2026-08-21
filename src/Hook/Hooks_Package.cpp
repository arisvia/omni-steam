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
bool g_licenseInitialized = false;

constexpr PackageId_t kInjectedPackageId = kSteamDefaultBasePackageId;
constexpr uint64_t kInjectedPkgAccessToken = kSteamDefaultBasePackageAccessToken;

RESOLVE_FUNC(CUtlMemoryGrow, void*, CUtlVector<AppId_t>*, int);
RESOLVE_FUNC(MarkLicenseAsChanged, int64_t, void*, uint32_t, bool);
RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool, void*);

HOOK_FUNC(GetPackageInfo, PackageInfo*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    if (!g_pCPackageInfo) {
        g_pCPackageInfo = pThis;
        spdlog::info("Hooks_Package: Captured CPackageInfo instance at {:p}", g_pCPackageInfo);
    }

    PackageInfo* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
    return pPkg;
}

CUtlVector<AppId_t>* FindAppIdVector(void* pPkg) {
    if (!pPkg)
        return nullptr;
    auto* base = reinterpret_cast<uint8_t*>(pPkg);

    // Check known candidate offsets for AppIdVec in 64-bit Steamclient PackageInfo
    for (size_t offset : {0x40, 0x20, 0x18, 0x38, 0x28, 0x30, 0x10, 0x08}) {
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
    if (!g_pCUser || !oMarkLicenseAsChanged || !oProcessPendingLicenseUpdates) {
        return false;
    }
    oMarkLicenseAsChanged(g_pCUser, kInjectedPackageId, true);
    oProcessPendingLicenseUpdates(g_pCUser);
    spdlog::info("Hooks_Package: Notified Steam of Package {} license update", kInjectedPackageId);
    return true;
}

bool InitFakeLicenseOnce(PackageInfo* pPkg) {
    if (!pPkg)
        return false;

    if (pPkg->Status != EPackageStatus::Available) {
        spdlog::warn("Hooks_Package: Package 0 status is not Available ({}), skipping injection",
                     static_cast<int>(pPkg->Status));
        return false;
    }

    auto* pAppIdVec = FindAppIdVector(pPkg);
    if (!pAppIdVec) {
        spdlog::warn("Hooks_Package: Could not resolve AppIdVec offset in Package 0");
        return false;
    }

    auto unlockedApps = LuaConfig::GetUnlockedApps();
    std::vector<uint32_t> appIds(unlockedApps.begin(), unlockedApps.end());
    if (!appIds.empty()) {
        int oldSize = pAppIdVec->m_Size;
        uint32_t numToAdd = static_cast<uint32_t>(appIds.size());
        spdlog::info("Hooks_Package: Injecting {} apps/DLCs into Package 0 (oldSize: {}, total: {})", numToAdd, oldSize,
                     oldSize + numToAdd);

        if (oCUtlMemoryGrow) {
            oCUtlMemoryGrow(pAppIdVec, static_cast<int>(numToAdd));
        }

        if (pAppIdVec->m_Memory.m_pMemory) {
            for (size_t i = 0; i < numToAdd; ++i) {
                pAppIdVec->m_Memory.m_pMemory[oldSize + i] = appIds[i];
            }
            pAppIdVec->m_Size = static_cast<int>(oldSize + numToAdd);
        }
    }

    g_licenseInitialized = true;
    MarkLicenseAsChangedAndProcessUpdates();
    return true;
}

bool TryInitFakeLicenseOnce() {
    if (g_licenseInitialized)
        return true;

    if (g_pCPackageInfo && oGetPackageInfo) {
        PackageInfo* pPkg = oGetPackageInfo(g_pCPackageInfo, kInjectedPackageId, kInjectedPkgAccessToken);
        if (!pPkg) {
            spdlog::warn("Hooks_Package: GetPackageInfo returned null for Package 0");
            return false;
        }
        g_pInjectedPackageInfo = pPkg;
        return InitFakeLicenseOnce(pPkg);
    }
    return false;
}

HOOK_FUNC(CheckAppOwnership, bool, void* pObj, uint32_t appId, AppOwnership* pOwn) {
    if (!g_pCUser) {
        g_pCUser = pObj;
        spdlog::info("Hooks_Package: Captured CUser instance at {:p}", g_pCUser);
    }

    bool result = oCheckAppOwnership ? oCheckAppOwnership(pObj, appId, pOwn) : false;
    TryInitFakeLicenseOnce();

    if (LuaConfig::HasApp(appId) || LuaConfig::HasDepot(appId)) {
        if (pOwn) {
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
