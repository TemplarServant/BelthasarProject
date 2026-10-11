// ct_world -- M3-B/C interactive OVERWORLD viewer. Renders the live world map (BG2 base +
// BG1 land/structures) from a WRAM+VRAM dump of the oracle standing on the overworld, then
// WALKS the player with the byte-exact C mover + camera (src/world.c, validated by
// ct.exe --checkowmove / --checkowcam):
//
//   Arrows : walk (8px-quantized steps, real terrain collision; the world is a TORUS)
//   C      : toggle free-camera mode (pan without the player; Shift dashes)
//   Esc    : quit
//
// Usage: ct_world[.exe] [--assets ct_assets.dat | --wram f --vram f] [--selftest]
//   --assets runs ENTIRELY from the packed CT_ASSET_WORLD blob (no ROM, no savestate dumps);
//   the dump mode defaults: saves/ow_w0.live.wram / saves/ow_w0.live.vram (produce with:
//   ct.exe <rom> --navworld saves/ow_w0.state ; ct.exe <rom> --peek saves/ow_w0.state saves/ow_w0.live --frames 1)
// --selftest uses the dummy video driver, scrolls a scripted path, prints an FNV of the final
// camera view; WORLD_SHOT=<out.raw> additionally writes the final 256x224 view ([W][H][RGB]).
#define SDL_MAIN_HANDLED
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "world.h"
#include "assets.h"

#define GW 96            // live displayed map: 96x64 metatiles = 1536x1024 px (landscape)
#define GH 64
#define MAPW (GW * 16)
#define MAPH (GH * 16)
#define VIEWW 256
#define VIEWH 224

static uint8_t* readfile(const char* path, int* n) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long s = ftell(f); rewind(f);
  if (s <= 0) { fclose(f); return NULL; }            // ftell failure (-1) would feed malloc((size_t)-1)
  uint8_t* b = malloc((size_t)s);
  if (!b || fread(b, 1, (size_t)s, f) != (size_t)s) { fclose(f); free(b); return NULL; }
  fclose(f); *n = (int)s; return b;
}

static uint32_t fnv32(const uint8_t* d, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
  return h;
}

