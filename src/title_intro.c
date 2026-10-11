// title_intro.c - the TITLE SEQUENCE's machine and its render (disasm/notes/title_intro.md).
//
// ⭐ THE SEQUENCE IS NOT A MOVIE, IT IS A MACHINE. `$7E:3023 JSR $3176` -- the sequence engine's
// main loop -- is a bare RTS; everything happens in the NMI, which runs two WRAM hook lists: eight
// VBLANK slots at $7E:0940 (dispatcher $39BE) and sixteen TASKS at $7E:0920/$0950 (dispatcher
// $398A, the list double-buffered by the NMI's own MVN). What installs and removes those tasks is a
// BYTE-CODE SCRIPT (C3/0B03) whose sixteen registers ARE the PPU shadow cells -- reg0 the mode-7
// scale, reg1 BG1HOFS, reg2 BG1VOFS, reg6 the ripple amplitude.
//
// So this file is that machine, transcribed: the interpreter, the two lists, each task's own body,
// the sprite-package drawers and the ROM's palette-job runner, plus a renderer that is LakeSnes'
// own PPU fetch for the two configurations the sequence uses (mode 7 for the swing, mode 2 with
// offset-per-tile for the logo).
//
// ⚠ Three render traps, each of which cost a wrong answer first (title_intro.md section 3):
// the scanline is 1-BASED (`ppu_runLine` runs lines 1..224 and it is `line` that enters the mode-7
// matrix); the oracle's 512x480 dump puts line y at buffer row y*2+16, i.e. IMAGE rows 8..231; and
// the state that renders frame N is the one the VBLANK HOOKS LATCHED at N, not the cells the tasks
// have already advanced -- which is why CtTitleIntroTick latches first and runs the tasks after.
#include <string.h>
#include <stdlib.h>
#include "title_intro.h"
#include "data/title_intro.h"
#include "data/seq_credits.h"

#define CT_M7_FIELD 1024                 // the mode-7 field is 128 tiles x 8 px on each axis
#define CT_TI_HOFS  0xFF40               // $210F/$210D's constant during the swing ($7E:5A94)
#define CT_TI_SEQ_ID 2                   // $7E:0384 -- this IS sequence 2 ($7E:3003 dispatches on it)

// ---- the overlay routines the machine dispatches on, by their $7E address ------------------------
enum { T_SCRIPT = 0x3749, T_PEND = 0x5BCF, T_MULSAVE = 0x37FA, T_WAVE = 0x5FFE, T_COPY = 0x5D32,
       T_START = 0x5B99, T_CLOCK = 0x5D01, T_SPIRAL = 0x5C98, T_LETTERS = 0x5D71, T_HANDS = 0x5D20,
       T_DAMP = 0x5BBF, T_SQUARE = 0x5E1E, T_EXIT = 0x34AB, T_FADE = 0x34C2,
       // entry 1's three, and $34E3 is the plain fade every stage's teardown uses
       T_FADEIN = 0x347A, T_CREDPAGE = 0x6BE5, T_FADEOUT = 0x34E3,
       // ⭐ $7E:3754 is $3749 with `LDA #$0B00 / TCD` instead of `#$0A00`: one dispatcher, the OTHER
       // register page. Entry 3 installs both ($0A20 and $0B20) and steps them as two separate
       // tasks -- sequence_table.md 8.1. ⚠ Disassembled with an 8-bit accumulator the two read
       // `LDA #$00 / ASL A / TCD`, which looks like "DP = 0" and is nonsense; the raw bytes
       // (`A9 00 0A 5B` and `A9 00 0B 5B`) settle it.
       T_SCRIPTB = 0x3754 };
enum { V_CGRAM = 0x388B, V_OAM = 0x380B, V_OAMCLR = 0x3843, V_M7 = 0x5C57, V_OFFS = 0x6212 };

static uint8_t ovlScript(uint16_t a) {
  a -= CT_TITLE_SCRIPT_BASE;
  return a < sizeof ctTitleScript ? ctTitleScript[a] : 0;
}
static uint8_t ovlPkg(uint16_t a) {
  a -= CT_TITLE_PKG_BASE;
  return a < sizeof ctTitlePkg ? ctTitlePkg[a] : 0;
}
static uint16_t scr16(uint16_t a) { return (uint16_t)(ovlScript(a) | (ovlScript(a + 1) << 8)); }

// ⭐ $7E:3465 (sine) / $7E:3461 (cosine) over the ROM's own 1024-byte table. The FIRST HALF is
// negated, and the `CPX #$0200` the routine ends on leaves a CARRY that its callers' next ADC picks
// up. ⚠ That term is carried here because the ROM's ADC reads it, not because this sequence needs
// it: the clock's spiral walks its angle $F6 -> $70, so the carry is 0 on all 68 of its frames and
// no frame of the title can grade it (title_intro.md 2.1).
static uint16_t ctSin(uint16_t a, int* carry, int* neg) {
  int idx = a & 0x3FF;
  uint16_t v = ctTitleSin[idx];
  *carry = idx >= 0x200;
  if (idx < 0x200) v = (uint16_t)(-(int)v);
  *neg = (v & 0x8000) != 0;
  return v;
}
static uint16_t ctCos(uint16_t a, int* carry, int* neg) { return ctSin((uint16_t)(a + 0x100), carry, neg); }

// ---- $7E:31BF / $7E:339F: the overlay's 16x16->32 multiply and 32/16 divide ----------------------
static uint16_t fw(const CtTitleIntro* t, int a) { return (uint16_t)(t->f[a] | (t->f[a + 1] << 8)); }
static void fws(CtTitleIntro* t, int a, uint16_t v) { t->f[a] = (uint8_t)v; t->f[a + 1] = (uint8_t)(v >> 8); }

// ⭐ $31BF is four hardware 8x8s -- and it zeroes $F6 (so $F7 too) and then ROLs the WORD at $F7,
// which rotates BIT 7 OF THE STALE $F8 into the middle ADC as a carry. $F8 is the quotient the
// driver's own divide left there, shifted one place left by every multiply since; model it and the
// whole swing computes, which is why this port no longer carries a captured matrix stream.
static void ctMul(CtTitleIntro* t) {
  uint16_t f0 = fw(t, 0), f2 = fw(t, 2);
  unsigned p0 = (f2 & 0xFF) * (f0 & 0xFF);
  unsigned p1 = (f2 & 0xFF) * (f0 >> 8);
  unsigned p2 = (f2 >> 8) * (f0 & 0xFF);
  unsigned p3 = (f2 >> 8) * (f0 >> 8);
  fws(t, 4, (uint16_t)p0);
  unsigned word = (unsigned)t->f[8] << 8;            // $F7 was just cleared by `STZ $F6`
  unsigned sum = p1 + p2;
  unsigned rol = ((word << 1) | (sum > 0xFFFF)) & 0xFFFF;
  int cout = (word >> 15) & 1;                       // ⭐ bit 7 of the old $F8
  t->f[7] = (uint8_t)rol;
  t->f[8] = (uint8_t)(rol >> 8);
  unsigned a = (sum & 0xFFFF) + fw(t, 5) + (unsigned)cout;
  fws(t, 5, (uint16_t)a);
  unsigned b = p3 + fw(t, 6) + (a > 0xFFFF);
  fws(t, 6, (uint16_t)b);
}

static void ctDiv(CtTitleIntro* t) {
  unsigned num = (unsigned)fw(t, 0) | ((unsigned)fw(t, 2) << 16);
  unsigned den = (unsigned)fw(t, 4) | ((unsigned)fw(t, 6) << 16);
  fws(t, 8, den ? (uint16_t)(num / den) : 0);
}

// A multiply as the tasks stage it: $F0 = a, $F2 = b, $F6 cleared, then $31BF; result word = $F5.
static uint16_t ctMulTo(CtTitleIntro* t, uint16_t a, uint16_t b) {
  fws(t, 0, a); fws(t, 2, b); fws(t, 6, 0);
  ctMul(t);
  return fw(t, 5);
}

// ---- C3/0D0E (8x8) and C3/0D5E (16x16): draw one sprite package into the OAM shadow --------------
// A package is [count+1][tile, attr, dx, dy] x n. Both drawers append at the $11 cursor and shift
// the high-table byte at $13 two bits per sprite: 0D0E shifts ZEROS in from the top (so every
// sprite it emits is 8x8 with x < 256), 0D5E rotates in the x-high bit and then a set bit (so every
// sprite it emits is 16x16). After four sprites the byte is full and $13 advances.
static void ctDrawPkg(CtTitleIntro* t, uint16_t addr, int large) {
  int n = ovlPkg(addr) - 1;
  uint16_t y = t->cell[CT_TI_C11], x = t->cell[CT_TI_C13];
  uint16_t p = (uint16_t)(addr + 1);
  for (int i = 0; i < n; i++, p += 4) {
    t->oam[(y + 2) & 0x1FF] = ovlPkg(p);
    t->oam[(y + 3) & 0x1FF] = ovlPkg(p + 1);
    int hi = 0x200 + ((x - 0x900) & 0x1F);
    if (large) {
      unsigned sx = (unsigned)(ovlPkg(p + 2) + t->cell[CT_TI_C15]) & 0xFFFF;
      if (sx >= 0x100) sx = 0x9801;                  // off screen: x = 257 via the high bit
      t->oam[y & 0x1FF] = (uint8_t)sx;
      int cin = (sx >> 15) & 1;                      // `ASL A` -> carry = bit 15
      uint8_t b = t->oam[hi];
      b = (uint8_t)((b >> 1) | (cin << 7));          // ROR A with that carry  -> x-high
      b = (uint8_t)((b >> 1) | 0x80);                // SEC / ROR A            -> size = 16x16
      t->oam[hi] = b;
      t->oam[(y + 1) & 0x1FF] = (uint8_t)(ovlPkg(p + 3) + t->cell[CT_TI_C17]);
    } else {
      t->oam[y & 0x1FF] = (uint8_t)(ovlPkg(p + 2) + t->cell[CT_TI_C15]);
      t->oam[(y + 1) & 0x1FF] = (uint8_t)(ovlPkg(p + 3) + t->cell[CT_TI_C17]);
      if (t->oam[hi]) t->oam[hi] >>= 2;
    }
    y = (uint16_t)(y + 4);
    if ((y & 0x0F) == 0) x++;
  }
  t->cell[CT_TI_C11] = y;
  t->cell[CT_TI_C13] = x;
}

// ---- the task/hook lists ($7E:395E installs, $39A7 removes) --------------------------------------
static void listAdd(uint16_t* l, int n, uint16_t v) {
  for (int i = 0; i < n; i++) if (!l[i]) { l[i] = v; return; }
}
static void listDel(uint16_t* l, int n, uint16_t v) {
  for (int i = 0; i < n; i++) if (l[i] == v) { l[i] = 0; return; }
}
// $7E:39B1 -- "replace me with X": the same pops as the delete exit, then `STA $0920,X` with the
// dispatcher's own cursor, so the new task takes over the SLOT the old one was running in.
static void listSwap(uint16_t* l, int n, uint16_t from, uint16_t to) {
  for (int i = 0; i < n; i++) if (l[i] == from) { l[i] = to; return; }
}

// ---- the palette jobs: the ROM's twelve 12-byte records at $7E:0520, run by FD/E39C --------------
// +0 kind (high nibble) | +1 first CGRAM index | +2 count | +3 step | +4 tick | +5 period
// +6 blend mode | +7 level | +8 target. The level walks toward the target by `step` every `period`
// frames; the frame it would pass the target the record COPIES THE BASE PALETTE BACK and frees
// itself (FD/E624 / FD/E8AC) -- which is why the settled screen is the untouched palette, not the
// last blended one.
static void ctPalRestore(CtTitleIntro* t, const uint8_t* r) {
  for (int i = 0; i < r[2]; i++) t->palShadow[(r[1] + i) & 0xFF] = ctTitlePal0[(r[1] + i) & 0xFF];
}

static void ctPalBlend(CtTitleIntro* t, const uint8_t* r, int kind) {
  int lvl = r[7];
  for (int i = 0; i < r[2]; i++) {
    uint16_t c = ctTitlePal0[(r[1] + i) & 0xFF];
    int rd = c & 31, gn = (c >> 5) & 31, bl = (c >> 10) & 31, r2, g2, b2;
    if (kind == 5) {                                 // FD/E64F -- scale every component by level/256
      r2 = (lvl * rd) >> 8; g2 = (lvl * gn) >> 8; b2 = (lvl * bl) >> 8;
    } else {                                         // FD/EA5C -- blend R and G toward 31; B := 0
      r2 = (rd + ((lvl * (31 - rd)) >> 8)) & 31;
      g2 = ((gn * 8 + ((lvl * (248 - gn * 8)) >> 8)) & 0xF8) >> 3;
      b2 = 0;
    }
    t->palShadow[(r[1] + i) & 0xFF] = (uint16_t)((b2 << 10) | (g2 << 5) | r2);
  }
}

static void ctJobsRun(CtTitleIntro* t) {
  for (int j = 0; j < 12; j++) {
    uint8_t* r = t->job[j];
    if (!r[0]) continue;
    r[4]++;
    if (r[4] != r[5]) continue;
    r[4] = 0;
    int lvl = r[7], step = r[3], tgt = r[8], n;
    if (lvl < tgt) {
      n = lvl + step;
      if (n > 0xFF) { ctPalRestore(t, r); r[0] = 0; continue; }
      if (n >= tgt) { r[0] = 0; n = tgt; }
    } else {
      n = lvl - step;
      if (n < 0) { ctPalRestore(t, r); r[0] = 0; continue; }
      if (n < tgt) { r[0] = 0; n = tgt; }            // BCS keeps the job alive at n == target
    }
    r[7] = (uint8_t)n;
    ctPalBlend(t, r, r[0] >> 4);
  }
}

