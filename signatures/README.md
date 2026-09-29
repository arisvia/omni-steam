# OmniSteam Signature Database

All dynamic client signatures are automatically harvested by CI and hosted on the **`data`** branch:
- CDN Base: `https://cdn.jsdelivr.net/gh/arisvia/omni-steam@data/signatures/<platform-dir>/<sha256>.toml`
- Raw Base: `https://raw.githubusercontent.com/arisvia/omni-steam/data/signatures/<platform-dir>/<sha256>.toml`

## Repository Assets on `main`:
- `anchor-map-windows.json`: Semantic RTTI Complete Object Locator (COL) vtable slot mapping rules used by `derive_signatures.py` to transfer function addresses across new Steam client versions.
