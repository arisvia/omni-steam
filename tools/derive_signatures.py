#!/usr/bin/env python3
"""Semantic signature derivation from official Steam client binaries.

Third tier of the signature pipeline (after upstream sync and symbol harvest).
Works on STRIPPED binaries where neither symbol tables nor cross-version byte
patterns survive a client update, by anchoring on semantics that DO survive
recompilation:

  1. Distinctive string literals referenced by the target function
     (e.g. the "CheckAppOwnership" profiling string inside CheckAppOwnership).
  2. RIP-relative LEA/MOV cross-references from .text to those string VAs.
  3. Function-start recovery: walk back from the xref to the nearest
     padding-bounded 16-byte alignment, then require corroborating evidence
     (a direct E9/E8 call target, or a valid decodable prologue reaching the
     xref site).

Emits the same TOML schema PatternLoader consumes
(binary_sha256 + [functions] name = { rva = "...", source = "derived:..." }).
Zero third-party dependencies for ELF/Mach-O; pefile is optional (PE parsing
falls back to a self-contained header parser when absent).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

# Canonical target -> probe substrings. A probe must be unique enough that all
# of its in-image occurrences are plausibly owned by the target function or its
# immediate static callers; every occurrence is cross-checked, and only
# function starts corroborated by call-target/padding evidence are emitted.
STRING_PROBES: dict[str, list[str]] = {
    "CheckAppOwnership": ["CheckAppOwnership"],
    "BBuildAndAsyncSendFrame": ["BBuildAndAsyncSendFrame"],
    "SpawnProcess": ["SpawnProcess"],
    "FillInAppOverview": ["FillInAppOverview"],
    "RecvPkt": ["RecvPkt"],
}

TOML_TEMPLATE = """\
# OmniSteam derived signatures - DO NOT EDIT MANUALLY
# Derived semantically from the official binary listed below.
source_binary = "{source_name}"
binary_sha256 = "{sha256}"
kind = "{kind}"

