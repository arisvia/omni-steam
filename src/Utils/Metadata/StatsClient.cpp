#include "StatsClient.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"

namespace fs = std::filesystem;

namespace StatsClient {

namespace {

constexpr size_t kMaxCacheEntries = 16384;
constexpr auto kFailureTtl = std::chrono::minutes(10);

struct FailureRecord {
    std::chrono::steady_clock::time_point at;
};

std::mutex g_mutex;
std::unordered_map<uint32_t, uint64_t> g_cache;
std::unordered_map<uint32_t, FailureRecord> g_failures;
bool g_diskLoaded = false;

std::string DiskCachePath() {
    return (fs::path(OmniPlatform::Paths::GetCacheDirectory()) / "stats_steamids.bin").generic_string();
}

// File layout: [magic u32][version u32][count u32]{appId u32, steamId u64}*
// Appends extend the record stream beyond the declared count; readers accept
// trailing records so appended entries are visible without consolidation.
constexpr uint32_t kDiskMagic = 0x53535449; // "ITSS"
constexpr uint32_t kDiskVersion = 1;
constexpr size_t kMaxFileSizeBytes = kMaxCacheEntries * 12 + 12;

void LoadDiskCacheLocked() {
    if (g_diskLoaded)
        return;
    g_diskLoaded = true;

    std::ifstream in(DiskCachePath(), std::ios::binary);
    if (!in)
        return;

    uint32_t magic = 0, version = 0, count = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (magic != kDiskMagic || version != kDiskVersion)
        return;

    while (in.good() && g_cache.size() < kMaxCacheEntries) {
        uint32_t appId = 0;
        uint64_t steamId = 0;
        in.read(reinterpret_cast<char*>(&appId), sizeof(appId));
        in.read(reinterpret_cast<char*>(&steamId), sizeof(steamId));
        if (!in.good())
            break;
        if (appId != 0 && steamId != 0) {
            g_cache.emplace(appId, steamId);
        }
    }
}

void AppendRecord(uint32_t appId, uint64_t steamId) {
    std::string path = DiskCachePath();
    std::error_code ec;
    bool needsRewrite = !fs::exists(path, ec) || fs::file_size(path, ec) > kMaxFileSizeBytes;

    if (needsRewrite) {
        std::vector<std::pair<uint32_t, uint64_t>> entries;
        for (const auto& [id, sid] : g_cache) {
            entries.emplace_back(id, sid);
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        uint32_t count = static_cast<uint32_t>(entries.size());
        out.write(reinterpret_cast<const char*>(&kDiskMagic), sizeof(kDiskMagic));
        out.write(reinterpret_cast<const char*>(&kDiskVersion), sizeof(kDiskVersion));
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto& [id, sid] : entries) {
            out.write(reinterpret_cast<const char*>(&id), sizeof(id));
            out.write(reinterpret_cast<const char*>(&sid), sizeof(sid));
        }
        return;
    }

    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out)
        return;
    out.write(reinterpret_cast<const char*>(&appId), sizeof(appId));
    out.write(reinterpret_cast<const char*>(&steamId), sizeof(steamId));
}

} // namespace

uint64_t GetCachedSteamId(uint32_t appId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LoadDiskCacheLocked();
    auto it = g_cache.find(appId);
    return it != g_cache.end() ? it->second : 0;
}

void StoreSteamId(uint32_t appId, uint64_t steamId) {
    if (appId == 0 || steamId == 0)
        return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_failures.erase(appId);
        g_cache[appId] = steamId;
    }
    try {
        fs::create_directories(fs::path(DiskCachePath()).parent_path());
        AppendRecord(appId, steamId);
    } catch (...) {
    }
}

bool GetDonorSteamId(uint32_t appId, uint64_t* outSteamId) {
    if (!outSteamId || appId == 0)
        return false;
    *outSteamId = 0;

    // Lua per-app override wins over everything.
    std::string luaOverride = LuaConfig::GetStatSteamId(appId);
    if (!luaOverride.empty()) {
        try {
            uint64_t parsed = std::stoull(luaOverride);
            if (parsed != 0) {
                *outSteamId = parsed;
                return true;
            }
        } catch (...) {
        }
    }

    if (!Config::IsStatsApiEnabled())
        return false;

    uint64_t cached = GetCachedSteamId(appId);
    if (cached != 0) {
        *outSteamId = cached;
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_failures.find(appId);
        if (it != g_failures.end() && std::chrono::steady_clock::now() - it->second.at < kFailureTtl) {
            return false;
        }
    }

    const std::string url = std::string(OmniEndpoints::Stats::kStatsBaseUrl) + "/" + std::to_string(appId);
    auto resp = OmniPlatform::Http::Get(url, 5000);

    uint64_t steamId = 0;
    if (resp.statusCode == 200 && !resp.body.empty()) {
        try {
            size_t consumed = 0;
            steamId = std::stoull(resp.body, &consumed);
            if (consumed != resp.body.size())
                steamId = 0;
        } catch (...) {
            steamId = 0;
        }
    }

    if (steamId != 0) {
        StoreSteamId(appId, steamId);
        *outSteamId = steamId;
        spdlog::info("StatsClient: Resolved donor SteamID {} for AppID {}", steamId, appId);
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_failures[appId] = {std::chrono::steady_clock::now()};
    }
    return false;
}

} // namespace StatsClient
