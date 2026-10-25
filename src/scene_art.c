// scene_art.c -- THE SCENE ADJUSTMENTS' NEW ART (a mod; scene_art.h). Nothing here runs unless the player turned the option
// on with the widescreen: the host sets seq_pic.h's g_seqPicArt for the stage, the grading harnesses never do.
#include "scene_art.h"
#include "widext.h"
#include <string.h>

// A stamp: the scene's own pixels from (su, sy) of its layer, w x h, laid at (du, dy) of the art (mirrored when flip), its
// see-through pixels leaving what is under them.
typedef struct { int16_t du, dy, su, sy; uint8_t w, h, flip; } CtArtStamp;
#include "art/entry3_sides.h"

// ENTRY 3. The machine as the field laid it out: 256 x 256 CGRAM indices (0 = see-through) in the field's own columns u. The
// stage's BG1 map shows the field's row r one word to the right ((r, c) at display column c + 1; its column 31 at the next
// row's display column 0); its tiles are 4bpp.
static uint8_t g_e3Orig[256][256];
static uint8_t g_e3Art[E3_ART_H][E3_ART_W];
static uint32_t g_e3Key = 0xFFFFFFFFu;

static int tilePixel(const CtSeqPic* p, unsigned chr, uint16_t e, int x, int y) {
  int row = (e & 0x8000) ? 7 - y : y, col = (e & 0x4000) ? x : 7 - x;
  unsigned base = chr + (unsigned)(e & 0x3FF) * 16;
  uint16_t a = p->vram[(base + row) & 0x7FFF], b = p->vram[(base + 8 + row) & 0x7FFF];
  return ((a >> col) & 1) | (((a >> (8 + col)) & 1) << 1) | (((b >> col) & 1) << 2) | (((b >> (8 + col)) & 1) << 3);
}

// Built again whenever the stage's BG1 map or tiles change (the setup uploads the map in the stage's first frames): the
// machine, then the stamps over an empty art, then the new pixels (kE3Px's runs: count, value; 0 = none, a multiple of 16 =
// see-through, else a CGRAM index).
static void entry3Build(const CtSeqPic* p) {
  const unsigned map = (unsigned)(p->r[0x07] & 0xFC) << 8, chr = (unsigned)(p->r[0x0B] & 0x0F) << 12;
  uint32_t key = 2166136261u;
  for (unsigned i = 0; i < 0x400; i++) key = (key ^ p->vram[(map + i) & 0x7FFF]) * 16777619u;
  for (unsigned i = 0; i < 0x4000; i += 61) key = (key ^ p->vram[(chr + i) & 0x7FFF]) * 16777619u;
  if (key == g_e3Key) return;
  g_e3Key = key;
  for (int ty = 0; ty < 32; ty++)
    for (int tx = 0; tx < 32; tx++) {
      uint16_t e = tx < 31 ? p->vram[(map + ty * 32 + tx + 1) & 0x7FFF] : p->vram[(map + ((ty + 1) & 31) * 32) & 0x7FFF];
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
          int c = tilePixel(p, chr, e, x, y);
          g_e3Orig[ty * 8 + y][tx * 8 + x] = (uint8_t)(c ? ((e >> 10) & 7) * 16 + c : 0);
        }
    }
  memset(g_e3Art, 0, sizeof g_e3Art);
  for (size_t s = 0; s < sizeof kE3Stamps / sizeof kE3Stamps[0]; s++) {
    const CtArtStamp* t = &kE3Stamps[s];
    for (int j = 0; j < t->h; j++)
      for (int i = 0; i < t->w; i++) {
        int sx = t->su + (t->flip ? t->w - 1 - i : i), sy = t->sy + j, dx = t->du + i + E3_ART_W, dy = t->dy + j;
        if (sx < 0 || sx > 255 || sy < 0 || sy > 255 || dx < 0 || dx >= E3_ART_W || dy < 0 || dy >= E3_ART_H) continue;
        if (g_e3Orig[sy][sx]) g_e3Art[dy][dx] = g_e3Orig[sy][sx];
      }
  }
  size_t n = 0;
  for (size_t i = 0; i + 1 < sizeof kE3Px && n < sizeof g_e3Art; i += 2)
    for (int k = 0; k < kE3Px[i] && n < sizeof g_e3Art; k++, n++)
      if (kE3Px[i + 1]) ((uint8_t*)g_e3Art)[n] = (kE3Px[i + 1] & 15) ? kE3Px[i + 1] : 0;
}

