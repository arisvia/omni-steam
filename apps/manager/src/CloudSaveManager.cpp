#include "CloudSaveManager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <optional>
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

// MKCOL fails unless every parent exists, so create each path level in turn.
void EnsureRemoteDirs(const WebDavConfig& webdav, const std::string& appRemoteDir, const std::string& relativePath,
                      std::set<std::string>& attempted) {
    std::string accumulated = appRemoteDir;
    std::istringstream stream(relativePath);
    std::string component;
    while (std::getline(stream, component, '/')) {
        if (component.empty())
            continue;
        accumulated += "/" + component;
        if (attempted.insert(accumulated).second) {
            WebDavClient::MkCol(webdav, accumulated);
        }
    }
}

// Extracts child entry paths from a Depth-1 PROPFIND multistatus document.
std::vector<std::string> ParsePropFindHrefs(const std::string& xml) {
    static const std::regex hrefRe(R"(<(?:[A-Za-z0-9]+:)?href>\s*([^<]+?)\s*</(?:[A-Za-z0-9]+:)?href>)");
    std::vector<std::string> hrefs;
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), hrefRe); it != std::sregex_iterator(); ++it) {
        std::string href = OmniPlatform::Encoding::UrlDecode((*it)[1].str());
        // Trim the query part some servers append (%3Ftoken=...)
        size_t query = href.find('?');
        if (query != std::string::npos)
            href = href.substr(0, query);
        hrefs.push_back(href);
    }
    return hrefs;
}

// Reduces an absolute href to the path relative to remoteDir.
std::optional<std::string> RelativeTo(const std::string& href, const std::string& remoteDir) {
    auto contains = [](const std::string& hay, const std::string& needle) {
        return hay.find(needle) != std::string::npos;
    };
    std::string dir = remoteDir;
    if (!dir.empty() && dir.front() != '/')
        dir = "/" + dir;

    size_t pos = std::string::npos;
    if (contains(href, dir))
        pos = href.find(dir);
    else if (contains(OmniPlatform::Encoding::UrlDecode(href), dir)) {
        pos = OmniPlatform::Encoding::UrlDecode(href).find(dir);
    }
    if (pos == std::string::npos)
        return std::nullopt;

    std::string rest = href.substr(pos + dir.size());
    while (!rest.empty() && rest.front() == '/')
        rest.erase(rest.begin());
    return rest;
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
    std::set<std::string> attemptedDirs;

    for (const auto& loc : locations) {
        if (!loc.exists)
            continue;

        auto files = SavePathResolver::ScanSaveFiles(loc.path);
        for (const auto& f : files) {
            std::ifstream file(f, std::ios::binary);
            if (!file)
                continue;

            std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::string relativePath = fs::relative(f, loc.path).generic_string();
            std::string remoteTarget = appRemoteDir + "/" + timestamp + "/" + relativePath;

            EnsureRemoteDirs(webdav, appRemoteDir, timestamp + "/" + relativePath, attemptedDirs);

            auto res = WebDavClient::UploadFile(webdav, remoteTarget, buffer);
            if (res.isSuccess()) {
                uploadedFiles++;
                spdlog::debug("CloudSaveManager: Uploaded {}", remoteTarget);
            } else {
                spdlog::warn("CloudSaveManager: Upload failed {} ({})", remoteTarget,
                             res.error.empty() ? std::to_string(res.statusCode) : res.error);
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

        size_t restoredFiles = 0;
        // Recursive walk: backups preserve subdirectories (EnsureRemoteDirs on
        // upload), so restore must descend into collections instead of skipping
        // them - Depth:1 PROPFIND alone silently dropped nested saves.
        std::function<void(const std::string&, const std::string&, int)> restoreDir =
            [&](const std::string& remoteDir, const std::string& localParent, int depth) {
                if (depth > 8) {
                    spdlog::warn("CloudSaveManager: WebDAV nesting deeper than 8 levels at {}, skipping", remoteDir);
                    return;
                }
                auto listing = WebDavClient::PropFind(webdav, remoteDir);
                if (!listing.isSuccess()) {
                    spdlog::warn("CloudSaveManager: PROPFIND failed for {} (HTTP {})", remoteDir, listing.statusCode);
                    return;
                }
                for (const auto& href : ParsePropFindHrefs(listing.body)) {
                    std::string relative = RelativeTo(href, remoteDir).value_or("");
                    if (relative.empty())
                        continue;
                    if (relative.back() == '/') {
                        restoreDir(remoteDir + "/" + relative, (fs::path(localParent) / relative).generic_string(),
                                   depth + 1);
                        continue;
                    }
                    auto payload = WebDavClient::DownloadFile(webdav, remoteDir + "/" + relative);
                    if (!payload.isSuccess()) {
                        spdlog::warn("CloudSaveManager: Download failed for {}/{}", remoteDir, relative);
                        continue;
                    }
                    std::string destination = (fs::path(localParent) / fs::path(relative)).generic_string();
                    std::string parentDir = fs::path(destination).parent_path().string();
                    if (!parentDir.empty())
                        fs::create_directories(parentDir);
                    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
                    if (!out) {
                        spdlog::warn("CloudSaveManager: Cannot write {}", destination);
                        continue;
                    }
                    out.write(payload.body.data(), static_cast<std::streamsize>(payload.body.size()));
                    ++restoredFiles;
                }
            };
        restoreDir(remoteAppDir, localDir, 0);

        spdlog::info("CloudSaveManager: Restored {} file(s) from backup {} into {}", restoredFiles, latest.timestamp,
                     localDir);
        return restoredFiles > 0;
    } catch (const std::exception& e) {
        spdlog::error("CloudSaveManager: Restore exception: {}", e.what());
        return false;
    }
}

std::vector<BackupMetadata> CloudSaveManager::ListRemoteBackups(uint32_t appId, const WebDavConfig& webdav) {
    std::vector<BackupMetadata> backups;
    if (webdav.serverUrl.empty())
        return backups;

    std::string remoteAppDir = webdav.remoteRootPath + "/" + std::to_string(appId);
    auto resp = WebDavClient::PropFind(webdav, remoteAppDir);
    if (!resp.isSuccess() || resp.body.empty())
        return backups;

    static const std::regex tsRegex(R"((\d{8}_\d{6}))");
    std::set<std::string> seenTimestamps;
    for (const auto& href : ParsePropFindHrefs(resp.body)) {
        for (auto it = std::sregex_iterator(href.begin(), href.end(), tsRegex); it != std::sregex_iterator(); ++it) {
            std::string ts = (*it)[1].str();
            if (seenTimestamps.insert(ts).second) {
                BackupMetadata meta;
                meta.appId = appId;
                meta.timestamp = ts;
                meta.backupFileName = ts;
                backups.push_back(meta);
            }
        }
    }

    std::sort(backups.begin(), backups.end(),
              [](const BackupMetadata& a, const BackupMetadata& b) { return a.timestamp > b.timestamp; });
    return backups;
}
} // namespace Manager
