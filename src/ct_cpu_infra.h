// ct_cpu_infra.h -- the oracle harness (zelda3 model): a LakeSnes core running the original ROM, and the port's
// WRAM (g_ram). Snapshots of both are compared at frame boundaries; a single ROM routine can also be called and
// measured on the oracle (CtOracleCallRoutine).
#ifndef CT_CPU_INFRA_H
#define CT_CPU_INFRA_H

#include <stdint.h>
#include <stdbool.h>
#include "snes.h"

#define CT_WRAM_SIZE  0x20000  // 7E:0000..7F:FFFF
#define CT_VRAM_SIZE  0x10000
#define CT_CGRAM_SIZE 0x200

// 65816 register file captured for comparison.
typedef struct CtRegs {
  uint16_t a, x, y, sp, pc, dp;
  uint8_t  k, db, p, e;        // program bank, data bank, status (nvmxdizc), emulation
} CtRegs;

// A comparable snapshot of machine state at a frame boundary.
typedef struct CtSnapshot {
  uint8_t ram[CT_WRAM_SIZE];
  uint8_t vram[CT_VRAM_SIZE];
  uint8_t cgram[CT_CGRAM_SIZE];
  CtRegs  regs;
} CtSnapshot;

// ---- The two worlds ----
extern uint8_t g_ram[CT_WRAM_SIZE]; // the C port's authoritative WRAM
extern bool    g_run_without_emu;   // set true once everything is ported

// ROM file IO (caller frees).
uint8_t* CtReadFile(const char* path, int* outLen);

// Build a LakeSnes oracle running the given ROM (auto-detects mapping).
// Returns NULL on failure; *outType receives the detected cart type
// (1=LoROM, 2=HiROM, 3=ExHiROM); Chrono Trigger must come back as 2.
Snes* CtOracleCreate(const uint8_t* rom, int len, int* outType);
void  CtOracleDestroy(Snes* s);
void  CtOracleRunFrame(Snes* s);
// PEEK_POKE_AT="N:ADDR=HH,...[;M:ADDR=HH,...]": WRAM bytes written before the (N+1)-th CtOracleRunFrame after the
// state loaded (PEEK_POKE's grammar per group); the oracle's side of the port's CT_POKEWRAM_AT. The state loader
// resets the count.
void  CtOracleFrameCountReset(void);
void  CtOracleStep(Snes* s);   // execute exactly one CPU opcode (for tracing)

// --- CPU bus-write logging ---
// CtOracleInstallWriteHook replaces the oracle's CPU write handler so every CPU write can be recorded.
// `ramAdr` is the WRAM-port latch ($2181-83) at the write, captured live because DMA also advances the port
// and is invisible to this hook. It matters only for writes to $2180 (WMDATA).
typedef struct CtWrite { uint32_t adr; uint32_t ramAdr; uint8_t val; uint8_t bank; uint16_t pc; } CtWrite;
void           CtOracleInstallWriteHook(Snes* s);
void           CtWriteLogStart(void);
void           CtWriteLogStop(void);
int            CtWriteLogCount(void);
const CtWrite* CtWriteLogData(void);

// Step the oracle until the CPU reaches bank:pc (else maxOps). Returns ops run, or -1.
int CtOracleRunToPC(Snes* s, uint8_t bank, uint16_t pc, int maxOps);

// Capture a snapshot from the oracle's live state.
void CtSnapshotFromOracle(const Snes* s, CtSnapshot* out);

// Compare two snapshots. Returns the number of mismatching bytes/fields and
// prints up to maxReport differences as "[label] region offset a=.. b=..".
int CtVerifyEq(const CtSnapshot* a, const CtSnapshot* b, const char* label, int maxReport);

// Copy the oracle's whole WRAM into the port's g_ram.
void CtPortCopyWramFromOracle(const Snes* s);

// --- Call one ROM routine and read its results back (zelda3's RunEmulatedFunc) ---
// Sets the CPU up as a JSR/JSL site would, runs until the routine returns, and reports the register file
// including the carry. Return detection is by stack pointer: sp is recorded before the fake return address
// is pushed, and only the final RTS/RTL brings it back. The check runs after each opcode, so the sentinel
// address is never executed.
#define CT_CALL_KEEP 0xFFFFFFFFu   // for dp/db: keep whatever the loaded state has

typedef struct CtCallSetup {
  uint32_t addr;        // routine entry as a 24-bit SNES address ("C0/5535" = 0xC05535);
                        // a bare 16-bit value defaults to bank $C0
  uint16_t a, x, y;     // entry registers
  uint32_t dp, db;      // direct page / data bank, or CT_CALL_KEEP
  int m8;               // 1 = 8-bit accumulator, 0 = 16-bit
  int x8;               // 1 = 8-bit index,       0 = 16-bit
  int carryIn;          // entry carry
  int isLong;           // 1 = the routine ends in RTL (JSL), 0 = RTS (JSR)
  int maxOps;           // give up after this many opcodes
  int noNmi;            // 1 = hold the vblank NMI off for the call (also CT_CALLROM_NONMI);
                        // otherwise a routine longer than a frame counts the handler's work
} CtCallSetup;

typedef struct CtCallResult {
  CtRegs regs;          // the register file AT RETURN (regs.p bit0 = the carry)
  int ops;              // opcodes executed
  int returned;         // 1 = the routine returned; 0 = still running at maxOps
  long refresh;         // of `cycles`, the DRAM refresh (40 per scanline crossed), not the routine's work
  long cycles;          // master cycles the routine cost (s->cycles delta); every access is charged
                        // at its speed (FastROM 6, WRAM 8, B-bus 6)
} CtCallResult;

extern uint32_t* g_ctPcHist;   // CT_CALLROM_PCHIST: per-PC execution counts of the last call

// Returns 1 if the routine returned, 0 if it ran past maxOps (out is still filled).
int CtOracleCallRoutine(Snes* s, const CtCallSetup* in, CtCallResult* out);

#endif