// ---- the tasks -----------------------------------------------------------------------------------
// $7E:5BCF -- the mode-7 pendulum. angle += 7, amplitude -= 1 (floored at 1), theta = $200 +
// (cos(angle)*amplitude >> 18), and the matrix is cos/sin(theta) * the script's scale. BG1VOFS is
// 65536/scale - 256.
static void ctTaskPendulum(CtTitleIntro* t) {
  int c, neg;
  t->cell[CT_TI_C66] = (uint16_t)(t->cell[CT_TI_C66] + 7);
  uint16_t v = ctCos(t->cell[CT_TI_C66], &c, &neg);
  uint16_t a = ctMulTo(t, neg ? (uint16_t)-(int)v : v, t->cell[CT_TI_C6C]);
  a >>= 2;
  if (neg) a = (uint16_t)-(int)a;
  t->cell[CT_TI_CFE] = (uint16_t)(a + 0x200);        // an explicit CLC precedes this ADC

  uint16_t v2 = ctCos(t->cell[CT_TI_CFE], &c, &neg);
  uint16_t ma = (uint16_t)(ctMulTo(t, v2, t->scr[0].reg[0]) + 1);
  t->cell[CT_TI_C5A] = t->cell[CT_TI_C60] = ma;

  uint16_t v3 = ctSin(t->cell[CT_TI_CFE], &c, &neg);
  uint16_t mb = ctMulTo(t, neg ? (uint16_t)-(int)v3 : v3, t->scr[0].reg[0]);
  if (neg) mb = (uint16_t)-(int)mb;
  t->cell[CT_TI_C5C] = mb;
  t->cell[CT_TI_C5E] = (uint16_t)-(int)mb;

  fws(t, 0, 0); fws(t, 6, 0); fws(t, 2, 1); fws(t, 4, t->scr[0].reg[0]);
  ctDiv(t);
  t->scr[0].reg[2] = (uint16_t)(fw(t, 8) - 0x100);
  t->cell[CT_TI_C6C] = t->cell[CT_TI_C6C] - 1 ? (uint16_t)(t->cell[CT_TI_C6C] - 1) : 1;
}

// $7E:5FFE -- the OFFSET-PER-TILE ripple. The 32 words at $0C40 are BG3's second tilemap row, which
// mode 2 reads as a per-COLUMN vertical offset for BG1; the table shifts one column per frame and a
// fresh sin(frame * 16) * reg6 enters at the left, so the wave travels across the TRIGGER logo and
// dies out as the damping task counts reg6 down to zero.
static void ctTaskWave(CtTitleIntro* t) {
  memmove(t->waveTail + 1, t->waveTail, sizeof t->waveTail - sizeof t->waveTail[0]);
  t->waveTail[0] = t->wave[0x3F];                    // (host mod: what falls off the right end -- see waveTail)
  for (int i = 0x3E; i >= 0x20; i--) t->wave[i + 1] = t->wave[i];
  int c, neg;
  uint16_t v = ctSin((uint16_t)(t->cell[CT_TI_C08] << 4), &c, &neg);
  uint16_t a = ctMulTo(t, neg ? (uint16_t)-(int)v : v, t->scr[0].reg[6]);
  if (neg) a = (uint16_t)-(int)a;
  a = (uint16_t)(a - 0x4A);                          // an explicit SEC precedes this SBC
  t->wave[0x20] = (uint16_t)((a & 0x3FF) | 0x2000);  // bit 13 = "this offset is BG1's"
}

// $7E:5C98 -- the clock's spiral. It walks an angle down from $F8 in steps of 2 and puts the clock
// at (cos*216 - 32, sin*195 - 64); when the angle passes $70 it hands the screen to mode 2's layer
// set, arms the second half of the script and parks the clock at (56, 58).
static void ctTaskSpiral(CtTitleIntro* t) {
  uint16_t a = (uint16_t)(t->cell[CT_TI_C68] - 2);
  if ((int16_t)a < 0x70) {
    t->tm = 0x13; t->ts = 0x00;                      // one 16-bit store to $212C
    t->scr[0].ip = 0x5B51;
    t->cell[CT_TI_C62] = 0x38;
    t->cell[CT_TI_C64] = 0x3A;
    listDel(t->task, 16, T_SPIRAL);
    return;
  }
  t->cell[CT_TI_C68] = a;
  int c, neg;
  uint16_t v = ctCos(a, &c, &neg);
  uint16_t x = ctMulTo(t, neg ? (uint16_t)-(int)v : v, 0xD8);
  if (neg) x = (uint16_t)-(int)x;
  t->cell[CT_TI_C62] = (uint16_t)(x + 0xFFE0 + c);   // the sine's own CPX carry; always 0 here
  v = ctSin(a, &c, &neg);
  uint16_t y = ctMulTo(t, neg ? (uint16_t)-(int)v : v, 0xC3);
  t->cell[CT_TI_C64] = (uint16_t)(y + 0xFFC0 + c);
}

static void ctTaskClock(CtTitleIntro* t) {           // $7E:5D01 -- the clock face, in two halves
  t->cell[CT_TI_C15] = t->cell[CT_TI_C62];
  t->cell[CT_TI_C17] = t->cell[CT_TI_C64];
  ctDrawPkg(t, 0x603F, 0);
  t->cell[CT_TI_C17] = (uint16_t)(t->cell[CT_TI_C64] + 0x40);
  ctDrawPkg(t, 0x60BC, 0);
}

static void ctTaskHands(CtTitleIntro* t) {           // $7E:5D20 -- the clock hands
  t->cell[CT_TI_C17] = 0x64;
  t->cell[CT_TI_C15] = 0x3A;
  ctDrawPkg(t, 0x61B4, 0);
}

// $7E:5D32 -- the copyright block: fourteen 16x16 sprites, and the sequence's own opening timer.
// It counts $0072 (seeded 110 by the script) down, installs the START handler when the count
// reaches 60, and deletes itself at zero -- which is why the block is on screen for exactly the
// first 110 frames and vanishes at 110 with nothing else changing.
static void ctTaskCopyright(CtTitleIntro* t) {
  t->cell[CT_TI_C15] = 0x90;
  t->cell[CT_TI_C17] = 0xB0;
  t->anchorLo = (uint8_t)((t->cell[CT_TI_C11] & 0x1FF) >> 2);
  ctDrawPkg(t, 0x61D9, 1);
  t->anchorHi = (uint8_t)(((t->cell[CT_TI_C11] - 1) & 0x1FF) >> 2) + 1;   // (host mod: the slots it drew)
  t->cell[CT_TI_C72]--;
  if (!t->cell[CT_TI_C72]) { listDel(t->task, 16, T_COPY); return; }
  if (t->cell[CT_TI_C72] == 0x3C) listAdd(t->task, 16, T_START);
}

// $7E:5D71 -- the five CHRONO letters, each sliding right at its own speed until it reaches its own
// target ($0046 current / $0050 target, 1/16 px).
static void ctTaskLetters(CtTitleIntro* t) {
  t->cell[CT_TI_C17] = 0x52;
  for (int i = 0; i < 5; i++) {
    uint16_t v = (uint16_t)(t->slide[i] + ctTitleSlideSpd[i]);
    if (v >= t->tgt[i]) v = t->tgt[i];
    t->slide[i] = v;
    t->cell[CT_TI_C15] = (uint16_t)((v >> 4) & 0xFF);
    ctDrawPkg(t, ctTitleSlidePkg[i], 0);
  }
}

static void ctTaskSquare(CtTitleIntro* t) {          // $7E:5E1E -- "© 1995 SQUARE"
  t->cell[CT_TI_C17] = 0xD0;
  t->cell[CT_TI_C15] = 0x78;
  ctDrawPkg(t, 0x6186, 0);
  t->cell[CT_TI_C15] = 0xB8;
  ctDrawPkg(t, 0x61A7, 0);
}

// $7E:5BBF -- the ripple's damping: every 32 frames reg6 loses one, and the task deletes itself
// when reg6 hits zero, leaving the logo still.
static void ctTaskDamp(CtTitleIntro* t) {
  if (t->cell[CT_TI_C08] & 0x1F) return;
  t->scr[0].reg[6]--;
  if (!t->scr[0].reg[6]) listDel(t->task, 16, T_DAMP);
}

// $7E:5B99 -- START. It is not the host's decision and it is not "the START button": the task
// leaves on ANY button ($0002 != 0), latches $0070 so it can only fire once, installs the FADE and
// then reads the pad a second time -- `BIT #$30`, i.e. L or R -- to decide whether to leave
// $7F0060 behind. Without that flag the reboot falls through scene 433's test into the attract
// demo instead of the game, which is what holding a shoulder button at the title does.
// $006E (the script's own handover near the end) makes the whole task a no-op.
static void ctTaskStart(CtTitleIntro* t) {
  if (t->cell[CT_TI_C6E] || t->cell[CT_TI_C70]) return;
  if (!t->joy) return;
  t->cell[CT_TI_C70] = (uint16_t)((t->cell[CT_TI_C70] & 0xFF00) | ((t->cell[CT_TI_C70] + 1) & 0xFF));
  listAdd(t->task, 16, T_EXIT);
  if (!(t->joy & 0x30)) t->exitFlag = 1;
  listDel(t->task, 16, T_START);
}

// $7E:34AB -- the exit. It asks the sound driver to fade the music ($1E00 = $81, $1E01 = $0040) and
// then REPLACES ITSELF in its own slot with $34C2, the brightness ramp.
static void ctSndMail(CtTitleIntro* t, int off, uint8_t v) {
  if (t->snd.mail) t->snd.mail(t->snd.ctx, off, v);
}
static void ctSndPost(CtTitleIntro* t) {
  if (t->snd.post) t->snd.post(t->snd.ctx);
}

static void ctTaskExit(CtTitleIntro* t) {
  // `LDX #$0040 / STX $1E01` is a 16-BIT store, so it writes $1E01 = $40 AND $1E02 = $00; $1E03 is
  // not touched and goes out to $2143 with whatever the last command left in it.
  ctSndMail(t, 1, 0x40);
  ctSndMail(t, 2, 0x00);
  ctSndMail(t, 0, 0x81);
  ctSndPost(t);
  listSwap(t->task, 16, T_EXIT, T_FADE);
}

// $7E:34C2 -- the fade-out itself, and the only thing that ends this sequence. Every $038E frames
// the brightness shadow $038D loses one; the NMI pushes it to $2100 (or $80 -- forced blank -- once
// it is 0), so the whole screen dims 15 -> 0 while every other task keeps drawing. At zero it asks
// the driver to stop the music ($F0), sets $7E:0000 and deletes itself: sixteen frames from the
// button to the reboot, which is exactly the window boot_loader.md §3.2 measured.
static void ctTaskFade(CtTitleIntro* t) {
  if (--t->briCnt) return;
  t->briCnt = t->briPeriod;
  if (--t->bri) return;
  ctSndMail(t, 0, 0xF0);                             // $34D5: the command byte ALONE
  ctSndPost(t);
  t->done = 1;
  listDel(t->task, 16, T_FADE);
}

// ---- SEQUENCE-TABLE ENTRY 1: the STAFF CREDITS ($7E:6B04) ----------------------------------------
// disasm/notes/sequence_table.md section 6. Three tasks and a setup, on the engine above -- entry 1
// installs NO vblank hook and runs NO script ($0940 and $0A20 are empty, measured), so the whole
// stage is the two fades, a 90-frame hold, and what the setup puts in VRAM and OAM.

// $7E:347A -- the brightness fade IN. The same four instructions as the title's $34C2 with the walk
// reversed and no sound: it counts $038F down, reloads it from $038E, steps $038D and leaves
// through $39A7 (delete self) the frame the brightness reaches $0F.
static void ctTaskFadeIn(CtTitleIntro* t) {
  if (--t->briCnt) return;
  t->briCnt = t->briPeriod;
  if (++t->bri != 0x0F) return;
  listDel(t->task, 16, T_FADEIN);
}

// $7E:34E3 -- the plain fade OUT: the title's $34C2 without the two sound commands. `INC $00` is the
// sequence's DONE flag, which is what the main loop at $7E:3026 polls. ⭐ This is the teardown
// EVERY entry shares; the title's is the special one, because only the title has to stop the music.
// ...and the frame it ends on is BLACK, not the level-1 the NMI has just latched: the task runs inside the NMI (v 228 on the
// logical oracle's credits, `--cputrace` with CPUTRACE_BEAM), the main loop at $7E:3026 sees $00 on the NMI's return and
// jumps to C0:0000 (v 231), whose first call C0/0B4E stores `$2100 = 80` -- all before line 0. MEASURED (the rule's
// cartridge, the credits before 0A5, --monitor 7E038D:3,7E0000:1): $038D 02 at c5587, 01 at c5588, 00 with $00 = 01 at
// c5589, and c5588's frame shows level 2, c5589's black; the port showed the 1 for a frame at the end of all four pages
// (build/_divergence/audit_2026-09-28.md item 4).
static void ctTaskFadeOut(CtTitleIntro* t) {
  if (--t->briCnt) return;
  t->briCnt = t->briPeriod;
  if (--t->bri) return;
  t->done = 1;
  t->l.bri = 0x80;                                   // C0/0B4E `LDA #80 / STA $2100`, in this same vblank
  listDel(t->task, 16, T_FADEOUT);
}

// $7E:6BE5 -- the page's own clock. ⚠ `LDA $08 / CMP #$005A` is SIXTEEN bits (the task dispatcher
// enters with `REP #20`, and the NMI's own counter is `REP #20 / INC $08`), and the common init
// clears $0000..$00FF, so the hold is exactly 90 frames from the setup. It leaves through $39B1,
// not $39A7: the fade takes over the SLOT this task was running in.
// ⚠ The two stores are the OAM shadow's cursor ($11) and pointer ($13 = $0900), which the title's
// vblank hook resets instead. Entry 1 draws no sprite package, so nothing in this stage reads them
// and no frame of it can grade them -- they are here because that is what the task does.
static void ctTaskCreditsPage(CtTitleIntro* t) {
  t->cell[CT_TI_C11] = 0;
  t->cell[CT_TI_C13] = 0x0900;
  if (t->cell[CT_TI_C08] != 0x005A) return;
  listSwap(t->task, 16, T_CREDPAGE, T_FADEOUT);
}

