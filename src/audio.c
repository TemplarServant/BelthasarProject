// ============================================================================
// audio.c — bank $C7's Akao sound-driver upload + the runtime command channel,
// driving the vendored SPC700/DSP.
//
// Canonical source: disasm/notes/audio_boot.md (which supersedes and corrects
// disasm/notes/apu_upload.md). Section references below are to that dossier.
//
// Nothing here is invented and nothing is hardcoded that the ROM carries: the
// six block SOURCES and the six per-block ARAM DESTINATIONS are read from the
// ROM's own tables at $C7:0010 and $C7:001C, and the sizes are the leading size
// words of the blocks themselves. The upload's handshake is the SPC700 IPL boot
// ROM's protocol (transcribed in §4a from snes/apu.c's embedded boot ROM), so
// the port and the oracle rendezvous the same way.
// ============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>     // getpid: CT_SPC_COVERAGE's per-process file
#else
#include <unistd.h>
#endif
#include "audio.h"
#include "assets.h"
#include "sound/lockstep.h"
#include "snes.h"
#include "apu.h"
#include "spc.h"
#include "dsp.h"
#include "ppu.h"

// Bank $C7 is HiROM: file offset = 0x70000 + addr (audio_boot.md §0 header).
#define C7_BASE      0x70000
#define C7_SRC_TABLE 0x0010     // 6 words: block source pointers, bank $C7
#define C7_DST_TABLE 0x001C     // 6 words: block ARAM destinations (one PER BLOCK, §2)
#define C7_NBLOCKS   6          // loop terminator CPX #$000C at $C7:00C0

// APU cycles per NTSC frame: snes/snes.c:18 has apuCyclesPerMaster =
// (32040*32)/(1364*262*60.0), i.e. 32040*32 APU cycles per second; one frame is
// 1/60 of that. (The DSP emits a sample every 32 cycles, giving dsp.c's 534
// samples per frame -- the two constants agree exactly.)
#define APU_CYCLES_PER_SEC   (32040.0 * 32.0)
#define APU_CYCLES_PER_FRAME (APU_CYCLES_PER_SEC / 60.0)

// The rendezvous, modelled rather than tuned.
//
// TWO separate numbers, and conflating them is what produced the earlier mystery constant:
//
// STEP_CYCLES is only the POLL GRANULARITY while spinning on a handshake. For the IPL upload the
// outcome is genuinely invariant in it (audio_boot.md §7) -- both sides spin.
//
// SETTLE_CYCLES is the real mechanism. After the SPC echoes the token, the 65816 does NOT write the
// next unit immediately: it finishes C7/09DA (BNE/INC/AND/PLP/RTS), stores the token, decrements the
// counter three times, branches, and only then does LDA [$12],Y / STA $2141 -- roughly 35 CPU cycles,
// which at FastROM is on the order of TEN APU cycles. Our spin returns the instant the echo appears,
// so without an explicit settle we can overwrite bytes the SPC has not consumed.
//
// Measured with --songload at poll granularity 16, sweeping the settle:
//   0 -> 3/6 ARAM regions byte-exact,  8 -> 4/6,  11 -> 4/6,  16 -> 6/6,  32/64/96 -> 6/6
// So the threshold is 16 and the mechanism is confirmed in kind: what was needed was never a coarse
// poll (the old STEP_CYCLES 96) but the CPU's inter-unit time. 32 is used, double the threshold.
//
// STILL NOT FULLY DERIVED, and worth saying: the instruction-count estimate is ~10 APU cycles and the
// measured threshold is 16 -- a factor of 1.6 unexplained, so 32 is a measured value with margin, not
// a computed one. What would close it: count the 09DA tail in MASTER cycles against the emulator's
// own $2140 access timing, or drive the port's writes from a cycle-accurate CPU model.
// Bounded spin. The real handshake settles in a handful of quanta; this exists
// so a protocol mismatch degrades to "no sound" instead of hanging Belthasar.
#define SPIN_LIMIT  200000

// ==== THE HDMA THE RASTER RUNS, FROM ITS CHANNELS (snes/dma.c dma_initHdma / dma_doHdma) =================================
// The background a clocked JSL meets is the HDMA the NMI left enabled, priced line by line from the channels and their
// tables in the host's WRAM -- not a per-engine mean -- where the host says which program runs (CtAudioSetRuleHdma):
//   the FIELD's channels, programmed by FD/C1EE (the field NMI's, and C0/0AFF's -- which D1/F4C0, the battle's exit
//   tail, runs too): eight indirect channels, the DMAP / table bank / indirect bank immediates FD/C1F7-C262, the table
//   set by `$0153 & 0F` (FD/C264: zero -> set A at FD/C26D's addresses, else set B at FD/C297's), enabled by `$0128`.
// What a frame costs (LakeSnes's rules, the cycles the CPU sits out): at line 0 h 16 the init -- 8, and per enabled
// channel 8 for its line count and 16 more for an indirect address; at h 1104 of every visible line -- 8, and per live
// (enabled, not terminated) channel 8 a byte it transfers (doTransfer: an entry's first line, or every line of a repeat
// entry), 8 for the next count byte, and on an entry's end 16 for the next indirect address (8 when the table ended on
// the last live channel). Each such run is also aligned twice (snes_syncCycles: to the 8-cycle master clock on entry,
// to the CPU's access on exit) -- the absolute clock's phase, which no port has (the CPU-timing class, CLAUDE.md): that
// part is LD_HDMA_SYNC a run, the mean MEASURED on the rule's oracle (below).
typedef struct {
  uint8_t enabled, terminated, doTransfer, repCount, indirect, len;
  uint16_t tableAdr;
} LdHdmaCh;
typedef struct {
  int prog;                 // 0: the per-engine mean (LD_BG); 1: the field's channels (FD/C1EE)
  const uint8_t* wram;      // the host's 128 KB image ($7E at 0, $7F at 0x10000)
  int on;                   // $420C as the channels see it now: 1 = `$0128`'s channels, 0 = none
  int nmiOn;                // 1: an NMI taken inside C7 writes `$0128` (D1/F4E6's absolute STA $420C, DB = 0)
  int cutLine;              // the V-IRQ that writes $420C = 0 (D1/FEB5, line 211); -1 none
  LdHdmaCh ch[8];
  long long dbgFrame;       // CT_AUDIO_LDFRAMES: this frame's HDMA cycles
} LdHdma;

#define CT_AUD_FIFO (1 << 16)   // CtAudio.fifo, in samples (stereo pairs) -- 122 frames, more than the longest load (55.8)
struct CtAudio {
  const uint8_t* rom;                // retained for the song directory below (the IPL upload also
  long  romLen;                      //   reads it; both read, neither writes)
  Snes* shell;      // zeroed stand-in: dsp_getSamples reads exactly one field of
                    // it (dsp->apu->snes->palTiming, snes/dsp.c:586) and nothing
                    // in apu.c/spc.c/dsp.c ever touches another. No snes.c needed.
  Apu*  apu;
  int   ready;
  double cycleCarry;                 // fractional APU cycles across frames; a clocked JSL's SPC time is DEBITED here
  int evenFrame;                     // the PPU's field this frame of SPC stands for (ppu.c evenFrame: an odd frame is short)
  int engineMode;                    // 0 field, 1 battle: the background block the loader clock charges
  int postLine;                      // raster line a JSL $C7:0004 begins on (default 12: the runner's early queue, measured)
  int lastFrames, lastEndLine;       // the last clocked JSL: NMIs crossed, the line it returned on
  long lastPolls; long long lastMaster;   // ...its $2140 polls and master cycles
  int mutedFrame;                    // CtAudioFrame skipped this frame: a clocked JSL already ran its SPC time
  long rulePostAt, ruleNmiWait;      // the rule (CtAudioSetRulePost): where the CPU posts, master cycles past the vblank,
                                     //   and how long a later NMI's handler waits before the poll goes on
  long lastRuleStart;                // the last rule JSL's start, master cycles past the vblank (traces)
  int ruleNmiOff;                    // the rule: the caller runs with the NMI disabled (CtAudioSetRuleNmiOff) -- the listing's clock
  int ruleOff;                       // the rule does not apply to this instance (CtAudioSetRuleOff): its SPC is not the cartridge's
  int ruleIrqLine;                   // the rule: the V-IRQ line whose handler waits for its H-blank (CtAudioSetRuleIrq; -1 none)
  LdHdma hdma;                       // the HDMA the loader clock walks (CtAudioSetRuleHdma); kept across the JSLs of a chain
  // THE OUTPUT IS THE DSP'S OWN STREAM. snes/dsp.c keeps its last 0x400 samples in a ring and dsp_getSamples reads the
  // newest 534 of them -- right for the reference, which runs exactly one frame of SPC between two reads, and wrong for
  // a clocked `JSL $C7:0004`, which runs up to ~56 frames of SPC inside ONE host frame: all but the last two of those
  // frames were overwritten before anything read them, and the frames the JSL had already paid for were output as
  // SILENCE. So every sample the DSP emits is taken into this FIFO as it is emitted (apuRun), and each output frame is
  // the next 534 of them, in order.
  int16_t fifo[CT_AUD_FIFO * 2];
  unsigned fifoHead, fifoLen;        // head = the oldest sample
  uint16_t dspRead;                  // dsp->sampleOffset already taken
  long fifoDropped;                  // samples lost to a full FIFO or to more than 0x400 emitted between two takes
  uint16_t src[C7_NBLOCKS];          // as read from $C7:0010
  uint16_t dst[C7_NBLOCKS];          // as read from $C7:001C
  uint16_t size[C7_NBLOCKS];         // each block's leading size word
  const uint8_t* blk[C7_NBLOCKS];    // each block's bytes (after the size word): the ROM's or the container's
  const CtAssets* snd;               // the asset container the sound's data comes from, or NULL (the ROM)
  int ninst;                         // the instruments the sample table holds
};

static void W(CtAudio* a, int k, uint8_t v) { a->apu->inPorts[k] = v; }
static uint8_t R(CtAudio* a, int k)         { return a->apu->outPorts[k]; }
static int ctStepCycles(void) {   // experiment hook: CT_AUDIO_QUANTUM overrides the 96 above
  static int n = -1;
  if (n < 0) { const char* e = getenv("CT_AUDIO_QUANTUM"); n = e ? atoi(e) : 16; }
  return n;
}
static long g_sndSpcCycles;   // SPC700 cycles stepped inside the current CtAudioLoadSong (the upload's own clock)
static long g_sndSettleCycles;   // ...of which the post-echo SETTLE (the 65816's inter-unit time, handed to the SPC)
static long g_sndMovedBytes;     // bytes the DEFRAGMENTER had the SPC move (command $07): its cost is the SPC's memmove, not a handshake
// Take every sample the DSP emitted since the last take (snes/dsp.c writes sampleBuffer[(sampleOffset & 0x3ff) * 2]
// then sampleOffset++). More than the ring's 0x400 between two takes cannot be recovered; apuRun never lets that happen.
static void audTake(CtAudio* a) {
  Dsp* dsp = a->apu->dsp;
  unsigned n = (uint16_t)(dsp->sampleOffset - a->dspRead);
  if (n > 0x400) { a->fifoDropped += n - 0x400; a->dspRead = (uint16_t)(dsp->sampleOffset - 0x400); n = 0x400; }
  for (unsigned i = 0; i < n; i++) {
    unsigned r = ((unsigned)a->dspRead + i) & 0x3FF;
    if (a->fifoLen == CT_AUD_FIFO) { a->fifoHead = (a->fifoHead + 1) % CT_AUD_FIFO; a->fifoLen--; a->fifoDropped++; }
    unsigned w = (a->fifoHead + a->fifoLen) % CT_AUD_FIFO;
    a->fifo[w * 2] = dsp->sampleBuffer[r * 2]; a->fifo[w * 2 + 1] = dsp->sampleBuffer[r * 2 + 1];
    a->fifoLen++;
  }
  a->dspRead = dsp->sampleOffset;
}
// apu_runCycles, taking the samples as it goes: 0x400 samples are 32768 APU cycles, so a run is cut into pieces well
// under that. Returns the cycles run, like apu_runCycles.
static long long g_apuTotal;   // CT_AUDIO_SPCCLOCK: every SPC cycle this CtAudio ran, printed per CtAudioFrame
static int apuRun(CtAudio* a, int cycles) {
  int ran = 0;
  while (ran < cycles) {
    int chunk = cycles - ran; if (chunk > 16000) chunk = 16000;
    ran += apu_runCycles(a->apu, chunk);
    audTake(a);
  }
  g_apuTotal += ran;
  return ran;
}
static void audReset(CtAudio* a) { a->fifoHead = a->fifoLen = 0; a->dspRead = a->apu->dsp->sampleOffset; }
static void step(CtAudio* a)                { int c = ctStepCycles(); g_sndSpcCycles += c; apuRun(a, c); }

// ==== THE LOADER CLOCK (disasm/notes/song_load_freeze.md section 6) ==========================
// The ROM's song upload is SYNCHRONOUS: `JSL $C7:0004` returns only after the SPC has echoed the
// last unit, and the field's main loop is parked for the whole of it (39.6 frames for song $18 at
// the scene-8 pendant, 55.8 for the battle song). A clockless port cannot know that duration, so
// this clock re-runs the JSL's time the way the oracle spends it:
//   * the 65816 side is COUNTED FROM THE LISTING: every instruction of the unit loop C7/06CD-06F5
//     and the token handshake C7/09DA-09E9 at LakeSnes's access costs -- ROM 6 (FASTROM: the boot
//     writes $420D = 1 at C2/0026, so banks >= $80 run at 6 cycles), WRAM 8, the B-bus ports
//     $2140-$2143 6, internal 6. A unit is 592 master cycles plus 42 per extra poll of $2140
//     (CMP 24 + BNE 18); a port write lands at the END of the STA's B-bus cycle and a poll reads at
//     the end of the CMP's, exactly where snes_cpuWrite and snes_cpuRead place them;
//   * the SPC side is the EMULATED DRIVER, caught up before every port access by the master cycles
//     elapsed since the last one x apuCyclesPerMaster, with LakeSnes's whole-opcode overshoot carried
//     (snes_catchupApu). So the number of polls a handshake takes -- the SPC's response latency,
//     the defragmenter's memmoves, the driver's preamble -- is not modelled, it is RUN. Measured
//     both ways on the scene-8 pendant load: the oracle takes 15.4 extra polls per unit
//     (`--hookpc C709E0` 4355 visits / 266 units at f170), the seeded port 12.5 at the old 52-cycle
//     poll = the same ~650 master cycles of SPC response;
//   * the frame's BACKGROUND is inserted at its raster position as STOLEN time -- the raster keeps
//     walking through it and the SPC keeps running, the loader does not: DRAM refresh 40 cycles when
//     a line crosses hPos 536 (snes_runCycles), the active HDMA channels at hPos 1104 on lines
//     0-224, and the NMI + IRQ handlers (with the DMA they perform) when the raster reaches line
//     225. Those two are per ENGINE MODE, not per scene, and were MEASURED on the oracle with the
//     cycle-tag probe (`CT_HOOK_CYCLES=1 ct.exe --hookpc C709E5 --from <state>`, which splits every
//     frame's 357,366 master cycles into main / NMI / DMA / HDMA / refresh): with the main loop
//     parked the FIELD's NMI+IRQ path costs 12,097 (+-100) cycles a frame and performs no DMA, its
//     six HDMA channels 14,310 (+-200; the same on Leene Square and the forest field); the BATTLE
//     engine's NMI+IRQ costs 27,600 on average with 5,434 of DMA in a 4-frame pattern, its eight
//     channels 17,904. The variation inside a mode is below 0.1% of the frame -- the joypad-wait
//     alignment and the HDMA tables' entry counts -- and is the one thing here that is not derived
//     instruction by instruction.
// What comes out: the master cycles the JSL took, the NMIs it crossed (the frames the field lost) and
// the line it returned on (>= 72 = the runner's raster gate C0/5A5D cuts the rest of the queue).
// Everything the old poll quantum and post-echo SETTLE stood in for is now the listing's own timing;
// both survive only for the unclocked callers (--audiotest, the IPL upload).
#define LD_MASTER_PER_LINE 1364
#define LD_LINES_PER_FRAME 262
#define LD_APU_PER_MASTER  (APU_CYCLES_PER_SEC / (1364.0 * 262.0 * 60.0))   // = snes.c apuCyclesPerMaster
// access costs (snes_getAccessTime): a ROM byte, a WRAM byte, a B-bus register, an internal cycle
#define RA 6
#define WA 8
#define BA 6
#define IA 6
// instruction costs, M=8 X=16, DP=$1E00 (no direct-page penalty), code in bank $C7
#define I_LDA_DPIY_ROM (RA + RA + 3 * WA + RA)   // LDA [dp],Y with the data in ROM         42
#define I_STA_BB       (3 * RA + BA)              // STA $21xx                              24
#define I_CMP_BB       (3 * RA + BA)              // CMP $21xx                              24
#define I_LDA_BB       (3 * RA + BA)              // LDA $21xx                              24
#define I_IMPL         (RA + IA)                  // INY DEX INC A CLC TYA TYX TAX ASL      12
#define I_XBA          (RA + 2 * IA)              //                                        18
#define I_BR_NT        (2 * RA)                   // branch not taken                       12
#define I_BR_T         (2 * RA + IA)              // branch taken                           18
#define I_LDA_DP       (2 * RA + WA)              // LDA/STA/STZ dp, M=8                    20
#define I_LDA_DP16     (2 * RA + 2 * WA)          // ...M=16 / X=16                         28
#define I_LDA_DPX      (2 * RA + IA + WA)         // LDA/STA/STZ dp,X, M=8                  26
#define I_LDA_DPX16    (2 * RA + IA + 2 * WA)     // ...M=16                                34
#define I_INC_DP       (2 * RA + WA + IA + WA)    // INC dp, M=8                            34
#define I_IMM8         (2 * RA)                   // LDA/AND/CMP #imm8                      12
#define I_IMM16        (3 * RA)                   // LDX/LDY/CPX #imm16                     18
#define I_ABSY_W16     (3 * RA + 2 * WA)          // LDA/STA abs,Y in WRAM, M=16            34
#define I_ABSX_W8      (3 * RA + WA)              // LDA abs,X in WRAM, M=8                 26
#define I_ABSX_W8_X16  (3 * RA + IA + WA)         // ...with X=16 (the index's extra cycle) 32
#define I_ABSX_W16_X16 (3 * RA + IA + 2 * WA)     // LDA/CMP abs,X|Y in WRAM, M=16 X=16     40
#define I_LONGX_ROM8   (4 * RA + RA)              // LDA long,X from ROM, M=8                30
#define I_LONGX_ROM16  (4 * RA + 2 * RA)          // LDA/ADC long,X from ROM, M=16           36
#define I_STA_DPI16    (2 * RA + 2 * WA + 2 * WA) // STA ($12), M=16                        44
#define I_INC_DP16     (2 * RA + 2 * WA + IA + 2 * WA) // INC dp, M=16                      50
#define I_PHX16        (RA + IA + 2 * WA)         // PHX, X=16                              28
#define I_PLX16        (RA + 2 * IA + 2 * WA)     // PLX, X=16                              34
#define I_JSR          (3 * RA + IA + 2 * WA)     //                                        40
#define I_RTS          (RA + 2 * IA + 2 * WA + IA) //                                       40
#define I_PHP          (RA + IA + WA)             //                                        20
#define I_PLP          (RA + 2 * IA + WA)         //                                        26
#define I_SEP          (2 * RA + IA)              // SEP / REP                              18
typedef struct {
  int active;                 // a clocked CtAudioService is running
  long long clk, lastClk;     // master cycles since the JSL, and the clock at the last APU catch-up
  int hPos, vPos;             // raster position, LakeSnes coordinates (hPos 0..1363, vPos 0..261)
  int nmis;                   // frame boundaries crossed (NMIs taken while the main loop was parked)
  long stolen;                // background cycles queued at the raster: the CPU sits them out
  double owed, hdmaFrac;      // APU cycles owed (snes->apuCatchupCycles); HDMA per-line fraction carry
  int mode;                   // 0 = field, 1 = battle: which background block the raster inserts
  long long budget;           // runaway guard: a broken handshake must not spin the raster forever
  long polls;                 // $2140 polls taken across the run (a measurement aid)
  int ifast;                  // THE RULE (CtAudioRuleOn, NMI on): the CPU's work is free, only its waits cost (below)
  long nmiWait;               // ...master cycles the NMI handler itself waits from the vblank (its pad latch, a line wait)
  int irqLine;                // ...the V-IRQ whose handler waits for that line's H-blank (-1: none armed), and
  int irqPending, irqWaiting; //    the wait met at the raster / being walked (the refresh inside it costs it nothing)
  LdHdma* hd;                 // the HDMA program the raster walks (CtAudioSetRuleHdma), or NULL: LD_BG's mean
  long long dbgCpu, dbgCpuAll, dbgBg[4], clk0, cpu0;   // (clk0 / cpu0: the clock and CPU sum where C7 begins)  // CT_AUDIO_LDFRAMES: master cycles the CPU spent / refresh, hdma, -, the IRQ's wait
} LdClock;
static LdClock ld;
// ==== THE RULE: THE INFINITELY FAST CPU (CLAUDE.md, "The infinitely fast CPU"; snes/snes.c lfIfast is the oracle's) =====
// On with CT_ORACLE_NMION_HOLD=1 and CT_ORACLE_IFAST=1 (the migration switches; without both nothing below changes). The
// zelda3 model taken whole: with the NMI enabled the beam, the timers and the APU advance ONLY while the CPU waits, every
// other cycle is held -- EXCEPT the CPU's side of the APU protocol: the sound driver's own code, bank C7 (entered only by
// `JSL C7/0004` and the reset's `JSL C7/0000`), runs at its real instruction cost on both sides, because the two-processor
// transfer is only correct at the CPU's real speed (the driver's receive loop, ARAM $1104-$111B, echoes the token at $110E
// after reading port 1 alone and reads ports 2-3 after the echo, in the ~35 cycles C7/09E5-09E9 and the next unit's
// `LDA [$12],Y / STA $2141` take; at zero CPU time the next unit overwrote bytes 2-3 of every unit -- the 0AD boot's
// `70 01` set B 3776 of 6144 bytes wrong). So this clock is the listing's for everything C7 does (ldRun, every poll round),
// and what the rule changes is the rest:
//   * the JSL STARTS where the CPU's LAST WAIT left the beam -- the CPU's work before it, the caller's `JSL` included, is
//     free: the field's main loop and the battle's run right after the NMI's RTI, and the NMI's work is free too except its
//     own waits -- the pad latch C2/84D2 (`C2/84F7 LDA #01 / BIT $4212 / BNE`, the auto-joypad read: 4224 master cycles
//     from the start of vblank, snes.c `autoJoyTimer = 4224`), reached from C0/EC15 (field), C2/031B (overworld), CD/091A
//     and CD/09C6 (battle), and in the battle's action frames CD/09AA's `LDA #40 / JSR CD/3B82` line wait (and the
//     program's op C1 waits) BEFORE CD/09C6's pad latch.
//     MEASURED on the IFAST oracle, the attract from attract_cena_inicial (PEEK_APUTRACE): every field post of the main
//     loop enters C7/0004 at line 228 h 146-200 (the pad's 4224 = line 228 h 132, plus the wait loop's phase), the battle's
//     idle-loop posts at 228 too (GATO's chimes, gato_wait), its action-frame posts at line 64 / 128 (the X-Strike, the
//     Frog Squash), the battle boot's #08 (CD/04FD, a boot NMI with no pad latch) at 225 h 12-22.
//   * an NMI met inside the JSL runs in its own bank: its work is free, its own waits (`nmiWait`) are beam time; so is the
//     field's V-IRQ's wait for line 211's H-blank (ldIrqWait, where the host says one is armed: CtAudioSetRuleIrq).
//   * NMI-OFF work keeps its own rule (CLAUDE.md, "NMI-off loads"): engine mode 2 (a location's load, the reset, the title's
//     setup) runs on the listing's clock with its own background, as before.
// The caller says where in the frame the CPU stands (CtAudioSetRulePost); the SPC's progress into the frame's slice --
// what this frame's earlier posts already ran -- is the floor, so a second post of the same frame starts where the first
// returned.
#define LD_PAD_WAIT 4224L     // the auto-joypad read the pad latch's C2/84F7 waits out (snes/snes.c `autoJoyTimer = 4224`)
static int g_audRule = -1;
int CtAudioRuleOn(void) {
  if (g_audRule < 0) { const char* h = getenv("CT_ORACLE_NMION_HOLD"), *f = getenv("CT_ORACLE_IFAST");
                       g_audRule = (!(h && *h == '0') && !(f && *f == '0')); }
  return g_audRule;
}
static long ldHist[65];              // CT_AUDIO_LDTRACE=1: polls per token handshake, 0..63 and 64+
static int ldTrace(void) { static int n = -1; if (n < 0) { const char* e = getenv("CT_AUDIO_LDTRACE"); n = (e && *e) ? 1 : 0; } return n; }
static const struct { long nmiBlock; double hdmaLine; } LD_BG[3] = {
  { 12097,        14310.0 / 225.0 },   // FIELD: NMI+IRQ, no DMA; six HDMA channels
  { 27600 + 5434, 17904.0 / 225.0 },   // BATTLE: NMI+IRQ + its DMA (4-frame mean); eight channels
  { 0,            0.0 },               // RESET: FD:C000 wrote $4200 = 0 and $420C = 0 -- no NMI, no HDMA
};
static CtAudio* g_ldA;               // CT_AUDIO_LDFRAMES: the CtAudio the rule's clock runs (set by ldStartRule)

