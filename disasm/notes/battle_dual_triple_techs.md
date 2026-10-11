# DUAL / TRIPLE TECHS in battle: boot records -> list -> grey -> nav -> confirm -> queue -> dispatch -> effects

Everything below is from the ROM disasm (`tools/dis.py`, bank C1 at M=8/X=16) and the LOGICAL oracle
(`CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1`, a scratch copy of `build/ct.exe`). Writer PCs are the
instruction after the store. Side files (scratchpad `dual/`): `combo_table.txt` (CC/1BEB decode of techs 39-75),
`learn_table.txt` (CC/2778), disasm dumps `c1c900.txt c1ccfd.txt cc_e700.txt c1_0d40.txt c1_12bc.txt c1_1626.txt
c1_b940.txt c1_d523.txt`, screenshots `p1-p3.png cm_f458.png tri_*.png`, monitors `icesword_mon.txt tri_mon*.txt`.

**Corrections to battle_tech_slice.md:** (a) `$9EE6/$9EE8/$9EE9` are the MP costs of SEAT 0/1/2, and `$9EE7` is the
partner byte. They are not "MP + three partner ids". (b) The single records are NOT packed: slot k = the char's k-th
tech, and `$9551` holds the slot. (c) The grey bit is written into the RECORD (`$1A82,X`) by the grey pass C1/0EE1.
The resolver only copies it to `$9EE5`. (d) The tech list is built ONCE at battle boot (CC/E755), not when the list
opens.

---

## 1. Boot: the per-seat record build (C1/FB5D -> C1/C96A, C1/CA1A, C1/CCCB)

Caller C1/FB5D: `$06 = seat 0..2`, `JSR C96A (singles) / JSR CA1A (duals) / JSR CCCB (triple)` for each seat. It runs
after C1/FB0B-FB2A has filled `$AEFF[seat] = $2980[seat]` (char id). A seat is FF when `$A09B[seat] != 0` or
`$2980` bit7 is set. It also runs after FD/AD09 has filled `$1A80..$1C23` (0x1A4 bytes) with FF (oracle f286,
writer FD:AD10). `$B1BE[char]` = the char's seat, FF when absent (FD:B29D, f285). A seat with `$AEFF = FF` gets no
records.

**Record area.** The base is FD/A84F[seat*2] = `$1A80 / $1B0C / $1B98` (0x8C bytes = 20 slots × 7). Slot offsets
come from CC/F3A1[slot] = slot×7, and the per-seat offsets from CC/F395 = `0000 008C 0118`.
- slots 0-7 hold the singles, at a fixed position: slot k = tech `FD/A89D[char] + k`. FD/A89D = `01 09 11 19 21 29
  31 39`, the same data as C2/BD40. Slots 8-9 are unused.
- slots 10-15 hold the duals, PACKED, at most 6 (C1/CAFF `CMP #06 / BCS RTS`).
- slot 18 (+0x7E) holds the triple. There is only ONE: C1/CDCE `BRA ->RTS` after the first qualifying one. Slots
  16, 17 and 19 are unused.

**Record bytes (7):**

| byte | meaning | source |
|---|---|---|
| +0 | tech id | loop counter |
| +1 | target group | CC/1ACB[id*2] |
| +2 | flags | CC/1ACC[id*2] & 0x3F; bit7 = GREY, set/cleared later by C1/0EE1 |
| +3 | MP cost paid by SEAT 0 | FF = seat 0 pays nothing |
| +4 | partner byte | single: FF. Dual: `F0 \| partner seat`. Triple: `(seatA<<4) \| seatB` |
| +5 | MP cost paid by SEAT 1 | FF = none |
| +6 | MP cost paid by SEAT 2 | FF = none |

The MP bytes are written by C1/CBC3 (single), C1/CC0B (dual) and C1/CC3E (triple). Each takes the participant's char,
finds its seat through `$B1BE`, and stores the cost at +3, +5 or +6 by that SEAT.

The cost of participant k is CC/253B[ CC/1BF0[id*11+k] & 0x7F ] (C1/CB48 k=0, CB61 k=1, CB7A k=2; `id*11` comes from
C1/C90B). Before the store it is scaled by the seat's accessory `$B3BA+seat` (C1/CBF6): A2 gives ceil(c/2), A3 gives
ceil(ceil(c/2)/2).

**Participants** are read by C1/CB93: group = CC/1BEB[id*11] & 0x7F, then `$B2EA/$B2EB/$B2EC` = CC/249F[group*3 + 0/1/2]
(char ids, FF = none).

**Singles (C1/C96A-CA19).** Loop `$08` = FD/A89D[char] .. FD/A89E[char]. Learned bits come from `$2837+char`, MSB first
(`ASL $0C` for each tech). The record pointer `$04 += 7` for EVERY tech, learned or not. A learned tech writes +0,
+1 and +2, sets +3/+5/+6 to FF, writes the caster's MP at its seat byte, and writes +4 = FF.

**Duals (C1/CA1A-CB47).** Ids `$08` = 0x39..0x65 (45 = 15 pairs × 3). Learned bits are
`$283E + pair` (X = `$00`, +1 every 3 techs), bits 7/6/5.

