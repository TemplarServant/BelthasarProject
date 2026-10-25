# The lead's plane `$0155` is one byte -- a cutscene does not re-derive it (2026-10-09)

User report (build/ct_debug_023.state, F5 at scene frame 93699, loc 07B, lead (112,200)): "eu fiquei preso".

## 1. What kind of stuck

Loc $07B is a castle dining hall with soldiers at two tables (story $7F:0000 = 0F, Crono alone). Loading the capture
(`Belthasar --scene 123 --loadstate ct_debug_023.state`) and driving each direction with `CT_DRIVEDBG=1`: the movement
gate is OPEN (`gate=1 explore=1 leadBusy=0`, no box, no lock), the lead TURNS (facing 0/1/2/3) but never moves.
The collision engine (C0/881E, CtPlayerControl) returns velocity 0 for every direction, with no actor hit
(`$01EB` = 80). So: a collision/plane wedge, not a VM wait.

The lead stands at `$1802/$1882` = 0704 / 0C7F. Tiles of row 12, columns 6-10, are shape 0x19 with zb 88 (the
table's upper edge, `CT_COLLDUMP=1`): shape 0x19 is solid iff the tentative Y sub-tile byte is < 80 (C0/9B86,
collision.md 3b), so at 0C7F the lead is in the shape-SOLID half. That half's plane is the 9B62 solid tail,
`(b2 & 18) >> 3` = 1 (b2 = 08), and the z-gate C0/9C3B lets a plane-1 lead through (`CMP $0155 / BEQ`). Row 11
(columns 7 and 9) and row 13 are full solid. The VM's own `$0155` in the capture is 01 -- but the host's mirror
`pz` was 0, and fieldPlayerCollide fed THAT into the probe's `$0155`: every neighbour failed C0/9C3B.

## 2. How the lead got there (both sides)

Walking the row from the east the slide C0/8A6D pushes the lead up into the strip and he walks it at Y = 0C7F on
plane 1 -- port and cartridge alike. The soldiers' table is obj15 (`8B 07 0D`, tile 7,13); its fn1 is the meal:
`75 0C / D9 06 8C 06 8C 06 8C / D9 07 8C 06 8C 08 8C / ... C3 CC (choice) ... 77 0C / 04 1A 44 / DA / E3 01`.
The two `D9`s walk Crono on the C0/5CEC programs (field_d9_party_walk.md) to tile 6 then tile 7 of row 12: from
0780 the cartridge's lead runs 0761 0742 0723 0704 06E5 (tile 6) and back to 0704 (tile 7) -- the capture's 0704.

Oracle, logical (`saves/field_re/s07b_table_seat.state`, section 5), `--monitor 7E0155:1 --writer` over the whole
event: no store to `$0155` from the press (f11, `$011F` 0) to control back (f1099); it stays 01. The only
writer seen afterwards is the step commit C0/9D5D (the PC after `9D5A STA $0155`), once the lead walks. With
Left 30 / Right 60 after f1099 the cartridge's lead walks 0704 -> 0524 -> 08E4 at Y = 0C7F.

Loc 002's wake-up (saves/wake.state, `--monitor 7E0155:1` f0-f1400): `$0155` = 01 throughout the scripted rise from
the bed (the vectors written at C0/AB28 / AB42) and after `$011F` goes 1 at f783.

## 3. The ROM's writers of `$0155`

| site | what |
|---|---|
| C0/0C4C `LDA #01 / STA $55` (D = $0100) | the location load's default |
| C0/9DC3 -> 9DFE `STA $0155` | the arrival tile's plane, or 1 if 0 (collision.md 3e) |
| C0/9D54-9D5A `STA $0155` | the step commit, unless the landed tile is IGNORE_Z (`$6C`) |

`ct.py refs 0155` finds the two absolute stores; 0C4C is the DP one (changeloc_arrival_subtile.md). Nothing on a
scripted move's path (op 0x96 -> C0/A810, the D9 programs) writes it, which the oracle confirms above.

## 4. What the port did

field.c kept the plane twice: the scene WRAM's `$0155` (written by ct_vm.c's load, CtSceneArrivalPriorities and
the commit mirror in fieldPlayerCollide) and a host `pz`, which fieldPlayerCollide pushed INTO the probe's
`$0155` on every call. While the script held the lead (`ctlLocked`), field.c re-derived `pz = zb & 3` from the
tile under the scripted lead ("bed z != floor z", from 2026-06, never measured). Under the seated Crono that is
the WALK tail's plane, 0 -- so the first free frame after the meal probed with plane 0 and every step was solid.

## 5. The fix and its grade

* fieldPlayerCollide seeds the probe's `$0155` from the scene's WRAM (the byte the ROM reads); `pz` is a mirror
  (the world map, with no scene, still hands its own in).
* The locked branch no longer re-derives the plane; it mirrors `$0155`.
* `tools/field_plane_cutscene_cmp.py` (check `field-plane-survives-a-cutscene-vs-logical-oracle`): the port
  `--loadwram`s the seed and runs the same drive one frame later (ct-frame-alignment-p2): `$1802 $1882 $0155 $011F`
  every frame f0-f1248, 0 frames differ, control back f1099 on both sides, lead ends 08E4,0C7F on both. With the
  old re-anchor: 97 frames differ, the port's lead never leaves 0704,0C7F.
* The user's own capture: `--loadstate ct_debug_023.state` now walks left (52,199), right (172,199) and up; down
  stays blocked by the table (row 13 is full solid).

The seed: `CT_ORACLE_LOGICAL=1 ct --teleport 123 11 14 0 --from saves/solo_forest.state --frames 300` with
`PEEK_POKE` = the capture's story page ($7F:0000-01FF, saves/field_re/s07b_flags_ct_debug_023.bin), then
`--peek ... --btnscript "0:5,10:35,40:68,20:1,0:3"` with `PEEK_SAVE` (Crono at 0780,0C7F facing down, plane 1).

## Open questions

* The port's NPC obj12 (a pacing servant, cadence 2) was not graded here; in one oracle drive it blocked the lead
  for 8 frames going up past (0684,0C4F) -- a different moment from the capture, not compared against the port.
* Other host-side `pz = zb & 3` sites remain (the `--scene` harness spawn, the door arrival before C0/9DC3, the
  old-snapshot fallback of --loadstate); they no longer reach the probe while a scene exists, since it reads
  `$0155`, but they still feed the `pz` mirror's printouts until the first collision call.
