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

namespace {

std::string EscapeTomlString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
            case '\\':
                out.append("\\\\");
                break;
            case '"':
                out.append("\\\"");
                break;
            case '\n':
                out.append("\\n");
                break;
            case '\r':
                out.append("\\r");
                break;
            case '\t':
                out.append("\\t");
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out.append(buf);
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

} // namespace

std::string ConfigManager::GetConfigFilePath() {
    if (fs::exists("omnisteam.toml")) {
        return "omnisteam.toml";
    }
    if (fs::exists("config/omnisteam.toml")) {
        return "config/omnisteam.toml";
    }
    std::string configDir = OmniPlatform::Paths::GetConfigDirectory();
    try {
        fs::create_directories(configDir);
    } catch (...) {
    }
    return (fs::path(configDir) / "omnisteam.toml").generic_string();
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

        std::string tempPath = path + ".tmp";
        {
            std::ofstream out(tempPath, std::ios::trunc);
            if (!out)
                return false;

            out << "# OmniSteam Configuration (Managed by OmniSteam Manager)\n\n";
            out << "[log]\nlevel = \"" << EscapeTomlString(dto.logLevel) << "\"\nfile_output = true\n\n";
            out << "[manifest]\nurl = \"" << EscapeTomlString(dto.manifestUrl) << "\"\n\n";
            out << "[stats]\nenable_api = " << (dto.statsEnableApi ? "true" : "false") << "\n\n";
            out << "[lua]\npaths = [\n";
            for (const auto& p : dto.luaPaths) {
                out << "    \"" << EscapeTomlString(p) << "\",\n";
            }
            out << "]\n\n";
            out << "[cloud]\nenabled = " << (dto.cloudEnabled ? "true" : "false") << "\n";
            out << "webdav_server_url = \"" << EscapeTomlString(dto.webdavServerUrl) << "\"\n";
            out << "webdav_username = \"" << EscapeTomlString(dto.webdavUsername) << "\"\n";
            out << "webdav_password = \"" << EscapeTomlString(dto.webdavPassword) << "\"\n";
            out << "webdav_remote_root = \"" << EscapeTomlString(dto.webdavRemoteRoot) << "\"\n";
            out.flush();
        }

        std::error_code ec;
        fs::rename(tempPath, path, ec);
        if (ec) {
            fs::copy_file(tempPath, path, fs::copy_options::overwrite_existing, ec);
            if (ec)
                return false;
            fs::remove(tempPath, ec);
        }

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
