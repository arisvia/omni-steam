#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Manager {

struct SearchResultItem {
    uint32_t appId = 0;
    std::string name;
    std::string tinyImage;
    double price = 0.0;
    bool isFree = false;
};

struct DlcInfo {
    uint32_t dlcId = 0;
    std::string name;
};
struct AppDepotInfo {
    uint32_t depotId = 0;
    std::string name;
    bool isShared = false;
    bool hasManifests = false;
};

struct AppDetails {
    uint32_t appId = 0;
    std::string name;
    std::string type; // "game", "dlc", "demo"
    std::string headerImage;
    std::string description;
    std::vector<uint32_t> dlcAppIds;
    std::vector<DlcInfo> dlcList;
    bool isSuccess = false;
};

class SteamApi {
public:
    static std::vector<SearchResultItem> SearchStore(const std::string& query, const std::string& language = "schinese",
                                                     const std::string& countryCode = "US");
    static AppDetails GetAppDetails(uint32_t appId, const std::string& language = "schinese");
    static std::vector<AppDepotInfo> GetAppDepots(uint32_t appId);
};

} // namespace Manager
