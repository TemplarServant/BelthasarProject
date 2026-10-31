// move.h -- field objects: the move step, the event-script runner and its opcode families, and the sprite animation
// pipeline (enqueue, drain, release, draw). See disasm/notes/field_tick.md.
#ifndef CT_MOVE_H
#define CT_MOVE_H

#include <stdint.h>

// The move-toward-tile step of op 0xA0 (C0/5129):
//  - if the move counter $1A01[idx] is still running, yields (returns 0, no change);
//  - else if the current tile ($1801/$1881) equals the script target ($7F2001[$1180[idx]+1/+2]), returns 1 (the
//    caller advances the IP); with $1C81 bit 0 set, the arrival snap C0/305D runs first;
//  - else issues a step: heading $01EE (C0/ABA2, nudged by +$30 when C0/5B95 reports a blocking actor), velocity and
//    OBJ priority (C0/ACFD), and the counter $1A01 reloaded from $1000[idx] (mirrored to $7F0B00); returns 0.
// `ram` is the WRAM image ($7E at offset 0, $7F at 0x10000); `rom` the HiROM image.
int CtActorMoveStep(uint8_t* ram, const uint8_t* rom, int idx);

// Sweep hook (--sweepsnap) for C0/305D, the arrival snap: zeroes both velocities and moves one pixel per frame
// toward the tile's X centre and Y bottom, arming $1A01 = 1. Returns 1 = SEC (still snapping), 0 = CLC (in place).
int ctSweepSnapCall(uint8_t* ram, int idx);

// C0/AA07, the position integration for states 3..6 (the move loop's A82A branch). For $1B80 == 0 it applies the
// $7F0B00,X tile clamp: a move that would cross a tile boundary on a non-first step frame stops the counter and
// pauses one frame. Writes $1800/$1880 and $1A01 (and $1980/$1981 on the $1B80 != 0 path).
void CtActorIntegrate(uint8_t* ram, int idx);

// C0/9E84: the PC's facing selector, called from the head of C0/9E29. Indexes a 64-entry word table at $C0:9FF7 from
// the velocity deltas ($1D2C/$1D2D) and dispatches to 16 handlers: 8 cardinal (set the facing) and 8 diagonal (keep
// the facing when it is one of the diagonal's two components, else force the first), with a walk tail (C0/A171,
// mode 1) and a run tail (C0/A18C, mode 6, or $17 when running without moving). The default $9FF2 is the idle chain
// (C0/A1E9: stand / mode $19 / blink $21, the blink gated on the $C0:FE00 table indexed by $01F8). Every tail shares
// one CMP-guarded body: an unchanged mode resets neither phase nor timer. Writes $1600, $1680, $1681, $1601, $0158,
// $0159, $01E5, $01F8 and $01DB. `ct.exe --sweepanim` checks it; disasm/notes/player_anim_9e84.md.
void CtPlayerAnimSelect(uint8_t* ram, const uint8_t* rom, int romLen, int idx);
// The JSR ($9FF7,X) dispatch alone (C0/9ECD, and the followers' C0/9F1C/9F6B/9FCF): `ix` = the folded even table
// index, y = the object index (obj*2). CtAnimModeSet = the shared CMP-guarded mode store (the C0/A175 body) for the
// arms that reach a tail by name (A254, mode $1B).
void CtAnimDispatch9FF7(uint8_t* ram, const uint8_t* rom, int romLen, int y, int ix);
void CtAnimModeSet(uint8_t* ram, int y, int k);

// C0/A88D: the object activation gate, a predicate recomputed on each call: state bit 7 clear, $1A81,X in 1..$7F,
// and $1801/$1881 (doubled, 8-bit) inside the camera window ($1D0A-4, $1D0C+4, $1D0E-2, $1D10+5, each saturating).
// Writes the window to $01C3-$01C6 and the latch to $0F00,X and returns the carry that gates C0/C98A, A98A, AB45 and
// A9CD in both arms of C0/A810. Not included: the rising-edge one-shot, its sub-calls and the falling-edge tails
// (see move.c). `ct.exe --sweepa88d` checks it.
int CtObjActiveGate(uint8_t* ram, int idx);

