#include <filesystem>
#include <spdlog/spdlog.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookManager.h"

namespace fs = std::filesystem;

namespace Log {
void Init();
}

namespace {
std::vector<std::string> GetCandidateLuaDirectories() {
    std::vector<std::string> dirs;

    // 1. Steam standard subdirectories (relative to steam.exe working directory)
    dirs.push_back("config/lua");
    dirs.push_back("lua");

    // 2. User profile / Home directory paths
    const char* userProfile = std::getenv("USERPROFILE");
    const char* home = std::getenv("HOME");

#if defined(OMNI_PLATFORM_WINDOWS)
    if (userProfile) {
        dirs.push_back(std::string(userProfile) + "/AppData/Roaming/OmniSteam/lua");
    }
#elif defined(OMNI_PLATFORM_MACOS)
    if (home) {
        dirs.push_back(std::string(home) + "/Library/Application Support/Steam/config/lua");
        dirs.push_back(std::string(home) + "/.config/omnisteam/lua");
    }
#else // Linux
    if (home) {
        dirs.push_back(std::string(home) + "/.local/share/Steam/config/lua");
        dirs.push_back(std::string(home) + "/.steam/steam/config/lua");
        dirs.push_back(std::string(home) + "/.config/omnisteam/lua");
    }
#endif

    // Deduplicate and filter existing directories or candidate creation
    std::vector<std::string> result;
    for (const auto& d : dirs) {
        if (fs::exists(d)) {
            result.push_back(d);
        }
    }
    // If none existed yet, default to standard config/lua
    if (result.empty()) {
        result.push_back("config/lua");
    }
    return result;
}

std::string GetConfigPath() {
    if (fs::exists("omnisteam.toml")) {
        return "omnisteam.toml";
    }
    if (fs::exists("config/omnisteam.toml")) {
        return "config/omnisteam.toml";
    }
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/omnisteam/omnisteam.toml";
    }
    return "omnisteam.toml";
}
} // namespace

static void InitializeOmniSteam() {
    Log::Init();
    spdlog::info("OmniSteam cross-platform core initializing...");

    std::string exePath = OmniPlatform::Process::GetExecutablePath();
    spdlog::info("Attached Process: {} (PID: {})", exePath, OmniPlatform::Process::GetCurrentProcessId());

    OmniPlatform::Thread::StartDetached([]() {
    // 1. Wait for Steam core module to be loaded by steam.exe
#if defined(OMNI_PLATFORM_WINDOWS)
        const std::string targetModule = "steamclient64.dll";
#elif defined(OMNI_PLATFORM_MACOS)
        const std::string targetModule = "steamclient.dylib";
#else
        const std::string targetModule = "steamclient.so";
#endif

        int waitAttempts = 0;
        while (!OmniPlatform::DynamicLibrary::GetLoadedModule(targetModule) && waitAttempts < 50) {
            OmniPlatform::Thread::Sleep(100);
            waitAttempts++;
        }
        spdlog::info("Target module {} ready (waited {}ms)", targetModule, waitAttempts * 100);

        // 2. Load Configuration and Pattern Signatures
        Config::Load(GetConfigPath());
        PatternLoader::Initialize();

        // 3. Parse and monitor all Lua script directories
        auto luaDirs = GetCandidateLuaDirectories();
        for (const auto& dir : luaDirs) {
            spdlog::info("Scanning Lua directory: {}", dir);
            LuaConfig::ParseDirectory(dir);
        }

        std::vector<std::string> watchDirs = luaDirs;
        for (const auto& p : Config::GetLuaPaths()) {
            if (fs::exists(p)) {
                watchDirs.push_back(p);
            }
        }

        OmniPlatform::DirectoryWatch::StartWatch(watchDirs, [](const std::string& path, bool isDir) {
            if (!isDir && path.ends_with(".lua")) {
                spdlog::info("Hot reload Lua: {}", path);
                LuaConfig::ParseFile(path);
            }
        });

        // 4. Install Detour Hooks
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
#elif defined(OMNI_PLATFORM_LINUX) || defined(OMNI_PLATFORM_MACOS)
__attribute__((constructor)) static void LibraryInit() {
    InitializeOmniSteam();
}
__attribute__((destructor)) static void LibraryFini() {
    HookManager::UninstallHooks();
}
#endif
