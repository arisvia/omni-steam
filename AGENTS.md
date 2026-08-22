# AGENTS.md - OmniSteam Engineering Guidelines & Quality Standards

## 1. Project Overview & Target Architecture
- **Language**: C++20 standard, C11 standard.
- **Targets**:
  - **Windows (x64)**: Steamclient hook (`libomnisteam.dll`), Proxy DLL (`dwmapi.dll`), Native ImGui Manager (`omnisteam.exe`).
  - **Linux (x86_64 & i386)**: Dynamic Preload hook (`libomnisteam.so` built for both 64-bit and 32-bit `ubuntu12_32`), CLI & Web GUI Manager.
  - **macOS (Apple Silicon arm64 & Intel x86_64)**: Universal binary Mach-O hook (`libomnisteam.dylib`).
- **Hook Engine**: `funchook` (Capstone 5.0.1 based, universal across x86/x64/ARM64).

---

## 2. Inviolable Code Rules & Anti-Regressions

### A. C++ Forward Declarations & Order of Definition
- **Rule**: Functions called within anonymous namespaces or compilation units MUST either be defined BEFORE their first call site or have an explicit forward declaration above the first caller.
- **Reason**: Clang / GCC (used in Linux & macOS CI) perform strict single-pass parsing where calling an undeclared identifier is an immediate fatal compilation error (`use of undeclared identifier`), unlike MSVC two-pass template lookups.

### B. Header & Include Integrity
- **Rule**: Never rely on transitive includes. If a file uses any symbol from `<cstdint>`, `<vector>`, `<string>`, `<fstream>`, `<memory>`, `<mutex>`, `<algorithm>`, `<chrono>`, or `<regex>`, it MUST explicitly `#include` that header.
- **Verification**: Run `python tools/check_code.py` before committing.

### C. Preprocessor Directives & Platform Guards
- **Rule**: Every `#if`, `#ifdef`, and `#ifndef` MUST match exactly one `#endif`.
- **Convention**: Always use the canonical platform macros:
  - `OMNI_PLATFORM_WINDOWS`
  - `OMNI_PLATFORM_LINUX`
  - `OMNI_PLATFORM_MACOS`
  - `OMNI_ARCH_X64`, `OMNI_ARCH_X86`, `OMNI_ARCH_ARM64`

### D. Return Paths in Non-Void Functions
- **Rule**: Every non-void function MUST guarantee a return value on all execution paths.
- **Reason**: Clang `-Wreturn-type` is treated as a fatal compilation error.

### E. JSON Parsing Robustness
- **Rule**: When parsing external JSON APIs (e.g. Steam Store API), NEVER assume fixed field order in regex patterns. Match fields independently or extract object tokens safely.

### F. Centralized Endpoints & Steam Magic Constants
- **Rule**: NEVER hardcode raw URL literals or Valve magic protocol tokens across implementation files.
- **Convention**:
  - All external network URLs, GitHub asset paths, CDN mirrors, and WebAPI endpoints MUST reside in `include/OmniPlatform/OmniEndpoints.h` under their respective namespaces (`GitHub`, `Manifest`, `Stats`, `Steam`).
  - All Steam protocol magic tokens, package IDs, and internal structures MUST reside in `include/OmniPlatform/SteamTypes.h`.

### G. Manager Architecture & Decoupling
- **Rule**: The manager dashboard MUST adhere to the 3-layer architecture:
  - `StaticAssets.h/.cpp`: Pure HTML/CSS/JS/i18n assets (no business/network logic).
  - `ApiRouter.h/.cpp`: `/api/*` REST routing, request payload parsing, and response serialization.
  - `WebServer.h/.cpp`: Pure TCP socket lifecycle, listening loop, and connection management.

### H. Steam Hook Architecture & Minimal Intrusiveness
- **Rule**: OmniSteam operates strictly on terminal decision-point hooks (`CheckAppOwnership` for app/DLC entitlement and `ConfigStore_GetBinary` for depot decryption keys). NEVER hook or tamper with Steam internal PICS package metadata pipelines (e.g. `GetPackageInfo`, `PackageInfoMgr`, or broadcasting `MarkLicenseAsChanged` for synthetic Package 0).
- **Reason**: 64-bit SteamClient enforces asynchronous PICS cloud metadata synchronization. Faking or altering packages forces Steam to query Valve servers for nonexistent packages, causing indefinite "Loading user data..." network hangs and CPackageInfo hash-table memory corruption.
- **Convention**: All entitlement overrides MUST occur cleanly within `CheckAppOwnership` using canonical `SteamOffsets::Ownership64` and `SteamOffsets::Ownership32` constants (`ExistInPackageNums`, `ReleaseState = Released`, `bOwnsLicense = true`, `bIsSubscribed = true`), strictly preserving native positive responses.

---

## 3. Pre-Commit Quality Checks
Before committing any changes, run the automated verification tool:
```bash
python tools/check_code.py --fix
```
This script automatically:
1. Validates standard include headers.
2. Checks delimiter and preprocessor balance.
3. Formats all 84+ C++ files with `clang-format`.
