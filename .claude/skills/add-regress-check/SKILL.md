---
name: add-regress-check
description: Pin a newly validated behavior into the one-command regression suite (tools/regress.py + tools/regress_checks.json). Use at the end of any engine milestone, after a fix is oracle-validated, or when a capture that seeds a SKIP check becomes available.
---

# add-regress-check — pin it or it rots

The suite matches **stdout signatures**, never exit codes (most ct.exe self-checks
exit 0 even on mismatch). A check = a command + literal substrings that must ALL
appear in its output.

## Check entry schema (tools/regress_checks.json)

```json
{
  "name": "field-battletest-gato",
  "argv": ["build/Belthasar.exe", "--assets", "ct_assets.dat", "--battletest"],
  "env": {"CT_BATTLE_DEBUGEND": "1"},          // optional
  "inputs": ["ct_assets.dat", "saves/battle_re/gato_t0.raw.wram"],
  "expect": ["battletest: PASS (staging 18/18, stats OK (Gato HP 76), points 0F)"],
  "pin": ["battletest: PASS.*"],               // regexes --pin uses to re-derive expect
  "tier": "quick",                             // <20s observed -> quick, else full
  "timeout_s": 30,                             // ceil(observed x5), min 30
  "notes": "GATO D8->staging->module->debug-end->0x0F pinned"
}
```

## Procedure

1. Make the command deterministic and print a COUNTABLE success line
   (`thing: PASS (a N/N, b OK)` — counts, not adjectives). If you are adding a new
   selfcheck mode to Belthasar/ct.exe, copy the `--worldtest`/`--battletest` pattern.
2. Every file the command needs goes in `inputs` — missing input shows as SKIP
   naming the file (that is how not-yet-seeded checks stay discoverable).
3. Run it TWICE; byte-identical signature both times. Nondeterministic → fix the
   check or don't add it.
4. Time it → tier + timeout_s. Add the entry.
5. `python tools/regress.py --only <name>` → PASS.
6. `python tools/regress.py --quick` → previous count + yours, 0 fail.
7. Document the check in `docs/TESTING.md` (one short paragraph: what it pins,
   which capture seeds it, when re-pinning is legitimate).

## Kinds of pins (know which you are writing)

- **Byte-exact vs oracle** (best): the check itself compares against oracle values
  ("0 mismatches -- BYTE-EXACT", staged-byte compares).
- **fnv32 render canary** (brittle by design): pins whole-frame pixels; changes on
  ANY intentional render change → re-pin via `--pin` only after verifying against
  oracle framebuffers.
- **Pinned residual / under-investigation**: a known non-clean count (e.g. 128/136)
  frozen so it cannot rot further. Say so in `notes`; NEVER "fix" these by re-pinning.

## Excluding a candidate

If an existing harness mode has no stable success signature (statefulness
experiments, interactive diagnostics), add it to the json's top-level `excluded`
list with the observed reason instead of forcing a check.
