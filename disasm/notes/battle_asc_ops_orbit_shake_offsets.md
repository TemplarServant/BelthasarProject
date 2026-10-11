# Battle anim-script ops, group C0-C5 / 27-2B / D8 / 75 / 19 / 10 / 15 / 9C / 9D / 5E / 6B / 25 / 43 / 4A (2026-09-29)

Sources: `tools/dis.py` on every handler (m=1 x=0, the C1/4CD2 `SEP #20` state), a byte scan of banks C1/CC/CD/CF/D1/FD for
every reader, and oracle runs (a copy of build/ct.exe; `CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1`,
`saves/battle_re/gato_wait.state`, the recipe's PEEK_POKE, `--monitor ... --writer --diff`). Writer PCs are "the instruction
after the store". Frames are the oracle run's own (f0 = the state's load).

Runs (char, k -> tech, $9877): Crono k4 -> 05 (05), Crono k6 -> 07 (07), Crono k7 -> 08 (08), Marle k5 -> 0E (0F),
Robo k2 -> 1B (1C), Robo k4 -> 1D (1D), Frog k2 -> 23 (23), Frog k4 -> 25 (25), Ayla k0 -> 29 (29), Magus k6 -> 37 (37).

## 0. The frame model these ops live in (C1/4CA2)

`STZ $A00F / JSR ($7A6B,X) / LDA $A00F / BNE loop`: an op that does not `INC $A00F` ends the track's frame (its advance, park or
not, is the last thing this track does this frame). Order inside one oracle frame, measured on every run below: the movers
(CF/EFAF combatants, then CF/EFED effect objects) tick BEFORE the script visits; an arm made by the script is first ticked on
the NEXT frame; a done flag set by a tick is seen by the SAME frame's visit.

## 1. Summary table

| op | handler | len | park / free | writes | downstream reader -> visible effect | tech (oracle) |
|---|---|---|---|---|---|---|
| C0 tt a b c | C1/6CF3 | 5 | arm parks; revisit parks until done, then advance 5; costs | $A2B0/2 = resolve(tt); mode 3 orbit arm (sec. 2) | mover mode 3 (CF/F1F8 cmb, CF/F68D obj) | 05: t9 `C0 18 08 FF 01` arm f858, ticks f859-866, release f866, next op f867 |
| C2 tt a b c | C1/6F53 | 5 | same | same, mode 5 | mover mode 5 (CF/F354 cmb, CF/F7FC obj) | 07: t0 (Crono) `C2 2B 14 E0 01` arm f853, ticks f854-861, release f861 |
| C3 a b c | C1/6F75 | 4 | same | centre = $5D9E/$5D9F, mode 5 | mode 5 | only dual techs (45 51 54 55 58 59 66): not run |
| C4 xx yy | C1/6F9A | 3 | $A4AF != xx: += yy, advance 0; == xx: advance 3; always costs | $A4AF[c] | CF/EAF8 cells' Y offset (float/sink) | 08: `C4 FF FF` step f852, advance f853 ... F0 at f987 |
| C5 yy | C1/6FEF | 2 | as C4 with xx = 0 | $A4AF[c] | same (land) | 08: `C5 01` f1312-1327 (F1..00), advance f1328 |
| 27 | C1/5806 | 1 | costs | $A5CD[c] = ($A5CD & 80) \| 1 | CF/E855 always-front render entry | 05: f765 00->01 (C1:5824); 07: f835, f893 |
| 28 | C1/5840 | 1 | costs | $A5CD[c] &= 80 | front entry off | 05: f1110; 07: f883 |
| 29 | C1/5844 | 1 | costs | cmb: $9FF7[c] = 01 (whole byte); list: \|= 1 | CF/EAD7 / EA8F / E9CB: no cells, shadow, afterimage | 25: f902 (C1:585D); 37: f1274 |
| 2A | C1/5879 | 1 | costs | cmb: $9FF7[c] = 00; list: &= 80 | shown again | 25: f946 |
| 2B | C1/587D | 1 | costs | $A4A4[c] = $A4AF[c] = 0 | cells back on the foot | 07: f1009 (C1:5893) |
| D8 xx yy zz | C1/74B4 | 4 | never parks; costs | $AB1E=xx $AB34=yy $AB13=zz+1 $AB29=$AB3F=$A2FE=$A2EB=1 $A86D=6 $A337=1 | mover mode 6 (CF/F39F): the SHAKE via $A4A4/$A4AF | 07: t3 GATO `D8 04 02 23` arm f895, ticks f896.., `D8 06 02 23` f956-992 |
| 75 nn | C1/6093 | 2 | FREE | dest heading := src heading ($A5AA/$A5B5) | orbit start (b=FF), D8, A8/A9, 15 | 05: t9-15 `75 0A` -> $A5B5 = C0 E0 00 .. A0 at f796 |
| 19 xx yy | C1/5611 | 3 | costs | cmb $1D0C/$1D23 = xx/yy; obj $A240/$A250 = xx/yy (high 0) | teleport to a literal point | 1B: t9 `19 10 10` f737, t8 `19 01 01` f740 |
| 10 xx yy | C1/4FBF | 3 | as 11/12 (park, $A3A9 = 3) | $A2B0/2 = xx/yy, then op 12's arm C1/4FED | slide mode 0 | 1B: t8 `10 FF 01` arm f785, +8/f f786-816, release f816 |
| 15 hh tt | C1/521C | 3 | arm parks; revisit until done, advance 3 | homing arm, mode 7 | CF/F405 / CF/F884 steering | 29: t8/t9 `15 FF 0C` arm f783, arrived f784, released f784 |
| 9C mm tt | C1/6299 | 3 | as 98 | 98's arm on a QUARTER vector, count x4 | mode 1 hop-slide with 4 hops | 23: t8 `9C 00 0C` f869 |
| 9D xx | C1/672E | 2 | as 99 (xx never read) | 99's arm, same two changes | same | 23: t9 f873, t10 f881 |
| 5E | C1/5D12 | **1** | costs | tracks 0-3 only: $9FF7[c] = ($9FF7 & 7F) \| 80 | the $9FF7 bit-7 gates (untargetable, no body) + hidden | 37: f1275 01->81 (C1:5D0D); f1276 the next byte `00` ends t3 |
| 6B xx | C1/5EDF | 2 | costs | $AB4B = xx, $AB4A = 1, $AB4E = 1, maybe INC $A08B | CF/E602: OBJ pal 4 colours 1-12 rotate every xx frames | 0E: f912, rotations f913, f933, .. f1053; 6D f1063 |
| 25 nn | C1/5769 | 2 | $A018[nn] < 80: FREE, advance 2; else the track ENDS | (none) | per-target track exists only if target nn does | 1D: `25 00` / `25 01`, $A018 = FF: t9, t10 die f1001 |
| 43 tt xx yy | C1/5A0C | 4 | FREE | $5D9E/$5D9F = resolve(tt) + (xx,yy) | stored point | already ported (sec. 6) |
| 4A (= 4D) | C1/5A99 | 1 | FREE | swap $5D9C/$5D9D | sync counters | already ported (sec. 6) |

