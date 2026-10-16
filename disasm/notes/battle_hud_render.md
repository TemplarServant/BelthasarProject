# BATTLE HUD STRIP — render RE dossier (GATO fight, party Crono+Marle)

Everything below is derived from the ROM (`tools/dis.py`, font at file-offset 0x3F8C60 = $FF:8C60)
and the committed oracle dumps in `saves/battle_re/` (fb + `.vram` + `.wram` + `.oam`[incl. CGRAM]),
plus states regenerated from `gato_press_action.state`. Prior work: `disasm/notes/battle_boot.md`
(the HDMA split + VRAM/CGRAM layout) and `disasm/notes/battle_menu_synthesis.md §(c)` (HUD provenance).
This dossier pins the **pixels**: geometry, tile→glyph tables, palettes, the window chrome, and the
per-phase content, with a byte-exact verify.

**FB decode (critical):** the oracle `.raw` fb is 512x480, pixel-doubled → true pixel (x,y)=fb(2x,2y);
bytes `[pad,B,G,R]`; the fb has an **8px top overscan** ⇒ **our-render-y = oracle-fb-y − 8**. All
"renderY" below are engine coords (0..223); "oracle-y" are fb rows (renderY+8). CGRAM→RGB is 5→8-bit
**replication** `(v<<3)|(v>>2)` (NOT floor `v*255/31`, which is off by 1 — see D4).

**Which dumps are which** (decoded, not the filenames):
| dump | phase | evidence |
|---|---|---|
| `p000`,`gato_t0`,`gato_t1` | pre-HUD (field / staging) | 0x7800 map still garbage |
| `p300` **==** `gato_t2` | ENEMY-NAME, **mid-slide** (window ~½ open) | map has "Gato"+party but body only reaches oracle-y ~190 |
| `p600` **==** (≈)`gato_t3` | COMMAND, **settled** | window full renderY 160-207, "Att/Tech/Item"×2 + party |
| regen `--btnscript 100:8,0:360` | ENEMY-NAME, **settled** | used as the settled enemy-name target (committed p300 is mid-slide) |

The `saves/battle_re/gf*.png` are a *different* capture session (a stone-path Gato arena, abbreviated
party names) kept only for eyeballing phase; the authoritative pixels are the `.raw` dumps above.

---

## Q1 — strip geometry + the tilemap / compositing

**Vertical extent (settled): renderY 160..207 (oracle-y 168..215), 48 scanlines** — confirms
`battle_boot.md`'s HDMA "scanlines 160-207". Measured on `p600.raw` / regen-f360 (clean column x=60):
- renderY 160-163 = **top border** rows: `{173,181,181}`, **`{255,255,255}` white line**, `{90,82,82}`, `{165,173,173}`
- renderY 164-202 = **body gradient** (39 rows), cool blue-grey `{107,115,123}`→`{8,16,24}`, roughly R,G=R+8,B=R+16 per row
- renderY 203-207 = **bottom border**: `{0,0,8}`,`{0,0,0}`,`{49,57,57}`,`{0,0,8}`,`{24,16,16}`
(`p300`/`gato_t2` reach only renderY ~182-207 = window still sliding up, so they are NOT settled targets.)

**Horizontal (settled), from the top white line + dark bevel columns:**
- ENEMY-NAME = **2 boxes**: box0 (enemy name) x2..95, box1 (party) x98..253.
- COMMAND = **3 boxes**: box0 (Crono cmd) x2..53, box1 (Marle cmd) x56..101, box2 (party) x104..253.
- boxes are divided by ~2px dark bevels (separators at x54-55, x102-103) + a 2px outer screen-edge frame (x0-1, x254-255). There are **no field-showing gaps** — the whole strip is opaque chrome.

