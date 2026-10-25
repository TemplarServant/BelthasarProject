#!/usr/bin/env python3
"""regress.py -- one-command regression suite for the CT decomp.

Runs the checks declared in tools/regress_checks.json (data, not code) against the
already-built binaries in build/ and the oracle inputs in the repo root.

    python tools/regress.py               # full suite, min(12, CPUs-2) parallel jobs
    python tools/regress.py --quick       # tier "quick" only
    python tools/regress.py --list        # list checks without running
    python tools/regress.py --only vm-512-scene-actors vmdiff-bedslide
    python tools/regress.py --jobs 8
    python tools/regress.py --pin render-scene1-snapshot   # re-derive expect lines
    python tools/regress.py --build       # tools/build.py first (exit code + Build OK)
    python tools/regress.py --check-inputs  # verify tools/regress_inputs.json presence/sha256
    python tools/regress.py --refresh-input-hashes  # rewrite sha256/size for present files
    python tools/regress.py --mutate NAME...  # prove NAME grades its declared mutations, in labs at once
    python tools/regress.py --mutate-all --mutate-jobs 4

CRITICAL harness fact: most ct.exe self-checks return exit code 0 even on mismatch.
This suite therefore NEVER trusts exit codes -- a check PASSes iff every literal
substring in its "expect" list appears in the combined stdout+stderr.

All checks are read-only (verified: no included check writes files), so they run as
parallel subprocesses. A check declaring "exclusive": true (it writes a file) runs in
the exclusive lane: one at a time, in declared order, in one worker beside the batch.
Checks whose declared inputs are missing are SKIPped with the missing file named (they stay in the suite so the needed capture stays discoverable).

Exit status: 1 iff any check FAILed (SKIPs do not fail the suite).
--check-inputs exits 1 if any input is missing or its sha256 disagrees with the
manifest (when a pin is recorded). See docs/TESTING.md and tools/regress_inputs.json.
"""

import argparse
import concurrent.futures
import hashlib
import json
import os
import queue
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECKS_JSON = os.path.join(ROOT, "tools", "regress_checks.json")
INPUTS_JSON = os.path.join(ROOT, "tools", "regress_inputs.json")
TIMES_JSON = os.path.join(ROOT, "build", "_regress_times.json")   # each check's last duration: longest first
LAST_JSON = os.path.join(ROOT, "build", "_regress_last.json")     # the last run's failures, with output (--repin-failed)
ORACLE_CACHE = None      # set by main() unless --no-oracle-cache: build/_oracle_cache

sys.path.insert(0, os.path.join(ROOT, "tools", "ctlib"))
import oracle_cache  # noqa: E402
import mutlabs  # noqa: E402
sys.path.pop(0)


def _cache_real_run(env, timeout):
    """subprocess.run under the check's timeout (a cached oracle run of a check whose argv is ct.exe itself)."""
    def real(argv, **kw):
        return subprocess.run(argv, timeout=timeout, **kw)
    return real


def load_checks():
    with open(CHECKS_JSON, "r", encoding="utf-8") as f:
        return json.load(f)


def resolve_argv(argv, root=ROOT):
    """CreateProcess does not resolve 'build/ct.exe' against the subprocess cwd on
    Windows -- make argv[0] absolute against the repo root when it exists there."""
    if argv[0] in ("python", "python3"):
        return [sys.executable] + list(argv[1:])
    exe = os.path.normpath(os.path.join(root, argv[0]))
    if os.path.exists(exe):
        return [exe] + list(argv[1:])
    return list(argv)


def missing_inputs(check, root=ROOT):
    return [p for p in check.get("inputs", [])
            if not os.path.exists(os.path.join(root, p))]


