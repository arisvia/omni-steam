# OmniSteam Signature Database

> **定位说明**：运行时符号解析（`SymbolTable`，零维护）是 Linux/macOS 的主要解析路径；
> 本目录的 TOML 数据库仅作为 **离线兜底**（符号被 strip 的发行版），可按需保留。

此目录存放由 `tools/harvest_signatures.py` 从官方 Steam 客户端采集的函数签名数据
（RVA / 符号事实数据，不含任何 Valve 二进制本体），按平台分子目录：

```
signatures/
├── linux-x64/        # steamclient.so (x86_64) — <sha256[:16]>.toml
├── windows-x64/      # steamclient64.dll / steamui.dll
└── macos-universal/  # steamclient.dylib (arm64 / x86_64)
```

## 解析优先级（PatternLoader）

1. 本地 RVA 缓存 (`cache/pattern_<sha256>.cache`)
2. **运行时符号表解析** (`SymbolTable`，遍历目标模块自身符号数据，自适应任意客户端更新)
3. 本目录 TOML（按二进制 SHA256 匹配）
4. 内置特征码扫描（Windows 主路径）

## 数据流

1. 手动触发 `.github/workflows/signature-harvest.yml`（Actions 页面 → Signature Harvest → Run workflow）
2. 三个平台 job 分别获取官方 Steam 客户端、提取 `steamclient` 库、运行采集脚本：
   - 二进制本体仅上传为 **私有 artifact（7 天过期）**，不入库
   - 生成的 `<sha256>.toml` 签名文件自动提交回本目录

### Windows 特征码的"锚点→派生→验证"流程

Windows 无符号表，函数位置真值无法凭空获得（鸡生蛋问题）。工作流的做法：

1. **锚点**：按二进制 SHA256 探测上游 opensteamtool 特征库是否已收录该版本
   （只取 name→RVA 对应关系，即"函数在哪"这一事实）；
2. **派生**：用 Capstone 在**我们下载的官方二进制**上从锚点 RVA 反汇编，
   重新生成特征码——相对跳转、RIP 相对寻址等易变操作数自动通配；
3. **验证**：上游参考特征码在真实字节流中全文匹配校验，失配则丢弃
   （保留 RVA，因为哈希键控文件里 RVA 本身已足以定位）。

因此入库的特征码全部源自 Valve 官方字节流，上游仅提供定位线索。

3. 发布打包时 `scripts/package-release.sh` 将本目录随 dist 分发
4. Manager `CoreInstaller::InstallCore` 部署到 `<cache>/signatures/`
5. Core 启动时 `PatternLoader` 按上述优先级逐级解析；运行时还会在内置
   特征码全部失配时主动拉取 `<base>/<platform>/<sha256>.toml` 远程兜底。

## 文件格式

```toml
binary_sha256 = "<完整 SHA256>"

[functions]
CheckAppOwnership = { rva = "0x9BBA20", source = "elf:symbol" }
```

未唯一解析的函数会出现在同目录 `report-<sha256>.json` 候选报告中，供人工复核。
