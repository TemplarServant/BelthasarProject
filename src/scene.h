// scene.h -- the field scene renderer: map bake, layers, tile copies and sprite composition.
#ifndef CT_SCENE_H
#define CT_SCENE_H

#include <stdint.h>
#include "assets.h"

// Render scene `index` (animation `frame`) to a freshly malloc'd RGB buffer
// (3 bytes/pixel). `blend` enables SNES color-math; `full` disables the
// scroll-mask crop. `pal_override` (NULL or 128*3 RGB) supplies the base scene
// palette from ct_assets.dat instead of decoding it from the ROM; palette
// animation is still applied on top. Sets *outW/*outH.
uint8_t* CtRenderScene(const uint8_t* rom, int romlen, int index, int frame,
                       int blend, int full, const uint8_t* pal_override, int* outW, int* outH);

// Render scene `index` from ct_assets.dat alone (no ROM), on the same compose path as
// CtRenderScene. Caller frees the returned RGB buffer.
uint8_t* CtRenderSceneFromAssets(const CtAssets* a, int index, int frame,
                                 int blend, int full, int* outW, int* outH);

// Mutable scene handle for scripted tile copies (event ops 0xE4/0xE5): open once per
// fieldLoad, bake (caller frees the RGB + prio buffers), mutate with CopyTiles, re-bake.
void* CtFieldSceneOpen(const CtAssets* a, int index, int frame);
void  CtFieldSceneClose(void* h);
uint8_t* CtFieldSceneBake(void* h, int* outW, int* outH, uint8_t** outPrioMask);
uint8_t* CtFieldSceneBakeLayersOff(void* h, int layersOff, int* outW, int* outH);   // the bake without BL1/BL2/BL3 bits (a window's)
// The widescreen's still backdrop (a mod; field.c fieldWideStill): layers BL2 2 / BL3 4 held to the wide picture --
// the 256 view's columns (canvas column vx on) drawn from canvas column vw -- and mirrored past them instead of
// repeated; 0 = the plain bake. Returns 1 when changed (re-bake).
int CtFieldSceneSetStill(void* h, int bits, int vx, int vw);
// A mod's picture (widext.h): five planes of w x h metatile ids drawn with this scene's palette and the given chip sets,
// metatile definitions (else this scene's) and pixel sheets into rgb ((16 w) x (16 h) x 3) and cov (BL1 under the
// sprites, BL2 / BL3 over them, 0 = nothing); 1 = done. steamPal (128 LE BGR555, or NULL): the colours the sheets were
// painted with, to read their indices as the room's own (never drawn).
int CtFieldSceneBakeExt(void* h, const uint16_t* planes, int w, int hgt, const uint8_t* const slotChips[6], int nMeta,
                        const uint16_t* metaIds, const uint8_t* metaCorners, const uint8_t* const pageSheet[8],
                        const uint8_t* steamPal, int planeMask, uint8_t* rgb, uint8_t* cov);
// The scene's live BG palette, 128 x RGB (it follows CtFieldSceneSetCgram and the palette cycles).
const uint8_t* CtFieldScenePalette(void* h);
// The last bake's MAIN-screen coverage, one byte per pixel: the layer bit (L1 1, L2 2, L3 4) on top, 0 = backdrop.
const uint8_t* CtFieldSceneCoverage(void* h, int* w, int* hgt);
const uint8_t* CtFieldSceneMainScreen(void* h, int* blendEn, int* subOp, int* half);   // the main screen before the math, when OBJ is sub-only
const uint8_t* CtFieldSceneScreens(void* h, const uint8_t** subCov, const uint8_t** mainPre, int* blendEn, int* subOp,
                                   int* half);   // the last bake's sub screen + layer bits, its main before the math
