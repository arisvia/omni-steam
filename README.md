# OmniSteam

> **One Codebase. All Platforms. Universal Steam Enhancement & Unlocker.**

OmniSteam 是一个现代、开源、全平台兼容的 Steam 客户端增强工具，原生支持 **Windows**、**Linux (含 Steam Deck / SteamOS)** 和 **macOS**。

采用 **“隐身注入核心 (Headless Core) + 独立管理面板 (Decoupled Manager)”** 的解耦架构，兼顾极致的隐蔽性与丰富的管理功能。

---

## 核心特性

- **跨平台原生注入**：
  - Windows: DLL 劫持 (`dwmapi.dll` / `xinput1_4.dll`)
  - Linux / Steam Deck: `LD_PRELOAD` 原生预加载
  - macOS: `DYLD_INSERT_LIBRARIES` 注入
- **通用 Hook 引擎**：基于 `funchook`（Capstone 5.0 反汇编引擎），支持 x86、x86_64 及 ARM64。
- **内核级热重载**：Linux (`inotify`)、Windows (`ReadDirectoryChangesW`)、macOS (`kqueue`) 全平台配置文件秒级实时生效。
- **动态特征签名库 (`patterns/`)**：运行时动态解析 TOML 格式的平台签名，不硬编码二进制偏移。
- **独立管理面板 (`omnisteam-manager`)**：
  - 🎮 Steam 官方 Store API 实时检索（AppID、游戏名、DLC 树）。
  - 📜 Lua 解锁脚本可视化一键生成与启用/停用管理。
  - 🌐 嵌入式 Web 仪表盘（支持 PC 与手机局域网远程管理，适配 Steam Deck 掌机）。
  - ☁️ 跨平台游戏存档智能探测与 WebDAV 增量云备份。

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

---

## 使用指南

### 1. 运行核心注入 (Core)

- **Linux / Steam Deck**:
  ```bash
  ./scripts/omnisteam.sh
  ```
- **macOS**:
  ```bash
  DYLD_INSERT_LIBRARIES=./build/lib/libomnisteam.dylib /Applications/Steam.app/Contents/MacOS/steam_osx
  ```
- **Windows**:
  复制生成的 `libomnisteam.dll` 和 `dwmapi.dll` 至 Steam 安装根目录启动 Steam 即可。

### 2. 运行管理面板 (Manager)

```bash
./build/bin/omnisteam-manager [可选端口，默认8080]
```
浏览器打开 `http://127.0.0.1:8080` 即可通过 Web 界面搜索 Steam 游戏并一键生成解锁脚本。

### 3. 手动 Lua 配置示例 (`~/.config/omnisteam/lua/games.lua`)
```lua
addappid(1361510) -- 解锁主游戏
addappid(1361511, 0, "5954562e7f5260400040a818bc29b60b335bb690066ff767e20d145a3b6b4af0") -- 带 Depot Key 解锁
addtoken(1361510, "2764735786934684318") -- 访问令牌
setManifestid(1361511, "5656605350306673283") -- 锁定清单 GID
setAppTicket(1361510, "0100000000000000...") -- Denuvo / SteamStub 凭据
```

---

## 阶段规划与路线图

详见 [docs/ROADMAP.md](docs/ROADMAP.md) 与 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
