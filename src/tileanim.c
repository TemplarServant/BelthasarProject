// tileanim.c -- FD/DE98 + FD/E022, the field's animated BG tiles. See tileanim.h and disasm/notes/field_bg_anim.md.
#include "tileanim.h"
#include "romview.h"

static int rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint8_t romb(const uint8_t* rom, long romLen, long o) { return CtRomByte(rom, romLen, o); }   // (romview.h)

// FD/DE98. D = $0500, DB = 0, X/Y 16-bit. The WRAM port ($2181 = $0400, $2183 = $7F) takes the tables in record order;
// a terminator ($80) record writes none, so the port does not advance for it.
void CtTileAnimLoad(uint8_t* w, const uint8_t* rom, long romLen, int loc) {
  if (!w || !rom || loc < 0 || loc > 511) return;
  #define T(i) romb(rom, romLen, 0x3DF310L + (long)(i))                            // $FD:F310,X (long,X: no wrap)
  int idx = romb(rom, romLen, 0x360001L + (long)loc * 14) & 0x3F;                  // DEAB-DEB5 `LDX $01FE / LDA $F60001,X / AND #3F / ASL`
  long X = romb(rom, romLen, 0x3DF290L + idx * 2) | (romb(rom, romLen, 0x3DF291L + idx * 2) << 8);   // DEB8 LDA $FDF290,X
  long port = 0x10400;                                                             // DEBD-DECC
  #define PUT(v) do { if (port < 0x20000) w[port] = (uint8_t)(v); port++; } while (0)  // STA $2180 (and $217F's high byte)
  for (int k = 0; k < 12; k++) {                                                   // DEC3 $18 = 0C / E017 DEC $18
    uint8_t* r = w + 0x05B0 + k * 5;                                               // Y = k * 5 (E00B ADC #0005)
    uint8_t fc = T(X); X++;                                                        // DED2-DED6
    if (fc == 0x80) { r[3] = 0x80; continue; }                                     // DED7-DEE0: the terminator
    r[0] = fc; r[2] = T(X); X++; r[3] = T(X); X++; r[4] = 0x7F;                    // DEE3-DEF8
    r[0] = 0; r[1] = 0;                                                            // DF02-DF07 / DF69-DF6E / DFCE-DFD3
    if (fc == 2) {
      for (int p = 0; p < 4; p++)                                                  // DF0E-DF36: d0*4, d1*4, four times
        for (int j = 0; j < 2; j++) { int v = (T(X + j) << 2) & 0xFFFF; PUT(v); PUT(v >> 8); }
      X += 2;
      for (int p = 0; p < 4; p++) for (int j = 0; j < 4; j++) PUT(T(X + j));      // DF3E-DF5C: s0, s1, four times
      X += 4;
    } else if (fc == 4) {
      for (int p = 0; p < 2; p++)                                                  // DF75-DF97: d0..d3 twice
        for (int j = 0; j < 4; j++) { int v = (T(X + j) << 2) & 0xFFFF; PUT(v); PUT(v >> 8); }
      X += 4;
      for (int p = 0; p < 2; p++) for (int j = 0; j < 8; j++) PUT(T(X + j));      // DFA1-DFBD: s0..s3 twice
      X += 8;                                                                      // DFBF-DFC5
    } else {
      for (int j = 0; j < 8; j++) { int v = (T(X) << 2) & 0xFFFF; PUT(v); PUT(v >> 8); X++; }   // DFD6-DFF1
      for (int j = 0; j < 8; j++) { PUT(T(X)); PUT(T(X + 1)); X += 2; }           // DFF3-E009
    }
  }
  #undef PUT
  #undef T
}

// FD/E022. `INC $1D` ($051D); even -> records 0-5 (E04B), odd -> 6-11 (E160). Per record, skipped when its VRAM word
// is negative: X = (acc & $0E00) >> 8 (the slot * 2), acc += $7F:0400+k*$20,X, source = $7F:0410+k*$20,X, bank = +4,
// DMAP7 = 01 (two registers, $2118/$2119 by BBAD $18, VMAIN $80), $80 bytes.
void CtTileAnimRun(uint8_t* w, CtTileAnimDma dma, void* ctx) {
  if (!w) return;
  w[0x051D]++;                                                                     // E03D
  int k0 = (w[0x051D] & 1) ? 6 : 0;                                                // E043-E048
  for (int k = k0; k < k0 + 6; k++) {
    uint8_t* r = w + 0x05B0 + k * 5;
    int dest = rd16(r + 2);
    if (dest & 0x8000) continue;                                                   // `LDA $B2 / BMI`
    int acc = rd16(r);
    int x = (acc & 0x0E00) >> 8;                                                   // `AND #0E00 / XBA / TAX`
    const uint8_t* t = w + 0x10400 + k * 0x20;
    acc = (acc + rd16(t + x)) & 0xFFFF;                                            // `CLC / ADC $7F0400,X / STA $B0`
    r[0] = (uint8_t)acc; r[1] = (uint8_t)(acc >> 8);
    if (dma) dma(ctx, dest, r[4], rd16(t + 0x10 + x), 0x80);                       // $4372 / $4374 / $4375 / $420B = 80
  }
}

int CtTileAnimShown(const uint8_t* w, int k, int* vramWord, int* bank, int* src) {
  if (!w || k < 0 || k >= 12) return 0;
  const uint8_t* r = w + 0x05B0 + k * 5;
  int dest = rd16(r + 2);
  if (dest & 0x8000) return 0;
  int acc = rd16(r), hit = -1, n = 0;
  const uint8_t* t = w + 0x10400 + k * 0x20;
  for (int s = 0; s < 8; s++) {
    int prev = (acc - rd16(t + s * 2)) & 0xFFFF;
    if (((prev & 0x0E00) >> 9) == s) { hit = s; n++; }
  }
  if (n != 1) return -1;
  *vramWord = dest; *bank = r[4]; *src = rd16(t + 0x10 + hit * 2);
  return 1;
}
