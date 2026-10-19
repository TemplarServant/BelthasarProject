// lz.c — Chrono Trigger LZ decompressor. See lz.h.
//
// Faithful port of the canonical routine reverse-engineered by Michael Springer
// ("Evil Peer"), as implemented in CTViewer (util/lz_decompress.rs). The format
// is a segmented LZ: a 2-byte size header, then flag bytes whose bits select
// literal (0) or back-reference (1). Back-references are 2-byte pairs; the
// window is 11-bit (small) or 12-bit (large), chosen once from the top bits of
// the byte just past the first segment. Segments can rebase via a 3-byte marker.
//
// BOUNDS. The algorithm itself has no terminator it can trust: a stream that is
// not actually LZ drives byte_pos and work_pos wherever its bytes say. Every
// read and write is therefore range-checked against the caller's caps, and any
// violation returns 0 ("not a stream I can decompress") rather than walking off
// the buffer. ct_lz_decompress keeps the old uncapped signature by passing
// SIZE_MAX, so a well-formed block decompresses byte-for-byte as before -- the
// checks only ever fire on input the routine could not have handled anyway.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lz.h"
#include "assets.h"

#define LZ_NO_CAP ((size_t)-1)

static int lz_copy_bytes(const uint8_t* data, size_t dataLen, uint8_t* out, size_t outCap,
                         size_t* byte_pos, size_t* work_pos, int small) {
  if (*byte_pos + 1 >= dataLen) return 0;
  uint16_t count = data[*byte_pos + 1];
  if (small) count >>= 3; else count >>= 4;
  count += 2;
  uint16_t off = (uint16_t)(((uint16_t)data[*byte_pos + 1] << 8) | data[*byte_pos]);
  if (small) off &= 0x07FF; else off &= 0x0FFF;
  if ((size_t)off > *work_pos) return 0;          // a back-reference before the output start
  if (*work_pos + (size_t)count + 1 > outCap) return 0;
  for (size_t i = 0; i < (size_t)count + 1; i++)
    out[*work_pos + i] = out[*work_pos - off + i];
  *work_pos += (size_t)count + 1;
  *byte_pos += 2;
  return 1;
}

static const CtAssets* g_lzAssets;
static const uint8_t* g_lzRom;
void ct_lz_use_assets(const void* assets, const uint8_t* rom) { g_lzAssets = (const CtAssets*)assets; g_lzRom = rom; }
const uint8_t* ct_lz_asset(size_t start, size_t* outLen, long* cyc7e, long* cyc7f) {
  uint32_t n = 0;
  const uint8_t* e = g_lzAssets ? CtAssetGet(g_lzAssets, CT_ASSET_LZ, (uint32_t)start, &n) : NULL;
  if (!e || n < 12) return NULL;
  uint32_t len = (uint32_t)(e[0] | e[1] << 8 | e[2] << 16 | (uint32_t)e[3] << 24);
  if (len + 12 != n) return NULL;
  if (outLen) *outLen = len;
  if (cyc7e) *cyc7e = (long)(int32_t)(e[4] | e[5] << 8 | e[6] << 16 | (uint32_t)e[7] << 24);
  if (cyc7f) *cyc7f = (long)(int32_t)(e[8] | e[9] << 8 | e[10] << 16 | (uint32_t)e[11] << 24);
  return e + 12;
}

size_t ct_lz_decompress_bounded(const uint8_t* data, size_t dataLen,
                                uint8_t* out, size_t outCap, size_t start) {
  if (data && data == g_lzRom) {                 // the ROM's stream: the container's copy, when it holds one
    size_t n = 0; const uint8_t* e = ct_lz_asset(start, &n, NULL, NULL);
    if (e) { if (n > outCap) return 0; memcpy(out, e, n); return n; }
  }
  if (start + 2 >= dataLen) return 0;
  size_t compressed_size = ((size_t)data[start + 1] << 8) | data[start];
  size_t byte_pos = start + 2;
  size_t byte_after = byte_pos + compressed_size;
  size_t work_pos = 0;
  uint8_t bits_count = 8;
  uint8_t cur_byte;
  if (byte_after >= dataLen) return 0;
  int small = (data[byte_after] & 0xC0) != 0;

  for (;;) {
    if (byte_pos >= dataLen) return 0;
    if (byte_pos == byte_after) {
      // segment boundary: control byte (low 6 bits) gives the next bit count,
      // 0 ends the stream; next 2 bytes rebase the segment end.
      cur_byte = data[byte_pos] & 0x3F;
      if (cur_byte == 0) return work_pos;
      if (byte_pos + 2 >= dataLen) return 0;
      bits_count = cur_byte;
      byte_after = start + (((size_t)data[byte_pos + 2] << 8) | data[byte_pos + 1]);
      byte_pos += 3;
    } else {
      cur_byte = data[byte_pos];
      if (cur_byte == 0) {
        // flag byte 0x00: eight raw literal bytes follow.
        if (byte_pos + 8 >= dataLen || work_pos + 8 > outCap) return 0;
        for (int i = 1; i <= 8; i++) out[work_pos++] = data[byte_pos + i];
        byte_pos += 9;
      } else {
        byte_pos += 1;
        int carry = cur_byte & 0x01;
        cur_byte >>= 1;
        uint8_t mem = cur_byte;
        if (carry) { if (!lz_copy_bytes(data, dataLen, out, outCap, &byte_pos, &work_pos, small)) return 0; }
        else       { if (byte_pos >= dataLen || work_pos >= outCap) return 0;
                     out[work_pos++] = data[byte_pos++]; }
        for (;;) {
          bits_count--;
          if (bits_count == 0) { bits_count = 8; break; }
          carry = mem & 0x01;
          mem >>= 1;
          if (carry) { if (!lz_copy_bytes(data, dataLen, out, outCap, &byte_pos, &work_pos, small)) return 0; }
          else       { if (byte_pos >= dataLen || work_pos >= outCap) return 0;
                       out[work_pos++] = data[byte_pos++]; }
        }
      }
    }
  }
}

size_t ct_lz_decompress(const uint8_t* data, uint8_t* out, size_t start) {
  return ct_lz_decompress_bounded(data, LZ_NO_CAP, out, LZ_NO_CAP, start);
}

