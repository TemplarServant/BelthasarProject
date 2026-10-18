# The PHANTOM pre-attack pose — "the hetake make a movement/animation that does not exist before the correct attack animation"

Report 5 (translated from Portuguese), user, 2026-08-22:

> sometimes the hetake make a movement/animation that does not exist before doing the correct attack animation

**REPRODUCED, at the pixel level, and it is NOT the anim-script interpreter.** Our published pose stream is
byte-identical to the oracle's for every Hetake attack in the user's own scenario (§2). The phantom is a
SECOND, non-script render path — `battleHetakeRender`'s `approaching` arm — that draws the attack-windup
cell (`anim 5` entry 0, assembly index **0x03**) during the AI **think** window, a window in which the ROM
arms no pose window at all. Its length is 3 frames when nothing else is running and **183 frames** when a PC
action overlaps it: that is the "sometimes".

A second, independent "movement that does not exist" was measured on the way: every Hetake hit permanently walks
**Crono's live foot** 3-4 px, off an `A8 04` that belongs to the script's SCRATCH track (§5). The oracle's
`$1D0C[0]/$1D23[0]` read `A0/46` for the entire fight.

---

## 0. TL;DR

| | |
|---|---|
| the phantom | assembly index **0x03** — `CtBattleEnemyAnimWin(0x5E, anim 5, facing 0)` entry 0, the attack WINDUP cell |
| when | the whole `mvPendAtk == 3+slot` window: from `enemy-think` to `enemy-arrive` |
| how long | **3 frames** normally; **183 frames** measured when a PC action holds the think (`battleThinkTick`, battle.c:5235 `if (h->actActive) continue;`) |
| the ROM's law | the displayed cell is `$967B`, published ONLY by `C1/4304` behind the `$A124,cmb != 0` gate; `$A124`'s SOLE writer is the playAnim tail `C1/4D61` |
| the ROM's measurement | `$A124` is **all-zero for all 11 combatants** from f0 to f436 — through the AI turn-start (f402) AND the whole pre-roll (f405-f436). First non-zero: f437 (`C1:4D64`, cmb5, mode 2) |
| the ROM's render | oracle framebuffer in the acting Hetake's sprite box is **byte-identical at f398 / f403 / f404** and first differs at **f437** |
| ours | framebuffer differs from idle at **f500, f501, f502** (the 3 think frames) with **exactly the same 2281-byte delta** as the real pose-03 frame f536, then reverts to idle for 33 frames, then plays the real attack |
| the C site | `src/field.c:3889` (and its `approaching` term, `src/field.c:3825` / `:3826`) |
| the fix | the `approaching && !acting` case must publish NO pose (`-1`) so the render keeps the idle cells — the ROM has no writer there |
| bonus defect | `src/battle.c:3672-3677` takes `animKnockDist` from ANY track, including track 15 (an effect object); the Hetake's `A8 04` scratch op therefore knocks the VICTIM (§5) |

---

## 1. The scenario and the exact repro command lines

The user's state, `saves/battle_crono_solo_lake.state` (loc 19, storyline 03, solo Crono, three Hetakes
`$AF02 = 5E 5E 5E FF`). Enemy seats: cmb3 `(120,63)`, cmb4 `(88,95)`, cmb5 `(168,127)`; victim cmb0 Crono
`(160,70)`.

```
# ORACLE — walk left into the ambush, then watch the pose PUBLISH and the pose-window MODE array
<probe> --monitor "7E967B:1,7EA013:1,7E9877:1,7EAE91:1" --from saves/battle_crono_solo_lake.state \
        --input "40:400,0:1600" --diff --writer --frames 2000
<probe> --monitor "7EA124:11,7EAE91:1,7EA013:1"        --from saves/battle_crono_solo_lake.state \
        --input "40:400,0:400"  --diff --writer --frames 810
<probe> --monitor "7E1D0C:11,7E1D23:11"                --from saves/battle_crono_solo_lake.state \
        --input "40:400,0:1600" --diff --frames 1000
<probe> --monitor "7E974A:8,7E96DE:8,7EAE91:1"         --from saves/battle_crono_solo_lake.state \
        --input "40:400,0:400"  --diff --writer --frames 810

# ORACLE — the framebuffer at any frame f (k = f-400)
<probe> --peek saves/battle_crono_solo_lake.state out.raw --btnscript "40:400,0:$k"
python tools/ct.py fb2png out.raw out.png ; python tools/ct.py oam out.raw.oam

# OURS — the same walk-in, full trace
CT_BATTLE_TRACE=1 CT_BATTLE_ASCTRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/Belthasar.exe --assets ct_assets.dat --scene 19 \
  --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest o.raw --frames 900 --walkscript "l:400,."

# OURS — the composed frame at module frame N (interactive path; NO --selftest; self-exits)
CT_BATTLESHOT_FIELD=out.raw CT_BATTLESHOT_FIELD_AT=N SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/Belthasar.exe --assets ct_assets.dat --scene 19 \
  --loadwram saves/forest_re/battle_crono_solo.dumpwram --walkscript "l:400,."

# OURS — the LONG phantom (a PC action holds the think). forest_pre has Marle, so a PC can act.
CT_BATTLE_SCRIPT="n:283,a:2,n:4,a:2,n:2000" CT_BATTLE_TRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/Belthasar.exe --assets ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram
#   -> battletrace f510 enemy-think / f510 pc-act / f651 act-end / f693 enemy-arrive   == 183 held frames
CT_BATTLESHOT=out.raw CT_BATTLESHOT_AT=600 CT_BATTLE_SCRIPT="n:283,a:2,n:4,a:2,n:2000" ... --battletest ...
```

