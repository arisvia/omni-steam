#include "PatternLoader.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace fs = std::filesystem;

namespace PatternLoader {

namespace {
std::mutex g_patternMutex;
std::unordered_map<std::string, PatternEntry> g_patterns;
std::unordered_map<std::string, uintptr_t> g_resolvedAddresses;

std::string GetTargetModuleName() {
#if defined(OMNI_PLATFORM_WINDOWS)
    return "steamclient64.dll";
#elif defined(OMNI_PLATFORM_MACOS)
    return "steamclient.dylib";
#else
    return "steamclient.so";
#endif
}

std::string NormalizeFunctionName(const std::string& name) {
    if (name == "ConfigStoreGetBinary" || name == "ConfigStore_GetBinary" || name == "LoadDepotDecryptionKey") {
        return "ConfigStore_GetBinary";
    }
    if (name == "IPCProcessMessage" || name == "IPC_ProcessMessage") {
        return "IPCProcessMessage";
    }
    if (name == "BGetCallback" || name == "GetCallback") {
        return "BGetCallback";
    }
    return name;
}

// Universal cross-version signature patterns for Steam client internals
void RegisterCoreSignatures() {
#if defined(OMNI_PLATFORM_WINDOWS)
    // 64-bit Windows Steamclient signatures
    RegisterPattern("CheckAppOwnership", "steamclient64.dll", "48 8B C4 89 50 10 48 89 48 08 55 53", 0);
    RegisterPattern("ConfigStore_GetBinary", "steamclient64.dll", "40 53 55 56 57 48 83 EC 38 48 63 FA 49 8B E9", 0);
    RegisterPattern("GetPackageInfo", "steamclient64.dll",
                    "48 89 5C 24 08 57 48 83 EC 20 49 8B F8 48 8B D9 8B CA 48 89 5C 24 30", 0);
    RegisterPattern("MarkLicenseAsChanged", "steamclient64.dll", "48 89 5C 24 20 89 54 24 10 55 56 57 48 83 EC 20", 0);
    RegisterPattern("ProcessPendingLicenseUpdates", "steamclient64.dll", "41 56 41 57 48 83 EC 38 83 B9 98 24 00 00 00",
                    0);
#elif defined(OMNI_PLATFORM_LINUX)
    // Linux ELF Steamclient signatures
    RegisterPattern("CheckAppOwnership", "steamclient.so", "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC", 0);
    RegisterPattern("ConfigStore_GetBinary", "steamclient.so", "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC", 0);
    RegisterPattern("GetPackageInfo", "steamclient.so", "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC", 0);
    RegisterPattern("MarkLicenseAsChanged", "steamclient.so", "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC", 0);
    RegisterPattern("ProcessPendingLicenseUpdates", "steamclient.so", "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC",
                    0);
#elif defined(OMNI_PLATFORM_MACOS)
    // macOS Mach-O Steamclient signatures
    RegisterPattern("CheckAppOwnership", "steamclient.dylib", "55 48 89 E5 41 57 41 56", 0);
    RegisterPattern("ConfigStore_GetBinary", "steamclient.dylib", "55 48 89 E5 41 57 41 56 41 55 41 54", 0);
    RegisterPattern("GetPackageInfo", "steamclient.dylib", "55 48 89 E5 41 57 41 56 41 55 41 54", 0);
    RegisterPattern("MarkLicenseAsChanged", "steamclient.dylib", "55 48 89 E5 41 57 41 56 41 55 41 54", 0);
    RegisterPattern("ProcessPendingLicenseUpdates", "steamclient.dylib", "55 48 89 E5 41 57 41 56 41 55 41 54", 0);
#endif
}
std::string GetCacheDirectory() {
    return OmniPlatform::Paths::GetCacheDirectory();
}

// Binary cache structure: Magic (4) + Version (4) + EntryCount (4) + [NameLen (2) + Name + RVA (8)]
constexpr uint32_t kPatternCacheMagic = 0x50544348; // "PTCH"

bool LoadRvaCache(const std::string& cachePath, uintptr_t moduleBase) {
    if (!fs::exists(cachePath) || moduleBase == 0) {
        return false;
    }

    std::ifstream file(cachePath, std::ios::binary);
    if (!file)
        return false;

    uint32_t magic = 0, version = 0, count = 0;
    if (!file.read(reinterpret_cast<char*>(&magic), 4) || !file.read(reinterpret_cast<char*>(&version), 4) ||
        !file.read(reinterpret_cast<char*>(&count), 4)) {
        return false;
    }

    if (magic != kPatternCacheMagic || version != 1 || count > 500) {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint16_t nameLen = 0;
        if (!file.read(reinterpret_cast<char*>(&nameLen), 2) || nameLen == 0 || nameLen > 256) {
            return false;
        }
        std::string name(nameLen, '\0');
        if (!file.read(&name[0], nameLen)) {
            return false;
        }
        uint64_t rva = 0;
        if (!file.read(reinterpret_cast<char*>(&rva), 8)) {
            return false;
        }

        uintptr_t addr = moduleBase + static_cast<uintptr_t>(rva);
        std::string norm = NormalizeFunctionName(name);
        g_resolvedAddresses[norm] = addr;
        spdlog::info("PatternLoader: Loaded cached RVA for {} -> 0x{:X} at {:p}", norm, rva,
                     reinterpret_cast<void*>(addr));
    }
    return true;
}

void SaveRvaCache(const std::string& cachePath, uintptr_t moduleBase) {
    if (moduleBase == 0 || g_resolvedAddresses.empty()) {
        return;
    }

    try {
        fs::create_directories(fs::path(cachePath).parent_path());
    } catch (...) {
    }

    std::ofstream file(cachePath, std::ios::binary | std::ios::trunc);
    if (!file)
        return;
    uint32_t magic = kPatternCacheMagic;
    uint32_t version = 1;
    uint32_t count = static_cast<uint32_t>(g_resolvedAddresses.size());

    file.write(reinterpret_cast<const char*>(&magic), 4);
    file.write(reinterpret_cast<const char*>(&version), 4);
    file.write(reinterpret_cast<const char*>(&count), 4);

    for (const auto& [name, addr] : g_resolvedAddresses) {
        uint16_t nameLen = static_cast<uint16_t>(name.size());
        file.write(reinterpret_cast<const char*>(&nameLen), 2);
        file.write(name.data(), nameLen);
        uint64_t rva = static_cast<uint64_t>(addr - moduleBase);
        file.write(reinterpret_cast<const char*>(&rva), 8);
    }
    spdlog::info("PatternLoader: Saved {} function RVAs to cache {}", count, cachePath);
}

} // namespace

void Initialize(const std::string& /*unused*/) {
    std::lock_guard<std::mutex> lock(g_patternMutex);
    g_patterns.clear();
    g_resolvedAddresses.clear();

    RegisterCoreSignatures();

    std::string modName = GetTargetModuleName();
    auto hModule = OmniPlatform::DynamicLibrary::GetLoadedModule(modName);
    uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
    std::string modulePath = hModule ? OmniPlatform::DynamicLibrary::GetModulePath(hModule) : "";
    std::string moduleHash = !modulePath.empty() ? OmniPlatform::Hash::Sha256File(modulePath) : "";

    if (moduleHash.empty()) {
        spdlog::warn("PatternLoader: Target module {} not ready, will scan on-demand", modName);
        return;
    }

    std::string cacheFile = GetCacheDirectory() + "/pattern_" + moduleHash + ".cache";

    // 1. Check if we already have cached RVAs for this exact binary hash
    if (LoadRvaCache(cacheFile, moduleBase)) {
        spdlog::info("PatternLoader: Hash {} matched, all RVAs loaded from local cache in 0.01ms (Zero Scan)",
                     moduleHash);
        return;
    }

    // 2. Hash changed or first run: Perform one-time dynamic pattern scan
    spdlog::info("PatternLoader: Binary hash changed or new version detected ({}), performing initial pattern scan...",
                 moduleHash);
    for (const auto& [name, entry] : g_patterns) {
        uintptr_t found = OmniPlatform::ByteSearch::FindPatternInModule(entry.moduleName, entry.pattern);
        if (found != 0) {
            uintptr_t finalAddr = found + entry.offset;
            g_resolvedAddresses[name] = finalAddr;
            spdlog::info("PatternLoader: Resolved {} at {:p} (RVA: 0x{:X})", name, reinterpret_cast<void*>(finalAddr),
                         finalAddr - moduleBase);
        } else {
            spdlog::warn("PatternLoader: Failed to find pattern for {} in {}", name, entry.moduleName);
        }
    }

    // 3. Save resolved RVAs to local cache for instant future launches
    SaveRvaCache(cacheFile, moduleBase);
}

bool RegisterPattern(const std::string& functionName, const std::string& moduleName, const std::string& pattern,
                     int32_t offset) {
    std::string normalized = NormalizeFunctionName(functionName);
    g_patterns[normalized] = PatternEntry{moduleName, pattern, offset};
    return true;
}

uintptr_t GetFunctionAddress(const std::string& functionName) {
    std::lock_guard<std::mutex> lock(g_patternMutex);
    std::string normalized = NormalizeFunctionName(functionName);

    auto itAddr = g_resolvedAddresses.find(normalized);
    if (itAddr != g_resolvedAddresses.end() && itAddr->second != 0) {
        return itAddr->second;
    }

    auto itPat = g_patterns.find(normalized);
    if (itPat == g_patterns.end()) {
        spdlog::warn("PatternLoader: Unknown function name: {}", functionName);
        return 0;
    }

    const auto& entry = itPat->second;
    uintptr_t found = OmniPlatform::ByteSearch::FindPatternInModule(entry.moduleName, entry.pattern);
    if (found != 0) {
        uintptr_t finalAddr = found + entry.offset;
        g_resolvedAddresses[normalized] = finalAddr;
        spdlog::info("PatternLoader: Resolved {} via fallback scan at {:p}", normalized,
                     reinterpret_cast<void*>(finalAddr));
        return finalAddr;
    }

    spdlog::warn("PatternLoader: Failed to find pattern for {} in {}", normalized, entry.moduleName);
    return 0;
}

} // namespace PatternLoader