**The tilemap:** ONE 32x32 map at **VRAM word 0x7800** (`.vram` byte 0xF000). Only **map rows 0-6**
render (the window's 7 tile rows). Decoded cells: each is `(tile:10)|(pal:3<<10)|(prio:1<<13)`.
Attributes observed: pal **2** (word hi 0x29, prio set) for labels/enemy-name/numbers/bars/Crono-name;
pal **3** (word hi 0x2D) for Marle's name row. Baked verbatim in `ctBattleHudMap{Enemy,Cmd}[7][32]`.

**Compositing (the HDMA split, from `battle_boot.md §1` + `PEEK_ISOBG` here):** for scanlines 160-207
the HDMA (ch0 mode4 $2107) rewrites all four BG map bases to word 0x7800 and ch4 sets TM=0x05 (BG1+BG3
only; BG2 field + OBJ OFF in the strip). Char bases are unchanged (BG1 chr word 0x1000 4bpp, BG3 chr
word 0x5000 2bpp — verified `--ppuregs`). So the ONE 0x7800 map is read twice:
- **BG3 (2bpp) = ALL the TEXT** (labels, names, HP/MP digits, bars, hand cursor). Confirmed by
  `PEEK_ISOBG=2` (index 2 = BG3): renders exactly the text on black.
- **BG1 (4bpp) = nothing usable** for the body: its char at tiles 0x100/0x1FF is **all-zero/blank**
  (decoded BG1 chr word 0x1000). The gray **window BODY is the per-scanline color-math gradient**
  (HDMA ch5 $2130-33) inside the window-masked box — NOT BG1 tile graphics. (`PEEK_ISOBG=0` shows the
  gray boxes because backdrop+color-math ride the BG1/main pass; index 4 = OBJ = the sprites only.)
- Priorities: text (BG3, mode1 BG3-priority, BGMODE 0x09) sits on top of the body.

For the C engine the compositing reduces to: **fill each box with the per-scanline gradient (+dither)
+ bevel frame, then draw the BG3 font glyphs on top** (see the render spec, Part 2).

## Q2 — window chrome: the WHOLE strip chrome is BG1, dither generator CLOSED

> CORRECTED 2026-09-23 (battle_hud_command_windows.md): the BG1 layer is the tilemap rows 16..21 ($7E:0B40, the base
> $D1:5800 + one frame $D1:59FC per enqueued seat). The "non-periodic, position-dependent" bitmap is those tiles
> (F0..FB edges, 0FC..0FF / 3FC..3FF bodies in the ROM's own non-periodic order) under the per-line COLDATA ramp: the
> three captures are one per-tile table, 36864 samples, 0 conflicts (tools/gen/battle_hud_chrome.py).

**The entire chrome — bodies (gradient AND the ±1 dither), box bevel frames, inter-box separators,
outer screen-edge frame — is 100% BG1** (decisive: `PEEK_ISOBG=0` renders ONLY BG1 and its color-math;
probed both phases, **the isolated-BG1 render == the normal render byte-exact at EVERY strip pixel
outside glyph ink** — cmd: 2192 diffs all under ink / 0 outside; enemy: 1256 / 0. The glyph ink itself
is the BG3 text drawn ON TOP — the hand cursor's ink lands on box0's bevel columns x2-4, rows 9-18).
BG1 in the strip is a **normal tiled BG**: its 4bpp body tiles (char word 0x1000)
carry a **fixed ±1 ordered dither** (a pre-dithered blue-grey gradient bitmap — NOT the blank tiles
0x100/0x1FF that the *scrolled-away* map row shows), and a **per-scanline COLDATA color-math ramp**
supplies the vertical gradient. Decoded HDMA (from `p600.raw.wram`, ch4 header $7EC91B / ch5 $7EC948):
- **ch4 (scanlines 160-207): TM=05, TS=05** — main = sub = BG1+BG3. The subscreen is BG1+BG3, **NOT the
  field (BG2)** → the window is **opaque, field-INDEPENDENT** chrome (dossier's earlier claim confirmed).
- **ch5: CGWSEL=$00** (`addSubscreen`=0 → color-math adds the **fixed COLDATA color**, not the subscreen;
  `snes/ppu.c:1065,314-320`), **CGADSUB=$01 then $81** (add on BG1 for lines 160-183, **subtract** 184-206),
  **COLDATA ramp E8→E1 (val 8→1, ADD) then E0→E7 (val 0→7, SUB)** stepping every 3 scanlines. This is
  exactly `body_5bit = G0(BG1 dithered ≈(6,7,8)) ± COLDATA(y)` — reproduces the baked gradient, with the
  dither carried in the BG1 pixels.

**The dither is NOT** (a) pseudo-hires (P1: the 512×480 fb has fb(2x)==fb(2x+1) and 2y==2y+1 for 0/8904
body px — a clean 2× double, no sub/main blend); (b) the field-textbox `CT_ASSET_WINDOW` weave (that made
the diff worse); (c) a tileable ordered-dither matrix (P2/P4: **no x-period ≤64, no `f(x,level)`, no
per-column `f(x)`** — it is a full pre-rendered dithered-gradient bitmap, non-periodic). It **is a fixed,
phase-invariant screen-space texture in the box interiors** (the earlier "phase mismatches" at x62-89
were text-edge bleed in the sampling mask; the interior is phase-invariant, and where two boxes share
geometry — the party box x104-253 — the two phases are byte-identical).

**Reproduction (byte-exact, zero invention):** the chrome cannot be reduced to a compact generator (no
period), and it is **position-dependent** — a single box's 3px bevel profile does NOT transfer to
another box (that stamp-on-every-box approximation was the last d>16 residual: 74 enemy / 101 cmd px,
command's at box0's left bevel under the hand-cursor ink, enemy's at the party box's right bevel). So
the **whole 256x48 BG1 layer is baked per-pixel from the BG1-only oracle render** (`PEEK_ISOBG=0`) into
`ctBattleHudBg1{Enemy,Cmd}[48][256]` = a uint8 index into `ctBattleHudBg1Pal[62][3]` — one table per
phase covering bodies + bevels + separators + outer frame (the strip is opaque, no field-showing gap).
iso0 gives the clean chrome **including under text** (BG3 off), so there is no text-bleed; the BG3
glyphs are drawn on top, the oracle's own compositing order. Verified: **chrome + glyphs reconstructs
the strip 12288/12288 byte-exact, BOTH phases** (box-interior body subset: 10162/10162 enemy,
8904/8904 command). The command source is regen f600 iso0 — verified `cmd600 NORMAL == committed
p600.raw` byte-for-byte, so its iso0 is p600's own BG1 layer.

The only other baked chrome data: CGRAM sub-palettes `ctBattleHudPal*` (the BG3 text palettes). The
old flat-gradient/single-box tables (`ctBattleHudGrad*`, `ctBattleHudBevel{L,R}*`, `ctBattleHudOuter*`)
are gone — subsumed by the full-strip layer.

## Q3 — the per-phase layouts (decoded cell-by-cell)

Glyph decode: strip tile T (≥0x100) → CT textcode = T−0x100 → font tile CT_ASSET_MENUFONT[code].
Special codes derived by rendering the ROM font ($FF:8C60): **0x5F=':'**, **0x60-0x63 = hand cursor ☞**
(2x2), **0x64/0x65/0x66 = small "H"/"M"/"P"** headers, **0x67-0x6F = HP-bar fill 0..8px**,
**0x5D/0x5E = bar end-caps**, **0x73-0x7C = small digits 0-9**, letters 0xA0-0xD3, '.'=0xE8, space 0xFF.

**(a) ENEMY-NAME (regen f360 / `p300` map):**
- box0 (enemy name) map r2 cols 1-4 = `1A6 1BA 1CD 1C8` = **"Gato"**, padded with **tile 0x1EF (code
  0xEF)** — the CC/6500 name-record pad byte, confirming the name comes from `$CC:6500 + id*11`
  (GATO id 0x92 → $CC:6B46 "Gato", pad 0xEF; matches `battle_boot.md §2`). Header row r1 not used here.
- box1 (party): r2 cols 13-17 `1A2 1CB 1C8 1C7 1C8` = **"Crono"** (pal2); r4 cols 13-17 `1AC 1BA 1CB
  1C5 1BE` = **"Marle"** (pal3). r1 headers cols 19-20 `164 166`="HP", cols 23-24 `165 166`="MP".
  r2 numbers cols 20-24 `17A 173 15F 1FF 17B` = "70: 8"; r4 `179 178 15F 174 175` = "65:12". Bar
  cols 25-30 `15D 167×4 15E` (end-cap + fill + end-cap).

**(b) COMMAND (settled `p600`):**
- box0 (Crono cmd): r1-r2 cols 0-1 = hand cursor tiles `160 161 / 162 163` (☞ pointing at Att.);
  r2 cols 2-5 `1A0 1CD 1CD 1E8`="Att.", r4 `1B3 1BE 1BC 1C1`="Tech", r6 `1A8 1CD 1BE 1C6`="Item".
- box1 (Marle cmd): **identical** "Att./Tech/Item" at cols 8-11 (no hand cursor — the hand marks the
  active PC only).
- box2 (party): r2 "Crono" + "70: 8" + full bar `16F×4`; r4 "Marle" + "65:12" + full bar; headers HP/MP.

All label ROM provenance re-verified: `$CC:FA4A`="Att.", `$CC:FA58`="Tech", `$CC:FA66`="Item"
(14-byte FF-padded, `battle_boot.md §4`) — the on-screen codes `1A0 1CD 1CD 1E8` etc. match byte-for-byte.

## Q4 — party-window content provenance (WRAM)

Read from `p600.raw.wram`:
- **PC battle blocks `$7E5E2D + seat*0x80`**: seat0 = id 00, curHP `+3`=70, maxHP `+5`=70, curMP `+7`=8,
  maxMP `+9`=8 → matches the displayed **"70: 8"**. seat1 = id 01, HP 65/65, MP 12/12 → **"65:12"**.
  (Same values in the char sheets `$7E2600 + charid*0x50`, +3/+5/+7.) Loader = FD/AE52 (`battle_boot.md`).
- **names**: char id per seat from party roster `$7E2980..82` = {00,01,0x80=empty}; name text from the
  per-character name buffers **`$7E2C23`** (CT-encoded; $2C23="Crono", next="Marle", …) — the standard
  `$7E2C23` name buffers (memory `ct-field-name-vars`).
- **name palette**: seat0 (Crono) = pal2 (white), seat1 (Marle) = pal3 (grey). This is a per-name-row
  attribute baked in the map; whether it is per-character or per-HP-status is an OPEN question (only a
  Crono+Marle capture exists — see Open questions).

**Per-seat cell layout** (party box, map cols 13..30; screen x = col*8):
`name` cols 13-17 · `space` 18-19 · `HP num` 20-22 · `:` (0x5F) col 22 · `MP num` col 24 ·
`bar` cols 25-30 (end-cap 0x5D, fill 0x67..0x6F ×4 scaled to curHP/maxHP, end-cap 0x5E). Header row
(map row 1): "HP" cols 19-20, "MP" cols 23-24. Row-y: seat k → renderY 171 + k*12 (map row 2,4,6);
header → renderY 163.

## Q5 — the hand cursor ☞

**It is NOT an OAM sprite** and **not** the menu's `nsHand` ($FF:9CF0). Decoding `p600.raw.oam`:
no sprite lies in the strip (all on-screen OBJ are the field/party/GATO sprites at Y<160; strip rows
carry only parked Y=240 sprites). The command cursor is **4 BG3 tiles 0x160-0x163** (font codes
0x60-0x63 = ROM $FF:8C60 + 0x60*16 = **$FF:9260**), palette 2, placed in the strip map at box0 (the
active PC's window) cols 0-1 rows 1-2. Rendering it is therefore free — it is just font codes 0x60-0x63
drawn like any other glyph. (An OAM navigation cursor may exist while *moving* the selection; the
settled captures show only the tilemap hand — see Open questions.)

## Q6 — HUD timeline (frame offsets)

Frames are relative to `gato_press_action.state` + the A-press (`--btnscript 100:8,0:N`); battle-module
boot ≈ f222 (`battle_boot.md §0`). Measured here (peek+detect window top / strip content):
- **battle trigger** f218-219 ($0117=0x21→0x01, `saves/battle_re/loss_monitor.txt`); **module boot** f222-223.
- **first HUD pixels** ≈ f295-301 (window starts sliding up; `gf301.png`).
- **window fully open (settled ENEMY-NAME)** ≈ **f304** (top locks at renderY 161; "Gato"+party present through f330).
- **command menus appear** between f330 and f380 (map gains "Att." by f380). `battle_boot.md` quotes
  ~f400/f550 for its own capture; the exact frame depends on ATB fill (input/RNG), so pin the *settled*
  states, not a universal command-open frame.

## Q7 — the open animation — ✅ CLOSED, and the guess below was WRONG

**See [battle_hud_band_anim.md](battle_hud_band_anim.md) (defect #23, 2026-09-04).** The strip does not
slide: it **irises**, symmetrically, about its own centre line, over 23 steps at two steps per frame.
`CD/053C` decrements the gap ABOVE (`DEC $C86A,X`) and the gap BELOW (`DEC $C870,X`) by one line EACH
while growing the band by two, across nine tables at `$7E:C867` — so the top edge rises and the bottom
edge falls at the same rate, and the band is strip rows `[23-s, 24+s]`, 2 scanlines to 48.

What is preserved below is the ERROR, on purpose. The original reading —

> The window **slides/wipes up from the bottom edge**: it is bottom-anchored and grows upward, the party
> box (right) filling slightly before the enemy box (left) (`gf301`,`302`,`303`,`305`), both settling to
> the full renderY 160-207 by ~f304-312.

— came from eyeballing six PNGs, and both halves of it are false: the band is centred, not
bottom-anchored, and the two boxes reveal together (the band is one HDMA entry spanning the whole
width; nothing can fill one box before the other). The right instinct in the old text was the last
sentence — *do NOT invent a curve* — and the resolution was not a finer PNG sweep but the ROM routine
plus `--hdma`, which reports the per-scanline registers directly and made the whole thing a measurement.
The frame numbers above are still good: the ladder is 13 frames and it ends at the settled f304, which
is what fixes the arm at 82 - 13 = 69 module frames.

---

# PART 2 — RENDER SPEC (the function to implement later)

```
void CtBattleHudRender(uint8_t* fb /*256x224 RGB*/, int phase /*0=enemy-name,1=command*/,
                       const uint8_t* mem /*$7E image*/, const uint8_t* rom);
```
Draw over the already-rendered field, in renderY **160..207** (CT_BHUD_Y0..+CT_BHUD_H). Data =
`src/data/battle_hud.h`. Font = `CT_ASSET_MENUFONT` (== ROM $FF:8C60); glyph(code)=font tile[code], 2bpp,
value 0 transparent, 1..3 = `ctBattleHudPal*[pal][v]`.

**1. Chrome — the full-strip BG1 layer, verbatim:** for renderY ry in 0..47, x in 0..255, plot
   `ctBattleHudBg1Pal[ ctBattleHudBg1*[ry][x] ]`. This one table IS the bodies (gradient + the fixed
   BG1 dither), the per-box bevel frames, the inter-box separators and the outer screen-edge frame,
   byte-exact (Q2 — baked from iso0; the strip is opaque, no field-showing gap). No per-box stamping:
   the dithered bitmap is position-dependent, so only the verbatim layer is exact.
   (`ctBattleHudBox{Enemy,Cmd}` remains the box geometry reference, e.g. for the hand-cursor column.)

**2. Text (BG3 glyphs on top).** For each used map row r∈{1,2,4,6} (screenY=`ctBattleHudRowY[r]`),
   each col c: `w = map[r][c]`; tile=w&0x3FF, pal=(w>>10)&7; if tile≥0x100 draw glyph(tile−0x100) at
   (x=c*8, y=ctBattleHudRowY[r]) with palette `pal`.

**3. Dynamic content** (replace the baked map cells; the map is the *layout* + the reference party):
   - **Enemy name** (phase 0, box0, r2 from col1): `CtBattleEnemyName(id,…)` (already in `battle.c`,
     ROM CC/6500), one glyph per char, code = CT-encode(char), pad tiles 0x1EF.
   - **Party** (both phases, right box): for seat k with char id = `mem[$2980+k]` (skip 0x80):
     name from `$7E2C23` buffer (CT codes → tiles 0x100+code) at r=2+2k cols 13-17; curHP=`u16 mem[$5E2D+k*0x80+3]`,
     maxHP `+5`, curMP `+7` rendered as small digits (code = 0x73+digit) at the HP/MP columns with ':' (0x15F);
     HP bar = end-cap 0x15D + `round(curHP/maxHP * 4)` full tiles 0x16F (partial via 0x167..0x16E) + end-cap 0x15E.
     Name palette: seat0→pal2, seat1→pal3 (baked; general rule OPEN).
   - **Command labels** (phase 1, box0+box1, r2/r4/r6): static "Att."/"Tech"/"Item" (`$CC:FA4A/FA58/FA66`).
   - **Hand cursor** (phase 1): tiles 0x160-0x163 at the active PC's box cols 0-1 rows 1-2 (font codes 0x60-0x63).

**Coords are byte-exact** (col*8 for x, the `ctBattleHudRowY` table for y — the row pitch is 12px, not
8, because the in-strip BG3 V-scroll is per-scanline HDMA-compressed; measured, not assumed).

---

# D4 — verify (reconstruct from baked data, diff vs oracle)

`tools/_gen_battlehud.py --verify` reconstructs the strip from `src/data/battle_hud.h` + the ROM font and
pixel-diffs vs the oracle fb over renderY 160-207 (12288 px). Output **after the full-strip BG1 chrome
bake** (the `--diffc-enemy/--diffc-cmd` C-engine dumps give the same numbers — verified identical):

```
[cmd   vs p600(committed,settled)]  exact=12288 (100.0%)  d<=16=0  d>16=0
     mean|err|=0.00  max|err|=0 ; glyph-INK 2221/2221 byte-EXACT ; box-interior BODY 8904/8904 byte-EXACT
[enemy vs regen-f360(settled)]      exact=12288 (100.0%)  d<=16=0  d>16=0
     mean|err|=0.00  max|err|=0 ; glyph-INK 1262/1262 byte-EXACT ; box-interior BODY 10162/10162 byte-EXACT
```

**The settled strip is fully byte-exact, BOTH phases** — chrome (full-strip BG1 layer, Q2) + BG3 glyphs
on top reconstruct all 12288 px with zero error. The former `d>16` residual (74 enemy / 101 command px,
max 99) was **entirely the single-box BEVEL approximation** (a `bevel_box()` profile lifted from one box
and stamped on every box — command's mismatch at box0's LEFT bevel x2-4 under the hand-cursor cell,
enemy's at the party box's RIGHT bevel x251-253); closed by baking the whole BG1 layer per phase instead.
Earlier off-by-1 fixed: CGRAM→RGB must use bit-replication (that alone moved glyph ink 41%→100% exact).

**Prior baselines, for history:** before the body bake: exact 65.6%/60.8%, d>16 2.6%/3.3%, mean|err|
3.59/3.89 (the un-reproduced ±1 body dither); after the body bake but with single-box bevels: exact
99.1%/99.1%, d>16 74/101 px, max|err| 99.

**Repro:** from repo root, `CT_EXE=<scratchpad ct_probe.exe> python tools/_gen_battlehud.py --verify`
(regenerates the settled enemy-name frame via `--peek gato_press_action.state … --btnscript 100:8,0:360`;
uses committed `saves/battle_re/p600.raw*` for the command phase).

---

## Open questions
- **~~Slide-open animation curve (Q7)~~ — CLOSED (battle_hud_band_anim.md).** It is not a slide and there
  is no curve: a 23-step centred iris, `CD/053C`/`CD/0577`, two steps per NMI, band = strip rows
  `[23-s, 24+s]`. Ported 2026-09-04; graded by battle-hud-iris-live / battle-hud-iris-closes. The one
  piece still open is COLOUR, not geometry: during the open the COLDATA ramp is shifted one strip row
  (`$C94F -= 4` per step), so 17 of the 48 rows want a second chrome bake.
  CLOSED 2026-10-15 (the last section: the chrome walks channel 5 from WRAM).
- **Name palette rule (Q4):** seat0=pal2 / seat1=pal3 is baked from the only party captured (Crono+Marle).
  Whether the palette is per-character (signature colour), per-seat, or per-HP-status needs a capture with
  a different party order / a low-HP PC (the HP-status colouring is the likely mechanism — unverified).
- **~~Enemy-phase HP bar~~ — CLOSED (ADDENDUM 2 §A).** The "HP bar" is the **ATB gauge**, not HP: the
  tile (0x67+level) = `32×(max−timer)/max` of the actor's ATB countdown; palette = pal2 when READY
  (timer==0) / pal3 while charging. The f360 capture read 0x167/pal3 because the ATB was empty & charging;
  p600 read 0x16F/pal2 because Crono's timer was 0. One rule, timer-driven — not per-phase.
- **~~Moving OAM cursor~~ — CLOSED (ADDENDUM 2 §B).** There is NONE: the hand is ALWAYS the tilemap tiles
  0x160-0x163 (verified across every nav frame + all A-states; `--peek .oam` shows no strip sprite ever).
- **The dither generator — CLOSED (see Q2).** The whole chrome (gradient + ±1 dither + bevels +
  separators + outer frame) is 100% BG1 (`PEEK_ISOBG=0` == the normal render byte-exact at every
  non-glyph-ink strip pixel, probed both phases). BG1 is a normal tiled BG whose 4bpp tiles (char word
  0x1000) hold a pre-dithered blue-grey gradient bitmap, under a per-scanline COLDATA color-math ramp
  (ch5 HDMA $2132: ADD 160-183, SUBTRACT 184-206; CGWSEL=0 so the addend is the FIXED color, TS=BG1+BG3
  so it is opaque/field-independent). The dither is non-periodic (no tileable matrix) AND
  position-dependent (one box's bevel profile does not transfer to another box), so the full-strip
  layer is baked per-pixel from the BG1-only oracle render into `ctBattleHudBg1{Enemy,Cmd}` (index into
  `ctBattleHudBg1Pal[62]`) — the strip reconstructs 12288/12288 byte-exact, both phases.
  NOT re-derived: only the exact per-line $2130-33 *program builder* in the CD/0000 boot (double-buffered
  band at $7EBC85) — irrelevant now that the settled output is baked from the layer isolation.
- **Multi-enemy / 3-PC layouts:** only a 1-enemy, 2-PC fight was captured; box counts/positions for 2-3
  enemies or a 3rd party row (map row 6 of the party box) are unobserved.

---

# ADDENDUM 2 — the party-box bar = ATB gauge (closes the "HP bar" Q) + command-menu navigation (closes "OAM cursor")

Method = **peek-only render-state sampling** (each `--peek` dump carries its OWN wram+vram+oam, read together
so logic↔render correlate at one instant) + `--monitor --writer` for builder/handler PCs + `tools/dis.py` on
the routines. Base = `gato_press_action.state` + A-boot. Scratch helpers: `bstate.py` (battle-state decoder),
`sweep.py`, `menu.py`; dumps `idle_f*.raw*`, `mdense_f*.raw*`, `act_f*.raw*`, `atb_f360.raw*`, `mn_*.raw*`,
`pre_battle.raw*`. FB/map decode as Q1-Q5 (map word 0x7800; bar = party-box cols 25-30 = end-cap 0x15D, 4 fill
cells cols 26-29, end-cap 0x15E).

## §A — the party-box "HP bar" is the **ATB gauge** (fill rule byte-exact)

**Proof it is NOT HP:** at enemy-name-settled (`atb_f360`, `--btnscript 100:8,0:352`) both PCs are FULL HP
(Crono 70/70, Marle 65/65 — the HP digits "70: 8"/"65:12" are identical to the command phase) yet **both bars
read empty** (tile 0x167, pal3). At command-settled (`p600`) the same full-HP PCs read **full** (0x16F, pal2).
The bar tracks the **ATB countdown**, not HP.

**Fill vs countdown** (one `--peek` per frame; wram `$AFAB`(Crono)/`$AFAC`(Marle) + vram bar total read
together via `sweep.py`; boot+idle `100:8,0:N`). Crono is pre-charged (max=1) so his bar is empty at f378
(timer 1) and **snaps full the instant timer hits 0** (f390). Marle's first-turn max=31, timer 31→0 over
f378→457, bar fills cell-by-cell:

| Marle timer $AFAC | 30 | 28 | 26 | 24 | 22 | 18 | 14 | 8 | 0 |
|---|---|---|---|---|---|---|---|---|---|
| bar total (0..32) | 0 | 2 | 4 | 7 | 8 | 13 | 16 | 23 | 32 |

**Builder = C1/0700** (`dis.py 10700`; writer PCs from `--monitor "0DB4:2,0D34:2,AFAC:1,AFAB:1" ... --writer`,
cells change at C1:0762/0774/0784):
```
timer = $99DD,X   (X=seat; $99DD,X is the per-tick MIRROR of the $AFAB,X countdown — S1)
max   = $9F22,X   (the value the countdown was last LOADED with)
q  = (timer<<8) / max        ; C1/010D = 8×ASL (=<<8) → dividend; SNES HW divide C1/00D7 ($4204/$4206)
$B5 = q >> 3                 ; C1/011B = only **3** LSRs — the JSR lands mid-routine at $011B, not the
                             ;   8-LSR head at $0116 (this is what makes the scale ×32, not ÷max)
fill = 32 − $B5              ; $82  (0..32)
fullcells = fill>>3 ; rem = fill&7
  → write tile 0x67 to all 4 fill cells; overwrite `fullcells` with 0x6F; then one cell = 0x67+rem
```
So **`fill = 32 − ((timer×256 / max) >> 3) = 32 × (max − timer) / max`** (fill tile per cell = 0x67 + min(8,
max(0, fill − 8·cell))). **Palette (C1/0757-075F): all 4 fill cells' hi-byte = 0x29 (pal2) if `$99DD,X==0`
(READY) else 0x2D (pal3, charging); end-caps 0x15D/0x15E stay pal2.**

**Verified byte-exact at 3 different `max`:** Crono max=1 (`$9F22[0]`=01 pre-charge → empty@t=1, full@t=0);
Marle max=31 (`$9F22[1]`=1F → full smooth curve above; e.g. t=30 → tile-0 cell = 0x69 = level 2, exact);
Crono steady max=72 (after he acts, `$AFAB` & `$9F22` both →0x48 at f546, `act` sweep → empty@t=72). **`$9F22,X`
= the countdown's LOAD value, refreshed on every reload** (=first-turn stagger, =`$B158` reload after — S1/§B).
Point-wise fill lags the read timer by ~1 tick (render-vs-logic frame skew; a clean reimpl reading its own
mirror at HUD-build time is self-consistent — endpoints are exact).

WRAM tilemap shadow (builder output, uploaded to VRAM word 0x7800): base **$7E0C80**, cell word =
`$0C80 + row*0x40 + col*2`; the 4 fill cells = row2(Crono) `$0D34/36/38/3A`, row4(Marle) `$0DB4/B6/B8/BA`.

**Command window OPEN = ACTIVE mode:** other actors' gauges KEEP filling while a PC command awaits input
(Marle's bar filled f390→460 while Crono's command sat open, current `$95D5`=0). The acting PC shows full
(timer 0, pal2). No flashing. **Enemies have NO bar** (`atb_f360` map: enemy box0 = "Gato" + pad 0x1EF only;
no HP/ATB cells anywhere in the enemy box).

**Side observation (belongs to battle_loop, not the bar):** after a PC physical action the whole ATB FROZE —
`act` sweep f560→760 all identical (Crono timer stuck at reloaded 72, Marle at 18). An attack-animation / wait
pause in the loop; flagged for the loop dossier, orthogonal to the bar rule.

## §B — command-menu navigation (Att/Tech/Item)

Cursor row **`$95DC ∈ {0,1,2}` = Att / Tech / Item**. Hand = the 4 tilemap tiles **0x160-0x163** (2×2) at box0
(the active PC) cols 0-1; top hand-row = `2*$95DC + 1` (cursorRow 0→map rows 1-2, 1→3-4, 2→5-6, pointing at
the label on row `2*cur+2`). **Always tilemap — NO OAM cursor in ANY state** (`menu.py` reports "OAM in strip:
NONE" for every nav frame and every A-state; `--peek .oam` carries no strip sprite). Closes the open question.

Direction (`--monitor "95DC:1" --btnscript "100:8,0:412,<dir>:3,0:40,…" --writer`; NOTE: `--peek` edge-timing
misregisters direction at fixed frame phases — the peek/monitor timeline gotcha — so **monitor + disasm are
authoritative**, and peek is used only for the per-dump cursorRow→hand render check):
- **Down** ($F1 bit 0x04 = LakeSnes 0x20): `$95DC++` mod 3 — Att→Tech→Item→Att. `INC $95DC,X` (C1/1268),
  wrap on `CMP #3 →0` (writer C1/1275).
- **Up** (bit 0x08 = 0x10): `$95DC--` mod 3 — Att→Item→Tech→Att. `DEC $95DC,X` (C1/1254), wrap `<0 →2`
  (writer C1/125E).
- Fixed **3-item cyclic** (immediates `CMP #3` / `LDA #2` — the standard PC command set; a per-char set with
  more/fewer commands is unobserved = boundary).
- **Auto-repeat:** the first press moves once; HELD, it repeats after a ~15-real-frame initial delay then ~1
  move per ~5 real frames (~2 logic ticks), cyclic (holding Down 70f: moves at f422,437,442,447,449,454,…).

**Confirm (A):** on **Att → `$9609`=1** (attack → target-select → the physical attack; GATO −17, S3);
on **Tech → `$95DB`=1**; on **Item → `$95DB`=2** (monitored f422-508). A Tech/Item submenu **replaces the
strip** (the Att/Tech/Item labels vanish, a hand sits at rows 1-2, the tech/item LIST is drawn by a separate
window/region — `mn_techsub`: `$95DB`=2, strip near-blank). Submenu content = **BOUNDARY** (not derived).

---

# ADDENDUM 3 — per-PC command-box CONTENT is queue-gated (fixes "both boxes always show Att/Tech/Item")

User report (translated from Portuguese): *"the boxes with Att., Tech and Item should only appear if the ATB bar is full"* (the Att/Tech/
Item box should appear only when that PC's ATB is full). Our engine bakes the **both-ready `p600`** map, whose
`Q3(b)` claims **two** command boxes (box0 Crono + box1 Marle, both "Att/Tech/Item"). That is only correct when
**both** PCs are pending; in the normal single-active flow **only the acting PC's box shows the list**. Method =
`--peek` strip-map decode (`cmdbox_sweep.py`, map word 0x7800, shadow $7E0C80) + `--monitor --writer` +
`dis.py`. Base `gato_press_action.state` + A-boot.

## §C — the content gate: box_k shows "Att/Tech/Item" ⟺ seat k is ENQUEUED in `$A6D9` (byte-exact timeline)

Per-frame sweep of box0/box1 command cells vs the ATB countdown `$AFAB` and the pending-command queue `$A6D9`
(`cmdbox_sweep.py 360..600`):

| frame | `$95D5` cur | `$AFAB` c / m | `$A6D9`[0,1] | box0 | box1 |
|---|---|---|---|---|---|
| f360 | 0xFF | 01 / 1F | FF FF | "Gato" (enemy-name) | (party) |
| f381 | 0xFF | 00 / 1E | FF FF | "Gato" (enemy-name) | — |
| **f383** | **0** | 00 / 1D | **00** FF | **Att/Tech/Item** | **blank** |
| f440 | 0 | 00 / 06 | 00 FF | Att/Tech/Item | **blank** |
| f455 | 0 | 00 / **00** | 00 FF | Att/Tech/Item | **blank** (Marle ATB full but not enqueued) |
| **f465** | 0 | 00 / 00 | 00 **01** | Att/Tech/Item | **Att/Tech/Item** |
| f600 (`p600`) | 0 | 00 / 00 | 00 01 | Att/Tech/Item | Att/Tech/Item |

So the box's command list is gated on **queue membership, not raw ATB**: Marle's ATB hits 0 at **f455** but her
box stays blank until she is **enqueued** at **f465** (`$A6D9`[1]=01) — a ~10-frame enqueue lag. `p600` (both
enqueued) is the *both-pending* special case, not the general state.

**ROM mechanism (cited).** The command strip builder = **C1/0560** (`dis.py 10560`; inline labels below are
dis.py file offsets, `1056A` = CPU `C1/056A`). Runs when `$95DB`==0 (no submenu):
```
1056A LDA $A6DE ; DEC ; STA $95F1      ; $95F1 = pending-PC count − 1
10571 LDA $A6DD ; TAX ; LDA $A6D9,X …  ; $95D5 (active seat) = queue[$A6DD] (first valid entry)  [1058A STA $95D5]
1058D..105A1: X = 0,1,2 (the three box slots):
  10593 LDA $A6D9,X ; BMI 1059B        ;   if queue[slot] == 0xFF (empty) → SKIP the box
  10598 JSR C1/0785                     ;   else BUILD that box's command list (Att/Tech/Item)
105A3 JSR C1/081E                       ; (party box)
```
The builder **C1/0785** copies the 7×6 command-list template `$CC:FA41` (→107BD) into the tilemap shadow
`$0CC0,Y` = `$7E0C80+row*0x40+col*2`, pal2 (hi-byte 0x29); an alternate/disabled template `$CC:FA6B` is drawn
at 107EE. **Non-enqueued slots are never visited**, so they keep the **init-time blank filler** (tile **0x1EF**
name-pad, written once at boot by **C1/0509**; verified: `$7E0D10` = box1 r2 col8 written `EF29` at boot, then
untouched until `A029`="A" at enqueue). Writers confirmed by `--monitor --writer`: enqueue `$A6D9` = **C1:1B83**
(Crono f381, Marle f462); box1 label = **C1:07D5**; blank filler = **C1:0509**; `$95D5` = **C1:058D**.

## §D — the enemy-name → command LAYOUT transition: gated on a PC becoming ACTIVE (`$95D5` ≠ 0xFF), not on ATB

The strip re-lays-out from **enemy-name (2 boxes)** to **command (3 boxes)** at **f383**, exactly when
`$95D5` (current actor) goes 0xFF→0 — i.e. when the first PC becomes ACTIVE / his menu opens (C1/058A stores
`$95D5` = `queue[$A6DD]`). It is **NOT** gated on ATB-ready: at f381 Crono's ATB is already full (`$AFAB`[0]=00) but
`$95D5`==0xFF still, and box0 still shows "Gato". So: **queue empty (`$95D5`==0xFF) → enemy-name 2-box; queue
non-empty (a PC enqueued → `$95D5` = a valid seat) → command 3-box.** The 3-box **chrome** (bevels/separators at
x54-55, x102-103) is present the whole command phase regardless of box1's content — only box1's *glyphs* are
queue-gated. So the baked `ctBattleHudBg1Cmd` chrome is correct for the single-ready state; only the baked
**map's box1 command glyphs** are wrong to show unconditionally.

## §E — what the NON-enqueued box contains, cell-by-cell (blank = invisible filler)

> CORRECTED 2026-09-23: the 0x1EF cells are the ENEMY NAME LIST (C1/04DC), blank only because "Gato" is short -- see
> battle_hud_command_windows.md section 4.

box1 when Marle not enqueued (`cb_f440`, cols 6-12, tile.pal): r1 = `1FF×6 100`; **r2 = `1FF 1EF 1EF 1EF 1EF
1EF 100`** (col6 space 0x1FF + cols7-11 name-pad 0x1EF + 0x100); r3-r6 = `1FF 100×6`. All pal2. Tiles **0x1FF
(space), 0x1EF (name-pad, the same invisible pad that fills "Gato"), 0x100 (code 0)** — **all render blank**, so
the box shows only chrome. When enqueued (`cb_f470`) those cells become `1A0 1CD 1CD 1E8`="Att." (r2 cols8-11),
`1B3 1BE 1BC 1C1`="Tech" (r4), `1A8 1CD 1BE 1C6`="Item" (r6), cols6-7/12 = space 0x1FF.

## §F — engine rule (the fix) + open items
**Per-box command content, byte-exact:** for each PC box slot k∈{0,1,2}: draw the "Att/Tech/Item" labels
(+ hand cursor if `$95D5`==k, at `$95DC`) **iff `$A6D9`[k] != 0xFF** (seat k enqueued); else draw **nothing**
(chrome only — matches the invisible filler). **Layout:** enemy-name 2-box while `$95D5`==0xFF; command 3-box
once any PC enqueues. The baked `ctBattleHudMapCmd` should be treated as the *populated-box* template, applied
per box only when that slot is enqueued — NOT unconditionally.
- **Open:** the internal C1/0785 secondary gate (`$95F1`/`$9F25,X`/`$9F28,X`/`$A117`/`$A6DE`/`$A115` choosing
  template FA41 vs FA6B) is a command-list-vs-alternate distinction *within* an enqueued box; in this fight it
  always resolves to FA41. A "disabled command" (grey/`$CC:FA6B`) state was not reproduced. The show/hide gate
  (the caller's `$A6D9`[slot]!=0xFF) is the operative, byte-verified rule.
- **Open:** the reverse transition (Crono in his own action window, Marle then readying) was not separately
  captured; by the same rule box1 populates when Marle enqueues regardless of Crono's phase (her enqueue at
  f465 happened while Crono's menu sat open — the rule is per-slot and phase-independent).

---

# ADDENDUM 4 — T2: the L/R READY-PC SWITCH ($A6DD head cycle) + T3b: command boxes PERSIST through an action ($A013)

## §G — T2: L/R at COMMAND level cycles the ready-queue HEAD `$A6DD`; `$95D5` + bracket + menu follow
User: *"when more than one character has a charged bar, the player can press right/left to
choose which character should act, and the frame changes"*. Method: `forest_battle_3hetake.state` (BOTH PCs ATB-ready +
enqueued, Crono command open), `--monitor "95D5:1,A6DD:1,A6DE:1,A6D9:4,95DC:1,9609:1,95DB:1,0700:8"` + RIGHT/LEFT.
- Initial: `$95D5`=0 Crono, `$A6DD`=0 head, `$A6DE`=2 tail, `$A6D9`=`[0,1,FF,FF]` (queue [Crono,Marle]), **`$9609`=0
  (COMMAND level)**, bracket (OAM #0-3, `battle_stance.md` ADD6) around Crono (148,35).
- **RIGHT (0x80)**: `$A6DD` 0→1 (writer **`C1:1224`**) → `$95D5` 0→1 (Marle, writer **`C1:058D`**) → bracket → Marle
  (115,108); the command menu (box0 hand) switches to Marle.
- **LEFT (0x40)**: `$A6DD` 1→0 → `$95D5`→0 → bracket → Crono. Verified repeatable (4 presses). The queue `$A6D9`
  itself is UNCHANGED — only the HEAD pointer `$A6DD` cycles. `$9609` stays 0, `$95DB` stays 0.

**Handler `C1/1224`** (`dis.py 11214`, the next/RIGHT branch):
```
11221 INC $A6DD                                   ; head++
11224 LDA $A6DD / CMP #03 / BNE / STZ $A6DD        ; wrap 3->0 (3 = max party = queue capacity)
1122F TAX / LDA $A6D9,X / BMI ->11221              ; skip EMPTY slots ($A6D9[head]<0=0xFF) -> loop = CYCLIC over enqueued
11236 LDA $95D5 / TAY / ... / LDA $95DC,Y / STA $95DC,X  ; copy OLD active PC's cursor row -> NEW seat ($95DC per-seat)
1124D JMP C1/179C
```
LEFT = the symmetric `DEC $A6DD` (wrap <0→2). So L/R = **cyclic selection of the ready-queue HEAD among enqueued PCs
(skip 0xFF)**; `$95D5 = $A6D9[$A6DD]` is recomputed each frame by the command builder (`C1:058D` = ADD3 §C `1058A`),
and the bracket + box0 hand + `$95DC` follow the new active seat.

**The two L/R contexts (gated on `$9609`) — both pinned, mutually exclusive:**
- **`$9609`==0 (COMMAND)**: L/R = ready-PC switch (`$A6DD`, this §G). U/D = the Att/Tech/Item cursor (`$95DC`, ADD2 §B).
- **`$9609`==1 (TARGET-SELECT)**: L/R (and U/D) = cycle the enemy target (`$9614`, handler `C1/176C`,
  `battle_scenario2.md` T3.1); the bracket has become the tile-0x000 hand (`battle_stance.md` A6.4).

## §H — T3b: the command layout + box content PERSIST while another combatant acts (`$A013`=1)
User: *"the box should not disappear until the player chooses"*. Our engine hides the 3-box chrome whenever `$95D5`
parks during an action window. The oracle does NOT park `$95D5` while a PC is still choosing. Capture: no-input GATO
fight, GATO acts f635-768 (`$A013`=1) while Crono holds his menu (`t3b_f620`, decoded strip + state):
- **f620** (`$A013`=1, GATO acting): `$95D5`=00 (Crono STILL choosing), `$9609`=0, `$A6DD`=0, **box0 r2 = `1A0 1CD 1CD
  1E8` = "Att." AND box1 r2 = "Att." — BOTH command boxes populated**; bracket present (122,111). f700 (`$A013`=0):
  identical. The strip keeps the command layout + box content through the WHOLE action.
- **Visibility rule (byte-exact):** the 3-box command LAYOUT is active iff **`$95D5`≠0xFF** (ADD3 §D), and box_k shows
  "Att/Tech/Item" iff **seat k is enqueued in `$A6D9`** (ADD3 §C). **NEITHER gate references `$A013`.** The
  action-freeze (`battle_loop.md` §E, `$A013`=1) freezes only the ATB timers — it does NOT clear `$95D5` or the queue.
  So the boxes do not disappear while an action plays. `$95D5`→0xFF ONLY when no PC is currently choosing (all ready
  PCs have acted). **Our bug** = gating box visibility on the action/park state; **fix** = gate ONLY on `$A6D9`
  membership + `$95D5`≠0xFF (drop any `$A013`/park dependency).

## §I — Open (T2/T3b)
- **`$95DC` on switch:** `C1/1224` copies old→new seat's cursor row; a per-char command set (>3 rows) is unobserved.
- **3-PC L/R:** only 2 enqueued PCs captured; the head cycle over 3 (wrap at 3) is disasm-proven, not 3-PC-captured.
- **`$A0D4` branch in `C1/1224`** (the `LDA $9916,Y` alt-path when `$A0D4`≠0) did not fire in this capture — a
  submenu/alternate-cursor state, unexercised.

---

## 2026-09-07 — THE BATTLE MENU, PIXEL-COMPARED: command + both submenus at 0 px

The strip had been verified byte-exact on TWO phases (enemy name, command) by the implementer's own
`_gen_battlehud.py --diffc-*`. The Tech/Item SUBMENU had never been pixel-compared at all — its
positions were derived from the strip tilemap and its chrome was ASSEMBLED. Comparing it found three
defects.

METHOD. Drive the ORACLE into the same submenu and diff our `CT_BATTLESHOT_HUD` strip (y160..207):

```
build/ct.exe --peek saves/gato_inbattle.state bsub_tech.raw \
    --btnscript "0:600,20:2,0:20,100:2,0:60"              # settle, Down to Tech, A
build/ct.exe --peek saves/gato_inbattle.state bsub_item.raw \
    --btnscript "0:600,20:2,0:12,20:2,0:20,100:2,0:60"    # settle, Down x2 to Item, A
```

against `--battletest saves/battle_re/gato_t0.raw.wram` with `CT_BATTLE_SCRIPT` doing the same moves.
Start: COMMAND **0**, TECH **336**, ITEM **22**.

### 1. The submenu chrome was ASSEMBLED, and its window junction was 48 px off

`battle_hud_sub.h` was built out of the COMMAND phase's own pixels (frames verbatim, interior cloned
on its 32-px period) with the window STRUCTURE read off captures believed to be hires/2x — its header
said *"the left window's right frame at real x~124 = dump 248-249 dark dip"*. Measured on a proper
capture, the Tech submenu's junction is at **columns 172/175**, and it is **two** dark columns, not
the four at 124/127/128/131 the assembly produced:

```
OURS   dark columns: 0, 3, 8, 124, 127, 128, 131, 252, 255
ORACLE dark columns: 0, 3, 8,           172, 175, 252, 255      (Item: none -- one unified window)
```

It is now CAPTURED with `PEEK_ISOBG=0` — the strip chrome is 100% BG1 (battle_hud_render.md Q2), so
that capture IS the chrome, with no text and no OBJ to subtract. Both phases use only colours
`ctBattleHudBg1Pal` already has, so the palette is untouched. Regenerate with
`tools/_gen_battlehud_sub.py`.

### 2. The Item list's scroll arrow is V-FLIPPED

Its map word is **A97F**: tile 0x17F (= CT code 0x7F, the menu font's UP triangle), palette 2,
priority 1, **VFLIP 1**. We drew it unflipped, so the arrow pointed the wrong way. Worth recording how
that was pinned down: the oracle's battle VRAM at char base **0xA000** holds `CT_ASSET_MENUFONT` byte
for byte, 0x7F included — so the glyph was never the question, only the flip. (A first attempt baked
a "battle 0x7F" tile off char base 0x2000, which is a different tile entirely; the map word settled
it.)

### 3. ⭐ The strip's NUMBERS use 0x73 + digit, not the name charset's 0xD4 + digit

Decoded off the oracle's own strip tilemap:

```
COMMAND  row 2 cols 20-24:  179 174 15F 17B      "61" ':' "8"
         row 4 cols 20-24:  179 178 15F 174 175  "65" ':' "12"
TECH     row 2 col 25:      17B                  "8"
         row 4 cols 24-25:  174 175              "12"
```

so digit *d* is code **0x73 + d**. The two ranges hold the SAME digit shapes **one pixel apart** in
the menu font (0xDC's `8` against 0x7B's, 0xD5's `1` against 0x74's) — which is exactly why the HP/MP
readout, which already used `0x73 + d`, was byte-exact while the submenu's MP column and the item
count, which went through the NAME encoder `battleHudCtEncode`, were 62 px out. Names keep 0xD4:
`battle.c`'s `ctChar` is the ROM's own text encoding for them.

### Result

| frame | before | after |
|---|---|---|
| COMMAND strip | 0 | **0** |
| TECH submenu strip | 336 | **0** |
| ITEM submenu strip | 22 | **0** |

`field-battle-techmenu-panel` and `field-battle-itemmenu-panel` re-pinned and given four mutations
between them (4 killed, 0 survived).

---

## 2026-09-07 — THE TARGET PICKER, and the ATB bar's partial path (closed the same day)

> **CORRECTED 2026-09-23 (battle_hud_command_windows.md).** The "picker" state below is NOT the picker: its WRAM
> (`gato_target.raw.wram`, and `atb_partial.raw.wram` too) has `$A6D9 = FF 01` and `$9609 = 00` -- Marle alone in the
> queue, at command level; the recipe's second A picked Crono's target and Crono had acted. `$960A = 1` there is a
> leftover of that pick. All three findings are C1/0785 / C1/081E's queue law: box0 shows "Gato" because seat 0's
> command window is not drawn (the name list, C1/04DC, stays wherever no window covers it); the narrow box0 is seat 1's
> window frame laid at col 6 (its left edge) because seat 0 is not enqueued; the hand is at 6 x `$A6DD` for every seat.
> In the ROM's own picker (forest ambush, Crono choosing, `$9609 = 1`) the windows and their frames stay, and no name
> shows. The same goes for ADDENDUM 3 §E's "invisible filler": those cells are the name list, blank only for a short
> name ("Avian Chaos" keeps "Chaos" at cols 7..11). The port draws both tilemaps from the queue now.

⭐ THE INSTRUMENT. `CT_BATTLE_RENDERWRAM` renders the strip from a given 128 KB WRAM with no battle
run — so feeding it the ORACLE's own mid-battle WRAM removes every timing and phase question: the
same bytes go in on both sides and only the renderer differs. That is what finally made the picker
comparable, and it is the right vehicle for any future battle-HUD state.

Oracle: `--peek saves/gato_inbattle.state --btnscript "0:600,100:2,0:20,100:2,0:200"` — settle into
the command menu, A on Att., A again. Start: **409 differing px** in the strip. Three defects, none of
which any existing check could see, because the picker had never been rendered from an oracle seed.

### 1. box0 keeps the enemy-name rows while the picker is up

The oracle's picker frame has `Gato` at map row 2 cols 1..4 — the same cells and the same `$7EACBC`
source the ENEMY-NAME phase uses — while the command box and the party rows carry on beside it. We
gated that block on `phase == 0` alone and left box0 empty for the whole of target-select.

The gate is the ROM's own picker counter **`$7E:960A`**: 1 while the picker is up, 0 in plain command
AND in both submenus (checked on five oracle states), INCed at `C1/2042 / 20B1 / 20E0 / 213A`, STZed
at `C1/1F86 / 216C / 21B2 / 2206 / 2262 / 22F8`, and read the same way at `C1/1A5B` (`LDA $960A /
BEQ`).

### 2. The picker has its OWN chrome

Same three boxes as COMMAND, but **box0 is four pixels narrower**:

```
oracle picker (BG1 only):  dark columns 0, 3, 48, 51, 100, 103, 252, 255
baked ctBattleHudBg1Cmd:   dark columns 0, 3, 52, 55, 100, 103, 252, 255
```

Captured (`PEEK_ISOBG=0`) rather than nudged, because 4 px is not a tile boundary — the frame TILES
differ, not their column. Baked as `ctBattleHudBg1Tgt` by the same generator as the submenus.

### 3. The hand sits one column left

With the active PC at seat 1 the oracle's hand tiles `0x160-0x163` are at map **cols 6-7** rows 1-2,
against cols 7-8 in the command layout — we drew it at the command column, 8 px right, on the box
frame. UNVERIFIED for seats 0 and 2 in this layout (the one picker capture this repo owns has seat 1
active), exactly as the box1 column is an extrapolation in the command layout.

**Result: 0 differing px** over the strip. Pinned as `battle-hud-target-select` (3 mutations, 3
killed).

### THE ATB BAR'S PARTIAL FILL — the capture that was missing, and what it settled

The section that used to sit here said the partial path was DERIVATION-ONLY: every command/picker
capture this repo owned had both PCs READY (timer 0, the bar's FULL path), and the two with a running
timer — `p300` and `gato_t2` — were captured MID-IRIS (`$CA1E=$01`, rows 182..185 of 48), so their
strip was not on screen to compare against. It named what would close it: *one oracle capture with a
running timer and the band fully open.* The user supplied exactly that; it is now
`saves/atb_running.state` (snes9x, spliced onto `gato_inbattle` by `tools/s9x_to_lake.py` — WRAM + CPU,
NOT VRAM).

#### The whole builder, C1/0700, transcribed

```
C1/0703  LDA $99DD,X / STA $AD / STZ $AE      gauge timer, zero-extended to 16 bits
C1/070A  LDA $9F22,X / STA $B3 / STZ $B4      its last LOAD value (the max)
C1/0713  LDA $AD / JSR C1/010D (ASL A x8)     timer << 8
C1/071D  JSR C1/00D7                          $4204/5 <- timer<<8, $4206 <- max, $4214 -> $B5 (HW divide)
C1/0722  LDA $B5 / JSR C1/011B (LSR A x3)     >> 3   -- the pair SCALES x32, it does not /max
C1/072C  SEC / LDA #20 / SBC $B5 / STA $82    fill = 32 - that, over 32 sub-px / 4 cells
C1/0733  LSR x3 -> $83 (fullcells) ; ASL x3 -> $86 ; $82 -= $86  -> rem
C1/0744  #67 into all four cells
C1/076B  #6F over the first $83 cells
C1/077A  #67 + rem into the next one, iff rem
C1/0752  LDX $80 / LDA $99DD,X / BNE -> attr #2D (pal3), else #29 (pal2)
```

#### ⭐ THE TIMER IS `$99DD`, NOT `$AFAB` — and the port was missing the copy that made that safe

Both reads in the builder — the gauge at `C1/0703` and the palette at `C1/0754` — take `$99DD,X`. The
port read `$AFAB,X`, the live timer.

`$99DD` is the HUD-FACING MIRROR. The static picture looked alarming at first: `$99DD` has 10 writers,
each sitting beside an `$AFAB` write of the same value (`C1/8290` is literally `LDA $AFAB / STA $99DD`
three times over; `C1/B395` is `STA $9F22,Y / STA $99DD,Y / STA $AFAB,Y`; likewise `C1/BDA9`, `C1/BEA1`,
`C1/BF1E`, `FD/A8EE`, `FD/A928`, `FD/B93B`) — while `$AFAB` has **16 more writers with no `$99DD`
partner**. That reads like the two could part company. MEASURING settles it the other way:

```
build/ct.exe --from saves/atb_running.state --monitor "7E99DD:3" --diff --writer --frames 30
  f0   7E99DD=3F09FF*
  f1   7E99DD=3E08FF*(C1:82A2)      ... every change, all 14 of them, from the SAME PC
  f27  7E99DD=3200FF*(C1:82A2)
```

`C1:82A2` is the instruction after that blanket copy's last `STA`. It refreshes all three PC seats
**every battle frame**, unconditionally, so the partnerless `$AFAB` writers are picked up by it within
the frame and the mirror never drifts across a frame boundary. Reading either array at a frame boundary
renders the same picture — which is why `$AFAB` had been working.

⚠ BUT THE LUCK WAS LOAD-BEARING. The port modelled `$AFAB` and had never modelled the copy at all, so
moving the renderer onto `$99DD` alone broke **six live-battle checks at once** (`field-battlehud-gato`,
both `lowhp-blink`s, `forest-camera-composite`, `hud-name-after-victory`, `battle-hud-iris-live`) —
`src/battle.c` writes `$AFAB` in fifteen places and mirrored none of them. `C1/8290` is now ported as a
wrapper around `CtBattleTick`: the body has a dozen early returns (debug-end, the intro ramp, the
action-window freeze, the tick-accumulator gate, the victory hold) and the ROM's copy answers to none of
them, so running it on every path out is the only placement that is both unconditional and after the
frame's ATB work — the ordering the `$99DD == $AFAB` equality in all 30 frame dumps demands.

#### ⭐ WHY A NAIVE COMPARISON ON THIS CAPTURE FAILS: the HUD rebuild does not run every frame

First diff on the state was **19 differing px**, all in the seat-1 bar, ours reaching ~4 px further
right. That is NOT the arithmetic. Swept `--peek saves/atb_running.state --btnscript "0:N"` for
N = 1..30 and read the bar's TILE CODES straight out of each frame's own VRAM (they sit at `$F0B4`
seat0 / `$F134` seat1, four words each, `29xx`/`2Dxx` over `0x167..0x16F`), against the fill computed
from the WRAM dumped beside them:

```
  f | s0 t  fill  VRAM | s1 t  fill  VRAM        fill = 32 - (((t<<8)/max)>>3)
  1 |  63     4     4  |   9    23    22         s0 max 72, s1 max 31
  8 |  59     6     4  |   5    27    22
  9 |  59     6     6  |   5    27    26
 13 |  57     7     7  |   3    29    28
 21 |  53     9     8  |   0    32    32  (pal 2D->29: the READY flip lands here, t hit 0 at f18)
 26 |  51    10     9  |   0    32    32
 29 |  50    10    10  |   0    32    32   <-- rebuild caught up on BOTH seats
 30 |  50    10    10  |   0    32    32   <--
```

The ROM's own tile codes only ever hold values the formula produces from a RECENT timer — it never
emits a value the formula cannot make — but the rebuild lags, irregularly (the plateaus above are
8, 4, 8, 5, 3 frames long). So on most frames the oracle's picture is one gauge step behind the WRAM
dumped with it, and NO renderer fed that WRAM could match it. The residual proves it exactly: it is
always ONE sub-pixel column, at `208 + VRAMfill`, every time.

```
f1   x230   (fill 23 vs VRAM 22)      f13  x236   (fill 29 vs VRAM 28)
f9   x234   (fill 27 vs VRAM 26)      f21  x216   (fill  9 vs VRAM  8)
```

#### Result

Frames 29 and 30 are the two in that window where the rebuild had caught up on both seats. On f30 the
oracle's VRAM carries `2D6F 2D69 2D67 2D67` for seat0 (timer 50 / max 72 → fill 10 = ONE full cell +
rem 2 — **the partial path**) and `296F` × 4 for seat1 (READY → full, pal2), with the band fully open
and the picker up. `CT_BATTLE_RENDERWRAM` on that frame's WRAM: **0 differing px** over the strip.

Pinned as `battle-hud-atb-partial` (4 mutations, 4 killed: the `>>3` scale, the partial cell, the
READY palette, and the cell code base). The fill arithmetic is no longer derivation-only.

## 2026-10-15 — THE WINDOW COLOR: the strip is the field window's tiles and palette, not a fixed bake

The Q2 bake was GATO's fight in Window Color 0, and "field-INDEPENDENT" held only for the picture behind the strip. The
strip's chrome follows the player's Window Color `$2991 & 7` (Dragon Tank review, disasm/notes/dragon_tank_bridge.md:
colour 6 draws near-black windows on the cartridge, the port drew grey).

* **No battle code loads window graphics.** The frame words (`$0B40`, Q2's correction) name tiles F0..FF and 3FC..3FF of
  the BG1 char base $1000, palette 0. Those are the field window's: the location load's C0/6E5C uploads
  FF/(9E10 + v * 0x280) to VRAM $1F00 / $4FC0, and C0/70E9 puts FF/B210 + v * 16 + 2 on CGRAM colours 1-15 of row 0
  (field.c fieldWindowFromRom / fieldPaletteText). The battle's boot copies the base page onto the live page $2200
  (CC/E6EC) after writing row 0's colours 8-15 only, so colours 1-7 stay the field's.
* **MEASURED** (`--peek`, the rule's oracle): the tank's fight (loc 01C, `$2991` = 86) holds VRAM $1F00-$1FFF and
  $4FC0-$4FFF = FF/9E10 + 6 * 0x280 byte for byte, and CGRAM 1-7 = FF/B210 + 6 * 16; its WRAM pages $2000 / $2200 the same.
* **The gradient** is HDMA channel 5 ($C948, CGWSEL CGADSUB COLDATA SETINI). On the settled strip the band entry is a
  repeat entry over D1/F83D's records from $C02F, one a line: CGADSUB 01 with COLDATA 8..1 (three lines a value), then 81
  with 0..7, and the last line's record $C0EC/$C0ED = 00 / E0 (no math). So pixel = CGRAM[pal * 16 + p] +- COLDATA(line).
* **The rule reproduces every capture**: the old colour-0 table (src/data/battle_hud_chrome.h) on all 7680 (tile, line,
  x) samples with v = 0 and that ramp; and the cartridge's strip outside the BG3 glyph ink on GATO's p600 (colour 0) and
  the tank's fight in colours 2, 4 and 6 (the last two made by re-running the ROM's load with `PEEK_POKE=2991=8v`).
* **The port** (field.c battleHudRenderStrip): the frame tiles from FF/9E10 + ($2991 & 7) * 0x280, the colours from the
  live page $2200 in the battle's WRAM, channel 5 walked from line 0 the way the HDMA does (a repeat entry transfers a
  record a line, a plain one once). The band's open/close moves the ramp with $C94F (CD/053C / CD/0577), which the
  per-line bake could not. The BG3 text palettes (CGRAM 8-15, the battle's own) stay baked. Pinned:
  battle-hud-window-colour-vs-oracle (12288/12288 px at colours 2, 4, 6); colour 0 stays
  battle-hud-strip-command-windows-vs-oracle's.
* Not modelled: a band record with CGWSEL bit 1 (sub-screen addition) -- none measured.
