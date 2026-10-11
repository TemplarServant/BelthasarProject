# BATTLE INTERPRETER VERIFICATION — enemy-AI + anim-script + tech-records + elemental-defenses

Verifies four community-documentation leads (`disasm/notes/community_leads.md` AREAS 1-3, 8) against the ROM
(`tools/dis.py`, HiROM flat = `(bank−0xC0)<<16|addr`) and the LakeSnes oracle (`ct_probe.exe --monitor`,
base `gato_press_action.state`; HOLD driver `100:8,0:N` = Crono idles + enemy acts; MASH driver
`100:16,0:16`×N). Every claim is HINT→VERIFIED/CORRECTED/REFUTED with a dis.py address or a monitor line.
**Zero invention.** Method note: `ct_probe --monitor` prints 2-byte regions **byte-swapped** (region `$B1D0`
shown `7BA7` = pointer `0xA77B`); all pointer values below are the corrected little-endian value.

---

# T1 — ENEMY AI TABLE : **VERDICT = CONFIRMED (with corrections)**

## T1.A The format, as the interpreter actually encodes it (all ROM-cited)

| element | ROM citation | verified fact |
|---|---|---|
| **AI pointer table** | `C1/AFD6 LDA $CC8B08,X` (X = id×2, `ASL A/TAX`); id = `$AF02,X` alive-list byte (`C1/8C2D`) | table at **CC/8B08**, indexed **id×2**. GATO id `0x92` → `word[CC/8B08+0x124]` = **CC/A77B** ✓ (L2.1 CONFIRMED) |
| **script data base** | `C1/B4DA LDX #8D08` (in `C1/B4AA`) | **CC/8D08** is both the data-region base AND the **confusion-override script** — if the enemy's status (`block+0x1E/0x21/0x26` bit7, read `C1/B4C1`) is chaos, the script pointer is forced to `CC/8D08` (L2.1 CONFIRMED + extended) |
| **two blocks** | autonomous entry `C1/AFD2` (from script base); when-hit entry `C1/B488` — `LDA $CC0000,X / CMP #FF / BEQ / INX` **scans past the first `$FF`** to land on block 2 | block1 = autonomous, block2 = when-hit, split at first `$FF` (L2.2 CONFIRMED) |
| **conditions = 4 bytes** | eval `C1/AFD2`; advance by 4 (`INX×4` at `C1/B011-B014`); handlers read args via `$CC0001,X..$CC0003,X` | 4-byte conditions, `$FE`-terminated (L2.2 CONFIRMED) |
| **condition dispatch** | `JSR ($B80D,X)` (`C1/B006`, `C1/874E`), X = condByte×2 | jump table **C1/B80D**, **0x30 opcodes** (0x00-0x2F). `23-28` alias to `18` (`C1/95FA`) exactly as L2.3 (CONFIRMED) |
| **action dispatch** | `JSR ($B88D,X)` (`C1/8D88`), X = actByte×2 (opcode → `$AEE3`) | jump table **C1/B88D**, **0x18 opcodes** (L2.4 CONFIRMED) |
| **targeting** | `JSR ($B8BB,X)` (`C1/AC2E`), X = value×2 (`ASL A/TAX` `C1/AC2C`) | table **C1/B8BB** — this IS the community's "**2×value + $01B8BB**" (their bank-01 = our C1) (L2.5 CONFIRMED) |

**Condition opcodes spot-verified** (L2.3): `00` always-true (`C1/8EA7`); **`17` random** (`C1/95DA`: `LDA $CC0001,X`
= prob byte → `LDA #$64 / JSL RNG / BCS =fail` — a `rand<prob` roll) ✓; `1F` distance (`C1/9765`, reads position vars
`$986E/$9870`).

