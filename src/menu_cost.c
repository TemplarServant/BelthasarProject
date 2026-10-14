// What the menu shell's open costs the CPU (menu_cost.h). Every constant is a sum of LakeSnes instruction costs, read
// off the oracle's beam traces one instruction at a time (tools/cycle_costs.py); the comments name the instructions.
// The Python twin tools/vwf_cost_proto.py is the same walk and is what tools/menu_init_cmp.py grades against the trace.
#include "menu_cost.h"

#include <stddef.h>

static long fo(unsigned a24) { return (long)(((a24 >> 16) & 0xFF) - 0xC0) * 0x10000L + (a24 & 0xFFFF); }
static int  rb(const uint8_t* rom, long romLen, long o) { return (o >= 0 && o < romLen) ? rom[o] : -1; }

// ---- C2/5E89: one plane-row (the caller's JSR is not in it) -------------------------------------------------------
#define ROW_HEAD (28 + 42 + 18 + 20 + 12)                 // 5E89 LDY $60, 5E8B LDA [$76],Y, 5E8D XBA, 5E8E LDA $62, LSR A
#define ROW_ODD  (12 + 42 + 12 * 4 + 18)                  // 5E91 BCC (not taken), 5E93 LDA [$79],Y, 4 x ASL A, 5E99 BRA
#define ROW_EVEN (18 + 42 + 12)                           // 5E91 BCC (taken), 5E9B LDA [$79],Y, 5E9D AND #F0
#define ROW_MID  (18 + 12 + 28 + 28 + 18 + 12 + 12 + 12 + 36)   // 5E9F REP .. 5EAC JMP ($5EAF,X)
#define ARM0     (18 + 28 + 36 + 44 + 36 + 18 + 44 + 40)        // 5EBF: two plain stores
#define ARM14    (18 + 28 + 36 + 44 + 36 + 18 + 44 + 44 + 40)   // 5ED3 .. 5EE4, after n x LSR A (12 each)
#define ARM57    (18 + 28 + 36 + 44 + 36 + 18 + 44 + 36 + 20 + 44 + 44 + 40)   // 5EEE .. 5F06, after (8-n) x ASL/ROL (62)

static long row(int code, int pen) {
  int n = pen & 7;                                        // $6A: the jump table d[0x25EAF] by the pen's phase
  long c = ROW_HEAD + ((code & 1) ? ROW_ODD : ROW_EVEN) + ROW_MID;
  if (n == 0) c += ARM0;
  else if (n <= 4) c += 12 * n + ARM14;
  else c += 62 * (8 - n) + ARM57;
  return c;
}

// ---- C2/5E36: the 2 bpp row loop -- 4 x 2 rows, $73 += F0, 8 x 2 rows ------------------------------------------------
#define LOOP_PRO (20 + 12 + 20 + 26 + 12 + 20 + 20 + 20)  // 5E36 PHB .. 5E41 STZ $61
#define LOOP_IT  (40 + 18 + 50 + 50 + 18 + 40 + 18 + 50 + 50 + 18 + 34)   // two JSRs + their INCs + DEC $68
#define LOOP_MID (18 + 28 + 12 + 18 + 28 + 18 + 12 + 20)  // 5E5D .. 5E6B
#define LOOP_EPI (26 + 40)                                // 5E87 PLB, 5E88 RTS

// ---- C2/5DC4: the glyph -------------------------------------------------------------------------------------------------
#define GLYPH_PRO (18 + 28 + 12 + 12 + 12 + 28 + 12 + 28 + 12 + 18 + 28 + 28 + 12 + 12 + 12 + 12 + 28 + 12 + 28 + 12 + 18 \
                   + 28 + 28 + 18 + 28 + 28 + 12 + 18 + 28 + 18 + 20 + 20 + 12 + 20 + 20 + 20 + 12 + 12 + 12 + 20 + 20   \
                   + 20 + 20 + 20 + 12 + 12 + 40)          // 5DC4 .. 5E14 BNE (not taken: 2 bpp), 5E16 JSR C2/5E36
