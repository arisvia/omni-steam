# OmniSteam 开发路线图 (Roadmap)

> 状态基线：2026-11。Windows 全功能可用；Linux/macOS 骨架（注入载体、funchook、Manager/云存档）已通，函数签名覆盖仍不完整（见 §3）。

## 1. 核心架构与功能现状 【主体落地；平台状态见各行】

| 模块 | 实施状态 | 核心实现与保障 |
| :--- | :--- | :--- |
| **跨平台底层 (OmniPlatform)** | ✅ Windows / Linux / macOS | funchook 统一挂钩；系统目录监视双缓冲热重载；跨平台统一进程探测（`/proc`、`sysctl`、Toolhelp）；macOS universal（arm64+x86_64）构建。**注意：steamclient 签名已三平台全覆盖（win 15 / lin 16 / mac 16，mac 含 arm64 双字段）；Linux 侧仍缺 `steamui` 模块（`steamui.so` 只随桌面客户端发布，CI 的 steamcmd 拿不到），故 UI 可见性钩子在 Linux 按缺模块降级**。 |
| **协议与所有权核心 (Core)** | ✅ Windows / macOS（arm64 真机 12 钩子全附着）；⚠️ Linux 待真机复核 | Package 0 原生增量扩容；AES 密钥快速回填；追加字段式请求码与令牌改写；无锁化 RichPresence 状态投递。逻辑全平台共享，函数锚点三平台均已发布（Linux TOML 16/16）。 |
| **成就系统与高可用** | ✅ Windows / macOS；⚠️ Linux 待真机复核 | Player.GetUserStats / eMsg 818 双协议劫持；stats 节点宕机/断网 Tier 3 自动本地合成空响应保底。 |
| **安全与防崩溃机制** | ✅ 全平台 | 30+ 款竞技游戏反作弊白名单直通；进程退出 detours 由 OS 回收杜绝 Loader Lock 崩溃；`g_watchThread` 显式分离。 |
| **Manager 独立管理端** | ✅ Web / CLI / 托盘 | 本地 23 万+ Depot 密钥与 CDN 实时同步；Lua 一键生成与 Base/DLC Depot 密钥单趟合并；Windows 状态托盘与有序关停。 |
| **跨平台云存档** | ✅ WebDAV / 本地快照 | `remotecache.vdf` 零开销路径解析；游戏退出单次文件事件唤醒；本地滚动快照与 WebDAV 双通道。 |
| **CI 与自动化签名维保** | ✅ 3 平台原生 Runner（Linux x64 / Windows x64 / macOS universal） | 限定名 VProf string-xref + 重定位解析后的跨 ABI vtable 锚点移植；`validate_signatures.py` 双机制交叉校验，不一致即拒绝发布。 |
## 2. 非 Windows 平台差距清单 【🔧 进行中】

按打通顺序排列：

