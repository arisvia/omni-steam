#include "DepotKeyStore.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"

namespace fs = std::filesystem;

namespace Manager {

namespace {
constexpr uint32_t kOmkyMagic = 0x4F4D4B59; // "OMKY"
constexpr uint32_t kCurrentVersion = 1;

#pragma pack(push, 1)
struct DepotKeyHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
};

struct DepotKeyRecord {
    uint32_t depot_id;
    uint8_t key[32];
};
#pragma pack(pop)

std::mutex g_storeMutex;
bool g_initialized = false;
std::vector<DepotKeyRecord> g_records;
const char* kRemoteDepotKeysUrl = OmniEndpoints::GitHub::kDepotKeysBin;

bool ParseBinaryContent(const uint8_t* data, size_t size) {
    if (size < sizeof(DepotKeyHeader)) {
        spdlog::warn("DepotKeyStore: Buffer size {} smaller than header", size);
        return false;
    }

    const auto* header = reinterpret_cast<const DepotKeyHeader*>(data);
    if (header->magic != kOmkyMagic) {
        spdlog::warn("DepotKeyStore: Invalid magic 0x{:08X}", header->magic);
        return false;
    }

    if (header->version != kCurrentVersion) {
        spdlog::warn("DepotKeyStore: Unsupported version {}", header->version);
        return false;
    }

    size_t expectedSize = sizeof(DepotKeyHeader) + header->count * sizeof(DepotKeyRecord);
    if (size < expectedSize) {
        spdlog::warn("DepotKeyStore: Buffer size {} smaller than expected payload {}", size, expectedSize);
        return false;
    }

    g_records.resize(header->count);
    const auto* recordsData = reinterpret_cast<const DepotKeyRecord*>(data + sizeof(DepotKeyHeader));
    std::memcpy(g_records.data(), recordsData, header->count * sizeof(DepotKeyRecord));

    return true;
}

void ImportKeysFromConfigVdf() {
    std::string steamPath = OmniPlatform::Paths::GetSteamInstallPath();
    std::string vdfPath = (fs::path(steamPath) / "config" / "config.vdf").generic_string();
    if (!fs::exists(vdfPath))
        return;

    std::ifstream in(vdfPath);
    if (!in)
        return;

    std::string line;
    std::regex depotRegex("\"(\\d{3,10})\"");
    std::regex keyRegex("\"DecryptionKey\"\\s*\"([0-9a-fA-F]{64})\"");
    uint32_t currentDepotId = 0;
    size_t imported = 0;

    while (std::getline(in, line)) {
        std::smatch m;
        if (std::regex_search(line, m, keyRegex)) {
            if (currentDepotId != 0 && m.size() > 1) {
                std::string keyHex = m[1].str();
                auto keyBytes = OmniPlatform::Encoding::HexToBytes(keyHex);
                if (keyBytes.size() == 32) {
                    auto it = std::lower_bound(g_records.begin(), g_records.end(), currentDepotId,
                                               [](const DepotKeyRecord& r, uint32_t id) { return r.depot_id < id; });
                    if (it == g_records.end() || it->depot_id != currentDepotId) {
                        DepotKeyRecord rec;
                        rec.depot_id = currentDepotId;
                        std::memcpy(rec.key, keyBytes.data(), 32);
                        g_records.insert(it, rec);
                        imported++;
                    }
                }
            }
        } else if (std::regex_search(line, m, depotRegex)) {
            try {
                currentDepotId = static_cast<uint32_t>(std::stoul(m[1].str()));
            } catch (...) {
                currentDepotId = 0;
            }
        }
    }
    if (imported > 0) {
        spdlog::info("DepotKeyStore: Imported {} additional keys from local Steam config.vdf", imported);
    }
}
} // namespace

