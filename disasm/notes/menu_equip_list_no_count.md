# The Equip candidate list draws NO count (C2/A569 -> C2/A59A)

User report 2026-10-09 (translated from Portuguese): *"the equipment count shows on the Equip screen when there is more than one"* -- the port's
Equip list printed a number after a candidate's name when the bag held more than one. The cartridge never does.

## The ROM

- **C2/8953** (the list build; a byte scan of bank C2 finds its two `JSR`s at C2/9C61 and C2/A466 -- the latter inside
  C2/A463, which the shop's C2/E0F1 also runs): walks the bag
  `$2400,Y` / `$2500,Y` (Y < 0xF2), keeps the ids in the slot's range (`C2/89DC[$0077*2]`) that C2/93A8 accepts, and
  stores `$0F00,X` = id and **`$0F62,X` = the count** (2898A `LDA $2500,Y / STA $0F62,X`); then sorts both arrays together
  by `$7C00[id]` (289A5-289D4).
- **C2/A569** (the eight rows): `$22` = 8, X = `$4FC6` (BG buffer row 7, col 3; `+0x80` a row), Y = `$0FC8`; each row
  `LDA $0F00,Y / STA $04C9`, `INY` unless 0, `JSR C2/A59A`. It never reads `$0F62`.
- **C2/A59A** (one row): `$7D` = 2000 (attribute); id != 0 -> `JSR C2/F2E2` (Y = `$7D00[id*2]`, A = CC0B, the name in
  CC:0B5F) / `DEC A` / `INY` / `JSR C2/EF65` -- the name's 10 characters after its icon byte; id 0 -> `LDY #9B76 /
  LDA #7E0A / JSR C2/EF65` (10 blank cells). `STZ $7D`, return. **No number routine is called.**
- The one-row slide's incoming row, **C2/9F71-9F84**: `LDA $0F62,Y / STA $04CA` (the count staged), `LDA $0F00,Y /
  STA $04C9`, `LDX #53C6 / JSR C2/A59A` -- the same name-only writer. `$04CA` is not read on this path (it is the shop's
  quantity bound elsewhere, C2/DA01).
- A byte scan of bank C2 for `62 0F` operands finds only 2898D / 289BA / 289C6 / 289C9 / 289CD (the build and its sort)
  and 29F75 (the slide's stage): nothing in the bank draws `$0F62`.

## The oracle

Drive `tools/menu_audit_cmp.py equipcount` (saves/battle_re/party3_field.state, CT_ORACLE_LOGICAL=1), pokes on BOTH
sides: `$2403=02 $2503=03` (Iron Blade x3), `$2404=03 $2504=0C` (Steelsaber x12); X, A (Equip), A (Crono), Down, Up, A
(the Weapon list) ... (the equiplist presses).

At oracle frame 212 (port f210, the list risen, substate 3): `$0F00` = `03 02 00`, `$0F62` = `0C 03 00`. The BG buffer
(`$7E:4E00 + row*0x40`, words) and VRAM (word `$6800 + row*0x20`) hold the same:

    row 7  (list row 0): cols 3-12 = 20B2 20CD 20BE 20BE 20C5 20B2 20BA 20BB 20BE 20CB ("SteelSaber"), cols 13-31 = 0000
    row 9  (list row 1): cols 3-12 = 20A8 20CB 20C8 20C7 20FF 20A1 20C5 20BA 20BD 20BE ("Iron Blade"), cols 13-31 = 0000
    row 11 (list row 2): cols 3-12 = 20FF x10 (C2/A59A's id-0 blank), cols 13-31 = 0000

No digit tile anywhere on the row; the picture (build/_menuaudit/equipcount/o_210.png) shows the two names alone. The
same in the gear panel (substate 2, f120).

## The port

`src/menu.c` `mEquipCandRows` (both gear substates) and `mEqListSlideRows` (the slide's strip) drew
`if (cnt[i] > 1) mNumR(..., cnt[i], 116, y)` -- a count C2/A59A does not write. Both removed; the rows are the name only.
The shop's equip sub-mode (`mShopEqRows`, the same C2/A569) already wrote the 10 name cells only. The Item screen's
count is its own routine and is untouched (menu-item-* checks green).

Before: `equipcount` 151 of 361 frames differ (box x100-115 on the list rows: f117-131 and f152-180 at y108, f188-251
at y76, f260-302 at y108); peeks f120 / f210 133 px each. After: 0 of 361, peeks 0 px; `equipslidecount` (weapons
02-0D held 2-13, Down x10 / Up x10 through C2/9F90's slide) 0 of 561.

Checks: `menu-equip-list-draws-no-count-vs-oracle`, `menu-equip-list-slide-draws-no-count-vs-oracle` (one mutation
each, re-adding the count line at each site: both KILLED).

## Open questions

None for the list. (`$04CA`'s value after the slide stage is not graded; nothing on the Equip path reads it.)
