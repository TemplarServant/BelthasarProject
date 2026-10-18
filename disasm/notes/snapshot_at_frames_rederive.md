# Re-deriving the AT frames of the four snapshot checks flagged by cbdd2e4

`cbdd2e4` ("battle: the initial ATB is reload-0x47, not a flat 0x1E") re-pinned four `CT_BATTLESHOT`
render canaries **without** re-deriving their AT frames, and said so:

> ⚠ THE FOUR SNAPSHOT CHECKS NEED THEIR *AT FRAMES* RE-DERIVED, not just their fnv accepted:
> lowhp-blink-grey (f228), forest-lastkill-dissolve (f1118), hetake-attack-pose (f272), dissolve-serial
> (f790). Their frames were chosen to catch a specific moment; the fight's timing moved under them, so
> re-pinning the fnv records whatever now sits at that frame under the old name.
> (`git log -1 --format=%B cbdd2e4`)

The four JSON names are `field-battle-lowhp-blink-grey`, `field-battle-forest-lastkill-dissolve`,
`field-battle-hetake-attack-pose`, `field-battle-dissolve-serial` (tools/regress_checks.json).

Nothing in this file was implemented — **no file under `src/`, `tools/` was edited and nothing was
built**. Every number below carries the command that produced it.

---

## 0. The method, and the one lucky lever

These four are canaries over **our own composed frame** (`battleshot: fN fnv=...`, src/field.c:4594-4599),
so the measurement is the current engine run, not the oracle. Two independent handles were used:

**(a) The mechanism measurement.** `CT_BATTLESHOT_AT` was swept frame by frame, the dumped
`[w,h,RGB]` raws were decoded, and the described moment was measured directly — corpse pixel counts in
each enemy's box (the method the `dissolve-serial` note itself records), sprite-region content hashes for
the pose ladder, and digit-pixel colours for the blink phase.

**(b) The pre-cbdd2e4 fnv as a fingerprint.** `git show cbdd2e4 -- tools/regress_checks.json` gives the
*previous* pinned fnv for each check. If the moment merely MOVED (same composed pixels, different frame
number), that old fnv still exists somewhere in the current run — and finding it identifies the new frame
**byte-for-byte**, which is far stronger than re-arguing from the notes' prose.

```
$ git show cbdd2e4 -- tools/regress_checks.json | grep -E "^[-+].*battleshot"
-        "battleshot: f228 fnv=63B03CAF"     +        "battleshot: f228 fnv=DF071B96"
-        "battleshot: f1118 fnv=B361CFED"    +        "battleshot: f1118 fnv=5C8DE90B"
-        "battleshot: f272 fnv=4B1B342C"     +        "battleshot: f272 fnv=FEDBB436"
-        "battleshot: f790 fnv=10498B57"     +        "battleshot: f790 fnv=BE1209E0"
```

Handle (b) LANDED for two of the four (forest-lastkill, hetake-attack-pose) and is provably impossible
for the other two (see §1 and §4).

---

## SUMMARY

| check | OLD AT | what is at the OLD AT now | NEW AT | new fnv |
|---|---|---|---|---|
| `field-battle-lowhp-blink-grey` | f228 | **still the moment** — grey blink phase, Crono critical, pre-window | **f228 (KEEP)** | `DF071B96` (unchanged) |
| `field-battle-forest-lastkill-dissolve` | f1118 | corpse fully gone (0 px), victory-results settle | **f911** | `B361CFED` |
| `field-battle-hetake-attack-pose` | f272 | plain idle mushroom, 21f before the enemy even thinks | **f326** | `4B1B342C` |
| `field-battle-dissolve-serial` | f790 | both corpses gone (0/0 px), stalemate idle | **f588** | `F0A5F838` |

f911 and f326 reproduce the **pre-cbdd2e4 pinned render byte-for-byte** — i.e. those two moments did not
change at all, they only moved in time (−207 and +54 frames respectively).

---

## 1. `field-battle-lowhp-blink-grey` — f228 STAYS. No change needed.

### What the check exists to catch (quoting its `notes`)

