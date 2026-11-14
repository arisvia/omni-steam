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
    _capstone,
    build_xref_index,
    direct_call_targets,
    find_string_vas,
    function_instructions,
    in_text,
    license_producer,
    license_via_rtti,
    parse_elf,
    parse_macho,
    parse_pe,
    transfer_via_map,
)

# targets whose owning method also carries a unique, fully-qualified VProf
# literal - i.e. the ones reachable by BOTH mechanisms.
CROSS_CHECKS: dict[str, str] = {
    "RecvPkt": "CCMInterface::RecvPkt",
    "BBuildAndAsyncSendFrame": "CWebSocketConnection::BBuildAndAsyncSendFrame",
}

# GetPackageInfo is non-virtual, so the anchor map cannot name it. Its second
# mechanism is the `package_info_print` console command: the ConCommand ctor
# registration site passes the handler address, and the handler tail-calls
# GetPackageInfo(cache, packageId, /*accessToken=*/0) on the cache-miss path.
# This shares no code with the callee-fingerprint tier (which starts from
# CheckAppOwnership and matches the utlrbtree lookup shape).
PKG_CMD_HELP = "Dumps package info for packageID"


def load_image(path: Path) -> Image | None:
    head = path.read_bytes()[:8]
    if head[:4] == b"\x7fELF":
        if head[4] != 2:  # 64-bit only
            return None
        return parse_elf(path)
    if head[:4] in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe"):
        return parse_macho(path)
    if head[:2] == b"MZ":
        try:
            return parse_pe(path)
        except ValueError:
            return None  # 32-bit PE: no 64-bit runtime consumes its RVAs
    return None


def package_cmd_handlers(img: Image, md) -> set[int]:
    """Handler addresses registered for the package_info_print ConCommand.

    The ctor argument block loads the command name, the help text and the
    callback with RIP-relative LEAs. MSVC puts the callback in r8 (before the
    help text) and SysV in rdx (after it), so ordering is ABI-dependent; the
    invariant is that the callback is the ONLY LEA in the block whose target
    lands in executable code.
    """
    from capstone.x86 import X86_OP_MEM, X86_REG_RIP

    text_va, text = img.text()
    index = build_xref_index(text_va, text)
    sites = set()
    for sva in find_string_vas(img, PKG_CMD_HELP):
        for xref in index.get(sva, []):
            fn = img.containing_function(xref)
            if fn is not None:
                sites.add((fn - img.base, xref))
    out: set[int] = set()
    for fn, xref in sites:
        seq = function_instructions(img, fn, md)
        pos = next((i for i, ins in enumerate(seq) if ins.address == xref), None)
        if pos is None:
            continue
        lo = pos
        while lo > 0 and seq[lo - 1].mnemonic != "call":
            lo -= 1
        hi = pos
        while hi < len(seq) - 1 and seq[hi].mnemonic != "call":
            hi += 1
        for ins in seq[lo : hi + 1]:
            if ins.mnemonic != "lea":
                continue
            for op in ins.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    tgt = ins.address + ins.size + op.mem.disp
                    if in_text(img, tgt):
                        out.add(tgt - img.base)
    return out


def package_info_via_concommand(img: Image, md) -> set[int]:
    """GetPackageInfo RVAs reachable from the console command handler.

    The cache-miss path passes accessToken = 0, so the third argument register
    (r8 on MSVC, rdx on SysV) is zeroed right before the transfer.
    """
    zero3 = ("r8", "r8d") if img.kind == "pe" else ("rdx", "edx")
    out: set[int] = set()
    for handler in package_cmd_handlers(img, md):
        seq = function_instructions(img, handler, md)
        for n, ins in enumerate(seq):
            if ins.mnemonic not in ("call", "jmp") or not ins.op_str.startswith("0x"):
                continue
            try:
                tgt = int(ins.op_str, 16) - img.base
            except ValueError:
                continue
            for prev in seq[max(0, n - 6) : n]:
                ops = [o.strip() for o in prev.op_str.split(",")]
                if prev.mnemonic == "xor" and len(ops) == 2 and ops[0] == ops[1] and ops[0] in zero3:
                    out.add(tgt)
                elif prev.mnemonic == "mov" and len(ops) == 2 and ops[0] in zero3 and ops[1] == "0":
                    out.add(tgt)
    return out


