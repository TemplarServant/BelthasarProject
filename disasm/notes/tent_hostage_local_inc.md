# The tent of horrors' hostage game: op 0x72, the sixteen-bit INC the live VM never had

The user, 2026-10-08: "the tent of horrors minigame for 80 silver points is bugged when you lose: marle as the hostage
goes down past the fire, and the sparkle that makes her rise when pressed is not appearing" (the hostage sinks past the fire; the
sparkle that lifts her when pressed never shows).

## 1. The script (scene 434, script 71, loc 1B2)

Bekkler (obj11) fn1, choice 2 (`1A 02 93` at 0595): `D2 0n 03 / 75 1A` for PCs 1-6 (someone besides Crono), local[13]
>= 80 (the points, $7F021A), `5F 50 0D` (pay), text 81, the cage set up (obj15-17 fn3, obj12-14 shown), text 82, then
the party-slot calls (C0/61E9: the object hosting `$0197 + slot`) `05 04 39` (slot 2: fn9, hide) and `05 02 37` -- the
hostage, party slot 1 (Marle here), runs the PCs' shared fn7:

    00C3  8E 33 / AA 09 / 96 05 00 / 8D 80 05 00 00 / 89 08 / AD 30      up to the rope, speed 8, wait
    00D3  13 12 20 01 01 49   IF local[12] != 0120 (16-bit, op 0x13) ... else -> 0121
    00D9  9C 40 01            one vector step down
    00DC  72 12               local[12] += 1                                   <- C0/6C85
    00DE  13 12 B2 00 00 05   IF local[12] == 00B2: `E8 0B` (sound) + `7C 10` SHOW obj8 (the blue switch)
    00E8  12 19 07 00 31      IF local[19] == 7 (the three monsters back in the cage): the win (text 67/83, prize)
    011D  AD 01 / 11 4D       wait 1, back to 00D3
    0121  E8 94 / 88 ...      the flames: sound, the hostage's palette burnt, `EA 25`, wait, CHANGELOC 05 (the fair)

obj8 (the switch, sprite 120 at 40,148) fn1 = fn2 = 01C3: `E8 0B / 05 02 28` (the hostage's fn8: `9C C0 70` up 0x70,
`60 70 00 12` local[12] -= 0070) `/ 91` (hidden again). Crono reaches it at the left wall (x 40) facing up.

## 2. The ROM

C0/6C85 (op 0x72):

    6C85 INY / TYX / LDA $7F2001,X / REP #20 / AND #00FF / ASL A / TAX
    6C92 LDA $7F0200,X / INC A / STA $7F0200,X      ; SIXTEEN-bit: the SEP #20 comes after (6C9B)
    6C9B SEP #20 / INY / TYX / SEC / RTS

Its siblings C0/6C69 (0x71), 6CA1 (0x73), 6CBD (0x75), 6CD6 (0x76), 6CF0 (0x77) share the skeleton; 0x71/0x73/0x75/0x77
put the SEP #20 before the access (8-bit), 0x72/0x76 after it (16-bit). move.c's runner already had the six right.

## 3. The port's defect and fix

