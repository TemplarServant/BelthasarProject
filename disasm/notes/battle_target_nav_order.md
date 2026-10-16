# The enemy target cursor's walk: which key goes where (2026-10-15)

The order the target hand visits the enemies on Up / Down / Left / Right is not one rule: the group handler of the select
(C1/1F79 -> C1/1FF8[$960D & 7F]) runs FIRST on every pass, and six of the handlers take the pad themselves -- two of them
with Down walking BACKWARDS. The port walked every group with C1/15F9's mapping. Fixed in src/battle.c
(battleEnemyGroupStep / battleEnemyTargetPass / battleEnemyTargetWalk); graded by tools/target_nav_order_cmp.py.

## 1. The pass (C1/1561, dis.py 11561 0x60 1 0 / 115E8 0x30 1 0)

```
1561  JSR C1/1F79                 ; the group handler, with this pass's $EE / $EF
1564  LDA $9613 / BPL             ; FF (nothing lit) -> 1569 STZ $9614, the cancel tail 1577
156E  LDA $EE / AND #08           ; B -> JSR C1/1B55, 1577 the cancel
15E8  LDA $EE / AND #C0           ; A or X -> JSR C1/1B55, JMP C1/161A the confirm
15F4  LDA $960C / BMI ->1617      ; a handler that set $960C = 80 owns the cursor: no walk here
15F9  LDA $EF / AND #05           ; Right | Down -> JSR C1/1B55, JMP C1/176C (next)
1608  LDA $EF / AND #0A           ; Left | Up    -> JSR C1/1B55, JMP C1/1786 (previous)
1617  JMP C1/179C                 ; STZ $EE / $EF
```

$EF = Up 08, Down 04, Left 02, Right 01 (battle_ui_sfx_sites.md 2.4). C1/176C / C1/1786 (dis.py 1176C 0x35 1 0):
`INC / DEC $9614`, wrap 0B -> 0 / FF -> 0A, until `$99C0,X` is not negative, then `STA $A62D`.

## 2. The handlers' own pads (C1/1FF8, dis.py 12169 0xE2 1 0, 1225F 0x45 1 0, 122F5 0x34 1 0)

C1/1F79 first: `$960F = $95D5`, `STZ $9613 / $960C / $960A / $A64F / $A6D8`, `$99C0` and `$A62D` FF (C1/1F8F-1F9B).

| group | handler | list | pad | then |
|---|---|---|---|---|
| 07 | C1/20A9 | C1/2045, X = 3..0A | none: C1/15F9 walks | `$A62D = $99C0[$9614]` (C1/207E) |
| 08 / 0A | C1/20B6 | C1/2045 | none | `$960C = 80`, `$A62D := $99C0` (C1/2088) |
| 0B / 0C | C1/2169 / C1/2203 | C1/20A9 | `$EF & 09` (Up\|Right) -> C1/27FA, `$EF & 06` (Down\|Left) -> C1/27FA | `$960C = 80` (216F / 2209), the band around $960F: `$9604 = 0`, `$9605 = $960F`, `$9606 = $99C0[$9614]`, `$9607 = 02`, `$9608 = 00 / 01`, C1/25A3, C1/27E8 |
| 0D | C1/21AF | C1/20A9 | as 0B | `$960C = 80`; the seat holding char 03 (C1/21D4-21E3; none: RTS), the band around it, C1/27E8 |
| 12 / 1A | C1/225F | C1/20A9 | `$EF & 09` -> C1/27FA (NEXT), `$EF & 06` -> C1/2814 (PREVIOUS) | `$9604 = 0`, `$9605 = $99C0[$9614]`, `$9607 = 09 / 19` (1A), C1/2701 the radius, C1/27E8 |
| 0F | C1/22F5 | C1/20A9 | as 12 | `$9604 = 0`, `$9605 = $99C0[$9614]`, C1/2332 the row, C1/27E8 |

