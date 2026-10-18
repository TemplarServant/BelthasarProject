# Battle TARGET-SELECT on the PC side -- groups 00/80, 01/81, 03 (items + techs)

Derived from the ROM disassembly (`python tools/dis.py <fo> <len> 1 0`, fo = `(bank-0xC0)<<16 | addr`; the whole
block runs M=1 / X=0) and measured in the LakeSnes oracle under the timing rule (`CT_ORACLE_LOGICAL=1
CT_ORACLE_VIRTUAL_BEAM=1`, `ct_tgt_probe.exe` = a copy of `build/ct.exe`). Fixture: `saves/battle_re/gato_wait.state`
(the field before the A that starts GATO), party/inventory/HP poked into the FIELD WRAM with `PEEK_POKE`.
Companion dossiers: `battle_multitarget.md` (group decode, radius law), `battle_multitarget_cursor.md` (the hand
OBJ + page blink), `battle_item_slice.md` (item staging / resolver / queue), `battle_ui_sfx_sites.md` (the blip and
the `$EE/$EF` button map), `battle_end_ui_teardown.md` §1.5-1.6 (the auto-cancel tail).

---

## 0 -- THE LAWS (what an implementer codes)

1. **One machine, rebuilt every menu tick.** Target-select is `$9609 != 0`. Its handler `C1/1561` runs on the
   menu tick (every 4th battle frame, the `$993A` request -- hook-measured, §3.4) and FIRST re-runs the builder
   `C1/1F79`, so the eligible list `$99C0` and the highlight `$A62D` are recomputed from live RAM on every tick
   while the cursor index `$9614` is kept.
2. **The group byte** `$960D` (item `CC/1BB5[id-0xBC]` via `$1581`; tech `CC/1ACB[id*2]` via `$9EE4`):
   `handler = C1/1FF8[min($960D & 0x7F, 0x20)]`. **bit7 changes only the WINDOW**, not the set: on the first
   build with `$A86B == 0` it stashes `$A86B = $95DB + 1`, zeroes `$95DB`, `$A6DF = FE`, `INC $A09A`, and swaps the
   bottom window to the PC status window (`C1/1C3A`: `D1/5800` -> `$0B40`, 0x180 bytes). 00 and 80 build the SAME
   list; 01 and 81 build the SAME list.
3. **Eligible PC (groups 00/80/01/81/04, builder `C1/2045` over seats 0..2):** present `$96F5,X != 0` AND
   `$9FF7,X` bit7 clear AND (`$A0A8,X == 0` OR `($960D & 0x7F) == 04`) AND `$A09B,X == 0`.
   `$A0A8,X` is what removes a DEAD PC (§2.3): it is `1` once the status scan `C1/2F97` has seen the PC dead
   (block `+0x1A` bit7, `$5E4A + 0x80*k`) or doomed (`$A119,X`), and is cleared by `C1/308C`.
4. **List ORDER / INITIAL cursor (00/80, 01/81):** the CASTER first (`$960F` = `$95D5`, when it passes the gates it
   goes to `$99C0[0]`), then the other eligible seats in seat order; compacted (`C1/27C5`), FF-padded to 11.
   `$9614` is 0 at every open, so **the initial cursor is the caster** (or the first eligible seat if the caster
   fails the gates). Measured: Crono caster `00 01`, Marle caster `01 00`, Lucca caster `02 00 01`.
5. **Eligible for group 03 (`C1/20E0`, Revive / Life / Life 2):** seat k with `$96F5+k != 0` AND `$A09B+k == 0`
   AND block `+0x1A` bit7 SET (dead). NO `$9FF7` / `$A0A8` test, NO caster-first: pure seat order, so **the
   initial cursor is the lowest dead seat**. Measured: Marle dead `01`; Marle+Lucca dead `01 02`.
6. **Arity:** 00/80/03 are single (`INC $960A`; `$A62D[0] = $99C0[$9614]`, rest FF). 01/81 are ALL
   (`$960C = 0x80`; `$A62D[0..10] = $99C0[0..10]`; no navigation at all, no nav blip).
7. **Navigation (single groups only, `C1/15F4` gate `$960C` bit7):** Right or Down (`$EF & 05`) -> `C1/176C`
   `INC $9614`, wrap 0x0B -> 0, skip entries with bit7; Left or Up (`$EF & 0A`) -> `C1/1786` `DEC $9614`, wrap
   -1 -> 0x0A, skip bit7. So the cursor cycles through the compact list in list order, both ways, wrapping.
   Each press posts the UI blip (`JSR C1/1B55`) even when the list has one entry and nothing moves.
   **There is NO side switch**: `$960D` is written only by the three openers (`C1/12AA`, `C1/1376`, `C1/14BF`)
   and no handler range spans both sides for a single group; the enemy side is unreachable from a PC group and
   vice versa (measured: Up/Down/Left/Right on Tonic only ever reach 00/01).