**Action opcodes + byte-lengths** (hand-decoded from the handlers; the loop then continues to the next action /
`$FE`):
| act | handler | length | what |
|---|---|---|---|
| `00` | `C1/99B8` | 1 | wander / idle (no-op) |
| **`01`** | `C1/99BE` | **3** | **ATTACK** `01 p1 p2`: p1 → `$AEE4` (=`$AE93`, the A/B selector), p2 → `$AEE5` (attack index) |
| **`02`** | `C1/9A39` | **4** | **TECH/special** `02 p1 tgt p3`: p1 → `$AEE4` (=`$AE93`) |
| `0F` | `C1/9E63` | 2 | message `0F msg` (peeks `$CC0001,X`, `JSL CD0033`) |
| `03/04/06/0E` | `C1/9B46/9B47/9B8C/9E62` | 1 | RTS/no-op in this ROM (spacing) |
| `10` | `C1/9E78` | 1 | revive |

## T1.B The four decoded scripts (with the confirmed lengths)

```
GATO  id 0x92  CC/A77B
 BLOCK1 (autonomous): [always] -> ATK-A idx5, ATK-A idx0, 06, 06, TECH 0x10 (SONG) tgt5, 00, 00
 BLOCK2 (when-hit):   [cond 1F 06..] -> ATK-B idx6, 00 ;  [always] -> message 03
Hetake id 0x5E  CC/9E36
 BLOCK1: [cond 17 rand(0x10)] -> 00,00,06,04 ;  [always] -> ATK-B idx5, 00, ATK-B idx6, 0E
 BLOCK2: [always] -> 00,00,06,00
Beetle id 0x8D  CC/A68E
 BLOCK1: [always] -> ATK-A idx6, ATK-B idx1, 06, revive?, 00,00,06,04
 BLOCK2: [always] -> 00,00,06,04
AvianChaos id 0x19  CC/9255
 BLOCK1: [cond 17 rand(0x20)] -> TECH 0x1E tgt6, 00, 08 ;  [always] -> 00,00,06, message0, 00,06,0A
 BLOCK2: [always] -> 00,00,06,00
```
Every block terminates `$FF` immediately after an action-`$FE` — a **clean parse** that validates the lengths.
AvianChaos casts a **tech (`02 1E`) gated on a `17` random (prob 0x20)** — its "Chaos" confuse.

## T1.C The `$AE93` rule — as the SCRIPT encodes it (this is the headline result)

`$AE93` is **not a turn-parity counter** — it is the **attack action's first parameter byte** (`p1`), copied
`$AEE4 → $AE93` by the tiny routine `C1/AD02` (`LDA $AEE4 / STA $AE93 / RTS`; writer `C1:AD08`). At anim
dispatch `C1/44AF`: `LDA $AE93 / BNE →44E0` selects **attack-B table `CD/4F26`** ($AE93≠0) vs **attack-A table
`CD/4926`** ($AE93=0).

- GATO's **block-1 (autonomous)** attack is encoded `01 00 ..` → p1=0 → `$AE93=0` → **attack A (`$9877`=0x31)**.
- GATO's **block-2 (when-hit)** attack is encoded `01 01 ..` → p1=1 → `$AE93=1` → **attack B (`$9877`=0x34)**.

So "attack A then B" = **autonomous-swing vs counter-swing**, chosen by which block runs, which depends on
whether GATO was hit since its last turn. **Oracle proof** (MASH, `--monitor "AE93,9877,6130"`):
```
f556  GATO curHP 6130: 76->59 (hit, C1:ED1A)
f557  $AE93 = 01                (C1:AD08)   <- when-hit block fired
f560  $9877 = 0x34 attack-B     (C1:44E7, the $AE93!=0 path)
...   (after a lull, $AE93 -> 0)
f1252 $9877 = 0x31 attack-A     (C1:44BB, the $AE93==0 path)
```

## T1.D The famous never-triggering SONG — found