A dual gets a record when it is learned, both `$B2EA` and `$B2EB` are != FF with `$B1BE[...] >= 0`, and THIS seat
(`$06`) is one of the two (C1/CA67-CA96). The record goes at `$04 + 0x46`; byte +4 = `F0 | seat of the other
participant` (C1/CAE4-CAFA).

**Triple (C1/CCCB-CDFE).** Ids 0x66..0x74. Learned = bit7 of `$284D + (id - 0x66)`, one byte per triple. All three
participants must be present. The record goes at `$04 + 0x7E`.

Byte +4 is built from the two OTHER seats (C1/CD9D-CDCB):
- high nibble: `seat($B2EC)`, or `seat($B2EA)` when `seat($B2EC)` is this seat;
- low nibble: `seat($B2EB)`, or `seat($B2EA)` when `seat($B2EB)` is this seat.

The miss path C1/CDD0 runs `ADC #7` with no STA. This is harmless because only one triple is kept.

**Pair / triple order** (CC/1BEB + CC/249F; full list in `combo_table.txt`): `$283E` C+Marle 39-3B, `$283F` C+Lucca
3C-3E, `$2840` C+Robo 3F-41, `$2841` C+Frog 42-44, `$2842` C+Ayla 45-47, `$2843` Marle+Lucca 48-4A, `$2844` Marle+Robo
4B-4D, `$2845` Marle+Frog 4E-50, `$2846` Marle+Ayla 51-53, `$2847` Lucca+Robo 54-56, `$2848` Lucca+Ayla 57-59, `$2849`
Robo+Frog 5A-5C, `$284A` Robo+Ayla 5D-5F, `$284B` Frog+Ayla 60-62, `$284C` Frog+Lucca 63-65; `$284D..$285B` triples 66-74.

Participant ORDER matters for three things: MP (k-th effect index), the actor (participant 0) and the ATB factor.
For example Aura Whirl's group is 0x25 = [Marle, Crono], so Marle is participant 0.

**Which singles unlock which combo.** This is not a battle gate; the battle only reads the bits. It is the
battle-end learner C1/F25F-F2DA + C1/F533 over table CC/2778, which has 25 entries [charmask, first id, count,
ptr16]. The rule is: `$2830[char]` (tech count) >= a threshold byte for each participant in charmask order, and then
`ORA` the bit into `$283E+i`; only entries whose charmask equals one of the party's masks `$B2D6[0..3]` are tried
(C1/F268-F27D). Example: Ice Sword needs Crono count >= 4 and Marle >= 3 (CC/27FA). The full table is
in `learn_table.txt`. The rock triples 70-74 (`$2857-$285B`) are not in CC/2778 (OPEN O7).

**MEASURED, boot records.**

Fixture: `saves/battle_re/gato_wait.state` with
`PEEK_POKE="2837=FF,2830=08,2838=FF,2831=08,2607=63,2608=00,2609=63,260A=00,2657=63,2658=00,2659=63,265A=00,283E=E0"`,
`--input "100:8,0:300"`. At f287-288 (writer C1:CAFD):
```
seat0 $1AC6 (slot10..12): 39 81 00 02 F1 01 FF | 3A 07 00 04 F1 02 FF | 3B 12 03 0C F1 08 FF
seat1 $1B52             : 39 81 00 02 F0 01 FF | 3A 07 00 04 F0 02 FF | 3B 12 03 0C F0 08 FF
seat1 single $1B0C (Aura): 09 80 00 FF FF 01 FF      <- Marle's own cost at +5 (seat 1), not +3
```

Triple party: add `2839=FF,2832=08,26A7=63,26A8=00,26A9=63,26AA=00,283F=E0,2843=E0,284D=80,2982=02`. Lucca joins the
fight in seat 2. At f288-289:
- Crono's duals are 39-3E: exactly 6, so the cap is full. Example: `3C 12 03 02 F2 FF 01` = Crono pays 2, Lucca pays 1.
- The triples are `66 08 00 08 21 08 08` (seat 0), `66 08 00 08 20 08 08` (seat 1) and `66 08 00 08 01 08 08`
  (seat 2).

---

## 2. Boot: the display list (CC/E755-E8D0, oracle writers CC:E773/E822/E89B/E8A2/E8C7, f290-292)

1. For each seat, set `$9F25[seat] = 1` when `$1AC6+X` (dual slot 10) >= 0, and `$9F28[seat] = 1` when `$1AFE+X`
   (triple) >= 0 (CE755-CE77E).
2. The list itself: Y = CC/F38C[seat] = `00/14/28` (20 entries per seat in `$94D0` and `$9551`). `$94D0..` was
   pre-filled with FF by CC:E348.
   - Singles, slots 0..7: if `record+0 != FF`, write `$94D0,Y = id`, `$9551,Y = slot`, Y++.
   - If `$9F25`: write `$94D0,Y = FC` (the "Double Technique" header), Y++. Headers have no `$9551` write; it stays FF.
   - Duals, slots 0x0A..0x0F: the same, with `$9551 = slot`.
   - If `$9F28`: write `FD` (the "Triple Technique" header), then `$94D0,Y = triple id` and `$9551,Y = 0x12`.
3. `$A02A[seat]` = the entry count up to the first FF, capped at 0x14. The count INCLUDES the headers.

Header text: CC/FB4B+0 = `[Double Technique]`, CC/FB4B+0x12 = `[Triple Technique]` (C1/0B3A-0B50).

