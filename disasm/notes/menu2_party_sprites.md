# menu2_party_sprites — THE MENU'S STANDING SPRITE IS THE CHARACTER'S OWN FIELD SPRITE

**Question that started it:** "extract the art of the other 5 characters." The menu could only draw Crono
and Marle, because the port carried two RGBA bitmaps captured out of oracle OBJ VRAM
(`src/data/menu_partyport.h`) and nothing for Lucca, Robo, Frog, Ayla or Magus — so an exchange
involving any of them showed an empty panel.

**The answer is that nothing needed extracting.** The menu draws the character's FIELD sprite, and this
engine already carries all seven of them.

---

## 1. VERDICT

For every one of the seven characters, the tiles the ROM's menu uploads to OBJ VRAM decode to **the same
colour index at every pixel** as `CT_ASSET_SPRITE[charId]` composed by the engine's own
`CtComposeSprite`, and the menu's OBJ palette is **the sprite asset's own palette, byte for byte** over
the twelve indices those frames use.

So the two baked bitmaps are **deleted**. The host composes the frame and hands it to the menu in
`CtMenuHost.partySpr[charId]`, the way it already hands over the portrait and the hand cursor.

Three things had to be measured to make that work: **which frame**, **where it goes**, and **when it is
dimmed**.

---

## 2. WHICH FRAME

Decode a panel's sprite out of a `--peek` capture (VRAM `$4000` word base, 4bpp, that panel's OAM
entries), normalise its opaque mask, then match it against **every** composed frame of every sprite
asset. The match is unique per character:

| charId | character | sprite asset | standing frame | `SPRITEANIM[3]` |
|---|---|---|---|---|
| 0 | Crono | 0 | **0** | 0 |
| 1 | Marle | 1 | **1** | 1 |
| 2 | Lucca | 2 | **1** | 1 |
| 3 | Robo | 3 | **0** | 0 |
| 4 | Frog | 4 | **1** | 1 |
| 5 | Ayla | 5 | **2** | 2 |
| 6 | Magus | 6 | **0** | 0 |

`CT_ASSET_SPRITEANIM[charId]` byte 3 predicts all seven, so the host reads that table rather than
carrying a list of constants. (Of the four bytes in that record, byte 3 is the *only* one that fits:
byte 1 gives 1,1,1,1 where the measurement says 0,1,1,1; byte 2 gives 3,0,0,0; byte 4 gives 1,2,2,2.)

**The pixel-level proof, not just the mask:** comparing the decoded OBJ pixels against the composed
frame index by index — `327/327` for Lucca and `286/286` for Frog on `_av_form1`, `414/414` Crono and
`300/300` Marle on the Tech capture. Every pixel, the same palette index.

⚠ **THE CURSOR MEMBER IS A DIFFERENT, ANIMATED POSE.** `$7E:1811 + charId*0x40` is 0 idle, 1 for the
cursor's member, `0x0B` for the picked one (C2/C973), and each selects different art: Lucca showed frame
**14** in one capture and **12** in another *at the same pose byte*, Frog 12, Robo 6, Ayla 13, and the
picked Lucca 74. So each character must be measured **while off the cursor**, which is why two different
party orderings were captured. That pose animation is **not ported** — the port draws the idle frame on
every panel.

---

## 3. WHERE IT GOES

Anchored by the sprite's own **opaque box** against two numbers per screen — a **centre x** and a **feet
y** — because the ROM's per-character frame SHAPE varies (Crono and Marle assemble as a 16×64 column of
four 16×16 sprites, Lucca/Robo/Ayla/Magus as a 2×2, and Frog's is an irregular hop) and that assembler,
**C2/F657** over the `$7E:1800` records, is not ported.

* **Form** — `centre x 168`, `feet y 56 + 48*panel`, straight from ROM table `C2/CE6E` at the X-menu
  index `$12` (see `menu2_window_gradient.md` §6).
* **Tech** — `centre x 28`, same feet line. Measured, not from a table.

