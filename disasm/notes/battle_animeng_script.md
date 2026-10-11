# BATTLE ANIM ENGINE — PART 2: the POSE / SPRITE-ASSEMBLY anim-script format (`$1CA2/$1CC3`, facing `$96DE`, CD/5FF0 linkage)

Derives, from the ROM disasm + LakeSnes oracle **only**, the **low-level per-slot pose scripts** that drive
the combatant SPRITE poses — GATO's idle claw-swap, his four facings, the attack swings — the thing
`battle_enemy_move_target.md` A.4 flagged as "the walk-pose anim tracks `$1CC3+slot·3` / `$1CA2+slot·3`"
and `battle_pc_approach_facing.md` §2.5 flagged as the `$96DE`-keyed directional layout. This is the
**pose/assembly engine, DISTINCT from Part 1's tile streamer** (`battle_animeng_objslot.md`): the CD/5FF0
action scripts' `03/06 aa` "play-anim" ops (`battle_interp_verify.md` T2) resolve into THESE. **Zero
invention** — every field byte-cited (`tools/dis.py`, flat = `(bank−0xC0)<<16|addr`) and cross-checked on
`gato_press_action.state` (`ct_slot.exe = build/ct.exe`, HOLD driver `100:8,0:N`).

> **Where the two levels meet.** `battle_interp_verify.md` T2 decoded the HIGH-level ACTION script
> (`CD/5FF0[$9877]`, multi-track, ops `20`pause/`03·06`playanim/`1E`strike/`50`showdmg). Its `03/06 aa`
> op means "**set animId `$8E = aa`**" (§4 below). `$8E` selects a **pose script** (bank **E4**) — the
> per-slot stream this dossier decodes. So: CD action op `06 aa` → `$8E=aa` → E4-table lookup → pose
> pointers `$1CA2/$1CC3` → the per-frame pose tick → assembly index `$967B` → the bank-CF OAM builder →
> `$7E0700`. **The spans finally emerge** as "run the CD script; each `06 aa` swaps the pose".

---

## 1 — Architecture (the whole chain, one pass)

```
 animId $8E  ──►  E4/2600[$8E×2] = ptrA (16-bit) ──►  $1CA2+slot*3  (+ bank E4 in +2)   ┐ per-slot pose
             └►  E4/2800[$8E×2] = ptrB (16-bit) ──►  $1CC3+slot*3  (+ bank E4)          ┘ SCRIPT pointers
 facing $96DE ─────────────────────────────────────────────────────────────────────────┐
 per-frame pose tick (enemy: C1/3360 → C1/33xx):                                        │
   $82 = classBase($86) + facing×stride($9671)    ◄── FACING DISPATCH ───────────────────┘
   copy 8 bytes ptrA[$82]  → $97D9+slot*8   (assembly-frame set for this facing)
   copy 8 bytes ptrB[$86]  → $9799+slot*8   (per-frame DURATIONS, via CC/FBCE[])
   timer $9694,X--;  at 0: cursor $969C++ (wrap 4), frame = $97D9[base+cursor] → $967B, reload timer
 $967B (assembly index) ──► C1/1C4A/1C5D ──► bank-CF OAM builder (CF/E6D6, CF/EB60) ──► OAM $7E0700
```

## 2 — animId `$8E` → the pose-script pointers (bank E4)

**The loader `CC/EB0A`** (`tools/dis.py 0x0CEB00`, 16-bit):
```
CC/EB0A  LDA $8E / ASL A / TAX          ; X = animId*2
CC/EB0E  LDA $E42600,X / STA $1CA2,Y    ; ptrA lo/hi  (table E4/2600)
CC/EB17  LDA $E42800,X / STA $1CC3,Y    ; ptrB lo/hi  (table E4/2800)
CC/EB23  LDA #E4 / STA $1CA4,Y          ; ptrA bank = E4
CC/EB28  LDA #E4 / STA $1CC5,Y          ; ptrB bank = E4
```
So per enemy slot there are **two 3-byte pose-script pointers**: `$1CA2+slot*3` (ptrA) and `$1CC3+slot*3`
(ptrB), both in bank **E4**. **Initial** animId per combatant comes from a table at **`E4/F601-F603`**
(`CC/EAE0: LDA $E4F603,X → $8E`) — GATO's is **0x57**.

