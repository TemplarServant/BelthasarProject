# Methodology

How BelthasarProject decides what the game does, how it checks that the C engine does the same, and the rules that keep
both honest. The model is [snesrev/zelda3](https://github.com/snesrev/zelda3): a C re-implementation checked frame by
frame against the original program running in an embedded emulator.

## 1. The oracle

`build/ct.exe` embeds **LakeSnes**, a SNES emulator in C (vendored in `snes/`, MIT), and loads your verified ROM
into it. The emulated cartridge is the **oracle**: the one source of truth for what the game does. The C engine is
compared against it, never against itself or against a description of the game.

The oracle can:

| Mode | What it does |
|---|---|
| `--read <spec>` | Print ROM bytes (`rom:C0/3B1B+16`) or live WRAM (`wram:7E0100+2`), optionally after loading a state and settling N frames. The first step in confirming any fact. |
| `--monitor <regions> --from <state>` | Stream chosen RAM regions every frame; `--diff` prints only changes; `--writer` names the instruction that wrote each change. This is how a mechanism is found. |
| `--peek <state> <out>` | Run to a frame and dump the framebuffer, WRAM, VRAM, OAM and CGRAM. `PEEK_SAVE` writes a state there; `PEEK_SHOTS` grabs many frames in one run. |
| `--followvm --obj N` | Step one object's event script on the cartridge. |
| `--cputrace`, `--hookpc` | Trace the CPU, with the beam position and cycle counts (`CPUTRACE_BEAM=1`). |
| `--callrom` | Run one ROM routine on given inputs and report what it wrote. |

The disassembler `tools/dis.py <hexaddr> <len> [m] [x]` decodes the ROM. Always pass the M and X register widths:
a linear decoder that guesses them garbles 16-bit code. `tools/ct.py refs <addr>` and `tools/refscan.py` find
callers and writers. Every mode and environment variable is listed in [DEBUG_SURFACE.md](DEBUG_SURFACE.md).

## 2. Comparing the engine with the oracle

Several instruments, each answering a different question:

- **Lockstep (`ct.exe --vmdiff <state>`).** Seeds the C engine from the oracle's RAM at a savestate, steps both one
  frame at a time, and reports the **first** divergence: scheduler, script pointers, and optionally positions
  (`--withpos`), a byte mask of every address the engine owns (`VMDIFF_WMASK`), the draw list (`VMDIFF_DRAW`), or
  OAM (`VMDIFF_OAM`). It also checks that the seed itself was faithful at frame 0. Success is the printed line
  `CLEAN: no scheduler divergence over N frames`.
- **The 512-scene check (`ct.exe --vm ct_assets.dat`).** Runs every field scene's init through the C VM and compares
  the placed actors with the reference packed from the ROM's scripts. Current baseline: 512 OK, 0 mismatches.
- **Micro-execution sweeps (`ct.exe --sweep*`, about 50 modes).** Some code paths are hard to reach from any
  scenario. A sweep runs one ROM routine on the oracle's CPU over a generated space of inputs and compares every
  byte it writes with the C version. `--sweepops` covers the field opcodes, `--sweepsolid` the solid scan,
  `--sweepdraw` the draw pass, `--sweepcad9` the animation drain, `--sweepaieval` the battle AI's condition walk.
  A sweep that grades a routine says nothing about whether the engine still calls it, so each one has a liveness
  check beside it.
- **Live lockstep in the game (`CT_FIELDLOCK`, `CT_PROJECT`).** The oracle runs inside `ct_field` and named
  projections of its RAM are compared with the engine's every frame.
- **Scenario comparators (`tools/*_cmp.py`, about 90 of them).** Each drives both sides through one scenario (a
  fight, a menu, a cutscene) and compares chosen bytes, frames or pictures.
- **The battle census (`tools/battle_ab.py`).** Fights every enemy on both sides and compares spans, effects and
  the return to the field. Current: 253 of 255 identical.
- **The attract audit (`tools/attract_divergence.py`).** Compares the whole attract demo (about 11,000 frames
  across many locations, battles and stages) with the cartridge frame by frame. It aligns each visit separately,
  then classifies each event as timing (the same picture, shifted), picture (no nearby frame matches), flash (one
  frame lit or dark on one side) or capture artefact. `build/_divergence/latest/report.html` shows the result. The
  `attract-divergence` agent ([.claude/agents/attract-divergence.md](../.claude/agents/attract-divergence.md)) runs
  it and triages each event down to the ROM routine.

## 3. The mandate: zero invention

The rule the project is built on:

> The ROM, observed through the oracle and the disassembly, is the sole source of truth. Every behaviour is
> derived from it, byte for byte. Nothing may be invented or inferred.

These count as invention and are rejected:

- **Scene-specific gates.** `if (scene == 2)`, per-location constants. The ROM has no per-scene code; it is one
  machine.
- **Heuristics that stand in for a ROM mechanism.** For example "player versus NPC", when the ROM branches on a
  per-object RAM flag. Find the flag and branch on it.
- **Eyeballing.** Tuning a constant until a render looks right. Every constant comes from the ROM.
- **Spot fixes.** If work on scene Y breaks scene X, the shared mechanism is wrong. Fix the mechanism.

A bug report, even an accurate one, is treated as a pointer to a datum in the ROM, never as the specification.
"The iris should be one pixel lower" means "go find the routine that places the iris and read what it does".

**Why so strict.** A mechanism that is invented but plausible corrupts the re-implementation even when the screen
looks right: it will be wrong in the next scene, the next story state, the next input. An honest gap can be found
and closed; a fabricated mechanism hides the gap. The project's history has many cases where a "fix" that matched
the screen turned out to be the wrong mechanism, and the right one, once found, fixed other scenes too.

**When the truth cannot be derived**, the work stops and the gap is written down: what is unknown, and what
measurement or capture would settle it. It is not approximated.

The mandate binds the **vanilla** game: every option at its default, and every headless run. The mods (widescreen,
internal resolution, frames between, free sync) may draw, time or run things the ROM does not, because that is
what they are for. They must be off by default, leave the vanilla game byte-identical when off, and say in the
overlay what they change ([DECISIONS.md](DECISIONS.md), 2026-10-10).

## 4. The timing rule (zelda3's model)

### The problem

On a real SNES, some behaviour depends on how many CPU cycles the code took:

- **Lag frames.** If one main-loop iteration takes longer than a frame, the game slows by a frame. Whether it
  overruns can depend on a few hundred cycles.
- **Beam reads.** Some routines read the PPU's beam counters (`$2137`, `$213C`, `$213D`), for example to seed a
  random edge or to stop work when the beam passes a line. The value depends on every cycle since power-on.
- **Handshakes.** The CPU and the sound processor run on separate clocks. The frame on which an upload ends depends
  on both.

A C engine has no cycle clock, so it cannot reproduce these without becoming a cycle-exact emulator from power-on.
zelda3 solved this by changing **both sides**: its harness replaces the beam-counter randomness with a
deterministic value and runs exactly one main-loop iteration plus its NMI per frame, so no lag frame exists. The C
code then has to match that oracle exactly.

BelthasarProject adopted the same approach (decided 2026-09-18, extended through 2026-10-09). The oracle runs in
**logical-frame mode** (`CT_ORACLE_LOGICAL=1`), and the engine matches it. Together these are called **the rule**.

### What the rule does

| Piece | On the console | Under the rule (both sides) |
|---|---|---|
| **Logical frames** | A long iteration spills into the next frame (a lag frame). | The start of vblank waits until the CPU sits in a wait loop. An iteration never spills. |
| **NMI-on work is free** | Long work with interrupts on (a heavy battle turn, a menu load, the battle boot) can cost frames. | It ends inside its frame, however long. Waits the ROM makes on purpose stay frames. |
| **NMI-off loads** | Decompression with interrupts off crosses 3 or 4 frame boundaries depending on where in the frame it started. | Every such stretch starts at phase 0, so C cycles cross exactly floor(C / 357,368) boundaries (357,368 = master cycles per NTSC frame). |
| **Virtual beam** | Beam reads return the real position. | They return defined values: a fixed beam for the seed routine `C3/02DD`; for the animation drain `C0/CA76`, a beam that advances with the drain's own visits (V = 0x38 + visits x 0x20, so a pass makes two visits); for the event runner `C0/5A46`, V = 0, so it never stops early. |
| **The infinitely fast CPU** | Beam, timers and the sound processor advance with every CPU cycle. | With interrupts on, they advance **only while the CPU waits**. Every other cycle is free. No CPU work moves anything against the beam. |
| **The sound processor** | Runs on its own clock. | Runs one frame of SPC per displayed frame. A CPU that polls a busy SPC is waiting, so the handshake costs the frames the SPC needs, on both sides. |
| **Except bank `$C7`** | | The CPU side of the sound transfer protocol runs at its real cost on both sides. At zero CPU time the transfer is wrong: the driver echoes a token before it reads the next ports, and the code between them is the protocol's settle time. |
| **The battle's line-64 cut** | The battle NMI waits for scanline 64 and then rewrites per-line records; the real cartridge lands about 20 lines lower. | The beam holds at line 64 while that service runs, so its writes land on line 64. A raster wait inside the service cuts again at its own line. |
| **The vblank-flag poll** | Ops `DF`/`E1` and `FF 96` spin on `$4210` and race the NMI's acknowledge (1 to 6 frames on the cartridge). | The poll sees the vblank that rises while it waits, so the store lands one frame after the visit. |

Since 2026-10-01 these are the default for the oracle's logical mode and for the engine. The switches
`CT_ORACLE_NMION_HOLD=0`, `CT_ORACLE_LINE64=0`, `CT_ORACLE_IFAST=0`, `CT_ORACLE_VIRTUAL_BEAM=0` and
`CT_ORACLE_RDNMI=0` exist for measurement only. A check that compares timing with the oracle runs the oracle under
the rule. Without `CT_ORACLE_LOGICAL=1`, `ct.exe` measures the real cartridge, which gives false divergences in
any timing comparison.

The rule is not a licence to invent. It covers only beam-counter reads and frames lost to CPU overrun. Everything
else (logic, state, RNG, data, what appears on screen) must still match the oracle exactly.

The full derivation is in [disasm/notes/oracle_logical_frames.md](../disasm/notes/oracle_logical_frames.md) and
[disasm/notes/oracle_rdnmi_rule.md](../disasm/notes/oracle_rdnmi_rule.md).

### No emulation (2026-10-09)

The project's owner later set the direction beyond the rule: **nothing in the port should be emulation**. The
ROM and the oracle stay the truth for *behaviour*: logic, state, RNG, data, what the player sees happen. Timing
artefacts of the SNES's limits (a song upload stalling the main loop, HDMA stealing cycles, a hand-back that falls
one frame either side of a vblank because a handshake ended 242 cycles past it) need not be reproduced unless they
change how the game works. Where a mechanism is more complex than modern hardware needs, a simpler one that keeps
the behaviour is preferred.

