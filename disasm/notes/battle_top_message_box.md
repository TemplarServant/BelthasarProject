# The battle's TOP MESSAGE BOX, party side (CD/0310, CD/02D8, CD/02CB) -- dossier 2026-09-29

Sources: ROM disassembly (`tools/dis.py`), `ct_topbox.exe` (copy of build/ct.exe) under
`CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1`, `--monitor --diff --writer`, `--cputrace`, `--peek` + fb2png.
Fixtures: `saves/battle_re/gato_wait.state` + the Lucca-solo PEEK_POKE (recipe), a derived state `f640.state`
(recipe to f640, scratchpad `tb/`), `f640i.state` (same + `2400=BD,2500=05` poked PRE-battle -- poking the item
bag on an in-battle state does nothing), `saves/battle_re/forest_battle_3hetake.state` (Crono+Marle vs 3 Hetake,
in battle at f0, `$2990 = C4`). Frame numbers: "abs" = from gato_wait, "rel" = from f640.state (abs - 640).

## 0. Corrections to the brief

* CD/0310 has **no DEC A**: `$020C` = the id as passed; tech 0x11 reads CC:3A09[0x11] = "Hit all enemies on a
  line" (CC:3C1E). CC:3A09[0] = "" (CC:3B0D). The index is the global tech id (01-74), 75 = "Can't run away",
  76-78 = "Single/Dual/Triple Tech".
* The Item submenu shows **no box** (section 3.4). `$A0DB` ("Skill/Item Info.") is read ONLY at C1/0E8D.
* The dwell byte is not "written twice": CD/04DF rewrites `$CCEB` on EVERY render step (f661..f686); the f684
  row is the reload after f683, the one frame with no step (section 4.1).
* The port's "18 frames" of `$CD05 = 10` for "Can't run away" is not the ROM's law on the logical oracle: it is
  one step per decoded character, 14 for that string (section 5.4).

## 1. The routines

CD jump table (CD/0000-0038): `CD/0021 -> 01B3` (results line, CE:EF11), `CD/0027 -> 0310`, `CD/002D -> 02D8`,
`CD/0030 -> 02CB`, `CD/0033 -> 0295` (enemy name box, battle_yakra.md sec. 4), `CD/0009 -> 04AA` (frame service).

```
CD/0310  LDX $CD1D / BEQ / RTL            16-bit X: $CD1D|$CD1E != 0 (an enemy name box, CD/0295) -> refused
CD/0316  CMP $CD06 / BEQ ->033F / STA $CD06    dedup latch (shared by ALL party posts)
CD/031E  CMP #FF / BEQ ->033F             FF only resets the latch
CD/0322  LDX #3A09 / STX $020D / LDA #CC / STA $020F
CD/032F  JSR CD/025E                      (below)
CD/0332  LDA #10 / STA $CCEC / INC $CA24 / LDA #01 / STA $CD05 / RTL
CD/02D8  LDX $CD1D / BEQ / RTL / PHA / LDA #0B / STA $53 / BRA 02E3      (CD/002D: message 0B)
CD/02CB  LDX $CD1D / BEQ / RTL / PHA / LDA #0C / STA $53                 (CD/0030: message 0C)
CD/02E3  PLA / CMP $CD06 / BEQ ->030F / STA $CD06 / CMP #FF / BEQ ->030F
CD/02F0  LDX #EF11 / STX $020D / LDA #CE / STA $020F / LDA $53 / JSR CD/025E
CD/0302  LDA #10 / STA $CCEC / INC $CA24 / LDA #01 / STA $CD05 / RTL
CD/025E  STA $020C / STZ $CCED / STZ $02A1 / $0210-12 := 7E:B400 / STZ $0214 / JSR CD/0239 (clears $B400-$B7FF)
         / JSL C2/0003 (= C2/57DF: $31-33 := [$020D-F + 2*id], $34 := 8 (since $0214 != 2), $15 := 4,
         $3D := $0200 the number cursor, $17 := 0)
```
Note: for CD/02D8/02CB the value in A is a **dedup key**, not the message: the message is fixed (0B / 0C).

