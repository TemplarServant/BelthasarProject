# BATTLE — LUCCA / FROG / 3-PC expansion (the next-milestone DOSSIER)

Derives, from the ROM (`tools/dis.py`, fileoffset=`(bank−0xC0)<<16|addr`) + the LakeSnes oracle
(`ct.exe --read/--monitor/--peek`) driven on the user's two converted states, everything the PORT
needs to fight with **Lucca** and **Frog** and to stage a **3-PC party** — the stage-1..3 anim engine
+ battle machine are already GENERIC (per `battle_anim_engine.md`); what was missing is **per-PC data +
the 3-PC layout laws + the two new enemies' staging facts**. Nothing implemented; `src/` + git untouched.

**Sources (byte-cited throughout).** `bat8_lake.state` = Crono+**Lucca** vs Naga-ette×4 (Manoria organ
hall, loc $081), `bat9_lake.state` = Crono+**Lucca**+**Frog** vs Diablos×3 (Manoria corridor, loc $082) —
the ONLY 3-PC capture. Both are mid-battle command-phase (`battle_states_catalog.md` §0). Combatant index
convention: PC **seats 0/1/2**, enemies **3 + staging-slot** (NOT compacted — `battle_states_catalog.md`
§0). Per-combatant 24-bit tables are `TABLE + cmb*3`; per-seat 5-byte staging is `$29B0 + seat*5`; enemy
12-byte staging is `$29BF + slot*12`. Read recipe reused from the catalog (`convert_full.py`, donor
`gato_mid.state`). HEAD at authoring = 23d3edd.

Cross-refs: `battle_states_catalog.md` (§3 stat-derive, §5 rewards — verified here), `battle_anim_engine.md`
(the pose engine the port drives), `battle_animeng_builder.md`/`_script.md`/`_tiles.md` (the `$967B`/`$9907`/
`$9671` machinery), `battle_hud_render.md` (the 2-PC strip this extends to 3), `battle_crit.md` (the crit
selector, reused), `battle_forest_return.md` AD.3 (the `$7F03FE = 3−nFollowers` regroup).

---

## 0. TL;DR — the pinned facts

**Per-PC engine data** (live `$1C78`/`$1C57` on the oracle + ROM tables):

| PC | charId | E3 base `$1C78[cmb]` | sheet `$1C57[cmb]` | E3 stride | window `$9671` | idle asm idx (facing-dep.) | normal `$9877` | crit `$9877` |
|---|---|---|---|---|---|---|---|---|
| Crono | 0 | `$DC27B0` | `$D272C0` (ROM) | 0x28 | 0x24 | C6/4F/C7 (bat8) · 33/C8 (bat9) | **0x00** | 0x01 |
| Marle | 1 | `$DC9AD8` | (not in bat8/9) | 0x28 | 0x24 | (baked) | **0x03** | 0x0F |
| **Lucca** | 2 | **`$DC4E70`** | **`$D36B40`** (ROM) | 0x28 | 0x24 | **0x31** (bat8) · **0x39** (bat9) | **0x05** | **0x10** |
| **Frog** | 4 | **`$DC7490`** | **`$D30000`** (ROM) | 0x28 | 0x24 | **0x3B** (bat9) | **0x08** | **0x09** |

**The two `$9877` per-charId tables** (ROM, indexed by charId 0..6 = Crono/Marle/Lucca/Robo/Frog/Ayla/Magus):
- **normal** `CD/4007` = `00 03 05 06 08 0A 0C` (measured live: Crono 0x00, Lucca 0x05).
- **crit** `CD/400E` = `01 0F 10 07 09 0B 0D` (`battle_crit.md`; measured Marle 0x0F).
- variant `CD/4000` = `0E 02 04 11 0E 0E 12` (the `$9930==0` sub-branch; not the rendered normal — §1.3).

**3-PC layout laws** (bat9): party box = **3 rows** at map rows 2/4/6 (renderY 171/183/195); name palette
seat0→pal2, seats 1&2→pal3; command box _k_ at **map cols [6k..6k+5]** (seat2 → cols 12-17); bracket = 4
corner OAM (tile 0x06) around the active PC; `$7F03FE = 3−nFollowers` regroup already ported.

**New enemies**: Naga-ette 0x1E (E3 `$DD4628`, sheet `$7F8CE0`, AI `CC/9323`), Diablos 0x48 (E3 `$DD7E68`,
sheet `$7FB0A0`, AI `CC/9A43`) — full records in §3.

**The gap**: the battle **logic** is already 3-seat-clean (every loop `seat<3`, queue wrap-3, regroup
`3−nf`); the **render + baked data** are hardwired to Crono/Marle (pal 5/6, seat∈{0,1}). Full list §4.

---

## 1. LUCCA + FROG ENGINE DATA

### 1.1 — E3 assembly bases + sheet pointers (live `$1C78`/`$1C57`, the stage-3 method)

Read on the oracle from the states (`--read "wram:1C78+30,wram:1C57+30" --from bat9_lake.state`), stride 3:

```
$1C78 (E3 assembly base, 24-bit):     $1C57 (sheet source, 24-bit):
  cmb0 Crono  B0 27 DC = $DC27B0        cmb0 Crono  C0 72 D2 = $D272C0   (ROM bank D2, UNCOMPRESSED)
  cmb1 Lucca  70 4E DC = $DC4E70  ★     cmb1 Lucca  40 6B D3 = $D36B40   (ROM bank D3, UNCOMPRESSED) ★
  cmb2 Frog   90 74 DC = $DC7490  ★     cmb2 Frog   00 00 D3 = $D30000   (ROM bank D3, UNCOMPRESSED) ★
  cmb6 Diablos 68 7E DD = $DD7E68       cmb6 Diablos A0 B0 7F = $7FB0A0   ($7F = boot-DECOMPRESSED)
```

- Lucca's base `$DC4E70` is **identical in bat8 and bat9** → stable seed (confirmed cross-state).
- **PC sheets point straight at ROM** (bank D2/D3, uncompressed — matches memory "PC sheets uncompressed"):
  Crono `$D272C0`, Lucca `$D36B40`, Frog `$D30000`. First tile of each is blank (16 zero bytes — normal
  tile-0). **Enemy sheets point into `$7F`** (boot-decompressed, like GATO) — the compressed-source +
  decompressor is the standing C5 boundary (`battle_anim_engine.md` §A.5 C5); interim = bake the `$7F`
  sheet from an oracle WRAM dump per combatant.
- The E3 records hold valid cell-word data (`--read rom:DC4E70+16` = `DD 00 00 00 00 00 DC 00 FD 00 …`,
  the `[48 cell-words][12 dx,dy]` structure of `battle_anim_engine.md` ADDENDUM 1). **PC E3 stride = 0x28**
  (4 cells, per `battle_animeng_builder.md` §6 "DC/27D0 stride 0x28, 4 cells") — Lucca/Frog inherit it.

### 1.2 — the facing-window stride `$9671` (measured)

`--read "wram:9671+9" --from bat8_lake.state` (both PCs had ticked): cmb0 Crono = **0x24**, cmb1 Lucca =
**0x24**. So the per-PC facing-window stride `$82 = $86 + facing×$9671` (`battle_animeng_script.md` §3) is
**0x24 for Crono AND Lucca** (GATO's was 0x28). Frog's read 0 at rest (not yet ticked); assume 0x24 (the
PC constant) pending a Frog-tick capture. **NB two distinct strides**: `$9671` = 0x24 (E4 facing window)
vs the E3 assembly stride = 0x28 (`base + idx×0x28`) — do not conflate.

### 1.3 — the attack/crit `$9877` selector (C1/43xx) — per-charId, THREE tables

`$9877` indexes the CD/5FF0 action-script (`battle_animeng_script.md` §4). For a PC physical it is set by
the selector at `C1/43C0..4400` (disasm; writer proven live). The routine reads `X = $2980[seat]` = charId,
then branches on the crit flag `$AE9B` and `$9930`:

```
C1/435B  LDA $AE9B / BPL ->14394              ; crit? ($AE9B bit7 set = crit — battle_crit.md C1/EDB0)
C1/4360    ... CD/401F params ... LDA $CD400E,X / STA $9877   ; ** CRIT id **  (writer C1:4392)
C1/4394  LDA $9930 / BNE ->143CD              ; else NORMAL, sub-split on $9930
C1/43CD    ... CD/401A params ... LDA $CD4007,X / STA $9877   ; ** NORMAL id ** (writer C1:43FF)  ◄ the rendered melee
C1/43C4  (fall, $9930==0) LDA $CD4000,X / STA $9877           ; a variant (CD/4015 params); not the rendered normal
```

**Measured live** (`--monitor "9877:1,..." --writer`): CRONO physical → `$9877=0x00` (writer C1:43FF =
the CD/4007 branch). LUCCA physical (bat8, switched active via RIGHT then A,A) → **`$9877=0x05`**, action
freeze `$A013=1`. Both match `CD/4007[charId]` exactly. The tables (`--read rom:CD/4000+7,...`):

| charId | 0 Crono | 1 Marle | 2 **Lucca** | 3 Robo | 4 **Frog** | 5 Ayla | 6 Magus |
|---|---|---|---|---|---|---|---|
| **normal `CD/4007`** | 00 | 03 | **05** ✓meas | 06 | **08** | 0A | 0C |
| **crit `CD/400E`** | 01 | 0F | **10** | 07 | **09** | 0B | 0D |
| variant `CD/4000` | 0E | 02 | 04 | 11 | 0E | 0E | 12 |

⇒ **Lucca normal `$9877`=0x05, crit=0x10; Frog normal=0x08, crit=0x09** (Frog's normal is table-derived —
the table is validated at Crono 0x00 + Lucca 0x05; his live capture was blocked by a concurrent Diablos
action, §1.5). `battle_crit.md` already pins `CD/400E` as the crit table (writer C1:4392, `$AE9B` gate).

> **PORT BUG surfaced.** `src/battle.c:1441` computes `normalSel = (cid==1)?0x0F:0x00` — i.e. it assumes
> **everyone but Marle uses `$9877=0x00`**. That is wrong for Lucca (0x05) and Frog (0x08): the normal id is
> a **per-charId ROM table `CD/4007`**, not a Crono/Marle special-case. Fix: `normalSel = rom[CD4007 + cid]`
> (mirror the existing crit read at `battle.c:1449` which already does `rom[ROM_CRIT_ANIM_TBL + cid]` =
> `CD/400E`). This is a real bug reachable the moment a non-Crono/Marle PC swings.

### 1.4 — idle / stance assembly ladders (`$9907[cmb]` = the per-combatant assembly cache)

`$9907[cmb]` (`battle_animeng_builder.md` §2, "current assembly index, skip-rebuild cache") is the stable
per-combatant read for the live pose. `--monitor "9907:9" --from … --diff`:

- **Crono (cmb0)**: bobs **C6 ↔ 4F ↔ C7** (bat8; matches `battle_animeng_builder.md` §8 "cursor C6↔4F↔C7").
  In bat9 (different formation/facing) it reads **33 ↔ C8** — the idle indices are **facing-dependent**
  (`$96DE`), not a fixed per-char constant.
- **Lucca (cmb1)**: **static 0x31** across 180 frames in bat8; **static 0x39** in bat9. No bob — Lucca's
  battle idle is a single held pose (index shifts with facing: 0x31 vs 0x39).
- **Frog (cmb2)**: **static 0x3B** across bat9 (240f). Single held idle pose.

So the port bakes each PC's idle index set (Crono {C6,4F,C7}/{33,C8}; Lucca {0x31}; Frog {0x3B}) OR — the
mandate-preferred route — drives the shared pose-tick with the char's E3 base + facing and lets these
emerge (the tick is generic; `battle_anim_engine.md` B.3). Facing `$96DE=0` for all PCs in both captures.

### 1.5 — LUCCA's attack ladder (captured, bat8) + Frog (table-derived)

Driving Lucca's physical (bat8: idle→`RIGHT` switches active to seat1 `$95D5=1`→`A` Att `$9609=1`→`A`
target→dispatch), `--monitor "9877,8E,A013,9907:2,967B"`:

- `$9877` = **0x05**, `$A013` = 1 (freeze) from ~f152.
- **Lucca's assembly ladder** (cmb1 `$9907[1]`): `31`(idle) → `EC` → `3D` → `05` → `23` → `2A` → `A8` →
  `A9` → `AA` → `08`. Driven by `$8E` animId rewrites `9A / 80 / 02 / 0D` (the CD-script windup→strike→
  recover, `battle_animeng_script.md` §4). During her swing Crono (cmb0) stays idle C7 → the animating
  combatant is unambiguously Lucca.
- **Frog**: his live capture was blocked — bat9's Diablos fill an ATB and act (`$9877`, freeze) across
  f27-177 (a ~150f enemy animation), pre-empting the target-select window. Frog normal `$9877=0x08` stands
  from `CD/4007` (validated pattern); his idle is 0x3B. His attack ladder is the one residual to capture on
  a calmer Frog-active window (or a patched state) — sanctioned interim per the task ("pin the idle +
  position-match the rest"). **Bonus**: the interfering Diablos action was `$9877=0x10`, pose ladder
  `$967B` 26→27→28 (= its initAnimId 0x26, §3) — a free Diablos attack sample.

---

## 2. THE 3-PC LAYOUTS (bat9, the only 3-PC capture)

### 2.1 — the party box is THREE rows (the 12288-byte strip law extended)

Decoding bat9's strip tilemap (VRAM word 0x7800 = byte 0xF000, cell `(tile:10)|(pal:3)|(prio)`,
`battle_hud_render.md` Q1) confirms the dossier's **formulaic** third row (Q4 "row-y: seat k → renderY
171 + k*12, map row 2/4/6"; the 2-PC capture only ever showed 2):

```
bat9 party box (map cols 13-30), rows 2/4/6:
  r2 (seat0): "Crono"  34: 2  |====|     (WRAM Crono 34/83, MP 2/10) ✓
  r4 (seat1): "Lucca"  48:14  |====|     (Lucca 48/75, 14/14)        ✓
  r6 (seat2): "Frog "  128:17 |====|     (Frog 128/128, 17/17)       ✓ ← the third row, LIVE
```

So the **12288-byte strip law is unchanged** — the same `CtBattleHudRender` seat-loop (`field.c:1693`
`for k<3`, `row=2+2k`, `ctBattleHudRowY={…171,-,183,-,195}`) draws seat 2 at map row 6 / renderY 195 over
the existing party-box body. Only the baked map/chrome were captured 2-row; the live pass fills row 6.

### 2.2 — name palette rule (resolves `battle_hud_render.md` Q4 open)

Name-row palettes (party box cols 13-17): bat8 {Crono pal2, Lucca pal3}; bat9 {Crono pal2, Lucca pal3,
**Frog pal3**}. Since full-HP Frog (128/128) is pal3, the palette is **NOT** HP-status. Rule: **seat0 →
pal2, seats 1 & 2 → pal3** (seat0/Crono white, the rest grey). (Whether it is strictly per-seat vs
per-character-with-Crono-always-seat0 is still formally ambiguous — Crono is always seat0 here — but the
render rule `pal = seat==0 ? 2 : 3` reproduces every observed row.)

