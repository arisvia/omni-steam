#include <elf.h>
#include <link.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

struct CallbackData {
    std::string moduleName;
    uintptr_t baseAddress = 0;
    uintptr_t textStart = 0;
    size_t textSize = 0;
    bool found = false;
};

static int PhdrCallback(struct dl_phdr_info* info, size_t size, void* data) {
    auto* cbData = reinterpret_cast<CallbackData*>(data);
    if (!info->dlpi_name)
        return 0;

    std::string path(info->dlpi_name);
    if (path.find(cbData->moduleName) != std::string::npos || (cbData->moduleName.empty() && path.empty())) {
        cbData->baseAddress = info->dlpi_addr;
        cbData->found = true;

        for (int i = 0; i < info->dlpi_phnum; ++i) {
            if (info->dlpi_phdr[i].p_type == PT_LOAD && (info->dlpi_phdr[i].p_flags & PF_X)) {
                cbData->textStart = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
                cbData->textSize = info->dlpi_phdr[i].p_memsz;
                break;
            }
        }
        return 1;
    }
    return 0;
}

bool BinaryParser::GetModuleTextSection(const std::string& moduleName, uintptr_t& outStart, size_t& outSize) {
    CallbackData data;
    data.moduleName = moduleName;
    dl_iterate_phdr(PhdrCallback, &data);

    if (data.found && data.textStart != 0) {
        outStart = data.textStart;
        outSize = data.textSize;
        return true;
    }
    return false;
}

uintptr_t BinaryParser::GetModuleBase(const std::string& moduleName) {
    CallbackData data;
    data.moduleName = moduleName;
    dl_iterate_phdr(PhdrCallback, &data);
    return data.found ? data.baseAddress : 0;
}

std::vector<BinaryParser::SectionInfo> BinaryParser::GetSections(const std::string& modulePath) {
    return {};
}

} // namespace OmniPlatform
