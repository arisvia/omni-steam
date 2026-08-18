# OmniSteam 各平台注入方案评估与选型报告

## 1. 核心诉求与评判标准
- **隐蔽性 (Stealth)**：不触发 Steam 客户端完整性自检、不破坏系统只读保护、不被常见安全策略拦截。
- **稳定性 (Stability)**：在 Steam 客户端自我更新 (Self-Update) 后不失效、不发生崩溃或死锁、支持多进程子调用继承。
- **易用性 (UX)**：用户部署操作步骤少、支持免 root / 免管理员提权。

---

## 2. Windows 平台注入方案对比

| 方案 | 隐蔽性 | 稳定性 | 易用性 | 综合评价 |
| :--- | :--- | :--- | :--- | :--- |
| **DLL Proxying / Hijacking (`dwmapi.dll` / `xinput1_4.dll`)** | **极高** (OS 原生加载机制) | **极高** (Steam 启动即加载) | **高** (直接放置于 Steam 根目录) | **最优解 (Recommended)** |
| **外部注入 (CreateRemoteThread / QueueUserAPC)** | **低** (易被杀软拦截内存写入) | **中** (依赖进程监听与注入时机) | **低** (需常驻独立 Injector 后台进程) | 不推荐 |
| **AppInit_DLLs / 注册表全局劫持** | **低** (需要修改系统注册表，受安全软件阻拦) | **低** (影响全系统所有进程) | **极低** | 不推荐 |
| **Windows 快捷方式包装 (Launcher Wrapper)** | **中** | **高** | **中** (桌面快捷方式被 Steam 自更新覆盖时失效) | 备选方案 |

> **Windows 结论**：`dwmapi.dll` / `xinput1_4.dll` 代理劫持是最佳方案。
> - Steam 客户端启动时必定优先在当前目录查找 `dwmapi.dll` / `xinput1_4.dll`。
> - 只要代理 DLL 完整导出了系统原 DLL 的所有函数并将调用转发给 `C:\Windows\System32\dwmapi.dll`，Steam UI 渲染和输入完全不受影响，且不会引起杀毒软件异常告警。

---

## 3. Linux / Steam Deck 平台注入方案对比

| 方案 | 隐蔽性 | 稳定性 | 易用性 | 综合评价 |
| :--- | :--- | :--- | :--- | :--- |
| **`LD_PRELOAD` 环境预加载 (Launcher Wrapper)** | **极高** (Linux 原生机制) | **极高** (Steam 及其子进程完整继承) | **极高** (可直接写入快捷方式或 SteamOS 启动项) | **最优解 (Recommended)** |
| **动态库篡改 / 符号覆盖 (`steamclient.so`)** | **极低** (Steam 自检立即触发重下修复) | **极低** (Steam 更新即被冲掉) | **极低** | 坚决避免 |
| **ptrace 进程附加注入** | **低** (受 `yama.ptrace_scope` 权限阻拦) | **中** (需要 root 或 CAP_SYS_PTRACE) | **低** (Steam Deck 只读文件系统受限) | 不推荐 |

> **Linux / SteamOS 结论**：
> - 使用 `LD_PRELOAD=libomnisteam.so steam` 是最稳健的方案。
> - 在 Steam Deck (SteamOS) 游戏模式下，可直接通过 Decky Loader 插件或向 `~/.steam/steam/steam.sh` 传入 preload，完全免 root。

---

## 4. macOS 平台注入方案对比

| 方案 | 隐蔽性 | 稳定性 | 易用性 | 综合评价 |
| :--- | :--- | :--- | :--- | :--- |
| **`DYLD_INSERT_LIBRARIES` 注入** | **极高** (macOS 官方动态加载机制) | **极高** | **高** (通过 `open` 脚本拉起) | **最优解 (Recommended)** |
| **Mach-O 注入与 LC_LOAD_DYLIB 篡改** | **低** (破坏 Codesign 签名，Gatekeeper 会阻止启动) | **低** | **低** (需要重新 ad-hoc 签名) | 不推荐 |
| **task_for_pid / mach_vm 外部注入** | **极低** (受 SIP 系统完整性保护阻拦，必须关闭 SIP) | **低** | **极低** | 坚决避免 |

> **macOS 结论**：通过自定义启动脚本指定 `DYLD_INSERT_LIBRARIES=libomnisteam.dylib` 是在不关闭 SIP、不破坏 App 签名下的唯一高隐蔽、高稳定方案。