### 2.3 — the 3-PC COMMAND-box layout (bat9, driven to Frog active) — NEW

Peeked bat9 with Frog active (`$95D5=02, $9609=00, $A013=00, $95DB=00` — clean command level; queue
`$A6D9 = 00 FF 02` = Crono+Frog enqueued, Lucca not). Tile-level strip (map row 2, `tile.pal`):

```
cols: 0  1 | 2   3   4   5 | 6 | 7   8   9  10  11 |12  13 |14  15  16  17 |18 19| 20 …          bar
tiles:1FF 1FF|1A0 1CD 1CD 1E8|1FF|1CC 1EF 1EF 1EF 1EF|162 163|1A0 1CD 1CD 1E8|1FF 1FF|"34: 2"|"|====|"
       (blank)|  "Att."  box0 |sp | blank (box1)      | HAND  |  "Att." box2  |     party HP/bars
```

**The command-box law**: box _k_ spans **map cols [6k .. 6k+5]** — the **hand** at cols 6k/6k+1, the
**"Att/Tech/Item" label** at cols 6k+2..6k+5. So **seat0 → cols 0-5, seat1 → cols 6-11, seat2 → cols
12-17**. (This matches the 2-PC dossier: box0 label cols 2-5, box1 label cols 8-11 = `6·1+2`.) A box is
populated iff its seat is enqueued (`$A6D9[k]!=0xFF`, `battle_hud_render.md` ADD3 §C), and the hand marks
the **active** seat (`$95D5==k`, at `$95DC`).

**Mutual exclusion with the party names**: seat2's command box (cols 12-17) sits **on top of the party
name column** (2-PC names were cols 13-17). Observed: when seat2's box is present (Frog active/enqueued),
the party **names are dropped** and the party box shows only HP/MP/bars (cols ~19-30); when it is absent
(bat9 frame-0, Frog not yet enqueued) the names show at cols 13-17. So the render is: **draw the party
names at cols 13-17 ONLY when no seat-2 command box is drawn; otherwise names off, HP/MP/bars only.**
(The exact reflow of the HP columns 19-30 vs 20-30 is a 1-col cosmetic to pin on the byte-exact bake; the
box-column law and the name/box2 exclusion are the operative rules.)

### 2.4 — the BRACKET seat-2 origin (the old open — closed)

The selection bracket is **4 corner OAM sprites, tile 0x06**, flipped to form a box (`battle_stance.md`
ADD6; `battle_forest_return.md` B.1). Peeked with Frog active, OAM (`$7E0700` shadow) sprites 0-3:

```
spr0 (162, 92) tile06 attr32   spr1 (170, 92) attr72(Hflip)
spr2 (162,116) attr B2(Vflip)  spr3 (170,116) attr F2(HVflip)
```

⇒ **Frog's (seat2) bracket box** = corners at x{162,170}, y{92,116} (spans ~x162-178, y92-124, center
~(170,108)). Frog's staged anchor `$29B0[seat2]` (bat9) = `0A 08 0E 00 03` → computed foot
`16·relX+fineX = 16·0x0A+0x0E = 174`, `16·relY+fineY = 16·8+0 = 128`. So the **bracket-top-left → foot
offset ≈ (162−174, 92−128) = (−12, −36)**. Per `battle_forest_return.md` B.1's fix, the bracket must be
**foot-anchored** (`origin = computedAnchor + offset`), NOT a fixed absolute — so the seat-2 entry is this
offset applied to Frog's live `battlePcAnchorDelta(2)`, exactly as Crono/Marle. (Marle's captured offset
was (+0,+12), Crono's (+13,0) — Frog's is (−12,−36); each is its own pose's bracket→foot delta.)

### 2.5 — the staged per-seat records `$29B0 + 5·seat` (extents/foot, seats 0-2)

`--read "wram:29B0+15"` (5 bytes/seat = relX, relY, fineX, fineY, +4):

```
              seat0 (Crono)        seat1 (Lucca)        seat2
bat8:         05 09 0F 00 38       0A 09 00 01 AB       08 0A 0F 00 FF  (empty — 2-PC)
bat9:         03 08 0F 00 01       07 07 0E 0C 00       0A 08 0E 00 03  (Frog) ★
```

The anchor law is the already-ported `16·rel + fine (+ pose extent)` (`battlePcAnchorDelta`,
`battle_forest_return.md` PART B). Seat 2 is a first-class record — bat9 supplies the third staged record
verbatim (Frog `0A 08 0E 00 03`). Host arrays for this are already sized ≥3 (§4).

### 2.6 — target-select / queue / L-R with 3 PCs

`$A6D9` is **seat-indexed** (`A6D9[k]` = k if seat k enqueued, else 0xFF; bat9 = `00 FF 02` = Crono+Frog
ready, Lucca charging). The ready-queue **HEAD `$A6DD`** cycles on L/R, **skipping 0xFF, wrapping at 3**
(`battle_hud_render.md` ADD4 §G, handler C1/1224 `CMP #03`). **Confirmed 3-PC live**: one `RIGHT` from
Crono-active (`$95D5=0`) advanced the head past the empty seat1 straight to **seat2 → `$95D5=02`** (Frog),
bracket + box2 + hand following — the disasm-proven "cyclic over enqueued, wrap-3" running over 3 seats
for the first time (ADD4 §I flagged it 2-PC-only). Target-select (`$9609=1`, enemy cursor `$9614`) is
enemy-indexed, PC-count-independent.

