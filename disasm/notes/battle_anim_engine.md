# BATTLE ANIM ENGINE — UNIFIED PORT SPEC (synthesis of the five component dossiers)

Synthesizes the DERIVED battle animation engine into ONE port contract. Sources (read those for the byte-level
disasm; this file does not re-derive — it composes and cross-checks):

- **[objslot]** `battle_animeng_objslot.md` — Part 1: the 12-slot TILE/PALETTE **effects streamer** `FD/E39C`.
- **[script]**  `battle_animeng_script.md`  — Part 2: the **pose script** engine (`$8E`→E4 tables→`$1CA2/$1CC3`
  →enemy tick `C1/3360`→assembly index `$967B`).
- **[builder]** `battle_animeng_builder.md` — Part 3: the **assembly→OAM builder** (`$967B`→`$9F47/$9F49`
  signed (dx,dy)→compose `CF/EAC5=EB60`→`$7E0700`).
- **[tiles]**   `battle_animeng_tiles.md`   — Part 3': the **tile-upload** path (`C1/1C4A` gather sheet
  `$7F8000`→OBJ-VRAM shadow `$7E63AD`→DMA) + the **PC pose tick `C1/2E00`**.
- **[corpus]**  `battle_animeng_corpus.md`  — the acceptance timelines (GATO 4-facing attacks, song sway+notes,
  walk leg-cycles, PC hurt).

Integration context: **[interp]** `battle_interp_verify.md` (CD/5FF0 anim-script format + `$8E` linkage),
**[pcapproach]** `battle_pc_approach_facing.md` + **[enemymove]** `battle_enemy_move_target.md` (the movers that
feed live positions + facing — these STAY), **[stance]** `battle_stance.md` + **[actionfx]** `battle_actionfx.md`
(the bakes that get retired), **[loop]** `battle_loop.md` §I/§G (the pinned span constants that go emergent).

**Mandate:** zero invention. Every stage keeps the regression suite green and converts a byte-exact bake into a
fixture. HEAD at authoring = f481bed; the concurrent lane's in-flight `battle_gato_atkfaces.h`/`battle_gato_song.h`
+ `src/{battle,field}.c` edits are treated as stage-2 retire-targets (see §B.2), their diffs ignored otherwise.

---

## TABLE OF CONTENTS
- **§A — the engine architecture as derived** (two subsystems; the full data flow, byte-cited; A.5 = the
  CONTRADICTIONS BETWEEN DOSSIERS that the port must resolve)
- **§B — the staged implementation plan** (B.0 what STAYS; B.1 Stage 1 enemy pose core; B.2 Stage 2 attack/hurt/
  song; B.3 Stage 3 PC anims; B.4 Stage 4 emergent spans)
- **§C — data dependencies** (build-time ROM/WRAM extraction vs runtime)
- **§D — OPEN items consolidated** (from all five dossiers, each with its stage impact)
- **§E — top risks**

---

# §A — THE ENGINE ARCHITECTURE AS DERIVED

## A.1 — Two independent subsystems, one OBJ output

The battle sprite picture is built by **two subsystems that never touch each other's RAM** and only meet at the
OBJ hardware ([objslot] §8):

1. **The POSE / SPRITE-ASSEMBLY engine** ([script]+[builder]+[tiles]). Drives every combatant BODY pose —
   GATO's idle claw-swap, his four facings, his walk legs, his directional rear-up/strike, and every PC's idle
   bob + attack swing. It produces (a) OAM **cell positions** into `$7E0700` and (b) the pose's **tile PIXELS**
   into the OBJ-VRAM shadow `$7E63AD`. Everything a port needs for the combatant sprites is here.

2. **The 12-slot TILE/PALETTE EFFECTS streamer** (`FD/E39C`, [objslot]). Twelve general tile/palette-animation
   channels → the DMA staging buffer `$7E2000/$7E2200`. It is **dormant for the combatant poses** — every slot
   sits at mode 0 through GATO's whole idle + attack + physical ([objslot] §6). Its ONLY activity in the GATO
   fight is the **SONG**, which lights slots 8-11 as **mode 5** (the musical-note colour oscillator, [objslot]
   §6). So subsystem 2 matters to the port only for the SONG's note effects (Stage 2), not for any pose.

A port can implement the two independently; the coupling is that a sprite whose OAM tile-number points at a
streamer slot's dest shows the streamed pixels ([objslot] §8).

## A.2 — The pose-engine data flow (the whole chain, each link cited)

```
 [animId $8E selector]  ── set by ONE of three sources:
    · initial per-combatant table  E4/F601-F603  (CC/EAE0: LDA $E4F603,X → $8E; GATO=0x57)   [script §2]
    · enemy WALK bearing→animId    C1/38F6 (ADC #40 / C1/01F9 / STA $8E)  + $96DE=C0F700[bear] [script §7]
    · CD/5FF0 action script  '06 aa' / '03 aa'  (C1/5FDE: $8E=[$E6],Y)  = the SCRIPTED ATTACK override [script §4]
       │
       ▼  loader CC/EB0A: X=$8E*2 →  E4/2600[X]=ptrA → $1CA2+slot*3  (enemy) / $1C99+PC*3 (PC)   [script §2, tiles §4]
       │                              E4/2800[X]=ptrB → $1CC3+slot*3  (enemy) / $1CBA+PC*3 (PC)
       │                              bank of both = E4 ; per-animId stride $9671/$966E = Δ(E4/2600 entries) [tiles §5]
       ▼
 [per-frame POSE TICK]   enemy = C1/3360→C1/33xx   ·   PC = C1/2E00   (structurally identical) [script §3, tiles §4]
       │   latch facing $96E9←$96DE ; mode $974A←$5E0D ;
       │   FACING DISPATCH:  $82 = $86(class base, CC/FB91[mode]) + facing × $9671(stride; GATO 0x28)  [script §3]
       │   copy 8-byte window ptrA[$82..+7] → $97D9+slot*8      (the assembly-frame set for this facing)
       │   copy 8 durations  ptrB[$86..]→CC/FBCE[]→ $9799+slot*8 (facing-INDEPENDENT dwell units)      [script §3]
       │   DEC timer $9694(enemy)/$9689(PC) ; at 0: cursor $969C/$968C++ (wrap 4) ;
       │   $967B = $97D9[base+cursor]                            ── the ASSEMBLY INDEX                 [script §3]
       ▼
 [$967B]  drives TWO consumers each frame, both gated by the change-cache $9907[cmb] (skip if unchanged) [builder §2, tiles §2]
   ├─► POSITION path  C1/1C4A position pass (11F5F):  $9F47[cmb*16 + 2i] = sign_extend( asm[i] )       [builder §2]
   │      asm = [ $1C78[pidx] + $967B × CCF40A[type] ]   (GATO base E3/0060, stride 0x78, 24 bytes=12 cells) [builder §4]
   │      →  compose CF/EAC5=EB60:  OAM.x = $9F47[dx] + ($1D0C+cmb) ; OAM.y = $9F49[dy] + ($1D23+cmb) ;
   │         attr = pal|prio (KEEPS the tile#+flip already in $0702)  →  $7E0700                       [builder §3]
   │      tile NUMBERS + base attr come from the $922D assembler (CF/E6D6..E7C3 → CF/EEC3 copy)         [builder §5]
   └─► PIXEL path     C1/1C4A cell-word pass (11CF8):  for each 16-bit cell word w in the E3 record:
          srcTile = w & 0x07FF ; flip = w & 0x4000 (H-flip via bit-reverse table C0/FD00) ; w==0 → skip [tiles §2]
          pixels = sheet[ $7F8000 + srcTile*0x20 ]  (ptr $1C57[slot], boot-decompressed)                [tiles §2,§5]
          → OBJ-VRAM shadow $7E0000 + CCF941[slot] (GATO=$7E70AD; VRAM_byte = shadow − 0x63AD)          [tiles §2,§3]
          → per-frame wholesale DMA ships $7E63AD → VRAM word 0                                          [tiles §3]
```