The song = GATO's **block-1 TECH action `02 10 05 00`** → anim id **0x10** (the 311-frame special, dispatched
via the third path `C1/469A`, writer `C1:AD08`→`$AE93`=0x10). It fires **autonomously when GATO is left un-hit**
(so block 2 never preempts). **Oracle proof** (HOLD, Crono idles in the menu): `f1151 $9877 = 0x10 (C1:469A)`.
It is **not "never-triggering"** — the earlier dossiers only ever captured MASH fights, where every round hits
GATO → block-2 counters → block-1's tech is never reached. Leave Crono alone and GATO sings (turn sequence in
HOLD = attack-A, then TECH 0x10).

**Oracle AI-decode cross-check** (`--monitor "AEE3,AEE4,AEE5,9877,AE93"` + `$B1D2`/`$B273` script pointers,
byte-swap corrected): the fetched bytes (`AEE3`=action-op `01`/`02`, `AEE4`=`00`/`0x10`, `AEE5`=idx) exactly match
GATO's ROM script `CC/A77B` (attack `01 00 05` at +05, tech `02 10 05 00` at +0D). ✓

---

# T2 — ANIM SCRIPTS : **VERDICT = CONFIRMED (opcode table validated; pointer address CORRECTED)**

## T2.A Pointer-table CORRECTION

The ROM's **live** anim-script pointer table (indexed by `$9877`) is **`CD/5FF0`** for attacks
(`C1/4535 LDA $CD5FF0,X → $9878`) and **`CD/61F0`** for tech/special (`C1/46EF LDA $CD61F0,X`). Both point into
**bank CD** scripts. The community's `CD/5EC6` / `CE/0380` (L1.1) is a **real but DIFFERENT table**
(`CD/5EC6[0]=0x0380` → CE/0380 — most likely the tech graphic-EFFECT scripts, not the OBJ animation that drives
`$9877`). **CORRECTED.** The "`$9878` = anim length" claim in `battle_loop §I` is also corrected: `$9878` is the
**script pointer** (its low byte is what `--monitor "9878:1"` showed).

**Live cross-check** (monitored `$9878` low byte vs the ROM table):
```
$9877=0x31 attack-A -> CD/5FF0[0x31] = CD/7A42  (mon $9878=42, writer C1:453C) ✓
$9877=0x34 attack-B -> CD/5FF0[0x34] = CD/7B72
$9877=0x10 special  -> CD/61F0[0x10] = CD/9EDA  (mon $9878=DA, writer C1:46F6) ✓
```

## T2.B Script format + opcode table (L1.2) VALIDATED

Each script = a **multi-track header** (`90` + a bitmask word + one 2-byte pointer per active track; the loader
loop is `C1/4562`, `CPY #0010`) followed by per-track opcode streams in bank CD. Decoding with the L1.2 opcode
table produces **clean parses** (every track ends on `00 Return` / `01 EndTech`), and the key opcodes land exactly
where the hint predicts:

| `$9877` | script | main-track decode (L1.2) | exec-track |
|---|---|---|---|
| `0x00` PC basic | `CD/6E0C` | `72`facing `78`sound `20 02`pause `03 07`playAnim … `50`**ShowDamage** `01`EndTech | **`CD/6E2D`: `24 02` wait → `1E 44` SUPERCMD(the hit) → `06 03` → `00` Return** |
| `0x31` GATO-A | `CD/7A42` | `24 01`wait `20 14`pause(20) `78 46`sound `72`facing `06`anim `20 02` `69`pal `20 02` … `20 0A` `00`Return | pause-sum ≈ **34f** |
| `0x34` GATO-B | `CD/7B72` | `72`facing `20 50`**pause(80)** `06`anim `24 02`wait `20 0F`pause(15) `50`ShowDamage `01`EndTech | pause-sum ≈ **95f** |
| `0x10` SONG | `CD/9EDA` | 11-track header (`F09E,9F0A,9F18,…`) + `78`sound `20 20`pause `50`ShowDamage `1E`super | many effect tracks |

Verified opcode meanings (by position + semantics): `72`=set-facing, `78 sd`=play-sound, `20 dd`=**pause dd
frames**, `03/06 aa`=play-animation, **`1E ss`=PerformSuperCommand (the actual hit)**, `50`=ShowDamage,
`00`=Return, `01`=EndTech. This matches L1.2 — **VERIFIED**.

