# Scene 113 (Truce Canyon gate clearing) — the CLOSED GATE stays on screen

Derived 2026-08-22. Read-only RE pass. **Symptom reproduced.** The bug is NOT scene-113
specific and NOT a graphics/palette/BG problem: it is the **object loader ops 0x81 / 0x82 /
0x83 writing the per-object script-visibility flag `$1A81,X`, which the ROM's loaders never
touch.** Any object whose startup function does `HIDE` *before* its loader op is re-shown by
our port.

---

## 1. The symptom, measured on both sides

### Oracle (ROM truth)

```
build/ct.exe  ->  $TMP/ctwf/gate/ct_probe.exe          (never probe with build/ct.exe)

<probe> --teleport 113 8 10 0 --from saves/jogo_trava_lake.state --frames 120 \
        --out "$TMP/ctwf/gate/orc113.state"
  teleport[jmp]: from 070 -> requested 071, now 071  scene=LOADED  player=PLACED
    leadPC obj=2 pos=(136,175) facing=0  party=00/80/80 storyline=0C
    objects: 16 active, 16 placed

<probe> --peek "$TMP/ctwf/gate/orc113.state" "$TMP/ctwf/gate/orc113.raw" --btnscript "0:90"
python tools/ct.py fb2png "$TMP/ctwf/gate/orc113.raw" "$TMP/ctwf/gate/orc113.png"
```

Render: the clearing, Crono, the flower cluster on the BG. **No gate.**

### Our engine

```
./build/Belthasar.exe --assets ct_assets.dat --scene 113 --selftest out.raw --frames 90
python tools/ct.py fb2png out.raw out.png
```

Render: a ~100 px blue swirling disc centred on map px (136,127), drawn over Crono.

`CT_DRAWDBG=1 ./build/Belthasar.exe --assets ct_assets.dat --scene 113 --selftest o.raw --frames 3`

```
[draw f0] obj1  spr=104 af=0 at(8,15)     foot(0,0) prio=80 mode=04 ... blob=0@0
[draw f0] obj11 spr=158 af=1 at(136,127)  foot(8,7) prio=80 mode=04 ... rot=0/0/0 blob=0@0
```

**The circle is obj11, sprite 158, drawn from the normal actor loop.** Not BG3, not a baked
palette, not a colour-gated leftover (`rot=0/0/0 blob=0@0`, and obj11 is not `colorGated` —
the op-0x88 BLOB emitter in this scene is obj9 at tile (8,5), a different tile).

---

## 2. The per-object draw flag: `$7E:1A81,X` (X = obj*2)

`$1A81,X` is the **script visibility byte**: `01` shown, `00` hidden, `80` removed. It is the
gate `C0/A894` tests in the activation pass, which is what latches `$0F00,X`, which is what
`C0/B127` / `C0/B0FC` use to link an object into the draw list. (Already dossier'd:
`frame_wiring.md` §2358-2377, `field_touch_preempt.md` §285-300, `anim_drain_cad9.md` §48.)

### EXHAUSTIVE ROM scan — every writer of `$1A81`

Byte-pattern scan of the whole ROM for every addressing mode that can name `$1A81` as an
absolute operand (`9D/9E/8D/9C/99/9F/8F 81 1A`, plus the read forms):

| site | instruction | value | reached from |
|---|---|---|---|
| `C0/42A6` | `STA $1A81,X` | `#01` | op 0x80 / 0x57 / 0x5C / 0x62 / 0x68 / 0x6A / 0x6C / 0x6D — **party slot 0 arm** (`C0/429C`) |
| `C0/431A` | `STA $1A81,X` | `#01` | same family — **party slot 1 arm** (`C0/4310`) |
| `C0/43CD` | `STA $1A81,X` | `#01` | same family — **party slot 2 arm** (`C0/43C3`) |
| `C0/4CD9` | `STA $1A81,X` | `#01`/`#00`/`#80` | ops 0x90 SHOW (`C0/4CD5`), 0x91 HIDE (`C0/4CE0`), 0x7E REMOVE (`C0/4CE6`), 0x7C (`C0/4CEC`), 0x7D (`C0/4CF9`) |
| `C0/6264` | `STA $1A81,X` | `#00` | op 0x0A KILL (`C0/6254`, together with `$1100,X := 80`) |