Measured lists:
- Crono+Marle, seat0: `01..08 FC 39 3A 3B`, seat1: `09..10 FC 39 3A 3B`, `$A02A = 0C 0C`.
- Triple party, seat0: `01..08 FC 39 3A 3B 3C 3D 3E FD 66` with `$9551 = 00..07 FF 0A..0F FF 12`, `$A02A = 11`.
- Triple party, seat2 (Lucca): `11..18 FC 3C 3D 3E 48 49 4A FD 66`. She has no C+M duals, because she is not a
  participant.

**When a dual appears:** only at boot, when it is learned and every participant is in the party. It never depends on
ATB, status or MP. Those only GREY it (§4).

The byte scan finds no other JSR to C96A/CA1A/CCCB in bank C1. On the oracle, the records are written only at
f286-289.

**Command label** (C1/0792-081D). The Tech row reads "Comb" (CC/FA6B) instead of "Tech" (CC/FA41) when all of these
hold:
- `$95F1 != 0`;
- `$9F25[seat] | $9F28[seat]`;
- and one of: `$A117 == 0`; or `$A6DE == 3`; or (`seat != $A115` and `$A6D9[$A115] < 0`).

Oracle fixtures have `$95F1=01 $A117=00`, and the command row shows "Comb" (`cm_f458.png`, `tri_f458.png`).

---

## 3. Open, the menu pass and navigation

**Open (C1/12BC)**, reached on A at command row 1 (C1/1296): gate `$A0A7 & 1` skips it; `$9EE7 = FF`, `STZ $A09A`,
`STZ $A099`; C1/0A88 renders the rows from `$95EB` (2 loops × 2 × C1/0B0B); C1/0AD3 builds `$1CDB[0..19]` (0 for
`$94D0` in {FB..FF}, else 1); `$9615 = 1`, `$95DB = 1`, `STZ $A869`, `INC $A86A`. `$95DF` (cursor) and `$95EB`
(scroll) are NOT reset. Measured: `$95DB` 0→1 at f475, writer C1:12E4.

**The 4-frame menu pass (C1/10C6)** runs when `$993A` != 0 (set every 4 frames by CF/E73B), in this order: C1/1B67;
C1/0C2D, which when `$95DB == 1` runs the grey pass C1/0EE1, then C1/0C83 (render + current-MP digits), then the
resolver C1/0D4D; C1/1153, which (with `$9609 == 0`, `$95DB == 1`) sets `$99E0 = $99E1` (the Wait-mode ATB freeze
while a submenu is open) and JMPs to the input C1/1320; C1/17DD; `STZ $EE`.

Oracle `--hookpc` confirms that C1/0C2D, C1/0EE1, C1/0D4D and C1/1320 each run once every 4 frames (f22, 26, 30, …).
Because the grey pass and the resolver run BEFORE the input step, a cursor move shows up in `$9EE3-9` on the NEXT
pass: `$95DF` changes at f503 (C1:1436) and `$9EE3` at f507 (C1:0DDC).

**Resolver (C1/0D4D).** X = CC/F38C[seat] + `$95EB` + `$95DF`. If `$9551,X == FF` (header/empty), `$9EE3..$9EE9` =
FF; otherwise copy the record at CC/F395[seat] + CC/F3A1[`$9551,X`] into `$9EE3..$9EE9`. Then draw the cost digits
(C1/0174): `$9EE6` → seat-0 row `$A75B/$A75D`, `$9EE8` → seat 1 `$A7DB/$A7DD`, `$9EE9` → seat 2 `$A85B/$A85D`; FF =
no digits. So each participant's cost sits on that participant's own HUD row: Ice Sword (p3) Crono "99 - 4" / Marle
"99 - 2"; Aura Whirl (p2) "99 - 2" / "99 - 1"; Fire Sword (tri_grey2) Crono "- 4", Marle blank, Lucca "- 2".

**Input (C1/1320).** `$80` = `$95EB + $95DF` (index relative to the seat base). Checked in order: `$EE` bit7 → confirm
C1/1369; `$EE` bit3 → cancel C1/138F; `$EF` bit3|bit1 → up C1/13AD; `$EF` bit2|bit0 → down C1/13F4 (the oracle Down,
pad 0x20, reaches C1/13F4).

**DOWN (C1/13F4):** repeat {steps++, `$80++`; if `$80 >= $A02A[seat]` stop with no move} until `$1CDB[$80] != 0`
(headers skipped). c = `$95DF + steps`; if c < 3 `$95DF = c`, else while c >= 3 {`$95EB++`, c--} and `$95DF = c` — the
cursor never passes row 2. **UP (C1/13AD)** mirrors it: repeat {`$80--`; if < 0 stop} until `$1CDB[$80] != 0`;
c = cursor - steps; if c >= 0 `$95DF = c`, else if `$95EB == 0` no move, else while c < 0 {`$95EB--`, c++}.
Measured over Crono's 12-entry list: `$95EB` 5→7 at f575 (the jump over FC at index 8, C1:142D), `$95DF` stays 2.

**Confirm (C1/1369)** — when `$A099 != 0` (the grey pass has run) and `$9EE5` bit7 is clear: `$960D = $9EE4`,
JSR C1/1F79 (target-select setup); if `$9613 < 0` `STZ $9614` and nothing opens, else `INC $A4EE`, `INC $9609`.
A GREY entry is a no-op. Oracle: A on a greyed Delta Force, and on an Ice Sword greyed by Marle's MP, leaves `$9609 = 0`.

