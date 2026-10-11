/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_GATO_FACES_H
#define CT_BATTLE_GATO_FACES_H
#include <stdint.h>
typedef struct { int16_t relX, relY; uint16_t tile; } CtGatoFaceCell;
typedef struct { const char* pose; uint8_t facing; uint8_t cellOff, nCell; } CtGatoFaceKf;
static const CtGatoFaceCell ctGatoFaceCell[36] = {0};
static const CtGatoFaceKf ctGatoFaceKf[3] = {0};
#define CT_GATO_FACE_NTILE 0xA0
static const uint8_t ctGatoFaceTiles0[CT_GATO_FACE_NTILE][32] = {0};
static const uint8_t ctGatoFaceTiles1[CT_GATO_FACE_NTILE][32] = {0};
static const uint8_t ctGatoFaceTiles2[CT_GATO_FACE_NTILE][32] = {0};
static const uint8_t (*const ctGatoFaceTilesTab[3])[32] = { ctGatoFaceTiles0, ctGatoFaceTiles1, ctGatoFaceTiles2 };
#endif
