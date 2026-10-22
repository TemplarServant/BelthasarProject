# Chrono Trigger — canonical C re-implementation

Re-implements the Chrono Trigger (SNES USA) field engine in portable C, validated frame-by-frame
against a LakeSnes **oracle** embedded in `build/ct.exe`. Build: `tools\build.bat`. The RE notes live
in the memory index (`MEMORY.md`).

---

# ⛔ MANDATE: CANONICAL ENGINE, ZERO INVENTION

The original ROM — through the **oracle** (`build/ct.exe`: `--vmdiff` lockstep, `--vm` 512-scene check,
`--monitor`/`--followvm`, savestates) and the ROM disassembly (`tools/dis.py <hexaddr> <len> [m] [x]`) —
is the **SOLE source of truth**. EVERY behavior is derived, byte-for-byte, from the ROM + the oracle's
actual RAM. **Nothing may be invented or inferred.**

When the user gives a shortcut ("the iris should be 1px lower", "the racers freeze"), that is a **POINTER
to a canonical datum** — go find and confirm it in the ROM/oracle, then implement THAT. The user's words
are never the implementation basis. ("it doesn't matter what I want", translated from Portuguese — go find the verified truth.)

## These are INVENTION — they are forbidden:

- **Scene-specific gates.** `if (sceneNo == 2)`, `if (loc == X)`, per-scene constants/branches. The ROM
  has no per-scene C code; the engine is ONE general machine. (Violation in this project: `bedIntro =
  sceneNo==2` — wrong.)
- **Heuristics that stand in for a real ROM mechanism.** e.g. "player vs NPC" (`obj != playerObj`) when
  the ROM actually branches on a per-object RAM flag (`$1B80,X`, `$7F0B00,X`, `$1C81,X`, ...). Find the
  FLAG, read the oracle's per-object value, branch on THAT. (Violation: the mover smooth-vs-clamp gate
  was made player-vs-NPC instead of `$1B80`/`$7F0B00` — wrong.)
- **Eyeballing a render to tune a constant.** Every constant comes from the ROM.
- **Spot-fixes (pontual).** Patching one symptom. **A regression in scene X caused by work on scene Y
  means the shared MECHANISM is wrong — fix the mechanism (structural) so ALL scenes become correct at
  once. NEVER add a scene-X special case to paper over it.**

## Required before ANY behavioral change:

1. **Disassemble** the ROM routine that produces the behavior (`tools/dis.py`).
2. **Read** the oracle's actual per-object RAM that gates it (savestate via `s9x_to_lake.py` +
   `--vmdiff`, or `--monitor`). Compare each case (e.g. Crono's bed mover vs a fair racer vs a sparkle).
3. **Implement THAT** mechanism, byte-exact, gated on the same RAM. Validate with `--vmdiff` (first-
   divergence) and the 512-scene `--vm`. **If you cannot derive it from the ROM/oracle, STOP and say so —
   do not guess, do not approximate, do not scope to a scene.**

See [[no-invention-rule]] in the memory for the longer-form rule and prior violations.

## ⭐ MODS ARE OUTSIDE THE MANDATE (user decision 2026-10-10)

"The rule of keeping the ROM's behaviour applies only to the game without 'mods', such as widescreen, a change of
resolution and a higher FPS." -- the user (translated from Portuguese).

- The ROM/oracle mandate binds the VANILLA game: every option at its default (ct_settings.ini absent or default), and
  every headless run (the regression suite never reads the settings file).
- A MOD is a player option the F1 overlay offers: the widescreen, the internal resolution, the frames between (60+ fps),
  the free sync mode, and later ones. A mod may draw, time or even run things the ROM does not, to serve what it is
  for. It must stay opt-in (off by default), must leave the vanilla game byte-identical when off, and says in its
  overlay tooltip what it changes.

## ⭐ NO EMULATION; hardware timing only where it changes the game (user decision 2026-10-09)

"I don't want anything to be emulation. Synchronisation differences caused by the technical limits of the era do not need
to be reproduced unless they influence the way the game works. My future goal is to simplify systems that at the moment
are more complex than they should be on current hardware." -- the user (translated from Portuguese).

- The port is native C. No solution may run ROM code on a CPU core or emulate hardware cycle by cycle. Existing pieces that do
  (the `src/xlat/` stages on the vendored cpu/ppu/dma -- seq_c3, wipe_c3, owwin_c6, gwarp_fd, hdma_fd -- and the vendored
  SPC700 for audio) are debt to replace with native C; ask before a large rewrite.
- The ROM and the oracle remain the truth for BEHAVIOUR (logic, state, RNG, data, what the player sees happen). Timing
  artefacts of the SNES's limits (a song upload parking the main loop, HDMA/refresh stealing cycles, lag, a hand-back a frame
  off because a handshake ended 242 cycles past a vblank) need not be reproduced unless they change how the game works.
