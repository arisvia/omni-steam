# OmniSteam 各平台注入方案评估、选型与实施状态

> 状态基线：2026-08。本文档包含"方案对比（设计期）"与"实施状态（当前代码）"两部分。

## 1. 核心诉求与评判标准
- **隐蔽性 (Stealth)**：不触发 Steam 客户端完整性自检、不破坏系统只读保护、不被常见安全策略拦截。
- **稳定性 (Stability)**：Steam 自更新后不失效、不崩溃死锁、支持子进程继承。
- **易用性 (UX)**：部署步骤少、免 root / 免管理员提权。

---

## 2. Windows 平台

### 方案对比（设计期）

| 方案 | 隐蔽性 | 稳定性 | 易用性 | 综合评价 |
| :--- | :--- | :--- | :--- | :--- |
| **DLL Proxying (`dwmapi.dll`)** | **极高** (OS 原生加载机制) | **极高** (Steam 启动即加载) | **高** (放置于 Steam 根目录) | **最优解 ✅ 已采用** |
| 外部注入 (CreateRemoteThread 等) | 低 (易被杀软拦截) | 中 (依赖监听与时机) | 低 (需常驻 Injector) | 不推荐 |
| AppInit_DLLs / 注册表劫持 | 低 | 低 | 极低 | 坚决避免 |
| 快捷方式包装 (Launcher Wrapper) | 中 | 高 | 中 (自更新覆盖即失效) | 备选 |

### 实施状态：✅ 完整可用
- `src/Platform/Windows/Proxy/dwmapi.cpp`：代理 DLL 完整转发 System32 `dwmapi.dll` 全部 32 个导出函数，
  `DLL_PROCESS_ATTACH` 时加载同目录 `libomnisteam.dll`（绝对路径优先，失败回退默认搜索）。
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

### 实施状态：⚠️ 注入链路通，Hook 层休眠
- 注入：`scripts/omnisteam.sh` 与 SteamOS 免 root 安装脚本（environment.d）就绪；
  Core 以 `LD_PRELOAD` 进入 Steam 进程后可正常初始化、解析 Lua、写心跳。
- Hook 层：当前**没有经过验证的 `steamclient.so` 函数特征签名**。为避免把 hook 挂到任意同名序言函数
  导致进程崩溃，`PatternLoader` 在该平台主动休眠并输出警告——解锁功能暂不可用，但注入框架随时待命。
- 后续计划：征集/逆向提取各版本 `steamclient.so` 差异化特征码后即可点亮全功能（架构无需改动）。

---

## 4. macOS 平台

### 方案对比（设计期）

| 方案 | 隐蔽性 | 稳定性 | 易用性 | 综合评价 |
| :--- | :--- | :--- | :--- | :--- |
| **`DYLD_INSERT_LIBRARIES`** | **极高** | **极高** | **高** | **最优解 ✅ 已采用** |
| LC_LOAD_DYLIB 篡改 | 低 (破坏签名, Gatekeeper 阻止) | 低 | 低 | 不推荐 |
| task_for_pid / mach_vm 注入 | 极低 (受 SIP 限制) | 低 | 极低 | 坚决避免 |

### 实施状态：⚠️ 同 Linux（注入通、Hook 层休眠）
- Universal 构建目标（arm64 + x86_64）已配置；无验证过的 `steamclient.dylib` 签名前，
  Hook 层主动休眠（见 §3 说明）。Manager 全功能可用。

---

## 5. 跨平台共性保障（已落地）

| 机制 | 实现 |
| :--- | :--- |
| Hook 引擎 | funchook v1.1.3 + Capstone 5.0.1（x86/x64/ARM64），事务化 attach/commit |
| 目录监视热重载 | Windows `ReadDirectoryChangesW` / Linux `inotify` / macOS `kqueue`，双缓冲快照切换 |
| HTTP 栈 | WinHTTP（TLS 1.2+ 强制）/ libcurl，统一 UA 与端点常量 (`OmniEndpoints.h`) |
| 配置契约 | TOML 全局配置 + Lua 解锁脚本 + 二进制密钥库，三平台完全一致 |
