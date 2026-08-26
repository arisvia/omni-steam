#include "ManifestClient.h"

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

namespace ManifestClient {

namespace {

constexpr size_t kMaxCacheEntries = 65536;
constexpr auto kNegativeTtl = std::chrono::minutes(10);

std::mutex g_cacheMutex;
std::unordered_map<uint64_t, uint64_t> g_positiveCache;
std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> g_negativeCache;
bool g_diskLoaded = false;

std::string GetDiskCachePath() {
    return (fs::path(OmniPlatform::Paths::GetCacheDirectory()) / "manifest_request_codes.bin").generic_string();
}

void LoadDiskCacheLocked() {
    if (g_diskLoaded)
        return;
    g_diskLoaded = true;

    std::string path = GetDiskCachePath();
    std::error_code ec;
    if (!fs::exists(path, ec))
        return;

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return;

    uint32_t magic = 0, version = 0, count = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    constexpr uint32_t kMagic = 0x4D524351; // "QCRM"
    constexpr uint32_t kVersion = 1;
    if (magic != kMagic || version != kVersion)
        return;

    // Header count is advisory - appended entries beyond it must stay
    // visible, so drain the record stream until EOF.
    while (in.good() && g_positiveCache.size() < kMaxCacheEntries) {
        uint64_t gid = 0, code = 0;
        in.read(reinterpret_cast<char*>(&gid), sizeof(gid));
        in.read(reinterpret_cast<char*>(&code), sizeof(code));
        if (!in.good())
            break;
        if (gid != 0 && code != 0) {
            g_positiveCache.emplace(gid, code);
        }
    }
    spdlog::info("ManifestClient: Loaded {} cached manifest request codes", g_positiveCache.size());
}

void AppendDiskCache(uint64_t gid, uint64_t code) {
    try {
        std::string path = GetDiskCachePath();
        fs::create_directories(fs::path(path).parent_path());

        // Serialize probe + rewrite + append under g_cacheMutex so concurrent
        // callers can never interleave header rewrites with raw 16-byte appends.
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        bool rewrite = false;
        std::ifstream probe(path, std::ios::binary | std::ios::ate);
        if (probe) {
            std::streamsize size = probe.tellg();
            rewrite = (size <= 0) || (size > static_cast<std::streamsize>(kMaxCacheEntries) * 16 + 12);
        } else {
            rewrite = true;
        }

        if (rewrite) {
            std::vector<std::pair<uint64_t, uint64_t>> entries(g_positiveCache.begin(), g_positiveCache.end());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
                return;
            uint32_t magic = 0x4D524351, version = 1;
            uint32_t count = static_cast<uint32_t>(entries.size());
            out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            out.write(reinterpret_cast<const char*>(&version), sizeof(version));
            out.write(reinterpret_cast<const char*>(&count), sizeof(count));
            for (const auto& [g, c] : entries) {
                out.write(reinterpret_cast<const char*>(&g), sizeof(g));
                out.write(reinterpret_cast<const char*>(&c), sizeof(c));
            }
        } else {
            std::ofstream out(path, std::ios::binary | std::ios::app);
            if (!out)
                return;
            out.write(reinterpret_cast<const char*>(&gid), sizeof(gid));
            out.write(reinterpret_cast<const char*>(&code), sizeof(code));
        }
    } catch (...) {
    }
}

std::string ResolveUpstreamBase(const std::string& upstreamEndpoint) {
    if (!upstreamEndpoint.empty()) {
        return upstreamEndpoint;
    }

    std::string configured = Config::GetManifestApiUrl();
    if (configured == "opensteamtool") {
        return OmniEndpoints::Manifest::kOpenSteamToolUrl;
    } else if (configured == "steamrun") {
        return OmniEndpoints::Manifest::kSteamRunUrl;
    } else if (configured == "wudrm") {
        return OmniEndpoints::Manifest::kWuDrmUrl;
    } else if (!configured.empty()) {
        return configured;
    }
    return OmniEndpoints::Manifest::kOpenSteamToolUrl;
}

bool ParsePlainUint(const std::string& body, uint64_t* out) {
    if (body.empty())
        return false;
    try {
        size_t consumed = 0;
        uint64_t value = std::stoull(body, &consumed);
        if (value == 0)
            return false;
        *out = value;
        return true;
    } catch (...) {
        return false;
    }
}

struct UpstreamResult {
    bool success = false;
    uint64_t code = 0;
};

UpstreamResult QueryOpenSteamTool(const std::string& gidStr) {
    std::string url = std::string(OmniEndpoints::Manifest::kOpenSteamToolUrl) + "/" + gidStr;
    auto resp = OmniPlatform::Http::Get(url, 6000);
    UpstreamResult r;
    if (resp.statusCode == 200 && !resp.body.empty())
        r.success = ParsePlainUint(resp.body, &r.code);
    return r;
}

UpstreamResult QueryWuDrm(const std::string& gidStr) {
    std::string url = std::string(OmniEndpoints::Manifest::kWuDrmUrl) + "/" + gidStr;
    auto resp = OmniPlatform::Http::Get(url, 6000);
    UpstreamResult r;
    if (resp.statusCode == 200 && !resp.body.empty())
        r.success = ParsePlainUint(resp.body, &r.code);
    return r;
}

UpstreamResult QuerySteamRun(const std::string& gidStr) {
    std::string url = std::string(OmniEndpoints::Manifest::kSteamRunUrl) + "/" + gidStr;
    auto resp = OmniPlatform::Http::Get(url, 6000);
    UpstreamResult r;
    if (resp.statusCode != 200 || resp.body.empty())
        return r;
    size_t pos = resp.body.find("\"content\"");
    if (pos == std::string::npos)
        return r;
    size_t q1 = resp.body.find('"', pos + 9);
    if (q1 == std::string::npos)
        return r;
    size_t q2 = resp.body.find('"', q1 + 1);
    if (q2 == std::string::npos)
        return r;
    r.success = ParsePlainUint(resp.body.substr(q1 + 1, q2 - q1 - 1), &r.code);
    return r;
}

} // namespace

ManifestDownloadResult RequestManifest(uint32_t depotId, uint64_t manifestId, const std::string& upstreamEndpoint) {
    ManifestDownloadResult res;
    res.depotId = depotId;
    res.manifestId = manifestId;

    std::string baseUrl = ResolveUpstreamBase(upstreamEndpoint);
    std::string requestUrl = baseUrl + "/manifest/" + std::to_string(depotId) + "/" + std::to_string(manifestId);
    res.requestUrl = requestUrl;

    spdlog::info("ManifestClient: Fetching manifest for depot {} (manifest_gid: {}) from {}", depotId, manifestId,
                 requestUrl);

    auto httpResp = OmniPlatform::Http::Get(requestUrl, 8000);
    if (httpResp.statusCode == 200 && !httpResp.body.empty()) {
        res.success = true;
        res.payload.assign(httpResp.body.begin(), httpResp.body.end());
        spdlog::info("ManifestClient: Successfully downloaded manifest for depot {} ({} bytes)", depotId,
                     res.payload.size());
    } else {
        res.success = false;
        res.errorMessage = httpResp.error.empty() ? ("HTTP " + std::to_string(httpResp.statusCode)) : httpResp.error;
        spdlog::warn("ManifestClient: Failed to fetch manifest for depot {}: {}", depotId, res.errorMessage);
    }

    return res;
}

std::string QueryManifestIdByDepot(uint32_t depotId) {
    // 1. Check local Lua config override first
    std::string luaManifest = LuaConfig::GetManifestId(depotId);
    if (!luaManifest.empty()) {
        return luaManifest;
    }
    return "";
}

uint64_t GetCachedRequestCode(uint64_t manifestGid) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    LoadDiskCacheLocked();
    auto it = g_positiveCache.find(manifestGid);
    return it != g_positiveCache.end() ? it->second : 0;
}

