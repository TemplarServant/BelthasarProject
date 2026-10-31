// field_cost.h -- the CPU cost of the field's NMI-off loads: the reload after a menu close (C0/0B5D, the NMI off,
// through C0/01A5 and the first pass C0/286C to the vblank wait C0/0B38) and the location load. Priced under the
// NMI-off load rule (CLAUDE.md): the stretch starts at phase 0 and crosses floor(C / CT_FRAME_CPU_CYCLES) frames;
// the NMI returns at the end of that frame and C0/2824's first `INC $19` comes one frame later (C0/EC60).
// Instruction costs come from LakeSnes's beam traces (tools/ctlib/costtab.py, tools/pass_cost_consts.py); the control
// flow is the ROM's, walked over the WRAM it reads. Python twin: tools/pass_cost_proto.py; grade:
// tools/reload_cost_cmp.py (disasm/notes/menu_shell_timing.md sec. 10).
// Not walked: the DMA legs' sync to the master clock, layer/HDMA arms no trace covers (reported), and the obj0
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

// The frames an NMI-off stretch of `cyc` cycles that ends in C0/0B28's vblank wait takes. C0/0B38 `LDA $4210 / BPL`
// polls RDNMI, set at line 225 and cleared at line 0 or on the read. From phase 0, the read lands rem = C mod F cycles
// into a vblank: within its 37 lines (37 x 1,324 cycles) the flag is still up and the wait falls through; past them it
// waits one frame more. Measurements: disasm/notes/menu_shell_timing.md sec. 10.
static inline int ct_nmi_wait_frames(long cyc) {
  long rem = cyc % 346886L;
  return (int)(cyc / 346886L) + ((rem + 18 >= 37L * 1324) ? 1 : 0);   // +18: the LDA's read after its three fetches
}

// C0/286C's pass alone, over `wram` as the cartridge holds it at the pass's first instruction (an oracle dump,
// `ct.exe --hookpc 286C` with CT_HOOK_DUMPWRAM): how the walk is graded against the traces.
long ct_field_pass_cycles(const uint8_t* rom, long romLen, const uint8_t* wram, int loc, CtReloadCost* out);

// One op of the event VM as the load's runners dispatch it (C0/5910 / C0/5977): the handler's cycles from the dispatch's
// JSR to its RTS, decided on `w` (the flat 128 KB WRAM image) as the handler is about to read it; B/blen the script
// body and addr the op. The runner's own cycles per op are CT_VM_DISPATCH. A handler whose arms are not walked is
// charged its fall-through path and named in *why (left alone when already set).
long ct_vm_op_cycles(const uint8_t* rom, long romLen, const uint8_t* w, const uint8_t* B, int blen, int addr, int obj,
                     const char** why);
#define CT_VM_DISPATCH 152L

// The location load, C0/0B5D (NMI off) -> C0/2848's first vblank wait C0/0B38, over `wram` as the port holds it once
// the objects are loaded. Covers every leg except the ops the object pass dispatches (ct_vm_op_cycles, priced by the
// VM as they run), C0/9DC3 (ct_load_9dc3_cycles) and the sound driver (the loader clock). -1 when a leg is not walked
// (out->why names it; out->total then prices that leg at its nearest arm).
typedef struct {
  long packets;     // C0/0960 -> C0/56D4: the decompressions, the map build, the uploads
  long anim;        // FD:DE98 + FD:E292
  long objects;     // C0/B192, B204, A7E9, C0/5709's fills and per-object heads (N = $7F:2000)
  long party;       // C2/8004 (A = 0B)
  long pass;        // C0/2848 -> C0/286C's pass -> C0/0B38
  long total;
  const char* why;
} CtLoadCost;
long ct_field_load_cycles(const uint8_t* rom, long romLen, const uint8_t* wram, int loc, CtLoadCost* out);
long ct_fd_de98_cycles(const uint8_t* rom, long romLen, int loc);
long ct_fd_e292_cycles(const uint8_t* rom, long romLen, int loc);
long ct_c28004_cycles(const uint8_t* rom, long romLen, const uint8_t* w);
// C0/9DC3 (C0/5926's BRL to the RTS into C0/56C8), from the arrival tile's property byte 0 $58, its plane $6A
// (C0/9DFA) and the conveyor's $1D30.
long ct_load_9dc3_cycles(int b58, int a6a, int d30);

#endif
