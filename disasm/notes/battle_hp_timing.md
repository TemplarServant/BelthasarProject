# BATTLE HP-UPDATE TIMING — when curHP changes relative to the damage popup (the "HP takes long to update" fix)

Derives the EXACT timing of the stored-curHP write and the HUD-HP display update relative to the damage-number
popup, for the GATO fight. User report (translated from Portuguese): *"the character's hp is taking long to update after taking damage,
it should update right after the animation with the damage value disappears"* — the displayed HP should drop right as the
damage number disappears; ours updates much later.

Method (mandate: measured, not invented). Base `gato_press_action.state` (repo root; A-press boots battle ≈f222).
Oracle `ct_probe.exe` (a scratch copy of `build/ct.exe`). **The whole frame timeline is on ONE `--monitor` run**
(monitor does NOT render, so the peek/monitor-timeline gotcha, `battle_loop.md §method`, cannot desync popup-vs-HP)
by watching the **OAM shadow `$7E0700`** (the digit-popup sprites live there; proven `== .oam` byte-exact,
`battle_actionfx.md ADDENDUM A2`) **together with** curHP + the freeze flag `$A013` in the SAME regions list.
`--peek` renders and is used ONLY for the separate HUD-display-lag corroboration (§2), aligned by event.

Ledger mash = `('100:20,0:40')×24` (the `battle_loop.md §G/§H` clean cadence; `100:16,0:16` can wedge `$A013`).
Repro (the authoritative timeline run):

    ct_probe --monitor "0700:512,A013:1,6130:2,5E30:2,5EB0:2,95D5:1,9877:1" --from gato_press_action.state \
      --input <('100:20,0:40')×24> --frames 1250            (parser: scratch/hp_popup.py, hp_full.py)

Digit-popup detector: an OAM sprite is a damage digit iff tileLow ∈ {0x90..0x99} AND attr bit0=1 (tile 0x190-0x199,
`battle_actionfx.md P1.1`) AND on-screen (y<0xE0). Flash detector: target pose tiles in OBJ palette 4 (all-white,
`battle_actionfx.md P3`) — Crono tiles 0x08-0x0F / GATO body 0x68-0x8F.

---

## (a) FRAME-ANCHORED TIMELINE — one monitor timeline, all events on one clock

Every action span is a `$A013`=1 freeze (`battle_loop.md §E`); the ATB is byte-frozen across it; the span END is the
frame `$A013` clears. **The curHP write (`C1:ED1A`) fires on the SAME frame the freeze clears (`C1:41BD`)** — the
span's LAST frame — for EVERY action, both directions:

| freeze ON | strike/flash (op-1E) | popup FIRST (op-50) | popup LAST | **curHP WRITE = freeze OFF** | popLast→HPwrite | selector | direction / delta |
|---|---|---|---|---|---|---|---|
| f478 | f532 | f572 | f628 | **f635** | **7 f** | 0x31 GATO-A | **enemy→Crono** 70→61 (−9) |
| f643 | f686 | f707 | f763 | **f768** | **5 f** | 0x00 PC-melee | **Crono→GATO** 76→60 (−16) |
| f772 | f796 | f935 | f991 | **f998** | **7 f** | 0x34 GATO-B | enemy→Crono 61→47 (−14) |
| f1005 | f1071 | f1091 | f1147 | **f1151** | **4 f** | 0x0F PC-ranged | PC→GATO 60→26 (−34) |
| f1155 | f1178 | f1314 | f1370 | **f1377** | **7 f** | 0x34 GATO-B | enemy→Crono 47→32 (−15) |

Writer PCs (`--writer`, PC after the store retires): freeze set **`C1:40A9`** (`INC $A013` @C1/40A6), freeze clear
**`C1:41BD`** (`STZ $A013` @C1/41BA), curHP **`C1:ED1A`** (`STA $5E30,X` @C1/ED17). curHP addresses: Crono `$5E30`
(block `$5E2D`+3), GATO `$6130` (block `$612D`+3); `$6132`=maxHP (constant 76 — NOT curHP). All values little-endian.

**The three load-bearing facts (byte-exact, both directions, 5 actions):**

