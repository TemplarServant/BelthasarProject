# The logical-frame oracle (`CT_ORACLE_LOGICAL`)

Status 2026-09-18: the mode is built and graded (oracle-logical-frame-gato-and-xstrike).
* The battle pre-roll is the logical oracle's (`u += 8`, §3).
* The attract's X-Strike is graded against this oracle whole (attract-xstrike-vs-logical-oracle).
* The battle main loop's two-frame iteration is ported (§4): GATO's whole fight is the logical
  oracle's, frame for frame (battle-gato-fight-vs-logical-oracle).

## 1. Why

CLAUDE.md's ONE exception (user decision 2026-09-18): the CPU's timing against the beam follows the **zelda3 model**.
snesrev/zelda3 checks its C against the original ROM by running the original one main-loop iteration and then the NMI
per frame (`zelda_cpu_infra.c` RunOrigAsmCodeOneLoop), with DMA taking no time. A **lag frame** -- an iteration whose
CPU work is still running when the NMI fires, so the frame's work spills into the next one -- happens on neither side.
The port is built the same way: one iteration per frame, no CPU clock. Two things on the real cartridge depend on the
CPU's position against the beam, and this model covers both:

* **beam reads** -- the field wipe's curtain seed C3/02DD reads `$213C/$213D`. That is the VIRTUAL BEAM
  (`CT_ORACLE_VIRTUAL_BEAM`, field_window_wipe.md "The virtual beam"): `$0351` / `00` on both sides.
* **lag frames** -- this note. On the real cartridge every one of them is a CPU overrun decided by a margin of a few
  hundred master cycles out of 357,368 (battle_boot_turns.md §5: the attract X-Strike's eight lag frames and GATO's
  twelve). The port has no clock to decide them, and a clock exact enough to do so has to reproduce the beam phase
  carried from power-on (field_window_wipe.md). The logical oracle takes them out of the reference instead.

Nothing else is exempt: every other behaviour is still graded against the REAL oracle, and a timing check that meets a
lag frame grades against this mode and says so.

## 2. The mechanism (snes/snes.c, `g_ctLogicalFrames`; the hook in snes/cpu.c `g_cpuOpHook`)

* **The hold.** At the step from line 224 into 225 (vPos 224, hPos 1362: vblank starts and the NMI is raised), with
  the NMI enabled, the beam is HELD while the CPU is not waiting. The CPU keeps executing (each access still calls
  snes_runCycle, which returns straight away), and the beam, HDMA and the H/V timers stand still. **The APU runs on
  during a hold.** It is the CPU's partner, not the beam's: frozen, the audio driver's handshake (C7:063D) is never
  answered and the frame falls to the valve.
* **The wait detector** needs no list of wait loops. The CPU is waiting when it has come back to an IDENTICAL state --
  PC, A, X, Y, S, P, D, DB, the emulation bit -- with no memory write since, except to the stack's page (a 64-entry
  ring of per-instruction states, walked back). WAI also counts. From such a state only something outside the CPU (an
  interrupt, DMA, the beam) can change its course. It catches C0/EC71 `LDA $0152 / BNE` (field), the battle's
  C1/0080 -> CD/0036 -> CD/04A6 -> CD/3E44 chain and CD/3E7D, and the audio poll C7:09E1. A work loop is never taken
  for a wait, even one that writes nothing: C1/283D walks X over the eleven combatants. The first detector ("no writes
  + at most 12 distinct PCs") did take it for a wait. It released the hold mid-work and made the very lag frame the
  mode exists to remove (X-Strike 268). Mutation [0] of the check keeps that version as a mutation.
* **The hold is tentative.** A load that spans frames by design is not an overrun, and the port prices those in real
  frames. Examples: a battle's boot (CF/FB92's wipe and the staging) and a field decompression with the NMI on. So:
  * **Snapshot.** At the last instruction boundary of line 224 (within 256 dots of the step, once per frame, only
    when the CPU is not already waiting), the whole machine is snapshot with `snes_saveState`.
  * **Valve.** If the CPU reaches a wait within `LF_VALVE` = 357,368 cycles (one frame of CPU time; the attract's
    X-Strike needs at most 307,532), the hold stands. If not, the snapshot is restored at the next instruction
    boundary and that frame runs in real time.
  * **Without the rollback** (mutation [2]), the valve keeps a frame of free CPU time. The boot runs short and
    GATO's first action comes early.
* **Accounting.** At exit the oracle prints `logical-frame: H hold(s), C cycles held, V valve release(s)` (V =
  rollbacks). `CT_ORACLE_LOGICAL_TRACE` prints one line per hold (its release PC) and per rollback.

## 3. What it measures

The attract from its first scene (saves/attract_cena_inicial.state, 9,200 frames): 465 holds, 247 rollbacks (the boots
and loads). The hold release PCs were CD:3E48 276, 7E:5214 131, C7:09E1 23, C0:EC75 16, C2:034B 7 and a handful more:
every one is a real wait.

