# Chrono Trigger LZ compression (Evil Peer / M. Springer)

CT compresses graphics, maps and other bulk data with a custom LZ, reverse
engineered by Michael Springer ("Evil Peer", author of Temporal Flux). Ported to
C (`src/lz.c`) and Python (`assets/lz.py`) from the canonical algorithm (as in
CTViewer's `util/lz_decompress.rs`).

## Format
- 2-byte little-endian **size** header at the block start, then the bitstream.
- **Mode** (window size) is chosen once from the top bits of the byte just past
  the first segment: `(data[byte_after] & 0xC0) != 0` ⇒ *small* (11-bit window),
  else *large* (12-bit window).
- Data is a sequence of **flag bytes**; each processed bit selects:
  - `0` → copy one **literal** byte;
  - `1` → a 2-byte **back-reference**. Length = `(hi >> (small?3:4)) + 2`, copied
    as `len+1` bytes; offset = `word & (small?0x07FF:0x0FFF)`, back from the
    current output position.
- Flag byte `0x00` is a fast path: **eight raw literal bytes** follow.
- **Segmentation:** when the read pointer reaches `byte_after`, a 3-byte marker
  rebases it — control byte (`& 0x3F`) gives the next bit count (`0` ends the
  stream); the following 2 bytes set the new segment end (relative to start).

## Decompressor location in ROM (this game)
Found by the offset-mask signatures (`AND #$07FF` / `AND #$0FFF` followed by `STA`):
- `AND #$0FFF` (large): file `0x30624`/`0x306E7` = **`$C3:0624` / `$C3:06E7`**
- `AND #$07FF` (small): file `0x307A9`/`0x3086B` = **`$C3:07A9` / `$C3:086B`**

⇒ the in-game decompressor lives in **bank `$C3`** (~`$C3:0600-08xx`), consistent
with bank C3 being touched during boot.

## Validation
- **Cross-implementation:** `ct.exe --lz 2A0CD4` and `python assets/lz.py … 2A0CD4`
  both decompress the graphics pack at file `0x2A0CD4` to **4096 bytes** (= 128
  4bpp tiles) with identical FNV-1a/32 = `01CE97F4`.
- The algorithm is the canonical one (CTViewer renders real ROMs with it), and
  the routine is located in-ROM (above), so the format is confirmed as the game's.

## Graphics extractor (visual validation)
`assets/gfx.py` LZ-decompresses a block, decodes SNES planar tiles (2bpp/4bpp),
and renders a dependency-free grayscale PNG/BMP. Run on the pack at `0x2A0CD4`,
it produces **128 4bpp tiles** that are clearly **coherent Chrono Trigger artwork**
(an enemy sprite + object tiles) — end-to-end visual confirmation that the LZ
output is real graphics, not noise. (Extracted images are ROM-derived and
git-ignored.)

## Palettes & colorization (done)
`assets/palettes.py` converts SNES 15-bit BGR → RGB with the PPU's exact formula
(`(c<<3)|(c>>2)` per 5-bit channel — matched against `snes/ppu.c`), so colors
equal what the oracle renders. Harness modes `--cgram`/`--fb`/`--vram` dump the
oracle's real CGRAM, framebuffer, and VRAM after N boot frames.

- **Colorization:** `gfx.py` takes a CGRAM dump + sub-palette and renders tiles in
  real color (`--vram <vram.bin> out.png 4 cgram.bin <sub>`). Verified: the VRAM
  at frame 300 colorized with sub-palette 0 shows the gold **"CHRONO TRIGGER ©1995
  SQUARE … LICENSED BY NINTENDO"** text and the copper **pendulum** — correct
  colors with a matched runtime palette. The framebuffer at frame 300 shows the
  same pendulum scene in color.

- **Palette animation/cycling** (battles, effects, intro): because the correct
  colors are the *runtime* CGRAM, palettes can rotate per frame. `--cgramseq`
  dumps CGRAM for N consecutive frames; `palettes.py timeline` renders a
  frames×256 image and reports which indices change. Found in the opening demo:
  static at frames 300/700/1600, but **indices 176-186 cycle around frame 1100**,
  **129-140 around frame 3000**, and a full reload (~237 indices, a fade) around
  frame 2200 — i.e. the engine animates localized palette bands at runtime.

## Next
- **Oracle byte-capture (full rigor):** run the in-game `$C3` decompressor on a
  block via run-to-PC (entry → RTL), capture its RAM output, and assert it equals
  `ct_lz_decompress` on the same input.
- `maps.py` / `text.py`; pair graphics packs with their palettes to colorize
  `gfx.py` output; orchestrate via `extract_assets.py` → `ct_assets.dat`.
