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
| Fill the bottom lines | `fill_bottom_lines` | the field's and the fights' lines 211-223, which the SNES blanks, drawn |
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
   view, not repeated (X4b). Three exceptions:
   - a background the room's front layers **cover everywhere** is no sky -- the SNES shows it only through a window: the
     Telepod's warp swirl in 008 (`fieldHiddenBg`);
   - **without the Steam margins drawn**, a side where the Steam release draws scenery over the background ends black:
     `kSteamCovers` in `src/field.c`, 42 locations, location numbers and sides only, printed by
     `tools/steam_bgcover.py` from a Steam copy (the skywalk past its tower wall, 01C);
   - Magus's castle (0A5): its BG2 map begins with 32 flat grey rows over the sky; with the widescreen the sky is drawn
     5 lines higher for the whole visit, so the pan ends with that band just above the screen (`fieldWideBg2Dy`, art
     direction).
3. Otherwise **black**, as the room's own end.

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

## Fill the bottom lines

The SNES's V-IRQ blanks lines 211-223 of the field and of fights (old TVs hid them). The option draws them; at a
room's bottom the last line repeats the room's last row instead of showing the map past it.

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