[functions]
{function_entries}
"""
def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_reference_toml(path: Path) -> dict[str, int]:
    """name -> rva. Accepts both schemas:
    - OmniSteam harvested: [functions] with `name = { rva = "0x..." }`
    - steam-monitor upstream: `[0xHASH]` sections with `name`/`rva` fields."""
    text = path.read_text(encoding="utf-8")
    out: dict[str, int] = {}
    for m in re.finditer(r'^(\w+) = \{([^}]*)\}', text, re.M):
        rs = re.search(r'rva = "(0x[0-9a-fA-F]+)"', m.group(2))
        if rs:
            out[m.group(1)] = int(rs.group(1), 16)
    for m in re.finditer(r'^\[0x([0-9A-Fa-f]+)\][ \t]*$\n(.*?)(?=^\[|\Z)', text, re.M | re.S):
        body = m.group(2)
        ns = re.search(r'^name = "(\w+)"', body, re.M)
        rs = re.search(r'^rva = "(0x[0-9a-fA-F]+)"', body, re.M)
        if ns and rs:
            out[ns.group(1)] = int(rs.group(1), 16)
    return out



STRING_PROBES: dict[str, list[str]] = {
    "CheckAppOwnership": ["CheckAppOwnership"],
    "BBuildAndAsyncSendFrame": ["BBuildAndAsyncSendFrame"],
    "SpawnProcess": ["SpawnProcess"],
    "FillInAppOverview": ["FillInAppOverview"],
    "RecvPkt": ["RecvPkt"],
}

def deref_qword(img: Image, va: int) -> int | None:
    off = img.va_to_offset(va)
    if off is None or off + 8 > len(img.data):
        return None
    return struct.unpack_from("<Q", img.data, off)[0]


def read_cstr(img: Image, va: int, limit: int = 128) -> str:
    off = img.va_to_offset(va)
    if off is None:
        return ""
    end = img.data.find(b"\x00", off, off + limit)
    return img.data[off : end if end != -1 else off + limit].decode(errors="ignore")


def in_text(img: Image, va: int) -> bool:
    off = img.va_to_offset(va)
    if off is None:
        return False
    tva, tbytes = img.text()
    return tva <= va < tva + len(tbytes)


def typeinfo_name_at_vtable(img: Image, vtable_start: int) -> str:
    """Resolve the RTTI class name for a vtable. MSVC x64 stores an image-relative
    DWORD RVA to the TypeDescriptor inside the Complete Object Locator at
    vtable_start-8; Itanium (ELF/Mach-O) stores the type_info pointer directly at
    vtable_start-8 with the mangled name pointer at +8."""
    ptr = deref_qword(img, vtable_start - 8)
    if not ptr:
        return ""
    if img.kind == "pe":
        off = img.va_to_offset(ptr)
        if off is None:
            return ""
        td_rva = struct.unpack_from("<I", img.data, off + 12)[0]
        return read_cstr(img, img.base + td_rva + 16)
    name_ptr = deref_qword(img, ptr + 8)
    return read_cstr(img, name_ptr) if name_ptr else ""


def vtable_slots_for_rva(img: Image, rva: int, max_slots: int = 512) -> list[tuple[int, int, str]]:
    """Locate (typeinfo_name, slot_index, vtable_va) for every vtable entry
    pointing at base+rva."""
    img.base = img.base or min((s[0] for s in img.sections), default=0)
    func_va = img.base + rva
    needle = struct.pack("<Q", func_va)
    results: list[tuple[int, int, str]] = []
    pos = 0
    while True:
        pos = img.data.find(needle, pos)
        if pos == -1:
            break
        slot_va = img.offset_to_va(pos)
        pos += 1
        if slot_va is None:
            continue
        # Walk back while entries keep pointing into .text to find vtable start.
        start_va = slot_va
        for _ in range(max_slots):
            prev = deref_qword(img, start_va - 8)
            if prev is None or not in_text(img, prev):
                break
            start_va -= 8
        slot = (slot_va - start_va) // 8
        results.append((typeinfo_name_at_vtable(img, start_va), slot, hex(start_va)))
    return results


def find_vtables_for_typeinfo(img: Image, ti_name: str) -> list[int]:
    """All vtable_start VAs whose RTTI chain resolves to ti_name.
    MSVC: vtable_start-8 -> COL (separate object); COL+12 holds the image-relative
    RVA of the TypeDescriptor whose +16 is the '.?A...' name.
    Itanium: vtable_start-8 -> type_info directly; type_info+8 -> mangled name."""
    img.base = img.base or min((s[0] for s in img.sections), default=0)
    pos = img.data.find(ti_name.encode())
    if pos == -1:
        return []
    name_va = img.offset_to_va(pos)
    if name_va is None:
        return []
    vtables: list[int] = []
    if img.kind == "pe":
        td_va = name_va - 16
        pat = struct.pack("<I", td_va - img.base)
        p = 0
        while True:
            p = img.data.find(pat, p)
            if p == -1:
                break
            col_va = img.offset_to_va(p - 12)
            p += 1
            if col_va is None:
                continue
            qcol = struct.pack("<Q", col_va)
            q = 0
            while True:
                q = img.data.find(qcol, q)
                if q == -1:
                    break
                hit_va = img.offset_to_va(q)
                q += 1
                if hit_va is not None:
                    vtables.append(hit_va + 8)
    else:
        ti_va = name_va - 16
        q = struct.pack("<Q", ti_va)
        p = 0
        while True:
            p = img.data.find(q, p)
            if p == -1:
                break
            hit_va = img.offset_to_va(p)
            p += 1
            if hit_va is not None:
                vtables.append(hit_va + 8)
    return vtables


def emit_anchor_map(ref_img: Image, ref_rvas: dict[str, int]) -> dict:
    """Distill a reference version into a committable anchor map:
    {name: [[typeinfo_name, slot], ...]}. Contains no Valve bytes - only RTTI
    class names, slot indices and our canonical function names."""
    mapping: dict[str, list[list]] = {}
    for name, rva in sorted(ref_rvas.items()):
        for ti_name, slot, _vtable in vtable_slots_for_rva(ref_img, rva):
            if ti_name:
                mapping.setdefault(name, []).append([ti_name, slot])
    return {"schema": 1, "anchors": mapping}


def transfer_via_map(img: Image, anchor_map: dict) -> dict[str, dict]:
    """Transfer using a committed anchor map (no reference binary needed)."""
    refs: list[tuple[str, int, str]] = []
    for name, pairs in anchor_map.get("anchors", {}).items():
        for ti_name, slot in pairs:
            refs.append((name, int(slot), ti_name))
    return transfer_via_vtable(img, refs)


def transfer_via_vtable(img: Image, refs: list[tuple[str, int, str]]) -> dict[str, dict]:
    """Re-derive RVAs on a NEW binary from (name, slot, typeinfo_name) pairs
    observed on a reference version. Vtable ordering is stable across
    recompiles, so the same (typeinfo, slot) identifies the same method."""
    img.base = img.base or min((s[0] for s in img.sections), default=0)
    out: dict[str, dict] = {}
    votes: dict[str, dict[int, int]] = {}
    for name, slot, ti_name in refs:
        if not ti_name:
            continue
        for vtable_start in find_vtables_for_typeinfo(img, ti_name):
            fva = deref_qword(img, vtable_start + slot * 8)
            if fva and in_text(img, fva):
                votes.setdefault(name, {}).setdefault(fva - img.base, 0)
                votes[name][fva - img.base] += 1
    for name, tally in votes.items():
        best = max(tally.items(), key=lambda kv: kv[1])
        out[name] = {
            "rva": f"0x{best[0]:x}",
            "source": "derived:vtable-slot-transfer",
            "confidence": best[1],
        }
    return out


class Image:
    """Minimal VA <-> offset mapping plus section bytes for one binary."""

    def __init__(self, path: Path, kind: str) -> None:
        self.path = path
        self.kind = kind
        self.data = path.read_bytes()
        self.base = 0
        self.sections: list[tuple[int, int, int]] = []  # (va, size, file_offset)
        self.section_names: list[str] = []  # parallel to self.sections
        self.func_starts: list[int] = []  # sorted function-start VAs (exact boundary table)

    def finish(self) -> None:
        self.sections.sort()

    def section_by_name(self, name: str) -> tuple[int, int, int] | None:
        for n, s in zip(self.section_names, self.sections):
            if n == name:
                return s
        return None

    def containing_function(self, va: int) -> int | None:
        """Exact function start for va via the format's boundary table, or None."""
        import bisect

        if not self.func_starts:
            return None
        i = bisect.bisect_right(self.func_starts, va) - 1
        return self.func_starts[i] if i >= 0 else None

    def va_to_offset(self, va: int) -> int | None:
        for sva, size, off in self.sections:
            if sva <= va < sva + size:
                return off + (va - sva)
        return None

    def offset_to_va(self, off: int) -> int | None:
        for sva, size, soff in self.sections:
            if soff <= off < soff + size:
                return sva + (off - soff)
        return None

    def text(self) -> tuple[int, bytes]:
        """Executable code section: prefer the canonical '__text'/'.text' name,
        fall back to the largest section."""
        for want in ("__text", ".text"):
            s = self.section_by_name(want)
            if s:
                off = self.va_to_offset(s[0])
                if off is not None:
                    return s[0], self.data[off : off + s[1]]
        best = max(self.sections, key=lambda s: s[1])
        off = self.va_to_offset(best[0])
        return best[0], (self.data[off : off + best[1]] if off is not None else b"")


