# BATTLE ITEM-DROP + CHARM — the last two pieces of the enemy REWARD record

Closes the `battle_results_window.md` §OPEN "**Drop-rate & Charm semantics** (`FD/AC08+`)" and the
`battle_status_pipeline.md` §1 headline "**`C1/E9D5` = CHARM**". Derives, from the ROM disasm
(`tools/dis.py <fileoffset>`, fileoffset = `(bank−0xC0)<<16 | addr`) + the LakeSnes oracle
(`ct_probe.exe` = `build/ct.exe`, `--monitor ADDR:LEN --from STATE --input '<btn:frames,…>' --diff
--writer`; ⚠ LEN is DECIMAL; `.state` WRAM at file 0x45; **A = 0x100** in the LakeSnes serial button
word, NOT 0x80=Right) **only**: the full 7-byte reward record, the item-drop ROLL at enemy death, and
the CHARM steal-item roll. Both were previously OPEN/misattributed; both are now byte-derived + shipped.

> **HEADLINE.**
> 1. The **7-byte reward record `CC/5E00[id×7]`** is fully decoded (confirms `battle_results_window.md`
>    P2): `+0..1 XP(u16) · +2..3 Gold(u16) · +4 DROP item · +5 CHARM item · +6 TP(u8)`. GATO's `01` at
>    byte+6 (the mission's open) = **TP**, not a drop index.
> 2. The **DROP** (`FD/AC08`) is granted at each enemy DEATH iff `rec+4 != 0` **and** `rec+4 != rec+5`
>    **and** `$AE5D[slot] bit6` **and** `rand(0..99) < 0x5A` (a **flat 90%**, on the MAIN `$26` stream).
>    `$AE5D[slot] bit6` is the **has-drop** spawn flag, oracle-proven `== (rec+4 != 0)`. The drop is
>    deduped into `$B2A7[0..2]` and granted to the inventory `$2400/$2500` at victory (`C1/D010`).
> 3. The **CHARM** (`C1/E9D5`) rolls on a PHYSICAL HIT iff the attacker's staged header byte+7 bit1
>    (`$AE4D` bit1) is set (**Ayla's Charm ONLY** — every basic weapon's `CC/213F[rec]+7 == 0`, so it is
>    INERT). `chance = $16` (×3 if attacker `block+0x2A == 0xA5`); `roll = rand(0..99)`; on success it
>    grants the target's `$AE5A`-available charm (`rec+5`) or drop (`rec+4`) item to the inventory
>    (`C1/D005`), `$B3BF` = the marker (0xFE miss / 0xFF none / ≥1 granted). `$AE5A bit7/bit6` =
>    has-charm/has-drop, oracle-proven `== rec+5!=0 / rec+4!=0`.

---

## PART 1 — THE 7-BYTE REWARD RECORD `CC/5E00` (id×7)  [confirms battle_results_window.md P2]

The death-time accumulator `FD/ABD2` (`dis.py 3DABD2`) reads the record with `X = id×7`:
```
3DABD6  LDA $CC5E00,X / ADC $B28C / STA $B28C   ; +0..1 = XP    (u16 LE)  -> $B28C
3DABE1  LDA $CC5E02,X / ADC $B2A5 / STA $B2A5   ; +2..3 = Gold  (u16 LE)  -> $B2A5
3DABEF  LDA $CC5E06,X / ADC $B2DB (carry $B2DD) ; +6    = TP    (u8)      -> $B2DB
3DAC08  LDA $CC5E04,X / … / CMP $CC5E05,X       ; +4 = DROP item ; +5 = CHARM item  (PART 2)
```
**Layout (7 bytes):** `+0..1 XP · +2..3 Gold · +4 drop-item · +5 charm-item · +6 TP`.

| enemy | id | record (`rom:CC/5E00+id×7`) | XP | Gold | **drop +4** | **charm +5** | TP |
|---|---|---|---|---|---|---|---|
| **Hetake** | 0x5E | `03 00 10 00 BD 00 01` | 3 | 16 | **0xBD** (Tonic) | 0x00 (none) | 1 |
| **GATO** | 0x92 | `0A 00 00 00 00 C9 01` | 10 | 0 | 0x00 (none) | **0xC9** (Power Meal) | 1 |

The mission's open — GATO byte+6 = `0x01` — is **TP** (the same field `battle_results_window.md` already
reads). The engine's reward staging (`src/battle.c battleApplyDamage`, `field.c fieldBattleFinish`) reads
`rec[0..1]=XP`, `rec[2..3]=Gold`, `rec[6]=TP`, `rec[4]=drop` — matching this layout exactly.

