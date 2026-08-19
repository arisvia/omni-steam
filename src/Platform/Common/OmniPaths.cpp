#include "OmniPlatform/OmniPaths.h"

#include <cstdlib>
#include <filesystem>
#include <set>

#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace OmniPlatform {

namespace {
#if defined(OMNI_PLATFORM_WINDOWS)
std::string GetSteamPathFromWindowsRegistry() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[MAX_PATH];
        DWORD bufSize = sizeof(buf);
        DWORD type = REG_SZ;
        if (RegQueryValueExA(hKey, "SteamPath", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &bufSize) ==
            ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::string(buf);
        }
        RegCloseKey(hKey);
    }
    return "";
}
#endif
} // namespace

std::string Paths::GetSteamInstallPath() {
#if defined(OMNI_PLATFORM_WINDOWS)
    std::string regPath = GetSteamPathFromWindowsRegistry();
    if (!regPath.empty() && fs::exists(regPath)) {
        return fs::path(regPath).generic_string();
    }
    if (fs::exists("C:/Program Files (x86)/Steam")) {
        return "C:/Program Files (x86)/Steam";
    }
    if (fs::exists("C:/Program Files/Steam")) {
        return "C:/Program Files/Steam";
    }
    return "C:/Program Files (x86)/Steam";
#elif defined(OMNI_PLATFORM_MACOS)
    const char* home = std::getenv("HOME");
    if (home) {
        std::string p = std::string(home) + "/Library/Application Support/Steam";
        if (fs::exists(p))
            return p;
    }
    return home ? (std::string(home) + "/Library/Application Support/Steam") : "/tmp/steam";
#else
    const char* home = std::getenv("HOME");
    if (home) {
        std::string p1 = std::string(home) + "/.local/share/Steam";
        if (fs::exists(p1))
            return p1;
        std::string p2 = std::string(home) + "/.steam/steam";
        if (fs::exists(p2))
            return p2;
    }
    return home ? (std::string(home) + "/.local/share/Steam") : "/tmp/steam";
#endif
}

std::string Paths::GetSteamLogsPath() {
    std::string steamBase = GetSteamInstallPath();
    std::string logsDir = (fs::path(steamBase) / "logs").generic_string();
    try {
        if (!fs::exists(logsDir)) {
            fs::create_directories(logsDir);
        }
    } catch (...) {
    }
    return logsDir;
}

std::vector<std::string> Paths::GetCandidateLuaDirectories() {
    std::vector<std::string> dirs;
    std::set<std::string> seen;

    auto addDir = [&](const std::string& d) {
        if (!d.empty()) {
            std::string normalized = fs::path(d).lexically_normal().generic_string();
            if (!seen.contains(normalized)) {
                seen.insert(normalized);
                dirs.push_back(normalized);
            }
        }
    };

    // 1. Current working directory relative paths
    addDir("config/lua");
    addDir("lua");

    // 2. Platform primary Steam installation path
    std::string steamRoot = GetSteamInstallPath();
    if (!steamRoot.empty()) {
        addDir(steamRoot + "/config/lua");
        addDir(steamRoot + "/lua");
    }

#if defined(OMNI_PLATFORM_WINDOWS)
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        addDir(std::string(userProfile) + "/AppData/Roaming/OmniSteam/lua");
    }
#elif defined(OMNI_PLATFORM_MACOS)
    const char* home = std::getenv("HOME");
    if (home) {
        addDir(std::string(home) + "/Library/Application Support/OmniSteam/lua");
        addDir(std::string(home) + "/.config/omnisteam/lua");
    }
#else
    const char* home = std::getenv("HOME");
    if (home) {
        addDir(std::string(home) + "/.config/omnisteam/lua");
    }
#endif

    return dirs;
}

std::string Paths::GetDefaultLuaDirectory() {
    auto candidates = GetCandidateLuaDirectories();
    for (const auto& d : candidates) {
        if (fs::exists(d)) {
            return d;
        }
    }

    std::string defaultPath = (fs::path(GetSteamInstallPath()) / "config" / "lua").generic_string();
    try {
        fs::create_directories(defaultPath);
    } catch (...) {
    }
    return defaultPath;
}

std::string Paths::GetConfigDirectory() {
#if defined(OMNI_PLATFORM_WINDOWS)
    const char* appData = std::getenv("APPDATA");
    if (appData) {
        return (fs::path(appData) / "OmniSteam").generic_string();
    }
    return "config";
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return (fs::path(home) / ".config" / "omnisteam").generic_string();
    }
    return "config";
#endif
}

std::string Paths::GetConfigPath() {
    if (fs::exists("omnisteam.toml")) {
        return "omnisteam.toml";
    }
    if (fs::exists("config/omnisteam.toml")) {
        return "config/omnisteam.toml";
    }

    std::string globalConfig = (fs::path(GetConfigDirectory()) / "omnisteam.toml").generic_string();
    if (fs::exists(globalConfig)) {
        return globalConfig;
    }

    return "omnisteam.toml";
}

std::string Paths::GetCacheDirectory() {
    std::string localCache = "cache";
    try {
        if (fs::exists(localCache) || fs::create_directories(localCache)) {
            return localCache;
        }
    } catch (...) {
    }

    std::string globalCache = (fs::path(GetConfigDirectory()) / "cache").generic_string();
    try {
        fs::create_directories(globalCache);
    } catch (...) {
    }
    return globalCache;
}

std::string Paths::GetCredentialsDirectory() {
    std::string credsDir = (fs::path(GetConfigDirectory()) / "credentials").generic_string();
    try {
        fs::create_directories(credsDir);
    } catch (...) {
    }
    return credsDir;
}

std::string Paths::ResolveLogFilePath(const std::string& componentName) {
    std::string logFileName = componentName + ".log";
    std::string logsDir = GetSteamLogsPath();

    try {
        if (fs::exists(logsDir) || fs::create_directories(logsDir)) {
            return (fs::path(logsDir) / logFileName).generic_string();
        }
    } catch (...) {
    }

    // Fallback to local logs
    try {
        if (fs::exists("logs") || fs::create_directories("logs")) {
            return (fs::path("logs") / logFileName).generic_string();
        }
    } catch (...) {
    }

    return logFileName;
}

} // namespace OmniPlatform
