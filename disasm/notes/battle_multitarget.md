# MULTI-TARGET tech semantics — the group byte 0x12 (Cyclone) decoded + the radius multi-hit law

Derives, from the ROM disasm + LakeSnes oracle **only**, (1) the full decode of the target-group byte `$960D`
(who is selectable and how many are hit), and (2) the AoE multi-hit LAW for group **0x12** (Crono's Cyclone):
which enemies take damage, the per-victim damage/rand, deaths, and the dispatch loop. **Zero invention** —
every field is byte-cited (`tools/dis.py <fileoffset>`, fileoffset = `(bank−0xC0)<<16 | addr`) and every value
is oracle-measured (`ct_probe.exe` = `build/ct.exe`, `--monitor ADDR:LEN --from STATE --btnscript ...`; ⚠ LEN
is DECIMAL). This CLOSES the tech-slice open **T1** (`battle_tech_slice.md §7`).

> **Seed (canonical injection, like the tech-slice `$2837|=0x80`).** The forest fight (loc 19 Guardia Forest,
> 3× Hetake id 0x5E, 14 HP each) has Crono with NO learned techs. Patch the FIELD pre-battle state's persistent
> seed `$2837 |= 0x80` (Crono's MSB = his first single tech = Cyclone), then boot the ambush → the battle-entry
> staging picks Cyclone up. `forest_cyclone.state = patch_state.py forest_holdleft_pre.state forest_cyclone.state
> 2837=80` (repo `$2837`=0 pre-patch, verified). btnscript: hold LEFT 60f (trigger the op-0xD8 ambush), neutral
> to the command phase (f329, Crono seat0 ready `$A6D9`=00FF), then `Down` (cursor Att→Tech), `A` (open Tech
> submenu, Cyclone auto), `A` (confirm Cyclone → target-select), `A` (confirm the primary → dispatch).

---

## 0 — HEADLINE RESULTS (oracle-measured on `forest_cyclone.state`)

1. **The target-group byte `$960D` decode (`C1/1F9D`-`C1/1FC9`):** the low 7 bits (`& 0x7F`, clamped to 0x20)
   index a **jump table at `C1/1FF8`** (33 words) → the handler that BUILDS the selectable/hit set into the
   per-slot table `$99C0,X` (11 entries) → copied to the highlight `$A62D,X`. **bit7 (`0x80`) = PC-side** (tested
   separately at `C1/1F9D`, before the AND). So the byte is `[bit7 = side: PC/enemy][bits0-6 = a HANDLER INDEX]`
   — NOT a packed bit-field of arity/dead-flags; the ARITY/shape is encoded by WHICH handler (§1).

2. **Group 0x12 (Cyclone) = the cursor-PRIMARY + all alive enemies within a RADIUS of it** (`C1/225F`→`C1/2701`):
   a range-gated multi-hit. The primary is the target-select cursor slot (`$9614`); the neighbours are found by a
   distance scan. **Radius law, byte-exact:** `d = (Xc>>4 − Xa>>4)² + (Ya>>4 − Yc>>4)²  ≤  $9607 (=0x09 = 3²)`,
   where positions are `$1D0C,X`(X)/`$1D23,X`(Y) each scaled `>>4` (`C1/011A`), and the per-axis square comes
   from the table `CC/FB6F = [0,1,4,9,16,25,36,49,64,81,100,121,144,169,196,225]` (= n², idx 0..15). Group **0x1A**
   is the same handler with radius `0x19` (5²); group **0x11** is the same radius scan but centred on the CASTER
   (`$960F`) with radius `0x10` (4²).

3. **The multi-hit is a per-victim LOOP with an INDEPENDENT rand each iteration** (`C1/D221`/`D26A`/`D2DE`,
   `DEC $AD8D / BNE`): the hit set `$99C0` is copied to the action's target list `$AD8E`/`$AECC` with count
   `$AD8D`; the damage driver loops `$AD8D` times, each iteration re-selecting the current victim (`E9A3`/`E9B8`),
   running the FULL pipeline (to-hit `C1/DC64`, formula `C1/E0F5`, variance rand `C1/AF22`), and staging that
   victim's delta into ITS OWN block slot `$B328,Y`. **Deaths are marked LATER** (the apply loop `C1/EC39`, at
   span-end), so a victim that "dies" does NOT abort the loop — every target in the set is always computed.

4. **Apply (span-end, `C1/EC7C`-`ECD0`):** a per-block loop subtracts each block's staged delta(s) from curHP
   `$5E30,X` (three delta slots `$B328/$B354/$B380` per block for multi-hit stacking), clamps ≤0→dead. Out-of-set
   enemies get NO delta → untouched.

### The slice ledger (one clean cast, primary = Hetake slot0)
```
f234  $AF02 = 5E5E5EFFFFFF   3 Hetakes alive; enemy blocks curHP $5FB0/$6030/$60B0 = 0E/0E/0E (14 each)
f329  command phase, Crono seat0 ready ($A6D9 = 00FF)
f365  TARGET-SELECT opens: $960D=12  $9607=09(radius²)  $9605=03(primary=actor3)
        $99C0 = 03 04 FF..  ($A62D = 03 04 FF..)  = HIT SET {actor3, actor4} = slot0 + slot1  (slot2 OUT)
f384  DISPATCH: $AD8D=02 (count)  $AD8E=$AECC= 03 04 FF..  ; $AD89=0x14=20 (damage computed)
f385  $B328[block3]=0x0014  $B328[block4]=0x0014  (both victims staged 20, same frame)
f706  span-end apply: $5FB0 (slot0) 14→0 (C1:ED12)  ; $6030 (slot1) 14→0xFFFA (C1:ECC7 subtract, underflow)
f707  $6030 (slot1) →0 (C1:ED12 clamp)  ; $AF02 = FFFF5E.. (slots 0,1 DEAD, slot2 alive 14 HP untouched)
```

---

## 1 — THE GROUP-BYTE DECODE (`$960D`, target-select open `C1/1F79`)

`$960D` is written at confirm from the action's group: physical Att `#$07` (`C1/12AA`), a tech's `$9EE4`
(`C1/1376`), an item's `$1581` (`C1/14BF`). Target-select open (`C1/1F79`) consumes it:
```
1F9D  LDA $960D / BPL ..           ; ** bit7 test = PC-SIDE flag ** (0x80 items/PC-target -> the PC branch 1FA2)
1FBC  LDA $960D / AND #7F          ; low 7 bits
1FC1  CMP #21 / BCC .. / LDA #20   ; clamp to 0x20
1FC7  ASL A / TAX / JSR ($1FF8,X)  ; ** JUMP TABLE C1/1FF8[idx] -> the set-builder handler **
1FCE  (find first selectable: scan $A62D,X for bit7 clear)
```
The builder fills `$99C0,X` (11-entry per-slot table; `$99C0`=count/primary, `$99C1..`=list) and the highlight
`$A62D,X`. A handler encodes **the loop RANGE** (start 0=PC-side / 3=enemy-side; end 3=one side / 0x0B=all) and
**the ARITY** by setting one of two flags — `$960C = 0x80` (ALL-in-range, no cursor) or `INC $960A` (single
cursor-select) — or by calling the radius scan (§2).

### The jump table `C1/1FF8` (idx = `$960D & 0x7F`, clamped 0x20) → handler, with the decoded meaning
| idx | handler | range (start,end) | arity | meaning |
|---|---|---|---|---|
| 0x00 | C1/203A | 0..? | single | default single (cursor); many unused ids alias here |
| 0x01 | C1/209A | 0..3 | ALL (`$960C`) | all PCs |
| 0x02 | C1/20D6 | — | self | `$99C0=$95D5` (the caster) |
| 0x03 | C1/20E0 | — | — | conditional (reads `$5E4A` dead/alive) |
| 0x04 | C1/209A | 0..3 | ALL | all PCs (= 0x01) |
| 0x07 | C1/20A9 | 3..0x0B | single (`$960A`) | single enemy, cursor — **ICE's group** |
| 0x08 | C1/20B6 | 3..0x0B | ALL (`$960C`) | all enemies |
| 0x09 | C1/20C6 | 0..0x0B | ALL | everyone |
| 0x0A | C1/20B6 | 3..0x0B | ALL | all enemies (= 0x08) |
| 0x0B | C1/2169 | — | ALL | (`$960C`) |
| 0x11 | C1/224B | radius@**caster** | AoE | area around `$960F` (caster), radius² **0x10** |
| **0x12** | **C1/225F** | radius@**primary** | **AoE** | **CYCLONE — area around the selected enemy, radius² 0x09** |
| 0x13/0x14 | C1/22A4 | — | — | party-membership scan (`$2980`) |
| 0x18 | C1/2329 | — | — | (`$99CC` sub-dispatch `C1/1FEA`) |
| 0x1A | C1/225F | radius@primary | AoE | = 0x12 but radius² **0x19** |
| 0x1B | C1/22D3 | — | — | — |
| 0x0F | C1/22F5 | — | — | — |
| others (0x0E,0x10,0x15-0x17,0x19,0x1C-0x20) | C1/203A | — | single | default |

**bit7 known values:** items use `0x80` (Tonic single PC → `& 0x7F` = 0x00 → C1/203A on the PC side via the
1F9D branch) and `0x81` (Lapis → 0x01 → C1/209A all-PCs). So `0x80|idx` = "the idx handler, PC side". Physical
Att = `0x07` (single enemy). Ice = `0x07`. Cyclone = `0x12`.

### `$960C` / `$960A` — the two arity flags (both cleared at entry `C1/1F80-1F86`)
- **`$960C = 0x80`** (set by a handler, or by the radius tail `C1/27E8`): the default epilogue (`C1/2088`)
  copies **ALL** `$99C0[0..10]` → `$A62D` — every in-range/in-radius slot is a target (the FF entries are skipped
  downstream). = "hit the whole set".
- **`INC $960A`** (single handlers, e.g. Ice `C1/20A9`): the epilogue copies only `$99C0[$9614]` → `$A62D` — one
  cursor-selected target.

---

## 2 — THE RADIUS MULTI-HIT LAW (Cyclone group 0x12, `C1/225F` → `C1/2701`)

### 2.1 Cyclone's handler (`C1/225F`)
```
225F  JSR C1/20A9              ; first run the single-ENEMY build (fills $99C0 with the alive enemies, cursor)
2262  STZ $960A                ; clear the single flag (NOT a plain single-select)
2282  LDA $9614 / TAX / LDA $99C0,X / STA $9605   ; ** $9605 = the CURSOR primary's actor index (the AoE centre) **
228C  LDA $960D / AND #7F / CMP #1A / (=1A? #19 : #09) / STA $9607   ; ** radius² = 0x09 (Cyclone) / 0x19 (0x1A) **
229E  JSR C1/2701             ; ** THE RADIUS SCAN ** -> $99C0 = {primary + in-radius}, then JMP C1/27E8 (sets $960C)
```
Group 0x11 (`C1/224B`) is the same scan with `$9605 = $960F` (the CASTER) and `$9607 = 0x10`.

### 2.2 The radius scan (`C1/2701`) — the distance test, byte-exact
```
2701  X=$9605 ; $80 = ($1D0C,X)>>4 ; $82 = ($1D23,X)>>4      ; centre coords, scaled >>4 (C1/011A = LSR×4)
271A  loop range $8E..$90 = [3, 0x0B) enemies  (or [0,3) PCs if $9604)
2735  for each X in range:
        skip if $96F5,X==0 (absent) / $9FF7,X bit7 (dead) / $A0A8,X!=0 / $A09B,X!=0 / X==$92 (the centre)
2751    $84 = ($1D0C,X)>>4 ; $86 = ($1D23,X)>>4
2768    dx = |$84 − $80| ; dy = |$86 − $82|                  ; 16-bit abs (REP #20)
2784    A = CC/FB6F[dx] + CC/FB6F[dy]                         ; = dx² + dy²   (FB6F = the squares table)
2797    CMP $9607 : if A <= radius² -> STA $99C1,Y / INY      ; ** INCLUDE this enemy **
27A5  next
27C0  $99C0 = $92 (re-add the CENTRE as the primary slot)    ; the loop EXCLUDED it; it is $99C0[0]
27E8  LDA #80 / STA $960C ; copy $99C0[0..10] -> $A62D        ; mark the whole set as targets
```
- **`CC/FB6F` = `00 01 04 09 10 19 24 31 40 51 64 79 90 A9 C4 E1`** = `[0,1,4,9,16,25,36,49,64,81,100,121,144,169,
  196,225]` = **n² for n = 0..15**. So the metric is Euclidean-squared in `>>4` (16-px) grid units. `dx`,`dy`
  must be ≤ 15 (the table's valid range); at 16px/unit that caps the reachable delta at 240px (whole field).
- **The centre (primary) is EXCLUDED from the loop** (`CPX $92 BEQ skip`) and **re-added as `$99C0[0]`** — so the
  final set = `[primary, ...in-radius neighbours (slot-order), FF...]`.

### 2.3 The set FOLLOWS the cursor (oracle-measured, primary moved with Down)
```
$9614=0 (slot0, primary actor3 @120,63):  $99C0 = 03 04 FF..   {slot0, slot1}   (slot2 @168,127 OUT)
$9614=1 (slot1, primary actor4 @88,95):   $99C0 = 04 03 FF..   {slot1, slot0}   (symmetric pair)
$9614=2 (slot2, primary actor5 @168,127): $99C0 = 05 FF..      {slot2} ALONE     (no neighbour in radius)
```
Arithmetic (byte-exact, `>>4` then subtract): centre slot0 `(7,3)`; slot1 `(5,5)`: `(5−7)²+(5−3)²=4+4=8 ≤ 9` IN;
slot2 `(10,7)`: `(10−7)²+(7−3)²=9+16=25 > 9` OUT. Centre slot2 `(10,7)`: slot0 `(7,3)` `9+16=25` OUT, slot1 `(5,5)`
`25+4=29` OUT → alone. **The target-select PRIMARY nav (`$9614` cursor) is UNCHANGED** (cycle all alive enemies,
skip-dead, `C1/176C`); the group only changes WHAT the confirmed primary HITS. So the "selectable set" for 0x12
is still "any alive enemy" (the primary); the AoE (`$A62D` highlight + the hit list) is derived from it.

---

## 3 — THE DISPATCH MULTI-VICTIM LOOP (the persist + the per-victim compute)

### 3.1 Hit set persists target-select → dispatch (oracle)
The confirm builds the queue record with the PRIMARY only; the radius list `$99C0` (built at target-select for
that primary) PERSISTS to dispatch. At animate, `C1/C600` copies it into the action's target arrays:
```
C600  loop X=0..0x0A over $99C0,X:
C612    (enemy) skip if $AF12,X bit7 (dead)
C621    $AD8E,X = $AECC,X = $99C0,X   ; the action target LIST
C62C    if != FF -> INY (count)
C63A  $AECB = $AD8D = Y              ; ** the target COUNT **
```
Oracle f384: `$99C0 = 03 04 FF..` → `$AD8E = $AECC = 03 04 FF..`, `$AD8D = 02`. (A single-target action → count 1,
list = [primary] — the identical path.)

### 3.2 The damage driver is a per-victim loop (`C1/D221` family) — INDEPENDENT rand each victim
```
D221 (type-4 enemy phys) : loop { E9A3 ; E9B8 ; D132 ; DA37 ; D508(stage) ; DEC $AD8D } while $AD8D
D26A (type-1 phys)       : loop { E9A3 ; E9B8 ; ... DC64(to-hit rand) ... D508 ; DEC $AD8D } while $AD8D
D2DE (type-3 magic etc.) : loop { E9A3 ; E9B8 ; DE9E ; DEF0 ; DC64 ; E0F5(formula+var rand AF22) ; E65F(def) ;
                                  DCAD ; D508 ; DEC $AD8D } while $AD8D
```
Each iteration `E9A3`/`E9B8` re-points the pipeline at the current target from `$AD8E`/`$AECC`; the to-hit
(`C1/DC64`, rand `#$64` at `D29E`) and the damage variance (inside `C1/E0F5`, rand `C1/AF22`) are drawn INSIDE
the loop. So **every victim gets its own to-hit roll AND its own damage-variance roll** — not one shared roll.
(Observed: `$26` advanced +2 between the compute frame f384 and the stage frame f385 = the two victims' rolls.)
The compute+stage (`C1/EC2F`, `STA $B328,Y`) writes each victim's delta into its OWN block slot; nothing is
applied yet. Both Hetakes here have identical stats → both rolled base 20 variance 0 → both 20 (visible as the
two `$B328` slots = 0x14 at f385).

### 3.3 Apply + deaths (span-end, `C1/EC7C`)
The apply loop iterates all 11 blocks, subtracting each block's staged delta(s) from curHP `$5E30,X`, clamping
≤0 → the dead flag (`+0x1D` bit7) + the alive-list `$AF02` clear + the reward accumulate (`FD/ABA2`). Because the
death marking is HERE (after the whole compute loop), a mid-set death never aborts the compute — **all victims
are always processed**, then all applied. Out-of-set blocks have a zero delta → untouched (slot2 stayed 14).
Counters: a surviving counter-enemy (GATO's cond-1F block-2) would arm per-victim on its qualifying hit
(`battleApplyDamage`); Hetakes have no counter (`battle_counter_gating.md`), so none fires here — the per-victim
counter-arm is structurally correct but UNEXERCISED (both victims died).

### 3.4 The tail (span 313, the 28f "multi-target tail")
Cyclone's `$A013` span is 313f (tech-slice §3.3), decomposed WINDUP 227 + POPUP 58 + TAIL 28. The 28f tail (vs a
single-hit's 5/7) is the per-victim apply staggering (oracle: slot0 finalised f706, slot1's clamp f707 — the
apply walks the block list). The span is a per-tech PINNED constant (the bank-CE playback is the T3 boundary),
NOT recomputed from the victim count — so the ENGINE keeps `CT_SPAN_PC_TECH_CYCLONE=313` unchanged.

---

## 4 — IMPLEMENTATION SPEC (`src/battle.c`, MECHANICS ONLY; visuals stay GATO-scoped)

The engine's action seam is single-target (`h->actTarget`/`h->actValue`, applied once at span-end
`battleAdvanceAction`). The multi-target extension keeps that seam and adds a per-action victim LIST:

**(a) Host state (`battle.h`):** `int actTgtN; int actTgtList[8]; int actTgtVal[8];` — the dispatched action's
victim set + per-victim damage (N=1 for every single-target action → byte-identical). Reset in `CtBattleBegin`.

**(b) The radius set-builder (new `battleBuildHitSet`):** given the primary combatant + the group, return the hit
set. `group != 0x12 && group != 0x1A` → `{primary}` (single). Else: `out[0]=primary`; for each alive enemy slot
`cmb=3+slot != primary`, `dx=|(mvLiveX[cmb]>>4)-(mvLiveX[primary]>>4)|`, `dy` likewise; include iff `dx≤15 &&
dy≤15 && dx*dx+dy*dy <= (group==0x1A?0x19:0x09)`. Positions = `h->mvLiveX/mvLiveY` (== the ROM `$1D0C/$1D23`;
proven equal — the forest Hetakes stage to 120/88/168, 63/95/127 = `16*r[0]+r[2]` from `$29BF`).

**(c) Dispatch (`battleDispatchScheduled`, PC-tech branch):** after resolving the primary `tgt` + `techId`, build
`h->actTgtN = battleBuildHitSet(...)` and compute per-victim: `for i: actTgtVal[i] =
battleComputeTechDamage(h, atk, actTgtList[i], techId)` (each call draws its own variance rand → independent per
victim, matching §3.2). `h->actValue = actTgtVal[0]` (the primary, for the single-victim readers). Every OTHER
dispatch path (physical, item, enemy, magic-tech Ice, `battleOpenAction`) sets `actTgtN=1`, `actTgtList[0]=tgt`,
`actTgtVal[0]=actValue` → unchanged.

**(d) Span-end apply (`battleAdvanceAction`):** replace the single `battleApplyDamage(h, actTarget, actValue)`
with `for i in [0,actTgtN): battleApplyDamage(h, actTgtList[i], actTgtVal[i])` (the item path stays single). N=1
reduces to the old call byte-for-byte (deaths/rewards/counter-arm per victim via the existing `battleApplyDamage`).

**(e) Popups (per victim):** the late-popup spawn loops the victim list (each victim's own anchor + value —
`battle_actionfx.md` P1 anchor law, unchanged), so a 2-hit Cyclone shows two numbers. Single-target → one popup,
identical. Visual flash/knockback/hurt-pose stay on the PRIMARY (`actTarget`) — GATO-scoped, not extended.

**Checks:** `field-battle-tech-cyclone-multi` — forest primary=slot0 casts Cyclone → both slot0+slot1 die (20
each), slot2 untouched (14 HP), determinism ×2; the existing `field-battle-tech-cyclone-dispatch` (GATO, single
enemy) already guards the single-target path (N=1) — verify it stays byte-identical.

---

## 5 — OPENS / BOUNDARIES

| # | item | status |
|---|---|---|
| M1 | **The radius scan's PC-side + caster-centred groups (0x11, 0x08/0x09 all-side, 0x13/0x14 party-scan)** | the jump-table decode + the radius routine are pinned; only 0x12 (enemy-primary radius) is EXERCISED end-to-end. A caster-centred AoE (0x11) or all-enemies (0x08) tech would confirm those handlers live (need a tech that uses them — none learned in the forest) |
| M2 | **The `$99C0` skip gates `$A0A8`/`$A09B`** (status-based target exclusion) | the radius loop skips actors with `$A0A8,X!=0` / `$A09B,X!=0` (some status/immunity); 0 for the plain Hetakes, so the exact condition is un-pinned |
| M3 | **Per-victim counter** | a surviving counter-enemy would arm per victim (`battleApplyDamage`); UNEXERCISED (Hetakes don't counter, and both died). Structurally covered |
| M4 | **The 3rd/2nd delta slots `$B354`/`$B380`** | the apply loop supports 3 stacked deltas per block (a multi-hit that strikes the SAME block twice); Cyclone hits each block once → only `$B328` used here |
| T3 (carried) | **The CE/D613 whirlwind pixels** | the presentation stays the tech-slice T3 open — the whirl fixture is GATO-scoped; this session touched MECHANICS only |

---

## SCRATCHPAD ARTIFACTS (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-9e84b785-…/scratchpad/`
- `ct_probe.exe` (= build/ct.exe). Base: repo `saves/battle_re/forest_holdleft_pre.state`; injected
  `forest_cyclone.state` (`patch_state.py … 2837=80`).
- Disasm anchors (fo = `(bank−0xC0)<<16|addr`): target-select open `0x11F79`; the group jump table `0x11FF8`
  (33 words); Cyclone handler `0x1225F`; the radius scan `0x12701`; the coord scaler `0x1011A` (LSR×4); the
  squares table `CC/FB6F` = `0x2CFB6F`; the dispatch target-list copy `0x1C600`; the damage driver loops
  `0x1D221`/`0x1D26A`/`0x1D2DE`; the delta stage `0x1EC2F`; the apply loop `0x1EC7C`.
- Monitors (btnscript `40:60,0:275,20:2,0:12,100:2,0:12,100:2,0:12,100:2,0:400`): the hit set `$99C0`/`$A62D`,
  the count `$AD8D`, the list `$AD8E`/`$AECC`, positions `$1D0F`/`$1D26`, enemy HP `$5FB0`/`$6030`/`$60B0`,
  alive `$AF02`, deltas `$B328`, `$26`.


---

# ADDENDUM 2026-10-04 -- a PC area tech's victims are C1/C586's, and they ride on the positions

The user: "when crono used cyclone hitting 3 of them, only 1 took damage / with fire whirl this also happened"
(the Manoria Naga-ettes). For a PC, C1/C1DD loads the tech's group CC/1ACB[id*2] & 7F and its AREA byte $0C =
CC/1ACC[id*2]; the Cyclone (01) and Fire Whirl (3C) are group 12, area 03. With $0C != 0 the area pass C1/C586 runs:
record CC/2AB0[3] = CC/2B1C `01 | 01 01 09` -> C1/C95C[1] = C1/C7A7 (`$9604 = $0A`, `$9605 = $06` = the target $B2AE,
`$9606 = 0`, `$9607 = 09`, `$9608 = 0`) and C1/1FDD routine 1 = C1/2701: the candidates 3..A that are present ($96F5),
not $9FF7 bit 7, $A0A8 = 0, $A09B = 0, not the centre, whose cells (`$1D0C` / `$1D23 >> 4`, C1/011A) satisfy
CC/FB6F[|dx|] + CC/FB6F[|dy|] <= $9607. The port's battleBuildHitSet is that law at the dispatch. What differed was the
POSITIONS: the walkers stopped for a whole enemy turn instead of at C1/4058's $A0FD, and the waits' menu passes did not
zero $E5 (the $A4 cycle ran a group ahead). Both fixed and graded: tools/naga_walk_cmp.py, 3600 frames identical
(check battle-naga-ettes-walk-as-the-cartridge-through-their-turns). The user's exact moment is not in the snapshot (a
CTFS holds no battle), so the case is closed by the mechanism, not by a replay.
Still open: the port gates the candidates on `$AF02` alone, not the ROM's four bytes; groups 0D 0F 11 13 1B (the other
shapes) still dispatch one victim. Groups 0B / 0C: see section 10.

---

## 9 — 2026-10-09: the one-victim latch and the held hit set (Blue Imps + Roly, loc 070)

The user: "in the battle against roly and blue imps, when using cyclone on two enemies, only one of them took damage".

**The latch.** `$B1FC` bit 1 = one victim. An enemy's physical attack sets it (AI op 01, C1/99C2-99C7; the forced
physical C1/BFAB too); C1/D357-D35D (`LDA $B1FC / BIT #02 / BNE`) ends the kind-3 damage loop after the victim it just
did. The PC command record's tech arm clears it before the tech runs:

    C1/BB49  LDX $06 / LDA $0001,X / BIT #10 ...      ; the record's class (+1) and kind (+2 = 01 / 03)
    C1/BB66  LDA $B1FC / AND #FD / STA $B1FC          ; ** the latch off **
    C1/BB6E  TDC / LDA #01 / STA $AD8D / LDA $0003,X / STA $AD8E / LDA $0006,X / STA $B18C ...

