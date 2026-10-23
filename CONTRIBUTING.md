# Contributing to BelthasarProject

Thank you for helping. BelthasarProject has a few firm rules, because a port that is faithful only by accident is no port
at all. Read this page before your first change. [docs/METHODOLOGY.md](docs/METHODOLOGY.md) explains the reasons
behind the rules.

## Ground rules

### 1. Never commit anything from the game

The repository contains no part of Chrono Trigger, and it has to stay that way.

- Never commit the ROM, `ct_assets.dat`, generated `src/data/*.h` or `src/xlat/*.c`, savestates (`.state`,
  `.lakewram`), RAM/VRAM/OAM dumps, battery saves (`.srm`), framebuffers, screenshots, traces, extracted graphics,
  text or dialogue. (The README's own pictures in `docs/media/` are the one exception; they come from
  `tools/release/make_media.py`.)
- Never paste byte dumps of ROM data or game text into code, comments, notes or issues. Describe a table by its
  address and layout (`CC/4700`, 23-byte records), not by its contents.
- If your tool needs game data, read it from the user's ROM at run time or generate it during setup.
- Before you open a pull request, run the leak scanner over your tree, with the extracted assets as an extra corpus:

  ```sh
  python tools/release/scan_rom_leaks.py . --corpus ct_assets.dat
  ```

  It must print `clean`. If it reports a coincidental match (a hardware table, a run of common values), explain it
  in the pull request; do not add it to the scanner's allowlist on your own. See [docs/ROM.md](docs/ROM.md).

### 2. Zero invention

For the vanilla game, the ROM, observed through the oracle and the disassembly, is the only source of truth.

- Derive every behaviour from the ROM routine that produces it and the oracle RAM that gates it. Cite both.
- No scene-specific branches (`if (scene == ...)`), no heuristics in place of the ROM's flag, no constants tuned by
  eye, no spot fixes. If your change breaks another scene, the shared mechanism is wrong: fix the mechanism.
- If you cannot derive it, stop and write down what is unknown and what would settle it. An honest gap is welcome.
  A plausible guess is not.
- Timing artefacts of the SNES's limits (lag, a handshake one frame either way) are not to be reproduced unless they
  change how the game works. Do not add emulation: no new code that runs ROM instructions on the vendored CPU, and no
  cycle-exact models. See [docs/DECISIONS.md](docs/DECISIONS.md), 2026-10-09.

### 3. Mods are opt-in and leave vanilla alone

A mod (widescreen, internal resolution, frames between, free sync, or a new one) may do things the cartridge does
not. It must:

- be off by default, and switched on only from the F1 overlay or `ct_settings.ini`;
- leave the vanilla game byte-identical when it is off; headless runs never read the settings, so the whole suite
  checks this for you;
- say in its overlay tooltip what it changes.

## Workflow

1. **Set up** from your own ROM ([README.md](README.md#quick-start)).
2. **Branch** from the default branch. One topic per branch.
3. **Find the truth first.** For a behaviour change, follow the playbooks in
   [`.claude/skills/`](.claude/skills/):
   - [re-derive](.claude/skills/re-derive/SKILL.md): reproduce on the oracle, find the RAM and its writer, and
     disassemble;
   - [re-implement](.claude/skills/re-implement/SKILL.md): port it gated on the same RAM;
   - [add-regress-check](.claude/skills/add-regress-check/SKILL.md): pin it.

   Write or update a dossier in `disasm/notes/` that cites addresses and measured values and lists open questions.
4. **Build** with `python tools/build.py` and trust its artifact check, not the banner. Close any running
   `Belthasar.exe` first: a locked executable is not replaced.
5. **Run the quick tier**: `python tools/regress.py --quick`. Run the full suite for anything touching shared
   machinery (the VM, the scheduler, loads, the battle loop, audio, `snes/`).
6. **Add a check** for the behaviour you fixed, in `tools/regress_checks.json`, with expected lines that come from the
   oracle, and at least one mutation (`python tools/regress.py --mutate <name>` must turn it red). A check that
   stays green under its mutation grades nothing. A state the check reads must come from a recipe
   (`tools/fixtures/<domain>.json`) or a public source (`tools/third_party_saves.json`), never a local capture:
   `python tools/input_provenance.py` (the check `inputs-come-from-recipes-or-sources`) fails otherwise.
7. **Never re-pin to make a red check green** without evidence from the oracle that the new output is right. Say in
   the pull request which checks moved and why.
8. **Open a pull request** that states what the ROM does (routine and RAM), how you measured it, which checks pin it,
   and anything left open.

If you change `snes/` (the oracle core), clear the oracle cache (`python tools/regress.py --clear-oracle-cache`) and
run the full suite. Any cached oracle output, including your own tools' caches, may describe the old oracle.

## Code style

- **Match the surrounding code.** C11 in `src/`, Python 3 (standard library only) in `tools/` and `assets/`. Follow
  the indentation, naming (`CtSomething` for engine entry points, `g_name` for globals) and line length of the file
  you are in.
- **Cite the ROM in comments.** A block that ports a routine names it in bank/address form (`C0/5A46`, `CD/0340`)
  and, where one exists, the dossier (`disasm/notes/field_tick.md`). State what RAM it reads and writes.
- **Keep ROM state in ROM addresses.** When the ROM keeps something in work RAM, the engine keeps it in the same WRAM
  byte, not in a host-side copy that can drift.
- **English is the project's language** for code, comments, notes and tool output (preferred, not enforced).
- **Commit messages** start with the area (`field:`, `battle:`, `menu:`, `world:`, `audio:`, `regress:`, `notes:`,
  `tools:`, `frontend:`) and say what the game does now, for example:
  `field VM: op 0xDA with no follower ends the visit (C0/3463 INX / CLC)`.

## Reporting a divergence

A divergence is any place where BelthasarProject does something the cartridge does not: a wrong picture, a frame of timing
that changes what happens, a script that stalls, wrong numbers in a fight.

Include:

- that your ROM passed the check (the programs refuse any other dump, so this is mostly a formality);
- the commit you built, your platform, and any non-default F1 options (try again with defaults: if it goes away, it
  is a mod issue);
- **where**: the location number, the story point, the party. Pressing F5 appends a line to `ct_debug_log.txt` with
  the frame, the location (`loc`) and the lead's position; that line is safe to paste;
- **how to get there**: the steps, or better a pad script from power-on or from a named save point;
- **what you expected**, preferably from the cartridge itself: run the same moment on the oracle
  (`ct.exe --peek`, see [docs/DEBUG_SURFACE.md](docs/DEBUG_SURFACE.md)) or on real hardware.

Do not attach savestates, battery saves, F5 snapshots (`ct_debug.*`) or RAM dumps to public issues: they contain
the game's data. A pad script and a location number are enough to reproduce most reports. If a screenshot helps,
keep it to what shows the problem.

## Where to ask

Open an issue on the repository for questions, proposals and divergence reports. For a larger change (simplifying a
priced mechanism such as the load or sound costs, splitting a large source file, a new mod), open an issue first to agree on the
approach. [docs/ROADMAP.md](docs/ROADMAP.md) lists what is wanted, with where to start.
