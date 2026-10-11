# The animation script's ORBIT (op $C1), the scratch-object mover, and the Zombor demo (2026-09-16)

Port: `src/battle.c` -- `battleEffectMoverFrame` (CF/EFED), `battleFxSinScaled` (CF/F9FB), the `case 0xC1` / `case 0xD4..0xD6`
arms in the track executor, and FD/A982's page clear in the staging. Check: `battle-anim-orbit-op-c1-vs-oracle`
(tools/zombor_orbit_cmp.py). Fixture: `saves/attract_zombor_pre.lakewram`. Trace: `CT_BATTLE_ORBTRACE=1`.

## 1. Why the Zombor leg was the attract's worst

`tools/attract_survey.py` after the wipes: loc 087 (the bridge, Zombor) 504 port frames against the cartridge's 880, median
difference 18.9, the twelve worst samples of the whole attract. `--monitor 7EA013:1,7E29C6:1,7E0119:1` on the cartridge from
f7085 of saves/attract_cena_inicial.state: battle mode at leg f249, ONE action (f368 -> f792, 424 frames: tech $3A, the triple
tech, actor seat 0, participants `00 02 01`), `$29C6` back to 40 at f793, the fade at f869. The port: `victory` on the fifth
frame of the fight.

**Cause 1 -- FD/A982.** `TDC / LDX #AD89 / STA $0000,X / INX / CPX #B3F5 / BCC`: the whole AI / command page $AD89..$B3F4 is
zeroed at the staging (the --monitor writer of every byte of it at f269). The port cleared two spans ($B188.., $AE91..) and
left the rest to the WRAM -- which on 087 held the credits' text under $AF15 (`41 53 48 49 2F 54 41 4B` = "ASHI/TAK"), and
bit 6 of 'A' marks an enemy slot HIDDEN to the alive test (`$AF15,X & 40`): no enemy, victory. The port clears the page now.

**Cause 2 -- op C1.** With the fight alive the demo commits the triple tech (`demo-commit-tech` at module f136, C1/B967's
record path as on 0AD/07D), and the script's four scratch objects (tracks 9-12) stopped on `C1 0C 00 02`: an opcode the port
had no length for.

## 2. Op $C1: the orbit (C1/6F31 -> C1/6D10, effect arm C1/6E8F)

```
C1/6F31  INY x3: $80 = a, $82 = b, $84 = c            three operands
C1/6F40  $A2B0 = $5D9E ; $A2B2 = $5D9F                 the STORED point (op 33's) = the centre
C1/6F4C  $8E = 4 ; JMP C1/6D10                         $8E = the advance the tail uses
C1/6D10  $8F = 3 ; `LDA $A010 / CMP #08 / BCS C1/6E8F` a track >= 8 = a scratch object, Y = $A2B6 (= track - 8: C1/4C0F-4C92
                                                        `STZ/LDA #k / STA $A2B6` per active track $5DB4+k)
C1/6E8F  `LDA $A342,Y / BNE C1/6F1A`                   already moving -> the REVISIT
         b == FF -> b = $A5B5,Y
         $AA7F,Y = $A2B0 ; $AA92,Y = $A2B2               the centre
         a bit7 clear: $AAA5,Y = b - $A32F,Y ; $AADE,Y = $A32F,Y       angle one step BEFORE b, step = +speed
                 set: $AAA5,Y = b + $A32F,Y ; $AADE,Y = -$A32F,Y       ... after b, step = -speed
         $AAF1,Y = a & 7F (radius) ; $A2F6,Y = 1 ; $A878,Y = $8F (mode 3) ; $A1E0,Y = 0
         $AF = $40 * c (C1/0089) ; $B5 = $AF / $A32F,Y (C1/00D7, x/0 = FFFF) -> $AAC3,Y*2 (16-bit count)
         $A342,Y = 1 ; C1/6F27 `LDA #00 / JMP C1/75BB`  PARK: the op is revisited every frame
C1/6F1A  `LDA $A1E0,X / BEQ C1/6F27` (still parked) ; else `STZ $A342,X` ; C1/6F2C `LDA $8E / JMP C1/75BB` = advance 4
```

`$A32F,Y` / `$A309,Y` are the scratch object's speed and rate (CC/F4BC / CC/F5BC by the op-08..0F byte); the port keeps them
per track (`ascSpeed` / `ascRate`) and writes the slot's copies at the arm. A combatant track (< 8) takes C1/6D32 -- the same
fields at the combatant's index, $A19D as the done flag, `$A86D,Y = $8F` -- no capture runs it: named, not ported.

## 3. The scratch-object mover, CF/EFED-F01B, and mode 3 (CF/F68D)

Once a frame, `LDX #0007` down to 0: `LDA $A342,X / BEQ` (moving), `DEC $A2F6,X / BNE` (the rate countdown, reloaded from
`$A309,X`), then `LDA $A878,X / ASL / TAX / JSR ($F02E,X)` -- the table: 0 CF/F4B2, 1 F503, 2 F61F, **3 F68D**, 4 F6E2,
5 F7FC, 6 F39F, 7 F884. Mode 3:

```
CF/F68D  `LDA $A1E0,Y / BNE rts`                      done: nothing
         $00 = $AAF1,Y ; $AAA5,Y += $AADE,Y            the radius, the angle
         $CC = CF/F9FB(angle) ; $CD = CF/F9FB(angle + $40)          r * sin, r * cos
         X = Y*2: $A240,X = $AA7F,Y + $CD ; $A250,X = $AA92,Y + $CC  8-bit adds (the objects' screen X / Y)
         $AAC3,X -= 1 (16-bit) ; zero -> $A1E0,Y = 1
CF/F9FB  REP / ASL / ASL / AND #03FF / TAX / LDA $C0F900,X / AND #00FF   the table, every 4th byte (255 at $40)
         `CPX #0200 / BCC` -> EOR #FFFF / INC          negated for angles >= $80
         $00 * value: $04:$05 = r * low, $05 += r * high; `LDA $05 / RTS`  the 16-bit product's high byte
```

The port runs this pass at the start of every frame's track step (before the tracks' visits: measured, the round
transitions -- the count reaching 0, the revisit clearing $A342, the next round's arm -- fall on the cartridge's frames
exactly) and mirrors `$A240/$A250` into the track that owns the slot, which is what draws the object.

## 4. Ops D4 / D5 / D6: wait for the move (C1/7349 / C1/73AD / C1/7364)

D4 first spends the 16-bit timer `$AAFC` (D5: `$AAFE`): `LDA / ORA / BEQ` then `SBC #01 / SBC #00` and park (C1/73A3
`LDA #00`). At zero both fall into D6: a scratch track (C1/738E) `LDA #01 / STA $A342,X` then `LDA $A1E0,X / BEQ park`;
done -> `STZ $A342,X / STZ $A1E0,X` and C1/73A8 `LDA #01 / JMP C1/75BB` (advance 1, the frame spent). A combatant (C1/7373)
`$A337,X = 1` then `$A19D,X`. The port ports the scratch branch on the WRAM flags and tests a combatant's own mover state
(slide steps, FWD count, hop) for $A19D.

## 5. Graded

`ct.exe --monitor 7EA240:16,7EA250:16,7EAAA5:8,7EAAC3:16,7EA1E0:8,7EAA7F:8,7EAA92:8,7EA342:8` over the leg against the
port's `CT_BATTLE_ORBTRACE` from the fixture, aligned on slot 1's first service: 83 frames of angles, counts, done and active
flags and positions RELATIVE to the centres identical -- the three rounds (`C1 0C .. 02`, `C1 0A .. 01`, `C1 08 .. 01`:
radius 12 for 32 steps, 10 for 16, 8 for 16, speed 4) and every transition. The cartridge draws a 24-px-wide circle
(r = 12 -> +11 / -12 through the table's 255 and the high byte). Five mutations killed.

**The centres differ: cartridge (143,160), port (137,160).** The stored point is a combatant's position, and the party's
battle positions come from C0/04DE's `$29B0 + 5*seat` records (byte +2 = the foot's pixel within the tile), staged from
where the party stood on the field when the location's `D8` fired -- the port's 087 walk leaves them 6-8 px to the left
(Crono's home (39,126) against the cartridge's (47,126)). A field residual of the 087 leg, reported by the check's second
line and open.

## 6. The hop-slide family: ops D2 / D3 / D7 and mover mode 4 (2026-09-16, second pass)

Port: `battleFxMode0` (CF/F4B2), `battleFxMode4` (CF/F6E2), `battleCmbMode4Frame` (CF/EFAF + CF/F23D), `battleFxNear` (CF/F957),
`battleLaw6671` / `battleLaw65BA` / `battleLaw7313`, the `case 0xD2` / `0xD3` / `0xD7` / `0x21` arms. Check:
`battle-anim-hopslide-ops-d2-d3-d7-vs-oracle` (tools/zombor_hopslide_cmp.py). Trace: `CT_BATTLE_M4TRACE=1`.

After the orbits the tech's tracks run `D2 0C` (the five scratch tracks 9-13 and the actor's track 0), `D3` on track 8, and
wait with D4/D5/D6. The port had lengths for them but no arms, so every track parked and the action never ended.

### 6.1 Op D2 (C1/705A): the hop-slide to resolve(tt)

```
C1/705A  INY / LDA [$E6],Y / JSR C1/762E              tt -> $A2B0/$A2B2 (C1/762E: `0C` = C1/764A, role 3's foot + $9708/$9713)
C1/7060  $A010 >= 8 -> C1/7138 (scratch)  == 4 -> nothing  else the combatant $A014[track]
         $D3/$D4 = the position (kept: $A35D/$A370 for a combatant, $A368/$A37B for an object), $D5/$D6 = the point
         JSR C1/0222 (the bearing $DB; an object keeps it in $A355 and its facing $C0:F700[$DB] in $A230)
         JSR C1/6671: $A880/$A882 = 4 * the signed deltas (16-bit); $AAF9 = x -/+ |dx|/2; $AAFA = y -/+ |dy|/2 then
                      - 0x40 floored at 0 (the hop's midpoint); $AAFC = 0x24
         $A880 *= speed, $A882 *= speed (C1/00A7, the low 16 bits; speed = $A324,cmb / $A32F,slot)
         $AA04,cmb*2 / $AA1A,slot*2 = 0 (16-bit)         the "height once" flag, clear = twice
         $B5 = 7FFF / 80 = FF (C1/00D7)                   the hop's velocity decrement
         goal $A383/$A396 ($A38E/$A3A1), rate countdown 1, MODE 4 ($A86D / $A878), done 0, arrival latch ($AB00 / $AB0B) 0
         steps $A888/$A8AE ($A89E/$A8C4), decrement $A9B8 ($A9CE) = FF, velocity $A920 ($A94C, 32-bit) = 7FFF, its reset
         copy $AA3D ($AA53) = 7FFF, accumulators $A8D4/$A8FA ($A8EA/$A910) = 0, height $A96C ($A998, 32-bit) = 0, phase
         $A9DE ($A9F4) = 0.   C1/7209 `LDA #02`: advance 2.