Readers: `C0/062B`, `C0/A894`, `C0/B127`, `C0/CAE2`, `C0/CB12`, `C1/0DB8`.

Also checked: the five `STZ $1A80,X` / `STA $1A80,X` sites in bank C0 (`C0/31A4 32 5E 32FB
3380 3407 34D3 3526 4D15 4E1B 4E34 4E69 4E82 …`) are all under `SEP #$20` (8-bit A), e.g.

```
4E17: SEP  #20
4E19: LDA  #01
4E1B: STA  $1A80,X        ; 8-bit -> does NOT spill into $1A81
4E1E: STA  $1B80,X
```

so no 16-bit store at `$1A80` writes `$1A81` as a side effect.

### The three NPC/monster loaders write NOTHING to `$1A81`

Event-op jump table `C0/5D6E` (`handler = table[op*2]`):

```
op 80 -> C0/421E   op 81 -> C0/4476   op 82 -> C0/4590   op 83 -> C0/46DF
op 90 -> C0/4CD5   op 91 -> C0/4CE0   op 7E -> C0/4CE6   op 0A -> C0/6254
op 57 -> C0/41E4   op 5C -> C0/41EC   op 62 -> C0/41F4   op 68 -> C0/41FC
op 6A -> C0/4204   op 6C -> C0/420C   op 6D -> C0/4214
```

`C0/4590` (op 0x82) and its shared prologue `C0/4557`:

```
4590: LDA #04 / JSR C0/4557
4557: LDX $6D / STA $1100,X            ; mode = 4
455C: INY / TYX / LDA $7F2001,X        ; sprite id
4562: LDX $6D / STA $1101,X / STA $4202
456A: REP #20 / LDA $71  / STA $1280,X
4571: LDA #2020 / STA $0C00,X
4577: SEP #20 / LDA #7F / STA $1200,X
457E: LDA #01 / STA $1600,X            ; facing
4583: LDA #00 / STA $1680,X
458A: LDA #00 / STA $1681,X
458D: INY / CLC / RTS
459C: $1B01,X = 1
45A3.. : $1380/$1300/$1400/$1480/$1500/$1580/$1201  (the E4:F000 descriptor)
```

**No `$1A81` anywhere.** Same for `C0/4476` (op 0x81) and `C0/46DF` (op 0x83) — confirmed by
the exhaustive scan above, which finds no `9D 81 1A` inside `4476-46DE` or `46DF-47xx`.

`C0/421E` (op 0x80) and the `C0/426A` slot match it shares with the seven pcSprite ops **do**
write `$1A81 := 01`, but only in the three IN-PARTY arms. The not-in-party arm is

```
428C: LDA #80 / LDX $6D / STA $1100,X
4293: LDA $D9 / STA $1101,X
4298: TYX / INX / SEC / RTS            ; returns with CARRY SET, no $1A81 write
```

---

## 3. Scene 113's script — what actually runs

`python tools/ct.py script 113 [--obj N]`  (scene 113 -> script 334, 17 objects, body 1926 B)

### obj11 = THE GATE (sprite 158)

```
==== obj11 fn0 @0x0697 ====
  0x0697: 91           HIDE            <-- $1A81 := 00   (C0/4CE0 -> C0/4CD9)
  0x0698: 8B 08 07     SETCOORD tile(8,7) -> px(136,128)
  0x069B: 82 97        LOADPCsprite spr=158   (0x97+7)   <-- C0/4590: does NOT touch $1A81
  0x069D: AA 00        ANIMESET 0
  0x069F: 00           RETURN
==== obj11 fn3 @0x0693 ====  90 SHOW / 00
==== obj11 fn4 @0x0695 ====  91 HIDE / 00
==== obj11 fn1 @0x06A1 ====  E3 00 / 4A 10 01 00 01 / E8 05 / 91 HIDE / 04 02 16 (obj1 fn6) / D9 ... / 04 02 27
```

**The HIDE is the FIRST op of fn0 and the loader op is the THIRD.** That op order is the whole
bug. obj11 is hidden on every arrival at every storyline; only another object CALLing its fn3
ever shows it.

