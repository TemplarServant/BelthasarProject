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

## Open

- **`$9614` is the port's enemy SLOT, the ROM's index into the compacted `$99C0`.** The walks visit the same enemies in
  the same order, but with an empty slot below the cursor the two numbers differ (slots 0 and 2 alive: the ROM's cursor
  on cmb5 is 1, the port's 2). Every reader of `$9614` (battleSelectedEnemy, battleTargetSetRevalidate, the openers,
  src/field.c battleTargetCursorRender) would move with it. Unmeasured.
- **The list's gates**: the port builds from the alive list `$AF02`; the ROM's C1/2045 / C1/2701 / C1/2332 gate on
  `$96F5` / `$9FF7` / `$A0A8` / `$A09B` (battleTargetSetRevalidate's caveat). A hidden or dying enemy can differ.
- 0B / 0C's band writes `$9604-$9608` and the ROM leaves them; battleBandSet puts them back (the dispatch's area pass
  stages its own). 0D / 0F / 1A are derived, not measured.
- The item select's enemy groups (C1/14C5) still walk with the old code.