1. **Stored curHP writes at span-END (freeze-clear frame).** Not mid-span. `C1:ED1A` == the frame `$A013`→0, every time.
2. **The popup (op-50 ShowDamage) is born LATE — at `span_end − 60..63`** (enemy −63 / PC −60/−61), NOT at the strike.
   It is born **64-163 frames AFTER** the visual strike (op-1E flash): the strike is early (exec-track), the popup late
   (main-track). Popup lifetime = **57 present-frames** (e.g. f572..f628 inclusive; ≈ the baked 56, `battle_fx.h`).
3. **The popup's LAST visible frame precedes the curHP write by 4-7 frames** (enemy 7 / PC 4-5). Universal law:
   `span_end = popup_birth + 57 + (4..7)` — holds for all 5 actions → **the action-span END tracks the ShowDamage
   popup's completion**, and the HP write lands ~5-7 f after the number vanishes. **This is exactly the user's report.**

Loop-exit corroboration (`--monitor "9876:1,A013:1,5E30:2,6130:2" --writer`, scratch/w9876b.txt): the anim engine sets
`$9876` (writer **`C1:4D03`**) 2-5 f before span-end (GATO-A f630; PC f766), which drops the `C1/4090` action loop
out of its `LDA $9876 / BEQ` wait; the completion then applies curHP + clears `$A013` on the last frame (f635 / f768).

---

## (b) THE ROM MECHANISM — which op/routine writes curHP, and what triggers it

The damage pipeline (`battle_loop.md §S3` + `ADDENDUM §A`, re-confirmed by fresh disasm here):

- **COMPUTE** (early, at dispatch / the TICKING enqueue gap, `battle_loop.md §I`): final damage → `$AD89`
  (`C1/DCED` normal / `C1/EDB0` crit).
- **op-1E `PerformSuperCommand`** (anim exec-track, handler `C1/56CC`; `battle_interp_verify.md T2.B`): fires
  MID-animation = the visual strike / hurt-flash (`battle_actionfx.md A2.1 A4 / A3.1`). **Not** the HP write.
- **op-50 `ShowDamage`** (anim main-track, handler `C1/5ADB`; `battle_interp_verify.md T2.B`): fires LATE in the
  script (`span_end−~62`) = spawns the rising damage-number OBJ (tiles 0x190+d). **Not** the HP write. The action loop
  `C1/4090` then waits (via `$9876`) for that FX to finish its ~57-frame life.
- **APPLY + CLAMP + STORE** (at anim-COMPLETION / span-end — the actual curHP write). Fresh disasm `tools/dis.py 1EC90
  0xA0 0 0` (16-bit A/X):

  ```
  C1/EC99  LDA $5E30,X / SEC / SBC $B328,Y / STA $5E30,X     ; APPLY: curHP -= staged hit-1 damage $B328,Y
  C1/ECA3..ECC4                                              ;   (optional -= $B354/$B380 hit-2/3, gated BIT #4000)
  C1/ECC7  LDA $5E30,X / BMI ->death / CMP #0 / BEQ ->death  ; clamp/death test
  C1/ECD1  CMP $5E32,X / BCC ->ED17 / LDA $5E32,X            ;   below maxHP -> store; else clamp to maxHP
  C1/ECDB..ED09  (death) LDA $5E4A,X / ORA #80 / STA         ; dead flag block+0x1D bit7; enemy -> JSL FD/ABA2
  C1/ED0F  STA $5E30,X                                       ;   curHP = 0 on death
  C1/ED17  STA $5E30,X                                       ; ** THE curHP WRITE ** (monitor PC C1:ED1A)
  ```

- **COMPLETION / freeze clear** (same frame as the store): action-execution routine `C1/4090` — `INC $A013` @`C1/40A6`
  (set), the per-frame loop `C1/40B0→40D1` (`LDA $9876 / BEQ ->40B0`, `battle_loop.md §I`, verified), then on
  `$9876≠0` the completion runs the apply above and `STZ $A013` @`C1/41BA`.

