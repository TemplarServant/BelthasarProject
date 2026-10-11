# THE NON-HP BATTLE ITEMS — Ether (MP), Heal (status cure), Revive (un-dead) — the item-slice O4 close

Closes `battle_item_slice.md` **O4** ("Non-HP selectors live"). The HP heal (Tonic, selector-2) shipped in
`76cfb7f`; this dossier derives, from the ROM disasm + LakeSnes oracle **only**, the other three consumable
effect kinds end-to-end, each oracle-measured on a purpose-built seed. Every field is byte-cited
(`tools/dis.py <fileoffset>`, fileoffset = `(bank−0xC0)<<16 | addr`) and every value is oracle-measured
(`ct_probe.exe` = `build/ct.exe`, `--monitor "ADDR:LEN" --from STATE --btnscript ... --diff --writer`; ⚠ **LEN
is DECIMAL**; writer PC = the instruction AFTER the store).

> ⚠ **ID CORRECTION.** The mission brief guessed "Ether 0xC9?"; the ROM name table `CC/0B5E` proves **Ether =
> 0xC0** (sub4). 0xC9 is "Power Meal". Verified ids (name table `CC/0B5E + id*11`): Tonic 0xBD, Mid Tonic 0xBE,
> Full Tonic 0xBF, **Ether 0xC0, Mid Ether 0xC1, Full Ether 0xC2**, Elixir 0xC3, HyperEther 0xC4, MegaElixir
> 0xC5, **Heal 0xC6, Revive 0xC7**, Shelter 0xC8, Power Meal 0xC9, Lapis 0xCA.

---

## 0 — HEADLINE RESULTS (all oracle-measured; seeds = `gato_item.state` recipe + a per-item patch)

1. **The effect record is `CC/05CC[sub*4] = [b0,b1,b2,b3]`** (sub = id−0xBC), read (for the restore path) by
   `C1/D1D3` as **`$16 = (b3 & 0x3F)`** (base), **`$18 = (b2 & 0x0F)`** (mult), **`$1A = b1`** (the HP/MP/undead
   selector). The **universal restore amount = `(b3 & 0x3F) × (b2 & 0x0F)`** (`$16 × $18`, mult `C1/C90B`;
   `(b2&0x0F)==0x0F` = FULL → maxHP/maxMP, `1DA57`). `$1A` bit7 = **HP** component, bit6 = **MP** component.
   The number is applied by the SAME universal delta pipeline the HP heal uses (`$AD89` → negate `C1/EBF8` →
   `$B328` → `C1/ED1A`), with the HP-vs-MP target chosen by the delta's flag `$B32B` bit6.

2. **Ether (0xC0) = MP restore, +10.** `CC/05CC[4] = 80 40 01 0A` → b1=**0x40 (MP)**, amount = `(0x0A&0x3F) ×
   (0x01&0x0F)` = **10×1 = 10**. Applied to **curMP = block+0x07** (`C1/ED27 SBC $5E34,X`), **clamped to maxMP =
   block+0x09** (`C1/ED55-ED65`). Measured: Marle curMP **1→11** (writer `C1:ED68`, span-end f599); clamp 8→**12**;
   **Mid Ether (0xC1)** `80 41 03 0A` → 10×3 = **+30** (1→31 on a raised maxMP). HP untouched.

3. **Heal (0xC6) = status cure, no HP.** `CC/05CC[10] = 04 00 00 00`, selector (`CC/0ABC[10*3]&0x7F`) = **0x00**.
   Reaches the cure `C1/DB5E`: **`+0x1D &= 0xC0`, `+0x1E &= 0x00`, `+0x1F &= 0xC2`** (+ clears the per-status
   timers `$AFB6/$B02F/$AFC1/$AFCC/$B00E` for a PC). Measured byte-exact live: +0x1D `0x61→0x40` (keeps bit6),
   +0x1E `0x04→0x00`, +0x1F `0x0F→0x02` (keeps bit1); **HP flat**. **Keeps the dead bit** (`&0xC0` retains bit7):
   Heal on a DEAD PC does NOT revive (measured: stays HP 0 / +0x1D 0x80). Applied at **DISPATCH** (f454), not
   span-end.