// ---- the field's channels (LdHdma prog 1): FD/C1EE's program, read from its own immediates ---------------------------
// snes_syncCycles' two alignments of one HDMA run, MEASURED on the rule's oracle against the deterministic cost of the same
// frames (the cartridge's own tables through this model, tools-side the scratch hsim): the scene-8 gate's `10 18` upload,
// poll-only frames f155-164 14,478 each = 12,664 + 226 runs x 8.03, the unit frames' mean the same; enemy 6E's exit chain,
// f7-f12 mean 14,182 = 12,416 + 212 x 8.33. The phase they depend on is the master clock's against the CPU's accesses --
// not uniform (that would be 8.5 a run for a 56-cycle line) and not the port's to know (the CPU-timing class).
#define LD_HDMA_SYNC 8
static const uint8_t LD_HDMA_LEN[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };   // snes/dma.c transferLength
static uint8_t ldRomFD(const CtAudio* a, unsigned off) {             // a byte of bank $FD
  long p = 0x3D0000L + (long)off;
  return (a && a->rom && p < a->romLen) ? a->rom[p] : 0;
}
// FD/C1EE's program as the clock needs it: channel i's DMAP, and its table in set A or set B -- the container's
// (CT_ASSET_SND_MISC 1) or the immediates read from the ROM.
static uint8_t ldFdDmap(const CtAudio* a, int i) {
  uint32_t n = 0; const uint8_t* m = a && a->snd ? CtAssetGet(a->snd, CT_ASSET_SND_MISC, 1, &n) : NULL;
  if (m && n >= 40) return m[i];
  return ldRomFD(a, 0xC1F8 + 4 * (unsigned)i);
}
static uint16_t ldFdTable(const CtAudio* a, int setB, int i) {
  uint32_t n = 0; const uint8_t* m = a && a->snd ? CtAssetGet(a->snd, CT_ASSET_SND_MISC, 1, &n) : NULL;
  if (m && n >= 40) { const uint8_t* t = m + 8 + (setB ? 16 : 0) + 2 * i; return (uint16_t)(t[0] | (t[1] << 8)); }
  unsigned at = setB ? 0xC298 + 5 * (unsigned)i : 0xC26E + 5 * (unsigned)i;   // FD/C26D / C297 `LDX #tbl / STX $x2`
  return (uint16_t)(ldRomFD(a, at) | (ldRomFD(a, at + 1) << 8));
}
static uint8_t ldHd7F(const LdHdma* hd, unsigned adr) { return hd->wram ? hd->wram[0x10000 + (adr & 0xFFFF)] : 0; }
// Line 0, h 16 (snes.c: `vPos == 0` -> hdmaInitRequested): FD/C1EE's channels at the set `$0153 & 0F` names, `$0128`'s
// enabled -- each loads its line count and, indirect, its address. 0 when none is enabled (dma_initHdma returns).
static long ldHdmaInit(LdHdma* hd, const CtAudio* a) {
  long cost = 0; int any = 0;
  uint8_t mask = hd->on ? (hd->wram ? hd->wram[0x0128] : 0) : 0;
  int setB = hd->wram ? (hd->wram[0x0153] & 0x0F) != 0 : 0;       // FD/C264 LDA $0153 / AND #0F / BEQ ->C26D (set A)
  for (int i = 0; i < 8; i++) {
    LdHdmaCh* c = &hd->ch[i];
    c->enabled = (mask >> i) & 1; c->terminated = 0; c->doTransfer = 0;
    if (!c->enabled) continue;
    any = 1;
    uint8_t dmap = ldFdDmap(a, i);                                  // FD/C1F7 LDA #44 / STA $00 .. FD/C213 LDA #40 / STA $70
    c->tableAdr = ldFdTable(a, setB, i);                            // FD/C26D / C297 `LDX #tbl / STX $x2`, eight of each
    c->indirect = (dmap & 0x40) != 0; c->len = LD_HDMA_LEN[dmap & 7];
    cost += 8;                                                      // the count byte
    c->repCount = ldHd7F(hd, c->tableAdr++);                        // (the table bank: FD/C242 LDA #7F / STA $x4)
    if (c->repCount == 0) c->terminated = 1;
    if (c->indirect) { cost += 16; c->tableAdr += 2; }              // the indirect address, two bytes
    c->doTransfer = 1;
  }
  return any ? 8 + cost + LD_HDMA_SYNC : 0;                         // + the run's own overhead
}
// h 1104 of a visible line (hdmaRunRequested): the transfers, then each live channel's count and its next entry.
static long ldHdmaLine(LdHdma* hd) {
  int any = 0, lastActive = 0;
  for (int i = 0; i < 8; i++) if (hd->ch[i].enabled) { any = 1; if (!hd->ch[i].terminated) lastActive = i; }
  if (!any) return 0;
  long cost = 8 + LD_HDMA_SYNC;
  for (int i = 0; i < 8; i++) {
    LdHdmaCh* c = &hd->ch[i];
    if (!c->enabled || c->terminated || !c->doTransfer) continue;
    cost += 8L * c->len;
    if (!c->indirect) c->tableAdr = (uint16_t)(c->tableAdr + c->len);
  }
  for (int i = 0; i < 8; i++) {
    LdHdmaCh* c = &hd->ch[i];
    if (!c->enabled || c->terminated) continue;
    c->repCount--; c->doTransfer = (c->repCount & 0x80) != 0;
    cost += 8;
    uint8_t next = ldHd7F(hd, c->tableAdr);
    if ((c->repCount & 0x7F) == 0) {
      c->repCount = next; c->tableAdr++;
      if (c->indirect) {
        if (c->repCount == 0 && i == lastActive) { cost += 8; c->tableAdr += 1; }   // only the high byte
        else { cost += 16; c->tableAdr += 2; }
      }
      if (c->repCount == 0) c->terminated = 1;
      c->doTransfer = 1;
    }
  }
  return cost;
}
static void ldCatch(CtAudio* a);
static void ldWalk(long cycles) {        // wall time passes: the raster walks and queues the background it meets
  while (cycles > 0) {
    int next = (ld.hPos < 16) ? 16 : (ld.hPos < 536) ? 536 : (ld.hPos < 1104) ? 1104 : LD_MASTER_PER_LINE;
    long stepc = next - ld.hPos; if (stepc > cycles) stepc = cycles;
    ld.clk += stepc; ld.hPos += (int)stepc; cycles -= stepc;
    if (ld.hPos == 16) {                                                  // line 0: the HDMA init (the program's)
      if (ld.hd && ld.vPos == 0) { long h = ldHdmaInit(ld.hd, g_ldA); ld.stolen += h; ld.dbgBg[1] += h; } }
    else if (ld.hPos == 536) { if (!ld.irqWaiting) { ld.stolen += 40; ld.dbgBg[0] += 40; } }     // DRAM refresh (a wait does not feel it)
    else if (ld.hPos == 1104) {                                           // HDMA run, visible lines only
      if (ld.vPos <= 224 && ld.hd) { long h = ldHdmaLine(ld.hd); ld.stolen += h; ld.dbgBg[1] += h; }
      else if (ld.vPos <= 224) { ld.hdmaFrac += LD_BG[ld.mode].hdmaLine; long h = (long)ld.hdmaFrac; ld.hdmaFrac -= (double)h; ld.stolen += h; ld.dbgBg[1] += h; } }
    else if (ld.hPos == LD_MASTER_PER_LINE) { ld.hPos = 0; ld.vPos++;
      if (ld.vPos == LD_LINES_PER_FRAME) ld.vPos = 0;
      if (ld.ifast && ld.vPos == ld.irqLine) ld.irqPending = 1;          // the rule: the V-IRQ (its wait, ldRun below)
      if (ld.hd && ld.vPos == ld.hd->cutLine) {                        // ...whose `STA $420C` = 0 (D1/FEB5) ends the HDMA
        ld.hd->on = 0; for (int i = 0; i < 8; i++) ld.hd->ch[i].enabled = 0; }
      if (ld.hd && ld.vPos == 225 && ld.hd->nmiOn) ld.hd->on = 1;      // the NMI, taken inside C7: `$0128` to $420C
      if (ld.vPos == 225) { ld.nmis++;                                    // vblank: the NMI is taken
        { static int lf = -1; if (lf < 0) { const char* e = getenv("CT_AUDIO_LDFRAMES"); lf = (e && *e) ? 1 : 0; }
          if (lf && g_ldA) { ldCatch(g_ldA); const uint8_t* r = g_ldA->apu->ram;
            fprintf(stderr, "ldframe nmi%d $83=%02X $97=%02X%02X clk=%lld cpu=%lld refresh=%lld hdma=%lld irq=%lld polls=%ld\n", ld.nmis, r[0x83], r[0x98], r[0x97], ld.clk, ld.dbgCpu, ld.dbgBg[0], ld.dbgBg[1], ld.dbgBg[3], ld.polls);
            ld.dbgCpu = 0; ld.dbgBg[0] = ld.dbgBg[1] = ld.dbgBg[3] = 0; } }
        if (ld.ifast) ld.stolen += ld.nmiWait;                          //   (the rule: its work free, its waits the beam's)
        else ld.stolen += LD_BG[ld.mode].nmiBlock; } }
  }
}
// THE RULE's V-IRQ (ld.irqLine, CtAudioSetRuleIrq): the field's interrupt C0/ECCC -- VTIME = 211 (C0/0B44 `LDA #00D3 /
// STA $4209`), re-armed by every field NMI (C0/EA70 `LDA #A1 / STA $4200`) -- takes the CPU out of C7 at line 211's start
// and waits for that line's H-blank (C0/ECEB `LDA $4212 / BIT #40 / BEQ`, HBlank = h >= 1096, snes.c $4212); its work
// before and after the wait is free, so the C7 code resumes at the H-blank. MEASURED on the IFAST oracle inside the scene-8
// gate cutscene's `10 18` upload (--cputrace, gate state f160): the IRQ entered at line 211 h 56 (the C7 instruction and
// the interrupt's pushes), the wait left at h 1320 (the read after line 211's HDMA, 1104-1272), the beam held from there.
static void ldIrqWait(void) {
  ld.irqPending = 0;
  if (ld.vPos != ld.irqLine || ld.hPos >= 1096) return;
  ld.irqWaiting = 1; ld.dbgBg[3] += 1096 - ld.hPos; ldWalk(1096 - ld.hPos); ld.irqWaiting = 0;
}
static void ldRun(long cycles) {         // the loader's CPU spends `cycles` -- after sitting out whatever the raster queued
  if (!ld.active || cycles <= 0 || ld.clk > ld.budget) return;
  while (cycles > 0) {
    if (ld.stolen > 0) { long b = ld.stolen; ld.stolen = 0; ldWalk(b); continue; }   // the background runs first (it may queue more)
    if (ld.irqPending) { ldIrqWait(); continue; }
    int next = (ld.hPos < 536) ? 536 : (ld.hPos < 1104) ? 1104 : LD_MASTER_PER_LINE;
    long chunk = next - ld.hPos; if (chunk > cycles) chunk = cycles;
    ld.dbgCpu += chunk; ld.dbgCpuAll += chunk; ldWalk(chunk); cycles -= chunk;
  }
  if (ld.stolen > 0) { long b = ld.stolen; ld.stolen = 0; ldWalk(b); }   // an event met at the chunk's end
  if (ld.irqPending) ldIrqWait();                                         // (the rule only: irqPending is never set off it)
}
static void ldCatch(CtAudio* a) {        // snes_catchupApu: run the SPC up to the CPU's clock before a port access
  if (!ld.active) return;
  ld.owed += (double)(ld.clk - ld.lastClk) * LD_APU_PER_MASTER; ld.lastClk = ld.clk;
  int n = (int)ld.owed;
  if (n > 0) { int ran = apuRun(a, n); ld.owed -= (double)ran; g_sndSpcCycles += ran; }
}
static void    Wc(CtAudio* a, int k, uint8_t v) { ldCatch(a); W(a, k, v); }   // snes_cpuWrite -> writeBBus $2140+k
static uint8_t Rc(CtAudio* a, int k)            { ldCatch(a); return R(a, k); }   // snes_cpuRead  -> readBBus
static void ldStart(int mode, int vPos) {
  memset(&ld, 0, sizeof ld); ld.active = 1; ld.mode = (mode >= 0 && mode <= 2) ? mode : 0;
  ld.vPos = (vPos >= 0 && vPos < LD_LINES_PER_FRAME) ? vPos : 12; ld.budget = 400LL * LD_MASTER_PER_LINE * LD_LINES_PER_FRAME;
}
// The rule's start (CtAudioRuleOn, NMI on): the clock's origin is the SPC itself -- its progress into this frame's slice,
// the master cycles past the vblank this frame's earlier posts ran it to -- and the beam stands there; if the CPU's post
// comes later in the frame (the caller's `postAt`: the NMI's own waits), the SPC first runs up to it. Returns 1 when the
// rule's clock was set up.
static int ldStartRule(CtAudio* a) {
  if (!CtAudioRuleOn() || a->ruleOff || a->engineMode == 2 || a->ruleNmiOff) return 0;
  long spcPos = a->cycleCarry < 0 ? (long)(-a->cycleCarry / LD_APU_PER_MASTER) : 0;   // master cycles past the vblank
  long frame = (long)LD_MASTER_PER_LINE * LD_LINES_PER_FRAME;
  memset(&ld, 0, sizeof ld); ld.active = 1; ld.mode = a->engineMode; ld.ifast = 1; ld.nmiWait = a->ruleNmiWait; g_ldA = a;
  ld.irqLine = a->ruleIrqLine;
  ld.hd = a->hdma.prog ? &a->hdma : NULL;
  ld.budget = 400LL * frame;
  long at = spcPos % frame;                                          // a post past an earlier one's frames counts its own
  long pos = 225L * LD_MASTER_PER_LINE + at;                         // the vblank's line 225, h 0, + the offset
  pos %= frame; ld.vPos = (int)(pos / LD_MASTER_PER_LINE); ld.hPos = (int)(pos % LD_MASTER_PER_LINE);
  if (a->rulePostAt > at && a->rulePostAt < frame) ldRun(a->rulePostAt - at);   // the NMI's waits: the SPC runs on
  a->lastRuleStart = at > a->rulePostAt ? at : a->rulePostAt;
  ld.clk0 = ld.clk; ld.cpu0 = ld.dbgCpuAll;
  return 1;
}
// the $400-poll loops the ROM uses outside the token (C7/0A6E, C7/0603, C7/062F): CMP $2140, BEQ, DEX, BNE
static int ldPoll400(CtAudio* a, uint8_t want) {
  for (int px = 0; px < 0x400; px++) { ldRun(I_CMP_BB); if (Rc(a, 0) == want) { ldRun(I_BR_T); return 1; } ldRun(I_BR_NT + I_IMPL + I_BR_T); ld.polls++; }
  return 0;
}

// Spin the APU until out-port `k` echoes `want`. Returns 0 on timeout.
static int waitEcho(CtAudio* a, int k, uint8_t want) {
  for (long i = 0; i < SPIN_LIMIT; i++) {
    if (R(a, k) == want) return 1;
    step(a);
  }
  return 0;
}

// ---- the upload ($C7:0038, audio_boot.md §4) --------------------------------
// The six blocks: the container's (CT_ASSET_SND_BOOT: [dest u16] + bytes), or both tables read out of the ROM. (§2: the
// old apu_upload.md read $001C as a single "initial target"; it is a 6-entry table parallel to $0010, and every block
// pushes its own destination to $2142/$2143 at $C7:00C6/$00CD.)
static int sndBootLoad(CtAudio* a, const uint8_t* rom, long romLen) {
  for (int b = 0; b < C7_NBLOCKS; b++) {
    if (a->snd) {
      uint32_t n = 0; const uint8_t* e = CtAssetGet(a->snd, CT_ASSET_SND_BOOT, (uint32_t)b, &n);
      if (!e || n < 2 || n - 2 > 0xFFFF) return 0;
      a->dst[b] = (uint16_t)(e[0] | (e[1] << 8)); a->size[b] = (uint16_t)(n - 2); a->blk[b] = e + 2; a->src[b] = 0;
      continue;
    }
    long s = C7_BASE + C7_SRC_TABLE + b * 2, d = C7_BASE + C7_DST_TABLE + b * 2;
    if (!rom || d + 1 >= romLen) return 0;
    a->src[b] = (uint16_t)(rom[s] | (rom[s + 1] << 8));
    a->dst[b] = (uint16_t)(rom[d] | (rom[d + 1] << 8));
    long p = C7_BASE + a->src[b];
    if (p + 1 >= romLen) return 0;
    a->size[b] = (uint16_t)(rom[p] | (rom[p + 1] << 8));
    if (p + 2 + a->size[b] > romLen) return 0;
    a->blk[b] = rom + p + 2;
  }
  return 1;
}