The port ran C1/BB66 only for the demo's records (battle.c battleDemoCommand). An interactive Cyclone dispatched after
a Blue Imp's attack found the bit set: the list is processed last-first, so the Roly (cmb 9) took the damage and the
loop stopped -- both Imps 0. Fixed in the interactive tech dispatch (battle.c, `C1/BB66-BB6B`).

**The dispatch REBUILDS the set (2026-10-15; this replaces "the held set").** C1/C600-C63E copies `$99C0,X` (X = 0..0A)
into `$AD8E,X` / `$AECC,X`, skipping an enemy entry whose `$AF12,X` bit 7 is set (hidden) and counting the non-FF ones
into `$AD8D` / `$AECB` -- but the `$99C0` it copies is the one the area pass C1/C586 has just built, not the select's.
The radius techs carry area 03 (CC/1ACC: Cyclone 01, Fire Whirl 3C, and 14 17 34 3B 3E 48 49 52-54): CC/2B1C = `01 / 01 01 09`, so
C1/C95C[1] = C1/C7A7 stages `$9604 = $0A` (0: the enemies), `$9605 = $06` (the action's target, `$B2AE` from C1/BB88),
`$9606 = 0`, `$9607 = 09`, `$9608 = 0`, then C1/C741 `$99CC := 01` and C1/1FDD[1] = C1/2701, the radius scan of sec. 2
on the feet `$1D0C,X / $1D23,X` of that frame (C1/27D9 FF-fills `$99C0 / $A62D`, C1/27AD / 27C5 the centre). C1/1FDD's
table C1/1FEA: 0 / 4 the band C1/25A3, 1 / 2 the radius C1/2701, 3 / 5 C1/23A4, 6 C1/2332. The square sum is an 8-bit
`ADC` of two CC/FB6F entries: it wraps past FF (|dx| = 15, |dy| = 6 -> 225 + 36 = 05, inside).
MEASURED, the rule's cartridge (--monitor --writer), gato_wait staged with Yakra, ACTIVE mode: Crono's Fire Whirl
confirmed during Yakra's attack, then Frog's Slurp on himself (its PC select writes `$99C0 = 02 00 01`, C1/203A) before
Fire Whirl dispatches: f937 `$B2AE = 03` (C1:BB88), `$99CC = 01` (C1:C746), `$99C0 = 03` (C1:27C5), `$AD8E = 03`.
The port held the select's table to the dispatch (`hitSetHeld`, from the 2026-10-09 Cyclone report, whose cause was
the `$B1FC` latch below): Fire Whirl struck Frog, Crono and Lucca (the user, 2026-10-15). It now runs the same pass
(battle.c battleAreaC586 + battleAreaRadius2701); tools/radius_dispatch_cmp.py, check
battle-radius-tech-victims-rebuilt-at-dispatch-vs-rule-cartridge. Still open: the radius around the CASTER (C1/C78D +
routine 1: areas 02 / 0A -- techs 1F, 2E, 37, 40, 6A) is not measured and keeps the group's own set.

