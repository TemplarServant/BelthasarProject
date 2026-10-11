# BATTLE VICTORY RESULTS — the reward window + XP/G/TP/item values & application (user report)

Derives the **victory RESULTS presentation + rewards** the user reports missing:
*"at the end of the battle (on winning) it should show some boxes at the top indicating what you
won (items, gold, xp)"*. Closes the `battle_hud_render.md`/`battle_loop.md` "results window" OPEN.

Every claim is anchored to a ROM disassembly address (`tools/dis.py <fileoffset>`, fileoffset =
`(bank−0xC0)<<16 | addr`) or an oracle observation (`ct_probe.exe --monitor --writer` for RAM timing /
`--peek` for the rendered window — the **peek≠monitor timeline** gotcha applies: RAM values/writers below
are `--monitor`; window pixels/text are `--peek`, aligned by event). Writer PCs = the PC **after** the store
retires. Bases: `saves/battle_re/forest_battle_3hetake.state` (3× Hetake, the NORMATIVE normal-fight capture)
and `gato_lowhp.state` (GATO 19HP — the silver-points SPECIAL). Decode conventions as `battle_hud_render.md`
(fb 512×480 [pad,B,G,R], **our-render-y = oracle-fb-y − 8**).

> **HEADLINE.** The CT victory results = a **single top-of-screen MESSAGE BOX** that shows a **sequence of
> reward lines**, each held until the player presses **A**: `Got N experience point(s)!` → `Got N tech
> point(s)!` → `Found NG!` → `Got 1x <item>!` (→ level-up / tech-learned per PC when applicable). Zero /
> absent lines are **skipped**. The reward VALUES come from a dedicated ROM table **`$CC:5E00`, 7 bytes per
> enemy** (id×7): `+0 XP(u16) +2 Gold(u16) +4 drop-item +5 charm-item +6 TP(u8)`. Rewards are **accumulated
> per-kill inside the enemy-death handler `FD/ABA2`** and **applied at victory** to the PC blocks / gold /
> inventory, then the block→sheet writeback (`C1/B442`, `battle_loop.md` ADDENDUM) carries XP/TP to the sheet.

---

## PART 1 — THE VISUAL (the results window)

### V1 — geometry + chrome (peek `sw_260`, forest, XP line showing)
A **top-anchored message box, full screen width, ~renderY 8–30 (≈3 text lines)**:
- renderY **8** top border `{173,181,181}`; renderY **9** solid **white line `{255,255,255}`** (full width);
- renderY **11–25** blue-grey gradient body `{99,107,115}`→`{24,33,41}` (same colour family as the HUD strip
  chrome, `battle_hud_render.md` Q1 — a standard CT dialogue/message window);
- renderY ~**29** bottom border `{57,66,66}`; **white glyph ink `{255,255,255}`** on rows ~15–22.

It is the **standard CT message/dialogue window** (the field textbox engine, memory `ct-field-textbox`), NOT
the HUD-strip tile-font: the text is **NOT** in the `0x100+code` tile map (a search for the "Got" tiles
`1A6 1C8 1CD` in WRAM+VRAM returned NOTHING) — it is the dialogue engine's **variable-width font composited
into tiles**. The bottom HUD strip (enemy-name phase, "Hetake" + party HP/MP) stays visible **beneath** the
results box the whole time.

### V2 — the message SEQUENCE (advance on A; zero/absent lines skipped)
Driven `--peek forest … + A-taps` (`sw_260`, `adv_1..4`) and `gato_lowhp … + A-taps` (`G_g1..g3`). Each line
**holds indefinitely until A** (idle sweep `sw_260/360/500` = f1160→f1400 all held the XP line → it does NOT
auto-advance). Order = the ROM template order (§V3):

| # | line (rendered, forest) | value source | shown iff |
|---|---|---|---|
| 1 | **`Got 9 experience point(s)!`** | XP total `$B28C` | XP > 0 (essentially always) |
| 2 | **`Got 3 tech point(s)!`** | TP total `$B2DB` | TP > 0 |
| 3 | **`Found 48G!`** | Gold total `$B2A5` | **Gold > 0** (SKIPPED for GATO, GP=0) |
| 4 | **`Got 1x Tonic!`** | each drop in `$B2A7[]` | **a drop present** (SKIPPED for GATO, no drop) |
| 5 | *(per-PC)* `{name} level up!` / `{name} … learned …` | XP/TP crossed a threshold | a level-up / tech learned — **OPEN** |

