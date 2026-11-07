// seq_stage.c -- the $C3 stage machine every sequence entry shares (disasm/notes/sequence_table.md sec. 12 and 13): the
// script interpreter C3/0AFF, the overlay and the common init (C3:0000 -> $7E:3000), the stage NMI C3/08D0 and its song
// check C3/09A4, the teardown, the overlay's brightness tasks, its sine and its multiply, the curtain C3/01E4 and the
// attract's button task. Each entry's own setup and tasks run on it: the race (seq_race.c), the ending (seq_ending.c),
// entries 3, 4, 6 and 7 (seq_entry*.c). The overlay's routines over the bare image that several entries call (the RNG
// C3/02DD, the stars, the mode-7 matrix) are seq_stagelib.c.
//
// As the race's tasks, it works on the WRAM image itself (D = $0000), so a frame can be compared with the oracle byte for
// byte (tools/race_stage_cmp.py).
#include "seq_race.h"     // CtRaceIo and the shared machine's declarations
#include "romview.h"     // the ROM's tables from ct_assets.dat (CT_ASSET_TABLE)
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lz.h"       // the overlay: C3:0000 decompresses it with C3/0557
#include "seq_entry4.h"    // CtSeqRng02DD8: C3/02DD, the curtain's seed (seq_stagelib.c)
#include "seq_stage_int.h"   // R, rd / wr / neg, the task-list library, pgw16 (shared with seq_race.c)

// ---- The $C3 script interpreter (C3/0AFF -> C3/0B03), over its register page in WRAM ----
// The same machine title_intro.c runs for the title (sequence_table.md sec. 8.1), here over the page itself, because the
// race's tasks read the registers straight from WRAM ($0A0A, $0A00, $0B1A, ...) and the race's own task writes the IP ($0A20).
// Encoding (C3/0B03): bit 7 clear = an assignment (low nibble = the register; high nibble with bit 5 clear = a 16-bit
// immediate, set when it is 0 and added otherwise; bit 5 set = a register named by the next byte, set when $20 and added
// otherwise); $8n / $9n = loop n; anything else dispatches on its low nibble through C3:0B74.
static unsigned pg16(const R* r, unsigned page, unsigned off) { return rd(r, page + off); }

