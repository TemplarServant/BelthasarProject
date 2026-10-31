// mixer.h -- the port's own S-DSP, the console's eight-voice sound mixer: BRR decoding, the Gaussian interpolation, the
// ADSR/GAIN envelopes, noise, pitch modulation, the echo with its 8-tap FIR, the main and echo volumes. A refactor of
// the vendored snes/dsp.c (LakeSnes, MIT) with the same output sample for sample (`ct.exe --mixreplay`,
// tools/mixer_replay.py). The model, stage by stage: disasm/notes/audio_mixer.md.
// No SPC700, timers or ports: the driver (sound/machine.h) writes the 128 registers through ct_mixer_write or the
// voice-level calls and pulls samples with ct_mixer_render. The 64 KB sound RAM (directory, BRR blocks, echo buffer)
// is the owner's, given by pointer; the mixer never allocates.
#ifndef CT_SOUND_MIXER_H
#define CT_SOUND_MIXER_H

#include <stdint.h>

typedef struct CtMixerVoice {
  // pitch
  uint16_t pitch;             // P (14 bits)
  uint16_t pitchCounter;      // the interpolation position: 4.12 fixed point over the decoded ring
  uint8_t  pitchMod;          // PMON bit: the previous voice's output bends this voice's pitch
  // BRR decoding
  int16_t  ring[12];          // three 4-sample groups of decoded BRR
  uint8_t  ringPos;           // where the next 4 decoded samples go (0, 4, 8)
  uint8_t  srcn;              // SRCN: the sample's directory entry
  uint16_t blockAdr;          // the BRR block being decoded (sound RAM address of its header)
  uint8_t  blockByte;         // the next data byte in the block (1..8)
  uint8_t  header;            // the block's header: shift, filter, loop, end
  uint8_t  noise;             // NON bit: the voice plays the noise generator
  uint8_t  startDelay;        // the key-on's 5-sample start
  // envelope
  uint8_t  rate[4];           // the attack, decay, sustain and GAIN rates (0..31, the counter table's index)
  uint8_t  envMode;           // 0 attack, 1 decay, 2 sustain, 3 release
  uint8_t  sustainLevel;      // ADSR2's SL
  uint8_t  gainSustainLevel;  // GAIN's bits 7-5 (the decay-to-sustain test reads them in GAIN mode)
  uint8_t  useGain;           // ADSR1 bit 7 clear: the GAIN register drives the envelope
  uint8_t  gainMode;          // GAIN bits 6-5: linear down, exp down, linear up, bent up
  uint8_t  directGain;        // GAIN bit 7 clear: the level is written directly
  uint16_t directValue;       // GAIN bits 6-0 x 16
  uint16_t preclamp;          // the last level before the clamp (the bent line's knee)
  uint16_t env;               // the envelope level, 11 bits
  // key on / key off
  uint8_t  keyOn;             // pending, taken on the next even sample
  uint8_t  keyOff;            // KOFF bit (level, not edge)
  // output
  int16_t  out;               // the voice's last sample (before the volumes): PMON's source and OUTX
  int8_t   volL, volR;        // VOL(L), VOL(R)
  uint8_t  echo;              // EON bit
} CtMixerVoice;

typedef struct CtMixer {
  uint8_t* aram;              // the 64 KB of sound RAM (owner's)
  uint8_t  regs[0x80];        // the register file as the SPC reads it back (ENVX/OUTX/ENDX are written by the mixer)
  CtMixerVoice v[8];
  // the global rate counter (the envelope and noise rates' clock), the even/odd sample
  uint16_t counter;
  uint8_t  evenSample;
  // FLG
  uint8_t  mute, softReset, echoWrites, noiseRate;
  uint16_t dirAdr;            // DIR x 256
  int8_t   mainVolL, mainVolR;
  int16_t  mixL, mixR;        // this sample's main mix
  int16_t  echoInL, echoInR;  // this sample's echo input
  int16_t  noise;             // the noise LFSR (15 bits)
  // echo
  int8_t   echoVolL, echoVolR, feedback;
  uint16_t echoAdr;           // ESA x 256
  uint16_t echoDelay;         // EDL x 512 (stereo samples)
  uint16_t echoLength;        // the delay latched at the buffer's start
  uint16_t echoPos;           // byte offset into the buffer
  uint8_t  firPos;
  int8_t   fir[8];            // C0..C7
  int16_t  firL[8], firR[8];  // the FIR's history
  uint8_t  voiceMask;         // debug (CT_DSP_VOICEMASK's twin): voices outside it reach neither mix; 0xFF normally
} CtMixer;