- **Forest (normative, 3× Hetake = XP9 / TP3 / G48 / one Tonic):** 4 lines (1–4), all present.
- **GATO (special, XP10 / TP1 / GP0 / no drop):** only **2 lines** — `Got 10 experience point(s)!` then
  `Got 1 tech point(s)!` — then the sequence **ends** (gold+item lines skipped). GATO's silver points are a
  **separate FIELD reward** (`$7F0052`=0x0F via the field event script op-0x58/0x5B, `battle_loop.md` §S5),
  applied AFTER the battle module returns — the battle STILL gives GATO's table XP10/TP1 (§P3.4).

### V3 — string provenance (ROM)
Battle-result message templates live in **bank CE at `CE/EF2D`** (file `0x0EEF2D`), pointer table at
**`CE/EF11`** (file `0x0EEF11`; 2-byte entries → EF2D, EF44, EF56, EF5E, EF68, EF74, …). CT-dictionary
compressed with substitution codes (`{0F}`=numeric value, `{11}`=char name, `{1F}`=item name). Decoded heads:
- `0x0EEF2D`: **"Got {0F} experience point(s)!"**
- `0x0EEF44`: **"Got {0F} tech point(s)!"** ; `0x0EEF56`: **"Found {0F}G!"**
- `0x0EEF5E`: **"Got 1x {1F}!"** ; `0x0EEF68`: **"{11} level up!"** ; `0x0EEF74`: **"{11} … learn …"**

The item name (`{1F}`, e.g. drop `0xBD` → **"Tonic"**) resolves through the standard item-name table (memory
`ct-field-item-menu-use`).

### V4 — timing (peek, forest; ordering)
- Victory (all enemies removed, `$AF02`→all-FF via `C1:B2C3`) ≈ **f1060**; first message (`Got N experience`)
  ≈ **f1100**; each line holds until A.
- The reward **application to RAM is decoupled from the message display** (cosmetic sequencing): gold `$2C53`
  and the PC-block XP/TP land in one burst ≈ **f1093** (right after the victory scan), the item + block→sheet
  writeback ≈ **f1337**, field handoff (`$95D5`→0x80 `C0:A730`) ≈ **f1442**. So an engine may apply ALL
  rewards at victory and merely *show* the sequence.

---

## PART 2 — THE VALUES (the ROM reward table)

### P2.1 — table `$CC:5E00`, 7-byte records, id×7  (byte-verified)
Loader/accumulator `FD/ABD2` (clean disasm): record index `X = id×7` (via `JSL C1/FDBF`=`C1/C90B` multiply
with `#0007`, `FD/ABC1`), then:
```
FD/ABD4  LDX $06                 ; X = id*7
FD/ABD6  LDA $CC5E00,X / ADC $B28C / STA $B28C   ; +0..1 = XP   (u16)  -> total $B28C   [writer FD:ABE1]
FD/ABE1  LDA $CC5E02,X / ADC $B2A5 / STA $B2A5   ; +2..3 = GOLD (u16)  -> total $B2A5   [writer FD:ABEC]
FD/ABEF  LDA $CC5E06,X / ADC $B2DB (carry INC $B2DD) / STA $B2DB  ; +6 = TP (u8) -> total $B2DB [FD:AC05]
FD/AC08  LDA $CC5E04,X / BEQ skip / STA $10       ; +4 = DROP item id (u8)
FD/AC10  CMP $CC5E05,X …                          ; +5 = CHARM item id (u8)   -> drop list $B2A7[] (§P3.2)
```
**Record layout (7 bytes):** `+0..1 XP (u16 LE) · +2..3 Gold (u16 LE) · +4 drop-item id · +5 charm-item id ·
+6 TP (u8)`. (Distinct from the 23-byte enemy STAT record `$CC:4700`, `battle_boot.md` FD/B438 — that one is
HP/level/combat-stats only, no rewards.)

