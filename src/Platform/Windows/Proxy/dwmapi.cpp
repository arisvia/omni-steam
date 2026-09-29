#include <windows.h>

#include <cctype>
#include <string>

namespace {
void LoadOmniSteamCore() {
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;

    char exePath[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exePath, MAX_PATH))
        return;

    std::string exeName(exePath);
    const size_t slash = exeName.find_last_of("\\/");
    exeName = (slash == std::string::npos) ? exeName : exeName.substr(slash + 1);
    for (char& c : exeName) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    if (exeName != "steam.exe") {
        return; // Never inject into steamwebhelper.exe
    }

    char modulePath[MAX_PATH];
    if (GetModuleFileNameA(nullptr, modulePath, MAX_PATH)) {
        std::string dir(modulePath);
        size_t pos = dir.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string fullCorePath = dir.substr(0, pos + 1) + "libomnisteam.dll";
            if (LoadLibraryA(fullCorePath.c_str()))
                return;
        }
    }
    LoadLibraryA("libomnisteam.dll");
}
} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID /*lpReserved*/) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Pin ourselves for the process lifetime. The host (steam.exe) may
        // LoadLibrary/FreeLibrary-cycle dwmapi.dll during bootstrap; without
        // the pin a stray FreeLibrary unmaps this module while our detached
        // loader thread below is still executing inside it -> 0xC0000005 in
        // "dwmapi.dll_unloaded". A pinned module can never be unloaded.
        HMODULE pinnedSelf = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&LoadOmniSteamCore), &pinnedSelf);

        HANDLE hThread = CreateThread(
            nullptr, 0,
            [](LPVOID) -> DWORD {
                LoadOmniSteamCore();
                return 0;
            },
            nullptr, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
        }
    }
    return TRUE;
}
