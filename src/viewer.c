// viewer.c — interactive SDL2 viewer for the C scene engine. Opens a window,
// renders a Chrono Trigger field scene (src/scene.c) to a texture, advances chip
// + palette animation in real time, and navigates scenes with the keyboard.
//
//   Left/Right : previous / next scene        Space : pause animation
//   F          : toggle full map vs camera crop  Esc : quit
//
// Usage: ct_viewer[.exe] [rom.sfc] [--scene N] [--assets ct_assets.dat] [--selftest]
//        ct_viewer[.exe] --datonly ct_assets.dat [--scene N]   (renders WITHOUT the ROM)
// --datonly renders entirely from ct_assets.dat (no ROM needed). --selftest renders
// one scene headlessly (dummy video driver) and prints an FNV of the pixels, so the
// SDL build/render path can be checked without a display.
#define SDL_MAIN_HANDLED
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "scene.h"
#include "assets.h"

static uint8_t* readfile(const char* path, int* n) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long s = ftell(f); rewind(f);
  if (s <= 0) { fclose(f); return NULL; }            // ftell failure (-1) would feed malloc((size_t)-1)
  uint8_t* b = malloc((size_t)s);
  if (!b || fread(b, 1, (size_t)s, f) != (size_t)s) { fclose(f); free(b); return NULL; }
  fclose(f); *n = (int)s; return b;
}

int main(int argc, char** argv) {
  const char* rompath = "Chrono_Trigger_USA_SNES.sfc";
  const char* datpath = NULL;
  int scene = 5, selftest = 0, dat_mode = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--scene") && i + 1 < argc) scene = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--assets") && i + 1 < argc) datpath = argv[++i];
    else if (!strcmp(argv[i], "--datonly") && i + 1 < argc) { datpath = argv[++i]; dat_mode = 1; }
    else if (!strcmp(argv[i], "--selftest")) selftest = 1;
    else if (argv[i][0] != '-') rompath = argv[i];
  }
  CtAssets assets; int have_assets = 0;
  if (datpath && CtAssetsOpen(&assets, datpath)) have_assets = 1;
  int romlen = 0; uint8_t* rom = NULL;
  if (dat_mode) {
    if (!have_assets) { printf("FAIL: cannot open assets '%s'\n", datpath); return 2; }
  } else {
    rom = readfile(rompath, &romlen);
    if (!rom) { printf("FAIL: cannot read ROM '%s'\n", rompath); return 2; }
  }

  if (selftest) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("FAIL: SDL_Init: %s\n", SDL_GetError()); return 3; }
  SDL_Window* win = SDL_CreateWindow("Chrono Trigger (CT decomp viewer)",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 768, 480,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
      selftest ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);

  SDL_Texture* tex = NULL; int tw = 0, th = 0;
  int frame = 0, paused = 0, full = 0, running = 1, dirty = 1;
  uint32_t last_render = 0;

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = 0;
      else if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
          case SDLK_ESCAPE: running = 0; break;
          case SDLK_RIGHT: scene = (scene + 1) & 0x1FF; dirty = 1; break;
          case SDLK_LEFT:  scene = (scene + 0x1FF) & 0x1FF; dirty = 1; break;
          case SDLK_SPACE: paused = !paused; break;
          case SDLK_f: full = !full; dirty = 1; break;
        }
      }
    }
    // `frame` is a 60Hz tick; each animation advances at its own delay/duration.
    // Re-render at most ~16 fps (CtRenderScene re-decodes the tileset each call).
    uint32_t now = SDL_GetTicks();
    if (!paused) {
      int tick = (int)((uint64_t)now * 60 / 1000);
      if (tick != frame && now - last_render >= 60) { frame = tick; last_render = now; dirty = 1; }
    }

    if (dirty) {
      dirty = 0;
      int W = 0, H = 0;
      uint8_t* px;
      if (dat_mode) {
        px = CtRenderSceneFromAssets(&assets, scene, frame, 1, full, &W, &H);   // no ROM
      } else {
        const uint8_t* palov = NULL;
        if (have_assets) {
          int pi = rom[0x360000 + scene * 14 + 3];
          uint32_t pl = 0;
          const uint8_t* p = CtAssetGet(&assets, CT_ASSET_SCENE_PALETTE, (uint32_t)pi, &pl);
          palov = (p && pl == 384) ? p : NULL;
        }
        px = CtRenderScene(rom, romlen, scene, frame, 1, full, palov, &W, &H);
      }
      if (selftest) {
        uint32_t fnv = 2166136261u;
        for (size_t i = 0; i < (size_t)W * H * 3; i++) fnv = (fnv ^ px[i]) * 16777619u;
        printf("selftest: scene %d -> %dx%d via SDL path, fnv32=%08X, renderer=%s\n",
               scene, W, H, fnv, ren ? "ok" : "null");
      }
      if (ren && px) {
        if (!tex || W != tw || H != th) {
          if (tex) SDL_DestroyTexture(tex);
          tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, W, H);
          tw = W; th = H;
          SDL_RenderSetLogicalSize(ren, W, H);
        }
        SDL_UpdateTexture(tex, NULL, px, W * 3);
      }
      free(px);
      char title[160];
      snprintf(title, sizeof(title), "CT viewer - scene %d  frame %d%s", scene, frame, full ? "  [full map]" : "");
      SDL_SetWindowTitle(win, title);
    }

    if (ren) { SDL_RenderClear(ren); if (tex) SDL_RenderCopy(ren, tex, NULL, NULL); SDL_RenderPresent(ren); }
    if (selftest) running = 0;
  }

  if (tex) SDL_DestroyTexture(tex);
  if (ren) SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  if (have_assets) CtAssetsClose(&assets);
  free(rom);
  return 0;
}
