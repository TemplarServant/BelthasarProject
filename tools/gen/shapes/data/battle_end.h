/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_END_H
#define CT_BATTLE_END_H
#include <stdint.h>
typedef struct { uint8_t oidx; int16_t x; uint8_t y; uint16_t tile;
                 uint8_t pal, prio, hf, vf; } CtEndObj;
#define CT_END_NOBJ 16
static const CtEndObj ctBattleEndObj[CT_END_NOBJ] = {0};
typedef struct { const char* name; const char* pose; uint8_t pal, kind;
                 uint8_t objOff, nObj, tileKf; int16_t anchorX; uint8_t anchorY; } CtEndKeyframe;
#define CT_END_NKF 4
static const CtEndKeyframe ctBattleEndKf[CT_END_NKF] = {0};
#define CT_END_NTILE 0x40
static const uint8_t ctBattleEndTiles[CT_END_NKF][CT_END_NTILE][32] = {0};
static const uint16_t ctBattleEndPal[8][16] = {0};
#define CT_END_REGROUP_LEADER_X   124
#define CT_END_REGROUP_LEADER_Y    99
#define CT_END_REGROUP_MARLE_X0   168   
#define CT_END_REGROUP_MARLE_Y0    68
#define CT_END_REGROUP_WALK_FRAMES 40   
#define CT_END_SETTLE_FRAMES 60  
#define CT_END_HOLD_MIN     140  
#endif
