# Chrono Trigger overworld (world map) format

Reverse-engineered from the ROM (workflow `overworld-re`, 3 finders + synthesize + verify, all
high-confidence; the 1000 AD decode was verified end-to-end). The overworld is a **separate engine
(bank C2)** from the field-scene engine (bank C0). Decoder: `assets/overworld.py`.

## Dispatch
`C0:0066` master location dispatcher: `LDX $0100 / CPX #$01F0 / BMI field / JML $C20000`. A location
index in WRAM `$0100 >= 0x1F0` enters the overworld; **world index = ($0100 & 0x1FF) - 0x1F0**, range
**0..7 (8 worlds)**. (assets/maps.py already flags field exit dests 0x1F0-0x1FF as kind `world`.)
World 0 = 1000 AD (reached from Crono's house ground floor's `world dest=0` exit).

## Data tables (all in bank C6, file 0x06xxxx; decode via `assets/lz.py` lz_decompress, the shared
## decompressor `$C30002` used by both field + overworld)
- `C6:FD00` (file 0x06FD00) = master descriptor ptr table: **8 u16 LOCAL offsets** into bank C6 ->
  the 8 per-world descriptors at `C6:FD10` + world*0x17 (23 bytes each; FD10,FD27,FD3E,FD55,FD6C,FD83,
  FD9A,FDB1). (Entries 8-15 are garbage = the first descriptor re-read; there are 8 worlds, not 16.)
- A **23-byte descriptor**; each byte indexes a bank-C6 3-byte-ptr subtable (0x80 high bit = none).
  Loader C2:2768-2A6D. Mapping (world 0 descriptor = `00 01 02 03 04 05 06 80 14 80 04 0B 04 05 06 07
  00 00 00 00 00 00 00`):
  - desc[0x00..0x06] -> `C6:FE20` -> tile gfx list (DMA'd to VRAM tile **512**), 4bpp, each LZ->4096B
  - desc[0x0C..0x0F] -> `C6:FDF0` -> tile gfx list (DMA'd to VRAM tile **0**), 4bpp, each LZ->4096B
  - desc[0x0A] -> `C6:FEA0` -> the land/structure **palette** block (512B; see Palette caveat)
  - desc[0x10] -> `C6:FF00` -> **L1** metatile/block defs (LZ->4096B = 512 blocks x 4 chips)
  - desc[0x11] -> `C6:FF20` -> **L2** world block-grid (LZ->12288B = 96 x 128 cells, 1 byte/cell)
  - desc[0x12/0x13] -> C6:FF80/FFA0 (aux, small); desc[0x15/0x16] -> C6:FFC0/FFE0 (exit/object data)

## Map format (uniform across all 8 worlds)
- **L2** = 96 (w) x 128 (h) grid, 1 byte per cell = an L1 block index. World = 96*16 x 128*16 = **1536
  x 2048 px**. (Width 96 confirmed by autocorrelation + a coherent render.)
- **L1** = 512 metatile blocks, 8 bytes each = 4 chips (TL,TR,BL,BR), each chip a little-endian u16:
  **tile = bits0-9, palette = bits10-12, priority = bit13, xflip = bit14, yflip = bit15** (the SAME
  SNES metatile encoding the field maps use).
- **gfx** = 4bpp 8x8 tiles. The two lists DMA to distinct VRAM tile bases (FDF0 -> tile 0, FE20 ->
  tile 512), so the 10-bit chip tile# indexes `[FDF0[0:512] | FE20[0:512]]`.
- Era -> world: W0=1000AD, W1=600AD, W2=2300AD, W3=65MBC, W4=12000BC, W5/W6=12000BC variants,
  W7=special (no field-scene exit; Epoch/script). W0 & W1 share L1 (the Guardia continent).

## GFX VRAM upload (C2:2CC1, disasm-confirmed) -- THE OVERWORLD HAS TWO BG LAYERS
The gfx-upload routine C2:2CC1 does FOUR WRAM($7F:9000)->VRAM DMAs (helper C2:2D70, X=src lo, Y=VMADDL,
A=len); VRAM tile# = VMADDL/16 (4bpp = 16 words/tile):
- JSR C2:2768 (FDF0, desc[0x0C..0x0F]) -> VMADDL $0000 = BG1 tiles 0-511   } the L1/L2 METATILE layer
- JSR C2:27DE (FE20, desc[0x00..0x06]) -> VMADDL $2000 = BG1 tiles 512-1023 } (what the L1 chips index)
- JSR C2:2826 (more gfx)               -> VMADDL $7000 = a SECOND layer (BG2/3 char base)
- JSR C2:2854 (more gfx)               -> VMADDL $7800 = the SECOND layer
=> CONFIRMED: BG1 gfx = FDF0 @ tile 0 + FE20 @ tile 512 (assets/overworld.py is correct here). The
"tiles look ~right but not 100%" is because the OVERWORLD COMPOSITES TWO BG LAYERS: BG1 = the L1/L2
metatiles (coastlines, structures, mostly transparent), drawn OVER a BG2/3 BASE layer that supplies the
ocean/grass texture (gfx at VRAM $7000/$7800). overworld.py renders only BG1 over a flat blue backdrop,
so transparent BG1 corners show flat blue instead of the base's ocean/grass -> the striped/incomplete
land. The base layer's own metatiles/tilemap are loaded by the master sequence C2:2C5C sub-loaders
(295E=L1, 298C=L2 are BG1; 29BA/29E8/2A44/2A16 load the rest incl the base layer) -- RE these to render
the full two-layer world. (CGRAM palette DMA confirmed: C2:2D4B, src $7F:8C00 len $200.)

