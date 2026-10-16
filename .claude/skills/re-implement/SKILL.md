---
name: re-implement
description: Implement a derived Chrono Trigger mechanism in the C engine, mandate-safe. Use AFTER re-derive produced a cited dossier (or one exists in disasm/notes/). Covers the forbidden-pattern checklist, the build+regress validation loop, pinning a new check, and the commit format.
---

# re-implement — from dossier to byte-validated C

**Precondition:** a dossier in `disasm/notes/` (or MEMORY note) with ROM/oracle
citations covers the mechanism. If any part you need is in its "Open questions",
go back to `re-derive` for that part first. If it cannot be derived: STOP and
report the boundary — do NOT approximate.

## Forbidden patterns (automatic reject — from CLAUDE.md, all really happened)

1. **Scene gates:** `if (sceneNo == X)`, per-scene constants/branches. The ROM has
   ONE general machine. If your fix needs a scene check, the mechanism is wrong.
2. **Stand-in heuristics:** e.g. `obj != playerObj` when the ROM branches on a
   per-object RAM flag ($1B80, $7F0B00, $1C81, $1C80, $1100 mode...). Find the flag,
   gate on THAT.
3. **Eyeballed constants:** every constant comes from the ROM or an observed oracle
   value, cited in a comment like `// C0/0617` or `// oracle: $1A00=0x40 mid-D9`.
4. **Spot fixes:** a regression in scene X caused by work on scene Y means the SHARED
   mechanism is wrong. Fix the mechanism so all scenes become correct at once.

## Implementation loop

1. **Re-verify each dossier claim at the site** you are about to change (fresh
   dis.py at the cited address; a quick oracle probe if cheap). Dossiers have been
   wrong before — the disasm is the truth.
2. Implement, matching the surrounding code style (dense, terse comments only for
   constraints the code can't show). New code paths must be INERT unless the
   canonical trigger fires (so existing baselines cannot move).
3. **Build honestly:** `tools\build.bat` — it prints a per-target table and
   `Build OK` only when every target verified. If it says a build/ct*.exe is
   RUNNING, close it (ask the user if it's theirs) — never test a stale exe.
4. **Quick tier after every stage:** `python tools/regress.py --quick` (~2 min after a
   port edit, measured 2026-10-16; ~3 min cold after an edit to the oracle itself --
   snes/*.c, src/main.c, ct_cpu_infra.c -- which the oracle cache cannot replay).
   Must stay `N pass, 0 fail`.
5. **Full suite before finishing:** `python tools/regress.py` (~12 min after a port
   edit, measured 2026-10-16). The pass
   count must equal the previous count (+ any check you added), 0 fail. The fnv32
   render canaries must be UNCHANGED unless your change intentionally altered
   pixels (then see re-pin rules below).
6. **Pin your work:** add a deterministic check to `tools/regress_checks.json`
   (see the `add-regress-check` skill) so the behavior can never silently rot.

## Re-pin rules (baselines)

- `python tools/regress.py --pin NAME` rewrites NAME's expectation from the current
  output. Allowed ONLY for an intentional, oracle-validated change (e.g. a render
  fix you verified against oracle framebuffers). FORBIDDEN as a way to make a red
  check green — a red check means your change altered behavior; fix or revert.
- Checks marked "UNDER INVESTIGATION" in their notes are pinned known-bad baselines.
  Never re-pin them without resolving the investigation.

## On any suite FAIL

- The runner prints the missed expectation + nearby actual output. For vmdiff
  checks, an EARLIER first-divergence frame = regression in VM/mover land.
- Bisect YOUR hunks (git stash / selective revert), not the check.
- If the failing scene is unrelated to your target scene → forbidden pattern #4:
  find the shared mechanism.

## Commit format (commit straight to main at milestones — authorized)

```
area: what landed -- key mechanism detail

Body: the mechanism with ROM citations (C0/xxxx), what was validated
(suite line, e.g. "regress: 45 pass, 0 fail, 2 skip"), boundaries left
open. End with:
Co-Authored-By: <model credit line>
```
Never claim byte-exact/270-of-270 in a commit unless the suite line proves it —
recorded claims that disagreed with reruns cost days.
