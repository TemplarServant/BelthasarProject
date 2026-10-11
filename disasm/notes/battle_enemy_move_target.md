# BATTLE — enemy MOVEMENT (GATO WALKS — mechanism derived) · ATTACK-CHOICE (hit-reactive; script move-suffix grammar) · DEAD-PC TARGETING (random living PC, re-roll)

Derives, from the ROM disasm + LakeSnes oracle **only**, the enemy-behavior truths behind two user reports (translated from Portuguese)
(GATO fight, Crono seat0 + Marle seat1):

1. *"GATO is not moving properly … he only uses one of his attacks because he is close"*
2. *"GATO is attacking Crono even after he has already fainted (hp 0)"*

> ⚠ **CORRECTION (supersedes this dossier's first revision).** The first revision concluded "GATO position —
> byte-STATIC, no relocation". **REFUTED BY USER STATE** `C:\Users\Icaro Ferris\Desktop\aux crono decompile\
> GATO_moving.state` (snes9x, GATO mid-relocation in a real fight). The negative was measured on the WRONG RAM —
> the staged formation record `$29BF+12·slot`, which is written ONCE at battle entry (C0/045A) and never again,
> so its constancy proves nothing about the live position (the project's documented false-negative trap,
> [[verify-negatives-on-proper-state]]). §A now derives the real movement mechanism byte-by-byte.
> **Why every prior negative happened:** (a) my `$29BF` monitor = wrong RAM (the LIVE position is `$1D0C/$1D23`,
> §A.2 — re-checked: in that same fight GATO's live Y went 111→118→85…, he WAS walking); (b) the prior lane's
> idle captures patched `$AFB1=FF` so GATO never ACTED → never walked; (c) MASH fights keep GATO in his when-hit
> block2 whose counter (attack-B) carries NO movement program (§B.2) → he genuinely does not walk in mash
> fights, which is all the early ledgers sampled.

**HEADLINE FINDINGS.**
- **(1) GATO RELOCATES, canonically:** before his block-1 PHYSICAL he **walks to the target** (approach,
  ~0.2px/f), strikes when close, then **walks away** (return); his SONG is performed **without moving**. The
  whole walk runs **outside the `$A013` action span** (the ATB keeps ticking through it) and the attack
  dispatches only once he is near the victim. Movement is driven by **movement-program suffix bytes inside the
  enemy's own AI-script entries** (a grammar the earlier T1.A/T1.B decode missed), through a per-slot mode
  machine + a trig mover — every link writer-PC-cited in §A.
- **(2) Attack choice remains HIT-REACTIVE** (block1 A/SONG un-hit · block2 B when-hit) — re-verified on the
  moving-state fight. The user's "only uses one … because he is close" is explained canonically: the PHYSICAL is
  the close-range walk-in attack; the counter (B) and the SONG fire from wherever he stands. The `1F 06`
  distance condition still never varied in any capture (OPEN, now plausibly live since positions do change).
- **(NEW DEFECT in our port):** our AI cursor executes the movement-suffix bytes as ACTIONS — GATO's
  `01 00 00` suffix runs as a second damaging attack (ours: A5, **A0(−13!)**, SONG; oracle: **A5, SONG**).
  §B.4/§D.3.
- **(3) Dead-PC targeting:** the ROM excludes dead PCs by **RE-ROLL on `curHP==0`** (`C1/A452`/`C1/B279`);
  oracle-proven that a dead Crono is never hit. **Our engine already excludes dead PCs** — the reported bug does
  not reproduce; the real (smaller) gap is targeting distribution (§C).

Method: user state converted per `.claude/skills/capture-oracle-state` (`tools/s9x_to_lake.py` onto the
in-battle donor `gato_mid.state`; WRAM off 0x45; render verified visually — GATO standing beside Crono at f2);
oracle `--peek` (renders) + `--monitor --writer` (per-frame RAM; ⚠ peek≠monitor timelines, aligned by
`$A013`/`$9877` events); ROM `tools/dis.py` + raw-byte hand-decode where the linear decoder desyncs (M/X flips).
Writer PCs = the PC after the store retires.

---

## A — GATO MOVEMENT : the derived mechanism (approach → strike → walk-away; song from standstill)

### A.1 The observed behavior (converted user state `gato_moving_lake.state`, GATO un-hit throughout)
Peek series (`gm_f2/30/60/90/200/400.png`): at f2 GATO stands ADJACENT to Crono mid-arena (the user's saved
moment = mid-excursion); he then walks back up and idles. Monitor timeline (OAM-cluster min-corner of his 12
body cells, `gm_mon.txt`; positions in OAM space):

| span | what | position | `$A013` | `$9877` |
|---|---|---|---|---|
| f0→f209 | **RETURN walk** (finishing the excursion the user saved) | (102,66)→(96,31), ~1px/5.5f | **0** | 31 (stale) |
| f209-f372 | idle at rest spot | (96,31±1) | 0 | — |
| f373→f685 | **SONG** — dispatched FROM the rest spot, **no approach** | in place (pose only) | **1** (311f) | 0x10 |
| f738→f922 | **APPROACH walk** to Crono | (96,30)→(96,65), ~1px/5.3f | **0** | — |
| f964→f1121 | **ATTACK-A** dispatched ADJACENT; strike lands (Crono −10 at span end) | at (96,64) + anim excursion | **1** (157f) | 0x31 |
| f1143→f1357 | **RETURN walk** | (96,65)→(96,26) | **0** | — |
| f1632→f1941 | SONG again (from rest; Marle −1) → f2008 approach → f2257 ATTACK-A … | cycle repeats | — | — |

Corroborated on the PRISTINE `gato_press_action` fight (`pristine_walk.txt`, live-Y `$1D29`): mode=1 approach
f380-478 (y 111→118) → attack-A f478 → mode=6 at f638 → return (y→85) → SONG f1151 from the away spot. **The
"return" is NOT to a stored home: it walks AWAY from the target until a stop test** (rest spots varied: y=111
staged, then 85; converted state: 31, then 24) — see the away-bearing + boundary check in A.4.

### A.2 The LIVE battle position (the RAM my first pass missed)
Per-combatant live coordinates: **X = `$1D0C+cmb` · Y = `$1D23+cmb`** (cmb = combatant idx 0..10; PCs 0-2,
enemies slot+3 → GATO cmb 6 = `$1D12`/`$1D29`). Found by whole-WRAM monotonic scan across the walk, confirmed by
writer tracing. Per-frame mirrors: `$A039,X`/`$A050,X` copied by the loop **C1/283D** (`LDA $1D0C,X/STA $A039,X;
LDA $1D23,X/STA $A050,X`, X=0..10) — called from the battle tick (`C1/10B9`,`C1/10E0`) AND the action loop
(`C1/40C9`), so battle logic always sees fresh positions. Derived per-combatant deltas `$975A,X`/`$9765,X`
(|pos−ref| clamps, C1/285A) + `$977B,X` feed the distance/targeting readers (`$986E/$9870` scratch, writers
`C1:2F5E`/`C1:2A5B`/`C1:29BE`; the cond-`1F` handler C1/9765 reads these). The staging record `$29BF+12k` is
written once (C0/045A) and never updated — it is the ENTRY anchor only.

### A.3 The mover (executor): CF/F978 — base + trig(angle)·phase, 8-phase segments
Per enemy slot X=0..7, gated `$98A7,X`!=0 (active), cadence `DEC $9819,X` (reload `$9742,X`):
```
CF/F98F  $9737,X += $98C2,X            ; PHASE 0..8 (CMP #08 → STZ $98A7,X = segment done)
CF/F9A5  A=$9786,X   → JSR CF/F9FB     ; trig: idx=angle*4 → table C0/F900 (quarter-reflected),
CF/F9AB  $C8 = result                  ;   × phase (hw multiply $4202/3, >>8) = scaled offset
CF/F9B0  A=$9786,X+0x40 → F9FB → $C9   ; the +90° component
CF/F9C2  $1D26,X = $98B7,X + $C8       ; live Y  = baseY + trig(angle)·phase>>8    (writer CF:F9C5)
CF/F9CB  $1D0F,X = $98AF,X + $C9       ; live X  = baseX + trig(angle+40)·phase>>8
```
($1D0F/$1D26 = the enemy rows of $1D0C/$1D23, +3.) One segment = phase 0→8 ≈ 8px along `angle` over ~40f
(observed: re-arm every 40f, ΔY 7-8px/segment, net **~0.2 px/frame**); long walks chain segments, each re-based.

### A.4 The per-slot MOVEMENT-MODE machine (who arms the segments)
Outer per-frame loop **C1/3714**: gated **`LDA $99E0 / BNE skip`** (the Wait-mode submenu freeze also freezes
enemy walking); per slot: `$974A,X`==0 → none; `DEC $9897,X`, reload `$989F,X` (the 40f segment cadence) → run
**C1/373B**: refresh the movement TARGET **`$9752,X` ← `$5E15,X`**; mode = `$974A,X` (bit7=halted) → class =
ROM **`CC/FBAB[mode]`** (`00 01 01 01 00 01 04 02 03 03 06 06 06 05 05 05`) → **`JSR ($3760,X)`** handler table:
```
C1/3760: [0]=C1/3772 (watch/idle)  [1]=C1/379A APPROACH  [2]=C1/38BF  [3]=C1/3959
         [4]=C1/3A3D RETURN/AWAY   [5]=C1/3BC2           [6]=C1/3E33  [7]=C1/3F5A
```
- **APPROACH (mode 1 → class 1, C1/379A):** if not walking: re-base `$98AF/$98B7 ← $1D0F/$1D26` (own pos),
  read the target cmb `$9752,X` → target pos `$1D0C,Y/$1D23,Y`, **angle = atan2(target−self) (JSR C1/0222)** →
  `$9786,X`; step/facing via trig `C0/F700` (C1:37D7 region); arm `$98A7`. Arrival = proximity test
  (`JSR C1/28B0` on the `$975A/$9765` deltas vs a range, C1/3888) → snap facing to the 0x40 grid, stop arming.
  Chases the LIVE target position (re-based every segment).
- **RETURN (mode 6 → class 4, C1/3A3D):** same shape but the atan2 operands are SWAPPED (target pos loaded
  into $D3/$D4 first, own pos second, C1/3A4F-3A6A) = **bearing AWAY from the target**, with a
  boundary/limit stop test (the sign checks at C1/3AA0-3AF6) — hence the variable rest spots (A.1).
- The mode machine (**C1/334x**): live mode `$974A,X` ← REQUEST **`$5E0D,X`** on change (writer **C1:336A**),
  also reloading the slot's anim-script pointers (`$1CC3+slot·3`, `$1CA2+slot·3` → the walk-pose anim tracks).
  Observed mode values: **1 = approach, 6 = return, FF/bit7 = halted** (battle-load state).

### A.5 WHO requests the movement: the AI script's MOVE-SUFFIX bytes (grammar correction)
The requests are written by **AI-interpreter handlers** — condition-table entries **cond-0x29 → C1/9810** and
**cond-0x2A/0x2B → C1/983A** (table C1/B80D) — plain 2-arg readers of the bank-CC script at cursor `$B1D2`:
```
C1/9810: ++$B1D2 → JSR C1/AC14 (TARGETING value at cursor → resolved target $AECC)
         ++$B1D2 → LDA $CC0000,X → $AEE5      ; the MODE byte
         X=$B252 (slot): $5E0D,X ← mode (C1:9833) ; $5E15,X ← target (C1:9839)
C1/983A: same + a candidate scan that AVOIDS the last target ($B23A,X vs the $AECC,Y list)
         (tech variant path CMP #02 → stores the target to $B16E,X instead)
```
Oracle cursor capture (`gm_cursor.txt`) ties every request to script bytes inside **GATO's own script CC/A77B**:

| frame | event | `$B1D2` lands | bytes consumed | effect |
|---|---|---|---|---|
| f685 | SONG span ends | **CC/A783** (writer C1:987D) | `05`@A782 → targeting; `01`@A783 → **mode 1** | approach armed toward the resolved PC (`$5E15`←0) |
| f961 | attack decode at dispatch | CC/A782 (C1:9A1E) | `01 00 05`@A780 → `$AEE3-E5` | the physical dispatches (adjacent) |
| f1121 | ATTACK span ends | **CC/A787** (writer C1:9821) | `06`@A786 → targeting; `06`@A787 → **mode 6** | return armed |
| f370 | SONG decode | CC/A78C (C1:9AC9) | `02 10 05 00`@A788 | song; its following `00 00` = **no movement** |

So GATO's autonomous section, whose T1.B reading was "ATK-A idx5, ATK-A idx0, 06, 06, SONG, 00, 00", actually
parses as **entries with MOVEMENT-PROGRAM SUFFIXES**:
```
CC/A780:  01 00 05  [01]  [00 00]  [06 06]     ; PHYSICAL: op,p1,tgt=05(random-PC) + pre-move mode 01
                                                ;   (+2 undecoded bytes) + post-move tgt 06, mode 06
CC/A788:  02 10 05 00  [00 00]                  ; SONG: op,p1,tgt,x + NO movement (modes 0)
CC/A78E:  FE
```
The move-suffix consumption happens at the surrounding actions' SPAN-ENDS (the pre-move of the NEXT action is
consumed when the PREVIOUS one finishes — f685 armed the approach for the attack dispatched at f964). The
**turn cycle is therefore [ATK-A(idx5), SONG] repeating** — there is no "attack idx0" turn. Cross-checked on the
pristine HOLD fight (`ab_hold.txt`): damage/decision events f78 A, f751 SONG, f1341 A, f2013 SONG — **A,SONG
alternation, never A-idx0** ✓. (⚠ This CORRECTS `battle_interp_verify.md` T1.A/T1.B: the action lengths 3/4 are
the DISPATCH-time reads only; entries carry additional movement/targeting suffix bytes, and the `06`/`00` bytes
in GATO's list are NOT standalone no-op actions. The exact full entry layout — the 2 bytes between the pre-move
mode and the post-move pair — is OPEN, E.1.)

### A.6 Dispatch gating + what the walk does NOT do
- The whole walk runs at **`$A013`=0**: the ATB keeps counting through it (the approach f738-922 sits inside
  GATO's 105-tick recharge; his dispatch f964 ≈ arrival f922 + ~40f). The physical **never dispatches
  mid-walk** — in every capture it fires only once the approach has stopped near the victim (the C1/28B0
  proximity test is the arrival criterion). Exact dispatch gate (arrival-flag vs ATB-order) = OPEN E.3.
- Movement does not touch the staging record (`$29BF+12k` constant all fight — first revision's §A data, still
  true) nor the combatant stat block (`$612D+…` has no coordinates; verified by walk-window diff).
- In MASH fights GATO is always in block2; his counter entry `01 01 06 [00]` carries **no movement program** →
  he stays put — which is why every earlier mash ledger read "static" and why span 224 needs no walk time.

---

## B — ATTACK CHOICE : hit-reactive (unchanged), now with the movement half of the user's report explained

### B.1 The rule (re-verified on the moving state)
`$AE93` = the attack action's `p1` (C1/AD02 `LDA $AEE4/STA $AE93`, writer C1:AD08); anim dispatch C1/44AF:
`$AE93`==0 → table CD/4926 → `$9877=0x31` (A); ≠0 → CD/4F26 → 0x34 (B); song via C1/469A → 0x10. Block choice =
**damaged-since-last-turn** (block1 autonomous / block2 when-hit). In the moving-state fight GATO was NEVER hit
(HP 76 constant) → **all block1** (A5/SONG cycle, §A.5) — consistent. MASH correlation (survivor state,
`ab_mash.txt`): every attack-B is immediately preceded by a GATO-HP drop; every A/SONG is un-hit. **Distance
does not drive A-vs-B.**

### B.2 The user's "only uses one … because he is close", reconciled
- In normal play the player hits GATO every round → block2 → **counter B every turn, no movement** — one attack,
  stationary. Canonical.
- Left alone, GATO alternates the walk-in PHYSICAL (A) and the stand-still SONG. The "being close" intuition
  maps to the real mechanism: **the physical is a close-range attack he walks into**; B and the SONG are cast
  from position.
- The block2 `1F 06` distance condition STILL never varied in any capture (every hit → B; the message fallback
  never ran). With positions now proven dynamic it is plausibly live in general — but un-derived (OPEN E.4);
  keep it always-pass, per data.

### B.3 Our port: the A/B/song selection is byte-correct…
Our MASH → both GATO turns B (`9877=34, AEE3=01,01,06`); our HOLD → A5, …, SONG cycling with real `$AE93`
(`our_mash.txt`/`our_hold.txt`). The hit-reactive block choice + `$AE93` chain need no change.

### B.4 …but the CURSOR GRAMMAR has a REAL defect (new, confirmed)
Our `battleAiRunTurn` (src/battle.c:386-453) parses GATO's list as standalone actions `[A5][A0][06][06][SONG]
[00][00]` and **executes the move-suffix bytes `01 00 00` as a second ATTACK (idx0) that deals damage**: our
HOLD cycle is A5, **A0(−13 to Crono, f827→f984)**, SONG — the oracle's is **A5, SONG** with no damage between
(§A.5). One in every three of our autonomous GATO turns is an invented attack. This is the concrete
action-cursor defect to fix (D.3); the fix and the movement port share the grammar work.

---

## C — DEAD-PC TARGETING : random living PC via RE-ROLL; our engine already excludes dead PCs

### C.1 Canonical mechanism (unchanged from rev-1, still valid)
Targeting dispatch `JSR ($B8BB,X)` (C1/AC2E; table C1/B8BB). **Random-PC = C1/A452** (value 5): eligibility per
seat via **C1/B279** — `LDA $5E30,X / BEQ dead` (**curHP==0** → set dead flag block+0x1D bit7 → mark `$AEFF,Y`
ineligible) — then `LDA #$63/JSR C1/AF22` roll partitioned in thirds (0x21/0x42), **`LDA $AEFF,Y / BMI
re-roll`**. Resolved seat → **`$B1F6 = seat·0x80`** (C1/E9D4). Value 6 ("nearest") = C1/A4AF (its `$986E` write
C1:A4C0 is seen at GATO's attack dispatches — with the live-position model of §A.2 now known, "nearest" is a
real spatial pick over the `$975A/$9765` deltas).
- **Refinement of rev-1's E.1:** GATO's physical resolves its PRE-MOVE victim from the script's own targeting
  byte (`05` = random-PC, consumed by the cond-2A handler at pre-decode — which also SCANS to avoid repeating
  the last target, `$B23A,X`, C1/984C-986E) and then walks to that victim; the dispatch-time resolution observed
  is the val-6/NEAREST flavor (C1:A4C0) — self-consistent, since after the approach the nearest PC IS the
  pre-move victim. The song's `tgt=05` picks a random living PC directly (hits mixed Crono/Marle ✓).

### C.2 Oracle proof (dead exclusion)
- `gato_crono_dead.state` (Crono curHP=0 + flag): every GATO action lands on Marle (65→46…); Crono stays 0,
  never re-targeted (`dead_crono.txt`).
- `gato_crono_lowhp.state` (natural kill): after Crono hits 0 (flag set by the apply path), all subsequent GATO
  actions target Marle (`nat_death.txt`).

### C.3 Our engine: dead-exclusion CORRECT; distribution + pre-move victim are the gaps
`battleLeadPc`/`battleAlive`/`battleAiTarget` (src/battle.c:287-377) all honor the dead flag; fragile-Crono run
(`our_frag.txt`): GATO kills Crono f415, then 7 straight actions on Marle, cronoHP stays 0 → **the reported
"attacks dead Crono" does not reproduce** (dead-check present since commit b73f0fe). Remaining fidelity gaps:
(a) our enemy ATTACK always targets the LEAD living PC (battle.c:1151) where the ROM picks a random living PC
(with last-target avoidance) at pre-decode; (b) we have no pre-move victim concept at all (no movement model).

---

## D — IMPLEMENTATION SPECS (derive-only lane; nothing applied)

**D.1 Enemy battlefield MOVEMENT (new subsystem — the user-visible fix).** Our engine renders GATO at a fixed
computed anchor (staged `16·rel+fine` + idle/attack bakes; `battleGatoRender`, field.c). Port the §A model:
- **State per enemy slot:** live position (init = staged anchor), movement mode (0 none / 1 approach / 6
  return / bit7 halt), movement target (combatant idx), segment phase+cadence. (Mirrors `$1D0C/$1D23`, `$974A`,
  `$5E0D/$5E15→$9752`, `$9737/$9819`.)
- **Mover tick** (every render frame, gated `$A013==0` AND the Wait-submenu gate `$99E0==0`): per §A.3/A.4 —
  approach: angle=atan2(targetLive−selfLive), advance ~0.2px/f net (the ROM's exact shape = 8-phase trig
  segments at 40f cadence, trig C0/F900; a faithful port should reproduce the segment structure, not just the
  net rate), stop within the C1/28B0 range; return: reverse bearing + stop rule (E.5 pins the exact limit test
  before byte-exactness is claimed).
- **Program arming from the AI script** (with D.3): physical entry = pre-move(1, victim) armed at the PREVIOUS
  action's span-end, post-move(6) at its own span-end; song = none. Dispatch of the walk-in physical WAITS for
  arrival (§A.6).
- **Render:** draw GATO at the live position (idle bake while idle; the walk uses distinct walk poses — visible
  in `gm_30/90.png` — whose bake/anim-script capture is new work, E.6). The hurt-flash/knockback/popup anchors
  must switch from the staged anchor to the live position.
- Validation targets: the §A.1 timeline (approach 35px ≈ 190f; dispatch-adjacent; return; song in place) on
  `gato_moving_lake.state` + the pristine fight's mode/position trace (`pristine_walk.txt`).

**D.2 Attack choice: NO CHANGE** to the hit-reactive block/`$AE93` chain (B.1/B.3). Keep cond-`1F` always-pass
(B.2) until E.4 derives its formula.

**D.3 AI-cursor GRAMMAR fix (defect).** Stop executing movement-suffix bytes as actions: after an executed
action entry, consume its suffix (physical: pre-move mode + 2 undecoded + post-move pair; song: 2 mode bytes)
into MOVEMENT-PROGRAM state (D.1) instead of treating them as next-turn actions. Acceptance: our HOLD GATO cycle
becomes **A5 → SONG → A5** (no damaging idx0 turn), byte-matching `ab_hold.txt`/`gm_mon.txt`; MASH unchanged
(B every turn). ⚠ Re-pin `field-battleloop-*` after (turn pattern changes shift victory frames/HP).
⚠ Before generalizing, re-segment the OTHER three scripts (Hetake/Beetle/Avian) under the suffix grammar (E.1)
— do not hand-tune GATO-only lengths (that would be a scene-gate-class violation).

**D.4 (optional fidelity) Enemy-attack victim = random living PC** with last-target avoidance (C.1), replacing
lead-PC at battle.c:1151 — properly it becomes the PRE-MOVE victim resolved at pre-decode (D.1/D.3 integration).
Keep the RE-ROLL shape (thirds at 0x21/0x42 on `battleRng(...,99)`; re-roll on dead) so the RNG stream tracks
the oracle. Re-derive pins after.

**D.5 Dead-PC exclusion: NO CHANGE — already correct** (C.3).

---

## E — OPEN questions
- **E.1 Movement-entry byte layout** — the 2 bytes between the physical's pre-move mode and the post-move pair
  (`00 00` at CC/A784-5), and the generic entry grammar for other enemies (Hetake/Beetle/Avian scripts need
  re-segmentation under the suffix grammar; their `04`/`0E`/`08`/`0A` bytes may likewise be suffix data, which
  would retire T1.A's "unknown ops" note). Decode via the cond-29/2A handler paths + per-enemy cursor captures.
- **E.2 Full C1/9810 vs C1/983A semantics** (29 vs 2A/2B: which entry types use which; the tech path storing to
  `$B16E,X`; the last-target-avoid scan details).
- **E.3 The dispatch gate** — arrival-flag vs ATB interplay (dispatch ≈ arrival+40f observed twice; reload-bound
  in the song case). Pin before claiming byte-exact turn timing with movement.
- **E.4 cond-`1F` distance formula** (C1/9765 over `$986E/$9870` + arg): never varied in any capture; now
  plausibly live. Derive by hitting GATO at controlled distances (e.g. patch his live pos mid-fight).
- **E.5 The return-walk stop rule** (the C1/3AA0-3AF6 sign/boundary tests) — rest spots varied (85/111/24/31);
  derive the exact limit before porting the return byte-exact.
- **E.6 Walk-pose anim bake** — the mode machine reloads per-slot anim-script pointers (`$1CC3/$1CA2+slot·3`);
  the walking GATO's poses need capture (like the idle/attack bakes).
- **E.7 `$B1F6` 3-seat/enemy-target encoding sweep** (rev-1 carry-over).
- **E.8 All-PCs-dead defeat** (rev-1 carry-over): our wipe run ends in defeat; the ROM's `C1/813F` live defeat
  path remains uncaptured.
- **E.9 Other enemies' movement** — Hetake leap / Beetle / Avian: same mode machine (the 8-slot loop is
  generic); their scripts' suffixes + mode values unexplored.

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/e5608df4-34bf-4aa1-b49d-5255ac6f07ac/scratchpad/`
- `gato_moving_lake.state` — the CONVERTED user proof state (splice: `python tools/s9x_to_lake.py
  "…\aux crono decompile\GATO_moving.state" scratch/gato_mid.state scratch/gato_moving_lake.state`; donor =
  in-battle `gato_mid.state`; verified loc 006 + visual).
- Peeks `gm_f2/30/60/90/120/160/200/260/320/400/500.png` (+.raw/.wram/.vram/.oam sidecars) — the walk, visually.
- Monitors: `gm_mon.txt` (OAM cluster + action state, 2400f) · `gm_writers.txt` (position writers) ·
  `gm_arm.txt` (segment arming) · `gm_mode.txt` (mode writer C1:336A) · `gm_req.txt` (request writers
  C1:9833/9891) · `gm_cursor.txt` (AI cursor at the requests) · `pristine_walk.txt` (pristine-fight walk).
- Rev-1 artifacts (still valid): `pos_hold.txt` (staging constancy), `ab_hold/ab_mash/dist_hold/dist_pristine`
  (A/B + targeting), `dead_crono/nat_death` (dead exclusion), `our_mash/our_hold/our_hold_long/our_frag`
  (our-engine runs), states `gato_mid/gato_survivor/gato_crono_dead/gato_crono_lowhp.state`,
  `gato_t0_cronofragile.raw.wram`.
- Repro commands inlined per section.

---

## ADDENDUM (Wave 2 implementer — disasm refinements to §A.5/§D.3/E.1/E.5)

**IMPLEMENTED (STEP 1, src/battle.c): the AI CURSOR-ADVANCE rule = FD/BA4A[op].** The dossier's §A.5
"grammar correction" is now byte-derived and shipped. The ROM's action executor reads the opcode at cursor
`$B1D2` (C1/8CAC `LDA $CC0000,X`) and dispatches `JSR ($B85F,X)` (opcode*2, table **C1/B85F**); the entry's
byte length is **`FD/BA4A[opcode]`** — proven both at **C1/9908** (`LDA $CC0000,X / TAX / LDA $FDBA4A,X /
ADC $B1D2 / STA $B1D2`, the skip handler's per-entry advance) AND by clean segmentation: every action-list in
ALL FOUR scripts (GATO/Hetake/Beetle/Avian) consumes EXACTLY to its `$FE` with these lengths.
**FD/BA4A[00..13] = `04 04 06 00 FF FF 04 04 04 02 03 05 04 03 02 02 04 0A 10 03`** (op00=4 move-only, 01=4
attack, 02=6 tech, 0F=2 msg, 0A=3, 10=4; op03=0 / op04-05=0xFF = variable-length control-flow skip C1/98C5).
This RETIRES the old hand-decoded switch (01→3, 02→4, 00→1) whose desync manufactured GATO's phantom second
attack out of the movement-suffix bytes (the `01 00 00` mis-read). GATO block0 now segments as
`[01 00 05 01][00 00 06 06][02 10 05 00 00 00]` = **[attack(pre-move mode=byte3=01), move-only(post tgt6/mode6),
song]** → cadence [attack, SONG] (the op-00 is skipped as move-only). **E.1 is CLOSED**: the "unknown ops"
04/08/0A/0E were operand/suffix bytes of longer entries under the wrong lengths; the sole genuine leading op
still un-derived is 0x0A (Hetake block2, handler C1/9978 = RTS) → traced + no-op (never an attack).

**Op semantics via C1/B85F** (corrects the old opcode meanings): op 00 → **C1/9810** (arms movement:
targeting via AC14 → mode byte → `$5E0D,X`=mode / `$5E15,X`=target, NO attack); op 01/02 → **C1/983A** (same +
last-target-avoid scan + sets `$AEE3`=attack/tech for the downstream dispatcher). So op 00 = MOVE-ONLY (skip as
filler), op 01/02 = the substantive attack/tech. The movement mode of an attack is its own byte[3]; the
post-move (return) is the following op-00 entry's byte[2]=tgt / byte[3]=mode.

**RETURN STOP RULE derived (E.5 → resolved mechanism, C1/3AA0-3AF6, NOT yet ported).** The return handler
C1/3A3D computes the AWAY bearing (atan2 operands swapped: target→$D3/$D4, self→$D5/$D6) then the stop test
**C1/3AA0**: `d1 = self.X($1D0F,X) − ref($80)` → $81(hi, signed); `d2 = self.Y($1D26,X) − ref($82)` → $83;
`oct = C1/0118(angle $DB)` → $84 (octant bits). It branches on (`$84 & 2`), (`$84 & 1`) + sign(d1)/sign(d2)
to → 13AF0 (keep walking) vs 13AF7 (stop/clamp): **keep moving AWAY until the position delta crosses zero in
the bearing's dominant axis** (the ref $80/$82 = the pre-computed limit, $82=$A5EC,X). This explains the
variable rest spots (85/111/24/31 = NOT a stored home). The rule is DERIVED but requires porting atan2
(C1/0222) + the octant fn (C1/0118) + the mode machine to be byte-exact.

**ARRIVAL = bounding-box OVERLAP (C1/28B0), not a radius.** Per-combatant box `minX $975A,X / maxX $9770,X /
minY $9765,X / maxY $977B,X`; C1/28B0 tests attacker-box vs target-box overlap (→ 1291D overlap / 12914 none).
Byte-exact arrival needs the per-sprite box EXTENTS (still to derive) — a proximity radius would be invention.

**MOVER (CF/F98F) + trig:** the executor is `phase += $98C2,X (0..8); offset = trigTable[angle*4]·phase>>8;
$1D26,X=baseY+trig(angle); $1D0F,X=baseX+trig(angle+0x40)`, segments re-based every 40f (mode machine C1/3714
→ C1/373B → CC/FBAB[mode] class → `JSR ($3760,X)`). The approach handler C1/379A reads the trig table at
**C0/F700** (via C1/01F9); the mover CF/F9FB reads **C0/F900** — BOTH need baking. atan2 = C1/0222.

**STATUS: movement subsystem NOT ported.** The mechanism is fully derived (every routine disassembled above),
but a byte-exact port = ~8 interlocking routines (atan2/sin/octant/mode-machine/approach/return/mover/arrival)
+ 2 baked trig tables + underdetermined inputs (sprite-extent boxes for arrival, walk-pose bake E.6). An
approximated net-rate mover would violate the no-invention mandate, so it is DEFERRED to a dedicated pass.
MASH-safety note: GATO's block1 (when-hit) attack `01 01 06 00` has byte[3]=00 = mode 0 = NO movement, so a
future port gated on the mode byte leaves every pinned MASH fight byte-identical (only the HOLD block0 walks).

---

## ADDENDUM (Wave 3 / STEP 2 implementer — the movement subsystem SHIPPED)

**IMPLEMENTED (src/battle.c + src/field.c + src/data/battle_trig.h): GATO WALKS.** The whole §A model is ported
byte-derived and validated. HOLD fight (`gato_t0`, GATO un-hit → block0): approach f415→f576 (mover runs at
`$A013==0` while the ATB ticks) → the physical DISPATCHES ADJACENT on the C1/28B0 box overlap (f576, `9877=31`,
never mid-walk) → −13 to Crono (unchanged formula) → RETURN walks away (mode 6) → SETTLES at the derived ref →
SONG in place (f987, op02, no pre-move). Cycle `[ATTACK, SONG]` repeats. Determinism ×2; MASH fights
byte-identical (block2 counter mode 0, no walk); full + quick suites green.

**Trig tables baked verbatim** (`tools/_gen_battletrig.py` → `src/data/battle_trig.h`): `ctBattleAtanTab` = **C0/F300**
(atan, 32×32, index `(|dy|>>3)*32 + (|dx|>>3)` — the atan2 reader indexes F300, NOT F700/F900 as an earlier note
said), `ctBattleSinTab` = **C0/F900** (signed sine, `(angle<<2)&0x3FF`, negate when idx≥0x200 → ±255),
`ctBattleFaceTab` = **C0/F700** (facing octants, for the walk-pose E.6). `--verify` spot-checks
sin(0/40/80/C0)=0/255/0/−255 + the atan sentinels.

**Ported routines** (all C, all cited): `battleAtan2` (C1/0222 verbatim, quadrant fold from dx/dy signs);
`battleTrigMul(angle,scale)` = the CF/F9FB / C1/00A7 8×16 hardware-multiply tail (`scale*signedSin>>8`, low
byte) shared by the mover (scale=phase) and the step/ref (scale=`$AE`); the mover CF/F98F (phase 0..8 = `0,0,
1,2,3,4,5,6,7` px, 32f/segment re-based, verified vs `pristine_walk` 111→118 = +7/segment); the mode machine
C1/3714/373B (32f cadence `$989F`, mover cadence 4f `$9742`, mode→class `CC/FBAB`); approach C1/379A
(angle=atan2(self,target)=0x40 toward Crono below); arrival = **C1/28B0 box overlap** with the DERIVED extents
`$971E/$9729` **oracle-read** (GATO 16, PC 8; box = `[live±ext] X, [live−ext .. live+8] Y`, writer C1/285A);
return C1/3A3D (away bearing atan2(target,self) swapped, ref = `target + step(awayAngle,0x40)` C1/3A7F-3A9B).

**§A.6 DISPATCH GATE (E.3, the derived observable):** the walk-in physical arms its approach at the enemy's ATB
fire (`battleOpenAction` defers, sets `mvPendAtk`, single RNG draw for the 05 random-PC victim) and dispatches
when the box overlaps (`battleArrive`), never mid-walk. **Residual (documented, not invented):** our approach
runs AFTER the ATB recharge (sequential) where the oracle overlaps the walk WITH the recharge (concurrent), so
our per-cycle timing is longer than the oracle's; and we dispatch immediately on arrival (the oracle's ~+40f
arrival→dispatch gap is un-modelled). The SHAPE (approach→adjacent→return→song) + the per-hit damage are exact.

**RETURN STOP (E.5 → shipped as the derived rule's geometric form).** The exact ROM settle (C1/3AA0 octant
reverse-oscillation) latches on `$9791` (writer 13BAA) and gates on the GLOBAL `$9872` (a collision-enable,
writers C1/29A6/2BB1/…) + a secondary tile-grid test C1/2B13 — a Python port of the raw octant branch walked
GATO the WRONG way (those inputs are unmodelled). So the return is shipped as the DERIVED RULE in its clean
geometric equivalent: walk AWAY until `(self − ref)·awayVec ≥ 0`, ref = `target + step(awayAngle,0x40)` (the
dossier's own "cross zero in the bearing's dominant axis"). Validated: Crono at Y=156 → GATO settles at Y=90 ≈
ref `156−64=92`. Rest spots therefore land at the derived ref (not the ROM's exact px — §A.1/E.5 latitude).

**E.6 WALK-POSE BAKE — DEFERRED (idle-pose fallback, sanctioned).** The mode machine reloads per-slot anim
tracks so the walking GATO has distinct leg poses (`gm_30/90`). Those poses are NOT yet baked; the render draws
his IDLE bob pose at the correct LIVE position (`battleGatoRender`, field.c). The MOVEMENT (the user's report)
is correct; the leg animation is the follow-up bake. No pose is synthesized (mandate).

**NEAREST TARGETING (byte 06) — kept at the lead-PC default (NOT switched).** Byte 06 = C1/A4AF nearest over the
`$975A/$9765` box deltas. A Manhattan/Euclidean stand-in would be an INVENTED metric (and in a staged formation
it is a 1-2px close call where the metric decides — it flipped GATO's mash counter Crono↔Marle, breaking the
byte-identical mash pin). C1/A4AF's exact comparison is un-derived, so 06 stays lead-PC; the movement never
depends on it (the walk uses the script's own 05 random-PC pre-move target + the actual struck victim for the
return reference). **E: port C1/A4AF for byte-exact nearest.**

**Files:** `tools/_gen_battletrig.py` (gitignored generator), `src/data/battle_trig.h` (baked), `src/battle.c`
(`battleAtan2`/`battleTrigMul`/`battleBox`/`battleBoxOverlap`/`battleMoveArm`/`battleArrive`/`battleMoveApproach`/
`battleMoveReturn`/`battleMoveTick`/`battleMoveInit` + the arming wiring in `battleAiRunTurn`/`battleOpenAction`/
`battleAdvanceAction` + host state in `battle.h`), `src/field.c` (`battleEnemyLiveDelta` + GATO/Hetake/cursor/
popup rebase). Live position held in the HOST (`mvLiveX/mvLiveY`), NOT `$1D0C` — that RAM doubles as the field
composite's camera scroll (`$1D0A-$1D0F`), which the forest composite reads (writing there garbled `fcc`).
Checks: `field-battle-gato-walk` (new, the HOLD walk); re-pinned `field-battle-ai-cursor-cadence` (walk cadence)
+ `field-battle-confirm-boundary` (turn-1 walk).

---

# E.3 RESOLVED (2026-07-22, battle_facing_engine.md SESSION-6c/7) — the dispatch does NOT wait for arrival

gato_press_action HOLD, full ATB monitor: GATO's first physical dispatches at **ATB-0 (f454) + 24 (f478)
= the think gap FROM THE ATB**, firing REMOTELY from (136,118) — 26px from Crono, NO box overlap, live
position constant through the span. The §A.6 "never dispatches mid-walk / fires once the approach has
stopped" observation was a COINCIDENCE of that capture's schedule (arrival f922 simply preceded the ATB
f964); the arrival-flag hypothesis is dead. Corollaries: (1) the +24 think applies to EVERY enemy turn
(ATB-0 -> dispatch), not just walk-ins; (2) the pre-move approach (armed at the previous span end / the
combat-start initial eval) runs INDEPENDENTLY of the ATB — the strike plays wherever the enemy stands;
(3) our engine's mvPendAtk arrival-defer pipeline is the E.3 guess and must be replaced by the ATB-timed
dispatch in the settle/pacing milestone (battle_facing_engine.md sessions 5-7 hold the full spec).

---

# F — THE TARGET RESOLVER, DISASSEMBLED (2026-09-08)

§C.1 recorded value 06 as *"'nearest' = C1/A4AF ... a real spatial pick over the `$975A/$9765` deltas"*
and `src/battle.c` carried the stronger admission that *"the ROM's own metric (C1/A4AF) is
un-disassembled, but its PICK equalled min-Euclidean-dist^2 in all 11 captured GATO counter hits, so
that is the sanctioned model"*. It is disassembled now. The fitted model was right except in one place.

## F.1 The chain

```
C1/AC1E  LDA $CC0000,X / BPL          the targeting byte; bit7 set -> use $AED8 directly
C1/AC2C  ASL A / TAX / JSR ($B8BB,X)  the resolver table
C1/A4AF  (value 06)  $AECB = 0 ; $986F = $B252 + 3 (SELF) ; $986E = 0 ; LDA #05 / JSR C1/0003
C1/0003 -> C1/0045   PHA/PHX/PHY / ASL A / TAX / JSR ($0051,X)        ; A=5 -> C1/2986
C1/2986  snapshot $1D0C/$1D23 of $986F and $9870 into $A039/$A050 ; STZ $9872 ;
         JSR ($2D81 + $986E*2)                                        ; sub-mode 0 -> C1/29B2
C1/29B2  $84 = 0 ; $86 = 0xFFFF (DEX from 0) ; loop:
           $9870 = $84
           skip if $96F5,X == 0                   present gate
           skip if $9FF7,X bit7                   hidden gate
           skip if $5E4A[$CCF8ED[X*2]] bit7       dead / ineligible
           JSR C1/2B37 -> dist^2 in $AF           (seeds $9893/$9894 = 0x0400; the movers' own metric)
           LDA $86 / CMP $AF / BCC keep-old ; else best = dist, $9873 = $84
         INC $84 / CMP #03 / BNE loop             <-- THE THREE PC SEATS, and only those
C1/A4C5  if $9873 bit7 -> unresolved ; if $AEFF[$9873] bit7 -> unresolved ; else $AECC = winner,
         $AECB = 1, $AD8D = 1
```

`C1/B8BB` is **not 16 entries** — it runs to at least 20 (`A3F6 A3F7 A411 A42E A43D A452 A4AF A4E0 A508
A541 A54B A555 A55F A569 A573 A5A3 A5D3 A603 A633 A663`), which matters because the targeting byte
reaches `0x10`.

## F.2 ⭐ The one correction: ties go to the LATER seat

```
129E0  A5 86     LDA $86      best so far (16-bit)
129E2  C5 AF     CMP $AF      candidate distance
129E4  90 09     BCC ->129EF  taken when best < dist  -> KEEP OLD
129E6            else (best >= dist)                  -> TAKE THE NEW CANDIDATE
```

so a TIE takes the new one. The port used a strict `<`, giving ties to the earlier seat. The party
lines up in a column, so a strike landing between two PCs is exactly the tie case. Corrected.

## F.3 What the targeting byte actually takes

Measured, `--monitor "7EAEE5:1" --diff` over 2200 frames on nine captures:

```
bat1 01 05 06     bat2 01 04 06 0A 0F 10     bat3 00 04 05 06 0E     bat4 01 06
bat5 01 06        bat6 01 04 05 06           bat7 05 06 0A           bat8 01 04 06 0E
bat9 01 06 0A 0E  gato 01 05 06
```

**06 appears in all nine.** 05 and 06 are the two the port resolves; every other value
(00/01/04/0A/0E/0F/10 at least) falls back to the lead PC. That fallback is a PORT DECISION and not the
ROM's — `C1/A4AF` leaves `$AECB = 0` and lets the caller handle "unresolved" — and it is the open work
this section leaves behind.

## F.4 ⚠ Two gates deliberately NOT ported as the ROM's bytes

`$96F5,X` and `$9FF7,X` have **zero references in the engine** — nothing writes them — so branching on
them would repeat the `$A2B5` mistake that `battle_enemy_move_modes.md` R6 caught (a live branch on a
WRAM byte the port never fills). `battlePresent`/`battleAlive` stand in and are named as stand-ins.
`$AEFF` IS maintained here, so `C1/A4CC`'s eligibility re-check is ported literally.

## F.5 ⚠ The correction is UNGRADED, and the stale comment that caused this

Reverting the tie-break to `<` passes the **entire suite** — no fixture puts two living PCs at equal
distance from an attacking enemy. So this is a correctness fix with no test behind it, recorded rather
than presented as validated. Closing it needs a fixture with a symmetric party position.

And the reason this took a detour: the comment above `battleAiTarget` still read *"Our engine has no
enemy-movement live-position model YET ... so 'nearest' is proxied to the lead living PC"* long after
the body had stopped doing that. It was read as current and reported as a live invention. Corrected in
place; the lesson is the same one `check-vs-own-reference` records — a claim in a comment is not
evidence about the code beneath it.

---

## CORRECTION 2026-09-09 — the approach's ARRIVAL is a DISTANCE test; `C1/28B0` is the step's collision test

§A above says "ARRIVAL = bounding-box OVERLAP (C1/28B0), not a radius". That is wrong, and the port built on it
— plus a "ready-hold" (`$AFAB == 0 → latch, no walk`, battle_ai_interp.md §7) that is not in the handler
either. Both were caught by the mover-from-seed sweep on `forest_wait_ambush_active` f2158+: the Beetle of
slot 1 at (120,150), Crono at (110,176), no box overlap in sight — and the oracle's approach pass at f2173
`INC $9829,X` (C1:3836) and `STZ $98A7,X` (C1:384F): arrived, no segment.

`C1/379A..38BE`, the whole handler, per pass:

```
C1/379C  STZ $A5FD,X
C1/379F  LDA $9791,X / BNE -> the STORED bearing $9786,X   else: $98AF/$98B7 := self, bearing := atan2(self -> $9752,X)
C1/37CC  $AE := 8; $9786,X := bearing; $8C/$8E := trig*8 (C1/01F9); $96DE,X := C0/F700[bearing]
C1/37F2  $A039/$A050 := the live feet of the target and of self
C1/3813  LDA $A2B5 / DEC / BNE ... : mode 1 -> JSR C1/2AEF ($9893 := 0x0400)     32 px
                                    mode 2 -> JSR C1/2AFB ($9893 := 0x1000)     64 px
                                    mode 3 -> JSR C1/2B07 ($9893 := 0x0010)      4 px
                                    else   -> BNE ->13831                       arrived outright
C1/2B49  |dx| -> $AD=$AE, JSR C1/0089 (the hardware multiply) -> $82; |dy| likewise -> $AF; $AF += $82;
         CMP $9893 / BEQ / BCC -> $9872 := 0 (arrived) else $9872 := FF          -- |dx|^2 + |dy|^2 <= threshold
C1/382C  LDA $9872 / BMI ->13851
C1/3831  ARRIVED: INC $9829,X; $5E1D := $5E0D; $9831/$9839 := the target's foot; STZ $98A7,X; done
C1/3851  NOT ARRIVED: $A053/$A03C,X := self + step; JSR C1/285A (its box: X +-$971E, Y -$9729..+8);
         $9875 := (mode == 3); JSR C1/2926 (the grid) / BMI -> blocked; mode 3 -> clear;
         JSR C1/28B0 / BPL -> clear                                              -- THIS is where 28B0 lives
C1/388D  BLOCKED: $DB := ((bearing + 0x40) & 0xC0) -- the nearest axis; $9786,X := it; INC $9791,X; STZ $98A7,X
C1/38AF  CLEAR: STZ $9791,X; $98A7,X := 1
```

`C1/28B0` walks Y over all eleven combatants — Y ≠ self, `$96F5,Y` present, `$9FF7,Y` bit7 (hidden) skipped,
`$A5CD,Y` bit7 skipped (the anim script's flag, C1/5821/5835; the airborne Avian carries it) — and returns
0x80 (|1 for an enemy Y) on any overlap of the caller's PROJECTED box with Y's box, else 0. Its nine callers
(`C1/3888 3930 39E1 3B8B 3D09 3E18 3F15 400C` and `C1/2C7B`) are all step-collision sites. The port's
class-4/5/6 "body deflector" tests the victim only; the ROM tests everybody (left as recorded; the sweep's
72 frames never reach a body).

Why the old law survived: GATO. Home (136,111), Crono (134,144): d² = 1093 > 0x400 → one 8-px segment →
(136,118), d² = 680 ≤ 0x400 → arrived. The "ready-hold" at "26 px remote" (f461) IS that arrival, and the box
overlap the port tested for never fired at all (GATO's box bottom 126 vs Crono's top 136). The port's GATO
walk is unchanged in space by the correction; every other approach now stops where the ROM stops.

## The mover's cadence, per slot, and its place in the frame (same day)

`CF/F978` (entered by `JMP` from `CF/EFBA`, the per-frame service path), X = 7..0:

```
CF/F97D  LDA $98A7,X / BEQ next                  a segment in flight?
CF/F984  DEC $9819,X / BNE next                  the per-slot counter, EVERY frame
CF/F989  LDA $9742,X / STA $9819,X               reload = the slot's PERIOD
CF/F98F  CLC / LDA $9737,X / ADC $98C2,X / STA   phase += the slot's STEP
CF/F99B  CMP #08 / BNE / STZ $98A7,X / STZ $9737,X     the segment's end
CF/F9A5  Y := base + trig(bearing) ; X := base + trig(bearing + 40)   (CF/F9FB)
```

`$9742,Y` and `$98C2,Y` are staged by the loader from E4/F609[id]: `& 7` → the step, `>> 4` → the period, and
the period is also seeded into `$9819,Y` (CC/EBCC-EBE0). Hetake/GATO/Avian 0x41 = 4/+1, Beetle 0x61 = 6/+1,
Diablos 0x22 = 2/+2, Naga-ette 0x21 = 2/+1. The port had 4 and +1 hard-wired and re-seeded the counter at
every segment start; `$9819`'s only writers are the loader and the wrap, so a segment that ended early leaves
its count for the next one. Measured on wait_ambush: `$9819[5]` DEC'd by CF:F987 at every frame a segment runs,
reloaded to 04 by CF:F98F on each phase (f2154, 2158, 2162 …), untouched between segments, and — the part the
port had backwards — **not decremented on the frame the handler starts the segment** (f2178, f2218: `$98A7 := 1`
by C1:3F52, `$9819` still 04, first DEC the next frame, phase 1 four frames later). The mover runs before the
handler pass within a frame. `battleMoveTick` now runs every slot's mover first, then the handler passes;
that moved every pinned first step by one frame (15 GATO/forest sequences re-pinned).

## THE INSTRUMENT: the mover from a mid-battle seed (`CT_BATTLE_MVSWEEP`)

`CtBattleMoverSeed` fills the host's per-slot mover state from a 128 KB WRAM image — `$974A $5E0D $9752 $9786
$98AF $98B7 $9737 $98A7 $9791 $9819 $9742 $98C2 $9897 $A5E4 $A5EC $9829 $5E1D $9831 $9839`, the live feet
`$1D0C/$1D23`, the record extents — and `CT_BATTLE_MVSWEEP=<n>` in the RENDERWRAM path steps the idle walk +
the mode machine + the mover n frames, `CT_BATTLE_MVTRACE` printing every phase step (`battlemove`), every
segment start (`mvseg`) and every approach pass (`mvapproach`). Two phases are measured per capture:
`CT_BATTLE_SCHEDPHASE` (the `$A4` INC frames) and `CT_BATTLE_MVCADPHASE` (the `$9897` DEC frames). The seed
is an oracle frame, so the trace is comparable line by line with `--monitor "7E1D0C:11,7E1D23:11,7E9737:8,
7E98A7:8"` from that frame on — until the oracle's AI arms a new mode, which the host does not run.

`field-battle-class6-orbit-seed` (`saves/battle_re/wa2158.raw.wram` = wait_ambush one frame before f2158)
matches the oracle frame for frame and pixel for pixel through 72 frames: the 0F→0A commit at sf1 with the
segment in flight (phases 5/6/7/8 at sf1/5/9/13, Y A2→9F), the class-6 orbit's start at sf21 (angle 00) and
steps at sf25..53 (X 8A→91), the next segment at sf61/65/69, and the Beetle's distance-law arrivals at sf16
and sf56 (d² = 776 ≤ 0x400, no segment). This closes three of the five recorded grading blind spots — the
class-6 halving is now graded (mutation killed), the mid-segment commit and the mover order are graded — and
it is the vehicle for the remaining two (the class-5 orbit flip, the class-6 rewind) once a seed inside those
windows is peeked.

---

## 2026-09-09 (later): the deflector against EVERY body, the phase that is never reset, the settle that leaves nothing behind -- and the boot wipe

Three more seeds through the same instrument (`CT_BATTLE_MVSWEEP`), each chosen where the oracle does something the
port had never been graded on. Every claim below is an oracle row (`--from forest_wait_ambush_active.state`,
`--diff --writer`) or a `gato_press_action.state --input "100:8,0:1900"` row, and the port now prints the same
event on the same frame.

### The body deflector is C1/28B0 at all four class-4/5/6 sites, and it tests everybody

`C1/3B8B` (class 4, skipped iff `$A2B5 == 03`), `C1/3D09` (class 5 far arm, skipped iff `0F`), `C1/3E18` (class 5
orbit arm, skipped iff `0F`), `C1/3F15` (class 6, skipped iff `0C`): the same `JSR C1/28B0` after the grid test,
on the projected step's box built by `C1/285A` from `$A03C/$A053,X := self + step`. The port tested the VICTIM's
box only. Witnessed the other way: **f4627 and f4667** (wa4620 sf8/sf48), the Beetle of slot 2 at (97,121),
mode 01, victim Crono at (110,176) -- its projected box (X 90..106, Y 112..136) meets the OTHER Beetle's box
(cmb 4 at (103,150): Y top 150 − 16 = 134), and the oracle deflects (`C1:38A7` bearing := 40, `C1:38AA` INC
`$9791`, `C1:38AD` STZ `$98A7`), twice, where a victim-only test says clear. `battleBodyBlocked` at all five
sites now.

Two byte-level corrections rode along. The box's Y top is the record's OWN Y extent (`$9729,X` = E4/F600 +8:
Beetle 8 × 16, Avian 2 × 16; 249 of 256 records differ from their X extent) -- the port's class-4/5/6 grid boxes
used the X extent on Y. And `maxY = y + 8` WRAPS (`CLC / ADC #08 / STA`, C1/28A6-28AC), it is not clamped.

### The $A039/$A050 mirror

`C1/28B0` compares box ROWS (`$975A/$9770/$9765/$977B`), whose only writer is `C1/285A`, fed from the mirror
`$A039/$A050`. `C1/283D` -- JSR'd at `C1/10B8` and `C1/10DF`, before each handler loop of the idle tick, and at
`C1/40C8` in the action state -- copies every PRESENT (`$96F5`) combatant's live foot into the mirror and rebuilds
its rows; within the loop, a slot's step branch overwrites its own entry with the projected position before its
tests, and a LATER slot's `C1/28B0` in the same loop sees that projected box. Ported as `mvMirX/Y`: refreshed at
the top of `battleMoveTick`'s handler pass, written by every step branch, read by every body test. Unobservable
in the captures (the `$9897` counters are staggered per slot and decrement in lockstep, so two slots never pass
on the same frame unless one sat in mode 0 -- `C1/3722` skips the DEC for mode 0 -- long enough to desync),
modelled because it is the mechanism.

### $9737 is never written by a handler

The phase's writers are `CF/F996` (the wrap: `+= $98C2,X`) and `CF/F9A2` (the end: STZ). No handler resets it
at a segment start; the port did, at all five starts. What the oracle does instead, on a Beetle (period 6, so a
48-frame segment outlives the 40-frame pass interval):

    f2548  slot 1 ph6 (120,144)          f2549  slot 2 ph6 (103,105)
    f2552  slot 1's fresh pass: $98B7 := 90 (C1:3A6D) -- re-based to the ph6 foot; $98A7 := 1; $9737 stays 06
    f2553  slot 2's fresh pass: $98B7 := 69 (C1:37BB) -- likewise
    f2554  slot 1 ph7: (79,89) = (121,137)  -- SEVEN pixels up from (120,144) in one step, from the new base
    f2555  slot 2 ph7: Y 6F = 111 from base 105
    f2560/f2561  ph8, the end (CF:F9A5 STZ), only there

And the other shape, a segment KILLED by a blocked pass and resumed later: f4627 slot 2 blocked at ph6 (`$98A7 :=
0`, `$9819` left at 02); f4707 its latched pass is clear (`$98A7 := 1`, no re-base -- the latched branch never
writes `$98AF/$98B7`); f4709 ph7 (X 5A = 90, two frames later on the leftover 02), f4715 ph8 and the end. The
five `mvPhase = 0` are gone; `wa2510` and `wa4620` pin both shapes.

Where the base is written, per class: class 1 fresh head `C1/37B0/37B8`; class 4 fresh head `C1/3A62/3A6A`; class
5 fresh head `C1/3BE7/3BEF` and its orbit arm `C1/3D58/3D60`; class 6 every pass `C1/3E38/3E3E`. The port's
classes 4 and 5 wrote it at the CLEAR branch instead of the fresh head -- same value whenever the pass is clear,
a different one after a blocked fresh pass (the ROM re-bases there too, f700/f979 on GATO: `C1:3A6D` on the
blocked passes). Moved.

`$9791` is a COUNTER (`INC`, C1/38AA C1/3BAD C1/3D28): 1, then 2 at f4667; every reader is a `BNE`. Ported as the
increment so the seed comparison reads the same byte.

### The blocked branches of the orbit and of class 6 write no $98A7

`C1/3E1D-3E27`: `$A5F4,X ^= 1 / BRA 3E30 / JMP 3D3A` -- the flip, then the exit. `C1/3F1A-3F49`: the rewind and the
direction flip, `BRA 3F52` -- the exit. Neither touches `$98A7,X`; a segment in flight keeps running. The port
stopped the mover in both (unobserved in the seeds -- `$98A7` was already 0 at f2517/f4631 -- corrected from the
bytes).

### The settle leaves nothing behind -- the retired latch, graded on GATO

`C1/3B40-3B5E`: `INC $9829,X ; $5E1D := $5E0D ; $9831/$9839 := the target's foot ; STZ $98A7,X ; -> 3BBC`. No
`$9791`, no facing, no flag. The port's `battleMoveReturn` parked behind a host latch (`mvStopped`), wrote
`mvWalking = 1` and forced `mvFacing = 1` at the settle. `gato_press_action --input "100:8,0:1900"`, GATO in
slot 3 (`$96E1 $974D $98AA $982C $9789 $9794 $98B2 $98BA`):

    f638   mode 6 committed
    f660   fresh pass: base := (88,76), bearing 40 (crossed), facing 01, $98A7 := 1, $9791 := 0
    f700   fresh pass, BLOCKED (Crono's box): bearing := 80 (C1:3BAA), $9791 := 1, $98A7 := 0, base := (88,7D)
    f739   latched: bearing 80, facing 02, $98A7 := 1, $9791 := 0 (no base write)
    f779.. fresh passes up (bearing C0, facing 00), bases 7D 75 6D 65 5D
    f979   blocked by the grid: bearing := 00, $9791 := 1, base := (80,55)
    f1018  latched: bearing 00, facing 03, $98A7 := 1
    f1057  fresh: base := (87,55), bearing 40, facing 01 -- SETTLED: $9829 := 1 (C1:3B45), $98A7 := 0 (C1:3B5E)
    f1096  settled again ($9829 := 1), f1136 again ($9829 := 2 -- a counter), f1148 $9829 := 0 (C1:8D34)
    f1151  the song; f1462 mode 1 requested, f1489 committed
    f1509  THE NEXT APPROACH'S FIRST PASS IS FRESH: $9791 = 0, base (87,55), bearing := 15 (C1:37D7), $98A7 := 1
    f1549  bearing 12, base (8D,58) = (141,88)
    f1589  blocked by MARLE's box at (94,5B) = (148,91): bearing := 40, $9791 := 1

The port's old pinned GATO walk had `stored=1` at that first approach pass, a bearing of 40 and a stale base
(128,85): the latch's `mvWalking = 1`. The new one prints bearing 15 from (135,85), then 12 from (141,88), then
`body=1 -> deflect bearing=40` from (148,91) -- the oracle's three rows, 200 frames earlier on the port's
timeline (gato_t0 boots earlier than press_action). The facing at the settle is `C1/3B14`'s, from the away
bearing: 01 here because the bearing is 40, not because anything says "face the party".

### The boot wipe, CF/FB92

    CF/FB92  REP #20 / LDX #7FEB / STZ $2D00,X / DEX / DEX / BPL      -- $2D00..$ACEB zeroed
    CF/FB9E  LDX #006A / STZ $80,X / DEX / DEX / BPL                   -- the DP scratch

before any loader stages anything (forest_holdleft f229, gato_t0 f141, wait_ambush f331: every row `CF:FB9A`).
The port boots its live fights from PRE-BATTLE seeds (gato_t0, forest_pre, batalha_nao_comeca), and those carry
the field's leftovers -- 0x80 -- in `$9FF7` and `$A5CD` (the two C1/28B0 skip flags: the ported deflector skipped
EVERY body, GATO walked into Crono), in `$A5F4` (the class-5 orbit direction: 0x80 rotated −0x10 and flipped to
0x81), in `$A5FD` slots 0-2 (class 6 skipped its centre capture and orbited (0,0)), in `$A5FC` and `$A5E4/$A5EC`.
`battleMoveInit` now zeroes the handler bytes the port reads from WRAM (`$9FF7 $A5CD $A5F4 $A5FC $A5FD $A605
$A60D $A615 $A61D $A625 $9875`), as the wipe does. The full-range memset is NOT modelled: the port stages its
own state inside `$2D00..$ACEB` at other moments (an audit ticket). The mid-battle seeds (`bat2`, `wa*`) are
post-wipe images and need nothing.

### Graded

`field-battle-class5-flip-seed` (wa4620, 100 f): the two body deflections by the other Beetle, the grid flip
(`$A5F4 := 0`), the reversed orbit's steps, the resumed segment at ph6 -- mutations: victim-only bodies, X extent
on Y, no flip, phase reset. `field-battle-class6-rewind-seed` (wa2510, 100 f): the rewind (`$A615 := E0`, `$A61D
:= 1`), both Beetles' in-flight segments across their re-pass (ph7 from the new base) -- mutations: no rewind,
phase reset, no approach re-base, no class-4 re-base. `field-battle-class6-orbit-seed` re-pinned with the
`mvblock` lines. Eleven live-fight checks re-pinned: the GATO fight (the post-settle approach aims at the target
at once), the forest fights (the Beetles' carried phase, the Hetakes' orbits from a zeroed direction).

### Left

The full-range boot wipe as one memset (audit the port's staging order first). The Avian's `$A5CD` bit7 --
set by its flying anim script through `C1/5821` (`AND #80 / ORA $8E`, `$8E` carrying the bit) -- is not ported
(the port never sets it; forest_holdleft's Avian never has it through f1400; bat2's does). `$9829` is a counter
too (`INC`, f1136 = 2); the port keeps a flag, every reader being a `BNE`/`BEQ`.
Found while re-pinning, not fixed here: on `gato_cyclone.raw.wram` a Cyclone cast at f1286 leaves GATO at (135,37),
inside the arena's blocked rows -- every approach pass after it is `mvblock ... grid=1` in all four quantized
directions, mode 1 never arrives, the turn gate (C1/84B8) never opens, and GATO never acts again; the same in the
pre-patch engine. How the Cyclone's victim displacement puts him there is the open question (the ROM's knockback is
the anim script's own displacement). `field-battle-cyclone-fx-not-replayed` was re-timed to a Cyclone at f610 and
the song at f2522 so its mutation grades again. The suite's mutation-anchor audit (`find` counted against the
sources) also lists 15 stale anchors in other domains (menu, follower, field, vmdiff) -- pre-existing, the standing
stale-mutations repair.
### The (135,37) trap, resolved the same day: a STALE KNOCK DISTANCE

Not the Cyclone's displacement at all. `gato_cyclone.state` (= `gato_press_action.state` with `$2837 |= 0x80`, the
tech-slice injection; the patcher is three lines: locate the WRAM image in the state by the `$29BF` records, OR
the bit) driven by the choreography dossier's btnscript: `$A013` 1 at f438, 0 at f751, and GATO's `$1D12/$1D29`
constant at (88,71) through the whole span -- no row. The role-3 track of the Cyclone (CE/094D) carries no A8/A9.
The port's 48 px came from `animKnockDist`: `battleAnimParse` took the `A8 30` ops of GATO's SONG (CD/9EDA, on
every one of its eight tracks) as the knock distance, and the PC-tech span path -- which does not parse a CD
script -- reset every other parse product but not that one, so the Cyclone's strike walked GATO 0x30 along the
bearing from Crono, up, into the blocked rows.

Two corrections, both from the bytes. The PC-tech path zeroes `animKnockDist` with the rest. And the parse reads
the knock only off the VICTIM ROLE's track: C1/69C3 (A8) and C1/6AF0 (A9) arm the mover of `$A014[$A010]`, the
combatant of the role RUNNING the track (`LDA $A010 / TAX / LDA $A014,X / TAY ... STA $A639,Y / STA $A34A,Y` =
the pre-aimed bearing `$A5AA,Y`, the base `$A35D/$A370,Y`, `$A337,Y := 1`); roles >= 4 branch to C1/6A2A /
C1/6A99 (effect objects). The loader (C1/4562: `ASL $80` from bit 15, Y = the role, X = the pointer) makes the
k-th pointer belong to role 15 - bit. Under that rule GATO's physical (mask 9000: roles 0, 3) keeps its `A9 04`
on role 3 = the victim's +3 (press_action f537 Crono, f1801 Marle); the song's `A8 30` on its other roles moves
no PC (press_action f1151-1462: `$1D0C/$1D0D` flat), and the Hetake's attack (0x14, mask 9001: roles 0, 3, 15)
carries `A8 04/06/08` on role 15 only -- the old parse had been knocking Crono 7 px on every Hetake hit; the
oracle (forest_holdleft f451-628, f650-852) never moves him. Six live-fight checks re-pinned on that, the cyclone
check back on its original script (song f2846, shot f2906) with its mutation graded again.

What the arm actually does per frame (CF/F13F/F149 and CF/F1E7/F1F0 are the writers, `$A639` the operand,
`$A86D` 2) is still the port's "dd-1 px, once" law from one sample (dd=4 -> +3); dd=0x30 on an effect role is not
a second sample. The mover itself is a named ticket.
### The move-forward mover (ops A8/A9) -- the victim's recoil is its own track's op, not a strike constant

Derived the same day, after the stale-knock finding. Three handler paths by the role running the track (`$A010`):
roles 0-3 (a combatant, `$A014[role]`), role 4 (every victim in `$A018`, the broadcast), roles >= 8 (an effect object,
`$A2B6`). All three ARM the same per-object mover: count `$A639 := dd`, bearing `$A34A := $A5AA` (what the `76` aim
wrote -- C1/60E6 resolves two points and takes C1/0222's atan2, UNQUANTIZED: Marle's recoil bearing is 0D), facing
`$96DB := C0/F700[bearing]`, base `$A35D/$A370 := the foot`, `$A2EB := 1`, mode `$A86D := 2`, acc `$A311 := 0`,
finished `$A19D := 0`, active `$A337 := 1` (effects: `$A644 $A355 $A230 $A2F6 $A878 $A31C $A1E0 $A368/$A37B $A342`).

The SERVICE, mode 2 (CF/F194 for a combatant; CF/F61F for an effect object, without the edge test), on the
object's rate divider (`$A2EB/$A2FE`, CF/EFC9 -- the speed ops 08..0F set BOTH `$A324 := CC/F4BC[op-8]` = 01 01 01
02 04 08 10 20 and `$A2FE := CC/F5BC[op-8]` = 04 02 01 01 01 01 01 01, C1/4F54):

    finished -> nothing
    the foot's tile row 0 / 0E, column 0 / 0F -> finished             (CF/F19B-F1B1, the arena's edge)
    DEC $A639 / BEQ finished                                            no step on the zeroing tick
    $A311 += $A324 ; foot = base + trig(bearing, $A311)                 (CF/F1B8-F1F0)

The VISIT differs between the two ops. A8's (C1/6A1B) waits for `$A19D` and then clears `$A337` and advances: the
count runs down in the service alone, so `A8 04` at speed 4 steps 4, 8, 12 over three passes (the forest's role-15
object, f475-479, and GATO's song notes at rate 2). A9's (C1/6B46, entered straight from the arm at C1/6B43):

    LDA $A19D,Y / BNE stop
    $AE := 10 ; the point 16 px ahead on the bearing ; $7BFD[(y & F0) | (x >> 4)] AND #C0 / BNE stop
    JSR C1/7C2B  (= C1/285A on the current foot, then C1/28B0 against every body) / BMI stop
    DEC $A639,X / BNE stay
    stop: TDC / STA $A337,Y ; advance          stay: park          -- neither INCs $A00F: the frame is spent

So `A9 04` on a victim is exactly ONE step of the speed, the frame after the op: arm (count 4, the arm's visit ->
3), next frame the service (-> 2, acc 4, +3 px at bearing 40) then the visit (the wall 16 px below the PC line ->
stop; had it been clear: -> 1, stay; the frame after: the service -> 0, finished, no step; stop). Oracle, press_action:

    f535  $A324 := 04                          C1:4F79   the `0C`
    f536  $A337 := 1, $A639 := 3               C1:6B46, C1:6B91   the arm and its visit
    f537  $A639 := 2, $A311 := 4, $1D23 90->93  CF:F1B6/F1C2/F1E7   the service (FIRST in the frame)
          $A337 := 0                           C1:6B97   the visit: stopped on the grid
    f538  nothing
    f1800/1801  Marle, bearing 0D: (B0,6F) -> (B3,70), the same shape

The port had modelled all of this as "at the strike constant (rel 27), cardinal-quantized, dd-1 px, once" -- three
numbers fitted to that one sample -- and parked the victim track on the op for four ticks. Now the interpreter's
A8/A9 (`battleAscFwdService`, `battleAscA9Revisit`, `battleAscFwdSetPos`) is the one writer of the victim's foot,
the strike-time block is gone, and the song's `A8 30` on its note objects (rate 2: `$A644` 30, 2F at f1197, 2E at
f1199 ...) and the Hetake's role-15 `A8 04/06/08` run through the same service unchanged.

Graded by `field-battle-victim-recoil-a9` (gato_t0: `asc ... t3 FWD` + `ascfwd` lines; 4 mutations: A9 as A8, no
arm visit, no grid lookahead, an 8-px lookahead). Two live-fight checks re-pinned: the victim track now releases
at the ROM's frame (three frames earlier than the four-tick park), and a second hit on a Crono already against
the wall moves him nothing (the arm's lookahead), which changes GATO's later target foot. The port's dispatch
marker sits 4 frames before the ROM's (rel 54 here for the oracle's rel 58) -- the standing pre-roll residue,
untouched.
### And the sprite: no knockback offset either (same day)

With the foot walk ported, the 6-px "knockback" the PC stance layer painted on top of it (CT_FX_KNOCK_MAG) was
measured against the OAM: `battle_animeng_corpus.md`, the 2026-09-09 correction -- the struck PC's cells are foot
+ record exactly (6E at f545, C6 at f560, 4F at f660), and the layer drew the STAGED foot, not the live one. Both
fixed; `field-battle-pc-hit-cells` pins the cells. Seen on the way: after the span the ROM's seat-0 walk resumes at
cursor 2 (C7 at f641) where the port resumes at cursor 3 (4F) -- the PC walk's cadence in this live fight is one
entry ahead (it was already at idx 2 at rel -4 where the ROM was at idx 1 at rel -9). Not this step's; recorded.
### The Avian's `$A5CD` bit7 is the LOADER's, not an anim op (same day)

Named in "Left" as "set by its flying anim script through C1/5821". Wrong: ops 27/28 (C1/5806, C1/5840) write `$A5CD,X`
with `AND #80 / ORA $8E` -- they set or clear bit0 and PRESERVE bit7. The oracle names the writer: wait_ambush f342,
`$A5CD[8] := 80` by CC:EB86, i.e. the STA at CC/EB83 in the enemy loader, right after the wipe:

    CC/EB75  LDA $E4F604,X / STA $9700,Y          the record's byte +4 -> the staged flags byte, per slot
    CC/EB7C  AND #80 / BEQ +6
    CC/EB80  STA $A5C5,Y / STA $A5D0,Y             bit7 -> $A5C5+slot (never read anywhere) and $A5D0+slot = $A5CD+cmb

Avian Chaos (id $19) has byte +4 = C0; the Beetles ($8D) 00; every id in the port's live fixtures (forest_pre CD 97
04 31, gato_t0's 00 B3 C3 D3, ambush_pre's 5E) has bit7 clear. `battleMoveInit` now stages `$9700+slot` and the two
bit7 copies from the record. Graded negatively on two forest checks (a mutation marking every enemy airborne makes
the Hetakes' orbits stop colliding with each other -- killed on idle-walk-boot and hetake-attack-sfx); positively
only through the seed sweeps, which read the staged byte from the oracle's WRAM. No live fixture with a bit7 record
exists yet -- the wait_ambush formation ($8D $8D $19) has no pre-battle seed in the tree.
### The boot wipe as one memset (same day)

`CF/FB92-FB9C`: `REP #20 / LDX #7FEB / STZ $2D00,X / DEX / DEX / BPL` -- X starts ODD, so the 16-bit pairs land at
$2D01..$ACEC ($2D00 itself is never written, $ACEC is) -- then `LDX #006A / STZ $80,X / DEX / DEX / BPL` for the DP
scratch $80..$EB (host state in the port). It runs before any loader stages anything, so `battleBootWipe` runs
before `stageAll` in `CtBattleBegin`, and the byte-by-byte zeroing that had accumulated in `battleMoveInit`
($99CF/$99D0, $9FF7, $A5CD, $A5F4, $A5FC, $A5FD, $A605..$A625, $9875, plus $A0B3 in CtBattleBegin) is gone with it.

Audit (`CT_BATTLE_WIPEAUDIT`, the seed's non-zero ranges before the wipe): every `--battletest` seed in the suite is
a PRE-battle field image (`$A0B3` 80, `$974A` 01 01 01 81 81 80 .., `$96F5` 80s) carrying the field engine's work RAM
from $2D01 up in hundreds of ranges. Nothing the battle reads survives the wipe in the ROM, and nothing did in the
port either except three item-menu checks, whose pinned `itemtrace item-open` line printed `$9F35/$9F36` = 02/E0 and
`$93EF/$93F4` = 80/80 -- field leftovers, read raw before the port's own writes (`$93EF := 40`, `$9F35 := itemId`)
land later in the same fight. Re-pinned to the ROM's 00. Mid-battle seeds (bat2, wa*) never pass through
CtBattleBegin, so they are not wiped -- they are post-wipe images by construction.
### The pre-roll's U, one level deeper (same day) -- and why it stops here

The 4-frame offset between the port's victim-track timeline and the ROM's (the A9 at rel 54 vs 58) is the
pre-roll residue: 13 + U, with the port shipping U = 8. Measured on press_action, frame by frame, `$CACE/$CACF/
$CAD0/$CA26/$CA20`:

    GATO's physical, key C0/C4 (U = 11):   f478 entry (C0, $CAD0 = 4)   f479 push C0   f480 serviced, cursor C1
      f481 push C1   f482 serviced, C2   f483 push   f484 serviced, C3   f485 push   f486 serviced, 2nd entry (C4,
      $CAD0 = 6) = a drain frame   f487 push C4   f488 serviced AND push C5   f489 serviced, C6 = done   f502 op 1
    Crono's physical, key 20/24 (U = 8):   f463 entry   f464 push 20   f465 serviced + push 21   f466 + push 22
      f467 + push 23   f468 serviced, 2nd entry   f469 push 24   f470 serviced + push 25   f471 serviced, done   f484 op 1

The structure is CD/132E: one 0x400-byte packet per sheet index, `$CACF` from 0 to `$CAD0` (4, then 2), a drain
frame at each entry; each packet costs one frame between its push (CD:3F67) and its service (CD:3ECD, the main
thread's CD/04DF after the CD/3E75 frame wait). That is 4 + 2 + 2 = 8 = U for every fast key. The extra frames
of the C key are the DECOMPRESSION of the next sheet spilling past the NMI: after C0..C3's service the next push
only lands in the following frame (the NMI those frames take is the lag path, D1:E891), while after C4's service
the C5 push lands in the same frame -- the same CF/E000 format word (5555, method 1 = CD/1445's H-flip through
C0/FD00 on all 32 groups) on both. `$CD02` (the pause toggle CD/3E44 tests) is 0 throughout: not an APU wait.

So U = packets + drains + (the packets whose predecessor's decompression crosses the frame), and the last term is
CPU time over the sheet's data, not a count in a table. Without a cycle model of CD/1368 the port cannot price
it; the 13 + 8 floor stays, the residue stays named (4 frames on the C0/C4 key, the commonest enemy physical).


---

# G — THE WHOLE `C1/B8BB` FAMILY IS ONE MACHINE, AND THE PORT'S RETURN TYPE IS THE WRONG SHAPE (2026-09-11)

§F.3 left "every other value falls back to the lead PC" as the open work and called it a port
decision. It is worse than a port decision: **seven of the nine witnessed values are not a single
seat at all.** The ROM's resolver does not return a target — it fills a **LIST** at `$AECC` and puts
the COUNT in `$AECB`. `battleAiTarget` returns an `int`, so values 01 and 02 cannot be expressed by
it even once their handlers are read.

## G.1 The table is 24 entries, not 20

`0x1B8BB`, words:

```
A3F6 A3F7 A411 A42E A43D A452 A4AF A4E0 A508 A541 A54B A555 A55F A569 A573 A5A3 A5D3 A603
A633 A663 A693 A6C3 A6ED A709
```

§F.1's "at least 20" was a floor read off the first twenty. Values `0x14`-`0x17` have handlers too.

## G.2 The nine witnessed values, disassembled

`tools/dis.py <off> <len> 1 0` — **X is SIXTEEN BITS here** (`CPY #0003`), and decoding with the
default 8-bit X desynchronises the whole block into `BRK`/`WAI` garbage. That is what had made this
family look unreadable.

| value | handler | what it does |
|---|---|---|
| **00** | `C1/A3F6` | a bare `RTS`. Sets **nothing** — not even `$AECB`. |
| **01** | `C1/A3F7` | **ALL LIVING PCs.** `Y = 0..2`, skip `$AEFF,Y == $FF`, else `STA $AECC,X / INX`; ends `TXA / STA $AECB` = the count. |
| **02** | `C1/A411` | `JSR C1/A3F7`, then the same loop for `Y = 3..$0A` — **all living combatants**, PCs then enemies. |
| **03** | `C1/A42E` | **SELF**: `$AECC = $B252 + 3`, `$AECB = 1`. |
| **04** | `C1/A43D` | `$AECC = $B19E[$B252 * 4] & $0F`, `$AECB = 1` — a **stored** target out of the enemy's own record, not a computed one. |
| **05** | `C1/A452` | random living PC — **ported**. |
| **06** | `C1/A4AF` | nearest living PC — **ported** (§F.1/F.2). |
| **0A** | `C1/A54B` | `$0A = $1E`, `JSR C1/AD68`. `0B`/`0C`/`0D` are the same with `$1F`/`$20`/`$21`. |
| **0E** | `C1/A573` | the three-seat scan with `$AF1F = $02`. |
| **0F** | `C1/A5A3` | the same with `$AF1F = $80`. |
| **10** | `C1/A5D3` | the same with `$AF1F = $04`. |

## G.3 `C1/AE70` — the one candidate filter every one of them runs

```
AE70  TDC / STZ $0B
AE73  LDA $AF1E / TAY              the seat being tested (0/1/2), held in $AF1E by the caller
AE77  LDA $AEFF,Y / CMP #FF / BEQ ->AED0      absent/dead marker -> skip
AE7E  REP #20
AE80  LDA $FDA80B,X / STA $0C      X = seat*2 -> the seat's BLOCK BASE, 16-bit
AE86  CLC / ADC $0A / TAX          + the BLOCK OFFSET the caller put in $0A
AE8A  TDC / SEP #20
AE8D  LDA $0000,X / BIT $AF1F      the block byte at that offset, against the caller's MASK
AE93  BEQ ->AED0                   no bit in common -> skip
AE95  LDX $0C / LDA $001D,X / BMI ->AED0      block+$1D bit7 = dead -> skip
AE9C  LDA $AECB / TAY / LDA $AF1E / STA $AECC,Y / INC $AECB     APPEND the seat, bump the count
AEAA  LDX $0C / LDA $0020,X / BIT #20 / BEQ +   -> ORA #40 into the entry, INC $AF20
AEBE            LDA $0020,X / BIT #10 / BEQ +   -> ORA #80 into the entry, INC $AF21
AED0  STZ $0D / RTS
```

So `$0A` is a **block offset** and `$AF1F` a **bit mask**, and the appended byte carries two flag
bits ($40 and $80) taken from the seat's block `+$20`, each with its own counter (`$AF20`, `$AF21`).

`$FD:A80B` = `5E2D 5EAD 5F2D 5FAD 602D 60AD` — which is exactly `battleBlock()`'s `0x5E2D + idx*0x80`
for the three PC seats, so the port already has the base this filter needs.

`C1/AD68` is the same three-seat sweep with `$AF1F = $FF` (match any bit) and `$0A` supplied by the
caller — which is why `0A`/`0B`/`0C`/`0D` are four one-liners. `C1/A573`/`A5A3`/`A5D3` inline the
sweep with `$0A = $1E` fixed and the mask varying (`$02`/`$80`/`$04`).

`C1/ADA1` is the tail every one of them ends on: with `$AECB >= 2` and `$AF21` non-zero it walks the
`$AECC` list filtering entries against `$B23A,X` — the last-target list §F's note already mentions as
the cond-2A handler's `$B23A` scan. So that scan is NOT handler-level after all; it is the resolver's
own reduction step, and §F's parenthetical leaving it out should be re-read in that light.

## G.4 ⚠ What porting this actually costs, stated before anyone starts

1. ~~**`battleAiTarget` must return a LIST**, not an `int`. Values 01 and 02 are inherently
   multi-target; a single-seat return cannot carry them, and squeezing them into "the first one"
   would be an invention with no ROM behind it.~~ ⛔ **WRONG IN ITS SECOND HALF — see §I.**
   `C1/AC3F` is `LDA $AECC / STA $B2AE`: the ROM itself puts the list's FIRST entry in the
   single-target shadow. That squeeze is not an invention, it is the consumer's own last instruction.
   The list is not lost either — `C1/AC33` mirrors all eleven entries into `$AD8E` first. One target
   and many are the same resolution read through two doors, so the `int` was never what stood in the
   way. The rest of this list (2, 3, 4) still holds.
2. `$AECB` / `$AECC` / `$AF1E` / `$AF1F` / `$AF20` / `$AF21` become real WRAM this port maintains.
   `$AEFF` already is (§F.4).
3. `C1/ADA1`'s reduction has to land with it, or a multi-entry list is produced and never narrowed.
4. **The gates `$96F5,X` / `$9FF7,X` are NOT what this path tests** — `C1/AE70` tests `$AEFF,Y` and
   block `+$1D` bit7, both of which the port already maintains. So §F.4's stand-in problem does not
   apply here: this filter is portable as the ROM's own bytes.

## G.5 ⚠ And it is not gradable by anything on disk today

Same wall as §F.5. The targeting byte reaching 01 is witnessed in seven of the nine captures, but
those are **mid-battle** states, which `[[ct-defects-2026-09]]` records as not seeding the port. A
micro-execution sweep is the shape that would work — seed `$AEFF`, the three blocks and `$0A`/`$AF1F`,
run `C1/B8BB[value*2]` in the oracle, compare `$AECB`/`$AECC`/`$AF20`/`$AF21` — and no bank-`$C1`
micro-execution harness exists yet (every `runSweep*` in `main.c` drives bank `$C0`/`$C2`).
**Build that harness first.** Porting seven handlers against nothing is how a green check that grades
agreement rather than correctness gets made.

---

# H — §G.5's PREREQUISITE IS BUILT, AND THE TWELVE RESOLVERS ARE PORTED AND GRADED (2026-09-11)

`--sweepaitarget`: **1065/1065 byte-exact, 0 mismatch, 0 lost.** Ten single-fact mutations, ten red.

## H.1 The harness

The **first bank-`$C1` micro-execution harness in this tree** — every other `runSweep*` in `main.c`
drives bank `$C0`/`$C2`, which is why §G.5 said to build it before porting anything.

Entry state, and none of it is guessed:

| | value | why |
|---|---|---|
| DB | **`$7E`** | `C1/AE70`'s `LDA $0000,X` indexes a block base **above `$2000`**, which bank 0 does not mirror; `$AECC`/`$AEFF` are `$7E` too. Mutating it to `$00` is one of the ten mutations, and it is red. |
| DP | `$0000` | `STZ $0B`, `STA $0C`, `STX $0A` |
| M / X | 8 / **16** | `CPY #0003`, `LDX #0002` |
| PC | `rom[C1/B8BB + value*2]` | read from the ROM's **own** table, never a list in the harness, so a mis-transcribed entry address cannot hide |

The RTS sentinel is the same trick the field sweeps use: `SP = $01FD` with `$01FE/$01FF = FD FF`, so
the handler's own `RTS` lands on `$C1:FFFE`.

## H.2 What is compared

`$AECB`, `$AECC[0..$10]`, `$AEC9`/`$AECA`, `$AF1E`-`$AF21` and the direct-page scratch `$0A`-`$0D` —
all **poisoned first**, so a write the port MISSES shows as loudly as a wrong one. That is what makes
value 00's handler being a bare `RTS` that writes *nothing* a graded fact instead of an assumption.

**The count tally is part of the check**: `297 empty / 534 single / 234 MULTI`. A family whose every
case resolved to one target would be indistinguishable from the single-seat model this port carries,
so `MULTI` going to zero means the sweep has stopped exercising the difference.

## H.3 The two skip classes are MEASURED, not asserted

The port decides both, so leaving them on trust would let it delete its own mismatches from the
denominator:

* **`a4e0-tailed` (84)** — `C1/ADA1` tail-calls `C1/A4E0` when `$AF20 != 0` and the winner carries bit
  `$40`. `A4E0` descends into `C1/0003 → C1/2986`, which branches on `$96F5,X` / `$9FF7,X` — the two
  bytes §F.4 records as written by nothing in this engine. Those cases **still micro-execute** and
  still compare every byte `A4E0` cannot touch; only `$AECB` and `$AECC[0]` are excused.
* **`rom-nonterminating` (15)** — ⭐ and this is a finding, not a limitation. `C1/ADA1` variant A's
  `BPL ->ADB3` (`ADD1`, offset `$E0` = −32, bytes `B9 CC AE 10 E0`) branches back to the **compare**
  with both cursors unchanged, so a ring match on an entry whose bit7 is clear re-tests the same entry
  for ever. **The ROM does not return.** The harness proves it rather than claiming it: it runs the
  oracle on those seeds anyway and FAILS if the ROM *does* come back.

## H.4 ⚠ Ported and graded is NOT wired

`CtBattleAiResolveList` is graded here and called from nowhere else. `battleAiTarget` still resolves
05 and 06 and still falls back to the lead PC for everything else. Wiring it means carrying the LIST
into the action layer — §G.4's four costs — and that is its own pass, deliberately not squeezed onto
the end of this one. What has changed is that the next pass now has a grader to build against.

---

# I — `C1/AC14`, THE CONSUMER — AND §G.4's FIRST COST WAS WRONG (2026-09-11)

§H ported the twelve resolvers and said wiring them was "its own pass". This is that pass's first
half, and it turned out to be fifty bytes rather than a subsystem: **the list's consumer is a real
routine in the ROM, and it decides both of the questions §G.4 said the port would have to decide.**

## I.1 The routine

```
AC14  20 46 AC               JSR C1/AC46      clear BOTH lists to $FF (I.2)
AC17  AE D2 B1 / E8          LDX $B1D2 / INX  the bank-CC script cursor, X SIXTEEN BITS
AC1B  8E D2 B1               STX $B1D2        consumed BEFORE the value is read
AC1E  BF 00 00 CC            LDA $CC0000,X    the targeting value, straight out of the script
AC22  10 08                  BPL ->AC2C       bit7 CLEAR -> a resolver index
AC24  AD D8 AE / 8D CC AE    LDA $AED8 / STA $AECC     bit7 SET -> a LITERAL target...
AC2A  80 13                  BRA ->AC3F                ...jumping PAST the copy loop
AC2C  0A / AA                ASL A / TAX      value*2
AC2D  FC BB B8               JSR ($B8BB,X)    the resolver — table in the PROGRAM bank
AC31  7B AA                  TDC / TAX
AC33  BD CC AE / 9D 8E AD / E8 / E0 0B 00 / 90 F4   copy $AECC[0..$0A] -> $AD8E[0..$0A]
AC3F  AD CC AE / 8D AE B2    LDA $AECC / STA $B2AE
AC45  60                     RTS

AC46  7B AA                  TDC / TAX
AC48  A9 FF                  LDA #FF
AC4A  9D CC AE / 9D 8E AD    STA $AECC,X / STA $AD8E,X
AC50  E8 / E0 0B 00 / 90 F4  INX / CPX #000B / BCC ->AC4A      ELEVEN entries of each
AC56  60                     RTS
```

## I.2 The two facts §G.4 got wrong, and the one it did not have

⭐ **The single-target shadow is the list's FIRST entry, and the ROM is what says so.** `AC3F` stores
`$AECC[0]` into `$B2AE` on *both* paths. §G.4 called exactly that an invention. It is not; it is the
consumer's own tail. And the multi form is not discarded to get there — `AC33` mirrors all eleven
entries into `$AD8E` first, which bank `$C1` reads from **42 sites** (indexed reads at `C1/862B`,
`9AF8`, `A104`, `A34C`, `BAEE`, `BBB6`, `C644`, `C6C4`, `C6E5`, `C716`, `D50F`, `DA8E`, `DB2C`,
`DD90`, and stores at another 28). So a caller that wants one target reads `$B2AE` and a caller that
wants the set reads `$AD8E`; they are the same resolution, not two mechanisms.

⭐ **"No target" is `$FF`, not the lead PC.** `C1/AC46` fills both lists with `$FF`. A resolver that
appends nothing leaves `$AECC[0] = $FF`, so `$B2AE = $FF`. `battleAiTarget`'s fall back to
`battleLeadPc` — which its own comment already flags as a port decision and not the ROM's — now has
the byte the ROM uses in its place. **387 of the sweep's 1335 cases resolve to no target**, so this
is the common case, not an edge one. (Note what `0` would have meant: seat 0, the first PC. The
choice of `$FF` is what makes "nothing" expressible at all, and it is one of the seven mutations.)

⚠ **The literal path's asymmetry.** `AC2A`'s `BRA ->AC3F` jumps *past* the copy loop, so on a bit7
value `$AD8E` stays all `$FF` from the clear while `$AECC[0]` carries the literal. `$AED8` itself has
exactly **one writer in the whole ROM** — `C1/AF0E LDA $AECC / AND #0F / STA $AED8` — and one reader,
`AC24`. So the literal path replays the low nibble of a *previous* resolution.

⚠ **`C1/AC46`'s `TDC` is load-bearing for `C1/AC2D`.** X is 16-bit in this block, so `TAX` transfers
the accumulator's B half too. `7B` at `AC46` zeroes all sixteen bits and every `LDA` between is
8-bit, so B is still 0 when `AC2D` runs; without it `JSR ($B8BB,X)` would index the handler table
with a stale high byte. Same shape as `C2/62ED`'s `AND #00FF` guarding its caller's `TAX`
(`src/world.c`) — a zeroing in the callee that the caller silently depends on. Neither is visible
from the caller alone, which is how both got missed the first time.

## I.3 Ported and graded

`src/battle.c` `battleAiTargetRequest` / `battleAiTargetClear`, exported as `CtBattleAiTargetRequest`.
`--sweepaitarget` phase 2: **1335/1335 byte-exact**, over `$AECC[0..$10]`, `$AD8E[0..$10]`, `$B2AE`,
the `$B1D2` cursor and the resolver's own outputs — all poisoned first, so a write the port MISSES is
as loud as a wrong one. `C1/AC14` is micro-executed **whole**, so the resolver runs inside the
oracle's own `JSR ($B8BB,X)` and the composition is graded end to end rather than piece by piece.

**The targeting value is not seeded.** `AC1E` reads it from the bank-`$CC` script at the cursor, so
the sweep points `$B1D2` at a real ROM byte holding the value it wants (scanned out of bank `$CC`).
A port that read the value from anywhere else — `$AEE5`, a parameter, the wrong bank — cannot pass,
and the cursor's own advance is compared as an output byte.

Seven mutations, seven red (17 of 17 on the check as a whole): the clear's fill `$FF` -> `0`; its
length 11 -> 17; reading the value at the un-advanced cursor; the literal's source `$AED8` -> `$AEE5`;
the literal path mirroring into `$AD8E` too; the mirror one entry short; and `$B2AE` taking
`$AECC[1]` instead of `$AECC[0]`.

⚠ **An eighteenth fact was NOT graded for a while and the runner is what said so.** Adding phase 2
copied phase 1's CPU entry-state line verbatim, which made the `DB = $7E` mutation's anchor match
three times in `main.c`; `regress.py --mutate` refuses a `find` that matches more than once and
reported `STALE` rather than grading the wrong site. The three sites now carry distinguishing
comments and the anchor names the phase-1 one. A sweep of **all 544 declared anchors in the suite**
found no others — see `[[ct-sweepops-blindness-classes]]`, which records this failure mode from a
previous occurrence: a `find` that no longer matches is a mutation silently not graded.

## I.4 What is still open

`CtBattleAiTargetRequest` is graded and **not yet wired**, for the same reason `CtBattleAiResolveList`
is not: values **05** and **06** — the two the live engine actually uses — are the two
`battleAiResolveList` still returns `-1` for. `05` draws the battle RNG and `06` descends into
`C1/2986`'s `$96F5`/`$9FF7` gates, the two bytes §F.4 records as written by nothing in this engine.
Wiring `AC14` in before those two fill the list would route the common case through a resolver that
declines to resolve it. So the remaining work is exactly: make `05`/`06` list-fillers (`$AECC[0] =
seat`, `$AECB = 1`, and `06`'s `$AD8D = 1`), then the action layer reads `$B2AE` and `$AD8E` instead
of an `int`. §G.4's costs 2 and 3 are paid; cost 1 is void; cost 4 was never a problem.

⭐ **A FIFTH COST NONE OF THEM NAMED, found 2026-09-11 and now the real blocker: `$AEFF` IS NOT
MAINTAINED ON DEATH.** Every resolver in this family gates on `$AEFF,Y == $FF`, and the routine that
writes that byte is `C1/B279` — `B2BE LDA #FF / STA $AEFF,Y`, on the arm a dead *or hidden*
combatant takes. This port's `battleDeadFlagRefresh` was a PREFIX of `C1/B279`: it stopped at `B2B6`,
exactly where `[[battle_crit_beatb_flash]]` §3.2's transcription stopped. So in the port `$AEFF` is
written once at staging and never again, and a PC who dies mid-battle would stay in every candidate
list the resolvers build. Wiring them before fixing that would have shipped a targeting family that
cheerfully aims at corpses.

The living half is fixed and graded (`--sweepdeadflag`, 450/450: `B28B-B28E` republishes `$AEFF,Y`,
`B291` clears `$24`, `B293-B298` sets bit0 of `$B03A,Y` — three stores the port did not have, all of
them no-ops on a live boot, which is why only a poisoning micro-execution sweep could grade them).
The dead/hidden half landed on the second pass the same day, for **enemies**: `B2B9`-`B2D8` is
unconditional and `C1/B3A0` — the enemy tail — turned out to be five instructions. Seats 3..10 now
reach `$AEFF = $FF`. **PCs do not**: `B2D9`'s `CPY #0003 / BCC ->B2E1` sends seats 0-2 into the
`$FD:A849` / `C1/BC60` death-effect dispatch, which is its own subsystem. 792/792 on every arm,
15 mutations.

⚠⚠ **AND THE CALLER IS THE OTHER HALF OF THE BLOCKER, which this pass found and did not fix.** The
ROM runs the 11-slot refresh `C1/B223` from the battle MAIN LOOP (`C1/8626`, `C1/8787`) as well as
from the boot (`C1/8130`); `src/battle.c` calls it **only at the boot**. So no matter how complete
`C1/B279` gets, a combatant who dies mid-battle is never re-derived and `$AEFF,Y` never reaches `$FF`
for him.

⭐ **AND THE OBVIOUS FIX — "call it every frame" — IS WRONG, MEASURED RATHER THAN ASSUMED.** The
sweep's own cursor `$B2D3` is written by `C1/B223` (`TDC / STA $B2D3`) and bumped at `C1/B260`, so
`--writer` names the frames it runs on:

```
build/ct.exe --monitor "7EB2D3:1" --from saves/battle_re/bat3_lake.state --frames 90 --writer
```

**42 of 90 frames**, and the pattern is irregular — `..X..X.XX.X.XX.X.X..X.X..X.X..X.XX.X.X..` —
but reproduces on a second, unrelated capture (`bat8_lake.state`: `.XX.X.X..X.X..X.XX.X.X..X.X..X...`).
Roughly two frames in five, which is the same rhythm `[[ct-battle-idle-walk-9841]]` records for the
battle's sub-frame dispatch. So the refresh is **gated by whatever gates `C1/8626`/`C1/8787`**, and a
per-frame call would be a heuristic standing in for a real ROM mechanism — the thing the mandate names
first. Two attempts to find those two sites' enclosing routine failed: neither is preceded by a
flow-terminating instruction whose successor anything calls (searched 0x400 back, all `JSR`/`JMP` in
bank `$C1` plus every `JSL` ROM-wide), and the battle mode table at `C1/$0051` has **no** entry in
`$8000-$8FFF`. That identification is the next piece, and it is not guessable from here.

⭐ **THE GATE IS DERIVED NOW — by tracing the ROM, after two static attempts failed.**
`build/ct.exe --trace 900000 --from saves/battle_re/bat3_lake.state` and reading the instruction
before each `C1/B223`: **30x `C1/8130`, 14x `C1/8626`, 0x `C1/8787`**. `C1/8626` sits inside dispatch
handler **`C1/8461`** — index 12 of the thirteen-slot `JSR ($B92D,X)` loop at `C1/8318-8340`, whose
one-hot mask `$AEC3` makes index 12 **bit 0 of `$B186`**. `C1/8130` is the main pass's own tail
(`C1/83B5 JMP C1/8120`, past a `$2989` bit5 test that battle boot has already cleared). And
`C1/8130`'s old "battle BOOT" label was wrong: the capture is MID-battle and it runs 30 times.
Details in `[[battle_crit_beatb_flash]]`.

So the cadence belongs to the **battle main loop's pass rate** plus `$B186` bit0 — neither of which
this port models as such. That is the shape the next pass has to match, and it is a scheduler
question, not a `C1/B279` one.

**Order of work: model the main-loop pass (and `$B186`), then the PC death dispatch, then 05/06 as
list-fillers, then the action layer reads `$B2AE`/`$AD8E`.**

---

# ADDENDUM 2026-10-04 -- what stops the walkers is $A0FD, not the turn

CF/EFAF (the NMI chain's CF/E748 `JSR CF/EFAF`): `LDA $A013 / BNE ->EFBD` (the combatants' loop) `/ LDA $A0FD / BEQ ->
JMP CF/F978 / RTS`. $A0FD's only writers are C1/4058's `INC` and the loop head's `STZ` at C1/1073 (after C1/106E's
`$A10E` test) -- a ROM-wide scan for 9C/EE/8D/CE FD A0 finds nothing else. So an enemy's turn walks on through its
handler (the decode, the name box CD/0295) and stops on C1/4058's frame. The port stopped the walkers for the whole
turn (battleThinkPending); now battleMoveTick's walker half reads $A0FD (written at every C1/4058 model, zeroed by
battleIterHead) and only the handler half (C1/3714, the main loop's) waits for the turn. MEASURED, the rule's oracle,
saves/field_re/m81_nuns_lake.state, no input: the kiss's name box `$CCEB = A0` (CD:02BE) f1512, the returns stepping
(CF:F9C5) to f1513, `$A0FD = 01` (C1:405B) f1513, `$A013 = 01` f1515. Same fight: the waits' menu passes C1/10E3 /
C1/10FA zero $E5 (C1/10EB / C1/1102) when they consume $993A, which holds the $A4 cycle (CF/E3FD) through the turn
(`$E5 = 00` C1:10ED f1505). tools/naga_walk_cmp.py: 3600 frames of the four Naga-ettes identical (was 364 differing).
