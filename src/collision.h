// collision.h -- field tile collision and the player's collision handlers (bank C0). See
// disasm/notes/collision.md and disasm/notes/_collision_specs/*.json (per-routine pseudocode).
// All these routines run with DP = $1D00, so a routine's "$XX" is WRAM $7E:1DXX; `ram` is the WRAM image
// ($7E at offset 0).
#ifndef CT_COLLISION_H
#define CT_COLLISION_H

#include <stdint.h>

// C0/9AD3 sub-tile shape decode (called as 9AA1 prop fetch -> 9AD3 -> 9C37 z-gate).
// Inputs: the property bytes $1D58 (byte0 = shape), $1D5A, $1D5C and the tentative sub-position $1D66 (X) /
// $1D68 (Y). shape = ($1D58 & 0xFC) >> 2: 0 walkable, 1 solid, 2..0x17 slopes (tables C0/9BF5, 9C0B, 9C21),
// >= 0x18 one-way/conveyor. Writes $1D56, $1D5E (one-way), $1D60, $1D6A (z-plane), $1D6C (IGNORE_Z) and the
// priority bytes $1D6E/$1D6F; the conveyor (shape 0x1E) also forces $1D30 to +-0x10 by its sign.
// The block decision itself is the z-gate C0/9C37.
void CtCollisionShapeDecode(uint8_t* ram);

// The walk-vs-solid decision of C0/9AD3 without a WRAM image. shapeFull = (byte0 & 0xFC) >> 2;
// subX/subY = sub-tile position (0..255, 256 subpixels per 16 px tile). Returns 1 = solid, 0 = walkable.
int CtCollisionShapeSolid(int shapeFull, int subX, int subY);

// C0/9C37 z-plane gate: reads $1D6A, $1D6C and the player plane $0155. Returns 1 = solid, 0 = walkable.
int CtCollisionZGate(const uint8_t* ram);

// The C0/8A9E probe core: prop fetch (C0/9AA1) + shape decode (9AD3) + z-gate (9C37) at the tentative
// position $1D66/$1D67, $1D68/$1D69, masked by $1D1E/$1D1F. Reads the planes $7E70C0 (treasure), $7E30C0,
// $7E7000, $7E7040 and writes the decode outputs as the ROM probe does. Returns 1 = solid, 0 = walkable.
// `offMapOut` (if non-NULL) receives 1 when C0/9AA1 rejects the cell: a chest tile ($7E70C0 bit 7 clear;
// field_treasure_chests.md).
int CtCollisionSolidAt(uint8_t* ram, int* offMapOut);

// Overrides the tile-property fetch used by CtCollisionSolidAt / CtCollisionCommit (the whole 8A6D path).
// `fn(ram, &b0, &b1, &b2)` returns the three bytes for the tile at $1D67/$1D69 and 1 if off-map.
// NULL restores the default (the WRAM planes). The interactive field uses it for its asset collision data.
void CtCollisionSetTileFetch(int (*fn)(uint8_t* ram, uint8_t* b0, uint8_t* b1, uint8_t* b2));

// C0/9C5C per-axis commit: fetch, shape and z-gate at $1D62+$1D2E / $1D64+$1D30. Solid zeroes the
// velocity $1D2E-$1D31; otherwise it latches a door tile and updates the plane $0155, the OBJ priority
// $0C00/$0C01[$0197] and the conveyor seed $1D2A/$1D2B. The 9CA1 touch-trigger state ($0112-$0116,
// $0117 bit 7) is not modelled here; the host's exit records stand in for it.
void CtCollisionCommit(uint8_t* ram);

// C0/9923 actor-proximity probe: returns 1 = blocked by a solid actor whose box holds the probe point
// $1D66/$1D68, 0 = clear. Reads $7F2000 (actor count), $0F00/$1100/$1800/$1880/$1B01 and the party
// indices $0197/$0199/$019B; sets the touch latch $01EB.
int CtCollisionActorBlock(uint8_t* ram);

// C0/8A6D player tile collision (run when $0120 != 0, after the d-pad dispatch and before the 9175 edge
// clamp). Picks one of eight directional handlers from the signs of $1D2E/$1D30; each probes the tiles
// (CtCollisionSolidAt) and actors (CtCollisionActorBlock) ahead, zeroes the blocked axis and commits.
void CtPlayerCollide(uint8_t* ram);

// C0/5B95 NPC actor-vs-actor test (from the move step C0/5170). Returns 1 = blocked (a solid actor in
// the one-tile box ahead of `moverIdx` in its facing $1600), 0 = movable. Gated by $1C80[moverIdx] & 0x02.
// The move step answers a block by turning the heading ($EE += 0x30).
int CtCollisionActorMovable(uint8_t* ram, int moverIdx);

extern int g_collPairCommit;   // collision.c: the commit C0/9D54 wrote $0155 and the lead's $0C00/$0C01
#endif
