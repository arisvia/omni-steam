# OmniSteam 各平台注入方案评估、选型与实施状态

> 状态基线：2026-11。本文档包含"方案对比（设计期）"与"实施状态（当前代码）"两部分。

## 1. 核心诉求与评判标准
- **隐蔽性 (Stealth)**：不触发 Steam 客户端完整性自检、不破坏系统只读保护、不被常见安全策略拦截。
- **稳定性 (Stability)**：Steam 自更新后不失效、不崩溃死锁、支持子进程继承。
- **易用性 (UX)**：部署步骤少、免 root / 免管理员提权。

---

## 2. Windows 平台

### 方案对比（设计期）

| 方案 | 隐蔽性 | 稳定性 | 易用性 | 综合评价 |
| :--- | :--- | :--- | :--- | :--- |
| **DLL Proxying (`xinput1_4.dll`)** | **极高** (OS 原生加载机制) | **极高** (Steam 启动即加载) | **高** (放置于 Steam 根目录) | **最优解 ✅ 已采用** |
| DLL Proxying (`dwmapi.dll`) | 高 | 中 (64 位 steam.exe 可经 KnownDLLs 直取系统 dwmapi) | 高 | **已退役** |
| 外部注入 (CreateRemoteThread 等) | 低 (易被杀软拦截) | 中 (依赖监听与时机) | 低 (需常驻 Injector) | 不推荐 |
| AppInit_DLLs / 注册表劫持 | 低 | 低 | 极低 | 坚决避免 |
| 快捷方式包装 (Launcher Wrapper) | 中 | 高 | 中 (自更新覆盖即失效) | 备选 |

### 实施状态：✅ 完整可用
- `src/Platform/Windows/Proxy/xinput1_4.cpp`：代理 DLL 完整转发 System32 `xinput1_4.dll` 全部导出（含 100-108 序数），
  `DLL_PROCESS_ATTACH` 时按自身模块绝对路径加载同目录 `libomnisteam.dll`（宿主 exe 目录与默认搜索为回退）。
- Core 初始化线程等待 `steamclient64.dll` 加载（最长 60s，超时明确报错退出，不会半装状态）。
- 特征签名与上游 opensteamtool 签名库逐项核对一致；RVA 缓存按二进制 SHA256 键控。
- Manager `CoreInstaller` 支持本地产物一键部署与 Release/Nightly 远程下载解压（校验解压结果）。

---

## 3. Linux / Steam Deck 平台

### 方案对比（设计期）

| 方案 | 隐蔽性 | 稳定性 | 易用性 | 综合评价 |
| :--- | :--- | :--- | :--- | :--- |
| **`LD_PRELOAD` 预加载** | **极高** | **极高** (完整继承子进程) | **极高** | **最优解 ✅ 已采用** |
| 动态库篡改 / 符号覆盖 steamclient.so | 极低 (自检触发重下) | 极低 (更新即失效) | 极低 | 坚决避免 |
| ptrace 注入 | 低 (yama 限制) | 中 (需 root/CAP_SYS_PTRACE) | 低 | 不推荐 |

### 实施状态：✅ 注入载体与函数寻址均已落地（steamclient 16/16），真机冒烟复核中
- 注入：`scripts/omnisteam.sh` 与 SteamOS 免 root 安装脚本（environment.d）就绪；Manager 侧 `SteamLauncher` 自动前置 `LD_PRELOAD`（见下）；
  Core 以 `LD_PRELOAD` 进入 Steam 进程后可正常初始化、解析 Lua、写心跳。
- Hook 层：`derive_signatures.py` 经 ELF `.eh_frame_hdr` 边界表 + `R_X86_64_RELATIVE` addend 解析后的
  RTTI/vtable 遍历，已产出 9 个函数锚点（含 `ConfigStore_GetBinary`/`RecvPkt`/`BBuildAndAsyncSendFrame`/
  `IPCProcessMessage`/`OptedInMask`），另有 `Steam_BGetCallback`/`Steam_FreeLastCallback` 走导出符号。
  仍缺 Package 0 扩容三件套与 `steamui.so` 的 `FillInAppOverview`（见 ROADMAP §2）。
  仅支持 64 位客户端：32 位 ELF 在 harvest/derive 阶段直接跳过。

