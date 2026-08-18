#include "ManifestClient.h"
#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/OmniEndpoints.h"
#include "Utils/Config/Config.h"
#include "Utils/Config/LuaConfig.h"
#include <spdlog/spdlog.h>

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
}

ManifestDownloadResult RequestManifest(uint32_t depotId, uint64_t manifestId, const std::string& upstreamEndpoint) {
    ManifestDownloadResult res;
    res.depotId = depotId;
    res.manifestId = manifestId;

    std::string baseUrl = ResolveUpstreamBase(upstreamEndpoint);
    std::string requestUrl = baseUrl + "/manifest/" + std::to_string(depotId) + "/" + std::to_string(manifestId);
    res.requestUrl = requestUrl;

    spdlog::info("ManifestClient: Fetching manifest for depot {} (manifest_gid: {}) from {}", depotId, manifestId, requestUrl);

    auto httpResp = OmniPlatform::Http::Get(requestUrl, 8000);
    if (httpResp.statusCode == 200 && !httpResp.body.empty()) {
        res.success = true;
        res.payload.assign(httpResp.body.begin(), httpResp.body.end());
        spdlog::info("ManifestClient: Successfully downloaded manifest for depot {} ({} bytes)", depotId, res.payload.size());
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

} // namespace ManifestClient