```

### 6.2 Mover mode 4 (CF/F6E2 for objects, CF/F23D for combatants)

The combatant loop CF/EFAF runs while `$A013` is set, X = 10..0: `LDA $A337,X / BEQ`, `DEC $A2EB,X / BNE`, reload `$A2FE,X`,
`JSR ($F01E,X)` by `$A86D,X` -- and falls into the scratch loop CF/EFED (section 3). Mode 4:

```
phase != 0 (falling):  CF/F957 arrival: |x - goal.x| < $D1 and |y - goal.y| < $D1 (8-bit differences made absolute by
                       `SBC / BPL / EOR #FF / INC A`); $D1 = 0x18 (objects) / 0x10 (combatants, and then only with
                       $A4AF >= E8) while the latch is clear -> INC done, INC latch, RTS; 1 px once the latch is set -> INC done, RTS
accumulators += steps (16-bit, slot*2)
LDX #0004: four sub-steps  phase 0: velocity -= decrement; a borrow: velocity = 0, phase = 1, INC done (the APEX -- what D6
                                    waits on first)            phase 1: velocity += decrement; a carry: velocity = the reset
                                    copy, phase = 0
                           phase 0: height += velocity (32-bit)   phase 1: height -= velocity
x = base + accumulator.high;  y = base + accumulator.high - height.highword (once if the flag is set, else twice)
a combatant: $1D0C/$1D23 = those, $A4AF = -height.highword (once / twice)
```

With FF per sub-step from 7FFF the apex comes after 129 sub-steps = 33 services, the height there ~0x20 px, drawn twice = 64 px:
the midpoint's `- 0x40`.

### 6.3 Op D3 (C1/720E), op D7 (C1/740B), op 21 (C1/5722)

D3: `$A2B0/$A2B2 = $AAF9/$AAFA`. A combatant: C1/5135 (the 11/12 slide arm) then `STZ $A337,X` and C1/7313 with `$A324,X`. An
object (C1/7244): C1/0222, C1/65BA (unit steps 0x100 on the dominant axis, (minor << 8) / dominant on the other, signed;
`STA $AAFE` = the dominant), `$ABF6,Y` = dominant / `$A32F,Y`, steps * speed, `$A355 = $A5B5 = $DB`, `$A230` = the facing,
goal, rate 1, MODE 0, `$A31C = 0`, done 0, base = the position, steps, zeroed accumulators, then C1/7313 with `LDA $A32F,X`
where X is the slot * 2 (C1/72E3 `TYA / ASL / TAX`) -- read as the ROM reads it. C1/7313: `$AAFE = $AAFE / speed` (C1/00D7),
then `$AAFC >= $AAFE -> $AAFE = $AAFC - $AAFE, $AAFC = 0` else `$AAFC = $AAFE - $AAFC, $AAFE = 0`. Advance 1. Mode 0 (CF/F4B2):
done -> nothing; `$ABF6` zero -> done; else accumulators += steps, position = base + the high bytes, `DEC $ABF6` to done.

D7 `nn tt`: resolve(tt); the position of the combatant (track 4: nothing) or the object; C1/7468: C1/65BA, `nn >= the
dominant distance` (`$8C` picks $DE or $E0) -> `$5D9E/$5D9F = 0`; else `$80/$82 += the unit steps` nn times (`DEC $8E / BNE`,
8-bit: nn = 0 runs 256) and `$5D9E/$5D9F = the position + the high bytes`. Advance 3. Op 21: `LDA $5D9F / BEQ C1/56FB` --
the stored Y at 0 POPS back to the caller (C1/56FB, op 1F's body); else `LDA #01`. The Ice's `03 02 / D7 26 2D / 21 1A` loop
walks the crystal 38 px per turn toward the caster and leaves when the remaining distance is under 38.

### 6.4 What else had to be true

* **C1/762E modes 9-11 and 24-29.** `0C` = C1/764A: role (mode - 9)'s foot + `$9708,X` (8-bit) and `$9713,X` (negative: a
  borrow floors at 0; positive: a carry caps at FF) -- CC/E3C7/E3CA for a PC (0, F0), the E4/F604 record's +1/+2 for an enemy
  (CC/EBB0/EBB7); the port's `CtBattleCursorOfs` already held them. `24..29` = C1/780B: the point (wA * role 0 + wB * $A017)
  >> 3 per axis (C1/0089 x2, a 16-bit add, C1/011B = three LSRs), weights CC/FB7F[2k] / [2k+1] = 7/1 6/2 5/3 3/5 2/6 1/7 --
  the actor's `98 02 26` before the orbits went 3/8 of the way to the victim, which the port had left inert.
* **`$AE92`**: C1/BBA4 `LDA #02 / STA $AE92` at a tech's commit, C1/BFDA `#01` at a physical's -- the command type
  battleTargetRevalidate reads (`CMP #02` at C1/D63F); the port never wrote it at the dispatch.
* **The opcode-length scan's park precedence.** `battleAnimOpLenRom` returned the first `A9 nn 4C BB 75` tail; op D6's handler
  reaches C1/73A3 `LDA #00` (the park) before C1/73A8 `LDA #01` (the advance), so D6 read as length 0 and the cursor never
  moved past a finished wait. A scanned 0 now defers to the handler-keyed table.
