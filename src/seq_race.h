// seq_race.h -- the motorcycle race's logic (sequence entry 0), native. See seq_race.c.
#ifndef CT_SEQ_RACE_H
#define CT_SEQ_RACE_H
#include <stdint.h>
#include "seq_pic.h"   // CtSeqStagePic: the stage's picture (stage 2), when the host draws it

// What the race sends to the sound processor and what it reads from the ROM:
//   `sfx`  = a mailbox command through $7E:377E ($1E00-$1E02, already written to WRAM by the caller),
//   `apu`  = the engine's pitch, written straight to the ports ($2141 = pitch, $2140 = $89),
//   `post` = a script's `F6` (C3/0BD4): $1E00-$1E03 are already in WRAM; the host runs C7/0004 on them,
//   `rom`  = the ROM image (HiROM file order), for the overlay's RNG table $C0:FE00 ($7E:3448),
//   `port` = what the CPU reads from the sound processor's output port n ($2140 + n) -- a script's `F5` (C3/0C2A) waits on
//            port 2's low nibble, the song's own marker,
//   `ppu`  = a script's `FB` (C3/0C5F): the byte `v` into $21`reg` -- the picture's (stage 2); NULL drops it,
//   `field`= the PPU's field bit ($213F bit 7: it flips at every frame's start) as the stage's NMI reads it this frame,
//   `postFrames` = the frames the last `post` kept the CPU in the driver's handshake (the SPC's time; 0 if unknown).
// Any of them may be NULL (`port` NULL: the marker never comes, and a script that waits on it holds there).
typedef struct {
  void* ctx;
  void (*sfx)(void* ctx, uint8_t cmd, uint8_t p1, uint8_t p2);
  void (*apu)(void* ctx, uint8_t pitch);
  void (*post)(void* ctx);
  const uint8_t* rom;
  long romLen;
  uint8_t (*port)(void* ctx, int n);
  void (*ppu)(void* ctx, uint8_t reg, uint8_t v);
  int (*field)(void* ctx);
  int (*postFrames)(void* ctx);
  CtSeqStagePic* pic;        // the stage's picture, filled natively (stage 2); NULL: no picture, the logic alone
} CtRaceIo;

// ---- THE $C3 STAGE'S SHARED MACHINE (every entry: the overlay, the common init, the interpreter, the NMI, the fades) ----

// A script's `F0` (C3/0C51: `$7E:00F0` = the target, JSL $7E:37F3) -- the entry's own inline routine. Returns 0, or -1 for a
// target the entry does not port. The interpreter adds the two bytes of the target to the IP afterwards (0BB1 / 0BB5).
typedef int (*CtSeqCall)(uint8_t* w, unsigned page, unsigned target, const CtRaceIo* io);

// One visit of a script task over its page (see CtRaceScript), with the entry's inline calls.
int CtSeqScript(uint8_t* w, unsigned page, const CtRaceIo* io, CtSeqCall call);

// C3:0000 -> $7E:3000 up to the entry's setup: the vectors, `operand` into $0384, the overlay decompressed into $7E:3000
// (C3:1313 by $C3:FE0003), the common init's WRAM ($7E:3063: the shadows, $0000-$00FF and $0900-$1DFF cleared, the driver
// reset $3177 unless $0384 bit 5, the OAM shadow, $13 = $0900). Returns -1 without the ROM.
int  CtSeqStageBegin(uint8_t* w, uint8_t operand, const CtRaceIo* io);

// A task the NMI runs: `slot` = its offset in the list, `task` = its address. Returns what the slot holds afterwards: `task`
// (it stays), 0 (it removed itself, $39A7) or another address (it replaced itself, $39B1).
typedef unsigned (*CtSeqTaskFn)(uint8_t* w, unsigned slot, unsigned task, const CtRaceIo* io);

// C3/08D0 -- one NMI of the stage, its logic (INC $08, the snapshot, the pad and the pause, the tasks through `run`).
// Returns 1 once the script ENDed ($7E:0000 != 0), 2 on the soft reset, else 0.
int  CtSeqStageNmi(uint8_t* w, uint16_t joy, const CtRaceIo* io, CtSeqTaskFn run);
// The same with the vblank hooks first (C3/08F7 JSL $7E:39BE, before `INC $08`: the LIVE list $0940, eight slots, each hook
// able to remove itself -- $39DE -- or replace itself -- $39E8): `hook` returns what its slot holds afterwards, as `run`.
int  CtSeqStageNmiHooks(uint8_t* w, uint16_t joy, const CtRaceIo* io, CtSeqTaskFn hook, CtSeqTaskFn run);

// C3/09A4 -- the stage NMI's song check, after C3/08D0 (the real NMI only: C3/08B3 `JSL C308D0 / JSR $09A4`; the setup's own
// `JSL C3/08D0` skips it): with $0388 clear and $0384 bit 5 clear, a script's new $038A (!= $0389) is played -- `$0389 =
// $038A`, the mailbox `10 <song> FF FF`, `JSL C7/0004` (io->post). The end of `FF 2E` asks for its song this way. Returns
// the frames the upload's handshake holds the NMI (io->postFrames): $0388 stays 1 through them (09D5's STZ comes after the
// JSL) and no NMI of the stage runs -- the caller holds them and clears $0388 at their end. 0 = no song posted.
int  CtSeqStageSong(uint8_t* w, const CtRaceIo* io);
// ...and the frames after it: C3/09A4's `JSL C7/0004` sits inside the NMI, but the NMI is not masked -- the next ones come
// in over it and run the whole stage (C3/08D0: the tasks, the scripts), while `$0388` stays set (09A6's guard skips another
// post) until the JSL returns and 09D5 clears it. `hold` = the frames left (0 = none); returns the new count. Call once a
// frame, after the frame's NMI. MEASURED, the rule's cartridge (FF 07, song 3A: `$0388` 01 from f683 to f696, `$0008` and
// page A's counters running every frame between).
int  CtSeqStageSongTick(uint8_t* w, const CtRaceIo* io, int hold);

