// lz.h -- Chrono Trigger LZ decompression (the "Evil Peer" / Michael Springer routine used for graphics,
// maps and scripts; see CTViewer's lz_decompress.rs) and the ROM's cost of the loads that use it.
#ifndef CT_LZ_H
#define CT_LZ_H

#include <stdint.h>
#include <stddef.h>

// Decompress the block whose 2-byte little-endian size header is at file offset
// `start` in `data`. Writes the decompressed stream to `out` and returns its
// length in bytes. `out` must be large enough for the decompressed data.
size_t ct_lz_decompress(const uint8_t* data, uint8_t* out, size_t start);

// The same routine with explicit bounds on both buffers. The format has no trustworthy terminator, so a
// caller whose pointer might not be an LZ stream (the object loader's, for one) must cap both ends.
// Returns the decompressed length, or 0 if any read or write would leave the buffers.
size_t ct_lz_decompress_bounded(const uint8_t* data, size_t dataLen,
                                uint8_t* out, size_t outCap, size_t start);

// What C3/0557 spends decompressing that stream, in SNES master cycles, walked token by token with the
// instruction timings. `dstBankOdd` picks the destination-bank arm of the dispatch at C3/0580 ($7F = 1);
// the stream's end byte picks the 11-bit window variant, as in the ROM. disasm/notes/field_load_cost.md
// sec. 2. Returns 0 for a stream it cannot walk.
long ct_lz_rom_cycles(const uint8_t* data, size_t dataLen, size_t start, int dstBankOdd);

size_t ct_lz_hirom_offset(unsigned addr24);   // 24-bit HiROM address -> file offset

// Streams from the asset container (CT_ASSET_LZ): once set, a decompression or cost asked of `rom` at an
// offset the container holds is served from the container (bytes and both costs) without reading the ROM.
// Other streams still come from `rom`. NULL clears it.

void ct_lz_use_assets(const void* assets, const uint8_t* rom);
// Another copy of the same ROM image (rom_verify.c's beside battle.c's), served the same way.
void ct_lz_add_rom_image(const uint8_t* rom);
// The container's entry for the stream at `start` (NULL = none): its decompressed bytes, length and two costs.
const uint8_t* ct_lz_asset(size_t start, size_t* outLen, long* cyc7e, long* cyc7f);

// What C0/A33B spends building the map after decompressing it: the plane copies and the RLE fill walked
// over the decompressed image `m`, plus the fixed half (the 4032-byte MVN and a measured one-shot constant).
long ct_map_build_cycles(const uint8_t* m, size_t len);
// C0/A33B besides its decompression, walked; -1 where the walk stops.
long ct_map_load_cycles(const uint8_t* rom, size_t romLen, int loc);

// The overworld load's packets (bank C2's list, resolved through the world header at $C6:FD00 + world*2)
// and their decompression cost. `srcOut` gets each 24-bit source, `dstOut` its destination bank ($7E/$7F).
int  ct_lz_world_packets(const uint8_t* rom, size_t romLen, int world,
                         uint32_t* srcOut, uint8_t* dstOut, int max);
long ct_lz_world_cost(const uint8_t* rom, size_t romLen, int world, int* outPackets);
// The whole world load, C0/0B5A -> C2/23CC (NMI back on), in non-refresh cycles; $E3/$E7 = each BG's camera
// cells (camera >> 3). -1 (and *why) off the walk.
long ct_lz_world_load_cycles(const uint8_t* rom, size_t romLen, int world, const uint16_t e3[2], const uint16_t e7[2],
                             int* outPackets, const char** why);
// C2/2C93's rebuild after the X menu: the entry's packets minus the four that survive the menu.
long ct_lz_world_return_cost(const uint8_t* rom, size_t romLen, int world, int* outPackets);

// The decompression cost of one location load: every packet the loader decompresses, priced with
// ct_lz_rom_cycles, plus (in *outDma) the VRAM uploads C0/2DC8 issues. disasm/notes/field_load_cost.md.
long ct_lz_load_cost(const uint8_t* rom, size_t romLen, int loc, long* outDma, int* outPackets);
long ct_lz_return_cost(const uint8_t* rom, size_t romLen, int loc);   // C0/0304's re-run after a battle: map+build, asm, BG3 asm, script
int  ct_lz_map_header_byte(const uint8_t* rom, size_t romLen, int loc, int idx);   // the map image's byte (C0/A33B -> $7E:B500), -1 if unreadable
int  ct_lz_load_tiles(const uint8_t* rom, size_t romLen, int loc, uint8_t* vram64k);   // the load's tile uploads into a 64 KB VRAM image
int  ct_lz_load_anim_tiles(const uint8_t* rom, size_t romLen, int loc, uint8_t* wram);   // C0/6D65 / 6D97: packets 6-7 -> $7F:6000 / $7F:7000
int  ct_lz_load_assembly(const uint8_t* rom, size_t romLen, int loc, uint8_t* asm4k);   // the tileset's assembly ($7E:B000), 512 x 4 words
int  ct_lz_load_assembly3(const uint8_t* rom, size_t romLen, int loc, uint8_t* asm2k);  // the BG3 assembly ($7E:C000, C0/0A14), 256 x 4 words
int  ct_lz_map_to_wram(const uint8_t* rom, size_t romLen, int loc, uint8_t* s7e);   // C0/A521's map cells at $7E:3000/3040/3080/30C0/7000/7040

// The sprite sheets the object pass decompresses (C0/4590's miss arm and C0/46DF, into the $7F:8000 heap).
// Which ones run depends on the objects and on C0/5C90's dedup, so the port's loaders add each one's
// ct_lz_rom_cycles here; the host zeroes it before a load. disasm/notes/field_load_cost.md sec. 6b.
extern long g_ctLzSpriteCycles;

#endif
