# loc 112 pre-battle freeze — mid-AB seed hang blocks op-0xD8

## Symptom (user, 2026-07-18 / clarified 2026-07-19)

`Belthasar.exe` live (not savestate-load specific): after the pre-battle approach
animation starts, enemy sprites vanish and the game freezes with **no battle**.
Savestate `jogo_trava.state` is only a **reference** for the start of that animation.

## Oracle timeline (spliced lake state `saves/jogo_trava_lake.state`)

- loc=$070 (112), storyline=$0C, control locked ($011F=0), player path $0120=1.
- **f133**: `$0117|=0x21` (writer C0:3AA8 = op-0xD8), `$2989=0x12`, `$2A1F=0x00`.
- **f134**: bit5 consumed (`$0117=0x01`).

### Script gate (obj10, scene 112 / script 333)

obj10 parks on `04 18 33` (CALL obj12 fn3, wait flag $7F0980=1), then:

```
12 06 04 03 03 11 06   ; wait while loc[6] < 4  (cmp mode 3 = <)
12 07 01 03 03 11 06   ; wait while loc[7] < 1
… face/hide …
D8 12 00               ; BATTLE at raw $7F2469
```

`loc[6]` / `loc[7]` live at `$7F020C` / `$7F020E`. Oracle: loc[7]=1 @f53 (C0:6C81 INC
path, op 0x71); loc[6] climbs 1→2→4 by f124 via enemy completions (`5B 02 06` =
loc[6]+=2 at end of approach scripts, and/or `71 06`).

### First divergence (pre-fix)

`--vmdiff jogo_trava_lake.state --frames 200`:

```
FIRST divergence at frame 25: obj11 ip ours=049A(op AB) oracle=049C(op AB)
```

obj11 is mid **op 0xAB anime-once** (anim 7):

| RAM | seed value | meaning (ROM C0/2E9A + C0/CA4B) |
|---|---|---|
| `$1780,X` | 2 | once-play mode |
| `$1781,X` | 7 | anim id |
| `$7F0B01,X` | 2 | playing (→1 when step ticker finishes, next AB visit completes) |
| `$1681,X` | 2 | current step index |
| `$1180,X` | 0x049A | IP parked on AB |

Without a WRAM seed of that state, the first AB visit **re-armed** (`animOpState==0`),
and without a renderer completion signal (`CtSceneAnimFinished` / `VMDIFF_ANIM`) the
op **never advanced**. Enemies never reached `5B 02 06` → `loc[6]` never hit 4 →
obj10 spun the IF poll forever → **D8 never ran** → battle never started.

## Root cause (live field — the user-visible freeze)

Storyline `$0C` takes the **early** branch of obj11/12 (IF storyline < `$21`):

```
0C 1A / 0C 1E     ; unpause obj13/15  (ROM C0/6282 AND $1000 #$7F)
8B 28 00          ; park self at (40,0)  ← approach actors leave the screen
02 1A 13 / 02 1E 13  ; REQ obj13/15 fn3  (spawn real fight enemies + 71 06)
```

Real battle enemies (obj13–16) run fn3: SETCOORD on-screen → MOVETO → **`71 06` (loc[6]++)**
→ obj10 waits `loc[6] >= 4` then **`D8 12 00`**.

### Bug 1 — invented map collision (primary hang)

- ROM does **not** init `$1C80` on op-0x83; oracle enemies have `$1C80=0`.
- Our default was `collMap=1` ("leftover 0x03") → field enemies **map-bounced**.
- **obj14** `96 0D 03` never arrived → only 3 of 4 `71 06` ran → `loc[6]=3` forever
  → obj10 IF-poll forever → **D8 never runs**.
- Meanwhile approach actors already HID/parked off-screen → **enemies vanish, freeze**.

### Bug 2 — ops 0x0A/0x0B/0x0C wrong

| op | ROM | was (wrong) | now |
|----|-----|-------------|-----|
| 0x0A | `$1100:=80`, `$1A81:=0` deactivate | `dead=1` only | mode 0x80 + dead + clear 1A81 |
| 0x0B | `ORA $1000 #$80` pause cadence | `dead=1` | `cadPause=1` |
| 0x0C | `AND $1000 #$7F` unpause | `dead=0` | `cadPause=0` |

REQ (C0/5FD0) refuses paused targets — model `cadPause` + skip cadence while paused.

### Bug 3 — mid-AB WRAM seed (secondary, loadwram/vmdiff)

`CtSceneSeedFromWram` restores mid-0xAB (`$1780/$1781/$7F0B01/$1681`) so once-anims
don't re-arm forever when resuming a mid-cutscene state.

## Fix summary

1. Default `coll`/`collMap` = **0** (zeroed WRAM); seed from `$1C80` on loadwram.
2. Correct 0x0A / 0x0B / 0x0C + REQ/cadence pause gate.
3. Mid-AB seed restore (prior commit).

## Validation

```
Belthasar --assets ct_assets.dat --scene 112 --loadwram saves/jogo_trava_loadwram.state \
  --selftest --frames 200
# obj10 reaches ip=0468 (D8) ~f125; $2989=0x12 staged; enemy id 0x0C slots filled
```

Oracle: loc[6] hits 4 by f124; D8 at f133.
