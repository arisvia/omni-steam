#include "SymbolTable.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace SymbolTable {

namespace {
std::string BaseName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}
} // namespace

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ModuleMatches(const std::string& modulePath, const std::string& moduleName) {
    std::string candidate = ToLower(BaseName(modulePath));
    std::string target = ToLower(BaseName(moduleName));
    return candidate == target;
}

} // namespace SymbolTable
