#include "StaticAssets.h"

#include <string>

#include "OmniPlatform/OmniBuildInfo.h"

namespace Manager {

namespace {
const char* kRawIndexHtml = R"rawhtml(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <title>OmniSteam</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&family=JetBrains+Mono:wght@400;500;600&display=swap" rel="stylesheet">
    <style>
        :root {
            --font-sans: 'Inter', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "PingFang SC", "Microsoft YaHei", sans-serif;
            --font-mono: 'JetBrains Mono', "Cascadia Code", "Consolas", monospace;
            --bg-base: #0b0f19;
            --bg-card: #111827;
            --bg-card-subtle: #162032;
            --border: #1f293d;
            --border-hover: #334155;
            --primary: #38bdf8;
            --primary-hover: #0ea5e9;
            --success: #22c55e;
            --danger: #ef4444;
            --warning: #f59e0b;
            --text-main: #f8fafc;
            --text-muted: #94a3b8;
            --text-sub: #64748b;
            --radius: 12px;
            --radius-sm: 8px;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        html, body {
            width: 100vw;
            height: 100vh;
            margin: 0;
            padding: 0;
            overflow: hidden;
            background: radial-gradient(circle at 50% 0%, #151d30 0%, var(--bg-base) 75%);
            color: var(--text-main);
            font-family: var(--font-sans);
            -webkit-font-smoothing: antialiased;
            -moz-osx-font-smoothing: grayscale;
            text-rendering: optimizeLegibility;
        }
        
        .app-layout {
            width: 100vw;
            height: 100vh;
            display: flex;
            flex-direction: column;
            padding: 16px 20px;
            gap: 12px;
            box-sizing: border-box;
            overflow: hidden;
        }
        .navbar {
            display: flex;
            justify-content: space-between;
            align-items: center;
            background: rgba(17, 24, 39, 0.7);
            backdrop-filter: blur(12px);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            padding: 12px 20px;
            flex-shrink: 0;
        }
        .brand {
            display: flex;
            align-items: center;
            gap: 12px;
        }
        .brand-icon {
            font-size: 24px;
            line-height: 1;
        }
        .brand-name {
            font-size: 18px;
            font-weight: 700;
            letter-spacing: -0.02em;
            background: linear-gradient(135deg, #ffffff 0%, var(--primary) 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .version-chip {
            background: rgba(56, 189, 248, 0.1);
            border: 1px solid rgba(56, 189, 248, 0.2);
            color: var(--primary);
            font-family: var(--font-mono);
            font-size: 11px;
            font-weight: 600;
            padding: 2px 8px;
            border-radius: 9999px;
        }
        .status-indicators {
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .status-pill {
            display: inline-flex;
            align-items: center;
            gap: 6px;
            background: var(--bg-card-subtle);
            border: 1px solid var(--border);
            padding: 6px 12px;
            border-radius: 9999px;
            font-family: var(--font-mono);
            font-size: 12px;
            color: var(--text-muted);
        }
        .dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: var(--text-sub);
        }
        .dot.online {
            background: var(--success);
            box-shadow: 0 0 8px rgba(34, 197, 94, 0.6);
        }
        .dot.warning {
            background: var(--warning);
            box-shadow: 0 0 8px rgba(245, 158, 11, 0.6);
        }
        /* Core Engine Banner */
        .core-panel {
            background: linear-gradient(135deg, rgba(22, 32, 50, 0.8) 0%, rgba(17, 24, 39, 0.9) 100%);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            padding: 16px 20px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            gap: 16px;
            flex-shrink: 0;
        }
        .core-info {
            display: flex;
            flex-direction: column;
            gap: 6px;
        }
        .core-header {
            display: flex;
            align-items: center;
            gap: 10px;
            font-size: 15px;
            font-weight: 600;
        }
        .hook-tag-row {
            display: flex;
            gap: 6px;
            flex-wrap: wrap;
        }
        .hook-pill {
            font-size: 11px;
            padding: 2px 8px;
            border-radius: 4px;
            background: rgba(255, 255, 255, 0.04);
            border: 1px solid var(--border);
            color: var(--text-sub);
        }
        .hook-pill.active {
            background: rgba(34, 197, 94, 0.12);
            border-color: rgba(34, 197, 94, 0.3);
            color: var(--success);
            font-weight: 500;
        }
        .core-controls {
            display: flex;
            align-items: center;
            gap: 8px;
            flex-shrink: 0;
        }

        /* Main 2-Column Grid */
        .workspace {
            flex: 1;
            min-height: 0;
            display: grid;
            grid-template-columns: 1.15fr 0.85fr;
            gap: 16px;
        }
        @media (max-width: 1024px) {
            .workspace { grid-template-columns: 1fr; }
            .core-panel { flex-direction: column; align-items: flex-start; }
        }
        
        .panel-card {
            background: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            padding: 18px 20px;
            display: flex;
            flex-direction: column;
            height: 100%;
            min-height: 0;
            box-sizing: border-box;
        }
        .panel-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 14px;
            flex-shrink: 0;
        }
        .panel-title {
            font-size: 15px;
            font-weight: 700;
            color: var(--text-main);
            display: flex;
            align-items: center;
            gap: 8px;
        }
        
        /* Inputs & Buttons */
        .search-bar {
            display: flex;
            gap: 8px;
            margin-bottom: 12px;
            flex-shrink: 0;
        }
        select, input[type="text"] {
            background: var(--bg-base);
            border: 1px solid var(--border);
            color: var(--text-main);
            border-radius: var(--radius-sm);
            padding: 9px 12px;
            font-size: 13px;
            outline: none;
            transition: border-color 0.2s ease, box-shadow 0.2s ease;
        }
        select:focus, input[type="text"]:focus {
            border-color: var(--primary);
            box-shadow: 0 0 0 2px rgba(56, 189, 248, 0.15);
        }
        input[type="text"] { flex: 1; }

        button, a.btn {
            padding: 8px 14px;
            border-radius: var(--radius-sm);
            font-size: 13px;
            font-weight: 600;
            cursor: pointer;
            border: none;
            transition: all 0.2s ease;
            display: inline-flex;
            align-items: center;
            gap: 6px;
            text-decoration: none;
        }
        .btn-primary { background: var(--primary); color: #0b0f19; }
        .btn-primary:hover { background: var(--primary-hover); }
        .btn-secondary { background: var(--bg-card-subtle); border: 1px solid var(--border); color: var(--text-main); }
        .btn-secondary:hover { background: #1e293b; border-color: var(--border-hover); }
        .btn-danger { background: rgba(239, 68, 68, 0.15); border: 1px solid rgba(239, 68, 68, 0.3); color: var(--danger); }
        .btn-danger:hover { background: var(--danger); color: #fff; }

        /* Dropzone */
        .ticket-dropzone {
            border: 2px dashed rgba(56, 189, 248, 0.35);
            background: rgba(15, 23, 42, 0.6);
            border-radius: var(--radius);
            padding: 22px 20px;
            text-align: center;
            cursor: pointer;
            transition: all 0.2s ease;
            margin-bottom: 14px;
            flex-shrink: 0;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            gap: 6px;
        }
        .ticket-dropzone:hover, .ticket-dropzone.dragover {
            border-color: var(--primary);
            background: rgba(56, 189, 248, 0.1);
            transform: translateY(-1px);
        }
        .ticket-icon { font-size: 28px; line-height: 1; }
        .ticket-title { font-size: 14px; font-weight: 700; color: var(--text-main); }
        .ticket-sub { font-size: 12px; color: var(--text-muted); line-height: 1.4; max-width: 92%; }

        /* Scrollable List Containers */
        .scroll-list {
            flex: 1;
            min-height: 0;
            overflow-y: auto;
            display: flex;
            flex-direction: column;
            gap: 8px;
            padding-right: 4px;
        }
        .scroll-list::-webkit-scrollbar { width: 5px; }
        .scroll-list::-webkit-scrollbar-thumb { background: var(--border); border-radius: 3px; }
        
        .list-row {
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: rgba(15, 23, 42, 0.6);
            border: 1px solid var(--border);
            padding: 10px 14px;
            border-radius: var(--radius-sm);
            gap: 12px;
            transition: background 0.15s ease, border-color 0.15s ease;
        }
        .list-row:hover {
            background: rgba(22, 32, 50, 0.8);
            border-color: var(--border-hover);
        }
        .row-main {
            display: flex;
            align-items: center;
            gap: 12px;
            overflow: hidden;
            flex: 1;
        }
        .game-art-box {
            width: 84px;
            height: 40px;
            min-width: 84px;
            border-radius: 4px;
            overflow: hidden;
            background: var(--bg-card-subtle);
            display: flex;
            align-items: center;
            justify-content: center;
            flex-shrink: 0;
            border: 1px solid rgba(255, 255, 255, 0.05);
        }
        .game-art { width: 100%; height: 100%; object-fit: cover; }
        .row-text { display: flex; flex-direction: column; overflow: hidden; }
        .row-title { font-size: 13.5px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .row-desc { font-size: 11.5px; color: var(--text-muted); display: flex; gap: 8px; margin-top: 2px; }
        .btn-group { display: flex; gap: 6px; align-items: center; flex-shrink: 0; }
        .empty-placeholder { text-align: center; color: var(--text-sub); padding: 40px 0; font-size: 13px; }

        /* Modal Dialog */
        .modal-mask {
            position: fixed;
            inset: 0;
            background: rgba(5, 8, 15, 0.85);
            backdrop-filter: blur(8px);
            display: none;
            align-items: center;
            justify-content: center;
            z-index: 1000;
            padding: 20px;
        }
        .modal-box {
            background: var(--bg-card);
            border: 1px solid var(--border);
            border-radius: var(--radius);
            width: 100%;
            max-width: 640px;
            max-height: 88vh;
            overflow: hidden;
            display: flex;
            flex-direction: column;
            position: relative;
            box-shadow: 0 24px 48px rgba(0, 0, 0, 0.7);
        }
        .modal-banner {
            position: relative;
            width: 100%;
            height: 180px;
            background: var(--bg-base);
            flex-shrink: 0;
        }
        .modal-hero {
            width: 100%;
            height: 100%;
            object-fit: cover;
        }
        .modal-close-btn {
            position: absolute;
            top: 12px;
            right: 12px;
            z-index: 50;
            background: rgba(0, 0, 0, 0.75);
            backdrop-filter: blur(4px);
            color: #fff;
            border-radius: 50%;
            width: 32px;
            height: 32px;
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
            border: 1px solid rgba(255, 255, 255, 0.2);
            font-size: 14px;
            transition: all 0.2s ease;
        }
        .modal-close-btn:hover {
            background: rgba(239, 68, 68, 0.8);
            border-color: rgba(239, 68, 68, 0.5);
        }
        .modal-body {
            padding: 20px 24px;
            display: flex;
            flex-direction: column;
            gap: 14px;
            overflow-y: auto;
            max-height: calc(88vh - 180px);
        }
        .dlc-chip-list {
            display: flex;
            flex-wrap: wrap;
            gap: 6px;
            max-height: 150px;
            overflow-y: auto;
            padding: 10px;
            background: var(--bg-base);
            border-radius: var(--radius-sm);
        }
        .dlc-chip {
            background: var(--bg-card-subtle);
            border: 1px solid var(--border);
            padding: 4px 8px;
            border-radius: 4px;
            font-size: 11.5px;
            display: flex;
            align-items: center;
            gap: 6px;
        }
    </style>
</head>
<body>
    <div class="app-layout">
        <!-- Navigation Header -->
        <div class="navbar">
            <div class="brand">
                <span class="brand-icon">🎮</span>
                <span class="brand-name">OmniSteam</span>
                <span class="version-chip">v{{VERSION}}</span>
            </div>
            <div class="status-indicators">
                <button class="btn-secondary" id="langToggleBtn" style="font-size:11.5px; padding:5px 10px; font-family:var(--font-mono);" onclick="toggleLanguage()">🌐 EN</button>
                <button class="btn-primary" style="font-size:12px; padding:5px 12px; display:flex; align-items:center; gap:6px; cursor:pointer;" onclick="openCloudModal()"><span data-i18n="cloudBtn">☁️ 云存档设置</span></button>
                <div class="status-pill">
                    <span id="steamDot" class="dot"></span>
                    <span id="steamStatusText">Steam: ...</span>
                </div>
                <div class="status-pill">
                    <span>🔑</span>
                    <span id="depotKeyStatus">Depot Keys: 0</span>
                </div>
            </div>
        </div>

        <!-- Core Engine Status & Deployment Panel -->
        <div class="core-panel">
            <div class="core-info">
                <div class="core-header">
                    <span id="coreStatusDot" class="dot"></span>
                    <span id="coreStatusTitle">Core Engine</span>
                </div>
                <div class="hook-tag-row">
                    <span id="hookAppOwn" class="hook-pill">CheckAppOwnership</span>
                    <span id="hookPackage" class="hook-pill">PackageInfo</span>
                    <span id="hookConfig" class="hook-pill">ConfigStore</span>
                    <span id="hookIpc" class="hook-pill">IPC</span>
                </div>
            </div>
            <div class="core-controls">
                <select id="channelSelect">
                    <option value="release">Release (正式版)</option>
                    <option value="nightly">Nightly (每夜版)</option>
                </select>
                <button class="btn-primary" id="btnInstallCore" onclick="installCore()"><span data-i18n="btnInstall">🚀 一键安装/更新 Core</span></button>
                <button class="btn-secondary" onclick="uninstallCore()"><span data-i18n="btnUninstall">🗑️ 卸载</span></button>
            </div>
        </div>

        <!-- Workspace (100% Height Fill) -->
        <div class="workspace">
            <!-- Left: Game Search & Unlock Panel -->
            <div class="panel-card">
                <div class="panel-header">
                    <span class="panel-title" data-i18n="searchTitle">🔍 搜索与解锁 (Search & Unlock)</span>
                </div>

                <!-- Denuvo Ticket Drag & Drop Area -->
                <div class="ticket-dropzone" id="ticketDropzone" onclick="document.getElementById('ticketFileInput').click()">
                    <input type="file" id="ticketFileInput" style="display:none" accept=".bin,.txt" onchange="handleTicketSelect(event)">
                    <div class="ticket-title" data-i18n="dropzoneTitle">📥 拖入 D 加密授权文件 (appticket.bin / tickets.txt)</div>
                    <div class="ticket-sub" data-i18n="dropzoneSub">自动识别 AppID 并匹配 Depot 解密 Key 生成脚本</div>
                </div>

                <!-- Search Input Toolbar with Region Filter -->
                <div class="search-bar">
                    <select id="regionSelect" title="Steam Store Region" style="width:125px;">
                        <option value="US" selected>🌐 Global (US)</option>
                        <option value="HK">🇭🇰 HK</option>
                        <option value="CN">🇨🇳 CN</option>
                    </select>
                    <input type="text" id="queryInput" placeholder="输入游戏名称或 AppID..." onkeydown="handleSearchKey(event)">
                    <button class="btn-primary" id="searchBtn" onclick="searchGames()"><span data-i18n="btnSearch">搜索</span></button>
                </div>

                <!-- Search Results -->
                <div id="searchResults" class="scroll-list">
                    <div class="empty-placeholder" data-i18n="emptySearch">输入游戏名称或 AppID 开始在线检索</div>
                </div>
            </div>

            <!-- Right: Installed Scripts Panel -->
            <div class="panel-card">
                <div class="panel-header">
                    <span class="panel-title"><span data-i18n="scriptsTitle">📜 已安装脚本</span> (<span id="scriptCount">0</span>)</span>
                    <div style="display:flex; gap:6px;">
                        <input type="text" id="scriptFilterInput" placeholder="过滤脚本..." style="width:120px; padding:6px 10px; font-size:12px;" oninput="filterScripts()">
                        <button class="btn-secondary" style="padding:6px 10px; font-size:12px;" onclick="loadScripts()"><span data-i18n="btnRefresh">🔄 刷新</span></button>
                    </div>
                </div>
                <div id="scriptList" class="scroll-list">
                    <div class="empty-placeholder" data-i18n="emptyScripts">正在读取脚本目录...</div>
                </div>
            </div>
        </div>
    </div>

    <!-- Game Details Modal Dialog -->
    <div class="modal-mask" id="gameModal">
        <div class="modal-box">
            <div class="modal-banner">
                <button class="modal-close-btn" onclick="closeModal()" title="Close">✕</button>
                <img id="modalCover" class="modal-hero" src="" alt="Cover">
            </div>
            <div class="modal-body">
                <div style="display:flex; justify-content:space-between; align-items:flex-start; gap:16px;">
                    <div style="display:flex; flex-direction:column; gap:4px;">
                        <h2 id="modalTitle" style="font-size:18px; font-weight:700;">Game Details</h2>
                        <div style="display:flex; align-items:center; gap:8px; margin-top:2px;">
                            <span id="modalAppId" class="status-pill" style="padding:2px 8px; font-size:11px; color:var(--primary);">AppID: -</span>
                            <a id="btnSteamDb" href="#" target="_blank" class="btn btn-secondary" style="font-size:11px; padding:3px 8px;">📊 SteamDB</a>
                            <a id="btnSteamStore" href="#" target="_blank" class="btn btn-secondary" style="font-size:11px; padding:3px 8px;">🛍️ Store</a>
                        </div>
                    </div>
                    <button class="btn-primary" id="modalUnlockBtn" style="padding:9px 18px; font-size:13px; flex-shrink:0;"><span data-i18n="btnUnlock">✨ 一键解锁</span></button>
                </div>
                <p id="modalDesc" style="font-size:12.5px; color:var(--text-muted); line-height:1.6; max-height:90px; overflow-y:auto;"></p>
                <div>
                    <h4 style="font-size:13px; font-weight:600; margin-bottom:8px;">📦 <span data-i18n="dlcTitle">包含的 DLC</span> (<span id="modalDlcCount">0</span>)</h4>
                    <div id="modalDlcList" class="dlc-chip-list">
                        <div class="empty-placeholder" style="padding:10px;">未包含独立 DLC</div>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <!-- WebDAV Cloud Save Modal Dialog -->
    <div class="modal-mask" id="cloudModal" style="display:none;">
        <div class="modal-box" style="max-width:540px;">
            <div style="display:flex; justify-content:space-between; align-items:center; padding:18px 24px; border-bottom:1px solid var(--border);">
                <h3 style="font-size:16px; font-weight:700; display:flex; align-items:center; gap:8px;" data-i18n="cloudTitle">☁️ WebDAV 云存档设置</h3>
                <button class="modal-close-btn" style="position:static; width:28px; height:28px;" onclick="closeCloudModal()">✕</button>
            </div>
            <div class="modal-body" style="gap:14px; padding:20px 24px;">
                <label style="display:flex; align-items:center; gap:10px; cursor:pointer; font-weight:600; font-size:13.5px;">
                    <input type="checkbox" id="cloudEnabled" style="width:16px; height:16px;">
                    <span data-i18n="cloudEnable">启用 WebDAV 多端云存档同步</span>
                </label>
                <div>
                    <label style="color:var(--text-muted); display:block; margin-bottom:5px; font-size:12px;" data-i18n="cloudUrl">WebDAV 服务器 URL (例如坚果云 / Alist / Nextcloud)</label>
                    <input type="text" id="webdavUrl" placeholder="https://dav.jianguoyun.com/dav/" style="width:100%; padding:8px 12px; background:var(--bg-base); border:1px solid var(--border); border-radius:6px; color:#fff; font-size:13px;">
                </div>
                <div style="display:grid; grid-template-columns:1fr 1fr; gap:12px;">
                    <div>
                        <label style="color:var(--text-muted); display:block; margin-bottom:5px; font-size:12px;" data-i18n="cloudUser">用户名 / 账号 (Username)</label>
                        <input type="text" id="webdavUser" placeholder="user@example.com" style="width:100%; padding:8px 12px; background:var(--bg-base); border:1px solid var(--border); border-radius:6px; color:#fff; font-size:13px;">
                    </div>
                    <div>
                        <label style="color:var(--text-muted); display:block; margin-bottom:5px; font-size:12px;" data-i18n="cloudPass">应用授权密码 / Token</label>
                        <input type="password" id="webdavPass" placeholder="••••••••" style="width:100%; padding:8px 12px; background:var(--bg-base); border:1px solid var(--border); border-radius:6px; color:#fff; font-size:13px;">
                    </div>
                </div>
                <div>
                    <label style="color:var(--text-muted); display:block; margin-bottom:5px; font-size:12px;" data-i18n="cloudRoot">云端存档存放根目录</label>
                    <input type="text" id="webdavRoot" placeholder="OmniSteam_Saves" style="width:100%; padding:8px 12px; background:var(--bg-base); border:1px solid var(--border); border-radius:6px; color:#fff; font-size:13px;">
                </div>
                <div id="cloudTestMsg" style="font-size:12px; min-height:18px;"></div>
                <div style="display:flex; justify-content:flex-end; gap:10px; margin-top:6px;">
                    <button class="btn-secondary" id="btnTestCloud" onclick="testCloudConnection()"><span data-i18n="btnTest">🔍 测试连接</span></button>
                    <button class="btn-primary" onclick="saveCloudConfig()"><span data-i18n="btnSave">💾 保存配置</span></button>
                </div>
            </div>
        </div>
    </div>
    <script>
        const I18N = {
            zh: {
                langToggle: "🌐 EN",
                cloudBtn: "☁️ 云存档设置",
                steamOnline: "Steam: 运行中",
                steamOffline: "Steam: 未运行",
                depotKeys: "Depot Keys",
                coreReady: "Core 引擎: 已就绪生效",
                coreStandby: "Core 引擎: 待命中 (等待 Steam 启动)",
                coreUninstalled: "Core 尚未部署",
                btnInstall: "🚀 一键安装/更新 Core",
                btnUninstall: "🗑️ 卸载",
                searchTitle: "🔍 搜索与解锁 (Search & Unlock)",
                dropzoneTitle: "📥 拖入 D 加密授权文件 (appticket.bin / tickets.txt)",
                dropzoneSub: "自动识别 AppID 并匹配 Depot 解密 Key 生成脚本",
                searchPlaceholder: "输入游戏名称或 AppID...",
                btnSearch: "搜索",
                scriptsTitle: "📜 已安装脚本",
                filterPlaceholder: "过滤脚本...",
                btnRefresh: "🔄 刷新",
                emptySearch: "输入游戏名称或 AppID 开始在线检索",
                emptyScripts: "暂未检测到已安装的 .lua 脚本",
                btnUnlock: "✨ 一键解锁",
                dlcTitle: "包含的 DLC",
                cloudTitle: "☁️ WebDAV 云存档设置",
                cloudEnable: "启用 WebDAV 多端云存档同步",
                cloudUrl: "WebDAV 服务器 URL (例如坚果云 / Alist / Nextcloud)",
                cloudUser: "用户名 / 账号 (Username)",
                cloudPass: "应用授权密码 / Token",
                cloudRoot: "云端存档存放根目录",
                btnTest: "🔍 测试连接",
                btnSave: "💾 保存配置",
                btnBackup: "☁️ 备份",
                btnRestore: "📥 恢复",
                btnLaunch: "🚀 安装/启动",
                btnDisable: "停用",
                btnEnable: "启用",
                btnDelete: "删除",
                statusActive: "已启用",
                statusDisabled: "已停用"
            },
                langToggle: "🌐 中文",
                cloudBtn: "☁️ Cloud Saves",
                steamOnline: "Steam: Online",
                steamOffline: "Steam: Offline",
                depotKeys: "Depot Keys",
                coreReady: "Core Engine: Ready & Active",
                coreStandby: "Core Engine: Standby (Waiting for Steam)",
                coreUninstalled: "Core Engine: Not Installed",
                btnInstall: "🚀 Install / Update Core",
                btnUninstall: "🗑️ Uninstall",
                searchTitle: "🔍 Search & Unlock",
                dropzoneTitle: "📥 Drop Denuvo Ticket (appticket.bin / tickets.txt)",
                dropzoneSub: "Auto detect AppID and match Depot Decryption Keys",
                searchPlaceholder: "Enter game title or AppID...",
                btnSearch: "Search",
                scriptsTitle: "📜 Installed Scripts",
                filterPlaceholder: "Filter scripts...",
                btnRefresh: "🔄 Refresh",
                emptySearch: "Search by game title or AppID",
                emptyScripts: "No installed .lua scripts found",
                btnUnlock: "✨ Unlock",
                dlcTitle: "Included DLCs",
                cloudTitle: "☁️ WebDAV Cloud Saves",
                cloudEnable: "Enable WebDAV Cloud Save Sync",
                cloudUrl: "WebDAV Server URL (e.g. Nextcloud / Alist / Jianguoyun)",
                cloudUser: "Username / Account",
                cloudPass: "App Password / Token",
                cloudRoot: "Remote Root Directory",
                btnTest: "🔍 Test Connection",
                btnSave: "💾 Save Settings",
                btnBackup: "☁️ Backup",
                btnRestore: "📥 Restore",
                btnLaunch: "🚀 Install/Play",
                btnDisable: "Disable",
                btnEnable: "Enable",
                btnDelete: "Delete",
                statusActive: "Active",
                statusDisabled: "Disabled"
            }
        };

        let g_currentLang = localStorage.getItem('omni_lang') || 'zh';
        function t(key) {
            return (I18N[g_currentLang] && I18N[g_currentLang][key]) ? I18N[g_currentLang][key] : key;
        }
        function applyLanguage() {
            document.getElementById('langToggleBtn').textContent = t('langToggle');
            document.querySelectorAll('[data-i18n]').forEach(el => {
                const k = el.getAttribute('data-i18n');
                if (I18N[g_currentLang][k]) el.textContent = I18N[g_currentLang][k];
            });
            document.getElementById('queryInput').placeholder = t('searchPlaceholder');
            document.getElementById('scriptFilterInput').placeholder = t('filterPlaceholder');
            fetchStatus();
            if (g_allScripts.length > 0) renderScripts(g_allScripts);
        }
        function toggleLanguage() {
            g_currentLang = (g_currentLang === 'zh') ? 'en' : 'zh';
            localStorage.setItem('omni_lang', g_currentLang);
            applyLanguage();
        }

        let g_allScripts = [];
        function handleSearchKey(event) {
            if (event.isComposing) return;
            if (event.key === 'Enter') {
                searchGames();
            }
        }

        async function fetchStatus() {
            try {
                const res = await fetch('/api/status');
                const data = await res.json();
                
                // Steam Status
                const sDot = document.getElementById('steamDot');
                const sText = document.getElementById('steamStatusText');
                if (data.steamRunning) {
                    sDot.className = 'dot online';
                    sText.textContent = `${t('steamOnline')} (PID: ${data.steamPid})`;
                } else {
                    sDot.className = 'dot';
                    sText.textContent = t('steamOffline');
                }
                document.getElementById('depotKeyStatus').textContent = `${t('depotKeys')}: ${data.depotKeysCount.toLocaleString()}`;

                // Core Status
                const cDot = document.getElementById('coreStatusDot');
                const cTitle = document.getElementById('coreStatusTitle');
                const hAppOwn = document.getElementById('hookAppOwn');
                const hPackage = document.getElementById('hookPackage');
                const hConfig = document.getElementById('hookConfig');
                const hIpc = document.getElementById('hookIpc');
                if (data.steamRunning && data.core && data.core.active) {
                    cDot.className = 'dot online';
                    cTitle.textContent = `${t('coreReady')} (v${data.core.installedVersion || '1.0.0'}, ${data.core.targetModule || 'steamclient'})`;
                    hAppOwn.className = data.core.checkAppOwnershipHook ? 'hook-pill active' : 'hook-pill';
                    hPackage.className = 'hook-pill active';
                    hConfig.className = data.core.configStoreHook ? 'hook-pill active' : 'hook-pill';
                    hIpc.className = data.core.ipcHook ? 'hook-pill active' : 'hook-pill';
                } else if (data.core && data.core.installed) {
                    cDot.className = 'dot warning';
                    cTitle.textContent = t('coreStandby');
                    hAppOwn.className = 'hook-pill';
                    hPackage.className = 'hook-pill';
                    hConfig.className = 'hook-pill';
                    hIpc.className = 'hook-pill';
                } else {
                    cDot.className = 'dot';
                    cTitle.textContent = t('coreUninstalled');
                    hAppOwn.className = 'hook-pill';
                    hPackage.className = 'hook-pill';
                    hConfig.className = 'hook-pill';
                    hIpc.className = 'hook-pill';
                }
            } catch(e) {}
        }

        async function installCore() {
            const channel = document.getElementById('channelSelect').value;
            const btn = document.getElementById('btnInstallCore');
            btn.textContent = '正在处理部署...';
            btn.disabled = true;

            try {
                const res = await fetch('/api/core/install', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ channel })
                });
                const data = await res.json();
                if (data.success) {
                    alert(data.message || '🎉 Core 核心已成功安装/更新至 Steam 目录！\n启动或重启 Steam 即可直接生效。');
                    fetchStatus();
                } else {
                    alert('安装 Core 提示：\n' + (data.message || '网络连接失败或未找到可用资产。'));
                }
            } catch(e) {
                alert('请求异常，请检查本地后台服务。');
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

        // Ticket Dropzone Handlers
        const dropzone = document.getElementById('ticketDropzone');
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
        function handleTicketSelect(e) {
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
                        msg += `\n\n⚠️ 缺少 ${data.missingDepots.length} 个 Depot 的解密 Key，已自动生成脚本并在后台尝试同步。可在 GitHub Issue 提交补全。`;
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
            const cc = document.getElementById('regionSelect').value;
            const btn = document.getElementById('searchBtn');
            btn.textContent = '检索中...';
            btn.disabled = true;

            const container = document.getElementById('searchResults');
            container.innerHTML = '<div class="empty-placeholder">正在连接 Steam 商店 API 检索...</div>';

            try {
                const res = await fetch(`/api/search?q=${encodeURIComponent(q)}&cc=${encodeURIComponent(cc)}`);
                const data = await res.json();
                container.innerHTML = '';
                if (!data || data.length === 0) {
                    container.innerHTML = '<div class="empty-placeholder">未找到匹配的 Steam 游戏（可尝试切换全球库或港区再次检索）</div>';
                    return;
                }
                data.forEach(item => {
                    const div = document.createElement('div');
                    div.className = 'list-row';
                    const thumbHtml = item.tinyImage ? 
                        `<div class="game-art-box"><img class="game-art" src="${item.tinyImage}" onerror="this.parentElement.innerHTML='<span style=\\'font-size:11px;color:var(--text-sub)\\'>🎮</span>'"></div>` :
                        `<div class="game-art-box"><span style="font-size:11px;color:var(--text-sub)">🎮</span></div>`;
                    
                    div.innerHTML = `
                        <div class="row-main" onclick="viewGameDetails(${item.appId})" style="cursor:pointer;">
                            ${thumbHtml}
                            <div class="row-text">
                                <span class="row-title" title="${item.name}">${item.name}</span>
                                <span class="row-desc">AppID: ${item.appId}</span>
                            </div>
                        </div>
                        <div class="btn-group">
                            <button class="btn-secondary" onclick="viewGameDetails(${item.appId})" style="font-size:12px; padding:5px 10px;">🔍 详情</button>
                            <button class="btn-primary" onclick="unlockGame(${item.appId}, '${encodeURIComponent(item.name)}')">✨ 解锁</button>
                        </div>
                    `;
                    container.appendChild(div);
                });
            } catch(e) {
                container.innerHTML = '<div class="empty-placeholder">检索失败，请检查网络连接</div>';
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

            const cover = document.getElementById('modalCover');
            cover.style.display = 'block';
            cover.src = `https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/${appId}/header.jpg`;
            cover.onerror = () => {
                cover.src = `https://cdn.cloudflare.steamstatic.com/steam/apps/${appId}/header.jpg`;
                cover.onerror = () => { cover.style.display = 'none'; };
            };
            modal.style.display = 'flex';

            try {
                const res = await fetch(`/api/appdetails?appId=${appId}`);
                const data = await res.json();

                document.getElementById('modalTitle').textContent = data.name || `App ${appId}`;
                document.getElementById('modalDesc').textContent = data.description || '暂无简介';
                document.getElementById('modalDlcCount').textContent = (data.dlcList ? data.dlcList.length : 0);

                if (data.headerImage) {
                    cover.src = data.headerImage;
                    cover.style.display = 'block';
                }

                const dlcContainer = document.getElementById('modalDlcList');
                dlcContainer.innerHTML = '';
                if (data.dlcList && data.dlcList.length > 0) {
                    data.dlcList.forEach(dlc => {
                        const tag = document.createElement('div');
                        tag.className = 'dlc-chip';
                        tag.innerHTML = `<span>${dlc.name}</span> <span class="version-chip" style="font-size:10px;">${dlc.dlcId}</span>`;
                        dlcContainer.appendChild(tag);
                    });
                } else {
                    dlcContainer.innerHTML = '<div class="empty-placeholder" style="padding:6px;">未包含独立 DLC</div>';
                }

                document.getElementById('modalUnlockBtn').onclick = () => {
                    unlockGame(appId, encodeURIComponent(data.name || `App_${appId}`));
                    closeModal();
                };
            } catch (e) {
                document.getElementById('modalTitle').textContent = `App ${appId}`;
                document.getElementById('modalDesc').textContent = '获取详情失败，请检查网络连接';
            }
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
                if (confirm(`🎉 成功为【${name}】生成并载入解锁脚本！\n包含 ${data.dlcCount || 0} 个 DLC 与所有匹配的 Depot 解密 Key。\n\n是否立即在 Steam 中调起安装/启动？`)) {
                    window.location.href = `steam://run/${appId}`;
                }
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
            } catch (e) {}
        }

        function renderScripts(scripts) {
            const container = document.getElementById('scriptList');
            container.innerHTML = '';
            if (!scripts || scripts.length === 0) {
                container.innerHTML = `<div class="empty-placeholder">${t('emptyScripts')}</div>`;
                return;
            }
            scripts.forEach(s => {
                const div = document.createElement('div');
                div.className = 'list-row';
                const displayName = s.title && s.title.trim() ? s.title.trim() : (s.primaryAppId ? `App ${s.primaryAppId}` : s.fileName);
                const subText = s.primaryAppId ? `AppID: ${s.primaryAppId}` : `${s.fileName}`;
                const statusText = s.enabled ? t('statusActive') : t('statusDisabled');
                const launchBtn = s.primaryAppId ? `
                    <button class="btn-primary" style="padding:4px 8px; font-size:12px; background:var(--accent);" title="Install / Launch in Steam" onclick="window.location.href='steam://run/${s.primaryAppId}'">${t('btnLaunch')}</button>
                ` : '';
                const cloudBtns = s.primaryAppId ? `
                    <button class="btn-secondary" style="padding:4px 8px; font-size:12px;" title="Backup" onclick="backupSaves(${s.primaryAppId}, '${encodeURIComponent(displayName)}')">${t('btnBackup')}</button>
                    <button class="btn-secondary" style="padding:4px 8px; font-size:12px;" title="Restore" onclick="restoreSaves(${s.primaryAppId}, '${encodeURIComponent(displayName)}')">${t('btnRestore')}</button>
                ` : '';
                div.innerHTML = `
                    <div class="row-main">
                        <div class="row-text">
                            <span class="row-title" title="${s.fullPath}">${displayName}</span>
                            <span class="row-desc">${subText} | <strong style="color:${s.enabled ? 'var(--success)' : 'var(--text-sub)'}">${statusText}</strong></span>
                        </div>
                    </div>
                    <div class="btn-group">
                        ${launchBtn}
                        ${cloudBtns}
                        <button class="btn-secondary" style="padding:4px 8px; font-size:12px;" onclick="toggleScript('${encodeURIComponent(s.fullPath)}', ${!s.enabled})">${s.enabled ? t('btnDisable') : t('btnEnable')}</button>
                        <button class="btn-danger" style="padding:4px 8px; font-size:12px;" onclick="deleteScript('${encodeURIComponent(s.fullPath)}')">${t('btnDelete')}</button>
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

        let savedHasPassword = false;
        async function openCloudModal() {
            document.getElementById('cloudModal').style.display = 'flex';
            document.getElementById('cloudTestMsg').textContent = '';
            try {
                const res = await fetch('/api/cloud/config');
                const cfg = await res.json();
                document.getElementById('cloudEnabled').checked = !!cfg.enabled;
                document.getElementById('webdavUrl').value = cfg.serverUrl || '';
                document.getElementById('webdavUser').value = cfg.username || '';
                savedHasPassword = !!cfg.passwordSet;
                const passInput = document.getElementById('webdavPass');
                passInput.value = '';
                passInput.placeholder = savedHasPassword ? '已保存（留空保持不变）' : '••••••••';
                document.getElementById('webdavRoot').value = cfg.remoteRoot || 'OmniSteam_Saves';
            } catch(e) {}
        }
        function closeCloudModal() {
            document.getElementById('cloudModal').style.display = 'none';
        }
        async function testCloudConnection() {
            const msg = document.getElementById('cloudTestMsg');
            const btn = document.getElementById('btnTestCloud');
            btn.textContent = '测试中...';
            btn.disabled = true;
            msg.style.color = 'var(--text-muted)';
            msg.textContent = '正在连接 WebDAV 服务器...';
            try {
                const res = await fetch('/api/cloud/test', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({
                        serverUrl: document.getElementById('webdavUrl').value.trim(),
                        username: document.getElementById('webdavUser').value.trim(),
                        password: document.getElementById('webdavPass').value.trim(),
                        remoteRoot: document.getElementById('webdavRoot').value.trim()
                    })
                });
                const data = await res.json();
                if (data.success) {
                    msg.style.color = 'var(--success)';
                    msg.textContent = '✅ WebDAV 连接与鉴权成功！';
                } else {
                    msg.style.color = 'var(--danger)';
                    msg.textContent = '❌ 连接失败: ' + (data.message || '网络或密码错误');
                }
            } catch(e) {
                msg.style.color = 'var(--danger)';
                msg.textContent = '❌ 请求异常，请检查网络配置。';
            } finally {
                btn.textContent = '🔍 测试连接';
                btn.disabled = false;
            }
        }
        async function saveCloudConfig() {
            const pass = document.getElementById('webdavPass').value.trim();
            const res = await fetch('/api/cloud/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    enabled: document.getElementById('cloudEnabled').checked,
                    serverUrl: document.getElementById('webdavUrl').value.trim(),
                    username: document.getElementById('webdavUser').value.trim(),
                    password: pass || (savedHasPassword ? '__OMNI_KEEP__' : ''),
                    remoteRoot: document.getElementById('webdavRoot').value.trim()
                })
            });
            const data = await res.json();
            if (data.success) {
                alert('🎉 WebDAV 云存档设置保存成功！');
                closeCloudModal();
            } else {
                alert('保存配置失败');
            }
        }
        async function backupSaves(appId, nameEncoded) {
            const name = decodeURIComponent(nameEncoded);
            const res = await fetch('/api/cloud/backup', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ appId: appId })
            });
            const data = await res.json();
            if (data.success) {
                alert(`🎉 游戏【${name}】存档已成功备份至 WebDAV 云端！`);
            } else {
                alert(`备份失败: ${data.message || '请先在云存档设置中配置有效的 WebDAV 服务器'}`);
            }
        }
        async function restoreSaves(appId, nameEncoded) {
            const name = decodeURIComponent(nameEncoded);
            if (!confirm(`确定要从 WebDAV 云端恢复【${name}】的历史存档吗？\n当前本地存档将被覆盖。`)) return;
            const res = await fetch('/api/cloud/restore', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ appId: appId })
            });
            const data = await res.json();
            if (data.success) {
                alert(`🎉 游戏【${name}】存档已成功从云端恢复至本地对应目录！`);
            } else {
                alert(`恢复失败: ${data.message || '未检测到可用的云端备份'}`);
            }
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
        applyLanguage();
        loadScripts();
        setInterval(fetchStatus, 3000);
    </script>
</body>
</html>)rawhtml";
} // namespace

std::string StaticAssets::GetIndexHtml() {
    std::string html = kRawIndexHtml;
    size_t pos = html.find("{{VERSION}}");
    if (pos != std::string::npos) {
        html.replace(pos, 11, OMNISTEAM_VERSION);
    }
    return html;
}

} // namespace Manager
