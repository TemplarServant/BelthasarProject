// title_intro.h -- the title sequence ($C3 sequence 2, `FF 02`) and the sequence-table stages that share its
// machine (disasm/notes/title_intro.md, sequence_table.md). The title opens on a mode-7 pendulum (745 frames),
// then the clock face, the "HRONO" slide, the mode-2 TRIGGER logo on an offset-per-tile wave and the copyright
// fade: about 1990 frames before the attract demo.
// The port runs the overlay's machine: the $C3 script interpreter (C3/0B03, DP = $0A00), the NMI's hook lists
// ($0940 vblank, $0920 tasks), the tasks, the sprite-package drawers (C3/0D0E, 0D5E) and the palette-job runner
// (FD/E39C). Only the boot's graphics, the overlay's tables and one seed state come from src/data.
// One tick is one NMI in the ROM's order: the vblank hooks latch the shadows into the PPU (so the screen shows the
// state the tasks left a frame earlier), the frame counter advances, the task list runs.
#ifndef CT_TITLE_INTRO_H
#define CT_TITLE_INTRO_H

#include <stdint.h>

// The overlay's live cells, in the order src/data/title_intro.h seeds them.
enum {
  CT_TI_C08, CT_TI_C11, CT_TI_C13, CT_TI_C15, CT_TI_C17, CT_TI_C44, CT_TI_C5A, CT_TI_C5C,
  CT_TI_C5E, CT_TI_C60, CT_TI_C62, CT_TI_C64, CT_TI_C66, CT_TI_C68, CT_TI_C6C, CT_TI_C6E,
  CT_TI_C70, CT_TI_C72, CT_TI_CFE, CT_TI_CELLS
};

// The sound commands the exit issues ($7E:34AB, $7E:34C2): stores into the mailbox $7E:1E00, then the
// `JSL $C7:0004` that flushes it. Kept as separate stores because $34D5 sets only $1E00; $1E01..$1E03 keep
// what the last command left ($1E03 is persistent RAM, audio_commands.md sec. 10.4).
typedef struct CtTitleSoundSink {
  void* ctx;
  void (*mail)(void* ctx, int off, uint8_t v);   // STA $1E00+off
  void (*post)(void* ctx);                       // JSL $C7:0004
  void (*wait)(void* ctx);                       // $7E:5AAA `LDA $2143 / AND #$0F / BEQ`
} CtTitleSoundSink;

// $2123/$2124/$2125: per-layer window enables and the mask logic, in the LakeSnes layer order
// (BG1..BG4, OBJ, and 5 = the COLOUR window, which $2130's clip/prevent-math modes test).
typedef struct CtSeqWin { uint8_t w1en, w1inv, w2en, w2inv, logic; } CtSeqWin;

// One HDMA channel's $43x0-$43x7 block, as far as the installer $7E:36CC programs it: DMAP+BBAD, the table
// address and a bank stored to both $4304 and $4307 (sequence_table.md 7.2). The live walk (table address,
// repeat count) belongs to the frame being drawn and lives in the render.
typedef struct CtSeqHdmaCh {
  uint8_t  dmap, bAdr, aBank, indBank;   // $43x0 / $43x1 / $43x4 / $43x7
  uint16_t aAdr;                         // $43x2/$43x3 -- where the channel's table starts
} CtSeqHdmaCh;

