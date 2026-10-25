// motion.h - what moves each pixel of the field's picture, and the picture redrawn at a higher internal scale from it.
//
// The field's compositor (field.c) stays the ONLY source of the picture: it draws the 256x224 frame exactly as before,
// and while it draws it records, per pixel, the thing that pixel moves with -- the camera (the map canvas, behind or in
// front of the sprites), the caption plane's own scroll, one object (by its place in the draw order), or nothing (the
// text box, the HUD, a wipe: screen-fixed). With the camera and every drawn object's position of this frame and the
// previous one, CtMotRaster redraws the frame at k x 256 by k x 224 with each owner placed at a sub-pixel position
// between the two frames: t = 256 is this frame as composed (each pixel a k x k block -- the plain upscale), t = 0
// puts everything back where the previous frame had it. A pixel a moving object uncovers is the map canvas at the
// shifted camera.
//
// Nothing here decides what the game shows; at t = 256 the output is the composed frame, pixel for pixel.
#ifndef CT_MOTION_H
#define CT_MOTION_H
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CT_MOT_W 544                      // the widest picture (the widescreen field); a frame says its own width
#define CT_MOT_H 224
#define CT_MOT_MAXSPR 96
#define CT_MOT_MAXSPX 65536               // the sprites' own pixels a frame keeps (CtMotFrame.spx)

// the owners (CtMotFrame.own): the map's layers behind the sprites (BG1/BG2/BG3, each at its own scroll) and in front of
// them (the priority-1 foreground of BG1/BG2), the caption plane behind and in front, and a sprite: CT_MOT_SPR + its
// draw index, | CT_MOT_FRONT when the pixel was drawn in front of the map's foreground
enum { CT_MOT_FIXED = 0, CT_MOT_CAPB = 1, CT_MOT_BG1 = 2, CT_MOT_BG1F = 3, CT_MOT_CAPF = 4, CT_MOT_BG2 = 5, CT_MOT_BG3 = 6,
       CT_MOT_BG2F = 7, CT_MOT_SPR = 8, CT_MOT_FRONT = 0x80 };
// the map owner of a canvas pixel from its layer bit (1 BG1, 2 BG2, 4 BG3, 0 the backdrop -- it moves with the camera)
static inline int CtMotMapOwner(int layerBit, int front) {
  if (layerBit & 2) return front ? CT_MOT_BG2F : CT_MOT_BG2;
  if (layerBit & 4) return CT_MOT_BG3;
  return front ? CT_MOT_BG1F : CT_MOT_BG1;
}

typedef struct CtMotFrame {
  int valid;                              // a field frame composed with its owners (else: the frame is screen-fixed)
  int w;                                  // the picture's width (256, or the widescreen field's); 224 lines
  int scene;                              // the location: a change between two frames = nothing moves between them
  uint8_t own[CT_MOT_W * CT_MOT_H];
  // 1 = a WORLD pixel the frame drew screen-fixed (own FIXED): the widescreen's Steam margins past a room's edge. The frames
  // between take it at the map place it shows at t -- it moves with the camera -- not at the screen's (fieldnative.c).
  // 2 = one of the margins' BG2 plane, which scrolls with BG2 (Magus's castle 0A5: its sky at half the camera's speed).
  uint8_t world[CT_MOT_W * CT_MOT_H];
  int camX, camY;                         // the map canvas pixel the screen's (0,0) shows
  int o2x, o2y, o3x, o3y;                 // BG2's and BG3's own scroll on top of it (canvas pixel p shows BGn pixel p + o)
  int bg3X, bg3Y;                         // the caption plane's scroll
  // A LAYER THAT SCROLLS ON ITS OWN at less than a pixel a frame (1 px every 2, 3, 4 frames: the mist, the clouds, a sky)
  // steps on the frames its whole pixel moves. qFrac set: q* are the layers' CONTINUOUS offsets (the host's fit over the
  // last frames, CtMotFit), which the frames between follow instead of the last step.
  int qFrac;
  double q2x, q2y, q3x, q3y, qbx, qby;
  // ...and THE CAMERA and EACH OBJECT the same way: a pan or a walk at less than a pixel a frame (the attract's Blackbird,
  // the camera at 0.75 px a frame and the ship at 0.25; the trial's pan, a pixel every 4 frames) steps on the frames its
  // whole pixel moves, and the frames between went a pixel in one frame and stood in the others. qcx / qcy: the camera's
  // continuous place; qsx / qsy: each object's (its own where it moves in whole pixels a frame, or not steadily).
  double qcx, qcy;
  double qsx[CT_MOT_MAXSPR], qsy[CT_MOT_MAXSPR];
  int nspr;
  int sprKey[CT_MOT_MAXSPR];              // the object (its index; the lead is its object too)
  int sprX[CT_MOT_MAXSPR], sprY[CT_MOT_MAXSPR];   // its map position this frame
  // THE ENGINE'S OWN FRACTIONS (the field; CtMotEngine): the camera's whole place as the ROM keeps it and the same place
  // in sixteenths -- the whole one * 16 + the remainder C0/92CA carries in $1D3B/$1D3C -- and each object's 12.4 words
  // ($1800/$1880,X), so CtMotFit takes a motion's rate from the engine where the drawn steps agree with it, and fits
  // the history only where they do not or there is none. camE 0 / a word -1: none.
  int camE, camEW[2], camEX[2];
  const uint8_t* w7;                      // the field's $7E page while the frame composes (CtMotSprite reads it), or NULL
  int sprWx[CT_MOT_MAXSPR], sprWy[CT_MOT_MAXSPR];
  int bright;                             // 0..15, the brightness applied after the picture (the canvas fill takes it)
  const uint8_t* canvas; int cW, cH;      // the map canvas (RGB, wrapped) the uncovered pixels come from
  int canvasX0, canvasY0;                 // ...the canvas pixel under the screen's (0,0) (the field's: the camera)
  int mx0, mx1;                           // ...inside the scroll mask's columns (outside them: black, the widescreen's sides)
  // EVERY PIXEL EACH SPRITE PUT DOWN, as it put it (before the brightness), the ones another sprite or the foreground then
  // covered included: what a sprite moving off another uncovers is that other sprite, not the map.
  int nspx;
  struct { int16_t x, y; uint8_t r, g, b, own, hole, pal; } spx[CT_MOT_MAXSPX];   // hole: an occluder's (the map shows); pal: its OBJ palette (0xFF: none)
  const void* native;                     // the map's layers when the frame can be drawn natively (fieldnative.c), else NULL
  const void* nativeFx;                   // the field's iris / colour window / caption plane for the native drawer (fieldnative.h)
  const void* owNative;                   // the overworld's own (field.c OwNat) when it can be drawn natively, else NULL
} CtMotFrame;

