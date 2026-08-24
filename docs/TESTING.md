# OmniSteam 测试体系与功能模块划分指南 (Testing & Modules)

测试代码位于 `tests/` 目录，按业务领域独立拆分。采用零外部框架的断言驱动（`assert`）设计，
秒级编译执行，离线 CI 环境稳定通过。

---

## 一、测试套件与模块映射表

| CTest 测试名 | 目标二进制 | 源码文件 | 覆盖的核心功能与业务模块 |
| :--- | :--- | :--- | :--- |
| **`PlatformTests`** | `test_platform` | `tests/test_platform.cpp` | **底层跨平台抽象层**：特征字节搜索 (`ByteSearch`)、十六进制编解码 (`Encoding`)、凭据票据存取 (`CredentialStore`)、反作弊白名单常量。 |
| **`IpcMetadataTests`** | `test_ipc_metadata` | `tests/test_ipc_metadata.cpp` | **Steam IPC 与元数据引擎**：结构偏移量断言（Ownership/PackageInfo 布局）、指令特征扫描 (`PatternLoader`)、IPC 序列化 (`SteamIPC`)、Manifest 解析 (`ManifestClient`)。 |
| **`ScriptManagerTests`** | `test_script_manager` | `tests/test_script_manager.cpp` | **Lua 解锁脚本管理**：AppID/DLC/Token 脚本生成、启用与停用状态切换、目录扫描。 |
| **`CloudSaveTests`** | `test_cloud_save` | `tests/test_cloud_save.cpp` | **云存档引擎**：存档路径探测（userdata / Proton compatdata）、WebDAV 客户端配置。 |
| **`PackagingIntegrationTests`** | `test_packaging_integration` | `tests/test_packaging_integration.cpp` | **打包与 Decky 整合**：发布资源定义、SteamOS 安装脚本、Decky 插件规范校验。 |
| **`DepotKeyTests`** | `test_depot_keys` | `tests/test_depot_keys.cpp` | **Depot 密钥仓库**：`depotkeys.bin` 二进制加载、二分查找、恶意头防护（数量上限/溢出）。 |
| **`PicsTokenTests`** | `test_pics_token` | `tests/test_pics_token.cpp` | **PICS 令牌注入器**：eMsg 8903 报文改写（令牌注入/覆盖/已正确则跳过）、无关字段逐字节保留、畸形输入安全拒绝。 |
| **`StatsProtoTests`** | `test_stats_proto` | `tests/test_stats_proto.cpp` | **protobuf 字段操作库**：varint 往返（含负数 10 字节编码）、fixed64 读取、追加覆盖语义、repeated 字段剥离、畸形输入安全拒绝。 |

> 已知局限：部分套件存在恒真断言（如 constexpr 常量与字面量比较），行为级覆盖将在阶段 7 增强
> （重点：LuaConfig 双缓冲并发、ManifestClient 缓存 TTL、ApiRouter 输入净化）。

---

## 二、运行测试命令

### 1. 运行完整测试套件 (CTest)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure -C Release
```

### 2. 独立运行指定功能模块测试

在 `build/bin/` 下直接执行：

```bash
./build/bin/test_platform               # 平台抽象层
./build/bin/test_ipc_metadata           # IPC 与元数据引擎
./build/bin/test_script_manager         # 脚本生命周期
./build/bin/test_cloud_save             # 云存档与 WebDAV
./build/bin/test_packaging_integration  # 打包与 Decky 规范
./build/bin/test_depot_keys             # Depot 密钥库
./build/bin/test_pics_token             # PICS 令牌注入器
```

---

## 三、测试设计规范

1. **完全自包含**：不依赖 GTest / Catch2 等外部框架，`assert` 驱动，编译与执行均为秒级。
2. **跨平台兼容**：临时文件统一使用 `std::filesystem::temp_directory_path()`，避免硬编码路径权限问题。
3. **零网络强制依赖**：网络相关模块内置本地兜底或纯解析验证，CI 隔离环境稳定通过。

## 四、提交前质检

除单元测试外，提交前必须运行项目自带静态质检工具：

```bash
python tools/check_code.py --fix
```

自动完成：标准 include 完整性校验、括号/预处理器配平检查、全量 C++ 文件 clang-format。
（上游参考目录 `source/` 已排除在检查与构建之外。）