### P2.2 — verified records (`--read rom:` + ROM file cross-check)
| enemy | id | record bytes | XP | Gold | drop | charm | TP |
|---|---|---|---|---|---|---|---|
| **Hetake** | 0x5E → CC/6092 | `03 00 10 00 BD 00 01` | **3** | **16** | 0xBD (Tonic) | 0x00 | **1** |
| **GATO** | 0x92 → CC/61FE | `0A 00 00 00 00 C9 01` | **10** | 0 | 0x00 | 0xC9 | **1** |
| Beetle | 0x8D | `03 00 0F 00 00 00 01` | 3 | 15 | — | — | 1 |
| Avian Chaos | 0x19 | `04 00 12 00 00 00 01` | 4 | 18 | — | — | 1 |
| (id0) | 0x00 | `F8 00 00 00 00 4F 1E` | 248 | 0 | — | 0x4F | 30 |

**Cross-check vs the forest capture:** 3× Hetake ⇒ XP 3·3=**9**, Gold 16·3=**48**, TP 1·3=**3**, drop 0xBD
(deduped to one) — matches the displayed `Got 9 experience` / `Found 48G!` / `Got 3 tech` / `Got 1x Tonic!`
**exactly**. GATO ⇒ XP **10**, TP **1**, Gold 0, no drop — matches `Got 10 experience` / `Got 1 tech` only.

---

## PART 3 — THE APPLICATION (accumulate per-kill → apply at victory → writeback to sheet)

### P3.1 — accumulate PER KILL, inside the enemy-death handler `FD/ABA2`
`FD/ABA2` is the enemy-death handler reached via `JSL FD/ABA2` from the clamp death path (`C1/ED04`,
`battle_loop.md` S3/S5 — the same handler that clears the alive-list `$AF02[slot]=0xFF` at `C1:B2C3`). It
reads the dead enemy's `$CC:5E00` record and **adds to running fight totals** (§P2.1):
- **XP** → `$B28C` (u16, carry `$B2A6`?/handled) — writer `FD:ABE1`;
- **Gold** → `$B2A5` (u16) — writer `FD:ABEC`;
- **TP** → `$B2DB` (u16, carry `$B2DD`) — writer `FD:AC05`.

Oracle (forest, `--monitor --writer`): each Hetake death (f377/f574/f980) bumps `$B28C` +3 (→3→6→9),
`$B2DB` +1 (→1→2→3), `$B2A5` +16 (→48). GATO death (f950): `$B28C`+10, `$B2DB`+1, `$B2A5`+0.

### P3.2 — the drop-item collection (deduped list `$B2A7`)
`FD/AC08+`: if `rec+4` (drop) ≠ 0, and `rec+4 != rec+5` (charm), gated on `$AE5D,slot` **bit6** + a
`rand(0..99) < 0x5A (90)` roll (`LDA #64 / JSL C1/FDCB` = RNG), the drop id is stored into the **drop list
`$B2A7[0..2]`** with a **dedup scan** (identical drops from multiple enemies collapse to one entry, max 3
unique). rec+5 = the **Charm** item (Ayla/Charm ability path). **The exact drop-rate / charm semantics is a
minor OPEN** (§OPEN); the drop ID (`rec+4`) and the list mechanism are pinned.

### P3.3 — apply at VICTORY (driver `C1/F950`)
After the victory scan, `C1/F950` iterates the 3 PC seats (blocks `$5E2D` / `$5EAD` / `$5F2D`), each gated on
a presence/eligibility byte (`LDA $AF00 … BMI skip` per seat; and `C1/F205` re-checks `$5E2F,X` = block+2
**bit4** before applying) and applies to **each SURVIVING PC** (dead/absent seats skipped — so a fallen PC
gets NO reward, canonical CT; exact gate = OPEN §OPEN):
- **XP** — `C1/F908`: `block+0x13 (3-byte XP) += $B28C`; carry → `block+0x15`; clamp `block+0x15 ≤ 0x98`
  (max XP ≈ 9,999,999). Writer **`C1:F914`**. **Each PC gets the FULL sum** (NOT divided — both Crono &
  Marle got +9 / +10).
- **TP** — `C1/F4C1`: `block+0x2D (tp-to-next, u16) −= $B2DB` (the earned TP is SUBTRACTED from the
  count-down-to-next-tech). Underflow → the learn-tech branch (`C1/F4D1`). Writer **`C1:F4DB`**.
- **Gold** — add-gold `C1/D0A2` (amount in Y `$AEF7` = `$B2A5`): `$2C53 (24-bit LE) += amount`, carry →
  `$2C55`, clamp `$2C55 ≤ 0x98` (max gold ≈ 9,999,999). Writer **`C1:D0B6`**.
