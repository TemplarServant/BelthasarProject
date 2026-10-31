// sound/driver.h -- the sound processor's program as native C: the boot ROM ($FFC0, the IPL) and the cartridge's
// sound driver (Akao, block 0 of the $C7 upload, ARAM $0200-$1DEE) with its tick, sequencer, events, slides, voices,
// commands and upload session. The map and each routine's reading: disasm/notes/audio_spc_driver.md (listing:
// `python tools/spc700dis.py driver --follow 0200 --table 1554:60,1608:60,1680:16,16A0:16,16C0:32,1544:8`).
// src/sound/lockstep.c compares it against the original on the vendored SPC700, instruction by instruction.
//
// State: SndDrv::m, in the sound processor's own RAM layout (so the lockstep compares it byte for byte); driver.c names
// each field it uses (D_ / T_ / P_). The same 64 KB is the S-DSP's memory, given to the mixer by sound/machine.h.
// Time: each routine marks the driver's address as it runs (SndClockAt) and the walker charges the instructions since
// the last mark from the cost table (sound/timing.h), so every access the outside sees lands on the original's cycle.
// The outside: SndDrvHost (S-DSP registers, ports, timers, control registers, the RAM stores the S-DSP may read). The
// machine runs the program in a coroutine and stops it at an instruction boundary (SndClock.want).
#ifndef CT_SOUND_DRIVER_H
#define CT_SOUND_DRIVER_H

#include <setjmp.h>
#include <stdint.h>

#include "sound/timing.h"

typedef struct SndDrvHost {
  void* ctx;
  void    (*dspWrite)(void* ctx, uint8_t reg, uint8_t val);   // an S-DSP register write ($00-$7F) at SndClock.io
  uint8_t (*dspRead)(void* ctx, uint8_t reg);                  // an S-DSP register read ($00-$7F) at SndClock.io
  uint8_t (*portRead)(void* ctx, int k);                       // the CPU's port k ($F4+k) at SndClock.io
  void    (*portWrite)(void* ctx, int k, uint8_t val);         // the driver's port k at SndClock.io
  uint8_t (*timerRead)(void* ctx, int k);                      // timer k's counter ($FD+k): read, and so cleared
  void    (*control)(void* ctx, uint8_t reg, uint8_t val);     // $F1 (timers, port clears, the boot ROM), $FA-$FC
  void    (*ramWrite)(void* ctx, uint16_t adr, uint8_t val);   // a store the S-DSP may read, before it lands
  void    (*stuck)(void* ctx);                                 // the program cannot go on (an IPL jump elsewhere)
} SndDrvHost;

// The SPC700's time: the cycle the next instruction starts on and its address; the marks walk the code with the cost
// table. The program stops (boundary) before any instruction that starts at or after `want`.
typedef struct SndClock {
  const SndTiming* tim;
  uint64_t cyc;         // the next instruction's start (apu.c's count)
  uint16_t ip;          // ... and its address
  uint64_t want;        // the host's time: no instruction starts at or after it before boundary() returns
  void (*boundary)(void* ctx);   // called with cyc >= want (the machine yields there)
  void* boundaryCtx;
  uint64_t io;          // the cycle of the access being made (apu.c's count at it: the instruction's start + the
                        //   access's index + 1), for the host callbacks
  uint64_t insStart;    // the instruction making it
  uint16_t insPc;
  int err;              // 1 the walk met an address the table does not know or ran away, 2 an access the table does
  uint16_t errAt;       //   not list, 3 the IPL jumped elsewhere than the driver
  uint16_t errTo;
  void (*trace)(void* ctx, uint16_t pc, uint64_t cyc);   // each instruction charged (the lockstep)
  void* traceCtx;
  void (*loopTop)(void* ctx);   // the main loop's top ($0299) about to run: the program is between two passes
  void* loopTopCtx;
} SndClock;

// The program's entries: the boot ROM ($FFC0), the driver's main loop ($0299), the SPC700's reset (its 8 cycles --
// five reads, an idle, the vector's two -- then the boot ROM: power-on).
enum { SND_ENTRY_IPL = 1, SND_ENTRY_LOOP = 2, SND_ENTRY_RESET = 3 };

typedef struct SndDrv {
  uint8_t m[0x10000];   // the sound processor's RAM, in its own layout
  SndDrvHost host;
  SndClock clk;
  int entry;            // where SndDrvProgram starts
  jmp_buf jb;           // the jumps to the main loop (`MOV SP,#$FF / JMP $0299`) and to the boot ROM (`JMP $FFC0`)
} SndDrv;

// The walker (driver.c): the program is now at `pc` -- the instructions from the last mark charged up to it.
void SndClockAt(SndDrv* d, uint16_t pc);

void     SndDrvInit(SndDrv* d, const SndDrvHost* host);   // m, clk and entry are the caller's
uint8_t* SndDrvMem(SndDrv* d);
// The program, from d->entry (the IPL at clk.cyc, or the main loop's top at $0299 with the RAM a running driver's).
// Never returns: run it in a coroutine (sound/coro.h) and let SndClock.boundary give the time back.
void     SndDrvProgram(SndDrv* d);

#endif
