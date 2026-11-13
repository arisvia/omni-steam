# OmniSteam 系统架构设计文档

> 状态基线：2026-11（RFC 001/002 落地、成就兜底与云存档自愈机制后）
> 平台支持矩阵：**Windows x64 功能完整；Linux / macOS 骨架已通但签名覆盖不全（见下方能力矩阵与 §5-断点 D）**

---

## 1. 架构总览

OmniSteam 采用 **"隐身注入核心 (Headless Core) + 独立管理面板 (Decoupled Manager)"** 的解耦架构：

- **Core (`libomnisteam.dll/.so/.dylib`)**：零 UI 的进程内探针。等待 Steam 核心模块加载后安装 detour hook，
  提供所有权伪装、Depot 密钥回填、Manifest 请求码注入等能力。不监听任何端口。
- **Manager (`omnisteam.exe` CLI/Web)**：独立生命周期管理进程。三层架构
  （`StaticAssets` → `ApiRouter` → `WebServer`），负责游戏检索、Lua 脚本生成、密钥库管理与 WebDAV 云存档。
- **通信契约**：两者之间**只有文件系统**——Lua 脚本目录（内核级目录监视热重载）、
  `depotkeys.bin` 二进制密钥库、`core_status.json` 心跳文件。无 Socket、无进程内依赖。

### 平台能力矩阵

| 能力 | Windows x64 | Linux x86_64 | macOS x86_64/arm64 |
| :--- | :--- | :--- | :--- |
| 注入载体 | `xinput1_4.dll` 代理劫持 | `LD_PRELOAD`（脚本；Manager 未自动化） | `DYLD_INSERT_LIBRARIES`（手动） |
| Hook 引擎 (funchook) | ✅ | ✅ | ✅（universal 构建；arm64 下 mprotect 行为待真机验证） |
| 函数特征签名库 | ✅ 10 函数（内置特征 + 上游同步 + vtable 移植 + 被调指纹） | ✅ 11 函数（限定名 string-xref + 跨 ABI vtable 锚点移植 + 被调指纹） | ✅ 11 函数（同左，经 Mach-O rebase 解析） |
| 所有权/解密/请求码解锁 | ✅ | ⚠️ 所有权/解密/请求码/成就链路锚点已齐，`GetPackageInfo` 已派生；仍缺 `CUtlMemoryGrow`/`MarkLicenseAsChanged`/`ProcessPendingLicenseUpdates` 与 `steamui` 侧 `FillInAppOverview` | ⚠️ 同左 |
| Manager CLI/Web/云存档 | ✅ | ✅ | ✅ |

---

## 2. 全链路数据流（Unlock 主链路）

