# OmniSteam 测试体系与功能模块划分指南 (Testing & Modules)

测试代码位于 `tests/` 目录，按业务领域独立拆分。采用零外部框架的断言驱动（`assert`）设计，
秒级编译执行，离线 CI 环境稳定通过。

---

## 一、测试套件与模块映射表

| CTest 测试名 | 目标二进制 | 源码文件 | 覆盖的核心功能与业务模块 |
| :--- | :--- | :--- | :--- |
| **`PlatformTests`** | `test_platform` | `tests/test_platform.cpp` | **底层跨平台抽象层**：特征字节搜索 (`ByteSearch`)、十六进制编解码 (`Encoding`)、凭据票据存取 (`CredentialStore`)、反作弊白名单常量。 |
| **`HttpTransportTests`** | `test_http` | `tests/test_http.cpp` | **HTTP 传输规范与安全**：URL 白名单策略、域名合法性、离线请求超时预算与强制错误处理、自研 WinHTTP/libcurl 边界。 |
| **`IpcMetadataTests`** | `test_ipc_metadata` | `tests/test_ipc_metadata.cpp` | **Steam IPC 与元数据引擎**：结构偏移量断言（Ownership/PackageInfo 布局）、指令特征扫描 (`PatternLoader`)、IPC 序列化 (`SteamIPC`)、Manifest 解析 (`ManifestClient`)。 |
| **`ScriptManagerTests`** | `test_script_manager` | `tests/test_script_manager.cpp` | **Lua 解锁脚本管理**：AppID/DLC/Token 脚本生成、DepotKey 智能去重、Denuvo 票据语句生成、启用/停用/删除全生命周期、AppID=0 边界防御。 |
| **`CloudSaveTests`** | `test_cloud_save` | `tests/test_cloud_save.cpp` | **云存档引擎**：存档路径探测（userdata / Proton compatdata）、WebDAV 客户端配置。 |
| **`PackagingIntegrationTests`** | `test_packaging_integration` | `tests/test_packaging_integration.cpp` | **打包与系统整合**：发布资源定义、SteamOS 脚本、Decky 插件规范、全平台签名完整性校验、`xinput1_4.def` 序数转发映射验证。 |
| **`DepotKeyTests`** | `test_depot_keys` | `tests/test_depot_keys.cpp` | **Depot 密钥仓库**：`depotkeys.bin` 二进制加载、二分查找、恶意头防护（数量上限/溢出）。 |
| **`PicsTokenTests`** | `test_pics_token` | `tests/test_pics_token.cpp` | **PICS 令牌注入器**：eMsg 8903 报文改写（令牌注入/覆盖/已正确则跳过）、无关字段逐字节保留、畸形输入安全拒绝。 |
| **`StatsProtoTests`** | `test_stats_proto` | `tests/test_stats_proto.cpp` | **protobuf 字段操作库**：varint 往返（含负数 10 字节编码）、fixed64 读取、追加覆盖语义、repeated 字段剥离、畸形输入安全拒绝。 |

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
./build/bin/test_http                   # HTTP 传输边界与超时
./build/bin/test_ipc_metadata           # IPC 与元数据引擎
./build/bin/test_script_manager         # 脚本生命周期
./build/bin/test_cloud_save             # 云存档与 WebDAV
./build/bin/test_packaging_integration  # 打包与 Decky 规范
./build/bin/test_depot_keys             # Depot 密钥库
./build/bin/test_pics_token             # PICS 令牌注入器
./build/bin/test_stats_proto            # 成就与统计 Protobuf 字段解析

### 3. 本地工件目录（全部在项目内、全部已忽略）

中间产物一律放这两处，不要新建 `source/`、`tmp/` 之类的顶层目录：

| 目录 | 内容 | 用途 |
| :--- | :--- | :--- |
| `.deps/` | `capstone-src`、`funchook_src-src`、`lua_source-src`、`spdlog-src`、`tomlplusplus-src`（FetchContent 检出） | Windows 上 FetchContent 走 git 拉浅克隆子模块会卡死，配置时显式指向本地检出：`-DFETCHCONTENT_SOURCE_DIR_CAPSTONE=.deps/capstone-src -DFETCHCONTENT_SOURCE_DIR_FUNCHOOK_SRC=.deps/funchook_src-src -DFETCHCONTENT_SOURCE_DIR_LUA_SOURCE=.deps/lua_source-src -DFETCHCONTENT_SOURCE_DIR_SPDLOG=.deps/spdlog-src -DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=.deps/tomlplusplus-src` |
| `.cache/` | `binrt/`（Linux SteamRT 容器真运行体两版）、`run-<id>/`（按 harvest run 取的当前三平台客户端 + 发布签名）、`refs/`（**旧版参考客户端**：`71b391fe…steamclient64.dll` / `abd32eb3…steamclient64.so` / `42776e12…steamclient.dylib` / `1bf1a790…steamui.dylib`）、`watch_run.sh`、`fetch_run_artifacts.sh` | 本地跑 `derive_signatures.py` / `validate_signatures.py` / `verify_abi_layout.py` 的输入镜像。`refs/` 里是 Valve 已不再分发的历史构建，删了取不回；当前版镜像用 `bash .cache/fetch_run_artifacts.sh <run-id> <artifact-name>...` 重取 |

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

