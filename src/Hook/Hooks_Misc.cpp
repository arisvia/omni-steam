#include "Hooks_Misc.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <spdlog/spdlog.h>
#include <string>

#include "OmniPlatform/OmniPaths.h"
#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Process/ProcessInjector.h"

#include "Hook/HookMacros.h"
#include "Hook/Hooks_Package.h"

namespace fs = std::filesystem;

namespace {

std::atomic<AppId_t> g_OnlineFixRealAppId{0};
std::atomic<AppId_t> g_activeRunningAppId{0};
// PIDs already claimed by an injection attempt, so overlapping SpawnProcess
// events or duplicate watchers never double-inject the same process.
std::mutex g_attemptedMutex;
std::set<uint32_t> g_attemptedPids;

bool ClaimPid(uint32_t pid) {
    std::lock_guard<std::mutex> lock(g_attemptedMutex);
    return g_attemptedPids.insert(pid).second;
}

void MonitorGameLifecycle(AppId_t appId, const char* exePath) {
    if (appId == 0 || !exePath)
        return;

    std::string exeName = fs::path(exePath).filename().string();
    if (exeName.empty()) {
        return;
    }

    const bool hasAppModules = !LuaConfig::GetInjectModules(appId).empty();
    const bool hasGlobalModules = !LuaConfig::GetInjectModules(0).empty();
    const bool needsInject = hasAppModules || hasGlobalModules;

    auto baseline = OmniPlatform::Process::FindProcessIdsByName(exeName);

    OmniPlatform::Thread::StartDetached([appId, exeName, baseline, needsInject]() {
        constexpr int kMaxPolls = 75; // ~15s at 200ms intervals
        OmniPlatform::Thread::Sleep(500);
        uint32_t targetPid = 0;
        for (int i = 0; i < kMaxPolls; ++i) {
            for (uint32_t pid : OmniPlatform::Process::FindProcessIdsByName(exeName)) {
                if (std::find(baseline.begin(), baseline.end(), pid) != baseline.end())
                    continue;
                if (!ClaimPid(pid))
                    return;
                targetPid = pid;
                if (needsInject) {
                    Process::ProcessInjector::InjectForApp(appId, pid);
                }
                break;
            }
            if (targetPid != 0)
                break;
            OmniPlatform::Thread::Sleep(200);
        }

        if (targetPid == 0) {
            return;
        }

        // Monitor game process until termination
#if defined(OMNI_PLATFORM_WINDOWS)
        HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, targetPid);
        if (hProc) {
            WaitForSingleObject(hProc, INFINITE);
            CloseHandle(hProc);
        }
#else
        while (OmniPlatform::Process::IsProcessRunning(targetPid)) {
            OmniPlatform::Thread::Sleep(1000);
        }
#endif

        if (g_activeRunningAppId.load() == appId) {
            g_activeRunningAppId.store(0);
        }
        spdlog::info("Hooks_Misc: Game process '{}' (AppID {}, PID {}) terminated", exeName, appId, targetPid);

        // Dispatch silent background cloud save backup if manager executable is present
        std::string managerExe = OmniPlatform::Paths::GetManagerExecutablePath();
        if (!managerExe.empty()) {
            std::string cmd = "\"" + managerExe + "\" backup " + std::to_string(appId) + " --silent";
#if defined(OMNI_PLATFORM_WINDOWS)
            STARTUPINFOA si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS,
                               nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                spdlog::info("Hooks_Misc: Dispatched silent cloud save backup for AppID {}", appId);
            }
#else
            std::string bgCmd = cmd + " >/dev/null 2>&1 &";
            std::system(bgCmd.c_str());
            spdlog::info("Hooks_Misc: Dispatched silent cloud save backup for AppID {}", appId);
#endif
        }
    });
}

HOOK_FUNC(SpawnProcess, void*, void* pCUser, const char* pExePath, const char* pCommandLine, const char* pWorkingDir,
          CGameID* pGameID, void* a6, void* a7, void* a8, void* a9, void* a10, void* a11, void* a12, void* a13,
          void* a14, void* a15) {
    AppId_t realAppId = pGameID ? pGameID->AppID() : 0;
    if (realAppId != 0) {
        g_activeRunningAppId.store(realAppId);
    }
    if (pGameID && pCommandLine && std::strstr(pCommandLine, "-onlinefix")) {
        g_OnlineFixRealAppId.store(realAppId);
        pGameID->SetAppID(kOnlineFixAppId);
        spdlog::info("Hooks_Misc: SpawnProcess detected -onlinefix! Spoofing AppID {} -> {} (cmd: {})", realAppId,
                     kOnlineFixAppId, pCommandLine);
    } else {
        g_OnlineFixRealAppId.store(0);
    }

    void* result = oSpawnProcess ? oSpawnProcess(pCUser, pExePath, pCommandLine, pWorkingDir, pGameID, a6, a7, a8, a9,
                                                 a10, a11, a12, a13, a14, a15)
                                 : nullptr;
    MonitorGameLifecycle(realAppId, pExePath);
    return result;
}
HOOK_FUNC(OptedInMask, int64_t, void* pThis, AppId_t appId) {
    if (appId == kOnlineFixAppId) {
        AppId_t realAppId = g_OnlineFixRealAppId.load();
        if (realAppId != 0) {
            spdlog::debug("Hooks_Misc: OptedInMask rerouting AppID {} -> {} for native controller and overlay support",
                          appId, realAppId);
            appId = realAppId;
        }
    }
    return oOptedInMask ? oOptedInMask(pThis, appId) : 0;
}

} // namespace

