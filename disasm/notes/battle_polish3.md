# BATTLE PRESENTATION POLISH III — four truths DERIVED (flurry flash = VICTIM / dead-PC ATB empties / Marle CLOSE-melee variant / counter facing)

Derives, from the ROM disasm + LakeSnes oracle **only**, four user-reported battle-presentation truths at HEAD
**a1004ea**. `src/` + git untouched — this notes file is the sole write. Method: oracle `ct_probe.exe`
(= `build/ct.exe`, copied to scratch) `--monitor` (per-frame RAM, aligned by `$9877`/`$A013` — the `--peek`≠
`--monitor` gotcha avoided), OAM shadow `$7E0700` decode for the palette-per-sprite proof, `tools/dis.py`
(fileoffset = `(bank−0xC0)<<16 | addr`) + raw-byte ROM reads for every table/routine, and a `ctlib.lake`
WRAM patcher (CRC-rewrapped states) to stage Marle at a chosen distance / disable crit. Our engine read from
`src/battle.c` + `src/field.c`.

## ⭐ HEADLINE

| # | user report | root truth (oracle) | our defect |
|---|---|---|---|
| **1** | flurry: "the one that flashes white is the TARGET, not GATO" | during attack-B (`$9877=0x34`) the **VICTIM** (the struck PC) flashes **pal4-white 6-7×**, once per punch/fist beat (rel 24/40/56/72/88/104/120, 16f period, ~7f on); **GATO's own cells are NEVER pal4** (0 frames) | `field.c:2504` draws **GATO** in pal4 on each fist beat; the victim flashes only ONCE (at the 1E strike) |
| **2** | dead PC: "his ATB bar should empty" | on death the ROM sets the dead PC's `$AFAB`(timer)=`$99DD`=`$9F22`(max)=`$B158`(reload) **→ timer==max → bar EMPTY**, and the countdown skips him (frozen empty) — routine **C1/B392** in the death path | `battleApplyDamage` PC-death sets only the dead flag; `$AFAB` keeps its pre-death value → a PC dying while ready (`$AFAB==0`) hits our `ready` guard → bar renders **FULL** |
| **3** | Marle: "very close to the enemy she does a melee attack" | Marle has a **distance-gated** attack: **dist ≤ 40px → MELEE `$9877=0x03`** (span 125f); **> 40px → RANGED `0x02`** (span 137f). Gate = `$9930` (C1/4A26 `JSR $2AE3`, dist²≤`0x0640`=1600=40²). SAME damage (both `$AEEB=02`, −18). The prior "Marle=0x0F" was her **CRIT** anim (CD/400E) | `battle.c:1454/1467` hardcodes Marle=`0x0F` **always** (no range branch; 0x0F is actually the crit-table value) |
| **4** | "GATO attacked Marle with his back turned, looking up" | GATO's attack **faces his target** — attack-A/attack-B on Marle → `$96DE[3]=3` (RIGHT), stamped by the CD attack script (writer **C1:600C**). In the ROM the counter fires **at rest**, never mid-return-facing-up | our REACTIVE counter can fire while the return mover is active; the facing stamp (`battle.c:1825`) is gated `!mvActive` → **skipped** → `mvFacing` stays UP(0) from the return walk → GATO renders attack-B facing UP |

Combatant / RAM key (this fight): GATO = enemy **slot 3**, facing var `$96DE[3]`; PCs = seats 0/1/2. Per-seat ATB
`$AFAB+seat` timer / `$9F22+seat` max / `$99DD+seat` tick-mirror / `$B158+seat` reload. Drives: **MASH**
`('100:20,0:40')×N` (keeps GATO in when-hit → attack-B), Marle **DRIVE** `0:120,100:12,0:26,100:12,0:PAD`.

---

# ITEM 1 — the flurry white-flash belongs to the VICTIM (DERIVED, OAM-proven)