Each pad arm is `JSR C1/1B55` (the blip: sites C1/217A 2188, 21C0 21CE, 2214 2222, 226B 2279, 2301 230F) and the step.
C1/27FA / C1/2814 (dis.py 127FA 0x43 1 0) are C1/176C / C1/1786's walk behind C1/282D, which scans `$99C0` for any
entry that is not FF (none: no step), and they do not write `$A62D` -- the handler's C1/27E8 does (`$960C = 80`,
`$A62D := $99C0[0..0A]`, dis.py 127E8 0x12 1 0). With `$960C = 80` C1/15F4 skips the pass's walk, so for these groups the
handler's mapping is the only one. C1/2045 (dis.py 12045 0x55 1 0): X from the caller's start to `$80` (C1/20A9 / 20B6: 03..0A), present ($96F5,X),
`$9FF7,X` bit 7 clear, `$A0A8,X` clear (except group 04), `$A09B,X` clear; the caster $960F into `$99C0`, the rest into
`$99C1,Y`; C1/27C5 shifts the list down when `$99C0` stayed FF. C1/2332 (dis.py 12332 0x72 1 0): the centre $9605's
`$1D23` +- 20 (clamped 00 / FF), every OTHER enemy passing the same four gates whose `$1D23` is inside, in X order into
`$99C1,Y`, the centre into `$99C0`.

Group users (CC/1ACB[tech * 2]): 0B Slash (02), 0C Flame Toss (11), 0D 5A, 0F 47 / 64, 12 Cyclone (01) and eleven more
(14 17 34 3B 3C 3E 48 49 52 53 54); 1A none among the techs.

## 3. Measured (the rule's cartridge, tools/target_nav_order_cmp.py)

gato_wait with four Hetakes staged through op 0x83 (tools/battle_sim.py) at cells cmb3 12,8 / cmb4 6,7 / cmb5 8,3 /
cmb6 10,10, Crono alone; the keys 60 frames apart between the select's open and its A.

- Cyclone, Down x4: `$9614` 00 (f665) -> 03 (f680) -> 02 (f740) -> 01 (f800) -> 00 (f860); `$99C0 = $A62D` = 03 06 /
  06 03 / 05 / 04 / 03 06; `$960C = 80` from the open (f665).
- Cyclone, Up x4: 0 -> 1 -> 2 -> 3 -> 0. Right / Left: Right forward, Left back.
- Slash, Down / Up: 0 -> 1 -> 2 -> 3 -> 0 (every key forward), `$A62D = 03` (the band around Crono holds cmb3 alone).
- Att: Down forward, Up back; `$A62D` = the cursor's enemy alone, `$960C = 00`.

Old port: Cyclone's walk 0 1 2 3 0 on Down and 0 3 2 1 0 on Up, Slash's Up backwards, `$A62D` never written during an
enemy select (5/12). Fixed: 15/15, every frame of `$9614` / `$A62D` identical on all five cases. Pinned: regress check
`battle-enemy-target-walk-order-vs-rule-cartridge` (full tier, 3 mutations killed); full suite 857 pass, 0 fail.

## 4. The port

battleMenuPassKey reads the handler's step from `$EF` first (battleEnemyGroupStep: the table above), and its blip site;
battleEnemyTargetPass is C1/1F79 for groups 07 / 08 / 0A / 0B-0D / 0F / 12 / 1A, run by the select's open (the Att's
C1/12B0, a tech's C1/1379) and first on every pass of the Att's and the tech's select: the step, `$960C`, `$99C0` and
`$A62D` (the set from battleBuildHitSet / battleBandSet / the row). battleEnemyTargetWalk is C1/176C / 1786 with its
`STA $A62D`. The other groups (05 / 06 / 09 / 0E and up) keep the old path.

## 5. `$9614` is an index into `$99C0`, not an enemy slot (2026-10-15, second pass)

C1/20A9 (`$80 = 0B`, X = 3, `INC $960A`) -> C1/2045 (dis.py 12045 0x55 1 0):

```
2047  LDA $96F5,X / BEQ skip        ; present
204C  LDA $9FF7,X / BMI skip
2051  LDA $A0A8,X / BEQ ok          ; set: skip unless the group is 04
205F  LDA $A09B,X / BNE skip
2064  CPX $960F / BNE               ; the caster ($960F = $95D5, a seat: never X >= 3) -> $99C0
2071  TXA / STA $99C1,Y / INY       ; the rest in X order
2076  INX / CPX $80 / BNE 2047
207B  JSR C1/27C5                   ; $99C0 still FF: shift the list down one
207E  LDA $9614 / TAX / LDA $99C0,X / STA $A62D
```

then C1/1FCC-1FD9: no `$A62D[0..0A]` without bit 7 -> `$9613 = FF` (C1/1F80 cleared it). The tech's open (C1/137C)
reads it as a veto (`STZ $9614`, `$9609` stays 0); the Att's open (C1/12B0) has none, and the held select's next pass
cancels (C1/1564 -> C1/1569 `STZ $9614` -> C1/1577). Neither opener resets `$9614`: the confirm (C1/1716 area), the
cancel (C1/1577) and the teardown (C1/1C02) zero it. So the cursor is a position in the compacted list: with an absent
or dead enemy below it the number and the enemy's slot differ, and when the cursor's own enemy leaves the list the same
index lands on the next one (battle_queue_death.md A.3, f381: `$99C0` compacted, `$9614` unchanged) -- or past the
end, which cancels.

