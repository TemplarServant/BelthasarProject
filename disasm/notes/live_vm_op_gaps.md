# The live VM's op gaps: what move.c's graded runner ran and ct_vm.c stepped over (2026-10-08)

The tent of horrors' hostage (op 0x72, disasm/notes/tent_hostage_local_inc.md) was one op of a class: `src/move.c`'s
`CtFieldRunnerOne` -- the byte-exact runner `--sweepops` micro-executes against the ROM -- carried a body that the LIVE
event VM (`src/ct_vm.c`: `step()` / `applyEffect()` / `moveOp()`, and the init pass `runFunc()`) either did not have
or paraphrased. A missing arm is not a hang: both runners advance the IP by the op's length (`opLen`) when nothing
claims it, so the op is skipped with no effect and `CT_VM_UNHANDLED=1` (which reports only an IP that did not move)
prints nothing.

## 1. Method

1. The set: every `case 0xNN` of `CtFieldRunnerOne`'s main switch (218 labels; 0x93/0x9A/0x9B appear only inside op
   0xFF's sub-switch and are not ops of this runner).
2. For each, the live VM's arms: `op == 0xNN`, ranges, `case`, the shared-family calls, `pcSprite()`, in `step()`,
   `applyEffect()`, `moveOp()` and `runFunc()` separately (the init pass is its own runner: C0/58ED dispatches
   through the same table C0/5D6E as the event runner, `5910 JSR ($5D6E,X)`, and ignores the carry, `5913 BRA`).
