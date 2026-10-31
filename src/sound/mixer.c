// mixer.c -- the port's S-DSP (mixer.h). Every stage keeps the vendored snes/dsp.c's (LakeSnes, MIT) integer widths and
// order of operations, so the output matches it sample for sample; the model and the source of each piece of the
// console's behaviour: disasm/notes/audio_mixer.md.
#include <string.h>
#include "mixer.h"

// The envelope / noise rate table: rate r fires every kRatePeriod[r] samples of the global counter, offset so the 32 rates
// interleave (the S-DSP's counter; dsp.c rateValues / rateOffsets). Rate 0 never fires.
static const int kRatePeriod[32] = {
  0, 2048, 1536, 1280, 1024, 768, 640, 512,
  384, 320, 256, 192, 160, 128, 96, 80,
  64, 48, 40, 32, 24, 20, 16, 12,
  10, 8, 6, 5, 4, 3, 2, 1
};
static const int kRateOffset[32] = {
  0, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536,
  0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0
};

// The S-DSP's Gaussian interpolation table: the console's hardware (its 512-entry ROM, as LakeSnes's dsp.c gives it), not
// the game's data (tools/release/scan_rom_leaks.py KNOWN_OK). Entry i weighs the sample (0x100 - i) positions away.
static const int ctMixGauss[512] = {
  0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
  0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x002, 0x002, 0x002, 0x002, 0x002,
  0x002, 0x002, 0x003, 0x003, 0x003, 0x003, 0x003, 0x004, 0x004, 0x004, 0x004, 0x004, 0x005, 0x005, 0x005, 0x005,
  0x006, 0x006, 0x006, 0x006, 0x007, 0x007, 0x007, 0x008, 0x008, 0x008, 0x009, 0x009, 0x009, 0x00a, 0x00a, 0x00a,
  0x00b, 0x00b, 0x00b, 0x00c, 0x00c, 0x00d, 0x00d, 0x00e, 0x00e, 0x00f, 0x00f, 0x00f, 0x010, 0x010, 0x011, 0x011,
  0x012, 0x013, 0x013, 0x014, 0x014, 0x015, 0x015, 0x016, 0x017, 0x017, 0x018, 0x018, 0x019, 0x01a, 0x01b, 0x01b,
  0x01c, 0x01d, 0x01d, 0x01e, 0x01f, 0x020, 0x020, 0x021, 0x022, 0x023, 0x024, 0x024, 0x025, 0x026, 0x027, 0x028,
  0x029, 0x02a, 0x02b, 0x02c, 0x02d, 0x02e, 0x02f, 0x030, 0x031, 0x032, 0x033, 0x034, 0x035, 0x036, 0x037, 0x038,
  0x03a, 0x03b, 0x03c, 0x03d, 0x03e, 0x040, 0x041, 0x042, 0x043, 0x045, 0x046, 0x047, 0x049, 0x04a, 0x04c, 0x04d,
  0x04e, 0x050, 0x051, 0x053, 0x054, 0x056, 0x057, 0x059, 0x05a, 0x05c, 0x05e, 0x05f, 0x061, 0x063, 0x064, 0x066,
  0x068, 0x06a, 0x06b, 0x06d, 0x06f, 0x071, 0x073, 0x075, 0x076, 0x078, 0x07a, 0x07c, 0x07e, 0x080, 0x082, 0x084,
  0x086, 0x089, 0x08b, 0x08d, 0x08f, 0x091, 0x093, 0x096, 0x098, 0x09a, 0x09c, 0x09f, 0x0a1, 0x0a3, 0x0a6, 0x0a8,
  0x0ab, 0x0ad, 0x0af, 0x0b2, 0x0b4, 0x0b7, 0x0ba, 0x0bc, 0x0bf, 0x0c1, 0x0c4, 0x0c7, 0x0c9, 0x0cc, 0x0cf, 0x0d2,
  0x0d4, 0x0d7, 0x0da, 0x0dd, 0x0e0, 0x0e3, 0x0e6, 0x0e9, 0x0ec, 0x0ef, 0x0f2, 0x0f5, 0x0f8, 0x0fb, 0x0fe, 0x101,
  0x104, 0x107, 0x10b, 0x10e, 0x111, 0x114, 0x118, 0x11b, 0x11e, 0x122, 0x125, 0x129, 0x12c, 0x130, 0x133, 0x137,
  0x13a, 0x13e, 0x141, 0x145, 0x148, 0x14c, 0x150, 0x153, 0x157, 0x15b, 0x15f, 0x162, 0x166, 0x16a, 0x16e, 0x172,
  0x176, 0x17a, 0x17d, 0x181, 0x185, 0x189, 0x18d, 0x191, 0x195, 0x19a, 0x19e, 0x1a2, 0x1a6, 0x1aa, 0x1ae, 0x1b2,
  0x1b7, 0x1bb, 0x1bf, 0x1c3, 0x1c8, 0x1cc, 0x1d0, 0x1d5, 0x1d9, 0x1dd, 0x1e2, 0x1e6, 0x1eb, 0x1ef, 0x1f3, 0x1f8,
  0x1fc, 0x201, 0x205, 0x20a, 0x20f, 0x213, 0x218, 0x21c, 0x221, 0x226, 0x22a, 0x22f, 0x233, 0x238, 0x23d, 0x241,
  0x246, 0x24b, 0x250, 0x254, 0x259, 0x25e, 0x263, 0x267, 0x26c, 0x271, 0x276, 0x27b, 0x280, 0x284, 0x289, 0x28e,
  0x293, 0x298, 0x29d, 0x2a2, 0x2a6, 0x2ab, 0x2b0, 0x2b5, 0x2ba, 0x2bf, 0x2c4, 0x2c9, 0x2ce, 0x2d3, 0x2d8, 0x2dc,
  0x2e1, 0x2e6, 0x2eb, 0x2f0, 0x2f5, 0x2fa, 0x2ff, 0x304, 0x309, 0x30e, 0x313, 0x318, 0x31d, 0x322, 0x326, 0x32b,
  0x330, 0x335, 0x33a, 0x33f, 0x344, 0x349, 0x34e, 0x353, 0x357, 0x35c, 0x361, 0x366, 0x36b, 0x370, 0x374, 0x379,
  0x37e, 0x383, 0x388, 0x38c, 0x391, 0x396, 0x39b, 0x39f, 0x3a4, 0x3a9, 0x3ad, 0x3b2, 0x3b7, 0x3bb, 0x3c0, 0x3c5,
  0x3c9, 0x3ce, 0x3d2, 0x3d7, 0x3dc, 0x3e0, 0x3e5, 0x3e9, 0x3ed, 0x3f2, 0x3f6, 0x3fb, 0x3ff, 0x403, 0x408, 0x40c,
  0x410, 0x415, 0x419, 0x41d, 0x421, 0x425, 0x42a, 0x42e, 0x432, 0x436, 0x43a, 0x43e, 0x442, 0x446, 0x44a, 0x44e,
  0x452, 0x455, 0x459, 0x45d, 0x461, 0x465, 0x468, 0x46c, 0x470, 0x473, 0x477, 0x47a, 0x47e, 0x481, 0x485, 0x488,
  0x48c, 0x48f, 0x492, 0x496, 0x499, 0x49c, 0x49f, 0x4a2, 0x4a6, 0x4a9, 0x4ac, 0x4af, 0x4b2, 0x4b5, 0x4b7, 0x4ba,
  0x4bd, 0x4c0, 0x4c3, 0x4c5, 0x4c8, 0x4cb, 0x4cd, 0x4d0, 0x4d2, 0x4d5, 0x4d7, 0x4d9, 0x4dc, 0x4de, 0x4e0, 0x4e3,
  0x4e5, 0x4e7, 0x4e9, 0x4eb, 0x4ed, 0x4ef, 0x4f1, 0x4f3, 0x4f5, 0x4f6, 0x4f8, 0x4fa, 0x4fb, 0x4fd, 0x4ff, 0x500,
  0x502, 0x503, 0x504, 0x506, 0x507, 0x508, 0x50a, 0x50b, 0x50c, 0x50d, 0x50e, 0x50f, 0x510, 0x511, 0x511, 0x512,
  0x513, 0x514, 0x514, 0x515, 0x516, 0x516, 0x517, 0x517, 0x517, 0x518, 0x518, 0x518, 0x518, 0x518, 0x519, 0x519
};

