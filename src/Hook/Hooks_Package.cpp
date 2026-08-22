#include <cstdint>
#include <cstring>
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
void* g_pInjectedPackage = nullptr;
constexpr PackageId_t kInjectedPackageId = kSteamDefaultBasePackageId;
constexpr uint64_t kInjectedPkgAccessToken = kSteamDefaultBasePackageAccessToken;

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
    spdlog::info("Hooks_Package: Injected {} total apps/DLCs into Package 0 memory structure",
                 g_staticAppBuffer.size());
    return true;
}

HOOK_FUNC(GetPackageInfo, void*, void* pThis, uint32_t packageId, uint64_t accessToken) {
    if (!g_pCPackageInfo) {
        g_pCPackageInfo = pThis;
        spdlog::info("Hooks_Package: Captured CPackageInfo instance at {:p}", g_pCPackageInfo);
    }

    void* pPkg = oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
    if (packageId == kInjectedPackageId && pPkg) {
        InjectPackage0Apps(pPkg);
    }
    return pPkg;
}

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

    // If Package 0 is already captured, trigger hot re-injection
    if (g_pInjectedPackage) {
        InjectPackage0Apps(g_pInjectedPackage);
    } else if (g_pCPackageInfo && oGetPackageInfo) {
        oGetPackageInfo(g_pCPackageInfo, kInjectedPackageId, kInjectedPkgAccessToken);
    }
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

    bool originalResult = oCheckAppOwnership ? oCheckAppOwnership(pObj, appId, pOwn) : false;
    if (originalResult) {
        // App is natively owned on Steam account; preserve legitimate package ID and license state
        return true;
    }

    if (LuaConfig::HasApp(appId) || LuaConfig::HasDepot(appId)) {
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
    uintptr_t fnGetPkg = PatternLoader::GetFunctionAddress("GetPackageInfo");
    if (fnGetPkg) {
        ATTACH_HOOK(fnGetPkg, GetPackageInfo);
        spdlog::info("Hooks_Package: Successfully installed GetPackageInfo hook at {:p}",
                     reinterpret_cast<void*>(fnGetPkg));
    } else {
        spdlog::warn("Hooks_Package: GetPackageInfo signature not resolved");
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
    NotifyLicensesChanged();
}
} // namespace Hooks_Package
