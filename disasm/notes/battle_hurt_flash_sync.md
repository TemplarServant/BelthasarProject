# BATTLE HURT-FLASH — the victim's white flash is a SCRIPT OPCODE PAIR ($993C via ops 0x69/0x6A), not a pinned offset

User report (translated from Portuguese): *"crono is getting a 'flash' when taking damage **before the animation is finished**
(out of sync)"* — Crono, as the VICTIM of an enemy attack, gets the white damage flash out of sync with
the attacker's animation (it lands early, before the strike).

**Verdict: REPRODUCED, and the mechanism is derived byte-for-byte.** The flash is not a timed effect at all.
It is a **per-combatant OAM-palette byte `$993C,X`** that the action's own anim script flips to `0x08`
(= OBJ palette 4, the all-white bake `ctBattleFxFlashPal`) with opcode **`69`** and restores from
`$9888,X` with opcode **`6A`**. Both the ON frame and the DURATION are emitted by the script.
Our engine ignores both opcodes (`src/battle.c:5202` — `default: break; // facing/palette/... : inert`)
and instead arms a host-side 9-frame one-shot at a **hard-pinned `rel`** (`CT_GATO_STRIKE = 27` from
dispatch for *every* enemy, `src/battle.c:2627` / `:5300-5302` / `:5462`). Since commit "battle anim script =
LIVE machine" the enemy's ANIMATION is driven live by the ported interpreter (`battleAscTick`) while the
FLASH is still driven by that constant — the two clocks are unrelated. That is the desync, literally.

---

## 1. THE ROM MECHANISM (both directions: disasm → oracle, oracle → disasm)

### 1.1 `$993C,X` = the combatant's live OAM palette field; `$9888,X` = its default

Battle setup, `CC/EA98` (`python tools/dis.py CEA98 0x30 1 0`):

```
CC/EAAB  LDA $29C7,X      ; the combatant's palette NUMBER from its setup block
CC/EAAE  ASL A            ; <<1  -> the OAM attribute palette field
CC/EAAF  STA $9888,Y      ; ** the DEFAULT **
CC/EAB2  STA $993C,Y      ; ** the LIVE **
```

Consumers read it as a palette index: `CF/FBEF  LDA $993C,X / ASL A x4 / TAY / copy 0x18 bytes $2102,Y -> $2302,Y`
(the CGRAM shadow row) and `C1/30CC LDA $993C,X / JSR C1/0111 / copy 0x1A bytes $2100,Y -> $2300,Y`.

Observed values (oracle, `gato_press_action.state` after boot, f292):
`$9888 = 0A 0C 0E FF FF FF 02 FF FF FF FF`, `$993C = 0A 0C 0E 00 00 00 02 00 00 00 00`
→ Crono `0x0A`>>1 = **pal5**, Marle `0x0C`>>1 = **pal6**, GATO (slot idx 6) `0x02`>>1 = **pal1** — exactly the
stance palettes `battle_stance.md` / `battle_actionfx.md` P3 baked. `0x08`>>1 = **pal4** = the all-white flash.

### 1.2 op `69 pp` = FLASH ON — handler `C1/5DD3` (`python tools/dis.py 15DD3 0xC0 1 0`)

```
C1/5DD3  INY / LDA [$E6],Y / STA $80     ; the operand pp
C1/5DD8  LDA #02 / STA $8E              ; op length 2
C1/5DDE  LDA $A010 / CMP #04 / BCS 5E01 ; $A010 = the running TRACK index
C1/5DE5  TAX / LDA $A014,X / TAX        ;   track<4: role -> combatant
C1/5DEA  CMP #03 / BCC 5DFA             ;   (a PC skips the enemy CGRAM half)
C1/5DEE  LDA $99D2 / BEQ 5DFA / LDA $A08C,X / BEQ 5DFA / INC $82
C1/5DFA  LDA #08 / STA $993C,X          ; ** THE FLASH: OAM palette field := 08 (pal4 white) **
C1/5E01  TDC / TAX                      ;   track>=4: walk the TARGET LIST $A018,X
C1/5E03    LDA $A018,X / BMI 5E1D / TAY / ... / LDA #08 / STA $993C,Y / INX / BRA 5E03
C1/5E1D  (operand pp * 24) -> stage 24 bytes of D1/4D90 into $2382 (the flash CGRAM row)
C1/5E7F  INC $A00F                      ; ** the op is FREE: zero frame cost **
C1/5E82  LDA $8E / JMP C1/75BB          ;   advance 2
```

### 1.3 op `6A` = FLASH OFF — handler `C1/5E87` (`python tools/dis.py 15E87 0x60 1 0`)

```
C1/5E87  LDA #01 / STA $8E              ; op length 1
C1/5E8B  LDA $A010 / CMP #04 / BCS 5EC9
C1/5E92  TAX / LDA $A014,X / TAX
C1/5EA8  LDA $9888,X / STA $993C,X      ; ** RESTORE the default palette **
C1/5EB0  (enemy path) copy $2000,X -> $2200,X, 0x100 bytes  ; the CGRAM restore
C1/5ECB  (track>=4) same over the $A018 target list -> $993C,Y
C1/5EDA  LDA $8E / JMP C1/75BB          ; no INC $A00F -> op 6A COSTS one frame
```

**Oracle writer PCs confirm both** (`--monitor "993C:11" --diff --writer`):
flash ON = **`C1:5DFF`** (the PC after `STA $993C,X` @C1/5DFC), flash OFF = **`C1:5EAE`** (after `STA $993C,X`
@C1/5EAB). Nothing else touches `$993C` inside an action window.

### 1.4 `$993C` → the rendered OAM attribute, byte-exact

`--monitor "0710:20,993C:1"` on the OAM shadow `$7E0700` (proven `== .oam`, `battle_actionfx.md` ADDENDUM A2),
Crono = OAM entries 4-7 = `$0710..$071F`, each `[x,y,tile,attr]`:

```
f530  0710 = 866F08**3A** 766F0A**3A** 867F0C**3A** 767F0E**3A**   993C=0A     attr pal = (3A>>1)&7 = 5
f531  ... unchanged ...                                            993C=08 *   <- the store
f532  0710 = 866F08**38** 766F0A**38** 867F0C**38** 767F0E**38**   993C=08     attr pal = (38>>1)&7 = 4  ** WHITE **
f534  (attrs still 38)                                             993C=0A *   <- the restore
f535  0710 = ...3A ...3A ...3A ...3A                               993C=0A
```

So the attribute's palette bits ARE `$993C`'s bits 1-3, one frame behind (the shadow is published next frame).
Rendered flash = **f532, f533, f534 = 3 frames**.

---

## 2. THE SCRIPTS — the ON frame *and* the duration are literals in the bytecode

Enemy scripts: `CD/5FF0[$9877*2]` → header (word-1 mask = the `$A0B3==0` dispatch), track **3 = the primary
victim** (`$A014` role fill `CC/F156`). Supers: `1E ss` → `CE/0000[ss*2]` (`C1/56CC`).

| action | `$9877` | victim block | the flash pair |
|---|---|---|---|
| GATO attack A | `0x31` | CD/**7A4E** | `24 01 / 20 14 / 78 46 / 72 0A / 06 09 / 20 02 / **69 00** / 20 02 / **6A** / 76 00 03 / 0C / A9 04 / 20 0A / 36 / 72 0A / 06 03 / 00` |
| GATO attack B (flurry) | `0x34` | CD/**7B8F** | `24 01 /` **7×** `[ 78 46 / **69 00** / 06 05 / 20 03 / **6A** / 06 03 / 20 04 ]` |
| Hetake attack | `0x14` | CD/**72F4** → `1E 44` → CE/**DB10** | `**69 00** / 20 04 / 06 05 / 20 08 / **6A** / 20 08 / 1F` |
| PC physical (victim block) | — | CE/**D428** | `72 0A / **69 00** / 20 02 / 06 05 / 20 02 / **6A** / 03 05 / 06 03 / 50` |

Note GATO-A carries the pair **inline** while the Hetake carries it inside the strike **super** `1E 44`
— i.e. there is no fixed position for it and no fixed distance from dispatch.

---

## 3. MEASURED ORACLE TIMELINES (freeze-on `$A013` = rel 0)

### 3.1 GATO fight — `gato_press_action.state`

```
MASH=$(python -c "print(','.join(['100:20','0:40']*24))")
"$TMP/ctwf/flash/ct_probe.exe" --monitor "993C:11,9888:11,A013:1,9877:1,5E30:2,6130:2" \
   --from gato_press_action.state --input "$MASH" --frames 1250 --diff [--writer]
```

| span (freeze) | `$9877` | attacker→victim | `$993C` := 08 (ON) | := default (OFF) | dur | span end |
|---|---|---|---|---|---|---|
| f478-f635 | 0x31 GATO-A | GATO→**Crono** | f531 = **rel 53** | f534 = rel 56 | **3 f** | rel 157 |
| f643-f768 | 0x00 PC melee | Crono→GATO | f685 = rel 42 | f694 = rel 51 | 9 f | rel 125 |
| f772-f998 | 0x34 GATO-B | GATO→**Crono** | f795/811/827/843/859/875/891 = **rel 23, 39, 55, 71, 87, 103, 119** (period 16) | +7 each | **7×7 f** | rel 226 |
| f1005-f1151 | 0x0F Marle | Marle→GATO | f1070 = rel 65 | f1078 = rel 73 | 8 f | rel 146 |
| f1155-… | 0x34 GATO-B | GATO→**Crono** | f1177 = rel 22 | … | | |

### 3.2 Forest fight — `saves/battle_re/forest_battle_3hetake.state` (a DIFFERENT enemy, DIFFERENT arena)

```
"$TMP/ctwf/flash/ct_probe.exe" --monitor "993C:11,9888:11,A013:1,9877:1,AE91:1" \
   --from saves/battle_re/forest_battle_3hetake.state --input "$MASH" --frames 1400 --diff --writer
```

| span | `$9877` | actor `$AE91` | ON | OFF | dur |
|---|---|---|---|---|---|
| f51-f228 (177) | 0x14 Hetake | 3 (enemy) → **Crono** | f102 = **rel 51** (C1:5DFF) | f118 = rel 67 (C1:5EAE) | **16 f** |
| f236-f377 (141) | 0x00 Crono melee | 0 → Hetake slot 3 | f294 = rel 58 | f302 = rel 66 | 8 f |
| f429-f574 | 0x0F Marle | 1 → Hetake slot 4 | f494 = rel 65 | f502 = rel 73 | 8 f |
| f847-f980 | 0x00 Crono melee | 0 → Hetake slot 5 | f898 = rel 51 | f906 = rel 59 | 8 f |

(`CF:FE5C` writes `08` to a *dead* enemy's slot after span-end — that is the death/dissolve path
`CF/FE59`, a different mechanism, not this one.)

**Therefore: the victim-flash offset is per-script (53 / 23 / 51) and the duration is per-script
(3 / 7 / 16 / 8). There is no constant anywhere in the ROM.**

---

## 4. OUR ENGINE — measured, side by side

```
CT_BATTLE_SCRIPT=n:500 CT_BATTLE_ASCTRACE=1 CT_BATTLE_TRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  build/Belthasar.exe --assets ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram
CT_BATTLE_MASHA=1 CT_BATTLE_ASCTRACE=1 CT_BATTLE_TRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  build/Belthasar.exe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
```

Forest, Hetake attack `$9877=0x14`, dispatch f293 (rel 0), span 202:

```
asc arm f314   rel=21   sel=14 mask=9001 tracks=3 actor=3 victim=1
battletrace f320 strike           <-- rel 27   ** OUR FLASH (CT_GATO_STRIKE), 9 frames **
asc f328  rel=35  t0 SLIDE (120,63)->(127,132) speed=2 n=34     <-- the lunge only STARTS here
asc f364  rel=71  t3 SUPER 44 -> CE/DB10                        <-- the script's real strike; CE/DB10[0] = `69 00`
ascposearm f369 cmb1 anim=05      <-- rel 76: the VICTIM's hurt pose window (the `06 05` inside that super)
asc f392  rel=99  t3 track-end
```

Our own interpreter reaches the flash opcode at **rel 71**; our flash fires at **rel 27** — **44 frames
before the animation it is supposed to belong to**, and it is over (rel 36) 35 frames before the victim
even changes pose. Against the oracle it is 24 f early (51 → 27) and 7 f short (16 f → 9 f).

GATO fight, attack B `$9877=0x34`, dispatch f331, span 224:

```
battletrace f331 anim-coverage $9877=34 ... CD/7B72 ... ops: ... 50=showdmg **69 6A** 72 78
asc arm f352  rel=21
battlesfx f353 rel=22  op78 track=3 id=46     <-- the `78 46` that IMMEDIATELY precedes `69 00`
battlesfx f367 rel=36 / f381 rel=50 / f395 rel=64 / f409 rel=78 / f423 rel=92
battletrace f358 strike                       <-- rel 27  ** OUR FLASH **
```

so our own interpreter would place the pulses at **rel 22, 36, 50, 64, 78, 92** while the pinned
flurry block (`src/battle.c:5508-5516`, `CT_FLURRY_FLASH_WINDUP 26` / `DWELL 8` / `CYCLES 6`) places them at
**26, 42, 58, 74, 90, 106** — drifting 4, 6, 8, 10, 12, 14 frames apart, plus a 7th oracle pulse we never
fire (oracle: 23, 39, 55, 71, 87, 103, **119**). The `anim-coverage` line proves the parser already SEES
`69` and `6A` in the script; the interpreter simply has no case for them.

| | ORACLE (`$993C`) | OURS (`flashLeft`) |
|---|---|---|
| GATO-A (0x31) on Crono | ON rel **53**, 3 f | ON rel **27**, 9 f |
| GATO-B (0x34) on Crono | rel 23,39,55,71,87,103,119 · 7 f | rel 26/27 (strike, 9 f) + 26,42,58,74,90,106 · 7 f |
| Hetake (0x14) on Crono | ON rel **51**, 16 f | ON rel **27**, 9 f |
| Crono melee on enemy | ON rel 51-58, 8-9 f | ARRIVAL+19 (`CT_PC_ATK_FLASH`), 9 f |

---

## 5. OUR BUG — exact sites

| file:line | what it does | why it is wrong |
|---|---|---|
| `src/battle.c:5300-5302` | `int strikeRel = isPc ? ... : CT_GATO_STRIKE;` | one constant (**27**) for **every** enemy and every attack. The ROM has no such quantity. |
| `src/battle.c:2627` | `#define CT_GATO_STRIKE 27` | measured off ONE GATO render; the canonical event is the script's `69`, whose position is 53 (GATO-A), 23 (GATO-B), 51 (Hetake). |
| `src/battle.c:5433-5434,5462` | `if (!h->actApplied && rel >= strikeRel) { ... h->flashTarget = h->actTarget; h->flashLeft = CT_FX_FLASH_DUR; h->fxFlashPal = battleFlashPal(h->actTarget); }` | **THE FLASH SITE.** It is armed off `rel`, not off the script. |
| `src/battle.c:2714` | `#define CT_FX_FLASH_DUR 9` | the ROM has no duration — `6A` ends it. Measured 3 / 7 / 8 / 16. |
| `src/battle.c:5264` | `if (h->flashLeft > 0 && --h->flashLeft == 0) {...}` | the host-side countdown that stands in for `6A`. |
| `src/battle.c:5202` | `default: break; // facing/palette/shadow/counter: inert` | **the omission**: the live interpreter walks `69`/`6A` and does nothing. |
| `src/battle.c:5508-5516` | the `$9877==0x34` flurry re-arm (`CT_FLURRY_FLASH_WINDUP/DWELL/CYCLES/DUR`) | a hand-fitted replica of what `CD/7B8F`'s seven `69`/`6A` pairs already say; wrong phase, wrong count. |
| `src/field.c:3027-3030`, `:2870,2902,3591,3817` | `battleEnemyFlashing()` / `flashPal == pal` render gate | reads the host one-shot instead of a per-combatant palette byte. |

Our engine models neither `$993C` nor `$9888` (`grep -n "993C\|9888" src/*.c src/*.h` → no battle hits).

---

## 6. MINIMAL MANDATE-SAFE CHANGE (derived; NOT applied — read-only task)

1. **Model `$993C`/`$9888`.** Add `uint8_t objPal[11]` (live, `$993C`) + `uint8_t objPalDef[11]` (`$9888`)
   to `CtBattleHost`, both seeded at battle boot by the exact `CC/EAAB-EAB2` law
   (`LDA $29C7,X / ASL A / STA $9888,Y / STA $993C,Y`) — i.e. the combatant's own setup-block palette
   number, shifted left by one. ⚠ Our engine currently has no `$29C7` reader; the nearest thing is
   `battleFlashPal()` (`src/battle.c:2761`), which is a hardcoded `idx==0?5:idx==1?6:1` and is ALREADY
   listed as a de-hardcode item (`battle_3pc_lucca_frog.md` G5). Seeding from `battleFlashPal(c) << 1`
   reproduces the current 2-PC fixtures byte-for-byte but inherits that hardcode; seeding from `$29C7,X`
   is the canonical form and closes G5 at the same time. Do not invent a third source.
   ⚠ **The `$29C7,X` index/stride was NOT pinned here** — a `--read "29C0+48"` on `gato_press_action.state`
   after settle gives `07 08 0F 00 B3 00 40 02 00 80 7F 00 …`, which does not read off as `05/06/…` at a
   stride I confirmed. The load-bearing facts (the `ASL A` and the two stores) are disassembled and the
   RESULT is observed (`$9888 = 0A 0C 0E FF FF FF 02 …`), so the mechanism stands; only the seed's source
   index is open. Nothing in the fix depends on it — the restore value is `$9888`, i.e. "the combatant's
   normal palette", which the renderer already resolves.
2. **Execute the two opcodes** in `battleAscTick`'s switch (next to `case 0x72`, `src/battle.c:~5107`),
   both ROM-cited and resolution-identical to `C1/5DD3` / `C1/5E87`:
   ```c
   case 0x69:  /* C1/5DD3: $993C[role] := 08 (OBJ pal4 white). Free op (INC $A00F @C1/5E7F), len 2 */
     if (t < 4) { int c = h->ascRole[t]; if (c >= 0 && c < 11) h->objPal[c] = 0x08; }
     else for (int i = 0; i < h->actTgtN; i++) { int c = h->actTgtList[i]; if (c>=0&&c<11) h->objPal[c]=0x08; }
     break;
   case 0x6A:  /* C1/5E87: $993C[role] := $9888[role]. Costs one frame, len 1 */
     ... same resolution, h->objPal[c] = h->objPalDef[c]; break;
   ```
   (`t < 4` vs the target-list walk is the handler's own `LDA $A010 / CMP #04 / BCS` branch, not a heuristic.)