**Oracle-confirmed** (`--monitor "1CA2:33,1CC3:33,96DE:8,008E:2"` on `gato_press_action`, at f292 when
battle goes live): `$8E=0x57` (writer **CC:EAE9**) →
```
$1CAB (slot3 GATO ptrA) = C0 72 E4  = $E472C0   (= E4/2600[0x57])   writer CC:EB28
$1CCC (slot3 GATO ptrB) = 09 BF E4  = $E4BF09   (= E4/2800[0x57])
$96DE[slot3] = 01  (facing DOWN)
```
(This corrects `battle_pc_approach_facing.md`'s "`$1CCC/$1CAB` = 09BFE4/C072E4" — those were the raw
little-endian bytes; the 24-bit VALUES are `$E4BF09`/`$E472C0`, both bank **E4**, matching the loader.)

## 3 — The per-frame POSE TICK (enemy engine `C1/3360` → `C1/33xx`) and the FACING dispatch

`tools/dis.py 0x013360`. Runs per-frame per enemy slot (it `DEC`s the per-slot frame timer `$9694`):
```
C1/3364  LDA $5E0D,X / STA $974A,X       ; mode  <- request  (mode machine, battle_enemy_move_target A.4)
C1/336A  LDA $96DE,X / STA $96E9,X       ; latch facing
C1/3370  TXA / ASL / CLC / ADC $9C / TAX ; X = slot*3
C1/3376  $1CC3,X→$C3/$C4/$C5             ; ptrB (24-bit) -> $C3
C1/3385  $1CA2,X→$C0/$C1/$C2             ; ptrA (24-bit) -> $C0
C1/3394  LDA $96DE,X→$A5, $9671,X→$A7, $A2CB,X→$A8 / JSR C1/00A7   ; $A9 = facing×$9671 (+$A2CB)  [hw-mult]
C1/33A8  LDA $974A,X / TAX / LDA $CCFB91,X   ; mode -> class (table CC/FB91)
C1/33B4  LDA #0C (or #04 / #18 per class) / STA $86       ; $86 = class BASE offset
C1/33C8  ADC $A9 / STA $82               ; ** $82 = $86 + facing-scaled $A9  <- FACING DISPATCH **
   ; --- copy the facing's 8-byte assembly window from ptrA ---
C1/33D7  LDX $CCF82F[slot](=slot*8) / LDY $82
C1/33DF  LDA [$C0],Y / STA $97D9,X ; ×8   ; ptrA[$82..+7] -> $97D9+slot*8  (the assembly-frame set)
   ; --- copy 8 durations from ptrB (facing-INDEPENDENT, read at $86) ---
C1/33F1  LDY $86
C1/33F4  LDA [$C3],Y / TAX / LDA $CCFBCE,X / STA $9799,slot*8 ; ×8   ; ptrB[$86..]→CC/FBCE[]→ $9799+slot*8
   ; --- tick the animation ---
C1/3409  DEC $9694,X / BNE done          ; per-slot frame TIMER
C1/3410  base $80 = slot*8 (JSR C1/0112 = ASL×3)
C1/3416  Y = $80 + $969C,X (cursor); dur=$9799,Y; if dur==0 -> STZ $969C,X (LOOP), Y=$80
C1/3425  LDA $97D9,Y / STA $967B         ; ** current ASSEMBLY-FRAME index -> $967B **
C1/3462  INC $969C,X / CMP #04 / BEQ STZ ; cursor advances, wraps at 4  (<=4 frames per animation)
C1/3471  reload $9694,X = $9799[base+cursor]  (next duration)
```
**Tables:** `CC/F82F = 00 08 10 18 20 28 30 38 …` (= `slot*8`, the per-slot stride into the `$97D9`/`$9799`
8-byte buffers). `CC/FBCE = 00 00 00 00 00 01 01 01 01 01 02 02 02 02 02 03 …` (maps a raw ptrB byte to a
duration unit; large bytes index past the 16-entry head into the continuation). `C1/0112 = ASL A×3 / RTS`
(= `slot*8`).

**The FACING dispatch, precisely:** `$82 = $86 + facing × $9671[slot]`. `$9671[slot]` is the per-monster
**window stride** (GATO = **0x28**, oracle-read at f292); `$86` is the **mode-class base** (`0x0C` for the
idle/watch class, `0x04`/`0x18` for others, from `CC/FB91[mode]`). So **facing selects which 8-byte window
of ptrA is copied into the slot's frame buffer** — each window is one direction's assembly sequence. The
pointer `$1CAB` stays constant across facings (as `battle_pc_approach_facing.md` §2.5 observed); it is the
READ OFFSET `$82` that the facing shifts. (`$96DE` is ALSO re-read downstream by the bank-CF assembly
builder for the final cell layout — facing is consumed at both stages.)