`c` = the track's combatant `$A014[track]` (tracks 0-3), track 4 = every entry of the `$A018` list up to a byte with bit 7,
tracks >= 8 = the effect object `$A2B6` = track - 8. Unless the table says otherwise an op does nothing on a track >= 8.

## 2. The orbit family C0 / C1 / C2 / C3 (C1/6CF3, 6F31, 6F53, 6F75 -> C1/6D14)

```
C0  $86=tt $80=a $82=b $84=c ; JSR C1/762E ; $8E=5 ; C1/6D10 $8F=3      C2  same, $8E=5 $8F=5, JMP C1/6D14
C1  $80 $82 $84 ; $A2B0/2 = $5D9E/F ; $8E=4 ; C1/6D10 $8F=3             C3  same as C1 but $8F=5, JMP C1/6D14
C1/6D14  track >= 8 -> C1/6E8F (effect arm, = the port's case 0xC1, mode $8F)   track 4 -> C1/6DC7   else combatant:
C1/6D25  Y = $A014[track] ; $A337,Y set -> C1/6DB8 revisit
  arm:   b == FF -> b = $A5AA,Y
         $AA74,Y = $1D0C,Y = $A2B0 ; $AA87,Y = $1D23,Y = $A2B2      <- the combatant's FOOT jumps to the centre
         a bit7 clear: $AA9A,Y = b - $A324,Y ; $AAD3,Y = $A324,Y     set: $AA9A = b + $A324 ; $AAD3 = -$A324
         $AAE6,Y = a & 7F (radius) ; $A2EB,Y = 1 ; $A86D,Y = $8F (mode) ; $A19D,Y = 0
         $AAAD[Y*2] (16-bit) = ($40 * c) / $A324,Y  (C1/0089, C1/00D7: x/0 = FFFF) ; $A337,Y = 1 ; C1/6F27 park
  revisit C1/6DB8: $A19D,Y == 0 -> park ; else $A337,Y = 0, C1/6F2C advance $8E
C1/6DC7 (track 4): for each list entry Y: $A337,Y set -> jump to the revisit ; else the arm above (and note: b == FF is
         REPLACED in $82 by the first target's $A5AA, so later targets start at that angle) ; list end -> park.
  revisit C1/6E69: n = number of entries with $A19D set (each of those: STZ $A337) ; n != 0 -> PARK ; n == 0 -> ADVANCE.
         (literal: the op moves on one frame after the arm while the orbits keep running; a target already done parks it
          and its cleared $A337 makes the next visit re-arm it.)
```