#define GLYPH_EPI (18 + 12 + 18 + 12 + 20 + 12 + 12 + 12 + 30 + 20 + 20 + 34 + 12 + 18 + 40)   // 5E19 BRA .. 5E35 RTS

static long glyph(int code, int pen) {
  long c = GLYPH_PRO + LOOP_PRO + LOOP_MID + LOOP_EPI;
  c += 4 * LOOP_IT + 3 * 18 + 12;                         // 5E5B BNE: taken three times, falls through once
  c += 8 * LOOP_IT + 7 * 18 + 12;                         // 5E85 BNE (5E83 DEC $68 costs as 5E59)
  c += 24 * row(code, pen);
  return c + GLYPH_EPI;
}

// ---- C2/58B2: the string loop, per byte; the terminator's C2/594F -------------------------------------------------------
#define BYTE_HEAD  (42 + 18 + 50 + 18 + 12)               // 58B2 LDA [$31] .. 58BA CMP #A0
#define GLYPH_CALL (12 + 18 + 28 + 18 + 40)               // 58BC BCC (not taken), 58BE REP, 58C0 STA $35, SEP, 58C4 JSR
#define GLYPH_TAIL (34 + 18)                              // 58C7 DEC $13, 58C9 BNE (taken)
#define TERM       (18 + 12 + 18 + 12 + 12 + 12 + 36 + 12 + 20 + 40)   // 58BC BCC (taken) .. 5900 JMP, 594F .. 5952 RTS

// ---- C2/5823 (C2:0009) with $15 = 4, $30 = 0; C2/57DF (C2:0003) with $14 = 80; C2/F90C (one line, no newline) ----------
#define VEC        18                                     // the bank vector's JMP (C2:0003 -> 57DF, C2:0009 -> 5823)
#define CALL9_HEAD (20 + 18 + 28 + 18 + 12 + 18 + 18 + 40)   // 5823 PHP .. 5830 JSR C2/584A
#define PRE_584A   (20 + 12 + 12 + 36 + 18 + 12 + 40)     // 584A .. 584E JMP ($5851,X) -> 5895 BRA -> 58B0 CLC / RTS
#define CALL9_DISP (12 + 20 + 12 + 12 + 52)               // 5833 BCS (not taken) .. 5839 JSR ($5842,X)
#define CALL9_TAIL (12 + 18 + 34 + 26 + 42)               // 583C LDA #00, XBA, PLD, PLP, RTL
#define INIT3      704                                    // 57DF PHP .. 5822 RTL on the pen-0 arm (5801 BNE taken,
                                                          //   580D BPL not taken, 580F STZ $34): 34 instructions
#define F90C_HEAD  (20 + 18 + 12 + 26 + 34 + 34 + 12 + 26 + 54)   // F90C PHP .. F91F JSL C2:0003
#define F90C_LINE  (12 + 26 + 54)                         // F923 LDA #20, F925 STA $0213, F928 JSL C2:0009
#define F90C_TAIL  (26 + 12 + 18 + 26 + 40)               // F92C LDA $0215, CMP #05, F931 BNE (taken), F941 PLP, RTS

long ct_menu_vwf_cycles(const uint8_t* rom, long romLen, unsigned table, int idx) {
  if (!rom) return -1;
  long tab = fo(0xFF0000u | (table & 0xFFFF)) + 2L * idx;
  int lo = rb(rom, romLen, tab), hi = rb(rom, romLen, tab + 1);
  if (lo < 0 || hi < 0) return -1;
  long o = fo(0xFF0000u | (unsigned)(lo | (hi << 8)));
  long loop = 0; int pen = 0;
  for (int k = 0; k < 256; k++) {
    int b = rb(rom, romLen, o + k);
    if (b < 0) return -1;
    if (b == 0) { loop += BYTE_HEAD + TERM; break; }
    if (b < 0xA0) return -1;                              // a control / dictionary code: not walked here
    loop += BYTE_HEAD + GLYPH_CALL + glyph(b, pen) + GLYPH_TAIL;
    int adv = rb(rom, romLen, 0x260E6L + (b - 0xA0));     // $C2:60E6, the advance table
    if (adv < 0) return -1;
    pen = (pen + adv) & 0xFF;
    if (k == 255) return -1;
  }
  return F90C_HEAD + VEC + INIT3 + F90C_LINE + VEC + CALL9_HEAD + PRE_584A + CALL9_DISP + loop + CALL9_TAIL + F90C_TAIL;
}