- **Item(s)** — add-item `C1/D010` (id in Y): find a free/matching inventory slot → `$2400,X = id` (ids),
  `$2500,X = count` (parallel counts, `INC`, cap 0x63=99). Writer **`C1:D03D`**. (Both `C1/D0A2` /`C1/D010`
  are the GENERIC give-gold / give-item helpers, also used by the field event VM.)
- **Level-up / tech-learn** — `C1/F205` (+`C1/F39A`) reads the char record `$CC:0000`, the XP/TP thresholds
  and the growth tables, raises the level + stats + learns techs, and emits the per-PC `level up!` / `learned`
  messages. **This subsystem is OPEN (out of scope, §OPEN).**

### P3.4 — the block XP/TP ride the writeback to the char sheet (already ported)
`block+0x13` XP and `block+0x2D` TP-to-next are written into the block at victory (§P3.3), then the
**block→sheet writeback `C1/B442`** (writer **`C1:B475`**, `battle_loop.md` ADDENDUM "BATTLE-END HP/STAT
WRITEBACK") copies the whole 0x50 block → `$7E2600 + id*0x50`, carrying XP (sheet+0x13) and TP-to-next
(sheet+0x2D) into the persistent char sheet the X-menu reads. Oracle (forest): sheet `$2613`/`$2663` 0→9,
`$262D`/`$267D` 5→2 / 10→7, all writer `C1:B475` at f1337. Gold `$2C53` and inventory `$2400/$2500` are the
persistent field stores directly (no writeback needed).

### P3.5 — application ADDRESS summary
| reward | ROM src | accum (per-kill) | applied to | when / writer |
|---|---|---|---|---|
| XP | `CC/5E00+id*7 +0` u16 | `$B28C` (FD:ABE1) | PC `block+0x13`(3B) → sheet `+0x13` | C1:F914 → writeback C1:B475 |
| Gold | `+2` u16 | `$B2A5` (FD:ABEC) | `$7E2C53` 24-bit LE | C1:D0B6 |
| TP | `+6` u8 | `$B2DB` (FD:AC05) | PC `block+0x2D` tp-to-next → sheet `+0x2D` | C1:F4DB → writeback C1:B475 |
| drop item | `+4` u8 | `$B2A7[0..2]` | `$7E2400` ids / `$7E2500` counts | C1:D03D |
| charm item | `+5` u8 | (Charm path) | — | OPEN |
| level/tech | char rec `$CC:0000` + thresholds | — | block stats / techs | C1/F205 — OPEN |

---

## PART 4 — IMPLEMENTATION SPEC (for the re-implement lane; NOT applied here)

Our engine (`src/battle.c`) currently ends the fight at the victory hold with **no reward computation or
results window** — `fieldBattleFinish` (`src/field.c`) does the HP/MP block→sheet writeback (§P3.4, already
ported) but never adds XP/G/TP/items. The spec:

**S1 — reward VALUES (data).** Bake or read the reward table **`CC/5E00`, 7-byte records** (a
`tools/_gen_*` bake of `{xp u16, gold u16, drop u8, charm u8, tp u8}[256]`, or read from `CT_ROM` like the
stat table). Look up `rec = table[enemyId]`.

**S2 — ACCUMULATE per kill.** In the enemy-death path (our `battleRemoveDead`, `battle.c` — the `$AF02[slot]
=0xFF` equivalent, `battle_loop.md` §D), add `rec.xp`→`h->xpTot`, `rec.gold`→`h->gpTot`, `rec.tp`→`h->tpTot`,
and push `rec.drop` (if nonzero) into a deduped `h->drops[3]`. (Drop-rate RNG gate = OPTIONAL, OPEN — a
byte-faithful port applies the drop always or with the `rand<0x5A` gate; state which.)

**S3 — APPLY at victory** (in `fieldBattleFinish`, BEFORE the existing block→sheet writeback, mirroring
`C1/F950`): for each **surviving** PC seat (skip dead — block+0x1D bit7 / the eligibility gate §P3.3):
`block[+0x13] (3B) += xpTot` (clamp ≤0x98xxxx); `block[+0x2D] (u16) -= tpTot`. Then `$2C53 (24-bit) += gpTot`
(clamp `$2C55 ≤ 0x98`); for each drop, add to `$2400`/`$2500` (find free/matching slot, count cap 99). The
existing writeback then carries the block XP/TP to the sheet `$2600+id*0x50` (unchanged). Each survivor gets
the FULL xpTot/tpTot (not divided).

**S4 — RENDER the results window** (during `victoryHold`, after the fixed celebration `CT_BATTLE_VICTORY_
CELEBRATE`, `battle.c`): draw a **top message box** (renderY ~8–30, full width, the dialogue-window chrome +
variable-width font — reuse the field textbox renderer, memory `ct-field-textbox`) showing the line sequence
`Got {xpTot} experience point(s)!` → `Got {tpTot} tech point(s)!` (iff tpTot>0) → `Found {gpTot}G!`
(iff gpTot>0) → `Got 1x {itemName}!` per drop → (level-up/tech messages — OPEN). **Advance each line on the
A-edge** (the same `battleAReleased`/edge machinery as the command confirm, `battle_loop.md` ADDENDUM T1);
hold until A. Strings byte-cited at `CE/EF2D` (§V3). Then hand to the field (existing handoff).

**S5 — integration points (files):** `battle.c` victoryHold → the message-sequence driver + the A-advance
gate; `field.c fieldBattleFinish` → the reward application (S3) folded in right before the block→sheet
`memcpy` writeback (line ~2619); the reward table data → a new `src/battle_reward.h` (or read via `CtBattleRom`
like the stat table). GATO note: the battle applies its table XP10/TP1 like any enemy; the 15 silver points
are the field VM's own op-0x58 reward (already the field's concern) — do NOT special-case GATO in the battle
module.

