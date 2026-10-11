# Z-plane (layered collision) — verified against the real game

The field z-plane gating was previously a guess from a CTViewer source comment (which
CTViewer never executes). It is now **verified against the actual Chrono Trigger code**,
found via the documented z-plane WRAM variable and our disassembler reading the same ROM
the LakeSnes oracle runs.

## WRAM variables (Chrono Compendium "Memory MAP", topic 1764)
- `$7E0155` — **player Z-plane** (the level the party leader is currently on).
- `$7E30C0` / `$7E7000` / `$7E7040` — the active location's tile-property bytes 0/1/2,
  de-interleaved into per-row 0x40-byte planes (raw tile maps at `$7E3000/3040/3080`).
- `$7E0100` — current location id.

## How the routine was found
`tools/disasm_static.py` (a static 65816 disassembler reusing the trace disassembler's
tables) scanned the ROM for accesses to `$0155`. The only coherent cluster is in bank
**C0 at `$9B41`-`$9DFE`** — the field tile-property / collision handler.

## The tile z-plane is `prop_byte2 & 3` (C0/9B41)
```
C0/9B41: LDA $5C        ; $5C = scene tile property byte 2 (the third prop byte)
C0/9B43: AND #$03       ; z_plane = byte2 & 3
C0/9B45: STA $6A        ; $6A = the tile's z-plane            <- matches maps.scene_props
C0/9B47: LDA $5C
C0/9B49: AND #$04       ; byte2 & 0x04 = COLLISION_IGNORE_Z
C0/9B4B: STA $6C        ; $6C = IGNORE_Z flag
...                     ; $6E/$6F = sprite priorities (byte>>2 & 0x10 | 0x20)
```

## The gating rule (C0/9C3B) — carry set = SOLID
```
C0/9C3B: LDA $6A        ; tile z-plane
C0/9C3D: BEQ $9C4F      ; z==0 (transition-solid)        -> SOLID
C0/9C3F: CMP #$03
C0/9C41: BEQ $9C51      ; z==3 (transition-walkable)     -> walkable
C0/9C43: CMP $0155      ; tile z-plane vs player z-plane
C0/9C46: BEQ $9C51      ; same plane                      -> walkable
C0/9C48: LDA $0155
C0/9C4B: CMP #$03
C0/9C4D: BEQ $9C51      ; player on the transition plane  -> walkable
C0/9C4F: SEC            ; otherwise (the OTHER plane)     -> SOLID
C0/9C50: RTS
C0/9C51: CLC            ; walkable
C0/9C52: RTS
```
So a tile is **solid by z-plane** iff `z==0`, or (`z != player_plane` and `z != 3` and
`player_plane != 3`). i.e. the level you are *not* on is solid; transition-solid (0) is
always solid; transition-walkable (3) and your own plane are passable. This combines with
the per-tile collision *shape* (Full/slope/half/…): `solid = shape_solid OR zplane_solid`.

## The plane update (C0/9D54) — when the player's plane changes
```
C0/9D54: LDA $6C        ; IGNORE_Z flag of the tile stepped onto
C0/9D56: BNE $9D6A      ; if IGNORE_Z set, do NOT change the plane
C0/9D58: LDA $6A        ; else player z-plane := tile z-plane
C0/9D5A: STA $0155
```
So the player's plane simply tracks the plane of the tile walked onto, **unless the tile
has IGNORE_Z** (`byte2 & 0x04`). Crossing a `z==3` transition tile therefore puts the
player on plane 3 (everything walkable), and the next step onto a `z==1`/`z==2` tile fixes
the plane to 1 or 2 — exactly the 1→3→2 "flip" the Temporal Flux editor docs describe
("wherever Crono loads becomes his plane; touching a transition reverses the roles").
NOTE: `IGNORE_Z` here gates the *plane update*, not the gating compare — so CTViewer's
guess that IGNORE_Z means "collision applies regardless of plane" is imprecise; the real
effect is "this tile does not change which plane you are on".

## Implementation (`assets/engine.py`)
`maps.scene_props` returns the z-plane + flags; `SceneEngine` tracks `player['z']`
(initialised to the spawn tile's plane), `_zsolid(z)` is the verified gating above,
`_walkable_foot` does `zplane_solid OR shape_solid`, and `move_player` updates the plane
to the tile walked onto (unless IGNORE_Z). Validated on scene 7 (a two-level court): a
plane-1 player is blocked from plane-2 walkable tiles and free on plane-1 ones; the same
tile becomes passable once the player is on plane 2; single-plane scenes (1, 5) are
unaffected (player stays on plane 1, identical to the shape-only behaviour).
