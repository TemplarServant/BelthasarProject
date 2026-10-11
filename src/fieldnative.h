// fieldnative.h - the field's frames between, drawn natively from the map's layers and the sprites' own pixels (see
// fieldnative.c). A mod: only the frames between use it; the vanilla frame is the field compositor's.
#ifndef CT_FIELDNATIVE_H
#define CT_FIELDNATIVE_H
#include <stdint.h>
#include "motion.h"

// Draw the field at t (0..256) between `prev` and `cur` into `out` (RGB, w*k x 224*k). `fb` is the composed frame (w wide):
// its screen-fixed pixels are taken from it. cur->native must hold the map's layers (scene.h CtSceneLayers).
// Returns 0 when it cannot (no layers, another location, a jump): the caller draws the frame another way.
// What the field composes over the map's layers that the native drawer redraws too (cur->nativeFx, may be NULL):
//   - the IRIS window (window 1 taking BG1/BG2 off the screen, field.c fieldBgWindowRows): its rows, in the view's columns;
//   - the COLOUR window (fieldColWindowRows): its rows and the math it applies inside them;
//   - the CAPTION plane (BG3 as text, fieldBg3PlaneRow): its picture, each priority apart, one period of its map, at the
//     frame's bg3X / bg3Y (CtMotFrame) -- the back cells where the map shows its backdrop, the front ones over everything.
typedef struct CtFieldNativeFx {
  int viewX;                                   // the view's column 0 in the picture (the widescreen's left side)
  int irisOn; int16_t irisL[224], irisR[224];
  int colOn, colMath, colHalf; uint8_t colFix[3]; int16_t colL[224], colR[224];
  int capOn, capW, capH; uint8_t* capRgb[2]; uint8_t* capOpq[2];
} CtFieldNativeFx;

int CtFieldRasterNative(const CtMotFrame* cur, const CtMotFrame* prev, const uint8_t* fb, int w, int t, int k, uint8_t* out);

#endif