## 1.1 CANONICAL — the struck PC flashes pal4, 6-7 pulses; GATO never flashes
`ct_probe --monitor "0700:512,9877:1,A013:1,6130:2" --from gato_press_action.state --input <MASH> --frames 900`
(full OAM-shadow decode: per sprite pal = `(attrByte3>>1)&7`; UPPER y<0x62 = GATO's 12 body cells tiles
`0x68–0x8E`, LOWER y≥0x62 = the struck PC's 4-cell sprite tiles `0x08/0x0A/0x0C/0x0E`). During attack-B
(`$9877=0x34`, dispatch f772):

```
pal4 pulses on the VICTIM (LOWER, tiles 08/0A/0C/0E), rel to f772:
  rel  24..30   40..46   56..62   72..78   88..94   104..110   120..125     (16f period, ~7f ON, ~9f OFF)
GATO body cells (0x68–0x8E) pal4 during the WHOLE attack-B: 0 frames
```

- The **victim flashes 6-7×**, one pulse per punch (the fist beat) — rel 24/40/56/72/88/104/120 exactly the
  0x16-fist-pose onsets (`battle_anim_polish2.md §2.1`: fist rel +26,+42,…, 16f apart). GATO stays pal1.
- **Reference (the palette proof):** when **Crono hits GATO** (f686-694, `$9877=0x00`), it is **GATO's UPPER
  cells** (`0x68–0x8E`) that go pal4 — the standard hurt-flash on the *victim of the swing*. So pal4 = the
  white hurt-flash, applied to *whoever is being struck*: Crono's swing flashes GATO; GATO's flurry flashes the
  struck PC.
- The single-strike (`1E`) hurt-flash and the 6× flurry pulses are the SAME channel — the first pulse (rel
  24-30) IS the strike beat; the flurry adds 5-6 more, one per fist.
- The `69 00 … 6A` "palette-flash" op in GATO's flurry track `CD/7B8F` (`battle_gato_moveset.md §B.2`) targets the
  **VICTIM's** OBJ palette, not GATO's — the corpus's "GS2 fist-out + white palette-flash" beat was mis-read as
  GATO flashing.

## 1.2 OURS + FIX SPEC
`battleGatoRender` flurry block (`field.c:2488-2506`) computes `flashOn = (p < DWELL)` (the fist half of each 16f
cycle — the CORRECT timing) but applies it to **GATO's own palette**: `ctAnimengGatoBlit(fb, idx, …, flashOn ?
ctBattleFxFlashPal : palw)` (`:2504`). So GATO flashes white 6× (wrong) and the VICTIM flashes only once (the
strike, `battle.c:1970` `flashLeft = CT_FX_FLASH_DUR=9`).

**FIX (two coupled changes):**
1. **`field.c:2504`** — blit GATO with `palw` **always** (drop the `flashOn ? ctBattleFxFlashPal` swap). GATO
   never flashes during his own flurry. (Keep `idx = flashOn ? fist : retract` — the POSE cadence is correct.)
2. **Pulse the VICTIM flash 6×** on the fist-beat schedule. In `battleAdvanceAction` (`battle.c`, the tick that
   already arms `flashTarget`/`fxFlashPal`/`flashLeft` at the strike, `:1970`), when `mem[$9877]==0x34` re-arm
   `h->flashTarget = h->actTarget; h->fxFlashPal = battleFlashPal(h->actTarget); h->flashLeft ≈ 7` on each fist
   beat — i.e. while `flashOn` (rel−WINDUP)%16 < DWELL over the flurry window (rel WINDUP..WINDUP+96). The render
   already swaps the matched victim to `ctBattleFxFlashPal` (pal4 white) via `battleStanceRender`
   (`field.c:1978`), so this makes the struck PC flash pal4 6× — byte-faithful to §1.1. Damage staging
   UNCHANGED (single hit + single popup; the flurry flash is cosmetic).

---

# ITEM 2 — a dead PC's ATB bar EMPTIES (timer forced to max) (DERIVED)

## 2.1 CANONICAL — the death routine sets timer = mirror = max = reload → bar EMPTY
LIVE death (mandate: a real transition, not a synthetic pre-dead state): `gato_crono_lowhp.state` (Crono seat0,
curHP 0x0A), MASH → GATO's counter kills him. `ct_probe --monitor "AFAB:3,9F22:3,99DD:3,5E30:2,5E4A:1,6130:2"
--diff --writer`:

```
f194  Crono $AFAB[0]=48 (reload C1:BCEB)  $9F22[0]=48   ; charging; frozen at 48 during GATO's action ($A013=1)
f424  Crono $5E30 -> 0000 (C1:ED12)  $5E4A -> 80 (dead flag, C1:B2B9)   ; GATO's counter kills him
      $AFAB[0]=48 (C1:B39E)  $9F22[0]=48 (C1:B398)  $99DD[0]=48 (C1:B39B)   ; HELD at max, FROZEN thereafter
