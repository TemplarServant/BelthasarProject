# BATTLE ANIM-ENGINE ACCEPTANCE CORPUS — frame-exact oracle timelines the future anim-engine must reproduce

Purpose: the **acceptance spec** for the future battle anim-engine / sprite-streamer port. Each capture below is
a byte-decoded oracle timeline (OAM cluster cells + per-frame tile-content hashes where the C1/CC streamer runs),
organized as **"ours today vs oracle"** so each engine-port stage has a concrete target. Nothing implemented;
`src/`+git untouched — this notes file is the only write.

**Scope of NEW work here (vs the prior dossiers):** GATO's **directional ATTACK poses** (four facings — the
`battle_pc_approach_facing.md` §2.6 documented boundary "attacks keep the front rear-up bake / no directional
attack capture exists"), the **SONG presentation** (GATO's singing pose sequence + the CD/9EDA **note sprites** —
neither derived in `battle_song_spotlight.md`, which only did the screen effect), the **GATO walk-cycle tile
stream** per facing (the `battle_enemy_move_target.md` E.6 / `battle_pc_approach_facing.md` P4 boundary), and the
**PC hurt reactions** that fall out of the attack captures.

---

## METHOD / DECODE CONVENTIONS (as `battle_stance.md` / `battle_actionfx.md`)

- Oracle `ct_ae.exe` (= `build/ct.exe`, HEAD build), copied to scratch. OAM decode via `tools/ctlib/oam.py`
  (signed 9-bit X, 9-bit tile, `pal=(attr>>1)&7`, `prio=(attr>>4)&3`, v/h flips).
- **OAM shadow `$7E0700` (512) + `$7E0900` (32) = the live OAM**, CPU-maintained → `--monitor "0700:512,0900:32"`
  is per-frame exact (no peek-timeline jitter). Rendered/VRAM frames via `--peek` (fb + `.oam` + `.vram`);
  **peek == monitor** verified at every capture here (±0f on the patched HOLD states).
- OBJ 16×16 4bpp, CGRAM `128 + pal*16 + v` (idx0 transparent). **A pose OBJ pixel at OAM (x,y) → oracle fb
  true-pixel (x, y+8).** Cluster = min-corner **anchor** + each cell `tile@(dx,dy)[flips]`.
- **GATO = pal1**, 12 body cells (tiles 0x68/6A/6C/6E + 0x80/82/84/86/88/8A/8C/8E); **tile 0x006 = the active-PC
  BRACKET, EXCLUDED** (`battle_stance.md` ADD.6). **Crono = pal5** (0x08/0A/0C/0E), **Marle = pal6** (0x20/22/24/26).
- **Facing var `$96DE+slot`** (GATO = enemy slot 3 → byte[3]): **0=UP, 1=DOWN/front, 2=LEFT, 3=RIGHT**
  (`ctBattleFaceTab` = C0/F700[bearing], `battle_pc_approach_facing.md` §2.2). Action selector `$9877`
  (0x31 attack-A / 0x34 attack-B / 0x10 song); action-span active `$A013`=1.
- **GATO NEVER uses OAM h/v-flip** in any facing (`hf=vf=0` everywhere); the direction is carried by the
  **per-facing OAM assembly + streamed tile CONTENT** (§ each capture proves distinct VRAM md5).

### Capture states (scratchpad, all bare-legacy lake states patched from `gato_mid.state` @WRAM 0x45)
The natural HOLD fight (`gato_press_action`/`gato_mid`) attacks Crono FRONT only (Crono sits directly below GATO;
`$96DE`=1). Side/up attacks are forced by patching the **victim's live-position row `$1D0C+cmb`/`$1D23+cmb`** so
GATO walks horizontally/upward to it, and killing the other PC (`curHP=0` + dead-flag `block+0x1D bit7`, the proven
`gato_crono_dead` lever) so targeting is deterministic. GATO live pos `$1D12`/`$1D29`, base (136,111).

