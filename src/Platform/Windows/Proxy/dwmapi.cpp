#include <windows.h>
#include <string>

// Function pointers for real DWMAPI functions forwarded to C:\Windows\System32\dwmapi.dll
namespace {
    HMODULE g_realDwmapi = nullptr;

    FARPROC GetRealFunc(const char* name) {
        if (!g_realDwmapi) {
            char sysPath[MAX_PATH];
            GetSystemDirectoryA(sysPath, MAX_PATH);
            std::string realDll = std::string(sysPath) + "\\dwmapi.dll";
            g_realDwmapi = LoadLibraryA(realDll.c_str());
        }
        return g_realDwmapi ? GetProcAddress(g_realDwmapi, name) : nullptr;
    }
}

// Load OmniSteam Core DLL
static void LoadOmniSteamCore() {
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        LoadLibraryA("libomnisteam.dll");
    }
}

extern "C" {

__declspec(dllexport) HRESULT WINAPI DwmEnableBlurBehindWindow(HWND hWnd, const void* pBlurBehind) {
    LoadOmniSteamCore();
    typedef HRESULT (WINAPI *tFunc)(HWND, const void*);
    static tFunc pFunc = (tFunc)GetRealFunc("DwmEnableBlurBehindWindow");
    return pFunc ? pFunc(hWnd, pBlurBehind) : S_OK;
}

__declspec(dllexport) HRESULT WINAPI DwmExtendFrameIntoClientArea(HWND hWnd, const void* pMarInset) {
    LoadOmniSteamCore();
    typedef HRESULT (WINAPI *tFunc)(HWND, const void*);
    static tFunc pFunc = (tFunc)GetRealFunc("DwmExtendFrameIntoClientArea");
    return pFunc ? pFunc(hWnd, pMarInset) : S_OK;
}

__declspec(dllexport) HRESULT WINAPI DwmGetColorizationColor(DWORD* pcrColorization, BOOL* pfOpaqueBlend) {
    LoadOmniSteamCore();
    typedef HRESULT (WINAPI *tFunc)(DWORD*, BOOL*);
    static tFunc pFunc = (tFunc)GetRealFunc("DwmGetColorizationColor");
    return pFunc ? pFunc(pcrColorization, pfOpaqueBlend) : S_OK;
}

__declspec(dllexport) HRESULT WINAPI DwmGetWindowAttribute(HWND hwnd, DWORD dwAttribute, PVOID pvAttribute, DWORD cbAttribute) {
    LoadOmniSteamCore();
    typedef HRESULT (WINAPI *tFunc)(HWND, DWORD, PVOID, DWORD);
    static tFunc pFunc = (tFunc)GetRealFunc("DwmGetWindowAttribute");
    return pFunc ? pFunc(hwnd, dwAttribute, pvAttribute, cbAttribute) : S_OK;
}

__declspec(dllexport) HRESULT WINAPI DwmIsCompositionEnabled(BOOL* pfEnabled) {
    LoadOmniSteamCore();
    if (pfEnabled) *pfEnabled = TRUE;
    typedef HRESULT (WINAPI *tFunc)(BOOL*);
    static tFunc pFunc = (tFunc)GetRealFunc("DwmIsCompositionEnabled");
    return pFunc ? pFunc(pfEnabled) : S_OK;
}

__declspec(dllexport) HRESULT WINAPI DwmSetWindowAttribute(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute) {
    LoadOmniSteamCore();
    typedef HRESULT (WINAPI *tFunc)(HWND, DWORD, LPCVOID, DWORD);
    static tFunc pFunc = (tFunc)GetRealFunc("DwmSetWindowAttribute");
    return pFunc ? pFunc(hwnd, dwAttribute, pvAttribute, cbAttribute) : S_OK;
}

__declspec(dllexport) BOOL WINAPI DwmDefWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) {
    LoadOmniSteamCore();
    typedef BOOL (WINAPI *tFunc)(HWND, UINT, WPARAM, LPARAM, LRESULT*);
    static tFunc pFunc = (tFunc)GetRealFunc("DwmDefWindowProc");
    return pFunc ? pFunc(hWnd, msg, wParam, lParam, plResult) : FALSE;
}

__declspec(dllexport) HRESULT WINAPI DwmFlush() {
    LoadOmniSteamCore();
    typedef HRESULT (WINAPI *tFunc)();
    static tFunc pFunc = (tFunc)GetRealFunc("DwmFlush");
    return pFunc ? pFunc() : S_OK;
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadOmniSteamCore();
    }
    return TRUE;
}
