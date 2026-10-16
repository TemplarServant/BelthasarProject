# The TARGET-SELECT CURSOR — the highlight→OAM chain, the origin formula (P5.4 CLOSED), and the multi-target blink

User defect (translated from Portuguese): *"when using crono's cyclone tech, which is multi-target, the pointer is not being shown
on the potentially affected enemies"* — during Cyclone's target-select the pointing hand appears
on only ONE enemy instead of on every enemy the tech will hit.

Everything below is derived from the ROM disasm (`python tools/dis.py <fileoffset> <len> [m] [x]`, fileoffset =
`(bank−0xC0)<<16 | addr`) and measured in the LakeSnes oracle (`ct_probe.exe` = a copy of `build/ct.exe`;
⚠ `--monitor` LEN is DECIMAL). **Zero invention.** This CLOSES `battle_actionfx.md` **P5.4** (both open items:
the cursor anchor formula AND multi-enemy navigation) and adds the render half of `battle_multitarget.md`
(whose §5 T3 left the presentation open).

> ⚠ The whole routine block `C1/1979–C1/1B18` runs with **M=1 (8-bit A) / X=0 (16-bit index)**. Disassemble it as
> `python tools/dis.py <fo> <len> 1 0` — with the default 8-bit index the decode desynchronises at `C1/1AA2`
> (`A0 01 00` = `LDY #$0001`) and prints garbage (`BRK #84 / BRL`).

---

## 0 — HEADLINE (the canonical mechanism, 6 lines)

1. The hand cursor is **four OAM sprites, entries 0–3** of the shadow `$7E0700`/`$7E0900`, all tile `0x000`,
   attr `0x32` (**pal 1, prio 3**), OBJ size-large (16×16). Built by **`C1/1979`** (dispatched every rebuild by
   **`C1/17DD`**), hidden by `C1/1AFE`.
2. **Cursor origin (P5.4, CLOSED)** — for the actor index `A` held in the highlight table `$A62D`:
   **`OAM.x = ($1D0C,A + $9708,A − 0x10) & 0xFF`** and **`OAM.y = ($1D23,A + $9713,A) & 0xFF`**
   (`C1/19C0-19D4`, and byte-identically in the multi loop `C1/1ABA-1ACE`).
3. `$9708,A` / `$9713,A` are **per-actor cursor offsets**: PCs (A=0..2) = `(0x00, 0xF0)` (`CC/E3C7`/`CC/E3CA`);
   enemies (A=3..10) = **bytes +1 / +2 of the 10-byte enemy graphics record `E4/F604 + id*10`** (loader
   `CC/EBB0`/`CC/EBB7` — the arrays alias, §2.3). Hetake `0x5E` → `(0, 0xF8=−8)`; GATO `0x92` → `(0, 0xD0=−48)`.
4. **Sprite 0 is ALWAYS the primary** `$A62D[0]`; **sprites 1–3 are a 3-entry PAGE window** into `$A62D`, whose
   start `$960B` toggles **0 ↔ 3** on every rebuild (`C1/1AA2-1AF8`). List entries with bit7 set (`0xFF`) are
   skipped but still burn a sprite slot; unused sprites are parked by setting their OAM high-X bit (`$0900`).
5. Rebuild cadence: `CF/E73B` `INC $9931 / AND #03 / BNE / INC $993A` → the request fires **every 4th battle
   frame**; the main loop consumes it (`C1/10C6`) and calls `C1/17DD`. Because page 1 (`$A62D[3..5]`) is empty for
   any set of ≤3, the effect is **the secondary cursors BLINK 4 frames on / 4 frames off while the primary's
   cursor stays lit**. No palette change, no bob: attr stays `0x32` every frame; the only thing that changes is
   `$0900` (visibility) and the target's own live position.
6. Single-target (`$A62E` bit7 set) takes a different head (`C1/19B4`) that builds **only sprite 0** and also
   raises the enemy-name/HP readout — i.e. **one cursor**, exactly as today.

### The slice ledger (oracle, forest 3×Hetake, Cyclone)
```
f245  $9931 starts ticking = 1  (battle frame 1; STZ at CC/F0E6)
f365  target-select opens: $9609=1 $960D=12 $9614=00
        $99C0 = $A62D = 03 04 FF..            (hit set {actor3,actor4})
        $960B 00->03   $0700 = 68 37 00 32 | 68 37 00 32 | 48 57 00 32 | (stale)   $0900=EA
        -> OBJ0=(104,55) OBJ1=(104,55) OBJ2=(72,87)  = actor3, actor3, actor4      3 HANDS
f368  $960B 03->00   $0900=FE                                                       1 HAND (primary only)
f374/f377/f380/f386/...  the 03<->00 toggle repeats (every 4th battle frame, §4)
f386  Down: $9614=02  $A62D = 05 FF..   OBJ0=(152,119)                              1 HAND (set = {actor5} alone)
f413  Down: $9614=01  $A62D = 04 03 FF  OBJ0/1=(72,87) OBJ2=(104,55)                3 HANDS
```

---

## 1 — THE DISPATCHER `C1/17DD` (who draws the cursor, and when)

`dis.py 117DD 0x20 1 0`:
```
117DD LDA $95D5 / BPL 117E5 / JMP C1/1AFE   ; $95D5 = 0xFF (no chooser) -> HIDE the 4 cursor OBJs
117E5 LDA $9609 / BEQ 117ED / JMP C1/1979   ; ** $9609 != 0 = TARGET-SELECT -> the CURSOR BUILDER **
117ED LDA $95DB / BEQ 117FB / DEC A / BNE 117F8
117F5 JMP C1/17FE   ; $95DB==1 tech submenu cursor      117F8 JMP C1/18B0 ; $95DB==2 item submenu cursor
117FB JMP C1/1B14   ; command level -> nothing
```
Callers (`dis.py 110A0 0x80 1 0`) — all three are gated on the redraw request `$993A`:
```
110C6 LDA $993A / BEQ .. / STZ $993A / JSR C1/1B67 / JSR C1/0C2D / JSR C1/1153 / JSR C1/17DD
110E3 (same, RTL variant)      110FA (same)
```

### 1.1 The HIDE path `C1/1AFE`
```
11AFE LDA $0900 / ORA #55 / STA $0900     ; set the OAM high-X bit of sprites 0..3 -> x += 256 (off-screen)
11B06 LDA #F0 / STA $0701/$0705/$0709/$070D ; and Y = 0xF0
11B14 STZ $EE / STZ $EF / RTS
```
Oracle: at the confirm frame (GATO f457) `$0700 = 78 F0 .. 82 F0 .. 7A F0 .. 82 F0 ..`, `$0900 = FF`. ✓

---

## 2 — THE BUILDER `C1/1979` — tiles, palette, and the ORIGIN FORMULA (P5.4 CLOSED)

`dis.py 11979 0x70 1 0`:
```
11979 LDA $9613 / BPL 11981 / JMP C1/1AFB   ; $9613 bit7 = "no selectable target" -> leave OAM untouched
11981 LDA $95DB / DEC A / BNE .. / JSR C1/0EBA
1198A LDA $0900 / ORA #55 / STA $0900       ; ** park ALL FOUR cursor sprites first **
11992 LDA #32 / STA $0703 / STA $0707 / STA $070B / STA $070F   ; ** attr 0x32 = pal1, prio3, no flip **
119A0 STZ $0702 / STZ $0706 / STZ $070A / STZ $070E             ; ** tile 0x000 **
119AC LDA $A62E / BMI 119B4 / JMP C1/1A7B   ; $A62D[1] valid  -> the MULTI head (§3)
119B4 LDA $A62D / BPL 119BC / JMP C1/1AFB   ; nothing to point at
119BC STA $A64E / TAX                       ; X = the target ACTOR index (0..10)
119C0 CLC / LDA $1D0C,X / ADC $9708,X / SEC / SBC #10 / STA $0700   ; ** OAM x **
119CD CLC / LDA $1D23,X / ADC $9713,X / STA $0701                   ; ** OAM y **
119D7 LDA $95DB / BNE 11A4B                 ; a submenu is open -> skip the name/HP readout
119DC LDA $A64E / CMP #03 / BCC 11A4B       ; PC target -> skip
119E3 ... $984D,X / CC/F8ED / $9F34 / $9F29,Y / E1/DE80 / JSL CD0030 ; enemy NAME + HP readout (single only)
11A4B LDA $0900 / AND #FE / STA $0900       ; ** un-park sprite 0 (only sprite 0 shows) **
11A78 JMP C1/1AFB (-> C1/1B14 RTS)
```
`attr 0x32` = `%0011_0010` → name-hi 0, **palette 1**, **priority 3**, no flip. The size bit lives in `$0900`
(bits 1/3/5/7) and is 1 for all four → **16×16**. Oracle `.oam` sidecar (`--peek`, §5) confirms:
`OBJ0..3 tile=00 attr=32 size=1`.