def parse_pe(path: Path) -> Image:
    img = Image(path, "pe")
    d = img.data
    pe_off = struct.unpack_from("<I", d, 0x3C)[0]
    assert d[pe_off : pe_off + 4] == b"PE\x00\x00", "bad PE signature"
    num_sections = struct.unpack_from("<H", d, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", d, pe_off + 20)[0]
    opt_off = pe_off + 24
    magic = struct.unpack_from("<H", d, opt_off)[0]
    img.base = struct.unpack_from("<Q" if magic == 0x20B else "<I", d, opt_off + 24)[0]
    sec_off = opt_off + opt_size
    for i in range(num_sections):
        off = sec_off + i * 40
        name = d[off : off + 8].rstrip(b"\x00").decode(errors="ignore")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", d, off + 8)
        if rawsize > 0:
            img.sections.append((img.base + vaddr, min(vsize, rawsize), rawptr))
            img.section_names.append(name)
    img.finish()
    # x64 exception table (.pdata): sorted RUNTIME_FUNCTION [begin, end, unwind].
    pdata = img.section_by_name(".pdata")
    if pdata:
        va, size, off = pdata
        n = size // 12
        img.func_starts = sorted(struct.unpack_from("<I", img.data, off + i * 12)[0] + img.base for i in range(n))
    return img


def parse_elf(path: Path) -> Image:
    img = Image(path, "elf")
    d = img.data
    is64 = d[4] == 2
    little = d[5] == 1
    end = "<" if little else ">"
    if is64:
        e_shoff = struct.unpack_from(end + "Q", d, 0x28)[0]
        e_shentsize = struct.unpack_from(end + "H", d, 0x3A)[0]
        e_shnum = struct.unpack_from(end + "H", d, 0x3C)[0]
        e_shstrndx = struct.unpack_from(end + "H", d, 0x3E)[0]
        shentsize = e_shentsize

        def shdr(i: int) -> tuple[int, int, int, int, int]:
            off = e_shoff + i * shentsize
            name, typ, flags, addr, offset, size = struct.unpack_from(end + "IIQQQQ", d, off)
            return name, typ, flags, addr, offset, size
    else:
        e_shoff = struct.unpack_from(end + "I", d, 0x20)[0]
        e_shentsize = struct.unpack_from(end + "H", d, 0x2E)[0]
        e_shnum = struct.unpack_from(end + "H", d, 0x30)[0]
        e_shstrndx = struct.unpack_from(end + "H", d, 0x32)[0]
        shentsize = e_shentsize

        def shdr(i: int) -> tuple[int, int, int, int, int, int]:
            off = e_shoff + i * shentsize
            name, typ, flags, addr, offset, size = struct.unpack_from(end + "IIIIII", d, off)
            return name, typ, flags, addr, offset, size

    str_sh = shdr(e_shstrndx)
    str_off, str_size = str_sh[4], str_sh[5]

    def sname(noff: int) -> str:
        endn = d.index(b"\x00", str_off + noff)
        return d[str_off + noff : endn].decode(errors="ignore")

    eh_hdr = None
    for i in range(e_shnum):
        name, typ, flags, addr, offset, size = shdr(i)
        if typ == 8:  # SHT_NOBITS
            img.section_names.append(sname(name))
            img.sections.append((addr, 0, offset))
            continue
        nm = sname(name)
        img.section_names.append(nm)
        if flags & 0x2 or size > (8 << 20):  # ALLOC or large (rodata)
            img.sections.append((addr, size, offset))
        if nm == ".eh_frame_hdr":
            eh_hdr = (addr, size, offset)
    img.finish()

    # .eh_frame_hdr binary-search table: exact function start list.
    # Layout: version(1) eh_frame_ptr_enc(1) fde_count_enc(1) table_enc(1)
    #         eh_frame_ptr(enc) fde_count(u32) pairs(int32 init_loc, int32 fde)
    # Encodings are almost universally pcrel/sdata4 for the pointer and
    # datarel/sdata4 for the table; verify and bail otherwise.
    if eh_hdr and eh_hdr[1] >= 12:
        va, size, off = eh_hdr
        ver = d[off]
        fde_count_enc, table_enc = d[off + 2], d[off + 3]
        if ver == 1 and fde_count_enc == 0x03 and table_enc == 0x3B:  # udata4 / datarel|sdata4
            fde_count = struct.unpack_from("<I", d, off + 8)[0]
            tbl = off + 12
            if fde_count and tbl + fde_count * 8 <= off + size:
                starts = set()
                for i in range(fde_count):
                    loc = struct.unpack_from("<i", d, tbl + i * 8)[0]
                    starts.add(va + loc)
                img.func_starts = sorted(starts)
    return img


def parse_macho(path: Path) -> Image:
    img = Image(path, "macho")
    d = img.data
    if d[:4] == b"\xca\xfe\xba\xbe":  # fat: use the largest slice
        nfat = struct.unpack_from(">I", d, 4)[0]
        best_off, best_size = 0, 0
        for i in range(nfat):
            _, _, off, size, _ = struct.unpack_from(">IIIII", d, 8 + i * 20)
            if size > best_size:
                best_off, best_size = off, size
        img.data = d[best_off : best_off + best_size]
        d = img.data
    ncmds = struct.unpack_from("<I", d, 16)[0]
    off = 32
    func_starts_data = None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", d, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", d, off + 16)
            if filesize > 0:
                img.sections.append((vmaddr, filesize, fileoff))
                img.section_names.append(d[off + 8 : off + 24].rstrip(b"\x00").decode(errors="ignore"))
            nsects = struct.unpack_from("<I", d, off + 64)[0]
            for s in range(nsects):
                so = off + 72 + s * 80
                sectname = d[so : so + 16].rstrip(b"\x00").decode(errors="ignore")
                saddr, ssize = struct.unpack_from("<QQ", d, so + 32)
                soffset = struct.unpack_from("<I", d, so + 48)[0]
                if ssize > 0:
                    img.sections.append((saddr, ssize, soffset))
                    img.section_names.append(sectname)
        elif cmd == 0x26:  # LC_FUNCTION_STARTS
            dataoff, datasize = struct.unpack_from("<II", d, off + 8)
            func_starts_data = d[dataoff : dataoff + datasize]
        off += size
    img.base = min((s[0] for s in img.sections), default=0)
    img.finish()
    if func_starts_data:
        # The blob is a plain ULEB128 delta stream (no count prefix); deltas
        # accumulate from the image base.
        va, starts = img.base, []
        pos = 0
        while pos < len(func_starts_data):
            delta = 0
            shift = 0
            while pos < len(func_starts_data):
                b = func_starts_data[pos]
                pos += 1
                delta |= (b & 0x7F) << shift
                shift += 7
                if not b & 0x80:
                    break
            va += delta
            if va:
                starts.append(va)
        img.func_starts = sorted(set(starts))
    return img


