// ct_vm.c — scene-script init interpreter in C, a faithful port of assets/script.py
// run_func (the init subset: flow + memory + actor ops). Run over the packed bytecode it
// reproduces the field actor placement the Python VM packs as CT_ASSET_ACTORS.
#include <stdio.h>
#include "romview.h"     // the ROM's small tables from ct_assets.dat (CT_ASSET_TABLE)
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>   // offsetof: the CtWram layout assert
#include <stdlib.h>
#include <math.h>
#include "move.h"
#include "seq_xlat.h"      // CtXlatWipeCall: the window wipe C3/01E4 translated      // CtObjSceneInit / CtObjSlotAlloc / CtObjListLink: the
                       // scene-entry tables and C0/A88D's rising-edge setup
#include "ct_vm.h"
#include "hdma_table.h"     // FD/C2C1, the field's HDMA table builder (CtHdmaTableBuild)
#include "rom_verify.h"
#include "objslot.h"       // FD/E39C, the palette-effect records op 0x88 arms (the battle's body)
#include "lz.h"
#include "field_cost.h"     // ct_vm_op_cycles: the load's object pass, op by op
#include "tables/ct_regroup.h"          // baked ROM battle-RETURN regroup script templates (tools/_gen_regroup.py)
#include "wipe.h"                     // CtWipeRoutine: the window wipe C3/01E4 and its seed C3/02DD, native
#define CTPI 3.14159265358979323846

// ---- the sound sink (ct_vm.h; disasm/notes/audio_commands.md §6) -------------
// The VM stages sound commands in its own WRAM at $1E00-$1E03 exactly as the ROM does, then
// hands off through this hook -- the C analogue of `JSL $C7:0004`. Keeping the mailbox in WRAM
// (rather than passing arguments) is what makes it faithful: $1E03 is STICKY across commands
// and the ROM depends on that, which a parameter-passing API would quietly erase.
static CtSoundSink gSoundSink = { 0, 0, 0 };
void CtSceneSetSoundSink(const CtSoundSink* sink) {
  if (sink) gSoundSink = *sink;
  else { gSoundSink.ctx = 0; gSoundSink.post = 0; gSoundSink.port = 0; }
}
// THE LOAD'S LAST JSLs. A location load runs its scripts' init (the init runner C0/5709) and obj0's slot 1 (C0/56C8 ->
// C0/595C) at its END, after the decompressions, with the NMI off: on the cartridge their sound commands reach the SPC in
// the load's last frame, the one C0/2848 begins in (the Lavos chain's 1D3: `10 0D` from C0/1B8B at f2227 as the load
// starts, slot 1's `E8 FF` -> `18 FF` at f2249 line 71, C0/2848 at f2249 line 89; tools/sound_post_cmp.py). The port runs
// the whole load in its first frame and holds the black after it, so a host that prices the load that way
// (CtSceneLoadPostsMode) gets those JSLs queued here -- the mailbox as each left it, $1E03 included -- and posts them with
// CtSceneLoadPostsFlush in the load's last frame. The load's own song post (C0/1B8B, at its start) is not queued.
// Each queued JSL keeps WHERE in the load it ran: the object pass's decompressions priced so far (g_ctLzSpriteCycles,
// the sprite loads of the objects whose init ran before it). The host posts it as many frames before the load's end as
// the work after it takes (CtSceneLoadPostCycles): the attract's 0A5 load posts its `10 18` at f4220 and reaches C0/2848
// at f4234 -- the later objects' sprite loads -- where the Lavos chain's 1D3 load posts slot 1's `18 FF` on C0/2848's frame.
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
  1,1,1,1,1,2,2,3,4,1,1,2,1,1,1,1,   // 0xB7 ANIMEREPEAT = op + anim + count (the handler reads B[+1]/B[+2]); was 1
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
  if (op == 0xCD || op == 0xCE) return 3;          // ADD/SUB GOLD: op + 2-byte amount (verified C0/385B/3874:
  if (op == 0xCC) return 4;                        //   read 2B then INX INX). CHECK-GOLD adds a 1-byte jump (C0/3827).
  return OPLEN[op];
}

// $C1 item/gold family opcodes this runner has executed. Read by CT_BAGDUMP (field.c) as the
// anti-vacuity half of `field-item-op-live-vm`: an inventory line printed after zero family ops
// grades nothing, and for 324 of these sites this VM used to run exactly zero of them.
int g_ctVmItemOps;
int g_ctVmAnimOps;   // liveness: anim-family ops the live VM ran through the shared body (CtVmAnimFamily)

// Same, for the $C2 party/roster family (ops 0xCF/0xD0/0xD1/0xD7/0xF8/0xF9/0xFA): 153 sites this
// runner used to step over. Read by CT_BAGDUMP as the anti-vacuity half of its pin.
int g_ctVmPartyOps;

// DEBUG-flag cache: env vars never change mid-process, so read each ONCE (lazy) instead of
// calling getenv() per VM op (applyEffect/step) / per frame (CtSceneTick). Behavior-identical.
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

// Script memory: global $7F0000, local $7F0200, system $7E0000; 16-bit big-endian.
// TAGGED as CtWram because it is no longer the SCENE's: in the ROM these 64 KB are
// process-lifetime and a location change never wipes them (disasm/notes/wram_lifetime.md 1),
// so the host owns one of these for the whole run and hands it to each CtScene.
// ⚠ THE FIELD ORDER IS LOAD-BEARING. Laid out so the struct IS a flat 128 KB WRAM image -- $7E at
// [0, 0x10000) and bank $7F at [0x10000, 0x20000) -- which is exactly the linear index wmIndex() already
// hands out, and exactly what the OVERWORLD engine expects: world.c reads its bank-$7F scripts as
// `ram + 0x10000 + pc`. That is what lets the world run on the HOST image instead of a private copy.
// `s` is therefore the base of the whole image, not just of $7E; g and l are views into bank $7F at
// their real offsets. Reorder these fields and the overworld silently reads the wrong bank.
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
// COLD INIT -- the port's C0/000E. Reached at reset, and in the ROM also from the in-game load-game
// path ($7F01EC==1 -> C0/0283 -> C0/02CA -> C0/000E, wram_lifetime.md 3), which the port does not have
// yet. This is the ONLY place the 64 KB is zeroed; a location change must NOT come through here.
static void memBoot(Mem* m) {
  memset(m, 0, sizeof *m);
  // ⚠ UNCITED PORT SEEDS, kept because removing them is a behavioural change nobody derived. The
  // write census says the load does not write $0104 / $1BA8 / $1BAA / $1BAB at all, so re-applying
  // them on every load would invent stores the ROM never makes -- they belong to cold init or to
  // nothing. ($1BA7 IS load-written, by the init runner's SIXTEEN-BIT `58F0 STZ $1B80,X` clearing the
  // pair $1BA6/$1BA7 -- the REP #20 at 58E2 is still in force -- so its seed only survives to the
  // first load.) Needs its own derivation; see wram_load_census.md.
  // ⭐ $1BAB's seed is GONE, and that one is derived rather than inherited. The oracle holds it at
  // ZERO everywhere measured -- boot f96/f100/f141, the settled title at f2060, scene 433 at f2280,
  // the attract's own overworld, all eight `ow_w<n>.live` captures, and the Truce-market door, where
  // `--monitor 7E1BAB:1 --diff --writer` reports no write at all across the transition. The census
  // already said the load never writes it, so the port's `|= 1` was permanent. It is not harmless:
  // the overworld BOOTSTRAP branches on it (world 0's `27 AB 1B 01 08` -- "if ($1BAB & 1) == 0,
  // jump"), so the stale bit sent the script down the wrong arm, where it spawned ONE object and
  // died instead of building the world (sequence_table.md 9.5).
  m->s[0x0104] |= 192; m->s[0x1BA7] |= 14; m->s[0x1BAA] |= 4; m->s[0x1BA8] |= 16;
  // NEW-GAME PARTY: Crono only, the other two active slots and all six reserve slots EMPTY (0x80 = bit7).
  // Cold init, not scene init -- the ROM's location load reads $2980-82 and never writes them, so a port
  // that re-seeded per load would erase a follower every door. Leaving the reserve zeroed instead of 0x80
  // would read as "character 0" six times to the Form screen's roster builder (C2/CD2B walks $2980,X for
  // X=3..8); every oracle state measured has 80 x6 there.
  m->s[0x2980] = 0x00; m->s[0x2981] = 0x80; m->s[0x2982] = 0x80;
  for (int rsv = 3; rsv < 9; rsv++) m->s[0x2980 + rsv] = 0x80;
}

// WHAT A LOCATION LOAD DOES TO MEMORY THAT SURVIVES IT (disasm/notes/wram_load_census.md 1).
// Measured, not chosen: the same four windows on two different source states. NOT ONE OF THEM IS AN
// INSTRUCTION -- all four are DMA channel 7, B->A, reading $2134 (MPYL with the multiplier forced to
// 0, i.e. a hardware zero source) set up by C0/2DF1 -- which is why no STA/STZ scan ever found them
// and why the port had nothing to copy for so long.
//
//   C0/7F87 -> C0/7F95   $7E:1D00-1DFF    256 B
//   C0/5709 -> C0/5717   $7F:0200-03FF    512 B   the script locals (settles events.md:40, YES)
//   C0/58AB -> C0/58B9   $7E:1900-19FF    256 B
//   C0/58BC -> C0/58CA   $7F:0580-1B3F   5568 B   OUTSIDE the modelled image -- see below
//
// Everything else in $7E SURVIVES. That is the whole point: the load rewrites an enumerable set of
// spans into a 64 KB that persists, and the port must let it, not hand it a zeroed page.
// C0/0B86-0C75 -- the location load's CONSTANT-INIT pass over the engine's direct page (D=$0100) plus
// a handful of absolutes. The port never modelled this routine: it got the same EFFECT for free from
// zeroing the whole image on every scene create, which is precisely the coupling the ownership move
// exposes. `sc->locChange`'s comment already named one line of it ("the new scene's memInit models the
// load's STZ $17, C0/0B96") -- there are about seventy.
//
// Every store below was read with `dis.py 0B86 <len> 1 0` and then CONFIRMED against the oracle with
// `--loadcensus --censusat`, 19 of 19 spot checks matching value AND writer PC (wram_load_census.md 4:
// the census reports the instruction AFTER the store, so `0BA0 STA $21` is seen as C0/0BA2). A linear
// disassembler desyncs; the measurement is what licenses the transcription.
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
  s[0x01B1] = s[0x01B4] = s[0x01B7] = s[0x01BA] = 0xE4;   // 0BBA LDA #E4 / 0BBC..0BC2 the four pointer BANKS
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
  // 0BF4 REP #20 -- everything from here to 0C1C is SIXTEEN-BIT, so each STZ clears a PAIR.
  s[0x012B] = 0; s[0x012C] = 0;                      // 0BF6 STZ $2B
  // 0BF8-0C0E: four 24-bit pointers, the WORDS from ROM $E4:FFE0/E2/E4/E6 (= 2000 2300 2600 2800)
  // beside the $E4 bank bytes stored above. Baked rather than read: the VM is ROM-free at runtime.
  { static const uint16_t p[4] = { 0x2000, 0x2300, 0x2600, 0x2800 };   // rom16($E4:FFE0 + 2n)
    static const int dst[4] = { 0x01AF, 0x01B2, 0x01B5, 0x01B8 };
    for (int i = 0; i < 4; i++) { s[dst[i]] = (uint8_t)p[i]; s[dst[i] + 1] = (uint8_t)(p[i] >> 8); } }
  s[0x0150] = 0; s[0x0151] = 0;                      // 0C10 STZ $0150 (absolute, 16-bit)
  s[0x2000] = 0; s[0x2001] = 0;                      // 0C13 LDA #0000 / 0C16 STA $7E2000
  s[0x0158] = 0; s[0x0159] = 0;                      // 0C1A STZ $58
  // 0C1C SEP #20 -- 8-bit again.
  s[0x0197] = s[0x0199] = s[0x019B] = 0x80;          // 0C1E LDA #80 / 0C20 / 0C22 / 0C24
  s[0x018D] = s[0x018E] = s[0x018F] = 0x80;          // 0C26 / 0C28 / 0C2A
  s[0x0191] = s[0x0190] = s[0x0192] = s[0x0193] = 0x80;   // 0C2C / 0C2E / 0C30 / 0C32
  s[0x0194] = s[0x2980]; s[0x0195] = s[0x2981]; s[0x0196] = s[0x2982];   // 0C34-0C44: the ACTIVE PARTY,
  // read and never written -- wram_lifetime.md 4 measured $2980-82 surviving a load as sentinels.
  s[0x01EB] = s[0x01AE] = 0x80;                      // 0C46 LDA #80 / 0C48 / 0C4A
  s[0x0155] = 0x01;                                  // 0C4C LDA #01 / 0C4E STA $55
  s[0x01FC] = 0x53; s[0x01FB] = 0x65; s[0x01FA] = 0x45;   // 0C50-0C5A
  s[0x2989] = 0;                                     // 0C5C LDA #00 / 0C5E STA $7E2989
  s[0x0BD9] = s[0x0BDA] = s[0x0BDB] = s[0x0BE9] = 0; // 0C62-0C6D
  s[0x01F8] = s[0x0400];                             // 0C70 LDA $0400 / 0C73 STA $F8
  // CT_F8TRACE: the seed, one line per location load -- the frames of the play clock $0400 that C0/0C70 hands the $C0:FE00
  // cursor (op 0x7F, the mover's bounce, the blink). The cartridge's side: PEEK_WRITEWATCH=01F8:1 (writer C0/0C75).
  { static int tr = -1; if (tr < 0) tr = getenv("CT_F8TRACE") ? 1 : 0;
    if (tr) printf("f8seed: loc %03X C0/0C70 $01F8 = %02X\n", s[0x0100] | (s[0x0101] << 8), s[0x01F8]); }
}

static void memLocationLoad(Mem* m) {
  memset(m->s + 0x1D00, 0, 0x100);                   // C0/7F95
  memset(m->l, 0, 0x200);                            // C0/5717 -- $7F:0200-03FF
  memset(m->s + 0x1900, 0, 0x100);                   // C0/58B9
  // C0/58CA clears $7F:0580-1B3F -- and the port DOES model that span now (Mem::hi, $7F:0400+, added
  // for the drain's sprite templates and seeded by every state), so the clear finally belongs here.
  // What lives in it: the per-object textbox latch $7F:0A00,X (C0/3580/3588/3635), the op-0xAD wait
  // counter $7F:0A01,X (C0/2F96), the CALL wait flag $7F:0980,X, the op-result page $7F:0A80,X, the
  // $7F:0B00,X move lifecycle and the party velocity ring $7F:0C00-0F7F.
  // WHAT IT COST TO LEAVE OUT (the user's "example of a text that does not occur", capture of the wake-up in
  // loc 2 spliced to saves/field_re/txtmiss_lake.state): op 0xBB's arms are chosen by that latch --
  // clear = REQUEST (post the box, park), set = COMPLETION (clear it, advance past the op). A latch left
  // non-zero from before the location load makes the scene's FIRST box take the completion arm and
  // vanish without ever reaching the box machine. Measured on --newgame: Mom (obj8) ran her `BB 06` at
  // body $030A with $7F:0A10 = $0B and the box was swallowed; the port's first bedroom box was her NEXT
  // line, `BB 07` (Mom's line about Leene's Bell), while the oracle shows string 6
  // (Mom's wake-up line) -- the line the user reported never appears.
  memset(m->hi + (0x0580 - 0x0400), 0, 0x1B40 - 0x0580);   // C0/58CA
  //
  // The load's own constant fills, in the same pass. Census-confirmed writers, +1 per the reported-PC
  // convention: $0920-092F = 0xFF (C0/5929, seen as C0/592E), $0B80-0B87 = 0x80 (C0/7155, seen as
  // C0/715A). ⚠ The heap cursor below is at the WRONG ADDRESS and is left that way deliberately:
  // `58DA STX $71` is DIRECT-PAGE with D=$0100 (C0/58AA PLD), so the ROM writes $7E:0171/0172 and
  // never touches $7E:0071/0072 across a whole load. The port is self-consistent at its own address,
  // so moving it is a separate, separately-validated change.
  m->s[0x71] = 0x00; m->s[0x72] = 0x80;              // C0/58D7 (see the address caveat above)
  memset(m->s + 0x920, 0xFF, 16);                    // C0/5929
  memset(m->s + 0xB80, 0x80, 8);                     // C0/7155
  memLoadInit0B86(m);                                // C0/0B86-0C75, the direct-page constant init
  // C0/56A6, the object init's head (C0/008E, every location load): `LDX #0048 / STX $69 / LDX #0078 / STX $6B / STZ $09A0`
  // (DP $0100) -- the two RASTER DEADLINES: the event runner's (C0/5A5D `CMP $69`) and the anim drain's (C0/CA99 `CMP $6B`,
  // op 0x47 moves it). The port never stored them: $0169/$016B read 0000 in its WRAM (the cartridge's: 0048 / 0078), which
  // nothing noticed while the drain ran without its deadline (a virtual beam that advances needs the ROM's line).
  m->s[0x0169] = 0x48; m->s[0x016A] = 0x00;          // 56A6-56A9
  m->s[0x016B] = 0x78; m->s[0x016C] = 0x00;          // 56AB-56AE
  m->s[0x09A0] = 0x00;                               // 56B0
  // C0/28AA, the OPENED-DOOR LIST's reset (CtWramDoorListAppend's list): `REP #20 / LDA #8080 / LDX #0000 /
  // STA $7F1CC8,X / INX / INX / CPX #0020 / BCC` -- sixteen empty words. It is the leg C0/0091 runs after C0/56A6 and
  // before C0/2848, on every location load C0/0066 reaches (not the overworld's JML C2:0000 at C0/006E, not the
  // C3 sequences' C0/007A, not the menu's or the battle's reload); nothing else in the ROM touches the list, so running it
  // in this pass, ahead of the object load, leaves the same list at C0/286C.
  for (int k = 0; k < 0x20; k++) m->hi[0x1CC8 - 0x0400 + k] = 0x80;
}

static void memInit(Mem* m, const uint8_t* gmem) {
  memBoot(m);
  memLocationLoad(m);
  if (gmem) memcpy(m->g, gmem, 0x200);
}

#ifdef _WIN32
// ===== CT_WATCH -- A REAL WRITE WATCH ON ONE WRAM BYTE (hex offset into the flat image) ========
// FOUR PASSES OF BISECTION FAILED to name the writer of $7E:0E7E; this found it on the first run, and
// the lesson is worth more than the bug: when a byte changes and you cannot say who changed it, make
// the STORE identify itself instead of bisecting the caller. The page holding the byte is set
// read-only, a vectored exception handler catches the access violation, and
// ExceptionInformation[1] carries the exact faulting address -- so only the watched byte is reported
// while every other write to the same 4K page is stepped over silently. On a hit it prints the
// faulting instruction and resolves it to function + file:line with dbghelp.
//
// Inert unless CT_WATCH is set. The WRAM is page-aligned only in that case, because VirtualProtect
// works on whole pages and would otherwise also trap the allocator's metadata sharing the page.
// ⚠ Single-threaded assumption: the handler unprotects, single-steps and re-arms, so a store from
// another thread during that window is missed. Fine for the field engine, not for the SDL audio
// thread if that ever touches WRAM.
// Example -- `CT_WATCH=0E81 Belthasar --scene 1 --worldtest` prints every writer of object 0's
// draw-list next-pointer, with the two list routines and the A8EC/A8EF one-shot each named.
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
      // ⚠ DO NOT filter on `hit == gWatchAddr`. A WIDE store (a memset, or any vectorised fill) that
      // covers the watched byte faults at the store's FIRST address, not at our byte, so an
      // address-equality filter reports nothing and silently steps the whole thing through. That
      // false negative cost a full pass: it produced a "the only writer is CtObjSceneInit" census
      // that was simply wrong. Instead: remember the value, step, and compare after.
      gWatchShadow = *gWatchAddr; gWatchPc = ep->ExceptionRecord->ExceptionAddress;
      // THE FAULTING PC IS NOT ENOUGH when the store is inside a CRT bulk routine -- it names memset,
      // not the caller. Walk the FAULTING frame's stack (a copy of ContextRecord, so the unwinder can
      // scribble on it) and keep the return addresses; they are resolved only if the byte really moved.
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
    if (gWatchAddr && *gWatchAddr != gWatchShadow) {                 // the step CHANGED our byte
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
  // PAGE-ALIGNED under the write watch so the protected page holds nothing but our WRAM -- otherwise
  // VirtualProtect would also trap the allocator's own metadata sharing the page.
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
  if (a >= 0x7F0400 && a < 0x800000) return m->hi[a - 0x7F0400];   // the rest of bank $7F (the textbox latch
  return 0;                                                          //  $7F0A00,X among it) is in the image too
}
// Linear index over the modelled image: [0,0x10000) = $7E, [0x10000,0x10200) = $7F0000,
// [0x10200,0x10400) = $7F0200. This is ALSO the offset into the oracle's 128KB WRAM buffer,
// which is why the mask can index both sides with one number.
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
static uint8_t gWmSeedO[WM_LEN];   // the ORACLE's image at reset -> the coverage denominator
static uint8_t gWmOchg[WM_LEN];    // bits the oracle has changed since the seed
static int32_t gWmFirst[WM_LEN];   // frame each address first diverged (-1 = never)
static int gWm = 0, gWmDropped = 0;

static void w8(Mem* m, uint32_t a, int v) {
  v &= 0xFF;
  if (a >= 0x7E0000 && a < 0x7F0000) m->s[a - 0x7E0000] = v;
  else if (a >= 0x7F0000 && a < 0x7F0200) m->g[a - 0x7F0000] = v;
  else if (a >= 0x7F0200 && a < 0x7F0400) m->l[a - 0x7F0200] = v;
  else if (a >= 0x7F0400 && a < 0x800000) { m->hi[a - 0x7F0400] = v; return; }   // the rest of bank $7F IS in the
                                            // image (hi, seeded from every state): the textbox latch $7F0A00,X
                                            // (C0/3588 / 3635) lives there and was being dropped on the floor.
  else { if (gWm) gWmDropped++; return; }   // OUTSIDE the model -- silently dropped until now.
                                            // Script ops take arbitrary absolute destinations, so
                                            // a write to $7F0400+ vanished with no trace. Counted.
  if (gWm) { int i = wmIndex(a); if (i >= 0) gWmMask[i] = 0xFF; }   // a byte store claims the byte
}

// C0/0C9D-0CBE -- the COMMIT half of the ROM's pending-location change, and the only copy of it.
// Remember where we came from (the live location word -> $0105/6, the lead's tile X/Y and facing^1
// -> $0107/8/9), then copy the staged destination into the loader slots: $0112 -> $0100 and
// $0114 -> $0102 as 16-bit stores, $0116 -> $0104 as an 8-bit one. That last width is measured, not
// assumed: $0117 reads 0x80 at commit time while $0105 lands on the OLD location's low byte, so the
// store cannot be 16-bit -- $0105 is written earlier, by 0C9D/0C9F.
// It is a WRAM operation, not a scene one: the ROM's copy runs in the field frame loop with DP=$0100
// and touches nothing else, and our two triggers for it (the $0119 drain, and a positional exit whose
// fade outran that drain) do not both have a live CtScene.
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
// 16-bit WRAM words are LITTLE-endian, as the 65816 stores them. These were big-endian (the CTViewer convention the
// VM started from): self-consistent for the locals ops 0x13/0x15/0x50 used among themselves, but a split byte order
// against every shared ROM-graded body (move.c's variable grid, 0x4D) and WRONG for C0/42C9's `LDA #0008 / STA
// $0D00,X` (pcArrivalArm), which stored $0D00 = 00, $0D01 = 08.
static int r16(Mem* m, uint32_t a) { return r8(m, a) | (r8(m, a + 1) << 8); }
static void w16(Mem* m, uint32_t a, int v) { w8(m, a, v & 0xFF); w8(m, a + 1, (v >> 8) & 0xFF); }

// ---- THE OPENED-DOOR LIST, $7F:1CC8 (disasm/notes/field_door_list.md) ----------------------------------------------
// Sixteen 16-bit tile indices into the $7E:3000 map ((row << 8) | col, the door request's $015B), $8080 = empty; packed,
// so a walk ends at the first negative word. Three routines own it and nothing else in the ROM touches it:
//   C0/28AA  the reset, one of the location load's legs (C0/0091)  -> memDoorListReset, in memLocationLoad
//   C0/28C0  the append, C0/0D49 on every door request              -> CtWramDoorListAppend
//   C0/28E1  the re-apply, C0/286C's first JSR (a location load's C0/2848, the reload's C0/2824) and C0/18C4 after a
//            battle -- each of them right after the map was decompressed afresh, doors closed -> CtWramDoorListApply
// The map is addressed the ROM's way: `$7E3000,X` long-indexed with a 16-bit X, so a row above 0 wraps X and the byte
// lands at $7E3000 + X, into bank $7F past $7EFFFF -- r8/w8 take the 24-bit address as is.
#define DOORLIST 0x7F1CC8u
static uint32_t doorCell(int x) { return 0x7E3000u + (uint32_t)(x & 0xFFFF); }
// C0/28C0, called from C0/0D49 with the request's tile index in $015B -- BEFORE the handler dispatch, so it runs for
// every door request whatever metatile the tile turns out to hold:
//   28C0 REP #20 / LDX #0000
//   28C5 LDA $7F1CC8,X / BMI ->28D9 ; CMP $5B / BEQ ->28D6 ; INX / INX / CPX #0020 / BCC ->28C5
//   28D6 SEP #20 / RTS
//   28D9 LDA $5B / STA $7F1CC8,X / BRA ->28D6
// Already listed: nothing. Full (sixteen entries, no empty word): nothing -- the seventeenth door is not remembered.
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
// C0/28F9, one entry: the map byte at the listed tile selects an arm by its CLOSED id, and each arm writes the block's
// OPEN ids as constants (not INCs -- only the first cell is `INC A`), the same blocks C0/0D78..149D bump:
//   E6 291C  (r,c)=E7  (r-1,c)=E7
//   EC 2933  (r,c)=ED  (r,c+1)=EF  (r-1,c+1)=EB  (r-1,c)=E9
//   EE 2958  (r,c)=EF  (r,c-1)=ED  (r-1,c-1)=E9  (r-1,c)=EB
//   FA 297D  (r,c)=FB  (r,c+1)=FD  (r-1,c+1)=F9  (r-1,c)=F7  (r-2,c)=F3  (r-2,c+1)=F5
//   FC 29BA  (r,c)=FD  (r,c-1)=FB  (r-1,c-1)=F7  (r-1,c)=F9  (r-2,c)=F5  (r-2,c-1)=F3
// Any other id (an already-open door, a tile that never was one) -> 2918, nothing written. Returns 1 when an arm ran.
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
// C0/28E1:
//   28E1 REP #20 / LDX #0000
//   28E6 LDA $7F1CC8,X / BMI ->28F6 ; JSR C0/28F9 ; INX / INX / CPX #0020 / BCC ->28E6
//   28F6 SEP #20 / RTS
// Returns the number of entries whose arm ran (a trace number, not a ROM value).
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
// C0/0D4C .. the five door handlers, their $7E:3000 half: `0D4C LDX $5B / LDA $7E3000,X` dispatches on the metatile's
// VALUE and each handler INCs a fixed block. Every INC is 8-bit but ONE: the E6 handler's second cell,
//   0D7D REP #20 / TXA / SEC / SBC #0100 / TAX / LDA $7E3000,X / INC A / STA $7E3000,X / SEP #20
// is a 16-bit read-modify-write of the word at (r-1,c) -- it writes (r-1,c+1) too, carrying into it from $FF.
//   E6 0D78  (r,c) ; word (r-1,c)
//   EC 0E5F  (r,c) (r,c+1) (r-1,c+1) (r-1,c)
//   EE 1014  (r,c) (r,c-1) (r-1,c-1) (r-1,c)
//   FA 11C9  (r,c) (r,c+1) (r-1,c+1) (r-1,c) (r-2,c) (r-2,c+1)
//   FC 1454  (r,c) (r,c-1) (r-1,c-1) (r-1,c) (r-2,c) (r-2,c-1)
// Returns 1 when a handler ran, 0 for any other id (C0/0D75 BRL ->16DC). The render's copy of the map is bumped by
// CtFieldSceneDoorOpen (scene.c); this is the WRAM the ROM's own readers -- C0/28E1 among them -- see.
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
// The party-slot indirection, in ONE place in this runner so it is traceable and so the ROM's own
// resolver (move.c's ctVmPartySlotTarget, graded by --sweepops at 60 micro-executed cases for each
// of ops 0x22/0x24/0xA9) is what answers. CT_SLOTTRACE prints every resolution: it is the only
// headless signature this indirection has, since the resolved object usually stands on the same
// tile as the asker and the facing it produces is then identical either way.
// DEDUPED, because the master loop re-runs op 0x22/0x24 with n=0 every few frames and the raw
// stream is neither short nor stable enough to pin; each DISTINCT (op, n, result) prints once.
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

// The seven one-byte PC loaders that hardwire their char id instead of taking an operand.
// ⚠ 0x68 AND 0x6A USED TO BE SWAPPED HERE, which is the exact mistake move.c's own arm warns about:
// "THE OPCODE -> ID MAPPING IS NOT MONOTONIC IN THE HANDLER ADDRESS ... exactly the shape that
// invites `id = (op - 0x57) / something` and gets two of the seven wrong". Read straight out of the
// ROM -- the C0:5D6E jump table, then each handler's own immediate:
//   op 57 -> C0/41E4  LDA $6D / STA $8D / LDA #00      id 0  Crono
//   op 5C -> C0/41EC  ...     / STA $8E / LDA #01      id 1  Marle
//   op 62 -> C0/41F4  ...     / STA $8F / LDA #02      id 2  Lucca
//   op 6A -> C0/4204  ...     / STA $90 / LDA #03      id 3  Robo   <-- the LATER handler, LOWER id
//   op 68 -> C0/41FC  ...     / STA $91 / LDA #04      id 4  Frog   <-- the EARLIER one, HIGHER id
//   op 6C -> C0/420C  ...     / STA $92 / LDA #05      id 5  Ayla
//   op 6D -> C0/4214  ...     / STA $93 / LDA #06      id 6  Magus
// So every op-0x68 site loaded Robo where the ROM loads Frog, and every 0x6A the reverse.
static int pcSprite(uint8_t op) {
  switch (op) { case 0x57: return 0; case 0x5C: return 1; case 0x62: return 2; case 0x6A: return 3;
    case 0x68: return 4; case 0x6C: return 5; case 0x6D: return 6; default: return -1; }
}
// Exposed so --vm can PRINT the map: the packed actor table in ct_assets.dat is built from
// assets/script.py's own copy of it, so a check that only compares the two sides goes green when
// BOTH carry the same mistake -- which is exactly what happened with 0x68/0x6A.
int CtScenePcLoaderId(int op) { return pcSprite((uint8_t)op); }

static int mvAngle(int dx, int dy);                  // fwd: the C0/ABA2 atan (face-toward ops)
static const uint8_t* mvTables(void);                // fwd: [0x400]=F700 facing table

// ---- op-0x81/0x82/0x83 per-object WRAM load mirror (ROM C0/4476 / C0/4590 / C0/46DF, shared
// C0/4557) + the sprite gfx/palette ALLOCATORS they feed. The battle staging (C0/0557, run by
// field.c's $0117 consumer) reads these per-object bytes straight from the $7E image, so the
// loaders must WRITE them there like the ROM does:
//   $1101,X = enemy id (C0/4564)          $1200,X = 0x7F constant (C0/4579; gfx bank)
//   $1201,X = header size byte (C0/476A)  $0C00/$0C01,X = 0x20 (C0/4571, word 0x2020)
//   $1B01,X = 1 solid marker (C0/46EF)    $1100,X = operand2 bit7 ? 6 : 5 (C0/47EC-47F4)
//   $0D00,X = ((slot&0xFE)<<4)|((slot&1)<<3), slot = (operand2&0x7F)+1 (C0/47AC-47C6) -- the
//             byte C0/0557's screen-Y slot formula consumes (GATO 0x86 -> slot 7 -> 0x68 -> battle
//             slot 3; oracle-stable from load to staging, gato_t0/t1 both 0x68).
// All three loaders consume a 5-byte sprite header [gfx, assembly, palette, anim, size]:
// op 0x81 -> $E4:F000 + id*5, op 0x82 -> $E4:F000 + (id+7)*5, op 0x83 -> the field enemy
// record $E4:F600 + id*10 bytes 0-4 (C0/46F7-476A read E4F601..E4F604; C0/45EC..4620 read
// E4F023..E4F027). From it (oracle: fresh --teleport 6 state, GATO rec `7B 76 B6 57 02`):
//   $1380/$1381,X + $1300,X = assembly ptr addr16+bank = asmPtrTable[byte1] (C0/470C-4715;
//                             GATO 0x76 -> E3:0000)
//   $1400,X (word LE)       = byte2 * 24 = the $E4-bank palette offset (C0/471A-4734; the
//                             MVN source of the CGRAM upload; GATO 0xB6 -> 0x1110)
//   $1480/$1500/$1580,X     = anim set byte3: (fp[i+1]-fp[i])>>2 / fp[i] / dp[i] from the
//                             u16 anim ptr tables (C0/473B-475D; GATO 0x57 -> 28/72C0/BF09)
//   $1201,X                 = byte4 (size flags; GATO 02)
// GFX allocator (C0/5C90 over $0920 set-id / $0930 owner-objX, heap cursor $71 base 0x8000):
// share = a slot already holding byte0 -> copy the owner's $1280 word (C0/478F); fresh = claim
// the first 0xFF slot, $1280,X keeps the PRE-decompress cursor (C0/4557 `LDA $71`), then the
// cursor advances by the LZ-decompressed size of gfxPtrTable[byte0] (C0/47FE-4831, dest
// $7F:$71, wrap-below-0x8000 = the C0/2E1E diagnostic halt). Monster ids >= 0xF8 skip the
// allocator: $1280/$1200 = the gfx table entry itself, ROM-direct (C0/477A -> C0/4845).
// PALETTE allocator (C0/7170 dispatch on $1100): see palAlloc7170 / palRelease734C below.
// All ROM-table reads need the ROM image (CtSceneSetRomImage); without it fresh loads leave
// the bytes 0 exactly as before (a WRAM-seeded scene carries the oracle's values regardless).
static const uint8_t* gEnemyRecTab; static int gEnemyRecLen;
void CtSceneSetEnemyRecTable(const uint8_t* tab, int len) { gEnemyRecTab = tab; gEnemyRecLen = len; }
static const uint8_t* gRomImg; static long gRomImgLen;
void CtSceneSetRomImage(const uint8_t* rom, long len) { gRomImg = rom; gRomImgLen = len; }
// `JSL FD/C2C1` -- the field's HDMA table builder, native (src/hdma_table.c, disasm/notes/field_hdma_tables.md): one of the
// two table sets is written from the WRAM's state and $0153 bit 0 flips. Every caller of the port's runs it here; with no
// ROM image (a host that never set one) the flip alone is left, as before the builder was in.
void CtHdmaTableBuild(uint8_t* wram128) {
  if (!wram128) return;
  if (!gRomImg || CtHdmaBuildNative(wram128, gRomImg, gRomImgLen) != 0) wram128[0x0153] ^= 0x01;
}
// ---- THE FRAME LEGS SWITCH (disasm/notes/frame_wiring.md) ---------------------------------------
// C0/A810's four per-object legs (C98A / A98A / AB45 / A9CD) + the A88D edges' one-shot setup and
// teardown, C0/CA76's drain and C0/B271's draw pass are all wired in CtSceneTick below and all gated
// here. They are ONE chain -- the legs stage a frame into the four-slot arena, the drain commits it,
// and only C0/B701 inside the draw pass hands the slot back -- so they go on together or not at all.
//
// DEFAULT ON since 2026-08-09. `CT_FRAMELEGS=0` restores the old behaviour and is what
// `vmdiff-wmask-party3` uses to keep grading the legs-off baseline.
//
// Getting here took closing two blockers, both found by measurement rather than reading:
//   A. THE DRAW LIST WAS BEING OVERWRITTEN. `worldInit` (field.c:236) memcpy's the overworld's 64
//      object blocks over `$7E:0B30..$1B2F` -- OWOBJ_BASE = $0B30, the ROM's own address -- and that
//      span contains the field's `$0E00..$0EFF` draw list whole. FIXED by a GATE, not a relocation:
//      `--hookpc` from an overworld state reports ZERO visits/frame to every leg of the field frame
//      (0097, 00C4, 5A46, A810, B271, B204) with the CPU always in bank $C2, so the ROM never runs the
//      field engine while the world owns that RAM -- and neither do we now (`!inWorld`).
//   B. NOT A SECOND DEFECT. It was the anim frame `$0F01` arriving one step early in the battle
//      staging record, i.e. family 4 below.
//
// ⚠ FAMILY 4 IS AN ACCEPTED SCOPE LIMIT, and the derivation behind accepting it is CLOSED on both
// sides. `C0/CA76`'s loop has exactly two exits -- the raster deadline and an empty queue -- so there is
// no structural stop to port; and the temporal one needs the drain's START scanline, which is emergent
// from the port's own execution time (measured `vPos` at `00D6`: 13-19 on truce_market, 45-65 on
// party3_field). The deadline really does bite (13 of 129 frames; exits reach vPos 153 against a budget
// of 120; a whole pass costs 0-4 / 39-41 / 50-55 scanlines), so the port runs one drain visit further
// than the ROM on roughly a tenth of frames. The two `scanline` values available are both wrong in a
// known direction -- 0xF0 drains fully, `>= $016B` refuses -- and picking anything between them would be
// a tuned constant. Where the lead is visible it is EXCLUDED AND PRINTED, never tolerated: see the
// staged record's +4 byte in field.c's battletest.
//
// WHAT ON COSTS IN THE HARNESS, pinned by `vmdiff-framelegs-party3`: on `--vmdiff party3_field
// --frames 130 VMDIFF_WMASK=1` it reports four families:
//   1. the three PCs' $0A00 screen position and 2. their $1601 anim timer -- both DOWNSTREAM of state
//      the ROM's player layer (C0/881E, C0/9E84) produces and that this harness deliberately does not
//      run: it injects the PCs' position, the touch latch and the camera edges instead. So they are
//      harness-scope, not engine defects, and the live engine (which does run the player layer) does
//      not have them.
//   3. obj32's $0A40/$0A41 on the frame it activates, and
//   4. C0/CA76's RASTER CLOCK, which is structural: CA85-CA9B compares a live V counter that advances
//      WHILE the drain works, so a clockless port either drains fully or not at all. On the frame the
//      ROM's deadline splits a class-0 object's two-pass stage (measured: obj32 at f74 -- the oracle
//      does pass 1, appends the upload and stops; pass 2 lands at f75) the port does both at once and
//      commits $0F01/$1B00 one frame early. That cost was already measured before this wiring existed
//      ("the queue is still non-empty at a frame boundary on 1/90 frames for this scenario").
// Families 1-3 are HARNESS scope: they are downstream of state the ROM's player layer produces and that
// `--vmdiff` deliberately does not run, so the live engine -- which does run it -- does not have them.
// Family 4 is the raster clock and is not derivable without cycle-counting the whole drain.
// What ON still BUYS, and why this is worth closing A and B for: the arena lifecycle actually works --
// without the legs `$0BC0..$0BC3` leak all four slots ten frames apart -- and `$7F0B01` gets both of its
// ROM writers, which is what op 0xAB's completion needs.
static int gLegs = -1;
static int legsOn(void) {
  if (gLegs < 0) { const char* e = getenv("CT_FRAMELEGS"); gLegs = (e && *e == '0') ? 0 : 1; }
  return gLegs;
}
static uint32_t romU16(uint32_t off) { return (uint32_t)CtRomByte(gRomImg, gRomImgLen, (long)(off)) | ((uint32_t)CtRomByte(gRomImg, gRomImgLen, (long)(off + 1)) << 8); }

// ============================ THE OBJ PALETTE ALLOCATOR, C0/7170 ============================
// Ported WHOLE. What stood here before was a stand-in that collapsed the ROM's 12-class dispatch
// into three ranges, wrote 2 of the ROM's 4 claim stores, had NO release at all, and -- the actual
// defect the write-mask comparator found -- fired on SCRIPT VISIBILITY every frame instead of on
// the camera-window ACTIVATION EDGE.
//
// RAM MAP (all obj-indexed tables are obj*2 strided):
//   $0B80..$0B87  8 slot owners; 0x80 = free, else the owner's obj*2. Init C0/7155 (8 unrolled
//                 STA #80). The owner names the NEWEST chain member, not the original claimer.
//   $0B00,X       sharer link PREV (toward the original claimer); 0x80 = nil.  NEVER initialised
//   $0B01,X       sharer link NEXT (toward the newest joiner);    0x80 = nil.  by the ROM.
//   $0F81,X       OAM palette attribute = (slot<<1)&0x0E. NEVER cleared on release -- it is the
//                 slot-recovery record the release path reads back (C0/7361, C0/7373).
//   $1400,X       16-bit share key = paletteId*24 (loadGfxMirror already writes hdr[2]*24).
//   $0154 bit7    "the script owns the palette table" -- aborts BOTH allocate and release
//                 (C0/7170, C0/734C). Cleared wholesale by C0/0BF2 STZ on every location entry;
//                 set by field VM op 0x33 (C0/3711 LDA #80 / TSB $54), which the port does not
//                 implement -- so it reads 0, as measured. We read the byte the ROM reads, so the
//                 gate is faithful whether or not we ever set it.
//
// THE DISPATCH IS A LITERAL 12-ENTRY TABLE, $7181..$7198, byte-verified:
//   99 71 | 12 72 | 17 72 | 1C 72 | 1C 72 | B4 72 | B4 72 | 1C 72 | 1C 72 | 1C 72 | 1C 72 | 1C 72
// The three distinct behaviours make a range test arithmetically equal for classes 0..8, which is
// why the old stand-in looked right -- but the ROM has a table and it covers 9/10/11, so we have a
// table. There is no bounds check in the ROM (C0/717B ASL/TAX/JSR); class >= 12 would dispatch
// through the word at $7199. Our generator only ever emits {0..7, 0x80}, so that is a TRIPWIRE
// below, never a silent clamp -- clamping in either direction would be invention.
static const struct { int kind, a, b; } kPalDispatch[12] = {   // kind 0 = fixed slot a; 1 = scan [a,b)
  { 0, 5, 0 },  { 0, 6, 0 },  { 0, 7, 0 },              // 0,1,2 -> C0/7199 / 7212 / 7217
  { 1, 0, 5 },  { 1, 0, 5 },                            // 3,4   -> C0/721C  LDX #00 / CPX #05
  { 1, 1, 4 },  { 1, 1, 4 },                            // 5,6   -> C0/72B4  LDX #01 / CPX #04
  { 1, 0, 5 },  { 1, 0, 5 },                            // 7,8   -> C0/721C
  { 1, 0, 5 },  { 1, 0, 5 },  { 1, 0, 5 },              // 9,10,11 -> C0/721C
};

// C0/7324-732C (and the two other copies at 728A / 71F2): a 16-BIT compare of $1400.
static int palShareKeyEq(Mem* m, int aX, int bX) {
  return r8(m, 0x7E1400u + (uint32_t)aX) == r8(m, 0x7E1400u + (uint32_t)bX)
      && r8(m, 0x7E1401u + (uint32_t)aX) == r8(m, 0x7E1401u + (uint32_t)bX);
}

// The 48-byte palette DATA upload every claim and share tail performs (C0/71BC / 724B / 72E3, all
// three with MVN operand bytes `54 7E E4`). BOTH MVNs COPY THE SAME 24 SOURCE BYTES: the PHX/PLX
// pair around the first one restores the source pointer, so the second block is a re-copy at
// +0x200, NOT source[24..47]. Omitting that pair is the obvious way to get this wrong.
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

// The CLAIM tail, identical in all three copies (C0/71A0-71B9 / 722F-7248 / 72C7-72E0).
static void palClaim(Mem* m, int objX, int slot) {
  w8(m, 0x7E0B00u + (uint32_t)objX, 0x80);                       // 72CD: PREV := nil
  w8(m, 0x7E0B01u + (uint32_t)objX, 0x80);                       // 72D0: NEXT := nil
  w8(m, 0x7E0B80u + (uint32_t)slot, (uint8_t)objX);              // 72D7: slot owner := me
  int attr = (slot << 1) & 0x0E;                                 // 72DA-72DC
  w8(m, 0x7E0F81u + (uint32_t)objX, (uint8_t)attr);              // 72E0
  palUpload(m, objX, attr);
}

// C0/7170 -- the gate, the dispatch, and the two passes. Returns 1 if a slot was assigned.
static int palAlloc7170(Mem* m, int obj) {
  if (r8(m, 0x7E0154) & 0x80) return 0;                          // 7170-7172: script owns the table
  int objX = obj * 2, cls = r8(m, 0x7E1100u + (uint32_t)objX);
  if (cls & 0x80) return 0;                                      // 7176-7179: class bit7
  if (cls >= 12) return 0;                                       // TRIPWIRE: the ROM would dispatch
                                                                 // through $7199; we never emit it.
  int kind = kPalDispatch[cls].kind, lo = kPalDispatch[cls].a, end = kPalDispatch[cls].b;
  if (kind == 0) {                                               // C0/7199 -- ONE fixed slot, and
    int slot = lo, own = r8(m, 0x7E0B80u + (uint32_t)slot);      //   NO other slot is ever tried
    if (own & 0x80) { palClaim(m, objX, slot); return 1; }       // 719E BPL not taken -> claim
    if (!palShareKeyEq(m, own, objX)) return 0;                  // 71F2-71FC mismatch -> 71EB CLC/RTS
    // *** PORTED VERBATIM, INCLUDING THE ROM'S OWN ANOMALY. *** C0/7210 = bytes `80 9E`, so this
    // share tail branches to C0/71B0 -- TWO BYTES PAST `71AE: LDX $C3`. X therefore still holds
    // $DB = the OCCUPANT's obj*2 from 720B, so the ROM writes $0B80 + occupant*2 (which can land
    // anywhere in $0B80..$0BFE, outside the 8-slot table) and derives the attribute from the
    // OCCUPANT INDEX rather than the slot. Do not "correct" this; do not unify the three tails.
    w8(m, 0x7E0B00u + (uint32_t)objX, (uint8_t)own);             // 7202: my PREV := occupant
    w8(m, 0x7E0B01u + (uint32_t)objX, 0x80);                     // 7207: my NEXT := nil
    w8(m, 0x7E0B01u + (uint32_t)own,  (uint8_t)objX);            // 720D: occupant's NEXT := me
    w8(m, 0x7E0B80u + (uint32_t)own,  (uint8_t)objX);            // 71B0 with X = occupant (!)
    int attr = ((own * 2) & 0x0E);                               // 71B4/71B5, from the occupant idx
    w8(m, 0x7E0F81u + (uint32_t)objX, (uint8_t)attr);            // 71B9
    palUpload(m, objX, attr);                                    // 71B7 keeps $1400[me] as source
    return 1;
  }
  for (int s = lo; s < end; s++) {                               // pass 1 SHARE (72B6 / 721E).
    int own = r8(m, 0x7E0B80u + (uint32_t)s);                    //   Does NOT skip self: a re-entry
    if (own & 0x80) continue;                                    //   self-shares, like the ROM.
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
  for (int s = lo; s < end; s++)                                 // pass 2 CLAIM (72C0-72C5 / 7228)
    if (r8(m, 0x7E0B80u + (uint32_t)s) & 0x80) { palClaim(m, objX, s); return 1; }
  return 0;                                                      // 72B2 / 734A: CLC, nothing written
}

// C0/734C -- the release. A doubly-linked unlink; four branches, three of them oracle-measured.
// $0F81,X is deliberately NOT cleared: it is how the slot is recovered here (7361 / 7373), and it
// also feeds every sprite's OAM attribute (src/oam.c), so tidying it would corrupt both.
static void palRelease734C(Mem* m, int obj) {
  if (r8(m, 0x7E0154) & 0x80) return;                            // 734C-734E: same gate as allocate
  int objX = obj * 2;
  int nxt = r8(m, 0x7E0B01u + (uint32_t)objX);                   // 7352
  int prv = r8(m, 0x7E0B00u + (uint32_t)objX);                   // 7357 / 737E
  int slot = r8(m, 0x7E0F81u + (uint32_t)objX) >> 1;             // 735E-7362 / 7370-7374: LSR
  if (nxt & 0x80) {                                              // I am the NEWEST (7355 BPL n/t)
    if (prv & 0x80) w8(m, 0x7E0B80u + (uint32_t)slot, 0x80);     // 7370-7377: sole owner -> FREE
    else { w8(m, 0x7E0B80u + (uint32_t)slot, (uint8_t)prv);      // 7365: slot := my PREV
           w8(m, 0x7E0B01u + (uint32_t)prv, 0x80); }             // 736B: PREV's NEXT := nil
  } else if (prv & 0x80) w8(m, 0x7E0B00u + (uint32_t)nxt, 0x80); // 7390-7394: I was the ORIGINAL
  else { w8(m, 0x7E0B00u + (uint32_t)nxt, (uint8_t)prv);         // 7385: middle unlink -- the one
         w8(m, 0x7E0B01u + (uint32_t)prv, (uint8_t)nxt); }       // 738B:  branch not oracle-measured
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
    static uint8_t scratch[0x10000];                   // measure the decompressed size like the ROM's
                                                       // JSL C30002 job ($0306 -> $71 += size). Only
                                                       // canonical LZ sets reach here (0x82/0x83<0xF8 ids;
                                                       // the ROM caps their heap at 32KB, C0/46CA/4833) --
                                                       // the 64KB scratch is 2x that canonical bound.
    size_t sz = ct_lz_decompress(gRomImg, scratch, src);
    // ...and the ROM's own cycles for it (C0/46BB / C0/4825, `$0305 = 7F`): a location load holds its black
    // window for every sprite sheet its object pass decompresses, not just for the map and the tilesets.
    g_ctLzSpriteCycles += ct_lz_rom_cycles(gRomImg, (size_t)gRomImgLen, (size_t)src, 1);
    cur = (cur + (uint32_t)sz) & 0xFFFFu;
    w8(m, 0x7E0071, cur & 0xFF); w8(m, 0x7E0072, (cur >> 8) & 0xFF);
  }
}

// op 0x81/0x82 NPC-loader mirror: the same header consumption over the NPC header table (the
// claims MATTER for a later monster load -- NPC palette slots 0..4 overlap the monster range
// 1..3 and op-0x82 gfx sets consume $0920 slots + heap, shifting a monster's $1280). GFX branch
// differs per op: 0x81 (C0/4476) is ALWAYS ROM-direct -- $1280/$1200 = the gfx table entry
// itself (C0/4518-4536; raw uncompressed banks D2/D3, no allocator, no $0920 claim -- oracle
// fresh loc-125 obj14 `81 02`: $1280/$1200 = D3:6B40, no slot); 0x82 (C0/4590) allocates
// (JSR C0/5C90 at C0/4626 + the $71 decompress advance, same as the monster path).
// The position WORDS a placement op stores ($1800/$1801 = X sub-pixel/tile, $1880/$1881 = Y): C0/4B74 (op 0x8B,
// X := tile*256 + $80, Y := tile*256 + $FF), C0/4C27 (op 0x8D, the script's raw words), op 0x8C (the two loc
// words). The script-init runner C0/5709 executes those ops for real at the LOAD, so by the time C0/286C's entry
// sweeps the palette slots (C0/B0E6 `LDA $1801,X / ASL`) every placed object's tile byte is in WRAM. The port's
// create-time prescan set only the host copies, and its per-frame mirror lands after the sweep -- so the sweep
// read the load fill's $FF for every placed NPC (scene 6: GATO lost his slot the moment the sweep read the byte
// the ROM reads). Written here at the prescan, once, as the handlers do.
static void vmPosWordsMirror(Mem* m, int obj, int px16, int py16) {
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  w8(m, X + 0x1800, (uint8_t)px16); w8(m, X + 0x1801, (uint8_t)(px16 >> 8));
  w8(m, X + 0x1880, (uint8_t)py16); w8(m, X + 0x1881, (uint8_t)(py16 >> 8));
}
// Op 0x8C = C0/4BC3, op 0x8B's INDIRECT twin: both operands index the scene array $7F0200 (stride 2) and the LOW
// byte of each slot is the TILE (`SEP #20 / LDA $7F0200,X`; op 0x21 stores $1801/$1881 there with an 8-bit STA,
// C0/65CE / 65E3). `STA $1801,X` / `STA $1881,X`, then `LDA #FF / STA $1880,X` -- and $1800,X is NOT written, so
// X keeps its sub-position: the X word is tile*256 + the old $1800 (the port mirrors $1800 from fx every frame;
// a fresh object's is the load fill's 00). No zero guard: tile (0,0) is placed like any other.
// The port read the slot with r16 -- which is BIG-endian here, tile << 8 -- and took >> 4 as pixels: X at the
// tile's left edge instead of its kept sub-position, and Y = tile*16 - 1/16, the foot of the tile ABOVE. Oracle,
// the attract's 07D: the Sorcerer's `8C 08 09` with var8 = 0007, var9 = 0019 and $1800 = 80 (its earlier
// `8B`) is $1800/$1880 = 0780/19FF; the port put it at 0700/18FF, 8 px left and 16 px high, and the battle
// staged it there (C0/057C reads $1801/$1881, disasm/notes/battle_playanim_list.md).
// The placement ops' OAM priority pair (C0/4B98-4BBC op 0x8B, C0/4BFC-4C20 op 0x8C, C0/4C42-4C6C op 0x8D): with $0F80,X
// bit 7 (AUTO) the new tile's property planes give it -- `LDA $7E7000,X / AND #40 / LSR / LSR / ORA #20` to $D9 and on
// to $0C01, the $7E7040 one to $0C00 (X = the tile index (y << 8) | x) -- and without it the pair is left as it was
// (`BPL` over the block). The port wrote neither, so an AUTO object placed after its loader kept the loader's 2020.
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
  // THE LOADER TAIL'S FOUR CONSTANT STORES, which every NPC arm makes and this mirror did not.
  // op 0x81 (C0/4476) falls into the shared descriptor walk whose tail is C0/453B-454F; op 0x82
  // (C0/4559) -- which op 0x83 enters two bytes earlier at C0/4557 (`LDX $6D`) -- repeats the same
  // four inline at C0/4571-458A:
  //   453B/4571  LDA #2020 / STA $0C00,X     the OAM priority pair
  //   4543/4583  LDA #00   / STA $1680,X     no anim mode
  //   4548/4588  LDA #00   / STA $1681,X     no phase
  //   454D/457E  LDA #01   / STA $1600,X     FACING = 1 = DOWN
  // The facing one is the visible half: an NPC whose script carries no FACE op stands facing DOWN,
  // and the port left $1600,X at whatever the byte held (0 = up on a fresh load). MEASURED on the
  // user's capture npc_esquerda.state (loc 5, storyline 3, the fair): the NPC beside the player,
  // obj15 at tile (23,38) -- its whole fn0 is `82 52 / 8B 17 26 / AA 01 / 00`, no FACE -- reads
  // $1600 = 01 in the ROM and read 00 here, and the anim drain C0/CAD9 picks the OAM assembly from
  // that byte, so the port drew the wrong row ("in our version he is facing left").
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  w8(m, X + 0x0C00, 0x20); w8(m, X + 0x0C01, 0x20);    // 453B / 4571 (16-bit store of #2020)
  w8(m, X + 0x1680, 0x00);                             // 4543 / 4583
  w8(m, X + 0x1681, 0x00);                             // 4548 / 4588
  w8(m, X + 0x1600, 0x01);                             // 454D / 457E
  // ...and SOLID, which the three NPC ARMS make around that shared tail rather than inside it:
  //   op 0x81 C0/447D LDA #01 / 447F STA $1B01,X      (before the descriptor walk)
  //   op 0x82 C0/459E LDA #01 / 45A0 STA $1B01,X      (after `JSR C0/4557`)
  //   op 0x83 C0/46ED LDA #01 / 46EF STA $1B01,X      (same shape)
  // $1B01,X bit0 is the CANDIDATE half of the object-solid scan (C0/5BDB `LDA $1B01,X / BIT #01 /
  // BEQ` -- skip this candidate). The port wrote the byte only for MONSTERS (loadMonMirror), so a
  // freshly loaded NPC was not a blocker at all: measured on a fresh `--scene 5`, objs 19/20/24/25/26
  // all read $1B01 = 00 where the oracle reads 01. Same family as the PC arms' three stores above.
  w8(m, X + 0x1B01, 0x01);                             // 447F / 45A0 / 46EF
  // ...and the two the arms make on the way in: `STA $1101,X` = the load op's operand (C0/448A for 0x81, C0/4564 in
  // C0/4557 for 0x82 -- the raw operand, not the record index, which is operand + 7) and, in C0/4557 alone,
  // `LDA #7F / STA $1200,X` (C0/4579): the sheet's bank, whatever C0/5C90 then finds. Op 0x81 takes its bank from the
  // record (loadGfxMirror's direct arm). Without them the port kept a previous location's bytes there: on the bell
  // stairs (1B7, from the fair) objects 29-31's $1200 read 00 / E6 / 07 where the cartridge's read 7F, and C0/CAD9's
  // tile copy C0/E687 reads that bank (measured: CT_LOADWALK_DUMP against `ct --hookpc 286C` CT_HOOK_DUMPWRAM).
  w8(m, X + 0x1101, (uint8_t)(mode == 4 ? hdrIdx - 7 : hdrIdx));
  if (mode == 4) w8(m, X + 0x1200, 0x7F);
  if (!gRomImg || gRomImgLen < 0x250000) return;       // palette slots assign on first RENDER,
  loadGfxMirror(m, obj, CtRomPtr(gRomImg, gRomImgLen, 0x24F000 + (long)hdrIdx * 5, 5), mode == 3);   // not at load
}

// The PC loader C0/448D (op 0x57/0x5C/0x62/0x6A/0x68/0x6C/0x6D via C0/41E4-4214, and op 0x80's three
// in-party arms C0/42B5 / 4329 / 43DC): the SAME descriptor walk as the NPC loader over the seven PC
// records at $E4:F000 + id*5 (C0/449B-4518 = C0/4595-4620 with the base changed; move.c's ctPcLoad
// shares ctLoaderWalk with CtObjLoad for exactly that reason), then a tail that is NOT the NPC's:
// the graphics pointer is read straight out of the 3-byte table ([$AF],Y at C0/4529 -- a PC's sheet
// is already resident, so no C0/5C90 dedup and no decompress), and C0/453B-454D store
// $0C00/$0C01 = $20, $1680 = 0, $1681 = 0, $1600 = 1. It does not write $1B01 (three NOPs at
// 4498-449A where C0/4590 has the store) -- the op-0x80 arms set that byte themselves.
//
// This mirror did not exist: the live PC arms wrote only the C-side actor (sprite/defined/mode),
// so in a FRESH load the lead's $1380/$1300/$1480/$1500/$1580 stayed ZERO (measured with
// CT_DUMPWRAM on scene 2: Crono all-zero, Mom and the cat filled by npcLoadMirror). The frame legs
// read those tables to drive a PC's $7F0B01,X, so once op 0xAB/0xB7 completed on the byte instead
// of the renderer's flag, the bedslide's `AB` parked forever in Belthasar while --vmdiff (seeded
// from the oracle's tables) went CLEAN. Same directGfx arm as C0/4845's ROM-direct path: $1280/$1200
// from the table entry, no allocator claim.
// C0/42A1-42AD (slot 0) / C0/4315-4321 (slot 1) / C0/43C8-43D4 (slot 2) -- THE THREE PARTY ARMS'
// COMMON PROLOGUE. All three are the same three stores, byte for byte, and they run BEFORE the shared
// `JSR C0/448D` tail (pcLoadMirror), so EVERY PC load makes them whichever opcode entered:
//
//   42A1 STZ  $1C80,X        object (bit1) + map (bit0) collision OFF for a party member
//   42A6 LDA #01 / STA $1A81,X   the script visibility byte
//   42AB LDA #01 / STA $1B01,X   SOLID = 1 (a PC is a blocker for other movers' C0/5B95 scan)
//
// The port had `$1C80 = 0` on the op-0x80 entry only, and as a C-field assignment (`a->coll = 0`) on
// the op-0x57/0x5C/0x62 entry -- and that C field is copied into WRAM only ONCE, at CtSceneCreate,
// BEFORE the init pass runs (the `$1C80` seed loop). So a lead loaded by op 0x57 kept the seed's
// leftover 0x03 in the byte, and $1C80 bit1 is precisely the gate C0/5B95 tests: the LEAD PC entered
// the object-solid scan and was DEFLECTED by +0x30 around any solid object in its path.
// MEASURED on the user's own F5 state (build/ct_debug.state, scene 5, the bell's `96 05 24` return
// leg at ip 0x05C6, Marle parked on the destination tile (88,591)): ours read obj1 $1C80 = 03 /
// $1B01 = 00 and swung Crono out to x = 99 and back -- "crono steps around marle on the way" -- while
// the ORACLE walks the same leg with x pinned at 88.0 for every frame of it (634.6 -> 589.9 -> 591.9)
// and holds obj1 $1C80 = 00 / $1B01 = 01. Scene 5 loads its lead with `57 00`, the entry that had
// none of the three stores.
static void pcSlotArmCommon(Mem* m, int obj, CtVmActor* a) {
  uint32_t X = 0x7E0000u + (uint32_t)obj * 2;
  w8(m, X + 0x1C80, 0x00); a->coll = 0; a->collMap = 0;   // 42A1 / 4315 / 43C8
  w8(m, X + 0x1A81, 0x01);                                // 42A6 / 431A / 43CD
  w8(m, X + 0x1B01, 0x01); a->solid = 1;                  // 42AB / 431F / 43D2
  // 42B3 `STX $97` / 4327 `STX $99` / 43DA `STX $9B` -- the party-slot table, written by the load op itself
  // and SIXTEEN-bit (X is wide from the runner's REP #10, move.c ctVmPcLoad), so the high byte is zeroed too.
  // The init pass wrote it only after every fn0 had run, so whatever read it DURING the load read the boot's
  // 0x80: a later object's `22 00` (the attract's loc 07D obj8, which spawns on the lead's tile) and the
  // follower arm's `LDA $97` (C0/435C), which then always took the no-leader arm.
  if (a->moveMode <= 2) { w8(m, 0x7E0197 + (uint32_t)a->moveMode * 2, (uint8_t)(obj * 2));
                          w8(m, 0x7E0198 + (uint32_t)a->moveMode * 2, 0); }
}
// THE TWO STORES EVERY PC LOAD MAKES BEFORE ITS PARTY TEST -- the arms here kept $1100,X and the slot table
// but not these, and move.c's ctVmPcLoad (the --sweepops-graded body) has both:
//   41E4/41EC/.../4214 `LDA $6D / STA $8D+id` (the fixed-char ops 0x57/0x5C/0x62/0x6A/0x68/0x6C/0x6D) and
//   422A-4268 the same through op 0x80's seven-way `CMP #n` chain (no default arm: an id > 6 stores nowhere)
//     -> $018D+id = THIS object's X, the char -> object table op 0xD3 reads (C0/3933 `LDX $D9 / LDA $8D,X`);
//   4295 (not in the party) / 42B0 / 4324 / 43D7 (slots 0-2) `LDA $D9 / STA $1101,X`
//     -> the object's char id, which op 0xD4 (C0/394E) compares for each party-object slot.
// Without $1101 a leave never matched a party OBJECT: at the end of the attract's 0AD (logical oracle f2286) the
// cartridge's `D4 01 / D4 04 / D3 03` compact $0197/$0199/$019B = 02 04 0A -> 02 08 80 in ONE visit, and the port
// (objects 1-8 at $1101 = 00 where the cartridge holds 00 01 02 03 04 05 06 7E) left the slots alone and spent a
// visit on each leave.
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

// C0/42B8-430F -- THE LEAD PC's ARRIVAL ARM, run by the slot-0 (in-party, mode 0) branch of the PC loader
// AFTER the generic C0/448D tables (C0/42B5 `JSR C0/448D` then falls into 42B8). It places the lead on
// the location loader's arrival bytes and, last of all, overwrites the tail's `$1600 = 1` (C0/454D)
// with the arrival FACING:
//   42B8 LDX $6D / LDA $02 / STA $1801,X / LDA $03 / STA $1881,X   the arrival TILE ($0102/$0103)
//   42C4 STX $0B89                                                    the lead's object offset
//   42C7 REP / LDA #0008 / STA $0D00,X / LDA #0000 / STA $1700,X
//   42D7 LDA $1000,X / AND #80 / ORA #01 / STA $1000,X               cadence 1, pause bit kept
//   42E1 LDA $04 / AND #03 / STA $1600,X                             FACING = $0104 bits 0-1
//   42E8 LDA $04 / BIT #08 / BNE -> $1880,X = $7F  else $FF           Y half-tile
//   42FA LDA $04 / BIT #04 / BNE -> $1800,X = $00  else $80           X half-tile
// Without this the port's lead always faced DOWN after a door: measured on predoor_f2f_s5 (Up into
// scene 439), the oracle arrives with $1600 = 00, ours read 01 -- the loader tail's value. The host
// already derives the arrival PIXEL from the same $0104 bits (field.c, the pendKind path); the facing
// and the WRAM mirrors are the VM's, so the actor and the image agree with the ROM's.
// The five stores the loader's DEFAULT PLACEMENT makes from the location's arrival bytes. They appear
// TWICE in the ROM, byte for byte: in the lead's arm (C0/42BA/42C1/42E1/42E8/42FA) and in the follower
// arm's no-leader branch (C0/438A/438F/4394/43A1-43AA/43B3-43BD). $0102/$0103 are the arrival TILE
// (the high bytes of the 12.4 words), $0104 bits 0-1 the FACING, bit 3 the Y half-tile and bit 2 the X.
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
// C0/4356-4386 -- THE FOLLOWER ARM'S TAIL of the PC loader, run by the slot-1 (C0/4310) and slot-2
// (C0/43C3) branches after the generic C0/448D tables, exactly where the lead's branch runs its arrival
// arm (pcArrivalArm above). Two things, and the port had neither on the live path:
//   4356 STZ $AB / 4358 STZ $AC / 435A STZ $AD      the THREE party ring cursors ($01AB head, $01AC and
//                                                   $01AD the two follower reads) -- the velocity ring
//                                                   restarts empty on every PC load
//   435C LDA $97 / 435E BMI ->438A                  no leader ($0197 bit7) -> the default placement arm
//   4360 REP #20 / AND #00FF / TAX / 4366 LDA $1800,X / 4369 LDX $99 / 436B STA $1800,X
//   436E LDX $97 / 4370 LDA $1880,X / 4373 LDX $99 / 4375 STA $1880,X
//   4378 STZ $1900,X                                 (16-bit: $1900 AND $1901)
//   437D LDX $97 / 437F LDA $1600,X / 4382 LDX $99 / 4384 STA $1600,X    the LEAD'S FACING, 8-bit
// i.e. a follower is loaded ON the leader's tile, facing the way the leader faces, with no velocity and
// with the trail empty. MEASURED across scene 5's north door into 439 (saves/predoor_f2f_s5.state,
// --btnscript "10:60,0:90", --monitor "7E0100:2,7E01AB:3,7E1802:2,7E1882:2,7E1602:1,7E1806:2,7E1886:2,
// 7E1606:1,7E1106:1"): at f7 the location flips to $01B7 with the ring still 1A/09/00 from scene 5, and
// by f35 the ring reads 00/00/00 with obj3 (Marle) at (392,335.94) facing 00 -- the lead's own tile and
// facing. The port used to carry the old cursors across the door: the follower spent scene 5's DOWN
// velocities in scene 439 and walked 15 px south of the lead, facing down ("the npcs start the scene
// facing the wrong way" / "marle moves badly when following crono"). --followtest never saw it
// because it drives one scene and never loads a location.
// The 16-bit `LDX $97` reads at 436E/437D are transcribed as the byte $0197 for the same reason the
// lockstep port gives (move.c, ctPcLoadSlot): $0198 is 0 in every state the engine can produce.
// ...and BEFORE that tail, between C0/448D's tables and 4356, the slot's own block -- the two slots differ only in
// their constants (move.c ctVmPcLoad's PS[] table is the --sweepops-graded copy):
//   432C LDX $6D / LDA $78 / BEQ / LDA #02 / STA $78 / STZ $1601,X          (43DF, the same)
//   4339 STX $0B8A                        slot 2: 43EC STX $0B8B -- SIXTEEN-bit, the next byte is zeroed too
//   433C REP / LDA #0020 / STA $0D00,X / LDA #0020 / STA $1700,X            slot 2: 0028 / 0040
//   434C LDA $1000,X / AND #80 / ORA #01 / STA $1000,X                      the period 1, the pause bit kept
// The port kept only the lead's copy of these (pcArrivalArm). Oracle, the attract's loc 112 (rule, rule_s4440 +156,
// PEEK_WRITEWATCH / --monitor): obj3 (slot 2) $0D00 = 0028, $1700 = 0040 and obj4 (slot 1) 0020 / 0020 (C0:4344 /
// C0:434A), $0B8B = 06 (C0:43EF) then $0B8A = 08 / $0B8B = 00 (C0:433C), and both periods 01 -- the port's read 04.
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
  if (lead & 0x80) {                                                   // 435E BMI ->438A: NO LEADER YET --
    // the party table $0197/$0199/$019B is refilled as each PC's own load op runs (42B3 / 4327 / 43DA,
    // pcSlotArmCommon), and the init runner walks the objects upwards from 0 (C0/58EA `LDX #0000`), so a
    // follower takes this arm only when its object comes BEFORE the lead's. It places her on the location's
    // own arrival tile and facing -- the same bytes the lead will use. (Scene 439's Marle is obj3, after the
    // lead obj1: oracle predoor_f2f_s5, `$0197 = 02` by C0:42B3 and `$0199 = 06` by C0:4327 in the same load
    // frame, so she takes the lead copy below. This note used to say otherwise because the port wrote the
    // table only after the whole init pass; the position is the same either way, the arrival tile.)
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
// THE LEAD's ARM INSIDE THE INIT PASS, and what it means for ORDER. C0/42B8 stores the arrival bytes into
// $1800/$1880 at the moment the load op runs; whatever the SAME fn0 does afterwards (`8B`/`8D` place)
// writes the same two words again, and the last writer is the position the location starts with. The
// host carries the arrival itself (field.c derives the pixel from these $0102-$0104 bytes and places the
// lead after CtSceneCreate), so here the arm's placement is recorded as "the arrival's", i.e. NOT placed
// by the script: any earlier `8B` is overwritten, exactly as 42BA/42C1 overwrite it, and a later one sets
// hasPos again. field.c then reads the script's placement back instead of pushing the arrival over it.
// MEASURED (saves/attract_cena_inicial.state, PEEK_SAVE at f1080, --monitor "7E0197:1,7E1804:2,
// 7E1884:2"): loc 01B loads at f38 with C0:42B5 $0197 = 04 and then C0:4B86/4B98 (op 0x8B) obj2 =
// 0880/17FF -- tile (8,23), not the arrival tile (8,8) -- and Crono walks in from the bottom of the
// cathedral. The port pushed the arrival pixel (136,144) over it and Crono started under the stained glass.
// C0/3CD0 -- op 0xF0's TIMED arm (operand low nibble != 0), the stores the port never made:
//   3CDD STA $1C / 3CDF STA $1D     rate AND countdown = the low nibble
//   3CE1..3CE9 LSR x4 / STA $1B     target brightness = the high nibble
//   3CEB LDA #02 / TSB $18          arm the ramp -- the bit C0/0C76 tests and op 0xF2 waits on
// (The immediate arm, low nibble 0, is `STA $19` alone and touches none of these.)
static void vmOpF0Arm(Mem* m, uint8_t v) {
  w8(m, 0x7E011C, v & 0x0F); w8(m, 0x7E011D, v & 0x0F);
  w8(m, 0x7E011B, (v >> 4) & 0x0F);
  w8(m, 0x7E0118, r8(m, 0x7E0118) | 0x02);
}
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
  w8(m, X + 0x1600, 0x01);                            // 457E -- op 0x83 enters that arm at C0/4557
  w8(m, X + 0x1680, 0x00);                            // 4583
  w8(m, X + 0x1681, 0x00);                            // 4588
  w8(m, X + 0x0D00, (uint8_t)(((slot & 0xFE) << 4) | ((slot & 1) << 3)));
  if (gRomImg && gRomImgLen >= 0x250000)
    loadGfxMirror(m, obj, CtRomPtr(gRomImg, gRomImgLen, 0x24F600 + (long)id * 10, 10), id >= 0xF8);   // C0/477A: F8+ ROM-direct
}

// op 0xA8 face toward object[op/2] (ROM C0/55A0) / 0xA9 face toward PC[op]'s object (C0/5605):
// angle self->target via ABA2, facing = F700[angle]. PC0 = obj 1.
#define FACE_TARGET(op_, b1) ((op_) == 0xA8 ? (b1) / 2 : (b1) + 1)

// Run object `obj`'s function at `start` (init), updating actor states (port of run_func,
// follow=False: req/calls are no-ops, textbox ignored). Stops at Return / a forever-yield.
// ---- THE FOUR STAGING SOUND OPS, shared by the runtime step AND the scene-load INIT pass -------
// These lived only in the runtime step. runFunc -- the init pass that executes every object's fn0 at
// scene load -- walked them by opLen and dropped the effect, so ANY scene that stages sound from fn0
// was silent. That is the missing Leene bell: loc 271's obj0 fn0 stages `E8 3F` + two `EB` + `EA 12`
// and none of it reached the mailbox. Scene 2's sounds live in runtime fns (obj8/obj10 fn1), which is
// why they always worked and hid the gap. One implementation, both callers -- not a second copy.
//
// $ED/$EE are deliberately NOT here. They are the two WAIT ops: their ROM handlers do not advance the
// IP while the condition is unmet, so they re-execute on the next visit -- and the init pass has no
// visit to yield. Leaving them walked-over keeps runFunc's control flow byte-identical to before.
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
    case 0xE8:                                     // C0/3C11 -- play SFX
      m[0x1E01] = B[addr + 1]; m[0x1E02] = 0x80; m[0x1E00] = 0x18;
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);                   // $1E03 deliberately untouched: it is STICKY
      break;
    case 0xEA:                                     // C0/3C2F -- play SONG (+ the $29AE shadow)
      m[0x29AE] = B[addr + 1];
      m[0x1E01] = B[addr + 1]; m[0x1E00] = 0x10;
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);
      break;
    case 0xEB:                                     // C0/3C4C -- THREE raw commands back to back
      m[0x1E01] = B[addr + 1]; m[0x1E02] = B[addr + 2]; m[0x1E03] = 0xFF; m[0x1E00] = 0x81;
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);
      m[0x1E01] = 0x00; m[0x1E02] = 0xFF; m[0x1E00] = 0x82;
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);
      m[0x1E01] = 0x00; m[0x1E02] = 0xFF; m[0x1E00] = 0x83;
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);
      break;
    case 0xEC:                                     // C0/3C98 -- raw escape, any command byte
      m[0x1E00] = B[addr + 1]; m[0x1E01] = B[addr + 2]; m[0x1E02] = B[addr + 3];
      seen |= vmSoundPostAcc(mem, blen, &ln, &fr);
      break;
  }
  return seen ? (fr * 1024 + ln) : -1;             // -1: no priced sink (ct.exe without audio, CT_NOAUDIO)
}

// The renderer's view of an object's scripted animation, DERIVED from the ROM bytes the anim family
// writes ($1780/$1781/$1680/$1681/$1301,X and $7F0B01,X) -- ONE derivation, used by the WRAM seed and
// after every anim opcode the live VM runs, so the C fields can never disagree with the image. The
// mode is $1780,X alone, exactly as the ROM's consumers branch: C0/CB3A picks $1781 when it is 2,
// else $1680; C0/CBF2 short-circuits mode 3 to $1301 (anim_drain_cad9.md 4). $1680 = $FF is the
// 'no anim' marker C0/2F87 writes, surfaced as -1. animBusy is kept only for readers that still
// look at it: the VM's own completion test is the byte, in CtVmAnimFamily (C0/2EA2).
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
// op 0x8E (C0/4C74, jump table C0/5D6E[0x8E]): the sprite-priority byte. `4C7C STA $0F80,X` stores the
// operand, then BIT #80 picks the arm. AUTO (bit7 set, C0/4C83-4CB6): X = ($1881,X << 8) | $1801,X = the
// FOOT tile's index into the property planes; $0C01,X := (($7E7000,X & #40) >> 2) | #20 (via $D9, 4CA8) and
// $0C00,X := (($7E7040,X & #40) >> 2) | #20 (4CA3). FIXED (C0/4CB7-4CD4): $0C01,X := op & #30 (4CBB);
// $0C00,X := (op << 4) & #30 (4CC6). Both arms end `$0F01,X := #FF / STZ $1601,X` (4CAB-4CB0 / 4CC9-4CCE).
// $0F80's bits 2-3 pick the OAM write group (C0/B334), $0C00/$0C01 the two halves' OBJ priority (C0/CE84/
// CE8F); src/oam.c reads exactly those bytes, and until this helper existed nothing in the port ever wrote
// them after the loader -- op 0x8E only set the C mirror `sprPrio`, so a script's `8E 33` reveal (the
// Manoria flames flicker in from OBJ prio 0 / the back group) reached the renderer but never the RAM.
static void vmOp8E(Mem* m, CtVmActor* a, int obj, uint8_t v) {
  uint32_t X = (uint32_t)obj * 2;
  a->sprPrio = v;                                                 // the C read of the same byte
  w8(m, 0x7E0F80 + X, v);                                          // 4C7C
  uint8_t c00, c01;
  if (v & 0x80) {                                                  // AUTO: from the foot tile's props
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

// THE SHARED BODIES WITH NO C MIRROR, one dispatch for both runners of this file: step() and the init pass runFunc.
// Each is move.c's graded body (CtFieldRunnerOne calls the same function; --sweepops micro-executes it against the
// ROM). Before 2026-10-08 these ops had no arm here -- 0x1C/0x20/0x23/0x27/0x28/0x29/0x2F/0x30/0x31/0x33 -- or
// paraphrases (0x19/0x1A through a C field, 0x24 through the C facing, 0x2A/0x2B/0x2C/0x32 with no effect, 0x7F, the
// pad branches, 0xE6/0xF4), and every missing one was stepped over by its length: the class CT_VM_UNHANDLED cannot
// see (disasm/notes/live_vm_op_gaps.md). The ROM's init runner C0/58ED dispatches through the same table C0/5D6E as
// the event runner (5910 `JSR ($5D6E,X)`) and ignores the carry, so runFunc takes the same bodies. `B` is the body,
// `hdr` its header length (the ROM's IP is body offset + header). Returns 1 if the op was one of them (*h = IP, carry).
static int vmSharedOp(Mem* m, const uint8_t* B, int hdr, int addr, int obj, CtHRet* h) {
  uint8_t* r = m->s; const uint8_t* S = B - hdr; int ip = addr + hdr, X = obj * 2, n = (int)gRomImgLen;
  return CtVmResultFamily(r, gRomImg, n, S, ip, X, h) || CtVmObjQueryFamily(r, gRomImg, n, S, ip, X, h)
      || CtVmObjBranchFamily(r, gRomImg, n, S, ip, X, h) || CtVmPadBranchFamily(r, gRomImg, n, S, ip, X, h)
      || CtVmCaptionFamily(r, gRomImg, n, S, ip, X, h) || CtVmPalOp33(r, gRomImg, n, S, ip, X, h)
      || CtVmRandOp7F(r, gRomImg, n, S, ip, X, h) || CtVmScrollFamily(r, gRomImg, n, S, ip, X, h);
}
// Two shared bodies that DO have C mirrors here, so each runner calls these instead of vmSharedOp.
// 0x89/0x8A (C0/4B49 / 4B58): move.c's CtVmSpeedFamily writes $1A00,X; this runner's mover reads the C copy (mvComp),
// which used to be the ONLY copy written -- the WRAM byte kept the load's 10 (C0/57FE) and a seed from it (an F5
// state, CtSceneSeedFromWram) put every scripted speed back to the default. `mspd` may be NULL (the init pass).
static void vmSpeedOp(Mem* m, CtVmActor* a, int* mspd, const uint8_t* B, int hdr, int addr, int obj) {
  CtHRet h; CtVmSpeedFamily(m->s, gRomImg, (int)gRomImgLen, B - hdr, addr + hdr, obj * 2, &h);
  a->moveSpeed = m->s[0x1A00 + obj * 2];
  if (mspd) *mspd = a->moveSpeed;
}
// 0x0A/0x0B/0x0C (C0/6254 / 6269 / 6282): move.c's CtVmObjEnableFamily writes the target's $1100/$1A81 (kill) or its
// $1000 bit 7 (the cadence pause the queue build C0/59EC and the activate guard C0/5AD1 read); the C fields follow.
// The carry is the ROM's: 0x0B on ITSELF ends the visit (627A CPX $6D / 627E CLC), and the init pass ran only the C
// half -- fn0's `0B <self>` (245 static sites) never set the byte C0/5AD1 tests. `mode` may be NULL (the init pass).
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
// THE OBJECT PASS'S CPU TIME. While a location load runs its scripts' init (C0/5709) and obj0's slot 1 (C0/595C), every op
// they dispatch is priced as the ROM runs it -- the runner's dispatch (CT_VM_DISPATCH) and the handler's arm on the WRAM
// it is about to read (field_cost.c ct_vm_op_cycles) -- into g_ctLzSpriteCycles, the load's object-pass clock, next to
// the sprite sheets the loaders decompress. gOpCostWhy names the first op priced on a path the walk does not decide.
// The pricing is the host's choice (CtSceneLoadOpsPriced), not the sound sink's: the ops are CPU work whether or not an SPC
// listens -- tied to the sink, a --loadwram run without the state's SPC priced none and loc 120's load came a frame short.
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
    if (op == 0x00 || op == 0xAF || op == 0xB0 || op == 0xB1 || op == 0xB2) {
      if (stopAddr) { *stopAddr = addr; *stopOp = op; }
      break;
    }
    // The bodies step() runs too (vmSharedOp, vmObjEnableOp, vmSpeedOp, and the item / party / tile-copy families):
    // C0/58ED's loop dispatches every op through the event table and ignores the carry (5910 JSR / 5913 BRA), so an op
    // with no arm here was stepped over by its length -- fn0's party edits (loc 0E2's `D4 03 / D2 01 05 / D4 01 / D3 01
    // / D3 02`), item tests (the tent's `C9`), `B8`, `7F`, `E4`, `20`, `29`, `33` among them (live_vm_op_gaps.md).
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
    // The four STAGING sound ops. The init pass reaches them because loc 271's obj0 fn0 -- the
    // wake-up scene's bell -- is exactly that: sound staged from an INIT function. Effect only; `nxt`
    // is untouched, so the walk continues at the opLen the table already had ($E8=2,$EA=2,$EB=3,$EC=4).
    else if (op == 0xE8 || op == 0xEA || op == 0xEB || op == 0xEC) (void)vmSoundStage(mem, blen, B, addr, op, 12);   // init pass: the cost is not consumed here
    else if (op == 0x10) nxt = addr + B[addr + 1] + 1;
    else if (op == 0x11) nxt = addr - B[addr + 1] + 1;
    else if (op == 0x12) { if (!cmp_(r8(mem, LOC + B[addr + 1] * 2), B[addr + 2], B[addr + 3])) nxt = addr + B[addr + 4] + 4; }
    else if (op == 0x13) { if (!cmp_(r16(mem, LOC + B[addr + 1] * 2), B[addr + 2] | (B[addr + 3] << 8), B[addr + 4])) nxt = addr + B[addr + 5] + 5; }
    else if (op == 0x14) { if (!cmp_(r8(mem, LOC + B[addr + 1] * 2), r8(mem, LOC + B[addr + 2] * 2), B[addr + 3])) nxt = addr + B[addr + 4] + 4; }
    else if (op == 0x15) { if (!cmp_(r16(mem, LOC + B[addr + 1] * 2), r16(mem, LOC + B[addr + 2] * 2), B[addr + 3])) nxt = addr + B[addr + 4] + 4; }
    // ⭐ op 0x16 = C0/63E6, DERIVED 2026-09-13 (was uncited). Five bytes,
    // `16 [idx] [val] [mode] [skip]`. The field VM's handler table is C0/5D6E, validated on five
    // handlers cited elsewhere in this file (AD->C0/2F96, CC->3827, CD->385B, CE->3874, D2->38E8).
    //   63E8 LDA $7F2001,X -> $D9   the flag INDEX          63EF -> $DB   the VALUE/mask
    //   63F6 operand 3 = the MODE; 63FA BPL picks the arm, and bit 7 set does 640A ORA #0100 --
    //        the index gains $100, so the flag comes from $7F:0100+idx instead of $7F:0000+idx.
    //   6433 JSR ($6467,X) with X = mode*2: EIGHT comparators, each CLC = holds / SEC = jump --
    //        6487 ==, 648F !=, 6497 >, 64A1 <, 64A9 >=, 64B5 <=, 64BF AND, 64C7 OR (C0/64B3 = CLC).
    //   6349 is the JUMP arm and adds the skip counted FROM THE SKIP BYTE, which is why the target
    //        below is addr+4+skip and not addr+5+skip.
    else if (op == 0x16) { int li = B[addr + 1]; if (B[addr + 3] & 0x80) li += 0x100; if (!cmp_(r8(mem, GLB + li), B[addr + 2], B[addr + 3] & 0x7F)) nxt = addr + B[addr + 4] + 4; }
    else if (op == 0x18) { if (!(r8(mem, GLB) < B[addr + 1])) nxt = addr + B[addr + 2] + 2; }
    // 0x19/0x1C/0x1A (C0/653B / 654E / 6568): vmSharedOp above -- the RESULT word $7F0A80,X and the CASE on its low byte.
    // The pass had the branch and neither load once, so every inn's wake-up beds (loc 00C's PCs: `48 0n 11 7E 0A /
    // 19 0A / 1A 00 .. / 1A 01 ..`) put the whole party in slot 0's bed.
    else if (op == 0xD2) {                               // PARTY-MEMBERSHIP conditional in INIT (ROM C0/38E8
      int pc = B[addr + 1], present = 0;                 // -> C2/8C36 mode 0): scan the 3 active-party char-ids;
      for (int k = 0; k < 3; k++) if (r8(mem, 0x7E2980 + k) == pc) { present = 1; break; }
      if (!present) nxt = addr + 2 + B[addr + 2];        // ABSENT -> take the jump, PRESENT falls through (addr+3).
    }                                                    // Without it Marle's fair-meeting init (obj3 @439, 'D2 01
                                                         // 04') fell into the in-party branch (op 0x5C load-follower
                                                         // + jump-to-return) and skipped her NPC place (8B)/show (91).
    else if (op == 0x4F) w8(mem, LOC + B[addr + 2] * 2, B[addr + 1]);
    // The local INC/DEC/SET 0x71-0x77 (C0/6C69..6CF0) through move.c's CtVmLocalIncFamily, as step() does.
    else if (op == 0x71 || op == 0x72 || op == 0x73 || op == 0x75 || op == 0x76 || op == 0x77) {
      int ni = addr + hdr; CtVmLocalIncFamily(mem->s, B - hdr, addr + hdr, &ni);
    }
    // The whole variable grid, 0x50-0x56/0x58/0x59 (C0/68C6..6A11, local words little-endian), through
    // the body --sweepops grades (move.c CtVmVarFamily) -- the same call step() makes. The init pass had 0x55 alone
    // and no 0x53/0x58: the fair's obj0 fn0 `53 52 00 1B` never loaded the Silver Points into its tally local, so
    // every entry started the count at 0 and the tally's `58 1B 52 00` wrote that back over $7F0052. (0x55 was once
    // written the other way round here -- entering the forest, loc 113's `55 10`, zeroed the storyline.)
    else if ((op >= 0x50 && op <= 0x56) || op == 0x58 || op == 0x59) {   // the whole grid (0x57 is a PC loader)
      int ni = addr + hdr;
      CtVmVarFamily(mem->s, B - hdr, addr + hdr, &ni);    // lengths = opLen's; nxt stays
    }
    else if (op == 0x5A) w8(mem, GLB, B[addr + 1]);
    // The pointer read 0x48/0x49 and the local arithmetic (C0/6792..67C7, C0/6A1F..6C68): the bodies --sweepops grades,
    // the same calls step() makes. The init pass had neither 0x49 nor 0x5E/0x60/0x61/0x6B/0x6F, so a location's obj0
    // fn0 `49 05 01 7E 0B` / `13 0B F0 01 ..` (where did the party come from) always compared a zero.
    else if (op == 0x48 || op == 0x49) { int ni = addr + hdr; CtVmReadPtrFamily(mem->s, B - hdr, addr + hdr, &ni); }
    // ...and the POKE FAMILY 0x4A-0x4D (C0/67D7..6829), which this pass walked over by its length: loc 01B's obj0 fn0
    // opens with `4A 01 04 7F 00` x6 -- FD/DE98's six step tables, slot 0 zeroed (the attract f1118, writer C0:67E0,
    // the loader's own frame) -- so its stained glass stood still on the cartridge and animated here.
    else if (op >= 0x4A && op <= 0x4D) { int ni = addr + hdr; CtVmPokeFamily(mem->s, B - hdr, addr + hdr, &ni); }
    else if (op == 0x5B || op == 0x5E || op == 0x5F || op == 0x60 || op == 0x61 || op == 0x63 || op == 0x64 ||
             op == 0x67 || op == 0x69 || op == 0x6B || op == 0x6F) {
      int ni = addr + hdr;
      CtVmLocalArithFamily(mem->s, gRomImg, (int)gRomImgLen, B - hdr, addr + hdr, &ni);
    }
    else if (op == 0x65) { int li = B[addr + 2], bit = B[addr + 1]; if (bit & 0x80) li += 0x100; w8(mem, GLB + li, r8(mem, GLB + li) | (1 << (bit & 7))); }
    else if (op == 0x66) { int li = B[addr + 2], bit = B[addr + 1]; if (bit & 0x80) li += 0x100; w8(mem, GLB + li, r8(mem, GLB + li) & ~(1 << (bit & 7))); }
    else if ((ps = pcSprite(op)) >= 0) { a->sprite = ps; a->defined = 1; a->solid = 1;
      // $1100,X via the ROM PC slot-match C0/426A: char-id ps vs the active-party roster
      // $7E2980/81/82 -> slot0=0 (LEAD PC, AAFD smooth), slot1=1, slot2=2, not-in-party=0x80.
      a->moveMode = (ps == r8(mem, 0x7E2980)) ? 0 : (ps == r8(mem, 0x7E2981)) ? 1
                  : (ps == r8(mem, 0x7E2982)) ? 2 : 0x80;
      // $1100,X IS THE LOAD OP'S OWN STORE, the first thing each arm of C0/426A does -- 429E (slot 0) / 4312 (1) /
      // 43C5 (2) `LDX $6D / STA $1100,X`, 4290 the not-in-party 80 -- so the object's own fn0 reads it back at once.
      // The pass wrote it only after every fn0 had run (the $0197 slot table's old defect, pcSlotArmCommon): the
      // inns' wake-up beds are chosen by `5C / 48 04 11 7E 0A / 19 0A / 1A 01 ..` (loc 00C's obj2), and Marle read
      // the fill's byte, took slot 0's bed and woke on Crono's tile -- the cartridge puts her in bed 2 (x 904).
      w8(mem, 0x7E1100 + (uint32_t)obj * 2, (uint8_t)a->moveMode);
      pcLoaderIdStores(mem, obj, ps);                          // 41E4.. STA $8D+id / 4295-43D7 STA $1101,X
      // The LEAD PC's $1C80 = 0x00 (oracle scene_439_marle: obj1 $1C82=00, every NPC=03). The leftover
      // 0x03 default (cat/Mom) is NOT cleared by the place code, so without this the lead keeps map+obj
      // collision and a SCRIPTED lead-PC move (op 0x96, a cutscene) bounces off the $7E7040 MOVER-solid
      // plane it should never test (the lead uses the player plane / smooth AAFD). That stalled Crono on
      // the scene-439 candy cutscene's `96 1C 06` (MOVETO 28,6 -> a mover-solid tile) forever, hanging
      // obj14's CALL+WAIT = the "the two's movement freezes" freeze. It is the WRAM byte that gates it
      // (C0/5B95 reads $1C80,X), and the arms clear it for slots 1 and 2 as well as 0 -- the old note
      // here that "followers keep 0x03" was wrong: C0/4315 and C0/43C8 STZ it too, and the oracle
      // reads obj2 $1C80 = 00. pcSlotArmCommon is those three stores.
      // Only an IN-PARTY arm shows the object and brings it back from a kill: the not-in-party arm C0/428C stores
      // $1100 = 80 -- the kill's own byte -- and $1101, and never $1A81 (42A6/431A/43CD are the in-party arms'). The
      // port showed and revived it on every loader, so scene 275's obj6 (killed by obj0's `0A 0C`, then `80 05` with
      // Robo out of the party, then `81 05`) came back visible where the cartridge's $1A81 stays 00.
      if (a->moveMode != 0x80) { a->moveAnim = 1; a->draw = 0; a->dead = 0; pcSlotArmCommon(mem, obj, a); }
      if (a->moveMode != 0x80) { pcLoadMirror(mem, obj, ps);
        if (a->moveMode == 0) pcLeadInitArm(mem, obj, a); else pcFollowerArm(mem, obj, a); } }   // a PC's $1000 cadence = 1 (oracle), not the NPC default 4; + C0/448D's tables, then 42B8 (lead) / 4356 (follower)
    else if (op == 0x80) { uint8_t id = B[addr + 1]; a->sprite = id; a->defined = 1; a->solid = 1;
      // op 0x80 (C0/421E) is the PARTY-AWARE PC loader: operand = char-id, routed via the shared loader
      // C0/426A which CMPs $D9(char-id) vs $7E2980/81/82 -> mode 0 (LEAD, C0/429C STZ $1C80), 1, 2, else 0x80.
      // Same slot-match as op 0x57/5C/62 (pcSprite); op 0x57 just uses the implicit $6D char-id. Scenes whose
      // lead self-loads via `80 00` (scene 7) need this -- conflating it with op 0x81 forced mode 3 (NPC) so
      // the lead never appeared and a carried follower (`80 01`) was never instantiated.
      a->moveMode = (id == r8(mem, 0x7E2980)) ? 0 : (id == r8(mem, 0x7E2981)) ? 1
                  : (id == r8(mem, 0x7E2982)) ? 2 : 0x80;
      w8(mem, 0x7E1100 + (uint32_t)obj * 2, (uint8_t)a->moveMode);   // C0/426A's arms, as above (4290/429E/4312/43C5)
      pcLoaderIdStores(mem, obj, id);                          // 422A-4268 STA $8D+id / 4295-43D7 STA $1101,X
      if (a->moveMode != 0x80) { a->draw = 0; a->dead = 0; pcSlotArmCommon(mem, obj, a); }   // the three arms' prologue, all slots
      if (a->moveMode != 0x80) { a->moveAnim = 1; pcLoadMirror(mem, obj, id);
        if (a->moveMode == 0) pcLeadInitArm(mem, obj, a); else pcFollowerArm(mem, obj, a); } }   // PC $1000 cadence = 1; + C0/448D's tables (42B5/4329/43DC), then 42B8 (lead) / 4356 (follower)
    // ⚠ THE NPC/MONSTER LOADERS DO NOT TOUCH THE VISIBILITY BYTE. C0/4476 (0x81), C0/4559+C0/4590
    // (0x82) and C0/46DF (0x83) write $1100/$1101/$1200/$1280/$1300/$1380/$1400/$1580/$1600/$1680/
    // $1681/$1B01/$0C00 and NOTHING else -- there is no `STA $1A81,X` anywhere in them (the only five
    // indexed writers of $1A81 in the whole ROM are C0/42A6, C0/431A, C0/43CD -- the three IN-PARTY
    // arms of the PC loader -- plus C0/4CD9, the 0x90/0x91/0x7E/0x7C/0x7D tail, and C0/6264, op 0x0A).
    // `a->draw = 0` here was an invented show, and it UNDID a preceding op-0x91: a startup function
    // shaped `91 HIDE / 8B SETCOORD / 82 LOADSPRITE` (scene 113 obj11, the Truce Canyon gate; also
    // 227/11, 272/8+9, 404/9, 419-423/8, 428/7) came back VISIBLE and the closed portal stayed on
    // screen. Oracle at loc 113: obj11 $1A81=00 with the loader having run ($1100=04, $1101=97).
    // ⚠ BUT THEY DO REWRITE $1100, and that is the whole of what op 0x0A's "kill" was: C0/6254 stores $1100 = 80 and
    // $1A81 = 00, nothing else. A load op after a kill therefore brings the object back to the queue (hidden, since
    // $1A81 stays 0). `dead` is the port's name for that $1100 bit 7, so every load op clears it -- it used to stay
    // set for good. Oracle, the attract's loc 112 (rule, from build/_divergence/audit_0928/fr/rule_s4440.state):
    // obj0's fn0 kills obj10/obj12/obj13 (`0A 14 / 0A 18 / 0A 1A`), obj10's and obj12's own fn0 then run `83 88 ..`,
    // so $1100 reads 05 88 / 05 88 / 80 00 once the pass is done, and the first build reloads obj10's and obj12's
    // counts (04 04) and every build after -- their patrol loops (`89 30 / 96 07 0E / 96 07 16 / 11 07`) run unseen.
    // The port had them dead, never scheduled; obj13, never reloaded, stays at 80.
    else if (op == 0x81) { a->sprite = B[addr + 1]; a->defined = 1; a->solid = 1; a->moveMode = 3; a->dead = 0;   // op 0x81 C0/4476: unconditional $1100=3 (always-NPC loader)
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
      a->px16 = B[addr + 1] * 256 + 0x80; a->py16 = B[addr + 2] * 256 + 0xFF;          // C0/4B7C-4B95, literally
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
    // op 0x84, C0/4867: `INY / TYX / LDA $7F2001,X / LDX $6D / STA $1B01,X` -- the WHOLE operand byte
    // into the WRAM solidity, and like op 0x0D's $1C80 this is where the byte really gets set, because
    // nearly every `84` lives in fn0. The port wrote only the C mirror, so the load arms' `$1B01 = 1`
    // stood and scene 5's fair RACERS (obj 8/9, `84 00` = walk-through) stayed SOLID in the byte the
    // object-solid scan reads (C0/5BDB). Oracle, fresh scene 5: obj8/obj9 $1B01 = 00, every other NPC 01.
    else if (op == 0x84) { a->solid = B[addr + 1] & 1;
                           w8(mem, 0x7E1B01 + (uint32_t)obj * 2, B[addr + 1]); }   // C0/486F, whole byte
    // THE INIT PASS MUST WRITE THE BYTE TOO. Nearly every `0D` lives in fn0, so this handler -- not
    // the live-VM one -- is where $1C80 actually gets its value, and C0/5BA0 reads the byte.
    else if (op == 0x0D) { a->coll = (B[addr + 1] >> 1) & 1; a->collMap = B[addr + 1] & 1;
      w8(mem, 0x7E1C80 + (uint32_t)obj * 2, B[addr + 1]); }      // C0/629F, the WHOLE operand byte
    else if (op == 0x0E) { a->snap = B[addr + 1];
      w8(mem, 0x7E1C81 + (uint32_t)obj * 2, B[addr + 1]); }      // C0/62AE
    else if (op == 0x21 || op == 0x22) {               // GetObjCoords, C0/65AA / C0/65EB: an object's TILE pair
      // -> vars op2/op3. 0x21 names the object by its OFFSET; 0x22 by party slot, `LDA $97,X` = $0197 + op1
      // (DP = $0100), and BRAs into 0x21's body at 65B2: `TAX / LDA $1801,X / STA $D9 / LDA $1881,X / STA $DB`,
      // then two 8-bit stores into the slots' LOW bytes (65CE / 65E3), the X var first. The bytes are the
      // WRAM ones, which every placement arm here has already written -- the lead's C0/42B8 arrival too
      // (pcLeadInitArm), although its C record leaves the pixel to the host. This pass read that record
      // (`actors[op1 + 1].x >> 4`, 0 for the lead) and did not run op 0x21 at all. Oracle, the attract's loc 07D
      // load: obj8's fn0 `22 00 06 07 / 8C 06 07` stores the lead's tile 07/21 (C0:65E7) and places obj8 at
      // 0700/21FF (C0:4BDB / 4BFC) on the same frame.
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
    // C0/3C05 `LDA $7F2001,X / STA $1F` (D = $0100): the BYTE is the op's whole effect, here as in step(). The init pass
    // only set the host's flag and left $011F at the loader's 1 (C0/0BC4), so a fn0 `E3 00` never locked the player: the
    // host's explore-mode sync then put the lead back on the arrival tile every frame after the scene's own loop moved it
    // off the map (Last Village 182 after the Ocean Palace: obj0 fn0 `E3 00`, obj1's loop `8B FF FF` -- Crono, dead in
    // the story, stood in the hut; the rule's cartridge, saves/field_re/pre_scene_19f: $011F = 00 at f3638).
    else if (op == 0xE3) { if (explore) *explore = B[addr + 1] ? 1 : 0; w8(mem, 0x7E011F, B[addr + 1]); }
    else if (op == 0xF0) { if (bright) {                // screen fade (ROM C0/3CD0): hi nibble =
      int tg = (B[addr + 1] >> 4) & 0xF;                 // target, lo = speed; speed 0 = INSTANT
      if ((B[addr + 1] & 0x0F) == 0) { bright[0] = tg; bright[1] = tg; w8(mem, 0x7E0119, (uint8_t)tg); }   // 3CF9 STA $19
      else { bright[1] = tg; vmOpF0Arm(mem, B[addr + 1]); }   // armed: ticks toward the target (C0/1F24)
    } }
    else if (op == 0x87) {                              // SCRIPT SPEED -- the full C0/4876 (see the
      int per = (B[addr + 1] + 1) & 0xFF;               //   live stepper for the derivation)
      uint32_t A1000 = 0x7E1000u + (uint32_t)obj * 2;
      int v = (r8(mem, A1000) & 0x80) | per;
      w8(mem, A1000, v);
      w8(mem, A1000 + 1, v);                            // C0/488B -- the countdown too
      a->moveAnim = per;
    }
    // anim ops in the STARTUP function set the actor's standing anim (a sitting NPC's AC
    // shape, a fair attendant's AA loop). The init runner must apply them or the NPC stays
    // idle: its fn0 sets the anim then RETURNs, so the runtime never reaches the op again.
    else if (op == 0xAA || op == 0xAB || op == 0xAC || op == 0xAE || op == 0xB3 || op == 0xB4 || op == 0xB7) {
      // The handlers' OWN stores ($1680/$1681/$1601/$1780/$1781/$1301,X, $7F0B01,X) through the one
      // body --sweepops grades, move.c's CtVmAnimFamily; the C fields the renderer reads are then
      // derived from those bytes. The init pass keeps walking whatever the carry says (it never parks).
      CtHRet ah; ah.ip = addr; ah.carry = 1; ah.ok = 0;
      CtVmAnimFamily(mem->s, B, addr, obj * 2, &ah);
      ctAnimSyncActor(a, (const uint8_t*)mem, obj * 2);
    }
    else if (op == 0x90) a->draw = 0;
    else if (op == 0x91) a->draw = 1;
    else if (op == 0x7E) a->draw = 2;
    else if (op == 0x7C) { int t = B[addr + 1] / 2; if (t < nobj) actors[t].draw = 0; }
    else if (op == 0x7D) { int t = B[addr + 1] / 2; if (t < nobj) actors[t].draw = 1; }
    else if (op == 0x2E) {                             // C0/4A4E: the SHARED body (move.c CtVmColorSlotOp) arms the $0520
      CtHRet cs; cs.ip = nxt; cs.carry = 1; cs.ok = 0; //   record / copies the blob; this runner ignores the carry
      CtVmColorSlotOp(mem->s, B - hdr, addr + hdr, &cs);
      nxt = cs.ip - hdr; }                             // (the ROM's IP is $7F:2001-relative: body offset + header)
    else if (op == 0x88) {                             // C0/4892: the SHARED body (move.c CtVmColorFx) arms the $0520
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
  // No previous location filled the arrival bytes $0100-$0104 for this resolver, and the lead's load op
  // runs the C0/42B8 arm on them (its facing = $0104 & 3, applied AFTER the loader tail's `$1600 = 1`).
  // The convention here, as for a `--scene N` cold start: the facing the tail leaves, 1. A script that
  // sets its own facing after the load (scene 2's bed, facing up) still wins, as it does in the ROM.
  w8(&mem, 0x7E0104, 1);
  for (int n = 0; n < count; n++) {
    out[n].sprite = -1; out[n].x = 0; out[n].y = 0; out[n].px16 = -1; out[n].py16 = -1; out[n].facing = 1;
    out[n].draw = 0; out[n].dead = 0; out[n].defined = 0; out[n].hasPos = 0; out[n].result = 0;
    out[n].solid = 0; out[n].colorFx = 0; out[n].colorGated = 0;   // solid=1 only on load-sprite (below)
  }
  CtVramSinkFn sink = g_ctVramSink;                  // a probe on its own copy of the memory: its DMAs (op 0x29's font)
  g_ctVramSink = NULL;                               //   are not the machine's and must not reach the host's VRAM
  for (int n = 0; n < count; n++) {
    int slot0 = (raw[1 + n * 32] | (raw[1 + n * 32 + 1] << 8)) - header;   // ptr rebased to body
    if (slot0 >= 0 && slot0 < blen)
      runFunc(body, blen, header, n, count, slot0, &mem, out, NULL, NULL, NULL, NULL, NULL);
  }
  g_ctVramSink = sink;
  return count;
}

// ===========================================================================
// Live scene scheduler (ports engine.py tick / _step / run_tasks / _move_op).
// ===========================================================================
typedef struct { int addr; int ret[8]; int prio, dc, tb, wait;
                 int waitObj, waitPrio, nm, callAddr; } CtSS;  // addr<0 = idle; waitObj>=0 = a 0x04/0x07
                                                     // callAddr = body addr of the in-flight CALL op (for
                                                     // --vmdiff: the ROM keeps $1180 on the call; we advance)
                                                     // nm = blocked on a name-entry screen (op 0xC8)
// THE CALLER'S WAIT LATCH IS $7F0980,X -- ONE BYTE PER OBJECT, read only by op 0x04/0x07 itself (C0/6109 `LDX $6D / LDA
// $7F0980,X / BEQ ->611A`, the fresh arm, else BRL ->61A7, the poll arm). The install parks $1180 ON the call's own byte
// (61A4 `TYX / CLC`), so the poll runs only while the IP sits there. A higher-priority call that pre-empts the waiter
// (C0/6168-616D: `$7F0580,X` := $1180 = that same 04 byte) runs its function from its first op with the latch still 1;
// the RETURN lands back on the 04, which polls again. waitObj is our latch; "parked" = the IP is the one just past the
// call (the port advances past it), so a pre-empted waiter runs its new function and polls only after coming back.
// (The tent of horrors, loc 1B2: Bekkler waits in his talk fn1 on obj0's game, `04 00 33`; the game's end calls his fn3
// at priority 0, `04 16 03`. Polled every visit, his fn3 never ran and the two waited on each other for good.)
static int ssParked(const CtSS* s) { return s->waitObj >= 0 && s->callAddr >= 0 && s->addr == s->callAddr + 3; }
// What a pre-empt PUSHES into the return slot: the ROM's $1180,X (C0/6168 / 6005 / 60CC, the invokes C0/5AC5 / 3154,
// C0/5B41), which for a parked waiter is the 04/07's OWN byte, not the op after it. The RETURN then lands ON the call,
// and the op decides on the latch as it always does (C0/6109): still set -> the poll; cleared -> a fresh install. The
// latch is cleared under the waiter when the pre-empting function runs a 04/07 of its own (the poll arm on ITS operands,
// below), so returning to "past the call" -- what the port pushed before -- skipped a call the ROM makes again.
static int ssPushAddr(const CtSS* s) { return ssParked(s) ? s->callAddr : s->addr; }
                                                 // call-and-WAIT until the callee finishes
// Per-frame, transient -- outside the struct: the debug savestate writes CtScene raw, so its layout must not move.
static uint8_t g_ranB0[64];                   // op 0xB0 / 0xAF dispatched in THIS frame's pass (C0/3002 ran)
static uint8_t g_landedB0[64];                // this frame's visit ENDED on a B0 it did not dispatch (no conga)
struct CtScene {
  uint8_t* raw; int rawLen, nobj, header, blen;
  const uint8_t* body;
  Mem* mem;        // NOT owned unless ownMem; see CtSceneCreateIn
  int ownMem;
  CtVmActor actors[64];
  CtSS st[64];
  double fx[64], fy[64], mx[64], my[64];     // float position + step velocity (px/frame)
  int mspd[64];                               // $1A00 move speed (op 0x89; engine default 0x10)
  int coll[64];                               // $1C80,X bit1 = OBJECT-collision gate (op 0x0D; ROM C0/5B95):
                                              // a mover with it set is blocked by other SOLID objects
  int collMap[64];                            // $1C80,X bit0 = MAP-collision gate (op 0x0D; ROM ACFD $7E7040)
  int c1C81[64];                              // $1C81,X (op 0x0E): when != 0, op 0x96 snaps to the tile (C0/305D).
                                              // DEFAULT 0 (oracle: Mom's start + fair NPCs = 0); Mom sets it to 1
                                              // via op 0x0E for ONE aligned move, then back to 0 (her slow snap).
  int touchHit;                               // ROM $01EB: the TOUCH HIT LATCH. C0/881E resets it to 0x80
                                              // ("nothing") every frame and the C0/9923 proximity scan stores
                                              // the hit actor's index; op 0xB0's lead branch (C0/3054) consumes
                                              // it and fires C0/3154. -1 here IS the 0x80 state. Set by the host
                                              // via CtSceneSetTouchHit -- the scan itself lives on the player
                                              // side (field.c / --vmdiff), exactly as the ROM runs it in C0/881E
                                              // BEFORE the event-VM, so the VM reads a value produced earlier
                                              // in the same frame.
  int blockCtr_UNUSED;                        // (was the ROM $F8 model; it lives at $7E01F8 now -- mvRandAngle)
  int (*tileSolid)(void* ctx, int tx, int ty);// MAP solidity hook (field.c): 1 = (tx,ty) is a solid wall
  void* tileCtx;
  int cycles[64], hasTask[64];                // cycles = the $1A01 step ticket (frames left)
                                              // per-PC walk-in-progress target, checked EVERY REAL FRAME by
                                              // run_tasks (not just when the ISSUING object's own script
                                              // cadence revisits the D9 instruction) -- matches the ROM's
                                              // per-PC 0x171F walk routine blocking on $7F03FC every frame,
                                              // so a follower's mover stops the instant it enters the target
                                              // tile instead of overshooting for up to a whole cadence period.
  // field_camera_unification.md PART G: the ONE camera. The VM maintains the DP=$1D00 scroll
  // ($1D0A/$1D0E coarse + $1D93/$1D96 phase = camPx) every frame via CtSceneCameraTick (99DE gate ->
  // 91AC seek/auto -> 93E1 commit; all state in mem.s[$1D..]). These are this frame's INPUT from the
  // field host (field.c), set via CtSceneCameraInput BEFORE CtSceneTick: camReqX/Y = the post-collision
  // scroll request (1/16px signed, = 881E's $1D2E/$1D30 after 88EE+8A6D, which the ported CtPlayerControl
  // leaves in g_collRam) -- 0 when not exploring or blocked. camLeadX/Y = the LEAD's foot world position
  // (px) at the START of the frame (before the host integrates its move; the 99DE deadzone reads
  // on-screen = camLeadX - camPx from THESE, not the post-move actor CtSceneTick already sees updated).
  int camReqX, camReqY, camLeadX, camLeadY, camHaveLead;
  int camAutoWarned;                          // one-shot log guard for the dormant $01BC auto-scroll (dossier OPEN)
  uint8_t atB0[64];                           // this object's visit last ran op 0xB0 (C0/3002, where the lead's
                                              // C0/9E84 runs) -- CtSceneActorAtB0
  int mode[64];                               // $1100,X per-object MOVE-MODE (the C0/A819 dispatch key): <3 ->
                                              // AAFD smooth (lead/follower PCs), 3..6 -> AA07 $7F0B00 lifecycle
                                              // (scripted NPCs/sparkles), 7 camera, 0x80 idle. Seeded from
                                              // actors[].moveMode at create, OVERRIDDEN from WRAM in the seed.
  int b00[64];                                // $7F0B00,X mirror (SIGNED, C0/AA07 lifecycle, ROM AA1B-AA27):
                                              // ==0 -> CLAMP this frame (per-axis tile-edge stall, zero the
                                              // ticket); >0 -> clear to 0 + FREE-integrate (the step-issue
                                              // frame: armed = the ticket reload $1A01:=$1000/operand, ROM
                                              // 4FDA/5071/52B0/54E5); <0 (=0x80) -> leave + FREE (vector ops,
                                              // ROM 4EFA arms #80). Re-armed by moveOp on each step-issue.
  int vecMove[64];                            // 1 = the active move is a VECTOR move (op 0x9C/0x9D): the ROM
                                              // handler has NO C0/5614 re-anim, so the actor SLIDES with its
                                              // current (idle) pose -- the renderer must NOT play the walk cycle
                                              // (the bell minigame's Crono bob: still pose, not walking)
  int moveAngle[64];                          // op-0x96 stored HEADING: the ROM (C0/4F61 cadence 1) re-issues
                                              // the step every frame but keeps the velocity CONSTANT until an
                                              // axis reaches its target TILE, then re-aims for the remaining
                                              // axis -- it does NOT recompute the angle from the integer tile
                                              // delta every frame (that quantized stair-step desynced Crono's
                                              // DIAGONAL candy move: oracle holds 14/28 for 16f, we jittered
                                              // 0x2D->0x20->0x40). -1 = no heading yet (recompute).
  // PARTY CONGA-LINE -- a VELOCITY RING, the ROM's own (disasm/notes/party_follower_ring.md, defect #20).
  // The state lives at the ROM's addresses, not in host fields: the ring is $7F:0C00 (velX) / 0C80 (velY) /
  // 0D00 + 0D80 (the PRE-ADJUST velocity pair, which is what picks the facing) / 0E00 + 0E80 (the two OAM
  // priority flags) / 0F00 (the terrain-shape byte), 0x80 entries; the write head is $7E:01AB and the two
  // read cursors are $7E:01AC (member 2) and $7E:01AD (member 3), CHAINED -- member 3's gap is measured
  // against member 2's cursor, not against the head. Those are the three bytes the PC loader already
  // clears. Keeping them in WRAM is what lets the lockstep grade them.
  // The host fields that remain are the ones the ROM keeps elsewhere or that are pure render bookkeeping:
  int folLeadP[2], folLeadSeen;               // the lead's last-pushed 12.4 position -- the PUSH's gate is that
                                              // the lead actually MOVED, which is not the same test as the
                                              // threshold's $1D32|$1D33 (see the push in CtSceneUpdateFollowers)
  int folMoving[64];                          // 1 = the follower stepped this frame (drives its walk anim)
  int folRoamPrev[64];                        // was this follower roaming last frame? (1 at init/placement so
                                              // a follower that roams from the start is NOT treated as a resume)
  int regroupFollowers;                       // UNUSED (its gather was retired for CtSceneBattleReturn); kept
                                              // so the debug scene states (CtSceneSaveState) keep their layout
  struct { int active, start, span, rate, ctr, phase; } palRot[64];  // op-0x88 cmd-0x20 PALETTE ROTATION
                                              // (the Telepod swirl): rotate `span` sprite-palette colors from
                                              // `start` by one every `rate` frames (ROM slot $0520, C0/4926)
  struct { int start, n; uint16_t bgr[16]; } palBlob[64];  // op-0x88 cmd-0x80+ BLOB: overwrite `n` BGR555
                                              // palette colours from `start` (= cmd low nibble). The Telepod
                                              // open's `88 8D` writes 3 blues to colours 13-15 = the contour
                                              // ring (our extracted sprite pal pads 13-15 black); fn5's `88 81`
                                              // is the post-warp 15-colour GREEN ramp from colour 1.
                                              // (open), 32->0 while ==2 (close); the iris radius scales with it.
                                              // cx/cy cache the swirl-emitter centre so the disc keeps drawing
                                              // through the close.
  int irisState;               // ROM $39: 0=idle, 1=OPENING (op-0xFF 0x90), 2=CLOSING (op-0xFF 0x91). The iris
                               // is its OWN state machine, independent of the swirl's op-0x88 colorFx.
  int textbox;                 // 1 = a dialogue box is up (blocks the showing object)
  int tbCloseArmed;            // C0/2081 set $0129 = 0x0D THIS frame; C0/2032 runs from the next
  int tbPageArmed;             // C0/2086 set $0129 = 8 THIS frame (a pressed page); C0/1FB4 runs from the next
  int tbPendChoice;            // a decision box's picked line, held until the op's COMPLETION visit
                               //   writes it into result (C0/365D) -- see CtSceneChoose
  int tbTable, tbIndex;        // current dialogue string-table file-addr + string index
  int tbTableAtShow;           // table snapshot taken when the box first shows (op time, not display)
  int tbChoice, tbObj;         // 1 if the open box is a decision/choice box; the object that opened it
  int tbOp;                    // the opening opcode (0xBB auto / 0xC1 top / 0xC2 bottom / 0xC0,C3,C4 choice)
  int tbOpt;                   // decision op operand2: $64=(>>2)&3 first option line, $65=&3 last (-1 = none)
  int naming, nameChar;        // op 0xC8 operand 0xC0+id: a NAME-ENTRY screen is up for char-id nameChar
  int loader, loaderArg;       // op 0xC8 ODD operand < $80: the mode-0 LOAD/NEW-GAME shell is up
  int shop, shopArg;           // op 0xC8 operand $80-$BF: menu MODE 2 = the SHOP (screen $0C); shopArg = the operand
  int warpReq;                 // op 0xFF operand $80-$8F: the bank-$FD GATE WARP (C0/3FE4-3FFC)
  int ffFade;                  // op 0xFF < $90: 1 = its own fade loop (C0/3FC1-3FD1) took its first `DEC $19` in the
                               //   op's frame -- the host runs the rest before the jump (CtSceneFfFading)
  int rebootReq;               // op 0xDF/0xE1 (C0/3B98/3B4B): 1 = staged, the machine waits for the vblank and
                               //   reboots through C0/0000; 3 = op FF 96 (C0/4181): the same wait, then the RESET
                               //   entry FD/C000; 2 = the host took it. Either way the main loop is dead.
  int seqReq;                  // op 0xFF operand < $80: a $C3 SPECIAL SEQUENCE was requested
                               //   (2 = the title screen). The script never resumes.
  int seqHas;                  // ⚠ 1 = seqReq HOLDS a request. Needed because the operand can be
                               //   ZERO: `FF 00` is how scenes 223 and 225 ask for table entry 0
                               //   (the motorcycle race -- sequence_table.md section 7.1), and a
                               //   bare `seqReq ? seqReq : -1` could not tell that from no request
                               //   at all. A separate flag rather than a -1 sentinel because the
                               //   scene is zero-initialised: -1 would have to be written at every
                               //   construction site, and missing one would silently request
                               //   entry 0 on a scene load.
                               //   (C0/0D00 -> C2/8000 -> screen $0E). loaderArg = the raw operand.
  int tcQ[16][8]; int tcN;     // scripted TILE COPIES for the host: sl,st,el,eb,dl,dt,flags (ctTileCopyQueue)
  // THE SONG-UPLOAD FREEZE (song_load_freeze.md section 6). A sound op's `JSL $C7:0004` is synchronous in
  // the ROM: the main loop parks until the SPC has echoed the last unit, the NMI keeps counting frames.
  // The sink prices the JSL (audio.c's loader clock: master cycles from the listing + the emulated SPC +
  // the frame's background); the tick then SUSPENDS after the posting visit -- the queue's tail and the
  // legs wait for the frame the JSL returns in -- and resumes there with the SAME queue (the ROM's queue
  // was built before the JSL; nothing re-arms a cadence while the loop is parked). The runner's raster
  // gate C0/5A5D (`CMP $69`, $0169 = $48 = line 72) reads the zelda3 model's FIXED virtual beam, V = 0
  // (user decision 2026-09-27; snes.c snes_readBBus for the oracle), so the return frame runs the rest of
  // the queue: the priced return line no longer cuts it at line 72 as it did on the real beam.
  int sndWait;                 // frames still to lose before the resume tick
  int sndResumeN;              // the queue index to resume at
  int sndPosted, sndFreezeFrames, rasterLine;   // per-visit: a priced post happened; frames it cost; the line it returned on
  int sndEndLine, sndFrozenTick;   // the resume line; this tick returned early (the host's per-frame legs must not run)
  int head117Set; uint8_t head117;  // $0117 as C0/0C76 read it at the frame's head (CtSceneHeadLatch), for the drain
  int exitCommitted;                // C0/0C9A committed the pending change: the main loop is left (CtSceneExitCommit)
  uint8_t sndElig[64];         // the suspended tick's eligibility snapshot
  uint8_t sndOrder[64]; int sndNq, sndResumeObj;   // ...its queue order, and the object whose visit the JSL interrupted
  // THE TAIL OVERRUN. The main loop syncs on the NMI with clear-then-wait semantics (C0/EC60: `INC $0152 /
  // LDA $0152 / BNE` -- the handler clears it), entered AFTER the frame's legs. Measured on the gate cutscene
  // (`--hookpc EC60/5AA3`): in a normal frame the runner ends by line 8 and the wait is entered at line 69
  // (61 lines of A810 + drain + draw pass); on the return frame the JSL came back at line 169, the wait was
  // entered at 245 -- past the NMI at 225 -- so the loop waited for the NEXT NMI and the following window
  // had no tick at all (no queue-build at f194 254; the next at f195). So one more frame is lost whenever
  // `return line + SND_TAIL_LINES >= 225`. SND_TAIL_LINES is that measured 61: the legs' cost is emergent
  // (heavy frames reach 134-188 here), so returns within ~20 lines of the boundary can go either way.
  int sndPostResumeFreeze;     // the tick after the resume is lost too (the tail overran the NMI)
  int sndOpAddr, sndSavedAddr; // the posting op's address, and the ip the op advanced to: the ROM stores the
                               // object's ip when the VISIT ends, i.e. after the JSL returns, so for the frames
                               // lost the oracle's ip still reads the op's own address (vmdiff f154: 063D vs 063F)
  // THE $0117 BIT-5 SERVICE, C0/1770-188B (disasm/notes/field_tile_copy_e5.md). The main loop's dispatch C0/0C76 ->
  // C0/16DC -> C0/1770 takes a bit-5 request (op 0xE5's, or op 0xD8's with bit 0) at the START of the frame after the op
  // and holds the main loop inside itself for several frames -- it parks in its own EC60 waits, so no WRAM byte carries
  // where it is and the port keeps the position here: 1 = the align loop C0/1781-178E (885A + a whole event frame per
  // step, until $0138 clears; that frame then runs `1790 JSR C0/AF4E` and FD/FFF7), 2 = the style branch's frames
  // (C0/179A on $0145: only C0/00DE runs -- no event VM, no player, no box machine), 3 = the frame the branch RTSes
  // into (C0/00A7 on: 881E .. 00BF, without C0/0097, C0/18D9 and C0/0C76). What used to stand for all this was a
  // constant 8-frame freeze (`copyFreeze`) and a 16-frame render delay, both fitted to the real-time cartridge's lag.
  int xfer, xferStyle, xferStep;
  int xferOp[7];               // $013E-$0144 as C0/1790's AF4E used them (the style frame's upload redraws that rect)
  int xferNmi46;               // $0146 was set by a style step: the next NMI (C0/EAD1) clears it
  int bgD2x, bgD2y, bgD3x, bgD3y;  // BG2/BG3 build origins minus BG1's (render model; see CtSceneLayerOffsets)
  int explore;                 // op 0xE3: 0 = player control LOCKED (cutscene), 1 = free
  int moveLock;                // $0162 != 0 = the player MOVEMENT controller (C0/881E, which also runs the
                               // party-trail) is locked even though explore/$011F may be 1 -- e.g. the candy
                               // stand leashes Crono ($0162=0x80) so the conga must NOT trail the followers
  uint16_t btnHeld;            // $00F2 held joypad (CT layout A=0x80 X=0x40 L=0x20 R=0x10 B=0x08
                               // Y=0x04 Sel=0x02 St=0x01), read by the op 0x34-0x39 button-branch ops
                               // (C0/66C6+; the bell minigame's "press A again"). Those ops read the
                               // WRAM byte now; this copy is only --vmdiff's CtSceneSetButtons bookkeeping.
  uint8_t btnPrev;             // last frame's $00F2, so CtSceneSetButtons can derive the
                               // NEWLY-PRESSED $00F0 that ops 0x3F-0x44 latch and consume.
  int bright, brightTarget;    // op 0xF0 screen fade: 0..15 (SNES INIDISP model); armed fades
                               // step +-1/frame, holding while tile copies are pending
  int loadFadeIn;              // the host is inside the LOCATION LOAD's fade-in ramp, ROM C0/2848 (called
                               // once from C0/0094, between the load and the first main-loop frame). That
                               // loop's body is `INC $19 / [STZ $1F] C0/881E / C0/00BF / C0/EC60`: it runs
                               // the 00BF legs (scheduler C0/59D9, copy-freeze C0/5A46, camera C0/A810,
                               // anim drain C0/CA76) and NONE of the main loop's own -- no joypad latch
                               // (C0/0097), no box machine (C0/1F87), no C0/0C76 (so no $0119 fade ramp
                               // and no $0117 changeloc drain). Measured, scene 468 --hookpc: C0/0097 is
                               // visited ZERO times across the whole ramp while C0/59D9 runs every frame.
  int locChange;               // the pending change COMMITTED (C0/0C9A, CtSceneExitCommit): the main loop
                               // has left for C0/0000 and the host loads $0100. Only the commit raises it --
                               // op 0xDD/0xDE (C0/3B1B) write $0100-$0104 and move nobody (see the op).
  int lcLoc, lcX, lcY, lcFacing;  // destination location id (word&0x1FF), tile x/y, facing 0-3
                               // (= (hibyte>>1)&3, per the ROM: LDA $DA / LSR / AND #$0F -> $04)
  int lcStyle;                 // always 0 now (read back from the committed words)
                               // op 0xE0 (C0/3AB7) is the FADE variant: it stages into the $0112-$0116
                               // WRAM cells + latches $0117 bit7 + arms the $0119=0x0F fade counter and
                               // PARKS its object on the op; the CtSceneTick drain (ROM C0/0C8A) counts
                               // $0119 down and raises locChange when it reaches 0 (fire+16 frames).
  int playerLoaded;            // 1 iff SOME object's init actually loads a PC. The ROM keeps the lead's
                               // object offset in $0197 and leaves it at the 0x80 "none" sentinel when no
                               // object loads one -- locations 92 and 240 (both LOADMON-only cutscene maps)
                               // read $0197 = 0x80 with the leadPC slot UNPLACED in the oracle. playerObj
                               // still falls back to 1 so every PC_i = playerObj+i reference stays in range;
                               // this flag is the "is there a player at all" answer the renderer needs.
  int playerObj;               // cached lead-PC object index (op 0x57 LOADPC slot 0). The party loads
                               // into CONSECUTIVE objects from here (PC_i = playerObj+i), so op 0x22/0x24
                               // (PC coords/facing) and op 0xB6 (cat-chase PC) resolve PC_i = playerObj+i,
                               // NOT the old hardcoded obj(i+1). Keeps the VM's player refs in sync with
                               // the engine rendering the lead PC as the player (Telepod obj3, else obj1).
  int rgBase;                  // body-offset of the ROM battle-RETURN regroup block (ct_regroup.h ctRegroupReturn,
                               // C0/5CEC's copy at $7F3700 = IP 16FF). The post-victory resume (CtSceneBattleReturn)
                               // and op 0xDA VM-CALL each follower onto it; its 0x95 walk-to-lead + 0x71/0x12
                               // barrier + E3 01 restore control. -1 = none.
  int d9Base;                  // body-offset of op 0xD9's first walk program ($7F3720 = IP 171F; slot k at +0x20*k).
};
// The followers, as the ROM finds them: the party slots $0199 / $019B (the object * 2; bit 7 = the slot is empty) --
// slot 1 first, and slot 2 only when slot 1 is filled (C0/345F / 34E2 for op DA, C0/0355 / 036D for the battle return).
// The object init (C0/426A) and the party ops (D4, C0/394E) keep the bytes in WRAM. Both callers used to take the objects
// of move-mode 1 and 2 instead, and a member who has left the party keeps his mode: in 078 after Yakra `D4 04` empties
// slot 2 ($019B 0A -> 8A) and the DA that follows releases Lucca alone on the cartridge ($7F03FE := 2, control back 20
// frames later), where the port released Frog too ($7F03FE := 1) and waited for his walk (57 frames; the playthrough's
// leg 27). Returns the count, the objects in fol[].
static int ctPartyFollowers(CtScene* sc, int fol[2]) {
  int s1 = r8(sc->mem, 0x7E0199), s2 = r8(sc->mem, 0x7E019B), nf = 0;
  if (!(s1 & 0x80) && (s1 >> 1) < sc->nobj) {
    fol[nf++] = s1 >> 1;
    if (!(s2 & 0x80) && (s2 >> 1) < sc->nobj) fol[nf++] = s2 >> 1;
  }
  return nf;
}

// $1001,X -- THE CADENCE COUNTDOWN, a real WRAM byte, not a private counter. The ROM's queue build
// tests and decrements it (C0/59F0-59F6), every enqueue arm reloads it from the period $1000,X
// (C0/5A1F / 5A2B / 5A40), op 0x87 re-arms it (C0/488B) and nine routines STZ it (C0/31AA, 3264,
// 3301, 3386, 340D, 34D9, 352C, 5B1B, 5B5F) to mean "eligible next build". It used to live only in
// `st[].dc`, which is what made op 0x87's SECOND store unportable -- writing the period to $1000
// alone cannot move a countdown that PASS 1 already reloaded. Written through here so the byte is
// the ROM's and the write mask grades it.
// (The PERIOD still lives in `moveAnim`, not in $1000: the fresh-spawn default 4 is a modelled
// value the port never stores. That asymmetry is real and unclosed -- $1000 is only authoritative
// on a WRAM-seeded scene.)
static void ctSetDc(CtScene* sc, int obj, int v) {
  sc->st[obj].dc = v;
  w8(sc->mem, 0x7E1001u + (uint32_t)obj * 2, v & 0xFF);
}

// Hand a scripted tile copy to the host (CtScenePollTileCopy): the render's copy of the map (SceneData.l1t..) and the
// field's collision planes, which the port keeps outside WRAM. `flags` = the $0144 mask, plus CT_TC_RENDER to reach only
// the picture or CT_TC_COLL only the planes -- op 0xE5's two halves land on different frames (the WRAM planes at
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

// memory + actor effects (the non-control ops), mirroring runFunc; coord ops also move
// the live float position. Reused by _step for the main-loop body.
// C0/FE00, the ROM's own table (op-0x7F RNG + AE2D): the container's (CT_ASSET_TABLE), else the user's dump
#define kRandAngle(i) (CtRomViewHas(0x00FE00) ? CtRomByte(NULL, 0, 0x00FE00 + (i)) : CtRomImage(NULL)[0x00FE00 + (i)])
static int mvRandAngle(Mem* m);
static void applyEffect(CtScene* sc, int obj, int addr, uint8_t op) {
  CtVmActor* a = &sc->actors[obj]; const uint8_t* B = sc->body; Mem* m = sc->mem; int ps;
  { dbgFlags(); if (gEnvTrace && obj == gEnvTraceObj)   // DEBUG: trace one object's ops
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
  // op 0x87 SCRIPT SPEED, the whole of C0/4876 (m=1 x=0):
  //   4876: INY / TYX / LDA $7F2001,X      ; the operand
  //   487C: LDX $6D / INC A / STA $D9      ; $D9 = operand + 1, in 8-bit (0xFF wraps to 0)
  //   4881: LDA $1000,X / AND #80          ; keep ONLY the pause bit
  //   4886: ORA $D9 / STA $1000,X / STA $1001,X
  //   488E: TYX / INX / SEC / RTS          ; IP +2, and SEC = the runner keeps going
  // We used to do `a->moveAnim = B[addr+1] + 1` and stop there -- the period lived in a C field and
  // never reached WRAM. The write-mask comparator caught it: obj33's $7E1042 sat at 0x04 (its seed
  // 0x84 with the pause bit cleared by obj32's `0C 42`) where the ROM writes 0x03. That is not
  // cosmetic -- it is the CAUSE of the pinned obj33 first divergence: a period of 4 instead of 3
  // means its next visit lands one frame late, which is the f129 `ip ours=0B00 oracle=0B02` row.
  // The SECOND store (C0/488B) re-arms the live COUNTDOWN with the same byte, and that is the whole
  // f129 divergence: the queue build at C0/00C1 runs BEFORE the runner at C0/00C4, so by the time
  // obj33's `87 02` executes, PASS 1 has already reloaded the countdown from the OLD period (4). The
  // ROM's store overwrites it with the NEW one (3), which is why the oracle's next visit is 3 frames
  // later and ours was 4. Writing $1000 alone could never fix that -- the period is read only at the
  // NEXT expiry. The countdown is now the WRAM byte $1001,X (see ctSetDc), so this is a real store.
  // Note A carries the pause bit into BOTH: a paused object re-armed by its own `0B`-then-`87` gets a
  // countdown of 0x80|period. Kept literal rather than masked -- the ROM does not mask it either.
  if (op == 0x87) {
    int per = (B[addr + 1] + 1) & 0xFF;
    uint32_t A1000 = 0x7E1000u + (uint32_t)obj * 2;
    int v = (r8(m, A1000) & 0x80) | per;
    w8(m, A1000, v);                                    // C0/4881-4888
    w8(m, A1000 + 1, v);                                // C0/488B  STA $1001,X -- re-arm the countdown
    if (sc && obj >= 0 && obj < sc->nobj) sc->st[obj].dc = v;   // keep the shadow in step
    a->moveAnim = per;
    return;
  }
  if (op == 0x84) { a->solid = B[addr + 1] & 1;               // $1B01,X solidity (C0/4867)
                    w8(sc->mem, 0x7E1B01 + (uint32_t)obj * 2, B[addr + 1]); return; }   // C0/486F, whole byte
  if (op == 0x22) {                                           // GetObjCoords (C0/65EB): PC[op1]'s
    // ⭐ THE OPERAND IS A PARTY SLOT, AND THE TABLE IS $0197. C0/65EB is op 0x21's handler with ONE
    // instruction inserted: `65EF LDA $7F2001,X / 65F3 TAX / 65F4 LDA $97,X / 65F6 BRA ->65B2`,
    // where 65B2 is 0x21's own `TAX / LDA $1801,X`. So 0x21 takes a raw object OFFSET and 0x22 takes
    // the same offset OUT OF $0197 + operand (DP = $0100). `playerObj + op1` was a paraphrase that
    // only agrees while the party objects are consecutive: on saves/battle_re/party3_field.state
    // $0197/$0199/$019B = 02/06/08, i.e. objects 1, 3, 4 -- where the paraphrase says 1, 2, 3.
    // The operand is the ALREADY-DOUBLED slot index; measured over the 504 shipped scenes it is 0 at
    // all 30 of this op's sites, 0/2 for op 0x24 and 0/2/4 for op 0xA9, never odd.
    // ⭐ THE LEAD'S TILE IS HIS WORDS' HIGH BYTE AS THE PASS FINDS THEM: `LDA $1801,X` / `LDA $1881,X` (C0/65B2-65BA) --
    // last frame's commit, because the frame's own step lands later, at C0/A810 (C0/00D3, after the event pass). The port's
    // host hands the lead's new position to fx/fy before the tick; the WRAM words take it only at the tick's A810 mirror,
    // so the bytes are the ROM's. MEASURED, loc 120 below the castle stairs (saves/field_re/scene468_stairs_to_120.state,
    // Down from f180, the port's keys a frame after): obj0's `22 00 09 0A` at f205 stores row 0A on the cartridge (the
    // word 0AFF before the step to 0B0F) and Lucca's poll sees 0B from f209 -- her box at f211; reading fx stored 0B at
    // f205 and opened it at f208 (ROADMAP F3).
    int po = vmSlotTarget(m, op, B[addr + 1]) / 2;            // TILE x/y -> op2/op3.
    if (po >= 0 && po < sc->nobj) {                           // drives the "wait for Crono to reach
      uint32_t X2 = (uint32_t)po * 2;                         // the spot" dialogue triggers (scene 1: Mom)
      int wx = r8(m, 0x7E1800 + X2) | (r8(m, 0x7E1801 + X2) << 8), wy = r8(m, 0x7E1880 + X2) | (r8(m, 0x7E1881 + X2) << 8);
      w8(m, LOC + B[addr + 2] * 2, (wx >> 8) & 0xFF);
      w8(m, LOC + B[addr + 3] * 2, (wy >> 8) & 0xFF);
    }
    return;
  }
  if (op == 0x21) {                                           // GetObjCoords (C0/65B0): object[op1/2]'s TILE
    int po = B[addr + 1] / 2;                                 // x/y -> locals op2/op3. Operand = a DIRECT obj
    if (po >= 0 && po < sc->nobj) {                           // index*2 (NOT a PC slot like 0x22). obj0's master
      w8(m, LOC + B[addr + 2] * 2, ((int)sc->fx[po] >> 4) & 0xFF);   // loop runs `21 06 0D 0E` (Marle->loc 0x0D/
      w8(m, LOC + B[addr + 3] * 2, ((int)sc->fy[po] >> 4) & 0xFF);   // 0x0E, read by the pendant obj14 fn3
    }                                                          // `8C 0D 0E`) and `21 1A 19 1A` (the fair cat
    return;                                                   // obj13 -> loc 0x19/0x1A, read by the cat girl
  }                                                           // obj12 fn1 `12 19 25`). MUST read the LIVE fx,
                                                              // not actors[].x (frozen at spawn for movers) --
                                                              // else the mirrored tile never tracks a moving
                                                              // NPC: Marle's pendant spawned at her SPAWN, and
                                                              // the cat's tile stayed 11 so the girl never saw it.
  if (op == 0xA8 || op == 0xA9) {                            // face toward object / PC (C0/55A0/5605):
    // op 0xA9 = face toward PC[op1]'s OBJECT. ROM C0/5605 reads $97,X (the party-slot->obj table),
    // i.e. PC[i] = the live lead-PC object, NOT a fixed obj1. Resolve PC[i] = playerObj+op1 (same as
    // op 0x22/0x24), so this is correct when the lead isn't obj1 (Telepod sc8: Crono=obj3). The old
    // FACE_TARGET +1 hardcode targeted empty obj1, so 'A9 00' no-op'd and Marle kept her LEFTOVER move
    // facing -- e.g. she stepped onto the left pod walking WEST (96 04 07) and stayed facing LEFT during
    // "Don't go away" (str 95) instead of turning DOWN toward Crono below her. (op 0xA8 still takes a
    // direct obj ref -- but it SHARES the gate below, which did change.) The static init runner keeps
    // FACE_TARGET (PC0=obj1) to stay byte-exact vs the Py VM.
    // ⭐ The two handlers differ by ONE instruction and then share a body: 0xA8 is `55A4 LDA $7F2001,X
    // / 55A8 TAX` (the operand IS the object offset) and 0xA9 is `5609 LDA $7F2001,X / 560D TAX /
    // 560E LDA $97,X / 5610 TAX / BRL ->55A9` -- the offset comes OUT OF $0197 + operand. move.c's
    // graded runner already reads it that way (ctVmPartySlotTarget / the 560E arm); this mirror was
    // still on `operand + playerObj`, which disagrees the moment the party objects are not
    // consecutive (party3_field.state: slots 02/06/08 = objects 1, 3, 4).
    int off = (op == 0xA8) ? B[addr + 1] : vmSlotTarget(m, op, B[addr + 1]);
    // 55A9 `LDA $1100,X / BMI ->5600`: the target's MOVE-MODE bit 7 declines the whole op, and it is
    // the SHARED body, so it gates 0xA8 too. This replaces an `actors[t].hasPos` stand-in. As in the
    // 0x95/0xB6 arm, it is NOT an emptiness test: an empty slot's 0x80 makes the ROM read
    // $7E:1100+0x80 = $7E:1180 and decline only because that byte happens to be negative.
    if (r8(m, 0x7E1100 + (uint32_t)off) & 0x80) return;
    int t = off / 2;
    if (t >= 0 && t < sc->nobj) {                            // Crono instead of holding the arms-raised pose
      int dx = (int)sc->fx[t] - (int)sc->fx[obj], dy = (int)sc->fy[t] - (int)sc->fy[obj];
      vmSetFacing(sc->mem, a, obj, mvTables()[0x400 + mvAngle(dx, dy)]);
    }
    return;
  }
  if (op == 0x4F) w8(m, LOC + B[addr + 2] * 2, B[addr + 1]);
  // New-game / scene RAM-init ops (run by scene 0's bootstrap). ROM C0/67D7/67FB/6846/69A6.
  // 0x48/0x49 (the pointer read, C0/6792/67C7) are not here: step() hands them to move.c's CtVmReadPtrFamily.
  // 0x4A-0x4D (the poke family, C0/67D7..6829) are not here: step() hands them to move.c's CtVmPokeFamily.
  else if (op == 0x4E) {                           // MemCopyLiteral (MVN): copy lenword-2 literal bytes
    uint32_t dst = (uint32_t)B[addr + 1] | ((uint32_t)B[addr + 2] << 8) | ((uint32_t)B[addr + 3] << 16);
    int cnt = (B[addr + 4] | (B[addr + 5] << 8)) - 2;       // source = body[addr+6..]; dest e.g. $7E2980 party
    for (int k = 0; k < cnt; k++) w8(m, dst + (uint32_t)k, B[addr + 6 + k]);
  }
  // Scene-5 fair-race SPEED RUBBER-BAND (RNG-dispatch, NOT position-gated). Each obj0 controller loop: op-0x7F draws a
  // random byte var[op1]:=$FE00[++$F8], op-0x6F shifts it >>4 to a 0..15 nibble, op-0x19 loads it as `result`; a 16-way
  // op-0x1A switch (already handled) routes the nibble to ONE racer's accel/decel/boost block (var[0x10+i] += / -= ),
  // then op-0x04 CALLs each racer's fn5 which runs op-0x8A ($1A00 := var[0x10+i]). So each racer's speed changes ~1/4 of
  // loops, up AND down, irregularly -> the lead changes (byte-exact: var[0x10..0x13] == the racers' live $1A00).
  else if (op == 0x89 || op == 0x8A) vmSpeedOp(m, a, &sc->mspd[obj], B, sc->header, addr, obj);   // C0/4B49 / 4B58
  // The local INC/DEC/SET 0x71-0x77 (C0/6C69..6CF0): move.c's CtVmLocalIncFamily, the body --sweepops grades. This
  // runner had 8-bit paraphrases of 0x71/0x73/0x75/0x77 and stepped over the SIXTEEN-bit 0x72/0x76: the tent of horrors'
  // hostage counter `72 12` never moved, so neither its sparkle (B2) nor its flames (0120) ever came.
  else if (op == 0x71 || op == 0x72 || op == 0x73 || op == 0x75 || op == 0x76 || op == 0x77) {
    int ni; CtVmLocalIncFamily(m->s, B - sc->header, addr + sc->header, &ni);
  }
  else if (op == 0x5A) w8(m, GLB, B[addr + 1]);
  // The variable grid 0x50-0x59 and the local arithmetic 0x5B-0x6F (C0/6A1F..6C68, the rubber-band's 0x6F shift
  // included) are not here: step() hands them to move.c's CtVmVarFamily / CtVmLocalArithFamily.
  else if (op == 0x65) { int li = B[addr + 2], bit = B[addr + 1]; if (bit & 0x80) li += 0x100; w8(m, GLB + li, r8(m, GLB + li) | (1 << (bit & 7))); }
  else if (op == 0x66) { int li = B[addr + 2], bit = B[addr + 1]; if (bit & 0x80) li += 0x100; w8(m, GLB + li, r8(m, GLB + li) & ~(1 << (bit & 7))); }
  // C0/6299 `LDA $7F2001,X / LDX $6D / STA $1C80,X` -- the operand byte goes into WRAM WHOLE, and
  // $1C80,X IS THE STORAGE, not a mirror: C0/5BA0 reads it back to gate the object-solid test. The
  // port kept only the two decoded bits in sc->coll/collMap and never wrote the byte, so the moment
  // that gate started reading WRAM (as the ROM does) it saw 0 forever. Measured before the fix:
  // npcMoveAngle entered 450 times in scene 285 and was gated 450 times -- object collision silently
  // dead, with the whole 183-check suite green. Nothing in the suite reaches this line.
  else if (op == 0x0D) { uint8_t v = B[addr + 1];
    sc->coll[obj] = (v >> 1) & 1; sc->collMap[obj] = v & 1;
    sc->actors[obj].coll = sc->coll[obj]; sc->actors[obj].collMap = sc->collMap[obj];
    w8(m, 0x7E1C80 + (uint32_t)obj * 2, v); }                    // C0/629F
  else if (op == 0x0E) { sc->c1C81[obj] = B[addr + 1];           // C0/62AE STA $1C81,X -- same shape
    w8(m, 0x7E1C81 + (uint32_t)obj * 2, B[addr + 1]); }
  else if ((ps = pcSprite(op)) >= 0) { a->sprite = ps; a->defined = 1;
    a->moveMode = (ps == r8(sc->mem, 0x7E2980)) ? 0 : (ps == r8(sc->mem, 0x7E2981)) ? 1
                : (ps == r8(sc->mem, 0x7E2982)) ? 2 : 0x80; sc->mode[obj] = a->moveMode;   // FIX: $1100 at the LIVE load site too
    pcLoaderIdStores(m, obj, ps);                                                         // 41E4.. STA $8D+id / 4295-43D7 STA $1101,X
    w8(m, 0x7E1100 + (uint32_t)obj * 2, a->moveMode);                                     // $7E mirror (battle transfer reads it)
    if (a->moveMode <= 2) w8(m, 0x7E0197 + (uint32_t)a->moveMode * 2, obj * 2);           // party-slot -> obj-X table (C0/426A)
    if (a->moveMode <= 2) { sc->coll[obj] = 0; sc->collMap[obj] = 0; a->coll = 0; a->collMap = 0;
      w8(m, 0x7E1C80 + (uint32_t)obj * 2, 0); }
    if (a->moveMode != 0x80) { a->draw = 0; a->dead = 0; pcLoadMirror(m, obj, ps);   // in-party only (see the init site)
      if (a->moveMode == 0) pcArrivalArm(m, obj, a);
      else { pcFollowerArm(m, obj, a); sc->fx[obj] = a->px16 / 16.0; sc->fy[obj] = a->py16 / 16.0; } } }   // STZ $1C80,X in ALL THREE party arms (C0/42A1 / 4315 / 43C8),
                                                    // not just the lead's -- and the BYTE, which is what C0/5BA0 reads
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
      else { pcFollowerArm(m, obj, a); sc->fx[obj] = a->px16 / 16.0; sc->fy[obj] = a->py16 / 16.0; } } }   // STZ $1C80,X in ALL THREE party arms (C0/42A1 / 4315 / 43C8),
                                                    // not just the lead's -- and the BYTE, which is what C0/5BA0 reads
  // No `draw` write: C0/4476 / C0/4559 / C0/46DF do not touch $1A81 (see the init-runner arms above).
  else if (op == 0x81) { a->sprite = B[addr + 1]; a->defined = 1; a->moveMode = 3; sc->mode[obj] = 3; a->dead = 0;
    npcLoadMirror(m, obj, B[addr + 1], 3); a->facing = 1; }         // unconditional NPC (C0/4476) + allocator claims
  else if (op == 0x82) { a->sprite = B[addr + 1] + 7; a->defined = 1; a->moveMode = 4; sc->mode[obj] = 4; a->dead = 0;
    npcLoadMirror(m, obj, B[addr + 1] + 7, 4); a->facing = 1; }
  else if (op == 0x83) { a->sprite = B[addr + 1] + 256; a->defined = 1; a->dead = 0;
    a->moveMode = (B[addr + 2] & 0x80) ? 6 : 5; sc->mode[obj] = a->moveMode;   // C0/47EC: op2 bit7 -> mode 6
    loadMonMirror(m, obj, B[addr + 1], B[addr + 2]); a->facing = 1; }                          // + the $7E per-object mirror (staging reads it)
  // ROM stores X at the tile CENTER ($1800 low=0x80, e.g. tile 26 -> 0x1A80 = 424.0) and Y at the
  // FOOT ($1880 low=0xFF = anchor-1/16). So only Y gets the -1/16 foot adjust; X is the raw center.
  // (Was: fx = a->x - 0.0625, putting every sprite 1px LEFT of the oracle -- bed.state obj1=424/our 423.)
  else if (op == 0x8B) { a->x = B[addr + 1] * 16 + 8; a->y = B[addr + 2] * 16 + 16; a->hasPos = 1; sc->fx[obj] = a->x; sc->fy[obj] = a->y - 0.0625;
    a->px16 = B[addr + 1] * 256 + 0x80; a->py16 = B[addr + 2] * 256 + 0xFF;
    vmPosWordsMirror(m, obj, a->px16, a->py16);                                    // C0/4B74's $1800/$1880 stores
    vmPlacePrio(m, obj, B[addr + 1], B[addr + 2]); }                               // C0/4B98-4BBC
  else if (op == 0x8C) { vmPos8C(m, a, obj, B[addr + 1], B[addr + 2]);           // C0/4BC3 (see vmPos8C)
    sc->fx[obj] = a->px16 / 16.0; sc->fy[obj] = a->py16 / 16.0; }
  // op 0x8D (C0/4C27) is the RAW-WORD setter: `LDA $7F2001,X / STA $1800,X` then the same into
  // $1880,X -- it stores the two 16-sub-pixel words UNCHANGED. Unlike op 0x8B (C0/4B74), which
  // synthesises the low bytes (X := 0x80 centre, Y := 0xFF foot) and therefore owns the -1/16
  // foot anchor, 0x8D has NO anchor to apply. Subtracting 1/16 here put every 0x8D-placed object
  // one sub-pixel high, and when the word was tile-aligned that crossed a TILE boundary: scene
  // 129's obj13 (`8D E0 02 00 08` = y 0x0800) sat at tile 7 instead of the oracle's tile 8, which
  // is what the C0/A947 camera-window test reads -- the trigger fell outside the window and could
  // never be activated. Oracle o129 wram: obj13 $1880/$1881 = 0x0800 exactly.
  else if (op == 0x8D) { int w8x = B[addr + 1] | (B[addr + 2] << 8), w8y = B[addr + 3] | (B[addr + 4] << 8);
    sc->fx[obj] = w8x / 16.0; sc->fy[obj] = w8y / 16.0; a->px16 = w8x; a->py16 = w8y;
    a->x = (int)sc->fx[obj]; a->y = (int)sc->fy[obj]; a->hasPos = 1;
    vmPosWordsMirror(m, obj, w8x, w8y);                                            // C0/4C31 / 4C3D, the raw words
    vmPlacePrio(m, obj, w8x >> 8, w8y >> 8); }                                     // C0/4C42-4C6C
  else if (op == 0x0F || op == 0x17 || op == 0x1B || op == 0x1D || op == 0xA6 || op == 0xA7 ||   // C0/5535..5593 -> the
           op == 0x1E || op == 0x1F || op == 0x25 || op == 0x26) {                                  //   554B tail (move.c)
    int ni, tg; CtVmFacingFamily(m->s, B - sc->header, addr + sc->header, obj * 2, &ni, &tg);
    if (tg / 2 < sc->nobj) sc->actors[tg / 2].facing = m->s[0x1600 + tg] & 3; }
  // SHOW / HIDE / REMOVE all funnel into ONE ROM tail, C0/4CD9 -- and what they write is $1A81,X:
  //   op 0x90 SHOW   C0/4CD5: LDX $6D / LDA #01 / STA $1A81,X / TYX / INX / CLC / RTS
  //   op 0x91 HIDE   C0/4CE0: LDX $6D / LDA #00 / BRA ->4CD9
  //   op 0x7E REMOVE C0/4CE6: LDX $6D / LDA #80 / BRA ->4CD9
  // $1A81 is the SCRIPT visibility flag, and it is the gate C0/A894 tests in the activation pass
  // (`LDA $1A81,X / BEQ ->A910 / BMI ->A910` -- hidden or removed => the object is NOT activated).
  // It is NOT $0F00: $0F00 is the per-frame latch A810 DERIVES from $1A81 plus the camera window.
  // The port's `draw` carries the same three states inverted (0 shown / 1 hidden / 2 removed), so
  // mirror the ROM byte too -- op 0x0A already writes $1A81 and the activation-pass port will read it.
  //
  // ⚠ CORRECTION to what this comment used to claim. It said the Manoria encounter is finalised by
  // "another object hiding the trigger NPC with `7D`". That attribution was mine and is UNSUPPORTED:
  // the only measured writers of obj32's post-battle $1A81 are C0:07FB / C0:080C, the enemy epilogue
  // (see CtSceneBattleRemoveObj) -- a `7D` would have reported a writer inside C0/4CD9. The op
  // semantics below are right; they are simply not that encounter's mechanism.
  else if (op == 0x90 || op == 0x91 || op == 0x7E) {
    a->draw = (op == 0x90) ? 0 : (op == 0x91) ? 1 : 2;
    w8(sc->mem, 0x7E1A81 + (uint32_t)obj * 2, (op == 0x90) ? 0x01 : (op == 0x91) ? 0x00 : 0x80);
  }
  // The CROSS-OBJECT show/hide land in the SAME C0/4CD9 tail as the self ops above, so they write the
  // target's $1A81 too: op 0x7C = C0/4CEC (LDA #01), op 0x7D = C0/4CF9 (INY / TYX / LDA $7F2001,X /
  // TAX -- the operand IS the target index -- / LDA #00 / BRA ->4CD9). Ours only moved `draw`, which
  // left $1A81 stale: after the Manoria fight another object hides the trigger NPC with `7D`, the
  // oracle's $1A81 goes 0 and the activation pass drops $0F00 to 0 (measured f2309 -> f2332), but our
  // pass kept it LIVE and C0/9923 could still see a defeated encounter's trigger.
  else if (op == 0x7C || op == 0x7D) {
    int t = B[addr + 1] / 2;
    if (t < sc->nobj) { sc->actors[t].draw = (op == 0x7C) ? 0 : 1;
                        w8(sc->mem, 0x7E1A81 + (uint32_t)t * 2, (op == 0x7C) ? 0x01 : 0x00); }
  }
  // op 0x88 COLOR effect (ROM C0/4892): cmd 0x20-0x5F allocates the per-object effect slot
  // (sets $7F0B80,X), cmd 0 closes it. The Telepod cutscene (obj11 fn2) calls the vortex's
  // open (88 8D palette blob + 88 20 = set ref) then close (88 00). The blob form (0x80-0x8F)
  // only copies palette DATA -- it does NOT set the ref, so it does not arm visibility here.
  else if (op == 0x88) {
    uint8_t c = B[addr + 1];
    if (c == 0) {                 // close (C0/48C0): clear the slot ref + restore the palette.
      a->colorFx = 0;
      sc->palRot[obj].active = 0; sc->palBlob[obj].n = 0;   // the object stops cycling NOW -- every object, the gate's swirl
                                  // too: C0/48C0 has no exception for it (a "keep cycling until the iris shut" rule stood here).
      // The WRAM side of the close -- C0/48C0 frees the record the ref names and re-uploads the object's 24-byte $E4
      // record (colours 1-12) into BOTH CGRAM pages, colours 13-15 untouched (Frog's weapon row survives) -- is the
      // shared body's now (step() / runFunc -> move.c CtVmColorFx). It runs ONLY when the ref $7F0B80,X is not
      // negative (48C8 BMI); this arm used to re-upload unconditionally.
    }
    else if (c >= 0x80 && c < 0x90) {                              // BLOB (C0/49F8-4A43): inline palette colours.
      // THE BLOB IS A CGRAM-SHADOW WRITE, PER SLOT, FOR EVERY OBJECT. C0/49F8: $DB = 0x80 + ($0F81,X >> 1) * 16
      // (the slot's first CGRAM entry), $DD = 2 * ($DB | (cmd & 0x0F)); the script word at ip+2 minus 2 =
      // the byte count; MVN copies those bytes from the script (ip+4) to $7E:2200+$DD (4A25-4A34, writer
      // C0:4A35) and again to $7E:2000+$DD (4A35-4A43, writer C0:4A43). Nothing about the object but its
      // slot is consulted, and every object sharing the slot sees the colours. Two scripts that were
      // dropped while this arm was gated on the Telepod-only `colorGated` flag: Manoria's flame obj16
      // prio-7 loop `88 82 0C 00 FF7F 2A7F 0000 0075 0054` (the BLUE of all four flames -- their $E4:1038
      // record is orange), and Frog's `88 8D 08 00 FF7F 0B6F FF01` = his weapon colours 13-15 (the sword
      // read black through the baked asset palette, defect #19). Measured writers on the oracle: $7E:231A /
      // $7E:211A by C0:4A35 / C0:4A43 the frame obj4 executes that op.
      // The two MVNs themselves are the shared body's (move.c CtVmColorFx, 4A32/4A40); this arm keeps the C mirror.
      int start = c & 0x0F, len = B[addr + 2] | (B[addr + 3] << 8), nbytes = len - 2;
      if (nbytes < 0) nbytes = 0;
      int ncol = nbytes / 2;                                        // the C mirror the swirl's clip/retention path reads
      if (start + ncol > 16) ncol = 16 - start;
      sc->palBlob[obj].start = start; sc->palBlob[obj].n = ncol;
      for (int i = 0; i < ncol; i++) sc->palBlob[obj].bgr[i] = B[addr + 4 + i * 2] | (B[addr + 4 + i * 2 + 1] << 8);
    }
    else if (c >= 0x20 && c < 0x60) {                              // ref/SET: arm the slot
      a->colorFx = 1;
      // ROM handler C0/4892: cmd 0x20/0x30 BEQ ->C0/4919 (the ROTATE arm); cmd&0xF0==0x40 BRL ->C0/4970
      // (a DIFFERENT per-frame transform = a colour RAMP/fade, still NYI). The arm at 4919 is GENERAL --
      // it allocates the per-object slot for ANY object, with NO PC/NPC distinction. Operand p1 = B[addr+2]
      // packs START (p1>>4) + SPAN (p1&0x0F); p2 = B[addr+3] = RATE (frames per 1-step advance). In our
      // per-sprite 16-colour palette the CGRAM base is the sprite's own palette, so START is the colour
      // index within it. The per-frame processor FD/E532 cyclic-LEFT-shifts span+1 colours.
      // The rotation (0x20/0x30) is a small, coherent set game-wide (census: 94 unique sites = 20 Telepod
      // swirl spr94/95 + 70 MON magic/teleport-glow + 3 PCspr + 1 PC) all using ~the same shimmer params
      // (start=1, span=14) -- it is NOT the 4958-site cmd-0x40-0x5F ramp form. So arming it for EVERY actor
      // is the ROM mechanism, not the broad "tint every NPC" change the colorGated scope had feared.
      // This restores Crono's gate-entry warp: scene-8 obj3 fn11 `88 20 1E 08` (after he steps into the
      // open Telepod gate) cycles HIS sprite palette -- previously dropped because Crono isn't colorGated.
      if (c == 0x20 || c == 0x30) {
        uint8_t p1 = B[addr + 2], p2 = B[addr + 3];
        sc->palRot[obj].start = (p1 >> 4); sc->palRot[obj].span = (p1 & 0x0F);
        sc->palRot[obj].rate = p2 ? p2 : 1; sc->palRot[obj].ctr = 0; sc->palRot[obj].phase = 0;
        sc->palRot[obj].active = (p1 & 0x0F) > 1;
      }
    }
  }
}

// ---- the REAL mover (ROM C0/4E73-5215 handlers + C0/ABA2 angle + C0/ACFD step) ----
// Position units: 1/256 tile (16 units = 1px; tile = px>>4). A "step" lasts $1A01 frames
// (the ticket), each frame adding (mspd * comp[angle]) / 256 units per axis, where comp =
// the C0/F800 sine table (signed, |127|; X reads sin[angle+0x40] WITHOUT wrap -- indices
// 0x100-0x13F land in the ROM's half-amplitude tail, a faithful quirk). Target-tile ops
// re-aim and start a new step each script VISIT with ticket = the actor's $1000 script
// speed, so movement is seamless at any cadence; 92/9C take the ticket from the operand.
static const uint8_t* mvTab;                       // [0x400 atan32x32][0x100 face][0x140 sin]
static uint8_t mvFallback[0x640];
void CtSceneSetMoverTables(const uint8_t* blob, int len) { if (blob && len >= 0x640) mvTab = blob; }
static const uint8_t* mvTables(void) {
  if (mvTab) return mvTab;
  for (int dy = 0; dy < 32; dy++)                  // fallback when ct_assets.dat is stale:
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
// ⚠ THIS IS THE TREE'S SECOND PORT OF C0/ABA2. The other is CtMoveDirAngle (src/oam.c), which
// move.c calls at seven real ROM sites and which --sweepaba2 micro-executes; it takes NO arguments,
// reads the four bytes the ROM's callers stage at $01F0-$01F3, models the routine's five WRAM side
// effects, and branches on an UNSIGNED borrow. This one takes deltas the caller has already
// computed, in the TILE domain (`tgtTile - curTile`, see the note at the op-0xA0 arm below).
// ⭐ IT IS NOT UNGRADED, and that was worth measuring rather than assuming: swapping the table
// index below and running the WHOLE suite turns 440 pass into 336 pass / 104 fail. `--mutate`
// cannot show that -- it runs only the one named check, and --sweepaba2 grades the other port.
// What is still open is whether the two AGREE over the domain this one is fed; no check compares
// them to each other.
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
static int mvComp(const uint8_t* T, int M, int idx) {   // signed units/frame, TRUNCATED:
  int v = T[0x500 + (idx & 0xFF)];                       // cos(a) = sin[(a+0x40) & 0xFF]: the ROM WRAPS the cosine
  // index mod 256 into the main sine quarter. The 0x140-long table's tail (0x100..0x13F) is a HALF-RATE sine
  // (ext[0x110]=25 != main[0x10]=49), NOT the rising cosine -- reading it unwrapped halved the X velocity of any
  // mover with angle >= 0xC0 (up-right), so racers drifted too vertical off the oracle path. Oracle byte-exact:
  // obj8 ang D0 -> sin[0x10]=49 -> vx=6 (not ext[0x110]=25 -> 3); obj10 ang FA -> vx=28 (not 20).
  return (v < 0x80) ? (M * v) >> 8 : -((M * (0x100 - v)) >> 8);  // ROM keeps only the mult high byte (-7/f exact)
}
// The STEP ISSUE writes the ROM's registers too: $1900/$1980 (the velocity bytes C0/ACFD stores), $1600
// (the heading's facing, `LDA $F700,X / STA $1600,X`), and then runs C0/5614 on them -- the idle/walk/run
// selector every move op JSRs after issuing (13 sites: 4EC0 4FA5 4FE3 5037 507D 50E4 5122 527B 52B9 53C4
// 5409 54B0 54EE). It writes $1680 = 1 (walk) with $1681 = $1601 = 0 unless already walking; the drain
// then animates from those bytes. `anim` = 0 for the one mover whose ROM issue path has no 5614 (op 0xA0).
static void mvStartStep(CtScene* sc, int obj, int angle, int ticket, int setFace) {
  const uint8_t* T = mvTables();
  sc->mx[obj] = mvComp(T, sc->mspd[obj], angle + 0x40) / 16.0;   // X: sin[a+0x40] (cosine)
  sc->my[obj] = mvComp(T, sc->mspd[obj], angle) / 16.0;          // Y: sin[a]; 16 units = 1px
  sc->cycles[obj] = ticket > 0 ? ticket : 1;
  if (setFace) vmSetFacing(sc->mem, &sc->actors[obj], obj, T[0x400 + angle]);   // 4FB5 LDA $F700,X / STA $1600,X
}
// The STEP ISSUE's registers, committed ONCE on the velocity ACFD finally publishes: $1900/$1980 (C0/AF1F/AF24
// store the pair; a walled step stores ZERO), then the op's `JSR C0/5614` -- the idle/walk/run selector on
// |$1900|+|$1980| that every move op runs after ACFD (13 sites: 4EC0 4FA5 4FE3 5037 507D 50E4 5122 527B 52B9
// 53C4 5409 54B0 54EE); it writes $1680 = 1 (walk) with $1681 = $1601 = 0 unless already walking, or $1680 = 0
// when the velocity is zero. The drain animates from those bytes. `anim` = 0 for op 0xA0, whose issue path
// has no 5614. Committing per ATTEMPT (player-ward, then the $FE00 bounce, then the stall) left the bounce's
// velocity in the image for a walled mover the ROM holds at zero -- party3's obj27 `96 37 1D`.
// ...and ACFD's OAM priority pair (AD85-ADE8 / AE06-AE25 / AED5-AF15), on the velocity it publishes: the cell of the
// TENTATIVE position ($1800 + $DD, $1880 + $DB, the high bytes, `AND $1D1E` 16-bit), then with $0F80 bit 7 (AUTO) the
// two property planes -- NOT crossed like the placement ops': $0C01 from $7E7040, $0C00 from $7E7000 (ADBD-ADD2) --
// else the byte's nibbles ($0C01 = p & 30, $0C00 = (p << 4) & 30, ADD7-ADE8). A zero velocity never reaches the block
// (AD73 BEQ ->ADEB) and a walled one stores zero past it (AF27). The live mover wrote no pair at all, so an NPC kept its
// loader's or its placement's for good -- MEASURED, the cartridge's scene 5 (saves/field_re/s5_bell_marle.state):
// obj 8's pair follows the tile it steps into the whole race (C0:ADD5: 2020 -> 3020 -> 2020 -> 3030 ...).
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
// ROM C0/FE00 (FULL 256 bytes): the pseudo-random angle table ACFD pulls from -- via the never-reset
// 8-bit global $F8 -- when a mover hits a SOLID map tile; the mover's random "bounce" off walls (AE27).
// The ROM (AE2B LDX $F8 under SEP #$10) indexes all 256 entries; a 64-entry table masked & 0x3F is WRONG
// for any $F8 >= 64 and traps movers in concave corners (the fair race at the booth) -- entries 64..255
// carry the escape angles the racer needs (verified: 64-entry => AF27 stall loop; 256-entry => routes the
// corner in ~5 bounces). Bytes 0..63 are unchanged from the prior table.
// THE PSEUDO-RANDOM ANGLE CURSOR -- ROM `$F8`, and it is **$7E01F8**, not $7E00F8: DP = $0100 throughout
// the field engine, the same trap as the `$68` op budget ($0168) and the `$71` gfx-heap cursor ($0171).
//
//   AE27  SEP #10
//   AE29  INC $F8          <- the cursor advances FIRST
//   AE2B  LDX $F8
//   AE2D  LDA $FE00,X      <- so the index is the POST-increment value
//
// It is a never-reset GLOBAL shared by the C0/ACFD wall bounce and VM op 0x7F (C0/6D09), which is why
// keeping it in the WRAM image rather than in a C field matters twice over: it SEEDS from the oracle
// (measured 0x36 on party3_field, writer C0:AE2B) and it is graded by the write mask. As a C field
// starting at 0 the port rolled angles 1,2,3... where the ROM rolled 0x37,0x38..., so obj27 -- parked on
// `96 37 1D`, a move into a mover-solid tile -- stalled forever on our side while the ROM's second roll
// (cursor 0x38) came back passable at f23 and it walked off at velocity (-1,+7).
static int mvRandAngle(Mem* m) {
  int c = (r8(m, 0x7E01F8) + 1) & 0xFF;                  // AE29 INC $F8 (8-bit: SEP #10 / M 8-bit)
  w8(m, 0x7E01F8, (uint8_t)c);
  return kRandAngle(c);                                  // AE2D LDA $FE00,X
}
// ROM C0/5B95 (OBJECT-only -- it reads NO $7E/$7F map tile): a mover with collision enabled ($1C80
// bit1, sc->coll) is blocked when the destination tile in its heading holds another SOLID object.
// ---- C0/5B95, THE OBJECT-SOLID TEST: its gate chain, in ONE place, over the ROM's own bytes ----
// Exported because --sweepsolid micro-executes C0/5B95 and compares against THESE functions. A sweep
// that graded a private copy of the rule would grade the copy, which is exactly how the draw
// compare's class guard came to survive its own mutation.
//
//   5B95: STZ $EA / LDX $6D
//   5B99: LDA $0F00,X / BNE ->5BA0      the MOVER's activation latch; 0 -> 5B9E CLC / RTS
//   5BA0: LDA $1C80,X / BIT #02 / BEQ   the object-collision bit; clear -> the same CLC / RTS
//   5BA9: LDA $1600,X / ASL A / STA $D9 the heading, from the FACING byte
//   5BBD: LDA $7F2000 / ASL A / TAX     scan starts at the LAST object and walks DOWN
//   5BC3: LDA $0F00,X / BNE ->5BD2      the CANDIDATE's activation latch  <-- the long-ungraded line
//   5BD5: LDA $1100,X / BMI ->5BC8      class bit7 = an unloaded slot
//   5BD7: CPX $6D / BEQ ->5BC8          itself
//   5BDB: LDA $1B01,X / BIT #01 / BEQ   not SOLID
//   5BE6: JSR ($5BF2,X)                 the per-heading distance box; carry set = BLOCKED
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
// C0/5BFA / 5C20 / 5C47 / 5C6D -- THE PER-HEADING DISTANCE BOX, byte for byte. `5BE6 JSR ($5BF2,X)`
// picks one of the four by `$1600,X * 2` (the FACING byte, not the move angle). `$DB`/`$DD` hold the
// MOVER's $1800/$1880, latched 16-bit at 5BB1/5BB6.
//
//   5BFA up     LDA $1880,X / SBC $DD / BPL rej / EOR #FFFF / CMP #0100 / BCS rej   then the X axis
//   5C20 down   LDA $1880,X / SBC $DD / BMI rej /             CMP #0100 / BCS rej   then the X axis
//   5C47 left   LDA $1800,X / SBC $DB / BPL rej / EOR #FFFF / CMP #0100 / BCS rej   then the Y axis
//   5C6D right  LDA $1800,X / SBC $DB / BMI rej /             CMP #0100 / BCS rej   then the Y axis
//   cross axis  LDA .. / SBC .. / BPL +3 / EOR #FFFF / CMP #0100 / BCS rej
//   5C43 rej    SEP #20 / CLC / RTS        accept: SEP #20 / SEC / RTS
//
// THREE ASYMMETRIES, all transcribed rather than tidied -- each one is a place a hand-port rounds off:
//
//  (1) THE PRIMARY AXIS IS HALF-OPEN AND THE TWO HALVES DIFFER BY ONE. On the negative-required arms
//      (up/left) the reject is `EOR #FFFF` -> `CMP #0100`, and ~A = |A|-1, so it accepts |d| <= 0x100.
//      On the positive-required arms (down/right) there is no EOR and it accepts d <= 0x00FF. So the
//      box reaches 16.0 px behind the heading and 15.9375 px ahead of it. The cross axis inherits the
//      same split from its own `BPL +3 / EOR`.
//  (2) THE FIRST SBC RUNS ON THE CARRY LEFT BY `5BD7 CPX $6D`. Nothing between them touches C (LDA /
//      BIT / BEQ / STX / LDX / JSR), so C = (cand2 >= mover2) -- and since 5BD7's BEQ already removed
//      equality, C = (cand > mover). A candidate with a LOWER index measures one subpixel further away
//      than the same candidate with a higher one.
//  (3) THE SECOND SBC ALWAYS RUNS WITH C = 0, because the last flag-setter before it is the primary
//      axis's `CMP #0100` with its BCS not taken. So the cross-axis delta is always -1.
//
// $1800/$1880 are 12.4 fixed point: #0100 = 16.0 px.
// The core takes the four coordinates in 12.4 and the carry, so the LIVE mover can pass its own
// sub-pixel position while --sweepsolid passes the ROM's $1800/$1880. ONE copy of the rule either way.
int CtVmSolidBoxAt(int mx, int my, int cx, int cy, int carry0, int heading) {
  int primaryY = (heading <= 1);                                    // 0 up / 1 down are the Y arms
  int wantPos  = (heading == 1 || heading == 3);                    // down / right require d >= 0
  int p = primaryY ? cy : cx, pm = primaryY ? my : mx;
  int q = primaryY ? cx : cy, qm = primaryY ? mx : my;
  uint16_t a = (uint16_t)(p - pm - (1 - carry0));                   // the FIRST SBC
  if (wantPos) { if (a & 0x8000) return 0; }                        // BMI -> 5C43
  else         { if (!(a & 0x8000)) return 0;                       // BPL -> 5C43
                 a = (uint16_t)~a; }                                // EOR #FFFF
  if (a >= 0x0100) return 0;                                        // CMP #0100 / BCS -> 5C43
  uint16_t b = (uint16_t)(q - qm - 1);                              // (3): the SECOND SBC, carry 0
  if (b & 0x8000) b = (uint16_t)~b;                                 // BPL +3 skips the EOR
  return b < 0x0100;                                                // CMP #0100 / BCS -> 5C43
}
// The WRAM form, exactly as C0/5B95 assembles it: $DB/$DD latched from the MOVER at 5BB1/5BB6, the
// candidate read per iteration, and the carry from 5BD7's `CPX $6D`.
int CtVmSolidBox(const uint8_t* ram, int mover2, int cand2, int heading) {
  return CtVmSolidBoxAt(ram[0x1800 + mover2] | (ram[0x1801 + mover2] << 8),
                        ram[0x1880 + mover2] | (ram[0x1881 + mover2] << 8),
                        ram[0x1800 + cand2]  | (ram[0x1801 + cand2]  << 8),
                        ram[0x1880 + cand2]  | (ram[0x1881 + cand2]  << 8),
                        cand2 >= mover2, heading);
}
// ---- C0/5B95's SCAN, the whole loop, in ONE place ----------------------------------------------
//
//   5BBD: LDA $7F2000 / ASL A / TAX      X starts at nobj*2 -- ONE PAST the last object
//   5BC3: LDA $0F00,X / BNE ->5BD2       the candidate gates (CtVmSolidCandGate)
//   5BC8: DEX / DEX / BNE ->5BC3         ...and the loop EXITS AT ZERO
//   5BCC: REP #10 / LDY $C7 / CLC / RTS  nothing blocks
//
// So the ROM probes the slot ONE PAST the array and NEVER tests index 0. Both are transcribed: the
// one-past probe is a real read of live WRAM (whatever the next object's bytes happen to hold), and
// index 0 -- the scene's controller object in every field scene -- can never block a mover.
//
// The position accessor is why this is one function instead of two. --sweepsolid drives it over a
// WRAM image and wants $1800/$1880; the live mover wants its own sub-pixel state, because the port
// commits the $1800/$1880 mirror once per frame AFTER the movement loop and WRAM would be a frame
// stale there. Passing `pos = NULL` reads the ROM's bytes. Everything else -- the bounds, the gate
// chain, the box -- is shared, so a mutation of any of it moves BOTH callers and the sweep sees it.
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
// ⚠ THE POSITIONS ARE THE COMMITTED $1800/$1880, NOT THE LIVE sc->fx/fy, and the previous commit's
// note here had it backwards. The ROM's nine `JSR C0/5B95` sites all sit in the MOVE-OP bodies
// ($4Exx-$54xx), i.e. inside the event VM at C0/00C4 -- and $1800/$1880 are written only by C0/A810's
// integrate arms (AA3B/AA43/AA4E/AA95/AADA/AB25 and the Y siblings), which run later, at C0/00D3.
// C0/ACFD, the step the move op does issue, writes velocity and the ticket, NEVER the position. So the
// ROM's own object-solid test reads the PREVIOUS frame's committed positions, uniformly, for every
// object -- there is no partial update to model.
// The port's phase is the same: CtSceneTick runs the scheduler (where moveOp -> npcMoveAngle ->
// npcSolidTile live) BEFORE the integrate and before the commit, so $1800/$1880 hold exactly what the
// ROM would have there. Reading the bytes rather than sc->fx/fy also removes a rounding mismatch that
// had no business existing: the commit truncates `(int)(fx * 16.0)` while the accessor rounded with
// lround(), and the box decides on single-subpixel margins.
static int npcSolidTile(const CtScene* sc, int obj, int heading) {
  return CtVmSolidScan(((Mem*)sc->mem)->s, sc->nobj, obj * 2, heading, NULL, NULL);
}
// ROM C0/5281 (the shared op 0x94/0xB5/0xB6 move body): if a SOLID OBJECT blocks the direct heading,
// rotate it +0x30 ONCE and commit -- the mover does NOT search further and does NOT stall; it issues the
// velocity step regardless. No map check exists in this path (C0/5B95 and the C0/ACFD step read no tile).
// LIVENESS COUNTERS for the object-solid path. --sweepsolid grades what C0/5B95 DECIDES; nothing
// graded that the engine still REACHES it, and that gap cost three commits: switching the mover
// gate to read $1C80 from WRAM (as C0/5BA0 does) while op 0x0D still wrote only sc->coll left
// object collision dead in every fresh scene, through three green 183-check runs. These are what
// CtSceneSolidLive reports, and field-solid-liveness pins them.
static int gMvaCalls = 0, gMvaGated = 0, gSolidHit = 0;
static int npcMoveAngle(const CtScene* sc, int obj, int angle) {
  // C0/5B95's head, both gates, through the one exported copy. The port used to have only the
  // $1C80 half: an UNACTIVATED mover (off-camera) does not consult the object plane at all in the
  // ROM -- 5B99's BNE falls to 5B9E CLC / RTS -- so it cannot be deflected by anything.
  gMvaCalls++;
  if (!CtVmSolidMoverGate(((Mem*)sc->mem)->s, obj * 2)) { gMvaGated++; return angle; }
  const uint8_t* T = mvTables();
  int f = T[0x400 + angle];                                        // 0 up,1 down,2 left,3 right
  { int hit = npcSolidTile(sc, obj, f); if (hit) { gSolidHit++; angle = (angle + 0x30) & 0xFF; } }
  return angle;
}
// Op 0xA1's C0/5B95 (51F4): 51F1's `JSR C0/ABA2` is followed at once by the solid test, with no `LDA $F700,X / STA
// $1600,X` between them, so the scan runs against the facing the object ALREADY had, and a hit nudges only the heading
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
// Would the current step's velocity cross INTO a solid MAP tile (ROM ACFD $7E7040-bit7 test)?
static int mvCrossesSolid(const CtScene* sc, int obj) {
  double nfx = sc->fx[obj] + sc->mx[obj], nfy = sc->fy[obj] + sc->my[obj];
  int ntx = (int)nfx >> 4, nty = (int)nfy >> 4;
  return (ntx != ((int)sc->fx[obj] >> 4) || nty != ((int)sc->fy[obj] >> 4)) && sc->tileSolid(sc->tileCtx, ntx, nty);
}
// C0/568B -- the move-op family's ARRIVAL / DECLINE exit forces the auto anim IDLE:
//   568B LDX $6D / LDA $1780,X / BEQ ->5693 / RTS      mode != 0: the script owns the anim, nothing
//   5693 LDA $1680,X / CMP #00 / BEQ ->56A5             already idle: nothing
//   569A LDA #00 / STA $1680,X / STZ $1681,X / STZ $1601,X   IDLE (anim 0) + step/timer reset
// Called from the seven move-op exits C0/4E85 4F9C 502D 50DB 5273 53BB 54A8 (anim_rate_c98a_5614.md).
// The port had the step-issue side (C0/5614: WALK on every issued step) but not this exit, so a scripted
// mover that reached its tile kept anim 1 and a walk frame in $0F01,X for as long as it stood there.
// MEASURED (marle_getting_in_the_left_pod, obj14 = Marle, `--monitor 7E169C:2,7E0F1C:2`): the oracle's
// $1680/$1681 go 01 01 -> 00 00 and $0F01 05 -> 01 on the frame after the ticket expires (f28); ours
// stayed 01 01 / 05 -- the 'Marle moved without animating' of the 2026-09-02 list (defect #7).
// $1600,X IS THE FACING. The drain's frame lookup (C0/CBA4, CtFieldFrameLookup) reads it from the image;
// every ROM facing writer is a `STA $1600,X` (the facing ops C0/xxxx for 0F/17/1B/1D/A6, the move family's
// `LDA $F700,X / STA $1600,X` at issue, the face-object ops). The port kept the facing in a C field and
// mirrored it only at load, so the drain rendered stale facings: Marle's `17` (face down) on the Telepod
// left $1600 = 0 and her idle frame came out as assembly 0 (idle-up) where the oracle shows 1 (idle-down).
static void vmSetFacing(Mem* m, CtVmActor* a, int obj, int fac) {
  a->facing = fac & 3;
  w8(m, 0x7E1600u + (uint32_t)obj * 2, (uint8_t)(fac & 3));
}
static void mvForceIdle(CtScene* sc, int obj) {
  uint32_t x = (uint32_t)obj * 2;
  if (r8(sc->mem, 0x7E1780 + x) != 0) return;                       // 568D-5692
  if (r8(sc->mem, 0x7E1680 + x) == 0) return;                       // 5693-5698
  w8(sc->mem, 0x7E1680 + x, 0); w8(sc->mem, 0x7E1681 + x, 0); w8(sc->mem, 0x7E1601 + x, 0);   // 569A-56A2
  ctAnimSyncActor(&sc->actors[obj], sc->mem->s, x);
}
// ROM C0/5265-5271 + C0/527B -- the `$1C81,X` BIT 1 arm of the move-toward-actor body (C0/5228), shared
// by ops 0x94/0x95 and, through C0/354F/5410, by 0xB5/0xB6. It is reached only once the mover is already
// ADJACENT: instead of stopping a tile short (the 5273 `JSR C0/568B` + INX INX SEC arrive path) the ROM
// calls C0/30B3 -- which steps ONE PIXEL on BOTH AXES AT ONCE toward the target's exact pixel and arms
// `$1A01` = `$7F0B00` = 1 -- and, on its SEC return, runs `527B JSR C0/5614`, the very same idle/walk/run
// selector every other move issue runs, before yielding with the IP unchanged.
//
// ⚠ ALL THREE OF THOSE WERE PARAPHRASED HERE. The old arm stepped ONE AXIS PER VISIT (X to completion,
// then Y), armed `$7F0B00` = #80, and never called 5614 at all -- so an actor walking this arm rendered
// with whatever `$1680,X` it last had, i.e. IDLE. That is the party JOIN: scene 439's `0D 00 / 0E 02 /
// 95 00` walks Marle onto Crono and she slid there with no walk cycle ("she walks to crono without
// animation"). ORACLE, saves/field_re/s439_marle_join_lake.state, `--monitor 7E1686:1,7E1906:1,7E1986:1`:
// `$1680,X` = 06 from f1198 while BOTH velocities are live ($10/$F0 -- |vx|+|vy| = $20, and $1100,X = 3
// < 4, so C0/5614 picks the fast index 6), then 01 from f1214 once only X still moves ($10/$00), then 00
// at f1250 when 568B arrives. Diagonal, too: the oracle walks (+1,-1) per visit, we walked (+1,0).
// Returns 1 = still snapping (yield, IP unchanged), 0 = fully aligned (caller takes the 5273 arrive arm).
// NOTE the port's live ticket is `sc->cycles`; the image's `$1A01,X` that 30B3 stores is read back here
// and, like everywhere else in this VM, not maintained afterwards (it is a seed-time input only).
// C0/305D -- the $1C81 BIT-0 ARRIVAL SNAP. One gate, five handlers: `LDA $1C81,X / BIT #01 / BEQ / JSR C0/305D / BCS`
// at C0/4F90 (0x96), C0/50CF (0x97), C0/501E (0x9A), C0/5158 (0xA0) and C0/51DC (0xA1). It tests the SUB-TILE nibbles
// and sets BOTH velocities in ONE call:
//   305F STZ $1900,X / STZ $1980,X          ; clear both
//   3065 LDA $1800,X / AND #F0 / CMP #80    ; X sub-tile at the CENTRE?
//   306E   (yes) LDA $1880,X / AND #F0 / CMP #F0 -> equal: CLC RTS = SNAP COMPLETE
//   3079   (no)  BCS -> 3082 LDA #F0 (-1px) else 307B LDA #10 (+1px) -> $1900,X
//   3089        THEN FALL INTO the SAME Y test -- 309B $1980,X = +10 / 30A7 = -10
//   3092/30A2   LDA #01 / STA $1A01,X / SEC RTS   ; ticket 1, carry SET = keep snapping
// So the step is DIAGONAL: X and Y correct together on one visit. The port stepped ONE AXIS per visit (X to
// completion, then Y), which on a 4-frame cadence takes twice as long and walks the actor along an L instead of the
// ROM's diagonal -- measured on Taban (obj10, $1C81 = 0x01) in scene 8: from f30 the oracle moves (+1,+1) px per
// visit while ours moved (+1,0). Returns 1 = stepped (the handler yields, IP parked), 0 = on the tile's
// centre-bottom (the handler completes). What follows the SEC differs per handler: 0x96/0x97 re-anim (JSR C0/5614)
// before `TYX / CLC`, 0xA0 goes straight to `516A TYX / CLC / RTS`.
static int mvArriveSnap305D(CtScene* sc, int obj, int atx, int aty) {
  int cx = atx * 16 + 8, cy = aty * 16 + 15;     // sub-tile 0x80 = px 8 (centre) / 0xF0 = px 15 (bottom)
  int dix = cx - (int)sc->fx[obj], diy = cy - (int)sc->fy[obj];
  if (!dix && !diy) return 0;
  sc->mx[obj] = dix ? (dix > 0 ? 1 : -1) : 0;    // 307B/3082
  sc->my[obj] = diy ? (diy > 0 ? 1 : -1) : 0;    // 309B/30A7 -- the SAME visit
  sc->cycles[obj] = 1; sc->hasTask[obj] = 1;     // 3092/30A2 `LDA #01 / STA $1A01,X` + SEC
  return 1;
}
static int mv30B3Arm(CtScene* sc, int obj, int tgtObj, int anim) {
  uint32_t x = (uint32_t)obj * 2;
  uint8_t* r = sc->mem->s;                                   // the flat 128 KB image move.c indexes
  int t2 = tgtObj * 2;
  w8(sc->mem, 0x7E01E9, t2); w8(sc->mem, 0x7E01EA, t2 >> 8); // 5237 STX $E9 -- SIXTEEN bit; 30B3 reads it
  if (!CtVmMoveSnap30B3(r, (int)x)) return 0;                // 526E JSR C0/30B3; CLC = already aligned
  w8(sc->mem, 0x7E1900u + x, r[0x1900 + x]);                 // re-store the pair 30B3 published so the
  w8(sc->mem, 0x7E1980u + x, r[0x1980 + x]);                 //   write mask claims those bytes as ours
  sc->mx[obj] = (double)(int8_t)r[0x1900 + x] / 16.0;        // $1900/$1980 are 1/16 px -> px/frame
  sc->my[obj] = (double)(int8_t)r[0x1980 + x] / 16.0;
  sc->cycles[obj] = r[0x1A01 + x];                           // 3121/3137/314B  LDA #01 / STA $1A01,X
  sc->b00[obj] = r[0x10B00 + x];                             // 3124/313A/314E  STA $7F0B00,X -- POSITIVE,
  sc->hasTask[obj] = 1;                                      //   so AA07 free-integrates this one step
  if (anim) {                                                // 527B / 53C4 JSR C0/5614 -- the walk the port lost;
    CtVmMoveStepAnim(r, (int)x);                             //   0x9E/0x9F's 5319 -> 5323 goes straight to TYX / CLC
    ctAnimSyncActor(&sc->actors[obj], r, x);
  }
  return 1;
}
// CT_MVTRACE=<obj>: one line per moveOp visit and per integration step, for ONE object. The mover's
// live state -- fx/fy (px), mx/my (px/frame), the cycles ticket, hasTask, mspd -- lives in CtScene,
// NOT in WRAM, so `$1A00`/`$1A01`/`$7F0B00` in a CT_DUMPWRAM image are seed-time mirrors that say
// nothing about why a step fails to persist. This is the only view of the real thing.
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
  // THE ANIM/FACING LAYER, per handler (move.c's ctVmMoveActorOp flags, byte-verified there): 0xA0 (C0/5129), 0xA1
  // (C0/5191) and 0x9E/0x9F (C0/52C0/54F5) write no facing ($F700 -> $1600), run no C0/5614 and take no C0/568B.
  int animLayer = !(op == 0xA0 || op == 0xA1 || op == 0x9E || op == 0x9F);
  // THE LEGGED PAIR 0x98/0x99 (C0/534A / C0/550E) opens with op 0x92's $1A80 head: once a step was issued, the
  // first visit whose ticket is spent completes the op (534C-535C: `STZ $1A80,X`, IP += 3, and NO C0/568B on this
  // arm) -- one aimed step of the script's own length, no re-aim. hasTask is the latch here, as for 0x92.
  if ((op == 0x98 || op == 0x99) && sc->hasTask[obj]) { sc->hasTask[obj] = 0; return 0; }
  // ROM C0/5614 (JSR'd at step-issue by the directional/tile move family -- 0x92/94/96/97/9A/9C/9D/B5/B6
  // -- but NOT the 0xA0 fmove): re-select the anim mode. A mode-3 SHAPE (op 0xAC) is PROMOTED (C0/5620):
  // held index $FF (our animIndex<0) -> auto walk (mode 0), else its held animeset (mode 1). Modes 0/1/2
  // are left as-is. This is what makes Taban/Marle play the WALK cycle while a scripted move slides them
  // (their 0xAC freeze-pose only shows when STILL). 0xA0 keeps the frozen shape (its issue path has no 5614).
  // op 0x9C/0x9D (vector moves, ROM C0/4EC7/4F0A) have NO C0/5614 re-anim -- exclude them so the
  // actor keeps its current pose; the tile-move family (0x96 etc) DOES re-anim (Taban/Marle walk).
  if (animLayer && op != 0x9C && op != 0x9D && a->animMode == 3) { a->animMode = (a->animIndex < 0) ? 0 : 1; a->animGen++; }
  if (op == 0x92 || op == 0x9C || op == 0x9D) {    // vector move: ONE step of operand frames
    if (sc->hasTask[obj]) { sc->hasTask[obj] = 0; sc->vecMove[obj] = 0; mvForceIdle(sc, obj); return 0; }   // C0/4E85 JSR C0/568B
    // op-0x9D (ROM C0/4F0A) reads BOTH operands as LOCAL-VAR indices: angle = var[op1], frames/ticket = var[op2]
    // (4F47 `LDA $7F0200,(op2*2) / STA $1A01`). op-0x9C/0x92 take both as literals. The bell minigame's ball
    // rises via `9D 1C 14` = var[0x1C] angle, **var[0x14] frames** = the bob strength -> the ball climbs HIGHER
    // the stronger the hit. Reading op2 as a literal (0x14) made the ball always rise the same 20 frames.
    int angle  = (op == 0x9D) ? r8(m, LOC + B[addr + 1] * 2) : B[addr + 1];
    int frames = (op == 0x9D) ? r8(m, LOC + B[addr + 2] * 2) : B[addr + 2];
    // ...and only op 0x92 RE-ANIMS. A ROM-wide scan for `JSR C0/5614` (bytes 20 14 56) finds thirteen
    // sites -- 4EC0 4FA5 4FE3 5037 507D 50E4 5122 527B 52B9 53C4 5409 54B0 54EE -- and the op jump table
    // C0/5D6E puts op 0x92 at C0/4E73, op 0x9C at C0/4EC7 and op 0x9D at C0/4F0A: 4EC0 is inside 0x92's
    // body and there is NO site anywhere in 4EC7..4FA4, so the two VECTOR moves never re-select the anim.
    // The C mirror already excluded them (the `animMode == 3` promote above); the WRAM call did not, so
    // C0/5614's mode-3 arm still ran on the bytes: `LDA $1680,X / CMP #FF / BEQ -> STZ $1780,X` then the
    // idle/walk/run selector. MEASURED on the bell minigame (saves/field_re/s5_bell_marle.state, the
    // pinned drive): the ball obj26 is frozen by its fn0's `AC 00` and the oracle holds $1780 = 03,
    // $1680 = FF, $0F01 = 00 for the whole rise (88,559.9 -> 88,515.4), while the port flipped to
    // $1780 = 00 / $1680 = 01 on the first `9D 1C 14` step and drew assembly frame 1 -- a different
    // sprite, which is what the user saw as the strength indicator "changing palette".
    mvStartStep(sc, obj, angle, frames, op == 0x92); mvIssueRegs(sc, obj, op == 0x92);   // only 92 SETS facing from the
    sc->b00[obj] = -128;                                    // vector ops ARM $7F0B00 = #80 (ROM 4EFA): NEGATIVE ->
                                                            // AA07 leaves it + FREE-integrates every frame (no clamp)
                                                            // -> the full operand distance slides smoothly across tiles.
    sc->vecMove[obj] = (op == 0x9C || op == 0x9D);          // mark: SLIDE with the idle pose (no walk cycle)
    sc->hasTask[obj] = 1; return 1;                         // angle; 9C/9D KEEP it (ROM: their
                                                            // handlers have no $F700->$1600)
  }
  sc->vecMove[obj] = 0;                                     // any tile-move family op re-anims (walk)
  int dtx, dty;                                    // target-tile family: re-aim per visit
  // C0/3546 / C0/354F, ops B5 / B6: `STY $E3` (the op's IP, 16-bit) before `JSR C0/5215` / `JSR C0/5410`, whose `LDX $E3`
  // hands it back -- the call ops' scratch word, overwritten (reqScratch).
  if (op == 0xB5 || op == 0xB6) { w8(sc->mem, 0x7E01E3, addr & 0xFF); w8(sc->mem, 0x7E01E4, (addr >> 8) & 0xFF); }
  int tgtObj = -1;                                 // the object the 0x94/0x95/0xB5/0xB6 arm resolved,
                                                   // kept for op 0x95's JOIN branch further down
  if (op == 0x94 || op == 0x95 || op == 0xB5 || op == 0xB6 ||   // 0x94/0xB5 (C0/5215) = toward object[op/2];
      op == 0x9E || op == 0x9F || op == 0x98 || op == 0x99 ||   //   + 0x9E/0x9F (C0/52C0/54F5), 0x98/0x99 (C0/534A/550E)
      op == 0x8F) {                                             //   + 0x8F (C0/5429, 543C LDA $97,X)
    // TARGET RESOLUTION, byte-exact against move.c's graded ctVmMoveActorOp -- this mirror had
    // PARAPHRASED it as `operand + playerObj`. ⚠ THAT NOTE USED TO SAY "right only while the party is
    // full" AND IT UNDERSTATED THE BUG: the operand is the already-DOUBLED slot index (measured, it is
    // only ever 0/2/4 across the 504 shipped scenes), so the paraphrase is off by a factor of two on
    // every non-zero operand even with a perfectly consecutive party. See field_party_slot_table.md.
    //   0x94 / 0xB5  (C0/5215): 5223 LDA $7F2001,X / TAX -- the operand IS the target's OBJECT OFFSET.
    //   0x95 / 0xB6  (C0/5410): 5423 LDA $97,X -- through the PARTY-SLOT table $0197 + operand, which
    //                holds an object offset, or 0x80 when the slot is EMPTY.
    int off = (op == 0xB6 || op == 0x95 || op == 0x9F || op == 0x99 || op == 0x8F) ? vmSlotTarget(m, op, B[addr + 1]) : B[addr + 1];
    // C0/5228: `LDA $1100,X / BMI ->5273` -- the target's MOVE-MODE bit 7 declines the whole move.
    // ⚠ THIS IS NOT AN "IS THE SLOT EMPTY" TEST, and writing it as one would be an invention. With an
    // empty slot's 0x80 the read ALIASES to $7E:1100+0x80 = $7E:1180, object 0's IP low byte, and the
    // chase declines only because that byte happens to be negative (0x8D on bedslide.state). A tidied
    // check would diverge wherever $1180 reads < 0x80. Reproduced exactly as the ROM indexes it.
    // This is the bedslide defect: scene 2 parks obj14/obj15 on `B6 02` / `B6 04`, party slots 2 and 3
    // are empty (Crono alone), the ROM declines, and the paraphrase chased objects 3 and 5 instead --
    // walking two HIDDEN objects ~1 px/frame that the oracle holds perfectly still.
    if (r8(m, 0x7E1100 + off) & 0x80) {           // -> C0/5273's JSR C0/568B; 0x9E/0x9F's 52D6 -> 531E is a bare
      sc->hasTask[obj] = 0;                        //   `TYX` (nothing called, nothing written); 0x98/0x99's 536E ->
      if (animLayer) mvForceIdle(sc, obj);         //   53B6 also clears SELF's $1A80 (hasTask) before 53BB's 568B
      return (op == 0xB5 || op == 0xB6) ? 1 : 0;
    }
    int t = off / 2;
    // Port-level bounds guard, not ROM: the gate above already rejects every empty slot, so this is
    // unreachable in practice and exists so a malformed script cannot index off the actor array.
    if (t >= sc->nobj || !sc->actors[t].hasPos) { sc->hasTask[obj] = 0; return (op == 0xB5 || op == 0xB6) ? 1 : 0; }   // port-only guard: no ROM exit here
    tgtObj = t;
    dtx = (int)sc->fx[t] / 16; dty = (int)sc->fy[t] / 16;
  } else if (op == 0x96 || op == 0xA0) { dtx = B[addr + 1]; dty = B[addr + 2]; }
  else { dtx = r8(m, LOC + B[addr + 1] * 2); dty = r8(m, LOC + B[addr + 2] * 2); }
  int atx = (int)sc->fx[obj] >> 4, aty = (int)sc->fy[obj] >> 4;
  // The chase (B5/B6, ROM C0/5215) moves only while FARTHER than one tile; once WITHIN one tile
  // (|dx|<=1 && |dy|<=1) it STOPS but stays PERPETUAL (never advances the script -- every chaser's
  // loop flows straight into the next object's fn0, so advancing would clone it). 94/96/A0 = exact tile.
  int dxr = atx - dtx, dyr = aty - dty;
  // ---- C0/5228's ARRIVAL TEST: ADJACENCY, and it is a WRAP-EXACT 8-BIT compare per axis ---------
  // 5245-5252 (Y) and 5254-5263 (X), both `CMP / INC A / CMP / DEC A DEC A / CMP` on the TILE BYTES
  // $1801/$1881 -- NOT a signed |d| <= 1, which differs at the map wrap. `atx`/`aty` ARE those bytes.
  int _sxb = atx & 0xFF, _syb = aty & 0xFF, _txb = dtx & 0xFF, _tyb = dty & 0xFF;
  int adj = ((_syb == _tyb) || (((_syb + 1) & 0xFF) == _tyb) || (((_syb - 1) & 0xFF) == _tyb)) &&
            ((_sxb == _txb) || (((_sxb + 1) & 0xFF) == _txb) || (((_sxb - 1) & 0xFF) == _txb));
  if (op == 0x94 || op == 0x95 || op == 0x9E || op == 0x9F || op == 0x98 || op == 0x99) {   // move-toward-actor (C0/5215) / -toward-party-slot (C0/5410,
    // which BRLs into 5228 at 5426): ONE body, so ONE arrival rule. It halts as soon as the two tiles are
    // ADJACENT, and C0/5273 then does INX INX + SEC -- it ADVANCES past the op (the runner C0/5A93 commits X
    // on SEC). So both COMPLETE (not perpetual): the Marle-bump fn2 walks her up to Crono, stops one tile
    // away, and proceeds to FACE/pose/"I'm so sorry!". Same body as 0xB5/0xB6, but those loop forever.
    // ⚠ 0x94 USED TO FALL THROUGH TO THE EXACT-TILE ARM BELOW (`dxr == 0 && dyr == 0`), which is a
    // different, strictly TIGHTER rule -- the one the ROM applies to op 0x96/0x97/0xA0, never to this body.
    // The jump table settles which routine 0x94 is: the word at $C0:5D6E + 0x94*2 is 5215, and C0/5215
    // falls straight into 5228, the body 0x95 BRLs into at 5426. 117 sites across the shipped scenes run
    // op 0x94. MEASURED on loc 44 (script 425 obj3 fn1 `89 40 / 94 04 / ...`, `Belthasar --scene 44
    // --talk 3`): with the exact-tile rule obj3 walked ON TOP of its target and finished at frame 288,
    // tile (5,9) -- obj2's own tile; with the ROM's rule it stops ADJACENT at frame 264, tile (6,8)
    // against obj2's (5,9). A tile too far and 24 frames late, on every one of those sites.
    if (adj) {
      // ROM C0/5265: with $1C81 bit1 set (the Marle JOIN runs `0D 00` collision-off + `0E 02` + `95 00`),
      // DON'T stop one tile short -- C0/30B3 walks self onto the PC's EXACT tile, 1px/visit (slowly), so she
      // walks all the way to Crono before op 0x5C makes her a follower ("Lead on!" walk-up).
      if ((sc->c1C81[obj] & 2) && mv30B3Arm(sc, obj, tgtObj, animLayer)) return 1;   // 5267-5271 + 527B ($1A80 kept)
      sc->hasTask[obj] = 0; if (animLayer) mvForceIdle(sc, obj); return 0;   // 5273 / 53B8-53BB; 0x9E/0x9F: 531E TYX
    }
  } else if (op == 0x8F) {
    // C0/5429 -- toward a party member until ADJACENT **or** comfortably inside the camera window (move.c, graded).
    // The only member with NO $1C81 test and NO snap: 5462-547A the same wrap-exact adjacency, then 547C-5486 the
    // camera's low bytes halved into tiles and the asymmetric box op 0x28 uses (548D-54A4); either ends in 54A6/54A8
    // `LDX $6D / JSR C0/568B` and IP += 2. Otherwise 54B6-54EE issue a step like 0x94's: heading, facing, the solid
    // test with its second facing write, $1000 into $1A01 and $7F0B00, ACFD, C0/5614.
    if (adj) { sc->hasTask[obj] = 0; mvForceIdle(sc, obj); return 0; }
    int bx = (atx - (r8(m, 0x7E1D0A) >> 1)) & 0xFF, by = (aty - (r8(m, 0x7E1D0E) >> 1)) & 0xFF;
    if (bx >= 0x02 && bx <= 0x0D && by >= 0x02 && by <= 0x0C) { sc->hasTask[obj] = 0; mvForceIdle(sc, obj); return 0; }
  } else if (op == 0xB5 || op == 0xB6) {           // chase: stop once ADJACENT, stay perpetual -- C0/3546 forces
    if (adj) {                                     // CLC after the JSR (3548) so the inner INX INX never lands
      if ((sc->c1C81[obj] & 2) && mv30B3Arm(sc, obj, tgtObj, 1)) return 1;   // 5267-5271 bit-1 arm is theirs too
      sc->hasTask[obj] = 0; mvForceIdle(sc, obj); return 1;   // C0/5273 JSR C0/568B
    }
  } else if (op == 0xA0) {                          // keep-facing pace (Mom's cooking).
    // ARRIVAL IS TILE-INDEX EQUALITY (ROM C0/5129, disasm/notes/field_touch_preempt.md §2):
    //   5144: LDA $1801,X -> $F0 / 5149: LDA $1881,X -> $F1     ; the HIGH bytes of the 1/16-px
    //   514E: CMP $F3 / BNE ->516D / 5152: LDA $F0 / CMP $F2     ; position, i.e. the TILE INDEX
    //   5158: LDA $1C81,X / BIT #01 / BEQ ->5164                 ; bit0 CLEAR (the default) ->
    //   5164: TYX / INX / INX / INX / SEC / RTS                  ;   COMPLETE, IP += 3
    // `dxr`/`dyr` above are exactly that compare -- `fx >> 4` IS the high byte the ROM reads.
    // This replaced a tile-CENTRE test (`cx = dtx*16+8; fabs(dpx) < 1`) whose comment argued the
    // object should "walk ON to the tile CENTRE so she covers a full 16px tile<->tile". The ROM
    // stops the moment the tile index matches, wherever in the tile that happens; the centre rule
    // was a strictly TIGHTER invention, and it cost real time -- the oracle finishes obj32's first
    // pace leg at loc $082 on frame 41, the port took until frame 66.
    // NOTE the direction/step machinery below is UNCHANGED and still aims at the tile centre: the
    // ROM heads via atan2 (C0/ABA2) at an anchor that is NOT yet derived, so only the arrival test
    // -- the part that IS derived -- moves. With $1C81 bit 0 the arrival does not complete at once: 515F JSR C0/305D
    // steps the object onto the tile's centre-bottom a visit at a time, `5162 BCS ->516A TYX / CLC / RTS` (no re-anim
    // here), and only the aligned object takes 5164's `INX x3 / SEC` -- scenes 1, 8 (Taban), 28, 29, 49, 108 ... set
    // the bit with `0E 01` / `0E 03` on objects that pace with 0xA0.
    double cx = dtx * 16 + 8, cy = dty * 16 + 8;
    double dpx = cx - sc->fx[obj], dpy = cy - sc->fy[obj];
    if (dxr == 0 && dyr == 0) {                      // op 0xA0's arrival (C0/5184): not a C0/568B site
      if ((sc->c1C81[obj] & 1) && mvArriveSnap305D(sc, obj, atx, aty)) return 1;   // 5158-5162 -> 516A
      sc->hasTask[obj] = 0; return 0;
    }
    // AIM BY THE TILE DELTA, like C0/516D's `JSR C0/ABA2` and like EVERY other mvAngle call site
    // here (lines ~1027/1028/1042) -- and like the green --checkdir validator, which proves the ROM
    // chain end-to-end as CtMoveDirAngle(rom, tgtTile - curTile) off the HIGH bytes
    // `r[oa+1] - r[0x1801+idx]` (main.c runCheckDir: 60/60 exact, 0 angle-mismatch).
    //
    // This branch alone used to pass the SIGN of the distance to the tile CENTRE. On a pace along a
    // row -- target tile Y == current tile Y, so the ROM has no Y component at all -- the centre is
    // still 8px from the object's actual sub-tile Y, so the port invented a northward step: loc $082
    // obj32's Y drifts 11248 -> 11136 (= 696 = dty*16+8) and STICKS there, ~7.94px of travel the ROM
    // never performs. Visible directly in RAM via VMDIFF_TRACK=32 --withpos.
    //
    // For a pure-axis move both forms give the same ANGLE (the atan index has fx_=0 regardless of
    // magnitude), which is why --checkdir stayed green while this was wrong; the damage is the
    // spurious second axis, and any DIAGONAL pace was getting a wrong angle outright.
    // NOT ARRIVED: the step is issued by the common path below, which is C0/516D-518D to the instruction: 516D JSR
    // C0/ABA2 (the heading from the tile delta), 5170 JSR C0/5B95 against the facing the object already has (no
    // $F700 -> $1600 on this handler: animLayer = 0) with the +$30 nudge, 517C-5184 `LDA $1000,X / STA $1A01,X /
    // STA $7F0B00,X`, 518A JSR C0/ACFD (the wall bounce included), and no C0/5614. (The arm used to issue here
    // without the solid test and the wall bounce, and capped the ticket to land on the tile's centre -- a stand-in
    // for the $7F0B00 per-tile clamp the common path arms like every other move-to-tile op.)
    (void)dpx; (void)dpy;
  } else if (dxr == 0 && dyr == 0) {               // reached the target tile (0x96/0x94/local)
    // ROM C0/4F90 (0x96) and C0/50CF (0x97) `LDA $1C81,X / BIT #01 / BEQ / JSR C0/305D / BCS` -- BIT 0, not != 0.
    // 0x97 is 0x96 with the two operands read through the scene vars; from 50B9 its bytes are 0x96's (move.c's
    // graded body serves both). The live mirror gated the snap on 0x96 alone, so the attract's 0E6 robots
    // (obj10-15, `97 0F 10` with $1C81 bit 0) finished on arrival and walked their next leg while the cartridge
    // stepped them (-1,+1) px a visit onto the tile's centre-bottom (obj10: c7762, 7766, 7770 ...).
    if ((op == 0x96 || op == 0x97) && (sc->c1C81[obj] & 1)) {
      if (mvArriveSnap305D(sc, obj, atx, aty)) return 1;   // 4F97 / 50D6 JSR C0/305D / BCS -> JSR C0/5614, TYX / CLC
    }
    if (op == 0xA1) {                              // C0/5191 = 0x97 without the anim/facing layer (move.c, graded):
      if ((sc->c1C81[obj] & 1) && mvArriveSnap305D(sc, obj, atx, aty)) return 1;   // 51DF / 51E3 -> 51EE, no 5614
      sc->hasTask[obj] = 0; return 0;              // 51E8 INX x3 / SEC -- and no JSR C0/568B
    }
    sc->hasTask[obj] = 0; mvForceIdle(sc, obj); return 0;   // aligned (or snap off) -> complete: C0/4F9C JSR C0/568B, then the SEC exit
  }
  // op 0x96/0x97 FRESH-move SETUP delay (ROM C0/4F61): the first visit only sets the target + ticket=0; the
  // ROM issues the velocity (C0/ACFD) and starts integrating on the object's NEXT cadence visit, when the
  // ticket reloads. So a freshly-issued move does NOT move on its issue frame. Without this, our movers start
  // ~one cadence cycle early -- verified on the scene-8 sparkles: oracle obj16's op 0x96 runs at f41 but the
  // move only starts ~f45 (ticket 0->3 at f44), while we moved at f41, cascading the sparkle-hide + the
  // "ALL: Oh wow" textbox too fast. hasTask==0 = this is the first visit of THIS move (set 0 on completion/
  // activation); a mid-move re-aim (cycles just hit 0, hasTask still 1) issues immediately, no extra delay.
  // ⚠ THIS SETUP VISIT IS INVENTED. C0/4F61 has now been disassembled in full and there is NO
  // separate first visit: the not-arrived path sets the facing AND arms AND issues, all at once --
  //   4FAB: JSR C0/ABA2            ; heading -> $EE
  //   4FB5: LDA $F700,X / STA $1600,X   ; facing, from the heading  <-- the part we DO reproduce
  //   4FBD: JSR C0/5B95 / (+0x30 sidestep at 4FC2-4FCF)
  //   4FD2: LDA $1000,X / STA $1A01,X / STA $7F0B00,X   ; arm the ticket
  //   4FE0: JSR C0/ACFD            ; ISSUE THE VELOCITY -- same visit
  //   4FE3: JSR C0/5614            ; re-anim (walk)
  //   4FE8: CLC / RTS              ; yield, IP parked (X = Y = the entry IP)
  // Ours instead spends a whole visit setting only the facing (`cycles = 0`), which is why the
  // comment below justifies itself from oracle TIMING (the scene-8 sparkle start, the bed-slide
  // facing leak) rather than from the handler.
  //
  // It is NOT simply removable, and that is the interesting part. It double-counts against the ROM's
  // real SHOW/HIDE/REMOVE visit-yield (C0/4CD9 ends CLC): vmdiff-DEMO8 goes 109 -> 69 with that yield
  // added, and back to 109 with this one removed. But swapping the pair turns
  // camera-reenter-scene5 and field-battle-ambush-generic-dissolve RED, so something ELSE in our
  // movement pipeline is leaning on this extra visit. Per the mandate that is a shared-MECHANISM
  // fault, not a scene fault: find what compensates before deleting this. (See the task notes.)
  if ((op == 0x96 || op == 0x97) && !sc->hasTask[obj]) {
    // The setup visit still SETS the facing to the move direction (ROM C0/5614 re-anim runs at the
    // step ISSUE, not the integration): oracle bed-slide f22 shows obj1 $1602=02 (west) + $1682=01
    // (walk) on this setup visit, velocity ($1900/ticket $1A01) only on the NEXT visit. Without this,
    // the walk cycle (CtSceneActorMoving = hasTask, true here) renders with the STALE facing -- Crono's
    // wake-slide drew a 1-frame "walk NORTH" (his sleep facing) before turning west, leaking below the
    // blanket. Set the SAME facing the integration visit will (mvStartStep setFace, line below).
    vmSetFacing(sc->mem, a, obj, mvTables()[0x400 + mvAngle(dtx - atx, dty - aty)]);
    sc->moveAngle[obj] = mvAngle(dtx - atx, dty - aty);   // store the heading for the integration visits
    // The synthetic YIELD is gone; the facing set above stays, because THAT part is derived
    // (C0/4FB5 `LDA $F700,X / STA $1600,X` runs at issue). C0/4F61 has no setup visit: 4FD2 arms
    // $1A01 and 4FE0 `JSR C0/ACFD` issues the velocity on the SAME visit, then 4FE8 CLC.
    // The blocker recorded here -- "something must take over the first sub-pixel step" -- is
    // answered: our own run_tasks integration loop already runs AFTER the runner inside the same
    // CtSceneTick (ct_vm.c, the "run_tasks: integrate movement" loop), mirroring the ROM frame order
    // C0/00C4 (scheduler) -> C0/00D3 (A810 -> AAFD). It gates on `cycles > 0`, so `cycles = 0` here
    // was PRECISELY what suppressed the first step. Falling through issues a real ticket instead.
    sc->hasTask[obj] = 1;
  }
  // ROM ACFD per-step velocity decision: aim player-ward; if that crosses INTO a wall, try ONE pseudo-random
  // angle (AE27, $FE00 via the global $F8); if THAT also walls, stall this step (AF27). Player-ward is retried
  // FIRST every step. (Object collision +0x30-once, C0/5281, applies to the player-ward heading.)
  // op 0x96 (FIXED tile) RE-AIMS ON EVERY STEP ISSUE, from the TILE delta -- C0/4F61 has no cached
  // heading anywhere:
  //   4F7C LDA $1801,X -> $F0 / 4F81 LDA $1881,X -> $F1     ; the actor's CURRENT tile (position HIGH byte)
  //   4F6D/4F74 the two operands -> $F2/$F3                 ; the target tile
  //   4F86 CMP $F3 / 4F8C CMP $F2 -> equal = arrived (snap/finish), else
  //   4FAB JSR C0/ABA2                                       ; heading from ($F2-$F0, $F3-$F1)
  //   4FD2 LDA $1000,X / STA $1A01,X / 4FE0 JSR C0/ACFD      ; ticket + ISSUE the velocity
  // and that whole block runs on EVERY visit whose ticket has expired. The port used to HOLD the
  // first heading while both axes were short of the target -- an invention its own comment justified
  // from a symptom (a candy-move jitter), and it is what made Taban walk a straight diagonal THROUGH
  // the Telepod: oracle obj10 in scene 8 re-aims its velocity at f2 (26,-17), f6 (22,-22) and f14
  // (28,-14) while the port held (26,-17) and drifted off the ROM's path from frame 6 (--vmdiff
  // --withpos on the user's own savestate).
  int _ang;
  { _ang = mvAngle(dtx - atx, dty - aty); sc->moveAngle[obj] = _ang; }   // 4FAB, every issue
  _ang = animLayer ? npcMoveAngle(sc, obj, _ang) : npcMoveAngleStale(sc, obj, _ang);   // 0xA1 51F4 / 0x9E 5329: the OLD facing
  // ticket = $1000,X (the object's cadence): op-0x87 sets it (moveAnim); the LEAD PC has NO op-0x87 so $1000
  // defaults to 1 (every frame, C0/42D7) -- NOT the 4-frame NPC heuristic. With ticket 4 the mode-0 (AAFD,
  // un-clamped) lead PC re-aims only every 4 frames, flying ~2 tiles per arrival check, so a fast move (the
  // bell minigame's `89 F0` return = ~7px/f) SKIPS the target tile and re-aim ping-pongs forever. Ticket 1
  // (matching the scheduler cadence at the PASS-1 default + the ROM's $1000) re-checks every frame -> lands.
  int _tk = a->moveAnim > 0 ? a->moveAnim : 1;  /* $1000,X cadence; default 1 (C0/42D7), no NPC multiplier */
  if (op == 0x98 || op == 0x99) _tk = B[addr + 2];   // 53F4: the legged pair's ticket is the SCRIPT byte
  mvStartStep(sc, obj, _ang, _tk, animLayer);
  CtVmMoveTrace(sc, obj, op, addr, "issued");
  if (sc->collMap[obj] && sc->tileSolid && mvCrossesSolid(sc, obj)) {
    _ang = mvRandAngle(m);                                                      // AE27-AE2D: the $7E01F8 cursor,
    mvStartStep(sc, obj, _ang, _tk, 0);                                         // 8-bit -> $FE00. setFace=0: the map bounce
    if (mvCrossesSolid(sc, obj)) {                                              // sets VELOCITY ONLY -- ROM C0/ACFD has NO
      sc->cycles[obj] = 0; sc->mx[obj] = 0; sc->my[obj] = 0; sc->hasTask[obj] = 0; mvIssueRegs(sc, obj, animLayer); return 1;   // AF27: zero velocity published, then 5614 -> idle. $1600 write, so the cat keeps its player-ward facing (C0/5290) through the bounce. AF27 stall
    }
  }
  // op 0x94/96/97 ARRIVAL clamp: don't let one step overshoot the TARGET tile. The move stays SMOOTH
  // (b00 = 0, no per-tile pause -- Crono's bed + the fair race are byte-exact) but cycles is capped so
  // the step lands ON the target tile centre instead of skipping past it. Without this a high-speed
  // step (the bell minigame's `89 F0` return = ~7 px/frame) flies through the 1-tile target and the
  // re-aim ping-pongs forever (Crono never finishes, the ball-rise script blocks). A no-op for movers
  // still far from the target (race mid-path): the cap only binds on the final approach step.
  // 0x97 (MOVETOvar, handler C0/5084) shares the move-to-tile arrival core with 0x96 (C0/4F61) --
  // it only resolves the target tile from a variable -- so it must clamp identically. Without it the
  // scene-8 Telepod sparkles (obj15-20: `89 F0` speed + `97` to the right pod) ping-pong forever, so
  // their fn3 never returns, obj0 fn3's CALL+WAIT hangs, and the whole demo (the "Oh wow" text, the
  // sparkle hide via fn4) is stuck -- exactly the reported "sparkles bug at the pod and never vanish".
  // ROM C0/4F61 (and 0x97 C0/5084, 0x94) arm the AA07 per-tile clamp ($7F0B00:=$1000, C0/4FDA) just like
  // chase/0xA0 -- it is NOT chase-only. So a move-to-tile step integrates ONE frame free (may cross one tile),
  // then halts at the next tile boundary (b00 path below): the mover advances tile-by-tile and RESTS on the
  // velocity-landed sub-tile, with NO re-centring (the centre snap is the separate $1C81 path at line 545,
  // default off). Verified byte-exact off the ROM: obj13/Taban rests at tile9+3px, not centre px8. The old
  // `tcx=dtx*16+8` centre-clamp here was an invented stand-in that wrongly snapped the early-aligning axis of
  // a diagonal move to centre (the user-visible "+5px to the right"). The clamp also kills the fast `89 F0`
  // sparkle/race ping-pong byte-exactly (1 tile/step), replacing what the centre-clamp used to do.
  // Move-to-tile (0x94/96/97) per-tile clamp applies to NPCs (Taban, the fair racers, the sparkles -- they need
  // the 1-tile/step halt for byte-exact arrivals + the 89-F0 anti-ping-pong). The LEAD PC's scripted move (Crono's
  // bed-exit) glides SMOOTH through tile boundaries; the b00 clamp there froze him ~13 frames at each tile boundary
  // = the user's "pauses when getting out of bed". Chase (B5/B6) always clamps (no fixed target tile).
  // ROM step-issue ARM of $7F0B00 (the $1A01 ticket reload, positive): op-0x96 C0/4FDA, op-0x97 fresh
  // C0/5071 (script operand) / re-issue C0/5119 ($1000,X), op-0x94 (shared body), chase op-0xB5/0xB6
  // C0/52B0/54E5 -- ALL load $1000,X into BOTH $1A01 and $7F0B00. POSITIVE = AA07 FREE-integrate this
  // issue frame (crosses one tile cleanly) then clears $7F0B00 to 0, so subsequent glide frames CLAMP at
  // the next tile edge. NO obj!=playerObj test -- the lead-PC-vs-NPC split is the per-object $1100
  // MOVE-MODE (sc->mode[], dispatched in run_tasks), NOT who issued the move. op-0x9A (move-to-variable,
  // ROM 4F0A class) arms $7F0B00 = #80 (NEGATIVE = FREE every frame, no clamp) like the other vector ops.
  sc->b00[obj] = (op == 0x94 || op == 0x95 || op == 0x96 || op == 0x97 || op == 0xA0 || op == 0xA1 || op == 0xB5 || op == 0xB6 ||
                  op == 0x9E || op == 0x9F || op == 0x98 || op == 0x99 || op == 0x8F) ? sc->cycles[obj]
               : (op == 0x9A) ? -128 : 0;
  mvIssueRegs(sc, obj, animLayer);                             // the velocity ACFD published, then JSR C0/5614 (not A0/A1/9E/9F)
  sc->hasTask[obj] = 1; return 1;
                                                                 // A0 KEEPS facing (ROM: its
                                                                 // handler has no $F700->$1600,
                                                                 // so Mom/cat keep facing-right
                                                                 // set before the stair descent)
}

// THE CALL OPS' DIRECT-PAGE SCRATCH (D = $0100). Every body that installs or queues a call computes the target's
// $7F0580 slot with the hardware multiplier and leaves it in $E1, and the function's pointer index in $E3:
//   op 02 / 05 (C0/5FB6): the accept arm (new priority $DF < the target's $1C00 = $D9) C0/5FEB-6015 `$E1 = $D9 * $80 + $DB`,
//     `$E3 = $DB << 4`; the defer arm ($DF > $D9) C0/603E-6062 `$E1 = $DF * $80 + $DB`, and `$E3` only when that slot is
//     free (C0/6053 `LDA $7F0580,X / BNE`);
//   op 03 / 06 (C0/607E) and 04 / 07 (C0/611A): the accept arm alone (C0/60B4-60DE, C0/6150-617A); their wait arms
//     (equal or less urgent) write nothing.
// Every body exits first, writing nothing, on `$1C01,X` (op 08's wait), `$1100,X` bit 7 or `$1000,X` bit 7, and on an
// equal priority. $DB = the target * 2. Read from the WRAM the ROM reads. Nothing in the field reads them back, but they
// are RAM: the 2300 A.D. fog's class $7759 takes its wave phase from the word at $01E2 (world.c owBg3WaveTick), so the
// attract's 1F2 starts from what the 0E6 leg's last call left there (cartridge `$01E2 = 03 E0` at f5719, writer C0:6017).
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
// ---- THE LEAD'S STEP TICKET (C0/3040 arms it, any later `STZ $1A01,X` in the same pass drops it) ----------------
// The ROM moves the lead in C0/A810's integrate (AB06 `LDA $1A01,X / BEQ` -> AB10-AB3F `$1800 += $1900`), AFTER the event
// pass: C0/881E computes the step ($1D32/$1D33) before the VM, the lead's own op B0 arms it (3040 `LDA #01 / STA $1A01,X`,
// 9E29 -> 9E53/9E5D copy the velocity into $1900/$1980), and every invoke that later in the same pass installs a function on
// the lead ends `STZ $1A01,X` (op 0x02-0x07 C0/6034 / 60FD / 6199, op D9 32FE, ...) -- the step is gone and A810 does not move
// him that frame. MEASURED on the rule's cartridge (saves/field_re/scene8_pod_latch.state, Up then Left on the Telepod touch,
// `--monitor 7E1A07,7E1906,7E1806 --writer`): at f21 $1906 = F0 by C0:9E56, $1A07 = 00 by C0:619C (obj11's `04 06 36`), and
// $1806 stays 3704 (no C0:AB28) -- the stale F0 sits there until fn7's MOVETO. The port's host (field.c) moves the lead
// before the tick; it hands the step's origin here (CtSceneLeadStepFrom) and the integrate leg takes it back when no
// ticket stands (vmLeadStepAtA810: never armed, or dropped). Transient per-frame state: statics, not CtScene members
// (a member breaks every --loadstate .state).
static int g_leadTk = -1;                  // -1 = the lead's 3040 arm has not run this pass, 1 = armed, 0 = dropped
static int g_leadFromOk, g_leadFromTick; static double g_leadFromX, g_leadFromY;
static int gVmTick;                        // trace timestamps (and the step origin's frame)
static void vmTicketStop(CtScene* sc, int o) {   // `STZ $1A80,X / STZ $1A01,X` on another object's op
  sc->hasTask[o] = 0; sc->cycles[o] = 0;
  if (o == sc->playerObj && g_leadTk == 1) g_leadTk = 0;
}
void CtSceneLeadStepFrom(CtScene* sc, double x, double y) {
  (void)sc; g_leadFromOk = 1; g_leadFromTick = gVmTick; g_leadFromX = x; g_leadFromY = y;   // this frame's, the tick to come
}
static int g_leadTakenTick = -1;           // the tick that took the lead's step back
int CtSceneLeadStepDropped(const CtScene* sc) { (void)sc; return g_leadTakenTick == gVmTick; }
// C0/A810 for the lead's host step, at the integrate leg (CtSceneTick, around the party trail).
//   unarmed  (-1): the lead's op B0 did not run this pass -- he sat on B2 (C0/3543 `TYX / CLC`) or another op, or the
//                  queue build left him out (C0/59F0: his countdown $1001 not expired) -- so $1A01 is 0 and AB06's BEQ
//                  skips him. Taken back BEFORE the party trail: with no 9E29 there is no ring push either.
//   dropped   (0): an invoke after his B0 zeroed $1A01 (vmTicketStop). Taken back AFTER the trail: 9E29 pushed.
static void vmLeadStepAtA810(CtScene* sc, int dropped) {
  if (sc->playerObj < 0 || sc->playerObj >= sc->nobj) return;
  if (g_leadTk != (dropped ? 0 : -1) || !g_leadFromOk || g_leadFromTick != gVmTick - 1) return;
  CtSceneSetActorPosF(sc, sc->playerObj, g_leadFromX, g_leadFromY); g_leadTakenTick = gVmTick; g_leadFromOk = 0;
}
static void reqOp(CtScene* sc, int target, int function, int priority) {
  if (target >= sc->nobj || function >= 16 || priority >= 8) return;
  // ROM C0/5FC8-5FD0: refuse if $1100 bit7 (inactive) or $1000 bit7 (cadence paused).
  // Approach scripts (loc 112) 0x0C-unpause battle-enemy slots then 0x02-REQ their fn3; without
  // the pause gate a paused slot still accepted REQs, but more importantly a still-paused target
  // would never get cadence ticks on hardware.
  // $1100 source: sc->mode[] (cold create syncs it from the load-op moveMode at 1709/applyEffect;
  // the wram seed overwrites it byte-exact from the oracle -- actors[].moveMode is NOT re-seeded,
  // so it must not be read here or seeded vmdiffs refuse REQs the oracle accepts, DEMO8 f109->f35).
  if (r8(sc->mem, 0x7E1C01 + (uint32_t)target * 2)) return;   // C0/5FC3 `LDA $1C01,X / BNE ->6039` (op 0x08's wait)
  if (sc->mode[target] & 0x80) return;
  if (sc->actors[target].cadPause) return;
  CtSS* ts = &sc->st[target];
  if (ts->prio == priority) return;
  // DEFER: target busy at a HIGHER prio (lower number) -> stash the REQ at ITS priority slot, so the
  // RETURN scan (op 0x00, p > prio) resumes it once the higher fn finishes. Guard the slot being WRITTEN
  // (ret[priority]), NOT ret[ts->prio]: the latter holds the fn that ts->prio PREEMPTED (non-zero whenever
  // the current fn is itself a preempt, e.g. the touch's fn2), which wrongly DROPPED the deferred REQ --
  // so Marle's post-bump fn5 (`02 06 55` from fn2, prio 5) never ran and she stood still instead of
  // pacing "looking for the pendant".
  if (ts->addr >= 0 && ts->prio < priority) { if (ts->ret[priority] == 0) ts->ret[priority] = fpPtr(sc, target, function); return; }
  if (ts->addr >= 0) ts->ret[ts->prio] = ssPushAddr(ts);          // C0/6005 / 60CC / 6168: $1180 (a waiter's 04 byte)
  ts->addr = fpPtr(sc, target, function); ts->prio = priority;
  // A PREEMPTING FUNCTION DROPS THE TARGET'S IN-FLIGHT MOVE. All three install arms end the same
  // four stores, in this order: `STA $1180,X / STA $1C00,X / STZ $1A80,X / STZ $1A01,X` --
  // op 0x02 C0/6027-6034, op 0x03 C0/60F0-60FD, op 0x04 C0/618C-6199 (ops 0x05/0x06/0x07 BRL into
  // the same bodies after translating the party index). Only the PREEMPT arm reaches them: the
  // same-prio `BEQ` and the less-urgent `BCS` return above without touching either byte, which is
  // why these two lines sit after the early returns and not at the top.
  // We had ported the pair everywhere else ($1A80/$1A01 -> hasTask/cycles at ct_vm.c:2367 and
  // :4432, and in move.c:1259 / :1351 / :1419) but not here, so a CALL that preempted a live
  // MOVETO left hasTask=1. The callee's first move visit then SKIPPED the fresh-move block, reused
  // the previous move's heading and facing, and -- with a pure-axis heading pointing away from the
  // new target -- never landed an axis: scene 19's obj25 walked right off the map, obj24's
  // `04 32 34` CALL never completed, and its `D8 00 00` BATTLE three ops later never ran.
  vmTicketStop(sc, target);                                        // C0/6031+6034 / 60FA+60FD / 6196+6199
  // do NOT reset ts->dc: the ROM
  // REQ/CALL leaves $1001 alone, so the callee first runs on its EXISTING cadence phase (the queue is
  // already built this frame), not next frame -- the callee's one-cadence-cycle defer. (There is no
  // `delay` timer: the old `delay = priority*priority` was a dead heuristic, written here but never read --
  // the 0x94/96/97 move-step takes its ticket from the $1000 cadence (moveAnim), not a separate counter.)
}


// The SCENE-ENTRY sweep, C0/B0E6 -> C0/B127. NOT the per-frame pass, and it must not be unified
// with it: the ROM has two boxes and they differ on three terms. C0/B127 does
//   $C3 = $1D0A - 4, clamped to 0 on a NEGATIVE result   (B12E-B138, same as A88D)
//   $C4 = $1D0C + 4, with NO 0xFF clamp                  (B13A-B140; A88D clamps)
//   $C5 = $1D0E,     with NO -2 at all                   (B142-B149; A88D subtracts 2)
//   $C6 = $1D10 + 5, with no clamp                       (B14B-B151; A88D clamps)
// and its caller admits CLASS 8, which the per-frame loop skips outright (B0F8 tests only #07,
// where A822/A826 test both) -- so a class-8 object can hold a slot from scene entry until the
// next C0/7155. Preserve that asymmetry; do not unify the two filters.
// On a PASS (B16D-B18F) the ROM makes the per-frame pass's rising edge (A8EA-A90B) for that object: `$0E80 / $0E81 /
// $0F00 := 80`, `$1681 / $1601 / $1B00 := 0`, `$0F01 := FF`, C0/7170 (the palette) and C0/6F9A (the OBJ slot); back in
// the loop (B101-B117) C0/AB45 (its screen position), C0/A9CD (the draw list), `STZ $79`, C0/CAD9 until it pops it, C0/B1B2
// (the upload) and C0/EA42 (the arena run); and the sweep ends `BRL C0/B271`, the draw pass. On a FAIL nothing (B190 CLC).
// With the legs off (legsOn) the port keeps the latch and the palette only, as the per-frame pass does. (Until 2026-10-13
// the six stores were left out on purpose, from before the legs were modelled.)
// WHERE THIS IS CALLED FROM, and it took two wrong attempts (objpal_allocator.md sec 8): the ROM
// runs the scene LOAD first (C0/56B0, whose C0/5709 script-init runner PLACES every object), and
// only then the location ENTRY (C0/286C), which sets the camera (0A50 -> 74A6 -> 7506/7553) and
// THEN sweeps (2881). So this needs BOTH the camera and the placed objects. It cannot live at the
// end of CtSceneCreate (the camera cells are still zero from memInit, which collapses the box to
// [0,4)x[0,5) and rejects everything -- scene 6's GATO loses its slot) nor at the end of
// CtSceneCameraEntry (the LEAD is not placed yet: our lead position otherwise reaches the VM only
// inside the per-frame loop). It is called from field.c's fresh-entry branch, after both.
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
    if (cls == 7) continue;                                      // B0FA -- class 8 IS admitted
    int vis = r8(m, X + 0x1A81);
    if (vis == 0 || (vis & 0x80)) continue;                      // B12A / B12C
    // B155 `LDA $1801,X / ASL A` and B161 `LDA $1881,X / ASL A`: the TILE BYTES, not a position. An object
    // the script has not placed yet carries the load fill's $FF here (C0/571A) while the host's float is
    // still 0, so the float admitted it -- scene 8's obj21 (`82 56 / AA 00 / 00`, placed only by its fn3)
    // claimed a slot at the load, the table filled, and the demo's sparkles found none (oracle: obj21's
    // slot comes at its `8B 06 05 / 90`, the sparkles take slot 4). The bytes are the ROM's own test.
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
// THE MENU'S RELOAD RUNS C0/286C WHOLE, and its object legs after the camera (C0/0A50) and the OBJ slots (C0/6F79, the
// host's fieldPassObjSlots) are `2875 JSR C0/7155` (the eight palette owners $0B80-0B87 := 80), `2878 JSR C0/E935` (the
// arena owners $0BC0-0BC7), `287B JSR C0/B204` (the whole draw-list page $0E00-0EFF := 80), `287E JSR C0/A7E9` (one
// (00, FF) pair per object from $0F00) and `2881 JSR C0/B0E6` -- the sweep relinks every object in the camera's box in
// INDEX order, with no party tail. The C0/01A5 MVN had put back the list from before the menu (the lead at its band's
// head, where the party tail left him) and the port kept it until the object pass relinked: the fade-in layered by the
// stale list (field_draw_list_order.md sec. 4).
void CtSceneMenuReloadObjects(CtScene* sc) {
  if (!sc) return;
  uint8_t* r = sc->mem->s;
  memset(r + 0x0B80, 0x80, 8);                                   // C0/7155
  CtObjSceneInit(r, sc->nobj);                                   // C0/6F79 (again: the host's) / E935 / B204 / A7E9
  palSweepB0E6(sc);                                              // C0/B0E6
}

static int ctWipeOp(CtScene* sc, int sub);
// THE POLL'S VERDICT (C0/61B4-61D6, the comment in step() below): 1 = keep waiting on `tgt` for a call made at priority
// `pr`, 0 = released. One body for the parked poll and for a 04/07 met with the latch already set.
static int ssCallBusy(const CtScene* sc, int tgt, int pr) {
  if (tgt < 0 || tgt >= sc->nobj) return 0;        // no such object: $1100,X reads 80 (unloaded), 61B7 BMI -> released
  const CtSS* t = &sc->st[tgt];
  return !sc->actors[tgt].dead && t->addr >= 0 && t->prio <= pr;
}
static int step(CtScene* sc, int obj) {            // 1 = yield, 0 = continue
  CtSS* s = &sc->st[obj]; const uint8_t* B = sc->body; int blen = sc->blen, addr = s->addr;
  if (ssParked(s)) {                               // 0x04/0x07, IP on the call: wait for the callee's function
    // ROM wait-poll (C0/61A7): WAIT while the callee's CURRENT priority ($1C00) is <= the called
    // priority (61D2 `CMP $D9` / 61D6 `BCS keep-waiting` -- i.e. requested_prio >= callee_prio);
    // RELEASE only when the callee climbs ABOVE it (returned PAST the called function). The old
    // exact-equality (== waitPrio) released the instant the callee was at any OTHER priority -- so
    // when the called fn was DEFERRED behind a still-running LOWER-numbered (= higher) priority
    // (reqOp line 610: callee busy at prio < requested -> fn stashed as ret[requested]), the
    // caller saw callee.prio != waitPrio and released IMMEDIATELY, never waiting for the deferred
    // call to actually run+finish. (Scene-8 Telepod: obj14's `04 28 46` calls obj20 fn6 -- the
    // sparkle's ~180f convergence-to-gate mover -- while obj20 is still looping fn5 at prio 3; the
    // early release revealed Marle-inside-the-gate ~195 frames too soon. Verified vs the oracle:
    // it parks at this CALL f592..f772.) Also release if the callee was deactivated (0x0A/0x0B).
    if (ssCallBusy(sc, s->waitObj, s->waitPrio)) return 1;
    s->waitObj = -1;             // RELEASE: the visit goes on (ROM SEC/continue)...
    // ...BUT THE POLL IS ITSELF ONE OF THE VISIT'S FIVE DISPATCHES. The parked IP is the 04's own byte (61A4 `TYX / CLC`),
    // so the visit that finds the call complete dispatches the 04 again, and its completion exit 61E0 `INX x3 / SEC`
    // spends one unit of the budget (5AB6 `DEC $67`) like any SEC op -- the next op is dispatch 2, not dispatch 1.
    // MEASURED on the combat tutorial (saves/field_re/scene254_line_c960.state, --monitor 7E1180:2): obj12 returns at
    // f7, obj0's next visit f11 runs `04 18 11` + `71 06` + `02 1A 21` + `4F 03 08` + `02 18 11` and stops on the `FE`
    // (budget spent), which runs at f15. Released for free, the port fitted the `FE` into f11 -- the yellow line four
    // frames early. (Not the `return 1` reverted below: that ended the visit, a whole cadence late.)
    return 0;                    // 61E0 SEC: the completed poll is this visit's first dispatch
    // NOTE: a `return 1` here (commit 7c6771d) to force the scene-8 sparkle "8f stagger" was REVERTED -- it
    // added a visit to EVERY CALL+WAIT, giving Crono's scene-2 bed-exit move 2 spurious pauses/slowdowns. The
    // real 2-cadence stagger comes from the CALLEE's cadence-phase defer (the PASS 1 eligibility snapshot),
    // NOT the caller's release; forcing +1 here was a hack that regressed every other scene. The scene-8
    // sparkle phase being one cadence early is a separate dc-phase issue (deferred), not the caller's job.
  }
  if (addr < 0 || addr >= blen) { s->addr = -1; return 1; }
  uint8_t op = B[addr]; int ln = opLen(B, addr);
  if (obj >= 0 && obj < 64) sc->atB0[obj] = (op == 0xB0 || op == 0xAF);   // both run C0/3002 (0xAF: C0/2FFD)
  if (obj >= 0 && obj < 64 && (op == 0xB0 || op == 0xAF)) g_ranB0[obj] = 1;
  { dbgFlags(); if (gEnvTraceAll && (obj == gEnvTraceAllObj || gEnvTraceAllObj < 0))   // DEBUG: trace ops (incl branches); <0 = all objs
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
    // RETURN to IDLE clears the per-object MOVE state. A finished function leaves no active move; if
    // we keep hasTask/cycles around, the object's NEXT function (e.g. re-running the scene-8 demo by
    // stepping on the pod again) skips the op-0x96 fresh-move setup-delay (it gates on hasTask==0) and
    // re-arms the wrong step -> the 2nd demo run desynced (user report; 1st run starts from a clean
    // scene-load). A mid-function interrupt (REQ resumes a lower prio) does NOT go idle, so a move that
    // is meant to resume after the interrupt is preserved.
    // RETURN-to-idle also reclaims the LEAD PC's ANIMATION. In the ROM the player object, once it is
    // no longer running a script (prio 7), is animated by the player movement controller (C0/8833),
    // which drives $1680/$1780 back to the idle/walk set (anim 0). A scripted function that left the
    // player on a one-shot animeset -- the Telepod DEMO sets Crono to anim 36 (the teleport-spin) /
    // anim 29 (a reaction pose) via op 0xAA -- must NOT linger after the demo: without this, Crono
    // talking to Marle (obj3 idle, facing UP) showed anim 29's up-frame (assembly 71, a crouched/bent
    // pose) instead of the standing idle-up (anim 0, up-frame 3). NPCs keep their script animeset at
    // rest (a sitting/cooking NPC's 0xAA pose), so this reclaim is for the PCs (move-mode 0/1/2) only:
    // the LEAD via the player controller C0/8833, the FOLLOWERS via their op-0xB0 idle-keeper (ROM
    // C0/3002 drives anim 0 for mode 0/1/2, just yields for mode 3+). Ours' followers RETURN to idle
    // (ip FFFF, conga-driven) instead of parking on their op-0xB0, so the scene-439 candy-shop Marle
    // kept her scripted WALK anim (animMode 1) while standing at the stand until this covers her too
    // (oracle marle_candy_shop_animation.state: obj3 $1680=0 idle at the stand). A looping animeset
    // (mode 1, op 0xAA/0xB4) is the transient case a scripted walk/demo leaves behind; a SHAPE freeze
    // (mode 3, op 0xAC) is a DELIBERATE held pose (bell-minigame bob, bed scenes) the ROM keeps, so
    // don't clear it. mode 2 (animerepeat) already self-reverts to 0 on completion.
    // ⭐ ...BUT ONLY WHILE CONTROL IS ACTIVE. This reclaim stands in for a routine that is not a
    // RETURN-time event at all: in the ROM the lead's $1680 is stamped EVERY FRAME by C0/9E84's
    // tails, and 9E84 is reached only from op 0xB0 (C0/3002 -> 3040 -> 9E29), i.e. only once the
    // object's script has gone idle onto its perpetual control op. A script that is still RUNNING
    // -- a cutscene, a minigame -- never reaches it, so whatever animeset that script last set
    // HOLDS. field.c already states the same rule on the render side ('the ROM's rule is purely
    // $011F ... while locked it does NOT run, so the script's last-set anim mode/frame HOLDS') and
    // gates its free-roam anim-0 stamp on it; this arm did not, and cleared the mirror on the
    // RETURN two bytes after the op that set it.
    // MEASURED (user report 2026-09-07, the fair's soda minigame: 'crono should switch to the
    // drinking animation but did not switch'). Their F5 capture, loc 439 with $011F = 0: obj1 carries
    // $1780 = 01 / $1680 = 3C -- the ROM bytes op 0xAA wrote -- while the C mirror read
    // animMode = 0, so the renderer drew assembly frame 2 (standing) instead of anim 0x3C. The
    // script is `00FB AA 3C / 00FD 00`: arm the drink animeset, RETURN on the very next byte.
    // ⭐ ...AND IT HAS TO CLEAR THE BYTES, NOT THE MIRROR. ctAnimSyncActor rebuilds this actor's anim
    // state FROM WRAM on every sync -- `am == 1` takes `a->animIndex = $1680,X` straight back out of
    // the byte -- so a clear that only touched the C side could not survive the next one. Measured on
    // the user's F5 of the Telepod (2026-09-10, "crono has the wrong sprite ... he is crouched and
    // should be standing"): obj3's fn8 ends `AA 1D / AD 30 / E3 01 / 0E 00 / 00`, the reclaim DID
    // fire on that RETURN (trace: f459 am=0 ai=0) and control did open (f460 ctlLocked=0) -- and then
    // the crouch came BACK the frame Marle's fn1 re-locked and called fn9 (f468 am=1 ai=29, the SAME
    // agen: not a new arm, the old byte resurfacing). fn9 is `89 20 / 96 06 09 / AD 02 / 0F / 00` and
    // has no animeset op of its own. $1680 still read 1D, and the oracle's own scene-8 RAM reads 00.
    // ⭐ AND THE ROUTINE IS OP 0xAE's, NOT THE IDLE ARM'S. The first cut of this stamped C0/5667's
    // three stores ($1680/$1681/$1601) and left `$1780` alone. That fixed the pose and broke the WALK
    // (user, same day: "crono moved to the current position without animation. he walked completely
    // idle"), because C0/5667 is the IDLE arm INSIDE the auto-selector C0/5614 and is only ever
    // reached with `$1780 == 0`: 5616 `LDA $1780,X / BEQ ->5630` sends modes 1 and 2 straight to
    // 561F RTS -- "the SCRIPT owns the anim" -- so leaving the mode at 1 meant the selector never
    // ran and never chose WALK for the scripted step.
    // MEASURED IN THE ORACLE, obj3 walking to the pod from scene8_gate_cutscene.state
    // (`--monitor "7E1686:1,7E1786:1,7E1906:1,7E1986:1,7E1687:1" --diff`):
    //     f0   $1680=01 $1780=02            f16  $1680=00 $1780=01
    //     f23  $1680=01 $1780=00  vel=F1    phase 01 -> 02 (f30) -> 03 (f37) -> 04 (f44)
    //     f46  $1680=00 vel=00              phase 00      -- arrival
    // i.e. through a scripted walk the ROM sits in MODE 0 with animeset 1 and the phase advancing.
    // So what the reclaim owes is the ROM's own return-to-auto, which is op 0xAE's handler C0/2E8F
    // falling into the shared animeset tail at C0/2E74 -- five stores, mode included:
    //     2E93 STA $1680,X (A=0)   2E74 STA $1780,X (A=0)   2E77 STZ $1601,X
    //     2E7C STA $7F0B01,X (0)   2E80 STZ $1681,X
    // With `$1780 = 0` the already-ported selector (C0/5614 at the 13 move-op sites, C0/568B at the
    // seven exits) picks WALK while the step runs and IDLE on arrival, which is both reports at once.
    // ⭐ ...AND ONLY WHEN THE RETURN REACHES THE CONTROL OP. The RETURN itself (C0/5F74-5FB5) touches nothing but the
    // return slots ($7F0580) and the level ($1C00): when a lower level holds a saved ip, the object goes on running THAT
    // function, and its animeset holds until the script itself reaches its 0xB0/0xAF. So the reclaim stands in for 3002
    // only where the port's object goes idle (the follower with no B0 of its own) or resumes ON a 0xB0/0xAF. MEASURED
    // (user F5 2026-10-09, the tent of horrors' hostage, "she changes a little after rising"): the switch's `05 02 28`
    // runs the hostage's fn8 (`9C C0 70 / 60 70 00 12 / 00`, priority 2) over her fn7 descent loop (priority 3,
    // `AA 09` the hanging pose) while Bekkler's `E3 01` holds $011F = 1; on the rule's cartridge $1684 / $1784 stay
    // 09 / 01 through fn8's RETURN and fn7 goes on at 00D9 -- the port's RETURN put her on anim 0, standing.
    int retTo = -1;
    if (s->prio != 7) for (int p = s->prio + 1; p < 8; p++) if (s->ret[p] != 0) { retTo = s->ret[p]; break; }
    int reachesCtl = retTo < 0 || (retTo < blen && (B[retTo] == 0xB0 || B[retTo] == 0xAF));
    if (reachesCtl && r8(sc->mem, 0x7E011F) != 0 && sc->mode[obj] <= 2 && sc->actors[obj].animMode == 1) {   // PCs = move-mode 0/1/2 (lead + followers)
      sc->actors[obj].animMode = 0; sc->actors[obj].animIndex = 0; sc->actors[obj].animGen++;
      uint32_t AX = 0x7E0000u + (uint32_t)obj * 2;
      w8(sc->mem, AX + 0x1680, 0x00);                    // C0/2E93  STA $1680,X with A = 0
      w8(sc->mem, AX + 0x1780, 0x00);                    // C0/2E74  STA $1780,X -- back to AUTO
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
    // saved ip (5F97-5FAA), and it returns with X = that ip and CARRY SET (5FAC-5FB2) -- so the runner's loop
    // (5AB4 BCC / 5AB6 DEC $67 / BNE 5AA3) KEEPS EXECUTING at the restored ip within this visit's budget.
    // Only the idle object's RETURN (5F7B `CMP #07 / BEQ 5FB3`: TYX / CLC) yields. MEASURED on Lucca in
    // scene 120: the player walking into her at (37,11) touches her every other frame (prio 2, fn2 = a bare
    // RETURN); each RETURN restores slot 7 = her idle continuation 0A68 and runs on through her poll loop
    // (18/18/16/12/11, five ops a visit: the frame-end ip reads 0AF6 at f207, 0AA2 at f209) until the mirror
    // reads row 11 and C1 55 posts the box at f211. Yielding here left that loop unvisited between touches
    // (a prio-7 object is not scheduled on its own), so 'Lucca does not trigger when leaving the staircase' (defect #13).
    return 0;                                      // 5FB1 SEC: the runner keeps going
  }
  if (op == 0xB0 || op == 0xB2 || op == 0xAF) {  // forever-yield: park. (An op-0xE0 staged earlier no longer
                                                  // commits here -- the canonical commit is the $0119 fade
                                                  // drain in CtSceneTick; the object parks ON the E0 op itself.)
    // THE TOUCH PREEMPT (ROM C0/3002, disasm/notes/field_touch_preempt.md sec 3). op 0xB0 is not just
    // a park: it is where a walked-into object gets yanked out of its own script. A pacing NPC's
    // `A0 / A0 / 11 07` is a CLOSED loop -- op 0x11 is a backward jump -- so it NEVER falls through to
    // its touch function. The only way out is this preempt.
    //
    //   3002: LDA $38 / BEQ ->3009 / TYX / CLC / RTS      ; global early-out while $0138 != 0
    //   300F: LDA $1100,X / BEQ ->3040 (lead) / DEC BEQ ->3030 / DEC BEQ ->3020 (followers)
    //   3040: LDA #01 / STA $1A01,X / JSR C0/9E29         ; lead body
    //   304C: LDA $1600,X / JSR C0/5B8D                   ; carry set iff $01EB bit7 clear
    //   3052: BCC ->3057
    //   3054: JSR C0/3154                                 ; <-- the ONLY touch invoke in the ROM
    //
    // ONLY the state-0 (lead PC) branch carries the probe -- C0/3054 is C0/3154's sole caller in the
    // whole ROM, so a follower or an NPC running op 0xB0 fires nothing. `$01EB` is the hit latch: the
    // player controller (C0/881E) resets it to 0x80 = "nothing" every frame and the proximity scan
    // C0/9923 stores the hit actor's index into it, and the main loop runs the player (00A7 JSR $881E)
    // BEFORE the event-VM (00C4 JSR $5A46) -- so the VM consumes an index produced earlier the SAME
    // frame. The host supplies it via CtSceneSetTouchHit; -1 = the 0x80 "nothing" state.
    // ...AND THE LEAD'S ANIMATION, WHICH THE PAD LOCK DOES NOT STOP. 3040's `JSR C0/9E29` opens with `JSR C0/9E84`, the
    // facing/anim arm, on every visit of the parked lead -- locked or not. Free, the host's controller runs that arm (field.c
    // CtPlayerAnimSelect after C0/881E); LOCKED it did not run at all, and a lead a cutscene had left on a looping animeset
    // kept cycling it. The ROM: C0/881E still runs every frame and zeroes the intent ($1D2C/$1D2D, 882F/8831 with D = $1D00;
    // C0/88EE, the d-pad, is skipped while $011F = 0), so 9E84's index is 0 -> slot 0 -> the idle chain C0/A1E9, whose
    // `LDA $1F / BEQ ->A211` puts $1680 back to 0 (with $1681 and $1601). MEASURED at the 600 A.D. arrival (the Telepod warp,
    // rule oracle): obj2's `96 08 0B / A6 00` ends the fall at c1112, its script parks on its fn0's `B0`, and at c1113
    // $1684 = 24 -> 00 by C0:A21F while $011F = 00 -- the lead stands facing the gate. The port went on cycling anim 36.
    // (C0/9E29's tail -- the trail push on a nonzero $1D32/$1D33 and 3040's `STA $1A01,X` -- is not part of this.)
    // ...AND OP 0xAF IS THE SAME BODY. Its handler C0/2FFD is `JSR C0/3002 / INX / RTS` (the op table at C0/5D6E:
    // 0xAF -> 2FFD, 0xB0 -> 3002): everything 3002 does -- the lead's 9E29, the followers' A26B / A2CE, the touch
    // probe -- runs for 0xAF too, and the INX then steps the ip past it (the yield below). A script that loops on
    // it under a condition is a PC's control op with an exit: scene 129's Lucca, fn0 `12 0C 00 00 04 / AF / 10 02 /
    // B1 / 11 0A` -- she follows while her loc[0C] reads 0 (user F5 2026-10-03, "lucca does not follow crono").
    // ...AND THE D-PAD'S GATE HAS TWO TERMS. C0/8833 `LDA $0162 / BNE ->8840 / LDA $011F / BEQ ->8840 / JSR C0/88EE`:
    // a decision box ($0162 = 1..3, C0/2024-202F) skips the d-pad exactly as $011F = 0 does, the intent stays zeroed and
    // the lead's visit takes slot 0 -> A1E9, whose `LDA $62 / CMP #02 / CPY $97` arm (A1E9-A1F5 -> A1D2) puts the
    // lead's $1680 = 19, the decision pose. The host's controller does not run under a decision box (field.c, the
    // movement gate's `!(tbActive && isChoice)`), so this arm is the lead's 9E84 then too. MEASURED (the user's F5
    // ct_debug_010, loc 00D's `Do you know about our establishment?`; the rule's cartridge, --teleport 13 19 27 3
    // from saves/field_re/npcface_lake.state, walk right, A): $011F = 01, $0162 = 02, $1682 = 19, $0F03 = A8 -- the
    // port kept $1682 = 00 and drew the walk's last frame 02.
    if (op != 0xB2 && gRomImg && gRomImgLen >= 0x10000
        && r8(sc->mem, 0x7E0138) == 0 && (r8(sc->mem, 0x7E0162) != 0 || r8(sc->mem, 0x7E011F) == 0)
        && CtSceneActorMode((const CtScene*)sc, obj) == 0) {
      w8(sc->mem, 0x7E1D2C, 0); w8(sc->mem, 0x7E1D2D, 0);                   // C0/882F / 8831 STZ $2C / $2D
      uint32_t X = (uint32_t)obj * 2;
      uint8_t was = sc->mem->s[0x1680 + X], wasF = sc->mem->s[0x1681 + X];
      CtPlayerAnimSelect(sc->mem->s, gRomImg, (int)gRomImgLen, (int)X);     // C0/3047 JSR C0/9E29 -> JSR C0/9E84
      if (sc->mem->s[0x1680 + X] != was || sc->mem->s[0x1681 + X] != wasF) {   // the C mirrors follow the bytes
        ctAnimSyncActor(&sc->actors[obj], sc->mem->s, (int)X); sc->actors[obj].animGen++;
      }
    }
    if (op != 0xB2 && r8(sc->mem, 0x7E0138) == 0 && CtSceneActorMode((const CtScene*)sc, obj) == 0 && obj == sc->playerObj)
      g_leadTk = 1;                                          // 3040 LDA #01 / STA $1A01,X: the lead's step armed
    if (op != 0xB2 && sc->touchHit >= 0 && sc->touchHit < sc->nobj
        && r8(sc->mem, 0x7E0138) == 0                      // C0/3002's $0138 early-out
        && CtSceneActorMode((const CtScene*)sc, obj) == 0) { // state 0 = the LEAD PC only
      int hit = sc->touchHit;
      sc->touchHit = -1;                                     // one shot: the ROM's latch is re-armed
      CtSceneTouch(sc, hit);                                 //   to 0x80 by the next frame's C0/881E
    }
    if (op == 0xAF) s->addr = nxt;                           // C0/2FFD's INX
    return 1;
  }
  // op 0xB1 ADVANCES the IP then YIELDS the visit (ROM handler ends CLC).
  if (op == 0xB1) { s->addr = nxt; return 1; }
  // The shared bodies with no C mirror (vmSharedOp above): their IP and their carry, as the ROM's handler leaves them.
  { CtHRet sh;
    if (vmSharedOp(sc->mem, B, sc->header, addr, obj, &sh)) { s->addr = sh.ip - sc->header; return sh.carry ? 0 : 1; }
    if (op == 0x0A || op == 0x0B || op == 0x0C) {
      vmObjEnableOp(sc->mem, sc->actors, sc->nobj, sc->mode, B, sc->header, addr, obj, &sh);
      s->addr = sh.ip - sc->header; return sh.carry ? 0 : 1;
    } }

  // ---- THE SIX SOUND OPCODES (disasm/notes/audio_commands.md §6) -------------------------
  // A ROM-wide scan for `JSL $C7:0004` plus every $2140-$2143 access in bank $C0 proves this is
  // the COMPLETE set: $E8/$EA/$EB/$EC stage the 4-byte mailbox at $1E00-$1E03 and JSL the sound
  // service; $ED/$EE poll an APU out-port. Each is transcribed from its handler below.
  //
  // ALL SIX END `CLC` -- they yield the visit, exactly like $AF/$B1. Only $E8 did so here
  // before; $EA/$EB/$EC/$ED/$EE fell through to the generic effect tail and KEPT RUNNING. That
  // was a scheduler divergence independent of whether any sound is emitted, and it is the one
  // thing on this front that can move --vmdiff. The OPLEN table already matched the ROM
  // ($E8=2, $EA=2, $EB=3, $EC=4, $ED=1, $EE=1), so no length changes.
  if (op == 0xE8 || (op >= 0xEA && op <= 0xEE && op != 0xE9)) {
    switch (op) {
      case 0xE8: case 0xEA: case 0xEB: case 0xEC:  // the four STAGING ops -> the shared implementation
        { int rr = vmSoundStage(sc->mem, sc->blen, B, addr, op, sc->rasterLine);   //   (also reached from the init pass, runFunc)
          // THE SYNCHRONOUS UPLOAD (song_load_freeze.md section 6): the JSL returned after `rr>>10` frame
          // boundaries, on raster line `rr & 1023`. The runner loop (CtSceneTick pass 2) reads these after
          // this visit: it suspends the tick for the frames lost and cuts the queue at the raster gate.
          if (rr >= 0) { sc->sndPosted = 1; sc->sndFreezeFrames += rr >> 10; sc->rasterLine = rr & 1023; sc->sndOpAddr = addr; } }
        break;
      case 0xED:                                  // C0/3CBC -- wait until $2141 == 0
        if (vmSoundPort(1) != 0) return 1;        // condition unmet: the handler does NOT INX,
        break;                                    //   so the op re-executes next visit
      case 0xEE:                                  // C0/3CC5 -- wait until ($2143 & $0F) == 0
        if ((vmSoundPort(3) & 0x0F) != 0) return 1;
        break;
    }
    s->addr = nxt; return 1;
  }
  // The pad branches 0x30/0x31/0x34-0x39/0x3B/0x3C/0x3F-0x44 (C0/66B2..6782) and 0x47 (C0/6788) run move.c's
  // CtVmPadBranchFamily through vmSharedOp above: the held pair $00F2 (34=A 35=B 36=X 37=Y 38=L 39=R), the dash byte
  // $00F8 (30 bit 1, 31 bit 7), and the edge latches $0150 (3B/3C) / $0151 (3F-44, the same masks as 34-39), which
  // C0/0097 `LDA $00F0 / TSB $51` accumulates every frame and only a taken branch's TRB clears. Not taken = jump by
  // the operand from its own address (66D0). This runner had 0x34-0x39 / 0x3F-0x44 / 0x47 as paraphrases and no arm
  // for 0x30/0x31/0x3B/0x3C (scene 7's `42` Y-wait and the bell game's `34 03` A-bob are the same bodies now).
  if (op == 0xAD || op == 0xB9 || op == 0xBA || op == 0xBC || op == 0xBD) {
    // wait ops (ROM C0/2F96): the per-actor counter $7F0A01 gains +1 per VISIT while
    // below N; when it reaches N the op consumes, resets it and the visit continues.
    int N = (op == 0xAD) ? B[addr + 1] : (op == 0xB9) ? 4 : (op == 0xBA) ? 8
          : (op == 0xBC) ? 16 : 32;
    if (s->wait >= N) { s->wait = 0; s->addr = nxt; return 0; }
    s->wait++;
    return 1;
  }
  // Ops 0x08 / 0x09, C0/6240 `LDX $6D / LDA #01 / STA $1C01,X / TYX / INX / SEC / RTS` and C0/624B
  // (`LDA #00`, same tail): the only two writers of $1C01,X in the image -- the invoke prologues'
  // FIRST refusal (5AC7 / 3156 `LDA $1C01,X / BNE`). CtVmInvokeGate already tested the byte; nothing
  // wrote it, so it was structurally zero until now.
  if (op == 0x08) { w8(sc->mem, 0x7E1C01 + (uint32_t)obj * 2, 1); s->addr = nxt; return 0; }
  if (op == 0x09) { w8(sc->mem, 0x7E1C01 + (uint32_t)obj * 2, 0); s->addr = nxt; return 0; }
  if (op == 0xBB || op == 0xC0 || op == 0xC1 || op == 0xC2 || op == 0xC3 || op == 0xC4) {
    // C0/357A `LDA $29 / BNE ->35B2` comes BEFORE the request: a box already up parks a FRESH op too (TYX /
    // CLC / RTS, no request, no latch). The request used to run first here, so an NPC invoked while another
    // object's box was ramping re-wrote $0129 = 1 over state 5 -- scene 120's Lucca chain, a press every
    // 16 frames: the first box never finished and the change-location never came.
    if (getenv("CT_TBOPDBG")) {
      // A DECISION op also carries the CURSOR MACHINE: $0162 (0 none / 1 requested / 2 cursor
      // live / 3 confirmed), $0163 the cursor's LINE, $0164/$0165 the first/last option line,
      // $0166 the confirmed line the completion arm stores as the script's `result`. Appended
      // only for 0xC0/0xC3/0xC4, so a plain box's line keeps its shape.
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
    // ...then C0/3580 `LDX $6D / LDA $7F0A00,X / BNE ->35A7`: the object's OWN latch decides between the
    // REQUEST (latch clear: set it, post the box, park) and the COMPLETION (latch set: clear it, advance).
    // The port keyed this on a C flag (`s->tb`) that no seed carries, so a state captured mid-box requested
    // the same box AGAIN when it closed (the scene-254 vmdiff checks lost their unpark: ours=12 ip-steps).
    if (r8(sc->mem, 0x7F0A00 + (uint32_t)obj * 2) == 0) {
                  sc->textbox = 1; sc->tbIndex = B[addr + 1]; sc->tbTableAtShow = sc->tbTable;
                  sc->tbChoice = (op == 0xC0 || op == 0xC3 || op == 0xC4); sc->tbObj = obj;
                  // The decision ops (0xC0/0xC3/0xC4, len 3) carry the OPTION-LINE config in operand2 (ROM
                  // C0/367C-3693: $64 = (op2>>2)&3 = first option line, $65 = op2&3 = last, within the option
                  // page). CT indents BOTH continuation AND option lines with spaces, so the renderer can't
                  // tell them apart by leading space -- it needs this to place the cursor on the real options.
                  sc->tbOpt = sc->tbChoice ? B[addr + 2] : -1;
                  // ...and the DECISION op's request writes the cursor machine's three bytes, which the
                  // port never had: C0/3683-3697 `LDA $7F2001,X / STA $D9 / AND #03 / STA $65 /
                  // LDA $D9 / LSR / LSR / AND #03 / STA $64 / LDA #01 / STA $62`.
                  //   $0165 = LAST option line = operand2 & 3
                  //   $0164 = FIRST option line = (operand2 >> 2) & 3
                  //   $0162 = 1 = "a decision is up, cursor not yet armed"
                  // $0162 is the machine the whole input frame turns on while a decision box is open
                  // (C0/1927, C0/201C) -- see the dossier section in disasm/notes/textbox.md.
                  if (sc->tbChoice) {
                    w8(sc->mem, 0x7E0165, B[addr + 2] & 3);            // 3689-368B
                    w8(sc->mem, 0x7E0164, (B[addr + 2] >> 2) & 3);     // 368D-3693
                    w8(sc->mem, 0x7E0162, 1);                          // 3695-3697
                  }
                  sc->tbOp = op; s->tb = 1;
                  w8(sc->mem, 0x7E0129, 1);        // C0/3626 LDA #01 / STA $29 -- the box is UP
                  // ...and the rest of the REQUEST, C0/3586-35A2 (op 0xBB; the five siblings differ only in
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
    // advance arm only once it has cleared $0129 -- which happens at the END of the close ramp.
    if (r8(sc->mem, 0x7E0129) != 0) return 1;                   // C0/3606: park while the box machine is busy
    // C0/365D-3669 -- the DECISION completion arm, and the 0xC3/0xC4 twins C0/36C6-36D2 and
    // C0/36FA-3706, are all `LDA #00 / STA $7F0A00,X / LDA $66 / STA $7F0A80,X / STZ $62`. The RESULT
    // is $0166, the LINE the cursor was confirmed on (written by the cursor arm C0/1AFE), not a
    // host-side 0-based index -- and the arm CLEARS $0162, releasing the input frame.
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
    // (TYX / INX / LDA $7F2001,X / STA $2B / ... STA $2C / ... STA $2D, D = $0100). sc->tbTable is the
    // host's file-address view of the same bytes; keeping only the C field lost the table on every WRAM
    // seed (a --loadwram talk fixture opened EMPTY boxes that any press dismissed -- scene 7's obj13,
    // 2026-09-03), and op 0xBB's own precondition is C0/3570 `LDA $2D / BNE`: no bank byte, no box.
    w8(sc->mem, 0x7E012B, B[addr + 1]); w8(sc->mem, 0x7E012C, B[addr + 2]); w8(sc->mem, 0x7E012D, B[addr + 3]);
    sc->tbTable = (B[addr + 1] | (B[addr + 2] << 8) | (B[addr + 3] << 16)) - 0xC00000; s->addr = nxt; return 0;
  }
  if (op == 0xFF) {                                  // screen/effect sub-commands (ROM C0/3FAF jumptable).
    // ⭐ THE OPERAND SPLITS THE OPCODE IN TWO AT $90 (C0/3FB7 `CMP #90 / BCS ->4000`). Only the >= $90
    // half is the small screen-effect jump table below. The < $90 half is a DIFFERENT MACHINE: C0/3FBB
    // ramps $19 (the INIDISP brightness shadow) to black one step per frame, C0/3FD3 quiesces
    // (JSR C0/0B4E), C0/3FD6 RESETS THE STACK (`LDX #06FF / TXS`) and then C0/3FE0 `JML $C3:0000`
    // with the operand in A -- i.e. it LEAVES THE FIELD ENGINE ENTIRELY and never returns to this
    // script. $C3:0000 stores A at $0384, LZ-decompresses $C3:1313 (34175 bytes) to $7E:3000 and
    // `JML $7E:3000`; that overlay dispatches on `$0384 & 1F` through its own table at $7E:3043, and
    // entry 2 = $7E:5978 = THE TITLE SCREEN. When a sequence ends the overlay runs `LDX #06FF / TXS /
    // JML $C0:0000` (7E:303B) -- the same reboot the menu shell exits through -- so the field engine
    // re-enters at C0/0066 with $0100 unchanged and the scene's script runs again from the top.
    // (Measured on the title: $0384 = 2 while it is up, and $7E:5BB7 `LDA #01 / STA $7F0060` is what
    // START leaves behind -- which is exactly the flag scene 433's loop at 0x000F branches on.)
    // So here the visit is PARKED and the request handed to the host: this script never resumes.
    // ...and $80-$8F is NOT that overlay. C0/3FDC `LDA $DF / BMI ->3FE4` splits the SAME faded,
    // stack-reset path in two: `< $80` JMLs to $C3:0000 (above), `>= $80` JMLs through
    // $FD:FFF1 / FFEB / FFE8 / FFE5 into FD/DA00 / DA5F / DABE / DB1D -- the TIME-TRAVEL effect,
    // which ends by rebooting into the $0100 the preceding DD/DE staged. Both halves park the
    // visit here; the host runs them as the two different machines they are, and the operand is
    // what tells them apart (disasm/notes/gate_warp_fd_family.md).
    // THE FADE BEFORE THE JUMP is the op's own, and its first pass runs HERE, in the op's frame: `3FBB LDA $19 / BEQ
    // ->3FD3 / BMI ->3FD3` (black already: jump now), else `3FC1 DEC $19 / JSR C0/881E / JSL FD/FFF7 / JSR C0/EC60`
    // and `3FCD LDA $19 / CMP #01 / BPL ->3FC1` -- one DEC a frame until $19 is 0, the jump on the frame after the one
    // that reached it. Nothing else of the field runs meanwhile (the scheduler is inside this visit), so an op met on
    // a C0/2848 ramp frame leaves the ramp where its INC put it. MEASURED (logical oracle, the attract's hub 1B1): the
    // ramp's INC to 0D and `DE`/`FF 61` in one frame (f3294, $0119 = 0C at its end), 0B..00 over f3295-3306, `JML
    // $C3:0000` at f3307; `FF 61` with $0119 = 0F before 0E6 (f5256) jumps at f5271. Only the first visit of the op
    // takes the DEC: a parked warp is re-read every tick.
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
      w8(sc->mem, 0x7E013D, B[addr + 4]); w8(sc->mem, 0x7E013C, 0);             // OPENS at centre ($3A, $3B) to radius $3D
      w8(sc->mem, 0x7E0139, 1); sc->irisState = 1;   // (C0/21E1 steps it, ctIrisFrame)
    } else if (B[addr + 1] == 0x91) {                 // C0/4055: LDA #02 / STA $39 -- it CLOSES from wherever $3C is
      w8(sc->mem, 0x7E0139, 2); sc->irisState = 2;
    } else if (B[addr + 1] == 0x97) {                 // C0/4191: 0x90's stores (4191-41A8), then `LDA #04 / STA $39`: the
      w8(sc->mem, 0x7E013A, B[addr + 2]); w8(sc->mem, 0x7E013B, B[addr + 3]);   // circle GROWS as the colour window
      w8(sc->mem, 0x7E013D, B[addr + 4]); w8(sc->mem, 0x7E013C, 0);             // (C0/232A, ctColCircleFrame)
      w8(sc->mem, 0x7E0139, 4);
    } else if (B[addr + 1] >= 0x98 && B[addr + 1] <= 0x9C) {
      // C0/41B0 / 41B4 / 41B8 / 41C0 / 41C4 -- ONE tail, five immediates: `LDA #05|#07|#09|#0B|#0E / BRA ->41BA`,
      // 41BA `STA $39 / TYX / INX / INX / RTS` (the table C0/400E, subs 98-9C). They hand C0/21E1 an entry -- 9C's 0E
      // is C0/23E2, the teardown of the line entry 6 draws (the combat tutorial's `FF 9C` after each `FE`). Two bytes;
      // the CLC at 400C below ends the visit as for every sub.
      static const uint8_t k39[5] = { 0x05, 0x07, 0x09, 0x0B, 0x0E };
      w8(sc->mem, 0x7E0139, k39[B[addr + 1] - 0x98]);
    } else if (B[addr + 1] == 0x96) {
      // C0/4181, the table's sub 6: `LDA $4210 / BPL ->4181` (the vblank-flag spin, NMI on), `JSR C0/0B4E` (SEI,
      // force blank, NMI and HDMA off), `LDX #06FF / TXS`, `JML FD/C000` -- the RESET vector's own target ($00:FF00
      // `SEI / CLC / XCE / JML FD:C000`), so the game reboots cold, as the console's reset button does. It never
      // returns to the runner: no CLC at 400C, nothing after it in this visit or this scene. One site in the 504
      // scripts: scene 0AC's obj9 fn1 at 0x0416, on the branch taken when `48 EC 01 7F 19` read a non-zero $7F01EC (the
      // byte C1:8143 sets when the party loses). The visit parks on the op and the host performs the spin and the cold
      // init at the vblank, as for 0xDF/0xE1 (field.c, 'THE DF/E1 REBOOT' and 'THE COLD INIT').
      sc->rebootReq = 3;
      return 1;                                     // park: s->addr stays on the op
    }
    s->addr = nxt;
    // ...AND IT ENDS THE VISIT. C0/3FAF's portable half is `4000 ... 4009 JSR ($400E,X) / 400C CLC /
    // 400D RTS`, and that CLC is UNCONDITIONAL -- whatever the sub-handler did, the runner's
    // `5AB4 BCC` closes the object's visit right there, so the ops after it wait for the NEXT frame.
    // We used to return 0 (keep spending the visit's 5-op budget), which ran obj0's autorun two ops
    // ahead of the ROM for the rest of scene 254 and fired its next CALL four frames early -- the
    // f435 divergence of the tutorial lockstep. (The per-sub IP deltas are already right: opLen gives
    // 5 for 0x90/0x97 and 2 otherwise, matching C0/4036's four INXs and C0/41BA's two. The parking
    // subs 0x92-0x95 run above (ctWipeOp). NOT PORTED here: 0x9E (C0/41C8, scene 0AC: `LDA $39 / BEQ` -> `LDA #0C /
    // STA $39` and park; parks until C0/21E1's entry 0C ends in 0D) -- entries 7-0D have no port (docs/ROADMAP.md
    // F19), so parking on it would hold the scene for good; 0x9B sets 0B, likewise unported. src/move.c's CtFieldRunnerOne has the whole ladder byte-derived and
    // graded by --sweepops FF:60.)
    return 1;
  }
  if (op == 0xC8) {                                 // Special-Dialog (ROM C0/3757). The operand lands in
    int arg = B[addr + 1];                          // $0125 and the LAUNCHER DISPATCH C0/0CF2-0D18 turns
    // it into a menu MODE (measured with --hookpc: `C8 41` reaches C0/0D00, `C8 C0` reaches C0/0D09):
    //   0CF4  BEQ ->0D04        operand $00        -> mode 5   (BRL C0/19C7)
    //   0CF6  BMI ->0D09        operand >= $80     -> mode = (operand ROL x3) & 3  -- $C0+id = 3
    //   0CF8  BIT #01 / BNE     operand ODD        -> C0/0D00 `LDA #00` = MODE 0, THE LOADER
    //   0CFC  LDA #06           operand EVEN       -> mode 6 (screen $0B = the SAVE POINT's save: its substate
    //                                                  table C2/D4F0 reuses the Save screen's CF61/CF92; `C8 40`)
    //   (and a >= $80 operand below $C0 is mode 2 = screen $0C, THE SHOP -- see the arm further down)
    if (arg >= 0xC0 && arg <= 0xC7) {               // mode 3 -> screen $0D: the ATB selector, then the
      if (!s->nm) { sc->naming = 1; sc->nameChar = arg - 0xC0; s->nm = 1; return 1; }  // NAME-ENTRY screen
      if (sc->naming) return 1;                     // still on the naming screen -> keep yielding
      s->nm = 0; s->addr = nxt; return 0;           // name entered (field cleared sc->naming) -> resume
    }
    // ⭐ MODE 0 = the LOAD / NEW-GAME shell (C2/8000 -> screen $0E). Scene 0's 0x0032 and 0x00CE are
    // the ONLY two mode-0 sites in all 504 shipped scenes (scanned), and they are the game's own cold
    // entry. It BLOCKS the script exactly like naming does -- and in the ROM it never resumes at all,
    // because both of the shell's exits end `JML $C0:0000`, a reboot. The host clears sc->loader when
    // it has performed that reboot; the resume arm is here so a host that declines simply steps over.
    if (arg && arg < 0x80 && (arg & 1)) {
      if (!s->nm) { sc->loader = 1; sc->loaderArg = arg; s->nm = 1; return 1; }
      if (sc->loader) return 1;
      s->nm = 0; s->addr = nxt; return 0;
    }
    // ⭐ MODE 2 = THE SHOP (screen $0C; C2/808F[2]). C0/0D09: X = operand & $3F -> $0A01 = the shop number, which the
    // screen's init C2/DACA indexes the list table $CC:2C6D with. C8 80..97 in the shipped scripts, one scene each. The
    // op is C0/3757's handshake: the first visit stores the operand in $0125 and arms $0117 bit6 (park, CLC); the main
    // loop fades (C0/0CCE) and launches; on the menu's return C0/0D33-0D39 clears $0117 bit6 and sets $0118 bit6, and
    // the next visit consumes that (`3774 TRB $18`), steps over the operand and CONTINUES (SEC). Parked here exactly as
    // the naming screen is; the host clears sc->shop when the shop has closed.
    if (arg >= 0x80 && arg < 0xC0) {
      if (!s->nm) { sc->shop = 1; sc->shopArg = arg; s->nm = 1; return 1; }
      if (sc->shop) return 1;
      s->nm = 0; s->addr = nxt; return 0;
    }
    // ⭐ MODE 6 = THE SAVE POINT'S SAVE (screen $0B; C2/808F[6]). C0/0CF8 `BIT #01 / BNE` not taken, C0/0CFC `LDA #06`:
    // an even operand, `C8 40` in the save point object every one of the 35 locations with one runs (loc 084 obj9:
    // `12 06 03 .. / 12 07 27 .. / 16 CF 80 86 0A / 31 06 / 65 03 F3 / C8 40 / ..` -- on the save tile, with $7F01CF bit
    // 7 already up, the pad's $00F8 bit 7 runs it). The same C0/3757 handshake as the shop's, so it parks in the shop's
    // fields (CtSceneShop; the host tells the two apart by the operand's bit 7, CtSceneSavePoint). MEASURED, the rule's
    // cartridge (a loc 084 save tile, A held): `$0125 = 40`, `$0117 = 40` at f38, $0119 0E .. 00 over f39-53, `$0A00 =
    // 0606` at f54, `$0067 = 0B` at f69; B at f170, the RTL at f179, `$0117 = 00` / `$0118 = 40` at f211. (Mode 5,
    // operand $00, is not ported: skipped.)
    // ⭐ MODE 5 = THE PARTY EXCHANGE (operand $00: C0/0CF4 `BEQ ->0D04`, `LDA #05 / BRL C0/19C7` -- the Y button's own
    // `LDX #0000 / ... / JSL C2/8000`, so screen $07 with $71 = 0, and the Y path's return C0/19D2, whose `$0117 bit6 ->
    // $0118 bit6` is this op's handshake). Parked in the shop's fields with operand 0 (CtSceneExchange); the host clears
    // sc->shop when the screen has closed. Sites: scenes 08D, 113, 127 and 1D0 (twelve).
    if (!(arg & 1) && arg < 0x80) {
      if (!s->nm) { sc->shop = 1; sc->shopArg = arg; s->nm = 1; return 1; }
      if (sc->shop) return 1;
      s->nm = 0; s->addr = nxt; return 0;
    }
    s->addr = nxt; return 0;
  }
  if (op == 0xC7 || op == 0xC9 || op == 0xCA || op == 0xCB ||
      op == 0xCC || op == 0xCD || op == 0xCE) {
    // ---- THE $C1 ITEM + GOLD FAMILY -- C0/3780 (0xC9 HAS-ITEM), C0/37B1 (0xCA ADD-ITEM),
    // C0/37C7 (0xC7 ADD-ITEM, id from the LOCALS), C0/37E3 (0xCB REMOVE-ITEM), C0/3827 (0xCC
    // HAS-GOLD), C0/385B (0xCD ADD-GOLD), C0/3874 (0xCE SUB-GOLD). All seven share one shape:
    //   TYX / INX / TDC / XBA / LDA $7F2001,X / INX / STX $C7 / TAY / LDA #leaf / JSL $C1:8003
    // -- i.e. stage the operand in Y, put a LEAF INDEX in A, and call the gate. The gate and its
    // six leaves are ALREADY IN THIS TREE, byte-derived and graded against ROM micro-execution by
    // --sweepops: move.c's ctVmC1Gate. So this arm CALLS it instead of transcribing it a second
    // time -- the tree already carries one field-VM opcode implementation too many
    // (disasm/notes/vm_two_implementations.md), and the item family is the first slice of that
    // delegation. `mem->s` is the flat 128 KB image the gate indexes ($7E:2400 == r[0x2400]),
    // which is the same buffer CtFieldRunnerOne hands it.
    //
    // Carry is SET on every exit of all seven (3799/37AF/37C5/37F7/...), so every arm CONTINUES
    // the visit -- `return 0` unconditionally. The lengths, the operand widths and the two branch
    // arms all live in the shared body; see the note on its definition in move.c.
    int ni = addr;
    CtVmItemFamily(sc->mem->s, B, addr, &ni);
    g_ctVmItemOps++;              // liveness counter for CT_BAGDUMP: an inventory/gold line that
    s->addr = ni; return 0;       // never ran a family op is a VACUOUS pin, not a passing one
  }
  if (op >= 0x4A && op <= 0x4D) {
    // ---- THE POKE FAMILY -- C0/67D7 (0x4A byte), C0/67FB (0x4B word), C0/680C (0x4C local byte), C0/6829 (0x4D
    // local word), all through the 24-bit pointer C0/67E3 stages at $01D0-$01D2. Delegated to move.c's
    // CtVmPokeFamily, the body --sweepops grades. This runner had 0x4A/0x4B as paraphrases and NO arm for 0x4C/0x4D,
    // which it stepped over: the fair's `4C 00 02 7E 1B` (Silver Points -> $7E0200, oracle f11 writer C0:6824)
    // never ran, and the {num} its next box prints read nothing. SEC on every exit: the visit continues.
    // The IP is the ROM's, as for 0x88 below: 0x4C/0x4D spill it into $01D9 (`6813 STX $D9`), and it counts from
    // $7F:2001, pointer table included.
    int ni = addr + sc->header;
    CtVmPokeFamily(sc->mem->s, B - sc->header, addr + sc->header, &ni);
    s->addr = ni - sc->header; return 0;
  }
  if (op == 0x7A) {
    // ---- OP 0x7A, C0/4D06 -- THE JUMP TOWARD A TILE. move.c's CtVmOp7A is the body --sweepops grades; this runner had
    // no arm for it, so the combat tutorial's imps (loc 0FE: `7A 04 03 02 / 71 09` x5, obj0 polling `loc[9] == 5`) never
    // left their places and the scene's end waited on them forever. Its two mirrors are this runner's mover pair:
    // $1A80,X = hasTask (the 4D08 armed test) and $1A01,X = cycles (the 4D0D ticket). Armed and running -> park (4D12
    // CLC); armed and spent -> `STZ $1A80 / STZ $1B80`, ip + 4, SEC (4D15-4D20); not armed -> the arm on the WRAM bytes
    // ($1A00 the speed, $1801/$1881 the tile, the position the commit wrote) and park (4E21 LDX $C7 / CLC). The motion
    // is C0/AAC7's, in the integration pass (the $1B80 arm there).
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
    // ---- THE SCREEN-EFFECT OPS -- C0/3D07 (0xF1: COLDATA $0121/$0122, CGADSUB $0BE0, the $0118 request) and C0/3F26
    // (0xFE: the eight 8.8 channels $7F:1CE9.., their deltas through the divider, the countdown $7F:1CE8, $0139 = 6).
    // Delegated to move.c's CtVmScreenFxOp, the bodies --sweepops grades. This runner walked over both by their length
    // (0xFE only yielded): the combat tutorial (loc 0FE) drew no line. 0xFE exits CLC (the visit ends, 3FAD); 0xF1 SEC
    // on its two-operand arms, CLC on the transition arms (3D41).
    CtHRet h;
    CtVmScreenFxOp(sc->mem->s, B - sc->header, addr + sc->header, &h);
    s->addr = h.ip - sc->header; return h.carry ? 0 : 1;
  }
  // ---- op 0x47 (C0/6788): `INY / TYX / LDA $7F2001,X / STA $6B / BRA 6784` (INY / TYX / SEC / RTS) -- the anim drain's
  // raster deadline ($016B, the byte C0/CA99 compares the V counter with) from the operand; two bytes, the visit goes on.
  if (op == 0x48 || op == 0x49 || op == 0x5B || op == 0x5E || op == 0x5F || op == 0x60 || op == 0x61 ||
      op == 0x63 || op == 0x64 || op == 0x67 || op == 0x69 || op == 0x6B || op == 0x6F) {
    // ---- THE POINTER READ (C0/6792 / 67C7) and THE LOCAL ARITHMETIC (C0/6A1F..6C68) -- move.c's CtVmReadPtrFamily /
    // CtVmLocalArithFamily, the bodies --sweepops grades. Before: 0x48 a byte paraphrase and no 0x49 (77 sites, the
    // `49 05 01 7E nn` arrival gates among them), 0x63/0x64 through `1 << (b & 7)` instead of the $FF20/$FF28 tables,
    // 0x6F a plain shift where the ROM's is a do-while on $01D9, and NO arm at all for 0x5E/0x60/0x61/0x6B (the
    // fair's obj0 fn0 `5E 15 22` / `61 15 20`). All SEC: the visit continues. IP from $7F:2001, as for the grid.
    int ni = addr + sc->header;
    if (op == 0x48 || op == 0x49) CtVmReadPtrFamily(sc->mem->s, B - sc->header, addr + sc->header, &ni);
    else CtVmLocalArithFamily(sc->mem->s, gRomImg, (int)gRomImgLen, B - sc->header, addr + sc->header, &ni);
    s->addr = ni - sc->header; return 0;
  }
  if ((op >= 0x50 && op <= 0x56) || op == 0x58 || op == 0x59) {
    // ---- THE VARIABLE-ASSIGNMENT GRID -- C0/68C6 (0x50 imm word->local), 68E9/6913 (0x51/0x52 local->local),
    // 693D/6964 (0x53/0x54 global->local), 698B (0x55 storyline->local), 69A6 (0x56 imm->global), 69C4/69EB
    // (0x58/0x59 local->global), 8 and 16 bits. Delegated to move.c's CtVmVarFamily, the body --sweepops grades
    // (it also spills $01D9 as the ROM does). 0x52/0x54/0x59 had NO arm here before (stepped over), and 0x50 was a
    // big-endian paraphrase; the VM's word model is little-endian now, so one body serves the whole grid.
    int ni = addr + sc->header;
    CtVmVarFamily(sc->mem->s, B - sc->header, addr + sc->header, &ni);
    s->addr = ni - sc->header; return 0;
  }
  if (op == 0x2E) {
    // ---- C0/4A4E, THE COLOUR-EFFECT RECORD -- delegated to move.c's CtVmColorSlotOp, the body --sweepops grades.
    // This runner had NO arm for it: the six bytes were skipped and no record was armed, so scene 122's
    // `2E 56 10 70 0F 08` (Marle's vanish in Guardia Castle 600, a MODE 5 record of sub-mode 6 over colours
    // $10..$7F) never tinted the room -- 7219dcb ported the sub-mode into FD/E5ED's service, which the live VM
    // then never reached. Same IP convention and carry as op 0x88 below.
    CtHRet cs; cs.ip = nxt + sc->header; cs.carry = 1; cs.ok = 0;
    CtVmColorSlotOp(sc->mem->s, B - sc->header, addr + sc->header, &cs);
    s->addr = cs.ip - sc->header;
    return cs.carry ? 0 : 1;
  }
  if (op == 0x88) {
    // ---- C0/4892, THE PER-OBJECT COLOUR EFFECT -- delegated to move.c's CtVmColorFx, the body --sweepops grades.
    // This arm used to keep only C mirrors (colorFx, the rotation, the blob copy) and ARMED NO RECORD: the 682
    // `88 4x` (fade to white, FD/E82C) and `88 5x` (scale, FD/E5A4) sites in the scene scripts did nothing, so the
    // attract's Flea (0AD, `88 40 1F F0 02` at f1569, measured: C0:4985 writes $052C = 40, FD:E863 walks $0533
    // FF -> F7 -> .., FD:E92D whitens $7E:2322..) materialised in her own colours instead of white. The record
    // now lands in $0520 and the VM pass ticks it (CtObjSlotService, src/objslot.c -- the battle's own body).
    // The C mirrors stay, applyEffect below: the renderer still draws the rotation (modes 2/3, FD/E532, not ported
    // into the page) from palRot. CARRY: the two allocating arms exit CLC when the eight slots are taken
    // (C0/491E / C0/4975) -- the op is consumed and the visit ends, which this runner spells `return 1`.
    // THE IP IS THE ROM'S: `4892 STY $C7` spills it into $01C7/$01C8, and the ROM's IP counts from $7F:2001, i.e.
    // it includes the pointer table (sc->header bytes) the body here starts past. Handing the body offset spilled
    // 05AB where the cartridge has 08CB on the Manoria nuns (vmdiff-manoria-nuns, header 0x320) -- so the shared
    // body gets `raw + 1` and `addr + header`, and the IP it returns comes back through the same offset.
    CtHRet cf; cf.ip = nxt + sc->header; cf.carry = 1; cf.ok = 0;
    CtVmColorFx(sc->mem->s, gRomImg, (int)gRomImgLen, B - sc->header, addr + sc->header, obj * 2, &cf);
    applyEffect(sc, obj, addr, op);
    s->addr = cf.ip - sc->header;
    return cf.carry ? 0 : 1;
  }
  if (op == 0xCF || op == 0xD0 || op == 0xD1 || op == 0xD3 || op == 0xD4 || op == 0xD7 ||
      op == 0xF8 || op == 0xF9 || op == 0xFA) {
    // ---- THE $C2 PARTY / ROSTER family's seven opcodes that had NO ARM IN THIS RUNNER AT ALL --
    // C0/388D (0xCF has-character), C0/38BE (0xD0 add to the RESERVE roster), C0/38D3 (0xD1 remove
    // from it), C0/37F9 (0xD7 count an item into a local), C0/3E5C/3E6E/3E75 (0xF8/0xF9/0xFA heal
    // the party). 153 static sites over the 504 shipped scenes, every one of them until now an
    // opcode this VM stepped over: recruiting did nothing, "is this character with us?" always fell
    // through to the present branch (the same defect 66f2e43 found for op 0xC9 has-item, and with
    // the OPPOSITE polarity -- 0xCF jumps when the leaf returns NONZERO), and an inn left HP and MP
    // untouched.
    //
    // Delegated, not transcribed: move.c's CtVmPartyGateFamily is the body --sweepops micro-executes
    // against the ROM at 60 cases per opcode. `gRomImg` may be NULL in a dat-only Belthasar run and
    // that is handled the same way move.c handles it -- every ROM read inside the gate is already
    // bounds-checked against romLen, so a missing image yields the routine's own zero-fill arm
    // rather than a special case invented here.
    //
    // ⚠ 0xD2 is deliberately NOT routed here yet. It has an arm in this runner (below), hand-derived down to the carry.
    //
    // ⭐ 0xD3 (C0/3919, JOIN) IS routed here now. Its old arm was a subset of leaf 1 (C2/8C79): it took the first empty
    // active slot and stopped -- no `JSR C2/8CF7` first (the joiner's RESERVE entry marked empty, `| 80`), no eviction
    // into the reserve when the three slots are full (8C88-8CCC), and none of C0/392F-3949 ($78, the object into
    // $0199 / $019B). MEASURED, the rule's cartridge after Crono's death (loc 1A9, saves/field_re/pre_scene_19f with A every
    // 40 frames from f6000): `D3 01` -> $2985 = 81 by C2:8D01 and $2982 = 01 by C2:8C9E at f9395; the port left $2985 = 01,
    // and the exchange screen that follows (C8 00) listed Marle twice, in the party and in the reserve.
    //
    // ⭐ 0xD4 (C0/394E, LEAVE) IS routed here now. Its old arm scanned the MEMBERSHIP array $7E:2980
    // for the char id, on the stated assumption that it agrees with the party OBJECT slots on emptiness.
    // C0/394E compares `$1101,X` for X = $0197/$0199/$019B -- the objects -- and the carry that decides
    // the visit is the last `CMP $D9` that ran. The two disagree whenever the party is on the roster but
    // not on the field: the attract's hub (loc 433, `D4 05 / D4 02 / D4 01 / D4 04` after its song) has
    // $0197/$0199/$019B = 80 80 80 and $2980 = 00 01 02 (logical oracle, f0-f47). There `3998 BMI ->39B1`
    // skips every compare, the carry stays the entry 0, and each D4 ENDS its visit -- the cartridge runs
    // them at f35, f39, f43, f47 and the `E0` at f51. The arm matched Lucca and Marle in $2980, compared
    // EQUAL, and ran three D4s in one visit: the `E0` came 8 ticks early and the hub leg was 8 frames short.
    CtHRet pg; pg.ip = addr; pg.carry = 1; pg.ok = 0;
    CtVmPartyGateFamily(sc->mem->s, gRomImg, (int)gRomImgLen, B, addr, &pg);
    g_ctVmPartyOps++;
    // ⚠ THIS TRACE EXISTS BECAUSE OF A REAL DEFECT IT WOULD HAVE CAUGHT. When the shared body was first
    // lifted out of move.c's switch it kept move.c's own operand reads -- `r[0x12001 + ip]`, the ROM's
    // $7F:2001 -- which are correct there (that runner passes `script = r + 0x12001`) and wrong here,
    // where the body is host memory and `addr` is body-relative. --sweepops could not see it: it grades
    // the caller for whom the two are the same pointer. Nor could the heal check, because 0xF8/0xF9/0xFA
    // read NO operand and were the only arms a headless drive could reach. Printing the OPERAND and the
    // resulting IP is what makes a wrong source visible. Deduped, like CT_SLOTTRACE.
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
    // 0x05-0x07 (C0/61E9 / 61FE / 6214) are 0x02-0x04 with the target read from the party-slot
    // table: `LDA $7F2001,X (the slot byte) / TAX / LDA $97,X` = $0197 + slot, the object HOSTING
    // that member (an X offset, obj * 2), then `BRL` into the parent's body. An empty slot (`BMI`,
    // 0x80) skips the op: ip + 3, SEC -- the visit goes on. The objects are not 1 + slot / 2: a
    // party of Crono and Lucca hosts Lucca in obj3 ($0199 = 06), and the 600 A.D. inn's bedroom
    // (loc 0x75, obj0 `07 02 13`) woke the empty Marle object instead of her (move.c's graded
    // runner already reads it this way).
    // ⭐ A 04/07 MET WITH THE LATCH ALREADY SET POLLS, ON ITS OWN OPERANDS -- it installs nothing. The latch $7F0980,X is
    // one byte per object (s->waitObj >= 0), and the op reads it before anything else: C0/6109 `LDX $6D / LDA $7F0980,X /
    // BEQ ->611A (install) / BRL ->61A7`, and 0x07's C0/6214 `BNE ->6232` -> 6236 `LDA $7F2001,X / TAX / LDA $97,X` (no
    // empty-slot test) -> `BRL 61AF`. The poll (61A7-61E8) takes the target and the priority from the op in hand, waits
    // (61E6 `TYX / CLC`, the IP on the op) while that priority >= the target's $1C00, and on release clears the latch
    // (61D8-61DC) and steps past the op (61E0 `INX x3 / SEC`). The latch is still set here when a higher-priority
    // function pre-empted a parked waiter (C0/6168, 5AC5, 3154 push the 04's byte and leave $7F0980 alone) and this op
    // belongs to the new function. MEASURED (the rule's cartridge, saves/scene8_duas_animacoes.state, the Telepod, Up
    // held then A on f22): the touch's fn2 of obj11 parks on `04 06 36` (Crono's fn6, the raised arms, priority 3); A
    // pre-empts it with fn1 (the same code, priority 1), whose own `04 06 36` polls -- obj11 waits f26-f43 while the
    // first fn6 plays and goes on at f46. The port installed the call again: Crono raised his arms a second time at f47
    // (the user, 2026-10-19, translated from Portuguese: "sometimes Crono raises his arms, lowers them and raises them
    // again"). disasm/notes/call_wait_latch_preempt.md sec. 4.
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
    reqScratch(sc, op, tgt, pr);                                     // (Recovered from a 2026-09-20 stash.)
    // ⭐ 0x03/0x04 (and 0x06/0x07, which BRL into their bodies) RETRY UNTIL THE TARGET TAKES THE CALL; only 0x02/0x05
    // queue or drop it (reqOp). Both bodies refuse the same way, C0/6086-60B2 and C0/6122-614E:
    //   LDA $1C01,X / BNE      -> 6106 / 61A4  `TYX / CLC`  the IP stays ON the op and the visit ends
    //   LDA $1100,X / BMI      -> 6100 / 6114  `INX x3 / SEC`  skipped (an inactive target)
    //   LDA $1000,X / BMI      -> the same skip (a paused cadence)
    //   new prio CMP $1C00,X / BEQ / BCS  -> 6106 / 61A4: equal or LESS urgent than what the target runs now = wait
    // and op 0x02 (C0/5FB6) is the one that skips an equal priority (5FE7 BEQ ->603A) or queues a lesser one in
    // $7F0580 (5FE9 BCS ->603E). MEASURED, the rule's cartridge after the Naga-ette fight (loc 081): obj15's `02 30 33`
    // starts the new Naga-ette's (obj24) fn3 at priority 3 -- appear, approach, strike -- and calls Lucca's fn 0x0A
    // (`04 06 3A`), whose `03 30 35` asks obj24 for fn5 at the SAME priority: Lucca sits on it f436-f507 while fn3 runs,
    // obj24 takes fn5 (ip 0922) when fn3 returns, and only then she recoils (`9C`). The port dropped the call (reqOp's
    // equal priority), so she recoiled while the Naga-ette was still appearing and its fn5 never ran.
    if (op == 0x03 || op == 0x04 || op == 0x06 || op == 0x07) {
      if (tgt < sc->nobj) {
        if (r8(sc->mem, 0x7E1C01 + (uint32_t)tgt * 2)) { s->addr = addr; return 1; }               // 608E / 612A
        if ((sc->mode[tgt] & 0x80) || sc->actors[tgt].cadPause) { s->addr = nxt; return 0; }      // 6100 / 6114
        if (pr >= sc->st[tgt].prio) { s->addr = addr; return 1; }                                   // 60B0 / 614C
      }
    }
    s->addr = nxt;
    reqOp(sc, tgt, B[addr + 2] & 0x0F, pr);
    // 0x04/0x07 = call AND WAIT for the callee to finish (Temporal Flux 'halt' variants;
    // Mom's curtain script relies on it: arms stay raised through the called copy).
    if ((op == 0x04 || op == 0x07) && tgt < sc->nobj) { s->waitObj = tgt; s->waitPrio = pr; s->callAddr = addr; return 1; }
    return 0;
  }
  if (op == 0x10) { s->addr = addr + B[addr + 1] + 1; return 0; }
  if (op == 0x11) { s->addr = addr - B[addr + 1] + 1; return 0; }
  if (op == 0xD2) {                                  // PARTY-MEMBERSHIP CONDITIONAL (ROM C0/38E8 -> C2/8C36
    // mode 0): byte1 = PC char-id, byte2 = jump offset. The query scans the 3 ACTIVE-party char-ids at
    // $7E2980/81/82 (C2/8CD1, limit 3) and returns 0 if found (present), 0xFF if not; op 0xD2's BNE TAKES
    // the jump when the PC is ABSENT. Target = (addr-of-byte2) + byte2 = (addr+2)+B[addr+2]; present falls
    // through (addr+3). Without this, Crono's Mom on the 2nd talk fell through B1->0x44C and showed the
    // Marle party-intro (#37/#39) even though Marle isn't in the party (D2 01 27 should have skipped it).
    int pc = B[addr + 1], present = 0;
    for (int k = 0; k < 3; k++) if (r8(sc->mem, 0x7E2980 + k) == pc) { present = 1; break; }
    s->addr = present ? nxt : (addr + 2 + B[addr + 2]);
    // C2/834D, THE GATE'S EPILOGUE, runs after every leaf of C2/8C36 -- this query included -- and this arm
    // never reaches the gate, so it is called here: it paints the three weapon rows (colours 13-15 of OBJ
    // slots 5/6/7, $7E:21BA/$21DA/$21FA, from bank $D1 via C2/F5ED), rebuilds the triple-tech flags and
    // rescans the relics. MEASURED: on the cartridge `$7E21BA` is written by C2:836C at f35 of the attract
    // capture (loc 433's `D4 ..` tail) and holds Crono's gold sword colours for every later leg; the port
    // read zero there at every sampled frame, so his sword drew black on loc 01C.
    CtVmC2Epilogue(sc->mem->s, gRomImg, (int)gRomImgLen);
    return 0;
  }
  if (op == 0x2D) {
    // C0/66A5 -- SKIP UNLESS SOMETHING IS HELD. $00F8 is the button latch, $00F9 the direction
    // latch; either one non-zero falls THROUGH into the guarded block, both zero takes the jump.
    //   66A6 LDA $00F8 / 66A9 BNE ->6701     (6701: INY / TYX / SEC / RTS -- fall through)
    //   66AB LDA $00F9 / 66AE BEQ ->66D0     (66D0: TYX / LDA $7F2001,X / TXA / CLC / ADC -- jump)
    //   66B0 BRA ->6701
    // ⚠ THE JUMP BASE IS addr+1, NOT addr+2: C0/66D0 is a bare `BB TYX` with NO `E8 INX`, where ops
    // 0x27/0x28 do `TYX / INX` -- that one missing byte is also why this opcode is two bytes where
    // they are three. (OPLEN[0x2D] = 2 already agreed.)
    //
    // ⭐ WHY THIS MATTERED: the op was already derived, with these exact citations, for the
    // --sweepops micro-executor (move.c case 0x2D) -- but the LIVE VM had no case at all, so it fell
    // to the generic advance and ran the guarded block UNCONDITIONALLY. In the attract that block is
    // scene 366's obj3 arm: `2D 0A / 56 00 62 00 / E0 B1 01 00 00` = "a button was pressed -> reset
    // $7F0062 and go back to location 433", i.e. the ABORT-THE-DEMO handler. Firing it with no input
    // meant the demo aborted itself the instant it reached scene 366, beating obj2's credits arm
    // (which is waiting out an `AD 6E`, 110 visits) and sending the attract back to the title.
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
    // ⭐ op 0x16 = C0/63E6, DERIVED 2026-09-13 (was uncited). Five bytes,
    // `16 [idx] [val] [mode] [skip]`. The field VM's handler table is C0/5D6E, validated on five
    // handlers cited elsewhere in this file (AD->C0/2F96, CC->3827, CD->385B, CE->3874, D2->38E8).
    //   63E8 LDA $7F2001,X -> $D9   the flag INDEX          63EF -> $DB   the VALUE/mask
    //   63F6 operand 3 = the MODE; 63FA BPL picks the arm, and bit 7 set does 640A ORA #0100 --
    //        the index gains $100, so the flag comes from $7F:0100+idx instead of $7F:0000+idx.
    //   6433 JSR ($6467,X) with X = mode*2: EIGHT comparators, each CLC = holds / SEC = jump --
    //        6487 ==, 648F !=, 6497 >, 64A1 <, 64A9 >=, 64B5 <=, 64BF AND, 64C7 OR (C0/64B3 = CLC).
    //   6349 is the JUMP arm and adds the skip counted FROM THE SKIP BYTE, which is why the target
    //        below is addr+4+skip and not addr+5+skip.
    else if (op == 0x16) { int li = B[addr + 1]; if (B[addr + 3] & 0x80) li += 0x100; if (!cmp_(r8(m, GLB + li), B[addr + 2], B[addr + 3] & 0x7F)) { jump = 1; target = addr + B[addr + 4] + 4; } }
    else { if (!(r8(m, GLB) < B[addr + 1])) { jump = 1; target = addr + B[addr + 2] + 2; } }   // 0x18
    s->addr = jump ? target : nxt;
    return 0;
  }
  if (op == 0x92 || op == 0x94 || op == 0x95 || op == 0x96 || op == 0x97 || op == 0x9A || op == 0x9C || op == 0x9D || op == 0xA0 || op == 0xA1 || op == 0xB5 || op == 0xB6 ||
      op == 0x9E || op == 0x9F || op == 0x98 || op == 0x99 || op == 0x8F) {
    int r = moveOp(sc, obj, addr, op);             // the bed-intro lead PC is revealed in run_tasks when its
    if (!r) s->addr = nxt;                          // wake-slide first INTEGRATES (a moving frame), NOT here at
    return r;                                       // the move-op setup visit -- avoids the 1-frame standing leak.
  }
  if (op == 0xE3) {                                 // explore mode: 0 = lock the player (C0/3C05: $011F)
    sc->explore = B[addr + 1] ? 1 : 0;
    w8(sc->mem, 0x7E011F, B[addr + 1]);             // 3C0B STA $1F -- the byte the A handler (C0/1AC1), the
                                                    // treasure probe and the d-pad gate all test. The C field
                                                    // alone left $011F at the loader's 1 through every cutscene.
    s->addr = nxt;
    return 0;
  }
  if (op == 0xDA) {
    // op 0xDA = C0/345A, the event's "HAND BACK to the player" -- a BLOCKING, follower-releasing op
    // (disasm/notes/scene439_control_release.md). It used to be a net-effect shortcut here:
    // `if (!(glb[0x54] & 1)) sc->explore = 1;`. That gate is an INVENTION -- $7F0054 appears NOWHERE in
    // C0/345A; the ROM's latch is $7E0154 bit0, its OWN re-entry flag. The two bytes were conflated, so
    // in scene 439 the candy conclusion released control only while the CAT-DELIVERY story flag was
    // clear (glb[0x54] bit0 is raised by obj12's `65 00 54` when you return the girl's cat). Bring the
    // cat back first, then walk into the candy, and the conclusion ends with control still locked --
    // the reported "after the scene ends the movement is not released". Oracle-isolated by a one-byte A/B.
    //
    //   345F: LDA $99  / BPL       -- no follower in party slot 1 -> IP+1, wholly inert
    //   3466: LDA $54  / BIT #01   -- the re-entry latch
    //   346C: LDA $7F03FE / CMP #03 / BCC -> TYX/CLC/RTS   -- phase < 3: PARK, IP unchanged
    //   3477: TRB $54  ... 349A LDX $C7 / INX / SEC        -- phase >= 3: drop the latch, IP += 1
    //   349F: release slot 1 (C0/32C8 copy), 34E2 slot 2 if present
    //   34E6/3533: $7F03FE := 2 (slot 2 absent) or 1 (both) ; 34F0/353B TSB $54 ; CLC/RTS = PARK
    //
    // The released follower's IP is parked at the literal $16FF -- the shared block C0/5CEC copies to
    // $7F3700, which this engine already materializes at sc->rgBase (ct_regroup.h). Its body walks the
    // follower back to the lead (`95 00`), counts arrivals (`71 FF` + the `12 FF 03 04 04` barrier) and
    // the LAST arriver's own `E3 01` is what restores control. So control comes back where the ROM puts
    // it -- after the walk -- instead of instantly, and the barrier seed is the same `3 - nFollowers`
    // law CtSceneBattleReturn already uses.
    Mem* m = sc->mem;
    int fol[2] = { -1, -1 };
    int nf = ctPartyFollowers(sc, fol);            // 345F `LDA $99 / BPL`, 34E2 `LDA $9B / BPL`
    // 3461 BPL -> 3463 INX / CLC: no follower -- the IP steps past it and the VISIT ENDS (CLC; move.c's arm had it). This
    // arm kept running: loc 070's ambush (obj22 fn1 `E3 00 / DA / E3 00 / AD 05`, cadence 4) reached its wait on the
    // DA's own visit, 4 frames before the cartridge (rule oracle: IP 035E f42 -> 0360 f46), and its fight began 8 early.
    if (nf == 0) { s->addr = nxt; return 1; }
    if (r8(m, 0x7E0154) & 1) {                     // 3468 BIT #01: a LATER visit
      if (r8(m, LOC + 0xFF * 2) < 3) return 1;     // 3472 BCC: the walk is still running -> PARK the IP
      w8(m, 0x7E0154, (uint8_t)(r8(m, 0x7E0154) & ~1u));   // 3477 LDA #01 / 3479 TRB $54
      // 347D-3496 -- THE PARTY VELOCITY RING IS RESET HERE, and this used to be skipped as 'five OAM
      // stores, a render detail'. They are nothing of the kind: $7F:0C00/0D00/0F00 are the ring's velX /
      // pre-adjust-X / shape rows (party_follower_ring.md) and $AB/$AC/$AD are its head and two cursors.
      //   347D  LDA $AB / STA $AC / STA $AD        both cursors := the head  -> every gap is 0
      //   3483  TAX / LDA #00                      (TDC/XBA above cleared the high byte: X = $AB, 8-bit)
      //   3486  STA $7F0C00,X / 348A STA $7F0C00,X  raw bytes 9F 00 0C 7F twice -- velX row, NOT 0C80
      //   348E  STA $7F0D00,X / 3492 STA $7F0D00,X  raw bytes 9F 00 0D 7F twice -- preX row, NOT 0D80
      //   3496  STA $7F0F00,X                      the shape row
      // Skipping it left a regrouped follower stacked ON the lead with a cursor still 24+ entries behind
      // the head, so the moment the lead walked off the gap was already past 0x18 and the follower
      // consumed the STALE entries from before the cutscene. Measured, scene 5 bell with Marle (the fixture
      // saves/field_re/s5_bell_marle.state): oracle f385 $01AB/AC/AD = 48 48 48, then Crono walks Left
      // from f480 and Marle stands on his tile until the gap reaches 0x18; the port kept 48 39 00, her
      // cursor crossed 0x18 at f489 and she walked NORTH by herself -- the pre-minigame walk-in's entries
      // -- off Crono's tile (defect #6, 'the bell with Marle').
      { unsigned hd = r8(m, 0x7E01AB) & 0xFF;
        w8(m, 0x7E01AC, hd); w8(m, 0x7E01AD, hd);
        w8(m, 0x7F0C00 + hd, 0); w8(m, 0x7F0D00 + hd, 0); w8(m, 0x7F0F00 + hd, 0); }
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
    return 1;                                      // 34F2 / 353D CLC / RTS -- PARK on the op
  }
  if (op == 0xE5) {
    // C0/3DC5 -- the QUEUED copy STAGES and yields: $013E/$0140/$0142 (16-bit), $0144, $0145 = $0144 & 7, `TSB $17` bit 5,
    // CLC. The copy is the main loop's, a frame later (the $0117 bit-5 service at the top of CtSceneTick: C0/1770 ->
    // `1790 JSR C0/AF4E`, then the style branch's frames with the event VM held). This arm used to queue the render's copy
    // 16 frames out, freeze the whole VM for a fitted 8 frames (`copyFreeze`) and re-time the issuer (`dc = 17 - 8`).
    // MEASURED, the logical oracle, saves/field_re/loc002_curtain_ctl.state + A at the curtain: E5 `..3C` at f73 (obj10's
    // cadence reloads 09, nothing else), $7E:3293.. copied at f74, obj10's $1015 held at 08 over f75-f76 (style 4); E5
    // `..3B` at f84, $7E:3253.. copied at f85, held f86-f89 (style 3).
    CtVmTileStageE5(sc->mem->s, B - sc->header, addr + sc->header);
    s->addr = nxt;
    return 1;
  }
  if (op == 0xE4) {                                 // copy tiles (curtains/doors/switches), immediate
    ctTileCopyQueue(sc, B[addr + 1], B[addr + 2], B[addr + 3], B[addr + 4], B[addr + 5], B[addr + 6], B[addr + 7]);
    // E4 is immediate, and the copy it makes is C0/3D97's `JSR C0/AF4E` on the map in WRAM -- the $7E:3000 the ROM's
    // readers see (the door handlers, C0/28E1, the next copy's source). The queue above is only the RENDER's copy of
    // the map (SceneData.l1t) and the collision planes; before this the WRAM never saw a scripted copy. MEASURED, the
    // logical oracle, saves/field_re/loc002_curtain_ctl.state, X at f4, B at f60: C0/0206's slot-1 run at f100 does
    // `E4 13 02 1B 0C 13 12 3B` then `E4 33 32 3B 3C 13 02 3B`, and $7E:3257-3259 go 3D3E3F (the reload's C0/A33B)
    // -> 3A3B3C (the curtain open) with the rest of the rect on all five planes.
    CtVmTileCopyE4(sc->mem->s, B - sc->header, addr + sc->header);
    // The COPY is immediate; the VISIT is over: `3DC0 LDX $C7 / INX / CLC / RTS`, and C0/5AB4 `BCC ->5ABA` ends the
    // visit on CLC, so the op after E4 runs on the object's NEXT visit. This arm used to return "continue" and ran it in
    // the same visit (the carry list at the end of step() names E4 too, but this return came first). MEASURED, the
    // logical oracle, scene 236's lever (saves/field_re/s236_lever.state, A at f10): obj9's visit at f95 runs
    // `E4 08 21 0A 22 08 15 3B` and stops, $1192 = 05EA and $7F00EC / $011F unchanged; its next visit (cadence 4) at
    // f99 runs `65 01 EC` / `E3 01` / `00`. The port ran all four at f95. The slot-1 runner (C0/595C) ignores the carry.
    s->addr = nxt;
    return 1;
  }
  if (op == 0xAA || op == 0xAB || op == 0xAC || op == 0xAE || op == 0xB3 || op == 0xB4 || op == 0xB7) {
    // ---- THE ANIMATION FAMILY -- C0/2E67 (0xAA), 2E9A (0xAB), 2F71 (0xAC), 2E8F (0xAE), 2E87 (0xB3),
    // 2E8B (0xB4), 2F01 (0xB7). Delegated to move.c's CtVmAnimFamily, the body --sweepops
    // micro-executes against the ROM, exactly like the item and party families above.
    //
    // WHAT THIS RETIRES. This arm used to keep the whole state in C fields (animMode/animIl/animLoops/
    // animBusy/animOpState/animOpAddr) and ended a BLOCKING 0xAB/0xB7 when the RENDERER said its own
    // playback was over (CtSceneAnimFinished from field.c's animTickActor). The ROM ends it on ONE byte:
    // C0/2E9C `LDA $7F0B01,X` / 2EA2 `DEC A / BEQ` -- 2 while the step ticker plays, 1 = complete on
    // the next visit -- and that byte is driven 2 -> 1 by the frame legs CtSceneTick already runs
    // (CtAnimTimerTick's CA4F arm, CtFieldFrameLookup's CBA4 arm) and forced back to 2 for an
    // off-camera object by A934. Measured cost of the private copy, scene 8's gate cutscene seeded
    // mid-B7: --vmdiff made ZERO ip-steps in 1500 frames (nothing headless ever cleared the flag),
    // and Belthasar ran the same cutscene ~800 frames ahead of the ROM (its `DD 71` at f1638 against
    // the oracle's f2447 under the same input) because the renderer's clock decided each B7's length.
    //
    // The C fields stay -- the renderer plays from them -- but they are now DERIVED from the bytes
    // (ctAnimSyncActor), never the other way round. The carry is the handler's: SET for the
    // ANIMESET family (continue), CLEAR for 0xAC (2F94) and for the arm/park visits of 0xAB/0xB7.
    uint8_t* W = (uint8_t*)sc->mem; int X = obj * 2;
    uint8_t pre0 = W[0x1780 + X], pre1 = W[0x1781 + X], pre2 = W[0x1680 + X], pre3 = W[0x1681 + X],
            pre4 = W[0x1601 + X], pre5 = W[0x1301 + X], pre6 = W[0x10B01 + X];
    CtHRet ah; ah.ip = addr; ah.carry = 1; ah.ok = 0;
    CtVmAnimFamily(sc->mem->s, B, addr, X, &ah);
    int changed = pre0 != W[0x1780 + X] || pre1 != W[0x1781 + X] || pre2 != W[0x1680 + X] ||
                  pre3 != W[0x1681 + X] || pre4 != W[0x1601 + X] || pre5 != W[0x1301 + X] ||
                  pre6 != W[0x10B01 + X];
    ctAnimSyncActor(&sc->actors[obj], W, X);
    // The renderer restarts its playback on a generation change. Every ANIMESET/SHAPE issue is a
    // restart in the ROM too (STZ $1681 / STZ $1601 on all of them); a 0xAB/0xB7 PARK visit writes
    // nothing (measured 0 writes) and must not restart the anime it is waiting on.
    if (changed || (op != 0xAB && op != 0xB7)) sc->actors[obj].animGen++;
    g_ctVmAnimOps++;
    s->addr = ah.ip;
    return ah.carry ? 0 : 1;
  }
  if (op == 0xD8) {                                 // *** BATTLE *** (ROM C0/3A83, 3-byte op `D8 imm1 imm2`).
    // The op BLOCKS across the whole battle and self-completes on the $0118 ack. The per-frame
    // consumer of the request lives in the FIELD tick (field.c, mirroring C0/1770-18D6); in a
    // consumer-less harness (ct.exe --vmdiff) the op just yields forever -- exactly the ROM's
    // behavior when the mainloop never services the request.
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
  if (op == 0xD9) {                                 // MOVEPARTY, C0/326C -- 1 + 3 coordinate pairs, one per party slot
    // The ROM does not walk the party itself. The FIRST visit ($7F03FC == 0, 32C8) releases each party slot onto
    // the walk program C0/5CEC copied to $7F3720 / 3740 / 3760 -- the C0/32C8 release op 0xDA also uses:
    //   32C8  STZ $1F                                        control off
    //   32CA  LDX $97 (16-bit) / $1C00,X * $80 + X /         the slot's IP pushed at its OWN priority level
    //         STA $7F0580,X ; $1180,X := 171F ; $1C00,X := 0  and repointed to its walk program, priority 0
    //   32FB  STZ $1A80,X / STZ $1A01,X / STZ $1001,X        no velocity, no ticket, eligible at once
    //   3306  the pair -> $7F3728/29 (& 7F), its sign bits -> $7F372B  = the program's `96 x y` / `A6 f`
    // Slot 0 unconditionally; slot 1 ($0199, 3344 BPL) and slot 2 ($019B, 33CB) only when present -- an absent one
    // ends the op with $7F03FC = 3 / 2, all three with 1. Each program is `E3 00 / 89 40 / 0D 00 / AE / 96 x y /
    // A6 f / 71 FE / 12 FE 04 04 04 ... 11 09 / E3 00 / 00`: walk (op 0x96, the MOVETO every script uses), face,
    // count the arrival in var FE = $7F03FC, wait for 4, RETURN to the pushed IP. The op PARKS (`LDX $C7 / CLC`);
    // a LATER visit (326E) parks while $7F03FC < 4, resets the party ring (327B-3296), parks while a present slot
    // still runs at priority 0 (329A-32B6), then $7F03FC := 0 and `INY x7 / TYX / CLC`: IP += 7, visit over.
    // The port drove the three movers straight from this op instead (the battle_entry_camera.md D.3 stand-in,
    // with its own per-frame arrival stop): the walks ended 8 frames early on the attract's 07D
    // (disasm/notes/field_op8c_place_from_vars.md section 7).
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
      if (k == 0) v |= r8(m, 0x7E0198) << 8;                        // 32CA LDX $97: SIXTEEN-bit, no bit-7 test
      else if (v & 0x80) { w8(m, 0x7F03FC, (uint8_t)(4 - k)); return 1; }   // 3346 #03 / 33CD #02, park
      int o = v / 2;
      if (!(v & 1) && o < sc->nobj && sc->d9Base >= 0) {
        CtSS* ps = &sc->st[o];
        if (ps->addr >= 0) ps->ret[ps->prio] = ssPushAddr(ps);     // 32E3 LDA $1180,X / 32E8 / 336D / 33F4 STA $7F0580,X
        ps->addr = sc->d9Base + 0x20 * k;                          // 32EE / 3373 / 33FA #171F / #173F / #175F
        { uint32_t ipw = (uint32_t)(ps->addr + sc->header);        // 32F1 STA $1180,X -- in WRAM NOW: a lead that
          w8(m, 0x7E1180 + (uint32_t)o * 2, ipw & 0xFF);           //   already had this frame's visit (it heads the
          w8(m, 0x7E1181 + (uint32_t)o * 2, (ipw >> 8) & 0xFF); }  //   queue) commits no visit-end mirror until next frame
        ps->prio = 0;                                              // 32F6 LDA #00 / STA $1C00,X
        ctSetDc(sc, o, 0);                                         // 3301 STZ $1001,X
        ps->wait = 0; ps->nm = 0;                                  // (the call latch $7F0980 survives: not 32C8's)
        vmTicketStop(sc, o);                                       // 32FB STZ $1A80,X / 32FE STZ $1A01,X
        sc->mx[o] = sc->my[o] = 0; sc->vecMove[o] = 0;
        sc->folRoamPrev[o] = 0;                                    // those STZs ARE the follower's walk-cycle release:
      }                                                            //   the conga's handover must not clear the new move again
      int a = B[addr + 1 + 2 * k], b = B[addr + 2 + 2 * k];        // 3309 / 3316: ip+1+2k, ip+2+2k
      uint8_t* prog = sc->raw + 0x1720 + 0x20 * k;                 // $7F3720 / 3740 / 3760
      prog[8] = (uint8_t)(a & 0x7F); prog[9] = (uint8_t)(b & 0x7F);   // 3311 / 331E STA $7F3728 / 3729
      prog[11] = (uint8_t)(((a & 0x80) ? 2 : 0) | ((b & 0x80) ? 1 : 0));   // 3322-333C STA $7F372B
    }
    w8(m, 0x7F03FC, 1);                                             // 3450 LDA #01 / STA $7F03FC
    return 1;                                                       // 3456 LDX $C7 / CLC -- park on the op
  }
  // 0xE6 (C0/3DF4, the per-layer drift $1D24-$1D29 + $1D4D; the camera bit feeds the commit accumulate 9238) and 0xF4
  // (C0/3D84, the auto-scroll mode $01BC; 0 also zeroes $1D4E/$1D4F) run move.c's CtVmScrollFamily through vmSharedOp.
  if (op == 0xE7) {                                 // scroll-to (ROM C0/3E30): operands<<1 -> $1D02/$1D03
    // (the scroll TARGET, 8px units); the field camera then SEEKS $1D0A/$1D0E toward it while the op
    // BLOCKS on $1D00 -- this is the pre-battle pan (battle_entry_camera.md A.2). Handler C0/3E30
    // (DP=$1D00), verbatim state machine:
    //   $1D00==0 -> ARM (3E41): $1D02=op1<<1, $1D03=op2<<1, INC $1D00 (=1), STZ $1F ($011F control off),
    //               then fall through 3E59 CLC RTS = BLOCK this visit (the seek has not started yet).
    //   $1D00==1 -> re-entry (3E35 DEC A / BEQ 3E59): CLC RTS = STILL BLOCKING (script IP parked here).
    //   $1D00==2 -> seek done (3E38): STZ $1D00, SEC RTS = ADVANCE the script (IP += 3, run the next op).
    // The per-frame stepper that drives $1D0A/$1D0E and flips $1D00 1->2 is CtSceneCameraSeekTick (ROM
    // C0/91AC), run ONCE PER FRAME at the top of CtSceneTick -- i.e. in EVERY host that ticks the VM: the
    // interactive field loop, main.c --vm/--vmdiff, AND the headless --battletest/selftest field loop, ALL
    // of which reach the VM only through CtSceneTick. That placement is the crux the PRIOR attempt missed:
    // it advanced the seek from a field.c main-loop call that the headless path skipped, so a blocking E7
    // hung the GATO battletest ($1D00 never cleared). Advancing inside CtSceneTick makes that structurally
    // impossible. The old instant-SNAP (settle the origin in one visit) was the deviation from the ROM;
    // blocking restores the ~65f forest / ~8f GATO pan and moves the event timeline TOWARD oracle lockstep
    // (D8 now fires POST-arrival: forest f168, GATO f218), while the field RENDER camera glides with it.
    Mem* me = sc->mem;
    uint8_t seekSt = me->s[0x1D00];
    if (seekSt == 0) {                              // ARM (C0/3E41): latch target, kill control, begin seek, BLOCK
      me->s[0x1D02] = (uint8_t)(B[addr + 1] << 1); me->s[0x1D03] = (uint8_t)(B[addr + 2] << 1);
      me->s[0x011F] = 0; sc->explore = 0;           // STZ $1F (C0/3E57): kill player control for the whole pan
      me->s[0x1D00] = 1; me->s[0x1D01] = 0;         // $1D00 = 1 (seeking), arrival bits clear. $1D0A/$1D0E LEFT
      return 1;                                     //   at the live camera; CtSceneCameraTick (91AC) seeks the
    }                                               //   coarse+phase toward $1D02/$1D03 at 1px/frame.
    if (seekSt == 1) return 1;                      // still seeking: BLOCK (IP parked on this op, re-entered/frame)
    me->s[0x1D00] = 0;                              // seekSt == 2: arrived -> STZ $1D00 + advance (SEC RTS). Return
    s->addr = nxt; return 0;                        //   0 (continue the visit) so the next op (D9 MOVEPARTY) runs.
  }
  if (op == 0xF2 && (r8(sc->mem, 0x7E0118) & 0x02)) {
    // C0/3CFD -- WAIT FOR THE ARMED FADE: `TYX / LDA $18 / BIT #02 / BNE ->3D05 / INX / CLC / RTS`.
    // While op 0xF0's bit is still set the INX is skipped, so the runner stores the IP back ON this op
    // and the visit ends; the frame C0/1F24 clears the bit, the op advances. The port advanced at once:
    // loc 01B's `F0 01 / F2 / D3 01 / D3 04 / DF ...` reached its location change before the fade.
    return 1;                                 // park: s->addr stays on the op, visit ends
  }
  if (op == 0xE0) {
    // ChangeLocation with FADE (ROM C0/3AB7; the handler runs with D=$0100, so its DP cells are
    // $01xx WRAM). `LDA $17 / BMI park` -- $0117 bit7 = "a fade-change is already pending". Fire
    // path: stage loc word -> $0112/$0113 (&0x1FF), tile x/y -> $0114/$0115, facing ((hi>>1)&0x0F
    // | hi&0x80 via TSB) -> $0116, latch $0117 |= 0x80, arm the fade counter $0119 = 0x0F, then
    // TYX/CLC/RTS = END THE VISIT WITH X RESTORED TO THIS OP (the 5AB1 runner stores ip=X on CLC).
    // The skip path (bit7 already set, C0/3AEE) is the same TYX/CLC park. Either way the object
    // REVISITS this op every frame until the staged load tears the scene down -- that park IS the
    // ROM's replay guard: scene 120's obj17 autorun JUMPBACKs into its cutscene branch right after
    // the E0, so advancing past the op replays the whole "Crono!"(str85).."She's Princess Nadia!"
    // (str92) dialogue forever. The COMMIT is not here: the field frame routine (C0/0C8A) drains
    // $0119 one per frame while $0117 bit7 is set and reloads via C0/000E when it hits 0 (ported
    // at the top of CtSceneTick); the load init then clears the latch (C0/0B96 STZ $17 -- ours via
    // memInit's zeroed s[]). Oracle scene-120 timeline: E0 fires f126 (ip parks ON the op,
    // $0119=0x0F), drains f127-f141, loc flips 0x78->0x6C at f142 with $0117 cleared.
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
    // C0/3B9E -- op 0xE0 with the destination in four LOCALS (the body, move.c CtVmChangeLocVars, is the one
    // --sweepops grades). The port had no effect for it: the op was stepped over, so loc 10F -- where every inn's
    // keeper sends the party to sleep (`E0 0F 03 08 08`) -- counted its wait, copied the way back into locals 0A-0D
    // and fell past `E2 0C 0A 0B 0D` to its `B2`: the screen stayed black for good. It parks like 0xE0 and the
    // same drain (C0/0C8A, top of CtSceneTick) commits $0112/$0114/$0116.
    CtHRet h; CtVmChangeLocVars(sc->mem->s, B - sc->header, addr + sc->header, &h);
    s->addr = h.ip - sc->header;
    return 1;                                 // CLC on both paths: park, visit ends
  }
  if (op == 0xDF || op == 0xE1) {
    // THE NON-RETURNING HALF of the family. 0xE1 = C0/3B4B, 0xDF = C0/3B98 (`LDA #01 / STA $1E / BRA ->3B4B`):
    //   3B4D REP #20 / LDA $7F2001,X / STA $D9 / AND #01FF / STA $12     the word, and the location into $0112
    //   3B5C LDA $7F2001,X / STA $14                                      the tile pair into $0114
    //   3B62 SEP #20 / LDA $DA / LSR / AND #0F / STA $16 / LDA $DA / AND #80 / TSB $16
    //   3B71 LDA $4210 / BPL ->3B71                                       spin until the vblank's NMI flag
    //   3B76 JSR C0/0B4E                                                  SEI, INIDISP $80, NMITIMEN/HDMAEN 0
    //   3B79 LDX $00 / STX $05 / LDX $02 / STX $07 / LDA $04 / STA $09    the location being left -> $0105..$0109
    //   3B85 LDX $12 / STX $00 / LDX $14 / STX $02 / LDA $16 / STA $04    the staged one -> $0100..$0104
    //   3B91 LDX #06FF / TXS / BRL ->0000                                 the stack thrown away: the REBOOT
    // It never returns to the runner, so NOTHING after it runs -- not the rest of this visit, not the next
    // object, not the next frame's main loop. The port ran it as 0xDD (return, keep the visit), and loc 1B1's
    // attract hub is exactly where that shows: `56 4C 62 00 / D4 03 / D3 01 / DF 87 02 1C 08` is followed by the
    // NEXT state's `16 62 4C 00` test, which the fall-through then passed -- `$7F0062 = 50`, `E0 F3 03` staged
    // into $0112, the load's commit copied it to $0100 (087 ran labelled 1F3) and the hub skipped 65 M B.C.
    // Here the stores are made, the visit parks on the op, and the host performs the rest at the vblank
    // (field.c, 'THE DF/E1 REBOOT'). Measured (saves/attract_entry3_handoff_f6750.state, --monitor): $0112 = 0087 and
    // $011E = 01 at f337, $0100 = 0087 at f338.
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
    // UNCONDITIONAL ChangeLocation, the RETURNING members: 0xDD C0/3B1B, 0xDE C0/3B45 (`LDA #01 / STA $1E / BRA ->3B1B`).
    // Operands: the location WORD (bits 0-8 = loc id; the high byte carries the facing and the arrival flags), then
    // tile X, tile Y. The whole handler is these stores and `SEC / RTS` -- IT MOVES NOBODY. No latch ($0117 is op 0xE0's),
    // no fade ($0119 is untouched), no load: C0/0066 reads $0100 only when something REBOOTS the field, and in the ROM
    // something always does. The census of all 65 sites (every location script, 2026-09-29): 40 are followed by a gate
    // warp (`FF 80-83`), 23 by a $C3 sequence (`FF` < $80), 1 by the loader (scene 0's `DD 00 00 00 00 / C8 41`), and the
    // last -- scene 172's obj9 fn1, `DD 0F 03 08 08 / E8 C3 / FF 9B / RETURN` -- is there for the origin: its fn0's
    // `DF CC 03` reboots into 1CC and files the DD's 10F as where the party came from ($0105, C0/3B79). This op used to
    // raise the host's location change as well, and the host ran its OWN door fade on it -- on the Telepod, in the four
    // frames between `DD 71 02 08 07` and the `FF 80` that follows it (the cartridge, seeded or not: $0100 = 0071 at
    // c2297, C0/3FC3's first `DEC $19` at c2301), the screen dimmed four steps the ROM does not take and then jumped back
    // up to C0/3FBB's ramp. And at scene 172 it would have loaded 10F.
    int word = B[addr + 1] | (B[addr + 2] << 8);
    w8(sc->mem, 0x7E01D9, word & 0xFF);                          // C0/3B23 STA $D9 (16-bit)
    w8(sc->mem, 0x7E01DA, (word >> 8) & 0xFF);
    // C0/3B34-3B3F: `LDA $DA / LSR / AND #0F / STA $04` then `LDA $DA / AND #80 / TSB $04` -- the
    // immediate arm puts the ARRIVAL FLAGS byte in $0104 ITSELF (the deferred arm stages $0116 and
    // C0/0CBC copies it at commit, which CtWramCommitLocation already ports). Ours filled only the C
    // mirrors, so $0104 kept the PREVIOUS door's byte and the arrival low-byte rule read stale bits.
    w8(sc->mem, 0x7E0104, ((B[addr + 2] >> 1) & 0x0F) | (B[addr + 2] & 0x80));
    // ...AND THE OTHER TWO STORES OF THE SAME HANDLER. With DP = $0100, C0/3B28 `STA $00` is the
    // masked location WORD into $0100/$0101 and C0/3B30 `STA $02` the tile pair into $0102/$0103 --
    // the IMMEDIATE arm writes the live trio itself, where the deferred arm (op 0xE0) stages
    // $0112/$0114/$0116 for C0/0CB4's commit to copy. Ours wrote only $0104 and kept the rest in the
    // C mirrors, so $0100 still read the location we were standing in. Nothing noticed while every
    // consumer went through CtScenePollLocChange -- but the REBOOT does not: C0/0066 is `LDX $0100`,
    // and the gate warp's whole job is to deliver the destination through it (the Telepod's
    // `DD 71 02 08 07 / FF 80` rebooted straight back into loc 8). Measured on the oracle: $0100
    // reads 0071 on the very frame the DD executes.
    w8(sc->mem, 0x7E0100, word & 0xFF);                          // C0/3B25 AND #01FF / 3B28 STA $00 (16-bit)
    w8(sc->mem, 0x7E0101, (word >> 8) & 0x01);
    w8(sc->mem, 0x7E0102, (uint8_t)B[addr + 3]);                 // C0/3B30 STA $02 (16-bit)
    w8(sc->mem, 0x7E0103, (uint8_t)B[addr + 4]);
    // The flag is a WRAM byte: C0/3B45 (0xDE) is `LDA #01 / STA $1E` before its BRA into the 0xDD body. The load's
    // constant init does not touch $011E; C0/286C ends `LDA $1E / RTS` and C0/2848's `BNE ->2866` skips its whole
    // brightness ramp on it (field.c).
    if (op == 0xDE) w8(sc->mem, 0x7E011E, 1);
    // CT_LOCTRACE: $0100 changed HERE (the oracle's `--monitor 7E0100:2` shows it on this op's frame), so the trace
    // prints here as CtScenePollLocChange prints a commit's; style = the $1E the DE set.
    { static int t = -1; if (t < 0) t = getenv("CT_LOCTRACE") ? 1 : 0;
      if (t) printf("loctrace: -> loc %03X tile(%d,%d) facing=%d style=%d\n", word & 0x1FF, B[addr + 3], B[addr + 4],
                    (B[addr + 2] >> 1) & 3, op == 0xDE ? 1 : 0); }
    s->addr = nxt;
    return 0;
  }
  applyEffect(sc, obj, addr, op);
  s->addr = nxt;
  // THE CLC-ONLY OPS: they advance the IP and END THE VISIT. Only the ones I have personally
  // disassembled are here -- 0x90/0x91/0x7E share C0/4CD9 (`STA $1A81,X / TYX / INX / CLC / RTS`) and
  // 0x7C/0x7D reach the same tail from C0/4CEC / C0/4CF9. A static classification puts 40 handlers in
  // this class; the rest are NOT applied, because that list is a CFG walk that cannot follow indirect
  // jumps and only 12 of its entries were hand-checked. Confirm an op against its C0/5D6E handler
  // before adding it.
  // CONFIRMED-BY-ME set. Each handler was disassembled from its C0/5D6E entry and contains a CLC and
  // NO SEC before its RTS, with no BRA leaving the window -- so it advances the IP and ends the visit:
  //   0x7C C0/4CEC  0x7D C0/4CF9  0x7E C0/4CE6  0x90 C0/4CD5  0x91 C0/4CE0   (the shared 4CD9 tail)
  //   0x2A C0/3ED6  0x2C C0/3EE6  0xA6 C0/5545  0xAC C0/2F71
  //   0xE4 C0/3D97  0xE5 C0/3DC5  0xE8 C0/3C11  0xEA C0/3C2F  0xEC C0/3C98
  //   0xED C0/3CBC  0xEE C0/3CC5  0xF0 C0/3CD0  0xF2 C0/3CFD  0xF3 C0/3D7A
  // NOT taken on faith from the static 40-op classification. RETRACTION: I dismissed that list as
  // "demonstrably wrong in at least one place -- it includes 0xFE, whose handler C0/3F26 contains a
  // SEC." The SEC is real and the conclusion was wrong. It is at 3F54, inside
  //     3F52: REP #20 / 3F54: SEC / 3F55: SBC $F0
  // -- carry SETUP for a 16-bit subtraction, not a return carry. 0xFE's only reachable RTS is
  // 3FAB: LDX $C7 / 3FAD: CLC / 3FAE: RTS. A static scan for "contains a SEC" cannot tell an
  // arithmetic SEC from a verdict; that is why each op is traced to its RTS here.
  //
  // THE HOLD-OUT ON 0xA6 IS OVERTURNED, and the reasoning is worth keeping. It was held back after
  // bisection showed that adding all fourteen candidates at once stalled the TEXTBOX typewriter
  // (`tbReveal` stuck at 0 through 400 frames while the rendered hash stayed byte-identical -- a
  // re-pin would have shipped the stall in silence). That was the right call THEN: the ROM fact was
  // never in dispute, but the substitute evidence was missing.
  // What changed: (a) all ten ops of the C0/554B tail were re-derived and independently refuted-
  // tested, 15/15 surviving; (b) holding ONE op out of a ten-op SHARED TAIL is precisely the per-op
  // special case the mandate forbids -- 0xA6 is not separable from 0x0F/0x17/0x1B/0x1D/0x1E/0x1F/
  // 0x25/0x26/0xA7 by anything in the ROM; and (c) the missing evidence now exists as a POSITIVE
  // test rather than an absence of red:
  //     scene 1, tbReveal at f60/f120/f240/f400 = 42 / 60 / 60 / 60  (textbox=1 idx=10)
  //   -- the box opens and the typewriter runs to the string length and saturates. The historical
  //   failure mode was `reveal` pinned at 0 forever; this is its opposite.
  // The two signatures that DO move are both port-only, and both were predicted:
  //   - camera-reenter-scene5: `reveal 4 -> 0` with cam(208,384), fgpx, fgskip AND fnv32=571C8700 all
  //     BYTE-IDENTICAL. It counts whether scene 1's obj15 opened a textbox in the ~17 frames before
  //     --reentertest's synthetic frame-3 door transition tears the scene down. Yielding correctly
  //     costs visits, so the box opens a few frames later. It is a race against a test artifact.
  //   - field-battle-ambush-generic-dissolve: the composed f362 hash moves; the battle OUTCOME
  //     (victory f1695 pcAttacks=5 cronoHP=70 marleHP=65) is byte-identical.
  // Every ORACLE-COMPARABLE measure is UNCHANGED: vmdiff-DEMO8 109, vmdiff-walk-touch-preempt f129,
  // vm-activation-pass-0F00, --checkstep 13/13, --checkdir 60/60, --checkmove, --porttick.
  // EARLIER DIAGNOSIS, RETRACTED: this said "we drop an op parked across a visit boundary". We do
  // not. The slot change at t18 is --reentertest's own fieldFree + fieldLoad (field.c: at frame 3 it
  // drives a real door transition, and the fade lands around t17), which legitimately resets every
  // slot. What actually differs is only whether obj15 reached its 0xBB before that teardown.
  //
  // Ops reaching this point are the ones that fall through to applyEffect; those with their own
  // earlier returns (0xB0/0xB1/0xB2 park, 0xB5 chase, 0xD8/0xE0 park-on-op, 0xD9) keep their own
  // semantics. THE THIRTEEN FORMERLY UNRESOLVED ARE NOW RESOLVED (all CLC -- see the two blocks
  // added to the list below). Two did NOT go into the flat list, for different reasons:
  //   0xEB (C0/3C4C) already has its own `return 1` at the sound-mailbox handler above -- correct
  //        carry, correct +3, no change needed.
  //   0xE2 (C0/3B9E) is CLC on both its paths but its IP delta is ZERO on BOTH -- 3BA2 `STY $C7`
  //        saves the ENTRY IP *before* the four INYs, and 3BFE `LDX $C7` restores it. It is a
  //        PARK-and-yield, the op-0xE0 ChangeLocation pattern (it also sets $0117 bit7 itself).
  //        Putting it in a flat list that does `s->addr = nxt` would fix the carry and BREAK the
  //        IP. It has its own arm now, next to 0xE0's (move.c CtVmChangeLocVars).
  if (op == 0x90 || op == 0x91 || op == 0x7C || op == 0x7D || op == 0x7E ||   // C0/4CD9 tail
      op == 0x2A || op == 0x2C || op == 0xAC ||                               // C0/3ED6 3EE6 2F71
      op == 0xE4 || op == 0xE5 ||                                            // C0/3D97 3DC5 (tile copy)
      op == 0xE8 || op == 0xEA || op == 0xEC || op == 0xED || op == 0xEE ||   // C0/3C11 3C2F 3C98 3CBC 3CC5
      op == 0xF0 || op == 0xF2 || op == 0xF3 ||                               // C0/3CD0 3CFD 3D7A
      // STEP A of the carry-model completion. 0x2B (C0/3EDE) and 0x32 (C0/3EE2) are `LDA #n / BRA
      // ->3ED8` into the SAME three instructions as 0x2A, which is already on this list:
      //     3ED6: LDA #04 / 3ED8: TSB $54 / TYX / INX / CLC / RTS
      // They differ only in the bit written to $54 (04 / 08 / 10). Yielding on one and not its two
      // siblings could not have been right. 0xFE (C0/3F26) reaches ONE reachable RTS,
      // `3FAB: LDX $C7 / 3FAD: CLC / 3FAE: RTS`, IP +18 which OPLEN already has.
      op == 0x2B || op == 0x32 || op == 0xFE ||                               // C0/3EDE 3EE2 3F26
      // STEP B: the C0/554B FAMILY, all ten, as ONE unit -- they are one ROM mechanism and splitting
      // them would be the invention. Every one of them exits through the same tail:
      //     5545: INY / TYX / LDA $7F2001,X          <- 0xA6 enters here
      //     554B: LDX $6D                            <- 0x0F/0x17/0x1B/0x1D/0xA7 BRA here (self)
      //     554D: STA $1600,X / STZ $1601,X          <- 0x1E/0x1F/0x25/0x26 BRA here (CROSS-OBJECT:
      //     5553: TYX / INX / CLC / RTS                 X is still the operand, so the write targets
      //                                                 ANOTHER object -- that is the whole difference)
      // IP deltas +1 (0x0F/0x17/0x1B/0x1D) and +2 (the rest) already match OPLEN.
      // 0xA6 was held out on a recorded JUDGEMENT CALL, not a factual dispute; it is landed here with
      // its nine siblings because holding one op out of a shared tail is exactly the per-op special
      // case the mandate forbids. The EFFECT is the ten's one tail too, move.c's CtVmFacingFamily (applyEffect
      // and the init pass call it): the facing AND the tail's `STZ $1601,X`, the walk-anim timer C0/C98A
      // decrements -- until 2026-09-27 the live VM stored the facing of five of them and nothing else.
      op == 0x0F || op == 0x17 || op == 0x1B || op == 0x1D ||
      op == 0x1E || op == 0x1F || op == 0x25 || op == 0x26 ||
      op == 0xA6 || op == 0xA7) return 1;                                     // C0/5535..5593 -> 554B
  // 0xA8 / 0xA9 (C0/55A0 / C0/5605, face toward an object / a party slot's object) end the same way once they have
  // set the facing: `55FB TYX / INX / INX / CLC / RTS`. Only the DECLINE -- the target's $1100 bit 7, `55AC BMI ->5600`
  // -- takes `5600 TYX / INX / INX / SEC / RTS` and lets the visit run on. Oracle, the cathedral (m81_frog_pre_lake,
  // CT_ORACLE_LOGICAL=1): obj4's `96 09 09 / AA 1D / A8 30 / AD 04` finishes the move at f26 and its wait counter
  // $7F0A09 takes its first step at f29 (C0:2FC3), not in the same visit -- the A8 ended it.
  if (op == 0xA8 || op == 0xA9) {
    int off = (op == 0xA8) ? B[addr + 1] : vmSlotTarget(sc->mem, op, B[addr + 1]);
    return (r8(sc->mem, 0x7E1100 + (uint32_t)off) & 0x80) ? 0 : 1;
  }
  // NOTE ON THE RUNNER'S CARRY, derived but deliberately NOT acted on here.
  // The carry is NOT "did the handler advance the IP" -- C0/5ABC-5ABF writes X back into $1180,X on
  // BOTH paths. It is "may I keep spending this visit's op budget": C0/5AB4 `BCC ->5ABA` ends the
  // visit on CLC, `DEC $67 / BNE ->5AA3` loops on SEC. So a handler can advance AND yield, and
  // SHOW/HIDE/REMOVE do exactly that -- C0/4CD9's tail is `TYX / INX / CLC / RTS`.
  //
  // Making these three `return 1` here is therefore literally what the ROM does, and it MADE THE
  // LOCKSTEP WORSE: vmdiff-DEMO8's first divergence moved 109 -> 69 (scene-8 obj16 falls one visit
  // behind completing the op 0x96 that follows its SHOW) and field-battle-ambush-generic-dissolve
  // went red. The oracle says the SYSTEM is wrong even though the fact is right, so the change is
  // not shipped. Hypothesis to test before retrying: the port's SYNTHETIC op-0x96/0x97 fresh-move
  // "setup delay" below (`(op == 0x96 || op == 0x97) && !hasTask`) may be a STAND-IN for exactly this
  // yield, in which case the two double-count and the stand-in is what has to go.
  // CT_VM_UNHANDLED=1: an op that gets here WITHOUT moving the IP has no case in this runner -- the visit re-runs it
  // until the five-op budget is spent, every visit, for good, and the object's script stops there. 0xA1, 0x8F and
  // 0x98/0x99/0x9E/0x9F were such holes (move.c's graded runner had them; this one did not) until 2026-09-28.
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
// Pre-seed the dialogue string table: scan obj 0's function slots (init/scene-init set it
// via 0xB8 — which slot varies by scene), then any object's. Read-only, so the validated
// tick behaviour is unchanged; live 0xB8 ops update sc->tbTable during play too.
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

// The party $7E:2980-82 persists across a load by simply being left alone: the ROM's own load READS it and
// never writes it (`0C34 LDA $7E2980 / 0C38 STA $94`, wram_lifetime.md 4). The new-game default is memBoot's.
void CtSceneGetParty(const CtScene* sc, int* a, int* b, int* c) {
  if (a) *a = sc ? r8((Mem*)sc->mem, 0x7E2980) : -1;
  if (b) *b = sc ? r8((Mem*)sc->mem, 0x7E2981) : -1;
  if (c) *c = sc ? r8((Mem*)sc->mem, 0x7E2982) : -1;
}

// LOCATION SONG carry ($7E29AE + the $1E00-$1E03 staging; C0/1B53, disasm/notes/audio_commands.md 7).
// The ROM's loader chain is C0/00F4 = 092B (loc*14) -> 1B53 (THE SONG) -> 0960 (tileset/map/SCRIPT),
// so the song shadow is already in place BEFORE any scene script exists, let alone runs its init. A
// scene whose init carries op-0xEA therefore OVERRIDES the location record -- which only works if the
// record is written first. Staging it as a carry reproduces that order: field.c computes it from the
// $F6 record before CtSceneCreate, and CtSceneCreate lays it down below, ahead of the init pass.
// -1 = the location declined (record bit7 set) or no ROM: leave $29AE alone, exactly as C0/1B7D's BMI
// skips both the store and the play.
static int g_carrySong = -1;
void CtSceneSetCarrySong(int song) { g_carrySong = song; }
static int g_loadSongPrice = -1;   // the last scene load's C0/1B8B JSL, as the sink priced it (frames << 10 | end line)
int CtSceneLoadSongPrice(void) { return g_loadSongPrice; }

// New-game ATB-mode selector A-confirm (ROM C2/E24C, disasm/notes/battle_atbmode.md S1.4):
//   LDA $54 / LSR A / LDA #$40 / BCS ->TRB : cursor row0(ACTIVE) -> TSB $2990 (bit6 SET)
//                                            cursor row1(WAIT)   -> TRB $2990 (bit6 CLEAR)
// Byte-exact: TSB/TRB only touch bit6, leaving the other packed-config bits (battle speed etc.,
// $2990 & 7, unrelated to this derivation) untouched.
void CtSceneSetAtbMode(CtScene* sc, int active) {
  if (!sc) return;
  int v = r8(sc->mem, 0x7E2990);
  v = active ? (v | 0x40) : (v & ~0x40);
  w8(sc->mem, 0x7E2990, v);
}

// `w` NULL = the scene owns a private, freshly-booted image and frees it with itself (the harness
// modes that build one throwaway scene and exit, and every path that predates host ownership).
// `w` non-NULL = build on the host's process-lifetime image: it is NOT cleared here and NOT freed
// with the scene. The caller is expected to have run CtWramLocationLoad() first -- doing it here
// would make a scene rebuild (state reload, --scene re-entry) look like a location change.
CtScene* CtSceneCreateIn(CtWram* w, const uint8_t* raw0, int rawLen, const uint8_t* gmem, int sceneNo) {
  if (rawLen < 1) return NULL;
  CtScene* sc = (CtScene*)calloc(1, sizeof *sc);
  if (!sc) return NULL;
  sc->mem = (Mem*)w;
  if (!sc->mem) {
    sc->mem = (Mem*)malloc(sizeof *sc->mem);
    if (!sc->mem) { free(sc); return NULL; }
    sc->ownMem = 1;
    memInit(sc->mem, NULL);                          // gmem is copied in at the old call site below
  }
  sc->tbTable = -1; sc->tbTableAtShow = -1;
  sc->tbPendChoice = -1;            // no decision pending (0 is a valid option LINE)
  sc->rgBase = -1; sc->d9Base = -1;
  // THE LOCATION LOAD'S OBJECT-TABLE FILL: every object's $0F80,X (the op-0x8E sprite-priority byte)
  // starts at #$80 = AUTO and $0F81,X (the OBJ palette attr) at 0, before any script runs. MEASURED on
  // the oracle across a real transition (saves/base_loc6.state, Down 55 -> loc 439: the whole 60-byte
  // row reads `80 00 80 00 ..` at f83, a block write with no per-byte PC, then only C0:4C7F (op 0x8E)
  // and C0:724B (the allocator's attr) touch it). The renderer's priority read is THAT byte
  // (CtSceneActorPrio), so a fresh scene must carry the fill, exactly as a seeded one carries the
  // oracle's. Without it an object that never runs `8E` read 0 = FIXED prio 0 (an occluder) -- while
  // one that runs `8E 00` on purpose (scene 8's Telepod vortex pair, $0F80 = 00 in every oracle state)
  // used to be mistaken for "unset" and promoted to AUTO, i.e. DRAWN.
  for (int k = 0; k < 64; k++) { w8(sc->mem, 0x7E0F80 + (uint32_t)k * 2, 0x80); w8(sc->mem, 0x7E0F81 + (uint32_t)k * 2, 0); }
  // THE ENGINE'S OWN PROGRAMS, IN THE SCRIPT IMAGE, AT THEIR ROM ADDRESSES. After the location script is
  // decompressed to $7F:2000 (C0/56D4), C0/5CEC copies four templates into the same image with MVN:
  //   5CEE  C0:5D37 -> $7F3700, $20 bytes   the battle-return regroup (IP 16FF: ops 0xDA / C0/5B1F)
  //   5CFC  C0:5D50 -> $7F3720, $20 bytes   op 0xD9's walk program, party slot 0 (IP 171F)
  //   5D0A  C0:5D50 -> $7F3740, $20 bytes   ... slot 1 (IP 173F)
  //   5D18  C0:5D50 -> $7F3760, $20 bytes   ... slot 2 (IP 175F)
  //   5D26  C0:5D68 -> $7F3780, $06 bytes
  // $7F2000 + k is raw[k] and an IP is an offset from $7F2001, so they go to raw[0x1700..0x1785] -- not past the
  // scene body, where this port used to append the regroup alone at an IP of its own. The largest shipped
  // script is 0x16FA bytes, so none of them reaches the region (a longer one would be overwritten, as the MVNs
  // overwrite it). rawLen stays the script's own length: the $7F2000 image diff grades the script bytes only.
  // The bytes between the script and $7F3700 read 0 here (op 0x00, RETURN), which also covers opLen's
  // over-read of up to b[o+5] past a truncated script.
  size_t sysHi = 0x1786, cap = ((size_t)rawLen + 8 > sysHi ? (size_t)rawLen + 8 : sysHi) + 8;
  sc->raw = (uint8_t*)calloc(cap, 1);
  if (!sc->raw) { free(sc); return NULL; }
  memcpy(sc->raw, raw0, rawLen); sc->rawLen = rawLen;
  memcpy(sc->raw + 0x1700, ctRegroupReturn, sizeof ctRegroupReturn);     // C0/5CF8 MVN
  for (int k = 0; k < 3; k++)
    memcpy(sc->raw + 0x1720 + 0x20 * k, ctRegroupD9, sizeof ctRegroupD9); // C0/5D06 / 5D14 / 5D22
  memcpy(sc->raw + 0x1780, ctRegroupTail, sizeof ctRegroupTail);         // C0/5D2F
  // ...AND THE IMAGE IS IN WRAM, where C0/56D4 and the MVNs put it: $7F:2000 + k = raw[k]. The runner here reads its
  // private copy, but every reader of the WRAM does not -- CtSceneSeedFromWram takes each object's current op from
  // $7F:2001 + IP, so an F5 capture (whose WRAM had zeros there) seeded every parked object as idle: the user's
  // scene-122 box at obj23's `BB 22` never reopened from build/ct_debug.state's image.
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
  // (fn0) runs op 0x57 (LOADPC slot 0) OR op 0x80 with the slot-0 char-id (scene 7's lead self-loads via
  // `80 00`). The party roster isn't committed yet here, so op 0x80 matches char-id 0 = the default lead
  // (Crono); the live op-0x80 handler does the full $7E2980 match. Telepod -> obj3; most scenes -> obj1.
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
  // The story flags are the host's either way: a private image gets them copied in, and a host-owned
  // one is seeded only when the caller actually supplies them (the ROM never rewrites $7F:0000-01FF on
  // a load -- ZERO writes across a whole transition, wram_load_census.md 5 -- so a NULL gmem on the
  // persistent path must leave the live flags alone rather than zero them).
  if (gmem) memcpy(sc->mem->g, gmem, 0x200);
  // C0/56B0's remaining scene-load fills, the ones memInit did not have: the 30 OBJ slots
  // ($0B88, C0/6F79), the arena owners ($0BC0, C0/E935), the WHOLE $0E00 page (C0/B204) and one
  // (00, FF) pair per object from $0F00 (C0/A7E9). The $0E00 fill is the load-bearing one --
  // C0/A9CD refuses to link an object unless $0E01,X is already negative. Written straight into
  // the image rather than through w8: these are scene-load constants, not per-frame stores, and
  // claiming them in the write mask would assert ownership the frame loop does not exercise.
  CtObjSceneInit(sc->mem->s, sc->nobj);
  // ...and C0/571A-58AA, the PER-OBJECT DEFAULT FILL that runs in the same load, between the
  // $7F:0200 and $7E:1900 DMA clears. It is the leg that puts `$1000,X` back to 4 -- i.e. CLEARS
  // the op-0x0B pause bit the activate guard C0/5AD1 refuses on -- and `$1100,X` back to 0x80 for
  // the whole 63-entry array before the scene's own objects get 0x07. Without it a pause (or a
  // live mode) set in one location rode into every later one on the same object INDEX: GATO is
  // object 8 of loc 6, and loc 130 pauses object 8 and never unpauses it. `raw + 1` is the
  // script's own 32-byte-per-object fn-pointer table, whose slot-0 word the fill copies into $1180.
  CtObjSceneDefaults(sc->mem->s, raw + 1, sc->nobj);
  // Active party char-ids (ROM $7E2980/81/82), read by op 0xD2's membership test (C2/8C36 mode 0), and
  // the RESERVE roster $2983..$2988. ON A PRIVATE IMAGE ONLY. The new-game default lives in memBoot; a
  // HOST image already holds it from cold init and, after that, holds the LIVE party -- re-seeding it
  // here would erase a follower (Marle after the join, whose fn0 needs op 0xD2 to see her in the party
  // so she reloads as a follower via op 0x5C instead of an NPC) on every single door. The private path
  // keeps the w8() writes so the write mask still claims these addresses for --vmdiff.
  if (sc->ownMem) {
    w8(sc->mem, 0x7E2980, 0x00); w8(sc->mem, 0x7E2981, 0x80); w8(sc->mem, 0x7E2982, 0x80);
    for (int rsv = 3; rsv < 9; rsv++) w8(sc->mem, 0x7E2980 + rsv, 0x80);
  }
  // The location song, staged BEFORE the init pass runs (see g_carrySong). C0/1B5F-1B73 stages the
  // $81 (00,FF,FF) "stop/prepare" in $1E00-$1E03 unconditionally; C0/1B7F-1B86 then commits the shadow
  // and the $10 play command. All of it is plain WRAM -- only the JSL is the sound hardware's side, so
  // this runs whether or not the host has an APU. A CT_NOAUDIO run must have IDENTICAL WRAM.
  // ...UNLESS the song is HELD. C0/1B53 opens with `LDA $7F01ED / BEQ ->1B5F`: non-zero takes C0/1B59
  // `LDA $7E29AE / BRA ->1B83` -- no $81 stop, no $F6 record, $29AE untouched -- and re-posts the song
  // already playing (`STA $1E01 / LDA #10 / STA $1E00 / JSL C7/0004`). The overworld entry C2/2F0F and the
  // world script's play-song C2/18F1 test the same byte. The attract sets it with its first op (`56 01 ED 01`,
  // oracle attract_cena_inicial f1 `$7F01ED = 01` by C0:69BE) and never clears it, so every one of its
  // scenes keeps song $18: `$29AE` reads 18 from f1 to f4800 across locs 1B1, 1F5, 16E, 01C, 01B, 0AD,
  // 112, 0DB, 1F7, 0A5 and 07D. Without the branch loc 0DB/112 wrote their own song and world 7 then
  // re-issued it (0x38) -- the user's "some scenes of this demo are changing the music".
  g_loadSongPrice = -1;
  if (r8(sc->mem, 0x7F01ED)) {
    w8(sc->mem, 0x7E1E01, r8(sc->mem, 0x7E29AE)); w8(sc->mem, 0x7E1E00, 0x10);   // C0/1B59 -> C0/1B83-1B88
    g_loadSongPrice = vmSoundPostMem(sc->mem, sc->blen, 12);                     // C0/1B8B JSL C7:0004
  } else {
    w8(sc->mem, 0x7E1E01, 0x00); w8(sc->mem, 0x7E1E02, 0xFF);
    w8(sc->mem, 0x7E1E03, 0xFF); w8(sc->mem, 0x7E1E00, 0x81);
    // ⭐ ...AND POSTED, before the play command is staged over it. C0/1B73 is `JSL C7/0004` right after the
    // $81 store (C0/1B70); the $10 gets its own JSL at C0/1B8B. The port staged both and let the frame's service post
    // what was left, so the $81 -- master volume back to FF at once -- never reached the driver, and a fade
    // the title's exit left behind (`81 40 00`, 7E/34B8) stayed on every song after it: the user's "on
    // load game in 600ad, the music starts and stops". Oracle, boot -> title -> load (PEEK_WRITEWATCH=1E00:4):
    // at each of f19, f46, f413, f444 `$1E00 = 81` by C0/1B73, `$1E00 = 00` by C7/0196, THEN `= 10` by
    // C0/1B8B and its own C7/0196. With no sink (CT_NOAUDIO, ct.exe) this is inert and the WRAM is unchanged.
    // Its time is the load's too: the cartridge's C0/1B73 JSL is ~15k cycles of C7 (the bell stairs, 1B7: 14,670 from
    // the JSL to its RTL, `ct --cputrace` CPUTRACE_BEAM), and the $10 below starts on the line it returns on.
    int songLn = 12, songFr = 0, songAny = vmSoundPostAcc(sc->mem, sc->blen, &songLn, &songFr);
    if (g_carrySong >= 0) {
      w8(sc->mem, 0x7E29AE, g_carrySong & 0xFF);
      w8(sc->mem, 0x7E1E01, g_carrySong & 0xFF); w8(sc->mem, 0x7E1E00, 0x10);
      // ⭐ ...AND POSTED TOO, inside the load: C0/1B8B `JSL C7/0004`. The port used to leave the `$10` for the frame's
      // service, which posted it DURING the load's black frames, so a location that uploads a song cost no more than
      // one that does not (field_load_cost.md 7, 'the field loader's post is still not consumed'). Oracle, the attract's
      // demo-battle RESET into loc 0 (saves/attract_demo_battle_f1850.state, A from f20): the `10 00` JSL runs from f57
      // line 246 to f68 line 142 -- 10.6 frames of the load's 12.2 (C0:0066 f57 line 241 -> C0:0094 f70 line 22) --
      // with no NMI and no HDMA behind it. The host adds its price to the load's black frames (CtSceneLoadSongPrice).
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
                                                       // ROM C0/45A0 etc.) force solid=1, op 0x84 overrides.
                                                       // Sprite-less coordinate objects (Telepod demo obj12)
                                                       // stay non-solid -> the player can step onto them.
    a->moveSpeed = 0x10;           // $1A00 default; an init op 0x89 overrides (captured by runFunc)
    a->coll = 1; a->collMap = 1;   // $1C80 RAM leftover default = 0x03 (ORACLE scene-8: every generic obj
                                   // reads 0x03; loaders 0x81/0x82 write $1100/$1B01 but NOT $1C80 --
                                   // C0/4476ff). Init 0x0D overrides; the loc-112 enemies' $1C80=0 comes
                                   // from the WRAM SEED (byte-exact), not from a cold default of 0.
    a->cadPause = 0;               // $1000 bit7 clear until op 0x0B / seed
    a->snap = 0;                   // $1C81 default 0 (no snap); init 0x0E overrides (obj10/Taban)
    a->moveMode = 0x80;            // $1100 default = idle (C0/A819 skip); a LOAD op (0x57/80-83) sets the real mode
    a->moveAnim = 4;               // $1000 OBJECT-SPAWN cadence default = 4 (oracle: every non-PC event obj across
  }                                //   candy/im_so_sorry/bedslide); op-0x87 or a PC loader (->1) overrides in fn0
  // THE $1C80/$1C81 COLD LEFTOVER, applied BEFORE the init pass -- the ROM's order, and the reason it
  // belongs here rather than after. Both are the ROM's STORAGE, not mirrors: C0/5BA0 gates the
  // object-solid test on $1C80 bit1 and C0/305D on $1C81, reading WRAM. They are process-lifetime and
  // their cold value is a LEFTOVER, measured at 0x03 for every generic object in the oracle's scene 8
  // (see the a->coll default just above). A fresh CtSceneCreate starts from a zeroed image, so the
  // port must put the leftover back -- and then let fn0's own `0D`/`0E` write over it (C0/629F /
  // C0/62AE), exactly as the ROM does. Seeding AFTER the init pass instead clobbers those ops back to
  // the reconstructed two bits, which is precisely what let a mutation of the init handler survive.
  // ⚠ WITHOUT THIS SEED THE OBJECT-SOLID SCAN IS DEAD: bit1 never sets, C0/5B95's head gates every
  // mover, and the whole suite stayed green through three commits of exactly that.
  for (int o = 0; o < count; o++) {
    w8(sc->mem, 0x7E1C80 + (uint32_t)o * 2, (uint8_t)((sc->actors[o].coll << 1) | sc->actors[o].collMap));
    w8(sc->mem, 0x7E1C81 + (uint32_t)o * 2, (uint8_t)sc->actors[o].snap);
  }
  gLoadPostOn = gLoadPostMode;                       // C0/5709 and C0/56C8 run at the load's end (gLoadPost, above)
  w8(sc->mem, 0x7E0173, 0);                          // C0/58DC STZ $73: op 0x82's party-slot counter (C0/4665 / 46D9)
  gOpCostOn = gOpCostMode; gOpCostWhy = NULL;       // a load the host prices: its ops are priced as they run
  gOpCostSum = 0; gOpCostN = 0;
  for (int n = 0; n < count; n++) {
    // C0/58ED / 58F0, the init runner's head for EVERY object of the scene ($BD = the object count * 2, inactive ones
    // included -- 58F3's `BMI` comes after): `STZ $1B01,X / STZ $1B80,X` with the REP #20 of 58E2 in force, so two
    // 16-bit clears -- $1B01/$1B02 (the solid byte and the next object's $1B00) and $1B80/$1B81 (op 0x7A's jump flag and
    // its gravity). The port had neither: a $1B80 left by a jump in one location (C0/4E1E) would ride into the next on
    // the same object index and C0/AAC7 would integrate it (measured: scene 5 re-entered from scene 1, one NPC more).
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
    // The countdown $1001,X is what the load LEFT: the fill's 00 (C0/5786, CtObjSceneDefaults above), re-armed
    // only by an fn0 op 0x87 (C0/488B, runFunc). Nothing else in the load writes it, so the first queue build after
    // a load (C0/59F0 `LDA $01,X / BEQ ->5A01`) enqueues EVERY object. MEASURED on the oracle under the rule, every
    // load looked at (--monitor 7E1000:n --writer): 16E c2505 `04 00 04 00 04 00 04 00` -> c2513 all four visited;
    // 0AD c3588 / 0E6 c7582 all counts 00 -> all reloaded on the first build; loc 468's stairs (saves/field_re/
    // scene468_stairs_to_120.state, Right+B) f46 `04 00 01 00` (C0:42E1, the lead's $1000 = 01) -> f52 `04 04 01 01`.
    // The port used to write the period here (obj0 1), so an object of period 4 made its first visit 3 frames late:
    // the attract's clouds over 16E ran 3 frames behind their own layer scroll (BG2 3 px, BG3 12 px off), the robots
    // of 0E6 and the curtain of 112 left 3 frames after the rest of their scene (build/_divergence/
    // audit_2026-09-28.md, #1). The old note here cited a port metric (scene 7 fgskip, scene 285 deflected), not
    // the cartridge.
    s->dc = r8(sc->mem, 0x7E1001u + (uint32_t)n * 2);
    s->wait = 0; s->waitObj = -1; s->waitPrio = 0;
    if (stopOp == 0x00) s->addr = stopAddr + 1;
    else if (stopOp == 0xAF || stopOp == 0xB0 || stopOp == 0xB1 || stopOp == 0xB2) s->addr = stopAddr;
    else s->addr = -1;
    // C0/5915-591B -- the init runner's own IP writeback: `LDA $7F2001,X / BEQ ->5915` leaves the
    // loop on fn0's terminator and then `INX / TXA / LDX $6D / STA $1180,X` commits the address
    // AFTER it. The load's fill (C0/571A, CtObjSceneDefaults) seeded fn0's START there; without
    // this advance a freshly-loaded scene reads the start where the ROM reads the continuation,
    // which is exactly what a spliced F5 capture hands the oracle.
    if (s->addr >= 0) { uint32_t ipw0 = (uint32_t)(s->addr + sc->header);   // = CTW_IPOFF, defined further down
      w8(sc->mem, 0x7E1180 + (uint32_t)n * 2, ipw0 & 0xFF);
      w8(sc->mem, 0x7E1181 + (uint32_t)n * 2, (ipw0 >> 8) & 0xFF); }
    // The live position is the ROM's $1800/$1880 WORD /16, whenever the init pass captured one. The
    // old form re-derived it as `y - 1/16`, which is right only for op 0x8B's synthesised 0xFF low
    // byte; op 0x8D stores the script's raw words, and a tile-aligned one (scene 129 obj13, y=0x0800)
    // came out a sub-pixel high = one TILE up, which is what C0/A947's window test reads.
    sc->fx[n] = sc->actors[n].px16 >= 0 ? sc->actors[n].px16 / 16.0 : (double)sc->actors[n].x;
    sc->fy[n] = sc->actors[n].py16 >= 0 ? sc->actors[n].py16 / 16.0 : sc->actors[n].y - 0.0625;
    sc->mspd[n] = sc->actors[n].moveSpeed; sc->cycles[n] = 0; sc->hasTask[n] = 0; sc->coll[n] = sc->actors[n].coll; sc->collMap[n] = sc->actors[n].collMap; sc->b00[n] = 0; sc->c1C81[n] = sc->actors[n].snap; sc->moveAngle[n] = -1;   // op-0x0E snap captured at init (obj10/Taban)
    sc->folRoamPrev[n] = 1;                  // conga: "was roaming" so f0 is not treated as a resume
    // ORACLE (BASE8 scene-8 wram): a PLACED object with NO load op has $1100 = 0x07 (not 0x80!) --
    // it PASSES the C0/5FC8 REQ gate (e.g. scene-439 obj0, REQ'd by Marle's bump fn2; the sprite-less
    // Telepod obj12). Only op-0x0A deactivation / the non-party PC-load abort (C0/428C) write 0x80.
    sc->mode[n] = sc->actors[n].dead ? 0x80                               // a kill no load op undid (C0/6254)
                : sc->actors[n].defined ? sc->actors[n].moveMode : 0x07;   // $1100,X: the load-op mode (the C0/A819
                                                                          // dispatch key); no load op -> the fill's 07.
  }
  // $7E image mirror for the battle transfer (field.c's C0/1770 consumer reads the SAME RAM the ROM
  // does): $1100,X move-mode + the party-slot -> object-X table $0197/$0199/$019B (maintained by the
  // ROM PC loader C0/426A; read by C0/024C's lead-cell record + C0/0715's defeat-pose staging).
  // CtSceneSeedFromWram overwrites the whole image with the oracle's values afterward when used.
  for (int n = 0; n < count; n++) {
    w8(sc->mem, 0x7E1100 + (uint32_t)n * 2, sc->mode[n] & 0xFF);
    if (sc->mode[n] >= 0 && sc->mode[n] <= 2) w8(sc->mem, 0x7E0197 + (uint32_t)sc->mode[n] * 2, n * 2);
  }
  // THE LEAD IS $0197, the object the slot-0 PC loaded into (C0/426A, above) -- not the first object whose fn0 has op
  // 0x57. Op 0x57 is CRONO's load (0x5C Marle, 0x62 Lucca, 0x6A Robo, 0x68 Frog, 0x6C Ayla, 0x6D Magus), and it loads
  // nothing when he is not in the party: after the Ocean Palace the lead is whoever stands in slot 0. MEASURED on Jackie's
  // ch23 save (loc 173, party Marle / Lucca / Frog): $0197 = 02 on the cartridge and in the port's own RAM, while the
  // scan cached obj3 (Crono's object, unloaded) -- the host then drew no lead at all, and Marle (obj1) walked invisible.
  // The scan stays as the fallback for a scene whose PCs all failed to load ($0197 = 80).
  { int v = r8(sc->mem, 0x7E0197);
    if (!(v & 0x80) && v / 2 < count) { sc->playerObj = v / 2; sc->playerLoaded = 1; } }
  // op 0x88 COLOR-effect visibility gating (the Telepod vortex, scene 8). The vortex sprites
  // (94/95) load + position at init but have NO palette -- their blue swirl is painted ONLY by
  // op 0x88 during the cutscene (ROM C0/4892; oracle scene_8_pod: obj27/28 placed, $7F0B80 all
  // 0xFFFF, vortex absent). We skip op 0x88's palette load and bake the swirl into the sprite,
  // so it would show constantly. Fix: mark the EMITTER (an object whose script runs op 0x88
  // cmd 0x20-0x5F = sets the effect ref) + any positioned object sharing its INIT tile (the
  // paired effect sprite, spr 94 + 95 co-located at 136,47) as colorGated; CtSceneActor then
  // draws them only while the effect is active. (Init never runs op 0x88, so colorFx starts 0
  // = hidden; the cutscene's open arms it, the close clears it.)
  // The emitter is an object whose script LOADS a full palette via an op-0x88 BLOB (cmd 0x80-0x8F)
  // -- only ~17 effect objects ROM-wide (sprite 94/95 dominate). We deliberately do NOT key on the
  // ref form (cmd 0x20-0x5F): that is a tint/color-cycle on a NORMALLY-VISIBLE sprite (426 objects,
  // incl. PCs) and must NOT be hidden. We also require a real NPC/effect sprite (>= 7): PC sprites
  // 0-6 are always visible, never palette-gated.
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
  (void)sceneNo;                                     // no per-scene branching (mandate): the engine is general.
                                                     // The lead PC is ALWAYS drawn; the bed-intro "hide" is the
                                                     // blanket SPRITE occluder + foot-tile priority (field.c),
                                                     // not a pcRevealed/sceneNo gate (that was removed).
  // Scene-entry hook: the engine runs obj 0's FUNCTION 1 on map load (flag-gated E4 room
  // states: opened doors, the intro's slept-in bed with Crono's hair on the pillow...).
  // Run it synchronously through the live stepper (the init runner has no tile-copy path),
  // then restore obj 0's startup-script address.
  // THE RUNNER IS C0/595C, called by the load at C0/56C8 right after the init runner C0/5709:
  //   595C REP / STZ $6D / LDA $7F2003 / TAX         obj0's slot-1 pointer, $6D = 0
  //   5967 LDA $7F2001,X / BEQ ->597C                the 00 terminator ends it -- NOT executed
  //   596D TXY / ... / JSR ($5D6E,X) / BRA ->5967    every other op, whatever carry it returns
  // It never looks at the carry, so an op that ends a VISIT (CLC) does not end this run. We used to stop
  // at the first yield: loc 0AD's fn1 opens `7D 0A / 7D 16 / 7D 1A / 7C 18` (all C0/4CD9-tail CLC ops)
  // and the run died on the first one, before its `16 57 04 06 25` gate and the four `E4` copies that
  // blank the stone chamber over the starfield. MEASURED (--hookpc 3D97 from PEEK_SAVE f1400 of
  // saves/attract_cena_inicial.state): four visits, $6D = 00, Y = 0206/020E/0216/021E = body 0x46..0x5E,
  // during the load, with obj0's $1180 never leaving its idle loop. A handler that PARKS (IP unchanged)
  // would spin C0/595C forever; no location can do that and still load, so the port stops there.
  // The runner itself is CtSceneRunObj0Slot1 -- the same routine the menu exit and the battle return call.
  if (count > 0) {
    CtSceneRunObj0Slot1(sc, "C0/56C8");
    sc->st[0].prio = 7;                              // C0/595C leaves $1001 alone: obj0 keeps the load's count
  }
  // C0/56CB-56D3, the load's last step: `LDA #0B / JSL C2/8004` = the $C2 party gate's leaf 11 (C2/8C5A RTS), run for
  // its epilogue C2/834D -- every load repaints the weapon rows $21BA/$21DA/$21FA (move.c CtVmC2LoadTail). Without it
  // the row a sequence stage's setup overwrote (C3:0636 at the race) stayed overwritten through 1B1 and 06B.
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
  // Seed $1A81 (the SCRIPT visibility flag the activation pass reads) from the state the init pass
  // left in `draw`. Ops 0x90/0x91/0x7E maintain it from here on, exactly as the ROM does; without a
  // seed every object that is visible WITHOUT ever running an explicit SHOW would read $1A81 = 0 and
  // the activation pass would hold it permanently inactive.
  for (int o = 0; o < sc->nobj; o++)
    w8(sc->mem, 0x7E1A81 + (uint32_t)o * 2,
       sc->actors[o].draw == 0 ? 0x01 : (sc->actors[o].draw == 2 ? 0x80 : 0x00));
  // NO palette sweep here: C0/B0E6 needs the camera, which does not exist yet. It runs from
  // field.c's fresh-entry branch via CtScenePalSweepEntry -- see palSweepB0E6.
  return sc;
}

CtScene* CtSceneCreate(const uint8_t* raw0, int rawLen, const uint8_t* gmem, int sceneNo) {
  return CtSceneCreateIn(NULL, raw0, rawLen, gmem, sceneNo);
}

// ===========================================================================
// WRAM bridge for the lockstep oracle differential (--vmdiff). The ROM field VM
// keeps per-object state in WRAM; our VM keeps it in CtSS/actors. These map
// between them so we can seed our VM from a mid-cutscene oracle state and then
// byte-compare the scheduler state frame-by-frame (the M1+M2 keystone tool).
// WRAM layout: $7E -> off 0; $7F0000 -> off 0x10000. The runner reads the opcode at
// $7F2001+IP (dispatch: LDA $7F2001,X with X=IP), so IP = body addr + header (body=
// raw+1+header, $7F2000 image==raw, opcode at wram[0x12001+IP]==raw[1+header+addr]).
// X = obj*2.
// ===========================================================================
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
  // ...AND $7F:0400-FFFF, which this seed used to skip. Mem IS a flat 128 KB image (the
  // ct_wram_layout_assert above proves it: s at 0, g at 0x10000, l at 0x10200, hi filling the rest to
  // 0x20000), so seeding three of its four regions left 63 KB of bank $7F at ZERO -- including the
  // sprite assembly templates at $7F:4800 and the staged OAM records at $7F:4BC0 that C0/CAD9's drain
  // reads and C0/B309's stage 2 copies out. Measured before this line existed: our image held 0
  // nonzero bytes across $7F:4BC0+0x540 where the oracle held 1158, so anything downstream of the
  // drain was reading zeros in every --vmdiff run.
  memcpy(sc->mem->hi, w + 0x10400, 0xFC00);         // $7F0400..$7FFFFF
  // ...and the ENGINE'S PROGRAMS in the script image, $7F3700-3785 (C0/5CEC's copies), which this VM executes from
  // sc->raw. They are not constant once a scene runs: op 0xD9 writes each walk's destination and facing into its
  // `96 x y` / `A6 f` (C0/3311-333C). A state captured mid-walk carries them only in WRAM -- the attract's demo
  // battle seed (saves/attract_demo_battle_pre.lakewram, f1735: $7F03FC = 02, the followers on `96` at IP
  // 1746/1766, $7F3748 = 01 19) -- and the template's `96 00 00` walked them to tile (0,0) instead.
  if (sc->raw && sc->rgBase >= 0) memcpy(sc->raw + 0x1700, w + 0x13700, 0x86);
  // ...and the rest of the image, $7F2000-36FF: the ROM's runner reads every op from $7F:2001 + IP (C0/5A46), so the
  // seed's own bytes are the script it runs. Taken only when the seed holds THIS scene's image (its object count and
  // fn table equal ours): an F5 capture from before the port wrote the image to WRAM holds zeros there. A seed whose
  // WRAM was poked runs the poked script on both sides -- tools/battle_sim.py stages any formation by writing
  // `83 id op2 / 8B x y ...` at $7F:3000 and pointing objects' $1180,X there, so the field's own op 0x83 (C0/46DF) loads
  // each enemy's sprite and palette.
  if (sc->raw && sc->rgBase >= 0 && !memcmp(w + 0x12000, sc->raw, (size_t)(1 + sc->header)))
    memcpy(sc->raw, w + 0x12000, 0x1700);
  int hoff = CTW_IPOFF(sc);
  // The dialogue string table is the ROM's $012B/$012C/$012D (op 0xB8, C0/3557); the location loader
  // clears the bank byte (C0/0BDA) and op 0xBB refuses a box while it is zero (C0/3570). A seeded state
  // carries the pointer its scene set long before the capture, so read it back instead of leaving the
  // create-time prescan's value (-1 when the seed replaced a fresh scene's memory).
  if (w[0x12D] != 0) {
    sc->tbTable = (w[0x12B] | (w[0x12C] << 8) | (w[0x12D] << 16)) - 0xC00000;
    if (w[0x129] != 0) sc->tbTableAtShow = sc->tbTable;   // a box already up: it was opened on this table
  }
  // A box up at capture time ($0129 != 0 with the speaker's $7F0A00,X latch set) is the machine's own state;
  // the host-facing view (sc->textbox, the speaker $012E, the string $012A, the position request $0130)
  // is rebuilt from those bytes so CtSceneAdvance / the --vmdiff mirror can close it and the op's
  // completion visit finds its latch.
  if (w[0x129] != 0) {
    int spk = w[0x12E] / 2;
    if (spk < sc->nobj && w[0x10A00 + spk * 2] != 0) {
      sc->textbox = 1; sc->tbObj = spk; sc->tbIndex = w[0x12A]; sc->tbChoice = 0; sc->tbOpt = -1;
      sc->tbOp = (w[0x130] == 1) ? 0xC1 : (w[0x130] == 2) ? 0xC2 : 0xBB;
      sc->st[spk].tb = 1;
    }
  }
  // Iris: the ROM's OWN bytes, with the field's direct page at $0100 (so the disassembly's $39..$3D are
  // $0139..$013D): $0139 = the machine's state (1 opening / 2 closing / 0 idle -- C0/2263 `STZ $39` when
  // the open completes, and the radius STAYS), $013A/$013B = the circle centre in screen pixels (copied to
  // $0350/$0352 for the C3/0008 window builder), $013C = the radius (INC at C0/2260 while opening, DEC at
  // 226D while closing), $013D = the target ($20). MEASURED on marle_teleportation: f193 $0139 = 1 with the
  // centre (135,55) and the target 32, $013C 1..32 over f194-f225, f226 $0139 = 0 with $013C = 32.
  // This used to read `$39` (the wrong page) and rebuild the radius from the $7F1AC8 window table -- five
  // stray rows that every state carries (the tent, the fair, the pod) made a FULL disc at the origin.
  sc->irisState = (w[0x139] == 1) ? 1 : (w[0x139] == 2) ? 2 : 0;   // (the rest -- $013A-$013D, the tables -- IS the seed)
  // The party-trail runs in the player MOVEMENT controller, gated by the d-pad gate $0162==0 && $011F!=0
  // (player.c). The candy stand LOCKS it ($011F=0x00, E3-locked) even though the player keeps a small scripted
  // down-leash; a reload must restore that from the state, else explore defaults to 1 and the conga drags Marle.
  sc->explore  = (w[0x11F] != 0);                    // $011F: 0 = control LOCKED (E3 00 / cutscene)
  sc->moveLock = (w[0x162] != 0);                    // $0162 != 0 also locks the controller (+ party-trail)
  // The screen's brightness is $0119 and nothing else: the NMI's C0/EC4E `LDA $0119 / BNE ->EC55 / LDA #80 / STA $2100`
  // writes it to INIDISP every frame. sc->bright stands for its low nibble (CtSceneSyncBright), and the create-time
  // prescan that ran before this memcpy left it at whatever THAT pass's fn0 set -- loc 002 at storyline 0 runs the
  // opening's `F0 00`, so sc->bright = 0 over a seed whose $0119 = 0F (saves/field_re/loc002_intro_curtain: WRAM equal
  // to the cartridge's every frame, the picture black every frame). Rebuilt from the byte like $011F/$0162 above.
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
    int pd = w[0x1000 + x] & 0x7F;                   // $1000,X cadence PERIOD (bit7=paused). op-0x87 periods set by the
    sc->actors[o].moveAnim = pd;                     // $1000,X is AUTHORITATIVE from the oracle's live cadence (op-0x87
                                                     // periods set by the script before this savestate aren't replayed
                                                     // here). pd=0 -> moveAnim 0 -> the C0/42D7 fallback cadence 1; this
                                                     // OVERRIDES the fresh-spawn default 4 so wram-seeded baselines are byte-exact.
    s->prio = prio; s->dc = cd; s->tb = 0; s->nm = 0;   // the shadow IS $1001,X: seed it literally. A
                                                        // seeded 0 means "expired, not yet enqueued" and
                                                        // C0/59F2 runs it on the first build -- the old
                                                        // `cd>0?cd:1` guess delayed that by a frame.
    s->wait = waitc; s->waitObj = -1; s->waitPrio = 0; s->callAddr = -1;
    // op-0xB0/0xB2 is an ACTIVE idle-keeper script (ROM C0/3002: drives the idle anim + sets $1A01=1 + CLC so the
    // IP never advances), NOT "no script" -- the candy stand parks Marle there ($1A01 consumed back to 0). Keep her
    // IP so the scripted stand runs (her own freeze + obj0's later re-point), else she falls to the conga as a
    // passive walking follower ("keeping the walking animation while standing still").
    uint8_t opAtIp = (0x12001 + ip < 0x20000) ? w[0x12001 + ip] : 0;   // script base = $7F2001 (= w[0x12001])
    int parkedScript = (opAtIp == 0xB0 || opAtIp == 0xB2);
    // Prio-7 disposition: the ROM has NO idle state -- the runner executes at $1180 every cadence
    // visit; a "done" object parks FOREVER on a forever-yield op (B0/B2 -- every parked object in
    // the oracle states sits on one). A prio-7 object whose IP is on any OTHER real op is a LIVE
    // AUTORUN mid-flight: gato_t0 catches obj0 between the challenge textbox and the battle op
    // (IP on `E3 00`, cadence 4/2 counting, no ticket) -- the old B0/B2-or-ticket-only rule seeded
    // it IDLE, freezing the whole GATO battle script on reload. Treat only a stale RETURN park
    // (op 0x00) as idle; everything else resumes at its IP (our own op-0x00 at prio 7 idles the
    // object again immediately, so a state the ROM parked mid-RETURN converges the same way).
    s->addr = (prio >= 7 && w[0x1A01 + x] == 0 && !parkedScript && opAtIp == 0x00) ? -1 : (ip - hoff);
                                                     // ($1A01 ticket>0) OR parked on an idle-keeper script (op B0/B2):
                                                     // a paced NPC (Marle's 27<->23 autorun MOVETO) runs its mover at
                                                     // prio 7; halting it on reload froze her mid-pace so she never
                                                     // walked into the player's column to bump him (scene-439 "pillar")
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
    // UNITS BRIDGE: the oracle WRAM $1800/$1880 are SUB-PIXELS (1/16 px), but our engine's fx/fy and
    // actors[].x/.y are PIXELS (op 0x8B seeds a->x = tile*16+8 px; the mover's mvComp/16 velocity and
    // atx=fx>>4 tile-compare are all pixel-based). Convert subpx -> px on the way in. Lossless: the
    // fraction is kept in the fx double, and the $1880 foot anchor (low 0xFF) survives as +15.9375 px.
    sc->actors[o].x = px >> 4; sc->actors[o].y = py >> 4; sc->actors[o].hasPos = 1;
    sc->actors[o].facing = w[0x1600 + x];            // $1600,X facing (read by op 0x24/branches)
    sc->fx[o] = px / 16.0; sc->fy[o] = py / 16.0;
    sc->mspd[o] = sc->actors[o].moveSpeed = w[0x1A00 + x];   // $1A00,X move speed (op 0x89): drives mvComp
                                                             // velocity. Default 0x10 != most NPCs (0x20) -> half-speed.
    sc->mode[o] = w[0x1100 + x];                    // $1100,X MOVE-MODE: seed byte-exact from the oracle so the
                                                    // C0/A819 dispatch matches (lead PC=0, scripted NPC=4, empty=0x80).
                                                    // Set at load + never changes during moves -> a copy is exact.
    // `dead` IS $1100,X bit7 -- the byte op 0x0A stores (C0/625F `LDA #80 / STA $1100,X`, the only store
    // besides $1A81 := 0 at 6264) and the first test of the draw-list gate (C0/A88F BMI), the rebuild
    // (C0/B0F3) and the OAM drain (C0/CAD9). It used to be left to the create-time prescan, which runs
    // every object's fn0 BEFORE this seed with whatever story flags the host handed it: under --loadwram
    // Belthasar passed none, so scene 129's obj0 fn0 branched on storyline 0 instead of the state's $12
    // and executed `0A 1E` -- the cutscene controller obj15 was dead from frame 1, the nuns were never
    // hidden and the Naga-ettes never shown (defect #15). The seed is the oracle's own bit.
    sc->actors[o].dead = (w[0x1100 + x] & 0x80) ? 1 : 0;
    sc->actors[o].solid = w[0x1B01 + x] & 1;        // $1B01,X bit0 = SOLID: needed so a loaded NPC blocks + touch-triggers
    // `draw` IS $1A81 -- the script-visibility byte the ops write (C0/4CD5 #01 shown / 4CE0 #00
    // hidden / 4CE6 #80 removed, all three into the one tail C0/4CD9). It used to be seeded from
    // $0F00 instead, and $0F00 is a DIFFERENT quantity: C0/A810 derives it from $1A81 AND the camera
    // window, so every loaded object that merely sat OFF-CAMERA at the seed came back marked hidden.
    // VMDIFF_DRAW is what made that visible -- 15 objects on party3_field, 17 on S5, 9 on the forest
    // state, all reading `ours=0000 oracle=0001` at frame 0 with the object loaded and $1A81 = 1.
    // The consumers that mean "this frame" read $0F00 directly (npcSolidTile / CtSceneActorTouchable);
    // this field is the other half of the pair, and one C field can only carry one of them.
    sc->actors[o].draw = (w[0x1A81 + x] & 0x80) ? 2 : (w[0x1A81 + x] ? 0 : 1);
    if (w[0x0F00 + x] != 0) sc->actors[o].defined = 1;        // an active/placed NPC -> defined (so it counts as placed)
    sc->b00[o] = (int8_t)w[0x10B00 + x];            // $7F0B00,X SIGNED mirror: a mid-step mover resumes its exact
                                                    // lifecycle phase (armed >0 issue frame / 0 clamp / 0x80 vector).
    sc->c1C81[o] = w[0x1C81 + x];   // $1C81,X snap-enable (op 0x0E): obj10/Taban=1 -> op 0x96 walks to the tile
                                    // centre via C0/305D (1px/visit). Without seeding it the diff skips the snap.
    // $1C80,X collision gates (op 0x0D). Seeded byte-exact: field enemies often have 0 (no map bounce)
    // while our old default collMap=1 made pre-battle MOVETOs hang on walls (loc 112 obj14 -> loc[6]
    // never reached 4 -> obj10 IF-poll forever -> D8 never ran -> enemies vanished, no battle).
    { uint8_t c80 = w[0x1C80 + x];
      sc->coll[o] = (c80 >> 1) & 1; sc->collMap[o] = c80 & 1;
      sc->actors[o].coll = sc->coll[o]; sc->actors[o].collMap = sc->collMap[o]; }
    sc->actors[o].cadPause = (w[0x1000 + x] & 0x80) ? 1 : 0;  // $1000 bit7 pause (op 0x0B/0x0C)
    // $1A81 = the SCRIPT visibility flag the activation pass reads. Take it from the WRAM image, NOT
    // from `draw`: draw is seeded from $0F00, which is the camera ACTIVATION LATCH, so an object that
    // is merely OFF-CAMERA at the seed would come back as $1A81 = 0 and could then never activate.
    // (Measured: obj32 at loc $082 has $0F00 = 0 at the seed and the oracle activates it at f73; the
    // draw-derived seed pinned it inactive and --vmdiff VMDIFF_ACT reported the divergence at f74.)
    w8(sc->mem, 0x7E1A81 + (uint32_t)x, w[0x1A81 + x]);
    // In-flight MOVE state: the ROM keeps the per-frame velocity at $1900/$1980 (SIGNED 8-bit subpx/frame)
    // and the step ticket at $1A01. A mover caught mid-step must resume with that exact velocity THIS frame,
    // else our VM re-derives it one frame late (the scene-5 ambient movers' 1-frame phase lag).
    int vx = (int8_t)w[0x1900 + x], vy = (int8_t)w[0x1980 + x], tk = w[0x1A01 + x];
    if ((vx || vy) && tk > 0) {                            // ACTIVE mid-step = a nonzero step TICKET ($1A01),
      sc->mx[o] = vx / 16.0; sc->my[o] = vy / 16.0;        // NOT prio<7: a PACED ambient NPC whose autorun has
      sc->cycles[o] = tk; sc->hasTask[o] = 1;              // RETURNed (prio 7) still drains its mover -- Marle
    }                                                      // pacing in scene 439 (vx=-31, tk=2, prio 7). With
                                                           // the old prio<7 gate she restarted cold (~6f late),
                                                           // so Crono over-descended onto the pillar and wedged.
                                                           // $1900/$1980 with tk==0 = STALE (a finished move).
    // In-flight BLOCKING anime (op 0xAB/0xB7): ROM C0/2E9A parks IP on the op with $1780=2 (mode once),
    // $1781=anim id, $7F0B01,X = 2 while the step ticker C0/CA4B is playing / 1 = "complete on next
    // visit". Without this seed the first AB visit RE-ARMS (animOpState was 0) and -- worse -- a partial
    // restore of state=2 without animOpAddr re-arms EVERY visit (addr mismatch) forever, hanging the
    // whole pre-battle cutscene (loc 112 jogo_trava: obj11 mid-AB 07, obj10 CALL-waits, D8 never runs).
    // One derivation for the seed AND for every anim op the live VM runs -- see ctAnimSyncActor. The
    // mode-2 arm no longer needs the parked-IP guard: the VM's completion test is the byte itself
    // (CtVmAnimFamily reads $7F0B01,X on every 0xAB/0xB7 visit), so a seeded 2 parks and a seeded 1
    // completes on the next visit, as in the ROM.
    ctAnimSyncActor(&sc->actors[o], w, x);
  }
}

// How many objects the DRAW compare actually graded on the last CtSceneDiffWram call, and out of how
// many. A check that pins "0 divergences" is worthless unless it also pins that something was looked
// at -- the ROM class guard (C0/B0F3) is exactly the kind of filter that can quietly swallow every
// object and still report clean. Same discipline as --sweepowregion asserting its own
// differing-nibble count.
// ⚠ THE COUNT IS INCREMENTED BY THE COMPARE ITSELF, not re-derived here. It used to re-run the guard
// in its own loop, and that made the guard UNFALSIFIABLE: deleting it from the compare left both
// states clean AND left this number at 25, so the mutation survived. Two copies of one rule cannot
// grade each other. (It survived for a second reason too, worth writing down: once `draw` is seeded
// from $1A81, an empty slot's leftover agrees with our copy of the same leftover, so no divergence
// appears even ungraded. The count is what makes the guard visible at all.)
static int gDrawGraded = 0, gDrawTotal = 0;
int CtSceneDrawGraded(const CtScene* sc, const uint8_t* w, int* total) {
  (void)sc; (void)w;
  if (total) *total = gDrawTotal;
  return gDrawGraded;
}

// Compare our scheduler state to the oracle WRAM; record up to `cap` divergences (this frame).
// Compares $1C00 priority (always) and $1180 IP (when our object is running a function, prio<7,
// so idle objects whose stale $1180 we don't track don't generate noise). withPos also checks pos.
int CtSceneDiffWram(CtScene* sc, const uint8_t* w, int withPos, CtVmDiff* out, int cap) {
  int n = 0, hoff = CTW_IPOFF(sc);
  gDrawGraded = 0; gDrawTotal = sc->nobj;
  for (int o = 0; o < sc->nobj && n < cap; o++) {
    int x = o * 2;
    CtSS* s = &sc->st[o];
    int oPrio = w[0x1C00 + x] & 0x07;
    int oIp   = w[0x1180 + x] | (w[0x1181 + x] << 8);
    // VMDIFF_ACT=1: also compare the $0F00 ACTIVATION LATCH (the C0/A810 pass). Env-gated and
    // OFF by default so the existing pinned checks keep their exact field set -- this adds a whole
    // new class of per-object divergence and would re-pin every one of them otherwise.
    { static int actOn = -1; if (actOn < 0) { const char* e = getenv("VMDIFF_ACT"); actOn = e && *e != '0'; }
      if (actOn) {
        int myAct = r8(sc->mem, 0x7E0F00 + (uint32_t)x) != 0;
        int oAct  = w[0x0F00 + x] != 0;
        // Class 8 alone is never written by the pass -- C0/A824 `CMP #08 / BEQ ->A83E` skips it
        // outright, so ours is whatever the seed left and comparing it would report the SEED, not
        // the mechanism. Class 7 IS written: C0/A828 `CMP #07 / BEQ ->A86B` goes to `JSR C0/A947`,
        // which runs its own (signed, unclamped) window test and stores 0x80 / STZ. It used to be
        // excluded here alongside 8 on the belief that neither was written, which is exactly the
        // blind spot that let the class-7 latch stay broken at the map origin -- see
        // disasm/notes/field_trigger_latch_a947.md.
        int cls = r8(sc->mem, 0x7E1100 + (uint32_t)x);
        if (cls != 8 && myAct != oAct && n < cap)
          out[n++] = (CtVmDiff){ o, "act", myAct, oAct };
      } }
    // VMDIFF_DRAW=1: THE DRAW COMPARE. `draw` is a C struct field, so a port that shows an object
    // the ROM hides was structurally invisible to every existing compare. The ROM keeps that state
    // in RAM and the predicate is its own:
    //   C0/B0F1  STX $6D / LDA $1100,X / BMI ->B11A / CMP #07 / BEQ ->B11A   ; class skip
    //   C0/B0FC  JSR C0/B127 / BCC ->B11A                                    ; carry clear = NOT drawn
    //   C0/B127  LDA $1A81,X / BEQ ->B190 / BMI ->B190                       ; hidden / removed
    //            ...then the camera-window test; B190 = CLC / RTS
    // The same head gates the drain visit (C0/CAE2, C0/CB12) and the activation pass (C0/A894). So
    // $1A81,X IS the script-visibility state and `draw` is our copy of it: 0 shown -> $01, 1 hidden
    // -> $00, 2 removed -> $80 (C0/4CD5 LDA #01 / C0/4CE0 LDA #00 / C0/4CE6 LDA #80, all three
    // falling into the one tail C0/4CD9). Comparing the ENCODED field rather than our own mirrored
    // byte is deliberate: the byte is already inside the write-mask compare, and what is ungraded is
    // whether the FIELD agrees with the ROM.
    // Env-gated and OFF by default for the same reason VMDIFF_ACT is -- it adds a whole class of
    // per-object divergence and would otherwise re-pin every existing check.
    // ⚠ THE GUARD IS THE ROM'S OWN, and without it this compare reports LEFTOVERS. C0/B0F1 skips an
    // object on `LDA $1100,X / BMI` (class bit7 = an unloaded slot) and on `CMP #07 / BEQ` BEFORE it
    // ever calls C0/B127, and $1A81 is process-lifetime -- no location load clears the page (the four
    // clears are $7E:1D00, $7F:0200, $7E:1900, $7F:0580, wram_load_census.md). So an empty slot keeps
    // whatever the PREVIOUS scene left there. Measured on bedslide: obj2-obj7 all read $1A81 = 01 with
    // $1100 = 80 and $0F00 = 00 -- six objects that do not exist in this scene at all. Comparing them
    // grades the leftover, not the mechanism. Same shape as VMDIFF_ACT's class-7/8 exclusion, and
    // derived the same way: from the instruction that does the skipping.
    { static int drwOn = -1; if (drwOn < 0) { const char* e = getenv("VMDIFF_DRAW"); drwOn = e && *e != '0'; }
      uint8_t cls = w[0x1100 + x];
      if (drwOn && !(cls & 0x80) && cls != 7) {
        gDrawGraded++;                                 // THE guard's only copy -- see CtSceneDrawGraded
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
    if (withPos) {                                   // our PIXELS -> SUB-PIXELS for the oracle compare (round, lossless)
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

// ---- WRITE-MASK BYTE COMPARE (see the long note in ct_vm.h) ----
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

// Every one of the four below is a thin shell over a Mem* core, so the scene-addressed and the
// wram-addressed entry points cannot drift apart: sc->mem IS the CtWram (CtSceneCreateIn), so
// they are the same call with the same buffers, reached two ways. See the header note.
static void wmResetM(const Mem* m, const uint8_t* w) {
  memset(gWmMask, 0, sizeof gWmMask); memset(gWmExcl, 0, sizeof gWmExcl);
  memset(gWmOchg, 0, sizeof gWmOchg);
  for (int i = 0; i < WM_LEN; i++) gWmFirst[i] = -1;
  gWmDropped = 0;
  // The seed is three bulk memcpys, NOT w8 calls, so it leaves the mask empty -- which is what
  // we want: an address that was seeded and never written is not something we CLAIM, it is
  // just an inherited byte. Record the oracle's seed image as the coverage denominator.
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
    if (d) gWmMask[i] |= d;                    // the RAW-pointer paths land here, bit-exactly
    if (w) { uint8_t o = (uint8_t)(gWmSeedO[i] ^ w[i]); if (o) gWmOchg[i] |= o; }
  }
}
void CtVmWmaskFramePost(const CtScene* sc, const uint8_t* w) { wmFramePostM(sc ? sc->mem : NULL, w); }
void CtVmWmaskFramePostW(CtWram* w7e, const uint8_t* w)      { wmFramePostM((const Mem*)w7e, w); }

// Latching byte compare: each address is reported ONCE, at the frame it first diverged, so a
// long run does not drown in repeats of the same defect.
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

// PHASE F, THE BYTE CENSUS. CtVmWmaskStats has always COUNTED the unmodelled bytes -- the ones the
// oracle drove away from the seed and that we never claim -- but a count cannot be given a verdict.
// The census enumerates them on exactly the counter's own condition (`oracle changed it AND we do not
// own it`), so the census and the number in the report can never drift apart.
//
// gWmExcl is reported ALONGSIDE rather than filtered out: a harness-injected byte is still a byte the
// engine does not write for itself, and whether that is a modelling gap or an artefact of the vehicle
// is precisely the verdict the census exists to record.
//
// The census, folded into runs and printed. Shared so the two vehicles cannot drift: ct.exe's
// --vmdiff grades the field VM alone (it links neither field.c nor battle.c), while CT_FIELDLOCK runs
// the SAME mask against the live engine's own $7E image -- and a byte that is unmodelled in the first
// but claimed in the second is a vehicle artefact, not a gap. Only the second can settle that, which
// is why this must be callable from both.
//
// Runs break on a gap OR on a change of the excluded flag, so a harness-injected span never hides
// inside a genuine one.
// $0508-$06FF: the 65816 stack. --bootcheck excludes exactly this window, and for the same reason --
// the port has no hardware stack, so no amount of porting will ever claim these bytes.
static int wmIsStack(uint32_t a) { return a >= 0x7E0508u && a < 0x7E0700u; }

// THE CENSUS'S OWN BLIND SPOT, and it is worth a category rather than a footnote. A region the port
// models but writes SOMEWHERE OTHER than the $7E image this mask watches can never show a claim, so
// it reports as unmodelled forever no matter how correct the port is -- and it looks exactly like a
// real gap. Every entry must name the check that actually grades it; one without a grader would just
// be a way to hide a gap behind a table.
//
// $0700/$0900 measured 2026-09-12 while probing the census's own `$0770` run: CtBuildFieldOAM is
// handed `R + 0x0700` and `R + 0x0900` as SCRATCH (main.c:695, and the comment at main.c:440 says so
// outright -- "the build writes into SCRATCH buffers, not $7E:0700"). The bytes are modelled AND
// graded: oam-build-field pins `10302 bytes compared over 180 frames -> 0 mismatches`.
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
  // THE 65816 STACK IS NOT A MODELLING GAP, and listing it every run buries the residue that is. The
  // port has no 65816 stack at all, so it can never claim $0508-$06FF -- the same window --bootcheck
  // already excludes, for the same reason. Filtered HERE and counted separately, never in
  // CtVmWmaskStats: that counter is pinned by vmdiff-wmask-party3, and moving it would be a silent
  // re-pin of a number another check is holding.
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

// ============================================================================
// UNIFIED FIELD CAMERA (field_camera_unification.md) -- the ONE camera.
// The ROM keeps exactly ONE field camera: the DP=$1D00 scroll ($1D0A/$1D0E coarse 8px-units +
// $1D93/$1D96 phase 0..15 = the rendered scroll camPx = $1D0A*8 + ($1D93&7)), maintained per frame by
// the player-controller chain C0/881E and read by battle staging + the PPU. field.c/player.c own the
// COLLISION half (88E5->88EE->8A6D->9175, leaving the post-collision request in $1D2E/$1D30); this tick
// owns the CAMERA tail: C0/99DE gate (deadzone+edges) -> C0/91AC seek/auto -> C0/93E1 accumulate+commit.
// All state lives in mem.s[$1D..] (F5/F9 + WRAM-seed persistence fall out for free). Derived byte-for-byte
// from the ROM (99DE/9A1F/9A3D/9A60/9A7E, 91AC B.4, 9238/92CA/944B/947D/94AD/94DF, 7506/7553, 885A).
// ============================================================================
static int camS8(uint8_t v) { return (v >= 0x80) ? (int)v - 256 : v; }               // signed byte
static int camGet16(const uint8_t* s, int off) { return s[off] | (s[off + 1] << 8); }
static void camPut16(uint8_t* s, int off, int v) { s[off] = (uint8_t)v; s[off + 1] = (uint8_t)(v >> 8); }

// C0/99DE: kill $2E/$30 per axis via the 4 deadzone/edge helpers. Carry-SET-in-ROM == KILL. Centre
// (128,136) on the lead's on-screen foot; edge-tile phase test caps a far pin; the coarse-tile test
// pins at the mask bound. onX/onY = the lead's on-screen foot byte (world - camPx) & 0xFF.
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

// C0/91AC: op-0xE7 seek servo (OVERWRITES $2E/$30 -- bypasses the gate AND edges, so a scripted seek
// ignores the deadzone) + the $01BC auto-scroll derive. Arrival = coarse==target AND step-counter&0xF==0
// (the E7 targets are always even = 16-aligned; entry zeroes the counters at a 16-aligned camera). Pace
// emerges as exactly 1 px/frame through the commit machine below.
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
    if (s[0x1D01] == 3) { s[0x1D00] = 2; s[0x1D01] = 0; }     // C0/91EC: both arrived -> COMPLETE
  }
  // C0/91F8 auto-scroll derive: only while $01BC armed. $0400 has no field-bank writer (dossier OPEN),
  // so this is dormant in the whole 512-scene corpus; log once if a scene ever arms it.
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

// C0/9238-93E0: accumulate this frame's request + op-E6 drift + auto delta into the 4.4 fraction
// ($3B/$3C, 8-bit signed wrap), extract whole px (truncate toward zero, carry the remainder), then commit
// via the per-pixel loops (phase 0..15, coarse 8px-twins bumped at the 7<->8 and 15<->0 crossings both
// directions, step counters $87/$89 +-1/px). The $74-$77 flags feed the VRAM streamer (presentation-side).
static void camCommit93E1(uint8_t* s) {
  s[0x1D3B] = (uint8_t)(camS8(s[0x1D3B]) + camS8(s[0x1D2E]) + camS8(s[0x1D24]) + camS8(s[0x1D4E]));  // 9238
  s[0x1D3C] = (uint8_t)(camS8(s[0x1D3C]) + camS8(s[0x1D30]) + camS8(s[0x1D25]) + camS8(s[0x1D4F]));
  // ---- C0/9252-92C8: BG2 and BG3, the same accumulate. A layer whose bit is set in $1D36 is DETACHED:
  // it takes its own speed ($37..$3A, from the map header) and its op-E6 drift ($26..$29) but NOT the
  // camera terms $2E/$30 and $4E/$4F.
  //   9252 LDA $36 / BIT #02 / BEQ ->926E
  //   9258 $3D = $37 + $3D + $26         ; 9262 $3E = $38 + $3E + $27        (detached)
  //   926E $3D = $2E + $37 + $3D + $26 + $4E ; 927E $3E = $30 + $38 + $3E + $27 + $4F
  //   928E LDA $36 / BIT #04 ... the same for BG3 into $3F/$40 from $39/$3A and $28/$29
  // Every add is `CLC / ADC` on 8 bits, so the sum wraps -- which the uint8_t cast is.
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
      // ⭐ THE HALF-CAMERA MAP MODE STEPS BG2 ON A 32-PX PHASE. 93F5 `LDA $0BC9 / BMI ->9408` swaps BG2's four committers:
      //   95E3 (X+) per pixel: old = $94; == 1F -> INC $12 / INC $14 / TSB $76 #02 / A = FF; == 0F -> the same, A = 0F;
      //        INC A / STA $94 / AND #01 / BNE -> skip / LDX $8B / INX / STX $8B
      //   9619 (X-): old == 0 -> DEC $14 / DEC $12 / TSB $77 #02 / A = 20; == 10 -> the same, A = 10; DEC A / STA $94 /
      //        even -> DEX $8B        964D / 9683: the same on $97, $16/$18, $74/$75 and $8D.
      // So the counter moves on every OTHER pixel of BG2's motion -- half the camera -- and the window a chip per 16 phase
      // steps. MEASURED on the attract's loc 0A5 ($0BC9 = 80): $1D89 = FECA (-310) with $1D8D = FF65 (-155) on the
      // cartridge at f3977, where the port's full-rate counter read FECA. BG3 keeps its ordinary committers (96B7..) and
      // no layer's normal arm needs the phase for its counter, so only these two are kept.
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
  // C0/93CC-93DE, the end of the same extraction: op 0xE6's DURATION. `LDA $4D / BEQ ->93E0 / DEC $4D / BNE ->93E0 /
  // STZ $24 / STZ $25 / STZ $26 / STZ $27 / STZ $28 / STZ $29` -- a non-zero count runs the three layers' drift for that
  // many frames and then stops it; zero leaves it running. (op 0xE6's own comment called this consumer un-located.)
  // MEASURED on the attract's loc 112 (--monitor 7E1D0E:2,7E1D24:2 --writer): the camera's `$1D24 = 0020` drift steps
  // $1D0E up to $36 and C0:93D8 zeroes it at f2649; without the count the port panned on to $44 and past the scene.
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

// C0/885A align machine (state $0138): glide 1px/f per axis TOWARD THE LEAD's side until both phases are
// 0 (camera 16-aligned), <=15px per axis; input (88EE) is disabled while it runs. Armed only by the D8
// battle entry (C0/1770, G.1.9) -- dormant in normal field play. Runs steps 5-6 (91AC+93E1) itself.
static void camAlign885A(CtScene* sc, int onX, int onY) {
  uint8_t* s = sc->mem->s;
  if (s[0x0138] == 1) {                                       // state 1: CHOOSE the per-axis direction
    s[0x1D2E] = s[0x1D30] = s[0x1D32] = s[0x1D33] = 0;
    if (s[0x1D93] != 0) s[0x1D2E] = ((onX & 0xFF) >= 128) ? 0x10 : 0xF0;
    if (s[0x1D96] != 0) s[0x1D30] = ((onY & 0xFF) >= 136) ? 0x10 : 0xF0;
    if (s[0x1D93] == 0 && s[0x1D96] == 0) { s[0x0138] = 0; return; }   // already aligned -> done
    camSeek91AC(sc); camCommit93E1(s); s[0x0138] = 2;
  } else if (s[0x0138] == 2) {                                // state 2: RUN (glide until both phases 0)
    s[0x1D32] = s[0x1D33] = 0;
    if (s[0x1D93] == 0) s[0x1D2E] = 0;
    if (s[0x1D96] != 0) { camSeek91AC(sc); camCommit93E1(s); return; }
    s[0x1D30] = 0;
    if (s[0x1D93] != 0) { camSeek91AC(sc); camCommit93E1(s); return; }
    s[0x0138] = 0;
  } else s[0x0138] = 0;
}

// The per-frame camera pipeline (C0/881E camera tail), first in CtSceneTick so EVERY VM host drives it
// (interactive field, headless --vm/--vmdiff, --battletest). The collision half already ran in the field
// host (its post-collision request handed in via CtSceneCameraInput); headless hosts leave the request 0,
// so the camera only moves via scripted op-0xE7/E6 or a WRAM-seeded mid-seek -- matching the input-less
// oracle. No-op-to-the-scroll when the request and all deltas are 0 (a still camera stays byte-identical).
void CtSceneCameraTick(CtScene* sc) {
  if (!sc) return;
  uint8_t* s = sc->mem->s;
  int camPxX = camGet16(s, 0x1D0A) * 8 + (s[0x1D93] & 7);     // the render camera = the deadzone witness base
  int camPxY = camGet16(s, 0x1D0E) * 8 + (s[0x1D96] & 7);
  int onX = sc->camHaveLead ? (sc->camLeadX - camPxX) : 128;  // lead on-screen foot (world - camPx); 128/136
  int onY = sc->camHaveLead ? (sc->camLeadY - camPxY) : 136;  //   = centre when no lead (headless: no gate motion)
  if (s[0x0138] != 0) { camAlign885A(sc, onX, onY); return; } // align machine owns the camera (C.2/C.3)
  s[0x1D2E] = (uint8_t)sc->camReqX;                           // $2E/$30 = the post-collision request (88EE+8A6D)
  s[0x1D30] = (uint8_t)sc->camReqY;
  camGate99DE(s, onX, onY);                                   // deadzone + camera-edge kill
  camSeek91AC(sc);                                            // op-0xE7 seek override + auto derive
  camCommit93E1(s);                                           // accumulate + commit (phase/coarse twins)
}

// C0/7506 (X) / C0/7553 (Y) scene-entry init: cam = clamp((arrTile-8)*16, mask) 16-aligned, phases +
// step counters + fractions zeroed. Input = the ARRIVAL TILE ($0102/$0103), NOT the lead pixel. Called by
// the field host on a fresh scene entry (a WRAM/state seed carries the live camera instead). Note the Y
// far-pin threshold is $1D1D-6 (asymmetric vs X's -8) and the far pins sit at mx1-256 / my1-224 = one px
// past the scroll-reachable maxima (an entry camera can legally rest there; walking can't reach it).
void CtSceneCameraEntry(CtScene* sc, int arrTileX, int arrTileY) {
  if (!sc) return;
  uint8_t* s = sc->mem->s;
  int x0 = s[0x1D1A], x1 = s[0x1D1B], y0 = s[0x1D1C], y1 = s[0x1D1D];
  int caxX;
  if      (arrTileX <= x0 + 8) caxX = x0 * 2;                 // left pin  (7516/7522)
  else if (arrTileX >  x1 - 8) caxX = (x1 + 1) * 2 - 0x20;    // right pin (753E)
  else                         caxX = (arrTileX - 8) * 2;     // normal    (752E)
  int caxY;
  if      (arrTileY <= y0 + 8) caxY = y0 * 2;                 // top pin   (7563/756F)
  else if (arrTileY >  y1 - 6) caxY = (y1 + 1) * 2 - 0x1C;    // bottom pin (7587)
  else                         caxY = (arrTileY - 8) * 2;     // normal    (7577)
  // ...AND NOTHING CLAMPS THE PINS AFTERWARDS. A room SHORTER than the screen (13 tiles: $1D1D - $1D1C = 12) has its
  // bottom pin ($1D1D+1)*2-$1C one coarse ABOVE its top pin, and 757B-7596 stores it as it is -- the camera then rests
  // 8 px above the top bound. Eight rooms in the ROM's 512 records (F6:000A-000D): 00E (y 2..14 -> $0E = 2, not 4),
  // 088 and 08B (34), 089 099 09A 0F9 0FA (98); no record makes either pin negative, and no X pin crosses its edge.
  // MEASURED: the user's own cartridge save in loc 00E (Truce mayor's 2F, aux "caixa_dialogo.state", Crono at $1880 =
  // 0B3F): $1D0E = 02. The port's "guard" here pinned it to $1C*2 = 04 -- 16 px low -- and the box of the NPC above
  // Crono (obj10 at y 159, C0/1FCD's speaker test against 128) moved from the top to the bottom.
  camPut16(s, 0x1D0A, caxX); camPut16(s, 0x1D0C, caxX + 0x20);
  camPut16(s, 0x1D0E, caxY); camPut16(s, 0x1D10, caxY + 0x1C);
  s[0x1D93] = s[0x1D96] = 0;                                  // phases 0 (16-aligned)
  camPut16(s, 0x1D87, 0); camPut16(s, 0x1D89, 0);             // step counters zeroed at entry
  s[0x1D3B] = s[0x1D3C] = 0;                                  // fraction accumulators zeroed
  s[0x1D00] = 0; s[0x1D01] = 0; s[0x0138] = 0;                // no seek / no align in progress
  sc->bgD2x = sc->bgD2y = sc->bgD3x = sc->bgD3y = 0;          // every layer built at this camera (C0/0A50)
  s[0x1D24] = s[0x1D25] = s[0x1D4E] = s[0x1D4F] = 0;          // op-E6 / auto deltas cleared
  // ...and the BG2/BG3 half of the same block. MEASURED on the oracle across two loads:
  // `--monitor 7E1D87:2,...,7E1D91:2` zeroes all SIX per-layer step counters together at the load
  // of loc 0AD, and `7E1D35:6` zeroes the layer-scroll setup together at the load of 01C (leaving
  // scene 366, whose BG2/BG3 speeds were F0/C0). The md-header decode then writes only what the new
  // map asks for (CtSceneLayerScrollLoad), so a speed can never leak from the previous location.
  s[0x1D26] = s[0x1D27] = s[0x1D28] = s[0x1D29] = 0;          // op-E6 BG2/BG3 drift
  s[0x1D3D] = s[0x1D3E] = s[0x1D3F] = s[0x1D40] = 0;          // BG2/BG3 4.4 fraction accumulators
  s[0x1D35] = s[0x1D36] = 0;                                  // free-scroll / detached-from-camera bits
  s[0x1D37] = s[0x1D38] = s[0x1D39] = s[0x1D3A] = 0;          // BG2/BG3 own speeds
  camPut16(s, 0x1D8B, 0); camPut16(s, 0x1D8D, 0);             // BG2 step counters
  camPut16(s, 0x1D8F, 0); camPut16(s, 0x1D91, 0);             // BG3 step counters
}

// C0/A385-A4FB -- THE LOCATION LOAD'S LAYER-SCROLL SETUP, off the decompressed map header at
// $7E:B500 (md[0..5]). DP = $1D00. Three bytes decide whether BG2/BG3 scroll on their own:
//   A385 LDA $7EB501 / STA $50                         ; md[1]
//   A3A7 LDA $50 / BIT #20 / BNE -> LDA #04 / STA $36   ; bit $20 -> BG3 DETACHED from the camera
//        (else STZ $36) ; A3B5 BIT #10 / BEQ / ORA #02  ; bit $10 -> BG2 detached
//   A459 LDA $36 / STA $35
//   A45D LDA $7EB502 / BEQ ->A4AD                      ; md[2] -- BG2's own speed, when non-zero:
//        AND #07 / BEQ / TAY / LDA #01 / (DEY/BEQ/ASL A/BRA) / STA $37   ; X = 1 << (code-1)
//        BIT #08 / BEQ / LDA $37 / EOR #FF / INC A / STA $37              ; bit 3 = X negative
//        AND #70 / BEQ / LSR*4 / (same) / STA $38                          ; Y code, bit 7 = Y negative
//        A4A5 LDA $35 / ORA #02 / STA $35
//   A4AD LDA $7EB503 ...                                ; md[3], the same encoding -> $39/$3A, $35 |= 4
// Speeds are signed 4.4 px/frame, consumed every frame by C0/9252/928E (camCommit93E1).
// VERIFIED against the live bytes, not only the listing: loc 0AD md[2]=$56 -> $1D37=20 $1D38=10
// ((+2,+1) px/f; the oracle's BG2 scroll went (182,91) -> (362,181) in 90 frames), loc 16E
// md[2]=$D0 md[3]=$F0 -> $1D38=F0 $1D3A=C0, $1D36=02 (measured: BG1 -1, BG2 -2, BG3 -5 px/f).
void CtSceneLayerScrollLoad(CtScene* sc, uint8_t md1, uint8_t md2, uint8_t md3) {
  if (!sc) return;
  uint8_t* s = sc->mem->s;
  s[0x0BCA] = (md1 & 0x80) ? 0x01 : 0x00;                     // A38B BPL / LDA #01 / STA $0BCA (BG3 present)
  s[0x0BC9] = (md1 & 0x40) ? 0x80 : 0x00;                     // A397 BIT #40 / LDA #80 / STA $0BC9 (the half-camera map mode)
  // C0/74AC-74C5, which the entry (74A6) runs after 7506/7553 when `$0BC9` is negative: BG2's streamer window at HALF the
  // camera, `$12 = ($0A >> 1) & FE`, `$14 = $12 + 20`, `$16 = $0E >> 1`, `$18 = $16 + 1C` (chip units). The port decodes the
  // header after its camera entry, so the window is taken here, from the camera that entry just placed.
  if (s[0x0BC9] & 0x80) {
    s[0x1D12] = (uint8_t)((s[0x1D0A] >> 1) & 0xFE); s[0x1D14] = (uint8_t)(s[0x1D12] + 0x20);
    s[0x1D16] = (uint8_t)(s[0x1D0E] >> 1);          s[0x1D18] = (uint8_t)(s[0x1D16] + 0x1C);
  }
  s[0x1D94] = s[0x1D97] = 0;                                  // BG2's phases, zeroed with the rest of the $1D page
  s[0x1D36] = (md1 & 0x20) ? 0x04 : 0x00;                     // A3A9-A3B3
  if (md1 & 0x10) s[0x1D36] |= 0x02;                          // A3B7-A3BF
  s[0x1D35] = s[0x1D36];                                      // A459
  for (int layer = 0; layer < 2; layer++) {                   // A45D (BG2, md[2]) / A4AD (BG3, md[3])
    uint8_t b = layer ? md3 : md2;
    if (!b) continue;                                         // BEQ -- the arm is skipped outright
    int xo = layer ? 0x1D39 : 0x1D37, yo = xo + 1;
    int xc = b & 0x07, yc = (b & 0x70) >> 4;
    if (xc) { uint8_t v = (uint8_t)(1 << (xc - 1)); if (b & 0x08) v = (uint8_t)(-v); s[xo] = v; }
    if (yc) { uint8_t v = (uint8_t)(1 << (yc - 1)); if (b & 0x80) v = (uint8_t)(-v); s[yo] = v; }
    s[0x1D35] |= (uint8_t)(layer ? 0x04 : 0x02);              // A4A5 / A4F5
  }
}

// BG2/BG3 sample offsets relative to BG1, in pixels: each layer's step counter minus BG1's. All six
// counters start at 0 on a load, and on the oracle all three layers' PPU scrolls sit at the SAME base
// once their counters are subtracted (loc 16E f450: 932+92 = 839+185 = 560+464 = 1024, with the
// PPU one frame behind the counters), so the difference IS the layer's offset from the camera.
//
// ⭐ A DETACHED LAYER IS NOT ON THE MAP'S CAMERA AT ALL. The difference above holds for a layer that
// takes the camera terms. One whose bit is set in $1D36 never did, so it is drawn at screen + its own
// counter from a base of 0 -- the camera never enters it. MEASURED on the attract's loc 01C (md[1] = $B0,
// both detached, counters 0): the oracle's camera is (16,16) and its BG2 draws with scroll (0,0) (the
// latch AND the HDMA table at $7F:12E6 -> $1514 both read 0), and the port, sampling BG2 at the camera,
// put the mountain ~32 px higher and ~16 px left. The same rule is consistent with the other two
// detached cases: loc 0AD's BG2 scroll equals its counter exactly ((182,91) at +91 frames), and loc 16E's
// BG2 is 839 + 185 = 1024 == 0 mod its 256-px wrap. The field crops one canvas at the camera, so a
// detached layer's sample offset is counter - camera.
// ⭐⭐ ...AND THE SELECTOR IS $1D35, NOT $1D36 -- it is the location load's TILEMAP BUILD that decides where a layer's
// origin is. C0/78EC (BG2) / C0/7CB5 (BG3) test `$1D35 BIT #02` / `BIT #04`: set, they upload the MAP ITSELF from
// metatile (0,0) (C0/794E / C0/7CE7, $1000 bytes, both halves of the 64x32 tilemap FD/C0D7 sets up); clear, they upload
// the 32x32 window at the ENTRY CAMERA ($1D0A/$1D0E, C0/7913 / C0/7CCD). The HDMA scroll tables hold the step counters
// themselves (FD/C1EE channels $0F/$11; loc 1B0: BG2 V = $1D8D), so a map-built layer shows map pixel = counter and a
// camera-built one map pixel = entry camera + counter. $1D35 is $1D36 (detached, md[1] bits 5/4) OR "has its own speed"
// (A4A5/A4F5), so the two only differed for a layer with a speed and no detach bit -- and there only where the entry
// camera is not (0,0): loc 1B0 (BG2 md[2] set, entry camera (0,304)) sampled its fire 304 px down, into the map's empty
// half, where the cartridge's BG2 shows the map's top rows.
void CtSceneLayerOffsets(const CtScene* sc, int* bg2x, int* bg2y, int* bg3x, int* bg3y) {
  if (!sc) return;
  CtSceneLayerOffsetsOf(sc, sc->mem->s, bg2x, bg2y, bg3x, bg3y);
}
// The same read of an object page kept elsewhere: under a battle the field's $0920-$1DFF is C0/0918's copy at $7F:2000
// (`page` = that copy - $0920, so page[$1D8B] is $7F:346B), the one C0/2C41 keeps scrolling -- the live $7E page is the
// battle's.
void CtSceneLayerOffsetsOf(const CtScene* sc, const uint8_t* s, int* bg2x, int* bg2y, int* bg3x, int* bg3y) {
  if (!sc || !s) return;
  int b1x = (int16_t)camGet16(s, 0x1D87), b1y = (int16_t)camGet16(s, 0x1D89);
  int cx = camGet16(s, 0x1D0A) * 8 + (s[0x1D93] & 7), cy = camGet16(s, 0x1D0E) * 8 + (s[0x1D96] & 7);
  int det2 = s[0x1D35] & 0x02, det3 = s[0x1D35] & 0x04;
  // ...and a camera-built BG2 in the half-camera map mode (`$0BC9` bit 7) is built from C0/792F's window, `$12 = ($0A >> 1)
  // & FE` / `$16 = $0E >> 1` of the ENTRY camera (C0/74B1), so its map pixel is that window's origin plus its counter.
  // Loc 0A5: entry camera row 48 -> window row 24 = 192 px, counter -155 -> the moon's sky from map row 37, where the
  // ordinary rule (entry camera + counter) read 229.
  if (!det2 && (s[0x0BC9] & 0x80)) {
    int ex = (cx - b1x + sc->bgD2x) >> 3, ey = (cy - b1y + sc->bgD2y) >> 3;   // BG2's build camera in chips (the entry's,
                                                                             //   or a rebuild's -- see below)
    if (bg2x) *bg2x = ((ex >> 1) & 0xFE) * 8 + (int16_t)camGet16(s, 0x1D8B) - cx;
    if (bg2y) *bg2y = (ey >> 1) * 8 + (int16_t)camGet16(s, 0x1D8D) - cy;
  } else {
  // ...and a camera-built layer whose tilemap was REBUILT at a later camera than BG1's (op 0xE5's style branch: C0/78EC /
  // 7CB5 build at the live $1D0A/$1D0E and C0/74E8 / 74F7 zero that layer's counters, ctXferReorigin) sits on its own
  // build camera: bgD2*/bgD3* = its build camera minus BG1's, 0 from the entry until such a rebuild.
  if (bg2x) *bg2x = (int16_t)camGet16(s, 0x1D8B) - (det2 ? cx : b1x - sc->bgD2x);
  if (bg2y) *bg2y = (int16_t)camGet16(s, 0x1D8D) - (det2 ? cy : b1y - sc->bgD2y);
  }
  if (bg3x) *bg3x = (int16_t)camGet16(s, 0x1D8F) - (det3 ? cx : b1x - sc->bgD3x);
  if (bg3y) *bg3y = (int16_t)camGet16(s, 0x1D91) - (det3 ? cy : b1y - sc->bgD3y);
}

// The field host hands us this frame's post-collision scroll request ($1D2E/$1D30 units, 1/16px signed)
// + the lead's start-of-frame foot (px), consumed by CtSceneCameraTick. leadValid=0 => no gate motion.
void CtSceneCameraInput(CtScene* sc, int reqX, int reqY, int leadX, int leadY, int leadValid) {
  if (!sc) return;
  sc->camReqX = reqX; sc->camReqY = reqY;
  sc->camLeadX = leadX; sc->camLeadY = leadY; sc->camHaveLead = leadValid;
}

// Field render read: the ONE camera in pixels. camPx = $1D0A*8 + ($1D93&7) (same Y). Since entry/D8 leave
// the camera 16-aligned, $1D93&7 == $1D87&7 == camPx mod 8 (what oam.c / battle staging already read).
void CtSceneCameraPx(const CtScene* sc, int* camx, int* camy) {
  if (!sc) return;
  CtSceneCameraPxOf(sc->mem->s, camx, camy);
}
void CtSceneCameraPxOf(const uint8_t* s, int* camx, int* camy) {
  if (!s) return;
  if (camx) *camx = camGet16(s, 0x1D0A) * 8 + (s[0x1D93] & 7);
  if (camy) *camy = camGet16(s, 0x1D0E) * 8 + (s[0x1D96] & 7);
}

// ---- THE MESSAGE BOX MACHINE, C0/1F87 (dispatch on $0129; D = $0100) ------------------------------------
// States, from the dispatch table C0/1F9x and the handlers (dis.py 1FB4 0x90, 2032 0x80):
//   1, 8      1FB4  JSR C0/20B2 (decode the page's text -- the C2 text engine's buffers, not modelled
//                   here) / INC $29 / STZ $31 / LDA #01 / STA $36
//   2-4, 9-B  1FC0  INC $29 / INC $31
//   C         1FC5  LDA #06 / STA $29 / JSR C0/2126 (the typewriter resumes on the new page)
//   5         1FCD  no window yet ($26 == 0): $26 := $30 (1 top / 2 bottom) or, for 0 (auto), the
//                   SPEAKER's screen half -- `LDX $2E / LDA $0A80,X / BPL -> 2` (a screen Y below 128
//                   puts the box at the BOTTOM); then STZ $27 / STZ $31 / STZ $33 / STZ $36 / JSR C0/20F2.
//                   window up: the OPEN ramp 1FF8 -- `LDA #29 / DEC A` (= $28) then four `INC $27 / CMP $27 /
//                   BEQ ->2045` steps; reaching $28 runs 2045 `LDA #06 / STA $29 / JSR C0/2126` THIS frame.
//   6         2018  JSR C0/2126 -- the typewriter, one character per frame. The ROM draws with the C2 text
//                   engine; the host reveals its own decoded string at the same rate and reports the end
//                   through CtSceneTextboxRevealDone (21D8: `$29 := 7 / $36 := $81`, 21C2: `$34 := FFFF`).
//   7         201C  waiting: $0134 == FFFF holds until the A handler spends it (C0/1ABD), $0134 == 0
//                   advances (2058: close 0D, or page 8 -- the host decides, it knows the text).
//   D         2032  the CLOSE ramp: four DECs and only four tests, the last one untested
//                   (A5 27 F0 17 | C6 27 F0 13 | C6 27 F0 0F | C6 27 F0 0B | C6 27 | 6B); at 0: 204D
//                   `STZ $29 / STZ $26 / LDA #20 / TRB $54`.
// MEASURED (scene 7's obj13, A at iteration 3): $0129 = 1 at f3, 2/3/4/5 at f4-f7, $0126 = 1 at f8 (the
// resolve), $0127 4..$28 over f9-f18 with $0129 = 6 at f18, 7 (+ $0134 = FFFF) at f73 for a 55-character
// string, 0D on the press at f75, 0 at f86.
// ---- THE WINDOW WIPE: op 0xFF 0x92-0x95 (C0/405D, C0/40EF, C0/40B3, C0/4145) and its frame, C0/21E1 -> C0/22D8 -> C3/01E4 ----
// The op arms it and PARKS (C0/400C's CLC ends the visit, the ip stays) until the effect is over, then completes:
//   $0139 = 0:   `$0350` busy -> park; else $0351 = 0, $0350 = mode, $0352/$0354 = $7F:17C8, $0139 = 3, $013C = kind; park
//   $0139 = 3:   `$0350` still set -> park; else complete and INX INX
//   92 ($0350 = 1, $013C = 0) / 93 ($0350 = 2, $013C = 1): complete = $0139 = 0, $7F1522/23 = 0, $0BD9/DA/DB/DD = 0, TRB $28 #80
//   94 ($0350 = 1, $013C = 3) / 95 ($0350 = 2, $013C = 2): complete = STZ $19 (black), $0139 = 0
// MEASURED (saves/attract_cena_inicial.state, --monitor 7E0119:1,7E0350:2,7E0139:1 --writer): loc 16E's `FF 94` at f578; C0:2306
// holds $0119 = 0F and C3:0237 counts $0351 01..1F over f579-f609; at f610 C3:023F clears $0350 and the same frame's visit
// completes ($0119 = 00 at C0:40C4, $0139 = 00 at C0:40C6). Loc 01C's `FF 93` at f769, $0351 01..1F f770-f800, done f801.
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
// C0/00B1 JSR C0/21E1: `LDA $39 / BEQ / DEC / ASL / TAX / JSR ($21EE,X)`, and entry 3 is C0/22D8 -- once per main-loop frame,
// BEFORE the object pass (C0/00B7). While $0350 is set it arms the window shadows, steps C3/01E4 (DP $0300) and holds
// `$19 = 0F`; after, with $013C < 2 (92/93), it clears them again (C0/2307).
// C3/01E4 runs NATIVE (wipe.c CtWipeRoutine) over this WRAM. Its seed C3/02DD folds the beam counters, which read the rule's
// virtual beam ($213C = $0351, $213D = 00; CLAUDE.md, the zelda3 model). The window is drawn by field.c from the tables the
// routine writes, through the HDMA channel FD/C1EE programs ($2128/$2129 by $013C bit 0) and the shadows C0/22D8 sets.
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
      // No ROM (a harness without one): the routine cannot run. What the op waits on -- the count -- is kept so the flow
      // still completes; the tables stay as they are. Reported once.
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

// ---- THE IRIS: C0/21E1's entries 1 and 2 (disasm/notes/field_iris_window.md) -------------------------------------------
// Op 0xFF 90 (C0/4036) stores the centre and the target radius ($013A/$013B/$013D), zeroes $013C and sets $0139 = 1; op
// 0xFF 91 (C0/4055) sets $0139 = 2. Every main-loop frame C0/21E1 dispatches on $0139 (table C0/21EE: 220E, 2269, 22D8, 232A):
// the open steps the radius $013C up by one a frame until it reaches $013D, the close down to 0, and each step rebuilds
// the circular WINDOW 1 table through C3/0077 into the buffer $0153 names ($7F:17C8 set, $7F:1AC8 clear) -- the table HDMA
// channel 5 hands to WH0/WH1 line by line, the one the frame shows (measured: the displayed window is the buffer written
// in the same frame). W12SEL $0BD9 = $22 gives window 1 to BG1 and BG2; TMW $7F:1522 = $17.

// C3/0077 (entered through `JSL C3/0008` with A = the argument; both field callers load #00). DP = $0300: $0350 the
// centre X, $0352 the centre Y, $0354 the radius (their high bytes zeroed first), $0356/$0358 the table's address and
// bank. A midpoint circle writes each line's (left, right) pair for the top half, the bottom half is copied from it
// mirrored, and the lines above and below are filled with the empty pair (01, 00) by an overlapping MVN. Ported one
// instruction at a time, the carry included: the ROM leaves several ADC/SBC without CLC/SEC, and the top rows' right edge
// is one pixel wider for it (the oracle's line 72 at radius 31 around x 135 is 130..141).
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
// The port of C3/0077 on a flat 128 KB image, for ct.exe --sweepiris (the routine's own byte-exact grade).
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

// ---- THE CIRCLE AS THE COLOUR WINDOW: C0/21E1's entries 4 and 5 (`FF 97` / `FF 98`) ------------------------------------
// C0/232A (entry 4) is the iris's opening (C0/220E) with the window made the COLOUR window: 2330-234A TMW $7F:1522/1523 =
// 17, W12SEL / W34SEL $0BD9 / $0BDA = 00, WOBJSEL $0BDB = 20 (window 1, not inverted), CGWSEL $0BDE = 12 (math only inside
// it, the sub screen added); the centre $3A / $3B and the radius $3C into $0350 / $0352 / $0354, `TSB $28` #20 (HDMA channel
// 5 on), the buffer by $53 ($7F:17C8 set, $7F:1AC8 clear) into $0356 / $0358, and `LDA #01 / JSL C3/0008`: C3/0077 with
// ARGUMENT 1 -- each pair of lines copied once, the circle half as tall (an ellipse). C0/238A (entry 5) steps the radius
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

// ---- THE FIXED CIRCLES: C0/21E1's entries 7 / 8 (`FF 99`) and 9 / 0A (`FF 9A`) ------------------------------------------
// Each is two frames of set-up -- `LDA $39 / CMP #07|#09 / BNE / INC $39`, else `STZ $39`: the same routine runs at 7 and
// at 8 (9 and 0A), so the circle lands in both buffers of the $53 pair -- and the window then STAYS (nothing in them
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

// ---- THE QUAD WINDOW: C0/21E1's entries 6 and 0E (the combat tutorial's yellow line) ---------------------------------------
// Op 0xFE (C0/3F26, move.c CtVmScreenFxOp) seeds eight 8.8 channels at $7F:1CE9.. (deltas at $7F:1CF9.., countdown $7F:1CE8)
// and sets $0139 = 6; `FF 9C` (C0/41C4) sets it to 0E. Entry 6 (C0/2409) is one frame of the line: the channels' high bytes
// become four vertices at $0360-$0367, C3/0EFA fills WINDOW 1's table with the quad between them, and the channels step.
// The window is the COLOUR window (WOBJSEL $0BDB = $20, CGWSEL $0BDE = $12: math only inside it) and the colour is the
// fixed one op 0xF1 put in $0121 (FD/C6F6 copies $0BE0 / $0121 into channel 6's CGADSUB / COLDATA rows): `F1 7F 00` adds
// (31, 31, 0) to BG1-3 and the OBJs of palettes 4-7 -- the yellow. MEASURED on the cartridge (saves/field_re/
// scene254_tutorial.state, the A every 32 frames, f1060 `--hdma` with HDMA_TRACE_BG): lines 80-126, window 1 from 72..80
// to 251..251, w1en 20, prevent 1, CGADSUB 17, fixed (31,31,0).

// C3/0EFA (`JSL C3/0011`, DP $0300): the vertices are ($60,$61) ($62,$63) ($64,$65) ($66,$67); the walk starts at the one
// with the least Y (a tie goes to the later one: `CMP / BCC`) and rasterizes the four edges in order through C3/1025, each
// writing one byte a line into the (left, right) pairs of the table [$50] -- the edges going DOWN into the first byte, and
// from the first edge going UP (`INC $50`, $6A = its top) into the second. Then line 0 takes the empty pair 01 00, an MVN
// carries it from the bottom line $6A to line $FF and another from line 0 down over line $68 -- the top vertex's own line
// is emptied too. Ported one instruction at a time with the carry the loops keep (C3/1076 / 109F enter their ADC on the
// borrow of the SBC before), graded by ct.exe --sweeppoly.
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
        polyW16(m, 0x50, (uint16_t)(polyD16(m, 0x50) + 1));                  // 0F94 INC $50 -- the second byte
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
// The port of C3/0EFA on a flat 128 KB image, for ct.exe --sweeppoly (the routine's own byte-exact grade).
void CtPolyQuadImage(uint8_t* w128) {
  if (w128) ctPolyQuad((Mem*)w128);
}
// C0/2409 -- entry 6, one frame of the line.
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
// C0/23E2 -- entry 0E (`FF 9C`): the line's teardown.
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

// C0/2126's HEAD, 212E-216A: the code the last render stopped on ($0215) moves the LINE cursor $0131 before the next
// render -- a page code (09-0C) restarts it (2161 `LDA #00`), a line code (05-08) steps it (215B `LDA $31 / CMP #03 /
// BNE -> INC A`, so line 3 wraps to 0); both `STZ $32` and clear the line buffer (JSR C0/20B2). Any other code leaves it.
static void tbLineStep(Mem* m, int code) {
  if (code >= 0x09 && code <= 0x0C) w8(m, 0x7E0131, 0);
  else if (code >= 0x05 && code <= 0x08) { int l = r8(m, 0x7E0131); w8(m, 0x7E0131, (uint8_t)(l == 3 ? 0 : l + 1)); }
}
void CtSceneTextboxLineStep(CtScene* sc, int code) { if (sc) tbLineStep(sc->mem, code); }

// C0/2096 set $0129 = 6 THIS frame (a pause ran out): C0/2126 types from the next. One frame long, so it is not part of
// CtScene (whose layout the CTFS snapshots carry).
static int gTbResumeArmed = 0;
static void tbMachineTick(CtScene* sc) {
  Mem* m = sc->mem;
  uint8_t st = (uint8_t)r8(m, 0x7E0129);
  gTbResumeArmed = 0;
  if (st == 0) return;
  // $0131 = THE LINE CURSOR. Its writers are these handlers (1FB9 STZ, 1FC2 INC, 1FEE STZ) and the typewriter's head
  // (tbLineStep: here at 1FC5's JSR C0/2126, the host's typewriter for every other iteration). MEASURED on the tent
  // keeper (--monitor 7E0129,7E0131): f12..f15 states 2..5 with $0131 0,1,2,3 (1FB4 then three 1FC0s), f16 the window
  // is created and $0131 = 0 (1FEE), then 1, 2, 3 at the three line breaks. $0136 = the NMI's upload request (C0/EB79
  // -> C0/6ECB copies the line buffer into window line $0131 while it is non-zero): 1FBB = 1, 1FF2 = 0 here, 21A3 = 1
  // in the host's typewriter, 21DC = $81 in CtSceneTextboxRevealDone, and the NMI's own EB7F STZ (field.c).
  if (st == 8 && sc->tbPageArmed) {
    // the host's press turned the page THIS frame -- the ROM's 2086 `LDA #08 / STA $29 / RTL` ends the machine's frame,
    // so 1FB4 (the clear, $0131 = 0) is the NEXT frame's. MEASURED on s439 (field-textbox-typewriter-vs-oracle): the
    // oracle reads $0129 = 08 / $0131 = 02 on the press frame f101; running 1FB4 here put ours one state ahead (09 / 00).
    sc->tbPageArmed = 0;
  } else if (st == 1 || st == 8) {                            // 1FB4 (JSR C0/20B2: the line buffer cleared -- the host's)
    w8(m, 0x7E0129, (uint8_t)(st + 1));
    w8(m, 0x7E0131, 0);                                       // 1FB9 STZ $31
    w8(m, 0x7E0136, 1);                                       // 1FBB LDA #01 / STA $36 -- the NMI uploads the blank lines
  } else if ((st >= 2 && st <= 4) || (st >= 9 && st <= 0xB)) {  // 1FC0
    w8(m, 0x7E0129, (uint8_t)(st + 1));
    w8(m, 0x7E0131, (uint8_t)(r8(m, 0x7E0131) + 1));          // 1FC2 INC $31
  } else if (st == 0xC) {                                     // 1FC5
    w8(m, 0x7E0129, 6);
    tbLineStep(m, r8(m, 0x7E0215));                           // 1FC9 JSR C0/2126 -- the page's own code
  } else if (st == 5) {                                       // 1FCD
    if (r8(m, 0x7E0126) == 0) {
      int req = r8(m, 0x7E0130), pos;
      if (req == 1 || req == 2) pos = req;
      else pos = (r8(m, 0x7E0A80 + r8(m, 0x7E012E)) & 0x80) ? 1 : 2;   // 1FDD LDX $2E / LDA $0A80,X / BPL
      w8(m, 0x7E0126, (uint8_t)pos);
      w8(m, 0x7E0127, 0);
      w8(m, 0x7E0131, 0);                                     // 1FEE STZ $31
      w8(m, 0x7E0133, 0);                                     // 1FF0 STZ $33
      w8(m, 0x7E0136, 0);                                     // 1FF2 STZ $36 -- no upload while the window opens
      // ⭐ AND $0215 IS SEEDED HERE, WITH THE WINDOW. MEASURED on the oracle (--monitor "7E0215:2,
      // 7E0129:1,7E0162:1" --diff, saves/field_re/s439_marle_choice_lake.state): box 0x2B opens at
      // f212 and $0215 is still the PREVIOUS box's 00 through states 1-4; at f217, the frame the
      // window is created, it becomes 04. Then 0x10 while the typewriter runs (f227), the break code
      // as each is consumed (06 at f257/f292), and at state 7 the code the string ENDED on -- 0C, a
      // page break, on 0x2B's page 1 (f327), and 00 on 0x29's option page (f140), which is the one
      // that lets the cursor arm one frame later.
      // Without this seed the byte is only ever written at the reveal's end, so a box INHERITS the
      // previous box's value for its whole ramp -- and an inherited 00 is "the string is finished",
      // which arms a multi-page decision's cursor on the WRONG page: the first press confirms and the
      // prompt page is never shown. That is the user's "shows very quickly / automatically,
      // it should be 2 presses of A". Any non-zero value would do for C0/2027's `BNE`; 04 is the ROM's.
      w8(m, 0x7E0215, 0x04);
    } else {                                                  // 1FF8: the OPEN ramp
      int c = r8(m, 0x7E0127), open = (c == 0x28);
      for (int k = 0; k < 4 && !open; k++) { c++; open = (c == 0x28); }
      w8(m, 0x7E0127, (uint8_t)c);
      if (open) w8(m, 0x7E0129, 6);                           // 2045
    }
  } else if (st == 7) {
    // ⭐ C0/201C -- STATE 7 BRANCHES ON $0162 BEFORE IT EVER LOOKS AT THE PRESS LATCH:
    //   201C LDA $62 / 201E BEQ ->2056        ; 0 -> the PLAIN box path (2056 -> 20A5 reads $0134)
    //   2020 CMP #01 / 2022 BNE ->20A0
    //   2024 LDA $0215 / 2027 BNE ->2056      ; a control code still pending = NOT the last page
    //   2029 LDA $64 / STA $63                ; arm the cursor on the FIRST option line
    //   202D LDA #02 / STA $62                ; cursor LIVE
    //   20A0 CMP #03 / BEQ ->2058             ; 3 = CONFIRMED -> 2058 -> 2081 `$29 = 0D` (close)
    //   20A4 RTL                              ; 2 = live but not confirmed -> NOTHING
    // So on a decision box the A press is NEVER the $0134 latch: with $0162 >= 1 the handler never
    // reaches 20A5 at all. $0215 is why a MULTI-PAGE decision needs two presses -- at the end of a
    // non-final page the pending page-break code is still there, so 2027 sends it down the ordinary
    // page-turn path and only the LAST page arms the cursor. The host writes $0215 when its reveal
    // finishes (0 = the string is exhausted, else the pending break code) -- field.c.
    // MEASURED (oracle, saves/field_re/s439_marle_choice_lake.state, A at f100/f200): $0162 = 01 at
    // f0 -> stays 01 through page 1's own wait at f91 -> 02 at f141, one frame after the OPTION page
    // reached state 7 at f140 -> 03 at f201 (the press) -> 01 at f212, the next decision box.
    int d = r8(m, 0x7E0162);
    int t34 = r8(m, 0x7E0134) | (r8(m, 0x7E0135) << 8);
    if (d == 0 && t34 != 0 && t34 != 0xFFFF) {
      // 20A9 `CPX #FFFF / BEQ` / 20AE `DEX / STX $34`: THE PAUSE'S COUNT -- an in-string `03 n` left $0134 = n * 15 (C0/21A8,
      // CtSceneTextboxPause) and nothing but this frame tick spends it; A does not (C0/1AB8 spends FFFF only).
      t34--;
      w8(m, 0x7E0134, (uint8_t)t34); w8(m, 0x7E0135, (uint8_t)(t34 >> 8));
    } else if (d == 0 && t34 == 0 && r8(m, 0x7E0215) == 0x03) {
      // 2058 on the pause code: 2096 `LDA $0216 / BEQ ->2081` -- a pause that ran out types on (`LDA #06 / STA $29`), and
      // `03 00` (no count left to spend) closes the box with no press (2081 `LDA #0D / STA $29`). MEASURED, the rule's
      // cartridge from saves/field_re/last_village_182_f3605.state: $0134 = 0078 at f517 by C0:21C0, one less a frame by
      // C0:20B1, $0129 = 06 at f638, 07 at f639 (the `03 00`), 0D at f640 -- "A tidal wave!" with no input.
      if (r8(m, 0x7E0216)) { w8(m, 0x7E0129, 6); gTbResumeArmed = 1; }   // 209B ... RTL: the typewriter is next frame's
      else w8(m, 0x7E0129, 0x0D);
    } else if (d == 0 && t34 == 0) {
      // 20A5 `LDX $34 / BEQ ->2058` -- ADVANCE NOW, no press: the typewriter reached state 7 through 21D8 alone
      // (a self-turning page code, 0x09/0x0A), which leaves $0134 at 0. 2058 on the code: a page code -> 2086
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
    else --c;                                    /* the 4th DEC is NOT tested */
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

// C0/00EB, one wait of the field's return from a battle: `JSR C0/881E / JSR C0/00DE / BRL C0/EC60` -- the player
// controller's camera tail, then C0/00DE (`JSL FD/C2C1`, which builds a HDMA table set and flips $0153 bit 0, and
// `JSL FD/FFF7` -> FD/E39C), then the NMI wait. C0/0304 makes four of them and C0/18C1's tail a fifth (field.c, the
// battle's return): under the rule each is one frame of the return, the first on the ack's own frame. MEASURED on the
// rule's oracle (the 0AD return, --monitor 7E1D8B:4,7E0153:1 --writer): one `$1D8B` step by C0:9585 and one `$0153`
// write by FD:C2D2 / FD:C2DE on the ack's frame and on each of the four after it.
void CtSceneReturnWaitFrame(CtScene* sc) {
  if (!sc) return;
  CtSceneCameraTick(sc);                              // C0/881E (the lead's controls are the host's; the camera is this layer's)
  CtHdmaTableBuild(sc->mem->s);                       // C0/00DE: JSL FD/C2C1
  ctObjSlotFrame(sc);                                 //          JSL FD/FFF7 -> FD/E39C
}
// C0/02AF-02C1, one frame of the defeat's fade: `JSR C0/881E / JSL FD/FFF7 / JSR C0/EC60` -- the camera and FD/E39C, no
// C0/00DE (the wait before it, C0/02A0-02AD, is C0/00EB itself: CtSceneReturnWaitFrame).
void CtSceneDefeatFadeFrame(CtScene* sc) {
  if (!sc) return;
  CtSceneCameraTick(sc);                              // C0/02B9 JSR C0/881E
  ctObjSlotFrame(sc);                                 // C0/02BC JSL FD/FFF7 -> FD/E39C
}

// ---- THE $0117 BIT-5 SERVICE, C0/1770-188B (disasm/notes/field_tile_copy_e5.md) -------------------------------------
// The main loop is C0/0097 `TSB $51 / JSR 18D9 / JSR 0C76 / JSR 881E / JSR 1AAC / JSL 1F87 / JSR 21E1 / JSR 274D /
// JSR 00BF / JSR EC60`. C0/0C76 fades (1F24/1F5A) and then dispatches $0117: bit 7 (0C8F, the location change), bit 6
// (0CC7, the menu), bit 4 (0D3E, a door, which ends in `BRL 16DC`), bit 1 (16DC, a chest) and then C0/1770:
//   1770 LDA $17 / BIT #20 / BNE / BRL 188C       bit 5 clear: on to bit 0 (the battle)
//   1779 LDA #20 / TRB $17 / LDA #01 / STA $38
//   1781 JSR 885A / JSR 00BF / LDA $38 / BEQ 1790 / JSR EC60 / BRA 1781      the camera aligns, a whole event frame per step
//   1790 JSR AF4E / JSL FD:FFF7 / JSR EC60                                   the copy, on the operands op 0xE5 staged
//   179A LDA $45 -> the style branch below, which waits in C0/EC60 with only C0/00DE (FD:C2C1 + FD:FFF7) per frame and
//   returns (RTS, or BRL EC60's) into C0/00A7 -- that frame runs 881E .. 00BF but not 0097, 18D9 or 0C76.
// MEASURED, the logical oracle (CT_ORACLE_LOGICAL=1): loc 002's curtain from saves/field_re/loc002_curtain_ctl.state
// (A at it) -- E5 `13 12 1B 1C 13 02 3C` at f73, $7E:3293.. copied at f74, obj10's cadence $1015 held at 08 over f75-f76,
// $0146 = 04 at f75; E5 `..3B` at f84, $7E:3253.. copied at f85, held f86-f89, $0146 = 03 02 01 over f86-f88. The new
// game's own opening (start_scene.state, the text mashed; E5 `..3B` at f2339, `..3C` at f2352) holds 4 and 2 frames alike.
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
// re-points BG1 at $1D7D: style 3 builds BG1 into the back buffer $1D82, swaps it in with BG2's upload (the 2nd frame --
// the one change the picture shows, oracle thumbnails N+4 of the copy frame N, the port's frame N+2), then rebuilds the
// front one and swaps back. Styles 1/2/4 show on the 1st (thumbnail N+3; loc 002's `..3C` at f77, the intro's at f36).
static const int kXferStyleFrames[8] = { 0, 2, 2, 4, 2, 0, 0, 0 };
static const int kXferStyleShown[8]  = { 0, 1, 1, 2, 1, 0, 0, 0 };

// C0/74D4 / 74E8 / 74F7 -- a layer's re-origin after its tilemap is rebuilt at the camera: STZ its tilemap origin ($1D99 /
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

// THE NMI A STYLE FRAME HANDS A TILEMAP. The field NMI starts `EAD1 LDA $46 / BNE ->EAD8`: `CMP #01` / `#02` / `#03` /
// `#04` upload $1D7C / $1D7E / $1D82 / $1D80 (C0/7F62), bit 3 C0/7F5D, bit 4 C0/7F58, and each of those ends `JSL FD:C1EE /
// STZ $52 / BRL EC15` -- past C0/EB79-EC13, so that NMI does NOT run 6ECB (the text line), 712E (the CGRAM, $7E:2200 x
// $200 over channel 7), ECA3 (the OAM), B1B2 (the VRAM queue, $09A0's clear) or the edge columns ($1D78-$1D7B). Bit 5
// (C0/EC77) and any other value fall into EB79. MEASURED, the logical oracle, scene 236's lever
// (saves/field_re/s236_lever.state, A at f10, E5 style 3 copied at f16): `--hookpc` reaches C0/7F62 once on f17, f18 and
// f19 and C0/EB79 / 712E / ECA3 / B1B2 on none of them (once on every other frame); the lights' rows of the page
// (CGRAM 55-59) read dim at f18/f19 while the cartridge's CGRAM keeps f17's bright ones (peek f20/f21).
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
    CtVmTileCopyAF4E(s);                             // 1790 JSR C0/AF4E, on $013E-$0144 as they stand NOW
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

// C0/0C9A-0CC4, THE PENDING CHANGE'S COMMIT: C0/0C9D-0CB2 file where the party was ($0100 -> $0105/6, the lead's tile
// $1801/$1881 -> $0107/$0108, its facing ^ 1 -> $0109), 0CB4-0CBE copy the staged $0112/$0114/$0116 into $0100-$0104,
// and 0CC0 `LDX #06FF / TXS / BRL C0/0000` leaves the main loop for good -- the load runs in this very iteration, and
// nothing after C0/0C76 (the controller, the A handler, the box machine, the scheduler) runs in the old scene again: a
// committed scene's CtSceneTick returns at once. Called by the tick's drain, or by a host that runs C0/0C76's head
// itself (field.c's fade block), ONCE: C0/0C9D's `$0105 = $0100` run twice files the destination as the origin.
void CtSceneExitCommit(CtScene* sc) {
  if (!sc || sc->exitCommitted) return;
  int po = sc->playerObj >= 0 ? sc->playerObj * 2 : 0;   // the ROM reads the lead offset from $0197
  CtWramCommitLocation((CtWram*)sc->mem, po);
  // THE DESTINATION IS READ BACK OUT OF THE COMMITTED WORDS, never from whatever an op happened
  // to stash in a C field. That is what makes the two deferred triggers ONE machine: an op-0xE0
  // and a positional map exit stage the same five bytes, the commit copies the same five bytes,
  // and the host then learns where it is going exactly the way the ROM's loader does -- by
  // reading $0100/$0102/$0103/$0104 (C0/0066 dispatches on $0100). A positional exit has no
  // opcode at all, so any C-field route would have had to invent a second way to say the same
  // thing. (The IMMEDIATE variants are a different ROM path: 0xDD/0xDE (C0/3B1B) write $0100 on the spot and
  // leave the delivery to whatever reboots next, 0xDF/0xE1 (C0/3B4B) reboot themselves at the vblank.)
  sc->lcLoc    = r8(sc->mem, 0x7E0100) | (r8(sc->mem, 0x7E0101) << 8);
  sc->lcX      = r8(sc->mem, 0x7E0102);
  sc->lcY      = r8(sc->mem, 0x7E0103);
  sc->lcFacing = r8(sc->mem, 0x7E0104) & 3;
  sc->lcStyle  = 0;
  sc->locChange = 1;
  sc->exitCommitted = 1;
}

#define SND_TAIL_LINES 61   // the tick's tail after the runner, in lines (see CtScene.sndPostResumeFreeze)
// THE RULE's infinitely fast CPU (CT_ORACLE_NMION_HOLD=1 + CT_ORACLE_IFAST=1, CLAUDE.md; audio.c CtAudioRuleOn is the sink's
// side): a sound op's JSL costs the frames its SPC needs from where the NMI left the beam, and the CPU work around it --
// the rest of the runner, the tick's legs -- is free, so the tail rule above (a real-time CPU's overrun) does not apply.
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
  // THE $0117 BIT-5 SERVICE decides which legs of the main loop this frame has (CtSceneXferFrame, the helpers above):
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
  // C0/0097, the FIRST instruction of the field frame: `LDA $00F0 / TSB $51`. $00F0 is the
  // NEWLY-PRESSED button byte; $0151 accumulates it stickily and ops 0x3F-0x44 consume bits with
  // `TRB $51`. Without this the latch is always 0 and those ops take their not-pressed branch every
  // visit -- scene 7's obj0 autorun then polls its Y/R/L/X ring forever and never fires.
  // ⚠ THE VM READS $00F0, IT DOES NOT WRITE IT. The input bytes belong to the ROM's joypad routine
  // (C2/84D2), which in the port is the HOST: field.c fieldPadNmi writes $00F0-$00FF (--vmdiff: CtSceneSetButtons
  // $00F0/$00F2). Writing them
  // here instead made --vmdiff claim four bytes it cannot maintain -- that harness never plumbs
  // buttons, so ours read 00 against the oracle's live 02 (Left) and vmdiff-wmask-party3 went red
  // on $00F1/$00F3 at f2. Claim only what this layer owns.
  // ...but ONLY on a main-loop frame. C0/2848's ramp (see loadFadeIn) never reaches C0/0097, so the
  // sticky latch does not accumulate while the new location fades in.
  if (!sc->loadFadeIn && head)
    w8(sc->mem, 0x7E0151, (uint8_t)(r8(sc->mem, 0x7E0151) | r8(sc->mem, 0x7E00F0)));   // 009A TSB $51
  // THE MESSAGE BOX MACHINE C0/1F87 -- every state the ROM walks, in the ROM's frame slot (C0/00AD, after
  // the A handler and before the VM). It lives here because the bytes it produces are what the event-VM
  // ops read, and --vmdiff runs the VM without any field layer. C0/00AD is a main-loop leg too, so the
  // load's fade-in ramp does not run it either.
  if (!sc->loadFadeIn && body) tbMachineTick(sc);

  gVmTick++;
  // THE SONG-UPLOAD FREEZE, decided FIRST (the sndWait fields; song_load_freeze.md section 6). While the
  // ROM's main loop is parked inside `JSL $C7:0004` only the NMI's work happens -- the upload flush
  // below, the fade counter's INIDISP, the deferred copies -- and none of the main-loop legs: not the
  // camera (C0/A810's tail), not the $0117 drain (C0/0C8A, in the C0/0097 chain), not the scheduler.
  int sndResuming = 0;
  sc->sndFrozenTick = 0;
  if (sc->sndPostResumeFreeze) { sc->sndPostResumeFreeze = 0; sc->sndFrozenTick = 1; }   // the tail overran: no tick this window
  else if (sc->sndWait > 0) {
    if (--sc->sndWait > 0) sc->sndFrozenTick = 1;
    else { sndResuming = 1; sc->rasterLine = sc->sndEndLine;
           // (THE RULE, the infinitely fast CPU: the tick's tail after the JSL is CPU work and costs no beam -- it never
           // overruns the NMI; the frames lost are the JSL's own, vmRuleIfast)
           sc->sndPostResumeFreeze = !vmRuleIfast() && (sc->sndEndLine + SND_TAIL_LINES >= 225);
           if (sc->sndResumeN > 0) sc->st[sc->sndResumeObj].addr = sc->sndSavedAddr; }   // the JSL returned: the visit ends, the ip commits
  } else sc->rasterLine = 12;   // the runner's start line: measured 12 on the early queue (`--hookpc 3C43` vPos)
  sc->sndPosted = 0; sc->sndFreezeFrames = 0;
  // ---- C0/EB8C, THE NMI'S UPLOAD FLUSH -- FIRST, and the position is load-bearing ---------------
  // C0/CA76's prologue is `LDA $09A0 / BEQ / RTS`: the WHOLE drain is refused while the VRAM upload
  // queue still holds work, and C0/B1B2 is the only thing that clears $09A0. It runs from the NMI,
  // i.e. at the TOP of the frame, ahead of everything below -- so `$09A0` is a PHASE, not a flag, and a
  // host that flushed at the end of its tick would refuse the drain on every frame following a frame
  // that uploaded. `vram = NULL` walks the queue and clears the gate without copying: the clear is the
  // half the field frame needs and the copy is the half a renderer needs, and the ROM only keeps them
  // in one routine to share the channel-7 setup.
  // ...but not after a frame whose NMI a style tilemap took: that NMI `BRL EC15`s past B1B2 (CtFieldNmiTilemapArm).
  if (legsOn() && !nmiTilemap) CtAnimUploadFlush(sc->mem->s, NULL);
  if (xk == CT_XF_STYLE) { ctXferStyleFrame(sc); return; }   // C0/179A's branch: C0/00DE and the NMI, nothing else
  if (!sc->sndFrozenTick) CtSceneCameraTick(sc);         // the ONE field camera (unification): maintain the
                                                         // DP=$1D00 scroll ONCE/frame here, so EVERY VM host (interactive,
                                                         // --vm/--vmdiff, headless --battletest) drives it.
  // op-0xE0 fade-change drain (ROM C0/0C8A, run per field frame BEFORE the script scheduler): while
  // $0117 bit7 is latched, DEC the $0119 fade counter once per frame; when it reaches 0 (or goes
  // negative -- the ROM's BMI), commit: remember where we came from (C0/0C9D-0CB2: current loc word
  // $0100 -> $0105/6, lead X/Y bytes $1801/$1881 -> $0107/$0108, lead facing^1 -> $0109), copy the
  // staged $0112/$0114/$0116 into the loader slots $00/$02/$04 and reload (BRL C0/000E) -- ours
  // raises locChange for the host; the new scene's memInit models the load's `STZ $17` (C0/0B96).
  // Ordering pinned by the oracle (scene 120): the E0 fires during frame N with $0119 left at 0x0F,
  // the first DEC lands on N+1, the reload on N+16 -- a top-of-tick drain reproduces that exactly.
  // ...and parked with it during the load's fade-in too: C0/2848's loop does not call C0/0C76, so a
  // change staged while the new location ramps up waits for the first main-loop frame to start draining.
  // ...and on the $0117 of the frame's HEAD. C0/0C76 runs at C0/00A4, before the player controller C0/881E, whose
  // accepted step stages a positional exit (C0/9D24 `ORA #80 / STA $0117`): the iteration that raises the latch has
  // already passed its drain, and the first DEC is the next one's. MEASURED on the forest's south exit (logical oracle,
  // saves/forest_exit.state, Down): the step to $1884 = 2F00 raises $0117 = 80 at f12, $0119 reads 0E at f13 and 00 at
  // f27, and $0100 = 01F0 at f28. A host whose controller runs before this tick latches the head's byte first
  // (CtSceneHeadLatch); the VM's own stagers (op 0xE0 & co.) run after the drain either way.
  uint8_t d117 = sc->head117Set ? sc->head117 : (uint8_t)r8(sc->mem, 0x7E0117);
  sc->head117Set = 0;
  if (!sc->sndFrozenTick && !sc->loadFadeIn && head && (d117 & 0x80)) {
    int fc = r8(sc->mem, 0x7E0119);
    if (fc != 0 && !(fc & 0x80)) {
      // 0C97 DEC $19 -- and sc->bright with it: it stands for $0119's low nibble, and the picture is its frame's $0119
      // (the NMI's C0/EC4E). Left at 15, the frame drew the host's copy of the byte from BEFORE the tick, one step late:
      // MEASURED on the attract's loc 0E6 exit (a pan through the fade, $0117 = 80 at cartridge --monitor f5672 / boot
      // port f7777), the port's first dim picture was one frame after the cartridge's while its scroll matched.
      w8(sc->mem, 0x7E0119, fc - 1);
      sc->bright = (fc - 1) & 0x0F;
    } else {
      CtSceneExitCommit(sc);
      return;                                            // the main loop is left (see CtSceneExitCommit)
    }
  }
  // THE ARMED FADE = C0/0C76 -> C0/1F24, once per main-loop frame (C0/00A4), gated on $0118 bit1:
  //   1F24 LDA $19 / AND #0F / CMP $1B / BEQ ->1F55          at the target: 1F55 `LDA #02 / TRB $18`
  //   1F2C BCS ->1F40                                        above it: the darkening arm
  //   1F2E/1F40 LDA $1D / BEQ / DEC $1D / RTS                 the COUNTDOWN: no step while $1D != 0
  //   1F35/1F47 LDA $1C / STA $1D                             reload it from the rate, then step:
  //   1F39 INC $19 / RTS                                      brighten (the bit clears on the NEXT call)
  //   1F4B LDA $19 / DEC A / BEQ ->1F53 / STA $19 / RTS       darken; reaching 0 stores AND clears the bit
  // So rate r steps every r+1 frames. The port stepped every frame whatever the rate: MEASURED on the
  // attract's loc 01B (`F0 01 / F2`, saves/attract_cena_inicial.state from f1080, --monitor "7E0119:1,
  // 7E011C:1,7E011D:1"): $011D alternates 00/01 and $0119 falls 0E, 0D, ... on every OTHER frame, f292-
  // f320 -- 28 frames, where ours went dark in 16. sc->bright stands for $0119's low nibble.
  // It is C0/0C76's, so it waits out the $0117 bit-5 service with the rest of the head (`head`): a copy's frames hold the
  // fade. That hold used to be `tcN == 0`, i.e. any render copy still queued -- 16 frames after every op 0xE5.
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
  // C0/00B1 JSR C0/21E1, before the pass: `LDA $39 / BEQ / DEC / ASL / TAX / JSR ($21EE,X)` -- 1 the iris opening (C0/220E),
  // 2 closing (C0/2269), 3 the window wipe (C0/22D8), 4 / 5 the circle as the colour window (C0/232A / 238A), 6 the quad
  // window (C0/2409) and 0E its teardown (C0/23E2), 7-0A the fixed circles (C0/2514 / 2589). (0B-0D -- C0/260E 264F
  // 2329 -- have no port.)
  if (!sc->sndFrozenTick && !sc->loadFadeIn && body) {
    int st39 = r8(sc->mem, 0x7E0139);
    if (st39 == 1 || st39 == 2) ctIrisFrame(sc);
    else if (st39 == 4 || st39 == 5) ctColCircleFrame(sc);
    else if (st39 >= 7 && st39 <= 0x0A) ctFixedCircleFrame(sc);
    else if (st39 == 6 || st39 == 0x0E) ctLineFrame(sc);
    else ctWipeFrame(sc);
    // C0/00B4 JSR C0/274D: the caption requests in $0154 bits 2/3/4 and, with none of them, bit 5's C0/2758 -- a chest's box
    // closed (`$29 = 0D`) when its tile $015D/$015E (field.c's probe, C0/1E52) leaves the camera (move.c CtVmCaption274D).
    CtVmCaption274D(sc->mem->s, gRomImg, (int)gRomImgLen);
  }
  if (sc->sndFrozenTick) return;                        // the song-upload freeze: no queue-build, no runner, no legs
  // ROM frame routine C0/00BF: queue-build (C0/59D9) THEN run (C0/5A46). Modelled as two passes.
  // PASS 1 (queue-build): decrement every live object's cadence ($1001); when it expires, reload
  // from $1000 and -- if the object has a function to run -- mark it eligible. The cadence cycles
  // even for IDLE objects (prio 7, no addr), so when one is REQ/CALL-activated it resumes on its
  // real cadence phase, not immediately. The eligible set is SNAPSHOT here: an object activated
  // during PASS 2 is not in it, so it first runs NEXT frame -- this is the callee's one-cadence-
  // cycle defer (verified via --vmdiff: scene-8 obj23 activated f31 runs its op 0xAA at f33, not
  // f31; the source of the demo's 8-frame sparkle-hide stagger).
  uint8_t elig[64];
  if (sndResuming) memcpy(elig, sc->sndElig, sizeof elig); else   // the resume frame runs the queue the JSL interrupted
  for (int n = 0; n < sc->nobj && n < 64; n++) {
    elig[n] = 0;
    // C0/59EC `LDA $00,X / BMI ->59F8`: the pause bit is the only test BEFORE the countdown. A killed object (op
    // 0x0A, $1100 = 80) is the C0/5A06 skip below, AFTER the DEC, so its count still drains to 0 -- the port used to
    // skip it here, holding the count, and a later load op that revives it (loc 112's obj10/obj12) would have
    // started from the held count instead of the ROM's 0. The battle's dead-mark is this pause bit (C0/07F6).
    if (sc->actors[n].cadPause) continue;         // $1000 bit7 (op 0x0B): cadence frozen until 0x0C
    CtSS* s = &sc->st[n];
    // C0/59F0-59F6, literally: read the countdown, RUN if it is ALREADY zero (an expired object that
    // could not be enqueued retries every frame), else decrement and RUN if that reached zero. The
    // two BEQs around one DEC are why an object re-armed mid-visit by op 0x87 lands on the new period
    // and not the old one. Value-for-value identical to the `dc > 1` form this replaced; the point is
    // that it now reads and writes $1001,X.
    uint8_t cd = (uint8_t)r8(sc->mem, 0x7E1001u + (uint32_t)n * 2);
    if (cd != 0) {
      ctSetDc(sc, n, cd - 1);                      // C0/59F4 DEC $1001,X
      if (s->dc != 0) continue;                    // C0/59F6 BEQ -> the enqueue arms
    }
    // cadence period = $1000 (op 0x87 -> moveAnim) else the ROM default. Default is 1 (C0/42D7:
    // every frame); we keep a 4-frame heuristic for generic NPCs (their tuned timing) but the
    // PLAYER (the lead-PC, index varies: obj1 sc2/5, obj3 sc8 -- key on playerObj not hardcoded)
    // uses the real default 1 (a scripted player move with no op 0x87, the bell bob, was 4x slow).
    // C0/5A06 `LDA $1100,X / BMI ->59F8`: an object whose MOVE-MODE has bit7 set is skipped -- and the
    // skip lands AFTER the DEC, so its countdown reaches 0 and STAYS there, because the reload lives
    // only inside the three enqueue arms (5A1F / 5A2B / 5A40). $1100 = 0x80 is not "idle": it is a
    // REMOVED object (op 0x0A) or a PC slot for a character who is not in the active party (the load
    // op's `: 0x80` fallback). Measured on party3 -- a 3-member party, so objects 2/5/6/7 are the
    // absent PC slots: the oracle holds all four at $1001=0 for 130 frames while the port re-armed
    // them to their period 4 every 4 frames. They are parked on op 0xB0 and the ROM never visits them.
    // Read from sc->mode[] rather than WRAM: the live load ops (op 0x80-0x83) update the mirror and
    // NOT $1100, so the WRAM byte is only trustworthy on a seeded scene. That asymmetry is real and
    // predates this -- C0/5BD5's ported scan reads the WRAM copy.
    if (sc->mode[n] & 0x80) continue;
    ctSetDc(sc, n, sc->actors[n].moveAnim > 0 ? sc->actors[n].moveAnim : 1);  /* 5A1F/5A2B/5A40 reload; default $1000=1 (C0/42D7) */
    elig[n] = 1;     // ENQUEUED on cadence-expiry regardless of prio/addr: an IDLE object (no
                     // function yet) that is REQ/CALL-activated by an EARLIER-index object in
                     // pass 2 then runs its new function THIS frame (it is already in the queue).
  }
  // PASS 2 (runner): run the queue from its HEAD (C0/5A66 `LDA $74`, the link `$1080,X`). The queue is built in
  // index order, but not purely APPENDED -- C0/5A01 `LDA $1100,X / BEQ ->5A31`: the object whose move-mode is 0,
  // the LEAD PC, is PREPENDED (5A3B `STA $80,X` links it to the old head, 5A3D `STX $0174`), every other object
  // appended at the tail $0175 (5A11-5A1B). So the lead runs FIRST and everything else in ascending order. A
  // low-index orchestrator (obj0) still runs BEFORE the callees it REQ/CALLs, so an async REQ'd callee that is
  // also enqueued this frame runs its first op the SAME frame (the demo's sparkle SPAWN at f40); a CALL+WAIT
  // still takes 2 caller-visits, so at the orchestrator's cadence 4 the sequential fn4 hides land 8 frames apart.
  // But a LEAD that obj0 repoints -- op 0xD9's release -- has already had its visit, and first runs the new
  // code NEXT frame. Oracle, saves/gato_t0.state (--vmdiff): obj0's `D9` at f41 leaves the lead at IP 171F
  // (its walk program's first byte) while the follower obj2 runs the same program's first ops that frame.
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
  if (!sndResuming) { memset(g_ranB0, 0, sizeof g_ranB0); memset(g_landedB0, 0, sizeof g_landedB0); }   // (a resumed
                                                                // pass keeps what its first half dispatched)
  for (int qi = sndResuming ? sc->sndResumeN : 0; qi < nq; qi++) {   // no cut: C0/5A5D on the fixed virtual beam
    int n = order[qi];
    CtSS* s = &sc->st[n];
    // idle (never activated): nothing to run. NOT a killed one: C0/5A66-5A8B pops the queue head and runs it with no
    // look at $1100, so an object an earlier visit killed this frame still has the visit it was queued for.
    if (s->addr < 0) continue;
    // THE PER-VISIT OP BUDGET, literally C0/5A93:
    //   5A93: LDA $68 / STA $67          ; $67 = the budget
    //   5AB1: JSR ($5D6E,X)              ; dispatch
    //   5AB4: BCC ->5ABA                 ; carry CLEAR -> end the visit
    //   5AB6: DEC $67 / 5AB8: BNE ->5AA3 ; carry SET -> spend one, next opcode
    // $68 is $0168 (DP is $0100 throughout the scheduler, confirmed in the --trace DP column), and
    // its only writer is C0/0BCA `LDA #05 / STA $68`; read back live as 5 on a field, an overworld
    // and a menu state. So: FIVE dispatches, hard.
    //
    // The previous loop exempted control-flow ops from the count and added a `tot < 0x68` cap. Both
    // are refuted: `DEC $67` is unconditional on the SEC path and the GOTO handlers DO return SEC
    // (op 0x10 -> C0/62B5, op 0x11 -> C0/62CB, shared tail 62C6 `TAX / SEP #20 / SEC / RTS`), and
    // 0x68 is not a count at all -- it is the direct-page ADDRESS of the budget read as one. (The
    // raster gate is real but lives in the OUTER scheduler C0/5A46, between ACTORS: 5A5D `CMP $69`
    // with $0169 = 0x0048. It truncates the actor list, never a visit.)
    //
    // MEASURED on the oracle, 1467 visits across three states: maximum 5, NEVER 6, and 95% ended on
    // the first CLC. The 5-op visits are precisely the pure control-flow poll loops this code used to
    // let run free -- e.g. `11 12 11 12 11` and `22 11 22 11 22`. Ours ran those to 104 ops.
    // (The old comment's "scene-1 Mom obj15 12/22/11" cannot be a per-visit ROM opcode count.)
    for (int k = 0; k < 5; k++)
      if (step(sc, n)) break;
    if (n < 64) g_landedB0[n] = !g_ranB0[n] && s->addr >= 0 && s->addr < sc->blen && sc->body[s->addr] == 0xB0;
    // ⭐ C0/5ABC-5ABF `REP #20 / TXA / LDX $6D / STA $1180,X` -- the runner writes the object's SCRIPT
    // POINTER back to WRAM at the end of every visit (C0/59AB is the same store on the autorun path).
    // Ours kept the ip only in the C struct, so $1180,X in any state we SAVE was whatever the last
    // seed left. That breaks the capture -> oracle workflow the F5 snapshot exists for: splicing a
    // Belthasar debug state onto a lake base sent the ROM to a STALE address and the scene went
    // nowhere (measured on the user's own build/ct_debug.state -- obj3 read ip 03CB while its live
    // script sat at 01B0+header, and the oracle just closed the box and stopped). Same family as the
    // loader arms' $1C80/$1A81/$1B01: a C mirror that never reached the RAM the ROM reads.
    // NB $1180,X is LITTLE-endian (the seed reads `w[0x1180+x] | w[0x1181+x]<<8`) -- the two bytes, explicitly.
    // ...AND A WAITING CALL (op 0x04/0x07) IS PARKED ON ITS OWN BYTE (61A4 `TYX / CLC`): the ROM's $1180 holds the call op
    // until it completes (the tutorial's obj0, $1180 = 0257 = `04 18 11` through f10); ours holds the IP past it in s->addr
    // and the call in callAddr, as the --vmdiff compare above reads it.
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
        sc->sndSavedAddr = s->addr; s->addr = sc->sndOpAddr;   // the ip commits when the visit ends -- on the return frame
        { uint32_t ipw = (uint32_t)(s->addr + CTW_IPOFF(sc));  // ...so the $1180,X mirror commits it too
          w8(sc->mem, 0x7E1180 + (uint32_t)n * 2, ipw & 0xFF);
          w8(sc->mem, 0x7E1181 + (uint32_t)n * 2, (ipw >> 8) & 0xFF); }
        sc->sndFrozenTick = 1; return;
      }
      if (!vmRuleIfast() && sc->rasterLine + SND_TAIL_LINES >= 225) sc->sndPostResumeFreeze = 1;   // same frame, but the tail will overrun the NMI
    }
  }
  // ---- the PARTY TRAIL, in the ROM's slot: it IS the PCs' op 0xB0 (C0/3002 -> 9E29 / A26B / A2CE), i.e.
  // part of the runner above, and its $1900/$1980/$1600/$1680 stores are consumed by C0/A810 below (AAFD
  // integrate, the C98A anim leg) and by the drain. field.c used to call it AFTER CtSceneTick, one frame
  // late for all three consumers.
  // THE LEAD'S STEP, AT C0/A810 (AB06 `LDA $1A01,X / BEQ`): the host moved him before the pass (field.c, C0/881E's step);
  // it stands only on a ticket his B0 armed this pass and nothing dropped (vmLeadStepAtA810).
  // (A song upload that parks the pass after his B0 keeps the ticket to the return frame, where A810 runs: the host does
  // not move him while parked, and the step he took on the posting frame commits there -- CtSceneMainLoopParked.)
  vmLeadStepAtA810(sc, 0);
  CtSceneUpdateFollowers(sc);
  vmLeadStepAtA810(sc, 1);
  g_leadFromOk = 0;
  // ⭐ C0/A810's OBJECT LOOP IS UNCONDITIONAL. A810 reads the object count into $BD and A819 walks
  // EVERY index every frame, dispatching on $1100,X (A81E CMP #03 / A822 CMP #08 / A826 CMP #07) --
  // there is no movement-ticket gate anywhere in it. Mode 7 goes to A86B `JSR C0/A947`, whose ONLY
  // effect is the activation latch, and for a mode-7 object that is the latch's ONLY writer (the
  // A88D pass skips modes 7/8).
  //
  // Ours used to live inside the run_tasks integrate loop, which is gated on `hasTask && cycles > 0`.
  // An object that never MOVES therefore never got its latch set -- and scene 2's CURTAIN is exactly
  // that: obj10, $1100 = 07, a fixed position, no mover, and $0F14 read 0000 here against the
  // oracle's 0080 (measured on saves/wake.state, identical camera cells on both sides). Everything
  // downstream reads that latch, so the curtain was invisible to the activate scan, the touch scan
  // and the draw pass alike -- "when pressing ACTION on the curtain in scene 2, it does not open/close".
  //
  // C0/A947 is A88D's camera-window test WITHOUT the clamps: $C3 = $1D0A-4, $C4 = $1D0C+4, $C5 =
  // $1D0E with no -2, $C6 = $1D10+5; A97E `LDA #80 / STA $0F00,X` inside, A985 `STZ $0F00,X` outside.
  // ⚠ FROM OBJ1. obj0's slot IS $0F00 itself, and that byte is ALIASED with the draw list: the ROM
  // writes it as `$0E81,X` with X = $7F (C0/A9C1 `LDA #80 / STA $0E80,X / STA $0E81,X`), and the
  // oracle's net value there is 08, owned by the draw pass -- writing 0x80/0x00 over it made
  // vmdiff-wmask-party3 diverge at f1 ($7E0F00 ours=00 oracle=08). Nothing reads it as a latch
  // either: the activate scan starts at objectCount*2 and walks DOWN, ending when X reaches 0
  // (C0/1D21 `DEX / DEX / BNE`), so obj0 is never probed -- as field_action_probe.md states.
  for (int n = 1; n < sc->nobj; n++) {
    if (sc->mode[n] != 7 || !sc->actors[n].hasPos) continue;
    int c3 = (sc->mem->s[0x1D0A] - 4) & 0xFF, c4 = (sc->mem->s[0x1D0C] + 4) & 0xFF;
    int c5 = sc->mem->s[0x1D0E] & 0xFF,       c6 = (sc->mem->s[0x1D10] + 5) & 0xFF;
    int tx = ((int)sc->fx[n] / 16 * 2) & 0xFF;       // A966 LDA $1801,X / ASL A -- the TILE, doubled
    int ty = ((int)sc->fy[n] / 16 * 2) & 0xFF;       // A972 LDA $1881,X / ASL A
    // ⭐ A947's FOUR COMPARES ARE **SIGNED** -- and A947, unlike A88D, does NOT clamp the edges.
    //   A96A CMP $C3 / A96C BMI ->A985      A96E CMP $C4 / A970 BPL ->A985
    //   A976 CMP $C5 / A978 BMI ->A985      A97A CMP $C6 / A97C BPL ->A985
    // BMI/BPL read the N flag of the 8-bit difference, so the compare is SIGNED; A88D's own window
    // test (A8D3/A8D7/A8DF/A8E3) is BCC/BCS = UNSIGNED, which is only correct THERE because
    // A8A3/A8AF/A8BB/A8C7 SATURATE the four edges first. This port reused A88D's unsigned form on
    // A947's UNCLAMPED edges: with the camera parked at the map origin ($1D0A = 0) `$1D0A - 4`
    // wraps to 0xFC -- read unsigned that is 252, so EVERY class-7 object fell outside the window,
    // its $0F00 latch stayed 0, and the two ROM scans that gate on it (activate C0/1CFC, touch
    // C0/9923) skipped it. Class 7 is exactly the sprite-less coordinate/trigger object, so the
    // casualties were the scene-8 Telepod DEMO trigger (obj12, tile 4,4, camera 0,0 -- "Crono steps
    // on the pod and nothing happens") and the scene-129 Cathedral PENDANT trigger (obj15, tile
    // 8,10, camera x-cell 0). Scene 2's curtain (obj10) only worked because its camera is mid-map.
    // Oracle out_s8.state: obj12 $1100 = 07 and $0F18 = 80 on every frame, writer C0:A983 = the
    // instruction after A980 `STA $0F00,X` inside A947.
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
      int ntx = (int)nfx >> 4, nty = (int)nfy >> 4;   // the tile the step would move INTO
      // ROM per-FRAME dispatch C0/A819: branch on the per-object MOVE-MODE $1100,X (sc->mode[n]).
      //   mode < 3        -> C0/AAFD: plain 16-bit add of the velocity, ALWAYS smooth, never reads
      //                      $7F0B00, crosses tiles freely (lead PC slot0=0, follower PCs 1/2).
      //   mode in 3..6    -> C0/AA07: the $7F0B00 (b00) clamp/free lifecycle below (NPCs/sparkles).
      //   mode 7 (camera) / 8 (skip) / 0x80 (idle) never reach here with an active ticket.
      // C0/AA07 does NO $7E7040 map test for ANY mover -- it blindly integrates the visit velocity.
      // The map-collision + AE27 random bounce lives ONLY in C0/ACFD, run ONCE per op-move VISIT
      // (moveOp), vetting a single sub-pixel step. So NO per-frame map test here.
      int st = sc->mode[n];
      // C0/A81E-A82A is a FOUR-arm dispatch and this had only two. The missing pair is not academic:
      //   A822  CMP #08 / BEQ ->A83E   mode 8 -> the object is SKIPPED ENTIRELY, no integrate
      //   A826  CMP #07 / BEQ ->A86B   mode 7 -> JSR C0/A947 ONLY, no integrate
      // The comment above claiming modes 7/8 "never reach here with an active ticket" is refuted by
      // measurement: party3_field's obj35 has $1146 = 07 and $1A47 = $18, a live ticket, and the port
      // walked it (pos 3327 -> 3312 -> 3296 ...) while the oracle holds it still.
      if (st == 8) continue;                           // A822
      if (st == 7) continue;                           // A826 -> A86B: handled by the pass ABOVE,
                                                       //   which runs whether or not there is a ticket
      { uint32_t X2 = (uint32_t)n * 2;
        if (r8(sc->mem, 0x7E1B80 + X2)) {
          // C0/AAC7 -- BOTH integrators open with `LDA $1B80,X / BEQ` (AA09 in AA07, AAFF in AAFD) and take this arm when
          // it is set: op 0x7A's jump. The ticket counts down, X += the 16-bit $1900, the 8-bit $1980 takes $1B81 with its
          // carry into $1981 -- the gravity -- and Y += the 16-bit $1980. Done on the bytes, the C position follows them.
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
        // battle_entry_camera.md Part D.3 (the op-0xD9 follower-degeneracy root cause): on real hardware
        // C0/AAFD's 16-bit ADD writes $1800/$1880,X DIRECTLY -- there is no separate float "shadow"
        // position, so every PC-mode mover (lead=0, followers=1/2) is ALWAYS live in WRAM. Our port keeps
        // the live position in sc->fx/fy but only mirrors it back into $1800/$1880 for the INTERACTIVELY-
        // controlled player (field.c fieldPlayerCollide, free-roam only -- never during an E3-locked
        // scripted move). A consumer that deliberately reads the "maintained" WRAM tile byte $1801/$1881,X
        // (field.c's D8 staging, T3.1 -- matching the ROM's own C0/041A behavior) then sees a STALE mirror
        // for any follower moved by a scripted op (op-0xD9's MOVEPARTY): the lead trails naturally close
        // (the F145 capture happened to seed him near his target) while a follower's mirror can be
        // arbitrarily far off (co-located with the lead's OWN seed position -- ~39px, the dossier's
        // observed defect). Mirror BOTH axes' 16-subpixel words for EVERY PC-mode actor here (the exact
        // reverse of the seed-time conversion `sc->fx[o] = px/16.0`, ct_vm.c CtSceneSeedFromWram) so any
        // consumer sees the true live position, matching the ROM's own always-live guarantee.
        uint32_t X2 = (uint32_t)n * 2;
        int wx = (int)(sc->fx[n] * 16.0) & 0xFFFF, wy = (int)(sc->fy[n] * 16.0) & 0xFFFF;
        w8(sc->mem, 0x7E1800 + X2, wx); w8(sc->mem, 0x7E1801 + X2, wx >> 8);
        w8(sc->mem, 0x7E1880 + X2, wy); w8(sc->mem, 0x7E1881 + X2, wy >> 8);
        continue;
      }
      // C0/AA07 lifecycle (ROM AA1B-AA27, mirror move.c lines 72-79): b00 = the $7F0B00 mirror.
      int b = sc->b00[n], clamp;
      if (b == 0) clamp = 1;                           // AA1F: ==0 -> per-axis tile-edge CLAMP this frame
      else { if (b > 0) sc->b00[n] = 0; clamp = 0; }   // AA23: >0 -> clear to 0 + FREE; <0 -> leave + FREE
      if (clamp) {                                     // ROM C0/AA07 clamp is PER AXIS (aa07Axis X then Y):
        int xc = (ntx != ((int)sc->fx[n] >> 4));       // a crossing axis is HELD PUT + kills the ticket
        int yc = (nty != ((int)sc->fy[n] >> 4));       // ($1A01:=0); the NON-crossing axis still advances
        if (xc || yc) {                                // this same frame -- so a diagonal step where only
          if (!xc) sc->fx[n] = nfx;                     // X did not cross -> advance X
          if (!yc) sc->fy[n] = nfy;                     // Y did not cross -> advance Y
          sc->cycles[n] = 0; continue;                 // either crossing zeroes the ticket (1-tile pause)
        }
      }
      sc->cycles[n]--; sc->fx[n] = nfx; sc->fy[n] = nfy;   // FREE (mode>=3 b!=0) or non-crossing clamp: advance both
    }
  // ---- THE POSITION COMMIT (ROM C0/AB25 inside AAFD, and aa07Axis inside AA07) -----------------
  // THE ROM HAS ONE POSITION STORAGE. $1800/$1880,X *is* the position, written by whichever integrate
  // arm ran, before A88D and the four legs read it back. This port keeps the live value in sc->fx/fy
  // and used to mirror it for PC-mode actors ONLY (the `st < 3` arm above), which left $1800 frozen at
  // the seed for every NPC -- and C0/AB45, the leg that computes the screen position the whole draw
  // list is keyed on, reads exactly those bytes. So the legs cannot be wired on a partial mirror: they
  // would sort every NPC into the bucket its frame-0 position belongs to.
  //
  // Mirroring EVERY positioned object is not a new claim invented for the legs -- it is the ROM's own
  // invariant, and it is what makes $1800 a graded byte instead of an unmaintained one (the write mask
  // claims exactly the addresses we change, so an object whose position did not move contributes
  // nothing). The interleaving is inert: A810 runs obj k's legs before obj k+1's integrate, but the
  // integrate touches only per-object tables ($1800/$1880/$1900/$1A01/$7F0B00), never the shared draw
  // list or the camera the legs read, so committing all of them first produces the same leg sequence.
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
  for (int n = 0; n < sc->nobj; n++)                     // op-0x88 palette rotation: advance the phase
    if (sc->palRot[n].active && ++sc->palRot[n].ctr >= sc->palRot[n].rate) {   // one step every `rate` frames
      sc->palRot[n].ctr = 0;                              // ROM FD/E532 cyclic-shifts span+1 colours
      sc->palRot[n].phase = (sc->palRot[n].phase + 1) % (sc->palRot[n].span + 1);
    }
  // (The iris is WRAM now -- $0139..$013D and the window tables, stepped by C0/21E1 before the pass: ctIrisFrame.)
  // ---- THE $0F00 ACTIVATION PASS (ROM C0/A810 + C0/A88D) ------------------------------------
  // $0F00,X is NOT a script hide flag -- it is the per-frame CAMERA-WINDOW ACTIVATION LATCH, and it
  // is what the ROM's two object scans actually test: the activate scan C0/1CFC and the player
  // proximity/touch scan C0/9923 both skip an object whose $0F00,X low byte is 0. The SCRIPT's own
  // visibility lives in $1A81 (ops 0x90/0x91/0x7E, C0/4CD9).
  //
  // Class filter, C0/A81B:  CMP #03 BCC ->A870 (classes 0-2, the PCs: AAFD then the same A88D test)
  //                         CMP #08 BEQ ->A83E (class 8: SKIPPED -- $0F00 untouched)
  //                         CMP #07 BEQ ->A86B (class 7: JSR C0/A947, then the loop tail -- also
  //                                             untouched, no window test at all)
  //                         otherwise A82A: AA07 then A88D.
  // Window test, C0/A88D:   skip (deactivate) if $1100,X bit7, or $1A81,X == 0, or $1A81,X bit7;
  //                         else $C3 = $1D0A-4, $C4 = $1D0C+4, $C5 = $1D0E-2, $C6 = $1D10+5, each
  //                         SATURATING (0 on borrow at A8A3/A8BB, 0xFF on carry at A8AF/A8C7); then
  //                         `LDA $1801,X / ASL A` -- the object's TILE, DOUBLED into the camera's
  //                         units -- must be >= $C3 and < $C4, and the same for $1881,X vs $C5/$C6.
  //                         (The margins are in TILE*2, i.e. +/-2 tiles, -1 tile, +2.5 tiles.)
  //
  // VALIDATED AT 0.000%: this predicate reproduces the oracle's own $0F00 for every object the pass
  // touches, on all 130 frames of the leftward walk at loc $082 -- 0 of 3770 samples wrong. Getting
  // there took two wrong scorings worth remembering: evaluating all 64 slots instead of the scene's
  // 36 (unused slots hold stale non-zero $0F00) read as 12% error, and testing the margins in TILE
  // units instead of tile*2 read as 3%.
  //
  // A8E5's "already live" check (BMI ->A90E) does NOT affect the latch VALUE -- it only skips the
  // one-shot setup ($0E80/$0E81, $0F01 = 0xFF, the palette/list calls) for an object that was
  // already active. So the latch itself is a pure function of the state above, no hysteresis.
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
      // A89B-A8C9 store the four window edges into the direct page ($C3-$C6 with D = $0100), 8-bit
      // each, and they run for every object that clears the two gates above -- A88F's $1100 bit7 and
      // A894's $1A81 tests both branch to A910 BEFORE the stores. The port used to keep them in C
      // locals; they are real memory, shared with C0/B788's emit (which overwrites $C3/$C5 as WORDS),
      // and a frame in which nothing is emitted ends on THESE values.
      // ...and it belongs to the same all-or-nothing set, which is itself a measurement: C0/B788's emit
      // overwrites $C3/$C5 as WORDS, so the value a frame ENDS on is the last emit's, not A88D's. With
      // the legs off no emit runs, and claiming these four then means holding the window edges where
      // the oracle holds a screen position -- 4 masked divergences at f1, measured.
      if (live && legsOn()) {                                   // == cleared both gates (see `live` above)
        w8(m, 0x7E01C3, (uint8_t)c3); w8(m, 0x7E01C4, (uint8_t)c4);     // A8A5 / A8B1
        w8(m, 0x7E01C5, (uint8_t)c5); w8(m, 0x7E01C6, (uint8_t)c6);     // A8BD / A8C9
      }
      if (live) {
        int tx = (r8(m, X + 0x1801) << 1) & 0xFF;              // A8D0: LDA $1801,X / ASL A (8-bit) -- the tile
        int ty = (r8(m, X + 0x1881) << 1) & 0xFF;              // A8DC: LDA $1881,X / ASL A    BYTE (see B155 above)
        live = (tx >= c3 && tx < c4 && ty >= c5 && ty < c6);    // A8D1-A8E3: BCC/BCS out
      }
      // THE PALETTE EDGES. C0/A88D is ONE routine with two exits, chosen by reading THIS latch
      // before overwriting it: A8E5 `LDA $0F00,X / BMI ->A90E` skips assign when already active,
      // so assign is the RISING edge and A905 -- the instruction right after the latch store -- is
      // the only caller of C0/7170 here; A910 `LDA $0F00,X / BPL ->A934` skips release when already
      // inactive, so release is the FALLING edge at A91B. The previous read IS the edge, which is
      // why this lives here and not in a pass of its own.
      // Measured: obj32's $7E0F40 goes 00 -> 80 at f73 with the writer reported as C0:A905, and
      // $0B81 is claimed that same frame and NEVER AGAIN in 200. The port used to assign every
      // frame to every script-visible object, which was the whole defect.
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
        // A8E5 `LDA $0F00,X / BMI ->A90E` -- the previous latch IS the edge detector, so this whole
        // block is the RISING edge's one-shot and A90E's `SEC / RTS` is the "already active" arm.
        if (!(prev & 0x80)) {
          w8(m, X + 0x0E80, 0x80);                              // A8EC  prev-link := none
          w8(m, X + 0x0E81, 0x80);                              // A8EF  next-link := none
          w8(m, X + 0x0F00, 0x80);                              // A8F2  the latch itself
          w8(m, X + 0x1681, 0x00);                              // A8F7  anim PHASE
          w8(m, X + 0x1601, 0x00);                              // A8FA  anim TIMER
          w8(m, X + 0x1B00, 0x00);                              // A8FD  the staging counter C0/B701 reads
          w8(m, X + 0x0F01, 0xFF);                              // A902  the "no frame yet" sentinel C0/CAD9 gates on
          palAlloc7170(m, o);                                   // A905  JSR C0/7170  the OBJ palette allocator
          CtObjSlotAlloc(R, (int)idx);                          // A908  JSR C0/6F9A  the OBJ slot allocator
          CtObjListLink(R, (int)idx);                           // A90B  JSR C0/A9CD  LINK into the draw list
        }
        // ...and back in A810, the carry being SET runs THE FOUR LEGS (A832-A83B, and identically at
        // A878-A881 on the state<3 arm). The unlink/relink BRACKET AB45 because the bucket is keyed on
        // the screen Y: the object must leave the bucket its OLD $0A80 belongs to before AB45
        // overwrites it, and re-enter on the NEW one.
        CtAnimTimerTick(R, gRomImg, (int)gRomImgLen, (int)idx);  // A832  JSR C0/C98A
        CtObjListUnlink(R, (int)idx);                            // A835  JSR C0/A98A
        CtObjScreenPos(R, (int)idx);                             // A838  JSR C0/AB45
        CtObjListLink(R, (int)idx);                              // A83B  JSR C0/A9CD
      } else {
        // A910 `LDA $0F00,X / BPL ->A934` -- the FALLING edge. Note the ROM tears down FIVE things
        // here, not just the palette: the arena run, the palette, the OBJ slot, the draw-list link,
        // and (A934, on BOTH arms) it forces $7F0B01,X back to 2.
        if (prev & 0x80) {
          w8(m, X + 0x0F00, 0x00);                              // A915  STZ $0F00,X
          CtArenaFreeRun(R, (int)idx);                           // A918  JSR C0/EA42 (the whole consecutive RUN)
          palRelease734C(m, o);                                  // A91B  JSR C0/734C
          CtObjSlotFree(R, (int)idx);                            // A91E  JSR C0/7056
          CtObjListUnlink(R, (int)idx);                          // A921  JSR C0/A98A
          // A924-A932: `LDA $7F0A00,X / BEQ ->A934 / LDA $30 / BNE ->A934 / LDA #0D / STA $29`.
          // DP = $0100, so $30/$29 are $0130/$0129. Ported verbatim rather than skipped -- if it ever
          // fires the write mask is what will say whether 0x0D is right.
          if (r8(m, 0x7F0A00 + idx) != 0 && r8(m, 0x7E0130) == 0) w8(m, 0x7E0129, 0x0D);
        }
        // A934 `LDA $7F0B01,X / BEQ ->A940 / LDA #02 / STA $7F0B01,X`. This is the blocking-anime
        // busy byte (op 0xAB/0xB7): an object that is off-camera has its "playing" state FORCED back
        // to 2, i.e. it can never reach the completion C0/2EA3 reads. Runs on both A910 arms.
        // ⚠ On the FLAT image, not r8/w8: those stop at $7F:03FF (WM_LEN), so through them this
        // read gave 0 and the store was dropped -- the force was a silent no-op until the anim family
        // moved onto the image (the same buffer CtAnimTimerTick's CA4F arm writes, R = m->s).
        if (R[0x10B01 + idx] != 0) R[0x10B01 + idx] = 0x02;
      }
    }
    // A846-A867 -- THE PARTY RE-LINK TAIL, run once after the object loop and easy to miss because it
    // is past the `BMI ->A819` that closes the loop. `LDA $99` / `LDA $97` are 8-bit reads of the
    // party-slot table ($0199 = member 2, $0197 = the lead; 0x80 = empty), `TAX` makes the byte the
    // obj*2 index, and each is unlinked and relinked IF its activation latch is non-zero -- which
    // re-seats the PCs in their bucket chain every frame. Slot 3 ($019B) is NOT in the tail; that is
    // the ROM's own asymmetry, transcribed rather than tidied.
    { static const uint32_t kSlot[2] = { 0x7E0199, 0x7E0197 };   // A846 then A858, in that order
      for (int k = 0; legsOn() && k < 2; k++) {
        int b = r8(m, kSlot[k]);
        if (b & 0x80) continue;                                  // A848 / A85A  BMI -> skip
        if (r8(m, 0x7E0F00 + (uint32_t)b) == 0) continue;         // A84B / A85D  BEQ -> skip
        CtObjListUnlink(m->s, b); CtObjListLink(m->s, b);         // A852/A855, A864/A867
      } }
    // ---- C0/00D6 `JSR C0/CA76` -- THE DRAIN, ONCE PER FRAME ------------------------------------
    // Once, not per object: the call-site census (a byte scan for `20 xx xx` across bank $C0) finds
    // C0/CA76 JSR'd from 00D6 and NOWHERE else. Calling the per-object VISIT (C0/CAD9) speculatively
    // instead is what collapsed --portrun from 1592 to 156 -- every object claims an arena slot, the
    // fifth claim fails and the frame is dropped.
    // THE PASS'S DEADLINE is the zelda3 model's VIRTUAL BEAM (scanline -1, move.c CtAnimDrainPass / src/ct_vbeam.h,
    // the user's decision 2026-09-27): the V counter the ROM reads at CA88/CA8C advances with the pass's own visits
    // and CA99 stops it at the ROM's line $6B -- two visits a pass at $6B = 0x78 -- the same V the oracle's drain reads
    // under CT_ORACLE_VIRTUAL_BEAM. Until then the port passed 0xF0 (CA94's vblank = no deadline) and emptied the queue
    // every frame: the objects past the fourth arena slot were popped with CLC and lost their frame on every step (the
    // attract's 0A5 bats in the hanging pose ~455 frames, Zombor's legs frozen on the 087 bridge).
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
    // ---- C0/00DB `BRL C0/B271` -- THE DRAW PASS ------------------------------------------------
    // In the same tick because C0/B701, the arena RELEASE, is reached only from C0/B309 inside it. The
    // legs stage frames into the arena and the drain commits them; without this the four slots are
    // never handed back and the pool exhausts. See CtAnimDrawPass for what it does and does not port.
    if (legsOn()) CtAnimDrawPass(m->s, NULL);
    // C0/00C7 `JSL FD/C2C1`, after the runner: builds one of the two HDMA table SETS ($0153 bit 0 clear -> set B,
    // $7F:1238-14F0, through ($C2E5,X); set -> set A, $7F:0F80-11E1, through ($C2DF,X)) and FLIPS the bit (C2CE
    // TSB / C2DA TRB). The NMI's FD/C1EE then programs the set the bit names (`AND #0F / BEQ` -> set A). The
    // window wipe's picture depends on which set is live (field.c fieldWipeWindow). Measured: $0153 alternates
    // 00/01 every frame (--read --settle 0..5 on the 16E wipe state).
    CtHdmaTableBuild(m->s);
    // C0/00CD `JSL FD/FFF7` (-> FD/E39C), the very next instruction: one frame of the twelve palette-effect records
    // $0520..$05AF -- the ones op 0x88 arms (`88 4x` fade to white, `88 5x` scale) -- writing the live CGRAM page
    // $7E:2200 the NMI uploads. The same body the battle's frame service ticks (src/objslot.c). Measured on the
    // attract's 0AD: C0:4985 arms $052C = 40 at f1569 and FD:E863 walks its acc from the next frame on.
    // ...and the location's own colour animation, modes 1/2/3/8 (records 0-5, FD/E292 at the load), with op 0x88's
    // `88 20` / `88 30` rotations (disasm/notes/field_bg_anim.md section 3).
    ctObjSlotFrame(sc);
  }
  ctXferAfterVm(sc);
}

int CtSceneActorCount(const CtScene* sc) { return sc->nobj; }
void CtSceneActor(const CtScene* sc, int obj, int* placed, int* sprite, int* x, int* y, int* facing) {
  const CtVmActor* a = &sc->actors[obj];
  // THE RENDER PREDICATE IS $0F00, not `draw`. C0/B0FC's `JSR C0/B127 / BCC ->B11A` is what decides
  // whether an object is linked into the draw list at all, and C0/B127 wants BOTH halves: $1A81
  // shown/not-removed AND inside the camera window. $0F00,X is precisely C0/A810's latch of that
  // pair, so it -- not the script-visibility byte alone -- is the "would the ROM draw this" answer.
  // Third consumer of the same rule, after npcSolidTile (C0/5BC3) and CtSceneActorTouchable; `draw`
  // keeps the $1A81 sense for CtSceneActorHidden, which asks the different question "did a script
  // explicitly hide it". Splitting them is what VMDIFF_DRAW exists to keep honest.
  *placed = a->defined && !a->dead && a->hasPos
            && r8((Mem*)sc->mem, 0x7E0F00 + (uint32_t)obj * 2) != 0;
  // There is NO palette-op term in the ROM's gate. C0/A88D (per frame) and C0/B127 (the rebuild) test
  // exactly $1100,X bit7 / $1A81,X in 1..7F / the camera window, and the OAM drain C0/CAD9 re-tests the
  // same triple. A `colorGated && portalCtr <= 0 -> hidden` term used to sit here ("the vortex is drawn
  // only while the iris is open"): it hid EVERY object whose script carries an `88 8x` palette blob --
  // Manoria's four flame objects (scene 129 obj16 fn5 `90 / 88 82 ..`) and the Naga-ette parked on the
  // same tile, so the nuns never caught fire (defect #15). The swirl is hidden by the PPU WINDOW, i.e.
  // field.c's iris clip, which keys on CtSceneActorColorGated and is unchanged.
  *sprite = a->sprite; *x = (int)sc->fx[obj]; *y = (int)sc->fy[obj]; *facing = a->facing;
}
// op-0x88 palette rotation for actor `obj` (the Telepod swirl). Returns 1 + fills the rotation
// (start colour index, span, current phase) if active; the colorGated pair (spr 94+95) shares the
// emitter's slot, so a gated object with no slot of its own inherits the scene's active rotation.
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
// op-0x88 BLOB palette overwrite for actor `obj` (the portal contour). Returns the colour count
// (0 = none) + fills start + the BGR555 colours; the colorGated pair shares the emitter's blob.
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
int CtSceneActorColorGated(const CtScene* sc, int obj) {   // 1 = an op-0x88 colour-gated sprite (the swirl)
  return (obj >= 0 && obj < sc->nobj) ? sc->actors[obj].colorGated : 0;
}
// Write an actor's live position. field.c calls this every free-control frame for PC1 so
// obj 1 tracks the input-driven player; otherwise its position stays frozen wherever the
// last script left it (the bed-exit), and the next script that drives PC1 (e.g. the curtain
// activate calling Crono's raise-arms fn) snaps the player back there.
void CtSceneSetActorPos(CtScene* sc, int obj, int x, int y) {
  if (obj < 0 || obj >= sc->nobj) return;
  sc->fx[obj] = x; sc->fy[obj] = y;
  sc->actors[obj].x = x; sc->actors[obj].y = y; sc->actors[obj].hasPos = 1;
}
// SUB-PIXEL variant (battle_ai_interp.md §12): the per-frame lead<-player sync must NOT floor -- the ROM
// has ONE position storage ($1800/$1880, 16-subpixel) and the player mover's fraction persists into any
// scripted walk that takes over (the D9 staging walk's step count depends on it: the floored 164.0 start
// cost one extra 1.9375px step vs the oracle's 164.9375 -> the +1px staged PC record).
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
  sc->actors[obj].draw = 0; sc->actors[obj].defined = 1;   // (keep solid as-is, like the real op 0x5C: the lead's
                                                       // collision excludes followers, so the solid flag is moot)
  int lead = sc->playerObj;                             // place it on the lead (the join start) if unplaced
  if (!sc->actors[obj].hasPos && lead >= 0 && lead < sc->nobj && sc->actors[lead].hasPos)
    CtSceneSetActorPos(sc, obj, (int)sc->fx[lead], (int)sc->fy[lead]);
  sc->actors[obj].hasPos = 1;
}

// PARTY CONGA-LINE: trail the active-party FOLLOWERS (move-mode 1 = member 2, mode 2 = member 3) behind
// the lead PC along its exact path. The ROM does this in the player controller (C0/93E1+), every field
// frame -- NOT the event VM -- so field.c calls this AFTER CtSceneTick (the lead's pos is already current).
// Model (oracle byte-exact vs the scene-8 2-member states, DELAY=23 CLAMP=15): push the lead's pos into a
// per-frame ring; a follower targets buffer[head - 23*member], clamped to the newest entry that is still
// >= 15px (manhattan) from the lead (so it stops ~1 tile back instead of overlapping when the lead halts),
// and walks its own index 1 entry/frame toward that target. A follower that is running a SCRIPT (st.addr
// >= 0, e.g. a scripted walk or the join cutscene) is left to the script -- the follow is roam-only.
#define CT_FOL_DELAY 23
#define CT_FOL_CLAMP 15
// ============================================================================================
// THE PARTY CONGA == THE ROM'S VELOCITY RING (disasm/notes/party_follower_ring.md, defect #20:
// "frog/lucca is glitching when joining the party and following crono").
//
// What this replaced was INVENTION. The old model was a ring of lead POSITIONS with a 23-entry delay,
// a 15-entry clamp, a per-follower off-path OFFSET, a re-anchor on script resume and a 1px/frame decay
// -- five interacting knobs, tuned until the two-member scene-8 states came out byte-exact, and wrong
// on the very first frame of a three-member party. The ROM has none of it.
//
// C0/9E3F -- THE PUSH, once per frame the LEAD MOVES:
//     LDA $AB / INC A / AND #7F / STA $AB / TAX      the head, 0x80 entries, wraps
//     LDA $1D32 / STA $7F0C00,X / STA $1900,Y        the lead's velocity  (12.4 signed: F0 = -1px)
//     LDA $1D33 / STA $7F0C80,X / STA $1980,Y
//     LDA $1D2C / STA $7F0D00,X                      the PRE-ADJUST pair (88E5's direction intent)
//     LDA $1D2D / STA $7F0D80,X
//     LDA $0C00,Y / STA $7F0E00,X                    both sprite-front priority flags
//     LDA $0C01,Y / STA $7F0E80,X
//     LDA $1D34 / STA $7F0F00,X                      the terrain/shape byte (stairs, ladders)
//
// C0/A26B -- MEMBER 2 ($99 = its object offset, $AC = its cursor). C0/A2CE is the same routine with
// $9B / $AD, and its gap is measured against $AC (A2E0 `LDA $AC / SEC / SBC $AD`) -- the conga is a
// CHAIN, each member trailing the one in front of it:
//     LDA #00 / STA $1900,Y / STA $1980,Y            default: NO velocity -> stand still
//     LDA $1D32 / ORA $1D33 / BEQ ->A28D             is the LEAD moving this frame?
//     LDA $AB / SEC / SBC $AC / AND #7F / CMP #18 / BCS ->A29D    moving:  gap >= 0x18 (24)
//     LDA $AB / SEC / SBC $AC / AND #7F / CMP #10 / BCS ->A29D    stopped: gap >= 0x10 (16)
//   A29D: LDA $AC / INC A / AND #7F / STA $AC / TAX  ADVANCE one entry...
//     LDA $7F0C00,X ... STA $1900,Y                  ...and copy ITS velocity + priority flags
//
// THE FOLLOWER'S POSITION IS NEVER WRITTEN. It is handed a VELOCITY and the ordinary mover integrator
// (C0/AAFD) moves it -- which is why its sub-pixel FRACTION survives the whole walk. That is the bug the
// position model could not express: the oracle's follower keeps the `1` of $2AF1 for forty frames while
// a position copy flattens it to 0 on frame 2.
//
// MEASURED, saves/battle_re/party3_field.state, Left held (--monitor 7E01AB:3,7E1806:2,7E1808:2,7E1906:1,7E1908:1):
//     f0   $01AB/AC/AD = 6F 6F 6F   both followers at X=$2AF1, velocity 0
//     f1   head 70                  the lead's velocity is F0 from here on; the head steps 1/frame
//     f24  head 07, $AC 6F -> 70    gap (07-6F)&7F = 0x18 EXACTLY -> member 2 advances, $1906 = F0,
//                                   and its X walks F1 -> E1 -> D1 (-1px, fraction 1 kept)
//     f47  $AC 07, $AD 6F -> 70     gap (07-6F)&7F = 0x18 -> member 3 advances, $1908 = F0
// So member 2 starts on f24 and member 3 twenty-three frames later, off member 2's cursor. Nothing here
// is a tuned constant: 0x18/0x10 are the two CMP immediates and the 0x80 wrap is the AND #7F.
//
// NOT DERIVED, and therefore not ported: the sub-threshold HOLD path C0/9F6F does more than stand still
// when the shape byte is nonzero -- it reads $7F0F00/$7F0C00/$7F0D00 and realigns a follower holding
// position on STAIRS/LADDERS. On flat ground ($7F0F00 = 0) it falls through to the same facing dispatch
// as the advance path, and the `$1900/$1980 = 0` default set at A26D is the whole of its effect, which
// is what this port implements. Also open: what makes $1D32|$1D33 nonzero on a frame the lead is being
// moved by a SCRIPT rather than by input (the roam gate below stands in for it).
// The follower's FACING + ANIMATION arm. Three ROM entries, one shape:
//   C0/9ED1 (member 2) and C0/9F20 (member 3), reached by `BRL` from the ADVANCE tails A2CB / A338 with
//   X = the entry just copied; and C0/9F6F, reached from all four HOLD sites (A28A/A29A/A2ED/A307) with
//   X = the entry the cursor is holding on. All three open `STZ $E5 / LDY $6D / LDA $7F0F00,X` (the
//   entry's SHAPE byte, = the lead's $1D34 when it was pushed) and end in `JSR ($9FF7,X)` -- the same
//   handler table as the lead's C0/9E84, so the facing stores and the A171/A18C/A1E9 tails are shared
//   (move.c CtAnimDispatch9FF7). What differs is the INDEX:
//   * after an ADVANCE (9EFC/9F4B): fold($7F0D00,X) >> 3 | fold($7F0D80,X) -- the entry's PRE-ADJUST pair,
//     exactly the lead's own index at push time -> walk (A171, $1680:=1) or run (A18C, $1680:=6; a
//     follower is never $0197, so never the head-shake);
//   * on a HOLD (9F7B-9FCF): each axis is (pre-adjust - adj(velocity)), where adj() first moves a velocity
//     with bit 3 set by 8 AWAY from zero (`BPL / BIT #08 / BEQ / SEC / SBC #08` on a negative byte,
//     `CLC / ADC #08` on a positive one), then `EOR #FF / INC A` negates it and `ADC $7F0D00,X`
//     adds the pre-adjust; a negative sum folds as (0x20 - v). For an ordinary entry (pre == velocity)
//     that is 0 -> slot 0 = C0/9FF2 -> the IDLE chain A1E9 ($1680:=0 through A211 unless the blink
//     timer $0158 has climbed) -- which is how a follower that has caught up stops walking.
//   The stair shape (bit 2 of $7F0F00,X): after an advance, $7F0D80,X == 0 clears phase+timer (9EF1) and
//   anything else is `BRL ->A254` (mode $1B; the CMP #18 before it has no consumer); on a hold (9FD3) the
//   phase+timer clear needs $7F0D80,X == 0 OR $7F0C80,X == 0 OR $7F0C80,X == $7F0D80,X, else nothing.
// Measured (bell fixture, --monitor "7E1684:2,7E1604:2"): f504 $1684=0101 with Marle walking, f537 0000
// at the stop -- the bytes this arm now writes.
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
    int sh = (bx >> 3) & 0xFF; w[0x01DB] = (uint8_t)sh;       // 9F07 LSR x3 / 9F0A STA $DB
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
  int ax = w[0x10C00 + cur];                                  // 9F7B LDA $7F0C00,X
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
  // A follower carried in from another scene (op 0x5C load on a transition) has its sprite + move-mode 1/2
  // but NO position yet -- the party spawns stacked on the lead, then spreads as you walk. Place it.
  for (int o = 0; o < sc->nobj; o++)
    if ((sc->mode[o] == 1 || sc->mode[o] == 2) && o != lead && !sc->actors[o].dead && !sc->actors[o].hasPos) {
      sc->fx[o] = sc->fx[lead]; sc->fy[o] = sc->fy[lead];
      sc->actors[o].x = (int)sc->fx[o]; sc->actors[o].y = (int)sc->fy[o]; sc->actors[o].hasPos = 1;
      sc->folRoamPrev[o] = 1;
    }
  // THE GATE IS THE FOLLOWER'S OWN SCRIPT OP. C0/A26B and C0/A2CE have exactly one caller each -- C0/3037
  // and C0/3027 inside C0/3002, which is op 0xB0 (and, through C0/2FFD `JSR 3002 / INX`, op 0xAF):
  //   3002  LDA $38 / BEQ ->3009 / TYX / CLC / RTS      ; $0138 != 0 (the camera ALIGN machine, C0/885A)
  //                                                      ;   -> nothing at all, for the lead AND the followers
  //   300F  LDA $1100,X / BEQ ->3040 (lead: $1A01:=1, JSR 9E29) / DEC A / BEQ ->3030 ($1A01:=1, JSR A26B)
  //         / DEC A / BEQ ->3020 ($1A01:=1, JSR A2CE) / else CLC RTS
  // A PC's fn0 is `5C 00 B0` (scene 5 obj2, oracle `--followvm --obj 2`: ip=05D2, raw `00 B0`, st=01) --
  // the init RETURN at prio 7 does `TYX / CLC` (C0/5FB3), i.e. the ip steps PAST the 00 and the object sits
  // on the B0 forever, executing it on every visit. So the conga runs on precisely the frames the follower's
  // script executes B0 with $0138 == 0; a follower whose ip a CALL or op 0xD9 (C0/32F4) moved elsewhere
  // gets nothing, and neither does one parked on 0xB2 (C0/3543 is a bare yield). The old CT_FOL_ROAMS
  // macro (B0 OR B2 OR no script, AND the host's explore/moveLock) was a stand-in for this.
  // Not modelled: a visit the runner's raster budget skipped (C0/5A5D) -- this pass runs once per frame.
  // Op 0xAF runs the same C0/3002 (C0/2FFD `JSR 3002 / INX`) and leaves the ip one byte PAST itself: a follower whose
  // visit last ran it (atB0, set for 0xB0 and 0xAF alike) sits right after an 0xAF.
  #define CT_FOL_ON_B0(o) (sc->st[o].addr >= 0 && sc->st[o].addr < sc->blen && (sc->body[sc->st[o].addr] == 0xB0                            || (sc->atB0[o] && sc->st[o].addr >= 1 && sc->body[sc->st[o].addr - 1] == 0xAF)))
  int haveAny = 0;
  for (int o = 0; o < sc->nobj; o++)
    if ((sc->mode[o] == 1 || sc->mode[o] == 2) && o != lead && sc->actors[o].hasPos && !sc->actors[o].dead)
      { haveAny = 1; break; }
  if (!haveAny) return;                                 // solo party: the ring simply is not consulted

  // ---- C0/9E3F, the push. $1D32/$1D33 are the lead's post-collision velocity, published by field.c
  // straight out of CtPlayerControl (the byte-exact C0/881E port) -- the same bytes the ROM reads here.
  int leadMoving = (w[0x1D32] | w[0x1D33]) != 0;        // A275 LDA $1D32 / ORA $1D33 / BEQ -- the THRESHOLD's
                                                        // test, and only that one.
  // THE PUSH'S OWN GATE IS THAT THE LEAD ACTUALLY MOVED, which is a DIFFERENT test. No `JSR C0/9E3F`
  // exists in the image -- the push is entered by a branch and its condition is not decoded (dossier
  // section 3) -- but the behaviour is measured and unambiguous. On two_member.state, --monitor
  // "7E01AB:1,7E1D32:2,7E1806:2" walking right: f3 head 03 lead $08B0, **f4 head 03 lead $08B0**,
  // f5 head 04 lead $08C0. At f4 the velocity is still $10 -- nonzero -- yet the lead did not move and
  // NO entry was pushed. Gating the push on the velocity instead would have put one extra entry in the
  // ring, and the follower would start 1 px early and stay 1 px ahead for the whole walk (measured:
  // --followtest maxerr 1px, framesOff 216 of 240). On saves/battle_re/party3_field.state the lead moves
  // every frame and the two tests coincide, which is exactly why one fixture cannot settle this.
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

  // ---- C0/A26B / C0/A2CE, the two followers, in chain order.
  for (int member = 1; member <= 2; member++) {
    int curAddr  = 0x01AB + member;                     // $01AC (member 2), $01AD (member 3)
    int prevAddr = 0x01AB + member - 1;                 // ...measured against the one IN FRONT
    int o = -1;
    for (int k = 0; k < sc->nobj; k++)
      if (k != lead && sc->mode[k] == member && sc->actors[k].hasPos && !sc->actors[k].dead) { o = k; break; }
    if (o < 0) continue;
    // C0/3002 RUNS WHEN THE OP IS DISPATCHED: A26B / A2CE have no other caller. A follower whose visit this frame ENDED
    // on its B0 without dispatching it -- a RETURN onto the B0 with the five-op budget spent (C0/5AB6) -- gets no conga
    // that frame; its next visit runs it (landedB0). MEASURED, the attract's loc 0E6 (the rule's cartridge from boot, `--monitor 7E168C --writer` and
    // `--followvm --obj 6`): Robo's fn7 ends `AD 20 / 4F 0C 0F / 4F 25 10 / 02 00 33 / 00` -- the wait's last pass plus
    // four ops spend the visit at c7669, the ip lands on 06A2's B0, and C0:A21C (A1E9's idle chain, reached from the B0)
    // zeroes $168C at c7670; the port zeroed it in the RETURN's frame (ROADMAP F2).
    int roaming = CT_FOL_ON_B0(o) && !g_landedB0[o] && w[0x0138] == 0;   // C0/3002: the op ran, and $0138 let it through
    // ...BUT NOT A TICKET THE NEW FUNCTION HAS ALREADY ISSUED. This pass runs after the whole runner, so on the frame a
    // follower leaves its B0 its new function has had its visit: the REQ that moved it (op 0x02-0x07's preempt arm,
    // C0/6031+6034 = reqOp) cleared $1A80/$1A01 BEFORE that visit, and a move op in it armed a fresh pair, which C0/A810
    // integrates in this same frame -- nothing in the ROM clears it again. The release below cleared it, so a follower's
    // first scripted step came one frame late. MEASURED on the rule's cartridge, the attract's 112 (--cputrace from a
    // state at c4606 + --monitor 7E1180,7E1880): at c4609 obj0's `02` REQs obj3 off its B0, obj3's visit runs AA 0E 89
    // and `96` (IP 03D5-03DB) and its $1880 steps 0AFF -> 0B26 in that frame; the port ran the same four ops at p4520 and
    // stepped at p4521 (CT_MVTRACE=3: `issued cyc=1 task=1`, then `skip cyc=0 task=0` at the integration). The conga
    // itself never arms a ticket (it integrates the ring's velocity itself, `hasTask` only driving the walk cycle), so a
    // live `cycles` here can only be the new function's.
    if (sc->folRoamPrev[o] && !roaming && sc->cycles[o] == 0) {   // script/cutscene takes over: release the walk cycle
      // ⭐ THE TICKET GOES WITH IT. `hasTask` and `cycles` are ONE unit -- the ROM's `$1A80,X`
      // (velocity/task) and `$1A01,X` (step ticket) -- and every other site in this file clears them
      // together, citing exactly that pair: 3526 STZ $1A80,X / 3529 STZ $1A01,X, and 5B59/5B5C.
      // This handover was the one place that split them, and the split DEADLOCKS the mover:
      // `moveOp` opens with `if (cycles > 0) return 1;` and the integration loop gates on
      // `hasTask && cycles > 0`, so `cycles > 0` with `hasTask == 0` is a state nothing can leave --
      // the op yields before it can re-arm, and the integrator skips before it can decrement.
      // MEASURED (CT_MVTRACE=4, the attract's scene 274): obj4 leaves its `B0` for `96 08 16`, the
      // move arms cycles=1/hasTask=1, this line lands the same transition, and from there every
      // single visit reads `cyc=1 task=0 pos=(136.00,159.94) v=(0.000,2.438)` -- a follower holding
      // a live velocity, never integrating, for 15000 frames. That froze the attract's last leg:
      // scene 274's obj0 sits on op 0x04 (C0/6109, call-AND-WAIT on `$7F0980,X`) waiting for obj4's
      // function to return, so `@0061 E0 DB 02 07 1C` never runs and the demo never reaches 00DB.
      sc->hasTask[o] = 0; sc->cycles[o] = 0;            // 3526 STZ $1A80,X / 3529 STZ $1A01,X
      if (sc->actors[o].animMode == 1) { sc->actors[o].animMode = 0; sc->actors[o].animIndex = 0; sc->actors[o].animGen++; }
    }
    sc->folRoamPrev[o] = roaming;
    if (!roaming) continue;                             // 3004 TYX / CLC / RTS, or the ip is not on 3002 at all
    int vx = 0, vy = 0;                                 // A26D: the DEFAULT is no velocity at all
    w[0x1900 + o * 2] = 0; w[0x1980 + o * 2] = 0;       // A26F/A272 (A2D2/A2D5)
    int gap = (w[prevAddr] - w[curAddr]) & 0x7F;        // A27D / A2E0 -- SBC then AND #7F
    int advanced = gap >= (leadMoving ? 0x18 : 0x10);   // A284 CMP #18 / A294 CMP #10, BCS -> advance
    int c;
    if (advanced) {
      c = (w[curAddr] + 1) & 0x7F;                      // A29D: advance ONE entry
      w[curAddr] = (uint8_t)c;
      vx = (int8_t)r8(sc->mem, 0x7F0C00 + c);           // A2A7/A2B6: copy that entry's velocity
      vy = (int8_t)r8(sc->mem, 0x7F0C80 + c);
      w[0x1900 + o * 2] = (uint8_t)vx; w[0x1980 + o * 2] = (uint8_t)vy;   // A2AD / A2BA
      w[0x0C00 + o * 2] = (uint8_t)r8(sc->mem, 0x7F0E00 + c);   // A2BD/A2C4: and its priority flags
      w[0x0C01 + o * 2] = (uint8_t)r8(sc->mem, 0x7F0E80 + c);
    } else c = w[curAddr];                              // A288 / A298: LDX $AC -- the entry it is HOLDING on
    // ---- the FACING + ANIMATION arm, C0/9ED1 (member 2) / C0/9F20 (member 3) after an advance, C0/9F6F
    // on a hold. Until 2026-09-05 the port derived only the facing (a hand-copied four-way switch) and
    // never wrote $1680/$1681/$1601 -- Marle walked in a fixed pose (user: 'walks without animation').
    ctFollowerAnimArm(sc, o, c, advanced);
    ctAnimSyncActor(&sc->actors[o], w, o * 2);          // the C mirrors follow the bytes the arm wrote
    sc->actors[o].facing = w[0x1600 + o * 2] & 3;
    // C0/AAFD, the ordinary mover integrate: `$1800,X += sign-extend($1900,X)` in 12.4. Adding
    // sixteenths to the double keeps the sub-pixel fraction, which is the whole point of the ring.
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
// --vmdiff's stand-in for the ROM's joypad routine (C2/8500) for the BUTTON byte pair only. Belthasar no longer
// calls it: its host runs the NMI's whole pad latch (field.c fieldPadNmi -> player.c CtJoypadPoll, $00F0-$00FB).
// $00F2 is the HELD buttons and $00F0 the NEWLY-PRESSED ones -- measured on the oracle with
// `--monitor 7E00F0:4` across a press edge, both carrying the same layout
// (A=0x80 X=0x40 L=0x20 R=0x10 B=0x08 Y=0x04 Sel=0x02 St=0x01). $00F0 is what C0/0097 latches into
// $0151 for the poll ops 0x3F-0x44.
// ⚠ THE D-PAD PAIR $00F1/$00F3 IS DELIBERATELY NOT WRITTEN. This API carries one byte: field.c
// passes only A/B/X, and --vmdiff passes `injBtn & 0xFF` on purpose -- it forwards the direction to
// the ORACLE and syncs our lead from it rather than running our player controller. Deriving the
// d-pad bytes from the high half was an invention, and it cost two red checks: ours read 00 against
// the oracle's live 02 (Left) on $00F1/$00F3 at f2. A layer claims only the bytes it is given.
void CtSceneSetButtons(CtScene* sc, uint16_t heldF2) {
  if (!sc) return;
  uint8_t held = (uint8_t)(heldF2 & 0xFF);
  w8(sc->mem, 0x7E00F0, (uint8_t)(held & ~sc->btnPrev));   // NEWLY pressed
  w8(sc->mem, 0x7E00F2, held);                             // HELD
  sc->btnPrev = held;
  sc->btnHeld = heldF2;
}
// ROM $01EB, the touch hit latch (see the struct field + op 0xB0). `obj` = the object the player's
// proximity scan overlapped this frame, or -1 for the ROM's 0x80 "nothing". Call once per frame
// BEFORE CtSceneTick, mirroring C0/881E running ahead of the event-VM in the main loop.
void CtSceneSetTouchHit(CtScene* sc, int obj) { if (sc) sc->touchHit = obj; }
// battle_end.md T2: arm the post-victory follower gather (field.c calls this on a VICTORY battle-return).
// CtSceneUpdateFollowers then walks every follower onto the leader's tile (stacked) and self-clears.
// C0/597D, the PC-object RESPAWN the post-menu reconcile C0/1A03 runs when $7E:2980-82 changed:
// re-execute one object's INIT function (fn0) and reset its per-object script state, so the PC loader
// inside it (op 0x80 / op 0x57) re-reads the party and re-derives which slot -- and which sprite --
// this object now is. Everything below is the scene-load init pass's own per-object body, for one n.
// The caller owns the POSITIONS: the ROM restores the snapshotted X/Y/prio into the objects the NEW
// $0197/$0199/$019B name, which is what makes slot 1's place on the map pass to whoever is slot 1 now.
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
  // ...and the $7E mirror the rest of the engine reads: $1100,X and the slot -> object table
  // $0197/$0199/$019B (the ROM's own C0/426A output, which is the whole point of re-running fn0).
  w8(sc->mem, 0x7E1100 + (uint32_t)obj * 2, sc->mode[obj] & 0xFF);
  if (sc->mode[obj] >= 0 && sc->mode[obj] <= 2) w8(sc->mem, 0x7E0197 + (uint32_t)sc->mode[obj] * 2, obj * 2);
}

// C0/1A03's party RECONCILE, the whole of it. The ROM walks its per-CHARACTER PC-object table
// $018D..$0193 in the order 0,1,2,4,3,5,6 and respawns EVERY character's object -- not just the ones
// that happened to be slots 0..2 -- because a re-ordered party can put a character on screen who had
// no slot a moment ago. This engine has no $018D..$0193, so the same set is identified the way the
// ROM's own loaders write it: a PC object is one whose $1100,X came from the PARTY-AWARE loader
// (op 0x57/0x5C/0x62/0x80 -> mode 0/1/2 in party, 0x80 when it aborted, C0/426A) AND whose sprite is
// a PC sprite 0..6. That excludes op 0x81's always-NPC mode 3 -- which matters, because scene 8's
// fair has NPCs wearing sprites 1 and 2 -- and op 0x82/0x83's modes 4..6.
// Returns the number of objects respawned. Positions are the CALLER's: the ROM puts the snapshotted
// X/Y/prio back into the objects the NEW $0197/$0199/$019B name.
int CtSceneReconcileParty(CtScene* sc) {
  if (!sc) return 0;
  static const int kOrder[7] = { 0, 1, 2, 4, 3, 5, 6 };   // C0/1A03's own character order
  int n = 0;
  for (int i = 0; i < 7; i++) {
    int cid = kOrder[i];
    for (int o = 0; o < sc->nobj; o++) {
      int m = sc->mode[o];
      if (!((m >= 0 && m <= 2) || m == 0x80)) continue;   // not a party-aware loader's object
      if (sc->actors[o].sprite != cid) continue;          // ...and this is THAT character's
      CtSceneReinitObj(sc, o);
      n++;
      break;
    }
  }
  return n;
}

// battle_forest_return.md AD.3c: the module epilogue dead-mark ($1000,obj |= 0x80). Mark a battle enemy's
// FIELD object REMOVED -- excluded from the activation scan (never re-drawn, never re-scheduled), the analog
// of the ROM's $1000 bit7 "gone" flag. Our `dead` flag gives placed=0 (CtSceneActor render-skip) + the
// PASS-1 scheduler skip; draw=2 tags it "removed" (not a hideable draw=1). Mover residue cleared ($1A81,Y).
void CtSceneBattleRemoveObj(CtScene* sc, int obj) {
  if (!sc || obj < 0 || obj >= sc->nobj) return;
  sc->actors[obj].dead = 1; sc->actors[obj].draw = 2;
  sc->actors[obj].cadPause = 1;                     // = the $1000 bit 7 stored below: C0/59EC and C0/5AD1 read it
  // ...and the ROM's OWN dead-mark, which is TWO bytes. C0/07ED-080C (m=1/x=0), the enemy epilogue:
  //   07ED: LDA $7E29C6,X / BNE ->080D      ; survivor -> restore field position instead
  //   07F3: LDA $1000,Y / ORA #80 / STA $1000,Y     (the store's PC reports as C0:07FB)
  //   07FB: LDA $1A81,Y / BPL ->0807 / (JSR C0/734C)
  //   0807: LDA #00 / STA $1A81,Y                   (reports as C0:080C)
  // So $1A81 := 0x00 -- NOT 0x80 -- plus the $1000 bit7 cadence pause (the bit op 0x0B writes).
  //
  // The 0x80 this used to write is not merely a different flavour of "gone": C0/0623-0635, the
  // next battle's enemy-eligibility scan, reads `LDA $1A81,X / BMI ->063F` -- bit7 makes a DEFEATED
  // enemy ELIGIBLE AGAIN. field.c:4365 mirrors that same term. On the ROM's byte the object cannot
  // re-stage (0x00 -> never activates -> $0F00 = 0 -> both terms fail); on 0x80 it can.
  // (Not yet exercised by a two-battle capture -- derived from the scan, not measured.)
  w8(sc->mem, 0x7E1A81 + (uint32_t)obj * 2, 0x00);                       // C0/0809
  w8(sc->mem, 0x7E1000 + (uint32_t)obj * 2,
     (uint8_t)(r8(sc->mem, 0x7E1000 + (uint32_t)obj * 2) | 0x80));       // C0/07F6
  sc->hasTask[obj] = 0; sc->cycles[obj] = 0; sc->mx[obj] = sc->my[obj] = 0;
}

// ---- C0/595C -- obj0's SLOT 1, run to its terminator (disasm/notes/obj0_slot1_runner.md) ----------------
//   595C REP #20 / STZ $6D / LDA $7F2003 / TAX / SEP #20    obj0's slot-1 pointer ($7F2001 + 0*32 + 1*2)
//   5967 LDA $7F2001,X / BEQ ->597C                           the 00 terminator ends it -- NOT executed
//   596D TXY / REP / AND #00FF / ASL / TAX / SEP
//   5977 JSR ($5D6E,X) / BRA ->5967                           every other op, whatever carry it returns
//   597C RTS
// It writes nothing of obj0's own script state: not $1180 (the IP), not $1C00 (the priority), not $1001, not
// the $7F0580 return rows -- so obj0 comes out of it exactly where it was, which is what the saved CtSS below
// restores. THREE callers in bank C0 (byte scan `20 5C 59`), each the step after the $7F:2000 image is built:
//   C0/56C8  the location load (C0/56B0, after the init runner C0/5709)
//   C0/0206  the menu-exit reload chain C0/01A5-0206 (C0/1975; after C0/56D4 re-decompresses the script)
//   C0/0330  the battle-return tail C0/0304-038C (after C0/56D4; before C0/A712 and the regroup C0/5B1F)
// MEASURED on the oracle, saves/attract_loc0AD_flea.state (--lifetrace --pcwatch C0/5977): f261 C0/0283,
// f268 C0/0330 -> C0/595C dispatches Y = 01F2 01FF 0204 0206 020E 0216 021E 0226 (live = body + 0x1C0) and
// the `EA 18` at body 0x0044 writes `$1E01 = 18` (C0/3C3C) / `$1E00 = 10` (C0/3C41); then C0/597C.
// A handler that PARKS (IP unchanged) would spin C0/595C forever; nothing that loads can do that, so we stop.
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
    vmOpCost(sc->mem, sc->body, sc->blen, at, 0);       // (a load's: priced as C0/5977 dispatches it)
    step(sc, 0);                                        // 5977 JSR ($5D6E,X) -- its carry is ignored
    if (sc->st[0].addr == at) { why = "parked"; break; } // the ROM would never leave C0/595C
  }
  int endAt = sc->st[0].addr;
  sc->st[0] = saved; ctSetDc(sc, 0, saved.dc);
  // obj0 before -> after: the ROM's runner leaves $1180/$1C00 alone, so the two must agree
  if (tr) fprintf(stderr, "%s %s@%04X obj0 @%04X p%d -> @%04X p%d\n", line, why, endAt & 0xFFFF,
                  saved.addr & 0xFFFF, saved.prio, sc->st[0].addr & 0xFFFF, sc->st[0].prio);
}

// battle_forest_return.md AD.3b/AD.5 step 3: the post-victory field RESUME (ROM C0/0330 -> C0/5B1F). Seed the
// arrival barrier loc[0xFF] (= $7F03FE) to 3-nFollowers, then VM-CALL the materialized RETURN block
// (sc->rgBase, ct_regroup.h) on EACH follower PC at priority 0 -- the exact C0/5B1F sequence: push the
// object's current IP under its current priority (our CtSS ret[] stack), IP := the block, prio := 0, kill
// mover/ticket/cadence. The block's `95 00` walks the follower to the LEAD, `71 FF`+`12` counts arrivals,
// and the last arriver's `E3 01` restores control (explore=1). NO followers -> explore=1 directly (C0/0359).
// The LEAD is never CALLed (nobody moves him; the followers come to him). The enemy removal + PC snap are the
// caller's epilogue (field.c). Idempotent-safe: does nothing if rgBase wasn't installed.
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
    if (s->addr >= 0) s->ret[s->prio] = ssPushAddr(s); // 5B41: PUSH current IP ($1180) under the current priority level
    s->addr = sc->rgBase;                              // 5B4C: IP := the regroup block (our addr = op position)
    s->prio = 0;                                       // 5B54: priority := 0
    ctSetDc(sc, o, 0);                                 // 5B5F STZ $1001,X -- eligible on the next build
    s->wait = 0; s->nm = 0;                            // (the call latch $7F0980 is not 5B1F's: it survives, as a pre-empt's)
    vmTicketStop(sc, o);                               // 5B59/5B5C: STZ $1A80 (velocity) / $1A01 (ticket)
    sc->mx[o] = sc->my[o] = 0; sc->vecMove[o] = 0;
  }
}
// Wire the NPC MAP-collision hook (ROM ACFD): a mover with $1C80 bit0 (collMap) won't cross into a tile
// the hook reports solid; it then re-aims via the C0/FE00 random-angle table (a bounce off the wall).
void CtSceneSetTileSolid(CtScene* sc, int (*fn)(void*, int, int), void* ctx) {
  if (!sc) return; sc->tileSolid = fn; sc->tileCtx = ctx;
}
// ---- C0/1CFC -- THE ACTION-BUTTON PROBE, byte for byte -----------------------------------------
//
// This replaces an INVENTION: a hand-written proximity box ("ACTIVATE search v2", reach 1-2 tiles,
// `dx <= 10 && dy <= 12`, nearest-wins, plus a "reach one tile further when the faced tile is solid"
// rule for talking across a counter). The ROM does something structurally different in all three
// respects -- the shape, the reach and the pick -- and needs no special case for any of them.
//
//   1CFC  REP #20 / AND #0006 / STA $DB      facing*2 & 6 -> 0/2/4/6 = the handler index
//   1D03  LDA #0080 / STA $EB                $EB = "no target"
//   1D08  LDX $97 / LDA $1800,X / STA $C3    $C3/$C5 = the PLAYER's 12.4 position
//   1D16  LDA $7F2000 / ASL A / TAX          X = the object count * 2 -- the scan starts HIGH
//   1D1C  LDA $0F00,X / BNE ->1D2D           not live/visible -> skip
//   1D21  DEX / DEX / BNE ->1D1C             ... and walks DOWN; X reaching 0 ends it (obj0 NEVER probed)
//   1D2D  LDA $1100,X / BMI ->1D21           bit 7 -> skip
//   1D32  CMP #07 / BEQ ->1D48               == 7 -> the arm that does NOT skip the player
//   1D36  CPX $97 / BEQ ->1D21               the player itself -> skip
//   1D3A  STX $EB / LDX $DB / JSR ($1D56,X)  the per-facing range test
//   1D43  BCC ->1D21                         rejected -> keep scanning
//   1D45  RTS                                accepted: CARRY SET, $EB = the target
//
// **FIRST match wins, scanning from the highest object index DOWN** -- not the nearest. That is a
// different selection rule from `min(dx+dy)` and picks a different object whenever two are in range.
//
// The four handlers (table C0/1D56 = `5E 1D 84 1D AB 1D D1 1D`, so index 0/2/4/6 = up/down/left/right)
// are the SAME SHAPE as the NPC-solid box C0/5BFA above -- signed test on the primary axis, one's-
// complement absolute value, `CMP` limit, then the cross axis -- with two different limits:
//
//   1D5E up     LDA $1880,X / SBC $C5 / BPL rej / EOR #FFFF / CMP #01C0 / BCS rej   then X vs #00C0
//   1D84 down   LDA $1880,X / SBC $C5 / BMI rej /             CMP #01C0 / BCS rej   then X vs #00C0
//   1DAB left   LDA $1800,X / SBC $C3 / BPL rej / EOR #FFFF / CMP #01C0 / BCS rej   then Y vs #00C0
//   1DD1 right  LDA $1800,X / SBC $C3 / BMI rej /             CMP #01C0 / BCS rej   then Y vs #00C0
//   cross axis  LDA .. / SBC .. / BPL +3 / EOR #FFFF / CMP #00C0 / BCS rej
//   1DA7 rej    SEP #20 / CLC / RTS          accept: SEP #20 / SEC / RTS
//
// $1800/$1880 are 12.4, so #01C0 is 28 px along the FACED axis and #00C0 is 12 px to either side: a
// directional, asymmetric box. 28 px is already ~1.75 tiles, which is why the ROM needs no "reach
// further across a blocked tile" rule to let the player talk over a counter.
//
// !! THE FIRST SBC RUNS WITH NO PRECEDING `SEC`, and the carry it inherits is NOT the one the dossier
// first guessed. `field_action_probe.md` section 3 attributed it to `1D32 CMP #07`; re-reading the
// scan, `1D36 CPX $97` runs AFTER that on the normal path and is itself a compare, and nothing
// between it and the handler touches C (`STX` sets no flags, `LDX` sets only N/Z, `JSR` none). So
// **C = (X >= $97)**, and since 1D36's own `BEQ` already removed equality, C = (obj > playerObj) --
// exactly the rule C0/5BD7's `CPX $6D` gives the solid box. The `$1100,X == 7` arm at 1D48 is the one
// that really does inherit `CMP #07`'s equality, i.e. C = 1. An object with a LOWER index than the
// player therefore measures one subpixel further away than the same object with a higher one. The
// SECOND SBC always runs with C = 0, because the last flag-setter before it is the primary axis's
// `CMP #01C0` with its BCS not taken -- so the cross-axis delta is always -1.
int CtVmActBoxAt(int mx, int my, int cx, int cy, int carry0, int facing) {
  int primaryY = (facing <= 1);                                     // 0 up / 1 down are the Y arms
  int wantPos  = (facing == 1 || facing == 3);                      // down / right require d >= 0
  int p = primaryY ? cy : cx, pm = primaryY ? my : mx;
  int q = primaryY ? cx : cy, qm = primaryY ? mx : my;
  uint16_t a = (uint16_t)(p - pm - (1 - carry0));                   // the FIRST SBC, carry per above
  if (wantPos) { if (a & 0x8000) return 0; }                        // BMI -> 1DA7
  else         { if (!(a & 0x8000)) return 0;                       // BPL -> 1DA7
                 a = (uint16_t)~a; }                                // EOR #FFFF
  if (a >= 0x01C0) return 0;                                        // CMP #01C0 / BCS -> 1DA7
  uint16_t b = (uint16_t)(q - qm - 1);                              // the SECOND SBC, carry 0
  if (b & 0x8000) b = (uint16_t)~b;                                 // BPL +3 skips the EOR
  return b < 0x00C0;                                                // CMP #00C0 / BCS -> 1DA7
}
// The scan itself. Returns the target object index, or -1 for "no target" ($EB = $80, carry clear).
// `facing` is the player's $1600 byte (0 up / 1 down / 2 left / 3 right) -- the same one `AND #0006`
// turns into the handler index.
int CtSceneActionProbe(const CtScene* sc, int playerObj, int facing) {
  if (!sc || playerObj < 0 || playerObj >= sc->nobj) return -1;
  Mem* m = (Mem*)sc->mem;
  uint32_t pX2 = (uint32_t)playerObj * 2;
  int px = r8(m, 0x7E1800 + pX2) | (r8(m, 0x7E1801 + pX2) << 8);    // 1D08: $C3
  int py = r8(m, 0x7E1880 + pX2) | (r8(m, 0x7E1881 + pX2) << 8);    // 1D0F: $C5
  // 1D16 starts at the object COUNT and walks down. Whether $7F2000 holds nobj or nobj-1 does not
  // matter here: a one-past object has no $0F00 and 1D1C skips it either way.
  for (int obj = sc->nobj - 1; obj >= 1; obj--) {                   // 1D21's `BNE` never probes obj0
    if (!sc->actors[obj].hasPos) continue;                          // no position -> no $1800 to read
    uint32_t X2 = (uint32_t)obj * 2;
    if (r8(m, 0x7E0F00 + X2) == 0) continue;                        // 1D1C: LDA $0F00,X / BNE
    int cls = r8(m, 0x7E1100 + X2);
    if (cls & 0x80) continue;                                       // 1D2D: BMI
    int carry0;
    if (cls == 0x07) carry0 = 1;                                    // 1D32/1D48: the arm that does NOT
    else {                                                          //   skip the player; C = 1 (CMP #07)
      if (obj == playerObj) continue;                               // 1D36: CPX $97 / BEQ
      carry0 = (obj > playerObj);                                   // 1D36's CPX, equality already gone
    }
    int cx = r8(m, 0x7E1800 + X2) | (r8(m, 0x7E1801 + X2) << 8);
    int cy = r8(m, 0x7E1880 + X2) | (r8(m, 0x7E1881 + X2) << 8);
    if (CtVmActBoxAt(px, py, cx, cy, carry0, facing & 3)) return obj;   // 1D45: the FIRST hit wins
  }
  return -1;                                                        // 1D25: $EB = $80, CLC
}
// Can the player ACTIVATE (action button) this object? It needs a position and a real
// FUNCTION 1 (activate handler) -- but NOT necessarily a sprite: the bedroom curtain
// (obj 10) is a sprite-less coordinate object whose fn1 toggles it open/closed.
// It must be VISIBLE though: the ROM examine/activate scan (C0:1CFC) walks the object list and
// SKIPS any object whose $7E0F00,X low byte is 0 -- which is the "not drawn / hidden" flag (op 0x91
// -> draw=1 clears it). The scene-8 Telepod GATE (obj 11) is solid + HIDDEN with fn1==fn2 = the
// Crono-follows-Marle cutscene; the ROM never lets the player activate it (it auto-runs as a script
// beat). Without this gate the C engine activated the hidden gate on ACTION = the premature teleport.
int CtSceneActorActivatable(const CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return 0;
  const CtVmActor* a = &sc->actors[obj];
  // Same gate as the touch scan: the ROM's activate scan C0/1CFC skips on $0F00,X == 0, i.e. on
  // the C0/A810 activation latch, not on the script's own visibility flag $1A81.
  if (a->dead || !a->hasPos) return 0;
  if (r8((Mem*)sc->mem, 0x7E0F00 + (uint32_t)obj * 2) == 0) return 0;
  int fn = fpPtr((CtScene*)sc, obj, 1);
  return fn >= 0 && fn < sc->blen && sc->body[fn] != 0x00;   // fn1 exists and isn't an empty RETURN
}
// Can the player TOUCH-trigger this object (walk onto it)? Like Activatable but for FUNCTION 2 (the
// touch slot). The ROM player-overlap scan (C0:9923) is structurally IDENTICAL to the activate scan:
// it walks the object list and skips hidden objects via the SAME $7E0F00,X low-byte test, then does a
// ~1-tile overlap test. So the gate is VISIBILITY (draw==0), NOT having a loaded sprite -- the Telepod
// DEMO trigger (obj 12) is sprite-less but VISIBLE/active (draw==0) and its fn2 ("All systems on!" ->
// teleport Crono to the right pad) fires when stepped on; the co-located solid gate obj 11 is HIDDEN
// (draw=1) so it is skipped (no cross-fire). The OLD touch loop required a loaded sprite (`defined`)
// and so missed the sprite-less demo trigger -- that is why stepping on the left pod did nothing.
int CtSceneActorTouchable(const CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return 0;
  const CtVmActor* a = &sc->actors[obj];
  // The ROM's proximity scan gates on the ACTIVATION LATCH, not on a script flag:
  // C0/9933 `LDA $0F00,X / BNE ->9945` -- zero means skip. That is the C0/A810 per-frame
  // camera-window latch (ported; see the pass in CtSceneTick), so an OFF-CAMERA object is
  // untouchable no matter what its script visibility $1A81 says.
  if (a->dead || !a->hasPos) return 0;
  if (r8((Mem*)sc->mem, 0x7E0F00 + (uint32_t)obj * 2) == 0) return 0;
  int fn = fpPtr((CtScene*)sc, obj, 2);
  return fn >= 0 && fn < sc->blen && sc->body[fn] != 0x00;   // fn2 exists and isn't an empty RETURN
}
// Has the VM EXPLICITLY hidden this object (op 0x91 draw=1 / op 0x7E draw=2)? Distinct from "not placed":
// requires a real, positioned sprite that was hidden -- so the field renderer can drop the lead PC during
// a script HIDE (the Telepod demo vanishes Crono) WITHOUT hiding a lead PC that simply has no VM position
// (scene 1's sleeping Crono is drawn by the field engine at px/py; its VM proxy obj2 is hasPos=0).
int CtSceneActorHidden(const CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return 0;
  const CtVmActor* a = &sc->actors[obj];
  return a->defined && a->hasPos && !a->dead && a->draw != 0;
}
// The PLAYER-controlled object = the lead PC, loaded by op 0x57 (LOADPC party-slot 0) in its init.
// This varies per scene: obj1 (Crono's room / fair), obj2 (scene 1), obj3 (the Telepod, where obj1/2
// are empty and the party is loaded into obj3-5). The field engine renders THIS object as the player
// (at px/py) and skips it in the actor loop; using the wrong index makes a script that moves the real
// lead-PC object (the Telepod demo's obj3 hop to the right pad) render a SECOND Crono ("sprite clone").
int CtScenePlayerObj(const CtScene* sc) { return sc->playerObj; }   // cached in CtSceneCreate
int CtScenePlayerLoaded(const CtScene* sc) { return sc ? sc->playerLoaded : 0; }
int CtSceneActorMoving(const CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return 0;
  // hasTask alone (NOT && cycles>0): cycles momentarily hits 0 at the end of each delay-frame move
  // segment, before step() re-issues the next one. Gating on cycles>0 made the walk anim blip to idle
  // for that 1 frame every cycle (the "flicker"). hasTask stays 1 across segments, cleared on arrival.
  return sc->hasTask[obj];
}
int CtSceneActorVecMove(const CtScene* sc, int obj) {   // 1 = active move is a vector slide (op 0x9C/0x9D):
  if (obj < 0 || obj >= sc->nobj) return 0;             // the actor keeps its idle pose, no walk cycle
  return sc->vecMove[obj];
}

int CtSceneActorSolid(const CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return 1;
  return sc->actors[obj].solid;                     // $1B01,X bit0; load-sprite sets 1, op 0x84 overrides
}

// ---- THE INVOKE PROLOGUE (C0/5AC5 activate, C0/3154 touch), in the ONE place it lives -------------
// The two ROM invokes are the SAME seven instructions with ONE immediate different -- raw bytes:
//   0x3154  [24 ROM bytes at 0x3154: python tools/dis.py 0x3154 24]
//   0x5AC5  [24 ROM bytes at 0x5AC5: python tools/dis.py 0x5AC5 24]
// i.e. LDX $EB, then four tests that each take the routine's own shared bail:
//   +02  LDA $1C01,X / BNE      ; (1) a pending cross-object wait (op 0x08 sets, op 0x09 clears)
//   +07  LDA $1100,X / BMI      ; (2) DEACTIVATED (op 0x0A, C0/6254: $1100,X := $80)
//   +0C  LDA $1000,X / BMI      ; (3) PAUSED     (op 0x0B, C0/6269: $1000,X |= $80)
//   +11  LDA $1C00,X / CMP #imm / BCC   ; (4) busy -- #02 for the activate, #03 for the touch
// The port had them in two shapes: CtSceneActivate carried only (4), CtSceneTouch carried all four in
// its own inline copy, and BOTH AND-ed (4) with an extra `st->addr >= 0` the ROM does not have --
// fail-OPEN, so the port could accept where the ROM declines. One predicate now, one immediate passed.
// (2) and (3) had been in the port all along via the touch and the move.c CALL family; the activate
// path simply never got them. (3) is the load-bearing one: the object PROBE C0/1CFC tests $0F00,X and
// $1100,X (1D1C / 1D2D) but NEVER $1000,X, and the activation pass C0/A88D tests $1100,X and $1A81,X
// only -- so a PAUSED object stays drawn, stays a probe hit, and only this prologue refuses it. Not
// academic: a decode of all 512 scene scripts (assets/script.py op_len walk from every object's every
// fn pointer) counts 521 op-0x0B sites and 823 op-0x0A sites.
//
// `prio` STANDS IN FOR $1C00,X, and that is a measured decision, not a shortcut: this engine does not
// maintain $1C00 at all. ct_vm.c reads it only to SEED (`w[0x1C00 + x] & 0x07`) and never writes it,
// and a fresh `--scene 2` run dumped at f200 (CT_DUMPWRAM) has $1C00 = 00 for all 24 objects -- so a
// literal `LDA $1C00,X / CMP #02` port would decline EVERY activate in the game. The live priority is
// `st->prio` (7 when idle, per CtSceneCreate and the op-0x00 RETURN handler C0/5F74, whose C0/5FA0
// `INC $1C00,X` walks the ladder back up), and --vmdiff already compares it against the oracle's
// $1C00 field by field.
// $1C01,X WAS the one byte here this engine did not maintain (ops 0x08/0x09 write it since 2026-09-03): its ONLY two writers in the whole
// 4 MB image are op 0x08 (C0/6240 `LDX $6D / LDA #01 / STA $1C01,X`) and op 0x09 (C0/624B `LDA #00`),
// and this VM has no handler for either -- 14 op-0x08 sites live in 5 scripts (332/338/355/371/399 =
// scenes 64/117/256/280/412), always bracketing a multi-visit op. The read is kept so the prologue
// stays whole and so a SEEDED scene (savestate, --vmdiff) is answered correctly, but on a cold scene
// it is structurally zero. Wiring it up means porting the two writers AND the $1C01 test into the live
// CALL family (op 0x02 SKIPs at C0/6039, ops 0x03/0x04 PARK) -- an OPEN gap, left whole for its own
// derivation rather than half-wired here.
// Graded by `ct.exe --sweepact`: both entries micro-executed, 324/324 verdicts, 5 mutations killed.
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
  vmTicketStop(sc, obj);                               // talking STOPS the NPC's in-flight move: else the
                                                       // preempted pacing move's hasTask lingers and she keeps
                                                       // the WALK anim through the dialogue (Marle "thank
                                                       // goodness, my pendant"). She re-issues + resumes on return.
}

// Touch trigger (slot 2): fired when the player walks into the actor -- monsters use it to
// start battles; sources of many ambush cutscenes. The ROM touch-start (C0/3170, distinct from
// the activate-start C0/5AC5) runs fn2 at PRIORITY 2 (5B/319F: LDA #02 / STA $1C00,X), NOT 1:
// it leaves prio 1 OPEN so the cutscene's helper functions can preempt it. The Marle bump
// (scene 439) depends on this -- obj3 fn2 (touch) at prio 2 CALLs obj0 fn3 which CALLs obj3 fn4
// at prio 1; fn4 (Crono's knockback, the "...!" / "I'm so sorry!" reactions) must PREEMPT fn2.
// With fn2 wrongly at prio 1 that CALL was rejected (reqOp's prio==priority guard), so the whole
// helper-reaction chain never ran. Oracle-confirmed (scene_439_marle_bump.state: obj3 $1C00=1
// running fn4 with fn2 parked at $7F0580 prio-2 slot; obj1/Crono fn3 at prio 3). Guard is the
// ROM's C0/3165 `CMP #03 / BCC skip`: skip only if already busy at prio < 3 (0/1/2).
// TEST SURFACE (CT_CALLFN) -- issue the request op 0x02..0x07 issues, and nothing more. Most of a
// scene's script lives in fn slots 3..15, which no headless driver can reach: `--talk`/CT_ACTIVATE
// fire slot 1 and CT_TOUCHAT fires slot 2, and everything else is only ever entered by ANOTHER
// object's call op. That made 40 real opcode sites structurally ungradable -- e.g. every op 0xA9
// whose operand is not 0 (25 of them in scene 21 alone) lives in a slot-5/6 function. This is not a
// new mechanism: it hands reqOp exactly the (target, function, priority) triple that step()'s
// 0x02-0x07 arm hands it, so the request is accepted or deferred by the ROM's own rules.
void CtSceneRequestFn(CtScene* sc, int obj, int slot, int prio) {
  if (!sc || obj < 0 || obj >= sc->nobj) return;
  reqOp(sc, obj, slot & 0x0F, prio & 0x0F);
}

void CtSceneTouch(CtScene* sc, int obj) {
  if (obj < 0 || obj >= sc->nobj) return;
  CtSS* st = &sc->st[obj];
  // THE SAME PROLOGUE AS THE ACTIVATE, and that is not a convenience: the two ROM routines are the
  // same seven instructions with ONE immediate different. Raw bytes --
  //   0x3154  [24 ROM bytes at 0x3154: python tools/dis.py 0x3154 24]
  //   0x5AC5  [24 ROM bytes at 0x5AC5: python tools/dis.py 0x5AC5 24]
  // -- LDX $EB, the $1C01/$1100/$1000 refusals, then `CMP #03` for the touch and `CMP #02` for the
  // activate, both branching to the routine's own shared bail. So the port carries ONE predicate and
  // passes the immediate, exactly as the ROM does.
  if (!CtVmInvokeGate(CtSceneMem7E(sc), obj, st->prio, 3)) return;   // C0/3154, CMP #03
  if (st->addr >= 0) st->ret[st->prio] = ssPushAddr(st);   // push the running fn ($1180) to its return slot
  int fn = fpPtr(sc, obj, 2);
  if (fn < 0 || fn >= sc->blen) return;

  st->addr = fn; st->prio = 2; st->tb = 0;              // ROM C0/319F: fn2 at PRIORITY 2
  ctSetDc(sc, obj, 0);                                  // 31AA STZ $1001,X (beside the 1A80/1A01 zeroes below)
  vmTicketStop(sc, obj);                               // the touch STOPS the actor's in-flight move (no lingering walk anim)
}

int CtSceneTextbox(const CtScene* sc, int* tableAddr, int* strIndex) {
  if (tableAddr) *tableAddr = sc->tbTableAtShow;     // the table as of when the box opened
  if (strIndex) *strIndex = sc->tbIndex;
  return sc->textbox;
}

// ---- THE TEXTBOX UNPARK IS THE BOX MACHINE'S OWN $0129, NOT A COUNT -------------------------------
// The event-VM's textbox family is SIX two-visit blocking ops -- 0xBB/0xC1/0xC2 (plain) and
// 0xC0/0xC3/0xC4 (decision) -- and every one of them parks on the same byte. Op 0xC2, C0/35FC:
//   3606 LDA $29 / BNE ->3640        ; $0129 != 0  -> TYX / CLC / RTS  = the IP does NOT advance
//   3612 LDA #01 / STA $7F0A00,X ... LDA #01 / STA $29 ... CLC / RTS   ; first visit: OPEN, park
//   3635 LDA #00 / STA $7F0A00,X / TYX / INX / INX / SEC / RTS         ; a later visit: ADVANCE
// A ROM-wide scan for readers of $0129 finds exactly those six handlers (C0/357A, 35BF, 3606, 364D,
// 36B4, 36E7) plus C0/1DF4, the A-button treasure probe -- so this gate belongs to the textbox family
// and nowhere else.
//
// THE LAW IS THE BYTE, NOT A NUMBER OF FRAMES. The script resumes on the first SCHEDULER VISIT at which
// $0129 == 0, and the box machine C0/1F87 clears it at the end of its CLOSE ramp C0/2032:
//   2032 LDA $27 / BEQ ->finish | DEC $27 / BEQ | DEC $27 / BEQ | DEC $27 / BEQ | DEC $27 | RTL
// -- FOUR decrements but only FOUR tests: the last DEC is untested, so with $0127 = 0x28 the counter
// reaches 0 on frame 10 and C0/204F STZ $29 only fires on frame 11. Press + 11 for the byte; the
// resume is press + 11..15 depending on where the press lands in the object's own visit lattice.
// MEASURED, sweeping the press phase on scene 254 (obj13 visits every 4 frames) and on intro_box.state
// (a 5-frame lattice): $0129 clears at press+11 in 14/14 runs, and the resume lands at +11/+12/+13/+14
// and +15. A FIXED 12 -- which is what this port shipped for one day -- is that one fixture's phase.
//
// So the port keeps the ROM's own bytes: the op sets $0129 = 1 when it opens the box, the host's
// dismiss sets $0129 = 0x0D (C0/2081) and re-arms $0127 = 0x28 (the immediate at C0/1FF8), the ramp
// below drains it in CtSceneTick, and the op advances on its next visit with $0129 == 0. The 1..7
// states in between (typing the page, waiting for input) are the FIELD layer's -- nothing in the VM
// reads them, only zero-vs-nonzero.
void CtSceneAdvance(CtScene* sc) {
  if (!sc || !sc->textbox) return;
  w8(sc->mem, 0x7E0129, 0x0D);                   // C0/2081 LDA #0D / STA $29 -- the CLOSE state
  w8(sc->mem, 0x7E0127, 0x28);                   // C0/1FF8's own full-open value, so the ramp is whole
  sc->tbCloseArmed = 1;                          // ...and C0/2081 RETURNS: the 0x0D arm (C0/2032) is only
                                                 //   dispatched on the box machine's NEXT frame
}
// C0/2126's end: the typewriter has drawn the page's last character (the host owns that string), so the
// machine leaves state 6 -- 21D8 `LDA #07 / STA $29 / LDA #81 / STA $36` and 21C2 `LDX #FFFF / STX $34`.
// (The $0216 arm at 21A8 -- an in-string pause code, `$34 := $0216 * 15` -- is not carried by the host's
// text model; every string measured so far has $0216 = 0.)
void CtSceneTextboxRevealDone(CtScene* sc, int pendingCode) {
  if (!sc || r8(sc->mem, 0x7E0129) != 6) return;
  w8(sc->mem, 0x7E0129, 7);
  // $0134 := FFFF (wait for A) on 21C2 / 21D3 only -- the end of the string (0), a waiting page (0B/0C), a waiting
  // line (07/08). A self-turning page (09/0A, 2181) and a full window met on a line code (05/06, 21CB) reach 21D8
  // directly and leave $0134 as it is (0 after the last press), so state 7 advances by itself (tbMachineTick).
  int self = (pendingCode == 0x05 || pendingCode == 0x06 || pendingCode == 0x09 || pendingCode == 0x0A);
  w8(sc->mem, 0x7E0136, 0x81);                   // 21DC LDA #81 / STA $36: the NMI uploads the last line once more
  if (!self) { w8(sc->mem, 0x7E0134, 0xFF); w8(sc->mem, 0x7E0135, 0xFF); }
  // $0215 = the control code the text engine stopped ON. The whole port of C0/2126 is not here, but
  // state 7 reads exactly one thing from it (C0/2024 `LDA $0215 / BNE ->2056`): a NON-ZERO code means
  // the string is not finished -- there is another page -- so the cursor must NOT arm and the press
  // turns the page like any other box. Zero means the string is exhausted and this is the option
  // page. The host knows which it is, so it passes the code: 0x0B/0x05 = a page/line break pending.
  w8(sc->mem, 0x7E0215, (uint8_t)pendingCode);
}
// The typewriter met an in-string PAUSE (`03 n`, C2/5945: $0215 = 03, $0216 = n): C0/2176 -> 21A8.
//   n != 0: `STA $4202 / LDA #0F / STA $4203` -> $0134 = n * 15, `LDA #07 / STA $29`, then 21A3 `LDA #01 / STA $36`
//   n == 0: 21AB `BEQ ->21D8` -- `$29 = 7 / $36 = $81`, $0134 left as it stands (0 after a pause ran out: the box closes
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
// State 7's PAGE advance, C0/2086 `LDA #08 / STA $29`: the machine re-decodes (8), counts (9-B) and resumes
// the typewriter (C -> 6).
void CtSceneTextboxPage(CtScene* sc) {
  if (!sc || r8(sc->mem, 0x7E0129) != 7) return;
  w8(sc->mem, 0x7E0129, 8);
  sc->tbPageArmed = 1;                           // ...and 2086 RETURNS: 1FB4 is the machine's NEXT frame (as 2081's 0x0D)
}
// The host's view of the same split: the box is DISMISSED (draw it ramping out) while the VM is still
// parked. Without this the field layer would keep the box fully open for the whole ramp and then snap
// it away -- the ROM ramps the window out from the press frame and unparks the script at its end.
//
// THE WINDOW BELONGS TO THE BOX MACHINE, NOT TO THE OP'S PARK LATCH. C0/1FCD (state 5) is the only
// place a window is ever created -- `LDA $26 / BNE ->1FF8` takes the OPEN ramp when one already
// exists, and otherwise computes its position into $0126 (1 = top, 2 = bottom; 0 = there is no
// window), zeroes the height $0127 and calls C0/20F2 -- and the close ramp's tail kills both bytes in
// the same breath:
//   204D STZ $29 / 204F STZ $26 / 2051 LDA #20 / 2053 TRB $54 / 2055 RTL
// The op's LATER visit (C0/3635, verbatim: `LDA #00 / STA $7F0A00,X / TYX / INX / INX / SEC / RTS`)
// only clears the per-object latch and advances the IP -- there is no `STA $29` on that arm, so the
// window can never come back for the box that was just dismissed. MEASURED on saves/intro_box.state
// (`--monitor "7E0126:1,7E0127:1,7E0129:1,7E0154:1" --btnscript "0:40,100:4,0:60" --diff --writer`):
// the press at f41 sets $0129 = 0D (C0:2085), $0127 ramps 28 -> 00 over f42..f51 (C0:2044), and at
// f52 $0126 AND $0129 both go to 00 (C0:2051 / C0:204F) and never change again.
//
// So the host's "a box is on screen" is the box machine's own byte, and BOTH post-dismiss states --
// 0x0D (ramping out) and 0 (gone) -- are "not an open box". Testing only 0x0D left the 0..4 frames
// between the ramp draining ($0129 = 0, press + 11) and the op's next scheduler visit (press + 11..15)
// reading as an OPEN box again, so the field layer re-opened the window it had just closed and then
// ran a second close ramp -- the box flashing back for a few frames after every single dismissal.
int CtSceneTextboxUnparking(const CtScene* sc) {
  if (!sc || !sc->textbox) return 0;
  uint8_t st = (uint8_t)r8(sc->mem, 0x7E0129);
  return st == 0x0D || st == 0;                  // C0/2081's close state, or C0/204D's cleared one
}

int CtSceneTextboxChoice(const CtScene* sc) { return sc->textbox ? sc->tbChoice : 0; }
int CtSceneTextboxResumeArmed(const CtScene* sc) { return sc ? gTbResumeArmed : 0; }   // see tbMachineTick, 2096
// Decision-box option-line range (op 0xC0/0xC3/0xC4 operand2): returns 1 + fills first/last option line
// (within the OPTION page) so the renderer can place the cursor on the real options (CT indents both the
// prompt's continuation lines and the option lines, so leading-space can't tell them apart). 0 = no choice.
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
// The mode-0 LOAD/NEW-GAME shell request (op 0xC8, odd operand < $80 -> C0/0D00). -1 = not up.
int CtSceneLoader(const CtScene* sc) { return sc->loader ? sc->loaderArg : -1; }
// The mode-2 SHOP request (op 0xC8 operand $80-$BF -> C0/0D09 -> C2/8000 -> screen $0C). -1 = none; else the raw
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
// The bank-$FD GATE WARP request (op 0xFF, operand $80-$8F -> C0/3FE8/3FF0/3FF8/3FFC). -1 = none.
int CtSceneReboot(const CtScene* sc) { return sc->rebootReq == 1 ? 1 : 0; }
// op FF 96 (C0/4181) is spinning toward the RESET entry FD/C000 (rebootReq 3); CtSceneRebootDone takes it too.
int CtSceneColdReset(const CtScene* sc) { return sc->rebootReq == 3 ? 1 : 0; }
void CtSceneRebootDone(CtScene* sc) { sc->rebootReq = 2; }
int CtSceneGateWarp(const CtScene* sc) { return sc->warpReq ? sc->warpReq : -1; }
void CtSceneGateWarpDone(CtScene* sc) { sc->warpReq = 0; sc->ffFade = 0; }
// 1 while a parked op has LEFT THE FIELD MAIN LOOP C0/0097..00BD -- the loop whose C0/00A1 `JSR C0/18D9` is the only
// caller of the X-button poll in bank $C0. op 0xC8 (C0/0CEA `JSR C0/0B4E`, C0/0D18 `JSL $C2:8000`: naming, the load
// shell, every mode) runs the menu program's own loop until it returns; op 0xFF < $90 (C0/3FD3 `JSR C0/0B4E / LDX
// #06FF / TXS`, then `JML $C3:0000` or the $FD warp) and op 0xDF/0xE1 (spin on $4210, reboot) never come back.
int CtSceneOffMainLoop(const CtScene* sc) {
  return sc->naming || sc->loader || sc->shop || sc->seqHas || sc->warpReq || sc->rebootReq;
}
// THE MAIN LOOP IS PARKED, asked BEFORE this frame's tick (the host's legs run ahead of it). A script's sound op whose
// `JSL C7/0004` crosses N frames holds the CPU inside the field main loop's VM visit: until it returns, no leg of the loop
// runs -- not the player controller C0/8833 (it runs inside the LEAD's visit, already spent before the JSL), not the A
// handler, not the box machine. That is every frame the tick will lose (sndWait > 1, or the overrun's window) AND the
// return frame (sndWait == 1: the iteration resumes after the VM, at C0/A810). MEASURED, the rule's cartridge, Guardia
// Forest's Power Tab (saves/field_re/s19_power_tab.state, Down held from before the box closes): obj41's `E3 01 / EA 08`
// gives control back and posts the song on f227, and the lead does not move until f263 -- then 1 px a frame.
int  CtSceneMainLoopParked(const CtScene* sc) { return sc ? (sc->sndPostResumeFreeze || sc->sndWait > 0) : 0; }
int  CtSceneFrameLegsOn(void) { return legsOn() && gRomImg != NULL; }   // the drain (C0/CA76) writes $0F01,X every tick
// Field calls this after the player confirms a name (having already CtSetName'd it): the blocked
// 0xC8 op then resumes the script on the next tick (mirrors CtSceneAdvance for textboxes).
void CtSceneNameDone(CtScene* sc) { sc->naming = 0; }

// Active-party char-id at slot 0/1/2 (ROM $7E:2980/81/82). $7E2980 maps to mem.s[0x2980].
// With no scene (sc==NULL) report the new-game default party the field engine seeds: Crono in
// slot 0, slots 1/2 empty (0x80) -- so dialogue name resolution works in scene-free contexts.
int CtSceneParty(const CtScene* sc, int slot) {
  if (slot < 0 || slot > 2) return 0x80;                   // out of range = empty
  if (!sc) return slot == 0 ? 0x00 : 0x80;                 // new-game default (Crono only)
  return sc->mem->s[0x2980 + slot];
}

// The character-NAME variables (the field engine's copy of the RAM name buffers the ROM keeps
// at $7E:2C23 -- 6-byte records, 5 chars + $00, indexed by char-id; record 7 = Epoch at
// $7E:2C4D). These are the new-game defaults, oracle-verified from WRAM against the ROM's text
// decoder (token handler C2/5B15, char-id->buffer pointer table C2/5FD8). Stored as ASCII (the
// names are all plain letters) -- equivalent to the CT-charset bytes the ROM holds, and the
// field font's cGlyph renders ASCII letters identically. char-id order: Crono Marle Lucca Robo
// Frog Ayla Magus (0-6), Epoch (7).
//   0 Crono = A2 CB C8 C7 C8   1 Marle = AC BA CB C5 BE   2 Lucca = AB CE BC BC BA
//   3 Robo  = B1 C8 BB C8      4 Frog  = A5 CB C8 C0      5 Ayla  = A0 D2 C5 BA
//   6 Magus = AC BA C0 CE CC   7 Epoch = A4 C9 C8 BC C1
// MUTABLE so the name-entry screen (op 0xC8) can rewrite a name -- the game's $7E:2C23 buffers
// are RAM; the player renames via the naming screen and dialogue then tracks it. Max 5 chars.
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
        // one digit -- the plain decimal of the value. ($02A1 selects C2/5CC0's hex arm instead; the field
        // leaves it 0 -- oracle, scene 5 tent keeper.) Without a scene there is no RAM to read: keep the tag.
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
        // `LDA $7F0200 / AND #00FF` -- the item id is the script's local 0 (the item ops' `4F id 00` puts it there) --
        // times 11 (`$4202/$4203`) + `$0B5E` + 1 in bank $CC: an item record is its icon byte, then the name. C2/5D23
        // takes the name's length: up to 10 bytes, ending at the first 0xEF (the record's padding). The bytes are the
        // CT charset the renderer draws (cGlyph takes 0xA0-0xF3 raw); 0xFF, the space INSIDE a name ("Power Tab"), is
        // the font's blank glyph of advance 4 (ROM 0x3F2060/0x3F3860 all zero, width 0x0260E6 + 0x5F = 4) -- the
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
  // CT_LOCTRACE=1: one line per COMMITTED location change. The only other evidence a chain of
  // scenes leaves is the host's `reboot:` line, which fires when a SEQUENCE ends -- not on a
  // field-to-field hop -- so reading a chain off that log silently drops every leg in between. That
  // is exactly how the attract's `01C -> 01B -> 0AD -> 112` was misread as `01C -> 112`. This is the
  // like-for-like counterpart of the oracle's `--monitor 7E0100:2 --diff`.
  { static int t = -1; if (t < 0) t = getenv("CT_LOCTRACE") ? 1 : 0;
    // stdout, not stderr: the host's `reboot:` / `sequence:` / `ow-op05:` lines are printf, and when
    // a run is piped the two streams buffer differently -- every stderr line lands FIRST regardless
    // of when it happened, so a chain read off the combined output comes out in the wrong order.
    // That is the same class of misreading this trace exists to prevent, so it shares their stream.
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
// The host says whether this frame is a C0/2848 ramp frame (the location load's fade-in) or a real
// C0/0097 main-loop frame. Only the FIELD host knows -- the ramp is a synchronous loop inside the
// loader, not a state any WRAM byte carries -- and every other VM host leaves it 0, i.e. main loop.
void CtSceneSetLoadFadeIn(CtScene* sc, int on) { if (sc) sc->loadFadeIn = on ? 1 : 0; }
int CtSceneBright(const CtScene* sc) { return sc->bright; }
// sc->bright IS $0119's low nibble; the host's C0/2848 ramp steps the WRAM byte, so it re-reads it here.
void CtSceneSyncBright(CtScene* sc) { if (sc) sc->bright = r8(sc->mem, 0x7E0119) & 0x0F; }
void CtSceneHeadLatch(CtScene* sc) { if (sc) { sc->head117 = (uint8_t)r8(sc->mem, 0x7E0117); sc->head117Set = 1; } }
int CtSceneActorMoveAnim(const CtScene* sc, int i) {
  if (i < 0 || i >= sc->nobj) return 0;
  return sc->actors[i].moveAnim;
}
int CtSceneActorPrio(const CtScene* sc, int i) {
  // op 0x8E byte (ROM C0/4C74): bit 7 = AUTO (position-based, from the foot tile props);
  // else FIXED OAM priorities: bits 4-5 = upper half, bits 0-1 = lower half (0-1 = behind
  // all of BG1 -> invisible in rooms, 2 = under prio-1 BG tiles, 3 = over them).
  if (i < 0 || i >= sc->nobj) return 0x80;
  // THE BYTE, not the C mirror. $0F80,X is filled with #$80 by the location load (CtSceneCreateIn),
  // stored by op 0x8E (C0/4C7C, vmOp8E) and carried by a WRAM seed. The old `sprPrio ? sprPrio : 0x80`
  // could not tell "never set" from a deliberate `8E 00` -- FIXED prio 0 on both halves, the ROM's
  // way of parking a loaded object BEHIND the BG (scene 8's Telepod vortex pair obj27/obj28 sit at
  // $0F80 = 00 in every oracle state; Manoria's flames at 04) -- and promoted it to AUTO, so the
  // vortex was drawn the moment the invented colorGated draw term went away.
  return r8((Mem*)sc->mem, 0x7E0F80 + (uint32_t)i * 2) & 0xFF;
}
int CtSceneActorScripted(const CtScene* sc, int i) {
  if (i < 0 || i >= sc->nobj) return 0;
  return sc->st[i].addr >= 0 && sc->st[i].prio < 7;  // a requested call (activate/touch/04...)
}
int CtSceneActorAtB0(const CtScene* sc, int i) { return (i >= 0 && i < sc->nobj && i < 64) ? sc->atB0[i] : 0; }
int CtSceneActorMode(const CtScene* sc, int i) { return (i >= 0 && i < sc->nobj) ? sc->mode[i] : -1; }  // $1100 probe

// ---- DEBUG save/load of the FULL live scene state (Belthasar F5/F9). A whole-struct snapshot of the
// VM (WRAM + scheduler + movers + flags). The only pointers in CtScene are raw/body (the static loaded
// scene data) and the tileSolid map-hook + ctx -- these are PRESERVED on load (the same scene/asset is
// already loaded), so only the dynamic state is restored. Same-scene only (nobj must match). Debug-only;
// not part of the canonical engine. ----
#define CT_STATE_MAGIC 0x53465443u   /* 'CTFS' LE */
#define CT_STATE_VERSION 4u          /* v4 = the $0F80 row (the op-0x8E sprite-priority byte) is now RAM
                                        the renderer reads (CtSceneActorPrio), filled #$80 at scene create
                                        and stored by op 0x8E; a v3 state was written by an engine that
                                        never wrote that row, so its bytes are zeros or leftovers -- read
                                        as FIXED prio 0 they would turn every object into an invisible
                                        occluder. v3 still LOADS: the row is rebuilt from the C mirror the
                                        old engine kept (`sprPrio`, 0 = never set = AUTO #$80).
                                        v2 = version field + scrubbed pointers.
                                        v3 = the WRAM IMAGE travels with the state again. When $7E moved
                                        to host ownership (b970585) `Mem mem` inside CtScene became
                                        `Mem* mem`, and TWO things broke silently: the pointer was
                                        neither scrubbed on save (a heap address on disk, the exact thing
                                        v2 was for) nor preserved on load (memcpy'd a stale pointer over
                                        the live one), and the 128 KB it used to carry stopped being
                                        saved at all -- so a reload restored the VM onto whatever memory
                                        happened to be there. The image is written explicitly now. */
int CtSceneSaveState(const CtScene* sc, const char* path, const double player[4]) {
  FILE* f = fopen(path, "wb");
  if (!f) return 0;
  uint32_t magic = CT_STATE_MAGIC, ver = CT_STATE_VERSION, sz = (uint32_t)sizeof(CtScene);
  CtScene* cp = (CtScene*)malloc(sizeof(CtScene));   // serialize a COPY with the live heap pointers
  if (!cp) { fclose(f); return 0; }                  // scrubbed (load overwrites them anyway) -- no
  memcpy(cp, sc, sizeof(CtScene));                   // heap addresses on disk (falsely diff-y states)
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
  // The image is read into a temp FIRST: a short/corrupt tail must not leave the live WRAM half-written.
  Mem* img = (got && havePlayer) ? (Mem*)malloc(sizeof(Mem)) : NULL;
  int haveImg = img && fread(img, sizeof(Mem), 1, f) == 1;
  fclose(f);
  if (!got || tmp->nobj != sc->nobj || !haveImg) { free(tmp); free(img); return 0; }
  uint8_t* raw = sc->raw; const uint8_t* body = sc->body;           // preserve the live pointers (static
  int (*ts)(void*, int, int) = sc->tileSolid; void* tc = sc->tileCtx;  // scene data + the field map-hook)
  Mem* mem = sc->mem; int own = sc->ownMem;                        // ...and the WRAM the HOST owns
  memcpy(sc, tmp, sizeof(CtScene));
  sc->raw = raw; sc->body = body; sc->tileSolid = ts; sc->tileCtx = tc;
  sc->mem = mem; sc->ownMem = own;
  // The image goes back INTO the live buffer rather than repointing at the temp, so a host-owned $7E
  // stays the SAME OBJECT across a reload -- the overworld's `w->wram` and the audio service both hold
  // that pointer, and swapping it under them would leave them writing a buffer nobody reads.
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
  // The ROM's completion condition is $7F0B01,X == 1 (C0/2EA2), driven 2 -> 1 by the frame legs
  // CtSceneTick runs (CtAnimTimerTick's CA4F arm, CtFieldFrameLookup's CBA4 arm) whenever the legs
  // are on and a ROM image is there for them to read the $E4 tables from. Then this renderer signal
  // decides NOTHING, and must not: writing the byte here would put the port's own playback clock
  // over the ROM's, which is the defect the delegation retired (see step()'s anim arm).
  if (legsOn() && gRomImg) return;
  // A host with no drain -- CT_FRAMELEGS=0, or a dat-only run without the ROM -- has nothing else
  // driving the byte, so the renderer's end-of-playback stands in for the ticker's 2 -> 1.
  uint8_t* w = (uint8_t*)sc->mem; int x = i * 2;
  if (w[0x1780 + x] == 2 && w[0x10B01 + x] >= 2) w[0x10B01 + x] = 1;
  sc->actors[i].animBusy = 0;
  sc->actors[i].animSeedFrame = -1;
}
int CtSceneActorAnim(const CtScene* sc, int i, int* mode, int* index, int* il, int* loops, int* staticFrame) {
  if (i < 0 || i >= sc->nobj) { *mode = 0; *index = 0; *il = 0; *loops = 0; *staticFrame = 0; return 0; }
  const CtVmActor* a = &sc->actors[i];
  *mode = a->animMode; *index = a->animIndex; *il = a->animIl; *loops = a->animLoops;
  // mode 2 surfaces the $1681,X RESUME STEP through this out-param; mode 3 surfaces the $1301,X held
  // frame. They are DIFFERENT ROM bytes and the mode-2 arm never reads $1301 (C0/2EE0-2EE3 stores the
  // anim id then `STZ $1681,X`), so mode 2 must NOT fall back to animStatic -- it falls back to 0,
  // the cursor the arm writes.
  //
  // It used to read `(mode == 2 && animSeedFrame >= 0) ? animSeedFrame : animStatic`, i.e. a mode-2
  // anime with no seeded cursor started at the HELD POSE FRAME. Scene 2's cat is exactly that script:
  // `AC 03` sets animStatic = 3, then fn3's `AB 03` armed the anime at step 3 -- past the end of a
  // 3-entry anim -- so it completed almost immediately. Measured on the user's capture
  // (cat_anim.state spliced to a loc-2 base): the ROM's AB 03 runs f7 -> f27 (step 01 frame 0x14 at
  // f7, step 02 frame 0x12 at f15, $7F0B01 2->1 at f25 = the 8/8/10 delays of sprite-66 anim 3),
  // 20 frames; ours ran f7 -> f11, FOUR. That is the reported "the cat's animation is too fast
  // or incomplete", and it is live in normal play, not just in seeded runs -- any object whose script
  // does an `AC n` before an `AB m` was starting the anime at step n.
  *staticFrame = (a->animMode == 2) ? (a->animSeedFrame >= 0 ? a->animSeedFrame : 0) : a->animStatic;
  return a->animGen;
}

// The ROM's move-anim selector (C0/5614 -> C0/5654-569C) re-writes the lead PC's $1680 anim mode EVERY frame
// while the player has control (idle=0, walk=1, run=6). So a SHAPE a script set on the lead (op 0xAC, $1680=$FF)
// does NOT survive once control returns -- the controller stamps it back to idle/walk. Our free-roam render
// computes the lead's anim locally but never wrote it back to the VM actor, so a stale held SHAPE (the Marle-bump
// obj1 fn3 knockdown, held through its 720-frame AD-F0 wait) lingered in animMode and RESURFACED the instant a
// new cutscene re-locked control (the pendant return) before fn3 ended -- Crono drawn fallen while handing back
// the pendant. field.c calls this each free-roam frame so the lead tracks the controller, not the stale script.
void CtSceneSetActorAnim(CtScene* sc, int i, int mode, int index) {
  if (i < 0 || i >= sc->nobj) return;
  CtVmActor* a = &sc->actors[i];
  if (a->animMode == mode && a->animIndex == index) return;
  a->animMode = mode; a->animIndex = index; a->animGen++;
}

// Pick option N (0-based among the option lines) in a decision box: store it as the box-owner's
// `result` (which op 0x1A branches on) and dismiss the box so the owner's script continues down the
// chosen path. ROM C0/36CC: result ($7F0A80,X) = the cursor's LINE ($66, ranging over $64..$65), NOT
// a 0-based index. $64 = first option line = (operand2>>2)&3. So result = optFirst + sel -- otherwise
// the CASE checks (op 0x1A `result==2/==3`, keyed on the line) never match the refuse option and the
// script always falls through to the accept path (Marle joined / pendant returned no matter the pick).
// ⭐ C0/1ADF -- THE DECISION BOX'S CURSOR ARM. While $0162 != 0 it owns the WHOLE input frame:
// C0/1927 `LDA $62 / BEQ ->192E / JSR C0/1ADF / RTS`, and the ordinary field input path bails on the
// same byte one instruction later (C0/1934 `LDA $62 / BEQ ->1939 / RTS`).
//   1ADF CMP #01 / BEQ ->1B0C            ; requested but not armed: the press is only EATEN
//   1AE3 CMP #02 / BNE ->1AFE
//   1AE7 LDA $00F7 / BIT #04 / BNE ->1B19      ; d-pad DOWN  -> cursor down
//   1AEE            BIT #08 / BNE ->1B29      ; d-pad UP    -> cursor up
//   1AF2 LDA $00F8 / BIT #80 / BEQ ->1B18      ; no A -> nothing
//   1AF9 LDA #03 / STA $62 / RTS               ; A -> CONFIRM
//   1AFE LDA $63 / BMI ->1B0C / CMP #04 / BEQ ->1B0C / STA $66 / LDA #04 / STA $63
//                                              ; $0166 = the confirmed LINE, $0163 retired to 4
//   1B0C LDA $00F6 / BIT #80 / BEQ ->1B18 / LDX #0000 / STX $34 / RTS   ; eat the press
//   1B19 LDA $63 / INC A / CMP $65 / BEQ ->1B22 / BCS ->1B25 | 1B22 STA $63 | 1B25 LDA $64
//   1B29 LDA $63 / BEQ ->1B32 / DEC A / CMP $64 / BCS ->1B22 | 1B32 LDA $65
// The cursor wraps between $0164 (first option line) and $0165 (last), both written by the decision
// op's request from operand2. $0166 is what the completion arm stores as the script's `result`.
// The host calls this every frame from its input block, in C0/1927's slot.
void CtSceneDecisionInput(CtScene* sc, int aPressed, int upPressed, int downPressed) {
  if (!sc) return;
  Mem* m = sc->mem;
  int d = r8(m, 0x7E0162);
  if (d == 0) return;                                        // 1929 BEQ ->192E: not a decision frame
  if (d == 2) {                                              // cursor LIVE
    int c = r8(m, 0x7E0163), first = r8(m, 0x7E0164), last = r8(m, 0x7E0165);
    if (downPressed) { w8(m, 0x7E0163, (uint8_t)((c + 1 > last) ? first : c + 1)); return; }   // 1B19
    if (upPressed)   { w8(m, 0x7E0163, (uint8_t)((c == 0 || c - 1 < first) ? last : c - 1)); return; }  // 1B29
    if (aPressed)    { w8(m, 0x7E0162, 3); return; }         // 1AF9-1AFB: CONFIRM
    return;                                                  // 1B18 RTS
  }
  if (d != 1) {                                              // 1AFE: $0162 == 3, retire the cursor
    int c = r8(m, 0x7E0163);
    if (!(c & 0x80) && c != 4) { w8(m, 0x7E0166, (uint8_t)c); w8(m, 0x7E0163, 4); }
  }
  if (aPressed) { w8(m, 0x7E0134, 0); w8(m, 0x7E0135, 0); }  // 1B0C-1B16: the press is spent
}

void CtSceneChoose(CtScene* sc, int option) {
  int line = option + (sc->tbOpt >= 0 ? (sc->tbOpt >> 2) & 3 : 0);
  // The ROM's own bytes: park the cursor on that LINE ($0163) and raise the CONFIRM ($0162 = 3,
  // C0/1AFB). The cursor arm (CtSceneDecisionInput, the port of C0/1ADF) then does C0/1AFE --
  // $0166 = $0163, $0163 = 4 (retired) -- and the box machine's state 7 sees the 3 and closes.
  // Every caller keeps working: this is the same confirm the A press raises, just aimed first.
  if (r8(sc->mem, 0x7E0162) != 0) {
    w8(sc->mem, 0x7E0163, (uint8_t)line);
    w8(sc->mem, 0x7E0162, 3);
  }
  // THE RESULT IS WRITTEN ON THE COMPLETION VISIT, not here. C0/365D (and identically C0/36C6 /
  // C0/36FA) is `LDA #00 / STA $7F0A00,X / LDA $66 / STA $7F0A80,X / STZ $62 / ... SEC / RTS` -- the
  // same arm that advances the IP, eleven frames after the press. C0/1FDD reads $7F0A80,X while the
  // box is still alive, so writing it early is not obviously harmless.
  sc->tbPendChoice = line;
  CtSceneAdvance(sc);                                 // the decision ops park on the same $0129 gate
}

void CtSceneGetFlags(const CtScene* sc, uint8_t* out0x200) { memcpy(out0x200, sc->mem->g, 0x200); }
// The VM's system memory ($7E image, 64KB) + globals page ($7F0000, 0x200) -- the battle transfer
// consumer (field.c, ROM C0/1770-18D6) and the battle module (battle.c) operate on these directly,
// like the ROM routines they port. CtSceneSeedFromWram fills the $7E image wholesale from an oracle
// state, so per-object bytes our engine doesn't maintain live ($0F81, $1280/$1281...) carry the
// oracle's canonical values there.
// NULL-safe on purpose: callers legitimately hold a NULL scene (the overworld has no VM), and a
// silent NULL deref here crashes the whole engine a frame later somewhere unrelated.
// C0/9DC3 (BRL from C0/5926, the end of the load's object pass): its WRAM half. The host (field.c
// fieldArrivalTileMode) runs the tile half -- C0/9AC8 on the arrival tile $0102/$0103, $66 = 80 / $68 = FF, C0/9AD3 --
// and hands in its $6A/$6E/$6F. `onMap` = 0 when the fetch fell off the map (none of 9DEA-9E22 then).
//   9DC3-9DC9  STZ $01AB / $01AC / $01AD          9DCC-9DDA  $7F0C00 / 0C80 / 0D00 / 0D80 = 0
//   9DF8-9DFE  $0155 = $6A, or 1 when it is 0
//   9E01-9E0B  the lead ($0197): $0C00,X = $6E, $0C01,X = $6F
//   9E0E-9E1D  the other two ($0199, $019B): both halves = $6F
// MEASURED, the attract's 1B0 (the vortex, arrival (07,1B)): op 0x8E leaves the three PCs' pairs 30 20 / 30 30 / 30 30
// at c8324 (C0:4CC9) and C0:9E0E / 9E17 / 9E20 rewrite them to 20 20 at c8325 -- the cockpit's console (BG1 priority 1)
// then hides their legs. The port never ran it and drew Crono over the console (the user's list 2026-09-27 #4).
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
  // CT_SNDTRACE=1: liveness for the VM's six sound ops. It MUST print before the sink runs -- the
  // teardown below zeroes $1E00 unconditionally, so a probe placed after the call always reads 00
  // and would report silence whether or not a command was staged. Reports whether a sink is even
  // installed, because ct.exe installs none and the ops are then correctly inert. Inert unless set.
  { static int tr = -1; if (tr < 0) { const char* e = getenv("CT_SNDTRACE"); tr = (e && *e) ? 1 : 0; }
    if (tr) { const uint8_t* m = mem->s + 0x1E00; const uint8_t* L = mem->s + 0x0100;
              fprintf(stderr, "[snd] blen=%-4d rawloc=%-3d cmd=%02X p1=%02X p2=%02X p3=%02X sink=%d\n",
                      blen, L[0] | (L[1] << 8), m[0], m[1], m[2], m[3], gSoundSink.post ? 1 : 0); } }
  int rr = -1;
  if (gLoadPostOn && gSoundSink.post && mem->s[0x1E00]) {      // the load's last JSLs: queued for its last frame (above)
    if (gLoadPostN < 32) { gLoadPostCyc[gLoadPostN] = g_ctLzSpriteCycles; memcpy(gLoadPost[gLoadPostN++], mem->s + 0x1E00, 4); }
    w8(mem, 0x7E1E00, 0x00);
    return startLine;                                          // (nothing crossed here: the load's black prices it)
  }
  if (gSoundSink.post) rr = gSoundSink.post(gSoundSink.ctx, mem->s, startLine);
  // ...and the callee's TEARDOWN, which is unconditional. C7/0150 does `LDX #1E00 / PHX / PLD`, so
  // `$00` inside bank $C7 IS $7E1E00, and the routine has exactly ONE exit -- C7/0192 `SEP #20` /
  // C7/0194 `STZ $00` -- reached from every path INCLUDING the two early ones (C7/0157 `LDA $05 /
  // BPL ->0192` when the driver latch is clear, C7/015B `LDA $00 / BEQ ->0192` when nothing is
  // pending). So the command byte is ALWAYS zero after the JSL returns, sink or no sink.
  // We modelled the staging and not the teardown, so with no sink installed (ct.exe never installs
  // one -- only field.c does) the command sat in $1E00 forever. The write-mask comparator found it:
  //   [wmask] f130 $7E1E00 m=18 ours=18 oracle=00 seed=00
  // Through w8 rather than a raw store ON PURPOSE: a raw stage-and-clear XORs to zero within one
  // tick, so the shadow diff would credit no bits and the byte would drop OUT of the mask -- it
  // would stop being compared instead of comparing equal. w8 claims it unconditionally.
  w8(mem, 0x7E1E00, 0x00);                     // C7/0192-0194
  return rr;
}
int CtSceneLoadPostsFlush(CtScene* sc, int count) {    // the next `count` queued JSLs (gLoadPost; < 0 = all), in order, each with
  int n = gLoadPostN, k0 = gLoadPostDone;              //   its mailbox
  if (count >= 0 && k0 + count < n) n = k0 + count;
  gLoadPostDone = n;
  if (gLoadPostDone >= gLoadPostN) gLoadPostN = gLoadPostDone = 0;
  if (!sc) return -1;
  // ...AND PRICED, like every other JSL: each one from the line the one before it returned on (vmSoundPostAcc). A song an
  // init script plays is uploaded INSIDE the load, with the NMI off, and the cartridge's black lasts as long as the upload:
  // loc 071's obj init `EA 1B` (the gate's clearing, entered from 070 with Marle and Lucca) posts `10 1B` at f7378 and the
  // SPC takes it to f7408; the ramp begins at f7425, 63 frames after the commit. The flush threw the price away and the
  // port lit the clearing at f7394 -- its scene, its gate and the warp to 1000 A.D. all ~31 frames early.
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
  if (sc->ownMem) free(sc->mem);      // a host-owned image OUTLIVES the scene -- that is the point
  free(sc->raw); free(sc);
}
