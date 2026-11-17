#!/usr/bin/env python3
"""core_status.json smoke regression gate (ROADMAP P0.3).

Validates the per-hook status document published by the injected core
(`src/main.cpp` -> `HookStatus::BuildCoreStatusJson`) or the Manager's
`/api/status` projection. Every expected hook must be present AND attached;
resolved-only license anchors must be present. Known degraded paths are
reported separately so the gate stays green for everything else while the
issue remains visible.

Usage:
    python tools/smoke_status.py <core_status.json> [--platform PLATFORM]
    python tools/smoke_status.py --url http://127.0.0.1:8080/api/status
    options: --ui / --no-ui (steamui hook expectation), --max-age SECONDS,
             --json (machine-readable report)

Exit codes: 0 = pass (warnings allowed), 1 = regression, 2 = input error.
"""

import argparse
import json
import sys
import time
import urllib.request

# Published hook keys expected from the steamclient module on every platform.
CORE_HOOKS = [
    "CheckAppOwnership",
    "ConfigStore_GetBinary",
    "IPCProcessMessage",
    "GetPackageInfo",
    "SpawnProcess",
    "OptedInMask",
    "BBuildAndAsyncSendFrame",
    "RecvPkt",
    "Steam_BGetCallback",
    "Steam_FreeLastCallback",
    "GetOrAddAppDataHook",
]

# Resolved-only anchors consumed by the license broadcast pipeline.
CORE_ANCHORS = ["MarkLicenseAsChanged", "ProcessPendingLicenseUpdates"]

# Known degraded paths: reported as warnings, never gate failures.
# - GetPackageInfo on Linux: the callee-fingerprint derive layer does not
#   publish it for the current ELF client yet (win/mac TOMLs have it).
# GetOrAddAppDataHook graduated to CORE_HOOKS: the NormalizeFunctionName alias
# (GetAppDataFromAppInfo -> GetOrAddAppData) is gated by the static shape probe
# (tools/verify_appdata_alias.py) and the real-machine hook-smoke jobs.
KNOWN_ISSUES = {}
KNOWN_ISSUES_BY_PLATFORM = {
    "linux": {"GetPackageInfo": "derive callee-fingerprint gap on current ELF client TOML"},
}

PLATFORM_BY_MODULE = {
    "steamclient64.dll": "windows",
    "steamclient.so": "linux",
    "steamclient.dylib": "macos",
}


def infer_platform(doc):
    module = str(doc.get("targetModule", "")).lower()
    for suffix, platform in PLATFORM_BY_MODULE.items():
        if module.endswith(suffix):
            return platform
    return None


