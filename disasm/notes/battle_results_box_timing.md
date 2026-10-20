# BATTLE VICTORY RESULTS — the TIMING machine (why the reward box "takes long to appear")

Derives the exact frame budget between the killing blow and the first reward glyph, and the per-line
show/hide cadence of the results box. User report (translated from Portuguese): *"after the battle ends, the message with the rewards
takes long to appear"*.

Every claim below is anchored to a ROM address (`python tools/dis.py <fileoffset>`, fileoffset =
`(bank-0xC0)<<16 | addr`) **and/or** an oracle observation. Claims pinned by only one of the two are
labelled **HYPOTHESIS** explicitly. Writer PCs from `--writer` are the PC **after** the store retires.
Base state: `saves/battle_re/forest_battle_3hetake.state` (3x Hetake, the NORMATIVE normal fight;
XP 9 / TP 3 / 48G / one Tonic drop). Probe: a copy of `build/ct.exe` (never `build/ct.exe` itself).

> **HEADLINE.** There is **no long pre-message timer**. From the victory DISPATCH (`C1/356D`, the frame
> `LDA #28 / STA $5DF1,Y` at `C1/35CA` arms the victory pose) the first reward glyph is on screen
> **71 frames** later, decomposing as **64 hard-coded frames** (`C1/3681: LDA #40 / STA $5DC9`, loop
> `C1/3686-C1/3693`, one `JSR C1/007E` frame per `DEC`) + **5 frames** of reward application + message
> build (`C1/83BB` -> `JSL FD/AD17` -> `JSL CD/0021` -> `CD/01B3` -> `CD/025E`+`CD/027F`) + **2 frames**
> of NMI tilemap upload (`$CA24` consumed at `D1:FE70`). On the same single clock the last enemy is
> delisted at `C1:B2C3` **48 frames before** the dispatch (45 of enemy dissolve + the 3 `JSR C1/007E`
> pose frames at `C1/358E/3596/359E`), so kill -> first glyph = **119 frames** on this fixture.
> The perceived "delay" is then the MESSAGE ENGINE's own cadence, not a hold: each line is visible for
> **176 frames** (16 unconditional `CD/020A` + a **160-frame DWELL** `CD/019D[($2991>>3)&7]`), then the
> **window CLOSES COMPLETELY for ~19-20 dark frames** (`CD/01FD STZ $CCEC` + a second `CD/020A` 16-frame
> ramp + ~4 frames of rebuild) before the next line opens. **Lines AUTO-ADVANCE with zero input**; a held
> A only cuts the dwell to 1 frame, and never skips the 16+16 ramps.
> Our engine: `CT_BATTLE_VICTORY_CELEBRATE 212` (src/battle.h:92) puts the box at victory+211 instead of
> victory+71, and `src/field.c:3816` forces ONE permanently-open box that never closes between lines.

---

## RAM MAP (every byte used below)

| Addr | Meaning | Writer(s) observed |
|---|---|---|
| `$7E:AF02+n` | alive list, 8 slots; `0xFF` = slot removed | `C1:B2C3` (last delist), `C1:B291` |
| `$7E:A0FE` | battle-ENDING flag; 2 real `INC` sites in the image | `C1:3574` (victory), `C1:3551` (defeat) |
| `$7E:5DF1+2k` | seat k's pose window; `0x28` = VICTORY entry | `C1:35CD` (i.e. `STA $5DF1,Y` @ `C1/35CA`) |
| `$7E:5DC9` | the 64-frame celebration counter (**reused as battle scratch elsewhere** — see TRAP 6) | `C1:3686` (arm), `C1:3693` (dec) |
| `$7E:020C` | current results MESSAGE ID (0..7) | `CD:0261` (`STA $020C` @ `CD/025E`) |
| `$7E:CCEC` | **the message WINDOW byte**: `0x10` while a box is up, `0x00` when not | `CD:0291` (open), `CD:0200`/`CD:0203` (close) |
| `$7E:CA24` | tilemap-upload request the NMI consumes | `CD:0294` (set), `D1:FE70` (consume) |
| `$7E:0053` | the loaded dwell length for the current line | `CD:01E0` |
| `$7E:2991` | config byte; bits 3-5 = **Message Speed** | (config menu, `C2/9327` / `C2/939B`) |
| `$7E:00F8` | HELD-button byte; **bit7 = A** | `C2:85AB` |
| `$7E:0200` | the message ARGUMENT (XP / TP / gold / item id) | `FD:AD2C` / `FD:AD49` / `FD:AD61` |
| `$7E:B2AF` | pending-reward bitmask: b7 XP, b4 TP, b6 Gold, b5 items | `C1:F4AA` |
| `$7E:0117` | field-side module ack; `0x0001` = battle module done | `C0:18C1`, `C0:3AB1` |
| `$7E:0126` / `$7E:0134` | the FIELD textbox window / wait state | **never written during a victory** |

---

## PART 1 — THE ROM CHAIN (ROM_DERIVED; every listing re-run for this dossier)

### 1.1 Victory scan -> dispatch (`C1/8167`)
`python tools/dis.py 18167 0x30 1 0`:
```
18167: TDC / 18168: TAX / 18169: LDA $AF02,X / 1816C: CMP #FF / 1816E: BNE ->1818E
18170: INX / 18171: CPX #0008 / 18174: BCC ->18169
18176: JSR C1/B3BB / 1817A: STA $7F01EC / 1817E: LDA #FF / 18180: STA $A5A9
18183: JSR C1/EA9D / 18186: LDA #08 / 18188: JSR C1/0003 / 1818B: JMP C1/83BB
```
`C1/0003` -> `JMP C1/0045` -> `PHA/PHX/PHY/ASL A/TAX/JSR ($0051,X)`. Table at `C1/0051` (raw bytes
`23 00 19 1B AA 1B 6E 10 58 40 86 29 6D 00 DD 1F 6D 35 4D 35`) -> **entry [8] = `$356D`** (victory),
entry [9] = `$354D` (defeat).