// THE STEAM RELEASE'S ART FOR IT (the Steam margins, a mod: settings.h wideSteamMargins, from the player's ct_wide.dat):
// the machine's field location 06B (where the story calls `FF 23`) has margins whose cells are tiles that release added
// for this scene -- its three sheets cgext107_0-2 (pages 2-4): the wings' pointed ends past the machine (plane 2, BG1's
// low layer), the sky's glow on (plane 0, the field's BG1 rows 3-4 the stage shows through BG2 above line 70) and the
// planet (plane 3, BG2's: not drawn -- see the planet below). Its column c is the room's metatile column c - x, x = (w - 16) / 2. A cell of the room's own
// metatiles (the sky's, rows 0-2) is left to the rules below. NULL = not drawn.
static const CtWideExt* g_e3Steam;
static CtWideExt g_e3SteamExt;
static int g_e3Own = 1;                                               // the scene adjustments' own art
// The Steam sheet's CGRAM index at the room's pixel (x, y) of a plane, its cell's page moved on by dpage (| 0x80: the
// sheet's tile row under the cell's): 0 = nothing there, -1 = not a sheet's cell (the room's own metatiles).
static int steamPx(int plane, int x, int y, int dpage) {
  const CtWideExt* e = g_e3Steam;
  int c = (x >> 4) + (e->w - 16) / 2, r = y >> 4;
  if (c < 0 || c >= e->w || r < 0 || r >= e->h) return 0;
  unsigned v = e->planes[(size_t)plane * e->w * e->h + (size_t)r * e->w + c];
  if (!v) return 0;
  unsigned t = v - 1, page = (t >> 8) + (unsigned)(dpage & 0x7F), trow = ((t >> 4) & 15) + (dpage & 0x80 ? 1 : 0);
  if ((t >> 8) < 1 || page > 7 || !e->pageSheet[page] || trow > 15) return -1;
  return e->pageSheet[page][(trow * 16 + (y & 15)) * 256 + (t & 15) * 16 + (x & 15)];
}
// The art's band index at the room's pixel (x, y) of the glow's cells (0 = none): the band runs ~5 pixels past the glow's
// cells (sky rows 3-4) into the sheet's next tile row, which no cell of the map names -- below the cells it is read on from
// that row, as one picture.
static int bandRow(int x, int y) {
  int b = steamPx(0, x, y, 2);
  if (b <= 0 && y >= 80) b = steamPx(0, x, y - 16, 0x82);
  return b > 0 ? b : 0;
}
// THE ROUND PLANET (the Steam release's art): its glow (plane 0's cells, sheet 0) bends down towards the sides, over the
// planet; the same cells two pages on (sheet 2) are the air's band under the glow, palette 1's 1-3 -- the stage's own CGRAM
// 0x11-0x13, the same colours -- ADDED to what lies under it: the Steam picture's light blue under the glow's white line is
// the band plus the planet, (65,82,115) + (8,56,112) = the screenshot's (73,138,227) -- the owner 2026-10-25: "should
// be shown correctly", not dropped and not drawn flat. The stage shows the sky's map
// above line 70 only (HDMA channel 3), so in the sides the bend is laid at the sky's own rows on every line -- the row of
// line l is l + the sky lines' V scroll -- and the planet shows only below it. Past the art's own columns (a 21:9 side is
// wider than its 96) the bend goes on: the edge column, lowered along its own slope (g_e3Slope, pixels a column).
static int g_e3SkyVofs;
// The planet's map and V scroll (its lines', below the switch): under the band on a sky line the planet is read as it would
// lie there, so the band's first row, where the middle's glow ends above line 70, is not the band over the night's black.
static unsigned g_e3PlanetSc = 0x6A;
static int g_e3PlanetVofs;
static int bg2Index(const CtSeqPic* p, unsigned sc, int mx, int my);
static int planetUnder(const CtSeqPic* p, int sky, int mx, int line, int my) {
  return sky ? bg2Index(p, g_e3PlanetSc, mx, (line + g_e3PlanetVofs) & 0x3FF) : bg2Index(p, p->r[0x08], mx, my);
}
static float g_e3Slope[2];
// The Steam art was painted for the field location 06B's rows of the palette; the stage draws the same colours from other
// rows and fades those: its sky is 4:15 (the art's 2:10, the same 000010, on a row nothing fades). Such an index takes the
// stage's row of the same colour, so the sides go grey with the middle in the flash and fade in with it.
static int horizonIndex(int v) {
  switch (v) {
    case 0x2A: return 0x4F;                                          // (the sky: 2:10 -> 4:15, 000010 both)
  }
  return v;
}
// THE GLOW WHOLE (the Steam release shows the field 06B, where the sky's glow -- its map's rows 6-9, y 48-79 -- shows
// whole, the art's band drawn against it): the stage switches BG2 from the sky's map to the planet's at line 70 (HDMA
// channel 3), cutting the glow's last rows off, and the art's band at its inner columns ends those rows lower -- a step at
// the 256. With the sides drawn, the glow's rows go on below the switch line over the planet, the middle's too (the sky's
// map at its own row: the line plus the sky lines' V scroll), so middle and sides are one band, as on Steam.
static int skyMapPixel(const CtSeqPic* p, int col, int ys) {
  const unsigned chr = (unsigned)(p->r[0x0B] >> 4) << 12;
  uint16_t e = p->vram[(0x6000 + (unsigned)(ys >> 3) * 32 + (unsigned)((col >> 3) & 31)) & 0x7FFF];
  int c = tilePixel(p, chr, e, col & 7, ys & 7);
  return c ? (int)((e >> 10) & 7) * 16 + c : 0;
}
// The glow's index at the side's column x and sky row ys (> 0), 0 = none there, -1 = not a sheet's cell; *band the air's
// band index there (0 = none), under the glow. The glow's lowest row -- a dark one under its white line -- gives way to
// the band, which lies under it there too: drawn, it stood as a pixel's gap between the white and the band (the owner,
// 2026-10-25: "1 pixel extra between the horizon's upper and lower parts").
static int horizonPx(int x, int ys, int* band) {
  const int ew = (g_e3Steam->w - 16) / 2 * 16;
  int xe = x, dy = 0;
  if (x < -ew) { xe = -ew; dy = (int)((-ew - x) * g_e3Slope[0] + 0.5f); }
  else if (x >= 256 + ew) { xe = 255 + ew; dy = (int)((x - 255 - ew) * g_e3Slope[1] + 0.5f); }
  int v = steamPx(0, xe, ys - dy, 0);
  *band = bandRow(xe, ys - dy);
  if (v > 0 && *band && steamPx(0, xe, ys - dy + 1, 0) <= 0) v = 0;   // (the glow's lowest row: the band's)
  return v > 0 ? horizonIndex(v) : v;
}
// BG2's CGRAM index at its map place (mx, my) of the map sc (BG2SC; 0 = see-through).
static int bg2Index(const CtSeqPic* p, unsigned sc, int mx, int my) {
  const unsigned chr = (unsigned)(p->r[0x0B] >> 4) << 12;
  mx &= 0x3FF; my &= 0x3FF;
  unsigned ta = ((sc & 0xFC) << 8) + (unsigned)((my >> 3) & 31) * 32 + (unsigned)((mx >> 3) & 31);
  if ((mx & 256) && (sc & 1)) ta += 0x400;
  if ((my & 256) && (sc & 2)) ta += (sc & 1) ? 0x800 : 0x400;
  uint16_t e = p->vram[ta & 0x7FFF];
  int c = tilePixel(p, chr, e, mx & 7, my & 7);
  return c ? (int)((e >> 10) & 7) * 16 + c : 0;
}
// The band added to the colour under it (colour math's add, each channel held at 31), in direct colour.
static int bandOver(const CtSeqPic* p, int band, int under) {
  uint16_t a = p->cgram[band & 0xFF], b = p->cgram[under & 0xFF];
  int r = (a & 31) + (b & 31), g = ((a >> 5) & 31) + ((b >> 5) & 31), bl = ((a >> 10) & 31) + ((b >> 10) & 31);
  return 0x10000 | (r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (bl > 31 ? 31 : bl) << 10;
}
// The glow's lowest row in a column (its sky rows 48-79), for the slope past the art.
static int glowBottom(int x) {
  int b = 48;
  for (int y = 48; y < 80; y++) if (steamPx(0, x, y, 0) > 0) b = y;
  return b;
}

// THE BAND UNDER THE MIDDLE'S GLOW (the owner, 2026-10-25: "the band in the middle too"). The art has no cells over the
// 256 (the stage draws its own glow there), so the band the art lays under its innermost column's glow -- its rows from
// that glow's lowest (which gives way to it, as in the sides), 5 of them -- goes on under the middle's glow, from each
// column's own lowest glow row: the middle's glow bends as the art's does (its lowest row 74 at the edges, 70 in the
// middle), and at the 256 the two meet, both ending at 74.
static uint8_t g_e3Prof[2][8];                                        // (the band's rows under the glow, left / right)
static int g_e3ProfN[2];
static int8_t g_e3MidGb[256];                                         // (the middle's lowest glow row, a column; -1 none)
static void bandProfile(int side, int x) {
  int gb = glowBottom(x), n = 0;
  while (n < 8 && bandRow(x, gb + n)) { g_e3Prof[side][n] = (uint8_t)bandRow(x, gb + n); n++; }
  g_e3ProfN[side] = n;
}
static void midGlowBottoms(const CtSeqPic* p) {
  for (int x = 0; x < 256; x++) {
    int b = -1;
    for (int y = 48; y < 80; y++) if (skyMapPixel(p, x, y)) b = y;
    g_e3MidGb[x] = (int8_t)b;
  }
}

void CtSceneArtEntry3Set(int steam, int own) {
  g_e3Steam = steam && CtWideExtGet(0x06B, &g_e3SteamExt) ? &g_e3SteamExt : NULL;
  g_e3Own = own;
  if (g_e3Steam) {
    const int ew = (g_e3Steam->w - 16) / 2 * 16;
    g_e3Slope[0] = (glowBottom(-ew) - glowBottom(-ew + 16)) / 16.0f;
    g_e3Slope[1] = (glowBottom(255 + ew) - glowBottom(255 + ew - 16)) / 16.0f;
    if (g_e3Slope[0] < 0) g_e3Slope[0] = 0;
    if (g_e3Slope[1] < 0) g_e3Slope[1] = 0;
    bandProfile(0, -1); bandProfile(1, 256);
  }
}

int CtSceneArtEntry3(const CtSeqPic* p, int layer, int* pmx, int my, int pri) {
  const int mx = *pmx;
  if (layer == -1) { entry3Build(p); if (g_e3Steam) midGlowBottoms(p); return -1; }                    // (a new frame)
  if (layer == -2) {                                                 // (a line, in order: the sky lines' V scroll)
    if ((p->r[0x08] & 0xFC) == 0x60) g_e3SkyVofs = p->vofs[1];
    else { g_e3PlanetSc = p->r[0x08]; g_e3PlanetVofs = p->vofs[1]; }
    return -1;
  }
  // THE SKY: above line 70 BG2 is the field's map $6000 (HDMA channel 3 switches BG2SC $60 -> $6A there), one screen
  // wide: the red moon in its rows 0-3, columns 14-22, over tile $101's sky. Past the 256 that map would repeat the moon
  // on both sides; there the sky's rows take the moonless column 0 (the night's own tile).
  // Below, the planet (and the sky's glow): BG2's maps are one screen wide too, so a side shows the screen's own columns
  // mirrored about its edge -- no seam where the 256 end, where a repeat broke the clouds.
  if (layer == 1) {
    int hs = p->hofs[1] & 0x3FF, x;
    if (hs >= 512) hs -= 1024;
    x = mx - hs;
    const int sky = (p->r[0x08] & 0xFC) == 0x60, line = (my - p->vofs[1]) & 0x3FF;
    const int ys = (line + g_e3SkyVofs) & 255;
    if (g_e3Steam && x >= 0 && x < 256 && g_e3MidGb[x] >= 0) {      // (the band under the middle's glow, its lowest row on)
      const int side = x >= 128, k = ys - g_e3MidGb[x];
      if (k >= 0 && k < g_e3ProfN[side]) {
        if (pri) return 0;
        return bandOver(p, g_e3Prof[side][k], planetUnder(p, sky, mx, line, my));
      }
    }
    if (g_e3Steam && (x < 0 || x >= 256)) {                          // (the Steam release's glow, bent, a mod)
      int band, h = horizonPx(x, ys, &band);
      if (h > 0) return pri ? 0 : h;
      if (h < 0 && !sky) { int c = skyMapPixel(p, 0, ys); return pri ? 0 : c; }   // (the night's tile on a planet's line)
      if (band) {                                                    // (the air's band over the sky or the planet)
        if (pri) return 0;
        return bandOver(p, band, planetUnder(p, sky, (x < 0 ? -1 - x : 511 - x) + hs, line, my));
      }
    } else if (!sky && ys >= 48 && ys < 80) {                        // (the glow whole, the middle's and the mirrored sides')
      int c = skyMapPixel(p, x < 0 ? -1 - x : x >= 256 ? 511 - x : x, ys);
      if (c) return pri ? 0 : c;
    }
    if (x >= 0 && x < 256) return -1;
    // (Below the bend the planet is the stage's own, mirrored about the screen's edge as below: the Steam art's planet,
    //  plane 3, is drawn anew and does not join the stage's clouds at the 256 -- a seam the whole scene long.)
    if ((p->r[0x08] & 0xFC) == 0x60 && (my & 255) < 32) { *pmx = mx & 7; return -1; }
    *pmx = (x < 0 ? -1 - x : 511 - x) + hs;
    return -1;
  }
  if (layer != 0 || (mx >= 8 && mx < 256)) return -1;                // (BG1 inside its own 256 columns: the ROM's)
  if (pri != E3_ART_PRI) return 0;                                   // (the machine's tiles are all of priority 0)
  int u = mx - 8, y = my & 255;                                      // u: the field's column (the machine's middle at 127.5)
  if (g_e3Steam && (u < 0 || u >= 256)) { int v = steamPx(2, u, y, 0); if (v > 0) return v; }   // (the Steam release's wings)
  if (u >= 128) u = 255 - u;                                         // the right side: the left's mirror
  if (u >= 0) return g_e3Orig[y][u];                                 // (the field's own columns past the display's wrap)
  int col = u + E3_ART_W;
  return col < 0 || !g_e3Own ? 0 : g_e3Art[y][col];                // (the adjustments' own art, when on)
}

// ENTRY 6 (`FF 26`, the Epoch into Lavos's shell). Its BG1 in mode 1 is one screen -- the cockpit's dashboard on the lower
// lines (the tunnel above them is mode 7, which the art is not asked for), the Epoch in the dive -- so the sides showed the
// dashboard three times. Past the 256 its BG1 shows nothing but, with the Steam margins on, the Steam release's cockpit walls
// (40_kokscr_ext, halved to the SNES's pixels by tools/steam_wide.py: the left wall its columns 0-95, the right 160-255, rows
// 96-175, standing on the picture's bottom row beside the dashboard -- their canopy's edge meets the dashboard's at row 168).
static const uint8_t* g_e6Img;
static int g_e6W, g_e6H;
void CtSceneArtEntry6Set(int steam) { g_e6Img = steam ? CtWideExtImage("40_kokscr_ext.png", &g_e6W, &g_e6H) : NULL; }
int CtSceneArtEntry6(const CtSeqPic* p, int layer, int* pmx, int my, int pri) {
  (void)pri;
  if (layer != 0) return -1;
  int hs = p->hofs[0] & 0x3FF;
  if (hs >= 512) hs -= 1024;
  const int x = *pmx - hs, row = ((my - p->vofs[0]) & 0x3FF) - 1;   // (the screen's column and row)
  if (x >= 0 && x < 256) return -1;
  // (Where nothing stands the side is black: the dashboard covers the whole 256 on its lines, the backdrop -- cyan -- never
  //  shows there.)
  if (!g_e6Img || g_e6W < 256 || g_e6H < 176) return 0x10000;
  const int ix = x < 0 ? x + 96 : x - 96, iy = row - 48;
  if ((x < 0 && ix < 0) || (x >= 256 && ix >= 256) || iy < 96 || iy >= 176) return 0x10000;
  const uint8_t* q = g_e6Img + 4 * ((size_t)iy * g_e6W + ix);
  if (q[3] < 128) return 0x10000;
  return 0x10000 | (q[0] >> 3) | (q[1] >> 3) << 5 | (q[2] >> 3) << 10;
}