// Per pixel, the layer bit its picture comes from (motion.h); the coverage's size.
const uint8_t* CtFieldSceneMotionCoverage(void* h);
// The map as layers (for the frames between, fieldnative.c): BG1/BG2/BG3 x priority 0/1, each its own picture over
// one period of the layer (sampled modulo its size at the layer's own scroll), with the screen and colour-math setup the
// bake composites them with. Index order = the bake's draw order: 0 BG3.0, 1 BG2.0, 2 BG1.0, 3 BG2.1, 4 BG1.1, 5 BG3.1.
typedef struct CtSceneLayers {
  const uint8_t* rgb[6]; const uint8_t* cov[6]; int w[6], h[6];
  int mainT, subT, blendEn, subOp, half;   // BL1 1 / BL2 2 / BL3 4 bits, as the bake uses them
  uint8_t backdrop[3];
  int beyond;                              // (host, widescreen) layers drawn past the room's edge (field.c fieldBeyondLayers): left side in bits 0-7, right in 8-15
  int still, viewX;                        // (host, widescreen) the still backdrop's layers and the 256 view's picture column
  int cut, cutL, cutR;                     // (host, widescreen) `beyond` ends on the left (cut bit 0) left of map column
                                           //   cutL, on the right (bit 1) from cutR on: field.c fieldSteamCovers past the margins
} CtSceneLayers;
int CtFieldSceneLayers(void* h, CtSceneLayers* out);   // 1 = filled (built on demand, again after each bake)
int CtFieldSceneLayerOffsetsBaked(void* h, int* o2x, int* o2y, int* o3x, int* o3y);   // the offsets the bake drew at
// The map header's layer-scroll bytes md[1..3] (0 when the pack predates them).
int   CtFieldSceneMapHeader(void* h, uint8_t* md1, uint8_t* md2, uint8_t* md3);
int   CtFieldSceneVramTilemaps(void* h, uint16_t* vram, const uint8_t* asm12, const uint8_t* asm3, int d35, int bg3);   // the ROM's 64x32 tilemap words at $6000/$6800/$7000 (C0/794E law, C0/79CF words)
// BG1's map size in metatiles ($0BCB / $0BCD, C0/A3D9-A3F2); returns 0 without a scene.
int   CtFieldSceneBg1Size(void* h, int* w, int* hgt);
// The BG2/BG3 sample offsets relative to BG1, in pixels (returns 1 when they changed, i.e. re-bake).
// mapBuilt = $1D35 & 6: the layers whose tilemap the load built from the MAP (C0/794E, C0/7CE7) rather than the camera.
int   CtFieldSceneSetLayerOffsets(void* h, int bg2x, int bg2y, int bg3x, int bg3y, int mapBuilt);
int   CtFieldSceneSetVramLayers(void* h, int layers);                  // layers the bake leaves to the host's VRAM (1 = changed)
int   CtFieldSceneScreenLayers(void* h, int* mainT, int* subT, int* mathEn);   // TM / TS / the math enables, BL bits
// The field's own handle (CtFieldSceneOpen) starts from the tileset's characters and the asset palette with no
// animation baked in; the ROM's machines drive them from WRAM (disasm/notes/field_bg_anim.md):
int   CtFieldSceneTileDma(void* h, int vramWord, const uint8_t* bytes, int n);   // a VRAM DMA into the BG1/BG2 characters
int   CtFieldSceneSetCgram(void* h, const uint8_t* page);                          // CGRAM 0-127 from the live page $7E:2200
int   CtFieldSceneSetEffect(void* h, int cgadsub);   // the field's live CGADSUB ($0BE0); 1 = the bake must redo
// The colour math the bake composes with: on = 0 -> the map header's (effect_flags); on = 1 -> these (CGWSEL, CGADSUB: a
// battle's arena, its NMI's shadows; the layer designations stay the header's). Returns 1 when that changes the inputs.
int   CtFieldSceneSetScreen(void* h, int on, int cgwsel, int cgadsub, int tm, int ts);   // a battle's colour math + TM/TS
int   CtFieldSceneMapFromWram(void* h, const uint8_t* s7e);                        // the maps from $7E:3000.. (a seed's)
int   CtFieldSceneMapFromWramRect(void* h, const uint8_t* s7e, int x0, int y0, int x1, int y1, int layers);   // a rect of them
// The VRAM's own copy (scene.c CtFieldScenePicHold): chips an op-0xE4 copy reached while the tilemap held them keep their
// metatile until an upload (the streamer's edge, a style rebuild, the load's fill, a door/chest handler) releases them.
int   CtFieldScenePicHold(void* h, int layer, int x0, int y0, int x1, int y1, const int* win, int cmx, int cmy);
int   CtFieldScenePicRelease(void* h, int layer, const int* win, int cmx, int cmy);   // outside win, or all (win NULL)
int   CtFieldScenePicReleaseRect(void* h, int layer, int x0, int y0, int x1, int y1);
int   CtFieldSceneDoorOpen(void* h, int tx, int ty);   // C0/0D4C: bump a door's L1 metatiles
int   CtFieldSceneChestOpen(void* h, int tx, int ty); // C0/16DC: bump a treasure chest's L1 metatile
void  CtFieldSceneCopyTiles(void* h, int sl, int st, int el, int eb, int dl, int dt, int flags);
void  CtFieldSceneDumpTiles(void* h, int x0, int y0, int x1, int y1);   // debug probe
// Render a field scene (blended, full) and a malloc'd priority mask (*outPrioMask, 1 byte/pixel:
// 1 = a BG1/BG2 priority-1 foreground tile pixel the player walks behind). Caller frees both buffers.
uint8_t* CtRenderFieldBase(const CtAssets* a, int index, int frame,
                           int* outW, int* outH, uint8_t** outPrioMask);