> battle_hud_names_blink.md Part B: low-HP flash GREY phase. Crono curHP=5 critical; f252 = GREY blink
> phase ((252/12)&1=1) -> HP/MP digits pal3 grey. Its fnv ... DIFFERS from -white (f240 ...) = the blink
> toggles every 12 frames ... RE-TIME (session-11 ready-scanner): ... **f252 now falls inside the first
> GATO window; f228 = the same grey phase one blink cycle earlier.**

So the frame must satisfy three things: (i) grey phase `(frames/12)&1 == 1`, (ii) Crono CRITICAL
(`curHP <= maxHP>>3`, src/field.c:2220-2221), (iii) NOT inside the first GATO action window.

### All three still hold at f228

**(iii) the GATO window.** It moved, but not past f228/f240:

```
$ CT_BATTLE_CRONOHP=5 CT_BATTLE_SCRIPT="n:600" CT_BATTLE_TRACE=1 CT_BATTLESHOT_HUD=1 \
  CT_BATTLESHOT_AT=228 CT_BATTLESHOT=<scratch>/bg.raw \
  ./build/Belthasar.exe --assets ct_assets.dat --battletest saves/battle_re/gato_t0.raw.wram
battletrace f248 enemy-think
battletrace f251 enemy-arrive
battletrace f278 strike
battletrace f408 act-end
battleshot: f228 fnv=DF071B96
```

First GATO window = **f251..f408**. f252 is still inside it (the exact reason f252 was abandoned) and
f228 / f240 are still both before f248. The note's own selection criterion is intact.

**(ii) Crono critical.**

```
$ CT_BATTLE_CRONOHP=5 CT_BATTLE_SCRIPT="n:600" CT_BATTLE_HPDBG=1 CT_BATTLESHOT_HUD=1 \
  CT_BATTLESHOT_AT=228 ... --battletest saves/battle_re/gato_t0.raw.wram
hpdbg seat0 curHP=5 maxHP=70 curMP=8 -> HP[tens=BLANK ones=5] (block $5E2D+3)
battleshot: f228 fnv=DF071B96
   (same at AT=240)
```
`70>>3 = 8`, `5 <= 8` → critical. No enemy has struck (first strike f278).

**(i) the digits ARE grey at f228 and white at f240** — measured on the pixels, not asserted. Crono's
HUD row is y=171..178 (`ctBattleHudRowY[2]=171`, src/data/battle_hud.h:25); HP-ones is col 21 (x=168),
MP-ones col 24 (x=192) (src/field.c:2240-2245):

```
f216  HPones colours [((24,33,41),19), ((255,255,255),15), ((140,148,148),12)]   <- WHITE (pal2)
f228  HPones colours [((49,66,74),19), ((140,148,148),15), ((90,107,107),12)]    <- GREY  (pal3)
f240  HPones colours [((24,33,41),19), ((255,255,255),15), ((140,148,148),12)]   <- WHITE
f252  HPones colours [((49,66,74),19), ((140,148,148),15), ((90,107,107),12)]    <- GREY
   (MPones identical pattern)
```
The 12-frame toggle is live and f228 is on the grey half.

**The pair is still a blink pair.** Diffing the f228 and f240 composed frames restricted to the HUD strip
(y 160..207) and bucketing by 8px tile column:

```
f228 vs f240  HUD strip: changed tile-cols=[8,9,10,11, 21,22,24, 26,27,28,29]
f216 vs f240  HUD strip: changed tile-cols=[8,9,10,11,          26,27,28,29]   (two WHITE frames)
f228 vs f252  HUD strip: changed tile-cols=[8,9,10,11,          26,27,28,29]   (two GREY frames)
```
Cols **21 (HP ones), 22 (':'), 24 (MP ones)** change *only* across a grey/white pair — that is exactly
the `hpPal` set (src/field.c:2240-2245). Cols 8-11 and 26-29 (the ATB bar cells + enemy area) change
between any two frames and are time-driven, not blink-driven.

### Why no byte-identical old frame exists (and that is expected)

The old pin `63B03CAF` does **not** occur anywhere in the current run:

