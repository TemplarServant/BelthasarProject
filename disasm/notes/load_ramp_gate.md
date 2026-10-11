# C0/2848 decides at its ENTRY: the load's brightness ramp and `$011E` (2026-09-17)

User item (translated from Portuguese): *"in some scenes (like the second one) you can see the scene before the curtain appears, unlike the
ROM"* — in some legs of the attract the scene is fully visible for a moment before the curtain (the window wipe)
opens over it. On the cartridge those legs are BLACK until the wipe lights them.

## 1. The ROM

```
C0/2848  JSR C0/286C          ; one pipeline frame -- C0/28E1, 0A50, 6F79, 7155, E935, B204, A7E9, B0E6,
                              ; 6E5C, 70E9, 29F7, 6EB0, 2B78, 6EF1, 6F0C, B262, 6F5E, 0B28, EC60.
                              ; NOT C0/00BF: no scheduler, so NO script of the new location has ticked yet.
         BNE ->2866           ; C0/286C ends `28A7 LDA $1E / 28A9 RTS` -- the DECISION, taken HERE
  284D   INC $19              ; the ramp: one brightness step a frame, C0/881E + C0/00BF + C0/EC60
  2860   LDA $19 / CMP #0F / BMI ->284D
  2866   STZ $1E              ; ...and the flag is cleared at the TAIL, on both paths
  2868   STZ $0407
```

`$011E` is set by the ChangeLocation family: `C0/3B45 LDA #01 / STA $1E` (op 0xDE, before the 0xDD body) and
`C0/3B98 LDA #01 / STA $1E` (op 0xDF, before the 0xE1 body). The location load itself blanks the screen —
`C0/0B86`'s constant-init pass ends `0B9E LDA #E0 / STA $21 / 0BA2 STZ $19 / 0BA4 STZ $BC` — so a skipped ramp
leaves `$0119 = 0` and the destination lights itself (its own `F0` fade, or a wipe).

**The ramp's own iterations DO tick the new location's scripts** (C0/00BF), and a script that stages the NEXT
location with 0xDE/0xDF sets `$011E` again while they run. That is why the decision and the clear are at
opposite ends of the routine.

## 2. Measured on the cartridge (saves/attract_cena_inicial.state)

`--monitor 7E011E:1,7E0119:1,7E0100:2 --diff --writer`, the attract's Zombor leg (loc 087):

