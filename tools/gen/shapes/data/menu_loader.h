/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_MENU_LOADER_H
#define CT_MENU_LOADER_H
#include <stdint.h>
#define CT_MENU_LOADER_TMH 64
static const uint16_t ctMenuChromeLoaderTM[32*CT_MENU_LOADER_TMH] = {0};
#define CT_MENU_LOADER_NBAND 3
static const struct { short y0, y1; unsigned short vofs; }
ctMenuLoaderBand[CT_MENU_LOADER_NBAND] = {
  {   0,  39, 0x00FF },
  {  40, 135, 0x01E7 },
  { 136, 215, 0x01F7 },
};
static const uint8_t ctMenuChromeLoaderPal[8][16][3] = {0};
#define CT_LOADER_ICON_TRANSPARENT 0x01000000u
static const uint32_t ctLoaderIcon[7][16][16] = {0};
static const uint8_t ctLoaderVwfTone[4][3] = {0};
static const uint32_t ctLoaderNumeral[3][16][16] = {0};
#define CT_LOADER_CHAPTERS 27
static const uint8_t ctLoaderChapThr[26] = {0};
static const char* const ctLoaderChapter[CT_LOADER_CHAPTERS] = {0};
static const uint8_t ctLoaderEraOf[512] = {0};
static const char* const ctLoaderEra[8] = {0};
#endif
