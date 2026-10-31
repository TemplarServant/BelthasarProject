// lz.c -- Chrono Trigger LZ decompressor (see lz.h) and the ROM's cost of the loads that use it.
//
// The routine was reverse-engineered by Michael Springer ("Evil Peer"); this follows CTViewer's
// util/lz_decompress.rs. The format is a segmented LZ: a 2-byte size header, then flag bytes whose bits
// select literal (0) or back-reference (1). Back-references are 2-byte pairs; the window is 11-bit (small)
// or 12-bit (large), chosen once from the top bits of the byte just past the first segment. Segments can
// rebase via a 3-byte marker. Every read and write is range-checked against the caller's caps and a
// violation returns 0; ct_lz_decompress passes SIZE_MAX for both.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lz.h"
#include "assets.h"
#include "romview.h"
// A byte of the ROM's tables (location records, pointer lists, the world header): from the container's named
// table when one covers it (CT_ASSET_TABLE, romview.h), else from the ROM.
#define RB(o) CtRomByte(rom, (long)romLen, (long)(o))

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
static const uint8_t* g_lzRom2[3];
static int lzIsRom(const uint8_t* p) {
  if (!p) return 0;
  if (p == g_lzRom) return 1;
  for (int i = 0; i < 3; i++) if (g_lzRom2[i] == p) return 1;
  return 0;
}
void ct_lz_add_rom_image(const uint8_t* rom) {
  for (int i = 0; i < 3; i++) if (!g_lzRom2[i] || g_lzRom2[i] == rom) { g_lzRom2[i] = rom; return; }
}

// CT_LZ_MISSLOG=<file>: logs each stream taken from a ROM image instead of the container, once per offset and
// caller, as `OFFSET CALLER kind`. CALLER is relative to the executable; tools/rom_census.py --lzmiss resolves it
// through build/Belthasar.map. The list feeds tools/gen/lz_pack.py.
#ifdef _MSC_VER
#include <intrin.h>
__declspec(dllimport) void* __stdcall GetModuleHandleA(const char* name);   // (not <windows.h>: its `small` macro)
#define LZ_CALLER() ((uintptr_t)_ReturnAddress() - (uintptr_t)GetModuleHandleA(NULL))
#else
#define LZ_CALLER() ((uintptr_t)__builtin_return_address(0))
#endif
#ifdef _WIN32
__declspec(dllimport) unsigned long __stdcall GetCurrentProcessId(void);
static long lzPid(void) { return (long)GetCurrentProcessId(); }
#else
#include <unistd.h>
static long lzPid(void) { return (long)getpid(); }
#endif
static const char* lzExe(void) {               // the executable's base name: a map resolves only its own callers
  static char name[64];
  if (!name[0]) {
#ifdef _WIN32
    char path[512] = "";
    extern __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void* m, char* p, unsigned long n);
    GetModuleFileNameA(NULL, path, sizeof path);
#else
    char path[512] = "?";
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n > 0) path[n] = 0; else strcpy(path, "?");
#endif
    const char* b = path;
    for (const char* q = path; *q; q++) if (*q == '\\' || *q == '/') b = q + 1;
    snprintf(name, sizeof name, "%s", b);
  }
  return name;
}
static void lzMiss(size_t start, uintptr_t caller, const char* kind) {
  static FILE* f; static int tried; static uint64_t seen[4096]; static int nseen;
  if (!tried) { tried = 1; const char* p = getenv("CT_LZ_MISSLOG"); char path[1024];
    if (p && strstr(p, "%d")) { snprintf(path, sizeof path, p, (int)lzPid()); p = path; }
    if (p && *p) f = fopen(p, "w"); }
  if (!f) return;
  uint64_t key = ((uint64_t)start << 32) ^ (uint64_t)caller;
  for (int i = 0; i < nseen; i++) if (seen[i] == key) return;
  if (nseen < 4096) seen[nseen++] = key;
  fprintf(f, "%06lX %llX %s %s\n", (unsigned long)start, (unsigned long long)caller, kind, lzExe());
  fflush(f);
}
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
  if (g_lzAssets && lzIsRom(data)) {              // a ROM stream: use the container's copy when it has one
    size_t n = 0; const uint8_t* e = ct_lz_asset(start, &n, NULL, NULL);
    if (e) { if (n > outCap) return 0; memcpy(out, e, n); return n; }
  }
  if (data && dataLen > 0x100000) lzMiss(start, LZ_CALLER(), lzIsRom(data) ? "decompress" : "decompress-other-image");
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