| span (`$A013` 1 -> 0) | real oracle | logical oracle | port |
|---|---|---|---|
| GATO attack A (gato_wait `--input 100:8,0:2400`, f452-605) | 157 | **153** | 153 |
| GATO song (f1023-1322) | 311 | **299** | 299 |
| GATO attack B (gato_press_action) | 226 / 222 | **213** | 213 |
| attract X-Strike (attract_demo_battle_f1850, f9-260) | 264 | **251** | 251 (was 252) |

GATO's three are the port's own spans to the frame: the op-01 close (C1/4D00 + C1/40D3-41BA) and the effect
teardown CD/0453 are exact, and what the real cartridge adds is lag frames alone.

**The pre-roll (DONE 2026-09-18).** The X-Strike's one frame was the pre-roll. `--hookpc` on the logical oracle:

* CD/0018 and CD/0F33 run at f19, CD/0F3F f20, CD/0FD5 f21 f22 f23 f24, CD/0FF8 f25.
* CD/0D53 (the arm) runs at f27 and C1/4CA2 (the first op) at f39.
* The real cartridge's frames are f23, f24, f25-28, f31, f35 and f47.

So CD/0018 is EIGHT CD/3E75 waits: 0F33, 0F3F, the four sheets and the map's two. The arm lands on the frame after
the map's second wait, and that frame is the first of the twelve C1/007E waits to the first op
(CT_FX_LOAD_TO_FIRST_OP). The old count had "the arm (1)" as a ninth frame, which carried one of the real
cartridge's lags there. Those lags are f29 f30 f33 f34, plus f19 inside CD/002A. `battleActPreroll` now does
`u += 8`.

The comparison, tools/xstrike_logical_cmp.py (the check attract-xstrike-vs-logical-oracle):

| | pre-roll | victim track t3 | span |
|---|---|---|---|
| logical oracle | 29 | 13 / 13 cursor steps on the same op-frame | 251 |
| real cartridge | 34 | 3 / 13 | 264 |
| port | 29 | | 251 |

The change moved 36 checks, each classified:

* Frames, rel and span shifted by -1 per third-stage action, and nothing else.
* The attract's two picture comparisons re-anchored to IDENTITY: the Frog Squash at port f7055 (-2) and entry 3 at
  f8899 (-1).
* The battle shots moved one frame to stay on the same moment of the action. Against the old picture only the target
  bracket differs, because it blinks on absolute time.

The run-off's CLEAR path (`u += 1`, CT_FLEE_ACT_PREROLL 22) is not re-measured on this oracle. The logical oracle's
forest battle runs on a different timeline from the real one's input script: the flee does not land.

## 4. The battle main loop's iteration -- PORTED 2026-09-18

GATO, from the song's end to his next attack: the real cartridge takes 279 frames, the logical oracle **231**, and the
port took 281 before this change. `--monitor 7E0027,7EAFB1,7E993A,7E9931,7EA013` on both oracles (scratchpad
atbgap.py) shows the same machine on both sides:

| window | frames | main-loop iterations (`$0027`) | GATO's ATB ticks (`$AFB1`) | frames per tick |
|---|---|---|---|---|
| real, song end -> attack | 279 | 103 | 102 | 2 (55x) or 3 (46x) |
| logical, song end -> attack | 231 | 103 | 102 | **2 (101x)** |
| real, attack A -> song | 516 | 207 | 204 | 2 (108x), 3 (94x) |
| logical, attack A -> song | 418 | 207 | 204 | **2 (202x)** |

### The iteration: two frames, two waits

One iteration is two frames because the idle tick C1/106E waits twice (`JSR C1/0003` with A = 3, table C1/0051).
`--hookpc` on the logical oracle, f1345-1545: 100 iterations in 200 frames.
* **Frame A.** C1/109D's wait returns. Then C1/10A4-10A9 dispatches the walk on `$A4` (C1/2D9F), C1/3714 and
  C1/283D run, and C1/10BB does `STZ $E5`.
* **Frame B.** C1/10BD's wait returns. Then:
  * C1/10C4 does `INC $E5`;
  * C1/10C6 is the `$993A` consumer;
  * C1/3714 and C1/283D run again;
  * the tail runs: C1/8130 the dead-flag sweep, the end tests, C1/81CA's Wait gate, C1/8231 the timer pass,
    C1/82A2 the cursors, C1/8313 the handlers;
  * the NEXT head runs: C1/8121 `INC $0027` and C1/1076-1099, the flee consumer. Its C1/109D wait ends the
    frame.
* **The real cartridge.** Its 3-frame iterations are lag frames: the work between the waits overruns the NMI, in
  about 45% of iterations.

### Three fitted rates, now one clock

The port carried three rates that were fits of that lag pattern:
* the logic tick at 2.5 frames (tickAcc +2/5);
* the `$A4` INC at 3 of 5 frames (schedAcc);
* the `$9897` mode pass at 4 of 5 frames (mvCadAcc).

