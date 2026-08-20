#include <chrono>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniBuildInfo.h"
#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/PatternLoader.h"

#include "Hook/HookManager.h"

namespace fs = std::filesystem;

namespace Log {
void Init(const std::string& componentName = "omnisteam_core");
}

namespace {
void UpdateCoreStatus(bool active, const std::string& targetModule = "", size_t luaCount = 0) {
    try {
        std::string cacheDir = OmniPlatform::Paths::GetCacheDirectory();
        std::string statusPath = (fs::path(cacheDir) / "core_status.json").generic_string();
        auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                       .count();

        std::ofstream out(statusPath, std::ios::trunc);
        if (out) {
            out << "{\n"
                << "  \"active\": " << (active ? "true" : "false") << ",\n"
                << "  \"pid\": " << OmniPlatform::Process::GetCurrentProcessId() << ",\n"
                << "  \"version\": \"" << OMNISTEAM_VERSION << "\",\n"
                << "  \"commit\": \"" << OMNISTEAM_GIT_COMMIT << "\",\n"
                << "  \"targetModule\": \"" << targetModule << "\",\n"
                << "  \"hooks\": {\n"
                << "    \"CheckAppOwnership\": true,\n"
                << "    \"ConfigStore_GetBinary\": true,\n"
                << "    \"IPCProcessMessage\": true\n"
                << "  },\n"
                << "  \"luaFilesCount\": " << luaCount << ",\n"
                << "  \"timestamp\": " << now << "\n"
                << "}\n";
        }
    } catch (...) {
    }
}
} // namespace

// Exported standard metadata query symbols
extern "C" {
#if defined(OMNI_PLATFORM_WINDOWS)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const char* OmniSteam_GetVersion() {
    return OMNISTEAM_VERSION;
}

#if defined(OMNI_PLATFORM_WINDOWS)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const char* OmniSteam_GetCommitHash() {
    return OMNISTEAM_GIT_COMMIT;
}
}

static void InitializeOmniSteam() {
    Log::Init();
    spdlog::info("OmniSteam cross-platform core initializing (Version: {}, Commit: {})...", OMNISTEAM_VERSION,
                 OMNISTEAM_GIT_COMMIT);

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
        Config::Load(OmniPlatform::Paths::GetConfigPath());
        PatternLoader::Initialize();

        // 3. Parse and monitor all Lua script directories
        auto luaDirs = OmniPlatform::Paths::GetCandidateLuaDirectories();
        size_t totalLuaLoaded = 0;
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

        // 5. Update live core status
        UpdateCoreStatus(true, targetModule, LuaConfig::GetUnlockedApps().size());
    });
}

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitializeOmniSteam();
    } else if (dwReason == DLL_PROCESS_DETACH) {
        UpdateCoreStatus(false);
        HookManager::UninstallHooks();
    }
    return TRUE;
}
#elif defined(OMNI_PLATFORM_LINUX) || defined(OMNI_PLATFORM_MACOS)
__attribute__((constructor)) static void LibraryInit() {
    InitializeOmniSteam();
}
__attribute__((destructor)) static void LibraryFini() {
    UpdateCoreStatus(false);
    HookManager::UninstallHooks();
}
#endif
