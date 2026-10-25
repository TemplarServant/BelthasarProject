---
name: add-regress-check
description: Pin a newly validated behavior into the one-command regression suite (tools/regress.py + tools/regress_checks.json). Use at the end of any engine milestone, after a fix is oracle-validated, or when a recipe makes the state a new check needs.
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
2. Every file the command needs goes in `inputs` — a missing input shows as SKIP
   naming the file. A STATE the check reads must come from a recipe
   (`tools/fixtures/<domain>.json` -> `tools/recipes/`, made by
   `python tools/fixtures.py make all`) or a public source
   (`tools/third_party_saves.json`, `python tools/fetch_saves.py`), never a local
   capture: `python tools/input_provenance.py` (the check
   `inputs-come-from-recipes-or-sources`) fails otherwise. Add the input to
   `tools/regress_inputs.json` (`--refresh-input-hashes`).
3. Run it TWICE; byte-identical signature both times. Nondeterministic → fix the
   check or don't add it.
4. Time it → tier + timeout_s. Add the entry, with at least one `mutations` entry
   (a deliberate defect in the code it covers).
5. `python tools/regress.py --only <name>` → PASS, and
   `python tools/regress.py --mutate <name>` → every mutation turns it red (a
   survivor means the check grades nothing there).
6. `python tools/regress.py --quick` → previous count + yours, 0 fail.
7. Document the check in `docs/TESTING.md` (one short paragraph: what it pins,
   which recipe seeds it, when re-pinning is legitimate).

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