⚠ `--battletest saves/forest_re/battle_crono_solo.raw.wram` **FAILS staging** on the current build
(`$29BF slot0 rec mismatch ... got 00 00 00 00`, `$2989=00 want 02`, `FAIL (staging 1/7)`), so the fight
never runs from that seed. The `--loadwram` + `--walkscript` path from the same capture works. Recorded as
open question O4; every number below came from the working paths.

---

## 2. FIRST: the anim-script interpreter is INNOCENT — the published pose stream is exact

Oracle `$967B` writer stream (`C1:4307` = the action pose engine's publish; `C1:2EF2`/`C1:342B` = the two
IDLE steppers) against our `ascpose` trace, expressed as rel = frame - dispatch:

| | oracle cmb5 (disp f405) | ours cmb5 (disp f503) | oracle cmb4 (f606) | ours cmb4 (f286) | oracle cmb3 (f815) | ours cmb3 (f715) |
|---|---|---|---|---|---|---|
| `02 05` pose **03** | 33 | 33 ✓ | 33 | 33 ✓ | 33 | 33 ✓ |
| `03 07` pose **04** | 61 | 61 ✓ | 68 | 68 ✓ | 52 | 53 (+1) |
| victim flinch **66** | 66 | 66 ✓ | 73 | 73 ✓ | 57 | 58 (+1) |
| `03 07` pose **05** | 68 | 68 ✓ | 75 | 75 ✓ | 59 | 60 (+1) |
| victim **C8** | 87 | 87 ✓ | 94 | 94 ✓ | 78 | 79 (+1) |
| `02 00` pose **06** | 91 | 91 ✓ | 98 | 98 ✓ | 82 | 83 (+1) |
| `06 03` pose **01** | 108 | 108 ✓ | 120 | 120 ✓ | 95 | 96 (+1) |

Two of the three attacks land every landmark on the oracle's own frame; the third is a uniform +1 (see O3).
**No pose we arm is absent from the ROM's stream, and no pose the ROM arms is missing from ours.** The
"animation that does not exist" is therefore not a script-arm error — it is drawn by something else.

The SLIDE geometry agrees too (oracle `$1D0C/$1D23` vs our `asc SLIDE` lines):

| actor | oracle travel | oracle steps | ours |
|---|---|---|---|
| cmb5 `(168,127)` | → `(161,81)` f441-f463 | 23 | `(168,127)->(163,81)` n=**23** |
| cmb4 `(88,95)` | → `(148,73)` f642-f671 | 30 | `(88,95)->(148,73)` n=**30** |
| cmb3 `(120,63)` | → `(148,70)` f851-f864 | 14 | `(120,63)->(151,66)` n=15 — the victim has DRIFTED, §5 |

---

## 3. THE ROM'S LAW: nothing can change an enemy's cell between the AI turn-start and the first playAnim

`python tools/dis.py 142F0 0x30 1 0` — the publish, at the tail of the pose engine `C1/424C`:

```
  14300: LDA $E40000,X
  14304: STA $967B          ; <<< THE PUBLISHED ASSEMBLY INDEX -- what the sprite draws
  14307: JSR C1/1C4A        ; stage the cells
  1430A: LDA #02 / 1430C: STA $A012
```

and it is reached only through `C1/4250`'s scan, whose per-combatant gate is `$A124,X != 0`
(`battle_enemy_anim_script.md` §7). `python tools/dis.py 14D50 0x28 1 0` — the ONLY writer of `$A124`:

```
  14D5C: LDA $8A / TAY
  14D5F: LDA $8E
  14D61: STA $A124,Y        ; <<< the playAnim opcode tail (C1/4D08 body, ops 02/03/04/05/06/26)
  14D64: TDC / STA $A12F,Y ... STA $A13A,Y / STA $A214,Y
```

So: **no anim script running ⇒ `$A124` is 0 ⇒ no publish ⇒ the enemy's cell cannot change.** Measured, on
the user's own state:

```
--monitor "7EA124:11,7EAE91:1,7EA013:1" --from saves/battle_crono_solo_lake.state --input "40:400,0:400" --diff --writer
  f0     7EA124=0000000000000000000000   7EAE91=80          7EA013=00
  f402   7EA124=0000000000000000000000   7EAE91=05*(C1:ACFC)  7EA013=00   <- the AI TURN-START picks cmb5
  f405   7EA124=0000000000000000000000   7EAE91=05          7EA013=01*(C1:40A9)   <- the DISPATCH
  f437   7EA124=0000000000020000000000*(C1:4D64)                          <- the FIRST arm, cmb5 mode 2
```

`$A124` is all-zero for **all eleven combatants** through the turn-start AND the whole pre-roll. The idle
selector does not move either (`--monitor "7E974A:8" --diff --writer`: no write at f402; the next write is
`C1:336A` at f602, after the action). And the RENDER confirms it — the acting Hetake's sprite box
`(150,95)-(190,135)` in the oracle's own framebuffer:

```
  f398  0 bytes differ (reference)     f437  782 bytes differ
  f403  0 bytes differ                 f440  782 bytes differ
  f404  0 bytes differ
```

