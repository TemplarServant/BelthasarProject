# tools/ — canonical RE tooling index

All tooling here is **observation only**: it reads the ROM, oracle savestates and
peek dumps; it never alters engine behavior (see the no-invention mandate in
`CLAUDE.md`). Run everything from the **repo root** (the ROM
`Chrono_Trigger_USA_SNES.sfc` and the peek artifacts live there).

## Canonical tools

| Tool | Purpose |
|---|---|
| `tools\build.bat` / `python tools/build.py` | Honest build: VS2022 + Ninja; per-target artifact check; fails if a `ct*.exe` is still running (file lock). `build.bat` is a thin wrapper. |
| `python tools/regress.py` | One-command regression suite (checks in `tools/regress_checks.json`); PASS = expected substrings in output, never exit codes. `--quick`, `--only`, `--build`, `--check-inputs` (vs `tools/regress_inputs.json` sha256 pins). |
| `tools/regress_inputs.json` | Capture-kit manifest: every regress input path + sha256 + how to obtain/regenerate. |
| `python tools/preflight.py` / `tools\preflight.ps1` | Local gate: build → check-inputs → regress (`--quick` default). Writes `build/preflight.json`. |
| `python tools/dis.py <hexaddr> <len> [m] [x]` | Quick linear 65816 decode at a ROM **file offset** (HiROM: file off == bank-C0 offset; `0x2164B` = C2/164B). Full opcode matrix from `disasm65816.py`; JSR/JMP labels carry the REAL bank of the code being decoded; JSL/JML use their operand bank. Default widths 8-bit/8-bit (`SEP #$30`), REP/SEP tracked. Linear — no branch following. The one static disassembler: decode any routine or range with it (`<len>` in bytes). |
| `python tools/disasm65816.py <trace.txt> [rom]` | Trace-driven disassembler (**the** opcode-matrix/`hirom_off()`/`fmt()` library the others import). |
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
| `python tools/rom_free_gate.py` | The ROM-free gate (the check `rom-free-no-dump-gate`): the whole attract with no dump (`CT_NO_ROM=1`) and with it; pictures and sound identical, no page of the empty image read. |
| `python tools/release/package.py` | Builds a player's release folder and archive from the committed tree, data-free and leak-scanned ([docs/RELEASE.md](../docs/RELEASE.md)). |
| `python tools/release/scan_rom_leaks.py <dir> [--corpus ct_assets.dat] [--binaries]` | Searches a tree (or a release folder) for byte runs shared with the ROM or the extracted assets; must print `clean` ([docs/ROM.md](../docs/ROM.md#the-leak-scanner)). |

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

## `ctlib` API one-liners

`import sys; sys.path.insert(0, 'tools')` then:

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
                      #   false-positive on data)
script.load(r, 8)     # -> (objs, body, scriptId) via scene header 0x360000+scene*14
script.disassemble(r, scene, obj=N)   # fn-pointer table + annotated listing (ann())
```

## Legacy `_*.py` probes → ctlib replacement

The underscore-prefixed scripts are **unmaintained one-off probes**. They are
local and untracked (`.gitignore` ignores `tools/_*`), so a fresh clone does not
have them; the table below records what each cluster did and where its logic went.
New work goes through `ctlib`/`ct.py`.

| Legacy cluster (probes) | Canonical source lifted | ctlib replacement |
|---|---|---|
| s9x block split: `_decode`, `_rdscript`, `_taban`, `_rdstate`, `_rdraw`, `_rdglb`, `_rendmore`, `assets/snes9x_extract` | `parse_ppu_state.py` (imported) | `ctlib.s9x` |
| LakeSnes .state layout | `s9x_to_lake.py` constants | `ctlib.lake` |
| fb decode + PNG: `_fbpng`, `_bg3img`, `_hudcheck`, `_isohud`, `gen_namemsg`/`gen_namewin` (inline copies) | `_fbpng.py` | `ctlib.fb` / `ct.py fb2png` |
| OAM decode: `_oambird`, `_oamdump`, `_oamhud`, `_oamhud2`, `_gullcount`, `_gulls2` | `_oambird.py` (size-bit printf bug FIXED: raw bit reported) | `ctlib.oam` / `ct.py oam` |
| WRAM object peek: `_rdstate`, `_rdraw`, `_rdglb`, `_wdiff`, `_scriptdump`, `_sd2` | `_rdstate.py` | `ctlib.wram.objs` / `ct.py objs` |
| OW ambient blocks: `_oracleamb`, `_boatprio` | `_oracleamb.py` | `ctlib.wram.ow_ambient` / `ct.py owamb` |
| ROM xref/banking: `_refscan`, `_find1100`, `_find1a00`, `_findptr`, `_jt` | `_refscan.py` + `disasm65816.hirom_off` | `ctlib.rom.refs` / `ct.py refs` |
| scene-script disasm: `_demo_trace`, `_s8`, `_s8b`, `_s8find`, `_s8map`, `_callscan`, `_storyscan` | `_demo_trace.py` `ann()` (verbatim, scene parameterized) | `ctlib.script` / `ct.py script` |
| BG3 2bpp probes: `_bg3*` | `_bg3img.py` | not lifted (resolved investigation; lift `bg3px` if BG3 work recurs) |
| 4bpp OBJ render: `owbpal0`, `_tilevis*`, `_gull8`, `_rendmore` | `owbpal0.py` | not lifted (`owbpal0` stays a documented pipeline step for `saves/ow_balloons.spr`) |
| mvf/trk trace parsing: `_mode_scan`, `_mode_evolve`, `_curvana`, `_mvdelta` | `_mode_scan.py` | not lifted (regex set still lives in `_mode_scan`) |
| 65816 tables: `_disasm_snap`, `_zdec`, `_zrun`, old `dis.py` table | `disasm65816.py` | `dis.py` now imports it |

Recipe kept from `_jt.py`: the event-op jump table is at `C0/5D6E`
(handler ptr = word at `0x5D6E + op*2`; see `JSR ($5D6E,X)` at `C0/5AB1`).

Other notable single-purpose keepers: `gen_namemsg.py` / `gen_namewin.py` /
`owbpal0.py` (asset-pipeline regenerators, tracked); and, local only,
`_sprdump.py` (the only decoder of `saves/*.spr`) and `_taban.py` (a live
`$7F2000` runtime-script walker).