### 1.2 The stager `C1/356D` — the ONLY blocking hold: 3 + 64 frames
`python tools/dis.py 1356D 0x140 1 0` (full span, not abridged):
```
1356D: JSL CD0009 / 13571: INC $A0FE
13574: LDX #0007 / 13577: STZ $98A7,X / 1357A: DEX / 1357B: BPL ->13577
1357D: LDX #0002 / 13580: STX $94 / 13582: JSR C1/30B6 / 13585: DEC $94 / 13587: BPL ->13582
13589: STZ $A1
1358B: JSR C1/1BAA / 1358E: JSR C1/007E                    <- frame 1
13591: INC $A1 / 13593: JSR C1/1BAA / 13596: JSR C1/007E   <- frame 2
13599: INC $A1 / 1359B: JSR C1/1BAA / 1359E: JSR C1/007E   <- frame 3
135A1: LDA #FF / 135A3: STA $95D5 / 135A6: JSR C1/0C2D
135A9: LDA $2989 / 135AC: AND #01 / 135AE: BEQ ->135B1 / 135B0: RTS   <- GATE, see Open Q1
135B1: LDX #0002 / 135B4: LDY #0004
135B7: LDA $A09B,X / 135BA: BEQ ->135C8
135BC: LDA #00 / 135BE: STA $5DF1,Y ...                     (dead seat: window 0)
135C8: LDA #28 / 135CA: STA $5DF1,Y / 135CD: LDA #00        (alive seat: VICTORY window +0x28)
...
13681: LDA #40 / 13683: STA $5DC9
13686: JSR C1/007E / 13689: JSL CD0009 / 1368D: JSR C1/369A / 13690: DEC $5DC9 / 13693: BNE ->13686
13695: STZ $E5 / 13697: STZ $A4 / 13699: RTS
```
`C1/007E` = ONE real frame: `python tools/dis.py 1007E 0x10 1 0` -> `INC $9E / JSL CD0036 / LDA $9E /
BNE ->10080 / RTS`. **There is no input test anywhere in `C1/356D`** — a byte scan of the whole
`C1/356D..C1/3699` span for direct-page reads of `$F0/$F2/$F4/$F6/$F8/$FA` returns zero hits, and a scan
for `20 7E 00` (`JSR C1/007E`) returns exactly FOUR sites: `C1/358E`, `C1/3596`, `C1/359E`, `C1/3686`.
So the 3 + 64 = **67 frames are not skippable**. Oracle confirms: with A held continuously, `$5DC9` still
counts `40 -> 00` one per frame.

### 1.3 Reward application + the message sequence (`C1/83BB` -> `FD/AD17`)
`python tools/dis.py 183BB 0x120 1 0`:
```
183D2: LDA $2989 / 183D5: BIT #20 / 183D7: BNE ->18423       (rewards-free variant; see Open Q1)
183E4: LDY $B2A5 / 183E7: JSR C1/D0A2                        (gold)
183EA: LDA $B2AF / ORA #40 / STA $B2AF / 183F2: JSR C1/F93E  (per-PC XP/TP)
183F5: JSL FDAD17                                            <- THE MESSAGE SEQUENCE
183F9: JSR C1/EAE8 / 183FC: JSR C1/B442 / 183FF: JSR C1/B4E9 (block->sheet writeback)
18406..1841F: item loop over $B2A7,X -> JSR C1/D005
18443: LDA $26 / 18445: STA $29AD / 1845A: JSL FDAAB0 / 1845E: JMP C1/0006  (module exit)
```
`FD/AD17` (`python tools/dis.py 3DAD17 0x160 1 0`) is straight-line, up to 8 message ids, each issued
with `JSL CD/0021`, gated on the `$B2AF` mask **and** on the value being non-zero:
- b7 -> id 0, `3DAD21 LDX $B28C / 3DAD24 BEQ ->3DAD32` then `STX $0200` (XP)
- b4 -> id 1, `$B2DB` / `$B2DD` (TP)
- b6 -> id 2, `3DAD56 LDX $B2A5 / 3DAD59 BEQ ->3DAD67` (Gold)
- b5 -> id 3, per entry of `$B2A7[0..5]` into `$7F0200` (drops)
- then per PC seat: id 4 (`$B2B0,X` b7, level up, repeated `$B311,X` times), id 5 (`$B2B3,X` b3, learned
  tech), id 6 (`$B2B3,X` b2), id 7 (`$B2B3,X` b1).

**A verifier caught an omission here.** The original derivation listed only the `$B2AF` bit gate. The
value-is-zero branches mean the LINE COUNT is data-driven — a fight with no gold renders fewer lines.
**The per-line cost is the invariant; the total is not.** Cross-checked on the oracle: forest ends with
`$B2AF = 0xF0` (4 lines rendered) and GATO with `0xD0` (2 lines rendered), yet the 74-frame tail
(3 + 64 + 7) is identical in both.

