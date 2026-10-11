#!/usr/bin/env python3
"""Local preflight gate for the CT decomp (build + input kit + regression).

    python tools/preflight.py              # build + --check-inputs + regress --quick
    python tools/preflight.py --full       # same, but full regress suite
    python tools/preflight.py --no-build   # skip compile
    python tools/preflight.py --skip-inputs
    python tools/preflight.py --json build/preflight.json
    python tools/preflight.py --only vmdiff-bedslide sweepcollide

Exit codes:
  0  all requested stages passed
  1  build / inputs / regress failed
  2  usage / internal error

Writes a machine-readable JSON report (default build/preflight.json) so agents
and local hooks can consume the gate without scraping banners. Does not push
anywhere and does not require network access. ROM + capture kit must be present
locally (see tools/regress_inputs.json).
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_JSON = os.path.join(ROOT, "build", "preflight.json")


def run(cmd, timeout=None):
    """Run cmd from repo root; return dict with rc, secs, out (combined)."""
    t0 = time.time()
    try:
        p = subprocess.run(
            cmd,
            cwd=ROOT,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=timeout,
        )
        out = (p.stdout or "") + (p.stderr or "")
        return {"rc": p.returncode, "secs": time.time() - t0, "out": out}
    except subprocess.TimeoutExpired as e:
        out = ""
        for s in (e.stdout, e.stderr):
            if s:
                out += s.decode("utf-8", "replace") if isinstance(s, bytes) else s
        return {"rc": 124, "secs": time.time() - t0, "out": out + "\n[timeout]"}
    except OSError as e:
        return {"rc": 127, "secs": time.time() - t0, "out": str(e)}


def tail_lines(text, n=30):
    lines = [l for l in text.splitlines() if l.strip()]
    return lines[-n:]


def main():
    ap = argparse.ArgumentParser(description="CT decomp local preflight gate")
    ap.add_argument("--full", action="store_true",
                    help="run full regress suite (default is --quick)")
    ap.add_argument("--no-build", action="store_true", help="skip tools/build.py")
    ap.add_argument("--skip-inputs", action="store_true",
                    help="skip tools/regress.py --check-inputs")
    ap.add_argument("--only", nargs="+", metavar="NAME",
                    help="pass through to regress.py --only")
    ap.add_argument("--jobs", type=int, default=4, help="regress parallel jobs")
    ap.add_argument("--json", default=DEFAULT_JSON, metavar="PATH",
                    help="write report JSON here (default build/preflight.json)")
    ap.add_argument("--target", default="ct",
                    help="build.py target(s): default 'ct'; use 'all' for every target")
    args = ap.parse_args()

    report = {
        "started_at": datetime.now(timezone.utc).isoformat(),
        "root": ROOT,
        "stages": {},
        "ok": False,
    }
    failed = False

    # ---- build ----
    if not args.no_build:
        print("preflight: build ...", flush=True)
        build_cmd = [sys.executable, os.path.join("tools", "build.py")]
        if args.target and args.target != "all":
            build_cmd.append(args.target)
        r = run(build_cmd, timeout=600)
        report["stages"]["build"] = {
            "cmd": build_cmd,
            "rc": r["rc"],
            "secs": round(r["secs"], 2),
            "ok": r["rc"] == 0 and "Build OK" in r["out"],
            "tail": tail_lines(r["out"], 20),
        }
        if not report["stages"]["build"]["ok"]:
            failed = True
            print("preflight: BUILD FAILED (%.1fs)" % r["secs"], flush=True)
            for l in report["stages"]["build"]["tail"]:
                print("  |", l, flush=True)
        else:
            print("preflight: build OK (%.1fs)" % r["secs"], flush=True)
    else:
        report["stages"]["build"] = {"skipped": True, "ok": True}

    # ---- input kit ----
    if not failed and not args.skip_inputs:
        print("preflight: check-inputs ...", flush=True)
        cmd = [sys.executable, os.path.join("tools", "regress.py"), "--check-inputs"]
        r = run(cmd, timeout=120)
        # --check-inputs exits 1 on MISS; for preflight we treat MISS as soft warn
        # unless --strict-inputs is desired. Default: warn but continue so a machine
        # missing optional stairs/two_member still runs the rest of the suite.
        n_miss = r["out"].count("  MISS")
        n_mismatch = r["out"].count("  MISMATCH")
        n_extra = r["out"].count("  EXTRA")
        hard_fail = n_mismatch > 0 or n_extra > 0 or r["rc"] not in (0, 1)
        # rc=1 with only MISSes is soft; rc=0 is full kit
        report["stages"]["inputs"] = {
            "cmd": cmd,
            "rc": r["rc"],
            "secs": round(r["secs"], 2),
            "ok": not hard_fail,
            "miss": n_miss,
            "mismatch": n_mismatch,
            "extra": n_extra,
            "soft_miss_ok": True,
            "tail": tail_lines(r["out"], 25),
        }
        if hard_fail:
            failed = True
            print("preflight: INPUTS HARD-FAIL (mismatch/extra)", flush=True)
        elif n_miss:
            print("preflight: inputs OK with %d MISS (suite will SKIP those checks)" % n_miss,
                  flush=True)
        else:
            print("preflight: inputs OK (%.1fs)" % r["secs"], flush=True)
    elif args.skip_inputs:
        report["stages"]["inputs"] = {"skipped": True, "ok": True}

    # ---- regress ----
    if not failed:
        tier = "full" if args.full else "quick"
        print("preflight: regress --%s ..." % tier, flush=True)
        cmd = [sys.executable, os.path.join("tools", "regress.py"),
               "--jobs", str(args.jobs)]
        if not args.full:
            cmd.append("--quick")
        if args.only:
            cmd.append("--only")
            cmd.extend(args.only)
        # Full suite can take ~6+ minutes; quick ~1 min
        timeout = 3600 if args.full else 900
        r = run(cmd, timeout=timeout)
        # Parse summary line: "regress: N pass, M fail, K skip in S.Ss"
        summary = ""
        npass = nfail = nskip = None
        for line in r["out"].splitlines():
            if line.startswith("regress:") and "pass" in line:
                summary = line.strip()
                import re
                m = re.search(r"(\d+) pass, (\d+) fail, (\d+) skip", line)
                if m:
                    npass, nfail, nskip = int(m.group(1)), int(m.group(2)), int(m.group(3))
        ok = r["rc"] == 0 and (nfail is None or nfail == 0)
        report["stages"]["regress"] = {
            "cmd": cmd,
            "tier": tier,
            "rc": r["rc"],
            "secs": round(r["secs"], 2),
            "ok": ok,
            "pass": npass,
            "fail": nfail,
            "skip": nskip,
            "summary": summary,
            "tail": tail_lines(r["out"], 40),
        }
        if not ok:
            failed = True
            print("preflight: REGRESS FAILED (%.1fs) %s" % (r["secs"], summary), flush=True)
            for l in report["stages"]["regress"]["tail"]:
                if l.startswith("FAIL") or l.startswith("---") or l.startswith("    "):
                    print("  |", l, flush=True)
        else:
            print("preflight: regress OK (%.1fs) %s" % (r["secs"], summary), flush=True)

    report["ok"] = not failed
    report["finished_at"] = datetime.now(timezone.utc).isoformat()

    # ---- write JSON ----
    out_path = args.json
    try:
        os.makedirs(os.path.dirname(os.path.abspath(out_path)) or ".", exist_ok=True)
        with open(out_path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(report, f, indent=2)
            f.write("\n")
        print("preflight: report -> %s" % out_path, flush=True)
    except OSError as e:
        print("preflight: could not write JSON: %s" % e, flush=True)
        return 2

    if report["ok"]:
        print("preflight: ALL GREEN", flush=True)
        return 0
    print("preflight: FAILED", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
