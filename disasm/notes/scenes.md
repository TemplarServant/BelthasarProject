# Scenes / field maps (SNES/USA)

Reverse-engineered from CTViewer (`backend_snes.rs`, `scene.rs`, `tileset.rs`,
`maps.rs`), verified against the ROM. Implemented in `assets/maps.py` (layer 1).

## Scene header — `0x360000 + scene_index*14` (SNES)
`[music(u8), tileset_l12(u8), tileset_l3(u8), palette(u8), map(u16), chip_anims(u8),
palette_anims(u8), script(u16), scroll_mask: l,t,r,b(4×u8)]`. SNES derives
`tileset_l12_assembly_index = tileset_l12`.

## Tileset (layer 1/2)
- `0x361C00 + tileset_l12*8` → **8 chipset bytes**. Slots 0-5 are static chips,
  6 = animated, 7 unused-here; `0xFF` = none.
- Each chipset's graphics: LZ block at `TS_PTRS[chipset]` (`0x362220`, 204×3-byte),
  decoded as 4bpp → 128 8×8 chips, placed at global index `slot*128`.
- Assembly: LZ block at `ASM_PTRS[tileset_l12]` (`0x362100`, 64×3-byte) → **512
  16×16 metatiles**, each 4 corners (TL,TR,BL,BR) as `u16`:
  chip `= (v&0x3FF) - 256`, sub-palette `= (v>>10)&7`, priority `0x2000`,
  flip-X `0x4000`, flip-Y `0x8000`.

## Scene palette — `0x3624C0 + palette*210` (210 bytes)
`read_palette(skip=16, set_size=15, set_count=7, set_start=1)` → 128 colors = 8
sub-palettes of 16; sub 0 is black, subs 1-7 are `[black + 15 BGR15 colors]`.
A chip's color = `palette[sub*16 + pixel]`.

## Tilemap — LZ block at `MAP_PTRS[map]` (`0x361E00`, 202×3-byte)
Decompressed block is read sequentially:
1. **6-byte header**: `layer12_size, bits, scroll_l2, scroll_l3, screen_flags, effect_flags`.
2. **Layer 1**: `w1*h1` metatile bytes. `w1=((layer12_size>>0)&3)*16+16`, `h1=((>>2)&3)…`.
3. **Layer 2**: `w2*h2` bytes (`>>4`, `>>6`).
4. **Layer 3**: `w3*h3` bytes (`bits>>0`, `bits>>2`) — only if `bits & 0x80`.
5. **Tile properties** (the rest, RLE): 3 bytes/prop; if `byte0 & 0x80` a 4th byte =
   repeat count (`0`→256). Padded/trimmed to `scene_w*scene_h`
   (`scene_w=max(w1,w2)`, indexed `y*scene_w+x`).
   - `byte0 & 0x01` = **L1_TILE_ADD** → `layer1[y*w1+x] |= 256` (upper metatile bank).
   - `byte0 & 0x02` = **L2_TILE_ADD** → `layer2 |= 256`.

A scene map holds several rooms in one tilemap.