### 2.1 The formula, verified in BOTH fights
| datum | forest actor3 | forest actor4 | forest actor5 | GATO actor6 |
|---|---|---|---|---|
| `$1D0C,A` (live x) | `78`=120 | `58`=88 | `A8`=168 | `88`=136 |
| `$1D23,A` (live y) | `3F`=63 | `5F`=95 | `7F`=127 | `71..76`=113..118 (idle bob) |
| `$9708,A` (dx) | 00 | 00 | 00 | 00 |
| `$9713,A` (dy) | `F8`=−8 | `F8`=−8 | `F8`=−8 | `D0`=−48 |
| **formula → OAM** | **(104,55)** | **(72,87)** | **(152,119)** | **(120, 65..70)** |
| **oracle OAM** | **(104,55)** ✓ | **(72,87)** ✓ | **(152,119)** ✓ | **(120, 65→70)** ✓ |

The GATO column reproduces `battle_actionfx.md` P5.2's measured *"x = 120 (constant), y = 65→70 as GATO
idle-settles (f436 y65, f450 y68, f468/f488 y70)"* — the "fixed offset above the target that inherits the
target's idle motion" IS `liveY − 48`. The forest column reproduces our baked `ctHetakeCursor` table
(`src/data/battle_hetake.h:184` = `{104,55},{72,87},{152,119}`) **exactly** — the bake was the formula's output all
along, so replacing it with the formula is byte-identical.

### 2.2 Where `$9708`/`$9713` come from — the PC defaults
`dis.py 0xCE3A0 0x50 1 0` (battle init, bank CC):
```
CE3C2 LDX #0002 / LDA #F0
CE3C7 STZ $9708,X          ; ** PC cursor dX = 0 **        (X = 2,1,0)
CE3CA STA $9713,X          ; ** PC cursor dY = 0xF0 (−16) **
CE3D0 LDX #0002 / LDA #08 / STA $971E,X / STA $9729,X      ; PC box half-extents = 8
```
Oracle read (forest, in battle): `$9708 = 00 00 00 00 00 00 …`, `$9713 = F0 F0 F0 F8 F8 F8 00 …` — indices 0-2
(PCs) `F0`, indices 3-5 (the three Hetakes) `F8`. ✓ Both directions.

### 2.3 Where `$9708`/`$9713` come from — the ENEMY graphics record `E4/F604 + id*10`
`dis.py 0xCEB50 0x90 1 0` (the per-enemy loader; `$8C` = enemy id, `$80` = enemy SLOT 0..7):
```
CEB65 LDA $8C / ASL A / STA $82 / ASL A / ASL A / CLC / ADC $82 / TAX    ; X = id*10
CEB73 LDY $80                                                            ; Y = enemy slot
CEB75 LDA $E4F604,X / STA $9700,Y      ; +0 flags
CEBB0 LDA $E4F605,X / STA $970B,Y      ; +1  == $9708,(slot+3)  ** cursor dX **
CEBB7 LDA $E4F606,X / STA $9716,Y      ; +2  == $9713,(slot+3)  ** cursor dY **
CEBBE LDA $E4F607,X / STA $9721,Y      ; +3  == $971E,(slot+3)  box half-extent X
CEBC5 LDA $E4F608,X / STA $972C,Y      ; +4  == $9729,(slot+3)  box half-extent Y
```
**The alias is exact**: the slot-indexed arrays `$9700/$970B/$9716/$9721/$972C` (stride 11) are the same bytes as
the combatant-indexed arrays `$9708/$9713/$971E/$9729` at `cmb = slot+3` (`$970B+slot == $9708+(slot+3)`, etc.).
Oracle proof (GATO = enemy slot 3 / cmb 6): `$9700 = 00 00 00 **02** …` (record +0 = 0x02 ✓),
`$9716 = 00 00 00 **D0** …` and `$9713 = F0 F0 F0 00 00 00 **D0** …` — the SAME byte at `$9719`. ✓

ROM records (`rom[((0xE4-0xC0)<<16|0xF604) + id*10]`):
```
id 5E (Hetake): 00 00 F8 10 10 41 70 4D 6B 30   -> dX=+0  dY=−8    extent 16/16
id 92 (GATO)  : 02 00 D0 10 10 41 7C 77 B8 A0   -> dX=+0  dY=−48   extent 16/16
id 00         : 00 00 F0 10 18 41 4F 22 10 1A   -> dX=+0  dY=−16   extent 16/24
id 01         : 00 00 F0 0C 0C 12 50 23 12 1B   -> dX=+0  dY=−16   extent 12/12
```
**Side finding (out of lane, real bug):** `src/battle.c:2876-2879 battleMoveExtent` returns 8 for every
non-GATO enemy. The canonical value is record `+3` — Hetake = `0x10` = **16**. Oracle `--read "971E+11"` on
`saves/battle_re/forest_battle_3hetake.state` → `08 08 08 **10 10 10** 00 …`. Our box-overlap arrival test
(`battleBox`) is therefore 8px too tight for every Hetake.

---

## 3 — THE MULTI HEAD `C1/1A7B` + the PAGE loop `C1/1AA2` (the blink)

`dis.py 11A7B 0x50 1 0`:
```
11A7B LDA $A62D / BMI 11AA2                 ; sprite 0 = the PRIMARY $A62D[0], always
11A80 TAX / CLC / LDA $1D0C,X / ADC $9708,X / SEC / SBC #10 / STA $0700
11A8E CLC / LDA $1D23,X / ADC $9713,X / STA $0701
11A98 LDA $0900 / AND $CCFB8D / STA $0900   ; un-park sprite 0 (abs = mask index 0 = 0xFE)
11AA2 LDY #0001 / STY $82                   ; $82 = OAM sprite cursor, starts at 1
11AA7 LDA $960B / TAY / STY $80             ; ** $80 = the PAGE START index into $A62D (persistent $960B) **
11AAD loop: LDY $80 / LDA $A62D,Y / BMI 11ADD          ; FF entry -> no sprite (but $82 still advances)
11AB4   TAX / LDA $82 / ASL A / ASL A / TAY            ; Y = sprite*4 (OAM stride)
11ABA   CLC / LDA $1D0C,X / ADC $9708,X / SEC / SBC #10 / STA $0700,Y
11AC7   CLC / LDA $1D23,X / ADC $9713,X / STA $0701,Y  ; ** identical origin formula **
11AD1   LDX $82 / LDA $0900 / AND $CCFB8D,X / STA $0900 ; un-park sprite $82
11ADD skip: INC $80 / LDA $80 / CMP #03 / BEQ 11AF1 / CMP #06 / BEQ 11AF1
11AE9   INC $82 / LDA $82 / CMP #04 / BNE 11AAD
11AF1 done: LDA $80 / CMP #06 / BNE +2 / TDC           ; index 6 wraps to 0
11AF8   STA $960B                                       ; ** persist the next page start **
11AFB JMP C1/1B14
```
`CC/FB8D = FE FB EF BF` = the per-sprite AND masks that clear OAM high-X bits 0/2/4/6.
`$960B` is written **only** at `C1/1AF8` and is never reset by target-select open (`C1/1F80-1F8C` clears
`$9613/$960C/$960A/$A64F/$A6D8` — not `$960B`), so it is a free-running **2-phase page toggle: 0 ↔ 3**.