int CtSeqScript(uint8_t* w, unsigned page, const CtRaceIo* io, CtSeqCall call) {
  R rr = { w, 0, io }, *r = &rr;
  for (int guard = 0; guard < 4096; guard++) {
    unsigned ip = pg16(r, page, 0x20);
    unsigned a = rd(r, ip);                                              // 0B03 LDA ($20) -- sixteen bits
    pgw16(r, page, 0x20, ip + 1);                                        // 0B05 INC $20
    if (!(a & 0x0080)) {                                                 // an assignment
      unsigned reg = page + (a & 0x000F) * 2;                            // 0B0C .. 0B16 $F0 = D + n * 2
      pgw16(r, page, 0xF0, reg);
      unsigned hi = a & 0x00F0, v;
      ip = pg16(r, page, 0x20);
      if (!(hi & 0x0020)) {
        v = (hi == 0) ? rd(r, ip) : (rd(r, reg) + rd(r, ip)) & 0xFFFF;   // 0B26 / 0B2A
        pgw16(r, page, 0x20, ip + 2);                                    // 0B4D INC $20 / 0B4F INC $20
      } else {
        unsigned x = rd(r, ip) & 0x001E;                                 // 0B36 / 0B40 `LDA ($20) / AND #001E / TAX`
        v = (hi == 0x20) ? rd(r, page + x) : (rd(r, reg) + rd(r, page + x)) & 0xFFFF;
        pgw16(r, page, 0x20, ip + 1);                                    // 0B4F
      }
      wr(r, reg, v);                                                     // 0B51 STA ($F0)
      continue;
    }
    pgw16(r, page, 0xF0, a);                                             // 0B55 STA $F0
    unsigned hi = a & 0x00F0;
    if (hi == 0x0080 || hi == 0x0090) {                                  // C3/0C92, the loop counters
      unsigned n = (a & 0x000F) * 2, ctr = pg16(r, page, 0x22 + n);
      ip = pg16(r, page, 0x20);
      if (ctr && !neg(ctr - rd(r, ip))) {                                // 0C9B BEQ / CMP ($20) / BMI
        pgw16(r, page, 0x22 + n, 0);
        pgw16(r, page, 0x20, ip + 4);                                    // 0CA3 past the limit and the target
      } else {
        pgw16(r, page, 0x22 + n, ctr + 1);                               // 0CAC INC A / STA $22,X
        pgw16(r, page, 0x20, rd(r, ip + 2));                             // 0CAF the target
      }
      if (hi == 0x0090) return 0;                                        // $9n: C3/0B67 JMP $0C92 -- a yield
      continue;                                                          // $8n: C3/0C8C, on to the next op
    }
    ip = pg16(r, page, 0x20);
    switch (a & 0x000F) {
      case 0xF:                                                          // 0B95 END: DEC $20 / $7E:0000 = 0001
        pgw16(r, page, 0x20, ip - 1); wr(r, 0x0000, 0x0001); return 0;
      case 0xE: pgw16(r, page, 0x20, rd(r, ip)); continue;               // 0B9F JUMP
      case 0xD: pgw16(r, page, 0x20, rd(r, ip)); return 0;               // 0BA6 JUMP and yield
      case 0xC: return 0;                                                // 0BAA yield
      case 0xA: taskAdd(r, rd(r, ip)); pgw16(r, page, 0x20, ip + 2); continue;   // 0BAB -> C3/0CE2
      case 0x9: taskDel(r, rd(r, ip)); pgw16(r, page, 0x20, ip + 2); continue;   // 0BBC -> C3/0CB8
      case 0x8: vblAdd(r, rd(r, ip)); pgw16(r, page, 0x20, ip + 2); continue;    // 0BC4 -> C3/0CF8
      case 0x7: {                                                        // 0BCC -> C3/0CCD: the hook out of $0940
        unsigned x = rd(r, ip);
        for (unsigned i = 0; i < 0x1000; i += 2) if (rd(r, 0x0940 + i) == x) { wr(r, 0x0940 + i, 0); break; }
        pgw16(r, page, 0x20, ip + 2); continue;
      }
      case 0x6:                                                          // 0BD4 the sound command
        wr(r, 0x1E00, rd(r, ip)); wr(r, 0x1E02, rd(r, ip + 2));
        pgw16(r, page, 0x20, ip + 4);
        if (!w[0x0388] && io && io->post) io->post(io->ctx);           // 0BE4 LDA $0388 / BNE / JSL C7/0004
        continue;
      case 0x1:                                                          // 0C19 a byte into bank 0 (the low-RAM mirror)
        w[rd(r, ip) & 0x1FFF] = w[(ip + 2) & 0xFFFF];
        pgw16(r, page, 0x20, ip + 3); continue;
      case 0x2:                                                          // 0C41 a word into bank 0
        wr(r, rd(r, ip) & 0x1FFF, rd(r, ip + 2));
        pgw16(r, page, 0x20, ip + 4); continue;
      case 0x3:                                                          // 0C7C GOSUB: $30 = IP + 2, then JUMP
        pgw16(r, page, 0x30, ip + 2); pgw16(r, page, 0x20, rd(r, ip)); continue;
      case 0x4: pgw16(r, page, 0x20, pg16(r, page, 0x30)); continue;     // 0C85 RETURN
      case 0x5: {                                                        // 0C2A wait for the song: port 2's low nibble
        if (!io || !io->port) { pgw16(r, page, 0x20, ip - 1); return 0; }   //   (no sound processor: the marker never comes)
        uint8_t v = (uint8_t)(io->port(io->ctx, 2) & 0x0F);             // 0C2C LDA $2142 (read until stable) / AND #0F
        if ((int8_t)(uint8_t)(v - w[ip & 0xFFFF]) >= 0) {                 // 0C38 CMP ($20) / BPL -> 0C28 -> 0BB5
          pgw16(r, page, 0x20, ip + 1); continue;
        }
        pgw16(r, page, 0x20, ip - 1); return 0;                          // 0C3E DEC $20 / RTS: again next frame
      }
      case 0xB:                                                          // 0C5F a byte into the PPU: $21[ip+1] = [ip]
        if (io && io->ppu) io->ppu(io->ctx, w[(ip + 1) & 0xFFFF], w[ip & 0xFFFF]);
        if (io && io->pic) CtSeqPicWrite(&io->pic->p, w[(ip + 1) & 0xFFFF], w[ip & 0xFFFF], 0);
        pgw16(r, page, 0x20, ip + 2); continue;
      case 0x0: {                                                        // 0C51 CALL: $7E:00F0 = the target, JSL $7E:37F3
        unsigned tgt = rd(r, ip);
        wr(r, 0x00F0, tgt);
        if (!call || call(w, page, tgt, io) < 0) { fprintf(stderr, "seqscript: call $%04X is not ported\n", tgt); return -1; }
        pgw16(r, page, 0x20, pg16(r, page, 0x20) + 2);                   // 0BB1 INC $20 / 0BB5 INC $20
        continue;
      }
      default:
        fprintf(stderr, "seqscript: op %02X at $%04X is not ported\n", a & 0xFF, ip - 1);
        pgw16(r, page, 0x20, ip - 1);
        return -1;
    }
  }
  return 0;
}

