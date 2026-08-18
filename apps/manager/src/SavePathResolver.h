#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Manager {

struct SaveLocation {
    uint32_t appId = 0;
    std::string path;
    std::string description; // e.g. "Proton CompatData", "Steam UserData", "Windows AppData"
    bool exists = false;
};

class SavePathResolver {
public:
    static std::string GetSteamInstallDirectory();
    static std::vector<SaveLocation> LocateSaveDirectories(uint32_t appId);
    static std::vector<std::string> ScanSaveFiles(const std::string& saveDir);
};

} // namespace Manager
