# BATTLE MISS / EVADE — the physical to-hit roll + the "MISS" popup presentation

Derives, from the ROM disasm + LakeSnes oracle **only**, what the battle module SHOWS when a physical attack
misses: the "MISS" glyph popup, the (absent) victim reaction, and the ATB/counter/HP semantics. Every claim is
anchored to a `tools/dis.py <fileoffset>` (fileoffset = `(bank−0xC0)<<16 | addr`; battle code runs 16-bit A/X
`m=0 x=0` unless noted 8-bit) or an oracle `ct_probe.exe`(`= build/ct.exe`) `--monitor`/`--peek`/`--read`
observation. Writer PCs = the PC *after* the store retires.

> ⚠ **HEADLINE — the "threshold" premise was pointed at the WRONG routine.** `battle_crit.md` §H (and the
> mission brief) said a physical strike "misses when `rand > hit×3`" via `C1/E9F0` (marker `$B3BF=0xFE`). That
> is the **status-effect roll** `C1/E9D5` (does the attack's *status* land?), gated on `$AE4D bit1` and run
> **after** damage. The **PHYSICAL to-hit** is a *different* routine, `C1/DC64`: **MISS iff
> `hit×100/evade < rand(0..99)`** (`hit` = attacker block+0x3A, `evade` = target block+0x3B). PROOF it is
> DC64 and not E9D5: **patching the target's EVADE `$6168` (block+0x3B) forces the miss** — and *only* DC64
> reads block+0x3B (E9D5 reads block+0x2A). So the derived law here supersedes §H for the miss the player sees
> ("MISS", no damage). The `hit×3` status roll is left as its own (unimplemented) mechanism (§OPEN).

---

## 0 — HEADLINE RESULTS (all oracle-measured)

1. **Physical accuracy = `C1/DC64`:** `hitPct = hit×100/evade` (clamped to 99 if `hit ≥ evade` ⇒ always hit);
   `roll = rand(0..99)` on the **MAIN `$26` stream** (`C1/DC94 = AF22`); **MISS iff `hitPct < roll`** ⇒ `$16=0`.
   `hit` = attacker block **+0x3A** (`$AEE9`), `evade` = target block **+0x3B** (`$AEEA`).
2. **The "MISS" popup = OBJ tiles `0x19A` (left) + `0x19B` (right), palette 0, prio 3, 8×8**, spawned by
   `C1/E8E2` (popup **type 5**) at the target's damage-number anchor (GATO leftX **128** / centerX **136** /
   restY **96**), same **parabolic rise arc** and **58-frame** life (`$A3D1`) as a damage number. Renders the
   white/olive "MISS" glyph — **pixel-verified 61/61 + 58/58 opaque px, 0 mismatch** vs a forced-miss fb.
3. **The victim does NOTHING:** no HP change, **no hurt-flash** (body tiles stay pal1, not the pal4 white
   silhouette), no knockback, no hurt pose — it stays idle. The **attacker's full swing still plays** (span
   unchanged, ~125–126f).
4. **ATB/queue:** the missed turn is **CONSUMED** (`$95D5→FF`, the actor dequeues, `$A013` freezes the full
   span, timer reloads). **No HP write** (`$6130` flat). The **when-hit COUNTER is NOT armed** (a miss deals 0
   damage → the "damaged since last turn" fact is never set).
5. A miss draws the `$26` hit roll **exactly once** (`$26` `0x1C→0x1D`), and **no** damage/bonus draw.

### The clean miss ledger (oracle `--monitor`, forced miss on `gato_miss.state` = a Crono-command-ready state
with GATO evade `$6168` patched → 0xFF; two-edge A drive `"100:10,0:20,100:10,0:230"`):
```
f34   $95D5 0→FF          Crono commits/dequeues (the attack dispatches)
f43   $AE94 →0x40  $26 0x1C→0x1D   MISS: E8E2 sets $AE94 bit6, the hit roll advanced $26 by ONE (no dmg draw)
f45   $A013 0→1           action-freeze ON (the swing span begins)
f107  $A3D1 0→1           "MISS" popup born
f108  $A3D1 1→2           (2-glyph popup)
f165  $A3D1 →0            popup done  (life f107→f165 = 58f)
f171  $A013 1→0           span end (freeze off; f45→f171 = 126f)  — GATO $6130 = 76 THROUGHOUT (no HP write)
f172  $AE94 →0            miss marker cleared for the next action
```