static int uploadDriver(CtAudio* a, const uint8_t* rom, long romLen) {
  if (!sndBootLoad(a, rom, romLen)) return 0;

  // $C7:004D — wait for the IPL's $BBAA ready signature. The CPU parks $FF/$F0
  // in the low ports while it waits; the SPC's boot ROM answers AA/BB.
  for (long i = 0; ; i++) {
    if (R(a, 0) == 0xAA && R(a, 1) == 0xBB) break;
    if (i >= SPIN_LIMIT) return 0;
    W(a, 1, 0xF0); W(a, 0, 0xFF);
    step(a);
  }

  // $C7:0062 — first destination to ports 2/3, then the $CC kick, then echo.
  W(a, 2, (uint8_t)(a->dst[0] & 0xFF));
  W(a, 3, (uint8_t)(a->dst[0] >> 8));
  W(a, 1, 0xCC); W(a, 0, 0xCC);
  if (!waitEcho(a, 0, 0xCC)) return 0;

  uint8_t idx = 0;
  for (int b = 0; b < C7_NBLOCKS; b++) {
    const uint8_t* p = a->blk[b];                      // (after the size word)
    idx = 0;                                           // $C7:007D — per BLOCK, not per upload
    for (uint16_t y = 0; y < a->size[b]; y++) {
      W(a, 1, p[y]);      // data byte FIRST ($2141)...
      W(a, 0, idx);       // ...then the running index ($2140) is what arms the SPC
      if (!waitEcho(a, 0, idx)) return 0;
      idx++;
    }
    // $C7:00B8-00BD — the block-boundary token. The IPL treats port0-Y in 1..127
    // as "block done" ($FFDA/$FFE9/$FFEB), and index 0 is reserved to mean "new
    // transfer starts here" ($FFD6), so the token may never be 0.
    idx = (uint8_t)(idx + 3);
    if (idx == 0) idx = 1;
    if (b == C7_NBLOCKS - 1) break;
    // $C7:00C6 — this block's successor destination, then the token in BOTH
    // ports. port1 != 0 is what tells the IPL another transfer follows (§4a r1).
    W(a, 2, (uint8_t)(a->dst[b + 1] & 0xFF));
    W(a, 3, (uint8_t)(a->dst[b + 1] >> 8));
    W(a, 1, idx); W(a, 0, idx);
    if (!waitEcho(a, 0, idx)) return 0;
  }

  // $C7:00E2 — execute. port1 == 0 makes the IPL JMP to the address in ports
  // 2/3, which is block 0's destination: the driver entry.
  W(a, 3, (uint8_t)(a->dst[0] >> 8));
  W(a, 2, (uint8_t)(a->dst[0] & 0xFF));
  W(a, 1, 0x00);
  W(a, 0, idx);
  if (!waitEcho(a, 0, idx)) return 0;
  W(a, 0, 0x00);

  // $C7:0118 — the CPU spins a delay loop here before touching the driver. Give
  // the SPC the same head start. (§8 gap 4: the exact master-cycle count of that
  // loop is derived-by-timing, not measured; it affects only WHEN the driver is
  // ready, never the ARAM image, which is why it is safe to approximate here and
  // why the ARAM hash is the real acceptance test.)
  apuRun(a, 2938);
  return 1;
}

// ============================================================================================
// THE SONG DIRECTORY -- bank $C7's eight per-song / per-instrument tables.
//
// Canonical source: disasm/notes/audio_song_loader.md + audio_song_data.md, both re-derived by
// audio_song_loader_verify.md (which matched ROM bytes to the oracle's ARAM for song ids $06/$1E/$08
// with ZERO mismatched bytes, and proved the eight tables TILE EXACTLY with no gap or overlap).
//
// NOTHING here is a typed literal that the ROM itself derives. In particular:
//   * the song COUNT is rom[$C7:0AE9], not 83;
//   * the INSTRUMENT count is (SAMPLEPTR_END - SAMPLEPTR)/3, i.e. the table's own extent, not 62;
//   * the BRR heap base is rom16($C7:40B4) + $4700 -- the size word of IPL block 2 plus block 2's
//     ARAM destination -- not the literal $4901. That is the same quantity CtAudioSeedMailboxPage
//     already computes as dst[2] + size[2] for the $1E60/$1EF6/$1EF8 seed, from the OTHER direction:
//     the two agreeing is a real cross-check, not a coincidence.
//   * every block's LENGTH comes from the block's own [u16 len] header, never from a table.
//
// $1E03 is the DSP ECHO DELAY (EDL), NOT an "SFX bank index" -- the loader dossier's label was wrong
// and the verify pass measured $F100 - ESA*$100 == EDL*2048. The echo buffer is therefore the top
// EDL*2048 bytes of ARAM, and $1EF2 = $F1 - EDL*8 is its base page.
#define C7_SEQPTR    0x70D18u   // + id*3          -> 24-bit ptr to the sequence block
#define C7_SAMPLEPTR 0x70AEAu   // + (instr-1)*3   -> 24-bit ptr to a BRR sample block
#define C7_LOOPOFF   0x70BA4u   // (also the end fence of SAMPLEPTR)
#define C7_PARAM1F40 0x70C20u
#define C7_PARAM1FC0 0x70C9Cu
#define C7_INSTSET   0x70E11u   // + id*32         -> the song's instrument list
#define C7_PERC      0x71871u   // + id*36         -> the per-song block pushed to ARAM $F120
#define C7_REC       0x7241Du   // + id*2          -> {byte0 = EDL nibble, byte1 = the $004D scalar}
#define C7_SONGCOUNT 0x70AE9u   // the count itself lives in the ROM
#define C7_SFXSET_A  0x74309u   // driver mode 0 (field)  -- IPL block 1
#define C7_SFXSET_B  0x75B0Bu   // driver mode 1 (battle) -- the block the IPL never uploads
#define C7_IPL_BLK2  0x740B4u   // block 2's [u16 len]; its ARAM destination is $4700

// ---- the sound's data: the asset container's (CT_ASSET_SND_*, assets/extract_assets.py pack_sound) when CtAudioInitWith
// was given one that holds them, else the ROM's. Every read the loader makes of bank $C7 goes through these.
static const uint8_t* sndGet(const CtAudio* a, uint32_t type, uint32_t id, uint32_t* n) {
  return a && a->snd ? CtAssetGet(a->snd, type, id, n) : NULL;
}
int CtAudioFromAssets(const CtAudio* a) { return a && a->snd; }
static unsigned sndInstrWord(const CtAudio* a, unsigned id, int which);   // (below)
static const uint8_t* sndPerc(const CtAudio* a, int songId);
static const uint8_t* sndSfxData(const CtAudio* a, int setB, unsigned* len);
static int sndException(const CtAudio* a, int songId);
static int sndCmpBytes(const char* what, int id, const uint8_t* x, int nx, const uint8_t* y, int ny) {
  if (nx == ny && (!nx || (x && y && !memcmp(x, y, (size_t)nx)))) return 1;
  int k = 0;
  while (k < nx && k < ny && x && y && x[k] == y[k]) k++;
  fprintf(stderr, "snd-data: %s %d differs: rom %d bytes, container %d, first at %d\n", what, id, nx, ny, k);
  return 0;
}
long CtAudioDataCompare(const uint8_t* rom, long romLen, const CtAssets* snd) {
  static CtAudio r, c;                                       // two data sources, no SPC: only the accessors run
  memset(&r, 0, sizeof r); memset(&c, 0, sizeof c);
  r.rom = rom; r.romLen = romLen;
  c.rom = NULL; c.romLen = 0; c.snd = snd;
  if (!snd || !CtAssetGet(snd, CT_ASSET_SND_BOOT, 0, NULL)) { fprintf(stderr, "snd-data: the container holds no sound\n"); return -1; }
  while (CtAssetGet(snd, CT_ASSET_SND_SAMPLE, (uint32_t)(c.ninst + 1), NULL)) c.ninst++;
  if (!sndBootLoad(&r, rom, romLen) || !sndBootLoad(&c, NULL, 0)) { fprintf(stderr, "snd-data: the blocks\n"); return -1; }
  long items = 0;
  for (int b = 0; b < C7_NBLOCKS; b++, items++)
    if (r.dst[b] != c.dst[b] || !sndCmpBytes("block", b, r.blk[b], r.size[b], c.blk[b], c.size[b])) return -1;
  if (CtAudioSongCount(&r) != CtAudioSongCount(&c) || CtAudioInstrumentCount(&r) != CtAudioInstrumentCount(&c)
      || CtAudioHeapBase(&r) != CtAudioHeapBase(&c)) { fprintf(stderr, "snd-data: counts / heap base\n"); return -1; }
  items += 3;
  for (int s = 0; s < CtAudioSongCount(&r); s++) {
    int nr = 0, nc = 0;
    const uint8_t* x = CtAudioSeqBlock(&r, s, &nr); const uint8_t* y = CtAudioSeqBlock(&c, s, &nc);
    if (!x || !y || !sndCmpBytes("sequence (+3 past it)", s, x, nr + 3, y, nc + 3)) return -1;
    if ((((x - rom) - 2) & 0xFFFF) != (long)(y[-2] | (y[-1] << 8))) { fprintf(stderr, "snd-data: sequence %d's address\n", s); return -1; }
    if (!sndCmpBytes("instrument list", s, CtAudioInstrSet(&r, s), 32, CtAudioInstrSet(&c, s), 32)) return -1;
    if (!sndCmpBytes("percussion", s, sndPerc(&r, s), 36, sndPerc(&c, s), 36)) return -1;
    int e1 = 0, s1 = 0, e2 = 0, s2 = 0;
    CtAudioSongRecord(&r, s, &e1, &s1); CtAudioSongRecord(&c, s, &e2, &s2);
    if (e1 != e2 || s1 != s2) { fprintf(stderr, "snd-data: record %d\n", s); return -1; }
    items += 5;
  }
  for (int i = 1; i <= CtAudioInstrumentCount(&r); i++, items++) {
    int nr = 0, nc = 0;
    const uint8_t* x = CtAudioSampleBlock(&r, i, &nr); const uint8_t* y = CtAudioSampleBlock(&c, i, &nc);
    if (!x || !y || !sndCmpBytes("sample", i, x, nr, y, nc)) return -1;
    if ((((x - rom) - 2) & 0xFFFF) != (long)(y[-2] | (y[-1] << 8))) { fprintf(stderr, "snd-data: sample %d's address\n", i); return -1; }
  }
  for (int i = 0; i <= CtAudioInstrumentCount(&r); i++)
    for (int w = 0; w < 3; w++, items++)
      if (sndInstrWord(&r, (unsigned)i, w) != sndInstrWord(&c, (unsigned)i, w)) { fprintf(stderr, "snd-data: instrument %d word %d\n", i, w); return -1; }
  for (int sb = 0; sb < 2; sb++, items++) {
    unsigned nr = 0, nc = 0;
    const uint8_t* x = sndSfxData(&r, sb, &nr); const uint8_t* y = sndSfxData(&c, sb, &nc);
    if (!sndCmpBytes("effect set", sb, x, (int)nr, y, (int)nc)) return -1;
  }
  for (int s = 0; s < 256; s++, items++)
    if (sndException(&r, s) != sndException(&c, s)) { fprintf(stderr, "snd-data: exception list, song %d\n", s); return -1; }
  for (int i = 0; i < 8; i++, items += 3)
    if (ldFdDmap(&r, i) != ldFdDmap(&c, i) || ldFdTable(&r, 0, i) != ldFdTable(&c, 0, i)
        || ldFdTable(&r, 1, i) != ldFdTable(&c, 1, i)) { fprintf(stderr, "snd-data: FD/C1EE channel %d\n", i); return -1; }
  return items;
}
int CtAudioSongCount(const CtAudio* a) {
  if (a && a->snd) { uint32_t n = 0; const uint8_t* m = sndGet(a, CT_ASSET_SND_MISC, 0, &n); return m && n ? m[0] : 0; }
  if (!a || !a->rom || (long)C7_SONGCOUNT >= a->romLen) return 0;
  return a->rom[C7_SONGCOUNT];
}
int CtAudioInstrumentCount(const CtAudio* a) {
  if (a && a->snd) return a->ninst;
  if (!a || !a->rom) return 0;
  return (int)((C7_LOOPOFF - C7_SAMPLEPTR) / 3);        // the table's own extent, not a constant
}
unsigned CtAudioHeapBase(const CtAudio* a) {
  if (a && a->snd) return (unsigned)a->size[2] + 0x4700u;   // block 2's length past its destination
  if (!a || !a->rom || (long)(C7_IPL_BLK2 + 2) > a->romLen) return 0;
  return (unsigned)(a->rom[C7_IPL_BLK2] | (a->rom[C7_IPL_BLK2 + 1] << 8)) + 0x4700u;
}
// One of an instrument's three words ($C7:0BA4 loop, $C7:0C20 / $C7:0C9C the $1F40 / $1FC0 params), at the loader's
// own index `(id - 1) * 2`: id 0 reads index -1, the long,X wrap into bank $C8.
static unsigned sndInstrWord(const CtAudio* a, unsigned id, int which) {
  if (a->snd) {
    uint32_t n = 0; const uint8_t* e = sndGet(a, CT_ASSET_SND_INSTR, id & 0xFFFF, &n);
    return e && n >= 6 ? (unsigned)(e[2 * which] | (e[2 * which + 1] << 8)) : 0;
  }
  static const unsigned tbl[3] = { C7_LOOPOFF, C7_PARAM1F40, C7_PARAM1FC0 };
  long tp = (long)tbl[which] + (long)(((id - 1) * 2) & 0xFFFF);
  return (tp + 2 <= a->romLen) ? (unsigned)(a->rom[tp] | (a->rom[tp + 1] << 8)) : 0;
}
// The song's percussion block (36 bytes, ARAM $F120).
static const uint8_t* sndPerc(const CtAudio* a, int songId) {
  if (a->snd) { uint32_t n = 0; const uint8_t* e = sndGet(a, CT_ASSET_SND_SONG, (uint32_t)songId, &n); return e && n >= 70 ? e + 32 : NULL; }
  long p = (long)C7_PERC + (long)songId * 36;
  return (p + 36 <= a->romLen) ? a->rom + p : NULL;
}
// An effect set ([u16 len][bytes]): set B is the battle's.
static const uint8_t* sndSfxData(const CtAudio* a, int setB, unsigned* len) {
  if (a->snd) { uint32_t n = 0; const uint8_t* e = sndGet(a, CT_ASSET_SND_SFXSET, setB ? 1u : 0u, &n); *len = n; return e; }
  long src = setB ? (long)C7_SFXSET_B : (long)C7_SFXSET_A;
  *len = (unsigned)(a->rom[src] | (a->rom[src + 1] << 8));
  return a->rom + src + 2;
}
// $C7:0734's song-exception list: 1 when the song is on it.
static int sndException(const CtAudio* a, int songId) {
  if (a->snd) {
    uint32_t n = 0; const uint8_t* m = sndGet(a, CT_ASSET_SND_MISC, 0, &n);
    for (uint32_t k = 1; m && k < n; k++) { if (m[k] & 0x80) break; if (m[k] == (uint8_t)songId) return 1; }
    return 0;
  }
  for (long p = 0x70AD8; p < a->romLen; p++) {
    if (a->rom[p] & 0x80) break;                                // bit7 = end of list
    if (a->rom[p] == (uint8_t)songId) return 1;
  }
  return 0;
}
static long c7ptr24(const CtAudio* a, unsigned tbl, int idx) {   // a 24-bit bank:addr -> flat offset
  long p = (long)tbl + (long)idx * 3;
  if (!a || !a->rom || p + 3 > a->romLen) return -1;
  unsigned bank = a->rom[p + 2], adr = a->rom[p] | (a->rom[p + 1] << 8);
  if (bank < 0xC0) return -1;
  return (long)((bank - 0xC0) << 16) + adr;
}
// A [u16 len][payload] block: returns the payload pointer and writes its length. NULL on any bound
// failure -- callers must not fall back to a table length, because the ROM never does.
const uint8_t* CtAudioSeqBlock(const CtAudio* a, int songId, int* lenOut) {
  if (a && a->snd) {
    uint32_t n = 0; const uint8_t* e = sndGet(a, CT_ASSET_SND_SEQ, (uint32_t)songId, &n);
    if (!e || n < 5) return NULL;
    if (lenOut) *lenOut = (int)n - 5;                  // [addr u16] + the block + the 3 bytes read past it
    return e + 2;
  }
  long p = c7ptr24(a, C7_SEQPTR, songId);
  if (p < 0 || p + 2 > a->romLen) return NULL;
  int n = a->rom[p] | (a->rom[p + 1] << 8);
  if (p + 2 + n > a->romLen) return NULL;
  if (lenOut) *lenOut = n;
  return a->rom + p + 2;
}
const uint8_t* CtAudioSampleBlock(const CtAudio* a, int instr, int* lenOut) {
  if (instr < 1 || instr > CtAudioInstrumentCount(a)) return NULL;   // the table is 1-based
  if (a->snd) {
    uint32_t n = 0; const uint8_t* e = sndGet(a, CT_ASSET_SND_SAMPLE, (uint32_t)instr, &n);
    if (!e || n < 2) return NULL;
    if (lenOut) *lenOut = (int)n - 2;
    return e + 2;
  }
  long p = c7ptr24(a, C7_SAMPLEPTR, instr - 1);
  if (p < 0 || p + 2 > a->romLen) return NULL;
  int n = a->rom[p] | (a->rom[p + 1] << 8);
  if (p + 2 + n > a->romLen) return NULL;
  if (lenOut) *lenOut = n;
  return a->rom + p + 2;
}
const uint8_t* CtAudioInstrSet(const CtAudio* a, int songId) {
  if (a && a->snd) { uint32_t n = 0; const uint8_t* e = sndGet(a, CT_ASSET_SND_SONG, (uint32_t)songId, &n); return e && n >= 70 ? e : NULL; }
  long p = (long)C7_INSTSET + (long)songId * 32;
  return (a && a->rom && p + 32 <= a->romLen) ? a->rom + p : NULL;
}
// The per-song record: EDL = byte0's low nibble (the echo delay), and byte1 the opaque $004D scalar.
int CtAudioSongRecord(const CtAudio* a, int songId, int* edl, int* scalar) {
  if (a && a->snd) {
    uint32_t n = 0; const uint8_t* e = sndGet(a, CT_ASSET_SND_SONG, (uint32_t)songId, &n);
    if (!e || n < 70) return 0;
    if (edl) *edl = e[68] & 0x0F;
    if (scalar) *scalar = e[69];
    return 1;
  }
  long p = (long)C7_REC + (long)songId * 2;
  if (!a || !a->rom || p + 2 > a->romLen) return 0;
  if (edl)    *edl    = a->rom[p] & 0x0F;
  if (scalar) *scalar = a->rom[p + 1];
  return 1;
}
// The echo buffer's base page: ESA = $F1 - EDL*8, so the buffer is [ESA*$100, $F100).
int CtAudioEchoBase(const CtAudio* a, int songId) {
  int edl = 0;
  if (!CtAudioSongRecord(a, songId, &edl, NULL)) return -1;
  return (0xF1 - edl * 8) & 0xFF;
}


// ============================================================================================
// THE SONG LOADER -- $C7:01A1-$0619, ported per audio_song_loader_verify.md's ordered plan.
//
// Structure notes that decide the shape of this code, all independently verified:
//  * It is ONE LONG BLOCKING BUSY-WAIT. No frame-wait, no WAI, no $42xx anywhere in
//    $C7:01A1-$0726 -- so a load runs to completion in a single call, advancing the APU inside
//    every spin. It does NOT need to be a multi-frame state machine.
//  * The per-unit wire protocol (disassembled at $C7:06CD): 3 data bytes to $2141/$2142/$2143,
//    then ONE rolling-token handshake on $2140. The block preamble ($C7:0680) is dest-lo ->
//    $2142, dest-hi -> $2143, command -> $2141, handshake, then verify $2141 echoed the command.
//  * $1E03 is the DSP ECHO DELAY, not an "SFX bank index" (ESA = $F1 - EDL*8).
//  * The token seed is per song-COMMAND, from the command byte ($C7:0A7B). Omitting it -- which
//    one dossier's port spec did -- hangs the whole thing in an infinite handshake spin.
// The $7E:1E00 page IS the state, exactly as in the ROM: $1E20[16] resident ids, $1E40[16]
// lengths, $1E60[17] ARAM bases (each slot's end is the next slot's base), $1F00/$1F20 the
// song's set and the to-load set, $1F80 the DIR staging.
#define M8(m,a)     ((m)[(a)])
#define M16(m,a)    ((unsigned)((m)[(a)] | ((m)[(a)+1] << 8)))
#define SM8(m,a,v)  ((m)[(a)] = (uint8_t)(v))
#define SM16(m,a,v) do { (m)[(a)] = (uint8_t)(v); (m)[(a)+1] = (uint8_t)((unsigned)(v) >> 8); } while (0)

