// ============================================================================
// audio.h — the SPC700 sound system (the port's fourth main system)
//
// Chrono Trigger's audio is not code we can re-implement: it is a SECOND
// PROCESSOR. The 65816 uploads Square's Akao sound driver into the SPC700's
// 64KB ARAM once at boot (bank $C7, over the IPL boot-ROM handshake) and from
// then on only ever posts 4-byte COMMANDS to the APU I/O ports $2140-$2143.
// Every note, instrument and envelope is the SPC700's own program running on
// its own DSP.
//
// So the faithful port drives the SAME emulated SPC700 the oracle uses
// (snes/apu.c + spc.c + dsp.c, already vendored for ct.exe) through the SAME
// upload and the SAME commands. That is the zelda3 model, and it is what
// disasm/notes/apu_upload.md called "the C drives the reused hardware"
// milestone.
//
// Canonical source: disasm/notes/audio_boot.md — every constant below is READ
// FROM THE ROM at run time (the $C7:0010 source table and the $C7:001C
// destination table), never hardcoded.
//
// Observation surface: build/ct.exe --aram (docs/DEBUG_SURFACE.md) dumps the
// oracle's ARAM, its $2140-43 ports and the DSP's live voices; CtAudioAram()
// below exposes the port's side of the same comparison.
// ============================================================================
#ifndef CT_AUDIO_H
#define CT_AUDIO_H

#include <stdint.h>

typedef struct CtAudio CtAudio;

// Boot the sound system: instantiate the SPC700+DSP, let its IPL boot ROM come
// up, then run bank $C7's upload (audio_boot.md §4) so the Akao driver lands in
// ARAM byte-identically to the oracle's. `rom` is the 4 MB ROM image (Belthasar
// already caches one via CtBattleRom); returns NULL when it is absent or short,
// so a ROM-less run stays silent instead of failing.
CtAudio* CtAudioInit(const uint8_t* rom, long romLen);
void     CtAudioFree(CtAudio* a);

// Non-zero once the six blocks are in ARAM and the driver has been started.
int      CtAudioReady(const CtAudio* a);

// Advance the SPC700 by exactly one NTSC frame (32040*32/60 = 17088 APU cycles,
// snes/snes.c:18); the fractional remainder is carried so long runs do not drift.
void     CtAudioFrame(CtAudio* a);

// Pull `samples` stereo frames (interleaved int16 L,R) — the vendored resampler,
// the same one the oracle feeds SDL with.
void     CtAudioSamples(CtAudio* a, int16_t* out, int samples);

// --- the command channel -----------------------------------------------------
// The game never writes $2140-43 directly: it stages a 4-byte command at
// $00:1E00-$1E03 and JSLs $C7:0004. CtAudioPost mirrors the raw flush at
// $C7:061C ($1E03->$2143, $1E02->$2142, $1E01->$2141, $1E00->$2140, high byte
// last-to-first exactly as the ROM does), which is the path taken by commands
// >= $80 and $18-$2F. Returns 0 and does nothing when the driver is not ready.
//
// NOT YET PORTED: the $C7:0140 service's other arms (the $10-$17 jump table at
// $C7:0AD9, and the $30-$3F / $70 / $71 handlers) — audio_boot.md §3 fences them
// as front-2 scope. Until they are derived, CtAudioService() below implements
// only the gates that ARE disassembled, and reports an unhandled command rather
// than guessing at one.
int      CtAudioPost(CtAudio* a, uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3);

