#include "DepotKeyStore.h"
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
    bool g_initialized = false;
}

void DepotKeyStore::Initialize(const std::string& jsonFilePath) {
    std::lock_guard<std::mutex> lock(g_storeMutex);
    g_depotKeys.clear();
    g_initialized = true;

    std::vector<std::string> candidatePaths = {
        jsonFilePath,
        "depotkeys.json",
        "../depotkeys.json",
        "../../depotkeys.json"
    };

    std::string foundPath;
    for (const auto& p : candidatePaths) {
        if (fs::exists(p)) {
            foundPath = p;
            break;
        }
    }

    if (foundPath.empty()) {
        spdlog::warn("DepotKeyStore: depotkeys.json not found in candidate paths");
        return;
    }

    std::ifstream inFile(foundPath);
    if (!inFile) return;

    std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    // Regex parsing for JSON key-value pairs: "12345": "aabbcc..."
    std::regex pairRegex("\"(\\d+)\"\\s*:\\s*\"([0-9a-fA-F]{64})\"");
    auto begin = std::sregex_iterator(content.begin(), content.end(), pairRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        uint32_t depotId = static_cast<uint32_t>(std::stoul((*it)[1].str()));
        std::string keyHex = (*it)[2].str();
        g_depotKeys[depotId] = keyHex;
    }

    spdlog::info("DepotKeyStore: Successfully loaded {} depot keys from {}", g_depotKeys.size(), foundPath);
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