// ---- The stage: C3:0000 -> $7E:3000 -> the NMI ----
// The machine every entry shares (the overlay, the common init, the NMI, the brightness tasks): the logic that decides how
// a stage plays and when it ends; each entry's own setup and tasks are its own file's (the race's, seq_race.c). The
// picture's work (the setups' packets and DMAs, the render tasks, the vblank hooks) is in seq_racepic.c and the entries'
// *pic.c files. sequence_table.md sec. 12 (the race) and sec. 13 (the ending).

int g_ctRaceOpenNmi;   // 1 while a stage's Open runs the setup's last NMI (seq_race.h)

// The overlay, decompressed where C3:0000 puts it: `LDA $FE0003` (= C3:1313) -> $0300, `$7E:3000` -> $0303, JSL C3/0557.
static int seqOverlay(uint8_t* w, const CtRaceIo* io) {
  if (!io || !io->rom) return -1;
  const uint8_t* p = CtRomPtr(io->rom, io->romLen, 0x3E0003, 3);    // the $FE:0000 table (CT_ASSET_TABLE)
  if (!p) return -1;
  uint32_t a = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
  size_t n = ct_lz_decompress_bounded(io->rom, (size_t)io->romLen, w + 0x3000, 0x10000 - 0x3000, ct_lz_hirom_offset(a));
  w[0x0300] = p[0]; w[0x0301] = p[1]; w[0x0302] = p[2];
  w[0x0303] = 0x00; w[0x0304] = 0x30; w[0x0305] = 0x7E;
  return n ? 0 : -1;
}

// $7E:3177 -- the driver reset (five commands through $377E; sequence_table / title_intro.c CtTitleIntroDriverReset), as WRAM
// leaves it: each command's bytes in $1E00-$1E02, the last one `81 00 FF`, posted by the host. `LDA $0384 / BIT #20 / BNE`:
// an entry called with bit 5 keeps the song the field left playing (the ending's `FF 2A`..`FF 2F`).
static void seqDriverReset(R* r, const CtRaceIo* io) {
  static const uint8_t cmd[5][3] = { { 0x85, 0x00, 0x00 }, { 0x89, 0x00, 0x00 }, { 0xF2, 0x00, 0x00 },
                                     { 0xF2, 0x01, 0x00 }, { 0x81, 0x00, 0xFF } };
  if (r->w[0x0384] & 0x20) return;                                    // 3177 LDA $0384 / BIT #20 / BNE -> RTS
  for (int k = 0; k < 5; k++) {
    if (k == 0) { r->w[0x1E02] = 0; r->w[0x1E03] = 0; }              // STZ $1E02 (16-bit)
    r->w[0x1E00] = cmd[k][0];
    if (k == 0 || k == 3 || k == 4) r->w[0x1E01] = cmd[k][1];
    if (k == 4) r->w[0x1E02] = cmd[k][2];
    if (io && io->post) io->post(io->ctx);
    r->w[0x1E00] = cmd[k][0];                                         // $377E never clears the mailbox (title_intro.c)
  }
}