## BASE LAYER RE'd (workflow overworld-baselayer-re): the overworld is 3 BGs (mode 1, $2105=$09)
- BG1 = land/structures (4bpp metatiles: L1 defs $7E3000, L2 grid $7E4000, tilemap VRAM $6000, char tiles
  0-511=FDF0 + 512+=FE20). LZ-loaded from ROM = what overworld.py renders.
- BG2 = a 2nd 4bpp metatile layer (defs $7E3800, grid $7E5800, tilemap $6800). NOT in ROM -- built at
  RUNTIME (MVN procedural fill C2:9AAD/9B0D + the overworld SCRIPT VM, desc[0x16]->C3:8B6C 5557B->$7F0400,
  whose tilemap-copy opcodes blit region data into the $7E grids).
- BG3 = 2bpp base ocean/grass texture (char VRAM $7000/$7800 from desc[0x08]->C6:FE20[0x14]->C4:F7AF 4096B
  + desc[0x14]->C6:FF40[0]->C6:C7F7 4096B; tilemap runtime-built). desc[0x12]/[0x13] (FF80/FFA0) = per-tile
  property/COLLISION maps (FFA0 = 64 rows x 48 bytes, 2 nibbles/byte; reader C2:6300 = row*0x30+col/2) -- useful
  for M3-C. Metatile expander = C2:09C5/0AAE (grid byte*8 + defs base -> 4 chips). Master seq builds BG1+BG2
  only (09C5 called $00=1,2; BG3 never expanded).
- FIXED in overworld.py: the block-def CHIP ORDER is +0=TL, +2=BL, +4=TR, +6=BR (engine C2:0AAE), i.e.
  [TL,BL,TR,BR] -- overworld.py had [TL,TR,BL,BR] (TR/BL swapped). Corrected -> terrain/structures now tile
  correctly. The remaining striped land = the missing BG2/BG3 base (runtime-built; full fix needs porting
  the C2 script VM + MVN builders -- large; or dump live VRAM $6000/$6800 via the oracle).

