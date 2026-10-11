# Sprite pack → palette pairing (NPC/PC sprites, SNES/USA)

Reverse-engineered from CTViewer's `backend_snes.rs` and verified against the
ROM. Implemented in `assets/sprites.py`. This lets any sprite pack be colorized
with its real palette (no need for the data to be on-screen).

## ROM tables
- **7-pointer table @ `0x24FFE0`** — seven 2-byte local pointers (bank `0x24`):
  `[0]`→sprite graphics pointer table, `[1]`→assembly, `[2]`/`[3]`→anim,
  **`[4]`→sprite palette pointer table**, `[5]`/`[6]`→anim data.
  Decoded values (USA): `0x242000 0x242300 0x242600 0x242800 0x242A00 0x243000 0x24A800`.
- **Sprite header @ `0x24F000 + index*5`** (5 bytes):
  `[bitmap_index, assembly_index, palette_index, anim_index, size_flags]`.
- **Graphics pointers** (table `[0]`, 248 × 3-byte SNES pointers): file offset =
  `0x10000*(bank-0xC0) + ptr`. Indexed by `bitmap_index`.
- **Palette pointers** (table `[4]`, 253 × 2-byte local pointers): each palette is
  **24 bytes = 12 BGR15 colors**. They map to palette **indices 1..12** — index 0
  is transparent and 13..15 are unused (CTViewer `read_palette` `set_start=1`,
  `set_pad=3`). Reading the 12 colors into indices 0..11 shifts every color by one
  (a bug to avoid); pixel value 0 must be transparent.

## Compression rule
`bitmap_index <= 6` → the **7 playable characters**, graphics stored
**uncompressed**. `bitmap_index > 6` → NPC/enemy graphics, **LZ-compressed**
(`src/lz.c` / `assets/lz.py`).

## Sprite assembly (chip → frame composition)
A raw decoded tile sheet is a 16-wide "chip sheet"; the recognizable sprite is
built by the **assembly** data (table `[1]` @ `$242300`, 231 × 3-byte pointers,
indexed by `assembly_index`). Per frame:
- `size_flags & 3` → `(groups_per_frame, tiles_per_group)`: `0→(1,4) 1→(1,8)
  2→(3,4) 3→(3,8)`. Each "tile" is a 16×16 **unit** = 4 chips.
- Layout: for each group, the unit chips are read upper-row then bottom-row
  (`u16` each: bits `0x3FF`=source tile, `0x4000`=flip-X, `0x8000`=flip-Y), then a
  signed `(ox,oy)` byte pair per unit positions it. Frame size = `units*10` bytes.
- Chip local positions in a unit: TL/TR/BL/BR = `(0,0)(8,0)(0,8)(8,8)`.

## Validation
- `sprites.py <rom> chars out.png` → the 7 playable characters with their paired
  palettes: **Crono** (orange spiky hair), **Marle** (blonde), **Lucca**, **Robo**,
  **Frog** (green), **Ayla**, **Magus** — recognizable, correctly colored. Palette 0
  reads as Crono's exact ramp (skin `FFCE9C`, hair `FF8C00`).
- `sprites.py <rom> frame <index> out.png` → composes & colorizes frame 0 of any
  sprite via the assembly. Verified on NPCs/enemies (e.g. sprite 9 = a purple/orange
  monster, sprite 100 = a mechanical chest) — recognizable, correctly colored,
  confirming the full chain LZ → 4bpp decode → palette pairing → assembly.
- `info` dumps the parsed tables; `render` shows the raw chip sheet (debug).
