#include <cstdint>
#include <string>

namespace OmniPlatform {
namespace Numbers {
uint64_t ParseUInt64(const std::string& str) {
    try {
        return std::stoull(str);
    } catch (...) {
        return 0;
    }
}

uint32_t ParseUInt32(const std::string& str) {
    try {
        return static_cast<uint32_t>(std::stoul(str));
    } catch (...) {
        return 0;
    }
}
} // namespace Numbers
} // namespace OmniPlatform
