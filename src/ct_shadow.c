// ct_shadow.c -- the routine-level shadow; see ct_shadow.h and docs/SHADOW_PROPOSAL.md.
#include <stdlib.h>
#include "romview.h"
#include <string.h>
#include "ct_shadow.h"
#include "cpu.h"
#include "cart.h"
#include "apu.h"
#include "dma.h"
#include "battle.h"
#include "move.h"
#include "collision.h"
#include "menu.h"
#include "oam.h"
#include "vwf.h"

#define SH_WRAM  0x20000
#define SH_DEPTH 16
#define SH_DETAILS 8

// ---- The registry ----------------------------------------------------------------------------------------------

// C1/AF22, the battle RNG (battle.c battleRngBase; --sweeprng). Entered `JSR C1/AF22` with A 8-bit = the count and
// X = the base (AF22 SEP #10 / STX $25: only X's low byte is read); D = 0, absolutes ($B31E, $2989) in bank $7E.
// AF73 REP #10 / PLX / STX $2C / RTS: X comes back 16-bit holding the $2C saved at AF28; A's low byte is the draw.
static int rngGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) { (void)e; (void)w; return in->dp == 0x0000 && in->db == 0x7E; }
static int rngRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                  CtShadowRegs* out) {
  (void)e; (void)romLen;
  int r = CtBattleRngBaseProbe(w, rom, (in->a & 0xFF) - 1, in->x & 0xFF);   // its `maxv` is the count less one
  out->a = (uint16_t)((in->a & 0xFF00) | (r & 0xFF));
  out->x = (uint16_t)(w[0x2C] | (w[0x2D] << 8));
  return CT_SH_RETURNS;
}

// The field-script opcode handlers (move.c g_ctVmOps, CtFieldRunnerOne; --sweepops). The runner C0/5A93 latches the
// object index (5A9C `STX $6D` -> $016D), then `5AAA AND #00FF / ASL A / TAX / JSR ($5D6E,X)`: D = $0100, X = the
// opcode doubled, Y = the IP of the opcode byte at $7F:2001+Y. The handler returns the new IP in X (committed at
// 5ABF) and the carry (1 = keep spending the visit, 0 = yield; 5AB4). The guard requires that X and the byte at the
// IP both be this handler's opcode: some handlers are subroutines of others with the same X and Y (op B6 at C0/354F
// runs op 95's body at C0/5410, op B5 op 94's at C0/5215), and those calls belong to the caller's grade.
// C3/0002's workspace ($0308-$0316) is not excluded here: the liveness watch finds it dead on the live calls.
static int vmGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  return in->dp == 0x0100 && in->x == e->arg * 2 && in->y < 0xDFFF && w[0x12001 + in->y] == e->arg;
}
static int vmRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                 CtShadowRegs* out) {
  (void)e;
  g_ctApuPort1 = in->apu[1]; g_ctApuPort3 = in->apu[3];   // ops ED and the C0/3CC6 poll read the SPC's mailbox
  CtHRet h;
  if (CtFieldRunnerOne(w, rom, (int)romLen, w[0x016D], in->y, &h) >= 0) return CT_SH_UNPORTED;
  out->x = (uint16_t)h.ip;
  out->p = (uint8_t)((in->p & ~1) | (h.carry & 1));
  return CT_SH_RETURNS;
}

// C0/ACFD, the velocity + priority builder (move.c CtBuildVelocityPriority; --sweepacfd). Entered with X = the object
// index and D = $0100; it reads the heading its caller staged at $01EE. Graded on WRAM alone (callers reload).
static int acfdGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; (void)w; return in->dp == 0x0100 && in->db == 0x00 && in->x < 0x80 && !(in->x & 1);
}
static int acfdRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                   CtShadowRegs* out) {
  (void)e; (void)romLen; (void)out;
  CtBuildVelocityPriority(w, rom, in->x);
  return CT_SH_RETURNS;
}

// C1/DC64, the physical to-hit roll (battle.c battleHitCore; --sweephitroll). In: hit $16, evade $18. Out: the
// verdict in $16, the roll in $18, zero in $AE4F. Its draw (JSR C1/AF22) is graded on its own as well.
static int battleGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; (void)w; return in->dp == 0x0000 && in->db == 0x7E;
}
static int hitRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                  CtShadowRegs* out) {
  (void)e; (void)romLen; (void)in; (void)out;
  int pct, roll; CtBattleHitCoreProbe(w, rom, &pct, &roll);
  return CT_SH_RETURNS;
}

// The enemy AI (battle.c aiWalk / aiCond / aiCommit; --sweepaieval). The walk C1/AFD2 is entered with A = the AI id
// (B = 0); each condition handler via `ASL A / TAX / JSR ($B80D,X)` (B006 / B021) with X = the op doubled and the
// cursor $B1D2 on its op byte; the commit C1/8C3E is a condition's JSR. The sweep stops inside the commit (C1/8C53),
// but the cartridge's calls return, so the shadow runs the *Whole entry points, commit included.
static int aiWalkRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                     CtShadowRegs* out) {
  (void)e; (void)romLen; (void)out;
  int unp = 0;
  return CtBattleAiWalkWhole(w, rom, in->a & 0xFF, &unp) < 0 ? CT_SH_UNPORTED : CT_SH_RETURNS;
}
static int aiCondGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)w; return in->dp == 0x0000 && in->db == 0x7E && in->x == e->arg * 2;
}
static int aiCondRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                     CtShadowRegs* out) {
  (void)romLen; (void)in; (void)out;
  int unp = 0;
  return CtBattleAiCondWhole(w, rom, e->arg, &unp) < 0 ? CT_SH_UNPORTED : CT_SH_RETURNS;
}
static int aiCommitRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                       CtShadowRegs* out) {
  (void)e; (void)romLen; (void)in; (void)out;
  int unp = 0;
  return CtBattleAiCommitSweep(w, rom, &unp) < 0 ? CT_SH_UNPORTED : CT_SH_RETURNS;
}

