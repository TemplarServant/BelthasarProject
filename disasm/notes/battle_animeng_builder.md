# BATTLE ANIM ENGINE — PART 3: the ASSEMBLY→OAM BUILDER (`$967B` → `$9F47/$9F49` → `$7E0700`)

Derives, from the ROM disasm (`tools/dis.py`, flat = `(bank−0xC0)<<16|addr`) + the LakeSnes oracle
(`ct_bld.exe = build/ct.exe`, base `gato_press_action.state`, HOLD driver `100:8,0:N`) **only**, the last
major piece of the battle pose engine: the bank-C1/CF machine that turns the pose-tick's **assembly index
`$967B`** (`battle_animeng_script.md` §3) into the per-combatant OAM cells in the shadow `$7E0700`. **Zero
invention** — every field byte-cited, every writer PC read from the oracle's CPU write-hook (`--monitor
--writer`), and the assembly DATA validated cell-for-cell against `battle_animeng_corpus.md` + the live OAM.

> **The headline.** The "sprite ASSEMBLY" the prior dossiers kept hitting as a boundary is **a flat array
> of signed `(dx,dy)` byte-pairs, one pair per OAM cell** — nothing more. `$967B` selects one such array
> (`base + $967B × stride`); the engine sign-extends its bytes into `$9F47/$9F49` (per-cell offsets) and a
> compose pass adds the live combatant position `$1D0C+cmb / $1D23+cmb`. GATO's idle-UP, idle-swap, the two
> walk-cycles, the song sway, and the front attack rear-up **all live in one 0x78-strided table at
> `E3/0060`** and fall out byte-exact. **The pose bakes (`battle_stance.h`/`battle_anim.h`) are retired:**
> each is `assembly(base + $967B×stride)` decoded here. The **CF/E6D6 / CF/EB60** the script dossier named
> are real (CF/EB60 = the compose loop that writes `$0700`; CF/E6D6 = the `$922D` tile/attr assembler) —
> but the pose ANIMATION is carried by the `(dx,dy)` arrays, not by them.

---

## 1 — Architecture (the whole OAM path, one pass)

```
 $967B (assembly index, from the pose tick C1/3360)
   │
   ▼  C1/1C4A  "BuildAssembly"  (per combatant cmb = $9679; 7 callers §6)
   │    $BA = $1C78[pidx] (+bank $1C7A) + $967B × CCF40A[type]     ; pidx = CCF92D[cmb] = cmb*3
   │    position pass 11F5F: for i in 0..CCF4FC[type]:            ; count = 24 bytes (GATO), 8 (Crono/Marle)
   │        $9F47[cmb*16 + i*2] = sign_extend( [$BA][i] )          ; the assembly bytes = signed dx / dy
   │    (parallel cell-word pass 11CF6: templates → construction buf $7E64xx, C0FD00 h-flip — §7)
   ▼
 $9F47 / $9F49  (per-cell 16-bit dx, dy — the pose LAYOUT)
   │
   ├── $922D  ← CF/E6D6..E7C3 assembles base OAM (X,Y,TILE,attr / cell) ── CF/EEC3 copies $922D → $0700
   ▼
   ▼  CF/EAC5 (=CF/EB60)  "OamCompose"  (per render-obj C5 = cmb + 0x0C)
   │    cellcount $34 = CCF50A[$9670[obj]×2].lo   (GATO 12, Crono/Marle 4)
   │    baseX $2E = ($1D0C+cmb) + $A498[obj];  baseY $30 = ($1D23+cmb) + $A4A3[obj]
   │    for each cell (X=OAM cursor $C9, Y=cell cursor $42=cmb*16):
   │        $0700,X = $9F47[Y] + baseX            ; OAM X   (store CF/EB58)
   │        $0701,X = $9F49[Y] + baseY            ; OAM Y   (store CF/EB5D)
   │        $0702,X = ($0702,X & C1FF) | attr     ; keep TILE+flip, set pal/prio (store CF/EB53)
   │        $0900   high-table  ← $902D[X] / $8E2D[Y]  (16×16 size + X-msb, CF/EB70-EB7C)
   ▼
 $7E0700 (128×4) + $7E0900 (32)  ── OAM DMA ch0 → $2104 (set up C2/01A6) ── hardware OAM