// ---- C2/D296: the slot's title line (the clear, then C2/F90C over the $FF:D03E table) ------------------------------------
#define D296_HEAD (20 + 18 + 12 + 26 + 18 + 18 + 34 + 28 + 18 + 12 + 18 + 34 + 12 + 40 + 12 + 12 + 12 + 18)   // .. D2B9
#define D296_MVN  (510 * 46)                              // D2BC MVN $7E,$7E: $01FD + 1 bytes
#define D296_TAIL (40 + 26 + 40)                          // D2BF JSR C2/F90C, D2C2 PLP, D2C3 RTS

static long d296(const uint8_t* rom, long romLen, int idx) {
  long v = ct_menu_vwf_cycles(rom, romLen, 0xD03E, idx);
  return v < 0 ? -1 : D296_HEAD + D296_MVN + D296_TAIL + v;
}

// ---- FF/F9C4: a slot's verdict (FF/FBD3 the base, the signature at $30:7FF8, FF/FBE7 the checksum) ----------------------
#define F9C4_HEAD (28 + 20 + 20 + 18 + 34 + 26 + 26 + 40 + (28 + 18 + 12 + 12 + 36 + 28 + 40) + 28 + 18 + 12 + 12 + 40 + 18)
#define F9C4_TAIL (28 + 26 + 26 + 34 + 42)                // F9F5 STA $00 .. F9FA RTL
#define FBE7      (20 + 20 + 18 + 34 + 26 + 26 + 28 + 28 + 18 + 18 + 12 + 1280L * (50 + 12 + 12) + 1279L * 18 + 12 \
                   + 26 + 26 + 40)                        // 1280 words summed, Y = 09FE down by 2

static unsigned slot_sum(const uint8_t* sram, int slot) {
  unsigned acc = 0, carry = 0;                            // FF/FBE7: CLC once; ADC ($00),Y carries through the chain
  long base = 0x0A00L * slot;
  for (int y = 0x9FE; y >= 0; y -= 2) {
    acc += (unsigned)(sram[base + y] | (sram[base + y + 1] << 8)) + carry;
    carry = acc > 0xFFFF; acc &= 0xFFFF;
  }
  return acc;
}

int ct_menu_slot_verdict(const uint8_t* sram, int slot) {
  if (!sram) return 0x8080;
  if ((sram[0x1FF8 + 2 * slot] | (sram[0x1FF9 + 2 * slot] << 8)) != 0xE41B) return 0x8080;
  unsigned chk = (unsigned)(sram[0x1FF0 + 2 * slot] | (sram[0x1FF1 + 2 * slot] << 8));
  return slot_sum(sram, slot) == chk ? 0x0000 : 0x4040;
}

static long f9c4(const uint8_t* sram, int slot) {
  int v = ct_menu_slot_verdict(sram, slot);
  if (v == 0x8080) return F9C4_HEAD + 18 + 18 + 18 + F9C4_TAIL;   // F9DE BNE (taken), F9ED LDA #8080, F9F0 BRA
  long c = F9C4_HEAD + 12 + 40 + FBE7 + 40;               // F9DE BNE (not taken), F9E0 JSR FF/FBE7, F9E3 CMP $7FF0,X
  return c + (v == 0 ? 12 + 18 + 18 : 18 + 18) + F9C4_TAIL;   // F9E6 BNE: F9E8/F9EB (ok) | F9F2 (the sum disagrees)
}

