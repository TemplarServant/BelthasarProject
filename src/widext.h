// widext.h - a MOD's data (settings.h wideSteamMargins, ROADMAP X4): the Steam release's widescreen margins, the strip
// past each room's scroll mask, read from the player's own copy by tools/steam_wide.py into a local ct_wide.dat (never
// shipped). Outside the ROM mandate (docs/DECISIONS.md, the mods): nothing here runs with the option off.
#ifndef CT_WIDEXT_H
#define CT_WIDEXT_H
#include <stdint.h>

// One location's margins: four planes of w*h metatile ids (0 = none) drawn from plane 3 (the ground) up to plane 0.
// Column c is the room's metatile column c - x, x = (w - (right + 1)) / 2 (`right` the scroll mask's last column); row
// r is the room's row r. They are drawn with the Steam release's tiles: its chip sets for BG1/BG2 slots 0-5 (128 chips
// of 64 pixels, 0..15; NULL = the ROM's slot) and its definitions of the metatiles the margins use (metaIds[i]'s four
// corners at metaCorners + 16 i: u16 chip in the scene's chip space, u8 palette row, u8 flags H 1 / V 2 / priority 4),
// in the Steam release's colours for the location (it recoloured rooms; the margins are drawn for its colours).
typedef struct {
  int w, h; const uint16_t* planes;
  const uint8_t* slotChips[6];
  int nMeta; const uint16_t* metaIds; const uint8_t* metaCorners;
  const uint8_t* pal555;          // the Steam release's palette for the location: 128 BGR555 colours (LE); NULL = none
} CtWideExt;

int CtWideExtGet(int location, CtWideExt* out);   // 1 = ct_wide.dat (CT_WIDE_DAT overrides the path) has this location

#endif
