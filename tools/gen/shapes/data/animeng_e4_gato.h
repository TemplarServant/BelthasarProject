/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_ANIMENG_E4_GATO_H
#define CT_ANIMENG_E4_GATO_H
#include <stdint.h>
typedef struct { uint8_t idx, dwell; } CtAnimengStep;        
typedef struct { CtAnimengStep step[4]; } CtAnimengWindow;   
static const CtAnimengWindow ctAnimengWinGato[4] = {0};
typedef struct { uint8_t rear, strike, strikeAt; } CtAnimengGatoAtk;  
static const CtAnimengGatoAtk ctAnimengGatoAtk[4] = {0};
typedef struct { uint8_t idx, a, b; } CtAnimengGatoSwing;
static const CtAnimengGatoSwing ctAnimengGatoFrontSwing[3] = {0};
#define CT_ANIMENG_GATO_HURT           0x0C
#define CT_ANIMENG_GATO_HURT_DWELL     15
#define CT_ANIMENG_GATO_FLURRY_DWELL   8   
#define CT_ANIMENG_GATO_FLURRY_CYCLES  6   
#define CT_ANIMENG_GATO_FLURRY_WINDUP  26  
typedef struct { uint8_t fist, retract, windup; } CtAnimengGatoFlurry;  
static const CtAnimengGatoFlurry ctAnimengGatoFlurryPair[4] = {0};
#define CT_ANIMENG_GATO_SONG_ARMSOUT   0x04
#define CT_ANIMENG_GATO_SONG_SWAY_DWELL 15
#define CT_ANIMENG_GATO_SONG_LIT0      42
#define CT_ANIMENG_GATO_SONG_LIT1      236
static const uint8_t ctAnimengGatoSongSway[4] = {0};
#endif
