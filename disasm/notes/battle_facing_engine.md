# BATTLE FACING ENGINE — per-combatant facing ($96DB[cmb]) + per-facing E4 windows: PC stances, PC attack, GENERAL enemy idle

Derives, from the ROM disasm + oracle only, the mechanisms behind FOUR user reports (2026-07-22):

1. *"Crono faces up the whole time (and Marle to the left), inherited from the GATO battle"* —
   CONFIRMED as inheritance: the baked stances ARE the GATO-fight facings (bat1 oracle: Crono $96DB[0]=0 UP,
   Marle $96DB[1]=2 LEFT). The real per-battle facing is staged by a nearest-enemy bearing law (§2).
2. *"when attacking, Crono teleports and slides facing the wrong direction"* — the attack's approach/strike/return
   poses are per-facing E4 WINDOWS; our engine plays the baked facing-0 instances regardless of facing (§4).
3. *"the enemies become invisible"* (bat2 post-intro, bat4) — our enemy render is per-ID (GATO/Hetake only);
   every other id has NO renderer. The general ROM chain (id → E4:F600 → windows/E3/sheet/palette) renders
   ANY enemy (§5).
4. *"the enemies turn into GATO"* — `battleGatoRender` gates only on slot-3 aliveness ($AF05), never on id 0x92:
   any enemy staged in slot 3 is drawn AS GATO (field.c:2616).

Method: bat1..9 lake states (`saves/battle_re/bat*_lake.state`, battle_states_catalog.md recipe), oracle
`--monitor --diff --writer` runs, `tools/dis.py` (flat = (bank−0xC0)<<16|addr). Button A=0x100.

---

## 1. THE PER-COMBATANT ARRAYS ARE cmb-SPACE, PC-ALIASED

`$96DB + cmb` = FACING (cmb 0-2 = PC seats; enemies at cmb = slot+3, i.e. the known `$96DE+slot` alias).
`$9747 + cmb` = MODE (enemy alias `$974A+slot`). `$1C99/$1CBA + cmb*3` = pose-script ptrA/ptrB (enemy alias
`$1CA2/$1CC3 + slot*3`). Same +3 alias family as `$1C78`/`$966E` (battle_pose_staging.md §4).

Facing decode (C0/F700 output, same for PCs and enemies): **0=UP, 1=DOWN, 2=LEFT, 3=RIGHT.**

Read from the user's states (WRAM off 0x45 in the lake container):

| state | $96DB PC facings | visual (oracle png) |
|---|---|---|
| bat1 (GATO) | Crono **0 UP**, Marle **2 LEFT** | the baked stances — the "inheritance" |
| bat2 (forest ambush) | Crono 3, Marle 2 | |
| bat3 (3×Hetake) | Crono **2 LEFT**, Marle **3 RIGHT** | bat3.png zoom: side stance ✓ |
| bat4 (3×Imp solo) | Crono 3 | bat4.png: right stance ✓ |
| bat8 (Cathedral) | all 0 UP | matches animeng_e4_pc.h "facing0" note |
| bat9 (3-PC) | Crono 2, Lucca 2, Frog 3 | Lucca "ALT stance 0x39" = her f2 idle (§3) |

## 2. THE PC INITIAL-FACING LAW (writer C1:3548, staged once at battle entry)

Monitor (jogo_trava_lake, D8 f133): `$96DB[0..2]` written at f225 by **C1:3548**. Disasm C1/350F-354C:

```
C1/350F  LDA #02 / STA $986E          ; helper sub-select 2 = "nearest ENEMY" (table C1/2D81 → C1/2A43)
C1/3517  $986F = cmb (loop 2..0)      ; self
C1/351C  LDA #05 / JSR C1/0045        ; helper 5 (C1/0051[5] → C1/2986): $9873 = argmin over cand 3..0x0A
                                      ;   of |dx|²+|dy|² (C1/2B37; squares via C1/0089), candidates gated
                                      ;   $96F5,X!=0 (present) ∧ $9FF7,X bit7=0 ∧ block+0x1D bit7=0 (alive;
                                      ;   block ptr = CC/F8ED[cand*2])
C1/3523  $D3/$D4 = self $1D0C/$1D23 ; $D5/$D6 = target $1D0C/$1D23
C1/353B  JSR C1/0222                  ; bearing (0x00=+X, 0x40=+Y, 0x80=−X, 0xC0=−Y)
C1/353F  LDA $C0F700,X                ; facing = ctBattleFaceTab[bearing] — the SAME enemy face tab
C1/3545  STA $96DB,X (X=cmb)          ; store
```

