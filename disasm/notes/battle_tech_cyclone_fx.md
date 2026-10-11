# CYCLONE's TECH GRAPHIC EFFECT — the SCREEN DARKEN + the VICTIM BLINK

User report (translated from Portuguese): *"in the original game, when doing the cyclone the screen gets darker and the enemies hit keep
flashing."* Both halves derived from the LakeSnes **oracle** only (no ROM disasm was needed: the mechanism
turned out to be the one `battle_song_spotlight.md` already disassembled, and this pass re-measures it
independently for Cyclone and pins its own curve). Nothing here is eyeballed — every number is a CGRAM sum
or an OAM palette field read frame by frame.

**Base states.** GATO: `gato_press_action.state` + `$2837 |= 0x80` (the canonical Cyclone-learned injection,
`battle_tech_slice.md`), driver `--btnscript "100:8,0:372,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:<f-424>"`;
`$A013` 0→1 at **f438** = rel 0. FOREST: `saves/battle_re/forest_holdleft_pre.state` + the same patch,
driver `"40:60,0:275,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:<tail>"`; dispatch **f388**.

---

## 0 — HEADLINE

1. **The darken is a CGRAM half-fade of BG palettes 1–7 — the SAME effect the GATO song uses.** Comparing a
   bright frame (rel 12) with a plateau frame (rel 172): BG palettes 1–7 (`CGRAM $10–$7F`) fall to
   **0.473–0.485** of their RGB with 15–16 of their 16 entries changed each, while **BG palette 0 and every
   OBJ palette 8–15 are byte-identical**. So the arena BGs dim and **every sprite (the combatants, the whirl
   blades) plus the HUD keep full brightness** — exactly `battle_song_spotlight.md` §B.2's law (it measured
   0.476 for the song), i.e. the same bank-C3/FD tech graphic-effect subsystem.
2. **The victims blink pal1 ↔ pal4** — the same white swap the hurt flash uses — **4 frames on / 5 off from
   rel 74**, then a single **sustained 9-frame flash at rel 206–214**.
3. **The blink covers exactly the HIT SET.** In the forest cast the two in-radius Hetakes flash while the
   third, outside Cyclone's radius, never does — and its phase matches the GATO cast frame for frame, so the
   law is arena-independent.

---

## 1 — THE DARKEN

### 1.1 Mechanism (CGRAM diff, bright rel 12 vs dark rel 172)

`cycpal.py` (scratchpad) peeks both frames and sums each palette's RGB:

| palette | kind | Σ(bright) | Σ(dark) | ratio | entries changed |
|---|---|---|---|---|---|
| p0 | BG | 506 | 506 | **1.000** | 0 |
| p1 | BG | 449 | 214 | **0.477** | 16 |
| p2 | BG | 690 | 331 | **0.480** | 16 |
| p3 | BG | 643 | 312 | **0.485** | 16 |
| p4 | BG | 608 | 293 | **0.482** | 16 |
| p5 | BG | 555 | 269 | **0.485** | 15 |
| p6 | BG | 788 | 382 | **0.485** | 16 |
| p7 | BG | 562 | 266 | **0.473** | 16 |
| p8–p15 | OBJ | — | — | **1.000** (p10/11/13/14/15: 0 changed) | — |

(p8/p9/p12 do change — those are the effect/streamed OBJ palettes: p12 is the whirl's own shimmer, whose
entries are rewritten by the effect, not faded. Sample: BG pal1 entry 0 goes `0240 → 0120`, `08E9 → 0464`,
`4A78 → 252C` — a clean halving; OBJ pal1 `0240 08A5 7FFF 5F5A …` is **identical** in both frames.)

### 1.2 The curve (CGRAM BG1–7 sum ratio, sampled per frame)

Measured with `cyccurve.py` — the **CGRAM** sum, not an fb mean: the fb mean is contaminated by the moving
sprites and the bright whirl blades (it dips an extra ~5 units when the blades leave), so it cannot be used
to fit the fade.

| rel | 32 | 36 | **37** | **38** | 39 | 40 | 44 | 48 | 52 | 56 | 60 | 64 | **68** | 72 | 76 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ratio | 1.000 | 1.000 | **1.000** | **0.923** | 0.923 | 0.896 | 0.836 | 0.772 | 0.704 | 0.648 | 0.586 | 0.524 | **0.481** | 0.481 | 0.481 |

