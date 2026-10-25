// seq_gwarp.c -- THE GATE WARP, native (see seq_gwarp.h): the program of bank $FD that op 0xFF $80..$8F jumps to, written over
// the WRAM image `w` (128 KB: $7E at 0, $7F at 0x10000) and the stage's picture (seq_pic.h), with no CPU and no PPU.
//
// The ROM's shape (disasm/notes/gate_warp_fd_family.md sec. 2 and 11): FD/DA00, DA5F, DABE and DB1D are one loop each --
//   JSR FD/DB97        the setup, NMI off: own NMI vectors (FD/EAF4), the PPU (FD/EB13), the flags saved (FD/EAA6), the
//                      mode-7 characters (each tile a flat colour) and map (FD/F1ED), the colours (FD/EF6F), the sprites
//                      (FD/F24C, $FD:DDB5), the ray picture drawn into $7F:4000 (DC1E-DCA0, FD/ECD5), the HDMA tables
//                      (FD/ED89, the hardware divider), the window table built twice (FD/EB8B), then RDNMI (FD/EADA)
//   loop: JSR FD/EAEB  wait for the NMI (FD/DE15: CGRAM and OAM by DMA, the window buffer, $1B += 1, HDMA on, INIDISP $0F)
//         [DB1D: INIDISP from $FD:DDF5, the brightness pulse]
//         JSR FD/DCBD  the mode-7 centre / scroll for $1B, the sprite colour ring rotated
//         $2123 / $23  by the counter's band (the window opening and closing)
//         JSR FD/EB8B  the next window table (a disc of radius $23)
//         JSR FD/F123  the BG colours' red and green from the sine
//   until $1B = the routine's end; then FD/EAC8 (blank, NMI off), FD/EAB7 (flags restored), JML $C0:0000.
//
// Every value is the ROM's: the tables are read from the user's ROM ($FD:DD96, DDF5, DDB5, ED79, ED81, F001, F081, F101, F181
// and the sine $C0:F800), the code's immediates are the code. Where the program leaves a register in memory -- the stack
// page (its JSRs, PHX, the NMI's pushes) and the accumulator's high byte (which XBA / TAX / TAY carry into an address or a
// colour: the first FF 83 frame's INIDISP is read at $FD:DDF5 + $3F00) -- the register is kept as the ROM leaves it, so the
// WRAM is the cartridge's byte for byte from the setup's end on. The clock: the setup is NMI-off work whose length does not
// depend on any data (CLAUDE.md, NMI-off loads), so its frame count is the listing's, measured once; every NMI-on iteration
// is free (the infinitely fast CPU): its writes land in the vblank, before the frame's HDMA starts.
#include "seq_gwarp.h"
#include "romview.h"     // the ROM's tables from ct_assets.dat (CT_ASSET_TABLE)
#include <string.h>
#include <stdio.h>

// FD/DB97's NMI-off stretch, from the hand-off (C0/3FE8's JML, line 239) to FD/EADA's RDNMI poll: 40,005,322 master cycles
// on the cartridge under the rule (ct.exe --racepiccmp RACEPICCMP_SPAN=FDFFF1,FDEADF from scene8_gate_warp_f2300.state; FF 81
// 40,005,274) -- it crosses 112 vblanks and the poll is met by the 112th's flag (EADF at frame +112, line 225). The setup
// reads nothing that differs between visits (no input, no WRAM it does not write first), so the count is the program's own.
#define GW_SETUP_FRAMES 112

// The four routines: their JSR return addresses (what the stack page keeps) and their constants -- the band edges of
// `CPY #imm` at DA0B / DA1F / DA2A (and the others') and the end of `CPY #imm` at DA4D.
typedef struct {
  uint16_t jSetup, jEaeb, jDcbd, jEb8b, jF123, jEac8, jEab7;
  uint16_t band1, band2, band3, end;
} Routine;
static const Routine kR[4] = {
  { 0xDA02, 0xDA05, 0xDA08, 0xDA47, 0xDA4A, 0xDA57, 0xDA5A, 0x0020, 0x02A0, 0x02C0, 0x02FF },   // FD/DA00 (FF 80)
  { 0xDA61, 0xDA64, 0xDA67, 0xDAA6, 0xDAA9, 0xDAB6, 0xDAB9, 0x0020, 0x00A0, 0x00C0, 0x00FF },   // FD/DA5F (FF 81)
  { 0xDAC0, 0xDAC3, 0xDAC6, 0xDB05, 0xDB08, 0xDB15, 0xDB18, 0x0020, 0x0040, 0x0060, 0x009F },   // FD/DABE (FF 82)
  { 0xDB1F, 0xDB26, 0xDB40, 0xDB7F, 0xDB82, 0xDB8F, 0xDB92, 0x0020, 0x02A0, 0x02C0, 0x02FF },   // FD/DB1D (FF 83 ...)
};

