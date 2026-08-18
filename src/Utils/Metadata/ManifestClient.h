#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace ManifestClient {

struct ManifestDownloadResult {
    bool success = false;
    uint32_t depotId = 0;
    uint64_t manifestId = 0;
    std::string requestUrl;
    std::string errorMessage;
    std::vector<uint8_t> payload;
};

ManifestDownloadResult RequestManifest(uint32_t depotId, uint64_t manifestId, const std::string& upstreamEndpoint = "");
std::string QueryManifestIdByDepot(uint32_t depotId);

} // namespace ManifestClient