def check_document(doc, platform, ui_expectation, max_age):
    """Returns (failures, warnings, passes) as lists of human-readable lines."""
    failures, warnings, passes = [], [], []

    def ok(msg):
        passes.append(msg)

    def fail(msg):
        failures.append(msg)

    def warn(msg):
        warnings.append(msg)

    if not isinstance(doc, dict):
        return ["document is not a JSON object"], [], []

    # -- schema / liveness ---------------------------------------------------
    if doc.get("active") is True:
        ok("active=true")
    else:
        fail("core is not active (active != true)")

    pid = doc.get("pid")
    if isinstance(pid, int) and pid > 0:
        ok(f"pid={pid}")
    else:
        fail(f"invalid pid: {pid!r}")

    for field in ("version", "commit"):
        if isinstance(doc.get(field), str) and doc[field]:
            ok(f"{field}={doc[field]}")
        else:
            fail(f"missing/empty {field}")

    ts = doc.get("timestamp")
    if isinstance(ts, (int, float)) and ts > 0:
        age = time.time() - ts
        if max_age is not None and age > max_age:
            fail(f"stale heartbeat: {age:.0f}s old (max {max_age}s)")
        else:
            ok(f"heartbeat age {age:.0f}s")
    else:
        fail(f"invalid timestamp: {ts!r}")

    # -- hooks ---------------------------------------------------------------
    hooks = doc.get("hooks")
    if not isinstance(hooks, dict):
        return failures + ["missing hooks object"], warnings, passes

    known = dict(KNOWN_ISSUES)
    known.update(KNOWN_ISSUES_BY_PLATFORM.get(platform, {}))

    expected = list(CORE_HOOKS)
    if ui_expectation == "required":
        expected.append("FillInAppOverview")
    elif ui_expectation == "optional":
        if hooks.get("FillInAppOverview") is True:
            ok("FillInAppOverview attached (optional on this platform)")
        else:
            warn("FillInAppOverview not attached (optional: steamui module may be absent)")

    for name in expected:
        if name in known:
            if hooks.get(name) is True:
                ok(f"{name} attached (known issue resolved?)")
            else:
                warn(f"{name} degraded: {known[name]}")
            continue
        value = hooks.get(name)
        if value is True:
            ok(f"hook {name} attached")
        elif value is False:
            fail(f"hook {name} present but NOT attached")
        else:
            fail(f"hook {name} missing from status document")

    for name, value in hooks.items():
        if not isinstance(value, bool):
            fail(f"hook {name} has non-boolean value {value!r}")
        if name not in expected and name not in known and name != "FillInAppOverview":
            warn(f"unexpected hook key {name} (new hook? update smoke expectations)")

    # -- anchors -------------------------------------------------------------
    anchors = doc.get("anchors")
    if not isinstance(anchors, dict):
        fail("missing anchors object")
    else:
        for name in CORE_ANCHORS:
            value = anchors.get(name)
            if isinstance(value, str) and value.startswith("0x") and len(value) > 2:
                ok(f"anchor {name} resolved at {value}")
            else:
                fail(f"anchor {name} unresolved (license broadcast pipeline broken)")
        if "CUtlMemoryGrow" not in anchors:
            warn("CUtlMemoryGrow unresolved (deferred by design; malloc fallback active)")

    return failures, warnings, passes


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("path", nargs="?", help="core_status.json path")
    parser.add_argument("--url", help="Manager /api/status endpoint instead of a file")
    parser.add_argument("--platform", choices=["windows", "linux", "macos"], help="override platform inference")
    ui = parser.add_mutually_exclusive_group()
    ui.add_argument("--ui", action="store_true", help="require the steamui FillInAppOverview hook")
    ui.add_argument("--no-ui", action="store_true", help="treat the steamui hook as optional")
    parser.add_argument("--max-age", type=float, default=300.0, help="max heartbeat age in seconds (0 disables)")
    parser.add_argument("--json", action="store_true", help="emit a machine-readable report")
    args = parser.parse_args()

    if not args.path and not args.url:
        parser.error("provide a core_status.json path or --url")

    try:
        if args.url:
            with urllib.request.urlopen(args.url, timeout=10) as resp:
                payload = json.load(resp)
            # /api/status wraps the core document; steamRunning gates liveness.
            if isinstance(payload, dict) and "core" in payload:
                if not payload.get("steamRunning"):
                    print("FAIL: Steam is not running (steamRunning=false)", file=sys.stderr)
                    return 1
                doc = payload["core"]
                # The Manager projection keeps hooks/anchors but drops pid /
                # timestamp; synthesize neutral values so schema checks pass.
                doc = dict(doc)
                doc.setdefault("active", doc.get("active", False))
                doc.setdefault("pid", payload.get("steamPid") or 1)
                doc.setdefault("timestamp", time.time())
                doc.setdefault("version", doc.get("installedVersion") or "unknown")
                doc.setdefault("commit", "manager-projection")
            else:
                doc = payload
        else:
            with open(args.path, "r", encoding="utf-8") as fh:
                doc = json.load(fh)
    except Exception as ex:  # noqa: BLE001 - input errors must exit 2 cleanly
        print(f"ERROR: cannot read status document: {ex}", file=sys.stderr)
        return 2

    platform = args.platform or infer_platform(doc)
    if platform is None:
        print("ERROR: cannot infer platform; pass --platform", file=sys.stderr)
        return 2

    if args.ui:
        ui_expectation = "required"
    elif args.no_ui:
        ui_expectation = "optional"
    else:
        # steamui lives in the host process on Windows/macOS; on Linux the
        # desktop-only steamui.so may be absent (and no TOML is published yet).
        ui_expectation = "optional" if platform == "linux" else "required"

    max_age = args.max_age if args.max_age > 0 else None
    failures, warnings, passes = check_document(doc, platform, ui_expectation, max_age)

    if args.json:
        print(json.dumps({
            "platform": platform,
            "pass": not failures,
            "failures": failures,
            "warnings": warnings,
            "passes": passes,
        }, indent=2))
    else:
        for line in passes:
            print(f"  ok   {line}")
        for line in warnings:
            print(f"  warn {line}")
        for line in failures:
            print(f"  FAIL {line}")
        print(f"{platform}: {len(passes)} ok, {len(warnings)} warn, {len(failures)} fail")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())