// ---- The ROM's cost of a decompression: C3/0557 priced instruction by instruction ----
// The port decompresses instantly; the ROM spends most of a location load here. The loop (large-window arm
// C3/05E5-0658; the 11-bit arm at C3/076B has one LSR A fewer and an AND #07FF mask) and the per-access
// timings are in disasm/notes/field_load_cost.md sec. 2: FastROM byte 6 master cycles, idle 6, direct page or
// WRAM 8, the $2180 write 6, MVN 46 per byte moved.
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

// HiROM: banks $C0-$FF map straight onto the file, offset = (bank - $C0) << 16 | addr (as in tools/dis.py).
size_t ct_lz_hirom_offset(unsigned addr24) {
  return (size_t)(((addr24 >> 16) - 0xC0u) & 0x3Fu) << 16 | (addr24 & 0xFFFFu);
}

long ct_lz_rom_cycles(const uint8_t* data, size_t dataLen, size_t start, int dstBankOdd) {
  if (g_lzAssets && lzIsRom(data)) {
    long c7e = 0, c7f = 0;
    if (ct_lz_asset(start, NULL, &c7e, &c7f)) return dstBankOdd ? c7f : c7e;
  }
  if (data && dataLen > 0x100000) lzMiss(start, LZ_CALLER(), lzIsRom(data) ? "cost" : "cost-other-image");
  if (!data || start + 2 >= dataLen) return 0;
  size_t base = start;
  size_t x = base + 2;
  size_t end = base + 2 + (((size_t)data[base + 1] << 8) | data[base]);
  if (end >= dataLen) return 0;
  int small = (data[end] & 0xC0) != 0;
  // The dispatch at C3/0580-059A: the four arms differ only in this fixed prologue (dossier sec. 2).
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

// ---- C0/A33B's map build: the half after its `JSL C3/0002`, priced the same way ----
// The decompressed map is read back through $2180 into four planes (A548 -> $7E:3000, A57D -> $3040,
// A5B9 -> $3080 only with map byte 1 bit 7, A5F0 -> the RLE-coded $30C0/$7000/$7040); the dimensions come
// from map bytes 0-1 (C0/A3C1-A408). Layout and RLE format: disasm/notes/field_load_cost.md sec. 5.
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
/* C0/A67B-A693 moves a fixed 4032-byte block with MVN. */
#define MBC_MVN_BLOCK (4032L * LZC_MVN_BYTE)
/* The one-shot half (header parse, the fixed 64/63-pass loops at C0/A670/A67F, the tail past the MVN):
   measured, the median over five locations. Used only where ct_map_load_cycles' walk stops. */
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

// C0/A33B walked: everything the map load costs besides its decompression (tools/map_cost_proto.py is the
// Python twin). ct_map_build_cycles stands in only where this walk stops (an RLE plane that runs past the map).
// The legs: the header parse C0/A33B-A508 (map byte 1 bits 7-4, $01BB, bytes 2-3); C0/7399's six plane masks
// and the record's bounds (bytes 10-13, the whole plane when byte 10 is negative: C0/7495); C0/A521's plane
// copies and RLE plane; C0/A66B's $7E:7080 fill (63 MVNs of 64 bytes) and the $E5:0000 list of 7-byte entries
// (count bit 7 = a column, C0/A6DC; a row's `AND $1E` at C0/A6C7 is 16-bit on its first pass only); the vector's
// JMP (18). disasm/notes/field_load_cost.md sec. 9.
long ct_map_load_cycles(const uint8_t* rom, size_t romLen, int loc) {
  if (!rom || loc < 0 || loc > 511) return -1;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return -1;
  unsigned mapId = (unsigned)RB(REC + 4) | ((unsigned)RB(REC + 5) << 8);
  size_t me = 0x361E00u + (size_t)mapId * 3;
  if (me + 3 > romLen) return -1;
  size_t src = ct_lz_hirom_offset((unsigned)RB(me) | ((unsigned)RB(me + 1) << 8) | ((unsigned)RB(me + 2) << 16));
  uint8_t* m = (uint8_t*)malloc(0x20000);
  if (!m) return -1;
  size_t len = ct_lz_decompress_bounded(rom, romLen, m, 0x20000, src);
  long k = -1;
  if (len < 8) goto out;
  {
    const int b1 = m[1], bb = RB(REC + 2) == 0xFF;
    k = 780 + ((b1 & 0x80) ? 68 : 44) + 32 + 56 + 32 + 50 + 32 + ((b1 & 0x10) ? 64 : 18) + 952   /* the header */
      + (bb ? 76 : 18) + 240 + (bb ? 76 : 18) + 334;
    /* header bytes 2 and 3 (C0/A463 / C0/A4B3), the layers' scroll speeds: a non-zero byte costs 182; its low field
       (bits 0-2) 118 + 54 a step past the first, its sign (bit 3) 58; its high field (bits 4-6) 166 + 54 a step, its
       sign (bit 7) 58 (tools/load_cost_consts.py) */
    for (int i = 2; i < 4; i++) {
      int b = m[i], lo = b & 7, hi = (b >> 4) & 7;
      if (!b) continue;
      k += 182;
      if (lo) k += 118 + 54L * (lo - 1) + ((b & 0x08) ? 58 : 0);
      if (hi) k += 166 + 54L * (hi - 1) + ((b & 0x80) ? 58 : 0);
    }
    const int dims[6] = { ((m[0] & 3) + 1) << 4, ((m[0] & 0x0C) + 4) << 2, (m[0] & 0x30) + 0x10,
                          ((m[0] & 0xC0) >> 2) + 0x10, ((m[1] & 3) + 1) << 4, ((m[1] & 0x0C) + 4) << 2 };
    for (int i = 0; i < 6; i++)                            /* C0/7399 */
      k += 38 + (dims[i] == 0x40 ? 50 : dims[i] == 0x20 ? 92 : dims[i] == 0x30 ? 116 : 110);
    k += 64 + ((RB(REC + 10) & 0x80) ? 214 : 222);
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
    unsigned x = RB(e5 + 2 * loc) | (RB(e5 + 2 * loc + 1) << 8), end = RB(e5 + 2 * loc + 2) | (RB(e5 + 2 * loc + 3) << 8);
    for (int guard = 0; ; guard++) {
      k += 28;
      if (x >= end || guard > 4096) { k += 76; break; }
      k += 78;
      int n = RB(e5 + x + 2);
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

// ---- The overworld load: bank C2's packet list ----
// C2's loader decompresses with the same C3/0557: `C2/275B LDA $C6FD00,X` (world*2) gives the world's header
// pointer in bank $C6; each `JSL C3/0002` site takes an index byte from that header (bit 7 = skip), *3 into a
// 3-byte pointer table, then $0300/$0302 = source, $0305 = destination bank. The site list was read off the
// oracle with `--hookpc` on each of the 23 sites (disasm/notes/field_load_cost.md sec. 10).
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
  size_t hdr = ct_lz_hirom_offset(0xC60000u | ((unsigned)RB(hp) | ((unsigned)RB(hp + 1) << 8)));
  int n = 0;
  for (int i = 0; i < CT_OW_SITE_N && n < max; i++) {
    const CtOwSite* s = &CT_OW_SITES[i];
    int idx;
    if (s->hdrIdx == -1) idx = 0;                        /* the site's own fixed entry */
    else if (s->hdrIdx == -2) idx = 1;
    else {
      if (hdr + (size_t)s->hdrIdx >= romLen) continue;
      idx = RB(hdr + s->hdrIdx);
      if (idx & 0x80) continue;                          /* the loader's own BMI: bit 7 = no packet */
    }
    size_t e = ct_lz_hirom_offset(s->table) + (size_t)idx * 3;
    if (e + 3 > romLen) continue;
    srcOut[n] = (unsigned)RB(e) | ((unsigned)RB(e + 1) << 8) | ((unsigned)RB(e + 2) << 16);
    dstOut[n] = s->dstBank;
    n++;
  }
  return n;
}

// C2/2C93, the world's rebuild after the X menu (C2/2556 -> C2/2E72 -> C2/03EF -> C2/2C93): the entry's sites
// minus 29B5 / 2A11 / 2A6D / 2A3F, whose packets survive the menu (read off a --cputrace of the return).
long ct_lz_world_return_cost(const uint8_t* rom, size_t romLen, int world, int* outPackets) {
  if (outPackets) *outPackets = 0;
  if (!rom || world < 0 || world > 7) return 0;
  size_t hp = ct_lz_hirom_offset(0xC6FD00) + (size_t)world * 2;
  if (hp + 2 > romLen) return 0;
  size_t hdr = ct_lz_hirom_offset(0xC60000u | ((unsigned)RB(hp) | ((unsigned)RB(hp + 1) << 8)));
  long cyc = 0; int n = 0;
  for (int i = 0; i < CT_OW_SITE_N; i++) {
    const CtOwSite* s = &CT_OW_SITES[i];
    if (s->site == 0x29B5 || s->site == 0x2A11 || s->site == 0x2A6D || s->site == 0x2A3F) continue;
    int idx;
    if (s->hdrIdx == -1) idx = 0;
    else if (s->hdrIdx == -2) idx = 1;
    else {
      if (hdr + (size_t)s->hdrIdx >= romLen) continue;
      idx = RB(hdr + s->hdrIdx);
      if (idx & 0x80) continue;
    }
    size_t e = ct_lz_hirom_offset(s->table) + (size_t)idx * 3;
    if (e + 3 > romLen) continue;
    uint32_t src = (unsigned)RB(e) | ((unsigned)RB(e + 1) << 8) | ((unsigned)RB(e + 2) << 16);
    cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset(src), s->dstBank == 0x7F);
    n++;
  }
  if (outPackets) *outPackets = n;
  return cyc;
}
// What bank C2's loader spends decompressing for one world.
long ct_lz_world_cost(const uint8_t* rom, size_t romLen, int world, int* outPackets) {
  uint32_t src[32]; uint8_t dst[32];
  int n = ct_lz_world_packets(rom, romLen, world, src, dst, 32);
  long cyc = 0;
  for (int i = 0; i < n; i++)
    cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset(src[i]), dst[i] == 0x7F);
  if (outPackets) *outPackets = n;
  return cyc;
}

// C2/09C5, the world's first screen (C2/2C81 / 2C88, one call per BG): the part the camera cells $E3/$E7
// decide. `LDA $E3,X / LSR / BCS / DEC / BPL / LDA #005F` picks the start column $08 (odd 18; even 42; even
// and zero 54) and `LDA $20,X / BIT #0008` its pixel offset (odd 60, even 48); the same on $E7/$26 for the
// row $0A. In the 18 columns (C2/0A46-0A64): the row offset wraps at 0x1800 when $0A >= 48 (C2/0A99, +6 a
// column), the column at 0x60 when $08 >= 78 (C2/0A52, +6 once), and VRAM column 32 or past takes the second
// screen (C2/0AE4, +24 a column). The rest (36 DMAs of 64 bytes) is fixed.
static long owFirstScreenVar(unsigned e3, unsigned e7) {
  long c = 0;
  unsigned h = e3 >> 1, v = e7 >> 1, col, row;
  if (e3 & 1) { c += 18 + 60; col = h; } else { c += (h ? 42 : 54) + 48; col = h ? h - 1 : 0x5F; }
  if (e7 & 1) { c += 18 + 60; row = v; } else { c += (v ? 42 : 54) + 48; row = v ? v - 1 : 0x3F; }
  if (row >= 48) c += 18 * 6;
  if (col >= 78) c += 6;
  unsigned px = ((e3 << 3) - ((e3 & 1) ? 8 : 16)) & 0x1FF;
  for (int k = 0; k < 18; k++, px = (px + 0x10) & 0x1FF)
    if ((px >> 3) >= 32) c += 24;
  return c;
}

// The world load, C0/0B5A (NMI off) -> C2/23CC (`LDA #81 / STA $4200`, NMI on), walked. Per packet: the
// decompression + the vector's JMP (18) + its site's C2 path (276 over a skip). Then C2/2A44's four counted
// lists after site 2A6D's packet (entries of 7/3/3/2 bytes at 506/234/234/166 cycles), C2/09C5 twice, and
// WORLD_LOAD_FIXED for the rest (measured on world 5's trace). disasm/notes/field_load_cost.md sec. 10.
// -1 (and *why) when a list count is 0 (a 65536-pass loop no trace ran) or the packet does not decompress.
#define WORLD_LOAD_FIXED 2041796L
long ct_lz_world_load_cycles(const uint8_t* rom, size_t romLen, int world, const uint16_t e3[2], const uint16_t e7[2],
                             int* outPackets, const char** why) {
  uint32_t src[32]; uint8_t dst[32];
  int n = ct_lz_world_packets(rom, romLen, world, src, dst, 32);
  if (outPackets) *outPackets = n;
  if (n <= 0) { if (why) *why = "no packets"; return -1; }
  long cyc = WORLD_LOAD_FIXED;
  for (int i = 0; i < n; i++)
    cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset(src[i]), dst[i] == 0x7F) + 18 + 276;
  // C2/2A44's lists: the packet the 2A6D site decompressed (header byte $15 into $C6:FFC0)
  size_t hp = ct_lz_hirom_offset(0xC6FD00) + (size_t)world * 2;
  size_t hdr = ct_lz_hirom_offset(0xC60000u | ((unsigned)RB(hp) | ((unsigned)RB(hp + 1) << 8)));
  unsigned idx = RB(hdr + 0x15);
  if (idx & 0x80) { if (why) *why = "C2/2A44: no packet (the lists read $7F:0400 as it stands)"; return -1; }
  size_t e = ct_lz_hirom_offset(0xC6FFC0) + (size_t)idx * 3;
  uint8_t* img = (uint8_t*)malloc(0x10000);
  if (!img) return -1;
  size_t len = ct_lz_decompress_bounded(rom, romLen, img, 0x10000,
                   ct_lz_hirom_offset((unsigned)RB(e) | ((unsigned)RB(e + 1) << 8) | ((unsigned)RB(e + 2) << 16)));
  static const int kSize[4] = { 7, 3, 3, 2 }, kCyc[4] = { 506, 234, 234, 166 };
  size_t p = 0;
  for (int l = 0; l < 4; l++) {
    unsigned cnt = p < len ? img[p] : 0;
    if (!cnt) { free(img); if (why) *why = "C2/2A44: a list count of 0"; return -1; }
    cyc += (long)cnt * kCyc[l];
    p += 1 + (size_t)cnt * kSize[l];
  }
  free(img);
  for (int b = 0; b < 2; b++) cyc += owFirstScreenVar(e3[b], e7[b]);
  return cyc;
}

