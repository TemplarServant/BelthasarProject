# A field object's OBJ priority is PER UNIT — the anim drain's attr fill

User report 2026-10-09: *"erro de prioridade dos soldados no F5"* — the port's F5 capture
`build/ct_debug_022.state` (log: `scene frame 89587 loc 07B lead (194,272)`), the 600 A.D. Guardia
Castle soldiers' mess. The two soldiers seated with their backs to the camera at the right-hand table
were drawn whole, over the chair backs. On the cartridge only their helmets show above the chairs.

## 1. The objects

`python tools/ct.py script 123 --obj 17` (obj18 is the same at tile (15,16)):
```
0x04EE: 82 2B      LOADPCsprite spr=50
0x04F0: 8B 0D 10   SETCOORD tile(13,16)
0x04F3: 8E 12      SETPRIO/group 12
0x04F5: 0F         FACE up
```
Op 0x8E's FIXED arm (C0/4CB7-4CC8, field_sprite_prio_and_player_gate.md sec. 1): `$0C01 = b & 30` = **10**
(OBJ priority 1, the BOTTOM byte), `$0C00 = (b << 4) & 30` = **20** (OBJ priority 2, the TOP byte); group
`b & 0C` = 0. Both the port's WRAM (capture, `CT_DUMPWRAM_AT=1`) and the oracle's hold obj17/obj18
`$0C00/$0C01 = 20/10`, `$0F80 = 12` — the stored pair was right; the render was not.

## 2. The ROM: which byte each unit takes (bank C0, the anim drain's frame build)

The OAM attr of each unit (one 16x16 OBJ = one OBJ tile t) is `$0F81,X | $0D01,X | <priority byte>`, and
the priority byte is chosen PER UNIT:

| class ($1201 & 3 / the sprite's size) | routines | rule |
|---|---|---|
| 0 (4 units) | C0/CD47-CDBB (the `$7F4800` record), C0/CE77.. (`$7F4BC0`) | per unit: `LDA $7F4804,X` (the unit's Y offset, frame record +0x21+2t, stored at CCDA) / `CMP #E8` / `BCC` → `ORA $0C00,Y`; else `ORA $0C01,Y`. So an offset of **-24..-1 takes the BOTTOM byte**, every other offset (-128..-25 **and every non-negative one**) the TOP byte. |
| 1 (8 units) | C0/D0F0-D114, C0/D257-D27B, C0/D4C6.. | units 0-3 `ORA $0C00,Y`, units 4-7 `ORA $0C01,Y` |
| 2 (12 units) | C0/DA23-DA5C, C0/DCE2.., C0/DEE7-DF20, C0/E0E2.. | units 0-3 `$0C00`, units 4-11 `$0C01` |
| 3 (24 units) | C0/E4D1-E52F | `LDA #22` into all 24 attrs — a fixed OBJ priority 2; neither byte is read |

(Found with a byte scan of bank C0 for `19 00 0C` / `19 01 0C` — `ORA $0C00,Y` / `ORA $0C01,Y` — and
`9F 7F 4C 7F` for class 3's last attr; disassembled with `tools/dis.py <addr> <len> 1 0`.)

Then mode 1's slot order (snes/ppu.c layersPerMode[1]: OBJ3, BG1.1, BG2.1, OBJ2, BG1.0, BG2.0, OBJ1, BG3.1,
OBJ0, BG3.0) puts an OBJ priority-1 pixel behind BG1/BG2 at either priority.

## 3. The oracle

Fixture: `saves/field_re/m81_frog_lake.state` with its `$7F:0000-01FF` page replaced by the capture's
(storyline 0F), then `CT_ORACLE_LOGICAL=1 ct.exe --teleport 123 12 16 1 --from <that> --frames 300 --out
saves/field_re/loc07B_soldiers_seated.state` (`scene=LOADED player=PLACED, 23 active, 22 placed`).
`--peek ... --btnscript "0:30"`, the `.oam` sidecar:

```
#36 184,111 tile 60 attr 21 (prio 2)   #37 168,111 62 21    obj18, units at yoff -32
#38 184,127 tile 64 attr 11 (prio 1)   #39 168,127 66 11    obj18, units at yoff -16
#40 152,111 tile 68 attr 21 (prio 2)   #41 136,111 6A 21    obj17
#42 152,127 tile 6C attr 11 (prio 1)   #43 136,127 6E 11    obj17
#28 128,143 tile 08 attr 3A (prio 3)   ... Crono (obj1, $0C00/$0C01 = 30/20): his unit 0 at yoff 0 takes
                                         the TOP byte (3) -- the non-negative arm of C0/CD4F
```
$0A80 (the anchor) = 0x8F = 143 for obj17/18. The picture: only the helmets over the chair backs.

## 4. What the port did, and the fix

`src/field.c drawSprite` knew two things: a boolean "front" (priority 3) per half, split by a PIXEL ROW 16
above the anchor; and, separately, the whole-object OCCLUDER (both nibbles of the op-0x8E byte < 2) stencilled
back to the map. An object with only ONE half below priority 2 — `8E 12` — fell in neither: its priority-1
units were drawn as priority 2, over the chairs (BG priority 0, so the foreground pass did not cover them).

Fix (one mechanism, every scene):
* `src/scene.c CtComposeSpriteHalves`: the compose also returns, per opaque pixel, its unit's byte — the
  table in sec. 2 (class 0 by `oy >= 0xE8`, classes 1/2 by index, class 3 fixed 2).
* `drawSprite` takes the two bytes' priorities (`$0C00 >> 4`, `$0C01 >> 4`); per pixel `p` = its unit's:
  `p < 2` and BG1/BG2 not windowed off → stencilled back to the map (the occluder's own rule, now per unit);
  otherwise plotted, and `front` = `p == 3` for the pixel that wins (no longer a set-only latch, so a priority-2
  unit drawn over a priority-3 one goes back behind the foreground).
* The lead and every NPC pass the map as the stencil source; the whole-object occluder list (`sten[i]`) now
  reads the stored pair (both bytes < 0x20, class ≠ 3) instead of the op-0x8E byte, as the drain does.

## 5. Validation

* `tools/frame_region_cmp.py saves/field_re/loc07B_soldiers_seated.state 2 2 0 0 256 224 -- --assets
  ct_assets.dat --scene 123 --loadwram saves/field_re/loc07B_soldiers_seated.lakewram` →
  **57344 / 57344 identical**; main (1b28fed) differs in 388 px, all in the two soldiers' lower units
  (x 144-191, y 127-142). Check `field-obj-unit-priority-seated-soldiers-vs-oracle` (two mutations: the
  per-unit stencil off; every class-0 unit on the top byte).
* The user's capture (`Belthasar --scene 123 --loadstate ct_debug_022.state`): the soldiers' region against
  the oracle frame is identical (the only differing pixels are Crono, who stands 6 px apart in the two runs).
* `regress.py --quick`: 564 pass, 0 fail (5 skips = missing fixtures, unrelated).

## Open questions

* The stencil restores the composed map (`f.base`) — it assumes BG1 or BG2 is opaque at that pixel. Where both
  are transparent a priority-1 OBJ pixel should show (and a priority-0 one loses only to BG3 priority 1). The
  same simplification the whole-object occluder always had; not met in this scene.
* The class-0 rule changes the half of a unit whose offset is not a multiple of 16 from the anchor
  (-24..-17 now bottom; -28..-25 wholly top; non-negative offsets top). Nothing in the suite moved; no
  oracle scene with such a unit and two different bytes has been graded yet.
* Class 3's fixed `#22` was found at C0/E4D1 only; no class-3 field object was looked at on the oracle.
