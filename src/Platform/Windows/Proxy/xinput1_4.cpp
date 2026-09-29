#include <windows.h>

#include <cctype>
#include <string>

namespace {

static HMODULE g_hRealXInput = nullptr;

typedef DWORD(WINAPI* XInputGetState_t)(DWORD, void*);
typedef DWORD(WINAPI* XInputSetState_t)(DWORD, void*);
typedef DWORD(WINAPI* XInputGetCapabilities_t)(DWORD, DWORD, void*);
typedef void(WINAPI* XInputEnable_t)(BOOL);
typedef DWORD(WINAPI* XInputGetAudioDeviceIds_t)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);
typedef DWORD(WINAPI* XInputGetBatteryInformation_t)(DWORD, BYTE, void*);
typedef DWORD(WINAPI* XInputGetKeystroke_t)(DWORD, DWORD, void*);

static XInputGetState_t o_XInputGetState = nullptr;
static XInputSetState_t o_XInputSetState = nullptr;
static XInputGetCapabilities_t o_XInputGetCapabilities = nullptr;
static XInputEnable_t o_XInputEnable = nullptr;
static XInputGetAudioDeviceIds_t o_XInputGetAudioDeviceIds = nullptr;
static XInputGetBatteryInformation_t o_XInputGetBatteryInformation = nullptr;
static XInputGetKeystroke_t o_XInputGetKeystroke = nullptr;

// Undocumented Ordinals for Big Picture / Guide Button
static FARPROC o_100 = nullptr;
static FARPROC o_101 = nullptr;
static FARPROC o_102 = nullptr;
static FARPROC o_103 = nullptr;
static FARPROC o_104 = nullptr;
static FARPROC o_108 = nullptr;

void LoadRealXInput() {
    if (g_hRealXInput)
        return;

    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    std::string realPath = std::string(sysDir) + "\\xinput1_4.dll";

    g_hRealXInput = LoadLibraryA(realPath.c_str());
    if (g_hRealXInput) {
        o_XInputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(g_hRealXInput, "XInputGetState"));
        o_XInputSetState = reinterpret_cast<XInputSetState_t>(GetProcAddress(g_hRealXInput, "XInputSetState"));
        o_XInputGetCapabilities =
            reinterpret_cast<XInputGetCapabilities_t>(GetProcAddress(g_hRealXInput, "XInputGetCapabilities"));
        o_XInputEnable = reinterpret_cast<XInputEnable_t>(GetProcAddress(g_hRealXInput, "XInputEnable"));
        o_XInputGetAudioDeviceIds =
            reinterpret_cast<XInputGetAudioDeviceIds_t>(GetProcAddress(g_hRealXInput, "XInputGetAudioDeviceIds"));
        o_XInputGetBatteryInformation = reinterpret_cast<XInputGetBatteryInformation_t>(
            GetProcAddress(g_hRealXInput, "XInputGetBatteryInformation"));
        o_XInputGetKeystroke =
            reinterpret_cast<XInputGetKeystroke_t>(GetProcAddress(g_hRealXInput, "XInputGetKeystroke"));

        o_100 = GetProcAddress(g_hRealXInput, MAKEINTRESOURCEA(100));
        o_101 = GetProcAddress(g_hRealXInput, MAKEINTRESOURCEA(101));
        o_102 = GetProcAddress(g_hRealXInput, MAKEINTRESOURCEA(102));
        o_103 = GetProcAddress(g_hRealXInput, MAKEINTRESOURCEA(103));
        o_104 = GetProcAddress(g_hRealXInput, MAKEINTRESOURCEA(104));
        o_108 = GetProcAddress(g_hRealXInput, MAKEINTRESOURCEA(108));
    }
}

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

extern "C" {

DWORD WINAPI XInputGetState(DWORD dwUserIndex, void* pState) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_XInputGetState ? o_XInputGetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputSetState(DWORD dwUserIndex, void* pVibration) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_XInputSetState ? o_XInputSetState(dwUserIndex, pVibration) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, void* pCapabilities) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_XInputGetCapabilities ? o_XInputGetCapabilities(dwUserIndex, dwFlags, pCapabilities)
                                   : ERROR_DEVICE_NOT_CONNECTED;
}

void WINAPI XInputEnable(BOOL enable) {
    if (!g_hRealXInput)
        LoadRealXInput();
    if (o_XInputEnable)
        o_XInputEnable(enable);
}

DWORD WINAPI XInputGetAudioDeviceIds(DWORD dwUserIndex, LPWSTR pRenderDeviceId, UINT* pRenderCount,
                                     LPWSTR pCaptureDeviceId, UINT* pCaptureCount) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_XInputGetAudioDeviceIds
               ? o_XInputGetAudioDeviceIds(dwUserIndex, pRenderDeviceId, pRenderCount, pCaptureDeviceId, pCaptureCount)
               : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, void* pBatteryInformation) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_XInputGetBatteryInformation ? o_XInputGetBatteryInformation(dwUserIndex, devType, pBatteryInformation)
                                         : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, void* pKeystroke) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_XInputGetKeystroke ? o_XInputGetKeystroke(dwUserIndex, dwReserved, pKeystroke)
                                : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputOrdinal100(DWORD a1, void* a2) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_100 ? (reinterpret_cast<DWORD(WINAPI*)(DWORD, void*)>(o_100))(a1, a2) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputOrdinal101(DWORD a1, DWORD a2, void* a3) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_101 ? (reinterpret_cast<DWORD(WINAPI*)(DWORD, DWORD, void*)>(o_101))(a1, a2, a3)
                 : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputOrdinal102(DWORD a1) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_102 ? (reinterpret_cast<DWORD(WINAPI*)(DWORD)>(o_102))(a1) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputOrdinal103(DWORD a1) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_103 ? (reinterpret_cast<DWORD(WINAPI*)(DWORD)>(o_103))(a1) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputOrdinal104(DWORD a1, void* a2) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_104 ? (reinterpret_cast<DWORD(WINAPI*)(DWORD, void*)>(o_104))(a1, a2) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputOrdinal108(DWORD a1, void* a2, void* a3, void* a4, void* a5) {
    if (!g_hRealXInput)
        LoadRealXInput();
    return o_108 ? (reinterpret_cast<DWORD(WINAPI*)(DWORD, void*, void*, void*, void*)>(o_108))(a1, a2, a3, a4, a5)
                 : ERROR_DEVICE_NOT_CONNECTED;
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID /*lpReserved*/) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

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
