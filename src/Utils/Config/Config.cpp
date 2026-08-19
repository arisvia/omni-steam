#include "Config.h"

#include <mutex>
#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

namespace {
std::mutex g_configMutex;
std::vector<std::string> g_luaPaths;
std::string g_manifestUrl = "https://manifest.opensteamtool.com";
bool g_statsEnabled = true;
} // namespace

namespace Config {

void Load(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(g_configMutex);
    try {
        auto tbl = toml::parse_file(configPath);

        g_luaPaths.clear();
        if (auto luaPaths = tbl["lua"]["paths"].as_array()) {
            for (const auto& el : *luaPaths) {
                if (auto str = el.value<std::string>()) {
                    g_luaPaths.push_back(*str);
                }
            }
        }

        if (auto manifestApi = tbl["manifest"]["url"].value<std::string>()) {
            g_manifestUrl = *manifestApi;
        }

        if (auto statsApi = tbl["stats"]["enable_api"].value<bool>()) {
            g_statsEnabled = *statsApi;
        }

        spdlog::info("OmniSteam Config loaded from: {}", configPath);
    } catch (const std::exception& ex) {
        spdlog::warn("Using default OmniSteam config: {}", ex.what());
    }
}

std::vector<std::string> GetLuaPaths() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_luaPaths;
}

std::string GetManifestApiUrl() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_manifestUrl;
}

bool IsStatsApiEnabled() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_statsEnabled;
}

} // namespace Config
