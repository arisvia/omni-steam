#include <dlfcn.h>
#include <mach-o/dyld.h>

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
    return "";
}

std::string DynamicLibrary::GetModulePath(ModuleHandle handle) {
    return "";
}

uint32_t DynamicLibrary::GetLastErrorCode() {
    return 0;
}

} // namespace OmniPlatform