// ---- C2/D19F: a valid slot's record (three members, the level, the flag) and C2/D266's title --------------------------
#define D19F_HEAD    (20 + 18 + 28 + 18 + 12 + 12 + 18 + 28 + 28 + 28 + 18 + 12 + 20)   // D19F PHP .. D1B6 STA $24
#define MEMBER_HEAD  (28 + 12 + 32 + 28 + 32 + 30)        // D1B8 LDY $61 .. D1C5 STA $004202
#define MEMBER_FULL  (12 + 12 + 30 + 18 + 18 + 28 + 36 + 12 + 40 + 40 + 18 + 9 * 46 + 18 + 12 + 30 + 18 + 18 + 28 + 36 \
                      + 12 + 18 + 6 * 46)                 // D1C9 BMI (not taken) .. D201 MVN (6 bytes)
#define MEMBER_EMPTY 18                                   // D1C9 BMI (taken)
#define MEMBER_TAIL  (18 + 20 + 12 + 12 + 12 + 20 + 34 + 34)   // D204 SEP .. D211 DEC $24
#define D19F_MID     (18 + 28 + 28 + 40 + 18 + 40 + 12 + 12 + 40 + 18 + 40 + 12 + 12 + 28 + 12 + 18 + 12 + 18 + 10 * 46 \
                      + 28 + 40 + 40 + 40 + 18)           // D215 .. D252 AND #0080, without D222's branch
#define D19F_TAIL    (40 + 26 + 40)                       // D261 JSR C2/D266, D264 PLP, D265 RTS
#define D266_HEAD    (20 + 18 + 28 + 28 + 32 + 12 + 32 + 32 + 18 + 12)   // D266 PHP .. D27C LDX #1A
#define D266_TAIL    (26 + 40 + 26 + 40)                  // D285 STX $020C, D288 JSR C2/D296, D28B PLP, D28C RTS

static long d19f(const uint8_t* rom, long romLen, const uint8_t* sram, long base) {
  long c = D19F_HEAD;
  for (int k = 0; k < 3; k++) {                           // D1C1 LDA $300580,X / D1C9 BMI: an empty party slot
    c += MEMBER_HEAD + ((sram[base + 0x580 + k] & 0x80) ? MEMBER_EMPTY : MEMBER_FULL) + MEMBER_TAIL;
    c += k < 2 ? 18 : 12;                                 // D213 BNE
  }
  int lv = sram[base + 0x59C] | (sram[base + 0x59D] << 8);
  c += D19F_MID + (lv < 0x3E8 ? 18 : 12 + 18);            // D222 BCC (taken), else D224 LDA #03E7
  int flag = sram[base + 0x793] & 0x80;
  c += flag ? 12 + 28 + 28 + 40 + 40 : 18;                // D255 BEQ; D257 LDY $61 .. D25E STA $0005,Y
  int chapter = sram[base + 0x603], x = 0x1A;             // D27E DEX / D27F CMP $FFD024,X / D283 BCC
  do { x--; } while (x > 0 && chapter < rb(rom, romLen, fo(0xFFD024) + x));
  long t = d296(rom, romLen, x);
  if (t < 0) return -1;
  return c + D19F_TAIL + D266_HEAD + (0x1A - x) * (12 + 30) + (0x1A - x - 1) * 18 + 12 + D266_TAIL + t;
}

// ---- C2/D2C4: the slot's header (string #0E + slot of $FF:D3FE) and its $C2:D329[slot] one-pixel shifts --------------
#define D2C4_HEAD (20 + 18 + 20 + 30 + 12 + 12 + 26 + 12 + 30 + 12 + 26 + 18 + 18 + 34 + 36 + 18 + 34 + 40)   // .. D2F0 JSR
#define D2C4_MID  (18 + 28 + 30 + 20)                     // D2F3 SEP .. D2FB STA $00
#define D306      (34 + 18 + 16 * (8 * 46 + 12 + 12) + 15 * 18 + 12 + 40)   // 16 columns x 8 read-modify-writes, RTS
#define D2C4_TAIL (26 + 40)