def find_string_vas(img: Image, probe: str) -> list[int]:
    """All VAs where the probe appears as a NUL-terminated (or delimited) literal."""
    needle = probe.encode()
    vas: list[int] = []
    pos = 0
    while True:
        pos = img.data.find(needle, pos)
        if pos == -1:
            break
        va = img.offset_to_va(pos)
        if va is not None:
            vas.append(va)
        pos += 1
    return vas


def find_xrefs(text_va: int, text: bytes, target_va: int, is64: bool) -> list[int]:
    """VAs of RIP-relative LEA/MOV instructions in .text referencing target_va."""
    xrefs: list[int] = []
    n = len(text)
    if is64:
        # REX.W lea/mov r64, [rip+disp32]: 48/4C 8D/8B modrm(mod=0,rm=5) disp32
        for i in range(n - 7):
            if text[i] in (0x48, 0x4C) and text[i + 1] in (0x8D, 0x8B) and (text[i + 2] & 0xC7) == 0x05:
                disp = struct.unpack_from("<i", text, i + 3)[0]
                if text_va + i + 7 + disp == target_va:
                    xrefs.append(text_va + i)
    else:
        # 32-bit: 8D/8B modrm(mod=0,rm=5) disp32
        for i in range(n - 6):
            if text[i] in (0x8D, 0x8B) and (text[i + 1] & 0xC7) == 0x05:
                disp = struct.unpack_from("<i", text, i + 2)[0]
                if text_va + i + 6 + disp == target_va:
                    xrefs.append(text_va + i)
    return xrefs


