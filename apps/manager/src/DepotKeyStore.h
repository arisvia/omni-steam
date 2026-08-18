#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

namespace Manager {

class DepotKeyStore {
public:
    static void Initialize(const std::string& jsonFilePath = "depotkeys.json");
    static std::string GetKeyForDepot(uint32_t depotId);
    static bool HasKey(uint32_t depotId);
    static size_t Count();
};

} // namespace Manager