**Cancel (C1/138F)**: JSR C1/1C3A, `STZ $95DB`, `$A6DF = FF`, `$95DF,X = $A863,X`, `$95EB,X = $A866,X`.

---

## 4. The GREY rule (C1/0EE1-103D), re-evaluated every pass for the 3 VISIBLE rows only

Setup: `$96/$98/$9A` = the 16-bit curMP of seats 0/1/2 (`$5E34/$5EB4/$5F34`). `$86` = CC/F38C[seat] + `$95EB`.

For each row r = 0..2, starting with palette `$94 = 0x29`:
- **Header** (`$94D0 & F0 == F0`):
  - FC is grey when `$A6DE < 2`;
  - FD is grey when `$A6DE < 3`;
  - FB/FE/FF stay 0x29.
- **Tech.** X = the record (`$9551` → CC/F3A1). It is GREY when ANY of these holds:
  1. `+3 != FF` and seat-0 MP < `+3` (16-bit compare, C1/0F7E);
  2. `+5 != FF` and seat-1 MP < `+5`;
  3. `+6 != FF` and seat-2 MP < `+6`;
  4. `+4 != FF`, with p = low nibble, and C1/103E is nonzero for p. C1/103E is nonzero when `$A6D9[p] < 0` (the
     partner is not in the ready queue) or when `$A0D1[$A6D9[p]] != 0`;
  5. `+4` high nibble != F, and C1/103E is nonzero for that seat (the second triple partner);
  6. `$A0D1[current seat] != 0`;
  7. id == 0x74 and `$1C48[$A0FF] != 0x42` (C1/0FE3-0FEF, OPEN O2).

  A grey row does `$1A82,X |= 0x80` and `$94 = 0x2D` (C1:100B). Otherwise it does `$1A82,X &= 0x7F` (C1:0FFB).
- **Paint.** `$94` is written as the attribute of 18 tiles in each of the two tile rows at `$A6E2/$A722` +
  CC/F39B[r].

After the three rows, `$A099 = 1` (C1:103D).

`$A0D1[seat]` is the tech-lock flag. C1/B1D2-B1E7 sets it to 1 when the seat is alive (block+0x1D bit7 clear) and
block+0x1E bit3 is set. It is 0 in all fixtures here.

**Measured grey cases:**
- **Partner not ready.** In the triple party, opening the list at f18 (from `tri_f458.state`) froze the ATBs through
  the Wait-mode `$99E0`, so Lucca never became ready.
  - `$9EE3` records were `3C 12 83..`, `3D 07 80..`, `3E 12 83..`, `66 08 80..`, with `$1B00 = 80` (C1:100B).
  - Ice Sword 2, whose partner Marle was ready, stayed white.
  - The "Triple Technique" header was grey (`$A6DE = 2`).
  - A on Delta Force did nothing.
  - Screenshots: `tri_grey.png`, `tri_grey2.png`.
- **Partner MP short.** `cm_f458.state` + `PEEK_POKE=5EB4=01`: Aura Whirl (Marle cost 1) stays `$1AC8 = 00`; Ice
  Sword → `$1ACF = 80`; Ice Sword 2 → `$1AD6 = 83` (C1:100B at f129/f141).

---

## 5. Target confirm → queue (C1/1626, the Tech branch C1/163B-1683, then C1/16A8-174B)

In order:
1. `DEC $9609`. If `$A0D5` (cursor memory; 0x80 in this save): `$A866[seat] = $95EB`, `$A863[seat] = $95DF`,
   `$A0D8[seat] = $9EE3`.
2. **Partner dequeue.** If `$9EE7 != FF`, run C1/174E for the low nibble, and for the high nibble when it is != F.
   C1/174E for seat p: `if ($A6D9[p] >= 0) DEC $A6DE`; `$A6D9[p] = FF`; `$93EE[CC/FAF0[p]] &= 0x7F`
   (CC/FAF0 = `00 07 0E`, the queue record per seat).
3. `$80 = 0x20`, `$82 = $9EE7`, `$84 = $9EE3`. `$9916[seat] = $95DC[seat]`; unless `$A0D4`, clear `$95DC..DE` and
   `$9916..18`; `STZ $A114, $960E, $9614, $A09A`.
4. **Action FIFO:** `$99D4[$99D8] = seat` (C1/16E2), later `INC $99D8` (C1/1713).
5. **The CHOOSER's queue record** (X = CC/FAF0[seat]): `$93EE = (old & 7F) | 40`, `$93EF = $80 | $9F38[seat]` (0x20),
   `$93F0 = $9615` (1), `$93F1 = $A62D` (target combatant), `$93F3` = partner byte, `$93F4` = tech id. In `$93EE`,
   bit7 = awaiting a command (set 0x81 by C1:1B2E), bit6 = a command was entered.
6. `STZ $95DB`, `$A6DF = FE`, `DEC $A6DE`, `$A6D9[seat] = FF`.
7. **New head:** scan `$A6D9[0..2]` from 0; the FIRST ready seat becomes `$A6DD` and `$95D5`, else both FF
   (C1/172A-1742). Then `STZ $A862`, `STZ $99E0`.