void CtMotBegin(CtMotFrame* m, int w);    // a new frame `w` pixels wide: every pixel screen-fixed, no objects
// The field's frame: the engine's camera (camWX/camWY, the ROM's whole place) and its $7E page, read for the camera's
// remainder now and for each object's words as CtMotSprite records it (motion.h CtMotFrame.camE).
void CtMotEngine(CtMotFrame* m, const uint8_t* w7, int camWX, int camWY);
// The continuous offsets of the self-scrolling layers for `cur` (the frame just composed), from a fit over the last frames
// of the same location (a history kept here). A layer whose recent offsets are not one steady slope keeps its own.
void CtMotFit(CtMotFrame* cur);
extern int g_ctMotNoFrac;                 // 1 = CtMotRaster ignores the continuous offsets (the owners' check)
// Between prev and cur at tt (0..1): the camera's place less cur's (dx, dy) and object i's (j its index in prev) less where
// cur has it -- the continuous places where CtMotFit fitted them, else the straight line of the step (dcx/dcy, wx/wy: the
// caller's, an overworld's across its wrap).
void CtMotCamShift(const CtMotFrame* cur, const CtMotFrame* prev, double tt, int dcx, int dcy, double* dx, double* dy);
void CtMotSprShift(const CtMotFrame* cur, int i, const CtMotFrame* prev, int j, double tt, int wx, int wy, double* dx, double* dy);
static inline void CtMotTag(CtMotFrame* m, int x, int y, int own) {
  if (m && (unsigned)x < (unsigned)m->w && (unsigned)y < CT_MOT_H) m->own[y * m->w + x] = (uint8_t)own;
}
static inline void CtMotFixRow(CtMotFrame* m, int y) {           // a screen-fixed row (the text box, the blank lines)
  if (m && (unsigned)y < CT_MOT_H) { uint8_t* r = m->own + y * m->w; for (int x = 0; x < m->w; x++) r[x] = CT_MOT_FIXED;
                                    memset(m->world + y * m->w, 0, (size_t)m->w); }   // (screen-fixed: no world pixel left there)
}
int  CtMotSprite(CtMotFrame* m, int key, int x, int y);   // the next drawn object: its owner tag (0 = the list is full)
static inline void CtMotSpritePixel(CtMotFrame* m, int x, int y, int r, int g, int b, int own) {   // (a sprite's pixel)
  if (!m || m->nspx >= CT_MOT_MAXSPX || (own & 0x7F) < CT_MOT_SPR) return;
  m->spx[m->nspx].x = (int16_t)x; m->spx[m->nspx].y = (int16_t)y;
  m->spx[m->nspx].r = (uint8_t)r; m->spx[m->nspx].g = (uint8_t)g; m->spx[m->nspx].b = (uint8_t)b; m->spx[m->nspx].own = (uint8_t)own;
  m->spx[m->nspx].hole = 0; m->spx[m->nspx].pal = 0xFF;
  m->nspx++;
}
static inline void CtMotSpriteHole(CtMotFrame* m, int x, int y, int own) {   // an occluder's pixel: what is under it goes
  if (!m || m->nspx >= CT_MOT_MAXSPX || (own & 0x7F) < CT_MOT_SPR) return;
  m->spx[m->nspx].x = (int16_t)x; m->spx[m->nspx].y = (int16_t)y; m->spx[m->nspx].own = (uint8_t)own; m->spx[m->nspx].hole = 1;
  m->spx[m->nspx].r = m->spx[m->nspx].g = m->spx[m->nspx].b = 0; m->spx[m->nspx].pal = 0xFF;
  m->nspx++;
}
// The picture after the compositor (`pic`, RGB w x 224) against the final frame (`fb`, after the overlays and the
// brightness `bright`): a pixel that no longer holds its owner's colour was drawn over by something screen-fixed.
void CtMotSettle(CtMotFrame* m, const uint8_t* pic, const uint8_t* fb, int bright);
// Draw `fb` (this frame, RGB w x 224) at scale k (1..8) into `out` (RGB, wk x 224k) at position t (0..256) between
// `prev` and `cur`. With no usable motion (either frame invalid or of another width, another location, a jump) it is
// the plain upscale.
void CtMotRaster(const CtMotFrame* cur, const CtMotFrame* prev, const uint8_t* fb, int w, int t, int k, uint8_t* out);

#ifdef __cplusplus
}
#endif
#endif
