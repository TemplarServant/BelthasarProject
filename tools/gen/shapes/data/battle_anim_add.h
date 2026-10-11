/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_ANIM_ADD_H
#define CT_BATTLE_ANIM_ADD_H
#include <stdint.h>
#include "battle_anim.h"
typedef struct { const char* pose; uint8_t pal; uint16_t repF; uint8_t objOff, nObj, tileKf;
                 int16_t anchorX; uint8_t anchorY; int16_t a, b; } CtAnimAddKf;
#define CTCRONOIDLEADD_NOBJ 12
static const CtAnimObj ctCronoIdleAddObj[CTCRONOIDLEADD_NOBJ] = {0};
#define CTCRONOIDLEADD_NKF 3
static const CtAnimAddKf ctCronoIdleAddKf[CTCRONOIDLEADD_NKF] = {0};
static const uint8_t ctCronoIdleAddTiles[CTCRONOIDLEADD_NKF][CT_ANIM_NTILE][32] = {0};
static const uint8_t ctCronoIdleStateToKf[3] = {0};  
#define CTGATOSWIPE_NOBJ 36
static const CtAnimObj ctGatoSwipeObj[CTGATOSWIPE_NOBJ] = {0};
#define CTGATOSWIPE_NKF 3
static const CtAnimAddKf ctGatoSwipeKf[CTGATOSWIPE_NKF] = {0};
static const uint8_t ctGatoSwipeTiles[CTGATOSWIPE_NKF][CT_ANIM_NTILE][32] = {0};
#define CT_GATO_IDLE2_F0 67
#define CT_GATO_IDLE2_N 120
static const CtGatoIdleStep ctGatoIdleTrajV2[CT_GATO_IDLE2_N] = {0};
#endif