### 3.1 The law, traced against the measurement
| list `$A62D` | page start | sprites drawn | `$0900` predicted | `$0900` oracle |
|---|---|---|---|---|
| `03 04 FF …` | 0 | 0←[0], 1←[0], 2←[1], (3 skipped by `FF`) | `0xEA` | **`EA`** ✓ |
| `03 04 FF …` | 3 | 0←[0] only (indices 3,4,5 are `FF`) | `0xFE` | **`FE`** ✓ |
| `03 04 05 FF …` | 0 | 0←[0], 1←[0], 2←[1], 3←[2] | `0xAA` | **`AA`** ✓ |
| `03 04 05 FF …` | 3 | 0←[0] only | `0xFE` | **`FE`** ✓ |
| `05 FF …` (primary alone) | either | 0←[0] only | `0xFE` | **`FE`** ✓ |

`$0900` bit layout: 2 bits per sprite, `bit0/2/4/6` = OAM X bit8 (**1 = parked off-screen**), `bit1/3/5/7` = size
(**1 = 16×16**). `0xEA = %11101010` → sprites 0,1,2 visible, sprite 3 parked.

**Consequences that matter for the render:**
- entries `$A62D[6..10]` are **never** given a cursor (the loop stops at index 3 or 6);
- the primary is drawn **twice** on page 0 (sprite 0 and sprite 1, identical x/y/tile/attr) — pixel-identical,
  so a framebuffer renderer may draw it once;
- for any hit set of size ≤3 (every set our engine can produce today) page 1 is empty → **primary always lit,
  secondaries blink**.

---

## 4 — THE REBUILD CADENCE (the blink clock)

`dis.py 0xFE720 0x40 1 0`:
```
FE73B INC $9931 / LDA $9931 / AND #03 / BNE FE748 / INC $993A   ; ** request a cursor-OBJ rebuild every 4 frames **
```
`$9931` writers = `STZ` at `CC/F0E6` (battle init) and this `INC`. Oracle (`--monitor "9931:1" --diff` on
`forest_cyclone.state`, input `40:60,0:300`): `$9931` is 0 until **f245**, where it becomes 1, then **+1 every
frame** — i.e. `$9931` = the battle's own frame index (1-based).

Observed toggle frames of `$960B` on that run: f365, 368, 374, 377, 380, 386, 389, 392, 397, 402, 405 …
Request frames (`$9931 & 3 == 0`) = f360, 364, 368, 372, 376, 380, 384, 388, 392, 396, 400, 404 — every observed
toggle is a request frame plus a **0–2 frame consume lag** (the monitor samples at a fixed point inside the
frame; `$993A` was still 1 at f360/364/372/376/384/388/396/400/404 and already consumed at f368/380/392).
**The law is the 4-frame request; the 3/5/6-frame jitter in a per-frame sample is the consume phase.**

**Not** a blink of any other kind: across all captured frames the attr byte of OBJ0..3 stayed `0x32` (no palette
alternation, no priority change), the tile stayed `0x00`, and the x/y only changed when the *target itself*
moved (GATO's idle bob y 65→70). Verified over 100+ consecutive frames in `--monitor "0700:16,0900:1"`.

---

## 5 — THE ORACLE CAPTURES (exact, reproducible)

**Seed states** (built in the scratchpad; the base is the repo's `saves/battle_re/forest_holdleft_pre.state`,
a legacy raw LakeSnes stream whose WRAM image starts at file offset **0x45** — located by matching the
`--peek` `.wram` sidecar bytes at `$2980` back into the state file):
```
python -c "d=bytearray(open('saves/battle_re/forest_holdleft_pre.state','rb').read()); d[0x45+0x2837]=0x80; open('forest_cyclone.state','wb').write(bytes(d))"
python -c "... d[0x45+0x2837]=0xFF ... open('forest_alltechs.state','wb').write(bytes(d))"   # all 8 Crono techs
```
`$2837` is Crono's learned-single-tech bitmask (MSB = his first tech); `0x80` = Cyclone only (the
`battle_multitarget.md` §Seed injection), `0xFF` = all eight.

**Drive to Cyclone's target-select and watch the whole chain**
```
ct_probe.exe --monitor "9609:1,960B:1,960D:1,9614:1,A62D:11,99C0:11,0700:16,0900:1,1D0C:11,1D23:11,9708:11,9713:11" \
  --from forest_cyclone.state --input "40:60,0:275,20:2,0:12,100:2,0:12,100:2,0:120" --diff
```
(hold LEFT 60f = trigger the op-0xD8 ambush; neutral to the command phase; `Down` = Att→Tech; `A` = open the
Tech submenu; `A` = confirm Cyclone → target-select at **f365**.)

**Move the primary** (append `,20:2,0:24,20:2,0:24`): `$9614` 0→2→1, `$A62D` `03 04 FF` → `05 FF` → `04 03 FF`,
OBJ0 `(104,55)` → `(152,119)` → `(72,87)`. The primary-alone case (`$9614=2`, slot2 at (168,127) is outside the
radius-9 disc from both others) shows **one** cursor — `$0900 = FE` on every frame of that selection.

**A 3-target set** (`forest_alltechs.state`, 4× `Down` in the tech list = **Lightning 2**, group `0x08` = all
enemies):
```
ct_probe.exe --monitor "...same..." --from forest_alltechs.state \
  --input "40:60,0:275,20:2,0:12,100:2,0:12,20:2,0:10,20:2,0:10,20:2,0:10,20:2,0:10,100:2,0:60" --diff
→ f414 $960D=08 $A62D=03 04 05 FF..  $0700 = 68 37 00 32 | 68 37 00 32 | 48 57 00 32 | 98 77 00 32  $0900=AA
```
and the live `.oam` sidecar of the matching `--peek` (same btnscript, tail `0:5`):
```
OBJ0 x=104 y= 55 tile=00 attr=32  X8=0 size=1
OBJ1 x=104 y= 55 tile=00 attr=32  X8=0 size=1
OBJ2 x= 72 y= 87 tile=00 attr=32  X8=0 size=1
OBJ3 x=152 y=119 tile=00 attr=32  X8=0 size=1
```
The rendered frame (`python tools/ct.py fb2png`) shows **three** white pointing hands, one per Hetake.

**GATO single target** (`saves/battle_re/gato_wait.state`, `--input "100:8,0:400,100:8,0:40,100:8,0:60"`):
`$A62D = 06 FF…` (GATO is actor **6** = enemy slot 3), `$9713[6] = D0`, `$1D0C[6] = 88`, `$1D23[6] = 71..76`,
`$0700` OBJ0 = `78 41..46 00 32` = **(120, 65..70)**, `$0900 = FE` → **one** cursor. ✓

### 5.1 Crono's tech → target-group table (bonus; oracle, `forest_alltechs.state`, `--monitor "9EE3:7"`)
| list idx | tech id | name | `$9EE4` group | flags `$9EE5` | MP `$9EE6` | set produced |
|---|---|---|---|---|---|---|
| 0 | 01 | Cyclone | **0x12** | 03 | 02 | radius²=9 around the primary |
| 1 | 02 | Slash | **0x0B** | 01 | 02 | line/corridor scan (`C1/2169`→`C1/25A3`); measured `{03}` in this geometry |
| 2 | 03 | Lightning | **0x07** | 00 | 02 | single enemy |
| 3 | 04 | Spincut | **0x07** | 00 | 04 | single enemy |
| 4 | 05 | Lightning 2 | **0x08** | 00 | 08 | **all alive enemies** — measured `03 04 05` |
| 5 | 06 | Life | 0x03 | 80 (grey) | 0A | — |
| 6 | 07 | Confuse | 0x07 | 80 | 0C | — |
| 7 | 08 | Luminaire | 0x08 | 80 | 14 | — |

**2026-09-17: the table is CC:1ACB** (two bytes a tech, 0x75 techs: singles, doubles, triples). The record staging
`C1/C990` (`REP #20 / ASL A / STA $02` = id*2) reads `LDA $CC1ACC,X / AND #3F` into the record's +2 and `LDA $CC1ACB,X`
into +1 (`C1/C9BC-C9CF`). Every measured value is in it (01 12/03, 02 0B/01, 03-08 above, 0B 07/00, 42 07/00) and the
attract 07D's Frog Squash 28 -> 08/00 (all enemies: the cartridge's `$A017 = 03`, `$A018 = 04 05 06 07`). `C1/C264`
reads an alternative `CC:1BB5[id] & 7F` when the actor record's bit 6 is set (not ported). battle.c battleTechGroup.