// ============================================================================================
// THE ROM'S COST of a decompression -- C3/0557 priced instruction by instruction.
// (disasm/notes/field_load_cost.md.) The port decompresses in no time at all; the ROM spends
// most of a location load in here, and a black hold that is not this long is not the ROM's.
//
// The loop, from the listing (large-window arm, C3/05E5-0658; the 11-bit arm at C3/076B is the
// same code with one LSR A fewer in the match body and an AND #07FF mask):
//     05E5 LDA #08 / STA $0B                    a group is 8 flags
//     05E9 CPX $09 / BEQ ->0644                 at the block end -> the chain
//     05ED LDA $0000,X / BEQ ->059D             a ZERO control byte = 8 unrolled literals
//     05F2 INX / LSR A / STA $0D / BCS ->0614   first flag: set = match
//     05F8 literal: LDA $0000,X / STA $002180 / INY / INX
//     0601 DEC $0B / BEQ ->05E5 / LSR $0D / BCS ->0614   ...then the next flag
//     0614 match: len = (b1>>4)+2, offset = u12, MVN moves len+1 bytes
//     0644 block chain: (X)&3F = the next group's flag count, 0 = end
// Timings are LakeSnes's, which are the SNES's: a ROM byte in a bank >= $80 with FastROM on
// costs 6 master cycles, an idle 6, a direct-page or WRAM access 8, the $2180 write 6, and MVN
// re-fetches its three opcode bytes per byte moved (18 + 8 + 8 + 12 = 46).
#define LZC_F   6                       /* a code/ROM byte (FastROM, banks >= $80) */
#define LZC_I   6                       /* internal cycle */
#define LZC_D   8                       /* direct page / stack (bank 0 below $2000) */
#define LZC_B   6                       /* $2180/$2181, the WRAM data port */
#define LZC_BR_NT   (2 * LZC_F)                          /* branch not taken            12 */
#define LZC_BR_T    (2 * LZC_F + LZC_I)                  /* branch taken                18 */
#define LZC_LDA_ABX8  (3 * LZC_F + LZC_I + LZC_F)        /* LDA $0000,X   (M=8, X=16)   30 */
#define LZC_LDA_ABX16 (3 * LZC_F + LZC_I + 2 * LZC_F)    /* LDA $0000,X   (M=16)        36 */
#define LZC_STA_L8    (4 * LZC_F + LZC_B)                /* STA $002180                 30 */
#define LZC_STA_L16   (4 * LZC_F + 2 * LZC_B)            /* STA $002181   (M=16)        36 */
#define LZC_IMP       (2 * LZC_F)                        /* INX/INY/TAX/TYA/SEC/LSR A   12 */
#define LZC_DP8       (2 * LZC_F + LZC_D)                /* STA $0B                     20 */
#define LZC_DP16      (2 * LZC_F + 2 * LZC_D)            /* LDA/STA/SBC/STX/LDX dp, M16 28 */
#define LZC_RMW8      (2 * LZC_F + LZC_D + LZC_I + LZC_D)/* DEC $0B / LSR $0D           34 */
#define LZC_REPSEP    (2 * LZC_F + LZC_I)                /* REP/SEP                     18 */
#define LZC_IMM16     (3 * LZC_F)                        /* AND #0FFF / ADC #0009       18 */
#define LZC_IMM8      (2 * LZC_F)                        /* LDA #08 / AND #3F           12 */
#define LZC_PHB       (LZC_F + LZC_I + LZC_D)            /*                             20 */
#define LZC_PLB       (LZC_F + 2 * LZC_I + LZC_D)        /*                             26 */
#define LZC_MVN_BYTE  (3 * LZC_F + 2 * LZC_D + 2 * LZC_I)/* per byte moved              46 */
#define LZC_JMP       (3 * LZC_F)                        /*                             18 */

#define LZC_HDR   (LZC_DP16 + LZC_BR_NT + LZC_LDA_ABX8 + LZC_BR_NT + LZC_IMP + LZC_IMP + LZC_DP8)
#define LZC_TAIL  (LZC_RMW8 + LZC_BR_NT + LZC_RMW8)
#define LZC_LIT   (LZC_LDA_ABX8 + LZC_STA_L8 + LZC_IMP + LZC_IMP)
#define LZC_MAT   (LZC_LDA_ABX8 + 4 * LZC_IMP + 2 * LZC_IMP + LZC_DP8 + LZC_REPSEP + LZC_LDA_ABX16 + \
                   LZC_IMM16 + LZC_DP16 + LZC_IMP + LZC_IMP + LZC_DP16 + LZC_DP16 + LZC_IMP + \
                   LZC_DP16 + LZC_PHB + LZC_PLB + LZC_IMP + LZC_STA_L16 + LZC_REPSEP + LZC_DP16 + \
                   LZC_IMP + LZC_IMP + LZC_BR_T)
#define LZC_GRPEND (LZC_RMW8 + LZC_BR_T + LZC_IMM8 + LZC_DP8)
#define LZC_FAST8  (LZC_DP16 + LZC_BR_NT + LZC_LDA_ABX8 + LZC_BR_T + \
                    8 * (LZC_LDA_ABX8 + LZC_STA_L8) + \
                    LZC_REPSEP + LZC_IMP + LZC_IMM16 + LZC_IMP + LZC_IMP + LZC_IMM16 + LZC_IMP + \
                    LZC_REPSEP + LZC_BR_T)
#define LZC_BLKMORE (LZC_DP16 + LZC_BR_T + LZC_LDA_ABX8 + LZC_IMM8 + LZC_BR_NT + LZC_DP8 + \
                     LZC_REPSEP + LZC_LDA_ABX16 + LZC_DP16 + LZC_DP16 + 3 * LZC_IMP + LZC_REPSEP + LZC_BR_T)
#define LZC_BLKEND  (LZC_DP16 + LZC_BR_T + LZC_LDA_ABX8 + LZC_IMM8 + LZC_BR_T + LZC_JMP)

// HiROM: banks $C0-$FF map straight onto the file, so the file offset of a 24-bit ROM address is
// (bank - $C0) << 16 | offset -- the same mapping tools/dis.py and fieldStageExit use.
size_t ct_lz_hirom_offset(unsigned addr24) {
  return (size_t)(((addr24 >> 16) - 0xC0u) & 0x3Fu) << 16 | (addr24 & 0xFFFFu);
}

long ct_lz_rom_cycles(const uint8_t* data, size_t dataLen, size_t start, int dstBankOdd) {
  if (data && data == g_lzRom) {
    long c7e = 0, c7f = 0;
    if (ct_lz_asset(start, NULL, &c7e, &c7f)) return dstBankOdd ? c7f : c7e;
  }
  if (!data || start + 2 >= dataLen) return 0;
  size_t base = start;
  size_t x = base + 2;
  size_t end = base + 2 + (((size_t)data[base + 1] << 8) | data[base]);
  if (end >= dataLen) return 0;
  int small = (data[end] & 0xC0) != 0;
  // The dispatch at C3/0580-059A, measured: the four arms differ only here (spread 0 inside each).
  long cyc = small ? (dstBankOdd ? 1030 : 1054) : (dstBankOdd ? 1042 : 1030);
  long matBody = LZC_MAT - (small ? LZC_IMP : 0);   /* 076B decodes the length with three LSRs */
  int flags = 8;
  for (;;) {
    if (x == end) {
      int b = data[x] & 0x3F;                                  /* 0644 */
      if (b == 0) return cyc + LZC_BLKEND;
      if (x + 2 >= dataLen) return 0;
      cyc += LZC_BLKMORE;
      flags = b;
      end = base + (((size_t)data[x + 2] << 8) | data[x + 1]);
      x += 3;
      if (end >= dataLen || end < base) return 0;
      continue;
    }
    if (x > end || x >= dataLen) return 0;
    int c = data[x];
    if (c == 0) { cyc += LZC_FAST8; x += 9; continue; }         /* 05ED BEQ ->059D */
    cyc += LZC_HDR;
    x++;
    int first = 1;
    for (;;) {
      if (!first) cyc += LZC_TAIL;
      if (c & 1) {
        if (x + 1 >= dataLen) return 0;
        int ln = ((small ? (data[x + 1] >> 3) : (data[x + 1] >> 4)) & 0x1F) + 2;
        cyc += LZC_BR_T + matBody + (long)LZC_MVN_BYTE * (ln + 1);
        x += 2;
      } else {
        cyc += LZC_BR_NT + LZC_LIT + (first ? 0 : LZC_BR_T);    /* 0612 BRA on later literals */
        x++;
      }
      c >>= 1;
      first = 0;
      if (--flags == 0) { flags = 8; cyc += LZC_GRPEND; break; }
    }
  }
}