1. ~~**签名锚点补全（核心阻塞）**~~ **已完成主链路**：重定位解析层（ELF `R_X86_64_RELATIVE` addend / Mach-O `LC_DYLD_INFO` rebase 与 chained fixups）打通了 strip 后的 RTTI 遍历，Windows 锚点图按 Itanium 同序槽位跨 ABI 移植；限定名 VProf 探针修复了 `RecvPkt`/`BBuildAndAsyncSendFrame` 的"0 xref"。Windows 15 / Linux 16 / macOS 16 函数，`tools/validate_signatures.py` 双机制交叉校验守门。
2. **Package 0 扩容（部分完成）**：`GetPackageInfo` **已三平台自动派生**（当前客户端 Windows `0x4df9f0` / Linux `0xf812a0` / macOS x86_64 `0x5619c6`+arm64 `0x5bb318`，随版本自动重算；Linux 这条 2026-11-19 才补齐——被调指纹原先只认 MSVC/clang 的原地 `cmp [node+0x10]`，GCC 先发 `mov ecx,[rdx+0x10]` 再 `cmp ebx,ecx`，ELF 因此零候选）。它是非虚函数，vtable 槽位无法覆盖，改用新增的**被调指纹派生**：先按字面量锚定调用方 `CheckAppOwnership`，再在其直接 callee 中匹配 utlrbtree 查找结构（`[this+0x20]` 根索引、`[this+0x38]` 节点数组、`shl 5` 步长、`[node+0x10]` 键比较、且含创建路径调用）。第二机制 `package_info_print` 控制台命令注册点独立复核，已纳入 CI 守门。
   许可子系统调用方已锚定：`CUser::BUpdateLicenses` 与 `CCompatManager::OnLicensesUpdated` 的限定名 VProf 字面量在 WIN/LIN/MAC 上均为唯一 xref，已作为 `STRING_PROBES` 加入派生层（win 15 / lin 16 / mac 16 函数，`validate_signatures.py` 10 项交叉校验 0 失败，损坏 anchor-map 的负向对照正确非零退出）。二者 RVAs：

   | 锚点 | Windows | Linux | macOS |
   | :--- | :--- | :--- | :--- |
   | `CUser::BUpdateLicenses` | `0x998fcb` | `0x1632fe0` | `0xa75220` |
   | `CCompatManager::OnLicensesUpdated` | `0x59f730` | `0x102de80` | `0x69e1b0` |

   许可三件套中的两个已按双机制发布（`derived:license-dual-chain`）：`MarkLicenseAsChanged`（WIN `0x9aa750` / LIN `0x1620d50` / MAC `0xa70f0c`，签名 `(this, packageId, bool)`）与 `ProcessPendingLicenseUpdates`（WIN `0x9986e0` / LIN `0x1621620` / MAC `0xa74e12`，按 `0x50` 步长遍历待处理向量并回写计数、返回 bool；WIN 的 `0x998700` 是其循环冷块，公共入口经 .pdata 前向回溯确定）。两条共享零代码路径的推导链：**生产链**（string-xref 族）以 `CPICSJobRequestProductInfoUpdate::BParseResponseMessage` 限定名 VProf 字面量锚定调用方，匹配 `(user=[cache+0x1d8], pkg, false)` 调用点形状得到 Mark，再取 Mark 的唯一向量排空调用方得到 PPLU；**守门链**（RTTI 族）从 CUser typeinfo 遍历 vtable 槽位，找到调用排空函数的虚槽得到 PPLU，再取其唯一 bool=false 被调得到 Mark。两链必须各自唯一且 RVA 一致才写入 TOML，`validate_signatures.py` 独立重跑两链复核。`CInternalCallback<..., LicensesUpdated_t>` 的 `Run()` 被 ICF 合并，不能作为机制（历史结论保留）。`CUtlMemoryGrow` **正式挂起**：钩子可选（`GrowAndAppend` 有显式 malloc 兜底），模板实例化众多且旧字节特征已死，无唯一锚点，fail-closed 不发布。
