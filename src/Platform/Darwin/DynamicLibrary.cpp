#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <string>
#include <unistd.h>

#include "OmniPlatform/OmniPlatform.h"
namespace OmniPlatform {

DynamicLibrary::ModuleHandle DynamicLibrary::Load(const std::string& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

DynamicLibrary::ModuleHandle DynamicLibrary::GetLoadedModule(const std::string& name) {
    // dlopen(RTLD_NOLOAD) on dyld matches by RESOLVED PATH, not basename.
    // Steam loads steamclient.dylib by absolute path outside the library
    // search paths, so a bare-name NOLOAD query returns NULL even when the
    // image is present (run 44258868872: dyld logged the load, core timed
    // out). Enumerate dyld images, match the basename, then NOLOAD the full
    // path to obtain a real handle.
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const char* image = _dyld_get_image_name(i);
        if (image == nullptr) {
            continue;
        }
        const char* base = std::strrchr(image, '/');
        base = (base != nullptr) ? base + 1 : image;
        if (name == base) {
            return dlopen(image, RTLD_NOLOAD | RTLD_NOW);
        }
    }
    return nullptr;
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