Every placement the port computes, against the oracle box measured for it:

| character | screen · panel | oracle ink box | port |
|---|---|---|---|
| Crono | Form 0 | `160..175, 21..55` | same |
| Lucca | Form 1 | `160..175, 72..103` | same |
| Robo | Form 1 | `156..179, 72..103` | same |
| Ayla | Form 1 | `161..175, 72..103` | same |
| Frog | Form 2 | `160..175, 128..151` | same |
| Magus | Form 2 | `159..176, 120..151` | same |
| Crono | Tech 0 | `20..35, 21..55` | same |
| Marle | Tech 1 | `22..34, 71..103` | same |

**8/8.** Pinned by `menu-party-sprites-all-seven` (`--sprtest`, 4 mutations, all killed).

---

## 4. WHEN IT IS DIMMED — a controlled experiment, not a correlation

On `_av_form1`, Crono's OBJ palette is the asset palette **halved in 5-bit space at all twelve indices
(12/12)** while Lucca's and Frog's are word-identical to the undimmed capture. Crono is also the locked
member on that state (`$7F:01DF = 0x80`) — but *one* witness cannot tell "locked" from "panel 0" or from
"Crono".

So it was tested. Same party (Robo/Ayla/Magus), same capture script, **only `$7F:01DF` differs**:

```
lock = 0x04  (FF/F9BB[5] -- AYLA)      objpal 4 Robo  : equal 12/12, halved  0/12
                                       objpal 6 Ayla  : equal  0/12, halved 12/12   <-- follows the LOCK
                                       objpal 7 Magus : equal 12/12, halved  0/12

lock = 0x80  (Crono, who is not in this party)
                                       objpal 4 Robo  : equal 12/12, halved  0/12
                                       objpal 6 Ayla  : equal 12/12, halved  0/12   <-- negative control
                                       objpal 7 Magus : equal 12/12, halved  0/12
```

The halving follows the locked **character**, not a panel position and not Crono. It is the same
half-math the rest of the menu dims with.

---

## 5. GETTING THE MISSING FIVE IN FRONT OF THE ORACLE

No state in `saves/` has Robo, Ayla or Magus in the active party. They were **fabricated**, the same way
`saves/tech_double.state` was: `ctlib.lake.read_payload` → patch the WRAM inside the payload → 
`write_wrapped`. Patching `$7E:2980..2982` alone was enough — the ROM did the rest, and `--peek` reports
`$7E:0067 = 05` with all three drawn.

```
saves/party_robo_ayla_magus.state   $2980 = 03 05 06   (Robo p0, Ayla p1, Magus p2)
saves/party_ayla_robo_magus.state   $2980 = 05 03 06   (Robo off the cursor, for his IDLE frame)
saves/lock_ayla.state               $2980 = 03 05 06, $7F01DF = 0x04   (the §4 experiment)
```

`tools/_gen_menusprstates.py` rebuilds all three.

---

## 6. WHAT IS STILL NOT DERIVED

* **`C2/F657`, the menu OAM assembler**, and the per-character frame SHAPE it produces. The port sees
  the output and anchors by the opaque box instead. This is why the anchor is an ink box rather than a
  frame origin.
* **The pose animation** (`$1811` = 1 for the cursor member, `0x0B` for the picked one). The port draws
  the idle frame everywhere.
* **Where the CHR is DMA'd from in ROM.** Not traced — and not needed: the port never reads menu VRAM,
  it composes the same sprite asset the field draws, which the asset pipeline extracted from the ROM.
  The equality above is what ties the two together.

## The menu sprite animator (C2/F643, C2/F657, C2/F69B)

C2/F643 spawns one task in slot 0x28 (`LDA #F657 / LDX #0028 / JSR C2/8249`). C2/F657 is its body: it sets
D = $1800 and walks the nine sprite blocks, D += 0x40 until D == $1A40, once per frame.

