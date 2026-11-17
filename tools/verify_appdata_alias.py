#!/usr/bin/env python3
"""Static shape probe for the GetOrAddAppData <-> GetAppDataFromAppInfo alias.

The runtime hook (Hooks_Misc) expects CAppInfoCache::GetOrAddAppData with the
MSVC x64 signature (void* pCache /*rcx*/, uint32_t appId /*edx*/, bool bCreate
/*r8b*/) returning CAppData*. The derive pipeline publishes the CUserAppInfo
vtable-slot anchor under the name GetAppDataFromAppInfo. Before the alias is
trusted at runtime, this probe disassembles the derived function in the real
client binary and checks the calling-convention shape:

  HARD gates (exit 1 on failure):
    - arg2 (edx, direct or via stack spill) is consumed
    - arg3 (r8b/r8d, direct or via stack spill) is consumed
  Corroboration (warnings only):
    - direct call sites from the derived ProcessPendingLicenseUpdates /
      CheckAppOwnership bodies into the candidate (license-pipeline proximity)

Usage (Windows PE client only; Linux/macOS rely on the dynamic hook-smoke):
    python tools/verify_appdata_alias.py \
        --binary "C:/Program Files (x86)/Steam/steamclient64.dll" \
        --signatures-dir smoke-cache/signatures/windows-x64 [--json]

Requires: capstone (pip install capstone).
"""

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

CANDIDATE = "GetAppDataFromAppInfo"
RELATED = ["ProcessPendingLicenseUpdates", "CheckAppOwnership"]