static uint8_t ctCredByte(uint16_t a) {
  a = (uint16_t)(a - CT_CRED_TEXT_BASE);
  return a < sizeof ctCredText ? ctCredText[a] : 0xFF;
}

// $7E:6C4D -- the BG1 tilemap writer. It runs with DP = $2100, so its "$16/$17" are VMADDL/VMADDH,
// its "$18/$19" are VMDATAL/VMDATAH and its "$80" is WMDATA: the text is streamed out of WRAM
// through the port the setup pointed at the page. A record is a tilemap WORD ADDRESS and then a
// NUL-terminated string; $FF ends the stream. The tile is `char - $40`, and SPACE ($20) is remapped
// to $40 first so it lands on tile 0 -- which is why the font starts at '@'.
static void ctCredBgText(CtTitleIntro* t, uint16_t a) {
  for (;;) {
    uint8_t lo = ctCredByte(a++);
    if (lo == 0xFF) return;
    int adr = lo | (ctCredByte(a++) << 8);
    for (;;) {
      uint8_t c = ctCredByte(a++);
      if (!c) break;
      if (c == 0x20) c = 0x40;
      t->vram[adr++ & 0x7FFF] = (uint8_t)(c - 0x40);  // `STA $18 / STZ $19`: the high byte is 0
    }
  }
}

// $7E:6C71 -- the same shape of stream, drawn as SPRITES. ⚠ Its "$0015/$0017" are ABSOLUTE (real
// WRAM), not the page-$2100 registers the other decoder uses: they are the pen's x and y. The tile
// is `char - $21`, the attribute byte is zero, a SPACE draws nothing, and the pen advances FIVE
// pixels per character either way -- an 8x8 font kerned by overlapping its cells.
// ⚠ It writes through $2104, i.e. the PPU's OWN OAM -- not the $7E:0700 shadow the title's tasks
// draw into. Entry 1 installs no vblank hook, so nothing would ever DMA that shadow: what the setup
// puts in OAM here is what every frame of the page renders.
static void ctCredOamText(CtTitleIntro* t, uint16_t a) {
  int slot = 0;                                      // $2102 was zeroed: OAMDATA fills from sprite 0
  for (;;) {
    uint8_t x = ctCredByte(a++);
    if (x == 0xFF) return;
    uint8_t y = ctCredByte(a++);
    for (;;) {
      uint8_t c = ctCredByte(a++);
      if (!c) break;
      if (c != 0x20 && slot < 128) {
        t->l.oam[slot * 4 + 0] = x;
        t->l.oam[slot * 4 + 1] = y;
        t->l.oam[slot * 4 + 2] = (uint8_t)(c - 0x21);
        t->l.oam[slot * 4 + 3] = 0;
        slot++;
      }
      x = (uint8_t)(x + 5);                          // `LDA $0015 / CLC / ADC #05` -- eight bits
    }
  }
}

static void ctSetupCredits(CtTitleIntro* t) {        // $7E:6B04
  // $6BF9: four greys, into BOTH halves of the palette shadow -- $7E:2200 is the BG's and $7E:2300
  // the OBJ's, and $7E:388B DMAs all 512 bytes of it to CGRAM once the setup returns. ⚠ Only these
  // eight entries can ever be read: every tilemap word this stage writes carries palette 0, and
  // every sprite attribute byte is 0.
  static const uint16_t grey[4] = { 0x0000, 0x77BD, 0x4631, 0x1CE7 };
  for (int i = 0; i < 4; i++) { t->palShadow[i] = grey[i]; t->palShadow[0x80 + i] = grey[i]; }
  // The fonts: $6BF9 DMAs a 512-byte packet (ROM index $AE through $7E:3691 -> C3/0557) to VRAM
  // word $0000, and $66B0 expands a 2bpp source at $7F:0300 into word $0100, padding each tile's
  // upper two planes with zeroes so the sprites can use the same glyphs at 4bpp. Both are the
  // BOOT's work, so they are captured (tools/gen/seq_credits.py), like the title's chr.
  memcpy(t->vram, ctCredFont, sizeof ctCredFont);
  // $6B1F: a FIXED-source DMA of $0800 bytes out of $D2:0000 (which is zero) -- exactly the 32x32
  // BG1 tilemap, cleared. Then the same trick twice on OAM: 32 bytes of $00 over the high table,
  // and 512 of the $E0 at $7E:4565 over the low one, which parks every sprite off screen.
  memset(&t->vram[0x0400], 0, 0x0400 * sizeof t->vram[0]);
  memset(t->l.oam, 0xE0, 0x200);
  memset(t->l.oam + 0x200, 0x00, 0x20);
  // $6B8A: `LDA $0382 / INC A / CMP #6 / BNE / LDA #1`. ⭐ The common init does NOT clear $0382, so
  // the counter survives the overlay's reboot and each of the attract's five visits shows the next
  // page (measured: 1 at boot f2787, then 2, 3, 4, 5).
  int page = t->page + 1;
  if (page == 6) page = 1;
  t->page = (uint8_t)page;
  ctCredBgText(t, ctCredBgPtr[page - 1]);
  ctCredOamText(t, ctCredOamPtr[page - 1]);
  t->tm = 0x11;                                      // $212C: BG1 + OBJ
  t->bg1sc = 0x04;                                   // $2107: the map at word $0400, 32x32
  t->obsel = 0x00;                                   // $2101: OBJ tiles at word $0000, 8x8/16x16
  // $6BD2: `LDX #0101 / STX $038E` -- period 1, counter 1, so both fades step every frame.
  t->briPeriod = 1;
  t->briCnt = 1;
  listAdd(t->task, 16, T_FADEIN);
  listAdd(t->task, 16, T_CREDPAGE);
}

// ---- the $C3 script interpreter (C3/0B03), DP = $0A00 --------------------------------------------
// Assignments (bit 7 clear): 0R/1R take a 16-bit immediate (set / add), 2R/3R take a register index
// byte. Commands (bit 7 set) dispatch on the LOW nibble through C3/0B74, except high nibbles $80
// and $90, which are the loop-counter opcode (run / yield) at C3/0C92.
static void ctScriptCall(CtTitleIntro* t, int pg, uint16_t a) {
  static const uint8_t hands[9] = { 0x40, 0xB0, 0x0B, 0x02, 0x00, 0x01, 0x03, 0x80, 0x00 };
  static const uint8_t fade[9]  = { 0x50, 0xC0, 0x10, 0x04, 0x00, 0x01, 0x00, 0x00, 0xFF };
  if (a == 0x5DA7) {                                 // ⭐ $7E:5DA7: BG MODE 2, and BG1VOFS = $FFB9
    t->mode = 2;
    t->scr[pg].reg[2] = 0xFFB9;
  } else if (a == 0x5DE9) {                          // the clock hands' white flash (kind 4, mode 3)
    memcpy(t->job[0], hands, sizeof hands);
  } else if (a == 0x5DB4) {                          // "© 1995 SQUARE" fading up (kind 5, mode 0)
    memcpy(t->job[0], fade, sizeof fade);
  }
}

// ⭐ `JSL $C3:0AFF` with DP = $0A00 ($7E:3749, task T_SCRIPT) or $0B00 ($7E:3754, T_SCRIPTB).
// One dispatcher, two register pages -- sequence_table.md 8.1.
static void ctScriptRun(CtTitleIntro* t, int pg) {
  for (int guard = 0; guard < 4096; guard++) {
    uint8_t op = ovlScript(t->scr[pg].ip++);
    if (!(op & 0x80)) {
      int r = op & 0x0F, hi = op & 0xF0;
      if (hi & 0x20) {
        uint16_t v = t->scr[pg].reg[(ovlScript(t->scr[pg].ip++) & 0x1E) >> 1];
        t->scr[pg].reg[r] = (hi == 0x20) ? v : (uint16_t)(t->scr[pg].reg[r] + v);
      } else {
        uint16_t v = scr16(t->scr[pg].ip);
        t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 2);
        t->scr[pg].reg[r] = (hi == 0x00) ? v : (uint16_t)(t->scr[pg].reg[r] + v);
      }
      continue;
    }
    int hi = op & 0xF0, n = op & 0x0F;
    if (hi == 0x80 || hi == 0x90) {
      uint16_t lim = scr16(t->scr[pg].ip), tgt = scr16(t->scr[pg].ip + 2);
      if (pg == 0 && !t->scr[pg].ctr[n] && lim) {    // (host mod: a slide on BG1's scroll -- see slideFrom)
        uint16_t a = tgt, end = (uint16_t)(t->scr[pg].ip - 1); int add = 0, ok = 1;
        while (a < end && ok) {
          uint8_t b = ovlScript(a);
          if (b & 0x80) ok = 0;
          else if (b & 0x20) a = (uint16_t)(a + 2);
          else { if ((b & 0xF0) == 0x10 && (b & 0x0F) == 1) add += (int16_t)scr16((uint16_t)(a + 1)); else if ((b & 0x0F) == 1) ok = 0;
                 a = (uint16_t)(a + 3); }
        }
        if (ok && add) {
          t->slideFrom = (int16_t)(t->scr[pg].reg[1] - add);
          t->slideEnd = (int16_t)(t->scr[pg].reg[1] + add * lim); t->slideOn = 1;
        }
      }
      if (t->scr[pg].ctr[n] && t->scr[pg].ctr[n] >= lim) {
        t->scr[pg].ctr[n] = 0;
        t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 4);
      } else {
        t->scr[pg].ctr[n]++;
        t->scr[pg].ip = tgt;
      }
      if (hi == 0x90) return;                        // $9n yields every iteration: a frame wait
      continue;
    }
    switch (n) {
      case 0x0: ctScriptCall(t, pg, scr16(t->scr[pg].ip)); t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 2); break;
      case 0x1: {                                       // byte poke into bank 0
        uint16_t a = scr16(t->scr[pg].ip);
        uint8_t v = ovlScript((uint16_t)(t->scr[pg].ip + 2));
        // ⭐ `F1 006E 01` is the sequence's own handover: $006E != 0 makes the START handler a
        // no-op, and the very next opcode installs $34AB -- so from here the fade runs whether or
        // not anyone pressed anything. It does NOT end the sequence; $34C2 does, fifteen frames of
        // brightness later.
        if (a == 0x006E) t->cell[CT_TI_C6E] = v;
        t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 3);
        break;
      }
      case 0x2: {                                       // word poke into bank 0
        uint16_t a = scr16(t->scr[pg].ip), v = scr16((uint16_t)(t->scr[pg].ip + 2));
        if (a == 0x0072) t->cell[CT_TI_C72] = v;
        t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 4);
        break;
      }
      case 0x3: t->scr[pg].ret = (uint16_t)(t->scr[pg].ip + 2); t->scr[pg].ip = scr16(t->scr[pg].ip); break;
      case 0x4: t->scr[pg].ip = t->scr[pg].ret; break;
      case 0x5: t->scr[pg].ip++; break;                         // wait on the APU's $2142 handshake
      case 0x6: t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 4); break;   // sound command
      case 0x7: listDel(t->vbl, 8, scr16(t->scr[pg].ip));  t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 2); break;
      case 0x8: listAdd(t->vbl, 8, scr16(t->scr[pg].ip));  t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 2); break;
      case 0x9: listDel(t->task, 16, scr16(t->scr[pg].ip)); t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 2); break;
      case 0xA: listAdd(t->task, 16, scr16(t->scr[pg].ip)); t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 2); break;
      case 0xB: t->scr[pg].ip = (uint16_t)(t->scr[pg].ip + 2); break;   // a direct $21xx write
      case 0xC: return;                                 // YIELD
      case 0xD: t->scr[pg].ip = scr16(t->scr[pg].ip); return;           // JUMP + yield
      case 0xE: t->scr[pg].ip = scr16(t->scr[pg].ip); break;            // JUMP
      default:  t->scr[pg].ip--; t->done = 1; return;           // END: $7E:0000 = 1
    }
  }
}


// ---- one NMI -------------------------------------------------------------------------------------
void CtTitleIntroReset(CtTitleIntro* t) {
  if (!t) return;
  uint8_t page = t->page;                            // $0382 outlives every stage, this one included
  CtTitleSoundSink snd = t->snd;                     // ...and the host's mailbox attachment is not machine state:
  memset(t, 0, sizeof *t);                           //    the title is re-opened by every `FF 02`, not only at boot
  t->page = page;
  t->snd = snd;
  t->frame = 0;
  memcpy(t->scr[0].reg, ctTitleSeedReg, sizeof t->scr[0].reg);
  memcpy(t->scr[0].ctr, ctTitleSeedCtr, sizeof t->scr[0].ctr);
  t->scr[0].ip = CT_TITLE_SEED_IP;
  t->scr[0].ret = CT_TITLE_SEED_RET;
  memcpy(t->task, ctTitleSeedTask, sizeof t->task);
  memcpy(t->vbl, ctTitleSeedVbl, sizeof t->vbl);
  memcpy(t->cell, ctTitleSeedCell, sizeof t->cell);
  memcpy(t->slide, ctTitleSeedSlide, sizeof t->slide);
  memcpy(t->tgt, ctTitleSeedTgt, sizeof t->tgt);
  memcpy(t->wave, ctTitleSeedWave, sizeof t->wave);
  memcpy(t->oam, ctTitleSeedOam, sizeof t->oam);
  memcpy(t->f, ctTitleSeedScratch, sizeof t->f);
  memcpy(t->palShadow, ctTitleSeedPalShadow, sizeof t->palShadow);
  t->mode = 7; t->tm = 0x01; t->ts = 0x10;
  // The title's own layer registers, measured on the oracle (`--ppuregs`: map=6400/6000 chr=5000
  // both, objTileAdr1=4000 objTileAdr2=5000). They were the constants the render used to carry.
  t->bg1sc = 0x65; t->bg2sc = 0x60; t->bg12nba = 0x55; t->obsel = 0x02; t->cgadsub = 0x21;
  // CGWSEL = $02: no clipping, no prevent-math, and bit 1 = ADD THE SUBSCREEN rather than the fixed
  // colour. It used to be implicit -- the render always fetched the subscreen when math was on.
  t->addSub = 1;
  t->stage = 2;
  memcpy(&t->vram[CT_TITLE_VRAM_BASE], ctTitleVram, sizeof ctTitleVram);
  t->sndBusy = CT_TITLE_SEED_SNDBUSY;
  t->sndCur = CT_TITLE_SEED_SNDCUR;
  t->sndReq = CT_TITLE_SEED_SNDREQ;
  t->bri = CT_TITLE_SEED_BRI;
  t->briPeriod = CT_TITLE_SEED_BRIPER;
  t->briCnt = CT_TITLE_SEED_BRICNT;
  t->frame = -1;                                     // the first tick produces frame 0
}