`JSL CD/0021` -> `JMP CD/01B3`. A whole-image signature scan for `22 21 00 CD` finds exactly 12 sites:
`C1/8203, C1/B386, C1/BC30, C1/BC46, FD/AD2E, FD/AD4B, FD/AD63, FD/AD7C, FD/ADAE, FD/ADF4, FD/AE20,
FD/AE3F`. `22 1E 00 CD` -> **zero hits**.

### 1.4 The message machine `CD/01B3` — 16 + DWELL + 16, per line
`python tools/dis.py 0D019D 0x80 1 0`:
```
D01A5: LDA $2991 / D01A8-AA: LSR A x3 / D01AB: AND #07 / D01AD: TAX / D01AE: LDA $CD019D,X / D01B2: RTS
D01B3: PHA/PHA/PHA
D01B6: LDX #EF11 / D01B9: STX $020D / D01BC: LDA #CE / D01BE: STA $020F   (the CE/EF11 string ptr table)
D01C1: PLA / D01C2: TAX / D01C3: LDA $D1EE99,X / D01C7: CMP #FF / D01C9: BEQ ->D01D1
D01CB: LDX #0080 / D01CE: JSR CD/3B71                       (per-message SFX; see 1.6)
D01D1: PLA / D01D2: JSR CD/025E / D01D5: JSR CD/027F        (build + OPEN the window)
D01D8: JSR CD/020A                                          <- 16 UNCONDITIONAL frames, box UP
D01DB: JSR CD/01A5 / D01DE: STA $53                         <- load the dwell
D01E0: PLA / D01E1: CMP #08 / D01E3: BNE ->D01EC
D01E5: LDA #3C / D01E7: JSR CD/3E3B / D01EA: BRA ->D01FD     (id 8 = FIXED 60f, no input test)
D01EC: LDA $53
D01EE: PHA / D01EF: JSR CD/3E75 / D01F2: LDA $F8 / D01F4: BPL ->D01F9   <- DWELL; A ($F8 b7) exits
D01F6: PLA / D01F7: BRA ->D01FD
D01F9: PLA / D01FA: DEC A / D01FB: BNE ->D01EE
D01FD: STZ $CCEC / D0200: STZ $CCED / D0203: INC $CA24       <- CLOSE the window
D0206: JSR CD/020A                                          <- 16 more frames, box DOWN
D0209: RTL
D020A: LDA #10 / D020C: PHA / D020D: JSR CD/3E75 / D0210: PLA / D0211: DEC A / D0212: BNE ->D020C / RTS
```
`CD/3E75` = one real frame: `python tools/dis.py 0D3E75 0x40 1 0` -> `JSR CD/3E7D / JSL C10012 / RTS`,
`CD/3E7D` = `INC $CA20 / JSR CD/3E44 / LDA $CA20 / BNE` (spin until the NMI clears it). So `CD/020A`'s
`LDA #10` is genuinely **16 FRAMES**, not 16 cheap iterations.

### 1.5 The dwell table
`CD/019D` raw bytes = `20 40 60 80 A0 C0 E0 FF` = 32 / 64 / 96 / 128 / 160 / 192 / 224 / 255 frames.
Index = `($2991 >> 3) & 7`. On the normative save `$2991 = 0xA0` -> index 4 -> **0xA0 = 160**.
Independently confirmed with `--callrom CD/01A5 --m8 --x16` -> `ret ops=8 a=00A0 x=0004`. `C2/9327`
unpacks the same field into `$0D90` (`LDA $2991 / LSR x3 / AND #07 / STA $0D90`), which is what makes it
the config menu's **Message Speed** slider.
Independent decode check that does NOT trust any dossier: `python tools/dis.py 0D019D 0x10 1 0`
disassembles the TABLE as code — `JSR CD/6040` (`20 40 60`) / `BRA ->D0142` (`80 A0`) /
`CPY #FFE0` (`C0 E0 FF`) — i.e. exactly those eight bytes.

### 1.6 The four reward lines are SILENT
Per-message SFX table at `D1/EE99` (read by `CD/01C3`, skipped on `#FF`): raw bytes
`FF FF FF FF 89 88 88 88`. Ids 0..3 (XP/TP/Gold/item) post **no** sound; ids 4..7 (level up / learned)
post `0x89` / `0x88` via `LDX #0080 / JSR CD/3B71`.

### 1.7 It is NOT the field textbox machine
`CD/025E` sets `$020C` = message id and `$0210/$0212 = $7E:B400` (a battle-local tile buffer) then
`JSL C2/0003`; `CD/027F` sets `$0213` then `JSL C2/0009`. Only the **bank-C2 string decoder / VWF glyph
renderer** is shared with the field. `--monitor "0126:2,0134:2"` across the whole victory shows only the
`f0` baseline row and **zero subsequent writes** to either.
This CORRECTS `battle_results_window.md` §V1's *"it is the standard CT dialogue window (the field textbox
engine)"* — right about the glyph renderer, **wrong about the machine**.
The string pointer table `CE/EF11` is loaded literally at `CD/01B6`; raw bytes at `0x0EEF11` =
`2D EF 44 EF 56 EF 5E EF 68 EF 74 EF 80 EF 93 EF A9 EF ...` -> id 0 = `CE/EF2D`, 1 = `CE/EF44`,
2 = `CE/EF56`, 3 = `CE/EF5E`, 4 = `CE/EF68`, 5 = `CE/EF74`, 6 = `CE/EF80`, 7 = `CE/EF93`, 8 = `CE/EFA9`.

---

## PART 2 — THE FRAME TIMELINE (one clock; reproduced fresh for this dossier)