int CtSeqStageBegin(uint8_t* w, uint8_t operand, const CtRaceIo* io) {
  R rr = { w, 0, io }, *r = &rr;
  // C3:0014 -- the vectors ($0500-$0507: JML $C3:0548 / $C3:0529), the operand into $0384, the overlay
  w[0x0500] = 0x5C; w[0x0501] = 0x48; w[0x0502] = 0x05; w[0x0503] = 0xC3;
  w[0x0504] = 0x5C; w[0x0505] = 0x29; w[0x0506] = 0x05; w[0x0507] = 0xC3;
  w[0x0384] = operand;
  if (seqOverlay(w, io) < 0) return -1;
  // $7E:3063 -- the common init's WRAM: the shadows, then $0000-$00FF and $0900-$1DFF cleared (a DMA out of MPYL = 0)
  w[0x038E] = 4; w[0x038F] = 4;
  static const uint16_t z[] = { 0x0380, 0x0381, 0x0390, 0x0392, 0x0394, 0x0391, 0x0393, 0x0395, 0x0396, 0x0388, 0x038D, 0x038C, 0x0366 };
  for (unsigned k = 0; k < sizeof z / sizeof z[0]; k++) w[z[k]] = 0;
  memset(w + 0x0000, 0, 0x100);
  // 30BD LDA $0384 / BIT #80 / BEQ: with bit 7 (entry 5's A = 85, C0/260E) the clear starts at $0920 and the init ENDS there
  // (30D7 RTS): no song test, no driver reset, no OAM shadow, no $13, no PPU reset -- the field's song plays on.
  if (operand & 0x80) { memset(w + 0x0920, 0, 0x14E0); return 0; }
  memset(w + 0x0900, 0, 0x1500);
  if (!(operand & 0x20)) { w[0x038A] = 0; seqDriverReset(r, io); }   // 30E9 BIT #20 / BNE / STZ $038A / JSR $3177
  memset(w + 0x0700, 0xE0, 0x200);                                    // JSR $3843: the OAM shadow, every sprite off screen
  wr(r, 0x13, 0x0900);                                                // 30F9 LDX #0900 / STX $13
  if (io && io->pic) {                                                // 3102-3170: the PPU reset through D = $2100 (8-bit A = 0, X = 0)
    static const uint8_t seq[] = { 0x00, 0x80, 0x01, 0x02, 0x32, 0xE0, 0x02, 0, 0x03, 0, 0x05, 0, 0x06, 0, 0x07, 0, 0x08, 0, 0x09, 0, 0x0A, 0,
      0x0B, 0, 0x0C, 0, 0x0D, 0, 0x0D, 0, 0x0E, 0, 0x0E, 0, 0x0F, 0, 0x0F, 0, 0x10, 0, 0x10, 0, 0x11, 0, 0x11, 0, 0x12, 0, 0x12, 0,
      0x13, 0, 0x13, 0, 0x14, 0, 0x14, 0, 0x15, 0, 0x1A, 0, 0x1B, 0, 0x1B, 0, 0x1C, 0, 0x1C, 0, 0x1D, 0, 0x1D, 0, 0x1E, 0, 0x1E, 0,
      0x1F, 0, 0x1F, 0, 0x20, 0, 0x20, 0, 0x21, 0, 0x23, 0, 0x24, 0, 0x25, 0, 0x26, 0, 0x27, 0, 0x28, 0, 0x29, 0, 0x2A, 0, 0x2B, 0,
      0x2C, 0, 0x2D, 0, 0x2E, 0, 0x2F, 0, 0x30, 0, 0x31, 0, 0x32, 0, 0x33, 0 };
    for (unsigned k = 0; k < sizeof seq; k += 2) CtSeqPicWrite(&io->pic->p, seq[k], seq[k + 1], 0);
    io->pic->vmain = 0; io->pic->cgadd = 0;                           // ($2115 = 0, $2121 = 0)
  }
  return 0;
}

