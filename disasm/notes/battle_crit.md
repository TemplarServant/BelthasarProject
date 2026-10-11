# BATTLE CRITICAL HIT — the PC crit roll, 2× damage, and the WHITE SCREEN FLASH (Crono-only)

Derives the PC critical-hit system from the user report *"when crono or marle land a critical hit, the screen
should flash white"* (translated from Portuguese) (on a PC crit the screen should flash white). Our engine has **no crit path**
(always-hit/no-crit, `battle_loop.md` open). Every claim is anchored to a `tools/dis.py` address (HiROM flat
offset = `(bank−0xC0)<<16 | addr`; battle code runs 16-bit A/X, `m=0 x=0`) or an oracle `--monitor`/`--peek`
observation on `gato_press_action.state`. Writer PCs = the PC *after* the store retires.

> ⚠ **HEADLINE (mandate: the user's words are a POINTER, not the spec).** The oracle shows the white screen
> flash fires on **Crono's** crit but **NOT Marle's**. The flash lives in the per-character CRIT ANIMATION;
> Crono has a dedicated crit anim (id `0x01`) that flashes, but **Marle's crit anim id is `0x0F` = her ordinary
> crossbow anim**, which does not flash. So "crono OR marle" is only half-true canonically (§C). Implementing a
> Marle flash would be INVENTION. See §C + §OPEN.

---

## A — THE CRIT ROLL  (site, RNG, threshold, applicability)

### A.1 Where the roll lives — super-command `C1/ED89`, dispatched from the hit's op-1E
The physical hit's anim exec-track op **`1E`** (`PerformSuperCommand`, `battle_interp_verify.md` T2.B) drives the
super-command handler `C1/EAE8`, which fetches a 3-byte record and dispatches through a jump table
(`dis.py 1EB20`):

```
C1/EB22  LDA $B2CC              ; $B2CC = super-command index (=1 for the basic physical hit)
C1/EB28  JSR C1/C90B            ; offset = $B2CC × stride
C1/EB2D  LDA $CC2A06,X → $1C    ; record byte +1
C1/EB35  LDA $CC2A07,X → $1E    ; record byte +2 = the CRIT MULTIPLIER  (=2, see A.4)
C1/EB3D  LDA $CC2A05,X → $20    ; record byte +0 = dispatch index
C1/EB43  ASL A / TAX
C1/EB45  JSR ($FA61,X)          ; ** dispatch **  entry[0] @C1/FA61 = C1/ED89
```
ROM record table `CC/2A05` (stride 3, `[dispatch, $1C, $1E]`): **idx 1 = `00 00 02`** → dispatch `0` → `C1/ED89`,
multiplier `$1E`=**2**. (idx 0 = `00 00 00`; idx 3-6 = `02 .. 04` = other attack types; the enemy physical uses a
different index whose dispatch ≠ 0, §D.)

### A.2 The roll itself — `C1/ED89`  (`dis.py 1ED89`, the byte-exact routine)
```
C1/ED89  LDX $B1F4             ; X = attacker battle-block base index ($B1F4)
C1/ED8C  LDA $5E72,X           ; A = attacker block+0x45  = the CRIT RATE
C1/ED8F  STA $1C               ; $1C = crit rate
C1/ED93  LDA #64               ; 100
C1/ED95  JSR C1/AF79           ; roll = rand(0..99)   [SEPARATE RNG stream, A.3]
C1/ED98  CMP $1C               ; roll − critRate
C1/ED9A  BEQ ->ED9E            ;   roll == rate → CRIT
C1/ED9C  BCS ->EDB8            ;   roll  > rate → no crit (RTS)
C1/ED9E  LDX $AD89 / STX $28   ; --- CRIT PATH ---   $28 = current damage
C1/EDA3  LDA $1E  / TAX / STX $2A   ;                $2A = multiplier ($1E = 2)
C1/EDA8  JSR C1/C90B           ; $2C = damage × multiplier
C1/EDAD  STX $AD89             ; ** CRIT DAMAGE STORE **  (monitor writer C1:EDB0)
C1/EDB0  LDA $AE9B / ORA #80 / STA $AE9B   ; ** SET CRIT FLAG $AE9B bit7 **  (writer C1:EDB8)
C1/EDB8  RTS
```
**Crit iff `roll ≤ critRate`** (BEQ→crit, fall-through→crit, BCS→no-crit). With `roll ∈ [0,99]`,
**P(crit) = (critRate+1)/100**.

⚠ The dossiers' "crit compute = `C1/EDB0`" was the `--writer` PC *after* the `STX $AD89` at `C1/EDAD` retires —
the store is EDAD, monitored as EDB0. There is **no** "pick DCED-vs-EDB0" branch: `C1/DCED` **always** computes
the normal damage into `$AD89` (formula × variance, `battle_loop.md` S3), and `C1/ED89` **conditionally
MULTIPLIES** it in place afterward. On a non-crit, ED89 RTSs at EDB8 and `$AD89`'s last writer stays DCED; on a
crit, EDAD overwrites it (monitor shows the crit's last writer = EDB0).

### A.3 The RNG — `C1/AF79` = `C1/AF22` with a SEPARATE index `$B3E6` (same table `FD/BA61`)
`dis.py 1AF22` vs `1AF79` are **byte-identical in algorithm** — both read `LDA $FDBA61,X`, divide via `C1/C92A`,
`+$25`, and `INC` the index — **except AF22 uses index `$26`** (the main stream: damage variance, hit-roll,
bonus) **and AF79 uses `$B3E6`** (the crit stream). So the crit roll draws from the **same FD/BA61 table** as our
ported `battleRng`, but on an **independent counter** that the main pipeline never touches.

**Oracle proof** (`--monitor "AE9B:1,AD89:2,5E72:1,5EF2:1,B3E6:1,0026:1,6130:2"`, mash `('100:20,0:40')×45`):
`$B3E6` (writer `C1:AFCC`) advances **once per PC physical hit** — f640, f1003, f1624, f1789 — and **does NOT
advance on enemy hits** (f476, f769, f1152). `$26` (writer `C1:AF73`) advances on every AF22 draw (variance/hit/
bonus). Two crits fired naturally in that run: **f1003** (`$AD89` 17→**34**, `$AE9B`→80) and **f1789** (18→**36**).

### A.4 Threshold source + applicability (PATCH-CONFIRMED)
- **Crit rate = attacker battle-block `+0x45`.** Loaded at battle-init by the stat-copy loop `C1/CF3B` (`LDA
  $CC0000,X / STA ($08),Y`). Observed: **Crono `$5E72` = 0x0A (10)**, **Marle `$5EF2` = 0x14 (20)** (set f289).
  → P(crit) ≈ **11 % Crono / 21 %** Marle. **Patch test** (state WRAM @0x45; set `$5E72`): **`0xFF` → every
  Crono hit crits** (damage doubles, `$AE9B`→80); **`0x00` → never crits** — so `+0x45` **IS** the crit rate.
- **Multiplier = 2** (record `CC/2A07`, idx1 → `$1E`=2). Confirmed 17→34, 18→36, 16→32 (exact doubling).
- **Applies to PC physicals only, BOTH melee and ranged.** Crono (formula type 1, `$AEEB`=01) and Marle (type 2,
  ranged, `$AEEB`=02) both roll — both advance `$B3E6`, both were observed to crit (Crono −34; Marle −36, forced).
  Enemy physicals do **not** roll (§D). Techs/items use other super-commands (unobserved — §OPEN).

---

## B — CRIT DAMAGE FORMULA
`crit_damage = normal_damage × 2`. The **normal** damage is the full pipeline `C1/DCED`
(`battle_loop.md` S3 / ADDENDUM A: `base = $AEEF×(A[+0x36]×12+A[+0x43]×5)/18` for PC melee, `×(256−T[+0x3E])/256
+ bonus`); the crit routine `C1/ED89` then does `$AD89 = $AD89 × $1E` (`$1E`=2, `C1/C90B` multiply at EDA8) and
sets `$AE9B` bit7. Byte-verified: base 17→34, 18→36, 16→32 (oracle `$AD89` deltas / GATO curHP `$6130`).

*(Aside: a SECOND crit doubler exists at `C1/F1C7` — `LDA $AE9B / BIT #80 / … LDX #0002 … $AD89×2`, or `$AD89 =
9999` if `$1E`==1 — for a different attack-type path; the basic physical uses ED89, not F1C7. Noted, not used.)*

---

## C — THE SCREEN FLASH  (what changes, frames, curve, sync)  — CRONO ONLY

Captured deterministically: a "Crono command-ready" checkpoint (`--peek … "100:8,0:380"` + `PEEK_SAVE`,
Crono `$95D5`=0 `$A013`=0 idle), crit rate patched to `0xFF`, then one driven attack (`--btnscript
"100:10,0:30,100:10,0:PAD"`; PAD sets the captured frame — **the btnscript length is the capture frame, NOT
`--frames`**). Every frame's framebuffer mean brightness computed; crit anim confirmed `$9877`=01, `$AE9B`=80.

### C.1 What changes — a BG-palette brighten toward white (OBJ palettes untouched)
Comparing CGRAM (state offset 197073) at the flash peak vs baseline:
- **BG palette (colours 0-127): mean 5-bit 12.50 → 19.57** at peak; **112/128 entries change, ALL brighter.**
- **OBJ palette (128-255): 17.25 → 17.43** — essentially **unchanged** (sprites keep normal colour).
- Backdrop colour 0 stays `0000` (black) — it is **not** a backdrop swap.
So the whole **background** washes toward white while sprites stay normal — the "white flash". Rendered frames
(`fb2png`) show the trees/ground/stone visibly lightened at f137-140, plus a large white **impact BURST** OBJ
centered on the target (GATO) at f140-150. Since LakeSnes stores raw CGRAM (color-math is applied at scanline,
not stored), the changed *stored* BG values ⇒ this is a **software palette-add** to the BG palettes, not hardware
color-math.

### C.2 Frames + intensity curve (fb mean brightness; baseline ≈ 80)
```
frame :  ≤136  137   138   139   140   141   144   148   150   152+
mean  :   80   126*  123   119   120   116   107    94    88    80     (* peak, +57%)
```
**Sharp single-frame onset at the strike (f137), then a ~linear fade back to baseline over ~15 frames**
(f137→f152). One pulse per crit (a blink). White impact-burst OBJ overlaps f140-150. Onset ≈ **+87 frames after
dispatch** in this capture = the crit strike/impact frame of Crono's `0x01` anim (later than his normal 0x00
strike; anim-specific).

### C.3 Sync + trigger — it is IN the per-character CRIT ANIMATION (bank-CD), gated by the crit flag
On a crit, the crit-flag-gated anim-select `C1/435B` (`LDA $AE9B / BPL →normal`) loads the **crit** anim id and
params (`dis.py 14340`):
```
C1/435B  LDA $AE9B / BPL ->14394          ; crit? (bit7)  no → normal path (14394, params CD/4015)
C1/4360  LDA $CD401F,X → $987A … $9884     ; crit anim PARAMS from CD/401F
C1/438B  LDA $CD400E,X → $9877             ; crit ANIM ID from CD/400E[charid]   (writer C1:4392)
```
`CD/400E` (bank CD, per-charid crit-anim id) = **`01 0F 10 07 09 0B 0D`** = Crono `0x01`, **Marle `0x0F`**, Lucca
`0x10`, Robo `0x07`, Frog `0x09`, Ayla `0x0B`, Magus `0x0D`. The flash is emitted by the **`0x01` crit anim
script** (bank-CD anim engine = the standing boundary), NOT by the shared crit params.

**Proof the flash is anim-specific, not crit-flag/param-driven (the mandate crux):**
- **Normal Crono hit** (crit rate patched `0x00`, anim `0x00`, `$AE9B`=00): mean brightness **flat 80** across the
  whole strike window (f134-150) — **no flash**. ⇒ flash is crit-specific.
- **Marle crit** (crit rate `0xFF`, driven to attack; `$9877`=**0F**, `$AE9B`=**80**, GATO 42→6 = −36 crit): mean
  brightness **flat ~81 across her ENTIRE crit anim** (densely swept f190-305) — **NO flash, NO white burst**,
  only the normal damage number. Because her crit anim id `0x0F` **equals her normal crossbow anim** (which does
  not flash), and the flash lives in the anim script, **Marle's crit does not flash**.

So: **Crono's crit flashes; Marle's does not.** The five PCs whose crit-anim id ≠ their normal-attack id (Crono,
Lucca, Robo, Frog, Ayla, Magus — all but Marle) get a dedicated crit anim and *presumably* flash (only Crono
verified; the rest unobserved — §OPEN).

---

## D — ENEMY CRITS: no (GATO does not roll)
The crit stream `$B3E6` (writer `C1:AFCC`) **does not advance on any GATO attack** (enemy computes f476/f769/
f1152 — no `$B3E6` change), only on PC hits. The enemy physical uses a different super-command index whose
dispatch ≠ 0 (not `C1/ED89`), so `C1/AF79` is never called and `$AE9B` is never set for an enemy. ⇒ **enemies do
not crit and never trigger the flash** (GATO only; other enemies unverified but the roll is PC-attack-gated).

---

## E — CRIT POPUP PRESENTATION: no difference
The crit damage number uses the **same font/colour/size as a normal hit**: OBJ digit tiles `0x190+d`, **palette
0** (white), prio 3, 8×8 — verified from the OAM of both a Crono crit ("34" = tiles 0x193,0x194) and a Marle crit
("36" = 0x193,0x196): all **pal 0** (identical to the normal-hit digits, `battle_actionfx.md` P1.1). Only the
VALUE is doubled. (An early visual impression of a "blue" number was the white impact-burst tint — the OAM
palette is authoritative: pal 0.)

---

## F — IMPLEMENTATION SPEC (`src/battle.c`) — mandate-safe

1. **Add a SECOND RNG stream for the crit roll** (mirror `C1/AF79`'s `$B3E6`): a new independent index into the
   SAME `battleRng` table (`FD/BA61`), separate from the existing `$26` stream. Because it is independent, adding
   it does **not** shift any existing `$26` draw → **no pinned damage/HP/turn value changes** (see §G).
2. **In `battleComputeDamage`** (src/battle.c ~L676, called at dispatch L1165/L1210 — where the `$26` draws
   already happen, so the order is preserved): AFTER the normal damage is computed, for a **PC physical** attack:
   ```
   critRate = attacker crit stat        // block+0x45; Crono 10, Marle 20 (or read the char stat)
   roll     = battleRngCrit(0..99)       // the NEW $B3E6-equivalent stream (draw ONCE per PC physical)
   if (roll <= critRate) { dmg *= 2; h->actCrit = 1; }   // set a per-attack crit flag (reset per action)
   ```
   Gate on PC physical (Crono melee / Marle ranged); do NOT roll for enemy attacks or techs.
3. **Flash render layer (Crono, and any PC with a dedicated crit anim):** when a crit lands AND the attacker has a
   dedicated crit anim (`CD400E[charid] != normalAnimId[charid]` — true for all PCs **except Marle**), play a
   **whole-screen BG-palette brighten toward white**: apply an additive-white bias to the BG palette entries only
   (leave OBJ/sprite palettes alone), **peak on the crit strike/impact frame**, then **linear fade to zero over
   ~15 frames** (curve §C.2: 80→126→…→80). Optionally the white impact-burst OBJ on the target (part of the 0x01
   anim). **Do NOT flash for Marle** (her crit anim `0x0F` has no flash — flashing her would be invention).
   Hook the flash at the crit strike event (the anim `1E` super-command frame our FX layer already keys the
   hurt-flash/popup on, `battle_actionfx.md` — the crit flash is a SEPARATE whole-screen effect layered on top of
   the existing per-target pal4 hurt-flash).
4. **Crit flag lifetime:** set on the roll, **clear per action** (ROM clears `$AE9B` at `C1:406C`/`C1:8D1F`/
   `C1:BFC8` after each action) so it never persists.
5. **Damage number:** unchanged (same white pal0 font; just the doubled value).

---

## G — REGRESSION IMPACT: none expected
The crit roll is a **new independent RNG stream** (`$B3E6`), so it does not perturb the existing `$26` draws that
produce the pinned damage/HP/turn values (`field-battleloop-*`, `field-battletest-gato`, `field-battlehud-gato`).
The flash is a pure render addition on the crit strike (like the existing FX layer). The pinned `battleloop`
signatures assume **no crit** (always the normal formula); if the crit roll is enabled with the real rate, a
crit *will* eventually fire in a long mash and change `pcAttacks`/`victory frame`/HP — so **either** keep crit
disabled for the deterministic regression fights **or** re-pin those signatures after enabling (crit is
probabilistic → the mash fights would need a fixed crit-stream seed to stay deterministic). Flag this before
wiring: the crit changes fight OUTCOMES, unlike the pure-cosmetic FX work.

---

## H — HIT/MISS ROLL (fell out of the same disasm — recorded per the mission)
`C1/E9F0` (`dis.py 1E9EE`, 8-bit A): `$16 = hitStat×3` (E9E4-E9E8: `ASL A / CLC / ADC $16`), then
`LDA #64 / JSR C1/AF22` (roll 0..99 on the **MAIN `$26` stream**), `CMP $16`:
```
C1/E9F3  CMP $16 / BEQ ->EA02 / BCC ->EA02   ; roll ≤ hitStat×3 → HIT
C1/E9F9  LDA #FE / STA $16 / STA $B3BF        ; else → MISS ($16=0xFE marker, $B3BF=0xFE)
```
**Miss iff `rand(0..99) > hitStat×3`.** Uses the main `$26` stream (AF22), distinct from the crit stream. The
`hitStat` source (`$16` seed) is block/attack-record derived (same stat family as the damage terms) — exact byte
not pinned here (minor open). Miss/evade rendering (the "Miss" glyph, `battle_actionfx.md` notes unused tiles
`0x19A/0x19B`) is out of scope.

---

## OPEN
- **Marle-flash / user report mismatch.** Oracle: Crono crit flashes, **Marle crit does not** (§C, empirically
  flat over her whole forced crit anim). The user asked for both — implementing a Marle flash is INVENTION.
  Recommend confirming with the user; if desired anyway, it is a deliberate deviation, not canon.
- **Other PCs' crit flash** (Lucca 0x10, Robo 0x07, Frog 0x09, Ayla 0x0B, Magus 0x0D): distinct crit-anim ids ⇒
  *presumably* flash, but only Crono (0x01) verified. Needs a per-character capture.
- **The `0x01` crit anim script contents** (the exact BG-palette-add opcode + the white-burst OBJ + per-frame
  fade curve) = the bank-CD anim-engine boundary (`battle_interp_verify.md` T2, `$0520+n*0xC` / `FD:E170`).
  Characterized empirically (BG mean 12.5→19.57, ~15-frame linear fade) but not opcode-decoded.
- **Crit rate origin decomposition:** block+0x45 is the battle-init stat copy (`C1/CF3B`); the base-stat vs
  weapon-bonus split into that byte not traced (observed Crono 10 / Marle 20 suffice).
- **Techs / items / multi-hit crits:** only PC basic physicals observed (super-cmd idx 1 → ED89). Tech/item
  super-commands and whether they crit = unobserved.
- **Enemy crits:** GATO does not roll; other enemies unverified (mechanism is PC-attack-gated, so likely none).
- **Exact hit-roll `hitStat` byte** (§H) — block/record-derived, not pinned (miss/evade out of scope).

## Scratchpad artifacts (paths for the caller; not committed)
Dir: `%TEMP%/claude/C--Users-…-e5608df4-…/scratchpad/`
- `ct_probe.exe` (oracle copy).
- States: `crono_crit_ready.state` (Crono ready, crit rate 0xFF → deterministic crit on drive
  `"100:10,0:30,100:10,0:PAD"`), `crono_normal_ready.state` (crit rate 0, control), `both_crit_ready.state`,
  `marle_current.state` (Marle ready after Crono's crit → drive `"0:120,100:12,0:26,100:12,0:PAD"`),
  `flash_peak.state`/`flash_base.state` (CGRAM compare), `inbattle_f470.state`/`crit_forced.state`.
- Renders: `n138.png`/`n140.png`/`k148.png` (Crono crit flash + white burst), `k152.png` (baseline),
  `h180.png` (Crono crit "34" number, pal0), `my282.png` (Marle crit — NO flash).
- Method: capture frame = btnscript total length (not `--frames`); fb mean-brightness auto-detects the flash;
  CGRAM at state offset 197073; crit rate patch at WRAM offset 0x45 (`state[0x45+addr]`).

---

## ADDENDUM (Wave 2 implementer — IMPLEMENTED + measured refinements)

**SHIPPED (src/battle.c + src/field.c):** the crit roll (separate `$B3E6` stream via `battleRngCrit`, rate =
block+0x45, ×2 at `battleComputeDamage`), the Crono crit anim selector `$9877=0x01` + span, the Marle ×2
no-flash asymmetry, and the whole-screen BG→white flash (`battleCritFlashRender`, brightens the framebuffer
BEFORE the combatant sprites, ~28% lerp-to-white peak, linear fade over `CT_CRIT_FLASH_DUR`=15f).

**MEASURED: the Crono crit anim (0x01 → CD/6E4A) FREEZE SPAN = 177 frames** (refines the OPEN "0x01 crit anim
script contents"). Oracle `--monitor "A013:1,9877:1,AD89:2,6130:2" --from crono_crit_ready.state --btnscript
"100:10,0:30,100:10,0:400"`: `$A013` 1@f45 → 0@f222 = **177f** (vs his normal 0x00 = 125), `$9877`=01
throughout, crit dmg **34 = 17×2** (GATO `$6130` 76→42 at span-end). Shipped as `CT_SPAN_PC_CRIT`=177.

**VERIFIED headless:** `CT_BATTLE_CRITRATE=0` (test hook forcing block+0x45) reverts the GATO mash fight
BYTE-IDENTICAL to the no-crit baseline (f2179 pcAtk6 cronoHP44) — confirming the crit stream is independent of
the `$26` damage stream (§G). `CT_BATTLE_CRITRATE=255` doubles every hit (Crono 34, Marle 16). Flash fade
captured (black-BG shot, mean): onset f256=72.4 → f260=54.2 → f264=31.3 → f268=13.1 → f271=4.5 (baseline) —
the §C.2 sharp-onset-then-~15f-linear-fade curve; Marle crit frame = 9.1 (NO flash). The white impact-BURST
OBJ (§C.1) is NOT baked (the anim-engine boundary) — the screen flash is the shipped user-visible effect.
Regression: `field-battle-crit-x2` (forced-crit determinism + the 0x01 anim). `CT_CRONO_CRIT_STRIKE`=87 (§C.2).