def run_check(check, root=ROOT):
    """Returns dict: status PASS/FAIL/SKIP/ERROR, secs, note, out, first_miss.
    root: the tree the check runs in -- the repo, or a mutation lab (tools/ctlib/mutlabs.py), a copy of it."""
    t0 = time.time()
    miss = missing_inputs(check, root)
    if miss:
        return {"status": "SKIP", "secs": 0.0,
                "note": "missing input: " + ", ".join(miss), "out": ""}
    env = dict(os.environ)
    if ORACLE_CACHE:
        # the oracle cache (tools/ctlib/oracle_cache.py): a check's own oracle runs and its tools' (sitecustomize)
        env["CT_ORACLE_CACHE"] = ORACLE_CACHE
        env["PYTHONPATH"] = os.pathsep.join([os.path.join(root, "tools", "oracle_cache_site")]
                                            + [p for p in [os.environ.get("PYTHONPATH")] if p])
        if root != ROOT:
            # a lab's runs key as the repo's (oracle_cache._alias): the cartridge side of a mutant is the baseline's hit
            env["CT_ORACLE_CACHE_ALIAS"] = root + "|" + ROOT
    for k, v in (check.get("env") or {}).items():
        env[k] = str(v)
    # "env_rule": what a check needs only under the infinitely fast CPU's migration switches (CT_ORACLE_NMION_HOLD=1 AND
    # CT_ORACLE_IFAST=1, CLAUDE.md) because its premise changes there -- e.g. a lockstep whose port runs the rule's clock
    # needs the oracle under the rule too. Without both switches it is not read: the check runs exactly as declared.
    if os.environ.get("CT_ORACLE_NMION_HOLD", "1") != "0" and os.environ.get("CT_ORACLE_IFAST", "1") != "0":
        for k, v in (check.get("env_rule") or {}).items():
            env[k] = str(v)
    timeout = check.get("timeout_s", 120)
    try:
        # Popen + communicate instead of run(): on a timeout the WHOLE TREE is killed. A check whose argv is a
        # python tool spawns build\ct*.exe as a grandchild; run() kills only the python child, the exe survives as
        # an orphan, holds build\'s file lock, and every later build of a --mutate run fails "inconclusive".
        argv = resolve_argv(check["argv"], root)
        if ORACLE_CACHE and os.path.basename(argv[0]).lower() == "ct.exe" and oracle_cache.mode_of(argv)[0]:
            r = oracle_cache.run(argv, dict(cwd=root, env=env, capture_output=True, text=True, errors="replace"),
                                 _cache_real_run(env, timeout), cache=ORACLE_CACHE)
            out = (r.stdout or "") + (r.stderr or "")
            return grade(check, out, time.time() - t0)
        p = subprocess.Popen(argv, cwd=root, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace")
        try:
            so, se = p.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)], capture_output=True)
            else:
                p.kill()
            so, se = p.communicate()
            raise subprocess.TimeoutExpired(p.args, timeout, output=so, stderr=se)
        out = (so or "") + (se or "")
    except subprocess.TimeoutExpired as e:
        out = ""
        for s in (e.stdout, e.stderr):
            if s:
                out += s.decode("utf-8", "replace") if isinstance(s, bytes) else s
        return {"status": "FAIL", "secs": time.time() - t0,
                "note": "TIMEOUT after %ds" % timeout, "out": out,
                "first_miss": "(timed out -- no signature matched)"}
    except OSError as e:
        return {"status": "ERROR", "secs": time.time() - t0,
                "note": "cannot exec: %s" % e, "out": ""}
    return grade(check, out, time.time() - t0)


def grade(check, out, secs):
    for want in check.get("expect", []):
        if want not in out:
            return {"status": "FAIL", "secs": secs,
                    "note": "signature missing", "out": out, "first_miss": want}
    return {"status": "PASS", "secs": secs, "note": check.get("note_pass", ""),
            "out": out}


def nearby_lines(out, want, n=8):
    """Actual output lines near the missed expectation: lines sharing its first
    token (e.g. 'checkowcam:'), else the last n non-empty lines."""
    lines = [l for l in out.splitlines() if l.strip()]
    token = want.split()[0] if want.split() else ""
    hits = [l for l in lines if token and token in l]
    return (hits or lines[-n:])[:n]


def print_failure_detail(name, check, res):
    print("--- FAIL %s" % name)
    print("    cmd: %s" % " ".join(check["argv"]))
    if check.get("env"):
        print("    env: %s" % " ".join("%s=%s" % kv for kv in check["env"].items()))
    print("    first missing expectation:")
    print("      %r" % res.get("first_miss", ""))
    print("    actual nearby output:")
    near = nearby_lines(res.get("out", ""), res.get("first_miss", "") or "")
    if not near:
        print("      (no output)")
    for l in near:
        print("      | %s" % l)


def pin_from_output(check, out):
    """The check's new expect lines from an output: every line matching one of its pin regexes. (lines, unmatched)."""
    new_expect, problems = [], []
    for rx in check.get("pin") or []:
        matched = [l for l in out.splitlines() if re.search(rx, l)]
        if not matched:
            problems.append(rx)
            continue
        for l in matched:
            if l not in new_expect:
                new_expect.append(l)
    return new_expect, problems


