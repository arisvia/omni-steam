#include "SteamApi.h"

#include <iomanip>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"

namespace Manager {

namespace {
std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}
} // namespace

std::vector<SearchResultItem> SteamApi::SearchStore(const std::string& query, const std::string& language,
                                                    const std::string& countryCode) {
    std::vector<SearchResultItem> results;
    if (query.empty())
        return results;

    // 1. Direct AppID lookup if query is purely numeric
    std::regex numericRegex("^\\d{3,10}$");
    if (std::regex_match(query, numericRegex)) {
        uint32_t directId = static_cast<uint32_t>(std::stoul(query));
        auto details = GetAppDetails(directId, language);
        if (!details.name.empty()) {
            SearchResultItem item;
            item.appId = directId;
            item.name = details.name;
            item.tinyImage =
                std::string(OmniEndpoints::Steam::kImageCdnBase) + std::to_string(directId) + "/capsule_sm_120.jpg";
            results.push_back(item);
            return results;
        }
    }

    // 2. Primary: Store Search API with US catalog
    std::string encodedQ = OmniPlatform::Encoding::UrlEncode(query);
    std::string url = std::string(OmniEndpoints::Steam::kStoreSearchApi) + "?term=" + encodedQ + "&l=" + language +
                      "&cc=" + countryCode;

    spdlog::info("SteamApi: Querying store search: {}", url);
    auto resp = OmniPlatform::Http::Get(url, 6000);
    if (resp.statusCode == 200 && !resp.body.empty()) {
        std::regex itemRegex("\\{\\s*\"id\"\\s*:\\s*(\\d+)\\s*,\\s*\"name\"\\s*:\\s*\"([^\"]+)\"");
        auto words_begin = std::sregex_iterator(resp.body.begin(), resp.body.end(), itemRegex);
        auto words_end = std::sregex_iterator();

        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch match = *i;
            SearchResultItem item;
            item.appId = static_cast<uint32_t>(std::stoul(match[1].str()));
            item.name = match[2].str();
            item.tinyImage =
                std::string(OmniEndpoints::Steam::kImageCdnBase) + std::to_string(item.appId) + "/capsule_sm_120.jpg";
            results.push_back(item);
        }
    }

    // 3. Fallback: Search Suggest API (effective for non-Latin / Chinese search terms)
    if (results.empty()) {
        std::string suggestUrl = "https://store.steampowered.com/search/suggest?term=" + encodedQ +
                                 "&f=games&cc=" + countryCode + "&l=" + language;
        spdlog::info("SteamApi: Fallback query suggest endpoint: {}", suggestUrl);
        auto suggestResp = OmniPlatform::Http::Get(suggestUrl, 6000);
        if (suggestResp.statusCode == 200 && !suggestResp.body.empty()) {
            std::regex suggestRegex("data-ds-appid=\"(\\d+)\"[^>]*>[\\s\\S]*?<div class=\"match_name\">([^<]+)</div>");
            auto s_begin = std::sregex_iterator(suggestResp.body.begin(), suggestResp.body.end(), suggestRegex);
            auto s_end = std::sregex_iterator();
            for (std::sregex_iterator it = s_begin; it != s_end; ++it) {
                std::smatch match = *it;
                SearchResultItem item;
                item.appId = static_cast<uint32_t>(std::stoul(match[1].str()));
                item.name = match[2].str();
                item.tinyImage = std::string(OmniEndpoints::Steam::kImageCdnBase) + std::to_string(item.appId) +
                                 "/capsule_sm_120.jpg";
                results.push_back(item);
            }
        }
    }

    spdlog::info("SteamApi: Found {} matching games for '{}'", results.size(), query);
    return results;
}
AppDetails SteamApi::GetAppDetails(uint32_t appId, const std::string& language) {
    AppDetails details;
    details.appId = appId;

    std::string url =
        std::string(OmniEndpoints::Steam::kAppDetailsApi) + "?appids=" + std::to_string(appId) + "&l=" + language;

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
            details.dlcList.push_back(DlcInfo{dlcId, "DLC " + std::to_string(dlcId)});
        }
    }

    spdlog::info("SteamApi: Parsed details for {} ({}): {} DLCs found", details.name, appId, details.dlcAppIds.size());
    return details;
}

} // namespace Manager
