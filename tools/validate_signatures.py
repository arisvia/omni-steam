#!/usr/bin/env python3
"""Cross-check derived signature RVAs using two independent mechanisms.

A wrong RVA is worse than a missing one: it hooks an unrelated function and
crashes the Steam client. The derivation pipeline therefore has two mechanisms
that share no code path:

  1. Qualified VProf string literals -> RIP-relative xref -> function boundary
     table (.eh_frame_hdr / LC_FUNCTION_STARTS).
  2. RTTI vtable slot transfer from the committed anchor map, resolved through
     the platform's relocation stream (R_*_RELATIVE addends on ELF, the
     LC_DYLD_INFO rebase opcodes / chained fixups on Mach-O).

Where both mechanisms can name the same target, they MUST agree. This script
asserts that and exits non-zero on any disagreement, so CI refuses to publish
a signature set that would hook the wrong code.

Only 64-bit client binaries are considered (OmniSteam targets 64-bit platforms).

Usage:
  python tools/validate_signatures.py --scan-root <dir> --map <anchor-map.json>
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from derive_signatures import (  # noqa: E402
    Image,
    build_xref_index,
    find_string_vas,
    parse_elf,
    parse_macho,
    transfer_via_map,
)

# targets whose owning method also carries a unique, fully-qualified VProf
# literal - i.e. the ones reachable by BOTH mechanisms.
CROSS_CHECKS: dict[str, str] = {
    "RecvPkt": "CCMInterface::RecvPkt",
    "BBuildAndAsyncSendFrame": "CWebSocketConnection::BBuildAndAsyncSendFrame",
}


def load_image(path: Path) -> Image | None:
    head = path.read_bytes()[:8]
    if head[:4] == b"\x7fELF":
        if head[4] != 2:  # 64-bit only
            return None
        return parse_elf(path)
    if head[:4] in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe"):
        return parse_macho(path)
    return None


def validate(img: Image, anchor_map: dict) -> tuple[int, int]:
    """Returns (checked, failed)."""
    transferred = transfer_via_map(img, anchor_map)
    text_va, text = img.text()
    is64 = img.kind != "elf" or img.data[4] == 2
    index = build_xref_index(text_va, text, is64)
    checked = failed = 0
    for name, probe in CROSS_CHECKS.items():
        via_vtable = transferred.get(name, {}).get("rva")
        if not via_vtable:
            continue  # target not anchored; nothing to cross-check
        starts: set[int] = set()
        for sva in find_string_vas(img, probe):
            for xref in index.get(sva, []):
                fn = img.containing_function(xref)
                if fn is not None:
                    starts.add(fn - img.base)
        independent = {f"0x{s:x}" for s in starts}
        checked += 1
        if len(independent) == 1 and via_vtable in independent:
            print(f"[validate] {img.path.name}: {name} OK ({via_vtable})")
        else:
            print(
                f"[validate] {img.path.name}: {name} MISMATCH "
                f"vtable={via_vtable} string-xref={sorted(independent)}",
                file=sys.stderr,
            )
            failed += 1
    return checked, failed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--scan-root", type=Path, required=True)
    ap.add_argument("--map", dest="map_path", type=Path, required=True)
    args = ap.parse_args()

    anchor_map = json.loads(args.map_path.read_text(encoding="utf-8"))
    total_checked = total_failed = scanned = 0
    for path in sorted(args.scan_root.rglob("*")):
        if not path.is_file():
            continue
        if "steamclient" not in path.name.lower():
            continue
        img = load_image(path)
        if img is None:
            continue
        scanned += 1
        checked, failed = validate(img, anchor_map)
        total_checked += checked
        total_failed += failed

    print(f"[validate] {scanned} binary/binaries, {total_checked} cross-check(s), {total_failed} failure(s)")
    if not scanned:
        print("[validate] no 64-bit steamclient binaries found", file=sys.stderr)
        return 1
    return 1 if total_failed else 0


if __name__ == "__main__":
    sys.exit(main())