**Validated 11/11** PC facings across bat1/2/3/4/8/9 (predicted == read $96DB, incl. bat9's 3 PCs).
Enemies do NOT get this entry computation (initial 0; their facing is stamped by the mover aim C1/37E9→37F2
and by attack scripts C1:600C — bat9's Diablos 2/3/3 are mid-battle latches).

## 3. THE PER-FACING E4 WINDOW SYSTEM (PC pose staging CC/E97E; idle window +0x0C)

PC pose staging (CC/E97E, PC pass) keys the SAME E4 tables by **charId** (0..6):

```
CE990  $1C57[seat] ← E4:2000[charId*3]   ; PC SHEET (RAW 4bpp in ROM — §3.1)
CE9A5  $1C78[seat] ← E4:2300[charId*3]   ; PC E3 assembly base
CE9BC  $1C99[seat] ← E4:2600[charId*2]   ; ptrA (bank E4)   CE9D3  $1CBA[seat] ← E4:2800[charId*2] ; ptrB
CEA0A  $966E[seat] = (ptrA[charId+1] − ptrA[charId]) >> 2   ; the FACING-ROW STRIDE
```

| charId | sheet | E3 base | ptrA | stride |
|---|---|---|---|---|
| Crono 0 | $D272C0 | $DC27B0 | E4/3000 | 0x128 |
| Marle 1 | $D40000 | $DC9AD8 | E4/34A0 | 0xF8 |
| Lucca 2 | $D36B40 | $DC4E70 | E4/3880 | 0x11C |
| Robo 3 | $D47C40 | $DCC080 | E4/3CF0 | 0x11C |
| Frog 4 | $D30000 | $DC7490 | E4/4160 | 0x118 |
| Ayla 5 | $D50000 | $DE0000 | E4/45C0 | 0x150 |
| Magus 6 | $D20000 | $DC0000 | E4/4B00 | 0xF4 |

(matches every known live $1C78: Crono $DC27B0, Marle $DC9AD8, Lucca $DC4E70, Frog $DC7490.)

**ptrA layout = 4 facing ROWS of `stride` bytes; window = row + CLASS BASE byte offset** (the tick's
$82 = CC/FB91[mode] class → base {cls0:0x0C, cls1:0x04, cls2:0x18} + facing*stride — battle.c
CtBattleEnemyIdleRest, same machine). CC/FB91[0..15] = `00 01 01 01 02 00 01 01 …`.

**The IDLE window (+0x0C, mode 0/cls 0) per facing** (poses/ptrB-dwells, ROM-dumped):

| char | f0 (up) | f1 (down) | f2 (left) | f3 (right) |
|---|---|---|---|---|
| Crono | C6 4F C7 4F | 43 2F 44 2F | C8 33 C9 33 | CA 37 CB 37 |
| Marle | D3 D4 D5 D4 | D6 D7 D8 D7 | D9 DA DB DA | DC DD DE DD |
| Lucca | E6 31 E7 31 | E8 35 E9 35 | EA 39 EB 39 | EC 3D ED 3D |
| Frog | CE 2F CF 2F | D0 D1 D2 D1 | D3 37 D4 37 | D5 3B D6 3B |

Cross-validated against LIVE poses: bat1 Crono $9907[0]=C7 ∈ f0 ✓, Marle DA ∈ f2 ✓; bat3 Crono C9 ∈ f2 ✓,
Marle DD ∈ f3 ✓; **the "mystery" bakes resolve: Lucca 0x31 = f0, "ALT" 0x39 = f2 (bat9 facing 2!), Frog
0x3B = f3, Marle stance 0xDA = f2, Crono stance/bob C6/C7/4F = f0.** Every retired stance = one facing
instance of this table. Observed idle cadence (bat3 monitor): C8(7f) 33(13f) C9(7f) 33(13f) — the exact
ptrB dwell decode is a residual (raw ptrB idle rows read 05/00/0A pairs; the 7/13 mapping is the O-item).

### 3.1 PC sheets are RAW ROM
`E4:2000[charId]` targets are UNCOMPRESSED 4bpp: the baked sparse sheets == ROM bytes at base + src*0x20,
verified 95/95 (Crono, $D272C0) + 56/56 (Marle, $D40000). The baked E3 records == ROM at base + idx*0x28
(`[16 cw][4 (dx,dy)]`), verified 13/13 Crono (cw + positions).

## 4. THE PC ATTACK IS FACING-WINDOWED TOO (bat3 monitor, Crono physical on Hetake0)

Monitor `8E/9907/967B/96DB/1D0C/1D23/A517` over A→A on bat3_lake (f0..f280):

```
f0-66    facing 2, idle cycle C9/33/C8/33            ; the f2 idle window ✓
f91-107  $A517=0x10 (LEFT track), $8E=02, X 160→128 at −2/f (2 segments, stop = victim+8 ✓ §1.5 law)
         poses 24→3B→25 (~6f each) = the f2 RUN window ptrA+2*stride+0x18 = `24 3B 25 26 3C 27`
f108     $96DB[0] ← 0  (re-aim AT VICTIM from the adjacent spot: (128,71)→(120,63) bearing ≈ up)
f109-146 strike poses 50→51→52 = the FACING-0 STRIKE window `50 51 52 / 53 54`
         (per-facing strike rows: f0 50-54 / f1 55-59 / f2 5A-5E / f3 5F-63 — consecutive in ROM)
f147-149 $96DB[0] ← 3 (re-aim HOME: home is right), poses 8F = f3 return-run, X 128→160 at +4/f
f164-181 settle pose 4B (f3 recover window `4B …`); f180 $96DB[0] ← 2 = re-aim nearest enemy (the §2 law
         re-runs at settle) → idle C8/33/C9 f2 cycle again
```

**Decode of the retired GATO-fight bakes:** ctAnimengCronoAtkByPose {20,41,50,51/52,8B,47} = A1/A2 ∈ the
f0 RUN window `20 41 21 22 42 23`, A3/A4 ∈ the f0 STRIKE window `50 51 52`, A5/A6 ∈ the **f1** recover
window `47 8B 8C 47` (the GATO-fight return aims DOWN=home). I.e. the bake is the facing-0/1 instance of
the general per-facing law; in bat3 the same machine plays f2-run → f0-strike → f3-return → f2-idle.
The facing re-stamps: arrival → faceTab[bearing victim−self]; return → faceTab[bearing home−self];
settle → the §2 nearest-enemy law again. ($8E during the span carries the phase/window select — its
exact PC-tick consumption (C1/2E00) is a residual; the window CONTENT per facing is fully ROM.)

**Correction (2026-10-04): the settle re-aim is the ATTACK SCRIPT'S op 72, not an act-end law.** Every re-aim
above is an op `72 ff` of the action's own script (C1/5FDE -> C1/75CC, `STA $96DB,X` C1/6009; oracle
saves/battle_re/gato_cyclone.state, a plain attack: f498 `$96DB = 00` C1:600C). An action whose script has no op 72
leaves the facing where it ended: the Cyclone's slide arm C1/6299 leaves `01` (down) at f627 (C1:638C), held through
the act end f733 and GATO's next action, until the idle walk's re-aim latch C1/2F22 (C1/2F8C-2F94, `INC $80` -> the
publish at C1/2F05) turns him at f951. The port's generic act-end re-aim (a "SETTLE" in battle.c) is removed: it made
that latch find nothing to change, so the pose was never republished and Crono stayed on the spin's last frame,
facing down, for the rest of the fight (the user's ct_debug_008 report). Pinned:
field-battle-cyclone-holds-its-facing-until-the-idle-reaim.

## 5. THE GENERAL ENEMY RENDER CHAIN (invisible enemies + GATO-swap)

Everything id-keyed, no capture needed (extends battle_pose_staging.md):

- **Record E4:F600[id*10]**: +0 sheet idx → E4:2000 (CT-LZ), +1 packet → E4:2300 = E3 base, **+2 palIdx →
  palette = E4:0000 + palIdx*24 = OBJ colors 1..12** (GATO 0xB6→0x241110 == bat1 CGRAM pal1 colors 1-12,
  byte-exact hit; Beetle/Avian/Imp/Hetake/Roly/Naga/Eaglet/Diablos all matched their in-battle CGRAM),
  +3 animId → ptrA/ptrB, +4 anim-set class.
- **The staged field record $29BF+12k carries the render bindings**: **+8 = TYPE INDEX in the formation =
  the OBJ PALETTE slot** (pal 1+*(typeIdx-1)*... observed typeIdx 1→pal1, 2→pal2 across bat1/2/3/4/6/8/9,
  8/8 states — incl. bat8's Naga typeIdx 2 → pal2 with pal1 = the formation's unused type-1) and
  **+9..+11 = the sheet DEST 24-bit** (GATO `00 80 7F` = $7F8000 ✓ == $1C57[cmb]).
- **Idle window** = ptrA + facing*stride + classbase(CC/FB91[$974A mode]) — validated against staged $97D9:
  bat1 GATO mode 6 (cls1, walking) facing 0 → `00 01 02 01` ✓ (captured mid-walk, pose 01 ✓); bat3 Hetakes
  slots 0/1 mode 0 → +0x0C `01 FF` rest 01 ✓, slot 2 mode 4 (cls2) → +0x18 rest 07 ✓ (the closed-mushroom
  variant!); bat4 Imps mode 1 f0 → `00 01 02 01` dwell `03 02 03 02` ✓.
- **E3 record type** = staged rec **+7 & 7** → $967C[cmb] (staging CEA70-CEA75): 0 = type-0 4-cell
  (stride 0x28, PC layout), 2 = type-2 12-cell (GATO 0x42&7=2, stride 0x78). CC/F505[type] → $1D7F[cmb].
- Facing-3 poses h-flip via cell-word bit14 (e.g. Imp E3[0x0A]); whole-OAM hf=vf=0 as GATO.
- Live foot init: $1D0C/$1D23[cmb] = 16*rel + fine (staging CEA82-CEA8D).

bat4's three Imps stream their OWN VRAM blocks (tiles 040/048/060, distinct content) = per-slot pose
independence; per-slot window cursors.

## 6. IMPLEMENTATION SPEC (structural, replaces per-ID renderers)

1. **Facing state**: host `pcFacing[3]` staged by the §2 law at battle boot + re-stamped at attack
   arrival/return/settle; enemy `mvFacing[slot]` (exists) already stamps at mover aim — keep, initial 0.
2. **PC stance render**: replace the baked-stance/idle-bob dispatch with pose = the per-facing idle window
   (ROM read, §3) cycled at the observed cadence; blit via the EXISTING ctAnimengPcBlit (E3+sheet already
   ROM-verbatim; add ROM-direct record/sheet fallback for indexes outside the sparse bakes).
3. **PC attack render**: approach poses = run window (+0x18) of the track facing; strike = strike window of
   faceTab[victim bearing]; return = run window of home bearing; settle = recover window; then §2 re-aim.
   The mover (battlePcAtkArm/Tick) stays byte-identical.
4. **General enemy idle render**: one `battleEnemyGenRender` looping staged slots: id → PoseStage resolver
   (exists) + LZ sheet (exists) + E3 record read (generalize IdleGather to return cells+positions) + palette
   from E4:0000[palIdx] on the CGRAM slot = staged typeIdx (+8) + window cursor by (mode,facing). GATO (0x92)
   keeps his special animated branches (id-gated NOW — kills the GATO-swap); Hetake's leap/attack bake stays;
   both validate the general idle against their pinned checks.
5. Checks: stance-facing (bat3 renderwram-style), enemy-visible (bat2/bat4), facing-law unit (11/11 table).

## 7. OPEN / RESIDUAL
- ptrB dwell byte semantics (idle 7/13 cadence vs raw 05/0A bytes) — observed cadence used, exact decode open.
- $8E's PC-tick consumption (C1/2E00) + the strike/recover window byte-offsets as a TOC (currently located
  by pose-value match; offsets are stable per char in ROM).
- Enemy palette colors 13-15 (c13=7FFF, c15=14A5 constant; c14 varies per battle — source untraced; enemy
  cells observed use 1-12).
- bat6's Imp+Roly sharing sheet dest $7FA000 (capture quirk?) — untraced.
- The window-cursor FF/loop semantics beyond the 4-entry idle cycles (walk 6-step rows etc).

## Repro
```
# facings + windows (states): scratchpad read_facing2.py / read_pose.py on saves/battle_re/bat*_lake.state
# facing writer: ct_probe --monitor "96DB:15,9747:12" --from saves/jogo_trava_lake.state --frames 400 --diff --writer
# bat3 attack: ct_probe --monitor "8E:2,9907:1,967B:1,96DB:1,1D0C:1,1D23:1,A517:1,9877:1" \
#   --from saves/battle_re/bat3_lake.state --btnscript "0:30,100:2,0:30,100:2,0:280" --diff
# dis: 0x013500 (facing law), 0x012986/0x012A43/0x012B37 (nearest-enemy), 0x0CE97E (PC staging), 0x0CEAD2 (enemy)
```

---

# IMPLEMENTATION ADDENDUM (same session) — §6 SHIPPED

- **battle.c**: `CtBattlePcPoseStage/IdleWin/E3Rec/SheetTile/WinPose` (the charId-keyed E4 walk, CC/E97E),
  `CtBattleEnemyE3Rec/PalRom/IdleWin` (the id-keyed enemy chain), `CtBattleAimFacing` (faceTab∘atan2),
  `battlePcAimFacing` + host `pcFacing[3]` staged in battleMoveInit + re-aimed at a PC attacker's span end
  (§4 f180); host `enPoseMode[8]` = the AI's combat-start initial eval (`battleAiInitialMode` — a DRY
  section walk: cond-17 random counts CLOSED so the pinned RNG stream is untouched; the random-branch
  idle variants (bat3 slot-2 mode-4) are the documented residual); host `enFacing[8]` = the raw-$96DE
  model (zeroed; stamped at the 3 aim sites) — mvFacing keeps its GATO default-1 interim.