```

**Confirmed writers of `$7E0700` (oracle `--monitor "0720:48" --writer --diff`, GATO idle f288-780):**
GATO's 12 cells `$0720-$074F` are written **only by CF:EB60** (39×). Crono's 4 cells `$0710` by **CF:EB60**
(34×) + **CF:EEDB** (the `$922D`→`$0700` copy, 25×). `$9F47` written by **C1:1F6E** (C1/1C4A position pass,
30×). Construction buffer `$7E70AD` (GATO) / `$7E64AD` (Crono) by **C1:1D41/1D92/1F1A** (C1/1C4A). `$922D`
by **CF:E7C3** (25×). The findoam "0 PCs" is a write-log-cap artifact — `--monitor --writer` is authoritative.

---

## 2 — The builder routine `C1/1C4A` (byte-cited)

`tools/dis.py 0x011C4A`. Entry cmb = `$9679`; assembly index = `$967B`; per-cmb "type" = `$967C,X`.

```
11C4A  LDX $9679 / LDA $967C,X / CMP #03 / BEQ RTS   ; type 3 = no sprite (skip)
11C5D  LDA $967B / CMP $9907,X / BEQ RTS             ; ** CACHE: skip if assembly unchanged **
11C66  STA $9907,X                                   ; latch new index  ($9907[cmb] = current assembly)
11C69  LDA $967C,X / ASL / TAX
11C6E  LDA $CCF40A,X → $A7 (16-bit)                  ; $A7 = STRIDE for this type
11C7A  LDA $967B → $A5 / JSR C1/00A7                 ; $A9 = $967B × stride   (hw-mult helper)
11C82  LDX $88(=cmb) / LDA $CCF92D,X → X             ; pidx = CCF92D[cmb] = cmb*3
11C8B  LDA $1C78,X + $A9 → $BA ; $1C7A,X → $BC       ; ** $BA/$BC = assembly ptr = base + idx*stride **
11C92  LDA $1C57,X → $BD ; $1C59,X → DB              ; $BD = 32-byte OAM-template base (cell-word pass)
   … cell-word pass 11CF6-11F3C (§7) …