```
2F662  LDA $00 / BMI ->next            ; +0x00 = charId, bit7 = no character in this block
2F666  LDA $18 / BMI ->next            ; +0x18 bit7 = block disabled
2F66A  LDA $11 / CMP $12 / BNE ->adv   ; +0x11 = requested anim, +0x12 = the one playing
2F670  CMP #00 / BEQ ->draw            ; both 0 -> this block is not animating
2F674  DEC $13 / BPL ->draw            ; +0x13 = the frame countdown (signed test)
2F678  LDA $0D73 / CMP $0D75 / BCS ->draw ; a per-frame budget on how many blocks may advance
2F680  JSR C2/F69B                     ; advance
2F683  LDA $18 / BNE ->next / JSR C2/F871   ; emit this block's OAM
```

C2/F69B is the advance:

```
2F69B  INC $16                         ; +0x16 = the frame index inside the animation
2F69D  LDA $11 / CMP $12 / STA $12     ; commit the request; Z = "it did not change"
2F6A3  BEQ ->2F6F7                     ;   unchanged -> keep walking the same table
2F6A5  STZ $16                         ;   changed -> restart at frame 0 and re-point the tables
2F6A7  LDA $17 / ASL A / TAX / LDA $14 / JSR ($F6D8,X)   ; kind -> [0, v, 2v, 3v] over +0x14
2F6B4  LDA $11 / AND #007F / ASL A / ASL A / STA $1E     ; animId * 4
2F6BD  ADC $1B / ADC $0A / STA $1B     ; pose table     = resolve(+0x14) + animId*4 + word(+0x0A)
2F6C3  LDA $1E / CLC / ADC $0C / STA $1E ; duration table =             animId*4 + word(+0x0C)
2F6F7  LDY $16 / LDA [$1E],Y           ; the frame's duration
2F6FB  BEQ ->2F6E0                     ;   0 terminates: bit7 of $11 -> restart at +0x21, else
                                       ;   restart at 0; if already 0 the animation is empty
2F6FD  STA $13                         ; reload the countdown
2F6FF  LDA [$1B],Y / CMP $03 / BEQ ->2F750 / STA $03     ; +0x03 = the assembly frame; the DMA that
                                       ;   follows uploads that frame's tiles
2F750  SEP #30 / DEC $13 / RTS         ; both exits decrement again, so a pose lasts `duration`
                                       ;   frames, not duration+1
```

The tables are the character's ordinary sprite animation set (CT_ASSET_SPRITEANIM carries them byte for byte). In the
oracle's live blocks Lucca's pointers resolve to $E4:39A0 (poses) and $E4:AA24 (durations), whose bytes are
`0A 0B 0C 0D 0E 0F` / `07 07 07 07 07 07`: CT_ASSET_SPRITEANIM[2] anim 1's D-facing column and its delays. The same
holds for Crono (E4/312C = 04 05 06 07 08 09), Frog (E4/427C) and anim 0x0B (Lucca E4/39C8 = 01 87 4A /
E4/AA4C = 0A 05 0F). The menu plays the character's walk cycle facing down, so block+0x03 is the assembly-frame index
the field sprite composer takes, and anim 0 frame 0 is the standing frame.

Blocks 0-6 are the characters' own ($1800 + 0x40 * charId, C2/F626). Block 7 ($19C0) is a copy whose character is set
at run time: the Tech screen's caster (C2/BA09: `LDA $9A90 / ... PEA $19C0 / PLD / CMP $00 / BEQ / STA $00 / JSR
C2/F422`). Block +0x21 is the loop point: an animation whose request has bit 7 (C2/B4E6's $19D1 = 90) restarts there at
its end (2F6E0 `LDA $11 / BPL / LDA $21 / STA $16`) instead of at frame 0.

In the port the machine runs in menu.c (g_mspr) instead of at $7E:1800, because the menu runs over a live field WRAM
whose $1800 array is the field's per-object table (CtObjSceneDefaults writes $1800,X) and must resume unchanged. The
VRAM DMA at 2F707 is replaced by composing the frame on the host.
