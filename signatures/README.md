# OmniSteam Signature Database

All dynamic client signatures are automatically harvested by CI and hosted on the **`data`** branch:
- CDN Base: `https://cdn.jsdelivr.net/gh/arisvia/omni-steam@data/signatures/<platform-dir>/<sha256>.toml`
- Raw Base: `https://raw.githubusercontent.com/arisvia/omni-steam/data/signatures/<platform-dir>/<sha256>.toml`

## Repository Assets on `main`:
- `anchor-map-windows.json`: Semantic RTTI Complete Object Locator (COL) vtable slot mapping rules used by `derive_signatures.py` to transfer function addresses across new Steam client versions.

## Platform directories

Only 64-bit targets are published: `windows-x64`, `linux-x64`, `macos-universal`.
There is no 32-bit namespace: OmniSteam does not build or ship a 32-bit core.
CMake rejects 32-bit toolchains at configure time, and the harvest/derive tools
skip 32-bit ELF images outright.

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

## Reaching non-virtual members: callee fingerprints

Some unlock targets are not virtual at all — `CPackageInfoCache` only has
destructor slots, so no anchor map entry can name `GetPackageInfo`. For those,
`derive_signatures.py` anchors a **caller** by string literal and then selects
the direct callee whose decoded instructions match a structural fingerprint of
the data structure it walks:

| Clause | Meaning |
| :--- | :--- |
| load `[this+0x20]` | utlrbtree root node index |
| load `[this+0x38]` | node array base |
| `shl …, 5` | 32-byte node stride |
| `cmp` against `[node+0x10]` | the uint32 key |
| at least one `call` | the create/insert path for a missing id |

The last clause matters: macOS emits the pure `Find` helper as a separate
call-free function with an identical lookup head, and it must be rejected.
This tier needs `capstone`; without it the tier is skipped rather than guessed.

## License subsystem caller anchors

The three Package-0 targets `CUtlMemoryGrow`, `MarkLicenseAsChanged` and
`ProcessPendingLicenseUpdates` have no unique literal and no RTTI class of
their own. Two *callers* inside the subsystem are anchorable by qualified VProf
budget literals and are now emitted by the string-xref tier on all three
platforms (Windows keeps the `CUser::` prefix, GCC/Clang emit the bare name):

| Anchor | Windows | Linux | macOS |
| :--- | :--- | :--- | :--- |
| `BUpdateLicenses` (`CUser::BUpdateLicenses`) | `0x998fcb` | `0x1632fe0` | `0xa75220` |
| `OnLicensesUpdated` (`CCompatManager::OnLicensesUpdated`) | `0x59f730` | `0x102de80` | `0x69e1b0` |

These are the entry points for a future callee-fingerprint tier. The
`CInternalCallback<CUserAppManager, LicensesUpdated_t>` route is a dead end:
its `Run()` is ICF-merged with three sibling event callbacks into one generic
dispatcher, so it cannot name an event-specific handler. Candidate RVAs
identified from the call graph are listed in `docs/ROADMAP.md` §2.2 but are
**not** published: two independent mechanisms must agree before any of them
reaches a TOML. A wrong RVA hooks unrelated code and crashes the client.

## steamui reference binary

The macOS harvest artifact now publishes `steamui.dylib` (Linux steamcmd has
no `steamui.so`). Its qualified budget literal
`CSteamUIAppController::FillInAppOverview` has exactly one RIP xref and
`derive_signatures.py` now probes the qualified spelling first — the bare
substring sits 0x17 bytes into the literal and never matches a LEA target.
The Windows `steamui.dll` has zero xrefs (pointer-table reference) and
correctly derives 0 functions. **No steamui TOML is published**: PatternLoader
hashes only the target module (`steamclient*`), so a steamui-keyed document
would be rejected by `DeclaresMatchingHash` and never consumed. See
`docs/ROADMAP.md` §2.3 for the remaining wiring work.

## Two-mechanism CI gate

`tools/validate_signatures.py` cross-checks every target reachable by two
independent mechanisms and fails CI on any disagreement — a wrong RVA hooks
unrelated code and crashes the client, so it must never be published.

| Target | Mechanism A | Mechanism B |
| :--- | :--- | :--- |
| `RecvPkt`, `BBuildAndAsyncSendFrame` | vtable slot transfer | qualified VProf string xref |
| `GetPackageInfo` | callee fingerprint from `CheckAppOwnership` | `package_info_print` ConCommand registration → handler → tail call with `accessToken = 0` |

The ConCommand check is ABI-aware: MSVC passes the callback in `r8` before the
help text, SysV in `rdx` after it, so the handler is identified as the only LEA
in the constructor block targeting executable code. MSVC inlines the package
creation path into that handler, so Windows reports `n/a` for this particular
cross-check instead of failing a check it cannot run.

## Offset semantics: the layout gate (`tools/verify_abi_layout.py`)

Correct RVAs are half the contract. The hooks also **write into client-owned
structures** at offsets transcribed from the Windows client
(`AppOwnership::bOwnsLicense` at +0x24, `PackageId` at +0x00, `ReleaseState` at
+0x04, `CNetPacket::m_pubData`/`m_cubData` at +0x10/+0x18). If those offsets stop
describing the live layout, nothing fails loudly: the hook attaches at a correct
RVA, the client survives, and the unlock simply does nothing. A `static_assert`
in `SteamTypes.h` cannot see that - our struct always agrees with itself.

Each harvest job therefore runs the layout gate over the same official binaries
it just derived RVAs from, against the TOMLs it is about to publish:

```
tools/verify_abi_layout.py --scan-root <64-bit images> --signatures <out> --strict --negative-control
```

The gate disassembles the client's own `CheckAppOwnership` at the published RVA,
follows the third argument register (MSVC `r8`, SysV `rdx`, AAPCS `x2`) through
its prologue copies, and requires the client to touch the assumed offsets with
the assumed widths - plus two semantic shapes that a coincidental width match
would not produce: the byte at +0x24 is loaded and branched on, and the dword at
+0x04 is compared against `Released`(4). AArch64 runs the same rules on the
fat binary's arm64 slice.

`--negative-control` shifts the whole assumed layout by one byte and requires
every image to come back `CONTRADICTED`; without it a green run could mean the
rules never match anything.

`CNetPacket` is deliberately **not** covered here: on all three platforms
`RecvPkt` reaches the payload only through the packet object's own vtable, so the
concrete class is not statically identifiable and a "+0x10 pointer / +0x18
length" pair found in that function would belong to some unrelated struct. That
anchor is proven at runtime instead - `Hooks_NetPacket` cross-checks the two
fields against each other on every real packet, `Hooks_Package` re-reads the
struct the client itself filled, and `tools/smoke_status.py` fails the smoke run
on a probe that observed objects and decoded none of them. See
`docs/TESTING.md` §五.

## What the symbol tier (`harvest_signatures.py`) is for

It reads symbol tables. Official clients are stripped (Linux/macOS entirely; the
Windows export table only carries the C-level `SteamClient`/`SteamAPI_*` entry
points), so on a production harvest it resolves **0 targets on every platform**
and publishes nothing - that is the expected outcome, and the publishing path is
`derive_signatures.py`. Its purpose is symbol-bearing internal builds, where its
resolutions are an independent third opinion on the same names. Its scan step
matches `<sha16>-steamclient.so` style names by suffix and **fails** if a
non-empty scan root yields no library at all, so "the inputs were never opened"
can no longer masquerade as "the client has no symbols".