int main(int argc, char** argv) {
  const char* wramPath = "saves/ow_w0.live.wram";
  const char* vramPath = "saves/ow_w0.live.vram";
  const char* datPath = NULL;
  int selftest = 0, worldId = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--wram") && i + 1 < argc) wramPath = argv[++i];
    else if (!strcmp(argv[i], "--vram") && i + 1 < argc) vramPath = argv[++i];
    else if (!strcmp(argv[i], "--assets") && i + 1 < argc) datPath = argv[++i];
    else if (!strcmp(argv[i], "--world") && i + 1 < argc) worldId = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--selftest")) selftest = 1;
  }
  uint8_t* wram = NULL;
  uint8_t* vram = NULL;
  int datMode = 0, spawnX = 0, spawnY = 0;
  if (datPath) {                                    // synthesize the wram/vram views from the packed blob
    static CtAssets assets;
    if (!CtAssetsOpen(&assets, datPath)) { printf("FAIL: cannot open assets '%s'\n", datPath); return 2; }
    uint32_t blen = 0;
    const uint8_t* b = CtAssetGet(&assets, CT_ASSET_WORLD, (uint32_t)worldId, &blen);
    if (!b || blen < 6) { printf("FAIL: no CT_ASSET_WORLD %d in '%s' (re-run extract_assets.py with the saves/ dumps)\n", worldId, datPath); return 2; }
    // base layout = 6 hdr + 6144*2 + 2048*2 + 512*2 + 3072 + 16 (fixed 20496) + 1 nrec (at offset
    // 20502) + nrec*7 + 28672 char -- reject a truncated blob before the memcpys below read past it
    if (blen < 20503 || blen < 20503u + (uint32_t)b[20502] * 7 + 28672) { printf("FAIL: CT_ASSET_WORLD %d truncated (%u bytes)\n", worldId, blen); return 2; }
    wram = calloc(1, 0x20000);
    vram = calloc(1, 0x10000);
    spawnX = b[2] | (b[3] << 8); spawnY = b[4] | (b[5] << 8);
    const uint8_t* p = b + 6;
    memcpy(wram + 0x4000, p, 6144); p += 6144;      // BG1 grid
    memcpy(wram + 0x5800, p, 6144); p += 6144;      // BG2 grid
    memcpy(wram + 0x3000, p, 2048); p += 2048;      // BG1 defs
    memcpy(wram + 0x3800, p, 2048); p += 2048;      // BG2 defs
    memcpy(wram + 0x18C00, p, 512); p += 512;       // palette
    memcpy(wram + 0x7000, p, 512); p += 512;        // property nibbles
    memcpy(wram + 0x7200, p, 3072); p += 3072;      // trigger map
    memcpy(wram + 0x1B9B, p, 16); p += 16;          // name table
    int nrec = *p++;
    wram[0x1B38] = (uint8_t)nrec;
    memcpy(wram + 0x7E00, p, (size_t)nrec * 7); p += nrec * 7;
    memcpy(vram + 0x4000, p, 28672);                // char (tiles 512-1407)
    datMode = 1;
    printf("ct_world: assets mode (world %d, %d entry records, spawn %d,%d)\n", b[1], nrec, spawnX, spawnY);
  } else {
    int wn = 0, vn = 0;
    wram = readfile(wramPath, &wn);
    vram = readfile(vramPath, &vn);
    if (!wram || wn < 0x20000) { printf("FAIL: wram dump '%s' (need 128KB; run ct.exe --navworld + --peek)\n", wramPath); return 2; }
    if (!vram || vn < 0x10000) { printf("FAIL: vram dump '%s' (need 64KB; run ct.exe --peek)\n", vramPath); return 2; }
  }

  // Palette: the engine's CGRAM source at $7F:8C00 (BGR15 -> RGB888, (c<<3)|(c>>2)).
  uint16_t cgw[256];
  uint8_t pal[256][3];
  for (int c = 0; c < 256; c++)
    cgw[c] = (uint16_t)(wram[0x18C00 + c * 2] | (wram[0x18C00 + c * 2 + 1] << 8));
