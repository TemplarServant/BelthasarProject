# THE ITEM COMMAND — vertical slice: Crono's TONIC (item 0xBD), menu → dispatch → consume → +50 HP → anim

Derives, from the ROM disasm + LakeSnes oracle **only**, the full path for ONE consumable item end-to-end
plus the shared item infrastructure every item reuses. The slice PROVES the pipeline; the other consumables
are data in the same tables. **Zero invention** — every field is byte-cited (`tools/dis.py <fileoffset>`,
fileoffset = `(bank−0xC0)<<16 | addr`) and every value is oracle-measured (`ct_probe.exe` = `build/ct.exe`,
`--monitor ADDR:LEN --from STATE --btnscript ...`; ⚠ **LEN is DECIMAL**).

> ⚠ **The GATO fight's real inventory is just the Pendant (a key item).** `gato_press_action.state`
> (the canonical fight, tech-lane base) has `$2400 = D6` / `$2500 = 01` only — the Pendant is NOT a battle
> item, so the item list is empty. Real-play states confirm the new-game stock: `batalha_2/7/8` (Desktop aux
> captures) hold `$2400 = D6 BD ..` (Pendant + **Tonic**), `batalha_8` also `C6` (**Heal**), `batalha_1`
> holds `C6 C7 C8` (Heal/Revive/Shelter). To exercise Tonic I patched the FIELD state's inventory
> (`$2400[1]=BD $2500[1]=3`, `+ $2400[2]=C6 $2500[2]=2` for the filter test) and Crono's curHP low
> (`$2603=15`, so a +50 heal reads cleanly 15→65), then booted the battle — the battle-entry staging then
> filters the inventory into the battle item buffer. This is a canonical injection (like the tech lane's
> `$2837|=0x80` / `crono_left/right`); the item's DATA is static ROM, independent of the seed.
> `gato_item.state = patch_state gato_press_action.state 2400+1=BD 2500+1=3 2400+2=C6 2500+2=2 2603=15`.

---

## 0 — HEADLINE RESULTS (all oracle-measured on `gato_item.state`)

1. **Tonic = item id 0xBD**. Static ROM data: name "Tonic" (`CC/0B5E + 0xBD×11`), target group **0x80**
   (`CC/1BB5[sub=1]`), effect selector **0x02** (HP-heal, `CC/0ABC[sub×3]` low-7), heal amount **50 = base×mult**
   (`CC/05CC[sub]` base 0x32 × mult 1 — the SAME shared field table the field-Use screen validated,
   see [[ct-field-item-menu-use]]), anim config `CD/5C26[sub×7]` (→ `$9877=01`).

2. **The command Item row is a per-seat SUBMENU** keyed on `$95DB` (the "$95DB submenu boundary": **0 = Att,
   1 = Tech, 2 = Item**). A-on-Item (`C1/128B` row dispatch → `JMP C1/12ED`) opens `$95DB=2`; the item list, its
   nav, the resolver, the confirm→target-select→queue chain are all DERIVED (§2). Freeze in Wait mode fires for
   `$95DB∈{1,2}`, so the item submenu freezes like the tech submenu (`battle_atbmode.md`).

3. **The chosen item reaches dispatch via the queue record** `$93EE,X`: `$93EF = 0x40` (item action marker),
   **`$93F4 = the item id`**. The item marker 0x40 is the byte-exact analogue of `0x80` (attack) / `0x20` (tech).

4. **The item is CONSUMED at CONFIRM (target-commit), on a battle STAGING COPY — not the field inventory.**
   Battle entry stages a **filtered** copy of `$2400/$2500` into `$1580` (5-byte records). Confirm does
   `DEC $1583,X` (`C1/1688`) on that copy; the field `$2400/$2500` is UNTOUCHED all fight and reconciled back at
   battle end (`C1/B500`, §3.3). This is unlike the tech's MP (spent at animate).

5. **HEAL = the DAMAGE pipeline with a NEGATIVE delta.** The effect stages `$B328 = −50` (`0xFFCE`, writer
   `C1/EC2F`) at dispatch and applies it at span-end via the **universal curHP writer `C1/ED1A`** (`STA $5E30,X`),
   the SAME routine that applies attack/tech damage. `curHP −= (−50) = +50`, **clamped to maxHP** (cap proven:
   curHP 40 → **70**, not 90). Tonic amount 50 = `CC/05CC` base×mult (the +50 rules out `CC/0ABC[+1]`=10).

