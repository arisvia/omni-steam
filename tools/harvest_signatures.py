#!/usr/bin/env python3
"""Harvest OmniSteam function signatures from Steam client binaries.

Supports ELF (Linux steamclient.so), Mach-O (macOS steamclient.dylib) and
PE (Windows steamclient64.dll / steamui.dll). The harvester reads the symbol
table (dynamic symbols included), matches target functions by unique
mangled/demangled substring, and emits:

  <out_dir>/<sha256[:16]>.toml    hash-keyed signature file consumed by
                                  PatternLoader at runtime
  <out_dir>/report-<sha256[:16]>.json
                                  full candidate dump for manual review of
                                  ambiguous or unresolved functions

Usage:
  python tools/harvest_signatures.py --binary <path> --out <dir> [--kind elf|macho|pe]

No third-party dependencies are required for ELF/Mach-O (uses readelf/nm +
c++filt). PE parsing uses `pefile` if installed, otherwise falls back to
`dumpbin /EXPORTS` when available.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Target functions: canonical name -> list of distinctive substrings that are
# matched against BOTH raw mangled symbols and demangled names (case-folded).
TARGETS: dict[str, list[str]] = {
    "CheckAppOwnership": ["checkappownership"],
    "ConfigStore_GetBinary": ["configstore", "getbinary", "loaddepotdecryptionkey"],
    "GetPackageInfo": ["getpackageinfo"],
    "MarkLicenseAsChanged": ["marklicenseaschanged"],
    "ProcessPendingLicenseUpdates": ["processpendinglicenseupdates"],
    "BBuildAndAsyncSendFrame": ["bbuildandasyncsendframe", "buildandasendframe"],
    "RecvPkt": ["recvpkt", "recvpacket"],
    "OptedInMask": ["optedinmask"],
    "SpawnProcess": ["spawnprocess"],
    "FillInAppOverview": ["fillinappoverview"],
}

TOML_TEMPLATE = """\
# OmniSteam harvested signatures - DO NOT EDIT MANUALLY
# Source binary : {source_name}
# SHA256        : {sha256}
# Harvested by  : tools/harvest_signatures.py ({kind} backend)
binary_sha256 = "{sha256}"

