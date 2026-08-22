#include "DlcStore.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/SteamTypes.h"

namespace fs = std::filesystem;

namespace Metadata {

namespace {
std::mutex g_dlcMutex;
std::unordered_set<uint32_t> g_knownDlcs;
std::unordered_map<uint32_t, std::vector<uint32_t>> g_baseAppToDlcs;
std::unordered_set<uint32_t> g_pendingFetches;
bool g_initialized = false;

std::string GetDlcCachePath() {
    std::string cacheDir = OmniPlatform::Paths::GetCacheDirectory();
    try {
        if (!fs::exists(cacheDir)) {
            fs::create_directories(cacheDir);
        }
    } catch (...) {
    }
    return (fs::path(cacheDir) / "dlc_cache.bin").generic_string();
}
} // namespace

void DlcStore::Initialize() {
    std::lock_guard<std::mutex> lock(g_dlcMutex);
    if (g_initialized)
        return;
    g_initialized = true;

    std::string cachePath = GetDlcCachePath();
    if (!fs::exists(cachePath)) {
        return;
    }

    try {
        std::ifstream in(cachePath, std::ios::binary);
        if (!in)
            return;

        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t entryCount = 0;

        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        in.read(reinterpret_cast<char*>(&entryCount), sizeof(entryCount));

        if (magic != kSteamDlcCacheMagic || version != kSteamDlcCacheVersion) {
            spdlog::warn("DlcStore: Cache file format mismatch or outdated (magic=0x{:X})", magic);
            return;
        }

        for (uint32_t i = 0; i < entryCount && in.good(); ++i) {
            uint32_t baseAppId = 0;
            uint32_t dlcCount = 0;
            in.read(reinterpret_cast<char*>(&baseAppId), sizeof(baseAppId));
            in.read(reinterpret_cast<char*>(&dlcCount), sizeof(dlcCount));

            if (dlcCount > 10000)
                break; // Boundary sanity check

            std::vector<uint32_t> dlcs(dlcCount);
            if (dlcCount > 0) {
                in.read(reinterpret_cast<char*>(dlcs.data()), sizeof(uint32_t) * dlcCount);
                for (uint32_t dlcId : dlcs) {
                    g_knownDlcs.insert(dlcId);
                }
            }
            g_baseAppToDlcs[baseAppId] = std::move(dlcs);
        }

        spdlog::info("DlcStore: Initialized with {} known DLCs across {} base games", g_knownDlcs.size(),
                     g_baseAppToDlcs.size());
    } catch (const std::exception& e) {
        spdlog::warn("DlcStore: Exception loading cache: {}", e.what());
    }
}

bool DlcStore::IsKnownDlc(uint32_t appId) {
    if (appId == 0)
        return false;

    std::lock_guard<std::mutex> lock(g_dlcMutex);
    return g_knownDlcs.contains(appId);
}

void DlcStore::RegisterDlcs(uint32_t baseAppId, const std::vector<uint32_t>& dlcIds) {
    if (baseAppId == 0 || dlcIds.empty())
        return;

    {
        std::lock_guard<std::mutex> lock(g_dlcMutex);
        auto& list = g_baseAppToDlcs[baseAppId];
        for (uint32_t dlcId : dlcIds) {
            if (std::find(list.begin(), list.end(), dlcId) == list.end()) {
                list.push_back(dlcId);
            }
            g_knownDlcs.insert(dlcId);
        }
    }

    SaveCache();
}

size_t DlcStore::Count() {
    std::lock_guard<std::mutex> lock(g_dlcMutex);
    return g_knownDlcs.size();
}

void DlcStore::SaveCache() {
    std::string cachePath = GetDlcCachePath();
    try {
        std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
        if (!out)
            return;

        uint32_t magic = kSteamDlcCacheMagic;
        uint32_t version = kSteamDlcCacheVersion;
        uint32_t entryCount = 0;

        std::lock_guard<std::mutex> lock(g_dlcMutex);
        entryCount = static_cast<uint32_t>(g_baseAppToDlcs.size());

        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&entryCount), sizeof(entryCount));

        for (const auto& [baseId, dlcs] : g_baseAppToDlcs) {
            uint32_t baseAppId = baseId;
            uint32_t dlcCount = static_cast<uint32_t>(dlcs.size());
            out.write(reinterpret_cast<const char*>(&baseAppId), sizeof(baseAppId));
            out.write(reinterpret_cast<const char*>(&dlcCount), sizeof(dlcCount));
            if (dlcCount > 0) {
                out.write(reinterpret_cast<const char*>(dlcs.data()), sizeof(uint32_t) * dlcCount);
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("DlcStore: Failed to save cache: {}", e.what());
    }
}

void DlcStore::AsyncFetchAppDlcs(uint32_t baseAppId) {
    if (baseAppId == 0)
        return;

    {
        std::lock_guard<std::mutex> lock(g_dlcMutex);
        if (g_baseAppToDlcs.contains(baseAppId) || g_pendingFetches.contains(baseAppId)) {
            return;
        }
        g_pendingFetches.insert(baseAppId);
    }

    OmniPlatform::Thread::StartDetached([baseAppId]() {
        std::string url = std::string(OmniEndpoints::Steam::kAppDetailsApi) + "?appids=" + std::to_string(baseAppId) +
                          "&filters=basic,dlc";

        auto resp = OmniPlatform::Http::Get(url, 6000);
        std::vector<uint32_t> fetchedDlcs;

        if (resp.statusCode == 200 && !resp.body.empty()) {
            std::regex dlcArrayRegex(R"regex("dlc"\s*:\s*\[([^\]]*)\])regex");
            std::smatch match;
            if (std::regex_search(resp.body, match, dlcArrayRegex) && match.size() > 1) {
                std::string listStr = match[1].str();
                std::regex idRegex(R"regex((\d+))regex");
                auto words_begin = std::sregex_iterator(listStr.begin(), listStr.end(), idRegex);
                auto words_end = std::sregex_iterator();
                for (auto it = words_begin; it != words_end; ++it) {
                    try {
                        uint32_t id = static_cast<uint32_t>(std::stoul(it->str()));
                        if (id > 0) {
                            fetchedDlcs.push_back(id);
                        }
                    } catch (...) {
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_dlcMutex);
            g_pendingFetches.erase(baseAppId);
        }

        if (!fetchedDlcs.empty()) {
            spdlog::info("DlcStore: Dynamically discovered {} DLCs for AppID {}", fetchedDlcs.size(), baseAppId);
            RegisterDlcs(baseAppId, fetchedDlcs);
        }
    });
}

} // namespace Metadata
