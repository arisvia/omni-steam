#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace Metadata {

class DlcStore {
public:
    static void Initialize();
    static bool IsKnownDlc(uint32_t appId);
    static std::vector<uint32_t> GetAllKnownDlcs();
    // DLCs of the given base games only - keeps removed games' DLCs from
    // unlocking forever via stale cache entries.
    static std::vector<uint32_t> GetDlcsForBases(const std::set<uint32_t>& baseApps);
    static void RegisterDlcs(uint32_t baseAppId, const std::vector<uint32_t>& dlcIds);
    static void AsyncFetchAppDlcs(uint32_t baseAppId);
    static size_t Count();
    static void SaveCache();
};

} // namespace Metadata
