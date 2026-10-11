/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_SETTLE_H
#define CT_BATTLE_SETTLE_H
#include <stdint.h>
typedef struct { uint8_t oidx; int16_t x; uint8_t y; uint16_t tile;
                 uint8_t pal, prio, hf, vf; } CtSettleObj;
#define CT_SETTLE_NOBJ 40
static const CtSettleObj ctBattleSettleObj[CT_SETTLE_NOBJ] = {0};
typedef struct { const char* name; uint8_t pc, pal, objOff, nObj, tileKf;
                 int16_t anchorX; uint8_t anchorY; uint16_t repN; } CtSettleKeyframe;
#define CT_SETTLE_NKF 10
static const CtSettleKeyframe ctBattleSettleKf[CT_SETTLE_NKF] = {0};
#define CT_SETTLE_NTILE 0x40
static const uint8_t ctBattleSettleTiles[CT_SETTLE_NKF][CT_SETTLE_NTILE][32] = {0};
typedef struct { uint8_t startV, kfIdx; } CtSettleStep;
#define CT_SETTLE_CRONO_NSTEP 5
static const CtSettleStep ctBattleSettleCrono[CT_SETTLE_CRONO_NSTEP] = {0};
#define CT_SETTLE_MARLE_NSTEP 5
static const CtSettleStep ctBattleSettleMarle[CT_SETTLE_MARLE_NSTEP] = {0};
#define CT_SETTLE_CRONO_ENDV 64   
#define CT_SETTLE_MARLE_ENDV 68   
#endif