6. **The anim is a per-item config** `CD/5C26` (7-byte, indexed by `sub = $AE93 − 0xBC`, `C1/475B`), NOT the
   tech's `CD/45A6`. Tonic sub=1 → `$9877 = CD/5C26[7] = 0x01`, script base `$9878 = CD/63F0[1×2] = 0x829C`.
   Span **166f** (freeze f453→f619), popup 58f (f552→f610).

### The slice ledger (one clean run: boot A, settle, Down Down→Item, A, A on Tonic, A on Crono)
```
f289  $1580 ← BD80020300 C600000200   battle item staging (FILTERED)      (FD/B01C; Pendant D6 excluded)
f394  $95DC 0→2        Down×2: command cursor Att→Tech→Item              (C1/128B row dispatch)
f407  $95DB 0→2  $9615 0→2   A on Item: SUBMENU opens                    (C1/12ED STA $95DB=2; list C1/095D)
f422  $9609 0→1 $960D 0→80 $9F35 0→BD   A on Tonic: target-select opens  (C1/1498 resolver; group $1581=0x80)
f442  $9609→0 $95D5→FF  $1583 03→02  $93EE=41 40 02 00 00 FF BD          (C1/1685: consume DEC $1583 + queue)
f451  $AE93 0→BD  $B328 0→FFCE(−50)   dispatch: id staged + heal computed (C1/BB23 $AE93; C1/EC2F $B328)
f453  $9877 0→01  $A013 0→1           freeze on, item anim starts         (C1/4776 $9877; C1/40A9 $A013)
f552  $A3D1 →1/2       ShowDamage(heal) popup born                       (C1/5AE3/5B33; $A3D2 ramps)
f610  $A3D1 →0         popup done (58f life)
f619  $A013 1→0  $5E30 15→65   span end (166f) + HP applied (+50)         (C1/41BD freeze off; C1/ED1A HP)
```

---

## 1 — THE ITEM DATA (Tonic's identity, all static ROM; `sub = id − 0xBC`)

| datum | value | ROM source | note |
|---|---|---|---|
| item id | **0xBD** | field inv `$2400` slot | consumables = id ≥ 0xBC |
| sub-index | **1** | `id − 0xBC` | Tonic=1, MidTonic=2, FullTonic=3, Ether=4, Heal=0x0A |
| name | "Tonic" | `CC/0B5E + id×11` (11-byte stride) | `CtMenuItemName` (menu.c) — SAME table as the field/menu |
| battle-usable + selector | `CC/0ABC[sub×3]` byte0 = **0x82** | bit7=battle-usable(1), bits0-6=selector(**0x02** HP) | the FILTER (§2.1) + the effect kind |
| target group | **0x80** | `CC/1BB5[sub=1]` | → `$1581` (staged) → `$960D` at confirm; Tonic = single PC |
| heal amount | **50** | `CC/05CC[sub×4]` = `80 80 01 32` → base 0x32×mult 1 | shared field effect table; MidTonic 50×4=200, FullTonic 50×10=500 |
| anim config | `CD/5C26[sub×7]` = `01 E1 8A 1D 00 7F FF` | → `$9877=01`, base `CD/63F0[2]=0x829C` | per-consumable pose (§5) |

**`CC/0ABC` (battle-item table, 3-byte stride, `sub×3`) — byte0 = [bit7 battle-usable][bits0-6 selector]:**
```
Tonic  BD sub1  82 0A 00   usable=1 sel=02(HP)     MidTonic BE sub2  82 64 00  usable sel02
FullT  BF sub3  82 BC 02   usable sel02            Ether    C0 sub4  82 20 03  usable sel02
Heal   C6 subA  80 0A 00   usable=1 sel=00(status) Revive   C7 subB  80 C8 00  usable sel00
Shelter C8 subC  02 96 00  usable=0  ← FIELD-ONLY  Elixir   C3 sub7  82 20 4E  usable sel02
```
**Byte0 bit7 is the battle-usable FLAG** — distinct from the field-usable flag (`$7700` bit1, [[ct-field-item-menu-use]]).
**Shelter (0xC8) has bit7 CLEAR → excluded from the battle list** (it is a field-only full-heal); Heal/Revive
(field-unusable, `$7700` bit1 clear) have bit7 SET → they DO appear in battle. The battle list and the field
list are governed by DIFFERENT flags.

