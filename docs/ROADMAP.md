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

1. ~~**签名锚点补全（核心阻塞）**~~ **已完成主链路**：重定位解析层（ELF `R_X86_64_RELATIVE` addend / Mach-O `LC_DYLD_INFO` rebase 与 chained fixups）打通了 strip 后的 RTTI 遍历，Windows 锚点图按 Itanium 同序槽位跨 ABI 移植；限定名 VProf 探针修复了 `RecvPkt`/`BBuildAndAsyncSendFrame` 的"0 xref"。Windows 15 / Linux 16 / macOS 16 函数，`tools/validate_signatures.py` 双机制交叉校验守门。
2. **Package 0 扩容（部分完成）**：`GetPackageInfo` **已三平台自动派生**（Windows `0x49b040` / Linux `0xeb8b70` / macOS `0x59a008`，随版本自动重算）。它是非虚函数，vtable 槽位无法覆盖，改用新增的**被调指纹派生**：先按字面量锚定调用方 `CheckAppOwnership`，再在其直接 callee 中匹配 utlrbtree 查找结构（`[this+0x20]` 根索引、`[this+0x38]` 节点数组、`shl 5` 步长、`[node+0x10]` 键比较、且含创建路径调用）。第二机制 `package_info_print` 控制台命令注册点独立复核，已纳入 CI 守门。
   许可子系统调用方已锚定：`CUser::BUpdateLicenses` 与 `CCompatManager::OnLicensesUpdated` 的限定名 VProf 字面量在 WIN/LIN/MAC 上均为唯一 xref，已作为 `STRING_PROBES` 加入派生层（win 15 / lin 16 / mac 16 函数，`validate_signatures.py` 10 项交叉校验 0 失败，损坏 anchor-map 的负向对照正确非零退出）。二者 RVAs：

   | 锚点 | Windows | Linux | macOS |
   | :--- | :--- | :--- | :--- |
   | `CUser::BUpdateLicenses` | `0x998fcb` | `0x1632fe0` | `0xa75220` |
   | `CCompatManager::OnLicensesUpdated` | `0x59f730` | `0x102de80` | `0x69e1b0` |

   许可三件套中的两个已按双机制发布（`derived:license-dual-chain`）：`MarkLicenseAsChanged`（WIN `0x9aa750` / LIN `0x1620d50` / MAC `0xa70f0c`，签名 `(this, packageId, bool)`）与 `ProcessPendingLicenseUpdates`（WIN `0x9986e0` / LIN `0x1621620` / MAC `0xa74e12`，按 `0x50` 步长遍历待处理向量并回写计数、返回 bool；WIN 的 `0x998700` 是其循环冷块，公共入口经 .pdata 前向回溯确定）。两条共享零代码路径的推导链：**生产链**（string-xref 族）以 `CPICSJobRequestProductInfoUpdate::BParseResponseMessage` 限定名 VProf 字面量锚定调用方，匹配 `(user=[cache+0x1d8], pkg, false)` 调用点形状得到 Mark，再取 Mark 的唯一向量排空调用方得到 PPLU；**守门链**（RTTI 族）从 CUser typeinfo 遍历 vtable 槽位，找到调用排空函数的虚槽得到 PPLU，再取其唯一 bool=false 被调得到 Mark。两链必须各自唯一且 RVA 一致才写入 TOML，`validate_signatures.py` 独立重跑两链复核。`CInternalCallback<..., LicensesUpdated_t>` 的 `Run()` 被 ICF 合并，不能作为机制（历史结论保留）。`CUtlMemoryGrow` **正式挂起**：钩子可选（`GrowAndAppend` 有显式 malloc 兜底），模板实例化众多且旧字节特征已死，无唯一锚点，fail-closed 不发布。
