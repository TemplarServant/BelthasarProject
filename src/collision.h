// collision.h -- the CT field tile-collision decode (the M2 subsystem that gates byte-exact player
// position). Ported from bank C0, reverse-engineered + adversarially verified; see
// disasm/notes/collision.md (the contract) and disasm/notes/_collision_specs/*.json (per-routine
// verified pseudocode). All these routines run with Direct Page D = $1D00, so a routine's "$XX" is
// WRAM $7E:1DXX; `ram` is the WRAM image ($7E at offset 0).
#ifndef CT_COLLISION_H
#define CT_COLLISION_H

#include <stdint.h>

// Port of the sub-tile collision SHAPE decode C0/9AD3 (the byte-exact core of the player's
// per-cell collision test, called as JSR $9AA1 prop-fetch -> JSR $9AD3 this -> JSR $9C37 z-gate).
// Inputs (already in `ram`, DP $1D00): the 3 fetched tile-property bytes $1D58 (byte0=shape),
// $1D5A (byte1), $1D5C (byte2), and the tentative next sub-position low bytes $1D66 (X) / $1D68 (Y)
// (= base $1D62/$1D64 + velocity $1D2E/$1D30, computed by the caller). Computes:
//   $1D56 = $1D68 >> 2 (Y proxy, scratch); shape = ($1D58 & 0xFC) >> 2;
//   shape 0 -> walkable tail; shape 1 -> solid tail; shape 2..0x17 -> slope eval via the ROM tables
//   C0/9BF5 (axis/sign+shift) / C0/9C0B (add) / C0/9C21 (eor sign); shape >=0x18 -> one-way/conveyor.
// Writes the one-way flag $1D5E, the shape-2 scratch $1D60, z-plane $1D6A, IGNORE_Z $1D6C, and the
// two sprite-priority bytes $1D6E/$1D6F. The conveyor shape (orig 0x1E) also forces the Y velocity
// accumulator $1D30 to +-0x10 by its current sign. The walk-vs-solid verdict is encoded into $1D6A
// (walk tail: byte2&3; solid tail: (byte2&0x18)>>3); the actual block decision is the subsequent
// z-gate C0/9C37 reading $1D6A/$1D6C against the player plane $0155.
void CtCollisionShapeDecode(uint8_t* ram);

// Pure shape-solidity verdict (the walk-vs-solid decision of C0/9AD3, byte-exact). shapeFull =
// (byte0 & 0xFC) >> 2 (0..0x3F); subX/subY = sub-tile position low bytes (0..255, 256 subpixels per
// 16px tile). Returns 1 = SOLID, 0 = walkable. The engine's real slope-table + one-way shape collision,
// usable without a WRAM image (e.g. the interactive field, in place of the old approximate cSolidShape).
int CtCollisionShapeSolid(int shapeFull, int subX, int subY);

// Port of the Z-plane gate C0/9C37 (carry SET = SOLID). Reads the decode outputs $1D6A (z-plane) /
// $1D6C (IGNORE_Z) and the player plane $0155. Returns 1 = solid (blocked), 0 = walkable.
int CtCollisionZGate(const uint8_t* ram);

// The full per-cell collision DECISION: prop-fetch (C0/9AA1) + shape-decode (C0/9AD3) + z-gate
// (C0/9C37), i.e. the C0/8A9E probe core. Reads the 16-bit tentative position $1D66/$1D67 (X) and
// $1D68/$1D69 (Y), the cell mask $1D1E/$1D1F, the de-interleaved property planes ($7E70C0 TREASURE plane /
// $7E30C0 byte0 / $7E7000 byte1 / $7E7040 byte2) in `ram`, and the player plane $0155. Writes the
// decode outputs $1D58/$1D5A/$1D5C/$1D5E/$1D60/$1D6A/$1D6C/$1D6E/$1D6F (and $1D30 for a conveyor tile)
// exactly as the ROM probe does. Returns 1 = SOLID (off-map or z-solid), 0 = walkable; if `offMapOut`
// is non-NULL it receives 1 when C0/9AA1 rejects the cell (the $7E70C0 TREASURE
// plane's bit7 was clear -- a chest tile; true off-map is the $1D1E/$1D1F mask. field_treasure_chests.md).
int CtCollisionSolidAt(uint8_t* ram, int* offMapOut);

// Override the tile-property fetch used by CtCollisionSolidAt / CtCollisionCommit (and thus the whole
// 8A6D feeler path). `fn(ram, &b0, &b1, &b2)` returns the 3 prop bytes for the tentative tile
// ($1D67=tileX hi, $1D69=tileY hi) and 1 if OFF-MAP. Pass NULL to restore the default (engine WRAM
// planes). The interactive field uses this to drive CtPlayerCollide off its asset shp/zb collision data.
void CtCollisionSetTileFetch(int (*fn)(uint8_t* ram, uint8_t* b0, uint8_t* b1, uint8_t* b2));

// Port of the per-axis collision COMMIT C0/9C5C: tentative pos $1D62+$1D2E / $1D64+$1D30 -> unchecked
// prop-fetch + shape + z-gate; on SOLID zeroes the velocity accumulators $1D2E/$1D2F/$1D30/$1D31, else
// updates the player plane $0155 + OBJ priority $0C00/$0C01[$0197] and the conveyor seed $1D2A/$1D2B.
// (The 9CA1 touch-trigger/door side-state is not modelled.)
void CtCollisionCommit(uint8_t* ram);

// Port of the actor-proximity probe C0/9923: returns 1 = BLOCKED by a solid actor whose box overlaps
// the probe point $1D66/$1D68, 0 = clear. Reads the actor list ($7F2000 count, per-actor $0F00/$1100/
// $1800/$1880/$1B01) and the player/party indices $0197/$0199/$019B.
int CtCollisionActorBlock(uint8_t* ram);

// Port of the player tile-collision dispatch C0/8A6D (run by the player controller when $0120!=0, after
// the d-pad dispatch sets the velocity accumulators $1D2E/$1D30 and before the 9175 edge clamp).
// Dispatches on the sign of $1D2E/$1D30 to a directional feeler/slide handler that probes the tiles
// ahead (CtCollisionSolidAt) + actors (CtCollisionActorBlock) and, on a block, zeroes the matching
// velocity accumulator (the wall-stop); on a clear path it runs CtCollisionCommit. NOTE: the 4 diagonal
// handlers are not yet ported -- a diagonal velocity currently passes through unclamped.
void CtPlayerCollide(uint8_t* ram);

// Port of the NPC actor-vs-actor collision C0/5B95 (called from the move-step C0/5170). Returns 1 =
// BLOCKED (a solid actor sits in the 1-tile box ahead of mover `moverIdx` in its facing $1600), 0 =
// movable. Gated by $1C80[moverIdx]&0x02 (collision-enable; off in the field default -> movable). The
// move-step responds to a block by nudging the heading $EE += 0x30. `ram` = WRAM image ($7E at 0).
int CtCollisionActorMovable(uint8_t* ram, int moverIdx);

extern int g_collPairCommit;   // collision.c: the commit C0/9D54 wrote $0155 and the lead's $0C00/$0C01
#endif