// The $C7:0140 entry as far as it is derived: reads the 4-byte mailbox out of
// the engine's own WRAM page (`mem7E` + 0x1E00), applies the driver-present and
// zero-command gates, dispatches the arms that are known, and clears $1E00 on
// the way out like $C7:0192 does. Returns 1 if a command was consumed, 0 if
// there was nothing to do, and -1 for a command whose handler is not yet
// derived (the caller may log it; the mailbox is still cleared, as the ROM does).
int      CtAudioService(CtAudio* a, uint8_t* mem7E);
extern int g_ctAudioTraceFrame;   // the host's frame number, stamped on CT_SNDPOSTS's lines
// The LOADER CLOCK (audio.c, song_load_freeze.md section 6): every CtAudioService re-runs the JSL's
// master cycles -- the listing's instruction costs, the emulated SPC caught up before each port access,
// the frame's NMI/HDMA/refresh background at its raster position -- and reports what a synchronous
// upload cost the parked field: the NMIs crossed (frames lost) and the line the RTL landed on.
struct Apu;
void      CtAudioSeedFromApu(CtAudio* a, const struct Apu* src);   // adopt a savestate's SPC/DSP/ARAM (the oracle's driver state)
void      CtAudioFrameSkip(CtAudio* a);   // a displayed frame whose SPC time a translated stage's bus ran: the field still flips
// The debug snapshot's sound processor (field.c F5/F9): the SPC, the DSP, ARAM and the timers to a file, and back -- the
// WRAM a restore brings back holds the CPU's side of the APU protocol, which only works against the SPC of the same moment.
int       CtAudioSaveState(CtAudio* a, const char* path);
int       CtAudioLoadState(CtAudio* a, const char* path);
void      CtAudioSetEngineMode(CtAudio* a, int mode);   // 0 field, 1 battle: which measured background block
void      CtAudioSetPostLine(CtAudio* a, int vPos);     // the raster line the next JSL begins on (default 12)
// THE RULE (CT_ORACLE_NMION_HOLD=1 + CT_ORACLE_IFAST=1, the migration switches; CLAUDE.md "The SPC under the rule"): the
// infinitely fast CPU -- with the NMI on, a JSL costs only the SPC's own time from where the CPU's last wait left the
// beam, and the per-frame SPC slice runs from vblank to vblank. CtAudioRuleOn reads the switches once. Under it every
// CtAudioService in engine modes 0/1 starts at max(postAt, the SPC's progress into this frame's slice) -- master cycles
// past the start of vblank -- and a NMI met while it waits holds the CPU's poll for `nmiWait` (the handler's own waits:
// the pad latch's auto-read, a battle line wait); mode 2 (NMI off) keeps the listing's clock. Defaults: both = the pad
// latch's wait (CtAudioRulePadWait, the field's main loop). Off the rule nothing here is read.
int       CtAudioRuleOn(void);
void      CtAudioSetRulePost(CtAudio* a, long postAt, long nmiWait);
void      CtAudioSetRuleNmiOff(CtAudio* a, int off);    // the caller's JSLs run with the NMI off: the listing's clock
void      CtAudioSetRuleOff(CtAudio* a, int off);       // this SPC does not stand for the cartridge's: the listing's clock
// The V-IRQ armed while the CPU sits in a JSL (the rule): its handler waits for that line's H-blank, the beam's time --
// the field's C0/ECCC at line 211 (VTIME = $D3, C0/0B44) unless $010F bit 7 (C0/ECD5). -1 = none (the overworld).
void      CtAudioSetRuleIrq(CtAudio* a, int line);
// The HDMA the loader clock walks under the rule, priced from the channels (audio.c LdHdma): prog 1 = the field's
// (FD/C1EE's program, `$0128`, the set `$0153` names, the tables in `wram128`'s bank $7F); `on` = enabled now;
// `nmiOn` = an NMI met inside the JSL enables them (the battle's exit tail, D1/F4E6); `cutLine` = the V-IRQ line that
// disables them (D1/FEB5: 211), -1 none. prog 0 = the per-engine mean (the default). Kept across JSLs until reset.
void      CtAudioSetRuleHdma(CtAudio* a, const uint8_t* wram128, int prog, int on, int nmiOn, int cutLine);
long      CtAudioRulePadWait(void);
long      CtAudioRuleLineAt(int line);                  // vblank -> line `line` of the next picture, master cycles
// $7E:5AAA's `LDA $2143 / AND #$0F / BEQ` -- the title setup's wait for the Akao driver to answer,
// on the same clock as the JSL. Returns 1 when the driver answered and reports the FRAMES the spin
// cost (measured, not baked; the oracle's is just over six). 0 = it gave up at SPIN_LIMIT.
int       CtAudioWaitReplyNibble(CtAudio* a, int* frames);
long long CtAudioLastPostRaster(const CtAudio* a, int* frames, int* endLine, long* polls);   // master cycles