// ============================================================================================
// C0/A33B's MAP BUILD -- the half after its `JSL C3/0002`, priced the same way.
// (disasm/notes/field_load_cost.md.) The decompressed map is read back through the
// auto-incrementing WRAM port ($2180) into four planes:
//
//   A548 -> $7E:3000   dims $0BCB x $0BCD          A57D -> $7E:3040   dims $0BCF x $0BD1
//   A5B9 -> $7E:3080   dims $0BD3 x $0BD5, only when map byte 1 has bit 7 set ($0BCA)
//   A5F0 -> $7E:30C0 + $7E:7000 + $7E:7040, dims $0BCB x $0BCD and RLE-coded: a byte with
//           bit 7 set opens a RUN (three plane bytes then a length, 0 meaning 256) and the
//           run's own cell is written by the header; anything else is three literal bytes.
//
// and the dimensions come from the first two bytes of the map itself (C0/A3C1-A408):
//   $0BCB = ((b0 & 03) + 1) << 4   $0BCD = ((b0 & 0C) + 4) << 2
//   $0BCF =  (b0 & 30) + 10        $0BD1 = ((b0 & C0) >> 2) + 10
//   $0BD3 = ((b1 & 03) + 1) << 4   $0BD5 = ((b1 & 0C) + 4) << 2
//
// The structure was checked against the ROM's own execution counts, not argued from the
// listing: CT_CALLROM_PCHIST on scene 468 reports A548 4096, A554 64, A5F0 838, A5F5 447,
// A61A 391, A641 3649, A657 3258, A60C 6, A649 58 -- and this walk reproduces every one.
#define MBC_LDA_ABS_B (3 * LZC_F + LZC_B)          /* LDA $2180                  24 */
#define MBC_STA_LX    (4 * LZC_F + LZC_D)          /* STA $7E3000,X              32 */
#define MBC_IMP       (2 * LZC_F)                  /* INX/TAX/XBA/TDC/DEY/TAY    12 */
#define MBC_CPX       (2 * LZC_F + 2 * LZC_D)      /* CPX $54  (X 16-bit)        28 */
#define MBC_DP8       (2 * LZC_F + LZC_D)          /* LDA/STA $55                20 */
#define MBC_IMM8      (2 * LZC_F)                  /* LDA #00 / AND #7F          12 */
#define MBC_LDY16     (3 * LZC_F)                  /* LDY #0100                  18 */
#define MBC_RTS       (LZC_F + 3 * LZC_I + 2 * LZC_D)
/* A554-A55F and its clones: LDA $55 / INC A / STA $55 / XBA / LDA #00 / TAX / CPX $52 */
#define MBC_ROWEND (MBC_DP8 + MBC_IMP + MBC_DP8 + MBC_IMP + MBC_IMM8 + MBC_IMP + MBC_CPX)
/* C0/A67B-A693 moves a fixed 4032-byte block with MVN -- 4032 in every location measured. */
#define MBC_MVN_BLOCK (4032L * LZC_MVN_BYTE)
/* The one-shot half -- the header parse, the two fixed 64/63-iteration loops at C0/A670/A67F
   and the tail past the MVN. A CONSTANT of the routine, measured over five locations
   (26780, 27044, 28118, 28898, 28960: median 28118, spread 2180 = 0.006 of a load frame). */
#define MBC_ONESHOT 28118L

static long mbcCopyPlane(int cols, int rows) {           /* A548-A55F */
  if (cols <= 0 || rows <= 0) return 0;
  long c = 0;
  for (int row = 0; row < rows; row++) {
    c += (long)cols * (MBC_LDA_ABS_B + MBC_STA_LX + MBC_IMP + MBC_CPX);
    c += (long)(cols - 1) * LZC_BR_T + LZC_BR_NT;        /* A552, the last one falls through */
    c += MBC_ROWEND + (row + 1 < rows ? LZC_BR_T : LZC_BR_NT);
  }
  return c;
}

long ct_map_build_cycles(const uint8_t* m, size_t len) {
  if (!m || len < 8) return 0;
  const int colsA = ((m[0] & 3) + 1) << 4, rowsA = ((m[0] & 0x0C) + 4) << 2;
  const int colsB = (m[0] & 0x30) + 0x10,  rowsB = ((m[0] & 0xC0) >> 2) + 0x10;
  const int cols3 = ((m[1] & 3) + 1) << 4, rows3 = ((m[1] & 0x0C) + 4) << 2;
  const int layer3 = (m[1] & 0x80) != 0;
  long c = mbcCopyPlane(colsA, rowsA) + mbcCopyPlane(colsB, rowsB);
  if (layer3) c += mbcCopyPlane(cols3, rows3);
  size_t p = 6 + (size_t)colsA * rowsA + (size_t)colsB * rowsB
             + (layer3 ? (size_t)cols3 * rows3 : 0);
  int row = 0, col = 0;
  while (row < rowsA) {
    if (p >= len) return 0;                              /* not a map this walk understands */
    int b = m[p];
    c += MBC_LDA_ABS_B;                                  /* A5F0 */
    if (!(b & 0x80)) {                                   /* A5F3 BMI not taken: three literals */
      if (p + 3 > len) return 0;
      c += LZC_BR_NT + 3 * MBC_STA_LX + 2 * MBC_LDA_ABS_B;
      p += 3;
      c += MBC_IMP + MBC_CPX;                            /* A607 INX / CPX */
      if (++col < colsA) { c += LZC_BR_T; continue; }
      c += LZC_BR_NT + MBC_ROWEND;                       /* A60C-A615 */
      col = 0;
      if (++row < rowsA) { c += LZC_BR_T; continue; }
      return c + LZC_BR_NT + MBC_RTS + MBC_MVN_BLOCK + MBC_ONESHOT;   /* A619 */
    }
    if (p + 4 > len) return 0;
    c += LZC_BR_T + MBC_IMM8 + 3 * (MBC_DP8 + MBC_STA_LX) + 2 * MBC_LDA_ABS_B;  /* A61A-A630 */
    c += 2 * MBC_IMP + MBC_LDA_ABS_B;                    /* TDC / XBA / LDA len */
    int ln = m[p + 3];
    p += 4;
    int y;
    if (ln) { c += LZC_BR_T + MBC_IMP; y = ln; }         /* A639 BNE -> A640 TAY */
    else    { c += LZC_BR_NT + MBC_LDY16; y = 0x100; }   /* A63B LDY #0100 */
    for (;;) {
      c += MBC_IMP;                                      /* A641 DEY */
      if (--y == 0) {
        c += LZC_BR_T + MBC_IMP + MBC_CPX;               /* A642 -> A607 INX / CPX */
        if (++col < colsA) { c += LZC_BR_T; break; }
        c += LZC_BR_NT + MBC_ROWEND;
        col = 0;
        if (++row < rowsA) { c += LZC_BR_T; break; }
        return c + LZC_BR_NT + MBC_RTS + MBC_MVN_BLOCK + MBC_ONESHOT;
      }
      c += LZC_BR_NT + MBC_IMP + MBC_CPX;                /* A644 INX / A645 CPX */
      if (++col < colsA) c += LZC_BR_T;                  /* A647 BCC ->A657 */
      else {
        c += LZC_BR_NT + MBC_ROWEND;                     /* A649-A654 */
        col = 0;
        if (++row >= rowsA) return c + LZC_BR_NT + MBC_RTS + MBC_MVN_BLOCK + MBC_ONESHOT;   /* A656 */
        c += LZC_BR_T;
      }
      c += 3 * (MBC_DP8 + MBC_STA_LX) + LZC_BR_T;        /* A657-A669 */
    }
  }
  return c;
}

