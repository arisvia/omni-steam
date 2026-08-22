#include "ManifestClient.h"

#include <cstdint>
#include <spdlog/spdlog.h>
#include <string>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"

namespace ManifestClient {

namespace {
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
bool FetchManifestRequestCode(uint64_t manifestGid, uint64_t* outCode) {
    if (!outCode || manifestGid == 0)
        return false;

    *outCode = 0;
    std::string gidStr = std::to_string(manifestGid);

    // 1. Try primary OpenSteamTool manifest endpoint
    std::string url1 = std::string(OmniEndpoints::Manifest::kOpenSteamToolUrl) + "/" + gidStr;
    auto resp1 = OmniPlatform::Http::Get(url1, 6000);
    if (resp1.statusCode == 200 && !resp1.body.empty()) {
        try {
            *outCode = std::stoull(resp1.body);
            if (*outCode != 0) {
                spdlog::info("ManifestClient: Resolved manifest request code {} for GID {} via OpenSteamTool", *outCode,
                             manifestGid);
                return true;
            }
        } catch (...) {
        }
    }

    // 2. Try WuDrm endpoint
    std::string url2 = std::string(OmniEndpoints::Manifest::kWuDrmUrl) + "/" + gidStr;
    auto resp2 = OmniPlatform::Http::Get(url2, 6000);
    if (resp2.statusCode == 200 && !resp2.body.empty()) {
        try {
            *outCode = std::stoull(resp2.body);
            if (*outCode != 0) {
                spdlog::info("ManifestClient: Resolved manifest request code {} for GID {} via WuDrm", *outCode,
                             manifestGid);
                return true;
            }
        } catch (...) {
        }
    }

    // 3. Try SteamRun endpoint (JSON format: {"content":"..."})
    std::string url3 = std::string(OmniEndpoints::Manifest::kSteamRunUrl) + "/" + gidStr;
    auto resp3 = OmniPlatform::Http::Get(url3, 6000);
    if (resp3.statusCode == 200 && !resp3.body.empty()) {
        size_t pos = resp3.body.find("\"content\"");
        if (pos != std::string::npos) {
            size_t q1 = resp3.body.find('"', pos + 9);
            size_t q2 = (q1 != std::string::npos) ? resp3.body.find('"', q1 + 1) : std::string::npos;
            if (q1 != std::string::npos && q2 != std::string::npos) {
                try {
                    *outCode = std::stoull(resp3.body.substr(q1 + 1, q2 - q1 - 1));
                    if (*outCode != 0) {
                        spdlog::info("ManifestClient: Resolved manifest request code {} for GID {} via SteamRun",
                                     *outCode, manifestGid);
                        return true;
                    }
                } catch (...) {
                }
            }
        }
    }

    spdlog::warn("ManifestClient: Failed to resolve manifest request code for GID {} across all upstreams",
                 manifestGid);
    return false;
}

} // namespace ManifestClient
