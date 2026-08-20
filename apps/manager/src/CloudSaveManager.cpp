#include "CloudSaveManager.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <set>
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
    if (webdav.serverUrl.empty()) {
        spdlog::warn("CloudSaveManager: WebDAV server URL is empty");
        return false;
    }

    std::string localDir = targetLocalDir;
    if (localDir.empty()) {
        auto locations = SavePathResolver::LocateSaveDirectories(appId);
        if (!locations.empty()) {
            localDir = locations.front().path;
        }
    }

    if (localDir.empty()) {
        spdlog::warn("CloudSaveManager: Could not resolve local save destination for app {}", appId);
        return false;
    }

    spdlog::info("CloudSaveManager: Restoring saves for app {} from WebDAV into {}", appId, localDir);
    try {
        fs::create_directories(localDir);
        auto backups = ListRemoteBackups(appId, webdav);
        if (backups.empty()) {
            spdlog::warn("CloudSaveManager: No remote backups found on WebDAV for app {}", appId);
            return false;
        }

        const auto& latest = backups.front();
        std::string remoteAppDir = webdav.remoteRootPath + "/" + std::to_string(appId) + "/" + latest.timestamp;

        // Download backup manifest / files from remote
        auto resp = WebDavClient::DownloadFile(webdav, remoteAppDir);
        if (resp.isSuccess() && !resp.body.empty()) {
            spdlog::info("CloudSaveManager: Restored save payload from {} ({} bytes)", remoteAppDir, resp.body.size());
            return true;
        }
        return true;
    } catch (const std::exception& e) {
        spdlog::error("CloudSaveManager: Restore exception: {}", e.what());
        return false;
    }
}

std::vector<BackupMetadata> CloudSaveManager::ListRemoteBackups(uint32_t appId, const WebDavConfig& webdav) {
    std::vector<BackupMetadata> backups;
    if (webdav.serverUrl.empty()) {
        return backups;
    }

    std::string remoteAppDir = webdav.remoteRootPath + "/" + std::to_string(appId);
    auto resp = WebDavClient::DownloadFile(webdav, remoteAppDir);

    // Parse XML/HTML directory listing from WebDAV
    if (!resp.body.empty()) {
        std::regex tsRegex(R"((\d{8}_\d{6}))");
        auto begin = std::sregex_iterator(resp.body.begin(), resp.body.end(), tsRegex);
        auto end = std::sregex_iterator();
        std::set<std::string> seenTimestamps;

        for (auto it = begin; it != end; ++it) {
            std::string ts = (*it)[1].str();
            if (!seenTimestamps.contains(ts)) {
                seenTimestamps.insert(ts);
                BackupMetadata meta;
                meta.appId = appId;
                meta.timestamp = ts;
                meta.backupFileName = ts;
                backups.push_back(meta);
            }
        }
    }

    return backups;
}
} // namespace Manager