// Power-on: every register 0 except ENDX = $FF; muted, soft reset, echo writes off until FLG is written.
void    ct_mixer_init(CtMixer* m, uint8_t* aram);
// The S-DSP register sink: what the SPC's write to $F3 (with $F2 = reg) does. reg 0..$7F.
void    ct_mixer_write(CtMixer* m, uint8_t reg, uint8_t val);
// The register file as the SPC reads it: the written values, ENVX ($x8) / OUTX ($x9) / ENDX ($7C) as the mixer left them.
uint8_t ct_mixer_read(const CtMixer* m, uint8_t reg);
// Run `frames` samples (32,040 Hz stereo frames) into out[2*i] = left, out[2*i+1] = right. out may be NULL (run silent).
void    ct_mixer_render(CtMixer* m, int16_t* out, int frames);

// ---- the host's clock: the SPC's exact samples per displayed frame ----
// The S-DSP makes a sample every 32 APU cycles; the APU runs 32,040 x 32 cycles a second against the master clock's
// 1364 x 262 x 60 cycles. An even field is 1364 x 262 master cycles, an odd one 4 cycles shorter (line 240's short
// line, snes.c), so a frame holds 534 samples or, now and then, 533. The clock counts in integers, so it does not drift.
typedef struct CtMixerClock { uint64_t master; } CtMixerClock;
int     ct_mixer_clock_frame(CtMixerClock* c, int oddFrame);   // samples this frame holds (advances the clock)

// ---- voice-level operations: a thin layer over the same registers ----
void ct_mixer_voice_source(CtMixer* m, int v, uint8_t srcn);
void ct_mixer_voice_pitch(CtMixer* m, int v, uint16_t pitch);                 // 14 bits
void ct_mixer_voice_volume(CtMixer* m, int v, int8_t l, int8_t r);
void ct_mixer_voice_adsr(CtMixer* m, int v, uint8_t adsr1, uint8_t adsr2);    // ADSR1 bit 7 set = ADSR mode
void ct_mixer_voice_gain(CtMixer* m, int v, uint8_t gain);                    // GAIN (takes effect with ADSR1 bit 7 clear)
// KON / KOFF write the whole mask like the registers do: a voice not in a KON mask loses a key-on still pending.
void ct_mixer_key_on(CtMixer* m, uint8_t mask);
void ct_mixer_key_off(CtMixer* m, uint8_t mask);
// One voice keyed with its source, pitch and envelope: SRCN, P, ADSR1/2 (or GAIN), then KON with that voice's bit.
void ct_mixer_voice_key_on(CtMixer* m, int v, uint8_t srcn, uint16_t pitch, uint8_t adsr1, uint8_t adsr2, uint8_t gain);
void ct_mixer_main_volume(CtMixer* m, int8_t l, int8_t r);
void ct_mixer_echo(CtMixer* m, uint8_t esaPage, uint8_t edl, int8_t volL, int8_t volR, int8_t feedback,
                   const int8_t fir[8], uint8_t eonMask);
void ct_mixer_noise(CtMixer* m, uint8_t nonMask);                             // the rate is FLG's (ct_mixer_flags)
void ct_mixer_flags(CtMixer* m, uint8_t flg);                                 // reset / mute / echo write off / noise rate
void ct_mixer_pitch_mod(CtMixer* m, uint8_t pmonMask);
void ct_mixer_directory(CtMixer* m, uint8_t dirPage);

#endif
