/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_FX_H
#define CT_BATTLE_FX_H
#include <stdint.h>
#define CT_FX_DIGIT_BASE 0x190   
#define CT_FX_DIGIT_W    8       
#define CT_FX_DIGIT_PAL  0       
#define CT_FX_FLASH_PAL  4       
#define CT_FX_MISS_BASE  0x19A   
#define CT_FX_MISS_NTILE 2       
#define CT_FX_RISE_N     25       
#define CT_FX_HOLD       32       
#define CT_FX_LIFETIME   57       
static const uint8_t ctBattleFxDigitTile[10][32] = {0};
static const uint8_t ctBattleFxMissTile[2][32] = {0};
static const uint16_t ctBattleFxDigitPal[16] = {0};
static const uint16_t ctBattleFxHealPal[16] = {0};
static const uint16_t ctBattleFxFlashPal[16] = {0};
static const int8_t ctBattleFxRiseDY[CT_FX_RISE_N] = {0};
#endif
