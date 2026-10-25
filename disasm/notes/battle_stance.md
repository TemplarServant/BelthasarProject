# BATTLE ENTRY STANCE — combatant idle-OBJ RE dossier (GATO fight, party Crono+Marle)

> **2026-09-17:** the selection bracket this dossier baked (`battle_bracket.h`) is retired; it is the ROM's C1/1115 now, rebuilt from the chooser's live foot on the `$993A` request, with the `$A4EF` grey band and the boot's D1/5EC2 tiles (battle_bracket.md).

Derives the **minimal battle STANCE subsystem** — the combatant sprites shown on battle entry, which
our `src/battle.c` never renders (the user's report: battle starts "without crono's and marle's
battle animations"). Picks up from `battle_loop.md` **ADDENDUM — entry STANCE** (field object arrays are
CLEARED at boot C1:CE04; the stance is the module's OWN OBJ engine building the OAM shadow `$7E0700`)
and pins the pixels: sprite provenance, the idle assembly, placement, the sequence mechanism, and the
boundaries — with a byte-exact render verify (`tools/_gen_battlestance.py --verify` → **0/2672 px**).

Everything is derived from the ROM (`tools/dis.py`, OBJ char base word 0 per `battle_boot.md §1`) and
`--peek` dumps regenerated from `gato_press_action.state` (A-press boots battle ≈ f222). Decoders:
scratch `an1.py`/`an2.py` (OAM cluster), `render.py`/`render2.py` (VRAM+CGRAM sprite render vs fb),
`vdiff.py` (VRAM diff), `anim.py` (anim-slots + shadow). FB decode (`battle_hud_render.md`): fb is
512×480 doubled, true px (x,y)=fb(2x,2y), bytes `[pad,B,G,R]`. **A stance OBJ pixel at OAM (x,y) lands
at oracle fb true-pixel (x, y+8)** (verified 0-mismatch; the SNES OBJ +1 scanline + strip overscan net
to +8). OBJ = 16×16 sprites, 4bpp, CGRAM `128 + pal*16 + [0..15]` (index 0 transparent), 5→8-bit
bit-replication `(v<<3)|(v>>2)`.

> **Dossier-table correction (re-derived, `an1.py idle_f460`):** `battle_boot.md §1` labelled the
> pal6 tiles 0x20.. sprites "GATO … at (164..180)". That cluster is **MARLE** (party seat 1, right
> side). The true palette map is **pal5 = Crono, pal6 = Marle, pal1 = GATO** (GATO is a 12-sprite
> pal1 assembly, tiles 0x68..0x8E, present as a field sprite too). Confirmed by rendering each cluster
> from VRAM+CGRAM: Crono (sword-ready), Marle (ponytail), GATO (the robot) — see verify.

Base repro: `CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python tools/_gen_battlestance.py
--verify` (regenerates the settled frame f460 via `--peek gato_press_action.state … --btnscript
"100:8,0:452"`; that btnscript reproduces the committed `idle_f460.raw` **byte-identical** in fb+vram+oam).

---

## S1 — sprite-sheet provenance: the boot UPLOADS battle poses (NOT the field sheet), same VRAM slots

**OBJ char base = VRAM word 0** (`battle_boot.md §1`, `--ppuregs`). Combatant sprites use these tile
numbers (constant field↔battle) but the **content is replaced at boot**:

| combatant | pal | tile numbers (16×16 cells) | 8×8 span |
|---|---|---|---|
| Crono | 5 | 0x008 0x00A 0x00C 0x00E | 0x08–0x1F |
| Marle | 6 | 0x020 0x022 0x024 0x026 | 0x20–0x37 |
| GATO  | 1 | 0x068 0x06A 0x06C 0x06E 0x080–0x08E (12 body) + 0x006 (foot quad) | 0x06,0x68–0x9F |

**VRAM diff pre-battle vs in-battle** (`vdiff.py pre_battle.raw idle_f460.raw`; `pre_battle` = the
field frame before the A-press, `--btnscript "0:60"`): the OBJ region **`0x0000-0x1400` and
`0x3000-0x3C00` is fully rewritten**, and **every combatant 8×8 tile's content changed** (Crono
0x08.., Marle 0x20.., GATO 0x68.. all differ). So the battle poses are a **fresh boot upload to the
same slots**, not the re-used field sheet — matching `battle_boot.md §1` ("0x0000-0x1400 & 0x3000-0x3BFF
OBJ chr uploads = battle poses"). Rendered proof: the field Crono (`render.py pre_battle.raw 5`) is the
standing/away pose; the in-battle Crono (`idle_f460`) is the side-facing sword-ready stance — different
graphics in the same tile numbers.

**CGRAM (`idle_f460.raw.oam` CGRAM):** OBJ pal5 (Crono), pal6 (Marle), pal1 (GATO); BG palette row 1 is
also swapped to the HUD set (`battle_boot.md §1`). Baked verbatim in `ctBattleStanceObjPal[8][16]`.

**ROM source of the pose PIXELS = BOUNDARY.** The battle-sprite/animation gfx (PC battle sheets +
GATO's enemy gfx) are streamed from ROM by the boot transition (`CD:0000` from `CF:FB65`) + the
per-frame chip-animation streamer (`FD:E170-E18B`, DMA ch7 fed from the `$7F04D0` table,
`battle_boot.md §2`); the enemy gfx/pose ROM table feeding the 0x400-0x6FF upload is `battle_boot.md`'s
own unexplored open item. Not a compact constant → the pose pixels are **baked** from the oracle VRAM
(`ct_charbase.h` / `kBattleStatDerive` precedent), boundary documented.

## S2 — the idle-ready stance: assembly, loop, active-vs-waiting

**The OAM assembly** (settled f460, `an2.py`; baked verbatim into `ctBattleStanceObj[]`), sprite =
`tile @ (dx,dy)` relative to the cluster anchor:

- **Crono (pal5, anchor 109,108):** `00E@(16,0) 00A@(0,7) 008@(16,13) 00C@(17,28)` — 4 cells, a
  ~16×36 side-on sword-ready pose.
- **Marle (pal6, anchor 164,63):** `020@(3,0) 024@(16,15) 022@(0,16) 026@(0,32)` — 4 cells, ~20×33.
- **GATO (pal1, anchor 104,63):** 12 body cells (0x68..0x8E) forming ~48×64, **plus** a 4× mirrored
  **tile-0x06 foot quad** `006@(18,45) 006H@(26,45) 006V@(18,69) 006VH@(26,69)` that appears only once
  combat is live (12 sprites pre-battle → 16 in-battle).

**The idle LOOP (cadence PARTIALLY OPEN, mandate-pinned to static minimum).** Across the dense capture
(`mdense_f379..f460`, 3-frame step; `an2.py 5`) Crono's stance is a **2-frame micro-bob**:
- **frame P:** `00A@(0,9)` `00C@(16,29)`, anchor y=107;
- **frame Q:** `00A@(0,7)` `00C@(17,28)`, anchor y=108 (arm/leg ±2px).
The **tile NUMBERS never change** (always 08/0A/0C/0E); a P↔Q swap moves the 00A(arm)/00C(leg) OAM
offsets and streams a single sub-tile (0x1B changed P↔Q, `vdiff.py mdense_f388 mdense_f406`). Peek P/Q
dwell is **irregular (6–18 frames)** — the `--peek`-timeline caveat (`battle_hud_render.md`) — so the
**exact per-frame cadence is OPEN**; the driver (S4) advances one stance-frame per anim-slot period
(Crono period byte ≈ 0x10). Per the mandate, the **settled static stance (frame Q, f460) is the
byte-exact implementable minimum**; the bob is marked OPEN with the measured ±2px amplitude + 2 frames.

**ACTIVE vs WAITING (byte-observed):** the **active PC oscillates**, the **waiting PC is static**.
Marle (waiting, `$95D5`≠1) is **rock-static** — `idle_f420/460/480/520/560` are the identical assembly
across 140 frames (`an2.py 6`). Crono, the current actor (`$95D5=0`, command open, f390+), shows the
P/Q bob. **READY vs CHARGING: no distinct pose** — Crono at f378 (charging, `$AFAB`=1) and f460
(ready/active, `$AFAB`=0) use the same 4 tiles; the only pre-settle difference is the ENTRANCE morph
(§S5). GATO (enemy) idles with a small body-tile stream (0x68/69/78 change f460↔f560, `vdiff.py`) + the
static foot quad.

## S3 — placement: `$29B0` is the FIELD-position handoff, NOT the battle anchor

The staged **PC record `$29B0 + 5*seat`** is written field-side at the C0/038F→C0/0430 handoff
(`src/field.c:1684-1695`): `{+0 relTileX = (ax>>4)-oTX, +1 relTileY, +2 fineX = (ax-camPx)&15,
+3 fineY, +4 prio = $0F01,X}` — i.e. **the PC's field tile/fine position when battle triggered** (seat0
f460 = `08 09 06 00 03`), *not* the on-screen battle anchor (~109,108). The **enemy record
`$29BF + 12*slot`** carries `+5 = id` (slot3 `$29E3+5` = **0x92 = GATO**), `+6/+7` empty/hidden bits
(`battle.c stageAll`). So the module PLACES combatants from its own **battle-formation layout** (PC seat
→ left staggered column; enemy → formation coords), seeding the walk-in from `$29B0` — the formation
ROM math is a BOUNDARY. The **settled anchors are read byte-exact from the OAM / `$7E0700` shadow**
(the ground truth for replay): Crono (109,108), Marle (164,63), GATO (104,63); baked in
`ctBattleStanceActor[]`.

## S4 — the frame-sequence mechanism: ROM anim engine + tile streamer (compact table = NO)

**The OAM shadow `$7E0700` is the live OAM** — `$7E0700[0:512]` == the `.oam` low table **512/512
byte-exact** (`anim.py`). The module's C1/CC OBJ engine builds it each frame from per-combatant
**animation-slot records at `$0520 + n*0xC`** (DP page D=$0500), ticked by **`FD:E3A9-E3CE`**: `+4` =
tick/phase, `+5` = period, `+3` = frame index, `+6..+11` = script pointer/state (`battle_boot.md §2`).
Across f388→f406→f460 the `+4` phase bytes advance per slot (e.g. slot0 01→03→09; slots 6/7 free-run
~1/frame). The **frame graphics are streamed** into the OBJ VRAM slots per frame (the `$7F04D0`/
`FD:E170` chip streamer) — GATO's body tiles and Crono's 0x1B sub-tile change between idle frames — so
the stance is **NOT reducible to a compact standalone frame table**: it is the output of the ROM
battle-animation-script interpreter (a large subsystem, the addendum's boundary).

**Therefore the replay uses the OAM-REPLAY BAKE fallback** (task-authorized): bake the observed settled
assembly + the referenced OBJ tiles + palettes per combatant (Crono/Marle/GATO). The anim engine is
CITED so a future full port knows the entry point (`FD:E3A9` + anim-slots `$0520+n*0xC` + the ROM
per-character battle-anim scripts); its per-frame decode is the boundary.

## S5 — boundaries beyond the idle-ready loop (enumerated from the dumps, NOT derived)

- **Entrance morph (field→battle):** `idle_f300..f340` (`an2.py`) show each combatant morph from the
  **field-standing 4-cell vertical column** (Crono `008@(0,0) 00A@(0,16) 00E@(0,32) 00C@(0,48)`,
  anchor ~126,96 — the field sprite) to the spread battle stance; Crono settles ~f340, Marle later ~f420.
- **Attack animation (`act_f470..f760`, CT_BATTLE_SCRIPT attack; `an2.py 5`):** idle → **wind-up**
  pose f470 (`008@(12,0) 00E@(0,14) 00C@(12,16) 00A@(12,32)`) → Crono **translates off-screen** (no
  pal5 at f640 — he walked to GATO) → **lunge/swing** f600 (`00E@(16,0) 008@(16,16) 00A@(0,20)
  00C@(16,32)`) → returns to idle (f680+, `$AFAB` reloaded 0x48 = he acted). Same 4 tile slots,
  streamed content.
- **Hurt flash / victory poses / techs / items / multi-enemy layouts / the moving nav cursor** — not
  present in these idle captures; BOUNDARY (the full battle-animation + gfx-streaming subsystem).

---

# PART 2 — REPLAY SPEC (what to bake + the render/tick rule)

**Baked data — `src/data/battle_stance.h`** (emitted by `tools/_gen_battlestance.py`; regenerated from the
oracle, deterministic):
- `ctBattleStanceObj[24]` — the settled combatant OAM list (the `$7E0700` shadow, onscreen sprites) in
  OAM-index order: `{oidx, x (int16 signed), y (uint8), tile (9-bit), pal, prio, hf, vf}`.
- `ctBattleStanceActor[3]` — `{name, pal, anchorX, anchorY, nspr}` for Crono/Marle/GATO (partition by pal).
- `ctBattleStanceObjTiles[0xA0][32]` — OBJ 8×8 tiles 0x00..0x9F, 4bpp planar, verbatim from the boot
  upload (S1). A 16×16 sprite of tile T uses 8×8 tiles `T, T+1, T+0x10, T+0x11`.
- `ctBattleStanceObjPal[8][16]` — OBJ CGRAM palettes, BGR555 LE words (combatant pals 1/5/6 used).

**Render rule (composites over the already-drawn field, once combat is live):**
```
for each sprite S in ctBattleStanceObj, drawn HIGHEST oidx FIRST (SNES: low index = on top):
  for the 16×16 cell (8×8 sub-tiles T, T+1, T+0x10, T+0x11):
    decode 4bpp pixel v (0..15); v==0 -> transparent
    apply hf/vf within the 16px cell
    plot rgb = bitrep(ctBattleStanceObjPal[S.pal][v]) at screen (S.x + sx, S.y + sy)
```
The sprite's oracle fb row = `S.y + 8`; in a 256×224 engine buffer that maps to fb via the +8 overscan,
so plot at engine `render_y = S.y` (if the shared drawSprite adds the SNES +1, feed it `S.y − 1` — the
verified net is fb row = OAM y + 8). x is the signed 9-bit OAM x directly. This reproduces all three
combatants **0/2672 opaque-px byte-exact** vs the oracle (overlap resolved by the oidx draw order).

**Frame selection per combatant STATE (S2):**
- **Any settled state (charging / ready / active / waiting)** → render the baked settled stance. This
  is byte-exact and is the implementable minimum. Marle-as-waiting is exactly this (static).
- **Active-PC idle bob** (Crono while `$95D5`==his seat) → OPEN: a 2-frame ±2px bob (frame P/Q, S2);
  cadence unpinned (peek-timeline) and tiles stream, so not baked byte-exact — safe to render the
  static stance, or a ~16-frame P/Q alternation as an approximation (mark as such, not oracle-exact).
- **Entrance morph, attack/hurt/victory/tech animations** → BOUNDARY (S5); out of the minimal stance.

**Party-agnostic vs baked:** the **mechanism is general** — OBJ base word 0, 16×16 cells, one CGRAM
palette per combatant, anchor from the battle-formation layout, pose tiles from the boot upload, OAM
built into `$7E0700`. The **pixels/assemblies are baked per observed combatant** (Crono seat0, Marle
seat1, GATO slot3); other characters/enemies need their own capture or a full port of the C1/CC OBJ
builder + `FD:E3A9` anim engine + ROM battle-anim scripts (documented boundary, `ct_charbase.h` precedent).

---

# Verify

`CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python tools/_gen_battlestance.py --verify`
regenerates the settled frame (btnscript `100:8,0:452`), reconstructs the three combatants from the
baked arrays, composites in OAM-priority order, and pixel-diffs vs the oracle fb over the combatant
bounding box:
```
[battle-stance verify] combatant OBJ over bbox x[108..183] y[65..147]:
  exact-ish=2672/2672  mismatch(d>24)=0  (100.0% match)
  Crono  pal5: 4 sprites, tiles 0x008/0x00A/0x00C/0x00E
  Marle  pal6: 4 sprites, tiles 0x020/0x022/0x024/0x026
  GATO   pal1: 16 sprites, tiles 0x006/0x068..0x08E
```

## Open questions
- **Idle-bob cadence (S2/S4):** the exact per-frame P/Q period + the per-frame tile stream are not
  cleanly pinnable from `--peek` (timeline jitter, dwell 6–18f); needs a per-frame monitored capture of
  the anim-slot `$0520+n*0xC` phase (`FD:E3A9`) correlated to the OAM. Cadence ≈ anim-slot period (~16).
- **Battle-formation anchor math (S3):** the ROM routine mapping seat/enemy-formation → screen anchor
  (seeded from `$29B0` field pos) is unpinned; anchors are baked from the OAM instead.
- **Pose-gfx ROM source (S1):** the battle-sprite/enemy-gfx ROM tables + the `$7F04D0`/`FD:E170` streamer
  program (shared with `battle_boot.md`'s open item) — baked from VRAM here.
- **Other parties/enemies:** only Crono+Marle vs GATO captured; other combatants need their own bake or
  the full OBJ-builder port.
- **Entrance / attack / hurt / victory / tech animations (S5):** enumerated from `idle_f300..340` /
  `act_f470..760` but not derived (the large animation subsystem). **Entrance now DERIVED — see ADDENDUM 3.**

---

# ADDENDUM 3 — S6: the ENTRANCE weapon-draw morph (field pose → settled stance), per-combatant pose ladder

Closes S5's first boundary (the "entrance morph"): the user report *"when the battle starts there is a more complex
weapon-grabbing animation"* (a more complex weapon-grabbing animation on battle start). Method = **dense
`--peek` sweep, one frame per render frame**, decoding each combatant's OAM cluster (`ent_sweep.py`: anchor =
min-corner, sprites = `tile@(dx,dy)[flips]`) + VRAM tile-content diffs (`vdiff.py`). All frames are the
deterministic peek timeline (`100:8,0:N-8` → frame N; naming verified byte-identical on re-peek, e.g.
`verify300`==`idle_f300`). Base `gato_press_action.state`, party Crono seat0 + Marle seat1 vs GATO.

## S6.1 — the shape of the entrance: two PCs morph, the enemy is static

Across `ent_f300..f422` (per-frame) each PC morphs from a **field-standing vertical 4-cell column** (the
pre-battle field sprite arrangement — the SAME battle-pose tiles, just stacked) through intermediate
crouch/gather + spread poses to the **settled side-on stance** (S2). **GATO does NOT weapon-draw** — his pal1
12-cell assembly is byte-identical at anchor (104,55) every frame f290→f382; he only gains the 4-cell foot
quad (12→16 sprites) and settles down 55→63 at combat-live (~f383→460), plus his ordinary idle body-tile
stream (S2) — none of that is an entrance animation. So the entrance = **Crono + Marle only**.

## S6.2 — CRONO: a clean discrete 4-pose ladder, 10 render-frames each (byte-exact, per-frame verified)

Pose changes land on **exactly** f315 / f325 / f335 / f345 (per-frame sweep `ent_f311..f346`) — a **regular
10-render-frame cadence** (≈4 logic ticks at the global ~2.5 f/tick, `battle_loop.md`). Each pose's tiles are
**stable within its dwell** (`vdiff ent_f315 vs ent_f324`: Crono tiles-changed=none; same for P3 f335/f344),
so each is a true keyframe. Assemblies (anchor = OAM min-corner; sprite = `tile@(dx,dy)`):

| pose | frames | dwell | anchor | assembly (pal5) | note |
|---|---|---|---|---|---|
| **P0 field** | ≤f290–**f314** | (pre) | (126,96) | `008@(0,0) 00A@(0,16) 00E@(0,32) 00C@(0,48)` | 16×64 vertical column (field sprite) |
| **P1 gatherA** | **f315–324** | 10 | (126,107) | `00A@(0,0) 008@(0,13) 00C@(0,29) 00E@(0,45)` | anchor drops +11, tiles reorder/compress |
| **P2 gatherB** | **f325–334** | 10 | (126,106) | `00E@(0,0) 008@(0,14) 00C@(0,30) 00A@(0,46)` | crouch, tile order 0E/08/0C/0A |
| **P3 spread** | **f335–344** | 10 | (112,106) | `00E@(14,0) 00A@(0,8) 008@(14,14) 00C@(14,30)` | anchor x −14, spreads to ~30×46 (sword out) |
| **P4 settled** | **f345→** | ∞ | (110,107)→(109,108) | `00E@(16,0) 00A@(0,9) 008@(16,13) 00C@(16,29)` → `…00A@(0,7) 008@(16,13) 00C@(17,28)` | = the S2 settled stance + idle bob |

So Crono's weapon-draw = **P0 held → P1(10f) → P2(10f) → P3(10f) → P4 settled@f345**; the tile NUMBERS never
change (always 08/0A/0C/0E), the **content streams per pose** (each pose's 0x08.. tiles differ from settled,
`vdiff ent_f320 vs idle_f460`). Total morph span **f315→f345 (30 render frames)**.

## S6.3 — MARLE: slower + irregular; P1 is an irreducible walk-morph (OPEN); P0/P2/P3 are stable keyframes

Marle's ladder is **not** the clean 10-frame cadence — it is longer with long irregular dwells and a walk
(anchor drift) inside P1 (transition frames pinned per-frame `ent_f313..f318 / f345..f350 / f398..f402`):

| pose | frames | dwell | anchor | assembly (pal6) | stability |
|---|---|---|---|---|---|
| **P0 field** | ≤f290–**f316** | (pre) | (169,63) | `020@(0,0) 022@(0,16) 026@(0,32) 024@(0,48)` | stable |
| **P1 gather-walk** | **f317–346** | 30 | (171,79)→(159,79) | `022@(0,0) 020@(16,0) 026@(0,16) 024@(16,16)` | **OPEN**: anchor walks 171→159 + tiles STREAM (`vdiff ent_f318 vs ent_f345`: Marle tiles changed) — a continuous walk-in, not a held keyframe |
| **P2 spread** | **f347–398** | 52 | (154,79) | `020@(16,0) 026@(0,8) 024@(16,16) 022@(16,32)` | **stable** across all 52f (`vdiff ent_f347 vs ent_f398`: tiles-changed=none) |
| **P3 settled** | **f399→** | ∞ | (164,63) | `020@(3,0) 024@(16,15) 022@(0,16) 026@(0,32)` | = the S2 settled stance |

Marle's morph span **f317→f399 (~82 render frames)** — ~2.7× Crono's, with different per-pose dwells. The
**driver rate differing per combatant is the S4 boundary** (anim-slot period `$0520+n*0xC`, `FD:E3A9`); do NOT
assume Crono's 10-frame cadence for Marle. Her **P1 is a genuine tween** (walk + tile-stream) — pinned as OPEN,
its endpoints measured (start f317 anchor 171 / end f346 anchor 159), the interpolation NOT invented.

## S6.4 — replay verdict + what is baked (`src/data/battle_entrance.h`, generator `tools/_gen_battleentrance.py`)

**Replayable as "the stance bake ×N poses"** — YES for every **stable** keyframe. Because the pose tiles STREAM
per pose (each pose's 0x08../0x20.. content differs from the settled bake and from each other), a byte-exact
replay needs **each keyframe's own OBJ tiles** (not reusable from `battle_stance.h`); the OAM assembly alone is
not enough. So the generator bakes, per stable keyframe, its OAM sprite list + its OBJ tiles + (shared) OBJ
palettes — exactly the stance-bake shape, ×N. Baked keyframes + measured schedule:
- **Crono:** P0(f305) P1(f320) P2(f330) P3(f340) — durations P0=held / P1,P2,P3 = 10f each; then P4 = the
  `battle_stance.h` settled stance from f345.
- **Marle:** P0(f305) P2(f370) — durations P0=held until f317, P1 = **OPEN walk f317–346 (render P0 or the
  P1-start keyframe; do NOT invent the tween)**, P2 held f347–398; then P3 = the settled stance from f399.
- **GATO:** none (static — the S2/S3 settled stance covers the whole entrance).

`--verify` reconstructs each baked keyframe over the acting combatant's bbox and pixel-diffs vs the oracle fb
at that keyframe's representative frame (same rule as the stance: fb row = OAM y + 8). See `# Verify` below.

## S6.5 — Open questions (entrance)
- **Marle P1 walk-morph tween (f317–346):** anchor 171→159 + streaming tiles = a continuous walk-in; only the
  endpoints are pinned, the per-frame interpolation is the S4 anim-driver boundary. Not baked.
- **Per-combatant morph RATE / pose count:** Crono = 4 poses @10f, Marle = 3 poses @30/52f — the ROM anim-slot
  period that sets each combatant's rate (`$0520+n*0xC`, `FD:E3A9`, S4) is unpinned; the render-frame durations
  are measured, the driver is the boundary. Other parties would need their own capture.
- **Morph START trigger:** Crono P0→P1 at f315, right after the HUD window settles (~f312, `battle_hud_render`
  Q6/Q7); the exact ROM event that kicks the first morph pose is not traced (window-open handoff, presumed).

---

# ADDENDUM 4 — the IDLE micro-animations (closes the S2/S4 idle-bob OPEN): active-PC bob · waiting-PC static · GATO idle

User report *"gato moves a little in the original game"* + S2's own OPEN (the P/Q bob cadence, "irregular
6-18 f from sparse peek"). Method: **`--monitor` of the OAM shadow `$7E0700`+`$7E0900` per-frame** (byte-exact,
one run) — NOT sparse `--peek`, so the cadence jitter caveat does not apply. **HOLD** driver (`100:8,0:N-8` = one
boot-A then hold) → Crono sits **ACTIVE** in the command menu, Marle **WAITING**, GATO **combat-idle**, a clean
contiguous window **f384-504** (GATO acts at f505). Every frame = each combatant's cluster (anchor + assembly).

## S4.1 — Crono (ACTIVE PC): a ±1-2 px 3-state micro-bob, aperiodic (supersedes S2's "cadence OPEN")
Three states (monitor-exact, HOLD, ref anchor (109,108)):
- **R** (the S2 settled stance): anchor (109,108), `00A@(0,7) 00C@(17,28)`
- **b** (down-forward, the most common ready-idle): anchor (110,107), `00A@(0,9) 00C@(16,29)`  [arm +2, leg +1,+1]
- **c**: anchor (110,108), `00A@(0,7) 00C@(16,28)`  [leg −1 x vs R]

(all keep `008@(16,13) 00E@(16,0)`; the bob moves only the **arm 00A** dy 7↔9 and **leg 00C** by ±1 px, plus the
anchor ±1 px.) **Amplitude ≤ 2 px.** Observed per-frame sequence (HOLD, monitor run-length):
`b f430-436 · c f437-443 · b f444-456 · R f457-469 · b f470-512…` — rest/excursion dwells **7,7,13,13,43** =
**aperiodic** (no clean period; driven by the anim-slot phase `$0520+n*0xC`, `FD:E3A9`, S4). Only **2 sub-tiles**
(0x19, 0x1B) stream across the bob (`vdiff` f450 vs f462) — so it is essentially a **pure OAM-offset bob** (cheap
to replay from the offset table + the stance tiles). Baked as `ctCronoIdleBob` (f400-504, `(dx,dy,state)`).

## S4.2 — Marle (WAITING PC): STATIC (confirmed, sharpens S2)
Marle's cluster is **byte-identical f397-510** (one settle at f397, then rock-static 113 frames). So **only the
ACTIVE PC bobs**; a waiting PC does not move at all. (S2 said this; now pinned per-frame over 113 f.)

## S4.3 — GATO: a real idle animation ("moves a little") — foot-bob + claw-swap
Over the clean window **f384-504** GATO shows TWO idle motions (its 16-sprite cluster = 12 body cells 0x68-0x8E +
the 4× mirrored tile-0x06 foot quad):
1. **Foot-quad Y-bob:** the foot quad's dy (rel. cluster anchor) drifts **50→51→50→49→48→47→46→45→46** over
   f384-504 — a slow **±few-px vertical bob** of the feet.
2. **Claw/arm swap (the visible motion):** at **f434** the claw tiles swap — `068`/`06A` move from top-right
   `(47,10)/(47,26)` to the bottom `(31,48)/(15,48)`, and `084`/`086` move UP to `(47,11)/(47,26)`; they swap
   **BACK at f473**. So GATO's two claws trade up/down positions for ~39 frames (f434-472).
Plus GATO's body tiles stream (S2). This is **one OBSERVED cycle** f384-504 — GATO acts at f505, so strict
periodicity is not confirmable from this state; pinned as the observed sequence (not invented). The mash timeline
(GATO idles longer as Crono's target) is interrupted by the hit-flash, so it is not a cleaner idle. Baked: 2
keyframes **I0_armsRest** (rep f395) + **I1_armsSwap** (rep f460), both **byte-exact** (1860/1860, 1911/1911),
+ the 121-frame `(dx,dy,arm,footDY)` table `ctGatoIdleTraj` (f384-504) — arm=0/1 selects the keyframe, footDY is
the per-frame foot-quad offset.

## S4.4 — replay + verify
Baked into `src/data/battle_anim.h` (generator `tools/_gen_battleanim.py`, shared with the attack animations
`battle_actionfx.md` A2/A3). `--verify` reconstructs the two GATO idle keyframes byte-exact vs the oracle fb
(overlap-safe composite): `GATO I0_armsRest f395 1860/1860 · I1_armsSwap f460 1911/1911` (0 mismatch). The Crono
bob + GATO idle are the "moves a little" the user reports; the WAITING PC stays put.

## S4.5 — Open questions (idle)
- **Strict GATO idle periodicity:** only one f384-504 cycle observed (GATO attacks after) — cannot prove it loops
  vs. is a one-time charge-up; pinned as observed. A no-attack state (GATO ATB never fills) would settle it.
- **Within-pose tile stream** (Crono bob 0x19/0x1B; GATO body tiles) — the anim engine (S4 boundary); keyframes
  baked at rep frames only.
- **Bob cadence law:** aperiodic in the observed window; the exact anim-slot phase → dwell relation
  (`$0520+n*0xC` +4 phase / +5 period, `FD:E3A9`) is the S4 boundary — dwells measured, driver not pinned.

---

# ADDENDUM 5 — completeness pass: T1 CRONO idle TILE-STREAM · T3 GATO idle RE-DERIVE (feet were the BRACKET)

Closes two OPENs the user reported: *"crono's 'standing' animation is not complete"* (T1) and *"GATO's
movement looks strange"* (T3). Method as ADD.4: OAM-shadow `--monitor "0700:512,0900:32"` (strip `*`) + per-frame
`--peek` VRAM tile-content hashing. Base `gato_press_action.state`, HOLD driver (Crono ACTIVE, Marle WAITING).
Bakes are ADDITIVE in `src/data/battle_anim_add.h` (generator `tools/_gen_battleanim_add.py`, gitignored) — the
existing `battle_anim.h` arrays stay byte-identical. **tile 0x006 = the active-PC selection BRACKET
(`battle_bracket.h`), NOT a combatant** — excluded everywhere (matches the concurrent `_gen_battleanim.py`).

## A5.1 — T1: CRONO active-idle STREAMS its sword/arm sub-tiles (3 variants), locked 1:1 to the bob state
S4.1 pinned the OAM **offset** bob (3 states R/b/c) but rendered the STATIC `battle_stance.h` tiles → Crono's
sword/arm never animate ("incomplete"). Peek f400-500 step-4, hash Crono's cells 0x08-0x1F: **3 distinct tile
variants** (V0 f400 / V1 f408 / V2 f420). The **only streaming sub-tiles are 0x0C,0x0D (leg cell 0x0C), 0x19
(head/arm cell 0x08), 0x1B (arm cell 0x0A)**; cell **0x0E is static**. The 3 variants map **1:1 to
`ctCronoIdleBob.state`** (0=rest 1=down 2=side), **locked exact over f400-500 with ZERO exceptions**
(bob=0→V0, bob=1→V1, bob=2→V2). Assemblies (pal5, anchor + `tile@(dx,dy)`):

| variant | bob.state | rep | anchor | assembly |
|---|---|---|---|---|
| **V0 rest** | 0 | f400 | (110,108) | `00E@(16,0) 00A@(0,7) 008@(16,13) 00C@(16,28)` |
| **V1 down** | 1 | f408 | (110,107) | `00E@(16,0) 00A@(0,9) 008@(16,13) 00C@(16,29)` |
| **V2 side** | 2 | f420 | (109,108) | `00E@(16,0) 00A@(0,7) 008@(16,13) 00C@(17,28)` |

Cadence = the SAME anim-slot phase that drives the OAM bob (V0↔V1↔V2 oscillation, dwell 4-12f irregular; run
sequence f400-500: V0 f400-404·V1 f408-416·V2 f420-424·V1 f428-436·V0 f440-444·V1 f448-456·V2 f460-468·V1
f472-500). So the replay is **free**: render the ACTIVE PC's idle from the variant for `ctCronoIdleBob[gi].state`
(OAM offset still the bob's dx,dy). **MARLE (WAITING) = FULLY STATIC** — re-confirmed over 200 f: OAM
byte-identical f400-600 AND tile-content **1 variant, none streaming** (cells 0x20-0x37, f400-560). Only the
ACTIVE PC animates. Baked `ctCronoIdleAddKf[3]` + `ctCronoIdleAddTiles` + `ctCronoIdleStateToKf[3]`.
**Verify: CI0 339/339 · CI1 351/351 · CI2 335/335 = 1025/1025 byte-exact** (bracket pixels excluded).

## A5.2 — T3: GATO idle RE-DERIVED on a 400-frame no-attack window — the "strange" = a LOOPED ENTRANCE-SETTLE
**The tile-0x006 "foot quad" was a mis-ID:** it is the active-PC selection **BRACKET** (pulses around Crono,
follows `$95D5`; own asset `battle_bracket.h`). The concurrent `_gen_battleanim.py` already neutralized the
spurious `footDY` (now 0). So **GATO = the 12 body cells only; it has NO feet.** But the **body-bob is still
wrong**: the shipped `ctGatoIdleTraj` loops **f384-504**, whose rows **f384-433 are the ENTRANCE-SETTLE**
(GATO's whole 12-cell body sinks: anchor y 57→63, `dy` −5..+1 = **6 px**) — looped forever + a **−4 px wrap
pop** at f504→f384. That perpetual 6px sink is the reported *strange movement*.

**Re-derivation, LONG contiguous window.** No natural window exists (GATO attacks ~f505); so: peek `f400` via
`PEEK_SAVE`, patch `$AFAC`/`$AFB1`=0xFF (Marle+GATO countdowns never reach 0), monitor NO-A → a **clean ~400-f
contiguous idle** (`$AFB1` 0xFE→0x49, never triggers). Truth (monitor-exact):
- **Body bob = ±1 px** (steady anchor y 62↔63), NOT 6 px. The whole 12-cell body translates together (top
  `08E@(16,0)` and bottom `082/080@(…,48)` move as one — a subtle vertical breathe, no crouch/compression).
- **Arm A↔B claw-swap, A-DOMINANT:** state A (`068/06A` top-right dy10/25, `082/080` bottom dy48) ~80f, state B
  (`084/086` top-right, `06A/068` down to dy48, `082/080` mid dy32) ~33f; **period ~115 f** (A[f72-153] B[f154-186]).
- **Periodicity now settled** (S4.5 OPEN): TWO natural cycles observed (f388-500, **f556-668**) + the no-attack
  window prove the loop; cycle-2 showed **96 f of pure state A** (the swap is anim-slot-phased, recurs
  ~112-128 f — it is NOT a fixed 121-f element, and NOT purely pre-attack).

**Side-by-side divergence (committed vs corrected):** body-top Y bob **CURRENT 57..63 = 6px** (loops the
entrance-settle) vs **CORRECTED 62..63 = 1px**; loop-wrap pop **CURRENT −4px** vs **CORRECTED −1px**; arm=B frac
**CURRENT 32%** vs **CORRECTED 29%** (footDY already 0 in both — bracket fix). Baked **`ctGatoIdleTrajV2[115]`**
(steady f72-186, one A+B period, loops A→A; footDY=0) — a drop-in replacement for `ctGatoIdleTraj`.

## A5.3 — Open questions (ADD.5)
- **Swap trigger law:** state B recurs ~112-128 f in the no-attack window AND appears pre-attack in the natural
  fight; whether it is a free idle-tic vs. a windup tell (or both) is the anim-slot-phase (`$0520+n*0xC`,
  `FD:E3A9`) S4 boundary — cadence measured, driver not pinned. V2 bakes one observed period (replay-as-observed).
- **Crono idle within-variant stream:** the 3 variants are baked at rep frames; any sub-frame stream inside a
  variant is the S4 boundary (unobserved at step-4 — the 3 variants were clean run-lengths).
- **Other PCs' idle streams:** only Crono (active) + Marle (waiting) captured; a 3rd party member unmeasured.

---

# ADDENDUM 6 — T1: the ACTIVE-PC SELECTION BRACKET fully derived (identity · pulse · lifecycle · audit · bake)

Completes the tile-0x006 re-ID that ADD.5 flagged in passing. User: *"white frame around crono … indicates that
the ATB bar is charged … they never vanish … there is an animation"*. The white frame is **OAM slots #0-3 = tile 0x006**
(pal1-attr, prio3), MISID'd by S2/S3/ADD4 as "GATO's foot quad" and baked into GATO's pal1 group → our engine drew
it permanently (the "they never vanish"). It is the selection bracket around the **ACTIVE PC** (`$95D5`). Method:
`$7E0700` `--monitor`/`--peek` on `gato_press_action` + `forest_battle_3hetake`; CGRAM/fb read per frame.

## A6.1 — Identity + geometry
Tile 0x006 = ONE L-corner (ink in the top-left 8×8 of the 16×16 cell); the 4 sprites mirror it into an open frame:
`#0 (0,0)hf0vf0 · #1 (+8,0)hf1vf0 · #2 (0,+24)hf0vf1 · #3 (+8,+24)hf1vf1` (offsets from the TL origin = slot0). Ink
uses pal1 indices **13/14/15** only.

## A6.2 — Position follows `$95D5`, NOT GATO (baked per fight/PC)
| fight | `$95D5` | bracket TL origin |
|---|---|---|
| GATO | 0 Crono | (122,108) | GATO | 1 Marle | (164,75) |
| Forest | 0 Crono | (148,35) | Forest | 1 Marle | (115,108) |
The GATO-fight bracket at y108-148 sits **around CRONO, BELOW GATO's body (y58-111)** — proof it is not GATO's feet.
Home-relative (cluster offset varies: GATO-Crono Δ+13,0; Forest-Crono Δ+4,−5); the home→origin offset is the S3
formation boundary.

## A6.3 — Animation = a PALETTE PULSE (position is byte-static)
OAM #0-3 are byte-identical f381→f454 (`--diff`, no move). The pulse: **idx13 = 0x7FFF const white** (inner line),
**idx15 = 0x14A5 const dark grey** (outer edge), **idx14 (CGRAM 158) = animated grey** triangle-waving
**`v = 0..31..0` (word `v|v<<5|v<<10`), ±1/frame, PERIOD 62** (trough v0 f424, peak v31 f393/f455). Byte-verified
f382-455: `v(F)=let p=(F-424)%62; (p if p<=31 else 62-p)`. Free-running global counter (phase source OPEN, waveform
exact). This is the "animation"; the CGRAM snapshot's constant pal1[0..7] hid it (the animated entry is idx14).

## A6.4 — Lifecycle (`--monitor "0700:16,95D5:1,9609:1"`)
- **APPEARS** the frame `$95D5` 0xFF→valid PC (command opens): GATO f381. Absent while `$95D5`==0xFF (OAM #0-3 parked).
- **FOLLOWS `$95D5`**: Crono acts → `$95D5`→1 (Marle, `C1:058D`) → bracket → Marle (164,75) f508. L/R switch (T2) →
  bracket Crono↔Marle.
- **Att-confirm → target-select** (`$9609`=1, `C1/129C`; hand builder `C1/1F79`): OAM #0-3 tiles **0x006 → 0x000**
  (the HAND), slot0 → the selected enemy. Bracket ⇄ target-hand = the SAME 4 OAM slots (dual role).
- **DISAPPEARS** when `$95D5`→0xFF.

## A6.5 — Audit + re-bake (deliverable)
| header | tile-0x006 | verdict |
|---|---|---|
| `battle_stance.h` | 4 (as GATO nspr16) | **CONTAM** → re-baked: GATO 16→12, `ctBattleStanceObj` 24→**20**, verify **2516/2516** (bracket EXCL 72) |
| `battle_anim.h` | 12 (3 GATO kf×4) + spurious footDY | **CONTAM** → re-baked: `ctBattleAnimObj` 72→**60**, GATO kf 16→12, footDY→0, verify **7472/7472** (EXCL 48/72/76) |
| `battle_entrance.h` · `battle_end.h` · `battle_fx.h` | 0 | CLEAN |
| `battle_hetake.h` | 0 OAM (bakes tile 0x000 hand) | CLEAN (right role) |
⚠ `ctGatoIdleTraj.footDY` was the STATIC bracket vs GATO's moving anchor (ADD4's 50→45 "foot-bob" was spurious) —
neutralized. NEW **`src/battle_bracket.h`** (`tools/_gen_battlebracket.py`, gitignored): the 4-corner assembly, the
tile quad, the pulse palette + 62-frame `ctBracketPulse` triangle, the `ctBracketPos` origins. Verify **624/624
bracket-ink exact** (4 pulse phases v31/19/5/0). `Belthasar` re-builds clean. **NOT wired** (per task) — draw as its
own asset gated on A6.4 (show iff `$95D5`≠0xFF & `$9609`==0; pos = `$95D5`'s home; →hand on `$9609`=1).

---

# ADDENDUM 7 — T3a: battle ENTRY position is a masked CUT (the "little teleport"), not a walk/slide

User: *"when entering battle mode, crono does a little teleport"*. **The field→battle position change is an
instantaneous CUT.** Crono's pal5 cluster is at the battle-staging position **(126,96)** from the EARLIEST battle
frame — CONSTANT across f214/218/222/226/234/250/270/280-290 (`--peek "100:8,0:N-8"`). There is **no per-frame path**
from a field position: the module rebuilds the OAM from the battle-formation staging (field arrays cleared `C1:CE04`
~f288, the entry-STANCE ADDENDUM). The cut is **masked by the battle-entry screen transition** (`CD/0000` swirl/mosaic,
`battle_boot.md`; first HUD pixels ~f295, `battle_hud_render.md` Q6) — the raw jump is never seen in the oracle. AFTER
the cut, the animated weapon-draw ENTRANCE morph (ADDENDUM 3, f290-390) plays from staging (126,96) → settled stance
(110,108) — that morph IS an animation; the field→battle jump is NOT. In the GATO fight field-pos ≈ staging ≈ centre
(small cut); the visible "teleport" is larger when the lead's field position is farther from the formation anchor.
**Fix direction:** place combatants at the formation staging DURING the masked entry transition — there is no
slide/walk table to replay. **OPEN:** a clean forest field-pos-before-cut capture (larger delta) — the
`forest_holdleft_pre` peek did not re-trigger headless (positions constant f220-260); the CUT verdict rests on the
GATO capture + the `C1:CE04` OAM-rebuild mechanism.

---

# ADDENDUM 8 — T1: the FORMATION→SCREEN ANCHOR MATH (closes S3 + ADD.7 boundary): `screen = 16·rel + fine`, camera folded into `rel`

Closes the **S3 "Battle-formation anchor math"** OPEN and the ADD.7 "OPEN: clean forest capture" — the reason the
baked absolute anchors put **the forest enemies at the WRONG position** (user: *"in the forest the enemies are
going to the wrong position"*). Our render bakes the GATO-capture absolute min-corners (`ctBattleStanceActor[]`
Crono(109,108)/Marle(164,63)/GATO(104,63); `ctHetakeIdle[]` (104,35)/(72,67)/(152,102)) and reuses them in EVERY
fight → any fight whose formation differs renders combatants at the GATO/forest positions instead of their own.
**The anchor is not a bake — it is COMPUTED from the staged record.** Derived from both fights, ROM+oracle only.

Method: peek both fights to a settled frame, decode the `$7E0700` OAM shadow (== hardware `.oam`, verified 4/4
clusters both fights) into per-combatant min-corner anchors (`t1detail.py`), read the staged records
`$29B0+5·seat` (PC) / `$29BF+12·slot` (enemy) and the arena origin `$1D0A/$1D0E` (`ct_probe --read`). GATO =
`gato_press_action` A-boot `--btnscript "100:8,0:452"`; FOREST = `forest_holdleft_pre` `--btnscript "40:20,0:440"`.

## A8.1 — THE LAW (byte-exact): `footScreen = (16·relX + fineX, 16·relY + fineY)`, K = 0

The staged record's `{relTileX, relTileY, fineX, fineY}` map DIRECTLY to a screen **foot/hotspot** with **no extra
constant** (K=0); the drawn cluster's min-corner = that foot **plus a per-sprite EXTENT** (how far the sprite art
reaches up-left from its foot). Proof table (`t1proof.py`), `foot = 16·rel + fine`, `extent = min-corner − foot`:

```
=== GATO fight ===
  combatant   rel      fine    foot=16rel+fine  min-corner  extent
  Crono s0   ( 8, 9)  ( 6, 0)  (134,144)        (109,108)   (-25,-36)
  Marle s1   (11, 6)  ( 0,15)  (176,111)        (164, 63)   (-12,-48)
  GATO  sl3  ( 8, 6)  ( 8,15)  (136,111)        (104, 63)   (-32,-48)
=== FOREST fight ===
  Crono s0   (10, 4)  ( 0, 7)  (160, 71)        (143, 40)   (-17,-31)
  Marle s1   ( 7, 9)  (15, 0)  (127,144)        (107, 96)   (-20,-48)
  Het  sl0   ( 7, 3)  ( 8,15)  (120, 63)        (104, 35)   (-16,-28)
  Het  sl1   ( 5, 5)  ( 8,15)  ( 88, 95)        ( 72, 67)   (-16,-28)
  Het  sl2   (10, 7)  ( 8,15)  (168,127)        (152,102)   (-16,-25)
```

- **ENEMIES — byte-exact & camera-independent.** The 3 forest Hetakes have DIFFERENT `rel` (from their 3 field
  tiles) but the IDENTICAL extent **(−16,−28)** (slot2 is (−16,−25) = its idle-phase Y+3, T1.1) → `min-corner =
  16·rel + fine + (−16,−28)` reproduces all three EXACTLY. GATO's extent is **(−32,−48)** (a bigger 12-cell sprite
  reaches further up-left). Enemy `fine` is a **fixed formation constant (8,15)** in BOTH fights (center-X /
  bottom-Y of the origin tile = the foot); `rel` is the LIVE field tile. So a Hetake at field tile T lands at
  `16·(T−originTile)+8/15+(−16,−28)` — its own screen position, in ANY framing.
- **PCs — foot is camera-correct in BOTH fights.** Marle's foot `16·rel+fine` = **(176,111)** GATO / **(127,144)**
  forest, each == the bottom of her sprite cluster (her `026`/`024` bottom cells reach exactly those rows). Crono's
  foot = **(134,144)**/(160,72≈forest) likewise = his foot. The foot (hence the position) is placed correctly by
  the record regardless of framing. Marle's Y-extent is **−48 in BOTH** fights; her X-extent (−12 vs −20) and
  Crono's whole extent DIFFER between fights **because the PC POSE differs** (GATO = spread sword-stance; the
  forest capture shows a compact pose — a stance/animation matter, NOT position; see A8.3).

## A8.2 — WHY camera-independent: the camera is folded into `rel` at staging (arena origin `$1D0A/$1D0E`)

The task's hypothesized `screen = world − cameraPx + K` is CONFIRMED with `K=0`, but the subtraction happens at
**staging time**, not render time: the field→battle handoff (`field.c:2459-2497`, ROM `C0/0430`, DP `$DB/$DD`)
computes `relTileX = (objPx>>4) − oTX`, `oTX = ($1D0A>>1)`, where **`$1D0A/$1D0E` = the arena origin = the settled
field-camera scroll in 8px units** (`op-0xE7` settles it before the battle; `camPx = $1D0A·8`). So
`16·rel = 16·objTile − 16·originTile = objWorld − cameraPx` — the camera is ALREADY subtracted inside `rel`. The
battle render therefore needs ONLY the record: `screen = 16·rel + fine`. (In-battle `$1D0A/$1D0E` are reused for
other purposes — read as 0/0x788F/0x008F — so the render must NOT re-read them; the origin lives in `rel`.)
Pre-battle origins observed: GATO `$1D0A=0,$1D0E=3`; forest-pre `$1D0A=3,$1D0E=60` (the settled value at trigger
differs after the LEFT-walk — irrelevant, it is already baked into the staged `rel`, which our engine reproduces
**byte-exact vs the oracle** — `field.c:4649-4657` BTCHK `$29BF` forest recs).

## A8.3 — THE FIX (deliverable): replace every baked absolute anchor with `16·rel + fine + extent`

`ctBattleStanceActor[]` (battle_stance.h:45) and `ctHetakeIdle[]` (battle_hetake.h) bake the min-corner as an
ABSOLUTE — replace with a COMPUTED anchor:
```
anchorX = 16·relX + fineX + extentX(combatant)      // rel,fine LIVE from the staged record:
anchorY = 16·relY + fineY + extentY(combatant)      //   PC seat  -> $29B0 + 5·seat  (relX,relY,fineX,fineY,prio)
                                                    //   enemy slot-> $29BF +12·slot (relX,relY,fineX,fineY,prio,id)
```
`extent` = the per-sprite baked offset from foot to the bake's min-corner (measured above): **Hetake (−16,−28),
GATO (−32,−48), Crono (−25,−36), Marle (−12,−48)** — capture-once constants (they encode the sprite's art extent,
NOT the framing). The OAM assembly stays baked relative to `anchor` exactly as now. This makes the forest enemies
land at their own (104,35)/(72,67)/(152,102) via their live `rel` instead of the GATO positions, and is valid in
ANY camera framing. (Cleaner alternative: anchor the assembly at the FOOT `16·rel+fine` and bake the assembly
foot-relative — same result, one fewer constant.)

## A8.4 — Open (T1)
- **PC extent is POSE-dependent** (Crono −25,−36 GATO vs −17,−31 forest; the pose differs between captures). The
  FOOT (position) is correct both ways; but a byte-exact PC *min-corner* in the forest needs the forest PC
  **stance** (the spliced/booted forest capture renders the PCs in a non-sword pose over un-streamed BG — the
  render is unreliable for PC pixels, enemies are byte-exact). The POSITION fix (A8.3) is pose-independent; the
  forest PC STANCE bake is a separate open item (only the GATO PC stance is byte-verified, S2/S6).
- **Cross-fight enemy extent** proven only within one fight per type (3 Hetakes share it); a second Hetake fight
  would re-confirm the extent is sprite-fixed (the mechanism — `rel` from field tile — guarantees it).
- **slot2 Hetake Y (−25 vs −28):** the +3 is its idle-anim phase at capture (T1.1/T1.2 per-slot independent idle
  stream), not a formula residue.

---

# ADDENDUM 8 — T2 VERDICT: GATO idle `ctGatoIdleTrajV2` was MIS-DERIVED (period 115 vs the true 120) — the residual "strange"

User: *"gato's movement in battle looks strange"*, **persisting after** the ADD.5 `ctGatoIdleTrajV2`
fix. Direct side-by-side of OUR replay vs a fresh **contiguous** oracle idle settles which of the three
hypotheses holds. **Method** = the ADD.5 no-attack capture, extended: peek f400 (`PEEK_SAVE`), patch
`$AFAC=$AFB1=0xFF` (Marle+GATO countdowns never fire), `--monitor "0700:512,0900:32"` for **400 frames**
(3+ periods; GATO stays alive+idle all 400). Per frame: GATO pal1 cluster min-corner `ay` (== cell 0x8E top;
cross-checked cell 0x80 bottom moves with it ⇒ whole-body), and arm state (cell 0x68 low ⇒ swap).

## 8.1 — the oracle idle is cleanly PERIODIC, period = **120** (not aperiodic, not 115)
Over f0-399 the steady idle (after a ~67-f settle-tail) repeats **exactly every 120 frames**:
- **arm swap:** arm-B onsets at **f154, f274, f394** (Δ = **120**); arm-A run = **87 f**, arm-B run = **33 f**.
- **body-top bob:** `ay` = **62↔63 (±1 px)** whole-body; within one 120-f period **62(27f)/63(33f)/62(27f)/
  63(33f)**, arm flips to B only in the *last* 63-block. Three identical cycles observed ⇒ **periodic, NOT
  aperiodic** (settles the ADD.4 S4.5 + ADD.5 A5.3 "periodicity/​swap-trigger OPEN": it is a fixed 120-f loop).

## 8.2 — the replay + keyframes are CORRECT (ruled out)
- **Replay math** (`src/field.c:1862-1869`): `gi=(frames−COMBAT_LIVE)%CT_GATO_IDLE2_N`; body placed at
  `(104+dx, 62+dy)` normalized `s->y+(62+dy)−kf->anchorY`. Both kf6 (anchorY 58) and kf7 (anchorY 63) normalize
  to body-top `62+dy` — **correct** for the different baked anchors.
- **Keyframes** (`battle_anim.h` kf6/kf7): the captured settled **arm-A** assembly `068@(47,11) 06A@(47,27)
  06C@(0,27) 06E@(0,11) 080@(30,48) 082@(15,48) 084@(32,32) 086@(16,32) 088@(32,16) 08A@(16,16) 08C@(32,0)
  08E@(16,0)` == **kf6 I0_armsRest** min-corner-relative, EXACTLY; captured **arm-B** == **kf7 I1_armsSwap**,
  EXACTLY (tiles byte-verified in ADD.6). kf6 was baked at f395 (ay 58, mid entrance-settle) but the settle is a
  **pure translation** (relative assembly invariant) ⇒ kf6 is a valid steady arm-A keyframe. **Not the bug.**

## 8.3 — the bug: the TRAJECTORY TABLE clips 5 frames ⇒ period 115 (should be 120)
`ctGatoIdleTrajV2` has `CT_GATO_IDLE2_N = 115` = arm-A **82 f** + arm-B 33 f. The generator's window
`GATO_V2_F0 = 72` began **5 frames INTO** the arm-A phase (which truly starts at **f67**), truncating arm-A
82↔87 and the period **115↔120**. The first `dy=0/arm=0` block is **22 rows** but must be **27**. (The other
blocks 33/27/33 already match; footDY=0 was already correct from ADD.6's bracket fix.)

**Side-by-side verify** (corrected period-120 vs current 115 vs the oracle steady window f67-399, on per-frame
`(body-top dy, arm)`, fixed phase):
```
CORRECTED (N=120): 333/333 exact,  no divergence
CURRENT   (N=115): 288/333, DRIFTS  (best-phase); predicts arm-B onsets 159/274/389 vs oracle 154/274/394
```
The 5-f/period drift is the residual *strange movement*: our claw swaps ~6 % early and the loop slowly
slides against the true 120-f cadence.

## 8.4 — VERDICT + the fix (corrected data)
**Verdict: the V2 TABLE is mis-derived (period 115 vs the oracle's 120); replay + keyframes are byte-correct;
the oracle idle is periodic (120), not aperiodic.** Fix = a **one-line generator constant**
`tools/_gen_battleanim_add.py`: **`GATO_V2_F0 = 72 → 67`** (F1 stays 186 ⇒ exactly one 120-f period). Its own
derivation with F0=67 re-emits **N=120**, blocks **27×{0,0}+33×{1,0}+27×{0,0}+33×{1,1}**, footDY=0, and matches
the oracle **120/120**. `src/field.c` needs **no edit** — it reads `CT_GATO_IDLE2_N`, so regenerating
`battle_anim_add.h` auto-adopts 120. (NOT committed/wired per task; corrected table also written to
`scratchpad/ctGatoIdleTrajV2_corrected.txt`.)

## 8.5 — Open (T2)
- **Arm-swap TRIGGER law** (why state B recurs every 120 f) — still the anim-slot-phase boundary
  (`$0520+n*0xC`, `FD:E3A9`); V2 replays the observed 120-f period (measured, not derived-from-phase).
- **Settle-tail phase** (the ~67-f rise 58→63 at combat-entry) — the entrance→idle handoff; the loop starts at
  the arm-A rest phase (gi 0), which is a valid idle phase; the exact entry phase is the S3 formation boundary.