Oracle, C2 on Crono (tech 07, f853): $AA9A=D8 (E0-08), $AAD3=08, $AAE6=14, $A86D=05, $A337=01, $AAAD=0008 ($40*1/8),
$1D0C/$1D23 := 88/7C (C1:6D46/6D4F). C0 on slot 1 (tech 05, f858): $AAA5=EB (b=$A5B5=F3 minus 8), $AADE=08, $AAF1=08,
$AA7F/$AA92 = 80/80, $A878=3, $AAC3=0008 -- the existing C1 arm, field for field.

## 3. The movers: both mode tables, and the modes these ops arm

CF/EFAF: runs only while `$A013` (else CF/F978, the AI mover). Combatants X=10..0: `$A337` live, `DEC $A2EB / BNE`, reload
`$A2FE`, `JSR ($F01E + 2*$A86D)`; then CF/EFED effect objects X=7..0: `$A342`, `$A2F6` / `$A309`, `JSR ($F02E + 2*$A878)`.
`$C6` = index, `$C8` = index*2, `$CA` = index*4.

| mode | combatant (CF/F01E) | effect object (CF/F02E) | armed by |
|---|---|---|---|
| 0 | F03E slide | F4B2 slide | 11/12/10 (+D3 obj) |
| 1 | F087 hop-slide | F503 hop-slide | 98/99/9A/9B/9C/9D |
| 2 | F194 move-forward | F61F move-forward | A8/A9/A2 |
| 3 | F1F8 orbit via draw offset | F68D orbit (ported) | C0/C1 |
| 4 | F23D hop to point (ported) | F6E2 (ported) | D2 |
| 5 | F354 ELLIPSE via draw offset | F7FC ellipse + depth | C2/C3 |
| 6 | F39F SHAKE | F39F (the same routine; indexes the COMBATANT arrays with the slot -- no op arms it) | D8 |
| 7 | F405 homing | F884 homing | 15 |

Mode 3 / 5, combatant (CF/F1F8 / CF/F354): `$A19D,Y` set -> RTS. `$00 = $AAE6,Y` (mode 5: `LSR` -> radius/2 for the Y term);
`$AA9A += $AAD3`; `$A4AF,Y = F9FB($AA9A)` (r*sin, the Y offset); mode 5 reloads `$00 = $AAE6` (full radius);
`$A4A4,Y = F9FB($AA9A + 40)` (r*cos, the X offset); `$AAAD[Y*2] -= 1` (16-bit), zero -> `INC $A19D`. The FOOT stays on the
centre; only the draw offsets move. Oracle, tech 07 f854-861: $A4A4 0E 10 12 13 13 13 12 10, $A4AF F8 FA FC FE 00 01 03 05,
angle E0..18 by 8; done + release f861.

Mode 5, effect object (CF/F7FC): done -> RTS. `X = (slot*11 + $AB4F[slot]) & FF` (CF/FA17 multiply, 8-bit add); with the angle
BEFORE this tick's step: `$AAA5 < 80 -> $A3DF[X] = ($A3DF[X] & 3F) | 80` (drawn before, i.e. IN FRONT of combatant $AB4F),
else `| 40` (behind). Then mode 3's body with the sin term's radius halved: `$A240 = $AA7F + r*cos`, `$A250 = $AA92 +
(r>>1)*sin` (8-bit adds, high bytes untouched), 16-bit count, done. Needs `$AB4F[slot]` from op 73/74 (ported).

