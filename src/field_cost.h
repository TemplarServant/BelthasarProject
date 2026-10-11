// What the field's RELOAD after a menu close costs the CPU -- C0/0B4E's NMI-off stretch (C0/0B5D, the `STA $4200`
// that turns the NMI off) to C0/0B28's vblank wait (C0/0B38), the ROM's C0/01A5 (the location's load legs again) and
// C0/286C (the first pipeline pass) -- priced the way the logical oracle counts it (CLAUDE.md, NMI-off loads): the
// stretch starts at phase 0, the wait is entered floor(C / CT_FRAME_CPU_CYCLES) frames later and the NMI comes back
// at the end of that frame; C0/2824's first `INC $19` is one full frame after that (C0/EC60).
//
// Every instruction is LakeSnes's cost, read off the cartridge's beam traces (tools/ctlib/costtab.py, the block
// constants tools/pass_cost_consts.py prints; arms no trace ran are priced by the key of an executed instruction --
// the opcode, the widths and the region its operand lands in). The control flow is the ROM's, walked over the WRAM it
// reads: the Python twin is tools/pass_cost_proto.py, the grade tools/reload_cost_cmp.py (disasm/notes/
// menu_shell_timing.md section 10). What is NOT walked is said where it is used: the DMA legs' sync to the master clock
// (a few cycles each), a location whose layers or HDMA run arms no trace covers (reported, not guessed), the obj0
// slot-1 run's ops other than 00 / 12 / 16.
#ifndef CT_FIELD_COST_H
#define CT_FIELD_COST_H
#include <stdint.h>

enum { CT_RELOAD_CALLER_X = 0,     // the field's X menu: C0/1964 (JSR C0/01A5 / JSR C0/1A03 / JSR C0/B192 / JSR C0/2824)
       CT_RELOAD_CALLER_C8 = 1 };  // op C8 (shop, naming): C0/0D1C (JSR C0/01A5 / JSR C0/B192 / ... / BRL C0/2824)

typedef struct {
  long pre;         // C0/0B5D -> C0/286C
  long pass;        // C0/286C -> C0/0B38 (the vblank wait)
  long vm;          // of `pre`: the obj0 slot-1 run (C0/595C)
  int  objects;     // objects C0/B0E6 drew
  const char* why;  // non-NULL: a path the walk does not cover (the total is then not the ROM's)
} CtReloadCost;

// The whole stretch C0/0B5D -> C0/0B38 over `wram` (the flat 128 KB image, $7E at 0 and $7F at +0x10000) as the
// menu leaves it at its RTL -- the field's state restored (C0/0905), the location `loc`. -1 when a leg is not walked.
long ct_field_reload_cycles(const uint8_t* rom, long romLen, const uint8_t* wram, int loc, int caller,
                            CtReloadCost* out);

// The black frames an NMI-off stretch that ENDS in C0/0B28's vblank wait leaves after its own frame: C0/0B38 `LDA $4210 /
// BPL` polls RDNMI, which the PPU sets at line 225 and clears at line 0 -- the end of vblank -- or on the read (LakeSnes
// snes.c). The stretch starts at phase 0 (a vblank's start, CLAUDE.md's NMI-off rule), so after floor(C / F) boundaries
// the wait's read lands rem = C mod F cycles into a vblank: inside its 37 lines (37 x 1,324 non-refresh cycles) the flag
// is still up and the wait falls through in that frame; past them it waits for the next vblank, one frame more.
// MEASURED (logical oracle): loc $082 38.184 F -> 39, loc $005 36.186 F -> 37, loc $082 after a walk 38.022 F -> 38.
static inline int ct_nmi_wait_frames(long cyc) {
  long rem = cyc % 346886L;
  return (int)(cyc / 346886L) + ((rem + 18 >= 37L * 1324) ? 1 : 0);   // +18: the LDA's read after its three fetches
}

// C0/286C's pass alone, over `wram` as the cartridge holds it at the pass's first instruction (an oracle dump,
// `ct.exe --hookpc 286C` with CT_HOOK_DUMPWRAM): how the walk is graded against the traces.
long ct_field_pass_cycles(const uint8_t* rom, long romLen, const uint8_t* wram, int loc, CtReloadCost* out);

#endif