// ---- the machine the code runs on: ROM, WRAM, the direct page $0500, the stack, the WRAM port, the math unit ----
static uint8_t rom8(const CtGwarp* g, uint32_t a) {           // a 24-bit address in the HiROM banks ($C0-$FF; $00-$3F's upper half)
  uint8_t bank = (uint8_t)(a >> 16);
  if (bank < 0xC0 && !((bank & 0x7F) < 0x40 && (a & 0x8000))) {
    static int said = 0;
    if (!said) { said = 1; fprintf(stderr, "gwarp: a read of $%06X outside the ROM -- not modelled\n", (unsigned)a); }
    return 0;
  }
  long o = ((long)(bank & 0x3F) << 16) | (long)(a & 0xFFFF);
  return CtRomByte(g->rom, g->romLen, o);                     // (its tables: CT_ASSET_TABLE, romview.h)
}
static uint32_t wa(uint32_t a24) { return (((a24 >> 16) & 1) << 16) | (a24 & 0xFFFF); }   // $7E/$7F:xxxx in the image
#define DP(o) w[0x0500 + (o)]
static unsigned dp16(const uint8_t* w, unsigned o) { return (unsigned)(DP(o) | (DP(o + 1) << 8)); }
static void dpw16(uint8_t* w, unsigned o, unsigned v) { DP(o) = (uint8_t)v; DP(o + 1) = (uint8_t)(v >> 8); }
static unsigned l16(const uint8_t* w, uint32_t a24) { return (unsigned)(w[wa(a24)] | (w[wa(a24 + 1)] << 8)); }
static void lw16(uint8_t* w, uint32_t a24, unsigned v) { w[wa(a24)] = (uint8_t)v; w[wa(a24 + 1)] = (uint8_t)(v >> 8); }
static void push(CtGwarp* g, uint8_t* w, uint8_t v) { w[g->s] = v; g->s = (uint16_t)(g->s - 1); }
static uint8_t pull(CtGwarp* g, const uint8_t* w) { g->s = (uint16_t)(g->s + 1); return w[g->s]; }
static void jsr(CtGwarp* g, uint8_t* w, uint16_t ret) { push(g, w, (uint8_t)(ret >> 8)); push(g, w, (uint8_t)ret); }
static void rts(CtGwarp* g) { g->s = (uint16_t)(g->s + 2); }
static void setA(CtGwarp* g, unsigned a) { g->c = (uint16_t)((g->c & 0xFF00) | (a & 0xFF)); }   // an 8-bit load: B stays
#define A8 ((uint8_t)g->c)
static void wmSet(CtGwarp* g, unsigned lo16) { g->wm = (g->wm & 0x10000) | (lo16 & 0xFFFF); }   // STX $2181
static void wmBank(CtGwarp* g, uint8_t b) { g->wm = (g->wm & 0xFFFF) | ((uint32_t)(b & 1) << 16); }   // STA $2183
static void wmPut(CtGwarp* g, uint8_t* w, uint8_t v) { w[g->wm] = v; g->wm = (g->wm + 1) & 0x1FFFF; }   // STA $2180
static void mulB(CtGwarp* g, uint8_t b) { g->mulRes = (uint16_t)(g->mulA * b); }                  // STA $4203 (LakeSnes)
static void divB(CtGwarp* g, uint8_t b) {                                                          // STA $4206 (LakeSnes)
  if (!b) { g->divRes = 0xFFFF; g->mulRes = g->divA; }
  else { g->divRes = (uint16_t)(g->divA / b); g->mulRes = (uint16_t)(g->divA % b); }
}
static void reg(CtSeqStagePic* sp, uint8_t r, uint8_t v) { CtSeqPicWrite(&sp->p, r, v, 0); }    // a $21xx store, in the vblank
static uint8_t sine(const CtGwarp* g, unsigned i) { return rom8(g, 0x00F800u + i); }              // `LDA $F800,X` (DB = $00)

// ---- FD/EC80: A pairs of FF / 00 into the WRAM port (an empty window line); X kept (PHX / PLX at the index width) ----
static void ec80(CtGwarp* g, uint8_t* w, uint16_t ret, int x8) {
  jsr(g, w, ret);
  uint16_t keep = g->x;
  if (x8) push(g, w, (uint8_t)g->x); else { push(g, w, (uint8_t)(g->x >> 8)); push(g, w, (uint8_t)g->x); }   // EC80 PHX
  g->x = x8 ? (uint16_t)(g->c & 0xFF) : g->c;                                                                  // EC81 TAX
  while (g->x) {                                                                                               // EC82 BEQ
    setA(g, 0xFF); wmPut(g, w, 0xFF); wmPut(g, w, 0x00);                                                       // EC84-EC89
    g->x = (uint16_t)((g->x - 1) & (x8 ? 0xFF : 0xFFFF));                                                      // EC8C DEX
  }
  g->s = (uint16_t)(g->s + (x8 ? 1 : 2)); g->x = keep;                                                         // EC8F PLX
  rts(g);
}