// C2/8791 and C2/87D5, inventory add and remove (menu.c CtMenuInvAdd / CtMenuInvRemove). Item in $04C9, count in
// $04CB; `PHX / PHP / SEP #30 ... PLP / PLX / RTS`, so any width enters and X is preserved. A's low byte is the
// verdict (0 done, $FF not held / bag full, $FE not enough). Absolutes ($2400,X / $2500,X) are in bank $7E.
static int invGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; (void)w; return in->db == 0x7E;
}
static int invRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                  CtShadowRegs* out) {
  (void)rom; (void)romLen;
  int r = e->arg ? CtMenuInvRemove(w, w[0x04C9], w[0x04CB]) : CtMenuInvAdd(w, w[0x04C9], w[0x04CB]);
  out->a = (uint16_t)((in->a & 0xFF00) | (r & 0xFF));
  return CT_SH_RETURNS;
}

// ---- The routines the --sweep* modes grade offline (src/main.c), each at its live entry ---------------------------

// The field's per-object routines. Each opens with `LDX $6D` (C0/9E84 `LDY $6D`): the object is the one latched in
// $016D (D = $0100), so the adapters read it there.
static int fieldGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; (void)w; return in->dp == 0x0100 && in->db == 0x00;
}
static int carryOut(const CtShadowRegs* in, CtShadowRegs* out, int c) {
  out->p = (uint8_t)((in->p & ~1) | (c ? 1 : 0)); return CT_SH_RETURNS;
}
// C0/ABA2, the heading toward ($01F2,$01F3) from ($01F0,$01F1) (CtMoveDirAngle; --sweepaba2, all WRAM).
static int angleRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                    CtShadowRegs* out) {
  (void)e; (void)romLen; (void)in; (void)out; CtMoveDirAngle(w, rom); return CT_SH_RETURNS;
}
// C0/AB45 the screen position (CtObjScreenPos; --sweepscreen), C0/A98A the draw list's unlink (CtObjListUnlink;
// --sweeplist), C0/C98A the animation timer (CtAnimTimerTick; --sweepc98a), C0/9E84 the player's facing and frame
// (CtPlayerAnimSelect; --sweepanim).
static int screenRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                     CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)in; (void)out; CtObjScreenPos(w, w[0x016D]); return CT_SH_RETURNS;
}
static int unlinkRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                     CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)in; (void)out; CtObjListUnlink(w, w[0x016D]); return CT_SH_RETURNS;
}
static int linkRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                   CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)in; (void)out; CtObjListLink(w, w[0x016D]); return CT_SH_RETURNS;
}
static int timerRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                    CtShadowRegs* out) {
  (void)e; (void)in; (void)out; CtAnimTimerTick(w, rom, (int)romLen, w[0x016D]); return CT_SH_RETURNS;
}
// C0/8A6D, the lead's tile collision (CtPlayerCollide), called from C0/8847 with D = $1D00. The feelers read the WRAM
// planes $7E:30C0 / $7E:7000 / $7E:7040 (ct.exe installs no host tile fetch).
static int collGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; (void)w; return in->dp == 0x1D00 && in->db == 0x00;
}
// C0/9C5C's positional exit (C0/9CA1 -> 9D2C) stages the destination $0112-$0116 and the latch $0117 bit 7 ($1D52 is
// its scratch). The port stages exits in the field host (field.c), not in CtCollisionCommit, so they are skipped.
static const CtShadowSkip s_collSkip[] = {
  { 0x0112, 0x0117, "C0/9CA1's positional exit staging: the field host's" },
  { 0x1D52, 0x1D52, "C0/9CA1's scratch" },
  { 0, 0, NULL } };
static int collRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                   CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)in; (void)out; CtPlayerCollide(w); return CT_SH_RETURNS;
}
static int pAnimRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                    CtShadowRegs* out) {
  (void)e; (void)in; (void)out; CtPlayerAnimSelect(w, rom, (int)romLen, w[0x016D]); return CT_SH_RETURNS;
}
// C0/B701 the animation release (CtAnimRelease: 1 SEC skip / 0 CLC draw; --sweepdrain), C0/CAD9 one drain visit
// (CtAnimDrainVisit: 1 SEC / 0 CLC / -1 unported; --sweepcad9), C0/305D the arrival snap (--sweepsnap): the carry.
static int releaseRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                      CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; return carryOut(in, out, CtAnimRelease(w, w[0x016D]));
}
static int drainRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                    CtShadowRegs* out) {
  (void)e; int r = CtAnimDrainVisit(w, rom, (int)romLen, w[0x016D]);
  return r < 0 ? CT_SH_UNPORTED : carryOut(in, out, r);
}
static int snapRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                   CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; return carryOut(in, out, ctSweepSnapCall(w, w[0x016D]));
}

// The battle's arithmetic and bookkeeping (D = 0, DB = $7E: battleGuard).
// C1/C90B the 16x16 multiply (CtBattleMul16Probe, returns the carry; X 16-bit) and C1/C92A the 32/16 divide
// (CtBattleDiv32Probe; `PHP ... PLP`, so the carry is preserved). --sweepmuldiv.
static int mulRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                  CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; return carryOut(in, out, CtBattleMul16Probe(w, in->p & 1));
}
static int divRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                  CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)out; CtBattleDiv32Probe(w, in->p & 1); return CT_SH_RETURNS;
}
// C1/C8F7 the seat redirect (CtBattleRedirectProbe: A = the seat in, the seat out; --sweepredirect).
static int redirectRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                       CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen;
  out->a = (uint16_t)((in->a & 0xFF00) | (CtBattleRedirectProbe(w, in->a & 0xFF) & 0xFF)); return CT_SH_RETURNS;
}
// C1/D174 the stat fetch (CtBattleStatFetchProbe; --sweepstatfetch, all WRAM).
static int statRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                   CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)in; (void)out; CtBattleStatFetchProbe(w); return CT_SH_RETURNS;
}
// C1/B279 a combatant's dead flag (CtBattleDeadFlagProbe; Y = the combatant; --sweepdeadflag): arm 4, the PC
// lifeline, runs on into host-side work the C does not port.
static int deadRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                   CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)out;
  return CtBattleDeadFlagProbe(w, in->y & 0xFF) == 4 ? CT_SH_UNPORTED : CT_SH_RETURNS;
}
// C1/F205 the victory's tech pass (CtBattleTechPass, its byte from $7F:01E0; --sweeptechpass).
static int techPassRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                       CtShadowRegs* out) {
  (void)e; (void)romLen; (void)in; (void)out; CtBattleTechPass(w, rom, w[0x101E0]); return CT_SH_RETURNS;
}