```
blink: OLD fnv 63B03CAF NOT FOUND in f1..f699     (exhaustive per-frame sweep)
```
This is the *expected* consequence of cbdd2e4 itself: `CT_BATTLESHOT_HUD=1` composites the strip, and the
strip draws the ATB gauge straight from `$AFAB+seat` / `$9F22+seat` (src/field.c:2252-2254). cbdd2e4's own
message records `$9F22` moving from `(c0,m30)` to `(c1,m31)`. So *every* HUD frame of this scenario
changed by design — which is precisely the `+ "battleshot: f228 fnv=DF071B96"` re-pin, and it is correct.

### VERDICT

**Keep AT=228, keep fnv `DF071B96`.** The flag was justified caution, but this one check did not lose its
moment. (Its partner `field-battle-lowhp-blink-white` at f240 is likewise still the white half.)

---

## 2. `field-battle-forest-lastkill-dissolve` — f1118 → **f911**

### What the check exists to catch (quoting its `notes`)

> guards the stand-vs-dissolve gate ... **f1425 = kill+18f, squarely inside the dissolve dither window
> (CT_DISSOLVE_HOLD=9 + elapsed>0 -> dither engaged, CT_DISSOLVE_STEPS=32 not yet exhausted) -- the shot
> must show the RED/orange+white dither pattern ..., not a static green/tan standing mushroom** ...
> RE-ANCHOR #2 (the DEATH-ROUTINE FREEZE ...): **the last kill moved f1017 -> f1100** and AT=1035 fell
> BEFORE it again. **Re-anchored to kill+18 = f1118**