static long d2c4(const uint8_t* rom, long romLen, int slot) {
  int n = rb(rom, romLen, 0x2D329L + slot);
  long v = ct_menu_vwf_cycles(rom, romLen, 0xD3FE, 0x0E + slot);
  if (n <= 0 || v < 0) return -1;
  return D2C4_HEAD + v + D2C4_MID + n * (40 + D306 + 34) + (n - 1) * 18 + 12 + D2C4_TAIL;   // D2FD JSR / D300 DEC / BNE
}

// ---- C2/D156 -------------------------------------------------------------------------------------------------------------
#define D156_HEAD       (20 + 18 + 28 + 34 + 26 + 28 + 12 + 26)   // D156 PHP .. D165 STA $0D4C
#define SLOT_TOP        (28 + 32 + 54)                    // D168 LDX $79, D16A STZ $0D49,X, D16D JSL FF/F9C4
#define SLOT_EMPTY_PICK (20 + 12 + 12 + 26 + 40 + 18)     // D171 LDA $00, D173 BEQ (not taken), LDA #1A, STA $020C, JSR, BRA
#define SLOT_VALID_PICK (20 + 18 + 46 + 40)               // D171, D173 BEQ (taken), D17F INC $0D49,X, D182 JSR C2/D19F
#define SLOT_BOTTOM     (40 + 34 + 20 + 12)               // D185 JSR C2/D2C4, D188 INC $79, D18A LDA $79, D18C CMP #03
#define D156_TAIL       (18 + 788 + 26 + 40)              // D190 LDX #D198, D193 JSR C2/8385 (its record: 788), PLP, RTS

long ct_menu_slot_scan_cycles(const uint8_t* rom, long romLen, const uint8_t* sram) {
  static const uint8_t blank[0x2000];
  if (!sram) sram = blank;
  long c = D156_HEAD;
  for (int slot = 0; slot < 3; slot++) {
    c += SLOT_TOP + f9c4(sram, slot);
    long body = ct_menu_slot_verdict(sram, slot) == 0 ? d19f(rom, romLen, sram, 0x0A00L * slot)
                                                      : d296(rom, romLen, 0x1A);
    long hdr = d2c4(rom, romLen, slot);
    if (body < 0 || hdr < 0) return -1;
    c += (ct_menu_slot_verdict(sram, slot) == 0 ? SLOT_VALID_PICK : SLOT_EMPTY_PICK) + body;
    c += SLOT_BOTTOM + hdr + (slot < 2 ? 18 : 12);        // D18E BCC
  }
  return c + D156_TAIL;
}

// ---- the whole stretch ---------------------------------------------------------------------------------------------------
// Fixed legs, the ROM's own cost as traced over a menu open (tools/menu_init_legs.py, colsave3's): the DMA legs jitter by
// a few tens with the DMA sync phase, the posts by the APU's (see menu_cost.h).
#define L_0B4E_REST  (24 + 24 + 40)                       // 0B5D STA $4200, 0B60 STA, 0B63 RTS
#define L_0918       (18 + 18 + 18 + 18 + 20 + 0x14E0L * 46 + 26 + 18 + 40)   // C0/0918: the 0x14E0-byte MVN save
#define L_HEAD_POSTS 26072L                               // 8068 JSR C2/8096 .. 80EE: its MVN head + five sound posts, idle
#define L_940D       1018364L                             // 80EE REP .. 80F3: C2/940D (the VRAM clear DMA, the WRAM fills)
#define L_FILLS      2072L                                // 80F3 .. 8124
#define L_968D       1977700L                             // 8124 JSR C2/968D .. 8127: the menu graphics
#define L_984A       25242L
#define L_8663       10464L
#define L_92F4       1418L
#define L_F3CA       289874L
#define L_9875       831482L                              // C2/9875: NOT WALKED YET (party / bag: +-1,800)
#define L_HP         2630L
#define L_806B       236L

