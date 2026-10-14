// seq_race.h -- the motorcycle race's logic (sequence entry 0), native. See seq_race.c.
#ifndef CT_SEQ_RACE_H
#define CT_SEQ_RACE_H
#include <stdint.h>

// What the race sends to the sound processor and what it reads from the ROM:
//   `sfx`  = a mailbox command through $7E:377E ($1E00-$1E02, already written to WRAM by the caller),
//   `apu`  = the engine's pitch, written straight to the ports ($2141 = pitch, $2140 = $89),
//   `post` = a script's `F6` (C3/0BD4): $1E00-$1E03 are already in WRAM; the host runs C7/0004 on them,
//   `rom`  = the ROM image (HiROM file order), for the overlay's RNG table $C0:FE00 ($7E:3448).
// Any of them may be NULL.
typedef struct {
  void* ctx;
  void (*sfx)(void* ctx, uint8_t cmd, uint8_t p1, uint8_t p2);
  void (*apu)(void* ctx, uint8_t pitch);
  void (*post)(void* ctx);
  const uint8_t* rom;
  long romLen;
} CtRaceIo;

// $7E:485D, the race's per-frame task, over the 128 KB WRAM image `w` ($7E at 0, $7F at 0x10000; D = $0000). Returns the
// task that holds this slot afterwards: 0x485D (it stays), 0 (it removed itself, $39A7: Johnny crossed the line first) or
// another task address that replaces it ($39B1: 0x4677, the player crossed first).
unsigned CtRaceStep485D(uint8_t* w, const CtRaceIo* io);

// $7E:4339, the camera's angle (L/R on the story's call) and height (X/A), and the steering masks $00B6/$00B8.
void CtRaceTask4339(uint8_t* w);

// One visit of a $C3 script task -- $7E:3749 (page $0A00) or $7E:3754 (page $0B00): C3/0AFF's interpreter over the page in
// WRAM (IP at +$20, registers +$00..+$1E, the loop counters +$22.., the gosub return +$30, the op at +$F0). Runs to the
// first yield. Returns 0, or -1 if it met an op or a call this port does not have (named on stderr).
int CtRaceScript(uint8_t* w, unsigned page, const CtRaceIo* io);

#endif
