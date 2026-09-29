#include "SteamApi.h"

#include <cstdint>
#include <iomanip>
#include <regex>
#include <set>
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
    std::set<uint32_t> seenAppIds;
    if (query.empty())
        return results;

    // 1. Direct AppID lookup if query is purely numeric
    std::regex numericRegex("^\\d{3,10}$");
    if (std::regex_match(query, numericRegex)) {
        try {
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
        } catch (...) {
        }
    }

    std::string encodedQ = OmniPlatform::Encoding::UrlEncode(query);
    std::string effectiveCc = countryCode.empty() ? "US" : countryCode;

    // 2. Primary: Store Search API
    std::string url = std::string(OmniEndpoints::Steam::kStoreSearchApi) + "?term=" + encodedQ + "&l=" + language +
                      "&cc=" + effectiveCc;

    spdlog::info("SteamApi: Querying store search: {}", url);
    auto resp = OmniPlatform::Http::Get(url, 8000);
    if (resp.statusCode == 200 && !resp.body.empty()) {
        const std::string& body = resp.body;
        size_t searchPos = 0;
        while ((searchPos = body.find("\"id\":", searchPos)) != std::string::npos) {
            size_t idStart = searchPos + 5;
            while (idStart < body.size() && (body[idStart] == ' ' || body[idStart] == '\t'))
                idStart++;
            size_t idEnd = idStart;
            while (idEnd < body.size() && std::isdigit(static_cast<unsigned char>(body[idEnd])))
                idEnd++;

            if (idEnd > idStart) {
                try {
                    uint32_t appId = static_cast<uint32_t>(std::stoul(body.substr(idStart, idEnd - idStart)));
                    if (!seenAppIds.contains(appId)) {
                        // Extract the containing item JSON object by balancing braces
                        size_t blockStart = body.rfind('{', searchPos);
                        size_t blockEnd = std::string::npos;
                        if (blockStart != std::string::npos) {
                            int depth = 0;
                            for (size_t i = blockStart; i < body.size(); ++i) {
                                if (body[i] == '{') {
                                    depth++;
                                } else if (body[i] == '}') {
                                    depth--;
                                    if (depth == 0) {
                                        blockEnd = i;
                                        break;
                                    }
                                }
                            }
                        }
                        std::string block =
                            (blockStart != std::string::npos && blockEnd != std::string::npos && blockEnd > blockStart)
                                ? body.substr(blockStart, blockEnd - blockStart + 1)
                                : "";
                        static const std::regex kNameRegex(R"regex("name"\s*:\s*"([^"]+)")regex");
                        std::smatch nameMatch;
                        if (std::regex_search(block, nameMatch, kNameRegex)) {
                            seenAppIds.insert(appId);
                            SearchResultItem item;
                            item.appId = appId;
                            item.name = nameMatch[1].str();
                            item.tinyImage = std::string(OmniEndpoints::Steam::kImageCdnBase) + std::to_string(appId) +
                                             "/capsule_sm_120.jpg";
                            results.push_back(item);
                        }
                    }
                } catch (...) {
                }
            }
            searchPos = idEnd;
        }
    }
    // 3. Fallback: Search Suggest API (effective for Chinese / non-Latin terms)
    if (results.empty()) {
        std::string suggestUrl = std::string(OmniEndpoints::Steam::kStoreSuggestApi) + "?term=" + encodedQ +
                                 "&f=games&cc=" + effectiveCc + "&l=" + language;
        auto suggestResp = OmniPlatform::Http::Get(suggestUrl, 6000);
        if (suggestResp.statusCode == 200 && !suggestResp.body.empty()) {
            static const std::regex kSuggestRegex(
                R"regex(data-ds-appid="(\d+)"[^>]*>[\s\S]*?<div class="match_name">([^<]+)</div>)regex");
            auto s_begin = std::sregex_iterator(suggestResp.body.begin(), suggestResp.body.end(), kSuggestRegex);
            auto s_end = std::sregex_iterator();
            for (auto it = s_begin; it != s_end; ++it) {
                std::smatch match = *it;
                try {
                    uint32_t appId = static_cast<uint32_t>(std::stoul(match[1].str()));
                    if (seenAppIds.contains(appId))
                        continue;
                    seenAppIds.insert(appId);

                    SearchResultItem item;
                    item.appId = appId;
                    item.name = match[2].str();
                    item.tinyImage = std::string(OmniEndpoints::Steam::kImageCdnBase) + std::to_string(appId) +
                                     "/capsule_sm_120.jpg";
                    results.push_back(item);
                } catch (...) {
                }
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