3. **Render from the byte.** `battleEnemyFlashing(h, c)` → `h->objPal[c] == 0x08`, and the PC path's
   `flashPal == pal` test (`src/field.c:2902`) → `h->objPal[charCombatant] == 0x08`. One source of truth.
4. **Delete the stand-ins for the ENEMY-attacker path**: the `flashTarget/flashLeft/fxFlashPal` arm at
   `src/battle.c:5462` and the whole `$9877==0x34` flurry block `:5508-5516` become dead for `!isPc`
   (the seven `69`/`6A` pairs in `CD/7B8F` reproduce the flurry exactly, with the 7th pulse we currently
   lack). Keep the knockback / hurt-pose / crit-wash arms where they are — they are separately derived.

**Scope, stated rather than assumed.** `battleAscArm/Tick` runs for **enemy actions and PC techs only**
(`src/battle.c:5276,5294`); a **PC physical** still rides the arrival-anchored bake, so its victim flash must
keep `CT_PC_ATK_FLASH` (which is itself the measured position of `CE/D428`'s `69 00`, so it is consistent)
until the PC physical moves onto the interpreter. That is a boundary, not a scene gate.

**No scene gate, no per-enemy constant, no eyeballed number is introduced** — the ON frame, the OFF frame and
the target set all come out of the ROM bytes the interpreter is already walking.

**Pinned signatures.** `field-battleloop-*` grade victory frame / attack count / HP — all functions of
damage, span and turn order, none of which this touches (`flashLeft` feeds only the renderer). The
screenshot checks that could move are the ones taken inside an action window:
`field-battle-hetake-attack-pose` (`enemy-arrive+75`), `field-battle-hetake-return-pose`
(`enemy-arrive+110`) and `field-battle-forest-lastkill-dissolve` — check each before/after, since the
flash palette is part of those frames. `field-battlehud-gato` is phase-0/f60 and cannot move.

---

## 7. EXACT REPRO COMMANDS

```sh
# scratch probe (never probe with build/ct.exe)
mkdir -p "$TMP/ctwf/flash" && cp build/ct.exe "$TMP/ctwf/flash/ct_probe.exe"
MASH=$(python -c "print(','.join(['100:20','0:40']*24))")

# (a) the flash byte + its writer PCs, GATO fight
"$TMP/ctwf/flash/ct_probe.exe" --monitor "993C:11,9888:11,A013:1,9877:1,5E30:2,6130:2" \
  --from gato_press_action.state --input "$MASH" --frames 1250 --diff --writer

# (b) $993C -> the rendered OAM attribute (same clock, OAM shadow $7E0700)
"$TMP/ctwf/flash/ct_probe.exe" --monitor "0710:20,993C:1" \
  --from gato_press_action.state --input "$MASH" --frames 700 --diff

# (c) a second enemy / second arena
"$TMP/ctwf/flash/ct_probe.exe" --monitor "993C:11,9888:11,A013:1,9877:1,AE91:1" \
  --from saves/battle_re/forest_battle_3hetake.state --input "$MASH" --frames 1400 --diff --writer

# (d) the handlers
python tools/dis.py 15DD3 0xC0 1 0     # op 69  FLASH ON
python tools/dis.py 15E87 0x60 1 0     # op 6A  FLASH OFF
python tools/dis.py CEA98 0x30 1 0     # the $9888/$993C boot fill

# (e) our engine, both arenas
CT_BATTLE_SCRIPT=n:500 CT_BATTLE_ASCTRACE=1 CT_BATTLE_TRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  build/Belthasar.exe --assets ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram
CT_BATTLE_MASHA=1 CT_BATTLE_ASCTRACE=1 CT_BATTLE_TRACE=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  build/Belthasar.exe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
```

---

## 8. OPEN QUESTIONS (explicitly NOT pinned here)

1. **The residual absolute offset.** Even once the flash rides the script, our interpreter reaches the
   Hetake strike super at rel 71 where the oracle's `69` fires at rel 51, and our span is 202 vs the
   oracle's 177. So the whole script runs ~20-25 f slow in that fixture (pre-roll 21/24/33 —
   `battle_enemy_anim_script.md` OPEN 1 — plus the approach-slide rate). Fixing the flash makes it
   *coincide with our own strike* (the causal law); making it land on the oracle's absolute frame is
   the separate span/mover residual.