**Repro — the command that settles the anchor argument (everything on the `--monitor` clock):**
```
python -c "print(','.join(['100:8,0:12']*40)+',0:2400')" > sc1.txt
ct_probe.exe --monitor "AF02:8,5DF1:1,CCEC:1,020C:1,0117:2" \
  --from saves/battle_re/forest_battle_3hetake.state --input "$(cat sc1.txt)" \
  --frames 2000 --diff --writer
```
Output, filtered to the victory-phase writers:
```
f729   00AF02=FFFFFFFFFFFFFFFF*(C1:B2C3)                    <- last enemy DELISTED
f777   005DF1=28*(C1:35CD)  005DC9=40*(C1:3686)             <- VICTORY DISPATCH (delist+48)
f778..f841  005DC9=3F..00*(C1:3693)                         <- the 64-frame hold, one per frame
f841   00020C=00*(CD:0261)                                  <- msg 0 staged (dispatch+64)
f846   00CCEC=10*(CD:0291)                                  <- window OPEN  (dispatch+69)
                                                            <- PIXELS at +2 = dispatch+71 = delist+119
f1022  00CCEC=00*(CD:0200)                                  <- line 0 closes (open+176)
f1038  00020C=01*(CD:0261)                                  <- msg 1 staged (+16 = the close ramp)
f1042  00CCEC=10*(CD:0291)                                  <- line 1 opens  (20 DARK frames)
f1218  00CCEC=00*(CD:0200)   f1234 00020C=02   f1237 00CCEC=10   (period 195, dark 19)
f1413  00CCEC=00*(CD:0200)   f1429 00020C=03   f1432 00CCEC=10   (period 195, dark 19)
f1608  00CCEC=00*(CD:0200)                                  <- last line closes
f1715  000117=0001*(C0:18C1)                                <- field-ack (+107 = 16 ramp + ~90 teardown)
```
**NO BUTTON IS PRESSED AFTER f800.** The lines advance by themselves. This is the direct refutation of
`battle_results_window.md` §V2 (*"each line holds indefinitely until A"*) — see TRAP 1.

Render side, independently (`--peek` + `python tools/ct.py fb2png`, read back as images): the box is
absent at win+118 (`chrome=none ink=0`) and complete at win+119 (`chrome=32..79(n=16) ink=1884`, text
`Got 9 experience point(s)!`). **No open ramp, no typewriter** — a 1-frame pop. Sequence with no input:
`Got 9 experience point(s)!` -> `Got 3 tech point(s)!` -> `Found 48G!` -> `Got 1x Tonic!`.

### The decomposition — every summand separately measured
| Span | Frames | Owner |
|---|---|---|
| delist `C1:B2C3` -> victory scan (`$A0FE` `C1:3574`, `$A5A9` `C1:8183`) | 45 | enemy DISSOLVE drain (`battle_dissolve.md` §S2) |
| victory scan -> dispatch `C1:35CD` | 3 | `JSR C1/007E` x3 at `C1/358E` / `3596` / `359E` |
| dispatch -> `$5DC9 == 0` | **64** | `C1/3681 LDA #40`, loop `C1/3686-3693` |
| `$5DC9 == 0` -> `$CCEC = 0x10` | 5 | gold `C1:D0B6`, `FD/AD17` staging, `CD/025E` + `CD/027F` |
| `$CCEC = 0x10` -> pixels | 2 | `$CA24` NMI tilemap upload (`D1:FE70`) |
| **delist -> first glyph** | **119** | |
| **dispatch -> first glyph** | **71** | *the number the port's constant must reproduce* |

### Per-line cadence
**No input:** `176` frames visible (16 `CD/020A` + 160 dwell) + `19-20` frames **fully dark**
(16 `CD/020A` + ~4 rebuild) = a **195-196 frame period**.
**A HELD:** line 1 still opens at dispatch+71 (the 16f `CD/020A` pre-wait is not input-gated), the dwell
collapses to 1 frame, per-line period = **36-37 frames**; kill -> field resume = 365 frames vs 1004 with
no input.
Cross-run identity that falls out of the structure to the frame: no-input `f841 -> f1608` = 767 frames
for 4 lines; A-held, same fixture, `f841 -> f972` = 131 frames; difference = **636 = 4 x 159**, and
159 = 160 − 1 because the dwell loop runs `JSR CD/3E75` **before** `LDA $F8 / BPL`, so a held A still
pays one dwell frame per line. That identity is essentially impossible to hit by accident, and it
confirms both the 160-frame dwell and the 16+16 ramps independently of the disassembly.

**Negative control:** B held for the whole sequence reproduces the no-input timeline byte for byte
(`f841 / f1038 / f1234 / f1429 / f1715`) with `$F8 = 0x0A` (bit7 clear). **Only A cuts the dwell.**
Button map measured on `$F0..$FA`: `$F0/$F4/$F6/$FA` are 1-frame EDGE copies, `$F2/$F8` the HELD copies;
in that layout `0x80 = A`, `0x40 = X`, `0x20 = L`, `0x10 = R`, `0x08 = B`, `0x04 = Select`, `0x02 = Y`,
`0x01 = Start`.