## EXACT RENDER -- SOLVED via a live engine snapshot (assets/overworld_live.py)
The displayed overworld is ASSEMBLED AT RUNTIME (BG1 = ROM L2 + script-VM region blits; BG2 base = MVN +
script VM), so the static ROM tables can't reproduce it -- but the live WRAM grids can. Captured a snes9x
save state standing on the 1000 AD overworld; the render now MATCHES the real world map (Guardia/Porre/
Medina/Choras/Sun Keep, blue ocean, correct tiles).
KEY CORRECTION: the displayed map is **96 x 64** (LANDSCAPE, 1536x1024), NOT 96x128. The live composited
grids are 6144B each: BG1 grid $7E:4000-57FF, BG2 grid $7E:5800-6FFF (block-defs BG1 $7E:3000, BG2 $7E:3800;
256 blocks x 8B). (The ROM L2 block is 12288B but only the top 96x64 is the live displayed map; reading
96x128 ran past the BG1 grid into the BG2 grid = the earlier "garbled bottom half".)
- Char: straight from VRAM (the save state's VRA block); chip tile# = VRAM tile (char_base 0). Palette:
  WRAM $7F:8C00 (256 BGR15; the CGRAM source). pal2 c4-11 = ocean blues, pal3 = grass greens.
- Composite (back->front): ocean-blue BACKDROP (the deep sea = fully-transparent metatiles over CGRAM 0,
  which the engine drives with a per-scanline HDMA blue gradient -- approximated as a vertical gradient) <
  BG2 (waves/grass, transparent) < BG1 (land/structures, transparent). Chip order [TL,BL,TR,BR] (C2:0AAE).
- Repro: `python assets/snes9x_extract.py <save.state>` (WRAM+VRAM -> build/) then `python assets/
  overworld_live.py`. Remaining nits: the HDMA ocean gradient is approximated; one small sprite/HUD box from
  the snapshot; wave-tile highlight is bright. assets/overworld.py (static ROM decode) stays as the
  structure reference but cannot match the runtime layout.

## LIVE EMULATOR BRIDGE (assets/bridge.py) + the exact-palette wall
There is a snes9x-rr Lua MCP bridge (file IPC: <proj>/tools/mcp/ipc/command.json + response.json). It
drives the LIVE emulator. WORKING ops: ping, read_memory (WRAM $7Exxxx/$7Fxxxx), write_memory, set_input,
capture_framebuffer (GD truecolor 256x224 -> decode: 11-byte hdr + w*h*4, bytes [a,R,G,B]). BROKEN in this
build: read_vram / read_cgram (the "readbyte_domain_arg" fallback ignores the domain -> returns garbage),
and PPU regs are open-bus. So the live CGRAM/VRAM are NOT cleanly readable; only WRAM + the rendered
framebuffer are. Verified live: $0100=0x1F0 (overworld w0); BG scroll shadow $7E0020/$7E0022 (wraps 0-511);
player/cam map vars near $7E0D84; phase-correlating two frames after set_input gives a clean scroll delta.
WALL on exact Python colors: deriving CGRAM from the rendered framebuffer needs the frame<->value-map
correspondence, which won't align (max ~11%): the ROM char vs live char + generic land edges + the ocean's
HDMA gradient + water-palette animation + sprites/HUD all confound it. So the pixel-exact OVERWORLD PALETTE
is not recoverable from this bridge build. The LAYOUT (grids) is correct; the C engine will render colors
natively (palette DMA + HDMA + anim). For a perfect IMAGE, capture/stitch live framebuffers (bridge.capture).

## OPEN (render polish, not the structure -- the L1/L2/gfx decode is verified correct)
- **Base layer / backdrop**: tile pixel 0 is transparent (SNES BG). The ocean is transparent metatiles
  over the backdrop; many LAND blocks also have transparent corners that in-game show a **base BG layer
  (BG2: ocean-blue at sea, grass-green on land)**, NOT a single backdrop colour. `overworld.py` currently
  fills transparency with a provisional blue (BACKDROP), which is right for the sea but leaves a striped
  look on land. The real base layer (a second metatile set, or a per-region backdrop) + the exact
  palette/backdrop are best captured from the engine's live CGRAM/VRAM via the oracle (drive to the
  overworld) -- TODO.
- desc[0x0A] -> C6:FEA0 (512B) is the land/structure palette (pal0-6 = browns/greens; pal7-15 mostly
  empty). It lacks an ocean-blue color 0, confirming the sea colour is the separate backdrop/BG2.
- The exit/object records (desc[0x15/0x16]) decompress cleanly but the per-record layout is unparsed
  (needed for M3-D location entry).

## The tilemap rebuild's direct-page words (C2/09C5)

The per-layer rebuild C2/09C5 (called with `$00` = 1, 2) leaves its working words in `$00-$1B` and the scroll
shadows when it returns. They are read later: C3/02DD (the seed, `ADC $0008`) folds `$0008` into `$0386` when the
world's window object calls it later in the same pass (the attract's 1F5 leg: the first seed call reads the last word
C2/0A55 stored). The port writes them in `owRebuildScratch` (src/world.c). All are 16-bit (09CC REP #20; the tables
are bank $C2 words), X = (layer - 1) * 2:

```
09DB-09E7  $20,X = $E3,X << 3 ; $26,X = $E7,X << 3
09E9-09F9  $14 / $16 / $10 = C2:0556 / C2:055C / C2:0562 [X]
09FB-0A06  $08 = $E3,X >> 1 when $E3,X is odd, else ($E3,X >> 1) - 1, and -1 -> $5F
0A08-0A13  $0A = the same from $E7,X, -1 -> $3F
0A15-0A29  $0C = ($20,X - (bit 3 ? 8 : 16)) & $1FF ; 0A2B-0A3F  $0E = ($26,X - (bit 3 ? 8 : 16)) & $FF
0A41       $02 = 18, then 18 rows of:
  0A6C  $00 = 16 ; $18 = $08 + $16 ; $1A = $0A << 6 ; $04 = $0A * 96 ; $06 = ($0E & $F0) >> 2 ; and 16 columns of
        0A90-0AA7  $04 += $60 (>= $1800 -> 0) ; $06 = ($06 + 4) & $3F ; DEC $00
  0ADC  $12 = ($0C >> 3, + $3E0 when >= $20) + $10   (then the row's two DMAs)
  0A4C  $08 += 1 (>= $60 -> 0) ; 0A57  $0C = ($0C + $10) & $1FF ; 0A62  DEC $02
```
