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
3. 发布打包时 `scripts/package-release.sh` 将本目录随 dist 分发
4. Manager `CoreInstaller::InstallCore` 部署到 `<cache>/signatures/`
5. Core 启动时 `PatternLoader` 按上述优先级逐级解析

## 文件格式

```toml
binary_sha256 = "<完整 SHA256>"

[functions]
CheckAppOwnership = { rva = "0x9BBA20", source = "elf:symbol" }
```

未唯一解析的函数会出现在同目录 `report-<sha256>.json` 候选报告中，供人工复核。