**Trigger verdict: the curHP write is ANIM-COMPLETION-gated, NOT op-triggered.** op-50 (ShowDamage) only spawns the
popup FX (~62 f earlier); op-1E only flashes. The write happens when the action's anim script finishes (signaled by
`$9876`, `C1:4D03`), i.e. at span-end. The popup's disappearance and the HP write are causally linked because the span
END waits for the ShowDamage FX to complete (+ ~5-7 f), so `HP write = (op-50 fire) + popup-lifetime(57) + ~5`.

---

## (c) IMPLEMENTATION SPEC for our C engine (`src/battle.c`) — mandate-safe, minimal

**Diagnosis of our bug.** Our `battleAdvanceAction` (src/battle.c ~L1177) spawns the popup INSIDE the visual-strike
block, at `rel >= strikeRel` (Crono 29 / Marle 67 / GATO 27), and applies HP at `rel >= spanRel` (the span end,
125/146/157/224). So our popup dies at `strikeRel + CT_FX_LIFETIME(56)` and the HP write is FAR later:

| attacker | popup born rel | popup dies rel | HP write rel (span end) | **our current gap** | oracle gap |
|---|---|---|---|---|---|
| Crono melee | 29 | 85 | 125 | **40 f** | 5 |
| Marle ranged | 67 | 123 | 146 | 23 f | 4 |
| GATO-A | 27 | 83 | 157 | **74 f** | 7 |
| GATO-B | 27 | 83 | 224 | **141 f** | 7 |

The **HP-apply is already canonical** (span-end = `C1:ED1A`, §a fact 1) — do NOT move it earlier (that would put the
stored write mid-freeze, contradicting the oracle: HP writes on the freeze-CLEAR frame). What is mistimed is the
**POPUP spawn**: it must be born LATE so it dies ~6 f before the span-end HP write. Fix = decouple the popup from the
strike:

1. **KEEP** the HP apply at `rel >= spanRel` (`battleApplyDamage` at span end). UNCHANGED. (op-completion, §b.)
2. **KEEP** the hurt-flash + knockback at `rel >= strikeRel` (op-1E visual strike). UNCHANGED — this is early and correct.
3. **MOVE the popup spawn** out of the strike block into its OWN one-shot trigger (add `h->popupSpawned`, reset per
   action in `battleOpenAction`/`CtBattleBegin`), firing at:

        rel >= spanRel - CT_FX_POPUP_LEAD          // op-50 ShowDamage's real (late) script position
        with CT_FX_POPUP_LEAD = CT_FX_LIFETIME + 6 = 62

   still gated on `wantShowDmg` (`!h->animParsed || h->animHasShowDmg` — the op-50 gate is already parsed, T2.B).
   The popup then ages 0..55 over `rel = spanRel-62 .. spanRel-7`, last-visible at `spanRel-7`, and HP applies at
   `spanRel` → **gap 7 f for every action** (oracle: enemy 7 / PC 4-5; a single lead=62 matches enemy exactly and PC
   within 2-3 f). For byte-exactness, split `CT_FX_POPUP_LEAD` per side: **enemy 63 / PC 60** (→ enemy gap 7, PC gap 5).
4. (cosmetic, optional) bump `CT_FX_HOLD` 31→32 so `CT_FX_LIFETIME` 56→57 to match the measured 57 present-frames
   (`battle_fx.h`); 1-frame detail, not load-bearing.

Net: popup now rises during the LAST ~62 f of the action, vanishes, and ~6 f later the HP drops — the canonical
"HP updates right as the number disappears" (user request). Damage values, `h->actSpan`, HP-apply frame, turn order,
and outcome are all untouched.

**Expressed in the anim-script event terms our interpreter parses** (`battle_interp_verify.md` P2.B; jump table
`C1/7A6B`): `op-1E → visual strike (flash/knockback), early` [KEEP]; `op-50 ShowDamage → spawn popup, at span_end−62`
[MOVE HERE]; `anim-completion ($9876) → apply curHP, at span_end` [KEEP]. Equivalently: HP applies at
`(op-50 fire) + popup-lifetime + ~5` — but since that equals span-end and our apply is already there, only the
popup spawn moves.

