# OmniSteam

> **One Codebase. All Platforms. Universal Steam Enhancement & Unlocker.**

OmniSteam 是一个现代、开源、全平台兼容的 Steam 客户端增强工具，原生支持 **Windows**、**Linux (含 Steam Deck / SteamOS)** 和 **macOS**。

采用 **“隐身注入核心 (Headless Core) + 独立管理面板 (Decoupled Manager) + 掌机原生插件 (Decky Plugin)”** 的解耦架构，兼顾极致的隐蔽性、丰富的管理功能与掌机操作体验。

---

## 核心特性

- **跨平台原生注入**：
  - Windows: DLL 劫持 (`dwmapi.dll` / `xinput1_4.dll`)
  - Linux / Steam Deck: `LD_PRELOAD` 原生预加载
  - macOS: `DYLD_INSERT_LIBRARIES` 注入
- **通用 Hook 引擎**：基于 `funchook`（Capstone 5.0 反汇编引擎），支持 x86、x86_64 及 ARM64。
- **内核级热重载**：Linux (`inotify`)、Windows (`ReadDirectoryChangesW`)、macOS (`kqueue`) 全平台配置文件与递归子目录秒级实时生效。
- **自适应动态特征码与二进制 RVA 缓存**：启动时基于 Steam 二进制 SHA256 自动探测内部函数并保存至 `cache/`，版本不变时零扫描直通启动，版本更新后自适应重新提取。
- **独立管理面板 CLI (`omnisteam`)**：
  - 🎮 Steam 官方 Store API 实时检索（AppID、游戏名、DLC 树）。
  - 📜 Lua 解锁脚本可视化一键生成与启用/停用管理，自动关联 `depotkeys.bin` 二进制密钥库。
  - 🌐 嵌入式 Web 仪表盘（支持 PC 与手机局域网远程管理，适配 Steam Deck 掌机）。
  - ☁️ 跨平台游戏存档智能探测与 WebDAV 增量多版本云备份。
- **Steam Deck 原生插件 (`plugins/decky-omnisteam`)**：
  - 完美接入 Decky Loader，在 Steam Deck 游戏模式快捷菜单 (QAM) 中一键查看与管理解锁状态。

---

## 编译与测试

### 依赖项
- C++20 兼容编译器 (GCC 11+, Clang 13+, MSVC 2019+)
- CMake 3.20+
- Lua 5.4, OpenSSL, libcurl

### 编译
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### 运行自动化测试
```bash
ctest --output-on-failure -C Release
```
测试套件覆盖：
- `PlatformTests` (Phase 1 跨平台底层与凭据存储)
- `Phase2Tests` (Phase 2 特征库、IPC 编解码、Manifest 调度)
- `Phase3Tests` (Phase 3 脚本生成与生命周期管理)
- `Phase4Tests` (Phase 4 存档路径探测与 WebDAV 配置)
- `Phase5Tests` (Phase 5 生产发布规范与 Decky 结构校验)
- `DepotKeyTests` (DepotKeyStore 索引与远程回退)

---

## 使用指南

### 1. 运行核心注入 (Core)

- **Windows**:
  复制生成的 `libomnisteam.dll` 和 `dwmapi.dll` 至 Steam 安装根目录，正常启动 Steam 即可。
- **Linux / 桌面 Linux**:
  ```bash
  ./scripts/omnisteam.sh
  ```
- **macOS**:
  ```bash
  DYLD_INSERT_LIBRARIES=./build/lib/libomnisteam.dylib /Applications/Steam.app/Contents/MacOS/steam_osx
  ```

### 2. Steam Deck / SteamOS 一键安装与 Decky 插件

我们为 Steam Deck 提供了**完全免 root / 不触碰系统只读保护**的脚本：

- **一键安装**：
  ```bash
  chmod +x scripts/install-steamos.sh
  ./scripts/install-steamos.sh
  ```
  安装完成后重启 Steam 即可生效。若系统已安装 [Decky Loader](https://github.com/SteamDeckHomebrew/decky-loader)，插件会自动注册到右侧 `···` 快捷访问菜单中。
- **一键卸载**：
  ```bash
  ./scripts/uninstall-steamos.sh
  ```

### 3. 运行管理面板 (Manager CLI)

```bash
omnisteam [可选端口，默认8080]
```
- 搜索游戏并一键生成解锁脚本（自动匹配内置与云端 `depotkeys.bin` 二进制密钥库）。
- 配置 WebDAV 云存档备份。

---

## 致谢与引用 (Credits & Acknowledgements)

OmniSteam 的诞生得益于以下优秀的开源项目与社区先驱：

- **[OpenSteam001/OpenSteamTool](https://github.com/OpenSteam001/OpenSteamTool)** - 本项目核心思想与 Steam 协议拦截方案的重要灵感来源。
- **[GreenLuma](https://github.com/)** - 经典的 Steam 客户端解锁器先驱项目。
- **[kubo/funchook](https://github.com/kubo/funchook)** - 强大的跨平台轻量级 Hook 框架（基于 Capstone）。
- **[marzer/tomlplusplus](https://github.com/marzer/tomlplusplus)** - 优雅高性能的 C++20 TOML 解析器。
- **[gabime/spdlog](https://github.com/gabime/spdlog)** - 极速现代 C++ 日志库。
- **[SteamDeckHomebrew/decky-loader](https://github.com/SteamDeckHomebrew/decky-loader)** - 优秀的 SteamOS 插件加载环境。

---

## 许可证 (License)

本项目基于 [GPL-3.0 License](LICENSE) 开源发布。