static int clamp16(int x) { return x < -0x8000 ? -0x8000 : (x > 0x7fff ? 0x7fff : x); }
static int wrap16(int x) { return (int16_t)(x & 0xffff); }   // the S-DSP's overflowing 16-bit adds

static int rateFires(const CtMixer* m, int rate) {
  if (rate == 0) return 0;
  return ((m->counter + kRateOffset[rate]) % kRatePeriod[rate]) == 0;
}

void ct_mixer_init(CtMixer* m, uint8_t* aram) {
  memset(m, 0, sizeof *m);
  m->aram = aram;
  m->regs[0x7c] = 0xff;            // ENDX
  m->evenSample = 1;
  m->mute = 1;
  m->softReset = 1;
  m->noise = 0x4000;
  m->voiceMask = 0xff;
}

// ---- BRR: four samples of the block's data, through the header's shift and prediction filter ----
static void brrDecode(CtMixer* m, CtMixerVoice* v) {
  const uint8_t* ram = m->aram;
  int shift = v->header >> 4;
  int filter = (v->header & 0xc) >> 2;
  int pos = v->ringPos;
  int old = v->ring[pos == 0 ? 11 : pos - 1] >> 1;
  int older = v->ring[pos == 0 ? 10 : pos - 2] >> 1;
  uint8_t byte = 0;
  for (int i = 0; i < 4; i++) {
    int s;
    if (i & 1) s = byte & 0xf;
    else {
      byte = ram[(v->blockAdr + v->blockByte + (i >> 1)) & 0xffff];
      s = byte >> 4;
    }
    if (s > 7) s -= 16;
    if (shift <= 0xc) s = (s << shift) >> 1;
    else s = (s >> 3) << 12;                       // shifts 13-15: 0 or -2048 (the hardware's)
    switch (filter) {
      case 1: s += old + (-old >> 4); break;
      case 2: s += 2 * old + ((3 * -old) >> 5) - older + (older >> 4); break;
      case 3: s += 2 * old + ((13 * -old) >> 6) - older + ((3 * older) >> 4); break;
    }
    v->ring[pos + i] = clamp16(s) * 2;             // 15 bits kept, doubled (bit 15 lost)
    older = old;
    old = v->ring[pos + i] >> 1;
  }
  v->ringPos += 4;
  if (v->ringPos >= 12) v->ringPos = 0;
}