// CD/16A6, the battle effect program's per-op dispatch (battle.c CtBattleFxProgOpMem; --sweepfxops/fx80/fxmisc/fx3d):
// `STX $43 / ASL / TAX / LDY $40 / INY / STY $40 / JSR ($16B5,X) / LDX $43 / RTS`, with A = the op and $40-$42 its
// address in bank $D1. The adapter stores the returned $40 and $43/$44 = the slot, as the dispatch does. An op the C
// does not port (a STOP) is `unported`. Needs the battle ROM loaded (CtBattleLoadRom).
static int fxGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; long rl = 0; const uint8_t* rom = CtBattleRom(&rl);
  int at = w[0x40] | (w[0x41] << 8);
  return rom && in->dp == 0x0000 && in->db == 0x7E && w[0x42] == 0xD1 && CtRomByte(rom, rl, 0x110000L + at) == (in->a & 0xFF);
}
static int fxRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                 CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)out;
  int p = CtBattleFxProgOpMem(w, in->x, w[0x40] | (w[0x41] << 8));
  if (p < 0) return CT_SH_UNPORTED;
  w[0x40] = (uint8_t)p; w[0x41] = (uint8_t)(p >> 8);
  w[0x43] = (uint8_t)in->x; w[0x44] = (uint8_t)(in->x >> 8);
  return CT_SH_RETURNS;
}

// FF/FAA3, the save load (menu.c CtMenuSaveLoad; --loadcheck). Entered by JSL from C2/E657 with DB = $7E; FF/FBD3
// takes the slot from $79 & 3 (D-relative) and its base from FF/FBE1 into $7B. It builds an MVN stub at $9690 and a
// record table at $9890, then copies each record out of battery RAM ($30:6000 + base). The C copies the same records
// from the oracle's SRAM; the stub, table, $7B and $00 are scratch left to the liveness watch.
static Snes* s_snes;
static int loadGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; return in->db == 0x7E && (w[(in->dp + 0x79) & 0xFFFF] & 3) < 3;
}
static int loadRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                   CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)out;
  if (!s_snes || !s_snes->cart->ram) return CT_SH_UNPORTED;
  int slot = w[(in->dp + 0x79) & 0xFFFF] & 3;
  return CtMenuSaveLoad(s_snes->cart->ram, (long)s_snes->cart->ramSize, slot, w) ? CT_SH_RETURNS : CT_SH_UNPORTED;
}

// C1/2D9F, the idle walk's dispatch (battle.c battleIdleWalk via CtBattleIdleWalkProbe): `STZ $9912 / LDA $A4 / BEQ
// (PCs) / JMP C1/3234 (enemy groups)`, from C1/10A9 every other frame; a publish runs the gather C1/1C4A into the
// sprite mirror. Not ported: the PC group (still the host's).
static int idleWalkRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                       CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)in; (void)out;
  return CtBattleIdleWalkProbe(w) < 0 ? CT_SH_UNPORTED : CT_SH_RETURNS;
}

// C1/3714, the mode machine (battle.c battleModeMachine3714 via CtBattleModeMachineProbe): per enemy slot the cadence
// $9897, the requested target $5E15 -> $9752 and the class handler C1/3760[CC/FBAB[mode]]; from C1/10B5 and C1/10DC.
static int modeMachineRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                          CtShadowRegs* out) {
  (void)e; (void)rom; (void)romLen; (void)in; (void)out;
  return CtBattleModeMachineProbe(w) < 0 ? CT_SH_UNPORTED : CT_SH_RETURNS;
}

