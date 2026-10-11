/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_ANIM_H
#define CT_BATTLE_ANIM_H
#include <stdint.h>
typedef struct { uint8_t oidx; int16_t x; uint8_t y; uint16_t tile;
                 uint8_t pal, prio, hf, vf; } CtAnimObj;
#define CT_ANIM_NOBJ 60
static const CtAnimObj ctBattleAnimObj[CT_ANIM_NOBJ] = {0};
typedef struct { const char* name; const char* pose; uint8_t pal;
                 uint16_t startF, endF; uint8_t objOff, nObj, tileKf;
                 int16_t anchorX; uint8_t anchorY; } CtAnimKeyframe;
#define CT_ANIM_NKF 9
static const CtAnimKeyframe ctBattleAnimKf[CT_ANIM_NKF] = {0};
#define CT_ANIM_NTILE 0xA0
static const uint8_t ctBattleAnimTiles[CT_ANIM_NKF][CT_ANIM_NTILE][32] = {0};
static const uint16_t ctBattleAnimPal[8][16] = {0};
typedef struct { int16_t dx; int8_t dy; int8_t poseId; } CtAnimStep;
#define CT_CRONO_ATK_F0 440
#define CT_CRONO_ATK_N 91
static const CtAnimStep ctCronoAttackTraj[CT_CRONO_ATK_N] = {0};
typedef struct { int16_t dx; int8_t dy; uint8_t arm; uint8_t footDY; } CtGatoIdleStep;
#define CT_GATO_IDLE_F0 384
#define CT_GATO_IDLE_N 121
static const CtGatoIdleStep ctGatoIdleTraj[CT_GATO_IDLE_N] = {0};
typedef struct { int16_t dx; int8_t dy; uint8_t state; } CtCronoBobStep;
#define CT_CRONO_BOB_F0 400
#define CT_CRONO_BOB_N 105
static const CtCronoBobStep ctCronoIdleBob[CT_CRONO_BOB_N] = {0};
#endif