---

## 2 — THE MENU FLOW (disasm `C1`, oracle-confirmed)

### 2.0 The battle item STAGING (`FD/B01C`, at battle entry — the T2-analogue)
> **CORRECTED 2026-09-30** (disasm/notes/battle_menu_pass.md sec. 11): the walk is C1/CE14 over 256 inventory slots, and
> an EMPTY slot is not skipped -- it goes to FD/B01C, which only advances the record pointer: an empty record, a blank row
> in the list. Only a non-empty slot that fails the filter takes no record (FD/B02B). C1/CDFF clears 0x500 bytes (256
> records), not 40. The port's old walk compacted the bag and capped it at 8.
Battle entry (after `C1/CE04` clears `$1580`, f288) walks the field inventory `$2400`/`$2500` and appends each
**battle-usable consumable** to `$1580` (5-byte records). The classifier `FD/AFD7` (consumable branch, id ≥ 0xBC):
```
3DAFD7 LDA id / CMP #BC / BCC skip           ; only id ≥ 0xBC (consumables)
3DAFE0 SBC #BC / TAX ($0A=sub) / ×3 ($02)
3DAFEE LDA $CC0ABC,X / BIT #80 / BEQ skip     ; ** FILTER: byte0 bit7 clear → NOT battle-usable, skip **
3DAFF6 STA $1580,X   (id)                     ; +0 = item id
3DAFFD LDA $CC1BB5,$0A / STA $1581,X          ; +1 = target group  (CC/1BB5[sub])
3DB00A LDA $CC0ABC,$02 / AND #7F / STA $1582,X ; +2 = selector      (CC/0ABC[sub×3] & 0x7F)
3DB015 LDA $0E / STA $1583,X                   ; +3 = count         (from $2500)
3DB01C $04 += 5                                ; next slot (5-byte stride)
```
Observed at **f289**: `$1580 = BD 80 02 03 00 | C6 00 00 02 00 | 00…` — Tonic `[id=BD, tgt=80, sel=02, cnt=3]`,
Heal `[id=C6, tgt=00, sel=00, cnt=2]`, the Pendant (`D6`, key item, not ≥0xBC-consumable) EXCLUDED. **The field
`$2400/$2500` and the battle `$1580` slots do NOT align** (Pendant is field slot0, Tonic is battle slot0).

### 2.1 Command-row dispatch (`C1/128B`, cursor `$95DC,X`, X = current seat `$95D5`)
```
1128B LDA $95DC,X / BNE .. / JMP C1/129C      ; row 0 = Att  → target-select DIRECT ($960D=7, $95DB stays 0)
11293 DEC A / BNE .. / JMP C1/12BC            ; row 1 = Tech → submenu ($95DB=1)
11299 JMP C1/12ED                             ; row 2 = Item → submenu ($95DB=2)
```

### 2.2 Item submenu OPEN (`C1/12ED`)
```
112ED LDA $A0A7 / BNE →1131D   ; gate (can't-item flag; 0 normally). BNE = skip the open, JMP 179C
112F2 STZ $A09A
112F5 LDA #02 / STA $9615      ; default target index = 2
112FA LDA $95E6 / STA $80 / JSR C1/095D  ; ** build the item list window from $1580 **
11302 JSL CFFD6A ; …copy the item-window tilemap chrome…
11318 LDA #02 / STA $95DB      ; ** $95DB = 2 = ITEM SUBMENU OPEN **
1131D JMP C1/179C
```
- `C1/095D` builds the display strings: for the 3-row window it calls `C1/09B0` per row, which reads
  `$1583,X` (count; `BEQ`→empty) then `$1580,X` (id; `BNE`→real), builds the name from **`$CC:0B5E + id×11`**
  (`10?CE ADC #0B5E`), and renders `<name> <separator-glyph> <count>` into `$0EC6` (`C1/09E7`+; the glyph
  0x2D/0x29 selection reads `$1582` bit7 & `$1584` — a per-row marker, `$1582` bit7 is always clear from staging).
  **So the battle window shows the item NAME + its COUNT** (like the field row).

