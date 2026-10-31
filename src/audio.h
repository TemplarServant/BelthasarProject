// audio.h -- the sound system's CPU side. The 65816 uploads the Akao sound driver into the SPC700's 64 KB ARAM at boot
// (bank $C7, over the IPL handshake) and then posts 4-byte commands to the APU ports $2140-$2143. The SPC side is the
// sound machine in C (sound/machine.h), or the vendored SPC700/DSP (snes/apu.c) as the reference.
// Reference: disasm/notes/audio_boot.md. The block tables ($C7:0010 sources, $C7:001C destinations) are read from the
// ROM or the asset container. `ct.exe --aram` (docs/DEBUG_SURFACE.md) dumps the oracle's ARAM, ports and voices;
// CtAudioAram() is the port's side of that comparison.
#ifndef CT_AUDIO_H
#define CT_AUDIO_H

#include <stdint.h>
#include "assets.h"

typedef struct CtAudio CtAudio;

// Boot the sound system: create the SPC700+DSP, let its IPL boot ROM come up, then
// run bank $C7's upload (audio_boot.md sec. 4). `rom` is the 4 MB ROM image (Belthasar
// caches one via CtBattleRom); returns NULL when it is absent or short, so a ROM-less
// run stays silent.
CtAudio* CtAudioInit(const uint8_t* rom, long romLen);
// The same, with the sound's data from the asset container (CT_ASSET_SND_*) when it holds them: the driver upload,
// the songs, samples, instruments and effect sets are then never read from the ROM (which may be NULL).
CtAudio* CtAudioInitWith(const uint8_t* rom, long romLen, const CtAssets* snd);
int CtAudioFromAssets(const CtAudio* a);   // 1 when the data comes from the container
// Every datum the loader takes, from the ROM and from the container, side by side: the driver's six blocks (destination,
// bytes), each song's sequence (its bytes, the 3 read past it, the address that prices the stream), instrument list,
// percussion and record, each sample (bytes, address), each instrument's three words (ids 0..N), both effect sets, the
// exception list's verdict for every song id, the heap base and FD/C1EE's channels. Prints the first difference to stderr;
// returns the number of items compared, or -1 at a difference (or when the container holds no sound).
long CtAudioDataCompare(const uint8_t* rom, long romLen, const CtAssets* snd);
void     CtAudioFree(CtAudio* a);

// Non-zero once the six blocks are in ARAM and the driver has been started.
int      CtAudioReady(const CtAudio* a);

// Advance the SPC700 by one NTSC frame (17088 APU cycles; an odd frame is 4 master
// cycles short, see audio.c apuFrameCycles); the fraction is carried across frames.
void     CtAudioFrame(CtAudio* a);

// Pull `samples` stereo frames (interleaved int16 L,R), resampled the way the
// vendored dsp_getSamples does for the oracle's SDL output.
void     CtAudioSamples(CtAudio* a, int16_t* out, int samples);

// ---- The command channel ----
// The game never writes $2140-43 directly: it stages a 4-byte command at
// $00:1E00-$1E03 and JSLs $C7:0004. CtAudioPost is the raw flush at $C7:061C
// ($1E03->$2143, $1E02->$2142, $1E01->$2141, $1E00->$2140, in that order), the path
// of commands >= $80 and $18-$2F. Returns 0 and does nothing when the driver is not ready.
int      CtAudioPost(CtAudio* a, uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3);