- **field.c**: `pcStanceIdx(charId, facing)` = ROM idle window (Crono win[0], others win[1] — every pinned
  capture's slot); the Crono bob poses = win[2-state] (facing0 == the retired fixture); `ctAnimengPcBlit`
  falls back to ROM-direct E3/sheet reads (the sparse bakes == ROM, so byte-identical either way);
  `battleAttackRender` resolves each swing phase from the per-facing windows (approach = run+0x18 by
  travel dir, strike = +0xC4 by victim aim, recover = +0x38 by home aim; crit steps mapped to the same
  windows; planted actions keep recover facing 1 = the pinned GATO instance); **`battleEnemyGenRender`**
  (NEW) draws every staged id with no dedicated renderer from the ROM chain (id-gated GATO now — the
  slot-3 GATO-swap is dead); wired into the interactive compose + battleshot + RENDERWRAM (which now
  reads the SEED's own $974A/$96DE and stages inert action state).
- **Validation**: quick 100/0/1; re-pins (all oracle-justified): pc-approach-early/late + forest-lastkill
  (stances/poses now facing-true in the forest), bat8/bat9 renderwram ×3 (facing fields + enemies in
  frame + the seat1 pose/stance mismatch HEALED: bat9 Lucca stanceIdx now == her live $39). New checks:
  bat2-enemy-visible (Beetle+Avian), bat3-stance-facing, bat4-enemy-visible (Imps) on the user's own
  batalha_2/3/4 captures (saves/battle_re/bat2/3/4.raw.wram + bat1..9_lake.state persisted).
- **Residuals** (unchanged from §7): ptrB dwell decode (idle cycles hold their pinned fixture cadence /
  enemy rest is static), $8E's PC-tick window select, planted-recover facing, the random-branch initial
  modes, enemy palette colors 13-15, per-slot window cursors/dissolve for generic enemies.

---

# ADDENDUM-HETAKE (same session) — the "leap arc" was the MOVER + POSES, not a pixel trajectory

bat3 monitor (slot-0 Hetake attacks Crono, no input, f137-199): $96DE[0]<-3 aim (f137), pose 0x03
windup, live foot (120,63) -> (148,67) at +2px/f (f142-155, the ordinary approach mover; arrival = the
box law), poses 0x04 (arr+3) -> 0x05 (arr+8) -> 0x06 (arr+29), return -4px/f (f188-195) with $96DE<-2,
home + re-aim 3 (f199). The Hetake AI (CC/9E36): SS1 = cond-17 random 16% -> move-only `00 00 06 04`
(mode 4 = the CLOSED-mushroom special idle -- bat3 slot2's mystery mode solved: a random hide move!);
SS2 alternates `01 01 05 00` (attack in place) and `01 01 06 0E` (attack with pre-move 0x0E approach).
Our engine's mover ALREADY walked these (aiPreMode) -- the baked ctHetakeAtkTraj arc EMBEDDED the
capture's own travel on top (double-counted, capture-victim-aimed = the user's "it does not animate at crono's
correct position"). SHIPPED: battleEnemyPoseBlit (shared type-0 E3 blit) + battleHetakeRender's acting
branch = ROM poses 03/04/05/06 riding the live mover foot (windup keyed on mvPendAtk during the walk);
timings = the capture's internal spacings (3/8/29) on our pinned window model. ctHetakeAtkTraj/
AttackKf/AtkBlit retired to fixtures. Suite FULL 123/0/3; new check hetake-attack-pose (f770 adjacent-
to-victim strike-settle). Residual: the exact think-gap anchoring of the pose steps vs our
CT_BATTLE_ENEMY_THINK model; the -4px/f return's exact stop-test.

---

# SCOUT: scene 254 (user pointer) = the FAIR BATTLE-TUTORIAL room

Our `--scene 254` = the Millennial-Fair battle-demo tent (the textbox about how attacks vary in combat; Crono + partner + 3-4 staged practice enemies visible in the field view). The user cites
it as THE exemplar of the battle system + multi-enemy target indicators -- a designed demo arena. Use it
to exercise/verify: multi-enemy target-select (hand cursor $9614 nav across >2 targets), the enemy-name
HUD with mixed counts, and tutorial-triggered battles from a scripted room. (The ORACLE loc for it is NOT
0x0FE -- a candy-donor teleport to loc 254 lands in an empty event-gated room; map our scene index to the
oracle loc via the scene table when driving the oracle side.) Not yet driven to battle in this session.

---

# ADDENDUM-CRIT (session 2) — the crit is LEAP -> double slash ADJACENT -> late return; beat-B effects need a LIVING victim

bat3crit oracle (saves/battle_re/bat3crit.state = bat3_lake with Crono crit-rate $5E72=0xFF -- the
crono_crit_ready technique -- deterministic crit; monitor + OAM/fb sweeps):

- dispatch f67 ($9877=01); **windup pose 0xD7 = the FACING-2 crit-windup window (+0x98)** -- the fixture's
  D5 was the f0 instance; the whole crit ladder is facing-windowed like everything else. Slashes = the
  facing's strike (+0xC4: 5A/5B/5C) + follow (+0xC8: 5D/5E) windows, rel 36/51/62/74/89 (== the GATO
  ladder rels within ~3f).
- **the leap**: no movement until rel 23, then live foot (160,71)->(132,65) at X **-4/f**, Y **-1/f**
  (f92-98) = the CF/F9FB trig machine: offset = trig(bearing)*(4*phase), phase per FRAME (the enemy
  walk's 4x speed), 8-phase segments. Halt on the FIRST **STRICT** AABB overlap (stop dx=12; the <= form
  would stop at 16). Crono strikes BOTH slashes planted there (X constant f98-186).
- **return at rel 113** (home re-aim $96DB<-3 f180, travel f183-193 at 4px/f, settle pose 4B f3, re-aim
  nearest f210 -> f2 idle). The engine's normal-attack return (rel 60) sent him home MID-crit = the
  user's "he should deliver two attacks THERE ... and then come back".
- **victim flash**: slash A = pal4 swap on the victim's 4 OAM cells (f162-168, ~7f) + the screen wash
  (fb-mean 65->104 peak, ~12f fade). Beat B: NOTHING here -- the 14-HP Hetake died at slash A. The GATO
  capture (victim alive, 76 HP) HAS Flash B (battle_timing_audit SS2). Law: beat-B victim flash + wash
  fire iff the victim still lives.

SHIPPED: pcAtkMvCritLeap (arm simulates the trig leap to learn the arrival -> goal; tick replays it
per-frame from rel 23, CT_CRONO_CRIT_LEAP0), CT_CRONO_CRIT_RETURN=113, beat-B alive gate (victim pal4 +
wash). GATO crit visuals: leap up, both slashes adjacent, GATO white-silhouettes at BOTH beats (alive).
Re-pin crit-flash2 (f276: Crono now still adjacent at beat B -- the fix); new checks crit-leap-mid +
crit-beatB-dead-suppressed. Suite FULL 125/0/3. Residual: the exact strict-vs-<= box form for the NORMAL
walk arrival (enemy path keeps <=, pinned); the leap's Y observed -1/f vs trig -0.8/f rounding.

---

# ADDENDUM-DWELL (session 2) — the ptrB dwell law + the window-cursor machine (enemy idles ANIMATE)

The tick C1/33A0-348C decodes it whole:
- window copy (C1/33DF): $97D9[0..7] = ptrA[classbase + facing*stride ..] (as known);
- **dwell copy (C1/33F4): $9799[k] = CC/FBCE[ ptrB[classbase + k] ]** — ptrB is indexed by the CLASS BASE
  ONLY (facing-independent) and the raw byte passes the CC/FBCE staircase remap (= floor(raw/5)).
  Verified 5/5 vs staged rows: Imp 0F/0A->3/2, Hetake 0F/1F->3/6, Diablos 0A->2, GATO-walk 0F->3.
- cursor (C1/3407+): $9694 counts the entry's dwell down one per LOGIC TICK (2 render frames — bat4 live:
  imp walk {3,2,3,2} = 10 ticks = the observed 20f cycle); expiry -> pose = $97D9[++cursor], wrap at 4
  (C1/3468) or at a zero-dwell entry (C1/341A/347B); reload $9694 = the new entry's dwell.

SHIPPED: CtBattleEnemyIdleWin returns the REAL dwells; battleEnemyGenRender cycles the window statelessly
(phase = elapsed logic-ticks mod the cycle) — Imps flap, Diablos beat wings, Nagas sway; single-pose
windows (Hetake rest `01 FF`) hold as before. Re-pins: the 5 renderwram fnvs (a cycling pose at the
synthetic frame). Suite FULL 125/0/3. Residuals: the PC tick (C1/2E00) cadence differs (Crono bob
observed ~40f period vs this law's 8f — the bob keeps its pinned fixture); GATO's 120f idle-swap period
vs his ptrB 1F/1F (=6/6 ticks -> 24f) — his dedicated renderer keeps the pinned fixture; the imps'
facing-cycling wander (the $5E15 idle mover) unmodelled; cursor phase-reset on window re-copy.

---

# ADDENDUM-MARLE-MELEE (session 3) — the close-melee ladder is per-facing windows (rel-timed)

bat3marle.state = bat3_lake + Marle staged/live at (168,143) adjacent to Hetake slot2 (168,127) + her
crit zeroed ($5EF2=0). Driving HER physical on the ADJACENT victim (A, Left-wrap, A): $9877=0x03 (the
40²-gate CLOSE selector) and the ladder, rel to dispatch: **0x1C@24, 0x2D@27, 0x90@42, 0x91@52, 0x6F@72,
idle@77** — facing-0 (victim straight above). polish3 §3.1's {24,35,94,95,75} = the SAME flat row
offsets **{+0x18 run[0], +0x2A, +0xC9, +0xCA, +0x38 recover[0]}** in her facing-2 row (5/5 match, same
+0x38 recover offset as Crono). The default-target run reproduced the FAR path ($9877=0x02, crossbow
ladder 37/8C/8D/D9, facing re-stamped 2 at dispatch = the victim aim). SHIPPED: battleMarleAttackRender
melee branch (rel ladder + victim-aim facing + planted foot; pcStanceIdx idle pre-24/post-77; no bolt
spark). Pinned in animengcheck-marlemelee (f0+f2 5/5). Suite FULL 125/0/3.
Residuals: the crossbow (0x02/0x0F) ladder's own facing instances (both captures were facing-2; the
window offsets for 37/8C/8D are un-located); a melee impact effect obj (none observed in the monitor);
the confirm-order quirk ($9614 0->1->0 nav yielding slot2 -- the display-order mapping un-derived).

---

# ADDENDUM-GENERIC-DISSOLVE (session 3)

Generic type-0 enemies now DISSOLVE on death (were vanish-on-death): battleDissolveRender's new branch
freezes the idle-cycle pose AT THE DEATH FRAME (stateless: death = now - (step+HOLD)), rebuilds its 4
cells into a raster scratch sheet from the slot's LZ cache, and rides the SAME dither + palette-ramp
compositor (ROM live palette -> the red target, the FD/E962 law) the Hetake branch uses. Verified live
on the forest AMBUSH fight (bat2's formation, played from the field pre-battle wram
saves/battle_re/ambush_pre.raw.wram): Beetle mid-dither f270; check field-battle-ambush-generic-dissolve
also pins the whole mixed-type fight (victory f1693, Avian tech span included). Suite FULL 126/0/3.
Note: the loc-112 battletest resume re-stages EMPTY (the mid-cutscene approach actors are parked when our
re-fired D8 scans) -- a harness scenario quirk, not an engine defect; use field-entry pre-battle wrams.
Remaining residuals: PC-tick (C1/2E00) cadence; GATO 120f idle period vs ptrB 24f; the imps' idle facing
wander; the crossbow ladder's facing instances; enemy palette colors 13-15; random-branch initial modes;
cursor phase-reset on window re-copy; the dissolve red-target per-arena variance.

---

# SESSION-4 ROUND — crossbow facings, PC-tick partials, the wander law (+ its blocker), colors 13-15 closed

- **CROSSBOW per-facing** SHIPPED: the ranged ladder = consecutive row offsets +0xC4/+0xC5/+0xC6 then
  idle[0] (+0x0C) of the victim-aim facing. bat3marle_f0.state (Marle far BELOW Hetake0) played the f0
  instance {2F 88 89 D3} at the same rels as f2 ({25,35,45,idle 62}); f2 = the retired bake {37 8C 8D D9}
  byte-for-byte -> ZERO re-pins. Timing/drift stay on the pinned ctMarleAtkTraj fixture. Pinned
  animengcheck-marlebow.
- **PC tick (C1/2E00) PARTIALS**: window select = $A440 (the PC anim STATE) x 4 (so idle=state 3 -> +0x0C,
  run=6, recover=14, strike=49 -- the whole TOC is state*4); dwell = raw ptrB byte / 5, MINIMUM 1
  (C1/2ECB div + INC); the PC ptrB IS facing-rowed with row stride = strideA/4 (= the staged $A2C8;
  Crono 0x4A, Marle 0x3E). UNRESOLVED: the bob's observed 7/13-frame steps vs the f2 idle dwells
  {2,1,1,1} -- a per-step divider between the dwell units and render frames is still missing; the bob
  keeps the pinned fixture cadence.
- **THE WANDER LAW (derived; implementation BLOCKED)**: the combat-start AI eval arms the FIRST entry's
  mover regardless of op -- GATO f376 $5E0D<-1 (writer C1:9891; his famous HOLD-fight walk at f421 IS
  this arm, not his ATB turn), bat2 Beetles mode-1, bat4 Imps mode-1, Avian mode-0x0F. Arming the
  substantive pre-move in-engine walked GATO in every pinned MASH fight (30 checks red) where the oracle
  holds him STATIONARY -> the ROM's strike-time mover HALT/reset law (the $974A-bit7 family) must be
  derived first. Reverted to the op-00-only arm (green); the pre-turn wander is the documented boundary.
- **COLORS 13-15 CLOSED**: the OBJ-palette tails are the BRACKET triad -- c13=7FFF white, c15=14A5 dark,
  and c14 = the fade engine's ANIMATED grey ramp (writer CF:E5DB, 0x421/frame = the period-62 triangle
  battleBracketRender already models as ctBracketPulse). Enemy body cells never index above 12 (proven),
  so the general enemy render's 1-12 palette is complete.

