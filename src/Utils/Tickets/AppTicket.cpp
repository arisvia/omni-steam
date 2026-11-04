#include "AppTicket.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"

namespace fs = std::filesystem;

namespace AppTicket {

static CacheTicketProviderFn g_cacheTicketProvider = nullptr;

void SetCacheTicketProvider(CacheTicketProviderFn fn) {
    g_cacheTicketProvider = fn;
}

uint64_t ExtractSteamIdFromTicket(const std::vector<uint8_t>& ticket) {
    // Valve AppOwnershipTicket binary format:
    // [uint32 Size][uint32 Version][uint64 SteamID][...]
    if (ticket.size() < kSteamIdTicketMinimumSize) {
        return 0;
    }
    uint64_t steamId = 0;
    std::memcpy(&steamId, ticket.data() + 8, sizeof(uint64_t));
    return steamId;
}

namespace {

// Splits a VDF line into up to two quoted/unquoted tokens.
bool VdfKeyValue(const std::string& line, std::string* key, std::string* value) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size() && tokens.size() < 2) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n'))
            ++i;
        if (i >= line.size())
            break;
        if (line[i] == '"') {
            size_t close = line.find('"', i + 1);
            if (close == std::string::npos)
                break;
            tokens.push_back(line.substr(i + 1, close - i - 1));
            i = close + 1;
        } else {
            size_t stop = line.find_first_of(" \t\r\n", i);
            if (stop == std::string::npos)
                stop = line.size();
            tokens.push_back(line.substr(i, stop - i));
            i = stop;
        }
    }
    if (tokens.size() < 2)
        return false;
    *key = tokens[0];
    *value = tokens[1];
    return true;
}

bool IsHexOnly(const std::string& s) {
    if (s.empty())
        return false;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

} // namespace

uint64_t ParseMostRecentSteamId64(const std::string& loginUsersVdf) {
    uint64_t mostRecent = 0;
    uint64_t last = 0;
    uint64_t currentId = 0;
    for (size_t pos = 0; pos < loginUsersVdf.size();) {
        size_t eol = loginUsersVdf.find('\n', pos);
        if (eol == std::string::npos)
            eol = loginUsersVdf.size();
        const std::string line = loginUsersVdf.substr(pos, eol - pos);
        pos = eol + 1;

        std::string key, value;
        if (VdfKeyValue(line, &key, &value)) {
            for (char& c : key) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (key == "mostrecent" && value == "1" && currentId != 0)
                mostRecent = currentId;
            continue;
        }
        // Account headers are standalone quoted SteamID64 lines.
        size_t begin = line.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        std::string token = line.substr(begin, end - begin + 1);
        if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
            token = token.substr(1, token.size() - 2);
        if (token.rfind("7656", 0) == 0 && token.size() == 17 && IsHexOnly(token)) {
            currentId = std::strtoull(token.c_str(), nullptr, 10);
            last = currentId;
        }
    }
    return mostRecent != 0 ? mostRecent : last;
}

