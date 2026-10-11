# BATTLE VICTORY — the TP → TECH-LEARN law (the last reward vertical)

Closes the `battle_victory_rewards.md` + `battle_results_window.md` OPEN "**TP / tech-learn
(`C1/F205`→`C1/F39A`) — a separate subsystem, not derived**". Derives, byte-exact from the ROM
(`tools/dis.py`) and **oracle-validated** with a forced forest learn (Crono→Cyclone, Marle→Aura),
the complete path: the per-char TP-countdown model, the CC/26FA / CC/276A / CC/1BEB tables, the
learn law (bitfield write `$2837+charId`, learn-count `$2830+charId`, the countdown reload with the
canonical **double-subtract**), the magic-tech gate, and the `{name} learns {tech}` message.

Addressing: `dis.py` file offset = `(bank−0xC0)<<16 | addr`; ROM read at runtime = `(bank&0x3F)<<16|addr`
(HiROM), i.e. bank `CC` → `0x0C0000`. Oracle = `ct_probe.exe` (= `build/ct.exe`); WRAM in a bare
`.state` at file offset `0x45`; `$7F0000` page (globals) = `glb[]` in the engine (`$7F01E0`=`glb[0x1E0]`).

> **HEADLINE.** At victory, after the per-seat XP level-up (`C1/F950`→`C1/F623`), the ROM calls
> **`C1/F205` once** which loops the 3 seats calling **`C1/F39A`** per present PC (gated on `block+0x02`
> bit4 = "all techs learned"). Per PC: **`block+0x2D` (u16) = TP-to-**next-**tech countdown**; the earned
> TP total (`$B2DB`) is spent against it. When `countdown ≤ earned` the tech is **learned** (`C1/F4DF`):
> **`$2837+charId |= (0x80 >> learnCount)`**, **`$2830+charId`++** (learnCount), and `block+0x2D` reloads
> to the **next** tech's TP requirement `CC/26FA[charId×16 + learnCount×2]`. The tech id learned = the
> char's tech-range start `CC/276A[charId×2]` **+ learnCount** (= `C2/BD40[charId]+learnCount`). A gate
> `CC/1BEB[techId×11]` bit7 (elemental/magic techs) blocks the learn unless the char's magic-unlock flag
> `$7F01E0` bit `charId` is set. Regardless of learning, **`block+0x16..18` (lifetime TP, 3 bytes) +=
> earned**. The learned bit is a **persistent global** (`$7E2837`, just past the 7 char sheets), so the
> **next battle's tech-list staging picks the tech up FOR FREE** (`battleTechBuildList` reads `$2837+cid`).

---

## PART 1 — THE ROUTINE CHAIN (ROM, disassembled)

`C1/F950` (victory reward driver): per seat 0/1/2 does XP (`C1/F908`) + level-up (`C1/F623`), **then
once at the end `JSR C1/F205`** (`1F9E3`) for TP/tech across all seats, then `C1/F9EB` (seat-3/reserve).

### `C1/F205` — the per-seat TP/tech dispatcher
```
1F205  init $B305..$B30E = FF (10 msg slots), $B30F = 0
1F218  for seat = 0..2:
1F21D    LDA $AEFF,seat / CMP #FF / BEQ next         ; absent seat -> skip
1F224    $B3CC = seat×8   ; $B1F4 = seat ; X = seat×0x80 (block offset $00)
1F23E    LDA $5E2F,X (block+0x02) / AND #10 / BNE next  ; ALL techs learned -> skip PC
1F245    $B2D1 = $5E2D,X (block+0 = the char id)
1F24B    JSR C1/F39A                                  ; the per-PC learn
1F258  JSR C1/F2DB (the seat-pair masks of the DUAL/TRIPLE-tech pass -- battle_results_box_timing.md 1.8; unported)
```