// C0/3154: the player's touch redirect, reached from op 0xB0's state-0 tail when C0/5B8D finds an object index in
// $01EB (bit 7 clear) rather than the $80 sentinel. It is op 0x02's call arm with the target from the touch latch and
// priority and function slot fixed at 2. Writes $1180/$1181, $1C00, $1A80, $1A01 and $1001 of the touched object and
// saves its IP to $7F0580 + (curPrio*128 + idx). Not included: the C0/31B1 tail. `ct.exe --sweeptouch` checks it.
void CtVmTouchRedirect(uint8_t* ram);

// Trace of the port's runner, the counterpart of --portrun's dispatch probe at C0/5AB1 (frame, opcode, $0167,
// $0168). Set g_ctVmTraceIdx to an object index (obj*2) to print CtFieldRunner's sequence; -1 = off.
// g_ctVmTraceFrame is stamped by the caller so the two traces can be read side by side.
extern int g_ctVmTraceIdx;
extern int g_ctVmTraceFrame;

// The SPC's output mailbox port 3, the field VM's one input from outside WRAM. Op 0xEE (C0/3CC5) does LDA $2143 and
// parks until the low nibble reads zero. $2143 is a hardware register, so the host supplies it, as CtJoypadPoll takes
// the joypad word:
//   * ct.exe    -> the oracle's APU (snes.c serves CPU reads of $2140-3 from apu->outPorts[adr & 3])
//   * Belthasar -> the SPC program src/audio.c drives
// The bank-$C7 driver writes $2143; the field engine reads it only at C0/3CC6.
extern uint8_t g_ctApuPort3;

// The SPC's output mailbox port 1, read by op 0xED (C0/3CBC) and supplied the same way. `LDA $2141` appears only at
// C0/3CBD and C7/0A80 in the ROM. Default 0 is the arm that lets the script advance.
extern uint8_t g_ctApuPort1;

// The SPC's four output ports ($2140-$2143 as the CPU reads them), for a script's pointer read (ops 0x48 / 0x49,
// C0/6792 / 67C7) whose 24-bit pointer names one: `48 42 21 00 nn` reads the song's marker on port 2 into a local and
// the script loops on it (scene 432 obj11, the Epoch's cockpit, before `FF 26`). The host fills them each frame from
// its SPC (Belthasar: src/audio.c's outPorts, field.c before the VM's tick); a host that never does reads zeros.
extern uint8_t g_ctApuOut[4];

// C0/ACFD: the field velocity + priority builder (the two components through the hardware multiplier, the $1C80,X
// bit-0 gate, the $FE00 bounce retry and the DP staging $01D9/$01DB-$01E1/$01E5). It reads the heading from $01EE
// because the bounce arm rewrites that byte (AE31 STA $EE); callers stage $01EE first. `ct.exe --sweepacfd` checks it.
void CtBuildVelocityPriority(uint8_t* ram, const uint8_t* rom, int idx);

// C0/5FB6, op 0x02: the priority call, the only op here that writes another object's state. Operand 1 is the target
// object; operand 2 is the new priority (high nibble) and the script-function slot (low nibble). Three reject gates
// ($1C01,X != 0 / $1100,X bit 7 / $1000,X bit 7) and four arms, all exiting ip+3 with carry set. Returns the new IP
// and writes the carry through `carry`. `ct.exe --sweepop02` checks it.
int CtVmOp02(uint8_t* ram, int ip, int* carry);

// C0/6109: op 0x04, the blocking priority call. Unlike 0x02 it needs the caller's own index: the $7F0980 latch that
// splits its fresh and poll arms is indexed by the caller, not the target.
int CtVmOp04(uint8_t* ram, int idx, int ip, int* carry);

