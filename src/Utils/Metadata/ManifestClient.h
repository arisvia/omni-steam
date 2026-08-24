#pragma once
#include <cstdint>
#include <string>
#include <vector>

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
bool FetchManifestRequestCode(uint64_t manifestGid, uint64_t* outCode);
uint64_t GetCachedRequestCode(uint64_t manifestGid);
bool IsNegativeCached(uint64_t manifestGid);
void StoreRequestCode(uint64_t manifestGid, uint64_t code);
void MarkRequestCodeFailed(uint64_t manifestGid);
void Shutdown();

} // namespace ManifestClient
