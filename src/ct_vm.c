// ct_vm.c -- the scene-script (event) VM: init and per-frame execution of the location scripts, the
// location load's memory init, and the op handlers. assets/script.py (run_func) is the Python reference VM.
#include <stdio.h>
#include "romview.h"     // the ROM's small tables from ct_assets.dat (CT_ASSET_TABLE)
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>   // offsetof: the CtWram layout assert
#include <stdlib.h>
#include <math.h>
#include "move.h"
#include "seq_xlat.h"      // CtXlatWipeCall: the window wipe C3/01E4 translated
#include "ct_vm.h"
#include "hdma_table.h"     // FD/C2C1, the field's HDMA table builder (CtHdmaTableBuild)
#include "rom_verify.h"
#include "objslot.h"       // FD/E39C, the palette-effect records op 0x88 arms (the battle's body)
#include "lz.h"
#include "field_cost.h"     // ct_vm_op_cycles: the load's object pass, op by op
#include "tables/ct_regroup.h"          // baked ROM battle-RETURN regroup script templates (tools/_gen_regroup.py)
#include "wipe.h"                     // CtWipeRoutine: the window wipe C3/01E4 and its seed C3/02DD, native
#define CTPI 3.14159265358979323846

// ---- Sound sink (ct_vm.h; disasm/notes/audio_commands.md sec. 6) ----
// The VM stages sound commands in WRAM at $1E00-$1E03 as the ROM does, then hands off through this hook
// (the C analogue of `JSL $C7:0004`). The mailbox stays in WRAM because $1E03 persists across commands.
static CtSoundSink gSoundSink = { 0, 0, 0 };
void CtSceneSetSoundSink(const CtSoundSink* sink) {
  if (sink) gSoundSink = *sink;
  else { gSoundSink.ctx = 0; gSoundSink.post = 0; gSoundSink.port = 0; }
}
// The load's last sound JSLs. A location load runs its scripts' init (C0/5709) and obj0's slot 1 (C0/56C8 -> C0/595C)
// at its end with the NMI off, so their sound commands reach the SPC in the load's last frame. The port runs the load in
// its first frame; with CtSceneLoadPostsMode on, those JSLs are queued here (the mailbox as each left it, $1E03
// included) and CtSceneLoadPostsFlush posts them later. Each keeps the object pass's cycles priced so far, so the host
// can post it as many frames before the load's end as the work after it takes (CtSceneLoadPostCycles). The load's own
// song post (C0/1B8B) is not queued. disasm/notes/overworld_entry_sound.md, field_load_cost.md.
static int gLoadPostMode = 0, gLoadPostOn = 0, gLoadPostN = 0, gLoadPostDone = 0;
static uint8_t gLoadPost[32][4];
static long gLoadPostCyc[32];
void CtSceneLoadPostsMode(int on) { gLoadPostMode = on ? 1 : 0; }
int  CtSceneLoadPostsPending(void) { return gLoadPostN - gLoadPostDone; }
long CtSceneLoadPostCycles(int k) { k += gLoadPostDone; return (k >= 0 && k < gLoadPostN) ? gLoadPostCyc[k] : -1; }
static void palSweepB0E6(CtScene* sc);                      // fwd (needs CtScene too)
static uint8_t vmSoundPort(int k) {
  return gSoundSink.port ? gSoundSink.port(gSoundSink.ctx, k) : 0;   // no APU -> 0 -> waits pass
}

// op lengths (assets/script.py OPLEN); the 5 variable ops are handled in opLen().
static const uint8_t OPLEN[256] = {
  1,1,3,3,3,3,3,3,1,1,2,2,2,2,2,1,
  2,2,5,6,5,5,5,1,3,2,3,1,2,1,2,2,
  2,4,4,3,3,2,2,3,3,2,1,1,3,2,1,3,
  2,2,1,2,2,2,2,2,2,2,3,2,2,3,3,2,
  2,2,2,2,2,3,3,1,5,5,5,6,5,5,1,3,
  4,3,3,4,4,2,4,1,4,4,2,3,1,3,3,3,
  4,3,1,3,3,3,3,3,1,3,1,3,1,1,5,3,   // 0x60 = 4: C0/6AC9 reads a WORD at ip+1 and the local at ip+3, then INY x3
  3,2,2,2,3,2,2,2,3,1,4,5,2,2,1,2,
  2,2,2,3,2,1,1,2,1,2,2,3,3,5,2,2,
  1,1,3,1,2,2,3,3,3,3,4,1,3,3,2,2,
  3,3,1,1,1,1,2,2,2,2,2,2,2,2,1,1,
  1,1,1,1,1,2,2,3,4,1,1,2,1,1,1,1,   // 0xB7 ANIMEREPEAT = op + anim + count (the handler reads B[+1]/B[+2])
  3,2,2,3,3,2,1,2,2,3,2,2,4,3,3,3,
  2,2,3,2,2,3,2,3,3,7,1,1,5,5,5,5,
  5,5,5,2,8,8,5,3,2,1,2,3,4,1,1,1,
  2,1,1,1,2,1,1,1,1,1,1,1,1,2,18,1,
};

static int opLen(const uint8_t* b, int o) {
  uint8_t op = b[o];
  if (op == 0x4E) return 4 + (b[o + 4] | (b[o + 5] << 8));
  if (op == 0x2E) { uint8_t cm = b[o + 1]; if (cm & 0x40) return 6; if (cm & 0x80) return 3 + (b[o + 3] | (b[o + 4] << 8)); return 2; }
  if (op == 0x88) { uint8_t c = b[o + 1]; if (c == 0) return 2; if (c == 0x20 || c == 0x30) return 4; if (c >= 0x40 && c < 0x60) return 5; if (c >= 0x80 && c < 0x90) return 2 + (b[o + 2] | (b[o + 3] << 8)); return 2; }
  if (op == 0xF1) return b[o + 1] == 0 ? 2 : 3;
  if (op == 0xFF) return (b[o + 1] == 0x90 || b[o + 1] == 0x97) ? 5 : 2;
  if (op == 0xCD || op == 0xCE) return 3;          // ADD/SUB GOLD: op + 2-byte amount (C0/385B/3874)
  if (op == 0xCC) return 4;                        // CHECK-GOLD: amount + 1-byte jump (C0/3827)
  return OPLEN[op];
}

// $C1 item/gold family ops this runner has executed. CT_BAGDUMP (field.c) prints it so the check
// `field-item-op-live-vm` can tell an inventory that ran no item ops from one that did.
int g_ctVmItemOps;
int g_ctVmAnimOps;   // liveness: anim-family ops the live VM ran through the shared body (CtVmAnimFamily)

// Same, for the $C2 party/roster family (ops 0xCF/0xD0/0xD1/0xD7/0xF8/0xF9/0xFA).
int g_ctVmPartyOps;

// Debug env vars, read once on first use instead of per op / per frame.
static int gDbgInit;
static const char *gEnvTrace, *gEnvTraceAll, *gEnvRaceMv;
static int gEnvTraceObj, gEnvTraceAllObj;
static void dbgFlags(void) {
  if (gDbgInit) return;
  gDbgInit = 1;
  gEnvTrace = getenv("CT_VMTRACE"); gEnvTraceObj = gEnvTrace ? atoi(gEnvTrace) : 0;
  gEnvTraceAll = getenv("CT_VMTRACE_ALL"); gEnvTraceAllObj = gEnvTraceAll ? atoi(gEnvTraceAll) : 0;
  gEnvRaceMv = getenv("CT_RACEMV");
}

// Script memory: global $7F0000, local $7F0200, system $7E0000. The WRAM lives for the whole process (a
// location change never wipes it, disasm/notes/wram_lifetime.md sec. 1), so the host owns one CtWram and
// hands it to each CtScene.
// Note: the field order matters. The struct is a flat 128 KB WRAM image ($7E at [0, 0x10000), $7F at
// [0x10000, 0x20000)), the index wmIndex() uses; the overworld (world.c) reads bank $7F as `ram + 0x10000 + pc`.
typedef struct CtWram {
  uint8_t s[0x10000];    // $7E:0000-FFFF   (and the flat base of the image)
  uint8_t g[0x200];      // $7F:0000-01FF   -- the story flags,      flat +0x10000
  uint8_t l[0x200];      // $7F:0200-03FF   -- the script locals,    flat +0x10200
  uint8_t hi[0xFC00];    // $7F:0400-FFFF   -- the rest of bank $7F: the world's script blob, the
} Mem;                   //                    sprite-gfx heap, everything C0/58CA's clear covers
// Compile-time proof of the two offsets the overworld depends on.
typedef char ct_wram_layout_assert[
  (offsetof(Mem, g) == 0x10000 && offsetof(Mem, l) == 0x10200 && sizeof(Mem) == 0x20000) ? 1 : -1];
#define GLB 0x7F0000u
#define LOC 0x7F0200u
// Cold init, the port's C0/000E (reset; in the ROM also the in-game load-game path, wram_lifetime.md sec. 3,
// not ported). The only place the WRAM is zeroed; a location change must not come through here.
static void memBoot(Mem* m) {
  memset(m, 0, sizeof *m);
  // Not derived: port seeds for $0104/$1BA7/$1BA8/$1BAA. The load never writes $0104/$1BA8/$1BAA ($1BA7 is
  // cleared by the init runner's 16-bit `58F0 STZ $1B80,X`), so these belong to cold init or to nothing.
  // $1BAB is not seeded: it is zero on the oracle (wram_load_census.md).
  m->s[0x0104] |= 192; m->s[0x1BA7] |= 14; m->s[0x1BAA] |= 4; m->s[0x1BA8] |= 16;
  // New-game party: Crono only; the other active slots and the six reserve slots empty (0x80). Cold init,
  // because the location load never writes $2980-$2988; the Form screen's roster builder (C2/CD2B) reads them.
  m->s[0x2980] = 0x00; m->s[0x2981] = 0x80; m->s[0x2982] = 0x80;
  for (int rsv = 3; rsv < 9; rsv++) m->s[0x2980 + rsv] = 0x80;
}

// What a location load clears (disasm/notes/wram_load_census.md sec. 1): four DMA channel 7 transfers, B->A,
// from $2134 with the multiplier forced to 0 (a hardware zero source), set up by C0/2DF1.
//
//   C0/7F87 -> C0/7F95   $7E:1D00-1DFF    256 B
//   C0/5709 -> C0/5717   $7F:0200-03FF    512 B   the script locals
//   C0/58AB -> C0/58B9   $7E:1900-19FF    256 B
//   C0/58BC -> C0/58CA   $7F:0580-1B3F   5568 B   outside the modelled image -- see below
//
// Everything else in $7E survives the load.
// C0/0B86-0C75: the location load's constant-init pass over the direct page (D = $0100) and a few absolutes
// (wram_load_census.md sec. 8). Read with `dis.py 0B86 <len> 1 0` and checked with `--loadcensus --censusat`.
static void memLoadInit0B86(Mem* m) {
  uint8_t* s = m->s;
  s[0x010A] = s[0x0100]; s[0x010B] = s[0x0101];      // 0B86 LDX $00  / 0B88 STX $0A   (16-bit)
  s[0x010C] = s[0x0102]; s[0x010D] = s[0x0103];      // 0B8A LDX $02  / 0B8C STX $0C
  s[0x010E] = s[0x0104];                             // 0B8E LDA $04  / 0B90 STA $0E
  s[0x0110] = 0; s[0x0111] = 0;                      // 0B92 / 0B94
  s[0x0117] = 0; s[0x0118] = 0;                      // 0B96 / 0B98  <- the op-0xE0 pending-change latch
  s[0x0138] = 0; s[0x010F] = 0;                      // 0B9A / 0B9C
  s[0x0121] = 0xE0;                                  // 0B9E LDA #E0 / 0BA0 STA $21
  s[0x0119] = 0; s[0x01BC] = 0;                      // 0BA2 / 0BA4
  s[0x0BDE] = 0x02;                                  // 0BA6 LDA #02 / 0BA8 STA $0BDE  (absolute, DB=$7E)
  s[0x017D] = 0x00; s[0x017E] = 0x09;                // 0BAB LDX #0900 / 0BAE STX $7D
  s[0x017F] = 0x70; s[0x0180] = 0x07;                // 0BB0 LDX #0770 / 0BB3 STX $7F
  s[0x017B] = 0xA0; s[0x017C] = 0x08;                // 0BB5 LDX #08A0 / 0BB8 STX $7B
  s[0x01B1] = s[0x01B4] = s[0x01B7] = s[0x01BA] = 0xE4;   // 0BBA LDA #E4 / 0BBC..0BC2 the four pointer banks
  s[0x011F] = 1; s[0x0120] = 1;                      // 0BC4 LDA #01 / 0BC6 / 0BC8
  s[0x0168] = 0x05;                                  // 0BCA LDA #05 / 0BCC STA $68
  s[0x0128] = 0x5F;                                  // 0BCE LDA #5F / 0BD0 STA $28
  s[0x0153] = 0; s[0x0126] = 0; s[0x0129] = 0;       // 0BD2 / 0BD4 / 0BD6
  s[0x012F] = 0; s[0x012D] = 0; s[0x0130] = 0;       // 0BD8 / 0BDA / 0BDC
  s[0x0144] = 0; s[0x0145] = 0; s[0x0146] = 0;       // 0BDE / 0BE0 / 0BE2
  s[0x015F] = 0; s[0x0178] = 0; s[0x01BB] = 0;       // 0BE4 / 0BE6 / 0BE8
  s[0x0162] = 0;                                     // 0BEA STZ $62   <- the d-pad gate
  s[0x0163] = 0x80;                                  // 0BEC LDA #80 / 0BEE STA $63
  s[0x0139] = 0; s[0x0154] = 0;                      // 0BF0 / 0BF2
  // 0BF4 REP #20: up to 0C1C every STZ is 16-bit and clears a pair.
  s[0x012B] = 0; s[0x012C] = 0;                      // 0BF6 STZ $2B
  // 0BF8-0C0E: four 24-bit pointers, the words from ROM $E4:FFE0/E2/E4/E6 (baked; the VM reads no ROM)
  // beside the $E4 bank bytes stored above.
  { static const uint16_t p[4] = { 0x2000, 0x2300, 0x2600, 0x2800 };   // rom16($E4:FFE0 + 2n)
    static const int dst[4] = { 0x01AF, 0x01B2, 0x01B5, 0x01B8 };
    for (int i = 0; i < 4; i++) { s[dst[i]] = (uint8_t)p[i]; s[dst[i] + 1] = (uint8_t)(p[i] >> 8); } }
  s[0x0150] = 0; s[0x0151] = 0;                      // 0C10 STZ $0150 (absolute, 16-bit)
  s[0x2000] = 0; s[0x2001] = 0;                      // 0C13 LDA #0000 / 0C16 STA $7E2000
  s[0x0158] = 0; s[0x0159] = 0;                      // 0C1A STZ $58
  // 0C1C SEP #20: 8-bit again.
  s[0x0197] = s[0x0199] = s[0x019B] = 0x80;          // 0C1E LDA #80 / 0C20 / 0C22 / 0C24
  s[0x018D] = s[0x018E] = s[0x018F] = 0x80;          // 0C26 / 0C28 / 0C2A
  s[0x0191] = s[0x0190] = s[0x0192] = s[0x0193] = 0x80;   // 0C2C / 0C2E / 0C30 / 0C32
  s[0x0194] = s[0x2980]; s[0x0195] = s[0x2981]; s[0x0196] = s[0x2982];   // 0C34-0C44: the active party
  // (read, never written; wram_lifetime.md sec. 4).
  s[0x01EB] = s[0x01AE] = 0x80;                      // 0C46 LDA #80 / 0C48 / 0C4A
  s[0x0155] = 0x01;                                  // 0C4C LDA #01 / 0C4E STA $55
  s[0x01FC] = 0x53; s[0x01FB] = 0x65; s[0x01FA] = 0x45;   // 0C50-0C5A
  s[0x2989] = 0;                                     // 0C5C LDA #00 / 0C5E STA $7E2989
  s[0x0BD9] = s[0x0BDA] = s[0x0BDB] = s[0x0BE9] = 0; // 0C62-0C6D
  s[0x01F8] = s[0x0400];                             // 0C70 LDA $0400 / 0C73 STA $F8
  // CT_F8TRACE: prints the $C0:FE00 random cursor's seed (from the play clock $0400) on each load.
  { static int tr = -1; if (tr < 0) tr = getenv("CT_F8TRACE") ? 1 : 0;
    if (tr) printf("f8seed: loc %03X C0/0C70 $01F8 = %02X\n", s[0x0100] | (s[0x0101] << 8), s[0x01F8]); }
}

static void memLocationLoad(Mem* m) {
  memset(m->s + 0x1D00, 0, 0x100);                   // C0/7F95
  memset(m->l, 0, 0x200);                            // C0/5717 -- $7F:0200-03FF
  memset(m->s + 0x1900, 0, 0x100);                   // C0/58B9
  // C0/58CA clears $7F:0580-1B3F: the per-object textbox latch $7F:0A00,X (C0/3580/3588/3635; op 0xBB takes
  // its completion arm when it is set, so a stale latch would swallow a scene's first box), the op-0xAD wait
  // counter $7F:0A01,X (C0/2F96), the CALL wait flag $7F:0980,X, the op-result page $7F:0A80,X, the
  // $7F:0B00,X move lifecycle and the party velocity ring $7F:0C00-0F7F.
  memset(m->hi + (0x0580 - 0x0400), 0, 0x1B40 - 0x0580);   // C0/58CA
  // C0/58CD `LDA #80 / STA $74..$77` (D = $0100): both queues empty -- the runner's ($0174/$0175, C0/59D9-5A46)
  // and the anim FIFO's head and tail ($0176/$0177; C0/C98A enqueues, C0/CA76 drains). A FIFO left non-empty
  // by the last location would link new objects behind entries the drain never reaches.
  m->s[0x0174] = m->s[0x0175] = m->s[0x0176] = m->s[0x0177] = 0x80;
  //
  // The load's own constant fills: $0920-092F = 0xFF (C0/5929), $0B80-0B87 = 0x80 (C0/7155).
  // Note: the heap cursor below is at the wrong address. `58DA STX $71` is direct page with D = $0100
  // (C0/58AA PLD), so the ROM writes $7E:0171/0172. The port is self-consistent at $0071/$0072; moving it
  // is a separate change.
  m->s[0x71] = 0x00; m->s[0x72] = 0x80;              // C0/58D7 (see the address caveat above)
  memset(m->s + 0x920, 0xFF, 16);                    // C0/5929
  memset(m->s + 0xB80, 0x80, 8);                     // C0/7155
  memLoadInit0B86(m);                                // C0/0B86-0C75, the direct-page constant init
  // C0/56A6, the object init's head (C0/008E, every location load): `LDX #0048 / STX $69 / LDX #0078 / STX $6B /
  // STZ $09A0` (DP $0100) -- the two raster deadlines: the event runner's (C0/5A5D `CMP $69`) and the anim
  // drain's (C0/CA99 `CMP $6B`; op 0x47 moves it).
  m->s[0x0169] = 0x48; m->s[0x016A] = 0x00;          // 56A6-56A9
  m->s[0x016B] = 0x78; m->s[0x016C] = 0x00;          // 56AB-56AE
  m->s[0x09A0] = 0x00;                               // 56B0
  // C0/28AA resets the opened-door list (CtWramDoorListAppend's) to sixteen empty words ($7F:1CC8, 0x8080).
  // C0/0091 runs it after C0/56A6 on every field location load (not the overworld, C3 sequence, menu or
  // battle reload paths); nothing else touches the list before C0/2848, so running it here is equivalent.
  for (int k = 0; k < 0x20; k++) m->hi[0x1CC8 - 0x0400 + k] = 0x80;
}

static void memInit(Mem* m, const uint8_t* gmem) {
  memBoot(m);
  memLocationLoad(m);
  if (gmem) memcpy(m->g, gmem, 0x200);
}

#ifdef _WIN32
// ---- CT_WATCH: a write watch on one WRAM byte (hex offset into the flat image) ----
// The byte's page is set read-only; a vectored exception handler lets each store through with a single
// step and reports the ones that change the byte, with function + file:line (dbghelp) and the callers.
// Inert unless CT_WATCH is set (the WRAM is then page-aligned). Note: single-threaded; a store from another
// thread during the step window is missed. Example: `CT_WATCH=0E81 Belthasar --scene 1 --worldtest`.
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
static uint8_t* gWatchAddr; static void* gWatchPage; static int gWatchArmed;
static uint8_t gWatchShadow; static void* gWatchPc;   // value across the single-step + the PC we stepped
static DWORD64 gWatchBt[16]; static int gWatchBtN;   // the faulting frame's callers (StackWalk64)
static void watchArm(void) {
  DWORD old;
  if (gWatchPage && VirtualProtect(gWatchPage, 4096, PAGE_READONLY, &old)) gWatchArmed = 1;
}
static LONG CALLBACK watchVeh(EXCEPTION_POINTERS* ep) {
  DWORD old;
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && gWatchArmed) {
    uint8_t* hit = (uint8_t*)ep->ExceptionRecord->ExceptionInformation[1];
    if (hit >= (uint8_t*)gWatchPage && hit < (uint8_t*)gWatchPage + 4096) {
      // Note: do not filter on `hit == gWatchAddr`. A wide store (memset, a vectorised fill) faults at its
      // first address, not at the watched byte. Instead remember the value, step, and compare after.
      gWatchShadow = *gWatchAddr; gWatchPc = ep->ExceptionRecord->ExceptionAddress;
      // The faulting PC names memset for a CRT bulk store, so keep the callers too (walked on a copy of
      // ContextRecord); they are resolved only if the byte really changed.
      { CONTEXT cc = *ep->ContextRecord; STACKFRAME64 sf; memset(&sf, 0, sizeof sf);
        sf.AddrPC.Offset = cc.Rip; sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrFrame.Offset = cc.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
        sf.AddrStack.Offset = cc.Rsp; sf.AddrStack.Mode = AddrModeFlat;
        gWatchBtN = 0;
        while (gWatchBtN < 16 &&
               StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &sf, &cc,
                           NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL) && sf.AddrPC.Offset)
          gWatchBt[gWatchBtN++] = sf.AddrPC.Offset;
      }
      if (0) {
        void* pc = ep->ExceptionRecord->ExceptionAddress;
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* si = (SYMBOL_INFO*)buf; si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 ln; DWORD ldisp = 0; ln.SizeOfStruct = sizeof ln;
        if (SymFromAddr(GetCurrentProcess(), (DWORD64)pc, &disp, si))
          fprintf(stderr, "[watch] WRITE to the watched byte from %s +0x%llx (pc %p)\n", si->Name, (unsigned long long)disp, pc);
        else fprintf(stderr, "[watch] WRITE to the watched byte at pc %p (no symbol)\n", pc);
        if (SymGetLineFromAddr64(GetCurrentProcess(), (DWORD64)pc, &ldisp, &ln))
          fprintf(stderr, "[watch]   %s:%lu\n", ln.FileName, (unsigned long)ln.LineNumber);
        fflush(stderr);
      }
      VirtualProtect(gWatchPage, 4096, PAGE_READWRITE, &old);   // let the store through...
      gWatchArmed = 0;
      ep->ContextRecord->EFlags |= 0x100;                        // ...then single-step and re-arm
      return EXCEPTION_CONTINUE_EXECUTION;
    }
  }
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && !gWatchArmed && gWatchPage) {
    if (gWatchAddr && *gWatchAddr != gWatchShadow) {                 // the step changed our byte
      char buf[sizeof(SYMBOL_INFO) + 256];
      SYMBOL_INFO* si = (SYMBOL_INFO*)buf; si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
      DWORD64 disp = 0; IMAGEHLP_LINE64 ln; DWORD ldisp = 0; ln.SizeOfStruct = sizeof ln;
      if (SymFromAddr(GetCurrentProcess(), (DWORD64)gWatchPc, &disp, si))
        fprintf(stderr, "[watch] %02X -> %02X by %s +0x%llx\n", gWatchShadow, *gWatchAddr, si->Name, (unsigned long long)disp);
      else fprintf(stderr, "[watch] %02X -> %02X at pc %p\n", gWatchShadow, *gWatchAddr, gWatchPc);
      if (SymGetLineFromAddr64(GetCurrentProcess(), (DWORD64)gWatchPc, &ldisp, &ln))
        fprintf(stderr, "[watch]   %s:%lu\n", ln.FileName, (unsigned long)ln.LineNumber);
      fflush(stderr);
      for (int f = 0; f < gWatchBtN; f++) {                      // the callers, nearest first
        DWORD64 d2 = 0; DWORD l2 = 0; IMAGEHLP_LINE64 li; li.SizeOfStruct = sizeof li;
        if (SymFromAddr(GetCurrentProcess(), gWatchBt[f], &d2, si)) {
          if (SymGetLineFromAddr64(GetCurrentProcess(), gWatchBt[f], &l2, &li))
            fprintf(stderr, "[watch]     #%d %s  (%s:%lu)\n", f, si->Name, li.FileName, (unsigned long)li.LineNumber);
          else fprintf(stderr, "[watch]     #%d %s +0x%llx\n", f, si->Name, (unsigned long long)d2);
        } else fprintf(stderr, "[watch]     #%d %llx\n", f, (unsigned long long)gWatchBt[f]);
      }
      fflush(stderr);
    }
    watchArm();
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
// CT_CRASHTRACE=1: an access violation anywhere prints the faulting function and its callers (file:line) to stderr,
// then the crash goes on as usual -- the port's own stack, for a crash with no debugger at hand.
static LONG CALLBACK crashVeh(EXCEPTION_POINTERS* ep) {
  if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || gWatchArmed) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT cc = *ep->ContextRecord; STACKFRAME64 sf; memset(&sf, 0, sizeof sf);
  sf.AddrPC.Offset = cc.Rip; sf.AddrPC.Mode = AddrModeFlat;
  sf.AddrFrame.Offset = cc.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
  sf.AddrStack.Offset = cc.Rsp; sf.AddrStack.Mode = AddrModeFlat;
  fprintf(stderr, "[crash] access violation at %p (address %p)\n", ep->ExceptionRecord->ExceptionAddress,
          (void*)ep->ExceptionRecord->ExceptionInformation[1]);
  for (int f = 0; f < 16 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &sf, &cc,
                                        NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL) && sf.AddrPC.Offset; f++) {
    char buf[sizeof(SYMBOL_INFO) + 256]; SYMBOL_INFO* si = (SYMBOL_INFO*)buf;
    si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
    DWORD64 d = 0; DWORD l = 0; IMAGEHLP_LINE64 li; li.SizeOfStruct = sizeof li;
    if (SymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &d, si) && SymGetLineFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &l, &li))
      fprintf(stderr, "[crash]   #%d %s  (%s:%lu)\n", f, si->Name, li.FileName, (unsigned long)li.LineNumber);
    else fprintf(stderr, "[crash]   #%d %llx\n", f, (unsigned long long)sf.AddrPC.Offset);
  }
  fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}
static void watchStart(uint8_t* base) {
  if (getenv("CT_CRASHTRACE")) {
    static int on = 0;
    if (!on) { on = 1; SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS); SymInitialize(GetCurrentProcess(), NULL, TRUE);
               AddVectoredExceptionHandler(0, crashVeh); }
  }
  const char* e = getenv("CT_WATCH"); if (!e) return;
  unsigned off = (unsigned)strtoul(e, NULL, 16);
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
  SymInitialize(GetCurrentProcess(), NULL, TRUE);
  gWatchAddr = base + off;
  gWatchPage = (void*)((uintptr_t)gWatchAddr & ~(uintptr_t)4095);
  AddVectoredExceptionHandler(1, watchVeh);
  watchArm();
  fprintf(stderr, "[watch] armed on %p (offset $%04X), page %p\n", (void*)gWatchAddr, off, gWatchPage);
}
#else
static void watchStart(uint8_t* base) { (void)base; }
#endif
CtWram* CtWramCreate(void) {
  // Page-aligned under the write watch so the protected page holds only the WRAM.
#ifdef _WIN32
  Mem* m = getenv("CT_WATCH") ? (Mem*)_aligned_malloc(sizeof *m, 4096) : (Mem*)malloc(sizeof *m);
#else
  Mem* m = (Mem*)malloc(sizeof *m);
#endif
  if (m) memBoot(m);
  if (m) watchStart((uint8_t*)m);
  return (CtWram*)m;
}
void     CtWramLocationLoad(CtWram* w) {
  if (!w) return;
  uint8_t before = ((Mem*)w)->s[0x0119];
  memLocationLoad((Mem*)w);
  if (getenv("CT_LOADCOST")) printf("locload: $0119 %02X -> %02X $011E=%02X (C0/0BA2)\n", before, ((Mem*)w)->s[0x0119], ((Mem*)w)->s[0x011E]);
}

uint8_t* CtWramSys(CtWram* w) { return w ? ((Mem*)w)->s : NULL; }
uint8_t* CtWramGlb(CtWram* w) { return w ? ((Mem*)w)->g : NULL; }
static int r8(Mem* m, uint32_t a) {
  if (a >= 0x7E0000 && a < 0x7F0000) return m->s[a - 0x7E0000];
  if (a >= 0x7F0000 && a < 0x7F0200) return m->g[a - 0x7F0000];
  if (a >= 0x7F0200 && a < 0x7F0400) return m->l[a - 0x7F0200];
  if (a >= 0x7F0400 && a < 0x800000) return m->hi[a - 0x7F0400];   // the rest of bank $7F
  return 0;
}
// Linear index over the modelled image: [0,0x10000) = $7E, [0x10000,0x10200) = $7F0000,
// [0x10200,0x10400) = $7F0200. It is also the offset into the oracle's 128 KB WRAM buffer, so one
// index serves both sides.
#define WM_LEN 0x10400
static int wmIndex(uint32_t a) {
  if (a >= 0x7E0000 && a < 0x7F0000) return (int)(a - 0x7E0000);
  if (a >= 0x7F0000 && a < 0x7F0200) return 0x10000 + (int)(a - 0x7F0000);
  if (a >= 0x7F0200 && a < 0x7F0400) return 0x10200 + (int)(a - 0x7F0200);
  return -1;
}
static uint32_t wmAddr(int i) {
  if (i < 0x10000) return 0x7E0000u + (uint32_t)i;
  if (i < 0x10200) return 0x7F0000u + (uint32_t)(i - 0x10000);
  return 0x7F0200u + (uint32_t)(i - 0x10200);
}
static uint8_t gWmMask[WM_LEN];    // bits WE claim to own (w8 stores | shadow-diff changes)
static uint8_t gWmExcl[WM_LEN];    // bits the harness injected -> comparing them would lie
static uint8_t gWmShadow[WM_LEN];  // our image at frame start
static uint8_t gWmSeedO[WM_LEN];   // the oracle's image at reset -> the coverage denominator
static uint8_t gWmOchg[WM_LEN];    // bits the oracle has changed since the seed
static int32_t gWmFirst[WM_LEN];   // frame each address first diverged (-1 = never)
static int gWm = 0, gWmDropped = 0;

static void w8(Mem* m, uint32_t a, int v) {
  v &= 0xFF;
  if (a >= 0x7E0000 && a < 0x7F0000) m->s[a - 0x7E0000] = v;
  else if (a >= 0x7F0000 && a < 0x7F0200) m->g[a - 0x7F0000] = v;
  else if (a >= 0x7F0200 && a < 0x7F0400) m->l[a - 0x7F0200] = v;
  else if (a >= 0x7F0400 && a < 0x800000) { m->hi[a - 0x7F0400] = v; return; }   // the rest of bank $7F
  else { if (gWm) gWmDropped++; return; }   // outside the image: dropped, and counted
  if (gWm) { int i = wmIndex(a); if (i >= 0) gWmMask[i] = 0xFF; }   // a byte store claims the byte
}

// C0/0C9D-0CBE: the commit half of the pending-location change. Saves where we came from (the location
// word -> $0105/6, the lead's tile X/Y and facing^1 -> $0107/8/9), then copies the staged destination into
// the loader slots: $0112 -> $0100 and $0114 -> $0102 (16-bit), $0116 -> $0104 (8-bit).
// A WRAM operation, not a scene one: its two callers (the $0119 drain, a positional exit) need not have a
// live CtScene.
void CtWramCommitLocation(CtWram* w, int leadOff2) {
  Mem* m = (Mem*)w;
  if (!m) return;
  w8(m, 0x7E0105, r8(m, 0x7E0100));                     // 0C9D LDX $00 / 0C9F STX $05
  w8(m, 0x7E0106, r8(m, 0x7E0101));
  w8(m, 0x7E0107, r8(m, 0x7E1801 + leadOff2));          // 0CA3/0CA6  lead tile X
  w8(m, 0x7E0108, r8(m, 0x7E1881 + leadOff2));          // 0CA8/0CAB  lead tile Y
  w8(m, 0x7E0109, r8(m, 0x7E1600 + leadOff2) ^ 1);      // 0CAD/0CB0 EOR #01 / 0CB2
  w8(m, 0x7E0100, r8(m, 0x7E0112));                     // 0CB4 LDX $12 / 0CB6 STX $00
  w8(m, 0x7E0101, r8(m, 0x7E0113));
  w8(m, 0x7E0102, r8(m, 0x7E0114));                     // 0CB8 LDX $14 / 0CBA STX $02
  w8(m, 0x7E0103, r8(m, 0x7E0115));
  w8(m, 0x7E0104, r8(m, 0x7E0116));                     // 0CBC LDA $16 / 0CBE STA $04  (8-bit)
}
// 16-bit WRAM words are little-endian, as the 65816 stores them.
static int r16(Mem* m, uint32_t a) { return r8(m, a) | (r8(m, a + 1) << 8); }
static void w16(Mem* m, uint32_t a, int v) { w8(m, a, v & 0xFF); w8(m, a + 1, (v >> 8) & 0xFF); }

// ---- Opened-door list, $7F:1CC8 (disasm/notes/field_door_list.md) ----
// Sixteen 16-bit tile indices into the $7E:3000 map ((row << 8) | col, the door request's $015B), $8080 = empty;
// packed, so a walk ends at the first negative word. Owners: C0/28AA reset (in memLocationLoad), C0/28C0 append
// (CtWramDoorListAppend), C0/28E1 re-apply after the map is decompressed afresh (CtWramDoorListApply).
// `$7E3000,X` is long-indexed with a 16-bit X, so the address can run past $7EFFFF into bank $7F.
#define DOORLIST 0x7F1CC8u
static uint32_t doorCell(int x) { return 0x7E3000u + (uint32_t)(x & 0xFFFF); }
// C0/28C0, called from C0/0D49 with the tile index in $015B before the handler dispatch, so it runs for every door
// request whatever the tile holds. Already listed or list full: nothing (a seventeenth door is not remembered).
void CtWramDoorListAppend(CtWram* w, int tileIndex) {
  Mem* m = (Mem*)w;
  if (!m) return;
  tileIndex &= 0xFFFF;                                 // `CMP $5B` / `LDA $5B` are 16-bit (REP #20)
  for (int x = 0; x < 0x20; x += 2) {
    int v = r16(m, DOORLIST + (uint32_t)x);
    if (v & 0x8000) { w16(m, DOORLIST + (uint32_t)x, tileIndex); return; }   // 28C9 BMI -> 28D9
    if (v == tileIndex) return;                                              // 28CD BEQ -> 28D6
  }
}
// C0/28F9, one entry: the map byte at the listed tile selects an arm by its closed id, and each arm writes the
// block's open ids as constants (ARM below: offsets and values). Any other id -> 2918, nothing written.
// Returns 1 when an arm ran.
static int doorReapply(Mem* m, int x) {
  int id = r8(m, doorCell(x));                         // 28FA TAX / SEP #20 / 28FD LDA $7E3000,X
  static const struct { uint8_t id, n; int16_t d[6]; uint8_t v[6]; } ARM[5] = {
    { 0xE6, 2, { 0, -0x100 },                                  { 0xE7, 0xE7 } },                               // 291C
    { 0xEC, 4, { 0, 1, 1 - 0x100, -0x100 },                    { 0xED, 0xEF, 0xEB, 0xE9 } },                   // 2933
    { 0xEE, 4, { 0, -1, -1 - 0x100, -0x100 },                  { 0xEF, 0xED, 0xE9, 0xEB } },                   // 2958
    { 0xFA, 6, { 0, 1, 1 - 0x100, -0x100, -0x200, 1 - 0x200 }, { 0xFB, 0xFD, 0xF9, 0xF7, 0xF3, 0xF5 } },       // 297D
    { 0xFC, 6, { 0, -1, -1 - 0x100, -0x100, -0x200, -1 - 0x200 }, { 0xFD, 0xFB, 0xF7, 0xF9, 0xF5, 0xF3 } },    // 29BA
  };
  for (int k = 0; k < 5; k++) {
    if (ARM[k].id != id) continue;
    for (int u = 0; u < ARM[k].n; u++) w8(m, doorCell(x + ARM[k].d[u]), ARM[k].v[u]);   // X steps are 16-bit
    return 1;
  }
  return 0;                                            // 2918 REP #20 / PLX / RTS
}
// C0/28E1: re-applies each listed entry up to the first empty word. Returns the number of entries whose arm
// ran (a trace number, not a ROM value).
int CtWramDoorListApply(CtWram* w) {
  Mem* m = (Mem*)w;
  if (!m) return 0;
  int n = 0;
  for (int x = 0; x < 0x20; x += 2) {
    int v = r16(m, DOORLIST + (uint32_t)x);
    if (v & 0x8000) break;                             // 28EA BMI -> 28F6
    n += doorReapply(m, v);
  }
  return n;
}
// C0/0D4C: the five door handlers, their $7E:3000 half. `0D4C LDX $5B / LDA $7E3000,X` dispatches on the
// metatile's value and each handler INCs a fixed block (H below). Every INC is 8-bit except the E6 handler's
// second cell (0D7D-0D8E), a 16-bit increment of the word at (r-1,c) that carries into (r-1,c+1).
// Returns 1 when a handler ran, 0 for any other id (C0/0D75 BRL ->16DC). The render's copy of the map is bumped
// by CtFieldSceneDoorOpen (scene.c); this is the WRAM copy the ROM's readers see.
int CtWramDoorOpen(CtWram* w, int tileIndex) {
  Mem* m = (Mem*)w;
  if (!m) return 0;
  int x = tileIndex & 0xFFFF, id = r8(m, doorCell(x));
  static const struct { uint8_t id, n; int16_t d[6]; } H[4] = {
    { 0xEC, 4, { 0, 1, 1 - 0x100, -0x100 } },                               // 0E5F
    { 0xEE, 4, { 0, -1, -1 - 0x100, -0x100 } },                             // 1014
    { 0xFA, 6, { 0, 1, 1 - 0x100, -0x100, -0x200, 1 - 0x200 } },            // 11C9
    { 0xFC, 6, { 0, -1, -1 - 0x100, -0x100, -0x200, -1 - 0x200 } },         // 1454
  };
  if (id == 0xE6) {                                    // 0D78 INC A / STA $7E3000,X
    w8(m, doorCell(x), id + 1);
    uint32_t a = doorCell(x - 0x100);                  // 0D7D-0D8A, sixteen bits wide
    w16(m, a, (r16(m, a) + 1) & 0xFFFF);
    return 1;
  }
  for (int k = 0; k < 4; k++) {
    if (H[k].id != id) continue;
    for (int u = 0; u < H[k].n; u++) { uint32_t a = doorCell(x + H[k].d[u]); w8(m, a, r8(m, a) + 1); }
    return 1;
  }
  return 0;
}

static int cmp_(int l, int r, int c) {
  switch (c & 7) {
    case 0: return l == r; case 1: return l != r; case 2: return l > r; case 3: return l < r;
    case 4: return l >= r; case 5: return l <= r; case 6: return (l & r) > 0; default: return (l | r) > 0;
  }
}
// The party-slot indirection, in one place: move.c's ctVmPartySlotTarget answers (graded by --sweepops for
// ops 0x22/0x24/0xA9). CT_SLOTTRACE prints each distinct (op, n, result) once (see docs/DEBUG_SURFACE.md).
static int gSlotTrace = -1;
static uint32_t gSlotSeen[64];
static int gSlotSeenN;
static int vmSlotTarget(Mem* m, int op, int n) {
  int off = ctVmPartySlotTarget(m->s, n);
  if (gSlotTrace < 0) gSlotTrace = getenv("CT_SLOTTRACE") ? 1 : 0;
  if (gSlotTrace) {
    uint32_t key = ((uint32_t)op << 16) | ((uint32_t)(n & 0xFF) << 8) | (uint32_t)(off & 0xFF);
    int k = 0;
    for (; k < gSlotSeenN; k++) if (gSlotSeen[k] == key) break;
    if (k == gSlotSeenN && gSlotSeenN < (int)(sizeof gSlotSeen / sizeof gSlotSeen[0])) {
      gSlotSeen[gSlotSeenN++] = key;
      printf("slotop: op=%02X n=%02X -> $%04X=%02X obj=%d\n", op, n, 0x0197 + n, off, off / 2);
    }
  }
  return off;
}

// The seven one-byte PC loaders that hardwire their char id instead of taking an operand. From the C0:5D6E
// jump table and each handler's immediate. Note: the mapping is not monotonic in the opcode (0x68 is Frog,
// 0x6A is Robo):
//   op 57 -> C0/41E4  LDA $6D / STA $8D / LDA #00      id 0  Crono
//   op 5C -> C0/41EC  ...     / STA $8E / LDA #01      id 1  Marle
//   op 62 -> C0/41F4  ...     / STA $8F / LDA #02      id 2  Lucca
//   op 6A -> C0/4204  ...     / STA $90 / LDA #03      id 3  Robo
//   op 68 -> C0/41FC  ...     / STA $91 / LDA #04      id 4  Frog
//   op 6C -> C0/420C  ...     / STA $92 / LDA #05      id 5  Ayla
//   op 6D -> C0/4214  ...     / STA $93 / LDA #06      id 6  Magus
static int pcSprite(uint8_t op) {
  switch (op) { case 0x57: return 0; case 0x5C: return 1; case 0x62: return 2; case 0x6A: return 3;
    case 0x68: return 4; case 0x6C: return 5; case 0x6D: return 6; default: return -1; }
}
// Exposed so --vm can print the map: the packed actor table in ct_assets.dat comes from assets/script.py's
// own copy, so comparing the two sides alone cannot catch a mistake both share.
int CtScenePcLoaderId(int op) { return pcSprite((uint8_t)op); }

static int mvAngle(int dx, int dy);                  // fwd: the C0/ABA2 atan (face-toward ops)
static const uint8_t* mvTables(void);                // fwd: [0x400]=F700 facing table

// ---- Ops 0x81/0x82/0x83: the per-object WRAM the loaders write (C0/4476 / C0/4590 / C0/46DF, shared
// C0/4557) and the sprite gfx/palette allocators they feed. The battle staging (C0/0557) reads these bytes
// from the $7E image:
//   $1101,X = enemy id (C0/4564)          $1200,X = 0x7F constant (C0/4579; gfx bank)
//   $1201,X = header size byte (C0/476A)  $0C00/$0C01,X = 0x20 (C0/4571, word 0x2020)
//   $1B01,X = 1 solid marker (C0/46EF)    $1100,X = operand2 bit7 ? 6 : 5 (C0/47EC-47F4)
//   $0D00,X = ((slot&0xFE)<<4)|((slot&1)<<3), slot = (operand2&0x7F)+1 (C0/47AC-47C6), the byte
//             C0/0557's screen-Y slot formula reads.
// All three consume a 5-byte sprite header [gfx, assembly, palette, anim, size]: op 0x81 -> $E4:F000 + id*5,
// op 0x82 -> $E4:F000 + (id+7)*5, op 0x83 -> the field enemy record $E4:F600 + id*10, bytes 0-4. From it:
//   $1380/$1381,X + $1300,X = asmPtrTable[byte1], addr16 + bank (C0/470C-4715)
//   $1400,X (word)          = byte2 * 24, the $E4-bank palette offset (C0/471A-4734; the CGRAM upload source)
//   $1480/$1500/$1580,X     = anim set byte3: (fp[i+1]-fp[i])>>2 / fp[i] / dp[i] (C0/473B-475D)
//   $1201,X                 = byte4 (size flags)
// Gfx allocator (C0/5C90 over $0920 set id / $0930 owner objX, heap cursor $71 from 0x8000): a slot already
// holding byte0 shares the owner's $1280 word (C0/478F); otherwise the first 0xFF slot is claimed, $1280,X
// keeps the cursor before the decompress (C0/4557), and the cursor advances by the LZ size of
// gfxPtrTable[byte0] (C0/47FE-4831). Monster ids >= 0xF8 skip the allocator and read $1280/$1200 straight from
// the gfx table (C0/477A -> C0/4845). Palette allocator: palAlloc7170 / palRelease734C below.
// Without a ROM image (CtSceneSetRomImage) fresh loads leave these bytes 0.
static const uint8_t* gEnemyRecTab; static int gEnemyRecLen;
void CtSceneSetEnemyRecTable(const uint8_t* tab, int len) { gEnemyRecTab = tab; gEnemyRecLen = len; }
static const uint8_t* gRomImg; static long gRomImgLen;
void CtSceneSetRomImage(const uint8_t* rom, long len) { gRomImg = rom; gRomImgLen = len; }
// `JSL FD/C2C1`, the field's HDMA table builder (src/hdma_table.c, disasm/notes/field_hdma_tables.md): writes one
// of the two table sets from the WRAM and flips $0153 bit 0. With no ROM image only the flip is done.
void CtHdmaTableBuild(uint8_t* wram128) {
  if (!wram128) return;
  if (!gRomImg || CtHdmaBuildNative(wram128, gRomImg, gRomImgLen) != 0) wram128[0x0153] ^= 0x01;
}
// ---- Frame legs switch (disasm/notes/frame_wiring.md) ----
// C0/A810's four per-object legs (C98A / A98A / AB45 / A9CD), the A88D edges' setup and teardown, C0/CA76's
// drain and C0/B271's draw pass are wired in CtSceneTick and gated here. They are one chain (the legs stage a
// frame into the four-slot arena, the drain commits it, C0/B701 in the draw pass frees the slot), so they go
// on together or not at all. On by default; `CT_FRAMELEGS=0` turns them off (`vmdiff-wmask-party3` grades
// that baseline). The field frame does not run while the overworld owns $7E:0B30-1B2F (`!inWorld`).
// The drain's raster deadline and the --vmdiff residual families (pinned by `vmdiff-framelegs-party3`) are
// in frame_wiring.md.
static int gLegs = -1;
static int legsOn(void) {
  if (gLegs < 0) { const char* e = getenv("CT_FRAMELEGS"); gLegs = (e && *e == '0') ? 0 : 1; }
  return gLegs;
}
static uint32_t romU16(uint32_t off) { return (uint32_t)CtRomByte(gRomImg, gRomImgLen, (long)(off)) | ((uint32_t)CtRomByte(gRomImg, gRomImgLen, (long)(off + 1)) << 8); }

// ---- OBJ palette allocator, C0/7170 / C0/734C (disasm/notes/objpal_allocator.md) ----
// RAM map (obj-indexed tables are obj*2 strided):
//   $0B80..$0B87  8 slot owners; 0x80 = free, else the newest chain member's obj*2 (init C0/7155).
//   $0B00,X       sharer link PREV (toward the original claimer); 0x80 = nil. Never initialised
//   $0B01,X       sharer link NEXT (toward the newest joiner);    0x80 = nil. by the ROM.
//   $0F81,X       OAM palette attribute = (slot<<1)&0x0E. Not cleared on release: the release reads the
//                 slot back from it (C0/7361, C0/7373).
//   $1400,X       16-bit share key = paletteId*24.
//   $0154 bit7    the script owns the palette table: aborts both allocate and release. Cleared by C0/0BF2 on
//                 every location entry; set by op 0x33 (C0/3711), which the port does not implement.
// The dispatch is the 12-entry table at $7181..$7198:
//   99 71 | 12 72 | 17 72 | 1C 72 | 1C 72 | B4 72 | B4 72 | 1C 72 | 1C 72 | 1C 72 | 1C 72 | 1C 72
// The ROM has no bounds check (C0/717B ASL/TAX/JSR); class >= 12 would dispatch through the word at $7199.
// The port only emits {0..7, 0x80}, so that case is a tripwire below rather than a clamp.
static const struct { int kind, a, b; } kPalDispatch[12] = {   // kind 0 = fixed slot a; 1 = scan [a,b)
  { 0, 5, 0 },  { 0, 6, 0 },  { 0, 7, 0 },              // 0,1,2 -> C0/7199 / 7212 / 7217
  { 1, 0, 5 },  { 1, 0, 5 },                            // 3,4   -> C0/721C  LDX #00 / CPX #05
  { 1, 1, 4 },  { 1, 1, 4 },                            // 5,6   -> C0/72B4  LDX #01 / CPX #04
  { 1, 0, 5 },  { 1, 0, 5 },                            // 7,8   -> C0/721C
  { 1, 0, 5 },  { 1, 0, 5 },  { 1, 0, 5 },              // 9,10,11 -> C0/721C
};

// C0/7324-732C (and the copies at 728A / 71F2): a 16-bit compare of $1400.
static int palShareKeyEq(Mem* m, int aX, int bX) {
  return r8(m, 0x7E1400u + (uint32_t)aX) == r8(m, 0x7E1400u + (uint32_t)bX)
      && r8(m, 0x7E1401u + (uint32_t)aX) == r8(m, 0x7E1401u + (uint32_t)bX);
}

// The 48-byte palette upload of every claim and share tail (C0/71BC / 724B / 72E3, MVN `54 7E E4`).
// Note: both MVNs copy the same 24 source bytes (the PHX/PLX around the first restores the source), so the
// second block is a re-copy at +0x200, not source[24..47].
// dest = 0x2100 + attr*16 + 2 = 0x2100 + slot*32 + 2; src = ROM $E4:0000 + the $1400 word.
static void palUpload(Mem* m, int objX, int attr) {
  if (!gRomImg || gRomImgLen < 0x250000) return;                 // no ROM image -> leave as-is
  uint32_t dst = 0x2100u + (uint32_t)attr * 16u + 2u;            // 72E5-72EF
  uint32_t src = 0x240000u + (uint32_t)(r8(m, 0x7E1400u + (uint32_t)objX)
                                     | (r8(m, 0x7E1401u + (uint32_t)objX) << 8));
  if (src + 24 > (uint32_t)gRomImgLen) return;
  for (int k = 0; k < 24; k++) {
    w8(m, 0x7E0000u + dst + (uint32_t)k, CtRomByte(gRomImg, gRomImgLen, (long)(src + (uint32_t)k)));                 // MVN #1
    w8(m, 0x7E0000u + dst + 0x200u + (uint32_t)k, CtRomByte(gRomImg, gRomImgLen, (long)(src + (uint32_t)k)));        // MVN #2
  }
}

// The claim tail, identical in all three copies (C0/71A0-71B9 / 722F-7248 / 72C7-72E0).
static void palClaim(Mem* m, int objX, int slot) {
  w8(m, 0x7E0B00u + (uint32_t)objX, 0x80);                       // 72CD: PREV := nil
  w8(m, 0x7E0B01u + (uint32_t)objX, 0x80);                       // 72D0: NEXT := nil
  w8(m, 0x7E0B80u + (uint32_t)slot, (uint8_t)objX);              // 72D7: slot owner := me
  int attr = (slot << 1) & 0x0E;                                 // 72DA-72DC
  w8(m, 0x7E0F81u + (uint32_t)objX, (uint8_t)attr);              // 72E0
  palUpload(m, objX, attr);
}

// C0/7170: the gate, the dispatch, and the two passes. Returns 1 if a slot was assigned.
static int palAlloc7170(Mem* m, int obj) {
  if (r8(m, 0x7E0154) & 0x80) return 0;                          // 7170-7172: script owns the table
  int objX = obj * 2, cls = r8(m, 0x7E1100u + (uint32_t)objX);
  if (cls & 0x80) return 0;                                      // 7176-7179: class bit7
  if (cls >= 12) return 0;                                       // tripwire: the ROM would dispatch
                                                                 // through $7199; never emitted
  int kind = kPalDispatch[cls].kind, lo = kPalDispatch[cls].a, end = kPalDispatch[cls].b;
  if (kind == 0) {                                               // C0/7199: one fixed slot; no
    int slot = lo, own = r8(m, 0x7E0B80u + (uint32_t)slot);      //   other slot is tried
    if (own & 0x80) { palClaim(m, objX, slot); return 1; }       // 719E BPL not taken -> claim
    if (!palShareKeyEq(m, own, objX)) return 0;                  // 71F2-71FC mismatch -> 71EB CLC/RTS
    // Note: the ROM's anomaly is kept. C0/7210 (`80 9E`) branches to C0/71B0, two bytes past `71AE LDX $C3`,
    // so X still holds the occupant's obj*2 from 720B: the ROM writes $0B80 + occupant*2 (anywhere in
    // $0B80..$0BFE) and derives the attribute from the occupant index. Do not unify the three tails.
    w8(m, 0x7E0B00u + (uint32_t)objX, (uint8_t)own);             // 7202: my PREV := occupant
    w8(m, 0x7E0B01u + (uint32_t)objX, 0x80);                     // 7207: my NEXT := nil
    w8(m, 0x7E0B01u + (uint32_t)own,  (uint8_t)objX);            // 720D: occupant's NEXT := me
    w8(m, 0x7E0B80u + (uint32_t)own,  (uint8_t)objX);            // 71B0 with X = occupant (!)
    int attr = ((own * 2) & 0x0E);                               // 71B4/71B5, from the occupant idx
    w8(m, 0x7E0F81u + (uint32_t)objX, (uint8_t)attr);            // 71B9
    palUpload(m, objX, attr);                                    // 71B7 keeps $1400[me] as source
    return 1;
  }
  for (int s = lo; s < end; s++) {                               // pass 1 share (72B6 / 721E).
    int own = r8(m, 0x7E0B80u + (uint32_t)s);                    //   Does not skip self: a re-entry
    if (own & 0x80) continue;                                    //   self-shares, as in the ROM.
    if (!palShareKeyEq(m, own, objX)) continue;                  // 7329 BNE -> 7312 next slot
    w8(m, 0x7E0B00u + (uint32_t)objX, (uint8_t)own);             // 7332: my PREV := old newest
    w8(m, 0x7E0B01u + (uint32_t)objX, 0x80);                     // 7337: my NEXT := nil
    w8(m, 0x7E0B01u + (uint32_t)own,  (uint8_t)objX);            // 733D: old newest's NEXT := me
    w8(m, 0x7E0B80u + (uint32_t)s,    (uint8_t)objX);            // 7340 -> 72D5/72D7 (X = the SLOT)
    int attr = (s << 1) & 0x0E;
    w8(m, 0x7E0F81u + (uint32_t)objX, (uint8_t)attr);
    palUpload(m, objX, attr);
    return 1;
  }
  for (int s = lo; s < end; s++)                                 // pass 2 claim (72C0-72C5 / 7228)
    if (r8(m, 0x7E0B80u + (uint32_t)s) & 0x80) { palClaim(m, objX, s); return 1; }
  return 0;                                                      // 72B2 / 734A: CLC, nothing written
}

// C0/734C: the release, a doubly-linked unlink with four branches. $0F81,X is not cleared: the slot is
// recovered from it here (7361 / 7373) and it feeds the sprite's OAM attribute (src/oam.c).
static void palRelease734C(Mem* m, int obj) {
  if (r8(m, 0x7E0154) & 0x80) return;                            // 734C-734E: same gate as allocate
  int objX = obj * 2;
  int nxt = r8(m, 0x7E0B01u + (uint32_t)objX);                   // 7352
  int prv = r8(m, 0x7E0B00u + (uint32_t)objX);                   // 7357 / 737E
  int slot = r8(m, 0x7E0F81u + (uint32_t)objX) >> 1;             // 735E-7362 / 7370-7374: LSR
  if (nxt & 0x80) {                                              // I am the newest (7355 BPL n/t)
    if (prv & 0x80) w8(m, 0x7E0B80u + (uint32_t)slot, 0x80);     // 7370-7377: sole owner -> free
    else { w8(m, 0x7E0B80u + (uint32_t)slot, (uint8_t)prv);      // 7365: slot := my PREV
           w8(m, 0x7E0B01u + (uint32_t)prv, 0x80); }             // 736B: PREV's NEXT := nil
  } else if (prv & 0x80) w8(m, 0x7E0B00u + (uint32_t)nxt, 0x80); // 7390-7394: I was the original
  else { w8(m, 0x7E0B00u + (uint32_t)nxt, (uint8_t)prv);         // 7385: middle unlink (the one
         w8(m, 0x7E0B01u + (uint32_t)prv, (uint8_t)nxt); }       // 738B:  branch not oracle-measured)
}

// The shared header consumption + gfx allocation (see the block comment above). `hdr` = the
// 5-byte [gfx, assembly, palette, anim, size]; `directGfx` = the monster id>=0xF8 ROM-direct
// path (C0/4845: $1280/$1200 from the gfx ptr table entry, no allocation).
static void loadGfxMirror(Mem* m, int obj, const uint8_t* hdr, int directGfx) {
  if (!gRomImg || gRomImgLen < 0x250000) return;
  if (!hdr) return;
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  uint32_t gfxTab = 0x240000u + romU16(0x24FFE0);      // sprite meta $E4:FFE0 [0]: 3-byte gfx ptrs (LZ)
  uint32_t asmTab = 0x240000u + romU16(0x24FFE2);      // [1]: 3-byte assembly ptrs
  uint32_t fpTab  = 0x240000u + romU16(0x24FFE4);      // [2]: u16 anim frame-table ptrs
  uint32_t dpTab  = 0x240000u + romU16(0x24FFE6);      // [3]: u16 anim duration-table ptrs
  uint32_t ap = asmTab + (uint32_t)hdr[1] * 3;         // assembly ptr -> $1380 word + $1300 bank
  w8(m, X + 0x1380, CtRomByte(gRomImg, gRomImgLen, (long)(ap))); w8(m, X + 0x1381, CtRomByte(gRomImg, gRomImgLen, (long)(ap + 1))); w8(m, X + 0x1300, CtRomByte(gRomImg, gRomImgLen, (long)(ap + 2)));
  uint32_t po = (uint32_t)hdr[2] * 24;                 // palette offset word $1400 (LE)
  w8(m, X + 0x1400, po & 0xFF); w8(m, X + 0x1401, (po >> 8) & 0xFF);
  uint32_t fy = (uint32_t)hdr[3] * 2;                  // anim set words $1480/$1500/$1580 (LE)
  uint32_t f0 = romU16(fpTab + fy), f1 = romU16(fpTab + fy + 2), d0 = romU16(dpTab + fy);
  uint32_t st = ((f1 - f0) & 0xFFFFu) >> 2;
  w8(m, X + 0x1480, st & 0xFF); w8(m, X + 0x1481, (st >> 8) & 0xFF);
  w8(m, X + 0x1500, f0 & 0xFF); w8(m, X + 0x1501, (f0 >> 8) & 0xFF);
  w8(m, X + 0x1580, d0 & 0xFF); w8(m, X + 0x1581, (d0 >> 8) & 0xFF);
  w8(m, X + 0x1201, hdr[4]);                           // size flags
  uint32_t gp = gfxTab + (uint32_t)hdr[0] * 3;
  if (directGfx) {
    w8(m, X + 0x1280, CtRomByte(gRomImg, gRomImgLen, (long)(gp))); w8(m, X + 0x1281, CtRomByte(gRomImg, gRomImgLen, (long)(gp + 1)));
    w8(m, X + 0x1200, CtRomByte(gRomImg, gRomImgLen, (long)(gp + 2)));
    return;
  }
  for (int s = 0; s < 16; s++)                         // C0/5C90 pass 1: share an existing set
    if (r8(m, 0x7E0920 + (uint32_t)s) == hdr[0]) {
      int own = r8(m, 0x7E0930 + (uint32_t)s);
      w8(m, X + 0x1280, r8(m, 0x7E1280 + (uint32_t)own));
      w8(m, X + 0x1281, r8(m, 0x7E1281 + (uint32_t)own));
      return;
    }
  int freeSlot = -1;                                   // pass 2: claim the first free slot
  for (int s = 0; s < 16 && freeSlot < 0; s++)
    if (r8(m, 0x7E0920 + (uint32_t)s) == 0xFF) freeSlot = s;
  if (freeSlot < 0) return;                            // ROM: LDX #4010 BRL C0/2E1E diagnostic halt
  w8(m, 0x7E0920 + (uint32_t)freeSlot, hdr[0]);
  w8(m, 0x7E0930 + (uint32_t)freeSlot, obj * 2);
  uint32_t cur = (uint32_t)r8(m, 0x7E0071) | ((uint32_t)r8(m, 0x7E0072) << 8);
  w8(m, X + 0x1280, cur & 0xFF); w8(m, X + 0x1281, (cur >> 8) & 0xFF);   // pre-decompress cursor
  uint32_t src = ((uint32_t)(CtRomByte(gRomImg, gRomImgLen, (long)(gp + 2)) - 0xC0) << 16) | romU16(gp);   // 3-byte SNES ptr -> file
  if (CtRomByte(gRomImg, gRomImgLen, (long)(gp + 2)) >= 0xC0 && src + 2 <= (uint32_t)gRomImgLen) {
    static uint8_t scratch[0x10000];                   // the decompressed size, as the ROM's JSL C30002
                                                       // job measures it ($0306 -> $71 += size). The ROM
                                                       // caps this heap at 32 KB (C0/46CA/4833).
    size_t sz = ct_lz_decompress(gRomImg, scratch, src);
    // ...and its ROM cycles (C0/46BB / C0/4825, `$0305 = 7F`): a location load's black window also covers
    // every sprite sheet its object pass decompresses.
    g_ctLzSpriteCycles += ct_lz_rom_cycles(gRomImg, (size_t)gRomImgLen, (size_t)src, 1);
    cur = (cur + (uint32_t)sz) & 0xFFFFu;
    w8(m, 0x7E0071, cur & 0xFF); w8(m, 0x7E0072, (cur >> 8) & 0xFF);
  }
}

// Ops 0x81/0x82, the NPC loaders: the same header consumption over the NPC header table. The claims matter for
// a later monster load (NPC palette slots 0..4 overlap the monster range 1..3; op-0x82 gfx sets take $0920
// slots and heap). 0x81 (C0/4476) is always ROM-direct: $1280/$1200 = the gfx table entry (C0/4518-4536; raw
// banks D2/D3, no allocator). 0x82 (C0/4590) allocates (JSR C0/5C90 at C0/4626 and the $71 advance).
// The position words a placement op stores ($1800/$1801 = X sub-pixel/tile, $1880/$1881 = Y): C0/4B74 (op 0x8B,
// X := tile*256 + $80, Y := tile*256 + $FF), C0/4C27 (op 0x8D, the script's raw words), op 0x8C (two locals).
// The init runner C0/5709 runs these ops at the load, so C0/B0E6's palette sweep (`LDA $1801,X / ASL`) sees
// every placed object's tile byte. Written here at the prescan, once, as the handlers do.
static void vmPosWordsMirror(Mem* m, int obj, int px16, int py16) {
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  w8(m, X + 0x1800, (uint8_t)px16); w8(m, X + 0x1801, (uint8_t)(px16 >> 8));
  w8(m, X + 0x1880, (uint8_t)py16); w8(m, X + 0x1881, (uint8_t)(py16 >> 8));
}
// Op 0x8C = C0/4BC3, op 0x8B's indirect twin: both operands index the locals $7F0200 (stride 2) and the low byte
// of each slot is the tile (`SEP #20 / LDA $7F0200,X`). It stores $1801,X / $1881,X and `$1880,X = FF`; $1800,X
// is not written, so X keeps its sub-position (tile*256 + the old $1800). No zero guard: tile (0,0) is placed.
// The placement ops' OAM priority pair (C0/4B98-4BBC op 0x8B, C0/4BFC-4C20 op 0x8C, C0/4C42-4C6C op 0x8D): with
// $0F80,X bit 7 (AUTO) the new tile's property planes give it (`LDA $7E7000,X / AND #40 / LSR / LSR / ORA #20`
// -> $D9 -> $0C01, the $7E7040 one -> $0C00; X = (y << 8) | x); without it the pair is left as it was.
static void vmPlacePrio(Mem* m, int obj, int tx, int ty) {
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  if (!(r8(m, X + 0x0F80) & 0x80)) return;                         // 4B98 LDA $0F80,X / BPL ->4BBF
  uint32_t ti = ((uint32_t)(ty & 0xFF) << 8) | (uint32_t)(tx & 0xFF);
  w8(m, X + 0x0C01, (uint8_t)(((r8(m, 0x7E7000 + ti) & 0x40) >> 2) | 0x20));   // 4B9F-4BA9 -> $D9 -> 4BBC
  w8(m, X + 0x0C00, (uint8_t)(((r8(m, 0x7E7040 + ti) & 0x40) >> 2) | 0x20));   // 4BAB-4BB7
  { static int tr = -1; if (tr < 0) tr = getenv("CT_PLACEPRIO_TRACE") ? 1 : 0;   // CT_PLACEPRIO_TRACE: a placement's pair
    if (tr && (r8(m, X + 0x0C00) != 0x20 || r8(m, X + 0x0C01) != 0x20))
      fprintf(stderr, "placeprio loc %03X obj%d tile (%02X,%02X) pair %02X %02X\n", r8(m, 0x7E0100) | (r8(m, 0x7E0101) << 8), obj,
              tx & 0xFF, ty & 0xFF, r8(m, X + 0x0C00), r8(m, X + 0x0C01)); }
}
static void vmPos8C(Mem* m, CtVmActor* a, int obj, int vxi, int vyi) {
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  int tx = r8(m, LOC + (uint32_t)vxi * 2), ty = r8(m, LOC + (uint32_t)vyi * 2);   // 4BCE ASL / 4BD2, 4BE8 / 4BEC
  w8(m, X + 0x1801, (uint8_t)tx);                                  // 4BD8
  w8(m, X + 0x1881, (uint8_t)ty);                                  // 4BF2
  w8(m, X + 0x1880, 0xFF);                                         // 4BF7-4BF9
  a->px16 = (tx << 8) | r8(m, X + 0x1800); a->py16 = (ty << 8) | 0xFF;
  a->x = a->px16 >> 4; a->y = (a->py16 + 1) >> 4; a->hasPos = 1;          // the C pixel as op 0x8B keeps it: foot = tile*16+16
  vmPlacePrio(m, obj, tx, ty);                                             // 4BFC-4C20
}
static void npcLoadMirror(Mem* m, int obj, int hdrIdx, int mode) {
  // The loader tail's four constant stores. Op 0x81 (C0/4476) falls into the shared descriptor walk whose tail
  // is C0/453B-454F; op 0x82 (C0/4559), which op 0x83 enters at C0/4557, repeats them at C0/4571-458A:
  //   453B/4571  LDA #2020 / STA $0C00,X     the OAM priority pair
  //   4543/4583  LDA #00   / STA $1680,X     no anim mode
  //   4548/4588  LDA #00   / STA $1681,X     no phase
  //   454D/457E  LDA #01   / STA $1600,X     facing 1 = down (an NPC with no FACE op faces down)
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  w8(m, X + 0x0C00, 0x20); w8(m, X + 0x0C01, 0x20);    // 453B / 4571 (16-bit store of #2020)
  w8(m, X + 0x1680, 0x00);                             // 4543 / 4583
  w8(m, X + 0x1681, 0x00);                             // 4548 / 4588
  w8(m, X + 0x1600, 0x01);                             // 454D / 457E
  // ...and solid, which the three NPC arms store around that shared tail (op 0x81 C0/447F before the walk,
  // op 0x82 C0/45A0 and op 0x83 C0/46EF after it). $1B01,X bit 0 is the candidate test of the object-solid
  // scan (C0/5BDB).
  w8(m, X + 0x1B01, 0x01);                             // 447F / 45A0 / 46EF
  // ...and the two the arms make on the way in: `STA $1101,X` = the load op's raw operand (C0/448A for 0x81,
  // C0/4564 for 0x82, whose record index is operand + 7) and, in C0/4557 only, `LDA #7F / STA $1200,X` (C0/4579),
  // the sheet's bank, which C0/CAD9's tile copy C0/E687 reads. Op 0x81 takes its bank from the record.
  w8(m, X + 0x1101, (uint8_t)(mode == 4 ? hdrIdx - 7 : hdrIdx));
  if (mode == 4) w8(m, X + 0x1200, 0x7F);
  if (!gRomImg || gRomImgLen < 0x250000) return;       // palette slots assign on first render,
  loadGfxMirror(m, obj, CtRomPtr(gRomImg, gRomImgLen, 0x24F000 + (long)hdrIdx * 5, 5), mode == 3);   // not at load
}

// The PC loader C0/448D (ops 0x57/0x5C/0x62/0x6A/0x68/0x6C/0x6D via C0/41E4-4214, and op 0x80's three in-party
// arms C0/42B5 / 4329 / 43DC): the same descriptor walk as the NPC loader over the seven PC records at
// $E4:F000 + id*5 (move.c's ctPcLoad shares ctLoaderWalk with CtObjLoad), then its own tail: the graphics
// pointer comes straight from the 3-byte table ([$AF],Y at C0/4529; a PC's sheet is resident, so no C0/5C90 and
// no decompress), and C0/453B-454D store $0C00/$0C01 = $20, $1680 = 0, $1681 = 0, $1600 = 1. It does not write
// $1B01 (three NOPs at 4498-449A); the party arms set that byte. The frame legs read these tables.
// C0/42A1-42AD (slot 0) / C0/4315-4321 (slot 1) / C0/43C8-43D4 (slot 2): the three party arms' common prologue,
// the same three stores, run before the shared `JSR C0/448D` tail on every PC load:
//   42A1 STZ  $1C80,X            object (bit1) and map (bit0) collision off for a party member
//   42A6 LDA #01 / STA $1A81,X   the script visibility byte
//   42AB LDA #01 / STA $1B01,X   solid = 1 (a PC blocks other movers' C0/5B95 scan)
static void pcSlotArmCommon(Mem* m, int obj, CtVmActor* a) {
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  w8(m, X + 0x1C80, 0x00); a->coll = 0; a->collMap = 0;   // 42A1 / 4315 / 43C8
  w8(m, X + 0x1A81, 0x01);                                // 42A6 / 431A / 43CD
  w8(m, X + 0x1B01, 0x01); a->solid = 1;                  // 42AB / 431F / 43D2
  // 42B3 `STX $97` / 4327 `STX $99` / 43DA `STX $9B`: the party-slot table, written by the load op itself and
  // 16-bit (X is wide from the runner's REP #10), so the high byte is zeroed too. Later objects' ops and the
  // follower arm (C0/435C `LDA $97`) read it during the same load.
  if (a->moveMode <= 2) { w8(m, 0x7E0197 + (uint32_t)a->moveMode * 2, (uint8_t)(obj * 2));
                          w8(m, 0x7E0198 + (uint32_t)a->moveMode * 2, 0); }
}
// The two stores every PC load makes before its party test (move.c's ctVmPcLoad has both):
//   41E4/41EC/.../4214 `LDA $6D / STA $8D+id` (the fixed-char ops) and 422A-4268 the same through op 0x80's
//   seven-way `CMP #n` chain (no default arm: an id > 6 stores nowhere)
//     -> $018D+id = this object's X, the char -> object table op 0xD3 reads (C0/3933 `LDX $D9 / LDA $8D,X`);
//   4295 (not in the party) / 42B0 / 4324 / 43D7 (slots 0-2) `LDA $D9 / STA $1101,X`
//     -> the object's char id, which op 0xD4 (C0/394E) compares for each party-object slot.
static void pcLoaderIdStores(Mem* m, int obj, int id) {
  if (id >= 0 && id <= 6) w8(m, 0x7E018D + (uint32_t)id, (uint8_t)(obj * 2));
  w8(m, 0x7E1101 + (uint32_t)obj * 2, (uint8_t)id);
}
static void pcLoadMirror(Mem* m, int obj, int id) {
  if (!gRomImg || gRomImgLen < 0x250000 || id < 0 || id > 6) return;
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  loadGfxMirror(m, obj, CtRomPtr(gRomImg, gRomImgLen, 0x24F000 + (long)id * 5, 5), 1);   // C0/449B-4536
  w8(m, X + 0x0C00, 0x20); w8(m, X + 0x0C01, 0x20);                   // 453B LDA #2020 / STA $0C00,X
  w8(m, X + 0x1680, 0x00);                                            // 4543
  w8(m, X + 0x1681, 0x00);                                            // 4548
  w8(m, X + 0x1600, 0x01);                                            // 454D
}

// C0/42B8-430F: the lead PC's arrival arm, run by the slot-0 branch of the PC loader after the C0/448D tables.
// It places the lead on the location loader's arrival bytes and finally overwrites the tail's `$1600 = 1`
// with the arrival facing:
//   42B8 LDX $6D / LDA $02 / STA $1801,X / LDA $03 / STA $1881,X   the arrival tile ($0102/$0103)
//   42C4 STX $0B89                                                    the lead's object offset
//   42C7 REP / LDA #0008 / STA $0D00,X / LDA #0000 / STA $1700,X
//   42D7 LDA $1000,X / AND #80 / ORA #01 / STA $1000,X               cadence 1, pause bit kept
//   42E1 LDA $04 / AND #03 / STA $1600,X                             facing = $0104 bits 0-1
//   42E8 LDA $04 / BIT #08 / BNE -> $1880,X = $7F  else $FF           Y half-tile
//   42FA LDA $04 / BIT #04 / BNE -> $1800,X = $00  else $80           X half-tile
// The host derives the arrival pixel from the same $0104 bits (field.c, the pendKind path).
// pcPlaceDefault: the five stores of the default placement from the arrival bytes. The ROM has them twice: in
// the lead's arm (C0/42BA/42C1/42E1/42E8/42FA) and in the follower arm's no-leader branch (C0/438A-43BD).
// $0102/$0103 are the arrival tile (the high bytes of the 12.4 words), $0104 bits 0-1 the facing, bit 3 the
// Y half-tile and bit 2 the X.
static void pcPlaceDefault(Mem* m, int obj, CtVmActor* a) {
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  uint8_t b4 = r8(m, 0x7E0104);
  w8(m, X + 0x1801, r8(m, 0x7E0102)); w8(m, X + 0x1881, r8(m, 0x7E0103));   // 42BA-42C1 / 438A-4391
  w8(m, X + 0x1600, (uint8_t)(b4 & 3)); a->facing = b4 & 3;                   // 42E1-42E5 / 4394-4398
  w8(m, X + 0x1880, (b4 & 0x08) ? 0x7F : 0xFF);                               // 42E8-42F7 / 439B-43AA
  w8(m, X + 0x1800, (b4 & 0x04) ? 0x00 : 0x80);                               // 42FA-430A / 43AD-43BD
}
static void pcArrivalArm(Mem* m, int obj, CtVmActor* a) {
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  w8(m, 0x7E0B89, (uint8_t)(obj * 2)); w8(m, 0x7E0B8A, 0);                    // 42C4 STX $0B89 (X = 16-bit offset)
  w16(m, X + 0x0D00, 8); w16(m, X + 0x1700, 0);                               // 42C9-42D2
  w8(m, X + 0x1000, (uint8_t)((r8(m, X + 0x1000) & 0x80) | 1));               // 42D7-42DE
  pcPlaceDefault(m, obj, a);                                                  // 42BA/42C1/42E1/42E8/42FA
}
// C0/4356-4386: the follower arm's tail of the PC loader, run by the slot-1 (C0/4310) and slot-2 (C0/43C3)
// branches after the C0/448D tables, where the lead's branch runs its arrival arm:
//   4356 STZ $AB / 4358 STZ $AC / 435A STZ $AD      the three party ring cursors ($01AB head, $01AC and
//                                                   $01AD the two follower reads): the velocity ring
//                                                   restarts empty on every PC load
//   435C LDA $97 / 435E BMI ->438A                  no leader ($0197 bit7) -> the default placement arm
//   4360 REP #20 / AND #00FF / TAX / 4366 LDA $1800,X / 4369 LDX $99 / 436B STA $1800,X
//   436E LDX $97 / 4370 LDA $1880,X / 4373 LDX $99 / 4375 STA $1880,X
//   4378 STZ $1900,X                                 (16-bit: $1900 AND $1901)
//   437D LDX $97 / 437F LDA $1600,X / 4382 LDX $99 / 4384 STA $1600,X    the lead's facing, 8-bit
// So a follower is loaded on the leader's tile, facing the leader's way, with no velocity and an empty trail.
// The 16-bit `LDX $97` reads at 436E/437D are taken as the byte $0197 (as move.c ctPcLoadSlot does): $0198 is
// 0 in every state the engine can produce.
// Before that tail, between C0/448D's tables and 4356, the slot's own block; the two slots differ only in their
// constants (move.c ctVmPcLoad's PS[] table is the --sweepops-graded copy):
//   432C LDX $6D / LDA $78 / BEQ / LDA #02 / STA $78 / STZ $1601,X          (43DF, the same)
//   4339 STX $0B8A                        slot 2: 43EC STX $0B8B (16-bit: the next byte is zeroed too)
//   433C REP / LDA #0020 / STA $0D00,X / LDA #0020 / STA $1700,X            slot 2: 0028 / 0040
//   434C LDA $1000,X / AND #80 / ORA #01 / STA $1000,X                      the period 1, the pause bit kept
static void pcFollowerSlotArm(Mem* m, int obj, CtVmActor* a) {
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  int s2 = (a->moveMode == 2);
  if (r8(m, 0x7E0178)) { w8(m, 0x7E0178, 2); w8(m, X + 0x1601, 0); }        // 432E-4336 / 43E1-43E9
  w16(m, s2 ? 0x7E0B8B : 0x7E0B8A, obj * 2);                              // 4339 / 43EC
  w16(m, X + 0x0D00, s2 ? 0x0028 : 0x0020);                              // 433C-4341 / 43EF-43F4
  w16(m, X + 0x1700, s2 ? 0x0040 : 0x0020);                              // 4344-4347 / 43F7-43FA
  w8(m, X + 0x1000, (uint8_t)((r8(m, X + 0x1000) & 0x80) | 1));          // 434C-4353 / 43FF-4406
}
static void pcFollowerArm(Mem* m, int obj, CtVmActor* a) {
  pcFollowerSlotArm(m, obj, a);                                        // 432C-4353 / 43DF-4406
  w8(m, 0x7E01AB, 0); w8(m, 0x7E01AC, 0); w8(m, 0x7E01AD, 0);          // 4356 / 4358 / 435A
  int lead = r8(m, 0x7E0197);                                          // 435C LDA $97 (the lead's obj*2)
  if (lead & 0x80) {                                                   // 435E BMI ->438A: no leader yet.
    // The party table is refilled as each PC's load op runs (pcSlotArmCommon) and the init runner walks the
    // objects up from 0 (C0/58EA), so a follower takes this arm only when its object comes before the lead's.
    // It is placed on the location's arrival tile and facing, the bytes the lead will use.
    pcPlaceDefault(m, obj, a);                                         // 438A-43C2
    uint32_t X2 = 0x7E0000u + (uint32_t)obj * 2;
    a->px16 = r8(m, X2 + 0x1800) | (r8(m, X2 + 0x1801) << 8);
    a->py16 = r8(m, X2 + 0x1880) | (r8(m, X2 + 0x1881) << 8);
    a->x = a->px16 >> 4; a->y = a->py16 >> 4; a->hasPos = 1;
    if (getenv("CT_FOLDBG"))
      fprintf(stderr, "[folload] obj%d no-leader arm: arrival (%.2f,%.2f) fac=%d (ring cleared)\n",
              obj, a->px16 / 16.0, a->py16 / 16.0, a->facing);
    return;
  }
  uint32_t L = 0x7E0000u + (uint32_t)lead, X = 0x7E0000u + (uint32_t)obj * 2;
  int px = r8(m, L + 0x1800) | (r8(m, L + 0x1801) << 8);               // 4366
  int py = r8(m, L + 0x1880) | (r8(m, L + 0x1881) << 8);               // 4370
  w8(m, X + 0x1800, (uint8_t)px); w8(m, X + 0x1801, (uint8_t)(px >> 8));   // 436B
  w8(m, X + 0x1880, (uint8_t)py); w8(m, X + 0x1881, (uint8_t)(py >> 8));   // 4375
  w8(m, X + 0x1900, 0); w8(m, X + 0x1901, 0);                          // 4378 STZ $1900,X (16-bit)
  uint8_t fac = r8(m, L + 0x1600);                                     // 437F
  w8(m, X + 0x1600, fac);                                              // 4384
  a->facing = fac & 3;                                                 // the C mirrors the renderer reads
  a->px16 = px; a->py16 = py; a->x = px >> 4; a->y = py >> 4; a->hasPos = 1;
  if (getenv("CT_FOLDBG"))
    fprintf(stderr, "[folload] obj%d <- lead $%04X: pos=(%.2f,%.2f) fac=%02X (ring cleared)\n",
            obj, 0x1800 + lead, px / 16.0, py / 16.0, fac);
}
// C0/3CD0: op 0xF0's timed arm (operand low nibble != 0):
//   3CDD STA $1C / 3CDF STA $1D     rate and countdown = the low nibble
//   3CE1..3CE9 LSR x4 / STA $1B     target brightness = the high nibble
//   3CEB LDA #02 / TSB $18          arm the ramp (the bit C0/0C76 tests and op 0xF2 waits on)
// (The immediate arm, low nibble 0, is `STA $19` alone and touches none of these.)
static void vmOpF0Arm(Mem* m, uint8_t v) {
  w8(m, 0x7E011C, v & 0x0F); w8(m, 0x7E011D, v & 0x0F);
  w8(m, 0x7E011B, (v >> 4) & 0x0F);
  w8(m, 0x7E0118, r8(m, 0x7E0118) | 0x02);
}
// The lead's arm inside the init pass. C0/42B8 stores the arrival into $1800/$1880 when the load op runs; a
// later `8B`/`8D` in the same fn0 writes the words again, and the last writer is where the location starts.
// The host places the lead at the arrival itself (field.c, after CtSceneCreate), so the arm's placement is
// recorded as "not placed by the script"; a later placement op sets hasPos again and field.c keeps it.
static void pcLeadInitArm(Mem* m, int obj, CtVmActor* a) {
  pcArrivalArm(m, obj, a);                                             // 42B8-430F
  a->hasPos = 0; a->px16 = a->py16 = -1; a->x = a->y = 0;              // the arrival is the host's to place
}
static void loadMonMirror(Mem* m, int obj, uint8_t id, uint8_t op2) {
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  int slot = (op2 & 0x7F) + 1;
  w8(m, X + 0x1101, id);
  w8(m, X + 0x1100, (op2 & 0x80) ? 6 : 5);
  w8(m, X + 0x1B01, 1);
  w8(m, X + 0x1200, 0x7F);
  if (gEnemyRecTab && id * 10 + 4 < gEnemyRecLen) w8(m, X + 0x1201, gEnemyRecTab[id * 10 + 4]);
  w8(m, X + 0x0C00, 0x20); w8(m, X + 0x0C01, 0x20);   // 4571 LDA #2020 / STA $0C00,X
  w8(m, X + 0x1600, 0x01);                            // 457E (op 0x83 enters that arm at C0/4557)
  w8(m, X + 0x1680, 0x00);                            // 4583
  w8(m, X + 0x1681, 0x00);                            // 4588
  w8(m, X + 0x0D00, (uint8_t)(((slot & 0xFE) << 4) | ((slot & 1) << 3)));
  if (gRomImg && gRomImgLen >= 0x250000)
    loadGfxMirror(m, obj, CtRomPtr(gRomImg, gRomImgLen, 0x24F600 + (long)id * 10, 10), id >= 0xF8);   // C0/477A: F8+ ROM-direct
}

// op 0xA8 face toward object[op/2] (ROM C0/55A0) / 0xA9 face toward PC[op]'s object (C0/5605):
// angle self->target via ABA2, facing = F700[angle]. PC0 = obj 1.
#define FACE_TARGET(op_, b1) ((op_) == 0xA8 ? (b1) / 2 : (b1) + 1)

// ---- The four staging sound ops, shared by the runtime step and the load's init pass ----
// $ED/$EE are not here: they are the two wait ops (their handlers do not advance the IP while the condition
// is unmet), and the init pass has no visit to yield, so runFunc steps over them.
static int vmSoundPostMem(Mem* mem, int blen, int startLine);
// One JSL of a sound op, accumulated: the frames it cost add up, the line it returned on is where the
// next JSL of the same visit begins (op 0xEB posts three in a row).
static int vmSoundPostAcc(Mem* mem, int blen, int* ln, int* fr) {
  int r = vmSoundPostMem(mem, blen, *ln);
  if (r < 0) return 0;
  *fr += r >> 10; *ln = r & 1023; return 1;
}
static int vmSoundStage(Mem* mem, int blen, const uint8_t* B, int addr, uint8_t op, int startLine) {
  uint8_t* m = mem->s;                             // $00:1E00 mirrors $7E:1E00
  int ln = startLine, fr = 0, seen = 0;            // the visit's raster line, the frames its JSLs cost
  switch (op) {
    case 0xE8:                                     // C0/3C11: play SFX
      m[0x1E01] = B[addr + 1]; m[0x1E02] = 0x80; m[0x1E00] = 0x18;
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);                   // $1E03 untouched: it persists
      break;
    case 0xEA:                                     // C0/3C2F: play song (+ the $29AE shadow)
      m[0x29AE] = B[addr + 1];
      m[0x1E01] = B[addr + 1]; m[0x1E00] = 0x10;
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);
      break;
    case 0xEB:                                     // C0/3C4C: three raw commands back to back
      m[0x1E01] = B[addr + 1]; m[0x1E02] = B[addr + 2]; m[0x1E03] = 0xFF; m[0x1E00] = 0x81;
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);
      m[0x1E01] = 0x00; m[0x1E02] = 0xFF; m[0x1E00] = 0x82;
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);
      m[0x1E01] = 0x00; m[0x1E02] = 0xFF; m[0x1E00] = 0x83;
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);
      break;
    case 0xEC:                                     // C0/3C98: raw escape, any command byte
      m[0x1E00] = B[addr + 1]; m[0x1E01] = B[addr + 2]; m[0x1E02] = B[addr + 3];
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);
      break;
  }
  return seen ? (fr * 1024 + ln) : -1;             // -1: no priced sink (ct.exe without audio, CT_NOAUDIO)
}

// The renderer's view of an object's scripted animation, derived from the bytes the anim family writes
// ($1780/$1781/$1680/$1681/$1301,X and $7F0B01,X); used by the WRAM seed and after every anim op, so the C
// fields always agree with the image. The mode is $1780,X alone, as the ROM's consumers branch: C0/CB3A picks
// $1781 when it is 2, else $1680; C0/CBF2 short-circuits mode 3 to $1301 (anim_drain_cad9.md sec. 4).
// $1680 = $FF is the 'no anim' marker C0/2F87 writes, surfaced as -1. animBusy is for C-side readers only; the
// VM's completion test is the byte, in CtVmAnimFamily (C0/2EA2).
static void ctAnimSyncActor(CtVmActor* a, const uint8_t* w, int x) {
  uint8_t am = w[0x1780 + x], aid = w[0x1781 + x], b01 = w[0x10B01 + x], a1680 = w[0x1680 + x];
  int idx = (a1680 == 0xFF) ? -1 : a1680;
  if (am == 3) { a->animMode = 3; a->animStatic = w[0x1301 + x]; a->animIndex = idx; }
  else if (am == 1) { a->animMode = 1; a->animIndex = a1680; a->animSeedFrame = (int)w[0x1681 + x]; }
  else if (am == 2) {
    a->animMode = 2; a->animIl = aid; a->animIndex = idx;
    if (b01 >= 2) a->animLoops = b01;                  // the armed count: 2 for 0xAB, operand+1 for 0xB7
    a->animSeedFrame = (int)w[0x1681 + x];             // the ROM step cursor (0 on a fresh arm)
  } else { a->animMode = 0; a->animIndex = a1680; }
  a->animBusy = (am == 2 && b01 >= 2) ? 1 : 0;
}

static void vmSetFacing(Mem* m, CtVmActor* a, int obj, int fac);   // fwd: $1600,X + the C field (defined with the move family)
// Op 0x8E (C0/4C74): the sprite-priority byte. `4C7C STA $0F80,X` stores the operand, then BIT #80 picks the
// arm. AUTO (bit 7, C0/4C83-4CB6): X = ($1881,X << 8) | $1801,X, the foot tile's index into the property planes;
// $0C01,X := (($7E7000,X & #40) >> 2) | #20 (via $D9, 4CA8) and $0C00,X := (($7E7040,X & #40) >> 2) | #20 (4CA3).
// FIXED (C0/4CB7-4CD4): $0C01,X := op & #30 (4CBB); $0C00,X := (op << 4) & #30 (4CC6). Both arms end
// `$0F01,X := #FF / STZ $1601,X`. $0F80 bits 2-3 pick the OAM write group (C0/B334), $0C00/$0C01 the two halves'
// OBJ priority (C0/CE84/CE8F); src/oam.c reads those bytes.
static void vmOp8E(Mem* m, CtVmActor* a, int obj, uint8_t v) {
  uint32_t X = (uint32_t)obj * 2;
  a->sprPrio = v;                                                 // the C read of the same byte
  w8(m, 0x7E0F80 + X, v);                                          // 4C7C
  uint8_t c00, c01;
  if (v & 0x80) {                                                  // AUTO: from the foot tile's properties
    uint32_t ti = ((uint32_t)r8(m, 0x7E1881 + X) << 8) | (uint32_t)r8(m, 0x7E1801 + X);
    c01 = (uint8_t)(((r8(m, 0x7E7000 + ti) & 0x40) >> 2) | 0x20);   // 4C8B-4C95 -> $D9 -> 4CA8
    c00 = (uint8_t)(((r8(m, 0x7E7040 + ti) & 0x40) >> 2) | 0x20);   // 4C97-4CA3
  } else {                                                         // FIXED
    c01 = (uint8_t)(v & 0x30);                                     // 4CB9-4CBB
    c00 = (uint8_t)((v << 4) & 0x30);                              // 4CBE-4CC6
  }
  w8(m, 0x7E0C00 + X, c00); w8(m, 0x7E0C01 + X, c01);
  w8(m, 0x7E0F01 + X, 0xFF); w8(m, 0x7E1601 + X, 0);               // 4CAB/4CB0 == 4CC9/4CCE
}

// The shared bodies with no C mirror, one dispatch for both runners of this file (step() and the init pass runFunc).
// Each is move.c's body (CtFieldRunnerOne calls the same function; --sweepops checks it against the ROM). The init
// runner C0/58ED dispatches through the same table C0/5D6E as the event runner and ignores the carry, so runFunc
// takes the same bodies (disasm/notes/live_vm_op_gaps.md). `B` is the body, `hdr` its header length (the ROM's IP
// is body offset + header). Returns 1 if the op was one of them (*h = IP, carry).
static int vmSharedOp(Mem* m, const uint8_t* B, int hdr, int addr, int obj, CtHRet* h) {
  uint8_t* r = m->s; const uint8_t* S = B - hdr; int ip = addr + hdr, X = obj * 2, n = (int)gRomImgLen;
  return CtVmResultFamily(r, gRomImg, n, S, ip, X, h) || CtVmObjQueryFamily(r, gRomImg, n, S, ip, X, h)
      || CtVmObjBranchFamily(r, gRomImg, n, S, ip, X, h) || CtVmPadBranchFamily(r, gRomImg, n, S, ip, X, h)
      || CtVmCaptionFamily(r, gRomImg, n, S, ip, X, h) || CtVmPalOp33(r, gRomImg, n, S, ip, X, h)
      || CtVmRandOp7F(r, gRomImg, n, S, ip, X, h) || CtVmScrollFamily(r, gRomImg, n, S, ip, X, h);
}
// Two shared bodies that have C mirrors here, so each runner calls these instead of vmSharedOp.
// 0x89/0x8A (C0/4B49 / 4B58): move.c's CtVmSpeedFamily writes $1A00,X; this runner's mover reads the C copy
// (mvComp), refreshed from the byte. `mspd` may be NULL (the init pass).
static void vmSpeedOp(Mem* m, CtVmActor* a, int* mspd, const uint8_t* B, int hdr, int addr, int obj) {
  CtHRet h; CtVmSpeedFamily(m->s, gRomImg, (int)gRomImgLen, B - hdr, addr + hdr, obj * 2, &h);
  a->moveSpeed = m->s[0x1A00 + obj * 2];
  if (mspd) *mspd = a->moveSpeed;
}
// 0x0A/0x0B/0x0C (C0/6254 / 6269 / 6282): move.c's CtVmObjEnableFamily writes the target's $1100/$1A81 (kill) or its
// $1000 bit 7 (the cadence pause the queue build C0/59EC and the activate guard C0/5AD1 read); the C fields follow.
// The carry is the ROM's: 0x0B on itself ends the visit (627A CPX $6D / 627E CLC). `mode` may be NULL (the init pass).
static void vmObjEnableOp(Mem* m, CtVmActor* actors, int nobj, int* mode, const uint8_t* B, int hdr, int addr, int obj,
                          CtHRet* h) {
  uint8_t op = B[addr]; int t = B[addr + 1] / 2;
  CtVmObjEnableFamily(m->s, gRomImg, (int)gRomImgLen, B - hdr, addr + hdr, obj * 2, h);
  if (t >= nobj) return;
  if (op == 0x0A) { actors[t].dead = 1; actors[t].draw = 1; actors[t].moveMode = 0x80; if (mode) mode[t] = 0x80; }
  else actors[t].cadPause = (m->s[0x1000 + t * 2] & 0x80) ? 1 : 0;
}

static void ctTileCopyQueue(CtScene* sc, int sl, int st, int el, int eb, int dl, int dt, int flags);   // fwd (needs CtScene)
static int g_initLastOp = -1;   // C0/5906 TXY: the offset of the last op the init runner dispatched (C0/56CB's Y)
// The object pass's CPU time. While a location load runs its scripts' init (C0/5709) and obj0's slot 1 (C0/595C),
// every op they dispatch is priced as the ROM runs it -- the runner's dispatch (CT_VM_DISPATCH) and the handler's arm
// on the WRAM it reads (field_cost.c ct_vm_op_cycles) -- into g_ctLzSpriteCycles, the load's object-pass clock.
// gOpCostWhy names the first op priced on a path the walk does not decide. The host turns pricing on
// (CtSceneLoadOpsPriced), independently of the sound sink: the ops are CPU work whether or not an SPC listens.
static int gOpCostOn = 0, gOpCostMode = 0;
void CtSceneLoadOpsPriced(int on) { gOpCostMode = on ? 1 : 0; }
static const char* gOpCostWhy = NULL;
static long gOpCostSum = 0; static int gOpCostN = 0;   // (the ops' share and count, for CT_LOADCOST)
static void vmOpCost(Mem* m, const uint8_t* B, int blen, int addr, int obj) {
  if (!gOpCostOn || !gRomImg) return;
  long c = CT_VM_DISPATCH + ct_vm_op_cycles(gRomImg, gRomImgLen, m->s, B, blen, addr, obj, &gOpCostWhy);
  g_ctLzSpriteCycles += c; gOpCostSum += c; gOpCostN++;
  if (getenv("CT_OPCOST_TRACE")) fprintf(stderr, "opcost obj%d @%04X op %02X %ld\n", obj, addr, B[addr], c);
}
const char* CtSceneLoadCostWhy(void) { return gOpCostWhy; }
long CtSceneLoadOpCycles(int* n) { if (n) *n = gOpCostN; return gOpCostSum; }
// The init pass (C0/58ED): runs object `obj`'s function at `start`, updating the actor states. Stops at the
// end-of-walk ops (00, AF, B0, B2).
static void runFunc(const uint8_t* B, int blen, int hdr, int obj, int nobj, int start, Mem* mem,
                    CtVmActor* actors, int* explore, int* bright, int* stopAddr, int* stopOp, CtScene* sc) {
  CtVmActor* a = &actors[obj];
  int addr = start, ops = 0, ps;
  CtHRet sh;
  if (stopAddr) { *stopAddr = -1; *stopOp = -1; }
  static int initDbg = -1;                            // CT_INITDBG=<obj>: trace the create-time fn0 walk
  if (initDbg < 0) { const char* e = getenv("CT_INITDBG"); initDbg = e ? atoi(e) : -1; }
  while (addr >= 0 && addr < blen && ops < 6000) {
    uint8_t op = B[addr];
    if (op != 0x00) g_initLastOp = addr;               // 5900 BEQ / 5906 TXY: every dispatched op
    if (op != 0x00) vmOpCost(mem, B, blen, addr, obj); // 5910 JSR ($5D6E,X): what it costs, before it runs
    if (initDbg == obj)
      fprintf(stderr, "[init obj%d] @%04X op=%02X (%02X %02X %02X)" "\n", obj, addr, op,
              addr + 1 < blen ? B[addr + 1] : 0, addr + 2 < blen ? B[addr + 2] : 0, addr + 3 < blen ? B[addr + 3] : 0);
    int ln = opLen(B, addr);
    if (addr + ln > blen) break;
    int nxt = addr + ln;
    // Only the 00 ends the ROM's init walk (5900 `LDA $7F2001,X / BEQ ->5915`); the loop ignores every handler's
    // carry (5913 BRA). B1's handler C0/353F is `TYX / INX / CLC`, so inside the walk it is a one-byte step, not a
    // stop. AF / B0 / B2 park in place (C0/3002, C0/3543 TYX / CLC; AF's INX after 3002 only for a non-PC).
    if (op == 0x00 || op == 0xAF || op == 0xB0 || op == 0xB2) {
      if (stopAddr) { *stopAddr = addr; *stopOp = op; }
      break;
    }
    // The bodies step() runs too (vmSharedOp, vmObjEnableOp, vmSpeedOp, and the item / party / tile-copy families):
    // C0/58ED dispatches every op through the event table (5910 JSR / 5913 BRA). An op with no arm here is stepped
    // over by its length (live_vm_op_gaps.md).
    else if (vmSharedOp(mem, B, hdr, addr, obj, &sh)) nxt = sh.ip - hdr;
    else if (op == 0x0A || op == 0x0B || op == 0x0C) {
      vmObjEnableOp(mem, actors, nobj, NULL, B, hdr, addr, obj, &sh);
    }
    else if (op == 0x89 || op == 0x8A) vmSpeedOp(mem, a, NULL, B, hdr, addr, obj);   // fn0 sets it then RETURNs (scene 13's kids `89 28`)
    else if (op == 0xB8) { w8(mem, 0x7E012B, B[addr + 1]); w8(mem, 0x7E012C, B[addr + 2]); w8(mem, 0x7E012D, B[addr + 3]); }   // C0/3557
    else if (op >= 0xC7 && op <= 0xCE && op != 0xC8) { int ni; CtVmItemFamily(mem->s, B - hdr, addr + hdr, &ni); nxt = ni - hdr; }
    else if (op == 0xCF || op == 0xD0 || op == 0xD1 || op == 0xD3 || op == 0xD4 || op == 0xD7 ||
             op == 0xF8 || op == 0xF9 || op == 0xFA) {
      CtHRet pg; pg.ip = addr + hdr; pg.carry = 1; pg.ok = 0;
      CtVmPartyGateFamily(mem->s, gRomImg, (int)gRomImgLen, B - hdr, addr + hdr, &pg);
      nxt = pg.ip - hdr;
    }
    else if (op == 0xE4) {                               // C0/3D97: the copy on the WRAM map, and the host's copy of it
      if (sc) ctTileCopyQueue(sc, B[addr + 1], B[addr + 2], B[addr + 3], B[addr + 4], B[addr + 5], B[addr + 6], B[addr + 7]);
      CtVmTileCopyE4(mem->s, B - hdr, addr + hdr);
    }
    // The four staging sound ops (fn0 can stage sound, e.g. loc 271's bell). Effect only; `nxt` is opLen's.
    else if (op == 0xE8 || op == 0xEA || op == 0xEB || op == 0xEC) (void)vmSoundStage(mem, blen, B, addr, op, 12);   // init pass: the cost is not consumed here
    else if (op == 0x10) nxt = addr + B[addr + 1] + 1;
    else if (op == 0x11) nxt = addr - B[addr + 1] + 1;
    else if (op == 0x12) { if (!cmp_(r8(mem, LOC + B[addr + 1] * 2), B[addr + 2], B[addr + 3])) nxt = addr + B[addr + 4] + 4; }
    else if (op == 0x13) { if (!cmp_(r16(mem, LOC + B[addr + 1] * 2), B[addr + 2] | (B[addr + 3] << 8), B[addr + 4])) nxt = addr + B[addr + 5] + 5; }
    else if (op == 0x14) { if (!cmp_(r8(mem, LOC + B[addr + 1] * 2), r8(mem, LOC + B[addr + 2] * 2), B[addr + 3])) nxt = addr + B[addr + 4] + 4; }
    else if (op == 0x15) { if (!cmp_(r16(mem, LOC + B[addr + 1] * 2), r16(mem, LOC + B[addr + 2] * 2), B[addr + 3])) nxt = addr + B[addr + 4] + 4; }
    // Op 0x16 = C0/63E6, five bytes `16 [idx] [val] [mode] [skip]`:
    //   63E8 LDA $7F2001,X -> $D9   the flag index          63EF -> $DB   the value/mask
    //   63F6 operand 3 = the mode; 63FA BPL picks the arm, and bit 7 set does 640A ORA #0100, so the flag
    //        comes from $7F:0100+idx instead of $7F:0000+idx.
    //   6433 JSR ($6467,X) with X = mode*2: eight comparators, CLC = holds / SEC = jump --
    //        6487 ==, 648F !=, 6497 >, 64A1 <, 64A9 >=, 64B5 <=, 64BF AND, 64C7 OR (C0/64B3 = CLC).
    //   6349, the jump arm, counts the skip from the skip byte: the target is addr+4+skip.
    else if (op == 0x16) { int li = B[addr + 1]; if (B[addr + 3] & 0x80) li += 0x100; if (!cmp_(r8(mem, GLB + li), B[addr + 2], B[addr + 3] & 0x7F)) nxt = addr + B[addr + 4] + 4; }
    else if (op == 0x18) { if (!(r8(mem, GLB) < B[addr + 1])) nxt = addr + B[addr + 2] + 2; }
    // 0x19/0x1C/0x1A (C0/653B / 654E / 6568) are in vmSharedOp above: the result word $7F0A80,X and the case on
    // its low byte.
    else if (op == 0xD2) {                               // party-membership test (C0/38E8 -> C2/8C36 mode 0):
      int pc = B[addr + 1], present = 0;                 // scan the 3 active-party char ids
      for (int k = 0; k < 3; k++) if (r8(mem, 0x7E2980 + k) == pc) { present = 1; break; }
      if (!present) nxt = addr + 2 + B[addr + 2];        // absent -> jump; present falls through (addr+3)
    }
    else if (op == 0x4F) w8(mem, LOC + B[addr + 2] * 2, B[addr + 1]);
    // The local INC/DEC/SET 0x71-0x77 (C0/6C69..6CF0) through move.c's CtVmLocalIncFamily, as step() does.
    else if (op == 0x71 || op == 0x72 || op == 0x73 || op == 0x75 || op == 0x76 || op == 0x77) {
      int ni = addr + hdr; CtVmLocalIncFamily(mem->s, B - hdr, addr + hdr, &ni);
    }
    // The variable grid, 0x50-0x56/0x58/0x59 (C0/68C6..6A11, local words little-endian), through move.c's
    // CtVmVarFamily, the same call step() makes.
    else if ((op >= 0x50 && op <= 0x56) || op == 0x58 || op == 0x59) {   // the whole grid (0x57 is a PC loader)
      int ni = addr + hdr;
      CtVmVarFamily(mem->s, B - hdr, addr + hdr, &ni);    // lengths = opLen's; nxt stays
    }
    else if (op == 0x5A) w8(mem, GLB, B[addr + 1]);
    // The pointer read 0x48/0x49 and the local arithmetic (C0/6792..67C7, C0/6A1F..6C68): the same calls step() makes.
    else if (op == 0x48 || op == 0x49) { int ni = addr + hdr; CtVmReadPtrFamily(mem->s, B - hdr, addr + hdr, &ni); }
    // ...and the poke family 0x4A-0x4D (C0/67D7..6829).
    else if (op >= 0x4A && op <= 0x4D) { int ni = addr + hdr; CtVmPokeFamily(mem->s, B - hdr, addr + hdr, &ni); }
    else if (op == 0x5B || op == 0x5E || op == 0x5F || op == 0x60 || op == 0x61 || op == 0x63 || op == 0x64 ||
             op == 0x67 || op == 0x69 || op == 0x6B || op == 0x6F) {
      int ni = addr + hdr;
      CtVmLocalArithFamily(mem->s, gRomImg, (int)gRomImgLen, B - hdr, addr + hdr, &ni);
    }
    else if (op == 0x65) { int li = B[addr + 2], bit = B[addr + 1]; if (bit & 0x80) li += 0x100; w8(mem, GLB + li, r8(mem, GLB + li) | (1 << (bit & 7))); }
    else if (op == 0x66) { int li = B[addr + 2], bit = B[addr + 1]; if (bit & 0x80) li += 0x100; w8(mem, GLB + li, r8(mem, GLB + li) & ~(1 << (bit & 7))); }
    else if ((ps = pcSprite(op)) >= 0) { a->sprite = ps; a->defined = 1; a->solid = 1;
      // $1100,X via the PC slot match C0/426A: char id ps vs the active party $7E2980/81/82
      // -> slot0 = 0 (the lead, AAFD smooth), slot1 = 1, slot2 = 2, not in the party = 0x80.
      a->moveMode = (ps == r8(mem, 0x7E2980)) ? 0 : (ps == r8(mem, 0x7E2981)) ? 1
                  : (ps == r8(mem, 0x7E2982)) ? 2 : 0x80;
      // $1100,X is the load op's own store, the first thing each arm of C0/426A does (429E slot 0 / 4312 / 43C5
      // `LDX $6D / STA $1100,X`, 4290 the not-in-party 80), so the object's own fn0 can read it back at once.
      w8(mem, 0x7E1100 + (uint32_t)obj * 2, (uint8_t)a->moveMode);
      pcLoaderIdStores(mem, obj, ps);                          // 41E4.. STA $8D+id / 4295-43D7 STA $1101,X
      // The in-party arms clear $1C80 (map + object collision) for all three slots (pcSlotArmCommon), so a
      // scripted lead move does not test the mover-solid plane. Only an in-party arm shows the object and brings
      // it back from a kill: the not-in-party arm C0/428C stores $1100 = 80 and $1101, never $1A81.
      if (a->moveMode != 0x80) { a->moveAnim = 1; a->draw = 0; a->dead = 0; pcSlotArmCommon(mem, obj, a); }
      if (a->moveMode != 0x80) { pcLoadMirror(mem, obj, ps);
        if (a->moveMode == 0) pcLeadInitArm(mem, obj, a); else pcFollowerArm(mem, obj, a); } }   // a PC's $1000 cadence = 1; + C0/448D's tables, then 42B8 (lead) / 4356 (follower)
    else if (op == 0x80) { uint8_t id = B[addr + 1]; a->sprite = id; a->defined = 1; a->solid = 1;
      // Op 0x80 (C0/421E), the party-aware PC loader: operand = char id, routed through C0/426A, which compares
      // $D9 with $7E2980/81/82 -> mode 0 (lead), 1, 2, else 0x80. The same slot match as the fixed-char ops.
      a->moveMode = (id == r8(mem, 0x7E2980)) ? 0 : (id == r8(mem, 0x7E2981)) ? 1
                  : (id == r8(mem, 0x7E2982)) ? 2 : 0x80;
      w8(mem, 0x7E1100 + (uint32_t)obj * 2, (uint8_t)a->moveMode);   // C0/426A's arms, as above (4290/429E/4312/43C5)
      pcLoaderIdStores(mem, obj, id);                          // 422A-4268 STA $8D+id / 4295-43D7 STA $1101,X
      if (a->moveMode != 0x80) { a->draw = 0; a->dead = 0; pcSlotArmCommon(mem, obj, a); }   // the three arms' prologue, all slots
      if (a->moveMode != 0x80) { a->moveAnim = 1; pcLoadMirror(mem, obj, id);
        if (a->moveMode == 0) pcLeadInitArm(mem, obj, a); else pcFollowerArm(mem, obj, a); } }   // PC $1000 cadence = 1; + C0/448D's tables (42B5/4329/43DC), then 42B8 (lead) / 4356 (follower)
    // Note: the NPC/monster loaders (C0/4476, C0/4559+4590, C0/46DF) do not touch the visibility byte $1A81;
    // its only indexed writers are the PC loader's in-party arms (C0/42A6/431A/43CD), C0/4CD9 (the
    // 0x90/0x91/0x7E/0x7C/0x7D tail) and C0/6264 (op 0x0A). So a preceding hide (op 0x91) stands.
    // They do rewrite $1100, which is all op 0x0A's kill changes besides $1A81 (C0/6254: $1100 = 80, $1A81 = 00),
    // so a load op after a kill puts the object back in the queue, still hidden. `dead` is the port's name for
    // that $1100 bit 7, so every load op clears it.
    else if (op == 0x81) { a->sprite = B[addr + 1]; a->defined = 1; a->solid = 1; a->moveMode = 3; a->dead = 0;   // op 0x81 C0/4476: $1100 = 3 (the NPC loader)
      w8(mem, 0x7E1100 + (uint32_t)obj * 2, 0x03);                                                                              // 4476 LDA #03 / 447A STA $1100,X
      npcLoadMirror(mem, obj, B[addr + 1], 3); a->facing = 1; }                                                                    // + gfx/palette allocator claims ($E4:F000 + id*5)
    else if (op == 0x82) { a->sprite = B[addr + 1] + 7; a->defined = 1; a->solid = 1; a->moveMode = 4; a->dead = 0; // op 0x82 C0/4559: $1100=4
      w8(mem, 0x7E1100 + (uint32_t)obj * 2, 0x04);                                                                              // 4559 STA $1100,X
      npcLoadMirror(mem, obj, B[addr + 1] + 7, 4); a->facing = 1;                                                                  // header $E4:F000 + (id+7)*5 (C0/45A8 E4F024,X = id*5)
      w8(mem, 0x7E0173, (r8(mem, 0x7E0173) + 1) & 0xFF); }                                                                         // 4665 / 46D9 INC $73, both arms
    else if (op == 0x83) { a->sprite = B[addr + 1] + 256; a->defined = 1; a->solid = 1; a->dead = 0;
      a->moveMode = (B[addr + 2] & 0x80) ? 6 : 5;      // op 0x83 C0/46DF: $1100=5 (via C0/4557), operand2
      loadMonMirror(mem, obj, B[addr + 1], B[addr + 2]); a->facing = 1; }   // bit7 -> 6 (C0/47EC-47F4); + the $7E per-object mirror
    else if (op == 0x8B) { a->x = B[addr + 1] * 16 + 8; a->y = B[addr + 2] * 16 + 16; a->hasPos = 1;
      a->px16 = B[addr + 1] * 256 + 0x80; a->py16 = B[addr + 2] * 256 + 0xFF;          // C0/4B7C-4B95
      vmPosWordsMirror(mem, obj, a->px16, a->py16);                                     // ...and its $1800/$1880 stores
      vmPlacePrio(mem, obj, B[addr + 1], B[addr + 2]); }                                // C0/4B98-4BBC
    else if (op == 0x8C) vmPos8C(mem, a, obj, B[addr + 1], B[addr + 2]);              // C0/4BC3 (see vmPos8C)
    else if (op == 0x8D) { int q0 = B[addr + 1] | (B[addr + 2] << 8), q1 = B[addr + 3] | (B[addr + 4] << 8);
      a->x = q0 >> 4; a->y = q1 >> 4; a->hasPos = 1; a->px16 = q0; a->py16 = q1;       // C0/4C31/4C3D: RAW words
      vmPosWordsMirror(mem, obj, q0, q1); vmPlacePrio(mem, obj, q0 >> 8, q1 >> 8); }   // C0/4C42-4C6C
    else if (op == 0x0F || op == 0x17 || op == 0x1B || op == 0x1D || op == 0xA6 || op == 0xA7 ||   // C0/5535..5593 ->
             op == 0x1E || op == 0x1F || op == 0x25 || op == 0x26) {                                  //   the 554B tail
      int ni, tg; CtVmFacingFamily(mem->s, B - hdr, addr + hdr, obj * 2, &ni, &tg);   // $1600 + STZ $1601 (move.c)
      if (tg / 2 < nobj) actors[tg / 2].facing = mem->s[0x1600 + tg] & 3; }          // the C mirror the renderer reads
    else if (op == 0x8E) vmOp8E(mem, a, obj, B[addr + 1]);   // C0/4C74: $0F80 + the $0C00/$0C01 halves
    // Op 0x84, C0/4867: `INY / TYX / LDA $7F2001,X / LDX $6D / STA $1B01,X`, the whole operand byte into the
    // solidity byte the object-solid scan reads (C0/5BDB). Nearly every `84` lives in fn0.
    else if (op == 0x84) { a->solid = B[addr + 1] & 1;
                           w8(mem, 0x7E1B01 + (uint32_t)obj * 2, B[addr + 1]); }   // C0/486F, whole byte
    // Op 0x0D writes the byte too: nearly every `0D` lives in fn0, and C0/5BA0 reads $1C80.
    else if (op == 0x0D) { a->coll = (B[addr + 1] >> 1) & 1; a->collMap = B[addr + 1] & 1;
      w8(mem, 0x7E1C80 + (uint32_t)obj * 2, B[addr + 1]); }      // C0/629F, the whole operand byte
    else if (op == 0x0E) { a->snap = B[addr + 1];
      w8(mem, 0x7E1C81 + (uint32_t)obj * 2, B[addr + 1]); }      // C0/62AE
    else if (op == 0x21 || op == 0x22) {               // GetObjCoords, C0/65AA / C0/65EB: an object's tile pair
      // -> vars op2/op3. 0x21 names the object by its offset; 0x22 by party slot (`LDA $97,X` = $0197 + op1,
      // DP = $0100) and BRAs into 0x21's body at 65B2: `TAX / LDA $1801,X / STA $D9 / LDA $1881,X / STA $DB`,
      // then two 8-bit stores into the slots' low bytes (65CE / 65E3), the X var first. The bytes are the WRAM
      // ones every placement arm has written, the lead's C0/42B8 arrival included.
      int X = (op == 0x21) ? B[addr + 1] : r8(mem, 0x7E0197 + B[addr + 1]);
      int tx = r8(mem, 0x7E1801 + (uint32_t)X), ty = r8(mem, 0x7E1881 + (uint32_t)X);
      w8(mem, LOC + B[addr + 2] * 2, tx);
      w8(mem, LOC + B[addr + 3] * 2, ty);
    }
    else if (op == 0xA8 || op == 0xA9) {               // face toward object / PC (C0/55A0/5605)
      int t = FACE_TARGET(op, B[addr + 1]);
      if (t >= 0 && t < nobj && actors[t].hasPos)
        vmSetFacing(mem, a, obj, mvTables()[0x400 + mvAngle(actors[t].x - a->x, actors[t].y - a->y)]);
    }
    // Op 0xE3, C0/3C05 `LDA $7F2001,X / STA $1F` (D = $0100): the byte is the op's whole effect, here as in step().
    else if (op == 0xE3) { if (explore) *explore = B[addr + 1] ? 1 : 0; w8(mem, 0x7E011F, B[addr + 1]); }
    else if (op == 0xF0) { if (bright) {                // screen fade (C0/3CD0): high nibble =
      int tg = (B[addr + 1] >> 4) & 0xF;                 // target, low = speed; speed 0 = instant
      if ((B[addr + 1] & 0x0F) == 0) { bright[0] = tg; bright[1] = tg; w8(mem, 0x7E0119, (uint8_t)tg); }   // 3CF9 STA $19
      else { bright[1] = tg; vmOpF0Arm(mem, B[addr + 1]); }   // armed: ticks toward the target (C0/1F24)
    } }
    else if (op == 0x87) {                              // script speed, C0/4876 (see the live
      int per = (B[addr + 1] + 1) & 0xFF;               //   stepper's arm)
      uint32_t A1000 = 0x7E1000u + (uint32_t)obj * 2;
      int v = (r8(mem, A1000) & 0x80) | per;
      w8(mem, A1000, v);
      w8(mem, A1000 + 1, v);                            // C0/488B: the countdown too
      a->moveAnim = per;
    }
    // Anim ops in fn0 set the object's standing anim (fn0 sets it and returns; the runtime never reaches it).
    else if (op == 0xAA || op == 0xAB || op == 0xAC || op == 0xAE || op == 0xB3 || op == 0xB4 || op == 0xB7) {
      // The handlers' stores ($1680/$1681/$1601/$1780/$1781/$1301,X, $7F0B01,X) through move.c's CtVmAnimFamily;
      // the renderer's C fields are derived from those bytes. The init pass ignores the carry (it never parks).
      CtHRet ah; ah.ip = addr; ah.carry = 1; ah.ok = 0;
      CtVmAnimFamily(mem->s, B, addr, obj * 2, &ah);
      ctAnimSyncActor(a, (const uint8_t*)mem, obj * 2);
    }
    else if (op == 0x90) a->draw = 0;
    else if (op == 0x91) a->draw = 1;
    else if (op == 0x7E) a->draw = 2;
    else if (op == 0x7C) { int t = B[addr + 1] / 2; if (t < nobj) actors[t].draw = 0; }
    else if (op == 0x7D) { int t = B[addr + 1] / 2; if (t < nobj) actors[t].draw = 1; }
    else if (op == 0x2E) {                             // C0/4A4E: the shared body (move.c CtVmColorSlotOp) arms the $0520
      CtHRet cs; cs.ip = nxt; cs.carry = 1; cs.ok = 0; //   record / copies the blob; this runner ignores the carry
      CtVmColorSlotOp(mem->s, B - hdr, addr + hdr, &cs);
      nxt = cs.ip - hdr; }                             // (the ROM's IP is $7F:2001-relative: body offset + header)
    else if (op == 0x88) {                             // C0/4892: the shared body (move.c CtVmColorFx) arms the $0520
      CtHRet cf; cf.ip = nxt; cf.carry = 1; cf.ok = 0; //   record / uploads the blob / restores; this runner ignores the
      CtVmColorFx(mem->s, gRomImg, (int)gRomImgLen, B - hdr, addr + hdr, obj * 2, &cf);   // carry (C0/5910 / 5913)
      nxt = cf.ip - hdr;                               // (the ROM's IP is $7F:2001-relative: body offset + header)
      uint8_t c = B[addr + 1]; if (c == 0) a->colorFx = 0; else if (c >= 0x20 && c < 0x60) a->colorFx = 1; }  // + the C mirror
    addr = nxt; ops++;
  }
}

int CtVmResolveActors(const uint8_t* raw, int rawLen, const uint8_t* gmem, CtVmActor* out, int maxActors) {
  if (rawLen < 1) return 0;
  int count = raw[0];
  if (count > maxActors) count = maxActors;
  int header = raw[0] * 32;                          // full header (use raw count for ptrs)
  if (1 + header > rawLen) return 0;
  const uint8_t* body = raw + 1 + header;
  int blen = rawLen - 1 - header;
  Mem mem; memInit(&mem, gmem);
  // No previous location filled the arrival bytes $0100-$0104 here, and the lead's load op takes its facing
  // from $0104 & 3 (C0/42B8). The convention, as for a `--scene N` cold start: facing 1, what the loader tail
  // leaves. A script that sets its own facing after the load still wins.
  w8(&mem, 0x7E0104, 1);
  for (int n = 0; n < count; n++) {
    out[n].sprite = -1; out[n].x = 0; out[n].y = 0; out[n].px16 = -1; out[n].py16 = -1; out[n].facing = 1;
    out[n].draw = 0; out[n].dead = 0; out[n].defined = 0; out[n].hasPos = 0; out[n].result = 0;
    out[n].solid = 0; out[n].colorFx = 0; out[n].colorGated = 0;   // solid=1 only on load-sprite (below)
  }
  CtVramSinkFn sink = g_ctVramSink;                  // a probe on its own copy of the memory: its DMAs (op 0x29's
  g_ctVramSink = NULL;                               //   font) must not reach the host's VRAM
  for (int n = 0; n < count; n++) {
    int slot0 = (raw[1 + n * 32] | (raw[1 + n * 32 + 1] << 8)) - header;   // ptr rebased to body
    if (slot0 >= 0 && slot0 < blen)
      runFunc(body, blen, header, n, count, slot0, &mem, out, NULL, NULL, NULL, NULL, NULL);
  }
  g_ctVramSink = sink;
  return count;
}

// ---- Live scene scheduler (engine.py tick / _step / run_tasks / _move_op is the Python reference) ----
typedef struct { int addr; int ret[8]; int prio, dc, tb, wait;
                 int waitObj, waitPrio, nm, callAddr; } CtSS;  // addr<0 = idle; waitObj>=0 = a 0x04/0x07
                                                     // call-and-wait until the callee finishes.
                                                     // callAddr = body addr of the in-flight CALL op (the ROM
                                                     // keeps $1180 on the call; the port advances past it).
                                                     // nm = blocked on a name-entry screen (op 0xC8)
// The caller's wait latch is $7F0980,X, one byte per object, read only by op 0x04/0x07 (C0/6109 `LDX $6D / LDA
// $7F0980,X / BEQ ->611A` the fresh arm, else BRL ->61A7 the poll arm). The install parks $1180 on the call's own
// byte (61A4 `TYX / CLC`), so the poll runs only while the IP sits there. A higher-priority call that pre-empts the
// waiter (C0/6168-616D: `$7F0580,X` := $1180) runs its function with the latch still 1; the RETURN lands back on the
// 04, which polls again. waitObj is the port's latch; "parked" = the IP is just past the call (the port advances
// past it), so a pre-empted waiter runs its new function and polls only after coming back.
static int ssParked(const CtSS* s) { return s->waitObj >= 0 && s->callAddr >= 0 && s->addr == s->callAddr + 3; }
// What a pre-empt pushes into the return slot: the ROM's $1180,X (C0/6168 / 6005 / 60CC, the invokes C0/5AC5 /
// 3154, C0/5B41), which for a parked waiter is the 04/07's own byte. The RETURN then lands on the call and the op
// decides on the latch (C0/6109): still set -> the poll; cleared (the pre-empting function ran a 04/07 of its own)
// -> a fresh install.
static int ssPushAddr(const CtSS* s) { return ssParked(s) ? s->callAddr : s->addr; }
// Per-frame, transient, outside the struct: the debug savestate writes CtScene raw, so its layout must not move.
static uint8_t g_ranB0[64];                   // op 0xB0 / 0xAF dispatched in this frame's pass (C0/3002 ran)
static uint8_t g_landedB0[64];                // this frame's visit ended on a B0 it did not dispatch (no conga)
struct CtScene {
  uint8_t* raw; int rawLen, nobj, header, blen;
  const uint8_t* body;
  Mem* mem;        // not owned unless ownMem; see CtSceneCreateIn
  int ownMem;
  CtVmActor actors[64];
  CtSS st[64];
  double fx[64], fy[64], mx[64], my[64];     // float position + step velocity (px/frame)
  int mspd[64];                               // $1A00 move speed (op 0x89; engine default 0x10)
  int coll[64];                               // $1C80,X bit1 = object-collision gate (op 0x0D; C0/5B95):
                                              // a mover with it set is blocked by other solid objects
  int collMap[64];                            // $1C80,X bit0 = map-collision gate (op 0x0D; ACFD $7E7040)
  int c1C81[64];                              // $1C81,X (op 0x0E): when != 0, op 0x96 snaps to the tile
                                              // (C0/305D). Default 0.
  int touchHit;                               // $01EB, the touch hit latch: C0/881E resets it to 0x80 every
                                              // frame, the C0/9923 proximity scan stores the hit actor's index,
                                              // op 0xB0's lead branch (C0/3054) consumes it and fires C0/3154.
                                              // -1 = 0x80. Set by the host (CtSceneSetTouchHit): the scan runs
                                              // on the player side before the event VM, as in C0/881E.
  int blockCtr_UNUSED;                        // unused; the ROM's $F8 lives at $7E01F8 (mvRandAngle)
  int (*tileSolid)(void* ctx, int tx, int ty);// map solidity hook (field.c): 1 = (tx,ty) is a solid wall
  void* tileCtx;
  int cycles[64], hasTask[64];                // cycles = the $1A01 step ticket (frames left)
                                              // hasTask = a per-PC walk target, checked every frame by
                                              // run_tasks (as the ROM's walk blocks on $7F03FC every frame),
                                              // so a follower stops the moment it enters the target tile.
  // The camera (field_camera_unification.md part G). The VM keeps the DP=$1D00 scroll ($1D0A/$1D0E coarse +
  // $1D93/$1D96 phase = camPx) every frame via CtSceneCameraTick (99DE gate -> 91AC seek/auto -> 93E1 commit).
  // These are this frame's input from field.c, set via CtSceneCameraInput before CtSceneTick: camReqX/Y = the
  // post-collision scroll request (1/16 px signed, 881E's $1D2E/$1D30 after 88EE+8A6D), 0 when not exploring or
  // blocked; camLeadX/Y = the lead's foot world position (px) at the start of the frame, before the host moves
  // it (the 99DE deadzone reads these).
  int camReqX, camReqY, camLeadX, camLeadY, camHaveLead;
  int camAutoWarned;                          // one-shot log guard for the dormant $01BC auto-scroll (open)
  uint8_t atB0[64];                           // this object's visit last ran op 0xB0 (C0/3002, where the lead's
                                              // C0/9E84 runs) -- CtSceneActorAtB0
  int mode[64];                               // $1100,X per-object move mode (the C0/A819 dispatch key): <3 ->
                                              // AAFD smooth (lead/follower PCs), 3..6 -> AA07 $7F0B00 lifecycle
                                              // (scripted NPCs/sparkles), 7 camera, 0x80 idle. Seeded from
                                              // actors[].moveMode at create, overridden from WRAM in the seed.
  int b00[64];                                // $7F0B00,X mirror (signed; C0/AA07 lifecycle, AA1B-AA27):
                                              // ==0 -> clamp this frame (per-axis tile-edge stall, zero the
                                              // ticket); >0 -> clear to 0 + free-integrate (the step-issue
                                              // frame, 4FDA/5071/52B0/54E5); <0 (0x80) -> leave + free (vector
                                              // ops, 4EFA arms #80). Re-armed by moveOp on each step issue.
  int vecMove[64];                            // 1 = the active move is a vector move (op 0x9C/0x9D): the ROM
                                              // handler has no C0/5614 re-anim, so the actor slides in its
                                              // current pose and the renderer must not play the walk cycle
  int moveAngle[64];                          // op 0x96's stored heading: the ROM (C0/4F61, cadence 1) re-issues
                                              // the step every frame but keeps the velocity until an axis
                                              // reaches its target tile, then re-aims for the remaining axis.
                                              // -1 = no heading yet (recompute).
  // Party conga line: the ROM's velocity ring (disasm/notes/party_follower_ring.md), kept at the ROM's addresses:
  // $7F:0C00 (velX) / 0C80 (velY) / 0D00 + 0D80 (the pre-adjust velocity pair, which picks the facing) / 0E00 +
  // 0E80 (the two OAM priority flags) / 0F00 (the terrain-shape byte), 0x80 entries. Write head $7E:01AB, read
  // cursors $7E:01AC (member 2) and $7E:01AD (member 3), chained: member 3's gap is measured against member 2's
  // cursor. The PC loader clears all three. The host fields below are what the ROM keeps elsewhere or render state:
  int folLeadP[2], folLeadSeen;               // the lead's last-pushed 12.4 position: the push is gated on the
                                              // lead having moved, which is not the threshold's $1D32|$1D33 test
                                              // (see the push in CtSceneUpdateFollowers)
  int folMoving[64];                          // 1 = the follower stepped this frame (drives its walk anim)
  int folRoamPrev[64];                        // was this follower roaming last frame? (1 at init/placement so
                                              // a follower that roams from the start is not treated as a resume)
  int regroupFollowers;                       // unused; kept so the debug scene states (CtSceneSaveState)
                                              // keep their layout
  struct { int active, start, span, rate, ctr, phase; } palRot[64];  // op 0x88 cmd 0x20, palette rotation
                                              // (the Telepod swirl): rotate `span` sprite-palette colours from
                                              // `start` by one every `rate` frames (slot $0520, C0/4926)
  struct { int start, n; uint16_t bgr[16]; } palBlob[64];  // op 0x88 cmd 0x80+, blob: overwrite `n` BGR555
                                              // palette colours from `start` (= cmd low nibble)
  int irisState;               // $39: 0 = idle, 1 = opening (op 0xFF 0x90), 2 = closing (op 0xFF 0x91). Its
                               // own state machine, independent of the swirl's op-0x88 colorFx.
  int textbox;                 // 1 = a dialogue box is up (blocks the showing object)
  int tbCloseArmed;            // C0/2081 set $0129 = 0x0D this frame; C0/2032 runs from the next
  int tbPageArmed;             // C0/2086 set $0129 = 8 this frame (a pressed page); C0/1FB4 runs from the next
  int tbPendChoice;            // a decision box's picked line, held until the op's completion visit
                               //   writes it into result (C0/365D) -- see CtSceneChoose
  int tbTable, tbIndex;        // current dialogue string-table file-addr + string index
  int tbTableAtShow;           // table snapshot taken when the box first shows (op time, not display)
  int tbChoice, tbObj;         // 1 if the open box is a decision/choice box; the object that opened it
  int tbOp;                    // the opening opcode (0xBB auto / 0xC1 top / 0xC2 bottom / 0xC0,C3,C4 choice)
  int tbOpt;                   // decision op operand2: $64=(>>2)&3 first option line, $65=&3 last (-1 = none)
  int naming, nameChar;        // op 0xC8 operand 0xC0+id: a name-entry screen is up for char id nameChar
  int loader, loaderArg;       // op 0xC8 odd operand < $80: the mode-0 load/new-game shell is up
                               //   (C0/0D00 -> C2/8000 -> screen $0E). loaderArg = the raw operand.
  int shop, shopArg;           // op 0xC8 operand $80-$BF: menu mode 2 = the shop (screen $0C); shopArg = the operand
  int warpReq;                 // op 0xFF operand $80-$8F: the bank-$FD gate warp (C0/3FE4-3FFC)
  int ffFade;                  // op 0xFF < $90: 1 = its own fade loop (C0/3FC1-3FD1) took its first `DEC $19` in the
                               //   op's frame -- the host runs the rest before the jump (CtSceneFfFading)
  int rebootReq;               // op 0xDF/0xE1 (C0/3B98/3B4B): 1 = staged, the machine waits for the vblank and
                               //   reboots through C0/0000; 3 = op FF 96 (C0/4181): the same wait, then the reset
                               //   entry FD/C000; 2 = the host took it. Either way the main loop is dead.
  int seqReq;                  // op 0xFF operand < $80: a $C3 special sequence was requested
                               //   (2 = the title screen). The script never resumes.
  int seqHas;                  // 1 = seqReq holds a request. Needed because the operand can be 0
                               //   (`FF 00` asks for table entry 0, the race; sequence_table.md
                               //   sec. 7.1), and the scene is zero-initialised, so -1 cannot be
                               //   the "none" sentinel.
  int tcQ[16][8]; int tcN;     // scripted tile copies for the host: sl,st,el,eb,dl,dt,flags (ctTileCopyQueue)
  // The song-upload freeze (song_load_freeze.md sec. 6). A sound op's `JSL $C7:0004` is synchronous: the main
  // loop parks until the SPC has echoed the last unit, while the NMI keeps counting frames. The sink prices the
  // JSL; the tick then suspends after the posting visit and resumes in the frame the JSL returns in, with the same
  // queue (the ROM's queue was built before the JSL). The runner's raster gate C0/5A5D (`CMP $69`) reads the fixed
  // virtual beam V = 0 (see docs/DECISIONS.md, the zelda3 model), so the return frame runs the rest of the queue.
  int sndWait;                 // frames still to lose before the resume tick
  int sndResumeN;              // the queue index to resume at
  int sndPosted, sndFreezeFrames, rasterLine;   // per-visit: a priced post happened; frames it cost; the line it returned on
  int sndEndLine, sndFrozenTick;   // the resume line; this tick returned early (the host's per-frame legs must not run)
  int head117Set; uint8_t head117;  // $0117 as C0/0C76 read it at the frame's head (CtSceneHeadLatch), for the drain
  int exitCommitted;                // C0/0C9A committed the pending change: the main loop is left (CtSceneExitCommit)
  uint8_t sndElig[64];         // the suspended tick's eligibility snapshot
  uint8_t sndOrder[64]; int sndNq, sndResumeObj;   // ...its queue order, and the object whose visit the JSL interrupted
  // The tail overrun (song_load_freeze.md sec. 6). The main loop syncs on the NMI with clear-then-wait semantics
  // (C0/EC60), entered after the frame's legs, so one more frame is lost when `return line + SND_TAIL_LINES >= 225`.
  // SND_TAIL_LINES = 61, the normal tail measured on the gate cutscene; the legs' real cost varies, so returns near
  // the boundary can go either way.
  int sndPostResumeFreeze;     // the tick after the resume is lost too (the tail overran the NMI)
  int sndOpAddr, sndSavedAddr; // the posting op's address, and the ip the op advanced to: the ROM stores the
                               // object's ip when the visit ends (after the JSL returns), so for the frames
                               // lost its ip still reads the op's own address
  // The $0117 bit-5 service, C0/1770-188B (disasm/notes/field_tile_copy_e5.md). C0/0C76 -> C0/16DC -> C0/1770 takes
  // a bit-5 request (op 0xE5's, or op 0xD8's with bit 0) at the start of the frame after the op and holds the main
  // loop for several frames in its own EC60 waits, so no WRAM byte records where it is; the port keeps it here:
  // 1 = the align loop C0/1781-178E (885A + a whole event frame per step until $0138 clears; that frame then runs
  // `1790 JSR C0/AF4E` and FD/FFF7), 2 = the style branch's frames (C0/179A on $0145: only C0/00DE runs), 3 = the
  // frame the branch returns into (C0/00A7 on: 881E .. 00BF, without C0/0097, C0/18D9 and C0/0C76).
  int xfer, xferStyle, xferStep;
  int xferOp[7];               // $013E-$0144 as C0/1790's AF4E used them (the style frame's upload redraws that rect)
  int xferNmi46;               // $0146 was set by a style step: the next NMI (C0/EAD1) clears it
  int bgD2x, bgD2y, bgD3x, bgD3y;  // BG2/BG3 build origins minus BG1's (render model; see CtSceneLayerOffsets)
  int explore;                 // op 0xE3: 0 = player control locked (cutscene), 1 = free
  int moveLock;                // $0162 != 0: the player movement controller (C0/881E, which also runs the
                               // party trail) is locked even when explore/$011F is 1 (e.g. $0162 = 0x80)
  uint16_t btnHeld;            // $00F2 held joypad (A=0x80 X=0x40 L=0x20 R=0x10 B=0x08 Y=0x04 Sel=0x02
                               // St=0x01). Ops 0x34-0x39 (C0/66C6+) read the WRAM byte; this copy is
                               // --vmdiff's CtSceneSetButtons bookkeeping.
  uint8_t btnPrev;             // last frame's $00F2, so CtSceneSetButtons can derive the
                               // newly pressed $00F0 that ops 0x3F-0x44 latch and consume.
  int bright, brightTarget;    // op 0xF0 screen fade: 0..15 (INIDISP); armed fades step +-1/frame,
                               // holding while tile copies are pending
  int loadFadeIn;              // the host is inside the location load's fade-in ramp, C0/2848 (called once
                               // from C0/0094, between the load and the first main-loop frame). Its body is
                               // `INC $19 / [STZ $1F] C0/881E / C0/00BF / C0/EC60`: it runs the 00BF legs
                               // (scheduler C0/59D9, C0/5A46, camera C0/A810, anim drain C0/CA76) and none of
                               // the main loop's own: no joypad latch (C0/0097), no box machine (C0/1F87),
                               // no C0/0C76 (so no $0119 fade ramp and no $0117 drain).
  int locChange;               // the pending change committed (C0/0C9A, CtSceneExitCommit): the main loop
                               // has left for C0/0000 and the host loads $0100. Only the commit raises it;
                               // op 0xDD/0xDE (C0/3B1B) write $0100-$0104 and move nobody (see the op).
  int lcLoc, lcX, lcY, lcFacing;  // destination location id (word&0x1FF), tile x/y, facing 0-3
                               // (= (hibyte>>1)&3: LDA $DA / LSR / AND #$0F -> $04)
  int lcStyle;                 // always 0 (read back from the committed words).
                               // Op 0xE0 (C0/3AB7), the fade variant, stages into $0112-$0116, latches
                               // $0117 bit7, arms the $0119 = 0x0F fade counter and parks its object on the
                               // op; the CtSceneTick drain (C0/0C8A) counts $0119 down and raises locChange
                               // at 0.
  int playerLoaded;            // 1 iff some object's init loads a PC. The ROM keeps the lead's object
                               // offset in $0197 and leaves it at 0x80 when no object loads one (e.g. the
                               // monster-only cutscene maps 92 and 240). playerObj still falls back to 1 so
                               // every PC_i = playerObj+i reference stays in range; this flag tells the
                               // renderer whether there is a player at all.
  int playerObj;               // cached lead-PC object index (op 0x57 slot 0). The party loads into
                               // consecutive objects from here (PC_i = playerObj+i); the renderer draws
                               // this object as the player.
  int rgBase;                  // body offset of the ROM's battle-return regroup block (ct_regroup.h ctRegroupReturn,
                               // C0/5CEC's copy at $7F3700 = IP 16FF). The post-victory resume (CtSceneBattleReturn)
                               // and op 0xDA VM-CALL each follower onto it; its 0x95 walk-to-lead + 0x71/0x12
                               // barrier + E3 01 restore control. -1 = none.
  int d9Base;                  // body-offset of op 0xD9's first walk program ($7F3720 = IP 171F; slot k at +0x20*k).
};
// The followers, as the ROM finds them: the party slots $0199 / $019B (the object * 2; bit 7 = empty), slot 1
// first and slot 2 only when slot 1 is filled (C0/345F / 34E2 for op DA, C0/0355 / 036D for the battle return).
// The object init (C0/426A) and the party ops (D4, C0/394E) keep the bytes in WRAM. Not the move mode: a member
// who has left the party keeps it. Returns the count, the objects in fol[].
static int ctPartyFollowers(CtScene* sc, int fol[2]) {
  int s1 = r8(sc->mem, 0x7E0199), s2 = r8(sc->mem, 0x7E019B), nf = 0;
  if (!(s1 & 0x80) && (s1 >> 1) < sc->nobj) {
    fol[nf++] = s1 >> 1;
    if (!(s2 & 0x80) && (s2 >> 1) < sc->nobj) fol[nf++] = s2 >> 1;
  }
  return nf;
}

// $1001,X, the cadence countdown. The queue build tests and decrements it (C0/59F0-59F6), every enqueue arm
// reloads it from the period $1000,X (C0/5A1F / 5A2B / 5A40), op 0x87 re-arms it (C0/488B) and nine routines
// STZ it (C0/31AA, 3264, 3301, 3386, 340D, 34D9, 352C, 5B1B, 5B5F) to mean "eligible next build". `st[].dc`
// shadows it.
// Not closed: the period lives in `moveAnim`, not in $1000; the fresh-spawn default 4 is never stored, so
// $1000 is authoritative only on a WRAM-seeded scene.
static void ctSetDc(CtScene* sc, int obj, int v) {
  sc->st[obj].dc = v;
  w8(sc->mem, 0x7E1001u + (uint32_t)obj * 2, v & 0xFF);
}

// Hand a scripted tile copy to the host (CtScenePollTileCopy): the render's copy of the map (SceneData.l1t..) and
// the field's collision planes, which the port keeps outside WRAM. `flags` = the $0144 mask, plus CT_TC_RENDER for
// the picture only or CT_TC_COLL for the planes only (op 0xE5's two halves land on different frames: the planes at
// C0/1790's AF4E, the tilemaps at the style branch's upload).
static void ctTileCopyQueue(CtScene* sc, int sl, int st, int el, int eb, int dl, int dt, int flags) {
  if (sc->tcN >= 16) return;
  int* q = sc->tcQ[sc->tcN++];
  q[0] = sl; q[1] = st; q[2] = el; q[3] = eb; q[4] = dl; q[5] = dt; q[6] = flags; q[7] = 0;
}

static int fpPtr(CtScene* sc, int obj, int fn) {
  if (obj < 0 || obj >= sc->nobj || fn < 0 || fn >= 16) return -1;
  return (sc->raw[1 + obj * 32 + fn * 2] | (sc->raw[1 + obj * 32 + fn * 2 + 1] << 8)) - sc->header;
}

// C0/FE00, the ROM's random table (op 0x7F, AE2D): from the container (CT_ASSET_TABLE), else the ROM dump
#define kRandAngle(i) (CtRomViewHas(0x00FE00) ? CtRomByte(NULL, 0, 0x00FE00 + (i)) : CtRomImage(NULL)[0x00FE00 + (i)])
static int mvRandAngle(Mem* m);
// Memory and actor effects of the non-control ops, mirroring runFunc; coordinate ops also move the live float
// position. Used by step() for the main-loop body.
static void applyEffect(CtScene* sc, int obj, int addr, uint8_t op) {
  CtVmActor* a = &sc->actors[obj]; const uint8_t* B = sc->body; Mem* m = sc->mem; int ps;
  { dbgFlags(); if (gEnvTrace && obj == gEnvTraceObj)   // CT_VMTRACE: trace one object's ops
      fprintf(stderr, "  [vm obj%d blen=%d rawloc=%d story=%d] @%04X op=%02X (%02X %02X %02X)\n", obj,
              sc->blen, m->s[0x0100] | (m->s[0x0101] << 8), m->g[0], addr, op, B[addr+1], B[addr+2], B[addr+3]); }
  if (op == 0x8E) { vmOp8E(sc->mem, a, obj, B[addr + 1]); return; }   // C0/4C74 (see vmOp8E)
  if (op == 0xF0) {                                      // screen fade (see runFunc)
    int tg = (B[addr + 1] >> 4) & 0xF;
    if ((B[addr + 1] & 0x0F) == 0) { sc->bright = tg; w8(m, 0x7E0119, (uint8_t)tg); }   // 3CF9 STA $19
    else vmOpF0Arm(m, B[addr + 1]);                      // 3CDD-3CED: the timed arm's four stores
    sc->brightTarget = tg;
    return;
  }
  // Op 0x87, script speed, C0/4876 (m=1 x=0):
  //   4876: INY / TYX / LDA $7F2001,X      ; the operand
  //   487C: LDX $6D / INC A / STA $D9      ; $D9 = operand + 1, in 8-bit (0xFF wraps to 0)
  //   4881: LDA $1000,X / AND #80          ; keep only the pause bit
  //   4886: ORA $D9 / STA $1000,X / STA $1001,X
  //   488E: TYX / INX / SEC / RTS          ; IP +2, SEC = the runner keeps going
  // The second store (C0/488B) re-arms the live countdown: the queue build (C0/00C1) runs before the runner
  // (C0/00C4), so the countdown was already reloaded from the old period this frame and the new one must
  // replace it. A carries the pause bit into both stores, unmasked, as in the ROM.
  if (op == 0x87) {
    int per = (B[addr + 1] + 1) & 0xFF;
    uint32_t A1000 = 0x7E1000u + (uint32_t)obj * 2;
    int v = (r8(m, A1000) & 0x80) | per;
    w8(m, A1000, v);                                    // C0/4881-4888
    w8(m, A1000 + 1, v);                                // C0/488B  STA $1001,X: re-arm the countdown
    if (sc && obj >= 0 && obj < sc->nobj) sc->st[obj].dc = v;   // keep the shadow in step
    a->moveAnim = per;
    return;
  }
  if (op == 0x84) { a->solid = B[addr + 1] & 1;               // $1B01,X solidity (C0/4867)
                    w8(sc->mem, 0x7E1B01 + (uint32_t)obj * 2, B[addr + 1]); return; }   // C0/486F, whole byte
  if (op == 0x22) {                                           // GetObjCoords (C0/65EB): PC[op1]'s
    // The operand is a party slot into $0197. C0/65EB is op 0x21's handler with one instruction inserted:
    // `65EF LDA $7F2001,X / 65F3 TAX / 65F4 LDA $97,X / 65F6 BRA ->65B2`, where 65B2 is 0x21's `TAX / LDA
    // $1801,X`. So 0x21 takes a raw object offset and 0x22 the offset at $0197 + operand (DP = $0100); the
    // party objects need not be consecutive. The operand is the already-doubled slot index.
    // The tile is the high byte of the words as the pass finds them (C0/65B2-65BA), i.e. last frame's commit:
    // the frame's own step lands later, at C0/A810 (C0/00D3). The host's new position goes to fx/fy before the
    // tick and reaches the WRAM words only at the tick's A810 mirror, so these bytes match the ROM's.
    int po = vmSlotTarget(m, op, B[addr + 1]) / 2;            // tile x/y -> op2/op3
    if (po >= 0 && po < sc->nobj) {
      uint32_t X2 = (uint32_t)po * 2;
      int wx = r8(m, 0x7E1800 + X2) | (r8(m, 0x7E1801 + X2) << 8), wy = r8(m, 0x7E1880 + X2) | (r8(m, 0x7E1881 + X2) << 8);
      w8(m, LOC + B[addr + 2] * 2, (wx >> 8) & 0xFF);
      w8(m, LOC + B[addr + 3] * 2, (wy >> 8) & 0xFF);
    }
    return;
  }
  if (op == 0x21) {                                           // GetObjCoords (C0/65B0): object[op1/2]'s tile
    int po = B[addr + 1] / 2;                                 // x/y -> locals op2/op3. Operand = a direct obj
    if (po >= 0 && po < sc->nobj) {                           // index*2 (not a PC slot like 0x22). Reads the
      w8(m, LOC + B[addr + 2] * 2, ((int)sc->fx[po] >> 4) & 0xFF);   // live fx/fy, not actors[].x (fixed at
      w8(m, LOC + B[addr + 3] * 2, ((int)sc->fy[po] >> 4) & 0xFF);   // spawn for movers).
    }
    return;
  }
  if (op == 0xA8 || op == 0xA9) {                            // face toward object / PC (C0/55A0/5605):
    // The two handlers differ by one instruction and share a body: 0xA8 is `55A4 LDA $7F2001,X / 55A8 TAX` (the
    // operand is the object offset), 0xA9 is `5609 LDA $7F2001,X / 560D TAX / 560E LDA $97,X / 5610 TAX / BRL
    // ->55A9` (the offset at $0197 + operand), as move.c's ctVmPartySlotTarget reads it. The init runner keeps
    // FACE_TARGET (PC0 = obj1), the Python VM's convention.
    int off = (op == 0xA8) ? B[addr + 1] : vmSlotTarget(m, op, B[addr + 1]);
    // 55A9 `LDA $1100,X / BMI ->5600`: the target's move-mode bit 7 declines the op, for 0xA8 too (shared body).
    // Not an emptiness test: an empty slot's 0x80 makes the ROM read $7E:1180, which happens to be negative.
    if (r8(m, 0x7E1100 + (uint32_t)off) & 0x80) return;
    // 55AE-55E4: the heading from the tile bytes `$1801/$1881` (the target's, then its own), or, on the same tile,
    // from the low bytes `$1800/$1880`; 55E6 JSR C0/ABA2 (an unsigned compare per axis: mvAngle's signs), then
    // 55F0-55F8 `LDA $F700,X / STA $1600,X / STZ $1601,X`: the frame timer restarts on every visit.
    {
      uint32_t T = (uint32_t)off, S = (uint32_t)obj * 2;
      int f2 = r8(m, 0x7E1801 + T), f3 = r8(m, 0x7E1881 + T), f0 = r8(m, 0x7E1801 + S), f1 = r8(m, 0x7E1881 + S);
      if (f1 == f3 && f0 == f2) {
        f0 = r8(m, 0x7E1800 + S); f1 = r8(m, 0x7E1880 + S); f2 = r8(m, 0x7E1800 + T); f3 = r8(m, 0x7E1880 + T);
      }
      vmSetFacing(sc->mem, a, obj, mvTables()[0x400 + mvAngle(f2 - f0, f3 - f1)]);
      w8(m, 0x7E1601 + S, 0);
      ctAnimSyncActor(&sc->actors[obj], sc->mem->s, (int)S);
    }
    return;
  }
  if (op == 0x4F) w8(m, LOC + B[addr + 2] * 2, B[addr + 1]);
  // Memory-init ops (scene 0's bootstrap among their users). 0x48/0x49 (C0/6792/67C7) and 0x4A-0x4D
  // (C0/67D7..6829) are not here: step() hands them to move.c's CtVmReadPtrFamily / CtVmPokeFamily.
  else if (op == 0x4E) {                           // MemCopyLiteral (MVN): copy lenword-2 literal bytes
    uint32_t dst = (uint32_t)B[addr + 1] | ((uint32_t)B[addr + 2] << 8) | ((uint32_t)B[addr + 3] << 16);
    int cnt = (B[addr + 4] | (B[addr + 5] << 8)) - 2;       // source = body[addr+6..]; dest e.g. $7E2980 party
    for (int k = 0; k < cnt; k++) w8(m, dst + (uint32_t)k, B[addr + 6 + k]);
  }
  // Ops 0x89/0x8A set $1A00 (the fair race's speeds come from these, driven by op 0x7F's random draws).
  else if (op == 0x89 || op == 0x8A) vmSpeedOp(m, a, &sc->mspd[obj], B, sc->header, addr, obj);   // C0/4B49 / 4B58
  // The local INC/DEC/SET 0x71-0x77 (C0/6C69..6CF0), 8- and 16-bit: move.c's CtVmLocalIncFamily.
  else if (op == 0x71 || op == 0x72 || op == 0x73 || op == 0x75 || op == 0x76 || op == 0x77) {
    int ni; CtVmLocalIncFamily(m->s, B - sc->header, addr + sc->header, &ni);
  }
  else if (op == 0x5A) w8(m, GLB, B[addr + 1]);
  // The variable grid 0x50-0x59 and the local arithmetic 0x5B-0x6F (C0/6A1F..6C68, the rubber-band's 0x6F shift
  // included) are not here: step() hands them to move.c's CtVmVarFamily / CtVmLocalArithFamily.
  else if (op == 0x65) { int li = B[addr + 2], bit = B[addr + 1]; if (bit & 0x80) li += 0x100; w8(m, GLB + li, r8(m, GLB + li) | (1 << (bit & 7))); }
  else if (op == 0x66) { int li = B[addr + 2], bit = B[addr + 1]; if (bit & 0x80) li += 0x100; w8(m, GLB + li, r8(m, GLB + li) & ~(1 << (bit & 7))); }
  // C0/6299 `LDA $7F2001,X / LDX $6D / STA $1C80,X`: the whole operand byte. $1C80,X is the storage that
  // C0/5BA0 reads to gate the object-solid test; sc->coll/collMap are decoded copies.
  else if (op == 0x0D) { uint8_t v = B[addr + 1];
    sc->coll[obj] = (v >> 1) & 1; sc->collMap[obj] = v & 1;
    sc->actors[obj].coll = sc->coll[obj]; sc->actors[obj].collMap = sc->collMap[obj];
    w8(m, 0x7E1C80 + (uint32_t)obj * 2, v); }                    // C0/629F
  else if (op == 0x0E) { sc->c1C81[obj] = B[addr + 1];           // C0/62AE STA $1C81,X, same shape
    w8(m, 0x7E1C81 + (uint32_t)obj * 2, B[addr + 1]); }
  else if ((ps = pcSprite(op)) >= 0) { a->sprite = ps; a->defined = 1;
    a->moveMode = (ps == r8(sc->mem, 0x7E2980)) ? 0 : (ps == r8(sc->mem, 0x7E2981)) ? 1
                : (ps == r8(sc->mem, 0x7E2982)) ? 2 : 0x80; sc->mode[obj] = a->moveMode;   // $1100 at the live load site too
    pcLoaderIdStores(m, obj, ps);                                                         // 41E4.. STA $8D+id / 4295-43D7 STA $1101,X
    w8(m, 0x7E1100 + (uint32_t)obj * 2, a->moveMode);                                     // $7E mirror (battle transfer reads it)
    if (a->moveMode <= 2) w8(m, 0x7E0197 + (uint32_t)a->moveMode * 2, obj * 2);           // party-slot -> obj-X table (C0/426A)
    if (a->moveMode <= 2) { sc->coll[obj] = 0; sc->collMap[obj] = 0; a->coll = 0; a->collMap = 0;
      w8(m, 0x7E1C80 + (uint32_t)obj * 2, 0); }
    if (a->moveMode != 0x80) { a->draw = 0; a->dead = 0; pcLoadMirror(m, obj, ps);   // in-party only (see the init site)
      if (a->moveMode == 0) pcArrivalArm(m, obj, a);
      else { pcFollowerArm(m, obj, a); sc->fx[obj] = a->px16 / 16.0; sc->fy[obj] = a->py16 / 16.0; } } }   // STZ $1C80,X in all three party arms (C0/42A1 / 4315 / 43C8)
  else if (op == 0x80) { uint8_t id = B[addr + 1]; a->sprite = id; a->defined = 1;   // party-aware PC loader (C0/421E->426A); see init site
    a->moveMode = (id == r8(sc->mem, 0x7E2980)) ? 0 : (id == r8(sc->mem, 0x7E2981)) ? 1
                : (id == r8(sc->mem, 0x7E2982)) ? 2 : 0x80; sc->mode[obj] = a->moveMode;
    pcLoaderIdStores(m, obj, id);                                                         // 422A-4268 STA $8D+id / 4295-43D7 STA $1101,X
    w8(m, 0x7E1100 + (uint32_t)obj * 2, a->moveMode);                                     // $7E mirror
    if (a->moveMode <= 2) w8(m, 0x7E0197 + (uint32_t)a->moveMode * 2, obj * 2);
    if (a->moveMode <= 2) { sc->coll[obj] = 0; sc->collMap[obj] = 0; a->coll = 0; a->collMap = 0;
      w8(m, 0x7E1C80 + (uint32_t)obj * 2, 0); }
    if (a->moveMode != 0x80) { a->draw = 0; a->dead = 0; pcLoadMirror(m, obj, id);   // in-party only (see the init site)
      if (a->moveMode == 0) pcArrivalArm(m, obj, a);
      else { pcFollowerArm(m, obj, a); sc->fx[obj] = a->px16 / 16.0; sc->fy[obj] = a->py16 / 16.0; } } }   // STZ $1C80,X in all three party arms (C0/42A1 / 4315 / 43C8)
  // No `draw` write: C0/4476 / C0/4559 / C0/46DF do not touch $1A81 (see the init-runner arms above).
  else if (op == 0x81) { a->sprite = B[addr + 1]; a->defined = 1; a->moveMode = 3; sc->mode[obj] = 3; a->dead = 0;
    npcLoadMirror(m, obj, B[addr + 1], 3); a->facing = 1; }         // NPC loader (C0/4476) + allocator claims
  else if (op == 0x82) { a->sprite = B[addr + 1] + 7; a->defined = 1; a->moveMode = 4; sc->mode[obj] = 4; a->dead = 0;
    npcLoadMirror(m, obj, B[addr + 1] + 7, 4); a->facing = 1; }
  else if (op == 0x83) { a->sprite = B[addr + 1] + 256; a->defined = 1; a->dead = 0;
    a->moveMode = (B[addr + 2] & 0x80) ? 6 : 5; sc->mode[obj] = a->moveMode;   // C0/47EC: op2 bit7 -> mode 6
    loadMonMirror(m, obj, B[addr + 1], B[addr + 2]); a->facing = 1; }                          // + the $7E per-object mirror (staging reads it)
  // The ROM stores X at the tile centre ($1800 low = 0x80, e.g. tile 26 -> 0x1A80 = 424.0) and Y at the foot
  // ($1880 low = 0xFF = anchor - 1/16), so only Y gets the -1/16 foot adjust.
  else if (op == 0x8B) { a->x = B[addr + 1] * 16 + 8; a->y = B[addr + 2] * 16 + 16; a->hasPos = 1; sc->fx[obj] = a->x; sc->fy[obj] = a->y - 0.0625;
    a->px16 = B[addr + 1] * 256 + 0x80; a->py16 = B[addr + 2] * 256 + 0xFF;
    vmPosWordsMirror(m, obj, a->px16, a->py16);                                    // C0/4B74's $1800/$1880 stores
    vmPlacePrio(m, obj, B[addr + 1], B[addr + 2]); }                               // C0/4B98-4BBC
  else if (op == 0x8C) { vmPos8C(m, a, obj, B[addr + 1], B[addr + 2]);           // C0/4BC3 (see vmPos8C)
    sc->fx[obj] = a->px16 / 16.0; sc->fy[obj] = a->py16 / 16.0; }
  // Op 0x8D (C0/4C27), the raw-word setter: `LDA $7F2001,X / STA $1800,X`, then the same into $1880,X; the two
  // 1/16 px words are stored unchanged. Unlike op 0x8B it has no foot anchor to apply (a -1/16 here would cross a
  // tile boundary on a tile-aligned word, which the C0/A947 camera-window test reads).
  else if (op == 0x8D) { int w8x = B[addr + 1] | (B[addr + 2] << 8), w8y = B[addr + 3] | (B[addr + 4] << 8);
    sc->fx[obj] = w8x / 16.0; sc->fy[obj] = w8y / 16.0; a->px16 = w8x; a->py16 = w8y;
    a->x = (int)sc->fx[obj]; a->y = (int)sc->fy[obj]; a->hasPos = 1;
    vmPosWordsMirror(m, obj, w8x, w8y);                                            // C0/4C31 / 4C3D, the raw words
    vmPlacePrio(m, obj, w8x >> 8, w8y >> 8); }                                     // C0/4C42-4C6C
  else if (op == 0x0F || op == 0x17 || op == 0x1B || op == 0x1D || op == 0xA6 || op == 0xA7 ||   // C0/5535..5593 -> the
           op == 0x1E || op == 0x1F || op == 0x25 || op == 0x26) {                                  //   554B tail (move.c)
    int ni, tg; CtVmFacingFamily(m->s, B - sc->header, addr + sc->header, obj * 2, &ni, &tg);
    if (tg / 2 < sc->nobj) sc->actors[tg / 2].facing = m->s[0x1600 + tg] & 3; }
  // SHOW / HIDE / REMOVE share one tail, C0/4CD9, which writes $1A81,X:
  //   op 0x90 SHOW   C0/4CD5: LDX $6D / LDA #01 / STA $1A81,X / TYX / INX / CLC / RTS
  //   op 0x91 HIDE   C0/4CE0: LDX $6D / LDA #00 / BRA ->4CD9
  //   op 0x7E REMOVE C0/4CE6: LDX $6D / LDA #80 / BRA ->4CD9
  // $1A81 is the script visibility flag that the activation pass tests (C0/A894 `LDA $1A81,X / BEQ ->A910 /
  // BMI ->A910`: hidden or removed -> not activated). $0F00 is the per-frame latch A810 derives from it and the
  // camera window. The port's `draw` holds the same three states (0 shown / 1 hidden / 2 removed).
  else if (op == 0x90 || op == 0x91 || op == 0x7E) {
    a->draw = (op == 0x90) ? 0 : (op == 0x91) ? 1 : 2;
    w8(sc->mem, 0x7E1A81 + (uint32_t)obj * 2, (op == 0x90) ? 0x01 : (op == 0x91) ? 0x00 : 0x80);
  }
  // The cross-object show/hide use the same C0/4CD9 tail and write the target's $1A81: op 0x7C = C0/4CEC
  // (LDA #01), op 0x7D = C0/4CF9 (INY / TYX / LDA $7F2001,X / TAX -- the operand is the target index -- /
  // LDA #00 / BRA ->4CD9).
  else if (op == 0x7C || op == 0x7D) {
    int t = B[addr + 1] / 2;
    if (t < sc->nobj) { sc->actors[t].draw = (op == 0x7C) ? 0 : 1;
                        w8(sc->mem, 0x7E1A81 + (uint32_t)t * 2, (op == 0x7C) ? 0x01 : 0x00); }
  }
  // Op 0x88, colour effect (C0/4892): cmd 0x20-0x5F allocates the per-object effect slot (sets $7F0B80,X),
  // cmd 0 closes it. The blob form (0x80-0x8F) only copies palette data; it does not set the ref.
  else if (op == 0x88) {
    uint8_t c = B[addr + 1];
    if (c == 0) {                 // close (C0/48C0): clear the slot ref + restore the palette.
      a->colorFx = 0;
      sc->palRot[obj].active = 0; sc->palBlob[obj].n = 0;   // every object stops cycling now (C0/48C0 has
                                  // no exception)
      // The WRAM side of the close is the shared body's (move.c CtVmColorFx): C0/48C0 frees the record the ref
      // names and re-uploads the object's 24-byte $E4 record (colours 1-12) into both CGRAM pages, colours 13-15
      // untouched, only when the ref $7F0B80,X is not negative (48C8 BMI).
    }
    else if (c >= 0x80 && c < 0x90) {                              // blob (C0/49F8-4A43): inline palette colours.
      // The blob is a CGRAM-shadow write per palette slot, for any object. C0/49F8: $DB = 0x80 + ($0F81,X >> 1) * 16
      // (the slot's first CGRAM entry), $DD = 2 * ($DB | (cmd & 0x0F)); the script word at ip+2 minus 2 = the byte
      // count; MVN copies the bytes from the script (ip+4) to $7E:2200+$DD (4A25-4A34) and again to $7E:2000+$DD
      // (4A35-4A43). Every object sharing the slot sees the colours. The two MVNs are the shared body's (move.c
      // CtVmColorFx); this arm keeps the C mirror.
      int start = c & 0x0F, len = B[addr + 2] | (B[addr + 3] << 8), nbytes = len - 2;
      if (nbytes < 0) nbytes = 0;
      int ncol = nbytes / 2;                                        // the C mirror the swirl's clip/retention path reads
      if (start + ncol > 16) ncol = 16 - start;
      sc->palBlob[obj].start = start; sc->palBlob[obj].n = ncol;
      for (int i = 0; i < ncol; i++) sc->palBlob[obj].bgr[i] = B[addr + 4 + i * 2] | (B[addr + 4 + i * 2 + 1] << 8);
    }
    else if (c >= 0x20 && c < 0x60) {                              // ref/SET: arm the slot
      a->colorFx = 1;
      // C0/4892: cmd 0x20/0x30 BEQ ->C0/4919 (the rotate arm); cmd&0xF0 == 0x40 BRL ->C0/4970 (a colour ramp,
      // not ported in this C mirror). The arm at 4919 allocates the slot for any object, PC or NPC. p1 = B[addr+2]
      // packs start (p1>>4) and span (p1&0x0F); p2 = B[addr+3] = rate (frames per step). Start is the colour index
      // within the sprite's 16-colour palette. The per-frame processor FD/E532 rotates span+1 colours left.
      if (c == 0x20 || c == 0x30) {
        uint8_t p1 = B[addr + 2], p2 = B[addr + 3];
        sc->palRot[obj].start = (p1 >> 4); sc->palRot[obj].span = (p1 & 0x0F);
        sc->palRot[obj].rate = p2 ? p2 : 1; sc->palRot[obj].ctr = 0; sc->palRot[obj].phase = 0;
        sc->palRot[obj].active = (p1 & 0x0F) > 1;
      }
    }
  }
}

// ---- Mover (C0/4E73-5215 handlers, C0/ABA2 angle, C0/ACFD step) ----
// Position units: 1/256 tile (16 units = 1 px). A step lasts $1A01 frames (the ticket), each frame adding
// (mspd * comp[angle]) / 256 units per axis, comp = the C0/F800 sine table (signed, |127|; X reads
// sin[(angle+0x40) & 0xFF], see mvComp). Target-tile ops re-aim and start a new step each script visit with
// ticket = the actor's $1000 script speed; 92/9C take the ticket from the operand.
static const uint8_t* mvTab;                       // [0x400 atan32x32][0x100 face][0x140 sin]
static uint8_t mvFallback[0x640];
void CtSceneSetMoverTables(const uint8_t* blob, int len) { if (blob && len >= 0x640) mvTab = blob; }
static const uint8_t* mvTables(void) {
  if (mvTab) return mvTab;
  for (int dy = 0; dy < 32; dy++)                  // fallback when ct_assets.dat lacks them:
    for (int dx = 0; dx < 32; dx++)                // formula tables (within 1 LSB of the ROM)
      mvFallback[dy * 32 + dx] = (dx | dy) ? (uint8_t)(int)floor(atan2(dy, dx) * 128.0 / CTPI + 0.5) : 0x40;
  for (int i = 0; i < 0x100; i++) mvFallback[0x400 + i] = (i < 0x20) ? 3 : (i < 0x60) ? 1 : (i < 0xA0) ? 2 : (i < 0xE0) ? 0 : 3;
  for (int i = 0; i < 0x140; i++) {
    double v = (i < 0x100 ? 127.0 : 63.5) * sin((i & 0xFF) * CTPI / 128.0);
    mvFallback[0x500 + i] = (uint8_t)((int)floor(v + 0.5) & 0xFF);
  }
  mvTab = mvFallback;
  return mvTab;
}
// Note: a second port of C0/ABA2. The other, CtMoveDirAngle (src/oam.c), is what move.c calls and what
// --sweepaba2 checks: it reads the bytes the callers stage at $01F0-$01F3, models the routine's WRAM side
// effects and branches on an unsigned borrow. This one takes deltas already computed in the tile domain
// (`tgtTile - curTile`, see the op-0xA0 arm). The suite covers it indirectly; no check compares the two ports.
static int mvAngle(int dx, int dy) {               // C0/ABA2: quadrant split + 32x32 atan
  const uint8_t* T = mvTables();
  int base, fx_, fy_;
  if (dy >= 0) {
    if (dx >= 0) { base = 0x00; fx_ = dx; fy_ = dy; }
    else { base = 0x40; fx_ = dy; fy_ = -dx; }     // quadrant swaps mirror the octant
  } else if (dx < 0) { base = 0x80; fx_ = -dx; fy_ = -dy; }
  else { base = 0xC0; fx_ = -dy; fy_ = dx; }
  int sh = ((fx_ | fy_) & 0x40) ? 2 : ((fx_ | fy_) & 0x20) ? 1 : 0;   // range reduction
  int idx = (((fy_ >> sh) & 0x1F) << 5) | ((fx_ >> sh) & 0x1F);
  return (base + T[idx]) & 0xFF;
}
static int mvComp(const uint8_t* T, int M, int idx) {   // signed units/frame, truncated:
  int v = T[0x500 + (idx & 0xFF)];                       // cos(a) = sin[(a+0x40) & 0xFF]: the ROM wraps the cosine
  // index mod 256. Note: the 0x140-long table's tail (0x100..0x13F) is a half-rate sine, not the cosine, and
  // must not be read here.
  return (v < 0x80) ? (M * v) >> 8 : -((M * (0x100 - v)) >> 8);  // the ROM keeps only the product's high byte
}
// The step issue: the velocity pair from the heading, the ticket, and the facing (`LDA $F700,X / STA $1600,X`).
// The registers are committed later by mvIssueRegs.
static void mvStartStep(CtScene* sc, int obj, int angle, int ticket, int setFace) {
  const uint8_t* T = mvTables();
  sc->mx[obj] = mvComp(T, sc->mspd[obj], angle + 0x40) / 16.0;   // X: sin[a+0x40] (cosine)
  sc->my[obj] = mvComp(T, sc->mspd[obj], angle) / 16.0;          // Y: sin[a]; 16 units = 1px
  sc->cycles[obj] = ticket > 0 ? ticket : 1;
  if (setFace) vmSetFacing(sc->mem, &sc->actors[obj], obj, T[0x400 + angle]);   // 4FB5 LDA $F700,X / STA $1600,X
}
// The step issue's registers, committed once on the velocity ACFD finally publishes (not per attempt): $1900/$1980
// (C0/AF1F/AF24; a walled step stores zero), then the op's `JSR C0/5614`, the idle/walk/run selector on
// |$1900|+|$1980| that every move op runs after ACFD (13 sites: 4EC0 4FA5 4FE3 5037 507D 50E4 5122 527B 52B9
// 53C4 5409 54B0 54EE). It writes $1680 = 1 (walk) with $1681 = $1601 = 0 unless already walking, or $1680 = 0
// when the velocity is zero. `anim` = 0 for op 0xA0, whose issue path has no 5614.
// mvIssuePrio: ACFD's OAM priority pair (AD85-ADE8 / AE06-AE25 / AED5-AF15) on the published velocity: the cell of
// the tentative position ($1800 + $DD, $1880 + $DB, the high bytes, `AND $1D1E` 16-bit), then with $0F80 bit 7 (AUTO)
// the two property planes -- not crossed like the placement ops': $0C01 from $7E7040, $0C00 from $7E7000
// (ADBD-ADD2) -- else the byte's nibbles ($0C01 = p & 30, $0C00 = (p << 4) & 30, ADD7-ADE8). A zero velocity never
// reaches the block (AD73 BEQ ->ADEB) and a walled one stores zero past it (AF27).
static void mvIssuePrio(CtScene* sc, int obj, int vx, int vy) {
  if (!vx && !vy) return;
  Mem* m = sc->mem;
  uint32_t x = (uint32_t)obj * 2;
  int wx = r8(m, 0x7E1800u + x) | (r8(m, 0x7E1801u + x) << 8);
  int wy = r8(m, 0x7E1880u + x) | (r8(m, 0x7E1881u + x) << 8);
  int tx = (wx + (int)(int8_t)(vx & 0xFF)) & 0xFFFF, ty = (wy + (int)(int8_t)(vy & 0xFF)) & 0xFFFF;   // AD85-AD91
  uint32_t cell = ((uint32_t)((ty >> 8) & 0xFF) << 8) | (uint32_t)((tx >> 8) & 0xFF);             // AD93-AD97
  cell &= (uint32_t)(r8(m, 0x7E1D1E) | (r8(m, 0x7E1D1F) << 8));                                     // AD99
  int p = r8(m, 0x7E0F80u + x);                                                                     // ADB6
  if (p & 0x80) {
    w8(m, 0x7E0C01u + x, (uint8_t)(((r8(m, 0x7E7040u + cell) & 0x40) >> 2) | 0x20));   // ADBD-ADC7
    w8(m, 0x7E0C00u + x, (uint8_t)(((r8(m, 0x7E7000u + cell) & 0x40) >> 2) | 0x20));   // ADCA-ADD2
  } else {
    w8(m, 0x7E0C01u + x, (uint8_t)(p & 0x30));                                            // ADDB-ADDD
    w8(m, 0x7E0C00u + x, (uint8_t)((p << 4) & 0x30));                                     // ADE2-ADE8
  }
}
static void mvIssueRegs(CtScene* sc, int obj, int anim) {
  uint32_t x = (uint32_t)obj * 2;
  int vx = (int)(sc->mx[obj] * 16.0), vy = (int)(sc->my[obj] * 16.0);
  mvIssuePrio(sc, obj, vx, vy);
  w8(sc->mem, 0x7E1900u + x, (uint8_t)(vx & 0xFF)); w8(sc->mem, 0x7E1980u + x, (uint8_t)(vy & 0xFF));
  if (anim) { CtVmMoveStepAnim(sc->mem->s, (int)x); ctAnimSyncActor(&sc->actors[obj], sc->mem->s, x); }
}
// C0/FE00 (all 256 bytes): the random angle table ACFD reads when a mover hits a solid map tile, its random
// bounce off walls (AE27). AE2B `LDX $F8` under SEP #$10 indexes all 256 entries.
// The cursor `$F8` is $7E01F8 (DP = $0100 in the field engine, as for $0168 and $0171):
//   AE27  SEP #10
//   AE29  INC $F8          <- the cursor advances first
//   AE2B  LDX $F8
//   AE2D  LDA $FE00,X      <- so the index is the post-increment value
// A never-reset global shared by the C0/ACFD wall bounce and op 0x7F (C0/6D09), seeded at each load from the
// play clock (C0/0C70), so it lives in the WRAM image.
static int mvRandAngle(Mem* m) {
  int c = (r8(m, 0x7E01F8) + 1) & 0xFF;                  // AE29 INC $F8 (8-bit: SEP #10 / M 8-bit)
  w8(m, 0x7E01F8, (uint8_t)c);
  return kRandAngle(c);                                  // AE2D LDA $FE00,X
}
// ---- C0/5B95, the object-solid test (objects only; it reads no map tile) ----
// A mover with collision enabled ($1C80 bit1) is blocked when another solid object sits in the box ahead.
// The gates are exported because --sweepsolid checks C0/5B95 against these functions.
//   5B95: STZ $EA / LDX $6D
//   5B99: LDA $0F00,X / BNE ->5BA0      the mover's activation latch; 0 -> 5B9E CLC / RTS
//   5BA0: LDA $1C80,X / BIT #02 / BEQ   the object-collision bit; clear -> the same CLC / RTS
//   5BA9: LDA $1600,X / ASL A / STA $D9 the heading, from the facing byte
//   5BBD: LDA $7F2000 / ASL A / TAX     the scan starts at the last object and walks down
//   5BC3: LDA $0F00,X / BNE ->5BD2      the candidate's activation latch
//   5BD5: LDA $1100,X / BMI ->5BC8      class bit7 = an unloaded slot
//   5BD7: CPX $6D / BEQ ->5BC8          itself
//   5BDB: LDA $1B01,X / BIT #01 / BEQ   not solid
//   5BE6: JSR ($5BF2,X)                 the per-heading distance box; carry set = blocked
int CtVmSolidMoverGate(const uint8_t* ram, int mover2) {
  if (ram[0x0F00 + mover2] == 0) return 0;                  // 5B99
  return (ram[0x1C80 + mover2] & 0x02) != 0;                // 5BA0
}
int CtVmSolidCandGate(const uint8_t* ram, int mover2, int cand2) {
  if (ram[0x0F00 + cand2] == 0) return 0;                   // 5BC3
  if (ram[0x1100 + cand2] & 0x80) return 0;                 // 5BD5
  if (cand2 == mover2) return 0;                            // 5BD7
  return (ram[0x1B01 + cand2] & 0x01) != 0;                 // 5BDB
}
// C0/5BFA / 5C20 / 5C47 / 5C6D, the per-heading distance box. `5BE6 JSR ($5BF2,X)` picks one of the four by
// `$1600,X * 2` (the facing byte, not the move angle). `$DB`/`$DD` hold the mover's $1800/$1880, latched
// 16-bit at 5BB1/5BB6.
//
//   5BFA up     LDA $1880,X / SBC $DD / BPL rej / EOR #FFFF / CMP #0100 / BCS rej   then the X axis
//   5C20 down   LDA $1880,X / SBC $DD / BMI rej /             CMP #0100 / BCS rej   then the X axis
//   5C47 left   LDA $1800,X / SBC $DB / BPL rej / EOR #FFFF / CMP #0100 / BCS rej   then the Y axis
//   5C6D right  LDA $1800,X / SBC $DB / BMI rej /             CMP #0100 / BCS rej   then the Y axis
//   cross axis  LDA .. / SBC .. / BPL +3 / EOR #FFFF / CMP #0100 / BCS rej
//   5C43 rej    SEP #20 / CLC / RTS        accept: SEP #20 / SEC / RTS
//
// Three asymmetries, kept as the ROM has them:
//  (1) The primary axis is half-open and the two halves differ by one. On the up/left arms the reject is
//      `EOR #FFFF` -> `CMP #0100`, and ~A = |A|-1, so it accepts |d| <= 0x100; on down/right there is no EOR
//      and it accepts d <= 0x00FF. The cross axis has the same split from its own `BPL +3 / EOR`.
//  (2) The first SBC runs on the carry left by `5BD7 CPX $6D` (nothing between touches C), so C = (cand >
//      mover): a lower-index candidate measures one subpixel further away.
//  (3) The second SBC always runs with C = 0 (the last flag-setter is the primary axis's `CMP #0100` with its
//      BCS not taken), so the cross-axis delta is always -1.
// $1800/$1880 are 12.4 fixed point: #0100 = 16.0 px. The core takes the coordinates and the carry, so the live
// mover can pass its own position and --sweepsolid the ROM's $1800/$1880, through one copy of the rule.
int CtVmSolidBoxAt(int mx, int my, int cx, int cy, int carry0, int heading) {
  int primaryY = (heading <= 1);                                    // 0 up / 1 down are the Y arms
  int wantPos  = (heading == 1 || heading == 3);                    // down / right require d >= 0
  int p = primaryY ? cy : cx, pm = primaryY ? my : mx;
  int q = primaryY ? cx : cy, qm = primaryY ? mx : my;
  uint16_t a = (uint16_t)(p - pm - (1 - carry0));                   // the first SBC
  if (wantPos) { if (a & 0x8000) return 0; }                        // BMI -> 5C43
  else         { if (!(a & 0x8000)) return 0;                       // BPL -> 5C43
                 a = (uint16_t)~a; }                                // EOR #FFFF
  if (a >= 0x0100) return 0;                                        // CMP #0100 / BCS -> 5C43
  uint16_t b = (uint16_t)(q - qm - 1);                              // (3): the second SBC, carry 0
  if (b & 0x8000) b = (uint16_t)~b;                                 // BPL +3 skips the EOR
  return b < 0x0100;                                                // CMP #0100 / BCS -> 5C43
}
// The WRAM form, as C0/5B95 assembles it: $DB/$DD latched from the mover at 5BB1/5BB6, the candidate read
// per iteration, and the carry from 5BD7's `CPX $6D`.
int CtVmSolidBox(const uint8_t* ram, int mover2, int cand2, int heading) {
  return CtVmSolidBoxAt(ram[0x1800 + mover2] | (ram[0x1801 + mover2] << 8),
                        ram[0x1880 + mover2] | (ram[0x1881 + mover2] << 8),
                        ram[0x1800 + cand2]  | (ram[0x1801 + cand2]  << 8),
                        ram[0x1880 + cand2]  | (ram[0x1881 + cand2]  << 8),
                        cand2 >= mover2, heading);
}
// ---- C0/5B95's scan loop ----
//   5BBD: LDA $7F2000 / ASL A / TAX      X starts at nobj*2, one past the last object
//   5BC3: LDA $0F00,X / BNE ->5BD2       the candidate gates (CtVmSolidCandGate)
//   5BC8: DEX / DEX / BNE ->5BC3         ...and the loop exits at zero
//   5BCC: REP #10 / LDY $C7 / CLC / RTS  nothing blocks
// So the ROM probes the slot one past the array (a real read of live WRAM) and never tests index 0 (the
// controller object), which can never block a mover.
// `pos` supplies positions; NULL reads the ROM's $1800/$1880 (what --sweepsolid and npcSolidTile use).
int CtVmSolidScan(const uint8_t* ram, int nobj, int mover2, int heading,
                  CtSolidPosFn pos, void* ctx) {
  if (!CtVmSolidMoverGate(ram, mover2)) return 0;                 // 5B99 + 5BA0
  int mx, my;
  if (pos) pos(ctx, mover2 >> 1, &mx, &my);
  else { mx = ram[0x1800 + mover2] | (ram[0x1801 + mover2] << 8);
         my = ram[0x1880 + mover2] | (ram[0x1881 + mover2] << 8); }
  for (int x2 = nobj * 2; x2 != 0; x2 -= 2) {                     // 5BBD start, 5BC8 DEX DEX / BNE
    if (!CtVmSolidCandGate(ram, mover2, x2)) continue;
    int cx, cy;
    if (pos) pos(ctx, x2 >> 1, &cx, &cy);
    else { cx = ram[0x1800 + x2] | (ram[0x1801 + x2] << 8);
           cy = ram[0x1880 + x2] | (ram[0x1881 + x2] << 8); }
    if (CtVmSolidBoxAt(mx & 0xFFFF, my & 0xFFFF, cx & 0xFFFF, cy & 0xFFFF, x2 >= mover2, heading))
      return 1;
  }
  return 0;
}
// Note: the positions are the committed $1800/$1880, not the live sc->fx/fy. The ROM's nine `JSR C0/5B95`
// sites sit in the move-op bodies, inside the event VM at C0/00C4, while $1800/$1880 are written only by
// C0/A810's integrate arms, later, at C0/00D3 (C0/ACFD writes velocity and the ticket, never the position).
// So the test reads the previous frame's committed positions for every object. CtSceneTick runs the scheduler
// before the integrate and the commit, so the bytes hold what the ROM has there.
static int npcSolidTile(const CtScene* sc, int obj, int heading) {
  return CtVmSolidScan(((Mem*)sc->mem)->s, sc->nobj, obj * 2, heading, NULL, NULL);
}
// C0/5281 (the shared op 0x94/0xB5/0xB6 move body): if a solid object blocks the direct heading, rotate it
// +0x30 once and go on: the mover does not search further and does not stall. No map check in this path.
// Liveness counters for the object-solid path (--sweepsolid checks what C0/5B95 decides, these that the engine
// reaches it): CtSceneSolidLive reports them, `field-solid-liveness` pins them.
static int gMvaCalls = 0, gMvaGated = 0, gSolidHit = 0;
static int npcMoveAngle(const CtScene* sc, int obj, int angle) {
  // C0/5B95's head, both gates: an unactivated (off-camera) mover does not consult the object plane at all
  // (5B99's BNE falls to 5B9E CLC / RTS).
  gMvaCalls++;
  if (!CtVmSolidMoverGate(((Mem*)sc->mem)->s, obj * 2)) { gMvaGated++; return angle; }
  const uint8_t* T = mvTables();
  int f = T[0x400 + angle];                                        // 0 up,1 down,2 left,3 right
  { int hit = npcSolidTile(sc, obj, f); if (hit) { gSolidHit++; angle = (angle + 0x30) & 0xFF; } }
  return angle;
}
// Op 0xA1's C0/5B95 (51F4): 51F1's `JSR C0/ABA2` is followed at once by the solid test, with no `LDA $F700,X / STA
// $1600,X` between them, so the scan runs against the facing the object already had, and a hit nudges only the heading
// ($01EE += $30, no facing write). The other move ops set $1600 from the heading first (4FB5, 50F9), which is what
// npcMoveAngle's `T[0x400 + angle]` stands for.
static int npcMoveAngleStale(const CtScene* sc, int obj, int angle) {
  gMvaCalls++;
  if (!CtVmSolidMoverGate(((Mem*)sc->mem)->s, obj * 2)) { gMvaGated++; return angle; }
  int f = r8(sc->mem, 0x7E1600u + (uint32_t)obj * 2) & 3;           // the stale $1600,X
  if (npcSolidTile(sc, obj, f)) { gSolidHit++; angle = (angle + 0x30) & 0xFF; }   // 51F9-51FE
  return angle;
}
// Entries to the object-solid path, how many the C0/5B95 head gated away, and how many deflected.
int CtSceneSolidLive(int* gated, int* deflected) {
  if (gated) *gated = gMvaGated; if (deflected) *deflected = gSolidHit; return gMvaCalls;
}
// Does the current step land on a solid map tile (ACFD's $7E7040 bit-7 test)? C0/AD81-ADB4: `$DF = $1800,X + $DD`,
// `$1880,X + $DB`, the position the step would reach, and the tile under it, `(Y high << 8 | $E0) & $1D1E`, read
// `LDA $7E7040,X / BMI ->AE27`; the random angle's retry tests its own landing the same way (AEB7-AEE1 -> AF27). The
// test runs on every issue, the object's own tile included (no "did the step change tiles" gate). Only a step with
// no velocity skips it (AD5F-AD73: $DD = 0 and $DB = 0 -> ADEB).
// Note the retry's stale high byte: the first test sign-extends dx into $DE on both arms (AD63-AD6F), the retry only
// for a nonzero dx (AE97-AEA5). A zero dx goes AEA7 `LDA $DB / BEQ` -> AEAB with $DE untouched, and AEBF's 16-bit
// `ADC $DD` adds the first heading's sign: FF00 when that dx was negative, so the probe lands a whole tile left of
// the step. `staleX` carries it (-16 px or 0).
static int mvCrossesSolid(const CtScene* sc, int obj, double staleX) {
  if (sc->mx[obj] == 0 && sc->my[obj] == 0) return 0;                 // AD5F-AD73 / AEA7-AEA9 -> AF18
  double nfx = sc->fx[obj] + sc->mx[obj] + (sc->mx[obj] == 0 ? staleX : 0), nfy = sc->fy[obj] + sc->my[obj];
  return sc->tileSolid(sc->tileCtx, (int)floor(nfx) >> 4, (int)nfy >> 4);   // ADA8-ADB4 / AED5-AEE1
}
// $1600,X is the facing; the drain's frame lookup (C0/CBA4, CtFieldFrameLookup) reads it from the image. Every ROM
// facing writer is a `STA $1600,X` (the facing ops, the move family's `LDA $F700,X / STA $1600,X` at issue, the
// face-object ops), so the C field and the byte are written together here.
static void vmSetFacing(Mem* m, CtVmActor* a, int obj, int fac) {
  a->facing = fac & 3;
  w8(m, 0x7E1600u + (uint32_t)obj * 2, (uint8_t)(fac & 3));
}
// C0/568B, the move-op family's arrival / decline exit, forces the auto anim idle:
//   568B LDX $6D / LDA $1780,X / BEQ ->5693 / RTS      mode != 0: the script owns the anim, nothing
//   5693 LDA $1680,X / CMP #00 / BEQ ->56A5             already idle: nothing
//   569A LDA #00 / STA $1680,X / STZ $1681,X / STZ $1601,X   idle (anim 0) + step/timer reset
// Called from the seven move-op exits C0/4E85 4F9C 502D 50DB 5273 53BB 54A8 (anim_rate_c98a_5614.md).
static void mvForceIdle(CtScene* sc, int obj) {
  uint32_t x = (uint32_t)obj * 2;
  if (r8(sc->mem, 0x7E1780 + x) != 0) return;                       // 568D-5692
  if (r8(sc->mem, 0x7E1680 + x) == 0) return;                       // 5693-5698
  w8(sc->mem, 0x7E1680 + x, 0); w8(sc->mem, 0x7E1681 + x, 0); w8(sc->mem, 0x7E1601 + x, 0);   // 569A-56A2
  ctAnimSyncActor(&sc->actors[obj], sc->mem->s, x);
}
// C0/5265-5271 + C0/527B: the `$1C81,X` bit-1 arm of the move-toward-actor body (C0/5228), shared by ops
// 0x94/0x95 and, through C0/354F/5410, by 0xB5/0xB6. Reached only once the mover is adjacent: instead of
// stopping a tile short (the 5273 `JSR C0/568B` arrive path) the ROM calls C0/30B3, which steps one pixel on
// both axes at once toward the target's exact pixel and arms `$1A01` = `$7F0B00` = 1, and on its SEC return
// runs `527B JSR C0/5614` (the idle/walk/run selector) before yielding with the IP unchanged.
// Returns 1 = still snapping (yield, IP unchanged), 0 = aligned (the caller takes the 5273 arrive arm).
// Note: the live ticket is `sc->cycles`; the $1A01,X that 30B3 stores is read back here and not maintained
// afterwards.
// C0/305D, the $1C81 bit-0 arrival snap. One gate, five handlers: `LDA $1C81,X / BIT #01 / BEQ / JSR C0/305D / BCS`
// at C0/4F90 (0x96), C0/50CF (0x97), C0/501E (0x9A), C0/5158 (0xA0) and C0/51DC (0xA1). It tests the sub-tile nibbles
// and sets both velocities in one call:
//   305F STZ $1900,X / STZ $1980,X          ; clear both
//   3065 LDA $1800,X / AND #F0 / CMP #80    ; X sub-tile at the centre?
//   306E   (yes) LDA $1880,X / AND #F0 / CMP #F0 -> equal: CLC RTS = snap complete
//   3079   (no)  BCS -> 3082 LDA #F0 (-1px) else 307B LDA #10 (+1px) -> $1900,X
//   3089        then the same Y test -- 309B $1980,X = +10 / 30A7 = -10
//   3092/30A2   LDA #01 / STA $1A01,X / SEC RTS   ; ticket 1, carry set = keep snapping
// So the step is diagonal: X and Y correct together on one visit. Returns 1 = stepped (the handler yields, IP
// parked), 0 = on the tile's centre-bottom (the handler completes). After the SEC, 0x96/0x97 re-anim (JSR C0/5614)
// before `TYX / CLC`; 0xA0 goes straight to `516A TYX / CLC / RTS`.
static int mvArriveSnap305D(CtScene* sc, int obj, int atx, int aty) {
  int cx = atx * 16 + 8, cy = aty * 16 + 15;     // sub-tile 0x80 = px 8 (centre) / 0xF0 = px 15 (bottom)
  int dix = cx - (int)sc->fx[obj], diy = cy - (int)sc->fy[obj];
  if (!dix && !diy) return 0;
  sc->mx[obj] = dix ? (dix > 0 ? 1 : -1) : 0;    // 307B/3082
  sc->my[obj] = diy ? (diy > 0 ? 1 : -1) : 0;    // 309B/30A7, the same visit
  sc->cycles[obj] = 1; sc->hasTask[obj] = 1;     // 3092/30A2 `LDA #01 / STA $1A01,X` + SEC
  return 1;
}
static int mv30B3Arm(CtScene* sc, int obj, int tgtObj, int anim) {
  uint32_t x = (uint32_t)obj * 2;
  uint8_t* r = sc->mem->s;                                   // the flat 128 KB image move.c indexes
  int t2 = tgtObj * 2;
  w8(sc->mem, 0x7E01E9, t2); w8(sc->mem, 0x7E01EA, t2 >> 8); // 5237 STX $E9 (16-bit); 30B3 reads it
  if (!CtVmMoveSnap30B3(r, (int)x)) return 0;                // 526E JSR C0/30B3; CLC = already aligned
  w8(sc->mem, 0x7E1900u + x, r[0x1900 + x]);                 // re-store the pair 30B3 published so the
  w8(sc->mem, 0x7E1980u + x, r[0x1980 + x]);                 //   write mask claims those bytes as ours
  sc->mx[obj] = (double)(int8_t)r[0x1900 + x] / 16.0;        // $1900/$1980 are 1/16 px -> px/frame
  sc->my[obj] = (double)(int8_t)r[0x1980 + x] / 16.0;
  sc->cycles[obj] = r[0x1A01 + x];                           // 3121/3137/314B  LDA #01 / STA $1A01,X
  sc->b00[obj] = r[0x10B00 + x];                             // 3124/313A/314E  STA $7F0B00,X, positive,
  sc->hasTask[obj] = 1;                                      //   so AA07 free-integrates this one step
  if (anim) {                                                // 527B / 53C4 JSR C0/5614 (the walk);
    CtVmMoveStepAnim(r, (int)x);                             //   0x9E/0x9F's 5319 -> 5323 goes straight to TYX / CLC
    ctAnimSyncActor(&sc->actors[obj], r, x);
  }
  return 1;
}
// CT_MVTRACE=<obj>: one line per moveOp visit and per integration step, for one object. The mover's live state
// (fx/fy px, mx/my px/frame, the cycles ticket, hasTask, mspd) lives in CtScene, not in WRAM, so the
// $1A00/$1A01/$7F0B00 bytes of a CT_DUMPWRAM image do not show it.
static int CtVmMoveTrace(CtScene* sc, int obj, int op, int addr, const char* where);
static int CtVmMoveTrace(CtScene* sc, int obj, int op, int addr, const char* where) {
  static int t = -2;
  if (t == -2) { const char* e = getenv("CT_MVTRACE"); t = e ? atoi(e) : -1; }
  if (t < 0 || t != obj) return 1;
  printf("mv[%d] %-7s op=%02X @%04X cyc=%d task=%d mspd=%02X mode=%02X b00=%d pos=(%.2f,%.2f) "
         "v=(%.3f,%.3f) moveAnim=%d c1C81=%02X\n",
         obj, where, op, addr, sc->cycles[obj], sc->hasTask[obj], sc->mspd[obj], sc->mode[obj],
         sc->b00[obj], sc->fx[obj], sc->fy[obj], sc->mx[obj], sc->my[obj],
         sc->actors[obj].moveAnim, sc->c1C81[obj]);
  return 1;                                          // always true: it is a probe inside an `if`
}

static int moveOp(CtScene* sc, int obj, int addr, uint8_t op) {   // 1 = yield, 0 = complete
  CtVmActor* a = &sc->actors[obj]; const uint8_t* B = sc->body; Mem* m = sc->mem;
  CtVmMoveTrace(sc, obj, op, addr, "enter");
  if (!a->hasPos) return 0;
  if (sc->cycles[obj] > 0) return 1;               // a step is in flight ($1A01) -> yield
  // The anim/facing layer, per handler (move.c's ctVmMoveActorOp flags): 0xA0 (C0/5129), 0xA1 (C0/5191) and
  // 0x9E/0x9F (C0/52C0/54F5) write no facing ($F700 -> $1600), run no C0/5614 and take no C0/568B.
  int animLayer = !(op == 0xA0 || op == 0xA1 || op == 0x9E || op == 0x9F);
  // The pair 0x98/0x99 (C0/534A / C0/550E) opens with op 0x92's $1A80 head: once a step was issued, the first
  // visit whose ticket is spent completes the op (534C-535C: `STZ $1A80,X`, IP += 3, no C0/568B on this arm),
  // one aimed step of the script's own length with no re-aim. hasTask is the latch, as for 0x92.
  if ((op == 0x98 || op == 0x99) && sc->hasTask[obj]) { sc->hasTask[obj] = 0; return 0; }
  // C0/5614 (JSR'd at step issue by the tile move family, not by 0xA0 or the vector moves 0x9C/0x9D at
  // C0/4EC7/4F0A): re-selects the anim mode. A mode-3 shape (op 0xAC) is promoted (C0/5620): held index $FF
  // (animIndex < 0) -> auto walk (mode 0), else its held animeset (mode 1). Modes 0/1/2 are left as they are.
  if (animLayer && op != 0x9C && op != 0x9D && a->animMode == 3) { a->animMode = (a->animIndex < 0) ? 0 : 1; a->animGen++; }
  if (op == 0x92 || op == 0x9C || op == 0x9D) {    // vector move: one step of operand frames
    if (sc->hasTask[obj]) { sc->hasTask[obj] = 0; sc->vecMove[obj] = 0; mvForceIdle(sc, obj); return 0; }   // C0/4E85 JSR C0/568B
    // Op 0x9D (C0/4F0A) reads both operands as local-var indices: angle = var[op1], frames/ticket = var[op2]
    // (4F47 `LDA $7F0200,(op2*2) / STA $1A01`). Ops 0x9C/0x92 take both as literals.
    int angle  = (op == 0x9D) ? r8(m, LOC + B[addr + 1] * 2) : B[addr + 1];
    int frames = (op == 0x9D) ? r8(m, LOC + B[addr + 2] * 2) : B[addr + 2];
    // ...and only op 0x92 re-anims: of the thirteen `JSR C0/5614` sites (4EC0 4FA5 4FE3 5037 507D 50E4 5122 527B
    // 52B9 53C4 5409 54B0 54EE), 4EC0 is inside 0x92's body (C0/4E73) and none lies in 4EC7..4FA4, the bodies of
    // 0x9C (C0/4EC7) and 0x9D (C0/4F0A). So the vector moves never re-select the anim, in WRAM or in the C mirror.
    mvStartStep(sc, obj, angle, frames, op == 0x92); mvIssueRegs(sc, obj, op == 0x92);   // only 92 sets the facing
    sc->b00[obj] = -128;                                    // vector ops arm $7F0B00 = #80 (4EFA): negative, so
                                                            // AA07 free-integrates every frame (no clamp) and the
                                                            // whole operand distance slides across tiles.
    sc->vecMove[obj] = (op == 0x9C || op == 0x9D);          // slide with the idle pose (no walk cycle)
    sc->hasTask[obj] = 1; return 1;                         // 9C/9D keep the facing (their handlers have
                                                            // no $F700 -> $1600)
  }
  sc->vecMove[obj] = 0;                                     // any tile-move family op re-anims (walk)
  int dtx, dty;                                    // target-tile family: re-aim per visit
  // C0/3546 / C0/354F, ops B5 / B6: `STY $E3` (the op's IP, 16-bit) before `JSR C0/5215` / `JSR C0/5410`, whose `LDX $E3`
  // hands it back -- the call ops' scratch word, overwritten (reqScratch).
  if (op == 0xB5 || op == 0xB6) { w8(sc->mem, 0x7E01E3, addr & 0xFF); w8(sc->mem, 0x7E01E4, (addr >> 8) & 0xFF); }
  int tgtObj = -1;                                 // the object the 0x94/0x95/0xB5/0xB6 arm resolved,
                                                   // kept for op 0x95's join branch further down
  if (op == 0x94 || op == 0x95 || op == 0xB5 || op == 0xB6 ||   // 0x94/0xB5 (C0/5215) = toward object[op/2];
      op == 0x9E || op == 0x9F || op == 0x98 || op == 0x99 ||   //   + 0x9E/0x9F (C0/52C0/54F5), 0x98/0x99 (C0/534A/550E)
      op == 0x8F) {                                             //   + 0x8F (C0/5429, 543C LDA $97,X)
    // Target resolution, as move.c's ctVmMoveActorOp has it (field_party_slot_table.md); the operand is the
    // already-doubled slot index:
    //   0x94 / 0xB5  (C0/5215): 5223 LDA $7F2001,X / TAX -- the operand is the target's object offset.
    //   0x95 / 0xB6  (C0/5410): 5423 LDA $97,X -- through the party-slot table $0197 + operand, which
    //                holds an object offset, or 0x80 when the slot is empty.
    int off = (op == 0xB6 || op == 0x95 || op == 0x9F || op == 0x99 || op == 0x8F) ? vmSlotTarget(m, op, B[addr + 1]) : B[addr + 1];
    // C0/5228: `LDA $1100,X / BMI ->5273`, the target's move-mode bit 7 declines the whole move.
    // Note: not an "is the slot empty" test. With an empty slot's 0x80 the read aliases to $7E:1180 (object 0's
    // IP low byte), and the move declines only when that byte is negative. Kept as the ROM indexes it.
    if (r8(m, 0x7E1100 + off) & 0x80) {           // -> C0/5273's JSR C0/568B; 0x9E/0x9F's 52D6 -> 531E is a bare
      sc->hasTask[obj] = 0;                        //   `TYX` (nothing called, nothing written); 0x98/0x99's 536E ->
      if (animLayer) mvForceIdle(sc, obj);         //   53B6 also clears self's $1A80 (hasTask) before 53BB's 568B
      return (op == 0xB5 || op == 0xB6) ? 1 : 0;
    }
    int t = off / 2;
    // Port-only bounds guard (not in the ROM), so a malformed script cannot index off the actor array.
    if (t >= sc->nobj || !sc->actors[t].hasPos) { sc->hasTask[obj] = 0; return (op == 0xB5 || op == 0xB6) ? 1 : 0; }   // port-only guard: no ROM exit here
    tgtObj = t;
    // 522D-5235 `LDA $1801,X / STA $F2 / LDA $1881,X / STA $F3` (and 52D8 / 5370 / 5444 for 0x9E / 0x98 / 0x8F): the
    // target's tile bytes from WRAM, as the pass found them. The lead moves in A810, after the pass; the host's new
    // position is already in sc->fx, so the C position would be a frame early.
    dtx = r8(m, 0x7E1801 + (uint32_t)off); dty = r8(m, 0x7E1881 + (uint32_t)off);
  } else if (op == 0x96 || op == 0xA0) { dtx = B[addr + 1]; dty = B[addr + 2]; }
  else { dtx = r8(m, LOC + B[addr + 1] * 2); dty = r8(m, LOC + B[addr + 2] * 2); }
  int atx = (int)sc->fx[obj] >> 4, aty = (int)sc->fy[obj] >> 4;
  // The chase (B5/B6, C0/5215) moves only while farther than one tile; once adjacent it stops but stays
  // perpetual (it never advances the script).
  int dxr = atx - dtx, dyr = aty - dty;
  // C0/5228's arrival test, adjacency: a wrap-exact 8-bit compare per axis, 5245-5252 (Y) and 5254-5263 (X),
  // both `CMP / INC A / CMP / DEC A DEC A / CMP` on the tile bytes $1801/$1881 (not a signed |d| <= 1).
  int _sxb = atx & 0xFF, _syb = aty & 0xFF, _txb = dtx & 0xFF, _tyb = dty & 0xFF;
  int adj = ((_syb == _tyb) || (((_syb + 1) & 0xFF) == _tyb) || (((_syb - 1) & 0xFF) == _tyb)) &&
            ((_sxb == _txb) || (((_sxb + 1) & 0xFF) == _txb) || (((_sxb - 1) & 0xFF) == _txb));
  if (op == 0x94 || op == 0x95 || op == 0x9E || op == 0x9F || op == 0x98 || op == 0x99) {   // move-toward-actor (C0/5215) / -toward-party-slot (C0/5410,
    // which BRLs into 5228 at 5426): one body, one arrival rule. It halts as soon as the two tiles are adjacent,
    // and C0/5273 then does INX INX + SEC: it advances past the op (the runner C0/5A93 commits X on SEC). So both
    // complete; 0xB5/0xB6 share the body but loop forever. (0x94 is C0/5215 by the jump table, which falls into
    // 5228; the exact-tile rule below belongs to 0x96/0x97/0xA0.)
    if (adj) {
      // C0/5265: with $1C81 bit1 set, do not stop a tile short: C0/30B3 walks onto the target's exact pixel,
      // 1 px per visit.
      if ((sc->c1C81[obj] & 2) && mv30B3Arm(sc, obj, tgtObj, animLayer)) return 1;   // 5267-5271 + 527B ($1A80 kept)
      sc->hasTask[obj] = 0; if (animLayer) mvForceIdle(sc, obj); return 0;   // 5273 / 53B8-53BB; 0x9E/0x9F: 531E TYX
    }
  } else if (op == 0x8F) {
    // C0/5429: toward a party member until adjacent or inside the camera window (as move.c has it).
    // The only member with no $1C81 test and no snap: 5462-547A the same wrap-exact adjacency, then 547C-5486 the
    // camera's low bytes halved into tiles and the asymmetric box op 0x28 uses (548D-54A4); either ends in 54A6/54A8
    // `LDX $6D / JSR C0/568B` and IP += 2. Otherwise 54B6-54EE issue a step like 0x94's: heading, facing, the solid
    // test with its second facing write, $1000 into $1A01 and $7F0B00, ACFD, C0/5614.
    if (adj) { sc->hasTask[obj] = 0; mvForceIdle(sc, obj); return 0; }
    int bx = (atx - (r8(m, 0x7E1D0A) >> 1)) & 0xFF, by = (aty - (r8(m, 0x7E1D0E) >> 1)) & 0xFF;
    if (bx >= 0x02 && bx <= 0x0D && by >= 0x02 && by <= 0x0C) { sc->hasTask[obj] = 0; mvForceIdle(sc, obj); return 0; }
  } else if (op == 0xB5 || op == 0xB6) {           // chase: stop once adjacent, stay perpetual; C0/3546 forces
    if (adj) {                                     // CLC after the JSR (3548) so the inner INX INX never lands
      if ((sc->c1C81[obj] & 2) && mv30B3Arm(sc, obj, tgtObj, 1)) return 1;   // 5267-5271 bit-1 arm is theirs too
      sc->hasTask[obj] = 0; mvForceIdle(sc, obj); return 1;   // C0/5273 JSR C0/568B
    }
  } else if (op == 0xA0) {                          // keep-facing pace (Mom's cooking).
    // Arrival is tile-index equality (C0/5129, disasm/notes/field_touch_preempt.md sec. 2):
    //   5144: LDA $1801,X -> $F0 / 5149: LDA $1881,X -> $F1     ; the high bytes of the 1/16-px
    //   514E: CMP $F3 / BNE ->516D / 5152: LDA $F0 / CMP $F2     ; position, i.e. the tile index
    //   5158: LDA $1C81,X / BIT #01 / BEQ ->5164                 ; bit0 clear (the default) ->
    //   5164: TYX / INX / INX / INX / SEC / RTS                  ;   complete, IP += 3
    // `dxr`/`dyr` above are that compare (`fx >> 4` is the high byte the ROM reads). With $1C81 bit 0 the arrival
    // does not complete at once: 515F JSR C0/305D steps the object onto the tile's centre-bottom a visit at a time
    // (`5162 BCS ->516A TYX / CLC / RTS`, no re-anim), and only the aligned object takes 5164's `INX x3 / SEC`.
    double cx = dtx * 16 + 8, cy = dty * 16 + 8;
    double dpx = cx - sc->fx[obj], dpy = cy - sc->fy[obj];
    if (dxr == 0 && dyr == 0) {                      // op 0xA0's arrival (C0/5184): not a C0/568B site
      if ((sc->c1C81[obj] & 1) && mvArriveSnap305D(sc, obj, atx, aty)) return 1;   // 5158-5162 -> 516A
      sc->hasTask[obj] = 0; return 0;
    }
    // Not arrived: the common path below issues the step, as C0/516D-518D does: 516D JSR C0/ABA2 (the heading from
    // the tile delta, as --checkdir checks CtMoveDirAngle), 5170 JSR C0/5B95 against the facing the object already
    // has (no $F700 -> $1600 here: animLayer = 0) with the +$30 nudge, 517C-5184 `LDA $1000,X / STA $1A01,X /
    // STA $7F0B00,X`, 518A JSR C0/ACFD (the wall bounce included), and no C0/5614.
    (void)dpx; (void)dpy;
  } else if (dxr == 0 && dyr == 0) {               // reached the target tile (0x96/0x94/local)
    // C0/4F90 (0x96) and C0/50CF (0x97) `LDA $1C81,X / BIT #01 / BEQ / JSR C0/305D / BCS`: bit 0, not != 0.
    // 0x97 is 0x96 with the two operands read through the scene vars; from 50B9 its bytes are 0x96's.
    if ((op == 0x96 || op == 0x97) && (sc->c1C81[obj] & 1)) {
      if (mvArriveSnap305D(sc, obj, atx, aty)) return 1;   // 4F97 / 50D6 JSR C0/305D / BCS -> JSR C0/5614, TYX / CLC
    }
    if (op == 0xA1) {                              // C0/5191 = 0x97 without the anim/facing layer (as move.c):
      if ((sc->c1C81[obj] & 1) && mvArriveSnap305D(sc, obj, atx, aty)) return 1;   // 51DF / 51E3 -> 51EE, no 5614
      sc->hasTask[obj] = 0; return 0;              // 51E8 INX x3 / SEC -- and no JSR C0/568B
    }
    sc->hasTask[obj] = 0; mvForceIdle(sc, obj); return 0;   // aligned (or snap off) -> complete: C0/4F9C JSR C0/568B, then the SEC exit
  }
  // Op 0x96/0x97 (C0/4F61), first visit of a move: the not-arrived path sets the facing, arms and issues on the
  // same visit:
  //   4FAB: JSR C0/ABA2            ; heading -> $EE
  //   4FB5: LDA $F700,X / STA $1600,X   ; facing, from the heading
  //   4FBD: JSR C0/5B95 / (+0x30 sidestep at 4FC2-4FCF)
  //   4FD2: LDA $1000,X / STA $1A01,X / STA $7F0B00,X   ; arm the ticket
  //   4FE0: JSR C0/ACFD            ; issue the velocity, same visit
  //   4FE3: JSR C0/5614            ; re-anim (walk)
  //   4FE8: CLC / RTS              ; yield, IP parked (X = Y = the entry IP)
  // The integration loop runs after the runner in the same CtSceneTick (C0/00C4 -> C0/00D3), so the first step
  // integrates this frame. hasTask == 0 marks the first visit of this move.
  if ((op == 0x96 || op == 0x97) && !sc->hasTask[obj]) {
    vmSetFacing(sc->mem, a, obj, mvTables()[0x400 + mvAngle(dtx - atx, dty - aty)]);   // 4FB5
    sc->moveAngle[obj] = mvAngle(dtx - atx, dty - aty);   // store the heading for the integration visits
    sc->hasTask[obj] = 1;
  }
  // ACFD's per-step velocity decision: aim at the target; if that crosses into a wall, try one random angle (AE27,
  // $FE00 via $F8); if that also walls, stall this step (AF27). The direct heading is retried first every step
  // (object collision's +0x30 nudge, C0/5281, applies to it).
  // Op 0x96 re-aims on every step issue, from the tile delta; C0/4F61 caches no heading:
  //   4F7C LDA $1801,X -> $F0 / 4F81 LDA $1881,X -> $F1     ; the actor's current tile (position high byte)
  //   4F6D/4F74 the two operands -> $F2/$F3                 ; the target tile
  //   4F86 CMP $F3 / 4F8C CMP $F2 -> equal = arrived (snap/finish), else
  //   4FAB JSR C0/ABA2                                       ; heading from ($F2-$F0, $F3-$F1)
  //   4FD2 LDA $1000,X / STA $1A01,X / 4FE0 JSR C0/ACFD      ; ticket + issue the velocity
  // and the block runs on every visit whose ticket has expired.
  int _ang;
  { _ang = mvAngle(dtx - atx, dty - aty); sc->moveAngle[obj] = _ang; }   // 4FAB, every issue
  _ang = animLayer ? npcMoveAngle(sc, obj, _ang) : npcMoveAngleStale(sc, obj, _ang);   // 0xA1 51F4 / 0x9E 5329: the old facing
  // Ticket = $1000,X (the object's cadence): op 0x87 sets it (moveAnim); a PC's load sets 1 (C0/42D7).
  int _tk = a->moveAnim > 0 ? a->moveAnim : 1;  /* $1000,X cadence; default 1 (C0/42D7) */
  if (op == 0x98 || op == 0x99) _tk = B[addr + 2];   // 53F4: the pair's ticket is the script byte
  mvStartStep(sc, obj, _ang, _tk, animLayer);
  CtVmMoveTrace(sc, obj, op, addr, "issued");
  if (sc->collMap[obj] && sc->tileSolid && mvCrossesSolid(sc, obj, 0)) {
    double staleX = sc->mx[obj] < 0 ? -16.0 : 0.0;                             // $DE as the first test left it
    _ang = mvRandAngle(m);                                                      // AE27-AE2D: the $7E01F8 cursor,
    mvStartStep(sc, obj, _ang, _tk, 0);                                         // 8-bit -> $FE00. setFace=0: the map bounce
    if (mvCrossesSolid(sc, obj, staleX)) {                                              // sets velocity only (C0/ACFD has no
      sc->cycles[obj] = 0; sc->mx[obj] = 0; sc->my[obj] = 0; sc->hasTask[obj] = 0; mvIssueRegs(sc, obj, animLayer); return 1;   // $1600 write). AF27 stall: zero velocity published, then 5614 -> idle.
    }
  }
  // The step issue arms $7F0B00 with the ticket: op 0x96 C0/4FDA, op 0x97 C0/5071 (fresh, the script operand) /
  // C0/5119 (re-issue, $1000,X), op 0x94 (shared body), the chase ops 0xB5/0xB6 C0/52B0/54E5 -- all load $1000,X
  // into both $1A01 and $7F0B00. Positive = AA07 free-integrates the issue frame (crossing at most one tile), then
  // clears $7F0B00 to 0, so the following frames clamp at the next tile edge: the mover advances tile by tile and
  // rests on the sub-tile where its velocity lands (the centre snap is the separate $1C81 path). Whether the clamp
  // applies is the object's $1100 move mode (sc->mode[], dispatched in run_tasks), not who issued the move.
  // Op 0x9A arms $7F0B00 = #80 (negative = free every frame, no clamp), like the vector ops.
  sc->b00[obj] = (op == 0x94 || op == 0x95 || op == 0x96 || op == 0x97 || op == 0xA0 || op == 0xA1 || op == 0xB5 || op == 0xB6 ||
                  op == 0x9E || op == 0x9F || op == 0x98 || op == 0x99 || op == 0x8F) ? sc->cycles[obj]
               : (op == 0x9A) ? -128 : 0;
  mvIssueRegs(sc, obj, animLayer);                             // the velocity ACFD published, then JSR C0/5614 (not A0/A1/9E/9F)
  sc->hasTask[obj] = 1; return 1;                              // A0 keeps its facing (no $F700 -> $1600)
}

// The call ops' direct-page scratch (D = $0100). Every body that installs or queues a call computes the target's
// $7F0580 slot with the hardware multiplier and leaves it in $E1, and the function's pointer index in $E3:
//   op 02 / 05 (C0/5FB6): the accept arm (new priority $DF < the target's $1C00 = $D9) C0/5FEB-6015 `$E1 = $D9 * $80 + $DB`,
//     `$E3 = $DB << 4`; the defer arm ($DF > $D9) C0/603E-6062 `$E1 = $DF * $80 + $DB`, and `$E3` only when that slot is
//     free (C0/6053 `LDA $7F0580,X / BNE`);
//   op 03 / 06 (C0/607E) and 04 / 07 (C0/611A): the accept arm alone (C0/60B4-60DE, C0/6150-617A); their wait arms
//     (equal or less urgent) write nothing.
// Every body exits first, writing nothing, on `$1C01,X` (op 08's wait), `$1100,X` bit 7 or `$1000,X` bit 7, and on an
// equal priority. $DB = the target * 2. Nothing in the field reads them back, but the overworld does: the 2300 A.D.
// fog's class $7759 takes its wave phase from the word at $01E2 (world.c owBg3WaveTick).
static void reqScratch(CtScene* sc, int op, int tgt, int pr) {
  uint32_t X = (uint32_t)tgt * 2;
  if (r8(sc->mem, 0x7E1C01 + X)) return;                         // 5FC3 / 608B / 6127 BNE
  if (tgt < 0 || tgt >= sc->nobj) return;
  if (sc->mode[tgt] & 0x80) return;                              // 5FC8 / 6090 / 612C `$1100,X` BMI
  if (sc->actors[tgt].cadPause) return;                          // 5FCD / 6095 / 6131 `$1000,X` BMI
  int d9 = sc->st[tgt].prio;                                     // `$1C00,X` = $D9 (the port keeps it here, not in WRAM)
  if (pr == d9) return;                                          // 5FE7 / 60B0 / 614C BEQ
  int queues = (op == 0x02 || op == 0x05);
  if (pr > d9 && !queues) return;                                // 60B2 / 614E BCS: the wait
  int e1 = (pr < d9 ? d9 : pr) * 0x80 + (int)X;
  w8(sc->mem, 0x7E01E1, e1 & 0xFF); w8(sc->mem, 0x7E01E2, (e1 >> 8) & 0xFF);
  if (pr > d9 && (r8(sc->mem, 0x7F0580 + (uint32_t)e1) | r8(sc->mem, 0x7F0581 + (uint32_t)e1))) return;   // 6053 BNE
  int e3 = (int)(X & 0xFF) << 4;
  w8(sc->mem, 0x7E01E3, e3 & 0xFF); w8(sc->mem, 0x7E01E4, (e3 >> 8) & 0xFF);
}
// ---- The lead's step ticket (C0/3040 arms it; a later `STZ $1A01,X` in the same pass drops it) ----
// The ROM moves the lead in C0/A810's integrate (AB06 `LDA $1A01,X / BEQ` -> AB10-AB3F `$1800 += $1900`), after the event
// pass: C0/881E computes the step ($1D32/$1D33) before the VM, the lead's own op B0 arms it (3040 `LDA #01 / STA $1A01,X`,
// 9E29 -> 9E53/9E5D copy the velocity into $1900/$1980), and any invoke later in the same pass that installs a function on
// the lead ends `STZ $1A01,X` (op 0x02-0x07 C0/6034 / 60FD / 6199, op D9 32FE, ...), so A810 does not move him that
// frame. The host (field.c) moves the lead before the tick; it hands the step's origin here (CtSceneLeadStepFrom) and
// the integrate leg takes it back when no ticket stands (vmLeadStepAtA810: never armed, or dropped). Transient per-frame
// state: statics, not CtScene members (a member breaks every --loadstate .state).
static int g_leadTk = -1;                  // -1 = the lead's 3040 arm has not run this pass, 1 = armed, 0 = dropped
// An op 0xDA ring reset (C0/347D) made in a visit after the lead's B0: in the ROM the lead's push (3040 -> 9E29) has run by
// then -- the lead is the queue's head (C0/5A31-5A3D) -- so the cursors take the pushed head. This port pushes after the
// runner (CtSceneUpdateFollowers), which re-applies the reset behind its push.
static int g_daRingPend = 0;
static int g_leadDefer, g_leadDeferTick; static double g_leadDeferX, g_leadDeferY;   // a busy lead's step (below)
static int g_leadFromOk, g_leadFromTick; static double g_leadFromX, g_leadFromY;
static int gVmTick;                        // trace timestamps (and the step origin's frame)
static void vmTicketStop(CtScene* sc, int o) {   // `STZ $1A80,X / STZ $1A01,X` on another object's op
  sc->hasTask[o] = 0; sc->cycles[o] = 0;
  if (o == sc->playerObj && g_leadTk == 1) g_leadTk = 0;
}
void CtSceneLeadStepFrom(CtScene* sc, double x, double y) {
  (void)sc; g_leadFromOk = 1; g_leadFromTick = gVmTick; g_leadFromX = x; g_leadFromY = y;   // this frame's, the tick to come
}
// The busy lead's step. C0/881E runs every frame (C0/00A7) whatever the lead's script is doing; what moves him is his op
// B0 (C0/3040 `STA $1A01,X` / 9E29), wherever in the pass his visit reaches it. A lead whose own function is running when
// the frame starts (a call another object made on him) may return onto his B0 in this very pass and walk this frame.
// The host does not move such a lead (field.c's leadBusy); it hands the step's destination here, and his B0 visit takes
// it (op B0) or the pass ends without it.
void CtSceneLeadDeferStep(CtScene* sc, double x, double y) {
  (void)sc; g_leadDefer = 1; g_leadDeferTick = gVmTick; g_leadDeferX = x; g_leadDeferY = y;
}
static int g_leadTakenTick = -1;           // the tick that took the lead's step back
int CtSceneLeadStepDropped(const CtScene* sc) { (void)sc; return g_leadTakenTick == gVmTick; }
// C0/A810 for the lead's host step, at the integrate leg (CtSceneTick, around the party trail).
//   unarmed  (-1): the lead's op B0 did not run this pass -- he sat on B2 (C0/3543 `TYX / CLC`) or another op, or the
//                  queue build left him out (C0/59F0: his countdown $1001 not expired) -- so $1A01 is 0 and AB06's BEQ
//                  skips him. Taken back before the party trail: with no 9E29 there is no ring push either.
//   dropped   (0): an invoke after his B0 zeroed $1A01 (vmTicketStop). Taken back after the trail: 9E29 pushed.
static void vmLeadStepAtA810(CtScene* sc, int dropped) {
  if (sc->playerObj < 0 || sc->playerObj >= sc->nobj) return;
  if (g_leadTk != (dropped ? 0 : -1) || !g_leadFromOk || g_leadFromTick != gVmTick - 1) return;
  CtSceneSetActorPosF(sc, sc->playerObj, g_leadFromX, g_leadFromY); g_leadTakenTick = gVmTick; g_leadFromOk = 0;
}
static void reqOp(CtScene* sc, int target, int function, int priority) {
  if (target >= sc->nobj || function >= 16 || priority >= 8) return;
  // C0/5FC8-5FD0: refuse if $1100 bit7 (inactive) or $1000 bit7 (cadence paused).
  // $1100 comes from sc->mode[] (synced from the load ops at create, overwritten by the WRAM seed);
  // actors[].moveMode is not re-seeded, so it must not be read here.
  if (r8(sc->mem, 0x7E1C01 + (uint32_t)target * 2)) return;   // C0/5FC3 `LDA $1C01,X / BNE ->6039` (op 0x08's wait)
  if (sc->mode[target] & 0x80) return;
  if (sc->actors[target].cadPause) return;
  CtSS* ts = &sc->st[target];
  if (ts->prio == priority) return;
  // Defer: target busy at a higher priority (lower number) -> stash the REQ at its priority slot, so the RETURN
  // scan (op 0x00, p > prio) resumes it once the higher function finishes. The guard is on the slot being
  // written (ret[priority]); ret[ts->prio] holds the function ts->prio pre-empted.
  if (ts->addr >= 0 && ts->prio < priority) { if (ts->ret[priority] == 0) ts->ret[priority] = fpPtr(sc, target, function); return; }
  if (ts->addr >= 0) ts->ret[ts->prio] = ssPushAddr(ts);          // C0/6005 / 60CC / 6168: $1180 (a waiter's 04 byte)
  ts->addr = fpPtr(sc, target, function); ts->prio = priority;
  // A pre-empting function drops the target's in-flight move. All three install arms end with the same four
  // stores, in this order: `STA $1180,X / STA $1C00,X / STZ $1A80,X / STZ $1A01,X` (op 0x02 C0/6027-6034, op 0x03
  // C0/60F0-60FD, op 0x04 C0/618C-6199; ops 0x05/0x06/0x07 BRL into the same bodies). Only the pre-empt arm reaches
  // them, so this sits after the early returns.
  vmTicketStop(sc, target);                                        // C0/6031+6034 / 60FA+60FD / 6196+6199
  // ts->dc is not reset: the ROM's REQ/CALL leaves $1001 alone, so the callee runs on the existing cadence phase.
}


// The scene-entry sweep, C0/B0E6 -> C0/B127. Not the per-frame pass, and not to be unified with it: the two
// boxes differ on three terms. C0/B127 does
//   $C3 = $1D0A - 4, clamped to 0 on a negative result   (B12E-B138, same as A88D)
//   $C4 = $1D0C + 4, with NO 0xFF clamp                  (B13A-B140; A88D clamps)
//   $C5 = $1D0E,     with NO -2 at all                   (B142-B149; A88D subtracts 2)
//   $C6 = $1D10 + 5, with no clamp                       (B14B-B151; A88D clamps)
// and its caller admits class 8, which the per-frame loop skips (B0F8 tests only #07, where A822/A826 test both),
// so a class-8 object can hold a slot from scene entry until the next C0/7155.
// On a pass (B16D-B18F) the ROM makes the per-frame pass's rising edge (A8EA-A90B) for that object: `$0E80 / $0E81 /
// $0F00 := 80`, `$1681 / $1601 / $1B00 := 0`, `$0F01 := FF`, C0/7170 (the palette) and C0/6F9A (the OBJ slot); back in
// the loop (B101-B117) C0/AB45 (its screen position), C0/A9CD (the draw list), `STZ $79`, C0/CAD9 until it pops it,
// C0/B1B2 (the upload) and C0/EA42 (the arena run); the sweep ends `BRL C0/B271`, the draw pass. On a fail nothing
// (B190 CLC). With the legs off (legsOn) the port keeps the latch and the palette only, as the per-frame pass does.
// Call site (objpal_allocator.md sec. 8): the ROM runs the load first (C0/56B0; C0/5709 places every object), then the
// location entry (C0/286C), which sets the camera (0A50 -> 74A6 -> 7506/7553) and then sweeps (2881). So this needs both
// the camera and the placed objects; field.c calls it from its fresh-entry branch, after both.
static void palSweepB0E6(CtScene* sc) {
  Mem* m = sc->mem;
  int c3 = m->s[0x1D0A] - 4; if (c3 < 0) c3 = 0;                 // B12E-B138
  int c4 = (m->s[0x1D0C] + 4) & 0xFF;                            // B13A-B140 (8-bit ADC, wraps)
  int c5 = (m->s[0x1D0E] & 0x80) ? 0 : m->s[0x1D0E];             // B142-B149 (BPL, no -2)
  int c6 = (m->s[0x1D10] + 5) & 0xFF;                            // B14B-B151
  for (int o = 0; o < sc->nobj; o++) {
    uint32_t X = 0x7E0000u + (uint32_t)o * 2;
    int cls = r8(m, X + 0x1100);
    if (cls & 0x80) continue;                                    // B0F6
    if (cls == 7) continue;                                      // B0FA (class 8 is admitted)
    int vis = r8(m, X + 0x1A81);
    if (vis == 0 || (vis & 0x80)) continue;                      // B12A / B12C
    // B155 `LDA $1801,X / ASL A` and B161 `LDA $1881,X / ASL A`: the tile bytes, not a position. An object the
    // script has not placed yet carries the load fill's $FF here (C0/571A).
    int tx = (r8(m, X + 0x1801) << 1) & 0xFF;                   // B155-B158
    int ty = (r8(m, X + 0x1881) << 1) & 0xFF;                   // B161-B164
    int inBox = !(tx < c3 || tx >= c4 || ty < c5 || ty >= c6);   // B159-B16B -> B190, nothing
    { static int pt = -1; if (pt < 0) { const char* e = getenv("CT_PALTRACE"); pt = (e && *e) ? 1 : 0; }
      if (pt) fprintf(stderr, "[palsweep] obj%d cls=%02X vis=%02X tile=(%02X,%02X) box=[%02X,%02X)x[%02X,%02X) -> %s\n",
                      o, cls, vis, tx, ty, c3, c4, c5, c6, inBox ? "admit" : "reject"); }
    if (!inBox) continue;
    if (!legsOn()) { w8(m, X + 0x0F00, 0x80); palAlloc7170(m, o); continue; }   // B175 / B188 (the legs off)
    uint8_t* R = m->s; int idx = o * 2;
    R[0x016D] = (uint8_t)idx;                                    // B0F1 STX $6D (D = $0100)
    w8(m, X + 0x0E80, 0x80); w8(m, X + 0x0E81, 0x80);            // B16F / B172
    w8(m, X + 0x0F00, 0x80);                                     // B175
    w8(m, X + 0x1681, 0x00); w8(m, X + 0x1601, 0x00);            // B17A / B17D
    w8(m, X + 0x1B00, 0x00);                                     // B180
    w8(m, X + 0x0F01, 0xFF);                                     // B185
    palAlloc7170(m, o);                                          // B188 JSR C0/7170
    CtObjSlotAlloc(R, idx);                                      // B18B JSR C0/6F9A
    CtObjScreenPos(R, idx);                                      // B101 JSR C0/AB45
    CtObjListLink(R, idx);                                       // B104 JSR C0/A9CD
    R[0x0179] = 0;                                               // B107 STZ $79
    for (int k = 0; k < 64 && CtAnimDrainVisit(R, gRomImg, (int)gRomImgLen, idx); k++) {}   // B109-B10C (BCS ->B109)
    CtAnimUploadFlush(R, NULL);                                  // B111 JSR C0/B1B2
    CtArenaFreeRun(R, idx);                                      // B117 JSR C0/EA42
  }
  if (legsOn()) CtAnimDrawPass(m->s, NULL);                      // B124 BRL C0/B271
}
void CtScenePalSweepEntry(CtScene* sc) { if (sc) palSweepB0E6(sc); }
// The menu's reload runs C0/286C whole. Its object legs after the camera (C0/0A50) and the OBJ slots (C0/6F79, the
// host's fieldPassObjSlots) are `2875 JSR C0/7155` (the eight palette owners $0B80-0B87 := 80), `2878 JSR C0/E935` (the
// arena owners $0BC0-0BC7), `287B JSR C0/B204` (the draw-list page $0E00-0EFF := 80), `287E JSR C0/A7E9` (one (00, FF)
// pair per object from $0F00) and `2881 JSR C0/B0E6`: the sweep relinks every object in the camera's box in index
// order, with no party tail, replacing the list the C0/01A5 MVN restored (field_draw_list_order.md sec. 4).
void CtSceneMenuReloadObjects(CtScene* sc) {
  if (!sc) return;
  uint8_t* r = sc->mem->s;
  memset(r + 0x0B80, 0x80, 8);                                   // C0/7155
  CtObjSceneInit(r, sc->nobj);                                   // C0/6F79 (again: the host's) / E935 / B204 / A7E9
  palSweepB0E6(sc);                                              // C0/B0E6
}

static int ctWipeOp(CtScene* sc, int sub);
// The poll's verdict (C0/61B4-61D6, see step()): 1 = keep waiting on `tgt` for a call made at priority `pr`, 0 =
// released. One body for the parked poll and for a 04/07 met with the latch already set. The release tests are the
// ROM's two bytes, not the port's `dead`: `61B4 LDA $1100,X / BMI` (op 0x0A's kill) and `61B9 LDA $1000,X / BMI` (the
// cadence pause: op 0x0B, a battle's dead-mark C0/07F3). A defeated enemy stays `dead` in the port, but op 0x0C
// (C0/6282) clears its $1000 bit 7 and the ROM then calls and waits on it like any other.
static int ssCallBusy(const CtScene* sc, int tgt, int pr) {
  if (tgt < 0 || tgt >= sc->nobj) return 0;        // no such object: $1100,X reads 80 (unloaded), 61B7 BMI -> released
  const CtSS* t = &sc->st[tgt];
  if ((r8(sc->mem, 0x7E1100 + (uint32_t)tgt * 2) & 0x80) || (r8(sc->mem, 0x7E1000 + (uint32_t)tgt * 2) & 0x80)) return 0;
  return t->addr >= 0 && t->prio <= pr;
}
static int step(CtScene* sc, int obj) {            // 1 = yield, 0 = continue
  CtSS* s = &sc->st[obj]; const uint8_t* B = sc->body; int blen = sc->blen, addr = s->addr;
  if (ssParked(s)) {                               // 0x04/0x07, IP on the call: wait for the callee's function
    // The wait poll (C0/61A7): wait while the callee's current priority ($1C00) is <= the called priority (61D2
    // `CMP $D9` / 61D6 `BCS keep-waiting`); release only when the callee climbs above it (returned past the
    // called function, which may still be deferred behind a higher one). Also released if the callee was
    // deactivated (0x0A/0x0B).
    if (ssCallBusy(sc, s->waitObj, s->waitPrio)) return 1;
    s->waitObj = -1;             // release: the visit goes on (SEC)...
    // ...but the poll is itself one of the visit's five dispatches: the parked IP is the 04's own byte (61A4
    // `TYX / CLC`), so the visit that finds the call complete dispatches the 04 again, and its completion exit
    // 61E0 `INX x3 / SEC` spends one unit of the budget (5AB6 `DEC $67`). The caller does not lose the visit.
    return 0;                    // 61E0 SEC: the completed poll is this visit's first dispatch
  }
  if (addr < 0 || addr >= blen) { s->addr = -1; return 1; }
  uint8_t op = B[addr]; int ln = opLen(B, addr);
  if (obj >= 0 && obj < 64) sc->atB0[obj] = (op == 0xB0 || op == 0xAF);   // both run C0/3002 (0xAF: C0/2FFD)
  if (obj >= 0 && obj < 64 && (op == 0xB0 || op == 0xAF)) g_ranB0[obj] = 1;
  { dbgFlags(); if (gEnvTraceAll && (obj == gEnvTraceAllObj || gEnvTraceAllObj < 0))   // CT_VMTRACE_ALL: every op; <0 = all objs
      fprintf(stderr, "  [run obj%d] t%d @%04X op=%02X p%d\n", obj, gVmTick, addr, op, s->prio); }
  if (gEnvTrace &&
      (op == 0xE3 || (op >= 0x02 && op <= 0x07) || op == 0x96 || op == 0xA0 ||
       op == 0xE4 || op == 0xE5 || op == 0x8E || op == 0x7C || op == 0x7D ||
       op == 0x90 || op == 0x91 ||
       ((op == 0xAA || op == 0xAB || op == 0xB7 || op == 0xAC) && obj == 1))) {
    fprintf(stderr, "[vm f%d] obj%d %04X:", gVmTick, obj, addr);
    for (int k = 0; k < ln && k < 8; k++) fprintf(stderr, " %02X", B[addr + k]);
    fprintf(stderr, "\n");
  }
  if (addr + ln > blen) { s->addr = -1; return 1; }
  int nxt = addr + ln;
  if (op == 0x00) {
    s->ret[s->prio] = 0;
    // A RETURN to idle clears the object's move state (hasTask/cycles), so its next function starts its moves
    // fresh. A RETURN that resumes a lower priority keeps it.
    // It also reclaims a PC's animation (move mode 0/1/2), standing in for C0/3002: once the script reaches its
    // perpetual control op 0xB0/0xAF, C0/9E84 (via 3040 -> 9E29) stamps the lead's $1680 every frame, and the
    // followers' 3002 arms drive anim 0. The port's followers return to idle instead of parking on a B0, hence
    // this arm. Conditions, each the ROM's:
    //  - control active ($011F != 0): a running cutscene never reaches 9E84, so its last animeset holds;
    //  - the RETURN reaches the control op: C0/5F74-5FB5 touches only the return slots ($7F0580) and the level
    //    ($1C00), so a resumed lower-priority function keeps its animeset until it reaches its own 0xB0/0xAF;
    //  - a looping animeset (mode 1); a mode-3 shape (op 0xAC) is a held pose and mode 2 self-reverts.
    // The stores are op 0xAE's return-to-auto, C0/2E8F into the shared tail C0/2E74, mode included (the bytes,
    // because ctAnimSyncActor rebuilds the C state from WRAM; and $1780 = 0, so C0/5614 can pick walk again):
    //     2E93 STA $1680,X (A=0)   2E74 STA $1780,X (A=0)   2E77 STZ $1601,X
    //     2E7C STA $7F0B01,X (0)   2E80 STZ $1681,X
    int retTo = -1;
    if (s->prio != 7) for (int p = s->prio + 1; p < 8; p++) if (s->ret[p] != 0) { retTo = s->ret[p]; break; }
    int reachesCtl = retTo < 0 || (retTo < blen && (B[retTo] == 0xB0 || B[retTo] == 0xAF));
    if (reachesCtl && r8(sc->mem, 0x7E011F) != 0 && sc->mode[obj] <= 2 && sc->actors[obj].animMode == 1) {   // PCs = move-mode 0/1/2 (lead + followers)
      sc->actors[obj].animMode = 0; sc->actors[obj].animIndex = 0; sc->actors[obj].animGen++;
      uint32_t AX = 0x7E0000u + (uint32_t)obj * 2;
      w8(sc->mem, AX + 0x1680, 0x00);                    // C0/2E93  STA $1680,X with A = 0
      w8(sc->mem, AX + 0x1780, 0x00);                    // C0/2E74  STA $1780,X: back to auto
      w8(sc->mem, AX + 0x1601, 0x00);                    // C0/2E77  STZ $1601,X
      w8(sc->mem, AX + 0x1681, 0x00);                    // C0/2E80  STZ $1681,X
      w8(sc->mem, 0x7F0000u + 0x0B01u + (uint32_t)obj * 2, 0x00);   // C0/2E7C  STA $7F0B01,X
    }
    if (s->prio == 7) { s->addr = -1; sc->hasTask[obj] = 0; sc->cycles[obj] = 0;
      return 1; }
    s->addr = -1; int found = 0;
    for (int p = s->prio + 1; p < 8; p++) if (s->ret[p] != 0) { s->prio = p; s->addr = s->ret[p]; found = 1; break; }
    if (!found) { s->prio = 7; sc->hasTask[obj] = 0; sc->cycles[obj] = 0; return 1; }
    // C0/5F74, the RETURN handler: the level's saved slot is cleared, $1C00,X climbs to the next level with a
    // saved ip (5F97-5FAA), and it returns with X = that ip and carry set (5FAC-5FB2), so the runner's loop (5AB4
    // BCC / 5AB6 DEC $67 / BNE 5AA3) keeps executing at the restored ip within this visit's budget. Only the idle
    // object's RETURN (5F7B `CMP #07 / BEQ 5FB3`: TYX / CLC) yields.
    return 0;                                      // 5FB1 SEC: the runner keeps going
  }
  if (op == 0xB0 || op == 0xB2 || op == 0xAF) {  // forever-yield: park. (A staged op 0xE0 commits in the $0119
                                                  // fade drain in CtSceneTick; the object parks on the E0.)
    // The touch pre-empt (C0/3002, disasm/notes/field_touch_preempt.md sec. 3): op 0xB0 is where a walked-into
    // object is pulled out of its own script (a pacing NPC's closed loop never reaches its touch function).
    //   3002: LDA $38 / BEQ ->3009 / TYX / CLC / RTS      ; global early-out while $0138 != 0
    //   300F: LDA $1100,X / BEQ ->3040 (lead) / DEC BEQ ->3030 / DEC BEQ ->3020 (followers)
    //   3040: LDA #01 / STA $1A01,X / JSR C0/9E29         ; lead body
    //   304C: LDA $1600,X / JSR C0/5B8D                   ; carry set iff $01EB bit7 clear
    //   3052: BCC ->3057
    //   3054: JSR C0/3154                                 ; <-- the only touch invoke in the ROM
    //
    // Only the state-0 (lead PC) branch carries the probe (C0/3054 is C0/3154's sole caller), so a follower or an NPC
    // on op 0xB0 fires nothing. `$01EB` is the hit latch: C0/881E resets it to 0x80 every frame and the proximity scan
    // C0/9923 stores the hit actor's index; the main loop runs the player (00A7) before the event VM (00C4), so the VM
    // reads an index produced earlier the same frame. The host supplies it via CtSceneSetTouchHit; -1 = 0x80.
    // The lead's animation: 3040's `JSR C0/9E29` opens with `JSR C0/9E84`, the facing/anim arm, on every visit of the
    // parked lead. Free, the host's controller runs it (field.c, CtPlayerAnimSelect after C0/881E); it runs here when
    // the d-pad is gated off. C0/8833 `LDA $0162 / BNE ->8840 / LDA $011F / BEQ ->8840 / JSR C0/88EE`: with $011F = 0
    // or a decision box up ($0162 = 1..3) C0/881E zeroes the intent ($1D2C/$1D2D, 882F/8831) and skips the d-pad, so
    // 9E84 takes slot 0 -> the idle chain C0/A1E9: `LDA $1F / BEQ ->A211` puts $1680 back to 0 (with $1681, $1601),
    // and the `LDA $62 / CMP #02 / CPY $97` arm (A1E9-A1F5 -> A1D2) sets the decision pose $1680 = 19.
    // (C0/9E29's tail, the trail push and 3040's `STA $1A01,X`, is not part of this.)
    // Op 0xAF is the same body: C0/2FFD is `JSR C0/3002 / INX / RTS`, so everything 3002 does runs for 0xAF too and
    // the INX steps past it (a control op with an exit).
    // A busy lead's step (CtSceneLeadDeferStep) lands here if his function returns onto his B0 within this pass.
    int leadDefer = op != 0xB2 && r8(sc->mem, 0x7E0138) == 0 && CtSceneActorMode((const CtScene*)sc, obj) == 0
                    && obj == sc->playerObj && g_leadDefer && g_leadDeferTick == gVmTick - 1;
    if (op != 0xB2 && gRomImg && gRomImgLen >= 0x10000
        && r8(sc->mem, 0x7E0138) == 0 && (leadDefer || r8(sc->mem, 0x7E0162) != 0 || r8(sc->mem, 0x7E011F) == 0)
        && CtSceneActorMode((const CtScene*)sc, obj) == 0) {
      if (!leadDefer) { w8(sc->mem, 0x7E1D2C, 0); w8(sc->mem, 0x7E1D2D, 0); }   // C0/882F / 8831 STZ $2C / $2D
      uint32_t X = (uint32_t)obj * 2;
      uint8_t was = sc->mem->s[0x1680 + X], wasF = sc->mem->s[0x1681 + X];
      CtPlayerAnimSelect(sc->mem->s, gRomImg, (int)gRomImgLen, (int)X);     // C0/3047 JSR C0/9E29 -> JSR C0/9E84
      if (sc->mem->s[0x1680 + X] != was || sc->mem->s[0x1681 + X] != wasF) {   // the C mirrors follow the bytes
        ctAnimSyncActor(&sc->actors[obj], sc->mem->s, (int)X); sc->actors[obj].animGen++;
      }
      if (leadDefer) sc->actors[obj].facing = sc->mem->s[0x1600 + X] & 3;  // 9E84's $1600 store
    }
    if (leadDefer) {                                         // the step, as the host would have taken it (CtSceneLeadStepFrom)
      g_leadDefer = 0;
      g_leadFromOk = 1; g_leadFromTick = gVmTick - 1; g_leadFromX = sc->fx[obj]; g_leadFromY = sc->fy[obj];
      CtSceneSetActorPosF(sc, obj, g_leadDeferX, g_leadDeferY);
    }
    if (op != 0xB2 && r8(sc->mem, 0x7E0138) == 0 && CtSceneActorMode((const CtScene*)sc, obj) == 0 && obj == sc->playerObj)
      g_leadTk = 1;                                          // 3040 LDA #01 / STA $1A01,X: the lead's step armed
    if (op != 0xB2 && sc->touchHit >= 0 && sc->touchHit < sc->nobj
        && r8(sc->mem, 0x7E0138) == 0                      // C0/3002's $0138 early-out
        && CtSceneActorMode((const CtScene*)sc, obj) == 0) { // state 0 = the lead PC only
      int hit = sc->touchHit;
      sc->touchHit = -1;                                     // one shot: the ROM's latch is re-armed
      CtSceneTouch(sc, hit);                                 //   to 0x80 by the next frame's C0/881E
    }
    if (op == 0xAF) s->addr = nxt;                           // C0/2FFD's INX
    return 1;
  }
  // op 0xB1 advances the IP then yields the visit (ROM handler ends CLC).
  if (op == 0xB1) { s->addr = nxt; return 1; }
  // The shared bodies with no C mirror (vmSharedOp above): their IP and their carry, as the ROM's handler leaves them.
  { CtHRet sh;
    if (vmSharedOp(sc->mem, B, sc->header, addr, obj, &sh)) { s->addr = sh.ip - sc->header; return sh.carry ? 0 : 1; }
    if (op == 0x0A || op == 0x0B || op == 0x0C) {
      vmObjEnableOp(sc->mem, sc->actors, sc->nobj, sc->mode, B, sc->header, addr, obj, &sh);
      s->addr = sh.ip - sc->header; return sh.carry ? 0 : 1;
    } }

  // ---- The six sound ops (disasm/notes/audio_commands.md sec. 6) ----
  // The complete set (every `JSL $C7:0004` and $2140-$2143 access in bank $C0): $E8/$EA/$EB/$EC stage the 4-byte
  // mailbox at $1E00-$1E03 and JSL the sound service; $ED/$EE poll an APU port. All six end `CLC`: they yield the
  // visit, like $AF/$B1.
  if (op == 0xE8 || (op >= 0xEA && op <= 0xEE && op != 0xE9)) {
    switch (op) {
      case 0xE8: case 0xEA: case 0xEB: case 0xEC:  // the four staging ops -> the shared implementation
        { int rr = vmSoundStage(sc->mem, sc->blen, B, addr, op, sc->rasterLine);   //   (also reached from the init pass, runFunc)
          // The synchronous upload (song_load_freeze.md sec. 6): the JSL returned after `rr>>10` frame boundaries,
          // on raster line `rr & 1023`. The runner loop (CtSceneTick pass 2) reads these after this visit.
          if (rr >= 0) { sc->sndPosted = 1; sc->sndFreezeFrames += rr >> 10; sc->rasterLine = rr & 1023; sc->sndOpAddr = addr; } }
        break;
      case 0xED:                                  // C0/3CBC: wait until $2141 == 0
        if (vmSoundPort(1) != 0) return 1;        // condition unmet: the handler does not INX,
        break;                                    //   so the op re-executes next visit
      case 0xEE:                                  // C0/3CC5: wait until ($2143 & $0F) == 0
        if ((vmSoundPort(3) & 0x0F) != 0) return 1;
        break;
    }
    s->addr = nxt; return 1;
  }
  // The pad branches 0x30/0x31/0x34-0x39/0x3B/0x3C/0x3F-0x44 (C0/66B2..6782) and 0x47 (C0/6788) run move.c's
  // CtVmPadBranchFamily through vmSharedOp above: the held pair $00F2 (34=A 35=B 36=X 37=Y 38=L 39=R), the dash byte
  // $00F8 (30 bit 1, 31 bit 7), and the edge latches $0150 (3B/3C) / $0151 (3F-44, the same masks as 34-39), which
  // C0/0097 `LDA $00F0 / TSB $51` accumulates every frame and only a taken branch's TRB clears. Not taken = jump by
  // the operand from its own address (66D0).
  if (op == 0xAD || op == 0xB9 || op == 0xBA || op == 0xBC || op == 0xBD) {
    // wait ops (ROM C0/2F96): the per-actor counter $7F0A01 gains +1 per visit while
    // below N; when it reaches N the op consumes, resets it and the visit continues.
    int N = (op == 0xAD) ? B[addr + 1] : (op == 0xB9) ? 4 : (op == 0xBA) ? 8
          : (op == 0xBC) ? 16 : 32;
    if (s->wait >= N) { s->wait = 0; s->addr = nxt; return 0; }
    s->wait++;
    return 1;
  }
  // Ops 0x08 / 0x09, C0/6240 `LDX $6D / LDA #01 / STA $1C01,X / TYX / INX / SEC / RTS` and C0/624B (`LDA #00`,
  // same tail): the only writers of $1C01,X, the invoke prologues' first refusal (5AC7 / 3156 `LDA $1C01,X / BNE`,
  // CtVmInvokeGate).
  if (op == 0x08) { w8(sc->mem, 0x7E1C01 + (uint32_t)obj * 2, 1); s->addr = nxt; return 0; }
  if (op == 0x09) { w8(sc->mem, 0x7E1C01 + (uint32_t)obj * 2, 0); s->addr = nxt; return 0; }
  if (op == 0xBB || op == 0xC0 || op == 0xC1 || op == 0xC2 || op == 0xC3 || op == 0xC4) {
    // C0/357A `LDA $29 / BNE ->35B2` comes before the request: a box already up parks a fresh op too (TYX /
    // CLC / RTS, no request, no latch).
    if (getenv("CT_TBOPDBG")) {
      // A decision op also carries the cursor machine: $0162 (0 none / 1 requested / 2 cursor live /
      // 3 confirmed), $0163 the cursor's line, $0164/$0165 the first/last option line, $0166 the confirmed
      // line the completion arm stores as the script's `result`. Printed only for 0xC0/0xC3/0xC4.
      char dec[96]; dec[0] = 0;
      if (op == 0xC0 || op == 0xC3 || op == 0xC4)
        snprintf(dec, sizeof dec, " $0162=%02X $0163=%02X $0164=%02X $0165=%02X $0166=%02X $0215=%02X",
                 r8(sc->mem, 0x7E0162), r8(sc->mem, 0x7E0163), r8(sc->mem, 0x7E0164),
                 r8(sc->mem, 0x7E0165), r8(sc->mem, 0x7E0166), r8(sc->mem, 0x7E0215));
      fprintf(stderr, "[tbop] f%d obj%d @%04X op=%02X str=%d $0129=%02X latch=%02X%s -> %s\n",
              gVmTick, obj, addr, op, B[addr + 1], r8(sc->mem, 0x7E0129),
              r8(sc->mem, 0x7F0A00 + (uint32_t)obj * 2), dec,
              r8(sc->mem, 0x7E0129) != 0 ? "PARK (box up)"
              : (r8(sc->mem, 0x7F0A00 + (uint32_t)obj * 2) == 0 ? "REQUEST" : "COMPLETE (advance)")); }
    if (r8(sc->mem, 0x7E0129) != 0) return 1;                   // the dialogue op parks while the box machine is up
    // ...then C0/3580 `LDX $6D / LDA $7F0A00,X / BNE ->35A7`: the object's own latch (a WRAM byte, so a seeded
    // state keeps it) decides between the request (latch clear: set it, post the box, park) and the completion
    // (latch set: clear it, advance).
    if (r8(sc->mem, 0x7F0A00 + (uint32_t)obj * 2) == 0) {
                  sc->textbox = 1; sc->tbIndex = B[addr + 1]; sc->tbTableAtShow = sc->tbTable;
                  sc->tbChoice = (op == 0xC0 || op == 0xC3 || op == 0xC4); sc->tbObj = obj;
                  // The decision ops (0xC0/0xC3/0xC4, len 3) carry the option-line config in operand2; the
                  // renderer needs it to place the cursor (option and continuation lines are both indented).
                  sc->tbOpt = sc->tbChoice ? B[addr + 2] : -1;
                  // ...and their request writes the cursor machine's three bytes, C0/3683-3697 `LDA $7F2001,X /
                  // STA $D9 / AND #03 / STA $65 / LDA $D9 / LSR / LSR / AND #03 / STA $64 / LDA #01 / STA $62`:
                  //   $0165 = last option line = operand2 & 3
                  //   $0164 = first option line = (operand2 >> 2) & 3
                  //   $0162 = 1 = a decision is up, cursor not yet armed
                  // The input frame turns on $0162 while a decision box is open (C0/1927, C0/201C;
                  // disasm/notes/textbox.md).
                  if (sc->tbChoice) {
                    w8(sc->mem, 0x7E0165, B[addr + 2] & 3);            // 3689-368B
                    w8(sc->mem, 0x7E0164, (B[addr + 2] >> 2) & 3);     // 368D-3693
                    w8(sc->mem, 0x7E0162, 1);                          // 3695-3697
                  }
                  sc->tbOp = op; s->tb = 1;
                  w8(sc->mem, 0x7E0129, 1);        // C0/3626 LDA #01 / STA $29: the box is up
                  // ...and the rest of the request, C0/3586-35A2 (op 0xBB; the five siblings differ only in
                  // the $30 immediate): `$7F0A00,X := 1 / $2A := string / $2E := $6D (the speaker) / STZ $32 /
                  // $29 := 1 / $30 := 0 (auto) | 1 (top, C1/C3) | 2 (bottom, C2/C4) / LDA #20 / TRB $54`. The box
                  // machine (tbMachineTick) reads $2E and $30 at its state 5 to place the window.
                  w8(sc->mem, 0x7F0A00 + obj * 2, 1);
                  w8(sc->mem, 0x7E012A, B[addr + 1]);
                  w8(sc->mem, 0x7E012E, (uint8_t)(obj * 2));
                  w8(sc->mem, 0x7E0132, 0);
                  w8(sc->mem, 0x7E0130, (op == 0xC1 || op == 0xC3) ? 1 : (op == 0xC2 || op == 0xC4) ? 2 : 0);
                  w8(sc->mem, 0x7E0154, (uint8_t)(r8(sc->mem, 0x7E0154) & ~0x20));
                  return 1; }
    // C0/3606 `LDA $29 / BNE -> CLC / RTS`: park while the box machine is busy, and take C0/3635's
    // advance arm only once it has cleared $0129 (at the end of the close ramp).
    if (r8(sc->mem, 0x7E0129) != 0) return 1;                   // C0/3606: park while the box machine is busy
    // C0/365D-3669, the decision completion arm, and the 0xC3/0xC4 twins C0/36C6-36D2 and C0/36FA-3706, are all
    // `LDA #00 / STA $7F0A00,X / LDA $66 / STA $7F0A80,X / STZ $62`. The result is $0166, the line the cursor was
    // confirmed on (written by the cursor arm C0/1AFE), and the arm clears $0162, releasing the input frame.
    if (op == 0xC0 || op == 0xC3 || op == 0xC4) {
      uint8_t res = (uint8_t)r8(sc->mem, 0x7E0166);              // 3663 LDA $66
      w8(sc->mem, 0x7F0A80 + (uint32_t)obj * 2, res);            // 3665 STA $7F0A80,X
      if (obj >= 0 && obj < sc->nobj) sc->actors[obj].result = res;   // op 0x1A's C mirror
      w8(sc->mem, 0x7E0162, 0);                                  // 3669 STZ $62
      sc->tbPendChoice = -1;
    }
    w8(sc->mem, 0x7F0A00 + obj * 2, 0);           // C0/3635 LDA #00 / STA $7F0A00,X
    sc->textbox = 0; s->tb = 0; s->addr = nxt; return 0;
  }
  if (op == 0xB8) {                                // C0/3557: the string-table pointer lives at $012B/$012C/$012D
    // (TYX / INX / LDA $7F2001,X / STA $2B / ... STA $2C / ... STA $2D, D = $0100). sc->tbTable is the host's
    // file-address view of the same bytes. Op 0xBB's precondition is C0/3570 `LDA $2D / BNE`: no bank byte, no box.
    w8(sc->mem, 0x7E012B, B[addr + 1]); w8(sc->mem, 0x7E012C, B[addr + 2]); w8(sc->mem, 0x7E012D, B[addr + 3]);
    sc->tbTable = (B[addr + 1] | (B[addr + 2] << 8) | (B[addr + 3] << 16)) - 0xC00000; s->addr = nxt; return 0;
  }
  if (op == 0xFF) {                                  // screen/effect sub-commands (ROM C0/3FAF jumptable).
    // The operand splits the opcode at $90 (C0/3FB7 `CMP #90 / BCS ->4000`). Only the >= $90 half is the
    // screen-effect jump table below. The < $90 half leaves the field engine: C0/3FBB ramps $19 (the INIDISP
    // shadow) to black, C0/3FD3 quiesces (JSR C0/0B4E), C0/3FD6 resets the stack (`LDX #06FF / TXS`), and C0/3FDC
    // `LDA $DF / BMI ->3FE4` splits it again: < $80 does C0/3FE0 `JML $C3:0000` with the operand in A (the $C3
    // special sequences, sequence_table.md: $C3:0000 stores A at $0384 and runs the overlay; entry 2 is the title; a sequence
    // ends with `JML $C0:0000`, so the field re-enters at C0/0066 with $0100 unchanged); >= $80 JMLs through
    // $FD:FFF1 / FFEB / FFE8 / FFE5 into FD/DA00 / DA5F / DABE / DB1D, the time-travel effect, which reboots into the
    // $0100 the preceding DD/DE staged (disasm/notes/gate_warp_fd_family.md). Both park the visit here and the host
    // runs them; the script never resumes.
    // The fade before the jump is the op's own, and its first pass runs here, in the op's frame: `3FBB LDA $19 / BEQ
    // ->3FD3 / BMI ->3FD3` (black already: jump now), else `3FC1 DEC $19 / JSR C0/881E / JSL FD/FFF7 / JSR C0/EC60`
    // and `3FCD LDA $19 / CMP #01 / BPL ->3FC1`, one DEC a frame until $19 is 0, the jump on the frame after. Nothing
    // else of the field runs meanwhile (the scheduler is inside this visit). Only the op's first visit takes the DEC.
    if (B[addr + 1] < 0x90 && !sc->seqHas && !sc->warpReq) {
      uint8_t b19 = r8(sc->mem, 0x7E0119);
      sc->ffFade = (b19 != 0 && !(b19 & 0x80));
      if (sc->ffFade) { w8(sc->mem, 0x7E0119, (uint8_t)(b19 - 1)); CtSceneSyncBright(sc); }
    }
    if (B[addr + 1] < 0x80) { sc->seqReq = B[addr + 1]; sc->seqHas = 1; return 1; }
    if (B[addr + 1] < 0x90) { sc->warpReq = B[addr + 1]; return 1; }
    if (B[addr + 1] >= 0x92 && B[addr + 1] <= 0x95) {  // the window wipe: parks until C3/01E4 has run its 32 calls
      if (ctWipeOp(sc, B[addr + 1])) s->addr = nxt;
      return 1;
    }
    if (B[addr + 1] == 0x90) {                        // C0/4036: TYX / INX / INX / LDA $7F2001,X -> $3A, -> $3B, -> $3D,
      w8(sc->mem, 0x7E013A, B[addr + 2]); w8(sc->mem, 0x7E013B, B[addr + 3]);   // STZ $3C, LDA #01 / STA $39: the iris
      w8(sc->mem, 0x7E013D, B[addr + 4]); w8(sc->mem, 0x7E013C, 0);             // opens at centre ($3A, $3B) to radius $3D
      w8(sc->mem, 0x7E0139, 1); sc->irisState = 1;   // (C0/21E1 steps it, ctIrisFrame)
    } else if (B[addr + 1] == 0x91) {                 // C0/4055: LDA #02 / STA $39: it closes from wherever $3C is
      w8(sc->mem, 0x7E0139, 2); sc->irisState = 2;
    } else if (B[addr + 1] == 0x97) {                 // C0/4191: 0x90's stores (4191-41A8), then `LDA #04 / STA $39`: the
      w8(sc->mem, 0x7E013A, B[addr + 2]); w8(sc->mem, 0x7E013B, B[addr + 3]);   // circle grows as the colour window
      w8(sc->mem, 0x7E013D, B[addr + 4]); w8(sc->mem, 0x7E013C, 0);             // (C0/232A, ctColCircleFrame)
      w8(sc->mem, 0x7E0139, 4);
    } else if (B[addr + 1] >= 0x98 && B[addr + 1] <= 0x9C) {
      // C0/41B0 / 41B4 / 41B8 / 41C0 / 41C4: one tail, five immediates: `LDA #05|#07|#09|#0B|#0E / BRA ->41BA`,
      // 41BA `STA $39 / TYX / INX / INX / RTS` (the table C0/400E, subs 98-9C). They hand C0/21E1 an entry -- 9C's 0E
      // is C0/23E2, the teardown of the line entry 6 draws (the combat tutorial's `FF 9C` after each `FE`). Two bytes;
      // the CLC at 400C below ends the visit as for every sub.
      static const uint8_t k39[5] = { 0x05, 0x07, 0x09, 0x0B, 0x0E };
      w8(sc->mem, 0x7E0139, k39[B[addr + 1] - 0x98]);
    } else if (B[addr + 1] == 0x9D) {                 // C0/41DC `LDA #02 / TRB $54`: the push program's end (vmTouchPush)
      w8(sc->mem, 0x7E0154, (uint8_t)(r8(sc->mem, 0x7E0154) & ~0x02));
    } else if (B[addr + 1] == 0x96) {
      // C0/4181, the table's sub 6: `LDA $4210 / BPL ->4181` (the vblank-flag spin, NMI on), `JSR C0/0B4E` (SEI,
      // force blank, NMI and HDMA off), `LDX #06FF / TXS`, `JML FD/C000`, the reset vector's target ($00:FF00
      // `SEI / CLC / XCE / JML FD:C000`): a cold reboot. It never returns to the runner. (One site: scene 0AC's obj9
      // fn1, taken when $7F01EC is non-zero, the byte C1:8143 sets when the party loses.) The visit parks on the op
      // and the host performs the spin and the cold init at the vblank, as for 0xDF/0xE1 (field.c).
      sc->rebootReq = 3;
      return 1;                                     // park: s->addr stays on the op
    }
    s->addr = nxt;
    // ...and it ends the visit: C0/3FAF's half is `4000 ... 4009 JSR ($400E,X) / 400C CLC / 400D RTS`, and that CLC
    // is unconditional, so the runner's `5AB4 BCC` closes the visit and the next ops wait for the next frame. (opLen
    // gives 5 for 0x90/0x97 and 2 otherwise, C0/4036's four INXs and C0/41BA's two; the parking subs 0x92-0x95 run
    // above, ctWipeOp.) Not ported here: 0x9E (C0/41C8: `LDA $39 / BEQ` -> `LDA #0C / STA $39` and park until C0/21E1's
    // entry 0C ends in 0D); entries 7-0D have no port (docs/ROADMAP.md F19), so parking on it would hold the scene for
    // good. src/move.c's CtFieldRunnerOne has the whole ladder (--sweepops FF:60).
    return 1;
  }
  if (op == 0xC8) {                                 // Special-Dialog (ROM C0/3757). The operand lands in
    int arg = B[addr + 1];                          // $0125 and the launcher dispatch C0/0CF2-0D18 turns
    // it into a menu mode:
    //   0CF4  BEQ ->0D04        operand $00        -> mode 5   (BRL C0/19C7)
    //   0CF6  BMI ->0D09        operand >= $80     -> mode = (operand ROL x3) & 3  ($C0+id = 3)
    //   0CF8  BIT #01 / BNE     operand odd        -> C0/0D00 `LDA #00` = mode 0, the loader
    //   0CFC  LDA #06           operand even       -> mode 6 (screen $0B, the save point's save: its substate
    //                                                  table C2/D4F0 reuses the Save screen's CF61/CF92; `C8 40`)
    //   (a >= $80 operand below $C0 is mode 2 = screen $0C, the shop; see the arm further down)
    if (arg >= 0xC0 && arg <= 0xC7) {               // mode 3 -> screen $0D: the ATB selector, then the
      if (!s->nm) { sc->naming = 1; sc->nameChar = arg - 0xC0; s->nm = 1; return 1; }  // name-entry screen
      if (sc->naming) return 1;                     // still on the naming screen -> keep yielding
      s->nm = 0; s->addr = nxt; return 0;           // name entered (field cleared sc->naming) -> resume
    }
    // Mode 0 = the load / new-game shell (C2/8000 -> screen $0E); its only sites are scene 0's 0x0032 and 0x00CE.
    // It blocks the script like naming, and in the ROM never resumes: both exits end `JML $C0:0000`, a reboot. The
    // host clears sc->loader once it has rebooted; the resume arm lets a host that declines step over it.
    if (arg && arg < 0x80 && (arg & 1)) {
      if (!s->nm) { sc->loader = 1; sc->loaderArg = arg; s->nm = 1; return 1; }
      if (sc->loader) return 1;
      s->nm = 0; s->addr = nxt; return 0;
    }
    // Mode 2 = the shop (screen $0C; C2/808F[2]). C0/0D09: X = operand & $3F -> $0A01 = the shop number, which the
    // screen's init C2/DACA indexes the list table $CC:2C6D with. The op is C0/3757's handshake: the first visit stores
    // the operand in $0125 and arms $0117 bit6 (park, CLC); the main loop fades (C0/0CCE) and launches; on the menu's
    // return C0/0D33-0D39 clears $0117 bit6 and sets $0118 bit6, and the next visit consumes that (`3774 TRB $18`),
    // steps over the operand and continues (SEC). The host clears sc->shop when the shop has closed.
    if (arg >= 0x80 && arg < 0xC0) {
      if (!s->nm) { sc->shop = 1; sc->shopArg = arg; s->nm = 1; return 1; }
      if (sc->shop) return 1;
      s->nm = 0; s->addr = nxt; return 0;
    }
    // Mode 6 = the save point's save (screen $0B; C2/808F[6]): an even operand (`C8 40`, C0/0CFC `LDA #06`) in the
    // save-point object of every location with one. The same C0/3757 handshake as the shop's, so it parks in the
    // shop's fields (CtSceneShop; the host tells them apart by the operand's bit 7, CtSceneSavePoint).
    // Mode 5 = the party exchange (operand $00: C0/0CF4 `BEQ ->0D04`, `LDA #05 / BRL C0/19C7`, the Y button's own
    // `LDX #0000 / ... / JSL C2/8000`, so screen $07 with $71 = 0, and the Y path's return C0/19D2, whose `$0117 bit6 ->
    // $0118 bit6` is this op's handshake). Parked in the shop's fields with operand 0 (CtSceneExchange); the host clears
    // sc->shop when the screen has closed.
    if (!(arg & 1) && arg < 0x80) {
      if (!s->nm) { sc->shop = 1; sc->shopArg = arg; s->nm = 1; return 1; }
      if (sc->shop) return 1;
      s->nm = 0; s->addr = nxt; return 0;
    }
    s->addr = nxt; return 0;
  }
  if (op == 0xC7 || op == 0xC9 || op == 0xCA || op == 0xCB ||
      op == 0xCC || op == 0xCD || op == 0xCE) {
    // ---- The $C1 item + gold family: C0/3780 (0xC9 has-item), C0/37B1 (0xCA add-item), C0/37C7 (0xC7 add-item,
    // id from the locals), C0/37E3 (0xCB remove-item), C0/3827 (0xCC has-gold), C0/385B (0xCD add-gold), C0/3874
    // (0xCE sub-gold). All seven share one shape:
    //   TYX / INX / TDC / XBA / LDA $7F2001,X / INX / STX $C7 / TAY / LDA #leaf / JSL $C1:8003
    // i.e. the operand in Y, a leaf index in A, and the gate, which is move.c's ctVmC1Gate (shared body,
    // disasm/notes/vm_two_implementations.md). `mem->s` is the flat 128 KB image it indexes.
    // Carry is set on every exit of all seven, so the visit continues. Lengths and branch arms live in the body.
    int ni = addr;
    CtVmItemFamily(sc->mem->s, B, addr, &ni);
    g_ctVmItemOps++;              // liveness counter for CT_BAGDUMP
    s->addr = ni; return 0;
  }
  if (op >= 0x4A && op <= 0x4D) {
    // ---- The poke family: C0/67D7 (0x4A byte), C0/67FB (0x4B word), C0/680C (0x4C local byte), C0/6829 (0x4D
    // local word), all through the 24-bit pointer C0/67E3 stages at $01D0-$01D2; move.c's CtVmPokeFamily. SEC on
    // every exit. The IP is the ROM's, as for 0x88 below: 0x4C/0x4D spill it into $01D9 (`6813 STX $D9`), counted
    // from $7F:2001, pointer table included.
    int ni = addr + sc->header;
    CtVmPokeFamily(sc->mem->s, B - sc->header, addr + sc->header, &ni);
    s->addr = ni - sc->header; return 0;
  }
  if (op == 0x7A) {
    // ---- Op 0x7A, C0/4D06: the jump toward a tile; move.c's CtVmOp7A. Its two mirrors are this runner's mover pair:
    // $1A80,X = hasTask (the 4D08 armed test) and $1A01,X = cycles (the 4D0D ticket). Armed and running -> park (4D12
    // CLC); armed and spent -> `STZ $1A80 / STZ $1B80`, ip + 4, SEC (4D15-4D20); not armed -> the arm on the WRAM bytes
    // ($1A00 the speed, $1801/$1881 the tile, the committed position) and park (4E21 LDX $C7 / CLC). The motion is
    // C0/AAC7's, in the integration pass (the $1B80 arm there).
    uint32_t X = (uint32_t)obj * 2;
    if (sc->hasTask[obj]) {                                          // 4D08 LDA $1A80,X / BEQ
      if (sc->cycles[obj] > 0) return 1;                             // 4D0D LDA $1A01,X / BNE -> 4D12 TYX / CLC
      sc->hasTask[obj] = 0;
      w8(sc->mem, 0x7E1A80 + X, 0); w8(sc->mem, 0x7E1B80 + X, 0);   // 4D15 / 4D18
      s->addr = addr + 4; return 0;                                  // 4D1B TYX / INX x4 / SEC
    }
    if (!gRomImg) { s->addr = addr + 4; return 0; }                  // no ROM tables: cannot aim (harness only)
    w8(sc->mem, 0x7E1A00 + X, sc->mspd[obj]);                        // the speed op 0x89/0x8A set (the C mirror)
    w8(sc->mem, 0x7E1A80 + X, 0); w8(sc->mem, 0x7E1A01 + X, 0);
    CtHRet h;
    CtVmOp7A(sc->mem->s, gRomImg, B - sc->header, addr + sc->header, (int)X, &h);
    sc->cycles[obj] = r8(sc->mem, 0x7E1A01 + X);                     // 4D7C / 4DCF / 4DF6 the ticket
    sc->hasTask[obj] = 1;                                            // 4E1B $1A80 = 1
    sc->mx[obj] = sc->my[obj] = 0; sc->vecMove[obj] = 0;
    s->addr = h.ip - sc->header; return 1;                           // 4E21 CLC: park on the arm
  }
  if (op == 0xF1 || op == 0xFE) {
    // ---- The screen-effect ops: C0/3D07 (0xF1: COLDATA $0121/$0122, CGADSUB $0BE0, the $0118 request) and C0/3F26
    // (0xFE: the eight 8.8 channels $7F:1CE9.., their deltas through the divider, the countdown $7F:1CE8, $0139 = 6);
    // move.c's CtVmScreenFxOp. 0xFE exits CLC (3FAD); 0xF1 SEC on its two-operand arms, CLC on the transition arms
    // (3D41).
    CtHRet h;
    CtVmScreenFxOp(sc->mem->s, B - sc->header, addr + sc->header, &h);
    s->addr = h.ip - sc->header; return h.carry ? 0 : 1;
  }
  // ---- op 0x47 (C0/6788): `INY / TYX / LDA $7F2001,X / STA $6B / BRA 6784` (INY / TYX / SEC / RTS) -- the anim drain's
  // raster deadline ($016B, the byte C0/CA99 compares the V counter with) from the operand; two bytes, the visit goes on.
  if (op == 0x48 || op == 0x49 || op == 0x5B || op == 0x5E || op == 0x5F || op == 0x60 || op == 0x61 ||
      op == 0x63 || op == 0x64 || op == 0x67 || op == 0x69 || op == 0x6B || op == 0x6F) {
    // ---- The pointer read (C0/6792 / 67C7) and the local arithmetic (C0/6A1F..6C68): move.c's CtVmReadPtrFamily /
    // CtVmLocalArithFamily (0x63/0x64 use the $FF20/$FF28 tables; 0x6F is a do-while on $01D9). All SEC. IP from
    // $7F:2001, as for the grid.
    int ni = addr + sc->header;
    if (op == 0x48 || op == 0x49) CtVmReadPtrFamily(sc->mem->s, B - sc->header, addr + sc->header, &ni);
    else CtVmLocalArithFamily(sc->mem->s, gRomImg, (int)gRomImgLen, B - sc->header, addr + sc->header, &ni);
    s->addr = ni - sc->header; return 0;
  }
  if ((op >= 0x50 && op <= 0x56) || op == 0x58 || op == 0x59) {
    // ---- The variable-assignment grid: C0/68C6 (0x50 imm word->local), 68E9/6913 (0x51/0x52 local->local),
    // 693D/6964 (0x53/0x54 global->local), 698B (0x55 storyline->local), 69A6 (0x56 imm->global), 69C4/69EB
    // (0x58/0x59 local->global), 8 and 16 bits; move.c's CtVmVarFamily (it also spills $01D9 as the ROM does).
    int ni = addr + sc->header;
    CtVmVarFamily(sc->mem->s, B - sc->header, addr + sc->header, &ni);
    s->addr = ni - sc->header; return 0;
  }
  if (op == 0x2E) {
    // ---- C0/4A4E, the colour-effect record: move.c's CtVmColorSlotOp (the service is FD/E5ED's). Same IP
    // convention and carry as op 0x88 below.
    CtHRet cs; cs.ip = nxt + sc->header; cs.carry = 1; cs.ok = 0;
    CtVmColorSlotOp(sc->mem->s, B - sc->header, addr + sc->header, &cs);
    s->addr = cs.ip - sc->header;
    return cs.carry ? 0 : 1;
  }
  if (op == 0x88) {
    // ---- C0/4892, the per-object colour effect: move.c's CtVmColorFx. The record lands in $0520 and the VM pass
    // ticks it (CtObjSlotService, src/objslot.c, the battle's body: `88 4x` fade to white FD/E82C, `88 5x` scale
    // FD/E5A4). The C mirrors stay (applyEffect below): the renderer draws the rotation (modes 2/3, FD/E532, not
    // ported into the page) from palRot. Carry: the two allocating arms exit CLC when the eight slots are taken
    // (C0/491E / C0/4975): the op is consumed and the visit ends.
    // The IP is the ROM's: `4892 STY $C7` spills it into $01C7/$01C8, counted from $7F:2001, i.e. including the
    // pointer table (sc->header bytes), so the shared body gets `B - header` and `addr + header`.
    CtHRet cf; cf.ip = nxt + sc->header; cf.carry = 1; cf.ok = 0;
    CtVmColorFx(sc->mem->s, gRomImg, (int)gRomImgLen, B - sc->header, addr + sc->header, obj * 2, &cf);
    applyEffect(sc, obj, addr, op);
    s->addr = cf.ip - sc->header;
    return cf.carry ? 0 : 1;
  }
  if (op == 0xCF || op == 0xD0 || op == 0xD1 || op == 0xD3 || op == 0xD4 || op == 0xD7 ||
      op == 0xF8 || op == 0xF9 || op == 0xFA) {
    // ---- The $C2 party / roster family: C0/388D (0xCF has-character; jumps when the leaf returns nonzero),
    // C0/38BE (0xD0 add to the reserve roster), C0/38D3 (0xD1 remove from it), C0/3919 (0xD3 join), C0/394E (0xD4
    // leave), C0/37F9 (0xD7 count an item into a local), C0/3E5C/3E6E/3E75 (0xF8/0xF9/0xFA heal the party); move.c's
    // CtVmPartyGateFamily. With no ROM image every ROM read inside the gate is bounds-checked, so it takes the
    // routine's own zero-fill arm.
    // 0xD3 is leaf 1 (C2/8C79) whole: `JSR C2/8CF7` first (the joiner's reserve entry marked empty, `| 80`), eviction
    // into the reserve when the three slots are full (8C88-8CCC), and C0/392F-3949 ($78, the object into $0199/$019B).
    // 0xD4 compares `$1101,X` for X = $0197/$0199/$019B (the objects, not the roster $2980), and the carry that
    // decides the visit is the last `CMP $D9` that ran; with every slot empty (`3998 BMI ->39B1`) it stays the entry
    // 0 and each D4 ends its visit.
    // 0xD2 is not routed here: it has its own arm below.
    CtHRet pg; pg.ip = addr; pg.carry = 1; pg.ok = 0;
    CtVmPartyGateFamily(sc->mem->s, gRomImg, (int)gRomImgLen, B, addr, &pg);
    g_ctVmPartyOps++;
    // CT_SLOTTRACE also prints each distinct (op, operand, resulting IP), so an operand read from the wrong source
    // (the ROM's $7F:2001 instead of this runner's body-relative script) shows. Deduped.
    if (gSlotTrace < 0) gSlotTrace = getenv("CT_SLOTTRACE") ? 1 : 0;
    if (gSlotTrace) {
      uint32_t key = 0x80000000u | ((uint32_t)op << 16) | ((uint32_t)B[addr + 1] << 8) | (uint32_t)(pg.ip & 0xFF);
      int k = 0;
      for (; k < gSlotSeenN; k++) if (gSlotSeen[k] == key) break;
      if (k == gSlotSeenN && gSlotSeenN < (int)(sizeof gSlotSeen / sizeof gSlotSeen[0])) {
        gSlotSeen[gSlotSeenN++] = key;
        printf("gateop: op=%02X arg=%02X ip %04X->%04X carry=%d\n", op, B[addr + 1], addr, pg.ip, pg.carry);
      }
    }
    s->addr = pg.ip;
    return pg.carry ? 0 : 1;      // this runner's protocol is inverted: 1 = yield, and carry 0 = yield
  }
  if (op >= 0x02 && op <= 0x07) {
    // 0x05-0x07 (C0/61E9 / 61FE / 6214) are 0x02-0x04 with the target read from the party-slot table: `LDA
    // $7F2001,X (the slot byte) / TAX / LDA $97,X` = $0197 + slot, the object hosting that member (obj * 2), then
    // `BRL` into the parent's body. An empty slot (`BMI`, 0x80) skips the op: ip + 3, SEC.
    // A 04/07 met with the latch already set polls on its own operands and installs nothing. The latch $7F0980,X
    // (s->waitObj >= 0) is read first: C0/6109 `LDX $6D / LDA $7F0980,X / BEQ ->611A (install) / BRL ->61A7`, and
    // 0x07's C0/6214 `BNE ->6232` -> 6236 `LDA $7F2001,X / TAX / LDA $97,X` (no empty-slot test) -> `BRL 61AF`. The
    // poll (61A7-61E8) waits (61E6 `TYX / CLC`, the IP on the op) while the op's priority >= the target's $1C00, and
    // on release clears the latch (61D8-61DC) and steps past the op (61E0 `INX x3 / SEC`). The latch is still set
    // here when a higher-priority function pre-empted a parked waiter and this op belongs to the new function
    // (disasm/notes/call_wait_latch_preempt.md sec. 4).
    if ((op == 0x04 || op == 0x07) && s->waitObj >= 0) {
      int t = (op == 0x04) ? B[addr + 1] / 2 : vmSlotTarget(sc->mem, op, B[addr + 1]) / 2;   // 61AB / 623B
      int p = (B[addr + 2] & 0xF0) >> 4;                                                    // 61C6-61D0
      s->waitObj = t; s->waitPrio = p; s->callAddr = addr; s->addr = addr + 3;              // parked on this op
      if (ssCallBusy(sc, t, p)) return 1;                                                   // 61E6 TYX / CLC
      s->waitObj = -1;                                                                      // 61D8-61DC latch := 0
      return 0;                                                                             // 61E0 INX x3 / SEC
    }
    int tgt;
    if (op <= 0x04) tgt = B[addr + 1] / 2;
    else {
      int v = vmSlotTarget(sc->mem, op, B[addr + 1]);                // 61F2 / 6207 / 6225
      if (v & 0x80) { s->addr = addr + 3; return 0; }                 // 61F4 / 6209 / 6227 BMI -> ip+3, SEC
      tgt = v / 2;
    }
    int pr = (B[addr + 2] & 0xF0) >> 4;
    reqScratch(sc, op, tgt, pr);
    // 0x03/0x04 (and 0x06/0x07, which BRL into their bodies) retry until the target takes the call; only 0x02/0x05
    // queue or drop it (reqOp). Both bodies refuse the same way, C0/6086-60B2 and C0/6122-614E:
    //   LDA $1C01,X / BNE      -> 6106 / 61A4  `TYX / CLC`  the IP stays ON the op and the visit ends
    //   LDA $1100,X / BMI      -> 6100 / 6114  `INX x3 / SEC`  skipped (an inactive target)
    //   LDA $1000,X / BMI      -> the same skip (a paused cadence)
    //   new prio CMP $1C00,X / BEQ / BCS  -> 6106 / 61A4: equal or less urgent than what the target runs now = wait
    // and op 0x02 (C0/5FB6) is the one that skips an equal priority (5FE7 BEQ ->603A) or queues a lesser one in
    // $7F0580 (5FE9 BCS ->603E).
    if (op == 0x03 || op == 0x04 || op == 0x06 || op == 0x07) {
      if (tgt < sc->nobj) {
        if (r8(sc->mem, 0x7E1C01 + (uint32_t)tgt * 2)) { s->addr = addr; return 1; }               // 608E / 612A
        if ((sc->mode[tgt] & 0x80) || sc->actors[tgt].cadPause) { s->addr = nxt; return 0; }      // 6100 / 6114
        if (pr >= sc->st[tgt].prio) { s->addr = addr; return 1; }                                   // 60B0 / 614C
      }
    }
    s->addr = nxt;
    reqOp(sc, tgt, B[addr + 2] & 0x0F, pr);
    // 0x04/0x07 = call and wait for the callee to finish.
    if ((op == 0x04 || op == 0x07) && tgt < sc->nobj) { s->waitObj = tgt; s->waitPrio = pr; s->callAddr = addr; return 1; }
    return 0;
  }
  if (op == 0x10) { s->addr = addr + B[addr + 1] + 1; return 0; }
  if (op == 0x11) { s->addr = addr - B[addr + 1] + 1; return 0; }
  if (op == 0xD2) {                                  // party-membership test (C0/38E8 -> C2/8C36
    // mode 0): byte1 = PC char id, byte2 = jump offset. The query scans the 3 active-party char ids at
    // $7E2980/81/82 (C2/8CD1, limit 3) and returns 0 if found, 0xFF if not; op 0xD2's BNE takes the jump when
    // the PC is absent. Target = (addr+2) + B[addr+2]; present falls through (addr+3).
    int pc = B[addr + 1], present = 0;
    for (int k = 0; k < 3; k++) if (r8(sc->mem, 0x7E2980 + k) == pc) { present = 1; break; }
    s->addr = present ? nxt : (addr + 2 + B[addr + 2]);
    // C2/834D, the gate's epilogue, runs after every leaf of C2/8C36, this query included; this arm does not go
    // through the gate, so it is called here: it paints the three weapon rows (colours 13-15 of OBJ slots 5/6/7,
    // $7E:21BA/$21DA/$21FA, from bank $D1 via C2/F5ED), rebuilds the triple-tech flags and rescans the relics.
    CtVmC2Epilogue(sc->mem->s, gRomImg, (int)gRomImgLen);
    return 0;
  }
  if (op == 0x2D) {
    // C0/66A5: skip unless something is held. $00F8 is the button latch, $00F9 the direction
    // latch; either one non-zero falls through into the guarded block, both zero takes the jump.
    //   66A6 LDA $00F8 / 66A9 BNE ->6701     (6701: INY / TYX / SEC / RTS -- fall through)
    //   66AB LDA $00F9 / 66AE BEQ ->66D0     (66D0: TYX / LDA $7F2001,X / TXA / CLC / ADC -- jump)
    //   66B0 BRA ->6701
    // Note: the jump base is addr+1, not addr+2: C0/66D0 is a bare `BB TYX` with no `E8 INX` (ops 0x27/0x28 do
    // `TYX / INX`), which is also why this op is two bytes where they are three. (The attract's demo-abort
    // handler is guarded by this op.)
    if (r8(sc->mem, 0x7E00F8) || r8(sc->mem, 0x7E00F9)) s->addr = nxt;    // 6701
    else s->addr = addr + 1 + B[addr + 1];                                // 66D1 / 66DE
    return 0;
  }
  if (op == 0x12 || op == 0x13 || op == 0x14 || op == 0x15 || op == 0x16 || op == 0x18) {   // (0x1A: vmSharedOp)
    Mem* m = sc->mem; int jump = 0, target = nxt;
    if (op == 0x12) { if (!cmp_(r8(m, LOC + B[addr + 1] * 2), B[addr + 2], B[addr + 3])) { jump = 1; target = addr + B[addr + 4] + 4; } }
    else if (op == 0x13) { if (!cmp_(r16(m, LOC + B[addr + 1] * 2), B[addr + 2] | (B[addr + 3] << 8), B[addr + 4])) { jump = 1; target = addr + B[addr + 5] + 5; } }
    else if (op == 0x14) { if (!cmp_(r8(m, LOC + B[addr + 1] * 2), r8(m, LOC + B[addr + 2] * 2), B[addr + 3])) { jump = 1; target = addr + B[addr + 4] + 4; } }
    else if (op == 0x15) { if (!cmp_(r16(m, LOC + B[addr + 1] * 2), r16(m, LOC + B[addr + 2] * 2), B[addr + 3])) { jump = 1; target = addr + B[addr + 4] + 4; } }
    // Op 0x16 = C0/63E6 (see the init pass, runFunc, for the disassembly).
    else if (op == 0x16) { int li = B[addr + 1]; if (B[addr + 3] & 0x80) li += 0x100; if (!cmp_(r8(m, GLB + li), B[addr + 2], B[addr + 3] & 0x7F)) { jump = 1; target = addr + B[addr + 4] + 4; } }
    else { if (!(r8(m, GLB) < B[addr + 1])) { jump = 1; target = addr + B[addr + 2] + 2; } }   // 0x18
    s->addr = jump ? target : nxt;
    return 0;
  }
  if (op == 0x92 || op == 0x94 || op == 0x95 || op == 0x96 || op == 0x97 || op == 0x9A || op == 0x9C || op == 0x9D || op == 0xA0 || op == 0xA1 || op == 0xB5 || op == 0xB6 ||
      op == 0x9E || op == 0x9F || op == 0x98 || op == 0x99 || op == 0x8F) {
    int r = moveOp(sc, obj, addr, op);
    if (!r) s->addr = nxt;
    return r;
  }
  if (op == 0xE3) {                                 // explore mode: 0 = lock the player (C0/3C05: $011F)
    sc->explore = B[addr + 1] ? 1 : 0;
    w8(sc->mem, 0x7E011F, B[addr + 1]);             // 3C0B STA $1F: the byte the A handler (C0/1AC1), the
                                                    // treasure probe and the d-pad gate test
    s->addr = nxt;
    return 0;
  }
  if (op == 0xDA) {
    // Op 0xDA = C0/345A, "hand back to the player": a blocking, follower-releasing op
    // (disasm/notes/scene439_control_release.md). Its latch is $7E0154 bit0, its own re-entry flag:
    //   345F: LDA $99  / BPL       -- no follower in party slot 1 -> IP+1, wholly inert
    //   3466: LDA $54  / BIT #01   -- the re-entry latch
    //   346C: LDA $7F03FE / CMP #03 / BCC -> TYX/CLC/RTS   -- phase < 3: park, IP unchanged
    //   3477: TRB $54  ... 349A LDX $C7 / INX / SEC        -- phase >= 3: drop the latch, IP += 1
    //   349F: release slot 1 (C0/32C8 copy), 34E2 slot 2 if present
    //   34E6/3533: $7F03FE := 2 (slot 2 absent) or 1 (both) ; 34F0/353B TSB $54 ; CLC/RTS = park
    //
    // The released follower's IP is set to $16FF, the shared block C0/5CEC copies to $7F3700 (sc->rgBase,
    // ct_regroup.h). It walks the follower back to the lead (`95 00`), counts arrivals (`71 FF` + the `12 FF 03 04 04`
    // barrier), and the last arriver's `E3 01` restores control. The barrier seed is `3 - nFollowers`, as in
    // CtSceneBattleReturn.
    Mem* m = sc->mem;
    int fol[2] = { -1, -1 };
    int nf = ctPartyFollowers(sc, fol);            // 345F `LDA $99 / BPL`, 34E2 `LDA $9B / BPL`
    // 3461 BPL -> 3463 INX / CLC: no follower -- the IP steps past it and the visit ends.
    if (nf == 0) { s->addr = nxt; return 1; }
    if (r8(m, 0x7E0154) & 1) {                     // 3468 BIT #01: a later visit
      if (r8(m, LOC + 0xFF * 2) < 3) return 1;     // 3472 BCC: the walk is still running -> park the IP
      w8(m, 0x7E0154, (uint8_t)(r8(m, 0x7E0154) & ~1u));   // 3477 LDA #01 / 3479 TRB $54
      // 347D-3496 reset the party velocity ring ($7F:0C00/0D00/0F00 = velX / pre-adjust-X / shape rows, $AB/$AC/$AD
      // its head and two cursors; party_follower_ring.md):
      //   347D  LDA $AB / STA $AC / STA $AD        both cursors := the head  -> every gap is 0
      //   3483  TAX / LDA #00                      (TDC/XBA above cleared the high byte: X = $AB, 8-bit)
      //   3486  STA $7F0C00,X / 348A STA $7F0C00,X  raw bytes 9F 00 0C 7F twice: the velX row, not 0C80
      //   348E  STA $7F0D00,X / 3492 STA $7F0D00,X  raw bytes 9F 00 0D 7F twice: the preX row, not 0D80
      //   3496  STA $7F0F00,X                      the shape row
      { unsigned hd = r8(m, 0x7E01AB) & 0xFF;
        w8(m, 0x7E01AC, hd); w8(m, 0x7E01AD, hd);
        w8(m, 0x7F0C00 + hd, 0); w8(m, 0x7F0D00 + hd, 0); w8(m, 0x7F0F00 + hd, 0); }
      // ...and the head it copies includes this frame's push when the lead's B0 visit came first (the lead heads
      // the queue); the port pushes later, so the reset is re-applied behind the push (g_daRingPend).
      if (g_leadTk != -1) g_daRingPend = 1;
      s->addr = nxt;                                       // 349A LDX $C7 / 349C INX / 349D SEC
      return 0;
    }
    if (sc->rgBase < 0) { s->addr = nxt; return 0; }  // block never materialized -> do not park forever
    for (int i = 0; i < nf; i++) {                 // 349F / 34F6: the C0/32C8 release, per follower
      int o = fol[i]; CtSS* fs = &sc->st[o];
      if (fs->addr >= 0) fs->ret[fs->prio] = ssPushAddr(fs);   // 32E8: push the current IP ($1180) onto $7F0580,X
      fs->addr = sc->rgBase;                             // 3519 LDA #16FF / 351C STA $1180,X
      fs->prio = 0;                                      // 3521 LDA #00 / 3523 STA $1C00,X
      ctSetDc(sc, o, 0);                                 // 352C STZ $1001,X
      fs->wait = 0; fs->nm = 0;                          // (the call latch $7F0980 survives: 32C8 does not touch it)
      vmTicketStop(sc, o);                               // 3526 STZ $1A80,X / 3529 STZ $1A01,X
      sc->mx[o] = sc->my[o] = 0; sc->vecMove[o] = 0;
    }
    w8(m, LOC + 0xFF * 2, (uint8_t)(3 - nf));      // 34E8 #02 (slot 2 absent) / 3535 #01 (both released)
    w8(m, 0x7E0154, (uint8_t)(r8(m, 0x7E0154) | 1u));    // 34F0 / 353B LDA #01 / TSB $54
    return 1;                                      // 34F2 / 353D CLC / RTS: park on the op
  }
  if (op == 0xE5) {
    // C0/3DC5: the queued copy stages and yields: $013E/$0140/$0142 (16-bit), $0144, $0145 = $0144 & 7, `TSB $17`
    // bit 5, CLC. The copy is the main loop's, a frame later (the $0117 bit-5 service at the top of CtSceneTick:
    // C0/1770 -> `1790 JSR C0/AF4E`, then the style branch's frames with the event VM held).
    CtVmTileStageE5(sc->mem->s, B - sc->header, addr + sc->header);
    s->addr = nxt;
    return 1;
  }
  if (op == 0xE4) {                                 // copy tiles (curtains/doors/switches), immediate
    ctTileCopyQueue(sc, B[addr + 1], B[addr + 2], B[addr + 3], B[addr + 4], B[addr + 5], B[addr + 6], B[addr + 7]);
    // E4's copy is C0/3D97's `JSR C0/AF4E` on the map in WRAM, the $7E:3000 the ROM's readers see (the door
    // handlers, C0/28E1, the next copy's source); the queue above is the render's copy (SceneData.l1t) and the
    // collision planes.
    CtVmTileCopyE4(sc->mem->s, B - sc->header, addr + sc->header);
    // The copy is immediate and the visit ends: `3DC0 LDX $C7 / INX / CLC / RTS`, so the op after E4 runs on the
    // object's next visit. (The slot-1 runner C0/595C ignores the carry.)
    s->addr = nxt;
    return 1;
  }
  if (op == 0xAA || op == 0xAB || op == 0xAC || op == 0xAE || op == 0xB3 || op == 0xB4 || op == 0xB7) {
    // ---- The animation family: C0/2E67 (0xAA), 2E9A (0xAB), 2F71 (0xAC), 2E8F (0xAE), 2E87 (0xB3), 2E8B (0xB4),
    // 2F01 (0xB7); move.c's CtVmAnimFamily. A blocking 0xAB/0xB7 ends on one byte: C0/2E9C `LDA $7F0B01,X` / 2EA2
    // `DEC A / BEQ` (2 while the step ticker plays, 1 = complete on the next visit), driven 2 -> 1 by the frame legs
    // (CtAnimTimerTick's CA4F arm, CtFieldFrameLookup's CBA4 arm) and forced back to 2 off camera by A934.
    // The renderer's C fields are derived from the bytes (ctAnimSyncActor). The carry is the handler's: set for the
    // animeset family (continue), clear for 0xAC (2F94) and for the arm/park visits of 0xAB/0xB7.
    uint8_t* W = (uint8_t*)sc->mem; int X = obj * 2;
    uint8_t pre0 = W[0x1780 + X], pre1 = W[0x1781 + X], pre2 = W[0x1680 + X], pre3 = W[0x1681 + X],
            pre4 = W[0x1601 + X], pre5 = W[0x1301 + X], pre6 = W[0x10B01 + X];
    CtHRet ah; ah.ip = addr; ah.carry = 1; ah.ok = 0;
    CtVmAnimFamily(sc->mem->s, B, addr, X, &ah);
    int changed = pre0 != W[0x1780 + X] || pre1 != W[0x1781 + X] || pre2 != W[0x1680 + X] ||
                  pre3 != W[0x1681 + X] || pre4 != W[0x1601 + X] || pre5 != W[0x1301 + X] ||
                  pre6 != W[0x10B01 + X];
    ctAnimSyncActor(&sc->actors[obj], W, X);
    // The renderer restarts its playback on a generation change. Every animeset/shape issue is a restart in the ROM
    // too (STZ $1681 / STZ $1601); a 0xAB/0xB7 park visit writes nothing and must not restart the anim it waits on.
    if (changed || (op != 0xAB && op != 0xB7)) sc->actors[obj].animGen++;
    g_ctVmAnimOps++;
    s->addr = ah.ip;
    return ah.carry ? 0 : 1;
  }
  if (op == 0xD8) {                                 // battle (C0/3A83, 3-byte op `D8 imm1 imm2`).
    // The op blocks across the whole battle and completes on the $0118 ack. The request's consumer is in the
    // field tick (field.c, C0/1770-18D6); in a harness without one (ct.exe --vmdiff) the op yields forever, as the
    // ROM would if the main loop never serviced it.
    Mem* mb = sc->mem;
    if (mb->s[0x0117] & 0x01) return 1;             // already pending (C0/3A83-3A88): yield, IP parked
    if (mb->s[0x0118] & 0x01) {                     // battle done (ack set at C0/18BD, seen at C0/3A89):
      mb->s[0x0118] &= (uint8_t)~0x01;              // TRB $18 (C0/3AAF), IP += 3, CLC = end the visit
      s->addr = nxt; return 1;                      // (C0/3AB1-3AB6: INX INX INX / RTS)
    }
    mb->s[0x2989] = B[addr + 1];                    // imm1 -> $7E2989 (C0/3A91-3A95, battle flags A)
    mb->s[0x2A1F] = B[addr + 2];                    // imm2 -> $7E2A1F (C0/3A9A-3A9E, battle flags B)
    mb->s[0x0117] |= 0x21;                          // ORA #21 (C0/3AA2-3AA6): bit0 battle + bit5 intro
    mb->s[0x011F] = 0; sc->explore = 0;             // STZ $1F (C0/3AA8): kills $011F player control
    return 1;                                       // first visit ends CLC/TYX/RTS: yield, IP unchanged
  }
  if (op == 0xD9) {                                 // MOVEPARTY, C0/326C: 1 + 3 coordinate pairs, one per party slot
    // The ROM does not walk the party itself. The first visit ($7F03FC == 0, 32C8) releases each party slot onto
    // the walk program C0/5CEC copied to $7F3720 / 3740 / 3760 -- the C0/32C8 release op 0xDA also uses:
    //   32C8  STZ $1F                                        control off
    //   32CA  LDX $97 (16-bit) / $1C00,X * $80 + X /         the slot's IP pushed at its OWN priority level
    //         STA $7F0580,X ; $1180,X := 171F ; $1C00,X := 0  and repointed to its walk program, priority 0
    //   32FB  STZ $1A80,X / STZ $1A01,X / STZ $1001,X        no velocity, no ticket, eligible at once
    //   3306  the pair -> $7F3728/29 (& 7F), its sign bits -> $7F372B  = the program's `96 x y` / `A6 f`
    // Slot 0 unconditionally; slot 1 ($0199, 3344 BPL) and slot 2 ($019B, 33CB) only when present -- an absent one
    // ends the op with $7F03FC = 3 / 2, all three with 1. Each program is `E3 00 / 89 40 / 0D 00 / AE / 96 x y /
    // A6 f / 71 FE / 12 FE 04 04 04 ... 11 09 / E3 00 / 00`: walk (op 0x96, the MOVETO every script uses), face,
    // count the arrival in var FE = $7F03FC, wait for 4, RETURN to the pushed IP. The op parks (`LDX $C7 / CLC`);
    // a later visit (326E) parks while $7F03FC < 4, resets the party ring (327B-3296), parks while a present slot
    // still runs at priority 0 (329A-32B6), then $7F03FC := 0 and `INY x7 / TYX / CLC`: IP += 7, visit over
    // (disasm/notes/field_op8c_place_from_vars.md sec. 7).
    Mem* m = sc->mem;
    static const int slotDp[3] = { 0x0197, 0x0199, 0x019B };
    int ph = r8(m, 0x7F03FC);                                       // 326E LDA $7F03FC / BEQ ->32C8
    if (ph != 0) {
      if (ph < 4) return 1;                                        // 3274 CMP #04 / BCS / 3278 TYX / CLC: park
      { unsigned hd = r8(m, 0x7E01AB) & 0xFF;                      // 327D LDA $AB / STA $AC / STA $AD / TAX
        w8(m, 0x7E01AC, hd); w8(m, 0x7E01AD, hd);                  //   (the same ring reset as op 0xDA's 347D)
        w8(m, 0x7F0C00 + hd, 0); w8(m, 0x7F0D00 + hd, 0); w8(m, 0x7F0F00 + hd, 0); }   // 3286-3296
      for (int k = 0; k < 3; k++) {                                // 329A / 32A4 / 32AE
        int v = r8(m, 0x7E0000u + (uint32_t)slotDp[k]);
        if (v & 0x80) continue;                                    //   BMI: the slot is empty
        if (v / 2 < sc->nobj && sc->st[v / 2].prio == 0) return 1; //   LDA $1C00,X / BEQ ->3278: still walking
      }
      w8(m, 0x7F03FC, 0);                                          // 32B8 LDA #00 / STA $7F03FC
      s->addr = addr + 7; return 1;                                // 32BE INY x7 / TYX / CLC
    }
    w8(m, 0x7E011F, 0); sc->explore = 0;                            // 32C8 STZ $1F
    for (int k = 0; k < 3; k++) {
      int v = r8(m, 0x7E0000u + (uint32_t)slotDp[k]);
      if (k == 0) v |= r8(m, 0x7E0198) << 8;                        // 32CA LDX $97: 16-bit, no bit-7 test
      else if (v & 0x80) { w8(m, 0x7F03FC, (uint8_t)(4 - k)); return 1; }   // 3346 #03 / 33CD #02, park
      int o = v / 2;
      if (!(v & 1) && o < sc->nobj && sc->d9Base >= 0) {
        CtSS* ps = &sc->st[o];
        if (ps->addr >= 0) ps->ret[ps->prio] = ssPushAddr(ps);     // 32E3 LDA $1180,X / 32E8 / 336D / 33F4 STA $7F0580,X
        ps->addr = sc->d9Base + 0x20 * k;                          // 32EE / 3373 / 33FA #171F / #173F / #175F
        { uint32_t ipw = (uint32_t)(ps->addr + sc->header);        // 32F1 STA $1180,X, in WRAM now: a lead that
          w8(m, 0x7E1180 + (uint32_t)o * 2, ipw & 0xFF);           //   already had this frame's visit (it heads the
          w8(m, 0x7E1181 + (uint32_t)o * 2, (ipw >> 8) & 0xFF); }  //   queue) commits no visit-end mirror until next frame
        ps->prio = 0;                                              // 32F6 LDA #00 / STA $1C00,X
        ctSetDc(sc, o, 0);                                         // 3301 STZ $1001,X
        ps->wait = 0; ps->nm = 0;                                  // (the call latch $7F0980 survives: not 32C8's)
        vmTicketStop(sc, o);                                       // 32FB STZ $1A80,X / 32FE STZ $1A01,X
        sc->mx[o] = sc->my[o] = 0; sc->vecMove[o] = 0;
        sc->folRoamPrev[o] = 0;                                    // those STZs are the follower's walk-cycle release:
      }                                                            //   the conga's handover must not clear the new move again
      int a = B[addr + 1 + 2 * k], b = B[addr + 2 + 2 * k];        // 3309 / 3316: ip+1+2k, ip+2+2k
      uint8_t* prog = sc->raw + 0x1720 + 0x20 * k;                 // $7F3720 / 3740 / 3760
      prog[8] = (uint8_t)(a & 0x7F); prog[9] = (uint8_t)(b & 0x7F);   // 3311 / 331E STA $7F3728 / 3729
      prog[11] = (uint8_t)(((a & 0x80) ? 2 : 0) | ((b & 0x80) ? 1 : 0));   // 3322-333C STA $7F372B
    }
    w8(m, 0x7F03FC, 1);                                             // 3450 LDA #01 / STA $7F03FC
    return 1;                                                       // 3456 LDX $C7 / CLC: park on the op
  }
  // 0xE6 (C0/3DF4, the per-layer drift $1D24-$1D29 + $1D4D; the camera bit feeds the commit accumulate 9238) and 0xF4
  // (C0/3D84, the auto-scroll mode $01BC; 0 also zeroes $1D4E/$1D4F) run move.c's CtVmScrollFamily through vmSharedOp.
  if (op == 0xE7) {                                 // scroll-to (C0/3E30): operands<<1 -> $1D02/$1D03
    // (the scroll target, 8 px units); the camera then seeks $1D0A/$1D0E toward it while the op blocks on $1D00
    // (the pre-battle pan, battle_entry_camera.md A.2). Handler C0/3E30 (DP = $1D00):
    //   $1D00==0 -> arm (3E41): $1D02=op1<<1, $1D03=op2<<1, INC $1D00 (=1), STZ $1F ($011F control off),
    //               then 3E59 CLC RTS = block this visit (the seek has not started yet).
    //   $1D00==1 -> re-entry (3E35 DEC A / BEQ 3E59): CLC RTS = still blocking (IP parked here).
    //   $1D00==2 -> seek done (3E38): STZ $1D00, SEC RTS = advance (IP += 3, run the next op).
    // The stepper that drives $1D0A/$1D0E and flips $1D00 1->2 is CtSceneCameraSeekTick (C0/91AC), run once per
    // frame at the top of CtSceneTick, so every host that ticks the VM (headless ones included) advances it.
    Mem* me = sc->mem;
    uint8_t seekSt = me->s[0x1D00];
    if (seekSt == 0) {                              // arm (C0/3E41): latch target, kill control, begin seek, block
      me->s[0x1D02] = (uint8_t)(B[addr + 1] << 1); me->s[0x1D03] = (uint8_t)(B[addr + 2] << 1);
      me->s[0x011F] = 0; sc->explore = 0;           // STZ $1F (C0/3E57): player control off for the whole pan
      me->s[0x1D00] = 1; me->s[0x1D01] = 0;         // $1D00 = 1 (seeking), arrival bits clear. $1D0A/$1D0E left
      return 1;                                     //   at the live camera; CtSceneCameraTick (91AC) seeks the
    }                                               //   coarse+phase toward $1D02/$1D03 at 1px/frame.
    if (seekSt == 1) return 1;                      // still seeking: block (IP parked on this op)
    me->s[0x1D00] = 0;                              // seekSt == 2: arrived -> STZ $1D00 + advance (SEC RTS):
    s->addr = nxt; return 0;                        //   the visit continues
  }
  if (op == 0xF2 && (r8(sc->mem, 0x7E0118) & 0x02)) {
    // C0/3CFD, wait for the armed fade: `TYX / LDA $18 / BIT #02 / BNE ->3D05 / INX / CLC / RTS`. While op 0xF0's
    // bit is set the INX is skipped, so the IP stays on this op and the visit ends; once C0/1F24 clears the bit the
    // op advances.
    return 1;                                 // park: s->addr stays on the op, visit ends
  }
  if (op == 0xF3 && (r8(sc->mem, 0x7E0118) & 0x08)) {
    // C0/3D7A, wait for the colour-math fade: `TYX / LDA $18 / BIT #08 / BNE ->3D82 / INX / CLC / RTS`, op 0xF2's twin on
    // bit 3 (op 0xF1's request, cleared by C0/1F5A when $0121 reaches $0122). Parked, the op runs again next visit.
    return 1;
  }
  if (op == 0xE0) {
    // ChangeLocation with fade (C0/3AB7, D = $0100). `LDA $17 / BMI park`: $0117 bit7 = a fade-change is already
    // pending. Fire path: the loc word -> $0112/$0113 (&0x1FF), tile x/y -> $0114/$0115, facing ((hi>>1)&0x0F |
    // hi&0x80 via TSB) -> $0116, $0117 |= 0x80, the fade counter $0119 = 0x0F, then TYX/CLC/RTS: the visit ends with
    // the IP on this op. The skip path (C0/3AEE) parks the same way, so the object revisits the op every frame until
    // the load tears the scene down; that park is the ROM's replay guard. The commit is the field frame's (C0/0C8A,
    // top of CtSceneTick): it drains $0119 one per frame while $0117 bit7 is set and reloads at 0; the load's init
    // clears the latch (C0/0B96 STZ $17, memLoadInit0B86).
    if (!(r8(sc->mem, 0x7E0117) & 0x80)) {
      int word = B[addr + 1] | (B[addr + 2] << 8);
      w8(sc->mem, 0x7E0112, word & 0xFF);
      w8(sc->mem, 0x7E0113, (word >> 8) & 0x01);
      w8(sc->mem, 0x7E0114, B[addr + 3]);
      w8(sc->mem, 0x7E0115, B[addr + 4]);
      w8(sc->mem, 0x7E0116, ((B[addr + 2] >> 1) & 0x0F) | (B[addr + 2] & 0x80));
      w8(sc->mem, 0x7E0117, r8(sc->mem, 0x7E0117) | 0x80);
      w8(sc->mem, 0x7E0119, 0x0F);
      sc->lcLoc = word & 0x01FF;              // mirror for the field consumer (read at commit time)
      sc->lcFacing = (B[addr + 2] >> 1) & 3;
      sc->lcX = B[addr + 3];
      sc->lcY = B[addr + 4];
      sc->lcStyle = 0;
    }
    return 1;                                 // park: s->addr stays on the op, visit ends
  }
  if (op == 0xE2) {
    // C0/3B9E: op 0xE0 with the destination in four locals (move.c CtVmChangeLocVars). It parks like 0xE0 and the
    // same drain (C0/0C8A, top of CtSceneTick) commits $0112/$0114/$0116. Its IP delta is zero on both paths (3BA2
    // `STY $C7` saves the entry IP before the INYs, 3BFE `LDX $C7` restores it).
    CtHRet h; CtVmChangeLocVars(sc->mem->s, B - sc->header, addr + sc->header, &h);
    s->addr = h.ip - sc->header;
    return 1;                                 // CLC on both paths: park, visit ends
  }
  if (op == 0xDF || op == 0xE1) {
    // The non-returning half of the family. 0xE1 = C0/3B4B, 0xDF = C0/3B98 (`LDA #01 / STA $1E / BRA ->3B4B`):
    //   3B4D REP #20 / LDA $7F2001,X / STA $D9 / AND #01FF / STA $12     the word, and the location into $0112
    //   3B5C LDA $7F2001,X / STA $14                                      the tile pair into $0114
    //   3B62 SEP #20 / LDA $DA / LSR / AND #0F / STA $16 / LDA $DA / AND #80 / TSB $16
    //   3B71 LDA $4210 / BPL ->3B71                                       spin until the vblank's NMI flag
    //   3B76 JSR C0/0B4E                                                  SEI, INIDISP $80, NMITIMEN/HDMAEN 0
    //   3B79 LDX $00 / STX $05 / LDX $02 / STX $07 / LDA $04 / STA $09    the location being left -> $0105..$0109
    //   3B85 LDX $12 / STX $00 / LDX $14 / STX $02 / LDA $16 / STA $04    the staged one -> $0100..$0104
    //   3B91 LDX #06FF / TXS / BRL ->0000                                 the stack thrown away: the reboot
    // It never returns to the runner, so nothing after it runs (not the rest of this visit, not the next object, not
    // the next frame's main loop). Here the stores are made, the visit parks on the op, and the host performs the
    // rest at the vblank (field.c, 'THE DF/E1 REBOOT').
    if (sc->rebootReq) return 1;                    // (a dead main loop never gets back here: no second staging)
    if (op == 0xDF) w8(sc->mem, 0x7E011E, 1);       // 3B98 LDA #01 / 3B9A STA $1E
    int word = B[addr + 1] | (B[addr + 2] << 8);
    w8(sc->mem, 0x7E01D9, word & 0xFF);             // 3B53 STA $D9 (16-bit)
    w8(sc->mem, 0x7E01DA, (word >> 8) & 0xFF);
    w8(sc->mem, 0x7E0112, word & 0xFF);             // 3B55 AND #01FF / 3B58 STA $12 (16-bit)
    w8(sc->mem, 0x7E0113, (word >> 8) & 0x01);
    w8(sc->mem, 0x7E0114, B[addr + 3]);             // 3B60 STA $14 (16-bit)
    w8(sc->mem, 0x7E0115, B[addr + 4]);
    w8(sc->mem, 0x7E0116, ((B[addr + 2] >> 1) & 0x0F) | (B[addr + 2] & 0x80));   // 3B64-3B6F
    sc->rebootReq = 1;
    return 1;                                       // park: s->addr stays on the op
  }
  if (op == 0xDD || op == 0xDE) {
    // Unconditional ChangeLocation, the returning members: 0xDD C0/3B1B, 0xDE C0/3B45 (`LDA #01 / STA $1E / BRA
    // ->3B1B`). Operands: the location word (bits 0-8 = loc id; the high byte carries the facing and the arrival
    // flags), then tile X, tile Y. The handler is these stores and `SEC / RTS`; it moves nobody. No latch ($0117 is
    // op 0xE0's), no fade, no load: C0/0066 reads $0100 only when something reboots the field, which in the shipped
    // scripts always follows (a gate warp `FF 80-83`, a $C3 sequence, the loader, or a later `DF`).
    int word = B[addr + 1] | (B[addr + 2] << 8);
    w8(sc->mem, 0x7E01D9, word & 0xFF);                          // C0/3B23 STA $D9 (16-bit)
    w8(sc->mem, 0x7E01DA, (word >> 8) & 0xFF);
    // C0/3B34-3B3F: `LDA $DA / LSR / AND #0F / STA $04` then `LDA $DA / AND #80 / TSB $04`: the immediate arm
    // writes the arrival flags byte $0104 itself (the deferred arm stages $0116 for C0/0CBC, CtWramCommitLocation).
    w8(sc->mem, 0x7E0104, ((B[addr + 2] >> 1) & 0x0F) | (B[addr + 2] & 0x80));
    // ...and the other two stores (DP = $0100): C0/3B28 `STA $00`, the masked location word into $0100/$0101, and
    // C0/3B30 `STA $02`, the tile pair into $0102/$0103. The reboot (C0/0066 `LDX $0100`) reads them.
    w8(sc->mem, 0x7E0100, word & 0xFF);                          // C0/3B25 AND #01FF / 3B28 STA $00 (16-bit)
    w8(sc->mem, 0x7E0101, (word >> 8) & 0x01);
    w8(sc->mem, 0x7E0102, (uint8_t)B[addr + 3]);                 // C0/3B30 STA $02 (16-bit)
    w8(sc->mem, 0x7E0103, (uint8_t)B[addr + 4]);
    // The flag is a WRAM byte: C0/3B45 (0xDE) is `LDA #01 / STA $1E` before its BRA into the 0xDD body. The load's
    // constant init does not touch $011E; C0/286C ends `LDA $1E / RTS` and C0/2848's `BNE ->2866` skips its whole
    // brightness ramp on it (field.c).
    if (op == 0xDE) w8(sc->mem, 0x7E011E, 1);
    // CT_LOCTRACE: $0100 changed here (the oracle's `--monitor 7E0100:2` shows it on this op's frame), so the trace
    // prints here as CtScenePollLocChange prints a commit's; style = the $1E the DE set.
    { static int t = -1; if (t < 0) t = getenv("CT_LOCTRACE") ? 1 : 0;
      if (t) printf("loctrace: -> loc %03X tile(%d,%d) facing=%d style=%d\n", word & 0x1FF, B[addr + 3], B[addr + 4],
                    (B[addr + 2] >> 1) & 3, op == 0xDE ? 1 : 0); }
    s->addr = nxt;
    return 0;
  }
  applyEffect(sc, obj, addr, op);
  s->addr = nxt;
  // The CLC-only ops: they advance the IP and end the visit. Each handler below was traced from its C0/5D6E entry
  // to its RTS and ends CLC with no SEC verdict (an arithmetic SEC, as in 0xFE's 3F54 `SEC / SBC $F0`, is not one).
  // Confirm a new op the same way before adding it. Ops with their own earlier returns (the parks, the chase,
  // 0xD8/0xE0/0xE2, 0xD9, 0xEB) keep their own semantics. The list:
  //   0x7C C0/4CEC  0x7D C0/4CF9  0x7E C0/4CE6  0x90 C0/4CD5  0x91 C0/4CE0   (the shared 4CD9 tail)
  //   0x2A C0/3ED6  0x2C C0/3EE6  0xA6 C0/5545  0xAC C0/2F71
  //   0xE4 C0/3D97  0xE5 C0/3DC5  0xE8 C0/3C11  0xEA C0/3C2F  0xEC C0/3C98
  //   0xED C0/3CBC  0xEE C0/3CC5  0xF0 C0/3CD0  0xF2 C0/3CFD  0xF3 C0/3D7A
  //   and below: 0x2B / 0x32 / 0xFE, and the ten ops of the C0/554B tail.
  if (op == 0x90 || op == 0x91 || op == 0x7C || op == 0x7D || op == 0x7E ||   // C0/4CD9 tail
      op == 0x2A || op == 0x2C || op == 0xAC ||                               // C0/3ED6 3EE6 2F71
      op == 0xE4 || op == 0xE5 ||                                            // C0/3D97 3DC5 (tile copy)
      op == 0xE8 || op == 0xEA || op == 0xEC || op == 0xED || op == 0xEE ||   // C0/3C11 3C2F 3C98 3CBC 3CC5
      op == 0xF0 || op == 0xF2 || op == 0xF3 ||                               // C0/3CD0 3CFD 3D7A
      // 0x2B (C0/3EDE) and 0x32 (C0/3EE2) are `LDA #n / BRA ->3ED8` into 0x2A's tail:
      //     3ED6: LDA #04 / 3ED8: TSB $54 / TYX / INX / CLC / RTS
      // differing only in the bit written to $54 (04 / 08 / 10). 0xFE (C0/3F26) has one reachable RTS,
      // `3FAB: LDX $C7 / 3FAD: CLC / 3FAE: RTS`, IP +18.
      op == 0x2B || op == 0x32 || op == 0xFE ||                               // C0/3EDE 3EE2 3F26
      // The C0/554B family, all ten, one ROM mechanism; every one exits through the same tail:
      //     5545: INY / TYX / LDA $7F2001,X          <- 0xA6 enters here
      //     554B: LDX $6D                            <- 0x0F/0x17/0x1B/0x1D/0xA7 BRA here (self)
      //     554D: STA $1600,X / STZ $1601,X          <- 0x1E/0x1F/0x25/0x26 BRA here (cross-object:
      //     5553: TYX / INX / CLC / RTS                 X is still the operand, so the write targets
      //                                                 another object)
      // IP deltas +1 (0x0F/0x17/0x1B/0x1D) and +2 (the rest), as in OPLEN. The effect is move.c's CtVmFacingFamily
      // (applyEffect and the init pass call it): the facing and the tail's `STZ $1601,X`, the walk-anim timer.
      op == 0x0F || op == 0x17 || op == 0x1B || op == 0x1D ||
      op == 0x1E || op == 0x1F || op == 0x25 || op == 0x26 ||
      op == 0xA6 || op == 0xA7) return 1;                                     // C0/5535..5593 -> 554B
  // 0xA8 / 0xA9 (C0/55A0 / C0/5605, face toward an object / a party slot's object) end the same way once they have
  // set the facing: `55FB TYX / INX / INX / CLC / RTS`. Only the decline (the target's $1100 bit 7, `55AC BMI ->5600`)
  // takes `5600 TYX / INX / INX / SEC / RTS` and lets the visit run on.
  if (op == 0xA8 || op == 0xA9) {
    int off = (op == 0xA8) ? B[addr + 1] : vmSlotTarget(sc->mem, op, B[addr + 1]);
    return (r8(sc->mem, 0x7E1100 + (uint32_t)off) & 0x80) ? 0 : 1;
  }
  // The runner's carry is not "did the handler advance the IP" (C0/5ABC-5ABF writes X back into $1180,X on both
  // paths) but "may the visit keep spending its op budget": C0/5AB4 `BCC ->5ABA` ends the visit on CLC, `DEC $67 /
  // BNE ->5AA3` loops on SEC. So a handler can advance and yield, as the list above does.
  // CT_VM_UNHANDLED=1: an op that gets here without moving the IP has no case in this runner: the visit re-runs it
  // until the five-op budget is spent, every visit, and the object's script stops there.
  if (s->addr == addr && getenv("CT_VM_UNHANDLED"))
    fprintf(stderr, "vm-unhandled: op %02X obj%d ip %04X\n", op, obj, addr);
  return 0;
}

// Linear-scan a function from `addr` for the first set-table op (0xB8), returning its
// table file-addr or -1 (stops at a Return). Aligned via op_len from a real entry point.
static int scanForTable(const uint8_t* B, int blen, int addr) {
  for (int k = 0; addr >= 0 && addr < blen && k < 256; k++) {
    uint8_t op = B[addr]; int ln = opLen(B, addr);
    if (addr + ln > blen) return -1;
    if (op == 0xB8) return (B[addr + 1] | (B[addr + 2] << 8) | (B[addr + 3] << 16)) - 0xC00000;
    if (op == 0x00) return -1;                          // Return ends this function
    addr += ln;
  }
  return -1;
}
// Pre-seed the dialogue string table: scan obj 0's function slots for an op 0xB8 (the slot varies by
// scene), then any object's. Read-only; live 0xB8 ops update sc->tbTable during play.
static void captureTable(CtScene* sc) {
  for (int sl = 0; sl < 16; sl++) {
    int t = scanForTable(sc->body, sc->blen, fpPtr(sc, 0, sl));
    if (t >= 0) { sc->tbTable = t; return; }
  }
  for (int o = 1; o < sc->nobj; o++)
    for (int sl = 0; sl < 16; sl++) {
      int t = scanForTable(sc->body, sc->blen, fpPtr(sc, o, sl));
      if (t >= 0) { sc->tbTable = t; return; }
    }
}

// The party $7E:2980-82 persists across a load: the ROM's load reads it and never writes it
// (`0C34 LDA $7E2980 / 0C38 STA $94`, wram_lifetime.md sec. 4). The new-game default is memBoot's.
void CtSceneGetParty(const CtScene* sc, int* a, int* b, int* c) {
  if (a) *a = sc ? r8((Mem*)sc->mem, 0x7E2980) : -1;
  if (b) *b = sc ? r8((Mem*)sc->mem, 0x7E2981) : -1;
  if (c) *c = sc ? r8((Mem*)sc->mem, 0x7E2982) : -1;
}

// Location song carry ($7E29AE and the $1E00-$1E03 staging; C0/1B53, disasm/notes/audio_commands.md sec. 7).
// The loader runs C0/092B -> 1B53 (song) -> 0960 (map, script), so the song is set before the scene's init,
// and an init op 0xEA overrides it. field.c computes the song from the $F6 record before CtSceneCreate,
// which writes it ahead of the init pass. -1 = the location declines (record bit 7, C0/1B7D BMI) or no ROM.
static int g_carrySong = -1;
void CtSceneSetCarrySong(int song) { g_carrySong = song; }
static int g_loadSongPrice = -1;   // the last scene load's C0/1B8B JSL, as the sink priced it (frames << 10 | end line)
int CtSceneLoadSongPrice(void) { return g_loadSongPrice; }

// New-game ATB-mode selector, A confirm (C2/E24C, disasm/notes/battle_atbmode.md sec. 1.4): row 0 (Active)
// sets $2990 bit 6 (TSB), row 1 (Wait) clears it (TRB). The other config bits are left alone.
void CtSceneSetAtbMode(CtScene* sc, int active) {
  if (!sc) return;
  int v = r8(sc->mem, 0x7E2990);
  v = active ? (v | 0x40) : (v & ~0x40);
  w8(sc->mem, 0x7E2990, v);
}

// `w` NULL: the scene owns a private, freshly booted image and frees it with itself (harness modes).
// `w` non-NULL: build on the host's process-lifetime image, which is neither cleared nor freed here. The
// caller runs CtWramLocationLoad() first, so a scene rebuild (state reload) is not a location change.
CtScene* CtSceneCreateIn(CtWram* w, const uint8_t* raw0, int rawLen, const uint8_t* gmem, int sceneNo) {
  if (rawLen < 1) return NULL;
  CtScene* sc = (CtScene*)calloc(1, sizeof *sc);
  if (!sc) return NULL;
  sc->mem = (Mem*)w;
  if (!sc->mem) {
    sc->mem = (Mem*)malloc(sizeof *sc->mem);
    if (!sc->mem) { free(sc); return NULL; }
    sc->ownMem = 1;
    memInit(sc->mem, NULL);                          // gmem is copied in below
  }
  sc->tbTable = -1; sc->tbTableAtShow = -1;
  sc->tbPendChoice = -1;            // no decision pending (0 is a valid option LINE)
  sc->rgBase = -1; sc->d9Base = -1;
  // The location load's object-table fill: every $0F80,X (the op 0x8E sprite-priority byte) starts at
  // 0x80 (auto) and $0F81,X (the OBJ palette attribute) at 0, before any script runs. Only op 0x8E
  // (C0:4C7F) and the allocator (C0:724B) write them later. CtSceneActorPrio reads this byte, so an
  // object that never runs `8E` is auto, while `8E 00` is a fixed priority 0.
  for (int k = 0; k < 64; k++) { w8(sc->mem, 0x7E0F80 + (uint32_t)k * 2, 0x80); w8(sc->mem, 0x7E0F81 + (uint32_t)k * 2, 0); }
  // The engine's own programs in the script image at their ROM addresses. After the script is decompressed
  // to $7F:2000 (C0/56D4), C0/5CEC copies templates in with MVN (disasm/notes/battle_forest_return.md AD.3a):
  //   5CEE  C0:5D37 -> $7F3700, $20 bytes   the battle-return regroup (IP 16FF: op 0xDA / C0/5B1F)
  //   5CFC  C0:5D50 -> $7F3720, $20 bytes   op 0xD9's walk program, party slot 0 (IP 171F)
  //   5D0A  C0:5D50 -> $7F3740, $20 bytes   ... slot 1 (IP 173F)
  //   5D18  C0:5D50 -> $7F3760, $20 bytes   ... slot 2 (IP 175F)
  //   5D26  C0:5D68 -> $7F3780, $06 bytes
  // $7F2000 + k is raw[k] (an IP is an offset from $7F2001), so they land at raw[0x1700..0x1785]. The largest
  // shipped script is 0x16FA bytes. rawLen stays the script's own length. The gap reads 0 (op 0x00, Return),
  // which also covers opLen's over-read of up to b[o+5] past a truncated script.
  size_t sysHi = 0x1786, cap = ((size_t)rawLen + 8 > sysHi ? (size_t)rawLen + 8 : sysHi) + 8;
  sc->raw = (uint8_t*)calloc(cap, 1);
  if (!sc->raw) { free(sc); return NULL; }
  memcpy(sc->raw, raw0, rawLen); sc->rawLen = rawLen;
  memcpy(sc->raw + 0x1700, ctRegroupReturn, sizeof ctRegroupReturn);     // C0/5CF8 MVN
  for (int k = 0; k < 3; k++)
    memcpy(sc->raw + 0x1720 + 0x20 * k, ctRegroupD9, sizeof ctRegroupD9); // C0/5D06 / 5D14 / 5D22
  memcpy(sc->raw + 0x1780, ctRegroupTail, sizeof ctRegroupTail);         // C0/5D2F
  // The image is also written to WRAM, where C0/56D4 and the MVNs put it. The runner reads its private copy,
  // but WRAM readers such as CtSceneSeedFromWram take each object's current op from $7F:2001 + IP.
  { uint8_t* img = (uint8_t*)sc->mem + 0x12000;                           // $7F:2000 in the flat 128 KB image
    memcpy(img, sc->raw, (size_t)rawLen);                                 // C0/56D4
    memcpy(img + 0x1700, sc->raw + 0x1700, 0x86); }                       // C0/5CEC's four MVNs ($7F3700-$7F3785)
  const uint8_t* raw = sc->raw;
  int count = raw[0]; if (count > 64) count = 64;
  sc->nobj = count; sc->header = raw[0] * 32;
  if (1 + sc->header > rawLen) { sc->body = raw; sc->blen = 0; return sc; }
  sc->body = raw + 1 + sc->header; sc->blen = rawLen - 1 - sc->header;
  sc->rgBase = 0x16FF - sc->header;                   // IP 16FF / 171F as body offsets (body = raw+1+header)
  sc->d9Base = 0x171F - sc->header;
  sc->blen = (int)(cap - 8) - 1 - sc->header;         // addressable through $7F3785 (the programs are CALL targets)
  sc->playerLoaded = 0;
  sc->playerObj = 1;                                   // cache the lead-PC object: first obj whose init
  // (fn0) runs op 0x57 (load PC slot 0) or `80 00` (char-id 0). The roster is not committed yet here, so
  // op 0x80 matches char-id 0, the default lead; the live op 0x80 handler does the full $7E2980 match.
  for (int o = 0; o < sc->nobj; o++) {
    int a = fpPtr(sc, o, 0); int found = 0;
    for (int k = 0; a >= 0 && a < sc->blen && k < 32; k++) {
      uint8_t op = sc->body[a];
      if (op == 0x57) { sc->playerObj = o; found = 1; break; }
      if (op == 0x80 && sc->body[a + 1] == 0x00) { sc->playerObj = o; found = 1; break; }   // `80 00` = lead char-id 0
      if (op == 0x00) break;
      a += opLen(sc->body, a);
    }
    if (found) { sc->playerLoaded = 1; break; }
  }
  // Story flags: copied in only when the caller supplies them. The ROM never rewrites $7F:0000-01FF on a
  // load (wram_load_census.md sec. 5), so a NULL gmem on a host image leaves the live flags alone.
  if (gmem) memcpy(sc->mem->g, gmem, 0x200);
  // C0/56B0's other scene-load fills: the 30 OBJ slots ($0B88, C0/6F79), the arena owners ($0BC0,
  // C0/E935), the whole $0E00 page (C0/B204; C0/A9CD links an object only if $0E01,X is negative) and a
  // (00, FF) pair per object from $0F00 (C0/A7E9). Written directly, not through w8: they are load
  // constants, not frame-loop stores, and stay out of the write mask.
  CtObjSceneInit(sc->mem->s, sc->nobj);
  // C0/571A-58AA, the per-object default fill (between the $7F:0200 and $7E:1900 DMA clears): `$1000,X`
  // back to 4, which clears the op 0x0B pause bit the activate guard C0/5AD1 tests, and `$1100,X` to 0x80
  // for all 63 entries before the scene's objects get 0x07. So a pause never carries into the next
  // location. `raw + 1` is the script's fn-pointer table; the fill copies each slot-0 word into $1180.
  CtObjSceneDefaults(sc->mem->s, raw + 1, sc->nobj);
  // Active party char-ids ($7E2980-82, read by op 0xD2's membership test C2/8C36) and the reserve roster
  // $2983-$2988, on a private image only. A host image holds the live party, and re-seeding it would drop
  // followers at every door. The private path uses w8() so the write mask claims these for --vmdiff.
  if (sc->ownMem) {
    w8(sc->mem, 0x7E2980, 0x00); w8(sc->mem, 0x7E2981, 0x80); w8(sc->mem, 0x7E2982, 0x80);
    for (int rsv = 3; rsv < 9; rsv++) w8(sc->mem, 0x7E2980 + rsv, 0x80);
  }
  // The location song, staged before the init pass (see g_carrySong; audio_commands.md sec. 7). C0/1B5F-1B73
  // stages the $81 (00,FF,FF) stop in $1E00-$1E03; C0/1B7F-1B86 commits the shadow and the $10 play command.
  // This is plain WRAM and runs with or without an APU, so a CT_NOAUDIO run has the same WRAM.
  // A held song: C0/1B53 `LDA $7F01ED / BEQ`; non-zero takes C0/1B59 (`LDA $7E29AE / BRA ->1B83`): no stop,
  // no $F6 record, and the current song is re-posted. C2/2F0F and C2/18F1 test the same byte. The attract
  // sets it with its first op (`56 01 ED 01`) and never clears it, so all its scenes keep song $18.
  g_loadSongPrice = -1;
  if (r8(sc->mem, 0x7F01ED)) {
    w8(sc->mem, 0x7E1E01, r8(sc->mem, 0x7E29AE)); w8(sc->mem, 0x7E1E00, 0x10);   // C0/1B59 -> C0/1B83-1B88
    g_loadSongPrice = vmSoundPostMem(sc->mem, sc->blen, 12);                     // C0/1B8B JSL C7:0004
  } else {
    w8(sc->mem, 0x7E1E01, 0x00); w8(sc->mem, 0x7E1E02, 0xFF);
    w8(sc->mem, 0x7E1E03, 0xFF); w8(sc->mem, 0x7E1E00, 0x81);
    // C0/1B73 posts the $81 (`JSL C7/0004`) before the play command is staged over it; the $10 gets its
    // own JSL at C0/1B8B. The $81 resets master volume to FF, so a fade left by the title's exit does not
    // carry into the next song. Inert with no sink (CT_NOAUDIO). The post's C7 time (~15k cycles) is part
    // of the load, and the $10 below starts on the line it returns on.
    int songLn = 12, songFr = 0, songAny = vmSoundPostAcc(sc->mem, sc->blen, &songLn, &songFr);
    if (g_carrySong >= 0) {
      w8(sc->mem, 0x7E29AE, g_carrySong & 0xFF);
      w8(sc->mem, 0x7E1E01, g_carrySong & 0xFF); w8(sc->mem, 0x7E1E00, 0x10);
      // Posted inside the load too (C0/1B8B `JSL C7/0004`), so a song upload adds to the load's black frames
      // (field_load_cost.md sec. 7). The host adds its price to the load (CtSceneLoadSongPrice).
      songAny |= vmSoundPostAcc(sc->mem, sc->blen, &songLn, &songFr);            // C0/1B8B
    }
    if (songAny) g_loadSongPrice = (songFr << 10) | songLn;
  }
  sc->bright = 15; sc->brightTarget = 15;
  sc->explore = 1; sc->moveLock = 0;                 // default free; the scene's startup
  for (int n = 0; n < count; n++) {                  // functions may lock it (op 0xE3 00)
    CtVmActor* a = &sc->actors[n];
    a->sprite = -1; a->x = 0; a->y = 0; a->px16 = -1; a->py16 = -1; a->facing = 1; a->draw = 0; a->dead = 0; a->defined = 0; a->hasPos = 0; a->result = 0;
    a->animSeedFrame = -1;
    a->solid = 0; a->colorFx = 0; a->colorGated = 0;   // $1B01 default 0; the load-sprite ops (0x80-0x83,
                                                       // C0/45A0 etc.) set solid=1, op 0x84 overrides.
                                                       // Sprite-less objects stay non-solid.
    a->moveSpeed = 0x10;           // $1A00 default; an init op 0x89 overrides (captured by runFunc)
    a->coll = 1; a->collMap = 1;   // $1C80 leftover default = 0x03 (loaders 0x81/0x82 write $1100/$1B01 but
                                   // not $1C80, C0/4476ff). Init op 0x0D overrides; a seeded scene takes
                                   // its $1C80 from the WRAM seed.
    a->cadPause = 0;               // $1000 bit7 clear until op 0x0B / seed
    a->snap = 0;                   // $1C81 default 0 (no snap); init 0x0E overrides (obj10/Taban)
    a->moveMode = 0x80;            // $1100 default = idle (C0/A819 skip); a LOAD op (0x57/80-83) sets the real mode
    a->moveAnim = 4;               // $1000 spawn cadence default = 4 (every non-PC event object);
  }                                //   op 0x87 or a PC loader (-> 1) overrides it in fn0
  // The $1C80/$1C81 cold leftover, written before the init pass so fn0's own `0D`/`0E` (C0/629F /
  // C0/62AE) write over it, as in the ROM. Both are read from WRAM: C0/5BA0 gates the object-solid test
  // on $1C80 bit 1, C0/305D reads $1C81. The cold value is 0x03 (see the a->coll default above).
  // Note: without this seed $1C80 bit 1 never sets and C0/5B95 treats every mover as unblocked.
  for (int o = 0; o < count; o++) {
    w8(sc->mem, 0x7E1C80 + (uint32_t)o * 2, (uint8_t)((sc->actors[o].coll << 1) | sc->actors[o].collMap));
    w8(sc->mem, 0x7E1C81 + (uint32_t)o * 2, (uint8_t)sc->actors[o].snap);
  }
  gLoadPostOn = gLoadPostMode;                       // C0/5709 and C0/56C8 run at the load's end (gLoadPost, above)
  w8(sc->mem, 0x7E0173, 0);                          // C0/58DC STZ $73: op 0x82's party-slot counter (C0/4665 / 46D9)
  gOpCostOn = gOpCostMode; gOpCostWhy = NULL;       // a load the host prices: its ops are priced as they run
  gOpCostSum = 0; gOpCostN = 0;
  for (int n = 0; n < count; n++) {
    // C0/58ED / 58F0, the init runner's head for every object (inactive ones included; 58F3's BMI comes
    // after): `STZ $1B01,X / STZ $1B80,X` under the REP #20 of 58E2, so two 16-bit clears -- $1B01/$1B02
    // (the solid byte and the next object's $1B00) and $1B80/$1B81 (op 0x7A's jump flag and gravity).
    // A jump left in one location (C0/4E1E) therefore never carries into the next.
    w16(sc->mem, 0x7E1B01 + (uint32_t)n * 2, 0); w16(sc->mem, 0x7E1B80 + (uint32_t)n * 2, 0);
    int slot0 = (raw[1 + n * 32] | (raw[1 + n * 32 + 1] << 8)) - sc->header;
    int stopAddr = -1, stopOp = -1;
    if (slot0 >= 0 && slot0 < sc->blen) {
      int br2[2] = { sc->bright, sc->brightTarget };
      runFunc(sc->body, sc->blen, sc->header, n, count, slot0, sc->mem, sc->actors, &sc->explore, br2, &stopAddr, &stopOp, sc);
      sc->bright = br2[0]; sc->brightTarget = br2[1];
    }
    CtSS* s = &sc->st[n];
    memset(s->ret, 0, sizeof s->ret); s->prio = 7; s->tb = 0; s->nm = 0;
    // The countdown $1001,X is what the load left: the fill's 00 (C0/5786, CtObjSceneDefaults), re-armed only
    // by an fn0 op 0x87 (C0/488B, runFunc). So the first queue build after a load (C0/59F0 `LDA $01,X / BEQ
    // ->5A01`) enqueues every object, and an object of period 4 makes its first visit at once.
    s->dc = r8(sc->mem, 0x7E1001u + (uint32_t)n * 2);
    s->wait = 0; s->waitObj = -1; s->waitPrio = 0;
    if (stopOp == 0x00) s->addr = stopAddr + 1;
    else if (stopOp == 0xAF || stopOp == 0xB0 || stopOp == 0xB1 || stopOp == 0xB2) s->addr = stopAddr;
    else s->addr = -1;
    // C0/5915-591B, the init runner's IP writeback: `LDA $7F2001,X / BEQ ->5915` leaves the loop on fn0's
    // terminator, then `INX / TXA / LDX $6D / STA $1180,X` commits the address after it (the fill at
    // C0/571A had seeded fn0's start there).
    if (s->addr >= 0) { uint32_t ipw0 = (uint32_t)(s->addr + sc->header);   // = CTW_IPOFF, defined further down
      w8(sc->mem, 0x7E1180 + (uint32_t)n * 2, ipw0 & 0xFF);
      w8(sc->mem, 0x7E1181 + (uint32_t)n * 2, (ipw0 >> 8) & 0xFF); }
    // The live position is the $1800/$1880 word / 16 whenever the init pass captured one. `y - 1/16` is
    // right only for op 0x8B's synthesised 0xFF low byte; op 0x8D stores the script's raw words, and
    // C0/A947's window test reads the tile they give.
    sc->fx[n] = sc->actors[n].px16 >= 0 ? sc->actors[n].px16 / 16.0 : (double)sc->actors[n].x;
    sc->fy[n] = sc->actors[n].py16 >= 0 ? sc->actors[n].py16 / 16.0 : sc->actors[n].y - 0.0625;
    sc->mspd[n] = sc->actors[n].moveSpeed; sc->cycles[n] = 0; sc->hasTask[n] = 0; sc->coll[n] = sc->actors[n].coll; sc->collMap[n] = sc->actors[n].collMap; sc->b00[n] = 0; sc->c1C81[n] = sc->actors[n].snap; sc->moveAngle[n] = -1;   // op 0x0E snap captured at init
    sc->folRoamPrev[n] = 1;                  // conga: "was roaming" so f0 is not treated as a resume
    // A placed object with no load op has $1100 = 0x07, not 0x80, so it passes the C0/5FC8 request gate.
    // Only op 0x0A and the non-party PC-load abort (C0/428C) write 0x80.
    sc->mode[n] = sc->actors[n].dead ? 0x80                               // a kill no load op undid (C0/6254)
                : sc->actors[n].defined ? sc->actors[n].moveMode : 0x07;   // $1100,X: the load-op mode (the C0/A819
                                                                          // dispatch key); no load op -> the fill's 07.
  }
  // $7E image mirror: $1100,X move mode and the party-slot -> object table $0197/$0199/$019B (written by the
  // PC loader C0/426A; read by C0/024C's lead-cell record, C0/0715's defeat-pose staging and field.c's
  // C0/1770 battle transfer). CtSceneSeedFromWram overwrites it when used.
  // A later object's fn0 may kill an earlier one (C0/6254 `STA $1100,X = 80` after that object's own fn0
  // ran), so the kill is carried over here rather than writing the earlier mode back.
  for (int n = 0; n < count; n++) {
    if (sc->actors[n].dead) sc->mode[n] = 0x80;
    w8(sc->mem, 0x7E1100 + (uint32_t)n * 2, sc->mode[n] & 0xFF);
    if (sc->mode[n] >= 0 && sc->mode[n] <= 2) w8(sc->mem, 0x7E0197 + (uint32_t)sc->mode[n] * 2, n * 2);
  }
  // The lead is $0197, the object the slot-0 PC loaded into (C0/426A), not the first object whose fn0 has op
  // 0x57: op 0x57 is Crono's load (0x5C Marle, 0x62 Lucca, 0x6A Robo, 0x68 Frog, 0x6C Ayla, 0x6D Magus) and
  // loads nothing when he is not in the party. The scan above is the fallback when $0197 = 80.
  { int v = r8(sc->mem, 0x7E0197);
    if (!(v & 0x80) && v / 2 < count) { sc->playerObj = v / 2; sc->playerLoaded = 1; } }
  // Op 0x88 colour-effect visibility gating. Effect sprites (e.g. the Telepod vortex, 94/95) load and
  // position at init with no palette; op 0x88 (C0/4892) paints them during the cutscene. The port skips
  // op 0x88's palette load and bakes the colours into the sprite, so it marks the emitter and any
  // positioned object on its init tile as colorGated, and CtSceneActor draws them only while the effect
  // is active (colorFx starts 0). Stand-in for the palette load (not ported).
  // The emitter is an object whose script loads a full palette with an op 0x88 blob (cmd 0x80-0x8F). The
  // ref form (cmd 0x20-0x5F) tints a normally visible sprite and is not gated; PC sprites (0-6) never are.
  for (int n = 0; n < count; n++) {
    int emit = 0;
    for (int fn = 0; fn < 16 && !emit; fn++) {
      int a = fpPtr(sc, n, fn);
      for (int k = 0; a >= 0 && a < sc->blen && k < 512; k++) {
        uint8_t op = sc->body[a]; int ln = opLen(sc->body, a);
        if (a + ln > sc->blen) break;
        if (op == 0x88) { uint8_t c = sc->body[a + 1]; if (c >= 0x80 && c < 0x90) { emit = 1; break; } }
        if (op == 0x00) break;
        a += ln;
      }
    }
    if (emit && sc->actors[n].defined && sc->actors[n].hasPos && sc->actors[n].sprite >= 7) {
      sc->actors[n].colorGated = 1;
      int tx = (int)sc->fx[n] >> 4, ty = (int)sc->fy[n] >> 4;     // pair the co-located effect sprite
      for (int j = 0; j < count; j++)                            // (vortex spr 94 + 95 at the same tile)
        if (sc->actors[j].defined && sc->actors[j].hasPos && sc->actors[j].sprite >= 7 &&
            ((int)sc->fx[j] >> 4) == tx && ((int)sc->fy[j] >> 4) == ty)
          sc->actors[j].colorGated = 1;
    }
  }
  sc->tileSolid = NULL; sc->tileCtx = NULL;   // field.c wires the map-collision hook after create ($F8 is in the image)
  sc->touchHit = -1;                            // = the ROM's $01EB "nothing" (0x80) at scene entry
  (void)sceneNo;                                     // unused: the engine has no per-scene branches
  // Scene-entry hook: the load runs obj 0's function 1 (flag-gated E4 room states: opened doors, ...)
  // through C0/595C, called at C0/56C8 right after the init runner C0/5709:
  //   595C REP / STZ $6D / LDA $7F2003 / TAX         obj0's slot-1 pointer, $6D = 0
  //   5967 LDA $7F2001,X / BEQ ->597C                the 00 terminator ends it (not executed)
  //   596D TXY / ... / JSR ($5D6E,X) / BRA ->5967    every other op, whatever carry it returns
  // It ignores the carry, so an op that ends a visit (CLC) does not end this run. A handler that parks
  // (IP unchanged) would spin C0/595C forever; the port stops there instead. The runner is
  // CtSceneRunObj0Slot1, also called by the menu exit and the battle return.
  if (count > 0) {
    CtSceneRunObj0Slot1(sc, "C0/56C8");
    sc->st[0].prio = 7;                              // C0/595C leaves $1001 alone: obj0 keeps the load's count
  }
  // C0/56CB-56D3, the load's last step: `LDA #0B / JSL C2/8004`, the $C2 party gate's leaf 11 (C2/8C5A RTS), run
  // for its epilogue C2/834D, which repaints the weapon rows $21BA/$21DA/$21FA (move.c CtVmC2LoadTail).
  if (gRomImg) {                                       // (a harness without the ROM image has no bank $D1/$FF to read)
    int x = (count > 0) ? fpPtr(sc, 0, 1) : 0;         // 595C LDA $7F2003 / TAX
    CtVmC2LoadTail(sc->mem->s, gRomImg, (int)gRomImgLen, x & 0xFF, g_initLastOp & 0xFF); }
  gLoadPostOn = 0; gOpCostOn = 0;
  if (getenv("CT_SHOWVORTEX"))       // DEBUG: run the emitter's open fn (its fn3 starts with op 0x88) to arm colorFx
    for (int n = 0; n < count; n++) if (sc->actors[n].colorGated) {
      int op3 = fpPtr(sc, n, 3);
      if (op3 >= 0 && op3 < sc->blen && sc->body[op3] == 0x88) {
        int la = sc->st[n].addr;
        sc->st[n].addr = op3; sc->st[n].prio = 0; ctSetDc(sc, n, 1);
        for (int g = 0; g < 200 && sc->st[n].addr >= 0; g++) if (step(sc, n)) break;
        sc->st[n].addr = la; sc->st[n].prio = 7; ctSetDc(sc, n, 1);
      }
    }
  captureTable(sc);
  // Seed $1A81 (the script visibility flag the activation pass reads) from the init pass's `draw`.
  // Ops 0x90/0x91/0x7E maintain it from here on; without the seed an object visible without an explicit
  // show would read 0 and stay inactive.
  for (int o = 0; o < sc->nobj; o++)
    w8(sc->mem, 0x7E1A81 + (uint32_t)o * 2,
       sc->actors[o].draw == 0 ? 0x01 : (sc->actors[o].draw == 2 ? 0x80 : 0x00));
  // No palette sweep here: C0/B0E6 needs the camera. field.c runs it on entry (CtScenePalSweepEntry).
  return sc;
}

CtScene* CtSceneCreate(const uint8_t* raw0, int rawLen, const uint8_t* gmem, int sceneNo) {
  return CtSceneCreateIn(NULL, raw0, rawLen, gmem, sceneNo);
}

// ---- WRAM bridge for the lockstep oracle differential (--vmdiff) ----
// The ROM keeps per-object state in WRAM; this VM keeps it in CtSS/actors. These map between them, to seed
// from an oracle state and compare the scheduler state frame by frame. WRAM: $7E at offset 0, $7F0000 at
// 0x10000. The runner reads the op at $7F2001+IP, so IP = body addr + header (body = raw+1+header). X = obj*2.
#define CTW_IPOFF(sc) ((sc)->header)                // body addr  <-> raw $1180 IP (opcode at $7F2001+IP)

// raw decompressed script vs the oracle's $7F2000 image (wram off 0x12000). 0 = identical.
int CtSceneScriptDiffWram(const CtScene* sc, const uint8_t* w, int* firstDiff) {
  int diffs = 0; if (firstDiff) *firstDiff = -1;
  for (int k = 0; k < sc->rawLen && 0x12000 + k < 0x20000; k++)
    if (sc->raw[k] != w[0x12000 + k]) { if (firstDiff && *firstDiff < 0) *firstDiff = k; diffs++; }
  return diffs;
}

// Seed our VM's per-object scheduler state + script memory from an oracle WRAM image.
void CtSceneSeedFromWram(CtScene* sc, const uint8_t* w) {
  memcpy(sc->mem->s, w, 0x10000);                    // $7E0000..$7EFFFF
  memcpy(sc->mem->g, w + 0x10000, 0x200);            // $7F0000 globals/flags
  memcpy(sc->mem->l, w + 0x10200, 0x200);            // $7F0200 locals
  // ...and $7F:0400-FFFF (Mem is a flat 128 KB image, see ct_wram_layout_assert), which holds the sprite
  // templates at $7F:4800 and the staged OAM records at $7F:4BC0 read by C0/CAD9 and C0/B309.
  memcpy(sc->mem->hi, w + 0x10400, 0xFC00);         // $7F0400..$7FFFFF
  // ...and the engine's programs at $7F3700-3785 (C0/5CEC's copies), which this VM executes from sc->raw.
  // They change while a scene runs: op 0xD9 writes each walk's destination and facing into its `96 x y` /
  // `A6 f` (C0/3311-333C), so a mid-walk state carries them only in WRAM.
  if (sc->raw && sc->rgBase >= 0) memcpy(sc->raw + 0x1700, w + 0x13700, 0x86);
  // ...and the rest of the image, $7F2000-36FF: the runner reads every op from $7F:2001 + IP (C0/5A46), so the
  // seed's bytes are the script it runs. Taken only when the seed holds this scene's image (same object count
  // and fn table). A poked seed runs the poked script on both sides (tools/battle_sim.py writes
  // `83 id op2 / 8B x y ...` at $7F:3000 and points objects' $1180,X there).
  if (sc->raw && sc->rgBase >= 0 && !memcmp(w + 0x12000, sc->raw, (size_t)(1 + sc->header)))
    memcpy(sc->raw, w + 0x12000, 0x1700);
  int hoff = CTW_IPOFF(sc);
  // The dialogue string table is $012B/$012C/$012D (op 0xB8, C0/3557); the loader clears the bank byte
  // (C0/0BDA) and op 0xBB refuses a box while it is zero (C0/3570). Read the seed's pointer back.
  if (w[0x12D] != 0) {
    sc->tbTable = (w[0x12B] | (w[0x12C] << 8) | (w[0x12D] << 16)) - 0xC00000;
    if (w[0x129] != 0) sc->tbTableAtShow = sc->tbTable;   // a box already up: it was opened on this table
  }
  // A box up at capture time ($0129 != 0 with the speaker's $7F0A00,X latch set): rebuild the host view
  // (sc->textbox, speaker $012E, string $012A, position request $0130) so CtSceneAdvance can close it.
  if (w[0x129] != 0) {
    int spk = w[0x12E] / 2;
    if (spk < sc->nobj && w[0x10A00 + spk * 2] != 0) {
      sc->textbox = 1; sc->tbObj = spk; sc->tbIndex = w[0x12A]; sc->tbChoice = 0; sc->tbOpt = -1;
      sc->tbOp = (w[0x130] == 1) ? 0xC1 : (w[0x130] == 2) ? 0xC2 : 0xBB;
      sc->st[spk].tb = 1;
    }
  }
  // Iris (DP = $0100): $0139 = state (1 opening, 2 closing, 0 idle; C0/2263 `STZ $39` ends the open and the
  // radius stays), $013A/$013B = centre in screen pixels (copied to $0350/$0352 for C3/0008), $013C = radius
  // (INC at C0/2260, DEC at 226D), $013D = target ($20).
  sc->irisState = (w[0x139] == 1) ? 1 : (w[0x139] == 2) ? 2 : 0;   // (the rest -- $013A-$013D, the tables -- IS the seed)
  // The party trail runs in the player movement controller, gated by $0162 == 0 && $011F != 0 (player.c);
  // restore both from the state.
  sc->explore  = (w[0x11F] != 0);                    // $011F: 0 = control locked (E3 00 / cutscene)
  sc->moveLock = (w[0x162] != 0);                    // $0162 != 0 also locks the controller (+ party-trail)
  // The screen brightness is $0119 (the NMI's C0/EC4E `LDA $0119 / BNE ->EC55 / LDA #80 / STA $2100` writes it
  // to INIDISP every frame). sc->bright is its low nibble (CtSceneSyncBright); rebuild it from the byte rather
  // than keep what the create-time prescan's fn0 set.
  sc->bright = w[0x119] & 0x0F;
  for (int o = 0; o < sc->nobj; o++) {
    int x = o * 2;
    int ip   = w[0x1180 + x] | (w[0x1181 + x] << 8);
    int prio = w[0x1C00 + x] & 0x07;
    int px   = w[0x1800 + x] | (w[0x1801 + x] << 8);
    int py   = w[0x1880 + x] | (w[0x1881 + x] << 8);
    int waitf= w[0x10980 + x];                       // $7F0980 CALL wait flag
    int waitc= w[0x10A01 + x];                       // $7F0A01 op-0xAD wait counter
    CtSS* s = &sc->st[o];
    memset(s->ret, 0, sizeof s->ret);
    for (int p = 0; p < 8; p++) {                    // $7F0580[ prio*0x80 + obj*2 ] return stack
      int ra = w[0x10580 + p * 0x80 + x] | (w[0x10580 + p * 0x80 + x + 1] << 8);
      s->ret[p] = ra ? (ra - hoff) : 0;
    }
    int cd = w[0x1001 + x];                          // $1001,X cadence countdown (run when it hits 0)
    int pd = w[0x1000 + x] & 0x7F;                   // $1000,X cadence period (bit 7 = paused), taken from the
    sc->actors[o].moveAnim = pd;                     // seed (op 0x87 periods set before the capture are not
                                                     // replayed). pd = 0 -> the C0/42D7 fallback cadence 1.
    s->prio = prio; s->dc = cd; s->tb = 0; s->nm = 0;   // the shadow is $1001,X, seeded as is: 0 means
                                                        // "expired, not yet enqueued" and C0/59F2 runs it
                                                        // on the first build.
    s->wait = waitc; s->waitObj = -1; s->waitPrio = 0; s->callAddr = -1;
    // Op 0xB0/0xB2 is an active idle-keeper (C0/3002: drives the idle anim, sets $1A01 = 1 and CLCs so the IP
    // never advances), not "no script". Keep the IP so it keeps running.
    uint8_t opAtIp = (0x12001 + ip < 0x20000) ? w[0x12001 + ip] : 0;   // script base = $7F2001 (= w[0x12001])
    int parkedScript = (opAtIp == 0xB0 || opAtIp == 0xB2);
    // Prio-7 disposition: the ROM has no idle state; the runner executes at $1180 every cadence visit, and
    // a finished object parks on a forever-yield op (B0/B2). A prio-7 object on any other op is an autorun
    // in flight and resumes at its IP. Only a stale Return park (op 0x00) seeds as idle; the port's own
    // op 0x00 at prio 7 idles it again at once, so both converge.
    s->addr = (prio >= 7 && w[0x1A01 + x] == 0 && !parkedScript && opAtIp == 0x00) ? -1 : (ip - hoff);
                                                     // a ticket ($1A01 > 0) or a B0/B2 park keeps the IP: a
                                                     // paced NPC runs its mover at prio 7
    // Reconstruct an in-flight CALL+WAIT (op 0x04/0x07): the ROM parks $1180 ON the call op with
    // the wait flag set; our model advances the IP past it and tracks waitObj. Bridge it.
    if (waitf && s->addr >= 0 && s->addr < sc->blen) {
      uint8_t op = sc->body[s->addr];
      if (op == 0x04 || op == 0x07) {
        // 0x07's poll arm (C0/6232) re-reads the host the same way its install did: $0197 + slot.
        int tgt = (op <= 0x04) ? sc->body[s->addr + 1] / 2 : (w[0x0197 + sc->body[s->addr + 1]] & 0x7F) / 2;
        int pr  = (sc->body[s->addr + 2] & 0xF0) >> 4;
        s->callAddr = s->addr;
        s->addr += opLen(sc->body, s->addr);
        if (tgt < sc->nobj) { s->waitObj = tgt; s->waitPrio = pr; }
      } else {
        // The latch is set but the IP is elsewhere: a higher-priority function pre-empted the waiter, and its
        // parked 04/07 is the return slot the RETURN will land on (the op-0x00 scan, the first slot above prio).
        for (int p = prio + 1; p < 8; p++) {
          int ra = s->ret[p];
          if (!ra) continue;
          if (ra > 0 && ra + 2 < sc->blen && (sc->body[ra] == 0x04 || sc->body[ra] == 0x07)) {
            int tgt = (sc->body[ra] == 0x04) ? sc->body[ra + 1] / 2 : (w[0x0197 + sc->body[ra + 1]] & 0x7F) / 2;
            if (tgt < sc->nobj) { s->waitObj = tgt; s->waitPrio = (sc->body[ra + 2] & 0xF0) >> 4;
                                  s->callAddr = ra; }   // ret[p] stays ON the 04: the RETURN polls there
                                                         // (or installs anew if a 04 of the pre-empting
                                                         // function cleared the latch first)
          }
          break;
        }
      }
    }
    // Units: WRAM $1800/$1880 are subpixels (1/16 px); fx/fy and actors[].x/.y are pixels. The fraction is
    // kept in the fx double, so the conversion is lossless.
    sc->actors[o].x = px >> 4; sc->actors[o].y = py >> 4; sc->actors[o].hasPos = 1;
    sc->actors[o].facing = w[0x1600 + x];            // $1600,X facing (read by op 0x24/branches)
    sc->fx[o] = px / 16.0; sc->fy[o] = py / 16.0;
    sc->mspd[o] = sc->actors[o].moveSpeed = w[0x1A00 + x];   // $1A00,X move speed (op 0x89): drives mvComp velocity
    sc->mode[o] = w[0x1100 + x];                    // $1100,X move mode, the C0/A819 dispatch key (lead PC 0,
                                                    // scripted NPC 4, empty 0x80); set at load only
    // `dead` is $1100,X bit 7: the byte op 0x0A stores (C0/625F `LDA #80 / STA $1100,X`) and the first test of
    // the draw-list gate (C0/A88F BMI), the rebuild (C0/B0F3) and the OAM drain (C0/CAD9). Taken from the
    // seed, not from the create-time prescan, which ran fn0 with whatever story flags the host passed.
    sc->actors[o].dead = (w[0x1100 + x] & 0x80) ? 1 : 0;
    sc->actors[o].solid = w[0x1B01 + x] & 1;        // $1B01,X bit 0 = solid: a loaded NPC blocks and touch-triggers
    // `draw` is $1A81, the script-visibility byte the ops write (C0/4CD5 #01 shown, 4CE0 #00 hidden, 4CE6 #80
    // removed, all into the tail C0/4CD9). $0F00 is a different quantity: C0/A810 derives it from $1A81 and
    // the camera window. Consumers that mean "this frame" read $0F00 directly (npcSolidTile /
    // CtSceneActorTouchable).
    sc->actors[o].draw = (w[0x1A81 + x] & 0x80) ? 2 : (w[0x1A81 + x] ? 0 : 1);
    if (w[0x0F00 + x] != 0) sc->actors[o].defined = 1;        // an active/placed NPC -> defined (so it counts as placed)
    sc->b00[o] = (int8_t)w[0x10B00 + x];            // $7F0B00,X signed: a mid-step mover resumes its lifecycle
                                                    // phase (> 0 issue frame, 0 clamp, 0x80 vector)
    sc->c1C81[o] = w[0x1C81 + x];   // $1C81,X snap enable (op 0x0E): op 0x96 walks to the tile centre via
                                    // C0/305D (1 px per visit)
    // $1C80,X collision gates (op 0x0D). Field enemies often have 0 (no map bounce).
    { uint8_t c80 = w[0x1C80 + x];
      sc->coll[o] = (c80 >> 1) & 1; sc->collMap[o] = c80 & 1;
      sc->actors[o].coll = sc->coll[o]; sc->actors[o].collMap = sc->collMap[o]; }
    sc->actors[o].cadPause = (w[0x1000 + x] & 0x80) ? 1 : 0;  // $1000 bit 7 pause (op 0x0B/0x0C)
    // $1A81, the script visibility flag the activation pass reads, copied from the seed.
    w8(sc->mem, 0x7E1A81 + (uint32_t)x, w[0x1A81 + x]);
    // In-flight move: the per-frame velocity is $1900/$1980 (signed 8-bit subpx per frame) and the step
    // ticket $1A01. A mover caught mid-step resumes with that velocity this frame.
    int vx = (int8_t)w[0x1900 + x], vy = (int8_t)w[0x1980 + x], tk = w[0x1A01 + x];
    if ((vx || vy) && tk > 0) {                            // active mid-step = a nonzero ticket, at any prio
      sc->mx[o] = vx / 16.0; sc->my[o] = vy / 16.0;        // (a paced NPC whose autorun returned to prio 7
      sc->cycles[o] = tk; sc->hasTask[o] = 1;              // still drains its mover); with tk == 0 the
    }                                                      // velocity is stale
    // In-flight blocking anim (op 0xAB/0xB7): C0/2E9A parks the IP on the op with $1780 = 2 (once), $1781 =
    // anim id, $7F0B01,X = 2 while C0/CA4B plays it and 1 = "complete on next visit". CtVmAnimFamily tests
    // that byte on every visit, so a seeded 2 parks and a seeded 1 completes. See ctAnimSyncActor.
    ctAnimSyncActor(&sc->actors[o], w, x);
  }
}

// How many objects the draw compare graded on the last CtSceneDiffWram call, and out of how many. A check
// that pins "0 divergences" also pins this, so a filter such as the class guard (C0/B0F3) cannot swallow
// every object unseen. The compare itself increments the count; re-deriving it here would copy the guard.
static int gDrawGraded = 0, gDrawTotal = 0;
int CtSceneDrawGraded(const CtScene* sc, const uint8_t* w, int* total) {
  (void)sc; (void)w;
  if (total) *total = gDrawTotal;
  return gDrawGraded;
}

// Compare the scheduler state to the oracle WRAM; record up to `cap` divergences this frame. Compares
// $1C00 priority always and $1180 IP while the object runs a function (prio < 7). withPos adds position.
int CtSceneDiffWram(CtScene* sc, const uint8_t* w, int withPos, CtVmDiff* out, int cap) {
  int n = 0, hoff = CTW_IPOFF(sc);
  gDrawGraded = 0; gDrawTotal = sc->nobj;
  for (int o = 0; o < sc->nobj && n < cap; o++) {
    int x = o * 2;
    CtSS* s = &sc->st[o];
    int oPrio = w[0x1C00 + x] & 0x07;
    int oIp   = w[0x1180 + x] | (w[0x1181 + x] << 8);
    // VMDIFF_ACT=1: also compare the $0F00 activation latch (the C0/A810 pass). Off by default so the
    // pinned checks keep their field set.
    { static int actOn = -1; if (actOn < 0) { const char* e = getenv("VMDIFF_ACT"); actOn = e && *e != '0'; }
      if (actOn) {
        int myAct = r8(sc->mem, 0x7E0F00 + (uint32_t)x) != 0;
        int oAct  = w[0x0F00 + x] != 0;
        // Class 8 is never written by the pass (C0/A824 `CMP #08 / BEQ ->A83E`), so comparing it would
        // compare the seed. Class 7 is: C0/A828 `CMP #07 / BEQ ->A86B` -> `JSR C0/A947`, its own signed,
        // unclamped window test (disasm/notes/field_trigger_latch_a947.md).
        int cls = r8(sc->mem, 0x7E1100 + (uint32_t)x);
        if (cls != 8 && myAct != oAct && n < cap)
          out[n++] = (CtVmDiff){ o, "act", myAct, oAct };
      } }
    // VMDIFF_DRAW=1: compare the `draw` field against the ROM's predicate (off by default, like VMDIFF_ACT):
    //   C0/B0F1  STX $6D / LDA $1100,X / BMI ->B11A / CMP #07 / BEQ ->B11A   ; class skip
    //   C0/B0FC  JSR C0/B127 / BCC ->B11A                                    ; carry clear = not drawn
    //   C0/B127  LDA $1A81,X / BEQ ->B190 / BMI ->B190                       ; hidden / removed
    //            ...then the camera-window test; B190 = CLC / RTS
    // The same head gates the drain visit (C0/CAE2, C0/CB12) and the activation pass (C0/A894). `draw`
    // encodes $1A81,X: 0 shown -> $01, 1 hidden -> $00, 2 removed -> $80 (C0/4CD5 / 4CE0 / 4CE6 -> C0/4CD9).
    // The field is compared, not the mirrored byte, which the write-mask compare already covers.
    // The guard is the ROM's class skip: $1A81 is never cleared by a load (wram_load_census.md), so an
    // unloaded slot ($1100 bit 7) keeps the previous scene's value and is not graded.
    { static int drwOn = -1; if (drwOn < 0) { const char* e = getenv("VMDIFF_DRAW"); drwOn = e && *e != '0'; }
      uint8_t cls = w[0x1100 + x];
      if (drwOn && !(cls & 0x80) && cls != 7) {
        gDrawGraded++;                                 // the guard's only copy -- see CtSceneDrawGraded
        int myV = sc->actors[o].draw == 0 ? 0x01 : (sc->actors[o].draw == 2 ? 0x80 : 0x00);
        if (myV != w[0x1A81 + x] && n < cap)
          out[n++] = (CtVmDiff){ o, "draw", myV, w[0x1A81 + x] };
      } }
    if (s->prio != oPrio && n < cap) out[n++] = (CtVmDiff){ o, "prio", s->prio, oPrio };
    if (s->prio < 7) {                               // we're running a function: compare the IP
      int addr = ssParked(s) ? s->callAddr : s->addr;
      int myIp = (addr < 0) ? -1 : (addr + hoff);
      if (myIp != oIp && n < cap) out[n++] = (CtVmDiff){ o, "ip", myIp, oIp };
    }
    if (withPos) {                                   // pixels -> subpixels for the oracle compare (rounded, lossless)
      int oPx = w[0x1800 + x] | (w[0x1801 + x] << 8);
      int oPy = w[0x1880 + x] | (w[0x1881 + x] << 8);
      int myPx = ((int)lround(sc->fx[o] * 16.0)) & 0xFFFF;
      int myPy = ((int)lround(sc->fy[o] * 16.0)) & 0xFFFF;
      if (myPx != oPx && n < cap) out[n++] = (CtVmDiff){ o, "posX", myPx, oPx };
      if (myPy != oPy && n < cap) out[n++] = (CtVmDiff){ o, "posY", myPy, oPy };
    }
  }
  return n;
}

// ---- Write-mask byte compare (see ct_vm.h) ----
// Gather our three disjoint buffers into the linear order the oracle's WRAM image uses.
static void wmGatherM(const Mem* m, uint8_t* out) {
  memcpy(out, m->s, 0x10000);                    // $7E0000..$7EFFFF
  memcpy(out + 0x10000, m->g, 0x200);            // $7F0000 globals/flags
  memcpy(out + 0x10200, m->l, 0x200);            // $7F0200 script locals
}
void CtVmWmaskEnable(int on) {
  if (on < 0) { const char* e = getenv("VMDIFF_WMASK"); gWm = e && *e != '0'; }
  else gWm = on ? 1 : 0;
}
int CtVmWmaskOn(void) { return gWm; }

// Each pair below is a thin shell over one Mem* core: sc->mem is the CtWram (CtSceneCreateIn), so the
// scene-addressed and wram-addressed entry points are the same call.
static void wmResetM(const Mem* m, const uint8_t* w) {
  memset(gWmMask, 0, sizeof gWmMask); memset(gWmExcl, 0, sizeof gWmExcl);
  memset(gWmOchg, 0, sizeof gWmOchg);
  for (int i = 0; i < WM_LEN; i++) gWmFirst[i] = -1;
  gWmDropped = 0;
  // The seed is bulk memcpys, not w8 calls, so it leaves the mask empty: a seeded byte never written is
  // inherited, not claimed. Record the oracle's seed image as the coverage denominator.
  if (w) memcpy(gWmSeedO, w, WM_LEN); else memset(gWmSeedO, 0, sizeof gWmSeedO);
  if (m) wmGatherM(m, gWmShadow); else memset(gWmShadow, 0, sizeof gWmShadow);
}
void CtVmWmaskReset(const CtScene* sc, const uint8_t* w) { wmResetM(sc ? sc->mem : NULL, w); }
void CtVmWmaskResetW(CtWram* w7e, const uint8_t* w)      { wmResetM((const Mem*)w7e, w); }

void CtVmWmaskExclude(uint32_t addr, int len) {
  if (!gWm) return;
  for (int k = 0; k < len; k++) { int i = wmIndex(addr + (uint32_t)k); if (i >= 0) gWmExcl[i] = 0xFF; }
}

static void wmFramePreM(const Mem* m) { if (gWm && m) wmGatherM(m, gWmShadow); }
void CtVmWmaskFramePre(const CtScene* sc) { wmFramePreM(sc ? sc->mem : NULL); }
void CtVmWmaskFramePreW(CtWram* w7e)      { wmFramePreM((const Mem*)w7e); }

static void wmFramePostM(const Mem* m, const uint8_t* w) {
  if (!gWm || !m) return;
  static uint8_t live[WM_LEN];
  wmGatherM(m, live);
  for (int i = 0; i < WM_LEN; i++) {
    uint8_t d = (uint8_t)(gWmShadow[i] ^ live[i]);
    if (d) gWmMask[i] |= d;                    // the raw-pointer paths land here, per bit
    if (w) { uint8_t o = (uint8_t)(gWmSeedO[i] ^ w[i]); if (o) gWmOchg[i] |= o; }
  }
}
void CtVmWmaskFramePost(const CtScene* sc, const uint8_t* w) { wmFramePostM(sc ? sc->mem : NULL, w); }
void CtVmWmaskFramePostW(CtWram* w7e, const uint8_t* w)      { wmFramePostM((const Mem*)w7e, w); }

// Latching byte compare: each address is reported once, at the frame it first diverged.
static int wmDiffM(const Mem* m, const uint8_t* w, int frame, CtVmByteDiff* out, int cap) {
  if (!gWm || !m || !w) return 0;
  static uint8_t live[WM_LEN];
  wmGatherM(m, live);
  int n = 0;
  for (int i = 0; i < WM_LEN && n < cap; i++) {
    uint8_t mk = (uint8_t)(gWmMask[i] & ~gWmExcl[i]);
    if (!mk || gWmFirst[i] >= 0) continue;
    if (!((live[i] ^ w[i]) & mk)) continue;
    gWmFirst[i] = frame;
    out[n++] = (CtVmByteDiff){ wmAddr(i), mk, live[i], w[i], gWmSeedO[i], frame };
  }
  return n;
}
int CtSceneDiffWramMasked(const CtScene* sc, const uint8_t* w, int frame,
                          CtVmByteDiff* out, int cap) {
  return wmDiffM(sc ? sc->mem : NULL, w, frame, out, cap);
}
int CtSceneDiffWramMaskedW(CtWram* w7e, const uint8_t* w, int frame,
                           CtVmByteDiff* out, int cap) {
  return wmDiffM((const Mem*)w7e, w, frame, out, cap);
}

// The byte census: lists the unmodelled bytes (the oracle changed them, the port never claims them) on the
// same condition CtVmWmaskStats counts, folded into runs. Excluded (harness-injected) bytes are listed with a
// label rather than filtered. Runs break on a gap or on a change of the excluded flag. Callable from both
// ct.exe's --vmdiff (field VM alone) and CT_FIELDLOCK (the live engine's $7E image).
// $0508-$06FF is the 65816 stack (also excluded by --bootcheck): the port has no hardware stack.
static int wmIsStack(uint32_t a) { return a >= 0x7E0508u && a < 0x7E0700u; }

// Regions the port models but writes outside the $7E image this mask watches; they would otherwise list
// as unmodelled. Every entry names the check that grades it. CtBuildFieldOAM builds $0700/$0900 into
// scratch buffers (main.c).
static const struct { uint32_t lo, hi; const char* graded; } kWmElsewhere[] = {
  { 0x7E0700, 0x7E08FF, "OAM shadow, built into scratch -- graded by oam-build-field (0 mismatches)" },
  { 0x7E0900, 0x7E091F, "OAM high table, same build -- graded by oam-build-field" },
};
static const char* wmElsewhere(uint32_t a) {
  for (size_t k = 0; k < sizeof kWmElsewhere / sizeof kWmElsewhere[0]; k++)
    if (a >= kWmElsewhere[k].lo && a <= kWmElsewhere[k].hi) return kWmElsewhere[k].graded;
  return NULL;
}

void CtVmWmaskCensusReport(int toStdout) {
  FILE* f = toStdout ? stdout : stderr;
  if (!gWm) { fprintf(f, "  [census] wmask is OFF -- nothing to report\n"); return; }
  // The 65816 stack is filtered here and counted separately, not in CtVmWmaskStats, whose count is
  // pinned by vmdiff-wmask-party3.
  int tot = 0, runs = 0, stk = 0, els = 0;
  for (int i = 0; i < WM_LEN; i++) {
    if (!gWmOchg[i] || gWmMask[i]) continue;
    uint32_t a = wmAddr(i);
    if (wmIsStack(a)) stk++; else if (wmElsewhere(a)) els++; else tot++;
  }
  fprintf(f, "  [census] %d unmodelled byte(s) -- the oracle drove them and we never claim them"
             " (+%d in the 65816 stack $0508-$06FF, structurally unclaimable and not listed;"
             " +%d modelled OUTSIDE this image, listed below with their grader)\n", tot, stk, els);
  for (int i = 0; i < WM_LEN; ) {
    if (!gWmOchg[i] || gWmMask[i] || wmIsStack(wmAddr(i))) { i++; continue; }
    const char* gr = wmElsewhere(wmAddr(i));
    int j = i + 1, ex = gWmExcl[i] ? 1 : 0;
    while (j < WM_LEN && gWmOchg[j] && !gWmMask[j] && (gWmExcl[j] ? 1 : 0) == ex
           && !wmIsStack(wmAddr(j)) && wmElsewhere(wmAddr(j)) == gr
           && wmAddr(j) == wmAddr(j - 1) + 1) j++;
    fprintf(f, "  [census] $%06X..$%06X  %5d B  %s\n", wmAddr(i), wmAddr(j - 1), j - i,
            gr ? gr : ex ? "HARNESS-INJECTED (excluded from the compare)" : "unclaimed");
    runs++;
    i = j;
  }
  fprintf(f, "  [census] %d run(s)\n", runs);
}

void CtVmWmaskStats(int* masked, int* excluded, int* oracleChanged, int* unmodelled, int* dropped) {
  int mk = 0, ex = 0, oc = 0, un = 0;
  for (int i = 0; i < WM_LEN; i++) {
    if (gWmMask[i]) mk++;
    if (gWmExcl[i]) ex++;
    if (gWmOchg[i]) { oc++; if (!gWmMask[i]) un++; }   // the ROM drives it and we never write it
  }
  if (masked) *masked = mk; if (excluded) *excluded = ex;
  if (oracleChanged) *oracleChanged = oc; if (unmodelled) *unmodelled = un;
  if (dropped) *dropped = gWmDropped;
}

// Our projected $1180 IP for one object (header+addr; the CALL op for a waiting caller); -1 = idle.
int CtSceneObjIp(const CtScene* sc, int obj, int* prio) {
  const CtSS* s = &sc->st[obj];
  if (prio) *prio = s->prio;
  if (s->prio >= 7) return -1;
  int addr = ssParked(s) ? s->callAddr : s->addr;
  return (addr < 0) ? -1 : (addr + sc->header);
}

// ---- Field camera (disasm/notes/field_camera_unification.md) ----
// The ROM has one field camera: the DP=$1D00 scroll ($1D0A/$1D0E coarse 8 px units, $1D93/$1D96 phase 0..15;
// rendered scroll camPx = $1D0A*8 + ($1D93&7)), updated per frame by the player-controller chain C0/881E and
// read by battle staging and the PPU. field.c/player.c own the collision half (88E5 -> 88EE -> 8A6D -> 9175,
// leaving the request in $1D2E/$1D30); this tick owns the camera tail: C0/99DE gate (deadzone, edges) ->
// C0/91AC seek/auto -> C0/93E1 accumulate and commit. All state lives in mem.s[$1D..].
static int camS8(uint8_t v) { return (v >= 0x80) ? (int)v - 256 : v; }               // signed byte
static int camGet16(const uint8_t* s, int off) { return s[off] | (s[off + 1] << 8); }
static void camPut16(uint8_t* s, int off, int v) { s[off] = (uint8_t)v; s[off + 1] = (uint8_t)(v >> 8); }

// C0/99DE: zero $2E/$30 per axis via the four deadzone/edge helpers (carry set in the ROM = kill). The
// deadzone centre is (128,136) on the lead's on-screen foot; the edge-tile phase test caps a far pin; the
// coarse-tile test pins at the mask bound. onX/onY = the lead's on-screen foot (world - camPx) & 0xFF.
static void camGate99DE(uint8_t* s, int onX, int onY) {
  int req = camS8(s[0x1D2E]);
  if (req != 0) {
    int px = (req < 0 ? -req : req) >> 4, kill;
    if (req > 0) {                                            // 9A60 X right
      int rt = s[0x1D0C] >> 1;                                //   $1D0C>>1 (right-edge tile, low byte)
      if (rt > s[0x1D1B])      kill = 1;                      //   past the mask right bound
      else if (rt == s[0x1D1B]) kill = (s[0x1D93] + px) >= 16;//   at the edge tile: phase would cross out
      else                      kill = (onX & 0xFF) <= 128;   //   deadzone: right-scroll needs onscreen >= 129
    } else {                                                  // 9A7E X left
      int lt = s[0x1D0A] >> 1;
      if (s[0x1D1A] > lt)       kill = 1;
      else if (s[0x1D1A] == lt) kill = (s[0x1D93] - px) < 0;
      else                      kill = (onX & 0xFF) >= 128;   //   left-scroll needs onscreen <= 127
    }
    if (kill) s[0x1D2E] = 0;
  }
  req = camS8(s[0x1D30]);
  if (req != 0) {
    int px = (req < 0 ? -req : req) >> 4, kill;
    if (req > 0) {                                            // 9A1F Y down
      int bt = s[0x1D10] >> 1;
      if (bt > s[0x1D1D])       kill = 1;
      else if (bt == s[0x1D1D]) kill = (s[0x1D96] + px) >= 16;
      else                      kill = (onY & 0xFF) <= 136;   //   down-scroll needs onscreen >= 137
    } else {                                                  // 9A3D Y up
      int tt = s[0x1D0E] >> 1;
      if (s[0x1D1C] > tt)       kill = 1;
      else if (s[0x1D1C] == tt) kill = (s[0x1D96] - px) < 0;
      else                      kill = (onY & 0xFF) >= 136;   //   up-scroll needs onscreen <= 135
    }
    if (kill) s[0x1D30] = 0;
  }
}

// C0/91AC: the op 0xE7 seek servo (it overwrites $2E/$30, bypassing the gate and edges) and the $01BC
// auto-scroll derive. Arrival = coarse == target and step counter & 0xF == 0 (E7 targets are even, so
// 16-aligned). The pace is 1 px per frame through the commit below.
static void camSeek91AC(CtScene* sc) {
  uint8_t* s = sc->mem->s;
  if (s[0x1D00] != 0) {
    int cX = s[0x1D0A], tX = s[0x1D02];                       // X (C0/91B0)
    if      (tX > cX) s[0x1D2E] = 0x10;
    else if (tX < cX) s[0x1D2E] = 0xF0;                       // -0x10
    else { if ((s[0x1D87] & 0x0F) != 0) s[0x1D2E] = 0xF0; else s[0x1D01] |= 0x01; }
    int cY = s[0x1D0E], tY = s[0x1D03];                       // Y (C0/91CE)
    if      (tY > cY) s[0x1D30] = 0x10;
    else if (tY < cY) s[0x1D30] = 0xF0;
    else { if ((s[0x1D89] & 0x0F) != 0) s[0x1D30] = 0xF0; else s[0x1D01] |= 0x02; }
    if (s[0x1D01] == 3) { s[0x1D00] = 2; s[0x1D01] = 0; }     // C0/91EC: both arrived -> complete
  }
  // C0/91F8 auto-scroll derive, only while $01BC is armed. Open question: $0400 has no known field-bank
  // writer, so no scene arms it; log once if one does.
  if (s[0x01BC] != 0) {
    if (!sc->camAutoWarned) {
      fprintf(stderr, "[camera] WARNING: $01BC=%02X auto-scroll armed (dossier OPEN; $0400=%02X)\n", s[0x01BC], s[0x0400]);
      sc->camAutoWarned = 1;
    }
    int f = s[0x0400], neg = f & 1;                           // bit2->X, bit1->Y, bit0->negate both (1px/f)
    s[0x1D4E] = (f & 4) ? (neg ? 0xF0 : 0x10) : 0;
    s[0x1D4F] = (f & 2) ? (neg ? 0xF0 : 0x10) : 0;
    if (s[0x1D0A] == 0) s[0x1D4E] = 0;                        // parked per-axis at coarse 0 (C0/922C)
    if (s[0x1D0E] == 0) s[0x1D4F] = 0;
  }
}

// C0/9238-93E0: accumulate this frame's request, op 0xE6 drift and auto delta into the 4.4 fraction
// ($3B/$3C, 8-bit signed wrap), extract whole pixels (truncate toward zero, keep the remainder), then commit
// per pixel (phase 0..15; the coarse 8 px twins bump at the 7<->8 and 15<->0 crossings; step counters
// $87/$89 +-1 per px). The $74-$77 flags feed the VRAM streamer.
static void camCommit93E1(uint8_t* s) {
  s[0x1D3B] = (uint8_t)(camS8(s[0x1D3B]) + camS8(s[0x1D2E]) + camS8(s[0x1D24]) + camS8(s[0x1D4E]));  // 9238
  s[0x1D3C] = (uint8_t)(camS8(s[0x1D3C]) + camS8(s[0x1D30]) + camS8(s[0x1D25]) + camS8(s[0x1D4F]));
  // C0/9252-92C8: BG2 and BG3, the same accumulate. A layer whose bit is set in $1D36 is detached: it takes
  // its own speed ($37..$3A, from the map header) and its op 0xE6 drift ($26..$29) but not the camera terms
  // $2E/$30 and $4E/$4F.
  //   9252 LDA $36 / BIT #02 / BEQ ->926E
  //   9258 $3D = $37 + $3D + $26         ; 9262 $3E = $38 + $3E + $27        (detached)
  //   926E $3D = $2E + $37 + $3D + $26 + $4E ; 927E $3E = $30 + $38 + $3E + $27 + $4F
  //   928E LDA $36 / BIT #04 ... the same for BG3 into $3F/$40 from $39/$3A and $28/$29
  // Every add is an 8-bit `CLC / ADC`, so the sum wraps (the uint8_t cast).
  { int det2 = s[0x1D36] & 0x02, det3 = s[0x1D36] & 0x04;
    int camX = camS8(s[0x1D2E]) + camS8(s[0x1D4E]), camY = camS8(s[0x1D30]) + camS8(s[0x1D4F]);
    s[0x1D3D] = (uint8_t)(camS8(s[0x1D3D]) + camS8(s[0x1D37]) + camS8(s[0x1D26]) + (det2 ? 0 : camX));
    s[0x1D3E] = (uint8_t)(camS8(s[0x1D3E]) + camS8(s[0x1D38]) + camS8(s[0x1D27]) + (det2 ? 0 : camY));
    s[0x1D3F] = (uint8_t)(camS8(s[0x1D3F]) + camS8(s[0x1D39]) + camS8(s[0x1D28]) + (det3 ? 0 : camX));
    s[0x1D40] = (uint8_t)(camS8(s[0x1D40]) + camS8(s[0x1D3A]) + camS8(s[0x1D29]) + (det3 ? 0 : camY));
    // 9312-93D0: extract whole pixels exactly like BG1 below (>>4 of the magnitude, the signed nibble
    // kept), then C0/9579-977x step each layer's counter one per pixel: `LDX $8D / INX / STX $8D`.
    // The coarse/phase twins those loops also bump ($1D0B.., $1D94/$1D97..) only feed the VRAM
    // streamer's row/column flags; the port composes from the map, so only the counters are kept.
    static const int acc[4] = { 0x1D3D, 0x1D3E, 0x1D3F, 0x1D40 };
    static const int cnt[4] = { 0x1D8B, 0x1D8D, 0x1D8F, 0x1D91 };
    for (int i = 0; i < 4; i++) {
      int a = camS8(s[acc[i]]), w = a / 16;
      s[acc[i]] = (uint8_t)(a - w * 16);
      // The half-camera map mode steps BG2 on a 32 px phase. 93F5 `LDA $0BC9 / BMI ->9408` swaps BG2's four committers:
      //   95E3 (X+) per pixel: old = $94; == 1F -> INC $12 / INC $14 / TSB $76 #02 / A = FF; == 0F -> the same, A = 0F;
      //        INC A / STA $94 / AND #01 / BNE -> skip / LDX $8B / INX / STX $8B
      //   9619 (X-): old == 0 -> DEC $14 / DEC $12 / TSB $77 #02 / A = 20; == 10 -> the same, A = 10; DEC A / STA $94 /
      //        even -> DEX $8B        964D / 9683: the same on $97, $16/$18, $74/$75 and $8D.
      // So the counter moves on every other pixel of BG2's motion (half the camera). BG3 keeps its ordinary
      // committers (96B7..), and no normal arm needs the phase for its counter.
      if (i < 2 && (s[0x0BC9] & 0x80)) {
        int ph = i == 0 ? 0x1D94 : 0x1D97, lo = i == 0 ? 0x1D12 : 0x1D16, hi = i == 0 ? 0x1D14 : 0x1D18;
        for (int k = 0; k < w; k++) {
          int A = s[ph];
          if (A == 0x1F) { s[lo]++; s[hi]++; A = 0xFF; }      // (the $74-$77 streamer flags are not kept: the port
          if (A == 0x0F) { s[lo]++; s[hi]++; A = 0x0F; }      //  clears them below, after this pass, unlike 93E1)
          A = (A + 1) & 0xFF; s[ph] = (uint8_t)A;
          if (!(A & 1)) camPut16(s, cnt[i], camGet16(s, cnt[i]) + 1);
        }
        for (int k = 0; k < -w; k++) {
          int A = s[ph];
          if (A == 0x00) { s[hi]--; s[lo]--; A = 0x20; }
          if (A == 0x10) { s[hi]--; s[lo]--; A = 0x10; }
          A = (A - 1) & 0xFF; s[ph] = (uint8_t)A;
          if (!(A & 1)) camPut16(s, cnt[i], camGet16(s, cnt[i]) - 1);
        }
        continue;
      }
      camPut16(s, cnt[i], camGet16(s, cnt[i]) + w);
    } }
  int ax = camS8(s[0x1D3B]), wX = ax / 16; s[0x1D3B] = (uint8_t)(ax - wX * 16);      // 92CA extract + remainder
  int ay = camS8(s[0x1D3C]), wY = ay / 16; s[0x1D3C] = (uint8_t)(ay - wY * 16);
  // C0/93CC-93DE, op 0xE6's duration: `LDA $4D / BEQ ->93E0 / DEC $4D / BNE ->93E0 / STZ $24 .. STZ $29`. A
  // non-zero count runs the three layers' drift for that many frames, then stops it; zero leaves it running.
  if (s[0x1D4D] != 0 && --s[0x1D4D] == 0) {
    s[0x1D24] = s[0x1D25] = s[0x1D26] = s[0x1D27] = s[0x1D28] = s[0x1D29] = 0;
  }
  int xp = wX > 0 ? wX : 0, xm = wX < 0 ? -wX : 0;
  int yp = wY > 0 ? wY : 0, ym = wY < 0 ? -wY : 0;
  s[0x1D76] = s[0x1D77] = s[0x1D74] = s[0x1D75] = 0;                                  // 93E1 head: clear streamer flags
  for (int i = 0; i < xp; i++) {                                                      // 944B X+
    camPut16(s, 0x1D87, camGet16(s, 0x1D87) + 1);
    int old = s[0x1D93];
    if (old == 0x0F) { s[0x1D0A]++; s[0x1D0C]++; s[0x1D76] = 1; old = 0xFF; }
    if (old == 0x07) { s[0x1D0A]++; s[0x1D0C]++; s[0x1D76] = 1; old = 0x07; }
    s[0x1D93] = (uint8_t)(old + 1);
  }
  for (int i = 0; i < xm; i++) {                                                      // 947D X-
    camPut16(s, 0x1D87, camGet16(s, 0x1D87) - 1);
    int old = s[0x1D93];
    if (old == 0x00) { s[0x1D0C]--; s[0x1D0A]--; s[0x1D77] = 1; old = 0x10; }
    if (old == 0x08) { s[0x1D0C]--; s[0x1D0A]--; s[0x1D77] = 1; old = 0x08; }
    s[0x1D93] = (uint8_t)(old - 1);
  }
  for (int i = 0; i < yp; i++) {                                                      // 94AD Y+
    camPut16(s, 0x1D89, camGet16(s, 0x1D89) + 1);
    int old = s[0x1D96];
    if (old == 0x0F) { s[0x1D0E]++; s[0x1D10]++; s[0x1D74] = 1; old = 0xFF; }
    if (old == 0x07) { s[0x1D0E]++; s[0x1D10]++; s[0x1D74] = 1; old = 0x07; }
    s[0x1D96] = (uint8_t)(old + 1);
  }
  for (int i = 0; i < ym; i++) {                                                      // 94DF Y-
    camPut16(s, 0x1D89, camGet16(s, 0x1D89) - 1);
    int old = s[0x1D96];
    if (old == 0x00) { s[0x1D0E]--; s[0x1D10]--; s[0x1D75] = 1; old = 0x10; }
    if (old == 0x08) { s[0x1D0E]--; s[0x1D10]--; s[0x1D75] = 1; old = 0x08; }
    s[0x1D96] = (uint8_t)(old - 1);
  }
}

// C0/885A align machine (state $0138): glide 1 px per frame per axis toward the lead's side until both
// phases are 0 (camera 16-aligned), at most 15 px per axis; input (88EE) is off while it runs. Armed only
// by the D8 battle entry (C0/1770). Runs 91AC and 93E1 itself.
static void camAlign885A(CtScene* sc, int onX, int onY) {
  uint8_t* s = sc->mem->s;
  if (s[0x0138] == 1) {                                       // state 1: choose the per-axis direction
    s[0x1D2E] = s[0x1D30] = s[0x1D32] = s[0x1D33] = 0;
    if (s[0x1D93] != 0) s[0x1D2E] = ((onX & 0xFF) >= 128) ? 0x10 : 0xF0;
    if (s[0x1D96] != 0) s[0x1D30] = ((onY & 0xFF) >= 136) ? 0x10 : 0xF0;
    if (s[0x1D93] == 0 && s[0x1D96] == 0) { s[0x0138] = 0; return; }   // already aligned -> done
    camSeek91AC(sc); camCommit93E1(s); s[0x0138] = 2;
  } else if (s[0x0138] == 2) {                                // state 2: glide until both phases are 0
    s[0x1D32] = s[0x1D33] = 0;
    if (s[0x1D93] == 0) s[0x1D2E] = 0;
    if (s[0x1D96] != 0) { camSeek91AC(sc); camCommit93E1(s); return; }
    s[0x1D30] = 0;
    if (s[0x1D93] != 0) { camSeek91AC(sc); camCommit93E1(s); return; }
    s[0x0138] = 0;
  } else s[0x0138] = 0;
}

// The per-frame camera pipeline (C0/881E camera tail), run first in CtSceneTick by every VM host. The field
// host has already run the collision half and passed its request via CtSceneCameraInput; headless hosts
// leave it 0, so the camera moves only by ops 0xE7/0xE6 or a seeded mid-seek. A zero request and zero
// deltas leave the scroll unchanged.
void CtSceneCameraRequestClear(CtScene* sc) { if (sc) { sc->camReqX = 0; sc->camReqY = 0; } }
void CtSceneCameraTick(CtScene* sc) {
  if (!sc) return;
  uint8_t* s = sc->mem->s;
  int camPxX = camGet16(s, 0x1D0A) * 8 + (s[0x1D93] & 7);     // the render camera = the deadzone witness base
  int camPxY = camGet16(s, 0x1D0E) * 8 + (s[0x1D96] & 7);
  int onX = sc->camHaveLead ? (sc->camLeadX - camPxX) : 128;  // lead on-screen foot (world - camPx); 128/136
  int onY = sc->camHaveLead ? (sc->camLeadY - camPxY) : 136;  //   = centre when there is no lead
  if (s[0x0138] != 0) { camAlign885A(sc, onX, onY); return; } // the align machine owns the camera
  s[0x1D2E] = (uint8_t)sc->camReqX;                           // $2E/$30 = the post-collision request (88EE+8A6D)
  s[0x1D30] = (uint8_t)sc->camReqY;
  camGate99DE(s, onX, onY);                                   // deadzone + camera-edge kill
  camSeek91AC(sc);                                            // op 0xE7 seek override + auto derive
  camCommit93E1(s);                                           // accumulate + commit (phase/coarse twins)
}

// C0/7506 (X) / C0/7553 (Y) scene-entry init: cam = clamp((arrTile-8)*16, mask), 16-aligned, with phases,
// step counters and fractions zeroed. Input is the arrival tile ($0102/$0103), not the lead pixel. Called
// by the field host on a fresh entry (a seed carries the live camera). The Y far-pin threshold is $1D1D-6
// (X uses -8), and the far pins sit one px past the scroll-reachable maxima (mx1-256 / my1-224).
static void cameraLaw7506(uint8_t* s, int arrTileX, int arrTileY) {
  int x0 = s[0x1D1A], x1 = s[0x1D1B], y0 = s[0x1D1C], y1 = s[0x1D1D];
  int caxX;
  if      (arrTileX <= x0 + 8) caxX = x0 * 2;                 // left pin  (7516/7522)
  else if (arrTileX >  x1 - 8) caxX = (x1 + 1) * 2 - 0x20;    // right pin (753E)
  else                         caxX = (arrTileX - 8) * 2;     // normal    (752E)
  int caxY;
  if      (arrTileY <= y0 + 8) caxY = y0 * 2;                 // top pin   (7563/756F)
  else if (arrTileY >  y1 - 6) caxY = (y1 + 1) * 2 - 0x1C;    // bottom pin (7587)
  else                         caxY = (arrTileY - 8) * 2;     // normal    (7577)
  // Nothing clamps the pins afterwards. In a room shorter than the screen (13 tiles) the bottom pin
  // ($1D1D+1)*2-$1C is one coarse above the top pin, and 757B-7596 stores it as is, so the camera rests 8 px
  // above the top bound. Eight location records do this (00E, 088, 08B, 089, 099, 09A, 0F9, 0FA); none makes
  // a pin negative.
  camPut16(s, 0x1D0A, caxX); camPut16(s, 0x1D0C, caxX + 0x20);
  camPut16(s, 0x1D0E, caxY); camPut16(s, 0x1D10, caxY + 0x1C);
}
void CtSceneCameraEntry(CtScene* sc, int arrTileX, int arrTileY) {
  if (!sc) return;
  uint8_t* s = sc->mem->s;
  cameraLaw7506(s, arrTileX, arrTileY);
  s[0x1D93] = s[0x1D96] = 0;                                  // phases 0 (16-aligned)
  camPut16(s, 0x1D87, 0); camPut16(s, 0x1D89, 0);             // step counters zeroed at entry
  s[0x1D3B] = s[0x1D3C] = 0;                                  // fraction accumulators zeroed
  s[0x1D00] = 0; s[0x1D01] = 0; s[0x0138] = 0;                // no seek / no align in progress
  sc->bgD2x = sc->bgD2y = sc->bgD3x = sc->bgD3y = 0;          // every layer built at this camera (C0/0A50)
  s[0x1D24] = s[0x1D25] = s[0x1D4E] = s[0x1D4F] = 0;          // op 0xE6 / auto deltas cleared
  // ...and the BG2/BG3 half: a load zeroes all six step counters and the layer-scroll setup together;
  // the map-header decode (CtSceneLayerScrollLoad) then writes only what the new map asks for.
  s[0x1D26] = s[0x1D27] = s[0x1D28] = s[0x1D29] = 0;          // op 0xE6 BG2/BG3 drift
  s[0x1D3D] = s[0x1D3E] = s[0x1D3F] = s[0x1D40] = 0;          // BG2/BG3 4.4 fraction accumulators
  s[0x1D35] = s[0x1D36] = 0;                                  // free-scroll / detached-from-camera bits
  s[0x1D37] = s[0x1D38] = s[0x1D39] = s[0x1D3A] = 0;          // BG2/BG3 own speeds
  camPut16(s, 0x1D8B, 0); camPut16(s, 0x1D8D, 0);             // BG2 step counters
  camPut16(s, 0x1D8F, 0); camPut16(s, 0x1D91, 0);             // BG3 step counters
}

// C0/A385-A4FB, the location load's layer-scroll setup from the decompressed map header at $7E:B500
// (md[0..5]). DP = $1D00. Three bytes decide whether BG2/BG3 scroll on their own:
//   A385 LDA $7EB501 / STA $50                         ; md[1]
//   A3A7 LDA $50 / BIT #20 / BNE -> LDA #04 / STA $36   ; bit $20 -> BG3 detached from the camera
//        (else STZ $36) ; A3B5 BIT #10 / BEQ / ORA #02  ; bit $10 -> BG2 detached
//   A459 LDA $36 / STA $35
//   A45D LDA $7EB502 / BEQ ->A4AD                      ; md[2] -- BG2's own speed, when non-zero:
//        AND #07 / BEQ / TAY / LDA #01 / (DEY/BEQ/ASL A/BRA) / STA $37   ; X = 1 << (code-1)
//        BIT #08 / BEQ / LDA $37 / EOR #FF / INC A / STA $37              ; bit 3 = X negative
//        AND #70 / BEQ / LSR*4 / (same) / STA $38                          ; Y code, bit 7 = Y negative
//        A4A5 LDA $35 / ORA #02 / STA $35
//   A4AD LDA $7EB503 ...                                ; md[3], the same encoding -> $39/$3A, $35 |= 4
// Speeds are signed 4.4 px per frame, consumed every frame by C0/9252/928E (camCommit93E1). Example:
// loc 0AD md[2] = $56 -> $1D37 = 20, $1D38 = 10, i.e. (+2,+1) px per frame.
// C0/74AC-74C5, run by the entry (74A6) after 7506/7553 when $0BC9 is negative: BG2's streamer window at half
// the camera, `$12 = ($0A >> 1) & FE`, `$14 = $12 + 20`, `$16 = $0E >> 1`, `$18 = $16 + 1C` (chip units).
static void layerHalfWindow74AC(uint8_t* s) {
  if (s[0x0BC9] & 0x80) {
    s[0x1D12] = (uint8_t)((s[0x1D0A] >> 1) & 0xFE); s[0x1D14] = (uint8_t)(s[0x1D12] + 0x20);
    s[0x1D16] = (uint8_t)(s[0x1D0E] >> 1);          s[0x1D18] = (uint8_t)(s[0x1D16] + 0x1C);
  }
}
void CtSceneLayerScrollLoad(CtScene* sc, uint8_t md1, uint8_t md2, uint8_t md3) {
  if (!sc) return;
  uint8_t* s = sc->mem->s;
  CtWramLayerScrollDecode(s, md1, md2, md3);
  layerHalfWindow74AC(s);                                     // decoded after the camera entry, so the window is set here
  s[0x1D94] = s[0x1D97] = 0;                                  // BG2's phases, zeroed with the rest of the $1D page
}
// The location load's C0/286C entry, after its object init: the camera law (C0/7506 / 7553) and C0/74AC's window.
// C0/7F7E's clear of the $1D page (CtSceneCameraEntry's extra) and the header decode (CtWramLayerScrollDecode,
// C0/A385-A4FB in C0/A33B) ran before the init (memLocationLoad, C0/7F95), so an init op's own store into the page
// stays (e.g. op 0x29's `TSB $1D36 / $1D35` at C0/3E87).
void CtSceneLoadCameraEntry(CtScene* sc, int arrTileX, int arrTileY) {
  if (!sc) return;
  uint8_t* s = sc->mem->s;
  cameraLaw7506(s, arrTileX, arrTileY);
  s[0x0138] = 0;                                              // outside the page, so cleared here
  sc->bgD2x = sc->bgD2y = sc->bgD3x = sc->bgD3y = 0;          // every layer built at this camera (C0/0A50)
  layerHalfWindow74AC(s);
}
// C0/A385-A4FB, the WRAM half: what the header decides, before the location's object init (C0/A33B is C0/00F4's leg).
void CtWramLayerScrollDecode(uint8_t* s, uint8_t md1, uint8_t md2, uint8_t md3) {
  if (!s) return;
  s[0x0BCA] = (md1 & 0x80) ? 0x01 : 0x00;                     // A38B BPL / LDA #01 / STA $0BCA (BG3 present)
  s[0x0BC9] = (md1 & 0x40) ? 0x80 : 0x00;                     // A397 BIT #40 / LDA #80 / STA $0BC9 (the half-camera map mode)
  s[0x1D36] = (md1 & 0x20) ? 0x04 : 0x00;                     // A3A9-A3B3
  if (md1 & 0x10) s[0x1D36] |= 0x02;                          // A3B7-A3BF
  s[0x1D35] = s[0x1D36];                                      // A459
  for (int layer = 0; layer < 2; layer++) {                   // A45D (BG2, md[2]) / A4AD (BG3, md[3])
    uint8_t b = layer ? md3 : md2;
    if (!b) continue;                                         // BEQ: the arm is skipped
    int xo = layer ? 0x1D39 : 0x1D37, yo = xo + 1;
    int xc = b & 0x07, yc = (b & 0x70) >> 4;
    if (xc) { uint8_t v = (uint8_t)(1 << (xc - 1)); if (b & 0x08) v = (uint8_t)(-v); s[xo] = v; }
    if (yc) { uint8_t v = (uint8_t)(1 << (yc - 1)); if (b & 0x80) v = (uint8_t)(-v); s[yo] = v; }
    s[0x1D35] |= (uint8_t)(layer ? 0x04 : 0x02);              // A4A5 / A4F5
  }
}

// BG2/BG3 sample offsets relative to the camera, in pixels (disasm/notes/field_bg_tilemap.md). All step counters
// start at 0 on a load. The load's tilemap build decides a layer's origin by $1D35 (C0/78EC BG2 / C0/7CB5 BG3,
// `BIT #02` / `BIT #04`): set, the map itself is uploaded from metatile (0,0), so map pixel = counter; clear, the
// 32x32 window at the entry camera, so map pixel = entry camera + counter. The HDMA scroll tables hold the step
// counters themselves (FD/C1EE channels $0F/$11). $1D35 = $1D36 (detached) OR has-own-speed (A4A5/A4F5).
// The field crops one canvas at the camera, so a map-built layer's offset is counter - camera.
void CtSceneLayerOffsets(const CtScene* sc, int* bg2x, int* bg2y, int* bg3x, int* bg3y) {
  if (!sc) return;
  CtSceneLayerOffsetsOf(sc, sc->mem->s, bg2x, bg2y, bg3x, bg3y);
}
// The same read of a page kept elsewhere: during a battle the field's $0920-$1DFF is C0/0918's copy at $7F:2000
// (`page` = that copy - $0920, so page[$1D8B] is $7F:346B), which C0/2C41 keeps scrolling.
void CtSceneLayerOffsetsOf(const CtScene* sc, const uint8_t* s, int* bg2x, int* bg2y, int* bg3x, int* bg3y) {
  if (!sc || !s) return;
  int b1x = (int16_t)camGet16(s, 0x1D87), b1y = (int16_t)camGet16(s, 0x1D89);
  int cx = camGet16(s, 0x1D0A) * 8 + (s[0x1D93] & 7), cy = camGet16(s, 0x1D0E) * 8 + (s[0x1D96] & 7);
  int det2 = s[0x1D35] & 0x02, det3 = s[0x1D35] & 0x04;
  // A camera-built BG2 in the half-camera map mode ($0BC9 bit 7) is built from C0/792F's window, `$12 = ($0A >> 1)
  // & FE` / `$16 = $0E >> 1` of the entry camera (C0/74B1), so its map pixel is that window's origin plus its counter.
  if (!det2 && (s[0x0BC9] & 0x80)) {
    int ex = (cx - b1x + sc->bgD2x) >> 3, ey = (cy - b1y + sc->bgD2y) >> 3;   // BG2's build camera in chips (the entry's
                                                                             //   or a rebuild's, see below)
    if (bg2x) *bg2x = ((ex >> 1) & 0xFE) * 8 + (int16_t)camGet16(s, 0x1D8B) - cx;
    if (bg2y) *bg2y = (ey >> 1) * 8 + (int16_t)camGet16(s, 0x1D8D) - cy;
  } else {
  // A camera-built layer whose tilemap was rebuilt at a later camera (op 0xE5's style branch: C0/78EC / 7CB5 build at
  // the live $1D0A/$1D0E and C0/74E8 / 74F7 zero its counters, ctXferReorigin) sits on its own build camera:
  // bgD2*/bgD3* = its build camera minus BG1's, 0 until such a rebuild.
  if (bg2x) *bg2x = (int16_t)camGet16(s, 0x1D8B) - (det2 ? cx : b1x - sc->bgD2x);
  if (bg2y) *bg2y = (int16_t)camGet16(s, 0x1D8D) - (det2 ? cy : b1y - sc->bgD2y);
  }
  if (bg3x) *bg3x = (int16_t)camGet16(s, 0x1D8F) - (det3 ? cx : b1x - sc->bgD3x);
  if (bg3y) *bg3y = (int16_t)camGet16(s, 0x1D91) - (det3 ? cy : b1y - sc->bgD3y);
}

// The field host passes this frame's post-collision scroll request ($1D2E/$1D30 units, signed 1/16 px) and
// the lead's start-of-frame foot (px), consumed by CtSceneCameraTick. leadValid = 0: no gate motion.
void CtSceneCameraInput(CtScene* sc, int reqX, int reqY, int leadX, int leadY, int leadValid) {
  if (!sc) return;
  sc->camReqX = reqX; sc->camReqY = reqY;
  sc->camLeadX = leadX; sc->camLeadY = leadY; sc->camHaveLead = leadValid;
}

// The field camera in pixels: camPx = $1D0A*8 + ($1D93&7) (same for Y). Entry and D8 leave the camera
// 16-aligned, so $1D93&7 == $1D87&7 == camPx mod 8 (what oam.c and battle staging read).
void CtSceneCameraPx(const CtScene* sc, int* camx, int* camy) {
  if (!sc) return;
  CtSceneCameraPxOf(sc->mem->s, camx, camy);
}
void CtSceneCameraPxOf(const uint8_t* s, int* camx, int* camy) {
  if (!s) return;
  if (camx) *camx = camGet16(s, 0x1D0A) * 8 + (s[0x1D93] & 7);
  if (camy) *camy = camGet16(s, 0x1D0E) * 8 + (s[0x1D96] & 7);
}

// ---- Message box machine, C0/1F87 (dispatch on $0129; D = $0100; disasm/notes/textbox.md,
//      field_textbox_a_press.md). Not modelled here: the C2 text engine's buffers (the host's).
//   1, 8      1FB4  decode the page (JSR C0/20B2) / INC $29 / STZ $31 / $36 := 1
//   2-4, 9-B  1FC0  INC $29 / INC $31          C  1FC5  $29 := 6 / JSR C0/2126 (typewriter, new page)
//   5         1FCD  create the window ($0126 = $0130, or by the speaker's screen half when auto), then the
//                   open ramp 1FF8: $0127 += 4 a frame up to $28, then 2045 $29 := 6 the same frame
//   6         2018  the typewriter, one character per frame; the host reveals its decoded string at that rate
//                   and reports the end (CtSceneTextboxRevealDone: 21D8 / 21C2)
//   7         201C  waiting: $0134 == FFFF until the A handler spends it (C0/1ABD); 0 advances (2058)
//   D         2032  the close ramp: four DECs, the last untested; at 0, 204D `STZ $29 / STZ $26 / TRB $54 #20`
// ---- Window wipe: op 0xFF 0x92-0x95 (C0/405D, C0/40EF, C0/40B3, C0/4145) and its frame, C0/21E1 -> C0/22D8 -> C3/01E4 ----
// (disasm/notes/field_window_wipe.md.) The op arms it and parks (C0/400C's CLC ends the visit) until the effect is over:
//   $0139 = 0:   `$0350` busy -> park; else $0351 = 0, $0350 = mode, $0352/$0354 = $7F:17C8, $0139 = 3, $013C = kind; park
//   $0139 = 3:   `$0350` still set -> park; else complete and INX INX
//   92 ($0350 = 1, $013C = 0) / 93 ($0350 = 2, $013C = 1): complete = $0139 = 0, $7F1522/23 = 0, $0BD9/DA/DB/DD = 0, TRB $28 #80
//   94 ($0350 = 1, $013C = 3) / 95 ($0350 = 2, $013C = 2): complete = STZ $19 (black), $0139 = 0
// C3:0237 counts $0351 01..1F over 31 frames; when C3:023F clears $0350, the same frame's visit completes.
static int ctWipeOp(CtScene* sc, int sub) {        // 1 = complete (advance 2), 0 = park
  Mem* m = sc->mem;
  static const uint8_t MODE[4] = { 1, 2, 1, 2 }, KIND[4] = { 0, 1, 3, 2 };
  int k = sub - 0x92;
  static int wt = -1; if (wt < 0) { const char* e = getenv("CT_WIPETRACE"); wt = (e && *e) ? 1 : 0; }   // CT_WIPETRACE
  if (wt) printf("wipe: t%d op FF %02X $0139=%02X $0350=%02X $0351=%02X $0153=%02X frz=%d ldf=%d\n", gVmTick, sub,
                 r8(m, 0x7E0139), r8(m, 0x7E0350), r8(m, 0x7E0351), r8(m, 0x7E0153),
                 sc->sndFrozenTick, sc->loadFadeIn);
  if (r8(m, 0x7E0139) == 0) {
    if (r8(m, 0x7E0350)) return 0;                                   // 408E LDA $0350 / BEQ / TYX RTS
    w8(m, 0x7E0351, 0); w8(m, 0x7E0350, MODE[k]);                     // 4095 STZ $0351 / LDA #mode / STA $0350
    w8(m, 0x7E0352, 0xC8); w8(m, 0x7E0353, 0x17); w8(m, 0x7E0354, 0x7F);   // LDX #17C8 / STX $0352 / LDA #7F / STA $0354
    w8(m, 0x7E0139, 3); w8(m, 0x7E013C, KIND[k]);                     // LDA #03 / STA $39 / LDA #kind / STA $013C
    return 0;
  }
  if (r8(m, 0x7E0139) != 3 || r8(m, 0x7E0350)) return 0;             // 4061 CMP #03 / BNE / LDA $0350 / BEQ
  if (k < 2) {                                                        // 406C (92) / 40FE (93)
    w8(m, 0x7E0139, 0); w8(m, 0x7F1522, 0); w8(m, 0x7F1523, 0);
    w8(m, 0x7E0BD9, 0); w8(m, 0x7E0BDA, 0); w8(m, 0x7E0BDB, 0); w8(m, 0x7E0BDD, 0);
    w8(m, 0x7E0128, (uint8_t)(r8(m, 0x7E0128) & ~0x80));
  } else {                                                            // 40C2 (94) / 4154 (95)
    w8(m, 0x7E0119, 0); sc->bright = 0; w8(m, 0x7E0139, 0);
  }
  if (wt) printf("wipe: t%d op FF %02X complete $0119=%02X $0128=%02X\n", gVmTick, sub, r8(m, 0x7E0119), r8(m, 0x7E0128));
  return 1;
}
// C0/00B1 JSR C0/21E1: `LDA $39 / BEQ / DEC / ASL / TAX / JSR ($21EE,X)`; entry 3 is C0/22D8, once per main-loop
// frame before the object pass (C0/00B7). While $0350 is set it arms the window shadows, steps C3/01E4 (DP $0300)
// and holds `$19 = 0F`; after, with $013C < 2 (92/93), it clears them (C0/2307).
// C3/01E4 runs natively (wipe.c CtWipeRoutine). Its seed C3/02DD reads the virtual beam ($213C = $0351, $213D = 00;
// see CLAUDE.md, the zelda3 model). field.c draws the window from the routine's tables, through the HDMA programs
// of FD/C1EE ($2128/$2129 by $013C bit 0) and the shadows C0/22D8 sets.
static void ctWipeFrame(CtScene* sc) {
  Mem* m = sc->mem;
  if (r8(m, 0x7E0139) != 3) return;
  if (r8(m, 0x7E0350)) {
    w8(m, 0x7F1522, 0x17); w8(m, 0x7F1523, 0x17);                     // 22DD
    w8(m, 0x7E0BD9, 0x88); w8(m, 0x7E0BDA, 0x88); w8(m, 0x7E0BDB, 0x88);
    w8(m, 0x7E0BDC, 0x00); w8(m, 0x7E0BDD, 0xFF);
    // JSL C3/000E -> C3/01E4 (wipe.c CtWipeRoutine): the first call's fill, every call's step through $C0:FE00 with
    // C3/02DD's seed, the tables at $7F:17C8 / $7F:18A8, $51's count and $50's clear on the 32nd call, $0386's update.
    // The registers it reads: X = 0004 (C0/21EA `DEC A / ASL A / TAX` of state 3) and the data bank 00 (--wipedump).
    int xr = CtWipeRoutine(gRomImg, gRomImgLen, m->s, CT_WIPE_FIELD_X, CT_WIPE_FIELD_DB);
    if (xr) {
      // No ROM (a harness without one): the routine cannot run. The count the op waits on is kept so the flow
      // completes; the tables are not stepped. Reported once.
      static int said = 0;
      if (!said) { said = 1; printf("wipe: C3/01E4 not run (%s, r=%d) -- the count is kept, the tables are not stepped%s", gRomImg ? "a bank it cannot address" : "no ROM image", xr, "\n"); }
      int mode = r8(m, 0x7E0350), stp = r8(m, 0x7E0351);
      if (stp == 0) {                                                 // 01FA: the fill, 00 (mode 1) or FF (mode 2)
        uint32_t base = ((uint32_t)(r8(m, 0x7E0352) | (r8(m, 0x7E0353) << 8)) + 0x01C0) & 0xFFFF;
        uint32_t bank = (r8(m, 0x7E0354) & 1) ? 0x7F0000 : 0x7E0000; // 0211 LDA $54 / LSR / BCC
        for (uint32_t i = 0; i < 0xE0; i++) w8(m, bank | ((base + i) & 0xFFFF), mode == 1 ? 0x00 : 0xFF);
      }
      stp++;                                                          // 0234 INC A / STA $51
      if (stp >= 0x20) { w8(m, 0x7E0351, 0); w8(m, 0x7E0350, 0); }    // 0237 CMP #20 / BMI / 023B STZ $51 / STZ $50
      else w8(m, 0x7E0351, (uint8_t)stp);
    }
    w8(m, 0x7E0128, (uint8_t)(r8(m, 0x7E0128) | 0x80));               // 22FE LDA #80 / TSB $28
    w8(m, 0x7E0119, 0x0F); sc->bright = 0x0F;                         // 2302 LDA #0F / STA $19
  } else if (r8(m, 0x7E013C) < 2) {                                   // 2307 LDA $3C / CMP #02 / BCS
    w8(m, 0x7F1522, 0); w8(m, 0x7F1523, 0);
    w8(m, 0x7E0BD9, 0); w8(m, 0x7E0BDA, 0); w8(m, 0x7E0BDB, 0); w8(m, 0x7E0BDD, 0);
    w8(m, 0x7E0128, (uint8_t)(r8(m, 0x7E0128) & ~0x80));
  }
}

// ---- Iris: C0/21E1's entries 1 and 2 (disasm/notes/field_iris_window.md) ----
// Op 0xFF 90 (C0/4036) stores the centre and the target radius ($013A/$013B/$013D), zeroes $013C and sets $0139 = 1; op
// 0xFF 91 (C0/4055) sets $0139 = 2. Every main-loop frame C0/21E1 dispatches on $0139 (table C0/21EE: 220E, 2269, 22D8, 232A):
// the open steps the radius $013C up by one a frame until it reaches $013D, the close down to 0, and each step rebuilds
// the window 1 table through C3/0077 into the buffer $0153 names ($7F:17C8 set, $7F:1AC8 clear), which HDMA channel 5
// feeds to WH0/WH1; the frame shows the buffer written in that frame. W12SEL $0BD9 = $22 (BG1, BG2); TMW $7F:1522 = $17.

// C3/0077 (entered through `JSL C3/0008` with A = the argument; both field callers load #00). DP = $0300: $0350 the
// centre X, $0352 the centre Y, $0354 the radius (their high bytes zeroed first), $0356/$0358 the table's address and
// bank. A midpoint circle writes each line's (left, right) pair for the top half, the bottom half is copied from it
// mirrored, and the lines above and below are filled with the empty pair (01, 00) by an overlapping MVN. Ported per
// instruction with the carry: several ADC/SBC have no CLC/SEC, which makes the top rows' right edge one pixel wider.
static uint16_t irisD16(Mem* m, int o) { return (uint16_t)r16(m, 0x7E0300u + (uint32_t)o); }
static void irisW16(Mem* m, int o, uint16_t v) { w16(m, 0x7E0300u + (uint32_t)o, v); }
static void irisMvn(Mem* m, uint32_t bank, uint16_t src, uint16_t dst, uint16_t cnt) {   // MVN: A+1 bytes, ascending
  for (uint32_t k = 0; k <= cnt; k++)
    w8(m, bank | (uint16_t)(dst + k), r8(m, bank | (uint16_t)(src + k)));
}
static void irisFill(Mem* m, uint32_t bank, uint16_t x, uint16_t n) {        // C3/01CA: X = start, $F0 = count - 1
  w16(m, bank | x, 0x0001);                                                  // 01CD LDA #0001 / STA $0000,X
  irisMvn(m, bank, x, (uint16_t)(x + 2), n);                                 // 01CA TXY / INY / INY ... MVN
}
static void ctIrisCircle(Mem* m, uint8_t arg) {
  w8(m, 0x7E0351, 0); w8(m, 0x7E0353, 0); w8(m, 0x7E0355, 0);              // 0089-008D STZ $51 / $53 / $55
  uint32_t bank = (r8(m, 0x7E0358) & 1) ? 0x7F0000u : 0x7E0000u;           // 008F LDA $58 / LSR / BCS -> DB 7F, else 7E
  uint16_t base = irisD16(m, 0x56), cx = irisD16(m, 0x50), cy = irisD16(m, 0x52), r = irisD16(m, 0x54);
  if (r == 0) {                                                              // 009E LDA $54 / BNE
    irisW16(m, 0xF0, 0x01C0); irisFill(m, bank, base, 0x01C0);              // 00A2 PLA / LDA #01C0 / JSR C3/01C6
    return;
  }
  uint32_t A, C;
  uint16_t X = r, Y = 0;                                                     // 00AC TAX
  C = (r >> 15) & 1; A = (uint16_t)(r << 1);                                 // 00AD ASL A
  irisW16(m, 0xF2, (uint16_t)A);                                             // 00AE STA $F2
  { uint32_t t = 3u - A - (1u - C); C = (t & 0x10000) ? 0 : 1; A = t & 0xFFFF; }   // 00B0 LDA #0003 / SBC $F2
  irisW16(m, 0xF2, (uint16_t)A);
  A = (uint16_t)(cy << 1); C = (cy >> 15) & 1;                               // 00BA LDA $52 / ASL A
  { uint32_t t = A + base; C = t >> 16; A = t & 0xFFFF; }                   // 00BD CLC / ADC $56
  irisW16(m, 0xF4, (uint16_t)A);                                             // 00C0 STA $F4
  uint16_t pushedF4 = (uint16_t)A;                                           // 00C2 PHA
  C = A & 1; A >>= 1; irisW16(m, 0xF6, (uint16_t)A);                         // 00C3 LSR A / STA $F6
  for (;;) {
    irisW16(m, 0xF0, X);                                                     // 00C6 STX $F0
    { uint32_t t = (uint32_t)irisD16(m, 0xF6) - X - (1u - C); C = (t & 0x10000) ? 0 : 1; A = t & 0xFFFF; }   // LDA $F6 / SBC $F0
    C = (A >> 15) & 1; A = (A << 1) & 0xFFFF;                                // 00CC ASL A
    uint16_t xa = (uint16_t)A;                                               // 00CD TAX
    A = ((uint16_t)~Y + 1) & 0xFFFF;                                         // 00CE TYA / EOR #FFFF / INC A
    { uint32_t t = A + cx + C; C = t >> 16; A = t & 0xFFFF; }                // 00D3 ADC $50
    if (A & 0x8000) A = 0;                                                   // 00D5 BPL / LDA #0000
    w16(m, bank | xa, (int)A);                                               // 00DA STA $0000,X
    { uint32_t t = (uint32_t)Y + cx + C; C = t >> 16; A = t & 0xFFFF; }      // 00DD TYA / ADC $50
    C = A >= 0x00FF; if (!(((A - 0x00FF) & 0xFFFF) & 0x8000)) A = 0x00FF;    // 00E0 CMP #00FF / BMI / LDA #00FF
    w8(m, bank | (uint16_t)(xa + 1), (int)(A & 0xFF));                       // 00EA STA $0001,X (8-bit)
    X = irisD16(m, 0xF0);                                                    // 00EF LDX $F0
    irisW16(m, 0xF0, Y);                                                     // 00F1 STY $F0
    Y = (uint16_t)(irisD16(m, 0xF4) - 2); irisW16(m, 0xF4, Y);              // 00F3 LDY $F4 / DEY / DEY / STY $F4
    A = ((uint16_t)~X + 1) & 0xFFFF;                                         // 00F9 TXA / EOR #FFFF / INC A
    { uint32_t t = A + cx + C; C = t >> 16; A = t & 0xFFFF; }                // 00FE ADC $50
    if (A & 0x8000) A = 0;                                                   // 0100 BPL / LDA #0000
    w16(m, bank | Y, (int)A);                                                // 0105 STA $0000,Y
    { uint32_t t = (uint32_t)X + cx; C = t >> 16; A = t & 0xFFFF; }          // 0108 TXA / CLC / ADC $50
    C = A >= 0x00FF; if (!(((A - 0x00FF) & 0xFFFF) & 0x8000)) A = 0x00FF;    // 010C CMP #00FF / BMI / LDA #00FF
    w8(m, bank | (uint16_t)(Y + 1), (int)(A & 0xFF));                        // 0116 STA $0001,Y
    Y = irisD16(m, 0xF0);                                                    // 011B LDY $F0
    uint16_t f2 = irisD16(m, 0xF2);
    if (f2 & 0x8000) {                                                       // 011D LDA $F2 / BPL ->0129
      A = Y; C = (A >> 15) & 1; A = (A << 1) & 0xFFFF; C = (A >> 15) & 1; A = (A << 1) & 0xFFFF;   // TYA / ASL / ASL
      { uint32_t t = A + 6 + C; C = t >> 16; A = t & 0xFFFF; }               // 0124 ADC #0006
    } else {
      A = Y; irisW16(m, 0xF0, X); X = (uint16_t)(X - 1);                     // 0129 TYA / STX $F0 / DEX
      { uint32_t t = A - (uint32_t)irisD16(m, 0xF0) - (1u - C); C = (t & 0x10000) ? 0 : 1; A = t & 0xFFFF; }   // SBC $F0
      C = (A >> 15) & 1; A = (A << 1) & 0xFFFF; C = (A >> 15) & 1; A = (A << 1) & 0xFFFF;   // ASL / ASL
      { uint32_t t = A + 0x0A + C; C = t >> 16; A = t & 0xFFFF; }            // 0131 ADC #000A
    }
    { uint32_t t = A + f2 + C; C = t >> 16; A = t & 0xFFFF; }                // 0134 ADC $F2
    irisW16(m, 0xF2, (uint16_t)A);                                           // 0136 STA $F2
    irisW16(m, 0xF0, Y); Y = (uint16_t)(Y + 1);                              // 0138 STY $F0 / INY
    uint16_t d = (uint16_t)(X - irisD16(m, 0xF0)); C = X >= irisD16(m, 0xF0);   // 013B CPX $F0
    if (d & 0x8000) break;                                                   // 013D BPL ->00C6
  }
  Y = pushedF4; X = Y;                                                       // 013F PLY / TYX
  irisW16(m, 0xF0, (uint16_t)(r + 1));                                       // 0141 LDA $54 / INC A / STA $F0
  irisW16(m, 0xF2, arg);                                                     // 0146 PLA / STA $F2
  uint16_t a2;
  if ((arg & 0x7F) == 0) {                                                   // 0149 AND #007F / BEQ ->0177
    do {                                                                     // 0177 DEX / DEX / LDA $0000,X / STA $0000,Y
      X = (uint16_t)(X - 2); w16(m, bank | Y, r16(m, bank | X)); Y = (uint16_t)(Y + 2);
      irisW16(m, 0xF0, (uint16_t)(irisD16(m, 0xF0) - 1));
    } while (irisD16(m, 0xF0));
    if (arg & 0x80) return;                                                  // 0185 LDA $F2 / BIT #0080 / BNE
    a2 = r;                                                                  // 018C LDA $54
  } else if ((arg & 0x7F) == 1) {                                            // 014E CMP #0001 / BNE -> exit
    uint16_t f0 = (uint16_t)(irisD16(m, 0xF0) >> 1); irisW16(m, 0xF0, f0);   // 0153 LSR $F0
    if ((f0 & 0x8000) || f0 == 0) return;                                    // 0155 LDA $F0 / BMI / BEQ
    do {                                                                     // 015B DEX x4 / LDA / STA / INY x2
      X = (uint16_t)(X - 4); w16(m, bank | Y, r16(m, bank | X)); Y = (uint16_t)(Y + 2);
      irisW16(m, 0xF0, (uint16_t)(irisD16(m, 0xF0) - 1));
    } while (irisD16(m, 0xF0));
    if (arg & 0x80) return;                                                  // 016B LDA $F2 / BIT #0080 / BNE
    a2 = (uint16_t)(r >> 1);                                                 // 0172 LDA $54 / LSR A
  } else return;
  a2 = (uint16_t)(a2 + cy);                                                  // 018E CLC / ADC $52
  if (((uint16_t)(a2 - 0x00DF) & 0x8000)) {                                  // 0191 CMP #00DF / BPL ->01AB
    irisW16(m, 0xF0, a2);                                                    // 0196 STA $F0
    uint16_t xs = (uint16_t)((a2 << 1) + base);                              // 0198 ASL A / CLC / ADC $56 / TAX
    uint16_t n = (uint16_t)((uint16_t)(0x00E0 - a2 - 1) * 2 - 1);            // 019D LDA #00E0 / SEC / SBC $F0 / DEC / ASL / DEC
    irisW16(m, 0xF0, n); irisFill(m, bank, xs, n);                           // 01A6 STA $F0 / JSR C3/01CA
  }
  uint16_t top = (uint16_t)(cy - r);                                         // 01AB LDA $52 / SEC / SBC $54
  if (!(top & 0x8000) && top != 0 && top != 1) {                             // 01B0 CMP #0001 / BMI / BEQ -> exit
    uint16_t n = (uint16_t)((uint16_t)(top - 1) * 2 - 1);                    // 01B7 DEC / ASL / DEC
    irisW16(m, 0xF0, n); irisFill(m, bank, base, n);                         // 01BA JSR C3/01C6 (STA $F0 / LDX $56)
  }
}
// C3/0077 on a flat 128 KB image, for ct.exe --sweepiris.
void CtIrisCircleImage(uint8_t* w128, uint8_t arg) {
  if (w128) ctIrisCircle((Mem*)w128, arg);
}
// C0/2214-225F (the open) and C0/226F-22BA (the close): the window's shadows, the circle's parameters from $013A/$013B/
// $013C, `TSB $28` #20 (HDMA channel 5 on), the buffer by $0153, and `LDA #00 / JSL C3/0008`.
static void ctIrisBuild(Mem* m) {
  w8(m, 0x7F1522, 0x17); w8(m, 0x7F1523, 0x17);
  w8(m, 0x7E0BD9, 0x22); w8(m, 0x7E0BDA, 0x00); w8(m, 0x7E0BDB, 0x00);
  w8(m, 0x7E0350, r8(m, 0x7E013A)); w8(m, 0x7E0352, r8(m, 0x7E013B)); w8(m, 0x7E0354, r8(m, 0x7E013C));
  w8(m, 0x7E0128, r8(m, 0x7E0128) | 0x20);
  uint16_t buf = r8(m, 0x7E0153) ? 0x17C8 : 0x1AC8;                          // 223E LDA $53 / BEQ
  w16(m, 0x7E0356, buf); w8(m, 0x7E0358, 0x7F);
  ctIrisCircle(m, 0x00);
}
static void ctIrisStep(Mem* m) {
  int st = r8(m, 0x7E0139);
  if (st == 1) {                                                             // C0/220E
    if (r8(m, 0x7E013C) >= r8(m, 0x7E013D)) {                                // 220E LDA $3C / CMP $3D / BCS ->2263
      w8(m, 0x7E0139, 0); w8(m, 0x7E013C, r8(m, 0x7E013C) - 1);              // 2263 STZ $39 / DEC $3C / BRA ->2214
    }
    ctIrisBuild(m);
    w8(m, 0x7E013C, r8(m, 0x7E013C) + 1);                                    // 2260 INC $3C
  } else if (st == 2) {                                                      // C0/2269
    if (r8(m, 0x7E013C) == 0) {                                              // 2269 LDA $3C / BEQ ->22BC
      w8(m, 0x7E0139, 0); w8(m, 0x7F1522, 0); w8(m, 0x7F1523, 0);            // 22BC STZ $39 / ...
      w8(m, 0x7E0BD9, 0); w8(m, 0x7E0BDA, 0); w8(m, 0x7E0BDB, 0);
      w8(m, 0x7E0128, r8(m, 0x7E0128) & ~0x20);                              // 22D3 LDA #20 / TRB $28
    } else {
      w8(m, 0x7E013C, r8(m, 0x7E013C) - 1);                                  // 226D DEC $3C
      ctIrisBuild(m);
    }
  }
}
static void ctIrisFrame(CtScene* sc) {
  ctIrisStep(sc->mem);
  sc->irisState = r8(sc->mem, 0x7E0139);                                     // the C mirror the host reads
}

// ---- The circle as the colour window: C0/21E1's entries 4 and 5 (`FF 97` / `FF 98`) ----
// C0/232A (entry 4) is the iris's opening (C0/220E) with the window made the colour window: 2330-234A TMW $7F:1522/1523 =
// 17, W12SEL / W34SEL $0BD9 / $0BDA = 00, WOBJSEL $0BDB = 20 (window 1, not inverted), CGWSEL $0BDE = 12 (math only inside
// it, the sub screen added); the centre $3A / $3B and the radius $3C into $0350 / $0352 / $0354, `TSB $28` #20 (HDMA channel
// 5 on), the buffer by $53 ($7F:17C8 set, $7F:1AC8 clear) into $0356 / $0358, and `LDA #01 / JSL C3/0008`: C3/0077 with
// argument 1: each pair of lines copied once, the circle half as tall (an ellipse). C0/238A (entry 5) steps the radius
// down first and ends, at 0, in C0/23E2 (ctLineClose). The fixed colour is the one op 0xF1 left in $0121 (the scripts put
// `F1 9F 00` after `FF 97`: blue at 31).
static void ctColCircleBuild(Mem* m) {
  w8(m, 0x7F1522, 0x17); w8(m, 0x7F1523, 0x17);                             // 2330-2336
  w8(m, 0x7E0BD9, 0x00); w8(m, 0x7E0BDA, 0x00);                             // 233A-233F
  w8(m, 0x7E0BDB, 0x20); w8(m, 0x7E0BDE, 0x12);                             // 2342-2349
  w8(m, 0x7E0350, r8(m, 0x7E013A)); w8(m, 0x7E0352, r8(m, 0x7E013B)); w8(m, 0x7E0354, r8(m, 0x7E013C));   // 234C-2358
  w8(m, 0x7E0128, r8(m, 0x7E0128) | 0x20);                                  // 235B TSB $28
  uint16_t buf = r8(m, 0x7E0153) ? 0x17C8 : 0x1AC8;                          // 235F LDA $53 / BEQ
  w16(m, 0x7E0356, buf); w8(m, 0x7E0358, 0x7F);
  ctIrisCircle(m, 0x01);                                                     // 237B LDA #01 / JSL C3/0008
}
static void ctLineClose(Mem* m);   // C0/23E2 (below, with the quad)
static void ctColCircleFrame(CtScene* sc) {
  Mem* m = sc->mem;
  int st = r8(m, 0x7E0139);
  if (st == 4) {                                                             // C0/232A
    if (r8(m, 0x7E013C) >= r8(m, 0x7E013D)) {                                // 232A LDA $3C / CMP $3D / BCS ->2384
      w8(m, 0x7E0139, 0); w8(m, 0x7E013C, r8(m, 0x7E013C) - 1);              // 2384 STZ $39 / DEC $3C / BRA ->2330
    }
    ctColCircleBuild(m);
    w8(m, 0x7E013C, r8(m, 0x7E013C) + 1);                                    // 2381 INC $3C
  } else if (st == 5) {                                                      // C0/238A
    if (r8(m, 0x7E013C) == 0) ctLineClose(m);                                // 238A LDA $3C / BEQ ->23E2
    else { w8(m, 0x7E013C, r8(m, 0x7E013C) - 1); ctColCircleBuild(m); }      // 238E DEC $3C, 2390..
  }
}

// ---- Fixed circles: C0/21E1's entries 7 / 8 (`FF 99`) and 9 / 0A (`FF 9A`) ----
// Each is two frames of set-up (`LDA $39 / CMP #07|#09 / BNE / INC $39`, else `STZ $39`): the same routine runs at 7 and
// at 8 (9 and 0A), so the circle lands in both buffers of the $53 pair, and the window then stays (nothing in them
// clears $28 bit 5; a later `FF 9C` -> C0/23E2 does). A circle round (7C, 06), above the picture's top, through C3/0077
// with argument 0 into the buffer $53 picks.
//   C0/2514 (7/8): TMW / TSW $7F:1522/1523 = 17 / 00, W12SEL $0BD9 = 00, WOBJSEL $0BDB = 00, W34SEL $0BDA = 03 (BG3 in
//     window 1, inverted: BG3 kept only inside it), CGWSEL $0BDE = 22 (math prevented inside, the sub screen added), the
//     radius 29, `TSB $28` #20, and the words $1D8D = 0100, $1D8F = F01D, $1D91 = 01C0 (DP $1D00's, C0/75E9's).
//   C0/2589 (9/0A): TMW / TSW 17 / 00, W12SEL 20 (BG2 in window 1), WOBJSEL 00, W34SEL 02 (BG3 in window 1), CGWSEL 22,
//     CGADSUB $0BDF = $0BE0 = 04 (BG3), TM $0BD7 = $7F:1520 = 04, TS $0BD8 = $7F:1521 = 13, $1D8D = 0100, the radius 27.
static void ctFixedCircleFrame(CtScene* sc) {
  Mem* m = sc->mem;
  int st = r8(m, 0x7E0139), nine = st >= 9;
  w8(m, 0x7F1522, 0x17); w8(m, 0x7F1523, 0x00);                             // 2514-251C / 2589-2591
  if (!nine) {
    w8(m, 0x7E0BD9, 0x00); w8(m, 0x7E0BDB, 0x00); w8(m, 0x7E0BDA, 0x03);     // 2520-252A
    w8(m, 0x7E0BDE, 0x22);                                                   // 252D
    w8(m, 0x7E0350, 0x7C); w8(m, 0x7E0352, 0x06); w8(m, 0x7E0354, 0x29);     // 2532-253E
    w8(m, 0x7E0128, r8(m, 0x7E0128) | 0x20);                                // 2541 TSB $28
    w16(m, 0x7E1D8D, 0x0100); w16(m, 0x7E1D8F, 0xF01D); w16(m, 0x7E1D91, 0x01C0);   // 2545-2556 (16-bit)
  } else {
    w8(m, 0x7E0BD9, 0x20); w8(m, 0x7E0BDB, 0x00); w8(m, 0x7E0BDA, 0x02);     // 2595-25A1
    w8(m, 0x7E0BDE, 0x22);                                                   // 25A4
    w8(m, 0x7E0BDF, 0x04); w8(m, 0x7E0BE0, 0x04);                            // 25A9-25AE
    w8(m, 0x7E0BD7, 0x04); w8(m, 0x7F1520, 0x04);                            // 25B1-25B6
    w8(m, 0x7E0BD8, 0x13); w8(m, 0x7F1521, 0x13);                            // 25BA-25BF
    w16(m, 0x7E1D8D, 0x0100);                                                // 25C3-25C8 (16-bit)
    w8(m, 0x7E0350, 0x7C); w8(m, 0x7E0352, 0x06); w8(m, 0x7E0354, 0x27);     // 25CD-25D9
    w8(m, 0x7E0128, r8(m, 0x7E0128) | 0x20);                                // 25DC TSB $28
  }
  uint16_t buf = r8(m, 0x7E0153) ? 0x17C8 : 0x1AC8;                          // 255B / 25E0 LDA $53 / BEQ
  w16(m, 0x7E0356, buf); w8(m, 0x7E0358, 0x7F);
  ctIrisCircle(m, 0x00);                                                     // 2577 / 25FC LDA #00 / JSL C3/0008
  if (st == 7 || st == 9) w8(m, 0x7E0139, (uint8_t)(st + 1));                // 257D / 2602 CMP #07|#09 / BNE / INC $39
  else w8(m, 0x7E0139, 0);                                                   // 2586 / 260B STZ $39
}

// ---- Quad window: C0/21E1's entries 6 and 0E (disasm/notes/field_quad_window.md) ----
// Op 0xFE (C0/3F26, move.c CtVmScreenFxOp) seeds eight 8.8 channels at $7F:1CE9.. (deltas at $7F:1CF9.., countdown $7F:1CE8)
// and sets $0139 = 6; `FF 9C` (C0/41C4) sets it to 0E. Entry 6 (C0/2409) is one frame of the line: the channels' high bytes
// become four vertices at $0360-$0367, C3/0EFA fills window 1's table with the quad between them, and the channels step.
// The window is the colour window (WOBJSEL $0BDB = $20, CGWSEL $0BDE = $12: math only inside it) and the colour is the
// fixed one op 0xF1 put in $0121 (FD/C6F6 copies $0BE0 / $0121 into channel 6's CGADSUB / COLDATA rows); e.g. `F1 7F 00`
// adds (31, 31, 0), the tutorial's yellow line.

// C3/0EFA (`JSL C3/0011`, DP $0300): the vertices are ($60,$61) ($62,$63) ($64,$65) ($66,$67); the walk starts at the one
// with the least Y (a tie goes to the later one: `CMP / BCC`) and rasterizes the four edges in order through C3/1025, each
// writing one byte a line into the (left, right) pairs of the table [$50]: the edges going down into the first byte, and
// from the first edge going up (`INC $50`, $6A = its top) into the second. Then line 0 takes the empty pair 01 00, an MVN
// carries it from the bottom line $6A to line $FF and another from line 0 down over line $68, so the top vertex's own line
// is emptied too. The loops keep the carry (C3/1076 / 109F enter their ADC on the borrow of the SBC before); checked by
// ct.exe --sweeppoly.
static uint16_t polyD16(Mem* m, int o) { return (uint16_t)r16(m, 0x7E0300u + (uint32_t)o); }
static void polyW16(Mem* m, int o, uint16_t v) { w16(m, 0x7E0300u + (uint32_t)o, v); }
static uint8_t polyD8(Mem* m, int o) { return (uint8_t)r8(m, 0x7E0300u + (uint32_t)o); }
static void polyW8(Mem* m, int o, uint8_t v) { w8(m, 0x7E0300u + (uint32_t)o, v); }
static uint32_t polyLong(Mem* m, uint16_t y) {                              // [$50],Y / DB=$52:$0000,Y
  return (((uint32_t)polyD8(m, 0x52) << 16) | polyD16(m, 0x50)) + y;
}
static void ctPolyEdge(Mem* m) {                                             // C3/1025
  polyW8(m, 0x54, 0); polyW8(m, 0x58, 0); polyW8(m, 0x56, 0); polyW8(m, 0x5A, 0); polyW8(m, 0xFB, 0);   // 1032-103A STZ
  uint8_t ya = polyD8(m, 0x55), yb = polyD8(m, 0x59);
  uint8_t a8 = (uint8_t)(yb - ya);                                           // 103C LDA $59 / SEC / SBC $55
  if (a8 == 0) {                                                             // 1041 BEQ ->1001: a flat edge
    uint16_t y = (uint16_t)((polyD16(m, 0x55) & 0x00FF) << 1);               // 1003 LDA $55 / AND #00FF / ASL / TAY
    uint8_t xa = polyD8(m, 0x53), xb = polyD8(m, 0x57), second;
    if ((uint8_t)(xa - xb) & 0x80) { w8(m, polyLong(m, y), xa); second = xb; }   // 100C CMP $57 / BMI ->101A: STA [$50],Y
    else { w8(m, polyLong(m, y), xb); second = xa; }                         // 1012 LDA $57 / STA [$50],Y / LDA $53
    w8(m, polyLong(m, (uint16_t)(y + 2)), second);                           // 101E INY / INY / STA [$50],Y
    return;                                                                  // 1022 JMP C3/10BA
  }
  if (yb < ya) {                                                             // 1043 BCS ->1059 not taken: swap the ends
    uint16_t t = polyD16(m, 0x57), u = polyD16(m, 0x53);                     // 1045 LDY $57 / LDX $53 / STY $53 / STX $57
    polyW16(m, 0x53, t); polyW16(m, 0x57, u);
    t = polyD16(m, 0x59); u = polyD16(m, 0x55);                              // 104D LDY $59 / LDX $55 / STY $55 / STX $59
    polyW16(m, 0x55, t); polyW16(m, 0x59, u);
    a8 = (uint8_t)((u & 0xFF) - polyD8(m, 0x55));                            // 1055 TXA / SEC / SBC $55
  }
  polyW8(m, 0xFA, a8); polyW8(m, 0xF2, a8);                                  // 1059 STA $FA / STA $F2
  uint16_t Y = (uint16_t)((polyD16(m, 0x55) << 1) + polyD16(m, 0x50));       // 105F LDA $55 / ASL / CLC / ADC $50 / TAY
  uint16_t X = polyD16(m, 0x53);                                             // 1066 LDX $53
  uint32_t A, C;
  { uint32_t t = (uint32_t)polyD16(m, 0x57) - polyD16(m, 0x53); C = (t & 0x10000) ? 0 : 1; A = t & 0xFFFF; }   // 1068 SEC / SBC
  uint16_t fa = polyD16(m, 0xFA);
  int left = (A & 0x8000) != 0;                                              // 106D BMI ->1093
  if (!left) {
    polyW16(m, 0xF8, (uint16_t)A);                                           // 106F STA $F8
    { uint32_t t = A - fa; C = (t & 0x10000) ? 0 : 1; A = t & 0xFFFF; }      // 1071 SEC / SBC $FA
  } else {
    A = ((A ^ 0xFFFF) + 1) & 0xFFFF;                                         // 1093 EOR #FFFF / INC A (C kept: the borrow)
    polyW16(m, 0xF8, (uint16_t)A);                                           // 1097 STA $F8
    { uint32_t t = (uint32_t)fa - A - (1u - C); C = (t & 0x10000) ? 0 : 1; A = t & 0xFFFF; }   // 1099 LDA $FA / SBC $F8
  }
  polyW16(m, 0xFC, (uint16_t)A);                                             // 1074 / 109D STA $FC
  for (;;) {
    A = polyD16(m, 0xFC);                                                    // 1076 / 109F LDA $FC
    if (!(A & 0x8000)) {                                                     // BMI
      do {
        X = (uint16_t)(left ? X - 1 : X + 1);                                // 107A INX / 10A3 DEX
        { uint32_t t = A - fa - (1u - C); C = (t & 0x10000) ? 0 : 1; A = t & 0xFFFF; }   // SBC $FA
      } while (!(A & 0x8000));                                               // BPL
    }
    { uint32_t t = A + polyD16(m, 0xF8) + C; C = t >> 16; A = t & 0xFFFF; }  // 107F / 10A8 ADC $F8
    polyW16(m, 0xFC, (uint16_t)A);                                           // STA $FC
    w8(m, ((uint32_t)polyD8(m, 0x52) << 16) + Y, X & 0xFF);                  // TXA / SEP #20 / STA $0000,Y (DB = $52)
    Y = (uint16_t)(Y + 2);                                                   // INY / INY
    uint8_t n = (uint8_t)(polyD8(m, 0xF2) - 1); polyW8(m, 0xF2, n);          // DEC $F2
    if (!n) break;                                                           // REP #20 / BNE
  }
}
static void ctPolyQuad(Mem* m) {                                             // C3/0EFA
  polyW8(m, 0x69, 0);                                                        // 0F03 STZ $69
  uint16_t Y = 0x0F35; polyW16(m, 0xF6, Y);                                  // 0F05 LDY #0F35 / STY $F6
  uint8_t A = polyD8(m, 0x61);                                               // 0F0A LDA $61
  if (A >= polyD8(m, 0x63)) { Y = 0x0F48; A = polyD8(m, 0x63); }            // 0F0C CMP $63 / BCC
  if (A >= polyD8(m, 0x65)) { Y = 0x0F5B; A = polyD8(m, 0x65); }            // 0F15 CMP $65 / BCC
  if (A >= polyD8(m, 0x67)) { Y = 0x0F6E; A = polyD8(m, 0x67); }            // 0F1E CMP $67 / BCC
  polyW8(m, 0x68, A);                                                        // 0F27 STA $68
  polyW16(m, 0xF4, 4); polyW16(m, 0xF0, Y);                                  // 0F2B LDA #0004 / STA $F4 / STY $F0
  int e = Y == 0x0F35 ? 0 : Y == 0x0F48 ? 1 : Y == 0x0F5B ? 2 : 3;           // 0F32 JMP ($03F0)
  for (;;) {
    int vs = 0x60 + 2 * e, vn = e == 3 ? 0x60 : vs + 2;                      // 0F35 / 0F48 / 0F5B / 0F6E (16-bit loads)
    polyW16(m, 0x53, polyD16(m, vs)); polyW16(m, 0x55, polyD16(m, vs + 1));
    polyW16(m, 0x57, polyD16(m, vn)); polyW16(m, 0x59, polyD16(m, vn + 1));
    polyW16(m, 0x55, polyD16(m, 0x55) & 0x00FF);                            // 0F83 LDA #FF00 / TRB $55 / TRB $59
    polyW16(m, 0x59, polyD16(m, 0x59) & 0x00FF);
    if ((uint16_t)(polyD16(m, 0x59) - polyD16(m, 0x55)) & 0x8000) {          // 0F8A LDA $59 / CMP $55 / BPL
      if (polyD16(m, 0xF6)) {                                                // 0F90 LDA $F6 / BEQ
        polyW16(m, 0x50, (uint16_t)(polyD16(m, 0x50) + 1));                  // 0F94 INC $50: the second byte
        polyW16(m, 0x6A, polyD16(m, 0x55)); polyW16(m, 0xF6, 0);             // 0F96 LDA $55 / STA $6A / STZ $F6
      }
    }
    ctPolyEdge(m);                                                           // 0F9C JSL C3/1025
    uint16_t n = (uint16_t)(polyD16(m, 0xF4) - 1); polyW16(m, 0xF4, n);      // 0FA0 DEC $F4
    if (!n) break;                                                           // BEQ ->0FA5
    e = (e + 1) & 3;                                                         // RTS -> the next edge (0F81 BRA ->0F35)
  }
  int hi = polyD8(m, 0x52) & 1;                                              // 0FA6 LDA $52 / LSR A / PHP
  polyW16(m, 0x50, (uint16_t)(polyD16(m, 0x50) - 1));                        // 0FAA DEC $50
  uint16_t f0 = (uint16_t)((((uint16_t)(0x0100 - polyD16(m, 0x6A) - 1)) << 1) - 1);   // 0FAC LDA #0100 / CLC / SBC $6A / ASL / DEC
  polyW16(m, 0xF0, f0);                                                      // 0FB4 STA $F0
  uint16_t X = (uint16_t)((polyD16(m, 0x6A) << 1) + polyD16(m, 0x50));       // 0FB6 LDA $6A / ASL / CLC / ADC $50 / TAY / TAX
  w16(m, polyLong(m, 0), 0x0001);                                            // 0FBE LDA #0001 / STA [$50]
  uint32_t bank = hi ? 0x7F0000u : 0x7E0000u;                                // 0FC3 PLP / BCC ->0FE3 (the $7E twin)
  w16(m, bank | X, 0x0001);                                                  // 0FC6 / 0FE3 STA $7x0000,X
  if (!f0) return;                                                           // 0FCC LDA $F0 / BEQ
  irisMvn(m, bank, X, (uint16_t)(X + 2), f0);                                // INY / INY / MVN
  uint16_t b = polyD16(m, 0x50);                                             // 0FD3 LDX $50 / TXY / INY / INY
  uint16_t top = polyD16(m, 0x68);                                           // 0FD8 LDA $68 / BEQ
  if (!top) return;
  irisMvn(m, bank, b, (uint16_t)(b + 2), (uint16_t)((top << 1) - 1));        // 0FDC ASL / DEC / MVN
}
// C3/0EFA on a flat 128 KB image, for ct.exe --sweeppoly.
void CtPolyQuadImage(uint8_t* w128) {
  if (w128) ctPolyQuad((Mem*)w128);
}
// C0/2409: entry 6, one frame of the line.
static void ctLineStep(Mem* m) {
  int cnt = r8(m, 0x7F1CE8);
  if (cnt) w8(m, 0x7F1CE8, cnt - 1);                                         // 2409 LDA $7F1CE8 / BEQ / DEC A / STA
  else {                                                                     // 2416 STZ $39, the X deltas zeroed
    w8(m, 0x7E0139, 0);
    w16(m, 0x7F1CF9, 0); w16(m, 0x7F1CFD, 0); w16(m, 0x7F1D01, 0); w16(m, 0x7F1D05, 0);
  }
  w8(m, 0x7F1522, 0x17); w8(m, 0x7F1523, 0x17);                             // 242F TMW / TSW
  w8(m, 0x7E0BD9, 0x00); w8(m, 0x7E0BDA, 0x00); w8(m, 0x7E0BDB, 0x20);       // 2439 W12SEL / W34SEL / WOBJSEL
  w8(m, 0x7E0BDE, 0x12);                                                     // 2446 CGWSEL
  for (int k = 0; k < 8; k++)                                                // 244B-2480: channel k's high byte -> $0360+k
    w8(m, 0x7E0360u + (uint32_t)k, r8(m, 0x7F1CEAu + 2u * (uint32_t)k));
  w8(m, 0x7E0128, r8(m, 0x7E0128) | 0x20);                                   // 2483 TSB $28 (HDMA channel 5)
  w16(m, 0x7E0350, r8(m, 0x7E0153) ? 0x17C8 : 0x1AC8); w8(m, 0x7E0352, 0x7F);   // 2487 LDA $53 / BEQ / STX $0350
  ctPolyQuad(m);                                                             // 24A3 JSL C3/0011
  for (int k = 0; k < 8; k++) {                                              // 24A7-250D: value += delta, 16-bit, CLC
    uint32_t v = 0x7F1CE9u + 2u * (uint32_t)k, d = 0x7F1CF9u + 2u * (uint32_t)k;
    w16(m, v, (r16(m, v) + r16(m, d)) & 0xFFFF);
  }
}
// C0/23E2: entry 0E (`FF 9C`), the line's teardown.
static void ctLineClose(Mem* m) {
  w8(m, 0x7E0139, 0);                                                        // 23E2 STZ $39
  w8(m, 0x7F1522, 0x00); w8(m, 0x7F1523, 0x00);
  w8(m, 0x7E0BD9, 0x00); w8(m, 0x7E0BDA, 0x00); w8(m, 0x7E0BDB, 0x00);
  w8(m, 0x7E0BDE, 0x02);                                                     // 23F9 CGWSEL
  w8(m, 0x7E0121, 0xE0); w8(m, 0x7E0122, 0xE0);                              // 23FE STA $21 / STA $22
  w8(m, 0x7E0128, r8(m, 0x7E0128) & ~0x20);                                  // 2404 TRB $28
}
static void ctLineFrame(CtScene* sc) {
  int st = r8(sc->mem, 0x7E0139);
  if (st == 6) ctLineStep(sc->mem);
  else if (st == 0x0E) ctLineClose(sc->mem);
}

// C0/2126's head, 212E-216A: the code the last render stopped on ($0215) moves the line cursor $0131 before the next
// render. A page code (09-0C) restarts it (2161 `LDA #00`), a line code (05-08) steps it (215B `LDA $31 / CMP #03 /
// BNE -> INC A`, so line 3 wraps to 0); both `STZ $32` and clear the line buffer (JSR C0/20B2). Other codes leave it.
static void tbLineStep(Mem* m, int code) {
  if (code >= 0x09 && code <= 0x0C) w8(m, 0x7E0131, 0);
  else if (code >= 0x05 && code <= 0x08) { int l = r8(m, 0x7E0131); w8(m, 0x7E0131, (uint8_t)(l == 3 ? 0 : l + 1)); }
}
void CtSceneTextboxLineStep(CtScene* sc, int code) { if (sc) tbLineStep(sc->mem, code); }

// C0/2096 set $0129 = 6 this frame (a pause ran out); C0/2126 types from the next. One frame long, so kept out of
// CtScene (whose layout the CTFS snapshots carry).
static int gTbResumeArmed = 0;
static void tbMachineTick(CtScene* sc) {
  Mem* m = sc->mem;
  uint8_t st = (uint8_t)r8(m, 0x7E0129);
  gTbResumeArmed = 0;
  if (st == 0) return;
  // $0131 is the line cursor. Its writers are these handlers (1FB9 STZ, 1FC2 INC, 1FEE STZ) and the typewriter's head
  // (tbLineStep: here at 1FC5's JSR C0/2126, the host's typewriter otherwise). $0136 is the NMI's upload request (C0/EB79
  // -> C0/6ECB copies the line buffer into window line $0131 while it is non-zero): 1FBB = 1, 1FF2 = 0 here, 21A3 = 1
  // in the host's typewriter, 21DC = $81 in CtSceneTextboxRevealDone, and the NMI's own EB7F STZ (field.c).
  if (st == 8 && sc->tbPageArmed) {
    // the host's press turned the page this frame: the ROM's 2086 `LDA #08 / STA $29 / RTL` ends the machine's frame,
    // so 1FB4 (the clear, $0131 = 0) belongs to the next frame.
    sc->tbPageArmed = 0;
  } else if (st == 1 || st == 8) {                            // 1FB4 (JSR C0/20B2: the line buffer is the host's)
    w8(m, 0x7E0129, (uint8_t)(st + 1));
    w8(m, 0x7E0131, 0);                                       // 1FB9 STZ $31
    w8(m, 0x7E0136, 1);                                       // 1FBB LDA #01 / STA $36: the NMI uploads the blank lines
  } else if ((st >= 2 && st <= 4) || (st >= 9 && st <= 0xB)) {  // 1FC0
    w8(m, 0x7E0129, (uint8_t)(st + 1));
    w8(m, 0x7E0131, (uint8_t)(r8(m, 0x7E0131) + 1));          // 1FC2 INC $31
  } else if (st == 0xC) {                                     // 1FC5
    w8(m, 0x7E0129, 6);
    tbLineStep(m, r8(m, 0x7E0215));                           // 1FC9 JSR C0/2126: the page's own code
  } else if (st == 5) {                                       // 1FCD
    if (r8(m, 0x7E0126) == 0) {
      int req = r8(m, 0x7E0130), pos;
      if (req == 1 || req == 2) pos = req;
      else pos = (r8(m, 0x7E0A80 + r8(m, 0x7E012E)) & 0x80) ? 1 : 2;   // 1FDD LDX $2E / LDA $0A80,X / BPL
      w8(m, 0x7E0126, (uint8_t)pos);
      w8(m, 0x7E0127, 0);
      w8(m, 0x7E0131, 0);                                     // 1FEE STZ $31
      w8(m, 0x7E0133, 0);                                     // 1FF0 STZ $33
      w8(m, 0x7E0136, 0);                                     // 1FF2 STZ $36: no upload while the window opens
      // $0215 is seeded with the window (04), so a box does not inherit the previous box's 00 ("the string
      // is finished") during its ramp, which would arm a multi-page decision's cursor on the first page.
      // Afterwards it holds 0x10 while typing, each break code as consumed, and the code the string ended on.
      w8(m, 0x7E0215, 0x04);
    } else {                                                  // 1FF8: the open ramp
      int c = r8(m, 0x7E0127), open = (c == 0x28);
      for (int k = 0; k < 4 && !open; k++) { c++; open = (c == 0x28); }
      w8(m, 0x7E0127, (uint8_t)c);
      if (open) w8(m, 0x7E0129, 6);                           // 2045
    }
  } else if (st == 7) {
    // C0/201C: state 7 branches on $0162 before it looks at the press latch:
    //   201C LDA $62 / 201E BEQ ->2056        ; 0 -> the plain box path (2056 -> 20A5 reads $0134)
    //   2020 CMP #01 / 2022 BNE ->20A0
    //   2024 LDA $0215 / 2027 BNE ->2056      ; a control code still pending = not the last page
    //   2029 LDA $64 / STA $63                ; arm the cursor on the first option line
    //   202D LDA #02 / STA $62                ; cursor live
    //   20A0 CMP #03 / BEQ ->2058             ; 3 = confirmed -> 2058 -> 2081 `$29 = 0D` (close)
    //   20A4 RTL                              ; 2 = live but not confirmed -> nothing
    // So on a decision box ($0162 >= 1) the press is never the $0134 latch. On a non-final page the pending
    // page-break code is still in $0215, so only the last page arms the cursor. The host writes $0215 when
    // its reveal finishes (0 = string exhausted, else the pending break code; field.c).
    int d = r8(m, 0x7E0162);
    int t34 = r8(m, 0x7E0134) | (r8(m, 0x7E0135) << 8);
    if (d == 0 && t34 != 0 && t34 != 0xFFFF) {
      // 20A9 `CPX #FFFF / BEQ` / 20AE `DEX / STX $34`: the pause's count. An in-string `03 n` left $0134 = n * 15 (C0/21A8,
      // CtSceneTextboxPause) and only this frame tick spends it; A does not (C0/1AB8 spends FFFF only).
      t34--;
      w8(m, 0x7E0134, (uint8_t)t34); w8(m, 0x7E0135, (uint8_t)(t34 >> 8));
    } else if (d == 0 && t34 == 0 && r8(m, 0x7E0215) == 0x03) {
      // 2058 on the pause code: 2096 `LDA $0216 / BEQ ->2081`. A pause that ran out types on (`LDA #06 / STA $29`), and
      // `03 00` (no count) closes the box with no press (2081 `LDA #0D / STA $29`).
      if (r8(m, 0x7E0216)) { w8(m, 0x7E0129, 6); gTbResumeArmed = 1; }   // 209B ... RTL: the typewriter is next frame's
      else w8(m, 0x7E0129, 0x0D);
    } else if (d == 0 && t34 == 0) {
      // 20A5 `LDX $34 / BEQ ->2058`: advance now, no press. The typewriter reached state 7 through 21D8 alone (a
      // self-turning page code, 0x09/0x0A), which leaves $0134 at 0. 2058 on the code: a page code -> 2086
      // `LDA #08 / STA $29`; a line code -> 8 when the window is full ($0131 == 3), else back to 6. (A pressed box
      // never gets here with $0134 == 0: the host's press turns the page or closes it in the same frame.)
      int c = r8(m, 0x7E0215);
      if ((c >= 0x09 && c <= 0x0C) || (c >= 0x05 && c <= 0x08 && r8(m, 0x7E0131) == 3)) w8(m, 0x7E0129, 8);
      else if (c >= 0x05 && c <= 0x08) w8(m, 0x7E0129, 6);
    } else if (d == 1) {
      if (r8(m, 0x7E0215) == 0) {                               // 2024/2027: the last page
        w8(m, 0x7E0163, (uint8_t)r8(m, 0x7E0164));              // 2029-202B
        w8(m, 0x7E0162, 2);                                     // 202D-202F
      }
    } else if (d == 3) {                                        // 20A0 -> 2058 -> 2081
      w8(m, 0x7E0129, 0x0D);
    }
    /* d == 0: the host's plain-box press path owns it. d == 2: 20A4 RTL, nothing. */
  } else if (st == 0x0D && !sc->tbCloseArmed) {              // 2032
    int c = r8(m, 0x7E0127), done = 0;
    if (c == 0) done = 1;
    else if (!--c) done = 1;
    else if (!--c) done = 1;
    else if (!--c) done = 1;
    else --c;                                    /* the 4th DEC is not tested */
    w8(m, 0x7E0127, (uint8_t)c);
    if (done) {                                                // 204D / 204F / 2051
      w8(m, 0x7E0129, 0); w8(m, 0x7E0126, 0);
      w8(m, 0x7E0154, (uint8_t)(r8(m, 0x7E0154) & ~0x20));
    }
  }
  if (st != 0x0D) sc->tbCloseArmed = 0;                       // the arm frame passes; the ramp starts next tick
  else if (sc->tbCloseArmed) sc->tbCloseArmed = 0;
}

// FD/FFF7 -> FD/E39C, one frame of the twelve palette-effect records (src/objslot.c). Reached from C0/00BF's slot
// (C0/00CD), from C0/00DE (the style branch's frames) and from C0/1793 after the queued copy.
static int gObjSlotCalls = 0;
static void ctObjSlotFrame(CtScene* sc) {
  static int tr = -1;
  if (tr < 0) tr = getenv("CT_OBJSLOTTRACE") ? 1 : 0;
  CtObjSlotIo io = { sc->mem->s, NULL, NULL, tr ? "objslot" : NULL, gObjSlotCalls++, 1, gRomImg, (long)gRomImgLen };
  CtObjSlotService(&io);
}

// C0/00EB, one wait of the field's return from a battle: `JSR C0/881E / JSR C0/00DE / BRL C0/EC60`, i.e. the
// camera tail, then C0/00DE (`JSL FD/C2C1`, which builds a HDMA table set and flips $0153 bit 0, and `JSL FD/FFF7`
// -> FD/E39C), then the NMI wait. C0/0304 makes four of them and C0/18C1's tail a fifth (field.c); each is one
// frame of the return, the first on the ack's own frame.
void CtSceneReturnWaitFrame(CtScene* sc) {
  if (!sc) return;
  CtSceneCameraTick(sc);                              // C0/881E (the lead's controls are the host's; the camera is this layer's)
  CtHdmaTableBuild(sc->mem->s);                       // C0/00DE: JSL FD/C2C1
  ctObjSlotFrame(sc);                                 //          JSL FD/FFF7 -> FD/E39C
}
// C0/02AF-02C1, one frame of the defeat's fade: `JSR C0/881E / JSL FD/FFF7 / JSR C0/EC60`, the camera and FD/E39C, no
// C0/00DE (the wait before it, C0/02A0-02AD, is C0/00EB itself: CtSceneReturnWaitFrame).
void CtSceneDefeatFadeFrame(CtScene* sc) {
  if (!sc) return;
  CtSceneCameraTick(sc);                              // C0/02B9 JSR C0/881E
  ctObjSlotFrame(sc);                                 // C0/02BC JSL FD/FFF7 -> FD/E39C
}

// ---- The $0117 bit 5 service, C0/1770-188B (disasm/notes/field_tile_copy_e5.md) ----
// The main loop is C0/0097 `TSB $51 / JSR 18D9 / JSR 0C76 / JSR 881E / JSR 1AAC / JSL 1F87 / JSR 21E1 / JSR 274D /
// JSR 00BF / JSR EC60`. C0/0C76 fades (1F24/1F5A) and then dispatches $0117: bit 7 (0C8F, the location change), bit 6
// (0CC7, the menu), bit 4 (0D3E, a door, which ends in `BRL 16DC`), bit 1 (16DC, a chest) and then C0/1770:
//   1770 LDA $17 / BIT #20 / BNE / BRL 188C       bit 5 clear: on to bit 0 (the battle)
//   1779 LDA #20 / TRB $17 / LDA #01 / STA $38
//   1781 JSR 885A / JSR 00BF / LDA $38 / BEQ 1790 / JSR EC60 / BRA 1781      the camera aligns, a whole event frame per step
//   1790 JSR AF4E / JSL FD:FFF7 / JSR EC60                                   the copy, on the operands op 0xE5 staged
//   179A LDA $45 -> the style branch below, which waits in C0/EC60 with only C0/00DE (FD:C2C1 + FD:FFF7) per frame and
//   returns (RTS, or BRL EC60's) into C0/00A7 -- that frame runs 881E .. 00BF but not 0097, 18D9 or 0C76.
int CtSceneXferFrame(const CtScene* sc) {
  if (!sc || sc->loadFadeIn) return CT_XF_NORMAL;    // C0/2848's ramp is not the main loop: no dispatch there
  if (sc->xfer == 2) return CT_XF_STYLE;
  if (sc->xfer == 3) return CT_XF_RESUME;
  if (sc->xfer == 1) return CT_XF_ALIGN;
  const uint8_t* s = sc->mem->s;
  if ((s[0x0117] & 0x20) && !(s[0x0117] & 0xC0)) return CT_XF_ENTER;   // 0C8F BPL / 0CC7 BIT #40 are dispatched first
  if (s[0x0138]) return CT_XF_ALIGN;                 // a seed taken inside C0/1781-178E (C0/1770 is $0138's only armer)
  return CT_XF_NORMAL;
}

// C0/179A's styles, on $0145 = the low three bits of op 0xE5's mask, i.e. which of BG1/BG2/BG3 the copy touched. The
// frames each spends (every one ends in C0/EC60) and the one whose NMI upload puts the new tiles on screen:
//   1  17A0 75E9(BG1) 74D4 00DE $46=1 STZ $45 EC60 | 17BC 00DE 87F1 BRL EC60                    2, shown on the 1st
//   2  17C9 78EC(BG2) 74E8 00DE $46=2 STZ $45 EC60 | 17E5 00DE 87F1 BRL EC60                    2, shown on the 1st
//   3  17F5 75E9 00DE $46=3 STZ $45 EC60 | 180E 78EC, $7C<->$82, 74D4 74E8 00DE $46=2 EC60 |
//      1835 75E9, $7C<->$82, 74D4 00DE $46=1 EC60 | 1859 00DE 87F1 BRL EC60                     4, shown on the 2nd
//   4  1866 7CB5(BG3) 74F7 00DE $46=4 STZ $45 EC60 | 1882 00DE 87F1 EC60 RTS                    2, shown on the 1st
//   0, 5-7  1862 `CMP #04 / BNE ->188B` RTS                                                     none, no upload
// The NMI (C0/EAD1) uploads the built tilemap to $1D7C (1) / $1D7E (2) / $1D82 (3) / $1D80 (4), STZ $46, and for 1 and 2
// re-points BG1 at $1D7D: style 3 builds BG1 into the back buffer $1D82, swaps it in with BG2's upload (the 2nd frame,
// the one change the picture shows), then rebuilds the front one and swaps back. Styles 1/2/4 show on the 1st.
static const int kXferStyleFrames[8] = { 0, 2, 2, 4, 2, 0, 0, 0 };
static const int kXferStyleShown[8]  = { 0, 1, 1, 2, 1, 0, 0, 0 };

// C0/74D4 / 74E8 / 74F7: a layer's re-origin after its tilemap is rebuilt at the camera: STZ its tilemap origin ($1D99 /
// $1D9B / $1D9D) and its two HDMA step counters ($1D87,$1D89 / $1D8B,$1D8D / $1D8F,$1D91), all sixteen-bit, then C0/7F9A
// (the edge-upload addresses $1D9F-$1DAB, render-side). The port's picture keeps each layer's build origin as an offset
// from BG1's (bgD2*/bgD3*, CtSceneLayerOffsets): BG1 re-origined moves BG1's, BG2/BG3 re-origined sit on BG1's camera.
static void ctXferReorigin(CtScene* sc, int layer) {
  uint8_t* s = sc->mem->s;
  int b1x = (int16_t)(s[0x1D87] | (s[0x1D88] << 8)), b1y = (int16_t)(s[0x1D89] | (s[0x1D8A] << 8));
  if (layer == 0) { sc->bgD2x -= b1x; sc->bgD2y -= b1y; sc->bgD3x -= b1x; sc->bgD3y -= b1y; }
  else if (layer == 1) { sc->bgD2x = b1x; sc->bgD2y = b1y; }
  else { sc->bgD3x = b1x; sc->bgD3y = b1y; }
  static const int org[3] = { 0x1D99, 0x1D9B, 0x1D9D }, ctr[3] = { 0x1D87, 0x1D8B, 0x1D8F };
  s[org[layer]] = s[org[layer] + 1] = 0;
  for (int k = 0; k < 4; k++) s[ctr[layer] + k] = 0;
}

static void ctXferSwapBg1(uint8_t* s) {               // 180E-1822 / 1835-184B: LDX $7C / PHX / LDX $82 / STX $7C / PLX / STX $82
  uint8_t a = s[0x1D7C], b = s[0x1D7D];
  s[0x1D7C] = s[0x1D82]; s[0x1D7D] = s[0x1D83]; s[0x1D82] = a; s[0x1D83] = b;
}

// The NMI of a style frame that hands a tilemap. The field NMI starts `EAD1 LDA $46 / BNE ->EAD8`: `CMP #01` / `#02` /
// `#03` / `#04` upload $1D7C / $1D7E / $1D82 / $1D80 (C0/7F62), bit 3 C0/7F5D, bit 4 C0/7F58, and each ends `JSL FD:C1EE /
// STZ $52 / BRL EC15`, past C0/EB79-EC13, so that NMI skips 6ECB (the text line), 712E (the CGRAM, $7E:2200 x $200 over
// channel 7), ECA3 (the OAM), B1B2 (the VRAM queue, $09A0's clear) and the edge columns ($1D78-$1D7B). Bit 5 (C0/EC77)
// and any other value fall into EB79.
int CtFieldNmiTilemapArm(uint8_t v46) {
  if (!v46) return 0;                                 // EAD3 BNE, else EAD5 BRL EB79
  if (v46 <= 4) return 1;                             // EAD8-EB43, the four CMPs
  return (v46 & 0x18) != 0;                           // EB46 BIT #08 / EB5A BIT #10; EB6E BIT #20 falls into EB79
}

// One frame of C0/179A's branch (the main loop is inside it: nothing but this and the NMI runs).
static void ctXferStyleFrame(CtScene* sc) {
  uint8_t* s = sc->mem->s;
  int st = sc->xferStyle & 7, k = ++sc->xferStep, v46 = 0;
  switch (st) {
  case 1: if (k == 1) { ctXferReorigin(sc, 0); v46 = 1; s[0x0145] = 0; } break;
  case 2: if (k == 1) { ctXferReorigin(sc, 1); v46 = 2; s[0x0145] = 0; } break;
  case 3: if (k == 1) { v46 = 3; s[0x0145] = 0; }
          else if (k == 2) { ctXferSwapBg1(s); ctXferReorigin(sc, 0); ctXferReorigin(sc, 1); v46 = 2; }
          else if (k == 3) { ctXferSwapBg1(s); ctXferReorigin(sc, 0); v46 = 1; }
          break;
  case 4: if (k == 1) { ctXferReorigin(sc, 2); v46 = 4; s[0x0145] = 0; } break;
  }
  CtHdmaTableBuild(s);                                // C0/00DE: JSL FD/C2C1 (a table set built, $0153 bit 0 flips)...
  ctObjSlotFrame(sc);                                 //   ...JSL FD/FFF7
  if (v46) { s[0x0146] = (uint8_t)v46; sc->xferNmi46 = 1; }
  if (k == kXferStyleShown[st]) {                     // this frame's NMI upload is the one the picture shows
    const int* o = sc->xferOp;
    ctTileCopyQueue(sc, o[0], o[1], o[2], o[3], o[4], o[5], o[6] | CT_TC_RENDER);
  }
  if (k >= kXferStyleFrames[st]) sc->xfer = 3;       // the last wait: the branch returns into C0/00A7
}

// After C0/00BF's pass (the one C0/1784 runs inside the align loop): C0/1787 `LDA $38 / BEQ ->1790`.
static void ctXferAfterVm(CtScene* sc) {
  uint8_t* s = sc->mem->s;
  if (sc->xfer == 1) {
    if (s[0x0138]) return;                           // 178B JSR EC60 / BRA 1781: another align frame
    CtVmTileCopyAF4E(s);                             // 1790 JSR C0/AF4E, on $013E-$0144 as they stand now
    int* o = sc->xferOp;                             //   (this frame's pass may have staged a newer op 0xE5)
    o[0] = s[0x013E]; o[1] = s[0x013F]; o[2] = s[0x0140]; o[3] = s[0x0141];
    o[4] = s[0x0142]; o[5] = s[0x0143]; o[6] = s[0x0144];
    if (o[6]) ctTileCopyQueue(sc, o[0], o[1], o[2], o[3], o[4], o[5], o[6] | CT_TC_COLL);   // the planes the port
    ctObjSlotFrame(sc);                              // 1793 JSL FD/FFF7                     //   keeps outside WRAM
    sc->xferStyle = s[0x0145]; sc->xferStep = 0;     // 1797 JSR EC60; 179A LDA $45 on the next frame
    sc->xfer = kXferStyleFrames[sc->xferStyle & 7] ? 2 : 3;
    if (getenv("CT_XFERTRACE"))
      printf("xfer: t%d C0/1790 AF4E %02X%02X %02X%02X %02X%02X mask %02X style %d\n", gVmTick,
             o[1], o[0], o[3], o[2], o[5], o[4], o[6], sc->xferStyle);
  } else if (sc->xfer == 3) sc->xfer = 0;            // the frame the branch returned into is over
}

// C0/0C9A-0CC4, the pending change's commit: C0/0C9D-0CB2 file where the party was ($0100 -> $0105/6, the lead's tile
// $1801/$1881 -> $0107/$0108, its facing ^ 1 -> $0109), 0CB4-0CBE copy the staged $0112/$0114/$0116 into $0100-$0104,
// and 0CC0 `LDX #06FF / TXS / BRL C0/0000` leaves the main loop; nothing after C0/0C76 runs in the old scene again, so a
// committed scene's CtSceneTick returns at once. Called by the tick's drain or by a host that runs C0/0C76's head itself
// (field.c), once only: run twice, C0/0C9D's `$0105 = $0100` would file the destination as the origin.
void CtSceneExitCommit(CtScene* sc) {
  if (!sc || sc->exitCommitted) return;
  int po = sc->playerObj >= 0 ? sc->playerObj * 2 : 0;   // the ROM reads the lead offset from $0197
  CtWramCommitLocation((CtWram*)sc->mem, po);
  // The destination is read back from the committed words $0100/$0102/$0103/$0104, as the ROM's loader does
  // (C0/0066 dispatches on $0100), so an op 0xE0 and a positional map exit go through the same five bytes.
  // The immediate variants are another path: 0xDD/0xDE (C0/3B1B) write $0100 on the spot and leave the
  // delivery to the next reboot; 0xDF/0xE1 (C0/3B4B) reboot themselves at the vblank.
  sc->lcLoc    = r8(sc->mem, 0x7E0100) | (r8(sc->mem, 0x7E0101) << 8);
  sc->lcX      = r8(sc->mem, 0x7E0102);
  sc->lcY      = r8(sc->mem, 0x7E0103);
  sc->lcFacing = r8(sc->mem, 0x7E0104) & 3;
  sc->lcStyle  = 0;
  sc->locChange = 1;
  sc->exitCommitted = 1;
}

#define SND_TAIL_LINES 61   // the tick's tail after the runner, in lines (see CtScene.sndPostResumeFreeze)
// The rule's infinitely fast CPU (CT_ORACLE_NMION_HOLD / CT_ORACLE_IFAST; see CLAUDE.md, the zelda3 model; audio.c
// CtAudioRuleOn is the sink's side): a sound op's JSL costs the frames its SPC needs from where the NMI left the beam,
// and the CPU work around it is free, so the tail rule above (a real-time CPU's overrun) does not apply.
static int g_vmRuleIfast = -1;
void CtSceneSetRuleIfast(int on) { g_vmRuleIfast = on ? 1 : 0; }
static int vmRuleIfast(void) {
  if (g_vmRuleIfast < 0) { const char* h = getenv("CT_ORACLE_NMION_HOLD"), *f = getenv("CT_ORACLE_IFAST");
                           g_vmRuleIfast = (!(h && *h == '0') && !(f && *f == '0')); }
  return g_vmRuleIfast;
}
void CtSceneTick(CtScene* sc) {
  // An op 0xDF/0xE1 has run: the ROM is spinning inside C0/3B71 until the vblank and then reboots, so no leg of
  // the main loop happens again in this scene (only the NMI, which is the host's).
  if (sc->rebootReq) return;
  if (sc->exitCommitted) { gVmTick++; return; }         // C0/0CC0 left the main loop (CtSceneExitCommit); the frame still counts
  // The $0117 bit 5 service decides which legs of the main loop this frame has (CtSceneXferFrame, the helpers above):
  // `head` = C0/0097-00A4 (the $51 latch, 18D9, 0C76's fades and dispatch), `body` = C0/00A7-00B4 (881E, 1AAC, the box
  // machine 1F87, the wipe 21E1, 274D); the event pass C0/00BF runs in every kind but STYLE.
  int xk = CtSceneXferFrame(sc);
  int nmiTilemap = CtFieldNmiTilemapArm((uint8_t)r8(sc->mem, 0x7E0146));   // C0/EAD1 on the $46 the last frame left
  if (sc->xferNmi46) { sc->xferNmi46 = 0; w8(sc->mem, 0x7E0146, 0); }   // C0/EAE4.. STZ $46: the NMI took the upload
  CtVmCaptionNmi(sc->mem->s);                            // C0/EB46-EB77: the caption plane's uploads, one arm an NMI
  if (xk == CT_XF_ENTER) {                               // 1779 LDA #20 / TRB $17 / 177D LDA #01 / STA $38
    w8(sc->mem, 0x7E0117, (uint8_t)(r8(sc->mem, 0x7E0117) & ~0x20));
    w8(sc->mem, 0x7E0138, 1);                            //   (1781's 885A is this frame's camera tick)
    sc->xfer = 1;
  } else if (xk == CT_XF_ALIGN) sc->xfer = 1;
  int head = (xk == CT_XF_NORMAL || xk == CT_XF_ENTER), body = (xk == CT_XF_NORMAL || xk == CT_XF_RESUME);
  // C0/0097, the first instruction of the field frame: `LDA $00F0 / TSB $51`. $00F0 is the newly pressed
  // button byte; $0151 accumulates it and ops 0x3F-0x44 consume bits with `TRB $51`.
  // Note: the VM reads $00F0 but never writes it. The input bytes belong to the joypad routine (C2/84D2),
  // which is the host's (field.c fieldPadNmi; --vmdiff uses CtSceneSetButtons).
  // Only on a main-loop frame: C0/2848's ramp (see loadFadeIn) never reaches C0/0097.
  if (!sc->loadFadeIn && head)
    w8(sc->mem, 0x7E0151, (uint8_t)(r8(sc->mem, 0x7E0151) | r8(sc->mem, 0x7E00F0)));   // 009A TSB $51
  // The message box machine C0/1F87, in the ROM's frame slot (C0/00AD, after the A handler and before the VM).
  // It lives here because the event ops read its bytes and --vmdiff runs the VM without a field layer. It is a
  // main-loop leg, so the load's fade-in ramp does not run it.
  if (!sc->loadFadeIn && body) tbMachineTick(sc);

  gVmTick++;
  // The song-upload freeze, decided first (the sndWait fields; song_load_freeze.md sec. 6). While the main
  // loop is parked inside `JSL $C7:0004` only the NMI's work happens (the upload flush below, the fade
  // counter's INIDISP, the deferred copies); no main-loop leg runs: not the camera, the $0117 drain or the
  // scheduler.
  int sndResuming = 0;
  sc->sndFrozenTick = 0;
  if (sc->sndPostResumeFreeze) { sc->sndPostResumeFreeze = 0; sc->sndFrozenTick = 1; }   // the tail overran: no tick this window
  else if (sc->sndWait > 0) {
    if (--sc->sndWait > 0) sc->sndFrozenTick = 1;
    else { sndResuming = 1; sc->rasterLine = sc->sndEndLine;
           // (under the rule's infinitely fast CPU the tick's tail after the JSL costs no beam and never overruns
           // the NMI; the frames lost are the JSL's own, vmRuleIfast)
           sc->sndPostResumeFreeze = !vmRuleIfast() && (sc->sndEndLine + SND_TAIL_LINES >= 225);
           if (sc->sndResumeN > 0) sc->st[sc->sndResumeObj].addr = sc->sndSavedAddr; }   // the JSL returned: the visit ends, the ip commits
  } else sc->rasterLine = 12;   // the runner's start line on the early queue (measured with `--hookpc 3C43`)
  sc->sndPosted = 0; sc->sndFreezeFrames = 0;
  // C0/EB8C, the NMI's upload flush, first. C0/CA76's prologue `LDA $09A0 / BEQ / RTS` refuses the whole drain
  // while the VRAM upload queue holds work, and only C0/B1B2 clears $09A0. It runs from the NMI, at the top of the
  // frame; flushing at the end of the tick would refuse the drain on every frame after an upload. `vram = NULL`
  // walks the queue and clears the gate without copying (the copy is the renderer's half).
  // Not after a frame whose NMI a style tilemap took: that NMI `BRL EC15`s past B1B2 (CtFieldNmiTilemapArm).
  if (legsOn() && !nmiTilemap) CtAnimUploadFlush(sc->mem->s, NULL);
  if (xk == CT_XF_STYLE) { ctXferStyleFrame(sc); return; }   // C0/179A's branch: C0/00DE and the NMI, nothing else
  if (!sc->sndFrozenTick) CtSceneCameraTick(sc);         // the field camera, once per frame for every VM host
  // Op 0xE0 fade-change drain (C0/0C8A, before the script scheduler): while $0117 bit 7 is latched, DEC the
  // $0119 fade counter once per frame; at 0 (or negative, the ROM's BMI) commit (CtSceneExitCommit) and raise
  // locChange for the host; the new scene's memInit models the load's `STZ $17` (C0/0B96). An op 0xE0 in frame N
  // leaves $0119 at 0x0F, so the first DEC lands on N+1 and the reload on N+16.
  // Parked during the load's fade-in (C0/2848's loop does not call C0/0C76).
  // It reads the $0117 of the frame's head: C0/0C76 runs at C0/00A4, before the player controller C0/881E, whose
  // step stages a positional exit (C0/9D24 `ORA #80 / STA $0117`), so the first DEC is the next iteration's. A
  // host whose controller runs before this tick latches the head's byte first (CtSceneHeadLatch).
  uint8_t d117 = sc->head117Set ? sc->head117 : (uint8_t)r8(sc->mem, 0x7E0117);
  sc->head117Set = 0;
  if (!sc->sndFrozenTick && !sc->loadFadeIn && head && (d117 & 0x80)) {
    int fc = r8(sc->mem, 0x7E0119);
    if (fc != 0 && !(fc & 0x80)) {
      // 0C97 DEC $19, and sc->bright with it (it stands for $0119's low nibble, which the NMI's C0/EC4E shows
      // this frame).
      w8(sc->mem, 0x7E0119, fc - 1);
      sc->bright = (fc - 1) & 0x0F;
    } else {
      CtSceneExitCommit(sc);
      return;                                            // the main loop is left (see CtSceneExitCommit)
    }
  }
  // The armed fade, C0/0C76 -> C0/1F24, once per main-loop frame (C0/00A4), gated on $0118 bit 1:
  //   1F24 LDA $19 / AND #0F / CMP $1B / BEQ ->1F55          at the target: 1F55 `LDA #02 / TRB $18`
  //   1F2C BCS ->1F40                                        above it: the darkening arm
  //   1F2E/1F40 LDA $1D / BEQ / DEC $1D / RTS                 the countdown: no step while $1D != 0
  //   1F35/1F47 LDA $1C / STA $1D                             reload it from the rate, then step:
  //   1F39 INC $19 / RTS                                      brighten (the bit clears on the next call)
  //   1F4B LDA $19 / DEC A / BEQ ->1F53 / STA $19 / RTS       darken; reaching 0 stores and clears the bit
  // So rate r steps every r+1 frames. sc->bright stands for $0119's low nibble. It is part of C0/0C76, so it
  // waits out the $0117 bit 5 service with the rest of the head (`head`).
  if (head && !sc->loadFadeIn && (r8(sc->mem, 0x7E0118) & 0x02)) {
    int cur = sc->bright & 0x0F, tg = r8(sc->mem, 0x7E011B);
    if (cur == tg) w8(sc->mem, 0x7E0118, r8(sc->mem, 0x7E0118) & ~0x02);       // 1F2A -> 1F55
    else if (r8(sc->mem, 0x7E011D)) w8(sc->mem, 0x7E011D, r8(sc->mem, 0x7E011D) - 1);   // 1F30/1F42 -> DEC
    else {
      w8(sc->mem, 0x7E011D, r8(sc->mem, 0x7E011C));                            // 1F35/1F47
      if (cur < tg) sc->bright++;                                              // 1F3D INC $19
      else if (--sc->bright == 0) w8(sc->mem, 0x7E0118, r8(sc->mem, 0x7E0118) & ~0x02);   // 1F4D-1F57
      w8(sc->mem, 0x7E0119, (uint8_t)sc->bright);                              // (the byte they step)
    }
  }
  // C0/0C81 LDA $18 / BIT #08 / BEQ / JSR C0/1F5A, right after the brightness: the colour-math fade that op 0xF1's
  // transition arm requests (3D3D `LDA #18 / TSB $18`). It walks COLDATA's shadow $0121 one step toward $0122 every
  // $0123 + 1 frames ($0124 the countdown) and, on reaching it, clears bit 3, the bit op 0xF3 waits on:
  //   1F5A LDA $21 / CMP $22 / BEQ ->1F82                   1F82 LDA #08 / TRB $18 / RTS
  //   1F60 BCS ->1F72 (above: down)                         below: up
  //   1F62/1F72 LDA $24 / BEQ / DEC $24 / RTS               the countdown, then
  //   1F69/1F79 LDA $23 / STA $24 / INC $21 (DEC $21)       reload it and step
  if (head && !sc->loadFadeIn && (r8(sc->mem, 0x7E0118) & 0x08)) {
    uint8_t c21 = r8(sc->mem, 0x7E0121), c22 = r8(sc->mem, 0x7E0122);
    if (c21 == c22) w8(sc->mem, 0x7E0118, r8(sc->mem, 0x7E0118) & ~0x08);     // 1F5E -> 1F82
    else if (r8(sc->mem, 0x7E0124)) w8(sc->mem, 0x7E0124, r8(sc->mem, 0x7E0124) - 1);   // 1F62/1F72 -> DEC $24
    else {
      w8(sc->mem, 0x7E0124, r8(sc->mem, 0x7E0123));                            // 1F69/1F79
      w8(sc->mem, 0x7E0121, (uint8_t)(c21 < c22 ? c21 + 1 : c21 - 1));         // 1F6F INC $21 / 1F7F DEC $21
    }
  }
  // C0/00B1 JSR C0/21E1, before the pass: `LDA $39 / BEQ / DEC / ASL / TAX / JSR ($21EE,X)`: 1 the iris opening (C0/220E),
  // 2 closing (C0/2269), 3 the window wipe (C0/22D8), 4 / 5 the circle as the colour window (C0/232A / 238A), 6 the quad
  // window (C0/2409) and 0E its teardown (C0/23E2), 7-0A the fixed circles (C0/2514 / 2589).
  // Not ported: entries 0B-0D (C0/260E, 264F, 2329).
  if (!sc->sndFrozenTick && !sc->loadFadeIn && body) {
    int st39 = r8(sc->mem, 0x7E0139);
    if (st39 == 1 || st39 == 2) ctIrisFrame(sc);
    else if (st39 == 4 || st39 == 5) ctColCircleFrame(sc);
    else if (st39 >= 7 && st39 <= 0x0A) ctFixedCircleFrame(sc);
    else if (st39 == 6 || st39 == 0x0E) ctLineFrame(sc);
    else ctWipeFrame(sc);
    // C0/00B4 JSR C0/274D: the caption requests in $0154 bits 2/3/4 and, with none of them, bit 5's C0/2758: a chest's box
    // closed (`$29 = 0D`) when its tile $015D/$015E (field.c's probe, C0/1E52) leaves the camera (move.c CtVmCaption274D).
    CtVmCaption274D(sc->mem->s, gRomImg, (int)gRomImgLen);
  }
  if (sc->sndFrozenTick) return;                        // the song-upload freeze: no queue-build, no runner, no legs
  // Frame routine C0/00BF: queue build (C0/59D9), then run (C0/5A46), as two passes.
  // Pass 1 (queue build): decrement every live object's cadence ($1001); when it expires, reload it from
  // $1000 and enqueue the object. The cadence cycles even for idle objects (prio 7, no addr), so one that
  // is activated resumes on its own cadence phase. The queue is fixed here: an object activated during
  // pass 2 and not already enqueued first runs on a later frame.
  uint8_t elig[64];
  if (sndResuming) memcpy(elig, sc->sndElig, sizeof elig); else   // the resume frame runs the queue the JSL interrupted
  for (int n = 0; n < sc->nobj && n < 64; n++) {
    elig[n] = 0;
    // C0/59EC `LDA $00,X / BMI ->59F8`: the pause bit is the only test before the countdown. A killed object (op
    // 0x0A, $1100 = 80) is the C0/5A06 skip below, after the DEC, so its count still drains to 0. The battle's
    // dead mark is this pause bit (C0/07F6).
    if (sc->actors[n].cadPause) continue;         // $1000 bit 7 (op 0x0B): cadence frozen until 0x0C
    CtSS* s = &sc->st[n];
    // C0/59F0-59F6: read the countdown $1001,X; run if it is already zero (an expired object that could not be
    // enqueued retries every frame), else decrement and run if that reached zero. So an object re-armed
    // mid-visit by op 0x87 lands on the new period.
    uint8_t cd = (uint8_t)r8(sc->mem, 0x7E1001u + (uint32_t)n * 2);
    if (cd != 0) {
      ctSetDc(sc, n, cd - 1);                      // C0/59F4 DEC $1001,X
      if (s->dc != 0) continue;                    // C0/59F6 BEQ -> the enqueue arms
    }
    // C0/5A06 `LDA $1100,X / BMI ->59F8`: an object whose move mode has bit 7 set is skipped after the DEC, so
    // its countdown reaches 0 and stays there (the reload is only in the enqueue arms 5A1F / 5A2B / 5A40).
    // $1100 = 0x80 is a removed object (op 0x0A) or a PC slot for a character not in the active party.
    // Read from sc->mode[], not WRAM: the live load ops (0x80-0x83) update the mirror and not $1100, so the
    // WRAM byte is reliable only on a seeded scene (C0/5BD5's scan reads the WRAM copy).
    if (sc->mode[n] & 0x80) continue;
    ctSetDc(sc, n, sc->actors[n].moveAnim > 0 ? sc->actors[n].moveAnim : 1);  /* 5A1F/5A2B/5A40 reload from $1000; 0 -> 1 (C0/42D7) */
    elig[n] = 1;     // enqueued on cadence expiry whatever its prio/addr: an idle object activated
                     // by an earlier object in pass 2 runs its new function this frame.
  }
  // Pass 2 (runner): run the queue from its head (C0/5A66 `LDA $74`, the link `$1080,X`). The queue is built in
  // index order, but C0/5A01 `LDA $1100,X / BEQ ->5A31` prepends the object whose move mode is 0, the lead PC
  // (5A3B `STA $80,X`, 5A3D `STX $0174`); every other object is appended at the tail $0175 (5A11-5A1B). So the
  // lead runs first and the rest in ascending order. A low-index orchestrator (obj0) runs before the callees it
  // requests, so a callee enqueued this frame runs its first op the same frame; but a lead that obj0 repoints
  // (op 0xD9's release) has already had its visit and runs the new code next frame.
  uint8_t order[64]; int nq = 0;
  if (sndResuming) { memcpy(order, sc->sndOrder, sizeof order); nq = sc->sndNq; }
  else {
    int heads = 0;
    for (int n = 0; n < sc->nobj && n < 64; n++) if (elig[n] && sc->mode[n] == 0) heads++;
    int h = heads, t = heads;
    for (int n = 0; n < sc->nobj && n < 64; n++) {
      if (!elig[n]) continue;
      if (sc->mode[n] == 0) order[--h] = (uint8_t)n;             // 5A31-5A3D: prepend (a later one lands in front)
      else order[t++] = (uint8_t)n;                              // 5A11-5A1B: append
    }
    nq = t;
  }
  { static int trObj = -2; if (trObj == -2) { const char* e = getenv("CT_VMTRACE"); trObj = e ? atoi(e) : -1; }
    if (trObj >= 0 && trObj < sc->nobj) {              // DEBUG (CT_VMTRACE=obj): per-frame scheduler view of one object
      CtSS* ts = &sc->st[trObj];
      fprintf(stderr, "[vmtrace t%d obj%d loc=%d] addr=%04X prio=%d dc=%d elig=%d dead=%d cadP=%d mode=%02X op=%02X l9=%02X%02X l10=%02X%02X\n",
              gVmTick, trObj, sc->mem->s[0x0100] | (sc->mem->s[0x0101] << 8),
              ts->addr & 0xFFFF, ts->prio, ts->dc, elig[trObj], sc->actors[trObj].dead,
              sc->actors[trObj].cadPause, sc->mode[trObj] & 0xFF,
              (ts->addr >= 0 && ts->addr < sc->blen) ? sc->body[ts->addr] : 0xEE,
              sc->mem->l[0x13], sc->mem->l[0x12], sc->mem->l[0x15], sc->mem->l[0x14]); } }
  if (!sndResuming) g_leadTk = -1;                      // no B0 visit of the lead yet in this pass (a resumed one had it)
  if (!sndResuming) g_daRingPend = 0;
  if (!sndResuming) { memset(g_ranB0, 0, sizeof g_ranB0); memset(g_landedB0, 0, sizeof g_landedB0); }   // a resumed
                                                                // pass keeps what its first half dispatched
  for (int qi = sndResuming ? sc->sndResumeN : 0; qi < nq; qi++) {   // no cut: C0/5A5D on the fixed virtual beam
    int n = order[qi];
    CtSS* s = &sc->st[n];
    // Idle (never activated): nothing to run. A killed one still runs: C0/5A66-5A8B pops the queue head with no
    // look at $1100, so an object killed earlier this frame keeps the visit it was queued for.
    if (s->addr < 0) continue;
    // The per-visit op budget, C0/5A93:
    //   5A93: LDA $68 / STA $67          ; $67 = the budget
    //   5AB1: JSR ($5D6E,X)              ; dispatch
    //   5AB4: BCC ->5ABA                 ; carry clear -> end the visit
    //   5AB6: DEC $67 / 5AB8: BNE ->5AA3 ; carry set -> spend one, next opcode
    // $0168's only writer is C0/0BCA `LDA #05 / STA $68`, so five dispatches. The DEC is unconditional on the
    // SEC path, control-flow ops included (op 0x10 -> C0/62B5, op 0x11 -> C0/62CB, tail 62C6 `... SEC / RTS`).
    // The raster gate lives in the outer scheduler C0/5A46 (5A5D `CMP $69`), between objects, never in a visit.
    for (int k = 0; k < 5; k++)
      if (step(sc, n)) break;
    if (n < 64) g_landedB0[n] = !g_ranB0[n] && s->addr >= 0 && s->addr < sc->blen && sc->body[s->addr] == 0xB0;
    // C0/5ABC-5ABF `REP #20 / TXA / LDX $6D / STA $1180,X`: the runner writes the object's script pointer back
    // to WRAM at the end of every visit (C0/59AB is the same store on the autorun path), so a saved state carries
    // it. $1180,X is little-endian. A waiting call (op 0x04/0x07) is parked on its own op (61A4 `TYX / CLC`);
    // the port holds the IP past it in s->addr and the call in callAddr, as the --vmdiff compare reads it.
    { uint32_t ipw = (uint32_t)((ssParked(s) ? s->callAddr : s->addr) + CTW_IPOFF(sc));
      w8(sc->mem, 0x7E1180 + (uint32_t)n * 2, ipw & 0xFF);
      w8(sc->mem, 0x7E1181 + (uint32_t)n * 2, (ipw >> 8) & 0xFF); }
    if (sc->rebootReq) return;                          // op 0xDF/0xE1 never returned to the runner (C0/3B71)
    if (sc->sndPosted) {                                // this visit's JSL $C7:0004 was priced
      sc->sndPosted = 0;
      if (sc->sndFreezeFrames > 0) {                    // frames lost: suspend here, resume on the return frame
        sc->sndWait = sc->sndFreezeFrames; sc->sndResumeN = qi + 1; sc->sndResumeObj = n;
        sc->sndEndLine = sc->rasterLine; memcpy(sc->sndElig, elig, sizeof elig);
        memcpy(sc->sndOrder, order, sizeof order); sc->sndNq = nq;
        sc->sndSavedAddr = s->addr; s->addr = sc->sndOpAddr;   // the ip commits when the visit ends, on the return frame
        { uint32_t ipw = (uint32_t)(s->addr + CTW_IPOFF(sc));  // ...so the $1180,X mirror commits it too
          w8(sc->mem, 0x7E1180 + (uint32_t)n * 2, ipw & 0xFF);
          w8(sc->mem, 0x7E1181 + (uint32_t)n * 2, (ipw >> 8) & 0xFF); }
        sc->sndFrozenTick = 1; return;
      }
      if (!vmRuleIfast() && sc->rasterLine + SND_TAIL_LINES >= 225) sc->sndPostResumeFreeze = 1;   // same frame, but the tail will overrun the NMI
    }
  }
  // The party trail, in the ROM's slot: it is the PCs' op 0xB0 (C0/3002 -> 9E29 / A26B / A2CE), part of the runner
  // above; its $1900/$1980/$1600/$1680 stores are consumed by C0/A810 below (AAFD integrate, the C98A anim leg)
  // and by the drain.
  // The lead's step, at C0/A810 (AB06 `LDA $1A01,X / BEQ`): the host moved him before the pass (field.c, C0/881E's
  // step); it stands only on a ticket his B0 armed this pass (vmLeadStepAtA810). A song upload that parks the pass
  // after his B0 keeps the ticket to the return frame (CtSceneMainLoopParked).
  vmLeadStepAtA810(sc, 0);
  CtSceneUpdateFollowers(sc);
  vmLeadStepAtA810(sc, 1);
  g_leadFromOk = 0;
  // C0/A810's object loop is unconditional: A819 walks every index every frame, dispatching on $1100,X (A81E
  // CMP #03 / A822 CMP #08 / A826 CMP #07), with no movement-ticket gate. Mode 7 goes to A86B `JSR C0/A947`,
  // the only writer of a mode-7 object's activation latch (the A88D pass skips modes 7/8), so an object that
  // never moves still gets it (disasm/notes/field_trigger_latch_a947.md).
  // C0/A947 is A88D's camera-window test without the clamps: $C3 = $1D0A-4, $C4 = $1D0C+4, $C5 = $1D0E (no -2),
  // $C6 = $1D10+5; A97E `LDA #80 / STA $0F00,X` inside, A985 `STZ $0F00,X` outside.
  // Note: from obj1. obj0's slot $0F00 is aliased with the draw list (C0/A9C1 writes it as `$0E81,X`, X = $7F),
  // and the activate scan never probes obj0 (C0/1D21 `DEX / DEX / BNE`; field_action_probe.md).
  for (int n = 1; n < sc->nobj; n++) {
    if (sc->mode[n] != 7 || !sc->actors[n].hasPos) continue;
    int c3 = (sc->mem->s[0x1D0A] - 4) & 0xFF, c4 = (sc->mem->s[0x1D0C] + 4) & 0xFF;
    int c5 = sc->mem->s[0x1D0E] & 0xFF,       c6 = (sc->mem->s[0x1D10] + 5) & 0xFF;
    int tx = ((int)sc->fx[n] / 16 * 2) & 0xFF;       // A966 LDA $1801,X / ASL A: the tile, doubled
    int ty = ((int)sc->fy[n] / 16 * 2) & 0xFF;       // A972 LDA $1881,X / ASL A
    // A947's four compares are signed, and unlike A88D it does not clamp the edges:
    //   A96A CMP $C3 / A96C BMI ->A985      A96E CMP $C4 / A970 BPL ->A985
    //   A976 CMP $C5 / A978 BMI ->A985      A97A CMP $C6 / A97C BPL ->A985
    // BMI/BPL read the N flag of the 8-bit difference. A88D's test (A8D3-A8E3) is BCC/BCS, unsigned, which is
    // correct only because A8A3-A8C7 saturate its edges first. With the camera at the map origin `$1D0A - 4`
    // wraps to 0xFC, which the signed test still handles.
    #define CT_S8(v) ((int)(int8_t)(uint8_t)(v))
    int in = CT_S8(tx - c3) >= 0 && CT_S8(tx - c4) < 0 && CT_S8(ty - c5) >= 0 && CT_S8(ty - c6) < 0;
    #undef CT_S8
    w8(sc->mem, 0x7E0F00 + (uint32_t)n * 2, in ? 0x80 : 0x00);
  }
  for (int n = 0; n < sc->nobj; n++)                 // run_tasks: integrate movement (ROM C0/AA07)
    if (CtVmMoveTrace(sc, n, 0xFF, 0, sc->hasTask[n] && sc->cycles[n] > 0 && sc->actors[n].hasPos
                                      ? "INTEG" : "skip") &&
        sc->hasTask[n] && sc->cycles[n] > 0 && sc->actors[n].hasPos) {
      double nfx = sc->fx[n] + sc->mx[n], nfy = sc->fy[n] + sc->my[n];
      int ntx = (int)nfx >> 4, nty = (int)nfy >> 4;   // the tile the step would move into
      // Per-frame dispatch C0/A819 on the move mode $1100,X (sc->mode[n]), four arms (A81E-A82A):
      //   mode < 3        -> C0/AAFD: plain 16-bit add of the velocity, always smooth, never reads
      //                      $7F0B00, crosses tiles freely (lead PC 0, follower PCs 1/2).
      //   mode 3..6       -> C0/AA07: the $7F0B00 (b00) clamp/free lifecycle below (NPCs, sparkles).
      //   A822  CMP #08 / BEQ ->A83E   mode 8 -> skipped entirely, no integrate
      //   A826  CMP #07 / BEQ ->A86B   mode 7 -> JSR C0/A947 only, no integrate (even with a live ticket)
      // No mover gets a per-frame map test: the map collision and AE27 random bounce are in C0/ACFD, run once
      // per op-move visit (moveOp).
      int st = sc->mode[n];
      if (st == 8) continue;                           // A822
      if (st == 7) continue;                           // A826 -> A86B: handled by the pass above,
                                                       //   which runs whether or not there is a ticket
      { uint32_t X2 = (uint32_t)n * 2;
        if (r8(sc->mem, 0x7E1B80 + X2)) {
          // C0/AAC7: both integrators open with `LDA $1B80,X / BEQ` (AA09 in AA07, AAFF in AAFD) and take this arm when
          // it is set (op 0x7A's jump). The ticket counts down, X += the 16-bit $1900, the 8-bit $1980 takes $1B81 with
          // its carry into $1981 (the gravity), and Y += the 16-bit $1980. Done on the bytes; the C position follows.
          int wx = (int)(sc->fx[n] * 16.0) & 0xFFFF, wy = (int)(sc->fy[n] * 16.0) & 0xFFFF;
          sc->cycles[n]--; w8(sc->mem, 0x7E1A01 + X2, sc->cycles[n]);                 // AACD DEC A / STA $1A01,X
          wx = (wx + r16(sc->mem, 0x7E1900 + X2)) & 0xFFFF;                            // AAD1-AADA (16-bit)
          int yf = r8(sc->mem, 0x7E1980 + X2) + r8(sc->mem, 0x7E1B81 + X2);             // AADF-AAE3
          if (yf > 0xFF) w8(sc->mem, 0x7E1981 + X2, r8(sc->mem, 0x7E1981 + X2) + 1);   // AAE6 BCC / AAE8 INC $1981,X
          w8(sc->mem, 0x7E1980 + X2, yf & 0xFF);                                       // AAEB
          wy = (wy + r16(sc->mem, 0x7E1980 + X2)) & 0xFFFF;                            // AAEE-AAF7 (16-bit)
          sc->fx[n] = wx / 16.0; sc->fy[n] = wy / 16.0;
          continue;
        } }
      if (st < 3) {                                    // C0/AAFD smooth add: advance both axes unconditionally
        sc->cycles[n]--; sc->fx[n] = nfx; sc->fy[n] = nfy;
        // C0/AAFD's 16-bit add writes $1800/$1880,X directly, so a PC-mode mover is always live in WRAM
        // (battle_entry_camera.md Part D.3; field.c's D8 staging reads $1801/$1881,X as C0/041A does).
        // Mirror both subpixel words here (the reverse of CtSceneSeedFromWram's conversion).
        uint32_t X2 = (uint32_t)n * 2;
        int wx = (int)(sc->fx[n] * 16.0) & 0xFFFF, wy = (int)(sc->fy[n] * 16.0) & 0xFFFF;
        w8(sc->mem, 0x7E1800 + X2, wx); w8(sc->mem, 0x7E1801 + X2, wx >> 8);
        w8(sc->mem, 0x7E1880 + X2, wy); w8(sc->mem, 0x7E1881 + X2, wy >> 8);
        continue;
      }
      // C0/AA07 lifecycle (AA1B-AA27, as move.c): b00 = the $7F0B00 mirror.
      int b = sc->b00[n], clamp;
      if (b == 0) clamp = 1;                           // AA1F: == 0 -> per-axis tile-edge clamp this frame
      else { if (b > 0) sc->b00[n] = 0; clamp = 0; }   // AA23: > 0 -> clear to 0, free; < 0 -> leave, free
      if (clamp) {                                     // the clamp is per axis (aa07Axis X then Y):
        int xc = (ntx != ((int)sc->fx[n] >> 4));       // a crossing axis is held and kills the ticket
        int yc = (nty != ((int)sc->fy[n] >> 4));       // ($1A01 := 0); the non-crossing axis still advances
        if (xc || yc) {                                // this same frame
          if (!xc) sc->fx[n] = nfx;                     // X did not cross -> advance X
          if (!yc) sc->fy[n] = nfy;                     // Y did not cross -> advance Y
          sc->cycles[n] = 0; continue;                 // either crossing zeroes the ticket (1-tile pause)
        }
      }
      sc->cycles[n]--; sc->fx[n] = nfx; sc->fy[n] = nfy;   // free (mode >= 3, b != 0) or no crossing: advance both
    }
  // ---- Position commit (C0/AB25 inside AAFD, aa07Axis inside AA07) ----
  // The ROM has one position storage: $1800/$1880,X, written by the integrate arm before A88D and the legs read it
  // (C0/AB45 computes the screen position the draw list is keyed on). The port keeps the live value in sc->fx/fy
  // and commits every positioned object here. Committing all before the legs is equivalent to the ROM's
  // per-object interleave: the integrate touches only per-object tables, never the draw list or the camera.
  for (int n = 0; n < sc->nobj; n++) {
    if (!sc->actors[n].hasPos) continue;                  // never placed: the ROM has no position for it either
    uint32_t X2 = (uint32_t)n * 2;
    int wx = (int)(sc->fx[n] * 16.0) & 0xFFFF, wy = (int)(sc->fy[n] * 16.0) & 0xFFFF;
    w8(sc->mem, 0x7E1800 + X2, wx); w8(sc->mem, 0x7E1801 + X2, wx >> 8);
    w8(sc->mem, 0x7E1880 + X2, wy); w8(sc->mem, 0x7E1881 + X2, wy >> 8);
  }
  dbgFlags();
  if (gEnvRaceMv) { static int last[64]; static int init9; if(!init9){init9=1;for(int o=0;o<64;o++)last[o]=-1;}
    for (int o = 8; o <= 11 && o < sc->nobj; o++) { int s = (int)sc->mspd[o];   // TEST: log racer speed changes
      if (s != last[o]) { fprintf(stderr, "RACE f%d obj%d mspd=%02X pos(%d,%d)\n", gVmTick, o, s & 0xFF, (int)sc->fx[o], (int)sc->fy[o]); last[o] = s; } } }
  for (int n = 0; n < sc->nobj; n++)                     // op 0x88 palette rotation: advance the phase
    if (sc->palRot[n].active && ++sc->palRot[n].ctr >= sc->palRot[n].rate) {   // one step every `rate` frames
      sc->palRot[n].ctr = 0;                              // ROM FD/E532 cyclic-shifts span+1 colours
      sc->palRot[n].phase = (sc->palRot[n].phase + 1) % (sc->palRot[n].span + 1);
    }
  // (The iris is WRAM: $0139..$013D and the window tables, stepped by C0/21E1 before the pass: ctIrisFrame.)
  // ---- $0F00 activation pass (C0/A810 + C0/A88D) ----
  // $0F00,X is the per-frame camera-window activation latch, not a script hide flag. The activate scan C0/1CFC
  // and the touch scan C0/9923 skip an object whose $0F00,X is 0. Script visibility is $1A81 (ops 0x90/0x91/0x7E).
  // Class filter, C0/A81B:  CMP #03 BCC ->A870 (classes 0-2, the PCs: AAFD then the same A88D test)
  //                         CMP #08 BEQ ->A83E (class 8: skipped, $0F00 untouched)
  //                         CMP #07 BEQ ->A86B (class 7: JSR C0/A947 only, no window test here)
  //                         otherwise A82A: AA07 then A88D.
  // Window test, C0/A88D:   deactivate if $1100,X bit 7, or $1A81,X == 0, or $1A81,X bit 7;
  //                         else $C3 = $1D0A-4, $C4 = $1D0C+4, $C5 = $1D0E-2, $C6 = $1D10+5, each
  //                         saturating (0 on borrow at A8A3/A8BB, 0xFF on carry at A8AF/A8C7); then
  //                         `LDA $1801,X / ASL A` (the tile, doubled into camera units) must be >= $C3 and
  //                         < $C4, and the same for $1881,X vs $C5/$C6.
  // A8E5's "already live" check (BMI ->A90E) only skips the one-shot setup ($0E80/$0E81, $0F01 = 0xFF, the
  // palette/list calls), so the latch is a pure function of the state above, with no hysteresis.
  {
    Mem* m = sc->mem;
    int c3 = m->s[0x1D0A] - 4; if (c3 < 0) c3 = 0;              // A89F + A8A3 clamp
    int c4 = m->s[0x1D0C] + 4; if (c4 > 0xFF) c4 = 0xFF;        // A8AB + A8AF clamp
    int c5 = m->s[0x1D0E] - 2; if (c5 < 0) c5 = 0;              // A8B7 + A8BB clamp
    int c6 = m->s[0x1D10] + 5; if (c6 > 0xFF) c6 = 0xFF;        // A8C3 + A8C7 clamp
    for (int o = 0; o < sc->nobj; o++) {
      uint32_t X = 0x7E0000u + (uint32_t)o * 2;
      int cls = r8(m, X + 0x1100);
      if (cls == 7 || cls == 8) continue;                       // A824/A828: this pass never writes them
      int vis = r8(m, X + 0x1A81);
      int live = !(cls & 0x80) && vis != 0 && !(vis & 0x80);
      // A89B-A8C9 store the four window edges into the direct page ($C3-$C6, D = $0100), 8-bit each, for every
      // object that clears the two gates (A88F and A894 branch to A910 before the stores). C0/B788's emit
      // overwrites $C3/$C5 as words, so the frame ends on the last emit's values; with the legs off no emit
      // runs, so these are written only with the legs on.
      if (live && legsOn()) {                                   // == cleared both gates (see `live` above)
        w8(m, 0x7E01C3, (uint8_t)c3); w8(m, 0x7E01C4, (uint8_t)c4);     // A8A5 / A8B1
        w8(m, 0x7E01C5, (uint8_t)c5); w8(m, 0x7E01C6, (uint8_t)c6);     // A8BD / A8C9
      }
      if (live) {
        int tx = (r8(m, X + 0x1801) << 1) & 0xFF;              // A8D0: LDA $1801,X / ASL A (8-bit): the tile
        int ty = (r8(m, X + 0x1881) << 1) & 0xFF;              // A8DC: LDA $1881,X / ASL A    byte
        live = (tx >= c3 && tx < c4 && ty >= c5 && ty < c6);    // A8D1-A8E3: BCC/BCS out
      }
      // Palette edges. C0/A88D has two exits, chosen by reading this latch before overwriting it: A8E5
      // `LDA $0F00,X / BMI ->A90E` skips assign when already active, so assign is the rising edge (A905, the
      // only caller of C0/7170 here); A910 `LDA $0F00,X / BPL ->A934` skips release when already inactive, so
      // release is the falling edge at A91B.
      int prev = r8(m, X + 0x0F00);
      uint8_t* R = m->s;                                        // the flat 128 KB image the legs index
      uint32_t idx = (uint32_t)o * 2;                           // every per-object table is obj*2 strided
      if (!legsOn()) {                                          // legs off: the latch + palette edges only
        w8(m, X + 0x0F00, live ? 0x80 : 0x00);                  // A8F2 / A916 (STZ $0F00,X)
        if (live && !(prev & 0x80))      palAlloc7170(m, o);    // A8E5 rising  -> A905
        else if (!live && (prev & 0x80)) palRelease734C(m, o);  // A910 falling -> A91B
        continue;
      }
      if (live) {
        // A8E5 `LDA $0F00,X / BMI ->A90E`: the previous latch is the edge detector, so this block is the
        // rising edge's one-shot and A90E's `SEC / RTS` is the "already active" arm.
        if (!(prev & 0x80)) {
          w8(m, X + 0x0E80, 0x80);                              // A8EC  prev-link := none
          w8(m, X + 0x0E81, 0x80);                              // A8EF  next-link := none
          w8(m, X + 0x0F00, 0x80);                              // A8F2  the latch itself
          w8(m, X + 0x1681, 0x00);                              // A8F7  anim phase
          w8(m, X + 0x1601, 0x00);                              // A8FA  anim timer
          w8(m, X + 0x1B00, 0x00);                              // A8FD  the staging counter C0/B701 reads
          w8(m, X + 0x0F01, 0xFF);                              // A902  the "no frame yet" sentinel C0/CAD9 gates on
          palAlloc7170(m, o);                                   // A905  JSR C0/7170  the OBJ palette allocator
          CtObjSlotAlloc(R, (int)idx);                          // A908  JSR C0/6F9A  the OBJ slot allocator
          CtObjListLink(R, (int)idx);                           // A90B  JSR C0/A9CD  link into the draw list
        }
        // Back in A810, a set carry runs the four legs (A832-A83B, and A878-A881 on the state < 3 arm). The
        // unlink/relink bracket AB45 because the bucket is keyed on the screen Y: the object leaves the bucket
        // of its old $0A80 before AB45 overwrites it, and re-enters on the new one.
        CtAnimTimerTick(R, gRomImg, (int)gRomImgLen, (int)idx);  // A832  JSR C0/C98A
        CtObjListUnlink(R, (int)idx);                            // A835  JSR C0/A98A
        CtObjScreenPos(R, (int)idx);                             // A838  JSR C0/AB45
        CtObjListLink(R, (int)idx);                              // A83B  JSR C0/A9CD
      } else {
        // A910 `LDA $0F00,X / BPL ->A934`: the falling edge. It tears down the arena run, the palette, the OBJ
        // slot and the draw-list link, and (A934, on both arms) forces $7F0B01,X back to 2.
        if (prev & 0x80) {
          w8(m, X + 0x0F00, 0x00);                              // A915  STZ $0F00,X
          CtArenaFreeRun(R, (int)idx);                           // A918  JSR C0/EA42 (the whole consecutive run)
          palRelease734C(m, o);                                  // A91B  JSR C0/734C
          CtObjSlotFree(R, (int)idx);                            // A91E  JSR C0/7056
          CtObjListUnlink(R, (int)idx);                          // A921  JSR C0/A98A
          // A924-A932: `LDA $7F0A00,X / BEQ ->A934 / LDA $30 / BNE ->A934 / LDA #0D / STA $29` (DP = $0100).
          if (r8(m, 0x7F0A00 + idx) != 0 && r8(m, 0x7E0130) == 0) w8(m, 0x7E0129, 0x0D);
        }
        // A934 `LDA $7F0B01,X / BEQ ->A940 / LDA #02 / STA $7F0B01,X`: the blocking-anim busy byte (op
        // 0xAB/0xB7). An off-camera object's "playing" state is forced back to 2, so it never reaches the
        // completion C0/2EA3 reads. Runs on both A910 arms.
        // Note: on the flat image, not r8/w8, which stop at $7F:03FF (WM_LEN); CtAnimTimerTick's CA4F arm
        // writes the same buffer.
        if (R[0x10B01 + idx] != 0) R[0x10B01 + idx] = 0x02;
      }
    }
    // A846-A867, the party re-link tail, run once after the loop's closing `BMI ->A819`. `LDA $99` / `LDA $97`
    // read the party-slot table ($0199 = member 2, $0197 = the lead; 0x80 = empty), `TAX` makes the byte the
    // obj*2 index, and each is unlinked and relinked if its activation latch is non-zero, re-seating the PCs in
    // their bucket chain every frame. Slot 3 ($019B) is not in the tail (the ROM's own asymmetry).
    { static const uint32_t kSlot[2] = { 0x7E0199, 0x7E0197 };   // A846 then A858, in that order
      for (int k = 0; legsOn() && k < 2; k++) {
        int b = r8(m, kSlot[k]);
        if (b & 0x80) continue;                                  // A848 / A85A  BMI -> skip
        if (r8(m, 0x7E0F00 + (uint32_t)b) == 0) continue;         // A84B / A85D  BEQ -> skip
        CtObjListUnlink(m->s, b); CtObjListLink(m->s, b);         // A852/A855, A864/A867
      } }
    // ---- C0/00D6 `JSR C0/CA76`: the drain, once per frame ----
    // C0/CA76 is called only from 00D6 (not per object; the per-object visit C0/CAD9 is inside it).
    // The pass's deadline is the virtual beam (scanline -1; move.c CtAnimDrainPass, src/ct_vbeam.h; see CLAUDE.md,
    // the zelda3 model, and disasm/notes/anim_drain_cad9.md sec. 16): the V the ROM reads at CA88/CA8C advances with
    // the pass's visits and CA99 stops it at line $6B, two visits a pass. The oracle reads the same V under
    // CT_ORACLE_VIRTUAL_BEAM.
    // CT_DRAINQTRACE=1: one `drainq` line per pass on stderr -- the location, the queue (object numbers, $76 through
    // the $1081,X links) before and after, and the visits; CT_DRAINQTRACE_ADDR=<hex>:<hexlen>,... appends WRAM bytes.
    if (legsOn()) {
      static int qt = -1; if (qt < 0) qt = getenv("CT_DRAINQTRACE") ? 1 : 0;
      char qb[192]; int qn = 0;
      if (qt) { int x = m->s[0x0176], n = 0; qn = snprintf(qb, sizeof qb, "drainq loc %03X q[", m->s[0x0100] | (m->s[0x0101] << 8));
                while (!(x & 0x80) && n++ < 64 && qn < 150) { qn += snprintf(qb + qn, sizeof qb - qn, " %d", x / 2); x = m->s[0x1081 + x]; } }
      int dv = CtAnimDrainPass(m->s, gRomImg, (int)gRomImgLen, -1);   // C0/CA76 on the virtual beam
      if (qt) { int x = m->s[0x0176], n = 0; const char* al = getenv("CT_DRAINQTRACE_ADDR");
                fprintf(stderr, "%s ] visits %d -> [", qb, dv);
                while (!(x & 0x80) && n++ < 64) { fprintf(stderr, " %d", x / 2); x = m->s[0x1081 + x]; }
                fprintf(stderr, " ]");
                while (al && *al) { char* e; long ad = strtol(al, &e, 16), ln = 1; if (*e == ':') ln = strtol(e + 1, &e, 16);
                                    fprintf(stderr, " %04lX=", ad);
                                    for (long i = 0; i < ln && ad + i < 0x20000; i++) fprintf(stderr, "%02X", m->s[ad + i]);
                                    al = (*e == ',') ? e + 1 : NULL; }
                fprintf(stderr, "\n"); }
    }
    // ---- C0/00DB `BRL C0/B271`: the draw pass ----
    // In the same tick because C0/B701, the arena release, is reached only from C0/B309 inside it; without it
    // the four slots are never handed back. See CtAnimDrawPass for what it does and does not port.
    if (legsOn()) CtAnimDrawPass(m->s, NULL);
    // C0/00C7 `JSL FD/C2C1`, after the runner: builds one of the two HDMA table sets ($0153 bit 0 clear -> set B,
    // $7F:1238-14F0, through ($C2E5,X); set -> set A, $7F:0F80-11E1, through ($C2DF,X)) and flips the bit (C2CE
    // TSB / C2DA TRB), so $0153 alternates every frame. The NMI's FD/C1EE programs the set the bit names (`AND #0F
    // / BEQ` -> set A); the window wipe's picture depends on it (field.c fieldWipeWindow).
    CtHdmaTableBuild(m->s);
    // C0/00CD `JSL FD/FFF7` (-> FD/E39C): one frame of the twelve palette-effect records $0520..$05AF (op 0x88's
    // `88 4x` fade to white, `88 5x` scale), writing the CGRAM page $7E:2200 the NMI uploads; the same body the
    // battle's frame service ticks (src/objslot.c). Also the location's own colour animation, modes 1/2/3/8
    // (records 0-5, FD/E292 at the load), with op 0x88's `88 20` / `88 30` rotations (field_bg_anim.md sec. 3).
    ctObjSlotFrame(sc);
  }
  ctXferAfterVm(sc);
}

int CtSceneActorCount(const CtScene* sc) { return sc->nobj; }
void CtSceneActor(const CtScene* sc, int obj, int* placed, int* sprite, int* x, int* y, int* facing) {
  const CtVmActor* a = &sc->actors[obj];
  // The render predicate is $0F00, not `draw`. C0/B0FC's `JSR C0/B127 / BCC ->B11A` decides whether an object
  // is linked into the draw list, and C0/B127 wants both $1A81 shown/not removed and the camera window;
  // $0F00,X is C0/A810's latch of that pair (also used by npcSolidTile, C0/5BC3, and CtSceneActorTouchable).
  // `draw` keeps the $1A81 sense for CtSceneActorHidden ("did a script hide it").
  *placed = a->defined && !a->dead && a->hasPos
            && r8((Mem*)sc->mem, 0x7E0F00 + (uint32_t)obj * 2) != 0;
  // The ROM's gate has no palette-op term: C0/A88D, C0/B127 and the OAM drain C0/CAD9 test only $1100,X bit 7,
  // $1A81,X in 1..7F and the camera window. A colour-gated swirl is hidden by the PPU window (field.c's iris
  // clip, keyed on CtSceneActorColorGated).
  *sprite = a->sprite; *x = (int)sc->fx[obj]; *y = (int)sc->fy[obj]; *facing = a->facing;
}
// Op 0x88 palette rotation for actor `obj`. Returns 1 and fills the rotation (start colour index, span,
// current phase) if active; a colour-gated object with no rotation of its own inherits the scene's active one.
int CtSceneActorPalRot(const CtScene* sc, int obj, int* start, int* span, int* phase) {
  if (obj < 0 || obj >= sc->nobj) return 0;
  int e = obj;
  if (!sc->palRot[obj].active && sc->actors[obj].colorGated)
    for (int j = 0; j < sc->nobj; j++)
      if (sc->actors[j].colorGated && sc->palRot[j].active) { e = j; break; }
  if (!sc->palRot[e].active) return 0;
  *start = sc->palRot[e].start; *span = sc->palRot[e].span; *phase = sc->palRot[e].phase;
  return 1;
}
// Op 0x88 blob palette overwrite for actor `obj`. Returns the colour count (0 = none) and fills start and
// the BGR555 colours; a colour-gated object shares the emitter's blob.
int CtSceneActorPalBlob(const CtScene* sc, int obj, int* start, uint16_t* bgr) {
  if (obj < 0 || obj >= sc->nobj) return 0;
  int e = obj;
  if (sc->palBlob[obj].n == 0 && sc->actors[obj].colorGated)
    for (int j = 0; j < sc->nobj; j++)
      if (sc->actors[j].colorGated && sc->palBlob[j].n) { e = j; break; }
  if (sc->palBlob[e].n == 0) return 0;
  *start = sc->palBlob[e].start;
  for (int i = 0; i < sc->palBlob[e].n; i++) bgr[i] = sc->palBlob[e].bgr[i];
  return sc->palBlob[e].n;
}
int CtSceneActorColorGated(const CtScene* sc, int obj) {   // 1 = an op 0x88 colour-gated sprite
  return (obj >= 0 && obj < sc->nobj) ? sc->actors[obj].colorGated : 0;
}
// Write an actor's live position. field.c calls this every free-control frame for the lead so the VM
// object tracks the input-driven player and a later script that drives it starts from there.
void CtSceneSetActorPos(CtScene* sc, int obj, int x, int y) {
  if (obj < 0 || obj >= sc->nobj) return;
  sc->fx[obj] = x; sc->fy[obj] = y;
  sc->actors[obj].x = x; sc->actors[obj].y = y; sc->actors[obj].hasPos = 1;
}
// Subpixel variant (battle_ai_interp.md sec. 12): the per-frame lead sync must not floor. The ROM has one
// position storage ($1800/$1880, 1/16 px), and the player mover's fraction carries into any scripted walk
// that takes over (e.g. the D9 staging walk's step count).
void CtSceneSetActorPosF(CtScene* sc, int obj, double x, double y) {
  if (obj < 0 || obj >= sc->nobj) return;
  sc->fx[obj] = x; sc->fy[obj] = y;
  sc->actors[obj].x = (int)x; sc->actors[obj].y = (int)y; sc->actors[obj].hasPos = 1;
}
// Right after CtSceneCreate: did the lead's own fn0 write $1800/$1880 after its arrival arm (pcLeadInitArm)?
int CtSceneLeadScriptPlaced(const CtScene* sc, int obj, double* x, double* y) {
  if (!sc || obj < 0 || obj >= sc->nobj || !sc->actors[obj].hasPos) return 0;
  if (x) *x = sc->fx[obj];
  if (y) *y = sc->fy[obj];
  return 1;
}

void CtSceneDebugMakeFollower(CtScene* sc, int obj, int member, int charId) {
  if (!sc || obj < 0 || obj >= sc->nobj || member < 1 || member > 2) return;
  w8(sc->mem, 0x7E2980 + member, charId & 0xFF);      // register in the active party (op 0xD3 effect)
  sc->mode[obj] = member; sc->actors[obj].moveMode = member;   // move-mode 1/2 = follower (op 0x5C effect)
  sc->st[obj].addr = -1;                                // idle script -> CT_FOL_ROAMS true (the follow runs)
  sc->hasTask[obj] = 0; sc->cycles[obj] = 0; sc->vecMove[obj] = 0;   // drop any pending scripted move
  sc->actors[obj].draw = 0; sc->actors[obj].defined = 1;   // solid is kept as is, like op 0x5C: the lead's
                                                       // collision excludes followers
  int lead = sc->playerObj;                             // place it on the lead (the join start) if unplaced
  if (!sc->actors[obj].hasPos && lead >= 0 && lead < sc->nobj && sc->actors[lead].hasPos)
    CtSceneSetActorPos(sc, obj, (int)sc->fx[lead], (int)sc->fy[lead]);
  sc->actors[obj].hasPos = 1;
}

// CT_FOL_DELAY / CT_FOL_CLAMP are unused: the follower model below is the ROM's velocity ring.
#define CT_FOL_DELAY 23
#define CT_FOL_CLAMP 15
// ---- Party followers: the ROM's velocity ring (disasm/notes/party_follower_ring.md sec. 2, 8) ----
// C0/9E3F, the push, once per frame the lead moves: the head $01AB steps (0x80 entries, AND #7F) and the entry takes
// the lead's velocity $1D32/$1D33 ($7F0C00/0C80), the pre-adjust pair $1D2C/$1D2D ($7F0D00/0D80), both priority
// flags ($7F0E00/0E80) and the shape byte $1D34 ($7F0F00).
// C0/A26B (member 2, cursor $AC) / C0/A2CE (member 3, cursor $AD, gap measured against $AC): no velocity by default;
// when the gap to the one in front is >= 0x18 (lead moving) or >= 0x10 (lead stopped), A29D advances the cursor one
// entry and copies its velocity and priority flags. The follower's position is never written: the integrator
// (C0/AAFD) moves it, so its subpixel fraction survives the walk.
// The facing and animation arm: C0/9ED1 / 9F20 after an advance, C0/9F6F on a hold, all ending in `JSR ($9FF7,X)`,
// the lead's C0/9E84 table (move.c CtAnimDispatch9FF7), with the index from the entry's pre-adjust pair (advance)
// or (pre-adjust - adj(velocity)) per axis (hold; 0 for an ordinary entry -> the idle chain A1E9).
// Open question (dossier sec. 6): what makes $1D32|$1D33 nonzero on a frame a script, not input, moves the lead;
// the push's `leadStepped` term below stands in for part of it.
static void ctFollowerAnimArm(CtScene* sc, int o, int cur, int advanced) {
  uint8_t* w = sc->mem->s; int y = o * 2;
  const uint8_t* rom = gRomImg; int romLen = (int)gRomImgLen;
  w[0x01E5] = 0;                                              // 9ED1 / 9F20 / 9F6F  STZ $E5
  if (!rom || romLen < 0x10000) { static int once; if (!once++) fprintf(stderr, "[fol] no ROM image: C0/9F6F's anim arm cannot run\n"); return; }
  int shape = w[0x10F00 + cur];                               // LDA $7F0F00,X
  if (advanced) {
    if (shape != 0 && (shape & 0x04)) {                       // 9ED9 BEQ ->9EFC / 9EDB BIT #04 / BNE ->9EE1
      if (w[0x10D80 + cur] == 0) { w[0x1681 + y] = 0; w[0x1601 + y] = 0; return; }   // 9EE1-9EE5 -> 9EF1-9EFB
      CtAnimModeSet(w, y, 0x1B); return;                      // 9EE7-9EEE: fold, CMP #18, BRL ->A254 (unconditional)
    }
    int bx = w[0x10D00 + cur];                                // 9EFC LDA $7F0D00,X
    if (bx & 0x80) bx = ((bx ^ 0xFF) + 0x21) & 0xFF;          // 9F00 BPL / 9F02 EOR #FF / CLC / ADC #21
    int sh = (bx >> 3) & 0xFF; w[0x01DB] = (uint8_t)sh;       // 9F07 LSR x3 / 9F0A STA $DB (advance: the pre-adjust pair)
    int by = w[0x10D80 + cur];                                // 9F0C LDA $7F0D80,X
    if (by & 0x80) by = ((by ^ 0xFF) + 0x21) & 0xFF;          // 9F10 BPL / 9F12 EOR #FF / CLC / ADC #21
    CtAnimDispatch9FF7(w, rom, romLen, y, (by | sh) & 0x7E);  // 9F17 ORA $DB / AND #7E / TAX / JSR ($9FF7,X)
    return;
  }
  if (shape == 0) { CtAnimDispatch9FF7(w, rom, romLen, y, 0); return; }   // 9F75 BEQ ->9FF2 = slot 0 = LDX $6D / BRL ->A1E9
  if (shape & 0x04) {                                         // 9F77 BIT #04 / 9F79 BNE ->9FD3
    int d = w[0x10D80 + cur], v = w[0x10C80 + cur];
    if (d == 0 || v == 0 || ((v - d) & 0xFF) == 0) { w[0x1681 + y] = 0; w[0x1601 + y] = 0; }   // 9FD3-9FE4 -> 9FE7
    return;                                                   // 9FE6 RTS
  }
  int ax = w[0x10C00 + cur];                                  // 9F7B LDA $7F0C00,X (hold: adj() moves a bit-3 byte 8 from 0)
  if (ax & 0x80) { if (ax & 0x08) ax = (ax - 8) & 0xFF; }     // 9F7F BPL / 9F81 BIT #08 / 9F85 SEC / SBC #08
  else           { if (ax & 0x08) ax = (ax + 8) & 0xFF; }     // 9F8A BIT #08 / 9F8E CLC / ADC #08
  int tx = ((ax ^ 0xFF) + 1 + w[0x10D00 + cur]) & 0xFF;       // 9F91 EOR #FF / INC A / CLC / ADC $7F0D00,X
  if (tx & 0x80) tx = ((tx ^ 0xFF) + 0x21) & 0xFF;            // 9F99 BPL / 9F9B EOR #FF / CLC / ADC #21
  int sh = (tx >> 3) & 0xFF; w[0x01DB] = (uint8_t)sh;         // 9FA0 LSR x3 / 9FA3 STA $DB
  int ay = w[0x10C80 + cur];                                  // 9FA5 LDA $7F0C80,X
  if (ay & 0x80) { if (ay & 0x08) ay = (ay - 8) & 0xFF; }     // 9FA9-9FB2
  else           { if (ay & 0x08) ay = (ay + 8) & 0xFF; }     // 9FB4-9FB9
  int ty = ((ay ^ 0xFF) + 1 + w[0x10D80 + cur]) & 0xFF;       // 9FBB EOR #FF / INC A / CLC / ADC $7F0D80,X
  if (ty & 0x80) ty = ((ty ^ 0xFF) + 0x21) & 0xFF;            // 9FC3 BPL / 9FC5 EOR #FF / CLC / ADC #21
  CtAnimDispatch9FF7(w, rom, romLen, y, (ty | sh) & 0x7E);    // 9FCA ORA $DB / AND #7E / TAX / JSR ($9FF7,X)
}
void CtSceneUpdateFollowers(CtScene* sc) {
  int lead = sc->playerObj;
  if (lead < 0 || lead >= sc->nobj || !sc->actors[lead].hasPos) return;
  uint8_t* w = sc->mem->s;
  // A follower carried in from another scene (op 0x5C on a transition) has its sprite and move mode 1/2 but
  // no position yet: it spawns on the lead, then spreads as the lead walks.
  for (int o = 0; o < sc->nobj; o++)
    if ((sc->mode[o] == 1 || sc->mode[o] == 2) && o != lead && !sc->actors[o].dead && !sc->actors[o].hasPos) {
      sc->fx[o] = sc->fx[lead]; sc->fy[o] = sc->fy[lead];
      sc->actors[o].x = (int)sc->fx[o]; sc->actors[o].y = (int)sc->fy[o]; sc->actors[o].hasPos = 1;
      sc->folRoamPrev[o] = 1;
    }
  // The gate is the follower's own script op. C0/A26B and C0/A2CE have one caller each, C0/3037 and C0/3027 inside
  // C0/3002, which is op 0xB0 (and, through C0/2FFD `JSR 3002 / INX`, op 0xAF):
  //   3002  LDA $38 / BEQ ->3009 / TYX / CLC / RTS      ; $0138 != 0 (the camera align machine, C0/885A)
  //                                                      ;   -> nothing at all, for the lead and the followers
  //   300F  LDA $1100,X / BEQ ->3040 (lead: $1A01:=1, JSR 9E29) / DEC A / BEQ ->3030 ($1A01:=1, JSR A26B)
  //         / DEC A / BEQ ->3020 ($1A01:=1, JSR A2CE) / else CLC RTS
  // A PC's fn0 is `5C 00 B0`: the init Return at prio 7 does `TYX / CLC` (C0/5FB3), so the object sits on the B0
  // and runs it every visit. A follower whose ip a call or op 0xD9 (C0/32F4) moved elsewhere gets nothing, nor
  // does one parked on 0xB2 (C0/3543, a bare yield). Op 0xAF leaves the ip one byte past itself, so a follower
  // whose visit last ran it (atB0) sits right after an 0xAF.
  // Not modelled: a visit the runner's raster budget skipped (C0/5A5D); this pass runs once per frame.
  #define CT_FOL_ON_B0(o) (sc->st[o].addr >= 0 && sc->st[o].addr < sc->blen && (sc->body[sc->st[o].addr] == 0xB0                            || (sc->atB0[o] && sc->st[o].addr >= 1 && sc->body[sc->st[o].addr - 1] == 0xAF)))
  int haveAny = 0;
  for (int o = 0; o < sc->nobj; o++)
    if ((sc->mode[o] == 1 || sc->mode[o] == 2) && o != lead && sc->actors[o].hasPos && !sc->actors[o].dead)
      { haveAny = 1; break; }
  if (!haveAny) return;                                 // solo party: the ring simply is not consulted

  // ---- C0/9E3F, the push. $1D32/$1D33 are the lead's post-collision velocity, published by field.c from
  // CtPlayerControl (the C0/881E port).
  int leadMoving = (w[0x1D32] | w[0x1D33]) != 0;        // A275 LDA $1D32 / ORA $1D33 / BEQ: the threshold's
                                                        // test only
  // The push's own gate is that the lead actually moved, a different test (dossier sec. 5a). The push is entered
  // by a branch whose condition is not decoded; measured, a frame with a nonzero velocity on which the lead did
  // not move pushes nothing.
  int lp0 = (int)(sc->fx[lead] * 16.0), lp1 = (int)(sc->fy[lead] * 16.0);
  int leadStepped = !sc->folLeadSeen || lp0 != sc->folLeadP[0] || lp1 != sc->folLeadP[1];
  sc->folLeadP[0] = lp0; sc->folLeadP[1] = lp1; sc->folLeadSeen = 1;
  if (leadMoving && leadStepped) {
    int h = (w[0x01AB] + 1) & 0x7F;                     // 9E42 INC A / AND #7F
    w[0x01AB] = (uint8_t)h;
    w8(sc->mem, 0x7F0C00 + h, w[0x1D32]);  w8(sc->mem, 0x7F0C80 + h, w[0x1D33]);
    w8(sc->mem, 0x7F0D00 + h, w[0x1D2C]);  w8(sc->mem, 0x7F0D80 + h, w[0x1D2D]);
    w8(sc->mem, 0x7F0E00 + h, w[0x0C00 + lead * 2]);
    w8(sc->mem, 0x7F0E80 + h, w[0x0C01 + lead * 2]);
    w8(sc->mem, 0x7F0F00 + h, w[0x1D34]);
    w[0x1900 + lead * 2] = w[0x1D32]; w[0x1980 + lead * 2] = w[0x1D33];   // 9E53/9E5D: the lead too
  }
  if (g_daRingPend) {                                   // an op 0xDA reset this pass, after the lead's visit (g_daRingPend)
    unsigned hd = w[0x01AB] & 0x7F;                     // 347D LDA $AB / STA $AC / STA $AD, on the head the push left
    w[0x01AC] = (uint8_t)hd; w[0x01AD] = (uint8_t)hd;
    w8(sc->mem, 0x7F0C00 + hd, 0); w8(sc->mem, 0x7F0D00 + hd, 0); w8(sc->mem, 0x7F0F00 + hd, 0);   // 3486-3496
    g_daRingPend = 0;
  }

  // ---- C0/A26B / C0/A2CE, the two followers, in the pass's order (not the chain's). They run inside each
  // follower's own B0 visit and the runner visits by index, so when member 3 is the lower object it measures its
  // gap against $01AC as it stood before member 2's advance of this frame.
  int folObj[3] = { -1, -1, -1 };
  for (int m = 1; m <= 2; m++)
    for (int k = 0; k < sc->nobj; k++)
      if (k != lead && sc->mode[k] == m && sc->actors[k].hasPos && !sc->actors[k].dead) { folObj[m] = k; break; }
  int folOrder[2] = { 1, 2 };
  if (folObj[1] >= 0 && folObj[2] >= 0 && folObj[2] < folObj[1]) { folOrder[0] = 2; folOrder[1] = 1; }
  for (int oi = 0; oi < 2; oi++) {
    int member = folOrder[oi];
    int curAddr  = 0x01AB + member;                     // $01AC (member 2), $01AD (member 3)
    int prevAddr = 0x01AB + member - 1;                 // ...measured against the one in front
    int o = folObj[member];
    if (o < 0) continue;
    // C0/3002 runs when the op is dispatched. A follower whose visit ended on its B0 without dispatching it (a Return
    // onto the B0 with the five-op budget spent, C0/5AB6) gets no conga that frame; its next visit runs it (landedB0).
    // One that ran its B0 keeps the conga even if a later object in the pass calls it off: A26B / A2CE ran in that
    // visit (cursor, velocity, the 9ED1 arm), and only the step is gone, since the call's accept arm `STZ $1A80,X /
    // STZ $1A01,X` (C0/6031-6034) leaves A810 nothing to integrate.
    int ranThenCalled = g_ranB0[o] && !CT_FOL_ON_B0(o);
    int roaming = ((CT_FOL_ON_B0(o) && !g_landedB0[o]) || ranThenCalled) && w[0x0138] == 0;   // C0/3002 ran, $0138 let it through
    // ...but not a ticket the new function has already issued. On the frame a follower leaves its B0 its new
    // function has had its visit: the request that moved it (op 0x02-0x07's preempt arm, C0/6031+6034) cleared
    // $1A80/$1A01 before that visit, and a move op in it armed a fresh pair, which C0/A810 integrates this same
    // frame. The conga never arms a ticket itself (`hasTask` only drives the walk cycle), so a live `cycles` here
    // is the new function's.
    if (sc->folRoamPrev[o] && !roaming && sc->cycles[o] == 0) {   // script/cutscene takes over: release the walk cycle
      // `hasTask` and `cycles` are one unit, the ROM's $1A80,X (velocity/task) and $1A01,X (step ticket), and are
      // cleared together (3526 STZ $1A80,X / 3529 STZ $1A01,X). Note: `cycles > 0` with `hasTask == 0` deadlocks
      // the mover (moveOp yields on `cycles > 0`, the integrator needs `hasTask`).
      sc->hasTask[o] = 0; sc->cycles[o] = 0;            // 3526 STZ $1A80,X / 3529 STZ $1A01,X
      if (sc->actors[o].animMode == 1) { sc->actors[o].animMode = 0; sc->actors[o].animIndex = 0; sc->actors[o].animGen++; }
    }
    sc->folRoamPrev[o] = roaming;
    if (!roaming) continue;                             // 3004 TYX / CLC / RTS, or the ip is not on 3002 at all
    int vx = 0, vy = 0;                                 // A26D: the default is no velocity
    w[0x1900 + o * 2] = 0; w[0x1980 + o * 2] = 0;       // A26F/A272 (A2D2/A2D5)
    int gap = (w[prevAddr] - w[curAddr]) & 0x7F;        // A27D / A2E0: SBC then AND #7F
    int advanced = gap >= (leadMoving ? 0x18 : 0x10);   // A284 CMP #18 / A294 CMP #10, BCS -> advance
    int c;
    if (advanced) {
      c = (w[curAddr] + 1) & 0x7F;                      // A29D: advance one entry
      w[curAddr] = (uint8_t)c;
      vx = (int8_t)r8(sc->mem, 0x7F0C00 + c);           // A2A7/A2B6: copy that entry's velocity
      vy = (int8_t)r8(sc->mem, 0x7F0C80 + c);
      w[0x1900 + o * 2] = (uint8_t)vx; w[0x1980 + o * 2] = (uint8_t)vy;   // A2AD / A2BA
      w[0x0C00 + o * 2] = (uint8_t)r8(sc->mem, 0x7F0E00 + c);   // A2BD/A2C4: and its priority flags
      w[0x0C01 + o * 2] = (uint8_t)r8(sc->mem, 0x7F0E80 + c);
    } else c = w[curAddr];                              // A288 / A298: LDX $AC, the entry it holds on
    // The facing and animation arm: C0/9ED1 (member 2) / C0/9F20 (member 3) after an advance, C0/9F6F on a hold.
    ctFollowerAnimArm(sc, o, c, advanced);
    ctAnimSyncActor(&sc->actors[o], w, o * 2);          // the C mirrors follow the bytes the arm wrote
    sc->actors[o].facing = w[0x1600 + o * 2] & 3;
    // C0/AAFD, the ordinary mover integrate: `$1800,X += sign-extend($1900,X)` in 12.4; adding sixteenths to the
    // double keeps the subpixel fraction.
    if (ranThenCalled) { sc->folMoving[o] = 0; continue; }   // the call's STZ $1A01: nothing for A810 (above)
    sc->fx[o] += vx / 16.0; sc->fy[o] += vy / 16.0;
    sc->actors[o].x = (int)sc->fx[o]; sc->actors[o].y = (int)sc->fy[o];
    sc->folMoving[o] = (vx || vy);
    sc->hasTask[o] = sc->folMoving[o];                  // drive the walk cycle
    if (getenv("CT_FOLDBG"))
      fprintf(stderr, "[fol] f%d obj%d m%d cur=%02X head=%02X gap=%02X adv=%d v=(%02X,%02X) pre=(%02X,%02X) shape=%02X 1600=%02X 1680=%02X 1681=%02X 1601=%02X 0138=%02X\n",
              gVmTick, o, member, c, w[0x01AB], gap, advanced, vx & 0xFF, vy & 0xFF,
              r8(sc->mem, 0x7F0D00 + c), r8(sc->mem, 0x7F0D80 + c), r8(sc->mem, 0x7F0F00 + c),
              w[0x1600 + o * 2], w[0x1680 + o * 2], w[0x1681 + o * 2], w[0x1601 + o * 2], w[0x0138]);
  }
  if (getenv("CT_RINGDBG")) fprintf(stderr, "[ring] f%d lead=(%.4f,%.4f) 1D32=%02X%02X head=%02X cur=%02X/%02X f1=(%.4f,%.4f) fac=%02X,%02X\n",
      gVmTick, sc->fx[lead], sc->fy[lead], w[0x1D32], w[0x1D33], w[0x01AB], w[0x01AC], w[0x01AD],
      sc->nobj>3?sc->fx[3]:0.0, sc->nobj>3?sc->fy[3]:0.0, w[0x1606], w[0x1608]);
}
// --vmdiff's stand-in for the joypad routine (C2/8500), button byte pair only. Belthasar does not call it: its
// host runs the NMI's whole pad latch (field.c fieldPadNmi -> player.c CtJoypadPoll, $00F0-$00FB).
// $00F2 = held buttons, $00F0 = newly pressed, same layout (A=0x80 X=0x40 L=0x20 R=0x10 B=0x08 Y=0x04
// Sel=0x02 St=0x01). C0/0097 latches $00F0 into $0151 for the poll ops 0x3F-0x44.
// Note: the d-pad pair $00F1/$00F3 is not written. --vmdiff forwards the direction to the oracle and syncs the
// lead from it rather than running the player controller.
void CtSceneSetButtons(CtScene* sc, uint16_t heldF2) {
  if (!sc) return;
  uint8_t held = (uint8_t)(heldF2 & 0xFF);
  w8(sc->mem, 0x7E00F0, (uint8_t)(held & ~sc->btnPrev));   // NEWLY pressed
  w8(sc->mem, 0x7E00F2, held);                             // HELD
  sc->btnPrev = held;
  sc->btnHeld = heldF2;
}
// $01EB, the touch hit latch (see the struct field and op 0xB0). `obj` = the object the player's proximity
// scan overlapped this frame, or -1 for the ROM's 0x80 "nothing". Call once per frame before CtSceneTick, as
// C0/881E runs ahead of the event VM in the main loop.
void CtSceneSetTouchHit(CtScene* sc, int obj) { if (sc) sc->touchHit = obj; }
// C0/597D, the PC-object respawn the post-menu reconcile C0/1A03 runs when $7E:2980-82 changed: re-run one
// object's init function (fn0) and reset its script state, so the PC loader inside it (op 0x80 / 0x57)
// re-reads the party and re-derives its slot and sprite. The body is the init pass's per-object body.
// The caller owns the positions: the ROM restores the snapshotted X/Y/prio into the objects the new
// $0197/$0199/$019B name.
void CtSceneReinitObj(CtScene* sc, int obj) {
  if (!sc || obj < 0 || obj >= sc->nobj || !sc->raw) return;
  int count = sc->nobj;
  int slot0 = (sc->raw[1 + obj * 32] | (sc->raw[1 + obj * 32 + 1] << 8)) - sc->header;
  int stopAddr = -1, stopOp = -1;
  if (slot0 >= 0 && slot0 < sc->blen) {
    int br2[2] = { sc->bright, sc->brightTarget };
    runFunc(sc->body, sc->blen, sc->header, obj, count, slot0, sc->mem, sc->actors, &sc->explore, br2, &stopAddr, &stopOp, sc);
    sc->bright = br2[0]; sc->brightTarget = br2[1];
  }
  CtSS* s = &sc->st[obj];
  memset(s->ret, 0, sizeof s->ret); s->prio = 7; s->tb = 0; s->nm = 0;
  ctSetDc(sc, obj, sc->actors[obj].moveAnim > 0 ? sc->actors[obj].moveAnim : 1);
  s->wait = 0; s->waitObj = -1; s->waitPrio = 0;
  if (stopOp == 0x00) s->addr = stopAddr + 1;
  else if (stopOp == 0xAF || stopOp == 0xB0 || stopOp == 0xB1 || stopOp == 0xB2) s->addr = stopAddr;
  else s->addr = -1;
  sc->mspd[obj] = sc->actors[obj].moveSpeed; sc->cycles[obj] = 0; sc->hasTask[obj] = 0;
  sc->coll[obj] = sc->actors[obj].coll; sc->collMap[obj] = sc->actors[obj].collMap;
  sc->b00[obj] = 0; sc->c1C81[obj] = sc->actors[obj].snap; sc->moveAngle[obj] = -1;
  sc->folRoamPrev[obj] = 1;
  sc->mode[obj] = sc->actors[obj].defined ? sc->actors[obj].moveMode : 0x07;
  // ...and the $7E mirror the engine reads: $1100,X and the slot -> object table $0197/$0199/$019B
  // (C0/426A's output).
  w8(sc->mem, 0x7E1100 + (uint32_t)obj * 2, sc->mode[obj] & 0xFF);
  if (sc->mode[obj] >= 0 && sc->mode[obj] <= 2) w8(sc->mem, 0x7E0197 + (uint32_t)sc->mode[obj] * 2, obj * 2);
}

// C0/1A03's party reconcile: when $7E:2980-82 differ from the shadow $0194-0196, it walks the per-character object
// table $018D..$0193 in the order 0,1,2,4,3,5,6 (`LDA $8D / BMI / STA $6D / JSR C0/597D`, then $8E, $8F, $91, $90,
// $92, $93) and respawns every character's object, not just slots 0..2, since a re-ordered party can bring on a
// character who had no slot. The table is the PC loaders' own store (pcLoaderIdStores: 41E4-4268 `LDA $6D / STA
// $8D+id`, made before their party test, so it holds a character's object whatever mode the test gave it).
// Returns the number of objects respawned. Positions are the caller's (see CtSceneReinitObj).
int CtSceneReconcileParty(CtScene* sc) {
  if (!sc) return 0;
  static const int kOrder[7] = { 0, 1, 2, 4, 3, 5, 6 };   // C0/1A03's own character order
  int n = 0;
  for (int i = 0; i < 7; i++) {
    uint8_t x = r8(sc->mem, 0x7E018D + (uint32_t)kOrder[i]);   // 1A1E LDA $8D,char / 1A20 BMI -> skip
    if (x & 0x80) continue;
    int o = x / 2;
    if (o >= sc->nobj) continue;
    CtSceneReinitObj(sc, o);                                 // 1A22 STA $6D / 1A24 JSR C0/597D
    n++;
  }
  return n;
}

// battle_forest_return.md AD.3c: the battle epilogue's dead mark. Marks a battle enemy's field object removed:
// excluded from the activation scan, never drawn or scheduled again. `dead` gives placed = 0 and the pass-1
// scheduler skip; draw = 2 tags it removed (not hidden). Mover residue is cleared.
void CtSceneBattleRemoveObj(CtScene* sc, int obj) {
  if (!sc || obj < 0 || obj >= sc->nobj) return;
  sc->actors[obj].dead = 1; sc->actors[obj].draw = 2;
  sc->actors[obj].cadPause = 1;                     // = the $1000 bit 7 stored below: C0/59EC and C0/5AD1 read it
  // ...and the ROM's own dead mark, two bytes. C0/07ED-080C (m=1/x=0), the enemy epilogue:
  //   07ED: LDA $7E29C6,X / BNE ->080D      ; survivor -> restore field position instead
  //   07F3: LDA $1000,Y / ORA #80 / STA $1000,Y     (the store's PC reports as C0:07FB)
  //   07FB: LDA $1A81,Y / BPL ->0807 / (JSR C0/734C)
  //   0807: LDA #00 / STA $1A81,Y                   (reports as C0:080C)
  // So $1A81 := 0x00, not 0x80, plus the $1000 bit 7 cadence pause (the bit op 0x0B writes). The value matters:
  // the next battle's eligibility scan C0/0623-0635 reads `LDA $1A81,X / BMI ->063F` (mirrored in field.c), so
  // 0x80 would make a defeated enemy eligible again. Derived from the scan; not yet checked on a two-battle capture.
  w8(sc->mem, 0x7E1A81 + (uint32_t)obj * 2, 0x00);                       // C0/0809
  w8(sc->mem, 0x7E1000 + (uint32_t)obj * 2,
     (uint8_t)(r8(sc->mem, 0x7E1000 + (uint32_t)obj * 2) | 0x80));       // C0/07F6
  sc->hasTask[obj] = 0; sc->cycles[obj] = 0; sc->mx[obj] = sc->my[obj] = 0;
}

// ---- C0/595C: obj0's slot 1, run to its terminator (disasm/notes/obj0_slot1_runner.md) ----
//   595C REP #20 / STZ $6D / LDA $7F2003 / TAX / SEP #20    obj0's slot-1 pointer ($7F2001 + 0*32 + 1*2)
//   5967 LDA $7F2001,X / BEQ ->597C                           the 00 terminator ends it (not executed)
//   596D TXY / REP / AND #00FF / ASL / TAX / SEP
//   5977 JSR ($5D6E,X) / BRA ->5967                           every other op, whatever carry it returns
//   597C RTS
// It writes none of obj0's own script state ($1180, $1C00, $1001, the $7F0580 return rows), so obj0 comes out
// where it was, which the saved CtSS below restores. Three callers in bank C0, each after the $7F:2000 image is built:
//   C0/56C8  the location load (C0/56B0, after the init runner C0/5709)
//   C0/0206  the menu-exit reload chain C0/01A5-0206 (C0/1975; after C0/56D4 re-decompresses the script)
//   C0/0330  the battle-return tail C0/0304-038C (after C0/56D4; before C0/A712 and the regroup C0/5B1F)
// A handler that parks (IP unchanged) would spin C0/595C forever; the port stops there instead.
void CtSceneRunObj0Slot1(CtScene* sc, const char* site) {
  if (!sc || sc->nobj <= 0) return;
  int fn1 = fpPtr(sc, 0, 1);
  if (fn1 < 0 || fn1 >= sc->blen) return;
  static int tr = -1; if (tr < 0) { const char* e = getenv("CT_SLOT1TRACE"); tr = (e && *e) ? 1 : 0; }
  char line[512]; int ln = 0;
  if (tr) ln = snprintf(line, sizeof line, "slot1: %s ops", site ? site : "?");
  CtSS saved = sc->st[0];
  sc->st[0].addr = fn1; sc->st[0].prio = 0; sc->st[0].waitObj = -1; sc->st[0].wait = 0; sc->st[0].nm = 0;
  ctSetDc(sc, 0, 1);
  const char* why = "guard";
  for (int guard = 0; guard < 600; guard++) {
    int at = sc->st[0].addr;
    if (at < 0 || at >= sc->blen) { why = "off"; break; }
    if (sc->body[at] == 0x00) { why = "end"; break; }  // 5967 LDA / BEQ ->597C
    if (tr && ln < (int)sizeof line - 16) ln += snprintf(line + ln, sizeof line - ln, " %02X@%04X", sc->body[at], at);
    vmOpCost(sc->mem, sc->body, sc->blen, at, 0);       // a load's op, priced as C0/5977 dispatches it
    step(sc, 0);                                        // 5977 JSR ($5D6E,X): its carry is ignored
    if (sc->st[0].addr == at) { why = "parked"; break; } // the ROM would never leave C0/595C
  }
  int endAt = sc->st[0].addr;
  sc->st[0] = saved; ctSetDc(sc, 0, saved.dc);
  // obj0 before -> after: the ROM's runner leaves $1180/$1C00 alone, so the two must agree
  if (tr) fprintf(stderr, "%s %s@%04X obj0 @%04X p%d -> @%04X p%d\n", line, why, endAt & 0xFFFF,
                  saved.addr & 0xFFFF, saved.prio, sc->st[0].addr & 0xFFFF, sc->st[0].prio);
}

// battle_forest_return.md AD.3b/AD.5 step 3: the post-victory field resume (C0/0330 -> C0/5B1F). Seeds the arrival
// barrier loc[0xFF] ($7F03FE) to 3 - nFollowers, then calls the regroup block (sc->rgBase, ct_regroup.h) on each
// follower PC at priority 0, as C0/5B1F does: push the current IP under the current priority (CtSS ret[]), IP :=
// the block, prio := 0, stop mover/ticket/cadence. The block's `95 00` walks the follower to the lead, `71 FF` +
// `12` counts arrivals, and the last arriver's `E3 01` restores control. No followers: control directly (C0/0359).
// The lead is never called. The enemy removal and PC snap are the caller's (field.c). No-op without rgBase.
void CtSceneBattleReturn(CtScene* sc) {
  if (!sc || sc->rgBase < 0) return;
  int fol[2] = { -1, -1 };
  int nf = ctPartyFollowers(sc, fol);                  // 0355 `LDA $99 / BPL`, 036D `LDA $9B / BPL`
  w8(sc->mem, LOC + 0xFF * 2, (3 - nf) & 0xFF);      // loc[0xFF] seed = 3 - nFollowers (the barrier target 3)
  if (nf == 0) {                                       // C0/0359: no followers -> restore control directly
    sc->explore = 1; w8(sc->mem, 0x7E011F, 1); w8(sc->mem, 0x7F03FE, 3);   // 0359 LDA #01 / STA $1F / LDA #03 / STA $7F03FE
    return;
  }
  for (int i = 0; i < nf; i++) {                       // C0/5B1F: prioritized VM CALL of the RETURN block
    int o = fol[i]; CtSS* s = &sc->st[o];
    if (s->prio < 1) continue;                         // 5B26: only CALL if current prio >= 1 (idle=7 passes)
    if (s->addr >= 0) s->ret[s->prio] = ssPushAddr(s); // 5B41: push the current IP ($1180) under the current priority
    s->addr = sc->rgBase;                              // 5B4C: IP := the regroup block (our addr = op position)
    s->prio = 0;                                       // 5B54: priority := 0
    ctSetDc(sc, o, 0);                                 // 5B5F STZ $1001,X: eligible on the next build
    s->wait = 0; s->nm = 0;                            // (the call latch $7F0980 is not 5B1F's: it survives, as a pre-empt's)
    vmTicketStop(sc, o);                               // 5B59/5B5C: STZ $1A80 (velocity) / $1A01 (ticket)
    sc->mx[o] = sc->my[o] = 0; sc->vecMove[o] = 0;
  }
  // 0384-0388 `LDA $AB / STA $AC / STA $AD`: both followers' read cursors back on the ring's head, so after the
  // regroup each follows the lead only once the head has run its gap again.
  { uint8_t h = (uint8_t)r8(sc->mem, 0x7E01AB); w8(sc->mem, 0x7E01AC, h); w8(sc->mem, 0x7E01AD, h); }
}
// Wire the NPC map-collision hook (C0/ACFD): a mover with $1C80 bit 0 (collMap) does not cross into a tile
// the hook reports solid; it re-aims via the C0/FE00 random-angle table (a bounce off the wall).
void CtSceneSetTileSolid(CtScene* sc, int (*fn)(void*, int, int), void* ctx) {
  if (!sc) return; sc->tileSolid = fn; sc->tileCtx = ctx;
}
// ---- C0/1CFC: the action-button probe (disasm/notes/field_action_probe.md sec. 2-3) ----
// The scan starts at the object count and walks down (1D16-1D21; obj0 is never probed), skipping $0F00,X == 0,
// $1100,X bit 7 and the player (1D36 CPX $97; class 7 skips this test), and the first object accepted by the
// per-facing range test (1D3A `JSR ($1D56,X)`) wins: carry set, $EB = the target. Not the nearest.
// The four handlers (C0/1D5E up, 1D84 down, 1DAB left, 1DD1 right) have the shape of the NPC-solid box C0/5BFA:
// signed primary-axis delta (BPL/BMI reject), one's-complement absolute value, `CMP #01C0`, then the cross axis
// `CMP #00C0`. In 12.4 units that is 28 px along the faced axis and 12 px to either side.
// Note: the first SBC has no `SEC`. On the normal path it inherits `1D36 CPX $97`'s carry, C = (obj > playerObj)
// (equality already removed by its BEQ), the rule C0/5BD7's `CPX $6D` gives the solid box; the `$1100,X == 7`
// arm at 1D48 inherits `CMP #07`'s equality, C = 1. So a lower-index object measures one subpixel further. The
// second SBC always runs with C = 0 (after `CMP #01C0` with BCS not taken), so the cross delta is always -1.
int CtVmActBoxAt(int mx, int my, int cx, int cy, int carry0, int facing) {
  int primaryY = (facing <= 1);                                     // 0 up / 1 down are the Y arms
  int wantPos  = (facing == 1 || facing == 3);                      // down / right require d >= 0
  int p = primaryY ? cy : cx, pm = primaryY ? my : mx;
  int q = primaryY ? cx : cy, qm = primaryY ? mx : my;
  uint16_t a = (uint16_t)(p - pm - (1 - carry0));                   // the first SBC, carry per above
  if (wantPos) { if (a & 0x8000) return 0; }                        // BMI -> 1DA7
  else         { if (!(a & 0x8000)) return 0;                       // BPL -> 1DA7
                 a = (uint16_t)~a; }                                // EOR #FFFF
  if (a >= 0x01C0) return 0;                                        // CMP #01C0 / BCS -> 1DA7
  uint16_t b = (uint16_t)(q - qm - 1);                              // the second SBC, carry 0
  if (b & 0x8000) b = (uint16_t)~b;                                 // BPL +3 skips the EOR
  return b < 0x00C0;                                                // CMP #00C0 / BCS -> 1DA7
}
// The scan itself. Returns the target object index, or -1 for "no target" ($EB = $80, carry clear).
// `facing` is the player's $1600 byte (0 up / 1 down / 2 left / 3 right), which `AND #0006` turns into the
// handler index.
int CtSceneActionProbe(const CtScene* sc, int playerObj, int facing) {
  if (!sc || playerObj < 0 || playerObj >= sc->nobj) return -1;
  Mem* m = (Mem*)sc->mem;
  uint32_t pX2 = (uint32_t)playerObj * 2;
  int px = r8(m, 0x7E1800 + pX2) | (r8(m, 0x7E1801 + pX2) << 8);    // 1D08: $C3
  int py = r8(m, 0x7E1880 + pX2) | (r8(m, 0x7E1881 + pX2) << 8);    // 1D0F: $C5
  // 1D16 starts at the object count and walks down. Whether $7F2000 holds nobj or nobj-1 does not matter:
  // a one-past object has no $0F00 and 1D1C skips it.
  for (int obj = sc->nobj - 1; obj >= 1; obj--) {                   // 1D21's `BNE` never probes obj0
    if (!sc->actors[obj].hasPos) continue;                          // no position -> no $1800 to read
    uint32_t X2 = (uint32_t)obj * 2;
    if (r8(m, 0x7E0F00 + X2) == 0) continue;                        // 1D1C: LDA $0F00,X / BNE
    int cls = r8(m, 0x7E1100 + X2);
    if (cls & 0x80) continue;                                       // 1D2D: BMI
    int carry0;
    if (cls == 0x07) carry0 = 1;                                    // 1D32/1D48: the arm that does not
    else {                                                          //   skip the player; C = 1 (CMP #07)
      if (obj == playerObj) continue;                               // 1D36: CPX $97 / BEQ
      carry0 = (obj > playerObj);                                   // 1D36's CPX, equality already gone
    }
    int cx = r8(m, 0x7E1800 + X2) | (r8(m, 0x7E1801 + X2) << 8);
    int cy = r8(m, 0x7E1880 + X2) | (r8(m, 0x7E1881 + X2) << 8);
    if (CtVmActBoxAt(px, py, cx, cy, carry0, facing & 3)) return obj;   // 1D45: the first hit wins
  }
  return -1;                                                        // 1D25: $EB = $80, CLC
}
// Can the player activate (action button) this object? It needs a position and a non-empty function 1, but
// not necessarily a sprite (a sprite-less coordinate object can have one). The activate scan C0/1CFC skips an
// object whose $0F00,X is 0: the C0/A810 activation latch, not the script's own visibility flag $1A81.
int CtSceneActorActivatable(const CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return 0;
  const CtVmActor* a = &sc->actors[obj];
  if (a->dead || !a->hasPos) return 0;
  if (r8((Mem*)sc->mem, 0x7E0F00 + (uint32_t)obj * 2) == 0) return 0;
  int fn = fpPtr((CtScene*)sc, obj, 1);
  return fn >= 0 && fn < sc->blen && sc->body[fn] != 0x00;   // fn1 exists and is not an empty Return
}
// Can the player touch-trigger this object (walk onto it)? Like Activatable but for function 2 (the touch
// slot). The proximity scan C0/9923 gates on the activation latch, not on a sprite: C0/9933 `LDA $0F00,X /
// BNE ->9945` (zero means skip), so an off-camera object is untouchable whatever its $1A81 says.
int CtSceneActorTouchable(const CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return 0;
  const CtVmActor* a = &sc->actors[obj];
  if (a->dead || !a->hasPos) return 0;
  if (r8((Mem*)sc->mem, 0x7E0F00 + (uint32_t)obj * 2) == 0) return 0;
  int fn = fpPtr((CtScene*)sc, obj, 2);
  return fn >= 0 && fn < sc->blen && sc->body[fn] != 0x00;   // fn2 exists and is not an empty Return
}
// Has the VM explicitly hidden this object (op 0x91 draw = 1 / op 0x7E draw = 2)? Distinct from "not placed":
// it needs a positioned sprite that was hidden, so the field renderer can drop the lead PC during a script hide
// without hiding a lead PC that simply has no VM position.
int CtSceneActorHidden(const CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return 0;
  const CtVmActor* a = &sc->actors[obj];
  return a->defined && a->hasPos && !a->dead && a->draw != 0;
}
// The player-controlled object: the lead PC ($0197, see CtSceneCreateIn). Its index varies per scene. The
// field engine renders this object as the player and skips it in the actor loop.
int CtScenePlayerObj(const CtScene* sc) { return sc->playerObj; }   // cached in CtSceneCreate
int CtScenePlayerLoaded(const CtScene* sc) { return sc ? sc->playerLoaded : 0; }
int CtSceneActorMoving(const CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return 0;
  // hasTask alone, not `&& cycles > 0`: cycles hits 0 at the end of each move segment before step() issues
  // the next, so the walk anim would blink to idle for a frame. hasTask stays 1 until arrival.
  return sc->hasTask[obj];
}
int CtSceneActorVecMove(const CtScene* sc, int obj) {   // 1 = active move is a vector slide (op 0x9C/0x9D):
  if (obj < 0 || obj >= sc->nobj) return 0;             // the actor keeps its idle pose, no walk cycle
  return sc->vecMove[obj];
}

int CtSceneActorSolid(const CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return 1;
  return sc->actors[obj].solid;                     // $1B01,X bit 0; load-sprite sets 1, op 0x84 overrides
}

// ---- Invoke prologue (C0/5AC5 activate, C0/3154 touch) ----
// The two invokes are the same seven instructions with one immediate different (`python tools/dis.py 3154 24`,
// `dis.py 5AC5 24`): LDX $EB, then four tests that each take the shared bail:
//   +02  LDA $1C01,X / BNE      ; (1) a pending cross-object wait (op 0x08 sets, op 0x09 clears)
//   +07  LDA $1100,X / BMI      ; (2) deactivated (op 0x0A, C0/6254: $1100,X := $80)
//   +0C  LDA $1000,X / BMI      ; (3) paused      (op 0x0B, C0/6269: $1000,X |= $80)
//   +11  LDA $1C00,X / CMP #imm / BCC   ; (4) busy: #02 for the activate, #03 for the touch
// (3) matters: the probe C0/1CFC never tests $1000,X and the activation pass C0/A88D does not either, so a
// paused object stays drawn and probed, and only this prologue refuses it.
// Stand-in: `prio` for $1C00,X, which the port does not maintain in WRAM (it reads it only to seed). The live
// priority is st->prio (7 when idle; the Return handler C0/5F74's `INC $1C00,X` at C0/5FA0 walks it back up),
// and --vmdiff compares it against the oracle's $1C00. $1C01,X is maintained by ops 0x08/0x09 (C0/6240 /
// C0/624B, its only writers). Checked by `ct.exe --sweepact`.
int CtVmInvokeGate(const uint8_t* w, int obj, int prio, int minPrio) {
  if (obj < 0) return 0;
  if (w) {
    uint32_t X = (uint32_t)obj * 2;                    // per-object tables are obj*2 strided
    if (w[0x1C01 + X] != 0) return 0;                  // 5AC7
    if (w[0x1100 + X] & 0x80) return 0;                // 5ACC
    if (w[0x1000 + X] & 0x80) return 0;                // 5AD1
  }
  return (prio & 0xFF) >= minPrio;                    // 5AD6 CMP #02 (activate) / 3168 CMP #03 (touch)
}

// Run object `obj`'s activate (slot 1) at priority 1 (ports engine.activate -> _call(obj,1,1)).
void CtSceneActivate(CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return;
  CtSS* st = &sc->st[obj];
  if (!CtVmInvokeGate(CtSceneMem7E(sc), obj, st->prio, 2)) return;   // C0/5AC5, CMP #02
  if (st->addr >= 0) st->ret[st->prio] = ssPushAddr(st);   // save current ($1180) to resume after
  int fn = fpPtr(sc, obj, 1);
  if (fn < 0 || fn >= sc->blen) return;
  st->addr = fn; st->prio = 1; st->tb = 0;
  ctSetDc(sc, obj, 0);                                 // the activate path's STZ $1001,X (C0/3264ff family)
  vmTicketStop(sc, obj);                               // talking stops the NPC's in-flight move, so no walk
                                                       // anim lingers through the dialogue; it re-issues on return
}

// Test surface (CT_CALLFN): issue the request ops 0x02..0x07 issue, and nothing more, so functions in slots
// 3..15 (reached otherwise only by another object's call op) can be driven headless. It hands reqOp the same
// (target, function, priority) triple step()'s 0x02-0x07 arm does.
void CtSceneRequestFn(CtScene* sc, int obj, int slot, int prio) {
  if (!sc || obj < 0 || obj >= sc->nobj) return;
  reqOp(sc, obj, slot & 0x0F, prio & 0x0F);
}

// C0/31B1, the push: C0/3154's tail (31AD `JSR C0/31B1`), run whether or not the touch installed fn2. A touched object
// whose $1B01,X has bit 1 is shoved one tile the way the lead faces, by the engine's own program: C0/5D68's six
// bytes at $7F3780 (C0/5D26's MVN), `96 tx ty / FF 9D / 00` (move to the tile, clear the latch, Return).
//   31B1 LDA $54 / BIT #02 / BNE -> RTS            one push at a time: the latch FF 9D (C0/41DC `LDA #02 / TRB $54`) clears
//   31BC LDA $1C00,X / BEQ -> RTS                  an object already at priority 0 (being pushed) is not pushed again
//   31C4 LDY $EB / LDA $1B01,Y / BIT #02 / BEQ     not pushable
//   31CD LDX $97 / LDA $1600,X                     the lead's facing: 0 up (3202), 1 down (3217), 2 left (31ED), else right
//                                                  (31DA) -- the tile word (Y << 8 | X) of the object's tile bytes, +-1 on
//                                                  that axis in 8 bits; up from row 0 or left from column 0 refuses
//   LDA $7E7040,X / BMI -> RTS                     the tile there mover-solid: refused (the word is not masked)
//   3228 STA $7F3781 (the program's operands); `$7F0580[$1C00 * 80 + $EB]` := `$1180,X` (the running IP, saved at its
//   own priority); `$1180,X` := 177F, `$1C00,X` := 0, STZ $1A80 / $1A01 / $1001; `LDA #02 / TSB $54`.
static void vmTouchPush(CtScene* sc, int obj) {
  Mem* m = sc->mem; CtSS* st = &sc->st[obj];
  uint32_t X = (uint32_t)obj * 2;
  if (r8(m, 0x7E0154) & 0x02) return;                                       // 31B1-31B5
  if ((st->prio & 0xFF) == 0) return;                                       // 31BC-31BF
  if (!(r8(m, 0x7E1B01 + X) & 0x02)) return;                                // 31C4-31CB
  int f = r8(m, 0x7E1600 + (uint32_t)r8(m, 0x7E0197));                      // 31CD-31CF
  int tx = r8(m, 0x7E1801 + X), ty = r8(m, 0x7E1881 + X);
  if (f == 0) { if (ty == 0) return; ty--; }                                // 3202-3209
  else if (f == 1) ty = (ty + 1) & 0xFF;                                    // 3217-321C
  else if (f == 2) { if (tx == 0) return; tx--; }                           // 31ED-31F6
  else tx = (tx + 1) & 0xFF;                                                // 31DA-31E1
  uint32_t word = (uint32_t)(ty << 8 | tx);
  if (r8(m, 0x7E7040 + word) & 0x80) return;                                // LDA $7E7040,X / BMI -> 31B9
  w8(m, 0x7F3781, (uint8_t)tx); w8(m, 0x7F3782, (uint8_t)ty);               // 322B STA $7F2001,X (X = 1780)
  sc->raw[0x1781] = (uint8_t)tx; sc->raw[0x1782] = (uint8_t)ty;             //   the runner reads its own copy
  if (st->addr >= 0) st->ret[st->prio] = ssPushAddr(st);                    // 3232-324B
  st->addr = 0x177F - sc->header; st->prio = 0; st->tb = 0;                 // 324F-325B
  ctSetDc(sc, obj, 0);                                                      // 3264 STZ $1001,X
  vmTicketStop(sc, obj);                                                    // 325E / 3261 STZ $1A80,X / $1A01,X
  w8(m, 0x7E0154, (uint8_t)(r8(m, 0x7E0154) | 0x02));                      // 3267 LDA #02 / TSB $54
}
void CtSceneTouch(CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return;
  CtSS* st = &sc->st[obj];
  // The same prologue as the activate (CtVmInvokeGate), with `CMP #03`. The two bails differ: the refusals go to
  // 31B0 `RTS`, the priority test to 31AD `JSR C0/31B1`, so the push runs whether or not fn2 was installed.
  // The touch-start runs fn2 at priority 2 (C0/319F `LDA #02 / STA $1C00,X`), not 1, leaving priority 1 open for
  // the helper functions a touch cutscene calls; it skips only if already busy at prio < 3 (C0/3165).
  if (!CtVmInvokeGate(CtSceneMem7E(sc), obj, 0xFF, 0)) return;            // 3156-3163 -> 31B0
  if (CtVmInvokeGate(NULL, obj, st->prio, 3)) {                             // 3165-316A CMP #03 / BCC ->31AD
    int fn = fpPtr(sc, obj, 2);
    if (fn >= 0 && fn < sc->blen) {
      if (st->addr >= 0) st->ret[st->prio] = ssPushAddr(st);   // push the running fn ($1180) to its return slot
      st->addr = fn; st->prio = 2; st->tb = 0;              // C0/319F: fn2 at priority 2
      ctSetDc(sc, obj, 0);                                  // 31AA STZ $1001,X (beside the 1A80/1A01 zeroes below)
      vmTicketStop(sc, obj);                               // the touch stops the actor's in-flight move
    }
  }
  vmTouchPush(sc, obj);                                    // 31AD JSR C0/31B1
}

int CtSceneTextbox(const CtScene* sc, int* tableAddr, int* strIndex) {
  if (tableAddr) *tableAddr = sc->tbTableAtShow;     // the table as of when the box opened
  if (strIndex) *strIndex = sc->tbIndex;
  return sc->textbox;
}

// ---- Textbox unpark: the box machine's own $0129 (disasm/notes/field_textbox_unpark.md) ----
// The textbox family is six two-visit blocking ops, 0xBB/0xC1/0xC2 (plain) and 0xC0/0xC3/0xC4 (decision), all
// parked on the same byte. Op 0xC2, C0/35FC:
//   3606 LDA $29 / BNE ->3640        ; $0129 != 0 -> TYX / CLC / RTS: the IP does not advance
//   3612 LDA #01 / STA $7F0A00,X ... LDA #01 / STA $29 ... CLC / RTS   ; first visit: open, park
//   3635 LDA #00 / STA $7F0A00,X / TYX / INX / INX / SEC / RTS         ; a later visit: advance
// The only other reader of $0129 is C0/1DF4, the A-button treasure probe.
// The script resumes on the first scheduler visit at which $0129 == 0; the box machine clears it at the end of
// its close ramp C0/2032 (four DECs, the last untested, so with $0127 = 0x28 C0/204F STZ $29 fires on frame 11).
// The resume therefore depends on the object's own visit phase, not a fixed count. The op sets $0129 = 1 when it
// opens the box, the host's dismiss sets $0129 = 0x0D (C0/2081) and re-arms $0127 = 0x28 (C0/1FF8's immediate),
// and the ramp drains in CtSceneTick. The VM reads only zero vs non-zero.
void CtSceneAdvance(CtScene* sc) {
  if (!sc || !sc->textbox) return;
  w8(sc->mem, 0x7E0129, 0x0D);                   // C0/2081 LDA #0D / STA $29: the close state
  w8(sc->mem, 0x7E0127, 0x28);                   // C0/1FF8's full-open value, so the ramp is whole
  sc->tbCloseArmed = 1;                          // ...and C0/2081 returns: the 0x0D arm (C0/2032) is only
                                                 //   dispatched on the box machine's next frame
}
// C0/2126's end: the typewriter has drawn the page's last character (the host owns that string), so the
// machine leaves state 6: 21D8 `LDA #07 / STA $29 / LDA #81 / STA $36` and 21C2 `LDX #FFFF / STX $34`.
// (The $0216 arm at 21A8, an in-string pause, is CtSceneTextboxPause.)
void CtSceneTextboxRevealDone(CtScene* sc, int pendingCode) {
  if (!sc || r8(sc->mem, 0x7E0129) != 6) return;
  w8(sc->mem, 0x7E0129, 7);
  // $0134 := FFFF (wait for A) on 21C2 / 21D3 only: the end of the string (0), a waiting page (0B/0C), a waiting
  // line (07/08). A self-turning page (09/0A, 2181) and a full window met on a line code (05/06, 21CB) reach 21D8
  // directly and leave $0134 as it is (0 after the last press), so state 7 advances by itself (tbMachineTick).
  int self = (pendingCode == 0x05 || pendingCode == 0x06 || pendingCode == 0x09 || pendingCode == 0x0A);
  w8(sc->mem, 0x7E0136, 0x81);                   // 21DC LDA #81 / STA $36: the NMI uploads the last line once more
  if (!self) { w8(sc->mem, 0x7E0134, 0xFF); w8(sc->mem, 0x7E0135, 0xFF); }
  // $0215 = the control code the text engine stopped on. State 7 reads only this from C0/2126's work (C0/2024
  // `LDA $0215 / BNE ->2056`): non-zero means another page follows, so the cursor does not arm and the press
  // turns the page; zero means the string is exhausted (the option page). The host passes the code.
  w8(sc->mem, 0x7E0215, (uint8_t)pendingCode);
}
// The typewriter met an in-string pause (`03 n`, C2/5945: $0215 = 03, $0216 = n): C0/2176 -> 21A8.
//   n != 0: `STA $4202 / LDA #0F / STA $4203` -> $0134 = n * 15, `LDA #07 / STA $29`, then 21A3 `LDA #01 / STA $36`
//   n == 0: 21AB `BEQ ->21D8`: `$29 = 7 / $36 = $81`, $0134 left as it stands (0 after a pause ran out: the box closes
//           on the next frame, 2096 -> 2081; FFFF if the last thing was a press-wait: the press spends it and closes)
// The countdown and what follows it are state 7's (the box machine, above).
void CtSceneTextboxPause(CtScene* sc, int n) {
  if (!sc || r8(sc->mem, 0x7E0129) != 6) return;
  w8(sc->mem, 0x7E0215, 0x03); w8(sc->mem, 0x7E0216, (uint8_t)n);
  w8(sc->mem, 0x7E0129, 7);
  if (n) {
    unsigned t = (unsigned)n * 15u;
    w8(sc->mem, 0x7E0134, (uint8_t)t); w8(sc->mem, 0x7E0135, (uint8_t)(t >> 8));
    w8(sc->mem, 0x7E0136, 0x01);
  } else w8(sc->mem, 0x7E0136, 0x81);
}
// State 7's page advance, C0/2086 `LDA #08 / STA $29`: the machine re-decodes (8), counts (9-B) and resumes
// the typewriter (C -> 6).
void CtSceneTextboxPage(CtScene* sc) {
  if (!sc || r8(sc->mem, 0x7E0129) != 7) return;
  w8(sc->mem, 0x7E0129, 8);
  sc->tbPageArmed = 1;                           // ...and 2086 returns: 1FB4 is the machine's next frame (as 2081's 0x0D)
}
// The host's view: the box is dismissed (draw it ramping out) while the VM is still parked.
// The window belongs to the box machine, not to the op's park latch. C0/1FCD (state 5) is the only place a
// window is created (`LDA $26 / BNE ->1FF8` takes the open ramp if one exists; otherwise it sets $0126 =
// 1 top / 2 bottom, zeroes the height $0127 and calls C0/20F2), and the close ramp's tail clears both:
//   204D STZ $29 / 204F STZ $26 / 2051 LDA #20 / 2053 TRB $54 / 2055 RTL
// The op's later visit (C0/3635) only clears the per-object latch and advances the IP. So both post-dismiss
// states, 0x0D (ramping out) and 0 (gone), are "not an open box", including the frames between the ramp
// draining and the op's next visit.
int CtSceneTextboxUnparking(const CtScene* sc) {
  if (!sc || !sc->textbox) return 0;
  uint8_t st = (uint8_t)r8(sc->mem, 0x7E0129);
  return st == 0x0D || st == 0;                  // C0/2081's close state, or C0/204D's cleared one
}

int CtSceneTextboxChoice(const CtScene* sc) { return sc->textbox ? sc->tbChoice : 0; }
int CtSceneTextboxResumeArmed(const CtScene* sc) { return sc ? gTbResumeArmed : 0; }   // see tbMachineTick, 2096
// Decision-box option-line range (op 0xC0/0xC3/0xC4 operand 2): returns 1 and fills the first/last option line
// within the option page, so the renderer can place the cursor (leading spaces cannot tell prompt continuation
// lines from option lines). 0 = no choice.
int CtSceneTextboxOptLines(const CtScene* sc, int* first, int* last) {
  if (!sc->textbox || !sc->tbChoice || sc->tbOpt < 0) return 0;
  if (first) *first = (sc->tbOpt >> 2) & 3;
  if (last)  *last  = sc->tbOpt & 3;
  return 1;
}
int CtSceneTextboxOp(const CtScene* sc) { return sc->textbox ? sc->tbOp : 0; }
int CtSceneTextboxObj(const CtScene* sc) { return sc->textbox ? sc->tbObj : -1; }

// Name-entry screen (op 0xC8 operand 0xC0+id): char-id being named, or -1 if no screen is up.
int CtSceneNaming(const CtScene* sc) { return sc->naming ? sc->nameChar : -1; }
// The mode-0 load/new-game shell request (op 0xC8, odd operand < $80 -> C0/0D00). -1 = not up.
int CtSceneLoader(const CtScene* sc) { return sc->loader ? sc->loaderArg : -1; }
// The mode-2 shop request (op 0xC8 operand $80-$BF -> C0/0D09 -> C2/8000 -> screen $0C). -1 = none; else the raw
// operand (the shop number is operand & $3F).
int CtSceneShop(const CtScene* sc) { return sc->shop ? sc->shopArg : -1; }
int CtSceneSavePoint(const CtScene* sc) { return (sc->shop && !(sc->shopArg & 0x80) && sc->shopArg) ? sc->shopArg : -1; }
int CtSceneExchange(const CtScene* sc) { return (sc->shop && sc->shopArg == 0) ? 1 : 0; }   // `C8 00`: mode 5
void CtSceneShopDone(CtScene* sc) { sc->shop = 0; }
// The $C3 special-sequence request (op 0xFF, operand < $90 -> C0/3FE0 `JML $C3:0000`). -1 = none.
int CtSceneSequence(const CtScene* sc) { return sc->seqHas ? sc->seqReq : -1; }
void CtSceneSequenceDone(CtScene* sc) { sc->seqReq = 0; sc->seqHas = 0; sc->ffFade = 0; }
// op 0xFF < $90 met a lit screen: its first `DEC $19` ran in the op's frame and the loop C0/3FC1-3FD1 is still owed.
int CtSceneFfFading(const CtScene* sc) { return sc ? sc->ffFade : 0; }
// CtSceneReboot: op 0xDF/0xE1 asked for a reboot. CtSceneGateWarp: the bank-$FD gate warp request (op 0xFF,
// operand $80-$8F -> C0/3FE8/3FF0/3FF8/3FFC), -1 = none.
int CtSceneReboot(const CtScene* sc) { return sc->rebootReq == 1 ? 1 : 0; }
// Op FF 96 (C0/4181) is spinning toward the reset entry FD/C000 (rebootReq 3); CtSceneRebootDone takes it too.
int CtSceneColdReset(const CtScene* sc) { return sc->rebootReq == 3 ? 1 : 0; }
void CtSceneRebootDone(CtScene* sc) { sc->rebootReq = 2; }
int CtSceneGateWarp(const CtScene* sc) { return sc->warpReq ? sc->warpReq : -1; }
void CtSceneGateWarpDone(CtScene* sc) { sc->warpReq = 0; sc->ffFade = 0; }
// 1 while a parked op has left the field main loop C0/0097..00BD, the loop whose C0/00A1 `JSR C0/18D9` is the only
// caller of the X-button poll in bank $C0. op 0xC8 (C0/0CEA `JSR C0/0B4E`, C0/0D18 `JSL $C2:8000`: naming, the load
// shell, every mode) runs the menu program's own loop until it returns; op 0xFF < $90 (C0/3FD3 `JSR C0/0B4E / LDX
// #06FF / TXS`, then `JML $C3:0000` or the $FD warp) and op 0xDF/0xE1 (spin on $4210, reboot) never come back.
int CtSceneOffMainLoop(const CtScene* sc) {
  return sc->naming || sc->loader || sc->shop || sc->seqHas || sc->warpReq || sc->rebootReq;
}
// The main loop is parked; asked before this frame's tick (the host's legs run ahead of it). A sound op whose
// `JSL C7/0004` crosses N frames holds the CPU inside the main loop's VM visit, and until it returns no leg of the
// loop runs: not the player controller C0/8833 (inside the lead's visit, already spent), the A handler or the box
// machine. That is every frame the tick loses (sndWait > 1, or the overrun's window) and the return frame
// (sndWait == 1: the iteration resumes after the VM, at C0/A810).
int  CtSceneMainLoopParked(const CtScene* sc) { return sc ? (sc->sndPostResumeFreeze || sc->sndWait > 0) : 0; }
int  CtSceneFrameLegsOn(void) { return legsOn() && gRomImg != NULL; }   // the drain (C0/CA76) writes $0F01,X every tick
// Field calls this after the player confirms a name (after CtSetName): the blocked 0xC8 op resumes the
// script on the next tick (like CtSceneAdvance for textboxes).
void CtSceneNameDone(CtScene* sc) { sc->naming = 0; }

// Active-party char-id at slot 0/1/2 (ROM $7E:2980/81/82). $7E2980 maps to mem.s[0x2980].
// With no scene (sc==NULL) report the new-game default party the field engine seeds: Crono in
// slot 0, slots 1/2 empty (0x80) -- so dialogue name resolution works in scene-free contexts.
int CtSceneParty(const CtScene* sc, int slot) {
  if (slot < 0 || slot > 2) return 0x80;                   // out of range = empty
  if (!sc) return slot == 0 ? 0x00 : 0x80;                 // new-game default (Crono only)
  return sc->mem->s[0x2980 + slot];
}

// The character name variables: the field engine's copy of the RAM name buffers at $7E:2C23 (6-byte records,
// 5 chars + $00, by char-id; record 7 = Epoch at $7E:2C4D; token handler C2/5B15, pointer table C2/5FD8).
// New-game defaults, stored as ASCII, which the field font's cGlyph renders like the CT-charset bytes the ROM
// holds. Char-id order: Crono Marle Lucca Robo Frog Ayla Magus (0-6), Epoch (7).
//   0 Crono = A2 CB C8 C7 C8   1 Marle = AC BA CB C5 BE   2 Lucca = AB CE BC BC BA
//   3 Robo  = B1 C8 BB C8      4 Frog  = A5 CB C8 C0      5 Ayla  = A0 D2 C5 BA
//   6 Magus = AC BA C0 CE CC   7 Epoch = A4 C9 C8 BC C1
// Mutable, so the name-entry screen (op 0xC8) can rewrite a name. Max 5 chars.
static char g_ctName[8][8] = {
  "Crono", "Marle", "Lucca", "Robo", "Frog", "Ayla", "Magus", "Epoch"
};
#define CT_NAME_VAR g_ctName

const char* CtGetName(int id) { return (id >= 0 && id < 8) ? g_ctName[id] : ""; }
void CtSetName(int id, const char* name) {
  if (id < 0 || id >= 8 || !name) return;
  int i = 0;
  for (; i < 5 && name[i]; i++) g_ctName[id][i] = name[i];  // the buffer holds up to 5 chars
  g_ctName[id][i] = 0;
}

int CtResolveNames(const char* in, int len, char* out, int cap, const CtScene* sc) {
  int o = 0;
  int numCur = 0x0200;                                      // C2/5815 `LDX #0200 / STX $3D`, once per string
  if (cap <= 0) return 0;
  for (int i = 0; i < len && o < cap - 1; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c == 0x01 && i + 1 < len) {                         // name-substitution marker
      unsigned char code = (unsigned char)in[++i];
      if (code >= 0x0D && code <= 0x0F) {                   // {num}: C2/5953 / 5985 / 59C8
        // `LDA [$3D]` 1, 2 or 3 bytes little-endian, the cursor advancing past them (INC $3D), then C2/615E /
        // 6180 / 61BD into 3 / 5 / 8 decimal digits and C2/5DAD / 5D91 / 5D56 drop the leading zeros down to
        // one digit: the plain decimal of the value. ($02A1 selects C2/5CC0's hex arm instead; the field leaves
        // it 0.) Without a scene there is no RAM to read: keep the tag.
        int w = code - 0x0C;
        if (!sc || !sc->mem) { const char* t = "{num}"; for (; *t && o < cap - 1; t++) out[o++] = *t; continue; }
        unsigned long v = 0;
        for (int k = 0; k < w; k++) v |= (unsigned long)sc->mem->s[(numCur + k) & 0xFFFF] << (8 * k);
        numCur += w;
        char nb[12]; snprintf(nb, sizeof nb, "%lu", v);
        for (const char* t = nb; *t && o < cap - 1; t++) out[o++] = *t;
        continue;
      }
      if (code == 0x1F) {                                   // {item}: C2/5B9B
        // `LDA $7F0200 / AND #00FF`: the item id is the script's local 0 (the item ops' `4F id 00` put it there),
        // times 11 (`$4202/$4203`) + `$0B5E` + 1 in bank $CC: an item record is its icon byte, then the name. C2/5D23
        // takes up to 10 bytes, ending at the first 0xEF (padding). The bytes are the CT charset the renderer draws
        // (cGlyph takes 0xA0-0xF3 raw); 0xFF, the space inside a name, is the font's blank glyph of advance 4, the
        // renderer's ' '. Without a scene there is no RAM to read: keep the tag.
        if (!sc || !sc->mem || !gRomImg) { const char* t = "{item}"; for (; *t && o < cap - 1; t++) out[o++] = *t; continue; }
        uint32_t at = 0x0C0B5Fu + (uint32_t)sc->mem->l[0] * 11u;
        for (int k = 0; k < 10 && at + (uint32_t)k < (uint32_t)gRomImgLen && o < cap - 1; k++) {
          uint8_t b = CtRomByte(gRomImg, gRomImgLen, (long)(at + (uint32_t)k));
          if (b == 0xEF) break;                             // C2/5D28 `CMP #EF / BEQ`
          out[o++] = (b == 0xFF) ? ' ' : (char)b;
        }
        continue;
      }
      int rec = -1;
      if (code >= 0x13 && code <= 0x19) rec = code - 0x13;  // Crono..Magus by char-id (C2/5B15)
      else if (code == 0x1A) rec = 0;                       // Crono, hardcoded (C2/5B3B)
      else if (code == 0x20) rec = 7;                       // Epoch ($7E:2C4D, C2/5BD6)
      else if (code >= 0x1B && code <= 0x1D) {              // active-party slot 0/1/2 (C2/5B70)
        int cid = CtSceneParty(sc, code - 0x1B);            // sc==NULL -> 0x80 -> empty (nothing)
        rec = (cid >= 0 && cid <= 6) ? cid : -1;            // empty/invalid slot prints nothing
      }
      if (rec >= 0)
        for (const char* p = CT_NAME_VAR[rec]; *p && o < cap - 1; p++) out[o++] = *p;
      continue;
    }
    out[o++] = in[i];
  }
  out[o] = 0;
  return o;
}
// Drain one queued scripted tile copy (sl,st,el,eb,dl,dt,flags); 1 if out[] was filled.
int CtScenePollTileCopy(CtScene* sc, int out[7]) {
  for (int i = 0; i < sc->tcN; i++) {
    for (int k = 0; k < 7; k++) out[k] = sc->tcQ[i][k];
    sc->tcN--;
    memmove(sc->tcQ[i], sc->tcQ[i + 1], sizeof(sc->tcQ[0]) * (sc->tcN - i));
    return 1;
  }
  return 0;
}
int CtScenePollLocChange(CtScene* sc, int* loc, int* x, int* y, int* facing, int* style) {
  if (!sc->locChange) return 0;
  sc->locChange = 0;                                 // one-shot: drain it
  // CT_LOCTRACE=1: one line per committed location change, including field-to-field hops (the host's
  // `reboot:` line fires only when a sequence ends). The counterpart of the oracle's `--monitor 7E0100:2 --diff`.
  { static int t = -1; if (t < 0) t = getenv("CT_LOCTRACE") ? 1 : 0;
    // stdout, like the host's `reboot:` / `sequence:` / `ow-op05:` lines, so a piped run keeps them in order.
    if (t) printf("loctrace: -> loc %03X tile(%d,%d) facing=%d style=%d\n",
                  sc->lcLoc & 0x1FF, sc->lcX, sc->lcY, sc->lcFacing, sc->lcStyle); }
  if (loc) *loc = sc->lcLoc;
  if (x) *x = sc->lcX;
  if (y) *y = sc->lcY;
  if (facing) *facing = sc->lcFacing;
  if (style) *style = sc->lcStyle;
  return 1;
}
int CtSceneExplore(const CtScene* sc) { return sc->explore; }
// The host says whether this frame is a C0/2848 ramp frame (the location load's fade-in) or a C0/0097
// main-loop frame. Only the field host knows (the ramp is a loop inside the loader, not a WRAM state);
// every other VM host leaves it 0, i.e. main loop.
void CtSceneSetLoadFadeIn(CtScene* sc, int on) { if (sc) sc->loadFadeIn = on ? 1 : 0; }
int CtSceneBright(const CtScene* sc) { return sc->bright; }
// sc->bright is $0119's low nibble; the host's C0/2848 ramp steps the WRAM byte, so it re-reads it here.
void CtSceneSyncBright(CtScene* sc) { if (sc) sc->bright = r8(sc->mem, 0x7E0119) & 0x0F; }
void CtSceneHeadLatch(CtScene* sc) { if (sc) { sc->head117 = (uint8_t)r8(sc->mem, 0x7E0117); sc->head117Set = 1; } }
int CtSceneActorMoveAnim(const CtScene* sc, int i) {
  if (i < 0 || i >= sc->nobj) return 0;
  return sc->actors[i].moveAnim;
}
int CtSceneActorPrio(const CtScene* sc, int i) {
  // Op 0x8E byte (C0/4C74): bit 7 = auto (position-based, from the foot tile props); else fixed OAM
  // priorities: bits 4-5 = upper half, bits 0-1 = lower half (0-1 = behind all of BG1, invisible in rooms,
  // 2 = under prio-1 BG tiles, 3 = over them).
  if (i < 0 || i >= sc->nobj) return 0x80;
  // Read from $0F80,X: filled with 0x80 by the location load (CtSceneCreateIn), stored by op 0x8E (C0/4C7C,
  // vmOp8E) and carried by a WRAM seed. `8E 00` (fixed prio 0) parks a loaded object behind the BG.
  return r8((Mem*)sc->mem, 0x7E0F80 + (uint32_t)i * 2) & 0xFF;
}
int CtSceneActorScripted(const CtScene* sc, int i) {
  if (i < 0 || i >= sc->nobj) return 0;
  return sc->st[i].addr >= 0 && sc->st[i].prio < 7;  // a requested call (activate, touch, 04, ...)
}
int CtSceneActorAtB0(const CtScene* sc, int i) { return (i >= 0 && i < sc->nobj && i < 64) ? sc->atB0[i] : 0; }
int CtSceneActorMode(const CtScene* sc, int i) { return (i >= 0 && i < sc->nobj) ? sc->mode[i] : -1; }  // $1100 probe

// ---- Debug save/load of the full live scene state (Belthasar F5/F9) ----
// A whole-struct snapshot of the VM plus the WRAM image. The pointers in CtScene (raw/body, the tileSolid hook
// and ctx, mem) are scrubbed on save and preserved on load, so only the dynamic state is restored. Same scene
// only (nobj must match). Debug only.
#define CT_STATE_MAGIC 0x53465443u   /* 'CTFS' LE */
#define CT_STATE_VERSION 4u          /* v2: version field, scrubbed pointers. v3: the WRAM image follows the
                                        struct. v4: the $0F80 row (op 0x8E's sprite-priority byte) is RAM the
                                        renderer reads; a v3 state still loads, its row rebuilt from the old
                                        `sprPrio` mirror (0 = auto, 0x80). */
int CtSceneSaveState(const CtScene* sc, const char* path, const double player[4]) {
  FILE* f = fopen(path, "wb");
  if (!f) return 0;
  uint32_t magic = CT_STATE_MAGIC, ver = CT_STATE_VERSION, sz = (uint32_t)sizeof(CtScene);
  CtScene* cp = (CtScene*)malloc(sizeof(CtScene));   // serialize a copy with the live heap pointers
  if (!cp) { fclose(f); return 0; }                  // scrubbed (load overwrites them anyway), so no
  memcpy(cp, sc, sizeof(CtScene));                   // heap addresses reach the disk
  cp->raw = NULL; cp->body = NULL; cp->tileSolid = NULL; cp->tileCtx = NULL; cp->mem = NULL;
  int ok = fwrite(&magic, 4, 1, f) == 1 && fwrite(&ver, 4, 1, f) == 1 && fwrite(&sz, 4, 1, f) == 1
        && fwrite(cp, sizeof(CtScene), 1, f) == 1
        && fwrite(player, sizeof(double), 4, f) == 4    // the field player state {px,py,pz,pfac}
        && sc->mem && fwrite(sc->mem, sizeof *sc->mem, 1, f) == 1;   // ...and the WRAM image (v3)
  free(cp);
  fclose(f);
  return ok;
}
int CtSceneLoadState(CtScene* sc, const char* path, double player[4]) {
  FILE* f = fopen(path, "rb");
  if (!f) return 0;
  uint32_t magic = 0, ver = 0, sz = 0;
  if (fread(&magic, 4, 1, f) != 1 || fread(&ver, 4, 1, f) != 1 || fread(&sz, 4, 1, f) != 1
      || magic != CT_STATE_MAGIC) { fclose(f); return 0; }
  if (ver != CT_STATE_VERSION && ver != 3u) {   // a pre-versioning (v1) state reads its sz field here -> distinct message
    fprintf(stderr, "[state] version mismatch: state v%u, engine v%u\n", ver, CT_STATE_VERSION);
    fclose(f); return 0;
  }
  if (sz != (uint32_t)sizeof(CtScene)) { fclose(f); return 0; }
  CtScene* tmp = (CtScene*)malloc(sizeof(CtScene));
  if (!tmp) { fclose(f); return 0; }
  int got = fread(tmp, sizeof(CtScene), 1, f) == 1;
  int havePlayer = got && fread(player, sizeof(double), 4, f) == 4;  // {px,py,pz,pfac}
  // The image is read into a temp first: a short or corrupt tail must not leave the live WRAM half-written.
  Mem* img = (got && havePlayer) ? (Mem*)malloc(sizeof(Mem)) : NULL;
  int haveImg = img && fread(img, sizeof(Mem), 1, f) == 1;
  fclose(f);
  if (!got || tmp->nobj != sc->nobj || !haveImg) { free(tmp); free(img); return 0; }
  uint8_t* raw = sc->raw; const uint8_t* body = sc->body;           // preserve the live pointers (static
  int (*ts)(void*, int, int) = sc->tileSolid; void* tc = sc->tileCtx;  // scene data + the field map-hook)
  Mem* mem = sc->mem; int own = sc->ownMem;                        // ...and the WRAM the host owns
  memcpy(sc, tmp, sizeof(CtScene));
  sc->raw = raw; sc->body = body; sc->tileSolid = ts; sc->tileCtx = tc;
  sc->mem = mem; sc->ownMem = own;
  // The image is copied into the live buffer rather than repointing at the temp: the overworld's `w->wram`
  // and the audio service hold that pointer.
  if (sc->mem) memcpy(sc->mem, img, sizeof(Mem));
  if (sc->mem && ver == 3u) {                        // legacy v3: rebuild the $0F80 row from the old mirror
    for (int i = 0; i < sc->nobj && i < 64; i++) {
      int v = sc->actors[i].sprPrio;
      w8(sc->mem, 0x7E0F80 + (uint32_t)i * 2, v ? (v & 0xFF) : 0x80);
    }
    fprintf(stderr, "[state] v3 state: $0F80 sprite-priority row rebuilt from the engine mirror (%d objects)\n", sc->nobj);
  }
  free(tmp); free(img);
  return 2;
}
// The location a snapshot was taken in: $0100 of the WRAM image that closes it (v3/v4), so the host can load that
// scene before CtSceneLoadState, which is same-scene only. `flags` (0x200 bytes, may be NULL) takes the image's
// $7F:0000 story page, for the scene's create-time fn0 pass. -1 if the file is not a loadable snapshot.
int CtSceneStateLoc(const char* path, uint8_t* flags) {
  FILE* f = fopen(path, "rb");
  if (!f) return -1;
  uint32_t magic = 0, ver = 0, sz = 0; uint8_t loc[2];
  long img = 0;
  int ok = fread(&magic, 4, 1, f) == 1 && fread(&ver, 4, 1, f) == 1 && fread(&sz, 4, 1, f) == 1
        && magic == CT_STATE_MAGIC && (ver == CT_STATE_VERSION || ver == 3u) && sz == (uint32_t)sizeof(CtScene);
  if (ok) img = (long)(12 + sz + 4 * sizeof(double));                    // header, CtScene, player[4] -> the image
  ok = ok && fseek(f, img + 0x0100, SEEK_SET) == 0 && fread(loc, 1, 2, f) == 2;
  if (ok && flags) ok = fseek(f, img + (long)offsetof(Mem, g), SEEK_SET) == 0 && fread(flags, 1, 0x200, f) == 0x200;
  fclose(f);
  return ok ? ((loc[0] | (loc[1] << 8)) & 0x1FF) : -1;
}
void CtSceneActorSetFacing(CtScene* sc, int i, int facing) {
  if (i >= 0 && i < sc->nobj) vmSetFacing(sc->mem, &sc->actors[i], i, facing);
}
void CtSceneAnimFinished(CtScene* sc, int i) {
  if (i < 0 || i >= sc->nobj) return;
  // The ROM's completion condition is $7F0B01,X == 1 (C0/2EA2), driven 2 -> 1 by the frame legs CtSceneTick
  // runs (CtAnimTimerTick's CA4F arm, CtFieldFrameLookup's CBA4 arm) when the legs are on and a ROM image is
  // there. Then this renderer signal decides nothing; writing the byte here would replace the ROM's clock.
  if (legsOn() && gRomImg) return;
  // Stand-in for a host with no drain (CT_FRAMELEGS=0, or no ROM): the renderer's end of playback replaces
  // the ticker's 2 -> 1.
  uint8_t* w = (uint8_t*)sc->mem; int x = i * 2;
  if (w[0x1780 + x] == 2 && w[0x10B01 + x] >= 2) w[0x10B01 + x] = 1;
  sc->actors[i].animBusy = 0;
  sc->actors[i].animSeedFrame = -1;
}
int CtSceneActorAnim(const CtScene* sc, int i, int* mode, int* index, int* il, int* loops, int* staticFrame) {
  if (i < 0 || i >= sc->nobj) { *mode = 0; *index = 0; *il = 0; *loops = 0; *staticFrame = 0; return 0; }
  const CtVmActor* a = &sc->actors[i];
  *mode = a->animMode; *index = a->animIndex; *il = a->animIl; *loops = a->animLoops;
  // Mode 2 surfaces the $1681,X resume step through this out-param; mode 3 surfaces the $1301,X held frame.
  // They are different bytes and the mode-2 arm never reads $1301 (C0/2EE0-2EE3 stores the anim id then
  // `STZ $1681,X`), so mode 2 falls back to 0, the cursor the arm writes, not to animStatic (an `AC n`
  // before an `AB m` must not start the anim at step n).
  *staticFrame = (a->animMode == 2) ? (a->animSeedFrame >= 0 ? a->animSeedFrame : 0) : a->animStatic;
  return a->animGen;
}

// The move-anim selector (C0/5614 -> C0/5654-569C) rewrites the lead PC's $1680 anim mode every frame while the
// player has control (idle 0, walk 1, run 6), so a shape a script set on the lead (op 0xAC, $1680 = $FF) does not
// survive once control returns. field.c calls this each free-roam frame so the VM actor tracks the controller.
void CtSceneSetActorAnim(CtScene* sc, int i, int mode, int index) {
  if (i < 0 || i >= sc->nobj) return;
  CtVmActor* a = &sc->actors[i];
  if (a->animMode == mode && a->animIndex == index) return;
  a->animMode = mode; a->animIndex = index; a->animGen++;
}

// C0/1ADF, the decision box's cursor arm. While $0162 != 0 it owns the whole input frame (C0/1927 `LDA $62 /
// BEQ ->192E / JSR C0/1ADF / RTS`; the ordinary input path bails on the same byte at C0/1934). $0162: 1 =
// requested, not armed (1ADF: the press is only eaten, 1B0C); 2 = live (1AE7 down / 1AEE up move the cursor
// $0163, wrapping between $0164 and $0165 (1B19 / 1B29); 1AF9 A confirms, $0162 := 3); 3 = confirmed (1AFE:
// $0166 := $0163, the line the completion arm stores as `result`, and $0163 := 4). The host calls this every
// frame from its input block, in C0/1927's slot.
void CtSceneDecisionInput(CtScene* sc, int aPressed, int upPressed, int downPressed) {
  if (!sc) return;
  Mem* m = sc->mem;
  int d = r8(m, 0x7E0162);
  if (d == 0) return;                                        // 1929 BEQ ->192E: not a decision frame
  if (d == 2) {                                              // cursor live
    int c = r8(m, 0x7E0163), first = r8(m, 0x7E0164), last = r8(m, 0x7E0165);
    if (downPressed) { w8(m, 0x7E0163, (uint8_t)((c + 1 > last) ? first : c + 1)); return; }   // 1B19
    if (upPressed)   { w8(m, 0x7E0163, (uint8_t)((c == 0 || c - 1 < first) ? last : c - 1)); return; }  // 1B29
    if (aPressed)    { w8(m, 0x7E0162, 3); return; }         // 1AF9-1AFB: confirm
    return;                                                  // 1B18 RTS
  }
  if (d != 1) {                                              // 1AFE: $0162 == 3, retire the cursor
    int c = r8(m, 0x7E0163);
    if (!(c & 0x80) && c != 4) { w8(m, 0x7E0166, (uint8_t)c); w8(m, 0x7E0163, 4); }
  }
  if (aPressed) { w8(m, 0x7E0134, 0); w8(m, 0x7E0135, 0); }  // 1B0C-1B16: the press is spent
}

// Pick option N (0-based among the option lines) in a decision box. The script's result ($7F0A80,X, which op
// 0x1A branches on) is the cursor's line ($66, over $64..$65; C0/36CC), not a 0-based index, so line = first
// option line ((operand2 >> 2) & 3) + N.
void CtSceneChoose(CtScene* sc, int option) {
  int line = option + (sc->tbOpt >= 0 ? (sc->tbOpt >> 2) & 3 : 0);
  // Park the cursor on that line ($0163) and raise the confirm ($0162 = 3, C0/1AFB), the same confirm the A
  // press raises. The cursor arm (CtSceneDecisionInput) then does C0/1AFE ($0166 = $0163, $0163 = 4) and the
  // box machine's state 7 sees the 3 and closes.
  if (r8(sc->mem, 0x7E0162) != 0) {
    w8(sc->mem, 0x7E0163, (uint8_t)line);
    w8(sc->mem, 0x7E0162, 3);
  }
  // The result is written on the completion visit, not here: C0/365D (and C0/36C6 / C0/36FA) is `LDA #00 /
  // STA $7F0A00,X / LDA $66 / STA $7F0A80,X / STZ $62 / ... SEC / RTS`, the arm that advances the IP.
  // C0/1FDD reads $7F0A80,X while the box is still alive.
  sc->tbPendChoice = line;
  CtSceneAdvance(sc);                                 // the decision ops park on the same $0129 gate
}

void CtSceneGetFlags(const CtScene* sc, uint8_t* out0x200) { memcpy(out0x200, sc->mem->g, 0x200); }
// C0/9DC3 (BRL from C0/5926, the end of the load's object pass): its WRAM half. The host (field.c
// fieldArrivalTileMode) runs the tile half -- C0/9AC8 on the arrival tile $0102/$0103, $66 = 80 / $68 = FF, C0/9AD3 --
// and hands in its $6A/$6E/$6F. `onMap` = 0 when the fetch fell off the map (none of 9DEA-9E22 then).
//   9DC3-9DC9  STZ $01AB / $01AC / $01AD          9DCC-9DDA  $7F0C00 / 0C80 / 0D00 / 0D80 = 0
//   9DF8-9DFE  $0155 = $6A, or 1 when it is 0
//   9E01-9E0B  the lead ($0197): $0C00,X = $6E, $0C01,X = $6F
//   9E0E-9E1D  the other two ($0199, $019B): both halves = $6F
void CtSceneArrivalPriorities(CtScene* sc, int onMap, uint8_t z6a, uint8_t p6e, uint8_t p6f) {
  if (!sc) return;
  Mem* m = sc->mem;
  w8(m, 0x7E01AB, 0); w8(m, 0x7E01AC, 0); w8(m, 0x7E01AD, 0);
  w8(m, 0x7F0C00, 0); w8(m, 0x7F0C80, 0); w8(m, 0x7F0D00, 0); w8(m, 0x7F0D80, 0);
  if (!onMap) return;
  w8(m, 0x7E0155, z6a ? z6a : 1);
  uint32_t x = r8(m, 0x7E0197); w8(m, 0x7E0C00 + x, p6e); w8(m, 0x7E0C01 + x, p6f);
  x = r8(m, 0x7E0199); w8(m, 0x7E0C00 + x, p6f); w8(m, 0x7E0C01 + x, p6f);
  x = r8(m, 0x7E019B); w8(m, 0x7E0C00 + x, p6f); w8(m, 0x7E0C01 + x, p6f);
}
// The $7E image (64 KB); with CtSceneMemGlb, the globals page ($7F0000, 0x200). The battle transfer (field.c,
// C0/1770-18D6) and battle.c operate on them directly. NULL-safe: the overworld holds a NULL scene.
uint8_t* CtSceneMem7E(CtScene* sc) { return sc ? sc->mem->s : NULL; }
// C0/56D4 again, as C0/0304's tail runs it after a battle: the script image back at $7F:2000 (the battle entry's
// C0/0918 stashed the object page $0920-$1DFF there). The same bytes CtSceneCreateIn wrote; what lies past the
// script keeps the stash, as the cartridge's decompression leaves it.
void CtSceneScriptImage(CtScene* sc) {
  if (!sc || !sc->raw || sc->rawLen <= 0) return;
  uint8_t* img = (uint8_t*)sc->mem + 0x12000;
  memcpy(img, sc->raw, (size_t)(sc->rawLen < 0xE000 ? sc->rawLen : 0xE000));
}

static int vmSoundPostMem(Mem* mem, int blen, int startLine) {   // the `JSL $C7:0004` the six sound ops end with
  // CT_SNDTRACE=1: liveness for the VM's six sound ops, printed before the sink runs (the teardown below zeroes
  // $1E00). Reports whether a sink is installed (ct.exe installs none, and the ops are then inert).
  { static int tr = -1; if (tr < 0) { const char* e = getenv("CT_SNDTRACE"); tr = (e && *e) ? 1 : 0; }
    if (tr) { const uint8_t* m = mem->s + 0x1E00; const uint8_t* L = mem->s + 0x0100;
              fprintf(stderr, "[snd] blen=%-4d rawloc=%-3d cmd=%02X p1=%02X p2=%02X p3=%02X sink=%d\n",
                      blen, L[0] | (L[1] << 8), m[0], m[1], m[2], m[3], gSoundSink.post ? 1 : 0); } }
  int rr = -1;
  if (gLoadPostOn && gSoundSink.post && mem->s[0x1E00]) {      // the load's last JSLs: queued for its last frame (above)
    if (gLoadPostN < 32) { gLoadPostCyc[gLoadPostN] = g_ctLzSpriteCycles; memcpy(gLoadPost[gLoadPostN++], mem->s + 0x1E00, 4); }
    w8(mem, 0x7E1E00, 0x00);
    return startLine;                                          // nothing crossed here: the load's black prices it
  }
  if (gSoundSink.post) rr = gSoundSink.post(gSoundSink.ctx, mem->s, startLine);
  // ...and the callee's teardown, which is unconditional. C7/0150 does `LDX #1E00 / PHX / PLD`, so `$00` in
  // bank $C7 is $7E1E00, and the routine's one exit C7/0192 `SEP #20` / C7/0194 `STZ $00` is reached from every
  // path, including the early ones (C7/0157 `LDA $05 / BPL`, C7/015B `LDA $00 / BEQ`). So the command byte is
  // zero after the JSL returns, sink or no sink.
  // Note: through w8, not a raw store: a raw stage-and-clear XORs to zero within one tick and would drop the
  // byte out of the write mask.
  w8(mem, 0x7E1E00, 0x00);                     // C7/0192-0194
  return rr;
}
int CtSceneLoadPostsFlush(CtScene* sc, int count) {    // the next `count` queued JSLs (gLoadPost; < 0 = all), in order, each with
  int n = gLoadPostN, k0 = gLoadPostDone;              //   its mailbox
  if (count >= 0 && k0 + count < n) n = k0 + count;
  gLoadPostDone = n;
  if (gLoadPostDone >= gLoadPostN) gLoadPostN = gLoadPostDone = 0;
  if (!sc) return -1;
  // ...and priced like every other JSL, each from the line the one before it returned on (vmSoundPostAcc). A song an
  // init script plays is uploaded inside the load with the NMI off, so the load's black lasts as long as the upload.
  int ln = 12, fr = 0, seen = 0;
  for (int k = k0; k < n; k++) {
    for (int b = 3; b >= 0; b--) w8(sc->mem, 0x7E1E00 + (uint32_t)b, gLoadPost[k][b]);
    seen |= vmSoundPostAcc(sc->mem, sc->blen, &ln, &fr);
  }
  return seen ? fr * 1024 + ln : -1;
}
uint8_t* CtSceneMemGlb(CtScene* sc) { return sc->mem->g; }
uint8_t* CtSceneMemLoc(CtScene* sc) { return sc->mem->l; }

void CtSceneFree(CtScene* sc) {
  if (!sc) return;
  if (sc->ownMem) free(sc->mem);      // a host-owned image outlives the scene
  free(sc->raw); free(sc);
}