typedef struct CtTitleIntro {
  int      frame;                  // ticks since the sequence's own frame 0; -1 = not started
  // The interpreter runs on two register pages: $7E:3749 and $7E:3754 are the same `JSL $C3:0AFF` behind
  // `LDA #$0A00 / TCD` and `LDA #$0B00 / TCD`. Entry 3 installs both ($0A20, $0B20) as separate tasks; the
  // title runs page A only. Page A's sixteen registers are the title's PPU shadow cells.
  struct CtSeqPage {
    uint16_t reg[16], ctr[16];     // the page's registers and its $xx22 loop counters
    uint16_t ip, ret;              // $xx20 script pointer, $xx30 call return
  } scr[2];
  uint16_t task[16], vbl[8];       // the $0920 task list and the $0940 vblank-hook list
  uint16_t cell[CT_TI_CELLS];      // $0008/$0011/.../$00FE, named by the enum above
  uint16_t slide[5], tgt[5];       // $0046 / $0050 -- the CHRONO letters' 1/16-px slide
  uint16_t wave[64];               // $0C00 -- the offset-per-tile table BG1 ripples on
  uint16_t waveTail[64];           // (host mod, no ROM cell) the columns the table pushed off its right end, newest first:
                                   // the ripple past the 32nd column, which only the widescreen's right side shows
  // (host mod) a slide the script runs on BG1's scroll -- a $8n/$9n loop whose body adds a constant to reg 1 (the TRIGGER
  // logo's -256 -> -48, 2 px a frame): where it starts and where the loop will leave it. The widescreen stretches it so the
  // logo comes in from beyond the wider screen's edge and still lands where the ROM puts it.
  int16_t  slideFrom, slideEnd; uint8_t slideOn;
  uint8_t  anchorLo, anchorHi;     // (host mod) the OAM slots [lo, hi) the copyright block drew this frame -- the widescreen
                                   // anchors them to the screen's right end
  uint8_t  oam[0x220];             // $0700 -- the OAM shadow the tasks draw into
  uint8_t  f[10];                  // $00F0..$00F9 -- the multiply/divide scratch ($F8 is read back)
  uint16_t palShadow[256];         // $2200 -- what the vblank hook DMAs into CGRAM
  uint8_t  job[12][12];            // $0520 -- the ROM's palette-job records (FD/E39C)
  uint8_t  mode, tm, ts;           // $2105 / $212C / $212D, written straight by the tasks
  // The rest of the PPU's layer setup, written by a stage's setup. The stages differ (the title: mode 2, BG1 at
  // $6400/$5000, OBJ at $4000; the credits: mode 0, BG1 at $0400/$0000, OBJ at $0000), so the render reads them.
  uint8_t  bg1sc, bg2sc, bg12nba, obsel, cgadsub;   // $2107 / $2108 / $210B / $2101 / $2131
  // The window block and $2130's colour-math modes besides add/sub. The title's setup never writes $2123-$212F,
  // so they stay zero there.
  uint8_t  win1l, win1r, win2l, win2r;  // $2126 / $2127 / $2128 / $2129
  uint8_t  tmw, tsw;                    // $212E / $212F -- which layers the window may clip
  uint8_t  clipMode, preventMath;       // $2130 bits 7-6 and 5-4
  uint8_t  addSub, subtract, halfColor; // $2130 bit 1, $2131 bit 7, $2131 bit 6
  CtSeqWin win[6];                      // $2123 / $2124 / $2125
  // HDMA. $038C is what the NMI stores into $420C every frame (C3/08FD), so it is the enable mask
  // the render honours; the channels themselves are installed by $7E:36CC.
  CtSeqHdmaCh hdma[8];
  uint8_t  hdmaEnable;             // $038C
  uint8_t  hram[0x400];            // $7E:F000..$F3FF -- the per-line HDMA data the tasks rebuild
  uint8_t  stage;                  // $0384 & $1F -- which sequence-table entry is running
  uint8_t  page;                   // $0382 -- the credits' page. Note: the common init does not clear
                                   // it, so it walks 1..5 across the attract's five visits.
  uint16_t vram[0x8000];           // the PPU's VRAM: the boot's fonts, plus what a setup writes
  uint8_t  bri, briPeriod, briCnt; // $038D / $038E / $038F -- the NMI's own $2100 brightness trio
  uint8_t  sndBusy, sndCur, sndReq; // $0388 / $0389 / $038A -- the NMI's own song-change watcher
  uint16_t joy;                    // $0002 -- the frame's auto-read joypad, as C3/0A8E leaves it
  uint8_t  exitFlag;               // $7F0060, as $7E:5B99 leaves it (cleared by holding L or R)
  uint8_t  done;                   // $7E:0000 -- the sequence's done flag: the stage is over
  CtTitleSoundSink snd;            // where $7E:34AB's and $34C2's commands go

  struct CtTiLatch {                // the vblank latch: the PPU state this frame renders with
    int16_t  m7[4];                // $211B-$211E
    uint16_t bg1h, bg1v, bg3h;     // $210D / $210E / $2111
    uint8_t  bri;                  // $2100 -- $038D, or $80 (forced blank) once it reaches 0
    uint8_t  oam[0x220];
    uint16_t cgram[256];
    uint16_t offs[64];             // BG3's tilemap words -- mode 2 reads them as per-column offsets
    uint16_t offsTail[64];         // (host mod) waveTail, latched with offs
    uint8_t  anchorLo, anchorHi;   // (host mod) the OAM slots anchored right, latched with the OAM
  } l;
} CtTitleIntro;