**In one sentence:** an action op (or the initial table, or the walk bearing) sets `$8E`; `$8E` selects a
bank-E4 pose script; the per-frame tick reads a facing-windowed, timed slice of it into the assembly index
`$967B`; `$967B` indexes the bank-E3 record whose bytes are BOTH the per-cell signed (dx,dy) **positions**
(→`$9F47`→compose→`$7E0700`) AND the per-cell (srcTile|flip) **tile gather list** (→sheet `$7F8000`→VRAM
shadow `$7E63AD`→DMA); the tile NUMBERS come from the separate `$922D` assembler.

**How the SPANS emerge** ([script] §4, [interp] T2.C): during a scripted attack the CD/5FF0 executor rewrites
`$8E` repeatedly (windup→strike→recover), sequenced by its `20`-pause ops; the frozen span (`$A013`=1) is
exactly how long the CD script takes to run all tracks to end. The `1E` super-command op's frame position is
the HP-apply/strike-flash beat; `50`-ShowDamage is the popup birth. So the pinned span constants are the
runtime of the interpreter (§B.4).

## A.3 — The effects streamer (subsystem 2) in detail — for the SONG notes

`FD/E39C` (RTL far target), DP=`$0500`, **12 slots × 12 bytes** (`$0520..$05AF`), ticked once/frame ([objslot]
§1-3). Per-slot record `{+0 mode<<4|count, +1 dest, +2 size, +3 frameIdx/step, +4 tick, +5 period, +6..8
srcPtr24, +9..11 listPtr24}`. Tick: `+4++`; at `+4==+5` advance (modes 1&8 bump `+3`, wrap at `(+0&0x0F)+1`);
mode 0 = inactive. **8 handlers** ([objslot] §4): modes 1&8 = ROM→buffer tile streamers (bank F6); 2&3 =
buffer scrollers; **4-7 = BGR555 palette OSCILLATORS** (`+7 += +3` signed, scale each colour component). The
song lights slots 8-11 to **mode 5** (`+0=0x50`, src `$80BF00`, period 3, [objslot] §6) — the sweeping note
colour effect. Slot records are loaded from a header+frame-list table at `$FDFExx` ([objslot] §5). The port
shape ([objslot] §8): 12 records, a per-frame tick, the 8 handlers, and a loader expanding `$FDFExx`. **The
streamed note pixels come from bank F6 (a bake target); its writer (the song tech-effect setup) + the
`$7E2000→VRAM` DMA site are OPEN** ([objslot] §9).

## A.4 — What the C port actually emits (engine boundary vs our software renderer)

Our engine has **no `$7E0700` OAM shadow and no VRAM** — it software-blits 8×8 cells straight to the framebuffer
(`battleBlitCell`, field.c) at a live foot. So the port does NOT reproduce `$7E0700`/`$7E63AD` literally; it runs
`tick → $967B → E3 assembly (dx,dy) + sheet gather (pixels)` and, per cell, blits the gathered tile at
`(liveFoot + dx, liveFoot + dy)` with the combatant palette. The **live foot** is the mover output
(`h->mvLiveX/Y`, [enemymove]/[pcapproach] — STAYS). This is the direct analogue of the compose pass
(`OAM.x = dx + $1D0C+cmb`, [builder] §3): our `liveFoot` == `$1D0C+cmb`, our `battleBlitCell` == one compose
iteration. **What changes from today: the SOURCE of (tile, dx, dy)** — today a baked keyframe selected by
`mvFacing`/`fxEnemyRearup`; after the port, the pose tick's `$967B` → E3 assembly + sheet gather.

## A.5 — CONTRADICTIONS BETWEEN THE DOSSIERS (items the port MUST resolve — do not paper over)

> These are flagged, not resolved. Each blocks byte-exactness of a specific stage; the resolution requires ROM
> work the dossiers explicitly deferred. **Resolve by disassembly + oracle before claiming byte-exact, not by
> guessing.**

**C1 — `C1/1C4A`: is it the POSITION builder or the TILE builder? (the big one — blocks Stage 1)**
The same routine `C1/1C4A` is described as two different machines reading the SAME `[$BA]` (the E3 record at
`base + $967B×stride`):
- [builder] §2/§4: `C1/1C4A` = "BuildAssembly". Its **position pass** (11F5F) reads `[$BA],Y` as **24 signed
  bytes → 12 (dx,dy) pairs → `$9F47`**; triple-confirmed against the oracle `$BA` (GATO idle idx5 = E3/02B8,
  24 bytes = the (dx,dy) layout).
- [tiles] §2/§5: `C1/1C4A` = "the tile BUILDER". Its **cell-word pass** (11CF8) reads `[$BA]` as **16-bit
  cell-words `srcTile|flip`** → gathers sheet tiles → VRAM shadow. Validated cell-for-cell (GATO song-DOWN
  idx0x0D = E3/0618, 21/21 cells placed). [tiles] never mentions `$9F47` or a position pass.