**Fixture** saves/battle_re/loc070_imps_roly_pre_lake.state (+ .lakewram): loc 070 reloaded by the ROM's loader
(`--teleport 112 12 18 1`) after bat6's fight was won, its group (obj22's ambush: obj23 / obj24 Blue Imps, obj25 the
Roly) respawned, Crono 60 frames into a walk onto it. Down 60, then from f331 Down, A, A, A: the ambush f42, the fight
f187, Crono ready f271, the select confirmed during the Imp's attack (f345-505), the dispatch f513 with `$AD8E = 05 06
09`, the Imps 13 -> 0, the Roly 24 -> 3. tools/cyclone_victims_cmp.py, check
battle-cyclone-every-victim-after-an-enemy-attack-vs-rule-cartridge.

**The ambush's clock (fixed the same day).** The port's fight was ready 8 frames early (f263 / f271). obj22's fn1
(`E3 00 / DA / E3 00 / AD 05 ...`, cadence 4) meets op 0xDA with no follower in party slot 1:

    C0/345A  STY $C7 / TYX / TDC / XBA / LDA $99 / BPL ->3466
    C0/3463  INX / CLC / RTS                ; no follower: the IP steps past it and the VISIT ENDS

The live VM's arm (ct_vm.c) stepped past it and kept running (move.c's graded arm had the CLC), so obj22 reached its
`AD 05` wait on that visit -- 4 frames early (rule oracle: IP 035E at f42, 0360 at f46) -- and its wait loop met
loc[6] = 3 on another phase: the D8 at f122 instead of f130. Fixed; the check grades the ambush first (10/10 regions up
to the D8, Crono ready f271 on both sides) and puts the keys on the same frames on both sides.

**Closed (2026-10-15).** `$99C0` is one table for every PC and a second PC's select does rewrite it between a radius
tech's confirm and its dispatch -- harmlessly on the cartridge, whose dispatch rebuilds it (above).

---

## 10 — 2026-10-11: the band techs (Slash 02, Flame Toss 11) -- C1/C586 for a PC

The user: "Lucca's flame toss should hit more than one enemy in a line". Flame Toss is group 0C, area 05 (CC/1ACB);
Slash group 0B, area 01. The port dispatched both to one victim.

**The select** (C1/1FF8[0B] = C1/2169, [0C] = C1/2203): a single-enemy cursor (C1/20A9, the pad through C1/1B55 /
C1/27FA), then `STZ $9604 / LDA $960F / STA $9605` (the caster), `$9606 = $99C0[$9614]` (the cursor's enemy),
`$9607 = 02`, `$9608 = 00` (0B) / `01` (0C), `JSR C1/25A3` and `JMP C1/27E8` (`$960C = 80`, `$99C0 -> $A62D`): the hand
lights every enemy in the band.

**The dispatch** (C1/C1DD's PC arm, C23E-C257): `STZ $0A` (the candidates are the enemies), `$0C = CC/1ACC[id*2]` (the
whole byte), then C1/C586 over CC/2AB0[$0C]: area 05 = CC/2B24 `01 / 00 00 82`, area 01 = CC/2B14 `01 / 00 00 02` --
C1/C95C[0] = C1/C78D (`$9604 = $0A`, `$9605 = $00`, `$9606 = $06`, `$9607 = $08`, `$9608 = $0E`; C1/C74C stages $00 =
participant 0's seat, $06 = $B2AE) and C1/1FDD routine 0, the band. The list goes to $AD8E / $AECC (an enemy whose $AF12
bit 7 is set left out), C659-C72B squeezes the absent.

**Measured**, the rule's cartridge, saves/field_re/m81_nuns_lake.state with `PEEK_POKE 7E2839=80` (Flame Toss learned):
Crono attacks (f470), Lucca's menu f727, Tech / Flame Toss / A: `$9604-$9608 = 00 01 03 02 01`, `$99C0 = $A62D = 03` at
the select (f767); a Down: `00 01 04 02 01`, `04` (f775); the dispatch `$AD8E = 03` / `04` .. (f779 / f787). Swept over
16 timings x 4 targets: always one victim -- these Naga-ettes stand > 16 px off every Lucca-target line (the nearest,
cmb5 behind cmb4: 21 px).

**Ported**: battle.c battleAreaC586 (the enemies' C1/C586, now with the PC arm: C1/C78D for `fn` 0) at the tech
dispatch; battleBuildHitSet's groups 0B / 0C for the cursor (the band's bytes put back: the renderer calls it every
frame). The band itself is graded by `ct.exe --sweepband25a3` (8000/8000 byte-exact; 955 cases with $9608 non-zero and
two victims or more) -- before, only the enemies' area 08 ($9608 = 0) had reached it. Checks sweep-band-25a3 and
battle-flame-toss-band-vs-rule-cartridge.

**Open**: no live fixture yet where the cartridge's Flame Toss takes two victims (the band is graded by the sweep);
group 0D (C1/21AF, tech 5A) and the area entries other than C1/C78D / C1/C813 + routines 0 / 4 are not ported.
