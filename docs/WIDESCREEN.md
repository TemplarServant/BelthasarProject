# The widescreen and the frames between (mods)

Belthasar's widescreen and its "frames between" (60+ fps) are **mods**: player options in the F1 overlay, off by
default, outside the ROM mandate ([DECISIONS.md](DECISIONS.md#2026-10-10-mods-are-outside-the-rom-mandate)). With every
option at its default -- and in every headless run of the suite -- the game is the cartridge's, byte for byte. This page
says what each option draws that the SNES does not, and why. The history of each choice is in
[ROADMAP.md](ROADMAP.md) X2-X11; the owner's decisions are dated there and in [DECISIONS.md](DECISIONS.md).

| Option (F1, Video) | Setting (`ct_settings.ini`) | What it does |
|---|---|---|
| Widescreen 16:9 / 16:10 / 21:9 | `widescreen` | more picture at the sides of the field, the world map, fights and the special sequences |
| Steam margins | `steam_margins` | past a room's edge, the strip the Steam release draws there (needs a Steam copy; greyed without one) |
| Fill the bottom lines | `fill_bottom_lines` | the field's and the fights' lines 211-223, which the SNES blanks, drawn; the dialogue window, the fight's HUD and its message window lowered in proportion |
| Frames between (60+ fps) | `interpolation` | pictures between two game frames on a faster display |

## The camera: the Steam release's

The Steam release (2018-2023) is the reference for all widescreen work (the owner, 2026-10-23). Its field node is the
SNES picture scaled into a 16:9 design with **the 256 columns always centred on the ROM's camera** (the executable,
0x5889D0): the picture never stops at a room's edge, so past the edge the sides show what lies beyond it. The port keeps
the SNES's 224 lines and the ROM's vertical camera: nothing of the Steam release's phone heritage (its 192-line crop,
its own vertical camera) is ported (the owner, 2026-10-23). The ROM's camera and everything it decides are untouched;
`fieldWideViewX` only places the view in the middle of the wider picture.

## Past a room's edge

What the sides show where the room ends, in order:

1. **The Steam margins** (option on and `ct_wide.dat` present): the Steam release's own strip for that room
   (`Game/field/mapext_bin/mapext_<location>.bin`: four planes plus an optional fifth, drawn with the Steam release's
   tiles in the ROM's palette). Plane 3 is BG2's low layer and scrolls with BG2; planes 2 / 1-0 are BG1's low / high
   layers; the fifth is opaque with BG3. A margin of one cell everywhere (266 of 412 locations: the dark around an
   interior) is a fill and yields to the room's own continuation. The margins have 4 to 6 columns a side; past them
   (a 16:9 side is 71 px: the fair's 005 has 64) the side goes on as below, never as the room's own layers again.
2. **A background** -- a layer the ROM marks as detached from the camera or scrolling on its own (`$1D35` / `$1D36`,
   from the map header md[1..3]) -- goes on: the Blackbird's clouds (16E), Flea's stars (0AD), Death Peak's snow (104).
   A still backdrop (detached, no speed: the skywalk's 01C sky) is painted for 256 columns and is **mirrored** about the
   view, not repeated (X4b). Four exceptions:
   - a background the room's front layers **cover everywhere** is no sky -- the SNES shows it only through a window: the
     Telepod's warp swirl in 008 (`fieldHiddenBg`);
   - **without the Steam margins drawn**, a side where the Steam release draws scenery over the background ends black:
     `kSteamCovers` in `src/field.c`, 42 locations, location numbers and sides only, printed by
     `tools/steam_bgcover.py` from a Steam copy (the skywalk past its tower wall, 01C);
   - **with the Steam margins drawn**, such a side ends black past the margins' own columns: a 16:9 side ends inside
     them, a 21:9 one reaches past them, where the skywalk's sky showed again beyond its tower wall (01C);
   - Magus's castle (0A5): its BG2 map begins with 32 flat grey rows over the sky, 5 lines of which show at the top of
     the pan; with the scene adjustments on (below) the sky is drawn 5 lines higher.
3. Otherwise **black**, as the room's own end.

Past a world map's own patch see the scene adjustments (1999 A.D.). The world map's sides are composed from a picture-only map of 128 columns round the camera (`CtWorldTilemapView`):
the streamed 64-column ring (512 px) is narrower than a 21:9 picture, whose left edge showed the far side of the ring.

The Steam release has no margins for the world maps (they are the SNES's own size) nor for the special sequences but
one (the Epoch's cockpit into Lavos): past the world map's edge, and beside the Black Omen panorama's machinery, the
sides need new art (the owner's to draw; `build/_steamre/wide_art_todo.csv` was the list of sides the margins leave
short at 16:9).

## Objects in the sides

The ROM latches and draws an object only inside its camera window (C0/A88D: two tiles past the 256 columns). An object
that is live and stands only in the extra columns is drawn by the widescreen itself, in the picture only: its own
animation stepped by the port's model of the ROM's ticker on a play state of its own, nothing written to the game
(`sidePlay` in `src/field.c`; an unlatched object whose sprite cannot reach the view is the sides' -- Crono in the robot
fight). On the world map, an object killed by its own visibility test (C2/78A1: the view plus 16 px -- the new-game
pan's gulls) is drawn on past the 256 columns until it leaves the wide picture: when its script stood on op 0x31 (move +
animate, the gulls' whole flight) a copy of its block takes that op's steps -- its 16.16 velocity and its anim sub-VM,
the flaps and the 30-frame glide at their own dwell (`owGhosts`, world.c `CtWorldGhostStep`); the game's state is not
changed.

## The special sequences and fights

The `$C3` stages (the race, the panoramas, the ending) draw their sides from their own layers at their own scroll
(`seq_pic.c`, `g_seqPicWideSide`); a stage's scene layer whose map is one screen wide is left out of the sides instead
of repeated (`g_seqPicWideSkip`: the Black Omen panorama's machinery). A fight's arena is composed as wide as the
picture round the battle's own 256 view, its colour math and the HUD's bands by line (`btFxCompose`).

## Scene adjustments

A mod of its own (`[video] scene_adjustments`, settings.h `sceneAdjust`, off by default, greyed without the widescreen):
art direction for the wide picture, scene by scene, where what the sides show by the rules above is wrong for the scene.

- **1999 A.D.'s world map (world 7, the attract's Day of Lavos):** its map is a patch of the world round Truce and
  Guardia (74 x 70 blocks; the rest of its grids empty -- VGMaps' rip is the same patch, and the Steam release has no
  world-map margins), and the camera stops at the patch's edge, where the sides were black. Past it they show 1000 A.D.'s
  geography (world 0's block grids from ct_assets.dat) drawn with 1999's own blocks, tiles and colours -- 92-95% of the
  terrain blocks past the patch are the same in both worlds; where the patch was redrawn the coast does not join yet
  (the owner adjusts it later, 2026-10-25). `CtWorldStream.ext`, `CtWorldTilemapView`.
  **The map editor** paints those cells: `python tools/map_editor.py` (RPG Maker style: a block palette, the map with
  zoom and scrolling, BG2 ground / BG1 overlay, pencil, rectangle, fill, erase -- nothing on BG1, the open sea on BG2 --, restore -- back to 1000 A.D.'s block --, pick,
  undo, the patch's border; the ROM's patch is locked). It draws from the player's ct_assets.dat and saves block
  numbers only to `ct_mapedits.dat` (CT_MAPEDITS, else the working folder, else the executable's), which Belthasar
  reads every 60 frames: paint with the game open and the sides follow (`owExtGrids` in `src/field.c`).
- **Magus's castle (0A5):** the sky is drawn 5 lines higher for the whole visit (BG2's offset in the picture only,
  `fieldWideBg2Dy`), so the pan ends with the band of flat grey over it just above the screen (the owner, 2026-10-24).
- **The machine rising over the planet (sequence entry 3: `FF 23` after the Blackbird, `FF 63` in the attract):** its
  layers are one screen wide, so past the 256 the red moon stood three times and the planet repeated. The sides' sky
  rows take the moonless column and the planet is mirrored about the screen's edge (`src/scene_art.c`, the stage's
  `g_seqPicArt`). Room for new art of the machine's own past its edge is there (stamps of its layer by place, cut at run
  time from the player's VRAM, and new pixels: `src/art/entry3_sides.h`, generated by `tools/scene_art_entry3.py`; no ROM
  pixel in it) -- none is drawn yet.

## The Steam release's art for a special scene

The machine rising over the planet (sequence entry 3) draws its BG1 -- and through BG2, above line 70, the sky's glow --
from the field location where the story calls `FF 23`, 06B, whose Steam margins (`mapext_107`) are cells of three sheets
of tiles that release drew for it (`cgext107_0`-`2`, pages 2-4): plane 2 the wings' pointed ends past the machine (80
px a side, BG1's low layer), plane 0 the glow (the field's rows 3-4), plane 3 the planet (BG2). With the Steam margins
on, the stage's sides draw them (`CtSceneArtEntry3Set`, the attract's 1B1 too: the same machine). The planet is round
there: the glow bends down towards the sides, laid at the sky rows' own place on every line (the line plus the sky
lines' V scroll). Under it lies the air's band, plane 0's cells two pages on (`cgext107_2`, palette 1's 1-3 -- the stage's
own CGRAM 0x11-0x13, the same colours), ADDED to the planet under it: the Steam capture's light blue under the glow's
white line is exactly band + planet ((65,82,115) + (8,56,112) = its (73,138,227)). The band runs ~5 px past the glow's
cells into the sheet's next tile row, which no map cell names; it is read on from there as one picture. The art has no
cells over the 256, so the band goes on under the middle's own glow with the art's innermost column's profile (its 5 rows
from the glow's lowest), from each column's lowest glow row: the middle's glow bends too (74 at the edges, 70 in the
middle) and meets the art's at 74, so the band is one across the picture (the owner, 2026-10-25). The glow's lowest row
-- a dark one under its white line -- gives way to the band, which lies under it there too: drawn, it stood as a pixel's
gap between the white and the band (the owner). On a sky line (above the switch) the band is added to the planet as it
would lie there (the planet's map at the planet lines' V scroll), not to the night's black. Past the art's own 96
columns (a 21:9 side is 133) the bend goes on along the planet's limb: the glow's lowest row, the middle's and the
art's, lies on one curve (70 in the middle, 74 at the 256, 78 at 76 columns out), fitted each frame as q0 + q2 (x -
127.5)^2; from ~80 columns out the art's cells cut the glow's lowest rows (they stay at row 79, a flat edge that made
21:9 run on flat -- the owner, 2026-10-25), so past the art's last uncut column a column is that column lowered by the
curve. 16:9 (71 columns a side) never reaches the cut and is unchanged. The middle's glow starts at its map's row 6 in
every column while its lowest row bends, so in the flash (which whitens its dark top rows) it stood as a flat block over
the sides' curve: its rows above its lowest row less the art's glow thickness are the sky, and its top bends with the
bottom and meets the art's at the 256 (the owner, 2026-10-25). The stage cuts the sky's map at line 70
(HDMA channel 3), the glow's last rows with it; the Steam release shows the field 06B, the glow whole, and its art is
drawn against that -- so with the sides drawn the glow's rows go on below the switch line, the middle's too, one band as
on Steam (the owner's reference, a Steam 16:9 capture, 2026-10-25). Below it the planet is the stage's own, mirrored
about the screen's edge: the art's planet (plane 3) is new art that never joins the stage's clouds. The art's sky sits on
a palette row the stage never fades (2:10); it takes the stage's row of the same colour (4:15), so the sides fade and
flash with the middle. The sky's
rows of the room's own metatiles keep the rules above (one moon).

Both `FF 23` and the attract's `FF 63` move the machine with mode 2's offset per tile: a triangle wave of 32 entries
in BG3's map that the stage scrolls through BG3's H scroll. Past the screen's edge the sides read the same row on, at
their columns' place in that map (it wraps at 32 entries, as in the middle; `optSide` in `src/seq_pic.c`, only while a
stage's art draws its sides), so the wave travels through the sides with the middle. (Before 2026-10-25 they carried
the line through the two edge entries on, which ran off the triangle: the sides jumped and stretched, the owner.) (Until 2026-10-25 this
document said the Steam release had no art for the scene: its `Game/special` folder has none, the margins of 06B do.)