Consequences:

- The pieces that still run ROM code or hardware on the vendored cores (the translated stages in `src/xlat/` and
  the SPC700) are debt to replace with native C.
- A timing-only residual of a frame, traced to hardware speed, is documented and left alone rather than chased
  with cycle pricing.

## 5. From a report to a pinned check

Every behaviour goes through the same loop. The playbooks in [`.claude/skills/`](../.claude/skills/) spell out each
step command by command; they were written for coding agents but work for people too.

1. **Derive** ([re-derive](../.claude/skills/re-derive/SKILL.md)). Reproduce the behaviour on the oracle from the
   nearest state. Find the RAM that changes (`--monitor --diff`), the instruction that writes it (`--writer`), and
   disassemble that routine until you can say in bytes what it reads and writes. Confirm in both directions: the
   disassembly predicts a RAM effect that the oracle shows, and an oracle value has a ROM source. Write a dossier in
   `disasm/notes/<topic>.md` that cites addresses and measured values and lists its open questions.
2. **Implement** ([re-implement](../.claude/skills/re-implement/SKILL.md)). Port the mechanism, gated on the same RAM
   the ROM reads, in the same frame slot. Check it against the forbidden-pattern list (scene gates, heuristics,
   eyeballed constants, host-side copies of ROM state).