std::vector<std::pair<uint32_t, std::string>> ParseAppTicketHexEntries(const std::string& localConfigVdf) {
    std::vector<std::pair<uint32_t, std::string>> entries;
    bool inAppTickets = false;
    int depth = 0;
    int appTicketsDepth = 0;
    std::string pendingKey;
    for (size_t pos = 0; pos < localConfigVdf.size();) {
        size_t eol = localConfigVdf.find('\n', pos);
        if (eol == std::string::npos)
            eol = localConfigVdf.size();
        const std::string line = localConfigVdf.substr(pos, eol - pos);
        pos = eol + 1;

        const size_t openBrace = line.find('{');
        if (openBrace != std::string::npos) {
            // VDF writes the block key either on the brace line or alone on
            // the previous line; accept both.
            std::string key, value;
            bool inlineKey = false;
            if (VdfKeyValue(line.substr(0, openBrace) + " \"x\"", &key, &value)) {
                for (char& c : key) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                inlineKey = (key == "apptickets");
            }
            if (!inAppTickets && (inlineKey || pendingKey == "apptickets")) {
                inAppTickets = true;
                appTicketsDepth = depth + 1;
            }
            pendingKey.clear();
            ++depth;
            continue;
        }
        if (line.find('}') != std::string::npos) {
            if (inAppTickets && depth == appTicketsDepth)
                inAppTickets = false;
            if (depth > 0)
                --depth;
            pendingKey.clear();
            continue;
        }
        std::string key, value;
        if (VdfKeyValue(line, &key, &value)) {
            pendingKey.clear();
            if (!inAppTickets || depth != appTicketsDepth)
                continue;
            if (!IsHexOnly(key) || key.size() > 9 || !IsHexOnly(value))
                continue;
            const uint64_t appId64 = std::strtoull(key.c_str(), nullptr, 10);
            if (appId64 == 0 || appId64 > 0xFFFFFFFFull)
                continue;
            entries.emplace_back(static_cast<uint32_t>(appId64), value);
            continue;
        }
        // Standalone quoted token: a block key waiting for its brace line.
        size_t begin = line.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            pendingKey.clear();
            continue;
        }
        size_t end = line.find_last_not_of(" \t\r\n");
        std::string token = line.substr(begin, end - begin + 1);
        if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
            token = token.substr(1, token.size() - 2);
        for (char& c : token) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        pendingKey = token;
    }
    return entries;
}

namespace {

// SteamID64 of the account currently logged in, per loginusers.vdf.
uint64_t GetActiveSteamId64() {
    const std::string steamRoot = OmniPlatform::Paths::GetSteamInstallPath();
    if (steamRoot.empty())
        return 0;
    std::ifstream in((fs::path(steamRoot) / "config" / "loginusers.vdf").generic_string(), std::ios::binary);
    if (!in)
        return 0;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ParseMostRecentSteamId64(content);
}

// Ordered forge-base candidates. A forged ticket inherits its base's Valve
// signature AND its SteamID, and Denuvo cross-checks the latter against
// GetSteamID - so any base not issued to the logged-in account (family-shared
// games carry the lender's SteamID) would guarantee Error 54.
// Order: the legacy CEG constant first, then the user's own cached tickets in
// userdata localconfig.vdf, which newer clients populate even where the
// legacy appid-7 ticket no longer exists.
std::vector<uint8_t> PickForgeBase(uint32_t* outBaseAppId) {
    if (outBaseAppId)
        *outBaseAppId = 0;
    const uint64_t activeId = GetActiveSteamId64();
    const uint32_t activeAccount = static_cast<uint32_t>(activeId & 0xFFFFFFFFull);

    auto acceptable = [&](const std::vector<uint8_t>& bytes, uint32_t sourceAppId) -> bool {
        if (bytes.size() <= kAppTicketSignatureSize)
            return false;
        const uint64_t ticketId = ExtractSteamIdFromTicket(bytes);
        if (activeAccount != 0 && ticketId != 0 && static_cast<uint32_t>(ticketId & 0xFFFFFFFFull) != activeAccount) {
            spdlog::debug("AppTicket: skipping forge base appid {} (SteamID account {} != active {})", sourceAppId,
                          static_cast<uint32_t>(ticketId & 0xFFFFFFFFull), activeAccount);
            return false;
        }
        return true;
    };

    if (g_cacheTicketProvider) {
        std::vector<uint8_t> legacy = g_cacheTicketProvider(kLocalAppTicketSourceAppId);
        if (acceptable(legacy, kLocalAppTicketSourceAppId)) {
            if (outBaseAppId)
                *outBaseAppId = kLocalAppTicketSourceAppId;
            return legacy;
        }
    }

    const std::string steamRoot = OmniPlatform::Paths::GetSteamInstallPath();
    if (!steamRoot.empty() && activeAccount != 0) {
        // userdata folders are named by 32-bit AccountID, not SteamID64.
        std::ifstream in(
            (fs::path(steamRoot) / "userdata" / std::to_string(activeAccount) / "config" / "localconfig.vdf")
                .generic_string(),
            std::ios::binary);
        if (in) {
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            for (const auto& entry : ParseAppTicketHexEntries(content)) {
                if (entry.first == kLocalAppTicketSourceAppId)
                    continue; // already probed above
                std::vector<uint8_t> bytes = OmniPlatform::Encoding::HexToBytes(entry.second);
                if (acceptable(bytes, entry.first)) {
                    spdlog::info("AppTicket: using localconfig appticket {} ({} bytes) as forge base", entry.first,
                                 bytes.size());
                    if (outBaseAppId)
                        *outBaseAppId = entry.first;
                    return bytes;
                }
            }
        }
    }
    return {};
}

} // namespace