// ⭐ $7E:3000: `JSR $3063` (the common init) and then `LDA $0384 / AND #$1F / ASL A / TAX /
// JSR ($3043,X)`. The table is sixteen words but the mask allows THIRTY-TWO indices, so 16..31 read
// the init's own code as pointers -- see disasm/notes/sequence_table.md section 1 before porting
// anything that gets there.
//
// The common init ($3063) is the part every entry shares: it clears $0000..$00FF and $0900..$1DFF
// with a fixed-source DMA out of MPYL (measured 0 at this entry, which is what makes the credits'
// 90-frame hold exact), zeroes the brightness and the sound shadows, and resets the PPU from
// $7E:3102 with DP = $2100 -- BGMODE 0, BG12NBA 0, OBSEL $02, every scroll and both colour-math
// registers 0. ⚠ $0382 is NOT in the list, which is how the credits' page counter survives.
// $7E:3043 itself: the table of setups, as far as it is derived. ⚠ The ROM's table has sixteen
// words behind eleven distinct routines and the mask reaches THIRTY-TWO indices; what is missing
// here is missing because it has not been derived, not because the ROM has nothing there.
typedef void (*CtSeqSetup)(CtTitleIntro*);
static CtSeqSetup ctSeqEntry(int entry) {
  switch (entry) {
    case 1:  return ctSetupCredits;                  // $7E:6B04 -- the staff credits
    default: return 0;                               // not ported: sequence_table.md section 5
  }
}
// ...and each setup's price in master cycles, run with the NMI still off (C3/0014 `LDA #01 / STA $4200`).
long CtTitleIntroStageSetupCycles(int stage) {
  switch (stage & 0x1F) {
    // $7E:6B04: `CT_CALLROM_NONMI=1 ct.exe --callrom 7E6B04 --from <the attract at f2337, overlay decompressed>
    // --rega 0061` -> 10620 ops, 276020 cycles of which 8080 are DRAM refresh.
    case 1:  return 267940;
    default: return 0;
  }
}

int CtTitleIntroResetStage(CtTitleIntro* t, int stage) {
  if (!t) return 0;
  CtSeqSetup setup = ctSeqEntry(stage & 0x1F);
  if (!setup) return 0;                              // nothing is touched -- whatever was running
                                                     // in this machine keeps its state
  uint8_t page = t->page;                            // the one cell the init leaves alone
  CtTitleSoundSink snd = t->snd;                     // ...and the host's mailbox attachment, which is not machine state
  memset(t, 0, sizeof *t);                           //   either (CtTitleIntroReset keeps it for the same reason): the
  t->page = page;                                    //   attract's first credits (`FF 61`) wiped it, and every title
  t->snd = snd;                                      //   after them posted nothing -- no song $30 at the pendulum
  t->stage = (uint8_t)(stage & 0x1F);                //   (cartridge f9122: `$1E00 = 10 30`, 7E:3767), no exit fade
  t->briPeriod = 4; t->briCnt = 4;                   // $3063: `LDA #04 / STA $038E / STA $038F`
  t->obsel = 0x02;                                   // $3102's PPU reset -- everything else is 0
  t->frame = -1;
  setup(t);
  // $7E:300D `JSR $388B` -- the same CGRAM DMA the title installs as a vblank hook, here run ONCE
  // from the main dispatch, right after the setup and before the first NMI.
  memcpy(t->l.cgram, t->palShadow, sizeof t->l.cgram);
  return 1;
}

void CtTitleIntroInput(CtTitleIntro* t, uint16_t joy) { if (t) t->joy = joy; }
void CtTitleIntroSetSoundSink(CtTitleIntro* t, const CtTitleSoundSink* s) {
  if (!t) return;
  if (s) t->snd = *s; else memset(&t->snd, 0, sizeof t->snd);
}

// ⭐ THE TITLE HAS ITS OWN SONG, and it is the setup that starts it -- `$7E:5AA5 LDA #$30 / JSR
// $375F`, measured at boot f97, sixteen frames after the sequence engine's common init sends its
// stop. $375F is "play song A": the id into $1E01, the LOAD command into $1E00 and `LDX #$FFFF /
// STX $1E02` over the other two, then `JSL $C7:0004`. C7/0A39 then overwrites $1E02/$1E03 with the
// song's OWN record ($C7:241D + id*2 -> $06/$C0 for song $30), which is why the mailbox reads
// `30 C0 06` for the rest of the sequence and why the exit's $81 carries that $06 out to $2143.
// ⚠ $375F also mirrors the id into $0389/$038A and the setup then spins on `$2143 & $0F`; neither
// is modelled -- nothing in this sequence reads those shadows, and the port's loader is synchronous.
void CtTitleIntroSetupSound(CtTitleIntro* t) {
  if (!t) return;
  t->sndReq = 0x30;                                  // $3761 STA $038A -- the song WANTED
  t->sndCur = 0x30;                                  // $3764 STA $0389 -- ...and the song PLAYING
  ctSndMail(t, 1, 0x30);                             // $3767 STA $1E01 -- the song id
  ctSndMail(t, 0, 0x10);                             // $376A LDA #$10 / STA $1E00
  ctSndMail(t, 2, 0xFF);                             // $376F LDX #$FFFF / STX $1E02 (two bytes)
  ctSndMail(t, 3, 0xFF);
  ctSndPost(t);                                      // $3775 JSL $C7:0004
  // $5AAA `LDA $2143 / AND #$0F / BEQ ->5AAA` -- the setup waits for the driver to answer before it
  // returns, and that wait is over six frames long on the oracle (boot 133..139).
  if (t->snd.wait) t->snd.wait(t->snd.ctx);
}

// $7E:3177 -- the overlay's DRIVER RESET, run by the common init at the opening (30E9 `BIT #20 / BNE ->30F6 / STZ $038A /
// JSR $3177`) and by the teardown before the reboot (302A ... `JSR $3177`). Five commands, each through $377E (the mailbox
// to $2140-$2143 and a spin on the echo): `85 00` (STZ $1E02 / LDX #0085 / STX $1E00), `89`, `F2 00`, `F2 01` (LDA #01 /
// STA $1E01), and `81 00 FF` (LDX #FF00 / STX $1E01 / LDA #81 / STA $1E00 / JMP $377E) -- src/xlat/seq_c3.c L_7E3177 has
// the routine as the translated stages run it. `$0384` bit 5 skips it (317A): the credits ($61), the race ($60) and
// entry 3 ($63) never reach it; the title ($02) does, at both ends. Oracle, the attract's second title: `$0384 = 02` f9090,
// the last command `$1E00 = 81` at f9106 (7E:31AD), the title's song `10 30` at f9122.
// $377E is NOT C7/0004: it copies $1E03..$1E00 to $2143..$2140 and spins on `CMP $2140` for the echo (re-sending every
// 0x400 polls), and never clears the mailbox -- which is why $3177's fourth command can rewrite $1E01 alone and still send
// `F2 01`. The port's post is C7/0004's (the driver's consumption zeroes $1E00), so the bytes are kept here and written
// again before every send. ($1E03 is persistent RAM that $3177 never writes and no post clears: left alone.) What $377E
// leaves behind (`81 00 FF`) is NOT put back after the last send: the ROM's next writer overwrites it inside the same
// frame (the song load `10 30` at the opening; the boot's `00 00 FF 05` after the teardown's `JML $C0:0000`, oracle
// f11153), and the field's per-frame catch-all service would send a leftover `81` to the driver a second time.
static void ctSnd377E(CtTitleIntro* t, const uint8_t m[3]) {
  for (int k = 0; k < 3; k++) ctSndMail(t, k, m[k]);
  ctSndPost(t);
}
void CtTitleIntroDriverReset(CtTitleIntro* t, uint8_t seq) {
  if (!t || (seq & 0x20)) return;                    // 317A BIT #20 / BNE ->3176 (RTS)
  uint8_t m[3] = { 0, 0, 0 };
  m[2] = 0x00;                                       // 317E STZ $1E02
  m[0] = 0x85; m[1] = 0x00; ctSnd377E(t, m);         // 3181 LDX #0085 / STX $1E00 / 3187 JSR $377E
  m[0] = 0x89; ctSnd377E(t, m);                      // 318A LDA #89 / STA $1E00 / JSR $377E
  m[0] = 0xF2; ctSnd377E(t, m);                      // 3192 LDA #F2 / STA $1E00 / JSR $377E
  m[1] = 0x01; ctSnd377E(t, m);                      // 319A LDA #01 / STA $1E01 / JSR $377E
  m[1] = 0x00; m[2] = 0xFF;                          // 31A2 LDX #FF00 / STX $1E01
  m[0] = 0x81; ctSnd377E(t, m);                      // 31A8 LDA #81 / STA $1E00 / JMP $377E
}

// ⭐ C3/09A4 -- the NMI's OTHER half. C3/08B3 calls C3/08D0 (the frame, §1) and then this: a
// song-change watcher that compares the song WANTED ($038A) with the song PLAYING ($0389) and, when
// they differ, loads the wanted one. It is why $375F writes BOTH -- a caller that writes only $038A
// (there are two in the overlay) is making a REQUEST this watcher services on the next NMI.
// The title never changes $038A after its setup, so here the watcher only ever finds them equal --
// but leave out $375F's `STA $0389` and it loads the title's song all over again, every frame.
static void ctNmiSound(CtTitleIntro* t) {
  if (t->sndBusy) return;                            // $09A6 LDA $0388 / BNE
  // $09AB LDA $0384 / BIT #$20 / BNE -- the SEQUENCE id gates the watcher off for entries $20-$3F.
  // This sequence is entry 2, so the branch is never taken here; it is written out because it is
  // the ROM's condition, not because the title needs it.
  if (CT_TI_SEQ_ID & 0x20) return;
  if (t->sndReq == t->sndCur) return;                // $09B2 LDA $038A / $09B5 CMP $0389 / BEQ
  t->sndCur = t->sndReq;                             // $09BA STA $0389
  t->sndBusy = 1;                                    // $09BD INC $0388
  ctSndMail(t, 0, 0x10);                             // $09C0 LDA #$10 / STA $1E00
  ctSndMail(t, 1, t->sndReq);                        // $09C5 LDA $038A / STA $1E01
  ctSndMail(t, 2, 0xFF);                             // $09CB LDX #$FFFF / STX $1E02
  ctSndMail(t, 3, 0xFF);
  ctSndPost(t);                                      // $09D1 JSL $C7:0004
  t->sndBusy = 0;                                    // $09D5 STZ $0388
}

// The last frame before the script's own handover (`F1 006E 01`), i.e. the last frame at full
// brightness. The sequence itself runs on past this: fifteen more frames of $34C2's fade.
int CtTitleIntroFrames(void) { return 1990; }

int CtTitleIntroTick(CtTitleIntro* t) {
  if (!t) return 0;
  // ---- the VBLANK hook list ($7E:39BE over $0940) ----------------------------------------------
  // Everything these push is what the frame ABOUT TO BE DRAWN uses, so the latch is taken here and
  // the tasks below already belong to the next frame.
  for (int i = 0; i < 8; i++) {
    switch (t->vbl[i]) {
      case V_CGRAM:                                  // $388B: DMA $7E:2200 -> CGRAM
        memcpy(t->l.cgram, t->palShadow, sizeof t->l.cgram);
        break;
      case V_OAM: {                                  // $380B: $3870 finishes the last high byte,
        int hi = 0x200 + ((t->cell[CT_TI_C13] - 0x900) & 0x1F);   // then DMA $0700 -> OAM
        uint8_t b = t->oam[hi];
        for (int k = (t->cell[CT_TI_C11] >> 2) & 3; k != 4; k++)
          b = (uint8_t)((b >> 2) | 0x40);            // unused slots: x-high 1 -> off screen
        t->oam[hi] = b;
        memcpy(t->l.oam, t->oam, sizeof t->l.oam);
        t->l.anchorLo = t->anchorLo; t->l.anchorHi = t->anchorHi; t->anchorLo = t->anchorHi = 0;
        t->cell[CT_TI_C11] = 0;
        t->cell[CT_TI_C13] = 0x900;
        break;
      }
      case V_OAMCLR:                                 // $3843: MPYL's $E0 over the 512 sprite bytes
        memset(t->oam, 0xE0, 0x200);
        break;
      case V_M7:                                     // $5C57: the matrix + BG1's scroll
        t->l.m7[0] = (int16_t)t->cell[CT_TI_C5A];
        t->l.m7[1] = (int16_t)t->cell[CT_TI_C5C];
        t->l.m7[2] = (int16_t)t->cell[CT_TI_C5E];
        t->l.m7[3] = (int16_t)t->cell[CT_TI_C60];
        t->l.bg1h = t->scr[0].reg[1];
        t->l.bg1v = t->scr[0].reg[2];
        break;
      case V_OFFS:                                   // $6212: the offset table -> VRAM, BG3HOFS
        memcpy(t->l.offs, t->wave, sizeof t->l.offs);
        memcpy(t->l.offsTail, t->waveTail, sizeof t->l.offsTail);
        t->l.bg3h = t->cell[CT_TI_C44];
        break;
      default: break;
    }
  }
  // C3/08D0 at 30903, between the hook list and the tasks: `LDA $038D / BNE / LDA #80 / STA $2100`.
  // So the brightness belongs to the same latch as the shadows -- what is on screen this frame is
  // the value the tasks left last frame.
  t->l.bri = t->bri ? t->bri : 0x80;
  t->cell[CT_TI_C08]++;                              // the NMI's own frame counter ($7E:0008)
  uint16_t live[16];
  memcpy(live, t->task, sizeof live);                // the NMI's MVN: tasks run off a snapshot
  for (int i = 0; i < 16; i++) {
    switch (live[i]) {
      case 0:          break;
      case T_SCRIPT:   ctScriptRun(t, 0); break;   // $7E:3749 -- DP = $0A00
      case T_SCRIPTB:  ctScriptRun(t, 1); break;   // $7E:3754 -- DP = $0B00, the SAME interpreter
      case T_PEND:     ctTaskPendulum(t); break;
      case T_WAVE:     ctTaskWave(t); break;
      case T_COPY:     ctTaskCopyright(t); break;
      case T_CLOCK:    ctTaskClock(t); break;
      case T_SPIRAL:   ctTaskSpiral(t); break;
      case T_LETTERS:  ctTaskLetters(t); break;
      case T_HANDS:    ctTaskHands(t); break;
      case T_SQUARE:   ctTaskSquare(t); break;
      case T_DAMP:     ctTaskDamp(t); break;
      case T_MULSAVE:  break;                        // $37FA only parks $4202 around a ROM call
      case T_START:    ctTaskStart(t); break;
      case T_EXIT:     ctTaskExit(t); break;
      case T_FADE:     ctTaskFade(t); break;
      case T_FADEIN:   ctTaskFadeIn(t); break;
      case T_CREDPAGE: ctTaskCreditsPage(t); break;
      case T_FADEOUT:  ctTaskFadeOut(t); break;
      default:         break;
    }
  }
  ctJobsRun(t);
  ctNmiSound(t);                                     // C3/09A4, the NMI's second half
  t->frame++;
  return !t->done;
}

