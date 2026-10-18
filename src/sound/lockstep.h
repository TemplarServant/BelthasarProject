// ============================================================================
// sound/lockstep.h -- the C sound driver (sound/driver.c) run beside the original on the vendored SPC700, tick by tick.
//
// CT_SNDDRV_LOCKSTEP=1 on Belthasar or ct.exe: every CtAudio's SPC (src/audio.c) is watched. Each time the original
// leaves its timer-0 wait (the loop at $0299/$029C reaches $02A0) the C driver replays what the original took since
// the last tick -- the commands the loop took, the ports at each poll, the timer and S-DSP reads, the upload sessions'
// headers, bytes and ticks -- and the two are compared: the S-DSP writes in order (KON and KOFF among them), the
// status ports, the control writes, the S-DSP register file, and the driver's RAM byte for byte. A mismatch is
// reported and both are put level again (the C driver takes the original's state) so a run counts every tick.
// The report goes to stdout at exit (CT_SNDDRV_LOCKSTEP_OUT=<file> appends it there, `%d` = the process id).
// ============================================================================
#ifndef CT_SOUND_LOCKSTEP_H
#define CT_SOUND_LOCKSTEP_H

struct Apu;

int  CtSndLockstepWanted(void);              // CT_SNDDRV_LOCKSTEP is set
void CtSndLockstepAttach(struct Apu* apu);    // watch this SPC (before its IPL upload: the boot is the first sync)
void CtSndLockstepResync(struct Apu* apu);    // its state was replaced (a savestate): level again at the next tick
void CtSndLockstepReport(void);               // the summary line (also at exit)
long CtSndLockstepMismatches(void);

#endif