// One opcode handler's result, what a ROM handler hands back to the runner C0/5A93: the new IP (X, committed at
// 5ABF), the carry (1 = keep spending the visit budget, 0 = yield; tested at 5AB4) and ok (0 = not ported: trap).
typedef struct { int ip; int carry; int ok; } CtHRet;
// Dispatch one opcode at `ip` for object `idx`. Returns the trapped opcode, or -1 on success. Exposed so --sweepops
// can run a single handler and check its carry and returned IP. `romLen` bounds the object loader's ROM reads (op
// 0x82 -> CtObjLoad); every other handler ignores it.
int CtFieldRunnerOne(uint8_t* ram, const uint8_t* rom, int romLen, int idx, int ip, CtHRet* out);

// C0/5A93, the event-script runner: runs object `idx`'s tick. Fetches the opcode at $7F2001 + $1180[idx], dispatches
// it, and continues while a handler returns carry set, up to the op budget $0168; carry clear ends the tick. The IP
// is written back to $1180[idx]. Returns -1, or the first opcode that is not ported (and stops there).
int CtFieldRunner(uint8_t* ram, const uint8_t* rom, int romLen, int idx);

// The field-script opcodes CtFieldRunnerOne ports, each with its bank-$C0 handler (the target of the runner's
// JSR ($5D6E,X) at C0/5AB1, entered with X = op*2 and Y = the IP). One table for --sweepops and the routine-level
// shadow (ct_shadow.c).
typedef struct { int op; uint16_t addr; const char* name; } CtVmOpInfo;
#define CT_VM_NOPS 211
extern const CtVmOpInfo g_ctVmOps[CT_VM_NOPS];

// C1/8003 -> JMP C1/CFC2 -> JSR ($D126,X): the six-leaf inventory/gold gate that ops 0xC7/0xC9/0xCA/0xCB/0xCC/0xCD/
// 0xCE call with LDA #leaf / JSL $C1:8003. Leaves: 0 = C1/CFE9 HasItem, 1 = C1/D005 AddItem, 2 = C1/D056 RemoveItem,
// 3 = C1/D086 HasGold, 4 = C1/D0A2 AddGold, 5 = C1/D0ED SubGold (6/7 = C1/AA7B / C1/1686, battle). The leaf runs with
// DB = $7E and D = $0000 (C1/CFCB-CFD6), so `ram` is the flat image with $7E at offset 0. `y` is the 16-bit argument
// staged in Y; the return is the leaf's 8-bit A.
int ctVmC1Gate(uint8_t* ram, int leaf, int y);

// The seven ops that call it: 0xC7/0xC9/0xCA/0xCB (items) and 0xCC/0xCD/0xCE (gold), ROM C0/37C7 / 3780 / 37B1 / 37E3 /
// 3827 / 385B / 3874. Shared by CtFieldRunnerOne (--sweepops) and the live event VM's step() in ct_vm.c. `script` +
// `ip` because the two runners keep the script in different places; everything else is WRAM. Returns 1 if
// `script[ip]` is in the family.
int CtVmItemFamily(uint8_t* ram, const uint8_t* script, int ip, int* outIp);

// The poke family: 0x4A/0x4B/0x4C/0x4D, ROM C0/67D7 / 67FB / 680C / 6829: a byte or a word (literal, or a local from
// $7F0200) through the 24-bit pointer C0/67E3 stages. Same two callers as CtVmItemFamily; carry always set.
// Returns 1 if `script[ip]` is in the family.
int CtVmPokeFamily(uint8_t* ram, const uint8_t* script, int ip, int* outIp);

// The screen-effect ops: 0xF1 (C0/3D07, the colour-math transition: $0121/$0122 COLDATA, $0BE0 CGADSUB, $0118) and
// 0xFE (C0/3F26, the eight interpolation channels at $7F:1CE8.. that C0/21E1's entry 6 consumes). Same two callers as
// CtVmPokeFamily; the IP and the carry come back in *out. Returns 1 if `script[ip]` is one of them.
int CtVmScreenFxOp(uint8_t* ram, const uint8_t* script, int ip, CtHRet* out);