2. **`$99D2` / `$A08C,X` / `$99D3`, and the `D1/4D90` 24-byte stage into `$2382` / `$2200`** — op 0x69's
   *second* half (an enemy-only CGRAM row swap, skipped for combatant < 3). Not needed for a PC victim
   (the user's case) and NOT derived here. If an ENEMY victim's flash ever looks wrong in colour rather
   than in timing, start there.
3. **op `0x60` (`C1/5D1D`, "switch palette")** is a third palette opcode in the same family; it was not
   reached by any captured script and is not derived.
4. **The one-frame publish lag.** `$993C` stores at f531 but the OAM shadow shows pal4 at f532. Whether
   our renderer should reproduce that 1-frame lag or read the byte live is unmeasured on our side.
5. **PC physicals** stay on the arrival-anchored pin (§6 scope). Whether `CT_PC_ATK_FLASH = 19` still
   matches `CE/D428`'s `69 00` under the current mover was not re-measured here.
6. GATO-B's 7th flash pulse (oracle rel 119) exists in `CD/7B8F` but our pinned block emits only 6; whether
   the interpreter's own walk of that script yields 7 was not run to completion (our `78 46` trace stops at
   rel 92 in the captured window).

---

## Adversarial review (2026-08-22, independent re-run, read-only on `src/`)