---

# SESSION-5 — the "halt-on-strike" was a MYTH: the initial arm is the ENTRANCE SETTLE (anchor-base boundary)

HOLD vs MASH side-by-side on gato_press_action (monitor $98A7/$1D12/$1D29/$974A to f700):
**both runs walk identically** — mode-1 latch f380 (C1:336A from the f376 $5E0D arm), one segment f421-453,
Y 0x6F->0x76 (111->118 at 1px/4f), then the mover idles (the 32f re-arm sees the arrival-box overlap ->
stop). The MASH's later f638 mode-6 is the ordinary post-attack return. There is NO strike-halt law.

**The real meaning:** the combat-start arm IS the ENTRANCE SETTLE — GATO's staged foot (136,111) walks
once to his rest (136,118 = the front-idle foot every bake uses). Our engine's GATO bakes were captured
SETTLED, so the render anchors already embed the +7px; arming the live mover on top double-counts it —
that is why the session-4 experiment moved GATO in every pinned fight (the oracle moves him ONCE, from a
base 7px higher). Same story as the old Crono-attack "single-base" fix, now on the enemy side. The
"idle/settle PHASE datum" note in the facing-bake addendum (min-corner 5px above the stance extent) was
this same settle observed from the other end.

**Implementation boundary (the next structural milestone):** re-base the GATO (and Hetake slot) render
anchors to the STAGED foot + let the initial arm's live mover carry the settle (and the Imps'/Beetles'
continuing slow approach = the "wander" — their box never overlaps at range, so they creep, re-based
every 32f). Expect a broad, oracle-justified re-pin wave (every entry-window shot moves by <=7px of
settle timing); the settled steady-state is identical. Until then the op-00-only arm stays (suite green)
and the pre-turn wander remains the documented gap.

---

# SESSION-6 — the settle-slice attempt: two real bugs fixed in design, one true blocker found (pacing)