## Compositing (the rules that make tiles correct)
- A metatile corner with `chip == 0` is an **empty corner — skip it** (transparent).
- A chip pixel of **index 0 is transparent — skip it**; else color =
  `palette[sub*16 + index]`. (No global transparent color; it's per sub-palette.)
- Per-chip **priority** bit (`v & 0x2000`) splits each layer into two passes.
  Back-to-front order (no sprites): `L3p0, L2p0, L1p0, L2p1, L1p1, L3p1`
  (so L1 sits above L2; L3 priority tiles end up on top by default).

## Layer 3 (BG3) — separate 2bpp tileset
L3 uses `tileset_l3` (scene header byte 2). Graphics: LZ block at `TS_PTRS[ts_l3]`
decoded as **2bpp** (4 colors); assembly: LZ block at `ASM3_PTRS[ts_l3]`
(`0x3621C0`, 19×3-byte) → **256 metatiles**, `parse` with `palette_size=4` and
**no -256** chip offset (chip `= v&0x3FF`), color = `palette[sub*4 + index]`.
L3 is drawn at the back (priority-0 pass) and on top (priority-1 pass).

## Layer wrapping (tiling)
SNES BG layers wrap: each layer's tilemap repeats modulo its own size. A layer
smaller than the scene (a repeating sky/fog/water/snow background, or an effect
overlay) **tiles to fill** the whole area — `tile = layer[(cy % lh)*lw + (cx % lw)]`
sampled across the full canvas (CTViewer `chip_x % chip_width`, `chip_y % chip_height`).
Without this a small L2/L3 only covered the top-left and the rest was black (the
"fog only on the left", "no background", "smoke only at the top" bugs). Full-size
layers (e.g. Leene Square's 48×64) are unaffected — wrapping is a no-op there.

## Color math (SNES main/sub-screen blend) — translucency / night lighting
The scene map header bytes 4/5 drive the blend (emulating TM/TS + CGADSUB):
- `screen_flags` (byte 4): low nibble = layers on the **MAIN** screen
  (`L1=0x01,L2=0x02,L3=0x04,spr=0x08`), high nibble = layers on the **SUB** screen.
- `effect_flags` (byte 5): low 3 bits = which layer enables color-math; `0x40`=half,
  `0x80`=subtract → mode `{Add, AddHalf, Sub, SubHalf}`. Fixed color = black
  (COLDATA modeling is a TODO).

Render the MAIN layers to a main surface (tagging each pixel with its top layer)
and the SUB layers to a sub surface; then per pixel: if the main top-layer is
blend-enabled and a sub pixel exists → `blend(main, sub, mode)`; else if main is
empty and sub exists → show sub straight. Example — **Leene Square (scene 5)**:
`screen=0x4B` (main L1+L2, **sub L3**), `effect=0x42` (AddHalf on L2) ⇒ the L3
lights blend 50% onto L2 where they overlap (translucent), not as an opaque layer.

## Animated chips (water, torches, glints)
Tileset chipset **slot 6** holds animated chip graphics (4bpp). A per-tileset
animation table (`get_relative_entries` @ `0x3DF290`, 64 entries, base `0x3DF310`)
gives, for chip-anims index = `tileset_l12`, a list of animations parsed by
`parse_chip_anims`: each `[frame_count(u8); dest=(u16-0x2000)/16; frame_count
durations(u8); frame_count src=(u16-0x6000)/32]`. Each anim covers **4 chips**.
At animation frame F, `chips[dest+i] = animated_chips[src(F)+i]` for `i in 0..4`.
The per-frame **durations** select which frame is showing at a 60Hz tick: frame f
lasts `{0x10:16, 0x20:12, 0x40:8, 0x80:4}` ticks (nibble of the duration byte);
`anim_frame_at(durs, tick)` walks the cumulative durations. `--frame N` / `--aframe
N` is the 60Hz tick (Leene Square's fountain has 9 chip-anims; step the tick past a
frame's duration to see it ripple).

## Palette animation (color cycling)
Separate from chip animation: certain palette colors are cycled/swapped over time.
Tables: a u16 pointer per set @ `0x3DF9C7` (index = scene palette index) → a
descriptor stream @ `0x3DF9? (0x3DFA77)`, and a predefined color table @ `0x367380`
(referenced as `SNES_addr - 0x7380`). Each descriptor: `id` (high nibble type:
`0x30`=cycle-fwd, `0x20`=cycle-back, `0x80`/`0x10`=sequence; low nibble=frame count),
`color_index`, `color_count`, `delay`. **Cycle** rotates the `color_count+1` palette
entries at `color_index` up/down by one per step; **Sequence** overwrites the range
from the color table per frame. Each anim advances one **step every `delay` 60Hz
ticks** (`step = tick // delay`), so different anims cycle at their own rates —
`--frame N` is the tick. e.g. scene 112's sparkle/shimmer; the viewer plays it at
true speed.

## Fixed color (COLDATA) and scroll — why they don't change a static frame
- The color-math **fixed color** ($2132 COLDATA) is written by runtime/script code,
  not stored in the scene record (CTViewer hardcodes black too). So a static render
  uses black — the only static-safe value. (A scene-specific tint would need script
  execution.)
- Layer **scroll / parallax** only accumulates over time; at camera (0,0) with a zero
  scroll accumulator all three modes (Normal/Parallax/IgnoreCamera) place every layer
  at the same origin. So scroll has **no effect on a static single-frame render** — it
  is purely a motion feature.

## Scroll mask / camera crop
Scene header bytes 10-13 = the scroll mask `l,t,r,b` in metatiles (`left==0x80`
⇒ whole layer-1). The output is **cropped to this rect** = the camera-visible
playable area. A scene map can hold several rooms; the mask picks this scene's
view (scene 1 = one room of map 60; scene 5 excludes the off-camera top/bottom).
Use `--full` to render the entire map instead.

## Validation
`python assets/maps.py <rom> scene <index> out.png` composites layers 1+2+3 with
color-math, cropped to the visible area:
- scene 1 → furnished **house interiors** (floors, walls, beds, tables, rugs).
- scene 48 → a blue-grey **cave** (crystal formations, ledges, save orbs).
- scene 5 → **Leene Square / the Millennial Fair** (colorful tents, hedges, stone
  paths, fountain, Telepods) — L3 supplies the foliage background (with `--no-l3`
  those areas are black).
Applying `+256`, skipping empty chips/transparent pixels, and overlaying L2/L3
renders the scenes correctly (vs. the sparse layer-1-only first pass).

## Rendering from ct_assets.dat (no ROM)
`scene.c` splits **load** (gather a scene's decoded data) from **compose** (pixels).
`CtLoadSceneFromRom` reads + decodes from the ROM; `CtLoadSceneFromAssets` fetches the
already-decoded data from `ct_assets.dat` (built by `assets/extract_assets.py`). Both
fill the same `SceneData` and run the same `CtComposeScene`, so a `.dat` render is
byte-identical to a ROM render. Chip/palette animation is data-driven
(`apply_chip_anim_data` / `apply_pal_anim_data` on a serialized descriptor stream), so
the same appliers run on ROM- or `.dat`-sourced descriptors. `ct.exe --datverify
ct_assets.dat` checks all 512 scenes × 3 ticks match the ROM path (1536/1536).

## C engine port (`src/scene.c`)
The full scene renderer is also implemented in C (`CtRenderScene`, reusing
`src/lz.c`) — the project's own engine, not the Python tools. `ct.exe --scene <i>
--out <raw>` produces a raw `[W][H][RGB]` dump (convert with `palettes.py raw`).
It is validated **byte-identical** to `assets/maps.py`: scenes 1/5/48 match on an
FNV-1a/32 of the pixels (e.g. Leene Square = `92B5D43B` in both). So the C engine
renders recognizable, correctly-colored field scenes.

## Status
Field rendering is feature-complete for a static frame: L1/L2/L3 + `+256`,
transparency, per-chip priority, SNES color-math (main/sub blend), scroll-mask crop,
**chip animation** and **palette animation** (both via `--frame N`). Fixed color
(black) and scroll/parallax are inherent static limitations (see above), and
`layer3_priority` (L3 priority tiles above vs below) is left at the default `true`.