// $C7:09DA -- write the token, spin until the SPC echoes it, advance (t+1)&$7F.
// The post-echo settle: the CPU's inter-unit time, handed to the SPC before we write the next unit.
// See the SETTLE_CYCLES commentary above. CT_AUDIO_SETTLE overrides it for sweeps.
static int sndSettle(void) {
  static int n = -1;
  if (n < 0) { const char* e = getenv("CT_AUDIO_SETTLE"); n = e ? atoi(e) : 32; }
  return n;
}
static uint8_t sndTok(CtAudio* a, uint8_t t) {
  if (!ld.active) {                                   // unclocked caller: the poll-quantum + settle model
    W(a, 0, t);
    for (long i = 0; i < SPIN_LIMIT; i++) { if (R(a, 0) == t) break; step(a); }
    int st = sndSettle();
    if (st > 0) { apuRun(a, st); g_sndSpcCycles += st; g_sndSettleCycles += st; }
    return (uint8_t)((t + 1) & 0x7F);
  }
  // C7/09DA, cycle for cycle (the caller has paid up to and including its JSR):
  ldRun(I_PHP + I_SEP);                               // 09DA PHP / 09DB SEP #20
  ldRun(I_STA_BB); Wc(a, 0, t);                       // 09DD STA $2140 -- the write lands at the end of its B-bus cycle
  long guard = 0x200000; long p0 = ld.polls;
  for (;;) {
    ldRun(I_CMP_BB); if (Rc(a, 0) == t) break;        // 09E0 CMP $2140 -- the read at the end of its B-bus cycle
    ldRun(I_BR_T); ld.polls++;                        // 09E3 BNE taken
    if (--guard == 0) break;                          // a dead driver: leave, do not spin the raster forever
  }
  { long k = ld.polls - p0; ldHist[k > 64 ? 64 : k]++; }
  ldRun(I_BR_NT + I_IMPL + I_IMM8 + I_PLP + I_RTS);   // 09E3 not taken / 09E5 INC A / 09E6 AND #7F / 09E8 PLP / 09E9 RTS
  return (uint8_t)((t + 1) & 0x7F);
}
// $C7:0680-$0694 + the $C7:09EA resync: open a transfer to `dst` with `cmd`, verifying the echo.
// The ROM's transfer unit is ONE $C7:09DA token handshake: a 3-byte streaming unit (C7/06CD-06F3)
// or a command block's acknowledgement. Counted so a load can be priced in the unit the CPU actually
// spends its time in -- the oracle shows the CPU parked inside `JSL $C7:0004` for the whole upload
// (the field engine freezes; see disasm/notes/anim_family_delegation.md section 4), and the freeze
// length is this count times the per-unit round trip.
static long g_sndUnits, g_sndBytes, g_sndBlocks;
long CtAudioLastLoadUnits(long* bytes, long* blocks, long* spcCycles, long* settleCycles, long* movedBytes) {
  if (bytes) *bytes = g_sndBytes; if (blocks) *blocks = g_sndBlocks; if (spcCycles) *spcCycles = g_sndSpcCycles;
  if (settleCycles) *settleCycles = g_sndSettleCycles; if (movedBytes) *movedBytes = g_sndMovedBytes;
  return g_sndUnits;
}
static int sndLoadTrace(void) { static int n = -1; if (n < 0) { const char* e = getenv("CT_AUDIO_LOADTRACE"); n = (e && *e) ? 1 : 0; } return n; }
// CT_AUDIO_LOADTRACE: a mark at a block's head -- master cycles since C7 began, the CPU's share, the taken polls and the
// handshakes so far (the cartridge's side: its --cputrace at the same head PC).
static void sndMark(const char* what, unsigned dst) {
  if (sndLoadTrace() && ld.active)
    fprintf(stderr, "[loadmark] %s $%04X clk=%lld cpu=%lld polls=%ld units=%ld\n", what, dst, ld.clk - ld.clk0, ld.dbgCpuAll - ld.cpu0, ld.polls, g_sndUnits);
}
// C7/09EA -- the resync a block head takes when the driver did not echo its command: the CURRENT token again (`LDA $84 /
// STA $2140`, not the next one), its echo awaited, and the token restarted at $E0 -- not C7/09DA's handshake.
static void sndResync(CtAudio* a, uint8_t* m) {
  uint8_t t = M8(m, 0x1E84);
  if (!ld.active) { sndTok(a, t); SM8(m, 0x1E84, 0xE0); return; }   // (the unclocked callers' poll-quantum model)
  ldRun(I_PHP + I_SEP + I_LDA_DP + I_STA_BB); Wc(a, 0, t);          // 09EA PHP / SEP #20 / LDA $84 / STA $2140
  for (long guard = 0x200000; guard > 0; guard--) {                 // 09F2 CMP $2140 / 09F5 BNE
    ldRun(I_CMP_BB); if (Rc(a, 0) == t) break;
    ldRun(I_BR_T); ld.polls++;
  }
  ldRun(I_BR_NT + I_IMM8 + I_LDA_DP + I_PLP + I_RTS);               // 09F7 LDA #E0 / STA $84 / PLP / RTS
  SM8(m, 0x1E84, 0xE0);
}
// Every block head's ack: `LDA $84 / JSR C7/09DA / STA $84 / LDA #cmd / CMP $2141 / BEQ`. 1 = the driver echoed the
// command; 0 = BEQ fell through into `JSR C7/09EA` and the head's `BRA` back to its own start.
static int sndHeadAck(CtAudio* a, uint8_t* m, uint8_t cmd) {
  ldRun(I_LDA_DP + I_JSR);
  SM8(m, 0x1E84, sndTok(a, M8(m, 0x1E84))); g_sndUnits++;
  ldRun(I_LDA_DP + I_IMM8 + I_CMP_BB);
  if (Rc(a, 1) == cmd) { ldRun(I_BR_T); return 1; }
  ldRun(I_BR_NT + I_JSR); sndResync(a, m); ldRun(I_BR_T);
  return 0;
}
// The heads the loader writes inline (C7/03C8 $1E80, 040D $1F40, 045B $1FC0, 04DB $2000, 059A $F120): `pre` cycles of the
// head's own work from its BRA target, then `LDA #hi / STA $2143 / LDA #lo / STA $2142 / LDA #cmd / STA $2141` and the
// ack -- none of the sample head's C7/0656 bookkeeping.
static void sndHeadLit(CtAudio* a, uint8_t* m, unsigned dst, uint8_t cmd, long pre) {
  g_sndBlocks++;
  if (sndLoadTrace()) fprintf(stderr, "[loadtrace] block cmd=%02X dst=$%04X (units so far %ld) clock %.2f f\n", cmd, dst, g_sndUnits, (double)ld.clk / 357368.0);
  sndMark("open", dst);
  for (int attempt = 0; attempt < 8; attempt++) {
    ldRun(pre + I_IMM8 + I_STA_BB); Wc(a, 3, (uint8_t)(dst >> 8));
    ldRun(I_IMM8 + I_STA_BB);       Wc(a, 2, (uint8_t)(dst & 0xFF));
    ldRun(I_IMM8 + I_STA_BB);       Wc(a, 1, cmd);
    if (sndHeadAck(a, m, cmd)) return;
  }
}
// C7/0655-069B -- a SAMPLE's head (the append's `JSR C7/0655`): PHP, the instrument's pointer from $C7:0AEA and the slot's
// destination from $1E60, and the ack; a miss goes back to 0656 (after the PHP).
static void sndCmdBlk(CtAudio* a, uint8_t* m, unsigned dst, uint8_t cmd) {
  g_sndBlocks++;
  if (sndLoadTrace()) fprintf(stderr, "[loadtrace] block cmd=%02X dst=$%04X (units so far %ld) clock %.2f f\n", cmd, dst, g_sndUnits, (double)ld.clk / 357368.0);
  sndMark("open", dst);
  ldRun(I_PHP);                                                     // 0655 PHP
  for (int attempt = 0; attempt < 8; attempt++) {
    // 0656-067E: REP / LDX $1C / LDA $20,X / DEC A / STA $82 / ASL / CLC / ADC $82 / TAX / SEP, the pointer (LDA $C70AEA,X /
    // STA $12, three bytes), LDY $12 / STZ $12 / STZ $13 / LDX $1C -- 448 on the cartridge's own count (--cputrace)
    ldRun(I_SEP + I_LDA_DP16 + I_LDA_DPX16 + I_IMPL + I_LDA_DP16 + 2 * I_IMPL + I_LDA_DP16 + I_IMPL + I_SEP
          + 3 * (I_LONGX_ROM8 + I_LDA_DP) + I_LDA_DP16 + 2 * I_LDA_DP + I_LDA_DP16);
    ldRun(I_LDA_DPX + I_STA_BB); Wc(a, 2, (uint8_t)(dst & 0xFF));   // 0680 LDA $60,X / 0682 STA $2142
    ldRun(I_LDA_DPX + I_STA_BB); Wc(a, 3, (uint8_t)(dst >> 8));     // 0685 LDA $61,X / 0687 STA $2143
    ldRun(I_IMM8 + I_STA_BB); Wc(a, 1, cmd);                        // 068A LDA #03  / 068C STA $2141
    if (sndHeadAck(a, m, cmd)) return;                              // 068F-069B; 069D JSR C7/09EA / 06A0 BRA ->0656
  }
}
// The streams: units of three bytes to $2141-$2143, one C7/09DA handshake each. THREE loops in the ROM, three counts:
//   SND_SAMPLE  C7/06A2-071C (an instrument, under the sample head): the size word, then `LDX $0C` and `DEX DEX DEX / BNE`
//               -- an UNSIGNED count to zero: exactly size/3 units (every sample is BRR, 9-byte blocks). Ends `LDA #E0 /
//               STA $84 / PLP / RTS` (the head's PHP).
//   SND_SEQ     C7/0527-0599 (the song's sequence): the size word, then `DEX DEX DEX / BPL` -- SIGNED: size/3 + 1 units,
//               the last one reading past the block. Ends `LDA #E0 / STA $84`, inline.
//   (C7/05E0's $F120 block: `LDX #000C / ... / DEX / BNE`, twelve units -- sndPercBlock.)
// Each picks, once, at its `TYA / CLC / ADC $0C / BCC` (06C1-06CB, 0542-054A), the FAST loop (06F9 / 0578: the block stays
// in its 64 KB bank) or the one that follows every INY with `BNE / INC $14` (06CD / 054C: a taken branch per byte, and
// the bank bump where Y wraps). The size word's own two INYs have the same `BNE / INC $14`.
enum { SND_SAMPLE = 0, SND_SEQ = 1 };
static void sndStream(CtAudio* a, uint8_t* m, const uint8_t* src, int n, int kind) {
  long y = a->snd ? (long)(src[-2] | (src[-1] << 8))               // Y at the size word (the container keeps its address)
                  : ((long)(src - a->rom) - 2) & 0xFFFF;
  long head = 0;                                                    // the size word: LDA [$12],Y / STA / (STA / ADC / STA) / INY / BNE
  for (int k = 0; k < 2; k++) { y = (y + 1) & 0xFFFF; head += I_LDA_DPIY_ROM + I_LDA_DP + I_IMPL + (y ? I_BR_T : I_BR_NT + I_INC_DP); }
  int slow = (y + n) > 0xFFFF;                                      // ADC $0C carries: the block crosses its bank
  if (kind == SND_SAMPLE)                                           // 06A6 STA $40,X / CLC / ADC $60,X / STA $62,X, 06B6-06BA the high bytes;
    head += 2 * I_LDA_DPX + I_IMPL + 2 * I_LDA_DPX + 2 * I_LDA_DPX  //   06C1 LDX $0C / REP / TYA / CLC / ADC $0C / SEP / BCC
            + I_LDA_DP16 + I_SEP + 2 * I_IMPL + I_LDA_DP16 + I_SEP + (slow ? I_BR_NT : I_BR_T);
  else                                                              // 0539 LDX $0C / CPX #0F00 / BCC (INC $F3: a long sequence), 0542-054A
    head += I_LDA_DP16 + I_IMM16 + (n >= 0x0F00 ? I_BR_NT + I_INC_DP : I_BR_T)
            + I_SEP + 2 * I_IMPL + I_LDA_DP16 + I_SEP + (slow ? I_BR_NT : I_BR_T);
  if (kind == SND_SEQ && n >= 0x0F00) SM8(m, 0x1EF3, M8(m, 0x1EF3) + 1);   // 0540 INC $F3
  ldRun(head);
  int units = 0;
  if (kind == SND_SAMPLE) { uint16_t x = (uint16_t)n; do { units++; x = (uint16_t)(x - 3); } while (x != 0); }
  else { int16_t x = (int16_t)n; do { units++; x = (int16_t)(x - 3); } while (x >= 0); }
  if (sndLoadTrace()) fprintf(stderr, "[loadtrace] stream %d bytes -> %d units (%s, %s loop)\n", n, units,
                              kind == SND_SAMPLE ? "sample 06CD/06F9 BNE" : "sequence 054C/0578 BPL", slow ? "bank-crossing" : "fast");
  for (int u = 0; u < units; u++) {
    for (int k = 0; k < 3; k++) {                                   // LDA [$12],Y / STA $214x / INY (+ BNE, INC $14 at a wrap)
      ldRun(I_LDA_DPIY_ROM + I_STA_BB); Wc(a, 1 + k, src[k]);
      y = (y + 1) & 0xFFFF;
      ldRun(I_IMPL + (!slow ? 0 : y ? I_BR_T : I_BR_NT + I_INC_DP)); }
    ldRun(I_LDA_DP + I_JSR);                                        // LDA $84 / JSR C7/09DA
    SM8(m, 0x1E84, sndTok(a, M8(m, 0x1E84)));
    ldRun(I_LDA_DP + 3 * I_IMPL + (u + 1 < units ? I_BR_T : I_BR_NT));   // STA $84 / DEX DEX DEX / BNE|BPL
    src += 3; g_sndUnits++; g_sndBytes += 3;
  }
  ldRun((slow ? I_BR_T : 0) + I_IMM8 + I_LDA_DP + (kind == SND_SAMPLE ? I_PLP + I_RTS : 0));   // (06F7|0576 BRA) LDA #E0 / STA $84 (/ PLP / RTS)
  SM8(m, 0x1E84, 0xE0);
}
// C7/059A-05FF -- the song's 36-byte block -> ARAM $F120: Y = song x 36 (REP / LDA $05 / AND / STA $82 / ASL / CLC / ADC /
// ASL / ASL / STA / ASL / CLC / ADC / TAY / SEP) and the pointer $C7:1871 (three LDA # / STA dp), all inside the head's
// retry; then `LDX #000C` and twelve units with `DEX / BNE`, and 05FF SEP #20.
static void sndPercBlock(CtAudio* a, uint8_t* m, const uint8_t* src) {
  sndHeadLit(a, m, 0xF120, 0x03, I_SEP + I_LDA_DP16 + I_IMM16 + I_LDA_DP16 + 2 * I_IMPL + I_LDA_DP16 + 2 * I_IMPL + I_LDA_DP16
                                 + 2 * I_IMPL + I_LDA_DP16 + I_IMPL + I_SEP + 3 * (I_IMM8 + I_LDA_DP));
  if (sndLoadTrace()) fprintf(stderr, "[loadtrace] stream 36 bytes -> 12 units ($F120 05E3 DEX/BNE)\n");
  ldRun(I_IMM16);                                                   // 05E0 LDX #000C
  for (int u = 0; u < 12; u++) {
    for (int k = 0; k < 3; k++) { ldRun(I_LDA_DPIY_ROM + I_STA_BB); Wc(a, 1 + k, src[k]); ldRun(I_IMPL); }
    ldRun(I_LDA_DP + I_JSR);
    SM8(m, 0x1E84, sndTok(a, M8(m, 0x1E84)));
    ldRun(I_LDA_DP + I_IMPL + (u + 1 < 12 ? I_BR_T : I_BR_NT));    // STA $84 / DEX / BNE
    src += 3; g_sndUnits++; g_sndBytes += 3;
  }
  ldRun(I_SEP);                                                     // 05FF SEP #20
}
// `CMP $2140 / B?? ->itself`: poll out-port `k` until (read == want) is `untilEqual`. The ROM's loops are
// unbounded; this one stops at SPIN_LIMIT. Clocked, each poll is charged; unclocked, the SPC is stepped.
static int sndSpin(CtAudio* a, int k, uint8_t want, int untilEqual) {
  for (long i = 0; i < SPIN_LIMIT; i++) {
    ldRun(I_CMP_BB);
    if ((Rc(a, k) == want) == (untilEqual != 0)) { ldRun(I_BR_NT); return 1; }
    ldRun(I_BR_T); if (ld.active) ld.polls++; else step(a);
  }
  return 0;
}
// $C7:0922 -- the $2F00 SFX-SET upload. ONE routine with TWO callers: the song loader's mode swap
// (C7/04D8 `JSR C7/0922`, when $1E10 changed or $F3 is armed) and command $70 (C7/0904). It is not the
// loader's block-open C7/0680 + stream C7/06CD, which is how the port used to model the swap, and the
// difference is not cosmetic:
//   0922-093A  $2143 = 2F, $2142 = 00, $2141 = (cmd $70 ? 05 : 03)  -- no C7/0656 bookkeeping
//   093D-0950  STA $86 / LDA $84 / JSR C7/09DA / STA $84 / LDA $86 / CMP $2141 / BEQ ->0952,
//              else JSR C7/09EA (re-sync on $84, then $84 = E0) and BRA ->0922
//   0952-0970  LDA $10 / BNE ->0998: set A = $C7:4309 (size) / $C7:430B, set B = $C7:5B0B / $C7:5B0D
//   0973-098F  three `LDA [$12],Y / STA $214x / INY`, `LDA $84 / JSR C7/09DA / STA $84`,
//              `DEX DEX DEX / BNE` -- an UNSIGNED count to zero, so exactly size/3 units (both sets are
//              6144 bytes = 2048 units). C7/06CD's signed loop pushes size/3 + 1.
//   0991-0997  STZ $F3 / LDA #E0 / STA $84 / RTS
static void sndSfxSet(CtAudio* a, uint8_t* m) {
  uint8_t cmd = (M8(m, 0x1E00) == 0x70) ? 0x05 : 0x03;
  g_sndBlocks++;
  sndMark("swap", 0x2F00);
  for (int attempt = 0; attempt < 8; attempt++) {
    ldRun(I_SEP + I_IMM8 + I_STA_BB); Wc(a, 3, 0x2F);                       // 0922 SEP / LDA #2F / STA $2143
    ldRun(I_IMM8 + I_STA_BB); Wc(a, 2, 0x00);                               // 0929 LDA #00 / STA $2142
    ldRun(I_LDA_DP + I_IMM8 + (cmd == 0x05 ? I_BR_NT + I_IMM8 + I_BR_T : I_BR_T + I_IMM8));   // 092E LDA $00 / CMP #70 / BNE / LDA #05 / BRA | LDA #03
    ldRun(I_STA_BB); Wc(a, 1, cmd);                                         // 093A STA $2141
    ldRun(I_LDA_DP + I_LDA_DP + I_JSR);                                     // 093D STA $86 / LDA $84 / JSR C7/09DA
    SM8(m, 0x1E84, sndTok(a, M8(m, 0x1E84))); g_sndUnits++;
    ldRun(I_LDA_DP + I_LDA_DP + I_CMP_BB);                                  // 0944 STA $84 / LDA $86 / CMP $2141
    if (Rc(a, 1) == cmd) { ldRun(I_BR_T); break; }                          // 094B BEQ ->0952
    ldRun(I_BR_NT + I_JSR + I_PHP + I_SEP + I_LDA_DP + I_STA_BB);           // 094D JSR C7/09EA: PHP / SEP / LDA $84 / STA $2140
    Wc(a, 0, M8(m, 0x1E84));
    sndSpin(a, 0, M8(m, 0x1E84), 1);                                        // 09F2 CMP $2140 / BNE
    ldRun(I_IMM8 + I_LDA_DP + I_PLP + I_RTS + I_BR_T);                      // 09F7 LDA #E0 / STA $84 / PLP / RTS, 0950 BRA ->0922
    SM8(m, 0x1E84, 0xE0);
  }
  int setB = M8(m, 0x1E10) != 0;
  unsigned x = 0; const uint8_t* sfx = sndSfxData(a, setB, &x);
  ldRun(I_LDA_DP + (setB ? I_BR_T : I_BR_NT) + 2 * (5 * RA + I_LDA_DP) + 3 * (I_IMM8 + I_LDA_DP) + I_LDA_DP16 + I_IMM16);   // 0952-0970
  const uint8_t* p = sfx;
  do {
    for (int k = 0; k < 3; k++) { ldRun(I_LDA_DPIY_ROM + I_STA_BB); Wc(a, 1 + k, p[k]); ldRun(I_IMPL); }   // LDA [$12],Y / STA $214x / INY
    ldRun(I_LDA_DP + I_JSR);                                                // LDA $84 / JSR C7/09DA
    SM8(m, 0x1E84, sndTok(a, M8(m, 0x1E84)));
    x = (x - 3) & 0xFFFF;
    ldRun(I_LDA_DP + 3 * I_IMPL + (x ? I_BR_T : I_BR_NT));                  // STA $84 / DEX DEX DEX / BNE
    p += 3; g_sndUnits++; g_sndBytes += 3;
  } while (x);
  ldRun(I_LDA_DP + I_IMM8 + I_LDA_DP + I_RTS);                              // 0991 STZ $F3 / LDA #E0 / STA $84 / RTS
  SM8(m, 0x1EF3, 0); SM8(m, 0x1E84, 0xE0);
}
// $C7:08E3 -- command $70: the driver MODE without a song. $1E01 non-zero = battle (set B), zero = field.
//   08E3  SEP / LDA $01 / CLC / ADC #FF / LDA #00 / ROL A   ; mode = ($1E01 != 0)
//   08ED  CMP $11 / BEQ ->091F                             ; already in that mode: nothing
//   08F1  STA $10 / STA $11
//   08F5  LDA #FE / STA $2140 / CMP $2140 / BNE            ; open, and wait for the echo
//   08FF  INC A / AND #7F / STA $84                        ; token 7F
//   0904  JSR C7/0922                                      ; the set
//   0907  LDA #E0 / LDX #0400 / STA $2141 / STA $2140      ; GO
//   0912  CMP $2140 / BEQ ->091A / DEX / BNE               ; up to $400 polls for the echo
//   091A  CMP $2140 / BEQ ->091A                           ; then until the driver moves off it
//   091F  JMP C7/0192
// The field posts it around a battle whose location sets $2A1F bit6 (C0/1BD5 `70 01`, C0/1C7F `70 00`) --
// the attract's demo battle: oracle build/_loc0AD.state f142 `$1E01 = 01, $1E00 = 70` by C0/1BDF and f261
// `$1E01 = 00, $1E00 = 70` by C0/1C89.
static int sndMode70(CtAudio* a, uint8_t* m) {
  int mode = M8(m, 0x1E01) ? 1 : 0;
  ldRun(I_SEP + I_LDA_DP + I_IMPL + 2 * I_IMM8 + I_IMPL + I_LDA_DP);        // 08E3-08ED
  if (mode == (int)M8(m, 0x1E11)) { ldRun(I_BR_T + 3 * RA); return 0; }    // 08EF BEQ ->091F JMP
  ldRun(I_BR_NT + 2 * I_LDA_DP);                                            // 08F1 STA $10 / STA $11
  SM8(m, 0x1E10, (unsigned)mode); SM8(m, 0x1E11, (unsigned)mode);
  ldRun(I_IMM8 + I_STA_BB); Wc(a, 0, 0xFE);                                 // 08F5 LDA #FE / STA $2140
  sndSpin(a, 0, 0xFE, 1);                                                   // 08FA CMP $2140 / BNE
  ldRun(I_IMPL + I_IMM8 + I_LDA_DP + I_JSR);                                // 08FF INC A / AND #7F / STA $84 / JSR
  SM8(m, 0x1E84, 0x7F);
  sndSfxSet(a, m);
  ldRun(I_IMM8 + I_IMM16 + I_STA_BB); Wc(a, 1, 0xE0);                       // 0907 LDA #E0 / LDX #0400 / STA $2141
  ldRun(I_STA_BB); Wc(a, 0, 0xE0);                                          // 090F STA $2140
  if (ld.active) ldPoll400(a, 0xE0);                                        // 0912 CMP $2140 / BEQ / DEX / BNE
  else for (long c = 0; c < 0x400L * 16; c += ctStepCycles()) { if (R(a, 0) == 0xE0) break; step(a); }
  sndSpin(a, 0, 0xE0, 0);                                                   // 091A CMP $2140 / BEQ ->091A
  ldRun(3 * RA);                                                            // 091F JMP C7/0192
  return 1;
}
// $C7:025A -- THE ARAM DEFRAGMENTER, driver command $07.
//
// The instrument-set heap fragments whenever a song evicts a set an earlier song left behind: the
// eviction zeroes $1E20[s] IN PLACE, so a live entry can sit above a hole and the append pass has
// nowhere contiguous to grow. The ROM's answer is not to re-pack the tables in WRAM -- the samples
// are in ARAM, and only the SPC700 can move them. So this is a PROTOCOL, not an algorithm: the
// 65816 opens command $07 once, then for each move streams three 16-bit words (the source's end,
// the destination base, the length) with one $C7:09DA token handshake apiece, and the driver does
// the block move on its side. We run the real driver, uploaded byte-exact, so it does the work.
//
// ⚠ THE DRIVER'S ACCEPTANCE IS CHECKED EXACTLY ONCE, on the first move: `0290 LDA $0C / BNE` skips
// the test on every later iteration, so $1E0C is a first-time flag rather than a counter. If the
// echo on $2141 is not still $07 the ROM abandons the compaction and takes the REBUILD path below.
//
// Returns 1 if the heap was compacted, 0 if the driver refused and the rebuild ran.
static int sndDefrag(CtAudio* a, uint8_t* m) {
  ldRun(I_SEP + I_IMM8 + I_STA_BB); Wc(a, 1, 0x07);     // 025A SEP #20 / 025C LDA #07 / 025E STA $2141
  ldRun(I_IMM16 + I_LDA_DP16 + I_SEP);                  // 0261 LDY #0000 / 0264 STY $0C / 0266 REP #20
  SM16(m, 0x1E0C, 0);                                   // 0264 STY $0C (16-bit STY, Y = 0)
  for (int y = 0; y < 0x20; y += 2) {                   // 02DB INY / INY / CPY #0020 / BNE ->0266
    ldRun(I_ABSY_W16 + I_BR_NT + 2 * I_IMPL + I_IMM16 + I_BR_T);   // 0268 LDA $1E20,Y / 026B BNE / (02DB INY INY CPY BNE)
    if (M16(m, 0x1E20 + y)) continue;                   // 0268 / 026B BNE -- destination occupied
    int x = y;                                          // 026D TYX -- search UPWARD from here
    while (x < 0x20 && !M16(m, 0x1E20 + x)) x += 2;     // 0270-0279
    if (x >= 0x20) break;                               // 027B BRL ->0320 -- nothing left to move
    unsigned srcEnd = M16(m, 0x1E60 + x);               // 027E LDA $60,X (DP = $1E00)
    ldRun(2 * I_IMPL + I_LDA_DPX16 + I_BR_NT + I_LDA_DPX16 + I_SEP + I_STA_BB); Wc(a, 2, (uint8_t)srcEnd);   // 026D TYX / 0270-0279 / 027E LDA $60,X / 0280 SEP / 0282 STA $2142
    ldRun(I_XBA + I_STA_BB); Wc(a, 3, (uint8_t)(srcEnd >> 8));                       // 0285 XBA / 0286 STA $2143
    ldRun(I_LDA_DP + I_JSR);                                                         // 0289 LDA $84 / 028B JSR
    SM8(m, 0x1E84, sndTok(a, M8(m, 0x1E84)));           // 0289-028E
    ldRun(I_LDA_DP + I_LDA_DP + I_BR_T);                                             // 028E STA $84 / 0290 LDA $0C / 0292 BNE
    g_sndUnits++;
    if (!M8(m, 0x1E0C)) {                               // 0290 LDA $0C / 0292 BNE ->029D
      SM8(m, 0x1E0C, (uint8_t)(M8(m, 0x1E0C) + 1));     // 0294 INC $0C -- EIGHT bits
      ldRun(-I_BR_T + I_BR_NT + I_INC_DP + I_IMM8 + I_CMP_BB);   // (0292 not taken) 0294 INC $0C / 0296 LDA #07 / 0298 CMP $2141
      if (Rc(a, 1) != 0x07) {                           // 0296 LDA #07 / 0298 CMP $2141 / BNE
        // ---- 02E4: the driver would not take command $07. Close the transfer with a zero word,
        // then REBUILD the tables from the song's own set in ROM rather than leaving them half
        // moved. The handshake's result is deliberately NOT stored back to $1E84 here.
        W(a, 2, 0x00); W(a, 3, 0x00);                   // 02E6-02EB
        sndTok(a, M8(m, 0x1E84));                       // 02EE LDA $84 / 02F0 JSR C7/09DA
        int xr = ((M8(m, 0x1E05) + 1) & 0xFF) << 5;     // 02F8 LDA $05 / INC A / AND #00FF / ASL x5
        for (int yr = 0x20; yr > 0; yr -= 2, xr -= 2) { // 031A DEX / DEX / DEY / DEY / BNE
          unsigned v = 0;                               // 0304 LDA $C70E0F,X: the song's own instrument list
          if (a->snd) {                                 //   (word xr/2 - 1 of the set $C7:0E11 + ($05 + 1) * 32 - 32)
            const uint8_t* set = xr >= 2 ? CtAudioInstrSet(a, (xr >> 5) - 1) : NULL;
            int w = ((xr - 2) & 0x1F) >> 1;
            if (set) v = (unsigned)(set[2 * w] | (set[2 * w + 1] << 8));
          } else {
            long tp = (long)0x70E0F + xr;
            v = (tp + 2 <= a->romLen) ? (unsigned)(a->rom[tp] | (a->rom[tp + 1] << 8)) : 0;
          }
          SM16(m, 0x1EFE + yr, v);                      // 0308
          SM16(m, 0x1F1E + yr, v);                      // 030B
          SM16(m, 0x1E1E + yr, 0);                      // 0311
          SM16(m, 0x1E3E + yr, 0);                      // 0314
          SM16(m, 0x1E60 + yr, 0);                      // 0317
        }
        SM8(m, 0x1E84, 0xE0);                           // 0320-0324
        return 0;
      }
    }
    unsigned base = M16(m, 0x1E60 + y);                 // 02A6 LDA $1E60,Y -- the DESTINATION base
    SM16(m, 0x1E20 + y, M16(m, 0x1E20 + x));            // 029F / 02A1 -- move the id down
    SM16(m, 0x1E20 + x, 0);                             // 02A4 STZ $20,X -- and free the source
    ldRun(I_SEP + I_LDA_DPX16 + I_ABSY_W16 + I_LDA_DPX16 + I_ABSY_W16 + I_SEP + I_STA_BB); Wc(a, 2, (uint8_t)base);   // 029D REP / 029F LDA $20,X / 02A1 STA / 02A4 STZ / 02A6 LDA $1E60,Y / 02A9 SEP / 02AB STA $2142
    ldRun(I_XBA + I_STA_BB); Wc(a, 3, (uint8_t)(base >> 8));                         // 02AE XBA / 02AF STA $2143
    SM16(m, 0x1E62 + y, base + M16(m, 0x1E40 + x));     // 02B5 CLC / ADC $40,X / 02B8 STA $1E62,Y
    ldRun(I_XBA + I_SEP + I_IMPL + I_LDA_DPX16 + I_ABSY_W16 + I_SEP + I_LDA_DP + I_JSR);   // 02B2 XBA / REP / CLC / ADC $40,X / STA $1E62,Y / SEP / LDA $84 / JSR
    SM8(m, 0x1E84, sndTok(a, M8(m, 0x1E84)));           // 02BD-02C2
    g_sndUnits++;
    unsigned lenx = M16(m, 0x1E40 + x);                 // 02C6 LDA $40,X
    if (sndLoadTrace()) fprintf(stderr, "[loadtrace] defrag move %u bytes, clock %.2f f\n", M16(m, 0x1E40 + x), (double)ld.clk / 357368.0);
    g_sndMovedBytes += (long)lenx;
    SM16(m, 0x1E40 + y, lenx);                          // 02C8 STA $1E40,Y
    ldRun(I_LDA_DP + I_SEP + I_LDA_DPX16 + I_ABSY_W16 + I_SEP + I_STA_BB); Wc(a, 2, (uint8_t)lenx);   // 02C2 STA $84 / REP / 02C6 LDA $40,X / 02C8 STA $1E40,Y / SEP / 02CD STA $2142
    ldRun(I_XBA + I_STA_BB); Wc(a, 3, (uint8_t)(lenx >> 8));                         // XBA / 02D1 STA $2143
    ldRun(I_LDA_DP + I_JSR);                                                         // 02D4 LDA $84 / JSR
    SM8(m, 0x1E84, sndTok(a, M8(m, 0x1E84)));           // 02D4-02D9
    ldRun(I_LDA_DP);                                                                 // 02D9 STA $84
    g_sndUnits++;
  }
  SM8(m, 0x1E84, 0xE0);                                 // 0320 SEP #20 / LDA #E0 / STA $84
  return 1;
}
// $C7:03EF -- the cmd-$02 pushes use a DIFFERENT unit: TWO bytes per handshake, $2141 then $2142,
// with $2143 untouched. Streaming them three-at-a-time (which is what $C7:06CD does for the cmd-$03
// bulk transfers) drops one source byte per unit AND shifts everything after the second byte -- the
// measured "ours takes oracle bytes 0,1 then 3,4 then 6,7" signature. The two commands are simply
// not the same protocol, and nothing in the earlier dossiers said so.
static void sndStream2(CtAudio* a, uint8_t* m, const uint8_t* src, int n) {
  if (sndLoadTrace()) fprintf(stderr, "[loadtrace] stream2 %d bytes -> %d units\n", n, (n + 1) / 2);
  ldRun(I_IMM16);                                                   // 03EC LDX #0000
  for (int x = 0; x < n; x += 2) {
    ldRun(I_ABSX_W8_X16 + I_STA_BB); Wc(a, 1, src[x]);             // 03EF LDA $1F80,X / 03F2 STA $2141
    ldRun(I_ABSX_W8_X16 + I_STA_BB); Wc(a, 2, src[x + 1]);         // 03F5 LDA $1F81,X / 03F8 STA $2142
    ldRun(I_LDA_DP + I_JSR);                                        // 03FB LDA $84 / 03FD JSR C7/09DA
    SM8(m, 0x1E84, sndTok(a, M8(m, 0x1E84)));
    ldRun(I_LDA_DP + 2 * I_IMPL + I_IMM16 + (x + 2 < n ? I_BR_T : I_BR_NT));   // 0400 STA $84 / INX INX / CPX #0040 / BNE
    g_sndUnits++; g_sndBytes += 2;
  }
  ldRun(I_IMM8 + I_LDA_DP);                                         // 0409 LDA #E0 / STA $84
  SM8(m, 0x1E84, 0xE0);
}
// C7/0431-0459 / C7/047F-04A7 -- a 16-slot parameter block: `LDY #0000`, then per slot REP / LDA $1F00,Y / DEC A / ASL A /
// TAX / LDA $C70C20|$C70C9C,X / SEP / STA $2141 / XBA / STA $2142 and the handshake, INY INY / CPY #0020 / BNE; LDA #E0 /
// STA $84. (`src` = the 16 words those reads produce, the caller's.)
static void sndParamStream(CtAudio* a, uint8_t* m, const uint8_t* src) {
  if (sndLoadTrace()) fprintf(stderr, "[loadtrace] param 32 bytes -> 16 units\n");
  ldRun(I_IMM16);                                                   // LDY #0000
  for (int k = 0; k < 16; k++) {
    ldRun(I_SEP + I_ABSX_W16_X16 + 3 * I_IMPL + I_LONGX_ROM16 + I_SEP + I_STA_BB); Wc(a, 1, src[k * 2]);
    ldRun(I_XBA + I_STA_BB); Wc(a, 2, src[k * 2 + 1]);
    ldRun(I_LDA_DP + I_JSR);
    SM8(m, 0x1E84, sndTok(a, M8(m, 0x1E84)));
    ldRun(I_LDA_DP + 2 * I_IMPL + I_IMM16 + (k < 15 ? I_BR_T : I_BR_NT));   // STA $84 / INY INY / CPY #0020 / BNE
    g_sndUnits++; g_sndBytes += 2;
  }
  ldRun(I_IMM8 + I_LDA_DP);                                         // LDA #E0 / STA $84
  SM8(m, 0x1E84, 0xE0);
}
// $C7:0A12 -- the echo-buffer cull: from slot 0 up, the first resident whose END high byte has
// reached the new ESA page is evicted, and everything from there to slot $1F is cleared.
static void sndCull(uint8_t* m) {
  unsigned esa = M8(m, 0x1EF2);
  for (int s = 0; s < 16; s++) {
    if (M8(m, 0x1E63 + s * 2) >= esa) {                 // $1E60[s+1] high byte = this sample's end
      for (int k = s; k < 16; k++) {
        SM16(m, 0x1E20 + k * 2, 0); SM16(m, 0x1E40 + k * 2, 0);
        SM16(m, 0x1E62 + k * 2, 0);
      }
      return;
    }
    if (M16(m, 0x1E20 + s * 2) == 0) return;            // first empty slot ends the scan
  }
}