* **The scratch objects' `$A240/$A250`.** Every ROM mover writes them; the port's field-kept movers (FWD, the teleports)
  now mirror the position there too (battleAscSetPos), so a D2/D3 arm reads the object where the ROM would.

### 6.5 The input residual: C0/1770's align wait (field.c, fieldBattleConsumer)

The check could only be absolute once the party's staged records matched. They did not: every `$29B0` record's fine X read 09
in the port against the cartridge's 0F (the orbit's 143 vs 137 centre). The field positions were identical; the difference was
the field CAMERA at the staging frame. C0/1770 (bit 5 of `$0117`, the D8's pulse): `LDA #01 / STA $38 / JSR C0/885A` arms the
align machine, then `JSR C0/885A / JSR C0/00BF / LDA $38 / BEQ / JSR C0/EC60 / BRA` glides the camera one pixel a frame until it
is 16-aligned, then C0/AF4E, `JSL FD/FFF7`, one more EC60 (its frame is spent inside the routine: the main loop's next VM
pass never happens), the style branch (0 -> RTS). Bit 0 (C0/188C, the staging) is reached only by the NEXT call, bit 5 being
clear (C0/1776). The main loop is C0/0097 `18D9 (-> 1770) / 0C76 / 881E (camera) / .. / 00BF (VM) / EC60`: the consumer runs at
the START of a frame and sees the op's bit a frame later. Measured: the 087 (the fixture is the cartridge mid-loop, `$0138 =
02`): C0/94A8 steps `$1D93` 06 -> 00 over f0-f6, C0:88DB clears `$0138` at f7, `$29B0` is staged at f9 (C0:0440), the song at
f12; the 0AD (already aligned): op f41 (C0:3AA8), `$0138` cleared f42, staged f44 = op + 3. The port's consumer (after the VM
pass) staged in the op's frame -- and, on the 087, then posted the battle song at f0 and FROZE 14 frames, which is why its
camera never glided. It now arms `$0138`, lets the next frame's camera tick take the first step (the same place relative to
the VM passes), waits a frame per step, spends the post-align frame and stages on the frame after; the records are
byte-identical (`02 07 0F 0E CB | 0D 07 0E 0F 39 | 08 0A 0F 00 D4`). Every D8-entered battle now stages op + 3 (the
battletest fixtures moved by 2-3 frames: bat9-return f2286 -> f2288, the ambush's shots, the demo Flea's frame).

### 6.6 Graded

`tools/zombor_hopslide_cmp.py`: the cartridge's --monitor of the movers' arrays over the leg against the port's
CT_BATTLE_M4TRACE, three movers each aligned on its own arm: slots 1-5 (D2) 46 frames x 5 objects -- positions (absolute),
modes, phases, heights, done, active and arrival latches identical (230/230 each); slot 0 (D3, mode 0) 33/33 frames; the actor
(D2 on a combatant, CF/F23D) 83/83 frames of feet, draw offsets, phases, done and arrival. The action now ends by its own
script (`end-action` at rel 395; the cartridge's at 424). Five mutations killed (a sixth, the 0x18 arrival window, is not
observable on this capture: the objects are already within 0x10 when they first fall).

## 7. Still open on this leg

* The script's own timing: the port reaches the D3 and the actor's D4/D6 one frame ahead of the cartridge (a `24 05`
  released a frame apart), and the whole action starts 8 frames early (the pre-roll: 30 frames from the dispatch to the
  first cursor move against the cartridge's 37); the action is 395 frames against 424.
* The effect program's op AA (D1/74F8): the bank-D1 runner stops on it (`fleeprog slot0 STOP unknown op AA`), on the 087 and
  on the Ice (whose action now ends by its script at 210 frames against the cartridge's 434 -- no Ice oracle state is in the
  repo; the three Ice checks are re-pinned as regression guards, not as correctness).
* The 0AD's detached BG2 counter (`$1D8B`) runs 5 steps behind the cartridge's before the D8 (the leg's own pre-battle
  timing), which is what the demo Flea's region check sees at its edges (1905/1920, BG pixels).

## 8. The effect program's op AA, the screen shake, the arena's line scroll and the white flash (2026-09-17)

Port: `battleShakeArm` / `battleShakeStop` (D1/F6B4 / D1/F692), `battleShakeFrame` (CD/0B51), `battleScrollFill` (CD/06B4),
the program op `case 0xAA` (CD/1C32), `battleObjSlotMode4` (FD/E82C, FD/E8D7), `CtBattleArenaScrollDelta` + field.c's
battle compose. Checks: `sweepshake-cd06b4-cd0b51` (`ct --sweepshake`), `sweeparenascroll-zombor-shake`
(`ct --sweeparenascroll`), `battle-fxprog-zombor-aa-mode4-shake` (tools/zombor_fxprog_cmp.py). Trace: `CT_BATTLE_SHAKETRACE=1`.

### 8.1 The script's `80 nn` request (C1/621B) and D1/F67A

`INY / LDA [$E6],Y / AND #0F / STA $80` then `LDA [$E6],Y / STA $53,X / INY / INX / DEC $80 / BNE`: the COUNT byte itself is
`$53` (its high nibble picks the handler) and the next n-1 bytes land in `$54..`. D1/F67A `LDA $53 / AND #F0 / LSR x3 / TAX /
JSR ($F686,X)`: six handlers -- 0-3 = F74D (the fade records, already ported), **4 = F6B4 (the shake arm), 5 = F692 (the
shake stop)**. The handlers read `$53..$57` from the WRAM, so a one-byte request (`80 51`) leaves `$54..` as the last one did;
the port had refused requests under two bytes.

### 8.2 The screen shake

D1/F692: `STZ $CCB0 / STZ $CCB8 / STZ $CCC0`. D1/F6B4 stops it and re-arms both axes:

```
X: $CCB3 = D1:F69C[($54 & 1C) >> 2]  (01 02 03 04 08 10 20 40: the re-roll period)   $CCB2 = 0
   $CCB5 = D1:F6A4[($54 & E0) >> 5]  (01 02 04 08 10 20 40 80: the amplitude-step period)   $CCB4 = 0
   $CCB6 = $55 >> 4 (the amplitude)   $CCB7 = $55 & 0F (its target)   $CCB0 = $54 & 3 (on)
Y: the same from $56/$57 into $CCBB $CCBA $CCBD $CCBC $CCBE $CCBF, $CCB8 = $56 & 3
$CCC0 = D1:F6AC[$54 & 3] | D1:F6B0[$56 & 3]   (00 28 A0 A8 | 00 14 50 54: bit 7..2 = BG1 X, BG1 Y, BG2 X, BG2 Y, BG3 X, BG3 Y)
```

The NMI's CD/0B51, per axis: `INC` the re-roll counter; at its period, zero it and draw the random byte from
`C0:FE00[A + $7C + 1]` (CD/0B16 `ADC $7C / TAX / LDA $C0FE00,X`, the carry the CMP set; A = the counter for X, `#40` for Y);
`$4202` = the random byte; `INC` the amplitude counter; at its period the amplitude steps toward its target (a DEC that reaches
0 turns the axis off); `$4203` = the amplitude; the offset is `$4217` (random x amplitude >> 8). An axis that is off zeroes its
three layers' pairs. CD/0C19-0C45: `$CCC0`'s bits hand the X offset to `$CCA4/A8/AC` and the Y offset to `$CCA6/AA/AE`
(16-bit, high byte 0). Then CD/0C48 (the fade-record commit, ported earlier -- its bit-7 copy is **12** bytes, `LDA #000B`;
the port had 24). The attract's Zombor: `80 45 E3 30 00 00` at the impact ($CCC0 = A8: all three layers, X only, amplitude
3 walking down every 128 frames), `80 51` 39 frames later.