So the E3 0x78-byte record is read as **24 signed dx/dy bytes** (builder) AND as **16-bit srcTile|flip
cell-words** (tiles). These cannot both be the same bytes at the same offset. **[builder] §7/§10 itself flags
the unreconciled part:** "the first loop INCs `$BA` while the second reads `[$BA]` at the block start… the
internal ordering (is the cell-word pass a no-op for 12-cell enemies, or a second entry point per caller?) is
unreconciled. The 7 callers may enter at distinct offsets." **Resolution direction (to PIN, not assume):** the
0x78-byte record is almost certainly STRUCTURED — a cell-word sub-block (tile gather) AND a dx/dy sub-block
(positions), the two passes reading different phases of `$BA`, OR the 7 callers (`C1/2F10,345D,3702,416A,418B,
41AC,4307`) entering `C1/1C4A` at distinct offsets for the two jobs. **Disassemble `C1/1C4A` per-caller and dump
the FULL 0x78-byte GATO record structure** to split position-bytes from cell-words before Stage 1 is byte-exact.
(Both outputs are independently validated, so an approximation that bakes E3 as two separate tables — one dx/dy,
one cell-word — is the sanctioned interim; the true single-record layout is the open item.)

**C2 — `FD/E170` vs `FD/E39C` vs `C1/1C4A`: three streaming paths, historically conflated.**
- [objslot] §8/§9: `FD/E39C`'s effect buffer is `$7E2000/$2200`; it cites (from battle_boot §2) "the DMA is
  ch7 `$7F04D0`/`FD:E170`" but flags the DMA site as its own OPEN.
- [tiles] §7 CORRECTS this: `FD/E170`'s channels source `$7F04D0 = 6C40 6CC0 79C0…`, bank `$05D2=7F` — all
  **bank-7F** — and GATO's pose tiles are NOT found at any `$7F` offset (only at the bank-7E shadow `$7E70AD`).
  So **`FD/E170` = the animated-BG-chip / effect streamer (bank-7F sources), NOT the combatant tiles**; the
  combatant pixels are `C1/1C4A → $7E63AD → wholesale DMA`.
**Reconciliation:** there are (at least) THREE paths — (1) `C1/1C4A→$7E63AD` = combatant POSE pixels [tiles];
(2) `FD/E39C→$7E2000` = the 12-slot tile/PALETTE effects, incl. the song notes [objslot]; (3) `FD/E170`
(bank-7F `$7F04D0`) = animated-BG chips [tiles §7]. `battle_boot §2`/`battle_stance S1/S4`'s "FD/E170 =
combatant tile streamer" and [objslot] §8's "my buffer's DMA is FD/E170" are the SAME stale mis-attribution.
**Item to resolve:** where `FD/E39C`'s `$7E2000` buffer actually DMAs to VRAM (its own OPEN §9), needed for the
Stage-2 note effects. Do NOT reuse `FD/E170` for it without confirming the source bank.

**C3 — the `$967B` value space: raw ptrA bytes (04/0x14) vs E3 index (04/05). (blocks Stage 1 idle byte-exactness)**
- [script] §3/§6: the enemy tick does `C1/3425 LDA $97D9,Y / STA $967B` (a DIRECT store), and `$97D9` was
  copied from `ptrA[$82]`; for GATO idle facing-1, `ptrA[0x34]=04 14 FF 00` → `$967B` ∈ {04, **0x14**}.
- [builder] §8: the oracle-`$BA`-pinned live `$967B` alternates **04↔05** (E3/0240↔E3/02B8), and notes "the
  0x14 is a bank-E4 pose-SCRIPT byte (`$97D9`) that the pose tick maps to `$967B=05` — a re-map inside the
  tick, not the builder."
So [script]'s direct `LDA $97D9/STA $967B` implies `$967B` takes the raw ptrA byte 0x14, but [builder]'s oracle
read shows `$967B=05`. **Item to resolve:** either an unmodelled `$97D9`→`$967B` remap exists (builder's claim,
underived) or one decode mislabeled its window. **Pin the exact `$97D9`→`$967B`→E3-index chain** (re-read the
tick around C1/3425 + oracle `$97D9`/`$967B` at the idle-swap frames) before the idle claw-swap is byte-exact
from scripts. Until then the E3 assembly INDICES (04↔05, oracle-`$BA`-pinned) are authoritative over the raw
ptrA bytes.

**C4 — idle/walk poses: pose-tick path, NOT the CD/`$8E` path (a correction [tiles] makes to [script]).**
[script] §3 wrote "PCs use the CD/5FF0 anim-script path directly for their poses." [tiles] §4 REFUTES this:
`$8E` stays 0 the whole idle; Crono's idle bob is the **`C1/2E00` E4 pose-script tick**, not the CD path.
[script] §4's `$8E` rewrites were the **scripted-ATTACK override** only. **Resolution (settled, [tiles]
supersedes [script]):** idle/walk/facing poses = the per-frame tick (`C1/3360` enemy / `C1/2E00` PC) off the E4
pose scripts; attacks = the CD/5FF0 `06 aa`→`$8E` override that draws the assembly directly ([script] §4). The
port wires BOTH into the same `$8E`→E4→`$967B` machine; the distinction is only WHO writes `$8E`.

**C5 — the sheet `$7F8000` provenance (a shared boundary, not a conflict).** [tiles] §5/§8: the sheet is
WRAM `$7F8000`, boot-decompressed from ROM; the compressed ROM source (keyed by enemy id 0x92) + the
decompressor are the SAME boundary battle_boot/battle_stance S1 flagged. Interim (sanctioned, §C): bake the
`$7F8000` sheet from an oracle WRAM dump (like `ct_charbase.h`), flagged as a fixture until the decompressor is
derived.

---

# §B — THE STAGED IMPLEMENTATION PLAN

Ordering rationale: **Stage 1 stands up the WHOLE enemy machine** (tick + builder + compose + sheet + all
tables) driving GATO's idle/walk/facings. Stages 2-4 then reuse that machine incrementally — Stage 2 adds the
CD/5FF0 `$8E`-override path + the FD/E39C note streamer; Stage 3 adds the parallel PC tick `C1/2E00`; Stage 4
adds the CD/5FF0 per-frame track interpreter so spans emerge. Each stage converts byte-exact bakes into
FIXTURES: because every bake is a byte-exact snapshot OF the engine output, a correct port reproduces its fnv
1:1 — **the ideal re-pin count per stage is ZERO** (as the mover port achieved, [pcapproach]/[enemymove]
ADDENDA: "empty re-pin table"). Any fnv shift on a MASH pin = a port bug, not a legitimate change.

## B.0 — What STAYS (adapted, never replaced)