// The loader proper. `cmd` is the mailbox command ($10/$11/$14/$15); `m` is the engine's $7E page.
// Returns 1 when the song was loaded, 0 when the driver ABORTED it ($C7:0A80) or the load hit an
// unported boundary, -1 on a structural refusal. Blocking, by the ROM's own design.
int CtAudioLoadSong(CtAudio* a, uint8_t* m, int songId, uint8_t cmd) {
  if (!a || !a->ready || !m || (!a->rom && !a->snd)) return -1;   // m == NULL is a real case: the overworld has no VM
  g_sndUnits = g_sndBytes = g_sndBlocks = 0; g_sndSpcCycles = 0; g_sndSettleCycles = 0; g_sndMovedBytes = 0;
  int nsong = CtAudioSongCount(a);
  if (songId < 0 || songId >= nsong) return -1;                 // $C7:01A5 CMP $C70AE9 / BCS -> drop
  ldRun(18 + I_LDA_DP + 30 + I_BR_NT + I_JSR + 250 + I_RTS + I_JSR + 276 + I_RTS + 2 * I_LDA_DP);   // $01A1-$01B3: SEP, LDA $01, CMP $C70AE9, BCS, JSR C7/0734 (the exception scan, 1 entry), JSR C7/0A39 (the record: 14 instructions), LDA/STA $05

  // ---- $C7:0734: the song-exception scan, then bit2 for cmd >= $14 ------------------------
  // Implemented as the SCAN, not as "inert": rom[$C7:0AD8] happens to start with $FF on this ROM so
  // the list terminates immediately, but a port that hardcoded that would be encoding an accident.
  if (sndException(a, songId)) SM8(m, 0x1E00, M8(m, 0x1E00) | 4);
  if (cmd >= 0x14) SM8(m, 0x1E00, M8(m, 0x1E00) | 4);

  // ---- $C7:0A39: the record, the ECHO-DELAY switch, the port block, the token seed, the abort --
  int edl = 0, scalar = 0;
  CtAudioSongRecord(a, songId, &edl, &scalar);
  SM8(m, 0x1E02, scalar);
  SM8(m, 0x1E03, edl);
  ldRun(I_LDA_DP + I_BR_NT);                                    // $0A53 CMP $F0 / $0A55 BEQ
  if (edl != (int)M8(m, 0x1EF0)) {                              // $0A53 CMP $F0 -- BEFORE the port block
    ldRun(I_JSR + 60 + 240 + I_RTS);                            // $0A57 JSR C7/09FD: the ESA rewrite, then the cull's slot walk (C7/0A12)
    SM8(m, 0x1EF0, edl);
    SM8(m, 0x1EF2, (0xF1 - edl * 8) & 0xFF);                    // ESA page; buffer = [ESA*$100,$F100)
    sndCull(m);                                                 // $09FD -> $0A12
  }
  for (int attempt = 0; ; attempt++) {                          // $0A5A, resent unboundedly on timeout
    ldRun(I_LDA_DP + I_STA_BB); Wc(a, 3, M8(m, 0x1E03));        // 0A5A LDA $03 / 0A5C STA $2143
    ldRun(I_LDA_DP + I_STA_BB); Wc(a, 2, M8(m, 0x1E02));        // 0A5F / 0A61
    ldRun(I_LDA_DP + I_STA_BB); Wc(a, 1, M8(m, 0x1E01));        // 0A64 / 0A66
    ldRun(I_LDA_DP + I_STA_BB); Wc(a, 0, M8(m, 0x1E00));        // 0A69 LDA $00 / 0A6B STA $2140
    ldRun(I_IMM16);                                             // 0A6E LDX #0400
    int ok = 0;
    if (ld.active) ok = ldPoll400(a, M8(m, 0x1E00));            // 0A71 CMP $2140 / BEQ / DEX / BNE
    else for (long c = 0; c < 0x400L * 16; c += ctStepCycles()) { if (R(a, 0) == M8(m, 0x1E00)) { ok = 1; break; } step(a); }   // 0x400 polls x the 16-cycle quantum, as a CYCLE budget
    if (ok || attempt > (ld.active ? 255 : 16)) break;          // the ROM's loop is unbounded; the clocked one allows ~16 frames
    ldRun(I_BR_T);                                              // 0A79 BRA ->0A5A
  }
  ldRun(I_IMPL + I_IMM8 + I_LDA_DP + I_LDA_BB);                 // 0A7B INC A / AND #7F / STA $84 / 0A80 LDA $2141 (then BPL)
  SM8(m, 0x1E84, (uint8_t)((M8(m, 0x1E00) + 1) & 0x7F));        // $0A7B THE TOKEN SEED
  if (Rc(a, 1) & 0x80) {                                        // $0A80 the driver refused the song
    uint8_t t = M8(m, 0x1E84);
    if (ld.ifast) {                                             // the rule: C7/0A85-0A8E on the clock, `DEC A / CMP $2140 /
      ldRun(I_BR_NT + I_LDA_DP + I_STA_BB); Wc(a, 0, t);        //   BEQ` a wait while the old echo stands (off the rule the
      ldRun(I_IMPL);                                            //   SPC is stepped outside the clock, as it always was)
      sndSpin(a, 0, (uint8_t)(t - 1), 0);
    } else {
      W(a, 0, t);
      for (long i = 0; i < SPIN_LIMIT && R(a, 0) == (uint8_t)(t - 1); i++) step(a);
    }
    SM8(m, 0x1E00, 0);                                          // $1E05 is deliberately NOT set
    return 0;
  }
  SM8(m, 0x1E05, (unsigned)songId);                             // $01B3 -- BUSY, and the id register

  // ---- $C7:01B5-$0375: the instrument-set DIFF ---------------------------------------------
  // Charged instruction by instruction as the port walks the same tables the ROM does (the loops are
  // the same shape): the resident search is what makes this phase cost -- measured 20 lines (~27k
  // cycles) on the scene-8 pendant load (`--hookpc C701B5` line 17 -> `C7025A` line 37), up to 16 x 16
  // compares at 88 cycles each. Access costs as in the loader clock (FastROM 6, WRAM 8).
  ldRun(I_SEP + I_IMM16 + 16 * (40 + 40 + 2 * I_IMPL + I_BR_T) - I_BR_T + I_BR_NT);   // 01B5-01C2: zero $1F00/$1F20 (STZ abs,X x2, DEX DEX, BNE) x16
  for (int k = 0; k < 16; k++) { SM16(m, 0x1F00 + k * 2, 0); SM16(m, 0x1F20 + k * 2, 0); }
  int lastOcc = -1;
  for (int s = 0; s < 16; s++) if (M16(m, 0x1E20 + s * 2)) lastOcc = s;
  ldRun(I_IMM16 + (lastOcc >= 0 ? (15 - lastOcc) * 88 + 34 + I_BR_T : 16 * 88 - I_BR_T + I_BR_NT)   // 01C4-01CE: the topmost resident, from slot 15 down (LDA abs,Y / BNE / DEY DEY / BNE)
        + I_LDA_DP16 + 2 * (I_IMM16 + I_LDA_DP16) + I_LDA_DP16 + 18 + 3 * I_IMPL + I_IMPL + 18 + I_LDA_DP16);   // 01D0-01E8: STY $0C, the two staging pointers, the set's end index
  const uint8_t* set = CtAudioInstrSet(a, songId);
  int nset = 0, ntl = 0;
  for (int k = 0; k < 16 && set; k++) {
    unsigned id = (unsigned)(set[k * 2] | (set[k * 2 + 1] << 8));
    ldRun(30 + I_BR_NT);                                        // 01EA LDA $C70E11,X / 01EE BEQ
    if (!id) { ldRun(I_BR_T - I_BR_NT); break; }                // the list is zero-terminated
    SM16(m, 0x1F00 + nset * 2, id); nset++;
    ldRun(44 + 2 * 50 + I_LDA_DP16 + I_BR_NT);                  // 01F0 STA ($12) / INC $12 x2 / 01F6 LDY $0C / 01F8 BEQ
    int resident = 0;
    for (int s = lastOcc; s >= 0; s--) { ldRun(34); if (M16(m, 0x1E20 + s * 2) == id) { resident = 1; ldRun(I_BR_T); break; } ldRun(I_BR_NT + 2 * I_IMPL + (s ? I_BR_T : I_BR_NT)); }   // 01FA CMP $1E1E,Y / BEQ / DEY DEY / BNE
    if (!resident) { SM16(m, 0x1F20 + ntl * 2, id); ntl++; ldRun(44 + 2 * 50); }   // only the MISSING ones get loaded: 0203 STA ($14) / INC $14 x2
    ldRun(2 * I_IMPL + I_LDA_DP16 + I_BR_T);                    // 0209 INX INX / CPX $0E / BNE ->01EA
  }
  ldRun(34 + (ntl ? I_BR_T + I_SEP + I_IMM16 : I_BR_NT + 18));  // 020F LDA $1F20 / BNE ->0217 (else JMP C7/037B)
  if (ntl) {
    for (int s = 0; s < 16; s++) {                              // evict anything the song does not use
      unsigned id = M16(m, 0x1E20 + s * 2);
      ldRun(I_LDA_DPX16 + I_BR_NT);                             // 021C LDA $20,X / 021E BEQ
      if (!id) { ldRun(I_BR_T - I_BR_NT); break; }
      int used = 0; ldRun(I_IMM16);                             // 0220 LDY #0000
      for (int k = 0; k < 16; k++) { ldRun(34); if (M16(m, 0x1F00 + k * 2) == id) { used = 1; ldRun(I_BR_T); break; } ldRun(I_BR_NT + 2 * I_IMPL + I_IMM16 + (k < 15 ? I_BR_T : I_BR_NT)); }   // 0223 CMP $1F00,Y / BEQ / INY INY / CPY / BNE (the ROM walks all 16 slots)
      if (!used) { SM16(m, 0x1E20 + s * 2, 0); SM16(m, 0x1E40 + s * 2, 0); ldRun(I_LDA_DPX16); }   // 022F STZ $20,X
      ldRun(2 * I_IMPL + I_IMM16 + (s < 15 ? I_BR_T : I_BR_NT));   // 0231 INX INX / CPX #0020 / BNE
    }
    int firstFree = 0;
    while (firstFree < 16 && M16(m, 0x1E20 + firstFree * 2)) firstFree++;
    ldRun(I_IMM16 + firstFree * (I_LDA_DPX16 + I_BR_NT + 2 * I_IMPL + I_IMM16 + I_BR_T) + (firstFree < 16 ? I_LDA_DPX16 + I_BR_T : 0) + I_LDA_DP16);   // 0238-0246: the first free slot, STX $0C
    int lastOcc2 = -1;
    for (int s = 0; s < 16; s++) if (M16(m, 0x1E20 + s * 2)) lastOcc2 = s;
    ldRun(I_IMM16 + (lastOcc2 >= 0 ? (15 - lastOcc2) * 88 + I_LDA_DPX16 + I_BR_T : 16 * 88 - I_BR_T + I_BR_NT) + I_LDA_DP16 + I_BR_NT);   // 0248-0255: the topmost resident from slot 15 down, CPX $0C / BNE
    if (firstFree != lastOcc2 + 1) {
      // $C7:0250 -- a HOLE below a live entry. The ROM compacts the heap through driver command $07
      // before appending; two songs in a row are enough to reach this (load $10 then $01), so a
      // build that declined here lost its music on an ordinary field-to-field walk.
      if (!sndDefrag(a, m)) {
        // The driver refused command $07 and sndDefrag rebuilt the tables from ROM. There is no
        // heap left to append into, so report and stop rather than write into a heap the driver
        // and we now disagree about.
        fprintf(stderr, "[audio] song $%02X: the driver refused command $07; tables rebuilt, load"
                        " abandoned\n", songId);
        SM8(m, 0x1E05, 0xE0); SM8(m, 0x1E00, 0);
        return 0;
      }
      // The compaction moved every live entry down, so the append cursor has to be re-derived.
      firstFree = 0;
      while (firstFree < 16 && M16(m, 0x1E20 + firstFree * 2)) firstFree++;
    } else ldRun(18);                                           // 0257 JMP C7/0326
    // $C7:0326-$033F: the append prologue -- SEP / STZ $0A / LDX #0000, the first slot whose LOW byte is 0 (`LDA $20,X` with
    // M=8 / BEQ / INX INX / CPX #0020 / BNE); none -> 0338 `LDA #00 / STA $1F,X / DEX / BNE` clears $1E20-$1E3F and X = 0.
    SM8(m, 0x1E0A, 0);
    firstFree = 0;
    while (firstFree < 16 && M8(m, 0x1E20 + firstFree * 2)) firstFree++;
    ldRun(I_SEP + I_LDA_DP + I_IMM16 + firstFree * (I_LDA_DPX + I_BR_NT + 2 * I_IMPL + I_IMM16 + I_BR_T));
    if (firstFree < 16) ldRun(I_LDA_DPX + I_BR_T);
    else { ldRun(I_BR_NT - I_BR_T + I_IMM8 + 0x20 * (I_LDA_DPX + I_IMPL + I_BR_T) - I_BR_T + I_BR_NT);
           for (int x = 0x20; x > 0; x--) SM8(m, 0x1E1F + x, 0);
           firstFree = 0; }
    ldRun(I_IMM16);                                             // 033F LDY #0000
    int k = 0;
    for (; k < ntl; k++) {                                      // $C7:0342 APPEND
      unsigned id = M16(m, 0x1F20 + k * 2);
      int len = 0;
      const uint8_t* blk = CtAudioSampleBlock(a, (int)id, &len);
      ldRun(I_SEP + I_ABSX_W16_X16 + I_BR_NT + I_LDA_DPX16 + 2 * I_LDA_DP16 + I_JSR);   // 0342 REP / LDA $1F20,Y / BEQ / STA $20,X / STX $1C / STY $1E / JSR C7/0655
      if (!blk || firstFree >= 16) break;
      unsigned base = M16(m, 0x1E60 + firstFree * 2);
      SM16(m, 0x1E20 + firstFree * 2, id);
      SM16(m, 0x1E40 + firstFree * 2, (unsigned)len);
      SM16(m, 0x1E62 + firstFree * 2, base + (unsigned)len);    // = $1E60[slot+1], the fence
      sndCmdBlk(a, m, base, 0x03);
      sndStream(a, m, blk, len, SND_SAMPLE);
      firstFree++;
      ldRun(2 * I_LDA_DP16 + 4 * I_IMPL + I_IMM16 + (k < 15 ? I_BR_T : I_BR_NT));   // 0352 LDX $1C / LDY $1E / INY INY INX INX / CPY #0020 / BNE ->0342
    }
    if (k < 16) {                                               // 0342 REP / LDA $1F20,Y / BEQ ->0361: the list's zero entry
      ldRun(I_SEP + I_ABSX_W16_X16 + I_BR_T + I_SEP + I_LDA_DPX16 + I_LDA_DP16);   // 0361 REP / LDA $60,X / STA $F6
      for (int x = firstFree * 2; x < 0x20; x += 2) {           // 0367 LDA #0000 / STA $62,X / STA $40,X / INX INX / CPX #0020 / BNE
        SM16(m, 0x1E62 + x, 0); SM16(m, 0x1E40 + x, 0);
        ldRun(I_IMM16 + 2 * I_LDA_DPX16 + 2 * I_IMPL + I_IMM16 + (x + 2 < 0x20 ? I_BR_T : I_BR_NT));
      }
    } else ldRun(I_BR_T);                                       // 035F BRA ->0375 (all sixteen loaded)
    ldRun(I_SEP + I_IMM8 + I_LDA_DP);                           // 0375 SEP / LDA #E0 / STA $84
    SM16(m, 0x1EF6, M16(m, 0x1E60 + firstFree * 2));            // the heap high-water mark
  }

  // ---- $C7:037B-$03C6: the DSP sample DIRECTORY, staged at $1F80 then pushed to ARAM $1E80 --------
  // `REP / LDA #1F80 / STA $12 / LDX #0000`; per song slot X: `LDA $1F00,X / BEQ ->03C8` (the first empty slot ENDS the
  // build), the resident search `LDY #0000 / CMP $1E20,Y / BEQ / INY INY / CPY #0040 / BNE` (32 words: the ids AND the
  // lengths after them), found -> 039E the slot's start `$1E60,Y` and start + the loop offset `$C70BA4 + (id-1)*2`; not
  // found -> 0399 two zeros; both `STA ($12)` with the pointer stepped 4.
  ldRun(I_SEP + I_IMM16 + I_LDA_DP16 + I_IMM16);
  { unsigned ptr = 0x1F80;
    for (int x = 0; x < 0x20; x += 2) {
      unsigned id = M16(m, 0x1F00 + x);
      ldRun(I_ABSX_W16_X16);                                    // 0385 LDA $1F00,X
      if (!id) { ldRun(I_BR_T); break; }                        // 0388 BEQ ->03C8
      ldRun(I_BR_NT + I_IMM16);                                 // 038A LDY #0000
      int y = 0, found = 0;
      for (; y < 0x40; y += 2) {
        ldRun(I_ABSX_W16_X16);                                  // 038D CMP $1E20,Y
        if (M16(m, 0x1E20 + y) == id) { found = 1; ldRun(I_BR_T); break; }
        ldRun(I_BR_NT + 2 * I_IMPL + I_IMM16 + (y + 2 < 0x40 ? I_BR_T : I_BR_NT));
      }
      if (found) {
        unsigned base = M16(m, 0x1E60 + y);
        unsigned loop = sndInstrWord(a, id, 0);
        SM16(m, ptr, base); SM16(m, ptr + 2, (base + loop) & 0xFFFF);
        ldRun(I_PHX16 + 3 * I_IMPL + I_ABSX_W16_X16 + I_STA_DPI16 + 2 * I_INC_DP16 + I_IMPL + I_LONGX_ROM16 + I_STA_DPI16
              + I_PLX16 + I_BR_T);                              // 039E PHX / DEC / ASL / TAX / LDA $1E60,Y / STA ($12) / INC $12 x2 / CLC / ADC / STA ($12) / PLX / BRA
      } else {
        SM16(m, ptr, 0); SM16(m, ptr + 2, 0);
        ldRun(I_IMM16 + I_BR_T + 2 * I_STA_DPI16 + 2 * I_INC_DP16);   // 0399 LDA #0000 / BRA / 03B5 STA ($12) / INC $12 x2 / STA ($12)
      }
      ptr += 4;
      ldRun(2 * I_INC_DP16 + 2 * I_IMPL + I_IMM16 + (x + 2 < 0x20 ? I_BR_T : I_BR_NT));   // 03BD INC $12 x2 / INX INX / CPX #0020 / BNE
    }
  }
  sndHeadLit(a, m, 0x1E80, 0x02, I_SEP); sndStream2(a, m, m + 0x1F80, 0x40);   // 03C8 SEP / the head / 03EC the stream

  // ---- $C7:040D / $045B: the two 16-slot parameter blocks -----------------------------------
  // $C7:0431 / $047F walk $1F00 -- the SONG's instrument list -- not the $1E20 residency slots, and push TWO bytes per
  // handshake. An empty entry does `LDA $1F00,Y / DEC A / ASL A / TAX`, i.e. X = 0xFFFE, which WRAPS the long,X read into
  // bank $C8: the values actually living at $C8:0C1E / $C8:0C9A (read here, measured 0x0502 / 0x1FC2), not invented defaults.
  { uint8_t buf[32];
    for (int k = 0; k < 16; k++) {
      unsigned id = M16(m, 0x1F00 + k * 2);
      unsigned v = sndInstrWord(a, id, 1);
      buf[k * 2] = (uint8_t)v; buf[k * 2 + 1] = (uint8_t)(v >> 8);
    }
    sndHeadLit(a, m, 0x1F40, 0x02, I_SEP); sndParamStream(a, m, buf);
    for (int k = 0; k < 16; k++) {
      unsigned id = M16(m, 0x1F00 + k * 2);
      unsigned v = sndInstrWord(a, id, 2);
      buf[k * 2] = (uint8_t)v; buf[k * 2 + 1] = (uint8_t)(v >> 8);
    }
    sndHeadLit(a, m, 0x1FC0, 0x02, I_SEP); sndParamStream(a, m, buf);
  }

  // ---- $C7:04A9-04BE: the first instrument's slot, doubled -> $FC ----------------------------
  // `LDA $1F00 / BNE` (M=8: the low byte; zero -> JMP C7/04C0), `LDX #0020`, `CMP $1E,X / BEQ / DEX DEX / BNE` from slot 15
  // down on the LOW bytes, then `DEX DEX / STX $FC`: the slot x 2 -- or $FFFE when nothing matched (X ran out at 0).
  ldRun(I_ABSX_W8);                                             // 04A9 LDA $1F00 (abs, M=8: 26)
  if (!M8(m, 0x1F00)) ldRun(I_BR_NT + 18);                      // 04AE JMP C7/04C0
  else {
    ldRun(I_BR_T + I_IMM16);                                    // 04AC BNE / 04B1 LDX #0020
    int x = 0x20;
    for (;;) {
      ldRun(I_LDA_DPX);                                         // 04B4 CMP $1E,X
      if (M8(m, 0x1E1E + x) == M8(m, 0x1F00)) { ldRun(I_BR_T); break; }
      x -= 2; ldRun(I_BR_NT + 2 * I_IMPL + (x ? I_BR_T : I_BR_NT));
      if (!x) break;
    }
    x = (x - 2) & 0xFFFF;
    ldRun(2 * I_IMPL + I_LDA_DP16);                             // 04BC DEX DEX / STX $FC
    SM16(m, 0x1EFC, (unsigned)x);
  }

  // ---- $C7:04C0: the driver MODE, and the $2F00 block swap it forces -------------------------
  // SEP / LDA $10 / CLC / ADC #FF / LDA #00 / ROL (A = $10 != 0) / CMP $11 / BEQ ->04D4 | STA $10 / STA $11 / INC $F3;
  // 04D4 LDA $F3 / BEQ ->04DB | JSR C7/0922.
  { int mode = M8(m, 0x1E10) ? 1 : 0;
    ldRun(I_SEP + I_LDA_DP + 3 * I_IMPL + I_IMM8 + I_LDA_DP);
    if (mode != (int)M8(m, 0x1E11)) { SM8(m, 0x1E10, (unsigned)mode); SM8(m, 0x1E11, (unsigned)mode);
                                      SM8(m, 0x1EF3, M8(m, 0x1EF3) + 1); ldRun(I_BR_NT + 2 * I_LDA_DP + I_INC_DP); }
    else ldRun(I_BR_T);
    ldRun(I_LDA_DP);
    if (M8(m, 0x1EF3)) { ldRun(I_BR_NT + I_JSR); sndSfxSet(a, m); }   // 04D8 JSR C7/0922 (it clears $F3 itself, 0991)
    else ldRun(I_BR_T); }

  // ---- $C7:04DB: THE SEQUENCE -> ARAM $2000 --------------------------------------------------
  // The head (04DB SEP, the literal $2000 / 03), then 04FF-0525: REP / LDA $05 / AND #00FF / STA $82 / ASL / CLC / ADC $82
  // / TAX / SEP, the pointer from $C7:0D18,X (LDA long,X / STA dp, three bytes), LDY $12 / STZ $12 / STZ $13; the size word
  // and the BPL loop in sndStream.
  { int n = 0; const uint8_t* seq = CtAudioSeqBlock(a, songId, &n);
    if (seq) { sndHeadLit(a, m, 0x2000, 0x03, I_SEP);
               ldRun(I_SEP + I_LDA_DP16 + I_IMM16 + I_LDA_DP16 + 2 * I_IMPL + I_LDA_DP16 + I_IMPL + I_SEP
                     + 3 * (I_LONGX_ROM8 + I_LDA_DP) + I_LDA_DP16 + 2 * I_LDA_DP);
               sndStream(a, m, seq, n, SND_SEQ); } }

  // ---- $C7:059A: the 36-byte per-song block -> ARAM $F120 ------------------------------------
  { const uint8_t* perc = sndPerc(a, songId);
    if (perc) sndPercBlock(a, m, perc); }

  // ---- $C7:0601: GO. Without this final $E0 the driver never starts playing. ------------------
  sndMark("go", 0);
  if (sndLoadTrace()) fprintf(stderr, "[loadtrace] GO, clock %.2f f (clk %lld, polls %ld, units %ld, cpu %lld)\n", (double)ld.clk / 357368.0, ld.clk, ld.polls, g_sndUnits, ld.dbgCpuAll);
  ldRun(I_IMM8 + I_IMM16 + I_STA_BB); Wc(a, 1, 0xE0);           // 0601 LDA #E0 / 0603 LDX #0400 / 0606 STA $2141
  ldRun(I_STA_BB); Wc(a, 0, 0xE0);                              // 0609 STA $2140
  if (ld.active) ldPoll400(a, 0xE0);                            // 060C CMP $2140 / BEQ / DEX / BNE
  else for (long c = 0; c < 0x400L * 16; c += ctStepCycles()) { if (R(a, 0) == 0xE0) break; step(a); }
  ldRun(I_LDA_DP + I_JSR + 240 + I_RTS);                        // 0614 STA $05 / 0616 JSR C7/0A12 (the cull's slot walk, ~240)
  SM8(m, 0x1E05, 0xE0);                                         // $0614 -- READY again
  sndCull(m);                                                   // $0616
  SM8(m, 0x1E00, 0);                                            // $0192
  return 1;
}