// ---- the Gaussian interpolation over the four samples around the position ----
static int16_t interpolate(const CtMixerVoice* v) {
  int pos = (v->pitchCounter >> 12) + v->ringPos;
  int frac = (v->pitchCounter >> 4) & 0xff;
  int16_t s3 = v->ring[(pos + 3) % 12];
  int16_t s2 = v->ring[(pos + 2) % 12];
  int16_t s1 = v->ring[(pos + 1) % 12];
  int16_t s0 = v->ring[pos % 12];
  int out = (ctMixGauss[0xff - frac] * s0) >> 11;
  out += (ctMixGauss[0x1ff - frac] * s1) >> 11;
  out += (ctMixGauss[0x100 + frac] * s2) >> 11;
  out = wrap16(out) + ((ctMixGauss[frac] * s3) >> 11);
  return clamp16(out) & ~1;
}

// ---- the envelope: one step of ADSR or GAIN, taken when the mode's rate fires ----
static void envelope(CtMixer* m, CtMixerVoice* v) {
  int level = v->env;
  int rate = 0;
  if (v->envMode == 3) {                           // release: -8 every sample
    rate = 31;
    level -= 8;
  } else if (!v->useGain) {
    rate = v->rate[v->envMode];
    switch (v->envMode) {
      case 0: level += rate == 31 ? 1024 : 32; break;          // attack
      case 1: level -= ((level - 1) >> 8) + 1; break;          // decay (exponential)
      case 2: level -= ((level - 1) >> 8) + 1; break;          // sustain (exponential)
    }
  } else if (!v->directGain) {
    rate = v->rate[3];
    switch (v->gainMode) {
      case 0: level -= 32; break;                                // linear decrease
      case 1: level -= ((level - 1) >> 8) + 1; break;            // exponential decrease
      case 2: level += 32; break;                                // linear increase
      case 3: level += (v->preclamp < 0x600) ? 32 : 8; break;    // bent increase
    }
  } else {                                         // direct gain
    rate = 31;
    level = v->directValue;
  }
  int sl = v->useGain ? v->gainSustainLevel : v->sustainLevel;
  if (v->envMode == 1 && (level >> 8) == sl) v->envMode = 2;
  v->preclamp = level & 0xffff;
  if (level < 0 || level > 0x7ff) {
    level = level < 0 ? 0 : 0x7ff;
    if (v->envMode == 0) v->envMode = 1;
  }
  if (rateFires(m, rate)) v->env = level;
}

