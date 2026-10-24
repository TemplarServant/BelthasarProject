#!/usr/bin/env python3
r"""Prove that checks can share ONE run, and give them the same env so tools/regress.py runs them once.

    python tools/regress_share.py NAME NAME [...] [--write] [--partition] [--argv JSON]

The checks must have the same argv and env_rule -- except the value of `--frames`, where the group runs to the
largest -- and their env is merged into one (a variable two of them set to different values is refused). Each check is run as declared and the group is run once with the merged env, and every
check must grade the merged run exactly as it grades its own:

  1. every expect line is in the merged output, and the check passes on its own;
  2. its pin regexes take the same lines from the merged output as from its own (`--pin` re-derives the same expect);
  3. every expect line occurs in as many output lines as in its own output -- no other check's probe prints a line
     that this check's expect matches, so a mutation that removes it from the check's own output removes it from the
     merged one too.

--argv '["python", "tools/frame_region_cmp.py", ...]' gives the group's command when it is not the checks' own with the
largest --frames (frame_region_cmp.py's several rectangles in one port run); the proof is the same.

The merged env only ADDS the other checks' probes (CT_*TRACE, CT_*MEMMON, ...): a variable that changes what the
program does (an input, an audio mode, a frame to stop at) differs between scenarios and is refused by the merge, or
fails 1-3. A check that stops earlier is equivalent only when nothing it grades or pins comes from its own last
frames or from where the run ends (an end-of-run summary): 1-3 refuse it otherwise. With --write each check's env
becomes the merged one (its own variables first), its `--frames` the group's, and its notes say so; the
suite (tools/regress.py, run_group) then runs the group once. Run `python tools/regress.py --mutate NAME ...` after.
Prints one line per check and exits 1 unless every check is equivalent.
"""
import argparse
import concurrent.futures
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regress  # noqa: E402


def merged_env(checks):
    env = {}
    for c in checks:
        for k, v in (c.get("env") or {}).items():
            if k in env and env[k] != v:
                return None, "%s=%r (%s) and %r (an earlier check)" % (k, v, c["name"], env[k])
            env.setdefault(k, v)
    return env, None


def frames_at(argv):
    """Index of the value of --frames in argv, or None."""
    return argv.index("--frames") + 1 if "--frames" in argv[:-1] else None


def merged_argv(checks):
    """The group's argv: the checks' own, with the largest --frames; None if they differ anywhere else."""
    def without_frames(a):
        i = frames_at(a)
        return a if i is None else a[:i - 1] + a[i + 1:]
    base = list(checks[0]["argv"])
    i = frames_at(base)
    for c in checks[1:]:
        if without_frames(c["argv"]) != without_frames(base) or (frames_at(c["argv"]) is None) != (i is None):
            return None
    if i is not None:
        base[i] = str(max(int(c["argv"][frames_at(c["argv"])]) for c in checks))
    return base


def lines_with(out, want):
    return sum(1 for l in out.splitlines() if want in l)


def why_not(c, sout, mout):
    """The reasons check c does not grade the merged output mout as it grades its own output sout ([] = equivalent)."""
    why = []
    if regress.grade(c, sout, 0)["status"] != "PASS":
        why.append("FAILs on its own run")
    lost = [w for w in c.get("expect", []) if w not in mout]
    if lost:
        why.append("%d expect line(s) not in the merged run, first %r" % (len(lost), lost[0][:100]))
    if c.get("pin") and regress.pin_from_output(c, sout) != regress.pin_from_output(c, mout):
        why.append("its pin regexes take other lines from the merged run")
    extra = [w for w in c.get("expect", []) if w in mout and lines_with(mout, w) != lines_with(sout, w)]
    if extra:
        why.append("%d expect line(s) also printed by another probe, first %r" % (len(extra), extra[0][:100]))
    return why


class Prover:
    """Runs each check as declared once, and each candidate group's merged command once."""

    def __init__(self, checks, jobs, argv=None):
        self.jobs, self.argv = jobs, argv
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
            self.solo = dict(zip([c["name"] for c in checks],
                                 ex.map(lambda c: regress.run_check(dict(c, expect=[])), checks)))
        self.runs = 0

    def verdict(self, group):
        """(merged result or None, {name: [reasons]}, refusal or None) for a candidate group."""
        argv = self.argv or merged_argv(group)
        if argv is None or any((c.get("env_rule") or {}) != (group[0].get("env_rule") or {}) for c in group):
            return None, {}, "another command (argv beyond --frames, or env_rule)"
        env, conflict = merged_env(group)
        if conflict:
            return None, {}, "the envs conflict: " + conflict
        self.runs += 1
        merged = regress.run_check(dict(group[0], argv=argv, env=env, expect=[], timeout_s=group_timeout(group)))
        mout = merged.get("out", "")
        return merged, {c["name"]: why_not(c, self.solo[c["name"]].get("out", ""), mout) for c in group}, None


