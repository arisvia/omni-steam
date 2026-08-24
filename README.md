# OmniSteam

> **One Codebase. All Platforms. Universal Steam Enhancement & Unlocker.**

OmniSteam 是一个现代、开源、全平台兼容的 Steam 客户端增强工具。当前 **Windows x64 功能完整**，
Linux (含 Steam Deck / SteamOS) 与 macOS 具备完整注入链路与 Manager 管理能力，核心函数 Hook 层暂缺
签名库（主动休眠以保证安全，详见 [架构文档 · 平台能力矩阵](docs/ARCHITECTURE.md#1-架构总览)）。

采用 **"隐身注入核心 (Headless Core) + 独立管理面板 (Decoupled Manager) + 掌机原生插件 (Decky Plugin)"**
的解耦架构——两者之间仅通过文件系统通信（Lua 脚本目录 + 密钥库 + 心跳文件），无任何 Socket 依赖。

---

## 核心特性

- **隐身注入**：Windows `dwmapi.dll` 代理劫持（透明转发系统 DWM 全部导出）；Linux `LD_PRELOAD`；macOS `DYLD_INSERT_LIBRARIES`。
- **自适应特征寻址**：基于 Steam 二进制 SHA256 键控 RVA 缓存（`cache/pattern_<sha256>.cache`），
  版本不变零扫描直通；升级后自动重扫。Windows 特征码已与上游签名库逐一核对。
- **全协议解锁链路**：
  - `CheckAppOwnership` 所有权伪装（bOwnsLicense=true / bFreeLicense=false / PackageId=0）；
  - Package 0 经 Valve 原生 `CUtlMemoryGrow` **增量**注入 AppID/DepotID（热重载零膨胀、可自愈）；
  - `ConfigStore_GetBinary` 回填 Depot AES-256 解密密钥；
  - `GetManifestRequestCode` 拦截 + **持久化正缓存 / 10 分钟负缓存**，eresult=OK 追加字段式注入（保留原始报文全部字段）；
  - Legacy CD-Key（eMsg 730 → 本地合成 785）；`-onlinefix` AppID 480 伪装与控制器/覆盖层还原；
  - steamui `FillInAppOverview` 合成购买时间戳保证库内即时可见与永久留存。
- **反作弊安全白名单 (`AntiCheatGuard`)**：CS2 / Dota 2 / TF2 / Apex 等 30+ 竞技游戏自动识别并透明直通原生逻辑。
- **内核级热重载**：双缓冲快照机制——读端永远看到完整配置，Lua 目录递归监视秒级生效且所有权判定无瞬断窗口。
- **Manager CLI + Web 仪表盘 (`omnisteam`)**：
  - Steam Store 实时检索（AppID / DLC 树）、一键生成解锁脚本并自动匹配 `depotkeys.bin`（17.5 万+ 条，CDN 四镜像自动同步）；
  - 嵌入式 Web 面板（回环绑定、并发处理、请求完整性校验），支持 Steam Deck 掌机操作；
  - Doctor 系统自检、Core 安装器（本地产物 / GitHub Release 自动部署）。
- **WebDAV 云存档**：跨平台存档智能探测（userdata / Proton compatdata）、时间戳多版本增量备份与还原。
- **Steam Deck 原生插件 (`plugins/decky-omnisteam`)**：接入 Decky Loader 快捷菜单。

---

## 编译与测试

### 依赖项
- C++20 兼容编译器 (MSVC 2019+ / GCC 11+ / Clang 13+)
- CMake 3.20+
- Linux/macOS 额外需要 OpenSSL 与 libcurl（依赖经 FetchContent 自动拉取）

### 编译
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 运行自动化测试
```bash
ctest --test-dir build --output-on-failure -C Release
```
测试套件覆盖：`PlatformTests`、`IpcMetadataTests`、`ScriptManagerTests`、`CloudSaveTests`、
`PackagingIntegrationTests`、`DepotKeyTests`（详见 [docs/TESTING.md](docs/TESTING.md)）。

提交前请运行质检工具（include 完整性 + 括号配平 + clang-format 自动修复）：
```bash
python tools/check_code.py --fix
```

---

## 使用指南

### 1. 安装并运行核心注入 (Core)

- **Windows**：在 Manager 中点击"安装核心"，或手动将 `libomnisteam.dll` 与 `dwmapi.dll` 复制到 Steam 安装根目录，正常启动 Steam 即可。
- **Linux / 桌面 Linux**：
  ```bash
  ./scripts/omnisteam.sh
  ```
- **macOS**：
  ```bash
  DYLD_INSERT_LIBRARIES=./build/lib/libomnisteam.dylib /Applications/Steam.app/Contents/MacOS/steam_osx
  ```

### 2. Steam Deck / SteamOS 一键安装

免 root、不触碰系统只读分区的脚本：

```bash
chmod +x scripts/install-steamos.sh
./scripts/install-steamos.sh     # 卸载: ./scripts/uninstall-steamos.sh
```

安装完成后重启 Steam 生效；若已装 [Decky Loader](https://github.com/SteamDeckHomebrew/decky-loader)，
插件自动注册到 `···` 快捷菜单。

### 3. 运行管理面板 (Manager)

```bash
omnisteam [端口, 默认8080]      # Web 仪表盘 http://127.0.0.1:8080
omnisteam doctor                # 系统自检
omnisteam search <关键词>       # 商店检索
omnisteam unlock <AppID>        # 一键解锁（自动匹配 Depot 密钥）
omnisteam list / toggle / remove
omnisteam import-ticket <file>  # Denuvo 票据导入
omnisteam backup [AppID]        # 云存档备份 / restore <AppID> 还原
```

配置文件为 TOML 格式（参考 `omnisteam.example.toml`）：日志级别、Manifest 上游选择
（opensteamtool / wudrm / steamrun / 自定义 URL）、DLC 自动发现开关、Lua 目录、WebDAV 凭据。

---

## 文档索引

| 文档 | 内容 |
| :--- | :--- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 系统架构、全链路数据流、模块明细、已知断点 |
| [docs/INJECTION_STRATEGY.md](docs/INJECTION_STRATEGY.md) | 各平台注入方案对比与实施状态 |
| [docs/ROADMAP.md](docs/ROADMAP.md) | 开发路线图与当前进度 |
| [docs/TESTING.md](docs/TESTING.md) | 测试体系与模块映射 |

---

## 致谢与引用 (Credits & Acknowledgements)

- **OpenSteamTool** — 核心思想与 Steam 协议拦截方案的重要灵感来源。
- **GreenLuma** — 经典的 Steam 客户端解锁器先驱项目。
- **[kubo/funchook](https://github.com/kubo/funchook)** — 跨平台轻量级 Hook 框架（基于 Capstone）。
- **[marzer/tomlplusplus](https://github.com/marzer/tomlplusplus)** — 高性能 C++20 TOML 解析器。
- **[gabime/spdlog](https://github.com/gabime/spdlog)** — 极速现代 C++ 日志库。
- **[SteamDeckHomebrew/decky-loader](https://github.com/SteamDeckHomebrew/decky-loader)** — SteamOS 插件加载环境。

---

## 许可证 (License)

本项目基于 [GPL-3.0 License](LICENSE) 开源发布。

> 本项目仅供学习研究，请支持正版游戏。