ct_vm.c's step (applyEffect) and init pass (runFunc) had 8-bit paraphrases of 0x71/0x73/0x75/0x77 and no arm for 0x72 or
0x76: the two-byte op was stepped over by its length. The counter stayed 0, so 00DE never showed the switch and 00D3
never took the flames; the descent loop ran for good (the user's "below the fire").

Fix: the body moved out of move.c's CtFieldRunnerOne into `CtVmLocalIncFamily` (move.c, the body --sweepops grades), and
both ct_vm.c sites call it, as they call CtVmLocalArithFamily / CtVmVarFamily for their neighbours.

## 4. Measured (the rule's cartridge, saves/field_re/s1b2_hostage_lake.state, `--monitor`)

| event | lose | switch (Left 96, Up 8, A f1119) |
|---|---|---|
| counter local[12] = 1 | f652 | f652 |
| local[12] = 00B2, obj8 shown ($1B10 = 80) | f1009 | f1009 |
| obj8 fn1 (the press) | -- | f1116 |
| local[12] = 0120 (the flames) | f1230 | f1569 |
| loc 005 | f1408 | f1747 |

The port after the fix: every one on the same frame, and the counter, Crono's and Marle's X/Y, obj8's IP and $1B10
identical change by change up to the location change (tools/tent_hostage_cmp.py, check
field-tent-hostage-80pt-counter-switch-flames-vs-rule-cartridge, 18/18 regions).

Not graded, for the record:
- the $1180 mirror: the cartridge shows a called function's first byte (00C3, 014F) on the frame the call installs it,
  the port from its first visit; and `EA 25` parks the cartridge's main loop inside the song upload with the IP in a
  register (no 0145 in $1180), the port shows 0145 for those frames -- the IP after it lands one frame apart (f1295 vs
  f1296) with nothing visible moving;
- the fair's load after the CHANGELOC: the cartridge's NMI-off load lands the fair's objects at +71 under a black screen,
  the port's in one frame, and the fade-in starts 2 frames early on the port (f1500 vs f1502) -- the location-load clock
  (memory ct-load-clock-floor-rule), open on its own.

## 5. The hostage's pose after the switch (2026-10-09)

The user, F5 build/ct_debug_021.state: "when pressing the sparkle for the hostage (marle) to rise, she is changing position
(ending up with the wrong position/sprite) ... she changes a little after rising". In the capture the hostage reads
`$1684 = 00 00`, `$1784 = 00 00` (anim 0, auto: the standing sprite) with fn7's loop still running (`$1184` = 00D9 / 011D,
local[12] = 0072 counting up) -- on the cartridge her fn7's `AA 09` holds 09 / 01 for the whole game.

Who: ct_vm.c's RETURN arm carried a reclaim (op 0xAE's tail, C0/2E8F -> 2E74: `$1680 = $1780 = $1601 = $1681 =
$7F0B01 = 0`) for a PC (move-mode 0-2) on a looping animeset under an open control -- the port's stand-in for C0/3002's
anim reset, which the ROM runs only when the object's script reaches its 0xB0/0xAF. It fired on EVERY RETURN. fn8 (the
switch's `05 02 28`, priority 2: `9C C0 70 / 60 70 00 12 / 00`) RETURNs over fn7 (priority 3), and the ROM's RETURN

    5F74 LDX $6D / LDA $1C00,X / CMP #07 / BEQ ->5FB3        ; the idle level: TYX / CLC / RTS
    5F7D ... STA $7F0580,X (the level's slot cleared) / INC $1C00,X until a saved ip / SEC / RTS

touches nothing else: fn7 goes on at 00D9 with her pose. Fix: the reclaim runs only when the RETURN leaves the object idle
(no saved slot, or the idle level's own RETURN -- the port's followers park there instead of on a B0) or resumes on a
0xB0/0xAF.

Why the check missed it: in the switch drive the monsters reach row 10 at f1124 and obj10 runs their fn7 (`E3 00`, the
catch), so fn8's RETURN (f1229) came with $011F = 0. The user had pushed the monsters back. The new escape drive does the
same: A on obj13 / obj12 / obj14 from f780 (their fn1 `02 1E 35` runs the monster's fn5, `92 C0 0B` back up), the switch
touched at f1097, the catch at f1260, fn8's RETURN at f1215 under $011F = 1. Measured on the rule's cartridge:

| drive | catch ($011F = 0) | fn8 RETURN | `$1684` after f600 |
|---|---|---|---|
| switch | f1124 | f1229 | 09 00 always |
| escape | f1260 | f1215 | 09 00 always (port before the fix: 00 00 from f1215) |

tools/tent_hostage_cmp.py now grades `$1684`, `$1784` and `$011F` in all three drives (35/35 regions).

Not graded, for the record: Marle's sprite is drawn one 1-px step later on the cartridge than on the port (two frames at
each pixel crossing, before and after the press alike, RAM identical) -- the drain's draw latency, not this defect.