### `C1/F39A` — per-PC: spend TP, learn, reload (the loop)
```
1F39A  $30 = $B2DB (save earned-TP pool)
1F39F  loop: $B2DA = FF ; JSR C1/F45F                 ; try to spend toward the current tech
1F3A7  LDA $B2DA / CMP #FF / BEQ 1F419(done)          ; FF = nothing learned this pass -> done
1F3AF  $02 = 0x2830 + charId  (learnCount ptr, WRAM)
1F3BD  $04 = 0x276A + charId×2 (per-char [base,count] ptr, ROM)
1F3CC  LDA $5E2F,X (block+0x02) / AND #10 / BNE done   ; all-learned guard (post-F4DF)
1F3D5  JSR C1/F4DF                                    ; COMMIT: set bit + learnCount++
1F3E7  learnCount = [$2830+charId] (now incremented)
1F3F3  threshold = CC/26FA[charId×16 + learnCount×2]  ; the NEXT tech's TP requirement (u16)
1F3F9  CMP threshold, block+0x2D(leftover) :
1F40D    BCS: block+0x2D = threshold − leftover        ; ($B2DB pool UNCHANGED -> next F45F re-subtracts pool)
1F3FE    BCC: $B2DB = leftover ; block+0x2D = threshold ; (leftover big enough for another immediate learn)
1F417  BRA 1F39F (loop)
1F419  done: $B2DB = $30 (restore pool) ; block+0x16..18 (3B) += $B2DB ; clamp 0x98967F
```

### `C1/F45F` — spend the pool against the current tech (sets `$B2DA`)
```
1F45F  baseIdx = CC/276A[charId×2]                     ; = the tech-range start (C2/BD40[charId])
1F46F  globalIdx = [$2830+charId](learnCount) + baseIdx ; = the tech id
1F478  $2C = globalIdx × 0x0B (C1/C90B mul; 11-byte tech records)
1F482  LDA CC/1BEB[globalIdx×11] / AND #80 / BEQ 1F4A2  ; bit7 = elemental/magic tech -> gated
1F48A    (gated) A = 1<<charId ; AND $7F01E0 / BNE 1F4A2 ; magic-unlock bit charId set -> ok
1F4A0    else BRA RTS                                    ; magic locked -> BAIL (block+0x2D untouched, $B2DA=FF)
1F4A2  $B2AF |= 0x10                                     ; "a learn is pending / show message"
1F4AF  countdown = block+0x2D (u16)
1F4B2  CMP countdown, $B2DB(pool) :
1F4B9    (countdown ≤ pool) LEARN: $B2DA = 0 ; block+0x2D = pool − countdown  (leftover)
1F4D1    (countdown > pool)  block+0x2D = countdown − pool ; $B2DA stays FF
1F4DE  RTS
```

### `C1/F4DF` — commit ONE learn (the bit write)
```
1F4DF  $0A = 0x80 >> learnCount                          ; the bit for this learn slot
1F4EF  [$2837+charId] |= $0A                              ; ** SET THE LEARNED BIT ** (writer C1:F4F9)
1F4F9  $B2B3+seat |= 0x08                                 ; per-seat "learned a tech this battle" flag
1F504  $B3CE[$B3CC++] = learnCount + baseIdx (= tech id)  ; the learned-tech MESSAGE queue
1F517  [$2830+charId] = learnCount + 1                    ; ** learnCount++ ** (writer C1:F520)
1F520  if (learnCount+1) >= CC/276A[charId×2+1](maxCount): block+0x02 |= 0x10  ; all techs learned
1F532  RTS
```

**The double-subtract (canonical, oracle-confirmed §3).** After a single learn, `block+0x2D` = `pool −
origCountdown` (leftover, from F45F). F39A reloads it to `threshold_next − leftover` (BCS, the common
branch) with **`$B2DB` unchanged = pool**, then loops → F45F, which finds `block+0x2D > pool` and does
`block+0x2D −= pool` **again**. Net single-learn result:
`block+0x2D = threshold_next − 2·earned + origCountdown`. This is real ROM behaviour and MUST be replicated.

---

## PART 2 — THE TABLES (ROM bytes, `charId` = 0 Crono … 6 Magus)

### `CC/276A` — per-char `[baseIdx, maxCount]` (2 bytes/char)
`baseIdx` = the char's first single-tech id (== `C2/BD40[charId]`); `maxCount` = 8 for every char.
```
Crono base=0x01 cnt=8   Robo  base=0x19 cnt=8
Marle base=0x09 cnt=8   Frog  base=0x21 cnt=8
Lucca base=0x11 cnt=8   Ayla  base=0x29 cnt=8
                        Magus base=0x31 cnt=8
```