### 8.3 CD/06B4: the per-line scroll records, every NMI

CD/0929 `JSR CD/06B4` (right before CD/0B51; the NMI's short path CD/0902 skips both while `$CD02`, a system box, is set):
`LDA $CA92 / ASL / TAX / JMP ($06BC,X)` -- 0 CD/06C4, 1 CD/076C, 2 CD/07E2, 3 CD/07B9; A 16-bit, X 8-bit.

```
0: BG1 = $2C70/$2C72 + $CCA4/$CCA6 + $BB01/$BB03   BG2 = $2C74/$2C76 + $CCA8/$CCAA   BG3 = $2C78/$2C7A + $CCAC/$CCAE
1: as 0, but BG3 = $CA5A + $CA5E + $CCAC / $CA5C + $CA60 + $CCAE   (the effect program slot 0's anchor + offset)
   CD/0706: X = 0..7C by 4 -- $BC89/$BC8B, $BD09/$BD0B, $BD89/$BD8B, 32 records each
2: the values of 1; per record BG1/BG2/BG3 H = $0A00,X + the value when $CC91/$CC93/$CC95 (a line wave)
3: 2, then BG3's records 32..63 the same way (X 80..FC; the 8-bit X wraps the loop)
```

`$CA92` is the runner's (CD/161A: 0 for `$CCA2` in {86, 7B..7F}, else 1, only while the runner is live) or a program op's
(CD/1880, 25BC, 262D). The staging's one-time fill (CD/0CC2) is the same records with the shake at 0.

### 8.4 The arena picture

The NMI's CD/09F1 points HDMA channel 1 at `$210D` (table `$C894`) and channel 2 at `$210F` (`$C8C1`), mode 43 (indirect,
four bytes a line; each scroll register written twice, so after a transfer it holds the record's 10-bit word whatever the
PPU's shared latch held). The port draws the arena from the field picture at the D8's camera, which is the view of the staged
values `$2C70..$2C76`; `CtBattleArenaScrollDelta` walks the two channels as dma.c does and returns each fb row's (H, V) minus
those, and field.c's battle compose samples the row that much further (the fill and the foreground pass alike). The field
canvas holds both layers in one picture, so a row whose BG1 and BG2 would move apart keeps the staged view and is reported
once (no capture has one: the Zombor's A8 moves all three layers together). Only rows whose last transfer came
from CD/06B4's record blocks ($BC89..$BD08 for BG1, $BD09..$BD88 for BG2) are the arena's: the HUD band's lines read other
records (a 07D-leg band row reads (0, +231)) and the port draws that band itself, so those rows keep the staged view. Only
while the battle module runs (the interactive battle): the headless --battletest shot is composed after its synchronous run. BG3 already went through its channel (the effect
compositor), so the anchor and the shake reach it by the records.

### 8.5 Program op AA (CD/1C32) and objslot mode 4 (FD/E82C)

AA k: `LDA [$40] / ASL / TAY / JSR CD/2436` (CD/2433 with X = 0: bound points 0 and 1 clamped in place, x 08..F8, y 08..D4),
then `$CA5A,X = -($5DA0,Y & FF)`, `$CA5C,X = -($5DA1,Y & FF)` (16-bit). The operand stays for the runner's post-INY. The
Zombor's program 4D: `AA 00 / 82 7F / 82 7F / 82 DF / 82 D7 / 93 13 02 04 / 7F / 9D 17 / 83 40 00 / 84 8E 50 19 03 / FF` --
the anchor FF78/FFC7 (the victim's centre 88/39), the offset stepped to 0040/0038, and a 64-frame loop.

Its continuation (D1/A208) waits in `EF / 7F / BF 04` -- op BF branches back while an objslot record 8..11 lives -- and the
records were the script's `80 12 B0` flash: objslot MODE 4. FD/E39C dispatches `$0520,Y >> 4` by DECs: 1 E437, 2 E532,
3 E4E8, **4 E82C**, 5 E5A4, 6 E807, 7 E57C, else E485. FD/E82C walks the acc as mode 5 does with the exits swapped (a borrow
below 0 -> E8AC: off, the base page `$7E2000` copied back to `$7E2200`, no acc store; a carry past FF -> off at the limit),
then `STA $4202` and the sub-mode `$0526`: 0 = FD/E8D7, 1 E962 (red), 2 E98F (green), 3 EA5C, 4 E935 (blue), 5 E9CA, else
EA11. FD/E8D7 moves each component toward white: B `AND #7C / STA $0D / EOR #7C / STA $4203` + `$4217`, R the same on
`AND #1F`, G on `((w & 03E0) >> 2)` with `EOR #F8 / AND #F8`, masked back with `AND #7C1F` and `ASL ASL` -- per 5-bit component
exactly `c + ((acc * (31 - c)) >> 8)` (mode 5's FD/E64F is exactly `(acc * c) >> 8`). The Zombor: records 8/9 over CGRAM
$10..$7F, acc 80 down by 8 every second frame -- a white flash of the arena. The arena composite takes it through the
per-palette factor with its new kind (`h->palKind`). Sub-modes 1-6 are PORTED 2026-09-30 (objslot.c objslotWhiteSub, graded
by `sweepbganim-white`, 4000/4000 against FD/E39C): each moves its components toward white as E8D7 does and writes the
live page from the base byte for byte as the ROM stores -- 1 E962 R into the LOW byte only (`(lo & E0) | R'`), 2 E98F G,
3 EA5C R and G with the word's blue ZEROED (`AND #001F`: Arc Impulse's yellow arena, T68 records 8/9 `40 10 38 .. 03`), 4
E935 B into the HIGH byte only (`(hi & 03) | B'`), 5 E9CA R and B (G kept), 6+ EA11 B and G with red ZEROED (`AND #7C00`).
Fire Sword 2 (T3E) runs sub-mode 1 over CGRAM $10-$7F (the red arena). The records' tick and accumulator were already the
cartridge's (T68: $0580/$058C equal field for field, one frame early with the whole action); only the page write was
missing. (Mode 4's. MODE 5's six other arms -- FD/E5ED: 1 E7C0 B+G, 2 E73A B+R, 3 E6A1 B, 4 E779 R+G, 5 E6D1 G, else E708 R, each scaling only its components by `(acc * c) >> 8` -- are ported 2026-09-26, objslot.c CtObjSlotSubMask, graded by sweep-bganim-machines.)

### 8.6 Graded

* `ct --sweepshake`: CD/06B4 + CD/0B51 micro-executed on 6000 seeded states against the port -- 6000/6000 byte-exact over
  `$CCA4..$CCF0`, the records and the commit's targets; every branch live.
* `ct --sweeparenascroll saves/attract_cena_inicial.state 7721 7885`: the oracle PPU's BG1/BG2 scrolls sampled on every line
  against the port's deltas over the Zombor's two shakes -- 36960 rows, all four registers identical, 12144 rows shifted.
* tools/zombor_fxprog_cmp.py: the program from AA 135/135 frames (slot, pointer, anchor + offset) and the slot ends on both
  sides; the fade records 34/34 from their start; the shake arm identical. The script's own timing shows in its last line
  (the stop 39 frames after the arm on the cartridge, 32 on the port) -- with the 087 pre-roll, still open.
* The trace `CT_BATTLE_SHAKETRACE` samples at the tick's end (after the script, which runs inside the body): a trace taken
  before the script misplaces every script-side event by a frame.

## 9. The 07D Frog Squash: BG3 priority and the colour-ramp rows (2026-09-17)

Check: `attract-07d-frog-squash-bg3-priority-vs-oracle`. Survey: the 07D leg's worst samples were tech $28's giant rainbow
frog -- drawn by the cartridge, absent from the port.

* The machinery was already exact: from program 4B's arm (D1/8425) 516 frames of both slots' flags, pointers, anchors and
  offsets and of the colour-ramp records `$BB05..` identical; the frog's BG3 map (VRAM $7C00) and tiles ($5000) byte-identical
  to the cartridge's at the frog frame; the per-line registers (TM=17, CGADSUB=04, the COLDATA gradient) too.
* **Row ownership.** Channel 5 (CGWSEL CGADSUB COLDATA SETINI) of the frog's lines points into the per-line records a program's
  `80 00`/`80 10` builds at `$BB05..$BC84` (four bytes a line): oracle f5041 `88 05 BB | B0 2F C0 | A8 E5 BB | E0 05 BB | 94 05
  BB`. The compositor took a row as the effect's only for `$BE95`; the ramp block is the effect's too.
* **BG3 priority.** ppu.c's `ppu_getPixel`: mode 1 with `$2105` bit 3 is actMode 8, whose layer list starts with BG3 priority 1
  -- BG3's priority tiles (map word bit 13) are in front of BG1/BG2 and of every OBJ; BG3's other tiles come last. The frog's
  tiles carry the bit (`02 38`). The compositor let BG3 through only on backdrop pixels; now a priority pixel wins over the
  arena and is marked in `gBattleFxFront`, and field.c puts those pixels back after the combatants, the effect sprites and the
  popups (before the results box).
* Graded at the aligned frame (host = battle + 6547 on the attract's 07D battle; port f7081, cartridge f5042): the frog's lower
  body, x 60..190 rows 138..168, 4030/4030.

Open, measured:

* **The ramp race.** The records rotate by one each frame (CD/2D1F `LDA $BB0B,X / STA $BB07,X`), and the runner does it
  WHILE the frame is being drawn. MEASURED 2026-09-17 (`--hookpc CD2D28` from a PEEK_SAVE state at attract f5000):
  **95 visits a frame** -- the loop's own `CPX #017C` / 4 -- spanning **vPos 128 through 137**, every frame of the frog.
  So the HDMA of scanlines 1..128 has already read the table when the sweep starts.
  ⚠ TRIED AND REVERTED 2026-09-17: snapshotting `$BB05..$BC84` before the rotation and letting `CtBattleFxLayerCompose`
  read it for rows below scanline 128 changes NOTHING measurable -- the graded frame's upper body (box (60,60)-(190,128),
  port f7081 vs cartridge f5042) is **446 / 8840 identical either way**. The per-line COLDATA does vary down the frame
  (`CT_BATTLE_FXLINES=534`: fix = (0,18,0) at line 1, (24,24,0) at 100, (16,0,0) at 128, (20,0,0) at 140), so the records
  reach those lines; what the split does not explain is why the upper body is that far off. The next pass should compare
  the port's per-line COLDATA against the cartridge's (`--hdma` on a state at the frog) before touching the compositor. The battle's V-IRQ is line 211 (D1/F29A `LDA #D3 / STA
  $4209`), so 128 is the main loop's CPU time before the runner -- a raster clock the port does not have. The port colours
  every row from the rotated records (the frog's upper body differs; `ct --sweepfxcompose` counts those rows).
* ~~**The frog's eyes are OBJ**~~ -- PORTED 2026-09-17: the OBJ sheets (CD/1368), the frame table `$4500` (C1/486A), anim op 60 (the palette) and the program's sprite cloud (op 80 09) -- battle_fx_sprites.md, check `attract-07d-frog-squash-eyes-vs-oracle` (882/882 visible pixels).
* **The enemies turn white** beside the frog: an OBJ-side effect, not derived.

## 10. The 087 Ice Sword crystal: BG3 in front, ADDED to the sub screen (2026-09-19)

USER REPORT (translated from Portuguese) "in the battle against zombor, the sprite does not appear in the ice skill's animation". The crystal is the effect's BG3
(map `$7C00`, sheets `$5000`). The port's map, tiles and per-line registers were already identical to the cartridge's.
Cartridge, `--ppuregs` at attract f7760: `addSub=1 TM=11101 TS=11001`. Port, `CT_BATTLE_FXLINES`: BGMODE `$49` (mode 1,
`$2105` bit 3, 16x16 BG3 tiles), TM 17, TS 13, TMW = TSW = 17, CGWSEL 02, CGADSUB 04, COLDATA 0 on lines 1-160.

* **The line was thrown away.** The compositor refused any line with `addSub && (TS & 0x1B)` (from 9124c31, the X-Strike),
  i.e. a sub screen with BG1/BG2/OBJ, because the port has no separate sub-screen picture. The crystal's lines are exactly
  that.
* **This sub screen is the main screen less BG3.** `ppu_getPixel(sub)` walks the same actMode-8 layer list with BG3 off.
  When TS's BG1/BG2/BG4/OBJ bits and window masks equal TM's, BG3 is not on the sub screen, and only BG3 has math (CGADSUB &
  3B = 0), the sub-screen pixel under a BG3 priority pixel is the picture the main screen shows without BG3: the arena, and
  the sprites under the crystal. ppu_handlePixel then does `r += r2` (subtract/half as CGADSUB says; a backdrop sub pixel
  is layer 5 -> COLDATA, no halving).
* **The sprites come after the compositor** in the port. So such a pixel is left alone and marked 2 in `gBattleFxFront`,
  with its colour and math in `gBattleFxFrontMath`. `CtBattleFxFrontApply` (field.c, after the combatants, cloud,
  cursors and popups) finishes it against the pixel under it. The pixel counts as the backdrop when the arena has no
  BG1/BG2 there and nothing was drawn over it since the compositor ran.
* **The grading harness had BG3 in its base.** `--sweepfxcompose` is documented as "main screen without BG3 and OBJ", but
  ppu.c's `g_ppuFxBase` only switched OBJ off. So a BG3 on the MAIN screen was already in the base, math-less, and read
  as the arena's pixel. It now switches BG3 off too. The X-Strike check (BG3 on the sub screen only) is unchanged.

Graded:
* `ct --sweepfxcompose saves/attract_087_ice_f7700.state 0 150`: 0 / 6281611 non-sprite pixels differ, and the effect
  changes 270567. That is the crystal's whole life, cartridge f7700-f7850, from the cartridge's own RAM, VRAM and window
  registers.
* The port's picture: cartridge f7800 against port f9761, box (88,8)-(168,136), 9237 / 10240. The rest is Zombor's pose
  under the crystal and the sea's sparkle, not the crystal (build/_ice/diff_7800_9761.png).

Open, measured: the Ice Sword's DAMAGE. The ROM stages the dual tech's two effects as two HITS, `$B328 + tgt*4 + $B2C7*$2C`
(C1/EC10-EC2C): Marle's then Crono's base, 194 and 161 (C1/DCED, oracle f7452). Extra effect `$B2CC = 08` ->
CC:2A05 = `0B 05 04` -> routine 11 = C1/EFBC `JSR C1/EDBA / JSR C1/EF39` (x5/4, then the element, neutral here), which
gives 242 and 201. The port sums the two effects into one number and has no routine 11, so it shows 355. The popups and the
staging belong with the Frog Squash item.

## 11. The sprites under the colour math (2026-09-30)

The compositor worked on the arena only; the port draws the sprites after it, so no OBJ pixel ever took the rows' math.
ppu_handlePixel runs on whatever the main screen shows: the CLIP (CGWSEL bits 6-7 against the colour window) blacks an OBJ
pixel like any other, and the MATH applies to layer 4 -- an OBJ pixel of palette 4-7 (`if(layer == 4 && pixel < 0xc0)
layer = 6`, and layer 6 has no CGADSUB bit) -- when CGADSUB bit 4 is set and CGWSEL bits 4-5 do not prevent it. On a row
the compositor owns, the sub screen is BG3 alone or nothing (a row whose TS carries BG1/BG2/OBJ with the sub screen added is
not modelled, and `subRest` has CGADSUB & 3B = 0), so the added pixel is BG3's through TSW, else the fixed colour (no
halving). No PC action's program has a window on OBJ (op 96's WOBJSEL operands 00 20 30 A0 F0: bits 0-3 always 0).

The palette is the OAM attribute's (bits 1-3). field.c's `plot` stamps each pixel with `g_bObjPalCur` while the battle
compose records: a combatant's cells take `$993C + cmb` as the compose read it (CF/EB1E `$37 = $9930,X | $99A9,X`,
h->objPalShown -- op C9's `EOR #08` moves Crono to palette 1 and GATO to 5 in Lightning 2), an effect object or a cloud
entry its own attribute, the shadow `$A5E3 | $9F46`, the status icon its record's, Marle's spark 0, the popups 0 / 4 (attr
31 / 39), the bracket its entry's, the hand 1; a non-sprite write (the foliage's BG priority tiles) clears it.
`CtBattleFxObjMath` runs last, after CtBattleFxFrontApply (BG3's front pixels are not the sprite's), from the per-row
registers the compose kept.

Cases, measured: Lightning 2 (Crono k=4): `$BE95` = `10 17 6n` -- the fixed colour (n,n,0), n 0 -> 1F -> 0 over c930-c1067,
added inside the colour window (W1 xor W2, both inverted) to BG1/BG2/BG3 and GATO; Heal (Frog k=3, c985-c1114): CGWSEL 02,
CGADSUB 10, TS 04 -- BG3's wave added to Frog alone; Aura Whirl (T39): `$BE95` 00 14 and 80.13's records (CGADSUB 14).

Graded (`ct --sweepfxcompose`, its new `sweepfxcompose-obj` line: the real frame's main layer and CGRAM index per pixel,
`g_ppuMainPix`, stands in for the sprite pass; the OBJ colour is pasted raw, then FrontApply and ObjMath run as field.c runs
them, and every OBJ pixel of a modelled row must equal the real frame):
* saves/battle_re/fx_lightning2_c920.state 0 160: 0 / 267981 sprite px differ, the math changed 42379 of them;
* saves/battle_re/fx_frog_heal_c975.state 0 150: 0 / 331167, the math changed 32415;
* the X-Strike and the Ice Sword states: 0 / 193570 and 0 / 318160 (no OBJ math there).
The same line also counts each drawn row's CGWSEL / CGADSUB / COLDATA against the PPU's own fields (`g_ppuLine*`): 0 differ
on all four states.

Two things the harness met that are not the compositor's:
* **The V-IRQ's forced blank.** D1/F29A `LDA #D3 / STA $4209`: the battle's IRQ D1/FE97 fires at line 211 and sets INIDISP
  = 80 at its hblank for its uploads, so lines 212.. draw nothing (ppu_handlePixel skips ppu_getPixel). field.c already
  blacks those rows after the compose; the harness's base is black there for the same reason and the compositor's BG3 on
  that "backdrop" is not compared (the new line excludes rows with no drawn pixel; the first line is left as it was pinned).
* **Records written mid-frame (a CPU-timing class).** The program service CD/1609 runs at vPos ~64 (the logical oracle,
  `--hookpc CD1609` in the combo 39 drive), 80.13 at vPos 84, and HDMA channel 5's per-line records ($BB05..) have no
  latch: the cartridge shows the new records from the write's line on and the old ones above it, until the next frame.
  The compositor (like zelda3's whole-frame render) reads the records after the frame's logic. Aura Whirl
  (saves/battle_re/fx_aura_whirl_c955.state 0 200): 3 frames, 1057 / 457654 sprite px, and 5262 / 32763 rows' registers
  over the COLDATA ramp -- the port's rows one frame ahead above the write line (f108 = c1063: CGADSUB 14 against the PPU's
  04 on rows 0-98, 14 from row 99; the ramp one line up). Every non-sprite pixel still matches (no BG3 under those rows).
  Measured, not modelled -- OPEN: the write line is where the CPU stands in the frame (a CPU-timing question of the kind
  CLAUDE.md's zelda3 section is about, but not one the rule as written decides: it names beam-counter reads, lag frames
  and NMI-off loads only). A decision is the user's.
