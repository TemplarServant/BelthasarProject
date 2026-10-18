# FIELD — the composite's vertical origin is `camy + 1`: the whole picture was one scanline low

Found by the **render half** of the scene-254 A/B — the lockstep compares the event VM, and nobody had ever
compared the *pixels* against the oracle at matched moments.

---

## 1. The rule

The SNES's `BGnVOFS` is **not** the source row shown at the top of the screen. The PPU renders scanlines
counting from **1**, and fetches BG row `y + VOFS` for scanline `y` — so the first visible scanline shows
BG row `VOFS + 1`. Our compositor sampled `f.base[(camy + y) * W + camx]`, i.e. it put source row `camy` on
screen row 0.

**The whole field picture — map and sprites, which share that origin — therefore sat exactly one scanline
too low, in every scene, since the first render.**

## 2. Measured against the oracle

A **static** room is the honest fixture (nothing moving but the player's own animation): loc 26,
`build/ct.exe --peek build/chest26.state ora.raw --btnscript "0:2"` against
`build/Belthasar.exe --scene 26 --at 488 623 --selftest ours.raw --frames 40`, comparing the oracle's image
rows 8..231 (its 512×480 buffer holds the 224 visible lines starting at row 8) with ours, over 200 rows ×
256 px:

| shift | before the fix | after |
|---|---|---|
| `dy = 0` | **21812 / 51200 (42.6%)** | **445 / 51200 (0.87%)** |
| `dy = +1` | 445 (0.87%) | 21731 (42.4%) |
| `dy = −1` | 24700 (48.5%) | — |

An exact mirror. Per row, the pure-map rows 10..25 and the player's body rows 143..165 go to **diff 0**,
not "close" — and the 0.87% that remains is the player's own animation phase, the only thing moving in that
room.

The **battle screen shares the field compositor** (the arena is the field BG under the battle camera, then
the combatants on top), so it moved with it. Verified the same way against `saves/gato_inbattle.state`,
comparing the background band above the combatants:

```
battle bg band dy=-1 -> diff  5733 /  9984 (57.4%)
battle bg band dy=+0 -> diff     1 / 10240 ( 0.0%)     <-- after the fix
battle bg band dy=+1 -> diff  5906 / 10240 (57.7%)
```

One pixel in ten thousand.

## 3. The fix

`camy += 1` immediately after the camera clamp, so every consumer of the composite origin — the map
memcpy, the sprite screen-Y, the stencil read-back, the iris anchor — moves together. `lastCamY` keeps the
ROM's raw `$1D0E`, because that is what the textbox speaker rule reads.

⚠ **The sprite pass's own `+1` is a different rule and stays.** `field.c`'s `py2 = ay - camy - CT_SPR_OY +
y + 1` is "an OBJ renders one scanline below its OAM Y" (`ct-field-sprite-pos`, commit cf9914f), derived
separately and pinned by the OAM checks. The two `+1`s compose: an OBJ at OAM Y = N lands on screen row
N + 1, and screen row R shows BG source row `camy + R + 1`.

⚠ **Two report lines had to change with it**, because they printed the composite origin as if it were the
camera: `selftest: … cam(x,y)` and `fieldshot: … camy=`. Both now print `lastCamY`. Without that the whole
re-pin wave would have baked `camy + 1` into 22 signatures and made the *reported* camera wrong forever —
`field-battle-forest-seek-glide` caught it by refusing to re-pin (`camy=512` → `camy=513`).

## 4. Blast radius

**22 checks re-pinned**, every one of them a field or battle render fnv, every one for this single reason:
`camera-reenter-scene5`, `field-battle-{bat9-return, endteardown-mode6, forest-camera-composite,
forest-return, forest-seek-glide}`, `field-battlehud-gato`, `field-chest-{already-open-is-empty,
preopened-at-load}`, `field-op4{1,2}-poll-{x,y}`, `field-scene{129,157,2,200,240,4,434,5}-*`,
`render-scene{1,5,6}-*`. No check's SUBJECT changed — the pixels moved up one row and the semantics they
grade (a door bumped, a chest opened, an occluder's order, a decision box's option page) are untouched.

## 5. OPEN — the rest of the render A/B

* **The ROM blacks out the bottom 13 scanlines** and we paint map there: the oracle's own frame has content
  in image rows 8..218, ours fills to 231. Not derived — the report's `$4200` IRQ mechanism was refuted by
  its own verifier (bits 5-4 = 00 = IRQ disabled), and the cut is mid-scanline (crop row 210 is `181010`
  on both sides). Probe: `--hookpc 00ECCC --frames 20` to count reaches per frame, and `C0/EA7A LDA #A1`
  for where VTIME 211 is armed.
* **A 13-line gutter, a wedge (op 0xFE / `C0/3F26`), a `$7E:21BA → $7E:211A` copy with no identified
  writer, and an anim-phase law that the verifier refuted** — all still open, each with its probe named in
  the campaign report.
* **The textbox text baseline** is one row high (`field.c:1279 +9 → +10`; the oracle's first glyph row is
  141, ours 140, glyph for glyph). Independent of this fix — it is inside the window, not the composite.
