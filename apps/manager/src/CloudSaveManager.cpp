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

#include "OmniPlatform/OmniPaths.h"
#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

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

class AppLockGuard {
public:
    explicit AppLockGuard(uint32_t appId) : m_appId(appId) {
        if (appId == 0)
            return;
        std::string lockDir = (fs::path(OmniPlatform::Paths::GetCacheDirectory()) / "locks").generic_string();
        try {
            fs::create_directories(lockDir);
        } catch (...) {
        }
        std::string lockPath = (fs::path(lockDir) / ("cloud_" + std::to_string(appId) + ".lock")).generic_string();
#if defined(OMNI_PLATFORM_WINDOWS)
        m_handle = CreateFileA(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_handle != INVALID_HANDLE_VALUE) {
            m_acquired = true;
        }
#else
        m_fd = open(lockPath.c_str(), O_CREAT | O_RDWR, 0666);
        if (m_fd >= 0) {
            if (flock(m_fd, LOCK_EX | LOCK_NB) == 0) {
                m_acquired = true;
            } else {
                close(m_fd);
                m_fd = -1;
            }
        }
#endif
        if (m_acquired) {
            spdlog::debug("CloudSaveManager: AppLockGuard acquired for AppID {}", m_appId);
        }
    }
    ~AppLockGuard() {
#if defined(OMNI_PLATFORM_WINDOWS)
        if (m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
#else
        if (m_fd >= 0) {
            flock(m_fd, LOCK_UN);
            close(m_fd);
            m_fd = -1;
        }
#endif
    }

    AppLockGuard(const AppLockGuard&) = delete;
    AppLockGuard& operator=(const AppLockGuard&) = delete;
    AppLockGuard(AppLockGuard&&) = delete;
    AppLockGuard& operator=(AppLockGuard&&) = delete;

    bool IsAcquired() const { return m_acquired; }

private:
    uint32_t m_appId = 0;
    bool m_acquired = false;
#if defined(OMNI_PLATFORM_WINDOWS)
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    int m_fd = -1;
#endif
};

} // namespace
static std::string GetStatusFilePath() {
    return (fs::path(OmniPlatform::Paths::GetConfigDirectory()) / "cloud_status.json").generic_string();
}

static std::string UnescapeJson(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            char next = input[++i];
            switch (next) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                default:
                    out += next;
                    break;
            }
        } else {
            out += input[i];
        }
    }
    return out;
}

std::vector<CloudSyncStatus> CloudSaveManager::GetAllSyncStatuses() {
    std::vector<CloudSyncStatus> list;
    std::string path = GetStatusFilePath();
    if (!fs::exists(path)) {
        return list;
    }
    std::ifstream in(path);
    if (!in) {
        return list;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    static const std::regex objRe(
        R"raw(\{\s*"appId"\s*:\s*(\d+)\s*,\s*"action"\s*:\s*"((?:\\.|[^"\\])*)"\s*,\s*"status"\s*:\s*"((?:\\.|[^"\\])*)"\s*,\s*"timestamp"\s*:\s*"((?:\\.|[^"\\])*)"\s*,\s*"fileCount"\s*:\s*(\d+)\s*,\s*"totalBytes"\s*:\s*(\d+)\s*,\s*"message"\s*:\s*"((?:\\.|[^"\\])*)"\s*\})raw");
    for (auto it = std::sregex_iterator(content.begin(), content.end(), objRe); it != std::sregex_iterator(); ++it) {
        CloudSyncStatus s;
        s.appId = static_cast<uint32_t>(std::stoul((*it)[1].str()));
        s.action = UnescapeJson((*it)[2].str());
        s.status = UnescapeJson((*it)[3].str());
        s.timestamp = UnescapeJson((*it)[4].str());
        s.fileCount = static_cast<size_t>(std::stoull((*it)[5].str()));
        s.totalBytes = static_cast<size_t>(std::stoull((*it)[6].str()));
        s.message = UnescapeJson((*it)[7].str());
        list.push_back(s);
    }
    return list;
}
std::optional<CloudSyncStatus> CloudSaveManager::GetSyncStatus(uint32_t appId) {
    auto all = GetAllSyncStatuses();
    for (const auto& s : all) {
        if (s.appId == appId) {
            return s;
        }
    }
    return std::nullopt;
}

void CloudSaveManager::RecordSyncStatus(const CloudSyncStatus& status) {
    auto all = GetAllSyncStatuses();
    bool found = false;
    for (auto& s : all) {
        if (s.appId == status.appId) {
            s = status;
            found = true;
            break;
        }
    }
    if (!found) {
        all.push_back(status);
    }

    std::ostringstream json;
    json << "[\n";
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& s = all[i];
        json << "  {\n"
             << "    \"appId\": " << s.appId << ",\n"
             << "    \"action\": \"" << OmniPlatform::Encoding::EscapeJson(s.action) << "\",\n"
             << "    \"status\": \"" << OmniPlatform::Encoding::EscapeJson(s.status) << "\",\n"
             << "    \"timestamp\": \"" << OmniPlatform::Encoding::EscapeJson(s.timestamp) << "\",\n"
             << "    \"fileCount\": " << s.fileCount << ",\n"
             << "    \"totalBytes\": " << s.totalBytes << ",\n"
             << "    \"message\": \"" << OmniPlatform::Encoding::EscapeJson(s.message) << "\"\n"
             << "  }" << (i + 1 < all.size() ? "," : "") << "\n";
    }
    json << "]\n";

    try {
        std::string path = GetStatusFilePath();
        std::string dir = fs::path(path).parent_path().string();
        if (!dir.empty()) {
            fs::create_directories(dir);
        }
        std::string tempPath = path + ".tmp." + std::to_string(OmniPlatform::Process::GetCurrentProcessId());
        {
            std::ofstream out(tempPath, std::ios::trunc);
            if (!out)
                return;
            out << json.str();
        }
        std::error_code ec;
        fs::rename(tempPath, path, ec);
        if (ec) {
            fs::copy_file(tempPath, path, fs::copy_options::overwrite_existing, ec);
            fs::remove(tempPath, ec);
        }
    } catch (...) {
    }
}