---

## 3. THE NEW ENEMIES (staging / anim-set / AI / rewards — enough to STAGE + render sanely)

Full AI/anim derivation is the SEPARATE follow-up; this pins what the fights need to stage and render.

### Naga-ette (id 0x1E) — bat8, 4×

```
E3 assembly base    $DD4628        (rom:DD4628 = 04 00 05 00 F3 00 F4 00 … valid cell-words)
sheet ptr $1C57     $7F8CE0        (boot-DECOMPRESSED into $7F; interim = WRAM-dump fixture like GATO)
init animId         0x2A           (E4/F600+id*10, byte+3; GATO's 0x57 sits at the same +3 ✓)
anim-set E4/F72C    57 3C 79 2A 00 00 F0 0C 14 21   (byte+4 = 0x00 = DISSOLVE class)
AI script (CC/8B08+id*2) -> CC/9323 :  17 10 00 00 FE 02 05 05 00 00 07 FE …  (parses, FD/BA4A grammar)
staging $29BF slots 0-3:  rel(9,5)/(6,5)/(3,6)/(12,6)  fine varies  prio 05/05/07/05  flags 0x00  HP 60
reward CC/5ED2:     08 00 50 00 00 00 01   = XP 8, Gold 80, drop –, charm –, TP 1  (catalog §5 ✓)
enemy battle block $5FAD/602D/60AD/612D:  id 0x1E, HP 0x3C/0x3C  (block idx == staging slot, verified)
```

### Diablos (id 0x48) — bat9, 3×

```
E3 assembly base    $DD7E68        (bank DD, like Naga)
sheet ptr $1C57     $7FB0A0        (boot-DECOMPRESSED)
init animId         0x26           (E4/F600+0x48*10 byte+3)
anim-set E4/F8D0    6F 4C 68 26 00 00 F0 08 08 22   (byte+4 = 0x00 = DISSOLVE class)
AI script -> CC/9A43 :  17 10 00 00 FE 02 06 05 00 00 00 FE …   (near-identical melee template to Naga)
staging $29BF slots 3-5:  rel(12,8)/(7,7)/(2,8)  fine(13,9)/(6,15)/(3,15)  prio 07/0A/0A  flags 0x00  HP 50
             (slots 0-2 empty id 0xFF — the non-compacted high-slot case, combatant idx = 6/7/8)
reward CC/5FF8:     0A 00 3C 00 00 00 01   = XP 10, Gold 60, TP 1  (catalog §5 ✓)
enemy blocks $612D/61AD/622D:  id 0x48, HP 0x32/0x32
live action sample: $9877=0x10, pose ladder $967B 26->27->28  (captured incidentally in the Frog drive)
```

### The dissolve-vs-stand gate (`E4/F600 + id*10`, byte+4)

GATO 0x92 `E4/FBB4` = `7B 76 B6 57 02 00 D0 10 10 41` → **byte+4 = 0x02**; Naga & Diablos → **0x00**. GATO
(byte4=0x02) is the story NPC that PERSISTS; the wild Naga/Diablos (byte4=0x00) DISSOLVE on death — the
same persist/dissolve split `battle_forest_return.md` AD.3c gates on the staging alive-byte (GATO
alive-restore vs Hetake dead-mark). So byte+4 = the anim-set's stand/dissolve class; the epilogue path is
already ported (`fieldBattleFinish` dead-mark). AI scripts: Naga & Diablos share the opening
`17 10 00 00 FE 02 0X 05 …` (a simple-melee AI template, `FE` = the movement/opcode delimiter) — they
parse under the same FD/BA4A grammar as GATO (`CC/A77B`); full parse/derivation deferred.

---

## 4. THE 3-PC FLOW GAPS (what the third seat breaks — inspection of battle.c/field.c/ct_vm.c)

**Logic layer = already 3-seat-clean.** Every party loop in `battle.c` is `seat<3` (stageAll `:134`,
PC-block copy `:153`, target RNG in thirds `:477`, ATB enqueue `:2404`, L/R cycle `:2429`); the ready-queue
`$A6D9` is a wrap-3 skip-empty array (`battle.c:610-633`); host arrays are sized ≥3 (`battle.h:148-149`
`schedAtk[3]`, `:164 pcInTurn[3]`, `:282 mvLiveX/Y[11]`, `:297 mvExtent[11]`). Nothing in the SIM breaks.

**Render + baked-data layer = hardwired to Crono/Marle (pal 5/6, seat∈{0,1}).** The three baked tables
that only know two PCs are `ctBattleStanceObj`/`ctBattleStanceActor` (`battle_stance.h`), `ctBracketPos`
(`battle_bracket.h`), `ctBattleHudMapCmd` (`battle_hud.h`). Concrete breakages:

| # | site | breakage | fix |
|---|---|---|---|
| G1 | `battle.c:1441` | `normalSel=(cid==1)?0x0F:0x00` — normal `$9877` wrong for Lucca/Frog | read `CD/4007[cid]` (§1.3) — **a real bug, not just 3-PC** |
| G2 | `battle_stance.h:44` | `ctBattleStanceActor[2]` is **GATO**, not a 3rd PC — seat 2 aliases the enemy | add a real seat-2 PC stance record; the port's live pose-tick (§1) makes this moot once wired |
| G3 | `field.c:3084-3090` | `battleBracketRender`: `seat>=... / pc==NULL` **drops seat 2** | remove the guard, foot-anchor the bracket (§2.4 offset) |
| G4 | `field.c:1635,1652,1669` | command strip draws only box0/box1, hand only `activePc<=1` | add **box2 at cols 12-17** + hand for seat 2 (§2.3); gate names vs box2 |
| G5 | `field.c:2873,2893,2995,1108` | death/victory/entrance-morph/hurt-flash loops `<2` or `pal∈{5,6}`; `battleFlashPal(2)` returns enemy pal1 | de-hardcode from the Crono/Marle pair → per-charId (Lucca pal?, Frog pal?) |
| G6 | `field.c:1947,2879,2897,2998,3104` | `seat==0?CRONO:MARLE` ternaries — any seat≥1 treated as Marle | key extents/pose on charId, not seat |
| G7 | `battle.c:1452-1456` | span pins Crono/crit + Marle/ranged only; "other PCs → normal span" | measure Lucca/Frog attack spans (Lucca `$A013` span capturable on bat8) |

**HUD party rows (G-none)**: SAFE — `field.c:1693 for k<3` already renders seat 2 @ y195 (§2.1);
only visually unverified before bat9.

**Victory / regroup / results — SAFE.** `fieldBattleFinish` iterates `seat<3` for rewards (`field.c:3205`),
HP writeback (`:3251`), PC tile-snap from staging (`:3291`), defeat pose (`:3307`). The follower regroup
is a **3-member-party design**: `ct_vm.c:2386` finds up to 2 followers (`fol[2]`, move-modes 1&2),
`ct_vm.c:2394` seeds `$7F03FE = 3 − nFollowers` — a full 3-PC party has nf=2 → seed **1** → the barrier
reaches 3 after both followers arrive (`battle_forest_return.md` AD.3b, the `C0/0359` no-follower vs
`C0/0371` 1-follower vs `C0/037E` 2-follower ladder). Results arrays are line-indexed (`battle.h:211`), not
PC-indexed. So the return template already fits 3 members — the only unexercised path is 3 live followers
(bat9 is the state to pin it).

---

## 5. IMPLEMENTATION SPEC

### 5.1 — extraction-generator extensions (bake per-PC + per-enemy data)

1. **Per-PC engine table** (new `battle_pc_engine.h` or extend the char tables): for charId 0/1/2/4 the
   `{E3 base $1C78, sheet $1C57, E3 stride 0x28, window $9671=0x24}` from §1.1/§1.2. PC sheets are ROM
   pointers (bank D2/D3) — no decompression; enemy sheets are `$7F` WRAM-dump fixtures (interim, C5).
2. **The `$9877` tables**: bake `CD/4007` (normal, 7 bytes) alongside the already-consumed `CD/400E` (crit).
   Replace `battle.c:1441`'s hardcode with `rom[CD4007 + cid]` (fixes G1).
3. **Idle assembly indices** (interim, until the pose-tick drives them): Lucca {0x31/0x39 by facing}, Frog
   {0x3B}, per §1.4 — or, mandate-preferred, wire the shared enemy/PC pose-tick (`battle_anim_engine.md`
   B.1/B.3) with each PC's E3 base + facing and let `$9907` emerge.
4. **Enemy staging records** (§3): id → {E3 base, sheet, initAnimId (E4/F600+id*10 +3), anim-set 10 bytes,
   AI ptr CC/8B08+id*2, reward CC/5E00+id*7}. The `byte+4` stand/dissolve class feeds the epilogue.

### 5.2 — render wiring

- Drive Lucca/Frog idle + attack through the **existing generic pose engine** — the port already routes
  `$8E → E4 → $967B → E3(base+idx*0x28) → sheet gather` (`battle_anim_engine.md`); the per-PC base/sheet
  from §1.1 is the only new input. Lucca's attack `$8E` sequence (9A/80/02/0D) and ladder (§1.5) are the
  acceptance fixture.
