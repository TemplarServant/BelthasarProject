/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_STANCE_H
#define CT_BATTLE_STANCE_H
#include <stdint.h>
typedef struct { uint8_t oidx; int16_t x; uint8_t y; uint16_t tile;
                 uint8_t pal, prio, hf, vf; } CtStanceObj;
#define CT_STANCE_NOBJ 20
static const CtStanceObj ctBattleStanceObj[CT_STANCE_NOBJ] = {0};
typedef struct { const char* name; uint8_t pal; int16_t anchorX; uint8_t anchorY, nspr; } CtStanceActor;
#define CT_STANCE_NACTOR 3
static const CtStanceActor ctBattleStanceActor[CT_STANCE_NACTOR] = {0};
#define CT_STANCE_NTILE 0xA0
static const uint8_t ctBattleStanceObjTiles[CT_STANCE_NTILE][32] = {0};
static const uint16_t ctBattleStanceObjPal[8][16] = {0};
#endif
