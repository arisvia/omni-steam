// ELF symbol-table enumeration (Linux).
//
// Locates the module's on-disk image via /proc/self/maps, then parses its
// section headers to enumerate .dynsym and (when present) the full .symtab.
// Handles both ELF64 and ELF32 (the ubuntu12_32 client build).

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

template <typename ElfEhdr, typename ElfShdr, typename ElfSym>
void ParseSymbolTable(std::ifstream& file, uint64_t symOffset, uint64_t symSize, uint64_t strOffset, uint64_t strSize,
                      const SymbolCallback& callback, const char* tableLabel) {
    if (symSize < sizeof(ElfSym))
        return;

    std::vector<char> symData(static_cast<size_t>(symSize));
    file.seekg(static_cast<std::streamoff>(symOffset));
    if (!file.read(symData.data(), static_cast<std::streamsize>(symData.size())))
        return;

    std::vector<char> strData(static_cast<size_t>(strSize));
    file.seekg(static_cast<std::streamoff>(strOffset));
    if (!file.read(strData.data(), static_cast<std::streamsize>(strData.size())))
        return;

    auto* symbols = reinterpret_cast<const ElfSym*>(symData.data());
    const size_t count = symData.size() / sizeof(ElfSym);
    for (size_t i = 0; i < count; ++i) {
        const ElfSym& sym = symbols[i];
        constexpr unsigned char kTypeMask = 0xF;
        if (ELF32_ST_TYPE(sym.st_info) != STT_FUNC)
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
    const bool is64 = e_ident[EI_CLASS] == ELFCLASS64;

    uint64_t shoff = 0;
    uint16_t shentsize = 0, shnum = 0, shstrndx = 0;
    if (is64) {
        Elf64_Ehdr ehdr{};
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
        shoff = ehdr.e_shoff;
        shentsize = ehdr.e_shentsize;
        shnum = ehdr.e_shnum;
        shstrndx = ehdr.e_shstrndx;
    } else {
        Elf32_Ehdr ehdr{};
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
        shoff = ehdr.e_shoff;
        shentsize = ehdr.e_shentsize;
        shnum = ehdr.e_shnum;
        shstrndx = ehdr.e_shstrndx;
    }
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
        const Elf64_Shdr* shstr64 = static_cast<const Elf64_Shdr*>(sectionAt(shstrndx));
        const Elf32_Shdr* shstr32 = static_cast<const Elf32_Shdr*>(sectionAt(shstrndx));
        if (is64 && shstr64) {
            strTabOffset = shstr64->sh_offset;
            strTabSize = shstr64->sh_size;
        } else if (shstr32) {
            strTabOffset = shstr32->sh_offset;
            strTabSize = shstr32->sh_size;
        }
    }
    std::vector<char> shstrData(static_cast<size_t>(strTabSize));
    file.seekg(static_cast<std::streamoff>(strTabOffset));
    if (!file.read(shstrData.data(), static_cast<std::streamsize>(shstrData.size())))
        return false;

    for (size_t i = 0; i < shnum; ++i) {
        uint32_t type = 0, link = 0;
        uint64_t offset = 0, size = 0;
        if (is64) {
            const auto* sh = static_cast<const Elf64_Shdr*>(sectionAt(i));
            if (!sh)
                continue;
            type = sh->sh_type;
            link = sh->sh_link;
            offset = sh->sh_offset;
            size = sh->sh_size;
        } else {
            const auto* sh = static_cast<const Elf32_Shdr*>(sectionAt(i));
            if (!sh)
                continue;
            type = sh->sh_type;
            link = sh->sh_link;
            offset = sh->sh_offset;
            size = sh->sh_size;
        }

        const bool isSymTab = type == SHT_SYMTAB;
        const bool isDynSym = type == SHT_DYNSYM;
        if (!isSymTab && !isDynSym)
            continue;

        uint64_t strOffset = 0, strSize = 0;
        if (is64) {
            const auto* strSec = static_cast<const Elf64_Shdr*>(sectionAt(link));
            if (!strSec)
                continue;
            strOffset = strSec->sh_offset;
            strSize = strSec->sh_size;
        } else {
            const auto* strSec = static_cast<const Elf32_Shdr*>(sectionAt(link));
            if (!strSec)
                continue;
            strOffset = strSec->sh_offset;
            strSize = strSec->sh_size;
        }

        const char* label = isDynSym ? "dynsym" : "symtab";
        spdlog::debug("SymbolTable: Parsing {} of {} ({} symbols)", label, modulePath,
                      size / (is64 ? sizeof(Elf64_Sym) : sizeof(Elf32_Sym)));
        if (is64) {
            ParseSymbolTable<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>(file, offset, size, strOffset, strSize, callback,
                                                                label);
        } else {
            ParseSymbolTable<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(file, offset, size, strOffset, strSize, callback,
                                                                label);
        }
    }
    return true;
}

} // namespace SymbolTable