### 2.3 Navigation (shared submenu handler `C1/1320`, runs while `$95DB≠0`) — item cursor is GLOBAL
```
11320 LDA $95D5 / TAX ; LDA $95EB,X + $95DF,X → $80   ; (the per-seat pair, used for display)
1132E button edges $EE/$EF dispatch:  A→C1/1369 confirm  B→C1/138F cancel  Up/Down→C1/13AD / C1/13F4
```
- Items use ONE SHARED inventory, so the item cursor/scroll is a **GLOBAL pair `$95E5`(cursor)/`$95E6`(scroll
  base)**, NOT the per-seat `$95DF,X`/`$95EB,X` the techs use. **Oracle-proven:** a Down in the item list moved
  `$95E5` 0→1 (f422) while `$95DF/$95EB` stayed 0. The selected list index = `$95E6 + $95E5`.

### 2.4 The item RESOLVER + CONFIRM-item (`C1/1498`, reached on A over the item list)
```
11498 LDA $95E6 / ADC $95E5 / STA $80 / STA $AD  ; index = scroll base + cursor
114A3 LDA #05 / STA $AE / JSR C1/0089 ; X = index×5  (5-byte $1580 stride)
114AC LDA $1582,X / BMI →skip          ; selector bit7 (grey/unusable) — clear from staging
114B1 LDA $1583,X / BEQ →skip          ; ** count == 0 → NOT confirmable ** (the only in-list block)
114B6 LDA $1580,X / STA $9F35          ; ** $9F35 = item id **
114BC LDA $1581,X / STA $960D          ; ** target group = the staged $1581 (0x80 for Tonic) **
114C2 STX $9F36 / JSR C1/1F79 / INC $9609  ; ** $9F36 = slot index (×5) ; open TARGET-SELECT **
```
Oracle (f422): A-on-Tonic → `$9F35=BD`, `$960D=0x80`, `$9F36=0`, `$9609=1`. A-on-Heal (Down first) →
`$9F35=C6`, `$960D=0x00`, `$9F36=5` (slot1×5) — **Heal IS confirmable** even with no status to cure (no
"already-full/no-status" grey in battle). **The item GREY rule = the staging FILTER** (non-battle items never
appear) **+ count==0** (a used-up stack cannot be re-picked); there is no MP-grey analogue.
CANCEL of the submenu (`C1/138F`) closes `$95DB`.

### 2.5 Target confirm → the QUEUE RECORD + CONSUME (`C1/1626` → item branch `C1/1685`)
The SAME confirm handler as Att/Tech; `$95DB` selects the branch (`DEC A` twice for item=2):
```
11626 DEC $9609                               ; close target-select
11629 LDA $95DB / BNE →11638 / DEC A / BNE →11685   ; $95DB=2 → item branch 11685
11685 LDX $9F36                               ; the selected staging slot
11688 DEC $1583,X                             ; ** CONSUME: staging count −1 (writer C1/1688 / --writer 168B) **
1168B LDA #FF / STA $9920
11690 LDA #40 / STA $80                        ; ** $80 = 0x40 = ITEM action marker **
11694 LDA $9F35 / STA $84                       ; ** $84 = item id → $93F4 **
11699 LDA #FF / STA $82                        ; target mode
116A2 STZ $95E5 / STZ $95E6                     ; clear the item cursor/scroll
116E6 (shared queue-build tail): $93EE|=0x40-pending; $93EF = $80|$9F38 = 0x40 (item); $93F0 = $9615 (target);
      $93F4 = $84 (item id) ; then dequeue+advance ($A6D9[seat]=FF, $95D5 ← next seat) — battle_confirm_queue.md
```
Observed queue `$93EE` = `41 40 02 00 00 FF BD` — `$93EF=0x40` (item), `$93F4=0xBD` (Tonic id), and `$1583`
03→02 **at the same frame f442** (consume is at target-commit, not at animate). CANCEL of target-select before
this commit = NO consume (the `DEC` is in the commit branch).

---

## 3 — THE DISPATCH + CONSUME TIMING

### 3.1 Dispatch (`C1/BB23` stages `$AE93 = item id`)
The queue `$93F4` → `$AE93` at animate (f451, writer `C1/BB23`). `$AE93` = **0xBD** (the item id) — the item
analogue of the tech id / attack record index. It drives BOTH the anim (§5) and the effect (§4).

### 3.2 CONSUME timing — at CONFIRM, on the battle COPY
- **When:** the count `$1583,X` decrements at **CONFIRM/target-commit (f442, `C1/1688`)** — 9 frames BEFORE
  dispatch (f451), well before the freeze (f453). NOT at animate (contrast: tech MP at animate).
