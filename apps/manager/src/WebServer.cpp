#include "WebServer.h"

#include "CoreInstaller.h"
#include "DenuvoImporter.h"
#include "DepotKeyStore.h"
#include "ScriptManager.h"
#include "SteamApi.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <thread>

#include "OmniPlatform/OmniBuildInfo.h"
#include "OmniPlatform/OmniEndpoints.h"
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
    <title>OmniSteam Control Center</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        :root {
            --bg: #090d16;
            --card-bg: #131a2a;
            --card-border: #1e293b;
            --accent: #38bdf8;
            --accent-hover: #0ea5e9;
            --success: #22c55e;
            --danger: #ef4444;
            --warning: #f59e0b;
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        html, body { width: 100vw; height: 100vh; margin: 0; padding: 0; overflow: hidden; background: var(--bg); color: var(--text-main); font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
        
        /* Full viewport app wrapper */
        .app-layout { width: 100vw; height: 100vh; display: flex; flex-direction: column; padding: 14px 18px; box-sizing: border-box; gap: 12px; overflow: hidden; }
        
        .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--card-border); padding-bottom: 10px; flex-shrink: 0; }
        .logo { font-size: 22px; font-weight: 800; color: var(--accent); display: flex; align-items: center; gap: 8px; }
        .status-bar { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
        .badge { background: #1e293b; border: 1px solid var(--card-border); padding: 4px 10px; border-radius: 9999px; font-size: 12px; font-weight: 500; color: var(--text-muted); }
        .badge.online { background: rgba(34, 197, 94, 0.15); border-color: rgba(34, 197, 94, 0.3); color: var(--success); }
        .badge.warning { background: rgba(245, 158, 11, 0.15); border-color: rgba(245, 158, 11, 0.3); color: var(--warning); }
        
        /* Core Banner */
        .core-banner { background: linear-gradient(135deg, #1e293b 0%, #0f172a 100%); border: 1px solid #334155; border-radius: 10px; padding: 10px 16px; display: flex; justify-content: space-between; align-items: center; gap: 14px; flex-shrink: 0; }
        .core-info { display: flex; flex-direction: column; gap: 4px; }
        .core-title { font-size: 15px; font-weight: 700; display: flex; align-items: center; gap: 8px; }
        .hook-tags { display: flex; gap: 6px; font-size: 11px; margin-top: 2px; flex-wrap: wrap; }
        .hook-tag { background: rgba(56, 189, 248, 0.1); border: 1px solid rgba(56, 189, 248, 0.25); color: var(--accent); padding: 2px 6px; border-radius: 4px; }
        .hook-tag.active { background: rgba(34, 197, 94, 0.15); border-color: rgba(34, 197, 94, 0.3); color: var(--success); }

        /* Main 2-column layout */
        .container { flex: 1; min-height: 0; display: grid; grid-template-columns: 1.1fr 0.9fr; gap: 14px; overflow: hidden; }
        @media (max-width: 1024px) { .container { grid-template-columns: 1fr; } .core-banner { flex-direction: column; align-items: flex-start; } }
        
        .card { background: var(--card-bg); border-radius: 10px; padding: 14px 16px; border: 1px solid var(--card-border); display: flex; flex-direction: column; height: 100%; min-height: 0; overflow: hidden; }
        .card-title { font-size: 15px; font-weight: 700; margin-bottom: 12px; display: flex; justify-content: space-between; align-items: center; flex-shrink: 0; }
        
        /* Dropzone */
        .dropzone { border: 2px dashed #334155; border-radius: 8px; padding: 10px 14px; text-align: center; background: rgba(15, 23, 42, 0.5); cursor: pointer; transition: all 0.2s ease; margin-bottom: 10px; flex-shrink: 0; }
        .dropzone:hover, .dropzone.dragover { border-color: var(--accent); background: rgba(56, 189, 248, 0.05); }
        .drop-icon { font-size: 20px; margin-bottom: 2px; }
        .drop-text { font-size: 12px; color: var(--text-muted); }

        .search-box { display: flex; gap: 8px; margin-bottom: 10px; flex-shrink: 0; }
        input[type="text"] { flex: 1; padding: 10px 14px; border-radius: 8px; border: 1px solid var(--card-border); background: var(--bg); color: #fff; font-size: 13px; outline: none; }
        input[type="text"]:focus { border-color: var(--accent); }
        button, a.btn { padding: 8px 14px; border-radius: 8px; font-weight: 600; cursor: pointer; border: none; font-size: 13px; transition: all 0.2s ease; display: inline-flex; align-items: center; gap: 6px; text-decoration: none; }
        .btn-primary { background: var(--accent); color: #090d16; }
        .btn-primary:hover { background: var(--accent-hover); }
        .btn-success { background: var(--success); color: #fff; }
        .btn-danger { background: var(--danger); color: #fff; padding: 5px 10px; font-size: 12px; }
        .btn-toggle { background: #334155; color: #f8fafc; padding: 5px 10px; font-size: 12px; }
        .btn-outline { background: transparent; border: 1px solid var(--card-border); color: var(--text-main); }
        .btn-outline:hover { background: #1e293b; color: #fff; }

        /* Scrollable Panels */
        .item-list { flex: 1; min-height: 0; overflow-y: auto; display: flex; flex-direction: column; gap: 8px; padding-right: 4px; }
        .item-list::-webkit-scrollbar { width: 6px; }
        .item-list::-webkit-scrollbar-thumb { background: var(--card-border); border-radius: 3px; }
        
        .game-item { display: flex; align-items: center; justify-content: space-between; background: #0f172a; border: 1px solid var(--card-border); padding: 8px 12px; border-radius: 8px; gap: 12px; min-height: 56px; box-sizing: border-box; }
        .game-info { display: flex; align-items: center; gap: 12px; overflow: hidden; flex: 1; }
        .game-thumb-box { width: 80px; height: 38px; min-width: 80px; max-width: 80px; border-radius: 4px; overflow: hidden; background: #1e293b; display: flex; align-items: center; justify-content: center; flex-shrink: 0; border: 1px solid rgba(255,255,255,0.06); }
        .game-thumb { width: 100%; height: 100%; object-fit: cover; }
        .game-thumb-placeholder { width: 100%; height: 100%; display: flex; align-items: center; justify-content: center; font-size: 11px; color: var(--text-muted); background: #1e293b; }
        .game-meta { display: flex; flex-direction: column; overflow: hidden; justify-content: center; }
        .game-name { font-weight: 600; font-size: 13.5px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 220px; }
        .game-sub { font-size: 11.5px; color: var(--text-muted); display: flex; gap: 8px; margin-top: 2px; }
        .action-group { display: flex; gap: 6px; align-items: center; flex-shrink: 0; }
        .empty-hint { text-align: center; color: var(--text-muted); padding: 30px 0; font-size: 13px; }

        /* Modal */
        .modal-overlay { position: fixed; inset: 0; background: rgba(0,0,0,0.75); display: none; align-items: center; justify-content: center; z-index: 1000; padding: 20px; }
        .modal-content { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 12px; width: 100%; max-width: 680px; max-height: 88vh; overflow-y: auto; padding: 20px 24px; display: flex; flex-direction: column; gap: 14px; position: relative; }
        .modal-header-img { width: 100%; height: 180px; object-fit: cover; border-radius: 8px; background: #090d16; }
        .modal-close { position: absolute; top: 14px; right: 14px; background: rgba(0,0,0,0.6); color: #fff; border-radius: 50%; width: 30px; height: 30px; display: flex; align-items: center; justify-content: center; cursor: pointer; border: none; font-size: 15px; }
        .dlc-tag-list { display: flex; flex-wrap: wrap; gap: 6px; max-height: 180px; overflow-y: auto; padding: 8px; background: #090d16; border-radius: 8px; }
        .dlc-tag { background: #1e293b; padding: 4px 8px; border-radius: 4px; font-size: 12px; color: var(--text-main); display: flex; align-items: center; gap: 6px; }
    </style>
</head>
<body>
    <div class="app-layout">
        <div class="header">
            <div class="logo">🎮 OmniSteam Control Center</div>
            <div class="status-bar">
                <span id="steamStatus" class="badge">Steam: 检测中...</span>
                <span id="depotKeyStatus" class="badge">Depot Keys: 0 条</span>
                <span class="badge">CLI v)rawhtml" OMNISTEAM_VERSION R"rawhtml(</span>
            </div>
        </div>

        <!-- Core Management Banner -->
        <div class="core-banner">
            <div class="core-info">
                <div class="core-title">
                    <span id="coreStatusIcon">⚙️</span>
                    <span id="coreStatusText">Core 注入引擎: 检测中...</span>
                </div>
                <div class="hook-tags">
                    <span id="hookAppOwn" class="hook-tag">所有权模拟 (CheckAppOwnership)</span>
                    <span id="hookPackage" class="hook-tag">许可证注入 (Package 0)</span>
                    <span id="hookConfig" class="hook-tag">Depot 解密 (ConfigStore)</span>
                    <span id="hookIpc" class="hook-tag">IPC 拦截 (IPCProcessMessage)</span>
                </div>
            </div>
            <div class="action-group" style="display:flex; gap:8px;">
                <select id="channelSelect" style="padding:6px 10px; border-radius:8px; background:#0f172a; border:1px solid #334155; color:#fff; font-size:12px;">
                    <option value="release">正式版 (Release)</option>
                    <option value="nightly">每夜版 (Nightly)</option>
                </select>
                <button class="btn-primary" id="btnInstallCore" onclick="installCore()">🚀 一键安装/更新 Core</button>
                <button class="btn-outline" onclick="uninstallCore()">🗑️ 卸载 Core</button>
            </div>
        </div>

        <div class="container">
            <!-- Search & Denuvo Section -->
            <div class="card">
                <div class="card-title">
                    <span>🔍 搜索与解锁 Steam 游戏 / DLC</span>
                </div>

                <!-- Denuvo Drag & Drop Area -->
                <div class="dropzone" id="dropzone" onclick="document.getElementById('ticketFileInput').click()">
                    <input type="file" id="ticketFileInput" style="display:none" accept=".bin,.txt" onchange="handleFileSelect(event)">
                    <div class="drop-icon">📥</div>
                    <strong style="font-size:13px;">拖入 D 加密授权文件 (appticket.bin / tickets.txt)</strong>
                    <div class="drop-text">自动识别 AppID、写入平台凭证、匹配本地 config.vdf 与 depotkeys.bin 生成一体化解锁脚本</div>
                </div>

                <div class="search-box">
                    <input type="text" id="queryInput" placeholder="输入游戏名称 (例如: Cyberpunk 2077, 魔女, 黑神话, Palworld)..." onkeydown="if(event.key==='Enter') searchGames()">
                    <button class="btn-primary" id="searchBtn" onclick="searchGames()">搜索</button>
                </div>
                <div id="searchResults" class="item-list">
                    <div class="empty-hint">输入游戏名称开始在线检索 Steam Store 库</div>
                </div>
            </div>

            <!-- Installed Scripts Section -->
            <div class="card">
                <div class="card-title">
                    <span>📜 已安装解锁脚本 (<span id="scriptCount">0</span>)</span>
                    <div style="display:flex; gap:6px;">
                        <input type="text" id="scriptFilterInput" placeholder="过滤脚本..." style="padding:4px 8px; font-size:12px;" oninput="filterScripts()">
                        <button class="btn-toggle" onclick="loadScripts()">🔄 刷新</button>
                    </div>
                </div>
                <div id="scriptList" class="item-list">
                    <div class="empty-hint">正在读取脚本目录...</div>
                </div>
            </div>
        </div>
    </div>

    <!-- Game Details Modal -->
    <div class="modal-overlay" id="gameModal">
        <div class="modal-content">
            <button class="modal-close" onclick="closeModal()">✕</button>
            <img id="modalCover" class="modal-header-img" src="" onerror="this.style.display='none'">
            <div style="display:flex; justify-content:space-between; align-items:flex-start; gap:16px;">
                <div style="display:flex; flex-direction:column; gap:6px;">
                    <h2 id="modalTitle" style="font-size:20px; font-weight:700; color:#fff;">游戏详情</h2>
                    <div style="display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin-top:2px;">
                        <span id="modalAppId" class="badge" style="font-weight:600; background:#1e293b; color:var(--accent);">AppID: -</span>
                        <a id="btnSteamDb" href="#" target="_blank" class="btn btn-outline" style="font-size:12px; padding:3px 10px;">📊 SteamDB</a>
                        <a id="btnSteamStore" href="#" target="_blank" class="btn btn-outline" style="font-size:12px; padding:3px 10px;">🛍️ Steam 商店</a>
                    </div>
                </div>
                <button class="btn-primary" id="modalUnlockBtn" style="padding:10px 18px; font-size:13px; flex-shrink:0;">✨ 立即生成并载入解锁</button>
            </div>
            <p id="modalDesc" style="font-size:13px; color:var(--text-muted); line-height:1.5;"></p>
            <div>
                <h4 style="font-size:14px; font-weight:600; margin-bottom:8px;">📦 包含的 DLC 列表 (<span id="modalDlcCount">0</span> 个)</h4>
                <div id="modalDlcList" class="dlc-tag-list">
                    <div class="empty-hint" style="padding:10px;">未包含独立 DLC</div>
                </div>
            </div>
        </div>
    </div>

    <script>
        let g_allScripts = [];

        async function fetchStatus() {
            try {
                const res = await fetch('/api/status');
                const data = await res.json();
                
                // Steam status
                const sElem = document.getElementById('steamStatus');
                if (data.steamRunning) {
                    sElem.textContent = `Steam: 运行中 (PID: ${data.steamPid})`;
                    sElem.className = 'badge online';
                } else {
                    sElem.textContent = 'Steam: 未启动';
                    sElem.className = 'badge';
                }
                document.getElementById('depotKeyStatus').textContent = `Depot Keys: ${data.depotKeysCount.toLocaleString()} 条`;

                // Core Hook status
                const cIcon = document.getElementById('coreStatusIcon');
                const cText = document.getElementById('coreStatusText');
                const hAppOwn = document.getElementById('hookAppOwn');
                const hPackage = document.getElementById('hookPackage');
                const hConfig = document.getElementById('hookConfig');
                const hIpc = document.getElementById('hookIpc');

                if (data.core && data.core.active) {
                    cIcon.textContent = '🟢';
                    cText.textContent = `Core 注入引擎: 已生效 (v${data.core.installedVersion || '1.0.0'}, 目标: ${data.core.targetModule || 'steamclient'})`;
                    hAppOwn.className = data.core.checkAppOwnershipHook ? 'hook-tag active' : 'hook-tag';
                    hPackage.className = 'hook-tag active';
                    hConfig.className = data.core.configStoreHook ? 'hook-tag active' : 'hook-tag';
                    hIpc.className = data.core.ipcHook ? 'hook-tag active' : 'hook-tag';
                } else if (data.core && data.core.installed) {
                    cIcon.textContent = '🟡';
                    cText.textContent = `Core 已就绪于 Steam 目录 (等待 Steam 启动即时注入)`;
                } else {
                    cIcon.textContent = '⚪';
                    cText.textContent = 'Core 尚未安装至 Steam 目录';
                }
            } catch(e) {}
        }

        async function installCore() {
            const channel = document.getElementById('channelSelect').value;
            const btn = document.getElementById('btnInstallCore');
            btn.textContent = '正在下载部署...';
            btn.disabled = true;

            try {
                const res = await fetch('/api/core/install', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ channel })
                });
                const data = await res.json();
                if (data.success) {
                    alert('🎉 Core 核心已成功安装/更新至 Steam 目录！\n启动或重启 Steam 即可直接生效。');
                    fetchStatus();
                } else {
                    alert('安装 Core 失败：' + (data.message || '网络连接失败'));
                }
            } catch(e) {
                alert('请求异常');
            } finally {
                btn.textContent = '🚀 一键安装/更新 Core';
                btn.disabled = false;
            }
        }

        async function uninstallCore() {
            if (!confirm('确定要从 Steam 目录卸载 OmniSteam 注入核心吗？')) return;
            const res = await fetch('/api/core/uninstall', { method: 'POST' });
            const data = await res.json();
            alert(data.success ? 'Core 卸载完成，Steam 已恢复纯净状态。' : '未检测到需清理的 Core 文件');
            fetchStatus();
        }

        // Dropzone handlers
        const dropzone = document.getElementById('dropzone');
        ['dragenter', 'dragover'].forEach(name => {
            dropzone.addEventListener(name, (e) => { e.preventDefault(); dropzone.classList.add('dragover'); });
        });
        ['dragleave', 'drop'].forEach(name => {
            dropzone.addEventListener(name, (e) => { e.preventDefault(); dropzone.classList.remove('dragover'); });
        });
        dropzone.addEventListener('drop', (e) => {
            if (e.dataTransfer.files && e.dataTransfer.files[0]) {
                uploadTicketFile(e.dataTransfer.files[0]);
            }
        });
        function handleFileSelect(e) {
            if (e.target.files && e.target.files[0]) {
                uploadTicketFile(e.target.files[0]);
            }
        }

        async function uploadTicketFile(file) {
            const reader = new FileReader();
            reader.onload = async () => {
                const payload = reader.result;
                const res = await fetch('/api/denuvo/upload', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/octet-stream', 'X-Filename': encodeURIComponent(file.name) },
                    body: payload
                });
                const data = await res.json();
                if (data.success) {
                    let msg = `🎉 成功解析并入库 D 加密游戏【${data.gameName}】(AppID: ${data.appId})！\n已自动载入 ${data.dlcCount} 个 DLC 与 ${data.resolvedDepotKeysCount} 条 Depot 解密 Key。`;
                    if (data.missingDepots && data.missingDepots.length > 0) {
                        msg += `\n\n⚠️ 注意: 缺少 ${data.missingDepots.length} 个 Depot 的解密 Key，已自动生成脚本并在后台尝试同步。可在 GitHub Issue 提交补全。`;
                    }
                    alert(msg);
                    loadScripts();
                } else {
                    alert('导入失败: ' + (data.message || '无法识别有效票据'));
                }
            };
            if (file.name.endsWith('.txt')) {
                reader.readAsText(file);
            } else {
                reader.readAsArrayBuffer(file);
            }
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
                    const thumbHtml = item.tinyImage ? 
                        `<div class="game-thumb-box"><img class="game-thumb" src="${item.tinyImage}" onerror="this.parentElement.innerHTML='<div class=\\'game-thumb-placeholder\\'>🎮 App</div>'"></div>` :
                        `<div class="game-thumb-box"><div class="game-thumb-placeholder">🎮 App</div></div>`;
                    
                    div.innerHTML = `
                        <div class="game-info" onclick="viewGameDetails(${item.appId})" style="cursor:pointer;">
                            ${thumbHtml}
                            <div class="game-meta">
                                <span class="game-name" title="${item.name}">${item.name}</span>
                                <span class="game-sub">AppID: ${item.appId}</span>
                            </div>
                        </div>
                        <div class="action-group">
                            <button class="btn-outline" onclick="viewGameDetails(${item.appId})" style="font-size:12px; padding:5px 10px;">🔍 详情</button>
                            <button class="btn-primary" onclick="unlockGame(${item.appId}, '${encodeURIComponent(item.name)}')">✨ 解锁</button>
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

        async function viewGameDetails(appId) {
            const modal = document.getElementById('gameModal');
            document.getElementById('modalTitle').textContent = '正在获取详情...';
            document.getElementById('modalAppId').textContent = `AppID: ${appId}`;
            document.getElementById('btnSteamDb').href = `https://steamdb.info/app/${appId}/`;
            document.getElementById('btnSteamStore').href = `https://store.steampowered.com/app/${appId}/`;
            document.getElementById('modalDesc').textContent = '';
            document.getElementById('modalCover').src = `https://cdn.akamai.steamstatic.com/steam/apps/${appId}/header.jpg`;
            document.getElementById('modalCover').style.display = 'block';
            modal.style.display = 'flex';

            const res = await fetch(`/api/appdetails?appId=${appId}`);
            const data = await res.json();
            
            document.getElementById('modalTitle').textContent = data.name || `App ${appId}`;
            document.getElementById('modalDesc').textContent = data.description || '暂无简介';
            document.getElementById('modalDlcCount').textContent = (data.dlcList ? data.dlcList.length : 0);

            const dlcContainer = document.getElementById('modalDlcList');
            dlcContainer.innerHTML = '';
            if (data.dlcList && data.dlcList.length > 0) {
                data.dlcList.forEach(dlc => {
                    const tag = document.createElement('div');
                    tag.className = 'dlc-tag';
                    tag.innerHTML = `<span>${dlc.name}</span> <span class="badge" style="font-size:10px;">${dlc.dlcId}</span>`;
                    dlcContainer.appendChild(tag);
                });
            } else {
                dlcContainer.innerHTML = '<div class="empty-hint" style="padding:6px;">未包含独立 DLC</div>';
            }

            document.getElementById('modalUnlockBtn').onclick = () => {
                unlockGame(appId, encodeURIComponent(data.name || `App_${appId}`));
                closeModal();
            };
        }

        function closeModal() {
            document.getElementById('gameModal').style.display = 'none';
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
                alert(`🎉 成功为【${name}】生成并载入解锁脚本！\n包含 ${data.dlcCount || 0} 个 DLC 与所有匹配的 Depot 解密 Key。Steam 核心已自动热重载生效！`);
                loadScripts();
            } else {
                alert('写入解锁脚本失败');
            }
        }

        async function loadScripts() {
            try {
                const res = await fetch('/api/scripts');
                g_allScripts = await res.json();
                document.getElementById('scriptCount').textContent = g_allScripts.length;
                renderScripts(g_allScripts);
            } catch(e) {}
        }

        function renderScripts(scripts) {
            const container = document.getElementById('scriptList');
            container.innerHTML = '';
            if (!scripts || scripts.length === 0) {
                container.innerHTML = '<div class="empty-hint">暂未检测到已安装的 .lua 脚本</div>';
                return;
            }
            scripts.forEach(s => {
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
        }

        function filterScripts() {
            const q = document.getElementById('scriptFilterInput').value.toLowerCase().trim();
            if (!q) {
                renderScripts(g_allScripts);
                return;
            }
            const filtered = g_allScripts.filter(s => 
                (s.title && s.title.toLowerCase().includes(q)) || 
                (s.fileName && s.fileName.toLowerCase().includes(q)) ||
                (s.primaryAppId && s.primaryAppId.toString().includes(q))
            );
            renderScripts(filtered);
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
        auto coreStatus = CoreInstaller::GetStatus();

        std::ostringstream json;
        json << "{"
             << "\"steamRunning\":" << (isRunning ? "true" : "false") << ","
             << "\"steamPid\":" << pid << ","
             << "\"depotKeysCount\":" << DepotKeyStore::Count() << ","
             << "\"installedScriptsCount\":" << scripts.size() << ","
             << "\"defaultLuaDir\":\"" << OmniPlatform::Encoding::EscapeJson(ScriptManager::GetDefaultLuaDirectory())
             << "\","
             << "\"core\":{"
             << "\"installed\":" << (coreStatus.installed ? "true" : "false") << ","
             << "\"active\":" << (coreStatus.active ? "true" : "false") << ","
             << "\"installedVersion\":\"" << OmniPlatform::Encoding::EscapeJson(coreStatus.installedVersion) << "\","
             << "\"targetModule\":\"" << OmniPlatform::Encoding::EscapeJson(coreStatus.targetModule) << "\","
             << "\"checkAppOwnershipHook\":" << (coreStatus.checkAppOwnershipHook ? "true" : "false") << ","
             << "\"configStoreHook\":" << (coreStatus.configStoreHook ? "true" : "false") << ","
             << "\"ipcHook\":" << (coreStatus.ipcHook ? "true" : "false") << "}"
             << "}";

        std::string body = json.str();
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " << body.length()
            << "\r\nConnection: close\r\n\r\n"
            << body;
        return oss.str();
    }

    if (request.rfind("GET /api/appdetails?", 0) == 0) {
        size_t idPos = request.find("appId=");
        uint32_t appId = 0;
        if (idPos != std::string::npos) {
            size_t endPos = request.find_first_of(" &\r\n", idPos);
            std::string idStr = request.substr(idPos + 6, endPos - (idPos + 6));
            try {
                appId = static_cast<uint32_t>(std::stoul(idStr));
            } catch (...) {
            }
        }

        auto details = SteamApi::GetAppDetails(appId);
        std::ostringstream json;
        json << "{"
             << "\"appId\":" << appId << ","
             << "\"name\":\"" << OmniPlatform::Encoding::EscapeJson(details.name) << "\","
             << "\"type\":\"" << OmniPlatform::Encoding::EscapeJson(details.type) << "\","
             << "\"headerImage\":\"" << OmniPlatform::Encoding::EscapeJson(details.headerImage) << "\","
             << "\"description\":\"" << OmniPlatform::Encoding::EscapeJson(details.description) << "\","
             << "\"dlcList\":[";
        for (size_t i = 0; i < details.dlcList.size(); ++i) {
            json << "{\"dlcId\":" << details.dlcList[i].dlcId << ",\"name\":\""
                 << OmniPlatform::Encoding::EscapeJson(details.dlcList[i].name) << "\"}";
            if (i + 1 < details.dlcList.size())
                json << ",";
        }
        json << "]}";

        std::string body = json.str();
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " << body.length()
            << "\r\nConnection: close\r\n\r\n"
            << body;
        return oss.str();
    }

    if (request.rfind("POST /api/core/install", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        std::string channel = "release";
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            if (body.find("\"nightly\"") != std::string::npos) {
                channel = "nightly";
            }
        }
        bool ok = CoreInstaller::InstallCore(channel);
        std::string respBody = ok ? "{\"success\":true}" : "{\"success\":false,\"message\":\"Install failed\"}";
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "
            << respBody.length() << "\r\nConnection: close\r\n\r\n"
            << respBody;
        return oss.str();
    }

    if (request.rfind("POST /api/core/uninstall", 0) == 0) {
        bool ok = CoreInstaller::UninstallCore();
        std::string respBody = ok ? "{\"success\":true}" : "{\"success\":false}";
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "
            << respBody.length() << "\r\nConnection: close\r\n\r\n"
            << respBody;
        return oss.str();
    }

    if (request.rfind("POST /api/denuvo/upload", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        std::string payload = (bodyPos != std::string::npos) ? request.substr(bodyPos + 4) : "";

        std::string filename = "";
        size_t fnPos = request.find("X-Filename:");
        if (fnPos != std::string::npos) {
            size_t fnEnd = request.find("\r\n", fnPos);
            std::string enc = request.substr(fnPos + 11, fnEnd - (fnPos + 11));
            while (!enc.empty() && enc.front() == ' ')
                enc.erase(0, 1);
            filename = OmniPlatform::Encoding::UrlDecode(enc);
        }

        auto res = DenuvoImporter::ImportFromPayload(payload, filename);
        std::ostringstream json;
        json << "{"
             << "\"success\":" << (res.success ? "true" : "false") << ","
             << "\"appId\":" << res.appId << ","
             << "\"gameName\":\"" << OmniPlatform::Encoding::EscapeJson(res.gameName) << "\","
             << "\"dlcCount\":" << res.dlcCount << ","
             << "\"resolvedDepotKeysCount\":" << res.resolvedDepotKeysCount << ","
             << "\"missingDepots\":[";
        for (size_t i = 0; i < res.missingDepots.size(); ++i) {
            json << res.missingDepots[i];
            if (i + 1 < res.missingDepots.size())
                json << ",";
        }
        json << "],\"message\":\"" << OmniPlatform::Encoding::EscapeJson(res.message) << "\"}";

        std::string respBody = json.str();
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "
            << respBody.length() << "\r\nConnection: close\r\n\r\n"
            << respBody;
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

                // Auto-fill depot keys for main game and all DLCs from local config.vdf and depotkeys.bin
                auto matchedKeys = DepotKeyStore::FindDepotKeysForApp(appId, details.dlcAppIds);
                for (const auto& [depotId, keyHex] : matchedKeys) {
                    if (depotId == appId) {
                        spec.depotKeyHex = keyHex;
                    }
                    spec.depotKeys[depotId] = keyHex;
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

            char buffer[65536];
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
