# Tech damage: formula type 6, extra-effect routine 11, one number per combatant, and the demo's deaths (2026-09-19)

USER REPORT (translated from Portuguese) "in the frog's skill, the damage on the enemies is "1", and 1 enemy is missing". The attract's 07D demo battle,
tech $28 (Frog Squash). Graded by these checks:
* `attract-07d-frog-squash-damage-vs-oracle`
* `attract-07d-frog-squash-five-numbers`
* `attract-087-ice-sword-damage-vs-oracle`

## 1. Formula type 6 (C1/E369-E3CA)

CC/213F[28] = `03 00 00 00 00 06 3E 60 00 0A 04 00`: +5 = type 6, +6 = defsel 3E (physical), +9 = POW 10.

```
E36D  JSR C1/E63B                         (wake the target: $5E4B & F9, ATB bookkeeping -- only if set)
E370  $2C = $5E64,X ($B1F4 = the attacker's block: +37) * 1 * $AEEF (POW)     two C1/C90B
E392  $28 = $5E32,X - $5E30,X (maxHP - curHP, 16 bits, also -> $0A)
E3A5  $2E:$2C = $28 * $2A                 C1/C90B: 16 x 16 -> 32
E3AC  $2C = $2E:$2C / 140                 C1/C92A: 32-bit dividend, 16-bit quotient
E3B4  A = $0A / JSR C1/C1B2               $30 = d/8 - d/64 - d/128 - d/1024 (each term from the previous shift)
E3BD  TDC / TAX / LDA $30 / CLC / ADC #01 / JSR C1/AF22 / STA $B2BE   the bonus: an 8-bit count, base 0
E3CA  JMP C1/E575                         the attacker's status multipliers, then $16 = $2C (E632)
```

The caller then picks the defence ($18 from defsel; C1/E678 makes the magic defence 3C x5/2) and runs the finisher
C1/DCAD. That is the same finisher as the physical: `$AD89 = $16 * (0x100 - $18) / 0x100`, `+ $B2BE & 0xFF`, then the
min-1 floor.

MEASURED: Frog (seat 1) has HP 50/800, `+37 = 128`, POW 10, and the enemies' `+3E = 127`. That gives
128 * 10 * 750 / 140 = 6857, then 6857 * 129 / 256 = **3455**. The bonus is 0, because a demo battle's AF22 returns
its base (`$2989` bit 5). This is the cartridge's `$B328 = 7F 0D` on targets 3-7 (C1:EC38, f4617/f4618 from
saves/attract_07d_battle_f4540.state). The port had no type 6 and fell to the floor: 1.

## 2. Extra effect routine 11 (the Ice Sword)

CC/1BEB[3A] = `07 61 40 22 00 04 0B 80 08 08 00`: Crono's effect CC/213F[04] and Marle's CC/213F[0B], both with
`$B2CC = 08`. CC:2A05[08] = `0B 05 04`, and routine 11 of the table C1/FA61 is C1/EFBC:
`JSR C1/EDBA / JSR C1/EF39 / RTS`. That is routine 2 (x`$1C`/`$1E` = x5/4), then routine 10 (the element; Zombor is
neutral, x4/4).

ORACLE, f7452 (--hookpc from the attract):
* C1/DCED `$AD89` = 00C2 / 00A1, i.e. 194 / 161.
* C1/EF39 is entered with 00F2 / 00C9, i.e. 242 / 201.
* C1:EC38 stores them as HIT 0 and HIT 1: `$B328 + tgt*4 + $B2C7*$2C` (C1/EC10-EC2C, Y = 24 / 50).

The display record sums the hits. The X-Strike reads `$ADB8 = 0F 01 03 00` (271 = 123 + 148, kind 3, C1:EBB4). The
port already summed; it lacked routine 11 and showed 355 where the cartridge shows 443.

## 3. One number per combatant

Op 0x50 (C1/5ADB) clears its per-combatant state `$92DD,X` (eleven bytes) on its first visit. C1/5B49-5CD4 then
walks the eleven `$AD9C/$AD9E` records (value, kind), and the number's X/Y is `$1D0C,Y` / `$1D23,Y` (+ `$9708/$9713`
when `$A003,Y`). The cartridge's OAM at f5130 has five `3455`s. The port had a pool of four that recycled the oldest,
so the fifth target's number replaced the first's. `CT_BATTLE_FX_POOL` is now 11 and a combatant's number is its own
slot. A second number on the same combatant replaces the first, as the record does.

## 4. A demo battle issues no death command (C1/AC75)

The five kills (HP 0 at f596, C1:ED12) run no death on the cartridge: no CD/0043 and no CD:3B7D in the 700 frames
after, and the enemies are still drawn when the battle hands back. The reason:

```
C1/AC75  LDA $2989 / BIT #20 / BNE ->AC81      the demo battle
C1/AC7C  LDA #06 / JSR C1/0003                 -> C1/0045's table entry 6 = C1/006D `JSL CF/FE20`
```

CF/FE20 walks `$AEB0,X` (X = 10..3) and runs CD/0024 (the death command CD/0043: the dissolve, the `#95`) for each
dying enemy. The HP, the dead flag, the delist (FD/ABA2) and the rewards are C1/ECC7-ED17's and stay. The port
started the dissolve and the sound at HP 0. The five dissolves then held the battle 78 frames (end-teardown f699
instead of f657), and every later attract leg moved. Now a demo death sets `deadNoCmd`, and the renderer keeps
drawing the enemy.

## 5. Open, measured

* ~~**The victims' positions during tech $28.**~~ CLOSED 2026-09-23: the tech does not move them -- the cartridge's
  `$1D0C`/`$1D23` are constant for the whole fight. They were STAGED wrong: the 07D's field placed them with a
  mis-ported op 0x8C (disasm/notes/field_op8c_place_from_vars.md). The staged records and the five numbers are now
  the cartridge's: (33,183) (205,187) (80,127) (159,127) (120,127).
* The enemies' white flash beside the frog (battle_effect_orbit.md 9).