PAD_BYTES = {0xCC, 0x90, 0x00}
TERM_BYTES = {0xC3, 0xC9, 0xCB}  # ret / leave / retf


def recover_function_start(text_va: int, text: bytes, xref_va: int, is64: bool) -> int | None:
    """Walk back from the xref to a padding-bounded 16-byte alignment; require
    that some direct call in .text targets the candidate (strong evidence)."""
    off = xref_va - text_va
    scan_start = max(0, off - 0x4000)
    candidate = None
    a = off & ~0xF
    while a >= scan_start:
        prev = text[a - 1] if a > 0 else 0xCC
        if prev in PAD_BYTES or prev in TERM_BYTES:
            candidate = text_va + a
            break
        a -= 0x10
    if candidate is None:
        return None

    # Corroborate: at least one direct near call to the candidate anywhere in .text.
    coff = candidate - text_va
    for i in range(scan_start, n := len(text) - 5):
        if text[i] == 0xE8:
            rel = struct.unpack_from("<i", text, i + 1)[0]
            if text_va + i + 5 + rel == candidate:
                return candidate
    # Fallback evidence: candidate itself decodes as a common prologue.
    b = text[coff : coff + 4]
    if b[:3] == b"\x48\x89\x5C" or b[:2] in (b"\x55\x8B", b"\x55\x48") or b[0] == 0x55 or b[:3] == b"\x48\x83\xEC":
        return candidate
    return None