// C2/57DF and C2/5823, the proportional text (vwf.c CtVwfBegin / CtVwfRun), entered by JSL (via C2:0003 / C2:0009, or
// from C2/6965). Both `PHP / REP #20 / PHD` and set D = $0200, so any m enters; 57DF's `LDX #0200` needs x 16-bit.
// Absolutes ($0231, $0237-$023A) are direct-page bytes when DB maps low WRAM. A returns 0000; X and Y are not read
// by any caller.
static int vwfGuard(const CtShadowEntry* e, const uint8_t* w, const CtShadowRegs* in) {
  (void)e; (void)w; return in->db == 0x7E || !(in->db & 0x40);
}
static int vwfRun(const CtShadowEntry* e, uint8_t* w, const uint8_t* rom, long romLen, const CtShadowRegs* in,
                  CtShadowRegs* out) {
  (void)in;
  if (e->arg) { if (CtVwfRun(w, rom, romLen) < 0) return CT_SH_UNPORTED; }
  else CtVwfBegin(w, rom, romLen);
  out->a = 0;
  return CT_SH_RETURNS;
}
static const CtShadowEntry k_fixed[] = {
  { 0xC28791, "inventory add (CtMenuInvAdd)", 0x00, 0x00, 0, 0, invGuard, invRun, CT_SH_A8 | CT_SH_X16, NULL },
  { 0xC287D5, "inventory remove (CtMenuInvRemove)", 0x00, 0x00, 0, 1, invGuard, invRun, CT_SH_A8 | CT_SH_X16, NULL },
  { 0xC1AF22, "battle RNG (battleRngBase)", 0x20, 0x20, 0, 0, rngGuard, rngRun, CT_SH_A8 | CT_SH_X16, NULL },
  { 0xC0ACFD, "velocity+priority (ACFD)", 0x30, 0x20, 0, 0, acfdGuard, acfdRun, 0, NULL },
  { 0xC1DC64, "battle hit roll (battleHitCore)", 0x30, 0x20, 0, 0, battleGuard, hitRun, 0, NULL },
  { 0xC1AFD2, "battle AI walk (aiWalk)", 0x30, 0x20, 0, 0, battleGuard, aiWalkRun, 0, NULL },
  { 0xC18C3E, "battle AI commit (aiCommit)", 0x30, 0x20, 0, 0, battleGuard, aiCommitRun, 0, NULL },
  { 0xC0ABA2, "move heading (CtMoveDirAngle)", 0x30, 0x20, 0, 0, fieldGuard, angleRun, 0, NULL },
  { 0xC0AB45, "screen position (CtObjScreenPos)", 0x00, 0x00, 0, 0, fieldGuard, screenRun, 0, NULL },
  { 0xC0A98A, "draw-list unlink (CtObjListUnlink)", 0x00, 0x00, 0, 0, fieldGuard, unlinkRun, 0, NULL },
  { 0xC0A9CD, "draw-list link (CtObjListLink)", 0x00, 0x00, 0, 0, fieldGuard, linkRun, 0, NULL },
  { 0xC0C98A, "anim timer (CtAnimTimerTick)", 0x30, 0x30, 0, 0, fieldGuard, timerRun, 0, NULL },
  { 0xC09E84, "player anim (CtPlayerAnimSelect)", 0x30, 0x30, 0, 0, fieldGuard, pAnimRun, 0, NULL },
  { 0xC08A6D, "player collision (CtPlayerCollide)", 0x00, 0x00, 0, 0, collGuard, collRun, 0, s_collSkip },
  { 0xC0B701, "anim release (CtAnimRelease)", 0x30, 0x20, 0, 0, fieldGuard, releaseRun, CT_SH_C, NULL },
  { 0xC0CAD9, "anim drain visit (CtAnimDrainVisit)", 0x30, 0x30, 0, 0, fieldGuard, drainRun, CT_SH_C, NULL },
  { 0xC0305D, "arrival snap (ctArriveSnap305D)", 0x00, 0x00, 0, 0, fieldGuard, snapRun, CT_SH_C, NULL },
  { 0xC1C90B, "battle mul16 (battleMul16)", 0x10, 0x00, 0, 0, battleGuard, mulRun, CT_SH_C, NULL },
  { 0xC1C92A, "battle div32 (battleDiv32)", 0x00, 0x00, 0, 0, battleGuard, divRun, CT_SH_C, NULL },
  { 0xC1C8F7, "battle seat redirect", 0x20, 0x20, 0, 0, battleGuard, redirectRun, CT_SH_A8, NULL },
  { 0xC1D174, "battle stat fetch", 0x30, 0x20, 0, 0, battleGuard, statRun, 0, NULL },
  { 0xC1B279, "battle dead flag", 0x30, 0x20, 0, 0, battleGuard, deadRun, 0, NULL },
  { 0xC1F205, "battle tech pass", 0x30, 0x20, 0, 0, battleGuard, techPassRun, 0, NULL },
  { 0xCD16A6, "battle effect op (CD/16A6)", 0x30, 0x20, 0, 0, fxGuard, fxRun, CT_SH_X16, NULL },
  { 0xC12D9F, "battle idle walk (C1/2D9F)", 0x20, 0x20, 0, 0, battleGuard, idleWalkRun, 0, NULL },
  { 0xC13714, "battle mode machine (C1/3714)", 0x20, 0x20, 0, 0, battleGuard, modeMachineRun, 0, NULL },
  { 0xFFFAA3, "save load (CtMenuSaveLoad)", 0x00, 0x00, 1, 0, loadGuard, loadRun, 0, NULL },
  { 0xC257DF, "text set-up (CtVwfBegin)", 0x10, 0x00, 1, 0, vwfGuard, vwfRun, CT_SH_A16, NULL },
  { 0xC25823, "text run (CtVwfRun)", 0x10, 0x00, 1, 1, vwfGuard, vwfRun, CT_SH_A16, NULL },
};
#define AI_NCOND 0x29   // the condition ops 00-28 (--sweepaieval's range; op 0F, C1/2C02's path test, is unported)
#define SH_NFIXED ((int)(sizeof k_fixed / sizeof k_fixed[0]))
#define SH_MAX (SH_NFIXED + CT_VM_NOPS + AI_NCOND)

static CtShadowEntry s_entries[SH_MAX]; static int s_n;
static char s_names[CT_VM_NOPS + AI_NCOND][40];

static void buildRegistry(const uint8_t* rom) {
  s_n = 0;
  for (int i = 0; i < SH_NFIXED; i++) s_entries[s_n++] = k_fixed[i];
  for (int i = 0; i < CT_VM_NOPS; i++) {
    snprintf(s_names[i], sizeof s_names[i], "vm op %02X %s", g_ctVmOps[i].op, g_ctVmOps[i].name);
    // m 8-bit, x 16-bit: the runner's 5A9E REP #10 and its SEP #20
    CtShadowEntry e = { 0xC00000u | g_ctVmOps[i].addr, s_names[i], 0x30, 0x20, 0, g_ctVmOps[i].op, vmGuard, vmRun,
                        CT_SH_X16 | CT_SH_C, NULL };
    s_entries[s_n++] = e;
  }
  for (int op = 0; op < AI_NCOND; op++) {        // the handler addresses, from the ROM's own table at C1/B80D
    char* nm = s_names[CT_VM_NOPS + op];
    snprintf(nm, 40, "battle AI condition %02X", op);
    uint16_t hand = (uint16_t)(rom[0x01B80D + op * 2] | (rom[0x01B80D + op * 2 + 1] << 8));
    CtShadowEntry e = { 0xC10000u | hand, nm, 0x30, 0x20, 0, op, aiCondGuard, aiCondRun, 0, NULL };
    s_entries[s_n++] = e;
  }
}

// ---- State -----------------------------------------------------------------------------------------------------

typedef struct { long calls, matched, mismatched, interrupted, exits, skipped, unported, skipDiffer, dead, unresolved; } ShStats;
typedef struct {
  const CtShadowEntry* e; int idx;
  uint16_t entrySp, minSp; uint32_t retPc;
  int interrupted; long callNo; uint32_t frame;
  CtShadowRegs in, out;
} ShActive;

