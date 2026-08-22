#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Metadata {

class DlcStore {
public:
    static void Initialize();
    static bool IsKnownDlc(uint32_t appId);
    static void RegisterDlcs(uint32_t baseAppId, const std::vector<uint32_t>& dlcIds);
    static void AsyncFetchAppDlcs(uint32_t baseAppId);
    static size_t Count();
    static void SaveCache();
};

} // namespace Metadata