The frame services (both already in the port as battleSysMsgFrame, with a wrong step law):
```
CD/04DF  LDA $CD05 / BEQ ->04FC / LDA #01 / STA $0213 / JSL C2/0009 / LDA $0215 / STA $CD05
         / JSR CD/01A5 / STA $CCEB / INC $CCED / RTS      -- the dwell reload + INC run on EVERY step
CD/01A5  $CCEB source = CD:019D[($2991 >> 3) & 7] = 20 40 60 80 A0 C0 E0 FF   (A0 on these fixtures, $2991=A0)
D1/E7EC  (NMI) LDA $CCED / BEQ / DEC $CCEB / BNE / LDA $CD1D / BEQ ->E800 / LDA #FF / STA $CE17
         / E800 STZ $CD1D / STZ $CCEC / STZ $CCED / INC $CA24
```
CD/04DF's callers: CD/04AA (= CD/0009; JSL'd at C1/10A0, C1/10C0, C1/1110, C1/354D, C1/356D, C1/3689) and
CD/3E7D (CD/3E89; every CD wait: CD/3E75, CD/3E3B loops, CD/020A). A plain `JSR C1/007E` wait (C1/007E-0088:
`INC $9E / JSL CD/0036 / LDA $9E / BNE`) does NOT step it -- e.g. C1/405F inside C1/4058 (action start).