// The $C7:0140 entry: reads the mailbox from the engine's WRAM page (`mem7E` + 0x1E00),
// applies the driver-present and zero-command gates, dispatches, and clears $1E00 on
// the way out like $C7:0192. Returns 1 if a command was consumed, 0 if there was
// nothing to do, -1 for a command whose handler is not ported (the mailbox is still
// cleared, as the ROM does). Not ported: $30-$3F ($C7:071D) and $71 ($C7:0755).
int      CtAudioService(CtAudio* a, uint8_t* mem7E);
extern int g_ctAudioTraceFrame;   // the host's frame number, stamped on CT_SNDPOSTS's lines
// The loader clock (audio.c, song_load_freeze.md sec. 6): every CtAudioService re-runs the JSL's master
// cycles (the listing's instruction costs, the SPC caught up before each port access, the frame's
// background at its raster position) and reports the NMIs crossed (frames lost) and the RTL's line.
struct Apu;
void      CtAudioSeedFromApu(CtAudio* a, const struct Apu* src);   // adopt a savestate's SPC/DSP/ARAM (the oracle's driver state)
void      CtAudioFrameSkip(CtAudio* a);   // a displayed frame whose SPC time a translated stage's bus ran: the field still flips
// The debug snapshot's sound processor (field.c F5/F9): the SPC, the DSP, ARAM and the timers to a file and back. The
// restored WRAM holds the CPU's side of the APU protocol, which only works against the SPC of the same moment.
int       CtAudioSaveState(CtAudio* a, const char* path);
int       CtAudioLoadState(CtAudio* a, const char* path);
void      CtAudioSetEngineMode(CtAudio* a, int mode);   // 0 field, 1 battle: which measured background block
void      CtAudioSetPostLine(CtAudio* a, int vPos);     // the raster line the next JSL begins on (default 12)
// The rule (CLAUDE.md "The infinitely fast CPU"; on unless CT_ORACLE_NMION_HOLD=0 or CT_ORACLE_IFAST=0, read once by
// CtAudioRuleOn): with the NMI on, a JSL costs only the SPC's own time from where the CPU's last wait left the beam, and
// the SPC slice runs vblank to vblank. Each CtAudioService in engine modes 0/1 starts at max(postAt, the SPC's progress
// into this frame's slice), in master cycles past vblank; an NMI met inside it holds the poll for `nmiWait` (the
// handler's own waits). Mode 2 (NMI off) keeps the listing's clock. Both default to the pad latch's wait
// (CtAudioRulePadWait). Off the rule nothing here is read.
int       CtAudioRuleOn(void);
void      CtAudioSetRulePost(CtAudio* a, long postAt, long nmiWait);
void      CtAudioSetRuleNmiOff(CtAudio* a, int off);    // the caller's JSLs run with the NMI off: the listing's clock
void      CtAudioSetRuleOff(CtAudio* a, int off);       // this SPC does not stand for the cartridge's: the listing's clock
// The V-IRQ armed while the CPU sits in a JSL (the rule): its handler waits for that line's H-blank (the field's C0/ECCC
// at line 211, VTIME = $D3 at C0/0B44, unless $010F bit 7, C0/ECD5). -1 = none (the overworld).
void      CtAudioSetRuleIrq(CtAudio* a, int line);
// The HDMA the loader clock walks under the rule, priced from the channels (audio.c LdHdma): prog 1 = the field's
// (FD/C1EE's program, `$0128`, the set `$0153` names, the tables in `wram128`'s bank $7F); `on` = enabled now;
// `nmiOn` = an NMI met inside the JSL enables them (the battle's exit tail, D1/F4E6); `cutLine` = the V-IRQ line that
// disables them (D1/FEB5: 211), -1 none. prog 0 = the per-engine mean (the default). Kept across JSLs until reset.
void      CtAudioSetRuleHdma(CtAudio* a, const uint8_t* wram128, int prog, int on, int nmiOn, int cutLine);
long      CtAudioRulePadWait(void);
long      CtAudioRuleLineAt(int line);                  // vblank -> line `line` of the next picture, master cycles
// $7E:5AAA's `LDA $2143 / AND #$0F / BEQ`: the title setup's wait for the driver to answer, on the
// same clock as the JSL. Returns 1 when the driver answered and reports the frames the spin cost;
// 0 = it gave up at SPIN_LIMIT.
int       CtAudioWaitReplyNibble(CtAudio* a, int* frames);
long long CtAudioLastPostRaster(const CtAudio* a, int* frames, int* endLine, long* polls);   // master cycles

// Seed the CPU-side $1E00 page as the upload tail $C7:00FF does (audio_boot.md
// sec. 7 step 6), including the $1E05 "driver present" latch that gates every later
// $C7:0004 call. Call once after CtAudioInit with the engine's WRAM page.
void     CtAudioSeedMailboxPage(const CtAudio* a, uint8_t* mem7E);