bool CloudSaveManager::BackupAppSaves(uint32_t appId, const WebDavConfig& webdav) {
    AppLockGuard lock(appId);
    if (!lock.IsAcquired()) {
        spdlog::warn("CloudSaveManager: Backup already in progress for AppID {}, skipping concurrent attempt", appId);
        return false;
    }
    if (webdav.serverUrl.empty()) {
        spdlog::warn("CloudSaveManager: WebDAV server URL is empty");
        RecordSyncStatus({appId, "backup", "failed", GetCurrentTimestamp(), 0, 0, "WebDAV server URL is empty"});
        return false;
    }

    auto locations = SavePathResolver::LocateSaveDirectories(appId);
    if (locations.empty()) {
        spdlog::warn("CloudSaveManager: No save directories found for app {}", appId);
        RecordSyncStatus({appId, "backup", "failed", GetCurrentTimestamp(), 0, 0, "No save directories found"});
        return false;
    }

    RecordSyncStatus({appId, "backup", "in_progress", GetCurrentTimestamp(), 0, 0, "Backing up..."});

    // Ensure root remote directory exists
    WebDavClient::MkCol(webdav, webdav.remoteRootPath);
    std::string appRemoteDir = webdav.remoteRootPath + "/" + std::to_string(appId);
    WebDavClient::MkCol(webdav, appRemoteDir);

    std::string timestamp = GetCurrentTimestamp();
    size_t uploadedFiles = 0;
    size_t totalBytes = 0;
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
                totalBytes += buffer.size();
                spdlog::debug("CloudSaveManager: Uploaded {}", remoteTarget);
            } else {
                spdlog::warn("CloudSaveManager: Upload failed {} ({})", remoteTarget,
                             res.error.empty() ? std::to_string(res.statusCode) : res.error);
            }
        }
    }

    spdlog::info("CloudSaveManager: Backup finished for app {} ({} files uploaded, {} bytes)", appId, uploadedFiles,
                 totalBytes);
    bool ok = uploadedFiles > 0;
    RecordSyncStatus({appId, "backup", ok ? "success" : "failed", timestamp, uploadedFiles, totalBytes,
                      ok ? "Backup completed" : "Upload failed"});
    return ok;
}

bool CloudSaveManager::RestoreAppSaves(uint32_t appId, const WebDavConfig& webdav, const std::string& targetLocalDir) {
    AppLockGuard lock(appId);
    if (!lock.IsAcquired()) {
        spdlog::warn("CloudSaveManager: Restore already in progress for AppID {}, skipping concurrent attempt", appId);
        return false;
    }

    if (webdav.serverUrl.empty()) {
        spdlog::warn("CloudSaveManager: WebDAV server URL is empty");
        RecordSyncStatus({appId, "restore", "failed", GetCurrentTimestamp(), 0, 0, "WebDAV server URL is empty"});
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
        RecordSyncStatus({appId, "restore", "failed", GetCurrentTimestamp(), 0, 0, "Could not resolve save path"});
        return false;
    }

    RecordSyncStatus({appId, "restore", "in_progress", GetCurrentTimestamp(), 0, 0, "Restoring..."});
    spdlog::info("CloudSaveManager: Restoring saves for app {} from WebDAV into {}", appId, localDir);
    try {
        fs::create_directories(localDir);
        auto backups = ListRemoteBackups(appId, webdav);
        if (backups.empty()) {
            spdlog::warn("CloudSaveManager: No remote backups found on WebDAV for app {}", appId);
            RecordSyncStatus({appId, "restore", "failed", GetCurrentTimestamp(), 0, 0, "No remote backups found"});
            return false;
        }

        const auto& latest = backups.front();
        std::string remoteAppDir = webdav.remoteRootPath + "/" + std::to_string(appId) + "/" + latest.timestamp;

        size_t restoredFiles = 0;
        size_t totalBytes = 0;
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
                    totalBytes += payload.body.size();
                    ++restoredFiles;
                }
            };
        restoreDir(remoteAppDir, localDir, 0);

        spdlog::info("CloudSaveManager: Restored {} file(s) from backup {} into {}", restoredFiles, latest.timestamp,
                     localDir);
        bool ok = restoredFiles > 0;
        RecordSyncStatus({appId, "restore", ok ? "success" : "failed", latest.timestamp, restoredFiles, totalBytes,
                          ok ? "Restore completed" : "Restore failed"});
        return ok;
    } catch (const std::exception& e) {
        spdlog::error("CloudSaveManager: Restore exception: {}", e.what());
        RecordSyncStatus({appId, "restore", "failed", GetCurrentTimestamp(), 0, 0, e.what()});
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