// Op 0x7A, C0/4D06: the jump toward a tile -- the arm (C0/ABA2's heading, C0/AC69's velocity into $1900/$1980, the ticket
// through the divider into $1A01, $1B80 = 1 and $1B81 = the third operand for C0/AAC7's arc) and the completion. Same two
// callers as CtVmPokeFamily; `idx` = the object's X. Returns 1 if `script[ip]` is 0x7A (the IP and the carry in *out).
int CtVmOp7A(uint8_t* ram, const uint8_t* rom, const uint8_t* script, int ip, int idx, CtHRet* out);

// The facing family: 0x0F/0x17/0x1B/0x1D/0xA6/0xA7 (self) and 0x1E/0x1F/0x25/0x26 (the object the operand names), ROM
// C0/5535..5593 -> the tail C0/554B-5556: $1600,X = the facing, $1601,X = 0 (the walk-anim timer). Same callers as
// CtVmPokeFamily; `idx` = the running object's X, `*outTgt` = the X written; carry always clear. Returns 1 if in it.
int CtVmFacingFamily(uint8_t* ram, const uint8_t* script, int ip, int idx, int* outIp, int* outTgt);

// The variable-assignment grid: 0x50-0x56, 0x58, 0x59, ROM C0/68C6..6A11: local/global/immediate copies, 8 or 16
// bits (a local word is little-endian, as the 65816 stores it). Same callers as CtVmPokeFamily; carry always set.
// Returns 1 if `script[ip]` is in the grid.
int CtVmVarFamily(uint8_t* ram, const uint8_t* script, int ip, int* outIp);

// The pointer read: 0x48/0x49, ROM C0/6792 / 67C7: a byte or a word through a script-supplied 24-bit pointer into a
// local. Same callers as CtVmPokeFamily; carry always set. Returns 1 if `script[ip]` is in the family.
int CtVmReadPtrFamily(uint8_t* ram, const uint8_t* script, int ip, int* outIp);

// The local inc / dec / set: 0x71/0x72/0x73/0x75/0x76/0x77, ROM C0/6C69..6CF0 (0x72 and 0x76 are 16-bit). Same
// callers; carry always set. Returns 1 if `script[ip]` is in the family.
int CtVmLocalIncFamily(uint8_t* ram, const uint8_t* script, int ip, int* outIp);

// The local arithmetic: 0x5B/0x5E/0x5F/0x60/0x61/0x63/0x64/0x67/0x69/0x6B/0x6F, ROM C0/6A1F..6C68 (`rom` holds the
// $FF20/$FF28 mask tables 0x63/0x64 index). Same callers; carry always set. Returns 1 if `script[ip]` is in it.
int CtVmLocalArithFamily(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int* outIp);

// Op families shared by CtFieldRunnerOne and the live VM (disasm/notes/live_vm_op_gaps.md). One signature: `script` =
// the $7F:2001 image the IP indexes, `idx` = the object's X; the carry is per op, so it comes back in `out`. Each
// returns 1 if `script[ip]` is its.
int CtVmObjEnableFamily(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out); // 0A 0B 0C
int CtVmSpeedFamily(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out);     // 89 8A
int CtVmResultFamily(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out);    // 19 1A 1C
int CtVmObjQueryFamily(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out);  // 20 23 24
int CtVmObjBranchFamily(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out); // 27 28
int CtVmPadBranchFamily(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out); // 30 31 34-39 3B 3C 3F-44 47
int CtVmCaptionFamily(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out);   // 29 2A 2B 2C 2F 32
// Their consumers: the main loop's C0/274D leg (bits 2/3/4 of $0154; 1 = an arm ran) and the field NMI's $46 arms 08/10/20
// (C0/EB46-EB77; returns the arm). A DMA they fire into VRAM goes to g_ctVramSink when a host keeps a VRAM image.
typedef void (*CtVramSinkFn)(int vramWord, const uint8_t* src, int bytes);
extern CtVramSinkFn g_ctVramSink;
int CtVmCaption274D(uint8_t* ram, const uint8_t* rom, int romLen);
int CtVmCaptionNmi(uint8_t* ram);
int CtVmPalOp33(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out);         // 33
int CtVmRandOp7F(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out);        // 7F
int CtVmScrollFamily(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out);    // E6 F4