- Manager `InstallCore` 自动下载/解压并校验 `libomnisteam.so` 落位 `ubuntu12_64/`；`Manager::SteamLauncher`（`POST /api/steam/launch`）以双 fork+setsid detached 启动 Steam 并前置 `LD_PRELOAD=<steam>/ubuntu12_64/libomnisteam.so`（保留既有 preload 链）。`scripts/omnisteam.sh` 支持 build 树与已安装路径双回退。
- **SteamRT（pressure-vessel）穿透要点**（2026-11-19 run 44444922527 实测）：
  ① `LD_PRELOAD` 必须指向容器内可见路径——`ubuntu12_64/libomnisteam.so`（bootstrap 复制而来）可用，`/tmp` 副本不可见；pv 只把 preload 转成 `--ld-preload`，**其余环境变量一律清洗**，故 `OMNISTEAM_CACHE_DIR` 到不了内层宿主，状态文件落在默认 `$HOME/.cache/omnisteam/`（`$HOME` 以同路径 bind 进容器，宿主外可读）。
  ② 进程链：`steamrt64/steam`（wrapper）→ `srt-client-container` → `srt-bwrap` → pv-adverb → 容器内 `steamrt64/steam`（真宿主）。每一跳都继承 preload；32 位子进程的 `wrong ELF class: ELFCLASS64` 是预期忽略，不是故障。
  ③ 冷启动容器里真宿主数分钟后才 `dlopen` `steamclient.so`（此前一直在跑 bootstrap 自检），核心等待上限由 `OMNISTEAM_TARGET_MODULE_WAIT_SECONDS` 调节（默认 60s，CI 冒烟设 600s）。

## 4. macOS 平台

### 方案对比（设计期）

| 方案 | 隐蔽性 | 稳定性 | 易用性 | 综合评价 |
| :--- | :--- | :--- | :--- | :--- |
| **`DYLD_INSERT_LIBRARIES`** | **极高** | **极高** | **高** | **最优解 ✅ 已采用** |
| LC_LOAD_DYLIB 篡改 | 低 (破坏签名, Gatekeeper 阻止) | 低 | 低 | 不推荐 |
| task_for_pid / mach_vm 注入 | 极低 (受 SIP 限制) | 低 | 极低 | 坚决避免 |