11F3D  (PLB) ; LDA $967C,X→type ; $80 = CCF4FC[type] ; $82/$83 = CCF917[cmb*2]   ; ** position pass **
11F5A  LDX $82 / LDY #0
11F5F  LDA [$BA],Y / STA $9F47,X                     ; $9F47[out] = assembly byte
11F64  BMI +/ TDC / BRA ++ / + LDA #FF               ; sign-extend
11F6B  ++ STA $9F48,X / INX INX / INY / CPY $80 / BNE 11F5F
11F75  INC $9912 / RTS
```

**So `$9F47` fills as consecutive sign-extended 16-bit words from the assembly bytes**, indexed
`$9F47 + CCF917[cmb*2]`. `CCF917[cmb*2] = cmb*16` (`00 00 10 00 20 00 …`), so combatant `cmb`'s cell data
sits at **`$9F47 + cmb*16`**. The compose pass then reads them as `dx = $9F47[cell*4]`, `dy = $9F49[cell*4]`
(4 bytes/cell = dx16 + dy16). GATO (cmb 6) → `$9FA7`; Crono (cmb 0) → `$9F47`.

`C1/00A7` = the hw-multiply (`$A9 = $A5 × $A7`, `$4202/$4203/$4216`). The **cache** `$9907,X` (skip-if-
unchanged) is why the pose only rebuilds when `$967B` changes — the per-frame idle re-uses the last layout.

## 3 — The compose routine `CF/EAC5` = `CF/EB60` "OamCompose" (byte-cited from raw)

Raw `CF/EAC5-EB93` (dis.py desyncs on this data-dense routine — hand-decoded from ROM bytes). Two entries
(`EAC5` 16-bit, `EAC8` 8-bit) merge at `EACA`. Per render-object `C5 = cmb + 0x0C` (`$42 = (C5-0xC)*16 =
cmb*16`, the `$9F47` cell-data base):

```
EADD  $2E = ($00,X) ;               with DP=$1D00, $00,X = $1D0C+cmb  → ** baseX = live combatant X **
EAE3  $3A = $A498,X (sign-ext $3B)  ; per-obj X nudge  (0 in this fight)
EAF2  $30 = ($17,X) = $1D23+cmb     → ** baseY = live combatant Y **
EAF8  $3C = $A4A3,X (sign-ext $3D)  ; per-obj Y nudge
EB09  $2E += $3A ; $30 += $3C
EB1E  $37 = $9930,X | $99A9,X ; $36 = 0   ; ** attr = palette | priority **  (GATO 0x02|0x30 = 0x32)
EB28  $34 = CCF50A[$9670[X]*2].lo   ; ** cell COUNT **  (GATO 12, Crono/Marle 4)
EB36  Y=$42 ; X=$C9 (OAM cursor)
  loop $34 times (REP #21):
EB3C   $32 = $9F47,Y + $2E          ; screen X = dx + baseX
EB44   $38 = $9F49,Y + $30          ; screen Y = dy + baseY
EB4B   $0702,X = ($0702,X & C1FF) | $36   ; ** keep TILE(0-8)+flip(14,15); set pal/prio(9-13) **  (EB53)
EB58   $0700,X = $32                ; OAM X
EB5D   $0701,X = $38                ; OAM Y
EB60   if $39==0 && $38<$F0: $0900[$902D,X] |= $8E2D,Y   ; high-OAM size/X-msb  (EB6C-EB7C, else off-screen)
EB7F   $C9 += 4 ; $42 += 4 ; DEC $34 / BNE loop
EB93  RTS
```

**The compose pass writes X/Y and the pal/prio attr; it PRESERVES the tile number and flip bits already in
`$0702`.** This is exactly why the idle claw-swap works: the pose changes are `(dx,dy)` rearrangements of a
FIXED tile set (`battle_animeng_corpus.md` A.1 — same tiles 0x68-0x8E, different positions), so only X/Y
recompute per frame. The **tile numbers** come from the separate `$922D` assembler (§5).

## 4 — The assembly TEMPLATE DATA format (the core result) — signed `(dx,dy)` byte-pairs

Per combatant, the assembly pointer is `$1C78[pidx]` (24-bit, bank in `$1C7A[pidx]`), pidx = `CCF92D[cmb] =
cmb*3`. One assembly = **`CCF4FC[type]` signed bytes = N cells × 2** (dx, dy), at `base + $967B × CCF40A[type]`:

| combatant | cmb | pidx | assembly base (live `$BA`) | stride `CCF40A[type]` | count `CCF4FC[type]` | cells |
|---|---|---|---|---|---|---|
| **GATO** (enemy, type 2) | 6 | 0x12 | **E3/0060** | **0x78** | **0x18 = 24** | **12** |
| **Crono** (PC, type 0) | 0 | 0x00 | **DC/27D0** | **0x28** | **0x08 = 8** | **4** |
| Marle (PC, type 0) | 1 | 0x03 | (bank DC, unread) | 0x28 | 0x08 | 4 |

Tables (`tools/dis.py`): `CCF40A` (`28 00 50 00 78 00 F0 00 40 01…` type→stride), `CCF4FC` (`08 10 18 30
40` type→dx/dy byte count), `CCF50A` (`04 00 08 01 0C 02 18 05 20 07` compose cell-count, low byte 4/8/12/
24/32), `CCF92D` (`00 03 06 09 0C 0F 12 15 18 1B 1E 21 | 00 0C 18 24` cmb→pidx), `CCF917` (`00 00 10 00 20
00 …` cmb→`$9F47` offset = cmb*16), `CCF941` (`AD 64 AD 67 AD 68 AD 6B …` cmb→construction-buf offset).

**Cell order is FIXED per combatant** (the compose pass walks tile-slot 0..N in lockstep with `$9F47`). GATO's
12 slots = tiles **0x68 0x6A 0x6C 0x6E 0x80 0x82 0x84 0x86 0x88 0x8A 0x8C 0x8E**; Crono's 4 = **0x08 0x0A
0x0C 0x0E**; Marle's 4 = **0x20 0x22 0x24 0x26** (`battle_animeng_corpus.md` METHOD). So the assembly is
purely positional — tile numbers are constant, supplied by `$922D`.

**Triple-confirmed** for GATO's live idle-swap (assembly index `$967B=5`): (a) ROM byte-search for the 24
live `$9FA7` dx/dy bytes → **E3/02B8**; (b) live register `$00BA = E302B8` at the frame `$9679=6`; (c)
`base E3/0060 + 5 × 0x78 = E302B8`. And derived `baseX = OAMx − dx` is **(136,125) constant across all 12
cells** = `$1D12/$1D29` = GATO's live position.

## 5 — The `$922D` base-OAM assembler + the `$0700` copy (tile numbers, priority, DMA)

`$922D` is a WRAM staging OAM (X,Y,**tile**,attr per sprite). **CF/EEC3** copies it to the shadow:
```
CF/EEC3  LDA $922D,X→$0700,Y ; $922E,X→$0701,Y ; $922F,X→$0702,Y(TILE) ; $9230,X→$0703,Y(attr)
         INY×4 / INX×4 / DEC $00C8 / BNE   ; whole OAM in one loop
```
`$922D` is filled by **CF/E6D6..E7C3** (writer CF:E7C3, e.g. `CF/E7B9: $922E,X += $A0E7,Y` applies a per-
sprite Y offset). The base OAM (tiles/attr/base pos) is assembled here; `CF/EAC5` then overwrites X/Y with
the animated `(dx,dy)+basepos`. Final OAM `$7E0700` DMAs to `$2104` (OAM) via ch0, set up at **C2/01A6**
(oracle `--findoam`). The `$0702` attr for GATO = **0x32 = pal 1 | prio 3** (from `$9930[obj]=02 |
$99A9[obj]=30`), matching every GATO cell's decoded attr — the byte-exact palette/priority composition.

## 6 — PC vs enemy — the SAME builder (deliverable #4 CLOSED)

`C1/1C4A` has 7 callers (`C1/2F10, 345D, 3702, 416A, 418B, 41AC, 4307`) covering both PCs and enemies —
**PCs use the identical builder, format, and compose path.** Crono (cmb 0, type 0) assembly base **DC/27D0**,
stride 0x28, 4 cells. Decoded Crono `$967B=0xC6` @ DC/46C0 = **`008@(16,13) 00A@(0,7) 00C@(17,28)
00E@(16,0)`** = the corpus Crono idle stance C0 (`battle_gato_idle_victory.md` C.2) **byte-exact**. So the
POSE-TICK that emits `$967B` may differ PC↔enemy (`battle_animeng_script.md` §3 flagged the PC per-frame
tick as un-derived) — but the **assembly-DATA layout and the OAM builder are one shared machine**. Marle's
table (cmb 1, pidx 3) is in bank DC adjacent to Crono's (unread but same format).

## 7 — The parallel cell-word / construction-buffer pass (tile-graphics, secondary)

`C1/1C4A`'s first loop (`11CF6-11F3C`) reads `[$BA]` as 16-bit **cell-words** (`AND #07FF` = a 32-byte
template index; word 0 → blank an OAM block), copies the template `DB:($BD + idx×32)` into the construction
buffer **`$7E0000 + CCF941[cmb*2]`** (GATO `$7E70AD`, Crono `$7E64AD`), with an optional **horizontal flip
when cell-word bit 14 is set**: `LDA ($8C),Y / TAX / LDA $C0FD00,X / STA [$AD],Y` — and **`C0/FD00` is the
256-byte BIT-REVERSE table** (`00→00 01→80 02→40 03→C0 …`), i.e. h-flip of the 4bpp tile bytes. The two-
level count is `CCF3C2[type*14]` (inner) × `CCF3C3` (outer), OAM sub-block offsets from `CC/F3B6+type*14`
via `[$82]`. This pass builds tile-GRAPHICS into the construction buffer (feeds the OBJ VRAM upload); the
POSE positions are the §2 position pass. GATO's construction buffer was near-empty this fight (tiles
already VRAM-resident), so the pose path is `(dx,dy)`-only. **The exact `$BA` interplay between the cell-
word pass (which INCs `$BA`) and the position pass (which reads `[$BA]` at the block start) for a 12-cell
enemy is not fully reconciled** — the position-pass OUTPUT is validated byte-exact regardless; see OPEN.

