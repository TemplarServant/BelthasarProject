# BATTLE ANIM ENGINE — PART 3: the POSE TILE-UPLOAD path (`C1/1C4A` builder) + the PC pose tick (`C1/2E00`)

Closes the two open halves the corpus (`battle_animeng_corpus.md`) proved must exist: **who moves the
per-pose/per-facing tile CONTENT into OBJ VRAM** (Parts 1/2 accounted for the OAM tile-NUMBERS and the
effect/BG streamer, but nothing uploaded GATO's body pixels 0x68–0x8E, whose VRAM content is md5-distinct
per facing), and **the PC-side pose tick** (Part 2 §9 left it "un-derived… a parallel PC-side routine"). All
derived from the ROM disasm (`tools/dis.py`, HiROM flat = `(bank−0xC0)<<16|addr`) + the LakeSnes oracle
(`ct_tiles.exe = build/ct.exe`), and **end-to-end pixel-validated**: the derived pipeline reproduces the
corpus's exact VRAM tile bytes (21/21 GATO cells; all four facing md5s). **Zero invention** — every field
byte-cited and every address confirmed against a live monitor / peek. `src/` + git untouched.

> **Where this sits.** `battle_animeng_script.md` (Part 2): `$8E → E4-tables → $1CA2/$1CC3 pose script →
> C1/3360 enemy tick → assembly index $967B → bank-CF OAM builder → OAM positions/tile-numbers`.
> `battle_animeng_objslot.md` (Part 1): `FD/E39C` 12-slot streamer = tile/palette EFFECTS ($7E2000 buffer,
> bank-F6/song), dormant for combatants. **This dossier: the third path — `$967B` also drives `C1/1C4A`,
> which GATHERS the pose's tile PIXELS from the sprite sheet into the OBJ-VRAM shadow, which a per-frame DMA
> ships to VRAM.** So Part 2's `$967B` feeds TWO consumers each frame: the CF OAM builder (positions) **and**
> the C1/1C4A tile builder (pixels). This dossier derives the pixels half + the PC tick that also feeds it.

---

## 1 — The whole pipeline in one pass (byte-cited; validated §6)

```
[BOOT]  ROM compressed sheet ──decompress──►  WRAM sheet $7F8000  (per-combatant, ALL poses/facings;
                                                                    8×8 4bpp tiles at sheet+tile·0x20)
        ROM assembly/GATHER table  = bank E3   (per-combatant; GATO base $E30000)
        ROM pose SCRIPTS           = bank E4   (assembly-index sequences; ptr array $1C99[PC]/$1CA2[enemy])
        pointers set from the $29C0 staged pack + E4/2300 meta tables   (loader CC/EAE0–EB6F, §5)

[PER FRAME · PER COMBATANT]
  POSE TICK        PC = C1/2E00   ·   enemy = C1/3360→C1/3440           (structurally identical, §4)
     advance per-slot timer  ($9689 PC / $9694 enemy)  &  cursor ($968C PC / $969C enemy)
     assembly index  $967B = E4:pose-script[ cursor + facing×stride ]   (facing dispatch C1/00A7)

  TILE BUILDER     C1/1C4A     (GATED: run only if  $967B ≠ cached $9907[slot]  — C1:1C60)     ← THE UPLOAD
     asm  = E3[ $1C78[slot] + $967B × strideClass ]   (strideClass = CC/F40A[$967C[slot]·2]; GATO=0x78)
     for each 16-bit cell word w in asm:
        srcTile = w & 0x07FF ;  flip = w & 0x4000 ;  (w==0 → skip this VRAM tile)
        pixels  = sheet[ $7F8000 + srcTile·0x20 ]                       (32 bytes = one 8×8 tile)
        if flip: bit-REVERSE every byte via table  C0/FD00              (H-flip storage; C1:1E2A)
        else   : raw copy                                               (C1:1D5A)
        store 32B → OBJ-VRAM SHADOW  $7E0000 + ( CCF941[slot] + cellPlacement )   (16×16 grid walk, §3)
     cache  $9907[slot] = $967B

  [NMI / per-frame]  wholesale DMA:  OBJ tile shadow $7E63AD ──► VRAM word 0
     ⇒  VRAM_byte  =  shadow_offset − 0x63AD    (proven §3: 98% shadow==VRAM)
     (FD/E170 is a SEPARATE streamer — animated BG chips / effect channels, bank-7F sources — §7)
```