---

## 1 — THE ACCURACY LAW  (`C1/DC64`, the D306 physical executor)

### 1.1 The executor `C1/D306` splits hit vs miss on `$16` (dis.py `1D306`, 8-bit)
```
1D306 LDA $AE4D / AND #20 / BNE 1D32C   ; $AE4D bit5 = "no accuracy" attack -> straight to damage
1D30D JSR C1/D174                        ; $16 = attacker[block+$AEE9] (HIT), $18 = target[block+$AEEA] (EVADE)
1D310 JSR C1/DE9E / JSR C1/DEF0          ; hit/evade stance buffs (bits in $5E4B/$5E4D) — ×105/100, ×2, ÷2 …
1D316 JSR C1/DC64                        ; ** THE ROLL **  -> $16 = 1 (HIT) / 0 (MISS)
1D319 LDA $16 / BNE 1D32C                ; $16 != 0 -> HIT (damage: D1A4/E0F5/E65F/DCAD)
1D31D JSR C1/D1B5 / STZ $1C / JSR C1/E8E2 ; ** MISS **: $1A = $AEF1, then the RESULT popup (§2)
1D325 LDA #80 / STA $B200                 ; result flag; BRA to the span tail (D354)
1D32C … (HIT) JSR C1/D1A4 / E0F5 / E65F / DCAD  ; the pinned damage pipeline (battle_loop.md S3)
1D338 …AE4D bit2 -> E976 (a secondary roll) ; …AE4D bit1 -> C1/E9D5 (the STATUS roll, §HEADLINE)
```
`D174` (dis.py `1D174`, 8-bit): `$16 = mem[$5E2D + $AEE9 + $B1F4]` (attacker block + `$AEE9`),
`$18 = mem[$5E2D + $AEEA + $B1F6]` (target block + `$AEEA`). For a physical weapon the effect-header
(`CC/213F[0x3A] = 03 00 00 3a 3b 01 3e 80 80 08 04 80`, staged into `$AEE6+`) gives **`$AEE9`=+3=`0x3A`**
(attacker HIT) and **`$AEEA`=+4=`0x3B`** (target EVADE).

### 1.1b ⚠ THE PORT RAN THIS PC-ONLY UNTIL 2026-08-22 — enemies could never miss

The gate transcribed above is `$AE4D & 0x20`, and it says nothing about who is attacking. `src/battle.c`
nevertheless ran the roll behind `battleIsPc(atk)`, on a comment claiming "the DC64 hit/miss roll belongs
to the PC pipeline only -- the enemy type-4 physical goes straight to the formula ... a hit roll there
would make it two draws". That was wrong, and the dossier already said so on the line above it.

Where bit 5 actually comes from, read out end to end:
* `C1/EB0E` seeds it from the staged header byte: `LDA $AEED / STA $AE4D`.
* The PC physical path ORs the attack's own flag byte in: `C1/D629 LDA $AE4D / ORA $B2CA`.
* Bit 5 is FORCED in exactly two places ROM-wide, `C1/D864` and `C1/D971`, and BOTH sit inside the TECH
  stagers (entries `C1/D7C4` / `C1/D8D1`) behind `LDA $B2CA / BIT #C0 / BEQ skip`. So "cannot miss" is a
  property of a TECH whose record flag carries bit6/bit7 — not of the attacker's side.

Measured (`--monitor "7EAE4D:1,7E9877:1" --diff --writer`, gato_wait mash): `$AE4D` reads **0x80** before
every PC action (writer `C1:D632`, the D629 OR) and **0x10** before every enemy action (writer `C1:EB15`,
the EB0E mirror). Neither has bit 5 set, so both roll. And the roll demonstrably RUNS for an enemy — on
GATO's attack B (`$9877 = 0x34`) the oracle executes DC64's own clamp:

```
  f1267   7E0016=63*(C1:DC6E)      ; `LDA #63 / STA $16` INSIDE C1/DC64, during an ENEMY action
```

That action also spends three `$26` draws, not the one the retired comment asserted. Fixed by dropping
the `battleIsPc` gate: techs already go through a different function that never reaches this roll, so for
everything this engine dispatches the bit-5 gate IS "is this a tech". Consequence: adding a draw per enemy
physical shifts every RNG stream, which moved 16 pinned checks — all re-pinned, semantics verified intact
(still a victory, statuses still land/block per immunity). NOT ported, stated: the header staging
(`$AEE6` → `$AEED`) itself, so a PHYSICAL reaching the D864/D971 arm would wrongly roll; none does.

### 1.2 The roll `C1/DC64` (dis.py `1DC64`, 8-bit) — byte-exact
```
1DC64 LDA $16 / CMP $18 / BCC 1DC72     ; if HIT >= EVADE:
1DC6A   LDA #63 / STA $16 ; LDA #64 / STA $18   ;   $16=99, $18=100  (hitPct clamps to 99 -> ALWAYS hits)
1DC72 LDA $16 / … / LDX #0064 / JSR C1/C90B     ; $2C = HIT × 100
1DC80 LDX $2C … LDA $18 … / JSR C1/C92A         ; $2C = (HIT×100) / EVADE
1DC8E STX $16                                   ; ** $16 = hitPct = HIT×100/EVADE **
1DC90 LDA #64 / JSR C1/AF22 / STA $18           ; ** roll = rand(0..99) on the $26 stream (= our battleRng) **
1DC99 LDA $16 / CMP $18 / BCC 1DCA5             ; hitPct < roll ?
1DC9F   LDA #01 / STA $16                        ;   NO  -> HIT  ($16=1)
1DCA5   TDC / STA $16                            ;   YES -> MISS ($16=0)
1DCA8 STA $AE4F / RTS
```
**`MISS iff hitPct < rand(0..99)`** (`BCC` = branch-if-`hitPct<roll` = miss). `hit ≥ evade` clamps
`hitPct=99` ⇒ `99 ≥ roll` always ⇒ **guaranteed hit** (but the roll STILL draws `$26`, `1DC90` — see §5).

### 1.3 The stats (oracle `--read` on a MID-battle state — the PRE-battle field state has zeroed blocks; the
`C1/CF3B` stat copy stages them at battle-init):
| combatant | block | HIT (+0x3A) | EVADE (+0x3B) | phys def (+0x3E) | mag def (+0x3C) |
|---|---|---|---|---|---|
| Crono | `$5E2D` | `$5E67`=**8** | `$5E68`=8 | — | — |
| Marle | `$5EAD` | `$5EE7`=**10** | `$5EE8`=8 | — | — |
| GATO  | `$612D` | `$6167`=**100** | `$6168`=**6** | `$616B`=127 | `$6169`=50 |

So in the GATO fight **everyone always hits** (Crono hit 8 ≥ GATO evade 6; GATO hit 100 ≥ Crono/Marle evade 8) —
which is exactly why our always-hit engine matched every pin. A miss requires `hit < evade` (patch evade high).

---

## 2 — THE "MISS" POPUP  (`C1/E8E2` → the type-5 result, glyph tiles `0x19A/0x19B`)

### 2.1 The result routine `C1/E8E2` (dis.py `1E8E2`, 8-bit)
```
1E8E2 LDA $1C / CMP #01 / …               ; $1C=0 on a to-hit miss (D306 STZ $1C) -> use $1A bit7
1E8F8 LDA $1A / BIT #80 / BEQ 1E927        ; ($1A = $AEF1, weapon byte +11; Crono 0x80 -> bit7 set)
1E8FE JSR C1/E89F ; LDA $0E / TAX
1E904   LDA #05 / STA $AD9E,X              ; ** popup TYPE = 5 **  (a damage number uses $AD9E,X = 1, C1/1D426)
1E909   TDC / STA $AD9C,X / STA $AD9D,X    ;    value 0
1E910   LDA $B1FD / TAY ; LDA #20 / STA $B202 ; target obj Y; flag $B202 = 0x20
1E91A   JSR C1/EBF8                        ; ** stage the result **  ($AD89 -> $B328,Y, flag 0x20 -> $B32B,Y)
1E91D   LDA $AE94 / ORA #40 / STA $AE94    ; set $AE94 bit6  (weapon-dependent via $AEF1; not a universal marker)
```
The **popup TYPE byte `$AD9E,X`** distinguishes: **1 = decimal number**, **5 = "MISS"** (both spawned through the
same builder). `$B202 = 0x20` is the **display-only** flag: at HP-apply the pre-pass `C1/EC39` scans `$B32B,Y`
for bit5 (0x20) and, if set, **zeroes the staged damage** before the `C1/EC99` HP subtract — so a miss stages a
display entry that **applies no HP**. (`$AE94` bit6 is set only when the weapon byte `$AEF1` has bit7/bit6 — for
Crono's sword it does, for Marle's bow it does not — so `$AE94` is NOT a universal miss marker; the reliable
signal is "a popup during a PC action with the target HP flat".)

### 2.2 The glyph — tiles `0x19A`/`0x19B`, palette 0 (oracle OAM at the popup frame)
`--peek` a settled miss popup (mash-peek, GATO evade patched): the target's number OAM slots carry
**tile `0x19A` at (128, y), tile `0x19B` at (136, y)**, `attr=0x31` = **pal0, prio3, ntbl1** (⇒ tile `0x19A`/
`0x19B`), size0 (8×8). Two 8×8 sprites, **8 px apart**, spelling **"MISS"** — exactly the "unused `0x19A/0x19B`
near the strip" `battle_actionfx.md` P1 flagged. Rendered from the in-battle OBJ VRAM (byte `0x3340`/`0x3360`,
same 0x3000–0x3C00 boot upload as the digit strip `0x190..0x199`). The glyph uses pal0 idx 1 (white 0x7FFF) /
2 (olive) / 3 (dark) — **same palette as the digits**, so it renders white-ish (NOT a separate colour). Pixel-
diff of the baked tiles + pal0 vs the oracle fb at the popup: **0x19A 61/61, 0x19B 58/58 opaque px, 0 mismatch.**

### 2.3 Position + motion + life — identical to a damage number
- **Anchor:** GATO **leftX 128 / centerX 136 / restY 96** (`battle_actionfx.md` P1.3), 2 tiles = 16 px centered.
- **Motion:** the SAME parabolic rise (`ctBattleFxRiseDY`): oracle y **87→97** over the rise, settling to rest 96.
- **Life:** **58 frames** (`$A3D1` nonzero f107→f165), the same `CT_FX_POPUP_LIFE` as a damage number.

---

## 3 — VICTIM REACTION + ATB / COUNTER / HP SEMANTICS

- **No hurt-flash.** The hit-flash is a pal1/5/6→pal4 (white) swap on the victim's OWN pose tiles
  (`battle_actionfx.md` P3). At the miss frame GATO's body tiles `0x080..0x086` are **pal1** (normal), zero pal4
  — no flash. (In D306 the flash/damage all live on the HIT branch `D32C+`; the miss branch `D31D` skips them.)
- **No knockback, no hurt pose, no dodge.** GATO stays in his idle stance; nothing in `E8E2` moves or re-poses
  the victim. (An enemy *dodge* anim, if any exists for other enemies, is UNOBSERVED — GATO has none; §OPEN.)
- **The attacker's swing is unchanged.** The strike op still runs its full animation (span **125** for Crono's
  melee = the pinned `CT_SPAN_PC`; oracle miss freeze f45→f171 = 126 real frames). Only the *impact* (damage +
  flash) is replaced by the "MISS" popup.
- **The turn is CONSUMED** (`$95D5→FF` at dispatch, actor dequeues, `$A013` freezes the full span, timer
  reloads at span-end). A miss is a spent turn.
- **No HP write** (`$6130`/`$5E30` flat across the whole action; §2.1 the 0x20 flag zeroes the stage).
- **The when-hit COUNTER is NOT armed by a miss.** GATO's reactive counter (`battle_counter_gating.md`) fires on
  a *qualifying hit* = damage applied. A miss applies 0 damage, so the "damaged since last turn" fact is never
  set (in our engine `battleApplyDamage`'s `dmg>0` gate is never reached → `aiHit`/`enCounterDelay` stay clear).

---

## 4 — FORCING A MISS DETERMINISTICALLY (the recipe)

The physical hit test only fires when `hit < evade`, so a miss is forced by **patching the target's EVADE
(block+0x3B) high** (dossier recipe (b), cleaner than sweeping mash cadences). Two contexts:

- **Oracle observation:** patch `$6168` (GATO evade) → 0xFF in a MID-battle Crono-ready state (the PRE-battle
  field state's blocks are zeroed until `C1/CF3B` stages them, so a field-state patch is overwritten at init).
  `patch_state.py crono_normal_ready.state gato_miss.state 6168=FF`; then a two-edge A drive (or a mash) makes
  Crono miss GATO. With evade 255: `hitPct = 8×100/255 = 3`, so ~97% of `rand(0..99)` misses.
- **Engine regression:** TEST HOOK `CT_BATTLE_ENEMYEVADE=<n>` (mirrors `CT_BATTLE_CRITRATE`) sets every alive
  enemy's staged evade (block+0x3B) to `n` at battle-init (post-stage). `n=255` ⇒ every PC physical misses GATO
  deterministically (RNG idx `$26` is seed-deterministic).

---

## 5 — IMPLEMENTATION ✅ LANDED (`src/battle.c`, `src/battle.h`, `src/field.c`, `src/data/battle_fx.h`)

- **`battleRollHit(h, atk, tgt)`** (battle.c): the DC64 test — `hit = block+0x3A`, `evade = block+0x3B`; if
  `hit ≥ evade` return HIT **without drawing**; else `roll = battleRng(0..99)` (the `$26` stream), MISS iff
  `hit×100/evade < roll`. Called at the top of **`battleComputeDamage`**: a miss sets `h->actMiss=1`, stages 0,
  and returns **before** the damage/bonus/crit draws — so a miss advances `$26` by exactly 1 (matches the oracle).
- **Strike suppression** (battle.c `battleAdvanceAction`): `wantStrike && !actMiss` — no flash/knockback/hurt on
  a miss. **Popup:** on `actMiss` spawn `battleSpawnPopup(…, kind=1)` (the "MISS" glyph) instead of the number.
  **Apply:** `if (!actMiss) battleApplyDamage(…)` — no HP, not counted as a landed attack; the turn still
  consumes (reload/latch/freeze-clear run unconditionally). `actMiss` reset at every dispatch + `CtBattleBegin`.
- **Render** (field.c `battleFxPopupsRender`): a `kind==1` popup blits `ctBattleFxMissTile[0..1]` (tiles
  0x19A/0x19B) at `centerX − 8`, same rise arc + pal0 as the digits (shared `battleFxBlitTile` helper).
- **Bake** (`tools/_gen_battlefx.py` → `src/data/battle_fx.h`): `ctBattleFxMissTile[2][32]` + `CT_FX_MISS_BASE`/
  `CT_FX_MISS_NTILE`. `--verify` patches evade in a `PEEK_SAVE`'d mid-battle state and pixel-diffs the glyph:
  **119/119 opaque px, 0 mismatch.**

> ⚠ **PIN-SAFETY (why zero pins move).** The ROM draws the hit roll on the `$26` stream **unconditionally**
> (`1DC90`, even the clamped always-hit case). Our battle loop already runs on a **different `$26` timeline**
> than the oracle (the whole loop is not `$26`-byte-exact — `battle_actionfx.md` "the SAME RNG-order boundary"),
> and the mandate here is **zero existing pins move**. So `battleRollHit` draws **only when a miss is possible**
> (`hit < evade`). Every existing fight has `hit ≥ evade` for all attacker/target pairs → **no draw → `$26`
> unperturbed → every pinned damage/HP/victory signature byte-identical** (verified: quick 76→77 pass, 0 fail;
> full unchanged). The hit TEST is byte-exact; only the (predetermined-outcome) DRAW is gated — a documented
> deviation, not an invention. Unconditional drawing would move every pin (forbidden by the mission).

**Regression check `field-battle-miss-forced`** (`CT_BATTLE_MASHA=1 CT_BATTLE_ENEMYEVADE=255
CT_BATTLE_DEBUGEND=320`, seed `gato_t0.raw.wram`): asserts
`battlemiss: f305 pc=0 tgt=6 tgtHP=76(dmg0,flat) missPopup=1 hurtPose=0 flash=0 span=125` + `battletest: PASS` —
HP flat, MISS popup spawned, no hurt/flash, turn consumed; deterministic ×2. The glyph pixels are separately
pinned by the generator's `--verify` (119/119).

---

## OPEN

- **The `hit×3` STATUS roll (`C1/E9D5`, marker `$B3BF=0xFE`).** The `battle_crit.md` §H routine — whether the
  attack's *status effect* (poison/sleep/…) lands — gated on `$AE4D bit1`, run after damage, using `roll >
  hitStat×3` on the `$26` stream and the `$AE5A`/`CC5E04`/`CC5E05` status tables + `JSL FD/B655`. Distinct from
  the to-hit; UNIMPLEMENTED (no status-inflicting attack is exercised in the GATO/forest fights).
- **Enemy DODGE / PC evade animation.** GATO on a miss just stays idle (no dodge). Whether any enemy plays a
  dodge anim on an evade, or a PC has an evade-sidestep, is UNOBSERVED (needs a capture of an enemy the PCs
  routinely miss, or a high-evade PC being missed).
- **"2× Evade" / Evade-up status.** The stance-buff routines `C1/DE9E`/`C1/DEF0` read `$5E4B`/`$5E4D` flag bits
  and scale hit/evade (×105/100, ×2, ÷2, ÷4 …) — the evade-boosting statuses. Characterized structurally but
  UNEXERCISED (no status-modified accuracy fight captured).
- **The secondary `C1/E976` roll** (`D306` AE4D bit2 branch): a second accuracy-style roll (reads block+0x46
  `$5E73`). Noted, not on the basic physical path, undecoded.
- **`$AE94` bit6 origin.** Its set/clear is weapon-`$AEF1`-dependent (`E8E2` branch); its downstream consumers
  beyond the D306 result flag are not fully traced (the reliable miss signal used is HP-flat + the type-5 popup).

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-…-9e84b785-…/scratchpad/`
- `ct_probe.exe` (= build/ct.exe). Base: repo `gato_press_action.state`; `gato_miss.state` =
  `crono_normal_ready.state` + `6168=FF` (`patch_state.py`); the generator's `fx_miss_ready.state` (PEEK_SAVE
  mid-battle) + `fx_miss.state` (evade patched).
- Renders: `mm135.png` / `ourM_275.png` (oracle vs OUR-engine "MISS" over GATO — byte-matching anchor 128/136/96).
- Disasm anchors: executor `0x1D306`; hit/evade setup `0x1D174`; roll `0x1DC64`; miss result `0x1E8E2`; stage
  `0x1EBF8`; apply pre-pass `0x1EC39` (0x20-zero) / HP write `0x1EC99`; status roll `0x1E9D5` (§HEADLINE).
- Method: force miss = target evade (block+0x3B) high; observe = `--monitor "AE94:1,A3D1:1,6130:2,0026:1"`;
  glyph = OAM tiles 0x19A/0x19B pal0; pixel-verify = `tools/_gen_battlefx.py --verify` (119/119).
