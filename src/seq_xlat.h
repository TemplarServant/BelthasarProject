// seq_xlat.h - a stage's PROGRAM translated from the ROM, run over the VENDORED hardware.
//
// User decision 2026-09-14 (disasm/notes/sequence_table.md section 11): the motorcycle race ($C3 sequence entry 0)
// depends on the CPU CLOCK -- the main loop decompresses the next bike frame and the NMI's tasks pick it up only
// once that work has finished (5..15 frames later, by cycle count), and its opening and closing transitions draw
// their scanline edges from C3/02DD, a generator that reads the PPU's H/V counters in the middle of the NMI. A
// clockless port cannot reproduce either. So the stage is ported as its program: tools/gen/xlat.py turns every
// instruction the stage runs into one labelled line of C (src/xlat/*.c), and each line hands the instruction to the
// vendored 65816 core (snes/cpu.c cpu_xlatExec) and its bus to the vendored snes.c/dma.c/ppu.c -- the same hardware
// the SPC700 (src/audio.c) and the stage picture (src/seq_ppu.c) already run on. The clock is the cartridge's by
// construction; what is translated, and checked at run time byte for byte, is WHICH instruction is where and
// where control goes next.
#ifndef CT_SEQ_XLAT_H
#define CT_SEQ_XLAT_H

#include <stdint.h>
#include "snes.h"
#include "cpu.h"

typedef struct CtXlat CtXlat;

// Called at every instruction boundary, before the core decides between an interrupt and the next opcode -- the
// same granularity as one cpu_runOpcode. Return nonzero to stop the run with that value.
typedef int (*CtXlatHook)(CtXlat* x);

struct CtXlat {
  Snes* snes;        // the hardware: clock, PPU, DMA, APU, cartridge
  Cpu*  cpu;         // snes->cpu: the registers, and the opcode bodies
  CtXlatHook hook;
  void* user;
  uint32_t failPc;   // where a CT_XLAT_MISS / BADOP / BADBYTES happened
  uint8_t  failWant, failGot;
  // 1 = an instruction the translation does not have -- an address it has no label for, or RAM code whose bytes are not
  // the ones it was made from -- runs through the SAME core, fetched and executed one at a time (cpu_xlatFetch +
  // cpu_xlatExec: what cpu_runOpcode does), and control goes back to the labels at the next boundary. Each such address
  // is named once on stderr. 0 = stop with CT_XLAT_MISS / CT_XLAT_BADBYTES, which is what a lockstep that grades the
  // translation itself wants. The attract's stages need 1: a button changes how long a frame's work runs, and an NMI
  // can then land while C3/08D0 has $0501 pointed at its bare RTI ($0500 = `JML C3:08C9`) -- a path no trace took.
  int interp;
};

enum {
  CT_XLAT_EXIT     = 2,    // control reached one of the program's exits (the race's `JML $C0:0000`)
  CT_XLAT_MISS     = -1,   // control reached an address the translation does not have
  CT_XLAT_BADOP    = -2,   // the byte at a translated address is not the translated opcode
  CT_XLAT_BADBYTES = -3,   // a translated operand in WRAM is not what the translation was made from
};

int ctXlatMiss(CtXlat* x);
int ctXlatBadOp(CtXlat* x, uint8_t want, uint8_t got);
int ctXlatBadBytes(CtXlat* x, int len, uint8_t b1, uint8_t b2, uint8_t b3);
int ctXlatInterp(CtXlat* x, int boundary);   // 0 = continue at dispatch; else the value to return (see `interp`)
uint8_t ctXlatPeek(const CtXlat* x, uint32_t adr);   // the program's own byte, without touching the bus

// ---- the three statements every translated line is made of ------------------------------------------------------
#define X_PC ((((uint32_t)c->k) << 16) | c->pc)

// The boundary: control must be exactly here (else re-dispatch on the real PC), the host gets its look, and a
// pending interrupt is taken the way cpu_runOpcode takes it -- instead of this instruction.
// Then cpu_runOpcode's g_cpuOpHook (the logical frame's wait detector, snes.c) at the fetch -- and when its valve rolls
// the machine back to the frame's snapshot, the PC is no longer here: re-dispatch on it.
#define X_BOUNDARY(adr) do {                                                              \
    if (X_PC != (uint32_t)(adr)) goto dispatch;                                            \
    if (x->hook) { int r_ = x->hook(x); if (r_) return r_; }                               \
    if (c->intWanted) { cpu_xlatInterrupt(c); goto dispatch; }                             \
    if (g_cpuOpHook) { g_cpuOpHook(c->mem, X_PC); if (X_PC != (uint32_t)(adr)) goto dispatch; } \
  } while (0)

