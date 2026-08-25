# OmniSteam 系统架构设计文档

> 状态基线：2026-08（对应 main 分支全链路加固重构后）
> 平台支持矩阵：**Windows x64 功能完整** | Linux / macOS 注入链路可用，函数 Hook 层暂缺签名（休眠安全模式）

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

| 能力 | Windows x64 | Linux x86_64/i386 | macOS arm64/x86_64 |
| :--- | :--- | :--- | :--- |
| 注入载体 | `dwmapi.dll` 代理劫持 | `LD_PRELOAD` | `DYLD_INSERT_LIBRARIES` |
| Hook 引擎 (funchook) | ✅ | ✅ | ✅ |
| 函数特征签名库 | ✅（与上游签名库核对一致） | ⚠️ 暂缺（Hook 休眠） | ⚠️ 暂缺（Hook 休眠） |
| 所有权/解密/请求码解锁 | ✅ | ❌ 待签名库 | ❌ 待签名库 |
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
                                   未命中→异步查上游(opensteamtool/
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

### 关键自愈设计

- **请求码缓存**：`cache/manifest_request_codes.bin` 持久化 GID→code；负缓存 10 分钟 TTL
  防止对失效 GID 无限轰炸上游。Steam 约 45s 自动重试一次即可命中缓存。
- **增量许可同步**：Core 记录自己注入过的 ID 集合，重载时只做差量 FastRemove/Grow-append，
  多次热重载不会重复膨胀 Package 0；Steam 自行重建 Package 后也能自愈。
- **RVA 缓存**：`cache/pattern_<SHA256>.cache` 以二进制哈希为键，版本不变零扫描；
  含合理性校验（RVA 上限 512MB），损坏缓存自动丢弃。

---

## 3. Core 子系统明细

| 模块 | 职责 | 关键实现点 |
| :--- | :--- | :--- |
| `Hooks_Package` | 所有权与许可证 | CheckAppOwnership / GetPackageInfo / CUtlMemoryGrow / MarkLicenseAsChanged；原子化并发防护；反作弊白名单优先短路 |
| `Hooks_NetPacket` | CM 协议拦截 | 手写 protobuf varint 解析（含越界防护）；追加字段式报文改写；32 槽互斥包池；GetManifestRequestCode 缓存优先注入；成就/统计双协议伪造（151/147 + 818/819） |
| `PicsTokenInjector` | PICS 令牌 | eMsg 8903 请求中为 addtoken 配置的 App 追加/覆盖 access_token，其余字段逐字节保留 |
| `Hooks_Decryption` | Depot 密钥 | 大小写不敏感零分配快速拒绝，仅在命中 decryptionkey 时分配 |
| `Hooks_Manifest` | 固定 Manifest 预取 | setManifestid 固定的 GID 启动时预取至 `depotcache/` |
| `Hooks_SteamUI` | 库可见性 | steamui.dll FillInAppOverview 合成购买时间戳 |
| `Hooks_Misc` | OnlineFix 与 DLL 注入 | SpawnProcess / OptedInMask（原子化真实 AppID）；addinject 配置的模块按进程名轮询注入（基线快照 + PID 认领去重 + WOW64 拒绝） |
| `Hooks_IPC` | 占位透传 | 当前无实际逻辑（预留扩展点） |
| `PatternLoader` | 函数寻址 | 五级解析：SHA256 键控 RVA 缓存 → **运行时符号表**（Linux/macOS 零维护路径）→ 本地签名 TOML → **远程签名库拉取**（内置特征码失配时触发，按 `windows-x64`/`linux-x64`/`linux-i386`/`macos-universal` 规范目录寻址）→ 内置特征码扫描；含 RVA 合理性校验与歧义跳过；结构布局由 `offsetof` 静态断言自证 |
| `SymbolTable` | 符号枚举 | ELF 节区解析（32/64 位，dynsym+symtab）/ Mach-O LC_SYMTAB 内存遍历 / PE 导出表；`__cxa_demangle` 反修饰 |
| `LuaConfig` | 解锁规则存储 | **双缓冲快照**：读端永远看到完整数据，重载在后台槽构建后指针切换；Lua 沙箱移除 io/package.loadlib/os.* 危险面 |
| `DlcStore` | DLC 元数据 | Steam API 异步发现 + 本地二进制缓存（锁外写盘、原子替换） |
| `AntiCheatGuard` | 安全白名单 | CS2/Dota2/T F2/Apex 等 30+ 竞技游戏透明直通 |

## 4. Manager 子系统明细

| 模块 | 职责 |
| :--- | :--- |
| `WebServer` | 回环 (127.0.0.1) HTTP 服务：完整请求读取(Content-Length)、15s 收包超时、每连接独立线程(上限16)、可靠 shutdown |
| `ApiRouter` | REST 路由；Host/Origin 回环校验（DNS 重绑定 / CSRF 防护）；可选 `[webui] token` 共享密钥门（`X-Omni-Token` 头，未配置不启用）；所有数值解析防溢出崩溃；密码永不回显（`passwordSet` + `__OMNI_KEEP__` 保持机制）；脚本路径操作经受管目录校验 |
| `StaticAssets` | 单文件嵌入式仪表盘（搜索/一键解锁/脚本管理/云存档/Doctor） |
| `ScriptManager` | Lua 生成（字符串转义防注入）、appmanifest 预创建、启用/停用/删除（路径白名单双重校验） |
| `DepotKeyStore` | 23万+ 密钥：多路径加载 → CDN 四镜像后台更新 → config.vdf 补录；32 位安全算术 + 数量上限 + 强制排序保证二分正确 |
| `CoreInstaller` | 本地产物部署或 Release/Nightly 下载解压（校验解压退出码）；心跳状态机（PID 存活检测清理陈旧状态） |
| `CloudSaveManager` + `SavePathResolver` + `WebDavClient` | 存档探测（userdata/compatdata）→ 时间戳版本化 WebDAV 备份/还原 |
| `Doctor` | 系统自检报告（Steam 进程/安装状态/密钥库/网络） |
| `ConfigManager` | omnisteam.toml 读写（值转义 + 临时文件原子替换） |

## 5. 已知断点与预留扩展点

| # | 断点 | 说明 | 现状 |
| :- | :--- | :--- | :--- |
| A | ~~`addtoken` 访问令牌~~ | 已由 `PicsTokenInjector` 实现（eMsg 8903 追加字段注入） | ✅ 2026-08 完成 |
| B | ~~`addinject` DLL 注入~~ | SpawnProcess 后按进程名轮询自动注入（含跨位数防护） | ✅ 2026-08 完成 |
| C | ~~Stats 成就统计上报~~ | `StatsClient` 供体 SteamID 解析 + eMsg 151/147 与 818/819 双协议伪造已落地 | ✅ 2026-08 完成 |
| D | Linux/macOS 签名库 | **2026-08-25 实测 + 自环打通**：三平台内部符号全部 strip（仅公开 `Steam_*` C API）；Windows 内置特征码在部分客户端版本上大面积失效。**vtable 槽位迁移派生已实现并验证**（`tools/derive_signatures.py`）：以任一已锚定版本为参照，经 RTTI（MSVC image-relative COL / Itanium type_info）定位同类 vtable，槽位序号跨版本不变即可迁移函数地址。自环测试 7/7 与上游锚定值精确一致；跨版本迁移经内置特征码独立命中交叉验证（RecvPkt 两法同址）。流水线已接入：Windows 作业快照安装器版参照二进制 + 上游锚点匹配时自动派生回推 | Windows 已闭环；Linux/macOS 待参照数据源 |
| E | ~~仪表盘鉴权~~ | Host 回环校验（防 DNS 重绑定）+ POST Origin 校验（防 CSRF）已落地；可选 `[webui] token` 共享密钥门（前端 401 自动提示并携带） | ✅ 2026-08 完成 |
| F | ~~Denuvo EncryptedAppTicket 消费~~ | `setAppTicket` 写入的票据现由 eMsg 5527 响应伪造消费（eresult=OK + 子消息注入）；AppOwnershipTicket(858) 伪造仍属增强池 | ✅ 2026-08 部分完成 |

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
├─ tests/                    六个 CTest 套件（见 TESTING.md）
├─ tools/                    check_code.py 质检、depot_key_tool.py / process_issue_key.py 密钥工具、
│                            harvest_signatures.py 三平台签名采集（按架构分桶）、
│                            sync_upstream_patterns.py 上游锚点同步（锚点→派生→验证）
└─ source/                   上游参考源码（仅本地参考，不入库/不参与构建）
```
