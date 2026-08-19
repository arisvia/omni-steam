#include "DepotKeyStore.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <spdlog/spdlog.h>
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
        spdlog::warn("DepotKeyStore: Truncated binary file, expected {} bytes, got {}", expectedSize, size);
        return false;
    }

    g_records.resize(header->count);
    const auto* recordsData = reinterpret_cast<const DepotKeyRecord*>(data + sizeof(DepotKeyHeader));
    std::memcpy(g_records.data(), recordsData, header->count * sizeof(DepotKeyRecord));

    return true;
}
} // namespace

void DepotKeyStore::Initialize(const std::string& binFilePath) {
    std::lock_guard<std::mutex> lock(g_storeMutex);
    g_records.clear();
    g_initialized = true;

    std::vector<std::string> candidatePaths = {binFilePath, "depotkeys.bin", "../depotkeys.bin", "../../depotkeys.bin",
                                               OmniPlatform::CredentialStore::GetStoragePath() + "/depotkeys.bin"};

    std::string foundPath;
    for (const auto& p : candidatePaths) {
        if (fs::exists(p)) {
            foundPath = p;
            break;
        }
    }

    // 1. Try local binary file first
    if (!foundPath.empty()) {
        std::ifstream inFile(foundPath, std::ios::binary | std::ios::ate);
        if (inFile) {
            std::streamsize fileSize = inFile.tellg();
            inFile.seekg(0, std::ios::beg);
            std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
            if (inFile.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
                if (ParseBinaryContent(buffer.data(), buffer.size())) {
                    spdlog::info("DepotKeyStore: Loaded {} depot keys from local binary {}", g_records.size(),
                                 foundPath);
                    return;
                }
            }
        }
    }

    // 2. Fallback: Automatically download from GitHub Raw repo
    spdlog::info("DepotKeyStore: Local depotkeys.bin not found, fetching from {}", kRemoteDepotKeysUrl);
    auto resp = OmniPlatform::Http::Get(kRemoteDepotKeysUrl, 10000);
    if (resp.statusCode == 200 && !resp.body.empty()) {
        const uint8_t* rawData = reinterpret_cast<const uint8_t*>(resp.body.data());
        if (ParseBinaryContent(rawData, resp.body.size())) {
            spdlog::info("DepotKeyStore: Successfully fetched and parsed {} depot keys from GitHub remote",
                         g_records.size());

            // Cache to local storage directory for offline usage
            std::string cachePath = OmniPlatform::CredentialStore::GetStoragePath() + "/depotkeys.bin";
            std::ofstream cacheOut(cachePath, std::ios::binary | std::ios::trunc);
            if (cacheOut) {
                cacheOut.write(resp.body.data(), resp.body.size());
                spdlog::info("DepotKeyStore: Cached remote depotkeys.bin to {}", cachePath);
            }
            return;
        }
    } else {
        spdlog::warn("DepotKeyStore: Could not fetch remote depot keys: {}", resp.error);
    }
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

} // namespace Manager