def fail(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(2)


def parse_pe(data):
    """Returns (sections, pdata_entries, image_base). Minimal PE32+ reader."""
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
        fail("not a PE binary")
    coff = e_lfanew + 4
    num_sections = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic != 0x20B:
        fail("not PE32+ (64-bit); 32-bit clients are unsupported")
    image_base = struct.unpack_from("<Q", data, opt + 24)[0]
    num_dirs = struct.unpack_from("<I", data, opt + 108)[0]
    pdata_rva, pdata_size = 0, 0
    if num_dirs > 3:
        pdata_rva, pdata_size = struct.unpack_from("<II", data, opt + 112 + 3 * 8)
    sections = []
    sec_off = opt + opt_size
    for i in range(num_sections):
        off = sec_off + i * 40
        name = data[off:off + 8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
        sections.append({"name": name, "vaddr": vaddr, "vsize": vsize, "rawptr": rawptr, "rawsize": rawsize})

    def rva_to_off(rva):
        for s in sections:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rawsize"]):
                return s["rawptr"] + (rva - s["vaddr"])
        return None

    pdata = []
    if pdata_rva and pdata_size:
        off = rva_to_off(pdata_rva)
        if off is not None:
            for i in range(pdata_size // 12):
                b, e, u = struct.unpack_from("<III", data, off + i * 12)
                if b == 0:
                    break
                pdata.append((b, e))
    return sections, pdata, image_base, rva_to_off


def find_function(pdata, rva):
    for b, e in pdata:
        if b == rva:
            return b, e
    for b, e in pdata:
        if b <= rva < e:
            return b, e
    return None


def read_toml_rvas(signatures_dir, sha256):
    for path in sorted(Path(signatures_dir).glob("*.toml")):
        text = path.read_text(encoding="utf-8")
        if sha256 not in text:
            continue
        rvas = {}
        for m in re.finditer(r'^(\w+)\s*=\s*\{[^}]*rva\s*=\s*"(0x[0-9a-fA-F]+)"', text, re.M):
            rvas[m.group(1)] = int(m.group(2), 16)
        return path.name, rvas
    return None, {}


def reg_usage(insns):
    """Tracks direct edx/r8 usage plus stack-spill round trips."""
    direct = {"edx": False, "r8": False}
    spills = {}  # stack offset string -> arg family
    for ins in insns:
        ops = ins.op_str
        if re.search(r"\bedx\b", ops):
            direct["edx"] = True
        if re.search(r"\br8[bwd]?\b", ops):
            direct["r8"] = True
        m = re.match(r"mov\s+(byte|dword|qword)\s+ptr\s+\[rsp\s*\+\s*(0x[0-9a-f]+|\d+)\],\s*(edx|r8[bwd]?)$", ops)
        if m:
            fam = "edx" if m.group(3) == "edx" else "r8"
            spills[m.group(2)] = fam
    spilled = {"edx": False, "r8": False}
    for ins in insns:
        for slot, fam in spills.items():
            if re.search(r"\[rsp\s*\+\s*" + re.escape(slot) + r"\]", ins.op_str):
                # a reload/consumer of the spilled argument
                if not ins.mnemonic.startswith("mov") or ins.op_str.split(",")[0].find("[rsp") < 0:
                    spilled[fam] = True
    return {
        "edx": direct["edx"] or spilled["edx"],
        "r8": direct["r8"] or spilled["r8"],
        "edx_direct": direct["edx"],
        "r8_direct": direct["r8"],
    }


def call_sites(text_bytes, text_rva, candidate_rva):
    """Byte-scan E8 rel32 calls targeting the candidate (corroboration only)."""
    sites = []
    for i in range(len(text_bytes) - 5):
        if text_bytes[i] != 0xE8:
            continue
        rel = struct.unpack_from("<i", text_bytes, i + 1)[0]
        if text_rva + i + 5 + rel == candidate_rva:
            sites.append(text_rva + i)
    return sites


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", required=True)
    ap.add_argument("--signatures-dir", required=True)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    try:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    except ImportError:
        fail("capstone missing: pip install capstone")

    data = Path(args.binary).read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    toml_name, rvas = read_toml_rvas(args.signatures_dir, sha)
    if not toml_name:
        fail(f"no signature TOML matches binary sha256 {sha[:16]}... in {args.signatures_dir}")
    if CANDIDATE not in rvas:
        fail(f"{toml_name} does not publish {CANDIDATE}")
    cand_rva = rvas[CANDIDATE]

    sections, pdata, image_base, rva_to_off = parse_pe(data)
    extent = find_function(pdata, cand_rva)
    if extent is None:
        fail(f"no .pdata entry covers candidate RVA 0x{cand_rva:x}")
    begin, end = extent
    off = rva_to_off(begin)
    body = data[off:off + (end - begin)]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    insns = list(md.disasm(body, image_base + begin))
    usage = reg_usage(insns)

    related_extents = {}
    for name in RELATED:
        if name in rvas:
            ext = find_function(pdata, rvas[name])
            if ext:
                related_extents[name] = ext
    text = next((s for s in sections if s["name"] == ".text"), None)
    callers = []
    if text:
        tb = data[text["rawptr"]:text["rawptr"] + text["rawsize"]]
        for site in call_sites(tb, text["vaddr"], cand_rva):
            for name, (b, e) in related_extents.items():
                if b <= site < e:
                    callers.append({"related": name, "call_rva": hex(site)})

    hard_ok = usage["edx"] and usage["r8"]
    report = {
        "binary_sha256": sha,
        "toml": toml_name,
        "candidate": CANDIDATE,
        "candidate_rva": hex(cand_rva),
        "function_size": end - begin,
        "instruction_count": len(insns),
        "arg2_edx_consumed": usage["edx"],
        "arg3_r8_consumed": usage["r8"],
        "license_pipeline_callers": callers,
        "pass": hard_ok,
    }
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"candidate {CANDIDATE} @ {hex(cand_rva)} ({end - begin} bytes, {len(insns)} insns) from {toml_name}")
        print(f"  arg2 edx consumed: {usage['edx']} (direct={usage['edx_direct']})")
        print(f"  arg3 r8  consumed: {usage['r8']} (direct={usage['r8_direct']})")
        if callers:
            for c in callers:
                print(f"  corroboration: called from {c['related']} at {c['call_rva']}")
        else:
            print("  warn: no direct call from license-pipeline functions (not fatal)")
        print("PASS" if hard_ok else "FAIL: calling-convention shape mismatch - alias unsafe")
    return 0 if hard_ok else 1


if __name__ == "__main__":
    sys.exit(main())