### obj0 fn0 at storyline 0x0C, op by op

```
0x0000: 8B 00 00        SETCOORD (0,0)
0x0003: 0B 18 / 0B 1A / 0B 1C      pause cadence of obj12/13/14   (C0/6269: $1000,X |= 80)
0x0009..0x002B: D2 xx 03 / 71 09   x7   count active-party members into loc[9]
0x002C: 55 10           loc[16] := STORYLINE            (C0/698B, see §5.1)
0x002E: 12 10 51 00 03  if loc[16] == 0x51 -> 5A 54 SETSTORYLINE 0x54     (0x0C: NOT taken)
0x0035: B8 00 00 F7     string table @F70000
0x0039: 16 46 01 86 1C  if ($7F0146 & 0x01) != 0  (mode 0x86&0x7F = 6)    -> taken ($7F0146 = 03)
0x003E: 18 24 10        if storyline < 0x24                                -> taken (0x0C)
0x0041: D2 01 09        PC1 in active party?  party = 00/80/80 -> NO, jump
0x0050: 16 DB 05 01 03  if glb[219]... ; 0x0057: 10 05 ; 0x0059: 56 01 ED 01
0x005D: 16 5B 01 00 08  if $7F005B == 0x01 -> loc[15]=1 / 4A 10 01 00 01   ($7F005B = 00: NOT taken)
0x0069: 12 10 D3 00 07  if loc[16] == 0xD3 (211)
          TRUE : 0x006E E3 00 / 0x0070 0A 16  KILL obj11
          FALSE: 0x0074 0A 1E  KILL obj15 / 0x0076 0A 20  KILL obj16     <-- storyline 0x0C
0x0078: E8 FF / 0x007A: 00 RETURN
```

Oracle confirms the FALSE arm: `$1100[15] = $1100[16] = 0x80`, `$1A81[15] = $1A81[16] = 00`
(op 0x0A = `C0/6254`: `$1100 := 80`, `$1A81 := 00`).

**Answer to "does 'first time' matter?": NO.** No storyline/flag gate hides the gate object.
obj11 is hidden unconditionally by its own fn0. The only storyline-dependent thing obj0 fn0
does to it is *kill* it at loc[16] == 0xD3.

### Oracle per-object state, scene 113 (`orc113.raw.wram`)

```
obj  $1A81 $0F00 $1100      note
 0    01    00    07
 1    00    00    80
 2    01    80    00        lead PC (Crono), $0197 = 4 -> obj2
 3-8  01    00    80        never loaded here; STALE from loc 112 (see §4)
 9    00    00    04        spr 95   fn0 ends with 91 HIDE
10    00    00    04        spr 94   fn0: 91 HIDE, then `16 5B 01 00 02 / 90` -> $7F005B=00, no SHOW
11    00    00    04        spr158   THE GATE — hidden
12    00    00    80  (13,14 same)   op 0x83 monsters, fn0 ends 91 HIDE
15    00    00    80        KILLed by obj0 fn0
16    00    00    80        KILLed by obj0 fn0
```

`$7F0000` storyline = `0C`; `$7F0220` (loc[16]) = `0C`; `$7F005B` = `00`; `$7F0146` = `03`;
`$7F2000` object count = `0x11` = 17.

---

## 4. Two-directional proof (disasm predicts -> oracle shows; oracle shows -> disasm explains)

The write-watch is the decisive measurement. Capture the state just before the scene loads,
then watch obj11's byte (`$1A81 + 11*2 = $7E1A97`) with the writer hook:

```
<probe> --teleport 113 8 8 0 --from saves/jogo_trava_lake.state --frames 10 --out t113_10.state
<probe> --monitor "7E1A97:1" --from t113_10.state --btnscript "0:40" --diff --writer
  f0      7E1A97=01*
  f9      7E1A97=00*(C0:4CDC)          <-- obj11 fn0's `91`; C0:4CDC = the PC AFTER STA $1A81,X @4CD9
  (nothing else, ever)
```