### `CC/26FA` — per-char TP thresholds (8× u16 = 16 bytes/char), indexed `[charId×16 + learnCount×2]`
`block+0x2D` reloads to `CC/26FA[charId×16 + learnCount×2]` after each learn. **The full learn-order +
TP-per-tech table (id : TP : name):**
```
Crono : 01 Cyclone 5   02 Slash 90   03 Lightning 0   04 Spincut 200   05 Lightning2 500  06 Life 400   07 Confuse 800   08 Luminaire 1000
Marle : 09 Aura 10     0A Provoke 50  0B Ice 0        0C Cure 150      0D Haste 250       0E Ice2 400   0F Cure2 600     10 Life2 900
Lucca : 11 FlameToss10 12 HypnoWave60 13 Fire 0       14 Napalm 160    15 Protect 250     16 Fire2 400  17 MegaBomb 600  18 Flare 1000
Robo  : 19 RocketPch 0 1A CureBeam 0  1B LaserSpin 5  1C RoboTackle150 1D HealBeam 400    1E UzziPch 600 1F AreaBomb 800  20 Shock 1000
Frog  : 21 Slurp 10    22 SlurpCut 15 23 Water 0      24 Heal 160      25 LeapSlash 250   26 Water2 400 27 Cure2 600     28 FrogSquash1000
Ayla  : 29 Kiss 10     2A RolloKick60 2B CatAttack100 2C RockThrow 200 2D Charm 400       2E TailSpin600 2F DinoTail 800  30 TripleKick1000
Magus : 31 Lightn2 0   32 Ice2 0      33 Fire2 0      34 DarkBomb 400  35 MagicWall 400   36 DarkMist400 37 BlackHole 900 38 DarkMatter900
```
The **`0` thresholds = techs a char joins already knowing** (they are pre-counted in the join `learnCount`):
Robo joins learnCount=2 (RocketPunch+CureBeam), Frog learnCount=1 (Slurp), Magus learnCount=3. Confirmed
in the forest base state: `$2830` = `[0,0,0,2,1,0,3]` and `$2837` = `[00,00,00,C0,80,00,E0]` — the set-bit
COUNT equals learnCount exactly (Robo 0xC0=2 bits, Magus 0xE0=3 bits), proving `bit = 0x80 >> learnCount`
filling from the MSB.

### `CC/1BEB` — per-tech 11-byte record; **byte0 bit7 = ELEMENTAL/MAGIC gate**
`byte0 bit7` set for the damage-magic + heal/status techs (all need magic unlocked). Set for single-tech
ids: `03 05 06 08 0B 0C 0D 0E 0F 10 11 13 14 15 16 17 18 23 24 26 27` (Lightning, Life, Luminaire, Ice,
Cure, Haste, FlameToss, Fire, Napalm, Protect, Fire2, MegaBomb, Flare, Water, Heal, Water2, Cure2, …).
Clear for the physical/support first techs Cyclone (0x01), Slash (0x02), Aura (0x09), Provoke (0x0A), all
Ayla, Frog physical, etc. When set, `C1/F45F` requires `$7F01E0 & (1<<charId)` (the per-char magic-unlock
bitmask = **0 in the forest era**, so magic techs cannot be learned pre-magic — canonical CT).

---

## PART 3 — ORACLE VALIDATION (forced forest learn, byte-exact)

Base `saves/battle_re/forest_battle_3hetake.state` (fresh L1 Crono+Marle, 3× Hetake alive `$AF02`=5E5E5E,
each awards **TP 1** ⇒ pool **3**). Patched the battle blocks so the countdown is below the pool:
`state[0x45 + 0x5E5A] = 2` (Crono `block+0x2D`) and `state[0x45 + 0x5EDA] = 1` (Marle) — no CRC guard on a
bare `.state`. Drove to victory (A-mash) with
`ct_probe --monitor "2837:2,2830:2,5E5A:2,5EDA:2,5E43:3,B2AF:1,B2B3:2,AF02:3" --from forest_learn.state
--input <A-mash> --frames 2200 --diff --writer`.

**The learn frame f857 — every byte matched the prediction:**

| field | Crono (seat0) | writer | Marle (seat1) | law |
|---|---|---|---|---|
| `$2837+cid` learned bits | 0→**0x80** | **C1:F4F9** | 0→**0x80** | `|= 0x80>>learnCount(0)` (Cyclone / Aura) |
| `$2830+cid` learnCount | 0→**1** | **C1:F520** | 0→**1** | ++ |
| `block+0x2D` TP-next | 2→**86** (0x0056) | **C1:F4DB** | 1→**45** (0x002D) | `threshold_next − 2·earned + origCountdown` |
| `block+0x16` TP total | 0→**3** | **C1:F42D** | 0→**3** | += earned (pool) |
| `$B2AF` | →bit4 | C1:F4AA | (shared) | learn-pending |
| `$B2B3+seat` | →bit3 (0x08) | C1:F5E8 | →bit3 | learned-this-battle |

