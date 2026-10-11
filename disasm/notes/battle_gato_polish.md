# GATO PRESENTATION POLISH — four user reports DERIVED (facing / song-rise+clip / target-flinch / flurry)

Derives, from the ROM disasm + LakeSnes oracle **only**, the four GATO-presentation truths behind the user's
reports, and pinpoints each divergence in our engine (HEAD bfdc812, anim-engine stages 1-3 live). `src/`+git
untouched — this notes file is the only write. Method: oracle `ct_ae.exe` (= build/ct.exe, HEAD) `--monitor`
(per-frame RAM, writer PCs) + `--peek` (renders + OAM/VRAM), our engine `ct_field_ae.exe --battletest`
(CT_BATTLE_SCRIPT/MASHA drivers + CT_BATTLE_FACETRACE/MVTRACE/TRACE + CT_BATTLESHOT_AT render captures), ROM
`tools/dis.py`. HOLD driver `100:8,0:N` (GATO un-hit → block1 attack-A + song); MASH `CT_BATTLE_MASHA=1`
(GATO hit → block2 attack-B). All frames are the monitor's state-relative counter (gato_press_action).

## ⭐ HEADLINE — the four reports resolve into TWO root defects (one shared)

| # | user report | root cause | fix locus |
|---|---|---|---|
| **1** | "GATO often **walks backwards**" | our RETURN leaves `mvFacing[3]=0` (UP/back) through settle; never re-faces the party | `battleMoveReturn` settle (battle.c) |
| **2** | "the song **cut off the top part**; in the original he moves UP and sings at the SAME position" | our return settles at a **target-dependent** (inconsistent, sometimes too-high) height; the song pose reaches 56px above the foot → top approaches/crosses `y<0` | same return (battle.c) + song foot (field.c) |
| **3** | "when he sings with Marle as the target, she does a **startled animation**" | the song's TARGET PC plays a flinch pose (assembly **0x5B**); our engine has no PC-react render | new target-flinch render (field.c) + PC flinch bake |
| **4** | "GATO **did not perform the multiple-punch attack**" | the flurry (attack-B) RENDERS fine, but item-1's stuck `mvFacing=0` routes attack-B to the **directional** branch → flurry never shown in mixed play | **fixed by item 1** |