At the same time obj11's `$1100` becomes `04` and its IP lands at `stopAddr+1` — i.e. the
`82 97` loader op at 0x069B **did execute**, right after the HIDE, and **left `$1A81` at 00**.
That is the ROM saying, in its own RAM, exactly what `C0/4590` says in its own bytes.

Control experiment (the same gate object in another scene, so the probe is not tautological):
scene 227 obj11 has the identical fn0 (`91 / 8D 00 18 F0 05 / 82 97 / AA 00 / 00`):

```
<probe> --teleport 227 8 8 0 --from saves/jogo_trava_lake.state --frames 40 --out t227_40.state
<probe> --monitor "7E1A97:1" --from t227_40.state --btnscript "0:80" --diff --writer
  f0      7E1A97=01*
  f14     7E1A97=00*(C0:4CDC)     <-- the same HIDE
  f25     7E1A97=01*(C0:4CDC)     <-- a LATER script SHOW (op 0x90/0x7C), not the loader
```

Both writers are `C0/4CD9`'s store. The loader never appears.

### `$1A81` is NOT re-initialised at scene load

Compare the whole `$1A80` page before and after the 112 -> 113 hop:

```
loc112 $1A80..: 00 01 x29 ... slot29 = 00 ... slot30,31 = 01, slot32+ = 00
loc113 $1A80..: slots 0..16 rewritten by scene 113's scripts;
                slots 17..31 IDENTICAL to loc 112, including the lone 00 at slot 29
```

Scene 113 has 17 objects, so slots 17..31 belong to no object here and carry loc 112's values
verbatim. Together with the exhaustive writer scan (only 5 indexed stores in the whole ROM,
all event-op handlers) this pins: **nothing in the scene loader initialises `$1A81`.** An
object loaded by op 0x81/0x82/0x83 that never runs 0x90/0x91 inherits the incoming value —
which in practice is `01`, and which our port's `draw = 0` default at `CtSceneCreate`
(`src/ct_vm.c:2840`) already reproduces.

---

## 5. WHERE OUR ENGINE IS WRONG

### 5.0 THE BUG (this report)

`src/ct_vm.c` — the init runner `runFunc()` (line 779) and the live stepper both clear the
draw flag inside the loader ops:

```
 829:  else if ((ps = pcSprite(op)) >= 0) { a->sprite = ps; a->defined = 1; a->draw = 0; ...
 841:  else if (op == 0x80) { ... a->draw = 0; ...
 851:  else if (op == 0x81) { ... a->draw = 0; ...
 853:  else if (op == 0x82) { a->sprite = B[addr + 1] + 7; a->defined = 1; a->draw = 0; ...   <-- SCENE 113
 855:  else if (op == 0x83) { ... a->draw = 0; ...
1285:  (live stepper, pcSprite)   1293: (0x80)   1301: (0x81)   1303: (0x82)   1305: (0x83)
```

`draw` is the port's encoding of `$1A81` (0 shown / 1 hidden / 2 removed) and it is written
back to WRAM at `src/ct_vm.c:2977-2979`:

```
for (int o = 0; o < sc->nobj; o++)
  w8(sc->mem, 0x7E1A81 + (uint32_t)o * 2,
     sc->actors[o].draw == 0 ? 0x01 : (sc->actors[o].draw == 2 ? 0x80 : 0x00));
```

Chain for scene 113 obj11: `91` sets `draw = 1` (line 913) -> `82 97` at line 853 sets
`draw = 0` -> seed writes `$1A81 = 0x01` -> the `$0F00` activation pass (port of C0/A810 +
C0/A88D, `src/ct_vm.c:3919-3930`, which deactivates when `$1A81 == 0`) latches `$0F00 != 0`
-> `CtSceneActor` (`src/ct_vm.c:4080-4081`) reports `placed = 1` -> field.c draws sprite 158.

**Minimal mandate-safe fix:** delete `a->draw = 0;` from the op 0x81 / 0x82 / 0x83 arms in
BOTH places (init runner lines 851 / 853 / 855, live stepper lines 1301 / 1303 / 1305). The
ROM's `C0/4476`, `C0/4590`, `C0/46DF` contain no `STA $1A81,X`; the port must contain no
write either. Everything else in those arms (sprite, defined, solid, moveMode, the `$7E` load
mirrors) is correct and must stay. No scene gate, no heuristic, no constant.