// C3/08D0 -- one NMI of the stage, its logic: INC $08, the task list into its snapshot $0950, the pad (C3/0A8E: $02, the
// pause on Start's edge once $0390 allows it, the soft reset on L+R+Select+Start unless $0396), then the tasks of the snapshot
// in order (7E:398A: sixteen slots, `CPX #0020`), each one able to remove or replace itself in the live list at its own index
// ($39A7 / $39B1). Paused ($0392), neither the hooks nor the tasks run.
int CtSeqStageNmi(uint8_t* w, uint16_t joy, const CtRaceIo* io, CtSeqTaskFn run) { return CtSeqStageNmiHooks(w, joy, io, NULL, run); }

int CtSeqStageNmiHooks(uint8_t* w, uint16_t joy, const CtRaceIo* io, CtSeqTaskFn hook, CtSeqTaskFn run) {
  R rr = { w, 0, io }, *r = &rr;
  CtSeqStagePic* pic = io ? io->pic : NULL;
  if (pic) { pic->hdmaen = 0; CtSeqPicWrite(&pic->p, 0x00, 0x80, 0); } // 08DC STZ $420C / 08ED LDA #80 / STA $2100
  if (hook && !rd(r, 0x0392))                                         // 08F2 LDA $0392 / BNE / JSL $7E:39BE
    for (unsigned x = 0; x < 0x10; x += 2) {                          // 39C0 .. 39D8 CPX #0010
      unsigned h = rd(r, 0x0940 + x);
      if (!h) continue;
      unsigned k = hook(w, x, h, io);
      if (k != h) wr(r, 0x0940 + x, k);                               // 39E3 STZ / 39F0 STA $0940,X
    }
  if (pic) {                                                          // 08FD LDA $038C / STA $420C, 0903 $038D or $80 -> $2100
    pic->hdmaen = w[0x038C];
    CtSeqPicWrite(&pic->p, 0x00, w[0x038D] ? w[0x038D] : 0x80, 0);
  }
  wr(r, 0x08, rd(r, 0x08) + 1);                                       // 090F INC $08
  memmove(w + 0x0950, w + 0x0920, 0x20);                              // 0911 MVN $0920 -> $0950, 32 bytes
  wr(r, 0x02, joy);                                                   // 0A99 LDA $4218 / STA $02
  if (pic) pic->joy = joy;
  if (joy == 0x3030 && !w[0x0396]) return 2;                          // 0A9E the soft reset (JML $00FF00)
  if (rd(r, 0x0390)) {                                                // 0ABB the pause
    if (w[0x03] & 0x10) {
      if (!rd(r, 0x0394)) {
        wr(r, 0x0394, rd(r, 0x0394) + 1);
        unsigned p = (rd(r, 0x0392) + 1) & 1;
        wr(r, 0x0392, p);
        if (p) { wr(r, 0x1E00, 0x00F2); }                             // 0ADB `F2` -- JSL $377A
        else { wr(r, 0x1E00, 0x4418); wr(r, 0x1E02, 0x0080); }        // 0AE7 `18 44 80 00`
        if (io && io->post) io->post(io->ctx);
      }
    } else wr(r, 0x0394, 0);
  }
  if (w[0x0392] && pic) {                                             // 0927 paused: $038D / 2, or $80
    unsigned b = w[0x038D] >> 1; CtSeqPicWrite(&pic->p, 0x00, (uint8_t)(b ? b : 0x80), 0);
  }
  if (!rd(r, 0x0392)) {                                               // 0922 LDA $0392 / BEQ -> JSL $7E:398A
    for (unsigned x = 0; x < 0x20; x += 2) {
      unsigned t = rd(r, 0x0950 + x);
      if (!t || !run) continue;
      unsigned k = run(w, x, t, io);
      if (k != t) wr(r, 0x0920 + x, k);
    }
  }
  return rd(r, 0x0000) ? 1 : 0;
}