**Measured.**
- Ice Sword chosen by Crono, f620: `$93EE..$93F4` = `41 20 01 06 00 F1 3A`; Marle's `$93F5` = `01 00 00 00 00 FF 00`
  (bit7 cleared); `$A6D9 = FF FF FF` (C1:172A), `$A6DE` 2→0 (C1:1721), `$95D5 = FF` (C1:1745);
  `$A0D8/$A863/$A866[0] = 3A/02/08` (C1:1656/1650/164A).
- Chosen by Marle (Right at f2 moves the head to her, C1:1224): `$93F5 = 41 20 01 06 00 F0 3A`, Crono's `$93EE` bit7
  cleared (f174).
- Delta Force (f374, or f832 continuous): `41 20 01 06 00 21 66`; both partner records become `01…`; `$A6DE` 3→0;
  `$99D4 = 00 FF FF FF`, `$99D8 = 01`.
- Head rule (not dual-specific, but a dual dequeues 2-3 seats at once): seats 0-2 ready, seat 1 confirms Attack → the
  new head is **0** at f218 (C1:1732/1735), `$A6D9 = 00 FF 02`.

---

## 6. Dispatch (C1/8580 → C1/B967 → the Tech branch C1/BB49)

**Choose the record.** C1/B967-B9B4 checks round-robin, from seat `$B18B`, the records whose `$93EE` has bit6 set.
The one it takes sets `$B18B = seat` and loses bit6 (`AND #BF`; writers C1:B98B seat 0, C1:B9A1 seat 1).
`$B3F4 = $B18B`. The FIFO entry is cleared by C1:85B1.

**Participant check (C1/B9B7-BA30).** `$B3E7[owner] = 1`, and each nibble != F of `$93F3` sets `$B3E7[nib] = 1`. For
each flagged seat, FD/A8A5 sets `$B3EA = 1` if block+0x1D bit7, block+0x21|+0x26 bit7, or block+0x1E & 0x86. Any
failure → FD/A93C cancels the whole action; for each flagged seat FD/A8CE does `$93EE/$93EF[s*7] = 0`, `$93F3 = FF`,
`$AFAB = $99DD = $9F22 = $B158[s]`, `$B188 = $B03A = 0`; then JMP C1/BD6B. Not exercised on the oracle (O4).

**Kind** from `$93EF`: bit7 attack BA68, bit6 item BA83, bit5 tech BB49. A tech also requires `$93F0` ∈ {1,3}.

**Tech (BB66-BC5D).**
1. `$AD8D = 1`, `$AD8E = $93F1` (target), `$B18C = $93F4` (id); C1/C1DD target setup (calls C1/CB93; gates
   the OWNER seat `$B18B` on the same statuses).
2. `$AE91 = $B1BE[$B2EA]` — **the actor is participant 0's SEAT, whoever chose**; `$AE92 = 2`, `$AE93 = id`.
3. AD09, AD35, BF79, then **MP**: for each participant k, `Y = cost` (C1/CB48, CB61, CB7A), then C1/CC71 finds the
   seat and does a 16-bit `SBC` on `$5E34/$5EB4/$5F34` after the accessory halving C1/CBF6.
4. AC57 … `JSL FD/ACEE` (it only clears `$AD9C..`); the action animates before C1/BC5A, which does `JSR BDB3` (ATB)
   and `JMP BCF8` (clear the records).

**Co-actors.** C1/D6C0-D6DF (in the effect executor): `$AE97 = $B1BE[$B2EB]`, `$AE98 = $B1BE[$B2EC]` (FF = none).
CC/F156 builds `$A014 = $AE91 | $A015 | $A016 | $A017`; a negative co-actor of a party actor is replaced from
CC/FBC8[actor*2] or CC/FBC9 (= `01 02 | 02 00 | 00 01`). **Anim:** `$9877 = CD/45A6[id*7][0]` (3A → 3A, 66 → 63).

**Measured.**
- Ice Sword (continuous drive from `gato_wait.state`): f622 `$93EE` 41→01 (C1:B98B), `$AE91..98 = 00 02 3A 00 00 02
  FF FF` (C1:BBF6); f623 `$AE97 = 01`, MP Crono 99→95 (C1:CC8F), Marle 99→97 (C1:CCAC), `$AD89 = 0x33`; f625
  `$A014 = 00 01 02 06` (CC:F1A7), `$9877 = 3A` (C1:45C2), `$A013 = 1` (C1:40A9); f1036 freeze end (411 f), GATO
  76→0 (C1:ED12).
- When Marle chose it: `$AE91 = 00` all the same (f176), with identical MP, `$A014` and span.
- Delta Force: f834/835 `$AE91..98 = 00 02 66 00 00 02 01 02`; f836 MP −8 each for Crono, Marle, Lucca
  (C1:CC8F / CCAC / CCC5); f838 `$A014 = 00 01 02 06`, `$9877 = 63`, `$A013 = 1`; f1546 end (708 f).

---

## 7. The effect: how the participants' stats combine (C1/D523-D742)

For k = 0, 1, 2 (`$B2C7`):
1. `eff = CC/1BF0[id*11+k]`. **If bit7 is set, SKIP this effect and continue with k+1** (C1/D5D4 `BIT #80 / BEQ /
   JMP D71E`, and D71E increments k). The participant still PAYS the MP for it (§6, `& 7F`).
