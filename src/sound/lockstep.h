// sound/lockstep.h -- the sound machine in C (sound/machine.h) run beside the original on the vendored SPC700 and S-DSP,
// instruction by instruction.
//
// CT_AUDIO_SPC=lockstep on Belthasar or ct.exe (src/audio.c): every CtAudio runs both, with the same calls from the host
// (the same port writes, the same runs); the game hears the original. They are compared as they run:
//   - the instructions: each one the machine charges against the one the SPC700 starts, address and cycle;
//   - every access the outside sees, in order, with its value and cycle: the S-DSP writes and reads, the port writes and
//     reads, the timer reads, the control writes ($F1, $FA-$FC);
//   - where each run stops (the cycle) and the ports the CPU then reads;
//   - every sample the two S-DSPs make;
//   - the RAM, byte for byte (but the registers' mirror $F0-$FF), at each frame's end and each song load's.
// The machine joins at power-on, or (after a seeded or loaded SPC, or a mismatch) at the original's next pass over
// the main loop's top ($0299), adopting its state there. The report goes to stdout at exit (CT_SNDDRV_LOCKSTEP_OUT=<file>
// appends it there, `%d` = the process id).
#ifndef CT_SOUND_LOCKSTEP_H
#define CT_SOUND_LOCKSTEP_H

#include <stdint.h>

struct Apu;
struct SndMachine;

void CtSndLockstepAttach(struct Apu* apu, struct SndMachine* m, int atPowerOn);   // compare these two
void CtSndLockstepDetach(struct Apu* apu);
void CtSndLockstepAdoptNext(struct Apu* apu);       // the original's state was replaced: the machine joins at $0299
int  CtSndLockstepActive(const struct Apu* apu);    // the machine runs (it has joined)
// One run of both: the original ran to its stop (ranRef); call before the machine runs, then after it.
void CtSndLockstepRunStart(struct Apu* apu);
void CtSndLockstepRunEnd(struct Apu* apu, int ranRef, int ranMachine);
void CtSndLockstepSamples(struct Apu* apu, const int16_t* lr, int n, uint64_t firstIndex);   // the original's samples
void CtSndLockstepPorts(struct Apu* apu);           // the ports the CPU reads, compared
void CtSndLockstepRam(struct Apu* apu);             // the RAM, compared
int  CtSndLockstepWanted(void);                    // CT_AUDIO_SPC=lockstep
void CtSndLockstepReport(void);
long CtSndLockstepMismatches(void);
long CtSndLockstepCompared(void);                   // instructions compared

#endif
