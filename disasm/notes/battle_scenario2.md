# BATTLE SCENARIO 2 — the FOREST fight (Guardia Forest, scene 19, 3× Hetake) end-to-end RE

The first **multi-enemy** battle (everything prior was single-enemy GATO): Crono+Marle vs **3× Hetake**
(enemy id **0x5E**, 14 HP each) in loc 19. Derives everything the wiring pass needs — the enemy sprites
(T1), the multi-enemy HUD strip (T2), target-select nav across 3 slots (T3), and the fight's numbers for a
regression pin (T4). All ROM/oracle-only, zero invention; boundaries called out.

**States (gitignored, local):** `saves/battle_re/forest_battle_3hetake.state` (in-battle, Crono command menu
open, 3 Hetakes alive, both PCs ATB-ready), `saves/battle_re/forest_holdleft_pre.state` (pre-battle field;
hold LEFT = 0x40 to trigger). Provenance + conversion in [[battle_dissolve]] PART 3.

**Method/decode** (as [[battle_stance]]/[[battle_hud_render]]/[[battle_actionfx]]): `--peek` dumps fb+`.wram`+
`.vram`+`.oam`(incl CGRAM); `--monitor "0700:512,0900:32"` = the live OAM shadow `$7E0700` low + `$7E0900`
high (strip the `*` change-markers). fb 512×480 doubled, true px `(x,y)=fb(2x,2y)` bytes `[pad,B,G,R]`; an OBJ
px at OAM `(x,y)` → fb `(x, y+8)`; OBJ 16×16, 4bpp, CGRAM `128+pal*16+v`, `(v<<3)|(v>>2)`. Enemy palette =
**pal1**. `dis.py <flat>` (HiROM flat = `(bank−0xC0)*0x10000+addr`, e.g. C1/176C → `0x1176C`). Peek/monitor
frame-timeline caveat applies (peek A-edge phase-misregisters; monitor+disasm authoritative for input/logic).

---

## T1 — HETAKE SPRITES (idle · entrance · attack), baked → `src/data/battle_hetake.h`

### T1.1 The three enemies: same type, DISTINCT VRAM blocks, INDEPENDENT idle streams
The 3 Hetakes render as three pal1 clusters in the `$7E0700` shadow (in-battle, settled), each a 4-cell 2×2
(16×16 cells), at three battle-formation anchors (min OAM corner):

| slot | block ($5FAD+slot·0x80) | tile block | idle anchor | assembly (rel anchor, settled snapshot) |
|---|---|---|---|---|
| 0 (upper-mid) | blk3 $5FAD | 0x40 | **(104,35)** | `040@(16,0) 042@(0,0) 044@(17,16) 046@(1,16)` |
| 1 (left) | blk4 $602D | 0x48 | **(72,67)** | `048@(16,0) 04A@(0,0) 04C@(17,16) 04E@(1,16)` |
| 2 (lower-right) | blk5 $60AD | 0x60 | **(152,102)** | `060@(16,13) 062@(0,13) 064@(16,0) 066@(0,0)` |

Each slot has its **own OBJ VRAM block** (0x40 / 0x48 / 0x60) — NOT one shared sprite. Slot0 and slot1 tile
CONTENT is **byte-identical** this frame (verified `blk(0x40)==blk(0x48)` all 4 cells); slot2 differs (386 vs
356 nonzero bytes) — it is the **same enemy at a different idle-anim phase** (hence its different cell layout:
cap on top `064/066@dy0`, stalk `060/062@dy13`). So the three stream their idle animation **independently**.

### T1.2 Idle = OAM-STATIC (no positional bob); the tile CONTENT streams (BOUNDARY)
OAM-shadow monitor (forest_battle HOLD, f0–f84 clean idle window): the three Hetake clusters' anchors +
assemblies are **unchanged frame-to-frame** (104,35 / 72,67 / 152,102) — unlike Crono (the active PC bobs) or
GATO (foot-bob+claw-swap, [[battle_stance]] ADD4). So a waiting/idle Hetake **does not move in OAM**. Its
tile PIXELS still stream (the C1/CC anim engine + `FD:E170` streamer, [[battle_stance]] S4 boundary) — the
idle bake is the settled snapshot; the per-frame pixel stream is the documented boundary.