```
 [Manager 进程]                                [Steam 进程 + Core Hook]
 ─────────────────                              ──────────────────────────
 ① /api/search ──► Steam Store API
 ② /api/unlock ──► SteamApi.GetAppDetails
        │             DepotKeyStore.FindDepotKeysForApp
        │             (本地 depotkeys.bin, 230k+ 条, CDN 自动同步)
        ▼
 ③ ScriptManager.SaveGameUnlock
        ├── <luaDir>/<appid>.lua          ───┐
        └── steamapps/appmanifest_X.acf   ───┤ 文件契约
                                             │
                                             ▼
                             ④ Core 初始化线程:
                                等 steamclient64.dll (最长60s)
                                → Config/AntiCheatGuard/DlcStore/PatternLoader
                                → LuaConfig.ParseDirectory(双缓冲快照)
                                → DirectoryWatch 注册热重载
                                → HookManager.InstallHooks()
                                             │
            ═════════════ 运行期 Hook 矩阵 ═════════════
                                             │
 ⑤ 库内可见性 ◄────────────────── Hooks_SteamUI.FillInAppOverview
        appmanifest 存在 +        (PurchasedTime=0 时合成时间戳)
        PurchasedTime 合成
                                             │
 ⑥ 所有权判定 ◄────────────────── Hooks_Package.CheckAppOwnership
        bOwnsLicense=true         (反作弊白名单 AppID 直通原生逻辑;
        bFreeLicense=false         未拥有且已解锁 → 改写 AppOwnership)
        PackageId=0                    │
                                       ├─ GetPackageInfo(PackageId=0)
                                       │    └─ CUtlMemoryGrow 增量注入
                                       │       AppIdVec / DepotIdVec
                                       └─ MarkLicenseAsChanged → UI 刷新
                                             │
 ⑦ 内容下载 ◄──────────────────── Hooks_Decryption.ConfigStore_GetBinary
        Depot AES-256 Key          ("depots\<id>\DecryptionKey" 直接回填)
                                             │
 ⑧ Manifest 授权 ◄─────────────── Hooks_NetPacket (eMsg 151/147)
        manifest_request_code      发送侧: 缓存命中→即时映射 jobid;
                                   未命中→异步查上游(omnisteam/opensteamtool/
                                   wudrm/steamrun), 结果写持久缓存
                                   接收侧: 有界等待 2.5s, 追加字段方式
                                   注入 eresult=OK + request_code
                                             │
 ⑨ 特殊场景 ◄──────────────────── Hooks_Misc.SpawnProcess (-onlinefix
        AppID 480 伪装,             → AppID 480 + OptedInMask 还原真身)
        控制器/覆盖层正常              Hooks_NetPacket.LegacyKey
                                   (eMsg 730 拦截, 本地合成 785 应答)
                                             │
 ⑩ 状态回传 ◄──────────────────── core_status.json 心跳 (cache 目录)
 ⑪ 热重载   ─────────────────►  DirectoryWatch 回调 → LuaConfig 双缓冲重建
        (Manager 改 lua 文件)        → Hooks_Package.SyncInjectedLicenses
                                     → Package 0 增量增删(不重复膨胀)
```

### 关键自愈与保护机制

- **请求码持久化缓存**：`cache/manifest_request_codes.bin` 持久化 GID→code；瞬时上游失败不设负缓存 TTL，保证 Steam 下一次重试即可无缝恢复。
- **增量许可同步**：Core 跟踪已注入 ID 集合，热重载执行差量 FastRemove / Grow-append，避免多次热重载导致 Package 0 无序膨胀。
- **二进制哈希 RVA 缓存**：`cache/pattern_<SHA256>.cache` 按二进制哈希键控；合理性上限（512MB）与损坏自动丢弃重搜。
- **非阻塞式进程退出生命周期**：Core 拦截 `SpawnProcess` 派发后台静默备份，游戏退出信号以单次文件落地；Steam 退出时保留挂钩内存由操作系统统一回收，杜绝在 Loader Lock 下逆序卸载引起 CRT fastfail（BEX64）。
---

## 3. Core 子系统明细

