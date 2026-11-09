# OmniSteam 开发路线图 (Roadmap)

> 状态基线：2026-11。全功能全平台完整可用，持续演进中。

## 1. 核心架构与功能现状 【✅ 全部落地】

| 模块 | 实施状态 | 核心实现与保障 |
| :--- | :--- | :--- |
| **跨平台底层 (OmniPlatform)** | ✅ Windows / Linux / macOS | funchook 统一挂钩；系统目录监视双缓冲热重载；跨平台统一进程探测（`/proc`、`sysctl`、Toolhelp）。 |
| **协议与所有权核心 (Core)** | ✅ 全平台 | Package 0 原生增量扩容；AES 密钥快速回填；追加字段式请求码与令牌改写；无锁化 RichPresence 状态投递。 |
| **成就系统与高可用** | ✅ 全平台 | Player.GetUserStats / eMsg 818 双协议劫持；stats 节点宕机/断网 Tier 3 自动本地合成空响应保底。 |
| **安全与防崩溃机制** | ✅ 全平台 | 30+ 款竞技游戏反作弊白名单直通；进程退出 detours 由 OS 回收杜绝 Loader Lock 崩溃；`g_watchThread` 显式分离。 |
| **Manager 独立管理端** | ✅ Web / CLI / 托盘 | 本地 23 万+ Depot 密钥与 CDN 实时同步；Lua 一键生成与 Base/DLC Depot 密钥单趟合并；Windows 状态托盘与有序关停。 |
| **跨平台云存档** | ✅ WebDAV / 本地快照 | `remotecache.vdf` 零开销路径解析；游戏退出单次文件事件唤醒；本地滚动快照与 WebDAV 双通道。 |
| **CI 与自动化签名维保** | ✅ 4 平台原生 Runner | Linux ELF `.eh_frame_hdr` 与 macOS Mach-O `LC_FUNCTION_STARTS` 自动符号派生；多平台二进制与测试矩阵联动。 |
## 2. 后续演进规划 【📋 规划中】

1. **纯本地成就 Schema 离线构造**：客户端本地解包游戏 appcache / Schema 元数据并拦截 `CMsgClientGetUserStatsResponse` 合成本地响应，消除外部供体（Donor）依赖。
2. **离线 LAN 联机广播支持**：局域网 Lobby 广播与 SteamSockets 本地模拟，覆盖断网对战场景。
3. **游戏侧 steam_api 伴侣模式**：覆盖非 Steam 客户端直接拉起场景的对等 API 代理。