// The $C2 party / roster / inventory-count family: 0xCF/0xD0/0xD1/0xD2/0xD3/0xD4/0xD7 and 0xF8/0xF9/0xFA, ROM C0/388D /
// 38BE / 38D3 / 38E8 / 3919 / 394E / 37F9 / 3E5C / 3E6E / 3E75, all reaching JSL $C2:8004 with a leaf index. Shared
// like CtVmItemFamily. The carry varies across the family, so it comes back in `out`. Returns 1 if `script[ip]` is in
// the family.
void CtVmC2Epilogue(uint8_t* ram, const uint8_t* rom, int romLen);   // C2/834D: weapon rows, triple techs, relics
void CtVmC2LoadTail(uint8_t* ram, const uint8_t* rom, int romLen, int x, int y);   // C0/56CB `LDA #0B / JSL C2/8004`: the gate's leaf 11 = the epilogue
int CtVmPartyGateFamily(uint8_t* ram, const uint8_t* rom, int romLen,
                        const uint8_t* script, int ip, CtHRet* out);

// Op 0x88, ROM C0/4892: the per-object colour effect: restore (48C0), rotate (4919), ramp (4970) and the inline
// palette blob (49F8). Shared by CtFieldRunnerOne (--sweepops) and the live VM's step() and init pass. `idx` = obj*2.
// The carry comes back in `out` (CLC = the slot table was full: the op is consumed and the visit yields).
void CtVmColorFx(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out);

// Op 0x2E, ROM C0/4A4E: the colour-effect record ($40/$50: a $0520 slot, for the fades FD/E82C / FD/E5A4), the
// inline palette blob ($80), else a two-byte no-op. Two callers, like CtVmColorFx; `script` = the $7F:2001 base.
void CtVmColorSlotOp(uint8_t* ram, const uint8_t* script, int ip, CtHRet* out);

// Op 0xE2, ROM C0/3B9E: op 0xE0's fade-change staged from four LOCALS ($0112 = the first's word, $0114/$0115/$0116 the
// others' low bytes), the $0117 latch, $0119 = 0F, and the park on the op. Two callers, like CtVmColorSlotOp.
void CtVmChangeLocVars(uint8_t* ram, const uint8_t* script, int ip, CtHRet* out);

// The animation family: 0xAA/0xAB/0xAC/0xAE/0xB3/0xB4/0xB7, ROM C0/2E67 / 2E9A / 2F71 / 2E8F / 2E87 / 2E8B / 2F01.
// Shared by CtFieldRunnerOne (--sweepops) and the live VM's step(). Writes $1680/$1681/$1601/$1780/$1781/$1301,X and
// $7F0B01,X as the handlers do; the carry varies (0xAC and the parking arms of 0xAB/0xB7 yield), so it comes back in
// `out`. Returns 1 if `script[ip]` is in the family.
int CtVmAnimFamily(uint8_t* ram, const uint8_t* script, int ip, int idx, CtHRet* out);

// Op 0xE4, ROM C0/3D97: the immediate scripted tile copy: stage $013E/$0140/$0142/$0144 and $01C7, then JSR C0/AF4E,
// which moves the rect through the WRAM map planes ($7E:3000/3040/3080/30C0/7000/7040 and the bit-6/7 merges). Shared
// by CtFieldRunnerOne (--sweepops) and the live VM's step(). Carry clear, eight bytes: the caller continues at ip + 8.
void CtVmTileCopyE4(uint8_t* ram, const uint8_t* script, int ip);

// Op 0xE5, ROM C0/3DC5: the queued tile copy: the same staging as 0xE4 plus $0145 = $0144 & 7 and TSB $17 bit 5, and
// no copy: the main loop's C0/1770 makes it a frame later (1790 JSR C0/AF4E). Carry clear, eight bytes. Shared like
// CtVmTileCopyE4.
void CtVmTileStageE5(uint8_t* ram, const uint8_t* script, int ip);
// C0/AF4E alone, on the operands already staged at $013E-$0144 -- C0/1790's call.
void CtVmTileCopyAF4E(uint8_t* ram);