```

The synthetic `gato_crono_dead.state` agrees: Crono's `$AFAB[0]` sits at `0x48` the whole run (it was **Marle's**
`$AFAB[1]` counting `0x15→0x00`, not Crono's — the initial mis-read).

**The death routine** `C1/B2B1` (`dis.py 1B2AC`; dead flag `LDA #80 / STA $5E4A,X` = `C1:B2B9`), for a **PC**
(`CPY #0003 / BCC` → the seat<3 path), reaches **C1/B392** (raw-decoded `99 22 9F / 99 DD 99 / 99 AB AF`):
```
C1/B392  LDA $B158,Y        ; the seat's ATB RELOAD value (= its bar max)
C1/B395  STA $9F22,Y        ; max   = $B158   (writer C1:B398)
C1/B398  STA $99DD,Y        ; mirror= $B158   (writer C1:B39B)
C1/B39B  STA $AFAB,Y        ; timer = $B158   (writer C1:B39E)
```
⇒ **timer == max**. The bar builder (`battle_hud_render.md ADD2 §A`) fill = `32×(max−timer)/max` = **0 → EMPTY**.
And the per-frame countdown skips the dead PC (its alive-gate), so it **stays** empty (frozen, not filling).

## 2.2 OURS + FIX SPEC
Our bar formula (`field.c:1751-1755`) is byte-correct: `atbT=$AFAB[k], atbM=$9F22[k]; ready=(atbT==0||atbM==0);
fill = ready?32 : 32−((atbT*256/atbM)>>3)`. For a dead PC with `$AFAB==$9F22` (both the max), `fill=0` (empty) —
**iff the sim holds `$AFAB==$9F22`**. But our `battleApplyDamage` PC-death branch (`battle.c:913-956`) sets ONLY
the dead flag; it **never touches `$AFAB`/`$9F22`**. Our countdown (`battle.c:2373`) then freezes the dead PC's
`$AFAB` at whatever it held at death:
- died while **ready** (`$AFAB==0`, in the queue) → our `ready` guard → **fill=32 → FULL bar** (the user defect);
- died mid-countdown → **partial** bar. Neither empties.

