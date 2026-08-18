# OmniSteam 全阶段开发路线图与里程碑 (Roadmap)

## 阶段规划总览

```
[阶段 1: 跨平台基建] ➔ [阶段 2: 特征码与IPC核心] ➔ [阶段 3: Manager 面板开发] ➔ [阶段 4: 云存档与WebDAV] ➔ [阶段 5: 发布与多平台体验优化]
      (已完成)               (当前就绪)               (规划中)                (规划中)                 (规划中)
```

---

## 阶段 1：跨平台抽象基建 (OmniPlatform) 【已完成】
- [x] 统一 OS 抽象接口 `OmniPlatform.h`（覆盖 Detour, DynamicLibrary, Memory, BinaryParser, ByteSearch, DirectoryWatch, Http, Hash, Process, CredentialStore）。
- [x] 基于 `funchook`（Capstone 5.0）的多平台 Hook 底层封装。
- [x] Linux（ELF / inotify / libcurl / OpenSSL / XDG 规范）底层实现。
- [x] macOS（Mach-O / kqueue / libcurl / OpenSSL / AppSupport）底层实现。
- [x] Windows（PE / WinHttp / Registry）底层实现。
- [x] 嵌入式 Lua 5.4 解释器与全局 API 注册（`addappid`, `setAppTicket`, `setManifestid` 等）。
- [x] 跨平台 CI 流水线（Ubuntu x86_64/i386, Windows x64, macOS arm64）与 CTest 自动化测试套件。

---

## 阶段 2：全平台特征码扫描与 IPC 拦截核心 【下一阶段目标】
- [ ] **全平台 Steam 二进制特征提取**：
  - Windows: `steamclient64.dll`、`steamui.dll` 签名库。
  - Linux: `steamclient.so` (32位与64位) 签名库。
  - macOS: `steamclient.dylib` 签名库。
- [ ] **IPC 协议与 Protobuf 消息编解码**：
  - 集成 `steam_messages.proto` 生成代码。
  - 拦截并模拟 `ISteamUser::GetAppOwnershipTicket`、`ISteamUtils` 消息。
- [ ] **Depot 解密与 Manifest 自动调度**：
  - `ConfigStore::GetBinary` 拦截验证与动态 Key 注入。
  - 接入公共 / 自建 Manifest API 自动拉取 Manifest GID。

---

## 阶段 3：OmniSteam Manager 面板开发 (Tauri / Rust / WebUI)
- [ ] **项目脚手架与基础框架**：
  - 在 `apps/manager/` 初始化 Tauri 2.0 跨平台项目。
  - 适配浅色/深色主题与响应式布局。
- [ ] **Steam 游戏与 DLC 检索**：
  - 对接 Steam Store WebAPI，支持搜索游戏名实时获取 AppID、封面、DLC 列表。
  - 游戏勾选一键生成并写入目标 `lua/*.lua` 脚本。
- [ ] **配置与状态管理**：
  - 可视化编辑 `omnisteam.toml`。
  - 监控当前 Steam 运行状态与 OmniSteam 注入状态。

---

## 阶段 4：WebDAV / S3 云存档重定向与同步系统
- [ ] **本地存档路径智能解析**：
  - Windows 原生路径、Linux Proton / WINE compatdata 前缀路径智能识别。
- [ ] **WebDAV / S3 客户端集成**：
  - 支持坚果云、Nextcloud、自建 WebDAV、阿里云盘 WebDAV、S3 等存储端。
  - 增量对比、哈希校验、带时间戳的存档历史版本备份与一键回退。

---

## 阶段 5：SteamOS / Steam Deck 深度整合与公开发布
- [ ] SteamOS (Decky Loader) 插件适配与无缝集成。
- [ ] 提供全平台一键安装包（Windows 安装器 / Linux AppImage & deb / macOS DMG）。
- [ ] 端到端集成测试与安全审计。
