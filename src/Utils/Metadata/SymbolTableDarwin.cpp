// Mach-O symbol-table enumeration (macOS).
//
// Locates the loaded image through dyld APIs and walks its LC_SYMTAB load
// command in memory (the __LINKEDIT segment is always mapped contiguously,
// so file offsets resolve to header-relative addresses).

#include "SymbolTable.h"

#include <cstdint>
#include <cxxabi.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <string>

namespace SymbolTable {

namespace {

std::string DemangleItanium(const char* mangled) {
    int status = 0;
    char* result = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    if (!result)
        return "";
    std::string demangled(result);
    free(result);
    return demangled;
}

} // namespace

bool ForEachFunction(const std::string& moduleName, const SymbolCallback& callback) {
    const uint32_t imageCount = _dyld_image_count();
    for (uint32_t i = 0; i < imageCount; ++i) {
        const char* imagePath = _dyld_get_image_name(i);
        if (!imagePath || !ModuleMatches(imagePath, moduleName))
            continue;

        const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
        if (!header || header->magic != MH_MAGIC_64)
            return false;

        uint64_t textVmaddrBase = 0;
        const nlist_64* symbolTable = nullptr;
        uint32_t symbolCount = 0;
        const char* stringTable = nullptr;

        const auto* cmdCursor = reinterpret_cast<const uint8_t*>(header) + sizeof(mach_header_64);
        for (uint32_t c = 0; c < header->ncmds; ++c) {
            const auto* cmd = reinterpret_cast<const load_command*>(cmdCursor);
            if (cmd->cmd == LC_SEGMENT_64) {
                const auto* seg = reinterpret_cast<const segment_command_64*>(cmdCursor);
                if (textVmaddrBase == 0 && (seg->initprot & VM_PROT_EXECUTE)) {
                    textVmaddrBase = seg->vmaddr;
                }
            } else if (cmd->cmd == LC_SYMTAB) {
                const auto* symtabCmd = reinterpret_cast<const symtab_command*>(cmdCursor);
                symbolTable =
                    reinterpret_cast<const nlist_64*>(reinterpret_cast<const uint8_t*>(header) + symtabCmd->symoff);
                symbolCount = symtabCmd->nsyms;
                stringTable =
                    reinterpret_cast<const char*>(reinterpret_cast<const uint8_t*>(header) + symtabCmd->stroff);
            }
            cmdCursor += cmd->cmdsize;
        }

        if (!symbolTable || !stringTable)
            return false;

        constexpr unsigned char kTypeMask = 0x0E; // N_TYPE bits
        for (uint32_t s = 0; s < symbolCount; ++s) {
            const nlist_64& sym = symbolTable[s];
            if ((sym.n_type & kTypeMask) != N_SECT)
                continue; // defined-in-section functions only
            if ((sym.n_type & N_STAB) != 0)
                continue;
            if (sym.n_value == 0 || sym.n_un.n_strx == 0)
                continue;

            const char* raw = stringTable + sym.n_un.n_strx;
            if (!raw || !*raw)
                continue;

            std::string demangled = DemangleItanium(raw);
            uint64_t rva = sym.n_value - textVmaddrBase;
            if (!callback(raw, demangled.empty() ? raw : demangled, rva))
                break;
        }
        return true;
    }
    return false;
}

} // namespace SymbolTable
