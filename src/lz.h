// lz.h — Chrono Trigger LZ decompression (the "Evil Peer" / Michael Springer
// routine used for graphics, maps, etc.). Faithful port of the canonical
// algorithm (as in CTViewer's lz_decompress.rs).
#ifndef CT_LZ_H
#define CT_LZ_H

#include <stdint.h>
#include <stddef.h>

// Decompress the block whose 2-byte little-endian size header is at file offset
// `start` in `data`. Writes the decompressed stream to `out` and returns its
// length in bytes. `out` must be large enough for the decompressed data.
size_t ct_lz_decompress(const uint8_t* data, uint8_t* out, size_t start);

// The same routine with explicit bounds on both buffers. A stream that is not
// actually LZ drives the read and write cursors wherever its bytes say -- there
// is no terminator the format lets you trust -- so a caller handing it a pointer
// that might not be a graphics packet (the object loader's, for one) must cap
// both ends. Returns the decompressed length, or 0 if any read or write would
// have left the buffers. Identical output to ct_lz_decompress for every stream
// that stays in bounds; the checks only fire where the uncapped version would
// have overrun.
size_t ct_lz_decompress_bounded(const uint8_t* data, size_t dataLen,
                                uint8_t* out, size_t outCap, size_t start);

// THE ROM'S OWN COST for that same stream, in SNES master cycles: what C3/0557 spends
// decompressing it, walked token by token with the listing's instruction timings (FastROM 6,
// WRAM/direct page 8, the $2180 port 6, MVN 46 per byte). `dstBankOdd` picks the destination-bank
// arm of the dispatch at C3/0580 ($7F = 1) and the stream's own end byte picks the 11-bit window
// variant, exactly as the ROM does -- the four combinations differ only in a fixed prologue.
// Cycle-EXACT against the oracle (27 packets over five locations, spread 0; see
// disasm/notes/field_load_cost.md). Returns 0 for a stream it cannot walk.
long ct_lz_rom_cycles(const uint8_t* data, size_t dataLen, size_t start, int dstBankOdd);

size_t ct_lz_hirom_offset(unsigned addr24);   // 24-bit HiROM address -> file offset

// THE STREAMS FROM THE ASSET CONTAINER (CT_ASSET_LZ): once set, a decompression or a cost asked of `rom` (the ROM image
// the engine reads) at an offset the container holds is served from it -- the bytes and both costs as the ROM's stream
// gives them -- and the ROM is not read. Streams it does not hold still come from `rom`. NULL clears it.

void ct_lz_use_assets(const void* assets, const uint8_t* rom);
// Another copy of the same ROM image the engine reads through (rom_verify.c's beside battle.c's): served the same way.
void ct_lz_add_rom_image(const uint8_t* rom);
// The container's entry for the stream at `start` (NULL = none): its decompressed bytes, length and two costs.
const uint8_t* ct_lz_asset(size_t start, size_t* outLen, long* cyc7e, long* cyc7f);

// What C0/A33B spends BUILDING the map after it decompresses it: the four plane copies and the
// RLE fill, walked over the decompressed image `m` the way the ROM walks it, plus the routine's
// fixed half (its 4032-byte MVN, derived, and a one-shot constant measured over five locations).
long ct_map_build_cycles(const uint8_t* m, size_t len);
// C0/A33B besides its decompression, WALKED (exact on the traces); -1 where the walk stops (map header bytes 2-3)
long ct_map_load_cycles(const uint8_t* rom, size_t romLen, int loc);

// The OVERWORLD load's packets (bank C2's own list, resolved through the world header at
// $C6:FD00 + world*2) and what decompressing them costs the ROM. `srcOut` gets each 24-bit
// source and `dstOut` its destination bank ($7E/$7F, which picks C3/0557's dispatch arm).
int  ct_lz_world_packets(const uint8_t* rom, size_t romLen, int world,
                         uint32_t* srcOut, uint8_t* dstOut, int max);
long ct_lz_world_cost(const uint8_t* rom, size_t romLen, int world, int* outPackets);
// The whole world load, C0/0B5A -> C2/23CC (the NMI back on), in non-refresh cycles: the packets with their sites, C2/2A44's
// lists, C2/09C5 over each BG's camera cells ($E3/$E7 = the camera >> 3), and the fixed rest (lz.c). -1 (*why) off the walk.
long ct_lz_world_load_cycles(const uint8_t* rom, size_t romLen, int world, const uint16_t e3[2], const uint16_t e7[2],
                             int* outPackets, const char** why);
// C2/2C93's rebuild after the X menu: the entry's packets minus the four that survive the menu (lz.c).
long ct_lz_world_return_cost(const uint8_t* rom, size_t romLen, int world, int* outPackets);

// The decompression cost of ONE location load: every packet C0/0960 / C0/A33B / C0/09DD
// decompress, priced with ct_lz_rom_cycles, plus (in *outDma) the VRAM uploads C0/2DC8 issues.
// This is the part of the load's black window this port can derive; see the dossier for the
// two phases that are still measured but not modelled.
long ct_lz_load_cost(const uint8_t* rom, size_t romLen, int loc, long* outDma, int* outPackets);
long ct_lz_return_cost(const uint8_t* rom, size_t romLen, int loc);   // C0/0304's re-run after a battle: map+build, asm, BG3 asm, script
int  ct_lz_map_header_byte(const uint8_t* rom, size_t romLen, int loc, int idx);   // the map image's byte (C0/A33B -> $7E:B500), -1 if unreadable
int  ct_lz_load_tiles(const uint8_t* rom, size_t romLen, int loc, uint8_t* vram64k);   // the load's tile uploads into a 64 KB VRAM image
int  ct_lz_load_anim_tiles(const uint8_t* rom, size_t romLen, int loc, uint8_t* wram);   // C0/6D65 / 6D97: packets 6-7 -> $7F:6000 / $7F:7000
int  ct_lz_load_assembly(const uint8_t* rom, size_t romLen, int loc, uint8_t* asm4k);   // the tileset's assembly ($7E:B000), 512 x 4 words
int  ct_lz_load_assembly3(const uint8_t* rom, size_t romLen, int loc, uint8_t* asm2k);  // the BG3 assembly ($7E:C000, C0/0A14), 256 x 4 words
int  ct_lz_map_to_wram(const uint8_t* rom, size_t romLen, int loc, uint8_t* s7e);   // C0/A521's map cells at $7E:3000/3040/3080/30C0/7000/7040

// ...and the part of the same window that only the LOADER ITSELF knows: the sprite sheets the object pass
// decompresses (C0/4590's miss arm and C0/46DF's, both `JSL $C3:0002` into the $7F:8000 heap). Which ones run
// depends on the location's objects and on C0/5C90's per-scene dedup, so it cannot be read off the location
// record -- the port's own loaders add each one's ct_lz_rom_cycles here, and the host zeroes it before a load
// and takes the total when it prices the black window. See disasm/notes/field_load_cost.md section 7.
extern long g_ctLzSpriteCycles;

#endif