Blast radius (why this is structural, not a spot fix) — scan of all 512 scenes for a fn0 whose
last visibility op precedes its loader op, with the false positives (branch-guarded HIDEs)
removed by inspection:

| scene | obj | fn0 shape |
|---|---|---|
| 113 | 11 | `91` @0x0697 then `82 97` @0x069B  (this report) |
| 227 | 11 | `91` @0x0607 then `82 97` @0x060D  (same gate object; verified in the oracle above) |
| 272 | 8, 9 | `91` then `82` |
| 404 | 9 | `91` then `83` |
| 419 / 420 / 421 / 423 | 8 | `91` @0x06EF then `81` @0x06F7 |
| 428 | 7 | `91` @0x03A5 then `81` @0x03AD |
| 273 | 6 | `91` then `80` — see 5.2, op 0x80 is the *conditional* case |
| 204 | 7, 8, 9 | FALSE POSITIVE: the `91` sits in the not-taken arm of `16 F3 40 06 06` |

### 5.1 SECONDARY — op 0x55 is INVERTED

ROM `C0/698B`:

```
698B: INY / TYX / LDA $7F2001,X        ; operand
6991: REP #20 / AND #00FF / ASL A / TAX
6998: SEP #20
699A: LDA $7F0000                      ; the STORYLINE
699E: STA $7F0200,X                    ; -> loc[operand]
69A2: TYX / INX / SEC / RTS
```

i.e. **`loc[operand] := storyline`**. Ours does the opposite:

```
src/ct_vm.c:817   else if (op == 0x55) w8(mem, GLB, r8(mem, LOC + B[addr + 1] * 2));
src/ct_vm.c:1252  else if (op == 0x55) w8(m,   GLB, r8(m,   LOC + B[addr + 1] * 2));
        (GLB = 0x7F0000, LOC = 0x7F0200 — src/ct_vm.c:98-99)
```

so it writes the STORYLINE from a local instead of the local from the storyline. Oracle
evidence in this very scene: after obj0 fn0's `55 10`, `$7F0220` (loc[16]) reads `0C` and
`$7F0000` still reads `0C`. It does not change scene 113's outcome (both compare arms read
loc[16] and neither matches 0x51 / 0xD3 either way), but it clobbers the storyline counter in
every scene that runs op 0x55. **Not fixed here — separate ticket.**

### 5.2 SECONDARY — op 0x80 / pcSprite write `$1A81` only when the PC is IN the party

`C0/426A` dispatches on `$D9` (char id) vs `$7E2980/81/82`; the three in-party arms (`C0/429C`,
`C0/4310`, `C0/43C3`) each do `STA $1A81,X` with `#01`; the not-in-party arm (`C0/428C`) writes
only `$1100 := 80` / `$1101 := id` and returns `SEC`. Our `a->draw = 0` at lines 829 / 841 /
1285 / 1293 is unconditional. Correct shape: write `draw = 0` only when `moveMode <= 2`.

### 5.3 SECONDARY — `pcSprite()` swaps char ids 3 and 4

```
C0/41FC (op 0x68): LDA $6D / STA $91 / LDA #04 / BRA ->421A     -> char id 4
C0/4204 (op 0x6A): LDA $6D / STA $90 / LDA #03 / BRA ->421A     -> char id 3
```

(the `$8D..$93` slot stores confirm the order: `$8D`=0 `$8E`=1 `$8F`=2 `$90`=3 `$91`=4
`$92`=5 `$93`=6). Ours, `src/ct_vm.c:425-426`:

```
case 0x68: return 3;   case 0x6A: return 4;      // SWAPPED vs C0/41FC / C0/4204
```

---

## 6. EXACT repro command lines

