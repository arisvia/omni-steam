#include <windows.h>

#include <cctype>
#include <string>

// Function pointers for real DWMAPI functions forwarded to C:\Windows\System32\dwmapi.dll
namespace {
HMODULE g_realDwmapi = nullptr;

FARPROC GetRealFunc(const char* name) {
    if (!g_realDwmapi) {
        char sysPath[MAX_PATH];
        if (GetSystemDirectoryA(sysPath, MAX_PATH)) {
            std::string realDll = std::string(sysPath) + "\\dwmapi.dll";
            g_realDwmapi = LoadLibraryA(realDll.c_str());
        }
    }
    return g_realDwmapi ? GetProcAddress(g_realDwmapi, name) : nullptr;
}

void LoadOmniSteamCore() {
    static bool loaded = false;
    if (loaded) {
        return;
    }
    loaded = true;
    // Inject ONLY into the main Steam client. Every Steam subprocess
    // (steamwebhelper.exe CEF renderers incl. sandboxed ones, steamservice,
    // crash handlers) resolves dwmapi.dll from the app directory; loading
    // the hook core into them corrupts their network/render stacks - store
    // pages and news feeds fail with "载入失败".
    char exePath[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exePath, MAX_PATH)) {
        return;
    }
    std::string exeName(exePath);
    const size_t slash = exeName.find_last_of("\\/");
    exeName = (slash == std::string::npos) ? exeName : exeName.substr(slash + 1);
    for (char& c : exeName) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    if (exeName != "steam.exe") {
        return;
    }
    char modulePath[MAX_PATH];
    if (GetModuleFileNameA(nullptr, modulePath, MAX_PATH)) {
        std::string dir(modulePath);
        size_t pos = dir.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string fullCorePath = dir.substr(0, pos + 1) + "libomnisteam.dll";
            if (LoadLibraryA(fullCorePath.c_str())) {
                return;
            }
        }
    }
    LoadLibraryA("libomnisteam.dll");
}
}
} // namespace