**Items 1, 2 and 4 are the SAME defect** — our simplified RETURN mover (Wave-3 geometric approximation of the
ROM's octant-oscillation retreat) leaves GATO facing UP and settling at a target-dependent height. One structural
fix to the return (re-face the party + a consistent settle) repairs the back-walk, the song height/clip, AND the
missing flurry. Item 3 is independent (a missing PC-react pose).

---

# ITEM 1 — "walks backwards": the RETURN facing rule (DERIVED) vs ours

## 1.1 CANONICAL — the facing FOLLOWS the current travel bearing, BOTH legs (disasm + oracle)

Both movement handlers write `$96DE` (the facing var, GATO = enemy slot 3 → byte[3]) the SAME way — from the
segment's atan2 bearing through the face table `C0/F700`:

- **APPROACH** (C1/379A → tail C1/37E9): `137E9 LDA $C0F700,X / 137EF STA $96DE,X` (X = `$DB` = atan2 bearing).
- **RETURN**  (C1/3A3D → tail C1/3B14): `13B14 LDA $C0F700,X / 13B1A STA $96DE,X` (X = the AWAY bearing, atan2
  operands swapped at 13A4F; the octant reverse-oscillation 13AA0-13AF5 adjusts `$DB` by +0x80 when the ref is
  crossed). **Identical mechanism — the facing tracks the heading in the return too.**

⇒ **RULE: GATO turns to face wherever he is walking.** He does NOT moonwalk / back away facing the party.

## 1.2 ORACLE ground truth (HOLD `100:8,0:1450`, `$96DE[3]` / `$974A[3]` mode / live `$1D12,$1D29`)

```
f 380  mode=01 APPROACH  facing=00        GATO=(136,111)  <- armed, stale UP
f 421  mode=01 APPROACH  facing=01 DOWN   GATO=(136,111)  \  approach: faces DOWN, walks DOWN
f 478  mode=01           facing=01 DOWN   GATO=(136,118)  /  attack-A dispatches adjacent
f 638  mode=06 RETURN    facing=01 DOWN   GATO=(136,118)  \
f 739  mode=06 RETURN    facing=02 LEFT   GATO=(136,125)   |  RETURN is a CURVED retreat; facing
f 779  mode=06 RETURN    facing=00 UP     GATO=(128,125)   |  tracks EVERY segment:
f1018  mode=06 RETURN    facing=03 RIGHT  GATO=(128, 85)   |  DOWN -> LEFT -> UP -> RIGHT -> settle
f1057  mode=06           facing=01 DOWN   GATO=(135, 85)  /   >>> SETTLES facing DOWN (front, toward party)
f1151  SONG dispatch     facing=01 DOWN   GATO=(135, 85)      sings facing FRONT
```
Crono (the return target) sits at (134,147). GATO curves up-and-around and **settles facing DOWN (01) toward the
party** at the rest/song spot (135,85). Cross-checked cycle 2: song dispatched facing 01/02 — always **toward the
party (down/left/right), NEVER UP (away)**.

## 1.3 OURS — the divergence (battle.c `battleMoveReturn`, verified by running our engine)

`battleMoveReturn` (battle.c:1474-1493) computes ONE away bearing `away = atan2(Crono→GATO) ≈ UP (0xC0)`, sets
`mvFacing = ctBattleFaceTab[away] = 0 (UP)`, and walks GATO straight up to the ref. The two settle exits —
`if (mvStopped) return;` (1476) and `if (dot>=0) {…; return;}` (1486) — **return WITHOUT touching mvFacing**, so
it holds the last value = UP. `battleGatoRender` idle branch (field.c:2472) then draws `mvFacing[3]=0` = the UP
walk assembly = **back view**.

`CT_BATTLE_FACETRACE` (our engine, HOLD): the whole return is `7× "return facing=0 (UP)" bearing=C0`; MVTRACE
settle = `f956 GATO=(136,90) facing 0`. So GATO retreats facing UP **and idles/prepares-to-sing facing UP** at the
rest spot — the "walks backwards". (The oracle shows the back only during its curve's UP segment, then faces front.)

| | oracle | ours | gap |
|---|---|---|---|
| return path | curve DOWN→LEFT→UP→RIGHT | straight UP | back-view the WHOLE return (vs 1 segment) |
| **settle facing** | **DOWN (01), toward party** | **UP (00), away** | idles/sings with back turned |

## 1.4 FIX SPEC (item 1)

`battleMoveReturn`, both settle exits (battle.c:1476, 1486): **on settle set `h->mvFacing[slot] = 1`** (DOWN =
front, the idle default our code already inits to at battle.h `mvFacing[i]=1`, and the oracle's observed rest-spot
facing §1.2). Mandate-clean: our return is the Wave-3 sanctioned geometric approximation of the ROM octant
oscillation (the exact curve is the deferred C1/3AA0 port); the oracle's OBSERVABLE settle state is "facing the
party", so the approximation must reproduce that end-state (it already reproduces the ref *height*; this adds the
ref *facing*). This kills the idle/song back-view (the dominant, ~94f-long static "back turned") and — via §ITEM 4
— restores the flurry. FULLER fidelity (the during-walk curve so intermediate facings match) needs the octant
port (OPEN, Wave-3). ⚠ MASH pins unaffected: GATO's block2 counter has no movement, mvFacing stays 1.

---

# ITEM 2 — "cut off the top part; moves UP and sings at the SAME position": song rise + clip

## 2.1 CANONICAL — no pre-move; the RISE is the RETURN; consistent HEIGHT (oracle-measured)

The song entry is `02 10 05 00` with suffix `00 00` = **NO movement program** (battle_enemy_move_target §A.5).
GATO does NOT teleport to a fixed song spot — he **sings from wherever the RETURN left him**, and the return
always brings him UP to ~the same height:

| song dispatch | GATO pos | preceding attack |
|---|---|---|
| f1151 | **(135, 85)** | attack-A on Crono, from (136,118) |
| f2413 | **(123, 87)** | attack-A at (148,98) |

**Y is consistent (~85-87)**, a ~26px rise from the entry home (136,111); X varies with the preceding attack.
So the user's "he moves UP" = the return walk; "always at the SAME position" ≈ the consistent HEIGHT (~85). The song is
sung facing FRONT (item 1) with the gentle claw sway. Oracle GATO song cluster (OAM, f1290): 12 body cells,
top tiles `8E/8C @ OAM y29` → **fb top ≈ y37** (with the +8 battle-OBJ scanline), foot ≈ fb y101.

## 2.2 OURS — height VARIES with the target; renders too high; the clip site

Our song foot = `111 + gdy` (field.c:2400), `gdy` tracking the live position via `battleEnemyLiveDelta` — so the
song follows the return-settle height. Because our return settles at **target-dependent** spots, the song height
VARIES (CT_BATTLESHOT_AT render captures, GATO body top row measured):

| our song | GATO body top (fb y) | vs oracle y37 |
|---|---|---|
| song-1 (post-Crono attack) | **y39** (foot y90) | ~ok |
| song-3 (post-Marle attack) | **y26** | **13px HIGHER** |

The song sway poses (E3 idx 0x0D/0E/0F, `animeng_e3_gato.h`) reach **dy = -56** = 56px above the foot (matching
the oracle). So the top pixel = `footY - 56`. When the return over-rises (post-Marle: foot y77 → top y26; a
higher-Crono real-play geometry rises further), the top marches toward and past `y0`. The **clip SITE** is
`plot()` (field.c:693: `if (x>=0 && x<CAMW && y>=0 && y<CAMH)`) — any pixel at `y<0` is silently dropped. Render
comparison (`our_song1150.png` vs `oracle_song.png`): ours sings visibly HIGHER, top near the frame edge;
oracle sings mid-arena with clear headroom.

> Honesty note: in `--battletest` (Crono staged low at y165) I reproduced the height VARIANCE and the too-high
> render (top y26), but not a hard `y<0` frame-clip; a hard clip needs the return to over-rise more (real-play PC
> geometry / a larger overshoot). The MECHANISM (return over-rise + a 56px-tall pose + the `plot` y<0 drop) is the
> clip; no separate clamp is the right fix.

## 2.3 FIX SPEC (item 2)

**No new clamp** — the pose reaching 56px above the foot is CORRECT (byte-matches the oracle). The fix is the
RETURN settling at the oracle's consistent height (the item-1 return work): make the return settle reproducibly
at the ref (`target + step(awayBearing,0x40)`) without the segment-boundary over-rise, so the song foot is
consistent (~oracle Y85) every cycle → the pose top lands at ~y37, never near the edge. Consider also whether our
battle render applies the oracle's +8 battle-OBJ scanline (our top y34 vs oracle y37 for the same foot suggests a
~3-8px upward bias). ⚠ ties to item 1 — one return fix resolves both.

