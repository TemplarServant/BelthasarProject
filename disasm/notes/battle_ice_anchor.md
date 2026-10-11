# ICE's CRYSTAL IS ANCHORED TO THE **CASTER** — the effect-object position chain, byte-derived

Closes the explicitly-flagged open item at `battle_tech_cyclone_choreography.md` line ~686:

> **OPEN (analogous, NOT touched):** `battleIceRender` (`src/battle_fx_ice.h`) still replays ABSOLUTE
> anchors. Marle is planted for Ice so nothing desyncs today, but **its shards orbit the target**, so the
> same re-anchoring is needed against the victim's live foot before Ice is exercised in another arena —
> that attachment has not been measured, so it stays absolute rather than guessed.

**The premise of that note is REFUTED.** The shards do **not** orbit the target. Every position write in
Ice's four effect tracks resolves to **role 0 = the ACTING combatant = the caster (Marle)**; the victim
enters only as the *bearing* of the shatter sweep (`76 00 03`), never as an origin. Derived below from the
ROM (`tools/dis.py`) + the LakeSnes oracle (`build/ct.exe`) only. **Zero invention.**

> **Reproducible oracle seed (new, all-committed sources — `ice_precast.state` is NOT in the repo):**
> `gato_ice.state = gato_press_action.state` with WRAM `$2838 |= 0x20` (Marle learns Ice; the tech-slice
> injection method, `battle_tech_slice.md`), driven by
> `--btnscript "100:8,0:692,80:2,0:12,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:N"`
> (A · idle · **RIGHT = the ready-PC switch to Marle**, `C1/1224`, `battle_hud_render.md` ADD4 §G · Down =
> Tech · A open · A pick Ice · A confirm GATO). **Ice dispatches at peek-f761** (`$AE93=0B`, `$9877=0C`,
> `$A013` 0→1) ⇒ `rel = peek_frame − 761`. Total pre-tail frames = 758.

---

## 0 — HEADLINE (6 lines)

1. Ice = tech `0x0B` → `CD/45A6[0x0B*7][0] = 0x0C` → `CD/5EF0[0x0C*2] = 0x1136` → script **`CE/1136`**
   (ROM-dumped). Header `90 F0 | 80 00` + **7** track pointers `1148 1162 116D 118C 11A3 11BA 0985`.