// ---- FD/EB8B: the window table for a disc of radius $23, centre ($1F, $21) = (80, 80), into $7F:70AC or $7F:72AC ----
// (by $24's parity, which it advances); 255 lines of WH0 / WH1 pairs. The half-widths come from the hardware divider and
// multiplier: q = $0100 / r, then x walks down from r as y walks up (EBDF-EC00), into $7F:702C + y.
static void eb8b(CtGwarp* g, uint8_t* w, uint16_t ret) {
  jsr(g, w, ret);
  setA(g, 0x80); DP(0x1F) = 0x80; DP(0x21) = 0x80;                             // EB8B-EB8F
  DP(0x24)++;                                                                   // EB91 INC $24
  setA(g, DP(0x24));
  int odd = A8 & 1; setA(g, A8 >> 1);                                           // EB93 LDA $24 / LSR A
  g->x = odd ? 0x72AC : 0x70AC; wmSet(g, g->x);                                 // EB98 / EBA0
  setA(g, 0x7F); wmBank(g, 0x7F);                                               // EBA6
  setA(g, DP(0x23)); DP(0x0A) = A8;                                             // EBAB LDA $23 / STA $0A
  if (!A8) {                                                                    // EBAF BNE
    setA(g, 0xFF); ec80(g, w, 0xEBB5, 0);                                       // EBB1 LDA #FF / JSR EC80
    rts(g); return;
  }
  g->x = 0x0100; g->divA = 0x0100;                                              // EBB7 LDX #0100 / STX $4204
  setA(g, DP(0x0A)); divB(g, A8);                                               // EBBD STA $4206 (and seven NOPs)
  setA(g, g->divRes); g->mulA = A8;                                             // EBC9 LDA $4214 / STA $4202
  setA(g, DP(0x0A)); DP(0x15) = A8;                                             // EBCF
  setA(g, 0x80); DP(0x14) = 0x80;                                               // EBD3
  DP(0x16) = 0; DP(0x17) = 0;                                                   // EBD7 / EBD9
  g->x &= 0xFF; g->y &= 0xFF;                                                   // EBDB SEP #$10; EBDD REP #$20
  for (;;) {
    g->x = DP(0x17); mulB(g, (uint8_t)g->x);                                    // EBDF LDX $17 / STX $4203
    g->c = (uint16_t)(dp16(w, 0x14) - g->mulRes); dpw16(w, 0x14, g->c);         // EBE4 LDA $14 / SEC / SBC $4216 / STA $14
    if (g->c & 0x8000) break;                                                   // EBEC BMI
    g->c = (uint16_t)((g->c >> 8) | (g->c << 8));                               // EBEE XBA
    lw16(w, 0x7F702Cu + g->x, g->c);                                            // EBEF STA $7F702C,X
    g->y = DP(0x15); mulB(g, (uint8_t)g->y);                                    // EBF3 LDY $15 / STY $4203
    g->c = (uint16_t)(dp16(w, 0x16) + g->mulRes); dpw16(w, 0x16, g->c);         // EBF8 LDA $16 / CLC / ADC $4216 / STA $16
  }
  g->c = 0;                                                                     // EC02 LDA #0000 / SEP #$20
  setA(g, (uint8_t)(DP(0x21) - DP(0x0A)));                                      // EC07 LDA $21 / SEC / SBC $0A
  if (DP(0x21) >= DP(0x0A)) ec80(g, w, 0xEC10, 1);                              // EC0C BCC / EC0E JSR EC80
  g->y = DP(0x1F); g->x = DP(0x0A);                                             // EC11 LDY $1F / EC13 LDX $0A
  if (g->x >= DP(0x21)) g->x = DP(0x21);                                        // EC15 CPX $21 / BCC / EC19 LDX $21
  g->x = (uint8_t)(g->x - 1);                                                   // EC1B DEX
  do {                                                                          // EC1C-EC36: the upper half, r-1 down to 1
    uint8_t h = w[wa(0x7F702Cu + g->x)];
    unsigned d = (unsigned)(g->y & 0xFF) - h;
    if (d & 0x100) g->c = 0x0500; else setA(g, d);                              // EC1E SBC / EC22 BCS / EC24 TDC
    wmPut(g, w, A8);                                                            // EC25 STA $2180
    unsigned u = (unsigned)(g->y & 0xFF) + h;
    setA(g, u > 0xFF ? 0xFF : u);                                               // EC29 ADC / EC2E BCC / EC30 LDA #FF
    wmPut(g, w, A8);
    g->x = (uint8_t)(g->x - 1);                                                 // EC35 DEX / EC36 BNE
  } while (g->x);
  g->y = DP(0x1F); g->x = DP(0x0A);                                             // EC38 LDY $1F / EC3A LDX $0A
  setA(g, DP(0x21));
  if (A8 < 0xFF) {                                                              // EC3E CMP #FF / EC40 BCS -> EC7D
    setA(g, A8 + DP(0x0A));                                                     // EC42 CLC / ADC $0A
    if (A8 >= 0xFF) { setA(g, 0xFF - DP(0x21)); g->x = A8; }                    // EC45 CMP #FF / BCC; EC49-EC4E
    DP(0x18) = (uint8_t)g->x;                                                   // EC4F STX $18
    g->x = DP(0x08);                                                            // EC51 LDX $08
    do {                                                                        // EC53-EC70: the lower half, 0 up
      uint8_t h = w[wa(0x7F702Cu + g->x)];
      unsigned d = (unsigned)(g->y & 0xFF) - h;
      setA(g, (d & 0x100) ? 0 : d); wmPut(g, w, A8);                            // EC55 SBC / EC59 BCS / EC5B LDA #00
      unsigned u = (unsigned)(g->y & 0xFF) + h;
      setA(g, u > 0xFF ? 0xFF : u); wmPut(g, w, A8);                            // EC62 ADC / EC66 BCC / EC68 LDA #FF
      g->x = (uint8_t)(g->x + 1);                                               // EC6D INX / CPX $18 / BNE
    } while (g->x != DP(0x18));
    setA(g, (uint8_t)(0 - DP(0x0A) - DP(0x21)));                                // EC72 LDA #00 / SEC / SBC $0A / SEC / SBC $21
    ec80(g, w, 0xEC7C, 1);                                                      // EC7A JSR EC80
  }
  rts(g);                                                                       // EC7D REP #$10 / RTS
}

// ---- FD/EF23: the matrix value 0xFF * (the divider's quotient) >> 6, two bytes into the WRAM port (A = $FF) ----
static void ef23(CtGwarp* g, uint8_t* w, uint16_t ret) {
  jsr(g, w, ret);
  g->mulA = A8;                                                                 // EF23 STA $4202
  g->y = g->divRes; dpw16(w, 0x10, g->y);                                       // EF26 LDY $4214 / STY $10
  setA(g, DP(0x10)); mulB(g, A8);                                               // EF2B LDA $10 / STA $4203
  DP(0x0E) = 0; DP(0x0F) = 0;                                                   // EF30 / EF32
  g->y = g->mulRes; dpw16(w, 0x0C, g->y);                                       // EF35 LDY $4216 / STY $0C
  setA(g, DP(0x11)); mulB(g, A8);                                               // EF3A LDA $11 / STA $4203
  unsigned a = dp16(w, 0x0D), m = g->mulRes, s = a + m;                         // EF40 REP #$21 / LDA $0D / ADC $4216
  g->v = ((~(a ^ m) & (a ^ s)) & 0x8000) != 0;
  g->c = (uint16_t)s; dpw16(w, 0x0D, g->c);                                     // EF47 STA $0D
  for (int i = 0; i < 6; i++) {                                                 // EF49-EF5F: LSR $0E / ROR $0C, six times
    unsigned hi = dp16(w, 0x0E), lo = dp16(w, 0x0C);
    unsigned cy = hi & 1; dpw16(w, 0x0E, hi >> 1);
    dpw16(w, 0x0C, (lo >> 1) | (cy << 15));
  }
  g->c = (uint16_t)dp16(w, 0x0C);                                               // EF61 LDA $0C / SEP #$20
  wmPut(g, w, A8);                                                              // EF65 STA $2180
  g->c = (uint16_t)((g->c >> 8) | (g->c << 8)); wmPut(g, w, A8);                // EF68 XBA / STA $2180
  setA(g, 0x00);                                                                // EF6C LDA #00 (B keeps the low byte)
  rts(g);
}

