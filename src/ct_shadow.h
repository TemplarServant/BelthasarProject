// ct_shadow.h — the routine-level shadow (docs/SHADOW_PROPOSAL.md).
//
// The oracle plays alone. Each time its CPU arrives at the entry of a REGISTERED ROM routine, the harness copies the
// oracle's WRAM, runs our C function for that routine on the copy, lets the oracle run the routine to its return, and
// compares the two: all 128 KB of WRAM (less the stack bytes the routine pushed, and the ranges an entry declares
// with its reason) and the registers the caller reads. The port's main loop is not involved, so the coverage is
// wherever the oracle goes.
//
// A call is not compared, and is counted apart, when
//   - the oracle takes an interrupt inside it (`interrupted`: the handler writes WRAM the C function never sees);
//   - the oracle leaves it somewhere other than the return address the caller pushed (`exits`);
//   - the entry's P widths or guard do not hold (`skipped`: the C function was written for another convention);
//   - the C function says it does not handle the case (`unported`).
#ifndef CT_SHADOW_H
#define CT_SHADOW_H

#include <stdint.h>
#include <stdio.h>
#include "snes.h"

// The CPU's registers at the entry (and the ones the C function hands back), and the SPC's four output ports as the
// CPU reads them at $2140-$2143.
typedef struct CtShadowRegs { uint16_t a, x, y, dp; uint8_t db, p; uint8_t apu[4]; } CtShadowRegs;

// The registers a caller reads after the return, compared in the entry's `outRegs`.
enum { CT_SH_A8 = 1, CT_SH_A16 = 2, CT_SH_X16 = 4, CT_SH_Y16 = 8, CT_SH_C = 16 };

// A WRAM range an entry does not compare, with the reason printed on the report.
typedef struct CtShadowSkip { uint32_t lo, hi; const char* why; } CtShadowSkip;

typedef struct CtShadowEntry {
  uint32_t addr;          // the 24-bit entry address the oracle's CPU fetches
  const char* name;
  uint8_t pMask, pValue;  // the P bits (m 0x20, x 0x10, d 0x08) the C function's convention needs at entry
  uint8_t isLong;         // entered by JSL (the return address is 3 bytes); otherwise by JSR
  int arg;                // the entry's own datum for its guard and adapter (a VM handler's opcode)
  int (*guard)(const struct CtShadowEntry* e, const uint8_t* wram, const CtShadowRegs* in);   // optional, peek-only
  // Runs the C function on `wram` (the oracle's 128 KB, $7E at offset 0) and fills the registers it hands back.
  // Returns CT_SH_RETURNS, or CT_SH_UNPORTED when the C function does not handle this case (counted `unported`, not
  // compared).
  int (*run)(const struct CtShadowEntry* e, uint8_t* wram, const uint8_t* rom, long romLen, const CtShadowRegs* in,
             CtShadowRegs* out);
  unsigned outRegs;
  const CtShadowSkip* skip;   // optional, ended by { 0, 0, NULL }
} CtShadowEntry;
enum { CT_SH_RETURNS = 0, CT_SH_UNPORTED = 1 };

// Arms the shadow on this oracle (one at a time). `dumpPrefix` (may be NULL): at the first mismatch, writes
// <prefix>.pre.wram (the oracle's WRAM at the entry), <prefix>.oracle.wram and <prefix>.port.wram (at the return).
void CtShadowInstall(Snes* s, const char* dumpPrefix);
void CtShadowUninstall(void);
// One line per registered entry the run reached, then the details of the first mismatches, the ranges not compared,
// and the `shadow:` total line.
void CtShadowReport(FILE* f);
long CtShadowMismatches(void);

#endif