2. Load the 12-byte CC/213F[eff] into `$AEE6..` (C1/D5EF).
3. Get the participant's seat as `$B1BE[$B2EA+k]`. If it is FF, STOP (C1/D60C → D742).
4. `$B2CC = CC/1BF3[id*11+k]`, the extra-effect selector.
5. **`$B18B = the participant's seat`** (C1/D632). This is the attacker for this effect's formula.
6. Run the handler `JSR ($D9D7 + 2*$AEE6)`.

Each effect's damage `$AD89` (after the extra effect, e.g. C1/EFBB) is staged into its OWN bank:
`$B328 + k*0x2C + tgt*4` (C1/EC12-EC2F; FD/ACFD clears all 0x84 bytes). The span-end apply writes the SUM of the
banks to HP in one frame (C1:ED1A/ED12). Other steps in the loop: C1/D70D-D71C has an early stop (O5), and after the
loop CC/1BEC & 7 selects C1/EB68.

**Measured** with GATO HP poked to 999 on mid-battle states (`6130=E7,6131=03,6132=E7,6133=03`):

| tech | k | participant | effect | staged | predicted |
|---|---|---|---|---|---|
| Ice Sword | 0 | Crono | 04, physical pow16 | `$B340` = 42 | `16·(5·12+3·5)/18 = 66`, `66·129/256 = 33`, +rand(0..1) = 34, ×5/4 (CC/2A05[08] = `0B 05 04`) = **42** |
| Ice Sword | 1 | Marle | 0B, magic pow11 | `$B36C` = 51 | `11·12·(1+8)/15 = 79`, `79·131/256 = 40`, +1 = 41, ×5/4 = **51** |
| Delta Force | 0 | Crono | 05, magic pow14, L1 M5 | `$B340` = 42 | **42** (var 0) |
| Delta Force | 1 | Marle | 0E, magic pow14, L1 M8 | `$B36C` = 65 | **65** (var 1) |
| Delta Force | 2 | Lucca | 16, magic pow14, L2 M8 | `$B398` = 72 | **72** (var 1) |

Ice Sword total: HP 999→906 (−93 = 42 + 51) at f578. Delta Force total: 999→820 (−179).

So each participant's effect runs the ordinary single-tech formula on THAT participant's stats. The magic formula
is the one in battle_tech_slice.md §T4. Each effect draws its own variance. Nothing mixes one caster's stats into
another's effect. The damages are ADDED.

---

## 8. Span end: every participant's ATB (C1/BDB3) and the records (C1/BCF8)

C1/BDB3 first zeroes bytes +0..+3 and +6 of PARTICIPANT 0's queue record (FD/A849[seat($B2EA)] = `$93EE/$93F5/$93FC`;
C1/BCF8 later clears the chooser's `$B3F4` record +1..+3/+6 and each partner nibble's). Then, for each
participant k with seat s, in order B2EA, B2EB, B2EC (FF ends the list):
1. `$AFAB[s] = $B158[s]`.
2. C1/BD6F: if block+0x20|+0x25 bit7, halve it; if block+0x1E bit5, double it (cap FF); raise 0 to 1. Mirror to
   `$99DD/$9F22`.
3. `f` = CC/2BDC[id] >> 4 for k=0, CC/2BDC[id] & 0x0F for k=1, CC/2BEB[id] & 0x0F for k=2.
4. If `$AFAB != FF`: `$AFAB += $AFAB*f/10` (cap FF).
   - **k=0 never gets it**: C1/BE17 does `LDX #0A` without `STX $2A`. The multiply C1/C90B has shifted `$2A` to 0,
     and C1/C92A with a zero divisor returns 0.
5. `$99DD/$9F22 = $AFAB`.
6. If `$B03A[s]`: `$B03A[s] = 1`, `$B188[s] = 0`, `$93EE[s*7] = 0`.

**Measured** (frames relative to the f458 mid-battle states, GATO HP 999):
- Ice Sword (f578, C1:BEA4): `$B158 = 48 66` → `$AFAB = 48 7A`. Crono 72 (no factor); Marle 102 + 102*2/10 = 122
  (CC/2BDC[3A] = 32).
- Delta Force (f1088, C1:BF21): `$B158 = 48 66 72` → `48 84 94`. Marle 102+30; Lucca 114+34 (CC/2BDC[66] = 53,
  CC/2BEB[66] = 03).
- Queue records cleared the same frame (`$93EE` C1:85D8, `$93F5` C1:BEC8). Both PCs re-enter the queue later on
  their own ATB (Crono f958, Marle f1062; C1:1B2E/1B83).

---

## 9. Reusable oracle drives (logical rule on; masks: A=100 Down=20 Right=80)

```
POKE_DUAL="2837=FF,2830=08,2838=FF,2831=08,2607=63,2608=00,2609=63,260A=00,2657=63,2658=00,2659=63,265A=00,283E=E0"
POKE_TRI="$POKE_DUAL,2839=FF,2832=08,26A7=63,26A8=00,26A9=63,26AA=00,283F=E0,2843=E0,284D=80,2982=02"
```

**DUAL (Ice Sword, Crono chooses)**

`--from saves/battle_re/gato_wait.state`, `PEEK_POKE=$POKE_DUAL`,
`--input "100:8,0:452,20:2,0:10,100:2,0:20,(0:8,20:2)x9,0:10,100:2,0:20,100:2,0:900"`. Write the `x9` out
literally; the grammar has no repeat.