Attempted the settle milestone slice (initial arm + first-action settle latch + bake-delta exclusion +
true-live generics). Two design corrections that SURVIVE for the next attempt:
1. the initial arm's target value 05 (random PC) must NOT resolve through battleAiTarget's RNG draw
   (C1/A452) -- it shifted the pinned stream (21 reds); the sanctioned proxy = nearest living PC
   (GATO's creep aims the nearest/lead; the settle stop is ATB-emergent so the seat choice steers <=8px).
2. the latch design (enInitArm / enSettleX/Y at battleOpenAction head; bake-anchored delta = 0 during the
   creep, live-settle-staged after; generics read mvLive directly) is sound and reusable.

THE TRUE BLOCKER: the settle magnitude is PACING-EMERGENT. Oracle cadence: mode write f376 -> first
segment f421 (+45f) -> first turn f478 (+57f) = ONE segment (7px). Ours: arm at combat start f82 ->
segment f163 (+81f) -> first GATO turn ~f235 (+72f) = TWO segments (14px) -- the pinned walk-in traces
(enemy-approach f235 / enemy-arrive f355, gato-walk et al) then shift by the head start, a 21-check
cascade that cannot be blanket-re-pinned without re-validating each against the oracle timeline. The
milestone therefore DEPENDS on re-deriving the combat-start pacing (why the oracle's arm->segment gap is
45f vs our 81f -- the mode-machine's initial cadence seed, and the enemy first-turn ATB alignment) so our
creep also spans exactly the oracle's segment count. Reverted to green (quick 104/0/1, FULL unchanged);
the op-00-only arm remains the shipped state.

---

# SESSION-6b — the mode-machine INITIAL CADENCE law (the last derivable piece of the settle milestone)

Monitor $9897/$9819 with writers through the arm window (gato_press_action):
- staging seeds **$9897[slot] = 0x20 + slot** (CC:E3F6 blanket 0x20, then CC:EBFD writes the per-slot
  STAGGER -- GATO slot3 = 0x23) and $9819[slot] = 4 (CC:EBE3, the segment step cadence);
- the countdown (writer C1:3727) runs ONLY once a mode is latched (starts f377, right after the f376
  $5E0D write), decrementing per LOGIC TICK (the 4-of-5-frames pattern ~1.25f/tick): 0x23 ticks ~ 44
  real frames -> the first handler pass/segment at f421 = arm + 45 EXACTLY;
- NO reset at arm: our battleMoveArm's `mvModeCad = 1` (fire next tick) is an INVENTION vs this law --
  the ROM lets the staged staggered counter time the first pass. (Correct model: on the INITIAL arm keep
  the staged 0x20+slot count; on turn re-arms the ROM behavior is the standing 32-tick reload.)

Even with this law, the creep spans TWO of our segments before our first GATO turn (~f235-analog = 153f
after combat start) vs the oracle's ONE (f478 - f376 = 102f) -- the enemy first-turn ATB alignment gap
(~50f, long absorbed by the pacing model's pinned offsets) is the true prerequisite. The settle milestone
is therefore: (1) re-audit the enemy first-turn alignment vs oracle, (2) apply the initial-cadence law
above + the session-6 latch design, (3) the re-pin wave with per-check oracle justification.

---

# SESSION-6c — the ATB law CONFIRMS our constants; a NEW inconsistency opens the re-audit's first question

Monitor $AFAB/$B158 (gato_press_action HOLD): staging seeds Crono 0x01 (pre-charged first actor) /
Marle+GATO 0x1F (the stagger -- our 0/0x1E model is the same law, off-by-one semantics), reloads $B158
byte-match our battleReload (Crono 0x48=72, others 0x66=102); countdown per logic tick (~2.53f, writer
C1:826B) from f378; GATO ATB-0 at f454; dispatch f478 = **ATB-0 + 24 = our CT_BATTLE_ENEMY_THINK
exactly**. So the first-turn law == ours; the "50f gap" is only the combat-start anchor frame difference
between scenarios, not a law mismatch.

**NEW (the re-audit's question 1):** in this HOLD run GATO's first attack-A ($9877=31, f478-637) fires
REMOTELY from (136,118) -- 26px from Crono, NO walk-in (position constant through the span) -- while our
pinned model (battle_enemy_move_target §A.6, from its own oracle capture) always defers the dispatch
until box-overlap arrival (our f235 approach -> f355 arrive -> strike adjacent). Both behaviors are
oracle-observed on different runs/states: the CONDITION selecting walk-in vs remote (range class? the
attack entry's targeting 05-random vs 06-nearest? the pre-move mode value vs distance?) is underived.
The settle/pacing re-audit starts here: reconcile walk-in-vs-remote, pin our combat-start anchor frame,
then apply the initial-cadence law + the settle latch (sessions 6/6b).

---

# SESSION-7 — E.3 RESOLVED: enemy dispatch = ATB-0 + 24, position-independent

Cross-checking the old walk-in model against the HOLD ATB monitor closed battle_enemy_move_target's
open E.3 (see that dossier's new addendum): the dispatch gate is the ATB ORDER (+ the 24f think), never
an arrival flag — the walk-in "defer" our engine implements (mvPendAtk -> battleArrive) was the guess.
The corrected pipeline for the milestone: (a) every enemy turn dispatches at ATB-0+24 from the live
position; (b) the pre-move approach arms at the PREVIOUS action's span end (first = the combat-start
initial eval) and walks on the $9897 stagger/32-tick cadence concurrently with the ATB; (c) the strike
anims anchor at the live foot wherever it is (remote attack-A is canonical). Milestone order stands:
implement (a)+(b)+the settle latch together, then the justified re-pin campaign fight-by-fight.

---

# SESSION-8 — THE MILESTONE LANDS: wander law derived + E.3 pipeline shipped (quick 104/0/1, FULL 126/0/3)

Derivations (all on gato_press_action, `--input "100:8,0:1900"` HOLD unless noted):

1. **$1D0C/$1D23 staging = IDENTITY with the $29BF record.** Probe: record `08 06 08 0F ..` -> foot
   (16*8+8, 16*6+15) = (136,111) == the staged $1D12/$1D29 boot values EXACTLY. Our battleMoveInit
   already implements this law, and our pinned `battlemove f239 live=(136,111)` proves OUR staged foot
   equals the oracle's. **SESSION-5's "the bakes embed the settled foot / +7px double-count" premise is
   DISPROVEN** — the bake anchors at the STAGED foot; render delta = live − staged is already correct;
   the settle latch (enInitArm/enSettleX/Y) is UNNECESSARY and was not built.

2. **THE WANDER LAW.** The HOLD monitor beyond f700 (SESSION-5 stopped at f700 and saw only the first
   segment): GATO strolls continuously between attacks. $974A latch timeline: f380 mode-1 (initial) ->
   f638 mode-6 (attack span end) -> f1489 mode-1 (SONG span end) -> f1905 mode-6 (attack-2 span end).
   §1 raw bytes (CC/A77B): `[01 00 05 01][00 00 06 06][02 10 05 00 00 00] FE`. **The span-end CURSOR ARM
   law**: at combat start (initial eval) and at every action's span end, the AI arms the mover from the
   §1 list AT THE PERSISTENT CURSOR — a move-only op-00 arms its tgt/mode bytes and IS CONSUMED (cursor
   advances); a substantive entry arms its PRE-move bytes [2]/[3] (cursor stays); wraps at the section
   end. One rule generates the whole latch timeline (attack pre-move -> trailing 00 retreat -> song (no
   suffix) -> WRAP to the attack's pre-move -> ...). The old aiPre-at-dispatch + trailing-00-peek
   (aiPostMode) were partial views of this cursor machine.

3. **The return "dance" converges on the REF.** Oracle path f668-1050: down 7, left 8, up 24+, right 7
   — the C1/3AA0 octant/oscillation machine — NET rest (135,85) ≈ the derived ref = victim +
   trig(away)*0x40 = (134,83). Our battleMoveReturn walks straight to the same ref (rest-equivalent,
   the dossier's §A.1/E.5 latitude — now explicitly extended to TIMING: our dance is shorter, so
   turn-2-style dispatches come earlier than the oracle's by the dance-duration difference).
   BONUS: the post-song walk (right+down, mystifying in the first monitor) is the mode-1 approach
   toward MARLE — the nearest PC from the ref rest. Explained, not special.

4. **The ATB retry law CONFIRMED at scale** (already implemented at the dispatch site): turn-2 ATB-0
   f887 lands MID-DANCE ($98A7 active) -> full reload 0x66 -> ATB-0 again f1143 (f887+102*2.5 EXACT)
   at rest -> window f1151. Turn-1 fired at f454 BETWEEN segments (mvActive==0, NOT settled) -> the
   gate is exactly $98A7 (segment-active), not a settle state. Think latency measured 24 / 8 / ~0
   across turns 1/2/3 — NOT a constant; our CT_BATTLE_ENEMY_THINK=24 (turn-1-exact) is kept and the
   variance documented as the underived queue-processing latency law.

5. **The $9897 cadence clock**: staged 0x20+slot (stagger); decrements on the 4-of-5 frame pattern
   (~1.25f/count) for ALL slots from combat-live, FREE-RUNNING regardless of mode; wrap -> reload 0x20
   + handler. $A013 spans freeze counter+mover (probe: no decrements f478+); the pending-turn think
   freezes the HANDLER+mover only (probe: counter decrements f454-457; the f461 pass never arms;
   position constant). Post-span resume puts the first return segment at f660 — EXACT fit. $98A7 =
   the per-slot segment-active flag; segments 32f of walking every 40f (0x20 × 1.25).

6. Song $9877 = 0x10 = the tech entry's p1 byte (oracle write f1151) — a $9877-selector datum for the
   span table (our pinned selector mapping unaffected; noted for the anim-selector law).

IMPLEMENTATION (src/battle.c/h): battleAiInitialMode2 arms the FIRST entry's movement regardless of op
(05-target -> nearest proxy, no RNG); battleSpanEndArm = the cursor law (replaces aiPre/aiPostMode);
battleOpenAction: every non-reactive enemy action goes through the think latch (the pre-move defer
branch DELETED); battleArrive opens the window from the live position + stamps the CD-script facing
(C1:600C) toward a physical's victim; battleMoveApproach arrival = plain stop; battleMoveTick: the
4-of-5 accumulator (mvCadAcc) + free-running staged counters + per-slot think-hold. RNG stream BYTE-
IDENTICAL to the old pipeline (trace: draws 77,2,38,8... same order+values; CT_BATTLE_RNGTRACE added).
The old "-13/hit" fear was a stale note from the pre-$AE93-rec-index era — both pipelines deal -8.

RE-PIN CAMPAIGN (justifications in tools/regress_checks.json notes, per check): gato-walk +
ai-cursor-cadence re-anchored to the E.3 timeline (creep f206-234 -> think f235 -> REMOTE window f259
-> retreat f442+, HP/draws byte-same); crit-x2/confirm-boundary/status-inflict/battlewait = frames +
ATB-residual fields only (values identical; battlewait victory 2387->2382); blink/victory FNVs =
same scenes with GATO on the E.3 path (the "victory" shots have been mid-fight scenes since the
pacing_v2 span growth — name is historical; visually verified); facing-left/right RE-TIMED to the
wander's real facing windows (f1550 left / f2100 right, battleface-verified + visual); song trio
re-timed to the new window f694-1005 (beam/notes visually verified); tech FNVs re-timed by the
dispatch delta (cyclone f250 unchanged, ice -37f; whirl/orbit/shatter visually verified); item checks:
GATO's attack now precedes the scripted item -> ordering shift (tonic-heal 7+50=57; cap still 70;
Marle-target values identical), pins de-anchored from frames. Old walk-in pins (enemy-approach f235 /
enemy-arrive f355) retired — the arrival-defer they pinned was the E.3 guess.

RESIDUALS: think-latency variance (24/8/0) underived; the octant dance path (rest-equivalent only);
the span-end arm's movement-target 05 uses the nearest proxy (the ROM may draw at arm — unprobed, our
stream is pinned green); Hetake/Avian §1 leading modes under dry cond-17 (no wander armed — their
oracle creep unverified); $9877=0x10 selector datum.

---

# SESSION-9 — THE $26 DRAW LEDGER: boot block index-exact, arm/eval/retry draws live (FULL 126/0/3)

The residuals lap. Monitoring $26 itself (--diff) across the HOLD + forest oracle runs produced the
canonical DRAW LEDGER, closing the proxy questions and one long-standing render residual:

1. ~~**The battle boot ZEROES $26** (oracle f234: A0->00)~~ — **CORRECTED 2026-08-22: the boot COPIES
   `$29AD` into `$26` AND `$B3E6`** (`C1/800A LDA $29AD / 800D STA $26 / 800F STA $B3E6`, one load, two
   stores — `python tools/dis.py 18000 0x1C 1 0`). The f234 observation was real and the law drawn from it
   was wrong: in THIS capture `$29AD` happens to be 0, so "A0 -> 00" and "seeded from `$29AD`" are
   indistinguishable. They are not elsewhere — `bat9_lake.state` carries `$29AD = 86` and `$B3E6 = 86` at
   f0 (its boot copied the nonzero cursor), and the end epilogue hands the final `$26` BACK:
   `C1/8443 LDA $26 / 8445 STA $29AD` (oracle GATO fight `f2257 7E29AD=2E*(C1:8448)`), right before the
   C1/B442 block->sheet writeback. ROM-wide the accessors are exactly C1/800A (read), C1/8445 (write), and
   the MENU's C2/805E + C2/E6A8 (`LDA $0D00 / STA $29AD`, its own cursor on exit); bank C0 never touches
   it — between fights it simply persists in WRAM. So "the stream origin is canonical, not the field's
   leftover index" is HALF right: it is not the field's `$26`, it is the persistent cursor `$29AD`, and the
   field does inherit the advanced value. Our port had `s[0x26] = 0` and `$B3E6 = 0` — every battle
   replayed an identical RNG sequence (same variance, crits, drops, AI draws) and the field never got the
   cursor back. Fixed in battleCombatStart / the battle-end writeback; pinned by
   `field-battle-rng-cursor-persists` on a synthetic seed with `$29AD = 86`. Everything BELOW this line
   (the shuffles, the count-exact ledger, idx 0x1B -> 0x75 -> roll 17 -> CRONO) stays true — it was all
   measured on `$29AD = 0` seeds, where the two laws coincide. Then the boot draws a fixed block: **shuffle-1** (C1/8072, rand%$AEC7 with
   consumed-entry redraw) fills $B163 from a pool of [one entry per alive PC][entries 0x0B..0x04 = all
   8 enemy slots]; **shuffle-2** (C1/80AD, rand%8) orders the PRESENT slots into $B23A. The RNG is the
   fixed table walk (C1/AF22 = table[$26++] % A — the CALLER loads the divisor), so the whole block is
   deterministic: **GATO fight 25+2 = 27 draws, forest 25+7 = 32 — matched EXACTLY by simulation and
   by our port** (the "13+14" reading was frame quantization). Our engine now lands the initial-arm
   roll at **idx 0x1B exactly like the oracle f376 — same table byte 0x75 -> roll 17 -> CRONO — the
   same creep target the oracle walks to.** Ported in battleCombatStart (the shuffles + reset).
2. **The initial eval draws** (f376 +1 GATO; forest +6 = 3 Hetakes x [cond-17 + target resolve]):
   cond-17 is a REAL roll now (the old dry probe reversed) and the first entry's TARGET resolves even
   when its mode byte is 0 (Hetake `01 01 05 00` — $5E15 is written unconditionally). **The bat3
   slot-2 CLOSED MUSHROOM residual is CLOSED**: Hetake §1 section-1 = cond `17 10` (16%) -> move-only
   `00 00 06 04` = MODE 4 — the closed-mushroom idle IS the 16% branch, and with real rolls it
   reproduces (the forest fight rolled one open: visually verified in the re-pinned shots).
3. **The span-end arm draws** (f1462 = song span end arming the attack's 05 pre-move, +1) and **the
   move-only 06 arm draws NOTHING** (f637: no tick) — the asymmetry is on the ledger itself.
4. **The aborted attempt (ATB-0 mid-walk) runs the decode and draws** (f893 +1 mid-dance) then
   discards + reloads — implemented as battleAiRunTurnEx(dry) at the retry site (cursor/aiHit/$AEE3
   untouched).
5. **Micro-law OPEN (the next derivation's entry points):** the per-site draw COUNTS don't fully
   reduce to our thirds-resolver — the forest steady-state shows span-end arms drawing exactly +1 on
   substantive entries with EITHER 05 or 06 targets, and dispatch decodes drawing +2 where cond(0)+
   thirds(1) predicts 1. Disassembly found: **C1/988E writes $5E15 <- $AECC (the target is resolved
   UPSTREAM into $AECC)** and **C1/98D3: a rand%100 vs #$19 (25%) roll inside the AI action-list
   walker itself** (per-entry probability branch over the FD/BA4A stepping) — the AI interpreter has
   its own roll layer. Deriving the $AECC resolver + the 25% walker = the full dispatch draw law
   (and likely the think-latency variance 24/8/~0, still open).

RE-PIN wave 2 (justifications in regress_checks.json notes): the canonical stream shifts every bonus
roll by small deltas (gatoHP 60->59, cyclone dmg 20->21, drop/charm/status roll values) — mechanisms
identical; forest fights re-anchored (victory f1222; the MASHA fight now kills both normal Hetakes
pre-attack so hetake-attack-pose moved to the HOLD script: window f259-461, the open attack-pose at
f280 REMOTE from the attacker's own foot — the E.3 render corollary visually confirmed); lastkill/
ambush dissolve shots re-timed to the new kill frames (visually verified mid-dither). quick 104/0/1,
FULL 126/0/3.

Remaining residuals after this lap: the AI-interpreter micro-law above ($AECC resolver, 25% walker
roll, think variance); the octant return-dance path (rest-equivalent only); PC bob 7/13 divider;
GATO 120f idle-swap; cursor phase-reset on window re-copy; dissolve red-target variance.

---

# ADDENDUM-DWELL-RATE (2026-08-26) — ONE dwell unit is TEN render frames, not two

User report (translated from Portuguese): *"the animation of some enemies (in battle) is too fast"*. It was: the port ran the
idle window at **2 render frames per dwell unit** — five times the ROM's rate.

The tick `C1/3407` decrements the per-slot counter `$9694,X` **once per VISIT** and reloads it from
`$9799` (= `CC/FBCE[ptrB byte]` = floor(raw/5), ADDENDUM-DWELL) when it expires, so a window entry lasts
`dwell` VISITS and the on-screen period is `dwell x (frames per visit)`. **The visit is not once per
frame**: `C1/3232` dispatches a rotating GROUP (`$A4` 0..3, member list `$992D`, member cursor `$9841`
which walks 3 entries per call and stops at 3 — `C1/348F CMP #03`), and only some visits reach the DEC.

MEASURED (`--monitor "<counter>:1" --diff`, the DEC frames), two independent fights:

| fixture | counter | DEC frames | intervals |
|---|---|---|---|
| `bat2_lake` Beetle slot 1 | `$9695` | 5, 18, 25, 38, 45, 58, 71, 84 | **13 / 7 / 13 / 7 / 13 / 13 / 13** |
| `bat4_lake` imp slots 0/1/2 | `$9694/5/6` | 2, 15, 22, 35, 42, 55, 62, 75 | **13 / 7 / 13 / 7 / 13 / 7 / 13** |

= EXACTLY **2 dwell units per 20 render frames**. Cross-checked on the OUTPUT, not just the counter:
the Beetle (dwell row `02 02 02 02`) advances its cursor `$969D` at f18 / f38 / f58 / f78 = every **20f**
= 2x10, and bat2's Avian slot 5 (dwell row `03 03 03 03`) republishes its assembly index `$990F` at
f288 / f321 / f348 / f381 = every **~30f** = 3x10.

SHIPPED: `CT_BATTLE_IDLE_DWELL_F = 10` (field.c), used by both the live idle cycle and the death-frozen
pose replay in `battleDissolveRender` (they must agree or a dying enemy freezes on the wrong pose).
`CT_BATTLE_IDLETRACE=1` prints `idlepose f<N> slot=.. id=.. mode=.. cursor=.. pose=.. dwell=..` on every
cursor change; on the `CT_BATTLE_RENDERWRAM` vehicle it first SWEEPS the render frame (82..682, or
`CT_BATTLE_IDLESWEEP`) so the whole cadence is printable from one process. On `bat2.raw.wram` our engine
now prints the Beetles advancing every 20f and the Avian every 30f — the oracle's own numbers.

OPEN (stated, not guessed): **the 13/7 jitter belongs to the `$A4` group scheduler**, which is not
derived — `$A4` cycles 0->1->2->3 every 6-7 frames (`--monitor "00A4:1" --diff` on bat4) and this
combatant's group DECs on two visits out of three. A stateless replay uses the MEAN (10), so an
individual pose boundary can sit up to ~3 frames either side of the ROM's. The earlier ADDENDUM-DWELL
claim "imp walk row {3,2,3,2} = 10 ticks = the observed 20f cycle" was wrong by this same factor of 5
(it is a 100-frame cycle); this addendum supersedes it. Also unchanged and still open: which fights arm
an enemy's idle window at all (in `bat4_lake` the imps' counters do not tick for 400 frames — their pose
changes come from the AI/mover, not the window).

---

## THE `$A4` GROUP SCHEDULER, DERIVED (2026-09-08) — the "13/7 jitter" is a 6/7 alternation

The ADDENDUM above records the `$A4` group scheduler as **not derived**, and two other pieces have since
been blocked on it: the idle dwell's stateless mean-10 replay, and the `$5E0D`/`$974A` split
(`battle_enemy_move_modes.md` V19), whose commit rides on this cadence. It is derived here.

### The counter lives in BANK CF, which is why a bank-C1 scan never found it

```
CF/E477  INC $A4
CF/E479  LDA $A4 / CMP #04 / BNE ->FE481
CF/E47F  STZ $A4              wrap 4 -> 0
```

`$A4` is a free-running **0 -> 1 -> 2 -> 3 -> 0** counter advanced once per pass of `CF/E470`. Every
earlier search for its cadence looked in bank C1 (where the battle logic is) and found only the DEC/INC
bookkeeping below, which is why the cadence looked emergent.

### What bank C1 does with it is BORROW AND RESTORE

```
C1/10A4  LDA $A4 / STA $993B      snapshot the counter BEFORE the dispatch
C1/10A9  JSR C1/2D9F              -> C1/2DA2 LDA $A4 / BEQ / JMP C1/3234, the group dispatch
C1/10AC  LDA $993B / CMP $A4 / BEQ ->110B5
C1/10B3  STA $A4                  RESTORE it from the snapshot
```

and the dispatch itself is:

```
C1/3234  STZ $9841                     reset the per-combatant cursor
C1/3237  DEC A / BNE / DEC $A4 / JMP C1/3252    A==1 -> group 1
C1/323F  DEC A / BNE / DEC $A4 / JMP C1/32A5    A==2 -> group 2
C1/3247  DEC A / BNE / DEC $A4 / JMP C1/32FC    A==3 -> group 3
C1/324F  JMP C1/34A6                            A==0 -> nothing
```

with `A = $A4` on entry (`C1/2DA2`). **So the counter is both the credit and the group id**, and the
three `DEC $A4` are transient: `C1/10B3` puts the value back. That is why `--monitor "00A4:1" --diff
--writer` never shows a DEC — the decrements are undone inside the same frame.

`$993B` is therefore two things at once: the restore snapshot, and the gate on the second increment —

```
C1/348C  INC $9841
C1/348F  LDA $9841 / CMP #03 / BEQ ->13499 / JMP C1/333D     walk the 3 combatants
C1/3499  LDA $A4 / CMP $993B / BEQ ->134A6                   already back at the snapshot -> no bump
C1/34A0  CMP #03 / BCS ->134A6                               capped at 3
C1/34A4  INC $A4
```

so the cursor's wrap can bump `$A4` one extra step, but only while it is below 3 and has actually moved
off the snapshot.

### ⭐ The measured cadence: 6 or 7 frames, alternating

`--monitor "00A4:1" --diff --writer` on `bat3_lake`, taking the `CF:E481` wrap (the `STZ`, which is
always what returns the counter to 0) as the cycle boundary:

```
wrap frames  6  13  19  26  33  39  46  53  59  65  72  79
intervals       7   6   7   7   6   7   7   6   6   7   7
```

and the writers alternate exactly as the two increments predict — on `bat4_lake`:

```
f0 =00 (CF:E481)   f2 =01 (C1:34A6)   f4 =02 (CF:E479)   f5 =03 (C1:34A6)
f7 =00 (CF:E481)   f9 =01 (CF:E479)   f10=02 (C1:34A6)   f12=03 (C1:34A6)
```

**The "13/7 jitter" the ADDENDUM recorded is this 6/7 alternation** — 13 is two cycles (7 + 6), not a
period of its own. The mean is ~6.7 frames per 0..3 cycle, i.e. ~1.7 frames per group step.

### What this unblocks, and what it does not

UNBLOCKED: the cadence is no longer "not derived". The mechanism is four instructions in bank CF plus a
borrow/restore in bank C1, all cited above.

⚠ STILL NOT A PORT. Two things stand between this and landing it:
* the increment is *per pass of `CF/E470`*, and how often that runs is the main loop's own rate — this
  engine's frame is not that loop, so the 6/7 alternation has to be reproduced from the ROM's own pass
  structure rather than assumed to be "every frame";
* `battle_enemy_move_modes.md` V19's consumer needs the per-combatant cursor too, not just `$A4`.

So this closes the derivation the two dependent items were waiting on, and leaves the port as its own
step with a measured target (wrap every 6 or 7 frames, alternating) to validate against.

---

## THE PER-COMBATANT CURSOR `$9841` — THE IDLE-TICK WALK, DERIVED AND PORTED (2026-09-09)

The `$A4` section above ends with "the consumers need the per-combatant cursor as well". This is that
cursor, and it turned out to be two consumers in one pass: the `$5E0D`/`$974A` split's commit
(`battle_enemy_move_modes.md` V19) **and** the enemy idle animation that `CT_BATTLE_IDLE_DWELL_F` replayed
statelessly (ADDENDUM-DWELL-RATE). Both are now `battle.c battleIdleWalk`.

### Where the walk runs: the idle state's two-frame tick

`C1/0003` → `C1/0045` is `PHA/PHX/PHY / ASL A / TAX / JSR ($0051,X)`, a state-dispatch table:
`$0023, $1B19, $1BAA, $106E, $4058, $2986, $006D`. The ATB manager's idle loop calls entry 3 (`C1/8127 LDA
#03 / JSR C1/0003`), and entry 3 is `C1/106E`:

```
C1/109D  JSR C1/007E              wait one frame ($9E semaphore, CD/0036 -> CD/04A6)
C1/10A0  JSL CD/0009              drain the upload queue (CD/04AA -> CD/3E44 spins on $CA20: more frames if needed)
C1/10A4  LDA $A4 / STA $993B      borrow the group counter
C1/10A9  JSR C1/2D9F              THE DISPATCH: A = $A4 -> C1/3234 (1..3 = enemy groups; 0 -> C1/2DA9, the PCs)
C1/10AC  LDA $993B / CMP $A4 / BEQ / STA $A4    restore
C1/10B5  JSR C1/3714              the mode machine, AFTER the dispatch, same frame
C1/10B8  JSR C1/283D
C1/10BD  JSR C1/007E / JSL CD/0009            the second frame wait
C1/10C6  ($993A flush: C1/1B67, C1/0C2D, C1/1153, C1/17DD)
C1/10DC  JSR C1/3714 / JSR C1/283D / RTS      the mode machine again -- NO dispatch on this frame
```

The action state (entry 4 = `C1/4058`, which advances frames through `C1/10FA`) has no dispatch and no
mode machine — which is the structural form of the "$A013 freeze" the port already used. Measured: bat3's
action f106..f283 (`$A013` 1→0) shows **no `$9694` movement, `$9912` climbing to 7 (the action's own
uploads with nobody to reset it), and no `$A4` write at all** — the counter's INC rides the idle tick's
frame waits, so `$A4` freezes with the walk and resumes 3 frames after the span end (f286).

### The dispatch cadence, measured on five captures

Taking the `$9912` STZ (`C1/2D9F`, reported as C1:2DA2) and INC (C1:1F78) writers as the dispatch marker:

```
bat3   $A4 INC   f1 f3 f4 | f6 f8 f9 | f11 f13 f14 | f16 f18 f19 ...     (gaps 2,2,1)
       dispatch  f1    f4 | f6    f9 | f11     f14 | f16     f19 ...     (gaps 3,2)
bat2   $A4 INC   f2 f3 f5 f7 f8 f10 f12 f13 f15 ...
       dispatch     f3 f5    f8 f10     f13 f15 f18 f20 f23 f25 ...
```

**A dispatch runs on two of every five frames, always on an `$A4`-INC frame, and never on the middle
INC of the 2-2-1 triple** — in the port's accumulator terms, on the INC frames that leave the 3-of-5
accumulator at 1 or 0. The group visited is `$A4` *after* that frame's INC (CF's pass runs first: on every
dispatch frame the last writer of `$A4` is C1:34A6, the walk's own bump, with the value CF had just
produced). Over 20 frames the 8 dispatches see every `$A4` value exactly twice, and a given group's visits
alternate 13 and 7 frames apart — the "13/7 jitter" of ADDENDUM-DWELL-RATE is this, and its mean of 10 is
what `CT_BATTLE_IDLE_DWELL_F` was.

⚠ ANCHORED, NOT PRICED, exactly as the `$A4` rate is: the tick's cost is two frame waits plus the queue
drain, and the queue occasionally takes a frame more. bat2 f58..f71 shows one period where the dispatch
gaps go 3,3,3 and the INC gaps 1,2,1 before both snap back to 3,2 / 2,2,1. The port reproduces the nominal
cadence and the sequence; it does not model the queue, so the phase can drift by a frame per such slip.

### The group roster and the cursor

`C1/2D9F`: `STZ $9912` (the upload budget), `A = $A4`, `BEQ` → `C1/2DA9` (the PC group, its own machine
with `$9689` timers — NOT ported here), else `C1/3234`: `STZ $9841`, then `DEC A / BNE / DEC $A4 / JMP`
three times → `C1/3252` (seats 0,1,2), `C1/32A5` (3,4,5), `C1/32FC` (6,7,FF). Each builds `$992D..$992F` in
canonical order **unless a seat's flag `$9925+slot` is set** — then that seat goes first (`$9926` → 1,2,0;
`$9927` → 2,0,1; group 3's `$992C` → 7,6,FF) and the flag is cleared. `C1/333D` reads `$992D[$9841]`,
`C1/348C` INCs the cursor, `C1/348F CMP #03` loops back — **all three seats in one pass**. The flag's writers
are `C1/344C INC $9922,X` (X = cmb, i.e. `$9925+slot` — the defer below) and `C1/1C5A STZ $9922,X` (the
publish); `$9925..$992C` have no other reference in the image.

### Per visited seat

```
C1/3341  LDA $992D,X / BMI              the FF seat -> skip
C1/3349  LDA $96F8,X / BEQ              not STAGED -> skip  ($96F8 = record byte +6 bit7 clear; verified on 4 seeds)
C1/3351  LDA $974A,X / CMP $5E0D,X / BNE ->commit
C1/3359  LDA $96DE,X / CMP $96E9,X / BNE ->commit
C1/3361  JMP C1/3407                    nothing changed -> straight to the tick
C1/3364  LDA $5E0D,X / STA $974A,X      COMMIT the mode
C1/336A  LDA $96DE,X / STA $96E9,X      publish the facing
C1/3370  ... $C0/$C3 := the ptrA/ptrB triples ($1CA2/$1CC3 + 3*slot); $A5 := $96DE,X; JSR C1/00A7
C1/33AA  CC/FB91[$974A] -> $86 = 0x0C / 0x04 (+ STZ $9829,X, C1/33BD) / 0x18;  $82 = $86 + $A9
C1/33D5  X = CC/F82F[slot]; 8 x  $97D9,X := [$C0],Y             the POSES row
C1/33F1  8 x  $9799,X := CC/FBCE[[$C3], $86+k]                  the DWELLS row (facing-independent index)
C1/3407  LDX $9C / DEC $9694,X / BNE ->next                     THE TIMER: one tick per VISIT
C1/340E  Y = slot*8 + $969C,X; if $9799[Y] == 0: $969C,X := 0, Y = slot*8     (C1/341A-3424)
C1/3425  $967B := $97D9[Y]; $9679 := cmb
C1/3434  LDA $9912 / BEQ ->publish
C1/3439  DEC A / BNE ->defer; LDA $967C,X / BNE ->defer; cmb == 4 -> publish; cmb >= 6 -> publish   (the exception)
C1/344C  INC $9922,X (= $9925+slot) / INC $9694,X / BRA ->next  DEFER: flag it, re-arm the timer by one
C1/3456  LDA $967C,X / CMP #03 / BCS ->advance; JSR C1/1C4A      type 3 publishes nothing
   C1/1C55  STZ $9922,X; $967B == $9907,X -> RTS; $9907,X := $967B; build + upload; C1/1F75 INC $9912
C1/3462  INC $969C,X; == 4 -> 0 (C1/346C)
C1/3471  Y = slot*8 + cursor; $9799[Y] == 0 -> cursor := 0 (C1/3480)
C1/3489  $9694,X := $9799[Y]                                      reload = the dwell of the NEW cursor
```

Three things in there that the stateless replay could not do, each measured:

* **The reload is `dwells[c+1]`, not `dwells[c]`.** forest3's Hetake in mode 0E (class 1 → base 0x04) has
  row poses `00 01 02 01` / dwells `03 03 02 02`: f244 publishes 00 and reloads 3 (= dwells[1]); f475
  publishes 01, reloads 2; f495 publishes 02, reloads 2; f515 publishes 01, reloads 3 (= dwells[0]).
* **A rebuild keeps the cursor.** bat2 f25: the Beetle's `$96DE[1]` := 3 (the mover aim, C1:37F2); f38, its
  next visit: `$96E9` := 3, the row is rebuilt to `09 0A 0B 0A` (C1:33E4), and the publish is **0B — the
  new row's [2]**, because the cursor stood at 2.
* **The upload budget defers, and the deferred seat goes first next time.** forest_holdleft_pre f339, the
  first dispatch after the entrance: all three Hetakes commit FF → request (`$974A` := 00 00 04, C1:336A),
  build their rows, and slots 0 and 1 publish 01 (`$9912` 1, then 2 — slot 1 = cmb 4 is the exception);
  slot 2's timer expires too but `$9912` == 2 → `$9694` := 03 03 **01** with writer **C1:3454** (the
  defer's INC) and `$9927` set; f346, slot 2 is first in the roster and publishes 07.

`$9907`'s boot value is the record's byte +4 (`CC/EA9E LDA $29C3,X / STA $A4DC,Y` → `C1/34BA` → `C1/1C66`),
shown from the boot publish (f244) until the first walk (f339): pose 05 for the Hetakes, then 01. `$974A`
boots as FF (`CC/E34E`), which is what makes the first visit commit unconditionally.

### What the port does with it

`src/battle.c`: `battleSchedAdvance` (the accumulator, now setting `schedWalk` on the two-of-five frames
and frozen with `$A013`), `battleIdleWalk` (the roster, the change detector + row rebuild, the tick with
the `$9912` budget and the C1/3441 exception, the deferral flags), `CtBattleIdleSeed` / `CtBattleIdleStep`
(the RENDERWRAM sweep drives the real walk from a seed's own bytes). The host holds `mvReq` (`$5E0D`),
`mvMode` (`$974A`, boots FF), `enFacingPub` (`$96E9`), the rows, `idleTimer`/`idleCur`/`idlePub`/`idleDefer`.
`battleMoveArm` writes the REQUEST (mode 0 included — C1/9833/9891 are unconditional, forest3 f243
`$5E0D[2]` := 00 → f244 committed), class 0's hand-back and the settle's save read/write the request byte,
the mode-1 turn gate reads it (`C1/84A3 LDA $5E0A,X`). `src/field.c`: the generic enemy render and the
death branch read the PUBLISHED pose (`idlePub`, or the seed's `$9907`); the `elapsed/10` cycle is gone.

Graded: `field-battle-idle-dwell-rate` now drives the walk from `bat2.raw.wram` (= bat2_lake f0, byte-
identical) and matches the oracle's `$9907/$969C/$9694` diff **frame for frame** through f35 (f5 slot2 00,
f13 grp2, f18 slot1 01, f20 slot5 01, f25 slot2 01, f33 grp2); `field-battle-idle-walk-boot` pins the live
boot on forest_pre — the f339 seven-event sequence, the f346 head-of-roster, and the walk-window
reload/rebuild rules. 8 mutations, 8 killed (one moved between the two checks because the bat2 rows never
reach the zero-dwell rule).

Re-pinned, with the reason per group: (a) the RENDERWRAM seeds' enemy pose is now the seed's `$9907`
(bat2/bat4/bat8/bat9/no-gato/lucca/victory-arm/hold2 — the old value was the stateless cycle sampled at
f400); (b) the pre-combat pose is the record's boot pose until the first walk (slot3 f100); (c) the
dissolve keeps the pose published at death (ambush f362); (d) the live gato/forest timelines shift by the
commit lag — GATO's approach starts 11 frames after the arm instead of 0 (f159 → f170, the group-2 visit),
the forest fights' 5th enemy action moves 15 frames (the wander's commit and no-reset on a same-mode
re-arm change the walk, hence the positions, hence the think/scan timing) — victory-settle/hold,
hud-name-after-victory, status-render, hetake-attack-sfx, hurt-flash, stale-deadflag. The first four forest
actions are frame-identical before and after.

### What is NOT in this port (each named, none guessed)

* **The PC group** (`$A4` == 0, `C1/2DA9` → `C1/2F13`): the PC idle machine with `$9689` timers; the port's PC
  bob is still its own model.
* **`$5E15` → `$9752`** (the move TARGET) is still written at the arm; the ROM copies it at the mode
  machine's pass (`C1/3742-374A`), before any handler reads it — a cadence collapse of at most one pass.
* **The handler scratch on a mode change**: the port resets `mvSegCad/mvActive/mvPhase/mvWalking/mvStopped`
  when the LIVE mode changes (moved here from the arm); the ROM does not reset `$98A7`/`$9737`/`$9819` at
  the commit — a new mode inherits an in-flight segment. Not measured on any capture; recorded, not modelled.
* **Dead enemies keep being visited** (`$96F8` has no clearer during the fight) — the port stops drawing
  them and latches the death pose; whether the ROM's dissolve tracks later publishes is not measured.
* **The phase**: the five-frame phase at combat start, the 3-frame resume after an action span, and the
  queue slips are all capture-dependent; sequences are exact, frames are within one period.

### THE PC GROUP (`$A4 == 0`): `C1/2DA9` → `C1/2F1E`, DERIVED AND PORTED (2026-09-09, same day)

The dispatch's fourth arm. `C1/2D9F`: `STZ $9912`, `LDA $A4 / BEQ` → `C1/2DA9` builds the PC roster from
`$9922..$9924` exactly like the enemy groups (the first set flag goes first and is cleared), and
`C1/2DFD..2F1E` walks the three seats:

```
C1/2E09  LDA $96F5,X / BEQ                absent -> skip
C1/2E11  LDA $9912 / CMP #01 / BNE        ONLY $9912 == 1 defers: INC $9922,X, no timer touch (the enemy walk's
                                          C1/3441 exception does not exist here)
C1/2E20  DEC $9689,X / BEQ                the dwell timer: one tick per visit; boot 1,2,3 (C1/34CD-34D7)
C1/2E28  JSR C1/2F97                      the WINDOW: the CC/F77F status scan -> $A440 (3 = idle; dead -> 08 via the
                                          first record; $A09B has no writer in the battle banks); dead ($5E4A bit7,
                                          C1/2FC4) or an icon sub-state change (C1/301A vs $A44C) -> $968C := FF
C1/2E2B  JSR C1/2F22                      the RE-AIM latch $A098 (below)
C1/2E2E  JSR C1/308C                      $A441 -> $A44C, and the icon handlers (($3216,X); the status render's)
C1/2E31  $A440 == 3 && ($99CF | $99D0) -> window 0x2B, $968C := FF   (flags the boot wipe zeroes; C1/1076-1086
                                          consumes them; C3/3D1C sets $99D0 -- not reached in any capture here)
C1/2E4C  ptrA/ptrB ($1C99/$1CBA + 3*seat) + window*4; $A5 := $96DB,X; C1/00A7 -> $82 = facing*stride
C1/2EA6  $968C += 1; dwell := ptrB[window*4 + idx]; 0 -> idx := 0 and re-read; $9689 := max(1, dwell / 5)
                                          (C1/00D7 = the hardware divide) -- the entry is advanced BEFORE the read
C1/2EDA  pose := ptrA[window*4 + $82 + idx] -> $967B
C1/2EF2  LDA $A440 / CMP $A103,X / BNE -> STA $A103,X, publish       the window changed
C1/2F01  CMP #03 / BNE -> publish                                      a status window always publishes
C1/2F05  LDA $80 / BNE -> publish                                      the facing just changed
C1/2F09  LDA $95D5 / CMP $94 / BNE -> skip                             a WAITING seat never re-publishes its idle
C1/2F10  JSR C1/1C4A                       STZ $9922,X; $967B != $9907,X -> publish + upload, INC $9912
```

**The re-aim latch `C1/2F22`.** `$A098` (writers: only `C1/2F3A` and `C1/2F41`) is a round-robin: a seat whose
timer expired and which is NOT the latched seat arms itself (`seat | 0x80`) if nothing is armed; the latched seat
clears the bit, and — unless dead (`$5E4A` bit7) or `$5E4B & 2` — re-aims its facing at the nearest enemy:
`#02 -> $986E`, `JSR C1/0045 (#05)` = `C1/2986` (the nearest scan the entry pass `C1/350F` also uses), `$99DA,X :=
$9873`, `C1/0222` atan2, `C0/F700` → `$96DB,X`; a changed facing sets `$80`, which forces the publish. So the PCs
re-face the moving enemies one seat per expiry, in turn.

**Measured, three captures + the boot.**

* bat3 (`$95D5` = 0): Crono publishes at EVERY PC visit — f6 33, f19 C8, f26 33, f39 C9, f46 33, f59 C8 … — his
  facing-2 idle row `C8 33 C9 33` at entries 3,0,1,2,3,0 with the timer reloading 1 (dwell 5 / 5); Marle's entry
  advances at f19, f39, f59 (`$968C[1]` 1→2→3→0) and her `$9907[1]` = DD never moves; `$A098` walks 81 → 80 → 00 →
  81 exactly as the latch code says; `$9922[1]` is set on every visit Crono uploaded first and consumed the next.
* bat2 (Crono facing 3, Marle waiting): f3 roster 1,0 — Marle's `$A103[1]` is 0 → latch + DA (C1:1C69), Crono
  DEFERRED (C1:2E1B, his entry stays 2); f10 Crono 37; f23 Marle entry 1→2 unpublished, Crono CA; f30 37; f43 CB.
* bat9 (three PCs): f9 all three advance and nobody uploads (Crono's entry 3 = 33 = what `$9907` held); f16
  Crono uploads C8 → Lucca AND Frog deferred (`$9922` = 00 01 01); f22 roster 1,2,0 — the builder consumes
  `$9923` only, Frog's flag survives to the next visit.
* the boot (holdleft): f325 the first PC dispatch — Crono's window 3 ≠ `$A103[0]` (0) → latch, and NO upload
  because `$9907[0]` already holds 33, the entrance ladder's last publish (entry 3 of window 0x30) which equals
  his idle entry [1]; f329 `$95D5` := 0; f331 C9; f344 Marle first (deferred at f331) publishes DD, Crono
  deferred with his timer untouched. gato_wait (Crono facing 0): the ladder ends on 4F = idle [1] again, f377
  latch without upload, f383 C7; after his attack the script's own re-arm publishes idle entry 0 (C6, f506) and
  that is what a waiting Crono shows until his next turn — `$9907` is ONE byte per seat and every publisher
  writes it.

**The port.** `battle.c battlePcWalk` (called by `battleIdleWalk` for group 0): the roster, the `$9912 == 1`
defer, the timer, `CtBattleStatusScan` as `C1/2F97`, the `$A098` latch with `battlePcAimFacing` (the entry law),
the `max(1, dwell/5)` reload through `CtBattlePcWinDwell`, `CtBattlePcWinPose` at the advanced entry, the
four-way publish rule, `pcPub` = `$9907,X`. The other publishers the port models write `pcPub` too: the pose
ladder (fallen / victory), the anim script's PC publishes (`ascPmPose`), and the PC attack's `retEnd+19` idle
re-arm (entry 0). `battleCombatStart` seeds `pcPub` with the entrance window's entry 3 — the port's entrance is a
baked morph layer; the ladder machine `CtBattlePcLadderTick` sits after the intro gate and never steps
pre-combat (found today; recorded, not changed). `field.c battleStanceRender` draws `pcPub` for every seat once
`$A103` is latched (the seed path: the seed's own `$9907` when its `$A103` is non-zero); the Crono-only bob layer
`battleCronoIdleRender` and its 105-frame captured fixture `ctCronoIdleBob` (state runs 4,13,7,13,7,13 — the walk's
13/7 visit gaps — then a 13/35 tail where the capture paused for actions, replayed mod 105) are retired.

Graded: the bat2 seed sweep now carries the PC lines and matches the oracle's `$9907[0..2]/$968C/$9922/$A098`
columns frame for frame (f3/f10/f23/f30/f43); the forest boot check carries the first PC visits (the seeded 33
needs no upload, Marle's latch publish, the active seat cycling, the waiting seat advancing unpublished).
13 mutations across the two checks. 24 more checks re-pinned: every one a battleshot or seed render whose PC
pose is now the walk's publish (a phase or history difference against the fixture: verified on GATO f540, forest
f172 and the Hetake-attack shot by old/new pixel crops — the same Crono, a different entry of his idle row).

Named, not ported: the 0x2B window's setter (`C3/3D1C`, the results path) and the `C1/1076` consumer with its
two sound posts; the icon sub-state handlers (`C1/308C` → `($3216,X)` — the status render's own port); `$A09B`
(no battle-bank writer, stays 0); the attack script's other PC publishes stay the PC bake's (only the idle
re-arm at `retEnd+19` is mirrored, measured as entry 0 on gato_wait f506).