// C0/A33B WALKED -- everything the map load costs besides its decompression, cycle for cycle (the traces' costs,
// tools/map_cost_proto.py is the twin; exact against the cartridge on loc $082 and $005). The model above stands in
// for it only where the walk stops (a map whose header bytes 2-3 set up layer scrolls: C0/A463 / C0/A4B3, no trace).
//   the header parse C0/A33B-A508: map byte 1's bits 7-4 ($0BCA, $0BC9, $36, ..), $01BB (C0/0BE8 clears it on every
//     location entry; C0/6DCF's FF arm -- record byte 2 = FF -- sets it), bytes 2 and 3 zero;
//   C0/7399: each of the six plane dimensions -> a mask by a CMP #40 / #20 / #30 chain, then the record's bounds
//     (bytes 10-13, or the whole plane's when byte 10 is negative: C0/7495);
//   C0/A521: the three plane copies through $2180 (the third only with byte 1 bit 7), then the RLE plane -- a byte
//     with bit 7 opens a run, a zero length is 256 (C0/A63B `LDY #0100` / BRA past the TAY);
//   C0/A66B: the $7E:7080 fill, 63 MVNs of 64 bytes, then the location's $E5:0000 list of 7-byte entries: a count
//     with bit 7 writes a COLUMN (C0/A6DC, +$100 a step), else a row; a row's `AND $1E` (C0/A6C7) is 16-bit on its
//     first pass and 8-bit after (the SEP at C0/A6CA), a column's (C0/A6E7) 16-bit on every pass (its step's own
//     REP #20 at C0/A6F8);
//   + the `JSL C3:0002` vector's JMP (18).
long ct_map_load_cycles(const uint8_t* rom, size_t romLen, int loc) {
  if (!rom || loc < 0 || loc > 511) return -1;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return -1;
  unsigned mapId = (unsigned)rom[REC + 4] | ((unsigned)rom[REC + 5] << 8);
  size_t me = 0x361E00u + (size_t)mapId * 3;
  if (me + 3 > romLen) return -1;
  size_t src = ct_lz_hirom_offset((unsigned)rom[me] | ((unsigned)rom[me + 1] << 8) | ((unsigned)rom[me + 2] << 16));
  uint8_t* m = (uint8_t*)malloc(0x20000);
  if (!m) return -1;
  size_t len = ct_lz_decompress_bounded(rom, romLen, m, 0x20000, src);
  long k = -1;
  if (len < 8 || m[2] || m[3]) goto out;                  /* C0/A463 / C0/A4B3: not traced */
  {
    const int b1 = m[1], bb = rom[REC + 2] == 0xFF;
    k = 780 + ((b1 & 0x80) ? 68 : 44) + 32 + 56 + 32 + 50 + 32 + ((b1 & 0x10) ? 64 : 18) + 952   /* the header */
      + (bb ? 76 : 18) + 240 + (bb ? 76 : 18) + 334;
    const int dims[6] = { ((m[0] & 3) + 1) << 4, ((m[0] & 0x0C) + 4) << 2, (m[0] & 0x30) + 0x10,
                          ((m[0] & 0xC0) >> 2) + 0x10, ((m[1] & 3) + 1) << 4, ((m[1] & 0x0C) + 4) << 2 };
    for (int i = 0; i < 6; i++)                            /* C0/7399 */
      k += 38 + (dims[i] == 0x40 ? 50 : dims[i] == 0x20 ? 92 : dims[i] == 0x30 ? 116 : 110);
    k += 64 + ((rom[REC + 10] & 0x80) ? 214 : 222);
    if (getenv("CT_MAPCOST_DEBUG")) fprintf(stderr, "mapcost: after %s %ld\n", "masks", k);
    static const long head[3] = { 336, 252, 252 };        /* C0/A521's plane copies */
    for (int i = 0; i < 3; i++) {
      if (i == 2) { k += 38; if (!(b1 & 0x80)) { k += 18; break; } k += 12; }
      int cols = dims[2 * i], rows = dims[2 * i + 1];
      k += head[i] + (long)rows * ((long)cols * 96 + (cols - 1) * 18 + 12 + 122) + (rows - 1) * 18 + 12;
    }
    if (getenv("CT_MAPCOST_DEBUG")) fprintf(stderr, "mapcost: after %s %ld\n", "planes", k);
    int cols = dims[0], rows = dims[1];                   /* the RLE plane */
    size_t p = 6 + (size_t)dims[0] * dims[1] + (size_t)dims[2] * dims[3] + ((b1 & 0x80) ? (size_t)dims[4] * dims[5] : 0);
    int row = 0, col = 0, done = 0;
    k += 282;
    while (!done) {
      if (p >= len || ((m[p] & 0x80) ? p + 4 : p + 3) > len) { k = -1; goto out; }   /* a literal 3 bytes, a run 4 */
      int b = m[p];
      k += 24;
      int adv = 0;
      if (!(b & 0x80)) { k += 156; p += 3; adv = 1; }
      else {
        k += 288;
        int ln = m[p + 3], y;
        p += 4;
        if (ln) { k += 30; y = ln; } else { k += 48; y = 0x100; }
        for (;;) {
          k += 12;                                         /* DEY */
          if (--y == 0) { k += 18; adv = 1; break; }
          k += 52;                                         /* INX / CPX */
          if (++col < cols) k += 18;
          else {
            k += 134; col = 0;
            if (++row >= rows) { k += 12 + 40; done = 1; break; }
            k += 18;
          }
          k += 174;                                        /* the run's cell */
        }
      }
      if (adv && !done) {                                  /* C0/A607 INX / CPX / BCC, the row end */
        k += 40;
        if (++col < cols) k += 18;
        else {
          k += 134; col = 0;
          if (++row < rows) k += 18; else { k += 12 + 40; done = 1; }
        }
      }
    }
    if (getenv("CT_MAPCOST_DEBUG")) fprintf(stderr, "mapcost: after %s %ld\n", "rle", k);
    k += 201766;                                           /* C0/A66B's fill and MVNs */
    long e5 = 0x250000L;
    unsigned x = rom[e5 + 2 * loc] | (rom[e5 + 2 * loc + 1] << 8), end = rom[e5 + 2 * loc + 2] | (rom[e5 + 2 * loc + 3] << 8);
    for (int guard = 0; ; guard++) {
      k += 28;
      if (x >= end || guard > 4096) { k += 76; break; }
      k += 78;
      int n = rom[e5 + x + 2];
      int y = n & 0x7F, first = 1;
      k += (n & 0x80) ? 124 : 106;                        /* a column (C0/A6DC) / a row */
      for (;;) {
        k += (first || (n & 0x80)) ? 128 : 120;
        first = 0;
        if (y == 0) { k += 18; break; }
        k += (n & 0x80) ? 102 : 66;
        y--;
      }
      k += 138;
      x += 7;
    }
    if (getenv("CT_MAPCOST_DEBUG")) fprintf(stderr, "mapcost: after %s %ld\n", "a66b", k);
    k += 18;                                               /* the C3:0002 vector */
  }
out:
  free(m);
  return k;
}

