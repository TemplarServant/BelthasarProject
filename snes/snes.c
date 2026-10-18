
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "snes.h"
#include "cpu.h"
#include "apu.h"
#include "spc.h"
#include "dma.h"
#include "ppu.h"
#include "cart.h"
#include "input.h"
#include "statehandler.h"

static const double apuCyclesPerMaster = (32040 * 32) / (1364 * 262 * 60.0);
static const double apuCyclesPerMasterPal = (32040 * 32) / (1364 * 312 * 50.0);

static void snes_runCycle(Snes* snes);
static void snes_catchupApu(Snes* snes);
static void snes_doAutoJoypad(Snes* snes);
static uint8_t snes_readReg(Snes* snes, uint16_t adr);
static void snes_writeReg(Snes* snes, uint16_t adr, uint8_t val);
static uint8_t snes_rread(Snes* snes, uint32_t adr); // wrapped by read, to set open bus
static int snes_getAccessTime(Snes* snes, uint32_t adr);

Snes* snes_init(void) {
  Snes* snes = malloc(sizeof(Snes));
  snes->cpu = cpu_init(snes, snes_cpuRead, snes_cpuWrite, snes_cpuIdle);
  snes->apu = apu_init(snes);
  snes->dma = dma_init(snes);
  snes->ppu = ppu_init(snes);
  snes->cart = cart_init(snes);
  snes->input1 = input_init(snes);
  snes->input2 = input_init(snes);
  snes->palTiming = false;
  return snes;
}

void snes_free(Snes* snes) {
  cpu_free(snes->cpu);
  apu_free(snes->apu);
  dma_free(snes->dma);
  ppu_free(snes->ppu);
  cart_free(snes->cart);
  input_free(snes->input1);
  input_free(snes->input2);
  free(snes);
}

void snes_reset(Snes* snes, bool hard) {
  cpu_reset(snes->cpu, hard);
  apu_reset(snes->apu);
  dma_reset(snes->dma);
  ppu_reset(snes->ppu);
  input_reset(snes->input1);
  input_reset(snes->input2);
  cart_reset(snes->cart);
  if(hard) memset(snes->ram, 0, sizeof(snes->ram));
  snes->ramAdr = 0;
  snes->hPos = 0;
  snes->vPos = 0;
  snes->frames = 0;
  snes->cycles = 0;
  snes->syncCycle = 0;
  snes->apuCatchupCycles = 0.0;
  snes->hIrqEnabled = false;
  snes->vIrqEnabled = false;
  snes->nmiEnabled = false;
  snes->hTimer = 0x1ff;
  snes->vTimer = 0x1ff;
  snes->inNmi = false;
  snes->irqCondition = false;
  snes->inIrq = false;
  snes->inVblank = false;
  memset(snes->portAutoRead, 0, sizeof(snes->portAutoRead));
  snes->autoJoyRead = false;
  snes->autoJoyTimer = 0;
  snes->ppuLatch = false;
  snes->multiplyA = 0xff;
  snes->multiplyResult = 0xfe01;
  snes->divideA = 0xffff;
  snes->divideResult = 0x101;
  snes->fastMem = false;
  snes->openBus = 0;
}

void snes_handleState(Snes* snes, StateHandler* sh) {
  sh_handleBools(sh,
    &snes->palTiming, &snes->hIrqEnabled, &snes->vIrqEnabled, &snes->nmiEnabled, &snes->inNmi, &snes->irqCondition,
    &snes->inIrq, &snes->inVblank, &snes->autoJoyRead, &snes->ppuLatch, &snes->fastMem, NULL
  );
  sh_handleBytes(sh, &snes->multiplyA, &snes->openBus, NULL);
  sh_handleWords(sh,
    &snes->hPos, &snes->vPos, &snes->hTimer, &snes->vTimer,
    &snes->portAutoRead[0], &snes->portAutoRead[1], &snes->portAutoRead[2], &snes->portAutoRead[3],
    &snes->autoJoyTimer, &snes->multiplyResult, &snes->divideA, &snes->divideResult, NULL
  );
  sh_handleInts(sh, &snes->ramAdr, &snes->frames, NULL);
  sh_handleLongLongs(sh, &snes->cycles, &snes->syncCycle, NULL);
  sh_handleDoubles(sh, &snes->apuCatchupCycles, NULL);
  sh_handleByteArray(sh, snes->ram, 0x20000);
  // components
  cpu_handleState(snes->cpu, sh);
  dma_handleState(snes->dma, sh);
  ppu_handleState(snes->ppu, sh);
  apu_handleState(snes->apu, sh);
  input_handleState(snes->input1, sh);
  input_handleState(snes->input2, sh);
  cart_handleState(snes->cart, sh);
}

static unsigned g_runFrameCalls = 0;   // (the harness frame, for the traces below)
void snes_runFrame(Snes* snes) {
  // TODO: improve handling of dma's that take up entire vblank / frame
  // run until we are starting a new frame (leaving vblank)
  while(snes->inVblank) {
    cpu_runOpcode(snes->cpu);
  }
  // then run until we are at vblank, or we end up at next frame (DMA caused vblank to be skipped)
  uint32_t frame = snes->frames;
  while(!snes->inVblank && frame == snes->frames) {
    cpu_runOpcode(snes->cpu);
  }
  { static int tr = -1; unsigned calls = ++g_runFrameCalls;   // CT_ORACLE_RUNFRAME_TRACE=1: a call that ran over a frame boundary --
    if(tr < 0) tr = getenv("CT_ORACLE_RUNFRAME_TRACE") != NULL;   // one opcode (a long DMA) past both the vblank and line 0:
    if(tr && snes->frames != frame)                           // one picture for two frames (the TODO above)
      fprintf(stderr, "runframe: call %u ran frames %u-%u (to V=%d) at %02X:%04X\n", calls, frame, snes->frames, snes->vPos,
              snes->cpu->k, snes->cpu->pc); }
  snes_catchupApu(snes); // catch up the apu after running
}

// CT probe: every master cycle the oracle runs lands in ONE of these buckets, chosen by the caller
// (g_cycTag: 0 = CPU outside an interrupt, 1 = CPU inside the NMI/IRQ handler -- set by the harness
// per opcode, 2 = general DMA (the $420B transfer, CPU halted), 3 = HDMA (init + per-line runs),
// 4 = DRAM refresh, the 40 cycles a line). The harness reads the buckets per frame to split a
// frame's 357,366 cycles into 'what the program got' and 'what the background took' -- the
// quantity a clockless port has to DERIVE before it can price a synchronous upload.
int g_cycTag = 0;
long g_cycTagCycles[8];
void snes_runCycles(Snes* snes, int cycles) {
  if(snes->hPos + cycles >= 536 && snes->hPos < 536) {
    // if we go past 536, add 40 cycles for dram refersh
    cycles += 40;
    g_cycTagCycles[4] += 40; g_cycTagCycles[g_cycTag & 7] += cycles - 40;
  } else g_cycTagCycles[g_cycTag & 7] += cycles;
  for(int i = 0; i < cycles; i += 2) {
    snes_runCycle(snes);
  }
}

void snes_syncCycles(Snes* snes, bool start, int syncCycles) {
  if(start) {
    snes->syncCycle = snes->cycles;
    int count = syncCycles - (snes->cycles % syncCycles);
    snes_runCycles(snes, count);
  } else {
    int count = syncCycles - ((snes->cycles - snes->syncCycle) % syncCycles);
    snes_runCycles(snes, count);
  }
}

