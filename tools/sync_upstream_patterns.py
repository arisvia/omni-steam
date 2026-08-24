#!/usr/bin/env python3
"""Anchor-based pattern derivation from the official Steam binary.

Upstream publishes per-version *anchors* keyed by the SHA256 of the Steam
binary in the dedicated signature repository (branch `pattern`, files at the
repository root):

    https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@pattern/<sub>/<sha256>.toml

The anchors only say WHERE a function lives. This script then works directly
on the locally downloaded official Steam binary:

  1. maps each anchor RVA to a file offset via the PE section table,
  2. disassembles with Capstone and derives a fresh wildcarded byte pattern
     (relative branches and RIP-relative displacements are masked),
  3. verifies upstream reference signatures against the actual bytes and
     drops any that do not match.

A binary whose hash is not yet in the upstream database yields zero anchors;
that is an EXPECTED state right after a Steam update (exit code 0, notice
printed) - Windows keeps working through built-in patterns until upstream
catches up.

Usage:
  python tools/sync_upstream_patterns.py --binary <dll> --out <dir> \
      [--sub steamclient] [--owner OpenSteam001] [--repo steam-monitor] [--ref pattern]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import urllib.request
from pathlib import Path

BLOCK_RE = re.compile(r"^\[0x([0-9A-Fa-f]+)\]$", re.M)
FIELD_RE = re.compile(r'^(\w+)\s*=\s*"(.*)"\s*$', re.M)

MIN_SOLID_BYTES = 14
MIN_TOTAL_BYTES = 18
MAX_TOTAL_BYTES = 48


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch_text(url: str) -> str | None:
    try:
        with urllib.request.urlopen(url, timeout=30) as resp:  # noqa: S310
            if resp.status != 200:
                return None
            return resp.read().decode("utf-8", "replace")
    except Exception:  # noqa: BLE001 - network probing must stay non-fatal
        return None


def parse_upstream(text: str) -> dict[str, dict[str, str]]:
    functions: dict[str, dict[str, str]] = {}
    blocks = list(BLOCK_RE.finditer(text))
    for i, block in enumerate(blocks):
        end = blocks[i + 1].start() if i + 1 < len(blocks) else len(text)
        body = text[block.end():end]
        fields = dict(FIELD_RE.findall(body))
        name = fields.get("name")
        rva = fields.get("rva")
        if not name or not rva:
            continue
        entry = {"rva": rva}
        if fields.get("sig"):
            entry["sig"] = fields["sig"]
        functions[name] = entry
    return functions


# Upstream names that need mapping onto our canonical identifiers.
NAME_ALIASES = {
    "ConfigStoreGetBinary": "ConfigStore_GetBinary",
}


class BinaryView:
    """RVA <-> file offset mapping plus raw access for one PE image."""

    def __init__(self, path: Path):
        import pefile  # type: ignore

        self.path = path
        self.data = path.read_bytes()
        self.pe = pefile.PE(data=self.data, fast_load=True)
        self.sections = [
            (s.VirtualAddress, max(s.Misc_VirtualSize, s.SizeOfRawData), s.PointerToRawData, s.SizeOfRawData)
            for s in self.pe.sections
        ]

    def rva_to_offset(self, rva: int) -> int | None:
        for vaddr, vsize, raw, rsize in self.sections:
            if vaddr <= rva < vaddr + min(vsize, rsize):
                delta = rva - vaddr
                if delta < rsize:
                    return raw + delta
        return None

    def read(self, offset: int, length: int) -> bytes:
        return self.data[offset : offset + length]

    def text_section(self) -> tuple[int, bytes]:
        # Prefer the section named .text when present.
        for s in self.pe.sections:
            if s.Name.rstrip(b"\x00") == b".text":
                return s.VirtualAddress, self.data[s.PointerToRawData : s.PointerToRawData + s.SizeOfRawData]
        # Fallback: first executable section.
        for s in self.pe.sections:
            if s.Characteristics & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
                return s.VirtualAddress, self.data[s.PointerToRawData : s.PointerToRawData + s.SizeOfRawData]
        raise RuntimeError("no executable section")


def signature_to_regex(pattern: str):
    parts = []
    for token in pattern.split():
        if token and set(token) == {"?"}:
            # One wildcard byte per token; tolerate IDA-style "??" and "?".
            parts.append(b".")
        else:
            # Explicit hex escapes: re.escape mangles high bytes like 0x89.
            parts.append(b"\\x%02x" % int(token, 16))
    return re.compile(b"".join(parts), re.DOTALL)


def derive_pattern(view: BinaryView, rva: int, md) -> str | None:
    """Disassembles forward from rva and builds an IDA-style wildcard pattern."""
    offset = view.rva_to_offset(rva)
    if offset is None:
        return None

    code = view.read(offset, MAX_TOTAL_BYTES + 16)
    tokens: list[str] = []
    solid = 0
    total = 0

    try:
        insns = list(md.disasm(code, rva))
    except Exception:  # noqa: BLE001
        return None

    for insn in insns:
        size = insn.size
        raw = code[total : total + size]
        if len(raw) != size:
            break

        mnemonic = insn.mnemonic
        is_rel_branch = mnemonic == "call" or mnemonic.startswith(("jmp", "j"))
        rip_relative = any(
            op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP
            for op in insn.operands
        )

        if is_rel_branch or rip_relative:
            keep = min(size - 4, size) if rip_relative else min(1, size)
            tokens += [f"{b:02X}" for b in raw[:keep]] + ["?"] * (size - keep)
            solid += keep
        else:
            tokens += [f"{b:02X}" for b in raw]
            solid += size

        total += size
        if solid >= MIN_SOLID_BYTES and total >= MIN_TOTAL_BYTES:
            break

    return " ".join(tokens) if total > 0 else None


def verify_signature(text_section_va: int, text_bytes: bytes, pattern: str, expected_rva: int | None) -> bool:
    regex = signature_to_regex(pattern)
    hits = 0
    first_hit_rva = None
    for match in regex.finditer(text_bytes):
        hits += 1
        if first_hit_rva is None:
            first_hit_rva = text_section_va + match.start()
        if hits > 4:
            break
    if hits == 0:
        return False
    if expected_rva is not None and hits == 1 and first_hit_rva != expected_rva:
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sub", action="append", default=[],
                        help="upstream subdirectory at the repository root (repeatable)")
    parser.add_argument("--owner", default="OpenSteam001")
    parser.add_argument("--repo", default="steam-monitor")
    parser.add_argument("--ref", default="pattern")
    parser.add_argument("--skip-derivation", action="store_true",
                        help="do not regenerate patterns from the local binary")
    args = parser.parse_args()

    subs = args.sub or ["steamclient"]
    digest = sha256_file(args.binary)
    mirrors = [
        f"https://cdn.jsdelivr.net/gh/{args.owner}/{args.repo}@{args.ref}",
        f"https://raw.githubusercontent.com/{args.owner}/{args.repo}/{args.ref}",
    ]

    md = None
    view = None
    if not args.skip_derivation:
        try:
            global capstone
            import capstone  # type: ignore

            view = BinaryView(args.binary)
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
            md.detail = True
        except Exception as exc:  # noqa: BLE001
            print(f"[sync] derivation unavailable ({exc}); falling back to anchor RVAs only")

    merged: dict[str, dict[str, str]] = {}
    report: dict[str, dict] = {"binary": args.binary.name, "sha256": digest, "sources": {}, "verified": {}}
    text_section = None
    if view is not None:
        try:
            text_section = view.text_section()
        except Exception:  # noqa: BLE001
            text_section = None

    for sub in subs:
        text = None
        used = None
        for base in mirrors:
            url = f"{base}/{sub}/{digest}.toml"
            text = fetch_text(url)
            if text is not None:
                used = url
                break
        if text is None:
            print(f"[sync] no upstream entry for {sub} @ {digest[:16]}")
            report["sources"][sub] = {"status": "missing"}
            continue

        functions = parse_upstream(text)
        print(f"[sync] {sub}: {len(functions)} anchored functions from {used}")
        report["sources"][sub] = {"status": "ok", "url": used, "count": len(functions)}

        for name, entry in functions.items():
            canonical = NAME_ALIASES.get(name, name)
            converted = {"rva": entry["rva"], "source": f"upstream:{sub}"}

            try:
                rva_int = int(entry["rva"], 16)
            except ValueError:
                rva_int = None

            if view is not None and md is not None and rva_int is not None:
                derived = derive_pattern(view, rva_int, md)
                if derived:
                    converted["sig"] = derived
                    converted["source"] += "+derived"

            if "sig" in entry and text_section is not None:
                va, blob = text_section
                ok = verify_signature(va, blob, entry["sig"], rva_int)
                report["verified"][canonical] = {"upstream_sig_valid": ok}
                if not ok and "derived" not in converted.get("source", ""):
                    converted.pop("sig", None)

            merged[canonical] = converted

    if not merged:
        # Expected right after a Steam client update: upstream has not
        # anchored the new binary yet. Not an error - Windows stays on
        # built-in patterns until upstream catches up.
        print("[sync] no upstream anchors for this binary yet; skipping")
        return 0

    lines = [
        "# OmniSteam signatures - anchored by upstream DB, derived from the official binary",
        f'binary_sha256 = "{digest}"',
        "",
        "[functions]",
    ]
    for name, entry in sorted(merged.items()):
        extra = ", ".join(f'{k} = "{v}"' for k, v in entry.items())
        lines.append(f"{name} = {{ {extra} }}")

    args.out.mkdir(parents=True, exist_ok=True)
    toml_path = args.out / f"{digest[:16]}.toml"
    toml_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    report_path = args.out / f"report-{digest[:16]}.json"
    report_path.write_text(json.dumps({"resolved": merged, **report}, indent=2), encoding="utf-8")

    print(f"[sync] wrote {toml_path} ({len(merged)} functions)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