// One instruction out of ROM: fetch its opcode through the bus (that read is timed) and run the core's body.
#define X_EXEC(op) do {                                                                   \
    uint8_t o_ = cpu_xlatFetch(c);                                                         \
    if (o_ != (op)) return ctXlatBadOp(x, (op), o_);                                       \
    cpu_xlatExec(c, o_);                                                                   \
  } while (0)

// One instruction out of WRAM: the operand bytes are checked against the translation before it runs, so a program
// the translation was not made from (a different overlay, an unexpected self-modification) stops instead of
// running under the wrong labels.
#define X_EXEC_RAM(op, len, b1, b2, b3) do {                                              \
    uint32_t p_ = X_PC;                                                                    \
    if (((len) > 1 && ctXlatPeek(x, p_ + 1) != (b1)) || ((len) > 2 && ctXlatPeek(x, p_ + 2) != (b2)) || \
        ((len) > 3 && ctXlatPeek(x, p_ + 3) != (b3)))                                      \
      { if (!x->interp) return ctXlatBadBytes(x, (len), (b1), (b2), (b3));                 \
        int r_ = ctXlatInterp(x, 0); if (r_) return r_; goto dispatch; }                   \
    X_EXEC(op);                                                                            \
  } while (0)

// The translated programs.
int CtXlat_seq_c3(CtXlat* x);    // src/xlat/seq_c3.c -- sequence entry 0 (the race) as the attract runs it
int CtXlat_wipe_c3(CtXlat* x);
int CtXlat_hdma_fd(CtXlat* x);   // src/xlat/hdma_fd.c -- the field's HDMA table builder FD/C2C1 (both sets, every mode handler)   // src/xlat/wipe_c3.c -- the field's window wipe C3/01E4 (+ C3/02DD), called through C3/000E
int CtXlat_owwin_c6(CtXlat* x);  // src/xlat/owwin_c6.c -- the overworld's window object $C6:E241 / $C6:E27E (op 4E), the same C3/01E4 inside
int CtXlat_gwarp_fd(CtXlat* x);  // src/xlat/gwarp_fd.c -- the GATE WARP, op 0xFF 80-83 -> JML $FD:FFF1/FFEB/FFE8/FFE5 (FD/DA00..)

// ---- a translated STAGE inside a host ----------------------------------------------------------------------------
// The host hands over at C3:0000 -- the first instruction of the $C3 sequence engine, reached from the field's
// op-0xFF handler through C0/3FD3 (`JSR $0B4E` shuts the PPU, NMI, DMA and HDMA off), `LDX #06FF / TXS / TDC / XBA /
// LDA $DF / JML $C3:0000`. What the stage is built from, each part proved by `ct.exe --xlatcmp` (XLATCMP_SEED):
//   * the host's WRAM, all 128 KB;
//   * the registers that path leaves: A = $00:<op operand>, X = S = $06FF, D = $0100, B = $00, M = 1, X = 0, I = 1;
//     Y and the C/V flags are the host's leftovers, and the stage overwrites them before reading them (a scrambled
//     Y/D/C/V changes stack bytes for 37 frames and no pixel);
//   * a POWER-ON PPU and DMA -- the stage never reads what the field left in either;
//   * the SPC: the host's own (its driver, its song, its timers), borrowed for the stage's duration;
//   * the CLOCK: see CT_XLAT_HANDOFF_* below.
//
// ⚠ THE CLOCK AT THE HANDOFF IS HISTORY THE PORT DOES NOT HAVE. The field reaches C3:0000 right after its own NMI, so
// the beam position there is the field NMI's latency plus its duration, and the stage's cycle-exact outputs follow
// from it: measured on the cartridge (ct.exe --xlatphase), delaying the handoff by 2..1000 master cycles never moves
// a bike frame, but it changes the C3/02DD generator's values (the opening and closing transitions' scanline edges)
// and the odd frame where the NMI, running into the visible lines, has not finished a mode-7 table before HDMA reads
// it. The SPC's phase and the generator's accumulator $0386 reach the same outputs. None of the three repeats on the
// cartridge itself: its three visits to the race in 30000 attract frames start at h=582/594/604, cyc%8 = 2/6/0,
// $0386 = 27/02/5B. So those pixels are not a port property to grade; the clock the stage starts on is the one the
// cartridge had at its FIRST visit from power-on (boot f8314), and every other output is the stage's own.
#define CT_XLAT_HANDOFF_V     239      // vPos at C3:0000, boot f8314 (ct.exe --xlatentries boot)
#define CT_XLAT_HANDOFF_H     590      // hPos
#define CT_XLAT_HANDOFF_CYC8  6        // master cycles mod 8 -- what snes_syncCycles aligns DMA on
#define CT_XLAT_HANDOFF_EVEN  1        // ppu->evenFrame (an odd frame's line 240 is four cycles short)
#define CT_XLAT_HANDOFF_WMADD 0x0F200  // $2181-$2183, the WRAM port as the field left it
#define CT_XLAT_HANDOFF_BUS   0xC3     // open bus: the last byte fetched, the bank of `JML $C3:0000`
// THE GATE WARP's handoff (op 0xFF $80-$8F: C0/3FE4-3FFC `JML $FD:FFF1..FFE5`, gate_warp_fd_family.md sec. 9), the same
// kind of history -- taken, like the $C3 entries', at the game's FIRST visit: scene 8's Telepod, the first warp of a play
// (saves/field_re/scene8_gate_warp_f2300.state, `ct.exe --xlatentries` at FD:FFF1: f17).
#define CT_XLAT_GWARP_V       239
#define CT_XLAT_GWARP_H       646
#define CT_XLAT_GWARP_CYC8    2
#define CT_XLAT_GWARP_EVEN    0
#define CT_XLAT_GWARP_WMADD   0x007E0
#define CT_XLAT_GWARP_BUS     0xFD     // the bank of `JML $FD:FFF1`