| state | Crono live pos | other PC | GATO does |
|---|---|---|---|
| `crono_left.state`  | (96,111)  left of GATO | Marle killed | walks LEFT, attacks facing **2** (span f72-229) |
| `crono_right.state` | (180,111) right of GATO | Marle killed | walks RIGHT, attacks facing **3** (span f116-273) |
| `crono_up.state`    | (136,55)  above GATO | Marle killed | walks UP, attacks facing **0** (span f147-302) |
| `crono_farleft/farright.state` | (32,111)/(220,111) | Marle killed | LONG horizontal walk (leg-cycle capture) |
| `marle_right.state` | Crono killed | Marle (176,111) | attacks Marle facing **3** (natural RIGHT bearing) |
| `gato_mid.state` (unpatched) | (134,144) below | — | natural FRONT attack (span f78-235), song |
| `gato_moving_lake.state` | — | — | RETURN walk (facing 0) + **SONG f373-685** |

---

# CAPTURE 1 — GATO'S DIRECTIONAL ATTACKS (four facings) ⭐ the user's "attacks facing the sides are missing"

**HEADLINE: GATO's ATTACK is fully directional — FOUR byte-distinct rear-up/strike pose sets (front/left/right/up),
selected by his walk-arrival facing `$96DE`, exactly like his idle/walk facings (§2). Same tile NUMBERS
(0x68–0x8E), `hf=vf=0` everywhere; the direction is the OAM assembly + streamed tile CONTENT (all four VRAM md5s
distinct). This is the `battle_pc_approach_facing.md` §2.6 boundary CLOSED.** GATO walks to the victim (the
`battle_enemy_move_target.md` mover), holds his walk pose ~27f (windup), **rears up**, **strikes toward the
victim**, then recovers to the walk/idle pose and returns.

### 1.1 The four ATTACK REAR-UP poses (pal1, anchor = min-corner; all `hf=vf=0`)

| facing | state / rep frame | anchor | assembly | VRAM md5 (0x68–0xA0) |
|---|---|---|---|---|
| **FRONT** (1) | `gato_mid` f125 | (110,56) | `068@(18,0) 08E@(0,4) 08C@(35,4) 06E@(2,16) 06C@(18,16) 06A@(33,16) 086@(2,32) 084@(18,32) 082@(34,32) 08A@(10,48) 088@(25,48) 080@(18,64)` | `528f2178` |
| **LEFT** (2) | `crono_left` f108 | (102,37) | `06E@(16,0) 08E@(4,16) 08C@(20,16) 06C@(0,32) 06A@(16,32) 068@(32,32) 084@(0,48) 082@(16,48) 080@(32,48) 08A@(8,64) 088@(24,64) 086@(18,80)` | `2632201d` |
| **RIGHT** (3) | `crono_right` f153 | (128,37) | `06E@(20,0) 08C@(12,16) 08E@(28,16) 068@(0,32) 06A@(16,32) 06C@(32,32) 080@(0,48) 082@(16,48) 084@(32,48) 088@(8,64) 08A@(24,64) 086@(14,80)` | `2ceb22ac` |
| **UP** (0) | `crono_up` f195 | (110,27) | `06E@(10,0) 06C@(25,0) 06A@(0,4) 068@(35,4) 086@(2,16) 084@(18,16) 082@(33,16) 08E@(2,32) 08C@(18,32) 08A@(34,32) 088@(10,48) 080@(25,48)` | `ae5b99c4` |

- The **FRONT rear-up == the shipped `battle_anim.h` kf8 `G1_rearup`** (byte-identical assembly) — our current
  bake. The other three are wholly distinct (different anchor, cell layout, and tile content).
