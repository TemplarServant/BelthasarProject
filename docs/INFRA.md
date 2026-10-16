# Infrastructure guide

How this repo is built, tested, laid out, and validated against the oracle. Companion
references: [`DEBUG_SURFACE.md`](DEBUG_SURFACE.md) (every ct.exe CLI mode + every debug
env var) and [`TESTING.md`](TESTING.md) (regression tiers and pinned baselines).

---

## Build

**`tools\build.bat`** — the historical driver: VS 2022 Build Tools (`vcvars64.bat`) +
the bundled CMake/Ninja, configure + compile into `build\`. Targets (from
`CMakeLists.txt`): `ct.exe` (headless oracle harness, always built);
`ct_viewer.exe`, `ct_field.exe`, `ct_world.exe` (SDL2 apps, built **only** when
`third_party/SDL2` is present — fetch once with `tools\get_sdl2.ps1`).

**`tools/build.py`** — the verifying replacement driver (design):

- **Honest per-target verification.** After the CMake/Ninja run it checks each expected
  artifact in `build\` actually exists and was freshly (re)linked, instead of trusting
  the driver's exit code plus a blanket "Build OK" banner. Success is reported (and the
  exit code set) *per target*.
- **Single-target argument.** `python tools/build.py ct_field` builds and verifies just
  that target; another ct*.exe running (the player's `ct_field` while `ct.exe` is rebuilt) does not block it.
- **A second tree.** `CT_BUILD_DIR=build_alt python tools/build.py ct_field` builds into `build_alt\` (gitignored):
  exes to test with while the player's `build\ct_field.exe` is running. The suite and the tools keep reading `build\`.
- **The three historical false-"Build OK" traps it catches** (all observed in this
  project; `build.py`'s own header is authoritative once it lands):
  1. **Locked exe.** With `ct_field.exe` still running, the link cannot replace the
     binary — yet "Build OK" was printed while the on-disk exe stayed the OLD build
     (documented in the project memory: *"build.bat 'Build OK' LIES when ct_field.exe
     is locked"*).
  2. **SDL2 silently absent.** Without `third_party/SDL2`, CMake *configures the SDL
     targets away* (`STATUS "SDL2 not in third_party/SDL2: skipping..."`) — the build
     "succeeds" but `ct_field.exe`/`ct_world.exe`/`ct_viewer.exe` are stale or missing.
  3. **Unconditional banner.** `build.bat`'s final `echo Build OK: build\ct.exe` names
     only `ct.exe` and never verifies **any** artifact's existence or freshness.

Rule of thumb inherited from those traps: after any build, trust the **artifact check**,
never the banner.

**Linux (gcc/clang) — a compile check, not the reference build.** `tools/build.py` is
Windows-only, but plain CMake builds all four targets on Linux: install `libsdl2-dev`
(the SDL2 targets fall back to the system SDL2 when `third_party/SDL2` is absent), then
`cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j`.
The binaries have no `.exe` suffix; `regress_checks.json` calls `build/ct*.exe`, so link
them (`ln -sf ct build/ct.exe`, same for the others) before `python tools/regress.py`.
Without the ROM and `ct_assets.dat` nearly every check SKIPs. gcc is stricter than MSVC
about two things MSVC lets through: a call to a function declared later (an implicit
declaration: add a prototype above the caller) and a hex constant ending in `E` glued
to `+`/`-` (`0x96DE+SLOT` is one invalid preprocessing number: write `0x96DE + SLOT`).

## Regression suite

**`python tools/regress.py`** — the one-command suite. Checks are declared as *data* in
`tools/regress_checks.json` and run against the already-built binaries in `build/` plus
the oracle inputs in the repo root.

- **Exit codes are never trusted.** Most ct.exe self-checks return 0 even on mismatch
  (see the exit-code discipline table in
  [`DEBUG_SURFACE.md`](DEBUG_SURFACE.md#3-reading-the-results--exit-code-discipline--savestate-formats));
  a check PASSes iff every literal substring in its `expect` list appears in the
  combined stdout+stderr.
- **Tiers.** `--quick` runs only the `tier: "quick"` checks; the default run is the
  full suite (min(12, CPUs-2) parallel jobs, `--jobs N` to change; `exclusive` checks run one at a time in
  their own lane beside the batch).
- **Pinning.** `--pin <check>` re-derives a check's `expect` lines from the current
  output (deterministic hashes like the `--lz`/`--datonly` FNV-32 make good pins).
- Missing inputs → **SKIP** (named), not FAIL; the suite exits 1 iff any check FAILed.
- **Input kit.** `tools/regress_inputs.json` lists every path declared in
  `regress_checks.json` with `sha256`, size, kind, regenerable flag, and `how` to
  obtain it. `python tools/regress.py --check-inputs` verifies presence + hashes
  (exit 1 on MISS/MISMATCH); `--refresh-input-hashes` rewrites pins from disk.
- **Preflight gate.** `python tools/preflight.py` (or `tools\preflight.ps1`) runs
  build → `--check-inputs` → `regress --quick` (or `--full`) and writes
  `build/preflight.json`. Soft-MISSes on optional captures do not hard-fail the
  gate; hash mismatches and regress FAILs do. No CI service is required — this is
  the local/agent one-command gate.
- **Memory index.** [`MEMORY.md`](../MEMORY.md) is the agent/human nav map (docs,
  skills, oracle one-liners, gotchas).
- `--list`, `--only <names>`, `--build` (drive the build first) round out the surface.

Tier contents and the meaning of each pinned baseline: [`docs/TESTING.md`](TESTING.md).

## Repo layout

| Path | What it is |
|---|---|
| `src/` | The C engine + harness. `scene.c` field-scene renderer (load ROM/.dat → one compose path); `ct_vm.c` event VM + scheduler + movers + textbox/naming; `collision.c`/`player.c`/`move.c` byte-exact ROM routine ports; `world.c` overworld camera/mover/streamer/ambient VM; `field.c` the interactive app (`ct_field.exe`); `ct_world.c`/`viewer.c` SDL viewers; `main.c` the ct.exe oracle harness; `assets.c` ct_assets.dat loader; `lz.c` the LZ decompressor. |
| `snes/` | Vendored **LakeSnes** oracle core (the embedded emulator). Do not edit casually — every validator's ground truth runs on it, and any change to its state serialization silently invalidates every `.state` on disk. |
| `assets/` | Build-time extraction pipeline (Python, dependency-free): ROM → `ct_assets.dat` (`extract_assets.py`, `lz.py`, `gfx.py`, `palettes.py`, `sprites.py`, `maps.py`, `text.py`, `script.py`...). The engine is ROM-independent at runtime; `ct.exe --datverify` proves the bridge byte-identical. |
| `disasm/`, `disasm/notes/` | The RE substrate: annotated 65816 traces, per-subsystem notes (`events.md`, `scenes.md`, `sprites.md`, `collision.md`...). Trace/dialogue artifacts are gitignored (ROM-derived, regenerable). |
| `tools/` | Canonical tooling: `build.bat`/`build.py`, `regress.py` + `regress_checks.json`, `dis.py`, `s9x_to_lake.py`, `disasm65816.py`, `get_sdl2.ps1`, `ct.py` (agent-facing wrapper), and the `ctlib/` helper library. One-off probe scripts use the `tools/_*.py` underscore convention and are gitignored (tracked exceptions like `tools/owbpal0.py` stay tracked). |
| `build/` | CMake/Ninja output (gitignored): `ct.exe`, `ct_field.exe`, `ct_world.exe`, `ct_viewer.exe`. |
| `saves/` | ROM-derived savestates/captures (gitignored; the regenerable ones come from recipes: `python tools/fixtures.py make all`). |
| repo root | **Scratch capture convention:** oracle work dumps its debris at the root — `.state` savestates, `.bin`/`.oam`/`.vram` WRAM/OAM/VRAM captures, `.raw` framebuffers, `.png` probe shots, `SHOT*`, probe `.txt`/`.json`. All root-anchored-gitignored; the ROM (`Chrono_Trigger_USA_SNES.sfc`, user-supplied) also lives here. |
| `third_party/SDL2/` | SDL2 dev libs (auto-fetched, gitignored). |
| `docs/` | This guide, `DEBUG_SURFACE.md`, `TESTING.md`. |

## Oracle workflow summary

The original ROM, observed through the embedded LakeSnes oracle, is the sole source of
truth (see the mandate in `CLAUDE.md`). The core loop: **disassemble** the ROM routine →
**read** the oracle's actual RAM that gates it → **implement that**, byte-exact →
**validate** with the lockstep differential + the 512-scene sweep.

**`ct.exe --vmdiff <state>`** — THE lockstep oracle differential. Seeds our C event-VM
from the oracle's WRAM, steps both sides per frame, and reports the FIRST scheduler
divergence (IP/prio; positions too with `--withpos`). Success is the printed line
`CLEAN: no scheduler divergence over N frames` (plus a frame-0 seed-fidelity check and
a script-vs-`$7F2000` image check) — the exit code is always 0, so parse the line.
Per-object drilldown via `VMDIFF_TRACK`/`VMDIFF_MVFLAGS`/`VMDIFF_ANIM`, button
injection via `CT_BTN` (oracle receives only the A bit — asymmetric by design).

**`ct.exe --vm <dat>`** — the 512-scene check. Without `--scene`, cross-checks the C
scene-VM's actor placement against the Python-packed actors for all 512 scenes and
prints `512 OK, 0 mismatch` style counts (current baseline: 507 OK with the --newgame
work); with `--scene N` it live-ticks one scene and dumps placements. Exit code always
0 — parse the counts.

**`ct.exe --monitor <regions> --from <state>`** — the generic WRAM probe: stream chosen
regions per frame (up to 32 regions × 0x400 bytes), `--diff` to emit only on change,
`--writer` to annotate each change with the writing instruction's bank:pc via the CPU
write hook, `--input`/`--btnscript` to drive buttons, `--json` for machines.

**`ct.exe --read <spec>`** — the one-shot byte probe: print raw ROM (HiROM-mapped,
`rom:C0/3B1B+16`) or live-oracle WRAM (`wram:7E0100+2`) bytes, plus the shortcuts
`flags`/`story`/`party`/`loc`; optionally after loading a state (`--from`) and settling
(`--settle N`). This is how a ROM fact is *confirmed* before it is implemented.

**`tools/s9x_to_lake.py`** — converts a Snes9x savestate into the LakeSnes format the
oracle consumes. Output is always **CTLS-wrapped** (see below). The base may be CTLS or
legacy raw. Snes9x captures (mid-cutscene states grabbed while playing) must be converted
before the oracle can run them; any change to the LakeSnes `handleState` layout still
invalidates every payload (CRC will not save you from a layout change — re-capture).

### LakeSnes `.state` on disk: CTLS + legacy

`ct.exe` save paths (`--savestate`, `--peek`+`PEEK_SAVE`, `--teleport --out`,
`--owforce`, …) write a **CTLS** container around the raw `snes_handleState` payload:

| Offset | Field |
|---|---|
| 0..3 | magic `CTLS` |
| 4..7 | version `u32LE` = 1 |
| 8..11 | payload length `u32LE` |
| 12..15 | IEEE CRC-32 of payload `u32LE` |
| 16.. | raw handleState stream (unchanged from historical files) |

Load (`ctLoadStateOracle` / `CtLakeStateLoadPath` in `src/ct_lake_state.c`):

- **CTLS** → verify length + CRC; refuse on mismatch (no silent zeros).
- **Legacy** (all existing pinned regress states) → accepted when size ≥ min and the
  first 11 bytes are 0/1 bools; still works so the suite needs no mass re-encode.
- Anything else (truncated CTLS, snes9x, CTFS, gzip, short junk) → **hard FAIL**.

Python: `tools/ctlib/lake.py` (`unwrap` / `wrap` / `read_payload` / `write_wrapped`).

**`tools/dis.py <hexaddr> <len> [m] [x]`** — the ROM disassembler for the
required-before-any-change step: disassemble the routine that produces the behavior.
`m`/`x` set the 65816 register-width flags. Gotcha (project memory): it prints `C0/`
for **all** JSR targets — a `JSR $1CDA` seen in bank C2 is really `C2/1CDA`.

Savestate formats (there are two distinct ones — the LakeSnes emulator `.state` and
ct_field's own `CTFS` CtScene state) are documented in
[`DEBUG_SURFACE.md` §3](DEBUG_SURFACE.md#3-reading-the-results--exit-code-discipline--savestate-formats).