extern "C" {

#define FORWARD_DWM_0(name, retType)                                                                                   \
    __declspec(dllexport) retType WINAPI name() {                                                                      \
        LoadOmniSteamCore();                                                                                           \
        typedef retType(WINAPI * tFunc)();                                                                             \
        static tFunc pFunc = (tFunc)GetRealFunc(#name);                                                                \
        if (pFunc)                                                                                                     \
            return pFunc();                                                                                            \
        return (retType)0;                                                                                             \
    }

#define FORWARD_DWM_1(name, retType, T1, a1)                                                                           \
    __declspec(dllexport) retType WINAPI name(T1 a1) {                                                                 \
        LoadOmniSteamCore();                                                                                           \
        typedef retType(WINAPI * tFunc)(T1);                                                                           \
        static tFunc pFunc = (tFunc)GetRealFunc(#name);                                                                \
        if (pFunc)                                                                                                     \
            return pFunc(a1);                                                                                          \
        return (retType)0;                                                                                             \
    }

#define FORWARD_DWM_2(name, retType, T1, a1, T2, a2)                                                                   \
    __declspec(dllexport) retType WINAPI name(T1 a1, T2 a2) {                                                          \
        LoadOmniSteamCore();                                                                                           \
        typedef retType(WINAPI * tFunc)(T1, T2);                                                                       \
        static tFunc pFunc = (tFunc)GetRealFunc(#name);                                                                \
        if (pFunc)                                                                                                     \
            return pFunc(a1, a2);                                                                                      \
        return (retType)0;                                                                                             \
    }

#define FORWARD_DWM_3(name, retType, T1, a1, T2, a2, T3, a3)                                                           \
    __declspec(dllexport) retType WINAPI name(T1 a1, T2 a2, T3 a3) {                                                   \
        LoadOmniSteamCore();                                                                                           \
        typedef retType(WINAPI * tFunc)(T1, T2, T3);                                                                   \
        static tFunc pFunc = (tFunc)GetRealFunc(#name);                                                                \
        if (pFunc)                                                                                                     \
            return pFunc(a1, a2, a3);                                                                                  \
        return (retType)0;                                                                                             \
    }

#define FORWARD_DWM_4(name, retType, T1, a1, T2, a2, T3, a3, T4, a4)                                                   \
    __declspec(dllexport) retType WINAPI name(T1 a1, T2 a2, T3 a3, T4 a4) {                                            \
        LoadOmniSteamCore();                                                                                           \
        typedef retType(WINAPI * tFunc)(T1, T2, T3, T4);                                                               \
        static tFunc pFunc = (tFunc)GetRealFunc(#name);                                                                \
        if (pFunc)                                                                                                     \
            return pFunc(a1, a2, a3, a4);                                                                              \
        return (retType)0;                                                                                             \
    }

#define FORWARD_DWM_5(name, retType, T1, a1, T2, a2, T3, a3, T4, a4, T5, a5)                                           \
    __declspec(dllexport) retType WINAPI name(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) {                                     \
        LoadOmniSteamCore();                                                                                           \
        typedef retType(WINAPI * tFunc)(T1, T2, T3, T4, T5);                                                           \
        static tFunc pFunc = (tFunc)GetRealFunc(#name);                                                                \
        if (pFunc)                                                                                                     \
            return pFunc(a1, a2, a3, a4, a5);                                                                          \
        return (retType)0;                                                                                             \
    }

FORWARD_DWM_1(DwmAttachMilContent, HRESULT, HWND, hwnd)
FORWARD_DWM_5(DwmDefWindowProc, BOOL, HWND, hWnd, UINT, msg, WPARAM, wParam, LPARAM, lParam, LRESULT*, plResult)
FORWARD_DWM_1(DwmDetachMilContent, HRESULT, HWND, hwnd)
FORWARD_DWM_2(DwmEnableBlurBehindWindow, HRESULT, HWND, hWnd, const void*, pBlurBehind)
FORWARD_DWM_1(DwmEnableComposition, HRESULT, UINT, uCompositionAction)
FORWARD_DWM_1(DwmEnableMMCSS, HRESULT, BOOL, fEnableMMCSS)
FORWARD_DWM_2(DwmExtendFrameIntoClientArea, HRESULT, HWND, hWnd, const void*, pMarInset)
FORWARD_DWM_0(DwmFlush, HRESULT)
FORWARD_DWM_2(DwmGetColorizationColor, HRESULT, DWORD*, pcrColorization, BOOL*, pfOpaqueBlend)
FORWARD_DWM_2(DwmGetCompositionTimingInfo, HRESULT, HWND, hwnd, void*, pTimingInfo)
FORWARD_DWM_2(DwmGetGraphicsStreamClient, HRESULT, UINT, uIndex, void*, pClient)
FORWARD_DWM_2(DwmGetGraphicsStreamTransformHint, HRESULT, UINT, uIndex, void*, pTransform)
FORWARD_DWM_3(DwmGetTransportAttributes, HRESULT, BOOL*, pfIsRemoting, BOOL*, pfIsConnected, DWORD*, pDwGeneration)
FORWARD_DWM_2(DwmGetUnmetTabRequirements, HRESULT, HWND, hwnd, void*, pRequirements)
FORWARD_DWM_4(DwmGetWindowAttribute, HRESULT, HWND, hwnd, DWORD, dwAttribute, PVOID, pvAttribute, DWORD, cbAttribute)
FORWARD_DWM_1(DwmInvalidateIconicBitmaps, HRESULT, HWND, hwnd)
FORWARD_DWM_1(DwmIsCompositionEnabled, HRESULT, BOOL*, pfEnabled)
FORWARD_DWM_3(DwmModifyPreviousDxFrameDuration, HRESULT, HWND, hwnd, INT, cRefreshes, BOOL, fRelative)
FORWARD_DWM_2(DwmQueryThumbnailSourceSize, HRESULT, void*, hThumbnail, void*, pSize)
FORWARD_DWM_3(DwmRegisterThumbnail, HRESULT, HWND, hwndDestination, HWND, hwndSource, void*, phThumbnailId)
FORWARD_DWM_4(DwmRenderGesture, HRESULT, void*, gt, UINT, cPoints, void*, pPoints, void*, pPointsUpdated)
FORWARD_DWM_2(DwmSetColorizationParameters, HRESULT, void*, pParameters, UINT, uUnknown)
FORWARD_DWM_2(DwmSetDxFrameDuration, HRESULT, HWND, hwnd, INT, cRefreshes)
FORWARD_DWM_4(DwmSetIconicLivePreviewBitmap, HRESULT, HWND, hwnd, void*, hbmp, void*, pptClient, DWORD, dwSITFlags)
FORWARD_DWM_3(DwmSetIconicThumbnail, HRESULT, HWND, hwnd, void*, hbmp, DWORD, dwSITFlags)
FORWARD_DWM_2(DwmSetPresentParameters, HRESULT, HWND, hwnd, void*, pPresentParams)
FORWARD_DWM_4(DwmSetWindowAttribute, HRESULT, HWND, hwnd, DWORD, dwAttribute, LPCVOID, pvAttribute, DWORD, cbAttribute)
FORWARD_DWM_2(DwmShowContact, HRESULT, DWORD, dwPointerId, void*, eShowContact)
FORWARD_DWM_3(DwmTetherContact, HRESULT, DWORD, dwPointerId, BOOL, fEnable, void*, pptTether)
FORWARD_DWM_2(DwmTransitionOwnedWindow, HRESULT, HWND, hwnd, void*, target)
FORWARD_DWM_1(DwmUnregisterThumbnail, HRESULT, void*, hThumbnailId)
FORWARD_DWM_2(DwmUpdateThumbnailProperties, HRESULT, void*, hThumbnailId, const void*, ptnProperties)

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadOmniSteamCore();
    }
    return TRUE;
}
