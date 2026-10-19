// ============================================================================
// sound/mixer_dsp.h -- the vendored S-DSP's state (snes/dsp.c) in the native mixer's names: a mixer that takes over
// from it (ct.exe --mixreplay's segments; src/audio.c's sound machine adopting a seeded SPC, and its lockstep).
// ============================================================================
#ifndef CT_SOUND_MIXER_DSP_H
#define CT_SOUND_MIXER_DSP_H

#include "sound/mixer.h"

struct Dsp;
void ct_mixer_from_dsp(CtMixer* m, const struct Dsp* d);   // m->aram is left as it is

#endif