// C0/65F4 / 6628 / 560E / 5423 / 61F2 / 6207 / 6225 / 623B: `LDA $97,X` with X = the script's operand. $0197/$0199/
// $019B are the three party slots' object offsets (obj*2, spilled by op 0x80's arms at C0/426A); bit 7 set = the slot
// is empty. So ops 0x22/0x24/0xA9/0x95/0xB6 name their target by party slot, and the operand is the already doubled
// slot index (0, 2 or 4 in the 504 shipped scenes). Shared with ct_vm.c's step().
int ctVmPartySlotTarget(const uint8_t* ram, int slot);

// C0/C98A: the per-object animation timer and phase advance, called from the movement loop. Decrements $1601[idx];
// at 0 (and for an animatable $1100 state, which also enqueues the object) advances the phase $1681[idx] and reloads
// $1601 from the bank-$E4 speed table (rom 0x240000): index = $1681 + (mod << 2) + $1580, mod = $1680 if $1780 <= 1
// else $1781; a zero entry re-looks-up at the phaseless base and (unless $1780 == 2) resets the phase.
void CtAnimTimerTick(uint8_t* ram, const uint8_t* rom, int romLen, int idx);

// C0/CB3A (and the CBDC cfg == 3 short-circuit): the frame lookup. The sequence pointer $D6 comes from the facing
// $1600 (0 -> $1500; 2 -> $1480*2+$1500; 1 -> $1480+$1500; 3 and above -> $1480*3+$1500) plus the phase $1681 and the
// mode modifier ($1781 if $1780 == 2 else $1680, << 2); the frame byte is read from the bank-$E4 table (rom
// 0x240000 + $D6). $1780 == 3 returns the static frame $1301. On the $FF sentinel it rewinds and re-fetches (phase
// $FF), or for $1780 == 2 returns -1 (CLC, do not commit). Returns the candidate byte.
int CtFieldFrameLookup(uint8_t* ram, const uint8_t* rom, int romLen, int idx);
// C0/5614, the move family's step-issue anim selector (idle / walk / run from |$1900|+|$1980|), on the image.
void CtVmMoveStepAnim(uint8_t* ram, int idx);

// C0/30B3: the one-pixel walk onto the target that the move-toward-actor body (C0/5228 and siblings) takes when the
// mover is adjacent and $1C81,X bit 1 is set (the party join's `0E 02`, so the actor does not stop a tile short).
// Zeroes both velocities, compares the whole-pixel parts (AND #FFF0) of self's and the target's $1800/$1880 (the
// target in $01E9, stored by the caller), and on a mismatch sets 1 px of velocity ($10 or $F0) plus $1A01 = $7F0B00
// = 1. Returns 1 = SEC (still snapping: the caller runs C0/5614 and yields), 0 = CLC (aligned: C0/568B, advance).
int CtVmMoveSnap30B3(uint8_t* ram, int idx);

// The four-slot sprite-assembly arena, C0/E952-EA62. $0BC0..$0BC3 is the owner table (bit 7 set = free, else the
// owner's $6D = obj*2) and $0D80,X receives the granted base as a word, $3800 + slot*$200 (an offset into the $7F
// staging bank). `n` = 1/2/3 slots (the piece class); a multi-slot claim needs consecutive slots. Alloc returns 1 =
// SEC (granted) / 0 = CLC (pool full, nothing written; the caller drops the animation frame). CtArenaFreeRun is
// C0/EA42: it releases the whole consecutive run owned by the object, not the same as CtArenaFree(..., 1).
int  CtArenaAlloc(uint8_t* ram, int idx, int n);
void CtArenaFree(uint8_t* ram, int idx, int n);
void CtArenaFreeRun(uint8_t* ram, int idx);