Real 250-frame window: 102 iterations, 204 C1/3714 passes, 148 CF/E477 INCs. Logical 200-frame window: 100, 200,
100.

### The pieces, each from the ROM

* **`$A4` is INC'd only by the NMI's upload service.** CF/E3F8 does
  `LDA $A013 / BNE skip / LDA $E5 / BNE upload`, then the group's DMA, then CF/E477 `INC $A4` (wrap at 4).
  * `$E5` is the idle tick's own byte: 1 through wait 1, 0 through wait 2.
  * An action's setup zeroes it (C1/4062). So the first frame A after an action or the teardown INCs nothing
    (gato_wait f1707).
  * The entrance loop's tail is C1/3711 `STZ $E5 / STZ $A4`.
* **`$0027` is INC'd by the head, after the tail.** The parity the cursor pass tests is the one the head wrote
  two frames earlier. The port used to INC before the pass, which put its cursor passes one iteration off and made
  GATO's op-00 turn 2 frames early.
* **An enemy turn is C1/4058: two waits to C1/40A6.** The waits are C1/405F and C1/40A0; C1/10FA only drains
  CD/0009. The main loop is suspended meanwhile: no head, no iteration, no mover (`INC $A0FD` stops CF/F978).
  * Logical: find -> `$A013` = +2 (f450 -> f452, f1021 -> f1023, f1551 -> f1553).
  * Real: +3, i.e. one lag frame.
  * `CT_BATTLE_ENEMY_THINK` 3 -> 2. The demo's C1/4058 is the same, 3 -> 2.
