#pragma once
#include "SavePathResolver.h"
#include "WebDavClient.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace Manager {

struct BackupMetadata {
    uint32_t appId = 0;
    std::string timestamp;
    std::string backupFileName;
    size_t fileCount = 0;
    size_t totalBytes = 0;
};
struct CloudSyncStatus {
    uint32_t appId = 0;
    std::string action; // "backup" or "restore"
    std::string status; // "success", "failed", "in_progress"
    std::string timestamp;
    size_t fileCount = 0;
    size_t totalBytes = 0;
    std::string message;
};

class CloudSaveManager {
public:
    // Core backup & restore routines (supporting both WebDAV cloud sync and local snapshot fallback)
    static bool BackupAppSaves(uint32_t appId, const WebDavConfig& webdav);
    static bool BackupAppSavesLocal(uint32_t appId);
    static bool RestoreAppSaves(uint32_t appId, const WebDavConfig& webdav, const std::string& targetLocalDir = "");
    static std::vector<BackupMetadata> ListRemoteBackups(uint32_t appId, const WebDavConfig& webdav);
    static std::string GetLocalBackupDirectory(uint32_t appId);

    static void RecordSyncStatus(const CloudSyncStatus& status);
    static std::optional<CloudSyncStatus> GetSyncStatus(uint32_t appId);
    static std::vector<CloudSyncStatus> GetAllSyncStatuses();
};
} // namespace Manager