static Snes* s_snes;
static const uint8_t* s_rom; static long s_romLen;
static const char* s_dump; static int s_dumped; static uint32_t s_dumpAddr; static long s_dumpCall;
static ShStats s_stats[SH_MAX];
static ShActive s_act[SH_DEPTH]; static int s_depth; static long s_overflow;
static uint8_t s_work[SH_DEPTH][SH_WRAM], s_pre[SH_DEPTH][SH_WRAM];
static uint8_t s_bits[1 << 21];                 // one bit per 24-bit address: is a registered entry there
static char s_detail[SH_DETAILS][256]; static int s_nDetail;

static void addDetail(const char* fmtd) {
  if (s_nDetail < SH_DETAILS) { strncpy(s_detail[s_nDetail], fmtd, sizeof s_detail[0] - 1); s_nDetail++; }
}

// The liveness watch (docs/SHADOW_PROPOSAL.md, S3). A call whose registers match but whose WRAM differs in at most
// SH_REC_ADDRS bytes is not graded at once: those bytes are watched on the oracle through every WRAM access (CPU
// reads/writes, WMDATA through its latch, DMA/HDMA via g_dmaBusHook). If the next access to each is a write, the
// difference is dead scratch and the call counts `dead`; if one is read first (read-modify-write and DMA source
// included), it counts `mismatched`, naming the reader. A wider or register difference is graded at once. Bytes
// never touched again by the end of the run leave the call `unresolved`.
#define SH_WATCH_TOP SH_WRAM
#define SH_REC 65536
#define SH_REC_ADDRS 96
typedef struct {
  int idx, remaining, done, n; long callNo; uint32_t frame; uint16_t entrySp; CtShadowRegs in;
  uint32_t addr[SH_REC_ADDRS]; uint8_t oracle[SH_REC_ADDRS], port[SH_REC_ADDRS];
} ShRec;
static ShRec s_rec[SH_REC]; static int s_recNext, s_recLive;
static int s_owner[SH_WATCH_TOP];               // the record watching the byte, +1; 0 = none
static uint8_t s_deadAt[SH_MAX][SH_WATCH_TOP / 8];
static CpuReadHandler s_origRead; static CpuWriteHandler s_origWrite;

static void recClose(ShRec* rc) { rc->done = 1; s_recLive--; }
static uint8_t s_recPort(const ShRec* rc, int k) {           // what our C left at k
  for (int j = 0; j < rc->n; j++) if ((int)rc->addr[j] == k) return rc->port[j];
  return 0;
}
// The cartridge touched watched byte k: a read proves the call's difference live, a write retires the byte.
static void watchHit(Snes* s, int k, int isRead, int byDma) {
  ShRec* rc = &s_rec[s_owner[k] - 1];
  s_owner[k] = 0;
  if (rc->done) return;
  ShStats* st = &s_stats[rc->idx];
  if (isRead) {
    char buf[256];
    st->mismatched++;
    snprintf(buf, sizeof buf, "mismatch %06X %s: call #%ld (frame %u) A=%04X X=%04X Y=%04X S=%04X: %d WRAM byte(s), "
             "%02X:%04X oracle %02X port %02X READ by the cartridge's %s at %02X:%04X (frame %u)",
             s_entries[rc->idx].addr, s_entries[rc->idx].name, rc->callNo, rc->frame, rc->in.a, rc->in.x, rc->in.y,
             rc->entrySp, rc->n, 0x7E + (k >> 16), k & 0xFFFF, s->ram[k], s_recPort(rc, k),
             byDma ? "DMA, started" : "CPU", s->cpu->k, s->cpu->pc, s->frames);
    addDetail(buf);
    recClose(rc);
  } else if (--rc->remaining == 0) {
    st->dead++;
    for (int j = 0; j < rc->n; j++) s_deadAt[rc->idx][rc->addr[j] >> 3] |= (uint8_t)(1u << (rc->addr[j] & 7));
    recClose(rc);
  }
}
static int wramOffset(const Snes* s, uint32_t adr) {
  uint8_t bk = (uint8_t)(adr >> 16); uint16_t lo = (uint16_t)adr;
  if (bk == 0x7E || bk == 0x7F) return (int)(adr - 0x7E0000);
  if (bk < 0x40 || (bk >= 0x80 && bk < 0xC0)) {
    if (lo < 0x2000) return lo;
    if (lo == 0x2180) return (int)(s->ramAdr & 0x1FFFF);   // WMDATA: the WRAM port's latch
  }
  return -1;
}
static uint8_t shRead(void* mem, uint32_t adr) {
  if (s_recLive) { int k = wramOffset((Snes*)mem, adr); if (k >= 0 && s_owner[k]) watchHit((Snes*)mem, k, 1, 0); }
  return s_origRead(mem, adr);
}
static void shWrite(void* mem, uint32_t adr, uint8_t val) {
  if (s_recLive) { int k = wramOffset((Snes*)mem, adr); if (k >= 0 && s_owner[k]) watchHit((Snes*)mem, k, 0, 0); }
  s_origWrite(mem, adr, val);
}
static void shDma(void* mem, uint32_t adr, int isRead) {
  if (s_recLive) { int k = wramOffset((Snes*)mem, adr); if (k >= 0 && s_owner[k]) watchHit((Snes*)mem, k, isRead, 1); }
}
static uint8_t cpuP(const Cpu* c) {
  return (uint8_t)((c->n << 7) | (c->v << 6) | (c->mf << 5) | (c->xf << 4) | (c->d << 3) | (c->i << 2) | (c->z << 1) | c->c);
}
static uint8_t stackByte(const Snes* s, uint16_t a) { return s->ram[a & 0x1FFF]; }   // bank 0's low 8 KB is WRAM

static void writeFile(const char* prefix, const char* what, const uint8_t* data) {
  char path[512]; snprintf(path, sizeof path, "%s.%s.wram", prefix, what);
  FILE* f = fopen(path, "wb"); if (f) { fwrite(data, 1, SH_WRAM, f); fclose(f); }
}
static int skipped(const CtShadowSkip* sk, int k) {
  for (; sk && sk->why; sk++) if ((uint32_t)k >= sk->lo && (uint32_t)k <= sk->hi) return 1;
  return 0;
}

