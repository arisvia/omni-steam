#include <windows.h>

#include <cctype>
#include <string>

#if defined(_MSC_VER)
#pragma comment(linker, "/EXPORT:DllCanUnloadNow=DWMAPI.DllCanUnloadNow,@111")
#pragma comment(linker, "/EXPORT:DllGetClassObject=DWMAPI.DllGetClassObject,@115")
#pragma comment(linker, "/EXPORT:DwmAttachMilContent=DWMAPI.DwmAttachMilContent,@116")
#pragma comment(linker, "/EXPORT:DwmDefWindowProc=DWMAPI.DwmDefWindowProc,@117")
#pragma comment(linker, "/EXPORT:DwmDetachMilContent=DWMAPI.DwmDetachMilContent,@118")
#pragma comment(linker, "/EXPORT:DwmEnableBlurBehindWindow=DWMAPI.DwmEnableBlurBehindWindow,@119")
#pragma comment(linker, "/EXPORT:DwmEnableComposition=DWMAPI.DwmEnableComposition,@102")
#pragma comment(linker, "/EXPORT:DwmEnableMMCSS=DWMAPI.DwmEnableMMCSS,@120")
#pragma comment(linker, "/EXPORT:DwmExtendFrameIntoClientArea=DWMAPI.DwmExtendFrameIntoClientArea,@121")
#pragma comment(linker, "/EXPORT:DwmFlush=DWMAPI.DwmFlush,@122")
#pragma comment(linker, "/EXPORT:DwmGetColorizationColor=DWMAPI.DwmGetColorizationColor,@123")
#pragma comment(linker, "/EXPORT:DwmGetCompositionTimingInfo=DWMAPI.DwmGetCompositionTimingInfo,@125")
#pragma comment(linker, "/EXPORT:DwmGetGraphicsStreamClient=DWMAPI.DwmGetGraphicsStreamClient,@126")
#pragma comment(linker, "/EXPORT:DwmGetGraphicsStreamTransformHint=DWMAPI.DwmGetGraphicsStreamTransformHint,@129")
#pragma comment(linker, "/EXPORT:DwmGetTransportAttributes=DWMAPI.DwmGetTransportAttributes,@130")
#pragma comment(linker, "/EXPORT:DwmGetUnmetTabRequirements=DWMAPI.DwmGetUnmetTabRequirements,@133")
#pragma comment(linker, "/EXPORT:DwmGetWindowAttribute=DWMAPI.DwmGetWindowAttribute,@134")
#pragma comment(linker, "/EXPORT:DwmInvalidateIconicBitmaps=DWMAPI.DwmInvalidateIconicBitmaps,@149")
#pragma comment(linker, "/EXPORT:DwmIsCompositionEnabled=DWMAPI.DwmIsCompositionEnabled,@188")
#pragma comment(linker, "/EXPORT:DwmModifyPreviousDxFrameDuration=DWMAPI.DwmModifyPreviousDxFrameDuration,@189")
#pragma comment(linker, "/EXPORT:DwmQueryThumbnailSourceSize=DWMAPI.DwmQueryThumbnailSourceSize,@190")
#pragma comment(linker, "/EXPORT:DwmRegisterThumbnail=DWMAPI.DwmRegisterThumbnail,@191")
#pragma comment(linker, "/EXPORT:DwmRenderGesture=DWMAPI.DwmRenderGesture,@192")
#pragma comment(linker, "/EXPORT:DwmSetDxFrameDuration=DWMAPI.DwmSetDxFrameDuration,@193")
#pragma comment(linker, "/EXPORT:DwmSetIconicLivePreviewBitmap=DWMAPI.DwmSetIconicLivePreviewBitmap,@194")
#pragma comment(linker, "/EXPORT:DwmSetIconicThumbnail=DWMAPI.DwmSetIconicThumbnail,@195")
#pragma comment(linker, "/EXPORT:DwmSetPresentParameters=DWMAPI.DwmSetPresentParameters,@196")
#pragma comment(linker, "/EXPORT:DwmSetWindowAttribute=DWMAPI.DwmSetWindowAttribute,@197")
#pragma comment(linker, "/EXPORT:DwmShowContact=DWMAPI.DwmShowContact,@198")
#pragma comment(linker, "/EXPORT:DwmTetherContact=DWMAPI.DwmTetherContact,@199")
#pragma comment(linker, "/EXPORT:DwmTetherTextContact=DWMAPI.DwmTetherTextContact,@156")
#pragma comment(linker, "/EXPORT:DwmTransitionOwnedWindow=DWMAPI.DwmTransitionOwnedWindow,@200")
#pragma comment(linker, "/EXPORT:DwmUnregisterThumbnail=DWMAPI.DwmUnregisterThumbnail,@201")
#pragma comment(linker, "/EXPORT:DwmUpdateThumbnailProperties=DWMAPI.DwmUpdateThumbnailProperties,@202")
#pragma comment(linker, "/EXPORT:DwmpAllocateSecurityDescriptor=DWMAPI.DwmpAllocateSecurityDescriptor,@136")
#pragma comment(linker, "/EXPORT:DwmpDxGetWindowSharedSurface=DWMAPI.DwmpDxGetWindowSharedSurface,@100")
#pragma comment(linker, "/EXPORT:DwmpDxUpdateWindowSharedSurface=DWMAPI.DwmpDxUpdateWindowSharedSurface,@101")
#pragma comment(linker, "/EXPORT:DwmpDxgiIsThreadDesktopComposited=DWMAPI.DwmpDxgiIsThreadDesktopComposited,@128")
#pragma comment(linker, "/EXPORT:DwmpEnableDDASupport=DWMAPI.DwmpEnableDDASupport,@143")
#pragma comment(linker, "/EXPORT:DwmpFreeSecurityDescriptor=DWMAPI.DwmpFreeSecurityDescriptor,@137")
#pragma comment(linker, "/EXPORT:DwmpGetColorizationParameters=DWMAPI.DwmpGetColorizationParameters,@127")
#pragma comment(linker, "/EXPORT:DwmpRenderFlick=DWMAPI.DwmpRenderFlick,@135")
#pragma comment(linker, "/EXPORT:DwmpSetColorizationParameters=DWMAPI.DwmpSetColorizationParameters,@131")
#pragma comment(linker, "/EXPORT:DwmpUpdateProxyWindowForCapture=DWMAPI.DwmpUpdateProxyWindowForCapture,@183")
#endif
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