### Fight-invariance (the discriminating measurement)
GATO (`gato_wait.state`, mashed to the win; **`gato_lowhp.state` does not exist in this tree** — the two
dossiers that produced the 200/212 constants were captured from it and cannot be re-run as written):
win `f2107` (`C1:B2C3`), victory scan `f2109` (**win+2** — GATO does not dissolve,
`battle_death_dissolve_gate.md`), `$5DC9 = 0x40` at `f2112`, `= 0` at `f2176`, box chrome + partial
glyphs at win+75, complete `Got 10 experience point(s)!` at win+76. `2 + 3 + 64 + 7 = 76`.
**The 74-frame tail (3 + 64 + 7) is bit-identical across the two fights; only the dissolve prefix moves
(45 vs 2).** So the total is a function of the dead enemy's dissolve, **not** of the reward-line count —
which is exactly why the port constant must be anchored at the DISPATCH, not at the delist.

---

## PART 3 — WHAT OUR ENGINE DOES (three defects)

### D1 — the constant is ~3x too long
`src/battle.h:92`:
```c
#define CT_BATTLE_VICTORY_CELEBRATE 212   // pacing_v2 FIX E: the oracle VICTORY class = 231f vs our old 219f
```
Only two sites compare against it: `src/battle.c:6783` (A-edge acceptance) and `src/field.c:3794` (the
box draw gate). `h->victoryHold = 1` at `src/battle.c:6979`, incremented once per REAL frame at
`src/battle.c:6678` (above both the `$A013` freeze gate and the logic-tick gate), so the box first draws
at `victoryFrame + (C - 1)` = **victory + 211**. Measured on the forest fixture: victory `f935`, box
`f1146`, with the screen **bit-identical from f997 to f1148**
(`CT_BATTLESHOT_AT` fnv sweep: `f997 fnv=C9BA4E1B`, `f1050`, `f1100`, `f1145`, `f1146`, `f1148` all
`C9BA4E1B`; with `CT_BATTLESHOT_HUD=1` all `F865E998`) — **149 frames / 2.48 s of frozen nothing**.

**`h->victoryHold = 1` is the port twin of the ROM's `C1/35CA STA $5DF1,Y = 0x28`**: the very next line,
`src/battle.c:6982`, is `CtBattlePcLadderArm(h, 0x28, 0);`, the same window arm. So `victoryHold == 1`
== ROM `f777` (the dispatch), and the box must render at `victoryHold == 72`.

### D2 — the box never closes between lines
`src/field.c:3816-3818` forces the window permanently open and only swaps the text:
```c
int sb = tbAnimBand, sc = tbCream, s0 = tbClipY0, s1 = tbClipY1;
tbAnimBand = 1 << 20; tbCream = 0; tbClipY0 = -1; tbClipY1 = 1 << 30;   // full window, grey scheme
drawCtWindow(fb, TB_TOPY, 40);
```
The ROM closes it (`CD/01FD STZ $CCEC`) for 19-20 frames between every pair of lines: measured
`f1022 -> f1042`, `f1218 -> f1237`, `f1413 -> f1432` above, and confirmed on the render (a `--peek`
sweep across the gap shows `rule=[] ink=0` for the whole interval, and an `f1050` PNG is battlefield +
HUD only, no window).
Note the port's instant DRAW is **correct** (the ROM pops the box fully formed in one frame, no slide, no
typewriter) — what is missing is the CLOSE.

### D3 — there is no auto-advance at all
`src/battle.c:6783` advances a line ONLY on an A edge:
```c
if (h->handoffLeft == 0 && h->victoryHold >= CT_BATTLE_VICTORY_CELEBRATE && tA) {
```
The ROM auto-advances after `CD/019D[($2991>>3)&7]` frames. Without input our results box hangs forever;
in a real interactive game the whole victory never ends.

---

## PART 4 — TRAPS (the record of how the wrong numbers were caught)

**TRAP 1 — a sampling artifact produced "holds indefinitely until A".**
`battle_results_window.md` §V2 asserts each line holds until A. Its evidence was an idle `--peek` sweep
at `sw_260/360/500` (monitor ~f1160..f1400). Because each line cycles with a **195-196 frame period**, a
coarse sweep lands on *some* line every time and looks like a permanent box. Caught by watching `$020C`
and `$CCEC` with `--diff --writer` and **zero input**: `f841 $020C=00 -> f1038 =01 -> f1234 =02 ->
f1429 =03 -> f1715 $0117=0001`.

**TRAP 2 — `battle_loop.md` §K's "FIXED ~200-frame celebration" folded four phases into one bucket.**
§K measured `mash-A kill f950 -> f1164 = 214f` and subtracted only the ~11f handoff. The ROM's fixed,
non-skippable part is **67 frames** from the dispatch (112 from the alive-list clear). §K's remaining
~150f is (i) the message lines' own unskippable 16+16 ramps and (ii) ~90 frames of module teardown +
~18 of handoff. §K's "~302f skipped by A" on a 2-line fight is exactly `2 x ~157` of DWELL — the dwell,
not a celebration.

**TRAP 3 — `battle_pacing_v2.md` FIX E raised 200 -> 212 against the WRONG event.**
`git show c08f4cc -- src/battle.h` cites ledger row
`f1938->2169 (231) | VICTORY-HOLD -> field-ack $0117->0001 (C0:18C1)`. That is **kill -> FIELD RESUMED**:
the whole module, including the box phase and the ~90f teardown — not kill -> first glyph. Worse, the two
compared runs used **different A cadences** (oracle mash = A every 16f; our `CT_BATTLE_MASHA` emits an
A-edge every 5 real frames — the toggle at `src/battle.c:6749-6764` sits AFTER the 2.5-real-frame logic
tick gate), so a 12-frame difference that lived entirely in the box phase was charged to the pre-box hold.

