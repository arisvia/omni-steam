#pragma once
#include <cstdint>
#include <string>
#include <utility>
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
    // Full per-hook registry snapshot published by the core (name, attached).
    // The three legacy booleans above stay in sync for older UI consumers.
    std::vector<std::pair<std::string, bool>> hooks;
    // Resolved-only anchors (name, hex address) used by smoke regression.
    std::vector<std::pair<std::string, std::string>> anchors;
    // Live layout-probe counters published by the core (name, value): what the
    // client itself filled at the offsets the hooks write. See HookStatus.h.
    std::vector<std::pair<std::string, uint64_t>> layout;
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