// ---- FD/ECD5: one pixel of colour A at ($15, $17) into the 4bpp sprite picture at $7F:4000 (16 tiles a row) ----
static void ecd5(CtGwarp* g, uint8_t* w, uint16_t ret) {
  jsr(g, w, ret);
  DP(0x0A) = A8;                                                                // ECD5 STA $0A
  setA(g, (DP(0x17) & 7) << 1); DP(0x18) = A8; DP(0x19) = 0;                    // ECD7-ECDE
  setA(g, (DP(0x17) & 0xF8) >> 2);                                              // ECE0-ECE5
  g->c = (uint16_t)((g->c >> 8) | (g->c << 8));                                 // ECE6 XBA
  g->c = (uint16_t)(g->c + dp16(w, 0x18)); dpw16(w, 0x18, g->c);                // ECE7 REP #$21 / ADC $18 / STA $18
  g->c = 0;                                                                     // ECED LDA #0000 / SEP #$20
  setA(g, DP(0x15) & 0xF8);                                                     // ECF2 LDA $15 / AND #F8
  g->c = (uint16_t)((g->c << 2) + dp16(w, 0x18)); g->x = g->c;                  // ECF6 REP #$20 / ASL / ASL / CLC / ADC $18 / TAX
  g->c = 0;                                                                     // ECFE LDA #0000 / SEP #$20
  setA(g, DP(0x15) & 7); g->y = g->c;                                           // ED03 LDA $15 / AND #07 / TAY
  static const uint16_t plane[4] = { 0x0000, 0x0001, 0x0010, 0x0011 };          // ED0C / ED28 / ED44 / ED60
  for (int i = 0; i < 4; i++) {
    int cy = DP(0x0A) & 1; DP(0x0A) >>= 1;                                      // LSR $0A
    uint32_t a = 0x7F4000u + plane[i] + g->x;
    setA(g, cy ? (w[wa(a)] | DP(0x25 + g->y)) : (w[wa(a)] & DP(0x2D + g->y)));  // ORA $0525,Y / AND $052D,Y
    w[wa(a)] = A8;
  }
  rts(g);
}