static long l_011b(const uint8_t* w, int caller) {
  long c = L_0B4E_REST + 40;                              // + the caller's JSR C0/011B
  c += 20 + (w[0x0129] ? 12 + 12 + 20 + 12 + 20 + 20 : 18);   // 011B LDA $29 / 011D BEQ
  c += 40 + L_0918 + 28 + 32 + 20 + 32 + 20 + 32 + 20 + 18;   // 0129 JSR C0/0918 .. 013D LDX #0000
  for (int k = 0; k < 3; k++) {                           // 0142 LDA $97,X / 0144 BMI: the three PC slots
    int present = !(w[0x0197 + 2 * k] & 0x80);
    c += 12 + 18 + 26 + (present ? 12 + 12 + 18 + 6 * 40 + 18 : 18) + 12 + 12 + 18 + (k < 2 ? 18 : 12);
  }
  c += 20 + 32 + 20 + 32 + 20 + 32 + 18 + 40 + 40 + 6 * 40 + 18 + 26 + 32 + 40;   // 0167 .. 01A4 RTS
  switch (caller) {                                       // the caller's code to its JSL C2:8000, JSL included
    case CT_MENU_CALLER_X:     return c + 12 + 18 + 54;   // C0/195E, C0/195F, C0/1960
    case CT_MENU_CALLER_C8_HI: return c + 12 + 18 + 20 + 12 + 18 + 18 + 18 + 12 + 18 + 20 + 4 * 12 + 54;   // .. 0D16
    // C0/19C7 `LDX #0000` (three FastROM fetches, 18) / `TDC` (12) / `XBA` (18) / `LDA #05` (12) / C0/19CE `JSL C2:8000` (54)
    case CT_MENU_CALLER_Y:     return c + 18 + 12 + 18 + 12 + 54;
    // C0/0CF0 `TDC` 12 / `XBA` 18 / `LDA $25` 20 / `BEQ` taken 18 / C0/0D04 `LDA #05` 12 / `BRL` 24 -- then the Y arm's five
    case CT_MENU_CALLER_C8_00: return c + 12 + 18 + 20 + 18 + 12 + 24 + 18 + 12 + 18 + 12 + 54;
    default:                   return c + 12 + 18 + 20 + 12 + 12 + 12 + 18 + 12 + 18 + 54;             // 0D00 arm
  }
}

static long l_816a(const uint8_t* w) {
  long c = 40 + 20 + 18 + 12 + 12 + 20 + 20 + 26 + 40;    // JSR, 816A PHP .. 8172 STX $75, 818E PLP, 818F RTS
  for (int x = 0; x < 9; x++) {
    c += 26;                                              // 8174 LDA $2980,X
    if (w[0x2980 + x] & 0x80) c += 18;                    // 8177 BMI (taken): an empty roster slot
    else c += 12 + 32 + 12 + 32 + 12 + 12 + (x < 3 ? 12 + 34 : 18) + 34;   // .. 8183 BCS / 8185 INC $73, 8187 INC $75
    c += 12 + 12 + (x < 8 ? 18 : 12);                     // 8189 INX, 818A CPX #09, 818C BCC
  }
  return c;
}

long ct_menu_open_stretch(const uint8_t* rom, long romLen, const uint8_t* wram, const uint8_t* sram, int mode,
                          int caller) {
  if (!rom || !wram) return -1;
  long scan = ct_menu_slot_scan_cycles(rom, romLen, sram);
  if (scan < 0) return -1;
  return l_011b(wram, caller) + 580 + (mode == 0 ? 44 : 0) + L_HEAD_POSTS + L_940D + L_FILLS + L_968D
       + 40 + scan + L_984A + L_8663 + l_816a(wram) + L_92F4 + L_F3CA + L_9875 + L_HP + L_806B;
}
