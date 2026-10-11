# THE TECH COMMAND — vertical slice: Crono's CYCLONE (tech id 1), menu → dispatch → MP → damage → anim

Derives, from the ROM disasm + LakeSnes oracle **only**, the full path for ONE physical PC tech end-to-end
plus the shared tech infrastructure every tech reuses. The slice PROVES the pipeline; the other techs are data
in the same tables. **Zero invention** — every field is byte-cited (`tools/dis.py <fileoffset>`, fileoffset =
`(bank−0xC0)<<16 | addr`) and every value is oracle-measured (`ct_probe.exe` = `build/ct.exe`).

> ⚠ **The first GATO fight has NO techs.** Crono learns Cyclone via tech points; `gato_press_action.state`
> (the canonical fight) has his single-tech seed `$7E2837 = 0` → the tech list is empty (`$9551`=all FF,
> `$A02A`=0, `$9EE3-9`=FF). To exercise Cyclone I patched the persistent seed `$7E2837 |= 0x80` (MSB = the
> char's first single tech) in the FIELD state, then booted the battle — the battle-entry staging then stages
> Cyclone into the list. `gato_cyclone.state = patch_state.py gato_press_action.state gato_cyclone.state
> 2837=80`. This is a canonical injection (like the `crono_left/right` patched-state method); the tech's DATA
> is static ROM, independent of the learned flag.

---

## 0 — HEADLINE RESULTS (all oracle-measured on `gato_cyclone.state`)

1. **Cyclone = tech id 1** (Crono's first single tech). Static ROM data: name "Cyclone" (`CC/15C4 + 1×11`),
   **MP 2** (`CC/253B[1]`), target group **0x12**, effect-header **`CC/213F[1]`** (formula type 1, defsel 0x3E,
   **power 10**), anim script base **`CE/0909`**.

2. **The command Tech row is a per-seat SUBMENU** keyed on `$95DB` (the "$95DB submenu boundary"): **0 = Att,
   1 = Tech, 2 = Item** (confirm handler `C1/1629 LDA $95DB`). A-on-Tech opens `$95DB=1`; the tech list, its
   nav, the MP-grey rule, and the confirm→target-select→queue chain are all DERIVED (§2).

3. **The chosen tech reaches dispatch via the queue record** `$93EE,X`: `$93EF = 0x20` (tech action marker),
   **`$93F4 = $9EE3` (the tech id)**. `$9EE3` is the PC-tech analogue of the enemy AI's `$AEE3`.

4. **MP is spent at ANIMATE-time (dispatch), not at confirm** — `$5E34` 8→6 (−2 = Cyclone's cost) at f436,
   writer **`C1/CC8F`** (`$5E34 -= cost`), two frames BEFORE the freeze (f438). Seat-dispatched: seat0
   `$5E34` / seat1 `$5EB4` / seat2 `$5F34`.

5. **DAMAGE = the PHYSICAL pipeline with a TECH coefficient — NOT the magic path.** Cyclone's effect-header
   (`CC/213F[1]`) is formula type 1 / defsel **0x3E (physical defense)** / power **10**. Same base formula as a
   weapon attack (`CC/213F[0x3A]` power 8) — only the power byte differs. **Predicted == measured, byte-exact:**
   `base = 10×(A36×12 + A43×5)/18 = 10×75/18 = 41; dmg = 41×(256−127)/256 = 20; + bonus rand(0..A36/3=1) = 21`
   → GATO 76→55 (−21) at span end. (The magic `/102.4` path is UNEXERCISED — Cyclone is physical; left OPEN.)

6. **The anim is a bank-CE tech script — the PC-tech table is `CD/45A6`/`CD/5EF0`, NOT `CD/61F0`.**
   `$AE93 = tech id (1)` → `$9877 = CD/45A6[$AE93×7][0] = 1` → `$9878 = CD/5EF0[$9877×2] = 0x0909` → script
   at **`CE/0909`** (4 tracks; strike + multi-target in SUPER-subscript `CE/D613`). Span **313f**
   (freeze f438→f751). The `CD/61F0` "tech table" is the ENEMY-tech path (GATO's song); PC techs go through
   `CD/45A6`/`CD/5EF0` → bank CE.

### The slice ledger (one clean run: boot A, settle, Down→Tech, A, A, A)
```
f381  $95DC 0→1        Down: command cursor Att→Tech            (C1/1128B row dispatch)
f397  $95DB 0→1        A on Tech: SUBMENU opens                 (C1/12E1 STA $95DB=1; list C1/0A88+0AD3)
f402  $9EE3 →01        resolver fills Cyclone's record          (C1/0D4D; $9EE3-9 = 01 12 03 02 FF FF FF)
f410  $9609 0→1 $960D 0→12   A on Cyclone: target-select opens  (C1/1369; $9EE4=0x12 → $960D)
f425  $95DB→0 $9609→0  A on GATO: confirm → queue committed      (C1/1626; $93EE = 01 20 01 06 00 FF 01)
f436  $5E34 8→6  $AD89→0x15   MP −2 spent + damage 21 computed    (C1/CC8F MP; C1/DCED damage)
f438  $9877 0→1  $A013 0→1    freeze on, Cyclone anim starts      (C1/45C2 $9877; C1/40A9 $A013)
f665  $A3D1 0→1        ShowDamage popup born
f723  $A3D1 →0         popup done (58f life)
f751  $A013 1→0  $6130 76→55  span end (313f) + HP applied (−21)  (C1/41BD freeze off; C1/ED1A HP)
```

---

## 1 — THE TECH DATA (Cyclone's identity, all static ROM)

| datum | value | ROM source | note |
|---|---|---|---|
| tech id | **1** | `C2/BD40` per-char range = `01 09 11 19 21 29 31 39 3C` → Crono (char 0) = ids **[1,9)** (8 single techs) | Cyclone = his first |
| name | "Cyclone" | `CC/15C4 + id×11` = `a2 d2 bc c5 c8 c7 be` | `ROM_TECH_NAMES` (menu.c) |
| MP cost | **2** | `CC/253B[1] = 0x02` (table: id1-3=2, id4=4, id5=8, id6=0xA, id7=0xC, id8=0x14) | `ROM_TECH_MP` |
| target group | **0x12** | staged record `$9EE4`; → `$960D` at confirm | Cyclone = area/multi around Crono |
| effect-header | `CC/213F[1]` | `03 00 00 3a 3b 01 3e 80 80 0a 04 00` | +5 type=**1**, +6 defsel=**0x3E**, +9 power=**10** |
| anim script | `CE/0909` | via `CD/45A6`/`CD/5EF0` (§4) | 4 tracks, span 313f |

`CC/213F[1]` (Cyclone) vs `CC/213F[0x3A]` (Crono's WEAPON attack) = `03 00 00 3a 3b 01 3e 80 80 08 04 80` —
**byte-identical except +9 (power 10 vs 8) and +11 (00 vs 80).** The tech is a weapon-power reskin.

The battle STAGES a 7-byte per-tech record into WRAM `$1A80+` at battle entry from the char's learned techs
(`$7E2837` seed drove it — the patch proof). Cyclone's staged record (`$9EE3-9`, resolver `C1/0D4D`):
```
$9EE3 = 01   tech id / dispatch id      (→ queue $93F4 → $AE93 → $9877)
$9EE4 = 12   target group               (→ $960D at confirm)
$9EE5 = 03   flags; BIT7 = greyed/unusable (set when curMP < MP, §2.4)
$9EE6 = 02   MP cost                    (checked vs curMP for the grey bit)
$9EE7 = FF   ⎫ dual/triple partner ids (single tech → FF; BIT7 skips display)
$9EE8 = FF   ⎬  the resolver's 10DDC/10E06/10E30 name-display gates read these
$9EE9 = FF   ⎭
```

---

## 2 — THE MENU FLOW (disasm `C1`, oracle-confirmed)

### 2.1 Command-row dispatch (`C1/1128B`, cursor = `$95DC,X`, X = current seat `$95D5`)
```
1128B LDA $95DC,X / BNE ..    ; row 0 = Att  → JMP C1/129C  (target-select DIRECT, group 7, $9615=0)
11293 DEC A / BNE ..          ; row 1 = Tech → JMP C1/12BC
11299 …                       ; row 2 = Item → JMP C1/12ED
```
The **Att** path (`C1/129C`) opens target-select immediately (`$960D=7`, `INC $9609`), `$95DB` stays 0. Tech/Item
open a submenu first.

### 2.2 Tech submenu OPEN (`C1/12BC`)
```
112BC LDA $A0A7 / AND #01 / BNE →skip   ; gate (some can't-tech flag; 0 here)
112C3 LDA #FF / STA $9EE7               ; target-mode default
112CB STZ $A099 / STZ $A09A
112CE INC $A862 / JSR C1/0A88 / JSR C1/0AD3 / STZ $A862   ; ** build the tech list window **
112DA LDA #01 / STA $9615                ; default target = 1
112DF LDA #01 / STA $95DB                ; ** $95DB = 1 = TECH SUBMENU OPEN **
```
- `C1/0A88` builds the display strings (name column) from a bank-**0x85** pointer (`10A99 LDA #8502 / STA [$64]`)
  keyed by the per-seat base `CC/F38C[seat]` into the menu list `$94D0`.
- `C1/0AD3` builds the per-slot VALIDITY mask `$1CDB,Y` (0x14=20 slots): slot value `$94D0,X` ∈ {FF,FE,FD,FC,FB}
  → `$1CDB=0` (empty/separator), else `$1CDB=1` (a real tech). `$A02A` = the tech COUNT (nav cap).

### 2.3 Navigation (submenu handler `C1/1320`, runs while `$95DB≠0`)
```
11320 LDA $95D5 / TAX ; LDA $95EB,X + $95DF,X → $80   ; selected list slot = scroll base + window cursor
1132E … button edges $EE/$EF dispatch:
   $EE bit7 (A)    → C1/1369  CONFIRM
   $EE bit3 (B)    → C1/138F  CANCEL
   $EF bit3/bit1   → C1/13AD  UP nav
   $EF bit2/bit0   → C1/13F4  DOWN nav
```
- **`$95EB,X` = scroll base, `$95DF,X` = cursor within a 3-ROW window** (the list scrolls when the cursor hits
  the window edge; UP `C1/13AD` decrements + skips `$1CDB==0` empties, DOWN `C1/13F4` increments capped at
  `$A02A,Y` and window size 3). The selected list index = `$95EB,X + $95DF,X`.

### 2.4 The tech RESOLVER + the MP-grey rule (`C1/0D4D`, per frame)
The resolver turns the selected slot into the dispatch record:
```
10D4D X = $95EB,X + $95DF,X + CC/F38C[seat]                 ; the list index
10D62 LDA $9551,X / CMP #FF / BEQ →empty($9EE3-9 = FF)       ; $9551 = per-seat RECORD-index list
10D87 record = $1A80 + CC/F395[seat] (16-bit) + CC/F3A1[slot]; $1A80,X..$1A86,X → $9EE3..$9EE9
```
The **grey rule** (`C1/0F28` loop → `C1/11003`): for each tech, if `curMP ($96) < techMP ($1A83 = $9EE6)` it does
`$1A82 |= 0x80` — i.e. sets **`$9EE5` bit7** (also handles dual/triple 2nd cost `$1A85`). **Oracle-proven:** with
Crono `$7E2607` (curMP) patched to 1 (< Cyclone's 2), `$9EE5` = **0x83** (bit7) vs 0x03 affordable. The confirm
(`C1/1369`) then BLOCKS: `LDA $A099 / BEQ →skip ; LDA $9EE5 / BMI →skip` — a greyed tech cannot be confirmed.

### 2.5 CONFIRM a tech (`C1/1369`) → target-select
```
11369 LDA $A099 / BEQ →skip ; LDA $9EE5 / BMI →skip   ; confirmable gates (MP/grey)
11373 LDA $9EE4 / STA $960D                            ; ** target group = the tech's $9EE4 (0x12) **
11379 JSR C1/1F79 ; INC $9609                          ; open TARGET-SELECT
```
CANCEL (`C1/138F`): `STZ $95DB` (close), restore cursor/base from `$A863,X`/`$A866,X`.

### 2.6 Target confirm → the QUEUE RECORD (`C1/1626`, the SAME confirm handler as Att, `$95DB=1` branch)
```
1626 DEC $9609                                  ; close target-select
1629 LDA $95DB / BNE →11638                      ; $95DB≠0 = submenu; DEC A → Tech branch 1163B
1163B (Tech) $95EB,X→$A866,X ; $95DF,X→$A863,X   ; save the tech-list cursor/base for re-entry
164D  $9EE3→$A0D8,X ; target-mode from $9EE7 ; $84=$9EE3 ; $82=$9EE7 ; $80=0x20
116E6 build queue record $93EE,X:
        $93EF,X = $80|$9F38  = 0x20              ; ** TECH action marker **
        $93F0,X = $9615      (target)
        $93F3,X = $82 = $9EE7 (target mode)
        $93F4,X = $84 = $9EE3 (** tech id **)
1716  STZ $95DB                                  ; close submenu
1719+ dequeue+advance ($A6D9[seat]=FF, $A6DD/$95D5 ← next seat)   ; = battle_confirm_queue.md B.1
```
Observed queue record `$93EE` = `01 20 01 06 00 FF 01` — `$93EF=0x20` (tech), `$93F4=0x01` (Cyclone id). This is
the byte-exact tech analogue of a physical-attack queue record ($80=0x80 att / 0x40 item).

---

## 3 — THE DISPATCH + COST

### 3.1 MP spend (at ANIMATE, `C1/CC86`)
```
1CC86 LDA $5E34 / SEC / SBC $0E / STA $5E34       ; $0E = the MP cost (passed in Y)
      (seat dispatch: CMP #01 → $5EB4 seat1 ; CMP #02 → $5F34 seat2)
```
`$5E34` 8→6 at **f436** (writer `C1/CC8F`), 2 frames BEFORE the freeze (f438). So MP is deducted when the action
**begins executing** (dispatch), NOT at the confirm (f425) — the confirm only queues. (No MP-halving gear active
here; the C2/BC59 halving path is unexercised.)

### 3.2 The anim selector `$9877` (`C1/45A0`, gated `$AE91 < 3` = a PC)
```
145AA LDA $AE93 → X = $AE93×7 ; LDA $CD45A6,X / STA $9877       ; $9877 = CD/45A6[$AE93×7][0]
145EE … $9878 = CD/5EF0[$9877×2]  (14617)                        ; the SCRIPT BASE (bank CE)
```
`$AE93` = the tech id (1, from the queue `$93F4`). `CD/45A6` records (7-byte, per action):
```
$AE93=0: 00 61 00 01 00 00 FF   → $9877=00      (a physical/no-tech config)
$AE93=1: 01 DA EF 20 01 01 FF   → $9877=01      ← CYCLONE
$AE93=2: 02 E8 EC 21 02 02 FF   → $9877=02
```
`CD/5EF0[$9877×2]` script-base table: `$9877=1 → $9878 = 0x0909`. Live cursor confirms bank **CE**
(`$5DBF=0xCE`, main `$5DBD=CE/0917`). **The PC-tech anim path = `CD/45A6` (config) + `CD/5EF0` (base) → bank CE**
— distinct from the enemy `CD/5FF0`(atk)/`CD/61F0`(tech) tables (bank CD).

### 3.3 The SPAN (313f) — does NOT follow the single-hit `windup+58+tail(5/7)` law
Freeze f438→f751 = **313f**. Popup born f665 (`$A3D1`→1), 58f life to f723 (`$A3D1`→0), then **28f** tail to
f751. Decomposition: WINDUP **227f** (Crono's approach mover + the cyclone spin/multi-target sweep) + POPUP **58**
+ TAIL **28**. The 28f tail (vs a single-hit's 5/7) is the multi-target completion — **the tail law
(`battle_span_emergence.md` §5) is single-hit-specific; a multi-target tech has a longer tail.**

### 3.4 Targeting (group 0x12)
`$9EE4 = 0x12` → `$960D` drives target-select (`C1/1F79`). Cyclone hits all enemies in an area around Crono; in
the GATO fight it resolves to GATO (single hit, `$B328`=21, one HP write). **The exact multi-target semantics of
group 0x12 (all-enemies vs area) need a multi-enemy capture to pin — OPEN** (a Cyclone-in-forest state would
show it; single-enemy here can't distinguish).

---

## 4 — THE DAMAGE FORMULA (physical, byte-exact)

The effect-header is staged by `C1/D5EF` (`LDA $CC213F,X / STA $AEE6,Y`, 12 bytes) from `CC/213F + index×12`,
where the index (`$B2C8`=1 for Cyclone) = the tech id for single techs. Fields (oracle `$AEEB`/`$AEEC`/`$AEEF`):
**formula type 1, defsel 0x3E (physical), power 10.** Final damage `$AD89` written by `C1/DCED`.

The pipeline (identical to the PC weapon attack, `battle.c battleComputeDamage`, only the coefficient changes):
```
coef = techPower = CC/213F[techId] +9 = 10          (weapon uses CC/213F[0x3A] +9 = 8)
base = coef × (A36×12 + A43×5) / 18                  ; C1/E0F5
dmg  = base × (256 − def) / 256                      ; def = target +0x3E (physical)
dmg += rand(0 .. A36/3)                              ; variance bonus (C1/E15E)
dmg  = max(dmg, 1)                                   ; universal min-1 floor (C1/DCF0)
```
Measured inputs: Crono A36=`$5E63`=5, A43=`$5E70`=3; GATO def=`$616B`=127. **Predicted = measured:**
```
base = 10×(5×12 + 3×5)/18 = 10×75/18 = 41
dmg  = 41×(256−127)/256   = 41×129/256 = 20.66 → 20
     + bonus rand(0..1)   = 21          → GATO 76→55 (−21) ✓✓✓
```
Cross-check: a NORMAL Crono attack (coef 8) = `8×75/18=33; 33×129/256=16` — matches the pinned "Crono attack
GATO −16". **So Cyclone is the physical pipeline with coef = the tech's power (10). It is NOT the magic path**
(magic would use a magic defsel + the `/102.4` constant). Crit was not observed (dmg 21 undoubled); whether a
multi-target physical tech rolls the crit stream (`C1/ED89`) is a detail (likely no for AoE) — left OPEN.

---

## 5 — THE PRESENTATION (anim script `CE/0909`)

Header `CE/0909` = masks `word1=0x8098 / word2=0x0080` → 4 tracks (A0B3=0, word1): **Y0 main `CE/0917`**, Y8
`CE/094D`, Y11 `CE/0960`, Y12 `CE/0970`. Main track (structurally an attack script):
```
72 0D   SETANIM $8E=0x0D (rear-up pose)      ; the pose comes from E4/E3 like everything (battle_anim_engine.md)
…plays… (03/06 block on pose one-shots)
1E ss   SUPER → CE/D613                        ; the STRIKE + multi-target sweep
… 50    ShowDamage popup ; 01 EndTech
```
The SUPER-subscript `CE/D613` = `[25 ROM bytes at 0xED613: python tools/dis.py 0xED613 25] …`
— the repeated `49 1x` blocks are the per-target strike/sweep; the `97 xx` refs in the effect tracks
(`CE/0960`/`0970`) are the spinning-blade assembly indices. **For an honest first render:** the poses (rear-up +
spin) come from the E3/E4 pose engine at the live foot (the existing engine boundary); the ShowDamage popup +
the single HP-apply already exist. **OPEN (same boundary as the song notes / GATO flurry):** the spinning-blade
EFFECT layer + the `CE/D613` super-subscript decode (the C3/FD-family effect subsystem) — the pixels of the
whirlwind, not derivable from the CD/CE bytes alone.

---

## 6 — IMPLEMENTATION SPEC ✅ LANDED (`src/battle.c` + `src/battle.h`)

> **STATUS (implemented):** the vertical slice is in the engine. `battle.c` adds the Tech submenu state
> machine (`battleTechSubmenu`/`battleTechBuildList`/`battleTechResolve`, shared by step F + the frozen menu
> pass), retires the "A-on-Tech INERT" boundary (`$95DB=1`, list from the RAW `$2837` seed), the resolver
> record `$9EE3-9` + the grey rule (`$9EE5` bit7), the target-group `$960D=$9EE4`, and the PC-tech dispatch
> (`schedTech` FIFO -> MP-at-animate `block+0x07 -= CC/253B[id]`, physical-coef damage `battleComputeTechDamage`,
> `$AE93=id` -> `$9877 = CD/45A6[id×7][0]` -> span pinned `CT_SPAN_PC_TECH_CYCLONE=313`). The target-group 0x12
> + base flags 0x03 are pinned per-tech as the oracle-measured T2 staging datum (`battleTechGroup`/`Flags`),
> NOT decoded from a coincidental table. Six regression checks pinned (`field-battle-tech-cyclone-menu`/`-grey`/
> `-dispatch`, seeds `gato_cyclone.raw.wram` / `gato_cyc_mp1.raw.wram` = `gato_t0` + `$2837|=0x80` [+`$2607=1`],
> gitignored). Suite: quick 70/0/1, full 92/0/3 (was 67/89). **One deviation from §0.5:** our engine's damage
> is **20** (base, byte-exact) not 21 — the +1 is the RNG variance bonus `rand(0..A36/3=1)`, which the oracle's
> specific run drew as 1 and our deterministic run draws as 0 (the SAME RNG-order boundary as the pinned
> physicals, e.g. "Crono→GATO 16/17"). Presentation (the CE/D613 whirlwind) stays the T3 OPEN — Crono idles
> through the pinned span. Everything else (menu bytes, record, `$960D`, MP-at-animate, `$9877`/`$AE93`, span
> 313) is byte-exact vs the oracle ledger.

The engine already has the tech SEAMS (`battle.c`): `isTech` threads through `battleActionSpan`/
`battleComputeTechDamage`/`battleOpenAction`, `ROM_TEC_ANIM_TBL`, and the confirm/dispatch split

The engine already has the tech SEAMS (`battle.c`): `isTech` threads through `battleActionSpan`/
`battleComputeTechDamage`/`battleOpenAction`, `ROM_TEC_ANIM_TBL`, and the confirm/dispatch split
(`battleCommitPcAction`/`battleDispatchScheduled`). What the slice adds:

**(a) Menu — retire the inert A-on-Tech boundary** (`battle.c:2552` "A on rows 1/2 INERT").
- On A@row1 (`$95DC==1`): open the submenu (`$95DB=1`), build the tech list from the SEAT's learned techs.
  Needs the tech-list STAGING: per-seat `$94D0` (display) + `$9551` (record index) + `$1CDB` (validity) +
  `$1A80` records + `$A02A` count, from the `$7E2837` learned seed + the ROM tables. (The FIELD menu already
  computes the learned list — `CtMenuTechList`, `menu.c` — reuse that logic for the seat.)
- Nav a 3-row window (`$95EB` base + `$95DF` cursor, skip empties `$1CDB`); the resolver fills `$9EE3-9` from the
  staged record; grey a tech when `curMP < $9EE6` (`$9EE5` bit7).
- Confirm → target-select with group `$9EE4` (not the fixed 7); the confirm builds the queue record carrying
  `$93EF=0x20` (tech) + `$93F4 = tech id`.

**(b) Dispatch — carry the tech through `battleCommitPcAction` → `battleDispatchScheduled`** (both currently
hardcode `isTech=0`, "Tech/Item are inert"). Push `{atk, tgtSlot, techId}` on the sched FIFO at commit; at
animate: **spend MP** (`curMP -= techMP`, seat block +7), compute tech damage (c), set `$9877 = techId`, span via
the CD/45A6→CD/5EF0→bank-CE map, `$95DB=1` + `$A013=1`.

**(c) Damage — replace `battleComputeTechDamage`'s floor-to-1** (correct only for a type-0 tech like the song)
with the **physical pipeline using the tech's own effect-header power**: `coef = rom[ROM_PC_ATK_REC + techEffIdx×12
+ 9]` where `ROM_PC_ATK_REC = CC/213F` (already defined) and `techEffIdx` = the tech id for single techs (Cyclone
→ `CC/213F[1]` → power 10). Everything else (base, def scaling, bonus, floor) is the existing physical path. Keep
the type-0/power-0 → floor-1 branch for flavor techs (the song). Magic-formula techs stay OPEN.

**(d) Anim** — `$9877 = techId` → `CD/45A6`/`CD/5EF0` → bank-CE script (interim: pin the span 313 for Cyclone,
the `CT_SPAN_*` precedent). Bake `CD/45A6`, `CD/5EF0`, and the CE script + `CE/D613` super for the presentation.

**Tables to bake (like `menu_romdata.h`):** `CC/213F` effect-headers (⭐ already `ROM_PC_ATK_REC`), `CC/253B` MP
(⭐ already baked), the tech records ($1A80 staging source: `CC/F38C`/`CC/F395`/`CC/F3A1` + the learned-tech
seed), the target-group byte, `CD/45A6`/`CD/5EF0` anim map, the CE script bytes.

**Checks to pin (`gato_cyclone.state` = `gato_press_action` + `$2837=80`, btnscript
`100:8,0:372,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:330`):**
1. `--monitor "95DB:1"` → `$95DB`=1 at f397 (A-on-Tech opens submenu, not inert).
2. `--monitor "9EE3:1,9EE4:1,9EE6:1"` → Cyclone record `01 12 02` at f402 (resolver).
3. grey rule: `$2607=01` (curMP 1) → `$9EE5`=0x83 at f402.
4. `--monitor "5E34:2"` → MP 0x0800→0x0600 at f436 (spend 2 at animate).
5. `--monitor "AD89:2,6130:2"` → damage 0x0015 at f436, GATO 0x4C00→0x3700 at f751 (−21).
6. `--monitor "9877:1,A013:1"` → `$9877`=1 at f438, span f438→f751 (313f).

---

## 7 — OPENS / BOUNDARIES

| # | item | status |
|---|---|---|
| T1 | **Target group 0x12 multi-target semantics** (all-enemies vs area-around-Crono) | needs a Cyclone-in-multi-enemy capture (single-GATO can't distinguish); the byte + the single-hit path are pinned |
| T2 | **The tech-list STAGING routine** (which fills `$94D0`/`$9551`/`$1A80` from the learned techs at battle entry) | the `$7E2837` seed drives it (patch-proven); the exact routine address un-located (the bank-0x85 pointer `$8502` in `C1/0A88` is the display source). The FIELD menu's `CtMenuTechList` is the learned-list logic |
| T3 | **The CE super-subscript `CE/D613`** (the strike/multi-hit sweep) + the spin EFFECT layer (`97 xx` assemblies, C3/FD family) | the presentation OPEN — same boundary as the song notes / GATO flurry pixels |
| T4 | **The MAGIC formula** (type-3 `C1/E203`) for a magic tech (Marle's Ice) | ✅ **CLOSED** (§T4.CLOSED): `floor(pow×12×(lvl+mag)/15)`, magic-def `×5/2`, `(256−def)/256`, `+rand(0..mag×77/256)`; 8 datapoints, full unroll, LANDED in `battleComputeTechDamage` (a2). `/102.4` REFUTED. Open sub-item **T4b** = weak/resist elemental multipliers (`C1/E575`, unexercised — GATO neutral) |
| T5 | **Crit for a physical tech** | Cyclone dealt 21 undoubled; whether an AoE tech rolls the crit stream (`C1/ED89`) not disambiguated (likely no) |
| T6 | **The `CD/45A6`/`CD/5EF0` reconciliation vs the enemy `CD/5FF0`/`CD/61F0`** | the PC-action path (gated `$AE91<3`) uses `CD/45A6`+`CD/5EF0`→bank CE; how it relates to the physical-attack `C1/438B`→`CD/40xx` selector (`battle_span_emergence.md` §2.1) is not fully unified |
| T7 | **MP-halving gear** (`C2/BC59`) | not active in this fight; the halved-cost path unexercised |

---

## §T4 — THE MAGIC DAMAGE FORMULA (probe; `/102.4` REFUTED, type-3 derived) — appended from the item-slice session

**Cheap follow-up done: Marle's ICE (magic tech) cast vs GATO.** The tech slice left T4 ("the magic `/102.4`
path") UNEXERCISED because Cyclone is physical. Here it is measured on a magic tech.

**Seed** (`gato_ice.state = gato_press_action.state` + `$2838 |= 0x20`): the learned-tech bitfield is a
per-char byte at **`$2837 + charId`** (Crono `$2837`, **Marle `$2838`**, Lucca `$2839`), MSB = the char's first
single tech. Marle's single techs start at id 9 (`C2/BD40`): Aura(9)=bit7, Provoke(0xA)=bit6, **Ice(0x0B)=bit5
=0x20** → `$2838 |= 0x20` learns Ice. Marle is already in the GATO party, so no roster patch is needed.
btnscript (Crono attacks to free the queue, then Marle casts): `100:8,0:373,100:2,0:12,100:2,0:430,20:2,0:12,
100:2,0:12,100:2,0:12,100:2,0:800`.

**Ice's identity** (`CC/213F[0x0B]` = `03 00 00 00 00 03 3C 70 00 0B 00 00`): **formula type +5 = 0x03 (MAGIC**,
vs Cyclone's type 1 physical), defsel **+6 = 0x3C (MAGIC defense**, vs the physical 0x3E), power **+9 = 0x0B = 11**,
MP `CC/253B[0x0B]` = 2.

**The clean cast (oracle ledger):**
```
f843  $95DB 0→1        Marle Down→Tech, A: her tech submenu opens ($9551 shows Ice; $A02A=1)
f872  $9609→0 $95DB→0  A on Ice + A on GATO: queued
f1037 $AE93 →0B        dispatch: Ice tech id staged                    (C1/BBB1)
f1038 $5EB4 0C→0A / $AD89 →40   MP −2 spent (at animate) + damage 40 COMPUTED (final $AD89=0x28)
f1040 $9877 →0C  $A013 0→1      Ice anim (selector 0x0C) + freeze (span 434f)
f1413 $A3D1 →1/2       popup born (58f life to f1471)
f1474 $A013 1→0  $6130 3B→13    span end + HP applied: GATO 59 → 19 (−40)   (C1/ED1A)
```

**MEASUREMENT: Ice (power 11) by Marle (magic 8, level 1) vs GATO (magic-def 50) = 40 damage.** Inputs
oracle-read: Marle magic `$5EE6`(block+0x39)=8, Marle level `$5EBF`(block+0x12)=1, power `$AEEF`=0x0B=11, GATO
magic-def `$6169`(block+0x3C)=0x32=50; final damage `$AD89`=0x28=40, applied `$6130` 0x3B→0x13 (59→19).

**The `/102.4` hypothesis is REFUTED:** `11 × 8 / 102.4 × (256−50)/256 ≈ 0.69` — off by ~58× from the measured 40.

**The REAL formula — type-3 (`C1/E0F5` dispatch on `$16` = formula type → type 3 at `C1/E203`), oracle+disasm:**
```
1E207 LDA $5E3F,X (= block+0x12 = attacker LEVEL) × 0x0C(12)  → $0A
1E21D LDA $5E66,X (= block+0x39 = attacker MAGIC) × 0x0C(12)  ; $0A += → $0A = 12×(Level+Magic)
1E237 $AEEF(power) × $0A                                       ; = power × 12 × (Level+Magic)
1E250 ÷ 0x0F (15)   (raw bytes A2 0F 00 = LDX #$000F → $2A divisor; JSR C1/C92A)
1E263 JSR C1/C063 (a further MAGIC-based scale)               ; then
1E26D + rand variance (C1/AF22) → $B2BE ; JMP C1/E575 (elemental + magic-def scaling → final $AD89)
```
So the magic base uses the attacker's **LEVEL + MAGIC** (each ×12), × the tech power, ÷ 15 — NOT `curMagic/102.4`.
For our inputs the pre-scale base = `11 × 12 × (1+8) / 15 = 11 × 108 / 15 = 79`. The final 40 (≈ 79 × ½) comes
after `C1/C063` (magic scale) + `C1/E575` (which, for an ELEMENTAL tech — Ice is Water — takes the
`BIT #F080` branch `1E583`: `JSR C1/2CA5` (elemental def; GATO elem-defs = rec[16..19] `04 04 04 04`, block+0x3F..0x42) → `LSR`
(÷2) → add) + the magic-def `(256−mdef)/256` scaling + the +variance. **A clean 2-term candidate that fits the
one datapoint:** `dmg ≈ (Level + Magic) × power / 2 × (256 − Mdef)/256 = 9 × 11 / 2 × 206/256 = 39.8 → 40`.

**Verdict:** `/102.4` is wrong; the CT magic formula is the **(Level+Magic)×power** family (type-3, `C1/E203`,
÷15 then magic/elemental/def scaling), damage computed at DISPATCH and applied at span-end via the same
universal writer `C1/ED1A`. **[SUPERSEDED — now CLOSED below in §T4.CLOSED.]** The `79→40` composition was
pinned by the full `C1/E203`/`C1/C063`/`C1/E575`/`C1/E678` unroll + 8 oracle datapoints; the reduction is NOT
`(256−Mdef)/256` alone (that gives 63) — it is the **magic-def ×5/2 pre-scale** (`50×5/2=125` → `(256−125)/256`).

---

## §T4.CLOSED — THE MAGIC FORMULA, CLOSED (8 datapoints, full disasm) — appended this session

**The closed type-3 (MAGIC) damage formula** — every term cited to the disasm and confirmed predicted==measured:
```
base    = floor( techPower × 12 × (LEVEL + MAGIC) / 15 )      ; C1/E203..E259
base    = base × elementalAffinity                            ; C1/E575 (NEUTRAL target -> ×1)
def_eff = floor( magicDef × 5 / 2 )                           ; C1/E678 (magic defsel 0x3C; physical 0x3E = raw)
dmg     = floor( base × (256 − def_eff) / 256 )               ; top-level def cut (>>8)
dmg    += rand( 0 .. floor(MAGIC × 77 / 256) )                ; C1/C063 range + C1/AF22 (AF22 arg = range+1)
dmg     = max(dmg, 1)                                         ; universal min-1 floor (C1/DCF0)
```
Inputs (block offsets, ATTACKER `$B1F4`=idx, TARGET `$B1F6`=idx): LEVEL = block+0x12 (`$5E3F,X`), MAGIC =
block+0x39 (`$5E66,X`), techPower = effect-header `CC/213F[id]+9` (`$AEEF`), defsel = `CC/213F[id]+6`, magicDef
= target block+0x3C (`$616B`... GATO `$6169`=0x32=50). Damage computed at DISPATCH (`$AD89`), applied span-end.

### The disasm unroll (the arithmetic, byte-exact)
- **`C1/E0F5` dispatch** on `$16` (formula type): `CMP #01 BEQ` type-1 (physical, `E0FF`) / `JMP E184`
  (`CMP #02` type-2 / else **type-3 `E203`**). Type-1 and type-3 both end in **`JMP C1/E575`** (shared finalize).
- **`C1/E203` (type-3 base):** `LDA $5E3F,X ×0C` (level×12, `1E20D`) + `LDA $5E66,X ×0C` (magic×12, `1E223`) →
  `12×(level+magic)` (`1E230 ADC`); `× $AEEF` (power, `1E242`); **`÷ 000F`** (`LDX #000F/JSR C1/C92A`, `1E254`).
  Then `LDA $5E66,X / JSR C1/C063` (`1E260`, magic → variance range `$30`); `LDA $30 / ADC #01 / JSR C1/AF22 /
  STA $B2BE` (`1E266`, the variance); `JMP C1/E575`.
- **`C1/C063` (the MAGIC VARIANCE RANGE, not a base scale):** `XBA` (magic<<8) then a shift-and-subtract chain
  →`$30 = floor(MAGIC × 77 / 256)`. It writes ONLY `$30` (and temp `$08`); **`$2C` (the base) is untouched.**
  Proof: MAGIC=8 → `8×77/256 = 2.40 → 2 = $30`; variance = `rand(0 .. $30+1)`.
- **`C1/E575` (elemental affinity):** reads attacker-block flags `$5E4E,X / $5E53,X` (bit7→×1.5 then JMP E632;
  bit3→×3/2; bit1→×5/4) and `$5E4B,X` (bit6/bit5→×8/10; bit2→÷4), stores `$2C→$16`, RTS. **A NEUTRAL target
  sets none** (oracle `$5ECB/$5ECE/$5ED3 = 0` for Marle-vs-GATO) → **×1**. (`C1/2CA5` is menu/cursor code —
  the §T4 "possibly 2CA5" guess was WRONG; there is no 2CA5 in this chain.) The weak/resist multipliers are
  **UNEXERCISED** (GATO neutral to Water) → the OPEN **T4b** (needs a weak/resistant-target capture).
- **`C1/E678` (magic def resolve):** the def routine `E65F` dispatches on the defsel byte `$18`: `CMP #3E` →
  raw phys def (physical); **`CMP #3C` → magic def `× 5 ÷ 2`** (`LDA $5E2D,X / ×0005 / ÷0002`, `1E68C`). This is
  the whole `79→40` secret: GATO `mdef 50 → def_eff 125`, so `(256−125)/256 = 131/256 = 0.512`, not `206/256`.

### The datapoint ledger (oracle-measured, all predicted==measured after subtracting the measured variance `$B2BE`)
Base state `gato_ice.state`; mid-battle capture `ice_precast.state` (post-stage, Ice queued, pre-compute f27),
one WRAM byte patched per run (`$5EE6` Marle magic / `$5EBF` Marle level), CRC-32 rewritten. Ice power 11,
GATO mdef 50 (all runs), affinity ×1. `base = floor(11×12×(L+M)/15)`; `base_def = floor(base×131/256)`.
```
  L   M   L+M   base   base_def   $B2BE(var)   measured $AD89   ✓
  1   8    9     79      40          0            0x28 = 40      ✓   (the original datapoint)
  1  16   17    149      76          4            0x50 = 80      ✓   (var range floor(16×77/256)=4)
  1   4    5     44      22          0            0x16 = 22      ✓
  1   2    3     26      13          0            0x0D = 13      ✓
  1   0    1      8       4          0            0x04 =  4      ✓
 10   8   18    158      80          0            0x50 = 80      ✓   (level enters IDENTICALLY to magic)
  5   8   13    114      58          0            0x3A = 58      ✓
  0   8    8     70      35          0            0x23 = 35      ✓
```
The ×2.5-magic-def is what discriminates the fit: raw-mdef `(206/256)` would give M=16→120 / M=4→35 (measured
76 / 22). The base is linear in `(L+M)` (magic and level symmetric); the variance depends ONLY on magic (`C063`).

### IMPLEMENTATION ✅ LANDED (`src/battle.c`)
`battleComputeTechDamage` gains regime **(a2)** — the type-3 magic path above — leaving the type-1 physical path
(Cyclone) byte-identical. Per-tech data for Ice (id 0x0B): target group **0x07** / flags 0x00 (measured record
`$9EE3-9 = 0B 07 00 …`), span **434** (`CT_SPAN_PC_TECH_ICE`, oracle freeze f1040→f1474), anim selector auto
`$9877 = CD/45A6[0x0B×7][0] = 0x0C`. Check **`field-battle-tech-ice-dispatch`** (seed `gato_ice.raw.wram` =
`gato_t0 + $2838|=0x20`, gitignored): Marle casts Ice → `dmg=41 techId=11 span=434`, GATO 76→35. Our engine's
**41 = base 40 + variance 1** (Marle readies AFTER GATO's first-turn RNG draw → `$26` advanced → the draw is 1);
the oracle's `gato_ice.state` cast after Crono and drew 0 (dmg 40) — the SAME RNG-order boundary as the
physicals (Cyclone 20/21). **base 40 byte-exact.** OPEN remaining: **T4b** (weak/resist elemental multipliers,
unexercised), **T5** (crit for a magic tech — none rolled).

---

## SCRATCHPAD ARTIFACTS (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-9e84b785-…/scratchpad/`
- `ct_probe.exe` (= build/ct.exe). Base: repo `gato_press_action.state`; injected `gato_cyclone.state`
  (`patch_state.py … 2837=80`), `gato_cyc_lowmp.state` (`2607=01`, the grey test); `gato_ice.state`
  (`2838|=0x20`, Marle learns Ice — the §T4 magic probe).
- `dis_c1_0A00.txt` / `dis_c1_1000.txt` / `dis_c1_2000.txt` — bank-C1 menu/confirm/resolver disasm dumps.
- `tech_window.raw` (+.wram/.vram/.oam) — a `--peek` of the tech list open (Crono/Cyclone).
- Disasm anchors: command dispatch `0x1128B`; Tech-open `0x112BC`; nav `0x11320`/`13AD`/`13F4`; confirm-tech
  `0x11369`; resolver `0x10D4D`; grey pass `0x10F28`+`0x11003`; queue-build `0x11626`+`0x116E6`; MP spend
  `0x1CC86`; `$9877` `0x145A0`; effect-header `0x1D5CD`/`0x1D5EF`; damage `0x1DCED`.
- ROM tables: `CC/15C4` names, `CC/253B` MP, `C2/BD40` range, `CC/213F` effect-headers, `CC/1BF0` eff-index,
  `CD/45A6` PC-anim config, `CD/5EF0` PC-anim script base, `CC/F38C`/`CC/F395`/`CC/F3A1` list maps, `CE/0909`
  Cyclone script.

---

# ADDENDUM (session 24) — `$95DB` IS **NOT** AN ACTION GATE (the "menu doesn't close" defect)

User report (translated from Portuguese): *"after selecting the cyclone tech and selecting the enemy, the menu for the character crono should
'close'."* The STATE was already right — `battleCommitPcAction` clears `$9609`, clears `$95DB`, dequeues the
PC and advances `$95D5` on the confirm frame (trace `f236 pc-commit-tech cur=-1 9609=0 95DB=0 q[0..0]`). The
defect was the RENDER, caused by an engine-internal collision.

**The collision.** `battle_loop.md` S2 had the engine write `$95DB = 1` at every action dispatch as a
repurposed "executing" gate, back when the Tech/Item submenus were inert boundaries (`§B`). Session 22/23
made those submenus real *and rendered*, and the HUD gates its submenu panel on
`$9609 == 0 && $95DB in {1,2}` (`src/field.c`). So the instant the confirmed action dispatched, `$95DB`
went 1 again and the strip redrew the **acting** PC's Tech panel — for the whole 313-frame Cyclone span.

**ORACLE (the same Cyclone cast, `--monitor "95DB:1,9609:1,A013:1,95D5:1,A6D9:3" --diff`):**
```
f397  $95DB=01                                   Tech submenu opens
f410  $95DB=01  $9609=01                         target-select opens (submenu still 1)
f425  $95DB=00  $9609=00  $95D5=FF  $A6D9=FFFFFF  ** the CONFIRM closes submenu + target-select AND
                                                     dequeues the PC (no chooser left in that capture) **
f438  $95DB=00  $A013=01                         the action span BEGINS -- $95DB stays 0 ...
f751  $95DB=00  $A013=00                         ... flat 0 for the whole span
```
So `$95DB` is purely the command SUB-MODE (0 command level / 1 Tech / 2 Item) and the action gate is
**`$A013` alone** (C1/40A6) — which the engine already models separately.

**SHIPPED:** the three `s[0x95DB] = 1` dispatch writes (`battleArrive`, `battleOpenAction`,
`battleDispatchScheduled`) and the matching span-end clear are deleted; `$A013` carries the freeze on its
own. The Wait-mode ATB gate `$99E0 = ($95DB in {1,2}) ? $99E1 : 0` now means what
`battle_atbmode.md` says it means (a submenu is open) instead of also firing on every action window.

**Re-pins:** 9 checks whose pinned trace text contained `95DB=1` for an action-window event
(`pc-act-tech` / `pc-act-item` / `enemy-arrive` / the confirm-boundary + cursor-cadence lines). **Text only**
— identical frames, spans, damage, HP and ids; verified line-by-line before pinning. Renders confirmed: at
f218 (submenu open, pre-target-select) the tech panel still draws; at f260/f500 (post-confirm, mid-span) the
strip shows the next chooser's command boxes and Crono's panel is gone.
