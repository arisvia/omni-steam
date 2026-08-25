# OmniSteam 开发路线图 (Roadmap)

> 状态基线：2026-08。阶段 1–5 主体功能已完成；阶段 6（全链路加固）已完成；
> 阶段 7 为当前已知的断点补全计划（详见 [ARCHITECTURE.md §5](ARCHITECTURE.md#5-已知断点与预留扩展点)）。

## 阶段总览

```
[1 跨平台基建] → [2 特征码与IPC核心] → [3 Manager面板] → [4 云存档WebDAV] → [5 发布打包] → [6 全链路加固] → [7 断点补全]
    ✅               ✅                   ✅                ✅                ✅             ✅             🚧 进行中
```

---

## 阶段 1：跨平台抽象基建 (OmniPlatform) 【✅ 已完成】
- [x] 统一 OS 抽象 `OmniPlatform.h`（Detour / DynamicLibrary / ByteSearch / DirectoryWatch / Http / Hash / Process / CredentialStore 等）。
- [x] funchook (Capstone 5.0.1) 多平台 Hook 封装，事务化 attach/commit。
- [x] Windows (PE/WinHttp/Registry)、Linux (ELF/inotify/libcurl)、macOS (Mach-O/kqueue) 实现。
- [x] 嵌入式 Lua 5.4 与全局 API 注册（addappid / addtoken / setManifestid / setAppTicket / setETicket / addinject）。

## 阶段 2：特征码扫描与 IPC 拦截核心 【✅ 已完成】
- [x] SHA256 键控 RVA 缓存 (`cache/pattern_<sha256>.cache`)：版本不变零扫描；含 RVA 合理性校验。
- [x] 手写 protobuf varint 编解码（无依赖、含越界防护），eMsg 151/147 服务方法拦截。
- [x] `Hooks_Decryption`：ConfigStore_GetBinary 密钥回填（热路径零分配快速拒绝）。
- [x] `ManifestClient`：多上游请求码解析（opensteamtool / wudrm / steamrun / 自定义）+ 持久化正缓存 + 负缓存 TTL。

## 阶段 3：Manager 面板 【✅ 已完成】
- [x] 三层架构 WebServer / ApiRouter / StaticAssets；Steam Store 检索与 DLC 树抓取。
- [x] ScriptManager：Lua 一键生成、启用/停用/删除、appmanifest 预创建。
- [x] DepotKeyStore：本地多路径加载 + CDN 四镜像后台同步 + config.vdf 补录。

## 阶段 4：WebDAV 云存档 【✅ 已完成】
- [x] SavePathResolver（userdata / Proton compatdata 穿透）、WebDavClient（MKCOL/PUT/GET/DELETE, Basic/Digest）、CloudSaveManager 时间戳版本化备份还原。

## 阶段 5：SteamOS 整合与打包 【✅ 已完成】
- [x] Decky Loader 插件 (`plugins/decky-omnisteam`)、免 root 安装脚本、CPack 打包 (TGZ/ZIP/DEB)。

## 阶段 6：全链路加固与正确性修复 【✅ 已完成 2026-08】

### 正确性
- [x] `CheckAppOwnership` 返回值对齐规范（bFreeLicense=false，消除与 bOwnsLicense 的矛盾状态）。
- [x] Package 0 热重载改为**增量注入/FastRemove**——修复多次重载重复膨胀 Package 0 的缺陷。
- [x] LuaConfig `GetManifestId` 数据竞争修复；整体重构为**双缓冲快照**（消除重载期间所有权瞬断窗口）；多目录一次性重建（修复互相覆盖）。
- [x] 移除 Linux/macOS 相同函数序言伪签名（原实现会将多个 hook 挂到同一随机地址），改为主动休眠 + 明确告警。
- [x] Manifest 上游 URL 修正（wudrm: `gmrc.wudrm.com/manifest`、steamrun: `manifest.steam.run/api/manifest`，与上游实现对齐）。
- [x] Core 初始化等待 steamclient 加载 5s→60s，超时明确报错退出（原为假装成功继续安装）。

### 稳定性与性能
- [x] RecvPkt 不再阻塞网络线程 8s：缓存优先 + 有界等待 2.5s + 异步结果落盘自愈。
- [x] 注入报文改为追加字段式改写（保留原始 header/body 全部字段）。
- [x] 数据包池互斥保护并扩容至 32 槽；protobuf 解析补齐全部边界检查。
- [x] Hooks_Package / Hooks_Misc 共享状态原子化；DlcStore 缓存锁外写盘 + 原子替换。
- [x] DepotKeyStore 32 位构建整数溢出防护、记录数上限、强制排序保证二分查找正确性。
- [x] Manager CLI/API 全部数值解析防溢出崩溃；CoreInstaller 校验解压退出码。

### 安全
- [x] `/api/toggle|delete` 受管目录白名单双重校验（Router 层 + ScriptManager 层），阻断任意路径文件操作。
- [x] WebDAV 密码不再回显（passwordSet + `__OMNI_KEEP__` 保持机制）；TOML 写入转义 + 原子替换。
- [x] Lua 沙箱移除 io 库整体与 package.loadlib；token 不再写入日志。
- [x] 生成的 Lua 脚本字符串全量转义，阻断脚本注入。

## 阶段 7：断点补全与新特性 【🚧 进行中】

按优先级排序（详细背景见 ARCHITECTURE.md §5）：

- [x] **PICS 访问令牌注入**（2026-08）：新增 `PicsTokenInjector` 模块消费 `addtoken` 收集的 access token，
      拦截 eMsg 8903 CMsgClientPICSProductInfoRequest，对已解锁且配置了令牌的 App 条目以追加字段方式
      覆盖 access_token（保留其余字段）；配套 `PicsTokenTests` 单元套件。
- [x] **Stats 成就统计上报**（2026-08）：新增 `StatsClient`（供体 SteamID 解析：Lua
      setStatSteamid > 本地缓存 > stats.opensteamtool.com，含 10 分钟失败退避）+
      eMsg 151/147 Player.GetUserStats 与 eMsg 818/819 CMsgClientGetUserStats 双协议伪造
      （改写请求 SteamID、剥离服务器统计、eresult→OK）；新增可复用 `ProtoFields` 手写 protobuf
      字段操作库与 `StatsProtoTests` 套件；Lua 新增 setStatSteamid API。
- [x] **addinject 接线**（2026-08）：`SpawnProcess` 后按 exe 文件名轮询新进程（基线快照去重、PID 认领防重复注入），
      自动调用 `ProcessInjector::InjectForApp`；平台层新增 `Process::FindProcessIdsByName`
      （Toolhelp32 / /proc / sysctl），注入器增加 WOW64 跨位数拒绝防护。
- [x] **仪表盘基础防护**（2026-08）：ApiRouter 全局 Host 回环校验（阻断 DNS 重绑定）+
      POST/PUT/DELETE Origin 回环校验（阻断浏览器 CSRF，非浏览器客户端不受影响）。
- [x] **下载产物完整性校验**（2026-08）：新增 `DownloadVerifier`——depotkeys.bin 与 Core 安装包下载后
      校验可选发布的 `<url>.sha256` 边车文件，摘要不匹配即拒绝该镜像（缺失/畸形边车宽松放行）。
- [x] **Linux/macOS 运行时符号解析**（2026-08）：新增 `SymbolTable` 跨平台符号表枚举
      （ELF 节区解析含 32/64 位、Mach-O LC_SYMTAB 内存遍历、PE 导出表），PatternLoader 直接从
      目标模块自身符号数据解析 hook 目标；模糊匹配歧义自动跳过防止挂错地址。
      ⚠️ 实采勘误：官方客户端的 steamclient 库已被 strip（首次采集 5 个 TOML 全空证实），
      该路径仅在未 strip 的发行版上生效。
- [x] **Linux/macOS 特征签名采集流水线**（2026-08）：`tools/harvest_signatures.py` 跨格式
      （ELF/Mach-O/PE）符号采集 + `signature-harvest.yml` 三平台工作流（二进制仅入私有 artifact，
      签名 TOML 自动回推仓库，空结果不产出 TOML）；作为离线兜底层。
- [x] **Windows 特征码运行时远程兜底**（2026-08）：PatternLoader 在内置特征码失配时自动从 CDN
      镜像拉取当前二进制哈希对应的签名库（depotkeys.bin 同款分发模型）；`tools/sync_upstream_patterns.py`
      对接上游真实锚点库 `OpenSteam001/steam-monitor@pattern`（锚点→本地官方二进制派生→验证）；
      上游未锚定新版本属预期状态（exit 0），Windows 继续走内置特征码。
- [ ] **Linux/macOS 锚点数据源**（当前阻塞项）：官方客户端已 strip，采集与符号路线均拿不到内部函数
      位置；需通过 VM 逆向提取差异化特征码/RVA 后手写 `signatures/<平台>/<哈希>.toml`
      （放入即生效），或推动上游 steam-monitor 扩展非 Windows 锚点。
- [x] **上游对齐 + 第二轮加固**（2026-08）：结构布局改为 `offsetof` 静态断言自证
      （移除与真实客户端布局相悖的手工 `SteamOffsets` 表）；移除未经验证的 `CUser+0x1E4`
      SteamId32 写入；`LuaConfig` addtoken/setStat 改走 staging 槽（热重载不再丢 token）；
      `SteamIPC.BufferReader` 溢出安全 + 截断可检测；ManifestClient 磁盘缓存全程持锁；
      hook 挂载真实结果登记（core_status.json 不再假绿）；注册表票据动态尺寸读取；
      WebUI 全量 HTML 转义 + 修复 I18N `en:` 块缺失导致的整页脚本失效；WebServer POSIX
      `shutdown()` 唤醒 accept。
- [x] **特征码采集按架构分桶**（2026-08）：`harvest_signatures.py` 按 ELF 类/机器码输出
      `linux-x64` / `linux-i386` 规范目录（此前 32 位 steamclient.so 的签名被错误归入
      linux-x64，i386 端 PatternLoader 永远 404）；collect 步骤直接透传规范目录。