C2/0009 = C2/5823 renders `$0213` characters per call and answers `$0215` = 10 while the string is not finished,
0 on the call that reads the terminator (menu2_tech_screen.md sec. 7: C2/FAA5 uses the same with `$0DC9`; the
menu's mShopDescTick documents one DECODED character per step, a dictionary code = one step per expanded char).
CD/027F (the enemy box) calls it once with `$0213 = 40`: whole string at once. **Every party post leaves the
rendering to CD/04DF with `$0213 = 01`: a typewriter, one character per step.**

The picture path (NEW -- not in the port): the NMI's CD/088D-08A9 (`LDA $CE0F / BEQ`, `LDA $CA1E / BNE` skip) ->
```
CD/0607  LDA $CA17 / BEQ ->064B ...  CD/064B: LDX #0200 / STX $45 / INC $CE14 / LDA $CCEC / CMP #10 / BEQ ->0694
CD/065A  (box closed) VRAM fill: $4370=09 (fixed source CD:06B2 = 00 00), $0200 bytes to $5800 if $CE14 odd,
         else to $5980                                     -- clears ONE half per NMI
CD/0694  (box open) LDA $CE14 / AND #01 / BEQ ->06A7 / X=B400 Y=5800 / JMP CD/3E8E  ; odd:  WRAM $B400-$B5FF
         06A7: X=B600 Y=5980 / JMP CD/3E8E                                           ; even: WRAM $B600-$B7FF
```
Buffer layout (oracle .wram after "Hit all enemies on a line"): 16 tile columns per $200 block, tile c = top 8
rows of column c, tile c+16 = bottom rows. $B400 = buffer columns 0-15 (screen x 0-127), $B600 = columns 16-31
(x 128-255). So **the left half of the line refreshes on NMIs that make $CE14 odd, the right half on even ones**.
`$CE14` is never reset by the battle boot (gato_wait f0: A8, unchanged through D1/F210-F29E; its only writer is
CD/0650), so its parity is whatever the byte carried in plus the NMIs that passed the CD/088D gate.

The band (window) install is gated too: the late IRQ D1/FE97 (`LDA $7C / AND #01 / BNE -> FD/FFFD`) runs
`JSR D1/FE4C` only when `$7C` is EVEN (`$7C` = the NMI counter, D1/E7E7-E7EA). Measured: post at rel f20 with
`$7C = B6` -> `$CA24` consumed the same frame; forest post f12 with `$7C = EF` -> consumed f13 (D1:FE70). The port's
battleHudBandFrame consumes `$CA24` every frame (no `$7C` gate) -- a shared defect for every box.

## 2. The callers and their conditions

### 2.1 Tech description: C1/0C2D -> C1/0E8D (the $993A rebuild)
CF/E73B-E745 (frame service): `INC $9931 / AND #03 / BNE / INC $993A`. The idle loop C1/109D-10E2: `JSR C1/007E /
JSL CD/0009 / ... / JSR C1/007E / JSL CD/0009 / INC $E5 / LDA $993A / BEQ / STZ $993A / JSR C1/1B67 / JSR C1/0C2D /
JSR C1/1153 / JSR C1/17DD`. The action loops C1/10E3 (no CD/0009) and C1/10FA (block, then C1/1110 `JSL CD/0009`).
C1/0C2D: `LDA $95DB / BNE`; `== 1` -> `JSR C1/0EE1 / JMP C1/0C83` -> C1/0D4D (the resolver: `$9EE3-9 := $1A80+X`,
X = CC:F395[2*seat] + CC:F3A1[$9551[CC:F38C[seat] + $95EB,seat + $95DF,seat]]; FF row -> `$9EE3-9 := FF`, C1/0D69)
-> C1/0E5A (hand draw only while `$9609 == 0`) -> **C1/0E8D `LDA $A0DB / BPL ->0E99 / LDA $9EE3 / JSL CD/0027`**.
`$95DB == 2` (item list) -> JMP C1/103D: nothing posted. `$A0DB = $2990 & 80` (CC/E3AC-E3B1, battle boot).
Consequences (all measured, section 4): posted on every rebuild while the tech list is up (also during the tech's
target select: `$95DB` stays 1 until C1/1719 at confirm), deduped by `$CD06`; an input handled by C1/1153 in one
rebuild is posted at the NEXT rebuild (C1/0C2D runs before C1/1153): +4 frames. An empty list posts FF (reset only).

### 2.2 Att: C1/127B-12B9 (command confirm)
`$95DC,seat == 0` (cursor on Att) -> C1/129C `LDA #FF / JSL CD/002D` (latch := FF, unless `$CD1D` set) -> target
select (`$960D = 07`, `INC $9609`). Tech (C1/12BC) and Item (C1/12ED) paths do NOT touch `$CD06`.

### 2.3 Target name: C1/17DD -> C1/1979 -> C1/19AC-1A47 (same rebuild, AFTER C1/1153)
C1/17E5 `$9609 != 0` -> C1/1979: `$9613` bit7 set -> none; C1/19AC single target only (`$A62E` bit7 set, `$A62D`
bit7 clear; else C1/1A7B multi / C1/1AFB). `$A64E := $A62D`; bracket to `$0700/01`; then
```
C1/19D7  LDA $95DB / BNE ->1A4B           only the Att command's picker (tech picker has $95DB = 1)
C1/19DC  LDA $A64E / CMP #03 / BCC ->1A4B enemies only (slots 3..10)
C1/19E3  $0200 := $984D[2*$A64E] (enemy id); $80 := CC:F8ED[2*$A64E] (its stat block offset)
C1/19F7  LDA $9F34 / BEQ ->1A05 ; LDA $9F29,Y (Y=$A64E) / BNE ->1A1C
C1/1A05  key := E1:DE80[$0200] ? FF : $A64E / JSL CD/0030      -> CE:EF11[0C] (name)
C1/1A1C  $0201-04 := $5E30-33,X (X=$80: HP cur, HP max) / key as above / JSL CD/002D -> CE:EF11[0B] (name+HP)
```
`$9F34` = count of seats whose `$5E57/$5ED7/$5F57` is A4 (SightScope) or B8 (Relic) (CC/F034-F055; STZ FD/B0F8).
`$9F29,Y` for Y = 3..10 is `$9F2C+k` = `($5FC7 + 80k & 02) ^ 02` (FD/B0FB-B10C; also CF/BA4C). E1:DE80 non-zero
for 95 enemy ids incl. GATO 0x92 -> GATO never gets a name box; Hetake 0x5E does. Item target select has
`$95DB = 0` (C1:1FB1) -> an item aimed at an enemy would post too (derived; not measured -- Tonic targets a PC).

### 2.4 "Can't run away": C1/106E-1099 (loop head) -- already ported (battleFleeConsumer)
`LDA #FF / JSL CD/0027 / LDA #75 / JSL CD/0027` once per L+R hold in a `$2989` bit7 fight.

## 3. Tables and text

* CC:3A09: u16 pointers, bank CC, index = `$020C`. Dictionary (DE:FA00) + characters, 00-terminated.
* CE:EF11[0B] = EF C3: `12 01 EF 0E E0 0E 00`; [0C] = EF CA: `12 01 00`.
* Code 12 (C2/5A4C), param < 8 -> C2/5A8A[param]; param 01 = C2/5AD0: id := [$3D] ($0200, cursor +1), text :=
  CC:6500 + 11*id (the port's ROM_ENEMY_NAMES), `$023A = 0B`: all 11 bytes are typed, the EF padding included.
  Code 0E (C2/5985): the 16-bit number at [$3D] ($0201-02, then $0203-04), its digits (fair_silver_points.md).
  Both are read when the typewriter REACHES the code, not at the post.
* Step counts (measured = decoded characters): tech 0x11 25, 0x75 14, CE:EF11[0C] 11, [0B] with 14/14:
  11+1+2+1+2 = 17. (Tech 0x12 "Put enemies to sleep", 20 characters, showed 19 unfinished steps before the next
  re-post -- consistent, not run to the end.)
* Geometry = the port's existing box: text origin x = 8 (`$34 = 8`), glyph rows at the band's first row + 6
  (screenshots: "H" at x 8, rows 22-29 of the 240-line png = renderY 14-21), window 24 lines via preset
  `$CA25 + $CA22 + $CCEC` (CE:EE6E), opposite the strip.

## 4. Measured timelines (logical oracle)

### 4.1 The recipe (abs)
| f | event (writer) |
|---|---|
| 640-641 | Down -> `$95DC = 01` f644 (C1:126B) |
| 652-653 | A -> `$95DB = 01` f656 (C1:12E4, rebuild) |
| 660 | rebuild: `$9EE3 = 11` (C1:0DB8) -> CD/0310: `$CD06 = 11`, `$020C-F = 11 09 3A CC` (CD:0261), `$CCEC = 10` (CD:0337), `$CD05 = 01` (CD:033F), `$CA24` consumed same frame (D1:FE70) |
| 661-686 | CD/04DF steps: `$CD05 = 10`, `$CCEB = A0`, `$CCED = 01..19` (CD:04F3/04FC); **f683 no step** (C1/4058's C1/007E wait): `$CCEB = 9F` (D1:E7F4), reload A0 at f684 |
| 664-665 / 676-677 | A (Flame Toss) -> `$9609 = 01` f668 (C1:138C); A (target) -> f680 `$95DB = 0` (C1:1719), `$9609 = 0` (C1:1629) -- no post, no close |
| 684 | `$A013 = 01` (C1:40A9): the action; typing continues through CD/3E7D waits |
| 687 | terminator step: `$CD05 = 00`, `$CCEB = A0`, `$CCED = 1A` |
| 688-846 | `$CCEB` 9F..01 (D1:E7F4) |
| 847 | close: `$CCEB/$CCEC/$CCED = 0` (D1:E806/E809), `$CA24 = 1` (D1:E80C), consumed f848 (D1:FE70); `$CE17` untouched |
| 1337 | Lucca's next turn (`$A6D9 = 00`, C1:1B83); Tech list at f1373 on Flame Toss: **no post** (`$CD06` still 11) |

### 4.2 Cursor moves, cancel, reopen (rel, from f640.state; input D,A, then D/U/R/L/B/A/A/A every 20)
Posts at f20 (11), f48 (12), f68 (11), f88 (12), f108 (11) -- each 4 frames after the cursor change (`$95DF` by
C1:1436 f44/f84, C1:13EF f64/f104), each a full restart (`$CCED := 0`, buffer cleared, `$CD05 := 1`).
B at f120 -> `$95DB = 0` f124 (C1:1395): the box keeps typing (terminator f134) and dwells. A at f140 reopens the
list f144: no post (dedup). A f160 -> picker f164; A f180 commits; box closes f294 = f134 + 160.
Next turn (rel): A on Att f809 -> `$CD06 = FF` (CD:02EC) [GATO: no name box]; B, Tech, A -> list f867, **post
f871** (id 11 again, because the Att confirm reset the latch).

### 4.3 Item submenu (rel, f640i.state)
`$95DB = 02` f28 (C1:131D), Tonic -> picker f68 (`$95DB = 0` C1:1FB1, `$9609 = 1` C1:14D8), commit f92: `$CCEC`,
`$CD05`, `$020C-F` never change. Picture: "Tonic : 5 Item", no top box.

### 4.4 Att target select
GATO (rel): A f0 -> `$9609 = 01` f4 (C1:12B9), `$0200 = 92`, `$0201-04 = 4C00 4C00`, `$A64E = 06`, `$9F34 = 01`,
no post (E1:DE80[92] = 1 -> key FF = `$CD06`).
Forest (Hetake 0x5E, `$9F34 = 0`): A f10 -> f12 post CD/0030 key 03: `$020C-F = 0C 11 EF CE` (CD:0261), `$CD05 = 01`
(CD:030F), `$CCEC = 10` (CD:0307); steps f13-f23 (11 x `$10`), terminator f24. Down f52 (while an action runs,
`$A013 = 1` since f40, active mode) -> `$A64E = 04` and post + step 1 in the SAME frame f54 (the action loop runs the rebuild
before its CD/0009); terminator f65. Same with `PEEK_POKE 9F34=01`: message 0B, steps f13-f29, terminator f30;
B f52 -> `$9609 = 0` f54: box stays.

### 4.5 Can't run away (rel, L+R held from f0)
Post f2 (C1/1093-1099, loop head), steps f3-f16 (14 x `$10`), terminator f17, dwell from f18.

## 5. The picture

### 5.1 Frame order inside one monitor frame (derived from the measurements below, consistent in all four)
NMI_N (`INC $7C`, D1/E7EC dwell, CD/0607 upload of the half picked by the new `$CE14`) -> main loop N (posts,
CD/04DF steps) -> late IRQ_N (band install if `$7C` even). The png of `--peek N` shows VRAM as of NMI_(N-1)
and the band installed by IRQ_(N-2).

### 5.2 Typing (rel, strip1.png = peek f18-f50; `$CE14` f20 = 08, +1 per frame)
f18-21 no window; f22-23 empty window (band from IRQ_20; NMI_21 uploaded the cleared left half); f24 "Hi"
(NMI_23, `$CE14` 0B, buffer of main 22 = 2 steps); f26 "Hit"; f28 "Hit al"; f30 "Hit all"; ... the left half
advances 2 characters every 2 frames; f45 "...on a li" / f47 "...on a line": the characters past x 128 appear on
the other parity. Full line from f47.

### 5.3 Re-post (rel, strip_rp.png; its labels r46..r56 are frames 48..58)
Post 0x12 at main 48. f50: left half blank, "ine" still on the right (NMI_49 odd uploaded the cleared buffer);
f51 both blank (NMI_50 even: buffer "P" has nothing right of x 128); f52 "Pu"; f54 "Put"; f56 "Put en".
The old text does not vanish with the post -- each half keeps its last upload until its own parity comes.

### 5.4 Name box (forest, strip_hp.png)
Post f12 (`$7C` odd) -> band at IRQ_13 -> window from f15 already showing "H" (NMI_14 odd, buffer of main 13);
f17 "Het", f19 "Hetak", f21 "Hetake", f27 "Hetake      1", f29 "... 14/", f31 "Hetake      14/14".
(The forest's scrambled BG is the s9x-splice VRAM artifact, not the box.)

Screenshots (scratchpad `tb/`): sheet1.png (f21/23/30/47/60), strip1.png, strip_rp.png, sheet2.png (item list
with no box; "Hetake"; "Hetake 14/14"), strip_hp.png, zoom.png.

## 6. IMPLEMENTATION SPEC (port)

WRAM, per post (all three party posts; the ROM's bytes, nothing host-side except the picture latches):
1. **CD/0310** = battleSysMsgPost (exists; make its guard 16-bit: `$CD1D | $CD1E`). **CD/02D8 / CD/02CB** = a new
   `battleTargetMsgPost(h, key, msg)`: same guard / latch / FF rule; `$020D-F := 11 EF CE`; CD/025E's bytes with
   `$020C := msg` (0B or 0C); `$CCEC := 10`; `$CA24++`; `$CD05 := 1`. Both must also clear `$B400-$B7FF` (CD/0239)
   and set `$0210-12 = 00 B4 7E` like battleNameBoxPost already does.
2. **Callers:**
   * In the port's `$993A` rebuild (battleBracketRebuild), in the ROM order C1/1B67, C1/0C2D, C1/1153, C1/17DD:
     C1/0C2D part: if `$95DB == 1`: run the resolver (C1/0D4D) and then `if ($A0DB & 80) post($9EE3)`.
     The resolver/post must NOT run every tick (battleTechSubmenu runs battleTechResolve each tick today) -- the
     post reads the `$9EE3` resolved in the rebuild, and the port's input for that rebuild comes after it.
   * C1/17DD part (after input): `$9609 != 0`, `$9613` bit7 clear, single target, `$95DB == 0`, `$A64E >= 3`:
     `$0200 := $984D[2*$A64E]`; if `$9F34 && $9F2C[$A64E-3]`: `$0201-04 := HP cur/max` of that enemy, msg 0B, else
     msg 0C; key = `E1:DE80[$0200] ? FF : $A64E`.
   * The Att confirm (C1/129C): `battleTargetMsgPost(h, 0xFF, 0x0B)` before opening the picker.
   * Boot: `$A0DB := $2990 & 80` (CC/E3AC); `$9F34` (CC/F034-F055: SightScope A4 / Relic B8 at `$5E57+80s`);
     `$9F2C+k := (($5FC7+80k) & 2) ^ 2` for live enemies (FD/B0FB). None of these exist in the port today.
3. **CD/04DF** (battleSysMsgFrame's second half): replace `sysMsgOpenT <= 18` by the step law. On each call with
   `$CD05 != 0`: `$0213 := 1`; typed++; `$CD05 := (typed <= len) ? 10 : 00` where len = decoded characters of
   [`$020D-F` + 2*`$020C`] (dictionary expanded, code 12 01 = 11, code 0E = digits, 00 ends); then ALWAYS
   `$CCEB := CD:019D[($2991>>3)&7]` and `$CCED++`. Call it only on frames where the ROM's CPU reaches CD/0009 or
   CD/3E7D; skip it on plain C1/007E waits (measured: C1/405F, the frame before `$A013` rises). The post-to-first-
   step gap follows from the loop: idle rebuild / loop-head posts step on the next frame, the action loop C1/10FA
   in the same frame. The pinned check field-battle-cantflee-box encodes the 18 (box-open f220 = post + 19) and
   must be re-graded against the logical oracle (14 steps + terminator).
4. **D1/E7EC** is right as ported. The enemy box's CD/027F keeps its one-call render (`$0213 = 40`).
5. **NMI upload, new** (CD/088D gate `$CE0F == 0 && $CA1E == 0`): `$CE14++`; half = odd ? LEFT (x < 128) :
   RIGHT (x >= 128); if `$CCEC == 10`: latch[half] := {table `$020D-F`, id `$020C`, `$0200-04`, typed}; else
   latch[half] := empty. `$CE14` stays in WRAM and is never reset at the boot.
6. **IRQ band gate**: install the `$CA24` request (D1/FE4C) only on frames whose `$7C` (after the NMI's INC) is
   even; odd frames run FD/E022 instead (memory: ct-field-anim-under-battle). Applies to every box and the strip.
7. **Draw** (field.c battleResultsRender): generalise the `CC:3A09` branch to decode any `$020C` (fieldCtDecodeAt
   over CC:3A09; drop the literal "Can't run away"); add a `CE:EF11` 0B/0C branch (name from ROM_ENEMY_NAMES
   + 11*`$0200`, 11 glyphs with the EF padding as 4-px spaces, then for 0B " " + digits of `$0201-02` "/" digits
   of `$0203-04`). Render the line once per latch: pixels x < 128 from the LEFT latch's prefix, x >= 128 from the
   RIGHT latch's (a glyph crossing 128 is split). The enemy name box and the results box go through the same
   latches (their whole text lands in one half one frame before the other).

## 6b. PORT STATUS (2026-09-30)

Ported (src/battle.c, src/field.c): items 1, 2, 3 (the step law; it steps on every frame, the C1/007E skip is not
modelled), 4 and 7 of section 6; items 5 (the NMI's half latches by `$CE14` parity) and 6 (the `$7C` band gate) are
not. What the port reads now, against the rule's oracle:

* **Tech description** (check `field-battle-topbox-tech-description`, Lucca solo from gato_wait, Down f640, A f652):
  oracle open f656 / post f660 / 25 steps f661-f685 / terminator f686 / close f846; port open f653 / post f655 /
  steps f656-f680 / terminator f681 / close f841. The box's machine is the cartridge's step for step; the run is
  5 frames early because the port's menu input is not on the 4-frame `$993A` pass (C1/10C6: the cartridge acts on
  the A at the pass after it, and posts at the next pass) -- the menu-cadence boundary, not a box defect.
* **"Can't run away"** (check `field-battle-cantflee-box`, re-graded): oracle post f607 / 14 steps / terminator
  f622 / close f782; port post f201 / terminator f216 / close f376 -- the same +15 / +160.
* **The HUD strip under an open box.** With `$CCEC = 10` the presets carry TWO `$BE9B` entries (the window uses the
  strip's BG3 register block): oracle f660 `08 85 BC | 18 9B BE | 30 85 BC | 50 85 BC | 30 9B BE | 04 85 BC`. The
  port's strip reader took the first, the window's band, and drew no strip while a description was up; it now takes
  the entry inside the strip's own rows (field.c ctBattleHudBand).
* The Att picker's name box (0B/0C) and the Att confirm's FF reset are wired; the boot bytes `$A0DB`, `$9F34`,
  `$9F2C+k` are set.

## 7. Open questions (not derived)

* C2/0009's internal state machine (`$15`: 4 -> 10 -> 0) was not disassembled; the one-character-per-call law
  rests on the four measured strings + the menu's measurement, not on the code.
* C2/5985's formatting of `{num}` beyond "14" (padding for 3+ digits) -- not measured.
* A party post while an enemy box is up is refused (code); an enemy post (CD/0295) while `$CD05 != 0` leaves the
  typewriter armed over a string CD/027F already finished -- CD/04DF would then zero `$CD05` and arm the dwell
  early. Not measured.
* `$CD06` is shared: a target key 03-0A can equal a tech id 03-0A and suppress that tech's first description
  (code consequence, not measured).
* The CD/088D `$CE0F != 0` branch (JSL CD/09CE) -- whether it uploads the box -- not disassembled.
* Why battle_flee.md measured 18 frames of `$CD05 = 10` on gato_t0 (no .state exists to re-run it); on the
  logical oracle the same string steps 14 times on consecutive frames.
* The monitor/peek frame-order model of 5.1 is inferred from four independent observations (typing, re-post,
  forest open, dwell DEC vs reload), not from a single trace (the `--cputrace` frame count is 3 off the logical
  monitor's, so its parity cannot be compared).