**Combatant→slot:** this tick is the **ENEMY** pose engine (indexed by the enemy `slot`, `$9C`). At f292
only slot 3 (GATO) had a loaded ptr; PC slots 0-2 held the default `03 00 03`. **PCs use the CD/5FF0
anim-script path directly** (§4) for their poses — the Crono idle bob (`battle_stance.md` ADD.4) is a PC
anim, not this `$969C` cursor loop (its cursor stayed 0 while GATO's stepped). The `$8E→E4-table→assembly`
*mechanism* is shared; the PC-side per-frame tick is a separate (un-derived) routine.

## 4 — How CD/5FF0 "play-anim" ops resolve into the pose (the linkage — spans EMERGE)

The high-level action script (`CD/5FF0[$9877]`, `battle_interp_verify.md` T2) runs its tracks via the
executor `C1/5Bxx-5Fxx`. The **play-anim op handler** (`tools/dis.py 0x015FD0`):
```
C1/5FDE  INY / LDA [$E6],Y / STA $8E     ; ** animId $8E = the script's operand byte 'aa' **   ([$E6],Y = CD cursor)
C1/5FE3  … draw the assembly for $8E …
```
So the CD op `06 aa` (or `03 aa`) literally does **`$8E = aa`**. **Oracle proof** — during GATO's attack-A
(`$9877=0x31`, span f478-635), `$8E` is rewritten repeatedly by the anim-script executor:
```
f502 $8E=0D (writer C1:5FE3)   f503 $8E=01 (C1:4E3E)   f525 $8E=0A (C1:5FE3)
f531 $8E=02 (C1:5DDC)          f534 $8E=01 (C1:5E8B)   f571 $8E=E4 (C1:5BE7)
```
Each write swaps GATO's on-screen assembly to a new pose frame of the swing (windup → strike → recover),
sequenced by the CD script's `20`-pauses. The **span is exactly how long the CD script takes** — the pose
changes are its `06 aa` ops (`battle_interp_verify.md` T2.C "the span EMERGES"). NB the attack path draws
the `$8E` assembly **directly** (via C1/5FE3+); it does **not** reload `$1CA2/$1CC3` (those stayed
`$E472C0` all attack) — the mode-machine reload (§3) is for the walk/idle poses, the CD path is for the
scripted attack poses. Both consume `$8E` as the animId.

## 5 — The pose-script BYTE FORMAT (bank E4)

**ptrA (`$1CA2`, →`$C0`) = the ASSEMBLY-FRAME sequences.** A run of **`FF`-terminated frame-index lists**,
laid out as fixed-stride **facing windows** (stride = `$9671[slot]`). Each byte is a **sprite-ASSEMBLY
index** (which 16×16-cell layout to draw; the assembly table itself lives in bank CF — the render
boundary). The tick copies an 8-byte window (`$82`) and plays cursor 0..3 of it; a `00` duration in ptrB
loops before the `FF` is reached, so `FF`/trailing `00` are terminator/pad.

**ptrB (`$1CC3`, →`$C3`) = the per-frame DURATIONS**, mapped through `CC/FBCE[]` to dwell units. Read at
`$86` (facing-independent), so all facings of one animId share timing.

GATO ptrA `$E472C0` (0x50 bytes) and ptrB `$E4BF09` (raw), byte-decoded:
```
ptrA: 01 FF 00 00 00 | 01 02 01 FF 00 00 00 | 01 10 FF 00 | 01 FF 00 00 | 0C FF 00 00 |
      0D 0E 0F 0E 0E FF 00 00 | 11 12 13 FF 12 13 FF 00 |          <- window group A (offsets 0x00..0x27)
      04 FF 00 00 | 03 04 05 04 FF 00 00 00 | 04 14 FF 00 | 04 FF 00 00 | 0C FF 00 00 |
      0D 0E 0F 0E 0E FF 00 00 | 15 16 17 FF 16 17 FF 00 |          <- window group B (offsets 0x28..0x4F)
ptrB: 0F 00 00 00 0F 0F 0F 0F 00 00 00 00 1F 1F 00 00 00 00 00 00 05 00 00 00 0F 0F 0F 05 05 00 …
```
Groups A/B are **0x28 apart** (= GATO's stride) — the two facing windows. Shared sub-sequences
(`0C FF`, `0D 0E 0F 0E 0E FF`) repeat in both; the *differing* runs `11 12 13`/`12 13` vs `15 16 17`/`16 17`
are the two directions' distinct claw frames.

## 6 — Example 1 (VALIDATED): GATO idle claw-swap — animId 0x57, facing 1 → frames 0x04 ↔ 0x14

Facing = 1 (down, oracle-read). Class base `$86 = 0x0C`; `$A9 = 1 × 0x28 = 0x28`; **`$82 = 0x0C + 0x28 =
0x34`**. `ptrA[0x34..0x3B] = 04 14 FF 00 04 FF 00 00` (verified by ROM read). So the tick copies
`04 14 FF 00 …` into GATO's `$97D9` and plays cursor frames **0x04 (arm-A / rest)** and **0x14 (arm-B /
swung)**, looping (the duration wraps before the `FF`). For contrast, facing 0 (`$82=0x0C`) →
`ptrA[0x0C]=01 10 FF 00` = frames 0x01/0x10.

**Validated against the OAM timeline** (`--monitor "969C:8,9694:8,96DE:8"`, HOLD idle): GATO's cursor
`$969F` (slot 3) steps **0→1→2→3→0**; the steady cycle **f658→f779 = 121 frames ≈ 120** with per-frame
dwells **27 / 34 / 27 / 33**, the long arm-A rest + short arm-B swing — exactly the
`battle_stance.md` ADD.8 "**period 120, arm-A run 87f, arm-B run 33f**" claw-swap. Facing `$96DE[3]=1`
constant. `$9694` timer units are 1-2 (CC/FBCE-mapped), stepped at the battle anim cadence. **This is the
"gato moves a little".**

## 7 — Example 2 (derived + facing-validated): GATO's four facings / the walk-pose selection

`battle_pc_approach_facing.md` §2 observed GATO's `$96DE` cycling **0(up)/1(down)/2(left)/3(right)** as he
walks. Two independent facing controls compose:
1. **Which POSE SCRIPT** — the mode/approach handler sets `$8E` from the walk BEARING:
   `C1/38F6: ADC #40 / JSR C1/01F9 / STA $8E` (`tools/dis.py 0x0138EF`) — a bearing→animId map — and
   simultaneously `$96DE = C0/F700[bearing]` (`C1/3900`, the `ctBattleFaceTab` of `battle_trig.h`). So the
   walk direction picks the animId (hence the ptrA) for that gait.
2. **Which WINDOW of it** — `$82 = $86 + facing × $9671[slot]` (§3) offsets into that ptrA; group A vs B
   above are two such windows (frames 11-13 vs 15-17).

**Validated linkage** (facing dossier §2.3, re-confirmable): as GATO's bearing sweeps, `$96DE` follows
`ctBattleFaceTab[bearing]` (0xC0→0/up, 0x40→1/down, 0x80→2/left, 0x00→3/right) and the drawn body
re-lays-out per facing (distinct md5s — `battle_pc_approach_facing.md` §2.4). The full 4-window→facing map
inside E4 (which `$82` yields left vs right vs up) needs a per-facing `$82`/`$86` read during an actual
walk (only the idle down-window 0x34 and the facing-0 window 0x0C are byte-pinned here) — §9.

## 8 — Integration note (how tick + script + facing compose; what a port needs)

- **Three cooperating pieces:** (1) the **animId selector** `$8E` — set by the initial table `E4/F601`,
  the enemy **mode/bearing** handlers (walk), and the **CD/5FF0** play-anim ops (attack) — all feeding the
  same `E4/2600·2800 → $1CA2/$1CC3` loader; (2) the **per-frame pose tick** (§3) that turns
  (pose-script × facing × timer) into a current **assembly index `$967B`**; (3) the **bank-CF sprite
  ASSEMBLY builder** (`CF/E6D6`, `CF/EB60`) that turns `$967B` + the combatant's live position into OAM
  `$7E0700`.
- **What the C port needs from the remaining components:**
  - **Bank-E4 pose scripts** (ptrA sequences + ptrB durations) + the `E4/2600·2800` and `E4/F601` tables —
    baked verbatim (small; provenance like `battle_trig.h`).
  - **The tables** `CC/F82F` (slot stride), `CC/FBCE` (duration map, full continuation), `CC/FB91`
    (mode→class), and the per-monster window stride `$9671` + class base `$86`.
  - **The bank-CF sprite-ASSEMBLY table** (assembly-index `$967B` → the 16×16-cell OAM layout + tile
    numbers per facing) — **the standing anim-engine boundary** (`battle_stance.md` S4). This is the one
    piece still baked in the current engine (`battle_anim.h` keyframes ARE snapshots of these assemblies);
    a full port replaces the bakes with: run the pose tick → get `$967B` → look up the CF assembly →
    emit OAM. The Part-1 tile streamer supplies the pixels those OAM tiles point at.
- **This retires the pose bakes** exactly as the user's directive wants: `ctGatoIdleTrajV2` (period-120
  claw-swap) = pose-tick(animId 0x57, facing 1, frames 04/14); the entrance/attack ladders =
  CD-script `06 aa` sequences; the four facings = the `$96DE` window dispatch. All emerge from the engine
  instead of frame tables.

