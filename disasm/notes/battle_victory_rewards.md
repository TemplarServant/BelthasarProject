# BATTLE VICTORY REWARDS — the XP LEVEL-UP + stat-growth law (the last open reward piece)

Closes the `battle_results_window.md` OPEN "**Level-up / stat growth / tech-learn (`C1/F205`+`C1/F39A`) —
explicitly OUT OF SCOPE**". The reward VALUES (EXP/TP/G table `CC/5E00`), the award law, and the results
message box were already derived + shipped (`battle_results_window.md` + commit `9b718cc` "victory results
window (wave 2)"; check `field-battle-results-reward`). This dossier derives the one remaining vertical: **what
happens when earned XP crosses a level threshold** — the threshold table, the stat-growth tables, and the exact
per-block writes — byte-exact from the ROM (`tools/dis.py`) and **validated against the oracle** with a forced
level-up on two characters.

> **HEADLINE.** At victory, for each surviving PC, the ROM (`C1/F950` -> `C1/F623`) applies the earned XP total
> `$B28C` against the PC's countdown-to-next `block+0x2B` (**NOT** the XP counter `block+0x13`). On each cross it
> **level++ (`block+0x12`)**, reloads `block+0x2B` from the threshold table **`CC/2632`[level×2]**, grows **maxHP
> `+0x5`** (table `CC/258A`) and **maxMP `+0x9`** (`CC/25C2`), and **recomputes the 6 core stats** `+0x0B..0x11`
> as **`stat = base(+0x2F..) + rate(CC/25FA)×(level−1)/100`**. Everything is written to the **battle BLOCK**
> `$5E2D+seat*0x80`; the existing block→sheet writeback (`C1/B442`) carries it to `$2600` so the field menu shows
> the new level + stats. curHP/curMP are **NOT** refilled. Speed (`+0x0D`) never grows (its rate is 0 for all).

Addressing: `dis.py` file offset = `(bank−0xC0)<<16 | addr`; ROM read at runtime uses `(bank&0x3F)<<16|addr`
(HiROM), i.e. bank `CC` → `0x0C0000`. Oracle probe = `ct_probe.exe` (a copy of `build/ct.exe`); WRAM in a bare
`.state` is at file offset `0x45`.

---

## PART 1 — THE ROUTINE CHAIN (ROM, disassembled)

`C1/F950` (victory XP driver, called after the victory scan) iterates the party. Per PC it sets `$B285` = the
PC's **battle-block base** (`$5E2D+seat*0x80`; empirically confirmed — see PART 3, the level-up writers are all
`$5E2D,X`-relative), then:
- **`C1/F908`** — XP counter: `block+0x13 (3B) += $B28C`, carry `+0x15`, clamp `+0x15 ≤ 0x98` / `+0x13 ≤ 0x967F`
  (max XP 9,999,999). *(Already ported — `field.c` `fieldBattleFinish`; unchanged here.)*
- **`C1/F623`** — the per-PC **level-up + growth** orchestrator, re-called by `C1/F950` while `$B28C` (earned) > 0:
  ```
  C1/F623:  JSR C1/F7B9            ; check/apply ONE level threshold
            LDA $B28B / BEQ +      ; $B28B==0 => a level was gained
            JMP C1/F7B8 (RTS)      ; no level -> done
        +:  JSR C1/F871            ; grow maxHP + maxMP
            <inline core-stat recompute, C1/F633..F791>
  ```

