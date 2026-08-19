#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Manager {

class DepotKeyStore {
public:
    static void Initialize(const std::string& binFilePath = "depotkeys.bin");
    static std::string GetKeyForDepot(uint32_t depotId);
    static bool HasKey(uint32_t depotId);
    static size_t Count();
};

} // namespace Manager
