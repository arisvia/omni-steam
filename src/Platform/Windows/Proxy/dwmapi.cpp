#include <windows.h>

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
    if (!loaded) {
        loaded = true;
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

#define FORWARD_DWM(name, retType, ...)                                                                                \
    __declspec(dllexport) retType WINAPI name(__VA_ARGS__) {                                                           \
        LoadOmniSteamCore();                                                                                           \
        typedef retType(WINAPI * tFunc)(__VA_ARGS__);                                                                  \
        static tFunc pFunc = (tFunc)GetRealFunc(#name);                                                                \
        if (pFunc)                                                                                                     \
            return pFunc(__VA_ARGS__);                                                                                 \
        return (retType)0;                                                                                             \
    }

FORWARD_DWM(DwmAttachMilContent, HRESULT, HWND hwnd)
FORWARD_DWM(DwmDefWindowProc, BOOL, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* plResult)
FORWARD_DWM(DwmDetachMilContent, HRESULT, HWND hwnd)
FORWARD_DWM(DwmEnableBlurBehindWindow, HRESULT, HWND hWnd, const void* pBlurBehind)
FORWARD_DWM(DwmEnableComposition, HRESULT, UINT uCompositionAction)
FORWARD_DWM(DwmEnableMMCSS, HRESULT, BOOL fEnableMMCSS)
FORWARD_DWM(DwmExtendFrameIntoClientArea, HRESULT, HWND hWnd, const void* pMarInset)
FORWARD_DWM(DwmFlush, HRESULT)
FORWARD_DWM(DwmGetColorizationColor, HRESULT, DWORD* pcrColorization, BOOL* pfOpaqueBlend)
FORWARD_DWM(DwmGetCompositionTimingInfo, HRESULT, HWND hwnd, void* pTimingInfo)
FORWARD_DWM(DwmGetGraphicsStreamClient, HRESULT, UINT uIndex, void* pClient)
FORWARD_DWM(DwmGetGraphicsStreamTransformHint, HRESULT, UINT uIndex, void* pTransform)
FORWARD_DWM(DwmGetTransportAttributes, HRESULT, BOOL* pfIsRemoting, BOOL* pfIsConnected, DWORD* pDwGeneration)
FORWARD_DWM(DwmGetUnmetTabRequirements, HRESULT, HWND hwnd, void* pRequirements)
FORWARD_DWM(DwmGetWindowAttribute, HRESULT, HWND hwnd, DWORD dwAttribute, PVOID pvAttribute, DWORD cbAttribute)
FORWARD_DWM(DwmInvalidateIconicBitmaps, HRESULT, HWND hwnd)
FORWARD_DWM(DwmIsCompositionEnabled, HRESULT, BOOL* pfEnabled)
FORWARD_DWM(DwmModifyPreviousDxFrameDuration, HRESULT, HWND hwnd, INT cRefreshes, BOOL fRelative)
FORWARD_DWM(DwmQueryThumbnailSourceSize, HRESULT, void* hThumbnail, void* pSize)
FORWARD_DWM(DwmRegisterThumbnail, HRESULT, HWND hwndDestination, HWND hwndSource, void* phThumbnailId)
FORWARD_DWM(DwmRenderGesture, HRESULT, void* gt, UINT cPoints, void* pPoints, void* pPointsUpdated)
FORWARD_DWM(DwmSetColorizationParameters, HRESULT, void* pParameters, UINT uUnknown)
FORWARD_DWM(DwmSetDxFrameDuration, HRESULT, HWND hwnd, INT cRefreshes)
FORWARD_DWM(DwmSetIconicLivePreviewBitmap, HRESULT, HWND hwnd, void* hbmp, void* pptClient, DWORD dwSITFlags)
FORWARD_DWM(DwmSetIconicThumbnail, HRESULT, HWND hwnd, void* hbmp, DWORD dwSITFlags)
FORWARD_DWM(DwmSetPresentParameters, HRESULT, HWND hwnd, void* pPresentParams)
FORWARD_DWM(DwmSetWindowAttribute, HRESULT, HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute)
FORWARD_DWM(DwmShowContact, HRESULT, DWORD dwPointerId, void* eShowContact)
FORWARD_DWM(DwmTetherContact, HRESULT, DWORD dwPointerId, BOOL fEnable, void* pptTether)
FORWARD_DWM(DwmTransitionOwnedWindow, HRESULT, HWND hwnd, void* target)
FORWARD_DWM(DwmUnregisterThumbnail, HRESULT, void* hThumbnailId)
FORWARD_DWM(DwmUpdateThumbnailProperties, HRESULT, void* hThumbnailId, const void* ptnProperties)

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadOmniSteamCore();
    }
    return TRUE;
}