---

# ITEM 3 — "Marle does a startled animation": the song TARGET flinch (DERIVED)

## 3.1 CANONICAL — the targeted PC switches to a distinct FLINCH pose (oracle OAM)

The song resolves a random-PC target (`tgt=05`); in gato_press_action it hits **Marle** (moveset §C). Marle's
cluster (pal6) across the song (OAM decode):

```
idle  (f450, pre-song)     Marle = 20@(3,0) 24@(16,15) 22@(0,16) 26@(0,32)   [assembly 0xDA = her stance]
song-start (f1165, rel14)  Marle = SAME (still idle)
song-mid   (f1230, rel79)  Marle = 22@(0,0) 20@(16,0) 26@(0,16) 24@(0,32)    [FLINCH -- distinct layout]
```
The flinch `22 20 26 24` (compact) is **byte-identical to the corpus §4.1 hurt-reaction pose** (`022 020 026
024`). Non-target **Crono is UNCHANGED** (`0E/0A/08/0C` all through). So this is the general "targeted PC reacts"
mechanism (also fires on physical strikes, corpus §4.1) — not song-specific, but most visible in the song since
there is no hurt-flash (song has 0× `1E`, moveset §B.3).

## 3.2 The trigger + the pose id (`$9907` monitor, writer PCs)