// ==== C7/0038 AGAIN, ON A RUNNING DRIVER: the reset's `JSL $C7:0000` (C0/0053), CLOCKED ===============================
// A reset does not reset the SPC. C0/000E calls the same upload the power-on does, and the upload's first loop is
// `LDA #F0 / STA $2141 / LDA #FF / STA $2140 / CPX $2140 / BNE` until the ports read $BBAA -- the running driver takes
// the F0 and hands the SPC back to its IPL, which answers AA BB, and then the whole driver goes over again, byte by
// byte. Same protocol as uploadDriver, run on the loader clock with the listing's own instructions, on the RESET
// background (no NMI, no HDMA: FD:C000's `$4200 = 0`). The master cycles it returns are CPU time the reset spends
// before C0/0057 -- the host holds the screen black for them.
int CtAudioColdBoot(CtAudio* a, long long* master, int* frames) {
  if (master) *master = 0; if (frames) *frames = 0;
  if (!a || !a->rom) return 0;
  ldStart(2, 0);
  ldRun(I_PHP + 28 + I_PHP + 2 * I_SEP + 3 * 28 + I_SEP + I_IMM8 + I_PHP + 26 + I_IMM16 + 28 + 34 + I_IMM16);  // 0038-004D prologue
  int ok = 0;
  for (long i = 0; i < SPIN_LIMIT; i++) {
    ldRun(I_IMM8); Wc(a, 1, 0xF0);                                   // 0050 LDA #F0 / 0052 STA $2141
    ldRun(I_STA_BB + I_IMM8); Wc(a, 0, 0xFF);                         // 0055 LDA #FF / 0057 STA $2140
    ldRun(I_STA_BB);
    ldRun(3 * RA + 2 * BA);                                           // 005A CPX $2140 (16-bit: ports 0 and 1)
    if (Rc(a, 0) == 0xAA && Rc(a, 1) == 0xBB) { ldRun(I_BR_NT); ok = 1; break; }
    ldRun(I_BR_T);                                                    // 005D BNE ->0050
  }
  if (!ok) { ld.active = 0; return 0; }
  uint8_t idx = 0;
  ldRun(I_IMM16 + 30); Wc(a, 2, (uint8_t)(a->dst[0] & 0xFF));         // 005F LDX #0 / 0062 LDA $C7001C / STA $2142
  ldRun(I_STA_BB + 30); Wc(a, 3, (uint8_t)(a->dst[0] >> 8));          // 0069 LDA $C7001D / STA $2143
  ldRun(I_STA_BB + I_IMM8); Wc(a, 1, 0xCC);                           // 0070 LDA #CC / STA $2141
  ldRun(I_STA_BB); Wc(a, 0, 0xCC);                                    // 0075 STA $2140
  ldRun(I_STA_BB);
  for (;;) { ldRun(I_CMP_BB); if (Rc(a, 0) == 0xCC) { ldRun(I_BR_NT); break; } ldRun(I_BR_T); ld.polls++; }   // 0078
  for (int b = 0; b < C7_NBLOCKS; b++) {
    const uint8_t* src = a->blk[b];
    // 007D LDA #00 / XBA / LDA $C70010,X / STA $12 / LDA $C70011,X / STA $13 / LDA #C7 / STA $14 / LDY #0 /
    // LDA [$12],Y / CLC / ADC #02 / STA $0C / INY / LDA [$12],Y / ADC #00 / STA $0D / INY
    ldRun(I_IMM8 + I_XBA + 30 + I_LDA_DP + 30 + I_LDA_DP + I_IMM8 + I_LDA_DP + I_IMM16 + I_LDA_DPIY_ROM + I_IMPL +
          I_IMM8 + I_LDA_DP + I_IMPL + I_LDA_DPIY_ROM + I_IMM8 + I_LDA_DP + I_IMPL);
    idx = 0;
    for (uint16_t y = 0; y < a->size[b]; y++) {
      ldRun(I_LDA_DPIY_ROM); Wc(a, 1, src[y]);                        // 00A2 LDA [$12],Y / STA $2141
      ldRun(I_STA_BB + I_XBA); Wc(a, 0, idx);                         // 00A7 XBA / STA $2140
      ldRun(I_STA_BB);
      for (;;) { ldRun(I_CMP_BB); if (Rc(a, 0) == idx) { ldRun(I_BR_NT); break; } ldRun(I_BR_T); ld.polls++; }   // 00AB
      idx++;
      ldRun(I_IMPL + I_XBA + I_IMPL + 2 * RA + 2 * WA + (y + 1 < a->size[b] ? I_BR_T : I_BR_NT));   // 00B0 INC/XBA/INY/CPY $0C/BNE
    }
    idx = (uint8_t)(idx + 3);                                         // 00B7 XBA / INC A x3 / BNE / INC A
    ldRun(I_XBA + 3 * I_IMPL + (idx ? I_BR_T : I_BR_NT + I_IMPL));
    if (idx == 0) idx = 1;
    ldRun(2 * I_IMPL + I_IMM16 + (b == C7_NBLOCKS - 1 ? I_BR_T : I_BR_NT));   // 00BE INX INX / CPX #000C / BEQ
    if (b == C7_NBLOCKS - 1) break;
    ldRun(I_XBA + 30); Wc(a, 2, (uint8_t)(a->dst[b + 1] & 0xFF));     // 00C5 XBA / LDA $C7001C,X / STA $2142
    ldRun(I_STA_BB + 30); Wc(a, 3, (uint8_t)(a->dst[b + 1] >> 8));    // 00CD / 00D1
    ldRun(I_STA_BB + I_XBA); Wc(a, 1, idx);                           // 00D4 XBA / STA $2141
    ldRun(I_STA_BB); Wc(a, 0, idx);                                   // 00D8 STA $2140
    ldRun(I_STA_BB);
    for (;;) { ldRun(I_CMP_BB); if (Rc(a, 0) == idx) { ldRun(I_BR_NT); break; } ldRun(I_BR_T); ld.polls++; }   // 00DB
    ldRun(I_BR_T);                                                    // 00E0 BRA ->007D
  }
  ldRun(I_XBA + I_IMM8); Wc(a, 3, (uint8_t)(a->dst[0] >> 8));          // 00E2 XBA / LDA #02 / STA $2143
  ldRun(I_STA_BB + I_IMM8); Wc(a, 2, (uint8_t)(a->dst[0] & 0xFF));     // 00E8 LDA #00 / STA $2142
  ldRun(I_STA_BB + I_IMM8); Wc(a, 1, 0x00);                           // 00ED LDA #00 / STA $2141
  ldRun(I_STA_BB + I_XBA); Wc(a, 0, idx);                             // 00F2 XBA / STA $2140
  ldRun(I_STA_BB);
  for (;;) { ldRun(I_CMP_BB); if (Rc(a, 0) == idx) { ldRun(I_BR_NT); break; } ldRun(I_BR_T); ld.polls++; }   // 00F6
  ldRun(I_XBA); Wc(a, 0, 0x00);                                       // 00FB XBA / STA $2140
  ldRun(I_STA_BB + I_IMM16);                                          // 00FF LDX #0100
  ldRun(0x100L * (3 * RA + IA + WA + I_IMPL) + 0xFFL * I_BR_T + I_BR_NT);   // 0102 STA $1DFF,X / DEX / BNE, 256 times
  ldRun(I_SEP + 5 * RA + I_IMPL + 3 * RA + 3 * I_LDA_DP16);           // 0108 REP / LDA $C740B4 / CLC / ADC #4700 / STA x3
  ldRun(I_IMM16 + 0x800L * I_IMPL + 0x7FFL * I_BR_T + I_BR_NT);       // 0118 LDX #0800 / DEX / BNE -- the delay loop
  ldRun(I_SEP + 2 * (I_IMM8 + I_LDA_DP) + I_IMM8 + I_LDA_DP + 3 * I_IMPL + I_IMM8 + I_IMPL + I_IMPL + I_IMM8 +
        I_LDA_DP + I_LDA_DP + I_IMM8 + I_LDA_DP + 18);                // 011E-013D ... JMP C7/0192
  ldRun(296);                                                         // $0192-$01A0: the epilogue and the RTL
  ldCatch(a);
  a->lastMaster = ld.clk; a->lastFrames = ld.nmis; a->lastEndLine = ld.vPos; a->lastPolls = ld.polls;
  ld.active = 0;
  a->cycleCarry -= (double)ld.clk * LD_APU_PER_MASTER;               // the SPC time it already ran
  a->ready = 1;
  if (master) *master = a->lastMaster; if (frames) *frames = a->lastFrames;
  return 1;
}

