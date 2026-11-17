#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PatternLoader {

struct PatternEntry {
    std::string moduleName;
    std::string pattern;
    int32_t offset = 0;
};

void Initialize(const std::string& patternDir = "");
uintptr_t GetFunctionAddress(const std::string& functionName);
// Cache-only snapshot of resolved function addresses (no lazy scanning),
// sorted by canonical name; used by core_status.json anchor reporting.
std::vector<std::pair<std::string, uintptr_t>> GetResolvedAddresses();
bool RegisterPattern(const std::string& functionName, const std::string& moduleName, const std::string& pattern,
                     int32_t offset = 0);
// Parses a signature document (TOML text) and applies the entries whose
// declared binary_sha256 matches moduleHash; returns the applied count.
// Shared by the remote-fetch path and the schema regression tests.
// Caller must hold the pattern lock (Initialize and its callees do).
size_t ApplySignatureDocumentText(const std::string& tomlText, uintptr_t moduleBase, const std::string& moduleHash,
                                  bool arm64 = false);
} // namespace PatternLoader