Reviewer's default was **"not proven"**. Everything below was re-run from a private snapshot of
`build/ct.exe` / `build/Belthasar.exe`, cwd = repo root. Nothing was taken from the dossier's own tables.

**Bottom line.** The **core mechanism is CONFIRMED** — every ROM address holds the claimed instruction,
every oracle row of §3.1/§3.2 reproduced to the frame, and `$993C` → OAM palette (1-frame lag) is exact.
**Four supporting claims are wrong or overstated**, one of the dossier's own ⚠ open items is now **CLOSED
in the ROM**, and **the §6 fix as written is incomplete** — it would double-flash PC techs, drop the
parse-fail fallback, miss four render sites, and leave the knockback on the old constant.

### A1. ROM addresses — CONFIRMED verbatim (3/3)

`python tools/dis.py 15DD3 0xC0 1 0`, `15E87 0x60 1 0`, `CEA98 0x30 1 0` all hold what §1 claims:

- `C1/5DFA LDA #08 / C1/5DFC STA $993C,X` (ON), `C1/5E17 STA $993C,Y` (target-list arm) — present.
- `C1/5EA8 LDA $9888,X / C1/5EAB STA $993C,X` (OFF), `C1/5ED1/5ED4` (target-list) — present.
- `CC/EAAB LDA $29C7,X / ASL A / STA $9888,Y / STA $993C,Y` — present.
- Costs: `C1/5E7F INC $A00F` is on **every** path of op `69` (`C1/5E54 LDA $82 / BEQ ->5E7F` short-circuits
  the enemy CGRAM half), so `69` is FREE; op `6A` ends `C1/5EDA LDA $8E / JMP C1/75BB` with no `INC`, and
  `C1/75BB` (`REP #21 / ADC $E6 / … / RTS`) contains none either — so **`6A` costs one frame**. Lengths
  `$8E = 02` / `01` confirmed.
- Three cosmetic omissions in §1.2's listing, none load-bearing: `C1/5DDC STZ $82`; the `BRA ->5E1D` at
  `C1/5DFF` (the `TDC/TAX` at `5E01` is *branch-target* code, not a fallthrough); and `dis.py CEA98`
  disassembles from mid-instruction (the real instruction there is `CEA97 CLC / CEA98 ADC $29C2,X`).

> **Correction to a NEIGHBOURING dossier.** `battle_span_emergence.md` line 197 lists
> "`78 sd` sound, `60/69/6A` pal, … → **0** (or set a state)". **`6A` is not 0-cost.** That table row is
> wrong; the engine is not (it reads the cost from the ROM via `battleAscOpFree`, which scans for
> `EE 0F A0`), so nothing downstream is affected — but the row should be fixed before someone trusts it.

### A2. Oracle reproduction — CONFIRMED to the frame (both fixtures, plus the OAM)

`--monitor "993C:11,9888:11,A013:1,9877:1" --from gato_press_action.state --input <MASH> --frames 1250
--diff --writer` reproduced §3.1 **row for row**: boot fill `CC:EAB2`/`CC:EAB5` @f292 →
`$9888 = 0A 0C 0E FF FF FF 02 FF FF FF FF`; GATO-A f478-635 ON **f531** (`C1:5DFF`) OFF **f534**
(`C1:5EAE`); PC melee f643 ON f685 OFF f694; GATO-B f772 with seven pulses f795/811/827/843/859/875/891;
Marle f1005 ON f1070 OFF f1078. The forest run reproduced §3.2 identically: Hetake f51-228 ON **f102**
OFF **f118** (16 f), PC melee ON f294/f898, Marle ON f494, and `CF:FE5C`'s post-span `08` on the dead slot.
**Writer PCs `C1:5DFF` / `C1:5EAE` are the only in-window writers — confirmed.**

`--monitor "0710:20,993C:1,2380:20"` confirms §1.4 **and extends it**:

```
f528  0710 = 866F08 3A ... 3A    993C=0A   2380 = 0900 8614 9E6F 5E53 FF2A ...
f531  0710 = ...    3A ... 3A    993C=08*  2380 = 0900 FF7F FF7F FF7F ... *   <- op 69 stages the row
f532  0710 = 866F08 38 ... 38    993C=08   (attr pal = (38>>1)&7 = 4  ** WHITE **)
f534  0710 = ...    38           993C=0A*
f535  0710 = ...    3A           993C=0A
```

**New, and it corroborates our fixture:** op `69`'s second half is not a detail — it is where the white
comes from. `--read "rom:D1/4D90+32"` = `FF7F` ×12 (BGR555 `0x7FFF` = pure white), and the handler's
`C1/5E40-5E51` writes six more `FF` bytes after the 24. The live `$2380` row at f531 is
`0900 | FF7F×12 | FFFF…`, which is **byte-identical to `src/data/battle_fx.h:51`
`ctBattleFxFlashPal[16] = {0x0009, 0x7FFF×12, 0xFFFF×3}`**. So "`08` = pal4 = the all-white bake" is not an
assumption — pal4 is *made* white by the same opcode, from a ROM table, and our existing fixture already
matches it exactly.

