#include "ConfigManager.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string>
#include <toml++/toml.hpp>

#include "OmniPlatform/OmniPlatform.h"

namespace fs = std::filesystem;
namespace Manager {

std::string ConfigManager::GetConfigFilePath() {
#if defined(OMNI_PLATFORM_WINDOWS)
    return "omnisteam.toml";
    const char* home = std::getenv("HOME");
    std::string base = home ? std::string(home) + "/Library/Application Support/OmniSteam" : "/tmp/omnisteam";
    fs::create_directories(base);
    return base + "/omnisteam.toml";
#else
    const char* home = std::getenv("HOME");
    std::string base = home ? std::string(home) + "/.config/omnisteam" : "/tmp/omnisteam";
    fs::create_directories(base);
    return base + "/omnisteam.toml";
#endif
}

ConfigDto ConfigManager::ReadConfig() {
    ConfigDto dto;
    std::string path = GetConfigFilePath();
    if (!fs::exists(path)) {
        return dto; // Return default values
    }

    try {
        auto tbl = toml::parse_file(path);
        if (auto level = tbl["log"]["level"].value<std::string>())
            dto.logLevel = *level;
        if (auto url = tbl["manifest"]["url"].value<std::string>())
            dto.manifestUrl = *url;
        if (auto stats = tbl["stats"]["enable_api"].value<bool>())
            dto.statsEnableApi = *stats;
        if (auto cloud = tbl["cloud"]["enabled"].value<bool>())
            dto.cloudEnabled = *cloud;
        if (auto serverUrl = tbl["cloud"]["webdav_server_url"].value<std::string>())
            dto.webdavServerUrl = *serverUrl;
        if (auto username = tbl["cloud"]["webdav_username"].value<std::string>())
            dto.webdavUsername = *username;
        if (auto password = tbl["cloud"]["webdav_password"].value<std::string>())
            dto.webdavPassword = *password;
        if (auto remoteRoot = tbl["cloud"]["webdav_remote_root"].value<std::string>())
            dto.webdavRemoteRoot = *remoteRoot;
        if (auto paths = tbl["lua"]["paths"].as_array()) {
            for (const auto& p : *paths) {
                if (auto str = p.value<std::string>())
                    dto.luaPaths.push_back(*str);
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("ConfigManager: Error reading {}: {}", path, e.what());
    }

    return dto;
}

bool ConfigManager::SaveConfig(const ConfigDto& dto) {
    std::string path = GetConfigFilePath();
    try {
        std::string parentDir = fs::path(path).parent_path().string();
        if (!parentDir.empty())
            fs::create_directories(parentDir);

        std::ofstream out(path, std::ios::trunc);
        if (!out)
            return false;

        out << "# OmniSteam Configuration (Managed by OmniSteam Manager)\n\n";
        out << "[log]\nlevel = \"" << dto.logLevel << "\"\nfile_output = true\n\n";
        out << "[manifest]\nurl = \"" << dto.manifestUrl << "\"\n\n";
        out << "[stats]\nenable_api = " << (dto.statsEnableApi ? "true" : "false") << "\n\n";
        out << "[lua]\npaths = [\n";
        for (const auto& p : dto.luaPaths) {
            out << "    \"" << p << "\",\n";
        }
        out << "]\n\n";
        out << "[cloud]\nenabled = " << (dto.cloudEnabled ? "true" : "false") << "\n";
        out << "webdav_server_url = \"" << dto.webdavServerUrl << "\"\n";
        out << "webdav_username = \"" << dto.webdavUsername << "\"\n";
        out << "webdav_password = \"" << dto.webdavPassword << "\"\n";
        out << "webdav_remote_root = \"" << dto.webdavRemoteRoot << "\"\n";

        spdlog::info("ConfigManager: Config saved successfully to {}", path);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("ConfigManager: Save failed: {}", e.what());
        return false;
    }
}

bool ConfigManager::MigrateLuaFiles(const std::string& oldDir, const std::string& newDir) {
    try {
        if (!fs::exists(oldDir) || oldDir == newDir)
            return false;
        fs::create_directories(newDir);

        for (const auto& entry : fs::directory_iterator(oldDir)) {
            if (entry.is_regular_file() &&
                (entry.path().extension() == ".lua" || entry.path().extension() == ".disabled")) {
                std::string targetFile = newDir + "/" + entry.path().filename().string();
                fs::copy_file(entry.path(), targetFile, fs::copy_options::overwrite_existing);
                spdlog::info("ConfigManager: Migrated {} -> {}", entry.path().string(), targetFile);
            }
        }
        return true;
    } catch (const std::exception& e) {
        spdlog::error("ConfigManager: Migration error: {}", e.what());
        return false;
    }
}

} // namespace Manager