**TRAP 4 — mixing the `--monitor` and `--peek` clocks costs exactly 1 frame.**
`peek N == monitor N-1` (monitor's `f0` row is the state AFTER the first driven frame). One derivation
compared a `--monitor` delist against a `--peek` box frame and reported 120 where the single-clock answer
is 119. The timeline in PART 2 is entirely on the monitor clock for this reason.

**TRAP 5 — anchoring the constant on the DELIST buries another mechanism's error inside it.**
One derivation proposed `CT_BATTLE_VICTORY_CELEBRATE = 79`, computed as "our delist -> box must equal the
ROM's delist -> box (119) given our 42-frame death freeze". But the ROM's freeze is **48** frames
(45 dissolve + 3 pose), so `79 = 72 + 6` (our dissolve-length deficit) `+ 1` (the clock mix of TRAP 4).
That hides a separate, already-OPEN dissolve-length defect inside the celebration constant, and misprices
every fight whose freeze is not 42f — GATO does not dissolve at all. **The mandate-safe number is 72**,
anchored at the dispatch, with the dissolve length fixed separately as its own mechanism.

**TRAP 6 — `$5DC9` is NOT a dedicated celebration counter.** The monitor shows it reused as battle
scratch throughout the fight (writers `C1:4474`, `C1:4CE5`, `CC:F071` at f27..f728 in the run above).
`C1/3683` arms it fresh, so the 64-frame reading is unaffected — but a probe that watches `$5DC9` alone
sees noise for the whole fight.

**TRAP 7 — `$CCEC` is used by the ENEMY-NAME box too.** In the run above `$CCEC = 0x10` with writer
`CD:0307` at f5 and f565 (the "Hetake" name box), cleared by `D1:E806`. **Only the `CD:0291` / `CD:0200`
writer pair belongs to the results box.** The enemy-name box that is still on screen at the win frame
closes at win+33 (its own `$CCEB` countdown, writer `D1:E7F4`), so the screen is genuinely BOX-FREE for
86 consecutive frames (win+33 .. win+118). A naive `$CCEC` sweep reads that first box as the results box
and mis-measures the onset.

**TRAP 8 — a raw `$A0FE` operand scan yields 9 hits, only 3 real.**
`FE A0` after any absolute opcode matches at `C1/3551 INC`, `C1/3571 INC`, `C7/8FC3 INC`, `CF/EDC5 LDA`,
`D3/1EE7 INC,X`, `E1/105B JSR`, `E7/805C TRB`, `EB/FD48 DEC,X`, `FD/4B10 JSR`. Disassembling the extras
(`python tools/dis.py 078FB0 / 3D4B00 / 27804C`) shows each sits in a data/compressed region and decodes
as garbage. Real: **two `INC` sites (`C1/3551` defeat, `C1/3571` victory) and one reader (`CF/EDC5`,
where with `$A013` it gates the OBJ/anim update away from `CF/EE2C`)**. State it as "two real INC sites
plus data-region false positives", never "exactly two INC sites in the image".

**TRAP 9 — a `--monitor` region that is written with the SAME value prints no `*`.**
`--diff` only prints on a value change. Use the `(bank:pc)` writer annotation (present only on frames
where the region was actually written) to detect a repeated identical post.

---

## Open questions

1. **`$2989` bit0 and bit5 were never exercised.** `C1/35A9-35B0` (`LDA $2989 / AND #01 / BEQ ->135B1 /
   RTS`) makes the victory stager **early-RTS**, skipping the pose setup AND the entire 64-frame hold; a
   fight with bit0 set would show the box ~64 frames sooner. `C1/83D5 BIT #20` selects a rewards-free
   variant at `C1/8423`. Both fixtures read `$2989 = 0x02`. Report as *could not reproduce under
   forest_battle_3hetake / gato_wait*, never as "never happens". **Do not port either branch on a guess.**
2. **The 45-frame dissolve gate is MEASURED, not DERIVED.** `INC $0027` at `C1/8121` first fires 43-45
   frames after the alive-list clear, coinciding exactly with `battle_dissolve.md` §S2's 43-frame
   dissolve, and GATO's 2 frames is consistent with "the end-check runs when the dissolve is done" — but
   the actual wait instruction in the turn tail was NOT found. `battle_end_ui_teardown.md` §6 lists it as
   OPEN and it remains OPEN. This is why the celebration constant must NOT be anchored on the delist.
3. **The new-game default of `$2991`'s message-speed field is unpinned.** The in-game save reads `0xA0`
   (index 4 -> 160). A raw 400-frame boot reads `$2991 = 0x00` (still at the title, not a valid reading).
   If the new-game default is index 0 the dwell is **32** frames and the whole no-input victory is ~4x
   faster. **This materially changes what "correct" means for the auto-advance and must be pinned via
   `--newgame` before any dwell is hard-coded.** Port the dwell as a TABLE LOOKUP on `$2991`
   (`CD/019D[($2991>>3)&7]`), never as a literal 160.
4. **The visual meaning of `CD/020A`'s two 16-frame holds** (window slide-in ramp vs a tile-upload drain)
   was not characterized — only their unconditional 16-frame cost was measured. The render shows the box
   fully formed on its first visible frame, so it is NOT a slide; what those 16 frames are FOR is unknown.
5. **The ~90-frame module teardown** between the item-add (`C1:D03D`, `f1625`) and the field-ack
   (`C0:18C1`, `f1715`) — `JSL FD/AAB0` + `JMP C1/0006` -> `C1/001F` -> `JML CF/FBE5` — was not
   decomposed (`battle_end_ui_teardown.md` territory). If part of it is a genuine post-results hold, some
   of FIX E's missing 12f belongs THERE, not before the box.
6. **Level-up / tech-learn lines (ids 4..7) were never measured.** Neither capture crossed a level-up
   threshold. **HYPOTHESIS** (ROM-derived from the shared `CD/01B3` path, never observed): their per-line
   cost is the same 16 + dwell + 16. They additionally post SFX `0x89` / `0x88` (see 1.6), which the four
   reward lines do not. This matters if the 212 was originally tuned against a fight that levelled up.
7. **The 2-frame state->render lead is asymmetric and undecomposed.** The OPEN leads by 2 (`$CCEC = 0x10`
   at +117, pixels at +119, needing the `$CA24` NMI upload); the CLOSE leads by only 1 in the A-held run.
   Whether the 2 frames are the DMA alone plus one frame of PPU latency was not separated. GATO renders a
   partial string at +75 and completes at +76 (a split VRAM upload, **not** a typewriter — a typewriter
   would advance per character over many frames); forest does not split. **Do not port a per-line "2".**
8. **Message id 8's fixed 60-frame dwell** (`CD/01E5 LDA #3C / JSR CD/3E3B`, no input test at all) is
   disassembled but never reached in either fixture.
9. **`battle_results_window.md` needs two recorded corrections**: §V1's "the field textbox engine" (wrong
   about the machine, right about the glyph renderer — see 1.7) and §V2's "holds indefinitely until A"
   (refuted — see TRAP 1). §V4's "victory ~f1060, first message ~f1100 (~40f gap)" cannot be reconciled
   with any measurement here; its drive script was never stated. A 40-frame gap is impossible given the
   64-frame `$5DC9` hold alone.