**Two-directional: the RAM says no window is armed, and the pixels say the sprite does not change.** (The
OAM tile indices are useless for this — the anim engine streams pose PIXELS into the SAME tile numbers;
`060/062/064/066` for cmb5 at f398 and at f440 alike. Judge the framebuffer, not the OAM.)

---

## 4. OURS: the phantom, frame-exact

Our trace of the same fight (`--loadwram` + `--walkscript "l:400,."`), cmb5's action:

```
  battletrace f500 enemy-think     <- battleOpenAction sets h->mvPendAtk = 5+3 (battle.c:4384-4386)
  battletrace f503 enemy-arrive    <- battleArrive clears it (battle.c:3987-3989)
  asc arm      f524 (rel 21)
  ascpose      f536 cmb5 anim=05 cur=0 pose=03 dwell=5     <- the REAL first pose (rel 33)
```

Composed frames, same sprite box, against the idle reference f498:

```
  f498    0    f501  2281    f504    0     f520    0
  f499    0    f502  2281    f505    0     f536  2281   <- the REAL pose 03
  f500 2281    f503     0
```

**f500/f501/f502 carry exactly the same 2281-byte delta as f536** — i.e. our engine draws the real attack
windup cell (index 0x03) for the three think frames, reverts to idle for the 33-frame pre-roll, and then
plays the attack. The screenshots show it plainly: at f498 the bottom-right Hetake is the upright mushroom,
at f501 it is the dark collapsed windup silhouette, at f505 it is the mushroom again, at f536 it is the
windup again.

### 4.1 WHY "sometimes": the think is HELD while any other window animates

`src/battle.c:5231-5237`:

```c
static void battleThinkTick(CtBattleHost* h) {
  for (int slot = 0; slot < 8; slot++) {
    if (h->enThinkDelay[slot] <= 0) continue;
    if (h->actActive) continue;                    // a window animates -> hold the think (fire when free)
    if (--h->enThinkDelay[slot] > 0) continue;
    battleArrive(h, slot + 3);
```

`mvPendAtk` therefore stays set for as long as the think is held, and the phantom is drawn the whole time.
Measured (`forest_pre` + `CT_BATTLE_SCRIPT="n:283,a:2,n:4,a:2,n:2000"`):

```
  battletrace f510 enemy-think
  battletrace f510 pc-act        <- Crono's attack opens the SAME frame
  battletrace f651 act-end
  battletrace f693 enemy-arrive  <- 183 FRAMES of phantom
```

`CT_BATTLESHOT_AT=600` on that run shows the left-hand Hetake standing in the dark windup pose while Crono
is mid-swing on a different one. Three seconds. Without an overlapping PC action it is 3 frames
(`CT_BATTLE_ENEMY_THINK`, battle.c:901) — a flicker. **That is the whole of "sometimes": one bug, a duration
that swings between a 50 ms twitch and three seconds depending on whether the player happens to be acting.**

### 4.2 The exact C

`src/field.c:3825`, `:3826`, `:3889`:

```c
3825:    int approaching = h->mvPendAtk == 3 + slot;                      // walking in (dispatch deferred to arrival)
3826:    if (acting || approaching) {
 ...
3889:      else if (approaching && !acting) pose = haveWin ? wApp[0] : 0x03;   // windup rides the walk (f139-155)
```

`wApp` is `CtBattleEnemyAnimWin(0x5E, 5, 0, ...)` (line 3866) — the attack script's `02 05` window,
read at a **hardcoded facing 0**. The comment's justification ("windup rides the walk (f139-155)") is a
pre-interpreter capture reading, and §3 shows the ROM does not do it: it is the last surviving arm of the
rel ladder that `battle_enemy_anim_script.md` §7.3c retired for the `acting` case and left standing here.
There is no equivalent in `battleEnemyGenRender` or `battleGatoRender` — `mvPendAtk` appears in exactly one
render site (`grep -n approaching src/field.c` → 3825/3826/3889), which is why only the Hetakes show it.

### 4.3 The minimal mandate-safe change (NOT made — this pass is read-only on src/)

Make the think window publish **no pose**, so the render keeps the idle cells exactly as it already does for
the `acting && animParsed` pre-publish gap (field.c:3874):

```c
3889:      else if (approaching && !acting) pose = -1;   // the ROM arms NO pose window between the AI
                                                        // turn-start and the script's first playAnim:
                                                        // $A124 (sole writer C1/4D61) is all-zero from
                                                        // the $AE91 write (C1/ACFC, f402) to f437, and
                                                        // the oracle's own framebuffer is byte-identical
                                                        // at f398/f403/f404. Keep the idle cells.
```

`pose < 0` already falls into the idle-cell loop at field.c:3897, which draws at `hs->anchorX + hdx` — the
same expression the `else` idle branch uses, so the result is byte-identical to a bystander. Equivalently
(and preferably, since it deletes the invented concept rather than neutering it): delete line 3825, change
3826 to `if (acting) {`, and delete 3889 — a non-acting enemy then takes the plain idle branch. The two are
pixel-equivalent; the second is the one that leaves no dead `approaching` term behind.

**Do NOT "fix" this by making the think window shorter or by clamping `battleThinkTick`.** The think length
is right — the oracle's own turn-start→dispatch gap is 3 frames (`$AE91` f402 → `$A013` f405), the same
`CT_BATTLE_ENEMY_THINK` we use. The defect is purely that we draw something there.