// ---- FD/DB97: the setup, up to FD/EADA's RDNMI poll ----
static void setup(CtGwarp* g, uint8_t* w, CtSeqStagePic* sp) {
  jsr(g, w, kR[g->entry].jSetup);                                               // DA00 JSR DB97 (and the others')
  g->x = 0x0500; push(g, w, 0x05); push(g, w, 0x00); g->s += 2;                 // DB9C LDX #0500 / PHX / PLD
  setA(g, 0x00); push(g, w, 0x00); g->s += 1;                                   // DBA1 LDA #00 / PHA / PLB
  sp->hdmaen = 0;                                                               // DBA5 / DBA8 STZ $420B / $420C
  setA(g, 0x8F); reg(sp, 0x00, 0x8F);                                           // DBAB INIDISP $8F
  g->x = 0; dpw16(w, 0x08, 0);                                                  // DBB3 LDX #0000 / STX $08
  // FD/EAF4: the NMI and IRQ vectors at $0500 / $0504 (`JML FD/DE15`, `JML FD/DE87`)
  jsr(g, w, 0xDBBA);
  w[0x0500] = 0x5C; w[0x0504] = 0x5C; w[0x0501] = 0x15; w[0x0502] = 0xDE; w[0x0503] = 0xFD;
  w[0x0505] = 0x87; w[0x0506] = 0xDE; w[0x0507] = 0xFD; setA(g, 0xFD); g->x = 0xDE87;
  rts(g);
  // FD/EB13: the PPU -- mode 7, BG1 on the main screen windowed, OBJ on the sub, colour math BG1 + sub halved
  jsr(g, w, 0xDBBD);
  static const uint8_t regs[][2] = {
    { 0x01, 0x82 }, { 0x02, 0x00 }, { 0x03, 0x00 }, { 0x05, 0x07 }, { 0x06, 0x00 }, { 0x07, 0x48 }, { 0x0B, 0x00 },
    { 0x0C, 0x00 }, { 0x23, 0x03 }, { 0x24, 0x00 }, { 0x25, 0x00 }, { 0x26, 0x00 }, { 0x27, 0xFF }, { 0x28, 0x00 },
    { 0x29, 0xFF }, { 0x2A, 0x00 }, { 0x2B, 0x00 }, { 0x2C, 0x01 }, { 0x2D, 0x10 }, { 0x2E, 0x01 }, { 0x2F, 0x10 },
    { 0x30, 0x22 }, { 0x31, 0x41 }, { 0x32, 0xE0 }, { 0x33, 0x00 } };          // EB19-EB79
  for (unsigned i = 0; i < sizeof regs / sizeof regs[0]; i++) reg(sp, regs[i][0], regs[i][1]);
  setA(g, 0xFF);                                                                // EB7C (WRIO, HTIME / VTIME: no picture)
  rts(g);
  // FD/EAA6: the story flags $7F:0000-01FF saved to $7F:74B3 (the effect uses $7F:0000 as scratch)
  jsr(g, w, 0xDBC0);
  for (g->x = 0; g->x < 0x200; g->x++) { setA(g, w[0x10000 + g->x]); w[0x174B3 + g->x] = A8; }
  rts(g);
  for (g->x = 0; g->x < 8; g->x++) {                                            // DBC1-DBD5: the plot's bit masks
    setA(g, rom8(g, 0xFDED79u + g->x)); DP(0x25 + g->x) = A8;
    setA(g, rom8(g, 0xFDED81u + g->x)); DP(0x2D + g->x) = A8;
  }
  // DBD7-DBF0: the mode-7 characters -- VMAIN $80, the high bytes of words 0-3FFF: character n is 64 pixels of colour n
  setA(g, 0x80); sp->vmain = 0x80; g->y = 0; sp->vaddr = 0;
  for (; g->y < 0x100; g->y++) { setA(g, g->y); for (g->x = 0x40; g->x; g->x--) CtSeqStagePicVramWrite(sp, 1, A8); }
  // FD/F1ED: the mode-7 map into $7F:0000 through the port (D = $2100): 128 rows twice, row n starts at the sine's
  // ($F800[4n] + $80) / 2 and steps by one
  jsr(g, w, 0xDBF4);
  push(g, w, 0x05); push(g, w, 0x00);                                           // F1ED PHD
  g->x = 0x2100; push(g, w, 0x21); push(g, w, 0x00); g->s += 2;                 // F1EE LDX #2100 / PHX / PLD
  g->x = 0; wmSet(g, 0); setA(g, 0x7F); wmBank(g, 0x7F);                         // F1F3-F1FA
  for (int k = 0; k < 2; k++) {
    jsr(g, w, k ? 0xF201 : 0xF1FE);                                             // F1FC / F1FF JSR F204
    g->x &= 0xFF; g->y &= 0xFF;                                                 // F204 SEP #$10
    g->x = 0;
    do {
      setA(g, (uint8_t)((uint8_t)(sine(g, g->x) + 0x80) >> 1));                 // F208 LDA $F800,X / CLC / ADC #80 / LSR
      for (g->y = 0x80; g->y; g->y--) { setA(g, A8 & 0x7F); wmPut(g, w, A8); setA(g, A8 + 1); }   // F20F-F217
      g->x = (uint8_t)(g->x + 4);                                               // F219-F21D INX x4 / BNE
    } while (g->x);
    rts(g);                                                                     // F21F REP #$10 / RTS
  }
  g->s += 2; rts(g);                                                            // F202 PLD / RTS
  // FD/EF6F: the colours into $7E:2000 -- cleared, then 64 BG words from $FD:F001 | $FD:F081 << 2 (bytes swapped: red and
  // green, the blue kept) and 128 OBJ words from $FD:F101 (blue)
  jsr(g, w, 0xDBF7);
  wmSet(g, 0x2000); setA(g, 0x7E); wmBank(g, 0x7E);
  for (g->y = 0x200; g->y; g->y--) wmPut(g, w, 0);                              // EF7A-EF81
  g->x = 0x2000; wmSet(g, 0x2000); setA(g, 0x7E); wmBank(g, 0x7E);
  g->c = (uint16_t)dp16(w, 0x08); dpw16(w, 0x0C, g->c);                         // EF91 REP #$20 / LDA $08 / STA $0C
  for (g->y = 0x40; g->y; g->y--) {
    g->x = (uint16_t)((dp16(w, 0x0C) & 0x3F) << 1);
    unsigned lo = (unsigned)(rom8(g, 0xFDF001u + g->x) | (rom8(g, 0xFDF002u + g->x) << 8));   // EF9E LDA $FDF001,X
    dpw16(w, 0x0E, lo);
    unsigned hi = (unsigned)(rom8(g, 0xFDF081u + g->x) | (rom8(g, 0xFDF082u + g->x) << 8));   // EFAB LDA $FDF081,X
    g->c = (uint16_t)((((hi >> 8) | (hi << 8)) << 2) | lo);                     // EFAF XBA / ASL / ASL / ORA $0E
    wmPut(g, w, A8); g->c = (uint16_t)((g->c >> 8) | (g->c << 8)); wmPut(g, w, A8);   // EFB4-EFBA
    dpw16(w, 0x0C, dp16(w, 0x0C) + 1);                                          // EFBF INC $0C
  }
  g->c = 0;                                                                     // EFC4 LDA #0000 / SEP #$20
  g->x = 0x2100; wmSet(g, 0x2100); setA(g, 0x7E); wmBank(g, 0x7E);
  g->c = (uint16_t)dp16(w, 0x08); dpw16(w, 0x0C, g->c);
  for (g->y = 0x80; g->y; g->y--) {
    g->x = (uint16_t)((dp16(w, 0x0C) & 0x0F) << 1);
    unsigned v = (unsigned)(rom8(g, 0xFDF101u + g->x) | (rom8(g, 0xFDF102u + g->x) << 8));   // EFE4 LDA $FDF101,X
    g->c = (uint16_t)(((v >> 8) | (v << 8)) << 2);                              // EFE8 XBA / ASL / ASL
    wmPut(g, w, A8); g->c = (uint16_t)((g->c >> 8) | (g->c << 8)); wmPut(g, w, A8);
    dpw16(w, 0x0C, dp16(w, 0x0C) + 1);
  }
  g->c = 0;                                                                     // EFFB LDA #0000 / SEP #$20
  rts(g);
  // FD/F24C: every sprite at Y = $F0, the high table $55 (small, X bit 8 set: off screen)
  jsr(g, w, 0xDBFA);
  setA(g, 0xF0); for (g->x = 0x200; g->x; g->x -= 4) w[0x06FD + g->x] = 0xF0;
  setA(g, 0x55); for (g->x = 0x20; g->x; g->x--) w[0x08FF + g->x] = 0x55;
  rts(g);
  for (g->x = dp16(w, 0x08); g->x < 0x40; g->x++) { setA(g, rom8(g, 0xFDDDB5u + g->x)); w[0x0700 + g->x] = A8; }   // DBFB-DC08
  setA(g, 0xAA); w[0x0900] = w[0x0901] = w[0x0902] = w[0x0903] = 0xAA;           // DC0A-DC15: sprites 0-15 large
  // FD/F1C1: the map's low bytes, $7F:0000 x $4000 by DMA (VMAIN 0: the step after $2118)
  jsr(g, w, 0xDC1A);
  sp->vmain = 0x00; g->x = (uint16_t)dp16(w, 0x08); sp->vaddr = g->x;
  CtSeqStagePicDma(sp, w, 0x40, 0x18, 0x10000, 0x4000); g->x = 0x4000; setA(g, 0x01);
  rts(g);
  // FD/ECC0: the sprite picture cleared, $7F:4000-5FFF
  jsr(g, w, 0xDC1D);
  wmSet(g, 0x4000); setA(g, 0x7F); wmBank(g, 0x7F);
  for (g->x = 0x2000; g->x; g->x--) wmPut(g, w, 0);
  rts(g);
  // DC1E-DCA0: 65 rays out of the picture's corner, angle x = 0..$40 -- direction ($F800[x], $F800[x + $40]) in 1/256 px a
  // step, the step's colour the low nibble of -($0C >> 8), $0C growing by a decreasing $0E (from ($F800[(x + $40) << 4] +
  // $80) >> 3 + $100); each ray ends where it leaves the 128 x 128 picture
  for (g->x = 0; g->x != 0x41; ) {
    push(g, w, (uint8_t)(g->x >> 8)); push(g, w, (uint8_t)g->x);              // DC21 PHX
    setA(g, sine(g, g->x)); DP(0x10) = A8; DP(0x11) = 0;                        // DC22-DC27
    setA(g, (uint8_t)(g->x + 0x40)); g->x = g->c;                               // DC29 TXA / CLC / ADC #40 / TAX
    setA(g, sine(g, g->x)); DP(0x12) = A8; DP(0x13) = 0;                        // DC2E-DC33
    setA(g, (uint8_t)(g->x << 4)); g->x = g->c;                                 // DC35 TXA / ASL x4 / TAX
    setA(g, (uint8_t)((uint8_t)(sine(g, g->x) + 0x80) >> 3)); DP(0x0E) = A8;    // DC3B-DC44
    setA(g, 0x01); DP(0x0F) = 0x01;                                             // DC46 LDA #01 / STA $0F
    g->x = 0; dpw16(w, 0x14, 0); dpw16(w, 0x16, 0);                             // DC4A-DC4F
    g->x = 0x0100; dpw16(w, 0x0C, 0x0100);                                      // DC51 LDX #0100 / STX $0C
    for (;;) {
      setA(g, (uint8_t)(((DP(0x0D) & 0x0F) ^ 0xFF) + 1));                       // DC56 LDA $0D / AND #0F / EOR #FF / INC A
      ecd5(g, w, 0xDC5F);                                                       // DC5D JSR ECD5
      g->c = (uint16_t)(dp16(w, 0x0C) + dp16(w, 0x0E)); dpw16(w, 0x0C, g->c);    // DC60 REP #$21 / LDA $0C / ADC $0E / STA $0C
      dpw16(w, 0x0E, dp16(w, 0x0E) - 1);                                        // DC68 DEC $0E
      g->c = 0;                                                                 // DC6A LDA #0000 / SEP #$20
      setA(g, DP(0x0D) & 0x0F); if (!A8) DP(0x0D)++;                            // DC6F-DC75
      g->c = (uint16_t)(dp16(w, 0x14) + dp16(w, 0x10)); dpw16(w, 0x14, g->c);    // DC77-DC7D
      g->c = 0;
      setA(g, DP(0x15)); if (A8 & 0x80) break;                                  // DC84 LDA $15 / BMI
      g->c = (uint16_t)(dp16(w, 0x16) + dp16(w, 0x12)); dpw16(w, 0x16, g->c);    // DC88-DC8E
      g->c = 0;
      setA(g, DP(0x17)); if (A8 & 0x80) break;                                  // DC95 LDA $17 / BPL
    }
    { uint8_t lo = pull(g, w); g->x = (uint16_t)(lo | (pull(g, w) << 8)); }    // DC99 PLX
    g->x++;                                                                     // DC9A INX / CPX #0041
  }
  // FD/EC91: the sprite picture, $7F:4000 x $2000 to VRAM $4000 (OBSEL $82's base)
  jsr(g, w, 0xDCA5);
  sp->vmain = 0x80; g->x = 0x4000; sp->vaddr = 0x4000;
  CtSeqStagePicDma(sp, w, 0x41, 0x18, 0x14000, 0x2000); setA(g, 0x01);
  rts(g);
  DP(0x24) = 0; DP(0x23) = 0;                                                   // DCA6 STZ $24 / STZ $23
  eb8b(g, w, 0xDCAC);                                                           // DCAA JSR EB8B
  DP(0x24)++;                                                                   // DCAD INC $24
  eb8b(g, w, 0xDCB1);                                                           // DCAF JSR EB8B
  // FD/ED89: the HDMA -- channels 1-4 indirect, mode 3 (M7A/B, M7C/D, M7X/Y, BG1 H/V), channel 5 indirect mode 1 (WH0/WH1)
  jsr(g, w, 0xDCB4);
  static const CtSeqPicHdma ch[6] = { { 0, 0, 0, 0, 0 },
    { 0x43, 0x1B, 0x6000, 0x7F, 0x7F }, { 0x43, 0x1D, 0x6007, 0x7F, 0x7F }, { 0x43, 0x1F, 0x600E, 0x7F, 0x7F },
    { 0x43, 0x0D, 0x6015, 0x7F, 0x7F }, { 0x41, 0x26, 0x74AC, 0x7F, 0x7F } };   // ED89-EDFE
  for (int i = 1; i <= 5; i++) sp->ch[i] = ch[i];
  setA(g, 0xF0);                                                                // EE01-EE17: the tables' line counts
  w[0x16000] = w[0x16003] = w[0x16007] = w[0x1600A] = w[0x174AC] = w[0x174AF] = 0xF0;
  w[0x1600E] = w[0x16011] = w[0x16015] = w[0x16018] = 0x70;
  w[0x16006] = w[0x1600D] = w[0x16014] = w[0x1601B] = w[0x174B2] = 0x00;
  lw16(w, 0x7F6001, 0x605C); lw16(w, 0x7F6004, 0x621C); lw16(w, 0x7F6008, 0x685C); lw16(w, 0x7F600B, 0x6A1C);   // EE43-EE87
  lw16(w, 0x7F600F, 0x701C); lw16(w, 0x7F6012, 0x7020); lw16(w, 0x7F6016, 0x7024); lw16(w, 0x7F6019, 0x7028);
  lw16(w, 0x7F74AD, 0x70CC); lw16(w, 0x7F74B0, 0x71AC);
  g->c = 0;                                                                     // EE8B LDA #0000 / SEP #$20
  // EE90-EF22: the per-line scale, 256 lines -- d = $F0 - (|line| * 2 * $E0 >> 8) (|line| = the line, or its complement
  // past $7F), q = $FFFF / d, the value $FF * q >> 6 -- as M7A (and M7B 0) from $7F:601C, as M7D (M7C 0) from $7F:681C
  for (int pass = 0; pass < 2; pass++) {
    g->x = pass ? 0x681C : 0x601C; wmSet(g, g->x); setA(g, 0x7F); wmBank(g, 0x7F);
    if (!pass) { g->x = 0xFFFF; g->divA = 0xFFFF; }                             // EE9B LDX #FFFF / STX $4204
    for (g->x = dp16(w, 0x08); g->x != 0x0100; g->x++) {
      setA(g, g->x);                                                            // EEA3 TXA
      if (A8 & 0x80) setA(g, A8 ^ 0xFF);                                        // EEA4 BPL / EOR #FF
      setA(g, A8 << 1); g->mulA = A8;                                           // EEA8 ASL / STA $4202
      setA(g, 0xE0); mulB(g, 0xE0);                                             // EEAC STA $4203
      setA(g, g->mulRes >> 8); DP(0x0A) = A8;                                   // EEB4 LDA $4217 / STA $0A
      setA(g, (uint8_t)(0xF0 - DP(0x0A))); divB(g, A8);                         // EEB9 LDA #F0 / SEC / SBC $0A / STA $4206
      if (!pass) {
        setA(g, g->divRes); g->mulA = A8;                                       // EEC8 LDA $4214 / STA $4202
        setA(g, 0xFF); ef23(g, w, 0xEED2);                                      // EECE LDA #FF / JSR EF23
        wmPut(g, w, 0); wmPut(g, w, 0);                                         // EED3 / EED6 STZ $2180
      } else {
        wmPut(g, w, 0); wmPut(g, w, 0);                                         // EF11 / EF14 STZ $2180
        setA(g, 0xFF); ef23(g, w, 0xEF1B);                                      // EF17 LDA #FF / JSR EF23
      }
    }
  }
  rts(g);                                                                       // EF22 RTS (C = 1: the last CPX #0100)
  jsr(g, w, 0xDCB7);                                                            // DCB5 JSR EADA: the poll (CtGwarpFrame)
}