The engine replaces ONLY the pose-cell SOURCE. Everything else in the battle render/logic is already
ROM-derived and stays:
- **The movers** — enemy walk (`battleMove*`, [enemymove]) and PC approach (`battlePcAtk*`, `ctPcMoveTrack`/
  `battle_tracks.h`, [pcapproach]) — they own the LIVE FOOT the engine draws at. `battle_trig.h`
  (atan/sin/face tables) stays.
- **The facing var** `mvFacing[slot] = ctBattleFaceTab[bearing]` ([pcapproach] §2.2) — it feeds the tick's
  facing dispatch (`$82 = $86 + facing×stride`). Stays; the engine CONSUMES it instead of selecting a bake.
- **The formation anchor** `16·rel+fine+extent` ([stance] ADD.8, `battleEnemyAnchorDelta`) — the staged
  home the live foot initializes from. Stays.
- **The HUD** (`CtBattleHudRender`, `battle_hud.h`), **damage numbers** (`battle_fx.h`, [actionfx] P1),
  **hurt-flash palette** (`ctBattleFxFlashPal` pal4-white swap, [actionfx] P3), **crit flash**, the
  **selection bracket** (`battle_bracket.h`), the **song SPOTLIGHT** screen effect (`battleSongFxRender`,
  `battle_songbeam.h`, [corpus] §2.3 "✓ match"), the **dissolve** (`battle_dissolve.h`), the **results window**
  — all stay.
- **The AI interpreter** (`battleAiRunTurn`, FD/BA4A cursor, [enemymove] ADDENDA), the **damage pipeline** +
  **ATB** ([loop]) — stay. Stage 4 only makes the anim SPAN emergent, not the AI decision.
- The **entrance / settle / victory** ladders (`battle_entrance.h`, `battle_settle.h`, `battle_end.h`) are also
  bakes of this same engine but are OUT of the four-stage scope (they can be derived later the same way).

## B.1 — STAGE 1: the ENEMY pose-engine core (GATO idle / walk / facings)

**Goal.** Drive GATO's idle claw-swap, his four facings, and his walk legs from the real E4 scripts + E3
assemblies + the `$7F8000`-equivalent sheet, at the live foot the mover already computes.

**Retire (→ fixtures):**
- `ctGatoIdleTrajV2` + `CT_GATO_IDLE2_N=120` (`battle_anim_add.h`) → the tick emits `$967B` alternating idx4↔idx5
  (E3/0240↔E3/02B8), the ptrB durations (via CC/FBCE) reproducing the 120f period (27/34/27/33 dwells,
  [script] §6). **[builder] §8/[script] §6 = the fixture** (idx4/idx5 == kf6/kf7).
- `ctBattleAnimKf` kf6/kf7 (`I0_armsRest`/`I1_armsSwap`, `battle_anim.h`) → E3 assembly idx4/idx5.
- `ctGatoFaceKf` + `ctGatoFaceCell` + `ctGatoFaceTiles0/1/2` (`battle_gato_faces.h`) → the `$96DE` window
  dispatch (`$82=$86+facing×0x28`) + the bearing→animId map ([script] §7); UP/LEFT/RIGHT emerge as their own
  E3 assemblies (walk-LEFT idx6-8, walk-RIGHT idx9-11, [builder] §8).
- The **walk-slide placeholder** — the `battleGatoRender` fallback that slides the IDLE bob pose at the live
  position while walking (field.c "he slides in his IDLE bob pose … the leg animation is the follow-up bake")
  → real walk poses idx6-11 + the per-facing leg-cycle tile stream ([corpus] §3.2).
- GATO's slice of `ctBattleStanceObj`/`battle_stance.h` (the settled entry stance) → subsumed by the idle tick.

**Bake-from-ROM (generators — the extractors that replace the snapshot generators):**
- **E4 pose scripts** ptrA(`$E472C0`)/ptrB(`$E4BF09`) for GATO animId 0x57 + the walk-bearing animIds
  ([script] §5); the tables **E4/2600, E4/2800, E4/F601** ([script] §2).
- **E3 assembly table** GATO base `E3/0060`, stride 0x78, 24-byte records ([builder] §4) — dumped as BOTH a
  dx/dy table and a cell-word table until C1 is resolved (§A.5).
- **The `$7F8000` sheet** for GATO (bake from oracle WRAM, §C, C5 interim).
- **The index/stride tables**: `CC/F40A` (type→stride), `CC/F4FC` (type→byte count), `CC/F50A` (class→cell
  count), `CC/F92D` (cmb→pidx=cmb*3), `CC/F917` (cmb→`$9F47` off), `CC/F941` (cmb→VRAM-shadow base),
  `CC/F3B6`/`F3C2` (per-cell 16×16 grid walk), `CC/F82F` (slot*8), `CC/FBCE` (duration map, FULL continuation),
  `CC/FB91` (mode→class), `CC/FBAB` (mode→class, mover — already in `battle_trig.h`? confirm). The per-monster
  window stride `$9671` (GATO 0x28) + class base `$86`. **`C0/FD00`** bit-reverse (already provable via
  `--read rom:C0/FD00`, [tiles] §2).

**Code shape (which layers change):**
- ADD `battleEnemyPoseTick(host, slot)` in `battle.c` (port `C1/3360`→`C1/33xx`): latch facing/mode; facing
  dispatch `$82=$86+facing×stride`; copy the 8-byte ptrA window + 8 ptrB durations; DEC timer, advance cursor,
  emit `h->poseAsmIdx[slot]` (=`$967B`). State per slot: `poseAsmIdx, cursor, timer, animId`.
- ADD `battlePoseBlit(fb, combatant, footX, footY, pal)` in `field.c` (port `C1/1C4A` + `CF/EB60`): resolve the
  E3 record at `base + poseAsmIdx×stride`; for each cell, gather its sheet tile (bit-reverse if flip, `C0/FD00`)
  and blit at `(foot + dx, foot + dy)` with pal1. Reuse the existing `battleBlitCell` per cell.
- REPLACE the `battleGatoRender` idle branch (field.c ~2303-2317) and the facing branch (~2289-2302) with a
  single call to the tick+blit. The mover/anchor/knockback/dead-standing wrappers (gdx/gdy, `deadStandFrame`
  latch, `fxKnockTarget`) are UNCHANGED — they still nudge the live foot.
- The `$922D` tile NUMBERS are the fixed per-combatant order (GATO 0x68,6A,6C,6E,80,82,84,86,88,8A,8C,8E,
  [builder] §4) — bake as a per-combatant tile-order table (interim for the underived `$922D`, §E risk).