---

## PART 2 — THE DROP ROLL (`FD/AC08`-`AC4B`, byte-exact)

### 2.1 The death handler tail (`dis.py 3DAC08`, 8-bit)
```
3DAC08  LDA $CC5E04,X       ; A = rec+4 (drop id)
3DAC0C  BEQ  3DAC4B         ; drop == 0 -> no drop  (a)
3DAC0E  STA  $10            ; $10 = drop id
3DAC10  CMP  $CC5E05,X      ; drop  vs  rec+5 (charm)
3DAC14  BEQ  3DAC4D         ; drop == charm -> the $B2AA list (b, unexercised)
3DAC16  LDX  $04            ; X = enemy slot ($0E-3)
3DAC18  LDA  $AE5D,X / BIT #40 / BEQ 3DAC4B   ; (c) $AE5D[slot] bit6 clear -> no drop
3DAC21  LDA #64 / JSL C1/FDCB                 ; (d) roll = rand(0..99)  -- DRAWS $26
3DAC27  CMP #5A / BCS 3DAC4B                   ; roll >= 90 -> no drop (so drop lands 90% of the time)
3DAC2B  … walk $B2A7[0..2]: empty -> store ; CMP $10 / BEQ -> DEDUP skip ; INY / CPY #03
3DAC40  STA $B2A7,Y         ; store the drop id (deduped, max 3)
3DAC46  LDA $B2AF / ORA #20 / STA $B2AF        ; $B2AF bit5 = "a drop was collected"
```
The RNG helper `C1/FDCB` (`dis.py 1FDCB`) is **`JSR C1/AF22; RTL`** — the SAME `$26`-stream RNG (table
`FD/BA61[$26]`, `$26++` per draw) that the miss-roll (`C1/DC94`), damage-variance (`C1/E15E`) and charm
(`C1/E9D5`) use. **The roll draws `$26` on EVERY drop-eligible death, BEFORE the dedup** (`3DAC21` precedes
`3DAC2D`) → N draws for N such deaths (3 for the forest), only one stored.

### 2.2 `$AE5D[slot] bit6` = the has-drop spawn flag = `(rec+4 != 0)`  [oracle-proven]
`$AE5D` is a per-enemy-slot flag array; `$AE5A` is its per-combatant twin (0-10). Their bits:
**bit6 = has-DROP (rec+4 != 0), bit7 = has-CHARM (rec+5 != 0)**, set at enemy spawn from the record.

| state | enemy (id) | `$AE5D[slot]` | `$AE5A[combatant]` | record +4/+5 | reading |
|---|---|---|---|---|---|
| `forest_battle_3hetake` | 3× Hetake (0x5E) | `40 40 40` | `40 40 40` (slots 3/4/5) | 0xBD / 0x00 | **bit6 set** (has drop), bit7 clear |
| `crono_normal_ready` | GATO (0x92) | `..80..` (slot 3) | `..80..` (slot 6) | 0x00 / 0xC9 | **bit7 set** (has charm), **bit6 CLEAR** (no drop) |

So `$AE5D bit6 == (rec+4 != 0)` on both enemies — the death handler's gate `(c)` is **byte-equivalent to
`(a) rec+4 != 0`**, which the engine already checks. No separate `$AE5D` image is needed. (The spawn-init
instruction that SETS these bits was not located in the disasm; the SEMANTICS are pinned by the two oracle
datapoints + the death handler `3DAC18` and the charm `1EA29` both reading them — §7 O2.)

