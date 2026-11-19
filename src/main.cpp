#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <vector>

#include "OmniPlatform/OmniBuildInfo.h"
#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Metadata/DlcStore.h"
#include "Utils/Metadata/PatternLoader.h"
#include "Utils/Security/AntiCheatGuard.h"

#include "Hook/HookManager.h"
#include "Hook/HookStatus.h"

namespace Hooks_Package {
void SyncInjectedLicenses();
}

namespace fs = std::filesystem;
namespace Log {
void Init(const std::string& componentName = "omnisteam_core");
}

namespace {
bool ReplaceFile(const fs::path& destination, std::string_view contents) {
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        spdlog::warn("Cannot create status directory {}: {}", destination.parent_path().string(), ec.message());
        return false;
    }

    fs::path temporary = destination;
    // The PID alone is not unique enough: the process-exit writer (DllMain / library
    // destructor) can race the init thread that is still booting, and both would then
    // funnel two different payloads through one scratch file.
    static std::atomic<uint64_t> scratchSequence{0};
    temporary += "." + std::to_string(OmniPlatform::Process::GetCurrentProcessId()) + "." +
                 std::to_string(scratchSequence.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            spdlog::warn("Cannot open temporary status file: {}", temporary.string());
            return false;
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            spdlog::warn("Cannot write temporary status file: {}", temporary.string());
            out.close();
            fs::remove(temporary, ec);
            return false;
        }
    }

    fs::rename(temporary, destination, ec);
    if (!ec)
        return true;

    // std::filesystem::rename cannot replace an existing file on Windows.
    std::error_code removeError;
    fs::remove(destination, removeError);
    ec.clear();
    fs::rename(temporary, destination, ec);
    if (!ec)
        return true;

    spdlog::warn("Cannot replace status file {}: {}", destination.string(), ec.message());
    fs::remove(temporary, removeError);
    return false;
}

void UpdateCoreStatus(bool active, const std::string& targetModule = "", size_t luaCount = 0) {
    try {
        const fs::path statusPath = fs::path(OmniPlatform::Paths::GetCacheDirectory()) / "core_status.json";
        if (!active) {
            std::error_code ec;
            fs::remove(statusPath, ec);
            return;
        }

        const auto now =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        const std::string json = HookStatus::BuildCoreStatusJson(
            OmniPlatform::Process::GetCurrentProcessId(), OMNISTEAM_VERSION, OMNISTEAM_GIT_COMMIT, targetModule,
            luaCount, static_cast<int64_t>(now), HookManager::GetHookStates(), PatternLoader::GetResolvedAddresses());
        ReplaceFile(statusPath, json);
    } catch (const std::exception& ex) {
        spdlog::debug("Failed to update core status: {}", ex.what());
    } catch (...) {
        spdlog::debug("Failed to update core status: unknown error");
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

static bool IsSteamHostProcess() {
    std::string exePath = OmniPlatform::Process::GetExecutablePath();
    size_t lastSlash = exePath.find_last_of("/\\");
    std::string exeName = (lastSlash != std::string::npos) ? exePath.substr(lastSlash + 1) : exePath;
    for (char& c : exeName) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return exeName == "steam.exe" || exeName == "steam" || exeName == "steam_osx";
}

static void InitializeOmniSteam() {
    if (!IsSteamHostProcess()) {
        return; // Never hook into steamwebhelper.exe or helper processes
    }

    // Pre-log stderr breadcrumb (stderr is unbuffered): CI needs it to tell
    // "library never loaded" apart from "host died before spdlog flushed".
    // Printed only for accepted hosts — every Steam child process inherits the
    // preload, so an unconditional line buries the real signal.
    std::fprintf(stderr, "[omnisteam] attached pid=%u exe=%s\n", OmniPlatform::Process::GetCurrentProcessId(),
                 OmniPlatform::Process::GetExecutablePath().c_str());

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

        // Wait for the host to map the steamclient module. The deadline must
        // work WITHOUT configuration: SteamRT under pressure-vessel scrubs the
        // environment on the way into the container, so an env override only
        // reaches processes outside it - the default itself has to outlast a
        // cold CI bootstrap (measured: the host was still running Steam's own
        // update check 2 minutes after our old 60s deadline fired).
        // Poll at 100ms for the first 10s (interactive launches map the module
        // immediately), then once a second: the loaded-module probe parses
        // /proc/self/maps, and ten minutes of 100ms polling is pure churn in a
        // host that is never going to load it.
        int deadlineMs = 600000;
        if (const char* envWait = std::getenv("OMNISTEAM_TARGET_MODULE_WAIT_SECONDS")) {
            const int seconds = std::atoi(envWait);
            if (seconds > 0 && seconds <= 3600) {
                deadlineMs = seconds * 1000;
            }
        }
        spdlog::info("Waiting for target module {} to load (max {}s)...", targetModule, deadlineMs / 1000);
        int waitedMs = 0;
        while (!OmniPlatform::DynamicLibrary::GetLoadedModule(targetModule)) {
            if (waitedMs >= deadlineMs) {
                break;
            }
            const int stepMs = (waitedMs < 10000) ? 100 : 1000;
            OmniPlatform::Thread::Sleep(stepMs);
            waitedMs += stepMs;
        }
        if (!OmniPlatform::DynamicLibrary::GetLoadedModule(targetModule)) {
            spdlog::error("Target module {} did not load within {}ms; aborting hook initialization", targetModule,
                          waitedMs);
            return;
        }
        spdlog::info("Target module {} ready (waited {}ms)", targetModule, waitedMs);

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
    }
    // ponytail: leaving detours until process teardown avoids rewriting live Steam
    // code from DllMain's loader lock; add an explicit shutdown hook if DLL
    // hot-unload is ever required. CoreInstaller invalidates the stale PID.
    return TRUE;
}
#elif defined(OMNI_PLATFORM_LINUX) || defined(OMNI_PLATFORM_MACOS)
__attribute__((constructor)) static void LibraryInit() {
    InitializeOmniSteam();
}
#endif
