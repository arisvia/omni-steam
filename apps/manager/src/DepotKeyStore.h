#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace Manager {

class DepotKeyStore {
public:
    static void Initialize(const std::string& binFilePath = "depotkeys.bin");
    static std::string GetKeyForDepot(uint32_t depotId);
    static bool HasKey(uint32_t depotId);
    static size_t Count();
    static size_t GetTotalKeyCount() { return Count(); }
    static std::unordered_map<uint32_t, std::string> FindDepotKeysForApp(uint32_t appId,
                                                                         const std::vector<uint32_t>& dlcAppIds = {});
};
} // namespace Manager
