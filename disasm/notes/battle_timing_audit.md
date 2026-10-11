# BATTLE TIMING AUDIT — (1) PC ATB charge wall-clock and (2) the Crono crit screen-flash sync

Two user reports, audited oracle-vs-ours frame-exact (mandate: measure both sides, derive from the ROM/oracle
only). Method = LakeSnes oracle (`ct.exe`→`ct_probe.exe`) `--monitor` (RAM timelines) + `--peek` (rendered
framebuffers, for the flash) on `gato_press_action.state` / a forced-crit in-battle state; our engine =
`ct_field.exe --battletest` with `CT_BATTLE_TRACE` / `CT_BATTLESHOT_AT`. Writer PCs = the PC after the store.
⚠ `--peek` ≠ `--monitor` timelines — every RELATIVE measurement below is taken **within one run's own timeline**,
anchored to the action-dispatch `$A013` 0→1 (which both capture). fb = 512×480 `[pad,B,G,R]`, mean brightness
= Σ(R+G+B)/3px.

---

## (1) ATB CHARGE — VERDICT: **BYTE-EQUAL** (cadence, reload value, freeze, and bar-render all match)

User: *"I have the impression that the ATB bar is charging too fast."* **Audited: our ATB charges at the
SAME wall-clock rate as the oracle.** The impression is the canonical **Active-mode reality** — the PC bars keep
ticking during GATO's approach WALK (`$A013`=0), which the oracle does identically; it only became *visible*
when enemy movement landed (b43f204). If anything ours is marginally SLOWER per cycle (the documented
sequential-walk residual, `battle_enemy_move_target.md` Wave-3 ADDENDUM), never faster.

### 1.A The four sub-mechanisms — measured both sides

| mechanism | ORACLE (measured) | OURS (measured) | match |
|---|---|---|---|
| **logic-tick cadence** | clean no-input drain **2.53 f/tick** (Marle 30 ticks f378→454 = 76f; gaps `4,2,3,2,3…`; GATO 102 ticks f378→635 = 257f = 2.52) | **2.50 f/tick** (Crono 72→44 f293→363 = 28t/70f; 42→0 f514→619 = 42t/105f) — the `tickAcc+=2 / ≥5 / -=5`, seed −2 → leading gap 4 then `2,3,2,3` | ✅ (NOT 1:1) |
| **reload value** `$AFAB←$B158` | Crono **0x48=72**, Marle **0x66=102**, GATO 0x66=102 (writer C1:BCEB) | Crono 72, Marle 102, GATO 102 (trace `act-end AFAB(c72,m102)`) | ✅ |
| **action freeze** `$A013`=1 | ATB byte-frozen for the whole action span (ledger §G, all `$AFAB` slots held); enemy **walk NOT frozen** (`$A013`=0, ATB ticks through the approach, `enemy_move_target.md` §A.6) | tick gate returns before the decrement when `$A013`=1 (`battle.c:1948`); `battleMoveTick` gated `$A013==0` (`battle.c:1431`) — walk ticks, action freezes | ✅ |
| **bar-render inputs** | `$9F22` (bar max) updated to the reload value at reload: Crono `$9F22←0x48` f768, Marle `←0x66` f1151 (writer **C1:BDB2**, the C1:BCEB twin) | `s[0x9F22+atk]=s[0xB158+atk]` at reload (`battle.c:1687`) = 72 / 102 | ✅ |

### 1.B Cadence — the 2/3-frame alternation is intact (the recent commit/animate split did NOT change it)

The turn-latch (`pcInTurn`) and commit/animate split operate *inside* the tick (step E enqueue, the pc-act
dispatch); they never touch the `tickAcc` accumulator (`battle.c:1955-1957`, which sits *after* the `$A013`
freeze + victory gates). Verified empirically both sides show the identical clean `[4,]2,3,2,3` gap pattern
(oracle `oracle_idle.txt` Marle f378,382,384,387,389,392…; ours the 2.50 f/tick drains above). **Our tick is
the 2.5-frame alternation, not 1:1.**

