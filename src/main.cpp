#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniBuildInfo.h"
#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/DlcStore.h"
#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Security/AntiCheatGuard.h"

#include "Hook/HookManager.h"

namespace Hooks_Package {
void SyncInjectedLicenses();
}

namespace fs = std::filesystem;
namespace Log {
void Init(const std::string& componentName = "omnisteam_core");
}

namespace {
void UpdateCoreStatus(bool active, const std::string& targetModule = "", size_t luaCount = 0) {
    try {
        std::string cacheDir = OmniPlatform::Paths::GetCacheDirectory();
        std::string statusPath = (fs::path(cacheDir) / "core_status.json").generic_string();
        if (!active) {
            std::error_code ec;
            fs::remove(statusPath, ec);
            return;
        }
        auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                       .count();

        std::ofstream out(statusPath, std::ios::trunc);
        if (out) {
            out << "{\n"
                << "  \"active\": true,\n"
                << "  \"pid\": " << OmniPlatform::Process::GetCurrentProcessId() << ",\n"
                << "  \"version\": \"" << OMNISTEAM_VERSION << "\",\n"
                << "  \"commit\": \"" << OMNISTEAM_GIT_COMMIT << "\",\n"
                << "  \"targetModule\": \"" << targetModule << "\",\n"
                << "  \"hooks\": {\n"
                << "    \"CheckAppOwnership\": " << (HookManager::IsHookActive("CheckAppOwnership") ? "true" : "false")
                << ",\n"
                << "    \"ConfigStore_GetBinary\": "
                << (HookManager::IsHookActive("ConfigStoreGetBinary") ? "true" : "false") << ",\n"
                << "    \"IPCProcessMessage\": " << (HookManager::IsHookActive("IPCProcessMessage") ? "true" : "false")
                << "\n"
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
        constexpr int kMaxWaitAttempts = 600;
        while (!OmniPlatform::DynamicLibrary::GetLoadedModule(targetModule) && waitAttempts < kMaxWaitAttempts) {
            OmniPlatform::Thread::Sleep(100);
            waitAttempts++;
        }
        if (!OmniPlatform::DynamicLibrary::GetLoadedModule(targetModule)) {
            spdlog::error("Target module {} did not load within {}s; aborting hook initialization", targetModule,
                          kMaxWaitAttempts / 10);
            return;
        }
        spdlog::info("Target module {} ready (waited {}ms)", targetModule, waitAttempts * 100);

        // 2. Load Configuration, Security Guard, DLC Metadata Cache, and Pattern Signatures
        Config::Load(OmniPlatform::Paths::GetConfigPath());
        Security::AntiCheatGuard::Initialize();
        Metadata::DlcStore::Initialize();
        PatternLoader::Initialize();

        // 3. Parse Lua unlock scripts and watch for hot reloads (deduplicated)
        std::set<std::string> watchDirSet;
        auto luaDirs = OmniPlatform::Paths::GetCandidateLuaDirectories();
        for (const auto& dir : luaDirs) {
            if (fs::exists(dir)) {
                spdlog::info("Scanning Lua directory: {}", dir);
                LuaConfig::ParseDirectory(dir);
                watchDirSet.insert(fs::weakly_canonical(fs::path(dir)).generic_string());
            }
        }

        for (const auto& p : Config::GetLuaPaths()) {
            if (fs::exists(p)) {
                watchDirSet.insert(fs::weakly_canonical(fs::path(p)).generic_string());
            }
        }

        std::vector<std::string> watchDirs(watchDirSet.begin(), watchDirSet.end());
        if (!watchDirs.empty()) {
            OmniPlatform::DirectoryWatch::StartWatch(watchDirs, [watchDirs](const std::string& path, bool isDir) {
                if (!isDir && path.ends_with(".lua")) {
                    spdlog::info("Hot reload: Lua configuration modified ({}), refreshing in-memory licenses...", path);
                    LuaConfig::ReloadDirectories(watchDirs);
                    Hooks_Package::SyncInjectedLicenses();
                }
            });
        }
        // 4. Install Detour Hooks
        HookManager::InstallHooks();
        UpdateCoreStatus(true, targetModule, LuaConfig::GetUnlockedApps().size());
    });
}

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID /*lpReserved*/) {
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