3. Where both have an arm, the effect compared op by op: WRAM bytes vs a C field only, 8 vs 16 bits, literal vs
   variable operands, the carry (the visit's end), and the operand range the shipped scripts actually use.
4. Sites: every object's 16 fn pointers of every parseable scene script, walked with `assets/script.py iter_ops`
   (shared code counted once); a site is INIT when it lies on fn0's walk before its first `00`.

## 2. The table

ROM = the C0/5D6E handler. "move.c" = what `CtFieldRunnerOne` did; "live before" = ct_vm.c before this change.

| op | ROM | move.c | live before (step / init) | sites (init) | action |
|---|---|---|---|---|---|
| 0x1C | C0/654E | result word := global word | step: NONE (skipped) / init: ok | 2 (2), loc 055 | CtVmResultFamily |
| 0x19 | C0/653B | $7F0A80,X := local WORD | step: C field `result` only, 8-bit / init: ok | 104 (35) | CtVmResultFamily |
| 0x1A | C0/6568 | CASE on $7F0A80,X low byte | the C field | 758 (122) | CtVmResultFamily (reads WRAM) |
| 0x20 | C0/658F | local := $7E2980 (the lead's id) | NONE / NONE | 8 (1): 040 08F 0FF 124 147 1B2 | CtVmObjQueryFamily |
| 0x23 | C0/65F8 | local := object's $1600 | NONE / NONE | 4: 086 087 10C 127 | CtVmObjQueryFamily |
| 0x24 | C0/661F | local := slot's $1600 | the C facing / NONE | 7: 005 013 03F 0D6 0F4 1A9 | CtVmObjQueryFamily |
| 0x27 | C0/662C | branch if object's $0F00 = 0 | NONE / NONE | 21: 0B0 111 112 170 171 175 1BE | CtVmObjBranchFamily |
| 0x28 | C0/6655 | branch on the object's screen box | NONE / NONE | 6: 16B | CtVmObjBranchFamily |
| 0x30 0x31 | C0/66B2 / 66BC | branch on $00F8 bit 1 / bit 7 | NONE / NONE | 6 + 3: 10C 111 139 13A | CtVmPadBranchFamily |
| 0x3B 0x3C | C0/6719 / 6724 | branch on latch $0150, TRB | NONE / NONE | 0 | CtVmPadBranchFamily |
| 0x34-0x39 0x3F-0x44 0x47 | C0/66C6..6788 | pad branches, drain line | paraphrases (equal) / 0x47 only | -- | CtVmPadBranchFamily (one body) |
| 0x29 | C0/3E84 | staff-roll caption: font, $C800 fill, $0BE9, $0BE1 | NONE / NONE | 68 (64), 37 scenes | CtVmCaptionFamily |
| 0x2A 0x2B 0x32 | C0/3ED6 / 3EDE / 3EE2 | $0154 \|= 04 / 08 / 10, CLC | yield, NO effect / NONE | 87 + 54 + 115 | CtVmCaptionFamily |
| 0x2C | C0/3EE6 | $1D8F/$1D91 = op*8, CLC | yield, NO effect / NONE | 12 | CtVmCaptionFamily |
| 0x2F | C0/3F12 | $0BE3/$0BE4 | NONE / NONE | 102 | CtVmCaptionFamily |
| 0x33 | C0/3711 | $0154 \|= 80, $0F81, 2x24 palette bytes from $E4 | NONE / NONE | 80 (76): 026 050 1D0 | CtVmPalOp33 |
| 0x7F | C0/6D09 | local := $FE00[++$F8] | ok (paraphrase) / NONE | 25 (11): 005 0E6 1B2 ... | CtVmRandOp7F |
| 0xE6 0xF4 | C0/3DF4 / 3D84 | layer drift / auto-scroll | ok (paraphrase) / NONE | 306 + 127 (1) | CtVmScrollFamily |
| 0x0B | C0/6269 | $1000 \|= 80; CLC when the target is ITSELF | no yield / C field only, no $1000 | 526 (409); self 290 | CtVmObjEnableFamily + yield |
| 0x0A 0x0C | C0/6254 / 6282 | kill ($1100, $1A81) / unpause | ok / C field only (0x0A no $1A81, 0x0C no $1000) | 843 (607) / 194 (5) | CtVmObjEnableFamily |
| 0x89 0x8A | C0/4B49 / 4B58 | $1A00,X | C field `mspd` only (no $1A00) | 2779 / 4 | CtVmSpeedFamily |
| 0xB8 | C0/3557 | $012B-$012D | ok / NONE (captureTable's C copy only) | 324 (324) | init: the three stores |
| 0xC7 0xC9-0xCE | C0/37xx-38xx | item / gold family | ok / NONE | C9 24 (12), CB 55 (1) | init: CtVmItemFamily |
| 0xCF 0xD0 0xD1 0xD3 0xD4 0xD7 0xF8-0xFA | C0/388D.. | party gate family | ok / NONE | D3 127 (34), D4 127 (26), CF 44 (15) | init: CtVmPartyGateFamily |
| 0xE4 | C0/3D97 | tile copy | ok / NONE | 645 (2): 08D 0C6 | init: CtVmTileCopyE4 + the host's copy |

Checked and EQUAL (no action): 0x65/0x66 (the live `1 << (bit & 7)` vs the ROM's $FF20/$FF28 tables -- every shipped
operand is 0-7, where they agree), 0x4E (every site's bank byte is $7E), 0x12-0x16 (every shipped mode is below the
range where the ROM's wide compare differs), 0x8B/0x8C/0x8D/0x8E, 0x87, 0x84, 0x0D/0x0E, the loaders, 0x92/0x9C/0x9D
(moveOp), and the families already shared (poke, var grid, pointer read, arithmetic, local INC, anim, facing, colour,
items, party gate in step, screen fx, 7A, E4/E5 in step).

Left as they are, for the record: 0x21/0x22 read the C position (`fx`/`fy`) where the ROM reads $1801/$1881 -- its
comment explains why (the mover integrates `fx`); 0xD2/0xD3 in step are hand-derived arms with their own pins.

## 3. What the ROM shows these gaps cost (and what was measured)

- Loc 0E6 (the attract's robots): objs 18-22's fn0 is `7F 06 / 12 06 80 03 06 / 83 6A 03 / 10 04 / 83 69 03`. The
  cartridge under the rule (`--monitor --from boot`): at f7514-7515 $01F8 32 -> 37, local[6] E9 .. 2F, the five
  enemy ids $1125.. = 69 6A 69 69 6A = $FE00[33..37] (E9 70 DC CD 2F) through the `>= 80` test. The port loaded 6A
  for all five and its RNG cursor never moved at the load. Now it draws $FE00[2B..2F] from its own cursor
  (ids 69 69 69 6A 6A): the op is exact, the CURSOR is not -- it drifts from the boot (cartridge $01F8 = 11 at f50,
  the port 00; 0E6's load itself draws 32 before the init on the cartridge, 34 on the port). Open on its own.
  tools/attract_0e6_init_draws_cmp.py, check attract-0e6-init-draws-the-robots-from-the-rom-table-vs-rule-cartridge.
- Loc 0E2 (storyline 0x42) fn0 rebuilds the party (`D4 03 / D2 01 05 / D4 01 / D3 01 / D3 02`): the init pass did
  none of it, so the PC loaders after it resolved against the old party.
- Loc 1B2 (the tent) obj9 fn0 `20 0F / 19 0F / 1A nn ..`: the clone takes the LEAD's sprite (Crono reads the same by
  luck); `C9 E2 03` picks its arm by an item.
- fn0's `0B <self>` (245 static sites) never set $1000 bit 7, the byte C0/5AD1 and the queue build test.
- Ops 0x29/0x2A/0x2B/0x2C/0x2F/0x32 are the endings' staff roll. Their consumer is the main loop's C0/274D (via C0/00B4:
  bits 2/3 -> `TSB $46` + C0/75A0, bit 4 -> C0/278F/27EB, the caption line into $7E:D800 and its VRAM address $0BE5),
  which the port does not have: the VM side is ported here, the roll itself is not (no capture reaches an ending).
  PORTED 2026-10-08 (field_caption_plane.md): C0/274D's bits 2/3/4, the NMI's EB46-EB77 arms, the load's $1D7C-$1D86
  and FD/C124's BG3SC/TM bytes, BG3 drawn from the VRAM image; a capture of the roll is `--teleport 91` (scene 05B,
  fn0 `29 80`, no ending flag needed), check field-caption-plane-staff-roll-vs-rule-cartridge.

## 4. Side effects of the port

- `ct.exe --vm` (the static resolve, no ROM): the init pass now runs fn0's `CF` / `C9`, which jump when the character /
  item is absent -- five objects take their hidden arm (scenes 84, 85, 90, 223, 436). assets/script.py mirrors both
  ops, the rebuilt ct_assets.dat differs in exactly those five actor tables, and `vm-512-scene-actors` reads 1896.
- The shared bodies met a no-ROM run for the first time through the init pass: op 0x29's record walk over $FD:A000
  is bounded by the ROM length, and the party gate's C2/F958 / C2/8C95 table reads are skipped with no ROM.
- Op 0x24 no longer passes ct_vm.c's traced slot resolver (`field-party-slot-table` keeps 0x22/0xA9): it runs the
  same ctVmPartySlotTarget inside move.c and stores the object's $1600 byte.
