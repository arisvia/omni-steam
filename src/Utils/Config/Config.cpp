#include "Config.h"

#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"

namespace {
struct ConfigSnapshot {
    std::vector<std::string> luaPaths;
    // Stored as the preset token, never as a resolved URL: ManifestClient owns the
    // token -> provider expansion, so a user override and the built-in default follow the
    // same path and the dashboard can round-trip the value it read.
    std::string manifestUrl = OmniEndpoints::Manifest::kDefaultUpstream;
    bool statsEnabled = true;
    bool autoUnlockDlc = true;
    bool allowInsecureTls = false;
};

std::mutex g_configMutex;
ConfigSnapshot g_config;

ConfigSnapshot ParseConfig(const std::string& configPath) {
    const auto table = toml::parse_file(configPath);
    ConfigSnapshot result;

    if (const auto* luaPaths = table["lua"]["paths"].as_array()) {
        result.luaPaths.reserve(luaPaths->size());
        for (const auto& element : *luaPaths) {
            if (auto path = element.value<std::string>(); path && !path->empty()) {
                result.luaPaths.push_back(std::move(*path));
            }
        }
    }
    if (auto value = table["manifest"]["url"].value<std::string>(); value && !value->empty()) {
        result.manifestUrl = std::move(*value);
    }
    if (auto value = table["stats"]["enable_api"].value<bool>()) {
        result.statsEnabled = *value;
    }
    if (auto value = table["dlc"]["auto_unlock"].value<bool>()) {
        result.autoUnlockDlc = *value;
    }
    if (auto value = table["security"]["allow_insecure_tls"].value<bool>()) {
        result.allowInsecureTls = *value;
    }
    return result;
}
} // namespace

namespace Config {

void Load(const std::string& configPath) {
    try {
        ConfigSnapshot next = ParseConfig(configPath);
        const bool allowInsecureTls = next.allowInsecureTls;
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            g_config = std::move(next);
        }
        OmniPlatform::Http::SetAllowInsecureTls(allowInsecureTls);
        spdlog::info("OmniSteam Config loaded from: {}", configPath);
    } catch (const std::exception& ex) {
        spdlog::warn("Config reload failed; keeping the last valid configuration: {}", ex.what());
    }
}

std::vector<std::string> GetLuaPaths() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_config.luaPaths;
}

std::string GetManifestApiUrl() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_config.manifestUrl;
}

bool IsStatsApiEnabled() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_config.statsEnabled;
}

bool IsAutoUnlockDlcEnabled() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_config.autoUnlockDlc;
}
} // namespace Config