3. **steamui 库可见性（macOS 已端到端接线；Windows 待 64 位参考二进制）**：运行时 `PatternLoader` 改为多模块消费——steamclient 与 steamui 各自哈希、各自基址应用 TOML（本地目录、CDN 远程兜底、运行时符号表均按模块分别执行）；缓存键为两模块组合哈希，任一变更即失效；导出兜底按特征条目所属模块查询。fat Mach-O 的 TOML schema 扩展为 `rva`（x86_64）+ `rva_arm64`，运行时读取已加载 slice 的 mach_header cputype 选择字段，缺字段 fail-closed（回归测试 `TestSignatureArchSelection`）。派生端 `parse_macho` 支持按 CPU 选 slice，新增 ARM64 ADRP+ADD xref 索引；**fat Mach-O 的 arm64 slice 现已由四类 slice 原生机制覆盖（16/16 函数带 `rva_arm64`）**：string-xref、UI 双链、**vtable 自迁移**（同一 fat 的 x86_64 slice 直接充当 reference——Itanium typeinfo 名与槽位序跨 slice 不变，故 `vtable_slots_for_rva(x86)` + `transfer_via_vtable(arm)` 无需外部参考；以已发布的 `RecvPkt` arm64 值作控制项验证）、以及 callee-fingerprint 与 license-dual-chain 的 **AArch64 孪生 tier**（AAPCS 第三参 `w2` 布尔、`bl #0x..` 调用点、`ldur [x,#-0x3c]`+`cmp #0x50` 的 pending-vector drain 形态、`umull`×80 索引步长、`0x94000000` 掩码全 .text 调用点扫描）。合并走 `merge_arm64_mechanism` 否决制：两机制分歧即丢弃（错 RVA 崩客户端，缺 RVA 只降级钩子）；`FillInAppOverview` 由 UI 双链独占，不参与 vtable 迁移竞争。实现期三个静默失效点已固化：capstone 的 ARM64 printer 位移/立即数按**十六进制**打印（`#0x50`），字符串或十进制匹配会全部落空；内存操作数自身含逗号（`x0, [x20, #0x190]`），按逗号 split 会截断；架构分发不能只换反汇编器，助记符（`call`→`bl`）与调用点扫描都要换。Tier 6 双链：M1 为 `CSteamUIAppController::FillInAppOverview` 限定名字面量，M2 为 `CSteamUIAppController::BuildCompleteAppOverviewChange - app` 字面量锚定的调用方内调用点指纹（刚追加进向量的 overview 消息作为第二参数传入：x86 匹配 `mov rsi, rax` + 元素槽存储，arm64 匹配 `str x1, [xN, #8]` 且 x1 未被改写）。两链一致才发布：x86_64 `0x574a40` / arm64 `0x5cbedc`，validate 交叉校验 0 失败。**Windows 仓库参考 steamui.dll 实为 i386**（早先"指针表"假设不成立——32 位绝对寻址被误读为 RIP 相对）；`parse_pe` 与 harvest 现在直接拒绝非 x64 PE。真机 64 位客户端确认附带 64 位 `steamui.dll`，其 `FillInAppOverview` 经双链发布为 `0x63b0d0`；三个 CI 平台 job 均已带 steamui 派生循环（mac 自 `0abe857`，win/lin 自 `44fdbbe`）。
4. **ABI 逐平台验证**：`CNetPacket`（+0x10/+0x18）与 `AppOwnership`（0x24/0x28）等布局锚点源自 Windows 客户端，需在 Linux/macOS 真机客户端上实测确认。
5. **i386 已完全移除**：Valve 已于 2026-10 将 Linux 客户端推向 64 位。CI/Release 不再构建 i386；CMake 在 32 位工具链下直接 `FATAL_ERROR`；`OMNI_ARCH_X86` 宏、ELF32 解析分支、32 位 xref 路径、`ubuntu12_32` 安装路径与 `linux-i386` 签名命名空间均已删除。harvest/derive 遇到 32 位 ELF 直接跳过。
6. **注入自动化（Manager 侧已落地，SteamRT 穿透待真机）**：新增 `Manager::SteamLauncher`（`apps/manager/src/SteamLauncher.cpp`）与 `POST /api/steam/launch` 端点、UI"启动 Steam（带注入）"按钮：Windows 直接拉起 steam.exe（代理 DLL 自动注入）；Linux 双 fork+setsid detached 启动并前置 `LD_PRELOAD=<steam>/ubuntu12_64/libomnisteam.so`；macOS 直接 exec `Steam.app/Contents/MacOS/steam_osx` 并前置 `DYLD_INSERT_LIBRARIES`（`open -a` 不转发环境变量）。脚本侧 `scripts/omnisteam.sh` 补已安装路径回退、新增 `scripts/omnisteam-macos.sh`。SIP 不剥离 DYLD 已获真机确认（见 §2.7）；SteamRT 容器穿透已验证（见条目 8：核心在容器内宿主成功附着）。
7. **macOS arm64 实测【✅ 已闭环：注入 + funchook + 全钩子真机绿】**：funchook 官方声明不支持 macOS arm64（`mprotect` EINVAL）。**2026-11-17 macos runner 真机确认的完整启动链**：① SIP 不剥离 `DYLD_INSERT_LIBRARIES`（Valve 开发者签名可插入）；② `steam_osx` 先 exec 到 `Steam.AppBundle/Steam/Contents/MacOS/steam_osx`（同 pid，DYLD 穿透，路径必须绝对）；③ **最终宿主是新 pid 的 spawn，环境被清洗，DYLD 在该跳丢失**——文件包装脚本路线被 Steam 包完整性修复还原，最终改为 **`__DATA,__interpose` 拦截 `posix_spawn`/`posix_spawnp`/`execve` 并在 spawn 时重注入 DYLD**（`src/Platform/Darwin/SpawnInterpose.cpp`，直接编入核心源码，静态库会被 dead-strip）。两个必须遵守的细节：插入型 dylib 装载在 libSystem 之后，`dlsym(RTLD_NEXT)` 永远找不到被拦截函数（全部返回 ENOSYS），而 `dlopen`+`dlsym` 又可能解析回我们自己的 interposer 造成无限递归（实测 5.8 万层后栈守卫区崩溃）——正确做法是直接 `extern "C"` 绑定 libsystem_kernel 的 `__posix_spawn`/`__execve` 系统调用桩；重注入必须按目标 basename 门控（只注入 steam 宿主），Steam Helper/CEF 带 library-validation 签名，插入未签名 dylib 会被 dyld abort。**另修复 P0 级构建缺陷：CMake APPLE 分支从未定义 `OMNI_PLATFORM_MACOS`，macOS 核心此前完全没有构造函数入口、所有 mac 守卫落入 Linux 分支——dylib 装载即惰性，本次才首次真正激活**。funchook arm64 mprotect 实测可用（run 44440919734：arm64 原生宿主 12 钩子全部成功改写，含 `CheckAppOwnership`/`RecvPkt`/`FillInAppOverview`）。
8. **真机冒烟验证（Windows ✅ / macOS arm64 ✅ / Linux 收敛中）**：`core_status.json` 全量 12 钩子逐 hook 上报（解析失败显式 false）+ 锚点地址表（`HookStatus::BuildCoreStatusJson`，单测 `HookStatusTests`）；Manager `/api/status` 透传 `hooks`/`anchors`，UI 药丸动态渲染（消灭 PackageInfo 假绿）；守门工具 `tools/smoke_status.py`（文件/`--url`、平台推断、已知降级 warn 不 gate）。`.github/workflows/hook-smoke.yml` 三平台真机 job + `docker/linux-smoke.Dockerfile` 本地容器。**首跑战果（2026-11-17，run 44064598296）：Windows 全绿**——静态别名探针在真实 `steamclient64.dll` 通过（`0x4de420`，541 字节/165 指令，edx/r8b 直接消费），动态冒烟 19 ok / 1 warn / 0 fail（12 钩子全附着含 `GetOrAddAppDataHook`/`FillInAppOverview`，Steam 存活、心跳新鲜）。CI 设施修复历程：apt 硬依赖拆分（noble 无 libgconf-2-4）、`steam_latest.deb` 稳定源、7z 解包 SteamSetup（NSIS 静默安装在托管 runner 不落盘）、DYLD 绝对路径。**macOS Apple Silicon 战果（2026-11-19，run 44440919734）：arm64 原生冒烟 19 ok / 1 warn / 0 fail**，12 钩子全附着，锚点 `MarkLicenseAsChanged=0xa92d18`、`ProcessPendingLicenseUpdates=0xa9725c`（运行时地址减装载基址即推导 RVA，证明 arm64 字段被真实消费）；唯一 warn 仍是设计挂起的 `CUtlMemoryGrow`。Rosetta x86_64 探针同轮确认全部 x86 签名解析成功（含别名修复），但 x64 Chromium-on-arm 在装钩后崩溃导致 status 未写出，已按"仅 arm64 门控、Rosetta 为信息性签名校验"降级为 warning。
   Linux SteamRT 容器穿透真因（run 44444922527，2026-11-19 定案，此前"glibc 版本错位"假设已被证据推翻）：核心装载进容器**完全成功**——`libomnisteam.so` 在容器 mount ns 可见、PID 13295 构造函数执行、宿主守卫放行、spdlog 落盘。两处独立缺口：① **pressure-vessel 清洗环境变量**，内层宿主看不到 `OMNISTEAM_CACHE_DIR`，`core_status.json` 写到默认 `$HOME/.cache/omnisteam/`（`$HOME` 按同路径 bind 进容器），而门禁只认注入目录 → CI 现同时监听两处并归一化；② **核心等 `steamclient.so` 上限固定 60s**，冷启动容器此时仍在跑 bootstrap（steamclient.so 缺席，Steam 自身 03:01:46 才打印 `Nothing to do`）→ 等待上限默认放大到 600s（前 10s 每 100ms 快轮询，之后每 1s；`OMNISTEAM_TARGET_MODULE_WAIT_SECONDS` 只能覆盖容器**外**的进程，pv 清洗环境，所以必须由默认值承担），CI 兜底轮询同步延长。辅助结论：`LD_PRELOAD` 绝对路径是容器可见的**前提**（ubuntu12_64 由 bootstrap 复制，pv 内 `/tmp` 不可见）；32 位子进程报 `wrong ELF class: ELFCLASS64` 属预期忽略；stderr 面包屑改为仅对通过守卫的宿主打印一次，避免每个子进程一行淹没证据。