- **Where:** the **battle staging copy `$1580`** (`+3` = count). The field inventory `$2500` is UNTOUCHED for
  the whole fight (oracle: `$2500` = `01 03 02` constant f0→f740; only `$1580[+3]` went 03→02).

### 3.3 The field-inventory MERGE-BACK (`C1/B500`, at battle end)
The field `$2400/$2500` is reconciled from `$1580` at battle end (adjacent to the HP writeback `C1/B442`):
```
1B500 loop staging slots (+5):
   LDA $1580,X / BEQ done              ; per staged item id
   …search $2400 for a matching id → STA $2400 / STA $2500 (update count)      (C1/B529/B52E)
   …or first empty $2400 slot → STA $2400 = $1580 id / STA $2500 = $1583 count (C1/B54D/B557)
```
So the consumed Tonic (`$1583` = 2) reconciles to the field `$2500` = 2 at battle end — a battle COPY merged
back, NOT an immediate field write. ⚠ The end-to-end `$2500` update is **inferred from the disassembled routine
+ the "copy untouched during battle" observation**; the live fight-completion was not reached headless (the
GATO fight does not end within the probe window) — the routine itself is unambiguous (§7 O1).

---

## 4 — THE EFFECT (Tonic +50 HP, byte-exact)

The effect stages a HP delta into `$B328,Y` at dispatch (`C1/EC2F`) and applies it at span-end via the
**universal curHP writer `C1/ED1A`** (`STA $5E30,X`) — the SAME routine attack/tech damage uses:
```
$B328,Y = −(amount)  = −50 = 0xFFCE   (writer C1/EC2F)              ; heal = NEGATIVE damage
amount  = CC/05CC[sub] base × mult    = 50 × 1 = 50                 ; shared field effect table
curHP  −= $B328  (= +50)  then clamp to maxHP                        (C1/ED1A / C1/ED17)
```
Measured (`gato_item.state`, Crono 15/70): `$5E30` (seat0 battle curHP = block `$5E2D`+3) **0x000F → 0x0041**
(15→65, +50) at **span-end f619** — the SAME anim-completion frame as the freeze-clear (`$A013`→0), byte-exact
timing with the tech/attack HP apply. **Amount source proven:** +50 = `CC/05CC` base(0x32)×mult(1) = 50; it
RULES OUT `CC/0ABC[+1]` (Tonic 0x0A = 10) — the battle heal reads the shared `CC/05CC`, unifying with the
field-Use screen ([[ct-field-item-menu-use]], "Tonic HP50").

**Cap at maxHP — proven:** on `gato_item_cap.state` (Crono curHP 40): `$5E30` **0x0028 → 0x0046** (40 → **70**,
NOT 90) at f619 — the delta is clamped so `curHP` never exceeds maxHP (block+5 = `$5E32`), the same clamp the
field HP-write uses (`C2/8E82`, "caps at effective max").

**Target group 0x80** (`$960D`): Tonic's target-select is over the PCs and healed the CONFIRMED PC (Crono,
seat0). The group byte is per-item from `CC/1BB5` (Tonic 0x80 single-PC; Heal 0x00; Lapis 0x81 all-party). The
exact 0x80-vs-0x00 encoding (both PC-targeting healers) is a minor open (§7 O2).

---

## 5 — THE PRESENTATION (anim `CD/5C26`)

For an item, `$AE93` = the item id (≥0xBC). The anim selector (`C1/475B`) branches on it:
```
1475B SEC / LDA $AE93 / SBC #BC / STA $80        ; sub = item id − 0xBC   (Tonic → 1)
14765 ASL×3 / SBC $80 / TAX                       ; X = sub×7  (7-byte stride)
1476F LDA $CD5C26,X / STA $9877                   ; ** $9877 = CD/5C26[sub×7][0] **  (Tonic → 0x01)
      …stages the 7-byte record → $987A/$987B/$987E/$9881/$9884/$987C…
147CB LDA $9877 ×2 / LDA $CD63F0,X / STA $9878    ; ** script base $9878 = CD/63F0[$9877×2] = 0x829C **
```
`CD/5C26` records (7-byte, per consumable): sub1 (Tonic) `01 E1 8A 1D 00 7F FF` → `$9877=01`; sub2/3
(Mid/FullTonic) `02/03 …`. **The PC-item anim path = `CD/5C26` (config) + `CD/63F0` (base)** — distinct from the
PC-tech path (`CD/45A6` + `CD/5EF0`, tech slice §4) and the enemy tables (`CD/5FF0`/`61F0`). Observed span
**166f** (freeze f453→f619), popup born f552 (`$A3D1`→1/2), 58f life to f610, HP applied f619. **OPEN (same
boundary as the tech whirlwind / GATO flurry):** the item-use POSE pixels (Crono raises the item) come from the
E3/E4 pose engine (`battle_anim_engine.md`); the `CD/5C26`→bank-CD/CE script decode is not unrolled here.

