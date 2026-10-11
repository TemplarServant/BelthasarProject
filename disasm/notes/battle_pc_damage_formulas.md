# BATTLE — a PC's physical is resolved per CHARACTER ID, and its block is derived from EQUIPMENT

User report (translated from Portuguese): *"lucca's damage too high"*. Two independent defects, both of them a **per-character constant
standing in for a ROM table**, and they compound:

1. the attack RECORD was hardcoded to Crono's (`0x3A`), so Lucca and Marle ran the **melee** formula with the
   **wrong power** and the **wrong variance range**;
2. the derived battle-block bytes `+0x3C..+0x45` were **baked for charId 0 and 1 only**, so for every other
   character the block kept the ROM's own `0xFF` placeholder at `+0x43` (the weapon term of the damage base)
   and `+0x45` (the crit RATE) — measured: **Lucca one-shot a full-HP 50-HP Diablos, 580 damage where the
   ROM's answer for the identical action is 17**, and she crit on every hit.

Defect 2 is the one the user saw, and it is reachable from our own new game: `src/data/ct_charbase.h` ships
`+0x41..+0x4F = FF` for all seven characters because `$CC:0000 + cid*0x50` does.

---

## 1. The attack record — `CC/2583` → `CC/1BEB` → `CC/213F`

```
C1/BFF3  LDA $B18B / TAX / LDA $AEFF,X / CMP #FF / BEQ ->1C021 / TAX   ; seat -> charId
C1/BFFF  LDA $CC2583,X / STA $B18C                                     ; charId -> ACTION id
C1/D53A  JSR C1/C90B / STX $B2C3                                       ; $B2C3 = $B18C * 11
C1/D5CD  LDA $CC1BF0,X / STA $B2C8 / BIT #80                           ; = CC/1BEB[$B2C3+5+$B2C7]
C1/D5EF  LDA $CC213F,X / STA $AEE6,Y                                   ; the 12-byte attack record
```

`CC/213F` record: **+5 = the FORMULA TYPE (`$16`)**, **+9 = the POWER (`$AEEF`)**, +6 = the defence selector.

| charId | action `$B18C` | record `$B2C8` | type | POW |
|---|---|---|---|---|
| 0 Crono | 0x75 | **0x3A** | 1 | 8 |
| 1 Marle | 0x76 | **0x3B** | **2** | **6** |
| 2 **Lucca** | 0x77 | **0x3B** | **2** | **6** |
| 3 Robo | 0x78 | 0x3A | 1 | 8 |
| 4 Frog | 0x79 | 0x3A | 1 | 8 |
| 5 Ayla | 0x7A | **0x39** | **5** | **9** |
| 6 Magus | 0x7B | 0x3A | 1 | 8 |

`CC/2583[0..6] = 75 76 77 78 79 7A 7B`. **There is no weapon-class branch on the equipped item** — the
"ranged weapons use another formula" behaviour is entirely this charId → record chain.

## 2. The three shipped formulas — `C1/E0F5`, dispatch on `$16`

`X = $B1F4 = attacker * 0x80`, block base `$5E2D`, so `$5E63,X` = `+0x36` Power, `$5E67,X` = `+0x3A` Hit,
`$5E70,X` = `+0x43` weapon term, `$5E3F,X` = `+0x12` Level.

```
type 1  MELEE   1E106 +0x36*12 -> $0A ; 1E11B +0x43*5  -> $30 ; 1E135 *POW ; 1E152 LDX #0012 (/18)
                1E15E bonus  LDA $5E63,X / LDX #0003 / ADC #01        -> rand(0 .. +0x36/3)
type 2  RANGED  1E18F +0x3A*8  -> $0A ; 1E1A4 +0x43*8  -> $30 ; 1E1BE *POW ; 1E1DB LDX #0012 (/18)
                1E1E3 bonus  LDA $5E67,X / JSR C1/C063 / ADC #01      -> rand(0 .. (+0x3A*77)>>8)
type 5  AYLA    1E2E1 +0x36*16 -> $0A ; 1E2F7 (+0x12)^2 /5 -> $30 ; 1E31A *POW ; 1E337 LDX #0014 (/20)
                1E34D bonus  LDA $5E3F,X / LDX #0003 / ADC #01        -> rand(0 .. +0x12/3)
```