// ---- FD/DE15: the warp's NMI ----
static void nmi(CtGwarp* g, uint8_t* w, CtSeqStagePic* sp, uint8_t p) {
  uint16_t c = g->c, x = g->x, y = g->y;
  push(g, w, 0xFD); push(g, w, 0xEA); push(g, w, 0xEF); push(g, w, p);          // the interrupt: K, PC (the wait at EAED /
  // EAEF -- which of the two depends on the beam; the bytes are overwritten before any frame ends, by EB8B's JSR EC80), P
  push(g, w, (uint8_t)(c >> 8)); push(g, w, (uint8_t)c);                         // DE15 REP #$30 / PHA
  push(g, w, (uint8_t)(x >> 8)); push(g, w, (uint8_t)x);                         // PHX
  push(g, w, (uint8_t)(y >> 8)); push(g, w, (uint8_t)y);                         // PHY
  push(g, w, 0x00); push(g, w, 0x05); push(g, w, 0x00);                          // PHB / PHD
  push(g, w, 0x05); push(g, w, 0x00); g->s += 2;                                 // DE20 LDX #0500 / PHX / PLD
  push(g, w, 0x00); g->s += 1;                                                   // DE25 TDC / PHA / PLB
  sp->hdmaen = 0;                                                               // DE2B / DE2E STZ $420B / $420C
  reg(sp, 0x00, 0x80);                                                          // DE31 INIDISP $80
  jsr(g, w, 0xDE38);                                                            // DE36 JSR F266: CGRAM <- $7E:2000 x $200
  sp->cgadd = 0; CtSeqStagePicDma(sp, w, 0x42, 0x22, 0x02000, 0x200);
  rts(g);
  jsr(g, w, 0xDE3B);                                                            // DE39 JSR F222: OAM <- $00:0700 x $220
  reg(sp, 0x02, 0x00); CtSeqStagePicDma(sp, w, 0x40, 0x04, 0x00700, 0x220);
  rts(g);
  if (DP(0x24) & 1) { lw16(w, 0x7F74AD, 0x72CC); lw16(w, 0x7F74B0, 0x73AC); }   // DE3C LDA $0524 / LSR / BCS: the window
  else { lw16(w, 0x7F74AD, 0x70CC); lw16(w, 0x7F74B0, 0x71AC); }                 // table EB8B built last
  dpw16(w, 0x1B, dp16(w, 0x1B) + 1);                                            // DE6E LDX $1B / INX / STX $1B
  DP(0x1E)++;                                                                   // DE73 INC $1E
  sp->hdmaen = 0x3E;                                                            // DE75 HDMAEN $3E (channels 1-5)
  reg(sp, 0x00, 0x0F);                                                          // DE7A INIDISP $0F
  g->s += 9 + 4;                                                                // DE7F PLD / PLB / PLY / PLX / PLA / RTI
  g->c = c; g->x = x; g->y = y;
}

