#include "PatternLoader.h"
#include "OmniPlatform/OmniPlatform.h"
#include <toml++/toml.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <filesystem>

namespace fs = std::filesystem;

namespace PatternLoader {

namespace {
    std::mutex g_patternMutex;
    std::unordered_map<std::string, PatternEntry> g_patterns;
    std::unordered_map<std::string, uintptr_t> g_resolvedAddresses;

    void RegisterBuiltinPatterns() {
#if defined(OMNI_PLATFORM_WINDOWS)
        RegisterPattern("ConfigStore_GetBinary", "steamclient64.dll", "48 89 5C 24 ?? 57 48 83 EC ?? 48 8B D9 48 8B FA 8B F2", 0);
        RegisterPattern("IPCProcessMessage", "steamclient64.dll", "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24", 0);
        RegisterPattern("BGetCallback", "steamclient64.dll", "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 48 8B F9 49 8B D8", 0);
#elif defined(OMNI_PLATFORM_LINUX)
        RegisterPattern("ConfigStore_GetBinary", "steamclient.so", "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC", 0);
        RegisterPattern("IPCProcessMessage", "steamclient.so", "55 48 89 E5 41 57 41 56 41 55 41 54 49 89 D4", 0);
        RegisterPattern("BGetCallback", "steamclient.so", "55 48 89 E5 41 57 41 56 53 48 83 EC", 0);
#elif defined(OMNI_PLATFORM_MACOS)
        RegisterPattern("ConfigStore_GetBinary", "steamclient.dylib", "55 48 89 E5 41 57 41 56 41 55 41 54", 0);
        RegisterPattern("IPCProcessMessage", "steamclient.dylib", "55 48 89 E5 41 57 41 56", 0);
#endif
    }
}

void Initialize(const std::string& patternDir) {
    std::lock_guard<std::mutex> lock(g_patternMutex);
    g_patterns.clear();
    g_resolvedAddresses.clear();
    RegisterBuiltinPatterns();

    if (!patternDir.empty() && fs::exists(patternDir)) {
        for (const auto& entry : fs::directory_iterator(patternDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".toml") {
                LoadFromToml(entry.path().string());
            }
        }
    }
}

bool RegisterPattern(const std::string& functionName, const std::string& moduleName, const std::string& pattern, int32_t offset) {
    g_patterns[functionName] = PatternEntry{ moduleName, pattern, offset };
    return true;
}

void LoadFromToml(const std::string& filePath) {
    try {
        auto tbl = toml::parse_file(filePath);
        if (auto patterns = tbl["patterns"].as_table()) {
            for (const auto& [key, val] : *patterns) {
                if (auto node = val.as_table()) {
                    std::string funcName(key.str());
                    std::string mod = node->get("module")->value_or("");
                    std::string pat = node->get("pattern")->value_or("");
                    int32_t off = static_cast<int32_t>(node->get("offset")->value_or(0));

                    if (!mod.empty() && !pat.empty()) {
                        RegisterPattern(funcName, mod, pat, off);
                        spdlog::debug("PatternLoader: Registered {} from TOML", funcName);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("PatternLoader: Failed to parse {}: {}", filePath, e.what());
    }
}

uintptr_t GetFunctionAddress(const std::string& functionName) {
    std::lock_guard<std::mutex> lock(g_patternMutex);
    auto itAddr = g_resolvedAddresses.find(functionName);
    if (itAddr != g_resolvedAddresses.end()) {
        return itAddr->second;
    }

    auto itPat = g_patterns.find(functionName);
    if (itPat == g_patterns.end()) {
        spdlog::warn("PatternLoader: Unknown function name: {}", functionName);
        return 0;
    }

    const auto& entry = itPat->second;
    uintptr_t found = OmniPlatform::ByteSearch::FindPatternInModule(entry.moduleName, entry.pattern);
    if (found != 0) {
        uintptr_t finalAddr = found + entry.offset;
        g_resolvedAddresses[functionName] = finalAddr;
        spdlog::info("PatternLoader: Resolved {} at {:p} (module: {})", functionName, reinterpret_cast<void*>(finalAddr), entry.moduleName);
        return finalAddr;
    }

    spdlog::warn("PatternLoader: Failed to find pattern for {} in {}", functionName, entry.moduleName);
    return 0;
}

} // namespace PatternLoader
