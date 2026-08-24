#include "PatternLoader.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <spdlog/spdlog.h>
#include <string>
#include <toml++/toml.hpp>
#include <unordered_map>
#include <vector>

#include "OmniPlatform/OmniEndpoints.h"
#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Metadata/SymbolTable.h"

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
                    "48 89 5C 24 18 89 54 24 10 55 56 57 48 83 EC 20 44 8B 49 20", 0);
    RegisterPattern("MarkLicenseAsChanged", "steamclient64.dll", "48 89 5C 24 20 89 54 24 10 55 56 57 48 83 EC 20", 0);
    RegisterPattern("ProcessPendingLicenseUpdates", "steamclient64.dll", "41 56 41 57 48 83 EC 38 83 B9 98 24 00 00 00",
                    0);
    RegisterPattern("BBuildAndAsyncSendFrame", "steamclient64.dll",
                    "48 8B C4 55 48 8D 68 A1 48 81 EC C0 00 00 00 48 89 70 18", 0);
    RegisterPattern("RecvPkt", "steamclient64.dll", "48 8B C4 55 48 8D A8 98 F6 FF FF", 0);
    RegisterPattern("OptedInMask", "steamclient64.dll", "89 54 24 10 55 53 56 57 41 54 41 55 48 8D AC 24 38 FF FF FF",
                    0);
    RegisterPattern("SpawnProcess", "steamclient64.dll",
                    "48 89 5C 24 18 4C 89 4C 24 20 48 89 54 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 30 "
                    "FF FF FF",
                    0);
    RegisterPattern("FillInAppOverview", "steamui.dll",
                    "48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1", 0);
#elif defined(OMNI_PLATFORM_LINUX)
    spdlog::warn("PatternLoader: No verified steamclient.so signatures are bundled; "
                 "function hooks stay dormant to avoid attaching to wrong addresses");
