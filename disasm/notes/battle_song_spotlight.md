# BATTLE — the GATO SONG screen effect (darken + spotlight) : DERIVED, oracle-proven

Derives, from the ROM disasm + LakeSnes oracle **only**, the screen effect behind the user report:

> *"GATO's attack that does only 1 damage [the SONG, tech `$9877`=0x10, span 311f] has the
> wrong animation. the screen should get darker and a spotlight should appear on him while he sings
> (maybe BG3?)"* — during the song the screen darkens and a SPOTLIGHT appears on GATO (maybe BG3?).
> Our engine renders the song with NO screen effect.

**HEADLINE — the mechanism is NOT an HDMA window aperture (the mission's first hypothesis is REFUTED).**
It is **two cooperating SNES colour-engine effects**, both oracle-proven by reading the emulator's true PPU
state from saved states (parse of `ppu_handleState`) + CGRAM diffs + pixel measurement:

1. **DARKEN = a CGRAM palette cross-fade to 50 %.** The **BG palettes 1–7** (CGRAM `$10–$7F`) are faded to
   half their RGB (measured mean ratio **0.476 ≈ ÷2**); **palette 0 and ALL OBJ palettes 8–15 (`$80–$FF`) stay
   full**. So the battlefield BGs darken while **every sprite (GATO, the PCs, the note effects) and the HUD
   read-out keep full brightness** — that is what reads as "GATO is lit". The fade ramps IN over ~24 f, holds,
   and ramps OUT over ~22 f (a per-frame CGRAM DMA from a preserved full-palette shadow at `$7E2020`).
2. **SPOTLIGHT = a BG3 sub-screen light-cone graphic ADDED by colour math.** Turning the effect on flips
   (oracle state delta) **`addSubscreen 0→1`, `mathEnabled` BG1 0→1 + BG2 0→1, BG3 `subScreenEnabled` 0→1**,
   scrolls **BG3 to (992,123)** to place the beam over GATO, and drops `bg3priority 1→0`. `subtractColor=0`,
   `halfColor=0` → a straight **ADD** (clamped). Where the BG3 beam is bright it adds warm **orange
   (≈ R112,G44,B11)** onto the (already-halved) arena → the bright cone; where BG3 is transparent nothing is
   added → the arena stays dark. **The cone shape is the drawn BG3 graphic, not a window** — no window register
   changes (`win_*` all 0, WH0/WH1 unused), which is why the HDMA-window-ramp hunt found only sprite buffers.

The user's **"maybe BG3"** is exactly right in spirit: **BG3 is the sub-screen layer that carries the beam**,
and the HUD/sprite layers are the ones excluded from the darkening.

Method: base states `gato_moving_lake.state` (scratch; song un-hit, span **monitor f373–685**) and, as an
independent cross-check, `gato_press_action.state` (HOLD boot `100:8,0:N`). Oracle `ct_song.exe` (copy of
`build/ct.exe`). `--peek` renders + `.wram`/`.oam`(=CGRAM) sidecars self-verify each frame's `$9877`/`$A013`;
`PEEK_SAVE` states parsed for the true PPU registers (`ppu_handleState` layout, `snes/ppu.c:189`). `--peek`≈
`--monitor` here (±1–2 f, verified). ROM `tools/dis.py`. **Every register value below is read from the oracle's
own PPU/CGRAM state — the mandate's source of truth.**

---

## A — THE VISUAL (pixel evidence)

### A.1 What appears (contact sheet `sheet1.png`, close-ups `sp500.png`/`hbview.png`)
The song plays with GATO singing; **~42 f into the span the arena darkens and a warm-orange cone of light
appears, converging on GATO**, holds for the middle of the song, then fades out ~75 f before the span ends.
The cone is **centred on GATO's column (screen x≈127)**, **widest at the top edge of the battlefield**, and
**converges downward to an apex just below GATO** (screen y≈113). GATO, the PCs and the HUD stay bright; the
grass/trees/path and the command-box BG go dark.

### A.2 Which pixels change (measured, `cone.py`/region stats; native 256×240, our-y = oracle-y−8)
`sp410` (song, effect OFF, meanL 88) vs `sp500` (effect held, meanL 58):

| region | OFF rgb | ON rgb | verdict |
|---|---|---|---|
| arena grass OUTSIDE cone | (130,95,76) | (62,44,36) | **×0.48 — halved** |
| arena trees OUTSIDE cone | (125,108,90) | (60,50,44) | ×0.48 — halved |
| arena grass INSIDE cone | (86,111,51) | (163,114,22) | halved **+ orange beam** (dR +77) |
| **GATO body (OBJ sprite)** | (162,90,87) | (163,85,82) | **dR +1 — FULL brightness (unfaded)** |
| **HUD read-out text** | (65,72,79) | (65,72,79) | **dR=dG=dB=0 — exactly unchanged** |

Fit of the outside darkening over 34 k pixels: `R_on≈0.53·R_off−1.5`, `G≈0.51·G−2.9`, `B≈0.50·B−2.5`
→ a clean **halving**. The beam add over the halved arena ≈ **(R112,G44,B11)** (warm orange/amber).

### A.3 The cone aperture (per-scanline `dL>+8` extent, content-independent red-boost method)
Centre x≈**127** on every scanline. Wide fan at the top (y8–40 ≈ x80–176), converging through
y95 x[99..156] (w57) → y105 x[106..151] (w45) → y110 x[114..141] (w27) → **apex ≈ (127, 113)**. Below y113 the
beam is gone. So the beam is a **∇ triangle/cone: base at the battlefield top, apex just under GATO, symmetric
about GATO's X**. (It is a *drawn* graphic, so this is the observed silhouette of the BG3 tiles, not a formula.)

### A.4 Layer identity (PEEK_ISOBG=0..3, `isosheet.png`)
**BG1** = command-menu window frames; **BG2** = the battlefield (grass/path/trees); **BG3** = (normally idle)
= the sub-screen beam carrier during the song; **BG4** = none (mode 1). Sprites (OBJ) = GATO, PCs, note
effects. The HUD read-out (names / HP:MP / bars) stays bright via unfaded palettes (OBJ / pal0).

---

## B — THE MECHANISM (registers + CGRAM; oracle-proven)

### B.1 The colour-math / layer state delta (parse of `ppu_handleState`, states `st410` OFF vs `st500` ON)
State parse: WRAM located at **file offset 85** (CTLS header + Snes preamble 69 B), so the PPU block =
**85 + 0x20000(ram) + 29(cpu) + 171(dma) = 131357**; field order per `snes/ppu.c:189`
(`ppu_handleState`), self-checked by `mode==1`. The bytes that change OFF→ON are the entire effect:

| PPU field | OFF | ON | meaning |
|---|---|---|---|
| `addSubscreen` (CGWSEL b1) | 0 | **1** | colour math adds the **sub-screen** (not the fixed colour) |
| `mathEnabled[BG1]` (CGADSUB b0) | 0 | **1** | BG1 main pixels take colour math |
| `mathEnabled[BG2]` (CGADSUB b1) | 0 | **1** | BG2 main pixels take colour math |
| `subtractColor`, `halfColor` | 0,0 | 0,0 | it is a **straight ADD**, clamped (no subtract, no ½) |
| `layer[BG3].subScreenEnabled` (TS b2) | 0 | **1** | **BG3 put on the SUB screen** = the beam |
| `bgLayer[BG3].hScroll` | 0 | **992** (`$3E0`) | position the beam graphic … |
| `bgLayer[BG3].vScroll` | 0 | **123** (`$7B`) | … over GATO |
| `bg3priority` | 1 | 0 | BG3 priority lowered |
| windows `win_*`, WH0/WH1, clip/preventMath | all 0/0 | **all 0/0** | **NO window is used** (refutes HDMA aperture) |

BG3 uses `tilemapAdr=$7C00`, `tileAdr=$5000`. So the composite is: **main = BG1(menu)+BG2(arena)+OBJ; sub =
BG3(beam); result(BG1/BG2 pixels) = main + BG3** (add). OBJ and BG3-on-main are not math-enabled → untouched.

### B.2 The darkening = CGRAM half-fade (CGRAM diff `z410.oam` vs `z500.oam`)
105 CGRAM entries change, each ≈ halved (e.g. `cg[20]` `4A78`(192,152,144) → `252C`(96,72,72)); **mean
on/off ratio 0.476**. Scope by palette:

| palettes | faded? |
|---|---|
| **pal 1–7 (BG, cg `$10–$7F`)** | **YES — halved** (arena + menu-box BG) |
| pal 0 (cg `$00–$0F`) | no |
| **pal 8–15 (OBJ, cg `$80–$FF`)** | **no** — every sprite stays full |

A **full-brightness source palette persists in WRAM at `$7E2020`** (the unfaded signature `78 4A 2D 19 …` is
found there in the ON state) while CGRAM holds the faded copy — i.e. the effect **cross-fades by DMAing faded
values from the preserved shadow to CGRAM each frame**, so it can ramp in and back out.

### B.3 The trigger — NOT an OBJ-anim op; the tech graphic-effect subsystem
The song's OBJ-anim (`CD/9EDA`, `$9877`=0x10) ops were disassembled via the jump table **C1/7A6B**:
`30/32/33/3A/3D` (C1/58EC–59B9) = an **effect-register machine** writing scratch vars `$5D80,X`/`$5D9C`/
`$5D9E`/`$5DA0,X` (coords via C1/762E) for the **note sprites**; `60` (C1/5D1D) = load a `D1/4100`|`D1/4400`
blob into a `$2302,X` buffer; `85`/`98`/`A8` (C1/6258/6299/69C3) = **note-sprite object drivers** (`$A5AA`/
`$A5BD`/`$A639`/`$A34A`/`$A35D`/`$A370`, reading combatant pos `$1D0C,Y`/`$1D23,Y`). **None touch colour math,
CGRAM or BG3.** So the darken+beam is the **tech GRAPHIC-EFFECT** path (bank **C3/FD** battle-effect code),
launched by the song tech (0x10) *separately* from the `$9877` OBJ anim (consistent with the
`battle_interp_verify.md` T2.A note that `CD/5EC6→CE/0380` is a distinct tech-effect table). Battle
register-write sites (ROM scan): **C3/5B69** `STA $2131`(CGADSUB), **C3/6733** `STA $2132`(COLDATA),
**C3/375C** `STA $212C`(TM), **C3/1C9A** window, and the master block **FD/EB39** (writes the whole W12SEL…
CGADSUB…COLDATA sequence). The field vblank shadow-copy **C2/0230** (DP `$38–$49 → $2123–$2132`) is the
overworld/field path and is NOT what drives battle. The exact effect-entry that arms
`add+BG1/BG2 + BG3-sub + the CGRAM fade` is bounded to bank C3/FD but not byte-traced (§E.1).

---

## C — THE TIMELINE (relative to the song span)

Span = `$9877`=0x10 & `$A013`=1, **312 f** (`gato_moving_lake` monitor f373–685; peek≈monitor ±1–2 f). Effect
detected per frame via `cg[20]` (`4A78` full → `252C` half) + the state parse. Frames below are span-relative
(f0 = span start):

| phase | span-rel | frames | what |
|---|---|---|---|
| song starts | f0 | ~24 | GATO sings, **no** screen effect yet |
| **FADE-IN** | ~f0+42 → +66 | ~24 f | CGRAM ramps full→half (`4A78→4236→3E14→35D1→3190→2D6E→252C`); BG3-sub/colour-math armed |
| **HELD** | ~f0+66 → +211 | ~145 f | arena at 50 %, orange cone on GATO |
| **FADE-OUT** | ~f0+214 → +236 | ~22 f | CGRAM ramps half→full (`252C→31B0→39F3→4236→4A78`) |
| no effect | ~f0+236 → +312 | ~76 f | song finishes; the "1" damage popup is born at span−62 (`battle_gato_moveset.md` §E) |

So the spotlight occupies the **middle ~55–60 %** of the 311 f song, with smooth ~24 f/~22 f cross-fades.
**Cross-checked on the independent HOLD fight** (`gato_press_action`, boot `100:8,0:N`): song at f1188 (spot
OFF) → **f1288 spot ON** (`cg20=252C`, `hbview.png` = identical cone) → f1388 OFF. Mechanism is general to the
song, not state-specific.

---

## D — OUR-ENGINE SPEC (software-blit; nothing applied — derive lane)

Our compose (`src/field.c`, `battleGatoRender` + the battle composite) is a software blit, so express the two
SNES effects directly in raster terms, gated on the 0x10 song span and driven off GATO's **live foot** anchor
(the same one the movement port uses):

**D.1 Darken pass (the CGRAM half-fade).** During the effect window, multiply the **battlefield BG layers**
(BG2 arena **and** BG1 command-window frame — everything drawn from BG palettes 1–7) by a fade factor
`k(t)`: `k` ramps **1.0→0.5 over ~24 f**, holds **0.5 for ~145 f**, ramps **0.5→1.0 over ~22 f**. **Do NOT
darken** the HUD read-out (names/HP/MP/bars) or **any sprite** (GATO, the PCs, the note effects) — they use
palettes 0/8–15 which stay full. (Implement as a per-BG-pixel `*k` in the blit, or fade the BG palette LUT if
one is used; keep OBJ + HUD at full.)

**D.2 Spotlight pass (the BG3 beam add).** Additively blend an **orange cone** (add ≈ `(112,44,11)`, clamped
to 255) over the darkened battlefield, **centred on GATO's live foot X**, a ∇ triangle: **base spanning the
top of the arena, apex just below GATO (~y_GATO+ a few px)**, half-width ≈ ½ the arena width at the top,
converging to ~0 at the apex (§A.3 extents: w≈57 px at y95, 0 at y113, centre x=127 = GATO's column). The
faithful source is the **BG3 beam graphic** (VRAM `tileAdr=$5000`, `tilemapAdr=$7C00`, positioned by scroll
(992,123)) — **bake it like the note sprites / walk poses** (an E-class VRAM bake); a triangle/gradient
approximation is a placeholder only. Add it AFTER the darken so the cone lands on the halved arena, matching
the measured `(halved_bg + beam)` interior.

**D.3 Timeline gate.** Run D.1/D.2 only during the middle of the 0x10 song span — start ≈ **span+42 f**, hold,
end ≈ **span+236 f** (§C), *independently of the note-sprite tracks*. Sprites (GATO singing pose + the note
effects) render as today, at full brightness, on top.

**D.4 No damage/AI change.** This is purely presentation; the song's `1`-damage + target (`battle_gato_moveset`
§F.1) and the AI/movement are unaffected.

---

## E — OPEN items
- **E.1 Exact ROM setup routine.** The `add+BG1/BG2 + BG3-sub + BG3-scroll + CGRAM cross-fade` is the tech
  graphic-effect subsystem (bank **C3/FD**; register-write sites cited §B.3) but the entry that the 0x10 song
  tech dispatches, and the exact CGRAM-fade routine, are not byte-traced. (The mechanism itself is fully
  proven from the oracle PPU/CGRAM state — this is only the code-path citation.)
- **E.2 The beam graphic bake.** BG3 tiles `$5000` / map `$7C00` — the actual cone tiles are unextracted (a
  VRAM bake, like the note-sprite / GATO walk-pose bakes). §A.3 gives its silhouette; the exact per-tile art +
  its own palette (an OBJ/BG3 palette, unfaded) still to capture.
- **E.3 Exact fade curve.** Sampled at ~4 f steps → looks linear over ~24 f in / ~22 f out; the exact
  per-frame CGRAM values (and whether it is a straight lerp of the source shadow toward ×0.5) not pinned.
- **E.4 Command-box BG (BG1) darkening.** BG1 is `mathEnabled` and uses BG palettes → it darkens with the
  arena (visible in `sp500`/`hbview`); the HUD read-out stays bright. The precise BG1-vs-HUD layer/palette
  split of the bottom strip was not fully separated (not needed for the SPEC: darken battlefield+command BG,
  keep the read-out + sprites full).
- **E.5 Beam apex exactly relative to GATO.** Apex measured at screen y≈113 (just below GATO's body); tie it
  to GATO's live foot Y once the render rebases onto the live position (movement port).

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-e5608df4-…/scratchpad/`
- `ct_song.exe` (oracle copy). States: `gato_moving_lake.state` (proof), `gato_press_action.state` (HOLD
  cross-check), `st410.state`/`st500.state` (PEEK_SAVE, effect OFF/ON for the PPU parse).
- Renders: `sheet1.png` (span contact sheet), `sp500.png` + `hbview.png` (spotlight held, two states),
  `isosheet.png` (BG isolation), `sp*/ft*/hb*.raw` (+ `.wram/.oam/.vram` sidecars).
- Analysis: `an.py` (fb→PNG+state), `sheet.py` (contact sheet), `cone.py` (additive-cone / darken fit),
  `aperture.py` (per-scanline). Inline Python: PPU-state parse, CGRAM diff, source-shadow search.
- Repro: `--peek gato_moving_lake.state out.raw --frames N` (song f374–686; effect ~f416–610); HOLD =
  `--peek gato_press_action.state out.raw --btnscript "100:8,0:N"`; disasm `tools/dis.py 0x17A6B` (op table),
  `0x20230` (C2 vblank copy), `0x3DEB2C` (FD battle PPU block).