8. **EMPTY set = the confirm does not go through.** After the handler, `C1/1FCC` scans `$A62D[0..10]`; if every
   entry has bit7 set, `$9613 = FF` (`C1/1FD9`). The openers (tech `C1/1379`, item `C1/14C5`) run `C1/1F79`,
   test `$9613`, and on bit7 do `STZ $9614` and skip `INC $9609`: **target-select never opens**, the submenu
   stays up with its cursor where it was, nothing is consumed, no MP spent, no action queued. The only sound is
   the generic UI blip that the submenu's A handler posts BEFORE the veto (`C1/1443` items / `C1/1334` techs ->
   `C1/1B55`, cmd `0x19` id `00`); there is **no buzz/refusal sfx**. Measured for Revive and for Life with nobody
   dead (§5.3).
9. **The set emptying WHILE open = silent auto-cancel.** `C1/1564`: `$9613` bit7 -> `STZ $9614` and the cancel
   tail `C1/1577` (no blip) -- `$9609 = 0`, `$95DB` restored from `$A86B` when stashed. Also fires when the list
   merely SHRINKS under the cursor (`$99C0[$9614]` becomes FF). Measured (§5.5).
10. **Buttons in target-select (`C1/1561`, in this order):** B (`$EE & 08`) -> blip + cancel `C1/1577`;
    A or X (`$EE & C0`) -> blip + confirm `C1/161A`; then (single groups) the nav of law 7.
11. **Confirm writes** (`C1/161A`-`C1/174B`): `$95DB = $A86B - 1` if stashed; `DEC $9609`; item: `DEC $1583,X`
    (consume); the seat's 7-byte queue record at `$93EE + CC/FAF0[seat]` (0/7/14):
    `+0 |= 0x40 (&0x7F)`, `+1 = $80 | $9F38[seat]` (0x80 Att / 0x20 Tech / 0x40 Item), `+2 = $9615` (0 Att / 1
    Tech / 2 Item), **`+3 = $A62D` (the primary combatant index; a PC = its seat 0..2)**, `+5 = $82` (FF item /
    `$9EE7` tech), `+6 = $84` (item id / tech id). For ALL groups `+3` is just `$A62D[0]` (the caster, first in
    the list); the set is rebuilt at dispatch (law 12).
12. **Dispatch re-resolves the target from the GROUP** (`C1/C1DD`, entered from the item path `C1/BA83` and the
    tech path `C1/BB49`, which first set `$B2AE = $AD8E = record+3`, `$AD8D = 1`): group `& 0x7F` =
    - **00 (and 05/06):** `$AD8E = C1/C8F7($B2AE)` (identity for a PC: `$AEFF,X != FF`, else `$AE6D,X` which is
      `00 01 02 ...` measured); **target dead (`+0x1A` bit7) -> `C1/C72B` `$AF23 = 1` = the action is DROPPED**
      (no `$A013` span, no effect, the item stays consumed, the turn is spent -- measured §5.6);
    - **01:** `$AD8E` = every seat 0..2 with `$AEFF,X != FF` in SEAT order (not caster-first); none -> dropped;
    - **03:** `$AD8E = C8F7($B2AE)`; target **NOT** dead -> dropped (a Revive whose target got up first);
    - **04:** all three seats unconditionally.
    Then `$AECC = $AD8E` (`C1/BAEE`), `$AE95 = 0x8000 >> $AECC[0]` (`C1/AD09`), `$AE99 = OR of 0x8000 >>
    $AECC[i], i = 1..n-1` (`C1/AD35`), and `CC/F18A`: `$A017 = index of the top bit of $AE95` (the primary),
    `$A018.. = the bits of $AE99 in combatant order`. Measured: Tonic->Marle `$AE95=0x4000 $A017=01`; Lapis
    (either caster) `$AE95=0x8000 $AE99=0x4000 $A017=00 $A018=01`; Revive->Marle `$AE95=0x4000 $A017=01`.

---

## 1 -- THE ENTRY `C1/1F79` (runs at every open AND every tick)

