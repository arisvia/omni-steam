#include <climits>
#include <dlfcn.h>
#include <link.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

DynamicLibrary::ModuleHandle DynamicLibrary::Load(const std::string& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

DynamicLibrary::ModuleHandle DynamicLibrary::GetLoadedModule(const std::string& name) {
    return dlopen(name.c_str(), RTLD_NOLOAD | RTLD_NOW);
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
