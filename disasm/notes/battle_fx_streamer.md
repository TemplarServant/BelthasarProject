# BATTLE FX LAYER — the tech-effect graphics: FD/E39C streamer + the OBJ effect-object engine (Cyclone's whirlwind)

> **2026-09-17: the three bakes this note describes are RETIRED.** src/data/battle_fx_whirl.h, battle_fx_ice.h and
> battle_fx_song.h (and battleWhirlRender / battleIceRender / battleSongNotesRender) are gone. The whirl, the Ice
> shards and the song notes are drawn by the ROM's effect-object machine: C1/48EC's animation table, CC/F278's
> records, the links CF/ECC2 and the compose CF/EEEB (battle_fx_sprites.md section 8, graded record for record
> against the cartridge). The ledgers below stay as the measurements they were; the "fixture" and "render" sections
> are history.

Derives, from the ROM disasm (`tools/dis.py`, HiROM flat = `(bank−0xC0)<<16|addr`) + the LakeSnes oracle
(`ct_probe.exe` = `build/ct.exe`, base `gato_cyclone.state`) **only**, the battle **tech-effect visual layer** —
the piece that draws Cyclone's whirlwind, the song's notes, Ice's crystals. Closes the standing OPENs
`battle_tech_slice.md` **T3** ("the CE super-subscript `CE/D613` + the spin EFFECT layer — the pixels of the
whirlwind, not derivable from the CD/CE bytes alone") and `battle_animeng_objslot.md` **§9 / O5** (the streamer's
role + DMA). **Zero invention** — every field byte-cited, every value oracle-measured on the Cyclone cast.