**FIX:** mirror `C1/B392` in the PC-death branch. In `battleApplyDamage` `if (curHP<=0)` (`battle.c:913`), add a
PC case (`battleIsPc(tgt)`, tgt = seat 0-2):
```c
s[0xAFAB + tgt] = s[0x9F22 + tgt] = s[0xB158 + tgt];   // timer = mirror-max = reload  -> bar EMPTY (C1/B392)
```
Because our countdown already skips dead PCs (`battle.c:2373` alive-gate), the bar renders empty and stays empty —
byte-faithful to §2.1. (Enemies have no bar — the ROM's `CPY #0003` gate; our `battleIsPc(tgt)` matches.)

---

# ITEM 3 — Marle's CLOSE-RANGE MELEE variant (`$9877=0x03`) — a 40-px distance gate (DERIVED + disasm)

## 3.1 CANONICAL — Marle's anim is distance-gated (oracle, crit-off, patched distance)
`ctlib.lake`-patched `marle_current.state` (Marle crit rate `$5EF2→0`; her live pos `$1D0D/$1D24` staged at a
chosen distance from GATO), DRIVE → she attacks. `--monitor "9877:1,9930:1,AE92:1,A013:1,AEEB:1,AE93:1,9907:3,
6130:2,1D0D:1,1D24:1,1D12:1,1D29:1"`:

| case | dist to GATO | `$9930` | **`$9877`** | span (`$A013`) | `$AEEB` | dmg (`$6130`) | mover |
|---|---|---|---|---|---|---|---|
| **FAR** (natural staging) | ~40+px | **0** | **0x02** (CD/6E83) | **137f** | 02 | −18 | planted |
| **CLOSE** (staged adjacent) | ≤40px | **1** | **0x03** (CD/6E97) | **125f** | 02 | −18 | planted |
| CRIT (`$AE9B`=80, any range) | — | — | 0x0F (CD/7164) | 146f | 02 | ×2 | planted |

- **`$9877=0x03` is a DISTINCT melee variant** — different action script `CD/5FF0[0x03]=CD/6E97` (vs ranged
  `[0x02]=CD/6E83`), its own 125f span, its own pose ladder. **Marle-melee assembly ladder** (`$9908`, cmb1):
  `0x24 → 0x35 → 0x94 → 0x95 → 0x75 → 0xD9`(idle-return).
- **SAME damage** — both variants carry `$AEEB=0x02` (crossbow formula type 2) + record `$AE93=0x01` → both
  dealt **−18** (`$6130` 42→24). The melee is **animation-only**; no damage/record change.
- **Both PLANTED** — Marle's `$1D0D` is constant through both attacks (no approach mover; `battle_pc_approach
  _facing.md §1.8` "Marle ranged, no mover" holds for the melee too — she only melees when already adjacent).
- **The prior "Marle normal = 0x0F" is her CRIT anim.** `0x0F = CD/400E[1]` (the crit table). Every earlier
  0x0F capture was a crit (my first far capture: `$AE9B=0x80`). Crit-off, Marle's normal is **0x02** (far) /
  **0x03** (close). `battle.c:1454/1467` hardcoding `0x0F` is therefore her crit-anim mislabeled as normal.

## 3.2 THE MECHANISM (disasm) — `$9930` melee flag, C1/2AE3 dist²≤40², CC/F63C weapon flag
PC-physical anim-select dispatches on `$AE92` via `($7A63,X)`; `$AE92=1` → **C1/432C → C1/49FF setup → C1/4340
selector** (Marle's attack is `$AE92=1`, measured). The setup **C1/49FF** (`dis.py 149FF`):
```
C1/49FF  LDA #01 / STA $9930                 ; $9930 = 1  (default = MELEE)
C1/4A04  $D3/$D4 = attacker live X/Y ($1D0C,X/$1D23,X);  $D5/$D6 = target live X/Y
C1/4A26  JSR C1/2AE3                          ; range test (raw C1/4A24 = 20 E3 2A = JSR $2AE3)
C1/4A29  LDA $9872 / BPL +2 / STZ $9930       ; out of range ($9872<0) -> $9930 = 0
C1/4A40  LDA $2980,Y / TAX / LDA $CCF63C,X     ; per-char WEAPON flag CC/F63C[charId]
C1/4A48  BNE ->4A4F (melee weapon: refine via C1/4A71) ; else (ranged: Marle/Lucca) keep the range result
```
- **C1/2AE3** sets the threshold `$9893=0x0640` (= **1600 = 40²**) then falls into the shared abs-Δ distance
  compare **C1/2B49** (`dx=|A039,X−A039,Y|`, `dy=|A050,X−A050,Y|`, dist²=dx²+dy²; the same engine as the
  counter's `C1/2B41`, `battle_counter_gating.md §1.C`): dist²≤1600 → `$9872=0` (in range → `$9930` stays 1);
  dist²>1600 → `$9872=0xFF` (`$9930`=0).
- **CC/F63C** (raw read) = `01 00 00 01 01 01 01` (charId 0-6 = Crono/Marle/Lucca/Robo/Frog/Ayla/Magus):
  **Marle=0 and Lucca=0 (RANGED weapons), all others=1 (MELEE).** For a ranged weapon `$9930` keeps the pure
  dist test; a melee weapon additionally runs `C1/4A71`.
- **The selector C1/4340** (`dis.py 143C0`): `$AE9B` crit? → `CD/400E[cid]`; else `LDA $9930 / BNE` → `$9930≠0`
  (close) → **`CD/4007[cid]`** (Marle 0x03); `$9930==0` (far) → **`CD/4000[cid]`** (Marle 0x02). Tables (raw):
  `CD/4007 = 00 03 05 06 08 0A 0C`, `CD/4000 = 0E 02 04 11 0E 0E 12`, `CD/400E = 01 0F 10 07 09 0B 0D`.

**Threshold pinned empirically** (patched Marle X sweep, reading BOTH live positions at dispatch):
```
Marle(178,113) GATO(139,117): dist²=39²+4²=1537 ≤1600 -> $9930=1 -> 0x03 (melee)
Marle(198,113) GATO(155,107): dist²=43²+6²=1885 >1600 -> $9930=0 -> 0x02 (ranged)
```
Exactly the `0x0640`=40² boundary.

## 3.3 CRONO's mirror (the "reverse variant" the mission asks about)
Crono is a MELEE weapon (`CC/F63C[0]=1`) so he runs `C1/4A71` AND has an approach mover: `CD/4007[0]=0x00`
(close/adjacent, his measured normal), `CD/4000[0]=0x0E` (the FAR/`$9930=0` variant), `CD/400E[0]=0x01` (crit).
In the GATO fight Crono is within 40px (dist²≈985) → `$9930=1` → **0x00**. So Crono's `0x0E` is his distance-gated
FAR variant (symmetric to Marle's 0x02/0x03 split), latent in normal play because he approaches to melee range;
**capture-pending** (not staged this session — the mechanism is disasm-derived, the value table-read).

## 3.4 OURS + FIX SPEC
`battleActionSpan` (`battle.c:1441-1468`) has **no range branch** — `int normalSel = (cid==1) ? 0x0F : rom[CD4007
+cid]` (`:1454`) + `else if (cid==1) { s[$9877]=0x0F; span=146; }` (`:1467`). Marle is always ranged-0x0F (which
is actually her crit anim), Crono always 0x00.

**FIX (implement the `$9930` gate for the PC anim selector — general, not a Marle special-case):**
1. Compute the melee flag `melee = dist²(attacker foot, target foot) ≤ 0x0640` (= 40²) from the live foot
   positions `h->mvLiveX/Y` (the same source the counter gate already uses, `battle_counter_gating.md §5 C1`).
2. Non-crit selector: `melee → s[$9877] = rom[CD4007 + cid]` (Marle **0x03** span **125**, Crono 0x00 span 125);
   `!melee → s[$9877] = rom[CD4000 + cid]` (Marle **0x02** span **137**, Crono 0x0E span TBD). Crit keeps
   `rom[CD400E + cid]` (unchanged).
3. Drop the `(cid==1)?0x0F` hardcode — `0x0F` is Marle's CRIT value only (already read via `ROM_CRIT_ANIM_TBL`
   at `:1461`). Damage is UNCHANGED (both variants `$AEEB=02`, same record/formula) — this is anim/span ONLY.
4. Marle stays PLANTED for both (no approach mover to add). Bake/route the `0x03` melee pose ladder
   (`0x24/0x35/0x94/0x95/0x75`) through the PC pose engine (or a keyframe bake) like her ranged pose.

⚠ Re-pin any `field-battle*marle*` signatures (her selector/span change 0x0F/146 → 0x02/137 far, 0x03/125 near).

---

# ITEM 4 — GATO's counter FACES its victim (never attacks facing UP) (DERIVED)

## 4.1 CANONICAL — the attack script re-faces GATO toward his target
`gato_crono_dead.state` (Crono dead → GATO's counter/attacks target MARLE, who sits to his RIGHT), MASH.
`--monitor "96DE:8,9877:1,A013:1,1D0C:8,1D23:8" --diff --writer`:

```
f66   $96DE[3]=1 (front)  $9877=0x31 (attack-A on Marle) dispatched
f90   $96DE[3] 1->3 (RIGHT, toward Marle)  writer C1:600C          ; the CD attack SCRIPT re-faces GATO
f374  $9877=0x34 (attack-B COUNTER on Marle)  $A013=1  $96DE[3]=3  ; counter attacks FACING RIGHT (his victim)
```
- GATO **faces his target** for the attack — RIGHT(3) toward Marle for both attack-A and the attack-B counter.
  The facing is stamped by the CD attack script itself (writer **C1:600C**, in the play-anim executor region,
  `battle_animeng_script.md §4`), independent of any walk/mover. **GATO never attacks facing away-from / up
  relative to his victim.**
- In the ROM the counter fires **at rest** — every qualifying hit lands while GATO is frozen during the PC's
  action (`battle_counter_gating.md §4`), so a mid-return-facing-up counter never occurs; and even so, the
  attack script would re-face him (above). The Y=85 mover-settle-before-action law (`battle_anim_polish2.md
  ITEM 3`, the song's dispatch gate) is the same shape: an enemy action does not fire mid-move.

## 4.2 OURS + FIX SPEC
Our reactive counter (`battle_counter_gating.md FIX C`) is a real-frame countdown
(`battleReactiveCounterTick`, `battle.c:1900`) that dispatches `battleOpenAction(h, cmb, reactive=1)`. The
target-facing stamp inside `battleOpenAction` (`battle.c:1825-1827`) is **gated on `!h->mvActive[atk-3]`**:
```c
if (!battleIsPc(atk) && !isTech && tgt>=0 && tgt<11 && !h->mvActive[atk-3]) {   // <-- skipped mid-move
    int b = battleAtan2(mvLiveX[atk],mvLiveY[atk], mvLiveX[tgt],mvLiveY[tgt]);
    h->mvFacing[atk-3] = ctBattleFaceTab[b & 0xFF];
}
```
If GATO is hit while his RETURN mover is active (mid-rise from a prior attack), the counter arms and ~4f later
fires with `mvActive[3]` still true → the stamp is **skipped** → `mvFacing[3]` retains the return-walk value
**UP(0)** → `battleGatoRender`'s attack-B flurry branch (`field.c:2490`) handles only `fac==1|3`, so `fac==0`
falls through to the directional rear→strike (`field.c:2521`) drawn **facing UP**. = "GATO attacked with his back turned,
looking up".

**FIX (mechanism, mirroring `battle_anim_polish2.md ITEM 3`'s song mover-idle gate):**
- **Gate the reactive-counter dispatch on the mover being IDLE.** In `battleReactiveCounterTick` (`battle.c:1902`),
  when the countdown expires but `h->mvActive[slot]` is still true, **hold** (do not dispatch; keep/re-arm the
  countdown) until the return mover settles — the ROM's "action fires at rest" law. Once idle, dispatch: the
  existing stamp at `battle.c:1825` runs (`mvActive` now false) and faces GATO toward the counter victim
  (the nearest PC — RIGHT for Marle), exactly §4.1.
- **Belt-and-suspenders:** for the reactive path, stamp `mvFacing[atk-3] = ctBattleFaceTab[atan2(self→victim)]`
  **unconditionally** (drop the `!mvActive` guard for the attack-facing case) — the attack script re-faces GATO
  regardless of mover state (C1:600C). Either change removes the facing-up counter; the mover-idle gate is the
  more faithful (it also fixes the wrong DISPATCH HEIGHT, the song-gate's concern).

---

# OPEN / boundaries
- **Item 1** — the exact per-punch flash duration (~7f measured) vs the fist-pose dwell (8f) is ±1-2f
  peek/monitor skew; the 16f period + fist-beat sync are authoritative. Marle-victim flurry (right-target,
  `gato_crono_dead`) not separately OAM-captured — the flash rides *whichever* PC is struck (same channel).
- **Item 2** — `C1/B392` sets timer=`$B158`(reload). Whether the death path or a shared reload owns it is moot:
  `--writer` proves `$AFAB=$9F22=$99DD=$B158` fire together on the death frame → timer==max → empty. Enemy-death
  path (`CPY #0003 →` `C1/B3A0`) not decoded (enemies have no bar).
- **Item 3** — Crono's `0x0E` FAR variant is table-read + disasm-derived, NOT captured (he approaches, so it is
  latent). The `C1/4A71` melee-weapon refinement (why a melee weapon's `$9930` can differ from the pure 40²
  test) is disasm-cited, not fully decoded. Whether `0x02`(CD/6E83) and `0x0F`(CD/7164) render visibly different
  (both crossbow) is the CD anim-engine boundary — the SELECTOR values are byte-proven regardless.
