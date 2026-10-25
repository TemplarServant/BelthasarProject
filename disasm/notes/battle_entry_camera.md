# BATTLE-ENTRY CAMERA + PC/enemy anchor + walk-end handoff (GATO scene 6 · FOREST scene 19)

Derives the two user reports about battle-entry POSITIONING, ROM+oracle only, zero invention:

1. *"when starting the battle with the hetake, they 'teleport' far to the right … the camera should
   move"* — the forest enemies land off from where they should; the camera should move.
2. *"when crono talks to GATO … he moves automatically … when the battle starts, crono visually
   'teleports' a few pixels away."* — the pre-battle auto-walk endpoint vs the
   battle-stance sprite differ by a few px.

**Verdict up front.** (1) The camera DOES move before a battle, but it is **NOT** a battle-module pan — it is a
**scene-scripted op-0xE7 camera SEEK** that runs during the pre-battle event and settles the field camera
(`$1D0A/$1D0E`) into the battle framing; the battle then stages every combatant *relative to that settled
camera*. Our engine's op-0xE7 snaps `$1D0A/$1D0E` in the VM correctly, but our **field RENDER camera** (the
stateful `camX` / `camy=py−112`, `field.c`) is a SEPARATE player-follow model that never executes the reframe,
so at battle time the field background and the arena-relative combatants use two different cameras → the
"teleport". (2) The PC anchor is `16·rel + fine + extent` (same law as the enemies, `battle_stance.md`
ADDENDUM 8), verified fresh against OAM in BOTH fights; the FOOT (`16·rel+fine`) is camera-correct because the
op-0xE7 pan is folded into `rel` at staging. The remaining Crono handoff residue is **1 px** (field-sim mover
endpoint); the FOLLOWER (Marle) is ~39 px off (the op-0xD9 party-move follower degeneracy).

Method: `ct_probe.exe` (oracle copy) `--monitor … --diff --writer` / `--peek` / `--read`; `tools/dis.py`
(fileoffset = (bank−0xC0)<<16 | addr; camera/event DP context below); OAM decode `t1detail.py` off the
`$7E0700` shadow in the `.wram`. States: repo `gato_press_action.state` (A-boot ≈ f222),
`saves/battle_re/forest_holdleft_pre.state` (hold LEFT 0x40 → D8 f168), `saves/battle_re/forest_battle_3hetake.state`
(in-battle). **⚠ monitor byte-order:** `--monitor "ADDR:2"` prints the two bytes in ADDRESS order (byte[ADDR]
first), so `$1D0E=4400` means byte[$1D0E]=0x44 (=camY 68), byte[$1D0F]=0x00 — NOT little-endian 0x4400.

**⚠ DP context (load-bearing).** The event VM runs D=$0100 (`$17`→`$0117`). The **field camera code runs
D=$1D00**, so a disasm `DEC $0A` / `STA $0E` / `LDA $02` in `C0/91AC`/`C0/944B` is really `$1D0A`/`$1D0E`/`$1D02`.
i.e. **`$1D0A` = field-camera scroll X, `$1D0E` = scroll Y, both in 8-px units** (`camPx = $1D0A·8`, `oTX =
$1D0A>>1` tiles). Proven: writer `C0:94C1` (a `STA $0E`-family store) retires into the `$1D0E` region every
seek step (traces below). This is the SAME camera the memory `ct-field-camera-scroll-model` calls "`$0A/$0E`".

---

## PART A — THE CAMERA MECHANISM: a scene-scripted op-0xE7 SEEK, then the battle stages from the settled origin

### A.1 There is NO battle-module camera pan; the battle READS the settled field camera as the arena origin

The battle-entry chain (`battle_entry.md §3`) never moves the camera. It READS `$1D0A/$1D0E`:
- `C0/0617` enemy-list builder: `LDA $1D0A / LSR / STA $DB` (=oTX), `LDA $1D0E / LSR / STA $DD` (=oTY);
  the eligible-enemy test is `$1801,X − oTX ∈ 1..14`, `$1881,X − oTY ∈ 1..13` (on-screen box).
- `C0/038F→C0/039B` map-block stager: `LDA $1D0A / LSR / AND $1D1E / STA $DB` … then an `MVN $7E,$7E` copy of
  the tilemap region around `($DB,$DD)` into the battle map buffer `$7E:7000+`. So the battle **bakes a copy of
  the field map framed at the arena origin** — the in-battle "field background" is drawn at `$1D0A/$1D0E`, not
  at any player-follow camera.
