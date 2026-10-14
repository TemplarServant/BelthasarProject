// seq_race.h -- the motorcycle race's logic (sequence entry 0), native. See seq_race.c.
#ifndef CT_SEQ_RACE_H
#define CT_SEQ_RACE_H
#include <stdint.h>

// What the race sends to the sound processor: `sfx` = a mailbox command through $7E:377E ($1E00-$1E02, already written to
// WRAM by the caller), `apu` = the engine's pitch, written straight to the ports ($2141 = pitch, $2140 = $89). Either may be NULL.
typedef struct {
  void* ctx;
  void (*sfx)(void* ctx, uint8_t cmd, uint8_t p1, uint8_t p2);
  void (*apu)(void* ctx, uint8_t pitch);
} CtRaceIo;

// $7E:485D, the race's per-frame task, over the 128 KB WRAM image `w` ($7E at 0, $7F at 0x10000; D = $0000). Returns the
// task that holds this slot afterwards: 0x485D (it stays), 0 (it removed itself, $39A7: Johnny crossed the line first) or
// another task address that replaces it ($39B1: 0x4677, the player crossed first).
unsigned CtRaceStep485D(uint8_t* w, const CtRaceIo* io);

#endif
