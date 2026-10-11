# BATTLE STATUS-EFFECT PIPELINE — the block status model, the inflict, the cure, wake-on-hit

Derives, from the ROM disasm + LakeSnes oracle **only**, the battle STATUS system: where a combatant's active
statuses live, how an attack inflicts one (mask + immunity), how it is cured, and the ONE cleanly-observable
status law (wake-on-hit). Every claim cites a `tools/dis.py <fileoffset>` (fileoffset = `(bank−0xC0)<<16 | addr`;
battle code runs 8-bit `m=1 x=1` unless a `REP` is shown) or an oracle `ct_probe.exe` (= `build/ct.exe`)
`--monitor`/`--read` observation. Writer PCs = the PC *after* the store retires.

> ⚠ **HEADLINE CORRECTION — `C1/E9D5` is NOT the status roll; it is the CHARM (steal-item) roll.**
> The mission brief (via `battle_miss_evade.md` §OPEN and `battle_crit.md` §H) said the post-damage STATUS roll
> is `C1/E9D5`, gated `$AE4D bit1`, "does the attack's poison/sleep land". **Disassembly + oracle prove this is
> the Charm mechanic** (roll on hit to obtain the enemy's charm item into the inventory), NOT sleep/poison. §1
> documents the proof and the real Charm mechanic. The **real** status system is a *different*, independently
> located subsystem (§2–§5): three per-combatant block bytes (`+0x1D/+0x1E/+0x1F`), the inflict executor
> `C1/DBAA`, the def table `CC/05CC`, the cure `C1/F036`, and the wake-on-hit `C1/DCAD`. This dossier supersedes
> the E9D5-as-status claim everywhere.

---

## 0 — HEADLINE RESULTS

1. **`C1/E9D5` = CHARM.** Gated `$AE4D` bit1 (the D306 physical tail, `1D345`), it rolls `rand(0..99) ≤ $16` and,
   on success, reads the TARGET's reward record `CC/5E00[targetId×7] + 4/+5` and grants that **item id** into the
   player inventory `$2400/$2500` via `JSL FD/B655 → C1/FDC3 → C1/D005`. `$B3BF` = the roll marker (0xFE = missed,
   0xFF = rolled-ok-no-item, 1/2 = granted). **Proven** (§1): a patched `$AE4D|=0x02` made a Crono hit run E9D5 →
   `$B3BF=0xFE` (writer `C1:EA00`); `CC/5E00[GATO 0x92]` = `0A 00 00 00 00 C9 01` (byte+5 = 0xC9 = an item id);
   `C1/D005` writes `$2400` (inventory). This is Charm, not status.

2. **The STATUS MODEL = three per-combatant block bytes.** In the battle block `$5E2D + seat*0x80`:
   - **`+0x1D` (`$5E4A`)** = status byte 0 (bit7 = **dead/wiped**; bits 0-5 curable; bit6 kept by the cure).
   - **`+0x1E` (`$5E4B`)** = status byte 1 (all 8 bits curable; **bits 1-2 cleared on taking damage = wake-on-hit**).
   - **`+0x1F` (`$5E4C`)** = status byte 2 (bits 0,2-5 curable; bits 1,6,7 kept).
   - **`+0x22/+0x23/+0x24` (`$5E4F/50/51`)** = the per-byte **IMMUNITY** masks (a set bit = immune to that status bit).

3. **INFLICT = `block[0x1D + category] |= mask`, immunity-gated** (`C1/DBAA`, `1DBCC-1DC57`): read
   `immunity = block[0x22 + category]`; if `immunity & mask == 0` (not immune) then `block[0x1D + category] |= mask`
   and `JSR C1/E006` (onset). `category` (which of the 3 bytes) and `mask` come from the effect's def record.

4. **The effect STATUS DEF = `CC/05CC` (4-byte record, `effectId×4`):** byte+1 (`CC/05CD`) = category/flags, byte+2
   (`CC/05CE`) = status mask, byte+3 (`CC/05CF`) = accuracy. (`C1/D20A` loads `$16=CC/05CD`, `$18=CC/05CE`;
   `C1/D1F9` loads `$16=CC/05CF`.)

5. **CURE = `C1/F036` (`$1C` bit6):** clears the curable status bits: `+0x1D &= 0xC0`, `+0x1E &= 0x00`,
   `+0x1F &= 0xC2` (keeps dead + the two permanent bits). This is the effect the Heal/Revive item (selector 0x00)
   reaches. (`$1C` bit7 = the co-located full-HP revive, `1F036-1F072`.)

6. **WAKE-ON-HIT (proven end-to-end):** the damage routine `C1/DCAD` (`1DCB1`) does `block+0x1E &= 0xF9` — clears
   **bits 1 and 2** of `+0x1E` whenever a combatant takes damage. Oracle: a GATO with `+0x1E = 0x04` patched, hit
   by Crono, went **`0x04 → 0x00` at f43** (the damage-compute frame, writer `C1:DCB9`), HP applied later at f171.
   So `+0x1E` bits 1-2 = the "sleep-class" statuses that break on damage.

7. **The "can't act" GATE:** the scheduler `C1/8660` (`18667`) reads `block+0x1E`; `BIT #04` (bit2) set → `JMP
   C1/881C` (skips the normal turn dispatch). So `+0x1E` bit2 = a turn-blocking status (SLEEP-class).

---

## 1 — `C1/E9D5` IS THE CHARM ROLL (the misattribution, corrected)

### 1.1 The routine (`dis.py 1E9D5`, 8-bit)
```
1E9D5 STZ $B3BF                       ; roll marker = 0
1E9D8 LDX $B1F4 / LDA $5E57,X         ; attacker block+0x2A
1E9DE CMP #A5 / BNE 1E9EA             ; == 0xA5 -> triple the chance:
1E9E2   LDA $16 / ASL / CLC / ADC $16 / STA $16   ; $16 *= 3   (the "hit×3" of battle_crit.md §H)
1E9EA LDA $16 / TDC / TAX / LDA #64 / JSR C1/AF22 ; roll = rand(0..99) on the $26 stream
1E9F3 CMP $16 / BEQ 1EA02 / BCC 1EA02 ; roll <= $16  -> SUCCEED (1EA02) ; else:
1E9F9   LDA #FE / STA $16 / STA $B3BF / BRA 1EA6E  ; MISS: $B3BF = 0xFE
   1EA02 (success): $0C = target slot ($B1FD); $0A = AF0A[target] * 7   (AF0A = target's charId/enemyId)
   1EA20 JSR C1/EB1C                  ; effect-type sub-dispatch ($1A = bit7-vs-bit6 selector)
   1EA27 $AE5A[target] bit7 set? -> value = CC/5E05[$0A] ; else bit6 -> value = CC/5E04[$0A]
   1EA5B $06 = value ; JSL FD/B655 ; $16 = 0            ; APPLY the id
   1EA67 (no applicable bit) $16 = 0xFF ; $B3BF = 0xFF
```

### 1.2 What it applies — the enemy REWARD record's charm item, into the INVENTORY
- `AF0A[target]` = the target's id (PC seat → charId 0-6 via `C1/FB00`/`FD/B240`+`B274`; enemy → enemy id, e.g.
  GATO = **0x92**). Oracle `--read AF0A` on a staged fight: `00 01 FF FF FF FF 92 FF..` (Crono 0, Marle 1, GATO 0x92).
- `CC/5E04[id×7]` / `CC/5E05[id×7]` are bytes **+4/+5 of the 7-byte reward record `CC/5E00[id×7]`** (the same table
  `battle_victory_rewards.md` uses for XP/GP/TP). GATO (0x92): `0A 00 00 00 00 C9 01` → +4=0x00, **+5=0xC9** (an item
  id, ≥ 0xBC). Crono (0): `F8 00 00 00 00 4F 1E` → +5=0x4F.
- `FD/B655` (`3DB655`): if `$06 ≥ 0xBC` → item effect via `CC/0ABC` (the item table, `battle_item_slice.md`); else
  `JSL C1/FDC3` (thunk → `C1/D005`). **The ONLY caller of `FD/B655` is E9D5** (`1EA5D`).
- `C1/D005` (`1D005`): walks `$2400` for a matching id and `INC $2500` (count), or appends a new `$2400/$2500`
  slot — i.e. **adds the item to the player inventory**. This is Charm: hit an enemy, roll, grab its charm item.

### 1.3 Oracle proof (patched `$AE4D` bit1 to force E9D5 on a normal Crono hit)
`statuspatch/` = a ROM copy with the 6 physical effect-headers' byte+7 `|= 0x02` (`$AE4D = $AEED = header byte+7`,
via `C1/EB0E`). Crono attacks GATO → `--monitor "B3BF:1,AE4D:1,612D:40" --diff --writer`:
```
f43  AE4D=82 (C1:D632)         ; bit1 now set -> D306 tail 1D34C reaches E9D5
f44  B3BF=FE (C1:EA00)         ; E9D5 ran and MISSED (roll > $16; $16 leftover 0 in the physical path)
f171 612D +3 = 4C->3B (C1:B28B)  ; only the normal physical damage; NO status byte moved
```
`$B3BF=0xFE` = the E9D5 "roll failed" store — E9D5 fires, is the charm roll, and touches no status byte. (The
physical path leaves `$16=0` — `C1/DCAD` `1DCFB STX $16` — so charm never lands off a raw physical without the
`CC/05CF` accuracy the charm-carrying weapon supplies; not pursued, it is not the status system.)

---

## 2 — THE STATUS MODEL (three block bytes + immunity)

The battle block is `$5E2D + seat*0x80` (`battle.h`; PC seats 0-2, enemies 3-10 at `$612D = $5E2D+2*0x80`, …).
The **status state** occupies:

| block off | addr (Crono) | role | cure keeps | wake-on-hit |
|---|---|---|---|---|
| **+0x1D** | `$5E4A` | status byte 0 | bits 6-7 (`&0xC0`); **bit7 = dead** | — |
| **+0x1E** | `$5E4B` | status byte 1 | nothing (`&0x00`) | **bits 1-2 cleared on damage** |
| **+0x1F** | `$5E4C` | status byte 2 | bits 1,6,7 (`&0xC2`) | — |
| +0x22 | `$5E4F` | IMMUNITY for byte 0 | — | — |
| +0x23 | `$5E50` | IMMUNITY for byte 1 | — | — |
| +0x24 | `$5E51` | IMMUNITY for byte 2 | — | — |

Evidence:
- The cure `C1/F036` and the pre-inflict reset `C1/DB5E` (`1DB70-1DB85`) both touch exactly `+0x1D &0xC0`,
  `+0x1E &0x00`, `+0x1F &0xC2` — pinning the 3 status bytes and their curable/permanent bit split.
- `C1/EA9D` (`1EA9D-1EAE7`) zeroes `$5E4B..$5E53` (block +0x1E..+0x26) at combat reset — the status + immunity +
  stat-buff region.
- The inflict `C1/DBAA` reads immunity at `$5E4F,X` where `X = block + category` (`1DBCC`), so immunity byte =
  `+0x22 + category`. **Oracle:** GATO block `+0x23 (= +0x22 + cat1) = 0xFF` (`--read wram:614A+16` →
  `.. FF ..` at `$6150`) = GATO immune to ALL byte-1 (`+0x1E`) statuses — a robot immune to Sleep. PCs read 0
  (no innate immunity) at `+0x22..+0x24`.

---

## 3 — THE INFLICT (`C1/DBAA`) + the def table + the roll boundary