// C0/286C, the pipeline pass C0/2848 runs once before its ramp (C0/0A50's tilemap build, then the draw chain),
// ending in the vblank wait C0/EC60. Measured in situ (1046 lines x 1364); its loops are screen-sized, so it is
// a constant of the routine. Not derived: C0/0A50 picks between tilemap builders (C0/7B0x / C0/7E2x) on a
// selector not yet read out; on a C0/7E2x map this is about a frame light. disasm/notes/field_load_cost.md sec. 6.
#define LOADC_FIRST_PASS 1426744L

long g_ctLzSpriteCycles = 0;   // the object pass's own decompressions, filled by the loaders (lz.h)

// The location loader's tables (bank $F6, read as C0/0960 / C0/A33B / C0/09DD read them) are listed in
// disasm/notes/field_load_cost.md sec. 3. Entries 0-5 are DMAed to VRAM by C0/2DC8 at 8 cycles a byte + 24.
long ct_lz_load_cost(const uint8_t* rom, size_t romLen, int loc, long* outDma, int* outPackets) {
  if (outDma) *outDma = 0;
  if (outPackets) *outPackets = 0;
  if (!rom || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;      /* $F6:0000, HiROM file offset */
  if (REC + 14 > romLen) return 0;
  long cyc = 0, dma = 0;
  int n = 0;
  const unsigned tileset = RB(REC + 1);
  if (tileset != 0xFF) {
    const size_t LIST = 0x361C00u + (size_t)tileset * 8;
    for (int i = 0; i < 8 && LIST + 8 <= romLen; i++) {
      unsigned idx = RB(LIST + i);
      if (idx == 0xFF) continue;
      size_t pe = 0x362220u + (size_t)idx * 3;
      if (pe + 3 > romLen) continue;
      size_t src = ct_lz_hirom_offset((unsigned)RB(pe) | ((unsigned)RB(pe + 1) << 8) |
                                      ((unsigned)RB(pe + 2) << 16));
      cyc += ct_lz_rom_cycles(rom, romLen, src, i >= 6);   /* 6D65/6D97 land in bank $7F */
      n++;
      if (i < 6) dma += 8L * (i == 5 ? 0x0F80 : 0x1000) + 24;   /* C0/2DC8, the VRAM upload */
    }
    size_t pp = 0x362100u + (size_t)tileset * 3;
    if (pp + 3 <= romLen) {
      size_t src = ct_lz_hirom_offset((unsigned)RB(pp) | ((unsigned)RB(pp + 1) << 8) |
                                      ((unsigned)RB(pp + 2) << 16));
      cyc += ct_lz_rom_cycles(rom, romLen, src, 0);
      n++;
    }
  }
  unsigned mapId = (unsigned)RB(REC + 4) | ((unsigned)RB(REC + 5) << 8);
  size_t me = 0x361E00u + (size_t)mapId * 3;
  if (me + 3 <= romLen) {
    size_t src = ct_lz_hirom_offset((unsigned)RB(me) | ((unsigned)RB(me + 1) << 8) |
                                    ((unsigned)RB(me + 2) << 16));
    cyc += ct_lz_rom_cycles(rom, romLen, src, 0);
    n++;
    // C0/A33B then builds what it decompressed; the fallback model needs the decompressed image.
    uint8_t* img = (uint8_t*)malloc(0x20000);
    if (img) {
      size_t mlen = ct_lz_decompress_bounded(rom, romLen, img, 0x20000, src);
      { long w = ct_map_load_cycles(rom, romLen, loc);   /* C0/A33B walked; the model where it stops */
        if (w >= 0) cyc += w; else if (mlen) cyc += ct_map_build_cycles(img, mlen); }
      free(img);
    }
  }
  // The rest of C0/00F4's packets (disasm/notes/field_load_cost.md sec. 6b):
  //   C0/6DCF  record byte +2 (BG3 tileset) != FF: `$F6:2220 + b*3 -> $7F:5080`, then C0/2DC8 DMAs $1000 bytes
  //            to VRAM $5000. FF: 6E22 `LDA #01 / STA $BB` only.
  //   C0/0A14  the same byte: `$F6:21C0 + b*3 -> $7E:C000`, no upload.
  //   C0/56D4  the location's script: record word +8, `$FC:F9F0 + id*3 -> $7F:2000`.
  {
    const unsigned b3 = RB(REC + 2);
    if (b3 != 0xFF) {
      size_t pe = 0x362220u + (size_t)b3 * 3, pa = 0x3621C0u + (size_t)b3 * 3;
      if (pe + 3 <= romLen) {
        cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)RB(pe) | ((unsigned)RB(pe + 1) << 8) |
                                                               ((unsigned)RB(pe + 2) << 16)), 1);
        n++;
        dma += 8L * 0x1000 + 24;                                        /* 6E1E JSR C0/2DC8, $1000 bytes */
      }
      if (pa + 3 <= romLen) {
        cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)RB(pa) | ((unsigned)RB(pa + 1) << 8) |
                                                               ((unsigned)RB(pa + 2) << 16)), 0);
        n++;
      }
    }
    const unsigned sid = (unsigned)RB(REC + 8) | ((unsigned)RB(REC + 9) << 8);
    size_t ps = ct_lz_hirom_offset(0xFCF9F0u) + (size_t)sid * 3;
    if (ps + 3 <= romLen) {
      cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)RB(ps) | ((unsigned)RB(ps + 1) << 8) |
                                                             ((unsigned)RB(ps + 2) << 16)), 1);
      n++;
    }
  }
  cyc += LOADC_FIRST_PASS;              // C0/2848's own first frame, before the ramp
  if (outDma) *outDma = dma;
  if (outPackets) *outPackets = n;
  return cyc;
}