// C0/CAD9: the animation-queue drain's per-object visit. C0/CA76 pops its FIFO head into $6D and calls this; the carry
// is the protocol: SEC = call again with this object still at the head, CLC = finished, pop it. Four gate tests (all
// CLC), then a dispatch on $1201,X & 3, the sprite's piece count: 0 = 4 pieces (C0/CBDC), 1 = 8 (C0/CEF5), 2 = 12
// (C0/D4F7), 3 = never drawn by this pass. Returns 0 = CLC, 1 = SEC.
int  CtAnimDrainVisit(uint8_t* ram, const uint8_t* rom, int romLen, int idx);

// C0/B701: the arena release and the draw pass's per-object gate (its only caller, C0/B309, reads the carry as
// BCC -> draw). With T = (class & 3) + 1 (the arena arity) and n = $1B00,X: (n & 0x7F) < T means not fully staged:
// reposition-emit and draw when bit 7 is already set, else skip; at or above T it instantiate-emits, frees T slots
// and stamps $1B00,X = 0x80. Class 3 goes to its own emitter C0/C73A. Returns 1 = SEC (skip the draw), 0 = CLC (draw).
int  CtAnimRelease(uint8_t* ram, int idx);

// C0/CA76: the animation FIFO drain. Pops the head into $6D and calls CtAnimDrainVisit until the queue empties or the
// raster deadline expires; SEC keeps the object at the head, CLC pops it. `scanline` is the V counter the ROM
// assembles from two $213D reads (>= 0xF0 = vblank, no limit; otherwise the pass stops at the word budget $6B/$6C);
// a negative `scanline` uses the virtual beam (src/ct_vbeam.h), and a fixed one drains fully or not at all. Returns
// the visit count, -1 for an unported class, or -2 if the list did not converge in 64 visits (a port-side guard).
int  CtAnimDrainPass(uint8_t* ram, const uint8_t* rom, int romLen, int scanline);

// C0/B271: the draw pass, for the arena lifecycle. Walks the Y-sorted buckets $0E00 high to low along $0E81 and calls
// C0/B701 (CtAnimRelease) per object, the engine's only arena release; on its CLC it advances that object's
// per-group OAM cursors and counts it as drawn. The OAM bytes are not emitted here: src/oam.c's CtBuildFieldOAM models
// stage 2's copy (--checkoam). Returns the number of objects drawn; `unresolved` (may be NULL) receives the count
// whose gate was undecidable (expected 0).
int  CtAnimDrawPass(uint8_t* ram, int* unresolved);
extern int gCtDrawSkipped;   // how many objects the last CtAnimDrawPass gate refused (B701 SEC)
// Note: CtBuildFieldOAM walks the same list again and emits bytes for every object, so a refused object shifts every
// later object's OAM cursor by its own size; see the note above CtBuildFieldOAM in src/oam.c.
extern int gCtDrawClsMask;   // bitmask of the sprite classes that reached the last draw list
extern int gCtDrawRunaway;   // cumulative trips of the draw walk's non-convergence guard (should stay 0)

// The object loader's WRAM-field half: C0/4557 (the shared prologue) plus C0/4590 (NPC, state 4) / C0/46E1 (enemy,
// state 5). Consumes one script byte at $7F2001+ip+1 as the sprite id, walks its 5-byte descriptor at $E4:F023
// through the four bank-$E4 pointer tables, runs the C0/5C90 graphics dedup and decompresses a miss through
// ct_lz_decompress, advancing $71. Returns the new script IP. disasm/notes/object_loader.md.
int CtObjLoad(uint8_t* ram, const uint8_t* rom, int romLen, int idx, int ip, int state);