The group jump table `C1/1FF8` (33 words, `dis.py`-dumped raw) decodes to:
`00→203A 01→209A 02→20D6 03→20E0 04→209A 05→2136 06→2163 07→20A9 08→20B6 09→20C6 0A→20B6 0B→2169 0C→2203
0D→21AF 0E→203A 0F→22F5 10→203A 11→224B 12→225F 13→22A4 14→22A4 15..17→203A 18→2329 19→203A 1A→225F 1B→22D3
1C..20→203A` — this **extends** `battle_multitarget.md` §1 (which left 0x05/0x06/0x0C/0x0D blank).
Handler ranges (`dis.py 1203A/1209A/120A9/120B6 …`):
```
1203A LDX #0003 / STX $80 / LDX #0000 / INC $960A   ; idx 0 default = PCs [0,3), SINGLE   (items' 0x80 -> here)
1209A LDX #0003 / STX $80 / LDX #0000 / LDA #80 / STA $960C ; all PCs
120A9 LDX #000B / STX $80 / LDX #0003 / INC $960A   ; single ENEMY [3,0x0B)   (Att 0x07 / Ice 0x07)
120B6 LDX #000B / STX $80 / LDX #0003 / LDA #80 …   ; ALL enemies (0x08 / 0x0A)
```
and the shared list builder `C1/2045` fills the **compact** list `$99C0` (skip `$96F5,X==0` absent, `$9FF7,X`
bit7 dead, `$A0A8,X`/`$A09B,X` status), compacts it (`C1/27C5`), then
`C1/207E: $A62D[0] = $99C0[$9614]` and, **iff `$960C` bit7 is set**, `C1/208D: $A62D[0..10] = $99C0[0..10]`.
**So: `$960C`=0x80 (all/AoE groups) ⇒ the whole set is highlighted; `$960A` (single groups) ⇒ only the cursor
entry.** That is the exact fork our renderer must reproduce.

---

## 6 — WHAT OUR ENGINE DOES INSTEAD (and why the user sees the defect)

`src/field.c:3852-3873` — `battleTargetCursorRender`:
```c
static void battleTargetCursorRender(uint8_t* fb, const uint8_t* mem, const CtBattleHost* h) {
  if (!mem || mem[0x9609] != 1) return;
  int slot = mem[0x9614];                       // <- ONE slot; no highlight set at all
  uint8_t id = mem[CT_ENEMY_BLOCK(slot) + 0];
  if (id == 0x5E && slot < CT_HETAKE_NSLOT) { … ctHetakeCursor[slot] … }   // per-id BAKED table
  else if (id == 0x92) { … ctBattleStanceActor[2].anchor + (16,7) … }      // per-id measured delta
  else return;                                  // any other enemy -> NO CURSOR AT ALL
  … draws exactly one 16x16 tile 0x000 pal1 …
}
```
Four distinct divergences from §2/§3:
1. **No set.** It draws one cursor over `$9614` regardless of `$960D`. The hit set (`battleBuildHitSet`,
   `src/battle.c:1315`) is computed only at dispatch, never for the highlight. **This is the user's defect.**
   Reproduced: `CT_BATTLE_SCRIPT="n:40,d:2,n:8,a:2,n:8,a:2,n:120" CT_BATTLESHOT=… CT_BATTLESHOT_AT=230
   build/ct_field.exe --assets ct_assets.dat --scene 19 --battletest saves/battle_re/forest_cyclone.raw.wram`
   → trace `techtrace f220 tech-confirm 9609=1 960D=12`; the f230 shot has **one** hand (over the Hetake at
   (104,55)) and none over the second in-radius Hetake at (72,87), while the oracle at the equivalent state
   shows hands on both.
2. **Per-enemy-id hard-coding** (`src/data/battle_hetake.h:184 ctHetakeCursor`, `CT_TGTCUR_DX/DY 16/7` at
   `src/field.c:3850-3851`) instead of the §2 formula. Any enemy that is neither `0x5E` nor `0x92` renders **no
   cursor**, so target-select is invisible in every unpinned fight.
3. **No blink.** The ROM's 4-frame page toggle (§3/§4) is absent.
4. **PC-side items point at an enemy.** `$960D` bit7 (item groups, e.g. Tonic `0x80`) is ignored; the renderer
   still reads `$9614` (an enemy slot) while `battleItemSubmenu` (`src/battle.c:1469-1481`) navigates a *PC*
   cursor in `h->itemTgtSel`. The hand lands on the wrong actor (or vanishes).

Related, same root cause (per-id constants where the ROM reads a table): `src/battle.c:1305 battleTechGroup`
hard-codes `{1→0x12, 0x0B→0x07, else 0x01}`; §5.1 is the measured table. And `src/battle.c:2876
battleMoveExtent` returns 8 for non-GATO where the ROM reads record `+3` (Hetake = 16).

---

## 7 — IMPLEMENTATION SPEC (a GENERAL `battleTargetCursorRender`)

All constants below are cited above; none is tuned to a render.

### (a) `src/battle.c` / `src/battle.h` — expose the set builder + the per-actor cursor offset
```c
/* battle.h */
int  CtBattleHitSet(CtBattleHost* h, int group, int primary, int* out, int cap);  /* wraps battleBuildHitSet */
int  CtBattleCursorOfs(const CtBattleHost* h, int cmb, int* dx, int* dy);         /* $9708,cmb / $9713,cmb */
```
```c
/* battle.c -- $9708,X / $9713,X (C1/19C4/19D1). PCs: CC/E3C7 STZ + CC/E3CA #F0.
   Enemies: the 10-byte gfx record E4/F604 + id*10, bytes +1/+2 (loader CC/EBB0/CC/EBB7; the
   slot-indexed $970B/$9716 arrays ALIAS the combatant-indexed $9708/$9713 at cmb = slot+3). */
#define ROM_ENEMY_GFXREC 0x24F604u          /* E4/F604 flat = ((0xE4-0xC0)<<16)|0xF604 */
int CtBattleCursorOfs(const CtBattleHost* h, int cmb, int* dx, int* dy) {
  if (cmb < 0 || cmb >= 11) return 0;
  if (cmb < 3) { *dx = 0; *dy = -16; return 1; }              /* 0x00 / 0xF0 */
  long n = 0; const uint8_t* rom = CtBattleRom(&n);
  if (!rom) return 0;
  int id = h->mem[battleBlock(cmb) + 0];                       /* same read as battleMoveExtent, battle.c:3196 */
  const uint8_t* rec = rom + ROM_ENEMY_GFXREC + (unsigned)id * 10u;
  *dx = (int8_t)rec[1]; *dy = (int8_t)rec[2];
  return 1;
}
```
`battleBuildHitSet` (`battle.c:1315`) already implements the radius law for `0x12`/`0x1A`. **Extend it with the
two group classes that are now disassembled AND oracle-measured** (leave everything else at `{primary}`):
```c
  if (group == 0x08 || group == 0x0A) {          /* C1/20B6: ALL enemies, range [3,0x0B) -- measured 03 04 05 */
    int n = 0;
    for (int slot = 0; slot < 8 && n < cap; slot++)
      if (s[0xAF02 + slot] != 0xFF) out[n++] = 3 + slot;       /* the $96F5/$9FF7 alive gates, C1/2047-204F */
    return n ? n : 1;                                          /* $99C0 order = slot order (C1/2071 STA $99C1,Y) */
  }
```
(Do **not** add 0x01/0x04/0x09/0x0B here: 0x0B's corridor test `C1/25A3` is not yet decoded, and the PC-side
groups have no oracle capture — see §8.)