## T2.C Cross-check against our byte-derived spans — the span EMERGES from the script

The frozen action-span length is **not a table value** — it is the emergent runtime of the anim script (sum of
`20`-pauses + `03/06` animation playback). The explicit `20`-pause totals **order-match the observed spans**:

| action | anim | observed span (`battle_loop §G/§I`) | script pause-sum | ordering |
|---|---|---|---|---|
| PC basic | `0x00` | **125f** | 2f + playback | lowest |
| GATO-A | `0x31` | **157f** | 34f + playback | middle |
| GATO-B | `0x34` | **224f** | 95f + playback | highest |
| GATO song | `0x10` | **311f** | many tracks | highest |

The **`1E 44` super-command sits in the exec-track after a `24` wait** → fires mid-animation = the measured strike
frame where GATO flashes (`battle_actionfx A4`, strike ≈ +26f). Our baked keyframes (`battle_anim.h` ladder,
`battle_fx.h` ShowDamage popup) thus become **fixtures**: `1E`↔strike-flash, `50`↔the damage-number popup.
The exact per-opcode frame durations (the `03/06` playback lengths pull from the sprite-anim subsystem) and the
per-frame bank-CC interpreter dispatch remain the **documented anim-engine boundary**.

---

# T3 — TECH RECORDS + THE ENUM CONFLICT : **VERDICT = CORRECTED (our "type 5" was wrong)**

## T3.A Record bases (all CONFIRMED)

- **Attack/tech damage records `CC/213F`** (12 bytes): **`+5`=formula-type, `+6`=defensive-stat selector,
  `+9`=power**. Record #1 = `CC/214B` (community's `0x0C214B`) ✓. Type-`01` records carry defsel `0x3E`
  (physical); type-`03` records carry defsel `0x3C` (magic-def) — self-consistent with the enum.
- **Enemy attack records `CC/89C6`** (12 bytes): all **type `04`**, defsel `0x3E`; GATO = record #1 (`CC/89D2`,
  power `0x12`=18) — matches the oracle (`$AEEF`=0x12).
- **MP costs `CC/253C`** (1 byte/tech) ✓: `02 02 02 04 08 0A 0C 14 …`.
- **Tech names `CC/15C4`** (11 bytes) ✓: #1 **Cyclone**, #2 **Slash**, #3 **\*Lightning**, #4 **Spincut**,
  #7 **Confuse** (decoded; upper A-Z=0xA0-B9, lower a-z=0xBA-D3).

## T3.B The DamageFormula enum conflict — RESOLVED in ctrando's favor

`$AEEB` (=`$16` at dispatch, `C1/D1A4 LDA $AEEB/STA $16`) is the formula-type. **Oracle-monitored**:
```
f419  Crono basic attack:  $AEEB = 01  $AEEC=3E  $AEEF=08   (writer C1:D5F6, PC record loader)
f557  GATO enemy attack:    $AEEB = 04  $AEEC=3E  $AEEF=12   (writer C1:D94C, enemy record loader)
f869  Marle ranged attack:  $AEEB = 02
```
Dispatch = a branch-chain: `C1/E0F5` `LDA $16 / CMP #01 / BEQ →PC-melee(E0FF) / else JMP E184` → `E184 CMP #02`
(ranged, else E1FC) → `E1FC CMP #03` (magic E203, else JMP E276) → **`E276 CMP #04 / BNE →E2D1`** (falls through to
**`E27B`** = enemy physical, which reads `$5E6A,X` = `A+0x3D`) → `E2D1` (types 5/6). So `$16`=4 lands on E27B.