9. **内置字节特征已对当前客户端失配**：`PatternLoader::RegisterCoreSignatures` 里的
   Windows 特征（`GetPackageInfo`/`MarkLicenseAsChanged`/`ProcessPendingLicenseUpdates`/
   `CUtlMemoryGrow`/`CheckAppOwnership`/`GetOrAddAppData`）在本机参考版
   `steamclient64.dll`（sha `71b391fe…`）上**全部 0 命中**，只有 `RecvPkt` 等少数仍匹配。
   这些特征属于旧版本，实际可用性依赖派生层而非内置特征。保留它们作为旧版兜底，
   但平台能力不应再以"内置特征齐全"为依据。
   **2026-11-17 全量审计 + 已拍板落地（降级不删除）**：13 条内置特征全在 Windows 分支；
   ① `BBuildAndAsyncSendFrame` 必留——当前 Windows TOML 不含它（MSVC 限定名 VProf 字面量进指针表，string-xref 拿不到），内置特征是唯一机制（根治待 derive 指针表 xref 层）；
   ② `g_patterns` 兼任"期望目标清单"，驱动远程 CDN 触发、缓存完整性判定、RVA 缓存模块归属、导出兜底模块选择四处非特征逻辑——已改为**三平台统一清单 + 空特征降级**（扫描前 `pattern.empty()` 短路），远程触发改用"已加载模块中存在未解析注册目标"判据，修复 POSIX 自愈层永不触发的缺陷；完整性判定跳过未加载模块（steamui 可选）；`CUtlMemoryGrow` 不入清单（挂起项，避免远程层每次启动空转）；
   ③ `GetOrAddAppData` 命名断裂已接别名（`NormalizeFunctionName`：`GetAppDataFromAppInfo → GetOrAddAppData`），语义同一性由双重门把关：`tools/verify_appdata_alias.py` 静态形状探针（.pdata 定界 + capstone 校验 edx/r8b 参数消费 + 许可管线调用点佐证）与 hook-smoke 真机动态冒烟。**Windows 真机裁决已通过（2026-11-17）：探针 PASS + 钩子附着 + Steam 存活，别名在 Windows 复活**；macOS arm64 原生冒烟已通过（2026-11-19 run 44440919734，12 钩子全附着含 `GetOrAddAppDataHook`）；Linux 待本轮冒烟复核；
   ④ Linux `GetPackageInfo` 缺口**已闭合（2026-11-19）**：被调指纹的 `[node+0x10]` 键比较子句原先只认 MSVC/clang 的原地 `cmp`，加上 GCC 的"先 load 进寄存器再 cmp"形态后，ELF 客户端唯一命中 `0xf812a0`（`validate_signatures.py` 独立复核同值），Linux TOML 升至 16/16，`smoke_status.py` 的 linux 降级白名单随之撤销。
   维护性：清单只在钩子面变化时维护，客户端更新零维护（RVA 全部来自 data 分支 TOML 自动重算）；唯一残留漂移项是 ① 的单条字节特征。
   另：仓库内参考 `steamui.dll` 为 i386 二进制（特征 0 命中属位宽失配）；**真机 64 位客户端确认附带 64 位 `steamui.dll`**（2026-11-14 CI 采集，`FillInAppOverview` 字面量在 x64 PE 命中 `0x63b0d0`），i386 参考仅作历史样本保留。
