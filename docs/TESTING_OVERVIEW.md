# Testing overview

A short guide to how BelthasarProject is tested. The detailed reference (every flag, the pinning rules, per-check notes)
is [TESTING.md](TESTING.md). How a behaviour gets a check in the first place is in
[METHODOLOGY.md](METHODOLOGY.md#5-from-a-report-to-a-pinned-check).

## The suite

```sh
python tools/regress.py --quick          # the quick tier, before every commit
python tools/regress.py                  # everything
python tools/regress.py --only NAME ...  # some checks
python tools/regress.py --failed         # what failed last time
python tools/regress.py --check-inputs   # are the inputs present, with the right hashes?
python tools/preflight.py [--full]       # build, then inputs, then the suite; writes build/preflight.json
```

Every check is data in [`tools/regress_checks.json`](../tools/regress_checks.json): a name, a command line, an
environment, the input files it needs, the lines its output must contain, a tier, a timeout, notes, and usually
mutations. `tools/regress.py` has no per-check logic.

### Current numbers (2026-10-22)

| | |
|---|---|
| Checks | **954** (a clean-room clone, 2026-10-23: 953 pass; the 954th, `tree-tracks-no-game-file`, needs no ROM) |
| Quick tier | 616 (the pre-commit tier, chosen by measured runtime) |
| Full tier | 316 (long oracle drives, the 512 x 3 asset verification, whole-demo comparisons) |
| Untiered | 21 (run only in the full run) |
| Excluded candidates | 3, each with the reason it has no stable signature |
| Checks with mutations | 765, carrying 2,050 mutations in all |
| Checks that must run alone | 60 (`exclusive`: one at a time in declared order, in one worker beside the parallel batch) |

By default the suite runs min(12, CPUs - 2) checks in parallel. The slowest single check sets the floor.

## How a check passes

- **Expected lines, never exit codes.** A check PASSES when every literal string in its `expect` list appears in its
  combined stdout and stderr. Many self-checks print a mismatch and still exit 0, and some known baselines exit 1 on
  purpose, so exit codes are not used.
- **SKIP, not FAIL, on missing inputs.** A check whose declared input files are missing is reported as SKIP with the
  missing file named, and stays in the suite so the input stays discoverable. `--check-inputs` lists what is
  missing, with the `how` field from [`tools/regress_inputs.json`](../tools/regress_inputs.json) (where the file
  comes from, or how to regenerate it).
- **Exit status** of `regress.py` is 1 when any check FAILED. SKIPs do not fail the run.
- **Re-pinning.** `--pin NAME` rewrites a check's expected lines from its current output, using the check's `pin`
  regexes. `--repin-failed` does that for the last run's failures from their saved output. A re-pin is a claim that
  the new output is right, so it needs evidence from the oracle. A check that pins a known residual is never
  "fixed" by re-pinning.
- **Mutations.** `python tools/regress.py --mutate NAME` applies each of the check's declared mutations (an exact
  find-and-replace in a source file), rebuilds, re-runs the check, and requires it to FAIL. A mutation that survives
  means the check does not grade that line, whatever its notes say. The source is always restored afterwards. Do
  not edit files while a mutation run is going: the restore writes back the original.

## Speed

- **The oracle cache** (`build/_oracle_cache`). An oracle run (`ct.exe --peek`, `--monitor`, `--hdma`, `--fb`) is a
  pure function of the `ct.exe` binary, its arguments, its environment, and the contents of its input files and the
  ROM. Its outputs are cached on that key, so a change to the engine alone replays the cartridge side of every
  comparison. A change to anything `ct.exe` links misses the cache, as it must.
- **One oracle run for many frames** (`PEEK_SHOTS`) instead of one run per frame.
- **Longest first.** The parallel batch starts with the checks that took longest last time.

## Where a check's state comes from

| Class | What it needs | Checks (2026-10-22) |
|---|---|---|
| **A: ROM only** | The verified ROM, `ct_assets.dat`, the built programs, and the files `tools/setup.py` generates. These boot from power-on, sweep ROM routines, render scenes, or drive the engine headless. | 953, together with B1 and B3 |
| **B1: recipe fixtures** | An oracle state at a game moment (a fight, a cutscene, a menu with a given party), made on your machine by `python tools/fixtures.py make all` from a recipe (`tools/fixtures/<domain>.json` -> `tools/recipes/`). 233 fixture entries in 10 domain files; 228 of the 239 state files the checks read. | (counted in the 953) |
| **B2: private captures** | A state captured from the owner's own playthrough that no recipe reproduces yet, listed in `tools/private_captures.json`: empty since 2026-10-23 (the last, `saves/field_re/pre_scene_19f.{state,lakewram}`, is made by the recipe `lg_ocean_palace_19f`); a check on one would SKIP with its name in a fresh clone. The list only shrinks: `tools/input_provenance.py` (the check `inputs-come-from-recipes-or-sources`) fails on a check reading a state file from none of A / B1 / B2 / B3. | 0 |
| **B3: public battery saves** | Other players' 8 KB `.srm` files (zdiemer's set and Jackie's Game Saves: 78 files), downloaded by pinned URL and verified by SHA-256 by `python tools/fetch_saves.py` from `tools/third_party_saves.json`; Jackie's loaded states are recipes (`tools/recipes/jackie_*.json`). | (with B1) |