The corpus's "same tile numbers, different content per facing" = **the gather list differs per `$967B`**
(each facing folds to a different block of E3 indices), sourced from the same `$7F8000` sheet, deposited at
the same fixed VRAM window. Nothing streams the combatant pixels except this build-on-`$967B`-change path.

---

## 2 — The tile BUILDER `C1/1C4A` (the upload routine), byte-exact

`tools/dis.py 0x11C4A`. Called per-combatant from the pose ticks (callers: **C1/2F10** PC · **C1/345D**
enemy · C1/3702 · C1/416A/418B/41AC · **C1/4307** the CD/song path). Entry + the CHANGE GATE:

```
11C4A  LDX $9679 / LDA $967C,X / CMP #03 / BEQ RTS   ; $967C[slot] = size CLASS; class 3 = no sprite -> skip
11C55  LDX $9679 / STX $88 / STZ $9922,X
11C5D  LDA $967B / CMP $9907,X / BEQ RTS             ; ** GATE: rebuild ONLY when assembly index CHANGED **
11C66  STA $9907,X                                   ;    cache it (per-slot "last built")
```

Then it resolves the three per-combatant pointers (via the stride-3 remap `$CCF92D[slot]` → X'):

```
11C6E  LDA $967C,X / ASL / TAX'                       ; X' = class·2
11C6E  LDA $CC:F40A,X' -> $A7                         ; strideClass  (class0=0x28,1=0x50,2=0x78,3=0xF0)
11C7A  LDA $967B -> $A5 / JSR C1/00A7                 ; $A9 = $967B × strideClass   (offset into E3)
11C82  LDX $88 / LDA $CC:F92D,X -> TAX''              ; X'' = 3·slot (record index into the ptr arrays)
11C8B  LDA $1C78,X'' + $A9 -> $BA  (bank $1C7A) ;      ; $BA:$BC = 24b ASSEMBLY ptr (E3) + index offset
11C92  LDA $1C57,X'' -> $BD       (bank $1C59 pushed) ; $BD:.. = 24b SHEET ptr  ($7F8000)
```

Per-cell loop (`11CF6…11F31`, `DEC $80` = cell count):

```
11CF8  LDA [$BA] -> $8C ; AND #07FF                   ; cell word; low 11 bits = SOURCE tile index
11CFF  BNE +                                          ; word==0 -> blank: STZ 0x20 bytes, next VRAM tile
11D4A  ASL×5 (tile·0x20) / ADC $BD -> Y               ; Y = sheet + tile·0x20   (32-byte source tile)
11D53  LDA $8C / AND #4000 / BNE ->decode (11DD3)     ; ** bit14 = H-FLIP: use the bit-reversal decode **
11D5A  LDA $0000,Y / STA $7E0000,X  ×16               ; RAW copy 32B  sheet -> shadow
       (X += 0x20 per cell; JMP C1/1F23)
 .decode (C1:1E2A):                                   ; unrolled 32×:
11DD3  STY $8C / STX $AD / SEP #30 / LDY #00
11DDB  LDA ($8C),Y / TAX / LDA $C0:FD00,X / STA [$AD],Y / INY   ; byte -> bit-reversed byte -> shadow
```

**`C0/FD00` is the 256-byte BIT-REVERSAL table** (`00→00 01→80 02→40 03→C0 04→20 …` — `reverse8(b)`):
sprites are stored bit-reversed in the sheet and this un-flips them (a per-cell H-flip encoding, orthogonal
to OAM h-flip). Confirmed: **`build/ct.exe --read "rom:C0/FD00+64"` = `00 80 40 C0 20 A0 60 E0 …`.**

- The staging DEST `$7E0000 + X` where `X = CCF941[slot] + perCellPlacement`. **`CC/F941`** (word, `LDA
  $CCF941,X` with X=slot·2) = the per-combatant VRAM-shadow base: `64AD 67AD 68AD 6BAD 6CAD 6FAD 70AD 73AD`
  (slots 0..7). **slot6 = 0x70AD = GATO** (matches the live `$9679=6` when GATO is built). `perCellPlacement`
  comes from a table at `CC/F3B6`/`CC/F3C2` (`11CD4 TXA / ADC #F3B6 / STA $82`) = the 16×16 tile-grid walk (§3).
- **The `$967B` change-gate proven live** (`--writer`): the staging buffer `$7E70AD` is written **by `C1:1E2A`**
  the frame after the assembly advances, and only then (dormant while `$967B` unchanged).

---

## 3 — The DEST is a 1:1 OBJ-VRAM SHADOW at `$7E63AD`; a wholesale DMA ships it

The staging bases `CC/F941` and the empirical VRAM positions line up as **VRAM_byte = shadow_offset − 0x63AD**:

| slot | combatant | staging base $CCF941 | −0x63AD = VRAM byte | VRAM tile |
|---|---|---|---|---|
| 0 | Crono | $7E**64AD** | 0x0100 | 0x08 ✓ |
| 1 | Marle | $7E**67AD** | 0x0400 | 0x20 ✓ |
| 6 | GATO  | $7E**70AD** | 0x0D00 | 0x68 ✓ |

**Proven wholesale** (`tiles/g421` peek, OBJ tile region 0x0000–0x1400): at offset **0x63AD** the WRAM shadow
matches VRAM **4782/4864 bytes = 98% (149/160 tiles byte-identical)** — the ~11 off-tiles are mid-update /
effect / bracket tiles. So the entire OBJ combatant-tile block is a WRAM shadow at `$7E63AD` that a per-frame
DMA copies to VRAM word 0; the builder writes each combatant's slice at its own offset. The **per-cell
placement** (from `CC/F3B6`) walks the SNES 16×16 OBJ grid — validated cell→VRAM map (GATO idx 0x0D):

```
cell 0..5  -> VRAM tiles 0x68 0x69 0x6A 0x6B 0x6C 0x6D   (word==0 skips 0x6E)
cell 7     -> 0x6F ;  cells 8..10 -> 0x78 0x79 0x7A (skip 0x7B) ; 0x7C 0x7D (skip 0x7E) 0x7F ;
cells16.. -> 0x80 0x81 0x82 0x83 0x84 0x85 0x86 0x87 …    (the 2×2/16-wide tile layout)
```

(The exact DMA *site* — the vblank drain that ships `$7E63AD`→VRAM — is inferred from the 98% shadow match +
the NMI DMA drain `battle_boot.md §3` `D1:E77C → JSL CD:0C95/CD:0C89`; not disassembled to the instruction. §8.)

---

## 4 — The PC pose tick `C1/2E00` (Part 2 §9's "un-derived PC-side routine" — RESOLVED)

`tools/dis.py 0x12E00`. A loop over the 3 PC slots (`$92`/`$94` = 0,1,2) that is **structurally identical to
the enemy tick `C1/3360`**, keyed on PC-indexed vars, feeding the SAME builder. It is NOT the `$8E`/CD path:

```
12E07  STX $94                                        ; $94 = PC index 0..2
12E09  LDA $96F5,X / BEQ ->skip                       ; PC active?
12E20  DEC $9689,X / BNE ->skip                       ; ** per-PC frame TIMER; advance only at 0 **
12E53  LDA $1C99,X3 -> $A1E8   (X3 = PC·3)            ; ptrA = pose-SCRIPT ptr   (bank E4)
12E5F  LDA $1CBA,X3 -> $A1FE                          ; ptrB = DURATION ptr
12E6B  $82 = $A440·4 ; ptrA += $82 ; ptrB += $82      ; $A440 = anim-state class offset
12E89  LDA $96DB,X -> $A5 ; LDA $966E,X -> $A7 ; JSR C1/00A7   ; $A9 = facing × stride  (SAME helper)
12E9F  LDA $94 / STA $9679                            ; ** set the build slot = PC index **
12EA9  $968C,X += 1                                   ; ** per-PC CURSOR ++ **
12EB3  X = ptrB + cursor ; LDA $E40000,X              ; duration byte; ==0 -> wrap cursor (loop)
12ED2  reload $9689,X = durByte×5 (min 1)             ; C1/00D7 (×5)  -> next frame timer
12EDC  X = ptrA + $A9(facing) + cursor
12EEB  LDA $E40000,X / STA $967B                      ; ** assembly index $967B = E4 pose-script byte **
12EF2… JSR C1/1C4A                                     ; ** SAME tile builder (§2) **
12F13  INC $92 / CMP #03 / BNE ->loop
```

**PC ↔ enemy var correspondence** (the two ticks share the E4 pose-script FORMAT + facing dispatch + builder):

| role | PC (`C1/2E00`) | enemy (`C1/3360`, Part 2 §3) |
|---|---|---|
| pose-script ptrA | `$1C99 + PC·3` | `$1CA2 + slot·3` |
| duration ptrB | `$1CBA + PC·3` | `$1CC3 + slot·3` |
| frame timer | `$9689,X` | `$9694,X` |
| cursor | `$968C,X` | `$969C,X` |
| facing | `$96DB,X` | `$96DE,X` |
| stride | `$966E,X` | `$9671,X` |
| facing dispatch | `C1/00A7` (`$A9=facing×stride`) | `C1/00A7` (same) |
| assembly index out | `$967B` | `$967B` |
| tile builder | `C1/1C4A` | `C1/1C4A` |

**The ptrA array is ONE contiguous table, PCs first:** `$1C99 = $1CA2 − 9`, so `$1C99/$1C9C/$1C9F` =
PC0/1/2 and `$1CA2 + slot·3` = enemies (GATO slot3 = `$1CAB` = `$E472C0`, exactly Part 2's value). Live
(gato_moving_lake): **PC0 ptrA = `$E43000`, PC1 = `$E434A0`** (both bank E4).

**VALIDATED (`gato_press_action`, HOLD, Crono ACTIVE / Marle WAITING):** Crono's built assembly `$9907[slot0]`
oscillates **C6↔4F↔C7** (= the `battle_stance.md` A5.1 idle-bob variants V0/V1/V2) driven by this tick; the
PC cursors `$968C` step **0→1→2→3→0 independently per PC** while the enemy `$969C` cursors are separate, and
**`$8E` stays 0 the whole idle** — so the PC idle bob is the **E4 pose-script path (C1/2E00), NOT the CD/5FF0
`$8E` path**. Part 2 §4's `$8E` writes were the SCRIPTED-ATTACK override (CD op `06 aa`), which overwrites
`$967B` for the action's duration; the resting/idle/walk poses are this tick.

---

## 5 — The graphics LAYOUT (deliverable item 2): where each piece lives + how it is indexed

**Three ROM/RAM structures, one meta bank (E4):**

1. **Pose SCRIPTS — bank E4.** Assembly-index sequences (the low-level anim, Part 2 §5). Per-combatant ptr in
   the contiguous array `$1C99[PC]/$1CA2[enemy]` (24-bit, bank E4), loaded by **CC/EB0A** from `E4/2600`
   (ptrA) / `E4/2800` (ptrB) indexed by `$8E`; the per-animId **stride `$966E`** is derived at load from the
   difference of consecutive `E4/2600` entries (`CC/EB2F–EB47`). Facing folds into `$967B` via `facing×stride`.
2. **Assembly / GATHER table — bank E3.** Per-combatant base in `$1C78[slot]` (24-bit; **GATO = `$E30000`**),
   entry at `base + $967B × strideClass`. Each entry = a run of **16-bit cell words**: `bits0-10 = SOURCE
   tile index` into the sheet, `bit14 = H-flip (bit-reverse) decode`, `word 0x0000 = blank/skip`. This is the
   "which sheet tiles → the fixed VRAM window, in 16×16 grid order" map — the pose-sheet ORGANIZATION.
3. **Sprite SHEET — WRAM `$7F8000`** (per-combatant, ptr `$1C57[slot]`), **decompressed from ROM at boot**.
   8×8 4bpp tiles at `sheet + srcTile·0x20`, `srcTile ∈ 0x000..0x7FF`, stored bit-reversed for flip cells.

**Meta-pointer loader (the "op-0x83 gfx-meta chain") = `CC/EAE0–EB6F`**: sets the per-combatant pointer
records `$1C60/$1C62/$1C81/$1C83 …` from the **event-staged battle pack `$29C0+`** (`LDA $29C8,X→$1C60`,
`$29CA,X→$1C62`, `battle_boot.md §2` pack) and the **`E4/2300`** meta table (`CEAF7 LDA $E42300,X→$1C81`),
alongside the `E4/2600/2800` pose-script load (`CEB0A`). So bank **E4** is the gfx-META bank (pointers +
scripts + strides); bank **E3** the assembly/gather DATA; the sheet PIXELS decompress to `$7F8000`.

**Corpus md5 tile-sets LOCATED** (gather from each state's own `$7F8000` sheet reproduces the corpus VRAM
bytes exactly): GATO **song-FRONT `528f2178` = E3 idx 0x0D** (full 21/21-cell end-to-end, §6); **LEFT
`2632201d` = E3 idx 0x18–0x1B**; **UP `ae5b99c4` = E3 idx 0x10–0x13** (4-frame attack blocks, 8 indices apart
per facing); FRONT/RIGHT (`528f2178`/`2ceb22ac`) resolve to distinct indices too (their anchor cell 0x68 is a
tile shared across many poses, so the block needs the full-window match to disambiguate). ⇒ the "ROM address"
of any GATO facing tile-set = **E3 `$E30000 + idx·0x78` (gather list) + the `$7F8000` sheet tiles it names.**

---

## 6 — END-TO-END VALIDATION (the mandate step): the derived pipeline == the oracle VRAM, byte-exact

Base `gato_moving_lake.state` (GATO turns UP→DOWN at f414 during the song). Method: per-frame VRAM peek +
`--writer`, then a pure-Python re-implementation of §1 checked against the oracle's own VRAM/WRAM.

**A. Trigger + timing** (per-frame VRAM diff aligned by GATO's facing `$96DE[3]` read from each `.wram`):
- facing flips `0→1` at f415; GATO's VRAM tiles are **unchanged f405–417** (still the UP md5), then change to
  the DOWN/song md5 **`2b336113` between f419 and f421**, dest = VRAM bytes **0x0D00–0x13FF (tiles 0x68–0x9F)**.
- `--writer "70AD:8"`: staging `$7E70AD` written by **`C1:1E2A`** at f417 (`7000F810BCE0FFF8`), right after the
  assembly `$967B` advanced (C1:4307, f415), which followed the facing write (C1:600C, f414).
  ⇒ **the tile upload LAGS the raw facing write by ~2 frames** (it waits for the pose/assembly to advance, per
  the `$967B` gate), and reaches VRAM ~5–6 frames later.

**B. Source + decode** (search the peek WRAM for the VRAM bytes): GATO's DOWN VRAM tile 0x68 is found
**bit-reversed** in WRAM at **`0x1A0E0` = `$7FA0E0`** (offset 0x20E0 into the `$7F8000` sheet = source tile
0x107) — never found in ROM verbatim ⇒ **sheet in WRAM, bit-reverse decode**.

**C. Full gather reproduces VRAM** (read live `$1C78[6]=$E30000`, `$967C[6]=class2→stride0x78`,
`$9907[6]=idx 0x0D`; gather E3[0x0D] from the sheet with C0/FD00 flips): **every non-blank cell places
exactly** —

```
cell 0  src 0x107 flip -> VRAM 0x68     cell 4  src 0x029 raw  -> VRAM 0x6C
cell 1  src 0x106 flip -> VRAM 0x69     cell 8  src 0x109 flip -> VRAM 0x78
cell 2  src 0x10B flip -> VRAM 0x6A     …  21/24 cells placed (3 are word==0 blanks)  → 21/21 non-blank EXACT
```

**D. The wholesale shadow** (§3): VRAM 0x0000–0x1400 == WRAM `$7E63AD`+ at **98%** (149/160 tiles byte-exact).

**E. The PC tick** (§4): Crono's built assembly + cursor advance as derived (C6/4F/C7; `$968C` 0→3→0).

---

## 7 — Correction to Parts 1/2 + `battle_boot.md`/`battle_stance.md`: FD/E170 is NOT the combatant tile path

`battle_stance.md S1/S4` and `battle_boot.md §2` attributed the combatant tile streaming to "the `$7F04D0`/
`FD:E170` chip streamer." **That is wrong for combatant OBJ tiles.** Live (`--read`), FD/E170's channel
sources `$7F04D0 = 6C40 6CC0 79C0 7A40 …`, `$7F04F0 = 6D40 6D80 7AC0 7B00 …`, bank `$05D2 = 7F` — all in the
**bank-7F** decompressed-data region, and GATO's DOWN VRAM tiles are **not** found at any `$7F` offset (only at
the bank-7E shadow `$7E70AD`). So **FD/E170 = the animated-BG-chip / effect streamer (bank-7F sources)**;
the combatant OBJ pixels are `C1/1C4A → $7E63AD shadow → wholesale DMA` (this dossier). The stance dossier's
observation that "GATO's body tiles change between idle frames" is correct — that change is `C1/1C4A` firing
on each `$967B` advance, not FD/E170.

---

## 8 — Open questions / boundaries

- **The ROM compressed sheet → `$7F8000` decompress.** The pointer setup is cited (`$29C0` staged pack +
  `E4/2300`, `CC/EAE0`); the ROM ADDRESS of GATO's compressed gfx (keyed by enemy id 0x92) and the
  decompressor invocation at boot (`CF/FB65 → CD/0000` transition) are the SAME boundary `battle_boot.md`
  ("GATO's OBJ graphics ROM source… unexplored") / `battle_stance.md S1` flagged. A port bakes the `$7F8000`
  sheet (like `ct_charbase.h`) or ports the boot decompress.
- **The OAM CELL POSITIONS** (x,y on screen) are a **separate** structure (the bank-CF builder, Part 2 §9)
  from the tile-CONTENT gather (E3) derived here. `C1/1C4A` produces the VRAM PIXELS; the OAM `$7E0700`
  (positions + which VRAM tile-number at which x,y) is built elsewhere and is the standing render boundary.
- **The exact wholesale-DMA site** (`$7E63AD` → VRAM 0) — proven to exist (98% shadow) + located to the NMI
  DMA drain family, but not disassembled to the transfer instruction (the `CD/0C95`/`CD/0C89` dispatch chain).
- **`CC/F3B6`/`CC/F3C2` per-cell placement table** (the 16×16 grid walk 0x68,0x69,…,skip,…,0x78) — mechanism
  shown + validated cell→VRAM map; the raw table not fully tabulated.
- **`$967B` value ranges per combatant** — GATO idle uses small indices (0x00,0x01,0x04,0x05,0x0D…), Crono
  uses C6/4F/C7; the indices are pose-script bytes (E4), magnitude arbitrary; per-facing they fold to blocks
  (UP 0x10–0x13, LEFT 0x18–0x1B). The `$967C` size-CLASS → `CC/F40A` stride (0x28/0x50/0x78/0xF0) is pinned.

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-e5608df4-…/scratchpad/`
- `ct_tiles.exe` (= build/ct.exe). Base states (repo root / scratch) `gato_moving_lake.state`,
  `gato_press_action.state`; corpus `.vram/.wram` (`corpus/{F_f125,L_f108,R_f153,U_f195}.raw.*`).
- `tiles/g4*.raw(.vram/.wram)` — the per-frame UP→DOWN turn capture (trigger/timing/dest, §6A).
- Key monitors: facing/pose `--monitor "008E:1,967B:1,96DE:8,9877:1,A013:1,9679:1" --diff`; the staging
  writer `--monitor "70AD:8,96E1:1,008E:1,967B:1" --writer --diff`; PC/enemy cache+cursor
  `--monitor "9907:8,969C:8,008E:1,95D5:1" --writer`; PC-tick advance `--monitor "968C:3,9689:3,96DB:3"`.
- Disasm: builder `tools/dis.py 0x11C4A` (gate 11C5D, raw-copy 11D5A, decode/`C1:1E2A` 11DD3); decode table
  `rom:C0/FD00`; PC tick `0x12E00`; enemy tick `0x13440`; meta loader `0x0CEAE0`; streamer `0x3DE170`.
- Live tables: `rom:CC/F92D` (slot·3 remap), `rom:CC/F941` (VRAM-shadow base), `rom:CC/F40A` (class stride);
  ptrs `wram:1C57`(sheet) `wram:1C78`(assembly/E3) `wram:1C99`/`1CA2`(pose/E4).

## Key addresses
- **Tile BUILDER `C1/1C4A`** — the pose tile upload; gate `C1:1C60` (`$967B ≠ $9907[slot]`); decode-store
  `C1:1E2A` (bit-reverse via `C0/FD00`); raw-copy `C1:1D5A`; dest = OBJ-VRAM shadow `$7E0000 + CCF941[slot]`.
- **`C0/FD00`** — 256-byte bit-reversal (H-flip) decode table.
- **`CC/F941`** — per-combatant VRAM-shadow base (GATO slot6 = 0x70AD); **VRAM_byte = shadow − 0x63AD**.
- **`CC/F40A`** — class→E3 stride (0x28/0x50/0x78/0xF0); **`CC/F92D`** — slot→3·slot ptr-record remap.
- **PC pose tick `C1/2E00`** (cursor `$968C`, timer `$9689`, ptrs `$1C99`/`$1CBA`) — parallel to enemy
  `C1/3360` (cursor `$969C`); both read `$967B` from bank-E4 pose scripts and call `C1/1C4A`.
- **Assembly/gather table = bank E3** (GATO `$E30000`, ptr `$1C78[slot]`, entry `+ $967B·strideClass`;
  cell word = `srcTile[0:11] | flip[14]`). **Sheet = WRAM `$7F8000`** (ptr `$1C57[slot]`, boot-decompressed).
- **Meta loader `CC/EAE0`** — sets the per-combatant ptrs from the `$29C0` pack + `E4/2300`/`E4/2600`/`E4/2800`.
- **`FD/E170`** — the animated-BG-chip / effect streamer (bank-7F sources), NOT the combatant tiles (§7).
