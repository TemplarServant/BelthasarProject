# The LADDER climb — collision shape 30 → `$1D34` bit2 → player anim mode `$1B`

Derived 2026-08-29 from the ROM (`tools/dis.py`) and MEASURED on the oracle. User report:
*"in scene 112, when moving on the ladder, crono should change animation"*.

## 1. Shape 30 is the LADDER, and the shape decode announces it

`C0/9AD3` (the tile shape decode, ported as `CtCollisionShapeDecode`) dispatches shapes ≥ 0x18
(`shape-2 ≥ 0x16`) through `C0/9B86`. The last arm is `shape-2 == 0x1C`, i.e. **raw shape 30**:

```
9BDD  LDA #04 / STA $5E          <- the tile-MODE byte
9BE1  LDA $30 / BEQ 9BF2
9BE5  BMI  9BEE
9BE7  LDA #10 / STA $30          <- clamp this frame's Y step to +$10
9BEE  LDA #F0 / STA $30          <-                        or -$10
9BF2  BRL  ->9B41                <- and take the WALKABLE tail
```

`$5E` (DP = `$1D00`, so `$1D5E`) is set **only here** in the whole decode; every other shape leaves the
`9AD3` head's `STZ $5E` standing. `assets/maps.py`'s `scene_collision` already named shape 30 "Ladder".

Scene 112 has 7 of them: **(28,9)-(28,11)** and **(8,19)-(8,22)**. 84 of the 512 scenes carry at least one.

## 2. Two writers publish `$5E` into `$1D34`

| ROM | when | code |
|---|---|---|
| `C0/9D6A` (inside the `C0/9C5C` movement COMMIT) | every accepted step | `LDA $5E / STA $34` (DP `$1D00`) |
| `C0/9E20` (inside `C0/9DDE`, the LOCATION-ARRIVAL evaluation) | every location load | `LDA $5E / STA $1D34` |

`C0/9DDE` is the arrival routine: `LDA $0102` → `C0/9AC8` (fetch that tile's props) → `$66 = $80`,
`$68 = $FF` (centre-X / foot-Y sub-tile probe) → `JSR C0/9AD3` → then `$0155` (plane), the party
priorities `$0C00/$0C01` at `$0197/$0199/$019B`, and finally `$1D34 = $5E`.

`C0/9D6A` also ORs in **bit 3** for a CONVEYOR tile (`9D6E`: `LDA $5A / AND #0C` selects a speed
$08/$10/$20 and `9D8C LDA #08 / TSB $34`). `$1D34` is additionally spilled into the party PATH-HISTORY
ring at `$7F0F00,X` by `C0/9E7C`.

## 3. `C0/9E84`'s FIRST arm reads it, ahead of the direction table

```
9E84  STZ $E5
9E86  LDY $6D
9E88  LDA $1D34 / BEQ ->9EAF        <- plain tile: straight to the $9FF7 direction table
9E8D  BIT #04   / BNE ->9E95
9E91  INC $E5   / BRA ->9EAF        <- CONVEYOR: $E5=1, then the table
9E95  LDA $1D2D / BEQ ->9EA4        <- LADDER: any vertical intent?
9E9A  (fold |A|) / CMP #18
9EA1  BRL ->A254                    <- `82` = BRL, UNCONDITIONAL: the CMP has no consumer
9EA4  $1681,Y = 0 / $1601,Y = 0 / RTS
```

* `A254` is one of the seven identical 15-byte tails, with **k = `$1B`** → `$1680,Y = $1B` (see
  `player_anim_9e84.md` for the tail family and the CMP guard).
* `9EA4` zeroes only phase + timer and **leaves `$1680` alone**, so the anim ticker re-advances to
  step 1 every frame: standing on the rungs PINS the pose at step 1.
* `$E5` is read by the RUN tail `C0/A18C` — it makes a blocked run play RUN instead of the
  run-into-wall head-shake.

## 4. Oracle measurement (this is the spec)

`ct.exe --teleport 112 8 23 0 --from fresh439.state --out lad112.state`, then
`--monitor 1D34:1,1682:1,1683:1,1603:1,0F03:1,1882:2 --btnscript "10:90"` (player idx 2):

| frames | `$1D34` | `$1680,X` | `$0F01,X` (drawn frame) | dwell |
|---|---|---|---|---|
| 1-15 (approach) | 00 | 01 walk | 17 → 18 → 19 | 7 |
| **16-79 (on the ladder)** | **04** | **1B** | **69 → 6A → 6B → 68** | **10** |
| 80+ (past the top) | 00 | 01 walk | 17 → 18 | 7 |

Release the d-pad mid-ladder (`--btnscript "10:25,00:20"`): `$1D2C/$1D2D` go `00F0 → 0000` on the
release frame, `$1680` HOLDS `1B` and the phase pins at 1 — the frozen climb pose.

The baked sprite-0 anim table already carries it: **anim `0x1B` = 4 steps, frames 68/69/6A/6B, all four
facings identical, dwell 10** (`ct_assets.dat` CT_ASSET_SPRITEANIM id 0) — matching the oracle exactly.

## 5. What the port was missing

`CtPlayerAnimSelect` (src/move.c) has carried the whole of `C0/9E84` — including this arm — since it
was ported, and `--sweepanim` grades it. But `ct_field`'s **free-roam** player never calls it: field.c
picks the lead's anim with its own selector, which had walk / run / head-shake / idle-blink and **no
`$1D34` arm at all**, so the ROM byte was computed by the collision commit and then ignored.

Fixed in field.c by transcribing the 9E88-9EA3 arm ahead of that selector (it needs no ROM data, so the
dat-only field stays ROM-free), plus `fieldArrivalTileMode` = `C0/9DDE`'s `$1D34` store so the flag does
not survive a door. Pinned: `field-ladder-climb-anim`, `field-ladder-arrival-tile-mode`.

**Known gap (asset, not port):** `fieldTileFetch` reconstructs prop1 from the packed collision plane and
carries only bits 4 and 6, so `9D6E`'s conveyor selector `$5A & 0x0C` is always 0 and `$1D34` can never
get its **bit 3**. The `$E5` arm is therefore unreachable in this host. Flagged, not faked.