// The field's return from a battle (disasm/notes/sequence_table.md 11.10): C0/18C1 `JSR C0/0283` -> C0/0304
// re-runs part of the load -- C0/A33B (the map, decompressed and built), C0/09DD (tileset assembly), C0/0A14
// (BG3 assembly), C0/56D4 (the script) -- with three C0/00EB frames and C0/0330's obj0 slot-1 run between them.
// No tiles, BG3 tileset or sprites. The caller adds the counted frames.
long ct_lz_return_cost(const uint8_t* rom, size_t romLen, int loc) {
  if (!rom || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  long cyc = 0;
  const unsigned tileset = RB(REC + 1);
  if (tileset != 0xFF) {                                              /* C0/09DD: $F6:2100 + t*3 */
    size_t pp = 0x362100u + (size_t)tileset * 3;
    if (pp + 3 <= romLen)
      cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)RB(pp) | ((unsigned)RB(pp + 1) << 8) |
                                                             ((unsigned)RB(pp + 2) << 16)), 0);
  }
  unsigned mapId = (unsigned)RB(REC + 4) | ((unsigned)RB(REC + 5) << 8);   /* C0/A33B */
  size_t me = 0x361E00u + (size_t)mapId * 3;
  if (me + 3 <= romLen) {
    size_t src = ct_lz_hirom_offset((unsigned)RB(me) | ((unsigned)RB(me + 1) << 8) | ((unsigned)RB(me + 2) << 16));
    cyc += ct_lz_rom_cycles(rom, romLen, src, 0);
    uint8_t* img = (uint8_t*)malloc(0x20000);
    if (img) {
      size_t mlen = ct_lz_decompress_bounded(rom, romLen, img, 0x20000, src);
      { long w = ct_map_load_cycles(rom, romLen, loc);   /* C0/A33B walked; the model where it stops */
        if (w >= 0) cyc += w; else if (mlen) cyc += ct_map_build_cycles(img, mlen); }
      free(img);
    }
  }
  const unsigned b3 = RB(REC + 2);                                   /* C0/0A14: $F6:21C0 + b*3 */
  if (b3 != 0xFF) {
    size_t pa = 0x3621C0u + (size_t)b3 * 3;
    if (pa + 3 <= romLen)
      cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)RB(pa) | ((unsigned)RB(pa + 1) << 8) |
                                                             ((unsigned)RB(pa + 2) << 16)), 0);
  }
  const unsigned sid = (unsigned)RB(REC + 8) | ((unsigned)RB(REC + 9) << 8);   /* C0/56D4 */
  size_t ps = ct_lz_hirom_offset(0xFCF9F0u) + (size_t)sid * 3;
  if (ps + 3 <= romLen)
    cyc += ct_lz_rom_cycles(rom, romLen, ct_lz_hirom_offset((unsigned)RB(ps) | ((unsigned)RB(ps + 1) << 8) |
                                                           ((unsigned)RB(ps + 2) << 16)), 1);
  return cyc;
}

