#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Manager {

struct CoreStatusInfo {
    bool installed = false;
    bool active = false; // Is live and hooked inside steam.exe
    uint32_t livePid = 0;
    std::string installedVersion;
    std::string installedCommit;
    std::string latestVersion;
    bool updateAvailable = false;
    std::string steamInstallPath;
    std::string targetModule;
    bool checkAppOwnershipHook = false;
    bool configStoreHook = false;
    bool ipcHook = false;
    uint64_t lastHeartbeat = 0;
};

struct InstallResult {
    bool success = false;
    std::string message;
};

class CoreInstaller {
public:
    static CoreStatusInfo GetStatus();
    static InstallResult InstallCore(const std::string& channel = "release"); // "release" or "nightly"
    static bool UninstallCore();
    static std::string GetLatestRemoteVersion(const std::string& channel = "release");
};

} // namespace Manager