// ---- a translated ROUTINE inside the field's frame: the window wipe --------------------------------------------------
// C0/00B1 `JSR C0/21E1` -> C0/21EA `JSR ($21EE,X)` with X = 4 (state 3) -> C0/22D8 -> C0/22FA `JSL C3/000E` -> C3/01E4,
// once per field frame while a wipe runs (field_window_wipe.md). The routine's only clock-dependent input is C3/02DD's
// read of the H/V counters ($2137 latch, $213C, $213D twice): the CPU's position in the frame when the field's main loop
// reaches C0/22FA. MEASURED (--hookpc C302DD, hPos added): V is constant over a wipe's 32 calls (255 on the attract's 16E,
// 253 on its 01C) and H is NOT (298..396 master cycles into the line on 16E, 1100..1284 on 01C) -- the field NMI's and
// the main loop head's work differ from frame to frame, and the port has no instruction-level clock for the field
// engine. So, as the race does (CT_XLAT_HANDOFF_*), every call starts on the clock the cartridge had at its FIRST call
// from power-on: the attract's 16E, f579. What follows from it (which of the 256 walks through $C0:FE00 each frame takes,
// so which lines lead) is the cartridge's own on that one call only; the walk's law, the fill, the saturation, the 32
// calls and $0386's update are exact by construction. ⚠ With the beam held, C3/02DD maps $0386 onto a FIXED POINT (7E)
// within a few calls, and from there every call takes one walk -- the curtain opens in rectangles where the cartridge's
// edge moves on every line. The cartridge's H is carried from power-on (a stall before the 01C load still moves its
// wipe), so no per-call H is derivable short of a cycle-exact machine; disasm/notes/field_window_wipe.md, "The curtain
// turns SQUARE".
#define CT_XLAT_WIPE_V   254     // vPos at C3/000E, 16E's first call (saves/attract_cena_inicial.state f577 + PEEK_SAVE; `XLATCMP_AT=C3000E ct.exe --xlatcmp`)
#define CT_XLAT_WIPE_H   1240    // hPos there (C3/02DD's reads then fall on v=255, h=306; `--hookpc C302DD` measures those)
// Run C3/000E on the host's 128 KB WRAM with the registers C0/22FA leaves (A = 00FF from `LDA #FF / STA $0BDD`, X = 4,
// D = $0100, B = 0, M = 1, X = 0, I = 0, S = $06F8 with the JSL's return C0:22FD on the stack) until its RTL reaches
// C0:22FE; the WRAM comes back with what the routine wrote (the tables at $7F:17C8, $0300's page, $0386). 0 = ran, < 0 =
// no ROM / a CT_XLAT_* stop (the WRAM is then left as it was).
int CtXlatWipeCall(const uint8_t* rom, long romLen, uint8_t* wram128, int vPos, int hPos);
int CtXlatWipePeek(const uint8_t* rom, long romLen, uint8_t* wram128, int vPos, int hPos);   // the same, kept out of the seed trace

