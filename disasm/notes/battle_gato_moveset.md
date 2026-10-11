# GATO'S COMPLETE MOVESET — the three moves (1-punch / flurry / song), the multi-hit pipeline, and the "0-damage song" bug

Derives, from the ROM disasm + LakeSnes oracle **only**, GATO's full move *presentation* behind two user reports
(GATO fight, Crono seat0 + Marle seat1):

1. *"sometimes GATO attacks, the character does not do the damage animation and some time later **0 damage** appears"*
   — sometimes GATO attacks, the PC shows NO hurt animation, and a **0** damage popup appears later.
2. *"GATO is only using 1 attack, in the original game he has more: attack with 1 punch, attack with **several punches**,
   sing"* — GATO should have THREE moves: single punch, MULTI-punch, and the song; ours shows only one.

Method (mandate: measured, not invented). Base `gato_press_action.state` (repo root; A-press boots battle ≈f222).
Oracle `ct_probe.exe` (scratch copy of `build/ct.exe`). `--monitor` for all RAM timelines (never `--peek`; the
peek≠monitor gotcha). ROM `tools/dis.py <fileoffset>` (fileoffset = (bank−0xC0)<<16 | addr) + a multi-track anim
decoder that mirrors our engine's `battleAnimOpLen` (ROM opcode jump-table `C1/7A6B`). Writer PCs = the PC after
the store retires. Drivers: **MASH** `('100:20,0:40')×24` (keeps GATO in his when-hit block2 → attack-B every turn),
**HOLD** `100:8,0:2700` (Crono idles in the command menu → GATO un-hit → block1 attack-A + song).

---

## HEADLINE FINDINGS

- **GATO has exactly THREE AI actions** (pinned `battle_interp_verify.md` T1.B, re-verified here on the oracle):
  block1 (un-hit) = **attack-A** (`$9877=0x31`) + **song** (`$9877=0x10`); block2 (when-hit) = **attack-B**
  (`$9877=0x34`). The user's "1 punch / several punches / sing" = these three.
- **The "several punches" (multi-punch) = attack-B (0x34) — but it is a SINGLE-HIT-DAMAGE attack with a *flurry
  ANIMATION*, NOT a multi-hit.** Its anim script `CD/7B72` has ONE strike (`1E`) and ONE rendered damage number
  (`50`); the "several punches" is a dedicated track (`CD/7B8F`) that plays the **punch sound `78 46` + palette-
  flash + anim SEVEN times**. The ROM's multi-hit HP pipeline (`$B354`/`$B380`, `BIT #$4000` gate) EXISTS but
  **GATO never uses it** — every GATO physical stages ONLY hit-1 (`$B328`), oracle-proven `$B354`/`$B380` == 0 all
  fight.
- **The "0-damage" bug = the SONG.** The song is a TECH (`02 10 05 00`, formula type 0 / power 0) whose base damage
  is 0 → bumped to **1** by the ROM's universal **min-1-damage floor** (`C1/DCF3-DCF6`). It has **ZERO `1E`
  across all 9 tracks** (no hurt-flash — canonical) but a `50` ShowDamage → a **"1" popup over the (random) target
  PC** (oracle: Marle 65→64). **Our engine hardcodes tech damage to 0** (`battle.c:1530 actValue = isTech ? 0`),
  so it shows a **"0"** popup and drains no HP — exactly report (1).
- **"Only one attack" is half-canonical, half-render-bug.** In normal (aggressive) play the player hits GATO every
  turn → block2 → **attack-B every turn** — canonical (the song/attack-A need GATO left un-hit). BUT our engine
  renders attack-A and attack-B *identically* (one strike + one popup; the 7× flurry is not modelled), so even the
  un-hit variety is invisible.
- **attack-A and attack-B use DIFFERENT power records** (new): attack-A = enemy record **#0** (`CC/89C6`, power
  **0x0C=12**) → **9** dmg; attack-B = record **#1** (`CC/89D2`, power **0x12=18**) → **14-15** dmg. Our engine
  hardcodes record #1 for both → attack-A damage is wrong (would be ~14 not 9); masked today because no pin
  exercises block1.

### The moveset table (all oracle-measured / disasm-pinned)

