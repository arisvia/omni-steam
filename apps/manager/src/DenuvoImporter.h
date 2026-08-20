#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Manager {

struct DenuvoImportResult {
    bool success = false;
    uint32_t appId = 0;
    std::string gameName;
    size_t dlcCount = 0;
    size_t resolvedDepotKeysCount = 0;
    std::vector<uint32_t> missingDepots;
    std::string message;
};

class DenuvoImporter {
public:
    static DenuvoImportResult ImportFromPayload(const std::string& payload, const std::string& filename = "");
    static DenuvoImportResult ImportTickets(uint32_t appId, const std::string& appTicketHex,
                                            const std::string& eTicketHex = "");
};

} // namespace Manager