| `$AEEB` | ctrando enum | routine | our old label | corrected |
|---|---|---|---|---|
| 1 | PC_MELEE | `C1/E0F5` (`A+0x36×12 + A+0x43×5`) | type 1 ✓ | ✓ |
| 2 | PC_RANGED | `C1/E184` (`(A+0x3A+A+0x43)×8`) | type 2 ✓ | ✓ (Marle) |
| 3 | MAGIC | (defsel `0x3C`) | — | ✓ (records) |
| **4** | **ENEMY_PHYS** | **`C1/E27B`** (`A+0x3D×2`) | **"type 5 — enemy physical"** | **CORRECTED → type 4** |
| 5 | PC_AYLA | — | (mislabeled here) | unobserved |
| 6 | MISSING_HP | — | — | unobserved |

**`battle_loop §A` / ADDENDUM A "type 5 — enemy physical → C1/E27B" is REFUTED: GATO's enemy physical is
type `4`.** The E27B *formula* (`$AEEF × A[+0x3D]×2 / 18`) is correct; only the type NUMBER was wrong. The
ctrando `DamageFormula` enum (ENEMY_PHYS=4, PC_AYLA=5) is **VERIFIED**.

---

# T4 — ELEMENTAL DEFENSES : **VERDICT = CONFIRMED**

- **Block offsets `+0x3F..0x42` = Lightning / Shadow / Water / Fire defense** (L8.2 element order).
- **ROM source = enemy record `CC/4700 + id×23`, bytes `rec+16..19`**, copied to `block+0x3F..0x42` by
  **`FD/B438`** (`LDA $AF0D,X` id → `id×23` via `ASL×3 / ADC / SBC` → `LDA $CC4700,X / STA $5FB0,Y …`). CONFIRMED.
- Scenario enemies (all **neutral**): GATO / Hetake / Beetle / AvianChaos = **`04 04 04 04`**; physical DEF
  `+0x3E` = **`0x7F`** (127) for all four. **Live block read** (GATO `$616C:4`) at f288 = `04 04 04 04`,
  `$616B`=`0x7F` — the ROM record reaches the live block ✓. (This also resolves `battle_scenario2`'s doubt:
  `+0x3E`=0x7F **IS** the physical DEF — `battle_loop` ADDENDUM A proved it by patch, and 33·(256−127)/256 ≈ 16
  matches Crono's observed damage.)
- The field **is meaningful** — a ROM scan finds non-neutral enemies: id `0x17` Fire=`0x80`, id `0x20`
  Lit/Shd=`0x40`, id `0x22` Lit=`0x02`, id `0x2B` Shd=`0x84`. `0x04` = neutral baseline; the **`0x80` high bit**
  = absorb/immune (e.g. a fire-elemental with Fire=`0x80`); `<0x04` = weakness.
- **Boundary:** the exact element→byte order + the (256−def)/102.4 magic reduction can't be disambiguated from
  these all-`0x04` enemies; needs a documented-affinity enemy + the (still-underived) magic-damage path (L6.2).

---

# PART 2 — INTERPRETER PORT SPEC (skeleton)

Both interpreters replace pinned tables in `src/battle.c` with genuine bytecode execution. **All addresses above
are the port contract.** The current `battle.c` fakes GATO's AI ("physical-attack lead PC") and pins the freeze
spans (157/224/125) — an interpreter makes both EMERGE.

## P2.A ENEMY-AI interpreter

**State (per enemy slot):** `scriptBase` (`CC/8B08[id×2]`), `sectionPtr` (`$B273`), `actionCursor` (`$B1D4,Y`,
persists across turns), `condMatch` (`$AF24`), `$AE93` (attack A/B selector), `$AEE3/E4/E5` (decoded action).

**Per turn (enemy ready):**
1. `id = aliveList[slot]`; `base = word[CC/8B08 + id×2]`. If status-chaos → `base = CC/8D08`.
2. Choose block: **autonomous** (from `base`) normally; **when-hit** (scan past first `$FF`) if damaged since last
   turn. (This is what drives `$AE93`.)
3. Walk sections: for each, evaluate its 4-byte conditions via the **cond table (0x00-0x2F)** — dispatch on
   opcode; `00`=always, `17`=`rand<prob`, etc.; ALL must hold; take the **first satisfied** section (`$FE`-delimited).
4. Run the next action from that section's list (cursor advances per turn, wraps at `$FE`): dispatch on the
   **action table (0x00-0x17)** — `01`=attack (`01 p1 idx`: `p1→$AE93`, `idx→$AEE5`), `02`=tech (`02 p1 tgt m`),
   `0F`=message, etc. Resolve target via the **targeting table (2×val + `C1/B8BB`)**.
5. Hand `$AE93`/`$AEE5` to the damage + anim layers.

Porting this makes GATO's autonomous-A / counter-B / song fall out **for free** (and Hetake/Beetle/Avian, and any
enemy) instead of the hardcoded "attack lead PC".

