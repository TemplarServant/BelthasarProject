// tileanim.h -- the field's animated BG tiles as the ROM keeps them: FD/DE98 (the loader) and FD/E022 (the runner).
//
// The machine lives in WRAM, so a state carries it: twelve 5-byte records at $7E:05B0 (+0/+1 a 16-bit accumulator,
// +2/+3 the VRAM word address, bit 15 = no animation, +4 the source bank) and, per record k, sixteen words at
// $7F:0400 + k*$20 -- eight step sizes (+0) and eight source addresses (+$10), one pair per slot. The runner picks the
// slot from the accumulator's bits 9-11, DMAs $80 bytes from the slot's source to the record's VRAM address and adds
// the slot's step (the duration byte * 4) to the accumulator, which carries into the next slot. It runs from the
// field's IRQ (C0/ECCC -> C0/ED08 `JSL FD/FFFD`) on the frames the main loop leaves $0153 bit 0 set (FD/C2C1 flips it
// every iteration), records 0-5 and 6-11 on alternate calls by $051D's parity -- so each record steps every 4 frames.
// disasm/notes/field_bg_anim.md.
#pragma once
#include <stdint.h>

// One of the runner's DMAs: $80 bytes from `bank:src` to VRAM word `vramWord`.
typedef void (*CtTileAnimDma)(void* ctx, int vramWord, int bank, int src, int bytes);

// FD/DE98 (C0/0112 `JSL FD/FFFA`, the location load): the records and the step/source tables for location `loc`.
// `w` = the flat 128 KB WRAM image ($7E at 0, $7F at 0x10000).
void CtTileAnimLoad(uint8_t* w, const uint8_t* rom, long romLen, int loc);

// FD/E022: one call of the runner ($051D++, then one half of the records).
void CtTileAnimRun(uint8_t* w, CtTileAnimDma dma, void* ctx);

// What record k's VRAM holds on a state nobody ran the runner for (a seed): the source of the slot whose step the
// accumulator took last -- the one slot s with slot(acc - step[s]) == s. Returns 1 and the DMA's (vramWord, bank, src),
// 0 for a record with no animation, -1 when more than one slot (or none) fits: the WRAM alone does not say.
int CtTileAnimShown(const uint8_t* w, int k, int* vramWord, int* bank, int* src);