- `C0/0430` PC stage + `C0/04ED/0557` enemy stage: `rel = objTile − oTX/oTY`, `fine = onScreenPixel & 0x0F`.

So `16·rel+fine = objTile·16 − oTX·16 + (screenPx&15) = objWorld − camPx` — every combatant's staged screen
position is **relative to the settled camera `$1D0A/$1D0E`**. Confirmed post-D8 both fights: `$1D0A/$1D0E` hold
the settled value from the D8 frame until the battle module boots (~f235/f288) and REPURPOSES the bytes
(writer `CC:E308` → 0/0x788F/0x008F, `battle_stance.md` A8.2) — never re-panned by the module.

Intro-style camera variants ARE possible (`$0145`-indexed switch `C0/17A0-1888`, DP-$1D00 zoom/pan) but both
fights run **style 0** (`$0145`=00 the whole entry, monitored) → no intro camera effect.

### A.2 The pan = op-0xE7 (C0/3E30), a blocking SEEK to a per-encounter scripted target

`dis.py 3E30`, DP=$1D00:
```
3E30 LDA $1D00 / BEQ 3E41      ; $1D00==0 -> ARM the seek (3E41); else it's in progress
3E41 …  STA $1D02  (op1<<1)    ; target camX  (8px units)  [store retires C0:3E4B]
     …  STA $1D03  (op2<<1)    ; target camY  (8px units)  [store retires C0:3E54]
     INC $1D00 (3E54)          ; $1D00 = 1  (seeking)       [retires C0:3E57]  + STZ $1F
3E35 DEC A / BEQ 3E59          ; re-entry while $1D00==1: CLC RTS  = OP BLOCKS (script waits)
3E38 STZ $1D00 (retires 3E3B)  ; re-entry when seek set $1D00==2: clear + SEC RTS (advance script)
```
The seek itself, `C0/91AC` (DP=$1D00, `dis.py 91AC`):
```
91AC LDA $1D00 / BEQ 91F8              ; no seek -> normal camera follow
91B0 LDA $1D02 / CMP $1D0A             ; target camX vs live camX
     BCC -> $2E=#F0 (−16) else $2E=#10 ; scroll-request X  (±16 into the commit accumulator)
     (equal + $1D0C sub-aligned) -> TSB $1D01 bit0   ; X arrived
91CE LDA $1D03 / CMP $1D0E             ; target camY vs live camY
     BCC -> $30=#F0 else $30=#10       ; scroll-request Y
     (equal + $1D0E sub-aligned) -> TSB $1D01 bit1   ; Y arrived
91EC LDA $1D01 / CMP #03 / BEQ         ; both axes arrived?
     LDA #02 / STA $1D00 (91F4→91F6)   ; -> seek COMPLETE ($1D00=2); STZ $1D01
91F8 …                                 ; normal camera commit C0/944B: $2E/$30 -> move $1D0A/$1D0E ~1px/frame
```
So op-0xE7 = **"pan the field camera to the scripted target `($1D02,$1D03)` and block the event script until it
gets there."** The target is a **level-designer constant per encounter** (in the event body, in 16-px tiles,
`×2` → 8-px units); it does NOT depend on the runtime enemy formation.

### A.3 Oracle traces (both scenarios) — writer PCs + values