- **Bracket**: drop the seat≥2 guard (G3), position `= battlePcAnchorDelta(seat) + bracketOffset[seat]`,
  seat2 offset ≈ (−12,−36) from §2.4 (or capture Frog's exact pose offset when the stance pose lands).
- **Attack/crit select**: charId-keyed already at `battle.c:1449`; add the normal table (G1) + Lucca/Frog
  spans (G7).

### 5.3 — HUD layout (the 3-PC strip)

- **Party box**: already loops `seat<3` → seat2 @ map row 6 / renderY 195 (§2.1). Draw names at cols 13-17
  **only when no seat-2 command box is drawn**; else names off (§2.3). Name palette `seat==0?2:3` (§2.2).
- **Command boxes**: box _k_ at map cols [6k..6k+5] — add **box2 (seat2) at cols 12-17**, hand at cols
  12-13 when `$95D5==2`, populated iff `$A6D9[2]!=0xFF` (§2.3). Fixes G4. The baked `ctBattleHudMapCmd`
  becomes the per-box template stamped at the box-k column, not a fixed 2-box strip.

### 5.4 — checks to add (on bat8/bat9-derived fixtures)

- `field-battle-bat8-lucca-idle`: render bat8, assert Lucca (cmb1) idle assembly `$9907[1]==0x31`.
- `field-battle-bat8-lucca-attack`: drive bat8 Lucca physical, assert `$9877==0x05` + the ladder
  {EC,3D,05,23,2A,A8,A9,AA} + span (fnv pin the frozen frame).
- `field-battle-bat9-3pc-hud`: render bat9, assert the party box shows 3 rows (Crono/Lucca/Frog HP) at
  rows 2/4/6, palettes {2,3,3}.
- `field-battle-bat9-frog-bracket`: drive bat9 to Frog-active, assert bracket OAM corners at
  (162,92)/(170,92)/(162,116)/(170,116).
- `field-battle-bat9-return`: interactive 3-PC victory → `$7F03FE` seed 1, both followers regroup,
  `$011F` restored (the `battle-forest-return`-style check for a 3-member party).

---

## 6. OPEN / boundaries

- **O1 — Frog's attack pose ladder**: not cleanly captured (bat9's Diablos animate ~150f and pre-empt the
  target-select window). Frog normal `$9877=0x08` is table-validated; his `$967B` ladder needs a calmer
  Frog-active capture or a patched state. (Diablos `$9877=0x10` / ladder 26-28 captured as a bonus.)
- **O2 — Frog's window stride `$9671`**: read 0 at rest (Frog un-ticked at frame 0); assumed the PC
  constant 0x24 (Crono/Lucca measured). Confirm on a Frog-ticked frame.
- **O3 — name/box2 HP-column reflow**: the party HP columns shift ~1 col (19 vs 20) between the 2-box and
  3-box states; pin exactly on the byte-exact strip bake.
- **O4 — name palette per-seat vs per-char**: Crono is always seat0, so `pal = seat==0?2:3` and "Crono
  white, others grey" are indistinguishable here; a capture with a non-Crono lead would disambiguate.
- **O5 — Lucca/Frog PC palette + entrance/death/victory poses** (G5/G6): their pal indices and non-Crono/
  Marle pose sets are un-baked; needs their stance/entrance captures (the standing `battle_stance.md` A8.4
  non-GATO-PC-pose item, now for PCs).
- **O6 — full Naga/Diablos AI + anim** (the separate follow-up): scripts pinned (CC/9323, CC/9A43) + parse
  structurally; the movement-suffix grammar + their E3 pose sets are the deferred enemy-derivation lane.
- **O7 — Marle's sheet `$1C57`**: not in bat8/9 (neither state has Marle); her base `$DC9AD8` is known
  (memory) — read it from any Crono+Marle state if the bake needs it.

## 7. REPRO (scratchpad, NOT committed)

```
SP=<session scratchpad> ; O=<oracle ct.exe copy>
# per-PC bases/sheets/staging (static reads)
$O --read "wram:2980+3,wram:1C78+30,wram:1C57+30,wram:29B0+15,wram:29BF+108" --from bat9_lake.state
# the $9877 tables + selector disasm
$O --read "rom:CD/4000+7,rom:CD/4007+7,rom:CD/400E+7" ;  python tools/dis.py 143C0 0x60
# Lucca attack (bat8): RIGHT->seat1, A(Att), A(target)
$O --monitor "95D5,9609,9877,8E,A013,9907:2,967B" --from bat8_lake.state --btnscript "0:112,80:3,0:6,100:3,0:22,100:3,0:120" --diff
# 3-PC HUD + Frog-active bracket (bat9)
$O --peek bat9_lake.state frogact.raw --btnscript "0:21,80:2,0:6,80:2,0:12,80:2,0:140"   # decode .vram strip + .oam bracket
# enemy anim-set / AI / reward
$O --read "rom:E4/F72C+10,rom:E4/F8D0+10,rom:CC/9323+16,rom:CC/9A43+16,rom:CC/5ED2+7,rom:CC/5FF8+7"
```
Artifacts: `bat/bat8_lake.state`, `bat/bat9_lake.state`, `hud_decode.py`, `frogstrip.py`, `frogact.raw*`.

---

## 8. IMPLEMENTATION STATUS (PART 1 = a1004ea; PART 2 = this lane)

**PART 1 (a1004ea, refined d79665a) — LANDED:** the G1 selector fix (`battle.c` normal `$9877` =
`CD/4007[cid]`, Lucca 0x05 / Frog 0x08); Lucca/Frog engine data (E3 `$DC4E70`/`$DC7490`, ROM sheets,
idle 0x31/0x3B, Lucca ladder) in the `animeng_*_pc.h` headers; per-seat palettes (`battle_pc_pal.h`); the
stance render + the **bracket** generalized to `seat<3` — the seat-2 bracket §2.4 is CLOSED here (foot-
anchored `16·rel+fine` from `$29B0+5·seat` + `(−12,−36)`; bat9 Frog seat2 `0A 08 0E 00` → (162,92), and
with the 4-corner box (dx 0/8, dy 0/24) the corners are EXACTLY (162,92)/(170,92)/(162,116)/(170,116)).

