# THE BATTLE'S ONE CGRAM — the arena, the entry frame, and the effect's palette records

2026-09-27. The user's list, items #1 and #2 (located by the attract-divergence agent,
`build/_divergence/user_2026-09-27.md`):

* *"when the battle with flea starts in the demo, the characters vanish for 1 or more frames"*
* *"after the frog's skill ends in Magus's castle, and the ice skill on zombor, the screen is darkening for a moment"*

Both were the port keeping battle colours in HOST state (`h->fxCgram` before it was filled, one factor per palette in
`h->palK`) where the SNES has exactly one CGRAM. Graded by `attract-battle-colours-entry-and-tech-vs-oracle`
(tools/attract_battle_colour_cmp.py).

## 1. The entry frame (item #1)

The battle's NMI uploads the whole live page to CGRAM: CD/08B4 `JSL C1/0009` → CF/E3C0 (the port's `battleNmiHead`,
`memcpy(h->fxCgram, s + 0x2200)`). Until that first NMI the PPU still holds what the FIELD's last NMI (C0/712E)
uploaded — on the cartridge the PCs' rows 13-15 are unchanged from the field through c1744-c1774 of the 0AD battle, and
the boot rewrites the same words.

The port's first battle compose (the `entered` frame) read `h->fxCgram` before any battle NMI: all zero at the 0AD (the
PCs black for one frame, port f3822: Crono's box 653/1056), the PREVIOUS battle's page at the 07D (f6531: Frog in
Marle's colours) and the 087 (f9331). Now main seeds `liveHost.fxCgram` with `g_fieldCgram` on the entry frame.
After: 1055/1056.

## 2. The arena is drawn with that CGRAM (item #2, the render)

The arena is the field's BG. The field's frames bake it from the page (`CtFieldSceneSetCgram`, C0/712E's upload); the
battle's frames froze it at the field's last CGRAM, and the battle's palette records (FD/E39C, `src/objslot.c`, writing
the page word by word) reached it as ONE factor per 16-word palette (`palK`), palette 1's applied to the whole
composite — "the port's BG composite has no per-palette split", which was never true: the bake is palette-indexed.

Right for the run-off's `$05A4` (CGRAM $10-$7F) nearly; wrong for any record over PART of the BG. CD/0453's CD/0E61
record `50 10 10` covers only CGRAM $10-$1F (BG palette 1) — at the 07D that is the candle glow's ~1000 px — and the
factor blacked the whole room for 16 frames (07D f7158: 27527 / 57344 identical to the cartridge, whose picture does
not change there).

Now the battle's frames bake the arena from `liveHost.fxCgram` (main, the `battlePhase` leg beside the field's
`fieldNmi` one), and `battleFleeFxRender` keeps only the mosaic. 07D f7158: 55026 (the pre-effect residual).

The OBJ side still uses the factors (`battleFleeObjPal`, per combatant palette): exact for a record over whole OBJ
palettes, which is every one measured; not re-derived here.

## 3. CD/0E79 — the effect load's own record (was counted, never armed)

`dis.py D0EF0` (16-bit X from D0E35):

```
CD/0EF8  JSR CD/0E9D / BCC 0F42          ; no sheets ({86, 7B..7F}) -> none of this
CD/0EFD  $2A21 & 01 -> CD/0F04: JSR CD/0E79 / LDA #1F / JSR CD/3E3B (31) / $BE89 &= FD / BRA 0F2D
CD/0F16  ($2C66 | $2C67) & 04 -> CD/0F20: JSR CD/0E79 / LDA #0F / JSR CD/3E3B (15)
CD/0F28  LDA #13 / STA $BE89
```

CD/0E79 and CD/0E61 share one body: CD/0E35 (CD/0E23 restores colours 24-31 from `$CCEE`; `$55` = `$2C6B` bit 6;
`$53` = CE:EFFA[`$55`] = 50; X = `$55` × 16, + $20 with `$2A21` bit 0), `$55` set → RTS, else eight bytes from the table
+ X into `$CCDA`, `$CCD9` = `$53`, `$CCE5` |= 80 (CD/0E8F-0E99); the NMI's CD/0C4F copies the record to `$0598` the
frame after. The tables differ only in their last pair:

| | CE:EFFE (CD/0E79, the load) | CE:F006 (CD/0E61, CD/0453) |
|---|---|---|
| x = 0 | `10 10 10 00 01 00 FF 00` — fade CGRAM $10-$1F DOWN | `10 10 10 00 01 00 00 FF` — back UP |
| x = 20 | `20 30 10 00 01 00 FF 00` | `20 30 10 00 01 00 00 FF` |

MEASURED, the 07D Frog Squash (`--monitor 7EA013:1,7ECCD9:1,7ECCE5:1,7E0598:3 --from saves/attract_07d_battle_f4540.state`):
the open f80 (C1:40A9), the load's arm f93 (CD:0E94 / 0E9C), `$0598 = 50 10 10` f94 (CD:0C5D), off f109 (FD:E5D5);
the teardown's f605 / f606 / f621 (FD:E629).

The port's pre-roll counted the 15 frames (`battleActPreroll`) and armed nothing. Now `battleActLoadWait` gives the wait
(31 / 15 / 0) and the action arms `battleFxLoadPalArm` at `load − 8 − wait` — the ROM's order: the arm, the wait,
CD/0F33, CD/0F3F, the four sheets, the map's two, then the load's last frame. Port 07D: open f113, records f123-f137
(acc EF → 0F), off f138 — 10 frames after the open where the cartridge's are 13: the pre-roll's known spill residual
(CD/0015's copies, `battleActPreroll`'s comment).

