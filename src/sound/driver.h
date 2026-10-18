// ============================================================================
// sound/driver.h -- the cartridge's sound driver (Square's "Akao", block 0 of the $C7 upload, ARAM $0200-$1DEE) as
// native C game code: the tick (the shadow flush, the status ports, the fades), the sequencer with its tempo
// accumulators, the 60 sequence events $C4-$FF and their look-ahead, the per-track and global slides, the per-voice
// vibrato / tremolo / pan sweep and the volume and pitch output, the command queue and the commands.
//
// Derived routine by routine from our own listing (`python tools/spc700dis.py driver --follow 0200 --table
// 1554:60,1608:60,1680:16,16A0:16,16C0:32,1544:8`); the map and each routine's reading are in
// disasm/notes/audio_spc_driver.md. Validated tick by tick against the original running on the vendored SPC700
// (src/sound/lockstep.c): the S-DSP writes, the status ports and the driver's RAM, identical on every tick.
//
// THE STATE. The driver's state lives in SndDrv::m, laid out as the cartridge driver keeps it in ARAM (the direct
// page, the per-track arrays at $0110-$018F and $F200-$F6FF, the fades and the command queue at $F15F-$F1B1, the
// sample directory and instrument tables at $1E00-$1FFF, the song state saved at $F800-$FF8F), so the lockstep compares
// it byte for byte; driver.c names every field it uses (the D_ / T_ / P_ constants). The same image holds what the
// LOADER put there -- the songs, the samples' directory, the instruments, the effect sequences, and today block 0's own
// tables (the semitone and length tables, the flush list, the operand counts): the driver reads them from wherever the
// loader put them, which is today the ARAM image src/audio.c's uploads fill.
//
// THE OUTSIDE. Everything the driver exchanges with the rest of the machine goes through SndDrvHost:
//   - its output, the S-DSP register writes, through a sink (dspWrite) -- the native mixer implements it;
//   - the few S-DSP registers it reads back (FLG, EDL, ADSR1, and the voices' OUTX in the meter mode);
//   - the commands the CPU posts, taken at the driver's own poll points (poll), and the loop's commands (SndDrvCommand);
//   - the status it puts on the four ports (status: port 0 the command's echo, 1-3 the three status values);
//   - timer 1 (16 ms) for the echo buffer's settle wait, and the control writes (tick rate, timer restart);
//   - the upload session (the host loads the data and calls SndDrvUploadHeader / SndDrvUploadTick).
// The upload's two-processor PROTOCOL (tokens, echoes, the receivers' self-modified copy loops) is not ported: the
// state the receivers build is, through the loader writing SndDrvMem.
// ============================================================================
#ifndef CT_SOUND_DRIVER_H
#define CT_SOUND_DRIVER_H

#include <stdint.h>

// The poll points: the cartridge driver's return address after each `CALL $0892` (the lockstep matches them).
enum {
  SNDDRV_POLL_SEQ = 0x03E3,       // the sequencer, after each track's step
  SNDDRV_POLL_VOICE = 0x031F,     // the music voices
  SNDDRV_POLL_SFXA = 0x0343,      // the effect voices 7-6
  SNDDRV_POLL_SFXB = 0x035F,      // the effect voices 5-4
  SNDDRV_POLL_ECHOWAIT = 0x130B,  // the echo buffer's settle wait
};

typedef struct SndDrvHost {
  void* ctx;
  void    (*dspWrite)(void* ctx, uint8_t reg, uint8_t val);   // the S-DSP register sink ($00-$7F)
  uint8_t (*dspRead)(void* ctx, uint8_t reg);                  // an S-DSP register read back
  // A command waiting on the ports: fills cmd[0..3] (cmd[0] = 0: none). The driver decides itself whether it queues it.
  void    (*poll)(void* ctx, int site, uint8_t cmd[4]);
  void    (*status)(void* ctx, int port, uint8_t val);          // a value put on port 0-3
  uint8_t (*timer1)(void* ctx);                                 // timer 1's 4-bit counter, read (and so cleared)
  void    (*control)(void* ctx, uint8_t reg, uint8_t val);      // $F1 (timers, port clear), $FA/$FB (timer targets)
  void    (*upload)(void* ctx);                                  // the upload session ($100E): returns at its end
} SndDrvHost;

typedef struct SndDrv {
  uint8_t m[0x10000];   // the driver's state and the loader's data, in the cartridge driver's ARAM layout
  SndDrvHost host;
  int alive;            // 0 after command F0 handed the SPC back to its IPL (the host reboots it)
} SndDrv;

void     SndDrvInit(SndDrv* d, const SndDrvHost* host);   // m is left as it is (the loader fills it)
uint8_t* SndDrvMem(SndDrv* d);
void     SndDrvBoot(SndDrv* d);                       // $0200-$0298: the driver starts
void     SndDrvCommand(SndDrv* d, const uint8_t cmd[4]);   // a command the main loop took ($0B96)
void     SndDrvTick(SndDrv* d);                       // one timer-0 tick ($02A0-$0374)
// Inside the upload session: each block's header ($1029-$1056; kind $E0 = the session's end marker). Returns the
// receiver the block goes to (0, 4, 6 = discarded; 1, 2, 3, 5 = stored; 7 = moved), or -1 for the end marker.
int      SndDrvUploadHeader(SndDrv* d, uint8_t kind, uint16_t dest);
void     SndDrvUploadTick(SndDrv* d);                 // $0415: the tick kind-5 blocks keep the music playing with
void     SndDrvUploadReset(SndDrv* d);                // a block's $FF/$F0 marker: the driver hands the SPC to its IPL


#endif