## 五、布局锚点复看（钩子写进去的偏移，客户端真的读它吗）

`static_assert(offsetof(...))` 只能证明我们自己的头文件自洽。钩子真正写进客户端对象的两处布局——
`AppOwnership`（`PackageId 0x00` / `ReleaseState 0x04` / `ExistInPackageNums 0x14` / `bOwnsLicense 0x24` /
`bFreeLicense 0x28` / `bFamilyShared 0x35`）与 `CNetPacket`（`m_pubData 0x10` / `m_cubData 0x18`）——
需要外部证据。偏移错位时 RVA 仍是错的概率为零（派生层每次重算），钩子照样附着、进程照样活着，
**唯一症状是解锁静默失效**，所以两层证据都要看。

### 1. 静态语义门禁（无需账号，已进 `signature-harvest.yml` 三平台 job）

对**当前官方客户端**在**已发布的 RVA** 上反汇编客户端自己的 `CheckAppOwnership`，核对我们写入的每个偏移：

```bash
# 单个镜像 + 它自己的签名 TOML（data 分支 signatures/<platform>/<sha16>.toml）
python tools/verify_abi_layout.py --binary <sha16>-steamclient.so --toml <sha16>.toml

# 整批（CI 用法：hash 前缀命名的目录 + 平台签名目录），并跑负向对照
python tools/verify_abi_layout.py --scan-root steam-dist-cls/linux-x64 \
  --signatures signatures-out --strict --negative-control
```

判读：`CONFIRMED` 全部规则命中；`PARTIAL` 有规则只是没观察到（编译器把比较搬进了被调方，正常）；
`CONTRADICTED` = **该 build 的布局与钩子假定不符，禁止发布这套 RVA**（退出码 1）。
`--negative-control` 把整套偏移整体位移 1 字节再跑一遍，必须被判 CONTRADICTED——否则说明规则空转，
门禁绿了也不算证据。`CNetPacket` 故意不在此工具覆盖范围内：RecvPkt 只通过包对象自己的 vtable 取载荷，
静态看不到具体类，配对匹配只会命中别的结构。

### 2. 运行时布局探针（真机，`core_status.json` 的 `layout` 段）

```bash
python tools/smoke_status.py ~/.cache/omnisteam/core_status.json --platform windows
```

`layout` 由两个探针组成，`observed` = 客户端交给我们多少个真实对象，`matched` = 其中按**我们的偏移**
解释得通的个数：

| 观察结果 | 含义 | 门禁 |
| :--- | :--- | :--- |
| `ownership 8/9 coherent` | 客户端自填的结构在 0x24/0x00/0x04 三项上同时自洽 | ok |
| `layout drift: ... 0/N coherently` | 偏移漂了：钩子写对了函数、写错了字段 | **FAIL** |
| `layout probe unobserved` | 这一轮客户端没走到（`+login anonymous` 会话没有可检查的许可） | warn |
| `missing layout object` | 跑在旧核心上（没发布探针） | **FAIL** |

### 3. 关闭 ROADMAP §2 条目 4 需要的产品级复看（要真实账号）

CI 用匿名会话，只能证明"没漂"；还需要一次**解锁结果**的复看，每台机器各一次即可：

1. 装当前核心，用真实账号登录，Lua 里注入一个未拥有的 AppID 及其一个 DLC AppID。
2. 打开库：两者都应出现（`FillInAppOverview` 的合成 `PurchasedTime` 路径）且状态可安装。
3. 起一次下载：manifest 请求码由 `Hooks_NetPacket` 注入，CDN 应直接开跑，不出现 "No Internet Connection"。
4. 读 `core_status.json` 的 `layout`：`ownershipObserved > 0` 且 `ownershipMatched > 0`（Windows/Linux/macOS 各一份）。
5. 把三份计数与客户端哈希贴回 `docs/ROADMAP.md` §2 条目 4，该条目即可关闭。