Mode 6, combatant (CF/F39F -> CF/F3BA): `DEC $AB13`; zero -> `$A4A4 = $A4AF = 0, $A337 = 0`, RTS. Else `DEC $AB29`; nonzero ->
RTS; zero -> `$AB29 = $AB34`, `$00 = $AB1E`, `$AB3F ^= 1`: now 1 -> `$A4A4 = $A4AF = 0` (rest); now 0 -> `$A4AF = F9FB($A5AA)`,
`$A4A4 = F9FB($A5AA + 40)` (displaced along the combatant's heading, read each time). Then (CF/F3A2) `$C6 == 3 && $A113 != 0`
-> `$A4AD = $A4A7`, `$A4B8 = $A4B2` (combatant 9 copies combatant 3's offsets; $A113 is staged by the enemy loader, CC/ECAF).
Oracle tech 07: `D8 04 02 23` on GATO ($A5AA=00): $A4A4[6] = 03 at f896, 00 at f898, 03 at f900 .. (two ticks displaced, two
at rest); `D8 06 02 23` with $A5AA=80 (op 85 at f955): FA; $AB13 0x24 -> 0 and $A337 -> 0 at f992 = 36 ticks after the arm.

Mode 7, combatant (CF/F405): `$CC,$CD = $1D0C,$1D23`; `$CE,$CF = $A383,$A396` (copies at $AC4A-D); CF/F957 with `$D1 = 10`:
`|x-gx| < 10 && |y-gy| < 10` (8-bit differences made absolute) -> `INC $A19D`, RTS (no move). Else CF/FA43 = `$AC52 =
atan2(pos, goal)` (the same law as C1/0222 = the port's `battleAtan2(x, y, gx, gy)`); `Y = $00 = $A324`; `$AC24` set ->
heading = $AC52; else heading `$AC11` == $AC52 -> keep; else turn by 1 up to `speed` times, `+1` when `$AC37 == 0`, `-1`
otherwise, stopping early on $AC52 with `INC $AC24` (locked on for good); `$AC11 = heading`; facing `$96DB = C0:F700[heading]`;
`$1D23 += F9FB(heading)`, `$1D0C += F9FB(heading + 40)` with `$00 = speed` (8-bit adds). Object (CF/F884): the same on
`$A32F / $AC1C / $AC42 / $AC2F / $A38E / $A3A1`, facing `$A230`, done `$A1E0`; the position read is `$A240/$A250`, 0 when its
high byte is negative; the move is 16-bit with the offsets sign-extended.

Mode 1, effect object (CF/F503) -- needed by 9C/9D, which the census scripts run on effect tracks (not ported: the port's
`battleAscHopArm` returns for t >= 8): done -> RTS; `$AC09 == 0` -> done; `$A8EA += $A89E`, `$A910 += $A8C4` (16-bit);
`$A32F` sub-steps: phase `$A9F4` 0: `$A94C -= $A9CE`, a borrow -> `$A94C = $A94E = 0, $A9F4 = 1`; phase 1: `$A94C += $A9CE`,
a carry -> `$A94C = $AA53, $A94E = 0, $A9F4 = 0`; then the height `$A998/$A99A` (32-bit) += (phase 0) or -= (phase 1) `$A94C/E`;
`$04 = $A99A`; `$A240 = $A368 + $A8EB` (high 0); `$A250 = $A37B + $A911 - $04` (`$AA1A` set) or `- 2*$04` (clear), `+` instead
of `-` when `$AA35` (9A/9B); `DEC $AC09`, zero -> done. Oracle tech 23 slot 0: height 1 at f872 -> $A250 = 83 - 2 - 2*1 = 7F.

## 4. What the written bytes do (every reader in the ROM)

* **$A4A4 / $A4AF** (combatant draw offsets, signed). Readers: CF/EAE3 `LDA $A498,X` and CF/EAF8 `LDA $A4A3,X` (X = cmb+0C:
  = $A4A4+cmb / $A4AF+cmb), sign-extended and added to the cells' base `$1D0C` / `$1D23` -- the CELLS only; the ground shadow
  (CF/EA8A, `$1D00,X-8 / $1D17,X-4`) and the status icon read the bare foot. CF/ED5A / ED98: the afterimage history
  (ported, battleTrailRecord). CF/F26A: mode 4's arrival (`$A4AF >= E8`). Writers: C1/588D-58A0 (2B), C1/6FC9/6FE0 (C4/C5),
  CF/F158-F18B (mode 1 hop), F211/F21D (3), F345/F350 (4), F36E/F37F (5), F3C1-F401 (6), CC/F091/F094 (the action wipe).
* **$9FF7** (per combatant; `$9FEB,X` with X = cmb+0C). `BNE` readers (any bit hides): CF/EAD7 (the cells' compose returns),
  CF/EA8F (no shadow), CF/E9CB (no afterimage), CF/FE28 (the removal routine skips its `INC $A0A8` / palette part). `BMI`
  readers (bit 7 only): C1/204C 2374 24F8 267E 273E (the target/list builders), C1/28BD (body collision), C1/29C4 2A0C 2A61
  2AAC. So 29 (bit 0) hides without untargeting; 5E (bit 7) also removes the combatant from lists and collisions. The port's
  field.c already reads the byte (battleHiddenObj, the shadow at 7486).
* **$A5CD** bit 0 (27/28): CF/E855 `LDA $A5C1,Y` (Y = cmb+0C) -> a second render entry, key FE, in front of everything
  (battle_enemy_cells.md sec. 4; field.c 5234 already reads it). Bit 7 (the loader's, CC/EB83) is what C1/28C2 tests; 27/28
  preserve it. D1/F1DD is a false scan hit (inside `JSL CD0C9D`).
* **$AB4A/$AB4B/$AB4E/$A08B** (6B/6D): the bank-CF frame-service pass, right after the bracket pulse CF/E5AA-E5DB (ported as
  battlePulseFrame) and the `$96F1/$96F3` 32-bit counter (CF/E5EB-E5FF):
  ```
  CF/E602  $AB4E == 0 -> skip ; DEC $AB4A ; != 0 -> skip ; $AB4A = $AB4B
           rotate words $2382..$2398 (OBJ palette 4, colours 1..12) up by one: new[C1] = old[CC], new[Cn] = old[Cn-1]
           $A08B != 0: lo = $99D3 & 0F ; rotate colours $99D3 .. ($99D3|0F) up by one (0F-lo copies; last -> first) ;
                        then every following row up to row 7 (CA = 2*(($99D3|0F)+10) 8-bit, += 20 while < 100):
                        colours 1..15 of the row up by one
  ```
  6B's `$A08B` scan (C1/5EED): `$99D2 != 0` and, walking `$A017,X` from X=0 (role 3 then the `$A018` list) to a byte with bit 7,
  the first combatant with `$A08C,Y != 0 && $993C,Y == 08` -> `INC $A08B`. Stopped by 6D (C1/5F9A) and by the action's end
  (C1/40EF `STZ $AB4E`). Oracle tech 0E: `69 04` (C1:5E39 loads D1:4D90[4*24] into $2382..) then `6B 14` at f912; f913 the
  first rotation (`$2382` := old CC = 7FFF, `$2384` := old C1 = 7F9B ..., writer CF:E631), then f933, f953 .. f1053: every 0x14
  frames; `6D` at f1063. Visible: the victim, drawn with palette 4 by 69's `$993C := 08`, shimmers (its 12 ice colours cycle).
* **$A5AA / $A5B5** (75): the heading read by the orbit arms (b = FF), mode 6 (D8's direction), A8/A9 (C1/69EA ..), 15's
  start heading (hh = FF), CF/F3EF/F3F9.

## 5. Findings the port must fix outside the new cases

1. **Op 5E is 1 byte; the port sizes it 2.** battleAnimOpLenRom walks C1/5D12 `A9 80 85 8E 80 DB` over the BRA into op 5F's
   handler and returns 5F's `A9 02 4C BB 75`. The real tail is C1/5D0D `LDA #01` (C1/5CF3's body). Oracle tech 37: 5E at
   CE/31A7 on f1275, cursor CE/31A8 (`00`) and track 3 dead at f1276 -- a 2-byte read would skip that `00` and run
   `7A DD 00 ..`. Needs a handler-keyed override consulted BEFORE the scan (`0x5D12 -> 1`). Ops 28 (C1/5840) and 2A (C1/5879)
   also BRA backwards into a neighbour's body; their scans return the neighbour's `1`, which is right only by coincidence
   (true tails C1/583B and C1/5874 are `LDA #01` too) -- put them in the same override list.
2. **Op C3 has no length** (the census's only STOP): `case 0x6F75: return 4;` (`$8E = 4` at C1/6F90; three `INY` fetches).
3. **Two stores for $A4AF, none for $A4A4.** The render adds `ascHopY[c]` (field.c battleHopY, five sites) and never an X
   offset; the mode-1 hop (battleAscHopTick) writes only `ascHopY`, the mode-4 loop both, the trail recorder reads WRAM. The
   new ops read AND write the offsets (C4 compares, D8/C0-C3 write both axes), so: make WRAM `$A4A4/$A4AF` the store (mirror
   `ascHopY[c] = (int8)s[0xA4AF+c]` after every writer, and have battleAscHopTick write the WRAM byte too), and add an X
   helper (`(int8)mem[0xA4A4+cmb]`) beside battleHopY at the same five sites -- cells only, never the shadow.
4. **Speed / rate are per COMBATANT in the ROM.** `$A324 / $A2FE` (ops 08-0F C1/4F6B-4F76, track 4 looping the list; 07) are
   read by the combatant arms of C0-C3, 15 and by the combatant mover loop. The port keeps `ascSpeed/ascRate` per track and
   writes WRAM only in D2. Mirror them in case 0x08-0x0F / 0x07 exactly as the ROM stores them, and read WRAM in the new arms.
   D8 itself overwrites `$A2FE[c] := 1` (a later move of that combatant runs at rate 1 until a speed op).
5. **The combatant mover loop dispatches mode 4 only** (battleCmbMode4Frame, `if (s[0xA86D+c] != 4) continue`). Modes 3, 5, 6,
   7 must be dispatched there (after the same `$A2EB` countdown); modes 0-2 stay with the port's per-track fields. The effect
   loop (battleEffectMoverFrame) needs modes 5, 7 and 1. D4/D6's combatant test must then also accept `$A19D` set by modes
   3/5/7 (it currently special-cases mode 4).

## 6. Implementation specs (battleAscTick, in the style of `case 0xC1`)

Common: `uint8_t* s = h->mem;`, `a1..a4` = rom[ip+1..ip+4]; `F9FB(ang, r)` = `battleFxSinScaled(rom, romLen, ang & 0xFF, r)`;
`list(i)` = `s[0xA018 + i]` (stop at bit 7); "cmb c" = `h->ascRole[t]` for t < 4. After any write of `$1D0C/$1D23` mirror
`h->mvLiveX/Y[c]`; after any write of `$A4AF[c]` mirror `ascHopY[c]`. `adv = 0; isFree = 0;` = park.

**C0 / C1 / C2 / C3** -- replace `case 0xC1` by one shared case:
```c
case 0xC0: case 0xC1: case 0xC2: case 0xC3: {
  int two = (op == 0xC0 || op == 0xC2);                 // tt present
  int mode = (op <= 0xC1) ? 3 : 5, L = two ? 5 : 4;     // $8F / $8E
  int A = rom[ip+1+two], B = rom[ip+2+two], C = rom[ip+3+two];
  if (two) { if (battleAscResolve(h,t,a1,&gx,&gy)) { s[0xA2B0]=gx; s[0xA2B2]=gy; } }   // C1/762E (unresolved: keep $A2B0/2)
  else     { s[0xA2B0] = s[0x5D9E]; s[0xA2B2] = s[0x5D9F]; }
  if (t >= 8)  -> the existing effect arm with s[0xA878+y] = mode (revisit/advance by L, not 4);
  else if (t == 4) {                                     // C1/6DC7
    for (i = 0; !(list(i) & 0x80); i++) { c = list(i); if (s[0xA337+c]) goto revisit4; cmbArm(c, &B); }   // B may become $A5AA
    adv = 0; isFree = 0; break;
    revisit4: n = 0; for (i = 0; !(list(i)&0x80); i++) { c = list(i); if (s[0xA19D+c]) { s[0xA337+c] = 0; n++; } }
    if (n) { adv = 0; isFree = 0; } /* else advance L */ break;
  } else { c = cmb; if (s[0xA337+c]) { if (!s[0xA19D+c]) { adv=0; isFree=0; } else s[0xA337+c]=0; break; }
           cmbArm(c, &B); adv = 0; isFree = 0; }
  // len stays L; the advance costs the frame (no INC $A00F)
}
cmbArm(c, &B):  if (*B == 0xFF) *B = s[0xA5AA+c];
  s[0xAA74+c] = s[0x1D0C+c] = s[0xA2B0]; s[0xAA87+c] = s[0x1D23+c] = s[0xA2B2];
  sp = s[0xA324+c];  if (!(A & 0x80)) { s[0xAA9A+c] = *B - sp; s[0xAAD3+c] = sp; } else { s[0xAA9A+c] = *B + sp; s[0xAAD3+c] = -sp; }
  s[0xAAE6+c] = A & 0x7F; s[0xA2EB+c] = 1; s[0xA86D+c] = mode; s[0xA19D+c] = 0;
  q = sp ? ((0x40*C) & 0xFFFF) / sp : 0xFFFF; wr16(s, 0xAAAD + 2*c, q); s[0xA337+c] = 1;
```
Mover (combatant loop, modes 3/5): `if (s[0xA19D+c]) continue; r = s[0xAAE6+c]; s[0xAA9A+c] += s[0xAAD3+c];
s[0xA4AF+c] = F9FB(s[0xAA9A+c], mode==5 ? r>>1 : r); s[0xA4A4+c] = F9FB(s[0xAA9A+c]+0x40, r); if (--cnt16(0xAAAD+2c)==0)
s[0xA19D+c]++;`. Effect loop mode 5: the depth write of sec. 3 first (old angle), then mode 3's body with the sin radius r>>1.

**C4 xx yy / C5 yy** (C5: xx = 0, len 2):
```c
case 0xC4: case 0xC5: { int X = (op == 0xC4) ? a1 : 0, Y = (op == 0xC4) ? a2 : a1;
  if (t >= 8) break;                                              // C1/6FEA: advance
  if (t == 4) { for (i = 0; !(list(i)&0x80); i++) { c = list(i); if (s[0xA4AF+c] == X) goto adv;   // the FIRST equal
                  s[0xA4AF+c] += Y; }                                                             // target advances
                adv = 0; isFree = 0; break; }                                                     // (rest unstepped)
  c = cmb; if (s[0xA4AF+c] == X) break; s[0xA4AF+c] += Y; adv = 0; isFree = 0; break; }
```
Costs the frame either way. Oracle tech 08: step f852 (park), equal f853 (advance), next op f854.

**27 / 28** (len 1, costs): `v = op==0x27 ? 1 : 0;` t < 4: `s[0xA5CD+c] = (s[0xA5CD+c] & 0x80) | v`; t == 4: the same for every
list entry; t >= 8: nothing.

**29 / 2A** (len 1, costs): t < 4: `s[0x9FF7+c] = v` (WHOLE byte: 2A clears a bit 7 set by 5E); t == 4: `s[0x9FF7+c] =
(s[0x9FF7+c] & 0x80) | v` per entry; t >= 8 nothing.

**5E** (len 1 -- fix sec. 5.1; costs): only `t < 4`: `s[0x9FF7+c] = (s[0x9FF7+c] & 0x7F) | 0x80` (C1/5CF6 `CMP #04 / BCS`: track 4
and effect tracks do nothing). (5D, not in the census, is the same with 00.)

**2B** (len 1, costs): t < 4 / each list entry: `s[0xA4A4+c] = s[0xA4AF+c] = 0`.

**D8 xx yy zz** (len 4, costs, never parks): t < 4 / each list entry c: `s[0xAB1E+c]=xx; s[0xAB34+c]=yy; s[0xAB13+c]=zz+1;
s[0xAB29+c]=s[0xAB3F+c]=s[0xA2FE+c]=s[0xA2EB+c]=1; s[0xA86D+c]=6; s[0xA337+c]=1;` t >= 8 nothing. Mover mode 6 as sec. 3
(including the `c == 3 && s[0xA113]` copy to combatant 9); it clears `$A337` itself.

**75 nn** (len 2, FREE): `src = nn < 0x0A ? s[0xA5AA + s[0xA014+nn]] : s[0xA5B5 + nn - 0x0A]`; t < 4: `s[0xA5AA+c] = src`;
t == 4: for `i = 0; !(s[0xA015+i] & 0x80); i++` `s[0xA5AA + s[0xA015+i]] = src` (C1/60C6 walks from `$A015`, role 1 --
literal); t >= 8: `s[0xA5B5+(t-8)] = src`. Keep `h->ascHead[]` coherent: set `ascHead[t'] = src` for every track t' whose
combatant/slot was written (the ROM stores one heading per combatant/slot; the port's per-track copy is a shadow of it).

**19 xx yy** (len 3, costs): t < 4 / each list entry: `$1D0C/$1D23 = xx/yy` (+ mvLive); t >= 8: `$A240[2y] = xx, $A241 = 0,
$A250 = yy, $A251 = 0` (+ `ascScrX/Y`). = `battleAscSetPos(h, t, a1, a2)` plus the track-4 loop.

**10 xx yy** (len 3): exactly `case 0x11` with `gx = a1, gy = a2` (C1/4FBF sets `$A2B2 = yy`, `$A2B0 = xx`, `$8E = 3`, then
C1/4FED); `ascParkLen = 3`; mode 0, no hop; an effect track keeps the bearing as its heading (C1/50C2, oracle f818 `$A5B5 =
40`). Track 4 (C1/5024): every list target armed through C1/5135, then advance 3 at once (no park).

**15 hh tt** (len 3): resolve(tt) -> goal. Combatant arm (C1/523C): if `$A337` -> revisit (`$A19D` ? `$A337 = 0`, advance 3 :
park). Else `$DB = battleAtan2($1D0C, $1D23, gx, gy)`; `h0 = hh == FF ? $A5AA : hh`; `$AC11 = h0`; `$AC37 = turnDir(h0, $DB)`;
`$A2EB = 1; $A86D = 7; $A311 = $A19D = $AC24 = 0; $A383/$A396 = goal; $A337 = 1`; park. Object (C1/5333): the same on
`$A240/$A250` (their high bytes zeroed at the arm), `$A5B5`, `$AC1C/$AC42`, `$A2F6/$A878/$A31C/$A1E0/$AC2F`, `$A38E/$A3A1`,
`$A342`. Track 4 (C1/52A7): walk the list arming each idle entry; the first entry found live jumps to a revisit that starts AT
THAT index (C1/530E `LDX $80`, unlike C0's which restarts at 0) and counts the NOT-done entries (done ones get `$A337 = 0`):
some left -> park, none -> advance 3. `turnDir` = C1/53B0 with q = angle >> 6:
```
q(h)==q(db): h >= db ? 1 : 0          half = ((h + 0x80) & 0xFF) < db ? 1 : 0
q(h)=0: q(db) 1->0 2->half 3->1    q(h)=1: 0->1 2->0 3->half    q(h)=2: 0->half 1->1 3->0    q(h)=3: 0->0 1->half 2->1
```
(0 = the heading steps +1, 1 = -1.) Oracle tech 29: `$AC42` = 01 / 00 for headings 35 / 45 with `$DB` = C0 -- as computed.

**9C mm tt / 9D xx** -- ops 98 / 99 with two changes (C1/62E4-62EF, C1/6319-6329; objects C1/6425-6430, C1/645B-646B):
before C1/0222 + C1/65BA the target point is replaced by C1/6549's quarter point `(D3 +/- |dx|>>2, D4 +/- |dy|>>2)` (8-bit,
toward the goal) -- so the bearing, the unit steps and the hop's `half` (= quarter dominant >> 1) come from it -- and the count
(`$ABFE` / `$AC09`, 8-bit) is shifted left twice. The stored goal ($A383/$A396, $A38E/$A3A1) is the full point. Result: the
slide covers ~the full distance (quantised to 4*(|d|>>2)) in 4x the ticks while the hop's decrement is 4x larger: four hops.
Oracle tech 23 (f869, (86,83) -> (88,4D), speed 1): `$AC09 = 34` = 4*(0x36>>2), `$A8C4 = FF00`, `$A89E = 0`, `$A9CE = 2AAA` =
FFFF/6, `$A94C = FFFF`. In the port: pass the quarter point to `battleSlideArmSteps` / `battleAscHopArm`'s `half`, the full
point to the goal, and `ascMvN[t] = (n << 2) & 0xFF`. Both scripts in the census use them on EFFECT tracks, so the object mode
1 (sec. 3) is a prerequisite. Track 4 (C1/63F9 -> C1/6540) does nothing but advance by the stale `$A3A9[4]` (literal; unused?).

**6B xx** (len 2, costs): `s[0xAB4B] = xx; s[0xAB4A] = 1; s[0xAB4E] = 1; if (s[0x99D2]) for (i = 0; !(s[0xA017+i] & 0x80); i++)
{ y = s[0xA017+i]; if (s[0xA08C+y] && s[0x993C+y] == 0x08) { s[0xA08B]++; break; } }`. Note the port renders the flash from
`h->objPal[]`, not `$993C`: read the port's store the op-69 arm writes (battleAscFlashOn: `objPal[c] = 0x08`) or mirror it.
Frame service: new `battleFxPalRotateFrame(h)` right after `battlePulseFrame(h)` in battleTickBody (the same gate), sec. 4's
CF/E602 on `s[0x2200..]`; clear `$AB4E` at the action's end (C1/40EF). The port already runs the pulse before the script, which
is the oracle's order (6B at f912, first rotation f913).

**25 nn** (len 2): `if (s[0xA018+nn] & 0x80) { h->ascLive[t] = 0; adv = 0; isFree = 0; }` (C1/577A JMP C1/4CF0: `STZ $5DAC,X`,
the cursor stays) else `isFree = 1` (INC $A00F at C1/5772). Oracle tech 1D (solo Robo, `$A014 = 00 01 02 00 FF..`): t9 `25 00`
and t10 `25 01` both end at f1001, the frame their `24 03` released.

**43 / 4A** (ported, checked against C1/5A0C / C1/5A99): correct. Two notes: C1/5A1D adds whatever C1/762E left in
`$A2B0/$A2B2` even for a mode the port cannot resolve (the port keeps the raw xx/yy); op 4D dispatches to the same C1/5A99.

## 7. Open questions (not derived)

1. C3 was not run (only in dual/triple techs, which the single-character recipe cannot select); its path is C2's
   (measured) with C1's centre (ported), but no frame of it is measured.
2. C0/C2 on track 4 and C4/C5/D8/27-2A/2B on track 4 are disassembly only; the "n != 0 -> park" revisit of C1/6E85 and C4's
   "first equal target advances" are the ROM's literal branches, unmeasured.
3. C0 on a combatant (mode 3, CF/F1F8) was not captured; C2 on a combatant (mode 5) was. The two differ only by the `LSR`.
4. Op 15's steering (the turn loop, `$AC24` lock, the move) was not exercised: tech 29 arms at the goal and arrives on the first
   tick. Techs 60/69/6C carry 15 in flight (multi-character).
5. `$A08B`'s arena rotation needs `$99D2 != 0` (a BG-drawn enemy); GATO's arena has 0. Which formations set `$99D2/$99D3/$A08C`
   (CC/EC90, D1/F236) was not traced. The edge `$99D3 & 0F == 0F` (DEC of a zero count loops 256 times) is unmeasured.
6. `$A113` (CC/ECAF, from the loader's `$82`): which formations set it -- i.e. where mode 6's combatant 3 -> 9 copy fires -- not
   derived (Zombor's legs/upper body are combatants 3/9 in battle_enemy_cells.md, but that pairing is not proven here).
7. The pixels: $9FF7/$A5CD/$A4A4/$A4AF reach the picture through routines already read (CF/EAD7, E855, EAE3/EAF8), but no
   `--peek` frame was compared for these ops in this pass.
8. Effect-object mode 6 (CF/F39F with a slot index writing combatant arrays) is reachable only if `$A878 = 6`; no op in this
   group sets it.