// ---- FD/DCBD: the mode-7 centre and scroll for $1B, then the sprite colour ring (15 words at $7E:2102) left by two ----
static void dcbd(CtGwarp* g, uint8_t* w) {
  jsr(g, w, kR[g->entry].jDcbd);
  setA(g, 0x80); w[0x1701C] = w[0x17020] = 0x80;                                 // DCBD-DCC3: M7X = $0080 (both bands)
  setA(g, 0x00); w[0x1701D] = w[0x17021] = w[0x17024] = w[0x17025] = w[0x17028] = w[0x17029] = 0;   // DCC7-DCDD: HOFS 0
  setA(g, DP(0x1B));
  g->c = (uint16_t)((g->c << 2) & 0x01FF); lw16(w, 0x7F7026, g->c);              // DCE3 REP #$20 / ASL / ASL / AND #01FF
  g->c = (uint16_t)(g->c + 0x70); lw16(w, 0x7F701E, g->c);                       // DCEE CLC / ADC #0070: M7Y, band 1
  g->c = 0;                                                                     // DCF6 LDA #0000 / SEP #$20
  setA(g, DP(0x1B) ^ 0xFF);                                                     // DCFB LDA $1B / EOR #FF
  g->c = (uint16_t)(((g->c << 2) - 0x70) & 0x01FF); lw16(w, 0x7F702A, g->c);     // DCFF ASL / ASL / SEC / SBC #70 / AND
  g->c = (uint16_t)(g->c + 0x70); lw16(w, 0x7F7022, g->c);                       // DD0E CLC / ADC #0070: M7Y, band 2
  g->c = 0;                                                                     // DD16 LDA #0000 / SEP #$20, DD1B BRA DD65
  g->x = (uint16_t)dp16(w, 0x08);                                               // DD65 LDX $08
  g->c = (uint16_t)l16(w, 0x7E2102); dpw16(w, 0x0C, g->c);
  g->c = (uint16_t)l16(w, 0x7E2104); dpw16(w, 0x0E, g->c);
  for (; g->x != 0x1A; g->x += 2) { g->c = (uint16_t)l16(w, 0x7E2106u + g->x); lw16(w, 0x7E2102u + g->x, g->c); }
  lw16(w, 0x7E211C, dp16(w, 0x0C)); lw16(w, 0x7E211E, dp16(w, 0x0E));
  g->c = 0;                                                                     // DD90 LDA #0000 / SEP #$20
  rts(g);
}

