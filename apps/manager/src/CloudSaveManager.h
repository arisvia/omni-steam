#pragma once
#include "WebDavClient.h"
#include "SavePathResolver.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Manager {

struct BackupMetadata {
    uint32_t appId = 0;
    std::string timestamp;
    std::string backupFileName;
    size_t fileCount = 0;
    size_t totalBytes = 0;
};

class CloudSaveManager {
public:
    static bool BackupAppSaves(uint32_t appId, const WebDavConfig& webdav);
    static bool RestoreAppSaves(uint32_t appId, const WebDavConfig& webdav, const std::string& targetLocalDir = "");
    static std::vector<BackupMetadata> ListRemoteBackups(uint32_t appId, const WebDavConfig& webdav);
};

} // namespace Manager