---

## 8 — VALIDATION: GATO's whole pose table (E3/0060, stride 0x78) vs the corpus — never-baked poses emerge

Decoded `base E3/0060 + idx × 0x78`, first 24 bytes as 12 `(dx,dy)`, shown anchor-relative (min-corner)
for direct comparison with `battle_animeng_corpus.md`. **Every one matches cell-for-cell:**

| idx | E3 addr | pose | corpus match |
|---|---|---|---|
| 0-4 | 0060…0240 | idle **UP/rest** (±1px body-bob variants) | idx4 = `068@(47,10) 06A@(47,25) … 08E@(16,0)` = corpus A.1 UP **exact** |
| **5** | 02B8 | idle **DOWN/swap** (`068@(31,48) 06A@(15,48) 084@(47,11) 086@(47,26)`) | corpus A.1 DOWN **exact** (live idle) |
| **6-8** | 0330/03A8/0420 | **walk-LEFT** (`088@(20,0) 08A@(4,0) … 068@(13,39) 08C@(24,48) 08E@(8,48)`) | corpus §3.1 walk-LEFT **exact**; the 3 frames = ±1px OAM shuffle (the "leg cycle" §3.2 = tile stream, OAM ~static — matches) |
| **9-11** | 0498/0510/0588 | **walk-RIGHT** (`088@(12,0) 08A@(28,0) … 068@(19,39) 08C@(8,48) 08E@(24,48)`) | corpus §3.1 walk-RIGHT **exact** |
| **13-15** | 0678/06F0/0768 | **song claw-SWAY** (`068@(42,11)/(42,12)/(41,13)`) | corpus §2.1 song sway (068 drifts 47→42→41 x, 11→13 y) **exact** |
| **20-23** | 09C0…0B28 | **attack FRONT rear-up** (`068@(18,0) 08E@(0,4) 08C@(35,4) … 080@(18,64)`) | corpus CAPTURE-1 FRONT rear-up **exact** (== shipped `battle_anim.h` kf8) |
| 16-19, 24-34 | 07E0… | attack windup / directional strike / other transients | (LEFT/RIGHT/UP rear-ups + strikes live here; per-facing anchors not each pinned) |

