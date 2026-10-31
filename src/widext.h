// widext.h -- a mod's data (settings.h wideSteamMargins, ROADMAP X4): the Steam release's widescreen margins, the strip
// past each room's scroll mask, read from the player's own copy by tools/steam_wide.py into a local ct_wide.dat (never
// shipped). Outside the ROM mandate (docs/DECISIONS.md, the mods): nothing here runs with the option off.
#ifndef CT_WIDEXT_H
#define CT_WIDEXT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// One location's margins: five planes of w*h cells (0 = none, v = metatile v - 1 of page (v - 1) >> 8), the Steam
// release's map layers: plane 3 with BG2's low-priority tiles, plane 2 with BG1's, planes 1 and 0 the priority-1 layer
// above the sprites, plane 4 (zero but in a few locations) drawn opaque with BG3. Column c is the room's metatile column
// c - x, x = (w - (right + 1)) / 2 (`right` the scroll mask's last column); row r is the room's row r. Pages 0 and 1 are
// metatiles drawn with the Steam release's tiles: its chip sets for BG1/BG2 slots 0-5 (128 chips of 64 pixels, 0..15;
// NULL = the ROM's slot) and its definitions of the metatiles the margins use (metaIds[i]'s four corners at metaCorners +
// 16 i: u16 chip in the scene's chip space, u8 palette row, u8 flags H 1 / V 2 / priority 4). A page in pageSheet (pages
// 1-7) is a 256 x 256 sheet of CGRAM indices (0 = nothing) instead: the tiles the Steam release added. steamPal: the
// Steam release's colours of the location's CGRAM 0-127 (128 LE BGR555; NULL = none in the file) -- never drawn (the port
// keeps the ROM's), the key to what a sheet's index stands for (scene.c CtFieldSceneBakeExt).
typedef struct {
  int w, h; const uint16_t* planes;
  const uint8_t* slotChips[6];
  int nMeta; const uint16_t* metaIds; const uint8_t* metaCorners;
  const uint8_t* pageSheet[8];
  const uint8_t* steamPal;
} CtWideExt;

int CtWideExtGet(int location, CtWideExt* out);   // 1 = ct_wide.dat (CT_WIDE_DAT overrides the path) has this location
int CtWideExtAvailable(void);                     // 1 = a ct_wide.dat was read (the setup writes one from a Steam copy it finds)
// A special scene's extension art by its name in the Steam release ("40_kokscr_ext.png"): w x h RGBA at the SNES's
// pixels, NULL = not in the player's ct_wide.dat (an older file may lack them: the setup writes it again).
const uint8_t* CtWideExtImage(const char* name, int* w, int* h);

#ifdef __cplusplus
}
#endif

#endif
