#!/usr/bin/env python3
"""Convert upstream opensteamtool pattern TOMLs into OmniSteam schema.

Upstream publishes per-version signature files keyed by the SHA256 of the
Steam binary:

    https://cdn.jsdelivr.net/gh/<owner>/<repo>@<ref>/pattern/<sub>/<sha256>.toml

with blocks shaped like:

    [0x4B1B1D77]
    name = "CheckAppOwnership"
    rva  = "0x9BBA20"
    sig  = "48 8B C4 89 50 10 ..."

This script fetches those files for a given local binary (matched by hash)
and emits our unified schema so PatternLoader / the runtime remote fallback
can consume them:

    binary_sha256 = "<sha256>"

    [functions]
    CheckAppOwnership = { rva = "0x9BBA20", source = "upstream:steamclient" }

Usage:
  python tools/sync_upstream_patterns.py --binary <dll> --out <dir> \
      [--sub steamclient] [--owner OpenSteam001] [--repo OpenSteamTool] [--ref main]
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sub", action="append", default=[],
                        help="upstream subdirectory under pattern/ (repeatable)")
    parser.add_argument("--owner", default="OpenSteam001")
    parser.add_argument("--repo", default="OpenSteamTool")
    parser.add_argument("--ref", default="main")
    args = parser.parse_args()

    subs = args.sub or ["steamclient"]
    digest = sha256_file(args.binary)
    mirrors = [
        f"https://cdn.jsdelivr.net/gh/{args.owner}/{args.repo}@{args.ref}/pattern",
        f"https://raw.githubusercontent.com/{args.owner}/{args.repo}/{args.ref}/pattern",
    ]

    merged: dict[str, dict[str, str]] = {}
    report: dict[str, dict] = {"binary": args.binary.name, "sha256": digest, "sources": {}}
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
        for name, entry in functions.items():
            canonical = NAME_ALIASES.get(name, name)
            converted = {"rva": entry["rva"], "source": f"upstream:{sub}"}
            if "sig" in entry:
                converted["sig"] = entry["sig"]
            merged[canonical] = converted
        print(f"[sync] {sub}: {len(functions)} functions from {used}")
        report["sources"][sub] = {"status": "ok", "url": used, "count": len(functions)}

    if not merged:
        print("[sync] nothing harvested; no output written")
        return 1

    lines = [
        "# OmniSteam signatures synced from upstream opensteamtool database",
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
