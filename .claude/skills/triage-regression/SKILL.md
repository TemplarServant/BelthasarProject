---
name: triage-regression
description: Diagnose a red regression check or a user-reported behavior regression in the CT engine. Use when python tools/regress.py reports FAIL, when a previously-working scene broke, or when a claim in memory/commits disagrees with what the code does now.
---

# triage-regression — first divergence, then the mechanism

## Step 1 — trust the suite, reproduce the red

```
python tools/regress.py --only <failing-check>
```
The runner prints the missed expectation AND the actual nearby output. Read both.
Classify:
- **vmdiff check** (`first divergence frame = N of 300`): N EARLIER than the pinned
  frame = a real VM/mover/scheduler regression at that frame.
- **byte-exact check** (`K mismatches`): the printout usually names the first
  mismatching address/frame — that is your probe target.
- **fnv32 canary changed**: pixels moved. If you didn't intend a render change,
  you broke the renderer; diff a screenshot (`--selftest` + SHOT machinery /
  fb2png) against a pre-change build to SEE what moved.

## Step 2 — bisect YOUR change, not the check

Stash/revert your hunks one group at a time and re-run `--only <check>`. The
guilty hunk names the mechanism you disturbed. If the working tree is clean and
the red appeared over time: `git bisect` with `python tools/regress.py --only
<check>` as the test (checks are fast with --only).

## Step 3 — find the frame-level truth in the oracle

For vmdiff-class regressions, watch the diverging object around frame N:
```
VMDIFF_TRACK=<obj> ct_probe --vmdiff <the check's state>       # per-object trace
ct_probe --monitor "<addr>:LEN" --from <state> --frames N+30 --diff --writer
```
(`ct_probe` = your scratch COPY of build/ct.exe.) Compare oracle vs our trace at
the first diverging frame; the first differing WRITE names the routine.

## Step 4 — fix the MECHANISM (the iron rule)

A regression in scene X caused by work on scene Y means the SHARED mechanism is
wrong. The fix is structural — make the mechanism match the ROM so all scenes are
right at once. NEVER:
- add a scene/condition special-case to mask the symptom,
- re-pin the red baseline to green (`--pin` is only for intentional,
  oracle-validated changes),
- widen a timeout/expectation to make it pass.

## Step 5 — close the loop

Full `python tools/regress.py` green at the previous count. If the episode taught
a non-obvious fact (a wrong assumption, a fragile pattern), append it to the
relevant `disasm/notes/*.md` dossier so the next model doesn't re-learn it.

## Claim vs reality conflicts

If a commit/memory claims a number ("270/270 byte-exact") and the current build
prints something else consistently (3+ runs): do NOT assume either side. Pin the
OBSERVED value as an "UNDER INVESTIGATION" baseline (notes say both numbers),
report the conflict, and treat resolving it as its own derivation task. This has
happened (playermove 269/270 vs recorded 270/270) — the suite now freezes such
baselines so they cannot rot further while unresolved.
