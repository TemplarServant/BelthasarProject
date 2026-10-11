# FIELD — treasure chests: a table in bank $F5, a plane at $7E:70C0, and the A handler's fall-through

User report (translated from Portuguese): *"chests do not open"*. That is not a defect in a ported routine — **the entire chest subsystem was
absent from `src/`**. A chest is not an event object and uses no event opcode (op `0xCA` AddItem has **220**
script sites for CT's **249** treasures, and every site sampled is a hidden Tab/key-item pickup, not a chest);
it is a dedicated engine subsystem with four parts, all running with **DP = `$0100`**.

Ported in this pass: `src/field.c` (the table reader, the probe, the award, the message) and `src/scene.c`
(`CtFieldSceneChestOpen` = the chest arm of C0/16DC, beside the door arm we already had).

---

## 1. The data — `$F5:F000`

| what | where |
|---|---|
| pointer table | `$F5:F000`, **513 u16** LOCAL pointers. `ptr[loc]` .. `ptr[loc+1]` bound the location's records. |
| global record base | `ptr[0]` = `$F402` = `$F000 + 513*2`, kept in `$1D06` (`A73C LDA $F5F000 / STA $06`). |
| record | **4 bytes**: `u16 tileIndex = (tileY << 8) \| tileX`, `u16 payload`. |
| payload | bit15 → **GOLD** = `payload << 1`; else bit14 → **award nothing** (0 such records ship); else **ITEM** = `payload & 0x01FF`. |
| totals | **249 records over 118 locations, 16 gold, 5 alias records** (`word0 == 0` → `word1` is a *location* to borrow: 109→302, 300→297, 303→110, 490→305, 495→245). |

Samples read straight from the ROM: `0x35F41A` (loc 26) `1E 25 CA 00 | 1A 27 C4 00 | 1B 27 C3 00`;
`0x35F40E` (loc 14) `29 08 32 80`; `0x35F42E` (loc 47) `0C 0B 99 00`.

## 2. The plane — `C0/A712` (DP = `$1D00`; callers `C0/033C` and `C0/0A59`, the two location loads)

```
A712 LDA #80 / LDX #0000 / STA $7E70C0,X / INX / CPX #0040 / BCC ->A717   ; first row
A721 …MVN $7E,$7E row-doubling up to $7E:AFFF                            ; 64 rows x $40, stride $100
A73C LDA $F5F000 / STA $06                                               ; $1D06 = the global base
A742 LDA $0100 / AND #01FF / [A748] ASL A / TAX
A74A LDA $F5F002,X / STA $56 ; LDA $F5F000,X / CMP $56 / BEQ ->A7B9 ; STA $08
A75A STX $50 / TAX / A75D LDA $F50000,X / BNE ->A769 / A763 LDA $F50002,X / BRA ->A748   ; ALIAS
A769 LDX $08 / STZ $54
A76D CPX $56 / BCS ->A7B9
A771 LDA $F50000,X / STX $52 / AND $1E / TAX / SEP #20 / LDA $54 / STA $7E70C0,X   ; plane[tile] = SLOT
A782 JSR C0/A7BC / BCC ->A7A9                                            ; already opened?
A787 LDA $7E30C0,X / BIT #01 / BEQ ->A7A9
A78F LDA $7E3000,X / CMP #FE / BEQ ->A7A3 / CMP #E0 / BNE ->A7A9
A79B LDA #E1 / STA $7E3000,X   |   A7A3 LDA #FF / STA $7E3000,X          ; PRE-BUMP the metatile
A7A9 LDA $54 / INC A / STA $54 / LDA $52 / CLC / ADC #0004 / TAX / BRA ->A76D
```

`C0/A7BC` is *isChestOpened(recordOffset)* — the same flag arithmetic as the award below, returning SEC when
the bit is set.

⚠ **`$7E:70C0` is the TREASURE plane, not an "off-map sentinel".** The only two ROM writers of it are inside
`C0/A712` (`0x00A717`, `0x00A77E`), and a `--peek` of the whole 64×64 plane in loc 26 has **exactly three**
non-`$80` cells — `(30,37)=00 (26,39)=01 (27,39)=02`, the three records in slot order. Off-map is done by the
`$1D1E/$1D1F` mask. The behaviour coincides with the old label (`C0/9AA1 LDA $7E70C0,X / BMI ->9AB2 / SEC`
returns SOLID for a cell whose bit7 is clear, so a chest tile is impassable *because of the plane*), which is
why the mislabel survived. Corrected in `collision.md` and `src/collision.h` / `src/collision.c`.

## 3. The probe — `C0/1DF4`, reached by `C0/1ADC BRL ->1DF4`

```
1AAC LDA $00F6 / BIT #80 / BNE ->1AB4 / RTS      ; A pressed
1AB4 LDX $34 / …                                 ; the menu-exit swallow ($0134 == $FFFF)
1AC1 LDA $1F / BNE ->1AC6 / RTS                  ; $011F != 0 = player control
1AD0 JSR C0/1CFC / BCC ->1ADC / JSR C0/5AC5      ; the OBJECT probe, then the INVOKE
1ADC BRL ->1DF4                                  ; <<<< THE TREASURE PROBE — the FALL-THROUGH
1DF4 LDA $29 / BNE ->1E09                        ; $0129: the message box must be idle
1DF8 LDX $97 / LDA $1600,X / ASL A / AND #0006 / TAX / JSR ($1E92,X)
```

`C0/1E92` = `9A 1E C7 1E E0 1E F7 1E`. Each handler builds `X = ($1881,player << 8) | $1801,player` stepped
one tile and reads the plane; **carry SET (proceed) means the plane byte's bit7 is CLEAR**, and `$01DB` keeps
the probed tile:

| facing | handler | probes |
|---|---|---|
| 0 UP | `C0/1E9A` → `C0/1EB1` | `(x, y-1)`, then **`(x, y-2)`** |
| 1 DOWN | `C0/1EC7` | `(x, y+1)` |
| 2 LEFT | `C0/1EE0` | `(x-1, y)` |
| 3 RIGHT | `C0/1EF7` | `(x+1, y)` |

Measured: `(30,39)` facing UP opens the chest at `(30,37)` (the two-tile reach); `(26,38)` facing DOWN opens
`(26,39)`; `(26,37)` facing DOWN opens nothing.

⚠ **`disasm/notes/field_action_probe.md:26` was wrong**: it annotates `1AD3 BCC ->1ADC` as *"carry clear = no
target → nothing happens"*. `1ADC` is a `BRL` into the treasure probe, so the probe runs on **every** A press,
whether or not an object answered. That single mis-read is why the port had no chests at all.

## 4. The award — `C0/1E0C`

```
1E0C AND #007F / ASL A / ASL A / CLC / ADC $1D08 / TAX     ; slot*4 + this location's base
1E18 LDA $F50000,X / CMP $DB / BNE ->1E09                  ; the record must sit on the probed tile
1E20 STX $DD / TXA / SEC / SBC $1D06 / LSR / LSR / STA $D9 ; the GLOBAL chest index
1E2B LSR/LSR/LSR / AND #003F / TAX                         ; flag byte = idx>>3
1E34 LDA $D9 / AND #07 / TAY / LDA $FF20,Y / STA $D9       ; C0/FF20 = 01 02 04 08 10 20 40 80
1E3E LDA $7F0001,X / BIT $D9 / BNE ->1E8D                  ; already open -> result 2
1E46 ORA $D9 / STA $7F0001,X                               ; SET IT
1E4C LDA #02 / TSB $17 / LDX $DB / STX $5D                 ; ask C0/16DC to bump the metatile
1E58 LDA $F50002,X
1E5C BMI ->1E7B                                            ; bit15 -> GOLD
1E5E BIT #4000 / BEQ ->1E66 / SEP #20 / RTS                ; bit14 -> nothing, and NO box
1E66 AND #01FF / STA $7F0200 / TAY / LDA #01 / JSL C1/8003 ; ITEM  -> result 0
1E7B ASL A / STA $0200 / TAY / LDA #04 / JSL C1/8003       ; GOLD  -> result 1
1F0E STA $2A / LDA $97 / STA $2E / STZ $32 / LDA #01 / STA $29 / STZ $30 / LDA #20 / TSB $54 / STZ $02A1
```

**Flags: `$7F:0001 + ((idx>>3) & 0x3F)`, bit `1 << (idx & 7)` → 249 chests = `$7F:0001..$7F:0020`**, directly
under the script globals. `JSL C1/8003` is the same trampoline the script ops `0xCA` (AddItem, `C0/37B1`) and
`0xCD` (AddGold, `C0/385B`) use — `C1/8003 → C1/CFC2 → JSR ($D126,X)`, leaf 1 = `C1/D005` (`STA $2400,X`),
leaf 4 = `C1/D0A2` (`STA $2C53`). One leaf, two callers.

## 5. The metatile bump — the *other* half of `C0/16DC`

The door dispatcher `C0/0D4C` falls straight into it (`C0/0D75 BRL ->16DC`), which is why our
`CtFieldSceneDoorOpen` used to end with `return 0; // C0/16DC: not a door metatile` — the routine it named
*is* the chest arm.

```
16DC LDA $17 / BIT #02 / BNE ->16E5 / BRL ->1770     ; the $0117 bit1 request from C0/1E4C
16E5 STZ $61 / LDA #02 / TRB $17 / JSR C0/1BA7
16EE LDX $5D / LDA $7E3000,X
16F4 CMP #FE / BEQ ->1700 ; INC $61 ; CMP #E0 / BEQ ->1700 ; BRA ->1770
1700 INC A / STA $7E3000,X / [1705..] re-upload the metatile to VRAM
```

Exactly **two** closed ids ship, `$FE` and `$E0`, and both are live (`$FE` at locs 13/22/26/30/42/71/112/130/
131/142/144/23/51/72/110/121/140/145/167/179/209/229/256/313/344/392/468/486…, `$E0` at 47/48/49/160/161/195/
285…). A sweep also found loc **437** = `$E5` and locs **0**/**12** = `$00` — records whose metatile is in
neither family, where both compares miss and the bump is simply inert. `$0161` (0 for `$FE`, 1 for `$E0`) is
read by the SFX/animation arm and has no map effect.

## 6. The box

`C0/1F87` is a 13-state machine on `$0129` called from the field frame loop at `C0/00AD JSL C0/1F87`. State 1
(`C0/1FB4`) runs `C0/20B2` — an `MVN` chain that fills `$7E:F000..$F3FF` with a repeat of 32 bytes from
`$D2:0000` (a scratch tilemap, **not** `..$F5FF`) — and state 5 runs `C0/20F2`:

```
20F2 LDA $2A / STA $020C                       ; the RESULT CODE selects the message
20F7 LDA $54 / BIT #20 / BEQ ->2107            ; $0154 bit5 -- set by C0/1F0E for a treasure
20FD LDX #FF00 / STX $020D / LDA #DE           ;   -> the pointer table $DE:FF00
2107 LDX $2B / STX $020D / LDA $2D             ;   (else the caller's own string)
210E STA $020F / LDX #F000 / STX $0210 / LDA #7E / STA $0212 / LDA #00 / STA $0214
2121 JSL C2/0003
```

**`$DE:FF00` is a 5-entry u16 pointer table** (`0A FF 21 FF 37 FF 52 FF 52 FF`) into bank `$DE`:

| result | string | bytes |
|---|---|---|
| 0 item | `"Got 1 {item}!"` | `06 EF×13 A6 C8 CD EF D5 EF 1F DE 00` |
| 1 gold | `"Found {num}G!"` | `06 EF×13 A5 24 42 EF 0E A6 DE 00` |
| 2 open | `"Empty!"` | `06 EF×19 A4 C6 C9 CD D2 DE 00` |

The leading `06` (newline + 3 spaces) is why the text sits on the window's **second** line, and the run of
`EF` spaces **is** the centring. `24`/`42` are dictionary refs (`$DE:FA00`, 128 substrings: `dict[3]="ou"`,
`dict[0x21]="nd"` → `F`+`ou`+`nd`). The port decodes all of this from the ROM at runtime — nothing baked.

## 7. What the port does

* `fieldTreasureList()` = §1 + the alias re-index; `fieldTreasureOpened/SetOpened()` = the `$7F:0001` bank;
  `fieldAddItem/fieldAddGold()` = the two `C1/8003` leaves; `fieldTreasureMessage()` = §6's decode.
* `fieldTreasureProbe()` = §3 + §4, called from the A-press path as the ROM's fall-through, gated on
  `$0129 == 0` and `$011F != 0` exactly as `C0/1DF4`/`C0/1AC1` are.
* The bump is requested the ROM's way — `$0117` bit1 + `$015D` — and consumed in the field main loop right
  beside the door's `$0117` bit4 consumer, sharing its re-bake.
* `fieldLoad()` runs §2's pre-bump for every already-opened record before the first bake.
* The box rides the field textbox: same window bitmap, same auto position, same open/close ramp and
  typewriter, with the host owning the string and the dismiss (`$0129`/`$0154` bit5 are set and cleared with
  it, so the probe's own gate behaves).

### Verified against the oracle

| case | oracle | ours |
|---|---|---|
| loc 26, item | f9 `$7F0001=40` (C0:1E4C), `$7E2400=CA` (C1:D03D), `$0117=02` (C0:1E50), `$015D=1E25`; f10 `$7E551E FE→FF` (C0:1705); box "Got 1 Lapis!" | `idx 6 payload 00CA -> result 0 item 202`, `bumped=1`, box "Got 1 Lapis!" |
| loc 14, gold | `$7F0001=08`, `$2C53 0x24E→0x2B2` (+100, C1:D0B6), `$7E3829 FE→FF`; box "Found 100G!" | `idx 3 payload 8032 -> result 1 gold 100`, box "Found 100G!" |
| loc 47, `$E0` | `$7F0002=08` (idx 11), item `$99`, `$7E3B0C E0→E1`, `$0161=01` | `idx 11 payload 0099 -> result 0 item 153`, `bumped=1` |
| re-press | no flag / item / tile write at all, box "Empty!" | `result 2 item 0 gold 0 msg 'Empty!'`, identical frame |
| control | the player **can walk** with the box up (`$1802` advances through `$0129` = 6/7) | the box takes no control lock |

The window geometry matches the oracle **exactly** (body top y=8 in 224-space, gradient steps 13/18/23/…/83,
end 83) and the text's left edge matches **exactly** (ink starts at x=72 in both).

Checks: `field-chest-opens-item`, `field-chest-gold`, `field-chest-already-open-is-empty`,
`field-chest-preopened-at-load`, `field-chest-e0-metatile-family` (all five mutations killed). Debug surface:
`CT_CHESTDBG=1`.

## 8. OPEN / boundaries

* **The text baseline is 1 px high.** Ours renders the message's ink at y 33..42 (224-space) where the oracle
  renders it at 34..43; x and the whole window match to the pixel. The two boxes are drawn by *different* ROM
  renderers (`C2/0003` for the treasure box, the field's own for dialogue) and our box borrows the field's, so
  this is a renderer difference, not a tuned constant. Settling it needs `C2/0003`'s own text origin — do not
  "fix" it by nudging `TB_*`.
* **The pre-bump's `$7E30C0` bit0 gate (`A78B`) is not portable today.** Our packed collision asset carries
  byte0 as `shp << 2` and drops bits 0-1 (the tile-priority pair), so `CtFieldSceneChestOpen`'s id test stands
  alone. Every shipped chest tile has bit0 set (loc 26's three are all `$7E30C0 = 0x07`), so the selection is
  the same; re-derive if a byte0 plane is ever packed.
* **The plane is not wired into collision.** The ROM makes every chest tile impassable through
  `C0/9AA1`'s bit7 test on `$7E:70C0`; our interactive `fieldTileFetch` has no such plane, and the measured
  chest tiles are already solid from their own props (`$7E30C0 = 0x07` = shape 1). Wiring it in is a global
  collision change and wants its own 512-scene pass.
* **Not derived:** the chest-open SFX (`C0/1BA7`, `$015F` bit4 at `16D8 LDA #10 / TSB $5F`), the
  `$0215` exit codes of the `C2/0003` request, and the VRAM re-upload at `C0/1705..` (our re-bake stands in).
* `C0/20F2` writes `$020C, $020D/E, $020F, $0210/11, $0212, $0214` — **not** `$0213`.

## The plane IS the collision (2026-09-03; defect #3, 'some chests do not open')

Section 8 above listed the treasure plane as not wired into collision, on the assumption that the chest
tiles are already solid from their own props. That assumption is FALSE for 89 of the 256 shipped records,
and it is the whole of defect #3.

The two prop-fetch entries differ in exactly one test (`python tools/dis.py 9A80 0x60 0 0`):

```
9AA1  REP #10 / REP #20 / AND $1E / SEP #20 / TAX
9AAA  LDA $7E70C0,X        ; THE TREASURE PLANE
9AAE  BMI ->9AB2           ; bit7 SET  = no treasure -> fall through to the props
9AB0  SEC / RTS            ; bit7 CLEAR = a treasure sits here -> HARD SOLID (A = 1, like off-map)
9AB2  LDA $7E30C0,X / STA $58 / LDA $7E7000,X / STA $5A / LDA $7E7040,X / STA $5C / CLC / RTS
9AC8  REP #10 / REP #20 / AND $1E / SEP #20 / TAX / BRA ->9AB2      ; the SAME fetch WITHOUT the test
```

and they have disjoint callers: 9AA1 has exactly ONE, C0/8AA4 inside the player feeler helper C0/8A9E
(which the eight direction routines C0/8A6D dispatches reach from 51 sites), while 9AC8 is called by the
movement COMMIT C0/9C5C (9C8D) and the arrival evaluation C0/9DDE (9DEA). So the plane blocks the player's
FEELER and nothing else.

The plane is built by C0/A712 at every location load (callers C0/033C and C0/0A59): fill $7E:70C0 with
$80 -- 64 rows of $40 at stride $100 -- then, per record of this location, `A771 LDA $F50000,X / AND $1E /
TAX / LDA $54 / STA $7E70C0,X`. The store is UNCONDITIONAL; A782's isOpened test gates only the metatile
pre-bump. Measured on the oracle: $7E77C2 stays 0x00 and the tile stays impassable after the chest is taken.

The port built no plane at all (`grep -rn 70C0 src/` reached only collision.c), and its interactive fetch
hook `fieldTileFetch` synthesises b0/b1/b2 from the packed asset planes and never looked at one. Worse, the
test that DID exist lived in the shared fetch `collFetchEngine`, i.e. on both entries -- one routine off.

MEASURED, loc 315's chest (2,7) with the player teleported to (2,8) facing up (`--teleport 315 2 8 0`),
20 frames of Up then A:

| | oracle | port before | port after |
|---|---|---|---|
| stops at | $1882 = 0x087F (y 135.9), 52 further Up frames change nothing | walked through to y 123 | y 135 |
| chest | opens | `no chest (6 record(s) here)` | `Got 1 MegaElixir!` |

Ported as: the plane built in `fieldLoad` next to the existing pre-bump pass, and the test moved out of the
shared fetch onto `CtCollisionSolidAt` (= 9AA1) so the commit (9AC8) no longer sees it. Check
`field-chest-tile-is-solid`, two mutations killed.


## The pre-bump after a door (2026-09-26, user defect #17)

"in a scene where the chests were already opened, they do not have their opened animation". The pre-bump of §2 ran only
from a flags IMAGE handed to `fieldLoad` -- and a door or an exit hands none (the host's WRAM already holds the
flags), so a room entered through a door drew its opened chests closed; only a cold start with `--loadflags`
pre-bumped them. And it bumped only the render's copy of the map: C0/A782's own store is `STA $7E3000,X`, the WRAM
map the tilemap is uploaded from and C0/16DC reads.

Now: the render's bump reads the LIVE `$7F:0001+` (C0/A7BC's) when no image is given, and `fieldChestPrebumpWram`
does A782 on the WRAM map -- `$7E30C0,X` bit 0 (A787-A78D; readable now that `ct_lz_map_to_wram` stages the cells
there), FE -> FF / E0 -> E1 -- right after C0/28E1 at the load, the cold load and the menu's reload, the order of
C0/286C (`JSR C0/28E1`, then C0/0A50 -> C0/A712). The battle return does not re-decompress the map in the port, so
its WRAM keeps the bump. `CT_CHESTDBG` prints `chestload:` for both halves; check
`field-chest-preopened-after-door` (a CT_CHANGELOC entry into loc 26 with chest 6's flag set; 2 mutations killed).
The §193 caveat stands for the render's half only (its asset has no b0 plane); the WRAM half reads the real bit.