Flinch onset narrowed to **rel ~41-42** (idle at f1185/rel34, FLINCH at f1192/rel41) — **coincident with the
spotlight fade-in (rel42), the note spawn (rel44), and the sway (rel45)**. i.e. the song's mid-span EFFECT phase
arms them together (the bank-C3/FD tech-effect subsystem, same launcher as the spotlight — song_spotlight §B.3/
E.1). At f1193 `$9907[1]` (Marle's cached assembly index, writer C1:1C69 = the builder) flips **0xDA (idle) →
0x5B (flinch)** and HOLDS 0x5B through the effect window. So **Marle's flinch = pose/assembly id 0x5B**. NOT tied
to the end-popup (the `50` ShowDamage births at rel~248, far later).

## 3.3 OURS + FIX SPEC (item 3)

Our `ctAnimengE3Marle` (animeng_e3_pc.h) has only idle 0xDA + attacks 0x37/8C/8D/D9 — **no 0x5B flinch**. During
the song our engine renders the PCs in their idle stance (battleStanceRender), so Marle never reacts.

**FIX:** (a) bake Marle's flinch assembly **0x5B** (Marle base DC/9AD8, the corpus §4.1 tiles `22 20 26 24`) into
`ctAnimengE3Marle` + a window entry in `animeng_e4_pc.h`; likewise Crono's react pose (his §4.1 guard pose
`00A/008/00E/00C`) for when the song targets him. (b) In the song render path, draw the resolved target PC
(`h->actTarget`) in its flinch assembly during the effect window (**rel 42-236**, the same gate as
`battleSongFxRender`), reverting to idle at/after span end. The PC pose engine is already live (stage 3), so this
is one extra pose + a target-gated select. ⚠ generalizes to any enemy action's target (corpus §4.1) — but the
song is the item-3 scope.

---

# ITEM 4 — "did not perform the multiple-punch attack": the flurry (TRIAGE)

## 4.1 The flurry LOGIC + RENDER both work (MASH)

`CT_BATTLE_MASHA=1` (GATO hit every turn → block2): attack-B (`$9877=0x34`, script CD/7B72) fires **6×**; each
dispatches with `span=224 play=20` (the flurry track). The render (field.c:2439-2451, `if $9877==0x34` FRONT
branch) alternates `CT_ANIMENG_GATO_FLURRY_FIST/RETRACT` with a white flash, 7 cycles over 49f. CT_BATTLESHOT_AT
captures:
- **f814** (retract): red rear-up pose, 875 body pixels, 135 white.
- **f819** (fist): **2025 white-flash pixels** — GATO a full white silhouette, arms up.

`flurry_814.png`/`flurry_819.png`: the fist/flash ↔ retract cycles render cleanly (engine poses, post-stage-2/3).
**So the flurry is NOT a regression** — when it runs, it shows the multi-punch.

## 4.2 …but item-1's stuck facing SUPPRESSES it in mixed/interactive play (verified)

The flurry branch (field.c:2439) is only reached when `fac==1` (line 2423 `fac = mvFacing[3]`; line 2424
`if (fac!=1) { …directional rear-up…; return; }`). After a block1 action (attack-A or song, which walk+return),
item-1 leaves `mvFacing[3]=0` (UP). Block2 (attack-B) arms NO movement → never resets it. So a subsequent attack-B
renders the **directional UP** rear-up, not the front flurry.

Mixed fight (`n:1000` idle → GATO does block1 → then mash), FACETRACE+TRACE at each attack-B dispatch:
```
attack-B f1949: mvFacing[3]=0 (UP) -> DIRECTIONAL branch -- NO flurry
attack-B f2574: mvFacing[3]=0 (UP) -> DIRECTIONAL branch -- NO flurry
attack-B f3053: mvFacing[3]=0 (UP) -> DIRECTIONAL branch -- NO flurry
```
Every attack-B in mixed play draws a back-turned UP rear-up instead of the flurry — **this is why the user never
saw the multi-punch.** (Pure MASH keeps facing 1 from the start → flurry shows; a passive player never triggers
attack-B at all — block2/when-hit only. Both hide it, for different reasons.)

