# The field's $C0:FE00 cursor `$7E01F8` = the play clock at the load (2026-10-08)

User report: the attract's loc 0E6 robots (objs 18-22, fn0 `7F 06 / 12 06 80 03 06 / 83 6A 03 / 10 04 / 83 69 03`)
load enemy ids 69 69 69 6A 6A in the port against the cartridge's 69 6A 69 69 6A, though op 0x7F itself is exact:
the cursor `$01F8` it steps drifts from power-on (cartridge 11 at c50, 1B c2148, 1F c2492, 30 c2889 ... 32 at 0E6; the
port `--scene 433` 00 until f2058, then 06, 0B, 1E ... 2A).

## 1. Who writes `$01F8` -- MEASURED

`CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1 PEEK_WRITEWATCH=01F8:1 ct.exe --peek boot x --frames 8200`: 23 writes,
two writers and only two (the PC printed is the one after the store):

| writer | what | when |
|---|---|---|
| C0/0C75 (`0C70 LDA $0400 / 0C73 STA $F8`) | the field init C0/0B86, run by every location load (C0/0085) | 15 times, one per load |
| C0/6D10 (op 0x7F, `6D0B LDA $F8 / INC A / 6D0E STA $F8`) | a script's random draw | 01C x3, 0E6 x5 |

No C0/AE29 (the mover's bounce) and no C0/A22F (the blink) before c8200. So the cursor a location's scripts draw from
is **the play clock's frames byte `$0400` at the load** -- and the drift was never the cursor's: it was the clock's.

## 2. The clock's ticks -- C2/85AE once per NMI that reaches the pad latch

C2/85AE (writer C2:85BC) is the third routine of C2/84D2, which `JSL C2/8002` reaches from the field NMI (C0/EA63 ->
C0/EC15, whatever `$0152` says: `EA82 LDA $0152 / BNE / BRL ->EC15`), the world's (C2/031B) and the fight's CD/088D once
`$7E` = 1/2 (memory ct-play-clock-0400). The port ticks at the head of a frame -- the NMI that ended the previous one.

Comparing the two sides seed by seed (convention-free: both seeds sit in NMI-off code) the port ran ahead 1-2 ticks
on every field leg and was exact on the world legs. Three mechanisms, all of the same class -- a vblank the port ticked
whose NMI the ROM has disabled or replaced:

### 2.1 C0/2848's entry frame

`C0/0094 JSR C0/2848` -> `JSR C0/286C`, whose last legs are `28A1 JSR C0/0B28` = `0B38 LDA $4210 / BPL ->0B38` (the
vblank FLAG, the NMI still off from the load) then `0B3D LDA #81 / STA $4200`, and `28A4 JSR C0/EC60`. The vblank
between the load and the pipeline pass is NMI-off; the first tick after a load is the NMI 286C waits for, the one
before the ramp's first `INC $19`. MEASURED on every load of the attract: seed -> first INC = 1 tick on the
cartridge, 2 in the port (`loadFadeEntry`'s head ticked).

### 2.2 The reboot out of a `$C3` stage (and a gate warp)

The stage's frames carry its own NMI (no C2/8002), and its exit `$7E:303B JML $C0:0000` -> C0/005D `JSR C0/0B4E`
(`SEI / STZ $4200`) keeps the NMI off until 286C's. The port's reboot frame ticked: `seqOpen` / `gwPhase` are cleared
on that frame before the clock's gate reads them. +1 per stage (the title, every `FF 61` credits page, 0E6's).

### 2.3 The fight's entry: two field NMIs the port did not tick

`--hookpc` C0/1770 / C0/1890 / C0/1895 / C0/18A4 / C0/18A7 / C2/85AE into the 0AD fight: op D8's iteration, C0/1770's
bit-5 half (`1790 AF4E / FD:FFF7 / JSR EC60`), the iteration it returns into at C0/00A7, then C0/188C's
`024C` staging, `JSL C0/2C41`, `JSR C0/00DE`, `18A4 JSR C0/EC60`, and `18A7 JSL C1/0000` the frame after -- a tick at
the head of each of the four frames after the op (the last in the frame D1/F24D zeroes `$7E`: the fight's NMI from
there, CD/09F1, no latch). The port's module ticks 1 and 2 (`battleFightNmiOn` < 3) are those two field NMIs; its
pre-fight branch returned before the clock.

### 2.4 The demo's staging spill -- a frame the rule does not have

Both demo fights opened their action ($A013 = 1) a frame late: the turn machinery was in step through the cursor find
(`$B185`, `$AFAB` on the same frame), then `CT_DEMO_STAGING_SPILL = 1` -- C1/BA33's staging, CPU work that the real
cartridge spilled into a frame and the rule (the infinitely fast CPU, CLAUDE.md) makes free. MEASURED on the rule's
cartridge: cursor find -> `$A013` = 2 frames in both (0AD trigger+98 -> +100, 07D +97 -> +99), the two C1/007E waits
of C1/4058. The spill now applies only with `CT_ORACLE_NMION_HOLD=0`.

## 3. Result

`--boot` (the power-on route: scene 0's boot script, then loc 1B1) against the rule's cartridge from power-on: every
seed of `$01F8` equal through loc 0E6 -- 00 11 1B 1F 30 13 39 16 33 0D 27 12 12 32 (tools/f8_cursor_cmp.py; check
field-f8-cursor-seeds-from-power-on-vs-rule-cartridge; before: 2 of 14, the port +3 by 1F5 and +14 by 0E6). The 01C
draws (`7F`, 31 32 33) follow on both sides. Also graded on the way, the 0AD fight trigger -> ack in one convention (the
sample where `$0117` turns 21): cartridge 378 ticks, the port's `--selftest` 378 (was 377); the 0AD exit tail's stash at the
ack is now the cartridge's (c4302 `$1D8B = 0586 / 02C3`).

The suite moved with 2.4: 54 attract checks re-anchored by vehicle (the old and the new binary on the
`$0100/$0119/$1E00/$0384/$A013` timeline) -- the lockstep vehicle -1 after the 0AD demo action, -2 after 07D's, -3 after
087's; the `--selftest` vehicle (fights synchronous, no spill) +1 from loc 112 and +2 from ~f6369, where C0/91F8's
auto-scroll (`$01BC` armed) steps the camera from `$0400`'s bits.

## 4. Open

* **0E6's five `7F 06` run in the INIT PASS** (the cartridge's C0/6D10 at c7515-7516, inside the load, 30 frames after
  the seed), and the port's init runner `runFunc` has no op 0x7F: `$7F020C` stays 00 and the cursor 32 (the live VM's
  `7F` at 01C is fine). The census commit ff229b3 (branch claude/objective-cray-618356, disasm/notes/live_vm_op_gaps.md)
  moves 0x7F into a family both runners call; with it the robots draw E9 70 DC CD 2F from the cartridge's 32.
* **`--scene 433` starts the attract cold** at 1B1: no scene 0 (17 ticks on the cartridge, c34-c50) and no 1B1 load +
  ramp before the title (10 more) -- its clock is a constant 22 behind the cartridge's on every seed (05 vs 1B ...
  1C vs 32 at 0E6). Every attract check runs that vehicle; only the `--boot` route is the cartridge's.
* Frame-only residuals seen on the way (no tick moves): the 07D fight's `70 01` / `70 00` uploads one frame longer
  in the port (entry stall 15 vs 14, exit 15 vs 14); the load windows' lengths (attract_leg_clock.md sec. 7).
