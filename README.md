# OmniSteam

> **One Codebase. All Platforms. Universal Steam Enhancement & Unlocker.**

OmniSteam 是一个现代、开源、全平台兼容的 Steam 客户端增强工具，原生支持 **Windows**、**Linux (含 Steam Deck / SteamOS)** 和 **macOS**。

---

## 核心特性

- **跨平台原生注入**：
  - Windows: DLL 劫持 (`dwmapi.dll` / `xinput1_4.dll`)
  - Linux / Steam Deck: `LD_PRELOAD` 原生预加载
  - macOS: `DYLD_INSERT_LIBRARIES` 注入
- **通用 Hook 引擎**：基于 `funchook`（Capstone 反汇编器），支持 x86、x86_64 及 ARM64。
- **内核级热重载**：Linux (`inotify`)、Windows (`ReadDirectoryChangesW`)、macOS (`kqueue`) 全平台配置文件实时生效。
- **零破坏性**：不篡改任何 Steam 二进制文件或本地文件校验。

---

## 快速上手

### 1. 编译
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### 2. 运行

- **Linux / Steam Deck**:
  ```bash
  ./scripts/omnisteam.sh
  ```
- **macOS**:
  ```bash
  DYLD_INSERT_LIBRARIES=./build/lib/libomnisteam.dylib /Applications/Steam.app/Contents/MacOS/steam_osx
  ```
- **Windows**:
  复制生成的 `libomnisteam.dll` 和 `dwmapi.dll` 至 Steam 安装目录。

### 3. 配置游戏解锁 (`~/.config/omnisteam/lua/games.lua`)
```lua
addappid(1361510)
addappid(1361511, 0, "5954562e7f5260400040a818bc29b60b335bb690066ff767e20d145a3b6b4af0")
addtoken(1361510, "2764735786934684318")
setManifestid(1361511, "5656605350306673283")
setAppTicket(1361510, "0100000000000000...")
```