### A3. REFUTED — §2's headline ("the ON frame *and* the duration are literals in the bytecode")

The **mechanism** (bounded by `69`/`6A`) is right. The **claim that the numbers are literals is wrong**, and
the dossier's own data already contradicted it without noticing. The four blocks read back byte-exact
(`--read "rom:CD/7B8F+56,rom:CD/7A4E+40,rom:CE/DB10+24,rom:CE/D428+24"` — §2's table is correct), and the
durations decompose as **`Σ(20 literals) + 1 (the 6A frame) + cost(06 05)`**:

| block | between `69` and `6A` | literal part | measured dur | ⇒ cost(`06 05`) |
|---|---|---|---|---|
| CD/7A4E GATO-A | `20 02` | 2+1 = **3** | **3** | (no `06`) — exact |
| CD/7B8F GATO-B | `06 05 / 20 03` | 3+1 = 4 | **7**, and **6** | 3, and **2** |
| CE/DB10 Hetake | `20 04 / 06 05 / 20 08` | 12+1 = 13 | **16** | 3 |
| CE/D428 PC phys | `20 02 / 06 05 / 20 02` | 4+1 = 5 | **8**, and **9** | 3, and 4 |

`06 aa` is `C1/4EA2`, whose cost is a **blocking pose one-shot** (`battle_span_emergence.md` line 191), i.e.
**emergent from the anim-rate machine, not a literal**. The proof is in the same capture, same fight, same
script: GATO-B's two spans give **ON rel 23 vs 22** and **dur 7 vs 6**, and its period drifts
16/16/16/16/16/16 (f772 span) vs **14/15/16/16/16/16** (f1155 span).

**Consequence for the fix:** executing `69`/`6A` gets the flash **causally** right (it becomes whatever our
interpreter's own clock says) but **cannot** make it byte-exact, because the duration is a function of our
`06` cost model. Concretely, our interpreter already runs GATO-B's seven blocks with a **14-frame** period
(A8 below) against the oracle's 16 — so the ported flash would come out **~6 f on / 14 f period**, not
7/16. That is a strictly better error than today's, but §6 should not be read as "byte-exact after this".

### A4. §1.1's ⚠ is now CLOSED — the `$29C7,X` seed IS pinnable, and §6.1's fallback is REFUTED

The dossier says the `$29C7,X` index/stride "was NOT pinned here" and offers `battleFlashPal(c) << 1` as a
byte-safe fallback. Both halves are now settled, and the second one is **wrong**.

**The index (ROM).** `python tools/dis.py CEA50 0x50 1 0` shows the enclosing loop:

```
CC/EA57  STZ $80 / STZ $81            ; the loop counter i
CC/EA5B  LDX $80 / LDA $CCF939,X / TAX ; ** X = CC/F939[i] = the combatant's SETUP-BLOCK base **
CC/EA62  LDA $29C5,X / BPL ->EA6A     ; bit7 SET -> JMP CC/EC26 (slot absent)
CC/EA6A  CLC / LDA $80 / ADC #03 / TAY ; ** Y = i + 3 = the COMBATANT SLOT **
   …      (Y unchanged through CC/EAB2)
CC/EAAB  LDA $29C7,X / ASL A / STA $9888,Y / STA $993C,Y
```

`--read "rom:CC/F939+16"` = `00 0C 18 24 30 3C 48 54 …` ⇒ **8 enemy blocks, stride 0x0C, base `$29BF`,
slot = i + 3.**

**The confirmation (oracle).** `--monitor "29BF:64,9888:11" --diff` on `gato_press_action.state`, at the
fill frame f292, gives the five blocks

```
i=0 $29C5=FF  -> absent   i=1 FF absent   i=2 FF absent
i=3 $29C5=00  -> present, $29C7 = 01  ->  <<1 = 02  ->  $9888[3+3] = $9888[6] = 02   OK
i=4 $29C5=FF  -> absent
```

which is **exactly** the observed `0A 0C 0E FF FF FF 02 FF FF FF FF`. Disasm → RAM and RAM → disasm both
close. **The seed is derivable; there is no reason to fall back.**

**And the fallback is not byte-safe.** §6.1 claims `battleFlashPal(c) << 1` "reproduces the current 2-PC
fixtures byte-for-byte". `--read "9888+11,993C+11,2980+3"` on three states:

| state | `$2980` party | `$9888` |
|---|---|---|
| `bat3marle` | 00 01 – | `0A 0C 0E` `02 02 02` FF… |
| `bat9_lake` (3-PC, Frog seat 2) | 00 02 **04** | `0A 0C 0E` `02 02 02 02` FF FF |
| `bat8_lake` | 00 02 80 | `0A 0C 0E` **`04 04 04 04`** FF FF FF FF |

`battleFlashPal` (`src/battle.c:2761`) returns `idx==0?5 : idx==1?6 : 1`, i.e. `<<1` ⇒ `0A / 0C / 02…`.
Against the ROM law that is **wrong in two places that our own pins exercise**:
**(1) PC seat 2** is `0x0E` = **pal7** (seat-indexed — `0A + 2*seat`, identical in bat8/bat9 regardless of
who occupies the seat), not `0x02`; the 3-PC pins `field-battle-bat9-3pc-hud` / `-frog-bracket` / `-return`
run that party. **(2) bat8's enemies** are `0x04` = **pal2**, not pal1 — `battleFlashPal` hardcodes pal1 for
*every* enemy and only happens to be right for GATO and the Hetakes.
⇒ **Seed from `$29C7,X` via `CC/F939[i]`. The `battleFlashPal<<1` alternative is refuted, not merely
inferior, and this closes `battle_3pc_lucca_frog.md` G5 as §6.1 hoped.**
*(One caveat that survives: `objPal[c] == 0x08` as the "is flashing" test is the ROM's own byte, but an
enemy whose `$29C7` were `0x04` would seed `objPalDef = 0x08` and read as permanently flashing. No captured
fixture does this — bat8's `0x04` is already the post-`ASL` value, i.e. `$29C7 = 0x02` — but the renderer
should test a live `flashing` flag set by `69`, or assert `objPalDef != 0x08`.)*

### A5. The §6 fix — mandate-safe in SHAPE, but incomplete and wrong in five places

**What is fine.** No scene gate, no per-enemy constant, no eyeballed number. The `0x08` is `C1/5DFA`'s own
literal; the `t < 4` vs target-list split is the handler's own `LDA $A010 / CMP #04 / BCS`; and — verified
in the code — `len` comes from `battleAnimOpLenAt` and `isFree` from `battleAscOpFree`, **both ROM-scanned**
(`src/battle.c:4966,4973`), so adding `case 0x69/0x6A` changes **no timing at all** (neither touches `adv`
nor `isFree`). The fields the snippet uses all exist: `h->ascRole[t]` (filled at `:4880-4882`),
`h->actTgtN` / `h->actTgtList`. Confirmed `default: break;` is `src/battle.c:5202`, the flash arm is
`:5462`, and `grep "993C\|9888" src/*.c src/*.h` has **no battle hits** (the two matches are an unrelated
comment in `collision.c:464` and `main.c:7687`).

**Defect 1 — PC TECHS WOULD DOUBLE-FLASH.** §6.4 says to make the `:5462` arm "dead for `!isPc`". But
`battleAscTick` runs for `(!isPc && animParsed)` **OR** `(isPc && actIsTech)` (`:5276`, `:5294` — §6's own
scope paragraph). A PC **tech** therefore executes the script's `69`/`6A` *and* keeps the `:5462` pinned
flash at `CT_CRONO_STRIKE`. The guard must be **`!isPc || h->actIsTech`**, i.e. "whatever the interpreter
drives", not "`!isPc`". As written this is a new visible bug on Cyclone/Ice, which have their own pins.

**Defect 2 — the parse-fail fallback is silently dropped.** `:5462`'s `wantStrike` deliberately begins
`(!h->animParsed || …)` with the comment "Parse-fail (`animParsed==0`, e.g. no ROM loaded) falls back to the
pre-interpreter unconditional behaviour (never drops a strike)". The enemy interpreter arm is itself gated
on `h->animParsed` (`:5276`), so deleting the `:5462` flash for `!isPc` makes the victim flash **vanish
entirely** whenever the script fails to parse. The fallback has to be kept.

**Defect 3 — the MISS gate is unmodelled, and two dossiers disagree.** `:5462` suppresses the flash on
`h->actMiss`, from `battle_miss_evade.md` §3: *"At the miss frame GATO's body tiles are pal1, zero pal4 — no
flash"*. If `69` unconditionally sets `$993C = 08`, a miss would flash. The resolution is in that same
dossier (line 170): *"In D306 the flash/damage all live on the HIT branch `D32C+`; the miss branch `D31D`
skips them"* — i.e. the `69` sits inside a **conditional script branch**, and §6 models no branch op at
all. For the fix's stated scope (enemy attackers) our engine never rolls an enemy miss today, so this is
latent rather than live — but the `actMiss` suppression must be **retained**, and "does an ENEMY attack
script carry the same hit/miss branch" is **not derived anywhere**.

**Defect 4 — the render-site list is incomplete (4 of 9 missed).** §6.3 lists `field.c:3027-3030, 2870,
2902, 3591, 3817`. The flash is also read at **`field.c:2960`** (`flashed = (flashPal >= 0 && s->pal ==
flashPal)`, the generic stance loop), **`:3992`** and **`:4017`** (`battleEnemyFlashing(h, 3+slot)` in two
more enemy paths), and — the one that actually breaks — **`:4748` `if (h->fxFlashPal == 5) return;`**, the
gate that stops Crono's idle-bob layer from drawing over the pal4 swap. That site keys on the **literal 5**
(Crono's stance pal); if `fxFlashPal` goes dead for enemy actions, the bob layer will draw Crono *during*
his hurt flash. §6.3's "One source of truth" is not achieved by the five sites it lists.

**Defect 5 (the structural one) — half the impact event stays on `CT_GATO_STRIKE`.** §6.4 keeps "the
knockback / hurt-pose / crit-wash arms where they are — they are separately derived". They are separately
*measured*, but they are armed by the **same** `if (!h->actApplied && rel >= strikeRel)` block at the
**same** rel 27, from the same `CT_GATO_STRIKE` the dossier condemns. Verified live (A8): today the Hetake
attack gives flash+knock at rel 27 and the script's victim flinch at rel 76. After §6, it gives
**knock at rel 27, flash at rel 71, flinch at rel 76** — one impact rendered on two clocks. That is an
improvement on the user's complaint and a *new* internal split, and it is the shape the mandate calls a
spot-fix: the wrong quantity is `strikeRel`, and the flash is only its most visible consumer. **The
structural question §6 never asks: what script event marks the STRIKE?** (the `1E` super's position, or the
`50` ShowDamage, both already parsed) — that is the derivation this dossier should have carried.

**Verdict on §6:** the two opcode cases are correct and safe; the *deletions* around them are not, as
written.

### A6. Is there a simpler cause? — Reviewed, and NO. The dossier's cause is the right one.

The report is *"Crono, taking damage, flashes before the animation is finished"* — victim = a PC,
attacker = an enemy, symptom = the **white flash**, specifically its position relative to the attacker's
animation. Candidates, measured against our own engine (A8):

| candidate | why it is not this | rank |
|---|---|---|
| **the pinned `CT_GATO_STRIKE = 27` flash** | our flash fires at **rel 27**; the Hetake's approach SLIDE only *starts* at rel 35, its strike super at rel 71, the victim flinch at rel 76. So Crono goes white **8 f before the enemy begins moving toward him** and is done 40 f before it lands. That is the report, literally. | **1 — this is it** |
| the victim hurt-POSE window | already script-driven (`ascposearm f369 cmb1 anim=05`, rel 76) and therefore *correct*; it is what makes the early flash conspicuous, not a second defect | 2 (same defect, seen from the other side) |
| the pre-roll residual (OPEN 1, ~20-25 f slow) | shifts the *whole* action uniformly; a uniformly late action does not put the flash before the swing. Also our span 202 vs oracle 177 — visible as pacing, not as desync | 3 |
| op-`50` ShowDamage popup (`battle_crit_timeline.md`) | spawns **late** (`span_end − 58 − tail`, `:5559`); a user seeing that would say the *number* is late, not that the *flash* is early | 4 |
| HP-apply frame (`battle_hp_timing.md`) | `rel >= spanRel`, the span's last frame, by design ("the HP updates when the number disappears") | 5 |
| low-HP HUD blink (`field-battle-lowhp-blink-*`) | 12-frame period on the **HP/MP digits only**, runs whether or not damage is taken, never touches the sprite | 6 — excluded |

Nothing else in the engine can whiten a victim: `grep flashTarget/fxFlashPal` finds exactly **three** arm
sites (`:5462` strike, `:5513` flurry, `:5533` crit-flash-2) and for an enemy attacker only the first two
can fire. The identification is exhaustive.

### A7. Pinned checks that would move — §6's list is right as far as it goes, and misses the obvious one

All four names §6 cites **do exist** in `tools/regress_checks.json` (I doubted `field-battle-hetake-attack-
pose`; it is there, `CT_BATTLESHOT_ON=enemy-arrive+75`). Ranked by exposure:

1. **`field-battle-gato-flurry`** — `CT_BATTLESHOT_AT=386`, and its own note says *"f386 = rel 26 = the
   FIRST fist-out … pal4 white-swap rides the fist"*. §6.4 deletes the block that produces that swap
   (`:5508-5516`) and replaces it with the interpreter's own pulses at rel 22/36/50/… — a different phase.
   **This is the single most directly-targeted pin and §6 does not name it.**
2. **`field-battle-hetake-attack-pose`** (`enemy-arrive+75`) — sits ~4 f from where the ported `69` fires
   (rel 71, A8). Almost certainly flips from "not flashing" to "flashing". Named. OK
3. `field-battle-hetake-return-pose` (`enemy-arrive+110`) — past the `6A` on both models; **may** hold.
   Named. OK
4. `field-battle-lowhp-blink-white` / `-grey` (f240/f228 on `gato_t0`, `CT_BATTLE_SCRIPT=n:600`) — full
   composited frames that may fall inside an enemy action window. **Not named; check them.**
5. `field-battle-forest-lastkill-dissolve` (`death#3+18`) — a PC kill, post-span; low risk. Named. OK
6. `field-battlehud-gato` (phase 0 / f60) — cannot move. §6 is right.
7. **Cannot move** (verified, worth stating so the suite is not re-pinned blind): `field-battle-crit-flash1`
   / `-flash2` / `-crit-beatB-dead-suppressed` are PC-attacker paths and keep `:5462`;
   `field-battle-hetake-pose-ladder` and every `ascpose`/`battlesfx` trace check are **immune**, because the
   two new cases change neither `adv` nor `isFree`.
8. If Defect 1 is not fixed, the Cyclone/Ice tech pins (`-tech-cyclone-whirl-asma/-asmb-flip`,
   `-tech-ice-crystal-orbit/-shatter`) move too, via the double flash.

### A8. §8 OPEN 6 — RESOLVED: our walk *does* yield 7 pulses (but at a 14-frame period)

Re-ran §7(e) on the snapshot binaries.

*Forest* (`--scene 19 --battletest forest_pre.raw.wram`, `CT_BATTLE_SCRIPT=n:500`) reproduces §4 exactly:
`asc arm f314 rel=21` → `battletrace f320 strike` (**rel 27**) → `asc f328 rel=35 t0 SLIDE (120,63)->(127,132)`
→ `asc f364 rel=71 t3 SUPER 44 -> CE/DB10` → `ascposearm f369 cmb1 anim=05` → `asc f392 rel=99 t3 track-end`,
span 202. **44 frames early, confirmed on today's tree.**

*GATO* (`CT_BATTLE_MASHA=1`) — the flurry, run to completion this time:
`battlesfx` `78 46` at **rel 22, 36, 50, 64, 78, 92, 106 = SEVEN**, twice over (spans f352 and f787).
So OPEN 6's worry is unfounded: the interpreter walks all seven `CD/7B8F` blocks and the ported flash would
fire seven pulses, matching the oracle's seven (the pinned block's six is the thing that is short).
**Residual:** our period is **14 f**, the oracle's **16 f** (oracle rel 23/39/55/71/87/103/119 vs ours
22/36/50/64/78/92/106) — the `06 aa` cost gap of A3, i.e. a *pose-engine* residual, not a flash one.

### A9. What is left open after this pass

1. **What script event marks the STRIKE** (Defect 5) — until that is derived, §6 fixes the flash and leaves
   the knockback/hurt/popup arm on `CT_GATO_STRIKE = 27`.
2. **Does an ENEMY attack script carry a hit/miss branch** like `CE/D306`'s `D31D` (Defect 3)?
3. **The `06 aa` cost** (`C1/4EA2`): the oracle's `06 05` costs 2-4 f depending on state; ours is fixed
   enough that GATO-B's period comes out 14 vs 16. That gap now bounds the flash's accuracy.
4. **The PC-seat palette law.** `$9888[0..2] = 0A 0C 0E` is seat-indexed and identical across bat8/bat9/
   bat3marle, but the routine that writes it (the PC half of the `CC/EA54` boot fill) was not disassembled
   here — only the ENEMY loop (`Y = i + 3`) was pinned.
5. §8's own items 2 (`$99D2`/`$A08C`/the `$2200` CGRAM half), 3 (op `0x60`), 4 (the 1-frame publish lag on
   our side) and 5 (`CT_PC_ATK_FLASH = 19` re-measurement) were **not** touched by this review.
