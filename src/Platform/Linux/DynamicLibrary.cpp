#include <algorithm>
#include <climits>
#include <cstdint>
#include <dlfcn.h>
#include <fstream>
#include <link.h>
#include <spdlog/spdlog.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {
namespace {

// Distinct file paths mapped into this process whose basename equals `name`.
std::vector<std::string> MappedPathsNamed(const std::string& name) {
    std::vector<std::string> out;
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        const size_t start = line.find('/');
        if (start == std::string::npos) {
            continue; // anonymous / heap mapping
        }
        std::string path = line.substr(start);
        // Unlinked-but-still-mapped objects are reported as "/path (deleted)".
        static const std::string kDeleted = " (deleted)";
        if (path.size() > kDeleted.size() &&
            path.compare(path.size() - kDeleted.size(), kDeleted.size(), kDeleted) == 0) {
            path.resize(path.size() - kDeleted.size());
        }
        const size_t base = path.find_last_of('/');
        const std::string leaf = (base == std::string::npos) ? path : path.substr(base + 1);
        if (leaf != name) {
            continue;
        }
        if (std::find(out.begin(), out.end(), path) == out.end()) {
            out.push_back(std::move(path));
        }
    }
    return out;
}

} // namespace

DynamicLibrary::ModuleHandle DynamicLibrary::Load(const std::string& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

DynamicLibrary::ModuleHandle DynamicLibrary::GetLoadedModule(const std::string& name) {
    if (ModuleHandle handle = dlopen(name.c_str(), RTLD_NOLOAD | RTLD_NOW)) {
        return handle;
    }
    // RTLD_NOLOAD resolves a name only against a loaded object's DT_SONAME or the
    // exact string it was loaded with - and Valve's steamclient.so carries NO
    // soname (verified on the current client), while Steam dlopens it by resolved
    // path. The bare-name probe can therefore stay empty forever even though the
    // module is mapped; retry with the absolute paths from /proc/self/maps. This
    // is the same trap the Darwin side fixed by enumerating _dyld images.
    for (const std::string& path : MappedPathsNamed(name)) {
        if (ModuleHandle handle = dlopen(path.c_str(), RTLD_NOLOAD | RTLD_NOW)) {
            return handle;
        }
    }
    return nullptr;
}

std::string DynamicLibrary::GetLoadedModulePath(const std::string& name) {
    return GetModulePath(GetLoadedModule(name));
}

void* DynamicLibrary::GetFunction(ModuleHandle handle, const std::string& name) {
    return handle ? dlsym(handle, name.c_str()) : nullptr;
}

bool DynamicLibrary::Free(ModuleHandle handle) {
    return handle ? dlclose(handle) == 0 : false;
}

std::string DynamicLibrary::GetCurrentDirectoryPath() {
    char buffer[PATH_MAX];
    return getcwd(buffer, sizeof(buffer)) ? std::string(buffer) : "";
}

std::string DynamicLibrary::GetModulePath(ModuleHandle handle) {
    if (!handle)
        return "";
    struct link_map* lm = nullptr;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) == 0 && lm && lm->l_name) {
        return std::string(lm->l_name);
    }
    return "";
}

uint32_t DynamicLibrary::GetLastErrorCode() {
    return 0;
}

} // namespace OmniPlatform