namespace {

// CAppInfoCache::GetOrAddAppData - the missing library-sidebar piece.
// CClientAppManager_ProcessPendingLicenseUpdates blocks until EVERY Package 0
// AppIdVec entry has resolved appinfo. Steam's server denies PICS access
// tokens for client-side-only injected ids (it never saw our licenses), so
// their appinfo stays a placeholder and the license update never completes -
// the UI never receives ownership flags and unlocked games never appear.
// Marking those entries with bSkipFlag makes Steam treat them like PICS
// unknown_appids and lets the pipeline finish.
//
// NOTE: the trampoline is oGetOrAddAppDataHook (generated by HOOK_FUNC +
// ATTACH_HOOK below). Do NOT call a RESOLVE_FUNC-declared pointer here - it
// stays null and turns every appinfo lookup into a nullptr return (hard
// client exit).
HOOK_FUNC(GetOrAddAppDataHook, CAppData*, void* pCache, AppId_t appId, bool bCreate) {
    CAppData* pData = oGetOrAddAppDataHook ? oGetOrAddAppDataHook(pCache, appId, bCreate) : nullptr;
    // Order matters: the unresolved check is lock-free; the injected-set
    // lookup takes a mutex and must stay on the rare path.
    if (pData && !bCreate && pData->IsUnresolvedAppInfo() && Hooks_Package::IsInjectedAppId(appId)) {
        spdlog::debug("Hooks_Misc: GetOrAddAppData app {} unresolved -> setting skip flag (license unlock path)",
                      appId);
        pData->bSkipFlag = true;
    }
    return pData;
}

} // namespace

namespace Hooks_Misc {

void Install() {
    uintptr_t fnSpawn = PatternLoader::GetFunctionAddress("SpawnProcess");
    if (fnSpawn) {
        ATTACH_HOOK(fnSpawn, SpawnProcess);
        spdlog::info("Hooks_Misc: Successfully installed SpawnProcess hook at {:p}", reinterpret_cast<void*>(fnSpawn));
    } else {
        spdlog::warn("Hooks_Misc: SpawnProcess signature not resolved");
    }

    uintptr_t fnOptedIn = PatternLoader::GetFunctionAddress("OptedInMask");
    if (fnOptedIn) {
        ATTACH_HOOK(fnOptedIn, OptedInMask);
        spdlog::info("Hooks_Misc: Successfully installed OptedInMask hook at {:p}", reinterpret_cast<void*>(fnOptedIn));
    } else {
        spdlog::warn("Hooks_Misc: OptedInMask signature not resolved");
    }

    uintptr_t fnAppData = PatternLoader::GetFunctionAddress("GetOrAddAppData");
    if (fnAppData) {
        ATTACH_HOOK(fnAppData, GetOrAddAppDataHook);
        spdlog::info("Hooks_Misc: Successfully installed GetOrAddAppData hook at {:p}",
                     reinterpret_cast<void*>(fnAppData));
    } else {
        spdlog::warn("Hooks_Misc: GetOrAddAppData signature not resolved");
    }
}

void Uninstall() {
    uintptr_t fnSpawn = PatternLoader::GetFunctionAddress("SpawnProcess");
    if (fnSpawn) {
        DETACH_HOOK(fnSpawn, SpawnProcess);
    }
    uintptr_t fnOptedIn = PatternLoader::GetFunctionAddress("OptedInMask");
    if (fnOptedIn) {
        DETACH_HOOK(fnOptedIn, OptedInMask);
    }
    uintptr_t fnAppData = PatternLoader::GetFunctionAddress("GetOrAddAppData");
    if (fnAppData) {
        DETACH_HOOK(fnAppData, GetOrAddAppDataHook);
    }
}

AppId_t GetActiveAppId() {
    return g_activeRunningAppId.load();
}

} // namespace Hooks_Misc