**GATO** `--monitor "1D00:1,1D02:2,1D0A:2,1D0E:2,0117:2" --from gato_press_action.state --input "100:8,0:290" --diff --writer`:
```
f0    $1D0A=0(camX0)   $1D0E=03 (camY3=24px)   $1D00=0
f182  $1D00=1 (C0:3E57)  target $1D02=00,$1D03=04 (C0:3E54)   ; op-0xE7 'E7 00 02' ARMS: targetY=4 (32px)
f185  $1D0E 03->04 (C0:94C1)                                  ; camera Y seeks 3->4 (ONE 8px step)
f186  $1D00 1->0 (C0:3E3B)                                    ; arrived; op advances
f218  $0117=2100 (C0:3AA8)  = D8 fires (bit5 intro + bit0)
f221  staged: $29B0 seat0 = 08 09 06 00 03   ARENA ORIGIN $1D0A=0 (camPx 0), $1D0E=04 (camPy 32)
f288  $000A=3804 (C1:CB3B) ; battle module takes over
```
**FOREST** `--monitor "…" --from forest_holdleft_pre.state --input "40:24,0:280" --diff --writer`:
```
f0    $1D0A=03 (camX3=24px)  $1D0E=3C (camY60=480px)   $1D00=0
f2-60 $1D0A 3->2->1->0 (C0:949F/9491)                  ; player HOLDS LEFT: camera follows to the LEFT bound 0
f52   $1D00=1 (C0:3E57)  target $1D02=00,$1D03=44 (C0:3E54)  ; op-0xE7 'E7 00 22' ARMS: targetY=0x44=68 (544px)
f60-116 $1D0E 3C->3D->…->44 (C0:94C1/94CF)             ; camera Y SEEKS 60->68 (+8 units=64px), ~1 unit/8f (~1px/f)
f117  $1D00 1->2 (C0:91F6)                              ; both axes arrived
f120  $1D00 2->0 (C0:3E3B)                              ; op advances
f168  $0117=2100 (C0:3AA8)  = D8 fires
f171  staged: $29B0 seat0 = 0A 04 00 07 00   ARENA ORIGIN $1D0A=0 (camPx 0), $1D0E=44 (camPy 544)
f235  $000A=1C30 (C1:CB3B)  ; battle module takes over
```

**Peek proof of the pan** (`--peek forest_holdleft_pre.state … --btnscript`, `fb2png`): `for_pre` (`0:2`) = party
centred-low in the clearing; `for_seek` (`40:24,0:120`) = camera panned DOWN, party framed higher; `forbat`
(the in-battle state) = the settled battle — Crono upper-right (~143,40), Marle centre (~107,96), the three
mushroom Hetakes framed, HUD at the bottom. The pan visibly reframes the clearing for the fight.

### A.4 THE RULE (both scenarios, one law)

> The battle draws the field background AND every combatant under **one camera = the arena origin
> `$1D0A/$1D0E`**, which is **the field camera position at the D8 frame, settled by a pre-battle
> scene-scripted op-0xE7 SEEK to a per-encounter designer target**. The seek pans ~1 px/frame and blocks the
> event script until it arrives; then op-0xD9 walks the party to battle spots and op-0xD8 stages. Combatant
> staged `rel/fine` are relative to this origin (`16·rel+fine = objWorld − camPx`).

- **GATO (scene 6, `E7 00 02`):** targetY=4 → pan is a **single 8-px step** (camY 3→4). The GATO arena is
  already framed, so the pan is ~nil — which is why a static-camera engine looks acceptable for GATO.
- **FOREST (scene 19, `E7 00 22`):** targetY=68 → a **64-px downward reframe** (camY 60→68), PLUS the X already
  driven to the left bound (camX 3→0) by the held-LEFT walk. A large, visible pan.

### A.5 Why our engine mis-places the forest enemies ("far to the right")

Our op-0xE7 (`ct_vm.c:1398-1410`) SNAPS the VM camera `$1D0A=$1D02`, `$1D0E=$1D03` (both axes — correct target),
and our staging (`field.c:2727-2734`) reads `oTX=$1D0A>>1`, `camPx=$1D0A·8` from it → so the **staged records +
computed anchors are arena-correct**. BUT the **field RENDER camera is a different variable**: `field.c:5146-5158`
maintains a stateful player-follow `camX` (seeded `px−128`, clamped `mx0..mx1`) and `camy = (int)py − 112`, and
this code is **NOT gated on `!battlePhase`** — it keeps running during battle (the VM is frozen, so `camX`/`py`
just hold their pre-battle player-follow values). The combatants are then drawn at **absolute arena-relative
screen coords** (`battleStanceRender`/`battleHetakeRender`, e.g. `field.c:2196 cells[c].x = anchorX+…`, no
`camx` subtraction). So:

> field background @ **player-follow camera** (`camX`, `py−112`)   vs   combatants @ **arena origin** (`$1D0A·8`,
> `$1D0E·8`).  When the two differ — which is exactly what the op-0xE7 reframe creates — the combatants slide
> off the terrain. For the forest the op-0xE7 pulls the camera to the LEFT bound (camX 0) + DOWN 64 px, while
> our player-follow camera sits lead-centred (camX ≳ 0, camy = py−112 ≠ 544) → the enemies (anchored to the
> left-down origin) render down-and-right of where the terrain puts them = the user's *"to the right"*.