3. **Validate.** Against the oracle, never against the engine's own earlier output: a lockstep, a sweep, or a byte
   comparison with a `--monitor` run. Then run the quick suite.
4. **Pin** ([add-regress-check](../.claude/skills/add-regress-check/SKILL.md)). Add a check to
   `tools/regress_checks.json` whose expected lines are the oracle-graded result, and **mutations**: small
   deliberate defects in the code the check covers. `python tools/regress.py --mutate <check>` must show each
   mutation turning the check red. A check that stays green under its mutation grades nothing, and is recorded as a
   blind spot.
5. **When something breaks** ([triage-regression](../.claude/skills/triage-regression/SKILL.md)). Reproduce the red
   check, bisect your own change, find the first divergent frame on the oracle, and fix the mechanism, not the
   symptom. Never re-pin a check to make it pass without evidence that the new output is the right one.

Before writing new code, check whether the work is already done. Dossiers and comments often call something
"open" after it has landed.

## 6. Traps worth knowing

These have each produced at least one wrong conclusion in this project:

- **Exit codes lie.** Many self-checks print a mismatch and exit 0. The suite passes on printed lines only.
- **A "Build OK" banner lies** when the executable is locked by a running copy. Trust `tools/build.py`'s artifact
  check.
- **A cached oracle output can outlive the oracle.** A tool that keeps a trace "if it is missing" will replay an
  old rule after `snes/*.c` changes. The suite's oracle cache keys on the `ct.exe` hash; your own scripts must too.
- **A converted snes9x state carries WRAM, CPU and SRAM only.** Its VRAM comes from the donor base state, so it is
  good for RAM-level derivation and not for comparing pixels.
- **The field runs with a non-zero direct page.** `LDA $17` in field code is WRAM `$0117`; the scheduler's queue
  build runs with DP = `$1000`. Many "no writer found" searches were this.
- **`JSR` is bank-local.** A `JSR $3009` in bank `$C4` calls `C4/3009`, not `C0/3009`.
- **"No scenario can reach this" is a claim about the current engine.** Each unported mechanism upstream shrinks
  what a scenario can reach. Such a claim expires when anything upstream lands; prefer a micro-execution sweep.