**This retires the bakes.** `ctGatoIdleTrajV2` = assembly idx4↔idx5. The 4 walk poses + their leg frames
(corpus §3, "our engine drops the legs") = idx6-8 / idx9-11 — **derivable, no capture needed**. The song
sway (corpus §2.1) = idx13-15. The front rear-up = idx20-23. During GATO's physical the oracle drives
`$967B=0x15,0x16` (rear-up, oracle `--monitor "967B:1,9679:1"` f504/f524) — the CD-script `06 aa` path
(`battle_animeng_script.md` §4) resolves into the SAME E3 table. All poses, one machine, one table.

(Minor: `battle_animeng_script.md` §6 read the idle as pose-script frames `04/14`; the BUILDER's live
`$967B` alternates **04↔05** = E3/0240↔E3/02B8. The `0x14` is a bank-E4 pose-SCRIPT byte (`$97D9`) that the
pose tick maps to `$967B=05` — a re-map inside the tick, not the builder; the builder indices are
oracle-`$BA`-pinned here.)

---

## 9 — What a C port needs (retiring `battle_stance.h` / `battle_anim.h`)

1. **The `(dx,dy)` assembly tables** baked verbatim per combatant: GATO **E3/0060** (0x78-strided, 24-byte
   records), Crono **DC/27D0** / Marle **DC/27D0+…** (0x28-strided, 8-byte). Provenance like `battle_trig.h`.
2. **The index tables** `CCF40A` (type→stride), `CCF4FC` (type→byte-count), `CCF50A` (class→cell-count),
   `CCF92D` (cmb→pidx=cmb*3), `CCF917` (cmb→`$9F47` offset=cmb*16).
3. **The builder**: `$9F47[cmb*16 + 2i] = sext(asm[i])`; skip-rebuild when `$967B` unchanged (`$9907`).
4. **The compose**: `OAM.x = dx + posX(cmb)`, `OAM.y = dy + posY(cmb)`, `attr = pal|prio` (keep tile+flip),
   16×16 high-OAM; positions from `$1D0C+cmb / $1D23+cmb`. Tile numbers + base attr from the `$922D` path.
5. **C0/FD00** bit-reverse for the h-flip variant (cell-word bit 14).

The port replaces "keyframe → OAM assembly" bakes with "`$967B` → `assembly(base+idx*stride)` → dx/dy +
live pos". The Part-1 tile streamer (`battle_animeng_objslot.md`) supplies the pixels the tiles point at;
this Part-3 supplies the cell POSITIONS; the `$922D` assembler supplies the tile NUMBERS.