int CtSeqStageSong(uint8_t* w, const CtRaceIo* io) {
  if (w[0x0388]) return 0;                                            // 09A6 LDA $0388 / BNE
  if (w[0x0384] & 0x20) return 0;                                     // 09AB LDA $0384 / BIT #20 / BNE
  if (w[0x038A] == w[0x0389]) return 0;                               // 09B2 LDA $038A / CMP $0389 / BEQ
  w[0x0389] = w[0x038A];                                              // 09BA STA $0389
  w[0x0388]++;                                                        // 09BD INC $0388
  w[0x1E00] = 0x10; w[0x1E01] = w[0x038A]; w[0x1E02] = 0xFF; w[0x1E03] = 0xFF;   // 09C0 .. 09CE
  int n = 0;
  if (io && io->post) { io->post(io->ctx); if (io->postFrames) n = io->postFrames(io->ctx); }   // 09D1 JSL C7/0004
  if (n <= 0) { w[0x0388] = 0; return 0; }                            // 09D5 STZ $0388
  return n;                                                           //   (after the frames the upload holds)
}

int CtSeqStageSongTick(uint8_t* w, const CtRaceIo* io, int hold) {
  if (hold > 0) {                                                     // the post's JSL still in the driver's handshake
    if (--hold == 0) w[0x0388] = 0;                                   // 09D5 STZ $0388, once it returns
    return hold;
  }
  return CtSeqStageSong(w, io);
}

void CtSeqStageEnd(uint8_t* w, const CtRaceIo* io) {
  R rr = { w, 0, io };
  seqDriverReset(&rr, io);
}

unsigned CtSeqTaskFade(uint8_t* w, unsigned slot, unsigned task, const CtRaceIo* io) {
  (void)slot; (void)io;
  if (--w[0x038F]) return task;                                       // DEC $038F / BNE -> RTS
  w[0x038F] = w[0x038E];                                              // LDA $038E / STA $038F
  switch (task) {
    case 0x347A: return ++w[0x038D] == 0x0F ? 0 : task;               // 3487 INC $038D / CMP #0F / BEQ -> JMP $39A7
    case 0x3495: return --w[0x038D] == 0 ? 0 : task;                  // 34A2 DEC $038D / BEQ -> JMP $39A7
    case 0x34E3: if (--w[0x038D]) return task;                        // 34F0 DEC $038D / BEQ
                 w[0x0000]++; return 0;                               // 34F6 INC $00 / JMP $39A7: the END
    default: return task;
  }
}

unsigned CtSeqSine(const CtRaceIo* io, unsigned a) {
  unsigned x = a & 0x03FF;
  unsigned v = io ? CtRomByte(io->rom, io->romLen, 0xF900 + x) : 0;
  return x >= 0x0200 ? v : ((v ^ 0xFFFF) + 1) & 0xFFFF;
}

void CtSeqMul31BF(uint8_t* w, unsigned d) {
  R rr = { w, 0, NULL }, *r = &rr;
  unsigned f0 = rd(r, d + 0xF0), f2 = rd(r, d + 0xF2);
  unsigned a0 = f0 & 0xFF, a1 = f0 >> 8, b0 = f2 & 0xFF, b1 = f2 >> 8;
  unsigned p1 = b0 * a0, p2 = b0 * a1, p3 = b1 * a0, p4 = b1 * a1;
  wr(r, d + 0xF6, 0);                                                 // 31D5 STZ $F6 (16-bit: $F6, $F7)
  wr(r, d + 0xF4, p1);                                                // 31DD STA $F4
  unsigned s = p2 + p3;                                               // 31EE CLC / ADC $4216
  unsigned c = s > 0xFFFF; s &= 0xFFFF;
  { unsigned wd = rd(r, d + 0xF7); unsigned nc = (wd >> 15) & 1;     // 31F2 ROL $F7 (16-bit: $F7, $F8)
    wr(r, d + 0xF7, ((wd << 1) | c) & 0xFFFF); c = nc; }
  s = s + rd(r, d + 0xF5) + c; c = s > 0xFFFF; s &= 0xFFFF;          // 31F7 ADC $F5
  wr(r, d + 0xF5, s);                                                 // 31F9 STA $F5
  s = p4 + rd(r, d + 0xF6) + c; s &= 0xFFFF;                          // 31FB LDA $4216 / ADC $F6
  wr(r, d + 0xF6, s);                                                 // 3200 STA $F6
}

