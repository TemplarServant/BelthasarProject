# THE BATTLE AFTERIMAGE — anim op A4/A5, the recorder CF/ED18, the draw CF/E92C

2026-09-27, the user's list item #3: *"in the skill on zombor, crono is not leaving a 'trail'"* (translated from Portuguese) (the attract's Ice Sword
at loc 087; located by the attract-divergence agent, `build/_divergence/user_2026-09-27.md` §3).

## The ROM

* **A4 rr dd = C1/691B** (A5 = C1/69BA, the same body with `$8F` = 0): for the track's combatant (`$A010` < 4:
  `$A014,X`; 4..7: each member of the `$A018` list; 8 and up: nothing) `$AB67` = rr (the record period), `$AB7D` = dd
  (the draw period), `$AB72` = `$AB88` = 1, the four history points x `$AB93/$ABA9/$ABBF/$ABD5` = `$1D0C` and y
  `$AB9E/$ABB4/$ABCA/$ABE0` = `$1D23`, `$A192` = 3 (on) / 0. Advance 3 (A4) / 1 (A5).
* **CF/ED18, the recorder** (CF/E756, in the IRQ chain while `$A013`, after the stepper CF/EC78 and the links CF/ECC2):
  combatants 10..0 with `$A192`: `DEC $AB72`; at zero reload from `$AB67`, shift the four points back one (their high
  bytes `$AC64..$ACA6` / `$AC6F..$ACB1` too) and put the newest = `$1D0C + $A4A4`, `$1D23 + $A4AF` (signed).
* **CF/E92C, the draw**, from the compose's combatant entry (CF/E909-E91E: `$A013`, and `$A186,X` with X = cmb + $0C =
  `$A192` + cmb): `DEC $AB88`; at zero reload from `$AB7D` and put the combatant's current cells down four more times
  (CF/E99C → CF/EB94, OAM after the body's, so behind it), newest first.

MEASURED (the agent's c7632 state, `--monitor`): A4 at c7634 (`$A192` = 03 by C1:6973), records c7635 / 7640 / 7645 ..
(`$AB93/$AB9E` = 51/65 at c7640 = `$1D0C` + 0, `$1D23` 6F + `$A4AF` F6), copies on c7643, 7646, .. 7679 (OAM #45-60,
CF:EA56).

## The port

`h->ascRole[t]` is the port's `$A014,X` (roles 0-2 the actors, 3 the target `$A017`, 4.. the rest of `$A018`) — the
first cut read WRAM `$A014`, which the port never stages (all FF), and armed nothing. The recorder runs at the start of
`battleAscTick` right after the movers (the ROM's IRQ-chain order): run after the tracks it recorded the arm frame's
point and every later one a frame early (copies a frame's motion off, f9637 5843 / 7000). The draw's countdown runs in
the compose step and is published at the NMI (`trailCmp` → `trailShown`); `battleStanceRender` blits the PC's current
pose at the four points before the body, oldest first. Only the PCs' renderer draws it (no measured enemy afterimage).

After: 13 trail frames, the dash's box (40,0)-(110,130): 8 exact, 4 within 2-32 px, f9646 / c7667 372 px (the copies:
the record phase against the cartridge — the tech's own timing residual, the ~18 frames the port reaches the dash early).
`$A4A4`'s ROM writers (CF/F21D, F37F, F401) are not ported; it is 0 through this dash on the cartridge too.