Timeline: f377/f441 Crono/Marle ready; f475 `$95DB = 1`; f587 `$9EE3 = 3A 07 00 04 F1 02 FF`; f595 `$960D = 07`,
`$9609 = 1`; f620 confirm (§5); f622-625 dispatch (§6); f1036 end.

Monitor: `"95DB:1,9609:1,960D:1,93EE:21,A6D9:3,A6DE:1,95D5:1,AE91:8,A014:4,5E34:2,5EB4:2,6130:2,9877:1,A013:1,AD89:2"`.

- Aura Whirl is 8 Downs instead of 9 (`$9EE3 = 39 81 00 02 F1 01 FF` at f579).
- **Marle chooses:** from `dual/cm_f458.state` (= `POKE_DUAL` + `--btnscript "100:8,0:450"` under `--peek`, saved with
  `PEEK_SAVE`), then `"0:2,80:2,0:10,20:2,0:10,100:2,0:20,(0:8,20:2)x9,0:10,100:2,0:20,100:2"`.
- **HP-999 variant:** poke `6130/6132` on the f458 state (the battle boot reloads HP from a field state).

**TRIPLE (Delta Force)**

`PEEK_POKE=$POKE_TRI`, `--input "100:8,0:450,0:170,20:2,0:10,100:2,0:14,(0:8,20:2)x14,0:10,100:2,0:20,100:2,0:1200"`.

Timeline: f616 all 3 ready; f800 `$9EE3 = 66 08 00 08 21 08 08`; f808 `$960D = 08`; f832 confirm; f836 MP −8 × 3;
f838 `$9877 = 63`, `$A013 = 1`; f1546 end.

Wait for all three to be ready BEFORE opening the list. In Wait mode an open submenu freezes ATB (`$99E0 = $99E1`,
C1/1172), so the triple would stay grey (§4).

**GREY fixture**
- Partner not ready: from `tri_f458.state` (= `POKE_TRI` + `"100:8,0:450"` saved), `"0:2,20:2,0:10,100:2,0:14,(0:8,20:2)x14,0:6"`.
- MP short: `cm_f458.state` + `PEEK_POKE=5EB4=01`.

---

## 10. IMPLEMENTATION SPEC: replace the host model with the WRAM machine

The port today (`battleTechBuildList` / `battleTechResolve` / `battleTechSubmenu`, battle.c ~4194-4290; HUD in
field.c ~4929-4990) is a singles-only host model. Divergences, each with its ROM replacement:

**S1. Boot build (new).** At battle entry, after `$AEFF` / `$B1BE` / `$B3BA` exist, call once (C1/FB5D):
1. FD/AD09: FF-fill `$1A80..$1C23`.
2. For seat 0..2: singles, duals and the triple exactly as in §1, reading `$2837+char`, `$283E+pair` and
   `$284D+i` and the ROM tables CC/1BEB, CC/249F, CC/1ACB, CC/1ACC, CC/253B, FD/A89D, FD/A84F.
3. Build the list as in §2: `$9F25/$9F28`, `$94D0/$9551` with FC/FD headers, `$A02A` including the headers.

Drop the per-open rebuild. `$9551` must be the record SLOT, not the packed index. The port writes `i`, which is wrong
as soon as the learned bits are not contiguous from the MSB.

`$1A80..$1C23` is the field mover table (`$1A80,X` in move.c). The ROM reuses it in battle and the field reload
rebuilds it. Check the port's WRAM ownership across the battle (O9).

**S2. Open** (C1/12BC, §3). No list rebuild. Keep `$95DF/$95EB` per seat. Rebuild `$1CDB` from `$94D0`. `$A099 = 0`.

**S3. Menu pass** every 4 frames, on the `$993A` consumer, in the order grey → render → resolve → input (C1/10C6).
Replace `h->techSel` / `techListN` with `$95EB[seat]` / `$95DF[seat]` and the §3 DOWN/UP algorithms (header skip,
3-row window, cap `$A02A`). The 4-frame record lag follows from this order.

**S4. Grey** (C1/0EE1, §4). Evaluate only the 3 visible rows. Write bit7 into the RECORD's +2. Check all of:
per-seat MP (+3/+5/+6 against that seat's curMP), partner readiness (`$A6D9[p] >= 0` and `$A0D1`), the chooser's own
`$A0D1`, the header rule (`$A6DE` < 2 / < 3) and the 0x74 gate. Set `$A099 = 1`. The port's
`cost(CC/253B[tid]) > chooser MP` is wrong for duals (wrong table, wrong seat) and for any seat-1/2 single (its cost
is at `$9EE8/$9EE9`, not `$9EE6`).

**S5. Resolver** (C1/0D4D): copy the 7-byte record into `$9EE3-9` (headers/empty → FF). The port's
`$9EE6 = mp` for every seat and `$9EE7-9 = FF` must go. Confirm stays C1/1369: `$A099 && !($9EE5 & 0x80)`, then
`$960D = $9EE4`.

**S6. Confirm / queue** (C1/1626, §5):
- partner dequeue C1/174E for each nibble of `$9EE7`;
- the chooser's queue record `$93EE+7*seat` = `[(x&7F)|40, 20|$9F38, $9615, $A62D, -, partner, id]`;
- `$99D4[$99D8++] = seat`;
- `$A6DE--`, `$A6D9[seat] = FF`;
- **new head = the LOWEST ready seat, else FF.** `battleQueueRemove` cycles forward from the current head and leaves
  the cursor on an empty slot — a divergence the oracle confirmed at f218.

`battleCommitPcAction` should carry the partner byte (or the queue record) into the sched FIFO.

**S7. Dispatch** (§6):
- Before running the action, check every participant with FD/A8A5. On failure, cancel as FD/A93C does (clear the
  records; ATB = `$B158` for ALL participants).
- The actor is participant 0's seat (already ported). `$AE97/$AE98` (already ported). MP per participant (already
  ported).