// ---- the render: LakeSnes' own fetch for the two configurations the sequence uses ----------------
static uint16_t tiVram(const CtTitleIntro* t, int word) {
  word &= 0x7FFF;
  if (word >= 0x7000 && word < 0x7040) return t->l.offs[word - 0x7000];  // BG3's map lives in WRAM
  return t->vram[word];
}

static int tiM7Tile(int row, int col) {
  row -= CT_TITLE_M7_MAP_R0; col -= CT_TITLE_M7_MAP_C0;
  if (row < 0 || row >= CT_TITLE_M7_MAP_H || col < 0 || col >= CT_TITLE_M7_MAP_W) return 0;
  return ctTitleM7Map[row * CT_TITLE_M7_MAP_W + col];
}

typedef struct { int sx, sy; } TiM7Start;

// ---- the PPU's LIVE register file, and the HDMA that rewrites it per scanline -------------------
// ⭐ Until entry 3 the port could draw a whole frame from one vblank latch, because the title never
// changes a register mid-frame. Entries 0 and 3 do: entry 3 moves a window edge and BG1's scroll on
// every line and swaps BG2's TILEMAP BASE at line 70, and entry 0 rewrites the whole mode-7 matrix
// per line and even changes $2105 itself (sequence_table.md 7.1/7.2). So the frame is drawn from
// THIS, seeded from the latch and then mutated exactly where HDMA writes.
typedef struct TiLine {
  uint8_t  mode, tm, ts, tmw, tsw, cgadsub, bg1sc, bg2sc, bg12nba, obsel;
  uint8_t  clipMode, preventMath, addSub, subtract, halfColor;
  uint8_t  win1l, win1r, win2l, win2r;
  CtSeqWin win[6];
  uint16_t bg1h, bg1v, bg3h;
  int bg1Wide, bg1WinLo;                 // (host mod) BG1 in the widescreen's sides (a slide): only its 512 columns from WinLo
                                         // (unwrapped) -- the period around the slide's end, so the logo never wraps round
  int16_t  m7[4];
  uint8_t  scrollPrev, scrollPrev2;                  // the write-twice latch $210D..$2114 share
  uint8_t  fixedR, fixedG, fixedB;                   // $2132 COLDATA -- entry 0 walks it per line
  int      y;                                        // (CtTitleIntroRenderEx: the line being drawn)
} TiLine;

// One channel's live walk. dma.c keeps this in the channel; here it is per FRAME, so the render can
// stay const -- it never writes back into the machine.
typedef struct TiHdmaRun {
  uint16_t tableAdr, size;
  uint8_t  repCount, doTransfer, terminated;
} TiHdmaRun;

// The overlay's own STATIC bytes that an HDMA channel can point at -- its descriptor tables, which
// are decompressed once at boot and never written again (measured: $7E:685E..$68A0 is identical at
// three frames of entry 3, while $F000 changes every frame). Only the region a ported stage uses is
// carried; anywhere else reads 0, and a 0 repeat count terminates the channel.
static const uint8_t* g_seqPage7E = 0;             // see CtSeqSetOverlayImage
static uint8_t ctSeqOverlay(uint16_t a) {
  return g_seqPage7E ? g_seqPage7E[a] : 0;
}

// DEBUG/TEST HOOK. An HDMA channel's descriptor table can point anywhere in $7E, so grading
// the walker against the cartridge means letting it read a real page. Point this at a 64 KB
// $7E image (a `--peek` .wram sidecar) and tiRead resolves everything outside $F000..$F3FF
// through it. A ported stage carries its own captured tables instead and leaves this null.
void CtSeqSetOverlayImage(const unsigned char* page7E) { g_seqPage7E = page7E; }

// A byte of $7E as the HDMA sees it: the per-line data the tasks rebuild lives at $F000..$F3FF, and
// everything else a channel can point at is the overlay's own static data.
static uint8_t tiRead(const CtTitleIntro* t, uint32_t adr) {
  uint16_t a = (uint16_t)adr;
  if (g_seqPage7E) return g_seqPage7E[a];          // a real page: everything resolves in it
  if (a >= 0xF000 && a < 0xF400) return t->hram[a - 0xF000];
  return ctSeqOverlay(a);
}

// snes/ppu.c ppu_write, for the registers a sequence's HDMA actually targets. ⚠ $210D/$210F/$2111
// and $210E/$2110/$2112 are WRITE-TWICE and share one latch pair, which is why mode 2 ("one
// register written twice") is what the ROM programs for a scroll channel.
static void tiWriteReg(TiLine* L, int adr, uint8_t val) {
  switch (adr & 0xFF) {
    case 0x05: L->mode = (uint8_t)(val & 7); break;
    case 0x07: L->bg1sc = val; break;
    case 0x08: L->bg2sc = val; break;
    case 0x0B: L->bg12nba = val; break;
    case 0x0D: L->bg1h = (uint16_t)(((val << 8) | (L->scrollPrev & 0xF8) | (L->scrollPrev2 & 7)));
               L->scrollPrev2 = L->scrollPrev; L->scrollPrev = val; break;
    case 0x0E: L->bg1v = (uint16_t)((val << 8) | L->scrollPrev); L->scrollPrev = val; break;
    case 0x10: /* BG2VOFS -- entry 3 drives it, but no ported stage READS BG2's v-scroll */
               L->scrollPrev = val; break;
    case 0x11: L->bg3h = (uint16_t)(((val << 8) | (L->scrollPrev & 0xF8) | (L->scrollPrev2 & 7)));
               L->scrollPrev2 = L->scrollPrev; L->scrollPrev = val; break;
    case 0x1B: L->m7[0] = (int16_t)((val << 8) | (uint8_t)L->m7[0]); break;
    case 0x1C: L->m7[1] = (int16_t)((val << 8) | (uint8_t)L->m7[1]); break;
    case 0x1D: L->m7[2] = (int16_t)((val << 8) | (uint8_t)L->m7[2]); break;
    case 0x1E: L->m7[3] = (int16_t)((val << 8) | (uint8_t)L->m7[3]); break;
    case 0x23: for (int i = 0; i < 2; i++) {
                 L->win[i].w1inv = (val >> (i * 4)) & 1; L->win[i].w1en = (val >> (i * 4 + 1)) & 1;
                 L->win[i].w2inv = (val >> (i * 4 + 2)) & 1; L->win[i].w2en = (val >> (i * 4 + 3)) & 1; }
               break;
    case 0x24: for (int i = 2; i < 4; i++) {
                 int s = (i - 2) * 4;
                 L->win[i].w1inv = (val >> s) & 1; L->win[i].w1en = (val >> (s + 1)) & 1;
                 L->win[i].w2inv = (val >> (s + 2)) & 1; L->win[i].w2en = (val >> (s + 3)) & 1; }
               break;
    case 0x25: for (int i = 4; i < 6; i++) {
                 int s = (i - 4) * 4;
                 L->win[i].w1inv = (val >> s) & 1; L->win[i].w1en = (val >> (s + 1)) & 1;
                 L->win[i].w2inv = (val >> (s + 2)) & 1; L->win[i].w2en = (val >> (s + 3)) & 1; }
               break;
    case 0x26: L->win1l = val; break;
    case 0x27: L->win1r = val; break;
    case 0x28: L->win2l = val; break;
    case 0x29: L->win2r = val; break;
    case 0x2A: for (int i = 0; i < 4; i++) L->win[i].logic = (val >> (i * 2)) & 3; break;
    case 0x2B: for (int i = 4; i < 6; i++) L->win[i].logic = (val >> ((i - 4) * 2)) & 3; break;
    case 0x2C: L->tm = val; break;
    case 0x2D: L->ts = val; break;
    case 0x2E: L->tmw = val; break;
    case 0x2F: L->tsw = val; break;
    case 0x30: L->clipMode = (uint8_t)((val >> 6) & 3); L->preventMath = (uint8_t)((val >> 4) & 3);
               L->addSub = (uint8_t)((val >> 1) & 1); break;
    case 0x31: L->cgadsub = (uint8_t)(val & 0x3F);
               L->halfColor = (uint8_t)((val >> 6) & 1); L->subtract = (uint8_t)((val >> 7) & 1);
               break;
    // $2132: bits 7-5 say WHICH of blue/green/red the low five bits are for, and one write can
    // set more than one of them -- entry 0's $EE is all three at once.
    case 0x32: if (val & 0x20) L->fixedR = (uint8_t)(val & 0x1F);
               if (val & 0x40) L->fixedG = (uint8_t)(val & 0x1F);
               if (val & 0x80) L->fixedB = (uint8_t)(val & 0x1F);
               break;
    default: break;                                  // a register no ported stage's HDMA targets
  }
}

// dma.c's bAdrOffsets / transferLength, verbatim.
static const int tiHdmaOff[8][4] = {
  {0,0,0,0}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1}, {0,1,2,3}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1}
};
static const int tiHdmaLen[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };

// dma_initHdma: at hPos 16 of line 0. Loads each enabled channel's first repeat count and, for an
// indirect channel, its first data pointer.
static void tiHdmaInit(const CtTitleIntro* t, TiHdmaRun* run) {
  for (int i = 0; i < 8; i++) {
    run[i].doTransfer = 0; run[i].terminated = 0; run[i].tableAdr = 0; run[i].size = 0;
    run[i].repCount = 0;
    if (!((t->hdmaEnable >> i) & 1)) continue;
    const CtSeqHdmaCh* c = &t->hdma[i];
    run[i].tableAdr = c->aAdr;
    run[i].repCount = tiRead(t, ((uint32_t)c->aBank << 16) | run[i].tableAdr++);
    if (run[i].repCount == 0) run[i].terminated = 1;
    if (c->dmap & 0x40) {                            // indirect: the entry carries a POINTER
      run[i].size = tiRead(t, ((uint32_t)c->aBank << 16) | run[i].tableAdr++);
      run[i].size |= (uint16_t)(tiRead(t, ((uint32_t)c->aBank << 16) | run[i].tableAdr++) << 8);
    }
    run[i].doTransfer = 1;
  }
}

// dma_doHdma: at hPos 1104 of every visible line, i.e. AFTER that line is drawn -- so the line the
// render is about to draw has already had one run per line above it.
static void tiHdmaRun(const CtTitleIntro* t, TiHdmaRun* run, TiLine* L) {
  int lastActive = 0;
  for (int i = 0; i < 8; i++)
    if (((t->hdmaEnable >> i) & 1) && !run[i].terminated) lastActive = i;
  for (int i = 0; i < 8; i++) {                      // all the copies first...
    if (!((t->hdmaEnable >> i) & 1) || run[i].terminated || !run[i].doTransfer) continue;
    const CtSeqHdmaCh* c = &t->hdma[i];
    int mode = c->dmap & 7;
    for (int j = 0; j < tiHdmaLen[mode]; j++) {
      uint8_t v;
      if (c->dmap & 0x40) v = tiRead(t, ((uint32_t)c->indBank << 16) | run[i].size++);
      else                v = tiRead(t, ((uint32_t)c->aBank << 16) | run[i].tableAdr++);
      tiWriteReg(L, c->bAdr + tiHdmaOff[mode][j], v);
    }
  }
  for (int i = 0; i < 8; i++) {                      // ...then all the updates
    if (!((t->hdmaEnable >> i) & 1) || run[i].terminated) continue;
    const CtSeqHdmaCh* c = &t->hdma[i];
    run[i].repCount--;
    run[i].doTransfer = (uint8_t)(run[i].repCount & 0x80);
    uint8_t next = tiRead(t, ((uint32_t)c->aBank << 16) | run[i].tableAdr);
    if ((run[i].repCount & 0x7F) == 0) {
      run[i].repCount = next;
      run[i].tableAdr++;
      if (c->dmap & 0x40) {
        if (run[i].repCount == 0 && i == lastActive) run[i].size = 0;  // last channel: low byte is 0
        else run[i].size = tiRead(t, ((uint32_t)c->aBank << 16) | run[i].tableAdr++);
        run[i].size |= (uint16_t)(tiRead(t, ((uint32_t)c->aBank << 16) | run[i].tableAdr++) << 8);
      }
      if (run[i].repCount == 0) run[i].terminated = 1;
      run[i].doTransfer = 1;
    }
  }
}

