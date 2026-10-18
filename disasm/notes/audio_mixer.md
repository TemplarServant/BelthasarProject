# The native mixer -- the port's S-DSP (sound step 3)

The SNES's sound hardware is two chips: the SPC700, a CPU that runs the game's sound driver (Square's Akao driver,
uploaded from bank $C7: disasm/notes/audio_spc_driver.md), and the S-DSP, which mixes eight voices of BRR samples into
32,040 stereo samples a second (LakeSnes's rate; the hardware's nominal one is 32,000). The S-DSP has no program: it is
128 registers and fixed logic. `src/sound/mixer.c` / `mixer.h` is that logic as a component the port owns, a refactor of
the vendored `snes/dsp.c` (LakeSnes, MIT), and it outputs **sample for sample** what `snes/dsp.c` outputs (sec. 9).

This is the console's hardware, not the game's data, so modelling it natively is inside the no-emulation decision (CLAUDE.md,
2026-10-09): what goes is the SPC700 running ROM code (step 2: the driver in C) and the register-polling shape of the
vendored core where it is not needed (dsp.c's 0x400-sample ring and its `dsp_getSamples` resampler, `apu.c`'s
every-32-cycles call). The mixer is driven from outside and renders straight into the caller's buffer.

**Where the behaviour comes from.** Every stage below is the arithmetic of LakeSnes's `snes/dsp.c`, kept to its integer
widths and order of operations (that is what makes the output identical, not just close). LakeSnes models the S-DSP the
way the console's public documentation describes it (the register map, BRR, the Gaussian table, the rate counter, the
envelope modes, the echo FIR -- as in fullsnes and anomie's S-DSP notes); the table of Gaussian weights is the chip's own
ROM. Where LakeSnes simplifies the real chip, the mixer keeps LakeSnes's model, because the target is the sound the port
plays today (sec. 11 lists those places).

## 0. Interface