### 实施状态：✅ 注入链与全钩子寻址已在 Apple Silicon 真机闭环
- **启动链测绘结论（真机）**：SIP 不剥离 `DYLD_INSERT_LIBRARIES`（Valve 开发者签名可插入）；`Steam.app/steam_osx` 先 *exec* 到 `Steam.AppBundle/Steam/Contents/MacOS/steam_osx`（同 pid，DYLD 穿透，插入路径必须绝对）；随后**最终宿主是一次新 pid 的 spawn，环境被清洗，DYLD 在该跳丢失**。因此单靠外层环境变量注入永远只能命中中间进程。
- **落地方案：`__DATA,__interpose` 重注入**（`src/Platform/Darwin/SpawnInterpose.cpp`，直接编入核心源码——静态库会被 dead-strip）。拦截 `posix_spawn`/`posix_spawnp`/`execve`，在目标为 steam 宿主时把 `DYLD_INSERT_LIBRARIES=<本 dylib 绝对路径>` 写回 envp 再放行，故最终宿主与中间进程同样装载核心。文件包装脚本路线已否决：Steam 包完整性修复会还原被替换的二进制。
- **两条硬约束（踩过坑）**：① 解析被拦截函数必须 `extern "C"` 直绑 libsystem_kernel 的 `__posix_spawn`/`__execve` 系统调用桩——插入型 dylib 装载在 libSystem 之后，`dlsym(RTLD_NEXT)` 找不到它们（全部 ENOSYS），而 `dlopen`+`dlsym` 可能解析回我们自己的 interposer 造成无限递归；② 重注入按目标 basename 门控（只注入 `steam_osx`/`steam`），Steam Helper/CEF 带 library-validation 签名，插入未签名 dylib 会被 dyld abort。
- **运行前提**：headless runner 无 LaunchServices，需 `-inhibitbootstrap -skipinitialbootstrap` 跳过更新器循环（等价 `steam.cfg` 的 `BootStrapperInhibitAll enable`），并直接拉起 `ipcserver`（launchd headless 不会重启它，否则 `SteamAPI_Init failed`）。
- **Hook 寻址**：universal dylib 两 slice 各自按已装载 slice 的 `mach_header.cputype` 选择 `rva` / `rva_arm64`（缺字段 fail-closed）。arm64 slice 现由四类 slice 原生机制覆盖 16/16 函数（见 ROADMAP §2.3）。**funchook 在 macOS arm64 的 mprotect 实测可用**：run 44440919734 中 arm64 原生宿主 12 个钩子全部改写成功、`core_status.json` 19 ok / 1 warn / 0 fail。
- Rosetta x86_64 路径：签名解析与钩子安装同样全部成功，但 x64 Chromium-on-arm 在装钩后崩溃，故该探针仅作签名校验（warning），门控由 arm64 原生探针承担。

---

## 5. 跨平台共性保障（已落地）

| 机制 | 实现 |
| :--- | :--- |
| Hook 引擎 | funchook v1.1.3 + Capstone 5.0.1（x86/x64/ARM64），事务化 attach/commit |
| 目录监视热重载 | Windows `ReadDirectoryChangesW` / Linux `inotify` / macOS `kqueue`，双缓冲快照切换 |
| HTTP 栈 | WinHTTP（TLS 1.2+ 强制）/ libcurl，统一 UA 与端点常量 (`OmniEndpoints.h`) |
| 配置契约 | TOML 全局配置 + Lua 解锁脚本 + 二进制密钥库，三平台完全一致 |

### 运行时可调环境变量（默认值即生产行为，仅诊断/CI 覆盖）

| 变量 | 平台 | 默认 | 作用 |
| :--- | :--- | :--- | :--- |
| `OMNISTEAM_CACHE_DIR` | 全部 | Win `%LOCALAPPDATA%\OmniSteam\cache`；macOS `$HOME/Library/Caches/OmniSteam`；Linux `$XDG_CACHE_HOME/omnisteam`，无则 `$HOME/.cache/omnisteam` | Lua/密钥库/`core_status.json` 落位目录。**注意**：SteamRT pressure-vessel 会清洗环境变量，容器内真宿主只能用默认值（`$HOME` 同路径 bind 挂载，外挂可读），因此外部工具必须两处都认（见 §3）。 |
| `OMNISTEAM_TARGET_MODULE_WAIT_SECONDS` | 全部 | `60` | 等宿主装载 `steamclient`（.dll/.so/.dylib）的上限，超时则放弃挂钩（不崩宿主）。冷启动容器需数分钟，CI 冒烟设 `600`；取值范围 1–3600，越界回退默认。 |
| `OMNISTEAM_INTERPOSE_DEBUG` | macOS | 关 | `=1` 时逐条打印被拦截的 `posix_spawn`/`posix_spawnp`/`execve`。默认关闭：Steam 每个子进程都会触发，一次启动上万行会淹没真正的证据。 |
| `OMNISTEAM_FORCE_DIRECT_SPAWN` | macOS | 关 | `=1` 时把更新器最后一跳 `sh -c "sleep 1; open -n '<App>'"` 改写成直接 exec 宿主二进制（headless runner 没有可用的 LaunchServices 会话）。产品默认不动 Steam 的启动方式。 |