### (b) `src/field.c` — the renderer
Replace the body of `battleTargetCursorRender` (field.c:3852-3873) with:
```c
static void battleTargetCursorRender(uint8_t* fb, const uint8_t* mem, CtBattleHost* h) {
  if (!fb || !mem || mem[0x9609] == 0) return;          /* C1/17E5 BEQ: any nonzero $9609 = target-select */
  if (mem[0x95D5] >= 3) return;                         /* C1/17DD: $95D5 = 0xFF -> C1/1AFE hides the OBJs */
  /* --- the highlight table $A62D (C1/2045 + C1/27E8) --- */
  int group   = mem[0x960D];
  int primary = 3 + mem[0x9614];                        /* our $9614 = enemy SLOT (battleSelectedEnemy) */
  if (mem[0x9614] >= 8 || mem[0xAF02 + mem[0x9614]] == 0xFF) return;
  int set[11], n = CtBattleHitSet(h, group & 0x7F, primary, set, 11);
  /* --- the PAGE window (C1/1AA2-1AF8): sprite0 = set[0]; sprites 1..3 = 3 entries from `page` --- */
  int page = ((h->frames / 4) & 1) ? 3 : 0;             /* $960B toggles 0<->3 on every rebuild (§3/§4) */
  int draw[4], nd = 0;
  draw[nd++] = set[0];                                  /* C1/1A7B: sprite 0 is ALWAYS $A62D[0] */
  { int i = page, spr = 1;
    for (;;) { if (i < n) draw[nd++] = set[i];          /* an FF entry draws nothing but still burns `spr` */
               i++; if (i == 3 || i == 6) break;
               spr++; if (spr == 4) break; } }
  for (int k = 0; k < nd; k++) {
    int cmb = draw[k], dx, dy;
    if (cmb < 0 || cmb >= 11 || !CtBattleCursorOfs(h, cmb, &dx, &dy)) continue;
    int ax = (h->mvLiveX[cmb] + dx - 0x10) & 0xFF;      /* C1/19C0 / C1/1ABA  (8-bit wrap) */
    int ay = (h->mvLiveY[cmb] + dy)        & 0xFF;      /* C1/19CD / C1/1AC7 */
    /* …unchanged 16x16 blit: tile quad 0x000/0x001/0x010/0x011 from ctBattleStanceObjTiles,
         palette ctBattleStanceObjPal[1] (attr 0x32 = pal1 prio3), plotted at (ax,ay)… */
  }
}
```
Notes that keep it mandate-safe and byte-identical where it is already pinned:
- **`h->mvLiveX/Y` are the ROM's `$1D0C,X`/`$1D23,X`** (already asserted and used by `battleBuildHitSet`,
  battle.c:1320) — the same source the ROM formula reads. No new position plumbing.
- **Byte-identical on both pinned fights.** Forest Hetake slots → `(104,55)/(72,87)/(152,119)` = the retired
  `ctHetakeCursor` values; GATO (cmb 6, rest `mvLiveY=118`) → `(120,70)` = today's `anchorY+7`. So
  `field-battle-*` cursor signatures for the single-target checks stay unchanged **on the page-0 phase**; the
  new blink changes frames where `(h->frames/4)&1 == 1` **only for sets of size ≥2** (a single-target set has
  `n==1`, so the page loop finds nothing to add and sprite 0 alone is drawn — identical every frame).
- **Delete** `ctHetakeCursor` (`src/data/battle_hetake.h:184`), `CT_TGTCUR_DX/DY` (`src/field.c:3850-3851`) and both
  `battleEnemyAnchorDelta`/`battleEnemyLiveDelta` calls inside the cursor path — the formula already tracks the
  live foot (that is what `$1D0C/$1D23` are), so the delta indirection would double-count.
- The primary appearing twice (sprite 0 and, on page 0, sprite 1) is pixel-identical; drawing it once is fine
  for a framebuffer renderer. If OAM byte-exactness is ever required, emit the duplicate.
- The renderer needs a non-const `CtBattleHost*` (or a const wrapper for `CtBattleHitSet`); both call sites
  (`field.c:4102`, `field.c:7090`) already hold one.

### (c) Suggested regression pin (`tools/regress_checks.json`)
`field-battle-tgtcursor-cyclone-multi` — forest, `CT_BATTLE_SCRIPT="n:40,d:2,n:8,a:2,n:8,a:2,n:120"`,
`CT_BATTLESHOT_AT=230` (page 0) **and** `=234` (page 1): the page-0 shot must contain hands at
(104,55) **and** (72,87) and none at (152,119); the page-1 shot only (104,55). Plus a `=230` GATO shot
(`--battletest saves/battle_re/gato_t0.raw.wram`, Att→target-select) whose fnv must equal today's — the
single-target byte-identity proof.

---

## 8 — OPEN

