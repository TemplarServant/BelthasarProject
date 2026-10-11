# The overworld picture started one scanline too low (2026-09-17)

Survey item: every overworld leg of the attract carried a per-pixel difference against the cartridge that no shift of
the CAMERA explained -- `tools/attract_survey.py` medians 10.8 (1F5), 8.5 (1F3), 6.2 (1F2 and 16E), 10.0 (1F0) with
both sides' scroll words equal. `tools/attract_bg_cmp.py` had carried an `ATTRACT_CMP_DY=-1` knob for it since the
flyover pass, documented as "a vertical phase residual common to every layer... Not explained yet"
(sequence_table.md 9.27). This is the explanation.

## 1. What the hardware does

`snes/ppu.c` -- the oracle's own renderer, and the reference for every picture this project grades -- states it in
three places:

```
snes.c:207        if(!snes->inVblank && snes->vPos > 0) ppu_runLine(snes->ppu, snes->vPos);
ppu.c:273         void ppu_runLine(Ppu* ppu, int line) {      // called for lines 1-224/239
ppu.c:277           if(!ppu->forcedBlank) ppu_evaluateSprites(ppu, line - 1);
ppu.c:281           for(int x = 0; x < 256; x++) ppu_handlePixel(ppu, x, line);
ppu.c:400             ly += ppu->bgLayer[curLayer].vScroll;    // ly came in as the SCANLINE
ppu.c:348         int row = (y - 1) + (ppu->evenFrame ? 0 : 239);   // ...which lands at row y - 1
```

So for output row `r` (0-based, what `ppu_putPixels` writes at buffer row `2r + 16`):

| layer | source line |
|---|---|
| BG1/BG2/BG3 | `vScroll + r + 1` |
| OBJ | evaluated at `r` -- an OAM y of N first appears on row N |

BG and OBJ do **not** share the offset: the scroll register is read against the scanline (1-based), the sprites
against the scanline minus one.

## 2. What the port did

`worldCompose` (src/field.c) sampled `vofs + y` for BG1/BG2 and `bg3v + y` for BG3 -- one line short -- and
`worldObjLine` then read `line = y - 1` to pull the sprites down with the map, so the two halves agreed with each
other and the whole overworld picture sat one scanline below the cartridge's. The FIELD composite already carried
the same law (`camy += 1`, measured there on loc 26, src/field.c); only the overworld never got it.

## 3. Measured, cartridge against port

* **The attract's closing 1F0 leg**, `--peek saves/attract_cena_inicial.state --frames 8834` against the port's
  `CT_SHOTS` f10860, a 41x41 shift search over the whole 256x224 frame:

  | shift | identical |
  |---|---|
  | dy = -1 | 56919 / 57344 (99.2%) |
  | dy = 0 | 21170 / 57344 (36.9%) |

  and after the fix, dy = 0 gives **56999 / 57344 (99.40%)** with no shift beating it.
  (The leg is a still picture on both sides -- consecutive thumbnails differ by 0.00 -- and the two are pixel-identical
  for its first 44 frames, while the fade-in holds the screen black.)
* **The world-5 flyover** (`tools/attract_bg_cmp.py 2240 206 2 222`): 56320 / 56320 with `ATTRACT_CMP_DY=-1`,
  22319 / 56320 without. After the fix: **56320 / 56320 unshifted**, and the knob is gone from the check.
* **The 1999 A.D. leg** (`4643 3076 0 224`), which carries OBJ (the fires) as well as map: 56267 shifted,
  55383 once only the BG moved, **56350** once `worldObjLine` took its own line back. That last step is the
  proof that BG and OBJ really do differ by one.

* **The whole attract** (`tools/attract_survey.py`, median best-match difference per visit):

  | leg | before | after |
  |---|---|---|
  | 1F5 | 10.8 | **0.0** |
  | 1F2 | 6.2 | **2.8** |
  | 1F7 | 3.3 | **0.6** |
  | 1F0 | 10.0 | **0.2** |
  | 1F3 | 8.5 | 9.9 |

  Every field leg is unchanged to the digit (01C 0.3, 01B 0.1, 0AD 4.8, 112 2.0, 0DB 0.4, 0A5 0.3, 07D 3.8,
  0E6 2.1, 087 1.8, 1B0 0.2, 16E 6.2), as it must be -- nothing outside the overworld composite moved.
  1F3 is NOT a regression of this change, and not a render defect at all -- see section 5.

## 4. The port

* src/field.c: `#define OW_BG_VLINE 1` added to the BG1/BG2 sample in `worldCompose` and to all three
  `worldBg3Px` calls; `worldObjLine`'s `int line = y - 1` -> `int line = y`.
* src/ct_world.c: the same `+ 1` in the world viewer's own compositor.
* tools/attract_bg_cmp.py: `ATTRACT_CMP_DY` stays as a measuring knob but no longer documents an open residual.

GRADED:

* attract-flyover-frame-vs-oracle -- 56320 / 56320 unshifted, one mutation (`OW_BG_VLINE 0`).
* attract-world7-colour-math-vs-oracle -- 56350 / 57344 unshifted, two mutations (the BG line and the OBJ line).
* Re-pinned, same picture one line up: ow-compose-render, ow-world1-colour-math, field-menu-overworld-resume.

## 5. Open

* The 1F0 leg's median is **0.2** now (was 10.0), and its worst sample is the white-out that closes the attract.
  That closing fade is nearly in step: `--monitor 7E0920:512 --diff --writer` has the cartridge's row-8 walk
  (writer C2:1FBB, class $1E38) running from f8826 to full white at f8845 -- leg-relative 212..231 -- and the port's
  from f10894 to f10913, leg-relative 214..233. Two frames, not more.
  ⚠ The survey pairs each cartridge sample with the BEST-matching port frame in the visit, so on a leg that holds
  a still picture the pair can be tens of frames apart and a fade LOOKS far out of step. Read a fade's phase from
  the shadow's own writes, never from a survey pair: `CT_SHOTS_PAL=1` writes the host's `$7E:0920` beside each
  `CT_SHOTS` frame for exactly that.
* **The 1F3 leg runs ONE FRAME late** (survey median 9.9, the worst overworld leg left -- and the number is
  mostly the instrument). Its camera is byte-exact: `ct.exe --monitor 7E0020:2,7E0026:2 --diff --writer` against
  the port's `CT_OWCAMTRACE` matches on **233 of 233 ticks, zero pixels apart**, once the port's tick 0 is read as
  the cartridge's leg-frame 37 (both sides hold the previous view while the world loader runs with no NMI, then
  C2/09E2/09E9 jump the camera to (720, 48) and C2/06CB walks `$26` down 4 px a frame, wrapping at 1024).
  The picture confirms it: the cartridge's f8148 matches the port's **f10213 at 99.3%**, not f10212 (18.3%).
  So the leg is one frame out of phase, which on a 4 px/frame pan is a whole 4-pixel shift.
  ⚠ The survey cannot see it: `CT_SHOTS_THUMBS`/`PEEK_THUMBS` record every SECOND frame, so an ODD offset is
  outside its search space and lands as a large per-frame difference. Measure a fast pan frame by frame at full
  resolution before believing a survey median.
* Loc 16E's one-line residual (9.23) is a FIELD scene and is not this: the field composite already adds its line.
