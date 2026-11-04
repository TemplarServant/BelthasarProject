# tools/ — the tooling index

All tooling here is **observation only**: it reads the ROM, oracle savestates and
peek dumps; it never alters engine behavior (see the no-invention mandate in
`CLAUDE.md`). Run everything from the **repo root** (the ROM
`Chrono_Trigger_USA_SNES.sfc` and the peek artifacts live there). The Python tools
need the standard library plus numpy (`python -m pip install -r requirements.txt`);
a few tools no check runs also need Pillow (`requirements-optional.txt`).

## Start here

```
python tools/setup.py --rom <your dump>     # verify the ROM, generate the data, build everything
python tools/fetch_saves.py                 # the public battery saves the recipes start from
python tools/fixtures.py make all           # the suite's states, from the recipes
python tools/build.py [target]              # build; trust its per-target artifact check
python tools/regress.py --quick             # the pre-commit tier
python tools/regress.py --summary           # how many checks, per tier, excluded, mutations
python tools/regress.py --mutate <check>    # prove a check grades something
python tools/dis.py <hexaddr> <len> [m] [x] # disassemble a ROM routine
python tools/ct.py <subcommand>             # read states, OAM, framebuffers, scripts (below)
python tools/doc_surface_check.py           # flags and env vars the sources read that no doc names
```

The suite and its checks: [docs/TESTING_OVERVIEW.md](../docs/TESTING_OVERVIEW.md). Every
`ct.exe` / `Belthasar` mode and environment variable: [docs/DEBUG_SURFACE.md](../docs/DEBUG_SURFACE.md).

## Canonical tools

