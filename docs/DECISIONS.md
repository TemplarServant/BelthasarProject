# Decision log

The decisions that shaped BelthasarProject, newest first. Each entry gives the date, the decision, why it was taken, its
consequences, and where it lives. "The owner" is the project's owner. Most of the code was written by coding agents
under the owner's direction, and an entry says when a choice was the owner's, when the owner delegated it, and when
it was derived from measurement. The owner's words are quoted in English translation.

The binding text of the rules is in [CLAUDE.md](../CLAUDE.md). The timing model is explained for newcomers in
[METHODOLOGY.md](METHODOLOGY.md#4-the-timing-rule-zelda3s-model).

---

## 2026-10-10: Public release as BelthasarProject

**Decision (owner).** Publish the project so others can collaborate:

- **Name:** BelthasarProject (first proposed as plain "Belthasar"), after Belthasar, the Guru of Reason who designed the Epoch. It keeps clear of the "Chrono" trademark, as
  Ship of Harkinian keeps clear of "Zelda".
- **License:** MIT, the same as LakeSnes (vendored in `snes/`) and zelda3.
- **History:** the public repository starts from one clean commit exported from an allowlist. The private history,
  which contains ROM-derived captures and generated tables, stays private.
- **Language:** everything in English: documentation, code comments, overlay strings and tool output.
- **ROM:** using the project requires your own dump of Chrono Trigger (USA), the No-Intro image (SHA-1
  `de5822f4f2f7a55acb8926d4c0eaa63d5d989312`, CRC32 `2D206BF7`). Nothing copyrighted is distributed.
- **Recipes:** every state that used to come from a private playthrough capture is rebuilt by the oracle from power-on:
  a pad script, optionally on top of a battery save that another recipe produced. None of the owner's saves is
  published. A setup tool replays the recipes from the user's ROM and regenerates `src/data/`, and the result must be
  byte-identical to today's private tables.
- **Gate:** publication waits until a clean-room build passes: export, fresh directory, setup with the ROM, build,
  suite.

**Why.** Legal safety (no ROM bytes, captures or savestates may ship) and an open path for contributors.

**Consequences.** The ROM check landed in C and Python (`src/rom_verify.c`, `tools/ctlib/rom.py`); a leak scanner
(`tools/release/scan_rom_leaks.py`); LICENSE and CREDITS. Work in progress: the recipes, the setup flow, and moving
the generators into the tree ([ROADMAP.md](ROADMAP.md#1-the-public-release-and-what-it-left-open)). The audit behind it: 51 generated
headers (12 from the ROM alone, 2 from oracle runs from power-on, 37 from captured states), and 807 regression checks
(289 need only the ROM, 518 need state fixtures).

**Where.** [ROM.md](ROM.md), [README.md](../README.md), [LICENSE](../LICENSE), [CREDITS.md](../CREDITS.md).

## 2026-10-10: Mods are outside the ROM mandate

**Decision (owner).** "The rule of keeping the ROM's behaviour applies only to the game without 'mods', such as
widescreen, a change of resolution and a higher FPS."

**Why.** The mods exist to go beyond the cartridge. Holding them to byte-exact ROM behaviour made the frames between
(built on reprojection) and the widescreen's sides (objects the ROM deactivates outside its 256-pixel window) more
contrived than they needed to be.

**Consequences.**

- The vanilla game is every F1 option at its default plus every headless run. It keeps the full mandate, and the
  suite checks it.
- A mod (widescreen, internal resolution, frames between, free sync, and later ones) may draw, time or simulate what
  the ROM does not: a native layered renderer at fractional positions, extrapolation, or animating objects past the
  ROM's camera window.
- A mod must be opt-in, must leave vanilla byte-identical when off, and must say in its overlay tooltip what it
  changes.
- Native frames-between drawers for the field and the overworld followed, plus the widescreen battle.

**Where.** CLAUDE.md, "MODS ARE OUTSIDE THE MANDATE"; `src/motion.c`, `src/fieldnative.c`, `src/field.c`.

## 2026-10-09: No emulation in the port

**Decision (owner).** "I don't want anything to be emulation. Synchronisation differences caused by the technical
limits of the era need not be reproduced unless they influence how the game works. My goal for the future, in fact,
is to simplify systems that are currently more complex than they need to be on today's hardware."

**Why.** It came up when an agent proposed running the sound driver's code cycle-exactly on the vendored 65816 to
settle one enemy's battle exit. That exit lands 242 cycles past a vblank, and the frame it lands on depends on the
CPU/SPC phase. The project is a port: the C should implement the game's behaviour natively, and cartridge timing is
not a goal in itself.

**Consequences.**

- Never propose emulation as a solution: no cycle-exact cores, no running ROM code on the vendored CPU, no wider use
  of translated stages.
- The existing emulated pieces are debt to retire as they are touched: the five translated stages in `src/xlat/` and
  the vendored SPC700. The owner is asked before a large rewrite.
- Timing-only residuals that come from hardware speed (the sound loader's clock, HDMA cost, a one-frame battle
  hand-back) are documented, not chased.
- The oracle stays the reference for behaviour (logic, state, RNG, data), and no longer for hardware-timing
  artefacts. This relaxes the timing entries below; the no-invention rule for behaviour still holds.
- This supersedes the older view in `disasm/notes/port_completion_plan.md` sec. 4 that running the SPC700 driver
  "is the correct call and not a gap".

**Where.** CLAUDE.md, "NO EMULATION"; [ROADMAP.md](ROADMAP.md#3-debt-emulation-and-bakes-to-retire).

## 2026-10-09: The frontend, an in-game options overlay

**Decision (owner).** The owner asked for a way to choose how the game behaves (frame-perfect or free sync,
widescreen, higher resolution and frame rate, mods) and chose:

- an overlay inside the game, not a separate launcher;
- SDL2 + Dear ImGui, with OpenGL and Vulkan backends;
- a first version covering sync, basic video, controls and audio, widescreen and 60+ fps;
- an approved order for the picture work: internal scale, then frames between, then widescreen.

**Consequences.** The F1 overlay and `ct_settings.ini` (headless runs never read it); the presenter with SDL_Renderer,
OpenGL 3.3 and Vulkan backends; free sync; internal resolution; frames between; widescreen. Items that the free sync
mode keeps on purpose: the title's upload wait (it keeps the pendulum in time with the music), the menu-open stretch,
the battle return's four waits, the gate warp, and the cold reset.

**Where.** `src/overlay.cpp`, `src/settings.c`, `src/present.c`, `src/present_vk.c`, `src/motion.c`,
`src/fieldnative.c`.

## 2026-10-08 / 2026-10-09: The main thread's vblank-flag poll under the rule

**Decision (delegated by the owner: "make the best decision"; adopted as the default 2026-10-09).** Ops `DF`/`E1`
(`C0/3B71`) and `FF 96` (`C0/4181`) spin on `LDA $4210 / BPL` with the NMI on, and race the field NMI's acknowledge
(1 to 6 frames on the cartridge). Under the rule the race is decided for the poll: it sees the vblank that rises while
it waits, never one the NMI acknowledged before the op ran. The store therefore lands one frame after the visit, on
both sides.

**Why.** The first version (the store in the visit's own frame) let the spin see a vblank that the NMI had already
acknowledged, which is impossible at any CPU speed. It put the staff roll and the attract demo a frame early.

**Consequences.** It matched the engine's existing reboot without changing the engine, and closed the staff roll's
caption-plane offset. `CT_ORACLE_RDNMI=0` measures without it.

**Where.** CLAUDE.md; `snes/snes.c`; [disasm/notes/oracle_rdnmi_rule.md](../disasm/notes/oracle_rdnmi_rule.md).

## 2026-10-08: The keyboard is one SNES pad

**Decision (derived from the ROM).** Default keys map one-to-one to the pad (A=Z, B=X, X=A, Y=S, L=D, R=C,
Select=Backspace, Start=Enter). Running is the B button, which is what the ROM reads (`$00F8` bit 1, `C0/8925`). The
old Shift-to-run binding was an invention and was retired.

**Where.** `src/settings.c`; [disasm/notes/field_dash_run.md](../disasm/notes/field_dash_run.md).

## 2026-10-01: The SPC under the rule, the infinitely fast CPU, and the rule as the default

**Decisions.**

1. **The SPC (owner, option A).** The sound processor runs one frame of SPC per displayed frame on both sides. While
   the rule holds the beam, the APU waits with it. A CPU that polls a busy SPC is waiting, so the handshake costs the
   frames the SPC needs. The engine waits on its own SPC at the same handshake points. *Rejected:* option B (the SPC
   keeps running through a hold). It put the oracle's sound ahead of the picture and ahead of the engine, which no
   player could reproduce: the race's sound drifted 1.8 seconds.
2. **The infinitely fast CPU (owner delegated: "do what similar projects do").** With the NMI on, the beam, the timers
   and the SPC advance only while the CPU waits. This is the zelda3 practice of running one iteration and its NMI per
   frame. It removed the last cases where a handshake's cost depended on where the CPU happened to be.
3. **The bank `$C7` exception (derived under that delegation).** The CPU's side of the sound transfer protocol runs at
   its real cost on both sides. At zero CPU time the transfer was wrong, both sides: the driver echoes a token before
   it reads the next two ports, and those ~35 cycles are the protocol's settle time. A boot upload got 3,776 of 6,144
   bytes wrong.
4. **The default flip.** The rule (NMI-on work free, the line cuts, the infinitely fast CPU, the virtual beam) became
   the default in the oracle's logical mode and in the engine. Every check family was triaged against it: 484 equal,
   149 re-pinned with evidence, 107 mechanical moves, 5 open items pinned as they were.

**Where.** CLAUDE.md, "The SPC under the rule" and "The infinitely fast CPU"; `snes/snes.c`; `tools/rule_flip/*.json`;
[disasm/notes/oracle_logical_frames.md](../disasm/notes/oracle_logical_frames.md) sec. 7.

## 2026-09-30: NMI-on work never costs frames; the battle's line-64 cut

**Decision (owner: "fix the decision").** CPU work done with the NMI on between two waits ends inside its frame on
both sides, however long it is: a heavy battle iteration, the battle boot (~3.7 million cycles), a menu or shop load.
The oracle holds the start of vblank until the main thread waits. Waits the ROM makes on purpose remain frames.

*Rejected:* charging floor(C / 357,368) frames per stretch, which brings lag frames back on heavy iterations and
needs the whole engine's cycle cost; and pricing the NMI's cost per scene.

**The line-64 cut (owner, option A).** During a battle action the NMI waits for scanline 64 (`CD/09AA`) and then
rewrites per-line records. Under the rule the beam holds at line 64 while that service runs, so its writes land on
line 64 on both sides. On the real cartridge they land about 20 lines lower, which is the CPU-timing class this
model removes.

**Where.** CLAUDE.md; `snes/snes.c`; `src/battle.c` (`battleLineCut`).

## 2026-09-27: No exception, the zelda3 model is the rule

**Decision (owner).** "There is no exception at all. Remove the exception and do it like zelda3." Faced with the
measurements, the owner chose a **virtual beam that advances** for the animation drain.

- The animation drain `C0/CA76` stops on a raster deadline. Under the rule its beam advances with the pass's own
  visits: V = 0x38 + visits x 0x20, so two visits a pass at the ROM's default deadline.
- The event runner `C0/5A46` reads a fixed virtual beam V = 0 (the zelda3 literal), so it never stops early on either
  side.

**Why.** With V = 0 the drain never cut either, and the oracle itself then froze an object in one pose. That would
have made the defect canonical: the attract's bats and Zombor's legs lost their frames. The owner wanted the port
to look like the real game, through a model that is deterministic and equal on both sides.

**Where.** `src/ct_vbeam.h`; `snes/snes.c` (`snes_readBBus`); `src/move.c` (`CtAnimDrainPass`);
[disasm/notes/anim_drain_cad9.md](../disasm/notes/anim_drain_cad9.md) sec. 16.

## 2026-09-26: Both sides deterministic, and then equal

**Decision (owner).** The timing model means both sides are deterministic and then **equal**. "History" (a divergence
blamed on what ran before) is never an excuse; zelda3's standard is that the RAM is equal.

**Consequences.** The random seed routine `C3/02DD` was traced across the whole attract demo until all 377 of 377
calls matched the cartridge (`tools/seed_history_cmp.py`). On the way, the battle AI's condition walk and a battle
formula's use of the direct page were ported in full.

## 2026-09-25: NMI-off loads start at phase 0

**Decision (owner, option B).** CPU work done with the NMI disabled that spans frames (decompressions, block copies)
crosses 3 or 4 frame boundaries on the cartridge, depending on where in the frame it began. Under the rule every such
stretch starts at phase 0: C cycles cross exactly floor(C / 357,368) boundaries. The engine prices the same stretch
the same way.

**Why.** The overworld menu's stash (3.29 frames of block moves) crossed 3 or 4 boundaries depending on the world's
frame phase.

**Where.** CLAUDE.md, "NMI-off loads"; `snes/snes.c` (`g_ctLogicalLoads`); `src/field_cost.c`, `src/menu_cost.c`.

## 2026-09-18: Adopt the zelda3 timing model in full

**Decision (owner: "adopt it in full").** Timing that depends on the CPU against the beam follows zelda3's model: the
oracle's logical frames (no lag frames) and a virtual beam, on both sides.

**Why.** The real cartridge's lag frames are overruns decided by margins of hundreds of cycles, and the CPU/beam
phase runs all the way from power-on. The engine has no clock to decide them, so a frame-exact comparison was
impossible without becoming a cycle-exact emulator.

**Consequences.** The logical oracle (`CT_ORACLE_LOGICAL=1`); the battle main loop re-derived as one iteration with
two waits; 127 checks re-pinned. Several hand-tuned "pacing" constants were removed, because they had only been
fitting the real cartridge's lag.

**Where.** CLAUDE.md, "The zelda3 model"; [disasm/notes/oracle_logical_frames.md](../disasm/notes/oracle_logical_frames.md).

## 2026-09-16 / 2026-09-18: The window wipe's curtain

**Decision (owner: "go with option 1").** The field's window wipe (`C3/01E4`, seeded by `C3/02DD`, which reads the beam
counters) was translated from the ROM and called each frame with a fixed beam taken from the cartridge's first call.

On 2026-09-18 the owner reported that the curtain "is turning square". Measurement showed that a fixed beam drives
the seed routine into a short cycle, while on the cartridge the horizontal counter differs on every call. The owner
first chose "build the clock", then "whichever you think best". The clock was measured: the phase depends on every
cycle since power-on and never resets, so an exact route means a cycle-exact emulator from power-on. **Decided: not
pursued.** The fixed beam stays, documented. The later rule (virtual beam, 2026-09-27) superseded the question.

**Where.** `src/xlat/wipe_c3.c`, `src/seq_xlat.c`;
[disasm/notes/field_window_wipe.md](../disasm/notes/field_window_wipe.md), "The curtain turns SQUARE".

## 2026-09-14: Translate the clock-dependent stages

**Decision (owner: "translation plus vendored hardware").** The motorcycle race (`$C3` sequence entry 0) depends on the
CPU clock in two ways. Its bike frames land in VRAM 5 to 15 frames after decompression, by cycle count. Its opening
and closing transitions draw scanline edges from `C3/02DD`, which reads the beam counters mid-NMI. So the stage's
program is ported as C generated from the ROM's instructions (`tools/gen/xlat.py`), each instruction run through the
vendored CPU, DMA and PPU.

*Rejected:* an idiomatic port with cycle pricing (the RNG needs 4-cycle exactness after thousands of instructions),
and an idiomatic port that stops at the clock.

**Consequences.** Exact by construction, graded per instruction (`ct.exe --xlatcmp`). The same method later carried
the window wipe, the overworld window, the gate warp (2026-09-26, 880 of 880 frames equal to the cartridge) and the
HDMA table builder. All five became debt under the 2026-10-09 decision.

**Where.** `src/seq_xlat.h`, `src/xlat/`;
[disasm/notes/sequence_table.md](../disasm/notes/sequence_table.md) sec. 11.

## 2026-06-12: The decompilation mandate

**Decision (owner).** "Nothing must be invented or inferred; everything must be based on canonical truths", and,
emphatically, "it doesn't matter what I want". Behaviour is derived only from the ROM's disassembly and the oracle's
measured state. Anything built without that confidence is undone and investigated until the mechanism is understood
in a way that scales, with no point fixes.

**Why.** A mechanism that is invented but plausible corrupts the re-implementation even when the screen looks right.
The owner's reports are treated as reliable pointers to where the truth is, never as the implementation.

**Where.** CLAUDE.md, "MANDATE: CANONICAL ENGINE, ZERO INVENTION"; [METHODOLOGY.md](METHODOLOGY.md#3-the-mandate-zero-invention).

## 2026-06-04: The approach

The project began as a re-implementation validated frame by frame against the ROM in an embedded LakeSnes core,
following zelda3. The first milestone was a deterministic, verifiable boot pipeline (`M0`), then the boot's
register and WRAM init ported and checked against the oracle (`M1`).

---

## Decisions waiting for the owner

- **The suite's attract vehicle.** The suite starts the attract demo cold at scene 433 (`--scene 433`), which skips
  scene 0 and the first load, so the play clock's random cursor starts a constant 22 ticks off the cartridge's. Only
  `--boot` matches the cartridge from power-on. Switching the vehicle means re-anchoring about 66 checks. Recorded as
  the owner's call.
- **Replacing the vendored SPC700.** Under the 2026-10-09 decision the audio is debt, and what is acceptable for its
  replacement is to be asked.
