#include "ProcessInjector.h"

#include <cstdint>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "OmniPlatform/OmniPlatform.h"

#include "Utils/Config/LuaConfig.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Process {

bool ProcessInjector::InjectForApp(uint32_t appId, uint32_t processId) {
    if (processId == 0)
        return false;

    auto modules = LuaConfig::GetInjectModules(appId);
    if (modules.empty())
        return false;

    spdlog::info("ProcessInjector: Found {} module(s) to inject for AppID {} (PID: {})", modules.size(), appId,
                 processId);

    bool allSuccess = true;
    for (const auto& mod : modules) {
        if (!InjectModule(processId, mod)) {
            allSuccess = false;
        }
    }
    return allSuccess;
}

bool ProcessInjector::InjectModule(uint32_t processId, const std::string& modulePath) {
    if (processId == 0 || modulePath.empty())
        return false;

    if (!fs::exists(modulePath)) {
        spdlog::warn("ProcessInjector: Target injection module not found on disk: {}", modulePath);
        return false;
    }

#if defined(OMNI_PLATFORM_WINDOWS)
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                      PROCESS_VM_WRITE | PROCESS_VM_READ,
                                  FALSE, static_cast<DWORD>(processId));
    if (!hProcess) {
        spdlog::warn("ProcessInjector: Failed to open target process PID {} (error {})", processId, GetLastError());
        return false;
    }

    // Refuse cross-bitness injection: a 64-bit LoadLibraryW address is invalid
    // inside a WOW64 target and would crash it.
    BOOL targetWow64 = FALSE;
    IsWow64Process(hProcess, &targetWow64);
    BOOL selfWow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &selfWow64);
    if (targetWow64 != selfWow64) {
        spdlog::warn("ProcessInjector: Skipping PID {} - bitness mismatch with Steam process", processId);
        CloseHandle(hProcess);
        return false;
    }

    std::wstring wPath = OmniPlatform::Encoding::Utf8ToWide(fs::absolute(modulePath).generic_string());
    size_t pathSizeBytes = (wPath.length() + 1) * sizeof(wchar_t);

    LPVOID pRemoteMem = VirtualAllocEx(hProcess, nullptr, pathSizeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMem) {
        spdlog::warn("ProcessInjector: Failed to allocate remote memory in target process (error {})", GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, pRemoteMem, wPath.c_str(), pathSizeBytes, nullptr)) {
        spdlog::warn("ProcessInjector: Failed to write module path to target process (error {})", GetLastError());
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel32) {
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    auto pLoadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(hKernel32, "LoadLibraryW"));
    if (!pLoadLibraryW) {
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pLoadLibraryW, pRemoteMem, 0, nullptr);
    if (!hThread) {
        spdlog::warn("ProcessInjector: Failed to create remote thread in target process (error {})", GetLastError());
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    spdlog::info("ProcessInjector: Successfully injected module '{}' into process PID {}", modulePath, processId);
    return true;
#else
    spdlog::info("ProcessInjector: Target injection module registered for PID {}: {}", processId, modulePath);
    return true;
#endif
}

} // namespace Process