`dis.py 11F79 0x64 1 0`:
```
11F79 LDA $95D5 / TAX / STX $960F        ; $960F (word) = the chooser seat = the CASTER  ($9610 = 00, measured)
11F80 STZ $9613 / STZ $960C / STZ $960A / STZ $A64F / STZ $A6D8
11F8F LDX #000B / LDA #FF / STA $99C0,X / STA $A62D,X / DEX / BPL   ; FF-fill both 12-byte tables
11F9D LDA $960D / BPL ->11FBC            ; bit7 clear -> no window stash
11FA2 LDA $A86B / BNE ->11FBC            ; already stashed (every tick after the first)
11FA7 LDA $95DB / INC A / STA $A86B      ; ** stash the submenu level + 1 (2 = Tech, 3 = Item) **
11FAE STZ $95DB                          ; the submenu is "closed" while the PC window is up
11FB1 LDA #FE / STA $A6DF / INC $A09A / JSR C1/1C3A   ; C1/1C3A: D1/5800 -> $0B40 (0x180) = the PC status window
11FBC LDA $960D / AND #7F / CMP #21 / BCC / LDA #20 / ASL / TAX / JSR ($1FF8,X)   ; the handler
11FCC TDC / TAX
11FCE LDA $A62D,X / BPL ->11FDC           ; any valid entry -> keep $9613
11FD3 INX / CPX #000B / BNE ->11FCE
11FD9 STA $9613                          ; ** all 11 bit7 -> $9613 = FF (the "empty set" flag) **
11FDC RTS
```