11. **新客户端编码漂移（2026-11-14 CI 实测三处，均已适配）**：
    - macOS 客户端从 classic `LC_DYLD_INFO` rebase 切到 `LC_DYLD_CHAINED_FIXUPS`（`DYLD_CHAINED_PTR_64_OFFSET`，fmt 6）。旧 chained 解析器位域全错，按 Apple `fixup-chains.h` 重写：ptr64 的 `next` 在 bits 51-62（4 字节步长）、`high8`（bits 36-43）补目标 bits 56-63、`segment_offset` 相对 image base 而非段 vmaddr、page_start 逐页累加且 MULTI 项是 chain_starts[] 索引。四处任一错都会让指针索引静默为空、整条 RTTI 族失效（新 mac 客户端曾因此 transfer 0）。重写后新 mac 客户端 RTTI 族复活，trio 双链恢复。
    - 待处理许可向量的排空循环从指针步长（`sub reg, 0x50`）改为索引步长（`lea r,[r+r*4]` + scale-8 索引 / `shl 4`），且 tag 比较出现反向位移编码（指针自向量尾向前跑：`[end-0x50+0x14]` == `[end-0x3c]`，与 `[elem+0x14]` 同字段）。谓词接受两种步长与两种位移，并加两条收紧子句防假阳性：tag 比较必须唯一（许可 dispatcher 会比多个 tag）、函数体 ≤256 条指令（真排空 31-75 条，dispatcher 数百条）。
    - Windows `steamui.dll` 的 `FillInAppOverview` 调用点落在冷块，且 MSVC 把新追加消息经 `r9` 中转（`mov [rax+rcx*8+8], r9` + `mov rdx, r9`）。M2 指纹改为"元素槽存储源寄存器 == arg2（rsi/rdx）最后写入源"，并在整个热/冷簇内扫描调用点。