`C1/C063` is a shift chain — `XBA` (×256) then `v*128 − v*32 − v*16 − v*2 − v` = **`v*77`**, then
`AND #FF00 / XBA` = **`>>8`**. Measured live: Lucca HIT 8 → `$0030 = 2`, Marle HIT 10 → `$0030 = 3`, both
stored by **`C1:C096`** (the `STA $30` inside `C1/C063`) — a direct proof that the type-2 tail ran.

Everything downstream is shared and was already right: defence `C1/E65F` (`+0x3E` raw, or `+0x3C * 5/2` when
the record's defsel is `0x3C`), the cut `C1/DCB9` `((256−def) * base) / 256 + bonus`, the min-1 floor
`C1/DCF0`, and the crit `C1/ED89` (`rand(0..99) <= +0x45` → `×2`). **Every PC crits**, Lucca and Marle
included: `CC/1BEB[+8] = 0x01` for every PC action record → `$B2CC = 1` → `CC/2A05[3] = 00 00 02` →
`JSR ($FA61,X)` → `C1/ED89` with multiplier `$1E = 2`.

## 3. The block is DERIVED at battle boot — `C1/FC00-FC7B` + `C1/CF15` + `FD/B033`

`C1/CF15` is a 3-seat loop over the block table `FD:A80B` whose caller hands it a source offset (`$06`), a
destination offset (`$08`) and a count (`$0A`):

```
1CF2D  LDA ($06),Y / JSL FD/B033          ; block[src] = an ITEM ID -> X = its record offset in bank $CC
1CF35  LDA $CC0000,X / STA ($08),Y / INX / INY / INC $04 / LDA $04 / CMP $0A / BCC ->1CF35
1CF48  INC $02 / LDA $02 / CMP #03 / BCC  ; three seats
```

`FD/B033` — the whole id map, five ranges:

| id | → offset in `$CC` | what |
|---|---|---|
| `< 0x5A` | `$0262 + id*5` | weapons |
| `0x5A..0x7A` | `$047E + (id−0x5A)*3` | armor |
| `0x7B..0x93` | `$04E1 + (id−0x7B)*3` | helmets |
| `0x94..0xBB` | `$052C + (id−0x94)*4` | accessories |
| `0xBC..0xF1` | `$05CC + (id−0xBC)*4` | consumables (= our `ROM_ITEM_AMOUNT_TBL`) |
| `>= 0xF2` | — | no record (`FD/B0B5 CMP #F2 / BCS ->B0D4`, X untouched) |

The battle boot calls it four times per seat and then sums the defence:

```
1FC08  $06=$29 $08=$43 $0A=06 / JSR C1/CF15     weapon    -> +0x43..+0x48
1FC17  $06=$27 $08=$70 $0A=03 / JSR C1/CF15     helmet    -> +0x70..+0x72
1FC26  $06=$28 $08=$48 $0A=03 / JSR C1/CF15     armor     -> +0x48..+0x4A   (over the weapon's 6th byte)
1FC35  $06=$2A $08=$4B $0A=04 / JSR C1/CF15     accessory -> +0x4B..+0x4E
1FC5D  +0x3E = min(0xFF, +0x37 + +0x48 + +0x70)                             ; 1FC71 CMP #00FF / BCC
```

**Verified byte-for-byte against the oracle's own `bat9_lake` blocks** (equip ids read from the state):

| PC | weapon | rec | armor | helmet | `+0x43` | `+0x45` | `+0x48` | `+0x70` | `+0x3E` |
|---|---|---|---|---|---|---|---|---|---|
| Crono | 0x01 | `03 00 0A 00 00` | 0x5B `05 00 00` | 0x7C `03 00 00` | 3 | 0x0A | 5 | 3 | 9+5+3 = **0x11** |
| Lucca | 0x1F | `05 00 14 00 00` | 0x5C `0A 00 00` | 0x7C | 5 | 0x14 | 0x0A | 3 | 6+10+3 = **0x13** |
| Frog | 0x3B | `06 00 17 00 00` | 0x5D | 0x7D | 6 | 0x17 | 0x10 | 8 | **0x26** |

— and the oracle's own erase of the placeholder is measured directly on `gato_wait.state`
(`--monitor "7E5EF0:1,7E5EF2:1" --diff`): `f316 7E5EF0=FF 7E5EF2=FF` (the raw sheet copy) →
`f317 7E5EF0=03 7E5EF2=14`, writer **`C1:CF3B`**.

## 4. Measured, oracle vs port

| action | oracle | port before | port after |
|---|---|---|---|
| Lucca physical, bat9 | `$AD89 = 0x11` = **17** (`$B18C=77`, POW 6, tail writer `C1:E1F9`, `$0030=2` via `C1:C096`) | 10 with a derived sheet; **580 (one-shot)** with the ROM's `FF` sheet | `atkform … rec=3B type=2 pow=6 … base=34` → 17 |
| Crono physical, bat9 | 19 / 21 (`C1:E17D`) | 19 / 20 | base 38, unchanged |
| Frog physical, bat9 | 36 / 39 | 36 | base 72, unchanged |
| Marle physical, gato | 20 base / **40 crit** (`f877 $AD89=0x28`, writer `C1:EDB0`) | 8 base / 16 crit | base 34 → **GATO 59 → 19 = 40** |

The `pcblock` trace (`CT_BATTLE_TRACE=1`) prints the derived block per seat at staging; on the ROM's own
`FF` sheet it now reads exactly what it reads on a derived one.

## 5. What is NOT ported (measured boundaries, none of them read by the damage pipeline)

* **`C1/CE3A`'s `+0x0B..+0x11 → +0x36..+0x3C` stat copies.** The field sheet already carries those bytes and
  they match the oracle's block byte-for-byte in every fixture read (Crono and Lucca in `bat9`).
* **The accessory EFFECT pass** (`C1/FC7F`: `+0x4C bit6 && +0x4D != 0` → `JSL FD/B14D`, `$0E = $29D7`). This
  is why Crono's `+0x38` (Speed) carries his Bandana's +1 — his accessory `0x95` record is `00 40 01 00`,
  bit6 set. We inherit the bonus from the sheet instead of deriving it.
* **`C1/CE9F`/`C1/CEDA`/`C1/CF08`'s equipment EFFECT tables** (`$CC:06A4` weapon / `$CC:08C0` armor /
  `$CC:0986` helmet, stride 6, through `FD/B141` + `C1/CF69` into `$29D7`/`$2A03`) — the elemental/status
  properties of gear. Block `+0x3D` and `+0x3F..+0x42` therefore still come from the sheet copy.
