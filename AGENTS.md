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

---

## 3. Pre-Commit Quality Checks
Before committing any changes, run the automated verification tool:
```bash
python tools/check_code.py --fix
```
This script automatically:
1. Validates standard include headers.
2. Checks delimiter and preprocessor balance.
3. Formats all 78+ C++ files with `clang-format`.