// Seed the CPU-side $1E00 page exactly as the upload tail $C7:00FF does
// (audio_boot.md §7 step 6), including the $1E05 "driver present" latch that
// gates every later $C7:0004 call. Call once after CtAudioInit with the engine's
// WRAM page.
void     CtAudioSeedMailboxPage(const CtAudio* a, uint8_t* mem7E);

// --- the song DIRECTORY (bank $C7's eight tables; disasm/notes/audio_song_loader_verify.md) ---
// Every quantity the ROM derives is derived here too -- the song count from rom[$C7:0AE9], the
// instrument count from the sample-pointer table's own extent, the BRR heap base from
// rom16($C7:40B4) + $4700 -- so none of 83 / 62 / $4901 appears as a literal. Block lengths always
// come from the block's own [u16 len] header.
int CtAudioSongCount(const CtAudio* a);
int CtAudioInstrumentCount(const CtAudio* a);
unsigned CtAudioHeapBase(const CtAudio* a);
const uint8_t* CtAudioSeqBlock(const CtAudio* a, int songId, int* lenOut);    // -> ARAM $2000
const uint8_t* CtAudioSampleBlock(const CtAudio* a, int instr, int* lenOut);  // 1-based
const uint8_t* CtAudioInstrSet(const CtAudio* a, int songId);                 // 32 bytes
int CtAudioSongRecord(const CtAudio* a, int songId, int* edl, int* scalar);   // $C7:241D + id*2
int CtAudioEchoBase(const CtAudio* a, int songId);   // ESA page = $F1 - EDL*8; buffer = [ESA*$100,$F100)

// Load a song: the port of $C7:01A1-$0619 (see audio_song_loader_verify.md). BLOCKING by the ROM's
// own design -- the routine contains no frame-wait at all, so it runs to completion in one call
// while advancing the APU inside every handshake spin. `cmd` is the mailbox command ($10/$11/$14/
// $15). Returns 1 loaded, 0 declined (the driver aborted it, or the load needs the unported ARAM
// defragmenter), -1 on a structural refusal.
// The last CtAudioLoadSong's cost in the ROM's own transfer unit: one $C7:09DA token handshake per 3
// streamed bytes or per command block. The oracle spends the whole upload inside `JSL $C7:0004` with
// the field engine frozen (39.6 frames for song $18 on the scene-8 gate fixture).
long CtAudioLastLoadUnits(long* bytes, long* blocks, long* spcCycles, long* settleCycles, long* movedBytes);
int CtAudioLoadSong(CtAudio* a, uint8_t* mem7E, int songId, uint8_t cmd);

// --- diagnostics -------------------------------------------------------------
const uint8_t* CtAudioAram(const CtAudio* a);      // 64KB, for --aram-style diffing
// The SPC itself, for a translated stage (src/seq_xlat.h) to borrow: while the stage runs, its bus drives this APU
// and the host must not advance it with CtAudioFrame -- its samples are still read with CtAudioSamples.
struct Apu* CtAudioApu(CtAudio* a);
uint32_t  CtAudioApuCycles(const CtAudio* a);               // the SPC's own cycle count (a stage's share of a frame)
void      CtAudioStageExitFrame(CtAudio* a, uint32_t ran);  // a translated stage left mid-frame: the frame's remaining SPC
// The reset's `JSL $C7:0000` (C0/0053) on the RUNNING driver: F0 hands the SPC to its IPL and the driver is uploaded
// again, on the loader clock with the reset's quiet background. Out: the master cycles and the frames it took.
int CtAudioColdBoot(CtAudio* a, long long* master, int* frames);
uint32_t       CtAudioAramFnv(const CtAudio* a);   // FNV-1a/32 over all of ARAM
void           CtAudioPorts(const CtAudio* a, uint8_t in[4], uint8_t out[4]);
int            CtAudioVoicesSounding(const CtAudio* a);   // DSP voices with gain != 0

#endif