// ---- one voice's sample ----
static void voiceSample(CtMixer* m, int ch) {
  CtMixerVoice* v = &m->v[ch];
  const uint8_t* ram = m->aram;
  int pitch = v->pitch;
  if (ch > 0 && v->pitchMod) pitch += ((m->v[ch - 1].out >> 5) * pitch) >> 10;
  v->header = ram[v->blockAdr];
  // the directory entry: the start address while starting, the loop address after
  uint16_t entry = m->dirAdr + 4 * v->srcn;
  if (v->startDelay == 0) entry += 2;
  uint16_t sampleAdr = ram[entry] | (ram[(entry + 1) & 0xffff] << 8);
  if (v->startDelay > 0) {
    if (v->startDelay == 5) {                      // the key-on's first sample
      v->blockAdr = sampleAdr;
      v->blockByte = 1;
      v->ringPos = 0;
      v->header = 0;
      m->regs[0x7c] &= ~(1 << ch);                 // ENDX cleared
    }
    v->env = 0;
    v->startDelay--;
    v->pitchCounter = 0;
    if (v->startDelay > 0 && v->startDelay < 4) v->pitchCounter = 0x4000;
    pitch = 0;
  }
  int s = v->noise ? wrap16(m->noise * 2) : interpolate(v);
  s = ((s * v->env) >> 11) & ~1;
  if (m->softReset || (v->header & 0x03) == 1) {   // FLG reset, or a block that ends without looping
    v->envMode = 3;
    v->env = 0;
  }
  if (m->evenSample) {
    if (v->keyOff) v->envMode = 3;
    if (v->keyOn) {
      v->startDelay = 5;
      v->envMode = 0;
      v->keyOn = 0;
    }
  }
  if (v->startDelay == 0) envelope(m, v);
  if (v->pitchCounter >= 0x4000) {                 // the position left the ring's first group: decode the next four
    brrDecode(m, v);
    if (v->blockByte >= 7) {
      if (v->header & 0x1) {                       // end flag: back to the loop point
        v->blockAdr = sampleAdr;
        m->regs[0x7c] |= 1 << ch;                  // ENDX set
      } else {
        v->blockAdr += 9;
      }
      v->blockByte = 1;
    } else {
      v->blockByte += 2;
    }
  }
  v->pitchCounter &= 0x3fff;
  v->pitchCounter += pitch;
  if (v->pitchCounter > 0x7fff) v->pitchCounter = 0x7fff;
  m->regs[(ch << 4) | 8] = v->env >> 4;            // ENVX
  m->regs[(ch << 4) | 9] = s >> 8;                 // OUTX
  v->out = s;
  if (!(m->voiceMask & (1 << ch))) return;
  m->mixL = clamp16(m->mixL + ((s * v->volL) >> 7));
  m->mixR = clamp16(m->mixR + ((s * v->volR) >> 7));
  if (v->echo) {
    m->echoInL = clamp16(m->echoInL + ((s * v->volL) >> 7));
    m->echoInR = clamp16(m->echoInR + ((s * v->volR) >> 7));
  }
}

// ---- the echo: the buffer's oldest sample through the FIR, mixed in; the input plus feedback written back ----
static void echoSample(CtMixer* m) {
  uint8_t* ram = m->aram;
  m->firPos = (m->firPos + 1) & 0x7;
  uint16_t adr = m->echoAdr + m->echoPos;
  int16_t rs = ram[adr] | (ram[(adr + 1) & 0xffff] << 8);
  m->firL[m->firPos] = rs >> 1;
  rs = ram[(adr + 2) & 0xffff] | (ram[(adr + 3) & 0xffff] << 8);
  m->firR[m->firPos] = rs >> 1;
  int sumL = 0, sumR = 0;
  for (int i = 0; i < 8; i++) {
    sumL += (m->firL[(m->firPos + i + 1) & 0x7] * m->fir[i]) >> 6;
    sumR += (m->firR[(m->firPos + i + 1) & 0x7] * m->fir[i]) >> 6;
    if (i == 6) { sumL = wrap16(sumL); sumR = wrap16(sumR); }   // the first seven taps wrap, the eighth clamps
  }
  sumL = clamp16(sumL) & ~1;
  sumR = clamp16(sumR) & ~1;
  m->mixL = clamp16(((m->mixL * m->mainVolL) >> 7) + ((sumL * m->echoVolL) >> 7));
  m->mixR = clamp16(((m->mixR * m->mainVolR) >> 7) + ((sumR * m->echoVolR) >> 7));
  int eL = clamp16(m->echoInL + wrap16((sumL * m->feedback) >> 7)) & ~1;
  int eR = clamp16(m->echoInR + wrap16((sumR * m->feedback) >> 7)) & ~1;
  if (m->echoWrites) {
    ram[adr] = eL & 0xff;
    ram[(adr + 1) & 0xffff] = eL >> 8;
    ram[(adr + 2) & 0xffff] = eR & 0xff;
    ram[(adr + 3) & 0xffff] = eR >> 8;
  }
  if (m->echoPos == 0) m->echoLength = m->echoDelay * 4;   // EDL is latched at the buffer's start
  m->echoPos += 4;
  if (m->echoPos >= m->echoLength) m->echoPos = 0;
}

