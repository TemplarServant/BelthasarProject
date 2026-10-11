# BATTLE STATES CATALOG — nine new user battle savestates (batalha_1..9)

Cataloging (not implementing) nine snes9x battle savestates the user dropped in
`C:\Users\Icaro Ferris\Desktop\aux crono decompile\batalha_1.state` … `batalha_9.state`.
Each was converted to a runnable LakeSnes oracle state, verified by render, and read
byte-for-byte from its WRAM. **Every datum below is read from the state's own WRAM /
the ROM — nothing inferred.** This is a scouting file: it maps each state to the OPEN
battle boundaries it can unlock (§4).

The user's party hints are **CONFIRMED**: batalha_8 = Crono+Lucca, batalha_9 =
Crono+Lucca+Frog (the only 3-PC capture in the set).

---

## 0. CONVERSION RECIPE (byte-exact; reproduced for all nine)

snes9x `#!s9xsnp:0011` → LakeSnes state = **splice four regions of the s9x container
into a battle donor** (donor supplies APU/DMA/cart scaffolding). The lake offsets were
derived from `snes/snes.c snes_handleState` + `snes/ppu.c ppu_handleState` and match the
project's known anchors (WRAM @0x45, CGRAM @197073, per `regress_checks.json`
field-atbsel notes):

| region | s9x source | lake offset | length |
|---|---|---|---|
| WRAM (`$7E`+`$7F`) | `RAM` block | **69** (0x45) | 0x20000 |
| CPU regs | `REG` block (BE `PB DB P A D S X Y PC`, decoded) | 131141 | 29 |
| VRAM | `VRA` block (byte-exact, no re-order) | 131537 | 65536 |
| CGRAM | `parse_ppu_state.parse_ppu(PPU)['cgdata']` → LE u16 ×256 | **197073** | 512 |
| OAM+hiOAM | `parse_ppu(PPU)['oamData']` (512+32) | 197585 / 198097 | 512 / 32 |

Splicing **VRAM+CGRAM+OAM** (not just WRAM+CGRAM) makes each render show the REAL arena
and sprites of the captured frame, independent of the donor.

- **Tool:** `scratchpad/batalha_state/convert_full.py <s9x> <donor> <out>` (imports
  `tools/parse_ppu_state.py`).
- **Donor:** `scratchpad/gato_mid.state` (proven battle donor; worked for all nine).
- **Render/still:** `ct_probe --peek <lake> out.raw --frames 2` then
  `scratchpad/batalha_state/fb2png.py out.raw out.png` (fb 512×480, [pad,B,G,R], true
  pixel at 2x,2y). `--frames 2` clears the mid-frame CPU-resume transient and stays on
  the captured moment (frame-2 HP == frame-0 WRAM, verified on batalha_1).
- **Catalog read:** `scratchpad/batalha_state/catalog2.py <s9x>` (reads WRAM directly →
  frame-0 truth; independent of the oracle).

Converted states: `scratchpad/batalha_state/bat<N>_lake.state`. Screenshots:
`scratchpad/batalha_state/bat<N>.png`. All nine render **sanely** (verified visually).

### WRAM record layout used (all oracle-cross-checked on batalha_1 = GATO)
- **Loc** `$0100` (word); **storyline** `$7F0000`.
- **Active party** `$2980-82`: char-id, **bit7 (0x80) = absent**. Ids 0 Crono / 1 Marle /
  2 Lucca / 3 Robo / 4 Frog / 5 Ayla / 6 Magus.
- **Names** `$2C23`, **stride 6, indexed by char-id** (all 7 names live there; CT text
  0xA0-B9=A-Z, 0xBA-D3=a-z). Decodes clean: Crono/Marle/Lucca/Robo/Frog/Ayla/Magus.
- **PC battle block** `$5E2D + seat*0x80` (seats 0/1/2 = $5E2D/$5EAD/$5F2D): +0 char-id,
  +3 curHP, +5 maxHP, +7 curMP, +9 maxMP (all u16), +0x12 level; present iff maxHP>0.
  Stat-derive record +0x3C..+0x45 (§3).
- **Enemy field-staging** `$29BF + 12*k` (k=0..7): +0 relX, +1 relY, +2 fineX, +3 fineY,
  +4 prio, **+5 id**, **+6 = 0 VALID / 0xFF empty**, +7 flags (bit6 0x40 = mover-mode-6,
  as GATO), +8..+11 tail. (Matches `battle_menu_synthesis.md` §1.6 exactly; GATO slot-3
  record `08 06 08 0F 04 92 00 42 01 00 80 7F` at $29E3 reproduced.)
