#include "PatternLoader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

#include "OmniPlatform/OmniEndpoints.h"
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

uintptr_t ParseHexNumber(const std::string& str) {
    try {
        if (str.rfind("0x", 0) == 0 || str.rfind("0X", 0) == 0) {
            return std::stoull(str.substr(2), nullptr, 16);
        }
        return std::stoull(str, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

void RegisterBuiltinPatterns() {
#if defined(OMNI_PLATFORM_WINDOWS)
    RegisterPattern("ConfigStore_GetBinary", "steamclient64.dll", "40 53 55 56 57 48 83 EC 38 48 63 FA 49 8B E9", 0);
    RegisterPattern("IPCProcessMessage", "steamclient64.dll",
                    "48 89 5C 24 18 48 89 6C 24 20 57 41 54 41 55 41 56 41 57 48 83 EC 30", 0);
    RegisterPattern("BGetCallback", "steamclient64.dll",
                    "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 48 8B F9 49 8B D8", 0);
#elif defined(OMNI_PLATFORM_LINUX)
    RegisterPattern("ConfigStore_GetBinary", "steamclient.so", "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC", 0);
    RegisterPattern("IPCProcessMessage", "steamclient.so", "55 48 89 E5 41 57 41 56 41 55 41 54 49 89 D4", 0);
    RegisterPattern("BGetCallback", "steamclient.so", "55 48 89 E5 41 57 41 56 53 48 83 EC", 0);
#elif defined(OMNI_PLATFORM_MACOS)
    RegisterPattern("ConfigStore_GetBinary", "steamclient.dylib", "55 48 89 E5 41 57 41 56 41 55 41 54", 0);
    RegisterPattern("IPCProcessMessage", "steamclient.dylib", "55 48 89 E5 41 57 41 56", 0);
#endif
}

void ParseTomlTable(const toml::table& tbl, uintptr_t moduleBase) {
    for (const auto& [key, val] : tbl) {
        if (auto node = val.as_table()) {
            std::string funcName = node->get("name") ? node->get("name")->value_or("") : std::string(key.str());
            funcName = NormalizeFunctionName(funcName);

            // 1. Check direct RVA
            if (node->get("rva") && moduleBase != 0) {
                std::string rvaStr = node->get("rva")->value_or("");
                uintptr_t rva = ParseHexNumber(rvaStr);
                if (rva != 0) {
                    uintptr_t directAddr = moduleBase + rva;
                    g_resolvedAddresses[funcName] = directAddr;
                    spdlog::info("PatternLoader: Direct RVA mapped {} -> 0x{:X} at {:p}", funcName, rva,
                                 reinterpret_cast<void*>(directAddr));
                }
            }

            // 2. Check pattern signature
            if (node->get("sig")) {
                std::string sig = node->get("sig")->value_or("");
                if (!sig.empty()) {
                    RegisterPattern(funcName, GetTargetModuleName(), sig, 0);
                }
            }
        }
    }
}
} // namespace

void Initialize(const std::string& patternDir) {
    std::lock_guard<std::mutex> lock(g_patternMutex);
    g_patterns.clear();
    g_resolvedAddresses.clear();

    RegisterBuiltinPatterns();

    std::string modName = GetTargetModuleName();
    auto hModule = OmniPlatform::DynamicLibrary::GetLoadedModule(modName);
    uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);
    std::string modulePath = hModule ? OmniPlatform::DynamicLibrary::GetModulePath(hModule) : "";
    std::string moduleHash = !modulePath.empty() ? OmniPlatform::Hash::Sha256File(modulePath) : "";

    if (!moduleHash.empty()) {
        spdlog::info("PatternLoader: Target module {} (Base: {:p}, SHA256: {})", modName,
                     reinterpret_cast<void*>(moduleBase), moduleHash);
    }

    // Candidate directories to probe (both legacy OpenSteamTool cache and OmniSteam cache)
    std::vector<std::string> candidateFiles;
    if (!moduleHash.empty()) {
        candidateFiles.push_back("opensteamtool/pattern/steamclient/" + moduleHash + ".toml");
        candidateFiles.push_back("omnisteam/pattern/steamclient/" + moduleHash + ".toml");
        candidateFiles.push_back("pattern/steamclient/" + moduleHash + ".toml");
        candidateFiles.push_back("pattern/" + moduleHash + ".toml");
    }

    // Probe existing TOMLs in pattern directory
    std::vector<std::string> searchDirs = {patternDir, "opensteamtool/pattern/steamclient",
                                           "omnisteam/pattern/steamclient", "pattern/steamclient", "pattern"};

    for (const auto& dir : searchDirs) {
        if (!dir.empty() && fs::exists(dir)) {
            try {
                for (const auto& entry : fs::directory_iterator(dir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".toml") {
                        candidateFiles.push_back(entry.path().string());
                    }
                }
            } catch (...) {
            }
        }
    }

    for (const auto& file : candidateFiles) {
        if (fs::exists(file)) {
            spdlog::info("PatternLoader: Parsing pattern TOML: {}", file);
            LoadFromToml(file);
        }
    }
}

bool RegisterPattern(const std::string& functionName, const std::string& moduleName, const std::string& pattern,
                     int32_t offset) {
    std::string normalized = NormalizeFunctionName(functionName);
    g_patterns[normalized] = PatternEntry{moduleName, pattern, offset};
    return true;
}

void LoadFromToml(const std::string& filePath) {
    try {
        auto tbl = toml::parse_file(filePath);
        std::string modName = GetTargetModuleName();
        auto hModule = OmniPlatform::DynamicLibrary::GetLoadedModule(modName);
        uintptr_t moduleBase = reinterpret_cast<uintptr_t>(hModule);

        ParseTomlTable(tbl, moduleBase);

        if (auto patterns = tbl["patterns"].as_table()) {
            ParseTomlTable(*patterns, moduleBase);
        }
    } catch (const std::exception& e) {
        spdlog::warn("PatternLoader: Failed to parse {}: {}", filePath, e.what());
    }
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
        spdlog::info("PatternLoader: Resolved {} via pattern scan at {:p} (module: {})", normalized,
                     reinterpret_cast<void*>(finalAddr), entry.moduleName);
        return finalAddr;
    }

    spdlog::warn("PatternLoader: Failed to find pattern for {} in {}", normalized, entry.moduleName);
    return 0;
}

} // namespace PatternLoader
