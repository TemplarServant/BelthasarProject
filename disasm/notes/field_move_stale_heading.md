# Guardia Forest (loc 19) — "the battle never starts": a STALE op-0x96 heading

**Verdict: REPRODUCED, root-caused, ROM-derived. Our op-0x96 (MOVETO) caches a per-object
heading (`sc->moveAngle[]`) that SURVIVES a CALL/REQ preemption, so the first move op of the
preempting function inherits the previous move's direction. In Guardia Forest that sends the
cutscene actor obj25 off the map forever, its `CALL+WAIT` never completes, and `D8` (BATTLE)
is never reached — the field freezes with `$011F = 0`.**

The ROM has NO stored heading: `C0/4F61` recomputes it with `JSR C0/ABA2` on every re-issue
visit (4FAB, unconditional on the not-arrived path).

---

## 1. What the user's save actually captures

`saves/batalha_nao_comeca_lake.state` (loc 19, storyline 03, `$011F = 00`, solo Crono
`$2980 = 00/80/80`) is **not** a broken state — it is the ROM sitting, legitimately, inside the
Guardia-Forest chase cutscene at one specific instruction pair:

| RAM | value | script body offset (base `0x0540`) | meaning |
|---|---|---|---|
| `$11B0` (obj24 IP) | `0x08BA` | **`0x037A`** | `04 32 34` = CALL obj25 **fn4**, and WAIT |
| `$11B2` (obj25 IP) | `0x08FB` | **`0x03BB`** | `96 09 0E` = MOVETO tile(9,14) |

So the moment the user names is: **obj24 is blocked on a CALL+WAIT whose callee is executing a
MOVETO**, three ops before `D8 00 00`. That is exactly the loc-112 pattern
(`battle_entry_loc112_freeze.md`) — a pre-battle MOVETO that must arrive or the `D8` never runs.