---

## PART 5 — PORTED (this session)

`src/battle.h`
- `CT_BATTLE_VICTORY_CELEBRATE` **212 -> 72**. `victoryHold == 1` is the port twin of the ROM's victory
  DISPATCH (the next statement is `CtBattlePcLadderArm(h, 0x28, 0)` = `C1/35CA STA $5DF1,Y`), so the box
  first draws at `victoryHold == 72` = dispatch + 71 = the 64 + 5 + 2 of PART 2.
  **NOT 79.** Anchoring on the DELIST instead would fold our 6-frame dissolve-length deficit into the
  celebration constant and misprice GATO, who does not dissolve at all (TRAP 5). That deficit stays its
  own OPEN item.
- `CT_BATTLE_RESULT_RAMP 16` (`CD/020A: LDA #10`, and `CD/3E75` is one real frame), `CT_BATTLE_RESULT_BUILD 3`
  (MEASURED 4/3/3 at the three forest boundaries; the derived floor is the two `JSR CD/3E7D` inside
  `CD/025E`+`CD/027F` plus an un-decomposed VWF pass -- Open question 4), `ROM_MSG_DWELL_TBL 0x0D019D`.
- `CT_BIN_AH` = A HELD. The results dwell is the ONE battle input read from the level state, because
  `CD/01F2` is `LDA $F8 / BPL` (the HELD copy), not the `$00F6` edge.
- `CtBattleHost.resultPhase` / `.resultPhaseT` = `CD/01B3`'s per-line phase.