- **Fix `battleTechDamage`:** `if (eff & 0x80) continue;` instead of `break` (C1/D5D4 → D71E). This matters for
  techs 3F, 40, 47, 5A, 60, 64, 6A and 73, whose skipped effect comes first; today they deal nothing from the later
  effects.
- Per-effect staging banks `$B328 + k*0x2C + tgt*4`, if WRAM fidelity is wanted. The port's sum already matches the
  applied HP.

**S8. Span end** (C1/BDB3 + C1/BCF8, §8): reload EVERY participant's ATB, not only `atk` (battle.c ~11320). Base
`$B158` → BD6F → factor from CC/2BDC / CC/2BEB, **with the k=0 factor always 0** (the ROM quirk; it keeps single techs
byte-identical to today). Clear the owner's and partners' queue records.

**S9. HUD** (field.c):
- rows = `$94D0[CC/F38C[seat] + $95EB + r]`;
- headers render the CC/FB4B text;
- palette per row from the grey pass (0x2D = grey);
- the cost digits come from `$9EE6/$9EE8/$9EE9` on EACH seat's row, not CC/253B on the chooser's row;
- the "Comb" label (§2).

**Checks to pin** (grade WRAM on the oracle drives of §9):

| # | what | expected |
|---|---|---|
| 1 | boot records | `$1AC6:21` / `$1B52:21` = the §1 bytes; triple `$1AFE:7`, `$1B8A:7`, `$1C16:7` |
| 2 | list | `$94D0:20` seat 0 = `01..08 FC 39 3A 3B`; `$A02A` = `0C 0C` (triple: `11 11 11`) |
| 3 | nav | `$95EB` 5→7 over FC; `$9EE3 = 3A 07 00 04 F1 02 FF` one pass after the move |
| 4 | grey | Delta Force `$1B00 = 80` with Lucca unready; `$1ACF = 80` with `5EB4=1`; A leaves `$9609 = 0` |
| 5 | confirm | `$93EE:14` = `41 20 01 06 00 F1 3A` / `01 00 00 00 00 FF 00`; `$A6D9 = FFFFFF`, `$A6DE = 0`, `$95D5 = FF` |
| 6 | dispatch | `$AE91 = 00`, `$AE97 = 01`, `$A014 = 00 01 02 06`, `$9877 = 3A`; MP 99→95/97; triple MP −8 × 3 and `$9877 = 63` |
| 7 | damage | banks 42/51 (sum 93); Delta Force 42/65/72 — variance is an RNG-order boundary, like the physicals |
| 8 | ATB at span end | `48 7A` / `48 84 94`; spans 411 / 708 |

---

## 11. Open questions

- **O1.** The "Comb" label inputs `$95F1`, `$A115` and `$A117`: semantics not derived. All fixtures have
  `$95F1 = 1` and `$A117 = 0`.
- **O2.** Tech 0x74 (Grand Dream) greys unless `$1C48[$A0FF] == 0x42` (C1/0FE3-0FEF). The meaning of `$A0FF` and
  `$1C48` is not derived and not measured.
- **O3.** The meaning of flag bits 0-5 of +2 (CC/1ACC & 3F, e.g. 03 for Cyclone and Ice Sword 2) is not derived; they
  are carried into `$9EE5`.
- **O4.** The dispatch abort (FD/A8A5 / FD/A93C, a participant disabled between confirm and dispatch) is ROM-read
  only. It needs a capture with a status poked after the confirm.
- **O5.** C1/D70D-D71C: the effect walk stops when `$7E:(B1F5 + id*11)` bit7 and `$B200` bit7. The address looks
  odd; verify on the oracle before porting.
- **O6.** Untested: the ally-target duals (group 0x81, Aura Whirl / Cure Wave), the accessory halving A2/A3, the
  >6-duals overflow, a KO'd partner during the list (grey through `$A6D9`), and Active mode (only Wait was driven).
- **O7.** How the rock triples 0x70-0x74 get their learned bits (`$2857-$285B`): they are not in the CC/2778
  learner.
- **O8.** Right/Left inside the tech submenu (`$EF` bit0/bit1 = Down/Up per C1/1346-135E) are not measured; the
  `$EE/$EF` edge mapping is the existing notes'.
- **O9.** In the port, `$1A80..$1C23` is shared with the field mover table (move.c). Confirm that the battle's
  FF-fill and records cannot leak into the field after the fight. In the ROM the field reload rebuilds it.
- **O10.** The description box text (C1/0E92: `JSL CD/0027` with `$9EE3` when `$A0DB` bit7) is not covered.
