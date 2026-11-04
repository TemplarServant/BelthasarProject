// seq_stage_int.h -- PRIVATE to seq_stage.c and seq_race.c: the state and the helpers both work the WRAM image with.
// No other file includes it (not main.c, not a public header). The helpers stay `static`: each of the two files keeps its
// own copy, as when each defined them.
#ifndef CT_SEQ_STAGE_INT_H
#define CT_SEQ_STAGE_INT_H
#include <stdint.h>
#include "seq_race.h"   // CtRaceIo
#include "seq_wram.h"   // CT_SEQ_W16 / CT_SEQ_W16_PUT: the WRAM word helpers

// The WRAM image, the carry (state threaded in program order where the code relies on it) and the host's hooks.
typedef struct { uint8_t* w; int c; const CtRaceIo* io; } R;

static unsigned rd(const R* r, unsigned a) { return CT_SEQ_W16(r->w, a); }
static void wr(R* r, unsigned a, unsigned v) { CT_SEQ_W16_PUT(r->w, a, v); }
static int neg(unsigned v) { return (v & 0x8000) != 0; }

// The task-list library (C3 overlay): $3934 removes X from the task list $0920, $395E adds it at the first free slot,
// $3974 adds a vblank hook at the first free slot of $0940. Each is `PHP ... PLP`: the caller's flags survive the call.
// Note: none of the three has a bound. `LDA $0920,X / BEQ / INX / INX / BRA` walks past the sixteen slots until it meets a
// zero (the task list runs into the hook list at $0940, that one into $0960). Kept; the cap only stops a runaway.
static void taskDel(R* r, unsigned x) { for (unsigned i = 0; i < 0x1000; i += 2) if (rd(r, 0x0920 + i) == x) { wr(r, 0x0920 + i, 0); return; } }
static void taskAdd(R* r, unsigned x) { for (unsigned i = 0; i < 0x1000; i += 2) if (!rd(r, 0x0920 + i)) { wr(r, 0x0920 + i, x); return; } }
static void vblAdd(R* r, unsigned x) { for (unsigned i = 0; i < 0x1000; i += 2) if (!rd(r, 0x0940 + i)) { wr(r, 0x0940 + i, x); return; } }

// A word of a $C3 script register page: the page's IP is its $20 (the interpreter CtSeqScript, seq_stage.c).
static void pgw16(R* r, unsigned page, unsigned off, unsigned v) { wr(r, page + off, v); }

#endif