// --------------------------------------------------------------------------------------------
// WHAT A LOCATION LOAD DECOMPRESSES, and what the ROM pays for it. The tables are the location
// loader's own (bank $F6), read here exactly as C0/0960 / C0/A33B / C0/09DD read them:
//   location record   $F6:0000 + loc*14   -- byte 1 = tileset id, word 4 = map id
//   packet list       $F6:1C00 + tileset*8   (8 entries, $FF = none)
//   packet pointers   $F6:2220 + idx*3       (24-bit source)
//   map pointers      $F6:1E00 + mapId*3
//   prop pointer      $F6:2100 + tileset*3
// C0/0960 sends entries 0-5 through C0/6D2F (decompress to $7E:3000, then C0/2DC8 DMAs $1000
// bytes to VRAM -- $0F80 for entry 5), then falls into C0/6D65/6D97, which decompress entries 6
// and 7 to $7F:6000 and $7F:7000 with NO DMA. C0/A33B decompresses the map to $7E:B500 and
// C0/09DD the tile properties to $7E:B000.
// The DMA is 8 master cycles a byte plus the channel setup, measured in the oracle's own bucket
// (CT_HOOK_CYCLES: dma=32792 on the frames that upload $1000 bytes = 8*4096 + 24).
// ============================================================================================
// THE OVERWORLD LOAD -- bank C2's own packet list, and what it costs.
// (disasm/notes/field_load_cost.md.) Leaving a location for the world map runs C2's loader, and it
// decompresses with the SAME C3/0557 this file already prices: bank $C2 holds 23 `JSL C3/0002`
// sites and a world load runs sixteen of them, 24 packets in all.
//
//   C2/275B  LDA $C6FD00,X          world*2 -> the world's HEADER pointer, bank $C6
//   each site: an index byte out of that header (BMI = bit 7 set = skip), *3 into a 3-byte
//              pointer table, then $0300/$0302 = source, $0305 = destination bank.
//
// The site list below is MEASURED, not read off a call graph: `--hookpc C2xxxx` on each of the 23
// sites says which ran and on which frame, and `--hookpc C30557 --hookwatch 0300/0302/0305` gives
// every call's source and destination. World 0's 24 packets come out in the oracle's own order,
// byte for byte.
typedef struct { uint16_t site; int16_t hdrIdx; uint32_t table; uint8_t dstBank; } CtOwSite;
static const CtOwSite CT_OW_SITES[] = {
  { 0x27A2, 0x0C, 0xC6FDF0, 0x7F }, { 0x27A2, 0x0D, 0xC6FDF0, 0x7F },
  { 0x27A2, 0x0E, 0xC6FDF0, 0x7F }, { 0x27A2, 0x0F, 0xC6FDF0, 0x7F },
  { 0x2B7C,   -1, 0xC6FDF0, 0x7F },                      /* fixed entry 0 */
  { 0x280C, 0x00, 0xC6FE20, 0x7F }, { 0x280C, 0x01, 0xC6FE20, 0x7F },
  { 0x280C, 0x02, 0xC6FE20, 0x7F }, { 0x280C, 0x03, 0xC6FE20, 0x7F },
  { 0x280C, 0x04, 0xC6FE20, 0x7F }, { 0x280C, 0x05, 0xC6FE20, 0x7F },
  { 0x280C, 0x06, 0xC6FE20, 0x7F },
  { 0x284F, 0x08, 0xC6FE20, 0x7F },
  { 0x287D, 0x14, 0xC6FF40, 0x7F },
  { 0x28AB, 0x0A, 0xC6FEA0, 0x7F },
  { 0x28CC,   -1, 0xC6FEA0, 0x7F },                      /* fixed entry 0 */
  { 0x27D9,   -2, 0xC6FDF0, 0x7E },                      /* fixed entry 1 ($C6:FDF3) */
  { 0x2987, 0x10, 0xC6FF00, 0x7E },
  { 0x29B5, 0x11, 0xC6FF20, 0x7E },
  { 0x29E3, 0x12, 0xC6FF80, 0x7E },
  { 0x2A11, 0x13, 0xC6FFA0, 0x7E },
  { 0x2A6D, 0x15, 0xC6FFC0, 0x7F },
  { 0x2A3F, 0x16, 0xC6FFE0, 0x7F },
  { 0x2959, 0x0B, 0xC6FEA0, 0x7E },
};
#define CT_OW_SITE_N ((int)(sizeof CT_OW_SITES / sizeof CT_OW_SITES[0]))

int ct_lz_world_packets(const uint8_t* rom, size_t romLen, int world,
                        uint32_t* srcOut, uint8_t* dstOut, int max) {
  if (!rom || world < 0 || world > 7) return 0;
  size_t hp = ct_lz_hirom_offset(0xC6FD00) + (size_t)world * 2;
  if (hp + 2 > romLen) return 0;
  size_t hdr = ct_lz_hirom_offset(0xC60000u | ((unsigned)rom[hp] | ((unsigned)rom[hp + 1] << 8)));
  int n = 0;
  for (int i = 0; i < CT_OW_SITE_N && n < max; i++) {
    const CtOwSite* s = &CT_OW_SITES[i];
    int idx;
    if (s->hdrIdx == -1) idx = 0;                        /* the site's own fixed entry */
    else if (s->hdrIdx == -2) idx = 1;
    else {
      if (hdr + (size_t)s->hdrIdx >= romLen) continue;
      idx = rom[hdr + s->hdrIdx];
      if (idx & 0x80) continue;                          /* the loader's own BMI: bit 7 = no packet */
    }
    size_t e = ct_lz_hirom_offset(s->table) + (size_t)idx * 3;
    if (e + 3 > romLen) continue;
    srcOut[n] = (unsigned)rom[e] | ((unsigned)rom[e + 1] << 8) | ((unsigned)rom[e + 2] << 16);
    dstOut[n] = s->dstBank;
    n++;
  }
  return n;
}

// What bank C2's loader spends decompressing for one world, priced with the same cycle-exact model
// as a field load. THE REST of its window -- setup, DMA, palettes, whatever else C2 does between
// C0/0066 and the world engine's first tick -- is not derived: measured on world 0 it is the
// difference between the whole window (44 frames from the commit, oracle) and this (35.1), i.e.
// CT_OW_LOAD_REST below.
// C2/2C93, the world's REBUILD after the X menu (C2/2556 -> C2/2E72 restore -> C2/03EF -> C2/2C93): the same loaders
// as the entry minus four -- MEASURED by --cputrace over the return (menu_audit_cmp.py's owsave drive, world 0): twenty
// `JSL C3/0002`, from 27A2 x4, 2B7C, 280C x7, 284F, 287D, 28AB, 28CC, 27D9, 2987, 29E3, 2959 over f302-f332; the entry's
// 29B5 / 2A11 / 2A6D / 2A3F never run (their packets survive the menu). Priced with the same decompressor model.
long ct_lz_world_return_cost(const uint8_t* rom, size_t romLen, int world, int* outPackets) {
  if (outPackets) *outPackets = 0;
  if (!rom || world < 0 || world > 7) return 0;
  size_t hp = ct_lz_hirom_offset(0xC6FD00) + (size_t)world * 2;
  if (hp + 2 > romLen) return 0;
  size_t hdr = ct_lz_hirom_offset(0xC60000u | ((unsigned)rom[hp] | ((unsigned)rom[hp + 1] << 8)));
  long cyc = 0; int n = 0;
  for (int i = 0; i < CT_OW_SITE_N; i++) {
    const CtOwSite* s = &CT_OW_SITES[i];
    if (s->site == 0x29B5 || s->site == 0x2A11 || s->site == 0x2A6D || s->site == 0x2A3F) continue;
    int idx;
    if (s->hdrIdx == -1) idx = 0;
    else if (s->hdrIdx == -2) idx = 1;
    else {
      if (hdr + (size_t)s->hdrIdx >= romLen) continue;
      idx = rom[hdr + s->hdrIdx];
      if (idx & 0x80) continue;
    }
    size_t e = ct_lz_hirom_offset(s->table) + (size_t)idx * 3;
    if (e + 3 > romLen) continue;
    uint32_t src = (unsigned)rom[e] | ((unsigned)rom[e + 1] << 8) | ((unsigned)rom[e + 2] << 16);
    cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset(src), s->dstBank == 0x7F);
    n++;
  }
  if (outPackets) *outPackets = n;
  return cyc;
}
long ct_lz_world_cost(const uint8_t* rom, size_t romLen, int world, int* outPackets) {
  uint32_t src[32]; uint8_t dst[32];
  int n = ct_lz_world_packets(rom, romLen, world, src, dst, 32);
  long cyc = 0;
  for (int i = 0; i < n; i++)
    cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset(src[i]), dst[i] == 0x7F);
  if (outPackets) *outPackets = n;
  return cyc;
}