3. **steamui 库可见性（macOS 已端到端接线；Windows 待 64 位参考二进制）**：运行时 `PatternLoader` 改为多模块消费——steamclient 与 steamui 各自哈希、各自基址应用 TOML（本地目录、CDN 远程兜底、运行时符号表均按模块分别执行）；缓存键为两模块组合哈希，任一变更即失效；导出兜底按特征条目所属模块查询。fat Mach-O 的 TOML schema 扩展为 `rva`（x86_64）+ `rva_arm64`，运行时读取已加载 slice 的 mach_header cputype 选择字段，缺字段 fail-closed（回归测试 `TestSignatureArchSelection`）。派生端 `parse_macho` 支持按 CPU 选 slice，新增 ARM64 ADRP+ADD xref 索引；Tier 6 双链：M1 为 `CSteamUIAppController::FillInAppOverview` 限定名字面量，M2 为 `CSteamUIAppController::BuildCompleteAppOverviewChange - app` 字面量锚定的调用方内调用点指纹（刚追加进向量的 overview 消息作为第二参数传入：x86 匹配 `mov rsi, rax` + 元素槽存储，arm64 匹配 `str x1, [xN, #8]` 且 x1 未被改写）。两链一致才发布：x86_64 `0x574a40` / arm64 `0x5cbedc`，validate 交叉校验 0 失败。**Windows 仓库参考 steamui.dll 实为 i386**（早先"指针表"假设不成立——32 位绝对寻址被误读为 RIP 相对）；`parse_pe` 与 harvest 现在直接拒绝非 x64 PE。真机 64 位客户端确认附带 64 位 `steamui.dll`，其 `FillInAppOverview` 经双链发布为 `0x63b0d0`；三个 CI 平台 job 均已带 steamui 派生循环（mac 自 `0abe857`，win/lin 自 `44fdbbe`）。
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
   另：仓库内参考 `steamui.dll` 为 i386 二进制（特征 0 命中属位宽失配）；**真机 64 位客户端确认附带 64 位 `steamui.dll`**（2026-11-14 CI 采集，`FillInAppOverview` 字面量在 x64 PE 命中 `0x63b0d0`），i386 参考仅作历史样本保留。
11. **新客户端编码漂移（2026-11-14 CI 实测三处，均已适配）**：
    - macOS 客户端从 classic `LC_DYLD_INFO` rebase 切到 `LC_DYLD_CHAINED_FIXUPS`（`DYLD_CHAINED_PTR_64_OFFSET`，fmt 6）。旧 chained 解析器位域全错，按 Apple `fixup-chains.h` 重写：ptr64 的 `next` 在 bits 51-62（4 字节步长）、`high8`（bits 36-43）补目标 bits 56-63、`segment_offset` 相对 image base 而非段 vmaddr、page_start 逐页累加且 MULTI 项是 chain_starts[] 索引。四处任一错都会让指针索引静默为空、整条 RTTI 族失效（新 mac 客户端曾因此 transfer 0）。重写后新 mac 客户端 RTTI 族复活，trio 双链恢复。
    - 待处理许可向量的排空循环从指针步长（`sub reg, 0x50`）改为索引步长（`lea r,[r+r*4]` + scale-8 索引 / `shl 4`），且 tag 比较出现反向位移编码（指针自向量尾向前跑：`[end-0x50+0x14]` == `[end-0x3c]`，与 `[elem+0x14]` 同字段）。谓词接受两种步长与两种位移，并加两条收紧子句防假阳性：tag 比较必须唯一（许可 dispatcher 会比多个 tag）、函数体 ≤256 条指令（真排空 31-75 条，dispatcher 数百条）。
    - Windows `steamui.dll` 的 `FillInAppOverview` 调用点落在冷块，且 MSVC 把新追加消息经 `r9` 中转（`mov [rax+rcx*8+8], r9` + `mov rdx, r9`）。M2 指纹改为"元素槽存储源寄存器 == arg2（rsi/rdx）最后写入源"，并在整个热/冷簇内扫描调用点。
10. **macOS universal CI 构建**：首次 push 触发的 CI 暴露三处 macOS runner 问题，均已修复：
   `cmake_args` 里 `arm64;x86_64` 未加引号被 shell 当命令分隔符（exit 127）→ 单引号包裹；
   Homebrew Python 受 PEP 668 保护导致 `pip install capstone` 退出 1 → harvest job 改用一次性 venv（derive 与 validate 两步都走 venv）；
   `FindOpenSSL` 抓到 arm64-only 的 Homebrew `openssl@4`，x86_64 slice 链接报 `_EVP_sha256 not found` → `src/Platform/Darwin/Hash.cpp` 改用 libSystem 的 CommonCrypto，APPLE 分支不再链接 OpenSSL。
   `23598e8` 的 CI 已全绿闭环：Windows x64 / ubuntu x86_64 / macOS universal 三个 build 全部 `success`，Signature Harvest 全部 `success`（`e97498a` 的工作流被新 push 的 concurrency 取消，由 `23598e8` 覆盖验证）。

## 3. 后续演进规划 【📋 规划中】

1. **纯本地成就 Schema 离线构造**：客户端本地解包游戏 appcache / Schema 元数据并拦截 `CMsgClientGetUserStatsResponse` 合成本地响应，消除外部供体（Donor）依赖。
2. **离线 LAN 联机广播支持**：局域网 Lobby 广播与 SteamSockets 本地模拟，覆盖断网对战场景。
3. **游戏侧 steam_api 伴侣模式**：覆盖非 Steam 客户端直接拉起场景的对等 API 代理。

