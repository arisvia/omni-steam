#include "SavePathResolver.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace fs = std::filesystem;

namespace Manager {

std::string SavePathResolver::GetSteamInstallDirectory() {
    return OmniPlatform::Paths::GetSteamInstallPath();
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

#if defined(OMNI_PLATFORM_WINDOWS)
    // 2. Windows Native Saved Games & AppData/Local/AppId paths
    const char* userProfile = std::getenv("USERPROFILE");
    const char* localAppData = std::getenv("LOCALAPPDATA");
    const char* appData = std::getenv("APPDATA");

    if (localAppData) {
        std::string localSavePath = std::string(localAppData) + "/" + std::to_string(appId);
        if (fs::exists(localSavePath)) {
            locations.push_back({appId, localSavePath, "Windows LocalAppData/" + std::to_string(appId), true});
        }
    }
    if (appData) {
        std::string roamingSavePath = std::string(appData) + "/" + std::to_string(appId);
        if (fs::exists(roamingSavePath)) {
            locations.push_back({appId, roamingSavePath, "Windows AppData/Roaming/" + std::to_string(appId), true});
        }
    }
    if (userProfile) {
        std::string savedGamesPath = std::string(userProfile) + "/Saved Games/" + std::to_string(appId);
        if (fs::exists(savedGamesPath)) {
            locations.push_back({appId, savedGamesPath, "Windows Saved Games/" + std::to_string(appId), true});
        }
        std::string docSavePath = std::string(userProfile) + "/Documents/My Games/" + std::to_string(appId);
        if (fs::exists(docSavePath)) {
            locations.push_back({appId, docSavePath, "Windows Documents/My Games/" + std::to_string(appId), true});
        }
    }
#else
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
