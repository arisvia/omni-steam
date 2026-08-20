#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

namespace {
std::vector<int16_t> ParsePattern(const std::string& pattern) {
    std::vector<int16_t> bytes;
    std::istringstream stream(pattern);
    std::string byteStr;

    while (stream >> byteStr) {
        if (byteStr == "?" || byteStr == "??") {
            bytes.push_back(-1);
        } else {
            bytes.push_back(static_cast<int16_t>(std::stoul(byteStr, nullptr, 16)));
        }
    }
    return bytes;
}
} // namespace

uintptr_t ByteSearch::FindPattern(uintptr_t start, size_t length, const std::string& pattern) {
    auto patternBytes = ParsePattern(pattern);
    if (patternBytes.empty() || length < patternBytes.size())
        return 0;

    const uint8_t* memory = reinterpret_cast<const uint8_t*>(start);
    size_t patternSize = patternBytes.size();

    for (size_t i = 0; i <= length - patternSize; ++i) {
        bool match = true;
        for (size_t j = 0; j < patternSize; ++j) {
            if (patternBytes[j] != -1 && memory[i + j] != static_cast<uint8_t>(patternBytes[j])) {
                match = false;
                break;
            }
        }
        if (match)
            return start + i;
    }
    return 0;
}

uintptr_t ByteSearch::FindPatternInModule(const std::string& moduleName, const std::string& pattern) {
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!BinaryParser::GetModuleTextSection(moduleName, textStart, textSize)) {
        return 0;
    }
    return FindPattern(textStart, textSize, pattern);
}

} // namespace OmniPlatform