---

## OPEN questions
- **Level-up / stat growth / tech-learn (`C1/F205` + `C1/F39A`)** — the whole post-XP growth subsystem: XP
  threshold (sheet `+0x2B` xp-to-next; ROM thresholds `0x01F83E`), stat-growth tables (`0x01F659`/`0x01F886`),
  TP-threshold / tech-learn (`0x01F3F4`, sheet `+0x2D`). Emits the `{name} level up!` / `learned` messages.
  **Explicitly OUT OF SCOPE** (a large separate subsystem); not derived here. The reward values feed it but
  it is not required to show the item/G/XP boxes the user asked for.
- **Drop-rate & Charm semantics** (`FD/AC08+`): the `$AE5D,slot` bit6 + `rand(0..99)<0x5A(90)` gate on the
  drop, and the `rec+5` charm item's Charm-ability flow. The drop ID + list mechanism are pinned; whether the
  rate is a universal ~90% or the flag makes it deterministic, and the charm path, are un-decoded (captures
  were confounded by peek-timing — only the post-writeback frame shows the item). Minor.
- **Dead-PC reward exclusion — exact gate.** `C1/F950` gates each seat on `$AF00/$AF01` (bit7) and `C1/F205`
  on `$5E2F,X` (block+2 bit4); a fight with a FALLEN survivor (Crono dead, Marle alive) was not captured to
  confirm the dead PC gets 0 XP. Canonical CT = dead PCs get no XP; the gate byte is one of those two.
- **Results-message micro-timing / pre-messages.** Whether a "Victory!" or per-PC pre-line precedes XP (none
  observed for these 2 fights); exact A-advance vs any auto-advance dwell; the dialogue-engine window-slide +
  typewriter of the results box (the field textbox subsystem, per-frame not captured here).
- **XP division / party size.** Confirmed each surviving PC gets the FULL sum for a 2-PC party; a 3-PC party
  and the reserve-member XP (CT gives reserve members reduced/зero XP in later game) is un-captured.
- **Edge cases:** >99-item stacks, XP/gold overflow past the 0x98xxxx clamp, an XP=0 or TP=0 enemy's line
  gating (only nonzero observed).

---

## Verify / repro (oracle copy `ct_probe.exe`)
- **Values:** `ct_probe --read "rom:CC/6092+7" --from saves/battle_re/forest_battle_3hetake.state` → Hetake
  `03 00 10 00 BD 00 01`. Table base `CC/5E00`, record = id×7.