- Prefer simplifying an over-complex mechanism when the simpler one keeps the game's behaviour.
- The no-invention rule below still holds for behaviour; the timing sections that follow are relaxed by this one.

## The zelda3 model: the CPU's timing against the beam (user decisions 2026-09-18, 2026-09-25, 2026-09-27)

Some ROM behavior depends on how many cycles the CPU spent, which a C engine cannot reproduce without being a
cycle-exact emulator from power-on (measured: disasm/notes/field_window_wipe.md, "The curtain turns SQUARE";
battle_boot_turns.md sec. 5). For that class the reference is the ROM run the way snesrev/zelda3 verifies itself
(`zelda_cpu_infra.c`: `PatchRom` swaps the H-counter RNG for a deterministic one on BOTH sides;
`RunOrigAsmCodeOneLoop` runs one main-loop iteration + the NMI per frame, so no lag frame exists). This is not an
exception to the mandate — it IS the mandate for that class ("There is no exception at all ... do it like zelda3", the user (translated from Portuguese),
2026-09-27): the oracle under the rule (`CT_ORACLE_LOGICAL=1`, which since the rule flip of 2026-10-01 carries the
virtual beam, the line cuts and the infinitely fast CPU by default) is the truth the port matches.
When in doubt about a timing read, do what zelda3 did.

- **Beam-counter reads** (`$2137` latch, `$213C`, `$213D`) return a VIRTUAL BEAM on both sides — the port and the
  oracle — instead of the real counters. The virtual values are part of the rule, not derived; they are the same
  everywhere and are documented where they are defined:
  - the seed routine C3/02DD: a fixed virtual beam (snes/snes.c `snes_readBBus`);
  - the anim drain's raster deadline C0/CA76 (CA82-CA99, `$213D` against the ROM's `$6B`): the virtual beam ADVANCES
    with the pass's own visits, V = 0x38 + visits × 0x20 (src/ct_vbeam.h) — two visits a pass at `$6B` = 0x78 (user
    decision 2026-09-27: the fixed value never cut, and the attract's bats and Zombor's legs lost their frames;
    disasm/notes/anim_drain_cad9.md sec. 16).
  - the event runner's deadline C0/5A46 (`$213D` against `$69`): the fixed virtual beam, V = 0 — the runner never
    stops early, on either side (user decision 2026-09-27, the zelda3 literal).
- **Lag frames** (a main-loop iteration whose CPU work overruns the NMI): the oracle's LOGICAL-FRAME mode holds the
  start of vblank until the CPU sits in a wait loop, so an iteration never spills into the next frame. Timing checks
  that meet a lag frame grade against that mode and say so.
