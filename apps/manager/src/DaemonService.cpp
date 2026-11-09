#include "DaemonService.h"

#include "CloudSaveManager.h"
#include "ConfigManager.h"
#include "WebServer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <vector>

#include "OmniPlatform/OmniPaths.h"
#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>

#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace Manager {

namespace {

std::atomic<bool> g_daemonRunning{false};

#if defined(OMNI_PLATFORM_WINDOWS)
constexpr UINT kTrayCallback = WM_APP + 1;
constexpr UINT kTrayOpen = 1;
constexpr UINT kTrayExit = 2;
constexpr char kTrayWindowClass[] = "OmniSteamTrayWindow";

UINT g_taskbarCreated = 0;
uint16_t g_webPort = 8080;
HWND g_trayWindow = nullptr;
NOTIFYICONDATAA g_trayIcon{};
bool g_trayIconAdded = false;

void OpenDashboard() {
    std::string url = "http://127.0.0.1:" + std::to_string(g_webPort);
    HINSTANCE result = ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        spdlog::warn("DaemonService: Failed to open dashboard {}", url);
    }
}

bool AddTrayIcon() {
    if (g_trayIconAdded)
        return true;
    if (!g_trayWindow)
        return false;

    g_trayIcon = {};
    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = g_trayWindow;
    g_trayIcon.uID = 1;
    g_trayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_trayIcon.uCallbackMessage = kTrayCallback;
    g_trayIcon.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    lstrcpynA(g_trayIcon.szTip, "OmniSteam Manager", static_cast<int>(sizeof(g_trayIcon.szTip)));
    g_trayIconAdded = Shell_NotifyIconA(NIM_ADD, &g_trayIcon) == TRUE;
    return g_trayIconAdded;
}

void RemoveTrayIcon() {
    if (!g_trayIconAdded)
        return;
    Shell_NotifyIconA(NIM_DELETE, &g_trayIcon);
    g_trayIconAdded = false;
}

void ShowTrayMenu(HWND hwnd) {
    POINT point{};
    if (!GetCursorPos(&point))
        return;
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    AppendMenuA(menu, MF_STRING, kTrayOpen, "Open Dashboard");
    AppendMenuA(menu, MF_STRING, kTrayExit, "Exit");
    SetForegroundWindow(hwnd);
    int command =
        static_cast<int>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr));
    PostMessageA(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command == static_cast<int>(kTrayOpen)) {
        OpenDashboard();
    } else if (command == static_cast<int>(kTrayExit)) {
        g_daemonRunning.store(false);
        DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK TrayWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarCreated != 0 && message == g_taskbarCreated) {
        g_trayIconAdded = false;
        AddTrayIcon();
        return 0;
    }
    switch (message) {
        case kTrayCallback:
            if (LOWORD(lParam) == WM_LBUTTONUP) {
                OpenDashboard();
            } else if (LOWORD(lParam) == WM_RBUTTONUP) {
                ShowTrayMenu(hwnd);
            }
            return 0;
        case WM_CONTEXTMENU:
            ShowTrayMenu(hwnd);
            return 0;
        case WM_QUERYENDSESSION:
            g_daemonRunning.store(false);
            PostQuitMessage(0);
            return TRUE;
        case WM_CLOSE:
            g_daemonRunning.store(false);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            RemoveTrayIcon();
            g_trayWindow = nullptr;
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(hwnd, message, wParam, lParam);
    }
}

void RunTrayLoop(uint16_t webPort) {
    g_webPort = webPort;
    g_taskbarCreated = RegisterWindowMessageA("TaskbarCreated");

    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = TrayWindowProc;
    windowClass.hInstance = GetModuleHandleA(nullptr);
    windowClass.lpszClassName = kTrayWindowClass;
    if (!RegisterClassExA(&windowClass)) {
        spdlog::warn("DaemonService: Failed to register tray window class");
        return;
    }

    g_trayWindow = CreateWindowExA(0, kTrayWindowClass, "OmniSteam Tray", 0, 0, 0, 0, 0, nullptr, nullptr,
                                   windowClass.hInstance, nullptr);
    if (!g_trayWindow) {
        spdlog::warn("DaemonService: Failed to create tray window");
        return;
    }

    if (AddTrayIcon()) {
        spdlog::info("DaemonService: Windows tray icon created");
    } else {
        spdlog::warn("DaemonService: Tray icon unavailable; retrying while daemon is running");
    }

    MSG msg{};
    bool quit = false;
    while (g_daemonRunning.load() && WebServer::IsRunning()) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (quit || !g_daemonRunning.load() || !WebServer::IsRunning())
            break;
        if (!g_trayIconAdded)
            AddTrayIcon();
        Sleep(200);
    }

    RemoveTrayIcon();
    if (g_trayWindow) {
        DestroyWindow(g_trayWindow);
        g_trayWindow = nullptr;
    }
    UnregisterClassA(kTrayWindowClass, windowClass.hInstance);
}
#endif

