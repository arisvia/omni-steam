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
bool g_userNotified = false;
constexpr PackageId_t kInjectedPackageId = kSteamDefaultBasePackageId;
constexpr uint64_t kInjectedPkgAccessToken = kSteamDefaultBasePackageAccessToken;

RESOLVE_FUNC(CUtlMemoryGrow, void*, CUtlVector<AppId_t>*, int);
RESOLVE_FUNC(MarkLicenseAsChanged, int64_t, void*, uint32_t, bool);
RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool, void*);

bool InitFakeLicenseOnce(PackageInfo* pPkg);
bool TryInitFakeLicenseOnce();
bool MarkLicenseAsChangedAndProcessUpdates();

HOOK_FUNC(GetPackageInfo, PackageInfo*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    if (!g_pCPackageInfo) {
        g_pCPackageInfo = pThis;
        spdlog::info("Hooks_Package: Captured CPackageInfo instance at {:p}", g_pCPackageInfo);
    }

    PackageInfo* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
    if (packageId == kInjectedPackageId && pPkg) {
        g_pInjectedPackageInfo = pPkg;
        if (!g_licenseInitialized) {
            InitFakeLicenseOnce(pPkg);
        }
    }
    return pPkg;
}

CUtlVector<AppId_t>* FindAppIdVector(PackageInfo* pPkg) {
    if (!pPkg)
        return nullptr;
    return &pPkg->AppIdVec;
}

CUtlVector<DepotId_t>* FindDepotIdVector(PackageInfo* pPkg) {
    if (!pPkg)
        return nullptr;
    return &pPkg->DepotIdVec;
}

bool MarkLicenseAsChangedAndProcessUpdates() {
    if (!g_pCUser || !oMarkLicenseAsChanged || !oProcessPendingLicenseUpdates) {
        return false;
    }
    oMarkLicenseAsChanged(g_pCUser, kInjectedPackageId, true);
    oProcessPendingLicenseUpdates(g_pCUser);
    g_userNotified = true;
    spdlog::info("Hooks_Package: Notified Steam of Package {} license update", kInjectedPackageId);
    return true;
}

bool InitFakeLicenseOnce(PackageInfo* pPkg) {
    if (!pPkg)
        return false;

    auto* pAppIdVec = FindAppIdVector(pPkg);
    if (!pAppIdVec) {
        spdlog::warn("Hooks_Package: Could not resolve AppIdVec offset in Package 0");
        return false;
    }

    auto unlockedApps = LuaConfig::GetUnlockedApps();
    std::set<uint32_t> existingApps;
    if (pAppIdVec->m_Memory.m_pMemory && pAppIdVec->m_Size > 0) {
        for (int i = 0; i < pAppIdVec->m_Size; ++i) {
            existingApps.insert(pAppIdVec->m_Memory.m_pMemory[i]);
        }
    }

    std::vector<uint32_t> appsToAdd;
    for (uint32_t id : unlockedApps) {
        if (existingApps.find(id) == existingApps.end()) {
            appsToAdd.push_back(id);
        }
    }

    if (!appsToAdd.empty()) {
        int oldSize = pAppIdVec->m_Size;
        uint32_t numToAdd = static_cast<uint32_t>(appsToAdd.size());
        spdlog::info("Hooks_Package: Injecting {} apps/DLCs into Package 0 AppIdVec (oldSize: {}, total: {})", numToAdd,
                     oldSize, oldSize + numToAdd);

        if (oCUtlMemoryGrow) {
            oCUtlMemoryGrow(pAppIdVec, static_cast<int>(numToAdd));
        }
        if (pAppIdVec->m_Memory.m_pMemory) {
            for (size_t i = 0; i < numToAdd; ++i) {
                pAppIdVec->m_Memory.m_pMemory[oldSize + i] = appsToAdd[i];
            }
            pAppIdVec->m_Size = static_cast<int>(oldSize + numToAdd);
            pAppIdVec->m_pElements = pAppIdVec->m_Memory.m_pMemory;
        }
    }

    // Also inject Depot IDs into DepotIdVec
    auto* pDepotIdVec = FindDepotIdVector(pPkg);
    if (pDepotIdVec) {
        std::set<uint32_t> existingDepots;
        if (pDepotIdVec->m_Memory.m_pMemory && pDepotIdVec->m_Size > 0) {
            for (int i = 0; i < pDepotIdVec->m_Size; ++i) {
                existingDepots.insert(pDepotIdVec->m_Memory.m_pMemory[i]);
            }
        }
        auto depotKeys = LuaConfig::GetDepotKeys();
        std::vector<uint32_t> depotsToAdd;
        for (const auto& [depotId, _] : depotKeys) {
            if (existingDepots.find(depotId) == existingDepots.end()) {
                depotsToAdd.push_back(depotId);
            }
        }
        if (!depotsToAdd.empty()) {
            int oldDepotSize = pDepotIdVec->m_Size;
            uint32_t numDepots = static_cast<uint32_t>(depotsToAdd.size());
            spdlog::info("Hooks_Package: Injecting {} depot IDs into Package 0 DepotIdVec (oldSize: {}, total: {})",
                         numDepots, oldDepotSize, oldDepotSize + numDepots);
            if (oCUtlMemoryGrow) {
                oCUtlMemoryGrow(pDepotIdVec, static_cast<int>(numDepots));
            }
            if (pDepotIdVec->m_Memory.m_pMemory) {
                for (size_t i = 0; i < numDepots; ++i) {
                    pDepotIdVec->m_Memory.m_pMemory[oldDepotSize + i] = depotsToAdd[i];
                }
                pDepotIdVec->m_Size = static_cast<int>(oldDepotSize + numDepots);
                pDepotIdVec->m_pElements = pDepotIdVec->m_Memory.m_pMemory;
            }
        }
    }

    g_licenseInitialized = true;
    MarkLicenseAsChangedAndProcessUpdates();
    return true;
}
bool TryInitFakeLicenseOnce() {
    if (g_licenseInitialized && g_userNotified)
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
        if (g_licenseInitialized && !g_userNotified) {
            MarkLicenseAsChangedAndProcessUpdates();
        }
    }

    TryInitFakeLicenseOnce();
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

void SyncInjectedLicenses() {
    if (g_pInjectedPackageInfo) {
        InitFakeLicenseOnce(g_pInjectedPackageInfo);
    } else {
        TryInitFakeLicenseOnce();
    }
}

} // namespace Hooks_Package
