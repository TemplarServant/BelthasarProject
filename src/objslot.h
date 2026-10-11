// objslot.h -- FD/E39C, the twelve 12-byte palette-effect records at $7E:0520, as ONE service.
//
// The ROM ticks the same table from every mode that has one: the field's VM pass (C0/00CD `JSL FD/FFF7`),
// the battle's frame service (CD/09BF), the load and entry loops (C0/00E6, C0/1793, ...). The records are
// armed by whoever owns the mode -- the field's op 0x88 (C0/4919 / C0/4970), the battle's staged fades
// (CD/0C48) -- and each record walks its own accumulator and writes the LIVE CGRAM page $7E:2200 (the one
// the NMI uploads) from the BASE page $7E:2000. Before this module the battle carried the only port and the
// field had none, so every `88 4x` / `88 5x` in a scene script armed nothing and drew nothing.
#pragma once
#include <stdint.h>

typedef struct CtObjSlotIo {
  uint8_t* s;              // the $7E bank, 64 KB ($0520 records, $2000 base page, $2200 live page)
  // called after every range the service writes, with the same arguments (k < 0 = the base copied back,
  // kind 0 = FD/E64F, 1 = FD/E8D7) -- the battle's one-factor-per-palette view hangs off it. May be NULL.
  void (*onRange)(void* ctx, int dst, int cnt, int k, int kind);
  void* ctx;
  const char* tag;         // trace prefix ("fleeobj" in the battle); NULL = no per-record trace
  int frame;               // the frame number the trace prints
  // 1 = also run modes 1, 2, 3 and 8 (FD/E437, E532, E4E8, E485: the location's own colour animation, which FD/E292
  // loads into records 0-5, and op 0x88's `88 20` / `88 30` rotations). The field's VM pass sets it; the battle's
  // service leaves it 0 -- its records tick as before, since the battle's palettes are not graded against these arms.
  int colourModes;
  const uint8_t* rom;      // the ROM image (modes 1 and 8 read their colours from bank $F6, mode 8 its frame list
  long romLen;             //   from bank $FD); NULL = those two write nothing
} CtObjSlotIo;

// FD/E5ED's sub-mode dispatch (mode 5, the record's +6): WHICH 5-bit components FD/E64F's `(acc * c) >> 8` scales, the
// others copied from the base page as they are. Bits: 1 = R, 2 = G, 4 = B. 0 = FD/E64F (all three), 1 = E7C0 (B, G),
// 2 = E73A (B, R), 3 = E6A1 (B), 4 = E779 (R, G), 5 = E6D1 (G), 6 and above = E708 (R).
int CtObjSlotSubMask(int sub);
// The range callback's `kind` for a mode-5 range: 0 = all three components (sub-mode 0), 1 = FD/E8D7 (mode 4, toward
// white), CT_OBJSLOT_KIND_SUB | mask = only the components of CtObjSlotSubMask(sub) scaled.
#define CT_OBJSLOT_KIND_SUB 0x10
// ...and CT_OBJSLOT_KIND_WHITE | sub (1-6) = a mode-4 range of FD/E82C's sub-modes 1-6 (toward white on some components,
// 3 and 6 zeroing the third, 1 and 4 writing one byte of the word): not a function of one factor per palette.
#define CT_OBJSLOT_KIND_WHITE 0x20

// FD/E39C: one frame of the whole table.
void CtObjSlotService(CtObjSlotIo* io);

// FD/E292 (C0/0116 `JSL FD/FFF4`, the location load): records 0-5 from location `loc`'s palette animation set, the
// mode byte of records 6-11 cleared. `s` = the $7E bank.
void CtObjSlotLoad(uint8_t* s, const uint8_t* rom, long romLen, int loc);