* **`C1/E575`'s attacker-flag multipliers** (`+0x4E`/`+0x53` bit7 → ×1.5, bit3 → ×1.5, bit1 → ×1.25). Zero in
  every fixture, unexercised, unported.
* **A type-2 TECH.** `battleComputeTechDamage` dispatches on the tech record's own `+5` and implements types
  1 and 3; a type-2 tech would fall to the base-0 arm. No shipped PC tech is type 2 in any fixture read.
* **Ayla (type 5) and Magus/Robo** are table-reads only — no fixture contains them. The formula is
  implemented from the disassembly; the probe that would close it is a capture with Ayla in the party
  (`$AEEF` must read 9 and the tail writer must be `C1:E362`).

## 6. Checks

`field-battle-pcblock-derive`, `field-battle-lucca-type2`, `field-battle-marle-type2` (below). The
anti-rot rule for all three: they must run on a seed whose **sheet carries the `FF` tail**
(`saves/battle_re/party3_freshsheet.lakewram`, or `gato_t0.raw.wram`, which does for both PCs). On a seed
whose sheet is already derived, deleting `battlePcEquipDerive` changes nothing and the check rots green —
which is exactly how this defect shipped: every battle fixture in the suite was an oracle capture of a
Crono/Marle fight, i.e. the two charIds the bake covered.