- The **side/up rear-ups are TALLER** (body extends to +80 vs front's +64; anchor y 27–37 = reared up ~15–24px vs
  front's 6px) — GATO reaches his claws toward the off-axis/above target.
- **LEFT ≠ mirror(RIGHT)** and **≠ mirror(front)**: four fully-distinct directional sprite sets (matches §2.4's
  idle/walk finding — GATO has 4 art sets, not 3+flip). Rendered proof (scratchpad `L_f108.png`/`R_f166.png`/
  `F_f125.png`/`U_f195.png`): GATO visibly turned left / right / front / reaching-up in each.

### 1.2 The STRIKE (lunge) pose — directional, distinct from the rear-up

| facing | rep frame | anchor | strike mechanism |
|---|---|---|---|
| **LEFT** | `crono_left` f122 | (86,37) | whole body **lunges LEFT** — anchor jumps (102→86, −16px); `068@(48,32)` claw thrusts to the far left toward the victim. assembly `06E@(32,0) 08E@(20,16) 08C@(36,16) 06C@(16,32) 06A@(32,32) 068@(48,32) 086@(0,40) 084@(16,48) 082@(32,48) 080@(48,48) 08A@(24,64) 088@(40,64)` (VRAM `cbf99065`) |
| **RIGHT** | `crono_right` f166 | (128,37) | claw cell **`086@(48,40)` extends far RIGHT** toward the victim; anchor holds (128,37). assembly `06E@(20,0) 08C@(12,16) 08E@(28,16) 068@(0,32) 06A@(16,32) 06C@(32,32) 086@(48,40) 080@(0,48) 082@(16,48) 084@(32,48) 088@(8,64) 08A@(24,64)` (VRAM `17ce4503`) |

The **front strike is DIFFERENT in kind**: the front rear-up OAM is FIXED and the claw-swipe streams in the
**tile content only** (0x80–0x9F, the shipped `ctGatoSwipeKf` GS1/GS2/GS3, `battle_actionfx.md` A4). The
**side strikes rearrange the OAM ASSEMBLY** (LEFT lunges the body −16px; RIGHT extends a claw cell to +48) — a
per-facing strike pose, not just a tile stream.

### 1.3 Per-attack timeline (monitor-exact, LEFT & RIGHT symmetric)

```
LEFT  (crono_left,  facing 2):  walk-LEFT pose  f34-66  ->  ATTACK dispatch f72 ($9877=31,$A013=1)
   windup: holds walk pose f67-98 (~27f)  ->  REAR-UP f99-118 (20f) anchor(102,37)
   ->  STRIKE/lunge f119-147 anchor(86,37) [-16px left]  ->  recover to walk/idle pose f148+  ->  span end f229 (Crono -9)
RIGHT (crono_right, facing 3):  walk-RIGHT pose f34-93  ->  ATTACK dispatch f116
   windup: holds walk pose f116-142 (~27f)  ->  REAR-UP f143-163 (21f) anchor(128,37)
   ->  STRIKE f164-191 [086 claw +48 right]  ->  recover f192+  ->  span end f273 (Crono -9)
UP    (crono_up,    facing 0):  walk-UP pose  f?-146  ->  ATTACK dispatch f147  ->  REAR-UP f174-220 anchor(110,27)
```
**Rear-up onset ≈ dispatch + 27f** (LEFT f72→99, RIGHT f116→143) — GATO holds his directional walk/idle pose for
the windup, then rears up. The strike lands mid-rear-up (hurt flash on the victim, §CAPTURE 4). Damage is
UNCHANGED by facing (attack-A rec#0 → −9, `battle_gato_moveset.md` §D).

### 1.4 Are side attacks CANONICAL? — YES for RIGHT (Marle), LEFT/UP need repositioning
GATO's physical pre-move victim = script `05` random-PC (last-target-avoiding, `battle_enemy_move_target.md`
§A.5/C). In the **staged GATO formation Marle sits at GATO's exact height to his RIGHT** (176,111 vs GATO 136,111),
so when GATO's physical randomly targets Marle he **walks RIGHT and attacks facing 3 — a normal, in-game side
attack** (this IS the user's report; `marle_right.state` reproduces it: facing 3, Marle −9). Crono sits directly
below → FRONT. **LEFT and UP** require a repositioned victim (no PC sits left of / above GATO in this formation),
but the **pose data is the same 4-facing sprite set** the render must select from (relevant to other formations /
3-PC parties / other walking enemies).

### 1.5 OURS TODAY vs ORACLE — the gap (code-provable)
`battleGatoRender` (`field.c:2131-2164`): the ATTACK branch (`if (h->fxEnemyRearup)`) renders the **front**
`ctGatoSwipeKf`/`ctGatoSwipeObj`/`ctGatoSwipeTiles` (GS1/GS2/GS3 = the FRONT rear-up + claw-swipe), shifting only
POSITION by `gdx/gdy` (live foot). The **facing selector (line 2174) sits AFTER the attack `return`**, so it never
runs for an attack. The code comment states it: *"attacks keep the front rear-up bake above (no directional attack
capture exists — documented boundary)."*

| | oracle | ours today | gap |
|---|---|---|---|
| GATO attacks facing LEFT/RIGHT/UP | distinct directional rear-up + directional strike (1.1/1.2) | **front rear-up + front claw-swipe, repositioned** | wrong pose every non-front attack |
| strike mechanism | side = OAM lunge/claw-extend; front = tile-stream on fixed OAM | always front tile-stream | wrong |

**FIX direction (bake, mandate-clean):** capture the 4 directional rear-up + strike keyframes (assembly + streamed
OBJ tiles 0x68–0x9F + pal1, per §1.1/1.2 rep frames — a bake shaped exactly like `battle_gato_faces.h` extended to
attacks) and select by `h->mvFacing[3]` in the attack branch (default facing 1 = the existing front bake, so every
pinned MASH fight — GATO block2, never walks, stays facing 1 — is byte-identical). Front stays the shipped kf8/GS.

---

# CAPTURE 2 — THE SONG PRESENTATION ⭐ the user's "the singing animation looks strange"

Base `gato_moving_lake.state`, song span **f373–685** (312f, `$9877`=0x10, `$A013`=1), GATO singing in place at
(128,85), facing 1 (front, toward the party) the whole span. Two components: **GATO's singing pose** and the
**musical NOTE sprites** — neither was derived in `battle_song_spotlight.md` (which covered only the darken+beam
screen effect; that IS now implemented in ours, `battleSongFxRender`).

### 2.1 GATO's singing pose = the FRONT idle pose + a GENTLE CLAW SWAY (not the big idle claw-swap)
GATO holds his **front/down idle assembly** (`08E@(16,0) 08C@(32,0) 06E@(0,11) 068@(47,11) …`) at anchor (96,29)
and animates a **small claw sway** — the top-right claw cells `068/06A` drift **in and down and back**:

```
068 position over the span (monitor-exact, gato_moving_lake):
  f360-417  068@(47,11)   arms OUT (pre-effect; song just started)
  f418-434  068@(42,11)   \
  f435-449  068@(42,12)    >  gentle sway IN, ~16f dwells, oscillating 47->42->41 (x, -6px) / 11->13 (y, +2px)
  f450-465  068@(41,13)   /   innermost; then back out to 42,11 and repeat (period ~48-64f)
  ... oscillates 42,11 <-> 42,12 <-> 41,13 through f618 ...
  f619-689  068@(47,10)   arms back OUT (song ending)
```
- **Amplitude ~6px** (claw in/out) — a subtle "swaying while singing" motion. It is **NOT** GATO's idle claw-SWAP
  (the big ~38px 068/06A up↔down alternation of `ctGatoIdleTrajV2`), and there are **no dedicated singing OAM
  poses** (no arms-raised gesture in the OAM).
- The sway is **active only in the lit MIDDLE of the song** (f418–618) — it starts ~span+45 and ends ~span+245,
  co-timed with the `battle_song_spotlight.md` spotlight fade-in (~span+42) / fade-out (~span+236).
- **Tile CONTENT streams too**: GATO's body tiles 0x68–0xA0 alternate **two states** (`2b336113` ↔ `3dad27a6`,
  ~40-70f cadence) = the mouth/face singing animation (the C1/CC streamer; two rep states, boundary as elsewhere).