### 2.3 Oracle: the forest drop, end-to-end (`--monitor … --input '100:4,0:22'×100 --diff --writer`)
```
f175  $B2A7 = BD 00 00 (writer FD:AC43)  $B2AF = 20 (FD:AC4B)  $B28C=3   ; 1st Hetake death: drop ROLLED + stored
f372  $B28C=6  ($B2A7 UNCHANGED)                                          ; 2nd death: rolled, DEDUPED (Tonic already listed)
f738  $B28C=9  ($B2A7 UNCHANGED)  ; f739 $AF02 all-FF (C1:B2C3)           ; 3rd death: rolled, deduped; VICTORY
f1057 $2400 = D6 BD .. (writer C1:D03D)  $2500 = 01 01 .. (C1:D044)       ; the Tonic GRANTED to inventory (C1/D010)
```
So: roll per death (3×), dedup to one Tonic, **granted at victory** (`C1/D010`, writers `C1:D03D/D044`).
`$B3BF` stayed 0 all fight (charm never fired — no bit1 attack). GATO (drop 0) never rolls (bit6 clear).

---

## PART 2b — THE `drop == charm` ARM (`FD/AC4D`), PORTED 2026-08-22

`FD/AC10 CMP $CC5E05,X / BEQ ->FD/AC4D` splits the death handler in two, and only the first half was
ported. When the reward record's drop (+4) EQUALS its charm (+5) and is nonzero, the item does not take
the rolled path at all — it goes to `FD/AC4D-AC6A`, which walks `$B2AA,Y` for Y = 0..2, dedups
(`CMP $10 / BEQ done`), stores, and sets `$B2AF |= 0x20`. **No `$AE5D` bit6 gate and no `rand(0..99)`
roll** — those two sit only on the rolled path (`FD/AC18-AC29`). So an enemy on this arm ALWAYS drops.

`$B2AA` is `$B2A7 + 3`: ONE 6-slot array, the rolled half in 0..2 and the guaranteed half in 3..5, which
the victory grant walk consumes whole (`C1/8408 LDA $B2A7,X`, X = 0..5, after `C1/83F9 JSR C1/EAE8 /
JSR C1/B442`). The dedup is WITHIN each half — a rolled Tonic and a charm Tonic can both grant. Ported:
`h->charmPend[]` collects these deaths and `battleResolveDrops` appends them after the rolled half,
unconditionally, deduped against the guaranteed half only.

**Oracle proof.** No shipped formation contains such an enemy, so the state was patched: `gato_inbattle`
(a MID-battle capture, i.e. after staging) with the three id bytes — the block's `+0`, `$AF02+3`,
`$AF0D+3` — set to `0x3E`, whose record is `0B 00 2D 00 BD BD 01` (drop == charm == `0xBD`, MidTonic):

```
  --monitor "7EB2A7:6,7EB2AF:1" --diff --writer
    f1513   7EB2A7=000000BD0000*(FD:AC65)   7EB2AF=20*(FD:AC6D)
```

the item lands in **slot 3** (= `$B2AA[0]`) with **no preceding `$26` draw**, writer `FD:AC65` = the
guaranteed store, and `$B2AF` gets its `0x20`.

⚠ **PORTED BUT NOT PINNED, and here is exactly why.** Eleven enemy ids ROM-wide have `drop == charm`
(`25 3E 3F 4D 4E 5F 6B 6C 80 91 D8`) and **not one appears in any capture on disk**. The `--battletest`
harness cannot fabricate one either: the staged enemy id is not in the seed WRAM — it is written by the
scene's own VM during the intro (the seed's `$29BF` records hold the FIELD objects, `$2989` is 0, and
patching either leaves `$AF02` at the scene's `0x92`). A `CT_BATTLE_ENEMYID` override hook was written
and then REMOVED: it produced a fight that never resolved (the enemy sat at HP 0 without delisting), so
it graded nothing and added a broken debug path. The arm therefore ships on ROM + the oracle measurement
above, with no regression check — the honest state, recorded rather than papered over with a green check
that exercises the other half. What would close it: a real capture of a fight containing one of those
eleven ids, or the scene-VM staging path made drivable.

## PART 3 — THE CHARM ROLL (`C1/E9D5`, byte-exact)  [supersedes the misattribution]