2. Oracle `$5DBD+4*role` at the cast: **role 0 → `CE/1148`** (Marle's cast pose), **role 6 → `CE/1162`**
   (the SHOWDMG popup), **roles 8/9/10/11 → `CE/116D` / `118C` / `11A3` / `11BA`** = the FOUR crystal
   shards (each parked inside its own super `CE/D6E5/D70C/D730/D754`).
3. All four shard tracks open with **`1B 09`** = op-`1B` teleport to **resolver mode `0x09`**. Mode 09-11 →
   `C1/764A`: `role = mode−9` → `X = $A014[role]` → point = **`$1D0C,X + $9708,X` / `$1D23,X + $9713,X`**.
   `mode 09 ⇒ role 0`.
4. **Role 0 is the ACTOR, measured on three different actions** — Ice: `$A014 = 01 02 00 06` (role0 = cmb 1
   = **Marle, the caster**; role3 = cmb 6 = GATO, the victim); Cyclone: `00 01 02 06`; GATO's own attack:
   `06 FF FF 00`. So Ice's crystal origin is **the caster's live foot + the caster's engage offset**.
5. High tracks (role ≥ 8) do NOT write `$1D0C/$1D23`: `C1/561F`'s `role ≥ 8` branch writes
   **`$A240[$A2B6*2]` / `$A250[$A2B6*2]`** — the effect-object position pair. Oracle: at rel 38 all four
   objects read exactly **(176, 95) = Marle (176,111) + (0, −16)**, while GATO sits 44 px away at
   (132,125). The crystal never approaches him.
6. **Experimental proof of caster-attachment** (§4.3): the committed fixture's capture and this new,
   independent capture of the same cast are **byte-identical up to a rigid −3 px X translation** over the
   whole orbit (every sprite, every tile name, rel 100/120/150/170/190) — exactly the difference in the
   caster's live X (`ice_precast` 179 vs the canonical 176). An arena-absolute effect would be identical;
   a victim-attached one could not translate with dy = 0.

---

## 1 — THE SCRIPT (bank CE), byte-cited

### 1.1 Which script loads

```
CD/45A6 + 0x0B*7 = 0C F4 00 25 3A 0C 44     -> $9877 = 0x0C  (the anim selector)
CD/5EF0 + 0x0C*2 = 36 11                     -> CE/1136
CE/1136: 90 F0  80 00 | 48 11 62 11 6D 11 8C 11 A3 11 BA 11 85 09
         word1=0xF090 word2=0x0080  -> 7 pointers
```
Oracle `--monitor "9877:1,AE93:1,A013:1,A014:8"` at **f761**: `$AE93=0B`, `$9877=0C`, `$A013` 0→1,
`$A014 = 01 02 00 06 FF FF FF FF`.

### 1.2 The tracks, decoded op-for-op (lengths verified — every track ends exactly on the next pointer)

```
role 0   CE/1148 (26 B, 1148+26 = 1162 ✓)   ** MARLE'S CAST — NO TRAVEL OP AT ALL **
  72 0D | 03 10 | 02 36 | 24 02 | 02 35 | 24 03 | 3D 0C | 78 70 | 34 1B | 22 1B 00 | 06 03 | 2E | 01 | 00

role 6   CE/1162 (11 B, ->116D ✓)           ** the damage popup **
  24 03 | 20 14 | 1E 09 | 22 1B 00 | 50 | 00

role 8   CE/116D (31 B, ->118C ✓)           ** SHARD 1 **
  1B 09            <- TELEPORT to mode 09 = role 0 (CASTER) live + engage offset
  72 03 | 73 00 | 20 10
  33 03            <- STORE ($5D9E,$5D9F) := resolve(mode 3) = the VICTIM's live foot
  41 1E 06         <- $5D86 := $5D9E     (op 41 = $5D80+dst := $5D80+src, C1/59E4)
  41 1F 07         <- $5D87 := $5D9F     (the victim's coords parked in effect vars 6/7)
  33 09            <- STORE := resolve(mode 09) = the CASTER + engage offset  (overwrites)
  78 72 | 1E 26 (super) | 24 02 | 78 6F
  1B 00            <- TELEPORT to mode 00 = role 0 (CASTER) live foot, NO engage offset
  1E 22 | 00

role 9   CE/118C (23 B, ->11A3 ✓)           ** SHARD 2 **
  1B 09 | 72 03 | 73 00 | 20 10 | 1E 27 | 24 02 | 0A | 1B 00 | 76 00 03 | A8 09 | 1E 22 | 00
role 10  CE/11A3 (23 B, ->11BA ✓)           ** SHARD 3 **  ... 1E 28 ... 76 00 03 | A8 12 ...
role 11  CE/11BA (25 B, ->11D3 = next header ✓)  ** SHARD 4 **  ... 1E 29 | 36 ... 76 00 03 | A8 1B ... 36 | 00
```

`76 mm tt` (`C1/60E6`, len 3) = **bearing from resolve(mm) to resolve(tt)**: it resolves `tt` into
`$A5BE/$A5C0`, resolves `mm` into `$D3/$D4`, `$D5/$D6 = tt`, `JSR C1/0222` (the delta/atan pair), then at
`C1/6144` for role ≥ 8: `LDX $A2B6 / LDA $DB / STA $A5B5,X`. So **`76 00 03` = the bearing CASTER → VICTIM**,
stored as this effect object's move angle. `A8 dd` then walks `dd` steps along it.

### 1.3 The supers (`1E ss` = `CE/0000[ss*2]`) — the orbit is FIXED-angle, the shatter is aimed

```
CE/0000[0x26] = D6E5 : 61 01 00 | 03 02 | 00 ...   (shard 1)
CE/0000[0x27] = D70C : 20 01 | 02 00 | 20 02 | 70 0A | 85 40 | A8 10 | 77 80 | 0B | A8 06 | 0C | C1 0C | 40 ...
CE/0000[0x28] = D730 : 20 02 | 02 00 | 20 01 | 70 0A | 85 80 | A8 10 | 77 80 | 0B | A8 06 | 0C | C1 0C | 80 ...
CE/0000[0x29] = D754 : 20 03 | 02 00 | 70 0A | 85 C0 | A8 10 | 77 80 | 0B | A8 06 | 0C | C1 0C | C0 ...
```
`85 aa` (`C1/6258`; role ≥ 8 → `C1/628C`: `LDX $A2B6 / STA $A5B5,X`) = **set this object's move angle to the
LITERAL `aa`** — `00 / 40 / 80 / C0` = the four quadrant bearings, i.e. the orbit's spread is **constant
data, independent of both caster and victim**. Only the shatter (`76 00 03`) is aimed at the victim.

---

## 2 — THE POSITION CHAIN (every hop disassembled)

| step | ROM | what it does |
|---|---|---|
| resolve | `C1/762E` (`0x01762E`) `PHY/PHX/TAY/ASL/TAX/JSR ($79D3,X)` | mode → `$A2B0/$A2B2` |
| mode table | `C1/79D3` (`0x0179D3`) | `00-08 → C1/7639`, **`09-11 → C1/764A`**, `12-18 → …`, `19-1B → C1/77DA` |
| mode 00-08 | `C1/7639` (`0x017639`) | `X = $A014[mode]` → `$A2B0 = $1D0C,X`, `$A2B2 = $1D23,X` |
| **mode 09-11** | `C1/764A` (`0x01764A`) | `Y = mode−9`; `X = $A014,Y`; `$A2B0 = $1D0C,X + $9708,X`; `$A2B2 = $1D23,X + $9713,X` (signed via the `$9713` BPL/BMI split, saturating 0/0xFF) |
| engage offsets | `CC/E3C5-E3CE` (`0x0CE3C5`) `LDA #F0 / STZ $9708,X / STA $9713,X / DEX / BPL` (X=2,1,0) | **PCs = (0, −16)**, a per-combatant constant; enemies come from `E4/F604+id*10` (`battle_multitarget_cursor.md` §2.2/2.3) |
| op `1B tt` | `C1/567D` (`0x01567D`) | `JSR C1/762E`; `$80/$81 = $A2B0/$A2B2`; `$8E=2`; `JMP C1/561F` |
| **the write** | `C1/561F` (`0x01561F`) | `A = $A010` (the ROLE). `< 8` → `X = $A014[role]` → `$1D0C,X/$1D23,X` (a COMBATANT). **`≥ 8` → `C1/5652`: `LDX $A2B6 / ASL / STA $A240,X / STZ $A241,X / … STA $A250,X / STZ $A251,X`** (an EFFECT OBJECT) |
| op `A8 dd` | `C1/69C3` (`0x0169C3`); role ≥ 8 → **`C1/6A99`** (`0x016A99`) | `Y=$A2B6`; `$A644,Y = dd` (steps); `$A355,Y = $A5B5,Y` (angle); `$A230,Y = C0/F700[angle]`; **`$A368,Y = $A240[Y*2]`, `$A37B,Y = $A250[Y*2]` = the move BASE**; `$A342,Y = 1` |
| per-frame apply | **`CF/F640`** (`0x0FF640`) | `DEC $A644,X`; `$A31C,X += $A32F,X`; `$CC = F9FB(angle)`, `$CD = F9FB(angle+0x40)` (`CF/F9FB` = the `C0/F900` sin table); **`$A250[slot*2] = $A37B,Y + $CC`; `$A240[slot*2] = $A368,Y + $CD`** |

**⇒ the rendered shard position is always `anchor + Σ(script displacement)`, and the anchor is the point the
last `1B tt` wrote — which for every one of Ice's four tracks is role 0 = the caster.** This is the exact
analogue of the combatant slide (`CF/F136: $1D0C,X = $A8D5,Y + $A35D,X`, `battle_tech_cyclone_choreography.md`
§2.2).

---

## 3 — WHAT THE ORACLE ACTUALLY SHOWS (this session, `gato_ice.state`, dispatch f761)

`--monitor "A240:8,A250:8,1D0C:8,1D23:8,A013:1" --diff` (166 rows) and
`--monitor "A014:8,5DBD:64,9708:8,9713:8,1D0C:8,1D23:8,A2B6:1" --diff`:

```
$A014   = 01 02 00 06 FF FF FF FF        role0 = cmb1 = MARLE (caster) ; role3 = cmb6 = GATO (victim)
$5DBD+4*0  = 4A 11 CE 00   -> CE/114A     (role 0  = Marle's cast track CE/1148)
$5DBD+4*6  = 62 11 CE 00   -> CE/1162     (role 6  = the popup track)
$5DBD+4*8  = F1 D6 CE 00   -> CE/D6F1     (role 8  in super 0x26  <- track CE/116D)
$5DBD+4*9  = 16 D7 CE 00   -> CE/D716     (role 9  in super 0x27  <- track CE/118C)
$5DBD+4*10 = 3A D7 CE 00   -> CE/D73A     (role 10 in super 0x28  <- track CE/11A3)
$5DBD+4*11 = 5C D7 CE 00   -> CE/D75C     (role 11 in super 0x29  <- track CE/11BA)
$9708 = 00 00 00 00 00 00 00 00           engage dX  (PCs 0)
$9713 = F0 F0 F0 00 00 D0 00 00           engage dY  (PCs −16; cmb5 −48)
$1D0C = 86 B0 8F 00 00 00 84 00           Crono 134  MARLE 176  cmb2 143  GATO 132
$1D23 = 93 6F A0 00 00 00 7D 00           Crono 147  MARLE 111  cmb2 160  GATO 125
```

**The anchor RAM, per frame, across the whole effect:**

| rel | `$A240/$A250` (4 objects) | caster `$1D0C[1]/$1D23[1]` | note |
|---|---|---|---|
| 0-37 | (0,0) ×4 | (176,111) | before the `1B 09` |
| **38** | **(176,95) ×4** | (176,111) | **`1B 09` = caster + (0,−16) EXACTLY** |
| 38-66 | (176,95) ×4 | (176,111) | parked (`20 10` + super entry) |
| 67-177 | orbit, x 161..190, y 80..109 | (176,111) | centre **(175.5, 94.5)** = caster + (0,−16) |
| **187** | **(176,111)**, (168,112), (168,112), (167,112) | (176,111) | **`1B 00` = the caster's live foot EXACTLY** |
| 188-235 | drift left/down to ~(150,115) | (176,111) | the `76 00 03` + `A8` shatter sweep |

**The caster's live foot is CONSTANT (176,111) for every frame of the span** — not by luck: role 0's track
`CE/1148` contains **no travel op at all** (no `98`/`1B`/`11`/`99`/`9B`), so the ROM never moves the caster
during Ice. GATO is at (132,125) the whole time; **no shard is ever within 25 px of him during the orbit.**

---

## 4 — THE FIXTURE (`src/battle_fx_ice.h`) vs. this capture

### 4.1 What the fixture bakes today

`CtIceSprite {uint8_t x, y, tile}` — 563 shards over 172 frames (`CT_ICE_REL0 64`, `CT_ICE_RELN 172`),
**absolute OAM coordinates** from `ice_precast.state` (not committed). `battleIceRender`
(`src/field.c:3720`) blits them at `sp->x, sp->y` verbatim, with `(void)mem;` — it reads **no** live RAM.

### 4.2 The capture's own anchor is recoverable from the fixture, tile-matched

The `1B` frames put the object *exactly* on the anchor, so a frame with the SAME tile in both captures gives
the anchor difference with no modelling at all:

| phase | fixture (ice_precast) | this capture | tile | object − OAM | implied caster |
|---|---|---|---|---|---|
| `1B 09` park (fixture rel 64-66 / mine rel 66) | OAM (171, 87) ×3-4 | OAM (168, 87) ×3 | **0x140** | (+8, +8) | fixture (179,111) · mine (176,111) |
| `1B 00` shatter park (fixture rel 185-190 / mine rel 187) | OAM (171, 95) | OAM (168, 95) | **0x12A** | (+8, +16) | fixture (179,111) · mine (176,111) |

Two independent phases, two different assembly offsets, the same answer:
**`ice_precast`'s caster sat at (179, 111); the canonical GATO-arena caster sits at (176, 111).**

### 4.3 ⭐ THE PROOF — two independent captures differ by EXACTLY the caster delta

`--peek` of this capture at the matching phase (fixture rel N ≡ this capture's rel N+2 — a 2-frame
pre-roll difference between the two states), OAM pal0/prio3 filtered, compared sprite-for-sprite:

```
fixture rel100 [(180,93,0x146),(163,96,0x146),(161,79,0x146),(179,78,0x146)]
mine    rel102 [(177,93,0x146),(160,96,0x146),(158,79,0x146),(176,78,0x146)]   IDENTICAL after x−3 ✔
fixture rel120 [(161,93),(165,76),(181,81),(175,98)] / mine rel122 [(158,93),(162,76),(178,81),(172,98)] ✔
fixture rel150 [(164,82),(176,80),(177,92),(165,92)] / mine rel152 [(161,82),(173,80),(174,92),(162,92)] ✔
fixture rel170 [(175,83),(174,91),(166,90),(168,81)] / mine rel172 [(172,83),(171,91),(163,90),(165,81)] ✔
fixture rel190 [(171,95,0x12A)]                      / mine rel192 [(168,95,0x12A)]                      ✔
```
Every sprite, every tile name, over the entire orbit: **a rigid translation of (−3, 0)** — precisely the
caster's ΔX, with ΔY = 0 because the caster's Y is 111 in both. If the crystal were arena-absolute the two
captures would be *identical*; it is not. If it were victim-attached the translation would have to equal
GATO's Δ (he walks — his Y differs between the captures, so dy = 0 is impossible). **Caster-attached.**

### 4.4 The one thing that is NOT a rigid translation (and the ROM says why)

```
fixture rel205 [(171,89,0x124),(163,95,0x10A),(154,95,0x12A)]
mine    rel207 [(168,89,0x124),(160,96,0x10A),(151,98,0x12A)]      x matches after −3; **y drifts 0/+1/+3**
```
The late shatter diverges in Y — and only there. That is exactly the `76 00 03` bearing (caster → **victim**)
feeding `A8`: GATO's live foot differs between the two captures, so the sweep is aimed slightly differently.
The fixture's shatter is pure-horizontal (y ≡ 95 ⇒ GATO's Y was 111 = the caster's Y in that state); mine
sweeps left-and-down toward GATO at (132,125). **This is a per-cast dependency the fixture cannot carry —
see OPEN I2.**

---

## 5 — CONVERSION PLAN (byte-exact, mandate-safe)

### (a) The anchor cell

`ax, ay` := **the CASTER's live foot** = `$1D0C[$A014[0]] / $1D23[$A014[0]]`. In the engine `$A014[0]` is
`h->actAttacker` by construction (the acting combatant), and `h->mvLiveX/Y` **is** `$1D0C/$1D23`
(`battleMoveInit`, `src/battle.c:3733`: `h->mvLiveX[seat] = 16*r[0]+r[2]` from `$29B0+5*seat`). So the read is
byte-identical to `battleWhirlRender`'s (`src/field.c:3699-3705`):

```c
int atk = (h->actAttacker >= 0 && h->actAttacker < 3) ? h->actAttacker : 0;
int ax, ay;
if (h->mvInited) { ax = h->mvLiveX[atk]; ay = h->mvLiveY[atk]; }          // $1D0C+cmb / $1D23+cmb
else if (mem)    { const uint8_t* r = mem + 0x29B0 + atk*5;               // pre-init: the staged foot
                   ax = 16*r[0]+r[2]; ay = 16*r[1]+r[3]; }
else return;                                                              // (drop the `(void)mem;`)
```
The ROM's own anchor is `foot + ($9708,$9713)` (mode 09) or `foot` (mode 00). `$9708/$9713` are **per-combatant
constants** — `CC/E3C5`: every PC is `(0, −16)` — and Ice's caster is always a PC (Marle), so the engage
offset folds into the baked delta with zero loss. Do **not** hardcode −16.

### (b) The bake: absolute → delta

`ctBattleIceSprites` entries become offsets from the capture's caster foot **(179, 111)** (§4.2):

```
dx = x − 179   (range −34 .. +6)          typedef struct { int8_t dx, dy; uint8_t tile; } CtIceSprite;
dy = y − 111   (range −39 .. −10)         (int16_t is equally fine — CtWhirlFrame uses int16_t)
```
563 entries, mechanically. `CT_ICE_TILE_BASE/NTILE/REL0/RELN`, the tile sheet, pal0 and the `{off,n}` frame
index are **untouched** — this is the same capture, re-expressed.

Render: `battleBlitCell(fb, ctBattleIceTiles, CT_ICE_NTILE, sp->tile, ax + sp->dx, ay + sp->dy, ctBattleIcePal, 0, 0);`
(high OAM index first, unchanged).

### (c) The generator

`tools/_gen_battlefx_ice.py` must do what `tools/_gen_battlefx_whirl.py` already does (its `frames.append((q[0]-live[0], q[1]-live[1], q[2]))`): read the anchor **from the same peek's `.wram`** —
`role0 = w[0xA014]; live = (w[0x1D0C+role0], w[0x1D23+role0])` — and emit `(x-live[0], y-live[1], tile)`.
Add the committed-source state recipe from the box at the top so the bake stops depending on the
uncommitted `ice_precast.state`. **If it is regenerated from the new state the fixture DATA changes**
(2-frame phase, different shatter bearing) — that is a separate, larger re-pin; the minimal change above
keeps the existing capture and only re-expresses it.

### (d) Re-pin consequence (the correctness proof, honestly)

`absolute = anchor_capture + delta` ⇒ re-rendering **on the capture's own anchor** is pixel-identical by
construction. Our engine's live anchor is **(176,111)** — measured, not assumed:
`CT_BATTLE_STAGEDBG=1 … --battletest saves/battle_re/gato_ice.raw.wram` prints
`[stage] PC seat1 obj2 rec=0B 06 00 0F` ⇒ foot `(16*11+0, 16*6+15) = (176,111)`, and the oracle's own
`$1D0C[1]/$1D23[1]` in this cast is the same (176,111). The capture's was (179,111).

**⇒ the two pixel pins WILL change, by exactly a 3 px leftward shift of the crystal. Stated explicitly:**

| check | today | after | why |
|---|---|---|---|
| `field-battle-tech-ice-dispatch` | trace text (dmg 41 / span 434 / ids) | **UNCHANGED** | no pixels |
| `field-battle-tech-ice-crystal-orbit` (f590 = rel 100, fnv `F5026B54`) | shards at (180,93)(163,96)(161,79)(179,78) | **(177,93)(160,96)(158,79)(176,78)** → new fnv | anchor 179→176 |
| `field-battle-tech-ice-crystal-shatter` (f695 = rel 205, fnv `8D0F4764`) | (171,89)(163,95)(154,95) | **(168,89)(160,95)(151,95)** → new fnv | anchor 179→176 |

This is **not** a refutation of the anchor rule — it is the rule's first effect, and the new orbit
coordinates are independently verifiable: `(177,93)(160,96)(158,79)(176,78)` is **exactly** the OAM the
oracle itself produces at that phase when the caster is at 176 (§4.3, this capture's rel 102). That is a
stronger check than an unchanged fnv: the re-anchored render reproduces the oracle byte-for-byte in an
arena where the old absolute bake was 3 px wrong. Re-pin both knowingly (same shape as the whirl's
`-asma`/`-asmb-flip` re-pin, `battle_tech_cyclone_choreography.md` ADDENDUM 2).

---

## 6 — VALIDATION

```
:: 1. the anchor rule (oracle, ~10 s each)
build\ct.exe Chrono_Trigger_USA_SNES.sfc --monitor "A240:8,A250:8,1D0C:8,1D23:8,A013:1" ^
  --from <gato_ice.state> --btnscript "100:8,0:692,80:2,0:12,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:340" --diff
   expect: rel 38 -> A240/A250 = (176,95) x4 == $1D0C[1],$1D23[1]-16 ; rel 187 -> (176,111) == the foot

:: 2. our render vs the oracle at the orbit pin's phase
build\ct.exe Chrono_Trigger_USA_SNES.sfc --peek <gato_ice.state> <tmp> ^
  --btnscript "100:8,0:692,80:2,0:12,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:105"      (= rel 102)
   expect OAM pal0/prio3 = (177,93)(160,96)(158,79)(176,78) tile 0x146  ==  the re-anchored f590 render

:: 3. the suite
python tools\regress.py --quick        (then FULL)
   field-battle-tech-ice-dispatch  MUST stay byte-identical (no pixel involvement)
   field-battle-tech-ice-crystal-orbit / -shatter  re-pin to the new fnv, then --pin
```

---

## 7 — OPEN

| # | item | what would close it |
|---|---|---|
| **I1** | `ice_precast.state` is **not committed**, so the fixture is not regenerable as shipped. | commit the state, or re-bake from the reproducible recipe in the header box (accepting the 2-frame phase + shatter-bearing change as a deliberate re-pin) |
| **I2** | **The shatter bearing is victim-dependent** (`76 00 03` = caster→victim, `C1/60E6`→`C1/6144`), so a translated fixture still carries `ice_precast`'s aim (§4.4, y off by 0/1/3 vs this capture). | port the effect-object mover: `1B` anchor (`C1/561F`/`C1/5652`) + `85`/`76` angle (`C1/628C`/`C1/6144`) + `A8` arm (`C1/6A99`) + the per-frame apply `CF/F640` with `CF/F9FB`/`C0/F900`. Then the shards are computed, not replayed |
| **I3** | The mask-bit → ROLE law is still unsolved (cyclone OPEN C1). New data point: Ice mask `word1=0xF090`/`word2=0x0080` (bits 15,14,13,12,7,4 + w2 b7) instantiates roles **0, 6, 8, 9, 10, 11**; Cyclone `0x8098`/`0x0080` (bits 15,7,4,3 + w2 b7) instantiates **0, 3, 4, 8** — the SAME bit (7) yields role 3 in one and role 10 in the other, so it is not a static bit→role table. | disassemble the CE track loader (`C1/4477` / `C1/4662` / `C1/4816`) and read the mask walk |
| **I4** | Ops `73`, `22`, `50`, `61`, `70`, `77`, `C1`, `34`, `3D` (used by Ice's tracks/supers) are located in `C1/7A6B` but not disassembled here; lengths were confirmed only by the track-length arithmetic in §1.2. | `dis.py` at `C1/6059`, `C1/572C`, `C1/5ADB`, `C1/5D93`, `C1/5FC1`, `C1/6154`, `C1/6F31`, `C1/5931`, `C1/59B9` |
| **I5** | Why `ice_precast`'s caster sits at x=179 rather than the staged 176 was not established (the state is unavailable). It does not affect the plan — the delta bake is correct either way — but it means the fixture's 3 px was a real, silent render error. | inspect `ice_precast.state` if it resurfaces (`$1D0C[1]`, `$29B0+5`) |

---

## SCRATCHPAD ARTIFACTS (not committed)
`…/scratchpad/ice/` — `gato_ice.state` (= `gato_press_action.state` + `$2838|=0x20`), `mon_a240.txt`
(the 166-row `$A240/$A250/$1D0C/$1D23` ledger), `pk*.oam/.vram/.wram` (the peek sidecars).
Disasm anchors (flat): resolver `0x01762E`, mode table `0x0179D3`, mode 00-08 `0x017639`, **mode 09-11
`0x01764A`**, op-1B `0x01567D`, **the position write `0x01561F` (+ the role≥8 branch `0x015652`)**,
op-33 `0x01591A`, op-41 `0x0159E4`, op-76 `0x0160E6` (+`0x016144`), op-85 `0x016258` (+`0x01628C`),
op-A8 `0x0169C3` (+ effect variant `0x016A99`), **the per-frame apply `0x0FF640`**, sin `0x0FF9FB`.
ROM tables: `CE/1136` (Ice script), `CE/0000` (supers), `CD/45A6`/`CD/5EF0` (tech→anim), `CC/E3C5`
(PC engage offsets), `C0/F900` (sin), `C0/F700` (bearing→facing).