| rel | 196 | 200 | **204** | 208 | 212 | 216 | 220 | 224 | 228 | 232 | **236** | 240 | 244 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ratio | 0.481 | 0.481 | **0.481** | 0.527 | 0.595 | 0.654 | 0.723 | 0.778 | 0.845 | 0.901 | **1.000** | 1.000 | 1.000 |

So: **unfaded through rel 37 → a first step to 0.923 at rel 38 → a ramp to 0.481 by rel 68 → that plateau
to rel 204 → a mirror ramp back to 1.000 by rel 236.** The ROM's cross-fade is *stepped* (it holds each
value ~2 frames, e.g. rel 38/39 and 40/41 read the same); a linear ramp between the measured endpoints fits
to within ±0.017 (worst case rel 232: model 0.932 vs measured 0.901).

---

## 2 — THE VICTIM BLINK

> **2026-09-17 -- DERIVED, the curve below is retired.** The blink is the CE/0909 script's anim op **6C**
> (`C1/5F13`, `6C 00 04`) on the victim track 3 and the `$A018` list track 4: per combatant a countdown `$A17C`
> reloaded to 4 and an ON bit `$A187` flipped at 0 -- ON = op 69's body (`C1/5DDC`, free: the reload is counted
> down in the same frame), OFF = op 6A's (`C1/5E8B`) -- while the script's sync store `$5D80` is non-zero; t0's
> `30 00 00` after its eight slides clears it and op 6C exits (restore, advance 3, the frame spent). Oracle
> (gato_cyclone.state): ON f511 with `$A17C = 03`, OFF + reload f515, ON f520 ... last ON f637, restore f638.
> The final sustained flash is the victim tracks' own `69 00 / .. / 6A`. The picture shows a combatant's palette
> attribute two frames after the script. Checks: battle-anim-op6c-cyclone-blink-vs-oracle (29 toggles, 28/28
> intervals, exit +1), field-battle-flash-attribute-uploaded-two-frames-late. See battle_fx_sprites.md §7.

### 2.1 Waveform (OAM shadow `$7E0700 + oidx*4 + 3`, palette = bits 1–3, monitored EVERY frame)

The victim's 12 GATO body cells are OAM indices **12–23** (identified by peeking a flash frame and a normal
frame and matching tiles 0x68–0x8E); `--monitor "0733:1"` therefore reads oidx 12's attribute byte for the
whole span in one pass. ⚠ oidx 8's attr byte is NOT the victim — the whirl effect object takes that slot
over for exactly rel 59..200, which is a good independent confirmation of the FX window.

```
rel   0.. 73  pal 1     (idle / approach)
rel  74.. 77  pal 4  (4 f)      <- the blink starts on the frame the SPIN begins
rel  78.. 82  pal 1  (5 f)
rel  83.. 86  pal 4  (4 f)      ... the 4-on/5-off period-9 cycle repeats EXACTLY,
   ...                              every cycle, with no drift:
rel 191..194  pal 4  (4 f)
rel 195..199  pal 1  (5 f)
rel 200..200  pal 4  (1 f)      <- the last cycle is CUT here (the effect ends), not completed
rel 201..205  pal 1  (5 f)
rel 206..214  pal 4  (9 f)      <- a single SUSTAINED final flash
rel 215..320  pal 1
```
Law: flash iff `rel ∈ [74,200] && ((rel-74) % 9) < 4`, plus `rel ∈ [206,214]`.

### 2.2 It is the HIT SET, not the primary (forest cast, dispatch f388)

Peeking the forest cast and counting OBJ palettes:

| rel | palettes present | pal4 sprite positions |
|---|---|---|
| 76 | `{0:4, 1:4, 4:8, 5:4, 6:4}` | (72,67)(88,67)(73,83)(89,83) = **Hetake slot1** + (104,35)(120,35)(105,51)(121,51) = **Hetake slot0** |
| 80 | `{0:4, 1:12, 5:4, 6:4}` | — |
| 85 | `{0:2, 1:4, 4:8, 5:4, 6:4}` | the same 8 |
| 90 | `{0:4, 1:12, 5:3, 6:4}` | — |