// The oracle is back at its caller: grade the call.
static void finish(Snes* s, ShActive* a, uint32_t pc) {
  ShStats* st = &s_stats[a->idx];
  char buf[256];
  if (a->interrupted) { st->interrupted++; return; }
  if (pc != a->retPc) {
    st->exits++;
    snprintf(buf, sizeof buf, "exit %06X %s: call #%ld (frame %u) left to %06X, not the return %06X",
             a->e->addr, a->e->name, a->callNo, a->frame, pc, a->retPc);
    addDetail(buf);
    return;
  }
  const uint8_t* w = s_work[a - s_act];
  if (s_dump && s_dumpCall && a->callNo == s_dumpCall && a->e->addr == s_dumpAddr) {   // --dumpcall, at the return
    writeFile(s_dump, "oracle", s->ram); writeFile(s_dump, "port", w);
  }
  int nDiff = 0, first = -1, skipDiff = 0;
  for (int k = 0; k < SH_WRAM; k++) {
    if (!(k & 0xFF) && !memcmp(w + k, s->ram + k, 0x100)) { k += 0xFF; continue; }   // an equal page, at once
    if (k > a->minSp && k <= a->entrySp) continue;   // the bytes the call pushed and popped
    if (w[k] == s->ram[k]) continue;
    if (skipped(a->e->skip, k)) { skipDiff = 1; continue; }
    if (first < 0) first = k;
    nDiff++;
  }
  if (skipDiff) st->skipDiffer++;
  const Cpu* c = s->cpu; unsigned o = a->e->outRegs;
  int regBad = 0; char regs[128] = "";
  if ((o & CT_SH_A8) && (c->a & 0xFF) != (a->out.a & 0xFF)) {
    regBad = 1; snprintf(regs, sizeof regs, " A oracle %02X port %02X", c->a & 0xFF, a->out.a & 0xFF); }
  if ((o & CT_SH_A16) && c->a != a->out.a) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " A oracle %04X port %04X", c->a, a->out.a); }
  if ((o & CT_SH_X16) && c->x != a->out.x) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " X oracle %04X port %04X", c->x, a->out.x); }
  if ((o & CT_SH_Y16) && c->y != a->out.y) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " Y oracle %04X port %04X", c->y, a->out.y); }
  if ((o & CT_SH_C) && (int)c->c != (a->out.p & 1)) {
    regBad = 1; snprintf(regs + strlen(regs), sizeof regs - strlen(regs), " carry oracle %d port %d", c->c, a->out.p & 1); }
  if (!nDiff && !regBad) { st->matched++; return; }
  if (!regBad && nDiff <= SH_REC_ADDRS) {           // the liveness watch decides
    {
      ShRec* rc = &s_rec[s_recNext]; s_recNext = (s_recNext + 1) % SH_REC;
      if (rc->n && !rc->done) { s_stats[rc->idx].unresolved++; recClose(rc); }   // the ring wrapped on it
      memset(rc, 0, sizeof *rc);
      rc->idx = a->idx; rc->callNo = a->callNo; rc->frame = a->frame; rc->entrySp = a->entrySp; rc->in = a->in;
      for (int k = 0; k < SH_WATCH_TOP; k++) {
        if (!(k & 0xFF) && !memcmp(w + k, s->ram + k, 0x100)) { k += 0xFF; continue; }   // an equal page, at once
        if (k > a->minSp && k <= a->entrySp) continue;
        if (w[k] == s->ram[k] || skipped(a->e->skip, k)) continue;
        rc->addr[rc->n] = (uint32_t)k; rc->oracle[rc->n] = s->ram[k]; rc->port[rc->n] = w[k]; rc->n++;
        if (s_owner[k]) {                            // an older call's difference never touched since: hand it over
          ShRec* old = &s_rec[s_owner[k] - 1];
          if (!old->done && --old->remaining == 0) {
            s_stats[old->idx].dead++;
            for (int j = 0; j < old->n; j++) s_deadAt[old->idx][old->addr[j] >> 3] |= (uint8_t)(1u << (old->addr[j] & 7));
            recClose(old);
          }
        }
        s_owner[k] = (int)(rc - s_rec) + 1;
      }
      rc->remaining = rc->n; s_recLive++;
      return;
    }
  }
  st->mismatched++;
  if (nDiff)
    snprintf(buf, sizeof buf, "mismatch %06X %s: call #%ld (frame %u) A=%04X X=%04X Y=%04X S=%04X: %d WRAM byte(s), "
             "first %02X:%04X oracle %02X port %02X;%s", a->e->addr, a->e->name, a->callNo, a->frame, a->in.a, a->in.x,
             a->in.y, a->entrySp, nDiff, 0x7E + (first >> 16), first & 0xFFFF, s->ram[first], w[first], regs);
  else
    snprintf(buf, sizeof buf, "mismatch %06X %s: call #%ld (frame %u) A=%04X X=%04X Y=%04X S=%04X:%s",
             a->e->addr, a->e->name, a->callNo, a->frame, a->in.a, a->in.x, a->in.y, a->entrySp, regs);
  addDetail(buf);
  if (s_dump && !s_dumped && (!s_dumpAddr || a->e->addr == s_dumpAddr) && (!s_dumpCall || a->callNo == s_dumpCall)) {
    s_dumped = 1;
    writeFile(s_dump, "pre", s_pre[a - s_act]); writeFile(s_dump, "oracle", s->ram); writeFile(s_dump, "port", w);
  }
}