- **Enemy battle block** `$5FAD + slot*0x80` (= combatant array index 3+slot): +0 id,
  +3 curHP, +5 maxHP (u16). Block index == field-staging slot (NOT compacted — verified:
  GATO staging slot 3 ↔ block $612D; batalha_5 enemies at staging slots 6/7 ↔ blocks
  $62AD/$632D).
- **Enemy name (ROM)** `$CC:6500 + id*11` = **file 0xC6500 + id*11** (11-byte, 0xEF pad;
  the earlier "0x66500" in one memory line is the wrong HiROM fold — 0xCC→0xC0000 base).
- **Reward record (ROM)** `$CC:5E00 + id*7` = file 0xC5E00 + id*7 (§5).
- **Phase** `$0117` (field D8 op flags): **bit0 = battle-in-progress, D8 parked**. All
  nine read `$0117=01` = mid-battle. The module-internal ATB vars ($95D5 actor, $A6D9
  queue, $A013/$95DB freeze, $7F01EC outcome) all read **0** at every capture instant =
  each was saved at an ATB-fill / command-select lull (not mid-action-anim), matching the
  renders (static command-phase HUD, PCs damaged, enemies still full HP).

---

## 1. MASTER TABLE

| # | loc / story | arena (loc-id anchor + render) | party (id · HP/MP · LV) | enemies (id · name · count · HP) | phase | lake state |
|---|---|---|---|---|---|---|
| **1** | $006 / $08 | Leene Square fair (stone path, Telepod dome; GATO) | Crono 61/70·8/8 L1, Marle 51/65·12/12 L1 | $92 **Gato** ×1 (76/76) | mid-battle, cmd | bat1_lake.state |
| **2** | $013 / $08 | Guardia Forest 1000AD (green woodland) | Crono 70/70·8/8 L1, Marle 65/65·12/12 L1 | $8D **Beetle** ×2 (12), $19 **Avian Chaos** ×1 (45) | mid-battle, cmd | bat2_lake.state |
| **3** | $013 / $08 | Guardia Forest 1000AD (green woodland) | Crono 70/70·8/8 L1, Marle 65/65·12/12 L1 | $5E **Hetake** ×3 (14) | mid-battle, cmd | bat3_lake.state |
| **4** | $071 / $0C | 600AD woodland (autumn trees, rock) | **Crono solo** 70/70·8/8 L1 | $0C **Blue Imp** ×3 (13) | mid-battle, cmd | bat4_lake.state |
| **5** | $070 / $0C | 600AD woodland (rock ledge + bridge) | **Crono solo** 62/70·8/8 L1 | $0C **Blue Imp** ×2 (13) [staging slots 6/7] | mid-battle, cmd | bat5_lake.state |
| **6** | $070 / $0C | 600AD woodland (rock ledge + bridge) | **Crono solo** 62/70·8/8 L1 | $0C **Blue Imp** ×2 (13), $10 **Roly** ×1 (24) | mid-battle, cmd | bat6_lake.state |
| **7** | $077 / $0C | 600AD woodland (greener grove) | **Crono solo** 44/70·8/8 L1 | $15 **Blue Eaglet** ×3 (16) | mid-battle, cmd | bat7_lake.state |
| **8** | $081 / $12 | Manoria Cathedral (organ hall, red carpet, glass) | Crono 27/83·8/10 L2, **Lucca** 75/75·14/14 L2 | $1E **Naga-ette** ×4 (60) | mid-battle, cmd | bat8_lake.state |
| **9** | $082 / $15 | Manoria Cathedral (stone corridor, torches) | Crono 34/83·2/10 L2, **Lucca** 48/75·14/14 L2, **Frog** 128/128·17/17 L5 | $48 **Diablos** ×3 (50) | **3-PC** mid-battle, cmd | bat9_lake.state |

Screenshots `bat<N>.png` alongside the states. HP/MP are frame-0 WRAM (authoritative);
LV from block +0x12.

---

## 2. PER-STATE NOTES (arena + enemy staging positions)

Staging positions are `rel(X,Y)` tiles from `$29BF+12k` (+0/+1), useful for the
multi-enemy target-select layout + op-0xE7 formation/camera boundaries.