4. **Revive (0xC7) = un-dead + cure + fixed 50 HP.** `CC/05CC[11] = 04 80 01 F2`, selector 0x00, b1=**0x80**.
   The **un-dead** = `C1/DB5E`'s `$16 bit7` gate (`1DB64 LDA $16 / BIT #80 / BEQ` → `+0x1D &= 0x7F`), fed by b1
   bit7: dead bit **+0x1D 0x80→0x00** at dispatch (f454, `C1:DB78`). The **HP amount = `(b3 & 0x3F) × (b2 &
   0x0F)` = (0xF2&0x3F) × (0x01&0x0F) = 0x32×1 = 50** (`$16=50`,`$18=1`,`$1A=0x80` measured; C1/D1D3). **FIXED
   50, NOT maxHP-scaled** (measured 50 on maxHP 65 AND 200). Applied to curHP **0→50** at **span-end** (f675,
   `C1:ED1A`), clamped to maxHP. **ATB auto-re-arms**: once alive (HP>0, dead-bit clear) the tick `C1:826B`
   resumes draining her timer from empty (0x66→0x65…, f677). Also cures status (`DB5E` masks).

5. **All three pass the battle-usable filter** (`CC/0ABC[sub*3]` bit7 SET): Ether `82`, Heal `80`, Revive `80`
   — all appear in the `$1580` staging (Shelter `02` = field-only, excluded; item lane §2.0). **Ally/dead-PC
   target-select works** (partial O2 close): Down-nav moved `$9614` to seat1 for each, and Revive's group
   `CC/1BB5[11]=0x03` allowed selecting the fallen Marle.

6. **Anim selector `$9877 = CD/5C26[sub*7][0] = the item sub`** (Ether 0x04, Heal 0x0A, Revive 0x0B). **Frozen
   spans** ($A013 window): **Ether 142f, Heal 125f, Revive 218f** (Tonic 166f). **Popup**: HP/MP restore shows the
   magnitude number (Ether 10, Revive 50, inside its span); **Heal shows NO number popup** (pure cure).

---

## 1 — THE DATA (ROM tables; `sub = id − 0xBC`)

**Item identities + effect records (dumped from the ROM):**
```
id  sub name        CC/0ABC[sub*3]        CC/1BB5   CC/05CC[sub*4]   effect
                    (usable,sel) b1 b2    (tgtgrp)  b0 b1 b2 b3
BD   1  Tonic       82(u1 s02) 0A 00      80        80 80 01 32      HP  50   (=0x32&3F × 1)   [shipped]
C0   4  Ether       82(u1 s02) 20 03      80        80 40 01 0A      MP  10   (=0x0A × 1)
C1   5  Mid Ether   82(u1 s02) D0 07      80        80 41 03 0A      MP  30   (=0x0A × 3)
C2   6  Full Ether  82(u1 s02) 70 17      80        80 42 06 0A      MP  60   (=0x0A × 6)
C6  10  Heal        80(u1 s00) 0A 00      00        04 00 00 00      cure (b1=0 → no restore, no undead)
C7  11  Revive      80(u1 s00) C8 00      03        04 80 01 F2      undead+cure+HP 50 (=0xF2&3F × 1)
C8  12  Shelter     02(u0 s02) 96 00      01        08 C2 0F 00      FIELD-ONLY (usable bit7 CLEAR → excluded)
CA  14  Lapis       82(u1 s02) F4 01      81        80 81 04 32      HP 200, group 0x81 (all-party) — OPEN O5
```
**Effect-record byte roles (b0=0x80 restore family / b0=0x04 cure-revive family):**
- **b1 (=`$1A`)**: bit7 = HP component present, bit6 = MP component present, (cure family) bit7 = **un-dead**.
  Ether 0x40 (MP), Tonic/Revive 0x80 (HP), Elixir 0xC2 (HP+MP), Heal 0x00 (neither).
- **b2 (=`$18`, `& 0x0F`)** = the amount MULTIPLIER (`0x0F` = FULL restore → maxHP/maxMP).
- **b3 (=`$16`, `& 0x3F`)** = the amount BASE.
- **selector `CC/0ABC[sub*3] & 0x7F`**: 0x02 = restore-number kind (Tonic/Ether family), 0x00 = cure/revive kind
  (Heal/Revive). Gates whether the cure `C1/DB5E` runs; the restore is b1-gated (runs for any item with b1 HP/MP).

---

## 2 — THE MP RESTORE PATH (Ether) — `C1/ED1D`