(Exact interactive px is our-engine-specific — the `CT_BATTLESHOT` hook composites combatants over a BLACK
buffer, `field.c:2664`, so it can't capture the field+combatant misalignment headlessly; flagged OPEN. The
STRUCTURAL cause — render camera ≠ arena origin — is proven from the code + the oracle origins above.)

---

## PART B — PC ANCHOR = 16·rel + fine + extent (verified fresh vs OAM, both fights)

Staged PC records `$29B0 + 5·seat` captured at the WRITE frame (`--monitor "29B0:15" … --diff --writer`,
writer `C0:049C`), OAM min-corners decoded fresh from the `$7E0700` shadow (`t1detail.py` on the settled peek):

```
fight   PC     staged rel  fine    foot=16·rel+fine   OAM min-corner   extent = corner − foot
GATO    Crono  (8, 9)     (6, 0)   (134,144)          (109,108)        (−25,−36)
GATO    Marle  (11,6)     (0,15)   (176,111)          (164, 63)        (−12,−48)
FOREST  Crono  (10,4)     (0, 7)   (160, 71)          (143, 40)        (−17,−31)
FOREST  Marle  (7, 9)     (15,0)   (127,144)          (107, 96)        (−20,−48)
```
(Staged bytes: GATO `08 09 06 00 03 | 0B 06 00 0F 02`; FOREST `0A 04 00 07 00 | 07 09 0F 00 00` — byte-exact vs
`battle_stance.md` A8.1. OAM corners re-derived fresh: GATO Crono(109,108)/Marle(164,63); FOREST
Crono(143,40)/Marle(107,96) — all confirm A8.1.)

**B.1 The FOOT is camera-correct in BOTH fights — because the pan is folded into `rel`.** e.g. FOREST Crono:
`oTY = $1D0E>>1 = 68>>1 = 34`, so his world tile Y = `relY+oTY = 4+34 = 38`; `foot = 16·rel+fine = (160,71) =
objWorld − camPx = (160, 608−544+7)`. The camPy=544 (the op-0xE7 pan) is baked into `relY=4` (tile 38 − origin
34). Had the camera NOT panned (e.g. `camPy=py−112`), `oTY` and thus `relY` would be different → the foot
lands somewhere else. **So the camera pan is exactly what makes the computed PC foot correct**; the baked
absolute anchors (`ctBattleStanceActor[]` Crono(109,108)/Marle(164,63)) hard-code the GATO post-pan framing and
are wrong for any other fight (forest Crono should be (143,40), Marle (107,96)).

**B.2 `extent` is POSE-dependent (not a fixed per-sprite constant like the enemies').** Crono's extent is
(−25,−36) in GATO but (−17,−31) in the forest, because the two OAM clusters are DIFFERENT POSES: GATO Crono is
the spread sword-stance (`00E@(16,0) 00A@(0,7) 008@(16,13) 00C@(17,28)`); the forest-state Crono is a compact
2×2 (`008@(16,0) 00A@(0,0) 00C@(16,16) 00E@(0,16)`). Marle's Y-extent is −48 in BOTH (her cluster reaches the
same distance up from the foot); her X-extent differs (−12 vs −20) with her pose. The **FOOT (position) is
pose-independent and always correct**; only the min-corner (art extent) tracks the pose.

**Consequence for the spec:** compute the PC anchor like the enemies — `anchor = 16·rel + fine + extent[pose]`,
`rel/fine` LIVE from `$29B0+5·seat`. Our engine renders the GATO sword-stance assembly in every fight, so use
its extent (Crono −25,−36 / Marle −12,−48): this lands the PC at the correct FOOT in any framing; a byte-exact
non-GATO PC *pose* (the forest compact stance) is a separate stance-bake item (`battle_stance.md` A8.4 OPEN).

---

## PART C — the field→battle WALK-END handoff (the GATO "teleports a few pixels")

The pre-battle event op-0xD9 (`D9 08 0B 8B 08 85 88`, handler `C0/326C`) **walks each party member to a distinct
battle spot** — `dis.py 32C8`: for the lead (`LDX $97`) it writes a move-script `$1180,X = #171F`, zeros
velocity `$1A01,X`/cadence `$1001,X`, then consumes the per-PC target operands (`330F+`). It is a scripted WALK,
not a snap. The oracle endpoints (from the staged records above): Crono → tile (8,11) fine (6,0); Marle → tile
(11,8) fine (0,15) — two distinct spots (lower-left + upper-right, the GATO formation).

