/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_HETAKE_H
#define CT_BATTLE_HETAKE_H
#include <stdint.h>
typedef struct { int8_t dx, dy; uint16_t tile; uint8_t hf, vf; } CtHetakeCell;
typedef struct { int16_t anchorX; uint8_t anchorY, ncell; CtHetakeCell cell[4]; } CtHetakeSlot;
#define CT_HETAKE_NSLOT 3
static const CtHetakeSlot ctHetakeIdle[CT_HETAKE_NSLOT] = {0};
#define CT_HETAKE_NTILE 0x80
static const uint8_t ctHetakeObjTiles[CT_HETAKE_NTILE][32] = {0};
static const uint16_t ctHetakeObjPal[16] = {0};
static const CtHetakeCell ctHetakeAttackKf[4] = {0};
static const uint8_t ctHetakeAttackTiles[8][32] = {0};
#define CT_HETAKE_ATK_N 25
static const int8_t ctHetakeAtkTraj[CT_HETAKE_ATK_N][2] = {0};
typedef struct { int16_t x; uint8_t y; } CtHetakeCursor;
static const CtHetakeCursor ctHetakeCursor[CT_HETAKE_NSLOT] = {0};
#endif
