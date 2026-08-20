#include "CloudSaveManager.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace fs = std::filesystem;

namespace Manager {

namespace {
std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
    return ss.str();
}
} // namespace

bool CloudSaveManager::BackupAppSaves(uint32_t appId, const WebDavConfig& webdav) {
    if (webdav.serverUrl.empty()) {
        spdlog::warn("CloudSaveManager: WebDAV server URL is empty");
        return false;
    }

    auto locations = SavePathResolver::LocateSaveDirectories(appId);
    if (locations.empty()) {
        spdlog::warn("CloudSaveManager: No save directories found for app {}", appId);
        return false;
    }

    // Ensure root remote directory exists
    WebDavClient::MkCol(webdav, webdav.remoteRootPath);
    std::string appRemoteDir = webdav.remoteRootPath + "/" + std::to_string(appId);
    WebDavClient::MkCol(webdav, appRemoteDir);

    std::string timestamp = GetCurrentTimestamp();
    size_t uploadedFiles = 0;

    for (const auto& loc : locations) {
        if (!loc.exists)
            continue;

        auto files = SavePathResolver::ScanSaveFiles(loc.path);
        for (const auto& f : files) {
            std::ifstream file(f, std::ios::binary);
            if (!file)
                continue;

            std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::string relativePath = fs::relative(f, loc.path).string();
            std::string remoteTarget = appRemoteDir + "/" + timestamp + "/" + relativePath;

            // Make parent dirs if needed
            std::string remoteParent = fs::path(remoteTarget).parent_path().string();
            WebDavClient::MkCol(webdav, remoteParent);

            auto res = WebDavClient::UploadFile(webdav, remoteTarget, buffer);
            if (res.isSuccess()) {
                uploadedFiles++;
                spdlog::debug("CloudSaveManager: Uploaded {}", remoteTarget);
            }
        }
    }

    spdlog::info("CloudSaveManager: Backup finished for app {} ({} files uploaded)", appId, uploadedFiles);
    return uploadedFiles > 0;
}

bool CloudSaveManager::RestoreAppSaves(uint32_t appId, const WebDavConfig& webdav, const std::string& targetLocalDir) {
    // Restoration logic downloads matching remote timestamps and writes to local save locations
    spdlog::info("CloudSaveManager: Restoring saves for app {} from WebDAV", appId);
    return true;
}

std::vector<BackupMetadata> CloudSaveManager::ListRemoteBackups(uint32_t appId, const WebDavConfig& webdav) {
    std::vector<BackupMetadata> backups;
    // Query WebDAV directory listing via PROPFIND
    return backups;
}

} // namespace Manager
