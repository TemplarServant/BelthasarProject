// ============================================================================
// sound/machine.h -- the console's sound processor as the game runs it: the sound program in C (sound/driver.h: the
// boot ROM, the cartridge's driver, its upload receivers) on its own clock, the three timers, the four ports each way
// and the S-DSP (sound/mixer.h) over the same 64 KB of RAM. No SPC700: the program is C, timed by the listing's cycles.
//
// THE CLOCK is the SPC700's cycle count since power-on (apu.c's `cycles`). The host runs it like snes/apu.c's
// apu_runCycles: SndMachineRun(n) runs every instruction that starts before now + n and stops at the first one that
// starts at or after it (the program, a coroutine, gives the time back there), returning the cycles run. Between two
// runs the host reads the ports the program wrote and writes the ones it reads: the order is the vendored core's, so a
// handshake costs the same SPC time to the cycle.
//
// THE SOUND: the S-DSP makes a sample every 32 cycles (at cycles 0, 32, 64, ...); each run renders those before the
// stop and hands them to the sink, in order. The mixer is brought up to an access's cycle before every S-DSP access and
// every store the S-DSP may read, as the hardware interleaves them.
// ============================================================================
#ifndef CT_SOUND_MACHINE_H
#define CT_SOUND_MACHINE_H

#include <stdint.h>

#include "sound/driver.h"
#include "sound/mixer.h"

typedef struct SndCoro SndCoro;

typedef struct SndTimer {
  uint8_t  enabled, divider, target, counter;
  uint64_t next;        // the cycle of the next prescaler step (every 128 cycles; timer 2 every 16)
} SndTimer;

// THE SAVESTATE: a coroutine's stack cannot be written down, so the machine keeps a restore point -- its whole state at
// the main loop's top ($0299, between two passes: nothing of the program's is pending), or power-on -- and the host's
// inputs since (its port writes and its runs). Loading one is the restore point taken back and the inputs replayed:
// the program goes the same way to the same instruction, cycle for cycle. A new restore point is taken at the loop's
// top once the record has grown.
typedef struct SndLogEntry { uint64_t t; uint8_t kind, k, v; } SndLogEntry;   // 'W' port k = v; 'R' a run to cycle t
typedef struct SndRestore {
  int kind;             // 0 power-on, 1 the main loop's top
  uint64_t cycles, mixed;
  uint8_t ram[0x10000];
  CtMixer mx;
  SndTimer t[3];
  uint8_t in[4], out[4], romReadable;
  int16_t newest[1024 * 2]; uint32_t newestPos;
} SndRestore;

typedef struct SndMachine {
  SndDrv d;             // the program and the RAM
  CtMixer mx;           // the S-DSP (its RAM = d.m)
  SndTimer t[3];
  uint8_t in[4];        // the CPU's ports as the program reads them ($F4-$F7)
  uint8_t out[4];       // the program's ports as the CPU reads them ($2140-$2143)
  uint8_t romReadable;  // $F1 bit 7 (the boot ROM mapped at $FFC0)
  uint64_t mixed;       // samples rendered: sample k is the S-DSP's step at cycle 32k
  int16_t newest[1024 * 2];   // the last 1024 samples (snes/dsp.c's ring: dsp_getSamples' view)
  uint32_t newestPos;
  void (*sink)(void* ctx, const int16_t* lr, int n);   // the samples, as rendered
  void* sinkCtx;
  int sinkPerSample;    // the sink called for each sample (the lockstep looks at the voices after each)
  int traces;           // the machine is the process's S-DSP for the debug traces (CT_DSP_KONTRACE, CT_DSP_RAWPCM)
  SndCoro* co;
  int started;
  // an observer of every access the outside sees (the lockstep): 'D' S-DSP write, 'd' S-DSP read, 'P' port write,
  // 'p' port read, 't' timer read, 'C' control write ($F1, $FA-$FC); `a` the register (or port / timer number)
  void (*obs)(void* ctx, char kind, uint8_t a, uint8_t v, uint64_t io);
  void (*ramObs)(void* ctx, uint16_t adr, uint8_t v, uint64_t io);   // each store the S-DSP may read (same ctx)
  void* obsCtx;
  SndRestore* restore;  // the savestate's restore point and the host's inputs since
  SndLogEntry* log; long logN, logCap;
  int replaying;
} SndMachine;

// The state a machine is handed at its main loop's top ($0299) -- a running driver's (src/audio.c's seeding from an
// oracle state, the lockstep): the cycle, the ports, the timers, the RAM and the S-DSP.
typedef struct SndMachineState {
  uint64_t cycles;
  uint8_t in[4], out[4], romReadable;
  SndTimer t[3];
} SndMachineState;

SndMachine* SndMachineNew(const SndTiming* tim);   // powered on (SndMachinePowerOn)
void SndMachineFree(SndMachine* m);
// Power-on: the RAM and the ports zero, the timers off, the S-DSP reset, the program at the boot ROM after the
// SPC700's reset sequence (8 cycles).
void SndMachinePowerOn(SndMachine* m);
// A running driver handed over at $0299: `ram` its 64 KB, `mx` its S-DSP (aram is re-pointed), `st` the rest.
void SndMachineAdopt(SndMachine* m, const uint8_t* ram, const CtMixer* mx, const SndMachineState* st);
void SndMachineSetSink(SndMachine* m, void (*sink)(void* ctx, const int16_t* lr, int n), void* ctx);
int  SndMachineRun(SndMachine* m, int cycles);              // apu_runCycles: returns the cycles run
void SndMachinePortWrite(SndMachine* m, int k, uint8_t v);   // the CPU's write of $2140+k
uint8_t SndMachinePortRead(const SndMachine* m, int k);      // the CPU's read of $2140+k
uint64_t SndMachineCycles(const SndMachine* m);
void SndMachineSync(SndMachine* m);                         // the S-DSP rendered up to the next instruction's start
void SndMachineNewest(const SndMachine* m, int16_t* out, int n);   // the newest n samples, oldest first (n <= 1024)
int  SndMachineVoicesSounding(const SndMachine* m);          // voices with a nonzero envelope
// The savestate (see SndRestore): written to / read from a buffer the caller frees (free()). Load returns 0 on a bad one.
int  SndMachineSave(const SndMachine* m, uint8_t** buf, long* len);
int  SndMachineLoad(SndMachine* m, const uint8_t* buf, long len);
// The machine's trace of each instruction it charges, and of each access (the lockstep): NULL to stop.
void SndMachineSetTrace(SndMachine* m, void (*trace)(void* ctx, uint16_t pc, uint64_t cyc), void* ctx);
void SndMachineSetObserver(SndMachine* m, void (*obs)(void* ctx, char kind, uint8_t a, uint8_t v, uint64_t io), void* ctx);

#endif