`src/battle.c`
- `battleResultsTick(h, aHeld)` -- `CD/01B3` as a phase counter, driven REAL-frame beside `victoryHold`
  (CD/3E75 spins on the NMI's `$CA20`, not on the `$993A` logic tick). Dwell = `CD/019D[($2991>>3)&7]`
  read from the ROM at run time; `aHeld` cuts it after exactly ONE frame, matching the ROM's
  `JSR CD/3E75` BEFORE `LDA $F8 / BPL`.
- The old `if (... && tA)` line advance is **deleted**: the ROM has no wait-for-A. `CT_BATTLE_MASHA`
  counts as A-held (it models a continuous masher -- the same drive as the oracle's own mash-A capture).

`src/field.c`
- `battleResultsRender` returns early on `resultPhase >= 2`, i.e. the window is CLOSED between lines
  (`CD/01FD STZ $CCEC`). Its one-pass, fully-formed draw is unchanged and correct.
- `CT_BATTLESHOT_RESULTS` composites the results box into the headless shot; `g_font` is the
  module-scoped font the callback needs. **Before this, no check in the suite graded the results box at
  all** -- which is how 212 stayed green.

### Measured, ours vs the ROM (forest, 4 lines)
| | ROM | ours (before) | ours (after) |
|---|---|---|---|
| dispatch -> first glyph | 71 | 211 | **71** |
| line visible | 16 + 160 | forever (until A) | **16 + 160** |
| gap between lines | 19-20 (box CLOSED) | 0 (box never closed) | **19** |
| advance | automatic | **only on an A edge** | **automatic** |
| A held | cuts the dwell to 1f | (n/a) | **cuts the dwell to 1f** |

No-input drive (`CT_BATTLE_SCRIPT` exhausted before the win): victory f1479 -> advances f1745 / f1940 /
f2135 -> dismiss f2327. **Period 195, exactly the oracle's.**

### Checks (all mutation-proven, 0 survivors)
`field-battle-results-box-onset` (+71 = a box), `-not-yet` (+70 = the box-free screen -- the pair pins the
onset to the frame in both directions), `-closes` (+90 = box-free again, inside line 0's post-close hold),
`-autoadvance` (event-anchored on `results-advance#3`, driven with NO input: reinstating the A-gate makes
the anchor never fire). Re-pinned for the shorter victory tail, damage/HP byte-identical everywhere:
`field-battleloop-gato/-forest`, `field-battlewait-gato`, `field-battle-lifecycle-sfx`,
`field-battle-crit-x2`, `field-battle-marle-type2`, `field-battle-forest-return`,
`field-battle-bat9-return`, `field-battle-ambush-generic-dissolve`, `field-battle-endteardown-mode6`.

### Open question 3 NARROWED (not closed)
`$2991` reads **0xA0 -> index 4 -> dwell 160** in EVERY fixture in the tree -- all `saves/**/*.raw.wram`
AND all `saves/**/*.lakewram` (those carry an 18-byte `HEADER\nRAM:131072:` prefix; read at +18 or the
byte looks like 0x00 and the dwell like 32 -- that off-by-header is its own trap). The engine never writes
`$2991`; `src/menu.c` only reads it (the config screen's Message Speed). The **new-game default** is still
unpinned, which is why the port does the table lookup and never a literal 160.

### Still OPEN after this change
- The ROM's **~90-frame module teardown** between the last line closing and the field ack (`C1/83F9` ->
  `JSL FD/AAB0` -> `C1/0006` -> `CF/FBE5`); ours goes straight to the 11-frame handoff. Some of FIX E's
  missing 12f probably lives THERE, after the box, not before it (Open question 5).
- Our dissolve is 41 render frames against the ROM's 43-45 (its own fixture pin, TRAP 5).
- `$2989` bit0 / bit5 and message id 8's fixed 60-frame dwell -- still unexercised by any fixture. Not ported,
  not guessed. (The level-up / tech-learn lines are now measured and ported: section 1.8.)

## 1.8 The seat lines and their sounds (ported 2026-09-26, user defect #15)

"the sounds for learning skills and levelling up do not play". MEASURED on the logical oracle:
`saves/battle_re/forest_battle_3hetake.state` with both PCs one step from a level and a tech
(`PEEK_POKE=5E3F=0A,5E58=05,5E59=00,5E5A=02,5E5B=00,5EBF=0A,5ED8=05,5ED9=00,5EDA=02,5EDB=00`, A mashed),
`--monitor 7E020C:1,7ECCEC:1,7E1E01:1` and `PEEK_WRITEWATCH=1E00:4`:

```
$020C  00 01 02 03 04 05 04 05 06          XP, TP, Gold, item, Crono level, Crono tech, Marle level, Marle tech, dual
$1E01   -  -  -  - 89 88 89 88 88          by CD/3B71 (called from CD/01CE), on the same frame as $020C
close (STZ $CCEC) -> $020C + sound = 16 frames (f938 -> f954, f977 -> f993); -> open ($CCEC = 10) 3 to 5 frames
```

Level only: `89 89`; tech only: `88 88 88` (Cyclone, Aura, the dual). FD/AD8C-AE4E issues the seat lines one seat
at a time: id 4 once per level gained (`DEC $B311,X / LDA $B311,X / BNE ->AD99`), then id 5 per tech in
`$B3CE[seat*8 ..]` (until FF, at most 8), then id 6 per entry of the shared `$B305[$B30F ..]` (`$B2B3,X` bit 2),
then id 7 with `$B310` (bit 1). The port (battle.c battleBuildResults / battleResultsTick) builds the same order,
a line per level, and posts `D1:EE99[id]` with pan $80 on the frame its phase 2 ends -- the build's first frame,
16 after the close. What it cannot show: ids 6/7. The dual and triple techs are learned after C1/F205's single techs: C1/F2DB builds
the pair masks `$B2D3..$B2D9` (80/40/C0 per seat pair -- NOT a turn-order sort, as battle_tech_learn.md PART 1
guessed), the `$B2D2` loop from C1/F25F walks them, and on this capture C1/F5DA writes `$B305 = 39` (the dual tech's
id), C1/F5DD `$B30F = 1` and C1/F5E8 sets Marle's `$B2B4` bit 2 (08 -> 0C). The port has none of that pass, so no
dual-tech learn, line or sound exists yet.
Check `field-battle-results-line-sfx` (3 mutations killed).

## 2026-10-19 -- the dwell takes a held A from the post + 19, every line (the rule's cartridge)

CD/01B3 per line: the post on frame 0 (`$020C` = the line, CD/01C3's sound), CD/027F's two waits (`$CCEC = 10` on frame 2), CD/020A's 16
(frames 3-18), the dwell from 19 (`LDA $F8 / BPL` after each frame's wait). Under the rule CD/025E's VWF build is NMI-on work and
costs no frame. MEASURED: the playthrough's leg 20 -- posts 3074 / 3135 / 8424 / 8459, boxes +2, line 0 of the second ambush closed
by an A held over 8443-8444 (post + 19), the next post 16 after the close; a GATO fight (Crono solo, A every 12 frames): post 2549,
box 2551, close 2569 on the A that rose there (post + 20). The port tested from post + 21 on the first line (its box at the stager's
return + 4) and post + 20 after (CT_BATTLE_RESULT_BUILD 3): it lost the A at post + 19 and the fight ended one A later. Now the
first box is the stager's return + 2 (hold - 2) and the build 2: leg 20's second ambush returns on the cartridge's frame (8744),
battle_module_tail_cmp's Black Hole on GATO is +0 on every quantity (was +3). battle-end-pc-stands moved to f2662: the port's
victory is 11 frames early in that drive (`$A5A9` f2471 against f2482) -- a separate gap the late dwell had hidden.