| move | AI entry | `$AE93` | `$9877` | anim script | span (`$A013`) | strikes(`1E`) | popups(`50`) | dmg | when |
|---|---|---|---|---|---|---|---|---|---|
| **attack-A** "1 punch" | `01 00 05 01` (blk1) | 0 | 0x31 | `CD/7A42` | 157f | **1** | 1 (rendered) | **9** (rec#0 pow12) | un-hit |
| **attack-B** "several punches" | `01 01 06 00` (blk2) | 1 | 0x34 | `CD/7B72` | 224f | **1** | 1 (rendered) | **14-15** (rec#1 pow18) | when-hit |
| **song** "sing" | `02 10 05 00` (blk1) | 0x10 | 0x10 | `CD/9EDA` | 311f | **0** | 1 (rendered) | **1** (min-floor) | un-hit |

---

## A — THE MULTI-HIT HP PIPELINE (disasm) — and the byte-proof GATO does NOT use it

### A.1 The apply routine `C1/EC7F` (fresh disasm `tools/dis.py 1EC7F`, 16-bit A/X)

Damage is staged into THREE per-target hit slots (stride `0x2C`, index `Y = target*4`): **`$B328` = hit-1,
`$B354` = hit-2, `$B380` = hit-3**, each with a 16-bit flag word (`$B32A`/`$B356`/`$B382`). The apply loop
subtracts each staged hit, **gated `BIT #$4000` on that hit's flag word**:

```
C1/EC7F  JSR C1/EC39                       ; pre-pass: clear slots for targets flagged $B32B bit5 ($20)
C1/EC85  LDA $B32B,Y / BIT #40 / BEQ ..    ; hit-1 present?
C1/EC91  LDA $B32A,Y / BIT #4000           ; ** hit-1 multi-hit gate **
C1/EC97  BNE ->ECA3                        ;   gate SET -> SKIP hit-1 subtraction (deferred/multi accounting)
C1/EC99  LDA $5E30,X / SEC / SBC $B328,Y / STA $5E30,X   ; curHP -= hit-1
C1/ECA3  LDA $B356,Y / BIT #4000 / BNE ->ECB5            ; hit-2 gate
C1/ECAB  LDA $5E30,X / SEC / SBC $B354,Y / STA $5E30,X   ; curHP -= hit-2  (only if staged+gated)
C1/ECB5  LDA $B382,Y / BIT #4000 / BNE ->ECC7            ; hit-3 gate
C1/ECBD  LDA $5E30,X / SEC / SBC $B380,Y / STA $5E30,X   ; curHP -= hit-3
C1/ECC7  LDA $5E30,X / BMI ->death / CMP #0 / BEQ ->death; clamp / death test
C1/ECD1  CMP $5E32,X / BCC ->ED17 / LDA $5E32,X          ; below maxHP -> store; else clamp
C1/ED17  STA $5E30,X                       ; ** the curHP write ** (monitor writer C1:ED1A)
```

**HP-write SHAPE: a multi-hit is ONE monitored write of the SUM** — hit-1/2/3 are all subtracted from the same
`$5E30,X` register *before* the single `STA` at `C1/ED17`. So the OAM/HP monitor sees one `C1:ED1A` write per
action even for a 3-hit attack. (This is why the `battle_hp_timing.md §a` ledger shows single writes; it does
NOT rule out multi-hit — the STAGING slots do.) Each hit's popup is spawned by its own `50` ShowDamage in the
anim script (multi-hit = multiple `50`s = multiple numbers). The single-hit STAGE is `C1/EC10`:
`LDA $AD89 / STA $B328,Y` (writer **C1:EC2F**) — writes hit-1 ONLY. hit-2/3 are staged by the multi-hit tech
path (tech Effect-Header type `08 MULTIHIT`, `community_leads.md` L3.2), which GATO has no action for.

### A.2 Oracle proof — GATO's physicals are SINGLE-HIT (MASH, attack-B every turn)

`ct_probe --monitor "B328:2,B354:2,B380:2,B356:2,B382:2,5E30:2,6130:2,9877:1,AE93:1,AEEB:1" --from
gato_press_action.state --input <MASH> --frames 1450 --diff --writer`. After the f287 init clears them,
**`$B354` (hit-2) and `$B380` (hit-3) hold `0000` for the ENTIRE fight**; only `$B328` (hit-1) is ever staged
(writer **C1:EC2F**), once per action:

| frame | selector | staged `$B328` | `$B354`/`$B380` | curHP applied |
|---|---|---|---|---|
| f476 | (attack-A) | `0900` (=9)  | **0000 / 0000** | (f635) Crono 70→61 −9 |
| f769 | 0x34 attack-B | `0E00` (=14) | **0000 / 0000** | (f998) Crono 61→47 −14 |
| f1155| 0x34 attack-B | `0F00` (=15) | **0000 / 0000** | (f1377) Crono 47→32 −15 |

**⇒ attack-B is single-hit-DAMAGE.** The "several punches" is purely the animation (§B.2).

---

## B — THE THREE ANIM SCRIPTS decoded (multi-track, 1E/50 counted)

Anim script = `90`/`98`-header (two 16-bit masks at bytes 0-1 / 2-3, byte-swapped, `popcount` = #tracks;
loader `C1/4562`) + one 2-byte track pointer per set mask bit, into bank CD. Decoded with the engine-mirror
op-length resolver (ROM jump-table `C1/7A6B` + the `battle.c battleAnimOpLen` fallback). `1E ss` = super-command
= the visual STRIKE (`C1/56CC`); `50-55` = ShowDamage popup (`C1/5ADB`); `20 dd` = pause dd; `78 sd` = sound.

**NOTE on the duplicate ShowDamage track.** All three scripts list the ShowDamage/strike track TWICE (the two
mask words produce mirrored track slots — e.g. attack-A tracks = [`95D3`,`7A4E`,`95D3`,`963E`]). The oracle
renders only **ONE** visible popup per action (§C proof: attack-A shows one "9", the song one "1"), so the
duplicate is a script-structure artifact, not two numbers.

### B.1 attack-A `CD/7A42` — the plain punch (masks 9000/9000, 4 tracks)
- `95D3` (×2): `72 0D 36 03 08 24 02 06 03 20 0F **50** 2E 01` → the **ShowDamage** popup track.
- `7A4E`: `... 78 46 72 0A 06 09 ...` → a single windup/anim track (no strike, no popup).
- `963E`: `24 01 20 14 **1E 49** 36 06 03 00` → the **ONE strike** (`1E ss=49`).
- **TOTAL: 1 strike, 1 rendered popup.** Single punch.

### B.2 attack-B `CD/7B72` — the FLURRY (masks 9000/9000, 4 tracks)
- `7B7E`: `72 0D 36 02 09 20 50(pause80) 06 03 24 02 20 0F **50** 2E 01` → main/ShowDamage (the pause 80 is why
  the span is long).
- **`7B8F`: `24 01 [78 46 69 00 06 05 20 03 6A 06 03 20 04] ×7 20 0A 36 06 03 00`** → **the "several punches"**:
  the block `punch-sound(78 46) + palette-flash(69 00 .. 6A) + 2 anim frames + pauses` repeated **7 times** (pause
  sum 7×7+10 = 59). NO `1E`, NO `50` — it is a purely cosmetic/audio flurry of 7 rapid punches.
- `95D3`: ShowDamage (shared, same as attack-A).
- `963E`: `1E 49` → the **ONE strike** (shared, same as attack-A).
- **TOTAL: 1 strike, 1 rendered popup, +7 cosmetic punch cycles.** Single-hit damage, multi-punch presentation.

### B.3 song `CD/9EDA` — the tech (masks 98F8/8000, **9 tracks**)
- `9EF0`: `... 20 20 06 03 **50** 2E 01` → ShowDamage popup.
- `9F0A`: `24 0A 02 09 78 AA ...` → sound `78 AA` (the song's music note), no strike/popup.
- `9F21`/`9F3B`/`9F4F`/`9F62`/`9F76`: `73 00 24 0x 1A 36 ... 70 02 00 09 85 xx A8 30 71 00` → **five moving-effect
  tracks** (the musical-note sprites `70`/`85 xx A8` sweeping across the party), no strike/popup.
- `9F8B`: `02 06 20 1E 06 05 20 0F 06 03 20 0F **50** 2E 01` → ShowDamage popup (duplicate).
- **TOTAL: `1E` = 0 (NO strike anywhere in all 9 tracks → NO hurt-flash), `50` = present (1 rendered popup).**
  (The `1E 09` bytes at `CD/9F28` are the OPERAND of counter-op `3A`, not a strike opcode.)

---

## C — THE SONG'S DAMAGE (predicted vs measured)

### C.1 Path: TECH, formula type 0 (NONE), min-1-damage floor
HOLD (`--monitor "9877,AE93,AEEB,AEEF,AD89,5E30,5EB0,6130,B328,B354" --input 100:8,0:2700`), the song fires
autonomously (block1) at f1148:

```
f1148  $AE93 = 10                          ; block1 TECH `02 10 05 00` -> p1=0x10 -> $AE93 (C1:AD08)
f1149  $AEEB = 00   $AEEF = 00 (C1:D83F)   ; formula type = 0 (NONE), power = 0  <- the song's damage RECORD
f1149  $AD89 = 0100 (writer C1:DCF9)       ; final damage = 1
f1151  $9877 = 10   (C1:469A)              ; song anim dispatched (the third, C1/46xx, path; span 311)
f1462  $5EB0 : 4100 -> 4000 (C1:ED1A)      ; MARLE curHP 65 -> 64  (-1)
f2411  $B328 = 0100 (C1:EC2F)              ; a later song: staged hit-1 = 1 (single hit)
```

**The floor** (`tools/dis.py 1DCED`): after the variance+bonus add, if the accumulated damage is 0 it is bumped
to 1 — this is where the song's "damage" comes from:
```
C1/DCF0  LDX $AD89 / BNE ->DCF9            ; damage != 0 -> keep it
C1/DCF5  INX / STX $AD89                   ; ** damage == 0 -> min-1 floor ** (writer C1:DCF9)
```
The song's record is formula-type 0 (NONE) + power 0 → base 0, bonus 0 → **floored to 1**. So the song deals a
**deterministic 1** (both captures `$AD89`=1). It is not a "real" damage tech — the min-1 floor makes a
0-power flavor/taunt tick 1.

### C.2 Popup truth (oracle OAM shadow `$7E0700`, monitor timeline)
`--monitor "0700:512" --input 100:8,0:1500`, digit detector (tile 0x190+d, attr bit0, on-screen):

- **Song popup = "1"**, OAM `(172, ~80-103)` (Marle's right-side column), rising with the `battle_actionfx.md`
  P1.4 arc, **born f1399 (= span_end 1462 − 63, the enemy popup-lead law) → HP applied f1462**.
- **Marle-in-pal4 (hurt-flash) frames = NONE** — the victim does **not** flash (matches §B.3's 0 strikes and the
  user's "PC shows NO hurt animation").
- (Same run confirmed **attack-A's popup = "9" over Crono** at `(130, ...)` — ONE popup, refuting the "duplicate
  = two numbers" worry.)

**Predicted (disasm: type-0/power-0 → floor 1) == measured (`$AD89`=1, Marle −1, popup "1").** The song's popup
value = the applied damage (`battle_actionfx.md` P1.2), positioned on the random-PC target it resolved (Marle
here; the AI targeting byte is `05` = random living PC).

---

## D — attack-A vs attack-B DAMAGE (record #0 vs #1)

Both physicals are formula type 4 (ENEMY_PHYS, `C1/E27B`, defsel `0x3E`) and single-hit, but they load
**different enemy attack records** — the record index `$B2C8` tracks `$AE93` (writer C1:D92A):

```
attack-A: $AE93=0 -> $B2C8=00 -> CC/89C6 rec#0  $AEEF=0x0C=12  -> $AD89 = 9   (f475-476)
attack-B: $AE93=1 -> $B2C8=01 -> CC/89D2 rec#1  $AEEF=0x12=18  -> $AD89 = 14  (f769-772; 15 @f1155)
```
Records dumped (`CC/89C6`, 12 bytes, `+5 type / +6 defsel / +9 power`):
```
rec#0 CC/89C6: 03 00 00 3A 3B 04 3E 10 00 0C 00 80   (type04 defsel3E POW=12)   <- attack-A
rec#1 CC/89D2: 03 00 00 3A 3B 04 3E 10 00 12 00 80   (type04 defsel3E POW=18)   <- attack-B
```
They are **byte-identical except +9 (power)**. base = `power × (A[+0x3D]×2) / 18`; with GATO `A[+0x3D]=7`:
attack-A = `12×14/18 = 9`, attack-B = `18×14/18 = 14` (× `(256−def)/256` ≈ ×1 for Crono, + small bonus). So the
9-vs-14 difference is purely the record power (12 vs 18).

---

## E — OUR CURRENT-BEHAVIOUR AUDIT (`src/battle.c`)

The AI half is already correct (interpreter ported, `battle_enemy_move_target.md` Wave3): `battleAiRunTurn`
picks block1 (attack-A + song) un-hit / block2 (attack-B) when-hit; leaving GATO alone DOES cycle attack-A→song.
The RENDER/DAMAGE half is where all three moves collapse:

| move | ROM (oracle) | OUR ENGINE | verdict |
|---|---|---|---|
| **attack-A** | 1 strike + 1 "9" popup; power-12 record (dmg 9) | `battleActionSpan`: `$9877=0x31`, span 157; `battleAnimParse` → `animHasStrike=1`/`animHasShowDmg=1` → ONE strike (`battleAdvanceAction` L1611) + ONE popup (L1642). Damage = `battleComputeDamage` with **hardcoded record #1 (power 18)** (`battle.c:756`) → ~14, **not 9**. | render OK-ish; **dmg wrong** (uses rec#1) |
| **attack-B** | 1 strike + 1 popup + **7× flurry** (sound/flash); power-18 (dmg 14) | Same code path as attack-A: `$9877=0x34`, span 224, ONE strike + ONE popup. The **7× flurry track `CD/7B8F` is NOT modelled** (our anim layer only tracks `hasStrike`/`hasShowDmg` booleans, `battle.c:1153-1154` — it cannot count or replay the flurry). | renders identically to attack-A → **user can't tell A from B** |
| **song** | 0 strikes (no flash) + 1 **"1"** popup; −1 to a random PC | `battleActionSpan`: `$9877=0x10`, span 311, `isTech=1`. `battleOpenAction` sets **`actValue = isTech ? 0`** (`battle.c:1530`) → `battleAdvanceAction` spawns the popup with **value 0** (L1642, `wantShowDmg=1` because the song script has a `50`) and applies 0 (no HP loss). No flash (`wantStrike=0`, 0 strikes → correct). | **"0" popup + no HP drain = report (1) EXACTLY** |

So report (1) = the song (0 instead of 1, no flash which is canonical). "Only one attack" = block2 dominance
(canonical) COMPOUNDED by A/B rendering identically (flurry missing) and the song being a broken "0".

Relevant constants read: `battle.c` `CT_SPAN_GATO_A 157`, `CT_SPAN_GATO_B 224`, `CT_SPAN_GATO_SPEC 311`,
`CT_GATO_STRIKE ≈27` (`battle_actionfx.md` A3.1), `CT_FX_POPUP_LEAD_ENEMY 63`, `CT_FX_FLASH_DUR 9`.

### Strike/popup FRAMES mapped to the script (from `battle_hp_timing.md §a`, monitor timeline)
| move | freeze on | strike (`1E`) | popup (`50`) born→last | curHP write (span end) |
|---|---|---|---|---|
| attack-A (157f) | f478 | f532 (rel 54) | f572→f628 (born span−63) | f635 |
| attack-B (226f) | f772 | f796 (rel 24) | f935→f991 (born span−63) | f998 |
| song (311f)    | f1151 | — (none) | f1399→~f1455 (born span−63) | f1462 (−1) |

---

## F — IMPLEMENTATION SPEC (derive-only lane; nothing applied)

**F.1 SONG damage — fix the "0" popup (the primary user-visible bug).**
- In `battleComputeDamage` (or a tech branch), give a NONE-formula / 0-power tech the **min-1-damage floor**
  (`C1/DCF3-DCF6`): a tech whose computed base is 0 deals **1**. For GATO's song specifically the derived value
  is a fixed **1**.
- In `battleOpenAction` (`battle.c:1530`), STOP hardcoding `actValue = isTech ? 0`. Compute the tech's real
  damage (for the song = 1) so `battleAdvanceAction` spawns a **"1"** popup and `battleApplyDamage` drains −1
  from the resolved target. Keep `wantStrike=0` (the song has 0 `1E` → no flash — already correct).
- Result: the song shows **"1"** over the random-PC target with no hurt-flash, −1 HP — canonical. ⚠ Re-pin
  `field-battleloop-*` after (the song now drains HP, shifting victory HP/frames).

**F.2 attack-B FLURRY — make the "several punches" visible (distinguish B from A).**
- The multi-punch is a **7× flurry ANIMATION**, not multi-hit damage. When `$9877=0x34`, replay the `CD/7B8F`
  cadence during the 224f span: **7 repetitions of {punch-sound `0x46` + palette-flash + 2 anim frames}** over
  the span's middle, in ADDITION to the single strike (`1E`, rel ~24) + single popup. The bake precedent is the
  GATO claw-swipe (`battle_actionfx.md` A4: 3 swipe frames GS1/GS2/GS3 baked from the MASH counter — those ARE
  attack-B's flurry frames, deduped by pixel-hash; extend to the 7-cycle sound/flash schedule).
- Do NOT add multi-hit damage: keep ONE strike + ONE popup + ONE HP subtraction (§A.2 proof). The flurry is
  cosmetic/audio only.

**F.3 attack-A DAMAGE — use the right record (fidelity).**
- `battleComputeDamage` hardcodes enemy record #1 (power 18). GATO's physical record index = **`$AE93`**
  (`$B2C8` tracks it): attack-A → `CC/89C6` rec#0 (power **12**, dmg 9); attack-B → rec#1 (power **18**, dmg 14).
  Read `rom[ROM_ENEMY_ATK_REC + s[0xAE93]*12 + 9]` for the power. (Today only attack-B is exercised by any pin,
  so attack-A's over-damage is latent.) ⚠ Re-pin after — but a HOLD/un-hit fight is needed to pin attack-A.

**F.4 Multi-hit staging — NO CHANGE for GATO.** `$B354`/`$B380` + `BIT #$4000` are the general multi-hit path
(tech Effect-Header type 08); GATO stages only `$B328`. Our `battleComputeDamage` single-`$B328` stage is
correct for GATO. (Implement the general multi-hit path only when a real multi-hit enemy/tech is captured — the
apply loop `C1/EC7F` in §A.1 is the contract: N staged hits gated `BIT #$4000`, summed into ONE `$5E30` write,
one `50` popup per hit.)

**F.5 "Only one attack" — no AI change.** The block1/block2 selection is already byte-correct. The variety
returns once F.1 (song shows 1) + F.2 (attack-B flurry) land: leaving GATO un-hit already triggers attack-A +
song in our engine (Wave3), they just render wrong/identically today.

---

## G — OPEN questions
- **G.1 — `$B2C8` (enemy attack-record index) source byte.** It equals `$AE93` (0/1) in every capture, but with
  two data points the exact ROM read (writer C1:D92A) — `$AE93` vs `$AEE5−5` (05→0 / 06→1 both fit) — is not
  disambiguated. Derive by an enemy with ≥3 attack indices before generalizing the record-index rule.
- **G.2 — The song's SUPER-COMMAND path `C1/469A`** (the third anim dispatch, `$9877=0x10` for a tech) is cited
  but not byte-decoded; its span (311f) is observed/pinned, its internal makeup is the bank-CC anim boundary.
- **G.3 — attack-A one-turn power context.** attack-A's `$AEEF=12` was captured once (HOLD f475); a second
  un-hit capture would confirm rec#0 is deterministic (not variance).
- **G.4 — Song target distribution.** The `05` random-PC resolver landed on Marle here; whether the song can hit
  Crono / how ties resolve is the `battleAiTarget(05)` thirds+re-roll (`battle_enemy_move_target.md` §C), already
  ported — not re-swept here.
- **G.5 — Flurry frame bake.** The `CD/7B8F` 7-cycle content (the distinct punch-frame tiles) needs a capture
  like `battle_actionfx.md` A4 (which baked 3 GS frames from the counter) extended to the full 7-cycle sound
  schedule — an anim-bake follow-up, not derivable from bytes alone (the C1/CC tile streamer boundary).
- **G.6 — Other enemies' multi-hit / techs.** Only GATO captured; the §A.1 multi-hit apply + the tech min-1
  floor generalize, but a real MULTIHIT (type-08) tech or a >0-power enemy tech is unobserved (magic /102.4
  path still un-derived, `community_leads.md` L6.2).

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-...-e5608df4-.../scratchpad/`
- `ct_probe.exe` (oracle copy of build/ct.exe), `song_oam.txt` (OAM-shadow monitor over the HOLD song span).
- `/tmp/animdec.py` (multi-track anim decoder, engine-mirrored op lengths → the §B decodes), `/tmp/animdump.py`
  / `/tmp/tdump.py` (raw script/track dumps), `/tmp/parse_oam.py` (digit-popup + pal4-flash detector, §C).
- Repro commands inlined per section (MASH `('100:20,0:40')×24`; HOLD `100:8,0:2700`). Disasm: `tools/dis.py
  1EC7F` (apply), `1DCED` (min-1 floor), `14562` (anim header loader); records `CC/89C6`.