### 3.1 The inflict executor `C1/DBAA` (`dis.py 1DBAA`, 8-bit)
```
1DBAA LDA $16 / CMP #FF / BEQ->skip           ; $16 = category (0..5) ; 0xFF = no status
1DBB4 CMP #00/#01/#02  -> 1DBC0 (byte-set path)
   1DBC0 X = block + $16 (category as the byte offset: 0->+0x1D, 1->+0x1E, 2->+0x1F)
   1DBCC LDA $5E4F,X / AND $18 / BEQ 1DBDC     ; immunity[cat] & mask == 0 ? (not immune)
   1DBDC LDA $5E4A,X / ORA $18 / STA $5E4A,X   ; ** block[0x1D+cat] |= mask **
   1DBE4 JSR C1/E006                            ; onset side-effect (§4.3)
1DBC0.. (cat 3/4/5, 1DC0D-1DC57): variants that also OR $18 into $5E4A,X (block[0x1D+cat]); cat3 pre-clears
   $5E4B bit5, cat4 conditional on $18 bit7.
```
`$16` = category, `$18` = mask. **Callers** (`JSR DBAA`): `C1/D2B7`, `C1/D4DF` (the status executor `D4D6`),
`C1/E989`, and the effect handlers `C1/EECC`/`EEF3`/`EF10` (+ `C9/73CC`, out-of-battle).

### 3.2 The def table `CC/05CC` (4-byte record, `effectId×4`)
`C1/D20A` loads the inflict inputs: `$16 = CC/05CD[eff×4]` (byte+1), `$18 = CC/05CE[eff×4]` (byte+2). `C1/D1F9`
loads `$16 = CC/05CF[eff×4]` (byte+3, the accuracy). The status-bearing records (dump, `effectId×4` from `0xC05CC`):
```
eff 0x01: 80 80 01 32     -> cat byte 0x80 (byte-0/+0x1D), mask 0x01, acc 0x32
eff 0x02: 80 81 04 32     -> cat byte 0x81 (byte-1/+0x1E), mask 0x04, acc 0x32   <- the SLEEP-class bit (§5)
eff 0x03: 80 82 0A 32     -> byte-2/+0x1F, mask 0x0A
eff 0x04: 80 40 01 0A     eff 0x05: 80 41 03 0A     eff 0x06: 80 42 06 0A
eff 0x07: 80 C2 0F 00     eff 0x0B: 04 80 01 F2     eff 0x0F: 40 04 40 00     eff 0x10: 40 04 04 00
eff 0x11: 02 0B 01 00     eff 0x12: 02 0E 01 00
```
The category byte (byte+1) low 3 bits select the block byte (0→+0x1D, 1→+0x1E, 2→+0x1F); the high bits
(`0x80`/`0x40`/`0xC0`) are op flags (set vs the `0x40`/`0xC0` clear-and-set variants — `HYPOTHESIS` from the
DBAA cat 0-5 branch structure, not exhaustively pinned). byte+2 = the bit mask.

### 3.3 The roll boundary
The **status executor `C1/D4D6`** (`1D4D6`) applies a status-only effect with **no separate accuracy roll** — per
target it runs `JSR D20A` (category+mask) then `JSR DBAA` (inflict), so the **immunity check in DBAA is the only
gate** for a pure status effect. For a status that RIDES a damaging spell, the spell's own hit roll (the magic-
accuracy roll `C1/D26D`: `acc = attackerMagic/CC05CE + CC05CD; rand(0..99); miss iff roll>acc`, `1D29C`) gates
whether DBAA is reached at all, and the def's `CC/05CF` accuracy (`C1/D1F9`) feeds a per-caller roll. **OPEN:** the
exact per-caller accuracy roll for each of the 7 DBAA callers is not exhaustively traced (§7 O2).

---

## 4 — CURE, RESET, ONSET

### 4.1 Cure `C1/F036` (the Heal/Revive selector-0 path)
```
1F073 LDA $1C / BIT #40 / BEQ->skip
1F079 $5E4A &= 0xC0 ; $5E4B &= 0x00 ; $5E4C &= 0xC2      ; clear the curable status bits
```
Reached through the effect-type dispatch (`$FA61` handler table, `1FA61`). The co-located `$1C` bit7 branch
(`1F036-1F072`) is the full-HP revive (`$AD89 = block+0x05 maxHP`, negated for the heal display).

### 4.2 Reset `C1/DB5E` (pre-inflict) + `C1/EA9D` (combat start)
`C1/DB5E` (`1DB70`) clears the same curable bits before a fresh apply (`$16` bit7 also clears `+0x1D` bit7 =
un-dead). `C1/EA9D` zeroes `$5E4B..$5E53` (status + immunity + buffs) at combat reset.

### 4.3 Onset `C1/E006` — the per-status timer setup (category 1 shown)
```
1E012 CMP #01 (category 1 / +0x1E) ; 1E01B $18 & 0xF0 gates (only mask bits 4-7 arm a timer):
  bit7 -> $AF27,X = $B0D4,X ; $AFB6,X = 1        (resets the actor ATB timer + a per-status flag)
  bit6 -> $AF32,X = $B0DF,X + block+0x37 ; $AFC1,X = 1
  bit5 -> $AF3D,X = $B0EA,X ; $AFCC,X = 1
  bit4 -> $AF7F,X = $B12C,X ; $B00E,X = 1
1E070 CMP #02 (category 2 / +0x1F) …  (further per-bit timers, not unrolled here)
```
So a timed status (mask bit 4-7 in `+0x1E`) arms a countdown in the `$AF27/$AF32/$AF3D/$AF7F` timer arrays with a
`$AFB6/$AFC1/$AFCC/$B00E` flag; mask bits 0-3 arm no timer (they are the instantaneous/until-hit statuses, e.g.
the `+0x1E` bit2 sleep). **OPEN:** the full per-bit onset semantics (which timer = what visible behavior) beyond
the ATB-slot reset is not fully decoded (§7 O3).

---

## 5 — ONE STATUS END-TO-END: the `+0x1E` bit2 "sleep-class" (wake-on-hit PROVEN)

The most observable status the block can carry directly is **`+0x1E` bit2 (mask 0x04, category 1)** — from `CC/05CC`
it is inflicted by **effect 0x02** (`80 81 04 32`). Its law, from the block model + two oracle observations:

**(a) WAKE-ON-HIT — proven byte-exact.** `gato_sleep.state` = `crono_normal_ready.state` with GATO block
`+0x1E (=$614B) |= 0x04`. Crono attacks the sleeping GATO (`--monitor "614B:1,6130:2" --diff --writer`,
`--input "100:10,0:20,100:10,0:230"`):
```
f0    614B=04                         ; GATO asleep (bit2)
f43   614B=04->00 (writer C1:DCB9)     ; the hit's damage-compute clears it -> WAKE  (C1/DCAD: block+0x1E &= 0xF9)
f171  6130=4C->3B (C1:ED1A)            ; HP applies at span-end (the pinned late-write)
```
Wake fires at the **damage-compute frame** (f43, the `C1/DCAD` variance wrapper), *before* the HP write (f171).
`&0xF9` clears **both** bits 1 and 2, so any `+0x1E` bit1/bit2 status breaks on any damage taken.

**(b) TURN-BLOCK (structural + partial).** The scheduler `C1/8660` (`18667 LDA $5E4B,X / BIT #04 / BNE JMP 881C`)
skips the normal turn dispatch when `+0x1E` bit2 is set. Oracle (GATO asleep, idle 700f): GATO's first action
slipped from the baseline **f247 → f502** (a 255-frame stall) — the status delayed the turn — but was not observed
to fully suppress it over the window, and a *raw-bit* sleeping PC (`crono_sleep.state`, Crono `+0x1E|=0x04`)
self-cleared at f139. **CAVEAT:** a direct block-bit patch skips the `C1/E006` onset (which arms the `$AF27..`
timer + `$AFB6..` flag a real inflict sets), so the full "asleep for N turns then auto-wake" behavior is NOT
reproduced by the raw patch — only the wake-on-hit clear (which is onset-independent, in the damage path) is
proven. Reproducing the full sleep duration/pose needs a real inflict via effect 0x02 (§7 O1).

**(c) POSE / HUD — OPEN.** The sleep pose (`$8E`/the enemy E3 anim) and any HUD status indicator were not
captured (§7 O4). The `battle_pose_staging.md` engine drives `$8E`; a status-pose selector was not traced.

---

## 6 — IMPLEMENTATION SPEC (for a future Phase 2 — NOT implemented this pass; see §7 O0)

The engine (`src/battle.c`) models **no** statuses today. The mandate-safe minimal slice, once a real inflict is
captured, is:

**(a) The status model** — add the 3 status bytes to the block image the engine already maintains: read/write
`block+0x1D/+0x1E/+0x1F` (they already exist in the staged `$7E` block; nothing to allocate). Immunity =
`block+0x22..+0x24`.

**(b) Inflict** — at the derived apply point (mirror `C1/DBAA`): `if ((block[0x22+cat] & mask) == 0)
block[0x1D+cat] |= mask;` with `cat`/`mask` from the effect's `CC/05CC` record (`byte+1 & 7` / `byte+2`). Bake the
`CC/05CC` table like `menu_romdata.h`. This is exercised only when a status-bearing attack is dispatched — none is
in the GATO/forest fights, so **no existing pin moves** (the whole slice is inert until such an attack exists).

**(c) Wake-on-hit** — in `battleApplyDamage` (the single place a hit lands), mirror `C1/DCAD`: on any damage `> 0`
to a combatant, `block[+0x1E] &= 0xF9`. This is the one law proven byte-exact (§5a) and is safe: with no status
ever set today, `&0xF9` is a no-op on every pin.

**(d) Cure** — the Heal/Revive item (selector 0x00, `battle_item_slice.md` O4) mirrors `C1/F036`: `+0x1D &= 0xC0;
+0x1E &= 0x00; +0x1F &= 0xC2`. Wires the item lane's open status-cure selector.

**(e) Turn-block + onset** — the `+0x1E` bit2 skip (`C1/8660`) and the `C1/E006` timers are the "asleep can't act
for N turns" behavior; defer until captured (they gate the scheduler and need the onset timer model, §7 O1/O3).

**Checks to pin (once inflict is captured):** a forced-inflict trace (the `block[0x1D+cat] |= mask` write +
immunity gate, determinism ×2) and the no-inflict baseline (natural fights: every status byte stays 0, zero pins
move). The wake-on-hit clear can be pinned NOW as an inert-safe `&0xF9` in `battleApplyDamage`.

---

## 7 — OPENS / BOUNDARIES

| # | item | status |
|---|---|---|
| **O0** | ~~Phase 2 NOT implemented.~~ **CLOSED (§8/§9).** | The real inflict is now derived end-to-end AND landed in the engine. See §8 (the staging model + the physical rider route + the oracle ledgers) and §9 (the implementation). |
| **O1** | ~~A real forced inflict~~ **CLOSED (§8).** | The clean route was NOT the pure-status executor `D4D6` (effects 0x0F/0x10, cat 4) but the **physical rider `E976`** (D306 hit-tail, `$AE4D bit2`): it inflicts a **category-1 (`+0x1E`) mask = staged-header byte+2** status, accuracy = byte+1. Forced via the `sleeppatch/` ROM (header byte+7 `|= 0x04` + byte+1 = acc + byte+2 = 0x04): GATO sleeps Crono byte-exact, immunity blocks GATO, the `E006` onset runs (arms no timer for the until-hit sleep), the `C1/8660` skip + wake-on-hit fire. Full ledgers §8.3-8.6. |
| **O2** | **The per-caller status accuracy roll.** | DBAA has 7 callers; the pure-status executor `C1/D4D6` has no roll (immunity-only), but the damage-riding callers gate on the spell hit roll + `CC/05CF`. Not exhaustively traced. |
| **O3** | **The full onset semantics `C1/E006`** (which `$AF27/$AF32/$AF3D/$AF7F` timer = which visible behavior; auto-wake duration; poison per-turn drain). | Only the ATB-slot reset (bit7) is decoded; the poison HP-drain tick was not located. |
| **O4** | **The id↔NAME map + pose + HUD.** | The bit→byte structure is pinned (§2/§3.2) and one bit's behavior proven (§5), but the human names (Sleep/Poison/Slow/Stop/Chaos/Lock/Blind…) are not tied to a ROM string table, and the status pose (`$8E`) / battle-HUD status icon were not captured. |
| **O5** | **Field persistence.** | Whether a battle status survives to the field char sheet (`$2600 + id*0x50`) / shows in the field menu was not observed (the GATO fight does not end with a live status in the capture window). |

