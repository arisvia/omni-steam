#include "SteamLauncher.h"

#include <cstdint>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>

#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Manager {

namespace {

#if !defined(OMNI_PLATFORM_WINDOWS)
// Double-fork + setsid so the long-lived Manager never becomes the parent of
// the Steam process tree (no zombies, no SIGHUP propagation on Manager exit).
// The environment variable is prepended to any existing value so other
// preloads (e.g. MangoHud, SteamRT shims) keep working.
bool SpawnDetachedWithPreload(const char* envName, const std::string& libPath, const std::string& exePath,
                              bool searchPath) {
    std::string value = libPath;
    const char* existing = std::getenv(envName);
    if (existing != nullptr && existing[0] != '\0') {
        value += ":";
        value += existing;
    }

    pid_t pid = fork();
    if (pid < 0) {
        spdlog::error("SteamLauncher: fork failed");
        return false;
    }
    if (pid == 0) {
        // Intermediate child: detach the session, orphan the grandchild to init.
        if (setsid() < 0)
            _exit(127);
        pid_t grand = fork();
        if (grand < 0)
            _exit(127);
        if (grand == 0) {
            setenv(envName, value.c_str(), 1);
            if (searchPath)
                execlp(exePath.c_str(), exePath.c_str(), static_cast<char*>(nullptr));
            else
                execl(exePath.c_str(), exePath.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

} // namespace

bool SteamLauncher::IsSteamRunning(uint32_t* outPid) {
#if defined(OMNI_PLATFORM_WINDOWS)
    auto pids = OmniPlatform::Process::FindProcessIdsByName("steam.exe");
#elif defined(OMNI_PLATFORM_MACOS)
    auto pids = OmniPlatform::Process::FindProcessIdsByName("steam_osx");
    if (pids.empty())
        pids = OmniPlatform::Process::FindProcessIdsByName("steam");
#else
    auto pids = OmniPlatform::Process::FindProcessIdsByName("steam");
#endif
    if (pids.empty())
        return false;
    if (outPid != nullptr)
        *outPid = pids.front();
    return true;
}

LaunchResult SteamLauncher::Launch() {
    if (IsSteamRunning()) {
        return {false, "Steam is already running; quit and restart it to apply the injection"};
    }

    const std::string steamPath = OmniPlatform::Paths::GetSteamInstallPath();
    if (steamPath.empty()) {
        return {false, "Steam installation path not found"};
    }

#if defined(OMNI_PLATFORM_WINDOWS)
    // The xinput1_4 proxy in the Steam root loads the core automatically; a
    // plain launch with the Steam working directory is all that is needed.
    const fs::path exe = fs::path(steamPath) / "steam.exe";
    if (!fs::exists(exe)) {
        return {false, "steam.exe not found in " + steamPath};
    }
    const std::wstring exeW = OmniPlatform::Encoding::Utf8ToWide(exe.string());
    const std::wstring dirW = OmniPlatform::Encoding::Utf8ToWide(steamPath);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exeW.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, dirW.c_str(), &si, &pi)) {
        return {false, "CreateProcess failed for steam.exe"};
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    spdlog::info("SteamLauncher: launched steam.exe (proxy DLL injection)");
    return {true, "Steam launched; the xinput1_4 proxy injects the core automatically"};
#elif defined(OMNI_PLATFORM_LINUX)
    const fs::path lib = fs::path(steamPath) / "ubuntu12_64" / "libomnisteam.so";
    if (!fs::exists(lib)) {
        return {false, "Core not installed: " + lib.string() + " missing (install the core first)"};
    }
    if (!SpawnDetachedWithPreload("LD_PRELOAD", lib.string(), "steam", true)) {
        return {false, "Failed to spawn steam with LD_PRELOAD"};
    }
    spdlog::info("SteamLauncher: launched steam with LD_PRELOAD={}", lib.string());
    return {true, "Steam launch requested with LD_PRELOAD=" + lib.string()};
#else
    const fs::path lib = fs::path(steamPath) / "libomnisteam.dylib";
    if (!fs::exists(lib)) {
        return {false, "Core not installed: " + lib.string() + " missing (install the core first)"};
    }
    // Steam.app may live in /Applications or ~/Applications; the data
    // directory (GetSteamInstallPath) is NOT the app bundle.
    fs::path exe = "/Applications/Steam.app/Contents/MacOS/steam_osx";
    if (!fs::exists(exe)) {
        const char* home = std::getenv("HOME");
        if (home != nullptr) {
            exe = fs::path(home) / "Applications/Steam.app/Contents/MacOS/steam_osx";
        }
    }
    if (!fs::exists(exe)) {
        return {false, "Steam.app/Contents/MacOS/steam_osx not found"};
    }
    if (!SpawnDetachedWithPreload("DYLD_INSERT_LIBRARIES", lib.string(), exe.string(), false)) {
        return {false, "Failed to spawn steam_osx with DYLD_INSERT_LIBRARIES"};
    }
    spdlog::info("SteamLauncher: launched steam_osx with DYLD_INSERT_LIBRARIES={}", lib.string());
    return {true, "Steam launch requested with DYLD_INSERT_LIBRARIES=" + lib.string()};
#endif
}

} // namespace Manager