Script→RAM base for scene 19: `body_offset + 0x0540 = IP` (derived by matching obj1's
`0x0096/0099/009C/009F/00A2/00A9` chain and obj21's `0x030D/0311/0313/0319` chain).

---

## 2. The trigger chain in scene 19 (script 113, 42 objects, body 1258 B)

`python tools/ct.py script 19 --obj N` stops at the first `00 RETURN`; the bodies below were
walked linearly with `assets/script.py`'s `op_len` over the whole body.

**Four independent ambushes; three are polled by obj1, one is a direct touch object.**

```
obj1  fn0 = 0x0060 `00 RETURN`; autorun continuation 0x0061 = the POLL LOOP
  0x0061  12 0A 01 00 28   IF loc[10]==1 else->0x008D
  0x0066  E3 00            lock control
  0x006A  04 24 33 / 04 26 33 / 04 28 33   CALL obj18/19/20 fn3 (+WAIT)
  0x0073  E7 20 22         camera seek        0x0076  D9 26 A8 23 A8 28 A7
  0x007D  D8 00 00         <== BATTLE         0x0080  77 0A  loc[10]=0
  0x008D  … same for loc[11] -> obj21/22/23, D8 02 00 at 0x00A9
  0x00B9  … same for loc[13] -> obj30/31/32, D8 02 00 at 0x00D5
  0x00E9  11 89            JUMPBACK -> 0x0061
```
`loc[N]` lives at `$7F0200 + 2N` (op 0x12's handler C0/62DE does `ASL A`), so
loc[10]=`$7F0214`, loc[11]=`$7F0216`, loc[12]=`$7F0218`, loc[13]=`$7F021A`.
The flags are set by the monsters' **touch** slot (fn2): obj18/19/20 `75 0A`, obj21/22/23
`75 0B`, obj30/31/32 `75 0D` (sprite 350, enemy id `0x5E` = Hetake, matching `$AF02 = 5E 5E 5E FF`).

**The fourth — the one the user hit — is obj24, an invisible trigger object at tile(20,19):**

```
obj24 fn0 = 0x0352  8B 14 13  SETCOORD tile(20,19)  (no sprite; touchable=1)
obj24 fn1/fn2..fn15 = 0x0357
  0x0357  12 0C 01 00 04   IF loc[12]==1 -> RETURN (already done)
  0x035F  E3 00            lock control            <-- $011F = 0
  0x0361  E7 0D 0B         camera seek (blocking)
  0x0364  04 32 33         CALL obj25 fn3 + WAIT
  0x0367  02 38 33         REQ  obj28 fn3
  0x036A  04 3A 33         CALL obj29 fn3 + WAIT
  0x036D  AD 05            WAIT
  0x036F  02 34 33         REQ  obj26 fn3
  0x0372  04 36 33         CALL obj27 fn3 + WAIT
  0x0375  16 00 08 03 04   IF storyline < 8 (glb[0]) else -> 0x037D
  0x037A  04 32 34         CALL obj25 fn4 + WAIT       <-- the user's frozen IP
  0x037D  D9 93 96 99 14 10 10
  0x0384  D8 00 00         <== BATTLE
  0x0387  75 0C            loc[12]=1
```

obj25 (`83 19 08` = sprite 281, the man being chased) has an autorun PATROL loop after its fn0:

```
obj25 fn0 = 0x038A … 0x0398 RETURN
  0x0399  96 16 11   MOVETO tile(22,17)     <-- RIGHT/UP leg
  0x039C  96 15 13   MOVETO tile(21,19)     <-- DOWN leg
  0x039F  96 12 11   MOVETO tile(18,17)     <-- LEFT/UP leg
  0x03A2  11 0A      JUMPBACK -> 0x0399
obj25 fn3 = 0x03A5  96 16 11 / AD 02 / AA 06 / E8 9B / AD 05 / E8 9B / AD 05 / E8 9B / 00
obj25 fn4 = 0x03B7  AA 01 / 89 50 / 96 09 0E (MOVETO tile(9,14)) / 91 HIDE / 00
```

Both `04 32 33` (fn3) and `04 32 34` (fn4) **preempt obj25 mid-patrol-MOVETO**. That is the
whole story.

---

## 3. ROM: op 0x96 has NO stored heading

`python tools/dis.py 4F61 90` — handler C0/4F61 (op-0x96 MOVETO):

```
4F61: LDX $6D
4F63: LDA $1A01,X        ; the movement TICKET
4F66: BEQ ->4F6B         ; ticket still running -> yield without touching anything
4F68: TYX / CLC / RTS
4F6B..4F78:  target tile  <- $7F2001,X (the two script operands) -> $F2/$F3
4F7C: LDA $1801,X -> $F0 ; CURRENT tile (HIGH byte of the 1/16px position)
4F81: LDA $1881,X -> $F1
4F86: CMP $F3 / BNE ->4FAB     ; not arrived on Y -> re-issue
4F8E: CMP $F2 / BNE ->4FAB     ; not arrived on X -> re-issue
   (arrived: 4F90 $1C81 bit0 snap C0/305D, else 4F9C C0/568B + INX INX INX + SEC = COMPLETE)
4FAB: JSR C0/ABA2        ; <== HEADING RECOMPUTED FROM SCRATCH, EVERY re-issue visit
4FB0: LDA $EE / TAX
4FB5: LDA $F700,X / STA $1600,X        ; facing from the fresh heading
4FBD: JSR C0/5B95 / (4FC2 +0x30 object-solid sidestep)
4FD2: LDA $1000,X / STA $1A01,X / STA $7F0B00,X   ; arm the ticket
4FE0: JSR C0/ACFD        ; issue the velocity from $EE
4FE3: JSR C0/5614 / 4FE8: CLC / RTS
```

`C0/ABA2` reads only `$F0/$F1` (current tile) and `$F2/$F3` (target tile) and writes the scratch
`$EE`. **There is no per-object heading storage anywhere in this path.** The velocity looks
"held" across a diagonal only because 4F63 refuses to re-issue while the ticket `$1A01,X` is
non-zero — a gate our port already models (`src/ct_vm.c:1676`, head of `moveOp`: `if (sc->cycles[obj] > 0) return 1;`).

---

## 4. Oracle: the ROM re-aims the instant the new op starts

`ct_probe --teleport 19 20 22 0 --from saves/solo_forest.state --out $TMP/fr_2022.state`
(lead placed at px(328,367), i.e. one tile below obj24), then walk UP.

Phase chosen so obj24's `04 32 33` preempts obj25 **mid DOWN-leg** (`0x039C`) — the same phase
that hangs our engine:

```
ct_probe --monitor "1832:2,18B2:2,11B2:2" --from $TMP/fr_2022.state \
         --input "0:70,10:60,0:600" --diff --frames 200
```

| frame | obj25 IP | obj25 px | motion |
|---|---|---|---|
| f120-f126 | `08DC` = body `0x039C` | 287.88 → 290.25 (y) | patrol leg, moving **DOWN** |
| **f127** | `08E5` = body `0x03A5` (fn3 `96 16 11`) | 290.25 → **289.94** | **first step REVERSES to UP/RIGHT** |
| f134 | `08E5` | (352.44, 287.25) = tile(22,17) | arrived |
| f135 | `08E8` = `0x03A8` | — | fn3 continues |

The ROM aims at the NEW target on the very first step of the new op. Same story for fn4
(`96 09 0E`, target tile(9,14)): the oracle walks one straight diagonal, x −2.31 px/f and
y −0.63 px/f **simultaneously**, from (311,287) at f289 to (155,238) at f364, arriving f365.

Full oracle timeline of the obj24 cutscene from `$TMP/fr_2022.state` with `--input "10:60,0:500"`:

| f | obj24 IP → body | op |
|---|---|---|
| 28 | `08A1` → `0x0361` | `E7 0D 0B` camera seek |
| 57 | `08A4` → `0x0364` | CALL obj25 fn3 |
| 153 | `08AA` → `0x036A` | CALL obj29 fn3 |
| 245 | `08AD` → `0x036D` | `AD 05` |
| 265 | `08B2` → `0x0372` | CALL obj27 fn3 |
| **289** | `08BA` → **`0x037A`** | CALL obj25 fn4 — **the user's captured moment** |
| 365 | (obj25 `08FF` = `0x03BF`) | obj25's MOVETO **arrives** (76 frames) |
| 369 | `08BD` → `0x037D` | `D9 …` |
| **381** | `08C4` → **`0x0384`** | **`D8 00 00` fires** |
| 457 | `0000` | battle module loaded |

Idle delays 60 / 90 / 120 before walking up all reach `0x0384` (f381 / f435 / f481).
**The oracle never hangs at any phase tried.**

---

## 5. Our engine: the hang, on the ordinary walk-in path

Seed (a straight WRAM dump of the teleported oracle state, wrapped in the `--loadwram` header):

```
ct_probe --teleport 19 20 22 0 --from saves/solo_forest.state --out $TMP/fr_2022.state
python tools/ct.py wram $TMP/fr_2022.state $TMP/fr_2022.raw.wram
python -c "raw=open(r'$TMP/fr_2022.raw.wram','rb').read(); \
 open(r'$TMP/fr_2022.dumpwram','wb').write(b'ctwram v1\nRAM:131072:'+raw)"
```

Sweep of the approach delay — **only the phase changes, nothing else**:

```
CT_VMTRACE=24 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
 Belthasar --assets ct_assets.dat --scene 19 --loadwram $TMP/fr_2022.dumpwram \
          --selftest o.raw --frames 900 --walkscript ".:<D>,u:60,.:900"
```

| `<D>` | obj24 addr at f900 | result |
|---|---|---|
| 0 / 20 / 40 / 60 | `0356` (returned) | battle ran, cutscene finished |
| **80 / 100** | **`0367`** | **HUNG** — blocked on `0x0364` = CALL obj25 **fn3** |
| **120** | **`037D`** | **HUNG** — blocked on `0x037A` = CALL obj25 **fn4** ⇒ *literally the `batalha_nao_comeca` IP pair* |

`.:120` in detail (`CT_VMTRACE=25` + `CT_CATTRACE=25`, 2500 frames):

```
t435  obj25 addr=0399   (patrol leg to tile(22,17) = RIGHTWARD; heading cached as pure RIGHT)
t458  obj25 addr=03B7   fn4 starts (AA 01)
t459  obj25 addr=03BB   96 09 0E  MOVETO tile(9,14)  -- target is UP-LEFT
…
f2499 obj25(4900,287) t(306,17) onSolid=1 face=3(R)
```

obj25 walks **RIGHT**, y frozen at 287, forever — 306 tiles off the right edge of the map.
`D8` is never reached, `$011F` stays 0, the player can never move again.

The `CT_TOUCHAT="24,85"` variant hangs the same way on the *earlier* CALL (`0x0364`), with the
stale heading = pure DOWN (obj25 walks to tile y=95 and keeps going).

Even when it does **not** hang, our path is wrong. Same seed, `--walkscript "u:60,.:340"`:

| | oracle | ours |
|---|---|---|
| obj25 fn4 `96 09 0E` from (≈311,287) to tile(9,14) | one straight diagonal, 76 f | **L-shaped**: 67 f pure-X (y pinned at 287) then 22 f pure-Y, 92 f total |

---

## 6. Root cause — `src/ct_vm.c`

### The defect

`src/ct_vm.c:1858-1860` (inside `moveOp`):

```c
  int _ang;
  if (op == 0x96 && sc->moveAngle[obj] >= 0 && dxr != 0 && dyr != 0)
    _ang = sc->moveAngle[obj];                          // hold the heading (no per-tile re-aim)
  else { _ang = mvAngle(dtx - atx, dty - aty); sc->moveAngle[obj] = _ang; }   // fresh / axis landed -> (re)aim
```

`sc->moveAngle[64]` (declared `src/ct_vm.c:1023`) is only ever (re)filled in two places:

* `src/ct_vm.c:2907` — scene create, `= -1`;
* `src/ct_vm.c:1838` — inside the "fresh move" block whose gate is
  `src/ct_vm.c:1830`: `if ((op == 0x96 || op == 0x97) && !sc->hasTask[obj]) {`.

`hasTask[obj]` is a **mover-state** flag ("a scripted step is in flight"), not a
move-op-identity flag. `reqOp` (`src/ct_vm.c:1915`, the CALL/REQ implementation) rewrites the target's
`ts->addr`/`ts->prio` and **deliberately touches nothing else** — no `hasTask`, no `cycles`, no
`moveAngle`. Only the op-0x00 RETURN path (`src/ct_vm.c:2054/2058`) clears `hasTask`.

So when a CALL preempts an object that is *mid* MOVETO:

1. `hasTask[obj]` is still 1 → the fresh-move block at 1830 is **skipped**;
2. `moveAngle[obj]` therefore still holds the **previous** MOVETO's heading;
3. line 1858 sees `dxr != 0 && dyr != 0` for the *new* target and reuses that stale heading;
4. the heading is only re-aimed when an axis lands (`dxr == 0 || dyr == 0`) — and with a stale
   pure-axis heading pointing away from the new target, **neither axis ever lands**.

### Why it is invention, not a port

The comment at 1023 justifies the cache as "the ROM … keeps the velocity CONSTANT until an axis
reaches its target TILE … it does NOT recompute the angle from the integer tile delta every
frame". C0/4F61 says otherwise: **4FAB `JSR C0/ABA2` is unconditional** on the not-arrived path,
and `$EE` is scratch. What actually keeps the oracle's velocity constant is the ticket gate at
4F63 (`LDA $1A01,X / BEQ`), which we already model at the head of `moveOp`
(`src/ct_vm.c:1676`). The cache is a second, un-derived copy of that
mechanism — and unlike the ticket it is never invalidated, so it leaks across script functions.

### Minimal mandate-safe change (DO NOT apply — reported only)

**Preferred (ROM-exact, deletes the invention):** remove the hold at `src/ct_vm.c:1858-1859` so
op 0x96 always re-aims:

```c
  int _ang = mvAngle(dtx - atx, dty - aty);   /* C0/4FAB: JSR C0/ABA2, every re-issue visit */
```
…and drop the now-dead `moveAngle[]` field (1023 / 1838 / 2907). Re-issue frequency is then
governed by `cycles`/`$1A01` exactly as 4F63 governs it in the ROM.

If that turns the Crono-candy diagonal red, the fault is in the **ticket/cadence** model
(`cycles` reaching 0 more often than `$1A01` does), not in a missing heading cache — fix it
there, per the mandate; do **not** restore the cache.

**Interim, if the cache must stay while that is investigated:** the cache must not outlive the
op that filled it. The smallest correct form is to key it to the owning script address — add
`int moveAngleAddr[64];`, store `addr` beside the angle at 1860, and require
`sc->moveAngleAddr[obj] == addr` at 1858. This is still a stand-in and should be labelled as one.

Note the sibling leak: the facing set at `src/ct_vm.c:1836` is behind the same stale
`!hasTask` gate, so a preempted actor also keeps the *old* facing on the new move's first step
(visible as `face=3(R)` while walking to an up-left target).

---

## 7. Exact repro command lines

```sh
# --- ORACLE (never hangs) -------------------------------------------------
cp build/ct.exe "$TMP/ct_probe.exe"
"$TMP/ct_probe.exe" --teleport 19 20 22 0 --from saves/solo_forest.state --out "$TMP/fr_2022.state"
"$TMP/ct_probe.exe" --monitor "11B0:2,11B2:2" --from "$TMP/fr_2022.state" \
        --input "10:60,0:500" --diff --frames 560          # obj24 reaches 0x08C4 (D8) at f381
"$TMP/ct_probe.exe" --monitor "1832:2,18B2:2,11B2:2" --from "$TMP/fr_2022.state" \
        --input "0:70,10:60,0:600" --diff --frames 200      # the f127 re-aim, in one table

# --- OURS (hangs) ---------------------------------------------------------
python tools/ct.py wram "$TMP/fr_2022.state" "$TMP/fr_2022.raw.wram"
python -c "raw=open(r'$TMP/fr_2022.raw.wram','rb').read();open(r'$TMP/fr_2022.dumpwram','wb').write(b'ctwram v1\nRAM:131072:'+raw)"

CT_VMTRACE=25 CT_CATTRACE=25 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
 build/Belthasar.exe --assets ct_assets.dat --scene 19 \
   --loadwram "$TMP/fr_2022.dumpwram" --selftest o.raw --frames 2500 \
   --walkscript ".:120,u:60,.:2500"
# -> obj25 addr sticks at 03BB, x climbs to 4900 (tile 306), obj24 addr sticks at 037D, no battle.

# same hang one CALL earlier:
CT_TOUCHAT="24,85" CT_VMTRACE=24 … --frames 800           # obj24 addr sticks at 0367

# the user's own seeds still WORK, because the WRAM restores the mover mid-move:
build/Belthasar.exe --assets ct_assets.dat --scene 19 \
  --loadwram saves/forest_re/batalha_nao_comeca.dumpwram --selftest o.raw --frames 900
```

---

## 8. Suggested regression check (for `add-regress-check`, after the fix)

Event-anchored, not frame-anchored: from `$TMP/fr_2022.dumpwram` with
`--walkscript ".:120,u:60,.:1200"`, assert obj24's script address **reaches body `0x0384`**
(the `D8`) and obj25's |x| never exceeds the map width. Both the `.:80` and `.:120` phases must
be pinned — `.:0` passes today and would grade nothing.

---

## Open questions

* **Why does the cache exist?** The 1023 comment cites "Crono's candy move: oracle holds 14/28
  for 16 f, we jittered 0x2D->0x20->0x40". I did **not** re-measure that scenario. Before
  deleting the cache, re-run it: if we still jitter with the cache gone, our re-issue cadence
  (`cycles` vs `$1A01`/`$1000,X`) is wrong and that is the real defect.
* **`C0/5B95` / `C0/ACFD` interaction.** 4FBD's object-solid sidestep adds `+0x30` to `$EE`
  *after* ABA2 and *before* the velocity issue; our `npcMoveAngle` does the same, but I did not
  grade it in this scenario (obj25 runs `0D 00`, so the map-collision arm never fires here).
* **Phase coverage.** I sampled 7 approach delays in ours (2 hang) and 6 in the oracle (0 hang).
  I did not enumerate every patrol phase, nor the other three ambush groups (obj18-20 /
  obj21-23 / obj30-32) — those fn3 bodies contain no MOVETO, so they cannot hang this way, but
  obj9-13's `96` moves in obj0's storyline-gated intro were not exercised at all.
* **A fresh `--scene 19` load** (no `--loadwram`) comes up with `storyline = 0` and only 1 placed
  NPC; there is no storyline override env var, so the fully-cold live path could not be driven
  here. Everything above is seeded from the oracle's own WRAM and then PLAYED.
* The obj1-polled loc[11] ambush (`D8 02 00` at body `0x00A9`) was also driven end-to-end and is
  clean: ours matches the oracle op-for-op (f4/f8/f20/f32 vs f4/f8/f20/f32).

---

# Adversarial review (2026-08-22, independent re-derivation)

**Verdict: the SYMPTOM and the mechanism are real and I reproduced both. The PRESCRIBED FIX IS AT
THE WRONG SITE, and the reviewer never disassembled the op that does the preempting.** The ROM's
CALL/REQ handlers END THEIR INSTALL ARM WITH `STZ $1A80,X` + `STZ $1A01,X` -- i.e. the ROM
explicitly DROPS the target's in-flight move when a function preempts it. Our `reqOp`
(`src/ct_vm.c:1915`) ports neither store. That single omission produces every symptom the dossier
reports (the infinite walk, the L-shaped path, AND the stale-facing "sibling leak"), and the
project ALREADY carries the byte-exact port of those two stores in `src/move.c:1259` / `:1351`.

Everything below was re-run by me, from my own snapshots
(`%TMP%\ctwf3\nostart-adv\ct_probe.exe` / `Belthasar.exe`), on states I built myself.

## R1. Every ROM citation re-checked -- all HOLD

| dossier claim | `tools/dis.py` | verdict |
|---|---|---|
| `4F63 LDA $1A01,X / 4F66 BEQ` ticket gate | `dis.py 4F61 90` | OK |
| `4F7C/$1801 4F81/$1881 -> $F0/$F1` (tile = position HIGH byte) | same | OK |
| `4F86 CMP $F3 / BNE ->4FAB`, `4F8C CMP $F2 / BNE ->4FAB` | same | OK |
| `4FAB JSR C0/ABA2` UNCONDITIONAL on the not-arrived path | same | OK |
| `4FB5 LDA $F700,X / 4FBA STA $1600,X` facing from the fresh heading | `dis.py 4FBA 40` | OK |
| `4FC2` +0x30 object-solid sidestep, `4FD4 $1000,X -> $1A01,X + $7F0B00,X`, `4FE0 JSR C0/ACFD` | same | OK |
| `C0/ABA2` reads ONLY `$F0..$F3`, writes `$EE` (+ `$EF/$F4/$F6/$F7` scratch), no per-object store | `dis.py ABA2 60`, `ABDC 30`, `ABF8 70` | OK |
| op-0x12 `loc[N]` = `$7F0200 + 2N` (`ASL A`) | `dis.py 62DE 30` -> `62E9 ASL A / 62ED LDA $7F0200,X` | OK |
| scene-19 script bodies (obj1 poll loop, obj24 `0x0357..0x0389`, obj25 `0x038A..0x03BF`) | raw dump of the seeded script image (`$7F2001 + IP`, base `0x0540`) | OK, byte-for-byte |

Two nits, neither load-bearing: obj1's loop has a `7D 52` at `0x0068` that the dossier's listing
omits; and the "-2.31 / -0.63 px/f" figures are leg AVERAGES -- the measured first-step deltas are
**-2.375 / -0.625** px.

## R2. Every oracle value re-measured on a state I built -- all HOLD

```
ct_probe --teleport 19 20 22 0 --from saves/solo_forest.state --out %TMP%\ctwf3\nostart-adv\fr_2022.state
#   -> leadPC obj=2 pos=(328,367) party=00/80/80 storyline=03      (identical to the dossier)
ct_probe --monitor "11B2:2,1C32:1,1A33:1,1AB2:1,1832:2,18B2:2" --from ...\fr_2022.state \
         --input "0:70,10:60,0:600" --diff --frames 200
```
* f119-f126 obj25 IP `08DC` (= body `0x039C`), **`$1C32 = 07`**, y `0x11FE -> 0x1224` (moving DOWN).
* **f127 IP `08E5` (body `0x03A5`), `$1C32` 07 -> 03, y `0x121F`, x `15F8 -> 15FD`** -- the new op
  re-aims on its FIRST step. Exactly as claimed.
* `$1AB2` (= `$1A80,X`) is `00` throughout for obj25; `$1A33` (= `$1A01,X`) alternates 01/00 (cadence 1).

Full cutscene, `--monitor "11B0:2,11B2:2,1832:2,18B2:2,1C32:1" --input "10:60,0:500" --frames 560`:
obj24 IP `08A1`@f28, `08A4`@f57 (+prio->03), `08AA`@f153, `08AD`@f245, `08B2`@f265,
**`08BA`@f289 (+prio->03, `$11B2 = 08FB`) = the user's exact IP pair**, `08BD`@f369, `08C4`@f381,
`0000`@f457 (battle). Identical to the dossier's table.
The fn4 diagonal at the preempt frame f289: x `0x1372 -> 0x134C` (-2.375 px) AND
y `0x11F6 -> 0x11EC` (-0.625 px) **on the same frame**.

User's save, `ct_probe --read "11B0+2,11B2+2,1C30+1,1C32+1,011F+1,0100+2,7F0218+2,1832+2,18B2+2"
--from saves/batalha_nao_comeca_lake.state`:
`$11B0=08BA  $11B2=08FB  $1C30=02  $1C32=03  $011F=00  $0100=0013  loc[12]=0000
 obj25 px = (305.0, 285.75)`. Every value as claimed.

## R3. The hang reproduces exactly -- and so does the D=0 pass

```
Belthasar --assets ct_assets.dat --scene 19 --loadwram ...\fr_2022.dumpwram \
         --selftest o.raw --frames 1400 --walkscript ".:<D>,u:60,.:1400"    (CT_VMTRACE=24, CT_BATTLE_TRACE=1)
```
| D | obj24 addr histogram | `battle-start` |
|---|---|---|
| 0 | 0356,0357,0361,0367,036D,0375,**037D(108f)**,0384,0387,0356 | **1** |
| 80 | 0356,0357,0361,**0367 x 1263 f** | **0** |
| 120 | 0356,0357,0361,0367,036D,0375,**037D x 943 f** | **0** |

`CT_CATTRACE=25`: t457 obj25 `addr=0399 prio=7` (patrol RIGHT leg) -> **t458 `addr=03B7 prio=3`**
-> t459 `addr=03BB` (`96 09 0E`, target UP-LEFT) -> thereafter x climbs monotonically
(f459 320 -> f799 **1083**, tile 67) with **y pinned at 287** and `face=3(R)`. Mechanism confirmed.

## R4. THE FIX IS AT THE WRONG SITE -- the ROM store the dossier never looked for

Event-op jump table `C0/5D6E`: op 0x02 -> `C0/5FB6`, op 0x03 -> `C0/607E`, op 0x04 -> `C0/6109`,
ops 0x05/0x06/0x07 -> `C0/61E9/61FE/6214`, which translate the party index via `$0197,X` and
**`BRL` back into the 0x02/0x03/0x04 bodies**. Every one of them ends its INSTALL arm the same way:

```
;  op 0x02 REQ   (python tools/dis.py 5FB6 80  +  dis.py 6003 60)
   6027: STA $1180,X      ; the new function pointer
   602E: STA $1C00,X      ; the new priority
   6031: STZ $1A80,X      ; <== DROP THE TARGET'S IN-FLIGHT MOVE
   6034: STZ $1A01,X      ; <== DROP ITS STEP TICKET
;  op 0x03        (dis.py 60F0 0x1C)          60F0/60F7 + 60FA STZ $1A80,X / 60FD STZ $1A01,X
;  op 0x04 CALL   (dis.py 6109 60 + 6144 90)  618C/6193 + 6196 STZ $1A80,X / 6199 STZ $1A01,X
;                                             61A0: STA $7F0980,X  = ARM THE CALLER'S WAIT LATCH
```
Both are reached ONLY on the preempt arm (`5FE7 BEQ` = same prio, `5FE9 BCS` = less urgent ->
the defer/park arms, which touch neither store). That is the canonical, RAM-flag answer to
"what happens to a mover when a function preempts it": **the ROM throws it away.**

Our `reqOp` (`src/ct_vm.c:1915-1938`) writes `ts->addr` and `ts->prio` and stops. The dossier
records this as *"deliberately touches nothing else"* and treats it as correct. It is not -- it is
two missing ROM stores.

**This is not a new derivation: the project already ported them.**
* `src/move.c:1259` -- `r[0x1A80+tgt] = 0; r[0x1A01+tgt] = 0;   // 6031/6034: drop any in-flight move`
* `src/move.c:1351` -- `r[0x1A80+tgt] = 0x00;   // 6196 STZ $1A80,X -- drop any in-flight move`
* `src/move.c:1419` -- `// 60FA / 60FD`

and `src/ct_vm.c` already fixes the mapping of that byte pair to our two fields, twice:
* `src/ct_vm.c:2367` -- `sc->hasTask[o] = 0; sc->cycles[o] = 0;   // 3526 STZ $1A80,X / 3529 STZ $1A01,X`
* `src/ct_vm.c:4432` -- `sc->hasTask[o] = 0; sc->cycles[o] = 0;   // 5B59/5B5C: STZ $1A80 / $1A01`

So the mandate-minimal change is **two lines in `reqOp`, on the install arm only**, right after
`ts->addr = fpPtr(...); ts->prio = priority;` (`src/ct_vm.c:1937`), matching the ROM's own order
(`$1180` -> `$1C00` -> `$1A80` -> `$1A01`):

```c
  ts->addr = fpPtr(sc, target, function); ts->prio = priority;
  sc->hasTask[target] = 0; sc->cycles[target] = 0;   /* C0/6031+6034 (op02) / 60FA+60FD (op03) /
                                                        6196+6199 (op04): STZ $1A80,X + STZ $1A01,X --
                                                        a preempting function DROPS the in-flight move */
```
It must NOT be added to the two early-return arms (`ts->prio == priority`, and the `ts->prio <
priority` defer) -- the ROM's `BEQ`/`BCS` skip both stores there.

**Why this is the right fix and the dossier's is not.** With `hasTask[target]` cleared, the
callee's first `moveOp` visit takes the fresh-move block at `src/ct_vm.c:1830`, which refills BOTH
`sc->moveAngle[obj]` (1838) **and** `a->facing` (1836). One ROM-cited store pair therefore closes:
1. the infinite walk (D=80/D=120 hang);
2. the L-shaped fn4 path (its first leg is pure-X *because the stale pure-X heading is held until
   that axis lands* -- same root, not a separate "hold" defect);
3. the stale-facing sibling leak the dossier names at `src/ct_vm.c:1836` -- **which its own PREFERRED
   fix does not touch at all**, because that line is behind the same `!hasTask` gate.

The dossier's PREFERRED fix (delete `moveAngle[]`) is a **spot fix on the symptom**: it makes the
runaway impossible by removing the only thing that could carry a stale heading, while leaving the
un-ported ROM stores absent, the stale facing intact, and the stale `cycles` ticket intact (which
matters the moment the target's `$1000,X` cadence is > 1 -- obj25's is 2 in our trace, `dc=1/2`).

## R5. "Reproduced: yes" is OVER-CLAIMED for the user's own scenario

From the user's own seed our engine **starts the battle and fights it**:
```
CT_VMTRACE=24 CT_BATTLE_TRACE=1 Belthasar --assets ct_assets.dat --scene 19 \
  --loadwram saves/forest_re/batalha_nao_comeca.dumpwram --selftest o.raw --frames 1500
-> obj24 addr: 037D (105 visits) -> 0384 (D8) -> 0387 -> 0356
-> "battlesfx f159 battle-start id=08 pan=80 (CD/04FD)", three Hetakes, cronoHP 70 -> 46 by f1384
```
The dossier states this in one line and still headlines **REPRODUCED**. What was reproduced is a
hang on a **self-made teleport seed at 2 of 7 sampled approach phases**. Whether that hang is the
user's "the battle does not start" is **not established** -- the user's captured moment does NOT hang here.
(Per memory `verify-negatives-on-proper-state`, the converse also applies: a PASS on a spliced seed
is not proof either -- see R6, which shows exactly why that seed passes for the wrong reason.)

## R6. A SECOND, INDEPENDENT divergence the dossier missed -- our op-0x04 does not park

`src/ct_vm.c:2213-2219`:
```c
  s->addr = nxt;                       // <== the caller's IP ADVANCES BEFORE the wait
  reqOp(sc, tgt, B[addr + 2] & 0x0F, pr);
  if ((op == 0x04 || op == 0x07) && tgt < sc->nobj) { s->waitObj = tgt; s->waitPrio = pr; s->callAddr = addr; return 1; }
```
The ROM does the opposite: `C0/6109` reads the caller's wait latch `$7F0980,X` FIRST (610B), the
install arm ends `61A0 STA $7F0980,X` + **`61A4 TYX / CLC / RTS` = PARK ON OUR OWN BYTE**, and the
IP advances by 3 only in the completion arms (`6114` / `61D8`+). Our trace shows the consequence
directly: at t1 from the user's seed obj24 already reads `addr=037D` while `$1180` in the seed is
`0x08BA` (= `0x037A`).

Worse, on that seed obj25 is ALREADY at prio 3 (`$1C32 = 03`), and our `reqOp` early-returns on
`if (ts->prio == priority) return;` -- installing **nothing**. The ROM's CALL in that case takes
`614A BEQ ->61A4` = **park and retry next visit** (it does not treat "already at that priority" as
done). So on the user's seed our whole `037A -> D8` sequence rides on state that merely happened to
be seeded, which is *why* that seed "works". It is not evidence of correctness.

I did not derive the full park/latch port here -- it is a separate ticket. Flagging it because it is
a live, ROM-cited divergence on the very state the user supplied.

## R7. What survives of the dossier's own analysis

* **`sc->moveAngle[]` IS invention.** Re-confirmed: `C0/4F61` stores no heading anywhere, `4FAB` is
  unconditional, `C0/ABA2`'s only output is the scratch `$EE`.
* But **DELETING it is not validated**, and the dossier says so itself: the justification at
  `src/ct_vm.c:1023` (Crono's candy diagonal, "oracle holds 14/28 for 16 f, we jittered
  0x2D->0x20->0x40") was never re-measured. Note also that the dossier's stated reason for the
  ROM's constant velocity -- "it is the ticket gate `4F63`" -- is **incomplete**: `C0/ABA2` is fed
  `$F0/$F1` and `$F2/$F3`, the position HIGH BYTES, i.e. **integer TILE deltas**, which do not
  change while the actor is inside one tile. Tile quantization is a second, independent reason the
  oracle's heading looks "held". Whether our `mvAngle(dtx - atx, dty - aty)` quantizes identically
  is an open question -- and it is the question that decides whether the cache can go.
  **Sequence: land the R4 ROM stores first (pure fidelity, zero invention), THEN re-derive the cache.**
* The **INTERIM** proposal (`moveAngleAddr[64]` keyed to the script address) is a forbidden stand-in
  and is now moot: the ROM flag exists, is named, and is already ported elsewhere in this repo.

## R8. Pinned checks that would move (`tools/regress_checks.json`, read -- suite NOT run)

Adding the R4 stores changes every scenario in which a REQ/CALL preempts a live mover:
* the touch-preempt seed, six checks on one state
  (`--vmdiff saves/battle_re/party3_field.state --frames 130`): **vmdiff-walk-touch-preempt**,
  **vm-activation-pass-0F00**, **vmdiff-wmask-party3**, **vmdiff-framelegs-party3**,
  **vmdiff-draw-visibility**, **vmdiff-oam-lockstep**;
* **vmdiff-DEMO8**, **vmdiff-S5**, **vmdiff-bedslide**, **vmdiff-withpos-bedslide**,
  **vmdiff-im_so_sorry**, **vmdiff-forest-idle-wanderers** (then vmdiff-forest-battle-entry), **field-cat-anime-once-duration**;
* the two that the `src/ct_vm.c:2695` note already names as sensitive to this exact gate:
  **field-battle-ambush-generic-dissolve**, **camera-reenter-scene5**;
* scene-5/439 movers: **render-scene5-snapshot**, **field-solid-liveness-fair**,
  **harness-play-goal-and-failure**; plus **vm-op-a0-pace-arrival**, **field-battle-bat9-return**,
  **field-drive-forest-exit-to-overworld**.

Deleting `moveAngle[]` on top of that additionally moves every op-0x96 **diagonal**, i.e. the same
`--vmdiff` set again plus the scene-439 candy move (**harness-play-goal-and-failure**).

Should NOT move (they exercise `src/move.c` / the packed-actor path, not `ct_vm.c`'s `reqOp`):
**npc-move-integration** (`--checkmove`), **actor-move-step** (`--checkstep`), **move-dir-angle**
(`--checkdir`), **porttick-autonomous-mover** (`--porttick`), **vm-512-scene-actors** (`--vm`).

## R9. Open questions (added by this review)

1. Is `$1A80,X` the exact analogue of our `hasTask`? `C0/4E1B` sets it to 1 and `C0/4FEC` /
   `C0/4D08` / `C0/5510` gate on it -- but `C0/4F61` (op 0x96) never READS it, and
   `src/move.c:1444` calls it "op 0x92's leg counter" while `src/ct_vm.c:4432` calls it
   "velocity". The STZ pair is unambiguous; the FIELD it maps to should be re-checked before the edit.
2. Does our `mvAngle(dtx - atx, dty - aty)` quantize the same way `C0/ABA2` does (tile high bytes,
   `$F300`/`$F400` atan tables)? This decides whether `moveAngle[]` can be deleted.
3. The op-0x04 park/latch port (R6): `$7F0980,X` and the `614A BEQ ->61A4` "target already at this
   priority -> park" arm are unmodelled. Needs its own derivation.
4. Our op-0x00 RETURN deliberately PRESERVES `hasTask`/`cycles` when resuming a lower prio
   (`src/ct_vm.c:2028-2034` comment). With the ROM dropping the mover at PREEMPT time, is that
   preservation still needed, or is it a second stand-in for the missing R4 stores?
5. Which of the four scene-19 ambushes does the user's report actually refer to? The
   `battle_crono_solo` seed (obj1-polled, walk LEFT) reaches `$2989 = 02` in both engines; the
   `batalha_nao_comeca` seed (obj24 chase) reaches `D8` and starts the battle in ours. Neither
   user-supplied state currently fails here.

**Reviewer's verdict: mechanism CONFIRMED, proposed fix REJECTED (wrong site: it leaves two ROM
stores un-ported and the facing leak unfixed), "reproduced" DOWNGRADED to `partially`.**