### 3.1 The caller — the D306 physical HIT-tail (`dis.py 1D338`)
```
1D335  JSR C1/DCAD                 ; damage compute+apply (variance wrapper / wake-on-hit)
1D338  LDA $AE4D / AND #04 / BEQ 1D345   ; bit2 -> an extra effect (D14F/E976)
1D345  LDA $AE4D / AND #02 / BEQ 1D34F   ; ** $AE4D bit1 -> JSR C1/E9D5 (CHARM) **  (only on the HIT path)
1D34C  JSR C1/E9D5
```
`$AE4D` = the attacker's staged header **byte+7** (`0x1EB0E` mirrors `$AEE6[7]`→`$AE4D`; the 17-byte
header `CC/213F[rec]` is staged to `$AEE6` at `0x1BF81`). **Every basic-weapon header `CC/213F[rec]+7`
= 0x00** (verified rec 0-6), so bit1 is set only by Ayla's **Charm** — the roll is INERT in every other fight.

### 3.2 The roll + grant (`dis.py 1E9D5`, 8-bit)
```
1E9D5  STZ $B3BF                              ; marker = 0
1E9DB  LDA $5E57,X (attacker block+0x2A) / CMP #A5 / BNE +   ; == 0xA5 (Ayla's Charm signature) ->
1E9E2    $16 = $16*3                          ;   triple the chance
1E9EA  LDA $16 / … / LDA #64 / JSR C1/AF22    ; roll = rand(0..99) on $26
1E9F3  CMP $16 / BEQ succeed / BCC succeed    ; SUCCEED iff roll <= chance ($16) ; else:
1E9F9    LDA #FE / STA $16 / STA $B3BF         ;   MISS -> $B3BF = 0xFE
1EA02  (success) $0A = AF0A[target]*7 ; JSR C1/EB1C (sets $1A) ;
1EA23  LDA $1A / BEQ 1EA40                     ; $1A != 0 -> try the CHARM branch:
1EA29    LDA $AE5A,X / BIT #80 / BEQ 1EA40     ;   bit7 (has-charm) set ->
1EA30    AND #7F / STA $AE5A,X / INC $B3BF     ;     clear bit7 (no re-charm), marker++
1EA3A    LDA $CC5E05,X (charm item rec+5) / BRA 1EA57
1EA40  (bit6 branch) LDA $AE5A,X / BIT #40 / BEQ 1EA67   ; bit6 (has-drop) set ->
1EA49    AND #BF / INC $B3BF / STA $AE5A,X ; LDA $CC5E04,X (drop item rec+4)
1EA57  CMP #00 / BEQ 1EA67 ; STA $06 / JSL FD/B655   ; grant the item id
1EA67  (no item) LDA #FF / STA $B3BF          ; rolled-ok-no-item -> $B3BF = 0xFF
```
- **`$AE5A[target]` bit7 = has-charm (rec+5!=0), bit6 = has-drop (rec+4!=0)** (§2.2). Success grants the
  charm item `rec+5` (bit7, the `$1A!=0` Charm-tech branch) or else the drop item `rec+4` (bit6); the used
  bit is CLEARED so an enemy can't be re-charmed. `$1A` comes from `C1/EB1C` (the `$FA61` effect dispatch on
  the effect id `$B2CC`) — the selector between the two branches (§7 O3).
- **`FD/B655`** (`3DB655`): `id >= 0xBC` → item-EFFECT (`CC/0ABC`, the `battle_item_slice.md` engine); else
  `JSL C1/FDC3 → C1/D005` = **inventory add-item** (walk `$2400` for a matching id → `INC $2500` cap 99, or
  append). Most charm items are `< 0xBC` accessories → inventory; the `>=0xBC` effect path is §7 O4.
- **`$B3BF`**: 0xFE roll-miss / 0xFF rolled-ok-no-item / incremented per granted item.

### 3.3 Oracle proof (`statuspatch/`, the header-patch seed)
`statuspatch/` = a ROM copy with the 6 physical headers' byte+7 `|= 0x02` (`$AE4D` bit1). Crono attacks
GATO → `--monitor "B3BF:1,AE4D:1"`: `f43 AE4D=82` (bit1 set), `f44 B3BF=FE` (writer `C1:EA00`) — E9D5 fires
and **misses** (the physical path leaves the chance `$16 = 0` — `C1/DCAD 1DCFB STX $16` — so `roll<=0`
succeeds only on `roll==0`). This is the exact observable the engine reproduces under `CT_BATTLE_CHARMTEST=0`.