| 模块 | 职责 | 关键实现点 |
| :--- | :--- | :--- |
| `Hooks_Package` | 所有权与许可证 | CheckAppOwnership / GetPackageInfo / CUtlMemoryGrow / MarkLicenseAsChanged；原子化并发防护；反作弊白名单优先短路；广播期间完备重入守卫 |
| `Hooks_NetPacket` | CM 协议拦截 | 手写 protobuf varint 解析（复用 ProtoFields，含越界防护）；追加字段式报文改写；32 槽互斥包池；GetManifestRequestCode 缓存优先注入；成就双协议伪造与 Tier 3 离线/宕机平滑保底（合成 k_EResultOK）；RichPresence 状态跟踪与无死锁投递；Denuvo 票据纯协议层拦截（eMsg 5527/858） |
| `Hooks_CallBack` | 回调与事件拦截 | 拦截 `Steam_BGetCallback` / `Steam_FreeLastCallback` C API 边界，线程局部标志隔离与独立 FIFO 异步事件队列分发（如 LicensesUpdated_t），杜绝锁重入死锁 |
| `Hooks_IPC` | IPC 消息拦截与 Denuvo | 拦截 `IPCProcessMessage`，在 CUtlBuffer C API 边界改写 GetSteamID 应答；零注入代理模式与游戏侧解耦 |
| `PicsTokenInjector` | PICS 令牌 | eMsg 8903 请求中为 addtoken 配置的 App 追加/覆盖 access_token，其余字段逐字节保留 |
| `Hooks_Decryption` | Depot 密钥 | 大小写不敏感零分配快速拒绝，仅在命中 decryptionkey 时分配 |
| `Hooks_Manifest` | 固定 Manifest 预取 | setManifestid 固定的 GID 启动时预取至 `depotcache/` |
| `Hooks_SteamUI` | 库可见性 | steamui.dll FillInAppOverview 合成购买时间戳（PurchasedTime）确保库内即时显示与留存 |
| `Hooks_Misc` | OnlineFix 与生命周期监控 | SpawnProcess / OptedInMask（原子化真实 AppID）；addinject 配置的模块按进程名轮询注入；MonitorGameLifecycle 监控游戏退出并静默派发异步备份信号 |
| `PatternLoader` | 函数寻址 | 五级解析：SHA256 键控 RVA 缓存 → 本地签名 TOML → **运行时符号表**（官方客户端内部符号已被 strip，实际仅能命中 `Steam_BGetCallback`/`Steam_FreeLastCallback` 等导出）→ 内置特征码扫描（当前仅 Windows 注册）→ **远程签名库拉取**（按 `windows-x64`/`linux-x64`/`macos-universal` 规范目录寻址）；含 RVA 合理性校验与歧义跳过；模块基址由 `BinaryParser::GetModuleBase` 提供（Windows HMODULE / Linux `dlpi_addr` / macOS dyld 镜像头）；结构布局由 `offsetof` 静态断言自证（锚点源自 Windows 客户端，非 Windows 布局仍需逐平台实测确认） |
| `SymbolTable` | 符号枚举 | ELF 节区解析（32/64 位，dynsym+symtab）/ Mach-O LC_SYMTAB 内存遍历 / PE 导出表；`__cxa_demangle` 反修饰 |
| `LuaConfig` | 解锁规则存储 | **双缓冲快照**：统一 `Has(id)` 谓词；读端永远看到完整数据，重载在后台槽构建后指针切换；Lua 沙箱移除 io/package.loadlib/os.* 危险面 |
| `DlcStore` | DLC 元数据 | Steam API 异步发现 + 本地二进制缓存（锁外写盘、原子替换） |
| `AntiCheatGuard` | 安全白名单 | CS2/Dota2/TF2/Apex 等 30+ 竞技游戏透明直通原生逻辑 |

## 4. Manager 子系统明细

| 模块 | 职责 |
| :--- | :--- |
| `WebServer` | 回环 (127.0.0.1) HTTP 服务：完整请求读取(Content-Length)、15s 收包超时、每连接独立线程(上限16)、可靠 shutdown |
| `ApiRouter` | REST 路由；Host/Origin 回环校验（DNS 重绑定 / CSRF 防护）；可选 `[webui] token` 共享密钥门（`X-Omni-Token` 头，未配置不启用）；所有数值解析防溢出崩溃；密码永不回显（`passwordSet` + `__OMNI_KEEP__` 保持机制）；脚本路径操作经受管目录校验 |
| `StaticAssets` | 单文件嵌入式仪表盘（搜索/一键解锁/脚本管理/云存档/Doctor） |
| `ScriptManager` | Lua 生成（字符串转义防注入）、appmanifest 预创建、启用/停用/删除（路径白名单校验，基于目录 mtime 内存缓存避免磁盘穿透） |
| `DepotKeyStore` | 23万+ 密钥：多路径加载 → CDN 四镜像后台更新 → config.vdf 补录；32 位安全算术 + 数量上限 + 强制排序保证二分正确 |
| `CoreInstaller` | 本地产物部署或 Release/Nightly 下载解压（校验解压退出码）；心跳状态机（PID 存活检测清理陈旧状态） |
| `CloudSaveManager` + `SavePathResolver` + `WebDavClient` | 存档探测（userdata/compatdata）→ 本地版本化快照回退（保留 5 份）+ WebDAV 云端增量同步 |
| `Doctor` | 系统自检报告（Steam 进程/安装状态/密钥库/网络） |
| `DaemonService` | 后台常驻服务（零 Socket 文件事件队列驱动，游戏退出自动触发云存档） |
| `ConfigManager` | omnisteam.toml 读写（值转义 + 临时文件原子替换） |

## 5. 已知断点与预留扩展点