## 4.3 VERDICT + FIX SPEC (item 4)

**Not a flurry regression — CANONICAL-BUT-SUPPRESSED by item 1.** The canonical attack-B is a block2 counter GATO
performs at his HOME facing FRONT (the oracle idles facing the party, item 1), so the ROM always shows the front
flurry. **Fixing item 1 (settle facing = 1) makes `mvFacing[3]=1` at every counter → attack-B re-enters the front
flurry branch → the multi-punch shows.** No flurry-code change needed. Secondary fidelity note: our fist frame is
a FULL-body white swap (`ctBattleFxFlashPal`) — the sanctioned representation of the CD/7B8F `69 00`/`6A`
palette-flash (moveset §B.2, the C1/CC boundary); acceptable, could be softened later.

---

# CONSOLIDATED FIX ORDER (one structural change unlocks 3 of 4)

1. **`battleMoveReturn` settle re-faces the party** (`mvFacing[slot]=1` on both settle exits, battle.c:1476/1486)
   → fixes **item 1** (idle/song back-view), unblocks **item 4** (flurry front branch), and — with a consistent
   settle height — bounds **item 2** (song no longer over-rises). ⚠ re-run `field-battle-gato-walk` + the
   HOLD/MASH pins; MASH stays byte-identical (block2 = no walk, facing already 1).
2. **Item 2 residual**: verify the song foot lands at the oracle height (~Y85) after (1); check the +8 battle-OBJ
   scanline bias. No new clamp.
3. **Item 3** (independent): bake Marle assembly 0x5B (+ Crono react) into `animeng_e3_pc.h`/`animeng_e4_pc.h`;
   render `h->actTarget` in the flinch during the song effect window (rel 42-236).

## OPEN / deferred
- The exact return CURVE (octant oscillation C1/3AA0-3AF5) so the *during-walk* intermediate facings match — the
  Wave-3 deferred port. The settle re-face (fix 1) resolves the user-visible idle/song back-view without it.
- The bank-C3/FD op that arms the target flinch at rel42 (same subsystem as the spotlight, song_spotlight E.1).
- Crono's react assembly id (only Marle's 0x5B pinned here; the song hit Marle in this state).

## Scratchpad artifacts (NOT committed)
Dir `%TEMP%/claude/…/e5608df4-…/scratchpad/polish/`:
- Oracle `../ct_ae.exe`; states `../gato_press_action.state`.
- `hold_facing.txt` (item-1 facing/mode/pos timeline), `our_facing.txt`/`our_song.txt`/`our_trace.txt`/
  `mixed.txt` (our-engine FACETRACE/MVTRACE/TRACE runs), `mash_trace.txt` (flurry fires).
- `our_song1150.png`+`oracle_song.png` (item-2 song render compare), `our_song3_marle.png` (varying height),
  `flurry_814.png`/`flurry_819.png` (item-4 retract vs fist-flash), `pc_*.raw.oam`/`on_*.raw.oam` (item-3 Marle
  flinch onset OAM).
- Repro: item1 `ct_ae.exe --monitor "96DE:8,974A:8,9877:1,A013:1,1D12:1,1D29:1" --from gato_press_action.state
  --btnscript "100:8,0:1450" --diff`; ours `CT_BATTLE_SCRIPT="n:1300" CT_BATTLE_FACETRACE=1 ct_field_ae.exe
  --battletest --assets ct_assets.dat`. item2 `CT_BATTLESHOT_AT=1150 CT_BATTLESHOT=out.raw` +
  `ct_ae.exe --peek … --btnscript "100:8,0:1282"`. item3 `--peek … "100:8,0:<f>"` + OAM decode (tools/ctlib/oam).
  item4 `CT_BATTLE_MASHA=1` (fires) vs `CT_BATTLE_SCRIPT="n:1000,a:8,n:24,…"` (suppressed).
