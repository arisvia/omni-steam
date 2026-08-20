#include "OmniPlatform/OmniPaths.h"

#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

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

    // 1. Primary Steam standard config/lua directory
    std::string steamRoot = GetSteamInstallPath();
    if (!steamRoot.empty()) {
        addDir(steamRoot + "/config/lua");
    }

    // 2. Relative working directory paths
    addDir("config/lua");

    // 3. User custom configuration directory
#if defined(OMNI_PLATFORM_WINDOWS)
    const char* appData = std::getenv("APPDATA");
    if (appData) {
        addDir(std::string(appData) + "/OmniSteam/lua");
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

    std::string steamRoot = GetSteamInstallPath();
    return steamRoot.empty() ? "config/lua" : (fs::path(steamRoot) / "config" / "lua").generic_string();
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
#if defined(OMNI_PLATFORM_WINDOWS)
    const char* localAppData = std::getenv("LOCALAPPDATA");
    std::string base = localAppData ? (fs::path(localAppData) / "OmniSteam" / "cache").generic_string()
                                    : (fs::path(GetConfigDirectory()) / "cache").generic_string();
#elif defined(OMNI_PLATFORM_MACOS)
    const char* home = std::getenv("HOME");
    std::string base = home ? (fs::path(home) / "Library" / "Caches" / "OmniSteam").generic_string()
                            : (fs::path(GetConfigDirectory()) / "cache").generic_string();
#else
    const char* xdgCache = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    std::string base = xdgCache ? (fs::path(xdgCache) / "omnisteam").generic_string()
                                : (home ? (fs::path(home) / ".cache" / "omnisteam").generic_string()
                                        : (fs::path(GetConfigDirectory()) / "cache").generic_string());
#endif
    try {
        fs::create_directories(base);
    } catch (...) {
    }
    return base;
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