def validate(img: Image, anchor_map: dict) -> tuple[int, int]:
    """Returns (checked, failed)."""
    from derive_signatures import pe_public_entry

    transferred = transfer_via_map(img, anchor_map)
    text_va, text = img.text()
    index = build_xref_index(text_va, text)
    md_pe = _capstone() if img.kind == "pe" else None

    def entry_of(rva: int) -> int:
        # A string xref can sit in an MSVC cold chunk whose .pdata entry is
        # not the public function start; compare entries, not chunks.
        if md_pe is None:
            return rva
        e = pe_public_entry(img, rva, md_pe)
        return rva if e is None else e

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
                    starts.add(entry_of(fn - img.base))
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
    md = _capstone()
    if md is not None:
        from derive_signatures import derive_callee_fingerprints

        fp, _ = derive_callee_fingerprints(img)
        via_fp = fp.get("GetPackageInfo", {}).get("rva")
        if via_fp:
            independent = {f"0x{r:x}" for r in package_info_via_concommand(img, md)}
            if not independent:
                # MSVC inlines the package creation path into the handler, so
                # this platform has no second mechanism; do not fail closed on
                # a check that cannot run.
                print(f"[validate] {img.path.name}: GetPackageInfo n/a (no ConCommand call site)")
            else:
                checked += 1
                if len(independent) == 1 and via_fp in independent:
                    print(f"[validate] {img.path.name}: GetPackageInfo OK ({via_fp})")
                else:
                    print(
                        f"[validate] {img.path.name}: GetPackageInfo MISMATCH "
                        f"fingerprint={via_fp} concommand={sorted(independent)}",
                        file=sys.stderr,
                    )
                    failed += 1
        # License trio: the producer chain (BParseResponseMessage VProf
        # literal -> call-site argument shape -> drain caller) must agree
        # with the RTTI gate chain (CUser vtables -> drain -> bool=false
        # callee). Different anchor families, different traversals.
        mark_p, pplu_p, _ = license_producer(img, md)
        mark_r, pplu_r, _ = license_via_rtti(img, md)
        for name, prod, gate in (
            ("MarkLicenseAsChanged", mark_p, mark_r),
            ("ProcessPendingLicenseUpdates", pplu_p, pplu_r),
        ):
            if prod is None and gate is None:
                print(f"[validate] {img.path.name}: {name} n/a (not derivable)")
                continue
            checked += 1
            if prod is not None and prod == gate:
                print(f"[validate] {img.path.name}: {name} OK ({hex(prod)})")
            else:
                print(
                    f"[validate] {img.path.name}: {name} MISMATCH "
                    f"producer={hex(prod) if prod is not None else None} "
                    f"rtti={hex(gate) if gate is not None else None}",
                    file=sys.stderr,
                )
                failed += 1
        # BUpdateLicenses: two distinct literals owned by the same function
        # (VProf budget + in-body assert message) must name the same start.
        vprof: set[int] = set()
        for probe in ("CUser::BUpdateLicenses", "BUpdateLicenses"):
            for sva in find_string_vas(img, probe):
                for xref in index.get(sva, []):
                    fn = img.containing_function(xref)
                    if fn is not None:
                        vprof.add(entry_of(fn - img.base))
            if vprof:
                break
        asserts: set[int] = set()
        for sva in find_string_vas(img, "pvecLicensesPrev not sorted"):
            for xref in index.get(sva, []):
                fn = img.containing_function(xref)
                if fn is not None:
                    asserts.add(entry_of(fn - img.base))
        if vprof and asserts:
            checked += 1
            if len(vprof) == 1 and vprof == asserts:
                print(f"[validate] {img.path.name}: BUpdateLicenses OK ({hex(next(iter(vprof)))})")
            else:
                print(
                    f"[validate] {img.path.name}: BUpdateLicenses MISMATCH "
                    f"vprof={sorted(hex(v) for v in vprof)} assert={sorted(hex(a) for a in asserts)}",
                    file=sys.stderr,
                )
                failed += 1
        # steamui FillInAppOverview: the direct VProf literal must agree with
        # the BuildCompleteAppOverviewChange caller fingerprint, on EVERY
        # slice of a fat binary (arm64 RVAs differ from x86_64).
        if "steamui" in img.path.name.lower():
            from derive_signatures import CPU_ARM64, derive, derive_ui_overview, macho_slice_cpus, parse_macho

            slices = [img]
            if img.kind == "macho" and img.cpu_type != CPU_ARM64 and CPU_ARM64 in macho_slice_cpus(img.path):
                slices.append(parse_macho(img.path, CPU_ARM64))
            for simg in slices:
                arch = "arm64" if simg.cpu_type == CPU_ARM64 else "x86_64"
                label = f"FillInAppOverview[{arch}]"
                res, _ = derive(simg)
                ui, _ = derive_ui_overview(simg, res)
                m1 = res.get("FillInAppOverview", {}).get("rva")
                m2 = ui.get("FillInAppOverview", {}).get("rva")
                if m1 is None or m2 is None:
                    # derive publishes FillInAppOverview only when BOTH
                    # mechanisms agree, so a build where one side is not
                    # derivable carries no published RVA to contradict.
                    print(f"[validate] {img.path.name}: {label} n/a (single mechanism only)")
                    continue
                checked += 1
                if m1 == m2:
                    print(f"[validate] {img.path.name}: {label} OK ({m1})")
                else:
                    print(
                        f"[validate] {img.path.name}: {label} MISMATCH literal={m1} fingerprint={m2}",
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
        name = path.name.lower()
        if "steamclient" not in name and "steamui" not in name:
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