---

## (d) PINNED REGRESSION SIGNATURES — do they change? NO.

The battle checks (`tools/regress_checks.json`) assert:
- `field-battleloop-gato` / `-forest` / `-wait`: `battleloop: victory f2171 pcAttacks=6 cronoHP=44 marleHP=65`
  (victory FRAME, PC-attack COUNT, final HP) — all functions of damage values + span lengths + apply-at-span-end +
  turn order, **none of which the popup-spawn move touches**. (`h->pcAttacks++` fires at span-end, unchanged.)
- `field-battletest-gato`: staging 18/18 + Gato HP 76 + points 0F (data-only boot; no action window).
- `field-battlehud-gato`: `battlehud: fnv=9764439D phase=0` — a HUD screenshot hash captured with `CT_BATTLE_DEBUGEND=60`
  = **debug-end at f60, BEFORE any action window / popup exists**. The popup timing cannot affect a phase-0/f60 frame.

**Verdict: no pinned signature changes.** Reason verified against the actual `expect`/`pin` strings, not assumed:
the only screenshot-hash check is pre-action (phase 0), and every value check is on damage/HP/frame/count quantities
that a pure popup-render-timing change leaves invariant. (If a NEW check ever screenshots mid-action-window, it WOULD
capture the moved popup — pin such a check only after this change lands.) Re-pin nothing.

---

## (e) OPEN ITEMS

- **DISPLAYED HUD-HP lag (§2) is a real, separate cadence — LEFT AS A BOUNDARY.** `--peek` corroboration (event-aligned,
  scratch/ph_*.png): the stored write is at span-end (peek f635 for Crono 70→61; ~f998 for 61→47), but the **HUD digit
  snaps to the new value ~10-13 f LATER** (HUD flip f645-646 / ~f1008-1012), a **clean SNAP, not a roll** (no intermediate
  values). Reproduces on 2 independent enemy→Crono hits. This lag applies ONLY to the PC HP shown in the HUD (enemy HP
  is not shown as a number — its only "display" is the popup, §a). Our engine's HUD reads the block curHP LIVE each frame
  (`field.c`), so ours updates AT the stored write (≈10 f EARLIER than canon's HUD). Matching canon's HUD-snap latency
  would need a ~10-13 f display delay on the PC-HP cell; the exact redraw routine/cadence (likely the battle-tick HUD
  redraw resuming after the `$A013` freeze lifts) is un-RE'd. NOT part of the primary fix (it makes the HUD LATER, the
  opposite of the user's ask) — documented, not implemented.
- **Strike-offset (op-1E) frame position** differs by scenario/timeline (our peek-derived 27/29 vs this ledger-monitor
  43/54 rel) — out of scope here (it is `battle_actionfx.md` A2/A3's byte-exact bake); this task only needs it as the
  "early" reference vs the "late" popup. Not touched.
- **Per-side popup lead** (enemy 63 / PC 60) vs a single 62: 2-3 f cosmetic; the exact ROM `$9876`-set trigger inside
  the bank-CC anim engine (what makes the ShowDamage FX signal completion) is the standing anim-engine boundary
  (`battle_interp_verify.md` P2.B / RESIDUAL BOUNDARIES).
- **Crit / tech / multi-enemy / >99-HP HUD**: only Crono-melee + Marle-ranged (PC→GATO) and GATO-A/B (enemy→Crono)
  captured; the span_end−62 popup law + span-end apply generalize (all are the same completion path) but per-attack
  spans for other actors are the `battle_loop.md §I` per-`$9877` table.

## Scratchpad artifacts (paths for the caller; not committed)
Dir: `%TEMP%/claude/C--Users-...-e5608df4-.../scratchpad/`
- `hp_orient.txt` (freeze/curHP/selector timeline), `hp_popup.py` + `hp_full.py` (OAM digit + flash parser, the §a table),
  `w9876b.txt` ($9876 loop-exit vs apply/A013-clear), `peek_hp.py` (peek stored-HP sweep), `ph_*.png` (HUD-lag frames).
- `ct_probe.exe` (oracle copy of build/ct.exe).