uint64_t GetSpoofSteamID(uint32_t appId) {
    if (!LuaConfig::HasApp(appId) && !LuaConfig::HasDepot(appId)) {
        return 0;
    }
    std::string ticketHex = OmniPlatform::CredentialStore::ReadTicket(appId, "AppTicket");
    if (ticketHex.empty()) {
        ticketHex = OmniPlatform::CredentialStore::ReadTicket(appId, "ETicket");
    }
    if (ticketHex.empty()) {
        return 0;
    }
    auto rawTicket = OmniPlatform::Encoding::HexToBytes(ticketHex);
    uint64_t steamId = ExtractSteamIdFromTicket(rawTicket);
    if (steamId != 0) {
        spdlog::debug("AppTicket: GetSpoofSteamID for AppID {} -> 0x{:X}", appId, steamId);
    }
    return steamId;
}

std::vector<uint8_t> ForgeLocalAppOwnershipTicket(uint32_t appId) {
    uint32_t baseAppId = 0;
    std::vector<uint8_t> source = PickForgeBase(&baseAppId);
    if (source.size() <= kAppTicketSignatureSize) {
        spdlog::debug("AppTicket: ForgeLocalAppOwnershipTicket for AppID {}: no usable forge base", appId);
        return {};
    }

    const size_t signedSize = source.size() - kAppTicketSignatureSize;
    std::vector<uint8_t> ticket;
    ticket.reserve(source.size() + sizeof(uint32_t));
    ticket.insert(ticket.end(), source.begin(), source.begin() + signedSize);

    const auto* appIdBytes = reinterpret_cast<const uint8_t*>(&appId);
    ticket.insert(ticket.end(), appIdBytes, appIdBytes + sizeof(uint32_t));
    ticket.insert(ticket.end(), source.begin() + signedSize, source.end());

    spdlog::info("AppTicket: Forged AppOwnershipTicket for AppID {} using AppID {} source (size: {} -> {})", appId,
                 baseAppId, source.size(), ticket.size());
    return ticket;
}

bool GetAppOwnershipTicket(uint32_t appId, AppOwnershipTicket& outTicket) {
    outTicket = {};

    // 1. Try imported credential store ticket first
    std::string ticketHex = OmniPlatform::CredentialStore::ReadTicket(appId, "AppTicket");
    if (!ticketHex.empty()) {
        outTicket.data = OmniPlatform::Encoding::HexToBytes(ticketHex);
        if (outTicket.data.size() >= sizeof(uint32_t)) {
            outTicket.totalSize = static_cast<uint32_t>(outTicket.data.size());
            outTicket.appIdOffset = kAppTicketAppIdOffset;
            outTicket.steamIdOffset = kAppTicketSteamIdOffset;
            std::memcpy(&outTicket.signatureOffset, outTicket.data.data(), sizeof(uint32_t));
            outTicket.signatureSize = static_cast<uint32_t>(kAppTicketSignatureSize);
            return true;
        }
    }

    // 2. Fall back to local CEG AppID 7 forgery
    outTicket.data = ForgeLocalAppOwnershipTicket(appId);
    if (outTicket.data.empty()) {
        return false;
    }

    outTicket.totalSize = static_cast<uint32_t>(outTicket.data.size() - sizeof(uint32_t));
    outTicket.appIdOffset = outTicket.totalSize - static_cast<uint32_t>(kAppTicketSignatureSize);
    outTicket.steamIdOffset = kAppTicketSteamIdOffset;
    outTicket.signatureOffset = outTicket.appIdOffset + sizeof(uint32_t);
    outTicket.signatureSize = static_cast<uint32_t>(kAppTicketSignatureSize);
    return true;
}

} // namespace AppTicket