Oracle full-recharge wall-clock: Crono reload 72 @f768 → next reload @f1769 (gap **1001f**); the *pure ticking*
portion is 72 ticks × 2.53 ≈ **182 frames ≈ 3.0 s @60fps**, the rest is `$A013` action-freeze between (mode-
independent, §F/§G). Ours: 72 × 2.50 ≈ **180 frames** of ticking + the same per-span freezes. Structurally equal.
(A mash-input run shows local 2.0-f/tick stretches — a headless-mash artifact, `battle_loop.md` §H; the
canonical clock is the no-input 2.53.)

### 1.C Bar render — the fill formula is a faithful port with matching inputs → identical visual fill

`field.c:1731-1735`: `fill = 32 − (((atbT*256)/atbM)>>3)`, `atbT=$AFAB+seat`, `atbM=$9F22+seat`, `ready→32`.
This is the ported C1/0700 builder (`battle_hud_render.md` §A). Since `atbT` drains at the matched 2.5-f/tick
and `atbM=72/102` matches the oracle (1.A), the bar fills at the same visual rate. Fill is linear over the
charge (identical on both, by construction):

| timer | 72 | 63 | 54 | 45 | 36 | 27 | 18 | 9 | 0 |
|---|---|---|---|---|---|---|---|---|---|
| fill /32 | 0 | 4 | 8 | 12 | 16 | 20 | 24 | 28 | 32 |
| cells (fill>>3) | 0 | 0 | 1 | 1 | 2 | 2 | 3 | 3 | 4 |

The whole HUD strip is already pinned byte-equal to the oracle (`field-battlehud-gato`, 12288/12288). **Only
residual:** the *initial-stagger* denominator — oracle Crono `$9F22`=0x01 / Marle `$1F`=31 at combat-start vs
ours Marle=30 (ours seeds the f378 combat-start value, oracle keeps the f290 load) — a ≤1-unit, sub-pixel
difference on the **first** pre-combat charge only; every post-attack recharge uses the matched 72/102.

### 1.D Why the user perceives "too fast" (canonical, not a bug)

GATO now WALKS to his victim before striking (b43f204). In BOTH the oracle and ours that approach runs at
`$A013`=0, so **the PC ATB bars keep filling during the walk** — proven canonical (`enemy_move_target.md`
§A.1/§A.6: "the whole walk runs outside the `$A013` action span, the ATB keeps ticking through it"; my
`oracle_idle.txt` shows Marle's bar ticking cleanly f378→454 straight *through* GATO's approach f380→478).
Before movement landed GATO stood still and the bars only advanced between his stationary attacks; now the
player watches them climb during the visible walk-in — the same rate, newly visible. **No cadence/reload/render
divergence exists to fix.**

---

## (2) CRIT SCREEN FLASH — VERDICT: **DIVERGENCE PINPOINTED** — we render only the SECOND of two flashes

User: *"when Crono lands a critical, the white effect on the screen is not synchronised, it is happening later."*
**Confirmed and root-caused.** Crono's crit is a **double-slash** whose animation emits **TWO** whole-screen
white flashes; our engine fires **one** flash keyed to the SECOND slash, so it lands ~37-41 frames after the
first (primary) strike impact — exactly "it is happening later."

### 2.A Setup (forced deterministic crit)

In-battle Crono-ready state (`inbattle_ready.state` via `--peek gato_press_action --btnscript "100:8,0:380"`
+ `PEEK_SAVE`), then patch **Crono crit rate `$5E72`=0xFF, Marle `$5EF2`=0x00** (ctlib `WRAM_OFF=69`,
`state[69+addr]`) → `crit_ready.state`. Every Crono hit crits (×2 dmg, `$AE9B`=80). Monitor confirmed
(`--monitor "A013:1,9877:1,AE9B:1,AD89:2,6130:2"`): crit computed **f42** (`$AD89`=0x22=34=17×2, `$AE9B`=80),
**anim dispatch `$A013`0→1 @f45, `$9877`→0x01** (Crono crit anim, writer C1:4392), span-end **f222** = **177f**,
GATO curHP 76→42 (−34) at span-end.

