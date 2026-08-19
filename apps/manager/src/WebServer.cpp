#include "WebServer.h"

#include <atomic>
#include <cstring>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>
#include <thread>

#include "OmniPlatform/OmniPlatform.h"

#include "DepotKeyStore.h"
#include "ScriptManager.h"
#include "SteamApi.h"
#if defined(OMNI_PLATFORM_WINDOWS)
#include <winsock2.h>

#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace Manager {

namespace {
std::atomic<bool> g_serverRunning{false};
std::thread g_serverThread;
SOCKET g_listenSocket = INVALID_SOCKET;

const char* kIndexHtml = R"rawhtml(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <title>OmniSteam Manager</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        :root { --bg: #0f172a; --card: #1e293b; --accent: #38bdf8; --text: #f8fafc; --muted: #94a3b8; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 24px; }
        .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #334155; padding-bottom: 16px; margin-bottom: 24px; }
        .logo { font-size: 24px; font-weight: 700; color: var(--accent); }
        .container { display: grid; grid-template-columns: 1fr 1fr; gap: 24px; }
        .card { background: var(--card); border-radius: 12px; padding: 20px; border: 1px solid #334155; }
        input, button { padding: 10px 14px; border-radius: 6px; border: 1px solid #475569; background: #0f172a; color: #fff; font-size: 14px; }
        input { flex: 1; }
        button { background: var(--accent); color: #0f172a; font-weight: 600; cursor: pointer; border: none; }
        .search-box { display: flex; gap: 8px; margin-bottom: 16px; }
        .item-list { display: flex; flex-direction: column; gap: 8px; max-height: 450px; overflow-y: auto; }
        .game-item { display: flex; justify-content: space-between; align-items: center; background: #0f172a; padding: 10px; border-radius: 6px; }
        .badge { background: #334155; padding: 2px 8px; border-radius: 4px; font-size: 12px; color: var(--muted); }
    </style>
</head>
<body>
    <div class="header">
        <div class="logo">🎮 OmniSteam Manager</div>
        <div><span class="badge">Core: Active (Decoupled)</span></div>
    </div>
    <div class="container">
        <div class="card">
            <h3>🔍 搜索 Steam 游戏与 DLC</h3>
            <div class="search-box">
                <input type="text" id="queryInput" placeholder="输入游戏名称 (例如: Cyberpunk 2077)..." onkeydown="if(event.key==='Enter') searchGames()">
                <button onclick="searchGames()">搜索</button>
            </div>
            <div id="searchResults" class="item-list"></div>
        </div>
        <div class="card">
            <h3>📜 已安装解锁脚本</h3>
            <div id="scriptList" class="item-list"></div>
        </div>
    </div>
    <script>
        async function searchGames() {
            const q = document.getElementById('queryInput').value;
            if(!q) return;
            const res = await fetch('/api/search?q=' + encodeURIComponent(q));
            const data = await res.json();
            const container = document.getElementById('searchResults');
            container.innerHTML = '';
            data.forEach(item => {
                const div = document.createElement('div');
                div.className = 'game-item';
                div.innerHTML = `<div><strong>${item.name}</strong> <span class="badge">${item.appId}</span></div><button onclick="unlockGame(${item.appId}, '${encodeURIComponent(item.name)}')">一键生成解锁</button>`;
                container.appendChild(div);
            });
        }
        async function unlockGame(appId, name) {
            await fetch('/api/unlock', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ appId, gameName: decodeURIComponent(name) })
            });
            alert('解锁脚本已写入！Steam 核心已自动热重载。');
            loadScripts();
        }
        async function loadScripts() {
            const res = await fetch('/api/scripts');
            const data = await res.json();
            const container = document.getElementById('scriptList');
            container.innerHTML = '';
            data.forEach(s => {
                const div = document.createElement('div');
                div.className = 'game-item';
                div.innerHTML = `<div>${s.fileName} <span class="badge">${s.enabled ? '已启用' : '已停用'}</span></div><button onclick="toggleScript('${s.fullPath}', ${!s.enabled})">${s.enabled ? '停用' : '启用'}</button>`;
                container.appendChild(div);
            });
        }
        async function toggleScript(path, enable) {
            await fetch('/api/toggle', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ filePath: path, enable })
            });
            loadScripts();
        }
        loadScripts();
    </script>