// C3/01E4 (through C3/000E) -- the curtain: one call a frame while `$0350` (D = $0300: $50) is 1 (open) or 2 (close);
// sequence_table.md sec. 18 walks it. Note: the close's clamp is `TDC`, which leaves B = $03, so the next refresh's `TAY`
// takes Y = $03xx and the rest of the buffer walks on one step. The caller's data bank is $00, so C3/02DD's absolutes are
// low RAM and the virtual beam (CtSeqRng02DD8). The field's window wipe has its own port of this routine (src/wipe.c).
void CtSeqCurtain01E4(uint8_t* w, unsigned x, const CtRaceIo* io) {
  uint8_t* d = w + 0x0300;
  if (!d[0x50]) { d[0x51] = 0; d[0x50] = 0; return; }                // 01F2 LDA $50 / BEQ -> 023B
  uint8_t a, b = 0;
  if (d[0x51]) a = d[0x51];                                           // 01F6 LDA $51 / BNE -> 0234
  else {
    d[0xF0] = (d[0x50] == 1) ? 0x00 : 0xFF;                           // 01FA STZ $F0 / CMP #01 / LDA #FF / STA $F0
    unsigned bank = (d[0x54] & 1) ? 0x10000 : 0;                      // 0211 LDA $54 / LSR A / BCC: $7F or $7E
    unsigned s = (unsigned)(d[0x52] | (d[0x53] << 8)) + 0x01C0;       // 0206 REP #21 / LDA $52 / ADC #01C0 / TAX
    for (unsigned i = 0; i <= 0xDF; i++) w[bank | ((s + i) & 0xFFFF)] = d[0xF0];   // STA long (16-bit) / MVN, A = DE
    x = (s + 0xDF) & 0xFFFF;                                          //   X past the MVN's last source byte
    a = 0x00; b = 0x03;                                               // 0233 TDC (D = $0300)
  }
  a++; d[0x51] = a;                                                   // 0234 INC A / STA $51
  int c = a >= 0x20;                                                  // 0237 CMP #20 / BMI
  if (!((uint8_t)(a - 0x20) & 0x80)) { d[0x51] = 0; d[0x50] = 0; return; }   // 023B STZ $51 / STZ $50
  d[0x57] = 0xC0; d[0x56] = 0xFE;                                     // 0242-0248
  { static int st = -1; if (st < 0) st = getenv("CT_XLAT_SEEDTRACE") ? 1 : 0;   // the seed's inputs, as seq_xlat.c traces them
    if (st) fprintf(stderr, "seedtrace stage f0 v=0 h=0 A=00FE X=%04X C=%d D=0300 B=00 $0386=%02X $0351=%02X $0008=%02X "
                    "$00F0=%02X $00F2=%02X $00F4=%02X bus=21\n", x & 0xFFFF, c, w[0x0386], w[0x0351], w[0x0008], w[0x00F0],
                    w[0x00F2], w[0x00F4]); }
  d[0x55] = CtSeqRng02DD8(w, 0xFE, &c, x);                            // 024A JSL C3/02DD / STA $55
  unsigned base = (unsigned)(d[0x52] | (d[0x53] << 8));
  x = (base + 0x01C0) & 0xFFFF;                                       // 0252 LDA $52 / CLC / ADC #01C0 / TAX
  unsigned f4 = (x + 0x00E0) & 0xFFFF;                                // 0259 CLC / ADC #00E0 / STA $F4
  d[0xF4] = (uint8_t)f4; d[0xF5] = (uint8_t)(f4 >> 8);
  unsigned wm = (d[0x51] & 1) ? base : (base + 0x00E0) & 0xFFFF;      // 025F LDA $51 / LSR A / LDA $52 / BCS / ADC #00E0
  unsigned wb = (unsigned)(d[0x54] & 1) << 16;                        // 026E STA $2183 (bank $7E / $7F)
  wm |= wb;
  if ((d[0x54] & 0xFE) != 0x7E) { static int said; if (!said++) fprintf(stderr, "seqcurtain: UNDERIVED -- the state's bank $%02X\n", d[0x54]); }
  unsigned db = wb;                                                   // 0273 PHA / PLB: the walk's data bank is $54
  b = 0x00;                                                           // 0275 TDC / XBA: A = 03, B = 00
  unsigned y = 1;                                                     // 0277 LDY #0001
  int close = d[0x50] == 2;                                           // 027A LDA $50 / CMP #02 / BEQ
  c = d[0x50] >= 2;
  const uint8_t* rom = io ? io->rom : NULL; long romLen = io ? io->romLen : 0;
  #define FE(i) CtRomByte(rom, romLen, 0xFE00 + (long)(i))
  for (;;) {
    y = (y - 1) & 0xFFFF;                                             // 0280 / 02AE DEY / BNE
    if (!y) {
      uint8_t v = FE(d[0x55]); d[0x55] = v;                           // LDA [$55] / STA $55
      unsigned t = (unsigned)(v & 0x1F) + 3 + (unsigned)c; c = t > 0xFF; y = ((unsigned)b << 8) | (t & 0xFF);   // AND #1F / ADC #03 / TAY
      v = FE(d[0x55]); d[0x55] = v;
      t = (unsigned)(v & 0x03) + 8 + (unsigned)c; c = t > 0xFF; d[0xF2] = (uint8_t)t;   // AND #03 / ADC #08 / STA $F2
    }
    uint8_t m = w[db | x], r;
    if (!close) {
      unsigned t = (unsigned)m + d[0xF2] + (unsigned)c; c = t > 0xFF; r = (uint8_t)t;   // 0296 LDA $0000,X / ADC $F2
      if (c) r = 0xFF;                                                //   BCC / LDA #FF
    } else {
      int t = (int)m - (int)d[0xF2] - (1 - c); c = t >= 0; r = (uint8_t)t;   // 02C4 LDA $0000,X / SBC $F2
      if ((r & 0x80) && !c) { r = 0x00; b = 0x03; }                   //   BPL / BCS / TDC
    }
    w[wm] = r; wm = (wm + 1) & 0x1FFFF;                               // STA $002180
    w[db | x] = r;                                                    // STA $0000,X
    x = (x + 1) & 0xFFFF;                                             // INX / CPX $F4 / BNE
    c = x >= f4;
    if (x == f4) break;
  }
  #undef FE
}

