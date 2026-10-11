/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_ANIMENG_E3_PC_H
#define CT_ANIMENG_E3_PC_H
#include <stdint.h>
typedef struct { int8_t dx, dy; } CtAnimengPcPos;              
typedef struct { uint8_t idx; CtAnimengPcPos pos[4]; uint16_t cw[16]; } CtAnimengPcRec;  
#define CT_ANIMENG_E3_CRONO_N 13
static const CtAnimengPcRec ctAnimengE3Crono[CT_ANIMENG_E3_CRONO_N] = {0};
#define CT_ANIMENG_E3_MARLE_N 6
static const CtAnimengPcRec ctAnimengE3Marle[CT_ANIMENG_E3_MARLE_N] = {0};
#define CT_ANIMENG_E3_LUCCA_N 11
static const CtAnimengPcRec ctAnimengE3Lucca[CT_ANIMENG_E3_LUCCA_N] = {0};
#define CT_ANIMENG_E3_FROG_N 1
static const CtAnimengPcRec ctAnimengE3Frog[CT_ANIMENG_E3_FROG_N] = {0};
#endif
