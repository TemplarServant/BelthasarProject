<!-- CONTRIBUTING.md, "Workflow", step 8. Delete what does not apply. -->

## What the ROM does

<!-- The routine (bank/address, e.g. C0/5A46) and the RAM that gates it, as the oracle reads it; the dossier in
disasm/notes/. For a change that is not behaviour (tools, docs, a mod, the build), say so instead. -->

## How it was measured

<!-- --vmdiff's first divergence, the oracle runs, the commands. -->

## Checks

<!-- The `regress:` summary line of the quick tier (the full suite for shared machinery), the checks that pin this and
their mutations, and every check that moved and why. -->

## Open

<!-- What is still unknown, and what would settle it. -->

---

- [ ] `python tools/release/scan_rom_leaks.py . --corpus ct_assets.dat` prints `clean`
- [ ] No scene-specific branch, no heuristic in place of the ROM's flag, no constant tuned by eye
- [ ] A mod is off by default, leaves the vanilla game byte-identical, and says in its tooltip what it changes
- [ ] No check re-pinned to turn it green without the oracle's evidence