// C0/286C, the pipeline pass C0/2848 runs ONCE before its ramp: it builds and uploads everything
// the new location needs (C0/0A50's tilemap build is ~2/3 of it, then B204/A7E9/B0E6/0B28 and the
// rest of the draw chain) and ends in the vblank wait C0/EC60. MEASURED IN SITU, because that
// wait makes it untimeable with --callrom: over three real transitions (twice into scene 468,
// once into 122) the work between C0/2848's entry and C0/EC60 is 1044, 1043 and 1052 lines --
// 1046 lines x 1364 = 1426744 master cycles, a spread of 0.5%. It is a CONSTANT of the routine
// because its loops are screen-sized, not map-sized (the same 4032-byte MVN at C0/A72D and the
// same 256-cell build in every location measured). ⚠ NOT fully derived: C0/0A50 dispatches
// between tilemap builders (C0/7B0x here, C0/7E2x on the one state of five that measured 33%
// higher), and what selects them is not yet read out -- on such a map this constant is about a
// frame light. See disasm/notes/field_load_cost.md.
#define LOADC_FIRST_PASS 1426744L

long g_ctLzSpriteCycles = 0;   // the object pass's own decompressions, filled by the loaders (lz.h)

long ct_lz_load_cost(const uint8_t* rom, size_t romLen, int loc, long* outDma, int* outPackets) {
  if (outDma) *outDma = 0;
  if (outPackets) *outPackets = 0;
  if (!rom || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;      /* $F6:0000, HiROM file offset */
  if (REC + 14 > romLen) return 0;
  long cyc = 0, dma = 0;
  int n = 0;
  const unsigned tileset = rom[REC + 1];
  if (tileset != 0xFF) {
    const size_t LIST = 0x361C00u + (size_t)tileset * 8;
    for (int i = 0; i < 8 && LIST + 8 <= romLen; i++) {
      unsigned idx = rom[LIST + i];
      if (idx == 0xFF) continue;
      size_t pe = 0x362220u + (size_t)idx * 3;
      if (pe + 3 > romLen) continue;
      size_t src = ct_lz_hirom_offset((unsigned)rom[pe] | ((unsigned)rom[pe + 1] << 8) |
                                      ((unsigned)rom[pe + 2] << 16));
      cyc += ct_lz_rom_cycles(rom, romLen, src, i >= 6);   /* 6D65/6D97 land in bank $7F */
      n++;
      if (i < 6) dma += 8L * (i == 5 ? 0x0F80 : 0x1000) + 24;   /* C0/2DC8, the VRAM upload */
    }
    size_t pp = 0x362100u + (size_t)tileset * 3;
    if (pp + 3 <= romLen) {
      size_t src = ct_lz_hirom_offset((unsigned)rom[pp] | ((unsigned)rom[pp + 1] << 8) |
                                      ((unsigned)rom[pp + 2] << 16));
      cyc += ct_lz_rom_cycles(rom, romLen, src, 0);
      n++;
    }
  }
  unsigned mapId = (unsigned)rom[REC + 4] | ((unsigned)rom[REC + 5] << 8);
  size_t me = 0x361E00u + (size_t)mapId * 3;
  if (me + 3 <= romLen) {
    size_t src = ct_lz_hirom_offset((unsigned)rom[me] | ((unsigned)rom[me + 1] << 8) |
                                    ((unsigned)rom[me + 2] << 16));
    cyc += ct_lz_rom_cycles(rom, romLen, src, 0);
    n++;
    // ...and then C0/A33B BUILDS what it just decompressed, which costs as much again. The walk
    // needs the decompressed image, so take it -- this port has the same decompressor the ROM does.
    uint8_t* img = (uint8_t*)malloc(0x20000);
    if (img) {
      size_t mlen = ct_lz_decompress_bounded(rom, romLen, img, 0x20000, src);
      { long w = ct_map_load_cycles(rom, romLen, loc);   /* C0/A33B walked; the model where it stops */
        if (w >= 0) cyc += w; else if (mlen) cyc += ct_map_build_cycles(img, mlen); }
      free(img);
    }
  }
  // ⭐ THE REST OF C0/00F4's PACKETS. The model stopped at C0/09DD, and scene 468 -- the location it was measured on --
  // has no BG3 tileset and a short script, so what it left out was inside its rounding there. The load is
  // `092B / 1B53 / 0960 / 6DCF / 7084 / 7F7E / A33B / 09DD / 0A14 / 56D4`, and three of those decompress too:
  //   C0/6DCF  record byte +2 (the BG3 tileset) != FF: `$F6:2220 + b*3 -> $7F:5080` (6DFE LDA #7F), then C0/2DC8 DMAs
  //            $1000 bytes of it to VRAM $5000 (6E07-6E1E). FF: 6E22 `LDA #01 / STA $BB` and nothing else.
  //   C0/0A14  `LDA $BB / BNE` (so the same byte), `$F6:21C0 + b*3 -> $7E:C000` (0A45 LDA #7E), no upload.
  //   C0/56D4  the location's SCRIPT: record word +8, `$FC:F9F0 + id*3 -> $7F:2000` (56FA LDA #7F).
  // MEASURED (`--cputrace` of the attract's reboot into loc 112, from f2435): C0:6E03 `E5:3C21 -> 7F:5080` in f2454,
  // C0:0A4A `EF:8F48 -> 7E:C000` in f2462, C0:56FF `FB:8021 -> 7F:2000` in f2463, between the map and the object pass.
  {
    const unsigned b3 = rom[REC + 2];
    if (b3 != 0xFF) {
      size_t pe = 0x362220u + (size_t)b3 * 3, pa = 0x3621C0u + (size_t)b3 * 3;
      if (pe + 3 <= romLen) {
        cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)rom[pe] | ((unsigned)rom[pe + 1] << 8) |
                                                               ((unsigned)rom[pe + 2] << 16)), 1);
        n++;
        dma += 8L * 0x1000 + 24;                                        /* 6E1E JSR C0/2DC8, $1000 bytes */
      }
      if (pa + 3 <= romLen) {
        cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)rom[pa] | ((unsigned)rom[pa + 1] << 8) |
                                                               ((unsigned)rom[pa + 2] << 16)), 0);
        n++;
      }
    }
    const unsigned sid = (unsigned)rom[REC + 8] | ((unsigned)rom[REC + 9] << 8);
    size_t ps = ct_lz_hirom_offset(0xFCF9F0u) + (size_t)sid * 3;
    if (ps + 3 <= romLen) {
      cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)rom[ps] | ((unsigned)rom[ps + 1] << 8) |
                                                             ((unsigned)rom[ps + 2] << 16)), 1);
      n++;
    }
  }
  cyc += LOADC_FIRST_PASS;              // C0/2848's own first frame, before the ramp
  if (outDma) *outDma = dma;
  if (outPackets) *outPackets = n;
  return cyc;
}