The delta pipeline is shared with the HP heal, EXCEPT the apply routes on the delta flag `$B32B` bit6:
```
1EC85 LDA $B32B,Y / BIT #40 / BEQ 1EC8F / [1EC8C] JMP C1/ED1D   ; $B32B bit6 SET → the MP branch
   (HP branch 1EC99: LDA $5E30,X / SBC $B328,Y / STA $5E30,X)   ; curHP -= delta   (block+3)
1ED1D REP #20 / LDA $B32A,Y / BIT #4000 / BEQ 1ED31
1ED27   LDA $5E34,X / SEC / SBC $B328,Y / STA $5E34,X           ; ** curMP -= delta ** (block+7)
1ED55 LDA $5E34,X / BMI 1ED64(→0) / CMP $5E36,X / BCC 1ED65 / LDA $5E36,X   ; ** clamp to maxMP (block+9) **
1ED65 STA $5E34,X
```
`$B32B` bit6 is set from the effect record's b1 bit6 (MP) via `$B202→$B203` (`C1/EBF8` `1EBF9 LDA $B202 / STA
$B203`; the negate-if-bit7 makes the delta negative = a restore). **Ledger (`ether_restore.state`, Marle
curMP 1/12):**
```
f288  5EB4=01 5EB6=0C            Marle MP staged 1/12                 (FD:AE83)
f442  1583 03→02                 consume Ether at confirm             (C1:168B)
f454  9614 1→0                   target-confirm (Down-nav → seat1)    (C1:16D7)
f599  5EB4 01→0B                 ** curMP += 10 = 11 ** (span-end)    (C1:ED68)
```
Clamp (`ether_cap.state`, curMP 8): 8→**0x0C=12** (f599). Mid Ether (`mid_ether.state`, curMP 1, maxMP 99): 1→**0x1F=31** (+30).

---

## 3 — THE CURE / REVIVE PATH — `C1/DB5E`

```
1DB5E TDC / LDX $B1F6
1DB62 LDA $16 / BIT #80 / BEQ 1DB70      ; ** $16 bit7 = UN-DEAD (from record b1 bit7) **
1DB68   LDA $5E4A,X / AND #7F / STA $5E4A,X   ; clear +0x1D bit7 (dead) — REVIVE only
1DB70 LDA $5E4A,X / AND #C0 / STA $5E4A,X ; +0x1D &= 0xC0  (keeps dead bit7 + status bit6)
1DB78 LDA $5E4B,X / AND #00 / STA $5E4B,X ; +0x1E &= 0x00  (clears all)
1DB80 LDA $5E4C,X / AND #C2 / STA $5E4C,X ; +0x1F &= 0xC2  (keeps bits 1,6,7)
1DB88 LDA $B1FD / CMP #03 / BCS 1DBA9     ; PC (seat<3)?
1DB90   STZ $AFB6,X / STZ $B02F,X / STZ $AFC1,X / STZ $AFCC,X / STZ $B00E,X   ; clear the status TIMERS
```
Same masks as `C1/F036` (the "Life"-tech cure). The Heal/Revive ITEM reaches `DB5E` at **DISPATCH** (f454).
**Heal ledger (`heal_cure.state`, Marle +0x1D=0x61 +0x1E=0x04 +0x1F=0x0F):** f454 → **0x40 / 0x00 / 0x02**, HP
flat (`C1:DB78/DB80/DB88`). **Heal keeps dead (b1 bit7 clear):** `heal_dead.state` (Marle dead) → stays HP 0 /
+0x1D 0x80.

**Revive HP source** (`C1/D1D3` loader, effId `$B18C`=subB=11, ×4):
```
1D1E2 LDA $CC05CF,X / AND #3F / STA $16   ; $16 = (b3=0xF2) & 0x3F = 0x32 = 50   (base)
1D1EA LDA $CC05CE,X / AND #0F / STA $18   ; $18 = (b2=0x01) & 0x0F = 1           (mult)
1D1F2 LDA $CC05CD,X / STA $1A             ; $1A = (b1=0x80)                       (HP component + undead)
```
→ restore handler `C1/DA37`: `$00 = $16 × $18 = 50 × 1 = 50` (`1DA46-1DA53`; `$1A` bit7 → HP, `1DA37`),
staged as a −50 heal (`$B202=0x80` `C1:DAB4` → `C1/EBF8` negate → `$B328=0xFFCE`). **Revive ledger
(`revive.state`, Marle dead 0/65):**
```
f288  5EB0=00 5EB2=41 5ECA=80         Marle staged DEAD (HP0, +0x1D bit7)      (FD:AE83)
f454  5ECA 80→00                      ** un-dead ** (+ cure) at dispatch       (C1:DB78)
f455  B32C=FFCE (−50) B202=80         −50 HP heal staged                        (C1:EC2F / C1:DAB4)
f675  5EB0 00→32                      ** curHP = 50 ** (span-end)               (C1:ED1A)
f677  AFAC 66→65 …                    ** ATB re-arms ** (drains from empty)    (C1:826B)
```
maxHP-independence: `revive_hi.state` (maxHP 200) → still **50**.

---

## 4 — ANIM / SPAN / POPUP

`$9877 = CD/5C26[sub*7][0]` = the item sub (Tonic 0x01, Ether 0x04, Heal 0x0A, Revive 0x0B; records dumped
`CD/5C26`). The bank-CD/CE item-anim playback is the O3 presentation BOUNDARY (as with Tonic); the **frozen
`$A013` span is pinned per item**: **Ether 142f (f457→599), Heal 125f (f457→582), Revive 218f (f457→675)**.
Popup ($A3D1→1/2, 58f): the HP/MP restore magnitude number spawns inside the span (Ether, Revive); **Heal
spawns NO number popup** (pure cure — its later f692 popup is GATO's own turn, outside Heal's span).

---

## 5 — IMPLEMENTATION SPEC (`src/battle.c`; generalises the item HP heal via the SAME action-kind seam)

The item lane already threads {itemId, tgt} through the schedule FIFO and applies the effect at span-end. What
this adds (NO new dispatch path; the SAME `battleComputeItemEffect` at dispatch + one apply seam at span-end):

**(a) `battleComputeItemEffect` (dispatch)** — route the restore amount by the record's b1:
`sub=id−0xBC; b1=CC05CC[sub*4+1]; base=CC05CC[+3]&0x3F; mult=CC05CC[+2]&0x0F; amount = (mult==0x0F)? maxRes :
base*mult;` if `b1&0x80` → HP delta, if `b1&0x40` → **MP delta** (set `h->actItemMp=1`). Cure-only items
(selector 0x00, b1 no HP/MP) → delta 0. `actValue = −amount` (drives the popup magnitude).

**(b) Span-end apply** — replace `battleApplyDamage(actValue)` for items with `battleApplyItemEffect(tgt,id,
actValue)`: (1) if selector==0x00 → cure `block+0x1D &= (b1&0x80 ? 0x40 : 0xC0)` (the un-dead-folded mask),
`+0x1E &= 0x00`, `+0x1F &= 0xC2`; (2) if `actValue!=0` → apply to **curMP (clamp maxMP)** if `actItemMp` else
**curHP (clamp maxHP)** — the same clamp `battleApplyDamage` uses. Revive's dead-bit clear (in (1)) flips
`battleAlive` true; the existing ATB tick resumes the (max=empty) timer for free.

**(c) Popup** — spawn the magnitude number only for `actValue != 0` (suppresses Heal's number).

**(d) Span** — `battlePcItemSpan`: Ether 0xC0→142, Heal 0xC6→125, Revive 0xC7→218 (Tonic 0xBD→166; default 166).

**(e) Ally target-select (item lane O2 partial-close)** — the item now heals/revives the PC CHOSEN in target-
select, not just the chooser. Add `h->itemTgtSel` (default = the chooser at open), navved over the PRESENT
roster seats (`$2980` bit7 clear, Down/Right +1 / Up/Left −1) in `battleItemSubmenu`'s target-select branch;
`battleCommitPcAction` commits it as `schedItemTgt`. `battleDispatchScheduled` resolves the item target with
**`battlePresent` (NOT `battleAlive`)** so a DEAD ally (the Revive target) is kept. Proven: oracle Down-nav →
seat1 for all three. The group→selectable-set restriction (Ether/Heal alive-only vs Revive fallen-only) + the
target-cursor RENDER (a bracket on the chosen PC) are the remaining O2 boundary — the gameplay target is
correct + validated; the HUD cursor is the O3 presentation boundary (as with the item-use pose).

**DEVIATION (documented, à la miss-evade §3):** the ROM cures/un-deads at DISPATCH (f454); we apply the whole
item effect (cure+undead+HP/MP) at SPAN-END (reusing the one item apply seam). The FINAL state is byte-exact;
only the intra-span render pose (a dead ally showing dead vs un-dead for the ~218f revive window) differs — that
pose is the O3 boundary. Nothing in the engine reads status/dead mid-span, so every pin is unaffected.

**Tables (already baked or read via `CtBattleRom`):** `CC/0ABC` selector, `CC/05CC` b1/b2/b3, `CC/1BB5` group,
`CD/5C26` anim.

---

## 6 — OPENS / BOUNDARIES

| # | item | status |
|---|---|---|
| ~~O1~~ | **Full-restore items** (Elixir 0xC3 / MegaElixir 0xC5, record `80 C2 0F 00`: b1=0xC2 = HP bit7 AND MP bit6, mult 0x0F = full) | **CLOSED 2026-08-22**: the dual HP+MP apply is now PORTED and MEASURED. C1/DA37's HP component falls through at C1/DAD1 into the MP component (two bit gates, not a selector); the span-end applier C1/EC85..ED65 does curHP then curMP. Oracle (gato_elixir.state, Marle 10/65 HP 1/12 MP): f658 `4100 4100 0C00 0C00` (C1:ED68) = both full. Our `(b1&0x40)&&!(b1&0x80)` selector dropped the MP half for 0xC2; fixed to compute+apply both. Pinned by `field-battle-item-elixir-hp-and-mp`. |
| O2 | **Ally target-select** — single-ally CLOSED (implemented §5e, validated on seat1). REMAINING: the group→selectable-set rule (alive-only vs Revive's fallen-only), the all-party resolve (Lapis 0x81), and the target-cursor RENDER (HUD bracket on the chosen PC = O3). |
| O3 | **The item-use POSE pixels** (bank-CD/CE `CD/5C26`→`CD/63F0` script; the revived ally standing up) | presentation OPEN — same boundary as Tonic / the tech whirlwind. |
| O4 | **Cure/undead APPLY TIMING** (ROM: dispatch; ours: span-end) | net-final byte-exact; the intra-span render pose is the only difference (§5 DEVIATION). |
| O5 | **The per-status timer semantics + status pose/HUD** | `DB5E` clears the timers; their onset behavior + the status icon are `battle_status_pipeline.md` O3/O4 (not needed for the cure). |

---

## SCRATCHPAD ARTIFACTS (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-9e84b785-…/scratchpad/`
- `ct_probe.exe` (= build/ct.exe). Base `gato_item.state` (= `gato_press_action` + inventory/HP patch, item lane).
  Seeds (all = `gato_item` + a patch via `patch_st.py SRC DST addr=val…`, WRAM file offset 0x45+addr):
  `ether_restore.state` (`2401=C0 2501=03 2402=00 2502=00 2657=01` — Ether, Marle curMP 1);
  `ether_cap.state` (`…2657=08`); `mid_ether.state` (`2401=C1 …2657=01 2659=63` — Mid Ether, maxMP 99);
  `heal_cure.state` (`2401=C6 2501=02 … 266E=04` — Heal, Marle +0x1E sleep); `heal_mask.state` (`…266D=61
  266E=04 266F=0F`); `heal_dead.state` (`2401=C6 … 2653=00 2654=00 266D=80` — Heal on a dead PC);
  `revive.state` (`2401=C7 2501=02 … 2653=00 2654=00 266D=80` — Marle dead); `revive_hi.state` (`…2655=C8`).
- btnscript (Down-nav → Marle seat1): `100:8,0:373,20:2,0:10,20:2,0:10,100:2,0:14,100:2,0:14,20:2,0:10,100:2,0:340`.
- Disasm anchors (file offset): MP apply `0x1ED1D`/`0x1ED27` (clamp `0x1ED55`); delta stage/negate `0x1EBF8`;
  cure/undead `0x1DB5E` (undead gate `0x1DB64`); restore handler `0x1DA37` (amount `0x1DA46`, full `0x1DA57`);
  revive loader `0x1D1D3` (`$16=b3&0x3F` `0x1D1E2`). ROM tables: names `CC/0B5E`, selector `CC/0ABC`, effect
  `CC/05CC`, group `CC/1BB5`, anim `CD/5C26`.
