/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_MARLEATK_H
#define CT_BATTLE_MARLEATK_H
#include <stdint.h>
#include "battle_anim.h"
#define CT_MARLEATK_NOBJ 16
static const CtAnimObj ctMarleAtkObj[CT_MARLEATK_NOBJ] = {0};
#define CT_MARLEATK_NKF 4
static const CtAnimKeyframe ctMarleAtkKf[CT_MARLEATK_NKF] = {0};
static const uint8_t ctMarleAtkTiles[CT_MARLEATK_NKF][CT_ANIM_NTILE][32] = {0};
typedef struct { const char* pose; uint16_t repF; uint16_t tile; int16_t x; uint8_t y; } CtSparkKf;
#define CT_MARLESPARK_NKF 4
static const CtSparkKf ctMarleSparkKf[CT_MARLESPARK_NKF] = {0};
static const uint8_t ctMarleSparkTiles[CT_MARLESPARK_NKF][4][32] = {0};
typedef struct { int16_t dx; int8_t dy; uint8_t pose; } CtMarleAtkStep;
#define CT_MARLEATK_TRAJ_F0 872
#define CT_MARLEATK_TRAJ_N 79
static const CtMarleAtkStep ctMarleAtkTraj[CT_MARLEATK_TRAJ_N] = {0};
#define CT_MARLEATK_STRIKE_F 939
#endif