* **The first head is the entrance loop's last frame** plus `CT_BATTLE_BOOT_CPU`. This is C1/8030-8127's CPU,
  rolled back to real time where it outruns a frame. Logical: 0AD 0, GATO 0, forest 1, 07D 4. It is still not
  derived and takes the 0 (was the real forest's 2).
* **The mode pass C1/3714 runs on both frames.** The mover's segment service CF/F978 is the NMI's, every frame
  outside an action.

### What grades it

**battle-gato-fight-vs-logical-oracle** (tools/gato_logical_cmp.py) compares the GATO fixture beside the logical
oracle's gato_wait, port frame + 227 = oracle frame. Everything below is identical, on every frame from the first
head f372 to f1935:
* `$A013`: attack A, the op-00 turn, the song, the teardown, attack A again;
* the three seats' `$AFAB`, and GATO's `$AFB1`;
* `$B185`, `$0027`, `$00A4`, `$00E5`;
* GATO's 136 mover steps on the oracle's `$1D12/$1D29`.

It has seven mutations, one per piece. The four mid-battle sweeps (the class-5/6 movers from wa2158 / wa2510 /
wa4620, and the idle walk from bat2) are graded against the logical oracle's continuation of the same seed
(tools/sweep_logical_cmp.py): 17, 18 and 41 mover steps, and 7 idle-window ticks, all identical.

**The attract.** The 0AD's `$A013` edges equal the logical oracle's (f1856 / f2107). The 07D's `INC $A013` comes
one frame early: its staging spills 2 frames against the constant's 1. The Frog Squash and entry 3 pictures are
IDENTITY with the cartridge 8 frames earlier (f7047, f8891).

### Not ported with it

* **The menu's own cadence.** The menu runs on C1/10C6's `$993A` consumer, every fourth frame by `$9931`. The
  menu and its nav repeat still ride the tick.
* **PC command paths.** C1/4058 is not modelled for PC commands (CT_BATTLE_DISPATCH_LAT), nor for the reactive
  counter (CT_BATTLE_COUNTER_DELAY).

## 5. Next

1. ~~**PC physical spans.**~~ **DONE: a PC physical runs its own script** ([battle_pc_physical_script.md](
   battle_pc_physical_script.md)). Its span is the script's op 01, the logical oracle's 124 / 139 / 141 / 141 (and
   the crit 169, Marle's far shot 135) with every cursor and foot identical; the stored spans are gone from that path.
2. **Fitted latencies.** `CT_DEMO_STAGING_SPILL` (logical 0AD 1, 07D 2), `CT_BATTLE_DISPATCH_LAT` and
   `CT_BATTLE_COUNTER_DELAY`: each is to re-measure on this oracle.
3. **The menu's `$993A` cadence** (above).
4. ~~**The 07D's first cursor.**~~ **FIXED (922aee1, integrated on top of this work).** It landed on `$B185 = 06`
   against the cartridge's 05. The cause was the RNG stream carried between the attract's fights, not the
   iteration: the AI init's draws run before C1/80EB, op 04 (C1/98C5) is ported, and C1/FD5E leaves `$2E = 0`
   (battle_boot_turns.md). attract-battle-rng-stream-vs-oracle grades it; on the merged tree the suite is green and
   its 3 mutations are killed.

## 6. NMI-on work never costs frames (user decision 2026-09-30, FIXED) -- and the battle NMI's line-64 cut

**The rule.** CPU work done with the NMI enabled between two waits ends inside its frame on both sides, however long.
snes.c holds the step into 225 until the main thread waits -- no one-frame valve (LF_VALVE), no rollback to real time.
Rejected (recorded in CLAUDE.md): floor(C / 357,368) per stretch -- ordinary battle iterations run 370k-470k cycles on
GATO's turns (C1/8231's status clock, C1/0299's rebuild, C1/B223's sweep), so it brought the lag frames of the 2026-09-18
rule back and needed the whole engine's cycle cost in the port; and pricing the NMI's cost per scene (the CPU-timing class).

**The count, for the traces.** `lfStretch`: at every instruction boundary the cycles since the last one, less the refresh
bucket's and HDMA's growth, when no interrupt handler runs (snes/cpu.c `g_cpuIntHook` pushes the SP an interrupt came
from; the SP coming back to it after RTI pops it); a wait of the main thread (lfInWaitLoop, looked at when the PC goes
back) ends the stretch. `CT_ORACLE_LOGICAL_TRACE=1` prints `stretch-end <start> (frame) -> <end> (frame) C=<cycles>` for
every one past LF_FRAME / 4. GATO's fight (gato_wait, no input): the boot C7:09E3 -> CC:F05F 3,663,320; stretches of
431,764 / 370,316 / 375,248 / 464,032 in the fight (C1/8231, C1/0299, C1/B223 ...), near-misses at 345,094 and 318,710.
LF_SAFETY (60 frames) is not the rule: a hold that long means the detector missed a wait -- released, said loudly.

**The battle's boot under the rule** (src/battle.c battleBootBandArm): C1/0000's setup (CF/FB97's clear .. CC/EECB) is
one held stretch; what is left is the ROM's own wait -- CC/F058 `INC $98CA`, CC/F05B waiting for $98CA = 4 while the
NMI's staged uploads CC/E000 (steps 1-3, `INC $98CA` at CC/E0A5 / E1AC / E22A) run one an NMI -- then the #08 post and
the band's seed. GATO: the upload's C7:0196 clear f281, #08 f284, seed f285 (the valve: f296); the 0AD (build/_loc0AD.state):
clear f140, #08 f143, seed f144 (valve f157). The port: `1 + apu + 3`, the upload priced on the battle's background block
(the cycle-tag probe, gato_wait: from f224 nmi 26-30k + dma 4-10k, hdma 17.9k -- LD_BG[1]; on the field's block the song
came out 55 NMIs, 1.35M cycles short of 59). With it GATO's whole fight is the rule oracle's on all 1564 frames, and
residuals of the old valve close: Crono's third slash 132 both (was 141), Water's objects (was 107 frames), Ice 426 both
(was 429), Yakra's name box (was 16 / 13).

**The line-64 cut** (option A): during an action ($CD2E = 0, $A013 != 0, $A028 = 0) CD/09AA `LDA #40 / JSR CD/3B82`
waits for line 64 and the service after it (CD/1609's effect program, the NMI's tail) rewrites per-line records HDMA reads
with no latch. Under the rule snes.c holds the beam from CD/3B82's RTS (CD/3BAA) to that NMI's RTI; battle.c copies bank
7E before the service and the compositor's HDMA runs of lines 0..63 read the copy (main.c's --sweepfxcompose takes the
oracle's copy at the hold). Aura Whirl (fx_aura_whirl_c955, f0-200): sprite px 1057 -> 0, rows' registers 5262 -> 371.

**The service's own raster waits: more cuts** (2026-09-30). The background program's op C1 (CD/1D93 `LDA [$40] / JMP
CD/3B82`, reached CD/1654 -> CD/16AF `JSR ($16B5,X)`) is the same wait with the program's line. Char 4's Frog Squash on
gato_wait (CT_ORACLE_RASTER_TRACE=1, no hold): 256 NMIs enter it at V=66 with A=80 and leave at V=128. Held, the beam
never reached an HBlank edge -- the oracle hung (the safety's WARNING at CD:3B90 / 3B92). The wait is the ROM's, so the
beam runs through it: the hold lets go at CD/3B82's entry and takes hold again at its exit, a NEW CUT at that line. Each
cut keeps bank 7E as it stood at its exit; the HDMA runs below cut 0 and between cut k-1 and cut k read copy k, the runs
from the last cut on the live WRAM (snes.c g_ctLineCut*, battle.c battleLineCut: a wait entered on line L ends on
max(b, L + 1), the service starting at 0). Frog Squash (fx_frog_squash_c750 = battle_ab tech 4 7's drive under both
switches, --sweepfxcompose f0-500): non-sprite px 1,458,126 -> 109,370, rows' registers 32,587 -> 1,073 (cuts: line 64
x472, line 128 x256).

**COLDATA carries over.** The battle writes `$2132` only through channel 3's tables (no CPU store in its banks; the scan's
FD/EB76 is data), and an entry writes only the components its bits 5-7 name, so the top lines keep the others from the
previous frame's last run (HDMA runs on lines 0..224). The compositor walks channel 3 to line 224 and starts the next frame
from what it leaves (gBattleFxFixCarry; --sweepfxcompose takes the oracle PPU's own at the frame's start) -- the rows 0-15
residual of Aura Whirl and Frog Squash (the blue component the port started at 0). Aura Whirl (fx_aura_whirl_c955,
f0-200, both switches): sprites 0, non-sprite px outside the V-IRQ blank 0, rows' registers 371 -> 130.

**The APU stands with the beam under the rule** (2026-09-30). The oracle ran the SPC through every hold (g_ctLogicalHoldApu = 1) for the valve's sake: a handshake inside a held stretch had to be answered before the valve fired. With no valve a poll of the APU is a wait (lfInWaitLoop: the plain poll and the counted one) and ends the stretch, so under CT_ORACLE_NMION_HOLD=1 the oracle now holds the APU too (lfReadEnv; CT_ORACLE_HOLDAPU=1 keeps the old way) -- the zelda3 model's SPC, a frame per frame whatever the CPU spent, and Belthasar's own relation. Measured: the line-64 cut's ~20 held lines of every action frame had become SPC time no port frame has -- battle_status_cmp's lockdrop, under both switches, dropped the tech a frame early on the cartridge (the #FF handshake answered sooner); with the APU standing it is identical. The whole suite under both switches: one check fixed, none broken.

## 7. The infinitely fast CPU (2026-10-01, CT_ORACLE_IFAST=1 with the rule)

The rule held the beam only at the step into 225. Two classes stayed on the CPU's clock: a sound handshake met when the
CPU's work had carried it past line 224 (plain option A charged it a frame -- the X-Strike's post at f7442 line 224, then
lines 200 and 175 the next frames, the oracle's span 252 against the port's 251), and the main thread's writes landing
mid-picture (CE/EE6E's presets from CD/05DA, below). The user delegated the choice to "the decision similar projects take":
snesrev's ports run one main-loop iteration and its NMI per frame and draw the frame after it -- a CPU that costs no time.
snes.c (lfIfast): per instruction, any context, a wait flag (a loop back to a state it was in with nothing written, a
counted APU poll, WAI; held while the PC stays inside the loop, dropped by a WRAM write or an interrupt's entry); in
snes_runCycle, with the NMI enabled and no wait, the cycle is held -- the beam, the timers, HDMA and the SPC stand --
EXCEPT while the CPU runs bank C7 (`snes->cpu->k != 0xC7`; the exception below, user decision 2026-10-01). The hold at the
step into 225 and the line-64 hold are its special cases (the line cut's copies stay for the compositor); NMI-off work
keeps its phase-0 rule (the held cycles are taken out of the frame's spent cycles).

MEASURED: the X-Strike (tools/xstrike_logical_cmp.py) 13 / 13, span 251 = the port; GATO's whole fight
(tools/gato_logical_cmp.py with CT_ORACLE_VIRTUAL_BEAM=1) identical on every frame -- $A013, the ATBs, $B185, $0027,
$00A4, $00E5 and GATO's mover steps -- where plain A left $A4's phase one off and the sixth action a frame late. Before
the C7 exception the boot was 11 frames shorter than plain A's (upload clear C7:0196 f270, arm CD:0512 f274, against
f281 / f285: the transfer loop free) and its uploads were corrupt (below); with C7 real it is 6 frames shorter (clear f275,
arm f279, `$0027 = 01` f355; CT_GATO_OFF=221, the fight identical on all 1564 frames). No detector WARNING.

**THE EXCEPTION: THE CPU's SIDE OF THE APU PROTOCOL** (user decision 2026-10-01, CLAUDE.md "The infinitely fast CPU").
With every CPU cycle free the two-processor transfer broke, on both sides. The driver's receive loop (ARAM $1104-$111B,
uploaded from C7: `CMP X,$F4 / BNE / MOV A,$F5 / MOV !$0097+Y,A / INC Y / MOV $F4,X` -- the ECHO, at $110E -- `/ MOV A,$F6
/ ... / MOV A,$F7 / ...`) reads ports 2 and 3 AFTER it echoes the token. On the cartridge the C7 loop spends ~35 CPU
cycles between seeing the echo and its next `STA $2142 / $2143` (C7/09E5-09E9, `STA $84 / DEX DEX DEX / BNE`, `LDA [$12],Y
/ STA $2141 / INY`) -- the protocol's settle. At zero CPU time the next unit's bytes overwrote the two the SPC had not read:
the IFAST oracle's `70 01` set B (ARAM $2F00, the 0AD boot from the f1740 state) had 3776 of 6144 bytes wrong, exactly
the 2nd and 3rd of every unit (the logical oracle's: 0), the attract's first song load left samples 3-7 2374 / 1222 / 914
/ 2331 / 1794 bytes off the ROM, and the port's rule clock, the same model, did the same (`--songload 0x30`: `+1 +2 +4
+5 ...` mismatches). So while the CPU runs the sound driver's own code its cycles are REAL on both sides: the beam, the
timers and the SPC advance. THE BOUNDARY IS BANK C7, derived: C7 is entered only by `JSL C7/0004` (59 sites in the code
banks, and the title overlay's $7E:3775) and the reset's `JSL C7/0000` (C0/0053); a scan for JSL/JML into C7 finds six
other hits, all operand bytes (e.g. C0/4A14 `ADC #2200 / TAY / LDX $C7`); and the C7 code ($C7:0000-0AE8) calls nothing
outside it (its two 0x22 bytes, C7/04D9 and C7/0905, are `JSR C7/0922` operands). An interrupt taken inside C7 runs in its
own bank and is held like any other work (its own waits excepted). The call still STARTS where the CPU's last wait left
the beam (the caller's work and its `JSL` are free), so nothing depends on where the CPU stood.
MEASURED with the exception: the uploads byte-exact on both sides (the oracle's `70 01` set B 0 / 6144 wrong; the attract's
first song's five fresh samples = the ROM; the port's `--songload 0x30` PASS); the first `10 18` (attract_cena_inicial f1)
30 vblanks on the oracle (28 before), the 0AD `70 01` 12 (9 before); GATO, the 07D play-anim list (6 / 6 arms, 459 / 459
poses) and the X-Strike (13 / 13, span 251) unchanged; the gate cutscene's `10 18` (scene 8, `EA 18`) 38 frames on the
rule oracle against 39 in real time (the loop's tail no longer overruns the NMI).

**The port's side: its SPC under the infinitely fast CPU** (2026-10-01; src/audio.c `CtAudioRuleOn`, on with
CT_ORACLE_NMION_HOLD=1 + CT_ORACLE_IFAST=1). Measured first, on the oracle (ct.exe --peek PEEK_APUTRACE + PEEK_WRITEWATCH,
--cputrace CPUTRACE_BEAM=1):
  * WHY PLAIN A CHARGED GATO's FIRST CHIME A FRAME. The SPC was never busy: the driver read the `19 42` post and echoed it
    114 APU cycles later (SPC $0D53). The post came at the hold (line 224 h 1362), the poll released the hold, the NMI
    fired at once and waited for line 64 (CD/09AA), and the driver's own tick rewrote its port 0 (`$F4 := 00`, SPC
    $02DD, every ~5,400 APU cycles) inside that NMI -- the CPU never saw the echo, timed out its $400 polls and re-posted
    at line 112 of the next frame. The same at f1586 and on the 07D (f267 at line 220: four frames). CPU position, every
    time; IFAST removes it (every post below starts where a wait left the beam).
  * WHERE THE CPU POSTS under IFAST: the field's main loop right after its NMI's pad latch (C0/EC15 `JSL C2/8002` ->
    C2/84F7 `LDA #01 / BIT $4212 / BNE`, the auto-joypad read: 4224 master cycles from the vblank, line 228 h 132; every
    field post of the attract enters C7/0004 at line 228 h 146-200); the battle's main loop after its service's pad latch
    (CD/09C6, `$7E` = 1/2: GATO's chimes at 228 h 162-168) or its last raster wait (CD/09AA line 64, the program's op C1:
    the X-Strike's posts at 64, the Frog Squash's at 128); the boot's band arm (`$7E` = 0, CD/09F1, no latch) at 225 h
    12-22; the exit tail's `70 00` (D1/F4C0, no latch) at 225 h 14-16.
  * WHAT THE INTERRUPTS DO WHILE THE CPU SITS IN A JSL (their work held, their waits the beam's): the field's NMI waits
    for the pad every frame (C2/84F7: 4224 master cycles from the vblank, attract f2-f30); the field's V-IRQ C0/ECCC
    (VTIME = 211, C0/0B44 `LDA #00D3 / STA $4209`, re-armed by every field NMI at C0/EA70 `LDA #A1 / STA $4200`) waits for
    line 211's H-blank (C0/ECEB `LDA $4212 / BIT #40 / BEQ`) unless $010F bit 7 (C0/ECD5) -- MEASURED inside the gate
    cutscene's `10 18` (--cputrace from a PEEK_SAVE at f160): the IRQ entered at line 211 h 56, its wait left at h 1320 (the
    read after the line's HDMA), the beam held from there; per frame the beam outside C7 was the pad's 4188 + the IRQ's
    1264 (C2 / C0 in the trace). The battle boot's NMIs (`70 01`, twelve of them) wait for nothing and meet no IRQ wait.
  * THE LOADER ON THE LISTING, FROM THE LAST WAIT: the port's clock prices C7 on the listing (the exception), starts the
    JSL where the CPU's last wait left the beam and walks the NMIs' and the V-IRQ's waits as beam time; it prices the
    oracle's own transfers from PEEK_SAVE states the frame before (ct.exe --songload, CT_SONGLOAD_SEEDPAGE/SEEDAPU/
    RULEPOST `4224,4224,211`): the attract's first `10 18` (f1) 30 NMIs, RTL line 252 h 368 against the oracle's 253 h 1026
    (without the IRQ wait: 238 h 286); the 0AD boot's `70 01` (f1742, `CT_SONGLOAD_CMD=70`, `4224,0`) 12 NMIs, RTL line 137
    h 807 against the oracle's 137 h 626; in the lockstep (--vmdiff, the gate cutscene against the rule oracle) `EA 18`
    38 frames, RTL line 223 h 834 against C0/3C47 at 221 h 740, and `EA 00` line 75 against 69 (no vblank crossed; the
    residual is the transfer's own, its polls 1812 against the oracle's ~1806).
  The port (src/audio.c, src/field.c, src/battle.c, src/ct_vm.c, src/main.c, all behind the two switches): Belthasar
  brings its SPC up headless and runs it one frame per frame (a headless fight's too: CtBattleSoundSink.frameSpc after each
  tick); every CtAudioService in engine mode 0/1 starts at max(the caller's post position, the SPC's progress into the
  frame's slice), C7's work on the listing (the exception) and the caller's own free, the NMIs' own waits as beam time
  (mode 2, NMI off, keeps the listing's clock), the field's V-IRQ wait at line 211 where field.c says one is armed
  (CtAudioSetRuleIrq: a field script's JSL -- the VM's sinks -- in a field frame with $010F bit 7 clear; the menus', the
  shops', the world's and a fight's posts meet none); a JSL that touches no port costs nothing; the VM keeps its suspension for
  the frames lost but drops the tail-overrun rule (C0/EC60's tail is CPU work); the battle's posts carry its NMI's line
  wait or pad latch and stall the main thread (sndStall) for the vblanks a JSL crosses; --vmdiff runs the SPC slice after
  the VM's tick. Under the IFAST oracle no battle post crosses a vblank (GATO, the attract's three fights). A run that
  starts from a WRAM image (--loadwram, --battletest, --menutest, --loadstate) carries no SPC: it brings one up only from
  the lockstep oracle's state (CT_FIELDLOCK, CtAudioSeedFromApu, the image's driver page kept), else stays silent as before
  (a device's SPC there keeps the listing's clock) -- measured on the stairs fixture, a cold driver uploads the song $0C
  the cartridge's already plays (+8 frames on the first load).

**OPEN -- the MAIN THREAD's table writes land mid-picture** (a CPU-timing class the rule does not cover yet). The 130 rows
left on Aura Whirl are its first and last frames (f11 from row 80, f191): the HDMA tables' headers ($C867 .. $C9CF, 0x1E
bytes each) are installed by CE/EE6E's preset copy (`LDY #C948 / LDA #001D / PLX / MVN 7E,CE` ...), JSL'd from CD/05DA
(the band animator, C1/003C -> CD/0012 -> CD/0534 -- the MAIN thread) and from D1/F3F6 / D1/FE69 (the IRQ side). On the
cartridge the new header is read by HDMA at whatever line the CPU reached (f11: rows 0-79 the old preset, then the new);
the port and --sweepfxcompose take a frame's state after its main thread for every line below the NMI's last cut. The
zelda3 model renders a frame after its main-loop iteration (all of the iteration's writes before line 0); making the
oracle do that (hold the beam at line 0 until the main thread waits) would move every scene, so it is a decision for
the user, not taken here.

**Migration** (until every family is converted): `CT_ORACLE_NMION_HOLD=1` / `CT_ORACLE_LINE64=1` switch the rules on in
both exes; without them the old valve stands. OPEN under the rule: the lifeline scenario of tools/battle_status_cmp.py
(Crono revived, then ready at c1126: the port enqueues him on the c1130 cursor pass -- `$B185 = 04`, his entry -- the
cartridge on the next, c1134; the port's `$B186` reads 00 where C1/82E2 writes 01 every pass). CLOSED 2026-09-30
(battle_menu_pass.md sec. 12): the enqueue is two steps, the close wiped twice and the open wiped on the `$A013` frame; and
the battle's boot is `2 + apu + 3 + 1` under the rule (C1/0000's own frame is the module's tick 2; the #08 post and the seed
take a frame each -- measured from power-on on the 0AD and the 07D: `1 + apu + 3` put #08 at `$7C` = 0F, two early). The
fixture is identical from GATO's first action on, at the cartridge's frame minus 228.

**The battle family under the three switches (2026-10-01, the flip's triage; proposals in tools/rule_flip/battle.json).**
The rule's cartridge runs GATO's fight 17 frames sooner than the valve's (gato_wait: C1/0000 at f222 in every model,
`$CA1E = FF` f296 -> f279, `$0027 = 01` f372 -> f355; every later event -17), the --battletest fixture 11 sooner (its
pinned arm CT_BHUD_ARM_FRAME - 11), so the fixture's offset is 221 and the fight is identical on all 1564 frames
(tools/gato_logical_cmp.py, which under the switches also gives its oracle the virtual beam: with IFAST and the real beam
the field's runner C0/5A46 reads a frozen `$213D`, cuts every pass, and gato_wait never reaches its battle).
* THE RENDER CLOCK (src/battle.c CtBattleRenderClock). GATO's pose cursor, the low-HP blink, the combat-live gate and the
  target page ran on the module's frames, calibrated on the pinned boot; on the cartridge they are fight-locked
  (`--monitor 7E990D`: every pose change -17 with the fight; `7EA110`: the blink's toggles f303 315 ... -> f286 298 ...).
  The clock now carries the fixture boot's move: every anchored battle shot is byte-identical to the valve's at the
  shifted anchor, and a fixed-frame shot moved -11 is the old picture (same fnv).
* THE BOOT'S SONG UPLOAD in a WRAM run: a cold driver keeps the old clock (CtAudioSetRuleOff), which charges the battle
  NMI's work inside C0/1BCE's `14 45` -- 59 NMIs where the rule's cartridge takes 53 (f222 line 228 -> C0/1BD2 f275
  line 240): the field-started fight ran 6 frames late. CT_AUDIO_SEEDSTATE (Belthasar) gives such a run the state's own
  SPC on the rule's clock: 52 NMIs, returning 26 lines short of the cartridge's (advancing the SPC 1-2 frames before the
  post changes nothing) -- the fight one frame early. (A cold driver on the rule's clock gives 53 for gato_wait but 51
  against the cartridge's 42 for forest_wait_ambush_active, where the state's own SPC gives 42: the driver's state is part
  of the price.) CLOSED 2026-10-01: THE FIGHT's V-IRQ. Every NMI of a fight -- D1/E77C, installed with the IRQ vector
  D1/FE97 by D1/F1B0, and the exit tail's D1/F4C0 -- opens `LDA #A1 / STA $4200` (D1/E792, D1/F4D6): the V-IRQ armed at the
  field's VTIME 211 (C0/0B47; the only other VTIME write is FD/EB87's register clear), and D1/FE97 waits for that line's
  H-blank (D1/FEB8 `LDA $4212 / AND #40 / BEQ`), the field C0/ECEB's wait. A beam trace of two frames inside gato_wait's
  `14 45` (--cputrace CPUTRACE_BEAM=1 from a PEEK_SAVE at f230): the IRQ enters at line 211 h 66, the wait leaves at h 1114,
  and those ~1,050 cycles are the only beam time outside bank C7 (the NMI's MVNs, its DMA and FD/E39C are held); --hookpc
  C709E5 CT_HOOK_CYCLES=1 over f223-f274: main + poll + hdma + refresh = 357,368 - ~1,100 every frame. The port's rule
  clock now meets it on every fight JSL (src/field.c FIGHT_IRQ_LINE, the boot's, the fight's, the exit chain's): `14 45`
  is 53 NMIs, returning on line 256 (the cartridge's 240), and the field-started fixtures run on the cartridge's frames
  (battle_ab: `$9931` from c279 / p279, every tech's and item's rise on the cartridge's frame).
* NOT NEEDED by the family's checks and left OPEN: the band arm one frame before the first CD/05C4 step ($9931/$A4EF a
  frame late in the boot), C1/8570's `$B188 := $93EE` copy.

## 8. A restored state is at a logical frame's start (2026-10-20)

The phase-0 rule for NMI-off loads (CLAUDE.md, 2026-09-25) measures the CPU work a frame had spent when the NMI went off
from three marks set on the step into line 225 -- `lfVbCycles` (the machine's cycle count), `lfVbRefresh` (the refresh
bucket) and `lfVbIfast` (the infinitely fast CPU's held cycles). None of them is in the savestate. Every saved state of the
oracle sits exactly on such a step (snes_runFrame stops on the instruction that sets inVblank at V=225, V=240 on an
overscan frame), but loading one left the marks at 0: a first frame that turned the NMI off before any vblank -- a location
load committed on the frame after the state -- priced its phase hold from the machine's whole cycle count.

MEASURED, the playthrough's trial (leg 31, from leg 30's end, the box-driven pad): continuous, the load back into the
courtroom (f9436) reads `NMI off at C0:0B5D, 4126 cycles into the frame -> phase hold`; from a state saved on f9436 it read
`102365085092 cycles into the frame`, the load ended frames off and the machines were 40641 bytes apart a few frames
later. Every tool that plays in stretches from its own states (playthrough.py's route walker and scene player,
tools/walkto.py) could leave the cartridge's continuous play at such a frame.

snes_handleState's load now puts the marks back where the step left them: `lfVbCycles = cycles - hPos` (the instruction's
tail is this line's hPos), the refresh less the line's 40 cycles if H has passed 536, and the ifast mark at the current
count (nothing of the frame has run). A state that is not at a vblank's start -- the valve's own snapshot at V=224 --
keeps its marks. After it, a cut at f9436 (and at the first flashback's commit, f3669) leaves the machine byte for byte
the continuous run's.