| Tool | Purpose |
|---|---|
| `tools\build.bat` / `python tools/build.py` | Honest build: VS2022 + Ninja; per-target artifact check; fails if a `ct*.exe` is still running (file lock). `build.bat` is a thin wrapper. |
| `python tools/regress.py` | One-command regression suite (checks in `tools/regress_checks.json`); PASS = expected substrings in output, never exit codes. `--quick`, `--only`, `--list`, `--summary`, `--build`, `--check-inputs` (vs `tools/regress_inputs.json` sha256 pins), `--pin`, `--mutate`. |
| `tools/regress_inputs.json` | Capture-kit manifest: every regress input path + sha256 + how to obtain/regenerate. |
| `python tools/mutation_retarget.py [--write] [--cross-file]` | Lists the suite's STALE mutations (their find text is gone) and re-targets the ones a refactor only moved, within a file or into another one. |
| `python tools/regress_share.py` | Proves that checks can share one run, and gives them the same env so `regress.py` runs them once. |
| `python tools/preflight.py` / `tools\preflight.ps1` | Local gate: build → check-inputs → regress (`--quick` default). Writes `build/preflight.json`. |
| `python tools/dis.py <hexaddr> <len> [m] [x]` | Quick linear 65816 decode at a ROM **file offset** (HiROM: file off == bank-C0 offset; `0x2164B` = C2/164B). Full opcode matrix from `disasm65816.py`; JSR/JMP labels carry the REAL bank of the code being decoded; JSL/JML use their operand bank. Default widths 8-bit/8-bit (`SEP #$30`), REP/SEP tracked. Linear — no branch following. The one static disassembler: decode any routine or range with it (`<len>` in bytes). |
| `python tools/disasm65816.py <trace.txt> [rom]` | Trace-driven disassembler (**the** opcode-matrix/`hirom_off()`/`fmt()` library the others import). |
| `python tools/refscan.py <hexaddr> [bank]` | Instruction-aligned reference scanner: who references a 16-bit address, decoding instruction boundaries (a plain byte scan, like `ct.py refs`, also matches operand bytes and data). |
| `python tools/s9x_to_lake.py <s9x> <base_lake> <out>` | Splice a snes9x state's WRAM+CPU into a ct.exe LakeSnes base state so the oracle can load and RUN it. PPU/VRAM/APU come from the base — use a same-mode base. |
| `python tools/parse_ppu_state.py [state]` | The definitive snes9x v11 container parser (block split, big-endian SnapPPU decode, FIL $21xx shadow). Library for everything s9x. |
| `python tools/ct.py <subcommand>` | Multi-command CLI over `ctlib` (below). |
| `python assets/script.py <rom> scene\|disasm\|talk ...` | Scene-script object extraction / raw disasm / dialogue simulation (the event-VM plumbing `ctlib.script` builds on). |
| `python tools/setup.py --rom <file>` | The single entry point from your ROM: verify it, build a bootstrap oracle from the shapes, replay the recipes, extract `ct_assets.dat`, generate `src/data/*.h` and `src/xlat/*.c`, pack `ct_tables.dat`, build. `--check` regenerates and compares; `--no-build`, `--oracle`, `--workdir`, `--out` serve the player's setup screen. |
| `python tools/fetch_saves.py [--verify]` | Downloads the public battery saves (`tools/third_party_saves.json`: 78 files, each by a pinned URL, verified by SHA-256). Run it before `fixtures.py`. |
| `python tools/fixtures.py {list,make,diff} [paths]` | The suite's state fixtures, made from recipes (`tools/fixtures/<domain>.json` -> `tools/recipes/`); `make all` makes every one. |
| `python tools/input_provenance.py [--list] [--write]` | Where every state file a check reads comes from (recipe, public source, or `tools/private_captures.json`); the check `inputs-come-from-recipes-or-sources`. |
| `python tools/playthrough.py cmp all` | The playthrough: one route from power-on (`tools/playthrough/route.txt`), graded leg by leg against the rule's cartridge; `try`, `walk`, `scene`, `diff` help write and inspect legs. |
| `python tools/walkto.py` | Oracle-driven pathfinder for building fixtures and legs: a d-pad drive that walks the lead to a target pixel or through an exit, probing the oracle itself. |
| `python tools/attract_divergence.py [--vehicle boot] [--visit LOC]` | The whole attract demo against the rule's cartridge, frame by frame; `build/_divergence/latest/report.html`. The `attract-divergence` agent runs it. |
| `python tools/battle_ab.py` | The battle census: fights enemies on both sides and compares spans, effects and the return to the field. |
| `python tools/battle_sim.py` | Any battle, port against the rule's cartridge: every enemy formation the ROM stages, any party, any bag. |
| `python tools/rom_free_gate.py` | The ROM-free gate (the check `rom-free-no-dump-gate`): the whole attract with no dump (`CT_NO_ROM=1`) and with it; pictures and sound identical, no page of the empty image read. |
| `python tools/doc_surface_check.py [--count]` | Read-only: the `--flag` literals of `src/main.c` / `src/field.c` and the `getenv` names of `src/` / `snes/` that no `docs/*.md` names. |
| `python tools/release/package.py` | Builds a player's release folder and archive from the committed tree, data-free and leak-scanned ([docs/RELEASE.md](../docs/RELEASE.md)). |
| `python tools/release/scan_rom_leaks.py <dir> [--corpus ct_assets.dat] [--binaries]` | Searches a tree (or a release folder) for byte runs shared with the ROM or the extracted assets; must print `clean` ([docs/ROM.md](../docs/ROM.md#the-leak-scanner)). |

## The families of tools

`tools/` is flat: about 200 scripts at its root, grouped by their names. Every tool opens
with a docstring (or a `#` header) that says what it does and how to run it; the check that
runs a tool names it in its `argv` in `tools/regress_checks.json`. Do not rename or move a
tool a check, a recipe or a mutation names.

| Family | What it is |
|---|---|
| `*_cmp.py` (about 130) | One behaviour, port against the oracle: each runs `Belthasar` / `ct.exe` and the cartridge on the same state and compares RAM, pictures or sound. Most are the `argv` of one or more checks. The prefix names the area: `attract_*` (the attract demo), `battle_*`, `menu_*` / `shop_*`, `ow_*` (the overworld), `race_*` / `seq_*` / `ending_*` / `entry3_*` (the `$C3` stages), `textbox_*`, `gate_*`, and single mechanisms (`zombor_*`, `cyclone_*`, `telepod_*`, ...). |
| `*_check.py` | Invariants checked without running the cartridge, mostly for the mods: `widescreen_check.py`, `native_fx_check.py`, `fill_*_check.py`, `options_overlay_check.py`, `sync_free_check.py`; and `dialogue_refs_check.py` (each dialogue box a script can open against the string the ROM reads). |
| Censuses and surveys | `battle_ab.py`, `battle_sim.py`, `battle_asc_census.py`, `attract_divergence.py`, `attract_survey.py`, `rom_census.py` / `rom_census_attract.py` (which ROM pages the game still reads), `ophist.py` (the field VM's opcode use). |
| Disassemblers and script readers | `dis.py`, `disasm65816.py`, `refscan.py`, `wdis.py` (code in WRAM, the `$C3` overlay), `cputrace_dis.py` (a `ct.exe --cputrace` run's code), `spc700dis.py` (the sound driver), `c3script.py` and `race_script_walk.py` (the `$C3` sequence scripts). |
| Cost models | `*_cost_proto.py`, `*_cost_consts.py`, `cycle_costs.py`, `cycle_legs.py`, `cycle_prof.py`, `menu_init_legs.py`: the priced mechanisms (a load, a menu open, a pass) walked over the ROM and measured on beam traces; `src/field_cost.c` and `src/menu_cost.c` are written from them. |
| State formats | `s9x_to_lake.py`, `parse_ppu_state.py`, `wram_peek.py` (a `Belthasar` run's WRAM at a frame), `ct.py` / `ctlib`. |
| Suite infrastructure | `regress.py` + `regress_checks.json` + `regress_inputs.json`, `regress_share.py`, `mutation_retarget.py`, `input_provenance.py` + `private_captures.json`, `fetch_saves.py` + `third_party_saves.json`, `preflight.py` / `.ps1`, `peekshots_selftest.py`, `doc_surface_check.py`. `oracle_cache_site/sitecustomize.py` puts the oracle cache in front of the oracle runs of a check's Python tools. |
| `gen/` | The generators of `src/data/*.h`, `src/xlat/*.c` and `ct_assets.dat`'s captures, run by `setup.py` from `gen/manifest*.json`; `gen/shapes/` holds each generated file's shape (declarations and sizes, no data) for the bootstrap build and the CI; `gen/pack_tables.py` makes `ct_tables.dat` and `src/tables/`. |
| `recipes/` + `recipes.py` | Oracle states rebuilt from power-on: one JSON per recipe, a `doc` and its `steps` (`power_on`, `pad`, `poke`, `teleport`, `sram_file`, ...). |
| `fixtures/` + `fixtures.py` | The suite's state fixtures by domain (`attract`, `battle`, `field`, `menu`, `party3`, ...), each made by a recipe. |
| `playthrough*.py` + `playthrough/` | The playthrough's route and its tools (`playthrough_align.py`, `playthrough_events.py`, `playthrough_triage.py`); `battle_pilot.py` plays a fight on the cartridge for route authoring. |
| `release/` | The release: `package.py`, `scan_rom_leaks.py`, `scan_dialogue.py`, `check_tracked.py` (no game file tracked; a quick check and the CI), `export_public.py`, `redact_rom_bytes.py`, `make_media.py` (the README's pictures). |
| `shadow_routes/` + `shadow_route.py` | Pad routes for `ct.exe --shadow` ([docs/SHADOW_PROPOSAL.md](../docs/SHADOW_PROPOSAL.md)), built one leg at a time. |
| `rule_flip/` | The triage of every check family against the timing rule when it became the default (2026-10-01), read by checks that cite it. |
| Mod data | `steam_wide.py` (the Steam release's margins from the player's own copy -> `ct_wide.dat`), `steam_bgcover.py`, `map_editor.py`, `scene_art_entry3.py` ([docs/WIDESCREEN.md](../docs/WIDESCREEN.md)). |
| Fetchers and packaging | `get_sdl2.ps1`, `get_sdl2_src.ps1`, `get_imgui.ps1` / `get_imgui.py`, `get_vulkan_headers.ps1`, `package_macos.py`. |
| The rest | Single-purpose probes, each described by its docstring: `ti_sim.py` (the title's overlay machine), `mixer_replay.py`, `motion_engine_rate.py`, `ow_bootstrap_sets.py`, `owbpal0.py` (builds `saves/ow_balloons.spr`), `backends_compare.py` (needs a GPU), `attract_audio_latency.py`. |

## `ct.py` subcommands

```
python tools/ct.py fb2png <in.raw|.fb> [out.png]   framebuffer/screenshot -> PNG
python tools/ct.py oam    <file.oam|s9x.state>     128-sprite OAM table
python tools/ct.py objs   <state|.wram>            field object-array table
python tools/ct.py owamb  <state|.wram>            overworld ambient blocks
python tools/ct.py refs   <addr>... [--rom PATH]   JSR/JSL callers + STA/STZ writers
python tools/ct.py script <scene> [--obj N]        event-VM fn table / annotated listing
python tools/ct.py wram   <state> <out.bin>        extract the 128KB WRAM
```

States/dumps are auto-detected: raw 128KB `.wram` sidecar, LakeSnes `.state`
(the raw `snes_handleState` stream ct.exe writes), or snes9x `#!s9xsnp:0011`
(gzip'd or raw). `.oam` sidecars are accepted in all three sizes ct.exe has
written (544 canonical / 1056 ppuregs / 1568 peek+CGRAM). `fb2png` accepts the
oracle 512x480 `[pad,B,G,R]` fb **and** the `[w][h][RGB]` shots from
Belthasar/ct_world (u32 or u16 header).

## `ctlib` modules

`import sys; sys.path.insert(0, 'tools')` then `from ctlib import <module>`. Each module's
docstring is its reference.

| Module | What it holds |
|---|---|
| `rom` | ROM access: `load()` (SHA-1 verified, copier header stripped), HiROM addressing (`hirom_off`), `refs()` (a heuristic byte-pattern xref scan). |
| `lake` | The LakeSnes `.state` layout ct.exe reads and writes: `is_lake`, `wram`, `cpu`, and the CTLS wrapper (`unwrap` / `wrap` / `read_payload` / `write_wrapped`). |
| `s9x` | snes9x (`#!s9xsnp:0011`) savestates: `split_blocks`, `wram`, `ppu`, `oam`. |
| `wram` | WRAM peekers over a raw 128 KB blob: `load` (auto-detects `.wram` / lake / s9x), `objs` (field object rows), `player`, `ow_ambient`. |
| `oam` | The 544-byte OAM blob: `decode` (128 sprites), `load` (`.oam` sidecars of every size, or an s9x state). |
| `fb` | Framebuffers and shots: `load`, `save_png`, `fb_to_png` (a dependency-free PNG writer). |
| `script` | Field event-VM scene scripts: `load`, `disassemble` (the fn-pointer table and an annotated listing). |
| `lz` | The ROM's LZ decompressor `C3/0557`, the Python twin of `src/lz.c`'s bounded decompressor. |
| `beam` | Beam traces (`ct.exe --cputrace` with `CPUTRACE_BEAM=1`): each instruction's PC and beam line. |
| `costtab` | Instruction costs read off beam traces, for hand-walked cost models. |
| `pathcost` | One path through ROM code priced instruction by instruction off the beam traces' costs. |
| `peekshots` | Many oracle frames from one `ct.exe --peek` run (`PEEK_SHOTS`). |
| `oracle_cache` | The content-addressed cache of oracle runs (`--peek` / `--monitor` / `--hdma` / `--fb`) that the suite and its tools share; `_exe_sha` keys on the binary with the link identity masked. |
| `oracle_session` | The oracle kept in one process (`ct.exe` / `ct_oracle.exe --serve`), for authoring tools. |
| `mutlabs` | The mutation labs: copies of the working tree under `build/_mutlabs/` that `regress.py --mutate` builds and runs its mutants in. |

```python
from ctlib import s9x, lake, fb, oam, wram, rom, script

s9x.split_blocks(p)   # snes9x state -> {name: bytes}; .wram(p); .ppu(p); .oam(p)
lake.is_lake(p)       # LakeSnes sniff; .wram(p); .cpu(p) (WRAM@69, 29-byte CPU block)
fb.load(p)            # -> (w, h, rgb); .save_png(out, w, h, rgb); .fb_to_png(p)
oam.decode(blob)      # 544-byte blob -> 128 sprite dicts (raw size bit, signed 9-bit X)
oam.load(p)           # .oam sidecar (544/1056/1568) or s9x state -> 544-byte blob
wram.load(p)          # auto-detect .wram/lake/s9x -> 128KB; .detect(p)
wram.objs(w)          # field object rows: tile/foot/solid/active/status/prio/
                      #   scriptptr/anim/cadence/speed/ticket/facing; .player(w) locks
wram.ow_ambient(w)    # $0B30-$1B30 stride 0x40 sig 0x0F63 blocks (pc/attr/pos/vel)
rom.load()            # ROM bytes (auto-strips 512-byte copier header); .hirom_off
rom.refs(r, 0x1A00)   # callers+writers -- heuristic byte-pattern scan (can
                      #   false-positive on data; tools/refscan.py decodes instructions)
script.load(r, 8)     # -> (objs, body, scriptId) via scene header 0x360000+scene*14
script.disassemble(r, scene, obj=N)   # fn-pointer table + annotated listing (ann())
```

## Conventions

- **Run from the repo root**; a tool finds the ROM, `ct_assets.dat` and `build/` there.
- **A check's tool never edits the working tree.** A tool that writes files writes under
  `build/` (or its own directory there); a check that writes a shared file is marked
  `exclusive` in `regress_checks.json`.
- **An oracle state a tool makes belongs to the oracle that made it**: make it on every run
  when that is cheap, or key it to the exe that made it ([docs/TESTING.md](../docs/TESTING.md)).
- **Print a signature.** A check passes on the lines a tool prints, never on its exit code, so
  a tool that grades something prints one stable line saying so (`... IDENTICAL`, `PASS`, a hash).
- **One-off probes** are named `tools/_*.py`. `.gitignore` ignores them, so they are local to
  one machine and a fresh clone has none; anything another person needs is a tracked tool with
  a docstring, or a function in `ctlib`.
- **Standard library first.** numpy is the one package a check needs; keep a new tool
  standard-library only where you can, and list any new package in `requirements.txt`.

Recipe kept from an old probe: the event-op jump table is at `C0/5D6E`
(handler ptr = word at `0x5D6E + op*2`; see `JSR ($5D6E,X)` at `C0/5AB1`).