## 9 — Open questions
- **The full assembly index → OAM cell layout** (bank-CF `CF/E6D6`/`CF/EB60` builder): how `$967B` +
  `$96DE` index the sprite-assembly (cell offsets, tile numbers, flips) — the render boundary. Decoding it
  is what makes the OAM byte-exact and fully retires `battle_stance.h`/`battle_anim.h`.
- **The exact duration table** `CC/FBCE` continuation (bytes >0x0F) and the cadence multiplier that turns
  the 1-2 timer units into the observed ~30f dwells (the battle anim tick rate). Mechanism clear; the
  frame-exact dwell per ptrB byte is un-tabulated.
- **The 4-facing window map inside a single animId** — only GATO idle's facing-0 (0x0C→01/10) and facing-1
  (0x34→04/14) windows are byte-pinned; left/right/up (facings 2/3/0 during a walk, with class base
  `$86=0x04`) need a live `$82`/`$86` read mid-walk, and may spill into the adjacent animId's ptrA (stride
  0x28 × facing 2 = 0x50 = end of GATO's ptrA) — i.e. the walk left/right likely use their OWN animIds
  (per the bearing→`$8E` map §7.1), not just an offset. Disambiguate with a walking-GATO capture.
- **The PC pose tick.** PCs are NOT in the `$1CA2/$1CC3/$969C` enemy engine (their cursors stay 0); the
  Crono idle bob + PC swings run a parallel PC-side routine off the same `$8E`/E4 tables — un-derived here.
