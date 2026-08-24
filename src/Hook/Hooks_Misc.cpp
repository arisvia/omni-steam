#include "Hooks_Misc.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <spdlog/spdlog.h>
#include <string>

#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Process/ProcessInjector.h"

#include "Hook/HookMacros.h"

namespace fs = std::filesystem;

namespace {

std::atomic<AppId_t> g_OnlineFixRealAppId{0};

// PIDs already claimed by an injection attempt, so overlapping SpawnProcess
// events or duplicate watchers never double-inject the same process.
std::mutex g_attemptedMutex;
std::set<uint32_t> g_attemptedPids;

bool ClaimPid(uint32_t pid) {
    std::lock_guard<std::mutex> lock(g_attemptedMutex);
    return g_attemptedPids.insert(pid).second;
}

void ScheduleGameInjection(AppId_t appId, const char* exePath) {
    const bool hasAppModules = !LuaConfig::GetInjectModules(appId).empty();
    const bool hasGlobalModules = appId != 0 && !LuaConfig::GetInjectModules(0).empty();
    if (!hasAppModules && !hasGlobalModules)
        return;

    std::string exeName = exePath ? fs::path(exePath).filename().string() : "";
    if (exeName.empty()) {
        spdlog::warn("Hooks_Misc: addinject configured for AppID {} but SpawnProcess exe path is empty", appId);
        return;
    }

    auto baseline = OmniPlatform::Process::FindProcessIdsByName(exeName);

    OmniPlatform::Thread::StartDetached([appId, exeName, baseline]() {
        constexpr int kMaxPolls = 75; // ~15s at 200ms intervals
        OmniPlatform::Thread::Sleep(500);
        for (int i = 0; i < kMaxPolls; ++i) {
            for (uint32_t pid : OmniPlatform::Process::FindProcessIdsByName(exeName)) {
                if (std::find(baseline.begin(), baseline.end(), pid) != baseline.end())
                    continue;
                if (!ClaimPid(pid))
                    return;
                ProcessInjector::InjectForApp(appId, pid);
                return;
            }
            OmniPlatform::Thread::Sleep(200);
        }
        spdlog::warn("Hooks_Misc: Timed out waiting for game process '{}' (AppID {}) to appear for addinject", exeName,
                     appId);
    });
}

HOOK_FUNC(SpawnProcess, void*, void* pCUser, const char* pExePath, const char* pCommandLine, const char* pWorkingDir,
          CGameID* pGameID, void* a6, void* a7, void* a8, void* a9, void* a10, void* a11, void* a12, void* a13,
          void* a14, void* a15) {
    AppId_t realAppId = pGameID ? pGameID->AppID() : 0;

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

    ScheduleGameInjection(realAppId, pExePath);
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

namespace Hooks_Misc {

bool IsOnlineFixActive() {
    return g_OnlineFixRealAppId.load() != 0;
}

AppId_t GetOnlineFixRealAppId() {
    return g_OnlineFixRealAppId.load();
}

void SetOnlineFixRealAppId(AppId_t appId) {
    g_OnlineFixRealAppId.store(appId);
}

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
}

void Uninstall() {}

} // namespace Hooks_Misc