### T1.3 Entrance = enemies APPEAR SETTLED (no morph)
Boot from `forest_holdleft_pre` (hold LEFT): staging fires **f234** (`$AF0D`=`$AF02`=`5E 5E 5E FF…`); peeks at
f245/f290/f335 show all three already at their formation anchors ((105,35)/(73,67)/(151,99)) — **identical
across the whole enemy-name→command window**. So the enemies **do NOT weapon-draw/morph** (only Crono+Marle
do, [[battle_stance]] ADD3); they are drawn settled from ~f245 (just after staging). Matches GATO (static
enemy). The only entry motion = the HUD strip slide + the PC weapon-draw. (`$95D5` boot timeline: staging
f234 → `$95D5`=0xFF **enemy-name phase f237–f328** → `$95D5`=0 **command phase f329**.)

### T1.4 The Hetake ATTACK — a LEAP ARC + spore-burst pose (byte-exact keyframe + measured trajectory)
In an ACTIVE-mode HOLD run a Hetake (slot0) acts (its `$AFAB` fills by f48; the swing animates **f85–f147**,
~62 render frames). The OAM-shadow trajectory (anchor − home (104,35)):

```
 f85..f100 RISE/ARC:  (0,-15)(2,-17)(4,-21)(6,-22)(8,-24)(10,-26 peak)(12,-25)(14,-25)(16,-25)(18,-24)
                      (20,-22)(22,-20)(24,-17)(26,-13)(28,-11)   -- rises to y-peak −26, drifts right +28
 f104..f111 HOLD:     (27,4)(29,4)                                -- lands displaced down-right, held
 f134..f140 RETURN:   (24,10)(20,5)(16,3)(12,2)(8,4)(4,7)(0,13)  -- arcs back toward home
 f147 HOME:           (0,0)                                       -- snaps to idle
```
The **attack pose** (rep f90, rendered) is a distinct **spore-burst lunge morph** — the cap tilts and a bright
spore/mouth opens center; assembly `040@(16,0) 042@(0,0) 044@(16,16) 046@(0,16)` in slot0's block (own
streamed tiles, differ from idle). The adjacent PC (Crono) **flinches ~f119–130**; **4 damage** applied to
Crono later (T4.3). Slots B/C use the **same mechanism** from their own home anchors (leap+spore); only
slot0's arc was densely captured. Per-frame within-pose pixel stream = the anim-engine BOUNDARY.

### T1.5 The target cursor / parked slots (tile 0x006 → 0x000), see T3.2
OAM #0–3 (tile **0x006**, pal1, **prio3**, mirror-quad, ~32/128 nonzero px = near-blank) are the **parked
target-cursor slots**; on target-select they become tile **0x000** (the pointing hand). Baked as
`ctHetakeCursor`.

---

## T2 — MULTI-ENEMY HUD STRIP (decoded cell-by-cell)

The strip mechanics are exactly [[battle_hud_render]] (one 0x7800 tilemap, BG1 chrome + BG3 glyphs, party
box, ATB bars, queue-gated command boxes). The **multi-enemy specifics**:

### T2.1 The enemy-name box shows ONE name — the first-alive enemy's — NOT a list/count
Enemy-name phase (`$95D5`==0xFF, boot f237–f328), strip box0 r2 decoded byte-exact:
`100 1A7 1BE 1CD 1BA 1C4 1BE 1EF` → cols 1–6 = **"Hetake"** (codes A7/BE/CD/BA/C4/BE = H-e-t-a-k-e) + pad
**0x1EF** (the CC/6500 name-pad, same as GATO's "Gato"). So with 3 identical enemies it is a **single 6-char
name**, from `$CC:6500 + id*11` (id 0x5E → CC/690A = "Hetake"), NOT "Hetake ×3" and NOT a 3-line list. The
name is the **first-alive enemy's** (no target exists yet in this phase). Whether a MIXED pack cycles/lists is
untestable with 3 identical enemies = **OPEN**.

### T2.2 Command phase = 3-box (both PCs ready); party box identical to GATO
Once a PC is active (`$95D5`≠0xFF, f329), the strip re-lays-out to the command layout ([[battle_hud_render]]
ADD3 §D). In `forest_battle_3hetake` **both** PCs are ATB-ready+enqueued (`$A6D9`[0,1] valid) so **both**
command boxes populate. Full strip map (`bstate.py … map`):
```
r1 |@@                #HH##HH#######|   box0 hand cursor (Crono active) + party HP/MP headers
r2 |@@Att.  Att. Crono# 70: 8[~~~~[#|   box0 Att + box1 Att + party: Crono 70/70 8/8, full ATB bar
r4 |##Tech  Tech Marle# 65:12[~~~~[#|   Tech/Tech + Marle 65/65 12/12
r6 |##Item  Item ###################|   Item/Item
```
box0=Crono cmd (hand at `$95DC`), box1=Marle cmd (queue-gated, populated here), box2=party. **seat2 row6 =
blank** (2-member party). The **party box is byte-identical to GATO's** (Crono seat0 pal2, Marle seat1 pal3,
HP/MP digits, end-capped ATB bars). **Enemies have NO HP/ATB cells** anywhere in the strip. So the ONLY
multi-enemy HUD difference vs GATO is T2.1 (which enemy name shows) + that the enemy-name box is a plain
single name; the 3-box command layout is the standard both-ready case.