### `C1/F7B9` — the level threshold (writers pinned in PART 3)
```
F7B9  LDA #FF / STA $B28B                 ; "no level" flag
F7C1  LDA block+0x1D & 0x80 -> JMP F85C   ; DEAD PC (bit7): skip (fallen PC gets no level-up)
F7CE  LDA block+0x12 / CMP #63 -> maxed   ; level 99 cap: zero $B28C, set $B2AF bit7, done
F7EF  REP; LDA $B28C / CMP block+0x2B / BCC F84F   ; earned XP  vs  NEXT (block+0x2B, u16)
F7FA  (earned >= NEXT: LEVEL UP)
F7FD    STZ $B28B                         ; flag "leveled"
F802    LDA $B28C / SBC block+0x2B / STA $B28C     ; earned -= NEXT   [writer C1:F80E]
F811    LDA block+0x12 / INC / STA block+0x12      ; LEVEL++          [writer C1:F818]
F818    $B2B0,seat |= 0x80                ; per-seat "this PC leveled" flag
F823    cap level at 0x63 (99)
F833    LDA block+0x12 *2 -> X ; LDA $CC2632,X -> block+0x2B          [reload NEXT; writer C1:F847]
F847    $B311,seat++                      ; per-seat LEVELS-GAINED count
F84F  (earned < NEXT):  block+0x2B -= $B28C ; $B28C=0                 [writer C1:F859]
F85F  if level>=99: block+0x2B = block+0x2C = 0
```

### `C1/F871` — HP + MP growth (pair tables)
```
$10=0 ; loop: t = CC258A[id*8 + $10] ; if t < level: $10+=2; loop
       else gain = CC258A[id*8 + $10 + 1] ; block+0x5 (maxHP) += gain ; clamp <= 0x3E7 (999)   [writer C1:F8A4]
(same with CC25C2 -> block+0x9 (maxMP) += gain ; clamp <= 0x63 (99))                            [writer C1:F8EF]
```
= find the **first** `{levelBreakpoint, gain}` pair (id×8, up to 4 pairs) whose breakpoint ≥ the new level.

### `C1/F633` — the 6 core-stat recompute (inline in `C1/F623`)
For each grown stat: `rate = CC25FA[id*7 + rateIdx]`; `prod = rate × (level−1)` (`C1/C90B` mul);
`q = prod / 100` (`C1/C92A` div, `#0x64`); `block[curOff] = block[baseOff] + q`; then clamp all to ≤ `0x63` (99,
loop `C1/F791`). The (baseOff, rateIdx, curOff) tuples (from `LDA $002F,X`/`STA $000B,X` … in `C1/F633..F78E`):

| stat | rate idx (CC/25FA) | base (block+) | current (block+) |
|---|---|---|---|
| Power | 0 | 0x2F | 0x0B |
| Vigor | 1 | 0x30 | 0x0C |
| **Speed** | **2 (= 0 for ALL chars)** | — | **0x0D (never rewritten)** |
| Magic | 3 | 0x32 | 0x0F |
| Hit | 4 | 0x33 | 0x10 |
| Evade | 5 | 0x34 | 0x0E |
| MagicDef | 6 | 0x35 | 0x11 |

Sanity check at level 1 (growth = 0 ⇒ current = base): Crono `+0x0B..0x11 = 05 08 0C 05 08 08 02` equals
`+0x2F,0x30,-,0x32,0x33,0x34,0x35 = 05 08 - 08 08 05 02` for the 6 grown stats — **byte-identical** (`ct_charbase.h`).

---

## PART 2 — THE TABLES (ROM bytes, bank CC; `id` = 0 Crono, 1 Marle, 2 Lucca, 3 Robo, 4 Frog, 5 Ayla, 6 Magus)

### XP-to-next `CC/2632` (u16 LE at `[level×2]`; the value reloaded into `block+0x2B` when you hit `level`)
`L1→L2 = 20, 40, 70, 110, 160, 220, 300, 400, 520, 650, 790, 940, 1100, 1270, 1450, 1640, 1840, 2050, 2270, 2500…`

### Core-stat growth rates `CC/25FA` (id×7 : Power,Vigor,**Speed=0**,Magic,Hit,Evade,MagicDef)
```
id0 Crono : 8C A5 00 1E 26 2A A0      id4 Frog   : 82 A0 00 20 2A 34 A3
id1 Marle : 1E 46 00 1E 24 5A AC      id5 Ayla   : 96 AC 00 48 50 1E 98
id2 Lucca : 1C 50 00 2A 20 64 A8      id6 Magus  : 78 64 00 4E 42 71 AF
id3 Robo  : A0 B2 00 2A 1E 34 82
```

