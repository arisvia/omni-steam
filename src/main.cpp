#include "OmniPlatform/OmniPlatform.h"
#include "Hook/HookManager.h"
#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include <spdlog/spdlog.h>

namespace Log { void Init(); }

static void InitializeOmniSteam() {
    Log::Init();
    spdlog::info("OmniSteam cross-platform core initializing...");

    std::string exePath = OmniPlatform::Process::GetExecutablePath();
    spdlog::info("Attached Process: {} (PID: {})", exePath, OmniPlatform::Process::GetCurrentProcessId());

    OmniPlatform::Thread::StartDetached([]() {
        OmniPlatform::Thread::Sleep(500);

        std::string homeDir = std::getenv("HOME") ? std::getenv("HOME") : "";
        std::string configPath = homeDir.empty() ? "omnisteam.toml" : homeDir + "/.config/omnisteam/omnisteam.toml";
        std::string luaDir = homeDir.empty() ? "lua" : homeDir + "/.config/omnisteam/lua";

        Config::Load(configPath);
        LuaConfig::ParseDirectory(luaDir);

        std::vector<std::string> watchDirs = { luaDir };
        for (const auto& p : Config::GetLuaPaths()) watchDirs.push_back(p);

        OmniPlatform::DirectoryWatch::StartWatch(watchDirs, [](const std::string& path, bool isDir) {
            if (!isDir && path.ends_with(".lua")) {
                spdlog::info("Hot reload Lua: {}", path);
                LuaConfig::ParseFile(path);
            }
        });

        HookManager::InstallHooks();
    });
}

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitializeOmniSteam();
    } else if (dwReason == DLL_PROCESS_DETACH) {
        HookManager::UninstallHooks();
    }
    return TRUE;
}
#else
__attribute__((constructor)) static void LibraryInit() {
    InitializeOmniSteam();
}

__attribute__((destructor)) static void LibraryFini() {
    HookManager::UninstallHooks();
    OmniPlatform::DirectoryWatch::StopWatch();
}
#endif