static void noiseStep(CtMixer* m) {
  if (rateFires(m, m->noiseRate)) {
    int bit = (m->noise & 1) ^ ((m->noise >> 1) & 1);
    m->noise = ((m->noise >> 1) & 0x3fff) | (bit << 14);
  }
}

void ct_mixer_render(CtMixer* m, int16_t* out, int frames) {
  for (int n = 0; n < frames; n++) {
    m->mixL = m->mixR = 0;
    m->echoInL = m->echoInR = 0;
    for (int ch = 0; ch < 8; ch++) voiceSample(m, ch);
    echoSample(m);                                 // also applies the main volume
    m->counter = m->counter == 0 ? 30720 : m->counter - 1;
    noiseStep(m);
    m->evenSample = !m->evenSample;
    if (m->mute) m->mixL = m->mixR = 0;
    if (out) { out[n * 2] = m->mixL; out[n * 2 + 1] = m->mixR; }
  }
}

uint8_t ct_mixer_read(const CtMixer* m, uint8_t reg) { return m->regs[reg & 0x7f]; }

void ct_mixer_write(CtMixer* m, uint8_t reg, uint8_t val) {
  reg &= 0x7f;
  int ch = reg >> 4;
  CtMixerVoice* v = &m->v[ch];
  switch (reg & 0x0f) {                            // the voice registers $x0-$x7, the FIR taps $xF
    case 0x0: v->volL = (int8_t)val; break;
    case 0x1: v->volR = (int8_t)val; break;
    case 0x2: v->pitch = (v->pitch & 0x3f00) | val; break;
    case 0x3: v->pitch = ((v->pitch & 0x00ff) | (val << 8)) & 0x3fff; break;
    case 0x4: v->srcn = val; break;
    case 0x5:
      v->rate[0] = (val & 0xf) * 2 + 1;
      v->rate[1] = ((val & 0x70) >> 4) * 2 + 16;
      v->useGain = (val & 0x80) == 0;
      break;
    case 0x6:
      v->rate[2] = val & 0x1f;
      v->sustainLevel = (val & 0xe0) >> 5;
      break;
    case 0x7:
      v->directGain = (val & 0x80) == 0;
      v->gainMode = (val & 0x60) >> 5;
      v->rate[3] = val & 0x1f;
      v->directValue = (val & 0x7f) * 16;
      v->gainSustainLevel = (val & 0xe0) >> 5;
      break;
    case 0xf: m->fir[ch] = (int8_t)val; break;
  }
  switch (reg) {                                   // the global registers $xC, $xD
    case 0x0c: m->mainVolL = (int8_t)val; break;
    case 0x1c: m->mainVolR = (int8_t)val; break;
    case 0x2c: m->echoVolL = (int8_t)val; break;
    case 0x3c: m->echoVolR = (int8_t)val; break;
    case 0x4c: for (int i = 0; i < 8; i++) m->v[i].keyOn = (val >> i) & 1; break;
    case 0x5c: for (int i = 0; i < 8; i++) m->v[i].keyOff = (val >> i) & 1; break;
    case 0x6c:
      m->softReset = (val & 0x80) != 0;
      m->mute = (val & 0x40) != 0;
      m->echoWrites = (val & 0x20) == 0;
      m->noiseRate = val & 0x1f;
      break;
    case 0x7c: val = 0; break;                     // any write to ENDX clears it
    case 0x0d: m->feedback = (int8_t)val; break;
    case 0x2d: for (int i = 0; i < 8; i++) m->v[i].pitchMod = (val >> i) & 1; break;
    case 0x3d: for (int i = 0; i < 8; i++) m->v[i].noise = (val >> i) & 1; break;
    case 0x4d: for (int i = 0; i < 8; i++) m->v[i].echo = (val >> i) & 1; break;
    case 0x5d: m->dirAdr = val << 8; break;
    case 0x6d: m->echoAdr = val << 8; break;
    case 0x7d: m->echoDelay = (val & 0xf) * 512; break;   // 2 KB steps, a stereo sample is 4 bytes
  }
  m->regs[reg] = val;
}

