#include "SavePathResolver.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace fs = std::filesystem;

namespace Manager {

namespace {
std::vector<std::string> GetSteamLibraryFolders(const std::string& steamDir) {
    std::vector<std::string> libraries;
    libraries.push_back(steamDir);

    std::string vdfPath = steamDir + "/steamapps/libraryfolders.vdf";
    if (!fs::exists(vdfPath)) {
        vdfPath = steamDir + "/config/libraryfolders.vdf";
    }

    if (fs::exists(vdfPath)) {
        try {
            std::ifstream file(vdfPath);
            std::string line;
            std::regex pathRegex("\"path\"\\s+\"([^\"]+)\"", std::regex::icase);
            while (std::getline(file, line)) {
                std::smatch match;
                if (std::regex_search(line, match, pathRegex) && match.size() > 1) {
                    std::string libPath = match[1].str();
                    if (fs::exists(libPath) &&
                        std::find(libraries.begin(), libraries.end(), libPath) == libraries.end()) {
                        libraries.push_back(libPath);
                    }
                }
            }
        } catch (...) {
        }
    }
    return libraries;
}

void ParseRemoteCacheLocations(const std::string& vdfPath, uint32_t appId, const std::string& steamDir,
                               const std::string& accountId, std::vector<SaveLocation>& locations) {
    if (!fs::exists(vdfPath))
        return;
    try {
        std::ifstream file(vdfPath);
        std::string line;
        std::string currentFile;
        std::string currentRoot;

        while (std::getline(file, line)) {
            size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos)
                continue;
            line = line.substr(first);

            if (line.front() == '"') {
                size_t q2 = line.find('"', 1);
                if (q2 != std::string::npos) {
                    std::string key = line.substr(1, q2 - 1);
                    if (key == "root") {
                        size_t q3 = line.find('"', q2 + 1);
                        if (q3 != std::string::npos) {
                            size_t q4 = line.find('"', q3 + 1);
                            if (q4 != std::string::npos) {
                                currentRoot = line.substr(q3 + 1, q4 - q3 - 1);
                            }
                        }
                    } else if (key != "ChangeNumber" && key != "OSType" && key != std::to_string(appId)) {
                        currentFile = key;
                    }
                }
            } else if (line.front() == '}' && !currentFile.empty() && !currentRoot.empty()) {
                std::string resolvedDir;
                if (currentRoot == "0") {
                    resolvedDir = steamDir + "/userdata/" + accountId + "/" + std::to_string(appId) + "/remote";
                }
#if defined(OMNI_PLATFORM_WINDOWS)
                else if (currentRoot == "WinMyDocuments") {
                    const char* prof = std::getenv("USERPROFILE");
                    if (prof)
                        resolvedDir = std::string(prof) + "/Documents";
                } else if (currentRoot == "WinAppDataLocal") {
                    const char* local = std::getenv("LOCALAPPDATA");
                    if (local)
                        resolvedDir = local;
                } else if (currentRoot == "WinAppDataRoaming") {
                    const char* app = std::getenv("APPDATA");
                    if (app)
                        resolvedDir = app;
                } else if (currentRoot == "WinSavedGames") {
                    const char* prof = std::getenv("USERPROFILE");
                    if (prof)
                        resolvedDir = std::string(prof) + "/Saved Games";
                }
#else
                else if (currentRoot == "WinMyDocuments" || currentRoot == "WinAppDataLocal" ||
                         currentRoot == "WinAppDataRoaming" || currentRoot == "WinSavedGames") {
                    std::string compatPrefix =
                        steamDir + "/steamapps/compatdata/" + std::to_string(appId) + "/pfx/drive_c/users/steamuser";
                    if (currentRoot == "WinMyDocuments") {
                        resolvedDir = compatPrefix + "/Documents";
                    } else if (currentRoot == "WinAppDataLocal") {
                        resolvedDir = compatPrefix + "/AppData/Local";
                    } else if (currentRoot == "WinAppDataRoaming") {
                        resolvedDir = compatPrefix + "/AppData/Roaming";
                    } else if (currentRoot == "WinSavedGames") {
                        resolvedDir = compatPrefix + "/Saved Games";
                    }
                }
#endif
                if (!resolvedDir.empty()) {
                    fs::path saveFilePath = fs::path(resolvedDir) / currentFile;
                    fs::path saveDirPath = saveFilePath.parent_path();
                    std::string dirStr = saveDirPath.generic_string();
                    if (!dirStr.empty() && fs::exists(dirStr)) {
                        bool exists = false;
                        for (const auto& l : locations) {
                            if (l.path == dirStr) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            locations.push_back({appId, dirStr, "Steam Auto-Cloud UFS (" + currentRoot + ")", true});
                        }
                    }
                }
                currentFile.clear();
                currentRoot.clear();
            }
        }
    } catch (...) {
    }
}
} // namespace

std::string SavePathResolver::GetSteamInstallDirectory() {
    return OmniPlatform::Paths::GetSteamInstallPath();
}

std::vector<SaveLocation> SavePathResolver::LocateSaveDirectories(uint32_t appId) {
    std::vector<SaveLocation> locations;
    std::string steamDir = GetSteamInstallDirectory();
    auto libraryFolders = GetSteamLibraryFolders(steamDir);

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

    // 1b. Steam Auto-Cloud UFS resolution via remotecache.vdf
    if (fs::exists(userDataPath)) {
        for (const auto& accountEntry : fs::directory_iterator(userDataPath)) {
            if (accountEntry.is_directory()) {
                std::string accountId = accountEntry.path().filename().string();
                std::string vdfPath = accountEntry.path().string() + "/" + std::to_string(appId) + "/remotecache.vdf";
                ParseRemoteCacheLocations(vdfPath, appId, steamDir, accountId, locations);
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
    // 2. Linux / Steam Deck Proton CompatData (WINE Prefix) across all Steam library roots:
    // <LibraryRoot>/steamapps/compatdata/<appid>/pfx/drive_c/users/steamuser/
    for (const auto& libRoot : libraryFolders) {
        std::string compatDataPath =
            libRoot + "/steamapps/compatdata/" + std::to_string(appId) + "/pfx/drive_c/users/steamuser";
        if (fs::exists(compatDataPath)) {
            std::vector<std::pair<std::string, std::string>> subPaths = {
                {"/AppData/Local", "Proton AppData/Local (" + fs::path(libRoot).filename().string() + ")"},
                {"/AppData/Roaming", "Proton AppData/Roaming (" + fs::path(libRoot).filename().string() + ")"},
                {"/Saved Games", "Proton Saved Games (" + fs::path(libRoot).filename().string() + ")"},
                {"/Documents", "Proton Documents (" + fs::path(libRoot).filename().string() + ")"},
                {"/Documents/My Games", "Proton Documents/My Games (" + fs::path(libRoot).filename().string() + ")"}};

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
