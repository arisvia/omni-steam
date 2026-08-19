# OmniSteam 测试体系与功能模块划分指南 (Testing & Modules)

本项目已完成全功能模块划分与自动化测试规范化。测试代码位于 `tests/` 目录下，按照业务领域与底层模块独立拆分，摆脱了开发过程中的临时阶段标记，便于长期维护与持续集成。

---

## 一、 测试套件与模块映射表

| CTest 测试名 | 目标二进制 | 源码文件 | 覆盖的核心功能与业务模块 |
| :--- | :--- | :--- | :--- |
| **`PlatformTests`** | `test_platform` | `tests/test_platform.cpp` | **底层跨平台抽象层**：内存掩码特征搜索 (`ByteSearch`)、十六进制编解码 (`Encoding`)、凭证存储与票据存取 (`CredentialStore`)。 |
| **`IpcMetadataTests`** | `test_ipc_metadata` | `tests/test_ipc_metadata.cpp` | **Steam IPC 与特征元数据引擎**：Steam 二进制指令特征扫描 (`PatternLoader`)、无锁高效 IPC 消息流序列化/反序列化 (`SteamIPC`)、Manifest GID 解析器 (`ManifestClient`)。 |
| **`ScriptManagerTests`** | `test_script_manager` | `tests/test_script_manager.cpp` | **Lua 解锁脚本管理**：游戏 AppID / DLC / Token 代码自动生成、脚本启用与停用重命名状态切换、目录自动扫描。 |
| **`CloudSaveTests`** | `test_cloud_save` | `tests/test_cloud_save.cpp` | **云存档与同步引擎**：跨平台游戏存档目录探测（Steam UserData、Proton CompatData 路径）、WebDAV 客户端多平台配置。 |
| **`PackagingIntegrationTests`** | `test_packaging_integration` | `tests/test_packaging_integration.cpp` | **打包与 Decky 插件整合**：发布打包资源定义校验（Linux/Windows 特征文件、SteamOS 免 root 安装脚本）、Decky Loader 插件规范 (`plugin.json` 架构校验)。 |
| **`DepotKeyTests`** | `test_depot_keys` | `tests/test_depot_keys.cpp` | **Depot 密钥仓库**：本地 `depotkeys.json` 解析、GitHub Raw 远端在线密钥自动兜底拉取与 Depot ID 密钥精准匹配。 |

---

## 二、 运行测试命令

### 1. 运行完整测试套件 (CTest)

```bash
# 进入构建目录
cd build

# 执行全部测试，并输出详细日志
ctest --output-on-failure -C Release
```

### 2. 独立运行指定功能模块测试

在 `build/bin/`（或 Windows `build/bin/Release/`）中可以直接执行对应的模块可执行文件：

```bash
# 1. 运行平台抽象测试
./build/bin/test_platform

# 2. 运行 IPC 与特征测试
./build/bin/test_ipc_metadata

# 3. 运行脚本管理器测试
./build/bin/test_script_manager

# 4. 运行云存档测试
./build/bin/test_cloud_save

# 5. 运行打包与 Decky 插件集成测试
./build/bin/test_packaging_integration

# 6. 运行 Depot 密钥库测试
./build/bin/test_depot_keys
```

---

## 三、 测试设计规范

1. **完全自包含（Self-Contained）**：测试不依赖外部第三方测试框架（如 GTest / Catch2），采用断言驱动（`assert`）以极致轻量的方式运行，秒级编译与执行。
2. **跨平台兼容**：临时文件与目录统一使用 `std::filesystem::temp_directory_path()` 动态获取，防止在 Windows / Linux / macOS 上出现硬编码路径权限问题。
3. **零外部网络强制依赖**：网络测试模块均内置本地兜底逻辑或模式解析验证，在离线与 CI 隔离环境下稳定通过。
