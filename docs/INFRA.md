# Infrastructure guide

How this repo is built, tested, laid out, and validated against the oracle. Companion
references: [`DEBUG_SURFACE.md`](DEBUG_SURFACE.md) (every ct.exe CLI mode + every debug
env var) and [`TESTING.md`](TESTING.md) (regression tiers and pinned baselines).

---

## Build

**`tools\build.bat`** — a thin wrapper that calls `tools/build.py`. On Windows the build
uses VS 2022 Build Tools (`vcvars64.bat`) + the bundled CMake/Ninja, configure + compile
into `build\`. Targets (from `CMakeLists.txt`): `ct.exe` (headless oracle harness, always
built, with `ct_oracle.exe`, the oracle alone that keys the suite's oracle cache);
`ct_viewer.exe`, `Belthasar.exe`, `ct_world.exe` (SDL2 apps, built **only** when
SDL2 is present — on Windows fetch it once with `tools\get_sdl2.ps1`).

**`tools/build.py`** — the build driver:

- **Honest per-target verification.** After the CMake/Ninja run it checks each expected
  artifact in `build\` actually exists and was freshly (re)linked, instead of trusting
  the driver's exit code plus a blanket "Build OK" banner. Success is reported (and the
  exit code set) *per target*.
- **Single-target argument.** `python tools/build.py Belthasar` builds and verifies just
  that target; another ct*.exe running (the player's `Belthasar` while `ct.exe` is rebuilt) does not block it.
- **A second tree.** `CT_BUILD_DIR=build_alt python tools/build.py Belthasar` builds into `build_alt\` (gitignored):
  exes to test with while the player's `build\Belthasar.exe` is running. The suite and the tools keep reading `build\`.
- **Data-free Belthasar.** `CT_DATAFREE=1 python tools/build.py Belthasar` (CMake `-DCT_DATAFREE=ON`; the driver
  passes ON or OFF on every configure, so the cache follows the environment) builds `Belthasar.exe` with none of the
  generated tables compiled in: it loads them from `ct_tables.dat` (`python tools/gen/pack_tables.py pack`, which
  `tools/setup.py` runs) and refuses a pack that does not match it. `ct.exe` and the other targets are unaffected. The
  design, the pack and the loader's checks: [ROM.md](ROM.md#the-data-free-belthasar).
- **The table pack's proof.** `python tools/gen/tables_proof.py` builds the optional target `ct_tables_proof`
  (`python tools/build.py ct_tables_proof`; never part of a plain build) and proves that the data-free storage, filled
  from a pack of today's `src/data/*.h`, equals the tables compiled from those headers byte for byte, with controls
  that a corrupt or mismatched pack is caught.
- **The three historical false-"Build OK" traps it catches** (all observed in this
  project; `build.py`'s own header is authoritative):
  1. **Locked exe.** With `Belthasar.exe` still running, the link cannot replace the
     binary — yet "Build OK" was printed while the on-disk exe stayed the OLD build
     (documented in the project memory: *"build.bat 'Build OK' LIES when Belthasar.exe
     is locked"*).
  2. **SDL2 silently absent.** Without `third_party/SDL2`, CMake *configures the SDL
     targets away* (`STATUS "SDL2 not in third_party/SDL2: skipping..."`) — the build
     "succeeds" but `Belthasar.exe`/`ct_world.exe`/`ct_viewer.exe` are stale or missing.
  3. **Unconditional banner.** The old `build.bat`'s final `echo Build OK: build\ct.exe`
     named only `ct.exe` and never verified **any** artifact's existence or freshness.

Rule of thumb inherited from those traps: after any build, trust the **artifact check**,
never the banner.

**Linux and macOS (gcc/clang) — not the reference build.** `python3 tools/build.py`
works there too: it takes `cmake` and `ninja` from the PATH and the system SDL2 (install
`libsdl2-dev` on Linux; the SDL2 targets fall back to it when `third_party/SDL2` is
absent), and `python3 tools/get_imgui.py` fetches Dear ImGui. The binaries keep their
Unix names, and CMake adds `build/<target>.exe` aliases beside them, so
`regress_checks.json`'s `build/ct*.exe` and the tools run unchanged. Without the ROM and
`ct_assets.dat` nearly every check SKIPs. gcc is stricter than MSVC
about two things MSVC lets through: a call to a function declared later (an implicit
declaration: add a prototype above the caller) and a hex constant ending in `E` glued
to `+`/`-` (`0x96DE+SLOT` is one invalid preprocessing number: write `0x96DE + SLOT`).

## Cloud sessions (claude.ai/code)

A cloud session is a fresh Linux container with a fresh clone, reclaimed after a while: anything worth keeping is
committed and pushed. Measured 2026-10-23 (Ubuntu 24.04, gcc, Python 3.13):

- **The hook.** `.claude/hooks/session-start.sh` (registered in `.claude/settings.json`, runs only when
  `CLAUDE_CODE_REMOTE=true`) installs `libsdl2-dev`, fetches Dear ImGui (`tools/get_imgui.py` falls back to a
  shallow `git clone` of the tag: the cloud proxy refuses github.com's archive links), runs `tools/fetch_saves.py`,
  installs the shapes and builds every target. Each step is skipped when its result is already there. Its last line
  says whether the container has the ROM's data, a ROM not yet set up, or no ROM.
- **The ROM is not in the container.** It is never committed here (docs/ROM.md). The owner attaches it to a message
  in the session's chat; it lands at `/root/.claude/uploads/<session>/<id>-<name>.sfc`, for that session only. The
  agent then runs `python3 tools/setup.py --rom <that path>` (it verifies the hash and copies the ROM to the root,
  where `.gitignore` keeps it out of every commit) and `python3 tools/fixtures.py make all`. The container is new each
  time, so the attachment and that cost recur per container. Without the
  ROM only the shapes build exists: `python3 tools/regress.py` gives 5 pass, 0 fail, 941 skip (`--quick`: 608 skip), the
  same as the GitHub CI ([TESTING.md](TESTING.md#the-github-ci-githubworkflowsciyml)). Engine work that changes behaviour cannot be
  validated without the ROM (the mandate's `--vmdiff` / `--vm` and the suite need the oracle on it).
- **Network.** The environment's policy decides which hosts answer. Under the default policy `fantasyanime.com` is
  refused, so the 27 Jackie saves of `tools/third_party_saves.json` do not fetch (51 of 78 do) and the checks and
  recipes that read them SKIP; adding that host to the environment's allowed domains fixes it (from the next session:
  a running one keeps the policy it started with). Where a host is out of reach, the owner attaches the archives (a
  zip of them, or the files) and `python3 tools/fetch_saves.py --from <zip or directory>` takes each missing one from
  there by its file name -- verified by the same SHA-256, so a wrong copy is refused, not used.
- **Time, measured on a 4-core container (2026-10-23).** `tools/setup.py` 21 min; `tools/fixtures.py make all` 64 min
  more (one recipe at a time). Both reuse what is already made (a second `make all`: 0.04 s), so a resumed container
  pays nothing; a new one pays both. With the ROM and the 78 saves, `python3 tools/regress.py` graded all 953 checks
  PASS on that container (2026-10-23; the 954th, `tree-tracks-no-game-file`, came after and passes too); the one check `playthrough-legs-vs-rule-cartridge` alone takes 72 min there,
  so `--quick` (or `--only`) is the tier to iterate with. A container restart keeps the working tree (the ROM, the
  generated files, `saves/`) but stops background jobs: commit before a long run.
- **Windows-only pieces.** `tools\build.bat` and the `tools/*.ps1` fetchers do not run here: `python3 tools/build.py`
  and `python3 tools/get_imgui.py` are the same steps. The agent's memory index (`MEMORY.md`) is not in a cloud
  container either: the rules live in `CLAUDE.md`, `.claude/no-invention-mandate.md`, `.claude/skills/` and
  `disasm/notes/`.

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
- `--list`, `--only <names>`, `--build` (drive the build first) round out the surface.

Tier contents and the meaning of each pinned baseline: [`docs/TESTING.md`](TESTING.md).

## Repo layout

| Path | What it is |
|---|---|
| `src/` | The C engine + harness. `scene.c` field-scene renderer (load ROM/.dat → one compose path); `ct_vm.c` event VM + scheduler + movers + textbox/naming; `collision.c`/`player.c`/`move.c` byte-exact ROM routine ports; `world.c` overworld camera/mover/streamer/ambient VM; `field.c` the interactive app (`Belthasar.exe`); `ct_world.c`/`viewer.c` SDL viewers; `main.c` the ct.exe oracle harness; `assets.c` ct_assets.dat loader; `lz.c` the LZ decompressor. |
| `src/tables/` | Generated, no data (`tools/gen/pack_tables.py headers`, from the shapes): the header the engine includes for each `src/data/*.h` table (the real header in a normal build, its declarations in a data-free one), the data-free storage `tables_storage.c` with its layout asserts, and `tables_real.c` for the proof. `src/datapack.c` loads `ct_tables.dat` into the storage. |
| `snes/` | Vendored **LakeSnes** oracle core (the embedded emulator). Do not edit casually — every validator's ground truth runs on it, and any change to its state serialization silently invalidates every `.state` on disk. |
| `assets/` | Build-time extraction pipeline (Python, dependency-free): ROM → `ct_assets.dat` (`extract_assets.py`, `lz.py`, `gfx.py`, `palettes.py`, `sprites.py`, `maps.py`, `text.py`, `script.py`...). The engine is ROM-independent at runtime; `ct.exe --datverify` proves the bridge byte-identical. |
| `disasm/`, `disasm/notes/` | The RE substrate: annotated 65816 traces, per-subsystem notes (`events.md`, `scenes.md`, `sprites.md`, `collision.md`...). Trace/dialogue artifacts are gitignored (ROM-derived, regenerable). |
| `tools/` | Canonical tooling: `build.bat`/`build.py`, `regress.py` + `regress_checks.json`, `dis.py`, `s9x_to_lake.py`, `disasm65816.py`, `get_sdl2.ps1`, `ct.py` (agent-facing wrapper), and the `ctlib/` helper library. One-off probe scripts use the `tools/_*.py` underscore convention and are gitignored (tracked exceptions like `tools/owbpal0.py` stay tracked). |
| `build/` | CMake/Ninja output (gitignored): `ct.exe`, `Belthasar.exe`, `ct_world.exe`, `ct_viewer.exe`. |
| `saves/` | ROM-derived savestates/captures (gitignored; the regenerable ones come from recipes: `python tools/fetch_saves.py`, then `python tools/fixtures.py make all`). |
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
a script-vs-`$7F2000` image check). It exits 1 on a divergence, but the suite grades the
printed line, never the exit code.
Per-object drilldown via `VMDIFF_TRACK`/`VMDIFF_MVFLAGS`/`VMDIFF_ANIM`, button
injection via `CT_BTN` (oracle receives only the A bit — asymmetric by design).

**`ct.exe --vm <dat>`** — the 512-scene check. Without `--scene`, cross-checks the C
scene-VM's actor placement against the Python-packed actors for all 512 scenes and
prints `512 OK, 0 mismatch` style counts (pinned baseline: 512 OK, 0 mismatch); with
`--scene N` it live-ticks one scene and dumps placements. It exits 1 on a mismatch; the
suite parses the counts.

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
Belthasar's own `CTFS` CtScene state) are documented in
[`DEBUG_SURFACE.md` §3](DEBUG_SURFACE.md#3-reading-the-results--exit-code-discipline--savestate-formats).