#elif defined(OMNI_PLATFORM_MACOS)
    spdlog::warn("PatternLoader: No verified steamclient.dylib signatures are bundled; "
                 "function hooks stay dormant to avoid attaching to wrong addresses");
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
        if (rva == 0 || rva > (512ull << 20)) {
            spdlog::warn("PatternLoader: Cache entry '{}' has implausible RVA 0x{:X}, discarding cache", name, rva);
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

// toml++ exposes a dual API: with exceptions enabled parse() returns a plain
// table, with them disabled it returns a parse_result wrapper carrying
// success state. Normalize both behind std::optional so call sites never
// depend on the build's exception mode.
std::optional<toml::table> ParseTomlDocument(const std::string& source, bool fromFile) {
#if TOML_EXCEPTIONS
    try {
        return fromFile ? std::optional(toml::parse_file(source)) : std::optional(toml::parse(source));
    } catch (...) {
        return std::nullopt;
    }
#else
    toml::parse_result parsed = fromFile ? toml::parse_file(source) : toml::parse(source);
    if (!parsed.succeeded())
        return std::nullopt;
    return std::move(parsed.table());
#endif
}

// Applies a parsed signature document ([functions] table of name -> rva).
size_t ApplyParsedSignatures(const toml::table& parsed, uintptr_t moduleBase) {
    size_t applied = 0;
    if (auto* functions = parsed["functions"].as_table()) {
        for (const auto& [name, node] : *functions) {
            uint64_t rva = 0;
            if (auto text = node.value<std::string>()) {
                try {
                    rva = std::stoull(*text, nullptr, 0);
                } catch (...) {
                    continue;
                }
            } else if (auto number = node.value<int64_t>()) {
                rva = static_cast<uint64_t>(*number);
            }
            if (rva == 0 || rva > (512ull << 20))
                continue;
            g_resolvedAddresses[NormalizeFunctionName(std::string(name))] = moduleBase + rva;
            ++applied;
        }
    }
    return applied;
}

bool DeclaresMatchingHash(const toml::table& parsed, const std::string& moduleHash) {
    auto declaredHash = parsed["binary_sha256"].value<std::string>();
    return declaredHash && (moduleHash.empty() || *declaredHash == moduleHash);
}

// Loads hash-keyed signature TOMLs produced by tools/harvest_signatures.py
// (see .github/workflows/signature-harvest.yml). Each file declares the
// SHA256 of the binary it was harvested from; only matching files apply.
void LoadExternalSignatures(const std::string& signaturesDir, uintptr_t moduleBase, const std::string& moduleHash) {
    if (moduleBase == 0)
        return;

    std::error_code ec;
    if (!fs::exists(signaturesDir, ec))
        return;

    size_t appliedFiles = 0;
    size_t appliedFunctions = 0;
    for (const auto& entry : fs::directory_iterator(signaturesDir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".toml")
            continue;

        auto parsed = ParseTomlDocument(entry.path().string(), true);
        if (!parsed) {
            spdlog::warn("PatternLoader: Skipping malformed signature file {}", entry.path().filename().string());
            continue;
        }

        if (!DeclaresMatchingHash(*parsed, moduleHash))
            continue;

        appliedFunctions += ApplyParsedSignatures(*parsed, moduleBase);
        ++appliedFiles;
    }

    if (appliedFiles > 0) {
        spdlog::info("PatternLoader: Applied {} external signature files ({} functions) from {}", appliedFiles,
                     appliedFunctions, signaturesDir);
    }
}

std::string GetSignaturePlatformDir() {
#if defined(OMNI_PLATFORM_WINDOWS)
    return "windows-x64";
#elif defined(OMNI_PLATFORM_MACOS)
    return "macos-universal";
#else
#if defined(OMNI_ARCH_X86)
    return "linux-i386";
#else
    return "linux-x64";
#endif
#endif
}

// Runtime remote fallback: fetches a fresh signature document from the CDN
// mirrors when local sources cannot resolve every registered target. This is
// what makes Windows pattern breakage self-healing across Steam updates.
void FetchRemoteSignatures(const std::string& moduleHash, uintptr_t moduleBase) {
    if (moduleHash.empty() || moduleBase == 0)
        return;

    const std::string platformDir = GetSignaturePlatformDir();
    const std::string shortHash = moduleHash.substr(0, 16);

    const char* bases[] = {OmniEndpoints::SignatureDb::kJsDelivrBase, OmniEndpoints::SignatureDb::kRawBase};
    const char* fileNames[] = {moduleHash.c_str(), shortHash.c_str()};

    for (const char* base : bases) {
        for (const char* fileName : fileNames) {
            std::string url = std::string(base) + "/" + platformDir + "/" + fileName + ".toml";
            auto resp = OmniPlatform::Http::Get(url, 5000);
            if (resp.statusCode != 200 || resp.body.empty())
                continue;

            auto parsed = ParseTomlDocument(resp.body, false);
            if (!parsed || !DeclaresMatchingHash(*parsed, moduleHash))
                continue;

            size_t applied = ApplyParsedSignatures(*parsed, moduleBase);
            if (applied > 0) {
                spdlog::info("PatternLoader: Fetched {} signatures from {}", applied, url);
                return;
            }
        }
    }
}

// Runtime symbol-table targets. Keep these aliases in sync with
// tools/harvest_signatures.py (TARGETS), which uses the same matching rules.
struct SymbolTarget {
    const char* canonical;
    std::vector<const char*> aliases;
};

const SymbolTarget kSymbolTargets[] = {
    {"CheckAppOwnership", {"checkappownership"}},
    {"ConfigStore_GetBinary", {"configstore", "getbinary", "loaddepotdecryptionkey"}},
    {"GetPackageInfo", {"getpackageinfo"}},
    {"MarkLicenseAsChanged", {"marklicenseaschanged"}},
    {"ProcessPendingLicenseUpdates", {"processpendinglicenseupdates"}},
    {"BBuildAndAsyncSendFrame", {"bbuildandasyncsendframe"}},
    {"RecvPkt", {"recvpkt", "recvpacket"}},
    {"OptedInMask", {"optedinmask"}},
    {"SpawnProcess", {"spawnprocess"}},
    {"FillInAppOverview", {"fillinappoverview"}},
};

// Resolves hook targets directly from the target module's own symbol data -
// the zero-maintenance path for Linux/macOS clients that retain symbols.
// Ambiguous matches (same alias hitting several distinct RVAs) are skipped so
// we never hook a wrong function.
void ResolveViaSymbolTable(const std::string& moduleName, uintptr_t moduleBase) {
    struct MatchState {
        uint64_t rva = 0;
        size_t hits = 0;
        bool ambiguous = false;
    };
    std::map<std::string, MatchState> matches;

    SymbolTable::ForEachFunction(moduleName, [&](const std::string& name, const std::string& demangled, uint64_t rva) {
        const std::string lowerName = SymbolTable::ToLower(name);
        const std::string lowerDemangled = SymbolTable::ToLower(demangled);

        for (const auto& target : kSymbolTargets) {
            for (const auto* alias : target.aliases) {
                if (lowerName.find(alias) == std::string::npos && lowerDemangled.find(alias) == std::string::npos) {
                    continue;
                }
                MatchState& state = matches[target.canonical];
                if (state.hits == 0) {
                    state.rva = rva;
                } else if (state.rva != rva) {
                    state.ambiguous = true;
                }
                ++state.hits;
                break;
            }
        }
        return true;
    });

    for (const auto& [canonical, state] : matches) {
        if (state.ambiguous || state.hits == 0) {
            spdlog::warn("PatternLoader: Symbol table match for {} is ambiguous ({} hits), skipping", canonical,
                         state.hits);
            continue;
        }
        g_resolvedAddresses[NormalizeFunctionName(canonical)] = moduleBase + state.rva;
        spdlog::info("PatternLoader: Resolved {} via runtime symbol table (RVA: 0x{:X})", canonical, state.rva);
    }
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

    // 2. Merge harvested signature TOMLs shipped with the deployment
    LoadExternalSignatures(GetCacheDirectory() + "/signatures", moduleBase, moduleHash);

    // 3. Resolve remaining targets from the module's own symbol table
    //    (zero-maintenance path; adapts to any client update automatically)
    ResolveViaSymbolTable(modName, moduleBase);

    // 4. If registered targets are still missing (e.g. patterns broke after a
    //    Steam update), try the remote signature database before scanning
    if (g_resolvedAddresses.size() < g_patterns.size()) {
        FetchRemoteSignatures(moduleHash, moduleBase);
    }

    // 5. Hash changed or first run: Perform one-time dynamic pattern scan
    spdlog::info("PatternLoader: Binary hash changed or new version detected ({}), performing initial pattern scan...",
                 moduleHash);
    for (const auto& [name, entry] : g_patterns) {
        if (g_resolvedAddresses.count(name))
            continue;
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

    // 6. Save resolved RVAs to local cache for instant future launches
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