---

## PART 4 — IMPLEMENTATION (SHIPPED in `src/battle.c` + `src/battle.h`)

### 4.1 The DROP — candidate at death, ROLL at victory (pin-preserving)
`battleApplyDamage` (enemy-death path) records each drop-eligible death into `h->dropPend[]` (one entry per
death, death-ordered, gated `rec[4]!=0 && rec[4]!=rec[5]`) — **no `$26` draw here**. `battleResolveDrops`
(called once at victory-entry, from `battleBuildResults`) rolls each pending candidate `rand(0..99)<0x5A` on
`$26`, deduping successes into `h->rewDrops[]`; `fieldBattleFinish` grants them to `$2400/$2500` (already
shipped, `C1/D010` match-or-append cap 99) and `battleBuildResults` appends the "Got 1x <item>!" results line.

> **DEVIATION (drop `$26` POSITION).** The ROM rolls PER death (mid-battle); we DEFER the draws to victory so
> they do not perturb mid-battle combat variance (the ROM's death-time draws shifted our forest `cronoHP`
> 21→1 — a real pin move, since our combat is **not** `$26`-lockstep with the oracle). The roll COUNT (one per
> drop-eligible death), the death-ORDER, and the DEDUP are all preserved byte-faithfully; only the `$26`
> position differs. With 3 Hetake rolls the drop lands at ~99.9%, so the forest Tonic is robustly deterministic.

### 4.2 The CHARM — `battleCharmRoll`, header-gated, inert
`battleCharmRoll(h, atk, tgt, chance)` mirrors `C1/E9D5`: `chance ×3` if `block+0x2A==0xA5`; `roll =
battleRng(…,99)`; miss (`>chance`) → `$B3BF=0xFE`; success → pick `rec+5` (has-charm) else `rec+4`
(has-drop) via `h->charmTaken[slot]` (the `$AE5A` bit7/bit6 clear), add-item `$2400/$2500` (C1/D005),
`$B3BF` marker. Wired at the PC-physical span-end HIT seam (after `battleApplyDamage`). **The natural gate
`$AE4D bit1` is unreachable** — our engine does not model the attack-header staging (`$AEE6/$AE4D`) — so the
roll fires ONLY under `CT_BATTLE_CHARMTEST` (the header-patch analogue): env value = the forced chance
(`0` → the `$B3BF=0xFE` oracle-match; `99` → a guaranteed hit exercising pick+grant+inventory). Inert
(no `$26` drawn) in every fight without the hook.

### 4.3 Checks (quick; zero pins move)
- **`field-battle-drop-roll`** — forest: `battle-drop k0/k1/k2 dropBD roll=51/78/88(<90) took=1 dropN=1`
  (the 3 per-death rolls, deduped) — deterministic ×2.
- **`field-battle-charm-forced`** — GATO `CHARMTEST=99 DEBUGEND=320`: `battle-charm atk0 tgt6 eid92
  roll=2<=99 itemC9 B3BF=01` (Crono charms GATO → Power Meal 0xC9 granted) + `battletest: PASS`.
- Baseline `field-battle-results-reward` note updated (drop no longer "unconditional"). Suite: **quick
  91/0/1, FULL 113/0/3** — unchanged (both features inert/pin-preserving).

---

## PART 5 — OPENS / BOUNDARIES