// ppu_getWindowState, verbatim.
static int tiWinState(const TiLine* L, int layer, int x) {
  const CtSeqWin* w = &L->win[layer];
  if (!w->w1en && !w->w2en) return 0;
  int t1 = x >= L->win1l && x <= L->win1r;
  int t2 = x >= L->win2l && x <= L->win2r;
  if (w->w1en && !w->w2en) return w->w1inv ? !t1 : t1;
  if (!w->w1en && w->w2en) return w->w2inv ? !t2 : t2;
  if (w->w1inv) t1 = !t1;
  if (w->w2inv) t2 = !t2;
  switch (w->logic) {
    case 0: return t1 || t2;
    case 1: return t1 && t2;
    case 2: return t1 != t2;
    default: return t1 == t2;
  }
}

// $210D/$210E feed BOTH the mode-7 scroll (13 bits, signed) and the BG layer scroll (10 bits), from
// the same latched word -- so the two readings below are two views of one register, not two sources.
static int s13(uint16_t v) { return ((int16_t)((v & 0x1FFF) << 3)) >> 3; }

static TiM7Start tiM7Starts(const TiLine* L, int y) {
  int A = L->m7[0], B = L->m7[1], C = L->m7[2], D = L->m7[3];
  int cH = s13(L->bg1h) - CT_TITLE_M7_X, cV = s13(L->bg1v) - CT_TITLE_M7_Y;
  cH = (cH & 0x2000) ? (cH | ~1023) : (cH & 1023);
  cV = (cV & 0x2000) ? (cV | ~1023) : (cV & 1023);
  TiM7Start s;
  s.sx = ((A * cH) & ~63) + ((B * y) & ~63) + ((B * cV) & ~63) + (CT_TITLE_M7_X << 8);
  s.sy = ((C * cH) & ~63) + ((D * y) & ~63) + ((D * cV) & ~63) + (CT_TITLE_M7_Y << 8);
  return s;
}

static int tiM7Pixel(const CtTitleIntro* t, const TiLine* L, const TiM7Start* s, int x) {
  int xp = (s->sx + L->m7[0] * x) >> 8, yp = (s->sy + L->m7[2] * x) >> 8;
  // M7SEL = $80 -> largeField: outside the 1024x1024 field the layer is transparent.
  int outside = xp < 0 || xp >= CT_M7_FIELD || yp < 0 || yp >= CT_M7_FIELD;
  if (outside) return 0;
  xp &= 0x3FF; yp &= 0x3FF;
  int tile = tiM7Tile(yp >> 3, xp >> 3);
  if (tile >= CT_TITLE_M7_TILES) return 0;
  return ctTitleM7Chr[tile * 64 + (yp & 7) * 8 + (xp & 7)];
}

// BG1 is the only layer the offset table applies to (its entries carry bit 13); BG2 never does.
static uint16_t tiOffsetValue(const CtTitleIntro* t, const TiLine* L, int col, int row) {
  int ox = col * 8 + (L->bg3h & 0x3FF);            // BG3SC = $70: a 32x32 map at word $7000
  int oy = row * 8;                                  // BG3VOFS is never written by this sequence
  return tiVram(t, 0x7000 + ((((oy >> 3) & 0x1F) << 5) | ((ox >> 3) & 0x1F)));
}

static void tiOPT(const CtTitleIntro* t, const TiLine* L, int layer, int* lx, int* ly) {
  int x = *lx, y = *ly;
  int hScroll = (layer == 0 ? L->bg1h : CT_TI_HOFS) & 0x3FF;
  int column = ((x - (x & 7)) - (hScroll & ~7)) >> 3;
  if (column <= 0) return;
  int valid = layer == 0 ? 0x2000 : 0x4000;          // bit 13 = "for BG1", bit 14 = "for BG2"
  uint16_t hOff = tiOffsetValue(t, L, column - 1, 0);
  uint16_t vOff = tiOffsetValue(t, L, column - 1, 1);
  if (hOff & valid) *lx = ((hOff & 0x3F8) + (column * 8)) | (x & 7);
  if (vOff & valid) *ly = (vOff & 0x3FF) + (y - ((layer == 0 ? L->bg1v : 0) & 0x3FF));
}

// BG fetch, transliterated from snes/ppu.c ppu_getPixelForBgLayer for the two bit depths the
// sequences use: bitDepthsPerMode says mode 2 is 4bpp on BG1/BG2 and mode 0 is 2bpp on all four --
// and mode 0 alone shifts each layer's palette block by eight (`paletteNum += 8 * layer`).
// Everything that used to be a title constant here now comes from the registers a setup writes.
// ⚠ Only BG1 and BG2 have their registers here. Mode 0 puts BG3/BG4 in the layer order, but no
// ported stage enables them in $212C, so $2109/$210A/$210C were never derived -- a stage that does
// enable one needs them read off its own setup, not guessed.
static int tiBgPixel(const CtTitleIntro* t, const TiLine* L, int x, int y, int layer, int priority) {
  int sc = layer == 0 ? L->bg1sc : L->bg2sc;
  int adr = ((sc & 0xFC) << 8) + (((y >> 3) & 0x1F) << 5 | ((x >> 3) & 0x1F));
  if ((x & 0x100) && (sc & 1)) adr += 0x400;         // BG1SC = $65: a 64x32 map
  if ((y & 0x100) && (sc & 2)) adr += (sc & 1) ? 0x800 : 0x400;
  uint16_t tile = tiVram(t, adr);
  if (((tile & 0x2000) != 0) != priority) return 0;
  int pal = (tile & 0x1C00) >> 10;
  int row = (tile & 0x8000) ? 7 - (y & 7) : (y & 7);
  int col = (tile & 0x4000) ? (x & 7) : 7 - (x & 7);
  int num = tile & 0x3FF;
  int depth = L->mode == 0 ? 2 : 4;
  int chr = (layer == 0 ? (L->bg12nba & 0x0F) : (L->bg12nba >> 4)) << 12;
  if (L->mode == 0) pal += 8 * layer;
  uint16_t p1 = tiVram(t, chr + num * 4 * depth + row);
  int px = ((p1 >> col) & 1) | (((p1 >> (8 + col)) & 1) << 1);
  if (depth == 2) return px ? 4 * pal + px : 0;
  uint16_t p2 = tiVram(t, chr + num * 4 * depth + 8 + row);
  px |= (((p2 >> col) & 1) << 2) | (((p2 >> (8 + col)) & 1) << 3);
  return px ? 16 * pal + px : 0;
}

// ppu_evaluateSprites: 8x8/16x16 (OBSEL = 0), name base $4000, and BOTH hardware limits -- 32
// sprites and 34 8x1 slivers per line. ⚠ Ninety-odd sprites are live in the logo phase but they
// never crowd one line: 20 is the most any scanline of this sequence carries, so neither limit ever
// fires and no frame can grade them. They are here because that is what the PPU does.
static void tiEvalSprites(const CtTitleIntro* t, int line, uint8_t* pix, uint8_t* prio) {
  const uint8_t* o = t->l.oam;
  const uint8_t* hi = t->l.oam + 0x200;
  uint8_t found[32];
  int nfound = 0, ntiles = 0;
  memset(pix, 0, 256);
  for (int i = 0; i < 128; i++) {
    int y = o[i * 4 + 1];
    int size = ((hi[i >> 2] >> ((i & 3) * 2 + 1)) & 1) ? 16 : 8;
    uint8_t row = (uint8_t)(line - y);
    if (row < size) {
      int x = o[i * 4] | (((hi[i >> 2] >> ((i & 3) * 2)) & 1) << 8);
      if (x > 255) x -= 512;
      if (x > -size) {
        if (++nfound > 32) { nfound = 32; break; }
        found[nfound - 1] = (uint8_t)i;
      }
    }
  }
  for (int k = nfound; k > 0; k--) {
    int i = found[k - 1];
    int y = o[i * 4 + 1];
    int size = ((hi[i >> 2] >> ((i & 3) * 2 + 1)) & 1) ? 16 : 8;
    uint8_t row = (uint8_t)(line - y);
    int x = o[i * 4] | (((hi[i >> 2] >> ((i & 3) * 2)) & 1) << 8);
    if (x > 255) x -= 512;
    if (x <= -size) continue;
    int tile = o[i * 4 + 2];
    int attr = o[i * 4 + 3];
    int pal = (attr >> 1) & 7;
    int hFlip = attr & 0x40;
    if (attr & 0x80) row = (uint8_t)(size - 1 - row);
    for (int c = 0; c < size; c += 8) {
      if (c + x <= -8 || c + x >= 256) continue;
      if (++ntiles > 34) break;
      int usedCol = hFlip ? size - 1 - c : c;
      uint8_t used = (uint8_t)((((tile >> 4) + (row / 8)) << 4) | (((tile & 0xF) + (usedCol / 8)) & 0xF));
      // OBSEL: `objTileAdr1 = (val & 7) << 13` and `objTileAdr2 = objTileAdr1 + (((val & 0x18) +
      // 8) << 9)` -- $02 for the title ($4000 / $5000), $00 for the credits ($0000 / $1000).
      int adr1 = (t->obsel & 7) << 13;
      int base = (attr & 1) ? adr1 + (((t->obsel & 0x18) + 8) << 9) : adr1;
      uint16_t p1 = tiVram(t, base + used * 16 + (row & 7));
      uint16_t p2 = tiVram(t, base + used * 16 + 8 + (row & 7));
      for (int px = 0; px < 8; px++) {
        int sh = hFlip ? px : 7 - px;
        int v = ((p1 >> sh) & 1) | (((p1 >> (8 + sh)) & 1) << 1) |
                (((p2 >> sh) & 1) << 2) | (((p2 >> (8 + sh)) & 1) << 3);
        int sc = c + x + px;
        if (v > 0 && sc >= 0 && sc < 256) {
          pix[sc] = (uint8_t)(0x80 + 16 * pal + v);
          prio[sc] = (uint8_t)((attr & 0x30) >> 4);
        }
      }
    }
    if (ntiles > 34) break;
  }
}

// tiEvalSprites for the host's wide picture: pix/prio are w1 columns wide (the view's column xv at xv + x0). Every sprite
// stays where the PPU puts it, inside the view's 256 columns -- except the copyright block's (the latch's anchor slots),
// which the widescreen moves by the right side's width so the block keeps its place against the screen's right end.
static void tiEvalSpritesEx(const CtTitleIntro* t, int line, uint8_t* pix, uint8_t* prio, int w1, int x0) {
  const uint8_t* o = t->l.oam;
  const uint8_t* hi = t->l.oam + 0x200;
  const int dxA = w1 - 256 - x0;
  uint8_t found[32];
  int nfound = 0, ntiles = 0;
  memset(pix, 0, (size_t)w1);
  for (int i = 0; i < 128; i++) {
    int y = o[i * 4 + 1];
    int size = ((hi[i >> 2] >> ((i & 3) * 2 + 1)) & 1) ? 16 : 8;
    uint8_t row = (uint8_t)(line - y);
    if (row < size) {
      int x = o[i * 4] | (((hi[i >> 2] >> ((i & 3) * 2)) & 1) << 8);
      if (x > 255) x -= 512;
      if (x > -size) {
        if (++nfound > 32) { nfound = 32; break; }
        found[nfound - 1] = (uint8_t)i;
      }
    }
  }
  for (int k = nfound; k > 0; k--) {
    int i = found[k - 1];
    int y = o[i * 4 + 1];
    int size = ((hi[i >> 2] >> ((i & 3) * 2 + 1)) & 1) ? 16 : 8;
    uint8_t row = (uint8_t)(line - y);
    int x = o[i * 4] | (((hi[i >> 2] >> ((i & 3) * 2)) & 1) << 8);
    if (x > 255) x -= 512;
    if (x <= -size) continue;
    const int anch = i >= t->l.anchorLo && i < t->l.anchorHi;
    const int lo = anch ? -x0 : 0, hiX = anch ? w1 - x0 : 256, sh = anch ? dxA : 0;
    int tile = o[i * 4 + 2];
    int attr = o[i * 4 + 3];
    int pal = (attr >> 1) & 7;
    int hFlip = attr & 0x40;
    if (attr & 0x80) row = (uint8_t)(size - 1 - row);
    for (int c = 0; c < size; c += 8) {
      if (c + x <= -8 || c + x >= 256) continue;
      if (++ntiles > 34) break;
      int usedCol = hFlip ? size - 1 - c : c;
      uint8_t used = (uint8_t)((((tile >> 4) + (row / 8)) << 4) | (((tile & 0xF) + (usedCol / 8)) & 0xF));
      int adr1 = (t->obsel & 7) << 13;
      int base = (attr & 1) ? adr1 + (((t->obsel & 0x18) + 8) << 9) : adr1;
      uint16_t p1 = tiVram(t, base + used * 16 + (row & 7));
      uint16_t p2 = tiVram(t, base + used * 16 + 8 + (row & 7));
      for (int px = 0; px < 8; px++) {
        int shb = hFlip ? px : 7 - px;
        int v = ((p1 >> shb) & 1) | (((p1 >> (8 + shb)) & 1) << 1) |
                (((p2 >> shb) & 1) << 2) | (((p2 >> (8 + shb)) & 1) << 3);
        int sc = c + x + px + sh;
        if (v > 0 && sc >= lo && sc < hiX) {
          pix[sc + x0] = (uint8_t)(0x80 + 16 * pal + v);
          prio[sc + x0] = (uint8_t)((attr & 0x30) >> 4);
        }
      }
    }
    if (ntiles > 34) break;
  }
}