### 2.2 The NOTE sprites (CD/9EDA note machine) — a full timeline
The song spawns **musical notes as OBJ sprites**, floating up around GATO's head. Byte-decoded from the OAM shadow:

- **Identity:** each note = ONE **16×16 (OBSEL size bit=1) OBJ, pal0, prio3**, tile cycling **0x0E6 → 0x0E8 →
  0x0EA** (~6f per glyph). The **three glyph tiles are STATIC in VRAM** (md5 `c7b2988b` constant all span) — the
  note "twinkles" by the OAM sprite **cycling which of the 3 fixed tiles** it shows (NOT a tile stream). Pal0 OBJ
  (CGRAM 128–143) — the note renders dark (blue/grey), unlike the white pal0 damage digits (same palette row,
  darker indices).
- **Slots / count:** OAM slots **12–16**, up to **5 concurrent**. Note-count timeline (span-relative in []):
  ```
  f349[<0]:0  f417[+44]:1  f435[+62]:2  f450[+77]:3  f480[+107]:4  f483[+110]:5(peak)
  f515[+142]:4  f532[+159]:3  f534[+161]:2  f577[+204]:1  f580[+207]:0
  ```
  Notes ramp **0→5 over f417–483**, hold ~5, ramp **5→0 by f580** — active f417–580 (span+44 → span+207),
  aligned with the spotlight window.