MEASURED, the rule's cartridge (tools/target_nav_order_cmp.py, the cases whose name holds ':'):

- **gap** (the four-Hetake fixture without op2 4: slots 0 / 2 / 3, cmb4 never staged): the Att's Downs `$9614` 0 -> 1 ->
  2 -> 0 and Ups 0 -> 2 -> 1 -> 0, `$A62D` = 03 / 05 / 06; Cyclone's Downs 0 -> 2 -> 1 -> 0, its Ups 0 -> 1 -> 2 -> 0;
  Slash's Downs 0 -> 1 -> 2 -> 0.
- **killed** (four Hetakes, cmb4 at 1 HP from f600; Crono's Att, Down, A kills it): `$A0A8,4 = 01` and `$AF02[1] = FF`
  at f817, `$96F5,4` cleared at f860 (the death routine's CF/FE7B); the next turn's (f1379) Att and Cyclone selects walk
  the three left as in the gap case.

Old port (fb0ddd5, `$9614` the slot over `$AF02`): 6/18 -- every walk the slot sequence (the gap Att 0 2 3 0 3 2 0, Cyclone Down 0 3 2 0), `$A62D` the same enemies on
the same frames; the original five cases 15/15 both ways. Ported: 18/18, every held frame of `$9614` / `$A62D`
identical; the original five cases unchanged (15/15). Pinned: regress check
`battle-enemy-target-cursor-indexes-the-list-vs-rule-cartridge` (full tier, 2 mutations killed);
`battle-enemy-target-walk-order-vs-rule-cartridge` now runs the five cases without ':' (`--skip :`).

The port (src/battle.c): battleEnemyList is C1/2045 for X = 3..0A with its four gates (the list no longer comes from
`$AF02`); battleEnemyCursor = `$99C0[$9614]` (-1 past the end), exported as CtBattleEnemyCursor for
src/field.c battleTargetCursorRender; battleTargetNav is C1/176C / 1786's wrap (0B -> 0, FF -> 0A, skip FF) over it;
battleSelectedEnemy reads the cursor (the first entry when the index is past the list -- a defensive fallback, kept);
battleEnemyTargetPass / battleCasterRadiusBuild / battlePcTargetPublish write `$9613` (battleTargetLitFlag);
battleEnemyTargetEmpty is C1/1564 on the Att's and the tech's pass; the tech's open takes C1/137C's veto; the openers'
"dead slot -> first alive" resets are gone (the ROM has none); battleTargetSetRevalidate keeps only its empty-set branch
(the non-empty re-resolve is the index's own behaviour now). battleEnemyTargetWalk returns for a group with
`$960C = 80` (C1/15F4) and for the groups the port has no handler for (02 / 05 / 06: one entry; 09 / 18: `$960C = 80`).

## Open

- ~~`$9614` is the port's enemy SLOT~~ -- closed by sec. 5.
- ~~The list's gates~~ -- closed by sec. 5 for C1/2045 (the enemy list). The measured death delisted through `$A0A8`
  and `$AF02` on the same frame, so the fixture does not tell the two apart; a hidden enemy (`$9FF7` bit 7) or one
  whose `$96F5` drops without `$AF02` would. battleTargetSetRevalidate's empty test still reads `$AF02` + `$AF15`
  bit 6 (its regress check pins the teardown frames) -- the ROM's is the same C1/1FD9 at the pass.
- The cursor's own enemy dying while the select is held is derived, not measured: the index then lands on the next
  entry, or past the end on the last one, and the Att's select cancels at the next pass (C1/1564). The cadence of the
  pass against the port's render-frame revalidate is the open note of battle_queue_death.md (f378 -> f381).
- 0B / 0C's band writes `$9604-$9608` and the ROM leaves them; battleBandSet puts them back (the dispatch's area pass
  stages its own). 0D / 0F / 1A are derived, not measured.
- The item select's enemy groups (C1/14C5) still walk with the old code.
- The groups 02 / 05 / 06 / 09 / 18 (C1/20D6 / 2136 / 2163 / 20C6 / 2329) have no handler in the port: their `$99C0`,
  `$A62D` and `$9613` are not published, and the hand reads CtBattleEnemyCursor over the enemy list.
