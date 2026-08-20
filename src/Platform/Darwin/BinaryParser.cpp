#include <cstdint>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

bool BinaryParser::GetModuleTextSection(const std::string& moduleName, uintptr_t& outStart, size_t& outSize) {
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const char* name = _dyld_get_image_name(i);
        if (name && std::string(name).find(moduleName) != std::string::npos) {
            outStart = _dyld_get_image_vmaddr_slide(i);
            outSize = 0x1000000;
            return true;
        }
    }
    return false;
}

uintptr_t BinaryParser::GetModuleBase(const std::string& moduleName) {
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const char* name = _dyld_get_image_name(i);
        if (name && std::string(name).find(moduleName) != std::string::npos) {
            return reinterpret_cast<uintptr_t>(_dyld_get_image_header(i));
        }
    }
    return 0;
}

std::vector<BinaryParser::SectionInfo> BinaryParser::GetSections(const std::string& modulePath) {
    return {};
}

} // namespace OmniPlatform
