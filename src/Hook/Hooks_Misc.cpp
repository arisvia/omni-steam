#include "Hooks_Misc.h"

#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>
#include <string>

#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookMacros.h"

namespace {

AppId_t g_OnlineFixRealAppId = 0;

HOOK_FUNC(SpawnProcess, void*, void* pCUser, const char* pExePath, const char* pCommandLine, const char* pWorkingDir,
          CGameID* pGameID, void* a6, void* a7, void* a8, void* a9, void* a10, void* a11, void* a12, void* a13,
          void* a14, void* a15) {
    if (pGameID && pCommandLine && std::strstr(pCommandLine, "-onlinefix")) {
        AppId_t originalAppId = pGameID->AppID();
        g_OnlineFixRealAppId = originalAppId;
        pGameID->SetAppID(kOnlineFixAppId);
        spdlog::info("Hooks_Misc: SpawnProcess detected -onlinefix! Spoofing AppID {} -> {} (cmd: {})", originalAppId,
                     kOnlineFixAppId, pCommandLine);
    } else {
        g_OnlineFixRealAppId = 0;
    }

    return oSpawnProcess ? oSpawnProcess(pCUser, pExePath, pCommandLine, pWorkingDir, pGameID, a6, a7, a8, a9, a10, a11,
                                         a12, a13, a14, a15)
                         : nullptr;
}

HOOK_FUNC(OptedInMask, int64_t, void* pThis, AppId_t appId) {
    if (appId == kOnlineFixAppId && g_OnlineFixRealAppId != 0) {
        spdlog::debug("Hooks_Misc: OptedInMask rerouting AppID {} -> {} for native controller and overlay support",
                      appId, g_OnlineFixRealAppId);
        appId = g_OnlineFixRealAppId;
    }
    return oOptedInMask ? oOptedInMask(pThis, appId) : 0;
}

} // namespace

namespace Hooks_Misc {

bool IsOnlineFixActive() {
    return g_OnlineFixRealAppId != 0;
}

AppId_t GetOnlineFixRealAppId() {
    return g_OnlineFixRealAppId;
}

void SetOnlineFixRealAppId(AppId_t appId) {
    g_OnlineFixRealAppId = appId;
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