// The census (docs/SHADOW_PROPOSAL.md, S3): the target of each JSR / JSL / JSR (abs,X) the oracle executes, counted
// at the next instruction fetched (an interrupt in between clears the mark). It reports the share of calls whose
// target is a registered entry, and the most-called routines that are not.
#define CEN_SLOTS (1 << 16)
static int s_censusTop;                          // 0 = off
static uint32_t s_cenKey[CEN_SLOTS]; static long s_cenCnt[CEN_SLOTS];
static int s_cenPrevCall;
static uint8_t peekCode(const Snes* s, uint32_t pc) {
  uint8_t bk = (uint8_t)(pc >> 16); uint16_t lo = (uint16_t)pc;
  if (bk == 0x7E || bk == 0x7F) return s->ram[pc - 0x7E0000];
  if ((bk < 0x40 || (bk >= 0x80 && bk < 0xC0)) && lo < 0x2000) return s->ram[lo];
  if (bk >= 0xC0 || ((bk < 0x40 || (bk >= 0x80 && bk < 0xC0)) && lo >= 0x8000)) {
    uint32_t o = ((uint32_t)(bk & 0x3F) << 16) | lo;
    return o < s->cart->romSize ? s->cart->rom[o] : 0;
  }
  return 0;
}
static void censusNote(const Snes* s, uint32_t pc) {
  if (s_cenPrevCall) {
    uint32_t h = (pc * 2654435761u) >> 16;
    for (int i = 0; i < CEN_SLOTS; i++) {
      uint32_t k = (h + (uint32_t)i) & (CEN_SLOTS - 1);
      if (s_cenCnt[k] && s_cenKey[k] != pc) continue;
      s_cenKey[k] = pc; s_cenCnt[k]++; break;
    }
  }
  uint8_t op = peekCode(s, pc);
  s_cenPrevCall = (op == 0x20 || op == 0x22 || op == 0xFC);
}
void CtShadowSetCensus(int top) { s_censusTop = top; }
void CtShadowSetDumpCall(long n) { s_dumpCall = n; }

static int findEntry(uint32_t pc) {
  for (int i = 0; i < s_n; i++) if (s_entries[i].addr == pc) return i;
  return -1;
}

static void shOp(void* mem, uint32_t pc) {
  Snes* s = (Snes*)mem; Cpu* c = s->cpu;
  if (s != s_snes) return;
  while (s_depth > 0) {
    ShActive* a = &s_act[s_depth - 1];
    if (c->sp > a->entrySp) {                       // the RTS/RTL (or whatever unwound it) has run
      finish(s, a, pc);
      s_depth--;
      if (s_depth > 0 && a->minSp < s_act[s_depth - 1].minSp) s_act[s_depth - 1].minSp = a->minSp;
      continue;
    }
    if (c->sp < a->minSp) a->minSp = c->sp;
    break;
  }
  if (s_censusTop) censusNote(s, pc);
  if (!(s_bits[pc >> 3] & (1u << (pc & 7)))) return;
  int first = findEntry(pc);
  if (first < 0) return;
  CtShadowRegs in = { c->a, c->x, c->y, c->dp, c->db, cpuP(c),
                      { s->apu->outPorts[0], s->apu->outPorts[1], s->apu->outPorts[2], s->apu->outPorts[3] } };
  // Several entries may share an address (two AI condition ops on one handler): the first whose convention holds.
  int idx = -1;
  for (int i = first; i < s_n && idx < 0; i++)
    if (s_entries[i].addr == pc && !c->e && (in.p & s_entries[i].pMask) == s_entries[i].pValue &&
        (!s_entries[i].guard || s_entries[i].guard(&s_entries[i], s->ram, &in))) idx = i;
  if (idx < 0) { s_stats[first].calls++; s_stats[first].skipped++; return; }
  const CtShadowEntry* e = &s_entries[idx];
  ShStats* st = &s_stats[idx];
  st->calls++;
  if (s_depth == SH_DEPTH) { s_overflow++; return; }
  int d = s_depth;
  memcpy(s_work[d], s->ram, SH_WRAM);
  CtShadowRegs out = in;
  int how = e->run(e, s_work[d], s_rom, s_romLen, &in, &out);
  if (how == CT_SH_UNPORTED) { st->unported++; return; }
  if (s_dump && !s_dumped && (!s_dumpAddr || e->addr == s_dumpAddr)) memcpy(s_pre[d], s->ram, SH_WRAM);   // --dump
  if (s_dumpCall && e->addr == s_dumpAddr && st->calls == s_dumpCall && s_dump)
    writeFile(s_dump, "pre", s->ram);           // --dumpcall: the entry's call N, whatever its grade
  s_depth++;
  ShActive* a = &s_act[d];
  a->e = e; a->idx = idx; a->entrySp = c->sp; a->minSp = c->sp; a->interrupted = 0;
  a->callNo = st->calls; a->frame = s->frames; a->in = in; a->out = out;
  uint16_t ret = (uint16_t)(stackByte(s, c->sp + 1) | (stackByte(s, c->sp + 2) << 8));
  uint8_t bank = e->isLong ? stackByte(s, c->sp + 3) : c->k;
  a->retPc = ((uint32_t)bank << 16) | (uint16_t)(ret + 1);
}
static void shInt(void* mem) {
  if ((Snes*)mem != s_snes) return;
  s_cenPrevCall = 0;
  for (int i = 0; i < s_depth; i++) s_act[i].interrupted = 1;
}

void CtShadowInstall(Snes* s, const char* dumpPrefix, uint32_t dumpAddr) {
  buildRegistry(s->cart->rom);
  s_snes = s; s_rom = s->cart->rom; s_romLen = (long)s->cart->romSize;
  s_dump = dumpPrefix; s_dumpAddr = dumpAddr; s_dumped = 0; s_depth = 0; s_overflow = 0; s_nDetail = 0;
  memset(s_stats, 0, sizeof s_stats); memset(s_bits, 0, sizeof s_bits);
  for (int i = 0; i < s_n; i++) s_bits[s_entries[i].addr >> 3] |= (uint8_t)(1u << (s_entries[i].addr & 7));
  memset(s_rec, 0, sizeof s_rec); memset(s_owner, 0, sizeof s_owner); memset(s_deadAt, 0, sizeof s_deadAt);
  memset(s_cenKey, 0, sizeof s_cenKey); memset(s_cenCnt, 0, sizeof s_cenCnt); s_cenPrevCall = 0;
  s_recNext = 0; s_recLive = 0;
  s_origRead = s->cpu->read; s_origWrite = s->cpu->write;
  s->cpu->read = shRead; s->cpu->write = shWrite; g_dmaBusHook = shDma;
  g_cpuShadowOpHook = shOp; g_cpuShadowIntHook = shInt;
}
void CtShadowUninstall(void) {
  if (s_snes) { s_snes->cpu->read = s_origRead; s_snes->cpu->write = s_origWrite; }
  g_dmaBusHook = NULL;
  for (int i = 0; i < SH_REC; i++)                 // what the run never resolved
    if (s_rec[i].n && !s_rec[i].done) { s_stats[s_rec[i].idx].unresolved++; s_rec[i].done = 1; }
  s_recLive = 0;
  g_cpuShadowOpHook = NULL; g_cpuShadowIntHook = NULL; s_snes = NULL;
}

