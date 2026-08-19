#include <windows.h>
#include <psapi.h>
#include <vector>
#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

DynamicLibrary::ModuleHandle DynamicLibrary::Load(const std::string& path) {
    return LoadLibraryA(path.c_str());
}

DynamicLibrary::ModuleHandle DynamicLibrary::GetLoadedModule(const std::string& name) {
    return GetModuleHandleA(name.empty() ? nullptr : name.c_str());
}

void* DynamicLibrary::GetFunction(ModuleHandle handle, const std::string& name) {
    return handle ? reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name.c_str())) : nullptr;
}

bool DynamicLibrary::Free(ModuleHandle handle) {
    return handle ? FreeLibrary(reinterpret_cast<HMODULE>(handle)) != FALSE : false;
}

std::string DynamicLibrary::GetCurrentDirectoryPath() {
    char buf[MAX_PATH];
    return GetCurrentDirectoryA(MAX_PATH, buf) ? std::string(buf) : "";
}

std::string DynamicLibrary::GetModulePath(ModuleHandle handle) {
    char buf[MAX_PATH];
    return GetModuleFileNameA(reinterpret_cast<HMODULE>(handle), buf, MAX_PATH) ? std::string(buf) : "";
}

uint32_t DynamicLibrary::GetLastErrorCode() {
    return GetLastError();
}

bool Memory::Protect(void* address, size_t size, uint32_t newProtect, uint32_t* oldProtect) {
    DWORD oldP = 0;
    BOOL res = VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldP);
    if (oldProtect)
        *oldProtect = oldP;
    return res != FALSE;
}

bool Memory::Read(void* address, void* buffer, size_t size) {
    if (!address || !buffer || size == 0)
        return false;
    SIZE_T readBytes = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, buffer, size, &readBytes) != FALSE;
}

bool Memory::Write(void* address, const void* buffer, size_t size) {
    if (!address || !buffer || size == 0)
        return false;
    SIZE_T writtenBytes = 0;
    DWORD oldProtect = 0;
    VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    BOOL res = WriteProcessMemory(GetCurrentProcess(), address, buffer, size, &writtenBytes);
    VirtualProtect(address, size, oldProtect, &oldProtect);
    return res != FALSE;
}

bool BinaryParser::GetModuleTextSection(const std::string& moduleName, uintptr_t& outStart, size_t& outSize) {
    HMODULE hMod = GetModuleHandleA(moduleName.empty() ? nullptr : moduleName.c_str());
    if (!hMod)
        return false;
    MODULEINFO modInfo;
    if (GetModuleInformation(GetCurrentProcess(), hMod, &modInfo, sizeof(modInfo))) {
        outStart = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
        outSize = modInfo.SizeOfImage;
        return true;
    }
    return false;
}

uintptr_t BinaryParser::GetModuleBase(const std::string& moduleName) {
    return reinterpret_cast<uintptr_t>(GetModuleHandleA(moduleName.empty() ? nullptr : moduleName.c_str()));
}

std::vector<BinaryParser::SectionInfo> BinaryParser::GetSections(const std::string& modulePath) {
    return {};
}

uint32_t Process::GetCurrentProcessId() {
    return ::GetCurrentProcessId();
}

std::string Process::GetProcessName(uint32_t pid) {
    return "steam.exe";
}

std::string Process::GetExecutablePath() {
    char buf[MAX_PATH];
    return GetModuleFileNameA(nullptr, buf, MAX_PATH) ? std::string(buf) : "";
}

void Thread::StartDetached(std::function<void()> task) {
    CreateThread(
        nullptr, 0,
        [](LPVOID param) -> DWORD {
            auto* fn = reinterpret_cast<std::function<void()>*>(param);
            (*fn)();
            delete fn;
            return 0;
        },
        new std::function<void()>(task), 0, nullptr);
}

void Thread::Sleep(uint32_t milliseconds) {
    ::Sleep(milliseconds);
}

} // namespace OmniPlatform