def derive(img: Image) -> tuple[dict[str, dict], list[dict]]:
    is64 = img.kind != "elf" or img.data[4] == 2
    text_va, text = img.text()
    resolved: dict[str, dict] = {}
    details: list[dict] = []
    for name, probes in STRING_PROBES.items():
        starts: set[int] = set()
        probe_report = []
        for probe in probes:
            for sva in find_string_vas(img, probe):
                xrefs = find_xrefs(text_va, text, sva, is64)
                probe_report.append({"probe": probe, "string_va": hex(sva), "xrefs": len(xrefs)})
                for x in xrefs:
                    # Exact boundary table (eh_frame_hdr/.pdata/LC_FUNCTION_STARTS)
                    # when available; padding-walk heuristic as fallback.
                    start = img.containing_function(x) or recover_function_start(text_va, text, x, is64)
                    if start is not None:
                        starts.add(start)
        if len(starts) == 1:
            rva = starts.pop() - img.base
            resolved[name] = {"rva": f"0x{rva:x}", "source": "derived:string-xref"}
        elif len(starts) > 1:
            details.append({"target": name, "status": "ambiguous", "candidates": len(starts)})
        if probe_report:
            details.append({"target": name, "probes": probe_report, "resolved": name in resolved})
    return resolved, details


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--kind", choices=["pe", "elf", "macho"], help="override format detection")
    parser.add_argument("--reference-binary", type=Path,
                        help="anchored reference version for vtable slot transfer")
    parser.add_argument("--reference-toml", type=Path,
                        help="signature TOML (name -> rva) matching --reference-binary")
    parser.add_argument("--map", type=Path,
                        help="committed anchor map JSON (no reference binary needed)")
    parser.add_argument("--emit-map", type=Path,
                        help="with --reference-binary/--reference-toml: distill the "
                             "reference into a committable anchor map JSON and exit")
    args = parser.parse_args()

    data_head = args.binary.read_bytes()[:4]
    kind = args.kind or (
        "pe" if data_head.startswith(b"MZ") else "elf" if data_head.startswith(b"\x7fELF") else "macho"
    )
    parser_cls = {"pe": parse_pe, "elf": parse_elf, "macho": parse_macho}[kind]

    # Map emission mode: distill reference into a committable JSON and exit.
    if args.emit_map:
        if not (args.reference_binary and args.reference_toml):
            parser.error("--emit-map requires --reference-binary and --reference-toml")
        ref_img = parser_cls(args.reference_binary)
        anchor_map = emit_anchor_map(ref_img, parse_reference_toml(args.reference_toml))
        args.emit_map.write_text(json.dumps(anchor_map, indent=1), encoding="utf-8")
        total = sum(len(v) for v in anchor_map["anchors"].values())
        print(f"[derive] anchor map written: {args.emit_map} ({len(anchor_map['anchors'])} names, {total} slots)")
        return 0

    img = parser_cls(args.binary)


    digest = sha256_file(args.binary)
    resolved: dict[str, dict] = {}
    details: list = []

    if args.map:
        anchor_map = json.loads(args.map.read_text(encoding="utf-8"))
        resolved = transfer_via_map(img, anchor_map)
        details.append({"mode": "committed-anchor-map", "entries": len(anchor_map.get("anchors", {}))})
        print(f"[derive] transferred {len(resolved)} function(s) from committed anchor map")
    elif args.reference_binary and args.reference_toml:
        ref_img = parser_cls(args.reference_binary)
        ref_rvas = parse_reference_toml(args.reference_toml)
        slot_map: list[tuple[str, int, str]] = []
        for name, rva in sorted(ref_rvas.items()):
            for ti_name, slot, vtable_va in vtable_slots_for_rva(ref_img, rva):
                slot_map.append((name, slot, ti_name))
                details.append({"ref": name, "rva": hex(rva), "vtable": vtable_va, "slot": slot, "typeinfo": ti_name})
        print(f"[derive] reference anchors: {len(ref_rvas)}, vtable slots observed: {len(slot_map)}")
        resolved = transfer_via_vtable(img, slot_map)
        print(f"[derive] transferred {len(resolved)} function(s) to target")

    string_resolved, string_details = derive(img)
    for name, meta in string_resolved.items():
        resolved.setdefault(name, meta)
    details.extend(string_details)
    print(f"[derive] {args.binary.name} sha256={digest[:16]} kind={kind}: {len(resolved)} function(s) derived")


    if not resolved:
        print("[derive] nothing resolved; skipping TOML (repo-noise rule)")
        return 0

    out_dir = args.out
    out_dir.mkdir(parents=True, exist_ok=True)
    entries = "\n".join(f'{n} = {{ rva = "{m["rva"]}", source = "{m["source"]}" }}' for n, m in sorted(resolved.items()))
    toml_path = out_dir / f"{digest[:16]}.toml"
    toml_path.write_text(
        TOML_TEMPLATE.format(source_name=args.binary.name, sha256=digest, kind=kind, function_entries=entries),
        encoding="utf-8",
    )
    (out_dir / f"report-derive-{digest[:16]}.json").write_text(
        json.dumps({"binary": args.binary.name, "sha256": digest, "resolved": resolved, "details": details}, indent=2),
        encoding="utf-8",
    )
    print(f"[derive] wrote {toml_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
