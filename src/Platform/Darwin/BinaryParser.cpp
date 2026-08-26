#include <cstdint>
#include <cstring>
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
            const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
            if (!header)
                return false;
            intptr_t slide = _dyld_get_image_vmaddr_slide(i);
            const uint8_t* cur = reinterpret_cast<const uint8_t*>(header + 1);

            for (uint32_t j = 0; j < header->ncmds; ++j) {
                const auto* cmd = reinterpret_cast<const load_command*>(cur);
                if (cmd->cmd == LC_SEGMENT_64) {
                    const auto* seg = reinterpret_cast<const segment_command_64*>(cur);
                    if (std::strcmp(seg->segname, "__TEXT") == 0) {
                        outStart = seg->vmaddr + slide;
                        outSize = seg->vmsize;
                        return true;
                    }
                }
                cur += cmd->cmdsize;
            }
            outStart = reinterpret_cast<uintptr_t>(header);
            outSize = 0x4000000; // 64MB fallback
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

std::vector<BinaryParser::SectionInfo> BinaryParser::GetSections(const std::string& /*modulePath*/) {
    return {};
}

} // namespace OmniPlatform