// ---- the clock ----
// samples = master x (32,040 / (1364 x 262 x 60)) = master x 267 / 178,684 (both / 120): an even field, 357,368 master
// cycles, is exactly 534.
int ct_mixer_clock_frame(CtMixerClock* c, int oddFrame) {
  uint64_t a = c->master * 267u / 178684u;
  c->master += oddFrame ? 357364u : 357368u;
  return (int)(c->master * 267u / 178684u - a);
}

// ---- voice-level operations ----
void ct_mixer_voice_source(CtMixer* m, int v, uint8_t srcn) { ct_mixer_write(m, (uint8_t)(v << 4 | 4), srcn); }
void ct_mixer_voice_pitch(CtMixer* m, int v, uint16_t p) {
  ct_mixer_write(m, (uint8_t)(v << 4 | 2), p & 0xff);
  ct_mixer_write(m, (uint8_t)(v << 4 | 3), (p >> 8) & 0x3f);
}
void ct_mixer_voice_volume(CtMixer* m, int v, int8_t l, int8_t r) {
  ct_mixer_write(m, (uint8_t)(v << 4 | 0), (uint8_t)l);
  ct_mixer_write(m, (uint8_t)(v << 4 | 1), (uint8_t)r);
}
void ct_mixer_voice_adsr(CtMixer* m, int v, uint8_t a1, uint8_t a2) {
  ct_mixer_write(m, (uint8_t)(v << 4 | 5), a1);
  ct_mixer_write(m, (uint8_t)(v << 4 | 6), a2);
}
void ct_mixer_voice_gain(CtMixer* m, int v, uint8_t g) { ct_mixer_write(m, (uint8_t)(v << 4 | 7), g); }
void ct_mixer_key_on(CtMixer* m, uint8_t mask) { ct_mixer_write(m, 0x4c, mask); }
void ct_mixer_key_off(CtMixer* m, uint8_t mask) { ct_mixer_write(m, 0x5c, mask); }
void ct_mixer_voice_key_on(CtMixer* m, int v, uint8_t srcn, uint16_t pitch, uint8_t a1, uint8_t a2, uint8_t gain) {
  ct_mixer_voice_source(m, v, srcn);
  ct_mixer_voice_pitch(m, v, pitch);
  ct_mixer_voice_adsr(m, v, a1, a2);
  ct_mixer_voice_gain(m, v, gain);
  ct_mixer_key_on(m, (uint8_t)(1 << v));
}
void ct_mixer_main_volume(CtMixer* m, int8_t l, int8_t r) {
  ct_mixer_write(m, 0x0c, (uint8_t)l);
  ct_mixer_write(m, 0x1c, (uint8_t)r);
}
void ct_mixer_echo(CtMixer* m, uint8_t esa, uint8_t edl, int8_t vl, int8_t vr, int8_t fb, const int8_t fir[8], uint8_t eon) {
  ct_mixer_write(m, 0x6d, esa);
  ct_mixer_write(m, 0x7d, edl);
  ct_mixer_write(m, 0x2c, (uint8_t)vl);
  ct_mixer_write(m, 0x3c, (uint8_t)vr);
  ct_mixer_write(m, 0x0d, (uint8_t)fb);
  if (fir) for (int i = 0; i < 8; i++) ct_mixer_write(m, (uint8_t)(i << 4 | 0xf), (uint8_t)fir[i]);
  ct_mixer_write(m, 0x4d, eon);
}
void ct_mixer_noise(CtMixer* m, uint8_t non) { ct_mixer_write(m, 0x3d, non); }
void ct_mixer_flags(CtMixer* m, uint8_t flg) { ct_mixer_write(m, 0x6c, flg); }
void ct_mixer_pitch_mod(CtMixer* m, uint8_t pmon) { ct_mixer_write(m, 0x2d, pmon); }
void ct_mixer_directory(CtMixer* m, uint8_t page) { ct_mixer_write(m, 0x5d, page); }