---

## 6 — IMPLEMENTATION SPEC (for `src/battle.c` — the item generalises the tech seam)

The engine already has the tech SEAMS (`isTech` through `battleActionSpan`/`battleComputeTechDamage`/
`battleOpenAction`, and the `battleCommitPcAction`/`battleDispatchScheduled` confirm/dispatch split). The item
**generalises them to an action-kind** (att / tech / item). What the slice adds:

**(a) Menu — retire the "A-on-Item INERT" boundary** (the `$95DB` deliberately-not-written boundary).
- On A@row2 (`$95DC==2`): open the submenu (`$95DB=2`), build the item list from the SEAT-shared **battle item
  staging**. Needs the staging: filter the field inventory (`$2400/$2500`) to battle-usable consumables
  (`CC/0ABC[sub×3]` bit7, id≥0xBC) into a 5-byte-record buffer `[id, CC/1BB5 tgt-group, CC/0ABC&0x7F sel, count, 0]`
  (`FD/B01C`). Keep it a COPY (do NOT mutate `$2400/$2500`).
- Nav a 3-row window over the buffer with a GLOBAL cursor/scroll (`$95E5`/`$95E6`, not the per-seat tech pair);
  confirm-item resolves `$9F35=id`, `$960D=tgt-group($1581)`, `$9F36=slot×5`, gated on `count≠0`; open
  target-select with the group.
- Target-confirm: **DEC the staging count** (consume at commit), build the queue record `$93EF=0x40` +
  `$93F4 = item id`.

**(b) Dispatch — carry the item through `battleCommitPcAction` → `battleDispatchScheduled`** with kind=ITEM.
Push `{itemId, tgtSlot}` on the sched FIFO at commit; at animate: set `$AE93 = itemId`, compute the effect (c),
`$9877 = CD/5C26[(itemId−0xBC)×7]`, span (interim: pin `CT_SPAN_PC_ITEM_TONIC = 166`, the `CT_SPAN_*` precedent),
`$95DB=2` + `$A013=1`. **NO MP** (items cost no MP).

**(c) Effect — Tonic = the HP writer with a NEGATIVE delta.** `amount = rom[CC/05CC + sub×4 + 3(base)] ×
rom[+2(mult)]` (already the field `ctItemFx*` — see `menu_itemuse.h` / [[ct-field-item-menu-use]]); stage
`$B328 = −amount`; at span-end apply `curHP -= $B328` (= +amount) **clamped to maxHP** — the existing universal
HP-apply seam (`C1/ED1A`; damage stages positive, heal negative). The effect KIND comes from the selector
(`CC/0ABC[sub×3] & 0x7F`: 0x02 = HP, and by extension MP/status/revive — the field `ctItemFxKind` covers these).

**(d) Consume-back** — the field `$2400/$2500` merges from the staging buffer at battle end (`C1/B500`): update
the matching id's count or add to the first empty slot. (Our engine already writes HP back at end via the
`g_sheetCarry` seam; add the inventory reconcile alongside it.)

**Tables to bake (like `menu_romdata.h` / `menu_itemuse.h`):** `CC/0ABC` (battle-usable bit7 + selector),
`CC/1BB5` (target group — ⭐ already the field `ctItemFxScope` source), `CC/05CC` (amount — ⭐ already
`ctItemFx*`), `CD/5C26` + `CD/63F0` (anim), the `CC/0B5E` names (⭐ already `CtMenuItemName`).