| # | item | status |
|---|---|---|
| O1 | **The `$B2AA` list** (the `rec+4 == rec+5` drop==charm branch, `3DAC4D`) | routes an identical drop/charm id to a separate list, granted unknown; UNEXERCISED (no pinned enemy has `drop==charm != 0`). Our drop gate requires `rec+4 != rec+5` (faithful); the `$B2AA` path is not implemented. |
| O2 | **The `$AE5D`/`$AE5A` spawn-init instruction** | the flag SEMANTICS (bit6=has-drop, bit7=has-charm = rec+4/rec+5 != 0) are oracle-proven on 2 enemies + read by `3DAC18` (drop) / `1EA29` (charm); the exact spawn-time store that sets them was not located (a computed/looped write my `dis.py` grep missed). Not needed — the drop gate uses the byte-equivalent `rec+4 != 0`. |
| O3 | **The charm's natural wiring** (`$AE4D bit1` gate + the `$16` status-accuracy chance + `C1/EB1C`'s `$1A` branch selector) | requires the attack-header staging model (`$AEE6`/`$AE4D`, `C1/1BF81`→`EB0E`) our engine does not have, plus the `$FA61`-dispatch effect `$B2CC`. The roll is validated via the forced hook (the header-patch analogue), exactly as the status lane validated E9D5 via `statuspatch/`. Natural Ayla's-Charm fire = future work. |
| O4 | **The `id >= 0xBC` charm grant path** (`FD/B655` → item-EFFECT `CC/0ABC` instead of inventory) | we grant via the inventory add-item (`C1/D005`, `id < 0xBC` accessories). A charm item `>= 0xBC` (a consumable) would apply its item effect immediately; the item-effect engine exists (`battle_item_slice.md`) but is not wired to the charm. |
| O5 | **Multi-drop / >3 unique drops / a fight with two distinct dropping enemies** | the dedup + 3-slot cap are implemented; only the single-unique-drop forest is captured. |

---

## SCRATCHPAD ARTIFACTS (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-9e84b785-…/scratchpad/`
- `ct_probe.exe` (= `build/ct.exe`). States (repo): `saves/battle_re/forest_battle_3hetake.state`
  (3× Hetake, drop capture), `gato_press_action.state`; (scratchpad) `crono_normal_ready.state` (GATO
  staged, `$AF0A[6]=0x92`, the charm/`$AE5D` datapoint), `statuspatch/` (ROM `$AE4D|=0x02`, the E9D5 proof).
- Method — **drop:** `--monitor "B2A7:3,B2AF:1,26:1,B28C:2,2400:8,2500:8" --from forest_battle_3hetake.state
  --input '100:4,0:22'×100 --frames 2700 --diff --writer` (drop-store FD:AC43 f175, grant C1:D03D f1057).
  **flags:** `--read "wram:AF0A+11,wram:AE5D+8,wram:AE5A+11" --from <state> --settle 0`.
  **charm:** run from `statuspatch/` (patched ROM) → `--monitor "B3BF:1,AE4D:1" --diff --writer` ($B3BF=FE).
- Disasm anchors (file offset): reward accumulator `0x3DABD2`; drop roll `0x3DAC08`; drop RNG `0x1FDCB`
  (→ `0x1AF22`); drop grant `0x1D010`; charm roll `0x1E9D5`; charm sub-dispatch `0x1EB1C`; charm caller
  (D306 tail) `0x1D306`; charm grant `0x3DB655`→`0x1D005`; `$AE4D=header+7` `0x1EB0E`; header staging
  `0x1BF81`. ROM tables: reward `CC/5E00` (`0x0C5E00`), header `CC/213F` (`0x0C213F`), RNG `FD/BA61`.

## 2026-10-19 -- the roll at the death (the DEVIATION of 4.1 removed)

The drop roll (FD/AC1F-AC27, `LDA #64 / JSL C1/FDCB / CMP #5A`) now draws `$26` at each drop-eligible death, in the
death path, as the ROM does (battle.c battleRollDrop); battleResolveDrops keeps only the guaranteed (drop == charm) half.
The deferral to the victory was kept while the port's combat was not `$26`-lockstep. It is now, on a route drive: the
playthrough's leg 20 (Guardia Forest's three ambushes) once the fight takes the route's presses on the cartridge's frames
(CT_WS_BATTLE_LEAD): the rule's cartridge draws at FD:AC21 on f1648 (`--cputrace`, `$26` A9 -> AA, the first Hetake's
death), the port drew the same roll at the victory (f1980), and every later roll was an entry off -- the hand-back `$29AD`
D3 against BE, the second ambush's targets, damage and length apart. With the roll at the death the leg's values (gold,
HP, control, locations) are the cartridge's through all three fights. Re-pinned: field-battle-drop-roll,
field-battle-end-uiteardown, battle-motion-owners-explain-the-fight (the forest fight's later rolls move).
