# BATTLE DISSOLVE — the enemy-death "disappearing" animation (byte-exact) + a 2nd real battle scenario (Guardia Forest)

Two targets, both derived ROM/oracle-only (no invention):
- **T2 (this file's PART 1+2):** the enemy-death **dissolve** our engine skips (it vanishes dead enemies
  instantly; the real game plays a particle disintegration). Mechanism pinned byte-exact + baked.
- **T1 (PART 3):** a second full battle scenario beyond GATO — the user's `hold_left_to_enter_battle.state`
  (loc 19 forest, Crono+Marle vs **3× enemy 0x5E**). Converted to a runnable oracle state + scouted.

Method: `--peek` is authoritative for the RENDERED dissolve (per `battle_end.md`'s method note — `--monitor`
fast-forwards animations). All frame numbers are the deterministic **peek** timeline of the repro below unless
tagged `(mon)`. OBJ conventions per `battle_stance.md` (16×16 sprites, 4bpp, CGRAM `128+pal*16+v`, fb row =
OAM y+8). Writer PCs from `--monitor --writer`. Repro state (gitignored, local):
`saves/battle_re/forest_battle_3hetake.state` (loc19, 3 Hetakes alive, Crono command menu open).
Kill btnscript: `A`=Att, `DOWN×k`=pick enemy slot, `A`=confirm → `NAV="0:6,100:3,0:8,20:3,0:8,20:3,0:8,100:3"`
kills the clean right-side slot-2 enemy (`DOWN×1` kills slot-1, `no DOWN` kills slot-0).

---

# PART 1 — T2 DERIVATION: the dissolve is PALETTE-SWAP + an 8×8 ORDERED-DITHER tile-clear

`battle_end.md`'s open item — *"the real enemy-death dissolve (FD/ABA2) is skipped in the stuck-peek tail;
whether the enemy should linger a beat is unverified"* — is now CLOSED. The dissolve **is** rendered by the
oracle; it was missed before because it is a **VRAM OBJ-tile stream**, invisible to an OAM/CGRAM-only look and
easy to mistake for an instant vanish (the dying enemy's OAM sprites stay in place the whole time; only their
tile PIXELS and palette change).

## S1 — FD/ABA2 is the death BOOKKEEPING, not the visual (dis.py `3DABA2`)

`FD/ABA2` (`battle_loop.md` S5 death path `JSL FD/ABA2`) reads the enemy's drop record `CC/5E00,X` and
accumulates **rewards** — XP `$B28C += CC5E00,X`, gold/TP `$B2A5 += CC5E02,X`, item `$B2DB += CC5E06,X`
(3DABD4-3DAC05), plus a hit/steal roll (`LDA #64 / JSL C1FDCB`, 3DAC21). No OBJ/VRAM/CGRAM write. The **visual**
dissolve is the battle-anim engine (bank C1/CC OBJ builder + gfx streamer `FD:E170`/`$7F04D0`, the
`battle_stance.md` S4 boundary) — the SAME subsystem that streams the stance/entrance/idle tiles. It is not a
compact ROM constant, so the EFFECT is derived byte-exact **empirically** (below) and baked, mechanism cited.

## S2 — the two parts + the timeline (peek frames, slot-2 Hetake death)

`$AF02[slot]` (alive-list) clears to `FF` **one frame after** the dead-flag (block+0x1D bit7, `battle_loop.md`
S3 `C1/ED04`); but the enemy's OBJ sprites are NOT dropped then — they keep drawing through the dissolve:

| phase | frames | what the OBJ sprites do |
|---|---|---|
| **alive** | ≤178 | tiles 0x60-0x66, OAM **pal 1** (normal enemy palette) |
| **death → PAL-SWAP** | ~180 | same tiles/positions, OAM attr palette **1 → 4** (a "death" recolor). Held sprite. |
| **hold** | 181–189 | full sprite in **pal 4**, tiles unchanged (nonzero 4bpp px = **629**, constant) |
| **DITHER dissolve** | 190–220 | tile pixels cleared ~**20 px/frame**, linearly, via the 8×8 matrix (S3). 629→0. |
| **removed** | 221–225 | tiles fully 0; OAM sprites finally dropped |

So death→gone ≈ **43 peek frames** (pal-swap ~+2, hold ~9, dither 31, remove ~4). Measured nonzero-pixel curve
(tiles 0x60-0x77, one run per frame): `629 (184-189) → 609,590,569,549,527,507,489,470,450,431,410,389,370,350,
330,309,289,270,251,231,212,193,172,153,134,116,96,76,56,38,19,0 (190-221)` — a clean **−20/frame** ramp.
(The OAM sprites & pal-4 CGRAM are CONSTANT the whole time — the animation is purely the tile-pixel clear.)