How a fixture became a recipe's twin: the twin is made, every check that reads it runs, and it is kept when they pass
unchanged. A check whose expectation is specific to the old capture was re-pinned only when it compares the port with
the oracle in the same run (and that comparison holds on the twin) or after its pinned value was re-validated against
the oracle on the twin; each such re-pin carries a dated note in the check. The last capture, `pre_scene_19f` (the Ocean Palace's
aftermath), moved on 2026-10-23 ([ROADMAP.md](ROADMAP.md#11-checks-still-on-private-captures-l)). The largest families moved on 2026-10-22,
for the record:

- ~~**The cathedral's three-member party**~~ (`saves/battle_re/party3_field.*`, `bat9*`, `party3_form`,
  `party3_freshsheet`; 81 checks): recipe-made since 2026-10-22 (`tools/fixtures/party3.json`). The owner's route was
  read off the capture's follower ring ($7F:0C00), which the earlier twin had missed; 27 checks were re-pinned, each
  re-graded against the oracle on the twin (dated notes in the checks).
- ~~**The attract's first cut**~~ (`saves/attract_cena_inicial.state`; 42 checks + 7 tools): off the private list since
  2026-10-22. Those checks ran the cartridge in real time and pinned frames of the owner's real-time run; their oracle
  side now runs the rule from recipe-made states (`tools/fixtures/attract_rule.json`), re-anchored.
- ~~**The rest family and the late game**~~: recipe-made since 2026-10-22 (`tools/fixtures/rest.json`,
  `tools/fixtures/lategame.json`, the late game from Jackie's public saves).
- ~~**GATO's fight before the first A**~~ (`saves/battle_re/gato_wait.state`): recipe-made since 2026-10-22
  (`tools/fixtures/gato.json`). The three checks that hung on the capture's sound phase showed a real port bug on the
  twin (the battle-entry upload posted two SPC frames early), fixed in the engine, not re-pinned.

## The leak scanner

[`tools/release/scan_rom_leaks.py`](../tools/release/scan_rom_leaks.py) is not a regression check but belongs to the
same gate: it searches a tree for byte runs that occur in the ROM or in the extracted assets, and exits 1 if it
finds any. Run it before publishing anything. Details: [ROM.md](ROM.md#the-leak-scanner).

## Probes for the mods

The mods (widescreen, internal resolution, frames between) are outside the ROM mandate. They have their own
invariants, which are checked against the vanilla picture:

| Probe | What it checks |
|---|---|
| `CT_NATIVECHECK=1` (environment, `Belthasar`) | The native frames-between drawer, at t = 256 and 1x, equals the composed frame pixel for pixel. Used by `field-native-frames-between-equal-the-composed-frame` and the overworld twin. |
| [`tools/native_fx_check.py`](../tools/native_fx_check.py) `[roll] [line] [gate]` | The same equality over the effects composed on the map: the staff roll's caption plane, the tutorial's colour window, the gate's iris. |
| [`tools/widescreen_check.py`](../tools/widescreen_check.py) `[scene ...]` | Runs a scene with widescreen 16:9 and with it off: the middle 256 columns of the wide picture must equal the 256 picture. It also reports the sides. Extra cases cover the caption plane, the colour window, a live battle (`forest`), a room wider than the picture (`5-edge`), and the room wrap turned off (`6-nowrap`). |
| `CT_WIDESHOTS=<prefix>` (`_EVERY`, `_FROM`) | Saves the presented wide picture every N frames, to sweep a long run by eye for side artefacts. A probe, not a check. |
| `CT_MOTCHECK=1` | Checks the per-pixel owners that the internal resolution and frames between rely on. Each frame is moved back to the previous frame's positions and compared with the previous frame as composed; what still differs is what the owners cannot explain. Used by `field-motion-owners-explain-the-attract-motion`, `battle-motion-owners-explain-the-fight` and others. |
| `CT_OVERLAY_SHOT=<bmp>[@frame][:tab]` | Saves the F1 overlay (or, with tab 9, the picture alone) headless. Used by `options-overlay-reads-the-settings-file` and the widescreen check. |

The suite runs headless, and headless runs never read `ct_settings.ini`, so every option is at its default there. All the other checks therefore guard the vanilla game. `options-free-sync-skips-the-hardware-waits` covers the free sync mode.

## Other instruments

The suite runs these, and they are also useful on their own:

- `ct.exe --vmdiff <state>`: lockstep against the oracle, first divergence;
- `ct.exe --vm ct_assets.dat`: the 512-scene actor check (baseline 512 OK, 0 mismatch);
- `ct.exe --sweep*`: micro-execution sweeps of ROM routines;
- `tools/*_cmp.py`: scenario comparisons against the oracle;
- `tools/battle_ab.py`: the battle census (run it in blocks; see [ROADMAP.md](ROADMAP.md) T4);
- `tools/attract_divergence.py`: the whole attract demo against the cartridge.

[DEBUG_SURFACE.md](DEBUG_SURFACE.md) lists every mode and environment variable.