**Acceptance ([corpus] + [script]):**
- Idle claw-swap emerges at **period 120** (arm-A 87f, arm-B 33f, [stance] ADD.8 / [script] §6).
- The four facings render distinct (UP/DOWN/LEFT/RIGHT), each its own E3 assembly, `hf=vf=0` ([corpus] §3.1,
  [pcapproach] §2.4 "four fully-distinct sets, not 3+flip").
- The walk shows the leg-cycle tile stream (LEFT 3 states, RIGHT 3, UP 2, [corpus] §3.2) — closes E.6/P4.

**Regression strategy:** MASH pins (`field-battleloop-gato`, `field-battlehud-gato`, `field-battletest-gato`)
MUST stay byte-identical — GATO's when-hit block2 never walks, stays facing-1 idle, which the engine must
reproduce byte-exact (the 120f loop). `field-battle-gato-walk`, `field-battle-gato-facing-left/-right` are
engine-driven snapshots of the SAME bakes → ideally zero re-pin; a shift there = a tick/gather bug. Legitimate
shift ONLY if the engine surfaces the leg-cycle stream (new pixels the static facing bake lacked) — then
re-pin those two facing checks with the leg frames, justified by [corpus] §3.2.

## B.2 — STAGE 2: attack / hurt / song PRESENTATIONS emergent

**Goal.** GATO's directional attacks, the song pose + notes, and the attack-B flurry EMERGE from the CD/5FF0
`06 aa`→`$8E` override into the Stage-1 engine — plus the FD/E39C streamer for the note colour effects.

**Retire (→ fixtures):**
- `ctGatoSwipeKf` + `ctGatoSwipeObj` + `ctGatoSwipeTiles` (`battle_anim_add.h`, the attack-A claw-swipe GS1/2/3
  AND the attack-B flurry frames) → CD/5FF0 `06 aa` sequence → E3 rear-up/strike indices. [actionfx] A4 = fixture.
- `ctBattleAnimKf` kf8 (`G1_rearup`, `battle_anim.h`) → E3 FRONT rear-up idx20-23 ([builder] §8).
- `ctGatoAtkFace` + `ctGatoAtkCell` + `ctGatoAtkTiles0-4` (`battle_gato_atkfaces.h` — **the concurrent lane's
  in-flight directional-attack bake**, untracked at HEAD) → the four directional rear-ups/strikes = E3
  idx16-34 ([builder] §8 "LEFT/RIGHT/UP rear-ups + strikes live here"). [corpus] CAPTURE 1 = the 4-facing fixture.
- `ctGatoSongCell` + `ctGatoSongTiles` + `ctGatoSongSway` (`battle_gato_song.h` — **concurrent lane**, untracked)
  → GATO's song = his FRONT idle assembly with the gentle claw sway; the sway is a pose-script frame set (or the
  note colour-osc), emergent from the CD/9EDA tech script. [corpus] §2.1 = fixture.
- `ctSongNoteGlyphTile` + `ctSongNoteTiles` + `ctSongNotePal0` (`battle_gato_song.h`) → the note SPRITES = the
  CD/9EDA note machine (5 moving-effect tracks, [corpus] §2.2) whose COLOUR effect is the **FD/E39C slots 8-11
  mode-5 palette oscillator** ([objslot] §6). [corpus] §2.2 spawn/drift/lifetime = the acceptance target.

**Bake-from-ROM:** the CD/5FF0/CD/61F0 attack + tech anim scripts (GATO-A `CD/7A42`, GATO-B `CD/7B72`, song
`CD/9EDA`, [interp] T2.A) — needed by Stage 4 too; the E3 rear-up/strike assemblies idx16-34 (four facings);
the FD/E39C `$FDFExx` slot header + frame-list for the song notes + the bank-F6 note pixels + the `$80BF00`
palette-osc params ([objslot] §5/§6). Note glyph tiles 0x0E6/0E8/0EA are STATIC in VRAM ([corpus] §2.2) —
bake once.

**Code shape:**
- WIRE the CD/5FF0 `06 aa` op to write `h->animId` (=`$8E`) which the Stage-1 tick already consumes ([script]
  §4 — attacks draw the `$8E` assembly directly via C1/5FE3+; the mode-machine reload is for walk/idle only).
- REPLACE the `battleGatoRender` `fxEnemyRearup` branch (field.c ~2215-2278, incl. the directional
  `ctGatoAtkFace` selection and the attack-B flurry) with the `$8E`-driven pose. The single strike/flash/popup
  still fires from `battle.c` (`fxEnemyRearup`/`CT_GATO_STRIKE`) — damage staging UNCHANGED.
- REPLACE the song branch (field.c ~2197-2210, `battleGatoSinging`) with the CD/9EDA-driven front-idle+sway
  pose; ADD a note-effects layer driven by the FD/E39C streamer (slots 8-11 mode-5) — a NEW subsystem (§A.3).
  The spotlight (`battleSongFxRender`) is unchanged.

**Acceptance ([corpus]):** four byte-distinct directional attacks (CAPTURE 1, all 4 VRAM md5s distinct); the
song front-idle + 6px sway + up-to-5 floating notes over span+44→+207 (CAPTURE 2); attack-B flurry
(field-battle-gato-flurry).

**Regression strategy:** `field-battle-gato-song-attackA`, `field-battle-gato-flurry` are engine snapshots →
zero re-pin if byte-exact. The note SPRITES are a genuinely new layer (today "none", [corpus] §2.3) → the song
pins gain note pixels; re-pin justified by CAPTURE 2. RIGHT (Marle-target) + FRONT (Crono-target) are the
in-fight facings; LEFT/UP attacks are non-canonical for the GATO formation ([corpus] §1.4) — verify via the
patched side-attack states, don't expect them in a natural GATO pin.

## B.3 — STAGE 3: PC anims via the parallel tick `C1/2E00`

**Goal.** Crono's idle bob + attack swing and Marle's crossbow emerge from the PC pose tick `C1/2E00` (parallel
to the enemy `C1/3360`, feeding the SAME builder), retiring the capture-absolute trajectory bakes and the
single-base teleport workaround.

**Retire (→ fixtures):**
- `ctCronoAttackTraj` + `CT_CRONO_ATK_N=91` (`battle_anim.h`, the 7-pose swing ladder) → `C1/2E00` tick + Crono
  E4 pose script + DC/27D0 assemblies (base DC/27D0, stride 0x28, 4 cells, [builder] §6). [actionfx] A2.1 = fixture.