## S3 — ★ the 8×8 ordered-dither matrix (byte-exact, GLOBAL 0/64)

Every dying-enemy OBJ pixel at sprite-local `(x,y)` is cleared at exactly **frame = D[y%8][x%8]** (the pixel is
present up to and including that frame, gone the next). Extracting the last-present frame per position over
N=184..221 gives a matrix that is **globally consistent — 0 of 64 cells have >1 distinct clear-frame** — i.e. a
single 8×8 pattern, tiled across the whole 32×32 sprite (verified continuous across all four 16×16 cells):

```
D (clear-frame, slot-2 timeline):        D_rel = D − 189  (clear-STEP 0..31; present iff D_rel >= curStep):
220 208 195 215 202 190 209 197           31 19  6 26 13  1 20  8
216 204 191 211 198 218 205 193           27 15  2 22  9 29 16  4
212 200 219 207 194 214 201 189           23 11 30 18  5 25 12  0
208 196 215 203 190 210 197 217           19  7 26 14  1 21  8 28
204 192 211 199 218 206 193 213           15  3 22 10 29 17  4 24
200 220 207 195 214 202 189 209           11 31 18  6 25 13  0 20
196 216 203 191 210 198 217 205            7 27 14  2 21  9 28 16
192 212 199 219 206 194 213 201            3 23 10 30 17  5 24 12
```

Each `D_rel` value 0..31 appears exactly twice in the 64 cells (a 32-level dispersed ordered dither; ~2 of 64
per level ⇒ ~20 of a ~630-px sprite per frame). `curStep = curFrame − dissolveStart`; **pixel present iff
`D_rel[y%8][x%8] >= curStep`.** This reproduces the entire tile-stream from ONLY the base tiles + this matrix.

## S4 — the pal-4 "death" palette (measured; BGR555 LE)

On death the enemy's OAM palette attr flips **1→4**; pal-4 CGRAM (`128+4*16 = 0xC0..0xCF`) holds a fixed
death-recolor, **constant across the whole dissolve** (no CGRAM fade):
`0009 339C 1F53 1E51 19ED 15AA 1148 0D05 10C4 10A2 08EA 04A6 294B 4211 7C7F 62F9`. (Distinct from the alive
pal-1 `0240 6C84 4800 2400 7C63×5 51F0×7`.) The visible effect is the mushroom recoloring, then disintegrating
into scattered reddish particles (the `7C7F`/`62F9` entries) as the dither thins it — the classic CT death poof.

## S5 — genericity (the dither is position-indexed, hence enemy-agnostic)

The matrix is indexed by **sprite-local pixel position** (tile-aligned, repeats every 8), not by pixel content,
so it applies to any enemy — only `dissolveStart` (the death frame) is per-death. **Cross-verified on a second
enemy:** the SAME 8×8 matrix reproduces the mid-left slot-1 Hetake death (different sprite pixels, different
screen position, own `dissolveStart=209`) **24576/24576 px, 0 mismatch**. (pal-4 content is verified for the
Hetake; whether the pal-4 death recolor is identical for other enemy TYPES — Beetle/Poly/Avian Chaos — is
OPEN, needs a non-Hetake death capture. The DITHER is proven generic.)

---

# PART 2 — REPLAY SPEC (`src/data/battle_dissolve.h`, generator `tools/_gen_battle_dissolve.py`)

**Baked (generic, tiny):**
- `ctBattleDissolveDither[8][8]` — the clear-STEP per sprite-local `(x%8,y%8)` (0..31).
- `ctBattleDissolvePal[16]` — the pal-4 death OBJ palette (BGR555 LE).
- `CT_DISSOLVE_STEPS=32`, `CT_DISSOLVE_HOLD≈9`, `CT_DISSOLVE_RATE_PX=20`.
- NOT baked: the enemy's base tiles (the engine already has the live enemy OBJ tiles) — the dissolve is applied
  ON them. This is why the bake is generic & small.