---

## 5. THE SECOND PHANTOM MOVEMENT: the scratch track's `A8 04` walks the VICTIM

Oracle, the whole fight: `$1D0C[0] = A0` and `$1D23[0] = 46` on **every** row of
`--monitor "7E1D0C:11,7E1D23:11" --diff` from f199 to f1323. **Crono's live foot never moves.**

Ours, read off the `asc` trace's `t15 FWD ... from=(x,y)` line (which resolves the victim's live foot):

```
  action 1   from=(160,70)   == (A0,46)   ok
  action 2   from=(163,70)               <- +3 x after the first strike
  action 3   from=(163,66)               <- -4 y after the second
```

`src/battle.c:5521-5527` applies a PERSISTENT displacement to `mvLive[actTarget]` whenever
`h->animKnockDist > 1`. Its own comment asserts the opposite of what happens:

```c
5520:  // never restored). Mechanism-gated on animKnockDist (the Hetake script has no A9/A8 -> 0).
```

The Hetake script **does** have an `A8`, and `battleAnimParse` picks it up — `src/battle.c:3672-3677`:

```c
      else if (op == 0xA9 || op == 0xA8) {
        int hnd = battleAnimOpHandler(rom, romLen, op);
        if ((hnd == 0x6AF0 || hnd == 0x69C3) && len > 1 && q + 1 < romLen)
          h->animKnockDist = rom[q + 1];
      }
```

The walk covers **every** track of the script. `CD/72B9`'s track **15** is
`96B2: 24 FF | 02 00 | 73 03 | 1B 03 | 76 03 00 | 0C | A8 04 | 33 23 | 41 1E 0E | 41 1F 0F | 36 | 00` —
and `battle_enemy_anim_script.md` §3.1 already derives what that `A8 04` IS: the SCRATCH object walking
12 px off the victim to COMPUTE the approach point (`33 23` then stores it into the mode-0x31 slot). It
moves no combatant at all: §2.2, from `C1/762E`'s handlers, "a track index >= 8 does not address a
combatant … every handler branches `LDA $A010 / CMP #08 / BCS` into the effect-object space
(`$A2B6`, `$AB5F`)".

`kd = 4` → `>>8` floor → **+3 px**, which is exactly the 160→163 measured. Consequences: Crono drifts a few
px per hit for the whole fight, and every subsequent Hetake approach aims at the drifted victim (our third
approach is `n=15` to `(151,66)` where the oracle walks `n=14` to `(148,70)` — the +1-frame column in §2).

**Minimal mandate-safe change (NOT made):** capture `animKnockDist` only from a track index < 8 — the ROM's
own combatant/effect-object split at `C1/762E` and in the `A8`/`A9` handlers (`C1/69F1`, `C1/6B1E`:
`LDA $A5AA,Y / TAX / LDA $C0F700,X / STA $96DB,Y`, Y a combatant only below 8). `battleAnimParse` already
walks track-by-track, so the track index is in hand at line 3672. A tighter gate (the track's ROLE must be
the victim) is derivable from the same `$A014` binding and would be equally faithful; the < 8 split is the
one the ROM makes unconditionally.

---

## 6. RAM map used here

| addr | meaning | writers seen |
|---|---|---|
| `$967B` | the PUBLISHED assembly index — what the sprite draws | `C1:4307` (action pose engine), `C1:2EF2` / `C1:342B` (the two IDLE steppers) |
| `$A124,cmb` | the pose-window MODE (0 = idle, 1 = one-shot, 2 = loop); the publish gate | `C1:4D64` (playAnim tail) and `C1:42C4`/`C1:42E2` (the tick clearing it) — nothing else |
| `$A12F,cmb` / `$A13A,cmb` | the window dwell countdown / the entry cursor | same |
| `$A012` | one publish every OTHER frame, globally | `C1:430C` |
| `$AE91` | the AI turn-start: which combatant acts | `C1:ACFC` |
| `$A013` | action window open | `C1:40A9` set / `C1:41BD` clear |
| `$9877` | the dispatched anim selector | `C1:44E7` |
| `$974A,slot` | the enemy's IDLE anim selector | `C1:336A`, `CC:E351` — NOT touched at the turn-start |
| `$96DE,slot` | the enemy's live FACING = the window COLUMN | `C1:600C` (op `72`), `C1:638C` |
| `$1D0C,cmb` / `$1D23,cmb` | the live foot | the movers |

## 7. Frame timeline (oracle, `saves/battle_crono_solo_lake.state`, `--input "40:400,0:1600"`)

```
  f128   $2989 = 02                 the battle is staged
  f402   $AE91 = 05   (C1:ACFC)     AI turn-start: cmb5 acts        <-- OURS DRAWS THE PHANTOM HERE
  f405   $A013 = 01   (C1:40A9)     dispatch; $9877 = 14 (C1:44E7)  <-- and here
  f405..f436                        $A124 ALL ZERO; framebuffer FROZEN (f403 == f404 == f398)
  f437   $A124[5] = 02 (C1:4D64)    `02 05` arms                     the sprite first changes
  f438   $967B = 03   (C1:4307)     pose 03 published
  f441..f463                        the approach slide, 23 steps
  f466/f473                         pose 04 / 05      f471 victim flinch 66      f492 victim C8
  f496                              pose 06 (`02 00`)  f497..f508 the return slide
  f513                              pose 01 (`06 03` settle)
  f595   $A013 = 00   (C1:41BD)     span end; f600 the idle stepper resumes (C1:2EF2)
```

