#include "DepotKeyStore.h"
#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/OmniEndpoints.h"
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

namespace Manager {

namespace {
    std::mutex g_storeMutex;
    std::unordered_map<uint32_t, std::string> g_depotKeys;
    const char* kRemoteDepotKeysUrl = OmniEndpoints::GitHub::kDepotKeysJson;

    void ParseJsonContent(const std::string& content) {
        std::regex pairRegex("\"(\\d+)\"\\s*:\\s*\"([0-9a-fA-F]{64})\"");
        auto begin = std::sregex_iterator(content.begin(), content.end(), pairRegex);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            uint32_t depotId = static_cast<uint32_t>(std::stoul((*it)[1].str()));
            std::string keyHex = (*it)[2].str();
            g_depotKeys[depotId] = keyHex;
        }
    }
}

void DepotKeyStore::Initialize(const std::string& jsonFilePath) {
    std::lock_guard<std::mutex> lock(g_storeMutex);
    g_depotKeys.clear();
    g_initialized = true;

    std::vector<std::string> candidatePaths = {
        jsonFilePath,
        "depotkeys.json",
        "../depotkeys.json",
        "../../depotkeys.json",
        OmniPlatform::CredentialStore::GetStoragePath() + "/depotkeys.json"
    };

    std::string foundPath;
    for (const auto& p : candidatePaths) {
        if (fs::exists(p)) {
            foundPath = p;
            break;
        }
    }

    // 1. Try local file first
    if (!foundPath.empty()) {
        std::ifstream inFile(foundPath);
        if (inFile) {
            std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
            ParseJsonContent(content);
            spdlog::info("DepotKeyStore: Loaded {} depot keys from local file {}", g_depotKeys.size(), foundPath);
            return;
        }
    }

    // 2. Fallback: Automatically download from GitHub Raw repo
    spdlog::info("DepotKeyStore: Local depotkeys.json not found, fetching from {}", kRemoteDepotKeysUrl);
    auto resp = OmniPlatform::Http::Get(kRemoteDepotKeysUrl, 8000);
    if (resp.statusCode == 200 && !resp.body.empty()) {
        ParseJsonContent(resp.body);
        spdlog::info("DepotKeyStore: Successfully fetched and parsed {} depot keys from GitHub remote", g_depotKeys.size());
        
        // Cache to local storage directory for offline usage
        std::string cachePath = OmniPlatform::CredentialStore::GetStoragePath() + "/depotkeys.json";
        std::ofstream cacheOut(cachePath, std::ios::trunc);
        if (cacheOut) {
            cacheOut << resp.body;
            spdlog::info("DepotKeyStore: Cached remote depotkeys.json to {}", cachePath);
        }
    } else {
        spdlog::warn("DepotKeyStore: Could not fetch remote depot keys: {}", resp.error);
    }
}

std::string DepotKeyStore::GetKeyForDepot(uint32_t depotId) {
    std::lock_guard<std::mutex> lock(g_storeMutex);
    if (!g_initialized) {
        g_storeMutex.unlock();
        Initialize();
        g_storeMutex.lock();
    }
    auto it = g_depotKeys.find(depotId);
    return it != g_depotKeys.end() ? it->second : "";
}

bool DepotKeyStore::HasKey(uint32_t depotId) {
    return !GetKeyForDepot(depotId).empty();
}

size_t DepotKeyStore::Count() {
    std::lock_guard<std::mutex> lock(g_storeMutex);
    return g_depotKeys.size();
}

} // namespace Manager