// ---- measuring the driver (disasm/notes/audio_spc_driver.md) -------------------------------------------------------
// CT_SPC_COVERAGE=<file>: every SPC700 address the driver executed, `ADDR COUNT` a line, written at exit (the listing
// tools/spc700dis.py --coverage marks). CT_DSP_LOG=<file>: every S-DSP register write, `frame apucycle reg value`.
static uint32_t* g_spcCov;
static FILE* g_dspLog;
static long g_audFrameNo;
static void spcCovHook(Apu* apu, uint16_t pc) { (void)apu; g_spcCov[pc]++; }
// CT_MIXER_CAPTURE=<file>: what the S-DSP was given and what it made, for the native mixer's replay (ct.exe --mixreplay,
// tools/mixer_replay.py; disasm/notes/audio_mixer.md sec. 9). Binary, little-endian: "CTMX", u32 version 1, then records:
//   'S' u32 apucycles, u32 n, n bytes of dsp_handleState, 64 KB ARAM -- a segment starts (power-on, a seeded or loaded SPC)
//   'W' varint dcyc, reg, val      -- an S-DSP register write ($F3), dcyc APU cycles after the previous record
//   'A' varint dcyc, u16 adr, val  -- an SPC write to its RAM that CHANGES the byte (the DSP's own echo writes are not
//                                     the SPC's: both replays make them)
//   'R' varint dcyc, reg, val      -- an S-DSP register read and the value the SPC got
//   'H' u32 n, u32 fnv             -- the next n samples the DSP made (FNV-1a/32 over int16 L,R), 4096 a record
// One SPC is followed: the last CtAudio made, seeded or loaded (each starts a new segment).
static FILE* g_mixCap;
static const Apu* g_mixApu;
static uint32_t g_mixLast;              // the APU cycle of the last record
static uint32_t g_mixHash, g_mixHashN;
#define MIXCAP_CHUNK 4096
static void mixPut32(uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; fwrite(b, 1, 4, g_mixCap); }
static void mixStamp(int tag, const Apu* apu) {
  uint32_t d = apu->cycles - g_mixLast;
  g_mixLast = apu->cycles;
  fputc(tag, g_mixCap);
  do { uint8_t b = d & 0x7f; d >>= 7; fputc(b | (d ? 0x80 : 0), g_mixCap); } while (d);
}
static void mixFlushHash(void) {
  if (!g_mixHashN) return;
  fputc('H', g_mixCap); mixPut32(g_mixHashN); mixPut32(g_mixHash);
  g_mixHash = 2166136261u; g_mixHashN = 0;
}
static void mixSampleHook(Dsp* dsp, int16_t l, int16_t r) {
  if (dsp->apu != g_mixApu) return;
  uint8_t b[4] = { (uint8_t)l, (uint8_t)((uint16_t)l >> 8), (uint8_t)r, (uint8_t)((uint16_t)r >> 8) };
  for (int i = 0; i < 4; i++) g_mixHash = (g_mixHash ^ b[i]) * 16777619u;
  if (++g_mixHashN == MIXCAP_CHUNK) mixFlushHash();
}
static void mixRamHook(Apu* apu, uint16_t adr, uint8_t val) {
  if (apu != g_mixApu || apu->ram[adr] == val) return;
  mixStamp('A', apu); fputc(adr & 0xff, g_mixCap); fputc(adr >> 8, g_mixCap); fputc(val, g_mixCap);
}
static void mixReadHook(Apu* apu, uint8_t adr, uint8_t val) {
  if (apu != g_mixApu) return;
  mixStamp('R', apu); fputc(adr, g_mixCap); fputc(val, g_mixCap);
}
static void mixSnapshot(const Apu* apu) {   // a segment begins: the DSP's whole state and the RAM it reads
  if (!g_mixCap) return;
  mixFlushHash();
  g_mixApu = apu; g_mixLast = apu->cycles;
  StateHandler* sh = sh_init(true, NULL, 0);
  dsp_handleState(apu->dsp, sh);
  fputc('S', g_mixCap); mixPut32(apu->cycles); mixPut32((uint32_t)sh->offset);
  fwrite(sh->data, 1, (size_t)sh->offset, g_mixCap); fwrite(apu->ram, 1, 0x10000, g_mixCap);
  sh_free(sh);
}
static void mixCapClose(void) { if (g_mixCap) { mixFlushHash(); fclose(g_mixCap); g_mixCap = NULL; } }

static void dspLogHook(Apu* apu, uint8_t adr, uint8_t val) {
  if (g_mixCap && apu == g_mixApu) { mixStamp('W', apu); fputc(adr, g_mixCap); fputc(val, g_mixCap); }
  if (g_dspLog) fprintf(g_dspLog, "%ld %llu %02X %02X\n", g_audFrameNo, (unsigned long long)apu->cycles, adr, val);
}
static void spcCovWrite(void) {
  const char* p = getenv("CT_SPC_COVERAGE");
  char path[1024];
  if (p && strstr(p, "%d")) { snprintf(path, sizeof path, p, (int)getpid()); p = path; }   // one file per process
  FILE* f = p ? fopen(p, "w") : NULL;
  if (!f) return;
  for (int i = 0; i < 0x10000; i++) if (g_spcCov[i]) fprintf(f, "%04X %u\n", i, g_spcCov[i]);
  fclose(f);
}
static void dspLogClose(void) { if (g_dspLog) fclose(g_dspLog); }
static void measureInit(void) {
  static int done;
  if (done) return;
  done = 1;
  const char* c = getenv("CT_SPC_COVERAGE");
  if (c && *c && (g_spcCov = (uint32_t*)calloc(0x10000, sizeof *g_spcCov))) { g_apuOpHook = spcCovHook; atexit(spcCovWrite); }
  const char* d = getenv("CT_DSP_LOG");
  if (d && *d && (g_dspLog = fopen(d, "w"))) { g_apuDspWriteHook = dspLogHook; atexit(dspLogClose); }
  const char* x = getenv("CT_MIXER_CAPTURE");
  if (x && *x && (g_mixCap = fopen(x, "wb"))) {
    static char buf[1 << 20];
    setvbuf(g_mixCap, buf, _IOFBF, sizeof buf);
    fwrite("CTMX", 1, 4, g_mixCap); mixPut32(1);
    g_mixHash = 2166136261u; g_mixHashN = 0;
    g_apuDspWriteHook = dspLogHook; g_apuRamWriteHook = mixRamHook; g_apuDspReadHook = mixReadHook;
    g_dspSampleHook = mixSampleHook;
    atexit(mixCapClose);
  }
}

CtAudio* CtAudioInit(const uint8_t* rom, long romLen) { return CtAudioInitWith(rom, romLen, NULL); }

CtAudio* CtAudioInitWith(const uint8_t* rom, long romLen, const CtAssets* snd) {
  if (snd && !CtAssetGet(snd, CT_ASSET_SND_BOOT, 0, NULL)) snd = NULL;   // a container without the sound: the ROM
  if (!snd && (!rom || romLen < C7_BASE + 0x6000)) return NULL;
  measureInit();
  CtAudio* a = (CtAudio*)calloc(1, sizeof *a);
  if (!a) return NULL;
  a->shell = (Snes*)calloc(1, sizeof(Snes));          // palTiming = false = NTSC
  if (!a->shell) { free(a); return NULL; }
  a->rom = rom; a->romLen = romLen;
  a->snd = snd;
  if (snd) while (CtAssetGet(snd, CT_ASSET_SND_SAMPLE, (uint32_t)(a->ninst + 1), NULL)) a->ninst++;
  a->evenFrame = 1;                                    // power-on: the PPU flips its field at (0,0), the first frame is even
  a->engineMode = 0; a->postLine = 12;
  a->rulePostAt = LD_PAD_WAIT; a->ruleNmiWait = LD_PAD_WAIT;   // the rule: the field's main loop, after its NMI's pad latch
  a->ruleIrqLine = -1;                                         // ...and no V-IRQ until the host says one is armed
  a->apu = apu_init(a->shell);
  apu_reset(a->apu);                                   // SPC PC -> $FFC0, the IPL
  if (CtSndLockstepWanted()) CtSndLockstepAttach(a->apu);   // the C driver beside this SPC (src/sound/lockstep.h)
  mixSnapshot(a->apu);
  a->ready = uploadDriver(a, rom, romLen);
  audReset(a);                                         // the IPL transfer's own output is not the game's
  if (!a->ready) fprintf(stderr, "[audio] $C7 upload did not complete -- running silent\n");
  return a;
}

void CtAudioFree(CtAudio* a) {
  if (!a) return;
  if (a->apu == g_mixApu) { mixFlushHash(); g_mixApu = NULL; }
  if (a->apu) apu_free(a->apu);
  free(a->shell);
  free(a);
}

int CtAudioReady(const CtAudio* a) { return a && a->ready; }

// ONE FRAME's SPC TIME. The console's frame is 262 lines of 1364 master cycles -- but in a non-interlaced odd frame line 240 is
// 4 cycles short (snes.c: `hPos == 1360 && vPos == 240 && !evenFrame`), and the SPC's time is the master clock's: 17,088 APU
// cycles an even frame, 17,087.81 an odd one. A constant 17,088 runs the sound processor 0.1 cycle a frame ahead of the
// console: over a long stage a song's marker crosses a frame boundary early -- the ending's `FF 2A`, page A's `F5 08` 10,850
// frames in, a frame before the cartridge's (ending-stage-logic-ff2a-ff2f-vs-oracle). (A MEAN of 17,087.90 every frame is not
// this either: the field flips each frame, the short line is the odd frame's.) The field flips at each frame's start
// (ppu_handleFrameStart) -- at power-on's (0,0) too: the first frame is even. A frame's slice runs vblank to vblank, so it holds
// the line 240 of the frame it starts in: its parity is used, then flipped.
#define APU_CYCLES_PER_MASTER (APU_CYCLES_PER_SEC / (1364.0 * 262.0 * 60.0))
static double apuFrameCycles(const CtAudio* a) { return APU_CYCLES_PER_FRAME - (a->evenFrame ? 0.0 : 4.0 * APU_CYCLES_PER_MASTER); }
void CtAudioFrameSkip(CtAudio* a) { if (a) a->evenFrame ^= 1; }
void CtAudioFrame(CtAudio* a) {
  if (!a) return;
  g_audFrameNo++;
  { static int ck = -1; static long fr = 0; if (ck < 0) { const char* e = getenv("CT_AUDIO_SPCCLOCK"); ck = (e && *e) ? 1 : 0; }
    if (ck) fprintf(stderr, "spcclock pass%ld total=%lld carry=%.1f" "\n", fr++, g_apuTotal, a->cycleCarry); }
  double want = apuFrameCycles(a) + a->cycleCarry;
  a->evenFrame ^= 1;                                   // (the next slice's frame)
  if (want < 1.0) { a->cycleCarry = want; a->mutedFrame = 1; return; }   // a clocked JSL already ran this frame's SPC time
  a->mutedFrame = 0;
  int n = (int)want;
  int ran = apuRun(a, n);                              // overshoots by at most one opcode
  a->cycleCarry = want - (double)ran;                  // carry it, so long runs do not drift
}

void CtAudioSamples(CtAudio* a, int16_t* out, int samples) {
  if (!a || !out || samples <= 0) return;
  audTake(a);                                          // a translated stage's bus runs this SPC without apuRun
  // One frame of the stream: the next 534 samples (32040*32/60 APU cycles = 17088 = 534 x 32, the frame CtAudioFrame
  // runs), resampled to `samples` the way dsp_getSamples does it. When the stream is short of a frame -- a host tick
  // with no SPC time behind it, which only a translated stage's once-per-iteration run can cause -- the frame is the
  // DSP's newest 534, as the reference would read it.
  int16_t fr[536 * 2];
  int take = 534;
  // CT_AUDIO_FIFOTRACE=<n>: every n output frames, how many samples wait in the FIFO -- the output's latency behind the
  // SPC, in samples (534 = one frame). A backlog that only grows is sound drifting behind the picture.
  { static int every = -2; static long calls = 0;
    if (every == -2) { const char* e = getenv("CT_AUDIO_FIFOTRACE"); every = e ? atoi(e) : -1; }
    if (every > 0 && (calls++ % every) == 0)
      fprintf(stderr, "audiofifo out%ld queued=%u (%.1f frames) dropped=%ld carry=%.1f muted=%d\n", calls - 1, a->fifoLen,
              a->fifoLen / 534.0, a->fifoDropped, a->cycleCarry, a->mutedFrame); }
  // THE DRAIN (a HOST output mechanism, like the device's level gate). The SPC's own clock is the cartridge's and must not
  // be touched -- its state is what the song loads' handshakes and the translated stages are graded on -- but a few
  // host paths give it time no displayed frame stands for (an unclocked handshake at a location load, a clocked
  // session's last opcode: ~100-250 samples a load, 0.06 a frame): left in the FIFO, the sound slides behind the picture
  // for good. So the output reads them away gently: while at least 4 samples wait beyond this frame's 534 and the SPC
  // time a song load or a clocked JSL legitimately pre-ran (`cycleCarry` < 0: the frames the SPC will stand still for,
  // carry/32 samples, which play on the frames they belong to), a frame takes 536 samples instead of 534 -- 0.4%
  // faster for as long as that lasts, inaudible. The FIFO settles at this frame plus 2-3 samples: a frame's SPC makes
  // 533-535 (whole instructions against the carried fraction), and with 2 in hand no frame reads short (measured: at
  // "2 or more" 79 frames of the demo read 533 and padded one).
  if (a->fifoLen >= 536) {
    long debt = (a->cycleCarry < 0) ? (long)(-a->cycleCarry / 32.0) : 0;
    if ((long)a->fifoLen - 534 - debt >= 4) take = 536;
  }
  if (a->fifoLen >= (unsigned)take) {
    for (int i = 0; i < take; i++) { unsigned r = (a->fifoHead + (unsigned)i) % CT_AUD_FIFO; fr[i * 2] = a->fifo[r * 2]; fr[i * 2 + 1] = a->fifo[r * 2 + 1]; }
    a->fifoHead = (a->fifoHead + (unsigned)take) % CT_AUD_FIFO; a->fifoLen -= (unsigned)take;
  } else if (a->fifoLen > 0) {
    // SHORT OF A FRAME with samples queued -- a translated stage that exited mid-frame ran only part of its last frame's
    // SPC time: play what it produced and hold the last sample for the rest, CONSUMING it. Left queued (the reference's
    // newest-534 read below consumes nothing), the partial frame sat behind the picture for good: each stage exit left
    // the demo's sound 296-444 samples later (CT_AUDIO_FIFOTRACE, the race's f8576, entry 3's f9061).
    unsigned n = a->fifoLen, i = 0;
    for (; i < n; i++) { unsigned r = (a->fifoHead + i) % CT_AUD_FIFO; fr[i * 2] = a->fifo[r * 2]; fr[i * 2 + 1] = a->fifo[r * 2 + 1]; }
    for (; i < 534; i++) { fr[i * 2] = fr[(n - 1) * 2]; fr[i * 2 + 1] = fr[(n - 1) * 2 + 1]; }
    a->fifoHead = (a->fifoHead + n) % CT_AUD_FIFO; a->fifoLen = 0;
  } else dsp_getSamples(a->apu->dsp, fr, 534);
  // CT_AUDIO_PCM=<path>: the frame as played at 32040 Hz (int16 L,R), 534 samples a frame -- a drained frame's 536 read
  // down to 534 by the nearest sample, centred (both ends kept, two skipped inside the frame), so frame k of the file is
  // output frame k and its boundaries are the output's: the same stream as ct.exe --peek's PEEK_PCM, side by side.
  // (Writing the whole 536 drifted the file 2 samples a drained frame against its readers' 534-sample frames -- blind
  // boundaries; writing the first 534 cut 2 samples at every drained frame's end -- a jump at the boundary.)
  { static FILE* pcm = (FILE*)1;
    if (pcm == (FILE*)1) { const char* pp = getenv("CT_AUDIO_PCM"); pcm = (pp && *pp) ? fopen(pp, "wb") : NULL; }
    if (pcm) { int16_t pf[534 * 2];
               for (int i = 0; i < 534; i++) { int r = (int)((i + 0.5) * take / 534.0); pf[i * 2] = fr[r * 2]; pf[i * 2 + 1] = fr[r * 2 + 1]; }
               fwrite(pf, sizeof pf, 1, pcm); fflush(pcm); } }
  double adder = (double)take / samples, location = 0.0;
  for (int i = 0; i < samples; i++) {
    int r = (int)location; if (r > take - 1) r = take - 1;
    out[i * 2] = fr[r * 2]; out[i * 2 + 1] = fr[r * 2 + 1];
    location += adder;
  }
}