**Our engine vs the oracle** (`CT_BATTLE_STAGEDBG=1 CT_BATTLE_DEBUGEND=60 Belthasar --battletest
saves/battle_re/gato_t0.raw.wram`):
```
seat   OURS (rel fine)   → screen    ORACLE (rel fine)  → screen    delta
Crono  (8,8) (6,15)        (134,143)  (8,9) (6,0)          (134,144)  1 px  (Y)
Marle  (8,8) (9,15)        (137,143)  (11,6)(0,15)         (176,111)  ~39 px X, 32 px Y
```
- **Crono (lead) = 1 px in Y** (screen 143 vs 144). The staging already reads the maintained on-screen coord
  (`field.c:2769-2774`, `CtActorScreenCoords`, the T3.4-option-(i) fix), so the residue is NOT the staging
  formula — it is the **field-sim mover endpoint**: our lead's walk-end world Y is 143 vs the oracle's 144 (the
  op-0xD9 walk ends 1 px short). `relY 8 + fineY 15 = 143` vs `relY 9 + fineY 0 = 144` is just the tile-boundary
  representation of that 1 px. This is `battle_loop.md` ADDENDUM-T3 option (ii), the deeper field-sim pin.
- **Marle (follower) = ~39 px** — staged at (8,8), essentially ON TOP of Crono (8,9), NOT at her own spot
  (11,8). The **op-0xD9 party-move follower is degenerate** in our engine: the conga follower lags ~1 tile
  behind the lead and never walks out to the distinct battle spot (the T3.4/`battle_loop.md` staging addendum
  "degenerate follower staging"). This is the LARGER of the two handoff errors, though the user's report names
  only the lead.

**Note the camera link (Part A):** because the battle stance draws Crono at `foot+extent` (arena-relative) while
the field sprite that just finished walking is drawn at the player-follow camera, ANY camera mismatch (Part A.5)
ADDS to this handoff delta. For GATO the op-0xE7 pan is ~nil so the visible teleport is dominated by the 1-px
mover residue; a fight with a real pan (forest) would also carry the camera component.

---

## PART D — IMPLEMENTATION SPEC (what our battle consumer must do)

### D.1 CAMERA (scenario-general) — set the field render camera to the arena origin during battle

The functional requirement: during battle the field background must be composited at the SAME camera the
combatants are anchored against.
```
at battle entry (D8 staged), for the whole battlePhase:
    camx = $1D0A · 8            // = camPx, the staging arena origin X  (field.c already reads this as camPx)
    camy = $1D0E · 8            // = camPy, the staging arena origin Y
```
- These are the values our op-0xE7 already snapped into `$1D0A/$1D0E` (`ct_vm.c:1408-1409`) and our staging
  already reads (`field.c:2733-2734`) — the fix is to make the RENDER path (`field.c:5158 camx/camy`) use them
  during `battlePhase` instead of the player-follow `camX`/`py−112`. Then combatants (drawn at `16·rel+fine+
  extent`) and the field terrain share one camera → the SAME anchor math lands correctly in BOTH scenarios.
- **Snap vs pan.** The oracle SEEKS ~1 px/frame during the pre-battle op-0xE7 (cosmetic glide) and is settled
  before D8. For our engine the minimum is to **SNAP** camx/camy to the arena origin at battle entry (correct
  final placement). A cosmetic pre-battle pan is optional fidelity: drive the render `camX/camY` toward the
  op-0xE7 target `$1D02·8/$1D03·8` at ~1 px/frame while the op blocks (mirrors `C0/91AC`); it changes nothing
  about the final battle framing.
- **Target source = the arena origin, not a runtime formation-center computation.** The camera position is the
  scripted op-0xE7 result already in `$1D0A/$1D0E`. Do NOT invent a "center on the formation" rule — the ROM
  frames via the designer's per-encounter op-0xE7 constant.
- **Scenario-general:** valid for GATO (origin (0,32), pan ~nil) and forest (origin (0,544), 64-px pan) and any
  fight, because it just consumes the staged origin.

### D.2 PC ANCHOR (scenario-general position; pose bake still per-capture)

