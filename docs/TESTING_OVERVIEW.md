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

### Current numbers (2026-10-10)

| | |
|---|---|
| Checks | **807** |
| Quick tier | 570 (the pre-commit tier, chosen by measured runtime) |
| Full tier | 235 (long oracle drives, the 512 x 3 asset verification, whole-demo comparisons) |
| Untiered | 2 (run only in the full run) |
| Excluded candidates | 2, each with the reason it has no stable signature |
| Checks with mutations | 635, carrying 1,680 mutations in all |
| Checks that must run alone | 55 (`exclusive`, run serially after the parallel batch) |

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

| Class | What it needs | Checks (2026-10-11) |
|---|---|---|
| **A: ROM only** | The verified ROM, `ct_assets.dat`, the built programs, and the files `tools/setup.py` generates. These boot from power-on, sweep ROM routines, render scenes, or drive the engine headless. | 580 of 818, together with B1 |
| **B1: recipe fixtures** | An oracle state at a game moment (a fight, a cutscene, a menu with a given party), made on your machine by `python tools/fixtures.py make all` from a recipe (`tools/fixtures/<domain>.json` -> `tools/recipes/`). 117 fixture files. | (counted in the 580) |
| **B2: private captures** | A state captured from the owner's own playthrough that no recipe reproduces yet. 68 files; they SKIP with their name in a fresh clone. | 238 |

How a fixture became a recipe's twin: the twin is made, every check that reads it runs, and it is kept when they pass
unchanged. A check whose expectation is specific to the old capture was re-pinned only when it compares the port with
the oracle in the same run (and that comparison holds on the twin) or after its pinned value was re-validated against
the oracle on the twin; each such re-pin carries a dated note in the check. The biggest private families left, and why
(details in each fixture's entry and in [ROADMAP.md](ROADMAP.md#11-checks-still-on-private-captures-l)):

- **The cathedral's three-member party** (`saves/battle_re/party3_field.*`, `bat9`, `party3_form`; ~60 checks): the
  closest recipe twin (`fx_party3_field`) passes 49 of 80; the rest show real port-vs-oracle differences on the new
  state (menu pictures, the door list, the reload cost, the follower ring) -- findings to fix, not re-pins.
- **The attract's first cut** (`saves/attract_cena_inicial.state`; 42 checks + 7 tools): those checks run the
  cartridge in real time, not under the rule, and pin frames of the owner's real-time run. The fix is to move their
  oracle side to the rule (where the power-on twin is exact) and re-anchor.
- **GATO's fight before the first A** (`saves/battle_re/gato_wait.state`; 35 checks): three checks depend on the sound
  processor's phase inside the capture, which a recipe does not reproduce; the WRAM twin passes the rest.

## The leak scanner

[`tools/release/scan_rom_leaks.py`](../tools/release/scan_rom_leaks.py) is not a regression check but belongs to the
same gate: it searches a tree for byte runs that occur in the ROM or in the extracted assets, and exits 1 if it
finds any. Run it before publishing anything. Details: [ROM.md](ROM.md#the-leak-scanner).

## Probes for the mods

The mods (widescreen, internal resolution, frames between) are outside the ROM mandate. They have their own
invariants, which are checked against the vanilla picture:

| Probe | What it checks |
|---|---|
| `CT_NATIVECHECK=1` (environment, `ct_field`) | The native frames-between drawer, at t = 256 and 1x, equals the composed frame pixel for pixel. Used by `field-native-frames-between-equal-the-composed-frame` and the overworld twin. |
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