- **NMI-off loads** (user decision 2026-09-25): CPU work done with the NMI disabled that spans frames (decompressions,
  block copies -- the overworld menu's stash and return, the field's reload) crosses 3 or 4 frame boundaries depending
  on where in the frame it began, i.e. on the CPU work before it. The LOGICAL-FRAME mode starts every such stretch at
  phase 0: at the first vblank after the NMI is disabled the beam holds for the cycles that frame had already spent, so
  C cycles of NMI-off work cross exactly floor(C / 357,368) boundaries wherever they started. The port prices the same
  stretch the same way (its cycles from the frame's start). The phase-0 start is part of the rule, not derived.

- **NMI-on work never costs frames -- FIXED** (user decision 2026-09-30, the zelda3 literal; it completes the 2026-09-18
  rule above). CPU work done with the NMI enabled between two waits ends inside its frame on both sides, however long:
  a heavy battle iteration (370k-470k cycles on GATO's turns), the battle's boot (~3.7M), a menu's or a shop's load, the
  effect load's block build CD/107D and fill CD/1052, the demo's sequences. The LOGICAL-FRAME mode holds the start of
  vblank until the main thread reaches a wait -- no one-frame valve, no rollback to real time -- and the port charges no
  frames for such work. (The waits the ROM makes inside a load stay: they are frames by design. NMI-OFF loads keep their
  own rule above.) Rejected alternatives, for the record: floor(C / 357,368) per stretch (it brings the lag frames back on
  heavy iterations and needs the whole engine's cycle cost in the port) and pricing the NMI's cost per scene (the
  CPU-timing class). DEFAULT since the rule flip (2026-10-01): `CT_ORACLE_NMION_HOLD=0` brings the old one-frame valve
  back for measurement only (and with it the plain logical oracle of before the rule).
- **The battle NMI's line wait** (user decision 2026-09-30, option A): during an action CD/09AA's `LDA #40 / JSR CD/3B82`
  waits for scanline 64, and the service after it (CD/1609's effect program and the NMI's tail) rewrites the per-line
  records. Under the rule the beam holds at line 64 while that service runs to its RTI, so its writes land on line 64 on
  both sides: the lines above keep the previous records, the lines from 64 down take the new ones. (The real cartridge
  lands ~20 lines lower -- the service's CPU time, the class this model removes.) A raster wait the service makes itself
  (the background program's op C1, CD/1D93 -> CD/3B82) lets the beam run to its line and cuts again there.
- **The SPC under the rule -- FIXED** (user decision 2026-10-01, option A, the zelda3 model): the sound processor runs ONE
  frame of SPC per displayed frame on both sides. While the rule holds the beam (an NMI-on stretch, the line-64 cut) the
  APU stands with it; a CPU that polls a busy SPC (the plain poll and C7/0A5A's counted one) is WAITING, so the stretch
  ends there and the handshake costs the frames the SPC needs. The port matches by waiting on its own SPC at the same
  handshake points; it never charges or skips SPC time the picture does not have. Rejected, for the record: the SPC
  running through a hold (option B) -- it puts the oracle's SPC ahead of the picture and of the port's, which a player
  cannot reproduce (the race's sound drifted 1.8 s that way). `CT_ORACLE_HOLDAPU=1` / `CT_ORACLE_APUPOLL=0` stay as
  measurement switches only.
- **The infinitely fast CPU -- the rule's model** (2026-10-01: the user delegated the choice, "the decision similar projects
  take"; snesrev/zelda3 run one main-loop iteration and its NMI per frame and then draw it). With the NMI enabled the beam,
  the timers and the SPC advance ONLY while the CPU waits; every other cycle is free. It generalises the hold at the step
  into 225 to every beam position, so no CPU work moves anything against the beam: a sound handshake costs exactly the SPC
  time it needs from where the CPU's last wait left the beam (option A's SPC, with no edge left that depends on where the
  CPU stood -- plain A charged the X-Strike's post at line 224 a frame and its post at line 200 none), the main thread's
  table writes land where its last wait left the beam, and flag polls race nothing. NMI-off work keeps its own rule.
  EXCEPT THE CPU's SIDE OF THE APU PROTOCOL (derived 2026-10-01, under the user's delegation -- the protocol's correctness): while the CPU runs the sound driver's own code --
  bank C7, entered only by `JSL C7/0004` (59 sites, and the title overlay's $7E:3775) and the reset's `JSL C7/0000`
  (C0/0053); C7 calls nothing outside itself -- its cycles are REAL on both sides (snes.c: the hold skips `k == C7`). The
  two-processor transfer is only correct at the CPU's real speed: the driver echoes each token (ARAM $110E) before it reads
  ports 2-3, and the ~35 cycles C7/09E5-09E9 + the next unit's `LDA [$12],Y / STA $2141` take are the protocol's settle --
  at zero CPU time the next unit overwrote them (the 0AD boot's `70 01` set B: 3776 of 6144 bytes wrong, both sides). The
  call still STARTS where the CPU's last wait left the beam (the caller's work and its JSL free); an interrupt taken inside
  it is held like any other work except its own waits (the field NMI's pad latch, the field V-IRQ's line-211 H-blank wait
  C0/ECEB). disasm/notes/oracle_logical_frames.md sec. 7.
  DEFAULT since the rule flip (2026-10-01; every family triaged against it, tools/rule_flip/*.json): the oracle's logical
  mode runs the infinitely fast CPU, the line cuts and the virtual beam unless `CT_ORACLE_IFAST=0` / `CT_ORACLE_LINE64=0`
  / `CT_ORACLE_VIRTUAL_BEAM=0` / `CT_ORACLE_NMION_HOLD=0` (measurement only); the port runs the same model always (its
  SPC headless, every JSL C7/0004 priced on SPC time, C7 on the listing from the CPU's last wait).
- **The main thread's vblank-flag poll** (delegated by the user 2026-10-08; the default since 2026-10-09): ops DF/E1
  (C0/3B71) and FF 96 (C0/4181) spin `LDA $4210 / BPL` with the NMI on, racing the field NMI's own ack (1-6 frames on the
  cartridge). Under the rule the race is decided for the poll: the vblank that rises while the spin waits is seen -- never
  the one C0/EA70 acked before the op ran -- so the store lands one frame after the visit, on both sides.
  `CT_ORACLE_RDNMI=0` measures without. disasm/notes/oracle_rdnmi_rule.md.

Everything else keeps the mandate above unchanged: the ROM + the oracle are the truth. The rule is not a license to
invent anything that is not a read of the beam counters or a CPU-overrun frame.

---

## Infrastructure (pointers)

- Build: `tools\build.bat`, or `python tools/build.py [target]` (per-target artifact verification — trust the artifact check, never the banner).
- Regression suite: `python tools/regress.py` (`--quick` = fast tier); parses printed signatures, NOT exit codes — see `docs/TESTING.md`.
- **Nothing may depend on one machine only** (the owner, 2026-10-22). On a fresh clone (a cloud session too): `python
  tools/fetch_saves.py` (public battery saves, verified by SHA-256) then `python tools/fixtures.py make all` (states from
  the ROM by recipes). A new check reads its states from a recipe (`tools/fixtures/`) or a public source
  (`tools/third_party_saves.json`), never a local capture: `tools/input_provenance.py` (check
  `inputs-come-from-recipes-or-sources`) fails otherwise, and `tools/private_captures.json` (the owner's captures still
  waiting for recipes, docs/ROADMAP.md 1.1) only shrinks. A run where those checks SKIP has not tested them.
- `docs/INFRA.md` = build/layout/oracle-workflow guide; `docs/DEBUG_SURFACE.md` = every `ct.exe` CLI mode + every debug env var.
- `tools/ct.py` (+ `tools/ctlib/`) = the agent-facing scripted wrapper over the oracle harness.
- **RE playbooks** (step-by-step, command-exact — follow them literally): `.claude/skills/`
  `re-derive` (find the canonical mechanism) → `re-implement` (mandate-safe C + validation loop) →
  `add-regress-check` (pin it); `capture-oracle-state` (s9x→lake, scenario captures);
  `triage-regression` (red suite / broken scene). When in doubt, start at `re-derive`.
- **Attract audit**: the `attract-divergence` subagent (`.claude/agents/attract-divergence.md`) compares the whole
  demo with the cartridge frame by frame via `tools/attract_divergence.py` (per-visit time warping, timing vs
  picture vs one-frame flash) and triages what it finds. `build/_divergence/latest/report.html` is the picture.