// ---- FD/F123: the 32 BG colours at $7E:2020 -- each keeps its blue, red = $F800[$1B] + $80 times $FD:F181[X] >> 8,
// green the same from $F800[$1B ^ $FF] ----
static void f123(CtGwarp* g, uint8_t* w) {
  jsr(g, w, kR[g->entry].jF123);
  for (g->x = (uint16_t)dp16(w, 0x08); g->x != 0x40; g->x += 2) {
    setA(g, DP(0x1B)); g->y = g->c;                                             // F125 LDA $1B / TAY
    setA(g, (uint8_t)(rom8(g, 0x00F800u + g->y) + 0x80)); g->mulA = A8;         // F128 LDA $F800,Y / CLC / ADC #80 / STA $4202
    setA(g, rom8(g, 0xFDF181u + g->x)); mulB(g, A8);                            // F131 LDA $FDF181,X / STA $4203
    setA(g, g->mulRes >> 8); DP(0x0E) = A8; DP(0x0F) = 0;                       // F13B LDA $4217 / STA $0E / STZ $0F
    setA(g, DP(0x1B) ^ 0xFF); g->y = g->c;                                      // F142 LDA $1B / EOR #FF / TAY
    uint8_t s = rom8(g, 0x00F800u + g->y);
    g->v = (s & 0x80) != 0;                                                     // F14B ADC #80: V = the sine's sign
    setA(g, (uint8_t)(s + 0x80)); g->mulA = A8;
    setA(g, rom8(g, 0xFDF181u + g->x)); mulB(g, A8);
    setA(g, g->mulRes >> 8);                                                    // F15A LDA $4217
    g->c = (uint16_t)((g->c >> 8) | (g->c << 8));                               // F15D XBA
    g->c = (uint16_t)((g->c >> 3) | dp16(w, 0x0E)); dpw16(w, 0x0E, g->c);        // F15E REP #$20 / LSR x3 / ORA $0E / STA $0E
    g->c = (uint16_t)((l16(w, 0x7E2020u + g->x) & 0x7C00) | g->c);               // F167 LDA $7E2020,X / AND #7C00 / ORA $0E
    lw16(w, 0x7E2020u + g->x, g->c);
    g->c = 0;                                                                   // F174 LDA #0000 / SEP #$20
  }
  rts(g);
}

int CtGwarpHas(uint8_t op) { return op >= 0x80 && op < 0x90; }

int CtGwarpOpen(CtGwarp* g, uint8_t* w, uint8_t op, CtSeqStagePic* sp, const uint8_t* rom, long romLen) {
  (void)w; (void)sp;
  memset(g, 0, sizeof *g);
  if (!rom || romLen < 0x400000) return -1;
  g->rom = rom; g->romLen = romLen; g->op = op;
  g->entry = op == 0x80 ? 0 : op == 0x81 ? 1 : op == 0x82 ? 2 : 3;               // C0/3FE4-3FFC
  g->c = op; g->x = 0x06FF; g->y = 0; g->s = 0x06FF;                             // C0/3FD6 LDX #06FF / TXS, A = $DF
  return 0;
}

int CtGwarpFrame(CtGwarp* g, uint8_t* w, CtSeqStagePic* sp, uint8_t* rgb) {
  if (g->done) return 1;
  g->frames++;
  const Routine* R = &kR[g->entry];
  if (g->frames == 1) setup(g, w, sp);                                          // the hand-off's frame: DB97 to the poll
  else if (g->frames == GW_SETUP_FRAMES + 1) {
    // FD/EADA met the vblank's flag: the NMI on, INIDISP 0, CLI; DCB8 STZ $1B / $1C; [DB20 STZ $35 / $36]; the loop's first
    // wait (STZ $1E, the spin) -- the flags at the spin: Z (LDA $1E), C = 1 from ED89's last CPX, V from its last EF23's ADC
    setA(g, 0x00); reg(sp, 0x00, 0x00);
    rts(g);
    DP(0x1B) = 0; DP(0x1C) = 0;
    rts(g);
    if (g->entry == 3) { DP(0x35) = 0; DP(0x36) = 0; }
    jsr(g, w, R->jEaeb); DP(0x1E) = 0; setA(g, DP(0x1E));
    g->cy = 1;
  } else if (g->frames > GW_SETUP_FRAMES + 1) {
    nmi(g, w, sp, (uint8_t)(0x22 | (g->v << 6) | g->cy));                      // P = N0 V M1 X0 D0 I0 Z1 C
    g->cy = 0;                                                                  // (every later spin: C = 0 from CPY #end)
    setA(g, DP(0x1E)); DP(0x1E) = 0; rts(g);                                     // EAED LDA $1E / BEQ, EAF1 STZ $1E / RTS
    if (g->entry == 3) {                                                        // DB27-DB3B: the brightness pulse
      unsigned s = DP(0x35) + 0x98u; DP(0x35) = (uint8_t)s;
      setA(g, (DP(0x36) + (s >> 8)) & 0x1F); DP(0x36) = A8; g->x = g->c;        // ADC #00 / AND #1F / STA $36 / TAX
      setA(g, rom8(g, 0xFDDDF5u + g->x)); reg(sp, 0x00, A8);                    // LDA $FDDDF5,X / STA $2100
    }
    dcbd(g, w);
    g->y = (uint16_t)dp16(w, 0x1B);                                             // DA09 LDY $1B
    uint8_t sel;
    if (g->y < R->band1 || (g->y >= R->band2 && g->y < R->band3)) {             // the opening and the closing: W1 on BG1
      setA(g, DP(0x1B) & 0x1F); g->x = g->c;                                    // LDA $1B / AND #1F / TAX
      setA(g, rom8(g, 0xFDDD96u + g->x)); DP(0x23) = A8;                         // LDA $FDDD96,X / STA $23: the radius
      sel = g->y < R->band1 ? 0x03 : 0x02;                                      // inverted (outside the disc), then plain
    } else { DP(0x23) = 0; sel = g->y < R->band2 ? 0x00 : 0x08; }               // no window; then W2 (00-FF: all of BG1 off)
    setA(g, sel); reg(sp, 0x23, sel);                                           // STA $2123
    eb8b(g, w, R->jEb8b);
    f123(g, w);
    g->y = (uint16_t)dp16(w, 0x1B);                                             // DA4B LDY $1B / CPY #end
    if (g->y == R->end) {
      jsr(g, w, R->jEac8);                                                      // FD/EAC8: blank, HDMA and the NMI off
      sp->hdmaen = 0; setA(g, 0x80); reg(sp, 0x00, 0x80); setA(g, 0x00);
      rts(g);
      jsr(g, w, R->jEab7);                                                      // FD/EAB7: the flags back
      for (g->x = (uint16_t)dp16(w, 0x08); g->x < 0x200; g->x++) { setA(g, w[0x174B3 + g->x]); w[0x10000 + g->x] = A8; }
      rts(g);
      g->done = 1;                                                              // JML $C0:0000
      return 1;
    }
    jsr(g, w, R->jEaeb); DP(0x1E) = 0; setA(g, DP(0x1E));                        // JMP -> JSR EAEB: STZ $1E, the spin
  }
  CtSeqStagePicFrame(sp, w, rgb);
  return 0;
}