## 8. Open questions

- **O1 — the ROM's Hetakes WALK between actions and ours do not.** `--monitor "7E1D0C:11,7E1D23:11" --diff`:
  cmb5 walks `(168,127) -> (150,96)` over f1017-f1281 at 1 px / 4 frames, cmb4 `(88,95) -> (105,108)` over
  f1072-f1176, and the 4th action dispatches at f1283 from the walked-to position. This CONTRADICTS
  `battle_enemy_anim_script.md` OPEN 6 ("their AI mover mode byte is 0, so that machine is inert for them").
  Not derived here — the writer of those `$1D0C` stores was not captured.
- **O2 — our Hetake IDLE is a frozen baked assembly.** The ROM's idle stepper publishes `00 / 01 / 02 / 07`
  through `C1:342B` all fight (see the f998-f1280 block of the `$967B` monitor); `battleHetakeRender`'s idle
  branch draws `ctHetakeIdle[slot]` with "NO per-frame idle clock". Missing animation, not a phantom, and out
  of scope for this report — but it is why the phantom reads as a "movement": the sprite is otherwise static.
- **O3 — the third attack runs +1 frame late on every landmark.** Its cause is at least partly the victim
  drift of §5 (a 31 px approach at speed 2 is 15 steps, a 28 px one is 14); whether anything else contributes
  was not separated.
- **O4 — `--battletest saves/forest_re/battle_crono_solo.raw.wram` fails staging** (`$29BF` slots all zero,
  `$2989=00 want 02`, `$AF0D` id-mirror zero) while the same capture loaded through
  `--loadwram saves/forest_re/battle_crono_solo.dumpwram` runs the fight correctly. Not investigated.
- **O5 — the `haveWin` reads at field.c:3866-3869 hardcode facing 0.** The ROM resolves the window COLUMN
  from `$96DB,X` inside the pose tick (`C1/4283`), and this fight's Hetakes face 3 / 3 / 0 (`asc FACE` lines,
  oracle `$96DE` writes at `C1:600C`). After the §4.3 fix those windows only feed the `!animParsed` rel
  ladder, so nothing observed depends on it — recorded, not fixed.

---

## Adversarial review (2026-08-22, independent pass — every probe re-run, every address re-disassembled)

Binaries snapshotted to `%TMP%\ctwf3\phantom-adv\` (`ct_probe.exe`, `Belthasar.exe`, `SDL2.dll`); all runs
with cwd = repo root.

**Verdict: the CONCLUSION survives; two of the four evidence legs do not.** The phantom is real and I
reproduced it end to end (RAM, trace and pixels). But the dossier's headline framebuffer proof is
mis-measured, two "sole/only writer" claims are factually false against the ROM, one ROM citation points
at the wrong instruction, and the "the think length is right" instruction is asserted rather than derived.

### A. Re-verified and CONFIRMED (my own runs, my own dis)

| claim | my check |
|---|---|
| `C1/4304 STA $967B` + `C1/4307 JSR C1/1C4A` | `dis.py 142F0 0x30 1 0` — exact |
| `C1/4D61 STA $A124,Y` (playAnim tail) | `dis.py 14D50 0x28 1 0` — exact |
| the publish gate is `$A124,X != 0` | `dis.py 14250 0x60 1 0` — `LDA $A124,X / BEQ ->1425A`. **The dossier omits the SECOND half of the gate: `LDA $A12F,X / BEQ ->14269` — the scan proceeds only when the dwell is also 0.** |
| `$A124` all-zero f0..f436; f402 `$AE91=05*(C1:ACFC)`; f405 `$A013=01*(C1:40A9)`; f437 `$A124[5]=02*(C1:4D64)`; f438 `$967B=03*(C1:4307)` | `--monitor "7EA124:11,7EAE91:1,7EA013:1,7E967B:1" --from saves/battle_crono_solo_lake.state --input "40:400,0:400" --diff --writer --frames 810` — every frame number identical. **Stronger than the dossier says: `$967B` is not written AT ALL between f401 and f438** (the idle steppers `C1:2EF2`/`C1:342B`, which fire every 5-15 f up to f401, stop dead at the turn-start). |
| `$974A` untouched at the turn-start, next write `C1:336A` at f602 | `--monitor "7E974A:8,7EAE91:1" --diff --writer` — exact |
| `$1D0C[0]=A0`, `$1D23[0]=46` for the whole fight | `--monitor "7E1D0C:11,7E1D23:11" --diff --frames 2000` — constant f199 to f1999 |
| OURS: think f500 / arrive f503 / asc arm f524 / ascpose f536 pose=03 | `--loadwram saves/forest_re/battle_crono_solo.dumpwram --walkscript "l:400,."` — exact |
| OURS: box (150,95)-(190,135) = 2281 B at f500/501/502 and at f536, 0 B at f503/505/520 | `CT_BATTLESHOT_FIELD_AT` — exact. **4x crop of (145,92)-(195,140): f498 mushroom -> f500 dark windup creature -> f503 mushroom -> f536 windup.** The phantom is unambiguous. |
| OURS: the 183-frame held phantom | `CT_BATTLE_SCRIPT="n:283,a:2,n:4,a:2,n:2000" --battletest saves/battle_re/forest_pre.raw.wram` -> think f510 / pc-act f510 / act-end f651 / arrive f693. `CT_BATTLESHOT_AT=600` shows the left Hetake in the windup while the mushroom pose is back at f505/f700. |
| track 15 `CD/96B2` bytes | ROM: `[24 ROM bytes at 0xD96B2: python tools/dis.py 0xD96B2 24]` — byte-exact as quoted. Header `CD/72B9 = 90 01 90 01 / C9 72 / F4 72 / B2 96` ok. **And I checked the other two tracks: `CD/72C9` (t0) and `CD/72F4` (t3) contain NO `A8`/`A9`** — so track 15 really is the only source of our `animKnockDist`. |
| the A8/A9 handlers split combatant vs effect object at index 8 | `dis.py 169C3` -> `LDA $A010 / CMP #08 / BCC ->169D3 / JMP C1/6A99`; `dis.py 16AF0` -> `... / JMP C1/6C54`. `C1/6A99` works on `$A2B6`/`$A5B5`/`$A230` — it never touches `$96DB`/`$1D0C`. The mechanism is real. |
| the victim drift | our `asc t15 FWD from=` line, 8 actions: `(160,70) -> (163,70) -> (163,66) -> (166,66) -> (169,66) -> (169,62) -> (172,62) -> (172,58)`. **Worse than the dossier states: it is UNBOUNDED and monotone, not "a few px".** |
| O4 (`--battletest .../battle_crono_solo.raw.wram` fails staging) | reproduced verbatim. Note the harness prints `$2980..2=008080 want 000180 (party: Crono+Marle)` — the *expected* party is the 2-PC one, so the failure looks like a fixture-expectation mismatch against a solo capture, not a bad WRAM. Still open. |

