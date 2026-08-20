#include "DenuvoImporter.h"

#include "DepotKeyStore.h"
#include "ScriptManager.h"
#include "SteamApi.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>

#include "OmniPlatform/OmniPlatform.h"

namespace fs = std::filesystem;

namespace Manager {

namespace {
bool WriteBinaryFile(const std::string& path, const std::vector<uint8_t>& data) {
    try {
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
        return true;
    } catch (...) {
        return false;
    }
}

uint32_t ExtractAppIdFromBinaryTicket(const uint8_t* data, size_t size) {
    // Valve AppOwnershipTicket typically contains uint32 AppID at fixed offsets (usually offset 8, 12, or 16)
    if (size >= 16) {
        for (size_t offset = 0; offset + 4 <= size && offset <= 32; offset += 4) {
            uint32_t val = *reinterpret_cast<const uint32_t*>(data + offset);
            if (val >= 100 && val <= 5000000) {
                return val;
            }
        }
    }
    return 0;
}
} // namespace

DenuvoImportResult DenuvoImporter::ImportTickets(uint32_t appId, const std::string& appTicketHex,
                                                 const std::string& eTicketHex) {
    DenuvoImportResult res;
    res.appId = appId;

    if (appId == 0) {
        res.message = "Invalid AppID provided";
        return res;
    }

    spdlog::info("DenuvoImporter: Importing tickets for AppID {}", appId);

    // 1. Write to Platform CredentialStore (Registry / XDG / AppSupport)
    if (!appTicketHex.empty()) {
        OmniPlatform::CredentialStore::WriteTicket(appId, "AppTicket", appTicketHex);
    }
    if (!eTicketHex.empty()) {
        OmniPlatform::CredentialStore::WriteTicket(appId, "ETicket", eTicketHex);
    }

    // 2. Also save raw binary files to tickets storage folder
    std::string credDir = OmniPlatform::Paths::GetCredentialsDirectory();
    std::string appDir = (fs::path(credDir) / std::to_string(appId)).generic_string();

    if (!appTicketHex.empty()) {
        auto rawAppTicket = OmniPlatform::Encoding::HexToBytes(appTicketHex);
        WriteBinaryFile((fs::path(appDir) / "appticket.bin").generic_string(), rawAppTicket);
    }
    if (!eTicketHex.empty()) {
        auto rawETicket = OmniPlatform::Encoding::HexToBytes(eTicketHex);
        WriteBinaryFile((fs::path(appDir) / "eticket.bin").generic_string(), rawETicket);
    }

    // 3. Query Steam Store API for game details and DLC list
    auto details = SteamApi::GetAppDetails(appId);
    res.gameName = details.name.empty() ? ("App_" + std::to_string(appId)) : details.name;
    res.dlcCount = details.dlcAppIds.size();

    // 4. Resolve Depot Keys from local config.vdf and DepotKeyStore
    UnlockGameSpec spec;
    spec.appId = appId;
    spec.gameName = res.gameName;
    spec.dlcAppIds = details.dlcAppIds;
    spec.appTicketHex = appTicketHex;
    spec.eTicketHex = eTicketHex;

    // Check main game depot key
    std::string mainKey = DepotKeyStore::GetKeyForDepot(appId);
    if (!mainKey.empty()) {
        spec.depotKeyHex = mainKey;
        res.resolvedDepotKeysCount++;
    } else {
        res.missingDepots.push_back(appId);
    }

    // Check DLC depot keys
    for (uint32_t dlcId : details.dlcAppIds) {
        std::string dlcKey = DepotKeyStore::GetKeyForDepot(dlcId);
        if (!dlcKey.empty()) {
            spec.depotKeys[dlcId] = dlcKey;
            res.resolvedDepotKeysCount++;
        }
    }

    // 5. Generate and save the all-in-one unlock script
    if (ScriptManager::SaveGameUnlock(spec)) {
        res.success = true;
        res.message = "Successfully created unified unlock & Denuvo script for " + res.gameName;
        spdlog::info("DenuvoImporter: Created all-in-one script for {} (AppID: {}) with {} keys", res.gameName, appId,
                     res.resolvedDepotKeysCount);
    } else {
        res.message = "Failed to write unlock script to Steam config/lua/";
    }

    return res;
}

DenuvoImportResult DenuvoImporter::ImportFromPayload(const std::string& payload, const std::string& filename) {
    DenuvoImportResult res;

    // Case 1: tickets.txt format (plain text summary)
    if (payload.find("appid:") != std::string::npos || payload.find("appticket") != std::string::npos) {
        uint32_t appId = 0;
        std::string appTicketHex;
        std::string eTicketHex;

        std::regex idRegex("appid\\s*:\\s*(\\d+)");
        std::regex appTicketRegex("appticket[^:]*:\\s*([0-9a-fA-F]+)");
        std::regex eTicketRegex("eticket[^:]*:\\s*([0-9a-fA-F]+)");

        std::smatch m;
        if (std::regex_search(payload, m, idRegex)) {
            appId = static_cast<uint32_t>(std::stoul(m[1].str()));
        }
        if (std::regex_search(payload, m, appTicketRegex)) {
            appTicketHex = m[1].str();
        }
        if (std::regex_search(payload, m, eTicketRegex)) {
            eTicketHex = m[1].str();
        }

        if (appId != 0) {
            return ImportTickets(appId, appTicketHex, eTicketHex);
        }
    }

    // Case 2: Binary file payload (e.g. appticket.bin or eticket.bin)
    const uint8_t* rawBytes = reinterpret_cast<const uint8_t*>(payload.data());
    size_t rawSize = payload.size();

    uint32_t detectedAppId = 0;
    std::regex fileIdRegex("(\\d{3,10})");
    std::smatch fileMatch;
    if (std::regex_search(filename, fileMatch, fileIdRegex)) {
        detectedAppId = static_cast<uint32_t>(std::stoul(fileMatch[1].str()));
    } else {
        detectedAppId = ExtractAppIdFromBinaryTicket(rawBytes, rawSize);
    }

    if (detectedAppId != 0) {
        std::string hexStr = OmniPlatform::Encoding::BytesToHex(rawBytes, rawSize);
        if (filename.find("eticket") != std::string::npos) {
            return ImportTickets(detectedAppId, "", hexStr);
        } else {
            return ImportTickets(detectedAppId, hexStr, "");
        }
    }

    res.message = "Could not identify AppID from uploaded ticket binary. Please ensure the filename contains the "
                  "AppID or upload tickets.txt.";
    return res;
}

} // namespace Manager