---

## 8 — O1 CLOSED: the STAGED-HEADER model + the PHYSICAL status RIDER (a REAL inflict, end-to-end)

The O1 block ("a real forced inflict, immunity-gated, with the `C1/E006` onset") is closed. The unblocking
insight (from `battle_drop_charm.md` — `$AE4D` = the staged header byte+7) is that the status a physical attack
inflicts is NOT read from `CC/05CC` (that table's `D4D6` pure-status executor only carries cat-4 effects, §3.2
0x0F/0x10) but from the **STAGED ATTACK HEADER** `$AEE6`, via a dedicated rider on the `D306` physical hit-tail.

### 8.1 The header STAGING model (dis.py `1D5DC` PC / `1D916` enemy) — byte-exact, oracle-observed

> **PORTED 2026-08-22** (`battleStageAtkHeader`, src/battle.c). The 12-byte copy + the `C1/EB0E` mirror
> now run at dispatch, and our staged bytes are byte-identical to the table below —
> `0300003A3B013E8080080480` for a PC physical and `0300003A3B043E1000120080` for GATO. Two consumers were
> switched off literals onto the header: the miss gate now tests `$AE4D & 0x20` (instead of a structural
> "techs go through another function" argument) and `C1/D174`'s two stat selectors are read from
> `$AEE9`/`$AEEA` (instead of hardcoded 0x3A/0x3B). Behaviour-neutral today and verified so: the forest
> fight's whole RNG stream is byte-identical to the pre-change baseline. Pinned by
> `field-battle-atk-header` (2 mutations, both killed). The enemy record index is resolved through the
> `$B18C` law (`CC/88CA[act*11+4]`) that the damage branch already used, which lands on the same bytes as
> this section's `CC/88CE[$B2C7+$B2C3]` expression.
>
> ⚠ **`C1/D629`'s `ORA $B2CA` is deliberately NOT ported, and the reason is a trap worth keeping.**
> `$B2CA` is staged by `C1/D58D`, which this engine does not model, so the byte holds whatever the seed
> WRAM left. That is not harmless: `forest_pre` carries `$B2CA = 0x29`, whose **bit 5 is set**, so OR-ing
> it closed the "cannot miss" gate and silently disabled EVERY PC hit roll in that fight. It surfaced only
> as five missing RNG draws (the stream diverging at index 0x26 from a `max=99` roll to a `max=1` bonus).
> The oracle reads `$B2CA = 00` for every action measured, so the OR is inert there — modelling it against
> an unstaged byte is strictly worse than leaving it out. Port `C1/D58D` first if an attack ever needs it.
>
> Still NOT reached: bytes **+1/+2** (the status rider's accuracy and mask) read 00 for every action in
> every fixture, so the natural rider still never fires — `CT_BATTLE_STATUSTEST` remains the only way to
> exercise it. Staging the header was necessary for that arm but is not sufficient.

Two stagers copy a **12-byte** attack header into `$AEE6..$AEF1`, keyed by the acting weapon/tech:
- **PC** (`C1/D5EF`, writer `C1:D5F6`): `$AEE6[0..11] = CC/213F[weaponId × 12]`. (`1D5DF LDA #0C` = the **12**-byte
  stride; `1D5EF LDA $CC213F,X / STA $AEE6,Y / … CPY #0C`.) ⚠ this is a **12**-stride view of `CC/213F`, distinct
  from the 17-stride `C1/1BF81` copy (that stages a *secondary* effect header for the `1BF46` pure-status dispatch).
- **ENEMY** (`C1/D945`, writer `C1:D94C`): `$AEE6[0..11] = CC/89C6[techId × 12]`, where `techId = CC/88CE[$B2C7+$B2C3]`
  (`1D923`); separately `$B2CC = CC/88D1[…]` = the effect id, and `JSR C1/EB0E` mirrors `$AEED/$AEEE → $AE4D/$AE4E`.

`C1/EB0E` (`1EB0E`): `$AE4D = $AEED` (**byte+7**), `$AE4E = $AEEE` (byte+8). The **12-byte header layout** (from the
oracle-staged Crono physical `03 00 00 3A 3B 01 3E 80 80 08 04 80` and GATO's attack-on-Crono `03 00 00 3A 3B 04 3E
10 00 12 00 80`):

| off | addr | role | Crono / GATO | reader |
|---|---|---|---|---|
| +0 | `$AEE6` | attack class (0x03 = physical) | 03 / 03 | — |
| **+1** | `$AEE7` | **STATUS accuracy** (the rider roll threshold) | 00 / 00 | `C1/D14F → $16` |
| **+2** | `$AEE8` | **STATUS mask** (the bit the rider ORs into `+0x1E`) | 00 / 00 | `C1/D14F → $18` |
| +3 | `$AEE9` | HIT stat selector (0x3A) | 3A / 3A | `C1/D174` (miss dossier) |
| +4 | `$AEEA` | EVADE stat selector (0x3B) | 3B / 3B | `C1/D174` |
| +5 | `$AEEB` | (damage-type/element sel) | 01 / 04 | — |
| +6 | `$AEEC` | DEFENSE selector (0x3E phys / 0x3C mag) | 3E / 3E | `C1/E65F` |
| **+7** | `$AEED`→`$AE4D` | **FLAGS**: bit1 charm · **bit2 status-rider** · bit4 (enemy→D306) · bit5 no-acc · bit7 (PC) | 80 / 10 | `D306` tail |
| +8 | `$AEEE`→`$AE4E` | flags2 | 80 / 00 | — |
| +9 | `$AEEF` | POWER (damage base) | 08 / 12 | `C1/E0F5`/`E27B` |
| +10 | `$AEF0` | (2nd power/coef) | 04 / 00 | — |
| +11 | `$AEF1` | weapon byte (`$1A`, popup-type sel) | 80 / 80 | `C1/E8E2` |

So the **status the physical carries lives in header byte+1 (accuracy) / byte+2 (mask), gated by byte+7 bit2** —
all in the staged `$AEE6` block, NOT in `CC/05CC`. In a stock header byte+1 = byte+2 = 0 and byte+7 bit2 = 0, so
the rider is inert (§8.2 (b)).

### 8.2 The status ROUTE — the `D306` physical hit-tail (dis.py `1D338`/`1D14F`/`1E976`)

```
1D335 JSR C1/DCAD                     ; damage compute + WAKE-ON-HIT (block+0x1E &= 0xF9, §5a)
1D338 LDA $AE4D / AND #04 / BEQ 1D345 ; header byte+7 bit2 (STATUS-RIDER) set?
1D33F   JSR C1/D14F                    ;   $16 = $AEE7 (byte+1 = accuracy) ; $18 = $AEE8 (byte+2 = mask)
1D342   JSR C1/E976                    ;   the roll + inflict:
        1E978 roll = rand(0..99) on $26 (AF22)  ; 1E97F LDA $16 / CMP roll / BCC skip  (skip iff acc < roll)
        1E985 LDA #01 / STA $16          ;   ** FORCE category = 1 (+0x1E) ** ; $18 still = mask
        1E989 JSR C1/DBAA                ;   the immunity-gated inflict (§3.1) + JSR E006 onset
1D345 LDA $AE4D / AND #02 → E9D5 (charm, §1)
```
Two laws pinned here:
1. **The physical rider ALWAYS inflicts category 1 (`+0x1E`)** — `E976` hard-writes `$16 = 1` before `DBAA`
   (`1E985`). So a melee-inflicted status is always a `+0x1E` byte-1 status; the mask (which bit) = header byte+2.
   The **sleep bit `+0x1E` bit2 (mask 0x04)** is exactly the `C1/8660` can't-act gate (§5b) + the `C1/DCAD` wake
   bit (§5a) — so a `byte+2 = 0x04` melee rider = SLEEP.
2. **Accuracy = header byte+1**, `MISS iff byte+1 < rand(0..99)` (`BCC` on `$16 < $0E`). `byte+1 = 0x63` (99) ⇒
   `99 ≥ rand(0..99)` always ⇒ deterministic inflict (the roll STILL draws `$26`).

`DBAA` then applies it immunity-gated (§3.1): `X = targetBlock($B1F6) + cat`; `if (block[0x22+cat] & mask) == 0`
(not immune) → `block[0x1D+cat] |= mask` + `JSR E006`; else (immune, cat 1) → `JMP DC5F` (sets `$40 = 0x80`, a
"blocked" marker), no status write. `E006` (§4.3): cat 1, `$18 & 0xF0` gates the timer arm — **mask 0x04 (bit2)
arms NO timer** (`1E01D BIT #F0 → BEQ → JMP E0F4`), an until-hit sleep; masks bit4-7 would arm `$AF27/$AF32/$AF3D/
$AF7F` + the `$AFB6/$AFC1/$AFCC/$B00E` flag.

### 8.3 The forced-inflict recipe (`sleeppatch/`, the header-patch analogue of `statuspatch/`)

Patch the two **staged-header SOURCE** records (12-byte stride) so a physical carries the sleep rider:
- **GATO → Crono** = `CC/89C6[id 1]` (file `0x0C89D2`): +1 → `0x63` (acc 99), +2 → `0x04` (mask +0x1E bit2), +7
  `0x10 → 0x14` (`|= 0x04` = AE4D bit2). (Also `CC/89C6[id 0]` @ `0x0C89C6` for GATO→Marle, same, to sleep both PCs.)
- **Crono physical** = `CC/213F[id 58]` (file `0x0C23F7`): +1 → `0x63`, +2 → `0x04`, +7 `0x80 → 0x84`.
`mk_sleeppatch.py` builds it; run the oracle from `sleeppatch/`. (byte+1/+2 are 0 in stock and read ONLY by the
rider `D14F` — patching them does not perturb the stock damage, oracle-verified: Crono still 17, GATO still 14.)

### 8.4 LEDGER — the inflict + the immunity negative (`--from crono_normal_ready.state --input "100:10,0:20,100:10,0:600" --diff --writer`)

```
f43   AE4D=84(C1:D632) AEE7=63/AEE8=04(C1:D5F6)  $26 1C->1E   ; Crono ATTACKS GATO: header staged w/ rider (bit2)
      614B(GATO +0x1E)=00 (writer C1:DCB9=wake)              ; **IMMUNITY NEGATIVE**: GATO NOT slept
f171  6130 4C->3B                                            ; Crono's normal damage lands (-17); patch non-destructive
f172  5E4B(Crono +0x1E) 00->04 (writer C1:DC0A)              ; **SLEEP INFLICTED** on Crono (cat1 mask04)
      AE4D=14(C1:EB15) AEE7=63/AEE8=04(C1:D94C)              ; GATO's patched header staged (enemy stager D94C)
f397  5E30 46->38                                            ; GATO's normal damage on Crono (-14)
```
- **Positive**: `$5E4B` (Crono `+0x1E`) `00→04`, writer `C1:DC0A` = the `DBAA` inflict tail. Category 1, mask 0x04
  = the sleep bit. GATO's own attack both DAMAGES and SLEEPS Crono (DCAD wake THEN E976 inflict → net slept).
- **Immunity negative**: `$614B` (GATO `+0x1E`) stays `00` — GATO `block+0x23 = 0xFF` (from the ROM enemy record
  `CC/4700[0x92×23]+4`) & mask 0x04 ≠ 0 → immune → the `DBAA` gate blocks it.
- **Onset**: `$AF27/$AF32/$AF3D/$AF7F` + `$AFB6/$AFC1` UNCHANGED at f172 — sleep (mask 0x04) arms no timer.

### 8.5 LEDGER — the can't-act skip + wake-on-hit (both PCs patched; long idle)

```
f172  5E4B=04  ; f508 5ECB(Marle +0x1E)=04           ; both PCs slept
f668+ 6130 = 3B FLAT through f1561 (~900f)           ; **SKIP**: GATO HP never drops -> NO PC ever acts (all
      while Crono HP 70->25, Marle 65->56 keep dropping ;   PC turns skipped, C1/8660 -> 881C, battle autonomous)
f1170 5E4B 04->00 (writer C1:DCB9)                   ; **WAKE-ON-HIT** of the REAL sleep (DCAD &0xF9)
f1171 5E4B 00->04 (writer C1:DC0A)                   ;   same rider attack re-inflicts (DCAD-then-E976 order)
```
Both PCs slept ⇒ every PC turn is skipped ⇒ the battle runs GATO's attacks with ZERO input, GATO HP flat (no PC
damages him). A subsequent rider hit WAKES then re-inflicts. Proven byte-exact.

### 8.6 The VISUAL of a slept PC (`--peek`, both PCs slept @ ~f700)

- **A yellow STAR/sparkle OBJ floats above the head** (the classic CT sleep indicator).
- The PC switches to a distinct **front-facing upright "asleep" pose** (vs the normal 3/4 battle-ready stance).
- **NO command window** — the slept PC never enters the command menu (the `C1/8660` skip's HUD manifestation:
  the normal frame shows Crono's Att/Tech/Item box + active-actor cursor; the slept frame shows neither).
The pose + star are the anim-engine's render of the status bit (the `$8E`/OBJ selector for a slept combatant was
NOT traced — a render-side remainder, §7 O4). The command-window suppression IS implemented (§9).

---

## 9 — IMPLEMENTATION (LANDED in `src/battle.c`; all inert in every pinned fight)

- **Status model init** (`stageAll` (v)): mirror `C1/EA9D` — ZERO the status byte `block+0x1E` (the one the engine
  acts on) for all combatants at combat start. REQUIRED: the field/seed leaves STALE bits in `+0x1E` (e.g. the
  forest seed's enemy slot2 = 0x07 → bit2 set) that the skip/wake would misread as a live sleep → the fight
  diverges. Only `+0x1E` (not `+0x1F..+0x26`) is zeroed: the immunity `+0x22..+0x26` is staged in (iv), and the
  item-cure fixture `heal_cure.raw.wram` intentionally pre-sets a `+0x1F` permanent bit its `&0xC2` cure keeps.
  With it the `field-battle-drop-roll`/forest fights are byte-identical again (was: `roll=22/42/0` vs `51/78/88`).
- **The rider `battleStatusRider`** (mirrors `D14F`+`E976`+`DBAA`+`E006`): cat 1 forced, mask 0x04; `roll =
  battleRng(0..99)`; inflict iff `acc ≥ roll` AND `(block[0x22+1] & mask) == 0` (immunity) → `block[0x1E] |= mask`.
  Sleep arms no `E006` timer. Called at the span-end apply seam AFTER `battleApplyDamage` (so wake runs first),
  for ANY landed physical (PC↔enemy, both `D306`). GATED by `CT_BATTLE_STATUSTEST=<acc>` — the header-staging
  analogue (our engine does not model `$AEE6/$AE4D`), so INERT (no `$26` drawn) in every pinned fight.
- **Wake-on-hit** (`battleApplyDamage`): `if (dmg > 0) block+0x1E &= 0xF9` — `C1/DCAD`. Inert (`&0xF9` on 0 = 0).
- **Can't-act skip** (`C1/8660`): a slept seat (`+0x1E` bit2) in the PC ready-edge (step E) reloads its ATB and
  does NOT enqueue (⇒ `CtBattleSeatQueued` false ⇒ no command box = the observed HUD); `battleOpenAction` mirrors
  the general gate for an enemy ATB turn (`!reactive`). Inert (bit2 never set in a pinned fight).
- **Immunity** — the `DBAA` gate inside the rider; GATO's `+0x23 = 0xFF` staged natively from the enemy record.
- **Cure** already exists (`battleApplyItemEffect`: `+0x1D &= 0xC0 / +0x1E &= 0x00 / +0x1F &= 0xC2`, `C1/DB5E`).
- **Checks**: `field-battle-status-inflict` (STATUSTEST=99 → the roll + the `+0x1E=04` bit + the skip + the wake,
  determinism ×2) and `field-battle-status-immunity` (GATO `imm=FF blocked=1 +0x1E=00`). Suite: FULL 119/0/3 quick
  97/0/1 — zero pins move (the hook is off everywhere else; the `stageAll` zero restored the forest pins).
- **~~OPEN (render)~~ → DERIVED, see [`battle_status_render.md`](battle_status_render.md)** (not yet implemented —
  that dossier carries the mandate-safe implementation plan + the `field-battle-status-render` check to pin it).
  Both halves come from ONE table-driven machine, the per-combatant status scan **`C1/2F97`**, which emits
  (a) the **pose class `$A440`** — the PC pose tick `C1/2E00` (`12E6B`) shifts `ptrA/ptrB` by `$A440×4`, so sleep
  (class **0x1A** → window **+0x68**) selects Crono's E3 assemblies `{0xCC,0xCD}` / Marle's `{0x56,0x57}`
  (byte-exact vs the observed OAM) instead of the normal class-3 `+0x0C` idle window — and (b) the **icon state
  `$A441`** (sleep = **4**) which arms the icon OBJ builder `CF/EE2C` off the record table `CC/F6D4`. Both come
  from the 4-byte record table **`CC/F77F`** (`{blockByteOff rel. $5E4A, mask, iconState, poseClass}`); sleep is
  row 2 (`01 04 04 1A` = `+0x1E` bit2). ⚠ **§8.6's "floating STAR" is CORRECTED**: the icon does not move — it is
  fixed at `liveFoot + (−8,−48)` and blinks between tiles **0x1A6 ↔ 0x1C6** every **10** frames (`CC/F5FF`),
  gated off during a frozen span (`$A013`). Its pixels are uncompressed ROM at **`$D1/3900`** and its palette is
  already `ctBattleAnimPal[2]`. The HUD (no command window) IS implemented.

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-9e84b785-…/scratchpad/`
- `ct_probe.exe` (= build/ct.exe). Base `crono_normal_ready.state` (a properly-staged mid-battle GATO state:
  `AF0A = 00 01 FF FF FF FF 92 ..`).
- `gato_sleep.state` = base + GATO block `+0x1E ($614B) |= 0x04` (file offset `0x45 + 0x614B`; the state is a raw
  LakeSnes stream, WRAM at file 0x45, no CTLS/CRC). `crono_sleep.state` = base + Crono `+0x1E ($5E4B) |= 0x04`.
- `statuspatch/` = a ROM copy (`Chrono_Trigger_USA_SNES.sfc` + `ct_assets.dat` + `ct_probe.exe` + `SDL2.dll`) with
  the 6 physical effect-headers' byte+7 `|= 0x02` (`$AE4D` bit1) — the E9D5=Charm proof (§1.3). Run from that dir.
- **`sleeppatch/`** (§8.3, the O1 close) = a clean ROM copy built by **`mk_sleeppatch.py`** with the two staged-
  header SOURCES patched to carry the SLEEP rider: `CC/89C6[1]` @ `0x0C89D2` (GATO→Crono) and `CC/89C6[0]` @
  `0x0C89C6` (GATO→Marle) and `CC/213F[58]` @ `0x0C23F7` (Crono physical), each `+1 = 0x63` (acc) / `+2 = 0x04`
  (mask +0x1E bit2) / `+7 |= 0x04` (AE4D bit2). Run the oracle from that dir; `slept.png` = the slept-PC render
  (star + pose + no command window, §8.6).
- Disasm anchors (this pass): PC header stager `0x1D5EF` (12-byte `CC/213F[weap×12]` → `$AEE6`, writer `C1:D5F6`);
  ENEMY header stager `0x1D945` (`CC/89C6[tech×12]`, writer `C1:D94C`); `$AE4D = header byte+7` mirror `0x1EB0E`;
  the physical status rider `D14F` `0x1D14F` (loads `$AEE7`/`$AEE8`) → `E976` `0x1E976` (roll + force cat 1 +
  DBAA); executor dispatch `0x1BF46` (`CC/05CC` byte+0: 0x80→D490 dmg / 0x04→D4AD cure / 0x40→D4D6 status);
  status inflict `0x1DBAA`; onset `0x1E006`; scheduler gate `0x18660`; combat reset (status zero) `0x1EA9D`;
  wake-on-hit `0x1DCAD` (`0x1DCB1`). Earlier: charm `0x1E9D5`; def table `CC/05CC` (`0xC05CC`); cure `0x1F036`.
- Method: force the melee status rider = patch the staged-header SOURCE (`CC/213F`/`CC/89C6`) byte+7 `|= 0x04` +
  byte+1 = acc + byte+2 = mask (via `mk_sleeppatch.py`); observe = `--monitor "5E4B:1,614B:1" --diff --writer`
  (inflict writer `C1:DC0A`, wake `C1:DCB9`, immunity = GATO `+0x23=0xFF` blocks); skip = both PCs slept, GATO HP
  flat over a long idle; visual = `--peek` @ ~f700.

---

## NATURAL HEADER STAGING — OBSERVED 2026-08-15 (was "underived, unreachable")

The port gates its status/charm riders behind `CT_BATTLE_STATUSTEST` / `CT_BATTLE_CHARMTEST` because
"our engine does NOT model the attack-header staging (`$AEE6`/`$AE4D`), so the natural fire is
unreachable (OPEN)". The staging has now been WATCHED running in an ordinary fight, so it is no
longer underived.

Repro: `saves/status_aplicado.state` (a live battle carrying status already: seat 0 `+0x1D = 0x80`,
seat 2 `+0x1E = 0x40`), driven with `0:40,100:2` x40 and

```
build/ct.exe <rom> --monitor "AEE6:12,5E4B:1,5F4B:1" --from saves/status_aplicado.state   --btnscript "$S" --writer
```

**Three writers re-stage the 12-byte header at `$AEE6` during normal combat**, each time an action
resolves:

| writer | note |
|---|---|
| **`C1:D5F6`** | the loop `C1/D5EF` this file already identified (`LDA $CC213F,X / STA $AEE6,Y / CPY #0C`) -- the 12-stride view of `CC/213F`. Now confirmed to be the NATURAL path, not a test-only one. |
| **`C1:D83F`** | second staging site |
| **`C1:D94C`** | third staging site |

Observed header values (12 bytes at `$AEE6`), one per action:

```
f0     03 00 00 3A 3B 04 3E 10 00 0C 00 80
f155   03 00 00 3A 3B 02 3E 80 80 06 04 80   (C1:D5F6)
f306   03 00 00 3A 3B 01 3E 80 80 08 04 80   (C1:D5F6)
f599   03 00 00 3A 3B 04 3E 90 00 10 00 00   (C1:D83F)
f1607  03 00 00 3A 3B 04 3E 10 00 0C 00 80   (C1:D94C)
```

Bytes 0-4 are stable across actions (`03 00 00 3A 3B`); bytes 5 and 7-11 vary per action -- byte 5
takes 04/02/01 and byte 10 takes 00/04, which is where a rider bit would live.

Also seen live, confirming §6 rather than re-deriving it: at f1607 seat 2's `+0x1E` was written by
**`C1:DCB9`**, i.e. inside the damage routine `C1/DCAD` -- the wake-on-hit `&= 0xF9`.

**What this unblocks and what it does not.** The mechanism and its source table are now pinned, so
the remaining work is a PORT: stage the 12-byte header from `CC/213F` when an action resolves and
let the existing rider read it, which retires both env gates. That is battle-engine work, not a
patch, and it is not attempted in this note.


---

## THE STAGING PIPELINE, DISASSEMBLED + MEASURED — 2026-08-15 (supersedes the "17-byte" reading)

The section above watched the staging run. This one opens the ROM behind it. Probe:

```
ct_probe <rom> --monitor "AE4D:1,AE4E:1,AEE6:12" --from saves/status_aplicado.state          --btnscript "<0:40,100:2 x40>" --writer --diff
ct_probe <rom> --monitor "B2C8:1,B2C3:2,B2C7:1,AE93:1,AE4D:1" --from saves/status_aplicado.state ... --writer --diff
```

### S1. THREE action classes, one shape — not three paths to one table

`dis.py 1D5DB / 1D809 / 1D91C` (m=1 x=0) show three byte-identical preambles, each pairing an ID
table with its own RECORD table. All three end in the same 12-byte copy to `$AEE6`:

| store (writer PC) | id table -> `$B2C8` | record table | terminator |
|---|---|---|---|
| `1D5F3` (`C1:D5F6`) | `CC/1BF0` (+ `CC/1BF3` -> `$B2CC`) | `CC/213F` | `BIT #80` -> `JMP C1/D71E` |
| `1D83C` (`C1:D83F`) | `CC/6FCE` | `CC/7AC9` | `CMP #FF` -> `JMP C1/D8AA` |
| `1D949` (`C1:D94C`) | `CC/88CE` | `CC/89C6` | `CMP #FF` -> `JMP C1/D9B0` |

The common tail is `TAX / STX $0028 / LDA #0C / TAX / STX $002A / JSR C1/C90B / LDX $2C /
STX $B2C5 / TDC / TAY / LDA <table>,X / STA $AEE6,Y / INX / INY / CPY #000C / BCC`.

**`C1/C90B` is a 16-bit shift-add MULTIPLY** (`dis.py 1C90B`): `LDX #0010 / STZ $2C / STZ $2E /
ROR $2A / BCC+ / CLC / LDA $28 / ADC $2E / STA $2E / ROR $2E / ROR $2C / DEX / BNE`, i.e.
`$2C:$2E = $28 * $2A`. With `$2A = 0x0C` the index is **`$B2C8 * 12`**, and `$B2C5` keeps it.

### S2. STRIDE 12, MEASURED — the `17-byte header` in `battle_drop_charm.md:108` is a different site

`battle_drop_charm.md` records the header as 17 bytes staged at `0x1BF81`. `dis.py 1BF81` confirms
that site is REAL (`$B18C * 17` via `ASL A x4 + ADC $0E`, `LDA $CC213F,X / STA $AEE6,Y / CPY #0011`)
-- but it is **not the combat path**: no run of `status_aplicado.state` ever reports it as a writer
of `$AEE6`, and every header the oracle DID stage sits at an exact multiple of **12** in the table
its own writer PC reads, while the x17 reading is integral for none of them:

| frame | writer | table | byte offset | /12 | /17 |
|---|---|---|---|---|---|
| f155  | `C1:D5F6` | `CC/213F` | 708 | **59.0** | 41.65 |
| f306  | `C1:D5F6` | `CC/213F` | 696 | **58.0** | 40.94 |
| f599  | `C1:D83F` | `CC/7AC9` | 72  | **6.0**  | 4.24 |
| f1607 | `C1:D94C` | `CC/89C6` | 0   | **0.0**  | 0.0 |

Cross-confirmation of the engine's existing constant: `battle.c:1840` uses `CC/213F[0x3A]+9` POW=8,
and the f306 staged header's byte[9] is `08`. `CC/213F[0x3a] = 03 00 00 3A 3B 01 3E 80 80 08 04 80`
matches the f306 staging byte for byte. The stride-12 assumption already in the engine is CORRECT.

### S3. `$AE4D` / `$AE4E` = header bytes 7 and 8 (`C1/EB0E`, both directions)

```
1EB0E  TDC / LDA $AEED (= $AEE6+7) / STA $AE4D
1EB15      LDA $AEEE (= $AEE6+8) / STA $AE4E / RTS
```
Observed, every action: `f155 AEE6=...3E|80|80... -> AE4D=80 AE4E=80`; `f599 ...3E|90|00 ->
AE4D=90 AE4E=00`; `f1607 ...3E|10|00 -> AE4D=10 AE4E=00`. Writers `C1:EB15`/`C1:EB1B` (the two
stores above), plus `C1:D632` = a second `$AE4D` mirror on the PC path, and **`C1:8E06` CLEARS
`$AE4D` to 00** before the enemy action (f1606). Gate consumers are already pinned elsewhere:
`$AE4D` bit1 -> `C1/E9D5` charm (`1D345`), bit2 -> `C1/E976` rider (`1D338`), bit5 = no-accuracy
(`battle_miss_evade.md:56`).

### S4. The riders are INERT in every fight we own — but NOT in the ROM

`$AE4D` over the whole `status_aplicado` run takes only `00 / 10 / 80 / 90`: **bit1 and bit2 never
set**. So porting the staging retires `CT_BATTLE_CHARMTEST` / `CT_BATTLE_STATUSTEST` while changing
no observable behavior in any pinned fight -- the riders correctly stay silent. They are not dead
ROM-wide. Scanning byte+7 within DERIVED bounds (`CC/213F` ends at `CC/2A05`, the effect table
`C1/EB1C` reads -> 187 records; `CC/7AC9` ends at `CC/88CE` -> 299):

| table | records | byte+7 bit1 (charm) | byte+7 bit2 (rider) |
|---|---|---|---|
| `CC/213F` | 187 | 51 | 50 |
| `CC/7AC9` | 299 | 0 | 30 |
| `CC/89C6` | 100 (unbounded above) | 34 | 38 |

**Grading consequence:** a check must pin the STAGING invariant (the 12 header bytes + `$AE4D`/
`$AE4E` per resolving action), because nothing in a pinned fight makes a rider fire.

### S5. (SUPERSEDED BY S7) — the open question, and the addressing error that caused it

`$B2C8` per action is measured and unambiguous **as a value**: `0x3A` and `0x3B` on the two PC
actions, `0x06` class-2, `0x00` the enemy action (each confirmed by the staged bytes matching that
record exactly). WHICH PC owns `0x3A` vs `0x3B` was NOT measured here -- no attacker id was in the
monitor set; what IS measured is that the two PC actions carry two DISTINCT `$B2C3` pointers
(`0x3305` and `0x1D05`) and stage two DISTINCT records. (`battle.c:1841`'s pre-existing comment
records `$B2C8=0x3A` for Crono seat 0; that is prior evidence, not a measurement of this run.)
What is NOT derived is how the ROM computes it. The disasm reads `X = $B2C7 + $B2C3` (16-bit,
`REP #20 / CLC / ADC $B2C3 / TAX`) then `LDA $CC1BF0,X`, but that arithmetic reproduces NEITHER
observed id: with `$B2C3=0x3305, $B2C7=0` -> `CC/1BF0[0x3305]` = `0x64`, not `0x3A`; with
`$B2C3=0x1D05, $B2C7=0` -> `0xFF`, not `0x3B`. Only the trivial enemy case (`CC/88CE[0]` = `0x00`)
matches. So the id-table addressing is **not** a plain byte offset -- either `$B2C3` is re-written
between the monitor's end-of-frame sample and the read, or the index is scaled/banked. NOT GUESSED.

A tempting shortcut was TESTED AND REFUTED: `rec = 0x3A + seat` is not a per-PC series --
`CC/213F[0x39/0x3A/0x3B/0x3C/0x3D]` have formula types `05/01/02/01/05` and POW `9/8/6/29/37`,
which is not "one basic attack per character".

### S6. What this exposes in the engine (NOT fixed here)

`battle.c:1840` hardcodes `CC/213F[0x3A]` for **every** PC basic attack. The oracle stages TWO
different records across the two PC actions of this run -- `0x3A` (POW 8, formula type 1) and `0x3B`
(POW **6**, type 2) -- so one hardcoded record cannot be right for both. S7 closes which PC gets
which: the hardcode is CORRECT for char 0/3/4/6 and WRONG for char 1 and 2 (`0x3B`, POW 6) and
char 5 (`0x39`, POW 9).


---

## S7. BOTH GAPS CLOSED — the full chain, ROM + oracle, both directions

### S7.1 The addressing error in S5

S5 could not reproduce `$B2C8` because it read the monitor's 2-byte field as a little-endian word.
**`--monitor` prints a byte range in ADDRESS order.** `$B2C3` shown as `3305` is `0x0533`, not
`0x3305`. That also resolves the impossibility S5 half-noticed: `$B18C` is loaded 8-bit, so
`$B18C * 11 <= 2805`, and `0x3305` = 13061 was out of range from the start.

### S7.2 `$B2C3` = `$B18C * 11` — an 11-byte ACTION record, not a pointer

`dis.py 1D523` (PC), `1D7C4` (class-2), `1D8D1` (enemy) are three byte-identical routines:

```
JSL FD/ACFD / TDC / TAX / STX $AE7B
LDA $B18C / TAX / STX $28 ; LDA #0B / TAX / STX $2A ; JSR C1/C90B ; STX $B2C3   ; $B2C3 = $B18C*11
STZ $B2C7 / STZ $B2C9 / STZ $B2CA / STZ $AD9B                                   ; cursor := 0
... flag bits from the record ...
LDA $B2C7 / REP #20 / CLC / ADC $B2C3 / TAX / LDA <idtable>,X / STA $B2C8
```

| class | entry | record base (x11) | flags | id list | header table |
|---|---|---|---|---|---|
| PC | `C1/D523` | `CC/1BEB` | `+0` (`AND #7F`), `+1` (`AND #40/#20`) | `+5` = `CC/1BF0` | `CC/213F` |
| class-2 | `C1/D7C4` | `CC/6FCA` | `+0` (`AND #40`) | `+4` = `CC/6FCE` | `CC/7AC9` |
| enemy | `C1/D8D1` | `CC/88CA` | `+0` (`AND #40`) | `+4` = `CC/88CE` | `CC/89C6` |

`$B2C7` is the CURSOR into the id list; an id with bit7 set is the TERMINATOR (`BIT #80` ->
`JMP C1/D71E`, which is what then advances `$B2C7`). Verified against every observed action:
`$B2C3 = $B18C*11` holds for `$B18C` = `0x79`->1331, `0x77`->1309, `0x06`->66, `0x00`->0, and
`idtable[$B2C3 + $B2C7]` reproduces `$B2C8` = `0x3A`, `0x3B`, `0x06`, `0x00`. (Two apparent
mismatches at cursor 3 are the intra-frame ordering artifact: the id `0x80` was read at cursor 1
or 2, and `C1/D71E` advanced the cursor to 3 before the frame ended.)

### S7.3 The class SELECTOR is `$AEE3` (`dis.py 18E02`)

```
18E03  STA $AE4D / STA $AE4E     ; A=0 -- CLEAR BOTH first  (the C1:8E06 clear seen at f1606)
18E09  LDA $AEE3 / CMP #02
18E0E  BEQ ->18E15
18E10  JSR C1/D8D1               ; $AEE3 != 2 -> ENEMY   (CC/88CA -> CC/89C6)
18E15  JSR C1/D7C4               ; $AEE3 == 2 -> CLASS-2 (CC/6FCA -> CC/7AC9)
```

Measured: `$AEE3 = 02` on both class-2 stagings (f599, f1387, writer `C1:D81D`), `= 01` on the
enemy staging (f1607, writer `C1:D92A`). The PC path is dispatched separately (`C1/BF7D`), so
`$AEE3` does not gate it -- observed `00` and `01` on PC stagings indifferently.

Each entry has exactly ONE caller in the whole ROM (byte scan for `JSR`/`JMP`/`JSL`):
`C1/D523 <- C1/BF7D`, `C1/D7C4 <- C1/8E15`, `C1/D8D1 <- C1/8E10`.

### S7.4 The PC action id: `$B18C = CC/2583[charId]` (`dis.py 1BFF3`)

```
1BFF3  LDA $B18B / TAX ; LDA $AEFF,X / CMP #FF / BEQ ; TAX
1BFFF  LDA $CC2583,X / STA $B18C ; JSR C1/BF79   (-> C1/D523)
```

`$AEFF+seat` is the seat->charId map the engine already builds (`battle.c:521`, from `$2980`).
`CC/2583[charId]` resolves the whole PC series, self-checked by record byte+0 == charId:

| charId | `$B18C` | `CC/1BEB[act]` | recId | POW(+9) | type(+5) | byte+7 |
|---|---|---|---|---|---|---|
| 0 | `0x75` | `00 00 80 04 00 3A 80 80 01 00 00` | `0x3A` | 8 | 01 | 80 |
| 1 | `0x76` | `01 00 80 05 00 3B ...` | `0x3B` | **6** | 02 | 80 |
| 2 | `0x77` | `02 00 80 05 00 3B ...` | `0x3B` | **6** | 02 | 80 |
| 3 | `0x78` | `03 00 80 04 00 3A ...` | `0x3A` | 8 | 01 | 80 |
| 4 | `0x79` | `04 00 80 04 00 3A ...` | `0x3A` | 8 | 01 | 80 |
| 5 | `0x7A` | `05 00 80 04 00 39 ...` | `0x39` | **9** | 05 | 80 |
| 6 | `0x7B` | `06 00 80 04 00 3A ...` | `0x3A` | 8 | 01 | 80 |

**This also identifies the actors in `status_aplicado.state`**: the observed `$B18C` values `0x79`
and `0x77` are chars **4 and 2**, not Crono+Marle. The earlier note's Crono/Marle attribution (and
the retraction that softened it) were both about the wrong pair; the capture is a late-game state.

### S7.5 The complete staging chain

```
PC:      seat -> $AEFF[seat] = charId -> CC/2583[charId] = $B18C
                -> CC/1BEB[$B18C*11 + 5 + $B2C7] = $B2C8   (bit7 = terminator)
                -> CC/213F[$B2C8*12] = the 12-byte header -> $AEE6[0..11]
non-PC:  $B18C (AI) -> ($AEE3==2 ? CC/6FCA : CC/88CA)[$B18C*11 + 4 + $B2C7] = $B2C8
                -> ($AEE3==2 ? CC/7AC9 : CC/89C6)[$B2C8*12] -> $AEE6[0..11]
then:    C1/EB0E: $AE4D = header[7] ; $AE4E = header[8]     (cleared at C1/8E03 first, non-PC)
gates:   $AE4D bit1 -> C1/E9D5 charm ; bit2 -> C1/E976 rider ; bit5 = no-accuracy
```

For the two non-PC actions, `$B2C8 == $AE93` was measured (`0x06` and `0x00`) -- which is the index
`battle.c:1849` already uses for enemy POW, so the engine's existing non-PC record resolution needs
no change; only the PC side does.

---

# THE TIMED-STATUS ARMS ($F0) — MAPPED, 2026-09-11, and it is THREE pieces, not one

`battle.c`'s `g_btStatusTimedUnported` counter has been the holding pattern for `C1/E006`'s four
`BIT #80/#40/#20/#10` arms: the status bit is set (the ROM sets it first, unconditionally) and the
timer arm is COUNTED rather than faked, because arming a timer nothing ticks would inflict a timed
status that never expires. That was the right call. This section is the map the port needs before it
stops being the right call.

## 1. The arms — `C1/E006`

```
E006  LDA $B1FD / TAX                the target's byte index
E00A  REP #20 / XBA / LSR A / TAY    a second, halved index (the $5E64 block view)
E012  LDA $16 / CMP #01 / BNE ->E070 this whole arm is gated on $16 == 1
E01B  LDA $18 / BIT #F0 / BEQ ->E0F4 no timed bit -> nothing to arm
E024  BIT #80 -> $AF27,X := $B0D4,X            ; $AFB6,X := 1
E036  BIT #40 -> $AF32,X := $B0DF,X + $5E64,Y  ; $AFC1,X := 1   <- the only arm with a stat term
E04C  BIT #20 -> $AF3D,X := $B0EA,X            ; $AFCC,X := 1
E05E  BIT #10 -> $AF7F,X := $B12C,X            ; ...
```

Each arm is `timer := a per-slot base` plus `run flag := 1`. Only the `#40` arm adds a block stat
(`$5E64,Y`). The four are otherwise the same instruction shape.

## 2. The tick — `C1/823C`-`8284`, and ONLY bit `$80` has one

⭐ **A full-ROM operand scan settles this.** Across banks `$C0`-`$FF`, every absolute and long
reference to the four timer bytes:

| timer | sites in bank `$C1` | readers |
|---|---|---|
| `$AF27` (bit 80) | `8250 LDA,X` · `8254 STA,X` · `8260 LDA,X` · `8268 STA,X` · `8890 STA,Y` · `E02B` (arm) · `FBC4 STA,X` | **yes — `8250` and `8260`** |
| `$AF32` (bit 40) | `8901 STA,Y` · `E041` (arm) | **none** |
| `$AF3D` (bit 20) | `898C STA,Y` · `E053` (arm) | **none** |
| `$AF7F` (bit 10) | `8AC3/8AD4/8AE2 STA,Y` · `E065` (arm) | **none** |

(The `C0/xxxx`, `CA/`, `CB/`, `D0/`, `E7/`, `EA/`, `EB/` hits are unrelated code in other banks that
happens to touch the same 16-bit address; only bank `$C1` is the battle engine.)

So **three of the four timers are written and never read back.** Porting those three arms is
byte-faithful and observationally inert. Only bit `$80` actually counts down:

```
823C  LDX $B3C4                      the OWNER slot
823F  LDA $AFB6,X / BEQ ->8277 / BMI ->8277     flag clear, or already expired (bit7) -> skip
8246  TDC / LDA $B3C0 / TAX          $B3C0 = a cursor that walks 0..$0A
824B  LDA $AFB6,X / BEQ ->825D
8250    LDA $AF27,X / DEC A / STA $AF27,X       cursor slot's flag SET -> tick the CURSOR's timer
8257    CMP #00 / BNE ->8277 / BRA ->826F
825D  LDX $B3C4                      cursor's flag CLEAR -> tick the OWNER's timer instead
8260    LDA $AF27,X / CMP #00 / BEQ ->826F
8267    DEC A / STA $AF27,X / CMP #00 / BNE ->8277
826F  LDA $AFB6,X / ORA #80 / STA $AFB6,X       EXPIRED: bit7 onto the run flag
8277  INC $B3C0 / LDA $B3C0 / CMP #0B / BCC ...
```

One decrement per frame, and expiry is **bit 7 of the run flag**, not the timer reaching zero being
observed directly.

## 3. ⚠ RETRACTION — `C1/B0EF`-`B1CF` is NOT the expiry consumer

**This section said the wrong thing when it was first written, one commit ago.** It read: *"So the
expiry is consumed centrally for all four, even though only one of them is ever decremented."* That
was inferred from the operand scan finding readers there, and **it is wrong**. The readers were never
disassembled; when they were, they turned out to do something else entirely.

The run flags do have readers, all in one block, one pair per status:

```
run40 ($AFC1)  C1/B0EF LDA,Y   C1/B0FE LDA,Y
run20 ($AFCC)  C1/B151 LDA,Y   C1/B160 LDA,Y
run10 ($AFD7)  C1/B182 LDA,Y   C1/B194 LDA,Y
run80 ($AFB6)  C1/B1B6 LDA,Y   C1/B1C5 LDA,Y
```

But each pair is a **bit-0 REBUILD**, not a test of the expiry:

```
B1B6  LDA $AFB6,Y / AND #FE / STA $AFB6,Y      clear bit 0
B1BE  LDA $5E4B,X / BIT #80 / BEQ ->B1CD
B1C5  LDA $AFB6,Y / ORA #01 / STA $AFB6,Y      set bit 0 iff the BLOCK's status bit is set
```

Checked mechanically rather than by eye: every one of the eleven run-flag reads in bank `$C1` is
followed by `29 FE` (`AND #FE`) or `09 01` (`ORA #01`) — **except** the ticker's own three, at
`C1/823F` (`F0 33` / `30 31` = BEQ/BMI), `C1/824B` (`F0 0D` = BEQ) and `C1/826F` (`09 80` = ORA #80).

So the run flag carries two independent things:

* **bit 0** = "this status is currently set", re-derived every pass from the block byte `$5E4B,X`;
* **bit 7** = "the timer expired", written once by `C1/826F`.

And **within bank `$C1`, the only site that tests bit 7 is `C1/823F`'s own `BMI`** — the ticker's
re-entry guard, which merely stops it ticking that slot again. Nothing in the battle bank clears a
status *because* its timer expired. (Stated as a bounded search, not an absolute: this is an operand
scan over banks `$C0`-`$FF` for absolute and long references plus a disassembly of every `$C1` hit.
Whatever ends a timed status is not reached through these four bytes in this bank — see
[[verify-negatives-on-proper-state]].)

That makes the port order below **more** important, not less: two of the three pieces exist and the
third does not resolve to anything yet.

## 4. What a port needs, in order

1. `C1/E006`'s four arms — cheap, and now gradable by the bank-`$C1` micro-execution harness the
   `--sweepaitarget` / `--sweepmoveclass` pair established. Needs `$16`, `$18`, `$B1FD`,
   `$B0D4`/`$B0DF`/`$B0EA`/`$B12C`, `$5E64`, and `E006`'s two continuations `E070` and `E0F4`.
2. `C1/823C`'s tick, **with** `$B3C0`/`$B3C4`'s meaning and its caller — the cursor-vs-owner split at
   `824B`/`825D` is not decoration and must not be flattened to "decrement my own timer".
3. `C1/B0EF`-`B1CF`, the expiry consumer. ⚠ **Until this lands, `g_btStatusTimedUnported` stays the
   right answer**: arming a timer whose expiry nothing consumes is a worse lie than not arming it.

Doing 1 without 3 is the half-mechanism the current counter exists to avoid.

---

# S8. THE NAGA-ETTE'S KISS -- an enemy tech is an attack on another table (2026-09-26, user defect #14)

User report (translated from Portuguese): "in the battle against the Naga-ettes a status was not applied". Fixtures: saves/field_re/m81_nuns_lake.state
(the cathedral, the fight starts from the field; the port runs it from m81_nuns_lake.lakewram) and
saves/battle_re/m81_kiss_lake.state (twelve frames before a kiss).

## S8.1 The AI chose it; the port threw it away

Naga-ette (id 1E) script CC/9323: section 0 = `17 10 00 00` (a 16% roll) -> `02 05 05 00` (op 02, tech 05); section 1
= move + attack. The port's AI picked section 0 at the cartridge's own draws, but an enemy op 02 went to
battleComputeTechDamage's "song boundary": no header, no draw, 1 damage, no status.

## S8.2 The cartridge's path (cputrace of m81_kiss_lake, frame 12)

`C1/8E15 JSR C1/D7C4` (the class-2 stager: CC/6FCA -> CC/7AC9, `$B18C` = the tech from C1/9A55), `C1/D85A JSR EB0E`, then
the SAME C1/D2DE chain an enemy attack runs: E9A3/E9B8, D174, DE9E, DEF0, **DC64 (hit roll, AF22 count 100)**, D1A4,
**E0F5 -> E27A (type 4) -> E63B, ..., E2CA (bonus, count 3)**, E575, **E65F**, **DCAD**, **D33F D14F / D342 E976 (the
rider, count 100) -> DBAA -> E006**. The kiss header CC/7AC9[05] = `03 32 20 3A 3B 04 3C 94 00 02 00 00`: type 4,
defence 3C (magic, x5/2), $AE4D 94 (bit 2 = rider), accuracy 50, mask 20 (+0x1E bit 5 = SLOW), power 2.
`--monitor`: Lucca's +0x1E ($5ECB) = 20 at f11 by C1:DBE4 (dispatch); her HP 57 -> 55 at f311 (C1:ED1A, the span end).

GATO's song (tech 10) is the same: CC/7AC9[10] = `03 01 20 39 39 00 00 30 00 00 00 80` -- type 0, $AE4D 30 (bit 5: no
hit roll), power 0 -> C1/E42F, whose bonus draw (E4AE, count C063(+0x36)+1) the port never made: its stream was kept
level only because the next resolver drew twice where the cartridge draws once.

## S8.3 The stages every formula shares

* E0F5's ladder on $16 = header +5: 1 E0FF, 2 E188, 3 E203, 4 E27A, 5 E2DA, 6 E36D, 7 E3D1, 8 E4B7->E511, anything
  else E42F (type 0 in the tables). CC/7AC9 (299 records): type 0 x70, 3 x165, 4 x55, 8/10/30/50/100 a few.
* **C1/E63B** opens types 0/1/2/4/5/6 (not 3): a sleep-class target (+0x1E & 06) wakes -- `&= F9`, `$B03A = 1`,
  and its ATB reloads through BD6F.
* **C1/E575** closes all nine (`JMP E575` at E181 E1F9 E273 E2D0 E366 E3CA E425 E4B4 E50E): the ATTACKER's
  (+0x21|+0x26) & 80 -> x1.5 (and out), & 08 -> /2*3, & 02 -> /4*5; +0x1E & 40 -> *8/10, **& 20 (Slow) -> *8/10**,
  & 04 -> /4. A Slowed PC hits for 80%.
* **C1/E65F**: $18 = header +6; 3E -> target +0x3E; 3C -> target +0x3C * 5 / 2; anything else IS the defence.
* **C1/DCAD**: target `+0x1E &= F9`, `(0x100 - $18) * $16 / 0x100` (C90B 16x16 -> 32 in $2C/$2E, C92A divides
  $2E:$28), + $B2BE & FF, 0 -> 1.
* **C1/BD6F** after every `LDA $B158,X / STA $AFAB,X` (84DE BCE8 BDE9 BE65 BEE2 E658): Haste halves, Slow doubles
  (carry -> FF), 0 -> 1, and a PC's $99DD/$9F22 take the result. MEASURED: the Slowed Lucca's next reload is
  $AFAC = E4 = $72 * 2 (C1:BD9B, f506 of m81_kiss_lake with A presses from f330).

## S8.4 ⚠ Correction to "THE TIMED-STATUS ARMS" above

`$AF27 + 11k` are TWELVE 11-byte timer arrays, the last of which is the ATB countdown `$AFAB` ($AF27 + $84); the run
flags `$AFB6 + 11k` end at `$B03A`. The ticker C1/823C-8284 walks X over all of them (`$B3C4` reached $8F on the
kiss fixture), so the "three of four timers are never read" claim above is wrong: `$AF3E` (Lucca's bit-20 timer)
counts down 5A -> 0 by C1:826B one step per iteration from f313, and at zero the turn machinery runs the family's
event -- C1/8975 for bit 20: `$AFCC,Y = 1`, `$AF3D,Y = $B0EA,Y` (5A again, C1:898F at f491 and f671), `$B05B,Y -= 1`,
and at 0 `$B05B,Y = 0A`, `$AFCC,Y = 0`. It does not clear the Slow bit. The port runs the ATB family only; the other
eleven (the arms, their tick, their events) are one port of the turn machinery, and g_btStatusTimedUnported still
counts the arms.

## S8.5 Ported (battle.c)

battleStageAtkHeader = C1/8E03 + D7C4 / D8D1 (`$B18C`, `$B2C3`, `$B2C7`/`$B2C9`, `$B2C8`, `$B2CA` with D864/D971's
cannot-miss, `$B2CC`, `$AE4D`/`$AE4E`); battleComputeDamage runs an enemy's types 4 and default (E42F) through
E63B -> formula -> E575 -> E65F -> DCAD, a PC's physicals through E63B/E575/E65F/DCAD, and the rider at DISPATCH after
DCAD (it ran at the span end, after the HP write); the wake moved from the span end to the formula (E63B) and DCAD;
battleAtbLoad = BD6F at every reload. An enemy of another formula type keeps its old arithmetic and prints
`battle-formula ... NOT PORTED`. Graded: the Naga-ette fight draws idx 2E/2F/30 at the kiss as the cartridge does
(f1671 there), Lucca +0x1E = 20, 2 damage; the header sequence of the whole fight equals the cartridge's
(10 10 10 10 94 10 94 94 10 94); GATO's song draws idx 1F at its dispatch as the cartridge's f1021 does.

---

# S9. THE TIMER MACHINE, PORTED -- and +1E bit 2 is CHAOS, bit 1 is SLEEP (2026-09-30)

This supersedes "THE TIMED-STATUS ARMS" above and every place this file calls `+0x1E` bit 2 "sleep". Two readings
settle the names, both from the ROM: the scan table `CC/F77F` gives bit 2 the STAR icon (state 4) and bit 1 the `Zz`
(state 5) with the DOWN pose (class 08); and a ready PC with bit 2 is sent by C1/851B JSL FD/AB30 to attack a random
combatant, an ally included, while bit 1 zeroes the ATB's flag. Hypno Wave (tech 12, header `02 01 02`) inflicts bit
1; Provoke (tech 0A, `02 01 04`) bit 2. The old checks forced mask 04 and called it sleep.

## S9.1 The machine (src/battle.c, every line cited there)

| piece | ROM | what it does |
|---|---|---|
| staging | FD/B0D5, FD/B4E7, FD/B7EB, C1/FD16 | flags 0, counts 0A, bases = timers = 3C 96 5A 6E 5A 3C 3C 3C 1E 19 3C 78 (arrays 0-11), $B03A = 1 per present combatant, the low-HP bit |
| rebuild | C1/B093 + C1/B0B6 (C1/8195-81C8), every iteration, before the Wait gate | the low-HP bit (+02 bit 0: curHP <= maxHP/8); each flag's bit 0 from its block bit; SLEEP -> $B03A = 0; LOCK -> $A0D1; CHAOS latch $AE7A / $B268; STOP -> $5E0A = 4 |
| tick | C1/8231, X = 0..8E, $B3C0 = X mod 11 | one step per iteration; STOP's redirect (array 0's flag up: every active timer of the combatant counts $AF27 instead) |
| cursors | C1/82A2, odd $0027 | thirteen cursors; an FF turn-order entry reads 255 bytes past the flag array (GATO's fight: array 4 finds $B0E1 = 96) |
| handlers | C1/8313, C1/B92D[0..12] | re-arm + clear the own $B186 bit + count 10 expiries (tables in battleStatusHandler); 12 = C1/8461, last |
| poison / MP | C1/88C5, C1/8B10 | maxHP / ((+37>>1)+8) damage, 5 MP every 10th, through C1/895B action 7F (C1/4058's two frames, script CE/8288 `50 2E 01`, C1/AC85 -> C1/EC7F); the dispatch resumes after it |
| disabled | C1/883D, C1/B575, FD/A95F/A8FE, C1/B3F9, C1/B9B8 | a stopped / slept / confused / berserk PC loses its pending turn: ATB from full, window closed, item back |
| cannot choose | FD/AB30 -> C1/BFAA | CHAOS: rand [0,11) until not itself and on the roster; BERSERK: rand [3,11) on the roster; the physical on it, `INC $AF23` after |
| hit roll | C1/DE9E, C1/DEF0 | blind/chaos hit x5/20+1, poison x15/20+1; target (+20\|+25)&40 evade x5/2 (FF cap), &01 x2, stop/sleep x4/20+1, blind/chaos x10/20+1, slow x15/20+1 then poison x10/20+1; the level term (stores only FF on a carry) |
| low HP | C1/302C | no record matched + the low-HP bit -> pose class 12 (the kneel), $A441 = 80 |
| colours | CF/E7D7 + C1/30B6 | MARK states (6 Magic Wall, 7 Protect, C Haste, D Slow) cycle colours 1 and 12 of the seat's row through CE/0100[CC/F754[state] + $A499]; the icon clear restores the row from $2100 |

## S9.2 The bits, by what the ROM does with them

`+1E` (category 1): 80 STOP (array 0, ends at the 10th; timer redirect; $5E0A = 4; clock icon, class 00) -- 40 POISON
(array 1, lasts; damage through 7F; bubbles icon) -- 20 SLOW (array 2 disarms at the 10th, the bit stays; ATB reload x2,
arrays 8/9 x2, E575 x8/10; a colour-cycle mark) -- 10 (array 8: 1 HP off every $B12C, silent; no icon) -- 08 LOCK ($A0D1;
`?` icon) -- 04 CHAOS (FD/AB30; hit /4; E575 /4; star icon, class 1A) -- 02 SLEEP ($B03A = 0; `Zz`, class 08) -- 01 BLIND
(hit /4; face icon). Bits 1 and 2 break on damage (C1/DCAD, C1/E63B). `+20|+25`: 80 HASTE (array 3; ATB reload /2),
40 (array 4; evade x5/2), 02 (array 11: armed by C1/B0B6, handler RTS), 01 (evade x2). `+21|+26`: 80 BERSERK, 40 MAGIC
WALL (array 5), 20 the MP restore (array 9), 04 PROTECT (array 10, armed only by C1/B0B6's bit 0). `+22` bit 7 LIFELINE.

## S9.3 Measured (the rule's oracle; the fixtures are GATO's attack headers CC/89C6[0..1] patched to carry the rider,
`+1 = 63, +2 = mask, +7 |= 4`, the same ROM on both sides through CT_ROM)

- Poison, slow, +1E 10, lock, blind: the whole state (timers, flags, counts, statuses, HP, $A013) frame for frame at the
  fixed offset 227 over 3000 frames -- identical but for the enemy dispatch's pre-existing two frames.
- Stop, sleep: identical once C1/883D landed (f605: `$AFAB = 48` FD:A92B, `$A6D9 = FF 01 FF` C1:1C18).
- Chaos: the same targets and results in order (Marle miss, GATO miss, Marle 8, ...); each confused attack opens 2-3
  frames before the cartridge's, whose AF22 retry loop plus the executor spill past a frame under the logical oracle (a
  CPU-timing residual: `$26` advances over f753, f754, f755 before `$AD8E` is written).
- Techs (battle_ab's recipe), timer states in sequence: Haste 585/585, Protect 602/602 (+21 bit 2 cleared at the 10th),
  Magic Wall 602/602, the MP restore (Haste's header patched to cat 4 mask 20) identical.
- The colour cycle: slow 1207/1207 states of ($A46D, $A499, $23A2, $23B8); Haste/Protect/Magic Wall identical until the
  first action close, where each side restarts the ramp at its own close (the pre-existing close-scan residue).
- The low-HP kneel: gato_wait with Crono's sheet at 5 HP, `$A440 = 12` f373 and pose 47 on both sides.

## S9.4 Open (the first four closed in S9.5)

- ~~LIFELINE (+22 bit 7)~~ -- PORTED, S9.5.1.
- ~~An enemy's CHAOS / BERSERK turn~~ -- PORTED, S9.5.2 (FD/AB30 is the PC's alone: its only caller is C1/8526).
- ~~LOCK's consumer~~ -- C1/0FD9 / C1/1047 were already read by battleTechGreyPass; C1/D764 PORTED, S9.5.3.
- ~~The handler-12 tail runs at the enemy action's OPEN~~ -- PORTED, S9.5.4.
- The icon re-arm at an action close lands 2-3 frames after the cartridge's (C1/411D runs before C1/41BD drops $A013).

## S9.5 The sweep's revive, the tail after the action, the confused enemy, the lock at the dispatch (2026-09-30)

Fixtures (all on GATO's fight, saves/battle_re/gato_wait.state, the same WRAM and ROM on both sides; the cartridge under the
rule, the port through CT_ROM; `tools/battle_status_cmp.py`, check battle-status-machinery-vs-logical-oracle):
`lifeline` = PEEK_POKE `2622=80,2603=05` (Crono's sheet: the lifeline, 5 HP); `sleep02` / `lock08` = GATO's attack
headers CC/89C6[0..1] with the rider (+1 = 63, +2 = 02 / 08, +7 |= 04); `chaosE` = Crono's physical header CC/23F7 with
the rider (+1 = 63, +2 = 04, +7 |= 04) and GATO's +23 immunity CC/5422 = 00; `blindE` = the same with +2 = 01.

### S9.5.1 LIFELINE -- C1/B279's PC arm past B2D9 (dis.py 1B2D9 0xE0 1 0)

```
B2E1  X = FD/A849[Y*2] (the seat's command record $93EE + 7Y), `STA $06`; record +1 bit 4 -> $B18B = Y, JSR C1/BC60
      (`BRA ->BCE0`: an RTS), the bit cleared
B30F  +22 bit 7 -> B316: +22 &= 7F; $AD89 = +39 * 5 (C1/C90B); $B1FD = Y; C1/E89F; $AD9C,X = $AD89, $AD9E,X = 1;
      $B2C7 = 0; $B202 = 80; C1/EBF8 (the slot, negated: a heal); $AE91 = 0, $AE92 = 2, $AE93 = 7A, $AE94 = 0, $AECC = Y;
      C1/AD09; JSR C1/AC57 (ACTION 7A: config CD/45A6[7A] = `7A E1 02 1B 06 06 FF`, script CE/81E0, tracks 3 and 8);
      `$0200 = $AF0A,$B1FD / LDA #09 / JSL CD/0021` (CE:EF11[9] = "{11} got up!"); JSL FD/ACEE; STZ $24 -> B3B7
B392  else `$9F22 = $99DD = $AFAB = $B158` (the fallen gauge) -> B3B7
```
The sweep C1/B223 (dis.py 1B223 0x56 1 0) also runs, per enemy Y off the roster whose $5E12,Y ($5E15 + slot, its move
target) is off it too, `$B23F,Y = FF / $B2B3,Y = 0`, and after the loop `$24 = 1` when all three PCs' $AEFF have bit 7.
Measured (the lifeline fixture): the attack's close f605 (`$A013 = 0`, C1:41BD) and in the same frame the tail's sweep
(`$5E4F = 00` C1:B318, `$AE93 = 7A` C1:B36A, `$B328 = E7 FF 00 80`, `$B2D3 = 00`: stopped at seat 0); `$A013 = 1` f607;
`$A013 = 0` f799 with HP 25 (C1:ED68) and `$020C = 09` (CD:0261); `$CCEC = 10` f801 (CD:0291), `= 00` f977 (CD:0203: 16 +
the 160 of CD/01A5 -- a HELD A cuts it, `LDA $F8 / BPL`); f993 `$B2D3 = 0B` (C1:B260) and `$0027` + 1 (C1:8124): the sweep,
the handler and the loop's tail ran in the RTL's frame; f995 the next sweep stands Crono up (`$5E4A = 00`, `$AEFF = 00`,
`$B03A = 01`). The record `$AD9C = 19 00 01 00` lives f605-f993 (the close of 7A has no FD/ACEE of its own: B38A's).
Ported: battleDeadFlagOne's arms 2 (B392) and 4 (the stores to C1/AC57), battleDeadFlagSweep (resumable, with the caller
kept: the iteration's C1/8130, the handler's tail, the dispatch), battleLifelineStage / MsgPost / MsgTick / Resume (the
status timers' action machinery runs 7A; CD/01B3's frames block the loop, C1/10E3's menu pass runs in each CD/3E75).
The fallen PC's gauge write moved from the HP write (a port stand-in for B392) to the sweep. C1/1BAA's `$95D5 = the first
queued slot` (C1/1C18-1C2B scans from slot 0; the port took the old cursor's) came out of the same fixture: Marle takes the
command at f605. --sweepdeadflagpc micro-executes the PC arm (576/576 over all of WRAM but the stack page).
With `lifeline` 0 (the boot's early sweep; C1/A452's resolver 05, whose own C1/B279 would run the revive inside the AI) the
arm stops before B30F and the next sweep takes it.

### S9.5.2 A CONFUSED / BERSERK ENEMY

FD/AB30 is called from C1/8526 alone (the PC path, `CMP #03 / BCC` at C1/8491), so its `Y >= 3` arm is dead code. What an
enemy's status changes:
- its SCRIPT: C1/AFF6 (every step of the eval's walk) and C1/B494 (the counter's) JSR C1/B4AA: (+21|+26) bit 7 (berserk)
  -> CC/8D08, else (+1E ^ +23) & +1E bit 2 (chaos, unless immune) -> CC/8D1E (`00 00 00 00 FE 04 01 00 06 01 01 00 06 01 01
  00 17 01 01 00 17 01 FE FF`); a changed chaos bit makes C1/B0B6 set $B268,Y = FF, so the next turn's C1/8D34 does nothing
  and the tail's C1/881C re-walks him.
- the COUNTER path (C1/8657, the handler's tail): the actor that just acted ($B3B7) an enemy with +1E bit 2 -> nobody
  counters (C1/8658-866E); a candidate with +1E & 8F is passed over (C1/86FF-8704). That was the `BIT #04` the old port read
  as a turn gate for every actor. Measured (blindE = chaosE with mask 01): the unpatched GATO counters Crono's hit (f783 ->
  f785), the blinded one does not. (A confused one does not either way: its counter script is CC/8D1E's block 2, an op 00.)
- C1/8CF9's head (every turn): `$AF24 = 0`, $B252 = slot, $B18B = the combatant, $AE93-$AE96 / $AE99-$AE9B = 0, $AE97 =
  $AE98 = FF (the first GATO action's roles are `06 FF FF 00`, oracle f452).
- C1/881C runs on EVERY pass of the ATB handler, not only after an enemy's turn: an enemy whose walk committed nothing
  ($AF24 = 2) is re-walked at the next pass.
- resolver 17 (C1/A709, the nearest other enemy) compares $9873 with itself; with no candidate it reads what the last
  C1/2986 left -- the PC re-aim C1/2F54 (sub-mode 2, the nearest enemy, `$99DA,X = $9873`), now ported for its stores.
Measured (chaosE, A at f640/f652): Crono's hit f657 (`$614B = 04`), no counter, f785 `$AE80 = 04` / `$B26E = FF` (C1:B1FB /
B200); GATO's turn f943 has no action and the eval commits CC/8D24; he attacks Crono at f1151 / f1513 (1 damage: C1/E575's
/4); f1667 `$AF24 = 02` on `01 00 17 01` (no ally) and the next pass re-walks him (f1671, `$B1DA = 8D28`); actions at
1153 1515 1877 2239 2601 -- the port's, frame for frame, c787-c2700.
NOT PORTED (no fixture reaches it): C1/8E28-8E45, the turn's own actor gate after the header's staging (dead without its
$AEB3 death mark, or +1E & 82) -> no action and no step -- it runs the formula (C1/D8D1 / D7C4, with its draws and rider)
first, which the port's enemy path computes later, at the think's end.

### S9.5.3 LOCK at the dispatch -- C1/D74F (dis.py 1D74F 0x75 1 0), from C1/D573 in the formula

Per participant $B2EA[k]: `LDA $A0D1,X / BNE fail` (C1/D764, X = its seat), the cost (C1/CB48 / CB61 / CB7A, scaled by
C1/CBF6) against curMP, `+1E & 08` -> fail; `$B3C8 = FF` on a failure. With $B3C9 bit 7 clear C1/D57A skips the formula
(C1/D742) and C1/BBDC drops the tech: C1/BC5A -> C1/BDB3's reloads, C1/BCF8's record clears, no action, no MP. Measured
(lock08, Crono's Cyclone confirmed f498 during GATO's locking attack): `$A0D1 = 01` f607, f609 `$B3C8 = FF`, `$AFAB = 48`,
`$93EE = 00` -- no `$A013`. The port drops it at c611: it runs C1/B967's body after C1/4058's two waits for every command,
where the cartridge's C1/BA33 -> C1/BF79 -> C1/D74F runs at the handler pass and never enters C1/4058 (the dispatch model;
the same holds for C1/B9B8's cancel). The grey pass's two readers (C1/0FD9 the chooser, C1/1047 a partner through C1/103E)
were already battleTechGreyPass's: lock08 with the list opened after f607 greys every Crono tech (`$1A82 = 83`, `81`, `80`
on both sides; the pass lands on the menu's own schedule).

### S9.5.4 THE ATB HANDLER'S TAIL AFTER THE ACTION

C1/84D6 JSR C1/8CF9 (the enemy's action), C1/8526 JSL FD/AB30 (a confused PC's) and C1/85CE JSR C1/B967 (a committed
command) all return into C1/860A / C1/860D, so `$B186 &= FFFE`, C1/B223 and C1/881C come after the action: at its close in
the port (stSweepAtEnd, h12TailPc; battleThinkTick when C1/B967 ran none). Measured (sleep02): Crono's `$B03A` 00 through
GATO's song f1015-f1314 (C1/B0B6's store, the tail's C1/B279 at f1314); the port had 01 (303 frames) and now has none.
Also: the action's own FD/ACEE (C1/8E88) precedes the tail.
Left as it was: C1/850E (an unready enemy entry with no PC pending) runs C1/881C alone, without C1/860D / C1/8626; the port
runs the whole tail there (it clears $B186 bit 0 and sweeps). A no-action enemy turn runs its eval before C1/883D / C1/B223
(the port's order, which the pinned traces carry). A counter's action (C1/8780) is followed by C1/8787's C1/B223 in the ROM,
not in the port.
