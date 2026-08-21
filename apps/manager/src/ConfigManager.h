#pragma once
#include <string>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"

namespace Manager {

struct ConfigDto {
    std::string logLevel = "info";
    std::string manifestUrl = "opensteamtool";
    bool statsEnableApi = true;
    std::vector<std::string> luaPaths;
    bool cloudEnabled = false;
    std::string webdavServerUrl;
    std::string webdavUsername;
    std::string webdavPassword;
    std::string webdavRemoteRoot = OmniEndpoints::CloudSave::kDefaultRemoteRoot;
};

class ConfigManager {
public:
    static std::string GetConfigFilePath();
    static ConfigDto ReadConfig();
    static bool SaveConfig(const ConfigDto& dto);
    static bool MigrateLuaFiles(const std::string& oldDir, const std::string& newDir);
};

} // namespace Manager