int CtAudioPost(CtAudio* a, uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3) {
  if (!a || !a->ready) return 0;
  // $C7:061C, in the ROM's own order: high port first, $2140 last -- $2140 is
  // the one the driver polls, so it must be written after its operands.
  if (!ld.active) { W(a, 3, p3); W(a, 2, p2); W(a, 1, p1); W(a, 0, p0); return 1; }
  // Clocked: C7/061C's loop -- the four stores, up to $400 polls for the echo of $2140, and while
  // $1E00 is still non-zero (it is) the whole thing again. Bounded here; the ROM's is not.
  for (int tries = 0; tries < 64; tries++) {
    ldRun(I_SEP + I_LDA_DP + I_STA_BB); Wc(a, 3, p3);         // 061C SEP #20 / 061E LDA $03 / 0620 STA $2143
    ldRun(I_LDA_DP + I_STA_BB); Wc(a, 2, p2);                 // 0623 / 0625
    ldRun(I_LDA_DP + I_STA_BB); Wc(a, 1, p1);                 // 0628 / 062A
    ldRun(I_LDA_DP + I_IMM16 + I_STA_BB); Wc(a, 0, p0);       // 062D LDA $00 / 062F LDX #0400 / 0632 STA $2140
    if (ldPoll400(a, p0)) break;                              // 0635 CMP $2140 / 0638 BEQ ->0643
    ldRun(I_LDA_DP + I_BR_NT + I_BR_T);                       // 063D LDA $00 / 063F BEQ / 0641 BRA ->061C
  }
  ldRun(80);                                                  // 0643 CMP #FC ... the short tail
  return 1;
}

int g_ctAudioTraceFrame = 0;
int CtAudioService(CtAudio* a, uint8_t* mem7E) {
  if (!a || !mem7E) return 0;      // NULL mem7E = no scene VM (overworld); nothing stages commands there
  uint8_t* mb = mem7E + 0x1E00;                        // $00:1E00 mirrors $7E:1E00
  int rc;
  // THE RULE: a JSL that never touches the ports -- the driver latch down (C7/0157), nothing staged (C7/015B), a command
  // the dispatch drops or that has no derived handler -- is CPU work only: it costs nothing and moves no SPC. (The host's
  // per-frame catch-all calls this with an empty mailbox every frame; the ROM has no such JSL at all.)
  { uint8_t c0 = mb[0x00];
    int touches = (mb[0x05] & 0x80) && c0 && ((c0 & 0x80) || c0 == 0x10 || c0 == 0x11 || c0 == 0x14 || c0 == 0x15 ||
                                              (c0 >= 0x18 && c0 < 0x30) || c0 == 0x70);
    if (CtAudioRuleOn() && !a->ruleOff && a->engineMode != 2 && !a->ruleNmiOff && !touches) {
      int undone = (mb[0x05] & 0x80) && ((c0 >= 0x30 && c0 < 0x40) || c0 == 0x71);   // (the dispatch below says -1)
      mb[0x00] = 0x00;                                 // $C7:0192's STZ $00, every exit
      a->lastMaster = 0; a->lastFrames = 0; a->lastPolls = 0;
      return undone ? -1 : 0;
    } }
  // CT_SNDPOSTS=1: every JSL that reaches the ports, `sndpost f<host frame> cmd=<$1E00> p1=<$1E01>` -- the port's side of
  // tools/sound_post_cmp.py, which reads the cartridge's $2140 command writes (C7/0635, a song's C7/0A6E) and their $2141.
  { static int sp = -1; if (sp < 0) sp = getenv("CT_SNDPOSTS") ? 1 : 0;
    if (sp && (mb[0x05] & 0x80) && mb[0x00]) printf("sndpost f%d cmd=%02X p1=%02X\n", g_ctAudioTraceFrame, mb[0x00], mb[0x01]); }
  if (!ldStartRule(a))                                 // (the rule: from where the SPC and the CPU's last wait stand)
    ldStart(a->engineMode, a->postLine);               // the JSL begins: the clock runs until the RTL
  ldRun(ld.ifast ? 740 - 54 : 740);                    // JSL 54 (the caller's: free under the rule) + C7/0004 JMP 18 + the $0140 prologue 264 (PHB PHD PHP REP REP PHA PHX PHY SEP LDA PHA PLB) + $0150-$0173 dispatch 404 (LDX PHX PLD SEP ... JMP ($0AD9,X))
  uint8_t cmd = mb[0x00];
  // The two early tests BRANCH TO THE COMMON EXIT, they do not return before it: C7/0157
  // `LDA $05 / BPL ->0192` and C7/015B `LDA $00 / BEQ ->0192`, and $0192 is the `STZ $00`
  // teardown. Ours used to `return 0` ahead of the clear at the bottom of this function, so a
  // command staged while the driver latch was down stayed in the mailbox instead of being
  // consumed. Fold them into the chain so every path reaches the clear, as the ROM does.
  if (!(mb[0x05] & 0x80))      rc = 0;                 // $C7:0157 — driver-present latch, -> $0192
  else if (cmd == 0x00)        rc = 0;                 // $C7:015B — nothing pending,      -> $0192
  else if (cmd & 0x80)         rc = CtAudioPost(a, mb[0], mb[1], mb[2], mb[3]) ? 1 : 0;  // $015F -> $061C
  else if (cmd < 0x10)         rc = 0;                 // $0161 — below the API, ignored
  else if (cmd < 0x18) {                               // $0169 -- the $C7:0AD9 jump table
    // The table is 10 -> $01A1, 11 -> $01A1, 12 -> $0192, 13 -> $0192, 14 -> $01A1, 15 -> $01A1,
    // 16 -> $0192, 17 -> $0192 (raw bytes A1 01 A1 01 92 01 92 01 A1 01 A1 01 92 01 92 01). The
    // $0192 entries ARE the common exit, i.e. genuine no-ops, not unimplemented handlers.
    if (cmd == 0x10 || cmd == 0x11 || cmd == 0x14 || cmd == 0x15) {
      // The loader reproduces the oracle's ARAM byte-exactly in all six regions (--songload, song
      // $30 vs a cold boot at f133, the frame its own load completes). ON by default; CT_NOAUDIO=1
      // still disables the whole subsystem, and CT_AUDIO_SONGLOAD=0 disables just the loader.
      static int gate = -1;
      if (gate < 0) { const char* e = getenv("CT_AUDIO_SONGLOAD"); gate = (e && *e == '0') ? 0 : 1; }
      rc = gate ? CtAudioLoadSong(a, mem7E, mb[1], cmd) : -1;
    }
    else rc = 0;
  }
  else if (cmd < 0x30)         rc = CtAudioPost(a, mb[0], mb[1], mb[2], mb[3]) ? 1 : 0;  // $0176 -> $061C
  else if (cmd < 0x40)         rc = -1;                // $017D -> $C7:071D:  NOT DERIVED
  else if (cmd == 0x70)        rc = sndMode70(a, mem7E);   // $0184 -> $C7:08E3, the driver mode without a song
  else if (cmd == 0x71)        rc = -1;                // $018B -> $C7:0755:  NOT DERIVED
  else                         rc = 0;                 // falls to $0192
  mb[0x00] = 0x00;                                     // $C7:0192 — STZ $1E00 on every exit
  ldRun(296);                                          // $0192-$01A0: the epilogue's pulls and the RTL
  ldCatch(a);                                          // the SPC is level with the CPU at the RTL
  a->lastMaster = ld.clk; a->lastFrames = ld.nmis; a->lastEndLine = ld.vPos; a->lastPolls = ld.polls;
  if (ldTrace() && ld.polls) { fprintf(stderr, "[ldtrace] polls per token:"); for (int k = 0; k <= 64; k++) if (ldHist[k]) fprintf(stderr, " %d:%ld", k, ldHist[k]);
    fprintf(stderr, "  | %lld master, %d NMIs, line %d\n", ld.clk, ld.nmis, ld.vPos); memset(ldHist, 0, sizeof ldHist); }
  // CT_AUDIO_RULETRACE=1: every JSL under the rule's clock -- the command, where it began (the beam, the SPC's progress
  // into the frame), the SPC time it took and the frame boundaries it crossed: the port's side of the oracle's
  // PEEK_APUTRACE=<from>:<to> with PEEK_WRITEWATCH=1E00:4.
  if (ld.ifast && getenv("CT_AUDIO_RULETRACE"))
    fprintf(stderr, "[ruleaudio] cmd=%02X %02X %02X %02X from %ld master past the vblank (asked %ld): %lld master, %d NMI(s) "
                    "crossed, returns on line %d h %d, %ld polls\n", cmd, mb[1], mb[2], mb[3], a->lastRuleStart, a->rulePostAt,
            ld.clk, ld.nmis, ld.vPos, ld.hPos, ld.polls);
  ld.active = 0; ld.ifast = 0;
  // The SPC time this JSL consumed is HOST time the field will now lose (the caller freezes for
  // lastFrames): debit it from CtAudioFrame's per-frame budget so the emulated SPC is not advanced
  // twice, once here and once per frozen frame. Nothing to debit for the unclocked callers.
  a->cycleCarry -= (double)ld.clk * LD_APU_PER_MASTER;
  return rc;
}
// $7E:5AAA `LDA $2143 / AND #$0F / BEQ ->5AAA` -- the title setup's own wait, run AFTER the song
// load's JSL has returned, and the last thing $7E:5978 does before the sequence's first NMI. It is
// not decoration: measured on the oracle it spins ~33000 times across boot frames 133..139, just
// over SIX FRAMES, which is part of why the sequence's tasks are installed at boot 139 and not at
// 133. Runs on the same loader clock as the JSL, so the frames it costs are MEASURED here rather
// than baked -- the caller loses them the way the ROM's CPU does.
// ⚠ The ROM's loop is UNBOUNDED (it hangs forever if the driver never answers). This one stops at
// SPIN_LIMIT and reports 0, because a host that hangs is worse than one that says it gave up.
int CtAudioWaitReplyNibble(CtAudio* a, int* frames) {
  if (frames) *frames = 0;
  if (!a || !a->ready) return 0;
  ldStart(a->engineMode, a->postLine);
  int got = 0;
  for (long i = 0; i < SPIN_LIMIT; i++) {
    ldRun(I_LDA_BB + I_IMM8);                          // 5AAA LDA $2143 / 5AAD AND #$0F
    if (Rc(a, 3) & 0x0F) { ldRun(I_BR_NT); got = 1; break; }
    ldRun(I_BR_T);                                     // 5AAF BEQ ->5AAA
  }
  ldCatch(a);
  a->lastMaster = ld.clk; a->lastFrames = ld.nmis; a->lastEndLine = ld.vPos; a->lastPolls = ld.polls;
  ld.active = 0;
  a->cycleCarry -= (double)ld.clk * LD_APU_PER_MASTER;   // the SPC time this spin already consumed
  if (frames) *frames = a->lastFrames;
  return got;
}

// Seed this instance from a LakeSnes Apu (a loaded savestate's): ARAM, the SPC's registers and the
// DSP's voice state are copied whole and only the object pointers are re-planted. After this the
// emulated driver is IN THE ORACLE'S STATE -- the same song playing, the same resident sets in ARAM,
// the same timers -- so a clocked load runs the transfer the oracle is about to run, not a cold one.
void CtAudioSeedFromApu(CtAudio* a, const Apu* src) {
  if (!a || !src || !a->apu) return;
  Apu* d = a->apu; Snes* shell = d->snes; Spc* spc = d->spc; Dsp* dsp = d->dsp;
  memcpy(d, src, sizeof *d); d->snes = shell; d->spc = spc; d->dsp = dsp;
  { void* mem = spc->mem; SpcReadHandler r = spc->read; SpcWriteHandler w = spc->write; SpcIdleHandler i = spc->idle;
    memcpy(spc, src->spc, sizeof *spc); spc->mem = mem; spc->read = r; spc->write = w; spc->idle = i; }
  memcpy(dsp, src->dsp, sizeof *dsp); dsp->apu = d;
  if (src->snes && src->snes->ppu) a->evenFrame = src->snes->ppu->evenFrame ? 1 : 0;   // the state's field: the next frame flips it
  a->ready = 1; a->cycleCarry = 0.0; a->mutedFrame = 0;
  audReset(a);
  CtSndLockstepResync(a->apu);
  mixSnapshot(d);
}
#define CT_APUSTATE_MAGIC 0x50415443u   /* 'CTAP' LE */
int CtAudioSaveState(CtAudio* a, const char* path) {
  if (!a || !a->apu || !path) return 0;
  StateHandler* sh = sh_init(true, NULL, 0);
  apu_handleState(a->apu, sh);
  FILE* f = fopen(path, "wb");
  uint32_t hdr[2] = { CT_APUSTATE_MAGIC, (uint32_t)sh->offset };
  int ok = f && fwrite(hdr, 4, 2, f) == 2 && fwrite(sh->data, 1, (size_t)sh->offset, f) == (size_t)sh->offset;
  if (f) fclose(f);
  sh_free(sh);
  return ok;
}
int CtAudioLoadState(CtAudio* a, const char* path) {
  if (!a || !a->apu || !path) return 0;
  FILE* f = fopen(path, "rb");
  if (!f) return 0;
  uint32_t hdr[2] = { 0, 0 }; uint8_t* buf = NULL;
  int ok = fread(hdr, 4, 2, f) == 2 && hdr[0] == CT_APUSTATE_MAGIC && hdr[1] > 0 && hdr[1] < (1u << 20)
           && (buf = (uint8_t*)malloc(hdr[1])) != NULL && fread(buf, 1, hdr[1], f) == hdr[1];
  fclose(f);
  if (ok) {
    StateHandler* sh = sh_init(false, buf, (int)hdr[1]);
    apu_handleState(a->apu, sh);
    sh_free(sh);
    mixSnapshot(a->apu);
    a->ready = 1; a->cycleCarry = 0.0; a->mutedFrame = 0;   // (as CtAudioSeedFromApu: the adopted driver's moment)
    audReset(a);
    CtSndLockstepResync(a->apu);
  }
  free(buf);
  return ok;
}
void CtAudioSetEngineMode(CtAudio* a, int mode) { if (a) a->engineMode = (mode >= 0 && mode <= 2) ? mode : 0; }   // 2 = the loader's quiet background
void CtAudioSetPostLine(CtAudio* a, int vPos)   { if (a) a->postLine = vPos; }
void CtAudioSetRulePost(CtAudio* a, long postAt, long nmiWait) { if (a) { a->rulePostAt = postAt; a->ruleNmiWait = nmiWait; } }
void CtAudioSetRuleNmiOff(CtAudio* a, int off) { if (a) a->ruleNmiOff = off ? 1 : 0; }
void CtAudioSetRuleOff(CtAudio* a, int off) { if (a) a->ruleOff = off ? 1 : 0; }
void CtAudioSetRuleHdma(CtAudio* a, const uint8_t* wram128, int prog, int on, int nmiOn, int cutLine) {
  if (!a) return;
  memset(&a->hdma, 0, sizeof a->hdma);
  a->hdma.prog = wram128 ? prog : 0; a->hdma.wram = wram128; a->hdma.on = on; a->hdma.nmiOn = nmiOn;
  a->hdma.cutLine = (cutLine >= 0 && cutLine < LD_LINES_PER_FRAME) ? cutLine : -1;
}
void CtAudioSetRuleIrq(CtAudio* a, int line) { if (a) a->ruleIrqLine = (line >= 0 && line < LD_LINES_PER_FRAME) ? line : -1; }
long CtAudioRulePadWait(void) { return LD_PAD_WAIT; }
// The master cycles from the start of vblank (line 225, h 0) to line `line` (h 0) of the next picture: where a NMI's
// raster wait CD/3B82 for that line releases the CPU (the rule's post position after a battle NMI's line wait).
long CtAudioRuleLineAt(int line) { return (long)(LD_LINES_PER_FRAME - 225 + line) * LD_MASTER_PER_LINE; }
long long CtAudioLastPostRaster(const CtAudio* a, int* frames, int* endLine, long* polls) {
  if (!a) return 0;
  if (frames) *frames = a->lastFrames; if (endLine) *endLine = a->lastEndLine; if (polls) *polls = a->lastPolls;
  return a->lastMaster;
}

void CtAudioSeedMailboxPage(const CtAudio* a, uint8_t* mem7E) {
  if (!a || !mem7E) return;
  uint8_t* m = mem7E;
  memset(m + 0x1E00, 0, 0x100);                        // $C7:00FF tail (§7 step 6)
  // Block 2's end address (dest + size), stored three times. Computed, not baked.
  uint16_t endB2 = (uint16_t)(a->dst[2] + a->size[2]);
  m[0x1E60] = (uint8_t)(endB2 & 0xFF); m[0x1E61] = (uint8_t)(endB2 >> 8);
  m[0x1EF6] = (uint8_t)(endB2 & 0xFF); m[0x1EF7] = (uint8_t)(endB2 >> 8);
  m[0x1EF8] = (uint8_t)(endB2 & 0xFF); m[0x1EF9] = (uint8_t)(endB2 >> 8);
  m[0x1EFA] = 0x00; m[0x1EFB] = 0xC7;                  // consumers NOT DERIVED (§8 gap 3)
  m[0x1EF0] = 0x05; m[0x1EF1] = 0x00;
  m[0x1EF2] = (uint8_t)(0xF1 - m[0x1EF0] * 8);   // ESA page = $F1 - EDL*8, derived (0xC9 for EDL 5)
  m[0x1E05] = 0xE0;                                    // bit7 = the driver-present latch
  m[0x1E00] = 0x00;
}

const uint8_t* CtAudioAram(const CtAudio* a) { return a ? a->apu->ram : NULL; }
struct Apu* CtAudioApu(CtAudio* a) { return a ? a->apu : NULL; }
uint32_t CtAudioApuCycles(const CtAudio* a) { return a ? a->apu->cycles : 0; }
// A TRANSLATED STAGE'S EXIT FRAME. A stage's bus runs this SPC itself while it is open (the host's slice is skipped,
// field.c `!seqX && !gwX`), one frame of SPC per stage frame -- and the frame it leaves in (`JML C0:0000`) its bus ran
// only up to the jump. The cartridge's SPC does not stop there: the rest of that frame's 17,088 cycles run on while the
// CPU goes on into C0/000E. The port ran nobody's: the remainder was lost, the frame's output came out ~528 samples short
// and the last sample was held over the gap. MEASURED (CT_XLAT_APUTRACE + CT_AUDIO_FIFOTRACE, the attract's race):
// stage call 425 ran 220 APU cycles and jumped, out8534 held 6 samples (16.5 ms of one held value, audible -- the user's
// "the curtain after the race cuts the music"); entry 3's exit, out9025, the same. The cartridge's PCM (PEEK_PCM, the
// real and the rule's) has no held run of 40 samples or more in the whole demo. `ran`: the APU cycles the stage's bus
// ran in that last frame.
void CtAudioStageExitFrame(CtAudio* a, uint32_t ran) {
  if (!a || !a->ready) return;
  double want = apuFrameCycles(a) - (double)ran;
  if (want < 1.0) return;
  int got = apuRun(a, (int)want);
  a->cycleCarry += want - (double)got;                 // (an opcode's overshoot, carried as CtAudioFrame carries it)
}

uint32_t CtAudioAramFnv(const CtAudio* a) {
  if (!a) return 0;
  uint32_t h = 2166136261u;
  for (int i = 0; i < 0x10000; i++) h = (h ^ a->apu->ram[i]) * 16777619u;
  return h;
}

void CtAudioPorts(const CtAudio* a, uint8_t in[4], uint8_t out[4]) {
  if (!a) return;
  for (int i = 0; i < 4; i++) { if (in) in[i] = a->apu->inPorts[i]; if (out) out[i] = a->apu->outPorts[i]; }
}

int CtAudioVoicesSounding(const CtAudio* a) {
  if (!a) return 0;
  int n = 0;
  for (int c = 0; c < 8; c++) if (a->apu->dsp->channel[c].gain) n++;
  return n;
}
