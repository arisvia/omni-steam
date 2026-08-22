# AGENTS.md - OmniSteam Engineering Guidelines & Quality Standards

## 1. Project Overview & Target Architecture
- **Language**: C++20 standard, C11 standard.
- **Targets**:
  - **Windows (x64)**: Steamclient hook (`libomnisteam.dll`), Proxy DLL (`dwmapi.dll`), Native ImGui / Web Manager (`omnisteam.exe`).
  - **Linux (x86_64 & i386)**: Dynamic Preload hook (`libomnisteam.so` built for both 64-bit and 32-bit `ubuntu12_32`), CLI & Web GUI Manager.
  - **macOS (Apple Silicon arm64 & Intel x86_64)**: Universal binary Mach-O hook (`libomnisteam.dylib`).
- **Hook Engine**: `funchook` (Capstone 5.0.1 based, universal across x86/x64/ARM64).

---

## 2. Inviolable Code Rules & Anti-Regressions

### A. C++ Forward Declarations & Order of Definition
- **Rule**: Functions called within anonymous namespaces or compilation units MUST either be defined BEFORE their first call site or have an explicit forward declaration above the first caller.
- **Reason**: Clang / GCC (used in Linux & macOS CI) perform strict single-pass parsing where calling an undeclared identifier is an immediate fatal compilation error (`use of undeclared identifier`), unlike MSVC two-pass template lookups.

### B. Header & Include Integrity
- **Rule**: Never rely on transitive includes. If a file uses any symbol from `<cstdint>`, `<vector>`, `<string>`, `<fstream>`, `<memory>`, `<mutex>`, `<algorithm>`, `<chrono>`, `<future>`, `<cstring>`, `<unordered_map>`, `<unordered_set>`, `<set>`, or `<regex>`, it MUST explicitly `#include` that header.
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

### E. JSON & Protobuf Parsing Robustness
- **Rule**: When parsing external JSON APIs (e.g. Steam Store API) or Steam Protobuf network packets, NEVER assume fixed field order or rigid schemas. Match fields independently, parse Varint/Tags safely, and handle buffer boundary checks gracefully.

### F. Centralized Endpoints & Steam Magic Constants
- **Rule**: NEVER hardcode raw URL literals or Valve magic protocol tokens across implementation files.
- **Convention**:
  - All external network URLs, GitHub asset paths, CDN mirrors, and WebAPI endpoints MUST reside in `include/OmniPlatform/OmniEndpoints.h` under their respective namespaces (`GitHub`, `Manifest`, `Stats`, `Steam`).
  - All Steam protocol magic tokens, package IDs, callback numbers, and internal structures MUST reside in `include/OmniPlatform/SteamTypes.h`.

### G. Manager Architecture & Decoupling
- **Rule**: 
  - The Manager dashboard MUST adhere to the 3-layer architecture (`StaticAssets`, `ApiRouter`, `WebServer`).
  - `DepotKeyStore` (230,000+ binary depot keys database & CDN sync) MUST remain strictly in Manager/CLI (business layer). Core Hook DLL (`libomnisteam`) MUST remain lightweight, loading keys and configurations solely from the Lua runtime.

### H. Core Hook Architecture & Specifications
- **Package 0 Dynamic Memory Expansion (`Hooks_Package`)**:
  - Unlocked AppIDs (Games & DLCs) and DepotIDs MUST be injected into Package 0 using Valve's exported `CUtlMemoryGrow` memory expansion routine to prevent memory corruption of `DepotIdVec`.
  - AppIDs and DepotIDs MUST be strictly separated: AppIDs in `AppIdVec` (for PICS metadata), DepotIDs in `DepotIdVec`.
- **Entitlement Decision Hook (`CheckAppOwnership`)**:
  - Injected apps MUST return `bOwnsLicense = true`, `bFreeLicense = false`, `ReleaseState = Released`, and `PackageId = 0`.
- **Depot Decryption Hook (`Hooks_Decryption`)**:
  - Intercept `ConfigStore_GetBinary` for `depots\<DepotId>\DecryptionKey` and supply the 32-byte AES key directly to Steam's content system.
- **SteamUI AppOverview Hook (`Hooks_SteamUI`)**:
  - Intercept `FillInAppOverview` in `steamui.dll` to supply non-zero `PurchasedTime` (synthetic purchase timestamp) for unlocked games, guaranteeing instant visibility and permanent retention in the Steam Library.
- **Network Packet Interception (`Hooks_NetPacket`)**:
  - Intercept `BBuildAndAsyncSendFrame` (eMsg 151) and `RecvPkt` (eMsg 147) for `ContentServerDirectory.GetManifestRequestCode#1`.
  - Asynchronously fetch 64-bit manifest request codes from `OmniEndpoints::Manifest` and inject `eresult = k_EResultOK` (1) with the request code to enable seamless CDN downloads without "No Internet Connection" failures.
- **Anti-Cheat Stealth Whitelist (`AntiCheatGuard`)**:
  - Competitive multiplayer games (CS2, Dota 2, TF2, Apex, etc.) MUST be recognized and transparently passed through to native Steam routines to prevent anti-cheat triggers.

---

## 3. Pre-Commit Quality Checks
Before committing any changes, run the automated verification tool:
```bash
python tools/check_code.py --fix
```
This script automatically:
1. Validates standard include headers.
2. Checks delimiter and preprocessor balance.
3. Formats all C++ files with `clang-format`.
