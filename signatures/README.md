# OmniSteam Signature Database

All dynamic client signatures are automatically harvested by CI and hosted on the **`data`** branch:
- CDN Base: `https://cdn.jsdelivr.net/gh/arisvia/omni-steam@data/signatures/<platform-dir>/<sha256>.toml`
- Raw Base: `https://raw.githubusercontent.com/arisvia/omni-steam/data/signatures/<platform-dir>/<sha256>.toml`

## Repository Assets on `main`:
- `anchor-map-windows.json`: Semantic RTTI Complete Object Locator (COL) vtable slot mapping rules used by `derive_signatures.py` to transfer function addresses across new Steam client versions.

## Platform directories

Only 64-bit targets are published: `windows-x64`, `linux-x64`, `macos-universal`.
`linux-i386` is intentionally absent — OmniSteam does not ship a 32-bit core, and
keeping the namespace empty makes any hand-built 32-bit core fail closed rather
than consume x64 RVAs.

## How the anchor map crosses ABIs

`anchor-map-windows.json` records `(RTTI class name, vtable slot)` pairs. Virtual
method order comes from the C++ class definition, so it is identical under the
MSVC and Itanium ABIs — the same map therefore replays onto Linux and macOS.
The class name is translated from the MSVC spelling (`.?AVCFoo@@`) to the
Itanium length-prefixed form (`4CFoo`) automatically.

Walking those vtables requires resolving pointers first, because a stripped
client does not store them in the file image:

| Format | Where the real target lives |
| :--- | :--- |
| PE | the stored qword (preferred VA) |
| ELF (PIE) | `R_X86_64_RELATIVE` / `R_AARCH64_RELATIVE` addend in `.rela.dyn` |
| Mach-O | slot marked by the `LC_DYLD_INFO` rebase stream, or a `LC_DYLD_CHAINED_FIXUPS` chain entry |

`tools/validate_signatures.py` cross-checks every target reachable by both the
vtable transfer and an independent qualified-VProf-string cross-reference, and
fails CI on any disagreement — a wrong RVA hooks unrelated code and crashes the
client, so it must never be published.
