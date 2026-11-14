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
  4. Callee-fingerprint transfer: anchor a caller by string xref, then pick
     the direct callee whose decoded instruction sequence matches a structural
     fingerprint of the target (used for non-virtual members such as
     CPackageInfoCache::GetPackageInfo, which no vtable slot can reach).

Emits the same TOML schema PatternLoader consumes
(binary_sha256 + [functions] name = { rva = "...", source = "derived:..." }).
Zero third-party dependencies for the string/vtable tiers; pefile is optional
(PE parsing falls back to a self-contained header parser) and capstone is
optional (only the callee-fingerprint tier needs it, and is skipped without it).
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
# Canonical target -> probe substrings, most specific first.
#
# Valve's VProf budget literals are FULLY QUALIFIED ("CCMInterface::RecvPkt").
# A bare "RecvPkt" probe only ever matches inside a longer literal, so it never
# lands on a string boundary and yields zero cross-references - that was the
# real reason RecvPkt/BBuildAndAsyncSendFrame "could not be derived" on
# Linux/macOS, not any missing relocation support. Qualified probes are also
# far more selective: exactly one function owns each budget name.
STRING_PROBES: dict[str, list[str]] = {
    # MSVC keeps this literal qualified ("CUser::CheckAppOwnership") while
    # GCC/Clang emit it bare, so the qualified spelling must be tried first:
    # the bare substring lands 7 bytes into the Windows literal and never
    # matches the LEA target.
    "CheckAppOwnership": ["CUser::CheckAppOwnership", "CheckAppOwnership"],
    "BBuildAndAsyncSendFrame": [
        "CWebSocketConnection::BBuildAndAsyncSendFrame",
        "BBuildAndAsyncSendFrame",
    ],
    "SpawnProcess": ["SpawnProcess"],
    # macOS steamui.dylib keeps the qualified VProf budget literal with exactly
    # one RIP xref; the bare substring lands 0x17 bytes into it and never
    # matches a LEA target. MSVC steamui.dll has zero xrefs (pointer table),
    # so that platform still reports n/a rather than a wrong RVA.
    "FillInAppOverview": ["CSteamUIAppController::FillInAppOverview", "FillInAppOverview"],
    "RecvPkt": ["CCMInterface::RecvPkt"],
    # License subsystem entry points. These are NOT the three Package-0
    # targets (CUtlMemoryGrow / MarkLicenseAsChanged /
    # ProcessPendingLicenseUpdates) - they are the anchored callers whose
    # neighborhoods those targets live in. Windows keeps the qualified
    # VProf spelling, GCC/Clang emit the bare budget name.
    "BUpdateLicenses": ["CUser::BUpdateLicenses", "BUpdateLicenses"],
    "OnLicensesUpdated": ["CCompatManager::OnLicensesUpdated"],
    # Producer anchor for the license trio (tier 5). MSVC keeps the qualified
    # VProf spelling (the bare literal has zero RIP xrefs there - pointer
    # table); GCC/Clang emit the bare budget name with exactly one xref.
    "BParseResponseMessage": [
        "CPICSJobRequestProductInfoUpdate::BParseResponseMessage",
        "BParseResponseMessage",
    ],
    # steamui: caller anchor for the FillInAppOverview dual chain (tier 6).
    # The literal carries a " - app" VProf suffix; the bare name also occurs
    # in an unreferenced log string, so only the full spelling is probed.
    "BuildCompleteAppOverviewChange": ["CSteamUIAppController::BuildCompleteAppOverviewChange - app"],
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



def deref_qword(img: Image, va: int) -> int | None:
    """Read a pointer-sized slot.

    On PE the stored qword IS the preferred VA. On PIE ELF and on Mach-O the
    stored bytes are NOT the pointer: ELF keeps zero and carries the real
    target in the R_*_RELATIVE addend, and Mach-O keeps a preferred VA that
    only becomes meaningful once the rebase stream marks this slot as a
    pointer. Reading raw bytes there yields 0 (ELF) or arbitrary data
    (Mach-O), which is exactly why vtable/RTTI traversal used to find nothing
    on Linux/macOS. Prefer the relocation index whenever the format has one.
    """
    if img.reloc_based:
        return img.reloc_ptrs.get(va)
    off = img.va_to_offset(va)
    if off is None or off + 8 > len(img.data):
        return None
    return struct.unpack_from("<Q", img.data, off)[0]


def pointer_sites(img: Image, target_va: int) -> list[int]:
    """Every slot VA holding a pointer to target_va.

    Relocation-backed formats answer from the index; PE falls back to scanning
    the image for the absolute qword (its pointers are stored unencoded).
    """
    if img.reloc_based:
        return img.ptr_sites.get(target_va, [])
    needle = struct.pack("<Q", target_va)
    out: list[int] = []
    pos = 0
    while True:
        pos = img.data.find(needle, pos)
        if pos == -1:
            break
        va = img.offset_to_va(pos)
        pos += 1
        if va is not None:
            out.append(va)
    return out


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
    pointing at base+rva. Uses the relocation index on ELF/Mach-O so the
    search finds slots whose file bytes are zero."""
    img.base = img.base or min((s[0] for s in img.sections), default=0)
    func_va = img.base + rva
    results: list[tuple[int, int, str]] = []
    for slot_va in pointer_sites(img, func_va):
        # Prefer the RTTI-anchored start: walk back only while the preceding
        # entry is itself a code pointer, then snap to the nearest vtable whose
        # type_info resolves, so a neighbouring vtable cannot absorb the slot.
        start_va = slot_va
        for _ in range(max_slots):
            prev = deref_qword(img, start_va - 8)
            if prev is None or not in_text(img, prev):
                break
            start_va -= 8
        name = typeinfo_name_at_vtable(img, start_va)
        if not name:
            # The walk-back overshot into a preceding vtable (common: vtables
            # are laid out back to back). Re-anchor on a real RTTI boundary.
            probe = start_va
            for _ in range(max_slots):
                candidate = typeinfo_name_at_vtable(img, probe)
                if candidate and probe <= slot_va:
                    start_va, name = probe, candidate
                    break
                probe += 8
        slot = (slot_va - start_va) // 8
        results.append((name, slot, hex(start_va)))
    return results


def itanium_name_candidates(ti_name: str) -> list[str]:
    """Mangled Itanium type_info name spellings for an anchor's class name.

    Anchor maps are recorded from whichever platform produced the reference
    (usually the MSVC '.?AVCFoo@@' spelling). Itanium spells the same class as
    a length-prefixed component - '4CFoo' at top level, or as the trailing
    component of a nested name 'N3Bar4CFooE'. Searching for the
    length-prefixed leaf covers both without needing a demangler.
    """
    cls = ti_name
    if cls.startswith(".?A"):
        cls = cls[4:]  # .?AV / .?AU / .?AT ...
        cls = cls.split("@")[0]
    if not cls:
        return []
    if re.fullmatch(r"\d+.*", cls):
        return [cls]  # already mangled
    return [f"{len(cls)}{cls}"]


def find_vtables_for_typeinfo(img: Image, ti_name: str) -> list[int]:
    """All vtable_start VAs whose RTTI chain resolves to ti_name.

    MSVC: vtable_start-8 -> COL (separate object); COL+12 holds the
    image-relative RVA of the TypeDescriptor whose +16 is the '.?A...' name.
    Itanium: vtable_start-8 -> type_info directly; type_info+8 -> the mangled
    name string. Both hops are *pointers*, so on PIE ELF / Mach-O they are
    resolved through the relocation index rather than raw image bytes.
    """
    img.base = img.base or min((s[0] for s in img.sections), default=0)
    vtables: list[int] = []
    if img.kind == "pe":
        pos = img.data.find(ti_name.encode())
        if pos == -1:
            return []
        name_va = img.offset_to_va(pos)
        if name_va is None:
            return []
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
            for hit_va in pointer_sites(img, col_va):
                vtables.append(hit_va + 8)
        return vtables

    for mangled in itanium_name_candidates(ti_name):
        needle = mangled.encode() + b"\x00"
        pos = 0
        while True:
            pos = img.data.find(needle, pos)
            if pos == -1:
                break
            name_va = img.offset_to_va(pos)
            pos += 1
            if name_va is None:
                continue
            # name string <- type_info+8 <- vtable_start-8
            for name_slot in pointer_sites(img, name_va):
                ti_va = name_slot - 8
                for ti_slot in pointer_sites(img, ti_va):
                    vtables.append(ti_slot + 8)
    return sorted(set(vtables))


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
        self.cpu_type = 0  # Mach-O slice CPU type (0 for ELF/PE)
        self.data = path.read_bytes()
        self.base = 0
        self.sections: list[tuple[int, int, int]] = []  # (va, size, file_offset)
        self.section_names: list[str] = []  # parallel to self.sections
        self.func_starts: list[int] = []  # sorted function-start VAs (exact boundary table)
        # Relocation-resolved pointer layer. `reloc_based` formats (PIE ELF,
        # Mach-O) do not store usable pointers in the file image, so every
        # pointer read goes through this index instead of raw bytes.
        self.reloc_based = False
        self.reloc_ptrs: dict[int, int] = {}  # slot VA -> target VA
        self.ptr_sites: dict[int, list[int]] = {}  # target VA -> [slot VAs]

    def index_pointer(self, slot_va: int, target_va: int) -> None:
        self.reloc_ptrs[slot_va] = target_va
        self.ptr_sites.setdefault(target_va, []).append(slot_va)

    def finish(self) -> None:
        # Two ordering constraints, in priority order:
        #  1. NAMED sections before unnamed segment/PT_LOAD ranges. A segment
        #     spans the whole mapping, so sorting it first would swallow every
        #     va_to_offset()/offset_to_va() lookup and rebase VAs onto the
        #     segment base instead of the owning section.
        #  2. Narrower spans first, so a section beats any wider section that
        #     contains it (e.g. .rodata.str inside .rodata).
        paired = sorted(
            zip(self.section_names, self.sections),
            key=lambda ns: (ns[0] == "", ns[1][1], ns[1][0]),
        )
        self.section_names = [n for n, _ in paired]
        self.sections = [s for _, s in paired]

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
    machine = struct.unpack_from("<H", d, pe_off + 4)[0]
    if machine != 0x8664 or magic != 0x20B:
        raise ValueError(f"{path.name}: not a 64-bit PE image (64-bit targets only)")
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
    """Parse a 64-bit ELF image.

    OmniSteam targets 64-bit platforms only, so ELF32 is rejected outright
    rather than half-parsed into RVAs that no runtime would consume.
    """
    img = Image(path, "elf")
    d = img.data
    if d[4] != 2:  # EI_CLASS != ELFCLASS64
        raise ValueError(f"{path.name}: not a 64-bit ELF image (64-bit targets only)")
    end = "<" if d[5] == 1 else ">"
    e_shoff = struct.unpack_from(end + "Q", d, 0x28)[0]
    shentsize = struct.unpack_from(end + "H", d, 0x3A)[0]
    e_shnum = struct.unpack_from(end + "H", d, 0x3C)[0]
    e_shstrndx = struct.unpack_from(end + "H", d, 0x3E)[0]

    def shdr(i: int) -> tuple[int, int, int, int, int, int]:
        off = e_shoff + i * shentsize
        name, typ, flags, addr, offset, size = struct.unpack_from(end + "IIQQQQ", d, off)
        return name, typ, flags, addr, offset, size

    str_sh = shdr(e_shstrndx)
    str_off, str_size = str_sh[4], str_sh[5]

    def sname(noff: int) -> str:
        endn = d.index(b"\x00", str_off + noff)
        return d[str_off + noff : endn].decode(errors="ignore")

    eh_hdr = None
    for i in range(e_shnum):
        name, typ, flags, addr, offset, size = shdr(i)
        nm = sname(name)
        if nm == ".eh_frame_hdr":
            eh_hdr = (addr, size, offset)
        if typ == 8:  # SHT_NOBITS (.bss): occupies VA space, carries no bytes
            continue
        if not (flags & 0x2) and size <= (8 << 20):  # non-ALLOC and small
            continue
        # Names and sections MUST stay index-aligned: finish() sorts them as
        # pairs, so appending a name without its section desynchronises every
        # later name lookup (that is how '.text' resolved to a 64-byte stub).
        img.section_names.append(nm)
        img.sections.append((addr, size, offset))
    img.finish()

    # Program headers give the authoritative VA<->offset mapping. Section
    # headers alone can disagree at boundaries (and .bss carries no bytes),
    # so prefer PT_LOAD ranges and keep sections only for name lookups.
    e_phoff = struct.unpack_from(end + "Q", d, 0x20)[0]
    e_phentsize = struct.unpack_from(end + "H", d, 0x36)[0]
    e_phnum = struct.unpack_from(end + "H", d, 0x38)[0]
    loads: list[tuple[int, int, int]] = []
    for i in range(e_phnum):
        po = e_phoff + i * e_phentsize
        p_type = struct.unpack_from(end + "I", d, po)[0]
        if p_type != 1:  # PT_LOAD
            continue
        p_offset, p_vaddr = struct.unpack_from(end + "QQ", d, po + 8)
        p_filesz = struct.unpack_from(end + "Q", d, po + 32)[0]
        if p_filesz:
            loads.append((p_vaddr, p_filesz, p_offset))
    if loads:
        named = list(zip(img.section_names, img.sections))
        img.sections = sorted(loads)
        img.section_names = ["" for _ in loads]
        # Re-attach names for the sections the rest of the tool looks up.
        for nm, sec in named:
            if nm in (".text", ".rodata", ".data.rel.ro", ".data", ".eh_frame_hdr"):
                img.sections.append(sec)
                img.section_names.append(nm)
        img.finish()

    # R_X86_64_RELATIVE / R_AARCH64_RELATIVE addends ARE the link-time target
    # VAs of every vtable slot, type_info pointer and static-object pointer.
    # The file bytes at those slots are zero, so this index is the only way to
    # traverse RTTI on a PIE shared object.
    rel_types = {8, 1027}  # R_X86_64_RELATIVE / R_AARCH64_RELATIVE
    for i in range(e_shnum):
        _name, typ, _flags, _addr, offset, size = shdr(i)
        if typ != 4:  # SHT_RELA
            continue
        for ro in range(offset, offset + size - 24 + 1, 24):
            r_offset, r_info, r_addend = struct.unpack_from(end + "QQq", d, ro)
            if (r_info & 0xFFFFFFFF) in rel_types:
                img.index_pointer(r_offset, r_addend)
    if img.reloc_ptrs:
        img.reloc_based = True

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


def _uleb(buf: bytes, pos: int) -> tuple[int, int]:
    val = shift = 0
    while pos < len(buf):
        b = buf[pos]
        pos += 1
        val |= (b & 0x7F) << shift
        if not b & 0x80:
            break
        shift += 7
    return val, pos


def _parse_macho_rebase(img: Image, segments: list[tuple[int, int, int]], off: int, size: int) -> None:
    """Walk the LC_DYLD_INFO rebase opcode stream.

    Classic rebase keeps the preferred VA in the file and only adds the slide
    at load time, so the stored qword is already the target. The stream tells
    us WHICH slots are pointers, which is what separates a real vtable entry
    from an unrelated qword that happens to look like code.
    """
    d = img.data
    blob = d[off : off + size]
    pos = seg_index = seg_off = 0
    while pos < len(blob):
        op = blob[pos]
        pos += 1
        imm, high = op & 0x0F, op & 0xF0
        if high == 0x00:  # DONE
            break
        if high == 0x10:  # SET_TYPE_IMM
            continue
        if high == 0x20:  # SET_SEGMENT_AND_OFFSET_ULEB
            seg_index = imm
            seg_off, pos = _uleb(blob, pos)
        elif high == 0x30:  # ADD_ADDR_ULEB
            add, pos = _uleb(blob, pos)
            seg_off += add
        elif high == 0x40:  # ADD_ADDR_IMM_SCALED
            seg_off += imm * 8
        elif high in (0x50, 0x60, 0x70, 0x80):
            if high == 0x50:  # DO_REBASE_IMM_TIMES
                count, skip = imm, 0
            elif high == 0x60:  # DO_REBASE_ULEB_TIMES
                count, pos = _uleb(blob, pos)
                skip = 0
            elif high == 0x70:  # DO_REBASE_ADD_ADDR_ULEB
                count = 1
                extra, pos = _uleb(blob, pos)
                skip = extra
            else:  # DO_REBASE_ULEB_TIMES_SKIPPING_ULEB
                count, pos = _uleb(blob, pos)
                skip, pos = _uleb(blob, pos)
            if seg_index >= len(segments):
                break
            base = segments[seg_index][0]
            for _ in range(count):
                slot_va = base + seg_off
                file_off = img.va_to_offset(slot_va)
                if file_off is not None and file_off + 8 <= len(d):
                    target = struct.unpack_from("<Q", d, file_off)[0]
                    if target:
                        img.index_pointer(slot_va, target)
                seg_off += 8 + (skip if high == 0x70 else 0)
                if high == 0x80:
                    seg_off += skip
        else:
            break


def _parse_macho_chained(img: Image, segments: list[tuple[int, int, int]], off: int, size: int) -> None:
    """Decode LC_DYLD_CHAINED_FIXUPS pointer chains.

    Modern dylibs pack each pointer into a bitfield (target + next + bind),
    so the raw qword is NOT the target. Only the rebase variants are useful
    here; binds resolve to external symbols and are skipped.
    """
    d = img.data
    if size < 36:
        return
    _ver, starts_off, _imports_off, _sym_off = struct.unpack_from("<IIII", d, off)
    seg_count = struct.unpack_from("<I", d, off + starts_off)[0]
    text_base = segments[0][0] if segments else 0
    for s in range(min(seg_count, len(segments))):
        info_off = struct.unpack_from("<I", d, off + starts_off + 4 + s * 4)[0]
        if not info_off:
            continue
        b = off + starts_off + info_off
        _sz, page_size, ptr_format = struct.unpack_from("<IHH", d, b)
        seg_offset = struct.unpack_from("<Q", d, b + 8)[0]
        page_count = struct.unpack_from("<H", d, b + 18)[0]
        if not page_size:
            continue
        seg_base = segments[s][0]
        for p in range(page_count):
            start = struct.unpack_from("<H", d, b + 20 + p * 2)[0]
            if start == 0xFFFF:  # DYLD_CHAINED_PTR_START_NONE
                continue
            page_va = seg_base + seg_offset + p * page_size
            offset_in_page = start * 8 if start & 0x8000 else start
            slot_va = page_va + offset_in_page
            for _ in range(4096):
                file_off = img.va_to_offset(slot_va)
                if file_off is None or file_off + 8 > len(d):
                    break
                raw = struct.unpack_from("<Q", d, file_off)[0]
                if ptr_format in (2, 6):  # DYLD_CHAINED_PTR_64[_OFFSET]
                    if raw >> 63:  # bind
                        nxt = (raw >> 51) & 0xFFF
                    else:
                        target = raw & 0xFFFFFFFFF
                        img.index_pointer(slot_va, target + (text_base if ptr_format == 6 else 0))
                        nxt = (raw >> 51) & 0xFFF
                elif ptr_format in (1, 7, 10, 13):  # ARM64E variants
                    if (raw >> 62) & 1:  # bind
                        nxt = (raw >> 51) & 0x7FF
                    else:
                        target = raw & 0xFFFFFFFF
                        img.index_pointer(slot_va, target + (text_base if ptr_format == 7 else 0))
                        nxt = (raw >> 51) & 0x7FF
                else:
                    break
                if not nxt:
                    break
                slot_va += nxt * 8


CPU_X86_64 = 0x01000007
CPU_ARM64 = 0x0100000C


def macho_slice_cpus(path: Path) -> list[int]:
    """CPU types present in a Mach-O (one for thin, several for fat)."""
    with path.open("rb") as f:
        d = f.read(4096)
    if d[:4] != b"\xca\xfe\xba\xbe":
        return [struct.unpack_from("<I", d, 4)[0]]
    nfat = struct.unpack_from(">I", d, 4)[0]
    return [struct.unpack_from(">I", d, 8 + i * 20)[0] for i in range(nfat)]


def parse_macho(path: Path, cpu: int | None = None) -> Image:
    img = Image(path, "macho")
    d = img.data
    if d[:4] == b"\xca\xfe\xba\xbe":  # fat: select by CPU, else largest slice
        nfat = struct.unpack_from(">I", d, 4)[0]
        best_off, best_size = 0, 0
        for i in range(nfat):
            scpu, _, off, size, _ = struct.unpack_from(">IIIII", d, 8 + i * 20)
            if cpu is not None:
                if scpu == cpu:
                    best_off, best_size = off, size
                    break
            elif size > best_size:
                best_off, best_size = off, size
        if best_size == 0:
            raise ValueError(f"{path.name}: no Mach-O slice for CPU {hex(cpu) if cpu is not None else 'any'}")
        img.data = d[best_off : best_off + best_size]
        d = img.data
    img.cpu_type = struct.unpack_from("<I", d, 4)[0]
    ncmds = struct.unpack_from("<I", d, 16)[0]
    off = 32
    func_starts_data = None
    segments: list[tuple[int, int, int]] = []  # (vmaddr, filesize, fileoff)
    rebase_blob: tuple[int, int] | None = None
    chained: tuple[int, int] | None = None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", d, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            # segment_command_64: cmd(4) cmdsize(4) segname(16) THEN vmaddr.
            # Reading at off+16 lands inside segname and yields ASCII garbage,
            # which used to shadow every real section in va_to_offset().
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", d, off + 24)
            if filesize > 0:
                segments.append((vmaddr, filesize, fileoff))
            nsects = struct.unpack_from("<I", d, off + 64)[0]
            for s in range(nsects):
                so = off + 72 + s * 80
                sectname = d[so : so + 16].rstrip(b"\x00").decode(errors="ignore")
                saddr, ssize = struct.unpack_from("<QQ", d, so + 32)
                soffset = struct.unpack_from("<I", d, so + 48)[0]
                if ssize > 0 and saddr:
                    img.sections.append((saddr, ssize, soffset))
                    img.section_names.append(sectname)
        elif cmd == 0x26:  # LC_FUNCTION_STARTS
            dataoff, datasize = struct.unpack_from("<II", d, off + 8)
            func_starts_data = d[dataoff : dataoff + datasize]
        elif cmd in (0x22, 0x80000022):  # LC_DYLD_INFO[_ONLY]
            rebase_off, rebase_size = struct.unpack_from("<II", d, off + 8)
            if rebase_size:
                rebase_blob = (rebase_off, rebase_size)
        elif cmd == 0x80000034:  # LC_DYLD_CHAINED_FIXUPS
            dataoff, datasize = struct.unpack_from("<II", d, off + 8)
            if datasize:
                chained = (dataoff, datasize)
        off += size

    # Segments are the authoritative mapping (they cover the header and any
    # inter-section padding); sections are kept for name lookups only and are
    # appended after so a segment never shadows a narrower section match.
    named = list(zip(img.section_names, img.sections))
    img.sections = sorted(segments)
    img.section_names = ["" for _ in segments]
    for nm, sec in named:
        img.sections.append(sec)
        img.section_names.append(nm)
    img.base = min((s[0] for s in segments), default=0)
    img.finish()

    if rebase_blob:
        _parse_macho_rebase(img, segments, *rebase_blob)
    if chained:
        _parse_macho_chained(img, segments, *chained)
    if img.reloc_ptrs:
        img.reloc_based = True
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


def find_xrefs(text_va: int, text: bytes, target_va: int) -> list[int]:
    """VAs of RIP-relative LEA/MOV instructions in .text referencing target_va.

    x86-64 only: REX.W lea/mov r64, [rip+disp32] (48/4C 8D/8B, modrm mod=0 rm=5).
    """
    xrefs: list[int] = []
    for i in range(len(text) - 7):
        if text[i] in (0x48, 0x4C) and text[i + 1] in (0x8D, 0x8B) and (text[i + 2] & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", text, i + 3)[0]
            if text_va + i + 7 + disp == target_va:
                xrefs.append(text_va + i)
    return xrefs


PAD_BYTES = {0xCC, 0x90, 0x00}
TERM_BYTES = {0xC3, 0xC9, 0xCB}  # ret / leave / retf


def recover_function_start(text_va: int, text: bytes, xref_va: int) -> int | None:
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


def build_xref_index(text_va: int, text: bytes) -> dict[int, list[int]]:
    """One pass over .text collecting every RIP-relative LEA/MOV target.

    find_xrefs() rescans the whole code section per probe; with a 30 MB .text
    and a handful of probes that is tens of seconds of pure repetition. The
    index costs a single pass and makes each probe lookup O(1).
    """
    index: dict[int, list[int]] = {}
    for i in range(len(text) - 7):
        if text[i] in (0x48, 0x4C) and text[i + 1] in (0x8D, 0x8B) and (text[i + 2] & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", text, i + 3)[0]
            index.setdefault(text_va + i + 7 + disp, []).append(text_va + i)
    return index


def build_xref_index_arm64(text_va: int, text: bytes) -> dict[int, list[int]]:
    """ARM64 string xrefs: ADRP+ADD pairs (there is no RIP-relative LEA).

    Pure encoding scan: every ADRP whose page result is consumed by an ADD
    #imm12 within the next few instructions contributes one target. The
    string tier needs no disassembler on arm64; LC_FUNCTION_STARTS already
    gives exact function boundaries.
    """
    index: dict[int, list[int]] = {}
    for i in range(0, len(text) - 4, 4):
        w = struct.unpack_from("<I", text, i)[0]
        if (w & 0x9F000000) != 0x90000000:  # ADRP
            continue
        imm = (((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 3)
        if imm & 0x100000:
            imm -= 0x200000
        page = ((text_va + i) & ~0xFFF) + (imm << 12)
        rd = w & 0x1F
        for j in range(i + 4, min(i + 24, len(text) - 4), 4):
            w2 = struct.unpack_from("<I", text, j)[0]
            if (w2 & 0xFF000000) != 0x91000000:  # ADD (immediate, 64-bit)
                continue
            if ((w2 >> 5) & 0x1F) == rd and not ((w2 >> 22) & 1):
                index.setdefault(page + ((w2 >> 10) & 0xFFF), []).append(text_va + i)
                break
    return index


# --------------------------------------------------------------------------
# Tier 4: callee-fingerprint transfer (non-virtual members)
#
# CPackageInfoCache::GetPackageInfo is NOT virtual - the class only has
# destructor slots - so no anchor map can reach it. It is however a direct
# callee of CUser::CheckAppOwnership, which tier 1 anchors on every platform.
# We therefore anchor the CALLER by string, then select the callee by a
# structural fingerprint of the utlrbtree lookup the function performs.
#
# The fingerprint is ABI-independent because it describes the data structure,
# not the instruction encoding:
#   [this+0x20]  root node index      [this+0x38]  node array base
#   node stride 32 (shl 5)            key compared at [node+0x10]
#   at least one direct call          the create/insert path for a missing id
# The last clause separates GetPackageInfo from the pure Find helper, which
# macOS emits as a separate call-free function with the same lookup head.
# --------------------------------------------------------------------------

PACKAGE_CACHE_CALLER = "CheckAppOwnership"


def _capstone():
    """Return a configured x86-64 disassembler, or None when unavailable.

    Only the x86-64 slices are fingerprinted: the macOS arm64 slice is covered
    by transferring the x86_64 result, since both slices are built from the
    same source and the runtime consumes one RVA namespace per platform.
    """
    try:
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    except ImportError:
        return None
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    return md


def function_instructions(img: Image, rva: int, md) -> list:
    """Decode one function body.

    ELF .eh_frame_hdr and Mach-O LC_FUNCTION_STARTS give exact boundaries.
    PE .pdata additionally lists unwind chains, so the next entry can sit
    INSIDE the body; there we decode to the first int3 pad instead, which is
    how MSVC separates functions.
    """
    va = img.base + rva
    off = img.va_to_offset(va)
    if off is None:
        return []
    if img.kind == "pe":
        out = []
        for ins in md.disasm(img.data[off : off + 0x2000], va):
            if ins.mnemonic == "int3":
                break
            out.append(ins)
        return out
    starts = img.func_starts
    import bisect

    i = bisect.bisect_left(starts, va)
    end = starts[i + 1] if i + 1 < len(starts) else va + 0x2000
    return list(md.disasm(img.data[off : off + (end - va)], va))


def direct_call_targets(img: Image, rva: int, md) -> set[int]:
    """Callees of a function, excluding its own internal branch targets.

    A local `jmp` (loop back-edge, hot/cold split) points INSIDE the body and
    is not a callee; keeping those produced self-referential "candidates".
    """
    seq = function_instructions(img, rva, md)
    if not seq:
        return set()
    lo = seq[0].address - img.base
    hi = seq[-1].address - img.base
    out = set()
    for ins in seq:
        if ins.mnemonic not in ("call", "jmp") or not ins.op_str.startswith("0x"):
            continue
        try:
            tgt = int(ins.op_str, 16)
        except ValueError:
            continue
        rva_tgt = tgt - img.base
        if in_text(img, tgt) and not (lo <= rva_tgt <= hi):
            out.add(rva_tgt)
    return out


def is_package_cache_accessor(img: Image, rva: int, md) -> bool:
    """Structural fingerprint of CPackageInfoCache::GetPackageInfo."""
    from capstone.x86 import X86_OP_MEM

    seq = function_instructions(img, rva, md)
    if not seq:
        return False
    head = seq[:24]
    # GCC/Clang sign-extend the 32-bit root index with movsxd where MSVC uses
    # a plain mov; both are loads of the same field.
    loads = ("mov", "movsxd", "movslq")
    root = any(
        i.mnemonic in loads
        and any(o.type == X86_OP_MEM and o.mem.disp == 0x20 and o.mem.index == 0 for o in i.operands)
        for i in head
    )
    nodes = any(
        i.mnemonic in loads
        and any(o.type == X86_OP_MEM and o.mem.disp == 0x38 and o.mem.index == 0 for o in i.operands)
        for i in head
    )
    stride = any(i.mnemonic == "shl" and i.op_str.endswith(", 5") for i in seq[:32])
    key = any(
        i.mnemonic == "cmp" and any(o.type == X86_OP_MEM and o.mem.disp == 0x10 for o in i.operands)
        for i in seq[:32]
    )
    creates = any(i.mnemonic == "call" for i in seq)
    return root and nodes and stride and key and creates


def derive_callee_fingerprints(img: Image) -> tuple[dict[str, dict], list[dict]]:
    md = _capstone()
    if md is None:
        return {}, [{"target": "GetPackageInfo", "status": "skipped", "reason": "capstone not installed"}]
    callers, _ = derive(img)
    anchor = callers.get(PACKAGE_CACHE_CALLER)
    if not anchor:
        return {}, [{"target": "GetPackageInfo", "status": "unresolved",
                     "reason": f"caller {PACKAGE_CACHE_CALLER} not anchored"}]
    caller_rva = int(anchor["rva"], 16)
    candidates = sorted(
        t for t in direct_call_targets(img, caller_rva, md) if is_package_cache_accessor(img, t, md)
    )
    if len(candidates) == 1:
        return (
            {"GetPackageInfo": {"rva": f"0x{candidates[0]:x}", "source": "derived:callee-fingerprint"}},
            [{"target": "GetPackageInfo", "caller": PACKAGE_CACHE_CALLER,
              "caller_rva": anchor["rva"], "resolved": True}],
        )
    # Ambiguity is a hard stop: a wrong RVA crashes the client, a missing one
    # only disables a hook.
    return {}, [{"target": "GetPackageInfo", "status": "ambiguous" if candidates else "unresolved",
                 "caller": PACKAGE_CACHE_CALLER, "candidates": [hex(c) for c in candidates]}]


# --------------------------------------------------------------------------
# Tier 5: license subsystem (MarkLicenseAsChanged / ProcessPendingLicenseUpdates)
#
# Both are non-virtual CUser members with no string literals of their own, so
# neither the anchor map nor a direct probe can name them. Two derivation
# chains that share no code path converge on each:
#
#   Producer (string-xref family):
#     'CPICSJobRequestProductInfoUpdate::BParseResponseMessage' VProf literal
#     -> the call site passing (user=[cache+0x1d8], pkg, false)
#     -> MarkLicenseAsChanged; then the unique caller of Mark that drains the
#     pending-update vector (stride 0x50, tag [elem+0x14]==0x50)
#     -> ProcessPendingLicenseUpdates.
#
#   Gate (RTTI family):
#     CUser typeinfo -> vtables -> the virtual slot whose body calls a
#     pending-vector drain -> ProcessPendingLicenseUpdates; the drain's
#     unique bool=false callee -> MarkLicenseAsChanged.
#
# A target is emitted only when both chains agree on a single RVA.
# CUtlMemoryGrow stays unresolved on purpose: the hook is optional
# (GrowAndAppend has an explicit malloc fallback) and the template has no
# unique anchor - many instantiations, and the old byte pattern is dead.
# --------------------------------------------------------------------------

LICENSE_PRODUCER_ANCHOR = "BParseResponseMessage"
LICENSE_CORROBORATOR = "BUpdateLicenses"
PENDING_TAG_DISP = 0x14
PENDING_ELEM_STRIDE = 0x50


def license_instructions(img: Image, rva: int, md) -> list:
    """Function body decode hardened for MSVC hot/cold chunks.

    A dense .pdata neighbour (<0x40 bytes away) is an unwind sub-range of the
    same body: keep the int3-stop walk. A distant neighbour is a real chunk
    boundary: decode the whole range skipping int3 pads, because assert
    paths split a chunk with int3 and continue afterwards (stopping at the
    first int3 lost the MarkLicenseAsChanged call in the Windows cold chunk).
    """
    if img.kind != "pe":
        return function_instructions(img, rva, md)
    import bisect

    va = img.base + rva
    off = img.va_to_offset(va)
    if off is None:
        return []
    i = bisect.bisect_right(img.func_starts, va)
    nxt = img.func_starts[i] if i < len(img.func_starts) else va + 0x2000
    if nxt - va < 0x40:
        return function_instructions(img, rva, md)
    end = min(nxt, va + 0x4000)
    return [ins for ins in md.disasm(img.data[off : off + (end - va)], va) if ins.mnemonic != "int3"]


_JCC = ("jmp", "je", "jne", "jg", "jge", "jl", "jle", "ja", "jae", "jb", "jbe", "js", "jns")


def license_cluster(img: Image, rva: int, md) -> tuple[list, set]:
    """Instructions of a function plus its MSVC hot/cold chunk cluster.

    Returns (seq, covered_rvas); branch targets inside the cluster are not
    callees. ELF/Mach-O boundaries are exact, so the cluster is one function.
    """
    seq: list = []
    covered: set[int] = set()
    seen: set[int] = set()
    queue = [rva]
    while queue and len(seen) < 24:
        cur = queue.pop(0)
        if cur in seen or cur in covered:
            continue
        seen.add(cur)
        body = license_instructions(img, cur, md)
        if not body:
            continue
        seq.extend(body)
        for ins in body:
            covered.add(ins.address - img.base)
        if img.kind != "pe":
            continue
        for ins in body:
            if ins.mnemonic not in _JCC or not ins.op_str.startswith("0x"):
                continue
            try:
                tgt = int(ins.op_str, 16) - img.base
            except ValueError:
                continue
            if tgt in seen or tgt in covered or not in_text(img, img.base + tgt):
                continue
            if abs(tgt - rva) > 0x40000 or img.containing_function(img.base + tgt) is None:
                continue
            queue.append(tgt)
    return seq, covered


def cluster_call_targets(img: Image, rva: int, md) -> set[int]:
    seq, covered = license_cluster(img, rva, md)
    out = set()
    for ins in seq:
        if ins.mnemonic not in ("call", "jmp") or not ins.op_str.startswith("0x"):
            continue
        try:
            tgt = int(ins.op_str, 16) - img.base
        except ValueError:
            continue
        if in_text(img, img.base + tgt) and tgt not in covered:
            out.add(tgt)
    return out


def _bool_and_this_setup(img: Image, seq: list, n: int) -> tuple[int | None, bool]:
    """Argument setup before call seq[n]: (third-arg value, this-is-field-load).

    The bool is 0/1 only when its register was last set by xor/mov-imm; any
    other write invalidates the window so a stale xor cannot leak across an
    unrelated overwrite. The this check requires a load from a heap object
    field ([base+disp], base neither rsp nor rbp) - the cache->user pointer.
    """
    from capstone.x86 import X86_OP_MEM, X86_REG_RBP, X86_REG_RSP

    if img.kind == "pe":
        boolregs, thisregs = ("r8b", "r8d"), ("rcx", "ecx")
    else:
        boolregs, thisregs = ("dl", "edx"), ("rdi", "edi")
    boolval = None
    fieldload = False
    seen_bool = seen_this = False
    for prev in reversed(seq[max(0, n - 10) : n]):
        ops = [o.strip() for o in prev.op_str.split(",")]
        if len(ops) != 2:
            continue
        dst = ops[0]
        if not seen_bool and dst in boolregs:
            seen_bool = True
            if prev.mnemonic == "xor" and dst == ops[1]:
                boolval = 0
            elif prev.mnemonic == "mov" and ops[1] in ("0", "1"):
                boolval = int(ops[1])
        if not seen_this and dst in thisregs:
            seen_this = True
            if prev.mnemonic == "mov":
                for op in prev.operands:
                    if (op.type == X86_OP_MEM and op.mem.index == 0 and op.mem.disp > 0
                            and op.mem.base not in (X86_REG_RBP, X86_REG_RSP)):
                        fieldload = True
        if seen_bool and seen_this:
            break
    return boolval, fieldload


def mark_call_targets(img: Image, rva: int, md) -> set[int]:
    """External calls from rva's cluster passing (this=[obj+field], pkg, false)."""
    seq, covered = license_cluster(img, rva, md)
    out = set()
    for n, ins in enumerate(seq):
        if ins.mnemonic != "call" or not ins.op_str.startswith("0x"):
            continue
        try:
            tgt = int(ins.op_str, 16) - img.base
        except ValueError:
            continue
        if tgt in covered or not in_text(img, img.base + tgt):
            continue
        boolval, fieldload = _bool_and_this_setup(img, seq, n)
        if boolval == 0 and fieldload:
            out.add(tgt)
    return out


def has_pending_drain_shape(img: Image, rva: int, md) -> bool:
    """Structural fingerprint of the pending-license-update drain loop:
    element tag compare [elem+0x14]==0x50 plus 0x50-byte stride arithmetic.
    ABI-independent: it describes the data structure, not the encoding.
    Both clauses exclude stack-relative operands: CheckAppOwnership compares
    a stack local at [rbp+0x14] and takes stack addresses at [rsp+0x50],
    which the loose form matched as a false drain."""
    from capstone.x86 import X86_OP_MEM, X86_REG_RBP, X86_REG_RSP

    seq = license_instructions(img, rva, md)
    if not seq:
        return False
    tag = any(
        i.mnemonic == "cmp"
        and any(o.type == X86_OP_MEM and o.mem.disp == PENDING_TAG_DISP
                and o.mem.base not in (X86_REG_RBP, X86_REG_RSP) for o in i.operands)
        and i.op_str.endswith(f", {hex(PENDING_ELEM_STRIDE)}")
        for i in seq
    )
    stride = any(
        (i.mnemonic == "sub" and i.op_str.endswith(", 0x50") and not i.op_str.startswith("rsp"))
        or (i.mnemonic == "add" and i.op_str.endswith(", -0x50") and not i.op_str.startswith("rsp"))
        for i in seq
    )
    calls = any(i.mnemonic == "call" for i in seq)
    return tag and stride and calls


def pe_public_entry(img: Image, rva: int, md) -> int | None:
    """Map an MSVC cold chunk to the public entry whose body reaches it.

    .pdata lists unwind sub-ranges as separate entries, so the chunk that
    contains a call site is not necessarily the function entry. The entry
    must start with a prologue and its decode must cover the chunk.
    """
    if img.kind != "pe":
        return rva
    import bisect

    va = img.base + rva
    i = bisect.bisect_left(img.func_starts, va)
    if i < len(img.func_starts) and img.func_starts[i] == va:
        body = license_instructions(img, rva, md)
        if body and body[0].mnemonic in ("push", "sub"):
            return rva
    best = None
    for f in img.func_starts[max(0, i - 16) : i]:
        frva = f - img.base
        if rva - frva > 0x400:
            continue
        body = license_instructions(img, frva, md)
        if not body or body[0].mnemonic not in ("push", "sub"):
            continue
        if any(ins.address == va for ins in body):
            best = frva
    return best


def license_producer(img: Image, md) -> tuple[int | None, int | None, list]:
    """Producer chain: BParseResponseMessage string anchor -> Mark -> drain."""
    details: list[dict] = []
    anchors, _ = derive(img)
    anchor = anchors.get(LICENSE_PRODUCER_ANCHOR)
    if not anchor:
        return None, None, [{"target": "license-trio", "status": "unresolved",
                             "reason": f"{LICENSE_PRODUCER_ANCHOR} not anchored"}]
    arva = int(anchor["rva"], 16)
    marks = mark_call_targets(img, arva, md)
    details.append({"mechanism": "producer", "anchor": LICENSE_PRODUCER_ANCHOR,
                    "anchor_rva": anchor["rva"], "mark_candidates": sorted(hex(m) for m in marks)})
    if len(marks) != 1:
        return None, None, details
    mark = marks.pop()
    cor = anchors.get(LICENSE_CORROBORATOR)
    if cor:
        cor_ok = mark in cluster_call_targets(img, int(cor["rva"], 16), md)
        details.append({"mechanism": "producer", "corroborator": LICENSE_CORROBORATOR,
                        "calls_mark": cor_ok})
        if not cor_ok:
            return None, None, details
    text_va, text = img.text()
    target = img.base + mark
    callers: set[int] = set()
    i = 0
    while True:
        i = text.find(b"\xe8", i)
        if i < 0 or i + 5 > len(text):
            break
        rel = struct.unpack_from("<i", text, i + 1)[0]
        if text_va + i + 5 + rel == target:
            fn = img.containing_function(text_va + i)
            if fn is not None:
                callers.add(fn - img.base)
        i += 1
    drains = set()
    for c in sorted(callers):
        if has_pending_drain_shape(img, c, md):
            e = pe_public_entry(img, c, md)
            if e is not None:
                drains.add(e)
    details.append({"mechanism": "producer", "mark": hex(mark),
                    "drain_callers": sorted(hex(d) for d in drains)})
    if len(drains) != 1:
        return mark, None, details
    return mark, drains.pop(), details


def license_via_rtti(img: Image, md) -> tuple[int | None, int | None, list]:
    """Gate chain: CUser RTTI -> vtable slots -> pending drain -> Mark.

    Shares no code path with the producer: different anchor family (RTTI
    relocations vs VProf literals) and different traversal (vtable slots vs
    call-site argument shapes).
    """
    details: list[dict] = []
    ti = ".?AVCUser@@" if img.kind == "pe" else "5CUser"
    drains: set[int] = set()
    for vt in find_vtables_for_typeinfo(img, ti):
        for slot in range(128):
            p = deref_qword(img, vt + slot * 8)
            if p is None or not in_text(img, p):
                break
            for t in cluster_call_targets(img, p - img.base, md):
                if has_pending_drain_shape(img, t, md):
                    e = pe_public_entry(img, t, md)
                    if e is not None:
                        drains.add(e)
    details.append({"mechanism": "rtti-vtable-drain", "typeinfo": ti,
                    "drains": sorted(hex(d) for d in drains)})
    if len(drains) != 1:
        return None, None, details
    pplu = drains.pop()
    seq, covered = license_cluster(img, pplu, md)
    marks: set[int] = set()
    for n, ins in enumerate(seq):
        if ins.mnemonic != "call" or not ins.op_str.startswith("0x"):
            continue
        try:
            tgt = int(ins.op_str, 16) - img.base
        except ValueError:
            continue
        if tgt in covered or not in_text(img, img.base + tgt):
            continue
        boolval, _ = _bool_and_this_setup(img, seq, n)
        if boolval == 0:
            marks.add(tgt)
    details.append({"mechanism": "rtti-vtable-drain", "pplu": hex(pplu),
                    "mark_candidates": sorted(hex(m) for m in marks)})
    mark = marks.pop() if len(marks) == 1 else None
    return mark, pplu, details


def derive_license_trio(img: Image) -> tuple[dict[str, dict], list[dict]]:
    md = _capstone()
    if md is None:
        return {}, [{"target": "license-trio", "status": "skipped", "reason": "capstone not installed"}]
    mark_p, pplu_p, det_p = license_producer(img, md)
    mark_r, pplu_r, det_r = license_via_rtti(img, md)
    details = det_p + det_r
    resolved: dict[str, dict] = {}
    for name, a, b in (
        ("MarkLicenseAsChanged", mark_p, mark_r),
        ("ProcessPendingLicenseUpdates", pplu_p, pplu_r),
    ):
        if a is not None and a == b:
            resolved[name] = {"rva": f"0x{a:x}", "source": "derived:license-dual-chain"}
            details.append({"target": name, "resolved": True, "rva": hex(a)})
        else:
            details.append({"target": name,
                            "status": "unresolved" if (a is None and b is None) else "mismatch",
                            "producer": hex(a) if a is not None else None,
                            "rtti": hex(b) if b is not None else None})
    details.append({"target": "CUtlMemoryGrow", "status": "deferred",
                    "reason": "optional hook with malloc fallback; no unique anchor across template instantiations"})
    return resolved, details


# --------------------------------------------------------------------------
# Tier 6: steamui FillInAppOverview (dual chain, per Mach-O slice)
#
# M1: the qualified 'CSteamUIAppController::FillInAppOverview' VProf literal
#     (RIP xref on x86_64, ADRP+ADD on arm64).
# M2: the caller 'CSteamUIAppController::BuildCompleteAppOverviewChange - app'
#     anchored by its own literal; inside it, the unique call that passes the
#     freshly appended overview message as the second argument:
#       x86-64: last write to rsi is `mov rsi, rax`, and rax was just stored
#               into a CUtlVector element slot [base+index*8+8];
#       arm64:  x1 is untouched in the window and `str x1, [xN, #8]` stored
#               it into the element slot.
# Emitted only when M1 and M2 agree on one RVA, per slice. The fat TOML
# carries rva (x86_64) plus rva_arm64; the runtime selects by loaded slice
# and fails closed when the needed architecture is absent.
# --------------------------------------------------------------------------

UI_CALLER_ANCHOR = "BuildCompleteAppOverviewChange"


def _arm64_capstone():
    try:
        from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM, CS_MODE_LITTLE_ENDIAN
    except ImportError:
        return None
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    return md


def arm64_function_instructions(img: Image, rva: int, md) -> list:
    import bisect

    va = img.base + rva
    off = img.va_to_offset(va)
    if off is None:
        return []
    i = bisect.bisect_left(img.func_starts, va)
    end = img.func_starts[i + 1] if i + 1 < len(img.func_starts) else va + 0x2000
    return list(md.disasm(img.data[off : off + (end - va)], va))


_ARM64_WRITES = ("mov", "movz", "movk", "movn", "mvn", "neg", "ldr", "ldur", "ldp", "add", "adds",
                 "sub", "subs", "orr", "orn", "eor", "eon", "and", "ands", "adr", "adrp", "csel",
                 "csinc", "csinv", "cneg", "cinc", "lsl", "lsr", "asr", "ror", "mul", "udiv", "sdiv")


def ui_fill_call_targets(img: Image, caller_rva: int, md_x86, md_arm) -> set[int]:
    out: set[int] = set()
    if img.cpu_type == CPU_ARM64:
        if md_arm is None:
            return out
        seq = arm64_function_instructions(img, caller_rva, md_arm)
        for n, ins in enumerate(seq):
            if ins.mnemonic != "bl" or not ins.op_str.startswith("#0x"):
                continue
            win = seq[max(0, n - 8) : n]
            x1_written = any(
                p.mnemonic in _ARM64_WRITES and p.op_str.split(",")[0].strip() in ("x1", "w1") for p in win
            )
            stored = any(
                p.mnemonic == "str" and p.op_str.startswith("x1, [") and p.op_str.endswith("#8]") for p in win
            )
            if stored and not x1_written:
                try:
                    out.add(int(ins.op_str[1:], 16) - img.base)
                except ValueError:
                    pass
    else:
        if md_x86 is None:
            return out
        seq = function_instructions(img, caller_rva, md_x86)
        for n, ins in enumerate(seq):
            if ins.mnemonic != "call" or not ins.op_str.startswith("0x"):
                continue
            win = seq[max(0, n - 10) : n]
            fresh_arg2 = False
            for p in reversed(win):
                ops = [o.strip() for o in p.op_str.split(",")]
                if ops and ops[0] in ("rsi", "esi"):
                    fresh_arg2 = p.mnemonic == "mov" and len(ops) == 2 and ops[1] == "rax"
                    break
            stored = any(p.mnemonic == "mov" and "*8 + 8], rax" in p.op_str for p in win)
            if fresh_arg2 and stored:
                try:
                    out.add(int(ins.op_str, 16) - img.base)
                except ValueError:
                    pass
    return out


def derive_ui_overview(img: Image, resolved: dict) -> tuple[dict[str, dict], list[dict]]:
    """FillInAppOverview dual chain; `resolved` carries the string-tier hits."""
    anchor = resolved.get(UI_CALLER_ANCHOR)
    if not anchor:
        return {}, [{"target": "FillInAppOverview", "status": "unresolved",
                     "reason": f"{UI_CALLER_ANCHOR} not anchored"}]
    md_x86 = _capstone() if img.cpu_type != CPU_ARM64 else None
    md_arm = _arm64_capstone() if img.cpu_type == CPU_ARM64 else None
    if (md_x86 is None and md_arm is None):
        return {}, [{"target": "FillInAppOverview", "status": "skipped", "reason": "capstone not installed"}]
    tgts = ui_fill_call_targets(img, int(anchor["rva"], 16), md_x86, md_arm)
    details = [{"target": "FillInAppOverview", "mechanism": "ui-caller-fingerprint",
                "caller_rva": anchor["rva"], "candidates": sorted(hex(t) for t in tgts)}]
    if len(tgts) != 1:
        return {}, details
    m2 = tgts.pop()
    m1 = resolved.get("FillInAppOverview")
    if m1 and int(m1["rva"], 16) == m2:
        return {"FillInAppOverview": {"rva": f"0x{m2:x}", "source": "derived:ui-dual-chain"}}, details
    details.append({"target": "FillInAppOverview", "status": "mismatch" if m1 else "unresolved",
                    "literal": m1["rva"] if m1 else None, "fingerprint": hex(m2)})
    return {}, details


def derive(img: Image) -> tuple[dict[str, dict], list[dict]]:
    text_va, text = img.text()
    if img.cpu_type == CPU_ARM64:
        xref_index = build_xref_index_arm64(text_va, text)
    else:
        xref_index = build_xref_index(text_va, text)
    resolved: dict[str, dict] = {}
    details: list[dict] = []
    for name, probes in STRING_PROBES.items():
        starts: set[int] = set()
        probe_report = []
        for probe in probes:
            # Probes are ordered most-specific first. Once a qualified probe
            # resolves the target, a looser fallback must not run: it would
            # match unrelated literals and either add noise to the report or
            # turn a clean hit into a false "ambiguous" verdict.
            if starts:
                break
            for sva in find_string_vas(img, probe):
                xrefs = xref_index.get(sva, [])
                probe_report.append({"probe": probe, "string_va": hex(sva), "xrefs": len(xrefs)})
                for x in xrefs:
                    # Exact boundary table (eh_frame_hdr/.pdata/LC_FUNCTION_STARTS)
                    # when available; padding-walk heuristic as fallback.
                    start = img.containing_function(x)
                    if start is None and img.cpu_type != CPU_ARM64:
                        start = recover_function_start(text_va, text, x)
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

    try:
        img = parser_cls(args.binary)
    except ValueError as e:
        print(f"[derive] skipping {args.binary.name}: {e}")
        return 0


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

    fp_resolved, fp_details = derive_callee_fingerprints(img)
    for name, meta in fp_resolved.items():
        resolved.setdefault(name, meta)
    details.extend(fp_details)
    if fp_resolved:
        print(f"[derive] callee-fingerprint tier: {', '.join(sorted(fp_resolved))}")
    trio_resolved, trio_details = derive_license_trio(img)
    for name, meta in trio_resolved.items():
        resolved.setdefault(name, meta)
    details.extend(trio_details)
    if trio_resolved:
        print(f"[derive] license-trio tier: {', '.join(sorted(trio_resolved))}")
    ui_resolved, ui_details = derive_ui_overview(img, resolved)
    details.extend(ui_details)
    if "FillInAppOverview" in resolved and "FillInAppOverview" not in ui_resolved:
        # A single-mechanism literal hit is not publishable (iron rule).
        resolved.pop("FillInAppOverview")
    for name, meta in ui_resolved.items():
        resolved[name] = meta
    if ui_resolved:
        print(f"[derive] steamui tier: {', '.join(sorted(ui_resolved))}")
    # Fat Mach-O: merge the arm64 slice (string-xref + UI tiers only; the
    # call-graph tiers are x86 disassembly and stay on the primary slice).
    if kind == "macho" and img.cpu_type != CPU_ARM64 and CPU_ARM64 in macho_slice_cpus(args.binary):
        arm_img = parse_macho(args.binary, CPU_ARM64)
        arm_resolved, arm_details = derive(arm_img)
        details.extend(arm_details)
        arm_ui, arm_ui_details = derive_ui_overview(arm_img, arm_resolved)
        details.extend(arm_ui_details)
        if "FillInAppOverview" in arm_resolved and "FillInAppOverview" not in arm_ui:
            arm_resolved.pop("FillInAppOverview")
        arm_resolved.update(arm_ui)
        for name, meta in arm_resolved.items():
            if name in resolved:
                if resolved[name].get("rva") != meta["rva"]:
                    resolved[name]["rva_arm64"] = meta["rva"]
            else:
                resolved[name] = {"rva_arm64": meta["rva"], "source": meta["source"]}
        print(f"[derive] arm64 slice: {len(arm_resolved)} function(s) merged")
    print(f"[derive] {args.binary.name} sha256={digest[:16]} kind={kind}: {len(resolved)} function(s) derived")


    if not resolved:
        print("[derive] nothing resolved; skipping TOML (repo-noise rule)")
        return 0

    out_dir = args.out
    out_dir.mkdir(parents=True, exist_ok=True)
    def fmt_entry(n: str, m: dict) -> str:
        fields = []
        if "rva" in m:
            fields.append(f'rva = "{m["rva"]}"')
        if "rva_arm64" in m:
            fields.append(f'rva_arm64 = "{m["rva_arm64"]}"')
        fields.append(f'source = "{m["source"]}"')
        return f'{n} = {{ {", ".join(fields)} }}'

    entries = "\n".join(fmt_entry(n, m) for n, m in sorted(resolved.items()))
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
