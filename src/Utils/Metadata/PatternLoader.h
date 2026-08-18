#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

namespace PatternLoader {

struct PatternEntry {
    std::string moduleName;
    std::string pattern;
    int32_t offset = 0;
};

void Initialize(const std::string& patternDir = "");
uintptr_t GetFunctionAddress(const std::string& functionName);
bool RegisterPattern(const std::string& functionName, const std::string& moduleName, const std::string& pattern, int32_t offset = 0);
void LoadFromToml(const std::string& filePath);

} // namespace PatternLoader