- **Application (forest):** `ct_probe --monitor "2C53:3,2613:3,2663:3,262D:2,267D:2,2400:20,AF02:4" --from
  saves/battle_re/forest_battle_3hetake.state --input <mash> --frames 2600 --diff --writer` → gold +48
  (C1:D0B6 f1093), XP/TP writeback (C1:B475 f1337), item 0xBD (C1:D03D). Accumulator: `--monitor
  "B28C:2,B2A5:2,B2DB:2" … --writer` → per-kill +3/+16/+1 (FD:ABE1/FD:ABEC/FD:AC05).
- **Window (forest):** `ct_probe --peek saves/battle_re/forest_battle_3hetake.state OUT --btnscript
  "<mash×15>,0:260"` → `Got 9 experience point(s)!`; append `,100:6,0:160` per A-tap → TP → `Found 48G!` →
  `Got 1x Tonic!`. **GATO:** same on `gato_lowhp.state` → `Got 10 experience point(s)!` → `Got 1 tech
  point(s)!` (no gold/item line).

## Scratchpad artifacts (paths for the caller; not committed)
Dir: `%TEMP%/claude/…/e5608df4-…/scratchpad/results/`
- Renders: `sw_260.png` (XP line), `adv_montage.png` (forest TP/gold/item lines), `gato_seq.png` (GATO
  XP/TP), `fo_15.png`; raw `*.raw` + `.wram/.vram/.oam` sidecars; held state `fo_15.state`.
- Decoders: `dec.py` (wram state), `txt.py` (tile-map text scan), `find.py` (glyph search), `render.py`
  (fb→PNG). `ct_probe.exe` (oracle copy) in `scratchpad/`.

---

## ADDENDUM (Wave 2 implementer — IMPLEMENTED)

**SHIPPED (src/battle.c + src/field.c):** the full reward VALUES + accumulation + application + the results
message box.

- **Reward table:** READ from ROM at runtime (`ROM_REWARD_TBL = 0x0C5E00`, `rec[id*7]`) rather than a baked
  `src/battle_reward.h` — a deliberate deviation from S1's "bake a header", chosen for consistency with the
  existing runtime ROM-table reads in battle.c (`ROM_ENEMY_STATS` CC/4700, `ROM_RELOAD_TBL`, `ROM_RNG_TBL`,
  etc.). Equally canonical (single source of truth = the ROM); a bake would be a redundant copy that can drift.
- **Accumulate per-kill** in `battleApplyDamage`'s enemy-death path (the `$AF02[slot]=FF` site): XP→`h->rewXp`,
  Gold→`h->rewGp`, TP→`h->rewTp`, drop(rec+4)→deduped `h->rewDrops[3]`. Drop-rate gate (`$AE5D` bit6 +
  `rand<0x5A`, §P3.2) is OPEN → applied UNCONDITIONALLY (the drop id/dedup mechanism is pinned; the rate is the
  minor open) — documented, and it draws no RNG so it does not perturb the `$26` stream.
- **Apply at victory** in `fieldBattleFinish`, immediately before the block→sheet writeback (§S3): each
  SURVIVING PC (dead skipped, block+0x1D bit7) gets the FULL XP (block+0x13, 3B) + TP subtracted (block+0x2D,
  u16); gold → `$2C53` (24-bit); drops → `$2400`/`$2500` (matching-or-free slot, cap 99). The block XP/TP then
  ride the existing writeback to the sheet (§P3.4). Level-up / tech-learn = OUT OF SCOPE (TP just decrements).
- **Results box** (`battleResultsRender`, field.c): after the fixed celebration (`CT_BATTLE_VICTORY_CELEBRATE`),
  a top CT dialogue window (reusing `drawCtWindow` + the VWF `cDrawGlyph`) shows the line sequence, each
  advanced by an A-edge (`resultLine` in battle.c's victory-hold), zero/absent lines skipped. Templates = the
  §V3 CE/EF2D decoded strings; item name via the existing `CtMenuItemName` ($CC/0B5E).

**VERIFIED byte-exact vs §P2.2** (`CT_BATTLE_TRACE` reward line): GATO → `xp=10 tp=1 gold+0 drops=0`, both PCs
xp 0→10; forest 3×Hetake → `xp=9 tp=3 gold+48(400→448) drops=1 itemBD(Tonic)`, both PCs xp 0→9, tpnext −3.
Sequence: GATO shows 2 lines (XP,TP), forest 4 (XP,TP,Gold,item). Regression: `field-battle-results-reward`.
