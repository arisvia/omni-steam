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
                                                     const std::string& countryCode = "CN");
    static AppDetails GetAppDetails(uint32_t appId, const std::string& language = "schinese");
};

} // namespace Manager
