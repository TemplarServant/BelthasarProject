/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_ANIMENG_E3_GATO_H
#define CT_ANIMENG_E3_GATO_H
#include <stdint.h>
typedef struct { int8_t dx, dy; } CtAnimengPos;              
typedef struct { CtAnimengPos pos[12]; uint16_t cw[48]; } CtAnimengE3Rec;
#define CT_ANIMENG_E3_GATO_N 0x20
static const CtAnimengE3Rec ctAnimengE3Gato[CT_ANIMENG_E3_GATO_N] = {0};
#endif
