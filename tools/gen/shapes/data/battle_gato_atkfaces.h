/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_GATO_ATKFACES_H
#define CT_BATTLE_GATO_ATKFACES_H
#include <stdint.h>
typedef struct { int16_t relX, relY; uint16_t tile; } CtGatoAtkCell;
typedef struct { const char* name; uint8_t facing; uint8_t strikeAt;
                 uint16_t rearOff; uint8_t rearN; uint8_t rearTile;
                 uint16_t strikeOff; uint8_t strikeN; uint8_t strikeTile; } CtGatoAtkFace;
static const CtGatoAtkCell ctGatoAtkCell[60] = {0};
#define CT_GATO_ATK_NFACE 3
static const CtGatoAtkFace ctGatoAtkFace[CT_GATO_ATK_NFACE] = {0};
#define CT_GATO_ATK_NTILE 0xA0
static const uint8_t ctGatoAtkTiles0[CT_GATO_ATK_NTILE][32] = {0};
static const uint8_t ctGatoAtkTiles1[CT_GATO_ATK_NTILE][32] = {0};
static const uint8_t ctGatoAtkTiles2[CT_GATO_ATK_NTILE][32] = {0};
static const uint8_t ctGatoAtkTiles3[CT_GATO_ATK_NTILE][32] = {0};
static const uint8_t ctGatoAtkTiles4[CT_GATO_ATK_NTILE][32] = {0};
static const uint8_t (*const ctGatoAtkTilesTab[5])[32] = { ctGatoAtkTiles0, ctGatoAtkTiles1, ctGatoAtkTiles2, ctGatoAtkTiles3, ctGatoAtkTiles4 };
#endif
