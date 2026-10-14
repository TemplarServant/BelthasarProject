// What the menu shell's OPEN costs the CPU -- the NMI-off stretch from the caller's C0/0B4E (C0/0B5D, the `STA $4200`
// that turns the NMI off) to C2/8087 (the one that turns it back on) -- priced the way the logical oracle counts it
// (CLAUDE.md, NMI-off loads): the stretch starts at phase 0, so C cycles of it cross floor(C / CT_FRAME_CPU_CYCLES)
// frame boundaries, and the scheduler's first scan S0 is that many frames after the JSL frame C.
//
// Every instruction is LakeSnes's cost (the trace's base per PC, tools/cycle_costs.py); the control flow is the ROM's,
// walked over the state it reads. Validated cycle for cycle against the oracle's beam traces (tools/menu_init_cmp.py,
// tools/menu_init_legs.py; disasm/notes/menu_shell_timing.md section 9). What it does NOT walk, said where it is used:
// C2/9875's data-dependent branches (+-1,800 cycles), the five sound posts' wait on the SPC's echo (the APU's phase:
// +-200 idle, up to ~25,000 with a song playing), and the DMA legs' 8-cycle sync jitter (a few tens).
#ifndef CT_MENU_COST_H
#define CT_MENU_COST_H
#include <stdint.h>

#define CT_FRAME_CPU_CYCLES 346886L   // one frame's non-refresh master cycles: 357,366 - 262 x 40 (snes.c)

// The callers' arms (their code between C0/011B's RTS and the JSL differs)
enum { CT_MENU_CALLER_X = 0,          // the field's X: C0/1958 -> C0/195B JSR C0/011B -> C0/1960 JSL C2:8000
       CT_MENU_CALLER_C8_HI = 1,      // op C8 $80+ (shop, naming): C0/0CEA .. C0/0D09 .. C0/0D18
       CT_MENU_CALLER_C8_ODD = 2,     // op C8 odd (the load shell): C0/0D00 `LDA #00` .. C0/0D18
       CT_MENU_CALLER_Y = 3,          // the field's Y (the exchange): C0/19C4 JSR C0/011B -> C0/19C7 .. C0/19CE JSL C2:8000
       CT_MENU_CALLER_C8_00 = 4 };    // op C8 00 (the exchange): C0/0CEA .. C0/0D04 `LDA #05 / BRL C0/19C7` .. C0/19CE

// C2/F90C over string `idx` of the pointer table at $FF:`table` (2 bpp, the pen from 0): the bank-C2 VWF, glyph by
// glyph (C2/58B2 -> C2/5DC4 -> 24 rows through C2/5E89's shift arm). -1 if the string holds a code it does not walk.
long ct_menu_vwf_cycles(const uint8_t* rom, long romLen, unsigned table, int idx);

// C2/D156, the save-slot scan (the init's one battery-dependent leg), from its first instruction to its RTS.
long ct_menu_slot_scan_cycles(const uint8_t* rom, long romLen, const uint8_t* sram);

// FF/F9C4's verdict on a slot: 0x8080 no signature, 0x4040 the sum disagrees, 0x0000 a save.
int  ct_menu_slot_verdict(const uint8_t* sram, int slot);

// The whole stretch, C0/0B5D -> C2/8087. `wram` = $7E (the roster $2980-88, the PC slots $0197-9C, $0129), `mode` the
// menu mode (0 = the load shell: C2/8000's $7F0061 test), `caller` one of the arms above. -1 if a leg cannot be walked.
long ct_menu_open_stretch(const uint8_t* rom, long romLen, const uint8_t* wram, const uint8_t* sram, int mode,
                          int caller);

#endif
