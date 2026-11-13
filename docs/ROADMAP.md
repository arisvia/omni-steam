# OmniSteam 开发路线图 (Roadmap)

> 状态基线：2026-11。Windows 全功能可用；Linux/macOS 骨架（注入载体、funchook、Manager/云存档）已通，函数签名覆盖仍不完整（见 §3）。

## 1. 核心架构与功能现状 【主体落地；平台状态见各行】

| 模块 | 实施状态 | 核心实现与保障 |
| :--- | :--- | :--- |
| **跨平台底层 (OmniPlatform)** | ✅ Windows / Linux / macOS | funchook 统一挂钩；系统目录监视双缓冲热重载；跨平台统一进程探测（`/proc`、`sysctl`、Toolhelp）；macOS universal（arm64+x86_64）构建。**注意：非 Windows 平台的函数寻址仍依赖不完整的签名库，解锁链路尚未全量打通（见 §3）**。 |
| **协议与所有权核心 (Core)** | ✅ Windows；⚠️ Linux/macOS 受签名覆盖限制 | Package 0 原生增量扩容；AES 密钥快速回填；追加字段式请求码与令牌改写；无锁化 RichPresence 状态投递。逻辑全平台共享，但依赖的函数锚点目前仅 Windows 全量可解析。 |
| **成就系统与高可用** | ✅ Windows；⚠️ Linux/macOS 同上 | Player.GetUserStats / eMsg 818 双协议劫持；stats 节点宕机/断网 Tier 3 自动本地合成空响应保底。 |
| **安全与防崩溃机制** | ✅ 全平台 | 30+ 款竞技游戏反作弊白名单直通；进程退出 detours 由 OS 回收杜绝 Loader Lock 崩溃；`g_watchThread` 显式分离。 |
| **Manager 独立管理端** | ✅ Web / CLI / 托盘 | 本地 23 万+ Depot 密钥与 CDN 实时同步；Lua 一键生成与 Base/DLC Depot 密钥单趟合并；Windows 状态托盘与有序关停。 |
| **跨平台云存档** | ✅ WebDAV / 本地快照 | `remotecache.vdf` 零开销路径解析；游戏退出单次文件事件唤醒；本地滚动快照与 WebDAV 双通道。 |
| **CI 与自动化签名维保** | ✅ 3 平台原生 Runner（Linux x64 / Windows x64 / macOS universal） | 限定名 VProf string-xref + 重定位解析后的跨 ABI vtable 锚点移植；`validate_signatures.py` 双机制交叉校验，不一致即拒绝发布。 |
## 2. 非 Windows 平台差距清单 【🔧 进行中】

按打通顺序排列：

1. ~~**签名锚点补全（核心阻塞）**~~ **已完成主链路**：重定位解析层（ELF `R_X86_64_RELATIVE` addend / Mach-O `LC_DYLD_INFO` rebase 与 chained fixups）打通了 strip 后的 RTTI 遍历，Windows 锚点图按 Itanium 同序槽位跨 ABI 移植；限定名 VProf 探针修复了 `RecvPkt`/`BBuildAndAsyncSendFrame` 的"0 xref"。Linux/macOS 各 9 函数，`tools/validate_signatures.py` 双机制交叉校验守门。
2. **Package 0 扩容（部分完成）**：`GetPackageInfo` **已三平台自动派生**（Windows `0x49b040` / Linux `0xeb8b70` / macOS `0x59a008`，随版本自动重算）。它是非虚函数，vtable 槽位无法覆盖，改用新增的**被调指纹派生**：先按字面量锚定调用方 `CheckAppOwnership`，再在其直接 callee 中匹配 utlrbtree 查找结构（`[this+0x20]` 根索引、`[this+0x38]` 节点数组、`shl 5` 步长、`[node+0x10]` 键比较、且含创建路径调用）。第二机制 `package_info_print` 控制台命令注册点独立复核，已纳入 CI 守门。
   仍缺 `CUtlMemoryGrow`、`MarkLicenseAsChanged`、`ProcessPendingLicenseUpdates`：无唯一字面量、无 RTTI 类、且调用方本身尚未锚定，需先锚定许可子系统调用方再套用同一指纹机制。**按 fail-closed 原则不猜测 RVA。**
3. **steamui 库可见性（缺二进制）**：`FillInAppOverview` 只注册了 `steamui.dll`。本机参考集里**没有** `steamui.so`/`steamui.dylib`，无法派生；且当前 `steamui.dll` 内该限定名字面量 `CSteamUIAppController::FillInAppOverview` **零 xref**（经指针表引用，非 RIP LEA），内置字节特征在该版本也已失配。需先取得非 Windows 的 steamui 模块，再补指针表引用解析。
4. **ABI 逐平台验证**：`CNetPacket`（+0x10/+0x18）与 `AppOwnership`（0x24/0x28）等布局锚点源自 Windows 客户端，需在 Linux/macOS 真机客户端上实测确认。
5. **i386 已完全移除**：Valve 已于 2026-10 将 Linux 客户端推向 64 位。CI/Release 不再构建 i386；CMake 在 32 位工具链下直接 `FATAL_ERROR`；`OMNI_ARCH_X86` 宏、ELF32 解析分支、32 位 xref 路径、`ubuntu12_32` 安装路径与 `linux-i386` 签名命名空间均已删除。harvest/derive 遇到 32 位 ELF 直接跳过。
6. **注入自动化**：Linux Manager 尚未写 `LD_PRELOAD`（仅 `scripts/install-steamos.sh` 手动）；macOS 依赖手动 `DYLD_INSERT_LIBRARIES`；2026-10 beta 起 Linux 客户端进入 SteamRT 容器，`LD_PRELOAD` 穿透性待验证。
7. **macOS arm64 实测**：funchook 官方声明不支持 macOS arm64（`mprotect` EINVAL），universal 构建后需真机验证 x86_64（Rosetta 客户端）与 arm64 两条路径。
8. **真机冒烟验证**：以 `core_status.json` 的逐 hook 状态为准，在真实客户端上跑一次完整解锁链路（当前无此回归手段）。

9. **内置字节特征已对当前客户端失配**：`PatternLoader::RegisterCoreSignatures` 里的
   Windows 特征（`GetPackageInfo`/`MarkLicenseAsChanged`/`ProcessPendingLicenseUpdates`/
   `CUtlMemoryGrow`/`CheckAppOwnership`/`GetOrAddAppData`）在本机参考版
   `steamclient64.dll`（sha `71b391fe…`）上**全部 0 命中**，只有 `RecvPkt` 等少数仍匹配。
   这些特征属于旧版本，实际可用性依赖派生层而非内置特征。保留它们作为旧版兜底，
   但平台能力不应再以"内置特征齐全"为依据。

## 3. 后续演进规划 【📋 规划中】

1. **纯本地成就 Schema 离线构造**：客户端本地解包游戏 appcache / Schema 元数据并拦截 `CMsgClientGetUserStatsResponse` 合成本地响应，消除外部供体（Donor）依赖。
2. **离线 LAN 联机广播支持**：局域网 Lobby 广播与 SteamSockets 本地模拟，覆盖断网对战场景。
3. **游戏侧 steam_api 伴侣模式**：覆盖非 Steam 客户端直接拉起场景的对等 API 代理。