## The special scenes past their 256

A native `$C3` stage's sides show its layers at their own place past the screen's edge (`seq_pic.c`). A layer drawn as one
screen -- a scene, not a background -- would show the same picture again in each side, so per stage (`stageWideBegin` in
`src/field.c`, `g_seqPicWideSkip` / `g_seqPicWideSkipModes`):

- **the race** (and the attract's): its road, ground and mountains are made to wrap -- they go on;
- **entry 4, Lavos falls**: mode 7 and the stars -- the sides are space (the stars are sprites, the middle's only);
- **entry 7, the Day of Lavos**: the planet (BG1) and "BUT... THE FUTURE REFUSED TO CHANGE." stay out -- they stood three
  times;
- **entry 6, the Epoch into Lavos**: the cockpit's dashboard (BG1 in mode 1 on the lower lines) stays out and the sides
  there are black; with the Steam margins on, the Steam release's cockpit walls stand beside it (its
  `into_lavos/40_kokscr_ext.png`, drawn at twice the SNES's resolution: `tools/steam_wide.py` halves it into
  `ct_wide.dat`'s `SPCL` section; the walls 96 x 80 pixels a side on the picture's bottom, their canopy meeting the
  dashboard's edge at row 168; drawn in their own colours, under the scene's colour math and brightness);
- **the endings** (`FF 2A` / `2B` / `2E` / `2F`): the world in mode 7 and the clouds in mode 1 go on; in mode 4 the
  planet (BG1) and the credits (BG2) stay out -- "The End" stood three times;
- **entry 3**: above.

Sprites only on the sub screen were repeated 256 columns along (snes/ppu.c's rule for the gate warp's tunnel texture);
in the native stages they are text and objects (the credits, entry 7's line), so they are not (`g_seqPicWideSubTexture`).

## Fill the bottom lines

The SNES's V-IRQ blanks lines 211-223 of the field and of fights (old TVs hid them). The option draws them. The SNES
shows the row `camy + 1` on line 0, so with the ROM's camera at a room's bottom the last line is one row past the room
(the next room, or the map wrapping to its top). In the field the option holds the picture's origin there instead: the
room's last row stays on line 223, and the camera's last pixel down moves the objects on the screen, not the picture
(2026-10-25; until then the line repeated the room's last row, which read as a doubled line).

## The frames between

On a display faster than 60 Hz, Belthasar draws pictures between two game frames at `t` (0-256): the field natively
from its layers (`fieldnative.c`), the world map natively (`owRasterNative`), anything else by reprojecting the frame's
owners (`motion.c` `CtMotRaster`). Every moving thing is placed at a **continuous place** fitted over the last 32 frames
(`CtMotFit`): the camera, each object, each self-scrolling layer -- so a pan of 0.75 px a frame (the Blackbird) moves
0.75 px a frame instead of 1, 1, 1, 0.

- **The rate comes from the engine where it keeps one** (`CtMotEngine`, 2026-10-24). The field's camera carries the
  remainder C0/92CA leaves in `$1D3B/$1D3C` (sixteenths, cut toward zero) and every object its 12.4 words
  `$1800/$1880,X`; a frame's rate is their step, used whenever the drawn place stepped as their whole part did. The
  fit over the history is the fallback (the layers, the overworld, the fight, a picture held behind its camera).
  Measured on the attract (`CT_MOTFITTRACE=2`, against `CT_MOTENGINE=0`): the frames whose shown step is off the
  engine's by more than 0.1 px fall from 71% to 10% for the objects (mean 0.30 -> 0.04 px) and from 6.5% to 0.6% for
  the camera; what remains is the trailing rule re-phasing a motion's first frames.

- A continuous place **trails** the drawn one (never past it): an object that stops -- at a wall -- reaches the drawn
  place and stays.
- A fit needs two steps and a steady slope; with none it goes on at the last frames' mean rate from where it was, and
  settles on the drawn place when the motion is whole pixels a frame. It never jumps.
- A layer whose offset is the same all along the history moves exactly with the camera; one held to the screen stays
  there; a parallax that only steps when the camera moves stops with it.
- A layer that moves on its own while the camera moves the other way (the 1000 A.D. intro's clouds, 1F0: the camera
  climbing a pixel every 4 frames, the clouds' drift going down one every 8) is no steady line on the map; its own
  offset is, so it is placed at its offset's continuous place plus the camera's.
- The Steam margins are world pixels (`CtMotFrame.world`): plane 3 at BG2's place at `t`, the rest at the camera's.
- `CT_NATIVECHECK=1` grades the drawers at the frame's whole places (`g_ctMotNoFrac`): the native picture at t = 256
  equals the composed frame, 0 px, through the attract, widescreen included.

## Getting the Steam margins

The setup ([ROM.md](ROM.md)) looks for an installed Steam copy of Chrono Trigger (the Steam registry key and
`libraryfolders.vdf` on Windows, `~/.steam` / `~/.local/share/Steam` / the Flatpak's on Linux, `CT_STEAM_DIR` to name
one) and, when it finds one, writes `ct_wide.dat` beside the game's data with `tools/steam_wide.py`. Without one the step
says so and the option stays greyed in F1. By hand:

    python tools/steam_wide.py ["<Steam's Chrono Trigger folder>"] [-o ct_wide.dat]
    python tools/steam_wide.py --find

`ct_wide.dat` holds data from the player's own purchase: it is made on the player's machine, never committed or
shipped (`.gitignore` `*.dat`). The reader of the Steam archive follows GitExl/CTViewer (MIT). The tree holds only the
reader and facts derived from an analysis (the 42 locations of `kSteamCovers`); publishing them is the owner's decision
(2026-10-24).

## Tools

| Tool | What |
|---|---|
| `tools/steam_wide.py` | the Steam copy's margins -> `ct_wide.dat` (`--find`: where the copy is; `--list`: the locations) |
| `tools/steam_bgcover.py` | per room and side, whether the Steam release draws over the background: kSteamCovers' lines |
| `tools/widescreen_check.py` | the 16:9 picture's middle against the 256 one (check `options-widescreen-view-equals-the-256-picture`) |
| `CT_WIDESHOTS`, `CT_MOTCHECK`, `CT_MOTDUMP`, `CT_NATIVECHECK` | probes ([DEBUG_SURFACE.md](DEBUG_SURFACE.md)) |