### maxHP growth `CC/258A` and maxMP growth `CC/25C2` (id×8 : `{breakpoint,gain}` pairs)
```
HP id0 Crono: (10,13)(21,15)(48,21)(99,10)   MP id0 Crono: (34,2)(99,1)
HP id1 Marle: (12,11)(23,14)(99,18)(255,0)   MP id1 Marle: (33,2)(99,1)
HP id2 Lucca: (12,13)(24,15)(32,16)(99,18)   MP id2 Lucca: (36,2)(99,1)
HP id3 Robo : ( 7,10)(16,15)(99,20)(255,0)   MP id3 Robo : (28,2)(99,1)
HP id4 Frog : (10,12)(21,14)(29,19)(99,20)   MP id4 Frog : (31,2)(99,1)
HP id5 Ayla : (19,15)(27,17)(46,23)(99,10)   MP id5 Ayla : (34,2)(99,1)
HP id6 Magus: (27,10)(40,28)(45,22)(99,18)   MP id6 Magus: (35,2)(99,1)
```

---

## PART 3 — ORACLE VALIDATION (forced level-up, BOTH CHARACTERS, byte-exact)

Base `saves/battle_re/forest_battle_3hetake.state` (fresh L1 Crono+Marle). Patched the **battle blocks** to level
10 / NEXT 5 (`state[0x45 + 0x5E3F]=0x0A`, `+0x5E58/59 = 05 00` Crono; `+0x5EBF`, `+0x5ED8/D9` Marle). The forest
fight awards **9 XP**, so 9 ≥ 5 ⇒ level 10 → 11.
`ct_probe --monitor "5E3F:1,5E40:3,5E58:2,5E32:2,5E36:2,5E38:7,5EBF:1,…" --from S --input <A-mash> --diff --writer`.

**Discriminating result (why block, not sheet):** a first run patched only the **SHEET** `$2600` (L10/NEXT5) and
left the block at L1 — **no level-up occurred** (block L1, NEXT 20 > 9), and the writeback (`C1:B475`, f1022)
copied the L1 block over the patched sheet. So the reward **reads/writes the BLOCK**, and the writeback carries
block→sheet. This is exactly our engine's `XP→block→writeback` architecture.

**Block-patched run (f859, the level-up frame) — every byte matched the prediction:**

| field (block off) | Crono id0 | writer | Marle id1 | law |
|---|---|---|---|---|
| level +0x12 | 10 → **11** | C1:F818 | 10 → **11** | +1 |
| XP +0x13 | → 9 | C1:F914 | → 9 | +earned |
| NEXT +0x2B | → **786** (0x0312) | C1:F859/F847 | → **786** | reload 790 − leftover 4 |
| maxHP +0x5 | 70 → **85** | C1:F8A4 | 65 → **76** | +HP pair gain (15 / 11) |
| maxMP +0x9 | 8 → **10** | C1:F8EF | 12 → **14** | +MP pair gain (2 / 2) |
| stats +0x0B..0x11 | `05080C05080802` → **`13180C090B0B12`** | C1:F791 | `02060808080808` → **`050D08110B0919`** | base + rate×10/100 |
| B2B0 seat flag | → 0x80 | C1:F823 | → 0x80 | leveled |
| B311 levels | → 1 | C1:F84D | → 1 | count |

Worked example (Crono Power): base `+0x2F`=5, rate[0]=`0x8C`=140, level 11 ⇒ `5 + 140×10/100 = 5+14 = 19`
(`0x13`). MagicDef: 2 + `0xA0`(160)×10/100 = 2+16 = 18 (`0x12`). Speed `+0x0D`=`0x0C` unchanged.
**Writeback** (`C1:B475`, f1110) then copied block→sheet: `$2612`=0B, `$260B`=`13180C090B0B12`, Marle `$2662`=0B,
`$265B`=`050D08110B0919` — the menu-visible sheet. **curHP `$5E30` / curMP `$5E34` stayed constant** through the
level-up (only maxHP grew) — level-up does not heal.

---

## PART 4 — IMPLEMENTATION (SHIPPED, byte-exact vs oracle)