// THE FIELD'S RETURN FROM A BATTLE (disasm/notes/sequence_table.md 11.10). C0/18C1 `JSR C0/0283` -> C0/0304 re-runs a
// SUBSET of the location load -- C0/A33B (the map, decompressed and built again), C0/09DD (the tileset's assembly),
// C0/0A14 (the BG3 assembly) and C0/56D4 (the script) -- with three C0/00EB frames and C0/0330's obj0 slot-1 run
// between them. No tiles, no BG3 tileset, no sprites. Priced on the same clock as the load; the caller adds the
// counted frames. Measured (`--cputrace` from saves/attract_07d_battle_f4540.state at f594, loc 07D): C0/0283 at
// f58, the map f59-73 (6 decompressing + 9 building), C0/09DD f74-76, C0/0A14 f77, C0/56D4 f78, C0/0330 f79, the
// runner C0/5A46 back at f82 -- 24 frames.
long ct_lz_return_cost(const uint8_t* rom, size_t romLen, int loc) {
  if (!rom || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  long cyc = 0;
  const unsigned tileset = rom[REC + 1];
  if (tileset != 0xFF) {                                              /* C0/09DD: $F6:2100 + t*3 */
    size_t pp = 0x362100u + (size_t)tileset * 3;
    if (pp + 3 <= romLen)
      cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)rom[pp] | ((unsigned)rom[pp + 1] << 8) |
                                                             ((unsigned)rom[pp + 2] << 16)), 0);
  }
  unsigned mapId = (unsigned)rom[REC + 4] | ((unsigned)rom[REC + 5] << 8);   /* C0/A33B */
  size_t me = 0x361E00u + (size_t)mapId * 3;
  if (me + 3 <= romLen) {
    size_t src = ct_lz_hirom_offset((unsigned)rom[me] | ((unsigned)rom[me + 1] << 8) | ((unsigned)rom[me + 2] << 16));
    cyc += ct_lz_rom_cycles(rom, romLen, src, 0);
    uint8_t* img = (uint8_t*)malloc(0x20000);
    if (img) {
      size_t mlen = ct_lz_decompress_bounded(rom, romLen, img, 0x20000, src);
      { long w = ct_map_load_cycles(rom, romLen, loc);   /* C0/A33B walked; the model where it stops */
        if (w >= 0) cyc += w; else if (mlen) cyc += ct_map_build_cycles(img, mlen); }
      free(img);
    }
  }
  const unsigned b3 = rom[REC + 2];                                   /* C0/0A14: $F6:21C0 + b*3 */
  if (b3 != 0xFF) {
    size_t pa = 0x3621C0u + (size_t)b3 * 3;
    if (pa + 3 <= romLen)
      cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)rom[pa] | ((unsigned)rom[pa + 1] << 8) |
                                                             ((unsigned)rom[pa + 2] << 16)), 0);
  }
  const unsigned sid = (unsigned)rom[REC + 8] | ((unsigned)rom[REC + 9] << 8);   /* C0/56D4 */
  size_t ps = ct_lz_hirom_offset(0xFCF9F0u) + (size_t)sid * 3;
  if (ps + 3 <= romLen)
    cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)rom[ps] | ((unsigned)rom[ps + 1] << 8) |
                                                           ((unsigned)rom[ps + 2] << 16)), 1);
  return cyc;
}

// One byte of a location's MAP IMAGE as C0/A33B decompresses it to $7E:B500 -- the header bytes the field engine
// reads back before the build (C0/A385-A4FB): byte 4 is what C0/A415-A456 packs into $0BD7/$0BD8 (the effect
// layer's staging bits, copied to $2C66/$2C67 by C0/0691 for the battle). -1 when the map cannot be read.
int ct_lz_map_header_byte(const uint8_t* rom, size_t romLen, int loc, int idx) {
  if (!rom || loc < 0 || loc > 511 || idx < 0) return -1;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return -1;
  unsigned mapId = (unsigned)rom[REC + 4] | ((unsigned)rom[REC + 5] << 8);
  size_t me = 0x361E00u + (size_t)mapId * 3;
  if (me + 3 > romLen) return -1;
  size_t src = ct_lz_hirom_offset((unsigned)rom[me] | ((unsigned)rom[me + 1] << 8) | ((unsigned)rom[me + 2] << 16));
  uint8_t* img = (uint8_t*)malloc(0x20000);
  if (!img) return -1;
  size_t mlen = ct_lz_decompress_bounded(rom, romLen, img, 0x20000, src);
  int v = ((size_t)idx < mlen) ? img[idx] : -1;
  free(img);
  return v;
}