- **`C1/01F9` in the bearing→animId path** disassembles as the `C0/F900` sine reader; how `$8E` becomes a
  valid pose animId from `sin(bearing+0x40)` (vs. an octant index) is not fully pinned (the mode handler's
  exact animId arithmetic per gait) — the *linkage* ($8E→pose) is proven, the *per-bearing value* is not.

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/e5608df4-.../scratchpad/`
- `ct_slot.exe` (= build/ct.exe). Base (repo root) `gato_press_action.state`.
- Monitors: pose pointers (`--monitor "1CA2:33,1CC3:33,96DE:8,008E:2"`), cursor/timer (`969C:8,9694:8`),
  animId+ptr during attack (`008E:1,1CAB:3,9877:1,A013:1 --writer` = the CD→$8E proof),
  buffers (`97D9:8,9799:8,9671:8`), idle cursor timing (`969F:1,9697:1`).
- Disasm: loader `tools/dis.py 0x0CEB00` + `0x0CEAE0`; pose tick `0x013360`/`0x013410`; play-anim op
  `0x015FD0`; bearing→animId `0x0138EF`; helpers `0x010112`/`0x0100A7`/`0x0101F9`; tables `CC/F82F`,
  `CC/FBCE`, `CC/FB91`; pose scripts ROM `$E472C0`(ptrA)/`$E4BF09`(ptrB), tables `E4/2600`,`E4/2800`,`E4/F601`.
- Key ROM addresses: animId loader **CC/EB0A**; initial-pose table **E4/F601-F603** (CC/EAE0); pose tick
  **C1/3360-33xx** (facing `$82=$86+facing×$9671`); play-anim op **C1/5FDE** (`$8E=[$E6],Y`); enemy facing
  **C1/3900** (`$96DE=C0F700[bearing]`); OAM builder **CF/E6D6·EB60** ← `$967B`.