</body>
</html>)rawhtml";

std::string HandleHttpRequest(const std::string& request) {
    if (request.rfind("GET / ", 0) == 0 || request.rfind("GET /index.html", 0) == 0) {
        std::string body = kIndexHtml;
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " << body.length()
            << "\r\nConnection: close\r\n\r\n"
            << body;
        return oss.str();
    }

    if (request.rfind("GET /api/search?", 0) == 0) {
        size_t qPos = request.find("q=");
        std::string q = "";
        if (qPos != std::string::npos) {
            size_t endPos = request.find_first_of(" &\r\n", qPos);
            q = request.substr(qPos + 2, endPos - (qPos + 2));
        }
        auto results = SteamApi::SearchStore(q);
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < results.size(); ++i) {
            json << "{\"appId\":" << results[i].appId << ",\"name\":\"" << results[i].name << "\"}";
            if (i + 1 < results.size())
                json << ",";
        }
        json << "]";
        std::string body = json.str();
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " << body.length()
            << "\r\nConnection: close\r\n\r\n"
            << body;
        return oss.str();
    }

    if (request.rfind("GET /api/scripts", 0) == 0) {
        auto scripts = ScriptManager::ListScripts();
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < scripts.size(); ++i) {
            json << "{\"fileName\":\"" << scripts[i].fileName << "\",\"fullPath\":\"" << scripts[i].fullPath
                 << "\",\"enabled\":" << (scripts[i].enabled ? "true" : "false") << "}";
            if (i + 1 < scripts.size())
                json << ",";
        }
        json << "]";
        std::string body = json.str();
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " << body.length()
            << "\r\nConnection: close\r\n\r\n"
            << body;
        return oss.str();
    }

    if (request.rfind("POST /api/unlock", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::regex idRegex("\"appId\"\\s*:\\s*(\\d+)");
            std::smatch m;
            if (std::regex_search(body, m, idRegex)) {
                uint32_t appId = static_cast<uint32_t>(std::stoul(m[1].str()));
                auto details = SteamApi::GetAppDetails(appId);
                UnlockGameSpec spec;
                spec.appId = appId;
                spec.gameName = details.name.empty() ? ("App_" + std::to_string(appId)) : details.name;
                spec.dlcAppIds = details.dlcAppIds;

                // Auto-fill depot key from local depotkeys.json if available
                std::string key = DepotKeyStore::GetKeyForDepot(appId);
                if (!key.empty()) {
                    spec.depotKeyHex = key;
                }

                ScriptManager::SaveGameUnlock(spec);
            }
        }
        std::string respBody = "{\"success\":true}";
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " << respBody.length()
            << "\r\nConnection: close\r\n\r\n"
            << respBody;
        return oss.str();
    }

    std::string notFound = "404 Not Found";
    return "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\nConnection: close\r\n\r\n" + notFound;
}
} // namespace

bool WebServer::Start(const std::string& host, uint16_t port) {
    if (g_serverRunning.load())
        return true;

#if defined(OMNI_PLATFORM_WINDOWS)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    g_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listenSocket == INVALID_SOCKET)
        return false;

    int opt = 1;
    setsockopt(g_listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr);

    if (bind(g_listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(g_listenSocket);
        return false;
    }

    if (listen(g_listenSocket, 10) == SOCKET_ERROR) {
        closesocket(g_listenSocket);
        return false;
    }

    g_serverRunning.store(true);
    spdlog::info("WebServer: OmniSteam Manager running at http://{}:{}", host, port);

    g_serverThread = std::thread([]() {
        while (g_serverRunning.load()) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(g_listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientSocket == INVALID_SOCKET)
                continue;

            char buffer[4096] = {0};
            int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesRead > 0) {
                std::string req(buffer, bytesRead);
                std::string res = HandleHttpRequest(req);
                send(clientSocket, res.data(), static_cast<int>(res.length()), 0);
            }
            closesocket(clientSocket);
        }
    });

    return true;
}

void WebServer::Stop() {
    if (!g_serverRunning.load())
        return;
    g_serverRunning.store(false);
    if (g_listenSocket != INVALID_SOCKET) {
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
    }
    if (g_serverThread.joinable()) {
        g_serverThread.join();
    }
}

bool WebServer::IsRunning() {
    return g_serverRunning.load();
}

} // namespace Manager