// ppu_getPixel for the sequence's two modes. Returns the LakeSnes layer id (0-3 BG, 4/6 OBJ,
// 5 backdrop) so the colour math below can ask whether $2131 enables it.
static int tiGetPixel(const CtTitleIntro* t, const TiLine* L, int x, int y, int sub,
                      const TiM7Start* m7, const uint8_t* objPix, const uint8_t* objPrio,
                      int* r, int* g, int* b) {
  // snes/ppu.c layersPerMode / prioritysPerMode / layerCountPerMode, the three rows the sequences
  // use. Mode 0 puts BG3 and BG4 in the order too; the credits leave them off in $212C.
  static const int lay0[12] = { 4, 0, 1, 4, 0, 1, 4, 2, 3, 4, 2, 3 };
  static const int pri0[12] = { 3, 1, 1, 2, 0, 0, 1, 1, 1, 0, 0, 0 };
  static const int lay2[8] = { 4, 0, 4, 1, 4, 0, 4, 1 }, pri2[8] = { 3, 1, 2, 1, 1, 0, 0, 0 };
  static const int lay7[5] = { 4, 4, 4, 0, 4 }, pri7[5] = { 3, 2, 1, 0, 0 };
  const int* lay = L->mode == 7 ? lay7 : L->mode == 0 ? lay0 : lay2;
  const int* pri = L->mode == 7 ? pri7 : L->mode == 0 ? pri0 : pri2;
  int n = L->mode == 7 ? 5 : L->mode == 0 ? 12 : 8;
  int screen = sub ? L->ts : L->tm;
  int clipped = sub ? L->tsw : L->tmw;         // $212E/$212F: which layers the window may clip
  int layer = 5, pixel = 0;
  for (int i = 0; i < n; i++) {
    int cur = lay[i], curPri = pri[i];
    int bit = cur == 4 ? 0x10 : (1 << cur);
    // ppu_getPixel: a layer draws if $212C/$212D enables it AND it is not window-clipped here.
    if (!(screen & bit)) continue;
    if ((clipped & bit) && tiWinState(L, cur, x)) continue;
    if (cur < 4) {
      if (L->mode == 7) {
        pixel = tiM7Pixel(t, L, m7, x);
      } else {
        if (cur > 1) continue;                       // see tiBgPixel: BG3/BG4 have no registers here
        int lx = x + ((cur == 0 ? L->bg1h : CT_TI_HOFS) & 0x3FF);
        int ly = y + ((cur == 0 ? L->bg1v : 0) & 0x3FF);
        if (L->mode == 2) tiOPT(t, L, cur, &lx, &ly);  // offset-per-tile is modes 2/4/6, not 0
        pixel = tiBgPixel(t, L, lx & 0x3FF, ly & 0x3FF, cur, curPri);
      }
    } else {
      pixel = (objPrio[x] == curPri) ? objPix[x] : 0;
    }
    if (pixel > 0) { layer = cur; break; }
  }
  uint16_t c = t->l.cgram[pixel & 0xFF];
  *r = c & 31; *g = (c >> 5) & 31; *b = (c >> 10) & 31;
  if (layer == 4 && pixel < 0xC0) layer = 6;
  return layer;
}

void CtTitleIntroRender(const CtTitleIntro* t, unsigned char* fb, int w, int h) {
  // CGWSEL = $02 / CGADSUB = $21 for the TITLE: no clipping, ADD, no halving, fixed colour (0,0,0),
  // and the math enabled for BG1 and the backdrop. During the swing TS = OBJ, so the copyright
  // block and the clock are ADDED onto the mode-7 pendulum in 5-BIT space; from the moment $5C98
  // writes $212C the subscreen is empty and the add becomes a no-op. $2131's low six bits are the
  // per-layer enables in the LakeSnes layer order, so the credits' $00 turns the whole block off.
  uint8_t objPix[256], objPrio[256];
  memset(objPrio, 0, sizeof objPrio);
  memset(fb, 0, (size_t)w * h * 3);
  // $2100: bit 7 is FORCED BLANK (the whole screen is black, sprites are not even evaluated) and the
  // low nibble scales every component on the way out -- `* brightness / 15`, AFTER the 5->8
  // expansion, exactly where snes/ppu.c ppu_putPixels does it. This is the fade $7E:34C2 walks down.
  int bri = t->l.bri & 0x0F;
  if (t->l.bri & 0x80) return;
  // The line's register file, seeded from the vblank latch. For the title and the credits it stays
  // exactly this for all 224 lines; for a stage with HDMA it is rewritten below, per line.
  TiLine L;
  memset(&L, 0, sizeof L);
  L.mode = t->mode; L.tm = t->tm; L.ts = t->ts; L.tmw = t->tmw; L.tsw = t->tsw;
  L.cgadsub = (uint8_t)(t->cgadsub & 0x3F);
  L.halfColor = t->halfColor; L.subtract = t->subtract; L.addSub = t->addSub;
  L.clipMode = t->clipMode; L.preventMath = t->preventMath;
  L.bg1sc = t->bg1sc; L.bg2sc = t->bg2sc; L.bg12nba = t->bg12nba; L.obsel = t->obsel;
  L.win1l = t->win1l; L.win1r = t->win1r; L.win2l = t->win2l; L.win2r = t->win2r;
  memcpy(L.win, t->win, sizeof L.win);
  L.bg1h = t->l.bg1h; L.bg1v = t->l.bg1v; L.bg3h = t->l.bg3h;
  for (int k = 0; k < 4; k++) L.m7[k] = t->l.m7[k];
  TiHdmaRun run[8];
  tiHdmaInit(t, run);                                // hPos 16 of line 0
  for (int y = 0; y < h; y++) {
    int line = y + 1;                                // ⚠ the SNES renders scanlines 1..224
    // dma_doHdma fires at hPos 1104 of every visible line, i.e. after that line is drawn -- so the
    // line about to be drawn has had exactly one run for every line above it.
    tiHdmaRun(t, run, &L);
    const int mathEnabled[6] = { L.cgadsub & 1, (L.cgadsub >> 1) & 1, (L.cgadsub >> 2) & 1,
                                 (L.cgadsub >> 3) & 1, (L.cgadsub >> 4) & 1, (L.cgadsub >> 5) & 1 };
    tiEvalSprites(t, line - 1, objPix, objPrio);
    TiM7Start m7 = tiM7Starts(&L, line & 0xFF);
    unsigned char* row = fb + (size_t)y * w * 3;
    for (int x = 0; x < w; x++) {
      int r, g, b, r2 = 0, g2 = 0, b2 = 0;
      int main = tiGetPixel(t, &L, x, line, 0, &m7, objPix, objPrio, &r, &g, &b);
      // ppu_handlePixel: the COLOUR window ($2130's clip/prevent-math modes test window layer 5).
      int cw = tiWinState(&L, 5, x);
      if (L.clipMode == 3 || (L.clipMode == 2 && cw) || (L.clipMode == 1 && !cw)) r = g = b = 0;
      int me = main < 6 && mathEnabled[main] &&
               !(L.preventMath == 3 || (L.preventMath == 2 && cw) || (L.preventMath == 1 && !cw));
      if (me) {
        int second = 5;
        if (L.addSub) second = tiGetPixel(t, &L, x, line, 1, &m7, objPix, objPrio, &r2, &g2, &b2);
        int useSub = L.addSub && second != 5;
        if (L.subtract) {
          r -= useSub ? r2 : L.fixedR; g -= useSub ? g2 : L.fixedG; b -= useSub ? b2 : L.fixedB;
        } else {
          r += useSub ? r2 : L.fixedR; g += useSub ? g2 : L.fixedG; b += useSub ? b2 : L.fixedB;
        }
        if (L.halfColor && (second == 5 || !L.addSub)) { r >>= 1; g >>= 1; b >>= 1; }
        if (r > 31) r = 31;
        if (g > 31) g = 31;
        if (b > 31) b = 31;
        if (r < 0) r = 0;
        if (g < 0) g = 0;
        if (b < 0) b = 0;
      }
      // 5-bit -> 8-bit LAST, exactly as the PPU does it on the way out, then the brightness.
      row[x * 3 + 0] = (unsigned char)((((r << 3) | (r >> 2)) * bri) / 15);
      row[x * 3 + 1] = (unsigned char)((((g << 3) | (g >> 2)) * bri) / 15);
      row[x * 3 + 2] = (unsigned char)((((b << 3) | (b >> 2)) * bri) / 15);
    }
  }
}

// ---- THE HOST'S OPTIONS: wider, sharper, between two frames (title_intro.h CtTitleIntroRenderEx) ---------------------
// tiGetPixel with the mode-7 sample handed in (taken at the sub-pixel) and the view's edges: outside the SNES's 256
// columns there are no sprites and no tiled layer (their maps wrap), only mode 7.
static void tiOPTEx(const CtTitleIntro* t, const TiLine* L, int layer, int* lx, int* ly);
static int tiGetPixelEx(const CtTitleIntro* t, const TiLine* L, int x, int sub, int m7px, int inView,
                        const uint8_t* objPix, const uint8_t* objPrio, int* r, int* g, int* b) {
  static const int lay0[12] = { 4, 0, 1, 4, 0, 1, 4, 2, 3, 4, 2, 3 };
  static const int pri0[12] = { 3, 1, 1, 2, 0, 0, 1, 1, 1, 0, 0, 0 };
  static const int lay2[8] = { 4, 0, 4, 1, 4, 0, 4, 1 }, pri2[8] = { 3, 1, 2, 1, 1, 0, 0, 0 };
  static const int lay7[5] = { 4, 4, 4, 0, 4 }, pri7[5] = { 3, 2, 1, 0, 0 };
  const int* lay = L->mode == 7 ? lay7 : L->mode == 0 ? lay0 : lay2;
  const int* pri = L->mode == 7 ? pri7 : L->mode == 0 ? pri0 : pri2;
  int n = L->mode == 7 ? 5 : L->mode == 0 ? 12 : 8;
  int screen = sub ? L->ts : L->tm;
  int clipped = sub ? L->tsw : L->tmw;
  int layer = 5, pixel = 0;
  for (int i = 0; i < n; i++) {
    int cur = lay[i], curPri = pri[i];
    int bit = cur == 4 ? 0x10 : (1 << cur);
    if (!(screen & bit)) continue;
    if ((clipped & bit) && tiWinState(L, cur, x)) continue;
    if (cur < 4) {
      if (L->mode == 7) pixel = m7px;
      else {
        if (cur > 1) continue;
        // outside the view only a 64-wide map (the title's BG1, $2107 = $65) has its own columns there -- within 256 of
        // the view's middle, so no column is drawn twice; a 32-wide one would wrap back to the view's own picture
        if (!inView && !(cur == 0 && L->bg1Wide)) continue;
        if (cur == 0 && L->bg1Wide && (unsigned)(x + ((((L->bg1h & 0x3FF) ^ 0x200) - 0x200)) - L->bg1WinLo) >= 512u) continue;
        int lx = x + ((cur == 0 ? L->bg1h : CT_TI_HOFS) & 0x3FF);
        int ly = (L->y) + ((cur == 0 ? L->bg1v : 0) & 0x3FF);
        if (L->mode == 2) tiOPTEx(t, L, cur, &lx, &ly);
        pixel = tiBgPixel(t, L, lx & 0x3FF, ly & 0x3FF, cur, curPri);
      }
    } else pixel = (objPrio[x] == curPri) ? objPix[x] : 0;   // (objPix/objPrio point at the view's column 0)
    if (pixel > 0) { layer = cur; break; }
  }
  uint16_t c = t->l.cgram[pixel & 0xFF];
  *r = c & 31; *g = (c >> 5) & 31; *b = (c >> 10) & 31;
  if (layer == 4 && pixel < 0xC0) layer = 6;
  return layer;
}
// tiOPT for the widescreen's columns: a column past the table's 32 takes the ripple the table pushed off its right end
// (waveTail) -- the wave keeps travelling into the right side instead of wrapping back to the left's.
static void tiOPTEx(const CtTitleIntro* t, const TiLine* L, int layer, int* lx, int* ly) {
  int x = *lx, y = *ly;
  int hScroll = (layer == 0 ? L->bg1h : CT_TI_HOFS) & 0x3FF;
  int column = ((x - (x & 7)) - (hScroll & ~7)) >> 3;
  int e = (column - 1) + ((L->bg3h & 0x3FF) >> 3);
  if (column <= 32 || e < 32) { tiOPT(t, L, layer, lx, ly); return; }
  if (layer != 0) return;
  uint16_t vOff = e - 32 < 64 ? t->l.offsTail[e - 32] : 0;
  if (vOff & 0x2000) *ly = (vOff & 0x3FF) + (y - (L->bg1v & 0x3FF));
}
static long tiFloorDiv(long a, long b) { long q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }

// One output row of a mode-7 line with no window anywhere (the swing): the mode-7 sample walks the row incrementally at
// the sub-pixel; the sprites, the layer order and the colour math are the 1x pixel's, as tiGetPixel resolves them.
static void tiRowM7(const CtTitleIntro* t, const TiLine* L, const uint8_t* objPix, const uint8_t* objPrio,
                    long sxk, long syk, int k, int x0, int W, int bri, unsigned char* row) {
  const long D = 256L * k, A = L->m7[0], C = L->m7[2];
  const int mathEnabled[6] = { L->cgadsub & 1, (L->cgadsub >> 1) & 1, (L->cgadsub >> 2) & 1,
                               (L->cgadsub >> 3) & 1, (L->cgadsub >> 4) & 1, (L->cgadsub >> 5) & 1 };
  long dx0 = -(long)x0 * k;
  long u = sxk + A * dx0, v = syk + C * dx0;
  long xp = tiFloorDiv(u, D), ru = u - xp * D, yp = tiFloorDiv(v, D), rv = v - yp * D;
  long qa = tiFloorDiv(A, D), ra = A - qa * D, qc = tiFloorDiv(C, D), rc = C - qc * D;
  for (int X = 0; X < W; X++) {
    int m7px = 0;
    if (xp >= 0 && xp < CT_M7_FIELD && yp >= 0 && yp < CT_M7_FIELD) {
      int tile = tiM7Tile((int)(yp >> 3), (int)(xp >> 3));
      if (tile < CT_TITLE_M7_TILES) m7px = ctTitleM7Chr[tile * 64 + (yp & 7) * 8 + (xp & 7)];
    }
    xp += qa; ru += ra; if (ru >= D) { ru -= D; xp++; }
    yp += qc; rv += rc; if (rv >= D) { rv -= D; yp++; }
    int xw = X / k;                                   // (the sprites are evaluated over the wide picture)
    int obj = objPix[xw], op = objPrio[xw];
    // main screen: OBJ3/OBJ2/OBJ1, BG1, OBJ0, the backdrop
    int ml = 5, mp = 0;
    if ((L->tm & 0x10) && obj && op >= 1) { ml = 4; mp = obj; }
    else if ((L->tm & 0x01) && m7px) { ml = 0; mp = m7px; }
    else if ((L->tm & 0x10) && obj) { ml = 4; mp = obj; }
    uint16_t c = t->l.cgram[mp & 0xFF];
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    if (ml == 4 && mp < 0xC0) ml = 6;
    if (ml < 6 && mathEnabled[ml]) {
      int sl = 5, sp = 0;
      if (L->addSub) {
        if ((L->ts & 0x10) && obj && op >= 1) { sl = 4; sp = obj; }
        else if ((L->ts & 0x01) && m7px) { sl = 0; sp = m7px; }
        else if ((L->ts & 0x10) && obj) { sl = 4; sp = obj; }
      }
      int useSub = L->addSub && sl != 5;
      int r2 = 0, g2 = 0, b2 = 0;
      if (useSub) { uint16_t c2 = t->l.cgram[sp & 0xFF]; r2 = c2 & 31; g2 = (c2 >> 5) & 31; b2 = (c2 >> 10) & 31; }
      if (L->subtract) { r -= useSub ? r2 : L->fixedR; g -= useSub ? g2 : L->fixedG; b -= useSub ? b2 : L->fixedB; }
      else { r += useSub ? r2 : L->fixedR; g += useSub ? g2 : L->fixedG; b += useSub ? b2 : L->fixedB; }
      if (L->halfColor && (sl == 5 || !L->addSub)) { r >>= 1; g >>= 1; b >>= 1; }
      if (r > 31) r = 31; if (g > 31) g = 31; if (b > 31) b = 31;
      if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    }
    row[X * 3 + 0] = (unsigned char)((((r << 3) | (r >> 2)) * bri) / 15);
    row[X * 3 + 1] = (unsigned char)((((g << 3) | (g >> 2)) * bri) / 15);
    row[X * 3 + 2] = (unsigned char)((((b << 3) | (b >> 2)) * bri) / 15);
  }
}

// One wide-picture pixel at the view's column xv (the mode-7 sample handed in), the colour math and the brightness applied.
static void tiPixEx(const CtTitleIntro* t, const TiLine* Ly, int xv, int m7px, int inView, const uint8_t* objPixV,
                    const uint8_t* objPrioV, int bri, unsigned char* o) {
    const int mathEnabled[6] = { Ly->cgadsub & 1, (Ly->cgadsub >> 1) & 1, (Ly->cgadsub >> 2) & 1,
                                 (Ly->cgadsub >> 3) & 1, (Ly->cgadsub >> 4) & 1, (Ly->cgadsub >> 5) & 1 };
    int r, g, b, r2 = 0, g2 = 0, b2 = 0;
    int main = tiGetPixelEx(t, Ly, xv, 0, m7px, inView, objPixV, objPrioV, &r, &g, &b);
    int cw = tiWinState(Ly, 5, xv);
    if (Ly->clipMode == 3 || (Ly->clipMode == 2 && cw) || (Ly->clipMode == 1 && !cw)) r = g = b = 0;
    int me = main < 6 && mathEnabled[main] &&
             !(Ly->preventMath == 3 || (Ly->preventMath == 2 && cw) || (Ly->preventMath == 1 && !cw));
    if (me) {
      int second = 5;
      if (Ly->addSub) second = tiGetPixelEx(t, Ly, xv, 1, m7px, inView, objPixV, objPrioV, &r2, &g2, &b2);
      int useSub = Ly->addSub && second != 5;
      if (Ly->subtract) { r -= useSub ? r2 : Ly->fixedR; g -= useSub ? g2 : Ly->fixedG; b -= useSub ? b2 : Ly->fixedB; }
      else { r += useSub ? r2 : Ly->fixedR; g += useSub ? g2 : Ly->fixedG; b += useSub ? b2 : Ly->fixedB; }
      if (Ly->halfColor && (second == 5 || !Ly->addSub)) { r >>= 1; g >>= 1; b >>= 1; }
      if (r > 31) r = 31; if (g > 31) g = 31; if (b > 31) b = 31;
      if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    }
    o[0] = (unsigned char)((((r << 3) | (r >> 2)) * bri) / 15);
    o[1] = (unsigned char)((((g << 3) | (g >> 2)) * bri) / 15);
    o[2] = (unsigned char)((((b << 3) | (b >> 2)) * bri) / 15);
}

void CtTitleIntroRenderEx(const CtTitleIntro* t, const struct CtTiLatch* prev, int t256, unsigned char* out,
                          int w1, int x0, int k) {
  if (k < 1) k = 1;
  const int W = w1 * k, H = 224 * k;
  memset(out, 0, (size_t)W * H * 3);
  if (t->l.bri & 0x80) return;
  int bri = t->l.bri & 0x0F;
  TiLine L;
  memset(&L, 0, sizeof L);
  L.mode = t->mode; L.tm = t->tm; L.ts = t->ts; L.tmw = t->tmw; L.tsw = t->tsw;
  L.cgadsub = (uint8_t)(t->cgadsub & 0x3F);
  L.halfColor = t->halfColor; L.subtract = t->subtract; L.addSub = t->addSub;
  L.clipMode = t->clipMode; L.preventMath = t->preventMath;
  L.bg1sc = t->bg1sc; L.bg2sc = t->bg2sc; L.bg12nba = t->bg12nba; L.obsel = t->obsel;
  L.win1l = t->win1l; L.win1r = t->win1r; L.win2l = t->win2l; L.win2r = t->win2r;
  memcpy(L.win, t->win, sizeof L.win);
  L.bg1h = t->l.bg1h; L.bg1v = t->l.bg1v; L.bg3h = t->l.bg3h;
  for (int q = 0; q < 4; q++) L.m7[q] = t->l.m7[q];
  // the frames between: the matrix and the scroll t/256 of the way from the previous latch (mode 7 only -- it is the
  // layer sampled at the sub-pixel; a jump is a cut)
  // (mode 2: BG1's scroll alone -- the TRIGGER logo sliding in)
  if (prev && t256 < 256 && (t->mode == 7 || t->mode == 2)) {
    int dh = s13(t->l.bg1h) - s13(prev->bg1h), dv = s13(t->l.bg1v) - s13(prev->bg1v), jump = abs(dh) > 64 || abs(dv) > 64;
    if (t->mode == 7) for (int q = 0; q < 4; q++) if (abs(t->l.m7[q] - prev->m7[q]) > 0x80) jump = 1;
    if (!jump) {
      if (t->mode == 7) for (int q = 0; q < 4; q++) L.m7[q] = (int16_t)(t->l.m7[q] - (t->l.m7[q] - prev->m7[q]) * (256 - t256) / 256);
      L.bg1h = (uint16_t)(t->l.bg1h - dh * (256 - t256) / 256); L.bg1v = (uint16_t)(t->l.bg1v - dv * (256 - t256) / 256);
    }
  }
  // the widescreen: BG1's slide stretched by the right side's width, so it starts beyond the wider edge (and covers it in
  // the same frames: faster) and ends where the ROM's does; outside a slide, BG1 stays inside the 256 view (a parked logo
  // past the view's edge is not on the ROM's screen)
  const int side = w1 - 256 - x0, slide = t->mode == 2 && t->slideOn && side > 0 && t->slideEnd != t->slideFrom;
  if (slide) {
    long D = (long)t->slideEnd - t->slideFrom, h = (int16_t)(((L.bg1h & 0x3FF) ^ 0x200) - 0x200);
    long e = t->slideEnd, from = t->slideFrom;
    if ((D > 0 && h >= from && h <= e) || (D < 0 && h <= from && h >= e)) {
      long hw = e - (e - h) * (D + (D > 0 ? side : -side)) / D;
      L.bg1h = (uint16_t)(hw & 0x3FF);
    }
  }
  L.bg1Wide = slide && (L.bg1sc & 1); L.bg1WinLo = slide ? t->slideEnd - 128 : 0;
  // Every line's registers and sprites first, in the PPU's order (HDMA rewrites them line by line); then the lines are
  // independent and are drawn in parallel.
  static TiLine lines[224];
  enum { TI_WMAX = 1024 };
  static uint8_t objPixL[224][TI_WMAX], objPrioL[224][TI_WMAX];
  if (w1 > TI_WMAX) return;
  TiHdmaRun run[8];
  tiHdmaInit(t, run);
  for (int y = 0; y < 224; y++) {
    tiHdmaRun(t, run, &L);
    L.y = y + 1;
    lines[y] = L;
    memset(objPrioL[y], 0, (size_t)w1);
    tiEvalSpritesEx(t, y, objPixL[y], objPrioL[y], w1, x0);
  }
  int yy;
  #pragma omp parallel for schedule(dynamic, 8)
  for (yy = 0; yy < 224; yy++) {
    const int y = yy;
    const TiLine* Ly = &lines[y];
    const uint8_t* objPix = objPixL[y]; const uint8_t* objPrio = objPrioL[y];
    const uint8_t* objPixV = objPix + x0; const uint8_t* objPrioV = objPrio + x0;   // (indexed by the view's x)
    int line = y + 1;
    TiM7Start m0 = tiM7Starts(Ly, line & 0xFF), m1 = tiM7Starts(Ly, (line + 1) & 0xFF);
    int fast = Ly->mode == 7 && !Ly->tmw && !Ly->tsw && !Ly->clipMode && !Ly->preventMath;
    const int mathEnabled[6] = { Ly->cgadsub & 1, (Ly->cgadsub >> 1) & 1, (Ly->cgadsub >> 2) & 1,
                                 (Ly->cgadsub >> 3) & 1, (Ly->cgadsub >> 4) & 1, (Ly->cgadsub >> 5) & 1 };
    for (int j = 0; j < k; j++) {
      unsigned char* row = out + (size_t)(y * k + j) * W * 3;
      long sxk = (long)m0.sx * k + (long)(m1.sx - m0.sx) * j, syk = (long)m0.sy * k + (long)(m1.sy - m0.sy) * j;
      if (fast) { tiRowM7(t, Ly, objPix, objPrio, sxk, syk, k, x0, W, bri, row); continue; }
      if (Ly->mode != 7) {                             // the tiled modes have nothing below the pixel: one 1x row, widened
        if (j > 0) { memcpy(row, out + (size_t)(y * k) * W * 3, (size_t)W * 3); continue; }
        for (int xw = 0; xw < w1; xw++) {
          int xv = xw - x0, inView = xv >= 0 && xv < 256;
          if (!inView && !Ly->bg1Wide) continue;   // (black: the pillarbox)
          unsigned char px[3];
          tiPixEx(t, Ly, xv, 0, inView, objPixV, objPrioV, bri, px);
          for (int i = 0; i < k; i++) memcpy(row + ((size_t)xw * k + i) * 3, px, 3);
        }
        continue;
      }
      for (int X = 0; X < W; X++) {
        long dx = (long)X - (long)x0 * k;                // the view's x, in 1/k pixels
        int xv = (int)tiFloorDiv(dx, k), inView = xv >= 0 && xv < 256;
        if (!inView && Ly->mode != 7 && !Ly->bg1Wide) continue;   // (black: the pillarbox)
        int m7px = 0;
        if (Ly->mode == 7) {
          long xp = tiFloorDiv(sxk + (long)Ly->m7[0] * dx, 256L * k), yp = tiFloorDiv(syk + (long)Ly->m7[2] * dx, 256L * k);
          if (xp >= 0 && xp < CT_M7_FIELD && yp >= 0 && yp < CT_M7_FIELD) {
            int tile = tiM7Tile((int)(yp >> 3), (int)(xp >> 3));
            if (tile < CT_TITLE_M7_TILES) m7px = ctTitleM7Chr[tile * 64 + (yp & 7) * 8 + (xp & 7)];
          }
        }
        tiPixEx(t, Ly, xv, m7px, inView, objPixV, objPrioV, bri, row + X * 3);
      }
    }
  }
}

// ---- DEBUG/TEST: the HDMA walker, reported the way ct.exe --hdma reports the cartridge ---------
// Runs one frame of HDMA over the channels in `t` and returns the colour-math state each scanline
// is drawn with. It exists so the walker can be graded against the ORACLE'S OWN per-line trace
// before any stage that uses HDMA is ported -- machinery nothing exercises is machinery nothing
// grades. Line k of the output is scanline k+1, which is the first line the SNES draws.
int CtSeqHdmaTrace(const CtTitleIntro* t, CtSeqLineMath* out, int maxLines) {
  if (!t || !out || maxLines <= 0) return 0;
  TiLine L;
  memset(&L, 0, sizeof L);
  L.mode = t->mode; L.tm = t->tm; L.ts = t->ts; L.tmw = t->tmw; L.tsw = t->tsw;
  L.cgadsub = (uint8_t)(t->cgadsub & 0x3F);
  L.halfColor = t->halfColor; L.subtract = t->subtract; L.addSub = t->addSub;
  L.clipMode = t->clipMode; L.preventMath = t->preventMath;
  L.bg1sc = t->bg1sc; L.bg2sc = t->bg2sc; L.bg12nba = t->bg12nba; L.obsel = t->obsel;
  L.win1l = t->win1l; L.win1r = t->win1r; L.win2l = t->win2l; L.win2r = t->win2r;
  memcpy(L.win, t->win, sizeof L.win);
  L.bg1h = t->l.bg1h; L.bg1v = t->l.bg1v; L.bg3h = t->l.bg3h;
  for (int k = 0; k < 4; k++) L.m7[k] = t->l.m7[k];
  TiHdmaRun run[8];
  tiHdmaInit(t, run);
  for (int i = 0; i < maxLines; i++) {
    tiHdmaRun(t, run, &L);
    out[i].cgadsub = L.cgadsub; out[i].half = L.halfColor; out[i].sub = L.subtract;
    out[i].fr = L.fixedR; out[i].fg = L.fixedG; out[i].fb = L.fixedB;
    out[i].tm = L.tm; out[i].ts = L.ts; out[i].mode = L.mode;
  }
  return maxLines;
}