// Sprite compose-buffer geometry. The buffer is a port intermediate (the SNES places OBJs straight into OAM): a chip
// at assembly offset (ox,oy) lands at buffer (ox+CT_SPR_OX, oy+CT_SPR_OY) and is drawn back at screen
// (anchor.x-CT_SPR_OX, anchor.y-CT_SPR_OY), so the origin cancels on screen and only bounds what the buffer holds.
// A unit's offsets are signed bytes (-128..127) and its chips add 0..15, so a pixel lands at -128..142 on either
// axis: W = H = 272 with the origin at 128. Which priority byte a pixel takes is its unit's
// (CtComposeSpriteHalves), not a buffer row.
#define CT_SPR_W  272
#define CT_SPR_H  272
#define CT_SPR_OX 128
#define CT_SPR_OY 128

// Compose sprite `frame` from a packed CT_ASSET_SPRITE blob into a CT_SPR_W x CT_SPR_H index buffer
// `out` (caller-allocated, CT_SPR_W*CT_SPR_H bytes; index 0 = transparent) and fill `outPal` with the
// 16-colour RGB palette (48 bytes). Draw at (actor.x-CT_SPR_OX, actor.y-CT_SPR_OY).
// Returns 1 on success, 0 on a bad blob/frame. Ports assets/sprites.py compose_frame.
int CtComposeSprite(const uint8_t* blob, uint32_t blobLen, int frame, uint8_t* out, uint8_t* outPal);
// ...and `box` (may be NULL) receives the opaque pixels' bounds in buffer coordinates, inclusive: x0, y0, x1, y1
// (x1 < x0 when nothing was drawn), so a per-frame draw walks that box instead of the whole 272 x 272.
int CtComposeSpriteBox(const uint8_t* blob, uint32_t blobLen, int frame, uint8_t* out, uint8_t* outPal, int* box);
// ...and `half` (may be NULL; CT_SPR_W x CT_SPR_H, written where `out` is) receives which OBJ priority byte the pixel's
// unit takes, as the anim drain fills the attrs: 0 = $0C00,X (the top), 1 = $0C01,X (the bottom), 2 = a fixed priority 2
// (class 3). Class 0 chooses per unit by its Y offset (C0/CD4F `CMP #E8`), classes 1/2 by the unit's index (units 0-3
// the top). See scene.c.
int CtComposeSpriteHalves(const uint8_t* blob, uint32_t blobLen, int frame, uint8_t* out, uint8_t* outPal, int* box,
                          uint8_t* half);

#endif
