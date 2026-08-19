#include <charconv>
#include <cstdint>
#include <string_view>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {
namespace Numbers {

uint64_t ParseUInt64(std::string_view str) {
    uint64_t val = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), val);
    return (ec == std::errc{}) ? val : 0;
}

uint32_t ParseUInt32(std::string_view str) {
    uint32_t val = 0;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), val);
    return (ec == std::errc{}) ? val : 0;
}

} // namespace Numbers
} // namespace OmniPlatform