| frame | what |
|---|---|
| f7087 | `$011E = 01` by **C0:3B9C** (op 0xDF, the `DF 87 02 1C 08` in loc 1B1's hub script) |
| f7088 | `$0100 = 0087` (the reboot) and `$0119 = 00` by **C0:0BA4** (the load's `STZ $19`) |
| f7088..f7127 | **forty frames at `$0119 = 00`** — no ramp at all |
| f7126 | `$011E = 00` by **C0:2868** — the ramp was reached and SKIPPED |
| f7128 | the wipe op arms (`$0350 = 01` C0:409D, `$0139 = 03` C0:40AC), screen still black |
| f7129 | `$7F1522 = 17` (C0:22E3) and `$0119 = 0F` (C0:2306) — masked and lit on the SAME frame |

So the cartridge never shows an unmasked frame. The same pattern at loc 0A5, entered by op 0xDE:
`$011E = 01` by **C0:3B49** at f3336 with `$0100 = 00A5` (C0:3B2A), then `$0119` fades out 0C→00 (C0:3FC3) and
stays 00.

## 3. What the port did

`field.c` read `$011E` at the END of its load window, on the frame the ramp would finish, instead of at the
load. The hub leg's own ramp therefore consumed the flag that the `DF`/`DE` had just set for the NEXT location:

```
locload:  $0119 02 -> 00 $011E=00        loc 1B1's load
loadfade: C0/2848 skipped ($011E) -- $0119=0D vmb=13     <- 1B1 eats 087's flag, on a LIT screen
reboot:   op 0xDF/0xE1 -> $0100=087
locload:  $0119 0D -> 00 $011E=00        loc 087's load: the flag is gone
loadfade: C0/2848 ramp done -- $0119 01->0F              <- 087 ramps the scene up, unmasked
```

and those sixteen ramp frames are the scene the user sees before the curtain. The port's own wipe trace shows
the other half: `$0350 = 01` and `$0139 = 03` for thirteen frames with `$7F1522 = 00`, because the wipe's frame
(C0/21E1 → C0/22D8) does not run while the host is inside the ramp.

## 4. The port

* src/field.c: `loadRampSkip` is latched from `$011E` where the location load completes — C0/2848's `284B` —
  and the ramp branch tests the latch; `2866 STZ $1E` still clears the byte at the tail, on both paths.
* Debug: `CT_LOADCOST=1` now prints `locload: $0119 <before> -> <after> $011E=<v> (C0/0BA2)` at every load and
  `vmb=` in the loadfade line; `CT_WIPETRACE=1` prints `wipemask: ... -> masked / NOT masked` and the wipe op's
  `frz=`/`ldf=` gates.

RESULT, the attract's 087 leg against the cartridge (thumbnail lit-fraction per sample, the leg's samples 10..38):

```
cart  0  0  0  0  0  0 13 26 39 52 65 78 89 89 89
before 0  0  0  0  0 39 77 88  3 16 29 43 56 69 82     <- the flash, then the real fade
after  0  0  0  0  0  3 16 29 43 56 69 82 89 89 89
```

Six skipped ramps become four, and all four now start from a black screen (`$0119=00 vmb=0`), as the ROM's do.

## 5. Open

* **Loc 0A5 and loc 0E6 still flash.** The cartridge skips their ramps too (0A5's `$011E = 01` at f3336), but in
  the port the hub leg's ramp still finishes AFTER its script has run the `DE`, so the flag is cleared before
  the destination's load. The cause is upstream of this fix: the port's load window (`loadHold` black frames)
  ticks the new location's scripts, so a script reaches its ChangeLocation earlier in the leg than the
  cartridge's does. Measured: cartridge loc 1B1 ramps `$0119` 01..0C over f3324-f3335 with `$011E = 00` and only
  then, at f3336, does the script run its `DE`.

## 6. RESOLVED 2026-09-19: the ramp is ABANDONED by the sequence's stack reset

Section 5's cause was wrong. On the cartridge the hub's `DE` also runs INSIDE the ramp (1B1 before 0A5: the ramp
$0119 01..0C over f3324-f3335, C0:284F; `DE` at f3336, $011E = 01 by C0:3B49), and the flag survives only because the
`FF 61` that follows opens a sequence: C0/3FBB fades $0119 0C..00 (C0:3FC3, f3337-f3348), C0/3FD6 `LDX #06FF / TXS`
throws the stack away and C0/3FE0 `JML C3:0000`. C0/2848's loop is gone with the stack, so its tail `2866 STZ $1E /
STZ $0407` never runs, and $011E is cleared only by 0A5's own load (C0:2868, f3499) -- which therefore skips its ramp
and stays at the load's $0119 = 0 until its `FF 92` (f3503). The port let its ramp state (`fade == 2 && loadRamp`)
outlive the sequence's open and ran the tail at the ramp's end (f5372); field.c now drops the ramp, tail and all, where
the sequence request is taken. 0A5 and 0E6 stay black until their curtain (port f5522 / f7471), as the cartridge does.

**2026-09-28: and the fade itself is ported.** The 3FBB fade quoted above (0C..00 over f3337-f3348 in this note's
real-time numbering; f3294-3306 on the logical oracle) was measured here and never run: the port opened the sequence on
the frame after the op, so 0A5 and 0E6 came 13 and 15 frames early. It is the op's own loop now (the first DEC in the op's
frame, the rest at the head of each frame, the target on the frame after the jump's) and the ramp stops on the frame
after the op instead of where the request is taken -- gate_warp_fd_family.md (the 2026-09-28 paragraph under "The host").