### B. REFUTED

**B1 — "the drawn cell is `$967B`, written only by `C1/4304`". FALSE.** There are **eight** `STA $967B`
(`8D 7B 96`) sites in bank C1: `C1/2EEF`, `C1/3428`, `C1/34BD`, `C1/36FF`, `C1/4167`, `C1/4188`, `C1/41A9`,
`C1/4304` (ROM byte scan; `C1/4167-41A9` are the three party-indexed publishes off `$2980/$2981/$2982`,
`C1/34BD` the `$96F5`/`$A4DC` staging loop, `C1/36FF` the `$5DC5` loop). The dossier's own §6 already lists
three. The conclusion survives only because I *measured* that none of them fired between f402 and f438 —
not because `C1/4304` is unique. State it that way.

**B2 — "`$A124`'s SOLE writer is the playAnim opcode tail `C1/4D61`". FALSE.** Two `STA $A124,Y`
(`99 24 A1`): `C1/4D61` **and `C1/4DC5`** (a second opcode body, `JSR C1/0A0A / ... / LDA $8A / TAY / LDA $8E
/ STA $A124,Y`). Plus `STZ $A124,X` at `C1/42C1` and `C1/42DF`, and a bulk `LDX #0083 / STZ $A124,X` clear at
`CC/F07D-F084`. Again the *measurement* (all-zero f0..f436) carries the argument; the "sole writer" premise
does not.

**B3 — the framebuffer proof. REFUTED AS STATED, and it does not measure what it claims.**
The dossier reports "byte-identical at f398 / f403 / f404 and first differs at f437 (782 bytes)". I peeked
f398/f403/f404/f405/f406/f407/f408/f410/f412/f414/f416/f418/f420/f436/f437/f440/f442/f445/f450/f455/f460/f465
and diffed the same box:

```
vs f398:  f403 0    f404 0    f405 0    f406 0    f407 0    f408 0
          f410 777  f412 782  f414 782  f416 782  f418 782  f420 782  f436 782  f437 782  f440 782
vs f436:  f437 0    f440 0    f442 2687 f445 2178 f450 1546 f455 1651 f460 1741 f465 1557
```

Two independent failures:

1. **The 782-byte delta appears at f409/f410 — 27 frames INSIDE the window the dossier calls frozen.** The
   dossier sampled three frames (f398/f403/f404), skipped to f437, found 782, and read it as "the sprite
   first changes at f437". It had already changed at f410.
2. **782 is not the pose.** The box is **0 bytes different between f436 and f440**, straight across the real
   `$967B=03` publish at f438; the sprite's pixels first move at **f441/f442 (2687 bytes)**, when the slide
   starts. Whatever the 782 bytes are, they are not the pose engine.

What they *are*: the splice's garbled BG. A full-screen diff mask (f398 vs f412) is a block of vertical
stripes over x~8-150 / y~8-140 — the known "a splice keeps the BASE state's VRAM" artefact — whose right
edge clips the box, and it redraws to black+circle tiles around f409.

**I re-established the render fact properly, and it holds:** a 4x crop of (145,92)-(195,140) at
f398 / f404 / f410 / f436 shows the acting Hetake's mushroom cells **pixel-identical**, only the black/striped
garbage to its left changing; and the OAM rows for that combatant (`idx 4-7`, X 168/152/169/153, Y 99/99/115/115,
tiles `060/062/064/066`, attr 22) are **byte-identical at f398 and f440**. So: *the ROM does not change the
acting enemy's cells between the turn-start and the first playAnim* — on new evidence, not the dossier's.