// THE FIELD'S CHARACTERS AS THE LOAD UPLOADS THEM: C0/0960 + C0/6D2F decompress the tileset's eight packets ($F6:1C00 +
// ts*8 -> $F6:2220 + idx*3), the first six to $7E:3000 and on to VRAM by C0/2DC8 -- $1000 bytes each ($0F80 for the
// sixth) at word $2000 / $2800 / $3000 / $3800 / $4000 / $4800 -- and C0/6DCF the BG3 tileset (record byte +2) to
// $7F:5080 and VRAM $5000 ($1000). Written into a 64 KB byte image of VRAM (word address * 2). Returns the packets.
int ct_lz_load_tiles(const uint8_t* rom, size_t romLen, int loc, uint8_t* vram64k) {
  if (!rom || !vram64k || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  int n = 0;
  uint8_t* buf = (uint8_t*)malloc(0x20000);
  if (!buf) return 0;
  const unsigned tileset = rom[REC + 1];
  if (tileset != 0xFF) {
    const size_t LIST = 0x361C00u + (size_t)tileset * 8;
    for (int i = 0; i < 6 && LIST + 8 <= romLen; i++) {
      unsigned idx = rom[LIST + i];
      if (idx == 0xFF) continue;
      size_t pe = 0x362220u + (size_t)idx * 3;
      if (pe + 3 > romLen) continue;
      size_t src = ct_lz_hirom_offset((unsigned)rom[pe] | ((unsigned)rom[pe + 1] << 8) | ((unsigned)rom[pe + 2] << 16));
      size_t len = ct_lz_decompress_bounded(rom, romLen, buf, 0x20000, src);
      size_t want = (i == 5) ? 0x0F80 : 0x1000;
      if (len > want) len = want;
      memcpy(vram64k + 0x4000 + (size_t)i * 0x1000, buf, len);
      n++;
    }
  }
  const unsigned b3 = rom[REC + 2];
  if (b3 != 0xFF) {
    size_t pe = 0x362220u + (size_t)b3 * 3;
    if (pe + 3 <= romLen) {
      size_t src = ct_lz_hirom_offset((unsigned)rom[pe] | ((unsigned)rom[pe + 1] << 8) | ((unsigned)rom[pe + 2] << 16));
      size_t len = ct_lz_decompress_bounded(rom, romLen, buf, 0x20000, src);
      if (len > 0x1000) len = 0x1000;
      memcpy(vram64k + 0xA000, buf, len);
      n++;
    }
  }
  free(buf);
  return n;
}

// C0/6D65 / C0/6D97, the end of C0/0960: the tileset's packets 6 and 7 decompressed to $7F:6000 and $7F:7000 with no DMA
// and no length cap (a long packet 6 runs on into $7000 and packet 7 lands over its tail) -- the animated characters
// FD/E022 DMAs from. `wram` = the flat 128 KB image. Returns the number of packets written.
int ct_lz_load_anim_tiles(const uint8_t* rom, size_t romLen, int loc, uint8_t* wram) {
  if (!rom || !wram || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  const unsigned tileset = rom[REC + 1];
  if (tileset == 0xFF) return 0;
  int n = 0;
  for (int i = 6; i < 8; i++) {
    const size_t LIST = 0x361C00u + (size_t)tileset * 8 + (size_t)i;
    if (LIST >= romLen || rom[LIST] == 0xFF) continue;                       // 6D67 / 6DA0 `CMP #FF / BEQ`
    size_t pe = 0x362220u + (size_t)rom[LIST] * 3;
    if (pe + 3 > romLen) continue;
    size_t src = ct_lz_hirom_offset((unsigned)rom[pe] | ((unsigned)rom[pe + 1] << 8) | ((unsigned)rom[pe + 2] << 16));
    size_t dst = (i == 6) ? 0x16000u : 0x17000u;                             // 6D7F / 6DB6, bank $7F
    ct_lz_decompress_bounded(rom, romLen, wram + dst, 0x20000u - dst, src);
    n++;
  }
  return n;
}

// THE TILESET'S ASSEMBLY as C0/09DD decompresses it to $7E:B000 ($F6:2100 + ts*3): 512 metatiles x 4 corner words
// (TL TR on the even tilemap row, BL BR on the odd -- C0/79CF / C0/7AC1), each `chip | pal << 10 | prio << 13 |
// fx << 14 | fy << 15`; the map's +256 ids select the second 2 KB. Into a 4 KB buffer; 0 when the record has none.
int ct_lz_load_assembly(const uint8_t* rom, size_t romLen, int loc, uint8_t* asm4k) {
  if (!rom || !asm4k || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  const unsigned tileset = rom[REC + 1];
  if (tileset == 0xFF) return 0;
  size_t pp = 0x362100u + (size_t)tileset * 3;
  if (pp + 3 > romLen) return 0;
  size_t src = ct_lz_hirom_offset((unsigned)rom[pp] | ((unsigned)rom[pp + 1] << 8) | ((unsigned)rom[pp + 2] << 16));
  uint8_t* buf = (uint8_t*)malloc(0x20000);
  if (!buf) return 0;
  size_t len = ct_lz_decompress_bounded(rom, romLen, buf, 0x20000, src);
  if (len > 0x1000) len = 0x1000;
  memset(asm4k, 0, 0x1000); memcpy(asm4k, buf, len);
  free(buf);
  return 1;
}

// THE BG3 ASSEMBLY as C0/0A14 decompresses it to $7E:C000 (`LDA $BB / BNE`: the record's byte +2, the BG3 tileset's,
// FF = none; $F6:21C0 + b*3; 0A45 LDA #7E): 256 metatiles x 4 corner words, the same layout as $7E:B000's, read by
// C0/7CB5's BG3 build. Into a 2 KB buffer; 0 when the record has none.
int ct_lz_load_assembly3(const uint8_t* rom, size_t romLen, int loc, uint8_t* asm2k) {
  if (!rom || !asm2k || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  const unsigned b3 = rom[REC + 2];
  if (b3 == 0xFF) return 0;
  size_t pp = 0x3621C0u + (size_t)b3 * 3;
  if (pp + 3 > romLen) return 0;
  size_t src = ct_lz_hirom_offset((unsigned)rom[pp] | ((unsigned)rom[pp + 1] << 8) | ((unsigned)rom[pp + 2] << 16));
  uint8_t* buf = (uint8_t*)malloc(0x20000);
  if (!buf) return 0;
  size_t len = ct_lz_decompress_bounded(rom, romLen, buf, 0x20000, src);
  if (len > 0x800) len = 0x800;
  memset(asm2k, 0, 0x800); memcpy(asm2k, buf, len);
  free(buf);
  return 1;
}

// THE MAP IN WRAM AS C0/A521-A66B LEAVES IT (dis.py 0A521): WMADD $7E:B506 (past the six header bytes) and four
// sequential streams, each written at ROW * 256 + COLUMN: layer A (BG1) at $7E:3000 (wA x hA from byte 0: w =
// ((b & 3) + 1) * 16, h = ((b & 0C) + 4) * 4), layer B (BG2) at $3040 (byte 0's high nibble: w = (b & 30) + 10,
// h = ((b & C0) >> 2) + 10), layer C (BG3) at $3080 only with byte 1 bit 7 (dims from byte 1 as A's), then the
// PROPERTIES over A's dims: three bytes a cell to $30C0 / $7000 / $7040, or with bit 7 of the first a RUN
// (`AND #7F`, the two bytes, then a count byte, 0 = 256; C0/A61A-A669). A translated stage builds its own tilemap
// from these cells (entry 3's mountains, $7E:3040 = 00 01 02 .. at the hand-off). Returns 1 when written.
int ct_lz_map_to_wram(const uint8_t* rom, size_t romLen, int loc, uint8_t* s7e) {
  if (!rom || !s7e || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  unsigned mapId = (unsigned)rom[REC + 4] | ((unsigned)rom[REC + 5] << 8);
  size_t me = 0x361E00u + (size_t)mapId * 3;
  if (me + 3 > romLen) return 0;
  size_t src = ct_lz_hirom_offset((unsigned)rom[me] | ((unsigned)rom[me + 1] << 8) | ((unsigned)rom[me + 2] << 16));
  uint8_t* img = (uint8_t*)malloc(0x20000);
  if (!img) return 0;
  size_t n = ct_lz_decompress_bounded(rom, romLen, img, 0x20000, src);
  if (n < 6) { free(img); return 0; }
  unsigned b0 = img[0], b1 = img[1];
  int wA = ((b0 & 3) + 1) * 16, hA = ((b0 & 0x0C) + 4) * 4;                 /* A3D9-A3F0 */
  int wB = (b0 & 0x30) + 0x10, hB = ((b0 & 0xC0) >> 2) + 0x10;              /* A3F5-A408 */
  int wC = ((b1 & 3) + 1) * 16, hC = ((b1 & 0x0C) + 4) * 4;                 /* A3C1-A3D6 */
  int hasC = (b1 & 0x80) != 0;                                              /* A38B: $0BCA */
  size_t p = 6;
  for (int r = 0; r < hA; r++) for (int c = 0; c < wA; c++) s7e[0x3000 + r * 256 + c] = (p < n) ? img[p++] : 0;
  for (int r = 0; r < hB; r++) for (int c = 0; c < wB; c++) s7e[0x3040 + r * 256 + c] = (p < n) ? img[p++] : 0;
  if (hasC) for (int r = 0; r < hC; r++) for (int c = 0; c < wC; c++) s7e[0x3080 + r * 256 + c] = (p < n) ? img[p++] : 0;
  { int r = 0, c = 0, left = 0; uint8_t q1 = 0, q2 = 0, q3 = 0;
    while (r < hA) {
      if (left == 0) {
        uint8_t v = (p < n) ? img[p++] : 0;
        if (v & 0x80) { q1 = (uint8_t)(v & 0x7F); q2 = (p < n) ? img[p++] : 0; q3 = (p < n) ? img[p++] : 0;
                        int cnt = (p < n) ? img[p++] : 0; left = cnt ? cnt : 256; }
        else          { q1 = v; q2 = (p < n) ? img[p++] : 0; q3 = (p < n) ? img[p++] : 0; left = 1; }
      }
      s7e[0x30C0 + r * 256 + c] = q1; s7e[0x7000 + r * 256 + c] = q2; s7e[0x7040 + r * 256 + c] = q3;
      left--;
      if (++c >= wA) { c = 0; r++; }
    } }
  free(img);
  return 1;
}