std::string GetEventsDirectory() {
    return (fs::path(OmniPlatform::Paths::GetCacheDirectory()) / "events" / "game_exit").generic_string();
}

void ProcessPendingGameExitEvents() {
    std::string dir = GetEventsDirectory();
    if (!fs::exists(dir)) {
        return;
    }

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        std::string filePath = entry.path().generic_string();
        std::ifstream file(filePath);
        if (!file) {
            continue;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        // Extract "appId": 12345
        static const std::regex appIdRe(R"regex("appId"\s*:\s*(\d+))regex");
        std::smatch m;
        if (std::regex_search(content, m, appIdRe)) {
            try {
                uint32_t appId = static_cast<uint32_t>(std::stoul(m[1].str()));
                spdlog::info("DaemonService: Consuming game exit event for AppID {}", appId);

                // Run save backup gracefully
                auto config = ConfigManager::ReadConfig();
                WebDavConfig webdav;
                if (config.cloudEnabled && !config.webdavServerUrl.empty()) {
                    webdav.serverUrl = config.webdavServerUrl;
                    webdav.username = config.webdavUsername;
                    webdav.password = config.webdavPassword;
                    webdav.remoteRootPath = config.webdavRemoteRoot;
                }
                CloudSaveManager::BackupAppSaves(appId, webdav);
            } catch (const std::exception& e) {
                spdlog::warn("DaemonService: Failed to parse event {}: {}", filePath, e.what());
            }
        }

        // Remove consumed event file
        fs::remove(filePath, ec);
    }
}

void EventPollLoop() {
    while (g_daemonRunning.load()) {
        try {
            ProcessPendingGameExitEvents();
        } catch (...) {
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

} // namespace

void DaemonService::RefreshAutostart() {
    std::string currentExe = OmniPlatform::Process::GetExecutablePath();
    if (currentExe.empty()) {
        return;
    }

#if defined(OMNI_PLATFORM_WINDOWS)
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE,
                      &hKey) == ERROR_SUCCESS) {
        std::string cmd = "\"" + currentExe + "\" daemon";
        RegSetValueExA(hKey, "OmniSteamDaemon", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>(cmd.size() + 1));
        RegCloseKey(hKey);
    }
#else
    const char* home = std::getenv("HOME");
    if (!home)
        return;
    std::string autostartDir = std::string(home) + "/.config/autostart";
    std::error_code ec;
    fs::create_directories(autostartDir, ec);
    std::string desktopFile = autostartDir + "/omnisteam-daemon.desktop";
    std::ofstream out(desktopFile);
    if (out) {
        out << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=OmniSteam Daemon\n"
            << "Exec=" << currentExe << " daemon\n"
            << "Hidden=false\n"
            << "X-GNOME-Autostart-enabled=true\n";
    }
#endif
}

int DaemonService::RunDaemon(uint16_t webPort) {
    if (g_daemonRunning.load()) {
        return 0;
    }
    g_daemonRunning.store(true);

    spdlog::info("DaemonService: Starting OmniSteam Daemon on web port {}...", webPort);

    // 1. Start event polling thread to consume <cache>/events/game_exit/*.json
    std::thread pollThread(EventPollLoop);

    // 2. Start WebServer, then keep the daemon alive through the Windows tray.
    bool serverStarted = WebServer::Start("127.0.0.1", webPort);
#if defined(OMNI_PLATFORM_WINDOWS)
    if (serverStarted) {
        RunTrayLoop(webPort);
    }
#endif
    while (g_daemonRunning.load() && WebServer::IsRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    g_daemonRunning.store(false);
    if (pollThread.joinable()) {
        pollThread.join();
    }
    WebServer::Stop();
    spdlog::info("DaemonService: OmniSteam Daemon stopped.");
    return 0;
}

} // namespace Manager