def write_checks(data):
    # indent=2 + ensure_ascii=False = the format the file is ALREADY in. With json.dump's defaults a
    # one-line re-pin rewrote all 4902 lines (indent 2 -> 1) and escaped every non-ASCII character
    # (40 x U+26A0 -> ⚠), so the actual change was invisible in the diff and every pin carried a
    # whole-file rewrite with it.
    with open(CHECKS_JSON, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")


def print_pin_diff(name, old, new):
    print("regress: pinned %s" % name)
    for l in old:
        if l not in new:
            print("    - %s" % l)
    for l in new:
        if l not in old:
            print("    + %s" % l)
    if old == new:
        print("    (unchanged)")


def do_pin(data, names, jobs):
    """Re-pin NAMES: run them (in parallel, without expectations) and rewrite each one's expect lines."""
    checks = {c["name"]: c for c in data["checks"]}
    for name in names:
        if name not in checks:
            print("regress: no check named %r" % name)
            return 2
        miss = missing_inputs(checks[name])
        if miss:
            print("regress: cannot pin %s -- missing input: %s" % (name, ", ".join(miss)))
            return 2
        if not checks[name].get("pin"):
            print("regress: check %s has no 'pin' regexes" % name)
            return 2
    print("regress: running %d check(s) to re-pin..." % len(names))
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as ex:
        outs = dict(zip(names, ex.map(lambda n: run_check(dict(checks[n], expect=[])).get("out", ""), names)))
    return apply_pins(data, checks, outs)


def apply_pins(data, checks, outs):
    rc = 0
    for name, out in outs.items():
        check = checks[name]
        new_expect, problems = pin_from_output(check, out)
        if problems:
            print("regress: PIN FAILED %s -- no output line matched:" % name)
            for rx in problems:
                print("    %r" % rx)
            for l in nearby_lines(out, "", 12):
                print("    | %s" % l)
            rc = 1
            continue
        old = check.get("expect", [])
        check["expect"] = new_expect
        print_pin_diff(name, old, new_expect)
    write_checks(data)
    return rc


def do_repin_failed(data):
    """Re-pin every check that FAILED on the last suite run FROM THE OUTPUT THAT RUN SAVED -- nothing is run again.
    A failure with no pin regexes, or a timeout, is listed and left alone."""
    try:
        with open(LAST_JSON, encoding="utf-8") as f:
            last = json.load(f)
    except (OSError, ValueError):
        print("regress: no saved run (%s) -- run the suite first" % LAST_JSON)
        return 2
    checks = {c["name"]: c for c in data["checks"]}
    outs, skipped = {}, []
    for name, r in sorted(last.get("failed", {}).items()):
        c = checks.get(name)
        if c is None or not c.get("pin") or r.get("note", "").startswith("TIMEOUT") or r.get("status") == "ERROR":
            skipped.append("%s (%s)" % (name, "gone" if c is None else "no pin regexes" if not c.get("pin")
                                        else r.get("note") or r.get("status")))
            continue
        if c.get("argv") != r.get("argv") or (c.get("env") or {}) != (r.get("env") or {}):
            skipped.append("%s (its argv/env changed since that run -- --pin it)" % name)
            continue
        outs[name] = r.get("out", "")
    for sk in skipped:
        print("regress: NOT re-pinned: %s" % sk)
    if not outs:
        print("regress: nothing to re-pin from %s (run of %s)" % (LAST_JSON, last.get("when", "?")))
        return 1 if skipped else 0
    print("regress: re-pinning %d check(s) from the run of %s (no re-run)" % (len(outs), last.get("when", "?")))
    rc = apply_pins(data, checks, outs)
    return rc or (1 if skipped else 0)


def _build(root=ROOT):
    """tools/build.py of the tree at root, every target. (ok, its output, seconds)"""
    env = dict(os.environ)
    if root != ROOT:
        env.pop("CT_BUILD_DIR", None)    # a lab's checks run its build/ct*.exe: it builds there whatever the shell says
    t0 = time.time()
    p = subprocess.run([sys.executable, os.path.join(root, "tools", "build.py")], cwd=root, env=env,
                       capture_output=True, text=True, errors="replace", timeout=600)
    out = (p.stdout or "") + (p.stderr or "")
    ok = p.returncode == 0 and ("Build OK" in out or "BUILT" in out or "UP-TO-DATE" in out)
    return ok, out, time.time() - t0


def do_build():
    """Drive tools/build.py (honest artifact check). Prefer its exit code.

    build.py only prints 'Build OK' after per-target verification and fails if a
    ct*.exe from this build/ is still running (locked). Fall back message if the
    banner is missing despite exit 0.
    """
    print("regress: building (python tools/build.py)...")
    ok, out, secs = _build()
    print("regress: build %s (%.1fs)" % ("OK" if ok else "FAILED", secs))
    if not ok:
        for l in out.splitlines()[-25:]:
            print("    | %s" % l)
        print("regress: aborting (build.py failed). If a ct*.exe is running it holds "
              "the file lock — kill it and rebuild.")
    return ok


def _read_exact(path):
    with open(path, "r", encoding="utf-8", newline="") as f:
        return f.read()


def _write_exact(path, s):
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(s)


def _resolve_mutation(root, m):
    """The edits of mutation m in the tree at root: (stale, originals, resolved). stale is why the mutation cannot be
    applied (None when it can); originals [(path, text)] restore every file it touches; resolved [(path, find, replace)]
    carry the file's REAL line endings."""
    # A mutation is a SET of edits: one behaviour may live at more than one site, and an
    # edit that only covers one of them tests a path the check may never take. The first
    # full --mutate-all run caught exactly that: the "same-world skip" mutation patched only
    # the door-exit overworld entry and SURVIVED, because that scenario enters through the
    # other site. Single find/replace stays as sugar for the common one-site case.
    originals, resolved = [], []
    for e in m.get("edits") or [m]:
        path = os.path.join(root, e["file"])
        s = _read_exact(path)
        originals.append((path, s))
        # LINE ENDINGS: this repo's working copy is CRLF (see the line-endings trap in the
        # memory index) but a JSON "find" carries \n. A single-line find never notices; a
        # multi-line one matches ZERO times and the mutation reads STALE for a reason that
        # has nothing to do with the source drifting. Fit the pattern to the file instead:
        # try it verbatim, then with \n -> \r\n (translating "replace" the same way, so a
        # mutation never converts a CRLF file to LF and blows up the next git diff).
        find, repl = e["find"], e["replace"]
        if find not in s and "\n" in find:
            crlf = find.replace("\r\n", "\n").replace("\n", "\r\n")
            if crlf in s:
                find = crlf
                repl = repl.replace("\r\n", "\n").replace("\n", "\r\n")
        resolved.append((path, find, repl))
        n_hits = s.count(find)
        if n_hits != 1:
            return "'find' matches %d times in %s (expected 1)" % (n_hits, e["file"]), originals, resolved
    return None, originals, resolved


def _apply_mutation(resolved):
    for path, find, repl in resolved:    # apply every edit, re-reading as we go so two
        _write_exact(path, _read_exact(path).replace(find, repl))  # edits to the SAME file compose


def _verdict_lines(mi, m, v):
    """The report of mutant mi: v is ("STALE", reason) | ("BUILD", build output) | ("RUN", run_check's result)."""
    kind, x = v
    if kind == "STALE":
        return ["  [%d] STALE -- %s. The mutation text drifted from the source; re-derive it." % (mi, x)]
    if kind == "BUILD":
        return ["  [%d] BUILD FAILED under the mutation -- inconclusive" % mi]
    if x["status"] == "FAIL":
        lines = ["  [%d] KILLED   %s" % (mi, m.get("why", ""))]
        if x.get("first_miss"):
            lines.append("       missed: %s" % x["first_miss"][:120])
        return lines
    return ["  [%d] SURVIVED %s  <-- the check does NOT grade this" % (mi, m.get("why", ""))]


def _killed(v):
    return v[0] == "RUN" and v[1]["status"] == "FAIL"


def do_mutate(data, names, jobs=0):
    """MUTATION TESTING: prove each named check actually GRADES something.

    A check may declare the edits that are supposed to break it:

        "mutations": [
          {"file": "src/field.c", "find": "<exact substring>",
           "replace": "<substring>", "why": "restores the rear-up gate"}
        ]

    For each one we apply the edit, rebuild, re-run the check, and require it to
    FAIL. A mutation the check still PASSes through is SURVIVED -- the check does
    not grade that line, whatever its notes claim. The project's own rule, in the
    harness instead of in a person's head: a surviving mutation is an obligation of
    proof, not a verdict.

    jobs > 0: that many mutants at once, each in a mutation lab (_mutate_in_labs); the working tree is never edited.
    jobs == 0: one at a time in the working tree itself. The source file is ALWAYS restored (finally), and rebuilt,
    before returning.
    """
    by_name = {c["name"]: c for c in data["checks"]}
    todo = []
    for n in names:
        c = by_name.get(n)
        if not c:
            print("mutate: no such check '%s'" % n)
            return 2
        if not c.get("mutations"):
            print("mutate: check '%s' declares no mutations -- nothing to prove" % n)
            return 2
        todo.append(c)
    if jobs > 0:
        return _mutate_in_labs(todo, jobs)

    t0 = time.time()
    killed = survived = 0
    for check in todo:
        name = check["name"]
        base = run_check(check)
        if base["status"] != "PASS":
            print("mutate: %s -- BASELINE IS %s, fix that first" % (name, base["status"]))
            return 2
        print("mutate: %s -- baseline PASS, %d mutation(s)" % (name, len(check["mutations"])))
        for mi, m in enumerate(check["mutations"]):
            stale, originals, resolved = _resolve_mutation(ROOT, m)
            if stale:
                v = ("STALE", stale)
            else:
                try:
                    _apply_mutation(resolved)
                    v = ("RUN", run_check(check)) if do_build() else ("BUILD", "")
                finally:
                    for path, s in originals:
                        _write_exact(path, s)
            for l in _verdict_lines(mi, m, v):
                print(l)
            if _killed(v):
                killed += 1
            else:
                survived += 1
        do_build()   # back to the restored source
    print("mutate: %d killed, %d survived (%.1fs)" % (killed, survived, time.time() - t0))
    return 1 if survived else 0


def _mutate_in_labs(todo, jobs):
    """--mutate in `jobs` MUTATION LABS at once (tools/ctlib/mutlabs.py: copies of the working tree at
    build/_mutlabs/<k>, each with its own build/). Every lab is synced with the working tree first; then a check's
    baseline and, once it PASSes, each of its mutants is one job: take a free lab, apply the edits there, build it, run
    the check in it (cwd = the lab), restore the lab. The working tree is never edited, so other sessions and the player
    build and run in it meanwhile. The oracle cache is the repo's, keyed as the repo's (CT_ORACLE_CACHE_ALIAS), and
    ct_oracle.exe is the same bytes in every lab (CMakeLists.txt /PDBALTPATH), so a port mutant's cartridge side is a
    hit on its baseline's runs.

    The verdicts and their lines are the serial path's, printed per check in the order given once all are in (and one
    line per job as it lands). A baseline that does not PASS is reported and its mutants skipped; the other checks go
    on, and the exit status is 2."""
    t0 = time.time()
    pool = mutlabs.labs(ROOT, jobs)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        copied = list(ex.map(lambda lab: lab.sync(), pool))
    print("mutate: %d lab(s) in %s synced with the working tree (%d file(s) copied, %.1fs)"
          % (jobs, mutlabs.LABS_DIR, sum(copied), time.time() - t0), flush=True)
    free = queue.Queue()
    for lab in pool:
        free.put(lab)

    def job(check, mi):
        lab = free.get()
        try:
            if mi is None:
                ok, out, _ = _build(lab.root)
                return ("RUN", run_check(check, lab.root)) if ok else ("BUILD", out)
            stale, originals, resolved = _resolve_mutation(lab.root, check["mutations"][mi])
            if stale:
                return ("STALE", stale)
            try:
                _apply_mutation(resolved)
                ok, out, _ = _build(lab.root)
                return ("RUN", run_check(check, lab.root)) if ok else ("BUILD", out)
            finally:
                for path, s in originals:
                    _write_exact(path, s)
                    lab.touched(os.path.relpath(path, lab.root))
        finally:
            free.put(lab)

    try:
        with open(TIMES_JSON, encoding="utf-8") as f:
            times = json.load(f)
    except (OSError, ValueError):
        times = {}
    verdicts = {c["name"]: {} for c in todo}
    base_bad = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        pending = {}
        for c in sorted(todo, key=lambda c: -times.get(c["name"], 1e9)):    # the slowest checks' mutants start first
            pending[ex.submit(job, c, None)] = (c, None)
        try:
            while pending:
                done, _ = concurrent.futures.wait(pending, return_when=concurrent.futures.FIRST_COMPLETED)
                for fut in done:
                    check, mi = pending.pop(fut)
                    name = check["name"]
                    v = fut.result()
                    if mi is not None:
                        verdicts[name][mi] = v
                        print("mutate: %s %s" % (name, _verdict_lines(mi, check["mutations"][mi], v)[0].strip()),
                              flush=True)
                    elif v[0] == "BUILD" or v[1]["status"] != "PASS":
                        base_bad[name] = "BUILD FAILED" if v[0] == "BUILD" else v[1]["status"]
                        print("mutate: %s -- BASELINE IS %s, fix that first" % (name, base_bad[name]), flush=True)
                        if v[0] == "BUILD":
                            for l in v[1].splitlines()[-25:]:
                                print("    | %s" % l)
                    else:
                        print("mutate: %s -- baseline PASS, %d mutation(s) queued" % (name, len(check["mutations"])),
                              flush=True)
                        for i in range(len(check["mutations"])):
                            pending[ex.submit(job, check, i)] = (check, i)
        except BaseException:
            for fut in pending:      # Ctrl-C / a job's error: the queued jobs never start; a running one restores
                fut.cancel()         # its lab on the way out (job's finally)
            raise
    for lab in pool:
        lab.save()

    print()
    killed = survived = 0
    for check in todo:
        name = check["name"]
        if name in base_bad:
            print("mutate: %s -- BASELINE IS %s, fix that first" % (name, base_bad[name]))
            continue
        print("mutate: %s -- baseline PASS, %d mutation(s)" % (name, len(check["mutations"])))
        for mi, m in enumerate(check["mutations"]):
            v = verdicts[name][mi]
            for l in _verdict_lines(mi, m, v):
                print(l)
            if _killed(v):
                killed += 1
            else:
                survived += 1
    print("mutate: %d killed, %d survived (%d lab(s), %.1fs)" % (killed, survived, jobs, time.time() - t0))
    return 2 if base_bad else (1 if survived else 0)


_TEXT_INPUTS = (".py", ".c", ".h", ".json", ".md", ".txt")


def _sha256_file(path):
    # A TEXT input is pinned by its content, not its line endings: the working copy is CRLF or LF depending on
    # core.autocrlf and on which tool last wrote the file, and git stores LF either way -- so a raw hash read one file
    # MISMATCH after a fresh checkout that changed nothing (23 of the text inputs were LF, 2026-09-28).
    if path.lower().endswith(_TEXT_INPUTS):
        with open(path, "rb") as f:
            return hashlib.sha256(f.read().replace(b"\r\n", b"\n")).hexdigest()
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_inputs_manifest():
    if not os.path.isfile(INPUTS_JSON):
        return None
    with open(INPUTS_JSON, "r", encoding="utf-8") as f:
        return json.load(f)


def do_check_inputs():
    """Verify every tools/regress_inputs.json entry: present + sha256 when pinned.

    Exit 0 if all recorded pins match and nothing required is missing.
    Missing files with sha256=null (never recorded) count as WARN/MISS but still
    exit 1 so a fresh machine is forced to notice the capture kit gap.
    """
    man = load_inputs_manifest()
    if not man:
        print("regress: missing %s" % INPUTS_JSON)
        return 2
    # Also flag checks.json inputs not listed in the manifest.
    checks = load_checks()
    declared = set()
    for c in checks["checks"]:
        for p in c.get("inputs", []):
            declared.add(p.replace("\\", "/"))
    catalog = {e["path"].replace("\\", "/"): e for e in man.get("inputs", [])}
    n_ok = n_miss = n_mismatch = n_extra = 0
    print("regress: --check-inputs (%d catalogued, %d declared by checks)"
          % (len(catalog), len(declared)))
    for path in sorted(declared):
        if path not in catalog:
            print("  EXTRA  %s  (in regress_checks.json but not regress_inputs.json)" % path)
            n_extra += 1
    for path in sorted(catalog):
        e = catalog[path]
        abs_p = os.path.join(ROOT, path)
        want = e.get("sha256")
        how = (e.get("how") or "").strip()
        if not os.path.isfile(abs_p):
            print("  MISS   %s" % path)
            if how:
                print("         how: %s" % how)
            n_miss += 1
            continue
        got = _sha256_file(abs_p)
        size = os.path.getsize(abs_p)
        if want is None:
            print("  OK-NEW %s  (%d B, no pin yet — run --refresh-input-hashes)" % (path, size))
            n_ok += 1
            continue
        if got.lower() != want.lower():
            print("  MISMATCH  %s" % path)
            print("         want %s" % want)
            print("         got  %s  (%d B)" % (got, size))
            if how:
                print("         how: %s" % how)
            n_mismatch += 1
            continue
        print("  OK     %s  (%d B)" % (path, size))
        n_ok += 1
    print("regress: inputs %d ok, %d miss, %d mismatch, %d extra"
          % (n_ok, n_miss, n_mismatch, n_extra))
    return 1 if (n_miss or n_mismatch or n_extra) else 0


def do_refresh_input_hashes():
    """Rewrite sha256/size/present_when_recorded for every catalogued path that exists."""
    man = load_inputs_manifest()
    if not man:
        print("regress: missing %s" % INPUTS_JSON)
        return 2
    # Merge any checks.json inputs that are not yet catalogued.
    checks = load_checks()
    by_path = {e["path"].replace("\\", "/"): e for e in man.get("inputs", [])}
    for c in checks["checks"]:
        for p in c.get("inputs", []):
            p = p.replace("\\", "/")
            if p not in by_path:
                by_path[p] = {
                    "path": p,
                    "kind": "unknown",
                    "regenerable": False,
                    "how": "Auto-added from regress_checks.json; fill in provenance.",
                    "used_by": [],
                    "sha256": None,
                    "size": None,
                    "present_when_recorded": False,
                }
            if c["name"] not in by_path[p].setdefault("used_by", []):
                by_path[p]["used_by"].append(c["name"])
                by_path[p]["used_by"] = sorted(by_path[p]["used_by"])
    n_upd = n_miss = 0
    for path in sorted(by_path):
        e = by_path[path]
        abs_p = os.path.join(ROOT, path)
        if not os.path.isfile(abs_p):
            e["sha256"] = None
            e["size"] = None
            e["present_when_recorded"] = False
            n_miss += 1
            print("  MISS  %s" % path)
            continue
        e["sha256"] = _sha256_file(abs_p)
        e["size"] = os.path.getsize(abs_p)
        e["present_when_recorded"] = True
        n_upd += 1
        print("  pin   %s  %s" % (path, e["sha256"][:16]))
    man["inputs"] = [by_path[p] for p in sorted(by_path)]
    with open(INPUTS_JSON, "w", encoding="utf-8", newline="\n") as f:
        json.dump(man, f, indent=2, ensure_ascii=False)   # the file's own format (its notes carry ⚠ and em dashes)
        f.write("\n")
    print("regress: refreshed %d hashes (%d still missing) -> %s"
          % (n_upd, n_miss, INPUTS_JSON))
    return 0


def save_run(checks, results):
    """build/_regress_times.json (durations, for the order) and build/_regress_last.json (failures + output, for
    --failed and --repin-failed); a partial run updates only the checks it ran."""
    try:
        with open(TIMES_JSON, encoding="utf-8") as f:
            times = json.load(f)
    except (OSError, ValueError):
        times = {}
    try:
        with open(LAST_JSON, encoding="utf-8") as f:
            last = json.load(f)
    except (OSError, ValueError):
        last = {}
    failed = last.get("failed", {})
    for c in checks:
        r = results[c["name"]]
        if r["status"] != "SKIP":
            times[c["name"]] = round(r["secs"], 1)
        failed.pop(c["name"], None)
        if r["status"] in ("FAIL", "ERROR"):
            failed[c["name"]] = {"status": r["status"], "note": r.get("note", ""), "out": r.get("out", ""),
                                 "argv": c["argv"], "env": c.get("env") or {}}
    os.makedirs(os.path.dirname(TIMES_JSON), exist_ok=True)
    with open(TIMES_JSON, "w", encoding="utf-8") as f:
        json.dump(times, f, indent=0, sort_keys=True)
    with open(LAST_JSON, "w", encoding="utf-8") as f:
        json.dump({"when": time.strftime("%Y-%m-%d %H:%M:%S"), "failed": failed}, f)


def main():
    ap = argparse.ArgumentParser(description="CT decomp regression suite")
    ap.add_argument("--list", action="store_true", help="list checks, don't run")
    ap.add_argument("--quick", action="store_true", help="tier 'quick' checks only")
    ap.add_argument("--only", nargs="+", metavar="NAME", help="run only these checks")
    # DEFAULT PARALLELISM: the checks are independent subprocesses, so the suite is bound by the
    # machine, and 4 was leaving a 20-core box idle. MEASURED on the quick tier (343 checks, 486 s of
    # summed check time): 4 jobs 145 s, 12 jobs 88 s, 18 jobs 84 s -- the floor is the single slowest
    # check (verifyframe-oam-ourlist, 57 s), so past ~12 there is nothing left to overlap. Only 4% of
    # the tier is the serial "exclusive" tail, which is why the scaling is nearly linear until then.
    # cpu_count-2 keeps the box usable while a run is going; the flag still overrides.
    _cpus = os.cpu_count() or 4
    ap.add_argument("--jobs", type=int, default=max(4, min(12, _cpus - 2)),
                    help="parallel subprocesses (default: min(12, CPUs-2))")
    ap.add_argument("--pin", nargs="+", metavar="NAME", help="run NAME(s) in parallel and rewrite their expect "
                    "lines from their pin regexes (baseline re-pin)")
    ap.add_argument("--repin-failed", action="store_true",
                    help="re-pin every check that failed on the last run FROM ITS SAVED OUTPUT (no re-run); "
                         "prints each old -> new line")
    ap.add_argument("--failed", action="store_true", help="run only the checks that failed on the last run")
    ap.add_argument("--no-oracle-cache", action="store_true",
                    help="run every oracle (ct.exe --peek/--monitor/--hdma) for real (tools/ctlib/oracle_cache.py)")
    ap.add_argument("--clear-oracle-cache", action="store_true", help="empty build/_oracle_cache and exit")
    ap.add_argument("--build", action="store_true",
                    help="run tools/build.py first (honest artifact check)")
    ap.add_argument("--check-inputs", action="store_true",
                    help="verify tools/regress_inputs.json presence + sha256 pins")
    ap.add_argument("--refresh-input-hashes", action="store_true",
                    help="rewrite sha256/size in tools/regress_inputs.json from disk")
    ap.add_argument("--mutate", nargs="+", metavar="NAME",
                    help="prove these checks GRADE something: apply each declared mutation, "
                         "rebuild, and require the check to FAIL (a survivor is reported)")
    ap.add_argument("--mutate-all", action="store_true",
                    help="--mutate every check that declares mutations")
    # MUTATION LABS (tools/ctlib/mutlabs.py): each mutant builds and runs in its own copy of the working tree, so N run at
    # once and the tree itself is never edited. MEASURED (4 checks, 23 mutations, 16 of them built and run, 20 CPUs,
    # 2026-10-16): in place 686 s; 4 labs 239 s; 8 labs 194 s -- a mutant is a build on every core plus a check, so the
    # labs share the compiler past that. CPUs/2 capped at 8; a lab is ~0.7 GB on disk.
    ap.add_argument("--mutate-jobs", type=int, default=max(2, min(8, _cpus // 2)), metavar="N",
                    help="mutants at once, each in a lab at build/_mutlabs/<k> (default min(8, CPUs/2)); 0 = one at a "
                         "time in the working tree itself (the old path)")
    args = ap.parse_args()

    if args.check_inputs:
        sys.exit(do_check_inputs())
    if args.refresh_input_hashes:
        sys.exit(do_refresh_input_hashes())

    global ORACLE_CACHE
    cache_dir = os.path.join(ROOT, "build", "_oracle_cache")
    if args.clear_oracle_cache:
        import shutil
        shutil.rmtree(cache_dir, ignore_errors=True)
        print("regress: emptied %s" % cache_dir)
        sys.exit(0)
    if not args.no_oracle_cache:
        ORACLE_CACHE = cache_dir

    data = load_checks()
    checks = data["checks"]

    if args.repin_failed:
        sys.exit(do_repin_failed(data))
    if args.pin:
        sys.exit(do_pin(data, args.pin, args.jobs))

    if args.mutate or args.mutate_all:
        names = args.mutate if args.mutate else [c["name"] for c in checks if c.get("mutations")]
        if not names:
            print("mutate: no check declares mutations yet")
            sys.exit(2)
        sys.exit(do_mutate(data, names, max(0, args.mutate_jobs)))

    if args.quick:
        checks = [c for c in checks if c.get("tier") == "quick"]
    if args.failed:
        try:
            with open(LAST_JSON, encoding="utf-8") as f:
                failed = set(json.load(f).get("failed", {}))
        except (OSError, ValueError):
            failed = set()
        checks = [c for c in checks if c["name"] in failed]
        print("regress: %d check(s) failed on the last run" % len(checks))
    if args.only:
        byname = {c["name"]: c for c in checks}
        unknown = [n for n in args.only if n not in byname]
        if unknown:
            print("regress: unknown check(s): %s" % ", ".join(unknown))
            print("known: %s" % ", ".join(sorted(byname)))
            sys.exit(2)
        checks = [byname[n] for n in args.only]

    if args.list:
        w = max(len(c["name"]) for c in checks) if checks else 4
        for c in checks:
            print("%-*s  %-5s  %s" % (w, c["name"], c.get("tier", "?"),
                                      " ".join(c["argv"])))
        print("(%d checks; excluded: %d -- see tools/regress_checks.json)"
              % (len(checks), len(data.get("excluded", []))))
        sys.exit(0)

    if args.build and not do_build():
        sys.exit(1)

    t0 = time.time()
    results = {}
    # LONGEST FIRST (the last run's durations, build/_regress_times.json; a check never timed counts as long): the wall
    # time of a parallel batch is bound below by its slowest check, and a slow check submitted last ran alone at the end.
    try:
        with open(TIMES_JSON, encoding="utf-8") as f:
            times = json.load(f)
    except (OSError, ValueError):
        times = {}
    par = sorted((c for c in checks if not c.get("exclusive")), key=lambda c: -times.get(c["name"], 1e9))
    ser = [c for c in checks if c.get("exclusive")]

    def report(c, r):
        results[c["name"]] = r
        print("%-4s %s (%.1fs)" % (r["status"], c["name"], r["secs"]), flush=True)

    # THE EXCLUSIVE LANE: the exclusive checks run one at a time, in their declared order, in ONE worker of the pool,
    # alongside the parallel batch -- not after it. "exclusive" means a check that writes a file, and the files they
    # write (build/_nmsfx.raw, build/_svsfx.srm, the battleshot dumps ...) are shared only among THEMSELVES: audited
    # 2026-10-16, no read-only check names any of them, and none is graded on wall-clock time (all CT_NOPACE). Serial
    # after the batch they were 112 s of the quick tier's 272 s.
    def lane():
        for c in ser:
            report(c, run_check(c))

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        lane_fut = ex.submit(lane) if ser else None       # first: the lane is the longest single job of the batch
        futs = {ex.submit(run_check, c): c for c in par}
        for fut in concurrent.futures.as_completed(futs):
            report(futs[fut], fut.result())
        if lane_fut:
            lane_fut.result()
    total = time.time() - t0
    save_run(checks, results)
    if ORACLE_CACHE:
        # 24 GB: the cap was 8 and sat full (2026-10-16), so every run evicted the entries of the binaries before it -- a
        # branch switch or a reverted change came back to a cold cache. One generation of the quick tier is ~0.13 GB.
        kept, size = oracle_cache.prune(ORACLE_CACHE, int(float(os.environ.get("CT_ORACLE_CACHE_GB", "24")) * (1 << 30)))
        print("regress: oracle cache %d entries, %.2f GB (CT_ORACLE_CACHE_GB caps it, default 24)" % (kept, size / (1 << 30)))

    # failure details first, then the aligned table in declaration order
    for c in checks:
        r = results[c["name"]]
        if r["status"] in ("FAIL", "ERROR") and "first_miss" in r:
            print_failure_detail(c["name"], c, r)

    print()
    w = max(len(c["name"]) for c in checks) if checks else 4
    for c in checks:
        r = results[c["name"]]
        note = r.get("note", "")
        print("%-5s %-*s %7.1fs  %s" % (r["status"], w, c["name"], r["secs"], note))
    npass = sum(1 for r in results.values() if r["status"] == "PASS")
    nfail = sum(1 for r in results.values() if r["status"] in ("FAIL", "ERROR"))
    nskip = sum(1 for r in results.values() if r["status"] == "SKIP")
    print("regress: %d pass, %d fail, %d skip in %.1fs" % (npass, nfail, nskip, total))
    sys.exit(1 if nfail else 0)


if __name__ == "__main__":
    main()
