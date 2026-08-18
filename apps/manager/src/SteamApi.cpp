#include "SteamApi.h"
#include "OmniPlatform/OmniPlatform.h"
#include "OmniPlatform/OmniEndpoints.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <regex>

namespace Manager {

namespace {
    std::string UrlEncode(const std::string& value) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        for (char c : value) {
            if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else {
                escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            }
        }
        return escaped.str();
    }

    std::string ExtractJsonString(const std::string& json, const std::string& key) {
        std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch match;
        if (std::regex_search(json, match, re) && match.size() > 1) {
            return match[1].str();
        }
        return "";
    }
}

std::vector<SearchResultItem> SteamApi::SearchStore(const std::string& query, const std::string& language, const std::string& countryCode) {
    std::vector<SearchResultItem> results;
    if (query.empty()) return results;

    std::string url = std::string(OmniEndpoints::Steam::kStoreSearchApi) + "?term=" + UrlEncode(query) +
                      "&l=" + language + "&cc=" + countryCode;

    spdlog::info("SteamApi: Querying store search: {}", url);
    auto resp = OmniPlatform::Http::Get(url, 6000);
    if (resp.statusCode != 200 || resp.body.empty()) {
        spdlog::warn("SteamApi: Search query failed with status {}", resp.statusCode);
        return results;
    }

    // Fast lightweight extraction of "id" and "name" pairs
    std::regex itemRegex("\\{\\s*\"id\"\\s*:\\s*(\\d+)\\s*,\\s*\"name\"\\s*:\\s*\"([^\"]+)\"");
    auto words_begin = std::sregex_iterator(resp.body.begin(), resp.body.end(), itemRegex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        SearchResultItem item;
        item.appId = static_cast<uint32_t>(std::stoul(match[1].str()));
        item.name = match[2].str();
        item.tinyImage = std::string(OmniEndpoints::Steam::kImageCdnBase) + std::to_string(item.appId) + "/capsule_sm_120.jpg";
        results.push_back(item);
    }

    spdlog::info("SteamApi: Found {} matching games for '{}'", results.size(), query);
    return results;
}

AppDetails SteamApi::GetAppDetails(uint32_t appId, const std::string& language) {
    AppDetails details;
    details.appId = appId;

    std::string url = std::string(OmniEndpoints::Steam::kAppDetailsApi) + "?appids=" + std::to_string(appId) +
                      "&l=" + language;

    spdlog::info("SteamApi: Fetching app details: {}", url);
    auto resp = OmniPlatform::Http::Get(url, 6000);
    if (resp.statusCode != 200 || resp.body.empty()) {
        spdlog::warn("SteamApi: Failed to fetch app details for {}", appId);
        return details;
    }

    const std::string& body = resp.body;
    if (body.find("\"success\":true") == std::string::npos) {
        return details;
    }

    details.isSuccess = true;
    details.name = ExtractJsonString(body, "name");
    details.type = ExtractJsonString(body, "type");
    details.headerImage = ExtractJsonString(body, "header_image");
    details.description = ExtractJsonString(body, "short_description");

    // Extract DLC array if present: "dlc":[1234, 5678, ...]
    std::regex dlcArrayRegex("\"dlc\"\\s*:\\s*\\[([^\\]]+)\\]");
    std::smatch dlcMatch;
    if (std::regex_search(body, dlcMatch, dlcArrayRegex) && dlcMatch.size() > 1) {
        std::string dlcListStr = dlcMatch[1].str();
        std::regex idRegex("(\\d+)");
        auto dlc_begin = std::sregex_iterator(dlcListStr.begin(), dlcListStr.end(), idRegex);
        auto dlc_end = std::sregex_iterator();

        for (std::sregex_iterator it = dlc_begin; it != dlc_end; ++it) {
            uint32_t dlcId = static_cast<uint32_t>(std::stoul((*it)[1].str()));
            details.dlcAppIds.push_back(dlcId);
            details.dlcList.push_back(DlcInfo{ dlcId, "DLC " + std::to_string(dlcId) });
        }
    }

    spdlog::info("SteamApi: Parsed details for {} ({}): {} DLCs found", details.name, appId, details.dlcAppIds.size());
    return details;
}

} // namespace Manager