Worked example — **Crono**: origCountdown 2, earned 3 ⇒ learn Cyclone (leftover 3−2=1); reload Slash
(threshold 90); `block+0x2D = 90 − 1 = 89` (BCS), then 2nd F45F `89 − 3 = 86` ✓. **Marle**: 1, 3 ⇒ learn
Aura (leftover 2); reload Provoke (50); `50 − 2 = 48`, then `48 − 3 = 45` ✓. **The double-subtract is
canonical** (the countdown ends `earned` lower than a naive `threshold−leftover`). `$7F01E0`=0 in the
forest (magic locked) — irrelevant here since Cyclone/Aura/Slash/Provoke are all bit7-clear.

**Persistence + next-battle pickup:** `$2837`=0x8080 held f857→f1288 (the whole post-victory window). The
learned bit is `$7E2837+cid`, a persistent global just past the 7 char sheets (`$2600 + 7×0x50 = $2830` =
learnCount[0..6], `$2837` = learnedBits[0..6]); the battle tech-list staging (`battleTechBuildList`,
`battle.c:1041`, `bits & (0x80>>k)`) and the field menu (`CtMenuTechList`, `menu.c:1209`) both read it —
so the learned tech lists **next battle with ZERO new code**.

---

## PART 4 — THE MESSAGE (`CE/EF74`)