---

## T3 — TARGET-SELECT nav across 3 slots (byte-exact from ROM + oracle)

Enter via Att-confirm (**C1/129C**, `dis.py 11290`): `LDA #07 STA $960D` (`$960D`=7 = the **cursor's own
battle-obj index**, a constant — NOT the target; corrects [[battle_actionfx]] P5.3's "target=$960D"),
`STZ $9615`, `JSR C1/1F79` (cursor builder), `INC $9609` (→ **target-select state**). `$9614` (the selected
slot) is **not** set here → it resolves to the **first alive slot 0** (observed initial `$9614`=0, cursor on
Hetake A).

### T3.1 The selected slot = `$9614` (0/1/2); nav is a cyclic list, skipping dead
Peek-verified persistent selector (peek couples wram+the cursor render): `$9614`=0/1/2 ↔ cursor on Hetake
A/B/C. (A transient monitor byte `$9618` is NOT the selector — it reads 0 in every peek.) **Nav handler
C1/176C–179C** (`dis.py 11740`), byte-exact:
```
NEXT  (DOWN 0x20 / RIGHT 0x80):  INC $9614; if ==0x0B → 0;  X=$9614; if $99C0,X<0 (BMI) loop; else $A62D=$99C0,X
PREV  (UP 0x10 / LEFT 0x40):     DEC $9614; if <0    →0x0A; X=$9614; if $99C0,X<0 (BMI) loop; else $A62D=$99C0,X
```
- **DOWN and RIGHT both = +1; UP and LEFT both = −1** (all four directions cycle the same linear enemy list;
  peek-confirmed: DOWN→slot1, RIGHT→slot1, UP→slot2(wrap), LEFT→slot2(wrap)). Order = **formation/slot order**
  (0=A→1=B→2=C), NOT screen geometry. Writers: INC **C1:176F**, wrap-to-0 **C1:177A**, DEC **C1:1789**.
- **Cyclic over up-to-11 slots** (0..0x0A), **skipping any slot with `$99C0,X` bit7 set** — this IS the
  retarget/skip-dead rule: a dead enemy's target-table entry goes negative, so nav auto-skips it and the
  cycle wraps through only the alive enemies. (`$99C0`/`$A62D` = the per-slot target table, built on
  target-select entry by **C1/1F79**; it is transient outside the state.)

### T3.2 The cursor OBJ + per-slot position (baked)
Cursor = OBJ tile **0x000, pal1, prio3, 16×16** (repurposes the parked foot-quad OAM #0–3; #1–3 park
off-screen x≈−100..−108, only #0 shows). Position = **enemy anchorX, anchorY + ~17–20** (per-slot, baked):
| slot | selected enemy | cursor (x,y) |
|---|---|---|
| 0 | Hetake A (104,35) | **(104,55)** |
| 1 | Hetake B (72,67) | **(72,87)** |
| 2 | Hetake C (152,102) | **(152,119)** |

### T3.3 Confirm / cancel (byte-verified)
- **A = DISPATCH on the selected slot:** `$9609`→0, `$95D5`→0xFF(dispatch, then next actor). Confirmed by a
  kill: A(Att)+DOWN(slot1)+A → **Hetake B (blk4 $602D) HP 14→0** (the HP write **lags to mash-f194**, long
  after the confirm — a settle < that reads HP unchanged; use ≥ ~170 f). $95D5→1 (Marle) right after confirm.
- **B = CANCEL:** `$9609`→0 but `$95D5` **stays** (back to the command menu, Crono still choosing). Identical
  to GATO ([[battle_actionfx]] P5.1). Confirm is **mandatory even for one target** — two A-edges per turn
  (A=Att→target-select, A=confirm→dispatch).

---

## T4 — THE FIGHT'S NUMBERS (for a future regression pin)

### T4.1 Staging + stats (ROM byte-exact)
- **Staged enemy recs `$29BF + 12·slot`** (field→battle, [[battle_entry]]): slot0 `07 03 08 0F 05 5E …`, slot1
  `05 05 08 0F 05 5E …`, slot2 `0A 07 08 0F 04 5E …` (+5 = id 0x5E), slot3 = FF (empty). `$AF02`=`$AF0D`=
  `5E 5E 5E FF FF FF FF FF`. **Enemy block index = 3 + formation-slot** → blocks **$5FAD/$602D/$60AD** (id 94,
  HP 14/14 each). PC staged recs `$29B0+5·seat`: seat0 `0A 04 00 07 00`, seat1 `07 09 0F 00 00`.
- **Enemy stats: ROM `$CC:4700 + id·23`** ([[battle_boot]]). Hetake id 0x5E → **$CC:4F72** =
  `0E 00 01 00 00 00 00 00 0A 05 02 08 06 32 04 7F 04 04 04 04 00 00 00` — HP `+0/+1`=**14**; block+0x37 stat
  bytes = rec+8..19 = `0A 05 02 08 06 32 04 7F 04 04 04 04` (byte-exact vs WRAM blk3). (Compare GATO id 0x92
  → CC/541E, HP 76.) The record's block+0x3E=0x7F is a fixed field (identical GATO/Hetake), **not** the
  physical DEF — the DEF byte within the stat bytes is unpinned here (see OPEN).

### T4.2 Reward on death: 3 XP + 16 + 1 item per Hetake (ROM + live-accumulated)
Kill bookkeeping **FD/ABA2** (`dis.py 3DABA2`) reads reward table **`$CC:5E00 + id·7`** → Hetake id 0x5E =
**$CC:6092** = `03 00 10 00 BD 00 01` and accumulates `$B28C += rec+0` (XP), `$B2A5 += rec+2`, `$B2DB += rec+6`
(item). Live-verified over the victory mash: each death adds **XP +3, $B2A5 +16, item +1** (`$B28C` 0→3→6→9,
`$B2A5` 0→16→32→48, `$B2DB`=3). (rec+4 = 0x00BD=189 = a further field — TP/GP semantics = battle_loop detail.)

### T4.3 Damage (measured; the formula check)
- **Crono normal → Hetake = 16** (damage-number popup "16" @ (80,80), captured pre-apply with the target still
  at 14 HP; one-shots the 14-HP Hetake). Formula inputs ([[battle_loop]] S3, `dmg = coef×(A36×12+A43×5)/18×
  (256−DEF)/256`): Crono block+0x36=**5**, +0x43=**3** (base 5·12+3·5 = 75). (Crono→GATO was 17,
  [[battle_actionfx]] P1 — the 16 vs 17 delta is the target DEF factor.)
- **Hetake normal → Crono = 4** (Crono HP 70→66; def 16). One Hetake acted in the window.
- **Marle** block+0x36=**2**, +0x43=**3** (base 39 < Crono's 75) → her normal is **< 16** (would not one-shot a
  14-HP Hetake). Her exact value is UNMEASURED — the isolated capture was input-timing-unreliable, and the
  mash "36" popup is a **Crono critical** (36 ≈ 2×base, not Marle's ~half-of-Crono) = **OPEN**.

### T4.4 The victory signature (deterministic, for the regression pin)
Mash-A on `forest_battle_3hetake` wins cleanly. Monitor (`101EC`,`B28C`,`B2A5`,`AF02`), mash `100:8,0:10`×N:
```
deaths (mash timeline):  slot0 $AF02→FF f168 · slot1 f365 · slot2 f719 (→ AF02 = FF FF FF FF FF FF)
rewards at victory:      XP $B28C = 9 · $B2A5 = 48 · item $B2DB = 3   (= 3×(3,16,1))
party at victory:        Crono 70/70, Marle 65/65 (undamaged in the clean mash) · $95D5=0xFF, $9609=0
```
Note **`$7F01EC` (outcome) stays 0** even after all die — the win is signalled by the **empty alive-list**
(`$AF02`=all FF), not that byte (the victory/spoils sequence + field return follow). A wiring-pass end-to-end
check can pin: 3× id-0x5E/14-HP staged; Crono 16 on a Hetake; per-kill 3 XP/16/1 item; total 9/48/3;
alive-list drains to all-FF.

---

## THE BAKE — `src/data/battle_hetake.h` (generator `tools/_gen_battlehetake.py`, gitignored)

Baked (T1): `ctHetakeIdle[3]` (the 3 idle clusters: anchor + 4-cell OAM assembly), `ctHetakeObjTiles[0x80][32]`
(OBJ tiles 0x00..0x7F 4bpp — cursor 0x00, foot 0x06, the 3 idle blocks 0x40..0x77, from the in-battle boot
upload), `ctHetakeObjPal[16]` (pal1), `ctHetakeAttackKf`+`ctHetakeAttackTiles[8]` (the spore-lunge keyframe
tiles 0x40..0x47 @ f90) + `ctHetakeAtkTraj[25]` (the leap arc), `ctHetakeCursor[3]` (tile-0x000 cursor per
slot). NOT baked: the per-frame idle/attack tile STREAM (anim-engine boundary) and the formation-anchor ROM
math (anchors baked from OAM).

**Verify** (`CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python tools/_gen_battlehetake.py --verify --emit`):
reconstructs the 3 idle Hetakes + the attack keyframe from the baked tiles/assembly/pal and pixel-diffs vs the
oracle fb:
```
slot0(0x40) tile content == slot1(0x48): True   (same enemy, distinct VRAM blocks)
Hetake slot0 anchor(104,35): exact(d=0)=629/629  mismatch=0   byte-EXACT (unoccluded)
Hetake slot1 anchor(72,67):  exact(d=0)=598/629  mismatch=31  residual = forest-FG foliage over sprite (fb shows green, verified)
Hetake slot2 anchor(152,102):exact(d=0)=520/602  mismatch=79  residual = forest-FG foliage over sprite
Hetake ATTACK kf f90:        exact(d=0)=671/671   mismatch=0   byte-EXACT
target cursor per slot:      [(104,55),(72,87),(152,119)]
```
slot0 (top, unoccluded) and the attack keyframe (up high) are **byte-EXACT**; slot1/slot2 residuals are the
**field FOREGROUND foliage drawn over the prio-2 enemy OBJ** — verified: at every mismatch the fb shows dark
forest green (e.g. (41,41,8)) where the mushroom pixel is (the sprite pixels themselves are proven correct by
slot0 being byte-exact and slot1's content being byte-identical to slot0). This is a render-compositing effect
(FG over sprite), not a bake error — the wiring pass must draw the enemy OBJ **behind** the field FG pass.

---

## OPEN questions
- **Marle's exact normal damage** (T4.3): isolated capture input-unreliable; the mash "36" is a Crono crit.
  Needs a clean single-Marle attack (monitor `$95D5`→1 then a well-timed A/A) — bounded < 16.
- **Physical-DEF byte in the stat record** (T4.1/T4.3): the record's block+0x3E=0x7F is a fixed field, not
  DEF; which of the stat bytes (rec+8..14 `0A 05 02 08 06 32 04`) is the physical defense the damage formula
  reads (256−DEF) is not pinned here — the full formula + coef live in [[battle_loop]] S3; the MEASURED
  Crono=16 is the regression anchor.
- **Mixed-pack enemy-name box** (T2.1): with 3 identical Hetakes, whether the box lists/cycles multiple
  DIFFERENT enemy types (vs the single first-alive name shown here) is untestable in this fight.
- **Per-frame idle/attack tile STREAM** (T1.2/T1.4): the C1/CC anim engine + `FD:E170` streamer
  ([[battle_stance]] S4) — only rep-frame tiles baked; slots B/C attack captured only structurally (same
  leap+spore mechanism as slot0).
- **Formation-anchor ROM math** (T1.1): the seat/slot→screen-anchor mapping is unpinned; anchors baked from
  the OAM shadow.
- **`$9614` init site** (T3): C1/129C leaves it as-is → first alive slot 0 (observed); the exact zero/init
  path (cursor builder C1/1F79 vs leftover) not traced.
- **`$99C0`/`$A62D` target-table content** during nav (T3.1): the skip-dead RULE is disasm-proven (BMI on
  `$99C0,X`), but the table is transient outside target-select — its exact fill (C1/1F79) not dumped mid-state.

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/…/e5608df4-…/scratchpad/`
- States/dumps: `hk_f0.raw*` (settled idle), `hk_atk90/105.raw*` (attack pose), `boot245/290/335.raw*`
  (entrance), `hk_ename.raw*` (enemy-name phase), `tgt_s0/s1/s2.raw*` + `het_cur0/1/2.raw*` (cursor slots),
  `kill_b.raw*`/`cd176/183/190.raw*` (Crono kill + "16" popup), `victory.raw*` (win state).
- Monitors: `hk_oamshadow.txt` (idle+attack OAM shadow), `hk_entrance.txt` (boot OAM shadow).
- Decoders: `hkrender.py` (cluster→PNG), `hkoam.py` (OAM-shadow→cluster table), `bstate.py` (battle-state),
  `hetakeA/B/C.png`, `hetakeA_atk90/105.png` (rendered sprites).
- Generator `tools/_gen_battlehetake.py` (gitignored) → `src/data/battle_hetake.h`. `ct_probe.exe` (oracle copy).

## Verify command
`CT_EXE=<scratch>/ct_probe.exe CT_SCRATCH=<scratch> python tools/_gen_battlehetake.py --verify --emit`
