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
bool g_userNotified = false;
constexpr PackageId_t kInjectedPackageId = kSteamDefaultBasePackageId;

static std::vector<AppId_t> g_injectedAppIds;
static std::vector<DepotId_t> g_injectedDepotIds;
static alignas(16) uint8_t g_fakePackage0[512] = {0};

RESOLVE_FUNC(MarkLicenseAsChanged, int64_t, void*, uint32_t, bool);
RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool, void*);

void UpdateFakePackage0() {
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

    std::memset(g_fakePackage0, 0, sizeof(g_fakePackage0));
    *reinterpret_cast<uint32_t*>(g_fakePackage0 + 0x00) = kInjectedPackageId;
    *reinterpret_cast<uint32_t*>(g_fakePackage0 + 0x18) = 0; // EPackageStatus::Available = 0

    if constexpr (sizeof(void*) == 8) {
        // 64-bit ABI (Windows x64, Linux x86_64, macOS arm64/x86_64)
        *reinterpret_cast<uintptr_t*>(g_fakePackage0 + 0x40) =
            reinterpret_cast<uintptr_t>(g_injectedAppIds.data()); // m_pElements
        *reinterpret_cast<int32_t*>(g_fakePackage0 + 0x50) = static_cast<int32_t>(g_injectedAppIds.size()); // m_Size

        *reinterpret_cast<uintptr_t*>(g_fakePackage0 + 0x60) =
            reinterpret_cast<uintptr_t>(g_injectedDepotIds.data()); // m_pElements
        *reinterpret_cast<int32_t*>(g_fakePackage0 + 0x70) = static_cast<int32_t>(g_injectedDepotIds.size()); // m_Size
    } else {
        // 32-bit ABI (Linux i386 / ubuntu12_32)
        *reinterpret_cast<uint32_t*>(g_fakePackage0 + 0x04) = 0; // Status
        *reinterpret_cast<uint32_t*>(g_fakePackage0 + 0x20) =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_injectedAppIds.data())); // m_pElements
        *reinterpret_cast<int32_t*>(g_fakePackage0 + 0x1C) = static_cast<int32_t>(g_injectedAppIds.size()); // m_Size

        *reinterpret_cast<uint32_t*>(g_fakePackage0 + 0x34) =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_injectedDepotIds.data())); // m_pElements
        *reinterpret_cast<int32_t*>(g_fakePackage0 + 0x30) = static_cast<int32_t>(g_injectedDepotIds.size()); // m_Size
    }

    spdlog::info("Hooks_Package: Synthesized Package 0 with {} apps and {} depots", g_injectedAppIds.size(),
                 g_injectedDepotIds.size());
}

void NotifyLicensesChanged() {
    if (g_pCUser && oMarkLicenseAsChanged && oProcessPendingLicenseUpdates && !g_userNotified) {
        UpdateFakePackage0();
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

    if (packageId == kInjectedPackageId) {
        if (g_injectedAppIds.empty()) {
            UpdateFakePackage0();
        }
        return reinterpret_cast<PackageInfo*>(g_fakePackage0);
    }

    return oGetPackageInfo ? oGetPackageInfo(pThis, packageId, accessToken) : nullptr;
}
HOOK_FUNC(CheckAppOwnership, bool, void* pObj, uint32_t appId, void* pOwn) {
    if (!g_pCUser) {
        g_pCUser = pObj;
        spdlog::info("Hooks_Package: Captured CUser instance at {:p}", g_pCUser);
        NotifyLicensesChanged();
    }

    bool result = oCheckAppOwnership ? oCheckAppOwnership(pObj, appId, pOwn) : false;

    if (LuaConfig::HasApp(appId) || LuaConfig::HasDepot(appId)) {
        if (pOwn) {
            uint8_t* raw = reinterpret_cast<uint8_t*>(pOwn);
            if constexpr (sizeof(void*) == 8) {
                // 64-bit SteamClient verified memory offsets (Windows x64, Linux x86_64, macOS)
                *reinterpret_cast<uint32_t*>(raw + 0x00) = kInjectedPackageId; // PackageId = 0
                *reinterpret_cast<uint32_t*>(raw + 0x1C) =
                    static_cast<uint32_t>(EAppReleaseState::Released); // ReleaseState = Released (4)
                *reinterpret_cast<uint32_t*>(raw + 0x20) = 1;          // ExistInPackageNums = 1
                raw[0x28] = 1;                                         // bOwnsLicense = true
                raw[0x30] = 1;                                         // bIsSubscribed = true
                raw[0x33] = 1;
                raw[0x34] = 1;
            } else {
                // 32-bit SteamClient verified memory offsets (Linux i386)
                *reinterpret_cast<uint32_t*>(raw + 0x00) = kInjectedPackageId;
                *reinterpret_cast<uint32_t*>(raw + 0x04) = static_cast<uint32_t>(EAppReleaseState::Released);
                *reinterpret_cast<uint32_t*>(raw + 0x08) = 1;
                raw[0x0C] = 1; // bOwnsLicense
                raw[0x0D] = 0; // bFreeLicense
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
    UpdateFakePackage0();
    g_userNotified = false;
    NotifyLicensesChanged();
}
} // namespace Hooks_Package
