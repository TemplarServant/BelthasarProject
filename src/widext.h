// widext.h - a MOD's data (settings.h wideSteamMargins, ROADMAP X4): the Steam release's widescreen margins, the strip
// past each room's scroll mask, read from the player's own copy by tools/steam_wide.py into a local ct_wide.dat (never
// shipped). Outside the ROM mandate (docs/DECISIONS.md, the mods): nothing here runs with the option off.
#ifndef CT_WIDEXT_H
#define CT_WIDEXT_H
#include <stdint.h>

// One location's margins: four planes of w*h metatile ids (0 = none) into its own BG1/BG2 assembly, drawn from plane 3
// (the ground) up to plane 0. Column c is the room's metatile column c - x, x = (w - (right + 1)) / 2 (`right` the
// scroll mask's last column); row r is the room's row r.
typedef struct { int w, h; const uint16_t* planes; } CtWideExt;

int CtWideExtGet(int location, CtWideExt* out);   // 1 = ct_wide.dat (CT_WIDE_DAT overrides the path) has this location

#endif