**Eight** sprites flash = the two enemies Cyclone's radius selected (`battle_multitarget.md`: primary slot0 +
slot1 in radius, slot2 out); the remaining 4 pal1 sprites are slot2, which **never** flashes. And the phase
is identical to the GATO cast: rel 76/85 flash, rel 80/90 normal — `(76−74)%9 = 2 < 4` ✓, `(80−74)%9 = 6` ✗,
`(85−74)%9 = 2` ✓, `(90−74)%9 = 7` ✗.

---

## 3 — WHAT OUR ENGINE DID INSTEAD

Neither half existed. `battleWhirlRender` drew the blades and nothing else; `battleEnemyFlashing`
(`src/field.c`) gated the pal4 swap on the single `h->flashTarget`, which a tech never sets (the tech path
skips the strike branch entirely), so no enemy ever flashed during Cyclone; and no darken pass ran for any
tech but the song (`battleSongFxRender`, gated on `mem[$9877] == 0x10`).

---

## 4 — IMPLEMENTATION (shipped)

* **`src/battle.h`** — the measured constants (`CT_CYC_DARK_IN0/IN_K/HOLD/K/OUT0/END`,
  `CT_CYC_BLINK0/_ON/_PERIOD/_END/_FIN0/_FIN1`) plus two published fields: `fxTechDarkK` (1/1000 of full
  brightness) and `fxTechFlashMask` (a per-combatant bitmask).
* **`src/battle.c`** (`battleAdvanceAction`) owns both curves — it must advance **every** frame, not only on
  the frames a shot happens to compose — and sets the mask from `actTgtList[0..actTgtN)`, i.e. the hit set
  the radius scan already built. Gated per-tech (`actTechId == CT_TECH_CYCLONE`) exactly like the whirl FX;
  the other techs' presentations are their own scripts' business and are not measured.
* **`src/field.c`** — `battleCycloneDarkenRender` scales the fb by `fxTechDarkK` and is composed **first**
  (beside `battleSongFxRender`) in both the shot path and the interactive path, so only the BG composite
  dims and every sprite/HUD drawn afterwards stays full-bright, matching §1.1's palette scope.
  `battleEnemyFlashing` now also consults the mask, so every enemy layer honours it with one gate.

**Validated** — `techfx` signature (forest): `rel=38 darkK=923` / `rel=68 darkK=481` /
`rel=74 darkK=481 blink=018` (bits 3+4 = combatants 3+4 = Hetake slots 0+1, **not** slot 2) /
`rel=206 darkK=497 blink=018` / `rel=236 darkK=1000`; GATO gives the same curve with `blink=040`
(combatant 6). Renders confirm it: at forest rel 76 the two in-radius Hetakes are solid white and the third
is untouched; at GATO rel 75 GATO is a white silhouette. `battle-multi` is byte-identical — the FX touched
no hit set, damage or span. Pinned as `field-battle-tech-cyclone-fx`; re-pinned
`field-battle-tech-cyclone-whirl-asmb-flip` (its f325 is rel 75 = a blink frame).

---

## 5 — OPEN

| # | item | what would close it |
|---|---|---|
| F1 | **The ROM side of the fade.** Not disassembled here — the CGRAM evidence plus `battle_song_spotlight.md` §B.3 (the C3/5B69 `STA $2131` / C3/6733 `STA $2132` register sites, the `$7E2020` full-brightness source palette) already identify the subsystem; the exact per-frame step table (the fade holds each value ~2 frames) was not read out of ROM. | trace the C3/FD effect entry the `80 02 F0` op calls (`CD/001B`, the standing T3/C5 boundary) |
| F2 | **Whether the plateau is a true half or a table.** 0.473–0.485 across palettes is consistent with a ÷2 plus rounding, but the per-entry rounding rule was not derived. | diff a single palette entry's bright/dark pairs against a ÷2 model |
| F3 | **Other techs.** Only Cyclone (and, previously, the song) is measured. Ice draws its crystals with no darken in the captures we have, but its curve was not sampled. | run §1.2's `cyccurve.py` on the Ice cast |
| F4 | **The HUD strip.** The oracle's BG1 command frame darkens with the BG palettes; our HUD is a monolithic byte-pinned bake with no BG1-vs-readout split, so it stays bright — the same documented residual the song pass carries. | split the HUD bake into chrome vs read-out |
