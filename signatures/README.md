# OmniSteam Signature Database

此目录存放由 `tools/harvest_signatures.py` 从官方 Steam 客户端采集的函数签名数据
（RVA / 符号事实数据，不含任何 Valve 二进制本体），按平台分子目录：

```
signatures/
├── linux-x64/        # steamclient.so (x86_64) — <sha256[:16]>.toml
├── windows-x64/      # steamclient64.dll / steamui.dll
└── macos-universal/  # steamclient.dylib (arm64 / x86_64)
```

## 数据流

1. 手动触发 `.github/workflows/signature-harvest.yml`（Actions 页面 → Signature Harvest → Run workflow）
2. 三个平台 job 分别获取官方 Steam 客户端、提取 `steamclient` 库、运行采集脚本：
   - 二进制本体仅上传为 **私有 artifact（7 天过期）**，不入库
   - 生成的 `<sha256>.toml` 签名文件自动提交回本目录
3. 发布打包时 `scripts/package-release.sh` 将本目录随 dist 分发
4. Manager `CoreInstaller::InstallCore` 部署到 `<cache>/signatures/`
5. Core 启动时 `PatternLoader` 按 **二进制 SHA256 匹配** 加载对应签名，
   优先级：本地 RVA 缓存 > 本目录 > 内置特征码扫描

## 文件格式

```toml
binary_sha256 = "<完整 SHA256>"

[functions]
CheckAppOwnership = { rva = "0x9BBA20", source = "elf:symbol" }
```

未唯一解析的函数会出现在同目录 `report-<sha256>.json` 候选报告中，供人工复核。
