#include "WebServer.h"

#include "DepotKeyStore.h"
#include "ScriptManager.h"
#include "SteamApi.h"

#include <atomic>
#include <cstring>
#include <iomanip>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>
#include <thread>

#include "OmniPlatform/OmniPlatform.h"

#if defined(OMNI_PLATFORM_WINDOWS)
#include <winsock2.h>

#include <tlhelp32.h>
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

bool CheckSteamProcess(uint32_t* outPid = nullptr) {
#if defined(OMNI_PLATFORM_WINDOWS)
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(hSnap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "steam.exe") == 0) {
                if (outPid)
                    *outPid = pe.th32ProcessID;
                CloseHandle(hSnap);
                return true;
            }
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return false;
#else
    return false;
#endif
}

const char* kIndexHtml = R"rawhtml(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <title>OmniSteam Manager Dashboard</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        :root {
            --bg: #0b0f19;
            --card-bg: #151d30;
            --card-border: #23314e;
            --accent: #38bdf8;
            --accent-hover: #0ea5e9;
            --success: #22c55e;
            --danger: #ef4444;
            --warning: #f59e0b;
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: var(--bg); color: var(--text-main); padding: 24px; }
        .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--card-border); padding-bottom: 16px; margin-bottom: 24px; }
        .logo { font-size: 24px; font-weight: 800; color: var(--accent); display: flex; align-items: center; gap: 8px; }
        .status-bar { display: flex; gap: 12px; align-items: center; }
        .badge { background: #1e293b; border: 1px solid var(--card-border); padding: 4px 12px; border-radius: 9999px; font-size: 13px; font-weight: 500; color: var(--text-muted); }
        .badge.online { background: rgba(34, 197, 94, 0.15); border-color: rgba(34, 197, 94, 0.3); color: var(--success); }
        .container { display: grid; grid-template-columns: 1.1fr 0.9fr; gap: 24px; }
        @media (max-width: 900px) { .container { grid-template-columns: 1fr; } }
        .card { background: var(--card-bg); border-radius: 12px; padding: 20px; border: 1px solid var(--card-border); display: flex; flex-direction: column; }
        .card-title { font-size: 18px; font-weight: 700; margin-bottom: 16px; display: flex; justify-content: space-between; align-items: center; }
        .search-box { display: flex; gap: 10px; margin-bottom: 16px; }
        input[type="text"] { flex: 1; padding: 12px 16px; border-radius: 8px; border: 1px solid var(--card-border); background: var(--bg); color: #fff; font-size: 14px; outline: none; }
        input[type="text"]:focus { border-color: var(--accent); }
        button { padding: 10px 18px; border-radius: 8px; font-weight: 600; cursor: pointer; border: none; font-size: 14px; transition: all 0.2s ease; display: inline-flex; align-items: center; gap: 6px; }
        .btn-primary { background: var(--accent); color: #0b0f19; }
        .btn-primary:hover { background: var(--accent-hover); }
        .btn-success { background: var(--success); color: #fff; }
        .btn-danger { background: var(--danger); color: #fff; padding: 6px 12px; font-size: 12px; }
        .btn-toggle { background: #334155; color: #f8fafc; padding: 6px 12px; font-size: 12px; }
        .item-list { display: flex; flex-direction: column; gap: 10px; max-height: 520px; overflow-y: auto; padding-right: 4px; }
        .item-list::-webkit-scrollbar { width: 6px; }
        .item-list::-webkit-scrollbar-thumb { background: var(--card-border); border-radius: 3px; }
        .game-item { display: flex; align-items: center; justify-content: space-between; background: #0f172a; border: 1px solid var(--card-border); padding: 12px; border-radius: 8px; gap: 12px; }
        .game-info { display: flex; align-items: center; gap: 12px; overflow: hidden; }
        .game-thumb { width: 70px; height: 32px; border-radius: 4px; object-fit: cover; background: #1e293b; }
        .game-meta { display: flex; flex-direction: column; overflow: hidden; }
        .game-name { font-weight: 600; font-size: 14px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 260px; }
        .game-sub { font-size: 12px; color: var(--text-muted); display: flex; gap: 8px; margin-top: 2px; }
        .action-group { display: flex; gap: 8px; align-items: center; }
        .empty-hint { text-align: center; color: var(--text-muted); padding: 40px 0; font-size: 14px; }
    </style>
</head>
<body>
    <div class="header">
        <div class="logo">🎮 OmniSteam Manager</div>
        <div class="status-bar">
            <span id="steamStatus" class="badge">Steam: 检测中...</span>
            <span id="depotKeyStatus" class="badge">密钥库: 加载中</span>
        </div>
    </div>
    <div class="container">
        <div class="card">
            <div class="card-title">
                <span>🔍 搜索与解锁 Steam 游戏 / DLC</span>
            </div>
            <div class="search-box">
                <input type="text" id="queryInput" placeholder="输入游戏名称 (例如: Cyberpunk 2077, 黑神话, Palworld)..." onkeydown="if(event.key==='Enter') searchGames()">
                <button class="btn-primary" id="searchBtn" onclick="searchGames()">搜索</button>
            </div>
            <div id="searchResults" class="item-list">
                <div class="empty-hint">输入游戏名称开始在线检索 Steam Store 库</div>
            </div>
        </div>
        <div class="card">
            <div class="card-title">
                <span>📜 已安装解锁脚本</span>
                <button class="btn-toggle" onclick="loadScripts()">🔄 刷新</button>
            </div>
            <div id="scriptList" class="item-list">
                <div class="empty-hint">正在读取脚本目录...</div>
            </div>
        </div>
    </div>

    <script>
        async function fetchStatus() {
            try {
                const res = await fetch('/api/status');
                const data = await res.json();
                const sElem = document.getElementById('steamStatus');
                if (data.steamRunning) {
                    sElem.textContent = `Steam: 运行中 (PID: ${data.steamPid})`;
                    sElem.className = 'badge online';
                } else {
                    sElem.textContent = 'Steam: 未启动';
                    sElem.className = 'badge';
                }
                document.getElementById('depotKeyStatus').textContent = `Depot Keys: ${data.depotKeysCount.toLocaleString()} 条`;
            } catch(e) {}
        }

        async function searchGames() {
            const q = document.getElementById('queryInput').value.trim();
            if(!q) return;
            const btn = document.getElementById('searchBtn');
            btn.textContent = '检索中...';
            btn.disabled = true;

            const container = document.getElementById('searchResults');
            container.innerHTML = '<div class="empty-hint">正在连接 Steam 商店 API 检索...</div>';

            try {
                const res = await fetch('/api/search?q=' + encodeURIComponent(q));
                const data = await res.json();
                container.innerHTML = '';
                if (!data || data.length === 0) {
                    container.innerHTML = '<div class="empty-hint">未找到匹配的 Steam 游戏</div>';
                    return;
                }
                data.forEach(item => {
                    const div = document.createElement('div');
                    div.className = 'game-item';
                    div.innerHTML = `
                        <div class="game-info">
                            <img class="game-thumb" src="${item.tinyImage}" onerror="this.style.display='none'">
                            <div class="game-meta">
                                <span class="game-name" title="${item.name}">${item.name}</span>
                                <span class="game-sub">AppID: ${item.appId}</span>
                            </div>
                        </div>
                        <div class="action-group">
                            <button class="btn-primary" onclick="unlockGame(${item.appId}, '${encodeURIComponent(item.name)}')">✨ 一键解锁</button>
                        </div>
                    `;
                    container.appendChild(div);
                });
            } catch(e) {
                container.innerHTML = '<div class="empty-hint">检索失败，请检查网络或后端服务</div>';
            } finally {
                btn.textContent = '搜索';
                btn.disabled = false;
            }
        }

        async function unlockGame(appId, nameEncoded) {
            const name = decodeURIComponent(nameEncoded);
            const res = await fetch('/api/unlock', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ appId: appId, gameName: name })
            });
            const data = await res.json();
            if (data.success) {
                alert(`🎉 成功为【${name}】生成解锁脚本！\n包含 ${data.dlcCount || 0} 个 DLC。Steam 核心已自动热重载生效！`);
                loadScripts();
            } else {
                alert('写入解锁脚本失败');
            }
        }

        async function loadScripts() {
            try {
                const res = await fetch('/api/scripts');
                const data = await res.json();
                const container = document.getElementById('scriptList');
                container.innerHTML = '';
                if (!data || data.length === 0) {
                    container.innerHTML = '<div class="empty-hint">暂未检测到已安装的 .lua 脚本</div>';
                    return;
                }
                data.forEach(s => {
                    const div = document.createElement('div');
                    div.className = 'game-item';
                    const displayName = s.title ? `${s.title} (${s.fileName})` : s.fileName;
                    div.innerHTML = `
                        <div class="game-info">
                            <div class="game-meta">
                                <span class="game-name" title="${s.fullPath}">${displayName}</span>
                                <span class="game-sub">AppID: ${s.primaryAppId || '通用'} | 状态: <strong style="color:${s.enabled ? '#22c55e' : '#94a3b8'}">${s.enabled ? '已启用' : '已停用'}</strong></span>
                            </div>
                        </div>
                        <div class="action-group">
                            <button class="btn-toggle" onclick="toggleScript('${encodeURIComponent(s.fullPath)}', ${!s.enabled})">${s.enabled ? '停用' : '启用'}</button>
                            <button class="btn-danger" onclick="deleteScript('${encodeURIComponent(s.fullPath)}')">删除</button>
                        </div>
                    `;
                    container.appendChild(div);
                });
            } catch(e) {}
        }

        async function toggleScript(pathEncoded, enable) {
            const path = decodeURIComponent(pathEncoded);
            await fetch('/api/toggle', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ filePath: path, enable: enable })
            });
            loadScripts();
        }

        async function deleteScript(pathEncoded) {
            if (!confirm('确定要删除此解锁脚本吗？')) return;
            const path = decodeURIComponent(pathEncoded);
            await fetch('/api/delete', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ filePath: path })
            });
            loadScripts();
        }

        fetchStatus();
        loadScripts();
        setInterval(fetchStatus, 3000);
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

    if (request.rfind("GET /api/status", 0) == 0) {
        uint32_t pid = 0;
        bool isRunning = CheckSteamProcess(&pid);
        auto scripts = ScriptManager::ListScripts();

        std::ostringstream json;
        json << "{"
             << "\"steamRunning\":" << (isRunning ? "true" : "false") << ","
             << "\"steamPid\":" << pid << ","
             << "\"depotKeysCount\":" << DepotKeyStore::GetTotalKeyCount() << ","
             << "\"installedScriptsCount\":" << scripts.size() << ","
             << "\"defaultLuaDir\":\"" << OmniPlatform::Encoding::EscapeJson(ScriptManager::GetDefaultLuaDirectory())
             << "\""
             << "}";

        std::string body = json.str();
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " << body.length()
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
        std::string decodedQ = OmniPlatform::Encoding::UrlDecode(q);
        auto results = SteamApi::SearchStore(decodedQ);
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < results.size(); ++i) {
            json << "{\"appId\":" << results[i].appId << ",\"name\":\""
                 << OmniPlatform::Encoding::EscapeJson(results[i].name) << "\",\"tinyImage\":\""
                 << OmniPlatform::Encoding::EscapeJson(results[i].tinyImage) << "\"}";
            if (i + 1 < results.size())
                json << ",";
        }
        json << "]";
        std::string body = json.str();
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " << body.length()
            << "\r\nConnection: close\r\n\r\n"
            << body;
        return oss.str();
    }

    if (request.rfind("GET /api/scripts", 0) == 0) {
        auto scripts = ScriptManager::ListScripts();
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < scripts.size(); ++i) {
            json << "{\"fileName\":\"" << OmniPlatform::Encoding::EscapeJson(scripts[i].fileName)
                 << "\",\"fullPath\":\"" << OmniPlatform::Encoding::EscapeJson(scripts[i].fullPath) << "\",\"title\":\""
                 << OmniPlatform::Encoding::EscapeJson(scripts[i].title)
                 << "\",\"primaryAppId\":" << scripts[i].primaryAppId
                 << ",\"enabled\":" << (scripts[i].enabled ? "true" : "false")
                 << ",\"fileSize\":" << scripts[i].fileSize << "}";
            if (i + 1 < scripts.size())
                json << ",";
        }
        json << "]";
        std::string body = json.str();
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " << body.length()
            << "\r\nConnection: close\r\n\r\n"
            << body;
        return oss.str();
    }

    if (request.rfind("POST /api/unlock", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        uint32_t appId = 0;
        size_t dlcCount = 0;
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::regex idRegex("\"appId\"\\s*:\\s*(\\d+)");
            std::smatch m;
            if (std::regex_search(body, m, idRegex)) {
                appId = static_cast<uint32_t>(std::stoul(m[1].str()));
                auto details = SteamApi::GetAppDetails(appId);
                UnlockGameSpec spec;
                spec.appId = appId;
                spec.gameName = details.name.empty() ? ("App_" + std::to_string(appId)) : details.name;
                spec.dlcAppIds = details.dlcAppIds;
                dlcCount = spec.dlcAppIds.size();

                // Auto-fill depot key from local depotkeys.bin if available
                std::string key = DepotKeyStore::GetKeyForDepot(appId);
                if (!key.empty()) {
                    spec.depotKeyHex = key;
                }
                ScriptManager::SaveGameUnlock(spec);
            }
        }
        std::ostringstream json;
        json << "{\"success\":true,\"appId\":" << appId << ",\"dlcCount\":" << dlcCount << "}";
        std::string respBody = json.str();
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "
            << respBody.length() << "\r\nConnection: close\r\n\r\n"
            << respBody;
        return oss.str();
    }

    if (request.rfind("POST /api/toggle", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        bool ok = false;
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::regex pathRegex("\"filePath\"\\s*:\\s*\"([^\"]+)\"");
            std::regex enableRegex("\"enable\"\\s*:\\s*(true|false)");
            std::smatch mPath, mEnable;
            if (std::regex_search(body, mPath, pathRegex) && std::regex_search(body, mEnable, enableRegex)) {
                std::string path = mPath[1].str();
                bool enable = (mEnable[1].str() == "true");
                ok = ScriptManager::ToggleScript(path, enable);
            }
        }
        std::string respBody = ok ? "{\"success\":true}" : "{\"success\":false}";
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "
            << respBody.length() << "\r\nConnection: close\r\n\r\n"
            << respBody;
        return oss.str();
    }

    if (request.rfind("POST /api/delete", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        bool ok = false;
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::regex pathRegex("\"filePath\"\\s*:\\s*\"([^\"]+)\"");
            std::smatch mPath;
            if (std::regex_search(body, mPath, pathRegex)) {
                std::string path = mPath[1].str();
                ok = ScriptManager::DeleteScript(path);
            }
        }
        std::string respBody = ok ? "{\"success\":true}" : "{\"success\":false}";
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "
            << respBody.length() << "\r\nConnection: close\r\n\r\n"
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
    spdlog::info("WebServer: Listening on http://{}:{}", host, port);

    g_serverThread = std::thread([]() {
        while (g_serverRunning.load()) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(g_listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientSocket == INVALID_SOCKET) {
                if (!g_serverRunning.load())
                    break;
                continue;
            }

            char buffer[8192];
            int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string response = HandleHttpRequest(buffer);
                send(clientSocket, response.c_str(), static_cast<int>(response.length()), 0);
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

#if defined(OMNI_PLATFORM_WINDOWS)
    WSACleanup();
#endif
    spdlog::info("WebServer: Stopped");
}

bool WebServer::IsRunning() {
    return g_serverRunning.load();
}

} // namespace Manager
