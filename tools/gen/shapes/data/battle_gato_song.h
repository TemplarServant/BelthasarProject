/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_BATTLE_GATO_SONG_H
#define CT_BATTLE_GATO_SONG_H
#include <stdint.h>
typedef struct { int16_t relX, relY; uint16_t tile; uint8_t sway; } CtGatoSongCell;  
static const CtGatoSongCell ctGatoSongCell[12] = {0};
#define CT_GATO_SONG_NTILE 0xA0
static const uint8_t ctGatoSongTiles[CT_GATO_SONG_NTILE][32] = {0};
#define CT_GATO_SONG_SWAY_N 4
#define CT_GATO_SONG_SWAY_DWELL 15
static const int8_t ctGatoSongSway[CT_GATO_SONG_SWAY_N][2] = {0};
#endif