bool IsNegativeCached(uint64_t manifestGid) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    auto it = g_negativeCache.find(manifestGid);
    if (it == g_negativeCache.end())
        return false;
    if (std::chrono::steady_clock::now() - it->second >= kNegativeTtl) {
        g_negativeCache.erase(it);
        return false;
    }
    return true;
}

void StoreRequestCode(uint64_t manifestGid, uint64_t code) {
    if (manifestGid == 0 || code == 0)
        return;
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_negativeCache.erase(manifestGid);
        g_positiveCache.emplace(manifestGid, code);
    }
    AppendDiskCache(manifestGid, code);
}

void MarkRequestCodeFailed(uint64_t manifestGid) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_negativeCache.emplace(manifestGid, std::chrono::steady_clock::now());
    if (g_negativeCache.size() > kMaxCacheEntries) {
        g_negativeCache.clear();
    }
}

bool FetchManifestRequestCode(uint64_t manifestGid, uint64_t* outCode) {
    if (!outCode || manifestGid == 0)
        return false;

    *outCode = 0;

    uint64_t cached = GetCachedRequestCode(manifestGid);
    if (cached != 0) {
        *outCode = cached;
        return true;
    }

    if (IsNegativeCached(manifestGid))
        return false;

    std::string gidStr = std::to_string(manifestGid);

    UpstreamResult r1 = QueryOpenSteamTool(gidStr);
    if (r1.success) {
        StoreRequestCode(manifestGid, r1.code);
        *outCode = r1.code;
        spdlog::info("ManifestClient: Resolved manifest request code {} for GID {} via OpenSteamTool", *outCode,
                     manifestGid);
        return true;
    }

    UpstreamResult r2 = QueryWuDrm(gidStr);
    if (r2.success) {
        StoreRequestCode(manifestGid, r2.code);
        *outCode = r2.code;
        spdlog::info("ManifestClient: Resolved manifest request code {} for GID {} via WuDrm", *outCode, manifestGid);
        return true;
    }

    UpstreamResult r3 = QuerySteamRun(gidStr);
    if (r3.success) {
        StoreRequestCode(manifestGid, r3.code);
        *outCode = r3.code;
        spdlog::info("ManifestClient: Resolved manifest request code {} for GID {} via SteamRun", *outCode,
                     manifestGid);
        return true;
    }

    MarkRequestCodeFailed(manifestGid);
    spdlog::warn("ManifestClient: Failed to resolve manifest request code for GID {} across all upstreams",
                 manifestGid);
    return false;
}

} // namespace ManifestClient
