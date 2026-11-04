#pragma once
#include <string>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"

namespace Manager {

struct ConfigDto {
    std::string logLevel = "info";
    // Round-tripped verbatim: the dashboard stores the preset token, and
    // ManifestClient::ResolveUpstreamBase() expands it. See OmniEndpoints::Manifest.
    std::string manifestUrl = OmniEndpoints::Manifest::kDefaultUpstream;
    bool statsEnableApi = true;
    std::vector<std::string> luaPaths;
    // Mirrors the keys the core hook reads from the same file. The dashboard rewrites the
    // whole config on every save, so any key missing here is deleted from omnisteam.toml.
    bool autoUnlockDlc = true;
    bool allowInsecureTls = false;
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