| call | what |
|---|---|
| `ct_mixer_init(m, aram)` | power-on: registers 0, ENDX = $FF, FLG's soft reset and mute set, echo writes off, the noise LFSR at $4000. `aram` = the owner's 64 KB of sound RAM (directory, BRR, echo buffer). |
| `ct_mixer_write(m, reg, val)` | the register sink: what the SPC's write to $F3 with $F2 = `reg` does (step 2's `dspWrite`). |
| `ct_mixer_read(m, reg)` | the register file as the SPC reads it: the values written, ENVX ($x8), OUTX ($x9) and ENDX ($7C) as the mixer left them. |
| `ct_mixer_render(m, out, n)` | n samples, interleaved int16 L,R (out may be NULL). |
| `ct_mixer_clock_frame(c, odd)` | the samples one displayed frame holds (sec. 10). |
| voice level | `ct_mixer_voice_source / _pitch / _volume / _adsr / _gain`, `ct_mixer_key_on / key_off` (whole masks, as KON/KOFF), `ct_mixer_voice_key_on` (SRCN, P, ADSR, GAIN, then KON with that voice's bit), `ct_mixer_main_volume`, `ct_mixer_echo` (ESA, EDL, EVOL, EFB, the 8 taps, EON), `ct_mixer_noise` (NON), `ct_mixer_flags` (FLG), `ct_mixer_pitch_mod` (PMON), `ct_mixer_directory` (DIR). A thin layer: each is the register writes, so it shares the state and the semantics of the sink. |

No SPC700, no timers, no ports. The state is `CtMixer` (mixer.h), plain data: a savestate can copy it.

## 1. One sample, in order

`ct_mixer_render`, per sample (dsp.c `dsp_cycle`):

1. the main mix and the echo input start at 0;
2. voices 0..7, each in turn (sec. 2-7) -- in order, because a voice's pitch modulation reads the previous voice's
   output of THIS sample;
3. the echo (sec. 8), which also applies the main volume;
4. the global rate counter steps (30720 down to 0, then back to 30720);
5. the noise generator steps if its rate fires (sec. 6);
6. the even/odd sample flips (key-on and key-off are taken on even samples);
7. FLG's mute zeroes the output.

## 2. The voice's sample source

- **The directory.** DIR x 256 + 4 x SRCN is the voice's entry: two words, the start address and the loop address.
  While the voice is starting (sec. 4) the start address is read, after that the loop address.
- **The block.** A BRR block is 9 bytes: a header (bits 7-4 shift, 3-2 filter, 1 loop, 0 end) and 8 bytes of 16
  four-bit samples. The voice reads the header of its current block every sample.
- **Decoding, four samples at a time.** When the interpolation position (`pitchCounter`, 4.12 fixed point) passes
  0x4000, the next four nibbles are decoded into a 12-sample ring (three groups of four):
  - the nibble is signed (-8..7); with shift 0-12 it becomes `(s << shift) >> 1`; shifts 13-15 (invalid on the chip)
    give `(s >> 3) << 12`: 0 for a non-negative nibble, -4096 for a negative one;
  - the prediction filter adds the previous two decoded samples (`old`, `older`, kept as the ring's halves):
    filter 1 `old x 15/16` (`old + (-old >> 4)`), filter 2 `old x 61/32 - older x 15/16`
    (`2 old + (-3 old >> 5) - older + (older >> 4)`), filter 3 `old x 115/64 - older x 13/16`
    (`2 old + (-13 old >> 6) - older + (3 older >> 4)`);
  - the result is clamped to 16 bits and doubled into the ring (`clamp16(s) * 2` into an int16: the top bit is lost,
    15 bits kept -- the hardware's);
  - after the block's 8th byte: with the end flag the voice jumps to the loop address and sets its ENDX bit, else it
    goes to the next block (+9).
- **End without loop.** A block whose header has end = 1 and loop = 0 sends the voice to release with its envelope at 0
  (checked every sample, before the key-on poll). FLG's soft reset does the same to every voice.

## 3. The Gaussian interpolation

The four ring samples around the position are weighed by the chip's 512-entry table (`ctMixGauss`, the S-DSP's ROM;
the same values as dsp.c's `gaussValues`, and on the leak scanner's KNOWN_OK list under both names):
`frac = (pitchCounter >> 4) & 0xFF`; oldest x G[0xFF - frac] + older x G[0x1FF - frac] + old x G[0x100 + frac], each
`>> 11`, the three summed with 16-bit wraparound, then + newest x G[frac] >> 11, clamped to 16 bits and the lowest bit
cleared. A noise voice skips this and plays the LFSR (sec. 6) doubled with wraparound.

Then the position advances: `pitchCounter &= 0x3FFF; += pitch`, capped at 0x7FFF (only pitch modulation takes a step
above 0x3FFF).

## 4. Key on, key off and the envelope

- **KON / KOFF.** A KON write sets each voice's pending key-on to its bit (a 0 bit cancels a key-on not yet taken); KOFF
  is a level: while its bit is set the voice is sent to release. Both are taken on EVEN samples only: key-off first,
  then key-on (start delay 5, attack).
- **The start.** For the 5 samples of the start delay the envelope is 0 and the pitch does not advance; on the first the
  voice takes its start address, block byte 1, ring position 0, a zero header, and clears its ENDX bit; on the delay's
  samples 3..1 the position is held at 0x4000 so three groups decode before the voice sounds.
- **The rate counter.** A rate r (0..31) fires on the samples where `(counter + offset[r]) % period[r] == 0`
  (periods 2048, 1536, 1280, 1024, 768, 640, 512 ... 2, 1; offsets 0 / 1040 / 536 in a three-step cycle; rate 0 never).
  Each envelope mode computes its next level every sample but stores it only when its rate fires.
- **ADSR** (ADSR1 bit 7 set): attack rate = AR x 2 + 1, +32 a step (+1024 at rate 31); at the top (above 0x7FF) the level
  clamps and the mode goes to decay. Decay rate = DR x 2 + 16, sustain rate = SR: both exponential,
  `level -= ((level - 1) >> 8) + 1`; decay turns to sustain when `level >> 8` equals the sustain level SL (ADSR2 bits 7-5).
- **GAIN** (ADSR1 bit 7 clear): bit 7 clear = direct (the level is `(GAIN & 0x7F) x 16`, every sample); otherwise rate =
  bits 4-0 and mode bits 6-5: linear decrease -32, exponential decrease as above, linear increase +32, bent increase +32
  below 0x600 (the level before the clamp, `preclamp`) and +8 above. In GAIN mode the decay-to-sustain test uses GAIN's
  bits 7-5 (dsp.c's `gainSustainLevel`).
- **Release**: -8 every sample (rate 31) until 0.
- The level is clamped to 0..0x7FF; ENVX = level >> 4.

The game's driver never selects GAIN: it parks every voice's $x7 at $A0 at boot (audio_spc_driver.md) and keys ADSR
voices -- no run of sec. 9 has a GAIN sample. The fuzz does.

## 5. The voice's output

`s = (sample x envelope) >> 11` with the lowest bit cleared: this is OUTX's source (OUTX = s >> 8) and the previous-
voice input of the next voice's pitch modulation. Then, unless the voice is outside `voiceMask` (the twin of dsp.c's
debug CT_DSP_VOICEMASK, 0xFF normally): main mix += `(s x VOL(L)) >> 7` and `(s x VOL(R)) >> 7`, each add saturated to
16 bits; an EON voice adds the same to the echo input.

## 6. Noise

A 15-bit LFSR (power-on $4000): when FLG's noise rate (bits 4-0, through the rate counter) fires,
`bit = b0 ^ b1; noise = (noise >> 1) & 0x3FFF | bit << 14`. It steps after the voices, so every NON voice of a sample
plays the same value.

## 7. Pitch modulation

For voice 1..7 with its PMON bit: `pitch += ((previous voice's s >> 5) x pitch) >> 10` -- the previous voice's output
of this sample bends this voice's step by up to about +-100%.

## 8. The echo

- **The buffer.** ESA x 256, EDL x 2 KB (EDL x 512 stereo samples, 4 bytes each); EDL is latched only when the position
  is back at the buffer's start (a new EDL takes effect at the next wrap); EDL 0 is one stereo sample.
- **The FIR.** Every sample the buffer's current sample (the oldest) is read, `>> 1`, into an 8-entry history per side;
  the output is the sum of the 8 taps C0..C7 x history (each `>> 6`), the first seven summed with 16-bit wraparound and
  the eighth added then clamped, the lowest bit cleared.
- **Mixing.** Output = clamp16(`(main mix x MVOL) >> 7 + (FIR x EVOL) >> 7`) per side.
- **Feedback.** The value written back = clamp16(echo input + wrap16(`(FIR x EFB) >> 7`)), lowest bit cleared, into the
  same slot it was read from (unless FLG bit 5 turns echo writes off); then the position moves 4 bytes and wraps at the
  latched length. The echo buffer is sound RAM: the mixer writes the owner's memory, as the chip does.

## 9. The proof: sample-exact against snes/dsp.c

**The capture** (`CT_MIXER_CAPTURE=<file>` on Belthasar, src/audio.c; hooks NULL by default in snes/apu.c and
snes/dsp.c). Binary, little-endian: `CTMX`, u32 version 1, then records:

| record | content |
|---|---|
| `S` | u32 APU cycle, u32 n, n bytes of `dsp_handleState`, 64 KB sound RAM: a segment starts (CtAudioInit's power-on, CtAudioSeedFromApu, CtAudioLoadState) |
| `W` | varint cycles since the previous record, reg, value: an S-DSP register write |
| `A` | varint, u16 address, value: an SPC write to its RAM that changes the byte (uploads, the driver's own clears; the DSP's echo writes are not the SPC's -- each replay makes them) |
| `R` | varint, reg, value: an S-DSP register read and the value the SPC got |
| `H` | u32 n, u32 FNV-1a: the next n samples the live DSP made (4096 a record) |

**The replay** (`ct.exe --mixreplay <file>`, src/sound/mixer_replay.c). Each segment: the vendored `Dsp` is loaded from
the state, the mixer is filled field for field from it (`mixerFromDsp`), each gets its own copy of the RAM. The S-DSP
makes a sample at every APU cycle divisible by 32, and `apu.c`'s `apu_cycle` runs before the access that carries a write
or a read, so an event at cycle W lands after the samples of the cycles before W: the replay renders
`ceil(W/32) - ceil(cur/32)` samples on both, then applies the event. Graded: (1) the reference's stream against the live
run's `H` hashes -- the replay IS the run's sound; (2) every mixer sample against the reference's; (3) every register
read, the mixer's against the value the SPC got; (4) the sound RAM at each segment's end (the echo writes). A census
counts what the run exercised.

**`ct.exe --mixfuzz <cases> [seed]`**: both mixers from power-on over random sound RAM (any byte stream is BRR: every
shift, filter and flag), random registers (ADSR and GAIN, loud volumes, FIR taps, EDL, noise rates) and random writes,
key-ons and RAM changes between samples, a random register read every sample, the RAM after each case.

**`tools/mixer_replay.py`** runs it all. Measured 2026-10-18 (every sample, read and RAM identical):

| run | what | samples | reads | census (voice-samples) |
|---|---|---|---|---|
| attract | `--scene 433`, CT_AUDIO_LOCKSTEP=1, to f11400 | 6,100,023 | 675,912 | echo + feedback, noise 189k, pmod 255k, all 4 BRR filters, shift 13+ 3.3k, 1464 loops |
| gato-battle | battle-end-pc-stands... (Gato, lockstep, the state's SPC) | 1,607,690 | 178,494 | echo, noise 74k, pmod 219k |
| nuns-battle | battle-flame-toss-band... | 491,634 | 48,724 | noise 139k, pmod 92k, filter 3 8.6k |
| magus-battle | battle-xstrike-death-fade... | 454,254 | 43,285 | echo, noise 76k, pmod 48k |
| save-screen | field-savepoint-a-opens-the-save-screen... | 171,236 | 17,080 | echo |
| shop | shop-trade-sound-keys-twice... | 171,235 | 17,416 | echo |
| menu-jingle | field-menu-save-jingle-and-write | 138,661 | 11,066 | echo |
| world-song | field-ow-entry-song-reaches-service | 131,844 | 10,443 | echo |
| fuzz | `--mixfuzz 200 1` | 1,875,277 | 1,875,277 | every GAIN mode (direct 382k, bent 67k ...), 3051 saturated samples |

No game run has a GAIN voice or a saturated output sample (census `clamped=0`; Magus's battle does saturate the running
sum of the voices, sec. 5); GAIN is graded by the fuzz alone.

**Checks.** `mixer-sample-exact-vs-dsp` (quick: the seven short runs + the fuzz, ~15 s) and
`mixer-sample-exact-attract-vs-dsp` (full: the attract, ~30 s). Mutations of src/sound/mixer.c, each killed (regress.py
--mutate): the Gaussian index, BRR filter 2's coefficient, the sustain decay, the noise tap, even-sample KON, the PMON
depth, the FIR's LSB, the feedback scale (12 killed of 12, 2026-10-18). What sees what: ENDX not set is seen only by the
fuzz's register reads (the game's driver reads no $7C in these runs); GAIN's bent knee only by the fuzz; the voice-add
saturation by Magus's battle (its running sum saturates although its final output never does); EDL's latch by the sound
RAM at the gato battle's segment end; all the others by the first run's samples.

## 10. The clock (for step 5)

The mixer has no APU: the host says how many samples to make. `ct_mixer_clock_frame(&clock, oddFrame)` gives the samples
of one displayed frame in exact integers: samples = master cycles x 32,040 / (1364 x 262 x 60) = master x 267 / 178,684;
an even field (357,368 master cycles) is exactly 534 samples, an odd one 4 master cycles shorter (line 240's short line,
snes.c; memory spc-phase-sensitivity), so now and then a frame holds 533. Today src/audio.c gets the same rate from the
vendored SPC's cycle count (17,088 / 17,087.81 APU cycles a frame, run in whole opcodes with a carry), so the per-frame
counts can differ by one sample at a frame boundary while the long-run count is the same; switching Belthasar to the
mixer (step 5) decides which clock feeds it, and the timing of each register write within the frame comes from the
driver in C (step 2).

## 11. Where LakeSnes (and so the mixer) simplifies the chip

Not differences from the port's sound -- the mixer is identical to it -- but places a future "more hardware-exact" pass
would look at, listed so nobody mistakes them for derived facts:

- the real S-DSP runs a 32-step pipeline per sample (each voice's work spread over fixed cycles); LakeSnes computes a
  whole voice at once, so register writes land on sample boundaries rather than mid-sample;
- KON/KOFF are polled every other sample with KOFF winning the same sample; the chip's own order inside the pipeline
  (and its KON latching) is finer;
- the echo buffer is read and written in the same step; the nominal 32,000 Hz is 32,040 here (LakeSnes's APU clock);
- `dsp_getSamples`'s resampling and the 0x400 ring are host plumbing, not the chip, and are not in the mixer.

## 12. Not done here

- Belthasar still plays through snes/dsp.c (step 5 switches it; the quick tier is byte-identical with the hooks built in).
- dsp.c's debug outputs CT_DSP_KONTRACE / CT_DSP_RAWPCM stay in dsp.c; the mixer keeps only `voiceMask`.
- Savestates of the mixer (`CtMixer` is plain data) and the per-frame host loop belong to step 5.
