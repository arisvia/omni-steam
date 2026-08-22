#pragma once
#include <string>
#include <vector>

namespace OmniPlatform {

class Paths {
public:
    // 1. Steam Installation & Subdirectories
    static std::string GetSteamInstallPath();
    static std::string GetSteamAppsPath();
    static std::string GetSteamLogsPath();
    static std::vector<std::string> GetCandidateLuaDirectories();
    static std::string GetDefaultLuaDirectory();
    // 2. OmniSteam Configuration, Cache, and Credentials
    static std::string GetConfigPath();
    static std::string GetConfigDirectory();
    static std::string GetCacheDirectory();
    static std::string GetCredentialsDirectory();

    // 3. Component Log Path Resolver (Steam/logs/omnisteam_*.log)
    static std::string ResolveLogFilePath(const std::string& componentName);
};

} // namespace OmniPlatform