// THE LOGICAL FRAME -- CLAUDE.md's one exception (the zelda3 model, user decision 2026-09-18). snesrev/zelda3 verifies its C
// against the original by running ONE main-loop iteration and then the NMI per frame (zelda_cpu_infra.c
// RunOrigAsmCodeOneLoop), so an iteration whose CPU work overruns the NMI -- a LAG frame -- never happens there, on either
// side. This is that, on a real-time machine: with g_ctLogicalFrames set (the oracle's CT_ORACLE_LOGICAL=1) the beam is
// HELD at the last dot of line 224 -- the step into 225 that starts vblank and raises the NMI -- while the CPU is not in a
// wait loop. The CPU keeps executing (its accesses still call snes_runCycle, which returns at once), the beam, HDMA and
// the timers stand still (the APU runs on with the CPU), and the iteration ends inside its own frame. THE WAIT DETECTOR needs no list of wait
// loops: the CPU is waiting when it has come back to an IDENTICAL state -- PC, A, X, Y, S, P, D, DB, emulation bit --
// with no memory write in between except to the stack's page: from there only something outside the CPU (an interrupt,
// DMA, the beam) can change its course. C0/EC71 `LDA $0152 / BNE`, the battle's C1/0080 -> CD/0036 -> CD/04A6 -> CD/3E44
// chain and CD/3E7D qualify; a work loop does not, even one that writes nothing (C1/283D walks X over the eleven
// combatants -- an earlier 'no writes, few PCs' test took it for a wait, released the hold mid-work and made the very
// lag frame this mode exists to remove). WAI counts as waiting.
// THE HOLD IS TENTATIVE. A load that spans frames by design (a battle's boot, a field's decompression with the NMI on)
// is not an overrun, and the port prices those in real frames. So at the last instruction boundary of line 224 the
// machine is snapshot (snes_saveState, once a frame, only when the CPU is not already waiting); if the CPU reaches a wait
// within LF_VALVE cycles -- one frame of CPU time; the attract's X-Strike needs 307,532 at most -- the hold stands, and
// if not, the snapshot is restored at the next instruction boundary and that frame runs in real time. The hold only
// runs with the NMI enabled (no NMI, no frame to protect). g_ctLogicalHeld / Holds / Valves count what it did (Valves =
// rollbacks; a valve with no snapshot to go back to is reported as such and simply released).
#define LF_N 64
#define LF_VALVE 357368L
#define LF_SNAPWIN 256
int g_ctLogicalFrames = 0;
long g_ctLogicalHeld = 0, g_ctLogicalHolds = 0, g_ctLogicalValves = 0;
typedef struct { uint32_t pc; uint16_t a, x, y, sp, dp; uint8_t db, p, e, wrote, apu; } LfState;
// A POLL OF A BUSY APU IS A WAIT (the rule's text: an APU handshake is a frame by design). C7/0A5A's command loop meets a
// busy SPC with a counted poll -- `LDX #0400 / CMP $2140 / BEQ / DEX / BNE` -- whose X changes every round, so the ring's
// identical-state test never matched it and the rule held ~1.26M cycles of waiting on the SPC as if it were CPU work (the
// new game's reboot, loc 1B1's `EA 00`: 18,673 polls). Under the rule, a return to the same PC with no write in between and
// an APU port read in the round is a wait too. A handshake the SPC answers at once never loops: it stays CPU work.
// MEASURED (tools/naming_cmp.py bootfast, CT_ORACLE_NMION_HOLD=1): the naming's park comes at f217 as on the valve (f215
// when the poll was held as CPU work -- two frames of the SPC's own time lost).
static int lfApuNow = 0;
static LfState lfRing[LF_N];
static int lfPos = 0, lfFill = 0, lfWroteNow = 0;
static long lfHoldNow = 0;
static uint8_t* lfSnap = NULL; static int lfSnapSize = 0;     // the tentative hold's snapshot
static uint32_t lfSnapFrame = 0xffffffffu, lfNoHoldFrame = 0xffffffffu;
static int lfRollbackPending = 0;
static LfState lfRingSnap[LF_N]; static int lfPosSnap = 0, lfFillSnap = 0;
static bool lfInWaitLoop(Snes* snes);
static bool lfWaitLoopRange(Snes* snes, uint32_t* lo, uint32_t* hi);
static int lfIfast = -1, lfWaitingNow = 0;             // the infinitely fast CPU (below, lfIntHook)
static uint32_t lfWaitLo = 0, lfWaitHi = 0;
long g_ctIfastHeld = 0; static long lfIfastRun = 0; static long lfVbIfast = 0;
// NMI-ON WORK NEVER COSTS FRAMES (user decision 2026-09-30, FIXED -- CLAUDE.md; the zelda3 literal, completing the
// 2026-09-18 rule). CPU work done with the NMI enabled between two waits ends inside its frame, however long -- a heavy
// battle iteration, the battle's boot, a menu's load, the effect load's block build: the step into 225 is held until the
// CPU waits, with no one-frame valve and no rollback to real time. The stretch's own CPU cycles are counted for the traces
// only (lfStretch: every instruction boundary adds the cycles since the last one, less the refresh bucket's and HDMA's
// growth, when no interrupt handler runs -- g_cpuIntHook pushes the SP an interrupt came from, the SP coming back to it
// after RTI pops it; a wait of the main thread, looked at when the PC goes back, ends it; CT_ORACLE_LOGICAL_TRACE prints
// `stretch-end` for every one past LF_FRAME / 4). LF_SAFETY is not the rule: a hold past it means the wait detector missed
// a wait -- released, and said loudly. MIGRATION: the rule is on with CT_ORACLE_NMION_HOLD=1; without it the old
// one-frame valve stands (g_ctLogicalValve) until the port charges no frames anywhere.
#define LF_FRAME 357368L
#define LF_SAFETY (60L * LF_FRAME)
int g_ctLogicalValve = 1;
static void lfReadEnv(void) {
  static int done = 0; if(done) return; done = 1;
  const char* v = getenv("CT_ORACLE_NMION_HOLD"); g_ctLogicalValve = (v && *v == '0');
  // UNDER THE RULE THE APU STANDS WITH THE BEAM (the zelda3 model: the SPC runs a frame per frame whatever the CPU spent --
  // the port's own relation, Belthasar's g_ctLogicalHoldApu = 0). The oracle ran it through a hold for the valve's sake (a
  // handshake inside a held stretch had to be answered before the valve fired); with no valve, a poll of the APU is a wait
  // (lfInWaitLoop: the plain poll and the counted one) and ends the stretch like any other. Held cycles were SPC time no
  // frame of the port gives: the line-64 cut alone holds ~20 lines of every action frame. CT_ORACLE_HOLDAPU=1 keeps the
  // old way (to measure what it moves).
  if(!g_ctLogicalValve) { const char* a = getenv("CT_ORACLE_HOLDAPU"); if(!(a && *a == '1')) g_ctLogicalHoldApu = 0; }
  { const char* f = getenv("CT_ORACLE_IFAST"); lfIfast = (!g_ctLogicalValve && !(f && *f == '0')); }
}
long g_ctLogicalCrossed = 0;
static int lfIntDepth = 0; static uint16_t lfIntSp[16];
// THE MAIN THREAD'S RDNMI UNDER THE RULE (2026-10-08, the user delegated the choice: "make the best decision" (translated from Portuguese); the zelda3
// literal -- "a flag poll races nothing"). Two field exits spin on the vblank flag with the NMI ENABLED: C0/3B71 (ops
// 0xDF/0xE1, `LDA $4210 / BPL` then C0/0B4E and the reboot) and C0/4181 (op FF 96, then JML FD/C000). The step into 225
// raises the flag and the NMI together; the field NMI reads $4210 itself (its ack), so the spin leaves only when its own
// read cycle falls after the flag rose and before the NMI is taken -- a race on the CPU's cycle phase against the beam,
// which the infinitely fast CPU does not remove: the rule's cartridge left 05C 6 frames after obj1's E1 visit and 05B 2
// (disasm/notes/field_caption_plane.md sec. 7), a number of no meaning to a machine with no CPU time. Under the rule the
// race is decided for the poll: a vblank that rises WHILE the main thread polls (its last $4210 read at interrupt depth 0
// came back clear -- lfRdnmiArmed) is seen by its next read even when the handler's ack came first, so the spin leaves on
// the first vblank after it began, on both sides (the port's reboot, at the head of the next iteration, is that). ONLY
// that one: the vblank that started the visit's own iteration was acked by C0/EA70 before the main thread ran the op, on
// any CPU at any speed, and no poll can see it (the first version handed it over too -- the store in the visit's frame,
// 1 frame early on every exit; oracle_rdnmi_rule.md sec. 4). An interrupt handler's read is the hardware's; a NMI-off
// spin (C0/0B38, FD/EADA) has no handler racing it and reads what it always read. ROM census of every `$4210` read: those
// four spins and acks whose value is discarded.
// THE DEFAULT since 2026-10-09 (CT_ORACLE_RDNMI=0 brings the race back, for measurement only; disasm/notes/
// oracle_rdnmi_rule.md): every DF/E1 exit stores $0100 one frame after its visit (05B f27 -> f28, 05C f547 -> f548, the
// attract's AD/B1/087/1F0 +1 each), the port's own frame.
static int lfRdnmi = -1, lfRdnmiMain = 0, lfRdnmiArmed = 0;
static int lfRdnmiOn(void) {
  if(lfRdnmi < 0) { const char* v = getenv("CT_ORACLE_RDNMI"); lfRdnmi = !(v && *v == '0'); }
  return lfRdnmi && g_ctLogicalFrames && lfIfast > 0;
}
static int lfAcctInit = 0; static unsigned long long lfAcctCycles = 0; static long lfAcctRefresh = 0, lfAcctHdma = 0;
static long lfStretch = 0, lfCrossed = 0; static uint32_t lfPrevPc = 0;
static void lfAccount(Snes* snes) {
  if(!lfAcctInit) { lfAcctInit = 1; lfAcctCycles = snes->cycles; lfAcctRefresh = g_cycTagCycles[4]; lfAcctHdma = g_cycTagCycles[3]; return; }
  long d = (long)(snes->cycles - lfAcctCycles) - (g_cycTagCycles[4] - lfAcctRefresh) - (g_cycTagCycles[3] - lfAcctHdma);
  if(lfIntDepth == 0 && d > 0) lfStretch += d;
  lfAcctCycles = snes->cycles; lfAcctRefresh = g_cycTagCycles[4]; lfAcctHdma = g_cycTagCycles[3];
}
static long lfIntSerial = 0;                               // one per interrupt taken from depth 0 (the line cuts' NMI)
// THE INFINITELY FAST CPU (CT_ORACLE_IFAST=1 with the rule; disasm/notes/oracle_logical_frames.md sec. 7). The zelda3 model
// taken whole -- snesrev's ports run one main-loop iteration and its NMI per frame and then draw the frame, i.e. the CPU
// costs no time at all: here the beam, the timers and the APU advance ONLY while the CPU waits (a loop back to a state it
// was in with nothing written, a counted poll of the APU, WAI), with the NMI enabled; every other cycle is held. It
// generalises the rule's hold at the step into 225 to every beam position, so no CPU work moves anything against the
// beam: a sound handshake costs the frames the SPC needs from where the CPU's last wait left the beam, the main thread's
// table writes land where its last wait left the beam, and a flag poll races nothing. NMI-off work keeps its own rule.
static void lfIntHook(void* mem) {
  Snes* snes = (Snes*) mem;
  lfWaitingNow = 0;                                        // a handler's work is the CPU's, not a wait
  lfAccount(snes);                                         // the interrupted code's cycles up to here
  if(lfIntDepth < 16) lfIntSp[lfIntDepth] = snes->cpu->sp;
  if(lfIntDepth == 0) lfIntSerial++;
  lfIntDepth++;
}
static int lfStretchOn = 0; static uint32_t lfStretchPc = 0, lfStretchFrame = 0;
static void lfStretchEndAt(Snes* snes, uint32_t pc) {       // a wait (or the NMI switched): the stretch ends here
  if(snes && lfStretch > LF_FRAME / 4 && getenv("CT_ORACLE_LOGICAL_TRACE"))
    fprintf(stderr, "logical-frame: stretch-end %06X (frame %u) -> %06X (frame %u) C=%ld crossed=%ld\n", lfStretchPc,
            lfStretchFrame, pc, snes->frames, lfStretch, lfCrossed);
  lfStretch = 0; lfCrossed = 0; lfStretchOn = 0;
}
static void lfStretchEnd(void) { lfStretchEndAt(NULL, 0); }
// THE BATTLE NMI's LINE WAIT (user decision 2026-09-30, option A -- CLAUDE.md). During an action the NMI waits for line 64
// (CD/09AA `LDA #40 / JSR CD/3B82`) and the service after it -- CD/1609's effect program and the NMI's tail -- rewrites the
// per-line records, which HDMA reads with no latch: the cartridge's write line is 64 plus that service's CPU time. Under the
// rule the beam holds from the wait's exit (CD/3B82's RTS at CD/3BAA, V >= 64) until that NMI's RTI, so every write of the
// service lands on line 64 on both sides. MIGRATION: on with CT_ORACLE_LINE64=1 (and the virtual beam).
// THE SERVICE'S OWN RASTER WAITS. The held service can wait for the beam itself: the background program's op C1
// (CD/1D93 `LDA [$40] / JMP CD/3B82`, reached from CD/1654 -> CD/16AF `JSR ($16B5,X)`) is the same routine with the
// program's line. MEASURED, char 4's Frog Squash on gato_wait (CT_ORACLE_RASTER_TRACE=1, no hold): 256 NMIs enter it at
// V=66 with A=80 and leave at V=128. Held, the beam never reaches an HBlank edge and the wait never ends. So the wait is
// the ROM's and the beam runs through it: the hold lets go at CD/3B82's entry and takes hold again at its exit -- a NEW
// CUT at that line (the writes before the wait land on the previous cut, the rest on this one). Each cut keeps bank 7E as
// it stood at the exit: the HDMA runs of the lines between the previous cut and this one read that copy, the runs after
// the last cut read the live WRAM (battle.c CtBattleFxLayerCompose; the port's own cuts, battle.c battleLineCut).
#define LF_LINE_CUTS 4
long g_ctLineHeld = 0; static int lfLineHold = 0, lfLine64 = -1;
uint8_t g_ctLineCutRam[LF_LINE_CUTS][0x10000];             // bank 7E at each cut of the NMI's service
int g_ctLineCutAt[LF_LINE_CUTS], g_ctLineCutN = 0;         // the cuts' lines (V at the wait's exit), in order
long g_ctLineCutSeq = 0;                                   // +1 per NMI whose service made a cut
static long lfLineCutSerial = -1;
extern int g_ctVirtualBeam;
static void lfOpHook(void* mem, uint32_t pc) {
  Snes* snes = (Snes*) mem; Cpu* c = snes->cpu;
  lfAccount(snes);                                         // the instruction that just ended
  while(lfIntDepth > 0 && c->sp == lfIntSp[(lfIntDepth > 16 ? 16 : lfIntDepth) - 1]) lfIntDepth--;   // back from RTI
  if(lfLine64 < 0) { const char* e = getenv("CT_ORACLE_LINE64"); lfLine64 = !(e && *e == '0'); }
  { static int rt = -1; if(rt < 0) { const char* e = getenv("CT_ORACLE_RASTER_TRACE"); rt = (e && *e == '1'); }
    if(rt && (pc == 0xCD3B82 || pc == 0xCD3BAA))
      fprintf(stderr, "raster: f%d %06X A=%02X V=%d H=%d depth=%d hold=%d ret=%02X%02X\n", snes->frames, pc, c->a & 0xFF,
              snes->vPos, snes->hPos, lfIntDepth, lfLineHold, snes->ram[(c->sp + 2) & 0xFFFF], snes->ram[(c->sp + 1) & 0xFFFF]); }
  if(lfLineHold && pc == 0xCD3B82) lfLineHold = 0;        // the service's own raster wait: the beam runs through it
  if(lfLine64 && g_ctVirtualBeam && pc == 0xCD3BAA && lfIntDepth > 0) {                      // the wait's exit: a cut
    if(lfIfast <= 0) lfLineHold = lfIntDepth;              // (the infinitely fast CPU holds the service anyway)
    if(lfLineCutSerial != lfIntSerial) { lfLineCutSerial = lfIntSerial; g_ctLineCutN = 0; g_ctLineCutSeq++; }
    if(g_ctLineCutN < LF_LINE_CUTS) {
      memcpy(g_ctLineCutRam[g_ctLineCutN], snes->ram, 0x10000); g_ctLineCutAt[g_ctLineCutN++] = snes->vPos;
    }
  }
  if(lfRollbackPending) {                                  // the valve fired: this frame runs in real time
    lfRollbackPending = 0;
    snes_loadState(snes, lfSnap, lfSnapSize);
    memcpy(lfRing, lfRingSnap, sizeof lfRing); lfPos = lfPosSnap; lfFill = lfFillSnap; lfWroteNow = 0;
    lfNoHoldFrame = snes->frames; lfHoldNow = 0;
    return;                                                 // cpu_runOpcode fetches at the restored PC
  }
  if(g_ctLogicalValve && snes->nmiEnabled && snes->vPos == 224 && snes->hPos >= 1362 - LF_SNAPWIN && lfSnapFrame != snes->frames &&
     lfNoHoldFrame != snes->frames && !c->waiting) {
    lfSnapFrame = snes->frames;
    if(!lfSnap) { lfSnapSize = snes_saveState(snes, NULL); lfSnap = (uint8_t*) malloc(lfSnapSize + 4096); }
    lfSnapSize = snes_saveState(snes, lfSnap);
    memcpy(lfRingSnap, lfRing, sizeof lfRing); lfPosSnap = lfPos; lfFillSnap = lfFill;
  }
  lfRing[lfPos].wrote = (uint8_t)lfWroteNow;               // the instruction that just ended
  lfRing[lfPos].apu = (uint8_t)lfApuNow; lfApuNow = 0;
  lfPos = (lfPos + 1) % LF_N;
  LfState* s = &lfRing[lfPos];
  s->pc = pc; s->a = c->a; s->x = c->x; s->y = c->y; s->sp = c->sp; s->dp = c->dp; s->db = c->db;
  s->p = (uint8_t)((c->n << 7) | (c->v << 6) | (c->mf << 5) | (c->xf << 4) | (c->d << 3) | (c->i << 2) | (c->z << 1) | c->c);
  s->e = c->e; s->wrote = 0; s->apu = 0; lfWroteNow = 0;
  if(lfFill < LF_N) lfFill++;
  if(lfIfast > 0) {                                        // the infinitely fast CPU's wait flag, any context
    if(lfWaitingNow && (pc < lfWaitLo || pc > lfWaitHi)) lfWaitingNow = 0;   // out of the loop: working again
    if(!lfWaitingNow && (pc <= lfPrevPc || (pc >> 16) != (lfPrevPc >> 16))) {
      uint32_t lo, hi;
      if(lfWaitLoopRange(snes, &lo, &hi)) { lfWaitingNow = 1; lfWaitLo = lo; lfWaitHi = hi; }
    }
  }
  // the main thread back at a state it has been in, with nothing written since: a wait -- the NMI-on stretch ends
  if(lfIntDepth == 0 && (pc <= lfPrevPc || (pc >> 16) != (lfPrevPc >> 16)) && lfInWaitLoop(snes)) lfStretchEndAt(snes, pc);
  else if(lfIntDepth == 0 && !lfStretchOn && lfStretch > 0) { lfStretchOn = 1; lfStretchPc = pc; lfStretchFrame = snes->frames; }
  lfPrevPc = pc;
}
static void lfNoteWrite(Snes* snes, uint32_t adr) {
  uint8_t bank = adr >> 16; uint16_t a = adr & 0xffff;
  bool low = bank == 0x7e || ((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && a < 0x2000);
  bool stack = low && a < 0x2000 && (a & 0xff00) == (snes->cpu->sp & 0xff00);
  if(!stack) { lfWroteNow = 1; lfWaitingNow = 0; }
}
static bool lfSame(const LfState* a, const LfState* b) {
  return a->pc == b->pc && a->a == b->a && a->x == b->x && a->y == b->y && a->sp == b->sp && a->dp == b->dp &&
         a->db == b->db && a->p == b->p && a->e == b->e;
}
static bool lfWaitLoopRange(Snes* snes, uint32_t* lo, uint32_t* hi) {
  if(snes->cpu->waiting) { *lo = *hi = 0; return true; }  // WAI
  if(lfWroteNow) return false;                             // the instruction in progress wrote
  const LfState* now = &lfRing[lfPos];
  int sawApu = 0;
  *lo = *hi = now->pc;
  for(int k = 1; k < lfFill; k++) {                        // walk back: every instruction since the match must not write
    const LfState* s = &lfRing[(lfPos - k + LF_N) % LF_N];
    if(s->wrote) return false;
    sawApu |= s->apu;
    if(s->pc < *lo) *lo = s->pc;
    if(s->pc > *hi) *hi = s->pc;
    if(lfSame(s, now)) return true;
    { static int apuPoll = -1; if(apuPoll < 0) { const char* e = getenv("CT_ORACLE_APUPOLL"); apuPoll = !(e && *e == '0'); }
      if(apuPoll && !g_ctLogicalValve && sawApu && s->pc == now->pc) return true; }   // a counted poll of the APU (above;
                                                                                   // CT_ORACLE_APUPOLL=0 measures without)
  }
  return false;
}
static bool lfInWaitLoop(Snes* snes) { uint32_t lo, hi; return lfWaitLoopRange(snes, &lo, &hi); }
// NMI-OFF LOADS AT PHASE 0 (CLAUDE.md's exception, user decision 2026-09-25). CPU work done with the NMI disabled that
// spans frames crosses 3 or 4 frame boundaries depending on where in its frame it began -- on the CPU work before it,
// which a C port cannot know. With g_ctLogicalLoads, the write that turns the NMI OFF notes the CPU cycles its frame has
// spent since the step into line 225 (the refresh excluded: `cycles` less the refresh bucket's growth), and the next
// step into 225 holds the beam for that many cycles while the CPU runs on -- so the stretch reaches its first vblank
// after one whole frame of CPU work, as if it had begun at the frame's start. Turning the NMI back on before that vblank
// cancels it (the stretch never crossed a boundary).
int g_ctLogicalLoads = 0;
// THE APU IN A HOLD. The oracle runs it on (1): its CPU is the cartridge's, and a sound handshake met inside a held
// stretch must be answered or the frame falls to the valve. A PLAYER of the port cannot: its output is one frame of SPC
// per displayed frame (the console's relation, and the zelda3 model's -- the SPC there runs a frame per frame whatever
// the CPU spent), so every held cycle the APU ran became samples no frame plays, queued behind the picture for good:
// the attract's race holds ~35% of a frame per frame and left the sound 1.8 s late for the rest of the demo (measured,
// CT_AUDIO_FIFOTRACE). Belthasar sets 0: the APU stands with the beam through a hold (and a phase hold), and a CPU that
// polls it there is a wait (C7:09E1), which releases the hold like any other.
int g_ctLogicalHoldApu = 1;
static unsigned long long lfVbCycles = 0; static long lfVbRefresh = 0;
static long lfPhaseHold = 0; static int lfPhasePending = 0;
long g_ctLogicalPhaseHolds = 0, g_ctLogicalPhaseHeld = 0;
static unsigned long long lfOffCycles = 0; static long lfOffRefresh = 0;
void snes_noteNmiWrite(Snes* snes, int wasOn, int nowOn) {
  if(g_ctLogicalFrames && wasOn != nowOn) lfStretchEnd();
  if(!g_ctLogicalFrames || !g_ctLogicalLoads) return;
  if(wasOn && !nowOn) {
    long spent = (long)(snes->cycles - lfVbCycles) - (g_cycTagCycles[4] - lfVbRefresh) - (g_ctIfastHeld - lfVbIfast);
    lfPhaseHold = spent > 0 ? spent : 0; lfPhasePending = lfPhaseHold > 0;
    if(getenv("CT_ORACLE_LOGICAL_TRACE"))
      fprintf(stderr, "logical-frame: frame %u (call %u) NMI off at %02X:%04X, %ld cycles into the frame -> phase hold\n",
              snes->frames, g_runFrameCalls + 1, snes->cpu->k, snes->cpu->pc, lfPhaseHold);
  } else if(!wasOn && nowOn && lfPhasePending) {
    lfPhasePending = 0; lfPhaseHold = 0;
  }
  if(wasOn && !nowOn) { lfOffCycles = snes->cycles; lfOffRefresh = g_cycTagCycles[4]; }
  else if(!wasOn && nowOn && getenv("CT_ORACLE_LOGICAL_TRACE"))   // the stretch's length, for pricing its port twin
    fprintf(stderr, "logical-frame: frame %u (call %u) NMI on at %02X:%04X (V=%d H=%d inNmi=%d), %llu cycles since it went off (%ld refresh)\n",
            snes->frames, g_runFrameCalls + 1, snes->cpu->k, snes->cpu->pc, snes->vPos, snes->hPos, snes->inNmi, snes->cycles - lfOffCycles, g_cycTagCycles[4] - lfOffRefresh);
}
void snes_setLogicalFrames(int on) {
  g_ctLogicalFrames = on;
  g_cpuOpHook = on ? lfOpHook : NULL;
  g_cpuIntHook = on ? lfIntHook : NULL;
  lfReadEnv();
  lfFill = 0; lfPos = 0; lfWroteNow = 0; lfHoldNow = 0;
  lfSnapFrame = lfNoHoldFrame = 0xffffffffu; lfRollbackPending = 0;
  lfIntDepth = 0; lfAcctInit = 0; lfStretchEnd(); lfPrevPc = 0; lfLineHold = 0; lfRdnmiMain = 0; lfRdnmiArmed = 0;
}
// A translated stage (seq_xlat.c CtXlatStageFrame) runs under the same rule as the oracle -- the logical frame and the
// phase-0 NMI-off stretches -- one host frame at a time: this switches the rule on and off around each frame WITHOUT
// resetting its state (the wait ring, the tentative hold's snapshot, a pending phase hold), which belongs to the stage's
// hardware across frames. The stage's machine is the only one that runs under it in Belthasar.
void snes_logicalResume(int on) {
  g_ctLogicalFrames = on; g_ctLogicalLoads = on;
  g_cpuOpHook = on ? lfOpHook : NULL;
  g_cpuIntHook = on ? lfIntHook : NULL;
  lfReadEnv();
  if(on) lfAcctInit = 0;                                   // the cycles between two resumed frames are not the stage's
}
// ...and a fresh machine starts with none of it: a stage's LAST NMI-off write (its exit, 7E:3032) notes a phase hold
// that belongs to the host's next stretch, not to the next stage's hardware.
void snes_logicalFresh(void) {
  lfIntDepth = 0; lfAcctInit = 0; lfStretchEnd(); lfPrevPc = 0; lfLineHold = 0; lfRdnmiMain = 0; lfRdnmiArmed = 0;
  lfFill = 0; lfPos = 0; lfWroteNow = 0; lfHoldNow = 0;
  lfSnapFrame = lfNoHoldFrame = 0xffffffffu; lfRollbackPending = 0;
  lfPhaseHold = 0; lfPhasePending = 0; lfVbCycles = 0; lfVbRefresh = 0;
}

// ...EXCEPT THE CPU's SIDE OF THE APU PROTOCOL (user decision 2026-10-01, CLAUDE.md): while the CPU runs the sound driver's
// own code -- bank C7, entered only by `JSL C7/0004` (59 sites in the code banks, and the title overlay's $7E:3775) and the
// reset's `JSL C7/0000` (C0/0053); a scan finds no other JSL/JML into C7 (its six other hits are operand bytes, e.g.
// C0/4A14 `ADC #2200 / TAY / LDX $C7`) -- its cycles are real on both sides: the beam, the timers and the SPC advance.
// The two-processor transfer is only correct at the CPU's real speed: the driver's receive loop (ARAM $1104-$111B) echoes
// the token at $110E after reading port 1 alone and reads ports 2-3 after the echo, in the ~35 cycles C7/09E5-09E9 + the
// next unit's `LDA [$12],Y / STA $2141` take on the cartridge; at zero CPU time the next unit overwrote bytes 2-3 of every
// unit (the 0AD boot's `70 01` set B: 3776 of 6144 bytes wrong; the logical oracle 0). The call still STARTS where the
// CPU's last wait left the beam, so nothing depends on where the CPU stood; an interrupt taken inside C7 runs in its own
// bank and is held like any other work.
static void snes_runCycle(Snes* snes) {
  if(lfRollbackPending) { snes->cycles += 2; return; }     // the rest of the instruction the valve fell in: discarded
  if(lfIfast > 0 && g_ctLogicalFrames && snes->nmiEnabled && !lfWaitingNow && !snes->cpu->waiting && snes->cpu->k != 0xC7) {
    if((lfIfastRun += 2) <= LF_SAFETY) {                   // the CPU works: nothing else moves (the APU stands, the rule's)
      snes->cycles += 2; g_ctIfastHeld += 2;
      return;
    }
    fprintf(stderr, "logical-frame: WARNING the infinitely fast CPU worked %ld cycles with no wait at %02X:%04X (V=%d) -- "
            "released (safety: a wait the detector does not see)\n", lfIfastRun, snes->cpu->k, snes->cpu->pc, snes->vPos);
    lfIfastRun = 0; lfWaitingNow = 1; lfWaitLo = 0; lfWaitHi = 0xffffffffu;
  } else lfIfastRun = 0;
  if(lfLineHold) {                                         // the battle NMI's service after its line-64 wait: held
    static long lineHeldNow = 0;
    if(lfIntDepth < lfLineHold) { lfLineHold = 0; lineHeldNow = 0; }   //   ...until that NMI has returned
    else if((lineHeldNow += 2) > LF_FRAME) {               // not the rule: the NMI's return was not seen -- released, loudly
      fprintf(stderr, "logical-frame: WARNING line-64 hold past a frame at %02X:%04X (depth %d of %d, SP %04X, saved %04X) -- released\n",
              snes->cpu->k, snes->cpu->pc, lfIntDepth, lfLineHold, snes->cpu->sp, lfIntSp[(lfLineHold > 16 ? 16 : lfLineHold) - 1]);
      lfLineHold = 0; lineHeldNow = 0;
    } else {
      if(g_ctLogicalHoldApu) snes->apuCatchupCycles += (snes->palTiming ? apuCyclesPerMasterPal : apuCyclesPerMaster) * 2.0;
      snes->cycles += 2; g_ctLineHeld += 2;
      return;
    }
  }
  if(lfPhasePending && snes->vPos == 224 && snes->hPos == 1362) {   // an NMI-off stretch's first vblank: phase 0
    if(lfPhaseHold > 0) {
      if(g_ctLogicalHoldApu) snes->apuCatchupCycles += (snes->palTiming ? apuCyclesPerMasterPal : apuCyclesPerMaster) * 2.0;
      snes->cycles += 2; lfPhaseHold -= 2; g_ctLogicalPhaseHeld += 2;
      return;
    }
    lfPhasePending = 0; g_ctLogicalPhaseHolds++;
  }
  if(g_ctLogicalFrames && !g_ctLogicalValve && lfIfast <= 0 && snes->nmiEnabled && snes->vPos == 224 && snes->hPos == 1362) {
    if(lfInWaitLoop(snes)) {                                 // the step into 225 with the CPU waiting: the frame ends
      if(lfIntDepth == 0) lfStretchEndAt(snes, ((uint32_t)snes->cpu->k << 16) | snes->cpu->pc);
    } else if(lfHoldNow < LF_SAFETY) {                       // the CPU is working: held, however long
      if(g_ctLogicalHoldApu) snes->apuCatchupCycles += (snes->palTiming ? apuCyclesPerMasterPal : apuCyclesPerMaster) * 2.0;
      snes->cycles += 2; lfHoldNow += 2;
      return;
    } else {                                                 // not the rule: the wait detector missed a wait
      lfCrossed++; g_ctLogicalCrossed++;
      fprintf(stderr, "logical-frame: WARNING frame %u held %ld cycles with no wait at %02X:%04X -- released (safety, "
              "not the rule: a wait the detector does not see)\n", snes->frames, lfHoldNow, snes->cpu->k, snes->cpu->pc);
    }
    if(lfHoldNow) {
      g_ctLogicalHeld += lfHoldNow; g_ctLogicalHolds++;
      if(getenv("CT_ORACLE_LOGICAL_TRACE"))
        fprintf(stderr, "logical-frame: frame %u held %ld cycles, released at %02X:%04X (stretch %ld)\n", snes->frames,
                lfHoldNow, snes->cpu->k, snes->cpu->pc, lfStretch);
      lfHoldNow = 0;
    }
  }
  if(g_ctLogicalFrames && g_ctLogicalValve && snes->nmiEnabled && snes->vPos == 224 && snes->hPos == 1362 &&
     lfNoHoldFrame != snes->frames) {                         // the step into 225: vblank + NMI (the old valve, A/B)
    if(!lfInWaitLoop(snes) && lfHoldNow >= LF_VALVE && lfSnapFrame == snes->frames) {
      g_ctLogicalValves++;
      if(getenv("CT_ORACLE_LOGICAL_TRACE"))
        fprintf(stderr, "logical-frame: frame %u no wait after %ld cycles at %02X:%04X -- ROLLED BACK, real time\n",
                snes->frames, lfHoldNow, snes->cpu->k, snes->cpu->pc);
      lfRollbackPending = 1; lfHoldNow = 0;
      snes->cycles += 2; return;
    }
    if(!lfInWaitLoop(snes) && lfHoldNow < LF_VALVE) {
      // held: the beam, HDMA and the timers stand still; the CPU runs -- and so does the APU, the CPU's partner, not the
      // beam's: a handshake with the SPC during the hold (the audio driver's C7:063D) must be answered. (Unless the
      // host plays the SPC: g_ctLogicalHoldApu, above.)
      if(g_ctLogicalHoldApu) snes->apuCatchupCycles += (snes->palTiming ? apuCyclesPerMasterPal : apuCyclesPerMaster) * 2.0;
      snes->cycles += 2; lfHoldNow += 2;
      return;
    }
    if(lfHoldNow) {
      g_ctLogicalHeld += lfHoldNow; g_ctLogicalHolds++;
      if(getenv("CT_ORACLE_LOGICAL_TRACE"))
        fprintf(stderr, "logical-frame: frame %u held %ld cycles, released at %02X:%04X%s\n", snes->frames, lfHoldNow,
                snes->cpu->k, snes->cpu->pc, lfHoldNow >= LF_VALVE ? " (VALVE, no snapshot: released)" : "");
      lfHoldNow = 0;
    }
  }
  snes->apuCatchupCycles += (snes->palTiming ? apuCyclesPerMasterPal : apuCyclesPerMaster) * 2.0;
  snes->cycles += 2;
  // check for h/v timer irq's
  bool condition = (
    (snes->vIrqEnabled || snes->hIrqEnabled) &&
    (snes->vPos == snes->vTimer || !snes->vIrqEnabled) &&
    (snes->hPos == snes->hTimer * 4 || !snes->hIrqEnabled)
  );
  if(!snes->irqCondition && condition) {
    snes->inIrq = true;
    cpu_setIrq(snes->cpu, true);
  }
  snes->irqCondition = condition;
  // handle positional stuff
  if(snes->hPos == 0) {
    // end of hblank, do most vPos-tests
    bool startingVblank = false;
    if(snes->vPos == 0) {
      // end of vblank
      snes->inVblank = false;
      snes->inNmi = false;
      lfRdnmiMain = 0;                                     // (the rule's main-thread flag ends with the vblank, as RDNMI's)
      ppu_handleFrameStart(snes->ppu);
    } else if(snes->vPos == 225) {
      // ask the ppu if we start vblank now or at vPos 240 (overscan)
      startingVblank = !ppu_checkOverscan(snes->ppu);
    } else if(snes->vPos == 240){
      // if we are not yet in vblank, we had an overscan frame, set startingVblank
      if(!snes->inVblank) startingVblank = true;
    }
    if(startingVblank) {
      // if we are starting vblank
      lfVbCycles = snes->cycles; lfVbRefresh = g_cycTagCycles[4]; lfVbIfast = g_ctIfastHeld;   // (the logical frame's start, for the phase hold)
      ppu_handleVblank(snes->ppu);
      snes->inVblank = true;
      snes->inNmi = true;
      if(snes->autoJoyRead) {
        // TODO: this starts a little after start of vblank
        snes->autoJoyTimer = 4224;
        snes_doAutoJoypad(snes);
      }
      if(snes->nmiEnabled) {
        if(lfRdnmiOn() && lfRdnmiArmed) lfRdnmiMain = 1;   // the main thread's RDNMI (the rule, above lfRdnmi)
        cpu_nmi(snes->cpu);
      }
    }
  } else if(snes->hPos == 16) {
    if(snes->vPos == 0) snes->dma->hdmaInitRequested = true;
  } else if(snes->hPos == 512) {
    // render the line halfway of the screen for better compatibility
    if(!snes->inVblank && snes->vPos > 0) ppu_runLine(snes->ppu, snes->vPos);
  } else if(snes->hPos == 1104) {
    if(!snes->inVblank) snes->dma->hdmaRunRequested = true;
  }
  // handle autoJoyRead-timer
  if(snes->autoJoyTimer > 0) snes->autoJoyTimer -= 2;
  // increment position
  snes->hPos += 2;
  if(!snes->palTiming) {
    // line 240 of odd frame with no interlace is 4 cycles shorter
    if((snes->hPos == 1360 && snes->vPos == 240 && !snes->ppu->evenFrame && !snes->ppu->frameInterlace) || snes->hPos == 1364) {
      snes->hPos = 0;
      snes->vPos++;
      // even interlace frame is 263 lines
      if((snes->vPos == 262 && (!snes->ppu->frameInterlace || !snes->ppu->evenFrame)) || snes->vPos == 263) {
        snes->vPos = 0;
        snes->frames++;
      }
    }
  } else {
    // line 311 of odd frame with interlace is 4 cycles longer
    if((snes->hPos == 1364 && (snes->vPos != 311 || snes->ppu->evenFrame || !snes->ppu->frameInterlace)) || snes->hPos == 1368) {
      snes->hPos = 0;
      snes->vPos++;
      // even interlace frame is 313 lines
      if((snes->vPos == 312 && (!snes->ppu->frameInterlace || !snes->ppu->evenFrame)) || snes->vPos == 313) {
        snes->vPos = 0;
        snes->frames++;
      }
    }
  }
}

static void snes_catchupApu(Snes* snes) {
  int catchupCycles = (int) snes->apuCatchupCycles;
  int ranCycles = apu_runCycles(snes->apu, catchupCycles);
  snes->apuCatchupCycles -= (double) ranCycles;
}

static void snes_doAutoJoypad(Snes* snes) {
  memset(snes->portAutoRead, 0, sizeof(snes->portAutoRead));
  // latch controllers
  input_latch(snes->input1, true);
  input_latch(snes->input2, true);
  input_latch(snes->input1, false);
  input_latch(snes->input2, false);
  for(int i = 0; i < 16; i++) {
    uint8_t val = input_read(snes->input1);
    snes->portAutoRead[0] |= ((val & 1) << (15 - i));
    snes->portAutoRead[2] |= (((val >> 1) & 1) << (15 - i));
    val = input_read(snes->input2);
    snes->portAutoRead[1] |= ((val & 1) << (15 - i));
    snes->portAutoRead[3] |= (((val >> 1) & 1) << (15 - i));
  }
}

// THE VIRTUAL BEAM -- the zelda3 model (CLAUDE.md; user decisions 2026-09-18 and 2026-09-27). The seed
// routine C3/02DD folds the beam counters (`CMP $2137 / ADC $213C / SBC $213D .. SBC $213D`) into `$0386`, and the beam
// at that instant is the CPU's cycle-exact history since power-on -- no C engine has it (field_window_wipe.md). As
// snesrev/zelda3 does with its H-counter RNG (zelda_cpu_infra.c PatchRom, "Better random numbers"), the reads become a
// VIRTUAL BEAM on both sides: `$213C` = the routine's own step counter `$0351` (1..31, so every call walks differently),
// `$213D` = 00 (both reads); no toggle and no PPU2 open bus is touched. These two values are the rule's own choice, not
// derived from anything. g_ctVirtualBeam: 0 = the real counters; 1 = virtual for every read (the port's own calls of the
// routine, seq_xlat.c CtXlatWipeCall / CtXlatOwWindowCall); 2 = virtual only while the CPU is inside C3/02DD-0306 (the
// oracle, CT_ORACLE_VIRTUAL_BEAM=1, and a translated sequence stage, seq_xlat.c CtXlatStageFrame -- the rule is both
// sides' or it is nothing: with it the port's whole attract hands C3/02DD the cartridge's inputs, call for call).
// THE EVENT RUNNER's deadline reads the same fixed beam (user decision 2026-09-27, the zelda3 literal): C0/5A46
// `LDA $213F` / 5A49 `LDA $2137` / 5A4C + 5A50 `LDA $213D` (low, high bit), then `CMP #00F0 / BPL` and 5A5D `CMP $69 /
// BCS -> 5A90` against the ROM's line $69 = 0x48. V = 0 never reaches it: the runner visits its whole queue every frame,
// on both sides (the port's runner, ct_vm.c, has no cut). On the real beam the cartridge cuts it on busy frames -- 253
// times over the attract, 115 of them at 0A5.
#include "../src/ct_vbeam.h"
int g_ctVirtualBeam = 0;
// ...and THE ANIM DRAIN's raster deadline (C0/CA76, user decision 2026-09-27: "faca como zelda3", the beam that ADVANCES).
// CA82 `LDA $213F` / CA85 `LDA $2137` / CA88 `LDA $213D` (low) / CA8C `LDA $213D` (high bit) at the pass's entry and again
// after every visit (CAB1 / CAC5 -> CA85), then `CMP #00F0 / BPL`, `CMP $6B / BCS -> CAD6`: the pass stops at line $6B
// (0x78). The real line is the frame's CPU history; the virtual one is the pass's own work: V = CT_DRAIN_VBEAM_ENTRY +
// visits * CT_DRAIN_VBEAM_STEP, the same on both sides (move.c CtAnimDrainPass with scanline < 0). The two values are
// the rule's choice, of the cartridge's own order (the attract's 0A5 bats: passes entered at lines 56..141, 31-36 lines a
// bat, 0-4 visits a pass): at $6B = 0x78 a pass makes two visits. The pass's counter restarts at the entry's $213F read.
static int g_ctDrainVbK = 0, g_ctDrainVbHi = 0, g_ctDrainVbV = 0;
uint8_t snes_readBBus(Snes* snes, uint8_t adr) {
  if(g_ctVirtualBeam && (adr == 0x3c || adr == 0x3d || adr == 0x3f)) {
    const Cpu* c = snes->cpu;
    if(c->k == 0xc0 && c->pc >= 0xca82 && c->pc <= 0xca90) {         // the drain's reads
      if(adr == 0x3f) { g_ctDrainVbK = 0; g_ctDrainVbHi = 0; }       //   CA82: the pass's entry (the real read below too)
      else if(adr == 0x3d) {
        if(!g_ctDrainVbHi) { g_ctDrainVbV = CT_DRAIN_VBEAM_ENTRY + g_ctDrainVbK * CT_DRAIN_VBEAM_STEP; g_ctDrainVbHi = 1;
                             return (uint8_t)(g_ctDrainVbV & 0xFF); }
        g_ctDrainVbHi = 0; g_ctDrainVbK++; return (uint8_t)((g_ctDrainVbV >> 8) & 0x01);
      }
    } else if(adr != 0x3f && (g_ctVirtualBeam == 1 || (c->k == 0xc3 && c->pc >= 0x02dd && c->pc <= 0x0306) ||
                              (c->k == 0xc0 && c->pc >= 0x5a46 && c->pc <= 0x5a53)))     // ...and the runner's
      return adr == 0x3c ? snes->ram[0x0351] : 0x00;
  }
  if(adr < 0x40) {
    return ppu_read(snes->ppu, adr);
  }
  if(adr < 0x80) {
    lfApuNow = 1;                  // (the wait detector: an APU poll)
    snes_catchupApu(snes); // catch up the apu before reading
    return snes->apu->outPorts[adr & 0x3];
  }
  if(adr == 0x80) {
    uint8_t ret = snes->ram[snes->ramAdr++];
    snes->ramAdr &= 0x1ffff;
    return ret;
  }
  return snes->openBus;
}

void snes_writeBBus(Snes* snes, uint8_t adr, uint8_t val) {
  if(adr < 0x40) {
    ppu_write(snes->ppu, adr, val);
    return;
  }
  if(adr < 0x80) {
    snes_catchupApu(snes); // catch up the apu before writing
    snes->apu->inPorts[adr & 0x3] = val;
    return;
  }
  switch(adr) {
    case 0x80: {
      snes->ram[snes->ramAdr++] = val;
      snes->ramAdr &= 0x1ffff;
      break;
    }
    case 0x81: {
      snes->ramAdr = (snes->ramAdr & 0x1ff00) | val;
      break;
    }
    case 0x82: {
      snes->ramAdr = (snes->ramAdr & 0x100ff) | (val << 8);
      break;
    }
    case 0x83: {
      snes->ramAdr = (snes->ramAdr & 0x0ffff) | ((val & 1) << 16);
      break;
    }
  }
}

static uint8_t snes_readReg(Snes* snes, uint16_t adr) {
  switch(adr) {
    case 0x4210: {
      uint8_t val = 0x2; // CPU version (4 bit)
      val |= snes->inNmi << 7;
      snes->inNmi = false;
      if(lfIntDepth == 0 && lfRdnmiMain) { val |= 0x80; lfRdnmiMain = 0; }   // the rule: the main thread's vblank flag
      if(lfIntDepth == 0) lfRdnmiArmed = !(val & 0x80);   // ...for a poll that is waiting on it (the rule, above)
      return val | (snes->openBus & 0x70);
    }
    case 0x4211: {
      uint8_t val = snes->inIrq << 7;
      snes->inIrq = false;
      cpu_setIrq(snes->cpu, false);
      return val | (snes->openBus & 0x7f);
    }
    case 0x4212: {
      uint8_t val = (snes->autoJoyTimer > 0);
      val |= (snes->hPos < 4 || snes->hPos >= 1096) << 6;
      val |= snes->inVblank << 7;
      return val | (snes->openBus & 0x3e);
    }
    case 0x4213: {
      return snes->ppuLatch << 7; // IO-port
    }
    case 0x4214: {
      return snes->divideResult & 0xff;
    }
    case 0x4215: {
      return snes->divideResult >> 8;
    }
    case 0x4216: {
      return snes->multiplyResult & 0xff;
    }
    case 0x4217: {
      return snes->multiplyResult >> 8;
    }
    case 0x4218:
    case 0x421a:
    case 0x421c:
    case 0x421e: {
      return snes->portAutoRead[(adr - 0x4218) / 2] & 0xff;
    }
    case 0x4219:
    case 0x421b:
    case 0x421d:
    case 0x421f: {
      return snes->portAutoRead[(adr - 0x4219) / 2] >> 8;
    }
    default: {
      return snes->openBus;
    }
  }
}

static void snes_writeReg(Snes* snes, uint16_t adr, uint8_t val) {
  switch(adr) {
    case 0x4200: {
      snes->autoJoyRead = val & 0x1;
      if(!snes->autoJoyRead) snes->autoJoyTimer = 0;
      snes->hIrqEnabled = val & 0x10;
      snes->vIrqEnabled = val & 0x20;
      if(!snes->hIrqEnabled && !snes->vIrqEnabled) {
        snes->inIrq = false;
        cpu_setIrq(snes->cpu, false);
      }
      // if nmi is enabled while inNmi is still set, immediately generate nmi
      if(!snes->nmiEnabled && (val & 0x80) && snes->inNmi) {
        cpu_nmi(snes->cpu);
      }
      snes_noteNmiWrite(snes, snes->nmiEnabled, (val & 0x80) != 0);
      snes->nmiEnabled = val & 0x80;
      break;
    }
    case 0x4201: {
      if(!(val & 0x80) && snes->ppuLatch) {
        // latch the ppu
        ppu_read(snes->ppu, 0x37);
      }
      snes->ppuLatch = val & 0x80;
      break;
    }
    case 0x4202: {
      snes->multiplyA = val;
      break;
    }
    case 0x4203: {
      snes->multiplyResult = snes->multiplyA * val;
      break;
    }
    case 0x4204: {
      snes->divideA = (snes->divideA & 0xff00) | val;
      break;
    }
    case 0x4205: {
      snes->divideA = (snes->divideA & 0x00ff) | (val << 8);
      break;
    }
    case 0x4206: {
      if(val == 0) {
        snes->divideResult = 0xffff;
        snes->multiplyResult = snes->divideA;
      } else {
        snes->divideResult = snes->divideA / val;
        snes->multiplyResult = snes->divideA % val;
      }
      break;
    }
    case 0x4207: {
      snes->hTimer = (snes->hTimer & 0x100) | val;
      break;
    }
    case 0x4208: {
      snes->hTimer = (snes->hTimer & 0x0ff) | ((val & 1) << 8);
      break;
    }
    case 0x4209: {
      snes->vTimer = (snes->vTimer & 0x100) | val;
      break;
    }
    case 0x420a: {
      snes->vTimer = (snes->vTimer & 0x0ff) | ((val & 1) << 8);
      break;
    }
    case 0x420b: {
      dma_startDma(snes->dma, val, false);
      break;
    }
    case 0x420c: {
      dma_startDma(snes->dma, val, true);
      break;
    }
    case 0x420d: {
      snes->fastMem = val & 0x1;
      break;
    }
    default: {
      break;
    }
  }
}

static uint8_t snes_rread(Snes* snes, uint32_t adr) {
  uint8_t bank = adr >> 16;
  adr &= 0xffff;
  if(bank == 0x7e || bank == 0x7f) {
    return snes->ram[((bank & 1) << 16) | adr]; // ram
  }
  if(bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) {
    if(adr < 0x2000) {
      return snes->ram[adr]; // ram mirror
    }
    if(adr >= 0x2100 && adr < 0x2200) {
      return snes_readBBus(snes, adr & 0xff); // B-bus
    }
    if(adr == 0x4016) {
      return input_read(snes->input1) | (snes->openBus & 0xfc);
    }
    if(adr == 0x4017) {
      return input_read(snes->input2) | (snes->openBus & 0xe0) | 0x1c;
    }
    if(adr >= 0x4200 && adr < 0x4220) {
      return snes_readReg(snes, adr); // internal registers
    }
    if(adr >= 0x4300 && adr < 0x4380) {
      return dma_read(snes->dma, adr); // dma registers
    }
  }
  // read from cart
  return cart_read(snes->cart, bank, adr);
}

void snes_write(Snes* snes, uint32_t adr, uint8_t val) {
  snes->openBus = val;
  uint8_t bank = adr >> 16;
  adr &= 0xffff;
  if(bank == 0x7e || bank == 0x7f) {
    snes->ram[((bank & 1) << 16) | adr] = val; // ram
  }
  if(bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) {
    if(adr < 0x2000) {
      snes->ram[adr] = val; // ram mirror
    }
    if(adr >= 0x2100 && adr < 0x2200) {
      snes_writeBBus(snes, adr & 0xff, val); // B-bus
    }
    if(adr == 0x4016) {
      input_latch(snes->input1, val & 1); // input latch
      input_latch(snes->input2, val & 1);
    }
    if(adr >= 0x4200 && adr < 0x4220) {
      snes_writeReg(snes, adr, val); // internal registers
    }
    if(adr >= 0x4300 && adr < 0x4380) {
      dma_write(snes->dma, adr, val); // dma registers
    }
  }
  // write to cart
  cart_write(snes->cart, bank, adr, val);
}

static int snes_getAccessTime(Snes* snes, uint32_t adr) {
  uint8_t bank = adr >> 16;
  adr &= 0xffff;
  if((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && adr < 0x8000) {
    // 00-3f,80-bf:0-7fff
    if(adr < 0x2000 || adr >= 0x6000) return 8; // 0-1fff, 6000-7fff
    if(adr < 0x4000 || adr >= 0x4200) return 6; // 2000-3fff, 4200-5fff
    return 12; // 4000-41ff
  }
  // 40-7f,co-ff:0000-ffff, 00-3f,80-bf:8000-ffff
  return (snes->fastMem && bank >= 0x80) ? 6 : 8; // depends on setting in banks 80+
}

uint8_t snes_read(Snes* snes, uint32_t adr) {
  uint8_t val = snes_rread(snes, adr);
  snes->openBus = val;
  return val;
}

void snes_cpuIdle(void* mem, bool waiting) {
  Snes* snes = (Snes*) mem;
  dma_handleDma(snes->dma, 6);
  snes_runCycles(snes, 6);
}

uint8_t snes_cpuRead(void* mem, uint32_t adr) {
  Snes* snes = (Snes*) mem;
  int cycles = snes_getAccessTime(snes, adr);
  dma_handleDma(snes->dma, cycles);
  snes_runCycles(snes, cycles);
  return snes_read(snes, adr);
}

void snes_cpuWrite(void* mem, uint32_t adr, uint8_t val) {
  Snes* snes = (Snes*) mem;
  if(g_ctLogicalFrames) lfNoteWrite(snes, adr);                    // the logical frame's wait detector
  int cycles = snes_getAccessTime(snes, adr);
  dma_handleDma(snes->dma, cycles);
  snes_runCycles(snes, cycles);
  snes_write(snes, adr, val);
}

// debugging

void snes_runCpuCycle(Snes* snes) {
  cpu_runOpcode(snes->cpu);
}

void snes_runSpcCycle(Snes* snes) {
  // TODO: apu catchup is not aware of this, SPC runs extra cycle(s)
  spc_runOpcode(snes->apu->spc);
}