- **bat1 — GATO** (the project's canonical solo fight, fresh capture). Crono+Marle,
  GATO center rel(8,6) flags=0x42 (mover-mode-6, periodic idle — the RE'd case). Redundant
  with existing `gato_*` states but a clean 2-PC reference.
- **bat2 — Forest ambush, MIXED formation.** 2×Beetle rel(10,9)/(5,6) + 1×Avian Chaos
  rel(7,6). Two enemy *types* on screen at once → multi-type enemy-name HUD + mixed AI.
  Matches the existing `forest_wait_ambush` scenario (fresh capture).
- **bat3 — Hetake ×3.** rel(7,3)/(5,5)/(10,7). Matches existing `forest_3hetake`.
- **bat4 — Blue Imp ×3** (Crono solo). rel(10,5)/(11,9)/(4,9). First Blue Imp capture.
- **bat5 — Blue Imp ×2** (Crono solo, rock-ledge room loc070). Enemies at staging
  **slots 6/7** (blocks $62AD/$632D) — the non-compacted high-slot case; good block-index
  regression sample.
- **bat6 — Blue Imp ×2 + Roly ×1** (same loc070 room, later/other wave). Imps rel(6→) +
  Roly rel(6,4) slot6. First Roly capture; another mixed-type formation.
- **bat7 — Blue Eaglet ×3** (Crono solo, loc077). rel from slots 0/1/2. First Blue Eaglet
  capture; Crono down to 44/70 (most-damaged solo state).
- **bat8 — Naga-ette ×4** (Crono+**Lucca**, Cathedral organ hall). rel(9,5)/(6,5)/(3,6)/
  (12,6) — the **largest formation (4 targets)**. Crono at 27/83 (hurt). First Lucca
  battle capture; first Cathedral arena.
- **bat9 — Diablos ×3** (Crono+**Lucca**+**Frog**, Cathedral corridor). rel(12,8)/(7,7)/
  (2,8), flags=00. **The only 3-PC party in the set** — HUD renders three PC rows.

---

## 3. PC BATTLE-STAT-DERIVE RECORDS (block +0x3C..+0x45 — seeds C1/CF15 for new chars)

`kBattleStatDerive` is currently baked only for char-ids 0/1 (Crono/Marle). These states
give the **equipment-derived battle record** for **Lucca (2)** and **Frog (4)** verbatim
(also base stats +0x0B..0x11 and effective +0x36..+0x40). Records are identical across the
states a char appears in (starting gear), so they are stable seeds.

| char (id) | base +0x0B..0x11 | effective +0x36..0x40 | **derive +0x3C..0x45** | crit +0x45 | from |
|---|---|---|---|---|---|
| Crono (0) | `05 08 0C 05 08 08 02` | `05 08 0D 05 08 08 02 07 10 05 04` | `02 07 10 05 04 04 04 03 00 0A` | 0x0A | (baked) |
| Marle (1) | `02 06 08 08 08 08 08` | `02 06 08 08 0A 08 08 0A 0E 04 04` | `08 0A 0E 04 04 05 04 03 00 14` | 0x14 | (baked) |
| **Lucca (2)** | `02 06 06 08 08 08 0A` | `02 06 06 08 08 08 0A 08 13 04 04` | `0A 08 13 04 04 04 05 05 00 14` | 0x14 | **bat8, bat9** |
| **Frog (4)** | `09 0E 0B 08 09 09 09` | `0B 0E 0B 08 09 09 09 E5 26 04 04` | `09 E5 26 04 04 05 04 06 00 17` | 0x17 | **bat9 only** |

(Crono in bat8/9 is Lv2 with `06 09 0C…` base vs Lv1 `05 08 0C…` in bat1-7 — a second,
higher-level Crono derive sample. Marle's Lv1 record confirmed identical to the bake.)
`+0x45` is the byte the task flags as the crit rate; recorded here as-is — the C1/CF15
disasm owns the exact field mapping.

---

## 4. OPEN-BOUNDARY → BEST-CAPTURE MAP

Which of the project's open battle boundaries each state can pin (⭐ = the single best
capture for that boundary):

| open boundary | state(s) | notes |
|---|---|---|
| **3-PC HUD layout + bracket seat2 origin** | ⭐**bat9** | the ONLY 3-PC party (Crono+Lucca+Frog); HUD renders 3 rows — seat2 (Frog) HP/MP row + bracket origin |
| **Lucca stance / entrance / attack anim + span ($9877)** | ⭐**bat8**, bat9 | Lucca live in a 2-PC fight (bat8, cleaner) and the 3-PC fight (bat9) — drive frames forward to capture her keyframes |
| **Frog stance / entrance / attack anim + span** | ⭐**bat9** | the only Frog capture (Lv5, HP 128) — his weapon/anim set |
| **stat-derive records C1/CF15 (char-ids 2,4)** | ⭐**bat9** (Frog+Lucca), bat8 (Lucca) | derive records already extracted verbatim in §3 |
| **crit rates block+0x45 (Lucca/Frog)** | bat8 (Lucca 0x14), ⭐bat9 (Frog 0x17) | §3 last column |
| **new enemy AI scripts (CC/8B08) + move-suffix grammar** | ⭐**bat6** (Imp+Roly mix), bat2 (Beetle+Avian), bat7, bat8, bat9 | 5 un-RE'd enemies (Blue Imp/Roly/Blue Eaglet/Naga-ette/Diablos); all flags=0x00 (mode≠6, unlike GATO's mode-6 0x42) → new movement-mode samples |
| **enemy anchors/extents + anim-set $E4/F600 byte4 (dissolve-vs-stand)** | all 4-9 | 5 new enemy ids each need their $E4/F600 record read; staging anchors (rel positions) captured in §2 |
| **per-enemy reward records $CC:5E00** | bat4-9 | 5 new records pulled in §5 (Blue Imp/Roly/Blue Eaglet/Naga-ette/Diablos) |
| **formation/camera op-0xE7 in new arenas** | bat1(Leene), bat2/3(Forest1000), bat4/5/6(600 wood), bat7, ⭐bat8/9(Cathedral) | 5 distinct new arenas beyond the GATO/forest set |
| **multi-enemy target-select layout** | ⭐**bat8** (×4), bat2/3/4/7/9 (×3) | bat8's 4-target Naga-ette row is the widest selector sample; bat2/bat6 add mixed-type target lists |
| **enemy tech/magic examples** | bat8 (Naga-ette), bat2 (Avian Chaos), bat9 (Diablos) | candidate casters — **must be confirmed by running the fight** (not visible in a still); flagged, not proven |
| **enemy hurt-flash / death dissolve on new sprites** | any 4-9 | all enemies at full HP → drive an attack to capture first-hit flash + death on non-GATO sprites |

**Redundant with existing captures** (still useful as fresh 2-PC references): bat1 (GATO),
bat2 (`forest_wait_ambush`), bat3 (`forest_3hetake`). **Genuinely new to the project:** the
entire 600AD-woodland + Manoria-Cathedral arena set (loc 070/071/077/081/082), five enemy
species, and **Lucca + Frog as playable battlers** (bat8/bat9).

---

## 5. ENEMY REWARD RECORDS (ROM `$CC:5E00 + id*7`, file 0xC5E00) — bonus

Verified byte-exact vs `battle_results_window.md` for Gato/Hetake/Beetle/Avian. Layout:
`+0..1 XP(u16) · +2..3 Gold(u16) · +4 drop · +5 charm · +6 TP(u8)`. maxHP from the live
battle block (authoritative). NEW rows (not in the results dossier) marked ★.

| id | name | XP | Gold | drop | charm | TP | maxHP | record |
|---|---|---|---|---|---|---|---|---|
| $92 | Gato | 10 | 0 | — | $C9 Power Meal | 1 | 76 | `0A 00 00 00 00 C9 01` |
| $8D | Beetle | 3 | 15 | — | — | 1 | 12 | `03 00 0F 00 00 00 01` |
| $19 | Avian Chaos | 4 | 18 | — | — | 1 | 45 | `04 00 12 00 00 00 01` |
| $5E | Hetake | 3 | 16 | $BD Tonic | — | 1 | 14 | `03 00 10 00 BD 00 01` |
| ★$0C | Blue Imp | 2 | 12 | — | — | 1 | 13 | `02 00 0C 00 00 00 01` |
| ★$10 | Roly | 3 | 25 | $BD Tonic | — | 1 | 24 | `03 00 19 00 BD 00 01` |
| ★$15 | Blue Eaglet | 3 | 32 | $C6 Heal | — | 1 | 16 | `03 00 20 00 C6 00 01` |
| ★$1E | Naga-ette | 8 | 80 | — | — | 1 | 60 | `08 00 50 00 00 00 01` |
| ★$48 | Diablos | 10 | 60 | — | — | 1 | 50 | `0A 00 3C 00 00 00 01` |

---

## 6. REPRO

```
SP=scratchpad ; BS=$SP/batalha_state ; AUX="Desktop/aux crono decompile"
# convert (full VRAM+CGRAM+OAM splice) — donor = gato_mid.state
python $BS/convert_full.py "$AUX/batalha_<N>.state" $SP/gato_mid.state $BS/bat<N>_lake.state
# still (captured moment)
$SP/ct_probe.exe --peek $BS/bat<N>_lake.state $BS/bat<N>.raw --frames 2
python $BS/fb2png.py $BS/bat<N>.raw $BS/bat<N>.png
# catalog straight from s9x WRAM (frame-0 truth)
python $BS/catalog2.py "$AUX/batalha_<N>.state"       # party/enemies/phase + names
python $BS/statederive.py "$AUX/batalha_<N>.state"    # PC derive blocks + enemy rel-positions
```

Scratchpad artifacts (not committed; `%TEMP%/claude/…/e5608df4-…/scratchpad/batalha_state/`):
`bat1..9_lake.state`, `bat1..9.png`, `convert_full.py`, `fb2png.py`, `catalog2.py`,
`statederive.py`, `catalog_all.txt`, `statederive.txt`. Oracle copy `scratchpad/ct_probe.exe`.
Donor `scratchpad/gato_mid.state`. Canonical parser reused: `tools/parse_ppu_state.py`.