// One byte of a location's map image as C0/A33B decompresses it to $7E:B500 (the header bytes read back by
// C0/A385-A4FB). Byte 4 is what C0/A415-A456 packs into $0BD7/$0BD8, the effect layer's staging bits (C0/0691
// copies them to $2C66/$2C67 for the battle). -1 when the map cannot be read.
int ct_lz_map_header_byte(const uint8_t* rom, size_t romLen, int loc, int idx) {
  if (!rom || loc < 0 || loc > 511 || idx < 0) return -1;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return -1;
  unsigned mapId = (unsigned)RB(REC + 4) | ((unsigned)RB(REC + 5) << 8);
  size_t me = 0x361E00u + (size_t)mapId * 3;
  if (me + 3 > romLen) return -1;
  size_t src = ct_lz_hirom_offset((unsigned)RB(me) | ((unsigned)RB(me + 1) << 8) | ((unsigned)RB(me + 2) << 16));
  uint8_t* img = (uint8_t*)malloc(0x20000);
  if (!img) return -1;
  size_t mlen = ct_lz_decompress_bounded(rom, romLen, img, 0x20000, src);
  int v = ((size_t)idx < mlen) ? img[idx] : -1;
  free(img);
  return v;
}

// The field's tiles as the load uploads them: C0/0960 + C0/6D2F decompress the tileset's packets 0-5 to VRAM
// words $2000-$4800 ($1000 bytes each, $0F80 for the sixth) and C0/6DCF the BG3 tileset (record byte +2) to
// VRAM $5000. Written into a 64 KB byte image of VRAM (word address * 2). Returns the packet count.
int ct_lz_load_tiles(const uint8_t* rom, size_t romLen, int loc, uint8_t* vram64k) {
  if (!rom || !vram64k || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  int n = 0;
  uint8_t* buf = (uint8_t*)malloc(0x20000);
  if (!buf) return 0;
  const unsigned tileset = RB(REC + 1);
  if (tileset != 0xFF) {
    const size_t LIST = 0x361C00u + (size_t)tileset * 8;
    for (int i = 0; i < 6 && LIST + 8 <= romLen; i++) {
      unsigned idx = RB(LIST + i);
      if (idx == 0xFF) continue;
      size_t pe = 0x362220u + (size_t)idx * 3;
      if (pe + 3 > romLen) continue;
      size_t src = ct_lz_hirom_offset((unsigned)RB(pe) | ((unsigned)RB(pe + 1) << 8) | ((unsigned)RB(pe + 2) << 16));
      size_t len = ct_lz_decompress_bounded(rom, romLen, buf, 0x20000, src);
      size_t want = (i == 5) ? 0x0F80 : 0x1000;
      if (len > want) len = want;
      memcpy(vram64k + 0x4000 + (size_t)i * 0x1000, buf, len);
      n++;
    }
  }
  const unsigned b3 = RB(REC + 2);
  if (b3 != 0xFF) {
    size_t pe = 0x362220u + (size_t)b3 * 3;
    if (pe + 3 <= romLen) {
      size_t src = ct_lz_hirom_offset((unsigned)RB(pe) | ((unsigned)RB(pe + 1) << 8) | ((unsigned)RB(pe + 2) << 16));
      size_t len = ct_lz_decompress_bounded(rom, romLen, buf, 0x20000, src);
      if (len > 0x1000) len = 0x1000;
      memcpy(vram64k + 0xA000, buf, len);
      n++;
    }
  }
  free(buf);
  return n;
}

// C0/6D65 / C0/6D97, the end of C0/0960: packets 6 and 7 to $7F:6000 and $7F:7000 with no DMA and no length cap
// (a long packet 6 runs into $7000 and packet 7 overwrites its tail). These are the animated tiles FD/E022 DMAs
// from. `wram` = the flat 128 KB image. Returns the number of packets written.
int ct_lz_load_anim_tiles(const uint8_t* rom, size_t romLen, int loc, uint8_t* wram) {
  if (!rom || !wram || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  const unsigned tileset = RB(REC + 1);
  if (tileset == 0xFF) return 0;
  int n = 0;
  for (int i = 6; i < 8; i++) {
    const size_t LIST = 0x361C00u + (size_t)tileset * 8 + (size_t)i;
    if (LIST >= romLen || RB(LIST) == 0xFF) continue;                       // 6D67 / 6DA0 `CMP #FF / BEQ`
    size_t pe = 0x362220u + (size_t)RB(LIST) * 3;
    if (pe + 3 > romLen) continue;
    size_t src = ct_lz_hirom_offset((unsigned)RB(pe) | ((unsigned)RB(pe + 1) << 8) | ((unsigned)RB(pe + 2) << 16));
    size_t dst = (i == 6) ? 0x16000u : 0x17000u;                             // 6D7F / 6DB6, bank $7F
    ct_lz_decompress_bounded(rom, romLen, wram + dst, 0x20000u - dst, src);
    n++;
  }
  return n;
}

// The tileset's assembly as C0/09DD decompresses it to $7E:B000 ($F6:2100 + ts*3): 512 metatiles x 4 corner
// words (TL TR on the even tilemap row, BL BR on the odd: C0/79CF / C0/7AC1), each `chip | pal << 10 |
// prio << 13 | fx << 14 | fy << 15`; ids +256 select the second 2 KB. Into a 4 KB buffer; 0 when there is none.
int ct_lz_load_assembly(const uint8_t* rom, size_t romLen, int loc, uint8_t* asm4k) {
  if (!rom || !asm4k || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  const unsigned tileset = RB(REC + 1);
  if (tileset == 0xFF) return 0;
  size_t pp = 0x362100u + (size_t)tileset * 3;
  if (pp + 3 > romLen) return 0;
  size_t src = ct_lz_hirom_offset((unsigned)RB(pp) | ((unsigned)RB(pp + 1) << 8) | ((unsigned)RB(pp + 2) << 16));
  uint8_t* buf = (uint8_t*)malloc(0x20000);
  if (!buf) return 0;
  size_t len = ct_lz_decompress_bounded(rom, romLen, buf, 0x20000, src);
  if (len > 0x1000) len = 0x1000;
  memset(asm4k, 0, 0x1000); memcpy(asm4k, buf, len);
  free(buf);
  return 1;
}

// The BG3 assembly as C0/0A14 decompresses it to $7E:C000 (record byte +2, FF = none; $F6:21C0 + b*3):
// 256 metatiles x 4 corner words, laid out as $7E:B000, read by C0/7CB5's BG3 build. Into a 2 KB buffer;
// 0 when there is none.
int ct_lz_load_assembly3(const uint8_t* rom, size_t romLen, int loc, uint8_t* asm2k) {
  if (!rom || !asm2k || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  const unsigned b3 = RB(REC + 2);
  if (b3 == 0xFF) return 0;
  size_t pp = 0x3621C0u + (size_t)b3 * 3;
  if (pp + 3 > romLen) return 0;
  size_t src = ct_lz_hirom_offset((unsigned)RB(pp) | ((unsigned)RB(pp + 1) << 8) | ((unsigned)RB(pp + 2) << 16));
  uint8_t* buf = (uint8_t*)malloc(0x20000);
  if (!buf) return 0;
  size_t len = ct_lz_decompress_bounded(rom, romLen, buf, 0x20000, src);
  if (len > 0x800) len = 0x800;
  memset(asm2k, 0, 0x800); memcpy(asm2k, buf, len);
  free(buf);
  return 1;
}

// The map in WRAM as C0/A521-A66B leaves it: from $7E:B506 (past the six header bytes), four streams written at
// row * 256 + column -- layer A (BG1) at $7E:3000, layer B (BG2) at $3040, layer C (BG3) at $3080 only with
// byte 1 bit 7, then the properties over A's dims, three bytes a cell to $30C0 / $7000 / $7040, or with bit 7
// of the first a run (`AND #7F`, two bytes, a count, 0 = 256; C0/A61A-A669). Stages build their own tilemap
// from these cells. Returns 1 when written.
int ct_lz_map_to_wram(const uint8_t* rom, size_t romLen, int loc, uint8_t* s7e) {
  if (!rom || !s7e || loc < 0 || loc > 511) return 0;
  const size_t REC = 0x360000u + (size_t)loc * 14;
  if (REC + 14 > romLen) return 0;
  unsigned mapId = (unsigned)RB(REC + 4) | ((unsigned)RB(REC + 5) << 8);
  size_t me = 0x361E00u + (size_t)mapId * 3;
  if (me + 3 > romLen) return 0;
  size_t src = ct_lz_hirom_offset((unsigned)RB(me) | ((unsigned)RB(me + 1) << 8) | ((unsigned)RB(me + 2) << 16));
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