- **Spawn + trajectory + lifetime** (per-note, from the OAM-slot tracker):
  - born near **(129,36)** (just above GATO's head/mouth; also secondary births ~(136,39)/(148,55)/(150,44)).
  - **drifts slowly RIGHT ~+0.5px/f** with a slow vertical wander (rise/fall), e.g. one note (129,36)→(173,36)
    over ~96f then curls up.
  - **lifetime ≈ 80–117f** per note (the long-lived ones), overlapping so 3–5 float at once.
- ROM path (cited, `battle_song_spotlight.md` B.3 / `battle_gato_moveset.md` §B.3): the song anim CD/9EDA has 5
  moving-effect tracks driving note objects (`85`/`98`/`A8`, C1/6258/6299/69C3) off the effect vars
  `$5D80/$5D9C/$5D9E/$5DA0`; the note graphic loads from D1/4100|D1/4400. Per-note byte-exact drift law = that
  effect subsystem (boundary); the OBSERVED spawn/drift/lifetime above is the acceptance target.

### 2.3 OURS TODAY vs ORACLE — the gap
During the song, `battleGatoRender` does NOT set `fxEnemyRearup` and `mvFacing[3]`=1, so it falls to the **idle
path** → draws `ctGatoIdleTrajV2` (the big idle claw-SWAP) at the idle cadence. There is **no note-sprite render
at all**.

| | oracle | ours today | gap |
|---|---|---|---|
| GATO song pose | front idle + gentle ~6px claw SWAY (2.1), lit-middle only | big idle claw-SWAP (~38px, 120f period) | wrong motion |
| note sprites | up to 5 floating notes (2.2) | **none** | missing entirely |
| screen effect | darken + orange spotlight cone | **implemented** (`battleSongFxRender`, `battle_songbeam.h`) | ✓ match (dossier's "no effect" gap closed) |

**FIX direction:** (a) gate a distinct song pose while `$9877`==0x10 — the front idle assembly with the 6px
068/06A sway (2.1) instead of the idle swap; (b) add a note-sprite layer — bake tiles 0x0E6/0E8/0EA + OBJ pal0,
spawn near GATO's head, cycle the tile ~6f, drift +0.5px/f, ~90f lifetime, up to 5 concurrent, over span+44→+207.

---

# CAPTURE 3 — GATO WALK CYCLES per facing ⭐ (E.6 / P4: our idle-slide placeholder misses the legs)

GATO walks to/from the victim at the enemy-mover rate **~0.2px/frame** (`battle_enemy_move_target.md` A.3,
8-phase trig segments). Each facing has a distinct **walk pose** (OAM) AND a **leg-cycle TILE stream** (the C1/CC
anim engine reloads the walk-anim track — E.6). Our engine slides ONE static facing rep-frame → no leg motion.

### 3.1 The four walk poses (OAM, pal1, `hf=vf=0`)
| facing | state / rep | anchor | walk pose (key cells) |
|---|---|---|---|
| **DOWN** (1) | `gato_mid` f40-70 | (104,63) | `068@(31,48)` (claws **dropped low**), tiles `431c0188` — distinct OAM from the idle (claws-up 068@(47,10)) |
| **LEFT** (2) | `crono_left` f45 | (105,52) | `08A@(4,0) 088@(20,0) 080@(0,16) 06E@(16,16) 06C@(32,16) 086@(0,32) 084@(16,32) 082@(32,32) 06A@(16,33) 068@(13,39) 08E@(8,48) 08C@(24,48)` |
| **RIGHT** (3) | `crono_right` f45 | (118,52) | `088@(12,0) 08A@(28,0) 06C@(0,16) 06E@(16,16) 080@(32,16) 082@(0,32) 084@(16,32) 086@(32,32) 06A@(16,33) 068@(19,39) 08C@(8,48) 08E@(24,48)` (mirror-ish of LEFT, distinct tiles) |
| **UP** (0) | `gato_moving_lake` f40 | (96,63) | `068@(47,10) 06A@(47,26) 080@(31,48) …` (back view — same OAM LAYOUT as front idle, different tile CONTENT) |

### 3.2 The LEG CYCLE is a multi-state TILE stream (the animation our engine drops)
On a LONG walk the walk pose's OAM stays ~fixed (±1px 068/06A shuffle) while the **tile content cycles distinct
leg states** (VRAM md5 of 0x68–0xA0, ~15–30f per state):

| facing | leg-cycle tile states (VRAM md5) | source |
|---|---|---|
| **LEFT** | `7619c1f9` → `edc9afa7` → `6b1d13cd` (3 states) | `crono_farleft` f45/f75/f105 |
| **RIGHT** | `35a0ff8e` → `a7b1e80e` → `609313ff` (3 states) | `crono_farright` f45/f75/f105 |
| **UP** | `cf40bcfc` ↔ `f7e985f0` (2 states) | `gato_moving_lake` f40/f90/f120/f200 |
| **DOWN** | `431c0188` (short approach — only 1 state captured; >1 expected on a longer walk) | `gato_mid` f40-70 |

A SHORT approach (e.g. `crono_left` f37-64, ~40px in ~27f) may hold ONE leg state — the swap dwell (~15-30f) is
longer than a short walk, so the legs appear static; the cycle only shows on longer walks (`crono_farleft` etc.).

### 3.3 OURS TODAY vs ORACLE — the gap
`battleGatoRender` (`field.c:2174-2187`) selects the facing rep-frame `ctGatoFaceKf` (UP/LEFT/RIGHT baked, one
static tile set each) and slides it at the live foot. Comment (lines 2127-2130): *"while he walks he slides in his
IDLE bob pose … the leg animation is the documented follow-up bake … No pose is SYNTHESIZED."*

| | oracle | ours today | gap |
|---|---|---|---|
| GATO walking | directional walk pose + 2–3-state leg-cycle tile stream (3.1/3.2) | facing rep-frame (1 static tile set) slid at 0.2px/f | no leg motion |

**FIX direction:** bake per-facing the walk pose + its 2–3 leg-cycle tile states (0x68–0x9F content at the §3.2 rep
frames + pal1) and cycle by a ~15-30f walk clock while `mvActive`. Front/down needs its own down-walk pose
(068@(31,48)) too.

---

# CAPTURE 4 — CRONO + MARLE hurt reactions (fall out of the attack captures)

When GATO's directional strike lands, the victim reacts. Decoded from the side-attack OAM shadows.

### 4.1 The hurt FLASH + KNOCKBACK (both PCs)
| victim | flash | knockback | pose |
|---|---|---|---|
| **Crono** (LEFT attack, GATO on his right) | pal5→**pal4 white, 3f** (f126-128) | anchor **−7px LEFT** (away from GATO): (80,80)→(73,79), held | holds a compact guard pose `00A@(0,0) 008@(16,0) 00E@(0,16) 00C@(16,16)` |
| **Marle** (RIGHT attack, GATO on her left) | pal6→**pal4 white, 3f** (f120-122) | anchor **+5px RIGHT** (away from GATO): (163,79)→(168,67), held | steps forward into `022@(0,0) 020@(16,0) 026@(0,16) 024@(0,32)` |

- **The flash = the whole-body pal4 all-white swap** (`battle_actionfx.md` P3), **3 render frames** (sharper than
  the ~9f the stance dossier estimated; matches A3.1's "Crono flashes pal4 f532-534 (3f)"). Already baked
  (`ctBattleFxFlashPal`, applied via `battleEnemyFlashing`/victim path). ✓
- **KNOCKBACK ~5–7px AWAY from the attacker**, then held (does not spring back within the span) — this **refines
  the `battle_actionfx.md` A3.3 "knockback OPEN"**: direction = away from the strike source, magnitude ~5-7px on
  these attack-A hits. (A3.3 measured ~9px on a different capture — magnitude is hit/HP-dependent; the DIRECTION
  = away-from-attacker is the stable law.)

### 4.2 OURS TODAY vs ORACLE
| | oracle | ours today | gap |
|---|---|---|---|
| hurt flash | 3f pal4 white swap | baked (`ctBattleFxFlashPal`) | ✓ (duration: ours CT_FX_FLASH_DUR=9 vs oracle 3 — minor) |
| knockback | ~5-7px away from attacker, held | none / OPEN | small — the away-vector is now derived |

### 4.3 Victory variants — reference (not re-captured; GATO survives these attacks)
The PC victory celebration (animated ~72f settle-in ladder into the static held pose) is already fully derived in
**`battle_gato_idle_victory.md` §C** (Crono C0–C5, Marle M0–M5 keyframe ladders + the `battle_end.h` held pose)
and its dead-standing-freeze fix (§B/D.2, shipped). Not duplicated here — that dossier is the victory acceptance
spec. (Not reachable in the CAPTURE-1 side-attack states; a fresh cross-capture would use `gato_kill.state`.)

---

# SUMMARY — captures made · gaps quantified · opens

**Captures (all oracle byte-decoded, this file's tables):**
1. GATO **4 directional attacks** (front/left/right/up) — rear-up + strike assemblies + VRAM md5s, per-frame
   timelines, all 4 VRAM-distinct; visual renders `{L,R,F,U}_f*.png`.
2. The **SONG** — GATO's front-idle + 6px claw-sway pose sequence + mouth tile-stream; the **note machine**
   (tiles 0x0E6/0E8/0EA pal0 16×16, slots 12-16, ≤5 concurrent, spawn→drift→lifetime + count timeline).
3. GATO **walk cycles** per facing — 4 walk poses + the 2–3-state leg-cycle tile stream (VRAM md5s).
4. **Crono/Marle hurt** — 3f pal4 flash + ~5-7px away-knockback; victory referenced to `battle_gato_idle_victory.md`.

**Gaps quantified (ours today):**
- **Attack:** front rear-up/GS-swipe drawn for ALL facings (code: `field.c:2131-2164`, facing selector after the
  attack return) → 3 of 4 facings render the wrong pose.
- **Song:** idle claw-SWAP (should be the 6px sway) + **zero notes** (spotlight already correct).
- **Walk:** facing rep-frame slid with **no leg cycle** (E.6/P4 boundary).
- **Hurt:** flash ✓ (9f vs 3f duration), knockback absent (now derived: ~5-7px away).

**Open items (boundaries / follow-ups):**
- **Bakes to capture** for the fix: 4 directional attack rear-up+strike keyframes (tiles+assembly, §1.1/1.2 rep
  frames); the song pose + note glyph tiles (0x0E6/0E8/0EA)+pal0 (§2); the per-facing walk leg-cycle tile states
  (§3.2). All follow the `battle_gato_faces.h` / `battle_anim.h` bake shape (assembly + streamed OBJ tiles + pal).
- **Note per-note drift LAW** (C1/62xx effect subsystem, `$5D80`/`$5DA0`) — only the observed spawn/drift/lifetime
  is pinned; the exact per-frame trajectory is the effect-engine boundary.
- **Within-pose per-frame tile stream** for every capture (the sword-arc/claw/mouth/leg pixels) = the standing
  C1/CC anim-engine + FD:E170 streamer boundary (`$0520+n*0xC`, `FD:E3A9`); rep-frame tiles are the bake unit.
- **UP-facing attack** is reachable only via a repositioned above-GATO victim (no PC sits above in this formation);
  the pose exists in the sprite data (§1.1) but is non-canonical for the GATO fight. LEFT is canonical only if a PC
  sits left (other formations / 3-PC). RIGHT (Marle) + FRONT (Crono) are the in-fight cases.
- **Knockback magnitude** is hit/HP-dependent (5-7px here, ~9px in A3.3); the away-direction is the stable law.

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-e5608df4-…/scratchpad/corpus/`
- Oracle `../ct_ae.exe` (= build/ct.exe). Toolkit: `ctk.py` (monitor parse + OAM decode via ctlib), `patch_state.py`
  (WRAM patcher, bare/CTLS), `parse_gato.py` / `parse_victim.py` / `dump_sprites.py` / `note_track.py` (analysis),
  `render.py` (fb→PNG + VRAM tile-hash).
- States: `crono_left/right/up/farleft/farright.state`, `marle_right.state` (patched from `../gato_mid.state`).
- OAM monitors: `left_oam.txt` `right_oam.txt` `up_oam.txt` `marleright_oam.txt` `song_oam.txt`.
- Peeks (`.raw`+`.oam`+`.vram`): `{L,R,F,U}_f*.raw` (attacks), `song_f470/f500.raw` (notes), `fl*/fr*/uw*/dw*.raw`
  (walk cycles), `cf*.raw` (Crono flash). PNGs: `L_f108/R_f166/F_f125/U_f195.png` (4 attacks), `song_f500.png`
  (spotlight+notes), `fl*.png` (walk).
- Repro: side attack = `patch_state.py gato_mid.state OUT 1D0C=<x> 1D23=<y> 5EB0=00,00 5ECA=80` then
  `--monitor "96DE:8,9877:1,A013:1,0700:512,0900:32" --from OUT --btnscript "0:N"`; song =
  `--monitor "…,0700:512,0900:32" --from gato_moving_lake.state --btnscript "0:690"` (span f373-685).

---

## CORRECTION 2026-09-09 -- CAPTURE 4's "knockback" is a change of POSE RECORD, not a displacement

The "~5-7 px away from the attacker, held, snapping back at anim-end" of §4.1 measured the min-corner of the
victim's cluster, and the min-corner moves because the POSE changes -- the record's own (dx,dy) differ. Re-measured
on `gato_press_action.state` (GATO's front attack on Crono, `--peek ... --btnscript "100:8,0:N"` -> .oam, the
pal-5 cells), against the E3 records (`E4:2300[0] -> $DC27B0`, idx*0x28, pos at +0x20):

    f545  foot (134,147) after the A9 recoil   pose 6E  cells (134,114)(118,114)(134,130)(118,130) = foot + rec 6E EXACT
    f560                                       pose C6  cells (125,124)(109,118)(126,139)(125,111) = foot + rec C6 EXACT
    f660  (after the span)                     pose 4F  cells (126,123)(110,119)(126,139)(126,110) = foot + rec 4F EXACT

and a scan of all 256 records finds no record matching any of those layouts with a uniform offset. So the OAM of
a struck PC is `$1D0C/$1D23 + the record` and nothing else; the 6E -> C6 change (`06 09` then `06 03` on the
victim track, published at rel 49 and 73 -- `$9907[0]` 6E at f527, C6 at f551) is the recoil the eye sees, and the
foot's own +3 is the A9 op (battle_enemy_move_target.md, "the move-forward mover"). The port's CT_FX_KNOCK_MAG /
battleKnockVector (6 px along the away vector, strike -> animRel) are retired, and the PC stance layer now draws
at the LIVE foot (it drew the staged $29B0 foot, so the A9's +3 never reached the screen and the 6 px roughly
stood in for it at the hurt pose). `field-battle-pc-hit-cells` pins the four cells of C6 at (134,147) -- the
oracle's f560 verbatim.

