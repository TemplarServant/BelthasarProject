// sound/timing.h -- the SPC700's time, for the sound driver in C: what each of the driver's instructions costs.
//
// The sound processor's time is part of the game: the frames the CPU waits on it (a song upload, a command's echo) and
// where in a sample each S-DSP write lands. The C driver (sound/driver.c) reproduces it with a cost model counted from
// the listing: every instruction the cartridge's driver would execute is charged its SPC700 cycles, in order, and each
// access the outside sees (a port, a timer, an S-DSP register, a store into the sample memory) gets the cycle the
// instruction makes it on. The costs are the instruction set's (fixed per opcode, a taken branch 2 more); the table
// below holds them per address of the driver's code -- derived data, not the code (SndTimingBuild).
//
// The C driver marks its way through the cartridge's code with SND_AT(pc) at each basic block it enters (sound/
// driver.c); the walker charges the instructions from the last mark to the next: straight on, a conditional branch taken
// when the next mark is its target, a jump or call followed to its target, a return to the next mark.
#ifndef CT_SOUND_TIMING_H
#define CT_SOUND_TIMING_H

#include <stdint.h>

enum { SND_INS_PLAIN = 0, SND_INS_COND = 1, SND_INS_JUMP = 2, SND_INS_RETURN = 3, SND_INS_NONE = 0xFF };

typedef struct SndIns {
  uint8_t  kind;          // SND_INS_*: NONE = no instruction known at this address
  uint8_t  len;           // bytes
  uint8_t  cyc, cycTaken; // cycles (a conditional branch: not taken / taken)
  uint16_t target;        // a branch's or a jump's (a call's: the callee)
  uint8_t  nio;           // accesses to the SPC's registers $F0-$FF, in order
  uint8_t  ioIdx[4];      //   the access's cycle within the instruction (0 = the opcode fetch's)
  uint8_t  ioReg[4];      //   the register's low byte ($F4 = port 0 ...)
  uint8_t  ioWrite[4];    //   1 = a write
  uint8_t  nw;            // the instruction's writes to RAM (any address), in order
  uint8_t  wIdx[4];       //   their cycles within the instruction
} SndIns;

typedef struct SndTiming { SndIns ins[0x10000]; } SndTiming;

// Builds the table from a 64 KB image holding the driver's code (block 0 at $0200) and the IPL at $FFC0 -- each address
// of [lo, hi) taken as an instruction and run alone on the vendored SPC700 (snes/spc.c) to time it. ct.exe and the tools
// only; the game reads the table from the asset container.
int SndTimingBuild(SndTiming* t, const uint8_t* image, uint16_t lo, uint16_t hi);

// The asset (CT_ASSET_SND_TIMING, made at setup by `ct.exe --sndtiming` from the player's ROM): the entries of the
// instructions the program can reach -- block 0's from $0200 and its six dispatch tables, the boot ROM's from $FFC0 --
// and block 0's code map (which of its bytes are an instruction's: the container's block 0 keeps only the others, its
// data). "SNDT", u32 version 1, u16 block start, u16 block length, the map (a bit a byte, LSB first), u32 n, then n
// entries of u16 address + 24 bytes (SndIns in order).
// SndTimingExport: the asset from a 64 KB image (block 0 at blo, blen bytes; the boot ROM at $FFC0); a malloc'd buffer.
long SndTimingExport(const uint8_t* image, uint16_t blo, uint16_t blen, uint8_t** out);
int  SndTimingRead(SndTiming* t, const uint8_t* in, long len);   // zeroed first: an address not in it has len 0
// Block 0's code map from the asset: 1 if byte `adr` is part of an instruction (0 if not, or outside the block).
int  SndTimingIsCode(const uint8_t* in, long len, uint16_t adr);

#endif