| # | item | why it is open / what closes it |
|---|---|---|
| C1 | **PC-side cursor rendered** (`$960D` bit7, e.g. a Tonic on a PC) | the DATA is pinned (`$9708/$9713` PC entries `(0,0xF0)`, `CC/E3C7`/`CC/E3CA`, and oracle-read `F0 F0 F0`), and `C1/1979` is actor-index-generic (no PC branch), but **no capture exists**: both repo battle states have an EMPTY inventory (`$1580 = 00…` in the forest AND in the GATO fight, so the Item submenu opens with nothing confirmable). Closes with a pre-battle state carrying ≥1 Tonic (patch `$2400`/`$2500` in the field state, then `Down,Down,A,A`). Predicted seat-0 cursor in the forest: `(160−16, 71−16) = (144,55)`. |
| C2 | **Sets of 4–6 (a true 2-page cycle)** | the page law is fully disassembled but every reachable set here is ≤3 (3 enemies max), so page 1 is always empty. A fight with ≥4 enemies + an all-enemies tech (group 0x08) would show sprites 1-3 walking `[0,1,2]→[3,4,5]`. Entries `[6..10]` provably never get a cursor. |
| C3 | ~~**The absolute blink PHASE**~~ **CLOSED 2026-10-15** (§10: `$960B` is the menu pass's own byte now, frame-identical to the cartridge) | (was: the cadence is pinned but the mapping from `$9931` to our `h->frames` origin is not measured, so `(h->frames/4)&1` may be out of phase.) |
| C4 | **Group `0x0B` (Slash) corridor test `C1/25A3`** | the handler (`C1/2169`: caster `$960F` → `$9605`, primary → `$9606`, width `$9607=2`, `$9608=0`, `JSR C1/25A3`) is located and the oracle result measured (`$A62D = 03 FF…` from primary slot0 in the forest geometry), but `C1/25A3` itself is not disassembled, so the *general* line law is unknown. Today it coincides with the single fallback → no visible error. |
| C5 | **`$9613` / `$A64E` name+HP readout** (`C1/19DC-11A4B`: `$984D,X`, `CC/F8ED`, `$9F34`, `$9F29,Y`, `E1/DE80`, `JSL CD/0030`) | the single-target path also raises the enemy NAME/HP display; not part of this lane, not rendered by our engine. |
| C6 | **`$A0A8,X` / `$A09B,X` skip gates** (carried from `battle_multitarget.md` M2) | still 0 for every actor in both captures, so the exact status semantics stay un-pinned; our `$AF02 != 0xFF` proxy is the measured equivalent here. |
| C7 | **`battleMoveExtent` (side finding)** | ROM reads record `+3`/`+4` (`CC/EBBE`/`CC/EBC5`); Hetake = 16, our engine says 8. Oracle `$971E = 08 08 08 10 10 10`. A separate fix (affects the box-overlap arrival test, not the cursor). |

---

## SCRATCHPAD ARTIFACTS (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-e956ecca-…/scratchpad/lane_D1/`
- `ct_probe.exe` (= `build/ct.exe`), `forest_cyclone.state` (`$2837=0x80`), `forest_2techs.state` (`0xC0`),
  `forest_alltechs.state` (`0xFF`) — all derived from `saves/battle_re/forest_holdleft_pre.state`, WRAM at
  file offset `0x45`.
- `l2cur.raw` + `.wram/.vram/.oam` + `l2cur.png` / `l2cur_zoom.png` (the 3-hand oracle frame),
  `ours_tgt.raw` / `ours_tgt.png` (our engine's 1-hand frame at the same state).
- Disasm anchors (fo = `(bank−0xC0)<<16|addr`, all with `m=1 x=0`): dispatcher `0x117DD`; builder `0x11979`;
  multi head `0x11A7B`; page loop `0x11AA2`; hide `0x11AFE`; tail `0x11B14`; list builder `0x12045`; the
  epilogue `0x1207E`/`0x1208D`; handlers `0x1203A`/`0x1209A`/`0x120A9`/`0x120B6`/`0x12169`/`0x1225F`;
  the AND-mask table `CC/FB8D` = `0x0CFB8D`; the PC cursor defaults `0xCE3C2`; the enemy record loader
  `0xCEB65`; the enemy gfx records `E4/F604` = `0x24F604`; the rebuild cadence `0xFE73B`.

---

## VERIFICATION (adversarial pass)

Independent re-run of every ROM address and every oracle datum the IMPLEMENTATION SPEC depends on
(`ct_probe.exe` = a fresh copy of `build/ct.exe`; states rebuilt from `forest_holdleft_pre.state` with the
same `0x45+0x2837` patch; `python tools/dis.py` re-run on every cited offset). **Mechanism: SOUND.
Two numeric claims in §7 about OUR CURRENT RENDER are REFUTED, plus one inverted pin.**

### CONFIRMED (re-derived / re-measured)

| # | claim | independent evidence |
|---|---|---|
| 1 | dispatcher `C1/17DD` (`$95D5` BPL -> hide `C1/1AFE`; `$9609` BEQ -> builder `C1/1979`) | `dis.py 117DD 0x20 1 0` -- byte-identical to §1 |
| 2 | **origin formula** `x=$1D0C,A+$9708,A-0x10`, `y=$1D23,A+$9713,A` | `dis.py 11979 0x70 1 0` (C1/19C0-19D4) **and** `dis.py 11A7B 0x50 1 0` + `11AC7 0x60` (C1/1A80-1A95, C1/1ABA-1ACE) -- three identical copies |
| 3 | page loop + toggle 0<->3, `$82` advances on skipped entries, stop at idx 3 or 6, `TDC` at 6 | `dis.py 11AC7 0x60 1 0` (C1/1ADD-1AF8) |
| 4 | `CC/FB8D = FE FB EF BF` | raw ROM read `rom[0x0CFB8D:+8] = FE FB EF BF 00 01 01 01` |
| 5 | PC defaults `$9708=0 / $9713=0xF0` | `dis.py 0xCE3A0 0x50 1 0` (CE3C2-CE3CE) + oracle `$9713 = F0 F0 F0 F8 F8 F8` read on `forest_battle_3hetake.state` (a state the dossier did NOT use) |
| 6 | enemy record `E4/F604 + id*10`, +1/+2 = cursor dX/dY, and the **slot<->combatant alias** | `dis.py 0xCEB50 0x90 1 0`; raw ROM `id 5E = 00 00 F8 10 10 41 70 4D 6B 30`, `id 92 = 02 00 D0 10 10 41 7C 77 B8 A0`; oracle `--monitor "9700:11,970B:11,9716:11,9708:11,9713:11,971E:11,9721:11" --from gato_wait.state --input "100:8,0:400" --diff` -> f292 `$9716[3]=D0` **is the same byte as** `$9713[6]=D0` (both `$9719`), `$9721[3]=10` == `$971E[6]=10` (`$9724`), `$9700[3]=02` == rec+0. Four independent bytes of the record. |
| 7 | 4 cursor OBJs, tile 0x000, attr 0x32, size 16x16, `$0900` bit0/2/4/6 = parked | `--peek forest_alltechs` `.oam` sidecar: `OBJ0(104,55) OBJ1(104,55) OBJ2(72,87) OBJ3(152,119) tile=00 attr=32 X8=0 size=1` |
| 8 | Cyclone set {3,4}: 3 hands on page 0 (`$0900=EA`), 1 on page 3 (`FE`) | reproduced verbatim from §5's command: f365 `$A62D=0304FF...` `$0700=68370032|68370032|48570032|(stale)` `$0900=EA`; f368 `$0900=FE` |
| 9 | Lightning 2 set {3,4,5}: all four sprites (`$0900=AA`) | f414 `$960D=08 $A62D=030405FF... $0700=...|98770032 $0900=AA` |
| 10 | the blink is visibility ONLY | two `--peek`s 4 frames apart: tail `0:5` -> OBJ0-3 all `X8=0`; tail `0:9` -> OBJ0 `X8=0`, OBJ1/2/3 `X8=1`. tile/attr constant `00/32`. |
| 11 | moving the primary rebuilds the set, **primary first** | `$9614` 0->2->1 gives `$A62D` `0304FF` -> `05FFFF` -> **`0403FF`** (primary first, then slot order -- matches `battleBuildHitSet`'s `out[0]=primary`) |
| 12 | GATO single: `$A62D=06FF...`, `$0900=FE`, hide path | f409-f454 `$0700` OBJ0 = `78 3F..46 00 32`; f457 `$0700 = 78F00032 82F00032 7AF00032 82F00032`, `$0900=FF` |
| 13 | `$A62D`/`$99C0` are FF-filled at target-select open and `$960B` is NOT reset | **new disasm** `dis.py 11F80 0x50 1 0`: `11F8F LDX #000B / LDA #FF / STA $99C0,X / STA $A62D,X`, and the STZ list is `$9613/$960C/$960A/$A64F/$A6D8` only. This is what makes the spec's `i < n` equivalent to the ROM's `BMI`. |
| 14 | group dispatch = `($960D & 0x7F)` clamped to 0x20, 33-word table `C1/1FF8` | **new disasm** `11FBC LDA $960D / AND #7F / CMP #21 / BCC / LDA #20 / ASL / TAX / JSR ($1FF8,X)`; the whole table re-decoded byte-by-byte from `dis.py 11FF8 0x46` -- every one of the 33 entries matches §5.1 |
| 15 | handlers `C1/2045` (list), `C1/207E`/`C1/208D` (the `$960C` bit7 fork), `C1/209A/20A9/20B6/20C6`; group 0x12 -> `C1/225F` -> `JSR C1/20A9` ... `$9607=9` (0x19 for 0x1A) -> `JMP C1/27E8` (which forces `$960C=0x80` + full copy) | `dis.py 12045 0x60`, `120A9 0x30`, `1225F 0x50`, `127C5 0x50` |
| 16 | rebuild cadence `CF/E73B` and `$9931` = battle frame index, **1 rebuild : 1 request** | `dis.py 0xFE720 0x40 1 0`; `--monitor "9931:1,993A:1,960B:1"`: `$9931` +1/frame (0x74,0x75,...); over f365-f483 there are **30** frames with `$9931&3==0` and **exactly 30** `$960B` toggles. The 3/5/6-frame gaps in a per-frame sample are consume phase, as §4 says. |
| 17 | `$9931` zeroed at `CC/F0E6` | `dis.py 0xCF0E0 0x14 1 0` -> `CF0E6 STZ $9931` |
| 18 | Crono tech->group table (§5.1) | re-measured all 8 rows independently (`--monitor "9EE3:7"`, 0..7 Down): `01120302 / 020B0102 / 03070002 / 04070004 / 05080008 / 0603800A / 0707800C / 08088014` -- exact |
| 19 | side finding C7 (`battleMoveExtent`) | `--read "971E+11" --from forest_battle_3hetake.state --settle 60` -> `0808081010100000000000`. Real bug. |
| 20 | the defect reproduces in our engine | `CT_BATTLE_TRACE=1 CT_BATTLE_SCRIPT="n:40,d:2,n:8,a:2,n:8,a:2,n:120"` -> `techtrace f220 tech-confirm ... 9609=1 960D=12`; the f230 shot has the hand ONLY at (104,55) (96 white px) and nothing at (72,87) / (152,119) (5 px = background). NOTE: the dossier's repro command omits `CT_BATTLE_TRACE=1`, without which no `techtrace` line is printed. |

### REFUTED

**R1 -- §7 "Byte-identical on both pinned fights ... GATO -> (120,70) = today's anchorY+7". FALSE.
Our engine renders the GATO cursor at (120,77).**
Template-matched the exact 16x16 tile-0x000 quad (rebuilt from `ctBattleStanceObjTiles` + `ctBattleStanceObjPal[1]`)
in our own shots:
```
CT_BATTLE_SCRIPT="n:240,a:2,n:400" CT_BATTLESHOT_AT=405 --battletest gato_t0.raw.wram  -> hand at (120,77)
                                   CT_BATTLESHOT_AT=396 (pre target-select)            -> no hand
```
Arithmetic: today's path is `anchorY(63) + CT_TGTCUR_DY(7) + anchorDelta + liveDelta`; with
`CT_EXT_GATO_Y = -48` (field.c:2008) that collapses to `mvLiveY + (-48) + 7`, i.e. today ALREADY contains
the ROM's `-48` and then adds `+7` on top. `mvLiveY[6]=118` at target-select is correct (verified:
`CT_BATTLE_MVTRACE` -> `battlemove f234 slot3 cmb6 live=(136,118)`, held through the f400 target-select),
so the ROM law gives **70** and today's render gives **77**. Adopting the formula moves the GATO cursor
**7 px up**. The oracle agrees with the formula, not with us: at `$1D23[6]=118` the oracle's cursor OAM is
`(120,70)` and its GATO body min-corner is `(104,63)` = exactly `ctBattleStanceActor[2]` with ZERO delta
(`--peek gato_wait.state`, pal-1 OBJs: foot 111 -> body (104,57)/cursor (120,63); foot 113 -> (104,58)/(120,65);
foot 118 -> (104,63)/(120,70)). So the fix is right -- but it is **not** signature-preserving.

**R2 -- §2.1/§7 "The forest column reproduces our baked `ctHetakeCursor` table exactly ... replacing it with the
formula is byte-identical". FALSE for slot 2.**
The baked *table* equals the formula, but the *render* adds `battleEnemyAnchorDelta`, which is `(0,-3)` for
forest slot 2 (`(127-28) - ctHetakeIdle[2].anchorY(102) = -3`). Measured with the same template match:
```
$9614=0 -> (104,55)   $9614=1 -> (72,87)   $9614=2 -> (152,116)     [oracle: (152,119)]
```
(`CT_BATTLE_SCRIPT="n:180,a:2,n:20,d:2,n:20,d:2,n:200"`, shots at f350/f372/f395.) So slot 2 shifts **3 px
down** under the formula (again toward the oracle) -- the pinned signature changes there too.

**R3 -- §7(c) the suggested pin's page labels are INVERTED w.r.t. §7(b)'s own formula.**
`CT_BATTLESHOT_AT` is compared against `h->frames` (field.c:4081). With `page = ((h->frames/4)&1) ? 3 : 0`,
f230 -> 230/4=57 (odd) -> **page 3 = one hand**, f234 -> 58 (even) -> **page 0 = two hands**. The dossier says
the opposite.

### CORRECTIONS the implementer must apply

1. **Drop the "single-target frames are unchanged" premise.** The GATO cursor moves 7 px up and the forest
   slot-2 cursor moves 3 px down. Every `field-battle-*` pin whose shot contains a target-select hand must be
   re-validated against the ORACLE (which agrees with the formula) and re-pinned -- not asserted equal to today.
2. **Swap the page labels in the proposed pin** (or re-derive them from `h->frames`).
3. **`$95D5` gate**: the ROM is `BPL` = bit7 (`mem[0x95D5] & 0x80`), not `>= 3`. Equivalent on today's
   measured values (0x00/0x80/0xFF) but not byte-exact.
4. The 0x08/0x0A branch must be inserted **before** `battleBuildHitSet`'s single-target early return
   (`if ((group != 0x12 && group != 0x1A) || ...) return 1;`), and it is currently **unreachable** in our engine:
   `battleTechGroup` (battle.c:1305) still returns 0x12/0x07/0x01 only (open C8), so Cyclone is the only
   behavior change users will see.
5. Optional byte-exactness note: the ROM latches the OAM x/y **only on rebuild frames** (every 4th); a
   per-frame renderer re-reads `mvLive` every frame. Irrelevant for a static target, <=1 px while one bobs.


---

# IMPLEMENTATION ADDENDUM (session 24) — SHIPPED

`battleTargetCursorRender` (`src/field.c`) was rewritten to the derived law and the per-enemy-id tables were
retired:

* **Origin** = `CtBattleCursorOfs` (new, `src/battle.c`): PC `(0,-16)` (CC/E3C7 + CC/E3CA), enemy = bytes
  +1/+2 of `E4/F604 + id*10`; the hand sits at `(mvLive + ofs - (0x10,0)) & 0xFF`. `ctHetakeCursor` and
  `CT_TGTCUR_DX/DY` are gone, and with them both `battleEnemyAnchorDelta`/`battleEnemyLiveDelta` calls in
  this path (mvLive IS `$1D0C/$1D23`, so the deltas double-counted).
* **Set** = `CtBattleHitSet` (a public wrapper over the existing `battleBuildHitSet`) keyed on
  `$960D & 0x7F`; `battleBuildHitSet` gained the all-enemies groups **0x08/0x0A** (C1/20B6) ahead of its
  single-target early return.
* **Blink** = sprite 0 is always `$A62D[0]`; sprites 1-3 are the 3-entry page window at `((frames>>2)&1)?3:0`.
* Head gate = the ROM's `LDA $95D5 / BPL` bit7 test (the verifier's correction), not `>= 3`.
* PC-side groups (`$960D` bit7, e.g. an item's 0x80) now draw the hand over the SELECTED ALLY
  (`h->itemTgtSel`) instead of reading `$9614` as an enemy slot — the divergence §5 listed under the same
  root cause.

**Validated** by template-matching the tile-0x000 quad in our own dumps: forest Cyclone target-select f234
(page 0) = hands at **(104,55) AND (72,87)**, f230 (page 3) = the primary only, slot2 (152,119) never (out
of radius) — the ROM positions exactly. GATO's single-target hand moved (120,77) → **(120,70)**, i.e. onto
the oracle's own OAM coordinate; the verifier's R1/R2 warning was right that this is a real move, and no
pinned check contained a target-select frame, so the suite stayed green. Pinned as
`field-battle-tgtcursor-cyclone-multi`.


---

# 10 — THE PAGE ON THE MENU PASS (2026-10-15; closes §8 C3)

The port drew sprites 1-3 on `((CtBattleRenderClock >> 2) & 1) ? 3 : 0` -- a render clock standing in for `$960B`. On
the rule's cartridge the page runs on the fight's own menu passes, and the render clock was one frame early: Area Bomb's
select (tools/caster_radius_dispatch_cmp.py's fixture, `--monitor 7E0900:1,7E960B:1`) has `$0900 = EA` (page 0)
f737-740 / f745-748 and `FE` (page 3) f741-744; the port's page-3 frames were f740-743 / f748.

**The ROM (§3, re-read 2026-10-15).** `C1/1AF8 STA $960B` is the byte's only writer in the whole ROM (the stores `8D/8E/9C
0B 96` occur at C1/1AF8 alone; the other byte matches, F4/C596 and FD/97A8, are outside the battle). The multi arm is taken
by `C1/19AC LDA $A62E / BMI` -- a SECOND lit actor, whatever the group: Cyclone around a lone enemy (`$A62D = 03 FF`) runs
the single arm C1/19B4 and does not turn the page. C1/1AA2-1AF8 with the 16-bit index: `$80 := $960B`, `$82 := 1`; per
entry `INC $80`, out at `$80` = 3 or 6, else `INC $82`, out at `$82` = 4; then `$960B := $80`, or 0 when it is 6. So from
page 0 the next is 3, from 3 it is 0 (and any other start ends at the next of 3 / 6, `$82` capping three sprites).
The battle's boot clears it with the rest of its page (no store names it: a block clear -- a `05` poked into the field's
WRAM reads `00` from the cartridge's f271 on; the port's entry clears the same page at its f216, before any select), and
after that nothing resets it: not the select's open C1/1F79 (§8 C13), not the single arm -- it carries over from one select
to the next.

**The port.** `battleMenuPassTail` (C1/17DD) runs C1/1979's gate -- a chooser (`$95D5` bit 7 clear), `$9609`, `$9613`
bit 7 clear -- and, when `CtBattleTargetHandSet` lights two or more (C1/19AC), the page loop above on WRAM `$960B`,
keeping the page it started at in `h->handPage`. The battle NMI ships it with the bracket's OAM entries
(`h->handPageShown`, beside `brkOamShown`), and `battleTargetCursorRender` draws sprites 1-3 from that page, the loop's
own end (entry 3 / 6). `CtBattleTargetHandSet` (battle.c) is the one set the pass and the renderer read: the PC-side and
caster-radius groups' published `$A62D`, the hit set around the cursor's slot otherwise.

**Measured** (`python tools/target_hand_page_cmp.py -v`, the rule's cartridge, every frame the select is held):

| case | `$960B` | turns (both sides) | shown sprites, port f = cartridge shadow f-1 |
|---|---|---|---|
| Area Bomb (group 11, `$A62D = 04 06`), 72 frames | identical | f737 (the open's own pass) f741 f745 ... f805 (18) | 71/71 |
| Cyclone on cmb3 (group 12, `$A62D = 03 06`), 135 frames | identical | f665 (the open's own pass) f672 f676 ... f796 | 134/134 |
| Cyclone with nobody near (`$A62D = 03`), 75 frames | identical, no turn | - | 74/74 |

The old build: 3/9 (only the no-turn case: its `$960B` never moved, and its sprites differ from the first turn on --
Area Bomb f740, Cyclone f668). The picture alignment:
the cartridge's frame N shows the shadow of N - 2 and in a fight the port's picture P is the cartridge's P + 1, so the
port's frame f must draw the shadow of f - 1 -- the thumbnails' secondary-hand cell turns on and off on the same frames at
P + 1 (BATTLE_AB_KEEP=1), and the sprite match is 71/71 at lag -1 against 54/71 at 0.

`field-battle-tgtcursor-cyclone-multi` (forest Cyclone, `--battletest`) keeps its pin: the page-0 window around its shot
moved from f221-224 to f223-226, and f223 is still a two-hand frame, fnv 3E6AD273 unchanged.

**Found in passing, not this section's** (each its own follow-up):
- ~~*The hand's visibility at the select's open.*~~ **CLOSED 2026-10-15, §11**: the hand is OAM entries 0-3 as the pass
  placed them and the NMI shipped them, positions and visibility too.
- *The enemy cursor's Down order.* On the fixture's Hetakes the cartridge's Down from cmb3 goes to cmb6, then cmb5, cmb4
  (`$A62D` 0306 -> 0603 -> 05 -> 04); the port's walks slots 4, 5, 6 (C1/176C's next-target rule).

# 11 — THE HAND IS OAM ENTRIES 0-3 AS THE PASS LEFT THEM (2026-10-15; closes §10's first follow-up)

§10 shipped the hand's page through the NMI, but the renderer still read the live `$9609`, `$95D5`, the set and the feet
every frame: the hand appeared on the frame the select opened (Area Bomb: port f737, the cartridge's picture f739 = the
port's f738) and left on the frame `$9609` fell, and its positions followed the feet between passes. On the ROM the hand
is four OAM entries that only the menu pass writes; the NMI's DMA ships the shadow as the pass left it.

**Who writes entries 0-3, per pass** (the `$993A` pass C1/10C6 / C1/10E3 / C1/10FA: C1/1B67, C1/0C2D, C1/1153, C1/17DD):

| step | condition | entries 0-3 |
|---|---|---|
| C1/1153 (before the key) | `$95D5` bit 7 | none (C1/179C) |
| | `$9609` != 0 | none here -- C1/1561, the select's key |
| | else | C1/1167 `JSR C1/1115`: the bracket, `$0900 = AA` |
| C1/1561's cancel C1/1577 (B at C1/156E, or the `$9613` veto C1/1564-156C) | | C1/15D0-15E3: `$0900 |= 55`, Y = F0 -- all four parked |
| C1/161A (the confirm) | | none; C1/1725-1742 makes `$95D5` the next queued PC (`$A6D9`), FF when there is none |
| C1/17DD (after the key) | `$95D5` bit 7 | C1/1AFE: all four parked, Y = F0 |
| | `$9609` != 0, `$9613` bit 7 | none (C1/1979 -> C1/1AFB) |
| | `$9609` != 0 | C1/1979: all four parked (C1/198A), tile 00 / attribute 32, then the single arm C1/19B4 (sprite 0, `$0900 AND FE`) or the multi arm C1/1A7B (sprite 0 + the page into 1-3, `$0900 AND CC:FB8D[k]` = FE FB EF BF) |
| | `$95DB` = 1 | C1/17FE: C1/1115 again (C1/18AA) or the partners' hands |
| | `$95DB` = 2 / 0 | none (C1/18B0 / C1/1B14) |

So the select's open pass writes the bracket and then the hand over it (the NMI ships the hand); its confirm parks them
(C1/1AFE) when nobody is queued -- with a PC waiting, `$95D5` is already that PC and C1/17DD writes nothing (`$9609` 0,
`$95DB` 0 -> C1/1B14), so the hand stays a pass and the bracket comes up on the next pass's C1/1115; a cancel parks them in C1/1577 -- the Att's (`$95DB` = 0) then shows nothing until the next pass's C1/1115
brings the bracket back, a tech's (`$95DB` = 1) gets the bracket on the same pass from C1/17FE.

**The port.** `battleMenuPass` takes C1/1153's arm before the key (`battleBracketPlace` = C1/1115, the gates left to its
callers); `battleMenuPassTail(h, b1115)` runs C1/17DD on the state after it and keeps the bracket's entries (`brkOam`,
`brkOn`) and the hand's (`handOam[4]` = x, y and the combatant; `handVis` = the entries whose `$0900` X-high bit C1/1979
cleared; `handPage`); `battleTargetHandBuild` is C1/1979's two arms with the page loop, `battleTargetCancel` parks both
(C1/15D0); `battleNmiHead` ships `handOamShown` / `handVisShown` / `handPageShown` beside `brkOamShown`; and
`battleTargetCursorRender` draws the shipped entries (3 down to 0: the lower OAM index in front), nothing live. Before, the
bracket was `CtBattleBracketBuild` on the state after the key, which put it back on the Att cancel's own pass (f686, the
cartridge's f690).

**Measured** (`python tools/target_hand_page_cmp.py -v`, the rule's cartridge; the shown sprites = the cartridge's entries
0-3 holding tile 00 / attribute 32 with the X-high bit clear, against CT_BATTLE_HANDTRACE, from two frames before the select
to nine after it, port f = cartridge shadow f-1):

| case | frames graded | hand shown, both sides | result |
|---|---|---|---|
| Area Bomb, confirmed | f735-f817 | f738-f809 | 83/83 |
| Cyclone on cmb3, confirmed | f663-f808 | f666-f800 | 146/146 |
| Cyclone, nobody near | f663-f748 | f666-f740 | 86/86 |
| Area Bomb, cancelled (back to the tech list) | f735-f789 | f738-f781 | 55/55 (cartridge `$0900 = AA` at the cancel's f781) |
| Att, cancelled (back to the command row) | f639-f694 | f642-f686 | 56/56 (cartridge `$0900 = FF` f686-689, `AA` f690) |
| Att confirmed, Marle already queued | f639-f691 | f642-f687 | 53/53 (the confirm's pass f682 keeps the hand: `$95D5 = 01`, `$0900 = FE`) |

`$960B` and its turns stay identical in every case (18/18 lines). Before this section (a3bce24's build on the same tool:
12/18) every case drew the hand a frame early at the open (on the frame `$9609` rose: f737 / f665 / f665 / f737 / f641 /
f641, the cartridge's shadow had none) and dropped it a frame early at the close (f808 against f809 ...), the queued
confirm five frames early (f682 against f687: the pass the hand stays). The queued confirm, measured on its own (Crono + Marle from gato_wait, both
ready at f415; Att opened f641, confirmed f653): the cartridge's `$95D5 = 01` from f653 with `$0900 = FE` and the hand's
entries through f658, Marle's bracket `A4 4B 06 32 ...` at f659; the port's CT_BATTLE_TRACE `bracket` seat1 on battle f443 =
field f659. The bracket after the Att cancel: the port's CT_BATTLE_TRACE `bracket` on battle f474 = field f690, the cartridge's f690.
