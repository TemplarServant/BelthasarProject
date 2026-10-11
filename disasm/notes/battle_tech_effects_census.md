# PC TECH EFFECTS 01-74 -- the per-tech effect table, the executor, and the port's gap list

Derived from the ROM disassembly (`tools/dis.py <fo> <len> 1 0`, fo = `(bank-0xC0)<<16|addr`; the executor runs
m=8-bit x=16-bit) and the logical oracle (`ct_techfx.exe` = a copy of `build/ct.exe`, `CT_ORACLE_LOGICAL=1
CT_ORACLE_VIRTUAL_BEAM=1`, `--monitor ... --diff --writer`; writer PC = the instruction after the store).
Builds on battle_tech_slice.md, battle_multitarget.md, battle_status_pipeline.md, battle_pc_damage_formulas.md,
battle_tech_damage_type6.md, battle_items_nonhp.md -- nothing there is redone. The per-tech table (all 116 techs,
every header byte decoded) is `techfx_table.txt` beside this file.

---

## 0. Headline results

1. **A tech is up to three EFFECTS, and each effect's header byte +0 is its KIND** -- `C1/D6FA LDA $AEE6 / ASL /
   TAX / JSR ($D9D7,X)`. Table C1/D9D7: **0 = HEAL `D221`, 1 = CURE/REVIVE `D23E`, 2 = STATUS `D267`, 3 = DAMAGE
   `D2DE`, 4 = none `D370`(RTS), 5 = `D371`, 6 = CHARM `D3BF`, 7 = `D3F4`, 8 = MULTI-HIT `D431`**, 9+ = `D48F` (RTS).
   PC techs use kinds 0,1,2,3,6,8. The port routes EVERY tech through the damage formula
   (`battleComputeTechDamage`), so every kind-0/1/2/6 tech deals **1 damage to an enemy**: verified on the port --
   Marle's Aura: `techact: seat=1 tech=09 tgt=6 n=1 dmg=1`, GATO 76->75; the cartridge heals Marle +46 (sec. 8).
2. **A bit7 effect id is SKIPPED, not a terminator** (`C1/D5D4 BIT #80 / JMP C1/D71E`: `INC $B2C7`, `JMP D5B9`
   while < 3). Its low 7 bits are only that participant's MP index. The port's `battleTechDamage` does
   `if (eff & 0x80) break;` -> 8 combos whose FIRST effect is a skip compute nothing (3F 40 47 5A 60 64 6A 73).
