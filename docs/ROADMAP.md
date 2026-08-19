# OmniSteam 全阶段开发路线图与里程碑 (Roadmap)

## 阶段规划总览

```
[阶段 1: 跨平台基建] ➔ [阶段 2: 特征码与IPC核心] ➔ [阶段 3: Manager 面板开发] ➔ [阶段 4: 云存档与WebDAV] ➔ [阶段 5: 发布与多平台体验优化]
      (已完成)               (已完成)                 (已完成)                (已完成)                 (已完成)
```

---

## 阶段 1：跨平台抽象基建 (OmniPlatform) 【已完成】
- [x] 统一 OS 抽象接口 `OmniPlatform.h`（覆盖 Detour, DynamicLibrary, Memory, BinaryParser, ByteSearch, DirectoryWatch, Http, Hash, Process, CredentialStore）。
- [x] 基于 `funchook`（Capstone 5.0）的多平台 Hook 底层封装。
- [x] Linux（ELF / inotify / libcurl / OpenSSL / XDG 规范）底层实现。
- [x] macOS（Mach-O / kqueue / libcurl / OpenSSL / AppSupport）底层实现。
- [x] Windows（PE / WinHttp / Registry）底层实现。
- [x] 嵌入式 Lua 5.4 解释器与全局 API 注册（`addappid`, `setAppTicket`, `setManifestid` 等）。
- [x] 跨平台 CI 流水线（Ubuntu x86_64/i386, Windows x64, macOS arm64）与 CTest 自动化测试套件 (`test_platform.cpp`)。

---

## 阶段 2：全平台特征码扫描与 IPC 拦截核心 【已完成】
- [x] **全平台 Steam 二进制特征提取与动态加载**：
  - Windows: `patterns/windows_x64.toml`。
  - Linux: `patterns/linux_x64.toml`。
  - macOS: `patterns/macos_x64.toml`。
  - 实现 `PatternLoader` 动态解析器与跨平台回退签名。
- [x] **IPC 协议与二进制消息编解码**：
  - 实现无依赖的 `BufferReader` 与 `BufferWriter` 结构 (`SteamIPC.h`)。
  - 拦截并调度 `IPCProcessMessage` 通信。
- [x] **Depot 解密与 Manifest 自动调度**：
  - `Hooks_Decryption.cpp`：`ConfigStore::GetBinary` 动态拦截与密钥回填。
  - `ManifestClient.cpp`：接入多上游（opensteamtool / steamrun / wudrm / 自定义）Manifest GID 自动化拉取。
- [x] 模块化测试套件：`tests/test_ipc_metadata.cpp`（特征匹配、IPC 序列化、Manifest 解析）。
---

## 阶段 3：OmniSteam Manager 面板开发 【已完成】
- [x] **项目脚手架与基础框架**：
  - 在 `apps/manager/` 构建独立无侵入管理进程 (`omnisteam-manager`)。
- [x] **Steam 游戏与 DLC 检索**：
  - 对接 Steam Store WebAPI (`SteamApi.cpp`)，实现游戏关键词实时检索与 DLC 树结构抓取。
- [x] **Lua 解锁脚本生命周期管理**：
  - 实现 `ScriptManager.cpp`，支持一键生成规范 `.lua` 脚本、目录扫描、重命名启用/停用与删除。
- [x] **嵌入式 Web 仪表盘与 REST 服务**：
  - 实现轻量高并发 WebServer（提供 `/api/search`、`/api/scripts`、`/api/unlock`、`/api/toggle`）。
- [x] 模块化测试套件：`tests/test_script_manager.cpp`（脚本生成、启用/停用生命周期测试）。
---

## 阶段 4：WebDAV 云存档重定向与同步系统 【已完成】
- [x] **本地存档路径智能解析 (`SavePathResolver`)**：
  - 支持 Steam 官方云存档路径：`<Steam>/userdata/<account_id>/<appid>`。
  - 支持 Linux / SteamOS Proton WINE compatdata 前缀路径智能穿透抓取。
- [x] **WebDAV 跨平台客户端 (`WebDavClient`)**：
  - 支持 `MKCOL`、`PUT`、`GET`、`DELETE` 与 Basic / Digest 认证。
- [x] **云存档多版本增量备份管理器 (`CloudSaveManager`)**：
  - 时间戳版本化（`YYYYMMDD_HHMMSS`）备份隔离与独立线程同步。
- [x] 模块化测试套件：`tests/test_cloud_save.cpp`（存档探测与 WebDAV 结构校验）。
---

## 阶段 5：SteamOS / Steam Deck 深度整合与发布打包 【已完成】
- [x] **SteamOS 游戏模式 Decky Loader 插件开发** (`plugins/decky-omnisteam/`)：
  - 提供 React/TypeScript 前端界面，可在 Steam Deck 快捷菜单 (QAM) 中直观查看与管理解锁状态。
- [x] **免 root 一键安装与卸载脚本** (`scripts/install-steamos.sh` & `scripts/uninstall-steamos.sh`)：
  - 采用 `systemd user environment.d` 挂载，完全不触碰 SteamOS 系统只读分区。
- [x] **CPack 多格式全平台自动打包配置**：
  - 支持生成 `.tar.gz`、`.zip`、`.deb` 等格式分发包。
- [x] 模块化测试套件：`tests/test_packaging_integration.cpp`（打包配置与 Decky 规范校验）。