// The OBJ slot allocator/release: C0/6F9A (dispatch), C0/6FBA (one slot), C0/7008 (a consecutive pair) and C0/7056
// (release). $0B88 is the slot table, bit 7 set = free, else the owner's $6D. Fills $1700 (OBJ template base = slot <<
// 5) and $0D00 (OAM tile base = ((slot & $FE) << 4) + ((slot & 1) << 3)), the same on both paths. Class 0 takes one
// slot scanning $19 down to $01 (slot 0 is never handed out); other classes a consecutive pair scanning $0C up to $16.
// Alloc returns 1 = SEC (granted), 0 = CLC (table full, nothing written) and -1 for the states the $6FA7 table sends
// to an RTS (only states 3 and 4 allocate; the others leave the carry untouched: no verdict). Free returns 1 = SEC
// (released) / 0 = CLC (not this object's slot); it never consults the class and frees the pair iff the neighbour is
// also ours.
int CtObjSlotAlloc(uint8_t* ram, int idx);
int CtObjSlotFree(uint8_t* ram, int idx);

// C0/AB45: the per-object screen position, called from the movement pass (C0/A838, C0/A87E, in C0/A810) and C0/B101.
// $0A00,X = (($1800,X >> 4) - camX) & $1FF with bit 8 forced on a borrow, $0A80,X likewise from $1880,X; camX =
// (($1D0A & $FF) << 3) + ($1D87 & 7), camY the same from $1D0E/$1D89. Bit 8 is the >= $100 arm of the OAM emitters'
// Y clamps, so an object left of or above the camera clamps as off-screen.
void CtObjScreenPos(uint8_t* ram, int idx);

// C0/B1B2: the VRAM upload, the consumer of the queue C0/CAD9 fills and the only clear of $09A0 (C0/CA76's whole-pass
// gate). Called from C0/B111 (the scene-entry sweep) and C0/EB8C (the NMI). Walks the queue from entry 0 ($09A0,X read
// 8-bit, a zero ends it), firing DMA channel 7 twice per entry: pair A from $0950/$0940/$0980 and pair B from
// $0970/$0960/$0990, source bank $7F, destination $2118 in mode 1 with VMAIN = $80. `vram` is a 64 KB VRAM image, or
// NULL to clear the gate without copying. Returns the number of entries flushed.
int CtAnimUploadFlush(uint8_t* ram, uint8_t* vram);

// The scene-entry init of the tables the animation tree allocates from: the C0/6F79 / C0/E935 / C0/B204 / C0/A7E9
// fills C0/56B0 runs on a scene load. $0B88..$0BA5 = $80 (30 free OBJ slots), $0BC0..$0BC7 = $80 (the arena owners),
// $0E00..$0EFF = $80 (bucket heads, out-of-list flags, prev and next), and one (00, FF) pair per object from $0F00 (the
// activation latch and the "no frame yet" sentinel). A precondition: CtObjListLink links only when $0E01,X is
// already negative, which C0/B204 sets.
void CtObjSceneInit(uint8_t* ram, int nobj);
// C0/571A-58AA: the location load's per-object default fill, written through the $2180 WRAM port. Thirteen arrays get
// their initial value per object; $1000,X = 4 clears the op-0x0B pause bit the activate guard C0/5AD1 refuses on.
// `body16` is the script's 32-byte-per-object fn-pointer table ($7F:2001), used only for the $1180,X fn0 seed; NULL
// skips it.
void CtObjSceneDefaults(uint8_t* ram, const uint8_t* body16, int nobj);

// C0/A98A and C0/A9CD: the Y-sorted sprite list's unlink and link, which bracket CtObjScreenPos in the movement pass
// (C98A / A98A / AB45 / A9CD at A832-A83B and A878-A881): an object leaves the bucket of its old screen Y before AB45
// recomputes it and re-enters by the new one. $0E00,Y (Y even) = the bucket heads, bucket = ($0A80,X >> 1) & $FE;
// $0E01,X = the out-of-the-list flag (bit 7 set = unlinked); $0E80,X/$0E81,X = prev/next; $80 = none. Each returns 1
// if it acted, 0 if there was nothing to do. A per-frame rebuild does not match: the chain order depends on insertion
// history (`ct.exe --checksort`).
int CtObjListUnlink(uint8_t* ram, int idx);
int CtObjListLink(uint8_t* ram, int idx);

#endif