Repro for the corrected measurement:
```
for k in 398 404 410 436 440 442; do
  <probe> --peek saves/battle_crono_solo_lake.state f$k.raw --btnscript "40:$k"; done
python boxdiff.py f398.raw 150 95 190 135 f404.raw f410.raw f436.raw f440.raw f442.raw
python crop.py "f398.raw,f404.raw,f410.raw,f436.raw" 145 92 195 140 4 cmp.png   # then LOOK at it
python tools/ct.py oam f398.raw.oam ; python tools/ct.py oam f440.raw.oam
```
(`boxdiff.py` / `crop.py` / `diffmask.py` = 20-line helpers over `tools/ctlib/fb.py::load`, which decodes the
512x480 [pad,B,G,R] peek to 256x240 RGB. Scratch only; not added to the tree.)

**B4 — "ROM `C1/762E` handler split `LDA $A010 / CMP #08 / BCS`". MIS-CITED.** `dis.py 17620 0x40 1 0`:
`C1/762E: PHY / PHX / TAY / ASL A / TAX / JSR ($79D3,X) / PLX / PLY / RTS` — a jump-table dispatcher with no
`CMP` in it. The `#08` split lives in the *individual* handlers (I found ~40 `LDA $A010 / CMP #08` sites in
bank C1, including `C1/69C9` and `C1/6AF6` = exactly the A8/A9 handlers), and the branch is **`BCC +3 / JMP`,
not `BCS`**. Substance intact, citation wrong. Additionally the handlers carry a **third** path the proposed
`< 8` gate would flatten: `CMP #04 / BEQ -> C1/6A2A` (A8) and `-> C1/6B9D` (A9).

**B5 — "the think length is right ... DO NOT clamp `battleThinkTick`". NOT DERIVED — and the 183-frame case
has no ROM counterpart at all.** `--monitor "7EAE91:1,7EA013:1" --diff --writer --frames 2200`:

```
f402 AE91=05*(C1:ACFC)  A013=00   f405 A013=01*(C1:40A9)   f595 A013=00*(C1:41BD)
f603 AE91=04            A013=00   f606 A013=01             f808 A013=00
f812 AE91=03            A013=00   f815 A013=01             f992 A013=00
f1280 AE91=05 / f1283 / f1473 ;  f1482 AE91=04 / f1485 / f1675 ;  f1952 AE91=05 / f1955 / f2145
```

**Six turn-starts for six: every `$AE91` write happens with `$A013 == 0`, and the window opens exactly 3
frames later.** The ROM never holds a pending dispatch across another combatant's window — the turn simply
does not start. Our engine instead *starts* the turn (`battleOpenAction` -> `mvPendAtk` + `enThinkDelay`,
battle.c:4384-4386) and then holds it (`battleThinkTick`, battle.c:5235 `if (h->actActive) continue;`),
entering a state with no observed ROM analogue and lasting the measured 183 frames. So the dossier's
justification — "the oracle's own turn-start->dispatch gap is the same 3 frames" — is true of the ONLY gap the
ROM ever shows and says nothing about the held case. **The "sometimes" (the 3-second version) is a symptom of a
SECOND divergence that the §4.3 fix hides rather than removes.** I did not derive the ROM gate itself:
`C1/ACF2` is a leaf (`TDC / STA $AE90 / LDA $B18B / STA $AE91 / ... / RTS` at `C1/AD08`), so the `$A013` test is
in an uncaptured caller. Recorded as O7, not claimed.

**B6 — "which is why only the Hetakes show it" is true of `mvPendAtk` but understates the class.**
`grep -n mvPendAtk src/*.c` -> the only render read is field.c:3825/3826/3889, confirmed. But
`battleHetakeRender` is itself entered on `mem[b + 0] != 0x5E` (src/field.c:3800) — an **enemy-ID gate on a
whole render path** — and the same if-chain still carries the rel ladder at field.c:3891-3898 (`rel < 3 / < 8
/ < 29 / < settleRel`, thresholds read off a capture) as the `!animParsed` fallback. Deleting one arm leaves
the class standing.

### C. Is the fix a forbidden pattern? (question 3)

The change itself is a **deletion of invention**, so it is not a new gate/heuristic/eyeballed constant. Both
variants are pixel-equivalent: `CtBattleEnemyAnimWin` (src/battle.c:490) is a pure ROM read with no side
effect, and the `pose < 0` path (field.c:3897-3903) blits at `hs->anchorX + hdx + cc->dx`, character for
character the expression the `else` idle branch uses at field.c:3914-3916, with the same `palw`.

But it is **partial**. The law this dossier derived (`C1/4250`: publish only while `$A124,X != 0` **and**
`$A12F,X == 0`) says the renderer must take its cell from the pose engine *alone*. The mandate-complete shape
of that chain is `if (livePose >= 0) pose = livePose; else pose = -1;` for every enemy — the dossier's
preferred variant (delete 3825, `if (acting) {`, delete 3889) is a strict subset that leaves 3891-3898.
Fixing the arm without saying so risks the exact "spot-fix (pontual)" the mandate forbids. Recommend landing
the deletion **and** recording the ladder's removal as the next step, with `!animParsed` decided explicitly
(what does the ROM draw for an enemy whose script we cannot parse? — untested).

### D. Which pinned checks move? (question 5 — read `tools/regress_checks.json`, suite NOT run)

I traced the event timeline of every pinned forest/Hetake scenario with the snapshot `Belthasar.exe`:

| scenario | thinks | pinned moment | verdict |
|---|---|---|---|
| `field-battle-hetake-attack-pose` (`n:900`, shot `enemy-arrive#1+75`) | f290-293, f510-513, f999-1002 ... | f368 | **no move** |
| `field-battle-hetake-return-pose` (shot `enemy-arrive#1+110`) | same | f403 | **no move** |
| `field-battleloop-forest`, `field-battle-forest-lastkill-dissolve`, `field-battle-pc-approach-early/late`, `field-battle-enemy-death-sfx`, `field-battle-results-reward`, `field-battle-drop-roll`, `field-battle-levelup-growth`, `field-battle-tech-learn` (all `CT_BATTLE_MASHA=1`) | **none** — the PCs kill all three before any enemy turn (`pc-act f195/f435/f760`, `death f336/f581/f893`) | — | **no move** |
| `field-battle-crit-leap-mid` (AT=223) / `field-battle-crit-beatB-dead-suppressed` (AT=289) | first think f520 | f223 / f289 | **no move** |
| `field-battle-tech-cyclone-*`, `field-battle-tgtcursor-cyclone-multi` (AT=234), `field-battle-dissolve-serial` | first think f977 | <= f563 | **no move** |
| `field-battle-ambush-generic-dissolve` (AT=362) | Beetles/Avian — `mem[b+0] != 0x5E`, `battleHetakeRender` never runs | — | **no move** |

**=> The PRIMARY fix moves NOTHING in the suite. The phantom is completely UNGRADED today** — in every pinned
scenario the think is exactly 3 frames and no shot lands inside one. Per memory `grade-invariant-when-no-behavior`,
the fix is therefore **obliged to ship with a new pinned check**, on a scenario that actually holds the think
(e.g. `CT_BATTLE_SCRIPT="n:283,a:2,n:4,a:2,n:2000"` + `CT_BATTLESHOT_AT=600` on `forest_pre.raw.wram`, whose
box delta is 2281 B today and must become 0 after). The dossier does not mention this; a fix that moves no
signature and pins nothing is a mutation waiting to survive.

**The SECONDARY fix (`animKnockDist`) DOES move checks.** `field-battle-hetake-pose-ladder` pins exact
`ascposearm`/`ascpose` frames f1034-f1097 for cmb3 under `^ascpose(arm)? f(102[0-9]|10[3-9][0-9]|1(1[0-1][0-9]|12[0-9]))`,
and the approach length depends on the victim's drifted foot (my run: 3rd action `SLIDE (120,63)->(151,66) n=15`
where the oracle walks 14 steps to `(148,70)`). `field-battle-hurt-flash-script-bound` runs the same `n:500`
script through `CT_BATTLE_ASCTRACE` and is exposed the same way. `field-battle-hetake-attack-sfx` pins
`rel=51` frame-agnostically and should hold.

### E. Simpler / different cause? (question 4)

I could not find a simpler cause for the *pixels*: the phantom cell is `wApp[0]` = ROM anim-5 entry 0 = `0x03`,
drawn by exactly one line, and removing that line removes it. But two other things in the report are being
attributed to it that are not the same defect:

- **the DURATION is a different bug** (B5): the ROM never opens a dispatch it then has to hold, so `battleThinkTick`'s
  hold is itself uncanonical. After the render fix the enemy will still *stand idle doing nothing* for 183 frames
  where the ROM would not have started its turn at all — visually quiet, mechanically still wrong.
- **the "movement" half of the user's phrase** may partly be O2 (our Hetake idle is a frozen baked pose while the
  ROM's idle stepper publishes `00/01/02/07` through `C1:342B` all fight — visible in my `$967B` monitor as the
  `33 / 01 / C8 / C9` cycle every 5-15 f up to f401, resuming at f600). A static sprite that suddenly assumes one
  attack cell reads as "a movement"; a breathing sprite that does the same reads as a glitch frame. Both bugs feed
  the same report.

### F. Open questions this review adds

- **O6** — `$967B` has eight writers and `$A124` two (plus three clear sites incl. `CC/F07D`). The "no publish in
  the think" fact is MEASURED, not structurally proven; nothing here shows those other sites *cannot* fire in some
  other fight (a PC-side turn-start, a multi-enemy formation).
- **O7** — where does the ROM gate the AI turn-start on `$A013 == 0`? `C1/ACF2` is a leaf; the caller was not
  captured. Until it is, "our think length is right" is an observation about one 3-frame case, and the held-think
  divergence (B5) is underived.
- **O8** — the publish gate is `$A124,X != 0` **AND** `$A12F,X == 0` (`C1/4250`, `14255/14258`). Does our port
  carry the dwell half? Not checked here.
- **O9** — the `A8`/`A9` handlers have a `CMP #04` path (`C1/6A2A` / `C1/6B9D`) distinct from both the `< 4`
  combatant path and the `>= 8` effect-object path. A bare `track < 8` gate at src/battle.c:3672 flattens it. What
  role is index 4, and does any script we run put an `A8`/`A9` there?
- **O10** — the pose-03 publish at f438 does not reach the framebuffer until f441/f442 (box delta 0 at f440, 2687
  at f442) while the OAM rows are unchanged. Is that the every-other-frame `$A012` publish plus the cell DMA queue,
  and does our engine carry the same latency? Unmeasured.
- **O11** — the phantom is ungraded by the whole suite (§D). Any fix must land with a new check or it is unmutated.