**Render/tick rule** (when an enemy's dead-flag block+0x1D bit7 is set, `battle_loop.md` S3):
```
on death:  d.step = -HOLD (hold the full sprite in pal4 first);  recolor its OBJ sprites pal1 -> pal4
each frame while dissolving:
   d.step += 1
   for each OBJ pixel (x,y) of the enemy sprite:
        draw it (pal4) ONLY IF ctBattleDissolveDither[y & 7][x & 7] >= d.step   // else transparent
   when d.step > 31:  drop the enemy's OAM sprites (fully cleared)
```
i.e. keep drawing the dead enemy (do NOT vanish it on the alive-list clear); recolor to pal4; and gate each
OBJ pixel on the 8×8 dither vs a step counter that advances 1/frame after a short hold. (Dead **PC** = the
fallen pose that persists, `battle_end.md` T3 — a different rule; only ENEMIES dissolve.)

**Verify** (`CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python tools/_gen_battle_dissolve.py --verify --emit`):
reconstructs the enemy OBJ tiles at every dissolve frame from ONLY `(base tiles @ dissolveStart + the 8×8
matrix)` and byte-compares the 4bpp pixels to the oracle's live VRAM:
```
[battle-dissolve verify] frames 189..221 (33) x 16 tiles x 64 px:  exact=33792/33792 (100.00%)  mismatch=0
slot1 cross-verify (same 8x8 matrix, own start=209):              exact=24576/24576             mismatch=0
```
A single 64-value matrix reproducing 33 frames of a 32×32 sprite (+ a second enemy) = the model, not a fit.

**Boundaries / OPEN:**
- The ROM routine that STREAMS the dithered tiles into OBJ VRAM = the C1/CC anim engine + `FD:E170`/`$7F04D0`
  gfx streamer (`battle_stance.md` S4 boundary); the EFFECT is byte-exact, the streamer program is not decoded.
- pal-4 death-recolor content: verified for the Hetake; per-enemy-type genericity unverified (needs a
  Beetle/Poly/Avian-Chaos death capture).
- The exact pal-swap frame vs the dead-flag (measured ~+2f, peek-timeline) and the ~9f hold are peek-frame
  measurements; the anim-engine tick that sets them is the same boundary.
- Our `src/battle.c` renders no combatant sprites yet (the `battle_stance.md` boundary), so this bake can only
  be USED once the battle OBJ builder is ported — the dissolve rule then plugs into it.

---

# PART 3 — T1 SCENARIO REPORT: the 2nd battle (loc 19 forest, 3× enemy 0x5E)

## Conversion (s9x → lake), verified runnable

`aux crono decompile/hold_left_to_enter_battle.state` decoded (WRAM direct): **loc `$0100`=19 (0x13)**, storyline
`$7F0000`=**0x08**, party `$7E2980`=`00 01 80` (**Crono + Marle**, 2-member), battle-mode `$2990`=0xC4 (**ACTIVE**,
bit6=1, `battle_atbmode.md`). Converted per `capture-oracle-state`: `--teleport 19 9 38 2 --from
candy_shop_lake.state` → base, then `s9x_to_lake.py` splice. Saved (gitignored): **`saves/battle_re/
forest_holdleft_pre.state`** (pre-battle field) + **`saves/battle_re/forest_battle_3hetake.state`** (in-battle,
command menu). Renders a **forest** (trees/mushrooms/grass); the left ~half is un-streamed field BG (the map's
left edge — Crono is walled at tile 8; harmless, the battle draws OBJ/HUD over it).

## The trigger — hold LEFT ≈ 12 frames commits, battle boots ≈ f168

Button grammar: LakeSnes currentState **LEFT = 0x40** (field `$00F2` layer shows 0x02; confirmed by Crono's
X decreasing). From the state, holding LEFT: Crono walks left into a wall at tile 8 (real scene-19 collision —
the pure teleport base walls identically), but the enemy-proximity list `$019D` is already populated, so at
**f12 the ambush commits** (`$011F` control → 0), a scripted approach cutscene runs, and at **f168 op-0xD8
fires** (`$0117 |= 0x21`, `$2989` written). Enemy staging lands at **f234** (`$AF0D`/`$AF02` = `5E 5E 5E FF…`).
(The user's "a little under 1 second" = the ~12-frame commit; the full boot is ~2.8 s.) Recipe pinned in the
generator/repro line above.

## The battle (all live-confirmed)

- **Enemies:** `$AF0D` = `$AF02` = **`5E 5E 5E FF FF FF FF FF`** → **3× enemy id 0x5E**. Field script (`ct.py
  script 19`, op-0x83 `LOADMON`): the pack Crono walks into = obj21/22/23 `83 5E 03/04/05` at tiles (7,37)(5,39)
  (10,41). Name (ROM `CC/6500`, 11B stride, validated on GATO="Gato"/id0x19="Avian Chaos"): id 0x5E = **"Hetake"**
  (the mushroom enemy in the render). Stats (`CC/4700`, HP@+0): **14 HP** each (`0E 00 01 …`). → a **3-enemy
  MULTI-ENEMY battle** (the first beyond single-enemy GATO). This unlocks/exercises: **target-select nav**
  (`A`,`DOWN` cycles slots — verified kills slot-1/2), **multi-slot alive-list** (`$AF02` per-slot FF on each
  death), **per-enemy dissolve** (each dies independently while the fight continues — the clean T2 case).
- **Other packs in scene 19** (same script, not the LEFT-trigger pack): id 0x8D **"Beetle"** (12 HP), 0x11
  **"Poly"** (99 HP), 0x19 **"Avian Chaos"** (45 HP) — staged as other `83`-loaded groups (obj14-31).
- **Intro style:** `$0145` = **0** (standard — no special battle transition; the "intro" is the field-side
  ambush cutscene, not a battle-module style).
- **D8 flags:** `$2989` = **0x02**, `$2A1F` = **0x00** (the op-0xD8 immediates imm1/imm2; cf. GATO's `D8 92 20`
  → `$2989`=0x92). `$2A1F` bit5 (loss-continue) not set here.
- **Party:** Crono **70 HP** / Marle **65 HP** (HUD-confirmed), ACTIVE ATB.

## Damage / kill sanity (confirms the T2 death path fires here)

Crono's normal attack (~17, `battle_loop.md` S3) one-shots a 14-HP Hetake: dead-flag `$5FCA`/`$60CA` → 0x80,
`$AF02[slot]` → FF, then the S3→S4 dissolve above. Full battle is a normal ATB fight (same spine as GATO,
`battle_loop.md`), just multi-enemy.

---

# Scratchpad artifacts (paths for the caller; NOT committed)

Dir: `%TEMP%/claude/…/e5608df4-…/scratchpad/`
- States: `holdleft_loc19.state` (=`saves/battle_re/forest_holdleft_pre.state`), `forest_battle_mid.state`
  (=`saves/battle_re/forest_battle_3hetake.state`), `base_loc19.state`; GATO refs `gato_lowhp.state`.
- Dissolve capture: `f1_184..226.raw*` (slot-2, step-1), `g1_192..232.raw*` (slot-1 cross-check),
  `dissolve_strip.png` (the visual: mushroom → pal4 → red-particle scatter → gone), `dsv_*.raw` (verify).
- Scene: `scene19_all.txt` (42-obj script), `fb_battle.png`/`dk_*.png` (battle renders).
- Generator `tools/_gen_battle_dissolve.py` (gitignored) → `src/data/battle_dissolve.h`. `ct_probe.exe` (oracle copy).

## Verify command
`CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python tools/_gen_battle_dissolve.py --verify --emit`

---

# ADDENDUM (T2 CORRECTION) — the dissolve is RED, not green: S4's pal-4 was a CGRAM byte-misread; provenance = the LIVE OBJ pal4 (a red disintegration palette)

User: *"the dissolve should be red and it is green"* (the dissolve should be RED, it is GREEN). **The user is
right, and S4/PART-2 are WRONG.** The baked `ctBattleDissolvePal` (green) is a **CGRAM read artifact**, not the
real death palette. Re-derived ROM+oracle, **render-confirmed** (mandate: trust the render).

## X1 — RENDER PROOF: the dying Hetake disintegrates RED-ORANGE (the surviving Hetakes stay green)

Kill the slot-2 Hetake in `forest_battle_3hetake` (dossier NAV `"0:6,100:3,0:8,20:3,0:8,20:3,0:8,100:3"`) and
peek the dissolve (`he_160.png`/`he_175.png`, W=160/175): the dying mushroom (right, ~x180,y70) dithers away in
**RED/ORANGE** particles while the two live Hetakes (left/upper) stay **green**. The dying enemy's OAM attr flips
**1→4** (verified `hpal.py`: enemy-tile pals `{1:12} → {4:4, 1:8}` at W=160, dying slot's 4 cells now pal4),
then its tiles dither-clear (the S3 8×8 matrix — that part is CORRECT) and are gone by W=190. So the recolor pal
is **pal4, and pal4 is RED** — the render shows red, definitively.

## X2 — the ACTUAL pal4 (both fights, ALL RED); GATO's is red too

Live OBJ pal4 CGRAM (`0xC0..0xCF`), read with the CORRECT 2-byte-per-color stride (`palread.py`,
`OAM.load_cgram`):
```
FOREST (Hetake) pal4 = 000D 0000 00F5 0026 0033 001E 00EF 0019 008D 0015 002C 0011 002A 0011 0009 0011
   RGB idx0..7 = (107,0,0)(0,0,0)(173,57,0)(49,8,0)(156,8,0)(247,0,0)(123,57,0)(206,0,0)   -> ALL RED/orange
GATO           pal4 = 000D 0000 009C 006B 007E 005B 0018 0057 00B7 003E 0012 002E 00AD 0029 002A 0019
   RGB idx0..7 = (107,0,0)(0,0,0)(231,33,0)(90,24,0)(247,24,0)(222,16,0)(198,0,0)(189,16,0) -> ALL RED
```
Both fights' pal4 is a **red/fire disintegration palette**, **present from battle START** (constant across the
dissolve — read identical at the boot-settled frame `t1_forestboot`/`t1_gato460` and at every dissolve frame
W=160-220). The Hetake is a GREEN enemy (pal1 `00C4 0010 00A2...` = greens) yet dissolves RED → **the recolor is
NOT the enemy's own palette** (refutes hypothesis c) and **NOT a fixed global** (GATO pal4 ≠ forest pal4, though
both red-family) → it is **(b) the LIVE OBJ pal4 row of the battle CGRAM**, a per-fight red disintegration palette
loaded at boot alongside pal1.

## X3 — ROOT CAUSE of the false "green": S4 read the CGRAM low-byte stream and re-paired it

The dossier's `0009 339C 1F53 1E51 19ED 15AA 1148 0D05 | 10C4 10A2 08EA 04A6 294B 4211 7C7F 62F9` is **pal0's 16
low bytes re-paired into 8 words + pal1's 16 low bytes re-paired into 8 words** — i.e. it read 32 consecutive
*low-order* CGRAM bytes as if they were 16 two-byte colors (skipping the high `00` bytes), so each real color's
low byte was glued to the NEXT color's low byte, fabricating high bits (0x33,0x1F,0x1E,0x19,0x15,0x11…) that read
as green/blue. Proof (`he_160` CGRAM): pal0 low bytes `[09,00,9C,33,53,1F,51,1E,ED,19,AA,15,48,11,05,0D]` paired
LE = `0009 339C 1F53 1E51 19ED 15AA 1148 0D05` (== dossier first 8, EXACT); pal1 low bytes paired = `10C4 10A2
08EA 04A6 294B 4211 7C7F 62F9` (== dossier second 8, EXACT). So the "green death palette" was never in CGRAM — it
is 32 red-palette low-bytes mis-assembled. (pal0 itself is also RED: `0009 0000 009C 0033 0053...`.)

## X4 — THE RULE for the compositor + the fix

**Recolor = the LIVE OBJ pal4 row of the battle CGRAM at death** (indices `0xC0..0xCF`, 2 bytes/color BGR555).
It is loaded per-fight at battle boot (present before any death) and is a **red/orange disintegration palette in
every fight** (the enemy's own color is irrelevant — a green Hetake dissolves red). Our engine must **source pal4
from the scene's loaded battle palette set** (the same CGRAM the stance/HUD already read), NOT bake one value.
- **Minimum fix:** replace the wrong `ctBattleDissolvePal` (battle_dissolve.h:27-28, the green) with a correct
  red pal4 — e.g. the forest value `000D 0000 00F5 0026 0033 001E 00EF 0019 008D 0015 002C 0011 002A 0011 0009
  0011` (2-byte stride, verified). Even a single baked red pal4 is far closer than the green (both fights are
  red-family); byte-exactness per fight requires sourcing the live pal4.
- The **8×8 dither matrix (S3) and the hold/rate (PART 2) are UNAFFECTED** — only the palette bytes were wrong.

## X5 — GATO does NOT dissolve (solo / fight-ending kill): stays standing through victory
Killing GATO (the only enemy) ends the fight; his OAM **never flips to pal4** and his body stays **pal1, n=12**
through the whole victory sequence ("Got 10 experience points!" `gf_270.png`, "Got 1 tech point!" `gh_400.png` —
GATO still standing, undissolved). So there is **no in-fight GATO dissolve** to capture; the dissolve only plays
for a **non-last** death (an enemy dying while the fight continues, e.g. a Hetake with siblings alive). The
"should be red" the user sees is our engine WRONGLY dissolving with the green artifact; the canonical red belongs
to the mid-fight dissolve, and the fight-ending enemy is not dissolved at all (a `battle_end`/victory-render
concern, its own boundary). This corrects PART 1 S2's "removed 221-225" framing for the LAST enemy specifically.