// $7E:302A -- the teardown after the END: the driver reset ($3177, skipped for $0384 bit 5), then `JML $C0:0000`.
void CtSeqStageEnd(uint8_t* w, const CtRaceIo* io);

// The overlay's shared brightness tasks (every entry): $7E:347A fades in ($038D up to $0F), $7E:3495 fades out ($038D down
// to 0), $7E:34E3 fades out and ENDs ($7E:0000 += 1) -- one step every $038E frames ($038F counts). CtSeqTaskFn's return.
unsigned CtSeqTaskFade(uint8_t* w, unsigned slot, unsigned task, const CtRaceIo* io);

// $7E:3465 -- the sine of an angle (0-$3FF) from the table at $C0:F900 (a byte each), negative below $200: `AND #03FF / TAX /
// LDA $C0F900,X / AND #00FF / CPX #0200 / BCS -> RTS / EOR #FFFF / INC A`. $7E:3461 is the cosine (`CLC / ADC #0100` first).
unsigned CtSeqSine(const CtRaceIo* io, unsigned a);
// $7E:31BF -- the unsigned 16 x 16 product $F0 * $F2 into $F4-$F7 (D = `d`) on the CPU's 8 x 8 multiplier, as the ROM has it
// (M = 16, X = 8), down to its `ROL $F7`: it parks the middle sum's carry in $F7 bit 0 for the high word, and its own carry out
// -- bit 7 of $F8, the byte after -- goes into the next ADC.
void CtSeqMul31BF(uint8_t* w, unsigned d);

// C3/01E4 (C3/000E), THE CURTAIN: one call of the window effect over `$0350-$0357` (D = $0300) with the caller's X (the data
// bank $00: a stage's task). Writes the state and the output half at `$54`:`$52`, `$0386` through C3/02DD (the rule's virtual
// beam). seq_race.c.
void CtSeqCurtain01E4(uint8_t* w, unsigned x, const CtRaceIo* io);
// $7E:424D / $4283 -- the attract's button task and its count to the END (CtSeqTaskFn's return).
unsigned CtSeqTaskAbort(uint8_t* w, unsigned task, const CtRaceIo* io);

// $7E:485D, the race's per-frame task, over the 128 KB WRAM image `w` ($7E at 0, $7F at 0x10000; D = $0000). Returns the
// task that holds this slot afterwards: 0x485D (it stays), 0 (it removed itself, $39A7: Johnny crossed the line first) or
// another task address that replaces it ($39B1: 0x4677, the player crossed first).
unsigned CtRaceStep485D(uint8_t* w, const CtRaceIo* io);

// $7E:4339, the camera's angle (L/R on the story's call) and height (X/A), and the steering masks $00B6/$00B8.
void CtRaceTask4339(uint8_t* w);

// One visit of a $C3 script task -- $7E:3749 (page $0A00) or $7E:3754 (page $0B00): C3/0AFF's interpreter over the page in
// WRAM (IP at +$20, registers +$00..+$1E, the loop counters +$22.., the gosub return +$30, the op at +$F0). Runs to the
// first yield. Returns 0, or -1 if it met an op or a call this port does not have (named on stderr).
int CtRaceScript(uint8_t* w, unsigned page, const CtRaceIo* io);

// $7E:42BD, the pad's three words rotated ($02 -> $06 -> $04 -> $02): $06 = the pad the NMI just read.
void CtRaceTask42BD(uint8_t* w);

// $7E:4F54, the HUD's task -- its logic only: the score's floor at 0 ($5116 -> 51A2). The rest is its picture (stage 2).
void CtRaceTask4F54(uint8_t* w);
int CtRaceTask44CE(uint8_t* w);   // the score counted down after the finish; 0 = done (off the list)

// THE STAGE, its logic (stage 1 of the native port): C3:0000 + $7E:3000 for entry 0/9 up to the main loop (the overlay
// decompressed into $7E:3000, the common init, the setup's race values, page A run once, the first NMI); one NMI a frame
// (`joy` = the $4218 word: A $0080, X $0040, L $0020, R $0010, B $8000, Y $4000, Select $2000, Start $1000, Up $0800, Down
// $0400, Left $0200, Right $0100); and the teardown. Open takes both arms of the setup's `BIT #0040` (the story's `FF 00` /
// `FF 09`, the attract's `FF 60`: its curtain $3D72 and button $424D). Frame returns 1 once the main loop has seen the END
// (`$7E:0000` set and the thread's pass returned), 2 on the soft reset, else 0.
int  CtRaceStageOpen(uint8_t* w, uint8_t operand, const CtRaceIo* io);
long CtRaceStageSetupCycles(uint8_t operand);   // the setup's own cycles, NMI off, by its arm (seq_race.c)
int  CtRaceStageFrame(uint8_t* w, uint16_t joy, const CtRaceIo* io);
void CtRaceStageClose(uint8_t* w, const CtRaceIo* io);

#endif