// ---- the field's HDMA tables: FD/C2C1 ------------------------------------------------------------------------------
// `JSL FD/C2C1` -- nine callers (C0/00C7 the main loop's, C0/00E0, C0/01AA, C0/0AFF's two -- the field NMI routine that
// D1/F4C0 also runs --, C0/0B2E, C0/1905, C0/194D, C0/19B6), every one with D = $0100, B = 0, M = X = 1: `LDA $53 / BIT #01`
// picks the set ($0153 bit 0 clear -> set B, $7F:1238-14F0, through ($C2E5,X) by the mode $0126; set -> set A, $7F:0F80-11E1,
// through ($C2DF,X)), the handler writes that set's eight channel tables and their indirect data ($7F:14F0..), and C2CE /
// C2DA flips the bit. FD/C1EE (the NMI's) then programs the channels at the set the bit names. Run on the host's 128 KB
// WRAM until its RTL; it touches WRAM only (the translation's stores: bank $7F's tables and data, $1D27-$1D57, $01D9/$01DB,
// $1DFB). 0 = ran, < 0 = no ROM / a CT_XLAT_* stop (the WRAM is then left as it was).
int CtXlatHdmaBuild(const uint8_t* rom, long romLen, uint8_t* wram128);

// ---- the OVERWORLD's window object: op 4E -> $C6:E241 (install + first call) / $C6:E27E (a call a frame) ------------
// C2/181C `LDY #0001 / LDA [$58],Y / STA $5B / SEP #20 / LDY #0003 / LDA [$58],Y / STA $5D / JSL C2/1836 (JML [$005B])`, the
// object VM's far call, reached from its dispatcher with X = op << 1 ($9C) and the world's D = 0, B = 0, I = 1. The routine
// programs HDMA channel 2 over $2127 (WH1) from the table at $7E:8A42, runs C3/01E4 (through C3/000E) on the object's
// +$22 mode and +$24 table, and points the table's two entries at the half C3/01E4 wrote ($7E:8A25 / $8A27, which the NMI's
// C2/02C2 copies into the entries every frame). Measured on the attract's 1F5 (ct.exe --calldump build/_ow_f100.state 1
// C6E241 C21830): A = E2C6, X = 009C, Y = 0003, S = 06E7, D = 0000, B = 00, M = 1, I = 1, v = 45, h = 768, latched = 1.
// The beam is the world VM's position in the frame -- line 45 on the first call, 0 / 1 / 258 / 261 on the per-frame
// ones -- the same underived input as the field's (CT_XLAT_WIPE_*): every call starts on the first call's.
#define CT_XLAT_OWWIN_V  45
#define CT_XLAT_OWWIN_H  768
// `hSecond`: the $213C latch toggle at the call -- -1 = as the last call left it (the world engine never reads the
// counter ports itself), 0/1 = force (the install, or a harness feeding a measured state).
// `ppu2Bus`: PPU2 open bus at the call (the last byte a $213x read returned; the high-bit form of a $213C/$213D read keeps
// its bits 1-7), -1 = as the last call left it.
int CtXlatOwWindowCall(const uint8_t* rom, long romLen, uint8_t* wram128, uint32_t target, int vPos, int hPos, int hSecond, int ppu2Bus);


typedef struct CtXlatStage CtXlatStage;
struct Apu;
// Does a translated program exist for this $0384? (the byte the op-0xFF operand writes)
int CtXlatStageHas(uint8_t seqId);
// Build the stage's hardware from the host (see above). `hostApu` may be NULL (a silent host): the stage then runs
// on a power-on SPC, and the $1E05 latch in the host's WRAM decides whether the program talks to it at all.
CtXlatStage* CtXlatStageOpen(const uint8_t* rom, long romLen, const uint8_t* wram128, uint8_t seqId,
                             struct Apu* hostApu, uint16_t hostY);
// Run to the start of the next vblank -- the moment the frame's picture is complete. 0 = running, 1 = the program
// handed control back (`JML $C0:0000`), < 0 = a CT_XLAT_* failure (the stage then stops).
int  CtXlatStageFrame(CtXlatStage* st);
// Run the stage by LOGICAL frames with its NMI-off stretches at phase 0 (CLAUDE.md's exception; snes.c's rule, the
// oracle's CT_ORACLE_LOGICAL): the field host sets it. ct.exe --xlatcmp does not -- it locksteps the stage against a
// real-time oracle in the same process, and the rule's state is the process's.
void CtXlatStageSetRule(CtXlatStage* st, int on);
Snes* CtXlatStageHardware(CtXlatStage* st);
// The frame as the PPU drew it, lines 1..224, into a w x h RGB888 buffer.
void CtXlatStagePicture(const CtXlatStage* st, unsigned char* rgb, int w, int h);
// The stage's WRAM, 128 KB ($7E then $7F), for the host to take back.
const uint8_t* CtXlatStageWram(const CtXlatStage* st);
long CtXlatStageFrames(const CtXlatStage* st);
uint32_t CtXlatStageFailPc(const CtXlatStage* st);
void CtXlatStageFailBytes(const CtXlatStage* st, uint8_t* want, uint8_t* got);   // a CT_XLAT_BADBYTES stop's first differing byte
void CtXlatStageClose(CtXlatStage* st);

#endif