void DepotKeyStore::Initialize(const std::string& binFilePath) {
    std::lock_guard<std::mutex> lock(g_storeMutex);
    g_records.clear();
    g_initialized = true;

    std::string cacheFile = (fs::path(OmniPlatform::Paths::GetCacheDirectory()) / "depotkeys.bin").generic_string();
    std::string configFile = (fs::path(OmniPlatform::Paths::GetConfigDirectory()) / "depotkeys.bin").generic_string();
    std::string credFile =
        (fs::path(OmniPlatform::CredentialStore::GetStoragePath()) / "depotkeys.bin").generic_string();

    std::vector<std::string> candidatePaths = {binFilePath, cacheFile, configFile, credFile, "depotkeys.bin"};

    std::string foundPath;
    for (const auto& p : candidatePaths) {
        if (!p.empty() && fs::exists(p)) {
            foundPath = p;
            break;
        }
    }

    // 1. Try local or standard cache binary file first for instant startup
    bool loadedFromLocal = false;
    size_t localCount = 0;
    if (!foundPath.empty()) {
        std::ifstream inFile(foundPath, std::ios::binary | std::ios::ate);
        if (inFile) {
            std::streamsize fileSize = inFile.tellg();
            inFile.seekg(0, std::ios::beg);
            std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
            if (inFile.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
                if (ParseBinaryContent(buffer.data(), buffer.size())) {
                    localCount = g_records.size();
                    loadedFromLocal = true;
                    spdlog::info("DepotKeyStore: Instantly loaded {} depot keys from cached binary {}", localCount,
                                 foundPath);
                }
            }
        }
    }

    // 2. Fetch/update from our repo's primary raw endpoint or CDN mirror
    std::vector<std::string> remoteUrls = {kRemoteDepotKeysUrl,
                                           "https://cdn.jsdelivr.net/gh/arisvia/omni-steam@main/depotkeys.bin",
                                           "https://raw.fastgit.org/arisvia/omni-steam/main/depotkeys.bin"};

    for (const auto& url : remoteUrls) {
        spdlog::info("DepotKeyStore: Checking remote depot keys from {}", url);
        auto resp = OmniPlatform::Http::Get(url, 6000);
        if (resp.statusCode == 200 && !resp.body.empty()) {
            const uint8_t* rawData = reinterpret_cast<const uint8_t*>(resp.body.data());
            if (rawData && resp.body.size() >= sizeof(DepotKeyHeader)) {
                const auto* header = reinterpret_cast<const DepotKeyHeader*>(rawData);
                if (header->magic == kOmkyMagic && header->version == kCurrentVersion) {
                    // Update if new or count is greater than local cached version
                    if (!loadedFromLocal || header->count > localCount) {
                        if (ParseBinaryContent(rawData, resp.body.size())) {
                            spdlog::info("DepotKeyStore: Updated depot keys to {} entries from {}", g_records.size(),
                                         url);
                            std::ofstream cacheOut(cacheFile, std::ios::binary | std::ios::trunc);
                            if (cacheOut) {
                                cacheOut.write(resp.body.data(), resp.body.size());
                                spdlog::info("DepotKeyStore: Persistently saved updated cache to {}", cacheFile);
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    ImportKeysFromConfigVdf();
}

std::string DepotKeyStore::GetKeyForDepot(uint32_t depotId) {
    std::lock_guard<std::mutex> lock(g_storeMutex);
    if (!g_initialized) {
        return "";
    }

    auto it = std::lower_bound(g_records.begin(), g_records.end(), depotId,
                               [](const DepotKeyRecord& rec, uint32_t id) { return rec.depot_id < id; });

    if (it != g_records.end() && it->depot_id == depotId) {
        return OmniPlatform::Encoding::BytesToHex(it->key, 32);
    }
    return "";
}

bool DepotKeyStore::HasKey(uint32_t depotId) {
    return !GetKeyForDepot(depotId).empty();
}

size_t DepotKeyStore::Count() {
    std::lock_guard<std::mutex> lock(g_storeMutex);
    return g_records.size();
}

std::unordered_map<uint32_t, std::string> DepotKeyStore::FindDepotKeysForApp(uint32_t appId,
                                                                             const std::vector<uint32_t>& dlcAppIds) {
    std::lock_guard<std::mutex> lock(g_storeMutex);
    std::unordered_map<uint32_t, std::string> result;
    if (g_records.empty())
        return result;

    auto isCommonRedistOrInvalid = [](uint32_t id) {
        if (id < 10)
            return true;
        // Steamworks Common Redistributables & VC/DirectX runtimes
        if (id >= 228980 && id <= 228999)
            return true;
        return false;
    };

    auto scanRange = [&](uint32_t baseId, uint32_t range) {
        auto it = std::lower_bound(g_records.begin(), g_records.end(), baseId,
                                   [](const DepotKeyRecord& r, uint32_t id) { return r.depot_id < id; });
        while (it != g_records.end() && it->depot_id <= baseId + range) {
            if (!isCommonRedistOrInvalid(it->depot_id)) {
                result[it->depot_id] = OmniPlatform::Encoding::BytesToHex(it->key, 32);
            }
            ++it;
        }
    };

    if (appId > 0) {
        scanRange(appId, 25);
    }

    for (uint32_t dlcId : dlcAppIds) {
        if (dlcId > 0) {
            scanRange(dlcId, 10);
        }
    }

    return result;
}

} // namespace Manager