3. **Physical techs roll to-hit.** Kind 3 runs `C1/DC64` unless the effective `$AE4D` bit5 is set (`D306-D316`);
   11 physical single-tech headers have `+7 = 0x80` (bit5 clear; Slurp Cut / Dino Tail carry A0), and Napalm /
   Mega Bomb carry 0x50 -- 13 techs roll (sec. 9 #3). Oracle: Cyclone's dispatch draws TWO `$26`
   values (31->33) and `--hookpc C1DC64` is visited once in that frame. The port's tech path draws only the bonus
   -- **this is the real cause of the "Cyclone 20 vs 21 RNG-order boundary"** in battle_tech_slice.md sec. 6.
4. **Multi-victim order is REVERSED**: `C1/E9B8` takes the current target from `$AD8E[$AD8D-1]` and the loop
   `DEC $AD8D`s, so the primary (`$AD8E[0]`) is processed LAST. Oracle, forest Cyclone (`$AD8E = 03 04`): the
   dispatch frame's last `$B1FD` writer is `C1:E9C4` with value **03**. The port iterates victims forward.
5. **The kinds' laws, oracle-confirmed on the synthetic GATO recipe (sec. 8):** heal = `MAGIC x (hdr+1 & 1F)`
   (+`rand(0..MAGIC-1)` only when the product mod 50 != 0; Aura 46 = 40+6, Life 50 = 5x10 with NO draw); Life
   un-deads at dispatch (`+0x1D 80->00`) and heals 50 at the span end; Haste sets `+0x20 |= 80`, arms `$AF48/$AFD7`,
   and halves the next reload (`$AFAB = 0x33 = 0x66/2`, `C1:BE2A`); Provoke rolls (`acc 80`) and, on GATO (immune
   `+0x23 = FF`), writes only `$40 = 80` (`C1:DC63`); Protect sets `+0x21 |= 04` and then **GATO's song and
   attack do 0** (`$AD89 = 0`, `C1:DD43`, `floor(d/10)*7`); Confuse = 4 hits x (1/4) = 60 with 5 draws; Rollo
   Kick = formula type 5 = 243 byte-exact; Charm steals (`$AE60 80->00`, `$B3BF = 1`); Lightning's element
   multiplier `x4/res` = 29/58/14/**0**/absorb for res 04/02/08/00/84.
6. **Only 21 of 116 techs are core-PORTED** (damage kinds with formula types 1/3/6, groups 07/08/12, routines
   2/10/11, and not needing a hit roll). 60 dual/triple techs have no menu path at all (they run only in the demo).
   Ranked gap list: sec. 9.

---

## 1. The tech record `CC/1BEB[id*11]` (11 bytes) -- field by field

| byte | meaning | reading code |
|---|---|---|
| +0 | bit7 = magic tech (learn gate); bits0-6 = participant GROUP n -> `CC/249F[n*3..+2]` = up to 3 char ids -> `$B2EA/$B2EB/$B2EC` | `C1/F482` (learn), `C1/D546 AND #7F` -> `D55E-D570` |
| +1 | bit6 -> `$B2CA |= 20` -> `$AE4D |= 20` (no hit roll); bit5 -> `$B2CA |= 10` -> `$AE4D |= 10` (skip the `DE87` gate); bits0-2 -> `$1A` -> `JSR C1/EB68` after all effects (sum the effects' numbers per enemy target, cap 9999); bit7: no long-absolute reader in the ROM | `C1/D593-D5B0`, `C1/D629 ORA $B2CA`, `C1/D732-D73F` |
| +2 | always 0x40; `ORA $B18B` -> `$B18E` (per-target "last hit by" record, `C1/D508` -> `$B192+tgt*4`) | `C1/D694` |
| +3 | ELEMENT `$B190` (bit7/6/5/4 -> resistance byte `+0x3F/+0x40/+0x41/+0x42`; 04/02 are not tested by it) | `C1/D6A4`, consumer `C1/EF3A` only |
| +4 | always 0; no long-absolute reader | -- |
| +5..+7 | effect k's header index into `CC/213F` (k = participant k); bit7 = SKIP (MP only) | `C1/D5CD`, `C1/D5D4` |
| +8..+10 | effect k's extra-effect selector `$B2CC` -> `CC/2A05[$B2CC*3]` | `C1/D61F` |

The target group and cursor flags are a separate table, `CC/1ACB[id*2]` = group (`C1/C9C9` -> `$9EE4` ->
`$960D`), `CC/1ACB[id*2+1] & 3F` -> `$9EE5` (`C1/C9BC`; bit7 of `$9EE5` = grey). MP: participant k pays
`CC/253B[(+5+k) & 7F]` (`C1/CB48/CB61/CB7A`: `LDA $CC1BF0+k,X / AND #7F / LDA $CC253B,X`), so CC/253B is indexed
by EFFECT index and continues past 0x38 (e.g. `[3C] = 12`, `[40] = 2`). Anim: `$9877 = CD/45A6[id*7][0]`.

**The effect walk** (`C1/D523-D74E`, one call per tech action):
```
D55B  $B2EA..EC = CC/249F[group*3..]        (participants)
D5B9  loop k = $B2C7 = 0..2:
D5CD    id = CC/1BF0[$B2C3+k]; bit7 -> D71E (skip: k++, continue)
D5EF    copy CC/213F[id*12] -> $AEE6..$AEF1 (12 bytes)
D601    seat = $B1BE[ $B2EA[k] ]; FF -> JMP D742 (abort ALL remaining effects)
D61F    $B2CC = CC/1BF3[$B2C3+k] ; D626 JSR EB0E ($AE4D = hdr+7, $AE4E = hdr+8) ; D629 $AE4D |= $B2CA
D632    $B18B = seat  (the ATTACKER of this effect is participant k, C1/E9A3)
D6FA    JSR ($D9D7 + 2*hdr[0])  (the kind)
D70D    if ($7E:B1F5 + $B2C3) bit7 AND $B200 bit7 -> D742 (abort)   -- operand OPEN, sec. 10
D72E  +1 & 7 -> $1A ; JSR C1/EB68 if nonzero
```
The target list `$AD8E/$AD8D` is built once (`C1/C600`) and restored before each effect (`D5B3-D5BD` via `$B2ED`),
so every effect hits the same set. Each effect's HP delta goes to its own slot `$B328 + tgt*4 + $B2C7*$2C`
(battle_tech_damage_type6.md sec. 2); the span-end apply sums the slots.

---

## 2. The effect header `CC/213F[i*12]` -> `$AEE6..$AEF1`

Staged by `C1/D5EF`; `$AE4D/$AE4E` mirrored by `C1/EB0E`. Byte meaning depends on byte +0 (the kind):

| byte | WRAM | kind 0 HEAL / 1 CURE | kind 2 STATUS | kind 3 DAMAGE / 8 MULTI | kind 6 CHARM | loader |
|---|---|---|---|---|---|---|
| +0 | `$AEE6` | kind | kind | kind | kind | `C1/D6FA` |
| +1 | `$AEE7` | k0: power `&1F` (0F = FULL); k1: bit7 revive, bit6 heal, `&1F` power | category (DBAA) | rider accuracy (k3) / HIT COUNT (k8) | chance (x3 if attacker `+0x2A == A5`) | `D132` `D14F` `D431` |
| +2 | `$AEE8` | resource: 80 HP, 40 MP (`$1A`) | mask | rider mask | -- | `D132` `D14F` |
| +3 | `$AEE9` | -- | -- | attacker hit-stat offset (3A) | -- | `D174` |
| +4 | `$AEEA` | -- | -- | target evade-stat offset (3B) | -- | `D174` |
| +5 | `$AEEB` | -- | base accuracy | FORMULA TYPE (E0F5) | -- | `D1A4` |
| +6 | `$AEEC` | -- | accuracy divisor (of MAGIC) | defence selector (E65F) | -- | `D1A4` |
| +7 | `$AE4D` | -- | bit5 no roll | bit7 phys (Magic Wall skips), bit6 magic (Protect skips), bit5 no hit roll, bit4 skip DE87, bit2 rider, bit1 charm | -- | `EB0E` |
| +8 | `$AE4E` | -- | -- | bit7 weapon extra effect (`EA7D`); bits0-1 defence x n/4 (`E741`) | -- | `EB0E` |
| +9 | `$AEEF` | -- | -- | POWER | -- | formula |
| +10 | `$AEF0` | -- | -- | class mask vs ENEMY `+0x19` (`D160`/`DE87`: AND != 0 -> "miss") | -- | `D160` |
| +11 | `$AEF1` | -- | miss-stage `$1A` | miss-stage `$1A` | miss-stage | `D1B5` |

Values met in PC techs: `+7` = 80 (physical), 70 (magic/buff), 50 (Napalm, Mega Bomb, Provoke, Hypno, Boogie),
60 (Frog Squash), A0 (Life, Slurp Cut, Dino Tail, Kiss...), B0 (hdr 42). `+8` = 80 or 00. `+10` = 04, 00, 24, 46.

---

## 3. The kinds (C1/D9D7), law + oracle + port

### Kind 0 HEAL -- `D221: loop { E9A3; E9B8; D132; DA37; D508 } per target` (12 techs)
`D132`: `$16 = attacker MAGIC (+0x39, $5E66,X)`, `$18 = hdr+1 & 1F`, `$1A = hdr+2`. `DA37` (HP, `$1A` bit7):
```
amount = ($18 == 0F) ? target maxHP (+5)             DA40-DA64
       : $16 * $18                                   DA46-DA53 (C1/C90B)
if (amount % 50) != 0: amount += rand(0 .. $16-1)    DA66-DA87 (C1/C92A remainder $32; AF22 base 0 count $16)
PC target: amount = min(amount, 999)                  DA96-DAA7
$AD89 = amount; $B202 = 80 (heal); JSR EBF8 (negate + stage); display kind 1   DAAC-DACC
```
(MP part `$1A` bit6, `DAD4-DB54`: `10 * $18`, FULL = maxMP -- no PC tech uses it.) HP lands at the span end
(`C1:ED1A`), clamped to maxHP. **Port: NOT PORTED** (techs); the item path `battleComputeItemEffect` has
base*mult but neither the mod-50 variance draw nor the 999 cap.

### Kind 1 CURE / REVIVE -- `D23E: D14F; DB5E; if $16 bit6 { D132; DA37 }; D508` (5 techs)
`$16 = hdr+1`. `C1/DB5E`: bit7 -> `+0x1D &= 7F` (un-dead); always `+0x1D &= C0`, `+0x1E = 0`, `+0x1F &= C2`, and
for a PC the timers `$AFB6/$B02F/$AFC1/$AFCC/$B00E` = 0 (battle_items_nonhp.md sec. 3). Then the kind-0 heal with
power `hdr+1 & 1F`. Life `CA` (revive + heal x10), Life 2 `CF` (revive + FULL), Kiss `4E` (cure + heal x14), hdr
41 `4F` (cure + FULL), hdr 44 `5C` (cure + heal x28). **Port: NOT PORTED** for techs (the item Revive/Heal
apply, `battleApplyItemEffect`, has the same masks at the span end -- the ROM does the cure at dispatch).

### Kind 2 STATUS -- `D267` (7 techs)
```
D26D  if !($AE4D & 20):  acc = MAGIC / hdr+6 (C92A; divisor 0 -> 0) + hdr+5 ; roll = rand(0..99) (AF22 #64)
D2A1      roll > acc -> MISS (D1B5 $1A=hdr+11; E8E2; $B200 = 80)
D2B4  D14F ($16 = hdr+1 category, $18 = hdr+2 mask) ; JSR C1/DBAA
D2BA  $16 == 80 -> MISS else $B200 = 0            (see sec. 10: DBAA never sets $16 = 80)
```
`C1/DBAA` by category: **0/1/2** -> immune if `block[0x22+cat] & mask` (cat 1: `$40 = 80`, `$16` stays 1; else
`$16 = 0`), else `block[0x1D+cat] |= mask`, `JSR E006`, then if `+0x1E & 20` and `+0x25` bit7 clear -> `+0x20 &= 7F`,
else `+0x1E &= DF`; **3** -> mask bit7 clears `+0x1E` bit5 first, then `+0x20 |= mask`, `E006`; **4** -> `+0x21 |=
mask`, `E006`; **5** -> `+0x22 |= mask` (no E006). `E006` arms: cat 1 bits 80/40/20/10 (`$AF27/$AF32/$AF3D/$AF7F`),
cat 3 bit80 `$AF48=$B0F5` / bit40 `$AF53`, cat 4 bit40 `$AF5E=$B10B` / bit20 `$AF8A` (the cat-2 arm `E070` jumps
straight out). PC uses: Provoke/Hypno/Boogie cat 1 (masks 04/02/80), Haste cat 3 `80`, Protect cat 4 `04`, Magic
Wall cat 4 `40`, Lifeline cat 5 `80`. The DAMAGE side reads `+0x21|+0x26`: bit2 (Protect) or bit6 (Magic Wall) ->
`floor(dmg/10)*7` (sec. 4). **Port: NOT PORTED** (only E976's cat-1 rider exists, `battleStatusRider`).

### Kind 3 DAMAGE -- `D2DE`, per target (88 techs have >= 1 such effect): sec. 4.

### Kind 6 CHARM -- `D3BF: D14F; C1/E9D5` (2 techs)
`E9D5`: chance `$16 = hdr+1` (x3 if attacker `+0x2A == A5`), `rand(0..99) <= $16` -> `JSR EB1C` with the tech's
`$B2CC` = routine 12 `EFC3`: `$1A = rand(0..99) < $1C` (Charm `$1C=50`, Twin Charm `64`); `$1A` and target `$AE5A`
bit7 -> clear it, grant `CC/5E05[id*7]` (charm item); else bit6 -> grant `CC/5E04` (drop); else `$16 = FF`
(`EA02-EA6E`). **Port: forced-only** (`battleCharmRoll` under `CT_BATTLE_CHARMTEST`), routine 12 not ported.

### Kind 8 MULTI-HIT -- `D431` (2 techs: Confuse 4 hits, Triple Kick 3)
```
D431  $B2D0 = hdr+1 (hit count) ; $B200 = 80
D44A  loop: if $B200 == 0 (previous hit landed) -> $AE4D = 20   (no roll, and every other $AE4D bit cleared)
D456      restore $AD8D ; save/restore $B2CC ; JSR D2DE (the kind-3 body, all targets) ; INC $AD9B
D478  $AD89 = $AE7B (the running sum DCAD keeps at DE7A) ; EBF8 to $B1FD ; D508
```
Each hit has its own variance draw and `$B2CC` scale (Confuse `14` = x1/4, Triple Kick `13` = x1/3).
**Port: NOT PORTED** -- one hit is computed (with the x1/4 scale), i.e. Confuse deals a quarter.

---

## 4. The damage kind `D2DE` (per target, reverse list order) -- what runs and what the port has

```
E9A3/E9B8  attacker = $B18B, target = $AD8E[$AD8D-1]            (port: forward order -- WRONG)
D2E4  if !($AE4D & 10): D160 ($16=hdr+10, $18=target +0x19) ; DE87: enemy && ($16 & $18) -> MISS ($1C=1)
D306  if !($AE4D & 20): D174 (hit/evade from +3/+4) ; DE9E ; DEF0 ; DC64 hit roll -> MISS
D32C  D1A4 ($16 = type, $18 = defsel) ; E0F5 formula ; E65F defence ; DCAD finisher
D338  $AE4D & 04 -> D14F + E976 rider ; D345 $AE4D & 02 -> E9D5 charm
```
**Formula ladder `C1/E0F5` on hdr+5** (status note S8.3): 1 `E0FF`, 2 `E188`, 3 `E203`, 4 `E27A`, 5 `E2DA`, 6
`E36D`, 7 `E3D1`, 8 `E4B7`, else `E42F`. PC tech headers use **1, 3, 5, 6** only (plus 0 in non-damage kinds).

| type | formula (bonus) | PC techs | port (`battleComputeTechDamage`) |
|---|---|---|---|
| 1 | `pow*(+36*12 + +43*5)/18` (+rand 0..+36/3) | 28 | PORTED (no hit roll, sec. 0.3) |
| 3 | `pow*12*(+12 + +39)/15` (+rand 0..+39*77>>8) | 61 | PORTED |
| 5 | `pow*(+36*16 + +12^2/5)/20` (+rand 0..+12/3) | 22 | **NOT PORTED** (exists only in the PC basic-attack path) -> 1 dmg |
| 6 | `+37*pow*(maxHP-curHP)/140` (+rand C1B2) | 4 | PORTED (measured, attract 07D) |

**`C1/E65F` defence** (`1E65F-1E76D`): defsel 3E -> target `+0x3E`; 3C -> `+0x3C*5/2`; else the literal. Then
target `+0x1E & 82` -> def x16/20; else `& 40` x18/20, `& 05` x18/20, `& 20` x18/20 (sequential); then `$AE4E &
3` -> def x n/4; then **clamp: def >= FF (or negative) -> FF** (`E75F-E76B`). Port `battleDefenceE65F`/the tech
paths: defsel only -- no status scaling, no `$AE4E&3`, **no FF clamp**: a 3C target with mdef >= 103 gives
`def_eff > 256`, a negative cut in the port where the ROM divides by 256 -- a divergence, not a boundary.

**`C1/DCAD` finisher and its TAIL** (`1DCAD-1DE86`):
```
DCB1  target +0x1E &= F9 ; $AD89 = base*(0x100-def)/0x100 + ($B2BE & FF) ; 0 -> 1        [port: battleDcad]
DCFD  JSR EA6F: $B2CC != 0 -> EB1C(CC/2A05[$B2CC]) ; $AE4E bit7 -> weapon effect:
        $B2CC = attacker +0x46, if attacker +0x47 bit0 -> EB1C                          [port: $B2CC yes, weapon NO]
DD0E  $0E = target (+0x21 | +0x26); $B1FC bit0 -> skip
DD18  !($AE4D & 40) && ($0E & 04)  -> dmg = floor(dmg/10)*7        (Protect)           [NOT PORTED]
DD45  else !($AE4D & 80) && ($0E & 40) -> dmg = floor(dmg/10)*7    (Magic Wall)        [NOT PORTED]
DD78  dmg = min(dmg, 9999)                                                             [NOT PORTED]
DD83  $B1FC bit6 (set by EF39 absorb) -> stage as a HEAL of dmg, clear bit6             [NOT PORTED]
DDBF  curHP == 0 -> nothing ; dmg >= curHP -> death staging ; else stage ; $AE7B += dmg
```
The `+0x1E &= F9` wake, the rider and the charm bit are already ported for enemies/physicals (status note S8).

---

## 5. Extra-effect routines (`CC/2A05[$B2CC*3]` = routine, `$1C`, `$1E`; table `C1/FA61`)

| routine | entry | effect | PC-tech selectors (techs) | port |
|---|---|---|---|---|
| 2 | `EDBA` | `$AD89 = $AD89*$1C/$1E` (32-bit product) | 03 x5/4, 04 x6/4, 05 x7/4, 06 x8/4, 13 x1/3, 14 x1/4, 34 x4/1 (15 techs) | PORTED (product truncated to 16 bits -- differs only if `dmg*$1C >= 65536`) |
| 10 | `EF39` | element: k from `$B190`; r = target `+0x3F+k`; r bit7 -> `$B1FC |= 40` (absorb); div = r&3F, r bit6 -> div-2; `dmg*4/div` (div 0 -> **0**, C92A) | 07 (32 techs) | PORTED, but **div 0 returns dmg unchanged (ROM: 0)** and the absorb flag has no consumer |
| 11 | `EFBC` | routine 2 then routine 10 | 08 x5/4, 09 x6/4, 0B x8/4 (28 techs) | PORTED (same two caveats) |
| 8 | `EEF7` | `rand(0..99) <= $1C` -> DBAA cat 1 mask `$1E` | 2E (Poyozo Dance, 80% `+0x1E |= 04`) | NOT PORTED |
| 12 | `EFC3` | `$1A = rand(0..99) < $1C` (charm-vs-drop pick) | 0F, 10 (Charm, Twin Charm) | NOT PORTED |
| 18 | `F132` | target `+0x19` bit7 clear and `rand(0..99) <= $1C` -> curHP = 0; else `$AD89 = 0` and the target leaves `$AE95/$AE99` | 24 (Black Hole, `$1C = 28`) | NOT PORTED |
| 0 | `ED89` | crit: `rand(0..99)` on `$B3E6` <= attacker `+0x45` -> x`$1E` | none -- **no PC tech crits** (closes tech-slice T5) unless its weapon effect selects it | -- |

Weapon effects reachable through `$AE4E` bit7 (25 techs, the physical ones): 14 weapons have `+4` bit0 set
(Demon Edge `0C`, Shiva Edge `2F`, SonicArrow `27`, Siren `26`, Plasma Gun `28`, Shock Wave `29`, WonderShot `02`,
Graedus `18`, Crisis Arm `19`, Pearl Edge `0C`, Demon Hit `0D`, Iron Fist `2E`, Bronze Fist `38`, Doom Sickle `16`)
-- weapon record `CC/0262+id*5` +3 -> block `+0x46`, +4 -> `+0x47`.

---

## 6. Target groups used by PC techs (`C1/1F79` -> `C1/1FF8[grp & 7F]`)

Bit7 of the group (`C1/1F9D`) switches the cursor to the PC side (`$A86B`, `C1/1C3A`); the handler builds `$99C0`.
Common gates in every scan: `$96F5,X != 0`, `$9FF7,X` bit7 clear, `$A0A8,X == 0`, `$A09B,X == 0`.

| grp | handler | set | techs | port (`battleBuildHitSet` + target-select) |
|---|---|---|---|---|
| 07 | `20A9` | one enemy (cursor, `$960A`) | 42 | PORTED |
| 08 | `20B6` | all enemies (`$960C = 80`) | 30 | PORTED |
| 12 | `225F` | primary + enemies with d^2 <= 09 around the PRIMARY (`2701`) | 12 | PORTED |
| 80 / 00 | `203A` | one alive PC, caster first (`$99C0 = $960F`) | 7 / 3 | **NOT PORTED** (port aims at an enemy) |
| 81 / 01 | `209A` | all alive PCs | 8 / 1 | **NOT PORTED** |
| 03 | `20E0` | one DEAD PC (`+0x1D` bit7 set, present) | 2 | **NOT PORTED** |
| 13 | `22A4` | d^2 <= 10 around ROBO's seat (`$2980 == 3`) (0x14: 19) | 3 | **NOT PORTED** |
| 11 | `224B` | d^2 <= 10 around the CASTER `$960F` | 2 | **NOT PORTED** |
| 0F | `22F5` -> `2332` | enemies with `|Y - Yprim| <= 0x20` (Y = `$1D23`) | 2 | **NOT PORTED** |
| 1B | `22D3` | d^2 <= 19 around MAGUS's seat (`$2980 == 6`) | 1 | **NOT PORTED** |
| 0B / 0C / 0D | `2169` / `2203` / `21AF` -> `25A3` | beam from the caster (0D: Robo's seat) toward the primary, half-width `$9607*8 = 16` px via `C1/0222` (angle) + `C1/01F9` (sine table `C0/F900`); `$9608` = 0 (0B, 0D) / 1 (0C) selects the far edge | 1 / 1 / 1 | **NOT PORTED** |

The geometric shapes other than 0x12 are disasm-read only; no single-enemy fixture distinguishes them.

---

## 7. MP and the dual/triple composition

- **Single**: `CC/253B[id]` at animate (`C1/CC8F`); oracle: Cyclone 99->97, Aura 99->98, Provoke ->98,
  Protect ->93, Confuse ->87, Life ->89, Rollo ->97, Charm ->95 (all = `CC/253B[id]`; Haste's MP not monitored). PORTED.
- **Combo**: participant k pays `CC/253B[(+5+k)&7F]` including SKIP entries (Aura Whirl: Marle 1 + Crono 2),
  halved by an A2/A3 accessory (`C1/CBF6`). PORTED (`battleTechSpendMp`, measured on the demo's X-Strike).
- **Damage composition**: NO stat pooling. Each effect is computed with ITS participant as the attacker
  (`$B18B` = participant k's seat, `C1/D632`), against each victim, then scaled by its own `$B2CC` routine;
  the deltas go to separate slots and are summed at the span end, the numbers summed by `C1/EB68`. Already
  measured by the port's own dossiers: X-Strike 271 = 123 + 148 (x5/4 each), Ice Sword 443 = 242 + 201.
- **Menu**: the battle tech list is built from `$2837+char` (singles only, `battleTechBuildList`); the combo
  list, the "all participants ready" gate and the dual/triple MP-grey (`$1A85`) are NOT PORTED -- the 60 combos
  are reachable only through the demo's scripted command.

---

## 8. Oracle confirmations (recipe: `saves/battle_re/gato_wait.state`, `PEEK_POKE="2980=<c>,2981=80,2982=80,
`<2837+c>=FF,<2830+c>=08,` + sheet MP 99; input `100:8,0:632,20:2,0:10,100:2,0:10,(20:2,0:10)xk,100:2,0:10,100:2,
0:700`; GATO = cmb 6, block `$612D`, HP `$6130`; raw logs in `techfx/*.txt`)

| tech (c,k) | dispatch | what the cartridge did | law confirmed |
|---|---|---|---|
| Cyclone (0,0) | f682 | hdr `03 00 00 3A 3B 01 3E 80 80 0A 04 00`, `$AE4D=80` (C1:D632); `$26` 31->33; `--hookpc C1DC64` 1 visit; `$AD89=15`; GATO 76->55 f989 | type 1 + **hit roll** (2 draws) |
| Aura (1,0), Marle HP 10 | f682 | hdr `00 05 80`, target `$AD8E=00` (self), `$26` +1, `$AD89=FFD2` (-46, C1:EC0F), HP 1->47 f991 (C1:ED1A), MAGIC 8 | heal = 8*5 + rand(0..7) |
| Life (0,5), Marle dead (`2653=00,2654=00,266D=80`) | f745 | `$99C0=01` (C1:210C); hdr `01 CA 80..A0`; `+0x1D 80->00` at dispatch; `$AD89=FFCE` (-50), `$26` unchanged; HP 0->50 f1214 | revive + heal 5*10, **no draw when mod 50 = 0** |
| Haste (1,4) | f730 | `+0x20=80` (C1:DC3C), `$AFD7=1` (C1:E0B7), `$AF48=6E`; no draw; next reload `$AFAB=33` (C1:BE2A); `$AF48` ticks 6E,6D.. (C1:826B) | DBAA cat 3 + E006 arm + BD6F halving |
| Provoke (1,1) | f694 | hdr `02 01 04 00 00 50 00 50`, `$26` +1 (acc roll), `$40=80` (C1:DC63), GATO `+0x1E` stays 00 (`+0x23=FF`), `$B200=00` | kind 2 roll; immune -> no status, no MISS |
| Protect (2,4) | f730 | Lucca `+0x21=04` (C1:DC3C); GATO song f1236 `$AD89=0000` (C1:DD43), GATO attack f1762 `$AD89=0000` (C1:DD43), Lucca HP 66 unchanged | DCAD tail `floor(d/10)*7` |
| Confuse (0,6) | f755-756 | hdr `08 04 00 3A 3B 01 3E 80 80 1D 04 00`, `$B2CC=14`, `$26` 31->36 (5 draws), `$AE4D -> 20` (C1:D456), `$AE7B=3C`; GATO 76->16 | 4 hits x (60/4); roll on hit 1 only |
| Rollo Kick (5,1) | f694 | hdr `.. 05 3E 80 00 0E 04 80`, `+0x36=27 +0x12=12`, `$B2BE=01` (C1:E362), `$AD89=F3` (243), 2 draws | type 5: 14*(624+64)/20=481 -> 242 +1 |
| Charm (5,4) | f730 | hdr `06 46 00..80..80`, `$B2CC=0F`, `$26` +2, `$AE60 80->00` (C1:EA35), `$B3BF=01` (C1:EA38) | kind 6 + routine 12 |
| Lightning (0,2) from `techfx/crono_cmd.state` with GATO `+0x3F` poked | f66 | res 04: `$AD89=1D` (29); 02: `3A`; 08: `0E`; **00: `0000`** (C1:EFBB), HP unchanged; 84: `$B340=E3FF0080` (heal), `$B1FC` bit6 cleared (C1:DDAD) | element x4/res, null, absorb |
| forest Cyclone (`forest_holdleft_pre.state`, `2837=80`) | f379 | `$AD8E=03 04`; last `$B1FD` write `03` by C1:E9C4; `$26` +4 | victims processed last-to-first |
| **port** Aura (`gato_ice.raw.wram` with `$2838=80`, the ice check's script) | f472 | `techact: seat=1 tech=09 tgt=6 n=1 dmg=1`, GATO 76->75 | the port's gap, sec. 0.1 |

Side product -- `$A013` spans measured (logical oracle, 1->0): Aura 307, Life 467, Haste 392, Provoke 152, Protect
152, Confuse 396, Rollo Kick 225, Charm 297, Lightning 300. The port pins only 01/0B/42 and gives every other tech
Cyclone's 313 (`battleActionSpan` default).

---

## 9. GAP LIST, ranked by how many techs need the missing piece (from `techfx_table.txt`)

Per-tech mechanisms (distinct techs; single ids 01-38, combos 39-74):

| # | missing piece | techs | notes |
|---|---|---|---|
| 1 | Dual/triple MENU path (list, readiness, combo grey) | 60 | the damage side of combos is ported; nothing can select them |
| 2 | Formula **type 5** in the tech path | 22 | `battleComputeTechDamage` falls to 1; the law is already in `battleComputeDamage` |
| 3 | **Hit roll** for techs whose effective `$AE4D` bit5 is clear | 13 | 01 04 07 14 17 19 1C 1E 25 2A 2B 2C 30; shifts every later `$26` draw |
| 4 | Kind 0 HEAL + ally targeting | 12 | 09 0C 0F 1A 1D 21 24 27 39 4B 4D 5C |
| 5 | Ally target handlers `203A`/`209A`/`20E0` (groups 00/80/01/81/03) | 21 | port target-select only walks enemies |
| 6 | **Effect-skip semantics** (bit7 = continue) | 8 | 3F 40 47 5A 60 64 6A 73 -- a port BUG, not just a gap |
| 7 | Kind 2 STATUS + DBAA categories 1/3/4/5 | 7 | 0A 0D 12 15 35 5D 67 |
| 8 | Kind 1 CURE/REVIVE | 5 | 06 10 29 50 60 |
| 9 | Geometric handlers 13/11/0F/1B/0B/0C/0D | 11 | 40 56 6A / 1F 2E / 47 64 / 37 / 02 / 11 / 5A |
| 10 | Kind 8 MULTI-HIT | 2 | 07 30 |
| 11 | Kind 6 CHARM natural + routine 12 | 2 | 2D 51 |
| 12 | Routine 18 (instant kill) / routine 8 (status rider) | 1 / 1 | 37 / 73 |
| 13 | Weapon extra effect via `$AE4E` bit7 (`EA7D`) | 25 | inert with a weapon whose `+0x47` bit0 is clear |
| 14 | `DE87` class gate (hdr+10 vs enemy `+0x19`) | 28 | inert vs GATO (`+0x19 = 00`); also missing for plain attacks |

Cross-cutting (every damaging tech -- 88 + the 2 multi-hit -- and mostly every attack):
- `C1/DCAD` tail: **Protect / Magic Wall `floor(d/10)*7`** (target side), **9999 cap**, **absorb** (`$B1FC` bit6).
- `C1/E65F`: target-status defence scaling, `$AE4E & 3`, and the **FF clamp** (a real divergence for mdef >= 103).
- **Multi-victim order** (reverse `$AD8E`) -- changes which victim gets which draw in every AoE tech.
- Routine 10: divisor 0 must give **0**; the absorb flag needs its DCAD consumer. Routine 2/11: 32-bit product.
- Header staging for techs: the port never writes `$AEE6..$AEF1`, `$AE4D/$AE4E`, `$B2CC`, `$B18B` for a tech
  (`battleStageAtkHeader` is PC-basic/enemy only), so every later reader of those bytes sees the previous action's.
- Timed-status arms (`E006`) and their ticker/expiry for Haste (`$AF48`) / Magic Wall (`$AF5E`) -- the same
  `g_btStatusTimedUnported` boundary; Haste's reload halving is already in `battleAtbLoad`.
- Per-tech `$A013` spans (presentation), 113 of 116 unmeasured.

Ported and measured: types 1/3/6, groups 07/08/12, routines 2/10/11, single + combo MP, combo damage composition.

---

## 10. Open questions (not derived here -- do not implement until closed)

1. **`C1/D70D LDA $7EB1F5,X` with X = `$B2C3` (= tech*11)**: a WRAM byte at `$B1F5 + tech*11` (for tech 1 it is
   `$B200`, the miss flag itself). If its bit7 and `$B200` bit7 are set the remaining effects are aborted. The
   operand looks like a table read that lands in WRAM; which techs it can ever fire for is not traced.
2. **`C1/D2BA CMP #80` after DBAA**: DBAA writes `$40 = 80` on a cat-1 immunity (`C1/DC5F`), never `$16 = 80`, so
   the "immune = MISS" branch looks unreachable from DBAA; measured on Provoke vs GATO: `$B200 = 00`, no MISS
   display. Whether any caller reaches `$16 = 80` otherwise is open.
3. **Line handlers `C1/25A3` (0B/0C/0D) and band `C1/2332` (0F)**: the geometry is read from the disasm; `$9608`'s
   exact effect (the far edge) needs a multi-enemy capture with the primary not the nearest.
4. **Group-flag byte `CC/1ACC & 3F` (`$9EE5` low bits: 01/02/03/05/06/0A/0B)**: consumer not traced (likely the
   multi-target cursor render).
5. **Record `+1` bit7** (Beast Toss, Drop Kick, Triple Raid, 3D Attack, Spin Strike) and **`+3` bits 04/02**: no
   reader found in bank C1 (`$B190` is read only by `EF3A`); `$B18E..$B191 -> $B192+tgt*4` (`C1/D508`) has no
   absolute indexed reader in C1/CC/CD/FD either -- the consumer of the "last hit by" record is open.
6. **Timed statuses' expiry** for Haste/Magic Wall (the `$AF48/$AF5E` families): the ticker `C1/823C` walks them
   (status note S8.4); the event run at expiry per family is not decoded.
7. **Hypno Wave / Boogie / Provoke on a NON-immune target** and the E006 arms of mask 80 (Boogie) -- not measured
   (GATO is immune to every `+0x1E` bit).
8. **Dual/triple menu staging**: which WRAM holds learned combos, the readiness gate, the second-cost grey
   (`$1A85`) -- not derived here.
9. **Kind 8 over several targets** (`$AD89 = $AE7B` staged only to `$B1FD`): no PC multi-hit tech is multi-target,
   so the behaviour of the running sum across victims is unexercised.
10. The two 16-bit leftovers already flagged in the port (`battleE575`'s `$2E`) apply to every tech too.

---

## Artifacts (scratchpad, not committed)
- `techfx_table.txt` -- per-tech summary, port status, mechanism->techs, full byte decode (generator
  `techfx/gen_table.py`; table dumps `techfx/dump.py`, `techfx/dump2.py`).
- `techfx/run.py` -- the oracle driver for the recipe; logs `aura_marle.txt`, `haste_marle.txt`,
  `provoke_marle.txt`, `confuse_crono.txt`, `rollo_ayla.txt`, `charm_ayla.txt`, `protect_lucca.txt`,
  `life_crono.txt`, `lightning_elem.txt`, `run_crono0.txt`.
- `techfx/crono_cmd.state` (+ `.raw.wram`) -- gato_wait + the Crono recipe at his command phase (f648), for pokes
  into battle blocks. `techfx/gato_aura.raw.wram` -- the port seed for the Aura run.
- Disasm anchors: executor `0x1D523`; kinds table `0x1D9D7`; kinds `0x1D221`/`1D23E`/`1D267`/`1D2DE`/`1D3BF`/
  `1D431`; loaders `0x1D132-1D220`; heal `0x1DA37`; cure `0x1DB5E`; inflict `0x1DBAA`; onset `0x1E006`; defence
  `0x1E65F`; finisher `0x1DCAD` (tail `0x1DD00`); extra effect `0x1EA6F`/`0x1EB1C`; routines `0x1ED89`..`0x1F132`;
  combiner `0x1EB68`; target handlers `0x1203A`..`0x122F5`, line `0x125A3`, band `0x12332`.