### 1.1 RAM map
| addr | meaning | writers |
|---|---|---|
| `$9609` | target-select open (0/1) | INC `C1/12B6` (Att) / `C1/1389` (Tech) / `C1/14D5` (Item); DEC `C1/157D` (cancel) / `C1/1626` (confirm); STZ `C1/1BFC` |
| `$960D` | the target GROUP (bit7 = PC-window stash, bits 0-6 = handler index) | `C1/12AA` (#07) / `C1/1376` (`$9EE4`) / `C1/14BF` (`$1581,X`) only |
| `$960F` (word) | the caster = `$95D5` | `C1/1F7D` |
| `$960A` | single-cursor group flag (INC by the single handlers, 1 while open) | `C1/1F86` STZ, handlers INC |
| `$960C` | 0x80 = ALL group (whole list highlighted, no nav) | `C1/1F83` STZ, `C1/209A/20B6/20C6/27E8` |
| `$9613` | bit7 = the eligible set is EMPTY (FF; 0x80 transiently from 20E0/2136) | `C1/1F80` STZ, `C1/1FD9`, `C1/2128`, `C1/2156` |
| `$9614` | cursor = INDEX into the compact list `$99C0` (NOT a combatant, NOT an enemy slot) | STZ at every close/veto (`C1/1381/14CD/1569/1577/16D4/1C02`); nav `C1/176C/1786` |
| `$9615` | action kind for the queue record +2: 0 Att / 1 Tech / 2 Item | `C1/12AD` / `C1/12DC` / `C1/12F7` |
| `$99C0[0..10]` | the compact eligible list (combatant indices), FF-padded | the handlers |
| `$A62D[0..10]` | the highlight / primary: single `[$99C0[$9614], FF..]`, all = copy of `$99C0` | `C1/2085`, `C1/208D`, `C1/1780/1799` |
| `$A86B` | 0 = no stash; else the stashed `$95DB + 1` | `C1/1FAB`; STZ `C1/158C` (cancel) / `C1/1623` (confirm) / `C1/1BF6` |
| `$A09A` | window-paint mode flag, 1 while stashed | `C1/1FB6` INC; STZ at cancel/confirm |
| `$A4EE` | "target changed" request for the readout-box side `$EC` (`C1/1A53`) | INC at opens and nav |

---

## 2 -- THE HANDLERS

Table `C1/1FF8` (33 words, raw ROM): `00:203A 01:209A 02:20D6 03:20E0 04:209A 05:2136 06:2163 07:20A9 08:20B6 09:20C6
0A:20B6 0B:2169 0C:2203 0D:21AF 0E:203A 0F:22F5 10:203A 11:224B 12:225F 13:22A4 14:22A4 15-17:203A 18:2329
19:203A 1A:225F 1B:22D3 1C-20:203A`.

### 2.1 `C1/203A` (00/80, also 0E/10/15-17/19/1C-20) and `C1/209A` (01/81, 04) -> the shared builder `C1/2045`
`dis.py 1203A 0x70 1 0`:
```
1203A LDX #0003 / STX $80 / LDX #0000 / INC $960A     ; range [0,3) PCs, SINGLE
1209A LDX #0003 / STX $80 / LDX #0000 / LDA #80 / STA $960C / BRA 12045   ; range [0,3), ALL
12045 TDC / TAY
12047 LDA $96F5,X / BEQ skip            ; present
1204C LDA $9FF7,X / BMI skip            ; hidden/untargetable
12051 LDA $A0A8,X / BEQ ->1205F
12056 LDA $960D / AND #7F / CMP #04 / BNE skip   ; $A0A8 set -> skip, EXCEPT group 04
1205F LDA $A09B,X / BNE skip
12064 CPX $960F / BNE ->12071
12069 LDA $960F / STA $99C0 / BRA skip  ; ** the CASTER goes to slot 0 **
12071 TXA / STA $99C1,Y / INY           ; everyone else appends from slot 1, seat order
12076 skip: INX / CPX $80 / BNE ->12047
1207B JSR C1/27C5                        ; if $99C0 is still FF (caster not eligible / not in range): shift $99C1.. down 1
1207E LDA $9614 / TAX / LDA $99C0,X / STA $A62D    ; primary = list[cursor]
12088 LDA $960C / BPL rts / LDX #000A / copy $99C0[0..10] -> $A62D[0..10]   ; ALL groups
```
(For the enemy ranges `[3,0x0B)` the caster is never in range, so `C1/27C5` always shifts -- the known enemy order.)

### 2.2 `C1/20E0` (03) -- one DEAD PC
`dis.py 120E0 0x56 1 0`:
```
120E0 INC $960A / TDC / TAX
120E5 LDA $96F5 / BEQ / LDA $A09B / BNE / LDA $5E4A / BPL / STZ $99C0 / INX      ; seat 0 dead?
120F8 LDA $96F6 / BEQ / LDA $A09C / BNE / LDA $5ECA / BPL / LDA #01 / STA $99C0,X / INX
1210D LDA $96F7 / BEQ / LDA $A09D / BNE / LDA $5F4A / BPL / LDA #02 / STA $99C0,X
12121 LDA $99C0 / BPL / LDA #80 / STA $9613     ; nobody dead
1212B LDA $9614 / TAX / LDA $99C0,X / STA $A62D
```
`$5E4A/$5ECA/$5F4A` = battle block `+0x1A` of seats 0/1/2 (block base `$5E30 + 0x80*k`; the field sheet's `+0x1D`).
Built directly compact in seat order; the caster plays no role.

### 2.3 The gate bytes, measured (Crono + Marle, Marle dead, GATO = combatant 6)
```
$96F5[0..10] = 01 01 00 | 00 00 00 01 00 00 00 00     (CC/E63D INC at f291)
$9FF7[0..10] = all 00                                 (enemies: CC/EABA from $29C6 bit7; anim ops C1/585A/586E/5D0A)
$A0A8[0..2]  = 00 01 00   from f389, writer C1/2FCB   (C1/2F97: `LDA $A119,X / BNE` or `LDA $5E4A,Y / BPL` -> #01)
$A09B[0..2]  = 00 00 00                               (CF/FBD0: $7F00B4[$2980,Y & 7] when `$A0A6` bit0 -- a per-fight mode, meaning not pinned)
$AEFF[0..2]  = 00 FF FF   from f372 (C1/B2C0)         (the dispatch's alive list; NOT read by target-select)
$5ECA        = 80
```
`$A0A8,X` lifecycle: SET by `C1/2F97` (reached from the ATB countdown `C1/2E28` when `$9689,X` hits 0, and from the
action-close status scan `C1/305C` -> `C1/3067`) when the combatant's block `+0x1A` bit7 is set or `$A119,X != 0`
(`$A119/$A11A/..` = "this PC takes lethal damage from the pending action", `C1/41BE`); CLEARED by `C1/308C`
(`STZ $A0A8,X` at `C1/30A4`) when that combatant's icon latch changes. Measured on Revive: Marle's `+0x1A` bit7 cleared
at dispatch f693 (writer `C1:DB78`), `$A0A8[1]` cleared only at f909 (`C1/30A4`, the action close) and `$AEFF[1] = 01` at
f912 (`C1/B28E`). So between a revive's dispatch and its close a Tonic list would still exclude her.

### 2.4 Other PC-side handlers (disasm only, for completeness)
- `02 -> C1/20D6`: `$99C0 = $A62D = $95D5` (self), no flag -> single self.
- `05 -> C1/2136`, `06 -> C1/2163`: the seat whose `$2980,X == 05` (Frog) / `04` (Ayla), gates `$96F5`/`$A09B`; none
  -> `$9613 = 80`.
- `04 -> C1/209A`: all PCs, and the only group that ignores `$A0A8` (dead PCs stay in; dispatch `C1/C3B8` takes all 3).
- `09 -> C1/20C6`: `[0,0x0B)` ALL = everyone.

---

## 3 -- THE PER-TICK HANDLER `C1/1561` and the openers

### 3.1 `C1/1561` (`dis.py 11561 0xC0 1 0`)
```
11561 JSR C1/1F79                       ; rebuild
11564 LDA $9613 / BPL ->1156E
11569 STZ $9614 / BRA ->11577           ; EMPTY -> cancel tail WITHOUT the blip
1156E LDA $EE / AND #08 / BEQ ->115E8   ; B
11574 JSR C1/1B55                       ; blip
11577 STZ $9614 / STZ $960E / DEC $9609 ; ** close **
11580 LDA $A86B / BEQ ->115C8
11585 DEC A / STA $95DB / STZ $A09A / STZ $A86B     ; restore the submenu level
1158F LDA $95DB / DEC A / BEQ ->115AE
11595   (item) copy D1/5BD0 -> $0B40 (0x180), $9920 = FF, INC $99E2     ; repaint the item window
115AE   (tech) STZ $A869 / INC $A86A                                    ; repaint the tech list
115C8 LDA $95DB / BNE / INC $A43F
115D0 park cursor OBJs 0-3 ($0900 |= 55, Y = F0) / BRA ->11617
115E8 LDA $EE / AND #C0 / BEQ ->115F4   ; A or X
115EE JSR C1/1B55 / JMP C1/161A         ; confirm
115F4 LDA $960C / BMI ->11617           ; ALL group: no navigation
115F9 LDA $EF / AND #05 / BEQ           ; Right(01) | Down(04)
115FF INC $A4EE / JSR C1/1B55 / JMP C1/176C
11608 LDA $EF / AND #0A / BEQ ->11617   ; Left(02) | Up(08)
1160E INC $A4EE / JSR C1/1B55 / JMP C1/1786
11617 JMP C1/179C                       ; STZ $EE / STZ $EF
```
`$EE/$EF` = the battle's edge pair with auto-repeat (`battle_ui_sfx_sites.md` §2.4): A=`$EE 80`, X=`40`, B=`08`;
Up=`$EF 08`, Down=`04`, Left=`02`, Right=`01`.

### 3.2 Nav `C1/176C` / `C1/1786`
```
1176C INC $9614 / LDA $9614 / CMP #0B / BNE / TDC / STA $9614 / TAX / LDA $99C0,X / BMI ->1176C / STA $A62D / JMP 179C
11786 DEC $9614 / LDA $9614 / BPL / LDA #0A / STA $9614 / TAX / LDA $99C0,X / BMI ->11786 / STA $A62D
```

### 3.3 The openers (the empty-set VETO)
```
Tech  C1/1369: LDA $A099 / BEQ skip / LDA $9EE5 / BMI skip / LDA $9EE4 / STA $960D / JSR C1/1F79
      C1/137C: LDA $9613 / BPL ->11386 / STZ $9614 / BRA skip   |  11386 INC $A4EE / INC $9609
Item  C1/1498: idx=$95E6+$95E5, X=idx*5 ; LDA $1582,X / BMI skip ; LDA $1583,X / BEQ skip
      C1/14B6: $9F35 = $1580,X ; $960D = $1581,X ; $9F36 = X ; JSR C1/1F79
      C1/14C8: LDA $9613 / BPL ->114D2 / STZ $9614 / BRA skip   |  114D2 INC $A4EE / INC $9609
Att   C1/129C: $960D = 07 ; $9615 = 0 ; JSR C1/1F79 ; INC $A4EE ; INC $9609   (no veto; C1/1561 auto-cancels)
```
Side effect to know: for a bit7 group the veto path still ran the `C1/1F79` stash (law 2) -- `$95DB = 0`, `$A86B`
set, PC window up, target-select closed. Unreachable in practice (the caster is always eligible for 80/81), noted.

### 3.4 Cadence (hook-measured)
`--hookpc C11561` visits every 4th frame (f684, 688, 692 ...), the `$993A` menu tick (`C1/10C6`); a press is
latched in `$EE/$EF` until the next tick consumes it. The monitor frames below are one lower than `--hookpc`'s.

---

## 4 -- PRESENTATION HOOKS (what changes on screen)

- **Hand cursor** (`C1/1979`, `battle_multitarget_cursor.md`): PCs use `$9708/$9713 = (0, F0)`, so the hand sits at
  `($1D0C,X - 16, $1D23,X - 16)`. Measured OAM0: Crono (`$1D0C/$1D23 = 86/93`) -> (118,131); Marle (`B0/6F`) ->
  (160,95); a dead Marle (Revive) -> (160,95). Lapis (ALL): OAM0 = primary, OAM1 = duplicate, OAM2 = Marle,
  `$0900 = EA` then `FE` on the next rebuild -- the same 0<->3 page blink as the enemy AoE.
- **Bit7 groups swap the bottom window** to the PC status window (`C1/1C3A`), restored on cancel (item `D1/5BD0`
  / tech list repaint) or dropped on confirm. Group 00 (Heal, Haste) and 03 (Revive, Life) keep the item / tech
  window up (measured `$A86B = 0`, `$95DB` unchanged).
- **Target name lit in the PC window** (bit7 only): the row painter paints every present PC's name with attr
  `0x2D` (`C1/0320/0342`, dim), then `C1/054C` (`LDA $A86B / BEQ`) -> `C1/08E8` repaints the `$A62D` row with attr
  `0x29` (lit), 10 cells at `$0CC0+X` / `$0D00+X` (X from `CC/FA29` / `CC/FA23` by `$9F20`). Buffer measured:
  cursor on Crono -> Crono row `29`, Marle row `2D`; on Marle -> the reverse. The displayed picture follows the
  `$0B40` buffer after its next upload (the f684 frame still showed the item window; f692 the PC window).
- No enemy name/HP readout for a PC target (`C1/19DC CMP #03 / BCC`).

---

## 5 -- ORACLE TIMELINES (monitor frame numbers; drive = `100:8` + neutral to f640 + 2-frame presses every 12)

Common prefix (bash, repo root):
```
cp build/ct.exe "$TEMP/ct_tgt_re/ct_tgt_probe.exe"
export CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1
P="$TEMP/ct_tgt_re/ct_tgt_probe.exe"; S=saves/battle_re/gato_wait.state
M="7E9609:1,7E960D:1,7E960F:2,7E9613:1,7E9614:1,7E9615:1,7E99C0:4,7EA62D:4,7EA86B:1,7E95DB:1,7E95D5:1,7E93EE:14"
```

### 5.1 Tonic (0x80), Crono + Marle alive, Crono chooses; Down Down Up Left Right A
```
PEEK_POKE="2980=00,2981=01,2982=80,2400=BD,2500=05" "$P" --monitor "$M,7E1583:1" --from $S --diff \
 --input "100:8,0:632,20:2,0:10,20:2,0:10,100:2,0:10,100:2,0:10,20:2,0:10,20:2,0:10,10:2,0:10,40:2,0:10,80:2,0:10,100:2,0:10,0:2,0:38"
f667  $95DB=02 $9615=02                                   item list open
f679  $9609=01 $960D=80 $960F=0000 $99C0=00 01 FF.. $A62D=00 FF.. $A86B=03 $95DB=00 $960A=01   (cursor = Crono)
f691  Down  $9614=01 $A62D=01      f703 Down  $9614=00 $A62D=00 (wrap)
f715  Up    $9614=01 $A62D=01 (wrap -1 -> 0A -> skip FF down to 01)
f727  Left  $9614=00               f739 Right $9614=01
f751  A     $9609=00 $A86B=00 $93EE = 41 40 02 01 00 FF BD  $1583 05->04  $95D5 -> 01 (next chooser)
```
Same drive with Marle choosing (`80:2` Right first at command level): f691 `$960F=0100 $99C0=01 00 FF $A62D=01`.
Crono/Marle/Lucca (`2982=02`), Lucca chooses (Right Right): `$99C0=02 00 01`, Down -> 00 -> 01 -> 02.
Tonic with Marle dead (`+2653=00,2654=00,266D=80`): `$99C0=00 FF`, nav does nothing; 3 PCs Marle dead: `00 02`.

### 5.2 Revive (0x03), Marle dead: `$99C0=01 FF`, cursor on Marle, window stays
```
PEEK_POKE="2980=00,2981=01,2982=80,2400=C7,2500=05,2401=BD,2501=05,2653=00,2654=00,266D=80" "$P" --monitor "$M,7EA0A8:3,7EAEFF:3,7E5ECA:1" --from $S --diff \
 --input "100:8,0:632,20:2,0:10,20:2,0:10,100:2,0:10,100:2,0:10,20:2,0:10,10:2,0:10,40:2,0:10,80:2,0:10,100:2,0:10,0:2,0:50"
f679  $9609=01 $960D=03 $99C0=01 FF.. $A62D=01 $A86B=00 $95DB=02
f691/703/715/727  Down/Up/Left/Right: $9614 stays 0 (each press still blips: hook C11B55 + C1176C/C11786)
f739  A     $93EE = 41 40 02 01 00 FF C7   $1583 05->04
```
Marle + Lucca dead (3 PCs): `$99C0=01 02`, Down -> 02, Down -> 01, Up -> 02.

### 5.3 Revive with NOBODY dead -- the confirm does not go through
```
PEEK_POKE="2980=00,2981=01,2982=80,2400=C7,2500=05" "$P" --monitor "7E9609:1,7E960D:1,7E9613:1,7E9614:1,7E99C0:4,7EA62D:4,7E95DB:1" \
 --from $S --input "100:8,0:632,20:2,0:10,20:2,0:10,100:2,0:10,100:2,0:40,1:2,0:40" --diff --writer
f667  $95DB=02 (C1:131D)
f679  A on Revive: $960D=03 (C1:14C2)  $99C0=FF.. (C1:1F97)  $A62D=FF.. (C1:2135)  $9613=FF (C1:1FDC)
      $9614=00 (C1:14D0)  **$9609 stays 00**, $95DB stays 02, $1583 stays 05
f719  B: $95DB=00 (C1:14E1)
```
`--hookpc` on the same drive (`--btnscript` form): `C1/1B55` blips on the A tick (f680) and a repeated A (f692);
`C1/14C5` and `C1/1F79` run on those ticks; **`C1/1561` is never reached**. Tech Life (0x06, group 03) with nobody
dead: identical (f727 `$960D=03 $9613=FF`, `$9609` stays 0, `$95DB` stays 01).

### 5.4 Lapis (0x81) / Aura (tech 0x09, 0x80) / Life (0x06, 0x03) / Heal (0xC6, 0x00)
- Lapis, both alive: f679 `$960C=80 $99C0=$A62D=00 01 FF`, Down/Up do nothing (no blip: hook), A -> record
  `41 40 02 00 00 FF CA`. Marle dead: `00 FF`.
- Aura (Marle chooses): f679 `$A86B=02 $95DB=00 $99C0=01 00 FF $A62D=01`; Down -> 00, Down -> 01; A -> Marle's
  record (offset 7) `41 20 01 01 00 FF 09`.
- Life, Marle dead: f727 `$960D=03 $99C0=$A62D=01`, `$A86B=00 $95DB=01`; A -> `41 20 01 01 00 FF 06`.
- Heal: `$99C0=00 01`, `$A86B=00`, `$95DB` stays 02, Down -> 01, record `41 40 02 01 00 FF C6`.
- B from a stashed select (Aura / Tonic): `$9609=0`, `$95DB` = 01 / 02 restored (`C1:1589`), `$A86B=0`; from Life
  (03): `$9609=0`, `$95DB` untouched.

### 5.5 The set changing under an open select
`tsel_marle.state` = `--peek` + `PEEK_SAVE` of the 5.1 Tonic drive after one Down (f698: cursor on Marle).
Loading it with `PEEK_POKE="5EB0=00,5EB1=00,5ECA=80,AF00=FF,A0A9=01"` (Marle dead, gates set): next tick
f1 `$99C0=00 FF`, `$A62D=FF`, `$9613=FF` (C1:1FDC), `$9609=0` (C1:1580), `$9614=0`, `$95DB=02` (C1:1589),
`$A86B=0` -- a silent auto-cancel back to the item list.

### 5.6 Target dead between confirm and dispatch (group 00): the action is dropped
`conf_704.state` = the 5.1 drive saved after the confirm, before the dispatch (`$93EE = 41 40 02 01 ..`,
`$AE93 = 00`). With the same Marle-dead poke: f5 `$AF23=01` (C1:C730), `$B2AE=01`, `$AD8E` stays FF, `$A013`
never rises, the record is cleared (C1:85D8), Marle stays dead, `$1583` stays 04 (consumed). Without the poke the
dispatch at f705 gives `$AD8E=01` (C1:C573) `$AE95=0x4000` (C1:AD31) `$AE93=BD` and `$A017=01` at f707.

### 5.6b Enemy target dead between confirm and dispatch (one-enemy tech): C1/C82D re-targets (2026-10-18)
C1/C335's dispatch on the tech's group (CC/1ACB & 7F): **00 / 05 / 06** take `$AD8E = C1/C8F7($B2AE)` -- the seat if
present, else `$AE6D,X` (FD/B223's identity) -- and drop on `$5E4A,X` bit 7 (C1/C36E -> C1/C72B, `$AF23 = 1`). **07, 0B-0F,
12, 1A** (C1/C427, C1/C506) take **C1/C82D**: `STZ $AF23`; for a PC (`$B18B < 3`) no present enemy -> `$AF23 = 1`
(C8F1); `$B2AE` present -> kept (C84E-C858); else `$2989` bit 5 picks the first present seat from 3 (C883-C89B, the demo's)
or `LDX #0003 / LDA #0B / JSR C1/AF22` until a present one (C861-C876); the pick into `$B2AE` / `$AD8E`, `$AD8D = 1`. Group
09's arms jump into C82D / C37F / C43B by a C1/AF22 roll. Measured (battle_sim `@013:18`, three Hetakes at 1 HP, Crono's
attack on the first and the next PC's tech on the same): the first falls at f941, the tech's dispatch writes `$B2AE = 04`
at f988 (the first living seat: the roll's pick here). The port took the first living seat without the ROM's writes
(battle.c battleTechC82D / battlePcRetarget now; check battle-sim-tech-retarget-c82d-vs-rule-cartridge). The roll picking
a later seat is not yet measured on a drive.

### 5.7 Dispatch masks (`M2="7EB2AE:1,7EAD8E:3,7EAECB:1,7EAE95:2,7EAE99:2,7EA017:3,7EAF23:1"`)
| action | record +3 | `$AD8E` (writer) | `$AE95` | `$AE99` | `$A017..` |
|---|---|---|---|---|---|
| Tonic -> Marle | 01 | `01` (C1:C573) | 0x4000 | 0 | `01 FF` |
| Lapis, Crono casts | 00 | `00 01` (C1:C39D) | 0x8000 | 0x4000 | `00 01` |
| Lapis, Marle casts | 01 | `00 01` | 0x8000 | 0x4000 | `00 01` (seat order, not the recorded caster) |
| Revive -> Marle (dead) | 01 | `01` (C1:C3EB) | 0x4000 | 0 | `01 FF` |
| Aura -> Crono | 00 | `00` (C1:C573) | 0x8000 | 0 | `00 FF` (Crono HP 1 -> 44 at f973) |

---

## 6 -- WHERE THE PORT DIFFERS TODAY (pointers, `src/battle.c`)

- `battleItemSubmenu` (~4337): A on an item opens target-select unconditionally and sets the PC cursor to the
  chooser; target nav (~4352) walks every PRESENT seat (`$2980` bit7) -- no gates, no group-03 dead-only list, no
  empty-set veto (Revive with nobody dead acts), no auto-cancel, no bit7 window stash.
- `battleTechSubmenu` (~4275): every tech group navigates ENEMIES (`battleTargetNav`); PC groups (Aura/Cure 0x80,
  Haste 0x00, Life 0x03) are not selectable on the PC side and Life is not vetoed.
- The port's `$9614` is an enemy SLOT; the ROM's is an index into `$99C0`.
- `battleStatusScanStore` (~3337, the port of `C1/2F97`) stores `$A440/$A441` but not `$A0A8,X = 1` /
  `$968C,X = FF` for a dead-or-doomed combatant (`C1/2FBD-2FD0`) -- the byte the PC list gate reads.
  `battleIconDispatch` already has the matching clear (`C1/30A4`).
- Dispatch: the group-00/03 target-alive/target-dead re-check (`C1/C348`, `C1/C3DD`, `$AF23` drop) and group 01's
  seat-order rebuild from `$AEFF` (`C1/C37B`).

---

## 7 -- OPEN QUESTIONS

| # | item | why open / what closes it |
|---|---|---|
| O1 | `$9FF7,X` bit7 on a PC | writers are the enemy loader (`CC/EABA`) and anim ops (`C1/585A/586E/5D0A`); never set on a PC in these captures. A cutscene fight that hides a PC would show it. |
| O2 | `$A09B,X` (per-fight PC exclusion, `CF/FBD0` from `$7F00B4[char]` when `$A0A6` bit0) | 0 in every capture; needs a fight with `$A0A6 != 0`. |
| O3 | exact `$A0A8` timing after a MID-battle death | the SET is `C1/2F97` (ATB countdown `C1/2E28` or action-close scan `C1/305C`), the CLEAR is `C1/308C` on an icon-latch change -- disasm-read; only the pre-dead start (f389) and the revive close (f909) were measured. A kill-then-Tonic capture pins the delay between `+0x1A` bit7 and the list drop. |
| O4 | `$A119/$A11A/$A11B` (`C1/41BE`, "lethal damage pending") | read as such from the disasm (type-3 damage `>=` curHP); not exercised. |
| O5 | group 03 on the ENEMY side at dispatch (`C1/C40F`: `$AF15,X` bit7 -> drop) | only PC targets are reachable from 20E0; noted for completeness. |
| O6 | the bit7 stash on a VETOED open (law 8 note) | unreachable with a live caster; not captured. |
| O7 | window pixels | the PC-window swap and the `0x29`/`0x2D` name attrs are measured in the `$0B40` buffer; the exact frame the BG3 upload shows them (f684 not yet, f692 yes) is not pinned. |
| O8 | the `$EC` readout-box side toggle for a PC target (`C1/1A53`: `$0701 < 2D` -> 1, `>= 9C` -> 0) | PC cursors here sit at y 95-131, inside the band, so it never fired. |