## P2.B ANIM-SCRIPT interpreter (and the timing contract)

**State (per anim, up to 8 tracks):** `trackPtr[8]` (from `CD/5FF0[$9877×2]` / `CD/61F0` for tech, into bank CD),
per-track `frameTimer`, current sprite frame, palette/facing/priority.

**Per render frame, per active track:** read opcode, dispatch (L1.2 table 0x00-0xDA): `20 dd`=hold dd frames;
`03/06 aa`=play sprite-anim `aa`; `1E ss`=execute the hit (damage apply); `50`=spawn ShowDamage popup; `78`=sound;
`72/73/74`=facing/priority; `00/01`=end track. The action ends when all tracks end.

**Timing contract with the `$A013` freeze law** (`battle_loop §E-I`): the frozen span currently pinned at
157/224/125f should instead **EMERGE = the number of render frames the anim script takes to run to end-of-all-tracks**
(`$A013`=1 for exactly that duration; the ATB is frozen throughout). Replace the `CT_ENEMY_SPAN`/`copyFreeze`
constants with "run the script; the span is however long it took." The `1E` super-command's frame position gives
the strike/HP-apply timing automatically (no separate pinned strike offset).

## P2.C Fixture strategy (our bakes become verification targets)

Our byte-exact bakes are now **regression fixtures** for the future interpreters:
- **AI:** GATO must select attack-A un-hit / attack-B when-hit / song when left alone (the `$AE93` chain above);
  Hetake/Beetle just attack; AvianChaos casts tech `0x1E` on a `17` roll. Pin via `--monitor "AE93,9877,AEE3"`.
- **Anim:** the interpreter's emergent span must reproduce **125f (0x00) / 157f (0x31) / 224f (0x34) / 311f (0x10)**
  (`battle_loop §G`), the `1E`-super must land on the baked strike keyframe (`ctCronoAttackTraj` f473-499 A4,
  `battle_anim.h`), and `50`-ShowDamage must fire at the baked popup birth (`ctBattleFxRiseDY`, `battle_fx.h`).
- **Damage:** formula-type `$AEEB` must be **1 (PC melee) / 2 (ranged) / 4 (enemy phys)** into `E0F5/E184/E27B`;
  elemental defs load `04 04 04 04` for GATO/Hetake (`+0x3F..0x42`).

Any interpreter that reproduces these fixtures is byte-correct against the oracle.

---

# RESIDUAL BOUNDARIES (flagged, not invented)
- **Per-turn action-cursor micro-mechanics** (sequential vs `04`-random-selector; the exact `$B1D4,Y` advance +
  wrap) — the AI-interp internals; the SCRIPT CONTENT is decoded, the per-turn pick order is the residual.
- **Rare action/condition opcode semantics** (`06/08/0A/0E`, cond `1F` distance math) — appear inert/positional
  in the four scripts; substantive ops (attack/tech/message/targeting/random) are pinned.
- **Anim per-opcode frame durations** (`03/06` playback pulls the sprite-anim subsystem) + the per-frame bank-CC
  opcode interpreter dispatch — the standing anim-engine boundary.
- **Magic-damage path + element→byte order** (L6.2) — the all-`0x04` scenario enemies can't disambiguate; needs a
  documented-affinity enemy.