Battle-result templates: pointer table `CE/EF11`, entry [5] → `CE/EF74`. CT-dictionary compressed;
tokens calibrated against the known strings (`EF2D`="Got {0F} experience point(s)!", `EF68`="{name} level
up!"): **`EF`=space, `0x11`={char name}, `0x12`={tech name}, `0x3C`="ea", literals A0-B9=A-Z / BA-D3=a-z**.
`CE/EF74` = `11 EF C5 3C CB C7 CC EF 12 00` = **"{name}"** space **`l·ea·r·n·s`** space **{tech}** →
literally **"{name} learns {tech}"**. (Our results line renders the ASCII approximation "{name} learned
{tech}!" — same treatment as kind-4 "level up!" ≈ `CE/EF68`; the results box string is not pixel-pinned.)

---

## PART 5 — SPEC (for the re-implement lane)

Add `CtBattleTechLearn(mem, glb, rom, romLen, block, charId, tpEarned, apply, learnedIds[])` — a faithful
port of the F39A/F45F/F4DF loop:
- read `countdown=block+0x2D`, `pool=tpEarned`, `baseIdx/maxCount=CC/276A[charId×2]`;
- loop: `learnCount=$2830+charId`; break if `learnCount≥maxCount`; `tid=baseIdx+learnCount`; if
  `CC/1BEB[tid×11]&0x80` and `!(glb[0x1E0] & (1<<charId))` → break (magic gate);
  - `countdown≤pool` → LEARN: `countdown = pool−countdown`; if apply set `$2837+charId |= 0x80>>learnCount`,
    `$2830+charId++`, `block+0x02 |= 0x10` when all learned; record `tid`; reload
    `next=CC/26FA[charId×16 + (learnCount+1)×2]`; `next≥countdown` → `countdown=next−countdown` (pool
    unchanged, double-subtract) else `pool=countdown; countdown=next`;
  - else → `countdown−=pool`; break;
- on apply: write `block+0x2D=countdown`; `block+0x16..18 += tpEarned` (clamp `0x98967F`).
- Tables: `ROM_TECH_TP=0x0C26FA`, `ROM_TECH_LEARN_RANGE=0x0C276A`, `ROM_TECH_META=0x0C1BEB`.
- Call it in `fieldBattleFinish` (replacing the stopgap `block+0x2D -= rewTp; clamp 0`), per surviving PC,
  passing `CtSceneMemGlb(vm)` for the magic gate. Predict (apply=0) in `battleBuildResults` to append a
  kind-5 "{name} learned {tech}!" line per learned tech (name via `CtMenuTechName`).

---

## PART 6 — IMPLEMENTATION ✅ LANDED (`src/battle.c` + `src/field.c`)

- **`CtBattleTechLearn(mem, glb, rom, romLen, block, charId, tpEarned, apply, learnedIds)`** (`battle.c`) —
  a faithful port of the F39A/F45F/F4DF loop (tables `ROM_TECH_TP`/`ROM_TECH_LEARN_RANGE`/`ROM_TECH_META` =
  `0x0C26FA`/`0x0C276A`/`0x0C1BEB`), including the **double-subtract** (the reload keeps `pool` unchanged so
  the next loop pass re-subtracts it) and the **magic gate** (`CC/1BEB` bit7 → `glb[0x1E0]` bit charId).
  `apply==0` predicts (the message); `apply!=0` writes the persistent globals `$2837`/`$2830` + `block+0x2D`
  + `block+0x16` lifetime TP.
- **Apply** (`field.c` `fieldBattleFinish`) — replaced the stopgap `block+0x2D -= rewTp; clamp 0` with
  `CtBattleTechLearn(s, CtSceneMemGlb(vm), …, apply=1)` per surviving PC (after XP/level-up, before the
  block→sheet writeback). The learned bit is a global (not the 0x50 block) so it does **not** ride the
  writeback — it is already persistent; the next battle's `battleTechBuildList` reads it directly.
- **Message** (`battle.c` `battleBuildResults` + `field.c` `battleResultsRender`) — a kind-5 line per learned
  tech (resultVal = `charId<<8 | techId`), rendered "{name} learned {tech}!" (name from `$2C23+id*6`, tech
  name via `CtMenuTechName` = `CC/15C4`). Predicted read-only at victory (agrees with apply). Kind-family
  extension of the shipped kind-4 "level up!" line (same VWF/box seam; the results box isn't a pinned pixel
  check, so the ASCII wording is accepted like kind-4).
- **Test hook** `CT_BATTLE_FORCETECH=1` (`CtBattleBegin`) — stages present PCs' `block+0x2D`=2 so the forest's
  3 TP forces a learn (Crono→Cyclone, Marle→Aura). Env-gated, inert unset (mirrors `CT_BATTLE_FORCELVL`).
- **Check** `field-battle-tech-learn` (quick) — pins `battle-techlearn pc0(n=1 bits=80 lc=1 next=86 tid=01
  list=01) pc1(n=1 bits=80 lc=1 next=46 tid=09 list=09)` (byte-exact vs the oracle `forest_learn2.state`
  both `block+0x2D`=2, `$5E5A`=0056 / `$5EDA`=002E) + `battle-results seq=0:9,1:3,2:30,3:BD,5:1,5:109` (the
  kind-5 message seam). `list=` replays the battle-staging scan on the post-learn `$2837`, proving free
  next-battle pickup; composes with `field-battle-tech-cyclone-menu` (pins `$2837=0x80` → Cyclone lists).
  Determinism ×2. Baseline (no FORCETECH) never learns, so `field-battle-results-reward` is unmoved.

## OPEN questions / boundaries
- **Multi-learn in one victory** (earned spanning ≥2 thresholds, or a `0`-threshold chain) — the loop
  handles it (the BCC branch re-spends the leftover; a `0` next-threshold auto-learns) but is UNREACHABLE
  in the captured states (a single fight's TP ≤ 3 vs the ≥50 second thresholds; the `0` slots are the
  pre-known techs already counted in the join learnCount). The double-subtract + reload are pinned on the
  single-learn case only.
- **Magic-tech learn** (`CC/1BEB` bit7 + `$7F01E0`) — the gate is DERIVED and wired (`glb[0x1E0]`) but not
  oracle-EXERCISED (needs a post-magic-unlock capture; `$7F01E0`=0 in every current battle state, so magic
  techs correctly never learn pre-magic). The exact `$7F01E0` write path (Spekkio / story) is not traced.
- **The message glyph string** — the ROM (`CE/EF74`) literally encodes "{name} learns {tech}" (dictionary-
  compressed); our line is an ASCII approximation ("learned … !"), not pixel-validated (the results box is
  not a pinned pixel check — only the TRACE line is). `$B3CE[]` message queue + `$B2AF`/`$B2B3` flags are
  decoded but only the id-queue is consumed by our renderer.
- **Reserve / 3-PC** — only the 2-PC forest party captured; each survivor is processed independently by
  `C1/F205` (per-seat block), the same as XP.
- **All-techs-learned residual** — when a char learns their 8th (last) tech, both the ROM and our port
  reload `block+0x2D` from an OVER-READ of `CC/26FA[charId×16 + 8×2]` (= the next char's first threshold);
  the ROM then does one more `F45F` pass before the `block+0x02` bit4 gate stops it, our loop breaks at the
  top. The residual `block+0x2D` of a maxed char differs but is NEVER read again (bit4 gates the char out of
  all future `C1/F205` processing). Unreachable (needs ≥1000s of TP) and unobservable — left as-is.

## Scratchpad artifacts (paths for the caller; not committed)
Dir: `%TEMP%/claude/…/9e84b785-…/scratchpad/`: `ct_probe.exe` (oracle copy); `forest_learn.state`
(Crono `block+0x2D`=2 / Marle=1 forced-learn); `rbox*.png`/`rb_*.png` (results-box peeks).

## PART 7 — 2026-09-26: C1/F205 WHOLE, the DUAL and TRIPLE techs included (`ct --sweeptechpass`)

User defect 2026-09-26 #15 (open half): the port never learned a dual or triple tech. PART 6's CtBattleTechLearn was the
single-tech half as a per-seat model; C1/F205 is one routine for the whole party, and the dual/triple half keys on the
party's COMBINATIONS. Ported whole on WRAM (battle.c battleTechPass: F205, F2DB, F39A, F45F, F4DF, F533), graded by
micro-executing C1/F205 on fuzzed battle WRAM, all of WRAM compared: 6000/6000, with single techs learned in 5799 cases,
duals in 3252, triples in 141 (check sweep-tech-pass).

* **C1/F2DB** -- the party masks: `$B2D3..5 = $80 >> $AEFF[seat]` (0 when absent), sorted ascending by three
  compare-and-swaps; `$B2D6 = 3|4`, `$B2D7 = 3|5`, `$B2D8 = 4|5`, `$B2D9 = 3|4|5`.
* **CC:2778** -- 25 records of five bytes, FF-terminated: the members' mask, the first tech, the count, a word pointer to
  the requirements (one byte per member, three per tech). F205 takes, for each of the four combinations, the FIRST record
  whose mask equals it (`$00 = $283E + the record's index` -- the learned bits of that group).
* **C1/F533** -- the members' tech levels ($2830[char], char 0 first) into `$B2E1..`, FF-terminated; per tech of the
  group ($B2DF = $80 >> k, $B304 counting up, the pointer += 3): not learned yet, every member's level >= its requirement,
  and CC/1BEB[tech*11] bit 7 clear -> the bit is set; a 2-member tech goes to `$B305[$B30F++]` and `$B2B3[$B1F4] |= 04`, a
  3-member one to `$B310` and `|= 02`. `$B1F4` is what the single-tech loop LEFT: the last present seat.
* The results sequencer FD/AD8C prints, per seat: level lines (message 4), the seat's techs (5, `$B3CE[seat*8..]`), then
  under the marked seat the duals (6, "Learns Dual Tech, {tech}!", CE/EF80) and the triple (7, "Learns Triple Tech,
  {tech}!"). FD/AD17 resets `$B30F` first.

**Two readings a model misses.** C1/F39A runs C1/F45F once more after a char's last tech -- the all-learned bit is tested
only when F45F reports a learn -- so the countdown takes one more subtraction, against a tech id past the char's range.
And C1/F5B7's magic test is ported but unreachable with the ROM's data: none of the 55 dual/triple techs has bit 7.

**Wiring.** fieldBattleFinish applies the pass once, after the level-ups (C1/F9E3), with the pool `$B2DB-$B2DD` = the TP
the battle earned (FD/ABF6's per-kill sum = the port's rewTp) and `$7F:01E0` from the field's $7F page;
battleBuildResults predicts it on a copy of the WRAM for the results lines. The boot now clears what FD/B758-B767 clears
(`$B2DB-DD`, `$B2AF`, `$B2B0-B5`; A = X = 0 from FD/B732). And the whole reward block is gated as C1/83D2 gates it: a DEMO
battle (`$2989` bit 5) takes no reward at all -- measured: the cartridge's `$2830..` change only at the demo battles'
boots (FD:B32E), never at their victories -- and C1/83D9 skips the items, gold, XP, TP and techs while `$2880` is set.
The port had applied the attract's demo victories' rewards; the old model happened to learn nothing there, the ROM-exact
pass did, and that moved the attract's timing by a frame (the three post-race picture checks) until the gate went in.
