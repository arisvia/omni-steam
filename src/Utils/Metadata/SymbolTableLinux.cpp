// ELF symbol-table enumeration (Linux).
//
// Locates the module's on-disk image via /proc/self/maps, then parses its
// section headers to enumerate .dynsym and (when present) the full .symtab.
// 64-bit only: OmniSteam targets x86_64/arm64, so ELF32 images are rejected.

#include "SymbolTable.h"

#include <cstdint>
#include <cstring>
#include <cxxabi.h>
#include <elf.h>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

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

bool FindModulePath(const std::string& moduleName, std::string& outPath) {
    std::ifstream maps("/proc/self/maps");
    if (!maps)
        return false;
    std::string line;
    while (std::getline(maps, line)) {
        size_t pathStart = line.find('/');
        if (pathStart == std::string::npos)
            continue;
        std::string path = line.substr(pathStart);
        if (ModuleMatches(path, moduleName)) {
            outPath = path;
            return true;
        }
    }
    return false;
}

void ParseSymbolTable(std::ifstream& file, uint64_t symOffset, uint64_t symSize, uint64_t strOffset, uint64_t strSize,
                      const SymbolCallback& callback, const char* tableLabel) {
    if (symSize < sizeof(Elf64_Sym))
        return;

    std::vector<char> symData(static_cast<size_t>(symSize));
    file.seekg(static_cast<std::streamoff>(symOffset));
    if (!file.read(symData.data(), static_cast<std::streamsize>(symData.size())))
        return;

    std::vector<char> strData(static_cast<size_t>(strSize));
    file.seekg(static_cast<std::streamoff>(strOffset));
    if (!file.read(strData.data(), static_cast<std::streamsize>(strData.size())))
        return;

    auto* symbols = reinterpret_cast<const Elf64_Sym*>(symData.data());
    const size_t count = symData.size() / sizeof(Elf64_Sym);
    for (size_t i = 0; i < count; ++i) {
        const Elf64_Sym& sym = symbols[i];
        if (ELF64_ST_TYPE(sym.st_info) != STT_FUNC)
            continue;
        if (sym.st_shndx == SHN_UNDEF || sym.st_value == 0)
            continue;
        if (sym.st_name >= strData.size())
            continue;
        const char* name = strData.data() + sym.st_name;
        if (!*name)
            continue;

        std::string demangled = DemangleItanium(name);
        if (!callback(name, demangled.empty() ? name : demangled, sym.st_value)) {
            spdlog::debug("SymbolTable[{}]: early stop by consumer at symbol #{}", tableLabel, i);
            return;
        }
    }
}

} // namespace

bool ForEachFunction(const std::string& moduleName, const SymbolCallback& callback) {
    std::string modulePath;
    if (!FindModulePath(moduleName, modulePath)) {
        spdlog::warn("SymbolTable: Module {} not found in /proc/self/maps", moduleName);
        return false;
    }

    std::ifstream file(modulePath, std::ios::binary);
    if (!file) {
        spdlog::warn("SymbolTable: Cannot open {}", modulePath);
        return false;
    }

    char e_ident[EI_NIDENT];
    file.read(e_ident, EI_NIDENT);
    if (!file || std::memcmp(e_ident, ELFMAG, SELFMAG) != 0) {
        spdlog::warn("SymbolTable: {} is not an ELF image", modulePath);
        return false;
    }
    if (e_ident[EI_CLASS] != ELFCLASS64) {
        spdlog::warn("SymbolTable: {} is not a 64-bit ELF image; OmniSteam supports 64-bit clients only", modulePath);
        return false;
    }

    Elf64_Ehdr ehdr{};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (!file)
        return false;
    const uint64_t shoff = ehdr.e_shoff;
    const uint16_t shentsize = ehdr.e_shentsize;
    const uint16_t shnum = ehdr.e_shnum;
    const uint16_t shstrndx = ehdr.e_shstrndx;
    if (shoff == 0 || shentsize == 0 || shnum == 0)
        return false;

    std::vector<char> sections(static_cast<size_t>(shentsize) * shnum);
    file.seekg(static_cast<std::streamoff>(shoff));
    if (!file.read(sections.data(), static_cast<std::streamsize>(sections.size())))
        return false;

    auto sectionAt = [&](size_t index) -> const void* {
        if (index >= shnum)
            return nullptr;
        return sections.data() + static_cast<size_t>(shentsize) * index;
    };

    uint64_t strTabOffset = 0, strTabSize = 0;
    {
        const auto* shstr = static_cast<const Elf64_Shdr*>(sectionAt(shstrndx));
        if (!shstr)
            return false;
        strTabOffset = shstr->sh_offset;
        strTabSize = shstr->sh_size;
    }
    std::vector<char> shstrData(static_cast<size_t>(strTabSize));
    file.seekg(static_cast<std::streamoff>(strTabOffset));
    if (!file.read(shstrData.data(), static_cast<std::streamsize>(shstrData.size())))
        return false;

    for (size_t i = 0; i < shnum; ++i) {
        const auto* sh = static_cast<const Elf64_Shdr*>(sectionAt(i));
        if (!sh)
            continue;

        const bool isSymTab = sh->sh_type == SHT_SYMTAB;
        const bool isDynSym = sh->sh_type == SHT_DYNSYM;
        if (!isSymTab && !isDynSym)
            continue;

        const auto* strSec = static_cast<const Elf64_Shdr*>(sectionAt(sh->sh_link));
        if (!strSec)
            continue;

        const char* label = isDynSym ? "dynsym" : "symtab";
        spdlog::debug("SymbolTable: Parsing {} of {} ({} symbols)", label, modulePath, sh->sh_size / sizeof(Elf64_Sym));
        ParseSymbolTable(file, sh->sh_offset, sh->sh_size, strSec->sh_offset, strSec->sh_size, callback, label);
    }
    return true;
}

} // namespace SymbolTable