- `ctCronoIdleBob` + `ctCronoIdleAddKf`/`ctCronoIdleAddTiles` (`battle_anim.h`/`battle_anim_add.h`, the 3-variant
  idle bob) → the tick's cursor alternating assembly C6↔4F↔C7 ([tiles] §4, [builder] §6). [stance] A5.1 = fixture.
- Crono/Marle slices of `ctBattleStanceObj` (`battle_stance.h`) → the `C1/2E00` tick (waiting PC = static cursor).
- `ctMarleAtkKf` + `ctMarleAtkTraj` + `ctMarleSparkKf` (`battle_marleatk.h`) → Marle's E4 script (animId 0x0F,
  [actionfx] MA.1); her 4-pose ladder = the tick; the bolt spark = a CD effect track / FD/E39C (planted, no
  foot move — the mover already leaves her in place, [pcapproach] §1.8).
- The **single-base teleport workaround** (the strike-rep rebase in `battleSkeleton`/`battleAttackRender`,
  HEAD f481bed) → the tick positions poses at the live foot natively; the PC approach mover ([pcapproach] P1)
  owns ALL travel, so no capture-travel rebase is needed ([pcapproach] ADDENDUM: "resolves for free when the
  real anim engine port lands — the poses will then be positioned by the engine, not by capture-absolute cells").

**Bake-from-ROM:** Crono/Marle E4 pose scripts + the DC/27D0 (Crono) / DC-adjacent (Marle) assembly tables
([builder] §6/§10); the PC sheets to `$7F8000`. Same builder/compose as Stage 1 (proven shared, [builder] §6
"7 callers cover both PCs and enemies").

**Code shape:** ADD `battlePcPoseTick` (port `C1/2E00`, PC-indexed vars `$1C99/$1CBA/$9689/$968C/$96DB/$966E`,
[tiles] §4); the `battlePoseBlit` from Stage 1 is reused (shared builder). REPLACE the PC attack render
(`battleAttackRender`, field.c ~2348) and the Crono/Marle idle layers; DROP the rebase math. The PC approach
mover + Marle's planted render are UNCHANGED.

**Acceptance:** Crono's 7-pose swing (A1 approach → A6 settle, [actionfx] A2.1) plays continuously at the live
foot (no teleport); the idle bob 3 variants lock to the tick cursor ([stance] A5.1); Marle's aim→fire→spark
([actionfx] MA) with no lunge. Waiting PC static ([stance] A5.2).

**Regression strategy:** `field-battle-pc-approach-early/-late` legitimately SHIFT — the workaround's "bounded
~2px/f schedule desync" ([pcapproach] ADDENDUM) is replaced by the true engine positioning; the mid-run frames
move (the fix's whole point). Re-pin justified. The stance verify (`render-scene6-gato`) must stay byte-exact
for the settled poses.

## B.4 — STAGE 4: SPANS emergent from the CD/5FF0 interpreter

**Goal.** The frozen action span STOPS being a pinned constant and becomes the runtime of the CD/5FF0 anim
script — "run the script; the span is however long it took" ([interp] P2.B, [loop] §I).

**Retire (→ fixtures):**
- The pinned span table in `battle.c`/[loop]: PC physical **125**, crit **146**, GATO-A **157**, GATO-B **224**,
  song **311** ([loop] §I) + `CT_ENEMY_SPAN`/`copyFreeze`/`CT_GATO_WINDOW`/`CT_GATO_STRIKE=27` → EMERGE = the
  number of frames the CD script runs all tracks to end (`$A013`=1 for exactly that span, [loop] §I "frozen-span
  length = passes until the anim engine sets `$9876 != 0`").
- The pinned strike beat (`CT_GATO_STRIKE`, `CT_FX_FLASH_DUR`) → emerge from the `1E` super-command op's frame
  position in the CD exec track ([interp] T2.C: "`1E`↔strike-flash, `50`↔the damage-number popup").

**Bake-from-ROM:** the CD/5FF0 (attacks) + CD/61F0 (tech) script bytes + the L1.2 opcode table ([interp] T2.B)
— the multi-track header + per-track opcode streams; the per-opcode frame durations (`20 dd` pause, `03/06 aa`
playanim playback lengths pull the pose-tick subsystem — the residual boundary, [interp] RESIDUAL).

**Code shape:** ADD a per-frame CD-track interpreter (port the `C1/5Bxx-5Fxx` executor + `C1/4090` frame loop):
per active track, dispatch the opcode (`20`=hold, `03/06 aa`=set `$8E` [feeds Stage 2's pose], `1E`=apply
damage, `50`=popup, `00/01`=end). `battleActionFreeze` (our existing `$A013` mirror) is HELD for as long as the
interpreter runs — its DURATION becomes emergent, not the pinned constant. The AI decision (which attack) is
UNCHANGED ([enemymove]); only the anim TIMING emerges.

**Acceptance ([loop] §I/§G):** the emergent spans reproduce 125/146/157/224/311; the `1E`-super lands on the
baked strike keyframe; `50`-ShowDamage fires at the popup birth. The full pacing ledger ([loop] §G) sums
byte-exact (INTRO 159 + TICKING 395 + FROZEN 1165 + VICTORY 231 = 1950).

**Regression strategy:** `field-battleloop-gato` re-pins to the emergent (longer/identical) victory frame —
damage/turn-order/outcome unchanged ([loop] PART 2 "pure per-span duration"). If the interpreter reproduces the
observed span lengths exactly, the victory frame is byte-identical and no re-pin is needed; a difference in the
emergent length is a span-interpreter bug to fix, not a re-pin.

---

# §C — DATA DEPENDENCIES (build-time extraction vs runtime)

**Extracted from ROM at build time (baked, provenance like `battle_trig.h`/`ct_charbase.h`):**
| datum | ROM location | used by | note |
|---|---|---|---|
| E4 pose scripts (ptrA seq + ptrB durations) | bank E4 (GATO `$E472C0`/`$E4BF09`) | tick | [script] §5 |
| E4/2600, E4/2800, E4/F601 tables | E4 | `$8E`→ptr loader | [script] §2 |
| E3 assembly table (dx/dy + cell-words) | bank E3 (GATO `$E30000`/`E3/0060`, stride 0x78) | builder+gather | split per C1 (§A.5) |
| DC/27D0 (Crono) + DC-adj (Marle) assemblies | bank DC | PC builder | [builder] §6 (stride 0x28) |
| CC index tables F40A/F4FC/F50A/F92D/F917/F941/F3B6/F82F/FBCE/FB91 | bank CC | tick+builder | [builder] §4, [script] §3 |
| `C0/FD00` bit-reverse (H-flip) | `C0/FD00` (256B) | gather | [tiles] §2 (`--read`-provable) |
| CD/5FF0, CD/61F0 anim scripts + opcode durations | bank CD | Stage-4 interpreter | [interp] T2 |
| FD/E39C `$FDFExx` slot headers + frame-lists | bank FD `$FE00+` | note streamer | [objslot] §5 |
| note colour-osc params + note pixels | `$80BF00` / bank F6 | note effects | [objslot] §6 |

**Read at RUNTIME (from the staged battle blocks / mover state — NOT baked):** the animId `$8E` (from the AI/
CD script), the facing `mvFacing[slot]` (from the mover), the live foot `mvLiveX/Y` (from the mover), the mode
`$974A`, the per-frame cursor/timer/`$967B` (engine state). These come from the already-ported movers + AI, not
from a table.

**INTERIM fixtures (flagged, until the underived boundaries close):**
- **The `$7F8000` sheet** — until the boot decompressor is derived (C5, [tiles] §8), bake the decompressed
  sheet per combatant from an **oracle WRAM dump** (like `ct_charbase.h`). Flagged as an interim fixture.
- **The `$922D` tile-NUMBER order** — until the `$922D` assembler is decoded ([builder] §10), bake the fixed
  per-combatant tile-number sequence ([builder] §4). The compose PRESERVES tile numbers, so a static order
  suffices for the observed poses (risk in §E).
- **The E3 record split** (dx/dy vs cell-words, C1) — until `C1/1C4A` is disassembled per-caller, bake E3 as
  TWO parallel tables (one dx/dy, one srcTile|flip), each independently oracle-validated.

---

# §D — OPEN ITEMS CONSOLIDATED (with stage impact)

| # | open item | source | blocks / interim |
|---|---|---|---|
| O1 | **`C1/1C4A` record structure** — dx/dy pass vs cell-word pass reading the same `$BA`; 7 callers may enter at distinct offsets | [builder] §7/§10, C1 | **Stage 1 byte-exactness.** Interim: two parallel E3 tables, each validated |
| O2 | **`$922D` tile-number assembler** provenance (CF/E6D6..E7C3) — the per-cell tile-number + base-pos origin | [builder] §5/§10 | **All stages.** Interim: static per-combatant tile-order bake (§C). Top risk (§E) |
| O3 | **`$7F8000` sheet decompressor** — the ROM compressed source (enemy id 0x92) + boot decompress | [tiles] §8, [stance] S1, C5 | **All stages.** Interim: bake sheet from oracle WRAM |
| O4 | **`$97D9`→`$967B` remap** — script says `$967B`∈{04,0x14}, builder says {04,05} | [builder] §8, [script] §3, C3 | **Stage 1 idle byte-exactness.** Use the oracle-`$BA` indices (04/05) as authoritative |
| O5 | **FD/E39C `$7E2000`→VRAM DMA site** + the song note-slot writer | [objslot] §9, C2 | **Stage 2 notes.** Don't reuse FD/E170 without confirming source bank |
| O6 | **4-facing window map inside one animId** — only idle facing-0 (0x0C) + facing-1 (0x34) byte-pinned; left/right may use their OWN animIds (bearing→`$8E`) | [script] §7/§9 | **Stage 1 facings.** Disambiguate with a walking-GATO capture |
| O7 | **duration map CC/FBCE continuation** (bytes >0x0F) + the cadence multiplier (1-2 timer units → ~30f dwells) | [script] §9 | **Stage 1 timing.** Mechanism clear, frame-exact dwell un-tabulated |
| O8 | **the `$0900` high-OAM math** + directional attack anchors (idx24-34, only FRONT idx20-23 cell-exact) | [builder] §10, [corpus] §1 | **Stage 2 side attacks.** Per-facing `$967B` capture needed |
| O9 | **note per-note drift LAW** (C1/62xx effect subsystem, `$5D80/$5DA0`) — only spawn/drift/lifetime observed | [corpus] §2.2, [objslot] §9 | **Stage 2 notes.** Observed timeline is the acceptance target |
| O10 | **within-pose per-frame tile stream** (sword-arc/claw/mouth/leg pixels) — the C1/CC streamer at sub-frame | [corpus], [actionfx] A2.2/A4 | **All stages fidelity.** Rep-frame tiles are the bake unit; per-frame stream is the standing boundary |
| O11 | **per-opcode `03/06` playback frame durations** (pulls the pose-tick) + per-frame CD dispatch | [interp] T2.C/RESIDUAL | **Stage 4 span composition.** Span LENGTHS observed/pinned; internal makeup open |
| O12 | **PC pose tick byte-exactness** + Marle/other-enemy assembly bases (bank DC, other monsters) | [tiles] §4, [builder] §10 | **Stage 3.** Crono (DC/27D0) + GATO (E3/0060) pinned; others same format, un-read |

---

# §E — TOP RISKS

1. **`$922D` tile-NUMBER provenance (O2).** The compose pass (`CF/EB60`) sets only X/Y/pal/prio and PRESERVES
   the tile-number + flip bits already in `$0702` ([builder] §3) — those come from the underived `$922D`
   assembler (`CF/E6D6..E7C3`, applies a per-sprite `$A0E7` Y offset). The interim static per-combatant
   tile-order bake ([builder] §4: GATO 0x68..0x8E fixed) reproduces every OBSERVED pose, but if any pose
   REORDERS tiles or `$922D` injects a per-frame tile-number change, the static bake breaks silently. **Mitigate:**
   validate the tile-number sequence is invariant across ALL of a combatant's `$967B` values before trusting the
   static bake; decode `$922D` if a mismatch appears (esp. attacks/techs beyond the GATO fight).

2. **Per-facing attack anchors + the LEFT≠mirror(RIGHT) 4-set assumption (O6/O8).** [corpus] §1.1 proved GATO's
   four directional rear-ups are wholly distinct (LEFT ≠ mirror(RIGHT), taller side/up rear-ups reaching +80 vs
   front +64), and only the FRONT (idx20-23) is cell-exact-matched to the E3 table; LEFT/RIGHT/UP anchors
   (idx24-34) each need their own `$967B` capture. **Risk:** synthesizing a facing from another (h-flip) — a
   forbidden invention ([corpus] §1.1, [pcapproach] §2.4). **Mitigate:** pin each facing's `$967B` from a
   side-attack capture (the patched `crono_left/right/up.state`, [corpus] method) before emitting it; keep the
   FRONT default byte-exact so every MASH pin is unaffected.

3. **Timing parity when spans go emergent (O11, Stage 4).** The pinned fights (`field-battleloop-gato`, the
   §G ledger 1950f) depend on span lengths 125/146/157/224/311 that today are constants. When Stage 4 makes
   them emerge from the CD interpreter, the emergent length must match to the frame or the whole pacing ledger
   (60% of the fight is frozen spans, [loop] §G) drifts, and the victory-frame fnv moves for a WRONG reason.
   **Mitigate:** the CD interpreter's emergent span is the acceptance test ([interp] P2.C fixture: reproduce
   125/157/224/311 exactly + `1E`-super on the baked strike frame) — land Stages 1-3 first (poses byte-exact
   under the CURRENT pinned spans), then Stage 4 only swaps the span SOURCE, re-pinning `field-battleloop-gato`
   solely if the emergent length legitimately differs (it should not).

---

# ADDENDUM 1 (2026-07-15) — C1 RESOLVED: the E3 0x78-byte record layout (`[48 cell-words][12 dx,dy]`)

**Status: RESOLVED by disasm + ROM dump + both-lane cross-validation** (the STEP-0 deliverable of the Stage-1
port). §A.5's C1 asked whether `C1/1C4A` reads the E3 record as signed (dx,dy) pairs (builder) or 16-bit
`srcTile|flip` cell-words (tiles). **Answer: BOTH — the 0x78 record is a single STRUCTURED record with the two
sub-blocks CONSECUTIVE, cell-words FIRST.** O1 and O4/C3's base ambiguity are closed by this.

### The layout (byte-exact, base = `$1C78[pidx]` = GATO `$E30000`, stride `0x78`)

```
 record N  =  base + N × 0x78 :
   +0x00 .. +0x60   (96 bytes = 48 × 16-bit words)   CELL-WORD sub-block  (tile GATHER list)
                        word = srcTile[bits0-10] | Hflip[bit14] ;  word 0x0000 = blank/skip
                        48 words in RASTER order → the 12 OBJ cells' 48 quadrant 8×8 tiles:
                        i 0-7  →VRAM 0x68-0x6F   i 8-15 →0x78-0x7F   i16-23→0x80-0x87
                        i24-31 →0x90-0x97        i32-39 →0x88-0x8F   i40-47 →0x98-0x9F
   +0x60 .. +0x78   (24 bytes = 12 signed (dx,dy) pairs)   POSITION sub-block  (OAM cell offsets)
                        pair k → OBJ cell tile-order[k] at (liveX+dx, liveY+dy)
                        GATO tile order = 0x68 6A 6C 6E 80 82 84 86 88 8A 8C 8E
```

### The disasm proof (the `$BA` threading §A.5 flagged unreconciled)

`C1/1C4A`: `$BA = $1C78[pidx] + $967B×stride` is set ONCE (11C90) at the record base. The **cell-word pass runs
FIRST** (11CF6–11F31): each iteration reads `[$BA]` (11CF8), stores a 32-byte tile (raw `11D5A`, or bit-reversed
via `C0/FD00` at `11DD3` when bit14 set), then **`INC $BA / INC $BA`** (11F23) — advancing `$BA` +2 per word for
`CCF3C2[$AF] × CCF3C3[$AF]` words. For GATO: `$AF = $AD×$AE = 2×0x0E = 0x1C` (helper `C1/0089`), and
`CCF3C2[0x1C]=0x08` (inner) × `CCF3C3[0x1C]=0x06` (outer) = **48 words** → `$BA` advances exactly **0x60**. The
**position pass** (11F3D–11F78) then reads `[$BA],Y` for `Y=0..CCF4FC[type=2]=0x18` — i.e. from the *advanced*
`$BA`, at record **+0x60**. So the position bytes physically follow the 48 cell-words.

### Why the two dossiers disagreed on the base (C1/O4 base ambiguity, closed)

[tiles] read the record at its true base `$1C78[6] = $E30000` and correctly reported cell-words at +0x00.
[builder] byte-searched the 24 live position bytes, found them at `$E302B8`, and sampled `$00BA = E302B8`
(the value AFTER the cell-word pass had already advanced `$BA` by +0x60). It back-factored that as
"base `E3/0060` + 5×0x78", folding the +0x60 position offset into a phantom base — numerically self-consistent
(`E30060 + 5×0x78 == E30000 + 5×0x78 + 0x60 == E302B8`) but mislabeling the base. **True base = `$E30000`;
positions are at +0x60, not +0x00.** The builder's assembly INDICES (idx4/idx5 etc.) are unchanged and correct —
only the base/offset attribution moves.

### Cross-validation (both lanes, byte-exact, this session)

- **Positions (+0x60)**: idx4 → `068@(47,10) 06A@(47,25) … 08E@(16,0)` = [builder]§8 idle-rest EXACT;
  idx5 → `068@(31,48) 06A@(15,48) 084@(47,11) 086@(47,26)` = [builder]§8 idle-swap EXACT; **idx6/idx8 =
  walk-LEFT 12/12**, **idx9/idx11 = walk-RIGHT 12/12** vs [corpus]§3.1 (idx7/idx10 = the ±1px leg-shuffle
  mid-frames). Whole-table split clean: 0/41 position blocks out of the |d|≤96 pose range.
- **Cell-words (+0x00)**: idx0x0D → i0=`4107`(src107 flip), i1=`4106`(src106 flip), i2=`410B`(src10B flip),
  i4=`0029`(src029 raw), i8=`4109`(src109 flip) = [tiles]§6C **5/5** (the dossier's "cell2 src 0x10B" is exact
  under the true base `$E30000`; the earlier "0x10D" was a wrong-base artifact). Raster: i6→0x6E blank,
  i7→0x6F, i8-10→0x78/79/7A, i11→0x7B blank — all match [tiles]§3.

### Port consequence (interim, sanctioned)

The port bakes ONE E3 table per GATO pose index carrying BOTH sub-blocks (`animeng_e3_gato.h`): the 12 (dx,dy)
pairs (compose/blit positions) + the 48 cell-words (sheet gather → per-pose 16×16 tile buffer). This SUPERSEDES
§C's "bake E3 as TWO parallel tables" interim — the true single-record split is now derived, so one table with
two decoded sub-blocks is exact, not an approximation. Idle = idx4 (rest) ↔ idx5 (swap); walk-LEFT = idx6/7/8;
walk-RIGHT = idx9/10/11 (leg cycle = the cell-word/tile differences; OAM ±1px). The `$97D9→$967B` remap (O4/C3)
and the cadence multiplier (O7) remain the timing OPEN — Stage 1 drives the tick with the oracle-validated
dwells (idle 87f/33f) as their sanctioned interim, exactly as the sheet (C5) is an interim fixture.