long CtShadowMismatches(void) { long n = 0; for (int i = 0; i < s_n; i++) n += s_stats[i].mismatched; return n; }

void CtShadowReport(FILE* f) {
  long calls = 0, matched = 0, mism = 0, intr = 0, exits = 0, skip = 0, unp = 0, dead = 0, unres = 0; int reached = 0;
  for (int i = 0; i < s_n; i++) {
    const ShStats* st = &s_stats[i];
    if (!st->calls) continue;
    reached++;
    fprintf(f, "%06X %-32s calls %ld  matched %ld  mismatched %ld  interrupted %ld  exits %ld  skipped %ld",
            s_entries[i].addr, s_entries[i].name, st->calls, st->matched, st->mismatched, st->interrupted, st->exits,
            st->skipped);
    if (st->unported) fprintf(f, "  unported %ld", st->unported);
    if (st->dead) fprintf(f, "  dead %ld", st->dead);
    if (st->unresolved) fprintf(f, "  unresolved %ld", st->unresolved);
    if (st->skipDiffer) fprintf(f, "  (%ld differed only where not compared)", st->skipDiffer);
    fprintf(f, "\n");
    calls += st->calls; matched += st->matched; mism += st->mismatched; intr += st->interrupted; exits += st->exits;
    skip += st->skipped; unp += st->unported; dead += st->dead; unres += st->unresolved;
  }
  for (int i = 0; i < s_nDetail; i++) fprintf(f, "  %s\n", s_detail[i]);
  if (s_overflow) fprintf(f, "  %ld call(s) past the nesting depth %d, not compared\n", s_overflow, SH_DEPTH);
  // The dead scratch, per entry: bytes the C leaves differently, which the cartridge rewrote before any read.
  for (int i = 0; i < s_n; i++) {
    if (!s_stats[i].dead) continue;
    char line[2048]; int o = snprintf(line, sizeof line, "  dead scratch %06X:", s_entries[i].addr);
    for (int k = 0; k < SH_WATCH_TOP && o < (int)sizeof line - 16; k++) {
      if (!(s_deadAt[i][k >> 3] & (1u << (k & 7)))) continue;
      int e2 = k; while (e2 + 1 < SH_WATCH_TOP && (s_deadAt[i][(e2 + 1) >> 3] & (1u << ((e2 + 1) & 7)))) e2++;
      o += (e2 > k) ? snprintf(line + o, sizeof line - o, " %02X:%04X-%04X", 0x7E + (k >> 16), k & 0xFFFF, e2 & 0xFFFF)
                    : snprintf(line + o, sizeof line - o, " %02X:%04X", 0x7E + (k >> 16), k & 0xFFFF);
      k = e2;
    }
    fprintf(f, "%s\n", line);
  }
  // The ranges not compared, listed whether or not anything differed there.
  { const CtShadowSkip* seen[8]; int ns = 0;
    for (int i = 0; i < s_n; i++)
      for (const CtShadowSkip* sk = s_entries[i].skip; sk && sk->why; sk++) {
        int dup = 0; for (int j = 0; j < ns; j++) if (seen[j] == sk) dup = 1;
        if (dup || ns == 8 || !s_stats[i].calls) continue;
        seen[ns++] = sk;
        fprintf(f, "  not compared: $%04X-$%04X, %s\n", sk->lo, sk->hi, sk->why);
      } }
  if (s_censusTop) {
    // The census: the targets ranked by calls; graded = a registered entry sits at the target.
    static int order[CEN_SLOTS]; int n = 0; long all = 0, graded = 0; int gradedR = 0;
    for (int k = 0; k < CEN_SLOTS; k++) if (s_cenCnt[k]) {
      order[n++] = k; all += s_cenCnt[k];
      if (s_bits[s_cenKey[k] >> 3] & (1u << (s_cenKey[k] & 7))) { graded += s_cenCnt[k]; gradedR++; }
    }
    for (int i = 1; i < n; i++) {                   // by calls, descending; address breaks ties
      int v = order[i], j = i - 1;
      while (j >= 0 && (s_cenCnt[order[j]] < s_cenCnt[v] ||
                        (s_cenCnt[order[j]] == s_cenCnt[v] && s_cenKey[order[j]] > s_cenKey[v]))) { order[j + 1] = order[j]; j--; }
      order[j + 1] = v;
    }
    fprintf(f, "census: %ld calls to %d routines; %ld calls (%.1f%%) to the %d routines a registered entry sits at\n", all,
            n, graded, all ? 100.0 * (double)graded / (double)all : 0.0, gradedR);
    int shown = 0;
    for (int i = 0; i < n && shown < s_censusTop; i++) {
      uint32_t k = s_cenKey[order[i]];
      if (s_bits[k >> 3] & (1u << (k & 7))) continue;
      fprintf(f, "  census ungraded %02X/%04X  %ld calls\n", k >> 16, k & 0xFFFF, s_cenCnt[order[i]]);
      shown++;
    }
  }
  fprintf(f, "shadow: %d entries, %d reached, %ld calls, matched %ld, mismatched %ld, interrupted %ld, exits %ld, "
          "skipped %ld, unported %ld, dead %ld, unresolved %ld\n", s_n, reached, calls, matched, mism, intr, exits, skip,
          unp, dead, unres);
}