| # | 断点 | 说明 | 现状 |
| :- | :--- | :--- | :--- |
| A | ~~`addtoken` 访问令牌~~ | 已由 `PicsTokenInjector` 实现（eMsg 8903 追加字段注入） | ✅ 2026-08 完成 |
| B | `addinject` DLL 注入 | SpawnProcess 后按进程名轮询自动注入（含跨位数防护）；仅 Windows（CreateRemoteThread），POSIX 显式返回失败 | ⚠️ 仅 Windows |
| C | ~~Stats 成就统计上报~~ | `StatsClient` 供体 SteamID 解析 + eMsg 151/147 与 818/819 双协议伪造已落地 | ✅ 2026-08 完成 |
| D | Linux/macOS 签名库 | **主链路已打通（9 函数）**：`derive_signatures.py` 新增重定位解析层——ELF 走 `R_X86_64_RELATIVE` addend、Mach-O 走 `LC_DYLD_INFO` rebase / chained fixups——使 strip 后的 RTTI/vtable 可遍历；Windows 锚点图按 Itanium 同序槽位跨 ABI 移植，产出 `ConfigStoreGetBinary`/`RecvPkt`/`IPCProcessMessage`/`OptedInMask`/`CloseAppCloud`/`GetAppDataFromAppInfo`；探针改用限定名 VProf 字面量（`CCMInterface::RecvPkt`）后 `BBuildAndAsyncSendFrame` 亦可派生。`tools/validate_signatures.py` 以两套独立机制交叉校验，不一致即 CI 失败。仍缺：Package 0 扩容三件套（`CUtlMemoryGrow`/`GetPackageInfo`/`MarkLicenseAsChanged`/`ProcessPendingLicenseUpdates`）与 `steamui.so/.dylib` 的 `FillInAppOverview`（无 RTTI 类、无唯一字面量，需人工逆向一版参考锚点） | 🔧 进行中 |
| E | ~~仪表盘鉴权~~ | Host 回环校验（防 DNS 重绑定）+ POST Origin 校验（防 CSRF）已落地；可选 `[webui] token` 共享密钥门（前端 401 自动提示并携带） | ✅ 2026-08 完成 |
| F | ~~Denuvo 隐身交付~~ | 纯协议层拦截（`Hooks_NetPacket` eMsg 5527/858）与 `Hooks_IPC` `IClientUser::GetSteamID` 供体动态伪装，杜绝命名管道与跨进程代理检测（详见 [RFC 001](rfc/RFC_001_STEALTH_DENUVO_TICKET_DELIVERY.md)） | ✅ 2026-09 完成 |
| G | ~~安全化 IPC 回调拦截~~ | `Hooks_CallBack` 拦截 C API 边界（`Steam_BGetCallback` / `Steam_FreeLastCallback`），线程局部合成标志隔离 + 异步事件队列（详见 [RFC 002](rfc/RFC_002_SAFE_IPC_CALLBACK_HOOKING.md)） | ✅ 2026-09 完成 |
## 6. 目录导览

```
├─ include/OmniPlatform/     公共头：OmniPlatform.h(平台API)、OmniEndpoints.h(URL集中)、
│                            SteamTypes.h(协议魔数/结构布局/偏移量唯一真相源)
├─ src/Hook/                 七大 Hook 模块 + HookMacros.h(事务化 attach/detach 宏)
├─ src/Platform/{Win,Linux,Darwin,Common}/  平台抽象层实现
├─ src/Utils/                Config(Lua/TOML)、Metadata(DlcStore/ManifestClient/PatternLoader)、
│                            Security(AntiCheatGuard)、Process(ProcessInjector)
├─ apps/manager/src/         Manager 三层架构 + 业务模块（见 §4）
├─ plugins/decky-omnisteam/  Steam Deck Decky Loader 插件
├─ scripts/                  Linux/SteamOS 安装与启动脚本
├─ tests/                    九个 CTest 套件（见 TESTING.md）
├─ tools/                    check_code.py 质检、depot_key_tool.py / process_issue_key.py 密钥工具、
│                            harvest_signatures.py / derive_signatures.py 多平台签名采集与派生工具、
│                            sync_upstream_patterns.py 上游锚点同步（锚点→派生→验证）
└─ source/                   上游参考源码（仅本地参考，不入库/不参与构建）
```