// Debug/test: back the sequence machine's $7E reads (the HDMA descriptor tables) with a real
// 64 KB page image, so the HDMA walker can be graded against the oracle's scanline trace.
void CtSeqSetOverlayImage(const unsigned char* page7E);

// One scanline's colour-math state, the fields ct.exe --hdma prints in its scanline trace.
typedef struct CtSeqLineMath {
  uint8_t cgadsub, half, sub, fr, fg, fb, tm, ts, mode;
} CtSeqLineMath;
int CtSeqHdmaTrace(const CtTitleIntro* t, CtSeqLineMath* out, int maxLines);

void CtTitleIntroReset(CtTitleIntro* t);
// Open one entry of the sequence table ($7E:3043, sixteen words, eleven distinct routines;
// disasm/notes/sequence_table.md): the common init at $7E:3063, then `JSR ($3043,X)`. Every entry shares the
// engine (task lists, NMI order, interpreter, drawers, brightness trio, mailbox); a stage brings its setup,
// tasks and graphics. Returns 0 for an entry that is not ported.
// Note: `page` must survive between stages; pass the same machine back in.
int  CtTitleIntroResetStage(CtTitleIntro* t, int stage);
// The frame's joypad, in the $4218 layout C3/0A8E stores at $7E:0002: low byte A $80, X $40, L $20, R $10;
// high byte B $80, Y $40, Select $20, Start $10, then the d-pad. $7E:5B99 leaves on any button; holding L or R
// suppresses $7F0060, so the boot falls into the demo.
void CtTitleIntroInput(CtTitleIntro* t, uint16_t joy);
// Attach the sound mailbox. Without it the exit's two commands are not issued; nothing else changes.
void CtTitleIntroSetSoundSink(CtTitleIntro* t, const CtTitleSoundSink* s);
// The title setup's music ($7E:5AA5 `LDA #$30 / JSR $375F`), issued once before the first NMI (so not a task);
// the rest of $7E:5978 is graphics, which the generator captures. $375F latches the id into both $0389 and
// $038A, which keeps the NMI's watcher (C3/09A4) from loading it again.
void CtTitleIntroSetupSound(CtTitleIntro* t);
// $7E:3177, the overlay's driver reset (`85 | 89 | F2 00 | F2 01 | 81 00 FF`), skipped when `$0384` bit 5 is set: the
// common init runs it at a sequence's opening and the teardown before the reboot.
void CtTitleIntroDriverReset(CtTitleIntro* t, uint8_t seq);
// After the song the setup runs on for 43 frames with the NMI off (a black screen), so the song leads the
// animation by that much. The port starts from the seed state and holds this many frames to keep the lead
// (disasm/notes/title_intro.md sec. 11).
#define CT_TITLE_SETUP_HOLD 43
// A native table entry's SETUP (its $3043 routine) in master cycles, priced into the entry's opening with the overlay's
// decompression (field.c, seqHold); 0 for an entry the port does not run natively. Each entry carries its own measurement.
long CtTitleIntroStageSetupCycles(int stage);
// Advance one NMI. Returns 1 while the sequence is still running -- 0 once its fade-out task has
// walked the brightness to zero and set $7E:0000, which is the frame the stage reboots on.
int  CtTitleIntroTick(CtTitleIntro* t);
int  CtTitleIntroFrames(void);          // how many frames the sequence plays before the demo
// Compose the latched frame into a w*h RGB888 buffer (the engine's 256x224 field frame buffer).
void CtTitleIntroRender(const CtTitleIntro* t, unsigned char* fb, int w, int h);
// The host's options (field.c): the same latched frame drawn `w1` columns wide with the SNES's 256 at `x0` (the widescreen),
// at k x the pixels (the internal resolution), and -- given the previous frame's latch `prev` -- at t/256 of the way
// from it (the frames between): the mode-7 matrix and its scroll are interpolated and the mode-7 layer is sampled at
// the sub-pixel, which is the pendulum's swing; sprites and the tiled modes keep their 1x pixel, and only mode 7 extends
// past the 256 columns (the tiled modes' maps wrap there: black, as a pillarbox). Output: RGB, (w1 * k) x (224 * k).
void CtTitleIntroRenderEx(const CtTitleIntro* t, const struct CtTiLatch* prev, int t256, unsigned char* out,
                          int w1, int x0, int k);

#endif