def group_timeout(group):
    """The merged run does at most the members' work together: the sum of their timeouts bounds it."""
    return sum(c.get("timeout_s", 120) for c in group)


def write_group(data, group, argv=None):
    argv, (env, _) = argv or merged_argv(group), merged_env(group)
    names = [c["name"] for c in group]
    stamp, timeout = time.strftime("%Y-%m-%d"), group_timeout(group)
    for c in group:
        c["timeout_s"] = timeout
        own = c.get("env") or {}
        c["env"] = dict(own, **{k: v for k, v in env.items() if k not in own})
        was = " ".join(c["argv"])
        c["argv"] = list(argv)
        c["notes"] = (c.get("notes") or "") + (
            " ==== SHARES ITS RUN (%s, tools/regress_share.py): argv and env are the group's (%s), proved to grade this "
            "check exactly as its own run did (%s); tools/regress.py runs the group once, its timeout the members' sum."
            % (stamp, ", ".join(n for n in names if n != c["name"]),
               "it was: " + was if was != " ".join(argv) else "the same argv"))


def frames_of(c):
    i = frames_at(c["argv"])
    return int(c["argv"][i]) if i is not None else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("names", nargs="+")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--partition", action="store_true",
                    help="split the checks into groups that can each share a run (greedy, the longest runs first) "
                         "instead of proving them all as one group")
    ap.add_argument("--argv", help="the group's command, a JSON list (default: the checks' own, largest --frames)")
    ap.add_argument("--jobs", type=int, default=max(1, min(12, (os.cpu_count() or 2) - 2)))
    a = ap.parse_args()

    regress.ORACLE_CACHE = os.path.join(regress.ROOT, "build", "_oracle_cache")
    data = regress.load_checks()
    byname = {c["name"]: c for c in data["checks"]}
    unknown = [n for n in a.names if n not in byname]
    if unknown:
        sys.exit("share: unknown check(s): %s" % ", ".join(unknown))
    checks = [byname[n] for n in a.names]
    miss = [c["name"] for c in checks if regress.missing_inputs(c)]
    if miss:
        sys.exit("share: missing inputs for %s" % ", ".join(miss))
    t0 = time.time()
    argv = json.loads(a.argv) if a.argv else None
    if argv and a.partition:
        sys.exit("share: --argv names one group's command; it does not go with --partition")
    pv = Prover(checks, a.jobs, argv)

    if not a.partition:
        merged, why, refusal = pv.verdict(checks)
        if refusal:
            print("share: %s" % refusal)
            return 1
        for c in checks:
            print("share: %-60s %6.1fs  %s" % (c["name"], pv.solo[c["name"]]["secs"],
                                               "; ".join(why[c["name"]]) or "equivalent"))
        print("share: %d checks, %.1fs as declared, %.1fs as one run (%.1fs wall)"
              % (len(checks), sum(pv.solo[c["name"]]["secs"] for c in checks), merged["secs"], time.time() - t0))
        if any(why.values()):
            print("share: NOT EQUIVALENT -- nothing written")
            return 1
        groups = [checks]
    else:
        groups = []                                  # [(members, merged secs)]
        for c in sorted(checks, key=lambda c: -frames_of(c)):
            for k, (g, _) in enumerate(groups):
                merged, why, refusal = pv.verdict(g + [c])
                if not refusal and not any(why.values()):
                    groups[k] = (g + [c], merged["secs"])
                    break
            else:
                groups.append(([c], pv.solo[c["name"]]["secs"]))
        before = sum(pv.solo[c["name"]]["secs"] for c in checks)
        for g, secs in groups:
            print("share: group of %2d, %6.1fs as declared, %6.1fs as one run: %s"
                  % (len(g), sum(pv.solo[c["name"]]["secs"] for c in g), secs, ", ".join(c["name"] for c in g)))
        print("share: %d checks in %d runs: %.1fs as declared, %.1fs shared (%d merged runs tried, %.1fs wall)"
              % (len(checks), len(groups), before, sum(s for _, s in groups), pv.runs, time.time() - t0))
        groups = [g for g, _ in groups if len(g) > 1]
    if a.write and groups:
        for g in groups:
            write_group(data, g, argv)
        regress.write_checks(data)
        print("share: wrote %d group(s), %d checks" % (len(groups), sum(len(g) for g in groups)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