**PART 2 (this lane) — LANDED:**
- **§2.3/§5.3 the 3-PC command box2 (G4)** (`field.c` `CtBattleHudRender`): box _k_ at map cols [6k..6k+5];
  box2 = the box-0 Att/Tech/Item template stamped at cols 14-17 + the hand at cols 12-13 (`activePc==2`,
  the §2.3-VERIFIED position), gated on `CtBattleSeatQueued(mem,2)`; when box2 is present the party NAMES
  are dropped (the §2.3 mutual exclusion) leaving HP/MP/bars. Byte-IDENTICAL for 2-PC (seat2 never enqueued)
  and phase-0 (the `field-battlehud-gato` fnv 486A8FA1 is unchanged).
- **§2.1 the 3rd party row + 3-DIGIT HP**: the `for k<3` loop already drew seat2 @ row6; bat9's Frog has
  128 HP, so the 2-PC 2-digit field was extended with a HUNDREDS digit at **col 19** (drawn only when
  `curHP>=100` → every <100 render byte-identical). Decoded verbatim from the bat9 oracle strip
  (`row6 col19='1' col20='2' col21='8'`) — NOT the O3 reflow (cols 20-30 match the 2-PC bake exactly).
- **§2.2 name palette** = the active-PC rule `pal = ($95D5==seat)?2:3` (unchanged from PART 1); bat9
  command phase with Crono active gives {2,3,3} as §2.2 requires.
- **§3 the new enemies STAGE byte-exact** through the port's fully id/ROM-table-driven pipeline with ZERO
  new code (verified: Naga 0x1E HP 60 / reward XP8·G80·TP1 / AI `CC/9323` `17 10 00 00` / anim-set dissolve
  0x00; Diablos 0x48 HP 50 / XP10·G60·TP1 / AI `CC/9A43` / dissolve 0x00 — all == §3). The dissolve gate
  and the AI parse are automatic (`fieldBattleFinish` + the FD/BA4A interpreter, both id-driven).

**PART 2 — DEFERRED (mandate: not fabricated):**
- **O6+ the enemy VISUAL POSE render** (E3/sheet/E4 fixtures + an id-dispatched enemy engine like `ctPcPoseBind`).
  The enemies stage + anchor from their staged records, but the sprite POSE is un-shipped: Naga's idle
  assembly **0x17 IS captured** (bat8 `$9907[3..6]`), but **Diablos's idle is NOT** (bat9 `$9907[6..8]=0` —
  its ATB was mid-action across the capture), so rendering it would violate the mandate. This is the same
  "full Naga/Diablos AI + anim" lane O6 already scoped. `battleEnemyComputedAnchor`'s binary GATO-vs-HETAKE
  extent dispatch is likewise un-generalized (no new enemy renders yet to consume it).
- **check `field-battle-bat8-lucca-attack` (live drive) + `field-battle-bat9-return`**: both need a fight
  DRIVEN/RETURNED through the sim, but a MID-battle WRAM cannot enter the port's battle (the `fieldBattle`
  consumer re-derives from FIELD objects + `stageAll` overwrites the blocks + resets `frames=0` — a mid-
  battle seed never composes). Lucca's attack DATA ($9877=0x05 + the ladder) IS pinned (selector +
  `animengcheck-pc2`); the return regroup ($7F03FE=3−nf, nf=2→seed 1) is the already-ported mechanism
  (§4) — its 3-PC check is SKIP-pending a party3 PRE-battle field capture.

**The render vehicle** (`field.c` `CT_BATTLE_RENDERWRAM=<128KB wram>`): renders the PART-2 surface
(battleStanceRender + entrance + Crono active-idle + attack/marle/endpose + bracket + `CtBattleHudRender`)
from a mid-battle WRAM + a synthetic host, since `--battletest` cannot (above). `CT_BATTLE_RENDER_SETACT=<seat>`
forces the driven Frog-active box2/bracket state (the L/R wrap-3 cycle that reaches it is battle.c's, tested
elsewhere). `CT_BATTLE_RENDEROUT=<raw>` dumps the frame.

**Checks added** (`tools/regress_checks.json`; seeds in gitignored `saves/battle_re/bat8,bat9.raw.wram`):
`field-battle-animeng-pc2-gather` (Lucca 0x31 8/8 + Frog 0x3B 9/9 gather + Lucca ladder), `field-battle-bat8-
lucca-idle` (Lucca idle 0x31 + selector 0x05 + Naga staging ×4), `field-battle-bat9-3pc-hud` (3 rows
34/48/128, pals {2,3,3}, Frog 3-digit HP, selector, Diablos staging ×3), `field-battle-bat9-frog-bracket`
(the §2.4 corners + box2), `field-battle-bat9-return` (SKIP-pending). Quick 67/0/1, FULL 89/0/3 (baseline
+4 green +1 skip, ZERO regressions).