Replace the baked absolute `ctBattleStanceActor[]` anchors with the computed law (same as the enemies, A8.3):
```
footX = 16·relX + fineX          // rel,fine LIVE from $29B0 + 5·seat  (relX,relY,fineX,fineY,prio)
footY = 16·relY + fineY
anchorX = footX + extentX(PC)    // extent = the rendered stance assembly's min-corner offset from the foot
anchorY = footY + extentY(PC)    //   Crono (−25,−36), Marle (−12,−48)  [GATO sword-stance bake, our render]
```
- POSITION (foot) is then correct in any framing/scenario. The extent is a per-POSE constant of whatever stance
  assembly we render (we ship the GATO sword-stance, so its extent). A byte-exact non-GATO PC pose is a separate
  stance-bake (OPEN). Cleaner alternative (A8.3): anchor the assembly at the FOOT and bake the cells
  foot-relative — one fewer constant.
- This is the SAME change already applied to the enemies (`battleEnemyAnchorDelta`, `field.c:1814`); extend it
  to the PC seats.

### D.3 FIELD-SIM WALK-END (the handoff residues)

- **Lead 1-px Y (Crono):** the staging formula is already correct (maintained coords). The residue is the field
  mover ending the op-0xD9 walk 1 px short in world Y (143 vs 144). Fix belongs in the field mover/op-0xD9
  endpoint (make the walk land byte-exact) — NOT in the battle anchor. Cosmetically ≤1 px; low priority.
- **Follower ~39 px (Marle):** the op-0xD9 party-move must walk EACH member to its own battle spot (lead (8,11),
  Marle (11,8) for GATO). Our conga follower stays ~1 tile behind the lead and is staged there. Fix belongs in
  the op-0xD9 handler / follower stepping so followers reach their distinct spots before staging. This is the
  larger handoff error and is scenario-general (every 2+-PC fight).

**Combined effect:** D.1 (camera=origin) makes the field↔combatant compositing pixel-consistent; D.2 puts each
PC at the correct FOOT; D.3 removes the last field-sim endpoint deltas. Together the field→battle sprite
handoff is pixel-continuous (modulo the pose-bake open item) in BOTH fights.

---

## PART E — OPEN / boundaries

- **Interactive render-camera delta (magnitude/direction):** the `CT_BATTLESHOT` hook composites combatants over
  black (`field.c:2664`), so the field+combatant misalignment can't be captured headlessly; the exact "to the right"
  px in real play is our-engine-specific (depends on our clamped `camX`/`py` at the trigger). The MECHANISM
  (render camera ≠ arena origin) and the oracle origins (GATO (0,32), forest (0,544)) are pinned; an interactive
  capture (or a battle-composite-over-field debug hook) would quantify it.
- **op-0xE7 target provenance in the event body:** proven via the writer PCs (`C0:3E54/3E57`) that op-0xE7 fires
  the forest reframe (target camY=68) and the GATO reframe (camY=4); the exact object/IP that hosts each op in
  the scene body was not located (not needed — the handler + oracle values are ground truth). The `×2` operand
  scaling and the target axes (`$1D02`=camX, `$1D03`=camY) are confirmed by the seek handler + both fights.
- **PC stance pose per fight (extent):** Crono/Marle extents are pose-dependent (B.2). Only the GATO sword-stance
  is byte-verified; the forest compact-pose PC bake is the standing `battle_stance.md` A8.4 open item. The
  POSITION (foot) fix (D.2) is pose-independent.
- **Camera seek RATE / sub-position:** the ~1 px/frame pan and the `$1D0C`/`$1D0E`-sub commit (`C0/944B`) are
  observed (1 unit/~8 f) but the exact sub-pixel accumulator law is the unported field-camera milestone
  (`ct_vm.c:1400` calls it out); only the SETTLED origin matters for placement, so it is not load-bearing for
  the spec.
- **Lead 1-px mover endpoint + follower party-move:** field-sim pins (D.3), deferred to the field mover/op-0xD9
  work; the battle side is correct once it reads the maintained coords + reaches the spots.
- **Intro styles 1-4 (`$0145`≠0):** both fights are style 0; the DP-$1D00 zoom/pan intro variants (`C0/17A0-1888`)
  are un-exercised here (a different encounter would be needed to see if any battle uses a module-side camera
  effect on top of the field origin).

## Repro / artifacts (scratchpad, NOT committed)
- `ct_probe.exe` (oracle copy), `ct_field_probe.exe` (+SDL2.dll) — engine copies.
- Screenshots: `for_pre.png` / `for_seek.png` / `forbat.png` (forest pan + settled battle), `gatobat.png`.
- OAM decode: `t1detail.py <peek>.wram <label>`; anchor proof `t1proof.py`.
- Camera trace commands are inline in A.3 (GATO/forest `--monitor … --diff --writer`).
