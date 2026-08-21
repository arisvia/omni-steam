#include <cstdint>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <string>
#include <unistd.h>

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
    char buf[1024];
    return getcwd(buf, sizeof(buf)) ? std::string(buf) : "";
}

std::string DynamicLibrary::GetModulePath(ModuleHandle handle) {
    if (!handle)
        return "";
    Dl_info info;
    if (dladdr(handle, &info) && info.dli_fname) {
        return std::string(info.dli_fname);
    }
    return "";
}

uint32_t DynamicLibrary::GetLastErrorCode() {
    return 0;
}
} // namespace OmniPlatform
