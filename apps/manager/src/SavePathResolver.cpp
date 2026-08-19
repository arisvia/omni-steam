#include "SavePathResolver.h"

#include <cstdlib>
#include <filesystem>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"

namespace fs = std::filesystem;

namespace Manager {

std::string SavePathResolver::GetSteamInstallDirectory() {
#if defined(OMNI_PLATFORM_WINDOWS)
    return "C:/Program Files (x86)/Steam";
#elif defined(OMNI_PLATFORM_MACOS)
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/Steam" : "";
#else
    const char* home = std::getenv("HOME");
    if (!home)
        return "";
    std::string defaultPath = std::string(home) + "/.local/share/Steam";
    if (fs::exists(defaultPath))
        return defaultPath;
    std::string altPath = std::string(home) + "/.steam/steam";
    if (fs::exists(altPath))
        return altPath;
    return defaultPath;
#endif
}

std::vector<SaveLocation> SavePathResolver::LocateSaveDirectories(uint32_t appId) {
    std::vector<SaveLocation> locations;
    std::string steamDir = GetSteamInstallDirectory();

    // 1. Steam UserData Remote saves: <Steam>/userdata/<account_id>/<appid>
    std::string userDataPath = steamDir + "/userdata";
    if (fs::exists(userDataPath)) {
        for (const auto& accountEntry : fs::directory_iterator(userDataPath)) {
            if (accountEntry.is_directory()) {
                std::string appSavePath = accountEntry.path().string() + "/" + std::to_string(appId);
                SaveLocation loc;
                loc.appId = appId;
                loc.path = appSavePath;
                loc.description = "Steam UserData (" + accountEntry.path().filename().string() + ")";
                loc.exists = fs::exists(appSavePath);
                locations.push_back(loc);
            }
        }
    }

#if !defined(OMNI_PLATFORM_WINDOWS)
    // 2. Linux / Steam Deck Proton CompatData (WINE Prefix):
    // <Steam>/steamapps/compatdata/<appid>/pfx/drive_c/users/steamuser/
    std::string compatDataPath =
        steamDir + "/steamapps/compatdata/" + std::to_string(appId) + "/pfx/drive_c/users/steamuser";
    if (fs::exists(compatDataPath)) {
        // Check AppData/Local, AppData/Roaming, Saved Games, Documents
        std::vector<std::pair<std::string, std::string>> subPaths = {{"/AppData/Local", "Proton AppData/Local"},
                                                                     {"/AppData/Roaming", "Proton AppData/Roaming"},
                                                                     {"/Saved Games", "Proton Saved Games"},
                                                                     {"/Documents", "Proton Documents"}};

        for (const auto& [sub, desc] : subPaths) {
            std::string fullSub = compatDataPath + sub;
            if (fs::exists(fullSub)) {
                SaveLocation loc;
                loc.appId = appId;
                loc.path = fullSub;
                loc.description = desc;
                loc.exists = true;
                locations.push_back(loc);
            }
        }
    }
#endif

    return locations;
}

std::vector<std::string> SavePathResolver::ScanSaveFiles(const std::string& saveDir) {
    std::vector<std::string> files;
    if (!fs::exists(saveDir))
        return files;

    try {
        for (const auto& entry : fs::recursive_directory_iterator(saveDir)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path().string());
            }
        }
    } catch (...) {
    }

    return files;
}

} // namespace Manager