```sh
mkdir -p "$TMP/ctwf/gate"
cp build/ct.exe "$TMP/ctwf/gate/ct_probe.exe"

# --- oracle: the gate clearing, no gate ---
"$TMP/ctwf/gate/ct_probe.exe" --teleport 113 8 10 0 --from saves/jogo_trava_lake.state \
   --frames 120 --out "$TMP/ctwf/gate/orc113.state"
"$TMP/ctwf/gate/ct_probe.exe" --peek "$TMP/ctwf/gate/orc113.state" \
   "$TMP/ctwf/gate/orc113.raw" --btnscript "0:90"
python tools/ct.py fb2png "$TMP/ctwf/gate/orc113.raw" "$TMP/ctwf/gate/orc113.png"
python tools/ct.py objs  "$TMP/ctwf/gate/orc113.raw.wram"

# --- oracle: who writes obj11's $1A81 ---
"$TMP/ctwf/gate/ct_probe.exe" --teleport 113 8 8 0 --from saves/jogo_trava_lake.state \
   --frames 10 --out "$TMP/ctwf/gate/t113_10.state"
"$TMP/ctwf/gate/ct_probe.exe" --monitor "7E1A97:1" --from "$TMP/ctwf/gate/t113_10.state" \
   --btnscript "0:40" --diff --writer

# --- ours: the gate is drawn ---
./build/Belthasar.exe --assets ct_assets.dat --scene 113 --selftest "$TMP/ctwf/gate/our113.raw" --frames 90
python tools/ct.py fb2png "$TMP/ctwf/gate/our113.raw" "$TMP/ctwf/gate/our113.png"
CT_DRAWDBG=1 ./build/Belthasar.exe --assets ct_assets.dat --scene 113 --selftest o.raw --frames 3
CT_TRIGDBG=1 ./build/Belthasar.exe --assets ct_assets.dat --scene 113 --selftest o.raw --frames 3

# --- ROM ---
python tools/ct.py script 113 --obj 11
python tools/dis.py 4590 0xD0     # op 0x82 loader
python tools/dis.py 4CD0 0x40     # the C0/4CD9 show/hide tail
python tools/dis.py 6254 0x40     # op 0x0A KILL
python tools/dis.py 421E 0xC0     # op 0x80 party loader
python tools/dis.py 698B 0x20     # op 0x55
```

Frame timeline (from `saves/jogo_trava_lake.state`, `--teleport 113`):
`f<=10` still loc 112 (obj11 `$1100=05`, IP `049A`) — `f~19` scene 113 loaded and obj11 fn0's
`91` fires (`$1A97: 01 -> 00`, writer `C0:4CDC`) — no further write through f120.

---

## Open questions

1. **What the *incoming* `$1A81` value is for a fresh object loaded by 0x81/0x82/0x83 that
   never runs 0x90/0x91.** Measured: the array is not cleared at scene load, so the object
   inherits the previous scene's byte at that slot. Our port's `draw = 0` default matches the
   common case (`01`), but I did NOT find a scene where the incoming byte is `00` and the ROM
   therefore leaves a would-be-visible NPC invisible. Needs a targeted A/B (enter the same
   scene from two different previous locations and diff `$1A81`).
2. **The `SEC` return of `C0/428C`** (op 0x80 with a char not in the active party) and of
   `C0/698B` (op 0x55). Carry-set vs carry-clear changes what the op dispatcher does next; not
   derived here. Our port ignores it.
3. **Op 0x4A** (`4A 10 01 00 01` in scene 113 obj0 fn0 and obj11 fn1, handler `C0/67D7`) is not
   decoded in this pass.
4. **Which script SHOWs scene 227's obj11 at f25** (op 0x90 from its own fn4, or a 0x7C from
   another object) — measured that it happens, not who does it. Irrelevant to the fix, but it
   is the reason scene 227 must NOT be used as the regression anchor for this behaviour.
5. **Whether obj9/obj10 (spr 95 / 94, tile (8,5)) are the open-gate vortex pair** — both are
   `colorGated` candidates in our port's tile-colocation heuristic (`src/ct_vm.c:2938`). Their
   hidden state here is correct on both sides, so the question is untested by this scenario.
6. The fix cannot be validated in this pass: this was a read-only engine audit (no `src/`
   edits, no build, no `regress.py`). The prediction is that deleting the three `a->draw = 0`
   writes makes obj11 `$1A81 = 0x00 -> $0F00 = 0 -> placed = 0`, byte-matching the oracle.
