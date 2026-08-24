// PE export-table enumeration (Windows).
//
// steamclient64.dll exports only a handful of C entry points today, so this
// backend rarely resolves hook targets by itself - it exists for symmetry and
// to pick up any future Valve exports without code changes.

#include "SymbolTable.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace SymbolTable {

bool ForEachFunction(const std::string& moduleName, const SymbolCallback& callback) {
    HMODULE module = GetModuleHandleA(moduleName.c_str());
    if (!module)
        return false;

    const auto* base = reinterpret_cast<const uint8_t*>(module);
    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const IMAGE_DATA_DIRECTORY& exportDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exportDir.Size == 0)
        return false;

    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exportDir.VirtualAddress);
    const auto* names = reinterpret_cast<const uint32_t*>(base + exports->AddressOfNames);
    const auto* functions = reinterpret_cast<const uint32_t*>(base + exports->AddressOfFunctions);
    const auto* ordinals = reinterpret_cast<const uint16_t*>(base + exports->AddressOfNameOrdinals);

    for (uint32_t i = 0; i < exports->NumberOfNames; ++i) {
        const char* name = reinterpret_cast<const char*>(base + names[i]);
        uint32_t functionRva = functions[ordinals[i]];
        // Ignore forwarded exports (RVA points outside the export section).
        if (functionRva >= exportDir.VirtualAddress && functionRva < exportDir.VirtualAddress + exportDir.Size) {
            continue;
        }
        if (!callback(name, name, functionRva))
            break;
    }
    return true;
}

} // namespace SymbolTable