### 2.B The ORACLE flash timeline — TWO flashes (peek sweep, all rel to dispatch `$A013`0→1 = f47 in this run)

fb-mean brightness swept frame-by-frame across the crit anim (`crit_ready.state`, drive
`"100:8,0:24,100:8,0:PAD"`, capture = 40+PAD; each capture's `.wram` gives `$A013`/`$9877`/`$AE9B`):

| flash | onset | peak fb-mean (base ≈ 80) | peak lerp→white | linear fade | what it is (rendered) |
|---|---|---|---|---|---|
| **Flash A** | **rel +50** (f97) | 126.6 | **26 %** | ~7 f (rel+50→+57) | **first slash IMPACT** — GATO whited-out by a white burst, whole screen washes white (`crit_f97.png`) |
| **Flash B** | **rel +91** (f138) | 125.8 | **25 %** | ~15 f (rel+91→+106) | **second slash** — Crono has leapt beside GATO (`crit_f110.png`→`crit_f138.png`), screen washes white again |

Both are sharp single-frame onsets. No third flash across the full f47→222 span. Both brighten the BG *and* the
sprite region uniformly (Flash A: BGtop 73→149, spriteCenter 86→159) — the whole-screen wash the user calls the
"white flash." Crono's `0x01` crit is a jump-in double-slash; each slash flashes.

### 2.C OURS — a single flash at rel +87 (= Flash B only), Flash A absent

`battle.c:1638-1666`: `strikeRel = h->actCrit ? CT_CRONO_CRIT_STRIKE(87) : CT_CRONO_STRIKE(29)`; at `rel≥strikeRel`
it sets `critFlashLeft = CT_CRIT_FLASH_DUR(15)`; `field.c:2022` lerps the framebuffer `CT_CRIT_FLASH_PEAK(28)%`
toward white, linear fade over 15f. Forced-crit trace: dispatch(pc-act, `$A013`→1) **f185**, our strike/flash
**f272 = rel+87**, span-end f362. `CT_BATTLESHOT` brightness over black:

| our frame | rel | mean | |
|---|---|---|---|
| f235 | **+50** (canonical Flash A) | 4.2 | **NO flash — Flash A is missing** |
| f272 | **+87** (our flash) | 77.1 | flash fires here |
| f280 | +95 | 40.8 | fading |
| f285 | +100 | 13.1 | ~gone (15f) |

Our single flash onset (rel+87) ≈ the canonical **Flash B** (rel+91), off by 4f. We **never render Flash A**
(rel+50) — the first and *primary* strike impact. So relative to the first visible slash, our flash is
**+37 to +41 frames late** → the user's "it is happening later."

### 2.D Root cause

`CT_CRONO_CRIT_STRIKE=87` was derived in `battle_crit.md` §C.2/ADDENDUM from a sweep that **sampled only from
f137 onward** (its own curve table literally starts "≤136 : 80"), so it saw only the second flash and pinned
~87. The first flash at f97 (rel+50) was outside the sampled window and was missed. Our engine faithfully
implements that (incomplete) pin → one flash, on the wrong (later) slash.

### 2.E Corrected spec (byte-derived; NOT applied — src/ untouched per mandate)

Crono's crit (`$9877`=0x01) fires **TWO** whole-screen BG→white flashes, relative to the action dispatch
(`$A013`0→1 = our `h->actStart`):

1. **Flash 1 — primary strike:** onset **rel +50**, peak ~**26 %** lerp-to-white, **linear fade ~7 f**.
2. **Flash 2 — second slash:** onset **rel +91**, peak ~**25 %** lerp-to-white, **linear fade ~15 f**.

Implementation options (`battle.c battleAdvanceAction` + `field.c battleCritFlashRender`):
- **Faithful:** add a `CT_CRONO_CRIT_STRIKE2=91` and fire the flash at BOTH rel-50 and rel-91 (two `critFlashLeft`
  re-arms; give Flash 1 a shorter fade ≈7f, e.g. a separate `CT_CRIT_FLASH_DUR1`). This reproduces the double
  pulse the ROM plays. Keep `CT_CRIT_FLASH_PEAK≈26-28%` (already correct).
- **Minimal (kills the "late" complaint but drops the 2nd pulse):** change `CT_CRONO_CRIT_STRIKE` **87→50** so
  the single flash coincides with the *primary* impact. Then the flash is on-time with the first strike; it
  merely omits the (secondary) second flash rather than being late.
- **Note the shared constant:** `strikeRel` also gates the GATO hurt-flash/knockback for the crit
  (`battle.c:1658-1662`). The first visible impact is rel+50, so the hurt-flash should key on rel+50 too
  (currently rel+87 — GATO flinches ~37f late as well). The HP-apply stays at span-end (rel+177, canonical
  C1:ED1A) — do **not** move that.

Marle's crit remains flash-less (`$9877`=0x0F == her normal crossbow anim, `actCritFlash=0`) — unchanged, canon.

`battle_crit.md` should be corrected: §C.2 documents only Flash B; the primary Flash A (rel+50) was missed.

---

## (3) OPEN

- **Flash A / Flash B split source:** both are byte-observed whole-screen washes with different fade lengths
  (7f vs 15f), plausibly an impact-burst OBJ (A) + BG-palette fade (B). The `0x01` crit anim script (bank-CD)
  is un-decoded (the standing `battle_crit.md` §OPEN "0x01 crit anim script contents") — the two onsets +
  fades are pinned empirically; the opcodes are not.
- **Exact Flash-A fade tail:** main fade ~7f to near-baseline (rel+57), with a ~2% residual to ~rel+60 (the
  lingering burst OBJ). Flash B fully clears by rel+106.
- **Other PCs' crit flashes** (Lucca 0x10 / Robo 0x07 / Frog 0x09 / Ayla 0x0B / Magus 0x0D): distinct crit
  anims, presumably also multi-flash; only Crono (0x01) measured. `battle_crit.md` §OPEN carry-over.
- **ATB mash-cadence artifact:** headless A-mash produces local 2.0-f/tick stretches vs the canonical no-input
  2.53; not a divergence (input-timing artifact, `battle_loop.md` §H), but flagged so a future mash-based
  cadence pin isn't mis-set.
- **Sequential-vs-concurrent enemy walk** (`enemy_move_target.md` Wave-3 residual): ours runs GATO's approach
  AFTER his recharge, the oracle overlaps them → our per-cycle wall-clock is slightly LONGER. Not the user's
  "fast" (it's the opposite direction); noted for completeness.

## Repro (paths from repo root; probe/field copied to the scratchpad `audit/` dir)
```
# (1) cadence + reload + $9F22 (oracle):
ct_probe --monitor "AFAB:8,A013:1" --from gato_press_action.state --input "100:8,0:1300" --frames 1350 --diff  <ROM>
ct_probe --monitor "9F22:3" --from gato_press_action.state --input <mash> --diff --writer  <ROM>   # f768 $9F22[0]←0x48 C1:BDB2
# ours: CT_BATTLE_MASHA=1 CT_BATTLE_TRACE=1 ct_field --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
# (2) crit flash (oracle peek sweep; crit_ready.state = in-battle Crono crit=0xFF):
#   for PAD: ct_probe --peek crit_ready.state sw.raw --btnscript "100:8,0:24,100:8,0:PAD" <ROM>  -> fb mean + .wram[$A013]
#   Flash A peak f97 (rel+50), Flash B peak f138 (rel+91); dispatch $A013->1 @f47
# ours: CT_BATTLE_CRITRATE=255 CT_BATTLE_SCRIPT="n:20,a:1,n:3,a:1,n:300" CT_BATTLESHOT=os.raw CT_BATTLESHOT_AT=<f>
#       dispatch f185, flash f272 (rel+87), NO flash at f235 (rel+50)
```
