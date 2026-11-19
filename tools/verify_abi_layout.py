#!/usr/bin/env python3
"""Verify the hooks' ABI layout anchors against the REAL client binaries.

Why this exists
---------------
`static_assert(offsetof(AppOwnership, bOwnsLicense) == 0x24)` in SteamTypes.h
proves our header agrees with itself - nothing more. The offsets themselves
were transcribed from the Windows client, and a hook that attaches at a
correctly derived RVA but writes the wrong field is invisible to every gate we
have: the client keeps running, the signature set publishes, the smoke test
reports "attached". (This already happened once to the sibling anchor: keeping
the stale CNetPacket payload offset made m_pubData read a 0xFFFFFFFF sentinel
and faulted UnpackRaw - crash-dump evidence, not a test failure.)

What this does
--------------
Disassemble the hooked function AT THE PUBLISHED RVA of an official client and
ask the client what it does with the offsets the hook assumes. AppOwnership is
the interesting case because the client FILLS the same struct: it has a dword
head (0x00..0x20) and a contiguous 1-byte tail (0x24..0x35), and the byte at
+0x24 is used as a boolean predicate on the ownership decision. Those are
properties of the layout, not of our header, and they are checkable.

Rules are arch-aware (x86-64 SysV, x86-64 MSVC, AArch64) and identical across
platforms on purpose: Windows is the reference where the product is proven to
work, so a rule that passes there and on Linux/macOS is differential evidence
that the layout did not drift per platform.

Coverage note: CNetPacket (+0x10 payload pointer / +0x18 length) CANNOT be
verified this way. The client reaches the payload only through the packet
object's own vtable (`mov rax,[pkt]; call qword ptr [rax+0x20]`) and the
concrete class is not statically identifiable, so any "pair at +0x10/+0x18"
hit in that function would be some unrelated struct. That anchor is instead
checked at RUNTIME, by Hooks_NetPacket validating the borrowed carrier against
the packet's own header fields and reporting counters through
core_status.json (gated by tools/smoke_status.py).

Usage:
  python tools/verify_abi_layout.py --binary <client> --toml <published.toml>
  python tools/verify_abi_layout.py --scan-root <dir> --signatures <dir> [--json out.json]

Exit status:
  0  no rule contradicted (and at least one anchor carried evidence)
  1  a rule was CONTRADICTED - this build's layout differs from the hook's
     assumption; publishing the RVA would hook the right function and write
     the wrong field
  2  nothing could be evaluated (missing input, no capstone, anchor absent).
     --strict turns that into 1 so CI cannot go green by checking nothing.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from derive_signatures import (  # noqa: E402
    CPU_ARM64,
    Image,
    _arm64_capstone,
    _capstone,
    arm64_function_instructions,
    function_instructions,
    macho_slice_cpus,
    parse_elf,
    parse_macho,
    parse_pe,
)

# ---------------------------------------------------------------------------
# The claim under test - mirrors the writes in src/Hook/Hooks_Package.cpp
# (HOOK_FUNC(CheckAppOwnership)). Field names are reported verbatim, so a hook
# change that starts writing another offset shows up as a missing rule.
# ---------------------------------------------------------------------------
OWNERSHIP_DWORD_FIELDS = {
    "PackageId": 0x00,
    "ReleaseState": 0x04,
    "SteamId32": 0x08,
    "MasterSubscriptionAppID": 0x0C,
    "TrialSeconds": 0x10,
    "ExistInPackageNums": 0x14,
    "TimeStamp": 0x1C,
    "TimeExpire": 0x20,
}
OWNERSHIP_BOOL_FIRST = 0x24  # bOwnsLicense
OWNERSHIP_BOOL_LAST = 0x35  # bFamilyShared
OWNERSHIP_BOOL_FIELDS = {"bOwnsLicense": 0x24, "bLicenseExpired": 0x25, "bFreeLicense": 0x28,
                         "bLicensePermanent": 0x2F, "bGuestPass": 0x30, "bBorrowed": 0x31,
                         "bFamilyShared": 0x35}
# char PurchaseCountryCode[4] - a 2-byte read at 0x18 plus byte reads at 0x1a/0x1b
# is what the client does with it; asserted as a shape, not per byte.
COUNTRY_FIELD = 0x18
# EAppReleaseState::Released, the value the hook writes at +0x04.
RELEASE_STATE_RELEASED = 4
BOOL_TAIL_MIN = 12  # of the 18 offsets in [0x24,0x35]


@dataclass
class AnchorReport:
    anchor: str
    function: str
    rva: int
    arch: str
    binary: str = ""
    confirmed: list[str] = field(default_factory=list)
    unobserved: list[str] = field(default_factory=list)
    contradicted: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    @property
    def status(self) -> str:
        if self.contradicted:
            return "CONTRADICTED"
        if self.confirmed:
            return "CONFIRMED" if not self.unobserved else "PARTIAL"
        return "UNOBSERVED"

    def to_json(self) -> dict:
        return {
            "binary": self.binary, "anchor": self.anchor, "function": self.function,
            "rva": f"0x{self.rva:x}", "arch": self.arch, "status": self.status,
            "confirmed": self.confirmed, "unobserved": self.unobserved,
            "contradicted": self.contradicted, "notes": self.notes,
        }


# ---------------------------------------------------------------------------
# instruction access abstraction: (mnemonic, op_str, memory accesses)
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class Access:
    base: str
    disp: int
    width: int  # bytes: 1 bool, 2 word, 4 dword, 8 qword, 16 vector (memset-style)


ARM_MEM = re.compile(r"\[(x\d+|w\d+|sp)(?:,\s*#(-?0x[0-9a-fA-F]+|-?\d+))?\]?")


def x86_accesses(ins) -> list[Access]:
    try:
        from capstone import CS_OP_MEM  # noqa: PLC0415
        from capstone.x86 import X86_REG_RIP  # noqa: PLC0415
    except ImportError:
        return []
    out: list[Access] = []
    for op in ins.operands:
        if op.type != CS_OP_MEM or op.mem.base == 0 or op.mem.base == X86_REG_RIP:
            continue
        base = ins.reg_name(op.mem.base)
        if not base or base in ("rsp", "rbp"):
            continue  # stack locals and frame slots are not our struct
        if op.mem.index != 0:
            continue  # indexed: array element, not a fixed field
        out.append(Access(base, op.mem.disp, op.size if op.size else 8))
    return out


def arm_accesses(ins) -> list[Access]:
    """AArch64: capstone detail is off, so parse op_str.

    Displacements print as HEX ("#0x24") and the accessed width comes from the
    mnemonic plus the value register: ldrb/strb -> 1, ldur/ldr with w -> 4,
    with x -> 8, stp/ldp -> 16 (two registers, i.e. a struct zero/copy).
    """
    if "[" not in ins.op_str:
        return []
    mn = ins.mnemonic
    if mn.startswith(("prfm", "ldxr", "stxr")):
        return []
    head = ins.op_str.split("[", 1)[0]
    if mn.startswith(("ldr", "ldur", "ldar", "ldtr", "str", "stur", "stlr", "sttr")):
        # sign/zero-extending forms name the SOURCE width: ldrsb/ldurb read a
        # byte, ldrsh/ldurh a halfword.
        if mn.endswith("b"):
            width = 1
        elif mn.endswith("h"):
            width = 2
        else:
            value = head.split(",")[0].strip().rstrip("!").strip()
            if "#" in value:  # not a memory form at all
                return []
            width = 4 if value.startswith("w") else 8
    elif mn in ("stp", "ldp", "ldpsw"):
        width = 16
    else:
        return []
    out: list[Access] = []
    for m in ARM_MEM.finditer(ins.op_str):
        base = m.group(1)
        if base.startswith("w"):
            base = "x" + base[1:]
        if base == "sp":
            continue
        disp = int(m.group(2) or "0", 0)
        out.append(Access(base, disp, width))
    return out


class Body:
    """Disassembled function plus the access index every rule reads from."""

    def __init__(self, insns: list, is_arm: bool) -> None:
        self.insns = insns
        self.is_arm = is_arm
        self.text = [f"{i.mnemonic} {i.op_str}" for i in insns]
        self.acc: list[list[Access]] = [
            arm_accesses(i) if is_arm else x86_accesses(i) for i in insns
        ]

    def offsets(self, regs: set[str], lo: int, hi: int) -> dict[int, set[int]]:
        seen: dict[int, set[int]] = defaultdict(set)
        for accesses in self.acc:
            for a in accesses:
                if a.base in regs and lo <= a.disp <= hi:
                    seen[a.disp].add(a.width)
        return seen

    def byte_offsets(self, regs: set[str], lo: int, hi: int) -> set[int]:
        return {o for o, ws in self.offsets(regs, lo, hi).items() if 1 in ws}


def struct_registers(body: Body, arg_reg: str, arch: str) -> set[str]:
    """arg_reg plus every register a prologue `mov` parks it into (chained)."""
    names = {arg_reg}
    if arch == "arm64":
        names |= {"x" + arg_reg[1:]} if arg_reg.startswith("w") else {"w" + arg_reg[1:]}
    for ins, accesses in zip(body.insns[:80], body.acc[:80]):
        if ins.mnemonic != "mov" or accesses:
            continue
        parts = [p.strip() for p in ins.op_str.split(",")]
        if len(parts) != 2:
            continue
        dst, src = parts[0], parts[1]
        if src in names or (arch != "arm64" and src in {r.lower() for r in names}):
            names.add(dst)
            if arch != "arm64" and dst.startswith("r"):
                names.add("e" + dst[1:] + "d")  # r14d <-> r14 storage
    return names


# ---------------------------------------------------------------------------
# rules
# ---------------------------------------------------------------------------
def check_ownership(binary: str, img: Image, rva: int, arch: str, shift: int = 0) -> AnchorReport:
    """`shift` displaces every assumed offset by N bytes.

    Only used by --negative-control: it must report a contradiction for a
    layout that is off by one filler byte, otherwise a green run means the
    rules cannot fail.
    """
    label = "AppOwnership" if not shift else f"AppOwnership+{shift}"
    rep = AnchorReport(label, "CheckAppOwnership", rva, arch, binary)
    is_arm = arch == "arm64"
    md = _arm64_capstone() if is_arm else _capstone()
    if md is None:
        rep.notes.append("capstone unavailable - rule not evaluated")
        return rep
    insns = arm64_function_instructions(img, rva, md) if is_arm else function_instructions(img, rva, md)
    if len(insns) < 50:
        rep.notes.append(f"body too short to judge ({len(insns)} insns)")
        return rep
    body = Body(insns, is_arm)

    bool_first = OWNERSHIP_BOOL_FIRST + shift
    bool_last = OWNERSHIP_BOOL_LAST + shift
    dword_off = {name: off + shift for name, off in OWNERSHIP_DWORD_FIELDS.items()}
    country = COUNTRY_FIELD + shift
    tail_range = (bool_first, bool_last)

    # bool CheckAppOwnership(void* pObj, AppId_t appId, AppOwnership* pOwn):
    # the third argument is the struct the hook writes.
    arg_reg = {"x64-sysv": "rdx", "x64-msvc": "r8", "arm64": "x2"}[arch]
    regs = struct_registers(body, arg_reg, arch)
    if not body.offsets(regs, 0, 0x40):
        rep.notes.append(f"no fixed-offset access on any alias of {arg_reg} ({sorted(regs)})")
        return rep

    # The client fills the struct through whichever alias it parks it in; that
    # register is the ownership pointer for every rule below.
    tail = body.byte_offsets(regs, *tail_range)
    widths = body.offsets(regs, 0x00, 0x48)
    dwords = {o for o, ws in widths.items() if 4 in ws}

    # R1: the 18-byte boolean tail, anchored at exactly +0x24.
    span = bool_last - bool_first + 1
    if len(tail) >= BOOL_TAIL_MIN and bool_first in tail and bool_last in tail:
        rep.confirmed.append(
            f"bool tail: {len(tail)}/{span} byte offsets in [0x{bool_first:x},0x{bool_last:x}]")
    elif len(tail) >= BOOL_TAIL_MIN:
        missing = [hex(x) for x in (bool_first, bool_last) if x not in tail]
        rep.contradicted.append(
            f"byte run in [0x{bool_first:x},0x{bool_last:x}] has {len(tail)} offsets but misses "
            f"{missing}: {sorted(hex(x) for x in tail)}")
    else:
        rep.unobserved.append(f"bool tail (only {len(tail)} byte offsets in [0x{bool_first:x},0x{bool_last:x}])")

    # R2: +0x24 is a predicate, i.e. it IS the ownership flag and not padding.
    pred = _predicate_use(body, regs, bool_first)
    if pred:
        rep.confirmed.append(f"bOwnsLicense +0x{bool_first:x} tested as a boolean: {pred}")
    else:
        rep.unobserved.append(f"bOwnsLicense +0x{bool_first:x} read and branched on")

    # R3: each dword field the hook writes is dword-sized for the client too.
    for name in ("PackageId", "ReleaseState", "ExistInPackageNums", "TimeStamp", "TimeExpire"):
        off = dword_off[name]
        if off in dwords:
            rep.confirmed.append(f"{name} +0x{off:x}: client accesses it as a dword")
        elif off not in widths:
            rep.unobserved.append(f"{name} +0x{off:x}: client never touches it in this function")
        else:
            rep.contradicted.append(
                f"{name} +0x{off:x}: client touches it, but only at width "
                f"{sorted(widths[off])} != 4")

    # R4: the release-state enum value. Strongest single fact: the client
    # compares the dword at +0x04 against 4 (Released) - the value the hook
    # writes. Absence is not a contradiction (macOS compares against a
    # register, and the check may live in a callee).
    rel = _cmp_against(body, regs, dword_off["ReleaseState"], RELEASE_STATE_RELEASED)
    if rel:
        rep.confirmed.append(f"ReleaseState +0x{dword_off['ReleaseState']:x} compared with Released(4): {rel}")
    else:
        rep.unobserved.append(f"ReleaseState +0x{dword_off['ReleaseState']:x} compared with Released(4)")

    # R5: PurchaseCountryCode[4] at +0x18 - a sub-dword access there means the
    # head really is 6 dwords + 4 chars, as our header declares.
    if country in widths and widths[country] & {1, 2}:
        rep.confirmed.append(f"PurchaseCountryCode +0x{country:x}: byte/word access (widths {sorted(widths[country])})")
    elif country in widths:
        rep.unobserved.append(f"PurchaseCountryCode +0x{country:x} only touched at width {sorted(widths[country])}")
    else:
        rep.unobserved.append(f"PurchaseCountryCode +0x{country:x} untouched here")

    # R6: bool fields the hook writes must be byte-addressed for the client.
    for name, off in (("bFreeLicense", OWNERSHIP_BOOL_FIELDS["bFreeLicense"] + shift),
                      ("bFamilyShared", OWNERSHIP_BOOL_FIELDS["bFamilyShared"] + shift)):
        if off in tail:
            rep.confirmed.append(f"{name} +0x{off:x}: byte field")
        elif off in widths:
            rep.contradicted.append(f"{name} +0x{off:x} is only touched at width {sorted(widths[off])}")
        else:
            rep.unobserved.append(f"{name} +0x{off:x} untouched here")
    return rep


def _predicate_use(body: Body, regs: set[str], off: int) -> str | None:
    """Return a snippet where [reg+off] is loaded as a byte and used as a test."""
    for i, accesses in enumerate(body.acc):
        if not any(a.base in regs and a.disp == off and a.width == 1 for a in accesses):
            continue
        direct = body.text[i]
        if body.is_arm:
            if re.match(r"(ldrb|strb) w\d+, \[(x\d+|sp), #0x%x\]" % off, direct):
                window = " ; ".join(body.text[i : i + 3])
                if re.search(r"\b(tbz|tbnz|cbz|cbnz)\b", window) or re.search(r"cmp w\d+, #[01]\b", window):
                    return window[:110]
            if direct.startswith("cmp byte"):
                return direct
        else:
            if direct.startswith("cmp byte ptr") and f"0x{off:x}]" in direct:
                return direct
            window = " ; ".join(body.text[i : i + 4])
            if re.search(r"test [a-z0-9]+, ?[a-z0-9]+", window) or "cmp byte ptr" in window:
                return window[:110]
    return None


def _cmp_against(body: Body, regs: set[str], off: int, value: int) -> str | None:
    for i, accesses in enumerate(body.acc):
        hit = None
        for a in accesses:
            if a.base in regs and a.disp == off and a.width == 4:
                hit = body.text[i]
                break
        if not hit:
            continue
        forms = (f"0x{value:x}", str(value))
        if body.is_arm:
            if hit.startswith("cmp") and any(f"#{f}" in hit for f in forms):
                return hit
            if hit.startswith(("ldr w", "ldur w")):
                dst = hit.split(",")[0].split()[1].strip()
                for nxt in body.text[i + 1 : i + 6]:
                    if nxt.startswith(f"cmp {dst}") and any(f"#{f}" in nxt for f in forms):
                        return f"{hit} ; {nxt}"
        else:
            if hit.startswith("cmp dword ptr") and any(hit.endswith(f", {f}") or f", {f}]" in hit or f", {f}" in hit.split("]")[-1] for f in forms):
                return hit
            if hit.startswith("mov ") and ("dword ptr" in hit):
                dst = hit.split(",")[0].split()[1].strip()
                for nxt in body.text[i + 1 : i + 6]:
                    if nxt.startswith("cmp " + dst) and any(nxt.endswith(", " + f) for f in forms):
                        return f"{hit} ; {nxt}"
    return None


# ---------------------------------------------------------------------------
def parse_toml_functions(path: Path) -> dict[str, dict[str, str]]:
    """Minimal TOML reader for the published signature files."""
    funcs: dict[str, dict[str, str]] = {}
    in_funcs = False
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("["):
            in_funcs = line == "[functions]"
            continue
        if not in_funcs or "=" not in line or line.startswith("#"):
            continue
        name, blob = line.split("=", 1)
        body_txt = blob.strip()
        entry: dict[str, str] = {}
        if body_txt.startswith("{") and body_txt.endswith("}"):
            for part in body_txt[1:-1].split(","):
                if "=" in part:
                    k, v = part.split("=", 1)
                    entry[k.strip()] = v.strip().strip('"')
        else:
            entry["rva"] = body_txt.strip('"')
        if "rva" in entry:
            funcs[name.strip()] = entry
    return funcs


def load_slices(path: Path) -> list[tuple[Image, str]]:
    """Every 64-bit slice of an image, tagged with the ABI used for arg regs."""
    head = path.open("rb").read(4)
    if head == b"\xca\xfe\xba\xbe":
        out: list[tuple[Image, str]] = []
        for cpu in macho_slice_cpus(path):
            out.append((parse_macho(path, cpu=cpu), "arm64" if cpu == CPU_ARM64 else "x64-sysv"))
        return out
    if head in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf"):
        img = parse_macho(path)
        return [(img, "arm64" if img.cpu_type == CPU_ARM64 else "x64-sysv")]
    if head == b"\x7fELF":
        with path.open("rb") as fh:
            ident = fh.read(5)
        if len(ident) < 5 or ident[4] != 2:
            raise ValueError(f"{path.name}: not a 64-bit ELF image")
        return [(parse_elf(path), "x64-sysv")]
    if head.startswith(b"MZ"):
        return [(parse_pe(path), "x64-msvc")]
    raise ValueError(f"{path.name}: unrecognised binary format")


def verify(binary: Path, toml: Path, shift: int = 0) -> list[AnchorReport]:
    funcs = parse_toml_functions(toml)
    reports: list[AnchorReport] = []
    for img, arch in load_slices(binary):
        entry = funcs.get("CheckAppOwnership")
        if not entry:
            continue
        key = "rva_arm64" if arch == "arm64" else "rva"
        if key not in entry:
            continue
        reports.append(check_ownership(binary.name, img, int(entry[key], 16), arch, shift))
    return reports


def toml_for(binary: Path, signatures: Path) -> Path | None:
    digest = hashlib.sha256()
    with binary.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            digest.update(chunk)
    short = digest.hexdigest()[:16]
    head = binary.open("rb").read(2)
    kind = {b"\x7fE": "elf", b"MZ": "pe"}.get(head, "macho")
    cand = signatures / {"pe": "windows-x64", "macho": "macos-universal", "elf": "linux-x64"}[kind] / f"{short}.toml"
    return cand if cand.exists() else None


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify hook ABI layout anchors against real client binaries")
    ap.add_argument("--binary", type=Path)
    ap.add_argument("--toml", type=Path)
    ap.add_argument("--scan-root", type=Path, help="directory of client binaries (bare or <sha16>-prefixed)")
    ap.add_argument("--signatures", type=Path, help="directory holding <platform>/<sha16>.toml")
    ap.add_argument("--json", type=Path, help="write a machine-readable report here")
    ap.add_argument("--strict", action="store_true", help="fail when no anchor could be evaluated")
    ap.add_argument("--negative-control", action="store_true",
                    help="also re-run every image with the assumed layout shifted by +1 byte; "
                         "each must be CONTRADICTED, proving the rules can actually fail")
    args = ap.parse_args()

    pairs: list[tuple[Path, Path]] = []
    if args.binary:
        if not args.toml:
            ap.error("--binary needs --toml")
        pairs = [(args.binary, args.toml)]
    elif args.scan_root and args.signatures:
        from harvest_signatures import find_steam_libraries  # noqa: PLC0415

        for binary in find_steam_libraries(args.scan_root):
            toml = toml_for(binary, args.signatures)
            if toml:
                pairs.append((binary, toml))
            else:
                print(f"[layout] {binary.name}: no published TOML, skipped")
    else:
        ap.error("use --binary/--toml or --scan-root/--signatures")

    if not pairs:
        print("[layout] nothing to verify")
        return 1 if args.strict else 2

    reports: list[AnchorReport] = []
    for binary, toml in pairs:
        reports.extend(verify(binary, toml))

    for rep in reports:
        print(f"[layout] {rep.binary} [{rep.arch}] {rep.anchor} via {rep.function}@0x{rep.rva:x}: {rep.status}")
        for line in rep.confirmed:
            print(f"[layout]   ok   {line}")
        for line in rep.unobserved:
            print(f"[layout]   n/a  {line}")
        for line in rep.contradicted:
            print(f"[layout]   BAD  {line}")
        for line in rep.notes:
            print(f"[layout]   note {line}")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps([r.to_json() for r in reports], indent=2), encoding="utf-8")

    bad = [r for r in reports if r.contradicted]
    evaluated = [r for r in reports if r.confirmed]
    status = 0
    if bad:
        print(f"[layout] FAIL: {len(bad)} anchor(s) contradict the hook layout")
        status = 1
    elif not evaluated:
        print("[layout] no anchor carried evidence")
        status = 1 if args.strict else 2
    else:
        print(f"[layout] OK: {len(evaluated)}/{len(reports)} anchor(s) carry layout evidence, 0 contradictions")

    if args.negative_control:
        # A gate that cannot fail is not a gate. Re-run with the assumed layout
        # displaced by one filler byte; that layout is wrong, so every image
        # MUST be rejected. Anything else means the rules match on vibes.
        accepted = []
        for binary, toml in pairs:
            for rep in verify(binary, toml, shift=1):
                flag = "rejected" if rep.status == "CONTRADICTED" else "ACCEPTED"
                print(f"[layout] control {rep.binary} [{rep.arch}] layout+1byte -> {rep.status} ({flag})")
                for line in rep.contradicted:
                    print(f"[layout]   because {line}")
                if rep.status != "CONTRADICTED":
                    accepted.append(rep)
        if accepted:
            print(f"[layout] FAIL: {len(accepted)} shifted layout(s) were not rejected - rules cannot fail")
            return 1
        print("[layout] control: the one-byte-off layout is rejected on every image checked")
    return status


if __name__ == "__main__":
    sys.exit(main())