[functions]
{function_entries}
"""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_tool(argv: list[str]) -> str:
    try:
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=120)
        return proc.stdout if proc.returncode == 0 else ""
    except (OSError, subprocess.TimeoutExpired):
        return ""


def demangle_itanium(names: list[str]) -> dict[str, str]:
    """Demangle a batch of Itanium-ABI names via c++filt (best effort)."""
    if not names:
        return {}
    result: dict[str, str] = {}
    filt = shutil.which("c++filt") or shutil.which("llvm-cxxfilt")
    if not filt:
        return result
    for name in names:
        out = run_tool([filt, name]).strip()
        if out:
            result[name] = out
    return result


def collect_symbols_elf(binary: Path) -> dict[str, int]:
    """name -> rva for defined dynamic+symtab symbols, via readelf."""
    symbols: dict[str, int] = {}
    output = run_tool(["readelf", "-W", "-s", str(binary)])
    for line in output.splitlines():
        parts = line.split()
        # readelf -s columns: Num: Value Size Type Bind Vis Ndx Name [ver]
        if len(parts) < 8 or ":" not in parts[0]:
            continue
        value_text, sym_type, bind, _, ndx, name = parts[1], parts[3], parts[4], parts[5], parts[6], parts[7]
        if sym_type not in ("FUNC", "IFUNC") or bind == "LOCAL":
            continue
        if ndx == "UND":
            continue
        try:
            rva = int(value_text, 16)
        except ValueError:
            continue
        if rva and name and not name.startswith("$"):
            symbols.setdefault(name, rva)

    # Fallback to nm when readelf produced nothing.
    if not symbols:
        nm_out = run_tool(["nm", "--defined-only", "-S", "--print-armap", str(binary)])
        for line in nm_out.splitlines():
            match = re.match(r"^([0-9a-fA-F]+)\s+\S+\s+(\S+)\s+(.+)$", line.strip())
            if match and " t T W i".find(match.group(2)[0]) >= 0:
                symbols.setdefault(match.group(3).strip(), int(match.group(1), 16))
    return symbols


def collect_symbols_macho(binary: Path) -> dict[str, int]:
    """name -> rva via nm (defined external + internal text symbols)."""
    symbols: dict[str, int] = {}
    nm_out = run_tool(["nm", "-U", str(binary)]) or run_tool(["nm", str(binary)])
    for line in nm_out.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+([TtWw])\s+(.+)$", line.strip())
        if match:
            symbols.setdefault(match.group(3).strip(), int(match.group(1), 16))
    return symbols


def collect_symbols_pe(binary: Path) -> tuple[dict[str, int], list[str]]:
    """name -> rva via pefile (preferred) or dumpbin fallback."""
    symbols: dict[str, int] = {}
    notes: list[str] = []

    try:
        import pefile  # type: ignore

        pe = pefile.PE(str(binary), fast_load=True)
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]]
        )
        base = pe.OPTIONAL_HEADER.ImageBase
        if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
            for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
                if exp.name and exp.address:
                    symbols.setdefault(exp.name.decode("utf-8", "replace"), exp.address)
        notes.append(f"pefile ok (exports={len(symbols)}, imagebase=0x{base:x})")
    except ImportError:
        notes.append("pefile not installed; trying dumpbin")
        dumpbin = shutil.which("dumpbin")
        if dumpbin:
            out = run_tool(["dumpbin", "/EXPORTS", str(binary)])
            for line in out.splitlines():
                match = re.match(r"^\s*[0-9]+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)$", line)
                if match:
                    rva = int(re.match(r"^\s*\d+\s+[0-9A-Fa-f]{8}\s+([0-9A-Fa-f]+)", line).group(1), 16)
                    symbols.setdefault(match.group(1), rva)
        else:
            notes.append("dumpbin unavailable; PE exports will be empty")
    except Exception as exc:  # noqa: BLE001 - harvesting must never crash CI
        notes.append(f"pefile error: {exc}")
    return symbols, notes


def harvest(binary: Path, out_dir: Path, kind: str | None) -> int:
    data_sha = sha256_file(binary)
    detected = kind or detect_kind(binary)
    out_dir = out_dir / platform_dir(binary, detected)
    print(f"[harvest] {binary.name} sha256={data_sha} kind={detected} platform={out_dir.name}")
    notes: list[str] = []
    if detected == "elf":
        raw = collect_symbols_elf(binary)
    elif detected == "macho":
        raw = collect_symbols_macho(binary)
    elif detected == "pe":
        raw, notes = collect_symbols_pe(binary)
    else:
        print("[harvest] unknown binary format", file=sys.stderr)
        return 1

    print(f"[harvest] {len(raw)} candidate symbols collected")
    if notes:
        for note in notes:
            print(f"[harvest] note: {note}")

    demangled = demangle_itanium([n for n in raw if n.startswith("_Z")]) if detected != "pe" else {}

    resolved: dict[str, dict] = {}
    candidates: dict[str, list[dict]] = {}
    for canonical, keys in TARGETS.items():
        matches: list[tuple[str, int]] = []
        for name, rva in raw.items():
            haystacks = [name.lower(), demangled.get(name, "").lower()]
            if any(key in h for key in keys for h in haystacks):
                matches.append((name, rva))
        candidates[canonical] = [{"symbol": n, "rva": f"0x{r:x}", "demangled": demangled.get(n, "")} for n, r in matches]
        unique_rvas = {r for _, r in matches}
        if len(unique_rvas) == 1:
            resolved[canonical] = {"rva": f"0x{matches[0][1]:x}", "source": f"{detected}:symbol"}
        elif len(unique_rvas) > 1:
            print(f"[harvest] AMBIGUOUS {canonical}: {len(unique_rvas)} distinct RVAs (see report)")

    out_dir.mkdir(parents=True, exist_ok=True)
    short = data_sha[:16]

    # Modern Steam clients strip internal symbols - zero resolved targets is
    # the COMMON outcome there. Writing an empty TOML would only add repo
    # noise, so emit the report alone and skip the signature file.
    if not resolved:
        print(f"[harvest] {binary.name}: 0/{len(TARGETS)} resolved; skipping TOML (report written)")
        report_path = out_dir / f"report-{short}.json"
        report_path.write_text(
            json.dumps(
                {"binary": binary.name, "sha256": data_sha, "resolved": {}, "candidates": candidates,
                 "note": "no uniquely-resolved functions"},
                indent=2,
            ),
            encoding="utf-8",
        )
        return 0

    function_entries = "\n".join(
        f'{name} = {{ rva = "{meta["rva"]}", source = "{meta["source"]}" }}' for name, meta in sorted(resolved.items())
    )
    toml_path = out_dir / f"{short}.toml"
    toml_path.write_text(
        TOML_TEMPLATE.format(
            source_name=binary.name,
            sha256=data_sha,
            kind=detected,
            function_entries=function_entries,
        ),
        encoding="utf-8",
    )

    report_path = out_dir / f"report-{short}.json"
    report_path.write_text(
        json.dumps({"binary": binary.name, "sha256": data_sha, "resolved": resolved, "candidates": candidates}, indent=2),
        encoding="utf-8",
    )

    print(f"[harvest] resolved {len(resolved)}/{len(TARGETS)} targets -> {toml_path}")
    for name in TARGETS:
        if name not in resolved:
            print(f"[harvest]   UNRESOLVED: {name}")
    return 0


def platform_dir(binary: Path, kind: str) -> str:
    """Canonical platform directory consumed by PatternLoader's remote tier
    (see PatternLoader::GetSignaturePlatformDir) and by the collect step of
    signature-harvest.yml. Linux must be split by architecture: the 32-bit
    ubuntu12_32 steamclient.so has its own binary hash and its own TOML
    namespace (signatures/linux-i386/), otherwise i386 clients would 404."""
    if kind == "pe":
        return "windows-x64"
    if kind == "macho":
        return "macos-universal"
    if kind == "elf":
        import struct

        with binary.open("rb") as fh:
            ident = fh.read(20)
        if len(ident) >= 19:
            ei_class, ei_machine = ident[4], struct.unpack_from("<H", ident, 18)[0]
            if ei_class == 2:
                return "linux-x64"
            if ei_class == 1 and ei_machine in (3, 40, 8):  # EM_386 / EM_ARM / EM_MIPS
                return "linux-i386"
            if ei_class == 2 or ei_class == 1:
                return "linux-x64"
    return "unknown"


def detect_kind(binary: Path) -> str:
    with binary.open("rb") as fh:
        magic = fh.read(4)
    if magic.startswith(b"\x7fELF"):
        return "elf"
    if magic in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xfe\xed\xfa\xcf", b"\xce\xfa\xed\xfe"):
        return "macho"
    if magic.startswith(b"MZ"):
        return "pe"
    return "unknown"


def find_steam_libraries(root: Path) -> list[Path]:
    patterns = ["steamclient.so", "steamclient64.dll", "steamclient.dylib", "steamui.dll", "steamui.dylib", "steamui.so"]
    found: list[Path] = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for filename in filenames:
            if filename.lower() in patterns:
                found.append(Path(dirpath) / filename)
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, help="single binary to harvest")
    parser.add_argument("--scan-root", type=Path, help="directory tree scanned for steamclient/steamui libraries")
    parser.add_argument("--out", type=Path, required=True, help="output directory for TOML + reports")
    parser.add_argument("--kind", choices=["elf", "macho", "pe"], help="override binary format detection")
    args = parser.parse_args()

    targets: list[Path]
    if args.scan_root:
        targets = find_steam_libraries(args.scan_root)
        print(f"[harvest] scan root {args.scan_root}: {len(targets)} steam libraries found")
    elif args.binary:
        targets = [args.binary]
    else:
        parser.error("either --binary or --scan-root is required")

    status = 0
    for binary in targets:
        status |= harvest(binary, args.out, args.kind)
    return status


if __name__ == "__main__":
    sys.exit(main())