// ---- The song directory (bank $C7's eight tables; disasm/notes/audio_song_loader_verify.md) ----
// The song count is rom[$C7:0AE9], the instrument count the sample-pointer table's own extent, the
// BRR heap base rom16($C7:40B4) + $4700. Block lengths come from each block's own [u16 len] header.
int CtAudioSongCount(const CtAudio* a);
int CtAudioInstrumentCount(const CtAudio* a);
unsigned CtAudioHeapBase(const CtAudio* a);
const uint8_t* CtAudioSeqBlock(const CtAudio* a, int songId, int* lenOut);    // -> ARAM $2000
const uint8_t* CtAudioSampleBlock(const CtAudio* a, int instr, int* lenOut);  // 1-based
const uint8_t* CtAudioInstrSet(const CtAudio* a, int songId);                 // 32 bytes
int CtAudioSongRecord(const CtAudio* a, int songId, int* edl, int* scalar);   // $C7:241D + id*2
int CtAudioEchoBase(const CtAudio* a, int songId);   // ESA page = $F1 - EDL*8; buffer = [ESA*$100,$F100)

// Load a song: $C7:01A1-$0619 (audio_song_loader_verify.md). Blocking, as in the ROM: the routine has
// no frame wait, so it runs to completion in one call, advancing the APU inside every handshake spin.
// `cmd` is the mailbox command ($10/$11/$14/$15). Returns 1 loaded, 0 declined (the driver aborted it,
// or refused the defragmenter's command $07), -1 on a structural refusal.
// CtAudioLastLoadUnits: the last load's cost in the ROM's transfer unit, one $C7:09DA token handshake
// per 3 streamed bytes or per block head.
long CtAudioLastLoadUnits(long* bytes, long* blocks, long* spcCycles, long* settleCycles, long* movedBytes);
int CtAudioLoadSong(CtAudio* a, uint8_t* mem7E, int songId, uint8_t cmd);

// ---- Diagnostics ----
const uint8_t* CtAudioAram(const CtAudio* a);      // 64KB, for --aram-style diffing
// The SPC itself, for a translated stage (src/seq_xlat.h) to borrow: while the stage runs, its bus drives this APU
// and the host must not advance it with CtAudioFrame -- its samples are still read with CtAudioSamples.
struct Apu* CtAudioApu(CtAudio* a);
struct SndMachine* CtAudioMachine(CtAudio* a);   // the sound machine in C (NULL under CT_AUDIO_SPC=ref)
// The CPU's side of the ports, outside a JSL (a test harness, a stage's own post): CtAudioPortWrite writes $2140+k now;
// CtAudioRunCycles runs the sound processor `cycles` (apu_runCycles' way: to the first instruction starting at or after
// them) and returns the cycles run.
void      CtAudioPortWrite(CtAudio* a, int k, uint8_t v);
// The instant sound uploads (a mod, src/field.c hostSoundInstant): a JSL's SPC time is not owed to the next frames and
// its output is not played -- the host freezes no frame for it. Off = the SNES's timing.
void      CtAudioSetInstant(CtAudio* a, int on);
// `ct.exe --sndtiming <out>`: the sound machine's cost table (CT_ASSET_SND_TIMING) from the ROM; returns its bytes, 0 = failed.
int       CtAudioTimingExport(const uint8_t* rom, long romLen, const char* path);
int       CtAudioRunCycles(CtAudio* a, int cycles);
void CtAudioAdvance(CtAudio* a, long master);       // run the SPC for CPU work before a post, owed by the frame's slice
void CtAudioFrameOrigin(CtAudio* a, long master);   // the frame grid begins `master` cycles earlier (no SPC is run)
CtAudio* CtAudioInitPowerOn(const uint8_t* rom, long romLen, const CtAssets* snd);   // the SPC at power-on: no upload yet
void      CtAudioAdvanceTo(CtAudio* a, uint32_t target);   // run the SPC up to its cycle `target`, owed by the slices
uint32_t  CtAudioApuCycles(const CtAudio* a);               // the SPC's own cycle count (a stage's share of a frame)
void      CtAudioStageExitFrame(CtAudio* a, uint32_t ran);  // a translated stage left mid-frame: the frame's remaining SPC
// The reset's `JSL $C7:0000` (C0/0053) on the running driver: F0 hands the SPC to its IPL and the driver is uploaded
// again, on the loader clock with the reset's quiet background. Out: the master cycles and the frames it took.
int CtAudioColdBoot(CtAudio* a, long long* master, int* frames);
uint32_t       CtAudioAramFnv(const CtAudio* a);   // FNV-1a/32 over all of ARAM
void           CtAudioPorts(const CtAudio* a, uint8_t in[4], uint8_t out[4]);
int            CtAudioVoicesSounding(const CtAudio* a);   // DSP voices with gain != 0

#endif
