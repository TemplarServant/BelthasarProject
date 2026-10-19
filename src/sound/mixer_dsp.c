// sound/mixer_dsp.c -- ct_mixer_from_dsp (mixer_dsp.h).
#include "sound/mixer_dsp.h"

#include <string.h>

#include "dsp.h"

// The vendored state, field for field, in the mixer's names (a seeded segment starts mid-song).
void ct_mixer_from_dsp(CtMixer* m, const Dsp* d) {
  memcpy(m->regs, d->ram, sizeof m->regs);
  for (int i = 0; i < 8; i++) {
    const DspChannel* c = &d->channel[i];
    CtMixerVoice* v = &m->v[i];
    v->pitch = c->pitch; v->pitchCounter = c->pitchCounter; v->pitchMod = c->pitchModulation;
    memcpy(v->ring, c->decodeBuffer, sizeof v->ring);
    v->ringPos = c->bufferOffset; v->srcn = c->srcn; v->blockAdr = c->decodeOffset; v->blockByte = c->blockOffset;
    v->header = c->brrHeader; v->noise = c->useNoise; v->startDelay = c->startDelay;
    memcpy(v->rate, c->adsrRates, sizeof v->rate);
    v->envMode = c->adsrState; v->sustainLevel = c->sustainLevel; v->gainSustainLevel = c->gainSustainLevel;
    v->useGain = c->useGain; v->gainMode = c->gainMode; v->directGain = c->directGain; v->directValue = c->gainValue;
    v->preclamp = c->preclampGain; v->env = c->gain; v->keyOn = c->keyOn; v->keyOff = c->keyOff;
    v->out = c->sampleOut; v->volL = c->volumeL; v->volR = c->volumeR; v->echo = c->echoEnable;
  }
  m->counter = d->counter; m->evenSample = d->evenCycle;
  m->mute = d->mute; m->softReset = d->reset; m->echoWrites = d->echoWrites; m->noiseRate = d->noiseRate;
  m->dirAdr = d->dirPage; m->mainVolL = d->masterVolumeL; m->mainVolR = d->masterVolumeR;
  m->mixL = d->sampleOutL; m->mixR = d->sampleOutR; m->echoInL = d->echoOutL; m->echoInR = d->echoOutR;
  m->noise = d->noiseSample;
  m->echoVolL = d->echoVolumeL; m->echoVolR = d->echoVolumeR; m->feedback = d->feedbackVolume;
  m->echoAdr = d->echoBufferAdr; m->echoDelay = d->echoDelay; m->echoLength = d->echoLength;
  m->echoPos = d->echoBufferIndex; m->firPos = d->firBufferIndex;
  memcpy(m->fir, d->firValues, sizeof m->fir);
  memcpy(m->firL, d->firBufferL, sizeof m->firL); memcpy(m->firR, d->firBufferR, sizeof m->firR);
}