// $7E:424D -- the attract's button (a task the attract's scripts add once their curtain is open; D = 0, A 8-bit): any pad bit
// in `$02` while no curtain runs (`$0350` = 0) starts the closing one -- `$0350` = the operand of 4258's `LDA #imm`, which the
// setup patched ($7E:4259: 02 for the race, 01 for entry 3) --, posts `18 BA 80` ($1E00-$1E02 through $377E, unless `$0388`),
// `$0399` = 32, `$7F:0062` = 0 (the world script then sends loc 1B1 to the title) and hands its slot to $4283, which counts
// `$0399` down and ENDs (`INC $00`). Returns what the slot holds afterwards.
unsigned CtSeqTaskAbort(uint8_t* w, unsigned task, const CtRaceIo* io) {
  if (task == 0x4283) {
    if (--w[0x0399]) return task;                                     // 4283 DEC $0399 / BNE -- 8-bit
    w[0x0000]++; return task;                                         // 4288 INC $00 (the task stays: the END tears down)
  }
  if (!(w[0x02] | w[0x03])) return task;                              // 424F LDX $02 / BEQ (X 16-bit)
  if (w[0x0350]) return task;                                         // 4253 LDA $0350 / BNE
  w[0x0350] = w[0x4259];                                              // 4258 LDA #imm / STA $0350
  if (!w[0x0388]) {                                                   // 425D LDA $0388 / BNE
    w[0x1E01] = 0xBA; w[0x1E02] = 0x80; w[0x1E00] = 0x18;            // 4262 LDX #80BA / STX $1E01 / LDA #18 / STA $1E00
    if (io && io->sfx) io->sfx(io->ctx, 0x18, 0xBA, 0x80);            // 426D JSR $377E (the ports, as the bump's)
  }
  w[0x0399] = 0x20; w[0x039A] = 0x00;                                 // 4270 LDX #0020 / STX $0399
  w[0x10062] = 0x00;                                                  // 4276 LDA #00 / STA $7F0062
  return 0x4283;                                                      // 427C LDX #4283 / JMP $39B1
}