- **`CtBattleLevelUp(mem, rom, romLen, block, id, xpEarned, apply)`** (`src/battle.c`): the whole law above, block-
  relative, reading tables `ROM_XP_THRESH`/`ROM_HP_GROW`/`ROM_MP_GROW`/`ROM_STAT_RATE` (`0x0C2632/258A/25C2/25FA`)
  from `CtBattleRom`. `apply==0` = read-only prediction (levels gained, for the message); `apply!=0` = write block.
- **Apply** (`src/field.c` `fieldBattleFinish`): after the existing XP/TP block application, before the block→sheet
  writeback, `CtBattleLevelUp(..., apply=1)` per surviving PC. The writeback (`memcpy` whole 0x50 block→`$2600`)
  carries the new level/stats to the sheet — the field menu shows them for free.
- **Message** (`src/battle.c` `battleBuildResults` + `src/field.c` `battleResultsRender`): a **`{name} level up!`**
  results line (kind 4, string `CE/EF68`) is appended per PC that gains a level; the name is decoded from the
  `$7E2C23+id*6` CT glyph codes to ASCII. Predicted read-only at victory (`apply=0`) so the line exists before the
  box shows; the block itself is applied at battle-end (block+0x12/+0x2B are untouched between, so predict == apply).
- **Test hook** `CT_BATTLE_FORCELVL=1` (`CtBattleBegin`): stages present PCs to L10/NEXT5 to force the level-up
  headlessly (reproduces the oracle capture). Env-gated, inert unset.
- **Check** `field-battle-levelup-growth` (quick): pins the byte-exact `battle-levelup pc0(g=1 L=11 hp=85 mp=10
  next=786 st=13180C090B0B12) pc1(g=1 L=11 hp=76 mp=14 next=786 st=050D08110B0919)`. Baseline (no FORCELVL) never
  levels (L1 NEXT 20 > earned 9) so `field-battle-results-reward` is unmoved. Determinism verified ×2.

Repro (our engine): `CT_BATTLE_TRACE=1 CT_BATTLE_MASHA=1 CT_BATTLE_FORCELVL=1 build/ct_field.exe --assets
ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram`.

---

## OPEN questions
- **TP / tech-learn** (`C1/F205` → `C1/F39A`, tech tables `CC/26FA`/`CC/276A`, per-char tech lists `CC/0000`+): the
  TP-to-next `block+0x2D` decrement is already applied (`field.c`, `battle_results_window.md`); the **underflow →
  learn-a-tech** path (+ the `{name} … learned …` line `CE/EF74`) is a separate subsystem, **not derived here**.
- **Multi-level from one fight**: unreachable in our states (a single kill's XP can't span two thresholds at these
  levels — after L→L+1, NEXT reloads to a value >> the leftover). The apply LOOP handles it (`C1/F950` re-calls
  `F623` while `$B28C`>0); the message shows **one** "level up!" per PC (matching the boolean `$B2B0` flag). Whether
  a multi-level gain shows one line or repeats is un-captured (minor).
- **The `C1/F7AB` speed guard** (post-clamp `if A < block+0x0D: block+0x0D = 0x32`): never fired for Crono/Marle
  (A ≈ 99 ≥ speed), so Speed stays put (as observed). The exact `A` and trigger are a deep edge case — left as the
  measured behaviour "Speed unchanged".
- **Level-99 cap / stat>99 clamp**: implemented per `C1/F823` (level ≤ 99, NEXT=0 at 99) and `C1/F791/F8A4/F8EF`
  (stats ≤ 99, maxHP ≤ 999, maxMP ≤ 99) but not exercised by a real capture (no ≤3px risk; standard CT caps).
- **Reserve-member XP / 3-PC party**: only the 2-PC forest party captured; each survivor gets the FULL earned XP
  (not divided) — a 3-PC party and reserve-member reduced XP are un-captured (see `battle_results_window.md` OPEN).

## Scratchpad artifacts (paths for the caller; not committed)
`…/scratchpad/`: `ct_probe.exe` (oracle copy); `forest_lvlup_block.state` (Crono+Marle block L10/NEXT5) +
`forest_lvlup_sheet.state` (the sheet-only discriminator); `mash.txt` (the A-mash input).