The rule is explicit and arithmetic: **AT = (last kill's act-end frame) + 18**.

### The last kill moved f1100 → f893

```
$ CT_BATTLE_MASHA=1 CT_BATTLE_TRACE=1 CT_BATTLESHOT_AT=1118 CT_BATTLESHOT=<scratch>/fl.raw \
  ./build/Belthasar.exe --assets ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram
battletrace f336 act-end  ... pcAtk=1 ... AF02=FF5E5EFF
battletrace f581 act-end  ... pcAtk=2 ... AF02=FFFF5EFF
battletrace f893 act-end  ... pcAtk=3 ... AF02=FFFFFFFF     <-- 3rd/LAST Hetake dies
battletrace f935 victory
battleloop: victory f1176 pcAttacks=3 cronoHP=70 marleHP=65
```
`$AF02` (the alive-list) flips its last live slot to `FF` on the act-end frame f893. That is the kill
frame by definition: the HP-apply + `dissolveActive[]` arm happen at SPAN END (src/battle.c:4390-4398 →
`battleApplyDamage` → src/battle.c:2077-2078), i.e. on the act-end frame.

**kill + 18 = f911.**

### The dither window, measured on pixels

Corpse box for slot2 = (150,94)-(192,134) (its staged anchor is `[anchor] slot2 id5E ... baked=(152,102)`
from `CT_BATTLE_ANCHORDBG=1`). "Corpse pixels" = pixels differing from the f1000 reference (corpse gone):

```
f884 602   f886 602   f888 602   f890 602   f892 602        <- alive/idle, static
f893 602   (fnv FEFD3DF2 != f892's E076AF98)                <- KILL: recolour starts on the act-end frame
f894 602   f896 602   f898 602   f900 602                   <- CT_DISSOLVE_HOLD=9: recoloured, not yet thinned
f902 587   f904 555   f906 517   f908 485                   <- thinning starts at kill+9 = f902  (exactly HOLD)
f909 468   f910 451   f911 435   f912 417   f913 400        <- kill+18 = f911: 435/602 = 72% left
f920 265   f926 144   f930  62   f932  20   f934 0          <- exhausted ~f933 (kill+40)
f1118  0                                                    <- the OLD AT: corpse long gone
```
The `602 → 587` break at exactly kill+9 is an independent confirmation that the kill frame is f893
(`CT_DISSOLVE_HOLD = 9`, src/battle.c:2078/3619).

Colour histogram of the corpse box (the note's "RED/orange" criterion):
```
f892 (alive): [((99,90,49),94), ((41,41,16),92), ((57,57,41),88), ((115,49,33),70), ...]   green/tan
f911 (AT)   : [((247,57,41),67), ((247,90,49),66), ((247,41,16),66), ((247,49,33),49), ...] every
                                                                     channel-R saturated = the red ramp
```
Visual check of the f911 dump: the fight-ending Hetake is a red/orange dithered blob next to Marle —
matching the note's own acceptance sentence verbatim.

### The clincher

```
$ CT_BATTLE_MASHA=1 CT_BATTLESHOT_AT=911 ... --battletest saves/battle_re/forest_pre.raw.wram
battleshot: f911 fnv=B361CFED
battleloop: victory f1176 pcAttacks=3 cronoHP=70 marleHP=65
```
`B361CFED` is the **exact fnv this check was pinned to before cbdd2e4**. The moment did not change at
all; it moved 207 frames earlier (the three Hetakes no longer buy a turn, so three enemy action-freezes
vanish from the ledger). This is a byte-for-byte identification, not an argument.

### What is at the OLD frame now

`f1118` → corpse pixels **0**; the fight is in the victory/results settle (`victory f935`,
`results-advance f1150`). The re-pinned `5C8DE90B` pins a results screen under the name
"lastkill-dissolve" — no dissolve is on screen at all.

### VERDICT: **AT 1118 → 911, expect `battleshot: f911 fnv=B361CFED`.**

---

## 3. `field-battle-hetake-attack-pose` — f272 → **f326**

### What the check exists to catch (quoting its `notes`)

> the Hetake attack rides the REAL approach mover ... with the ROM E3 poses 03/04/05/06 at the live foot
> ... RE-SCENARIO (SESSION-9): ... switched to the HOLD script (n:900) where **Hetake-0 attacks NORMALLY:
> window f259-461, strike f286, shot f280 = the open attack-pose ladder mid-anim.** RE-TIME (session-11
> ready-scanner): ... dispatch frames shift ~-8.

### The attack window moved f239→f293 (its length and internal offsets are unchanged)

```
$ CT_BATTLE_SCRIPT="n:900" CT_BATTLE_TRACE=1 CT_BATTLESHOT_AT=272 ... \
  --scene 19 --battletest saves/battle_re/forest_pre.raw.wram
battletrace f285 enemy-noop id5E#2 ... A013=0 span=0
battletrace f290 enemy-think   ... AE93=01 AEE3=01,01,05
battletrace f293 enemy-arrive  ... A013=1 span=202          <- the act window OPENS, length 202
battletrace f320 strike        ... knock=(t0,6,0)           <- strike = window+27
battletrace f495 act-end                                    <- 293+202 = 495
```
`span=202` and `strike = start+27` are byte-identical to the note's recorded `f259-461` (202) and
`f286` (+27). Only the window's START moved.

### The pose ladder, measured in the RENDER

`battleHetakeRender` selects the E3 pose by `rel = frames - actStart`:
`rel<3 → 0x03, rel<8 → 0x04, rel<29 → 0x05, else 0x06` (src/field.c:3395-3397). Hashing slot0's sprite
region (96,20)-(146,80) of the composed frame, per frame:

```
f288 bb6f0686 rel=-5   <- IDLE hash
f290 0d4c8620 rel=-3       f292 6458dc0a rel=-1     f293 70fbbcc8 rel=0
f296 1fdcf1fb rel=3    CHANGED   (0x03 -> 0x04)
f301 c928f145 rel=8    CHANGED   (0x04 -> 0x05)
f309 33891ac5 rel=16   CHANGED
f316 c928f145 rel=23   CHANGED
f320 13980c2f rel=27   CHANGED   (the strike frame)
f322 a4c38647 rel=29   CHANGED   (-> 0x06, the visibly OPEN lunge pose; held to rel 48)
f342 bb6f0686 rel=49   CHANGED   back to the IDLE hash
```
The ladder is intact and its transitions land exactly on the code's 3/8/29 boundaries.

### The clincher, and why the notes' prose is stale

The pre-cbdd2e4 pin `4B1B342C` occurs at exactly ONE frame of the current run:

```
$ (per-frame sweep f280..f359)
  hetake: OLD fnv 4B1B342C FOUND at f326
$ CT_BATTLE_SCRIPT="n:900" CT_BATTLESHOT_AT=326 ... --scene 19 --battletest saves/battle_re/forest_pre.raw.wram
battleshot: f326 fnv=4B1B342C
```
f326 = window + **33**, i.e. inside the `0x06` band — **the open pose**, which is what the note's phrase
"the open attack-pose ladder mid-anim" describes. Visual check of the f326 dump: the top Hetake is in the
open lunge assembly (clearly distinct from the two closed idle mushrooms) while Marle is white
hurt-flashing.

⚠ **The notes' recorded window numbers are stale, and following their arithmetic would have picked the
wrong frame.** `f259-461 / strike f286 / shot f280` minus the recorded `-8` re-time implies shot = window
+21, which today is f314 — and the f314 render (`A15AF98C`) is NOT the old render. The byte-identical
fingerprint says the true old offset was +33 (old window start = 326-33 = f239, strike f266, act-end
f441). Later re-pins (the +1px staging fix, the arrival-anchored PC attack) moved the window without the
notes being updated. **The fingerprint is the evidence; the prose is not.**

### What is at the OLD frame now

`f272` = window − 21, i.e. **21 frames before the enemy even thinks** (`enemy-think f290`). Measured: the
slot0 sprite hash at f272 equals the idle hash, and the slot0 box differs from a quiescent f200 reference
by 16 px (background noise). Visual check: three closed idle mushrooms, nothing attacking. The re-pinned
`FEDBB436` pins a plain idle frame under the name "hetake-attack-pose".

### VERDICT: **AT 272 → 326, expect `battleshot: f326 fnv=4B1B342C`.**

---

## 4. `field-battle-dissolve-serial` — f790 → **f588**

### What the check exists to catch (quoting its `notes`)

> When Cyclone kills two enemies on the SAME frame the ROM does NOT animate both: it runs one death
> routine, then the other. ... **THE SHOTS: f790 the higher-index corpse (slot1) is mid-dither while
> slot0 is still fully drawn**; f830 slot1 is gone and slot0 has STARTED its own dither. Measured as
> **opaque-pixel counts in each enemy's box** -- slot0 holds a constant 621 px through f770-f815 (intact,
> queued) then falls 463 -> 170 -> 0, while slot1 falls to 0 first.

### The simultaneous double-kill still happens — it moved to f563

```
$ CT_BATTLE_SCRIPT="n:40,d:2,n:10,a:2,n:10,a:2,n:10,a:2,n:600" CT_BATTLE_TRACE=1 CT_BATTLESHOT_AT=790 \
  ... --scene 19 --battletest saves/battle_re/forest_cyclone.raw.wram
battletrace f236 pc-commit-tech ... AF02=5E5E5EFF
battletrace f250 pc-act-tech    ... A013=1 span=313
battletrace f279 strike
battletrace f563 act-end        ... pcAtk=1 ... AF02=FFFF5EFF   <- slot0 AND slot1 die on ONE frame
```

### The serialization, measured on pixels (the note's own method)

Boxes from `CT_BATTLE_ANCHORDBG` (`slot0 baked=(104,35)`, `slot1 baked=(72,67)`): slot0 =
(102,27)-(144,67), slot1 = (70,59)-(112,99). Corpse pixels = differing from an f700 reference:

```
f562  slot0=629  slot1=645
f563  slot0=629  slot1=645     <- KILL (both)
f571  slot0=629  slot1=645     <- HOLD 9
f572  slot0=629  slot1=625     <- slot1 thinning begins at kill+9  (629 unchanged = slot0 QUEUED, intact)
f581  slot0=629  slot1=447
f588  slot0=629  slot1=305     <- kill+25, the MIDPOINT of slot1's 32-step dither (47% left)
f596  slot0=629  slot1=150
f603  slot0=629  slot1= 16     <- slot1 done at kill+40; slot0 STILL 629 = 100% intact
f612  slot0=629  slot1= 16
f613  slot0=609  slot1= 16     <- NOW slot0 starts its own dither (= slot1-done + HOLD 9)
f620  slot0=470  ...  f632 slot0=231  ...  f644 slot0=0
f790  slot0=  0  slot1=  0     <- the OLD AT
f830  slot0=  0  slot1=  0
```
slot0 holds a **constant 629 px for the entire 50 frames** that slot1 dissolves, then runs its own — the
exact serialization (`battleAdvanceDissolves`, src/battle.c:2222-2231, highest combatant index first) the
check exists to pin.

### Choosing the new frame

The old AT cannot be fingerprinted: `10498B57` does **not** occur in the current run
(`serial: OLD fnv 10498B57 NOT FOUND in f1..f999`, exhaustive per-frame sweep). That is expected —
cbdd2e4 records the RNG stream position moving so that "the cmb4 21 -> 20", i.e. the rendered Cyclone
damage popup differs, so no frame of this fight can be byte-identical.

So the offset is re-derived from the note's own criterion. slot1's dither runs kill+9 .. kill+40; "**mid**-
dither" = the midpoint, kill+25 = **f588**, where slot1 is at 305/645 = 47% and slot0 is at 629/629 = 100%.

```
$ CT_BATTLE_SCRIPT="n:40,d:2,n:10,a:2,n:10,a:2,n:10,a:2,n:600" CT_BATTLESHOT_AT=588 \
  ... --scene 19 --battletest saves/battle_re/forest_cyclone.raw.wram
battleshot: f588 fnv=F0A5F838
```
Visual check of the f588 dump: the slot1 mushroom is a red mid-dither blob while the slot0 mushroom is
still fully drawn in its normal green/tan palette — one dissolving, one queued and intact.

⚠ **HONEST BOUND on the offset.** The note's two pixel-ledger statements are mutually inconsistent by
~7 frames, so the OLD offset cannot be pinned exactly: "slot0 holds a constant 621 px through f770-f815"
implies old kill ≈ f765 → old AT = kill+25; "then falls 463 -> 170 -> 0" (a ~19px/frame slope) implies
old kill ≈ f771 → old AT = kill+19. Any frame in **f581..f595** satisfies the criterion identically
(slot0 = 629, slot1 mid-dither). If the parent prefers the sibling check's stated rule (kill+18) instead
of the midpoint, that is **f581, fnv `91C09574`** (slot0=629, slot1=447). Both are defensible; f588 is
recommended because "mid-dither" is the note's own word.

### What is at the OLD frame now

`f790` → slot0 = 0 px, slot1 = 0 px. Both corpses vanished at f644. The re-pinned `BE1209E0` pins an idle
stalemate frame under the name "dissolve-serial".

### VERDICT: **AT 790 → 588, expect `battleshot: f588 fnv=F0A5F838`.** (alt: f581 / `91C09574`)

---

## 5. Side observations (NOT part of the four; flagged, not acted on)

* **`forest_cyclone` never reaches victory.** In the current run only ONE PC action ever dispatches
  (`pcAtk=1`); the third Hetake survives and the run terminates on the `CT_BATTLE_DEBUGEND` safety net:
  `battletrace f4000 end-debug ... AF02=FFFF5EFF`, with both PC ATB timers reading ready
  (`AFAB(c0,m0,g255)`) and `cur=1` pinned from f677 onward. The check has no `battleloop:` expect, so
  nothing grades this. **NOT DERIVED** whether this stall is new with cbdd2e4 or pre-existing — proving
  that would require building the parent commit, which this run is forbidden from doing. Worth its own
  triage.
* The `field-battle-lowhp-blink-white` partner (AT=240) was not in the flagged four and was separately
  confirmed above to still be the WHITE half of the same toggle — no action.

## 6. What was NOT derived

* No ROM disassembly was used and none was needed: all four are canaries over **our composed frame**, and
  the task's own framing allows measuring the current engine run for those. No claim here is about the
  ROM's own timing.
* **Not oracle-compared.** None of the four fnvs has an oracle reference; they are port signatures at an
  instant. What is oracle-anchored is only the *criteria* in each check's notes (dissolve HOLD/STEPS,
  the descending-index serialization, the E3 pose ladder, the 12-frame blink period), and those criteria
  were each measured to hold at the recommended frame.
* The old AT offset for `dissolve-serial` is bounded (kill+19..kill+25), not pinned — see §4.
* The `hetake-attack-pose` notes' window numbers (`f259-461 / f286 / f280`) are **stale and were not
  reconciled**; they disagree with the byte-identical fingerprint by 12 frames. The fingerprint was
  trusted. Whoever applies the new AT should also correct that sentence in the check's `notes`.
