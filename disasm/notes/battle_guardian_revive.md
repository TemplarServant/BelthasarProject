# The Guardian's Bits: the revive, the roles, the item header, the lethal hit (2026-10-26)

The playthrough's `40_guardian` (Arris Dome 0DB) fights the Guardian (enemy D4, combatant 3) and its two Bits (CF,
combatants 7 and 8) with tools/battle_pilot.py (`PILOT_TARGETS=CF,D4`). On the rule's cartridge the party wins in
about 23000 frames. The port lost it three ways, and each one was a mechanism, not a number. All of them were measured
with `--monitor --diff --writer` against the port's memmon, from the leg's start state.

## 1. Effect op 2C, C1/58AB: the combatant back in the fight

```
58AB  LDA $A010 / CMP #08 / BCS 58D4        ; tracks 8.. : nothing
      CMP #04 / BEQ 58C3
      TAX / LDA $A014,X / TAX               ; a role track: its combatant
      INC $96F5,X / STZ $A0A8,X / BRA 58D4
58C3  TDC / TAY
      LDA $A018,Y / BMI 58D4                ; track 4: the list, to its FF
      TAX / INC $96F5,X / STZ $A0A8,X / INY / BRA 58C5
58D4  INC $A00F / LDA #01 / JMP C1/75BB     ; free
```

`$96F5,X` is the "present" byte and `$A0A8,X` the "gone" flag that C1/2045 tests when it builds the target list
`$99C0`. The Guardian revives both Bits at f12006 (`$1D` bit 7 cleared by FD:B487), and op 2C makes them targets at
f12140 (C1:58CE / C1:58D1). The port had no op 2C. Its list then held the Guardian alone (`$99C0 = 03` against the
cartridge's `03 07 08`), so the pilot's attack hit the Guardian and his counter took the party.

## 2. The victims' roles, CC/F186-F1C9

`$A017` (role 3, the primary) is the index of the first set bit of the 16-bit mask `$AE95` (bit 15 = combatant 0;
0 when the mask is empty), followed by `ORA #FF00 / STA $A017`. `$A018..` takes every set bit of `$AE99`, combatants
0..0A in order, each stored 16-bit as `idx | FF00` (index registers 16-bit). `$AC5C..` gets the same values. The
revive has `$AE95` = 0100 and `$AE99` = 0080, so `$A017 = 07` and `$A018 = 08 FF` (CC:F1A7 / F1BF). The port built
both from its own target list, which held Bit 7 alone, so the track-4 op 2C brought back one Bit. The fix is
`battleRoleVictims` in src/battle.c.

## 3. A PC item's command header, C1/BAC9-BB09

`$AE90 = $AE94 = 0`, `$AE91 = $B1BE[$B2EA]`, `$AE92 = 03`, `$AE93` = the item. The victims go from `$AD8E` into
`$AECC` (`$AECB` the count), then JSR C1/AD09 (`$AE95`) and C1/AD35 (`$AE99`). After C1/BF46 the header is rewritten:
`$AE91 = $B18B`, `$AE92 = 03`, `$AE93` = the item, `STZ $AE94`. Crono's Tonic on Lucca at f7819 has `$AE91 = 00 03
BD` and `$AE95 = 0020`. The port left the previous action's masks in place. Once the roles came from the masks, the
Tonic's effect played on Crono's seat and the heal landed 24 frames late.

## 4. The lethal hit's staging at the compute, C1/DDD9-DE3A

In C1/DDBF (the damage kind's record), `LDA $5E30,X / CMP $AD89` with the victim's HP at or below the damage leads to:

```
DDEF  $B202 = 0 / JSR C1/EBF8              ; the slot, as a survivor's
DDF7  victim >= 3:  $AEB2 = 1, $AEB0,X = 1, $5E0A,X = 0      ; the REQUESTED move mode
DE0A  $B03A,victim = 0                      ; the ATB flag stops
      victim < 3:  $B242,Y = FF for every Y < 8 with $5E15,Y = victim
DE35  $AECB = 1, then the kind-3 record (DE5B-DE7D)
```

Bit 7's lethal hit writes `$5E11 = 00` at f6212 (C1:DE0A). The mode machine commits the request after the revive
(`$974E = 00` at f12319, C1:336A), and the Bit stands at its place. The port left this staging to its end-of-action
death path, so the request stayed at 0A (the hover) and the revived Bit walked 32 px off its place (C1/3714's
segments, `battleMoveStepper`). Every later attack then ran 2-9 frames long, until an enemy chose another target.

After the four fixes the leg is SAME: every chain matches the cartridge, and the end picture is 0 px at port +0.