**Checks to pin (`gato_item.state` = `gato_press_action` + `2400+1=BD 2500+1=3 2400+2=C6 2500+2=2 2603=15`;
btnscript `100:8,0:373,20:2,0:10,20:2,0:10,100:2,0:14,100:2,0:14,100:2,0:300`):**
1. `--monitor "1580:10"` → `BD80020300C60000 02..` at f289 (staging FILTERED: Tonic+Heal, Pendant D6 excluded).
2. `--monitor "95DB:1"` → `$95DB`=2 at f407 (A-on-Item opens the submenu, not inert).
3. `--monitor "960D:1,9F35:1"` → `0x80` / `0xBD` at f422 (resolver: target group + item id).
4. `--monitor "93EF:1,93F4:1"` → `0x40` / `0xBD` at f442 (queue: item marker + id).
5. `--monitor "1580:5"` → `$1583` 03→02 at f442 (consume at confirm, on the copy).
6. `--monitor "5E30:2,A013:1"` → curHP `0x000F→0x0041` (+50) at f619 = span-end (heal); cap seed (curHP 40)
   → `0x0028→0x0046` (clamp 70) at f619.

---

## 7 — OPENS / BOUNDARIES

| # | item | status |
|---|---|---|
| O1 | **The end-to-end field `$2500` merge-back** | routine `C1/B500` disassembled + "copy untouched during battle" observed; the LIVE count reaching `$2500` not reached headless (GATO fight doesn't end in the probe window). Reasoned-certain, not frame-observed |
| O2 | **Target-group encoding for healers** (Tonic 0x80 vs Heal 0x00, both PC-target) | the bytes (`CC/1BB5`) + Tonic=PC-target (healed Crono) are pinned; the exact group-code → PC-select semantics for 0x00 not disambiguated (single-GATO / no-status Crono can't distinguish) |
| O3 | **The item-use POSE pixels** (`CD/5C26`→CD/CE script; Crono raising the item) | presentation OPEN — same boundary as the tech whirlwind / song notes |
| O4 | **Non-HP selectors live** (Heal 0x00 status-cure, Revive, Ether MP) | the tables (`CC/0ABC` sel, `CC/05CC` amount) are pinned; only the HP selector (0x02, Tonic) was measured in-battle end-to-end. Heal was confirmed CONFIRMABLE but its status-cure effect on a no-status PC not measured |
| O5 | **Throw-items / multi-target / all-party (Lapis 0x81)** | the group byte staged; the all-party resolve + the equipment→other-buffer branches of `FD/B01C` (id<0xBC) not exercised |
| O6 | **The `$A0A7` can't-item gate** | the submenu-open gate (`C1/12ED` BNE) reads `$A0A7`; the condition that sets it (status? sealed?) not derived (0 here) |

---

## §T4 — THE MAGIC DAMAGE FORMULA PROBE (tech-slice T4 open) — see battle_tech_slice.md §T4 for the append

The magic-formula measurement (Marle's Ice vs GATO) is derived in the SAME session; the **`/102.4` hypothesis
is REFUTED**. Full note appended to `battle_tech_slice.md §T4`. Summary: one clean cast, Ice (power 11) by Marle
(magic 8, level 1) vs GATO (magic-def 50) = **40 damage**; the real formula is type-3 (`C1/E203`) built from the
attacker's **Level (block+0x12) and Magic (block+0x39)**, NOT `curMagic/102.4`.

---

## SCRATCHPAD ARTIFACTS (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-9e84b785-…/scratchpad/`
- `ct_probe.exe` (= build/ct.exe). Base: repo `gato_press_action.state`; injected `gato_item.state`
  (`2400+1=BD 2500+1=3 2400+2=C6 2500+2=2 2603=15`), `gato_item_cap.state` (`2603=40`, the maxHP-cap test),
  `gato_ice.state` (`2838|=0x20`, Marle learns Ice, the §T4 magic probe).
- Disasm anchors (file offset = `(bank−0xC0)<<16|addr`): command dispatch `0x1128B`; Item-open `0x112ED`; list
  build `0x1095D`/row `0x109B0`; resolver `0x11498`; confirm-item + consume `0x11685`/`0x11688`; queue tail
  `0x116E6`; anim `0x1475B`; damage stage `0x1EC2F`; HP apply `0x1ED1A`; staging `0x3DAFD7`/`0x3DB01C`;
  merge-back `0x1B500`; magic formula `0x1E203`.
- ROM tables: `CC/0B5E` names, `CC/0ABC` battle-usable+selector, `CC/1BB5` target-group, `CC/05CC` amount,
  `CD/5C26` anim config, `CD/63F0` anim script base.