#define PAL_REFRESH(from, to) for (int c = (from); c < (to); c++) { \
    pal[c][0] = (uint8_t)(((cgw[c] & 31) << 3) | ((cgw[c] & 31) >> 2)); \
    pal[c][1] = (uint8_t)((((cgw[c] >> 5) & 31) << 3) | (((cgw[c] >> 5) & 31) >> 2)); \
    pal[c][2] = (uint8_t)((((cgw[c] >> 10) & 31) << 3) | (((cgw[c] >> 10) & 31) >> 2)); }
  PAL_REFRESH(0, 256)
  CtWorldCam cam;
  CtWorldMover mv;
  static CtWorldStream stm;                             // the streamed 64x32 tilemaps (the PPU truth)
  if (datMode) {                                        // fresh state at the packed spawn (the C2 loader way)
    memset(&cam, 0, sizeof(cam));
    CtWorldEnter(&cam, &mv, spawnX >> 3, spawnY >> 3);
    CtWorldTilemapBuild(&stm, wram, &cam);
  } else {                                              // live state from the dumps
    CtWorldCamSeed(&cam, wram);
    CtWorldMoverSeed(&mv, wram);
    CtWorldStreamSeed(&stm, wram, (const uint16_t*)vram);
  }
  printf("ct_world: map %dx%d, camera (%d,%d), player (%d,%d)\n", MAPW, MAPH, cam.bg1h, cam.bg1v, mv.posX, mv.posY);

  if (selftest) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("FAIL: SDL_Init: %s\n", SDL_GetError()); return 3; }
  SDL_Window* win = SDL_CreateWindow("Chrono Trigger overworld (CT decomp, M3-B camera)",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, VIEWW * 3, VIEWH * 3,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
      selftest ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  SDL_RenderSetLogicalSize(ren, VIEWW, VIEWH);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, VIEWW, VIEWH);

  uint8_t* view = malloc((size_t)VIEWW * VIEWH * 3);
  int running = 1, frame = 0, freeCam = 0;
  Uint32 frameT0 = SDL_GetTicks();
  uint16_t entryLatch = 0xFFFF;                   // the $1B32 model (M3-D): latched at boundaries
  int entryFlash = 0;
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = 0;
      if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
      if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_c) freeCam = !freeCam;
    }
    int up = 0, down = 0, left = 0, right = 0, speed = 1;
    if (selftest) {                                 // scripted walk: spawn -> the Truce door, then A
      if (frame < 72) left = 1;
      else if (frame < 80) down = 1;
      else if (frame < 88) left = 1;
      if (frame >= 140) running = 0;
    } else {
      const Uint8* k = SDL_GetKeyboardState(NULL);
      speed = (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) ? 2 : 1;
      right = k[SDL_SCANCODE_RIGHT]; left = k[SDL_SCANCODE_LEFT];
      down = k[SDL_SCANCODE_DOWN]; up = k[SDL_SCANCODE_UP];
    }
    int dx = 0, dy = 0;
    if (freeCam) {                                  // pan the camera directly (no player)
      dx = right ? speed : left ? -speed : 0;
      dy = down ? speed : up ? -speed : 0;
    } else {
      CtWorldMoverFrame(&mv, wram, up, down, left, right, &dx, &dy);
      if (mv.phase == 1) {                          // boundary: refresh the entry latch (C2/3550+39AE+32CD)
        int cx = mv.posX >> 3, cy = mv.posY >> 3;
        int ep = (CtWorldProp(wram, cx - 1, cy - 1) & CtWorldProp(wram, cx, cy - 1) & 4) != 0;
        entryLatch = ep ? CtWorldEntryScan(wram, mv.posX, mv.posY) : 0xFFFF;
      }
      int aPress = selftest ? (frame == 110) : SDL_GetKeyboardState(NULL)[SDL_SCANCODE_Z];   // A = Z (field.c fieldKeyPad)
      if (aPress && entryLatch != 0xFFFF) {         // A on an entry -> the $027C=3 transition (C2/35E5)
        CtWorldEntry e;
        CtWorldEntryDecode(wram, entryLatch, &e);
        printf("ct_world: ENTER location %03X at tile (%d,%d), flags %X (record +%04X, name %02X)\n",
               e.destLoc, e.destX, e.destY, e.flags, entryLatch, e.nameId);
        entryFlash = 20;
      }
    }
    CtWorldStreamFrame(&stm, &cam, wram, dx, dy);   // scroll + rebuild the VRAM tilemaps (byte-exact)
    if (frame % 10 == 9) {                          // sea palette cycling (every 10 frames, RE'd live):
      uint16_t t0 = cgw[0x24];                      // rotate-left CGRAM[24..27] and [28..2B], swap [2D]/[2E]
      cgw[0x24] = cgw[0x25]; cgw[0x25] = cgw[0x26]; cgw[0x26] = cgw[0x27]; cgw[0x27] = t0;
      t0 = cgw[0x28];
      cgw[0x28] = cgw[0x29]; cgw[0x29] = cgw[0x2A]; cgw[0x2A] = cgw[0x2B]; cgw[0x2B] = t0;
      t0 = cgw[0x2D]; cgw[0x2D] = cgw[0x2E]; cgw[0x2E] = t0;
      // (the engine also flashes CGRAM[20] one tick in four -- colour 0 is transparent on
      // hardware (backdrop+HDMA shows through), so it is intentionally NOT applied here)
      PAL_REFRESH(0x20, 0x30)
    }
    // Compose the view the PPU way: per pixel through the STREAMED 64x32 tilemaps (BG2 under BG1),
    // 4bpp char fetch + palette. The tilemap is rebuilt by the streamer as the camera moves.
    for (int y = 0; y < VIEWH; y++) {
      uint8_t* dst = view + (size_t)y * VIEWW * 3;
      for (int x = 0; x < VIEWW; x++) {
        uint8_t rr = 0, gg = 0, bb = 0;
        for (int bg = 1; bg >= 0; bg--) {           // BG2 first (base), BG1 over
          uint16_t hofs = bg == 0 ? cam.bg1h : cam.bg2h, vofs = bg == 0 ? cam.bg1v : cam.bg2v;
          int wx = (hofs + x), wy = (vofs + y + 1);   // BG line `vScroll + row + 1` (OW_BG_VLINE, src/field.c)
          int col = (wx >> 3) & 0x3F, row = (wy >> 3) & 0x1F;
          uint16_t chip = stm.tilemap[bg][(col & 0x1F) + ((col & 0x20) << 5) + row * 0x20];
          int t = chip & 0x3FF, pb = ((chip >> 10) & 7) * 16;
          int tx = wx & 7, ty = wy & 7;
          if (chip & 0x4000) tx = 7 - tx;
          if (chip & 0x8000) ty = 7 - ty;
          int b = (t + 512) * 32 + ty * 2;
          int k = 7 - tx;
          uint8_t v4 = (uint8_t)(((vram[b] >> k) & 1) | (((vram[b + 1] >> k) & 1) << 1) |
                                 (((vram[b + 16] >> k) & 1) << 2) | (((vram[b + 17] >> k) & 1) << 3));
          if (v4 == 0 && bg == 0) continue;         // BG1 transparent over BG2
          rr = pal[pb + v4][0]; gg = pal[pb + v4][1]; bb = pal[pb + v4][2];
        }
        dst[x * 3] = rr; dst[x * 3 + 1] = gg; dst[x * 3 + 2] = bb;
      }
    }
    if (!freeCam && entryLatch != 0xFFFF) {         // "location here" banner strip (name text = later)
      for (int y = 4; y < 16; y++)
        for (int x = 88; x < 168; x++) {
          size_t o = ((size_t)y * VIEWW + x) * 3;
          int edge = (y == 4 || y == 15 || x == 88 || x == 167);
          view[o] = edge ? 255 : 32; view[o + 1] = edge ? 255 : 32; view[o + 2] = edge ? 255 : 64;
        }
    }
    if (entryFlash > 0) {                           // entry feedback: brighten the frame
      entryFlash--;
      for (size_t i = 0; i < (size_t)VIEWW * VIEWH * 3; i++)
        view[i] = (uint8_t)(view[i] / 2 + 128);
    }
    if (!freeCam) {                                 // player marker (sprite compositor = later milestone):
      int sxp = ((mv.posX - cam.bg1h) % MAPW + MAPW) % MAPW;     // a 12x14 box anchored at the pos point
      int syp = ((mv.posY - cam.bg1v) % MAPH + MAPH) % MAPH;
      for (int y = -14; y < 0; y++)
        for (int x = -6; x < 6; x++) {
          int vx = sxp + x, vy = syp + y;
          if (vx < 0 || vx >= VIEWW || vy < 0 || vy >= VIEWH) continue;
          int edge = (y == -14 || y == -1 || x == -6 || x == 5);
          size_t o = ((size_t)vy * VIEWW + vx) * 3;
          if (edge) { view[o] = 255; view[o + 1] = 32; view[o + 2] = 32; }
          else { view[o] = (uint8_t)(view[o] / 2 + 100); view[o + 1] /= 2; view[o + 2] /= 2; }
        }
    }
    SDL_UpdateTexture(tex, NULL, view, VIEWW * 3);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    if (!selftest) {                                // 60 fps governor: sleep only the remainder
      Uint32 el = SDL_GetTicks() - frameT0;         // (vsync already paces when active)
      if (el < 16) SDL_Delay(16 - el);
      frameT0 = SDL_GetTicks();
    }
    frame++;
  }
  printf("ct_world: %d frames, camera ended at (%d,%d), view fnv=%08X\n",
         frame, cam.bg1h, cam.bg1v, fnv32(view, (size_t)VIEWW * VIEWH * 3));
  const char* shot = getenv("WORLD_SHOT");
  if (shot) {
    FILE* f = fopen(shot, "wb");
    if (f) {
      uint16_t wh[2] = { VIEWW, VIEWH };
      fwrite(wh, 2, 2, f); fwrite(view, (size_t)VIEWW * VIEWH * 3, 1, f); fclose(f);
      printf("ct_world: view -> %s\n", shot);
    }
  }
  free(view); free(wram); free(vram);
  SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
  return 0;
}