## 10 — Open questions
- **`$922D` assembler internals** (`CF/E6D6..E7C3`): the exact source of the tile NUMBERS + base positions
  it writes (traced as far as "assembles X/Y/tile/attr, applies `$A0E7` per-sprite Y offset") — the tile-
  number origin (per-combatant sprite def) is not byte-decoded. Enough is pinned that the POSE path (§2-4)
  is complete; the tile-number provenance is the remaining assembler boundary.
- **`C1/1C4A` cell-word pass vs position pass `$BA` interplay** (§7): the first loop INCs `$BA` while the
  second reads `[$BA]` at the block start; both validated at their outputs, but the internal ordering (is
  the cell-word pass a no-op for 12-cell enemies, or a second entry point per caller?) is unreconciled. The
  7 callers may enter at distinct offsets — not disassembled per-caller.
- **Directional attack anchors** (idx 24-34): the LEFT/RIGHT/UP rear-ups + strikes are in the table but only
  the FRONT (idx20-23) was matched cell-exact; the per-facing anchors (`battle_animeng_corpus.md` §1.1/1.2)
  would each need its own idx pinned via a side-attack `$967B` capture.
- **Marle + other enemies' bases**: Crono (DC/27D0) + GATO (E3/0060) pinned; Marle's exact base (bank DC,
  pidx 3) and other monsters' tables (Hetake etc.) not read — same format, per-combatant `$1C78[pidx]`.
- **The `$0900` high-OAM math** (`$902D` byte-index, `$8E2D` OR-mask): mechanism decoded (size bit + X-msb),
  the two tables not dumped (all GATO cells are 16×16 on-screen, so the observable is uniform).
- **PC pose TICK** (`$967B` emission for PCs): the builder is shared (proven), but the routine that DRIVES a
  PC's `$967B` (idle bob, swing) — parallel to the enemy tick `C1/3360` — stays where `battle_animeng_
  script.md` §3 left it (un-derived). This dossier closes the BUILDER for PCs, not their tick.

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-e5608df4-…/scratchpad/`
- `ct_bld.exe` (= build/ct.exe) + `SDL2.dll`. Base state (repo root) `gato_press_action.state`.
  `live700.raw.wram` (peek f700, full 0x20000 WRAM — construction bufs, `$9F47`, `$0700`, pointer tables).
- **Writer probes** (the empirical spine): `--monitor "0720:48" --writer --diff` (GATO cells → CF:EB60),
  `"0710:8" --writer` (Crono → CF:EB60 + CF:EEDB), `"9F47:8" --writer` (→ C1:1F6E), `"922D:16" --writer`
  (→ CF:E7C3), `"64AD:16"/"70AD:32" --writer` (construction → C1:1D41/1D92/1F1A), `--findoam --watch 0720`.
- **Pointer/index reads**: `--monitor "00BA:3,9679:1,967B:1,967C:12"` (per-cmb assembly `$BA`),
  `"9FA7:48,0720:48,9907:12,96DE:8"` (GATO dx/dy vs OAM, the (136,125) base derivation).
- **Disasm**: builder `tools/dis.py 0x011C4A 0x140` + tail `0x011D87`/`0x011F23`; helper `0x0100A7`;
  compose (hand-decode raw) `CF/EAC5-EB93`; `$922D` copy `CF/EEC3`; assembler `CF/E6D6-E7C3`.
- **ROM data**: GATO poses `E3/0060 + idx*0x78` (24-byte records); Crono `DC/27D0 + idx*0x28` (8-byte);
  tables `CC/F40A`,`CC/F4FC`,`CC/F50A`,`CC/F92D`,`CC/F917`,`CC/F941`,`CC/F3B6`; bit-reverse `C0/FD00`.
- Key addresses: builder **C1/1C4A** (`$9907` cache, `$BA=$1C78[pidx]+$967B*stride`, pos pass `11F5F`);
  compose **CF/EAC5=EB60** (`$0700,X=$9F47[dx]+$1D0C+cmb`; stores EB58/EB5D/EB53; high-OAM EB70); copy
  **CF/EEC3** (`$922D`→`$0700`); `$922D` assembler **CF/E6D6..E7C3**; OAM DMA setup **C2/01A6**.