12. **Linux 真实运行体覆盖与热补丁桩（2026-11-19 实测）**：
    - **签名必须按"宿主真正 dlopen 的那份"发布**。Linux 有两个并行构建：steamcmd 的 `ubuntu12_64`（`744bfba676ac5155`）与 Steam 自更新落盘的 `steamrt64`；真机容器里宿主加载的是后者（`dab8df0daa27ffa6`），而收割只取前者 → 11 钩子里 9 个 "manifest-only entry, no fallback pattern"，只有导出符号与运行时符号表能兜住的 3 个起来。收割现已安装 launcher deb、解包 bootstrap tar、起真客户端自更新，**并等 sha256 连续两次采样不变**才收——文件"出现"时还是 deb 自带的滞后构建（体积 52.8MB → 稳定后 53.6MB），早期版本就是被这一步骗过，发布了两把无人加载的哈希。
    - **funchook 搬不动 Valve 热补丁桩的 8 位条件跳转**：`OptedInMask` 在两个 Linux 构建里都是 `cmp esi,0xf` / `ja rel8`(`77 13`) / `mov rdi,[rdi+0x180]` / `jmp rel32`，funchook 对被窃取指令区里的 ip-relative 位移只支持 32 位，直接 `FUNCHOOK_ERROR_CANNOT_FIX_IP_RELATIVE`（"Could not fix ip-relative address. The size is not 32."）→ 该钩子在 Linux 从未挂上过（与签名无关，是引擎限制）。修法在派生侧：证明桩只改写 rdi、其余寄存器由尾跳原样转发后，发布 **`OptedInMaskImpl`**（尾跳目标，`is_func_start` 且长度 ≥20 条才发），核心在桩 attach 失败时改挂 impl（**三参数**转发，因为实现体读 `edx`），状态仍登记在规范名 `OptedInMask` 下，不进 `g_patterns` 期望清单、不参与完整性判定。Windows/macOS 走桩路径成功，逻辑不变。
    - 另：`find -iname 'steamui*.so'` 这类 glob 从前导字符锚定，对 `<sha16>-steamui.so` 一律不匹配，曾让 Linux `steamui.so`（真机在 `steamrt64/` 里就有，`48e48023b7b5035b`）既没被派生也没进取证工件。

10. **macOS universal CI 构建**：首次 push 触发的 CI 暴露三处 macOS runner 问题，均已修复：
   `cmake_args` 里 `arm64;x86_64` 未加引号被 shell 当命令分隔符（exit 127）→ 单引号包裹；
   Homebrew Python 受 PEP 668 保护导致 `pip install capstone` 退出 1 → harvest job 改用一次性 venv（derive 与 validate 两步都走 venv）；
   `FindOpenSSL` 抓到 arm64-only 的 Homebrew `openssl@4`，x86_64 slice 链接报 `_EVP_sha256 not found` → `src/Platform/Darwin/Hash.cpp` 改用 libSystem 的 CommonCrypto，APPLE 分支不再链接 OpenSSL。
   `23598e8` 的 CI 已全绿闭环：Windows x64 / ubuntu x86_64 / macOS universal 三个 build 全部 `success`，Signature Harvest 全部 `success`（`e97498a` 的工作流被新 push 的 concurrency 取消，由 `23598e8` 覆盖验证）。

## 3. 后续演进规划 【📋 规划中】

1. **纯本地成就 Schema 离线构造**：客户端本地解包游戏 appcache / Schema 元数据并拦截 `CMsgClientGetUserStatsResponse` 合成本地响应，消除外部供体（Donor）依赖。
2. **离线 LAN 联机广播支持**：局域网 Lobby 广播与 SteamSockets 本地模拟，覆盖断网对战场景。
3. **游戏侧 steam_api 伴侣模式**：覆盖非 Steam 客户端直接拉起场景的对等 API 代理。