## 4. `$01BB` — C0/6DCF's FF arm (why the 087 armed a record at all)

```
C0/6DCF  LDX $FE / LDA $F60002,X / CMP #FF / BEQ 6E22      ; record byte +2 = the BG3 tileset
C0/6E22  LDA #01 / STA $BB / RTS                            ; DP $0100 -> $01BB
C0/A429  LDA $01BB / BEQ / LDA $0BD7 / AND #13 / STA $0BD7  ; (and $0BD8 at A44C)
```

C0/0BE8 clears `$01BB` at the load's init (C0/0B86, before C0/00F4's legs); C0/0691 copies `$0BD7` into `$2C66` at a
battle's boot. 247 of the 512 locations have no BG3 tileset. The port never wrote the 1 (only `field_cost.c`'s copy),
so at the 087 `$0BD7` = 17 and `$2C66` = 17: bit 2 set, CD/0F16's 15 frames at the Ice Sword's load and CD/0E61's
fade-up at its teardown — the cartridge has `$2C66/$2C67` = 13 00 (c7102 `$01BB = 01` by C0:6E26, c7103 `$0BD7 = 13` by
C0:A459). Now `fieldMapHeaderStage` stores it between C0/6DCF's tile load and C0/A415's header read.

The 15 frames: every attract frame after the 087 Ice Sword's load moved −15 with its content (Crono's dash f9638 →
f9623, the crystal f9785 → f9770, the Epoch's departure f10722 → f10707 and return f10830 → f10815). **OPEN:** the port
now reaches the dash ~18 frames before the cartridge (offset 1997 at the battle's start, 1979 at the dash) — a
shortfall the wrong 15 frames hid; not tuned.

`$01BB`'s other readers (C0/0A14's BG3 assembly, the field's BG3 tilemap gate, the field TM at C0/A515) now see the
ROM's value at those 247 locations; the full suite moved nothing there.

## 5. What the suite said

Full run: the 087 crystal and the 1F0 return/departure (−15, §4); the two crit shots (`field-battle-crit-flash1`,
`-crit-beatB-dead-suppressed`) — their hash is the COMBATANTS-ONLY buffer, black under the sprites, which the
whole-arena factor had washed grey (107,107,107) while a crit's white records ran; the full frames are byte-identical
to before (the page reproduces the flash exactly); `battle-obj-priority-3-over-priority-tiles`'s arena, now in the
battle's page's colours (2465 px one 5-bit step brighter; its priority count unchanged).

## 6. The arena's SCREEN registers (the user's item #6, the same day)

*"when the frog uses the skill in the battle in Magus's castle, while the skill runs the effect on the candles' 'light' is
turning green; in the original game, apparently that effect disappears, and then comes back when the skill ends"*.

The candles' glow is BG3 on the SUB screen, added to BG1/BG2 by colour math. On the cartridge
(`saves/attract_07d_battle_f4540.state`, `--monitor 7EBE89:4,7EBE8D:4,7EBE91:4,7EBE95:4 --writer`; `--ppuregs`):

| | `$BE95` CGWSEL CGADSUB COLDATA SETINI | `--ppuregs` |
|---|---|---|
| the battle, before the tech | `02 03 E0 00` | addSub=1 TM=BG1+BG2+OBJ TS=BG3 |
| the effect load (f109 CD:0F3F clears `$BE91`; f110 on) | `00 00 E0 00` | addSub=0 (f160); f400 also BG3 on the main screen, fixed (0,18,0) |
| CD/0CF6 at the teardown (f606 CD:0D1F) | `02 03 E0 00` (f607) | addSub=1 |

CGWSEL bit 1 clear = the math takes the fixed colour, not the sub screen: the glow is simply not added through the tech.
The port had two faults:

* **`$0BDF` never staged.** C0/0691 stages `$2C6A` = 02 and `$2C6B` = `$0BDF` at a battle's boot (CD/0CF6 copies them
  to `$BE91/$BE92`), and `$0BDF` = `$0BE0` = the map header's byte 5 (C0/A40B-A412, the same byte the field bake takes as
  its effect flags). The port's `$0BDF` was 0: every battle booted with CGADSUB 00. `fieldMapHeaderStage` stores it now.
* **The arena ignored the battle's colour math.** The bake composed with the map header's CGADSUB always. Now the
  battle's frames hand it the shadow the battle's NMI ships (`$BE95` CGWSEL CGADSUB -- `CtFieldSceneSetScreen`): with
  CGWSEL bit 1 clear no sub screen is added (the fixed-colour math stays the battle compositor's). The LAYER
  designations stay the field's: mid-tech the battle's TM puts BG3 on the main screen, but that BG3 is the effect's
  (CtBattleFxLayerCompose draws it) -- taking TM drew the field's glow tiles solid (the Frog Squash box lost 45 px).
  And only once the battle's NMI has shipped them (`liveHost.frames > 0`): its first frame still shows the field's
  registers, as its CGRAM (§1) -- taking them there cost the 0AD entry's PCs box 1055 -> 778. The field's frames go back
  to the header's.

After: 07D mid-tech (port f6718 / c4700) 56282 → 57228 / 57344, (f6818 / c4800) 57116, the effect's start (f6661 /
c4640) 57344; before the tech (f6651 / c4630) 57144; after the teardown unchanged (the pre-existing ~2300 px); the
Frog Squash's tint box (attract-07d-frog-squash-colours) 11424 -> 12062 / 12320 (the glow was inside it).