> **HEADLINE (the T3 answer):** the whirlwind is **NOT streamer graphics** and **NOT a BG effect** — it is an
> **OBJ "effect object"** drawn by the SAME multi-track anim interpreter (`C1/4CA2`) that drives the combatant
> poses, on its **tracks 8-15** (the interpreter's tracks 0-7 = combatants, **8-15 = effect objects**). The
> FD/E39C streamer's ONLY role in Cyclone is a **mode-5 palette OSCILLATOR on slots 8-9** (two flash bursts) that
> recolours the whirlwind's palette — exactly the mechanism it uses for the song notes. So the effect layer is
> two independent pieces: (a) the OBJ effect-object geometry [the interpreter's tracks 8-15], (b) the palette
> shimmer [the FD/E39C streamer]. Both are DERIVED here; the byte-exact PORT is gated on two named sheet/move
> OPENs (§7), so this session lands the derivation + the concrete OAM ledger (the render spec), not an
> approximate visual.

---

## ✅ LANDED — the whirlwind RENDERS, byte-exact (src/battle_fx_whirl.h + field.c `battleWhirlRender`)

Both PORT gates (O-A sheet, O-B mover) are CLOSED as an **oracle FIXTURE** (the sanctioned interim, the
`ct_charbase.h` precedent) — byte-verbatim capture, not invention — and the whirlwind now renders in the
battle compose. **Verified byte-exact vs the oracle fb: 309/309 opaque whirlwind px, 3 orientations** (asm A
upright @ peek-f600, asm B upright @ peek-f540, asm B 180-flip @ peek-f514 — our render == the oracle's own
OAM rendered from its VRAM/CGRAM, identity OAM→fb transform). Pinned: `field-battle-tech-cyclone-whirl-asma`
(rel 161, fnv F25F32B7) + `-whirl-asmb-flip` (rel 75, fnv DE4D56C0), determinism ×2. Suite quick 88/0/1.

**Three findings that refined §3-4 (all oracle-measured):**

1. **The spin is FLIP-DRIVEN — 4 orientations, not "2 assemblies".** §4.2 undercounted. The blade rotates via
   OAM **H/V-flip flags**: the same two tile sets appear BOTH upright AND 180°-rotated (hf=vf=1, tiles reordered
   BR↔TL). So the spin cycle is **O0 asm-A-upright → O1 asm-B-upright → O2 asm-A-180 → O3 asm-B-180**, ~4f each.
   The 4 orientations are a fixed 4×4 table (`ctBattleWhirlOrient`); each = the 4 sprites {cellDX,cellDY,tile,hf,vf}.

2. **The sweep FLICKER-MULTIPLEXES.** A per-frame `--peek` OAM sweep (peek-f499..640, EVERY frame) shows exactly
   **one 32×32 quad per frame** (n=4 sprites always), but its (anchor, orientation) jumps ~30px between adjacent
   frames — 3-4 effect objects (the Y8/Y11/Y12 tracks) time-sliced through the SAME 4 OAM slots (classic SNES
   flicker). So the faithful render target is the **per-frame OAM replay** (a 142-entry position+orientation
   table, `ctBattleWhirlFrame`, rel 60..201), NOT a single smooth curve. The monitor `$A240[0]` DID trace a clean
   diamond (2px/f, 15f edges, center ≈(135,116) ≈ GATO's midsection) — that is ONE of the multiplexed objects;
   the O-B closed-form mover remains structurally described but the render uses the byte-exact table (mandate-safe).

3. **The mode-5 palette shimmer is a NO-OP for the visible render.** pal0 (CGRAM 128-143) is **CONSTANT** at its
   shimmered value `0000 7FFF 5F1B 0466 08C9 7FFF 33DF 1DB9 …` across the ENTIRE visible window (measured f540,
   560, 580, 600, 620, 650 — all identical). The FD/E39C mode-5 osc (§2) clamps pal0 there after burst #1 and
   leaves it; so the whirlwind's palette is a single baked constant (byte-exact at every pin). The dynamic
   oscillation only touches the windup/fade edges (f473-504 / f640-671) where the blade isn't cleanly visible —
   a documented cosmetic OPEN, NOT needed for the render.

**Timeline mapping (nailed):** the freeze/anim `$A013:0→1` at **peek-f439** == our `actStart` (rel 0). Our
`--battletest` dispatches Cyclone at f250 (pin "techtrace f250"), so **rel = h->frames − actStart = peek_frame −
439**, and the whirlwind is live **rel 60..201** (peek-f499..640), inside the 313f span; popup born rel 227.
An OBJ at OAM (x,y) lands at our fb (x,y) directly (identity — verified against the cyc_f600 fb, GATO/Crono/
whirlwind all align). Regen: `tools/_gen_battlefx_whirl.py` (patches gato_press_action.state $2837|=0x80, drives
the oracle, extracts tiles/pal/table).

---

## 0 — THE ORACLE LEDGER (Cyclone cast, `gato_cyclone.state`, all measured)

`gato_cyclone.state = gato_press_action.state + $2837|=0x80` (Crono learns Cyclone — the tech-slice patch).
Driver `--btnscript "100:8,0:372,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:330"` (Down→Tech, A, A, A). Freeze
f438→f751 = **313f** (the pinned `CT_SPAN_PC_TECH_CYCLONE`).

```
f438  $9877=01 $A013=01   Cyclone anim + freeze ON              (CD/45A6→CD/5EF0→CE/0909)
f473  slots 8,9 -> mode 5  FD/E39C palette-osc burst #1 ($0580/$058C +0=0x50)   [--monitor 0520:1,052C:1,...]
f504  slots 8,9 -> mode 0  (burst #1 releases)
~f540 whirlwind VISIBLE    OBJ effect object, pal0, prio3, 32x32 (§4 OAM ledger)
f560  GATO body -> pal4    hurt-flash (strike hit) + whirlwind assembly B
f640  slots 8,9 -> mode 5  palette-osc burst #2
f665  $A3D1=01             ShowDamage popup BORN
f671  slots 8,9 -> mode 0  (burst #2 releases)
f723  $A3D1=00             popup done (58f life)
f751  $A013=00  $6130 76→55  span end + HP applied (−21)
```

The whirlwind's visible life is **~f540→f640** (100f), the "cyclone spin/multi-target sweep" the tech slice
§3.3 named as the 227f WINDUP's business end. The popup+tail (f665→f751) is the standard 58+28.

---

## 1 — THE TWO-LAYER ARCHITECTURE (the T3 resolution)

The battle sprite picture is built by THREE independent producers that only meet at the OBJ hardware:

| # | producer | drives | RAM | this dossier |
|---|---|---|---|---|
| 1 | **pose/assembly engine** (`battle_animeng_*`) | combatant BODIES (GATO, Crono, Marle) | `$1CA2/$1CC3` scripts, `$967B` asm | prior work |
| 2 | **the anim INTERPRETER `C1/4CA2` tracks 8-15** | **OBJ EFFECT OBJECTS** (whirlwind, slash-arc, notes-as-sprites) | `$A240/$A250` pos, `$A337` move, `$8E` animId | **§3, §4 — NEW** |
| 3 | **FD/E39C 12-slot streamer** | tile-stream + **palette OSC** on effect tiles | `$0520` slots | **§2 — Cyclone role NEW** |

The whirlwind's **geometry** is producer 2 (OBJ sprites). Its **palette shimmer** is producer 3 (mode-5 osc).
`battle_tech_slice.md` T3's "the spin EFFECT layer" is producer 2; its "same boundary as the song notes" is
producer 3. They are DECOUPLED — a port can build the geometry without the shimmer and vice-versa.

**Proof the whirlwind is OBJ, not streamer-tiles or BG** (oracle, §4): the effect sprites appear as NEW OAM
entries (pal0, prio3) whose (tile,pal) are absent from the pre-action OAM; the streamer slots stay **mode 0**
(no tile-stream, `+3` frameIdx frozen) the whole cast — they only ever go **mode 5** (palette), which writes
CGRAM not tiles. So no tile-streaming produces the whirlwind; the geometry is pure OAM from the interpreter.

---

## 2 — LAYER 3: THE FD/E39C STREAMER'S ROLE IN CYCLONE (mode-5 palette osc)

The 12-slot streamer model is fully derived in `battle_animeng_objslot.md` (12 slots × 12 bytes `$0520..$05AF`,
per-frame tick `FD/E3AC`, 8 mode handlers; modes 1&8 = ROM→buffer tile streamers, 2&3 = scrollers, **4-7 =
BGR555 palette oscillators**). This dossier adds its **Cyclone behaviour**, measured live:

- `--monitor "0520:1,052C:1,0538:1,0544:1,0550:1,055C:1,0568:1,0574:1,0580:1,058C:1,0598:1,05A4:1" --diff`:
  slots 8 (`$0580`) and 9 (`$058C`) flip to **mode 5** (`+0 = 0x50`) at **f473-504** and again **f640-671** —
  TWO bursts. Every other slot stays mode 0 all cast. This is byte-identically the song's slot-8-11 mode-5
  behaviour (`battle_animeng_objslot §6`).
- **What mode-5 does to the whirlwind:** the effect uses **OBJ palette 0**; its CGRAM (entries 128-143)
  changes across the cast — baseline `…08C9 1084 20EA 429A…` → f600 `…08C9 7FFF 33DF 1DB9…` (indices 5,6,7
  oscillate). Mode 5 = `FD/E64F` "component × phase >> …, fade toward black", clamp-and-stop
  (`battle_animeng_objslot §4`). So the streamer **pulses the whirlwind's palette** (a shimmer/flash on the
  slash colours), it does NOT draw the whirlwind.

**Conclusion:** for Cyclone the streamer is a **palette-only** contributor. A port that draws the whirlwind
geometry (§3-4) with a static pal0 is correct-but-un-shimmered; the mode-5 osc is a cosmetic polish layer,
the same one the song notes need. **O5 (the streamer→VRAM DMA site) is irrelevant to Cyclone** — no tile
streaming happens; the palette osc writes CGRAM directly.

---

## 3 — LAYER 2: THE OBJ EFFECT-OBJECT ENGINE (the whirlwind geometry)

The whirlwind is drawn by the multi-track anim interpreter `C1/4CA2` (fully derived in
`battle_span_emergence.md` §1: loop `C1/4090`, per-track tick `C1/4CA2`, opcode jump table **`C1/7A6B`**,
per-track cursor `$5DBD+track*4`). The NEW finding: **the interpreter's track index `$A010` selects between
COMBATANT and EFFECT-OBJECT handling** in every draw/move opcode.

### 3.1 The track split (byte-cited, three opcodes)

Every graphics op branches on `$A010` (the current track): `CMP #08 / BCS →effect` and `CMP #04 / BEQ →multi`:

| op | handler | track < 4 (combatant) | track == 4 | track ≥ 8 (**EFFECT OBJECT**) |
|---|---|---|---|---|
| **72 SETANIM** | `C1/5FDE` | `$1D0C,X/$1D23,X` combatant pos → `JSR C1/75CC` (`15FEE`) | multi-loop `$A018,Y` (`1600E`) | **`16034`: `$A2B6`=obj idx; pos `$A240,X/$A250,X`; `$8E`→`JSR C1/75CC`** |
| **98 DRAWANIM** | `C1/6299` | `$1D0C,X/$1D23,X` (`162BB`) | `C1/63F9` | **`C1/63FC`** |
| **A8 MOVE** | `C1/69C3` | `$A014,X`→move via `$C0F700` bearing (`169DA`) | `C1/6A2A` | **`C1/6A99`** |

So **tracks 0-7 = the up-to-8 combatants; tracks 8-15 = effect objects** with their OWN position array
`$A240/$A250` (vs combatants' `$1D0C/$1D23`), move flag `$A337`, and index `$A2B6`. This is the "C3/FD-family
effect subsystem" `battle_tech_slice.md` T3 flagged — it is the interpreter's high-track path, not a separate VM.

### 3.2 How an effect object is DRAWN (op 72, effect path `C1/16034`)

```
16034  LDA $A2B6 / ASL / TAX          ; effect-object index → X
16039  LDA $A240,X → $80 ; $A250,X → $81   ; ** effect-object screen position **
16043  LDX $A2B6 / LDA $8E / JSR C1/75CC   ; draw the assembly for animId $8E at ($80,$81)
```

`C1/75CC` is the **same assembly dispatcher combatants use** — `JSR ($79A1,X)`, X = `$8E*2` (the animId → OBJ
cell-assembly + tile-gather, `battle_animeng_builder`/`tiles`). So an effect object with `$8E`=3 draws
**assembly #3** = the whirlwind's OBJ cells (tiles §4) at `$A240/$A250`, palette/prio from the track's `60/73`
ops. The effect assemblies live in the same `$79A1` table space as combatant poses (a distinct animId range).

op 98 (`DRAWANIM`) is a parallel draw: helper `C1/762E` does `JSR ($79D3,X)` (X=arg2*2) — a SECOND
effect-assembly table `$79D3` (the main track's `98 02 03` / `98 02 19` spawn the flash/secondary cells).

### 3.3 How an effect object MOVES (op A8, effect path; op 85 angle)

op A8 (`C1/69C3`, arg = target/param → `$A5BD`) starts a move: it seeds `$A639,Y` (target), reads the bearing
table `$C0F700[$A5AA,Y]` → `$96DB,Y`, copies `$1D0C/$1D23` → `$A35D/$A370` (origin), sets `$A337,Y=1` (moving).
The per-frame mover then walks `$A240/$A250` toward the target (a straight interpolation on the `$C0F700`
bearing) — this is what **sweeps the whirlwind around the target**. op 85 (`C1/6258`) sets the sweep ANGLE;
ops `08-0F` set speed. The exact per-frame position law (the sweep curve) is the **effect-mover OPEN** (§7 O-B)
— only ~5 OAM sample points are captured here, not the closed-form, and it must not be invented.

### 3.4 The multi-strike SUPER (op 1E → CE/0000[ss*2]; op 49 sync)

op 1E (`C1/56CC`, byte-exact): `X=ss*2`; save return `$E6+2 → $A569,Y`; **`$E6 = CE/0000[ss*2]`, `$E8 = CE`**;
run the subscript; op 1F (`C1/56FB`) restores `$E6` from `$A569` (return). The subscripts (table `CE/0000`,
one word per ss) are SHORT (`ss=1D→CE/D612`, `ss=1E→CE/D617`, 5 bytes apart) mini-scripts that orchestrate the
**multi-target strike timing** via op 49 (`C1/5A70`, `49 idx delta`: `$5D80+idx += signed delta`, clamped) on
the cross-track SYNC counters `$5D9C-$5D9F` (the `24/21/22` WAIT ops poll these — `battle_span_emergence §3`).
`CE/D612` = `2D 80 02 F0 1F` (op 2D setCFFF=1 · op 80 · op F0 · op 1F return) — a tiny sync pulse, NOT the
"long `49 1F E2 11 49…` sweep" the tech slice quoted: that quote concatenated many 5-byte subscripts. So the
`1E` supers are the strike CHOREOGRAPHY (when each hit lands), not the whirlwind pixels.

---

## 4 — CYCLONE'S WHIRLWIND: the script + the OAM ledger (the render spec)

### 4.1 The anim script `CE/0909` (4 tracks, decoded byte-exact)

Reached by `$9877=1` → `CD/45A6[1×7][0]=1` → `CD/5EF0[1×2]=0x0909` → `CE/0909` (`battle_tech_slice §3.2`).
Header `98 80 80 00` = mask word1 `0x8098` (bits 15,7,4,3) → tracks **Y0, Y8, Y11, Y12**; pointers
`0917 094D 0960 0970`.

```
Y0  main (Crono)   CE/0917:  0C · 72·0D SETANIM(spin pose 0x0D) · 06·1D PLAY · 20·05 PAUSE ·
                             06·26 PLAY · 1E·1D SUPER(strike-sync#1) · D0 shadow · 27 · D9·30 ·
                             98·02·03 DRAWANIM(effect cells) · D1 · 36 SYNC++ · 1E·1F SUPER#2 ·
                             30·00·00 setSYNC · 78·FF SOUND · 36 · 1E·1E SUPER#3 · 26·0E·02 ·
                             0C · D0 · 98·02·19 DRAWANIM · D1 · 28 · DA · 1B·19 slide · 03·33 PLAY ·
                             06·03 PLAY · 24·03 WAIT · 20·14 PAUSE(20) · 2E setCFFF=0 · 01 ENDACT
Y12 effect object  CE/0970:  D9·30 · 24·01 WAIT · 1C·00 · 73·03 PRIO=3 · 72·03 SETANIM=3 ·
                             60·00 pal=0 · 20·04 PAUSE · 02 PLAY-loop · 00 RET      ← ** THE WHIRLWIND **
Y11 effect object  CE/0960:  24·01 WAIT · 20·14 PAUSE · 6C · 00 RET
Y8  effect object  CE/094D:  34·00 · 24·01 WAIT · 20·14 PAUSE · 6C · 00 RET
```

**Track Y12 is the whirlwind:** on a track ≥ 8 (effect object) it sets `PRIO=3, SETANIM $8E=3, pal=0,
PLAY-loop` — i.e. draw **assembly #3** at the effect position, **pal 0, prio 3, looping**. This matches the
oracle OAM byte-for-byte (pal0/prio3 below). The main track Y0 does Crono's spin pose + fires the 3 strike
supers + spawns the `98` flash cells. Y8/Y11 (op 6C, `C1/5F13`, a sync-gated `$8E=3` effect play) are the
secondary strike-sweep effect objects.

### 4.2 The OAM ledger (oracle `--peek`, effect sprites = content absent from the pre-action OAM)

`.oam` peek dumps decoded (128 sprites, [x,y,tile,attr] + highOAM + CGRAM). The whirlwind is **ONE 32×32
effect object = a 2×2 quad of 16×16 sprites**, palette 0, priority 3, alternating **two assemblies** (the
2-frame spin) and **sweeping position** (the circling):

```
frame   whirlwind OBJ (pal0 prio3)              GATO           note
f480    (none — tech name box "Spin-cut")                       windup / announce
f540    tiles [CA CC EA EC] @ (131-147, 63-79)                  spin phase B, high
f560    tiles [C6 C8 E6 E8] @ (136-152,101-117)  body → pal4    spin phase A, low-right + STRIKE flash
f580    tiles [CA CC EA EC] @ (111-127, 98-114)  body → pal4    spin phase B, low-left
f600    tiles [C6 C8 E6 E8] @ (120-136, 86-102)                 spin phase A, centre  (the clean whirlwind)
f620    tiles [CA CC EA EC] @ (113-129, 93-109)                 spin phase B, centre-left
f650    (fading)                                 body → pal4    second strike flash
f665    (none)                                                  popup born
```

- **Assembly A** = OBJ tiles `C6 C8 E6 E8` (the 16×16 quad → VRAM tiles C6/C7/D6/D7, C8/C9/D8/D9, E6/E7/F6/F7,
  E8/E9/F8/F9), **Assembly B** = tiles `CA CC EA EC`. The two alternate every ~20f = the **2-frame spin
  cycle** (= `PLAY-loop` on Y12 flipping animId-3's two sub-frames).
- The 5 positions trace a loop around GATO's midsection (128,94) — the **cyclone circling** (the op-A8/85 sweep).
- **GATO flashes pal4** (the hurt-white swap, `battle_actionfx` P3) at f560 and f650 = the two strike hits
  (driven by the `1E` supers / `98` flash).
- Palette: **pal0** (CGRAM 128-143), indices 5-7 oscillated by the mode-5 streamer (§2). The whirlwind's colours
  are golden/white (`7FFF 5F1B 0466 08C9 …`).

The whirlwind's OBJ TILES (`C6-F9` region) are **dynamically loaded** (they differ from the pre-action VRAM),
via the same assembly tile-gather path as combatant poses (`C1/1C4A → $7E63AD → DMA`, `battle_animeng_tiles`)
applied to the effect object — sourced from an **effects sheet** (the OPEN §7 O-A).

---

## 5 — THE FRAME LEDGER (implementation timeline, oracle-anchored)

```
f438  freeze ON; $9877=1; interpreter starts CE/0909 (4 tracks armed)
f438-~540  WINDUP: Crono approach mover + spin pose (72·0D); tracks arm the effect objects
f473-504   streamer slots 8,9 mode-5 (palette shimmer burst #1)
~f540      effect object #1 (Y12) visible: assembly-3 whirlwind, pal0 prio3, at melee foot
f540-640   whirlwind SPINS (assembly A↔B every ~20f) + SWEEPS around GATO (op-A8/85 mover)
f560,f650  GATO pal4 hurt-flash (the two strikes, via 1E supers)
f640-671   streamer slots 8,9 mode-5 (shimmer burst #2)
f665       op-50 ShowDamage → popup born ($A3D1=1)
f665-723   popup 58f life
f723-751   tail 28f (multi-target completion)
f751       EndTech; HP applied (GATO 76→55); freeze OFF (span 313f)
```

---

## 6 — WHAT A PORT NEEDS (the spec) + PHASE-2 ASSESSMENT

Our engine software-blits cells at a live foot (`battleBlitCell`, no OAM/VRAM). To render the whirlwind it must:

1. **An effect-object slot** (analogue of the interpreter's tracks 8-15): a position `(x,y)` seeded at Crono's
   melee foot, an animId (=3), pal 0, prio 3, alternating two assemblies every ~20f, swept around the target.
2. **The two whirlwind assemblies** (A = tiles C6/C8/E6/E8 quad; B = CA/CC/EA/EC) — their cell (dx,dy) offsets +
   the tile PIXELS. Source = the effects sheet (OPEN O-A) → the sanctioned INTERIM is to **bake the two
   assemblies from an oracle OAM+VRAM dump** (exactly the `ct_charbase.h` / `$7F8000`-sheet fixture precedent):
   the OAM gives the cell layout, the VRAM the 32×32 pal0 pixels, at f600 (asm A) and f540 (asm B).
3. **The sweep positions** — the per-frame `$A240/$A250` walk (op-A8 bearing interpolation). **This is the OPEN
   (O-B):** only 5 sample points are captured; the closed-form sweep is the effect-mover subsystem, un-derived.
4. **(cosmetic)** pal0 mode-5 shimmer (the streamer) — optional polish.
5. Fire it for the derived window (~f540→f640 within the 313f span), with the two GATO pal4 strike flashes
   already handled by `battle_actionfx`.

**Phase-2 verdict: DO NOT ship an approximate whirlwind this session.** A byte-exact render is blocked on TWO
un-derived inputs — the **effects-sheet ROM source + animId-3 assembly** (O-A, the same boundary as the combatant
sheet decompressor / `$922D`) and the **effect-mover sweep law** (O-B, only sampled). Rendering it now would
require inventing the sweep curve and guessing the sheet — a mandate violation. The honest deliverable is THIS
derivation + the OAM ledger (§4, the exact render target) + the interim-fixture path (bake the 2 assemblies from
the oracle). When O-A/O-B close (or the bake-fixture is accepted), §6.1-6.5 is a mechanical port that reproduces
the ledger; pin it with a battleshot fnv at f600 (assembly A, clean) + f540 (assembly B) — the determinism-×2
check.

---

## 7 — OPENS / BOUNDARIES

| # | item | status |
|---|---|---|
| **O-A** | **the effects SHEET** (whirlwind OBJ tiles C6-F9, pal0) ROM source + the **animId-3 assembly** (`$79A1[3]`/`$79D3[arg]`) | ✅ CLOSED as FIXTURE (`src/battle_fx_whirl.h`): tiles baked byte-verbatim from live OBJ VRAM (asm A @ peek-f600, asm B @ peek-f540 — they STREAM into names 0xC6-0xFD when the effect fires + stay stable), pal0 the shimmered constant. RESIDUAL: the ROM sheet SOURCE (the E4:2000-family chain / `$922D` decompressor) is still un-located — same boundary as the combatant sheet decomp; the bake is the sanctioned interim. |
| **O-B** | **the effect-MOVER sweep law** (op-A8 `$A240/$A250` per-frame walk on the `$C0F700` bearing; op-85 angle) | ✅ CLOSED as FIXTURE (`ctBattleWhirlFrame`, 142 entries rel 60-201): DENSE per-frame `--peek` OAM sweep (every frame f499-640) → the render-correct (anchor,orientation) replay. The whirl flicker-multiplexes 3-4 objects (one quad/frame), so the per-frame OAM IS the target. STRUCTURE derived: `$A240[0]` monitor traces a clean diamond (2px/f, 15f edges, center ≈GATO midsection) = one multiplexed object; the closed-form op-A8 mover (why these waypoints) is un-decoded — the byte-exact table is mandate-safe (no invented curve). |
| O-C | op 6C (`C1/5F13`), op 98 sub-table `$79D3`, op 80/F0 in the supers — the secondary-effect ops | characterized structurally (sync-gated effect plays); exact per-op semantics of the Y8/Y11 secondary sweeps not fully decoded |
| O-D | the streamer mode-5 exact colour math (`FD/E64F` fixed-point) + the slot-arm site (who writes `$0520` slot 8/9 = 0x50 at f473) | `battle_animeng_objslot §9` O5; cosmetic for Cyclone. The tile-stream DMA (O5) is IRRELEVANT here (no tile-stream) |
| O-E | Ice's crystals / the song notes as effect objects | ✅ **ICE CLOSED** (§8 below, `src/battle_fx_ice.h` + `battleIceRender`): the SAME engine — Ice's `CE/1136` script has 4 effect tracks (Y2/Y3/Y8/Y11), each a 16x16 crystal shard (assembly `$8E=3`, pal0/prio3), captured as the per-frame OAM sprite list + static sheet; byte-exact 372/372 vs the oracle, pinned ×2. The **song notes** are now CLOSED too (§9, `src/battle_fx_song.h` + `battleSongNotesRender`): the SAME engine, `CD/9EDA` tracks — byte-verbatim per-frame OAM, pinned ×2. |

**Bottom line:** the tech-effect VISUAL layer is DERIVED — (1) the whirlwind geometry = OBJ effect objects on
the interpreter's tracks 8-15 (op 72/98 draw at `$A240/$A250`, animId-3 assembly, pal0/prio3, 2-frame spin +
op-A8 sweep); (2) the palette shimmer = FD/E39C mode-5 on slots 8,9; (3) the strikes = the `1E`→`CE/0000[ss*2]`
supers + op-49 sync. The OAM ledger (§4) is the byte-exact render target. **The PORT is now LANDED** (see the
LANDED addendum above): O-A + O-B both closed as the sanctioned oracle fixture (`src/battle_fx_whirl.h`), the
whirlwind renders in `battleWhirlRender`, byte-exact 309/309 vs the oracle fb across 3 orientations, pinned ×2.
`battle_tech_slice.md` T3 is answered — structurally AND in pixels. Residuals: the ROM sheet source + the
closed-form op-A8 mover law (both un-decoded, the fixture is the mandate-safe interim); the mode-5 shimmer
edges (a no-op inside the visible window). The fixture is GATO-cast-specific (absolute anchors, like the
entrance bakes); a different Cyclone cast needs the anchors re-expressed relative to the target's live foot.

---

## 8 — ✅ ICE (O-E): the SECOND effect object LANDS — Marle's crystal, byte-exact (`src/battle_fx_ice.h` + `battleIceRender`)

The mandate's O-E ("Ice's crystals … the SAME engine, one `--peek` to pin their assemblies") is CLOSED,
proving §3's model generalises: Ice is a **different effect** (multi-shard, animated, no sweep) drawn by the
**same interpreter high-track machinery**, and the same **oracle-fixture** render path (`ct_charbase.h` /
`battle_fx_whirl.h` precedent) reproduces it byte-for-byte. **Verified byte-exact vs the oracle: 372/372
(rel 100) + 84/84 (rel 150) + 178/178 (rel 200, overlapping shards) + 236/236 (rel 208) opaque crystal px** —
our `--battletest` cast == the oracle's own OAM/VRAM render, identity OAM→fb. Pinned
`field-battle-tech-ice-crystal-orbit` (f627, fnv 368C6FE5) + `-shatter` (f732, fnv E8F91F85), determinism ×2.

### 8.1 The CE script `CE/1136` (chain: `$9877=0x0C` → `CD/5EF0[0x0C×2]=0x1136`; decoded byte-exact)

Reached by tech id **0x0B** → `CD/45A6[0x0B×7][0]=0x0C` (= `$9877`, the anim selector) → `CD/5EF0[0x0C×2]=0x1136`
→ `CE/1136`. Header `90 F0 80 00` = track mask **0xF090** (track = 15−bitpos, MSB=Y0) → tracks **Y0, Y1, Y2,
Y3, Y8, Y11**; pointers `1148 1162 116D 118C 11A3 11BA`. (The mask convention validated on Cyclone: `0x8098`
→ Y0/Y8/Y11/Y12 = the §4.1 tracks, exactly.)

```
Y0  main (Marle)    CE/1148:  72·0D SETANIM(cast pose) · 03·10 · 02 · 36 · 24·02 · 02 · 35 · 24·03 · 3D·0C ·
                              78·70 SOUND · 34·1B · 22·1B·00 WAITc · 06·03 · 2E · 01 ENDACT   ← Marle's cast
Y1  popup           CE/1162:  24·03 · 20·14 PAUSE · 1E·09 SUPER · 22·1B·00 WAITc · 50 SHOWDMG · 00 RET
Y2  EFFECT shard    CE/116D:  1B·09 · 72·03 SETANIM=3 · 73·00 PRIO · 20·10 PAUSE · 1E·26 SUPER · … · 1E·22 · 00
Y3  EFFECT shard    CE/118C:  1B·09 · 72·03 SETANIM=3 · 73·00 PRIO · 20·10 PAUSE · 1E·27 SUPER · 76·00·03 ANGLE · A8·09 MOVE · 1E·22 · 00
Y8  EFFECT shard    CE/11A3:  1B·09 · 72·03 SETANIM=3 · 73·00 PRIO · 20·10 PAUSE · 1E·28 SUPER · 76·00·03 ANGLE · A8·12 MOVE · 1E·22 · 00
Y11 EFFECT shard    CE/11BA:  1B·09 · 72·03 SETANIM=3 · 73·00 PRIO · 20·10 PAUSE · 1E·29 SUPER · 76·00·03 ANGLE · A8·1B MOVE · 1E·22 · 36 · 00
```

So the crystal = **FOUR effect shards** (Y2/Y3/Y8/Y11), each drawing **assembly `$8E=3`** (the SAME generic
effect-assembly slot the whirlwind used — re-sheeted per tech: Cyclone streams whirlwind tiles into the OBJ
names, Ice streams crystal tiles) at PRIO, with **staggered supers 26/27/28/29** and **different move bearings**
(A8 arg 09/12/1B → the shards spread). Y0 is Marle's cast pose (not an effect object), Y1 fires the SHOWDMG
popup. This is the "assembly-3 is generic, the SHEET is per-tech" law the whirl dossier predicted (§3.2).

### 8.2 The oracle ledger (source `ice_precast.state` — the mid-battle Ice-queued gato_ice cast)

Driving `ice_precast.state` forward (Ice already queued → dispatches with **zero input**, more deterministic
than a menu btnscript; the whirl's `gato_press_action.state` base is equally non-committed):

```
peek-f26  $AE93 → 0x0B      Ice tech id staged (dispatch, C1/BBB1)
peek-f30  $9877 → 0x0C  $A013 0→1   Ice anim (selector 0x0C) + freeze ON  == our actStart (rel 0)
rel 64..177  ORBIT: 4 shards hovering ~(171,87), tile-cycle 0x140→0x142→0x144→0x146 (staggered/shard, ~5f each)
rel 178..180 (3 empty frames — the shards vanish before impact)
rel 181..235 SHATTER: 1-4 shards sweep LEFTWARD (x 171→163→154→145, ~9px), tiles 0x10A/0x10C/0x122-0x12C
peek-f402 $A3D1 → 1        SHOWDMG popup born (rel ~373)
peek-f463 $A013 1→0  $6130 59→19   span end + HP applied (−40)   span = 463−30 = 434 (CT_SPAN_PC_TECH_ICE)
```

`rel = peek_frame − 30` == our render `rel = h->frames − actStart` (identical mapping model to the whirl).

### 8.3 The OAM ledger (the render spec) + the two structural differences vs the whirlwind

- **pal0, prio3, 16x16** OBJ sprites (objSize=0 → size-bit-1 = 16x16). **No flips.** Tiles in the objTileAdr2
  half (names ≥ 0x100 → VRAM byte `0x2000 + (name−0x100)·32`). **No pal0 combatant exists**, so `pal==0` is a
  clean crystal filter (like the whirl's pal0/prio3).
- **DIFFERENCE 1 — multi-shard, not flicker-multiplexed.** The whirl showed ONE 32×32 quad/frame time-sliced
  across 4 OAM slots; Ice draws **up to 4 independent 16×16 shards SIMULTANEOUSLY** (the 4 CE effect tracks),
  each at its own (x,y) with its own animation-cycle phase. So the fixture is a **variable-length per-frame
  sprite list** (offset+count), not a fixed 4-sprite quad.
- **DIFFERENCE 2 — static sheet, animated by NAME.** The whirl's tiles were stable (2 assemblies). Ice's
  tiles ALSO stay static in VRAM (`vram[0x2000:0x2A00]` hash CONSTANT rel 64..238 — one streamed sheet), but
  the animation plays by each shard **referencing different NAMES** within that sheet frame-to-frame (0x140→…→
  0x146 orbit, 0x10A/0x12C shatter) — a sprite-sheet animation, not tile re-streaming. So ONE VRAM capture
  (rel 100) gives every crystal tile; pal0 is likewise a single constant (the rel 60-63 variation is the
  pre-crystal fade-IN, before any shard draws).

### 8.4 The fixture + the render (mandate-safe)

`src/battle_fx_ice.h` (regen `tools/_gen_battlefx_ice.py`, drives ~173 `--peek`): **96 tiles** (names
0x100-0x15F, 4bpp, byte-verbatim from live OBJ VRAM @ rel 100), **pal0** (16 BGR555, the ice-blue constant),
**563 shards over 172 frames** (rel 64..235) as `{x,y,tile}` per shard + a `{off,n}` frame index. `battleIceRender`
(field.c, beside `battleWhirlRender`, called at both compose sites): gate `actActive && actIsTech && actTechId
== 0x0B` (Ice — **the canonical dispatched-tech selector `$AE93`**, the ONLY per-tech branch, exactly as the
whirl selects itself by tech id 1 — NO scene/tech-id hacks), `rel = frames − actStart`, blit each shard's 16×16
(`battleBlitCell`, HIGH OAM index first so low-index draws on top — the shard-overlap priority, verified at rel
200). **Zero invention**: every tile/pixel/position is oracle-measured; the per-frame OAM replay is the same O-B
closure the whirl used, generalised. The fixture is gato_ice-cast-specific (absolute anchors on GATO, like the
whirl); a different Ice cast needs the anchors re-expressed relative to the target's live foot.

**Residual (honest):** the ROM effects-SHEET source + the closed-form op-A8 shard-spread law are un-decoded —
the SAME boundary as the whirlwind's O-A/O-B (the combatant sheet decompressor / the effect-mover subsystem);
the byte-verbatim fixture is the mandate-safe interim. The mode-5 palette shimmer (whirl §2) does not touch
Ice's pal0 inside the visible window (constant, measured). The song notes (the OTHER O-E half) are now CLOSED — §9 below.

---

## 9 — ✅ SONG NOTES (O-E, the OTHER half): the THIRD effect object LANDS — GATO's musical notes, byte-exact (`src/battle_fx_song.h` + `battleSongNotesRender`)

The mandate's remaining O-E ("the song notes … same engine, `CD/9EDA` tracks") is CLOSED, completing the
tech-effect visual layer. GATO's song emits floating **musical-note OBJ effect objects** drawn by the SAME
interpreter high-track machinery as the whirlwind (§3) and Ice (§8) — a THIRD effect through one engine —
and the SAME oracle-fixture render path reproduces them byte-for-byte. **This SUPERSEDES the prior INVENTED
note render** (a heuristic drift pool: `+age/2` px right, `-age/5` px up, glyph `(age/6)%3`, flagged in
`battle_animeng_corpus.md` 2.2 as "the per-note drift LAW is the C1/62xx effect-subsystem boundary") with the
derived byte-verbatim per-frame OAM replay. **Verified byte-exact vs the oracle fb: 142/143 opaque note px
(rel 100) + 152/152 (rel 150)** — the 1px @ rel 100 is a note-overlap enumeration artifact present in BOTH
our fb and the oracle fb (our render == the oracle at all 143). Pinned `field-battle-song-notes-orbit`
(f891, rel 100, fnv 3ED9D32D) + `-drift` (f941, rel 150, fnv 4D56C6AF), determinism ×2.

### 9.1 The oracle ledger (source `gato_moving_lake.state` — the un-hit HOLD song, GATO in place at 128,85)

Song span (freeze `$A013`=1 with `$9877`=0x10) = **f373–685 (312f)**; songStart f373 == rel 0 (`rel =
peek_frame − 373`, identical mapping to the whirl/ice). The `$A3D1` "1"-damage popup is born at f620 (rel 247
= span−62). Per-frame `--peek` OAM sweep (filter pal0 note glyphs 0x0E6/0E8/0EA):

```
rel 44         no notes yet (last quiet frame)
rel 46         first note @ (129,36) glyph 0x0E6 (just above GATO's head/mouth)
rel 46..208    UP TO 5 concurrent notes (OAM slots 12-16), drifting UP-and-RIGHT ~+0.5px/f, glyphs twinkling
rel 209        no notes (window ends)
```

The note window is **rel 46..208** (163 frames, **473 notes total** across the window), co-timed with (but not
identical to) the spotlight window (rel 42..236, §D.3) and the singing-pose sway (rel ~45..245, corpus 2.1).
pal0 is **CONSTANT** across the window (measured rel 40..210 identical `1947 7FFF 29EF 0CA7 08C9 …`) — the
**FD/E39C mode-5 palette oscillator is a NO-OP for the notes' pal0** inside the visible window, exactly like
the whirlwind (§2, §4 finding 3): the streamer mechanism was first *found* on "the song notes," but the notes'
rendered colour is a single baked constant. So — like Cyclone — the palette is proven constant-in-window, not
derived as an oscillator (`--peek` `.oam` CGRAM sidecar; assert in `tools/_gen_battlefx_song.py`).

### 9.2 The OAM ledger (the render spec) + the identity with §3's model

- Each note = ONE **16×16 (OBSEL size bit=1) OBJ, pal0, prio3**, no flips — same class as the whirl/ice
  effect sprites. Names cycle **0x0E6 → 0x0E8 → 0x0EA** (glyph = `(name−0x0E6)/2`); the 3 glyph tiles are
  **STATIC in VRAM** (objTileAdr1 half, VRAM byte `name·32`; each 16×16 = 4 sub-tiles T,T+1,T+0x10,T+0x11),
  the note "twinkles" by the OAM sprite CYCLING which fixed glyph it shows (NOT a tile stream) — the SAME
  "static sheet, animated by NAME" law Ice used (§8.3 DIFFERENCE 2). Note tiles + pal0 verified byte-exact
  vs live VRAM/CGRAM.
- **Like Ice, multi-sprite (up to 5 simultaneous), NOT flicker-multiplexed** (contrast the whirl's single
  time-sliced quad, §4 finding 2). So the fixture is a **variable-length per-frame sprite list** (offset +
  count), same shape as Ice's.
- **DIFFERENCE vs Ice — foot-relative bake.** The notes track GATO's head, so each note is stored as
  `{dx,dy,glyph}` = screen (x,y) MINUS GATO's song foot (128,85); the render rebases on GATO's LIVE foot.
  This is the whirl/ice "a different cast needs the anchors re-expressed relative to the live foot" residual —
  done here. **CRITICAL anchor fix:** `battleGatoRender` anchors GATO's body at his staged-foot base
  **(136,111)** + walk/anchor delta (= his live foot); `battleSongNotesRender` MUST use the SAME base, not
  the capture's live-foot constant (128,85). In `gato_moving_lake` the two coincide (136,111+delta==128,85),
  but in the `gato_t0` hold-fight GATO sings DISPLACED (live foot ~136,76), so the (128,85) base placed the
  notes 35px too high — corrected to (136,111)+delta so the notes track his live head in any cast.
- ROM path (cited, `battle_song_spotlight.md` B.3 / corpus 2.2): the song anim `CD/9EDA` drives the note
  objects via the **85/98/A8 note-sprite drivers** (`C1/6258/6299/69C3`) off the effect vars
  `$5D80/$5D9C/$5D9E/$5DA0`; the note graphic loads from `D1/4100|D1/4400`. This is exactly §3's effect-object
  engine (the interpreter's high tracks, `$A240/$A250`), same as whirl/ice.

### 9.3 The cast-specific realization (why one byte-verbatim bake, not a span-relative law)

Cross-checked on a SECOND HOLD song (`gato_press_action`, songStart f1151): at the SAME rel the note positions
AND glyph phase DIFFER (rel 113: moving_lake `(161,36)c (141,23)b (130,37)a (152,45)b (129,35)a` vs
press_action `(172,36)b (150,21)c (140,40)c (162,46)b (140,34)b`). So the note trajectory is the effect
subsystem's **cast-specific realization** (an RNG / absolute-frame-driven glyph phase + spawn/drift), NOT a
pure span-relative curve — the closed-form law is the SAME un-decoded boundary as the whirl's op-A8 mover
(O-B) and Ice's shard-spread. The byte-verbatim per-frame replay of ONE realization is therefore the
mandate-safe interim, EXACTLY as the whirl fixture is `gato_cyclone`-cast-specific and Ice's is
`gato_ice`-cast-specific. (`--peek` vs `--monitor` note: the OAM shadow `$7E0700` via `--monitor` runs 1 frame
AHEAD of the `--peek` OAM and shows a different glyph phase — the documented `--peek≠--monitor` gotcha — so
`--peek` is authoritative and is what the bake + the pixel-verify both use.)

### 9.4 The fixture + the render (mandate-safe)

`src/battle_fx_song.h` (regen `tools/_gen_battlefx_song.py`, drives ~165 `--peek`): **3 glyph tiles** (names
0x0E6/0E8/0EA, 4bpp, byte-verbatim from live OBJ VRAM @ rel 113), **pal0** (16 BGR555, the note constant),
**473 notes over 163 frames** (rel 46..208) as `{dx,dy,glyph}` per note (foot-relative to 128,85) + a
`{off,n}` frame index. `battleSongNotesRender` (field.c, beside `battleWhirlRender`/`battleIceRender`, called
at both compose sites): gate `battleGatoSinging` (`$9877`==0x10 on an enemy action — **the canonical song
selector `C1/469A`**, exactly as the whirl/ice select by dispatched tech id — NO scene/tech hacks),
`rel = frames − actStart`, rebase on GATO's live foot (136,111+delta), blit each note's 16×16
(`battleSongNoteBlit`, HIGH OAM index first so low-index draws on top — the note-overlap priority). The note
glyph tiles + pal0 were RELOCATED here from `battle_gato_song.h` (which now carries only the singing POSE).
**Zero invention**: every tile/pixel/position is oracle-measured; the per-frame OAM replay is the same O-B
closure the whirl/ice used.

**Diagnosis of the pre-existing `field-battle-song-spotlight` pin (f1100):** the song-spotlight check's frame
has DRIFTED with the battle-loop timing — in the current engine the song dispatches at `actStart=791`, so
f1100 = **rel 309**, PAST the note window (rel 46..208), the spotlight window (rel 42..236) AND the popup
(dead by rel 304). The notes therefore do NOT touch its fnv (8ED96E53 both before + after this work), so it is
NOT re-pinned (mandate: zero existing pins move). The byte-exact notes are pinned instead by the two NEW
checks at real note frames (f891 rel 100 / f941 rel 150). **The tech-effect visual layer is now COMPLETE:**
whirlwind (§3-7), Ice crystals (§8), song notes (§9) — three distinct effects, one effect-object engine, all
byte-exact oracle fixtures.

**Residual (honest):** the ROM effects note-drift law (the `$5D80` effect subsystem) + the RNG/abs-frame
glyph-phase source are un-decoded — the SAME boundary as the whirl's O-B / Ice's shard-spread; the
byte-verbatim fixture is the mandate-safe interim. The mode-5 palette shimmer does not touch the notes' pal0
inside the visible window (constant, measured).

---

## SCRATCHPAD ARTIFACTS (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-9e84b785-…/scratchpad/`
- `ct_probe.exe` (= build/ct.exe). Base `gato_cyclone.state` (= repo `gato_press_action.state` + `2837=80`).
- `cyc_f{288,480,540,560,580,600,620,650,665}.raw` (+ `.wram/.vram/.oam`) — the peek captures; `cyc_f*.png`
  (fb2png), `cyc_f600_zoom.png` (the clean whirlwind).
- `oam_decode.py` (OAM/CGRAM decoder + content-diff), `decode_ce.py` (the CE anim-script decoder w/ the
  `C1/7A6B` length table).
- Monitors: `--monitor "9877:1,A013:1,95DB:1,6130:2,A3D1:1,AE93:1"` (ledger); `"0520:1,052C:1,…,05A4:1"`
  (12 slot modes — the mode-5 bursts); `"0520:144"` (full slot dump).
- Disasm anchors: interp loop `0x14090`, tick `0x14CA2`, JT `0x17A6B`; effect ops 72 `0x15FDE`(+effect path
  `0x16034`), 98 `0x16299`(+helper `0x1762E` `JSR ($79D3,X)`), 1E `0x156CC` (`CE/0000[ss*2]`), 1F `0x156FB`,
  49 `0x15A70`, A8 `0x169C3`(+effect `0x16A99`), 85 `0x16258`, 6C `0x15F13`; assembly dispatch `C1/75CC`
  (`($79A1,X)`). ROM: Cyclone script `CE/0909`; super table `CE/0000`; supers `CE/D612`/`D617`.