- **Item 4** — the CD-op at `C1:600C` that stamps `$96DE` is cited by writer PC, not byte-decoded (the CD-script
  facing op). The observable (GATO faces his target for every attack; counter fires at rest) is fully pinned.

# Scratchpad artifacts (paths for the caller; NOT committed)
Dir `%TEMP%/claude/C--Users-…-9e84b785-…/scratchpad/`: oracle `ct_probe.exe` (+SDL2.dll, ROM), states
`gato_press_action`, `gato_crono_lowhp` (item 2 live death), `gato_crono_dead` (items 1-victim/4), `marle_current`
+ derived `marle_far_nocrit`/`marle_close_nocrit`/`msw.state` (item 3, `ctlib.lake`-patched crit-off / staged
distance). `oam_flurry.txt` (item 1 full-OAM capture; inline python pal4-per-region decoder).
Repro (from the scratch dir, ROM in CWD):
```
# item1 (OAM pal4 = victim, not GATO):
ct_probe --monitor "0700:512,9877:1,A013:1,6130:2" --from gato_press_action.state --input <MASH> --frames 900
# item2 (dead-PC ATB -> timer==max==empty):
ct_probe --monitor "AFAB:3,9F22:3,99DD:3,5E30:2,5E4A:1,6130:2" --from gato_crono_lowhp.state --input <MASH> --diff --writer
python tools/dis.py 1B2AC 0x40 0 0     # death routine -> C1/B392 (LDA $B158,Y / STA $9F22,$99DD,$AFAB)
# item3 (Marle melee gate):
python3 patch (ctlib.lake): $5EF2=0 (crit off); $1D0D/$1D24 = staged distance to GATO
ct_probe --monitor "9877:1,9930:1,AE92:1,A013:1,AEEB:1,9907:3,1D0D:1,1D24:1,1D12:1,1D29:1" --from marle_close_nocrit.state --input "0:120,100:12,0:26,100:12,0:260" --diff
python tools/dis.py 149FF 0x40 0 0 ; python tools/dis.py 143C0 0x80 0 0   # setup ($9930) + selector
# CC/F63C = 01 00 00 01 01 01 01 ; CD/4007/4000/400E per-charId anim tables ; C1/2AE3 thr $9893=0x0640=40^2
# item4 (counter faces target):
ct_probe --monitor "96DE:8,9877:1,A013:1,1D0C:8,1D23:8" --from gato_crono_dead.state --input <MASH> --diff --writer   # $96DE[3]=3 via C1:600C
```
