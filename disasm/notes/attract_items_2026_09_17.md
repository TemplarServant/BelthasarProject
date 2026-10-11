# Four attract items, measured (2026-09-17)

The user's four reports, each taken to a measurement before anything was changed. One was a real defect and is
fixed; two are the same clock residue seen from two angles; one is an already-open raster race, now measured
much more precisely and with one attempted fix ruled out.

## 1. "you can see the scene before the curtain appears" — FIXED

A real defect, and the only one of the four. See **disasm/notes/load_ramp_gate.md**: C0/2848 reads `$011E` at
its ENTRY (`JSR C0/286C / BNE ->2866`, and C0/286C ends `LDA $1E / RTS`), while the port read it at the end of
its load window, so the attract hub's own ramp ate the flag its `DF 87` had set for loc 087. The Zombor leg then
ramped its scene up over sixteen frames where the cartridge holds `$0119 = 00` for forty and lights the screen
on the wipe's own first frame, masked. Check `load-ramp-gate-011e-at-entry`, two mutations.

Loc 0A5 and loc 0E6 still show it, for a reason upstream of that fix (see load_ramp_gate.md section 5).

## 2. "in the Zombor scene, the ice animation is not appearing" — it IS drawn, late and weaker

The 087 battle runs ONE action, tech `$3A` (`--monitor 7E9877:1,7EAE93:1,7EA013:1,7EA014:4`: `$AE93 = 3A` at
cartridge f7449 by C1:BBB1, dispatched at f7453 with `$9877 = 3A`, `$A013` 0→1 and `$A014 = 00 02 02 09`;
`CD/45A6 + 0x3A*7` → selector `3A` → `CD/5EF0[0x3A*2]` → script **CE/330D**, header `D0 FC | C0 00` and nine
tracks 3327 3384 33AE 33C4 33E0 3416 3438 345A 347D).

Counting near-white pixels (all three channels > 200) over the leg's thumbnails, both sides have **two** white
bursts and the second is the ice:

| | first burst | second burst (the ice) |
|---|---|---|
| cartridge | samples 28..208, peak 1203 | samples **323..434, peak 2187** |
| port | samples 33..233, peak 1206 | samples **345..453, peak 1411** |

So the ice is there, **22 samples (44 frames) late** and with a **peak 65% of the cartridge's**. The cartridge's
f7736 is matched by the port's f9806 at a thumbnail difference of 5.1 (against 83.2 at the same index).

The machine behind it drifts in the same window: `tools/fxobj_cmp.py zombor 400` (it passes at 230) reports
`66/72 identical, matrix sequence differs, oracle frames held 30`, first difference obj0's `$A1B0` at oracle
f791 / port f525. Thirty frames the cartridge holds and the port does not.

## 3. "Flea's sprite when taking the attack is wrong" — the sprite is right, the leg is behind

Loc 0AD, the first demo battle (tech `$42` → CE/3C9A). At the survey's own index the two frames look nothing
alike (cartridge f1990 vs port f4058: the cartridge has Flea as a solid WHITE hurt silhouette on a near-black
field, the port has her in her own colours on a bright purple one). Searched over the visit instead, the
cartridge's f1990 matches the port's **f4066** — four samples, eight frames, later — and there the port draws the
**same white silhouette**, 45145 / 57344 pixels identical over the whole frame; the residue is the starfield.

The same leg's other samples: cartridge f1970 → port f4052 (d = +7 samples, 164.2 → 14.0), f1984 → f4074
(d = +11, 143.3 → 7.7), f2000 → f4076 (d = +4, 4.2 → 2.2). The hurt flash is not the defect; the action's clock is.

## 4. "the colours of the frog's attack are a little different" — the known ramp race, measured

Loc 07D, tech `$28` = the Frog Squash (`CD/45A6 + 0x28*7` → selector `28` → CE/2A13). Already open as
"the ramp race" in battle_effect_orbit.md section 9. Measured properly this time with `--hookpc CD2D28` from a
`PEEK_SAVE` state at attract f5000: the rotation is **95 visits a frame** (the loop's own `CPX #017C` / 4)
spanning **vPos 128 through 137**, every frame of the frog — so the HDMA of scanlines 1..128 has already read
the table when the sweep starts.

**Tried and reverted**: snapshotting `$BB05..$BC84` before the rotation and reading it for rows below scanline
128 changes nothing measurable — the graded upper-body box (60,60)-(190,128), port f7081 against cartridge
f5042, is **446 / 8840 identical either way**. The per-line COLDATA does vary down the frame
(`CT_BATTLE_FXLINES=534`: fix = (0,18,0) at line 1, (24,24,0) at 100, (16,0,0) at 128, (20,0,0) at 140), so the
records do reach those lines; the split is not what the upper body is missing. Next pass: compare the port's
per-line COLDATA against the cartridge's (`ct.exe --hdma` on a state at the frog) before touching the compositor
again.

## What items 2 and 3 have in common

Both are the port's battle clock, not its drawing: an action's frames drift from the cartridge's (8 frames in
0AD, 44 in 087) and the survey's index-aligned pairing then shows a picture that is simply not there yet. The
existing open items name the same thing from the other side — the CPU-clock class of the animation drain, the
087 pre-roll, the thirty held frames above. A survey median over a leg with a fast action measures that drift,
not the renderer; measure a best-match frame before believing a defect.
