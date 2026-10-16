// move.h — the CT field actor movement tick (the move-step handler). Ported from the 0xA0 step-issue
// handler C0/5129 and the velocity+priority build C0/ACFD..AE25 (see disasm/notes/field_tick.md).
#ifndef CT_MOVE_H
#define CT_MOVE_H

#include <stdint.h>

// Port of the field move-toward-target step-issue handler (event-script opcode 0xA0, C0/5129):
//  - if the move counter $1A01[idx] is still running, yields (returns 0, no state change);
//  - else if the actor's current tile ($1801/$1881 = world hi-bytes) has reached the script target
//    ($7F2001[$1180[idx]+1/+2]), returns 1 (move complete -> the caller advances the script IP);
//  - else (re)issues a step toward the target: heading $EE (atan2 C0/ABA2), velocity $1900/$1980
//    (speed $1A00 * sin/cos, C0/ACFD), OBJ priority $0C00/$0C01 (C0/AE0D/ADD7), and reloads the
//    counter $1A01 from the per-actor move-duration $1000[idx] (+ mirror $7F0B00), returning 0.
// Writes those fields in `ram` (WRAM image: $7E.. at offset 0, $7F.. at 0x10000). `rom` = the raw
// HiROM image (for the $F300/$F700 atan2 + $F800 sin tables). Collision (C0/5B95) is dormant for
// field NPCs, so its +0x30 heading nudge is a no-op here.
int CtActorMoveStep(uint8_t* ram, const uint8_t* rom, int idx);

// TEST HOOK for --sweepsnap: C0/305D, the arrival snap C0/5129 runs when `$1C81,X` bit0 is set.
// Zeroes both velocities, then walks ONE PIXEL PER FRAME toward the tile's X centre and Y bottom,
// arming $1A01 = 1. Returns 1 = SEC (still snapping) / 0 = CLC (already there). Exposed only so the
// sweep can micro-execute the real routine against it; the engine reaches it via CtActorMoveStep.
int ctSweepSnapCall(uint8_t* ram, int idx);

// Port of the state-3..6 actor position integration C0/AA07 (the move loop's A82A branch). For
// $1B80==0 actors it applies the $7F0B00,X tile-CLAMP: a move that would cross a tile boundary on a
// non-first step frame instead stops the counter and pauses 1 frame (then crosses on the next step's
// first frame). Use this for state-3..6 actors instead of the simple AAFD add; closes the --portrun
// tile-crossing residual. Writes $1800/$1880 + $1A01 (+ $1980/$1981 for the $1B80!=0 path).
void CtActorIntegrate(uint8_t* ram, int idx);

// Port of the field event-script RUNNER (C0/5A93): runs actor `idx`'s script tick. Fetches the
// opcode at $7F2001 + $1180[idx], dispatches it, and loops up to the op-budget ($68, field = 5)
// while a handler returns "continue" (carry SET = advance the IP and run the next opcode);
// "yield" (carry CLEAR) ends the tick. The post-tick IP is written back to $1180[idx]. Implements
// the opcodes the field NPC scripts use (0xA0 move-to-tile via CtActorMoveStep, 0xA6 set-facing,
// 0xB1/0xB2 stay); any other opcode is a trap. Returns -1 if every opcode this tick was handled,
// else the first UNIMPLEMENTED opcode (and stops there) -- so the caller can see what to port next.
// C0/9E84 -- the PC's FACING selector, called from the HEAD of C0/9E29. Indexes a 64-entry WORD
// table at $C0:9FF7 from the two velocity deltas ($1D2C/$1D2D) and dispatches to 16 handlers: 8
// CARDINAL (unconditional facing) and 8 DIAGONAL (keep the current facing when it is already one
// of the diagonal's two components, else force the first), split between a WALK tail (C0/A171,
// mode 1) and a RUN tail (C0/A18C, mode 6 -- or mode $17 when you asked to run and did not move).
// The $9FF2 default is the IDLE chain (C0/A1E9): stand / mode $19 / idle BLINK $21, the last
// gated on a $C0:$FE00 ROM table indexed by the free-running counter $01F8. Every tail shares one
// CMP-guarded body: an UNCHANGED mode resets neither phase nor timer.
// Writes $1600, $1680, $1681, $1601, $0158, $0159, $01E5, $01F8 and $01DB. Graded by --sweepanim.
// Full derivation: disasm/notes/player_anim_9e84.md.
void CtPlayerAnimSelect(uint8_t* ram, const uint8_t* rom, int romLen, int idx);
// The `JSR ($9FF7,X)` handler dispatch on its own (C0/9ECD, and the followers' C0/9F1C/9F6B/9FCF): `ix` is
// the already-folded, even table index; y = the object index (obj*2). Writes $1600 and the anim tails'
// bytes ($1680/$1681/$1601/$0158/$0159/$01E5/$01F8). CtAnimModeSet = the shared CMP-guarded mode store
// (C0/A175 body) for the arms that reach a tail by name (A254, mode $1B).
void CtAnimDispatch9FF7(uint8_t* ram, const uint8_t* rom, int romLen, int y, int ix);
void CtAnimModeSet(uint8_t* ram, int y, int k);

// VM op 0x02 (C0/5FB6) -- the PRIORITY CALL, the only ported op that writes ANOTHER object's
// state. Operand 1 is the target object, operand 2 packs the new priority in bits 7..3 and the
// script-function slot in bits 3..0. Three reject gates ($1C01,X != 0 / $1100,X bit7 / $1000,X
// bit7) and four arms; EVERY one exits IP+3 with carry SET. Returns the new IP and writes the
// carry through `carry`. Graded by --sweepop02 -- NOT by --portrun, which cannot reach this
// opcode at all (see the sweep's own comment). Full derivation in the sweepop02 check notes.
// C0/A88D -- the OBJECT ACTIVATION GATE. A RECOMPUTED predicate, not a stored flag: state bit7
// clear, $1A81,X in 1..$7F, and the object's $1801/$1881 (doubled, 8-bit) inside the camera
// window ($1D0A-4, $1D0C+4, $1D0E-2, $1D10+5, each SATURATING). Writes that window to
// $01C3-$01C6 and the latch to $0F00,X, and returns the ROM's carry -- which is what gates
// C0/C98A, A98A, AB45 and A9CD in BOTH arms of C0/A810 (A82D and A873).
// The rising-edge one-shot, the four sub-calls and the falling-edge tails are NOT included;
// see the long note in move.c. Graded by --sweepa88d.
int CtObjActiveGate(uint8_t* ram, int idx);

// C0/3154 -- the PLAYER'S TOUCH REDIRECT, reached from op 0xB0's state-0 tail when C0/5B8D's
// carry says $01EB holds a real object index (bit 7 clear) rather than the $80 sentinel. It is
// op 0x02's CALL arm with the target taken from the touch latch and both the new priority and
// the function slot fixed at 2. Writes $1180/$1181, $1C00, $1A80, $1A01 and $1001 on the TOUCHED
// object, and saves its live IP to $7F0580 + (curPrio*128 + idx). The C0/31B1 tail is NOT
// included. Graded by --sweeptouch.
void CtVmTouchRedirect(uint8_t* ram);

// THE PORT-SIDE MIRROR OF --portrun's DISP PROBE. The harness can hook the ROM's dispatch at
// C0/5AB1 and print (frame, opcode, $0167, $0168); without the same view of the PORT's loop a
// divergence in the script IP cannot be told apart into "wrong handler" vs "wrong budget" vs
// "wrong number of visits" -- which is exactly the ambiguity that made the f253 residual opaque.
// Set g_ctVmTraceIdx to an object index (obj*2) to print CtFieldRunner's own sequence; -1 = off.
// g_ctVmTraceFrame is stamped by the caller purely so the two traces can be read side by side.
extern int g_ctVmTraceIdx;
extern int g_ctVmTraceFrame;

// THE ONE INPUT THE FIELD VM TAKES FROM OUTSIDE WRAM. Op 0xEE (C0/3CC5) does `LDA $2143` -- the
// SPC700's OUTPUT mailbox port 3 -- and parks its own IP until the low nibble reads zero. $2143 is
// a hardware register, not $7E/$7F, so the flat r[] image cannot hold it and the host must supply
// it, exactly as CtJoypadPoll takes the joypad word from the host:
//   * ct.exe    -> the oracle's own APU (snes.c serves CPU reads of $2140-3 from apu->outPorts[adr & 3])
//   * ct_field  -> the SPC700 that src/audio.c already drives through the bank-$C7 driver upload
// A ROM-wide scan settles that it is a real handshake and not a CPU scratch byte: $2143 is written
// 30 times by the bank-$C7 Akao driver and read in the entire field engine ONLY at C0/3CC6.
extern uint8_t g_ctApuPort3;

// THE SPC700's OUTPUT MAILBOX PORT 1, read by op 0xED (C0/3CBC) and supplied the same way for the
// same reason. The same ROM-wide scan settles it is a handshake and not scratch: across the whole
// 4 MB image `LDA $2141` appears exactly TWICE -- C0/3CBD (this opcode) and C7/0A80, inside the
// driver -- while `STA $2141` appears 40 times, all of them in banks $C3 and $C7. Default 0 is the
// arm that lets the script advance, so a host that never sets it gets a no-op rather than a wedge.
extern uint8_t g_ctApuPort1;

// THE SPC700's FOUR OUTPUT PORTS ($2140-$2143 as the CPU reads them), for a script's pointer read (ops 0x48 / 0x49, C0/6792 /
// 67C7) whose 24-bit pointer names one: `48 42 21 00 nn` reads the song's marker on port 2 into a local and the script loops
// on it (scene 432 obj11, the Epoch's cockpit, before `FF 26`). The host fills them each frame from its SPC (ct_field: src/
// audio.c's outPorts, field.c before the VM's tick); a host that never does reads zeros.
extern uint8_t g_ctApuOut[4];

// C0/ACFD -- the field VELOCITY + PRIORITY builder, byte-exact (the two components through the
// hardware multiplier, the $1C80,X bit-0 gate, the $FE00 BOUNCE retry and all of the direct-page
// staging $01D9/$01DB-$01E1/$01E5). It reads the heading from $01EE rather than taking it as an
// argument, BECAUSE THE BOUNCE ARM REWRITES THAT BYTE (AE31 STA $EE) -- a parameter cannot express
// it. Callers must stage $01EE first. Graded by --sweepacfd.
void CtBuildVelocityPriority(uint8_t* ram, const uint8_t* rom, int idx);

int CtVmOp02(uint8_t* ram, int ip, int* carry);

// C0/6109 -- op 0x04, the BLOCKING priority call. Unlike 0x02 it needs the CALLER's own index:
// the $7F0980 latch that splits its fresh and poll arms is indexed by the caller, not the target.
int CtVmOp04(uint8_t* ram, int idx, int ip, int* carry);

// One opcode handler's result -- exactly what a ROM handler hands back to the runner C0/5A93:
// the new script IP (returned in X and committed at 5ABF), the carry (1 = keep spending the visit
// budget, 0 = yield -- tested at 5AB4), and ok (0 = this opcode is not ported here, so trap).
typedef struct { int ip; int carry; int ok; } CtHRet;
// Dispatch ONE opcode at `ip` for object `idx`. Returns the trapped opcode, or -1 on success.
// Exposed so --sweepops can micro-execute a single handler and grade its carry and returned IP,
// which are invisible from outside CtFieldRunner's loop.
// `romLen` bounds the ROM reads the OBJECT LOADER (op 0x82 -> CtObjLoad) makes into the $E4:F023
// descriptor table and the packet table it decompresses from; every other handler ignores it.
int CtFieldRunnerOne(uint8_t* ram, const uint8_t* rom, int romLen, int idx, int ip, CtHRet* out);

int CtFieldRunner(uint8_t* ram, const uint8_t* rom, int romLen, int idx);

// The field-script opcodes CtFieldRunnerOne ports, each with its ROM handler in bank $C0 -- the target of the runner's
// `JSR ($5D6E,X)` (C0/5AB1), entered with X = op*2 and Y = the IP. One table for --sweepops and the routine-level
// shadow (ct_shadow.c), so a handler graded on fuzzed inputs is also graded on every call the cartridge makes.
typedef struct { int op; uint16_t addr; const char* name; } CtVmOpInfo;
#define CT_VM_NOPS 211
extern const CtVmOpInfo g_ctVmOps[CT_VM_NOPS];

// C1/8003 -> `JMP C1/CFC2` -> `JSR ($D126,X)`, the SIX-LEAF inventory/gold gate every one of the
// script ops 0xC7/0xC9/0xCA/0xCB/0xCC/0xCD/0xCE calls with `LDA #leaf / JSL $C1:8003`. The table at
// $C1:D126 is, leaf for leaf: 0 = C1/CFE9 HasItem, 1 = C1/D005 AddItem, 2 = C1/D056 RemoveItem,
// 3 = C1/D086 HasGold, 4 = C1/D0A2 AddGold, 5 = C1/D0ED SubGold (6/7 = C1/AA7B / C1/1686, battle).
// The leaf runs with DB = $7E and D = $0000 (C1/CFCB-CFD6), so `$2400,X` IS $7E:2400 -- which is
// why `ram` is the flat 128 KB image with $7E at offset 0, the same one CtFieldRunnerOne takes.
// `y` is the 16-bit argument the caller staged in Y; the return is the leaf's 8-bit A.
int ctVmC1Gate(uint8_t* ram, int leaf, int y);

// The SEVEN script opcodes that call it: 0xC7/0xC9/0xCA/0xCB (items) and 0xCC/0xCD/0xCE (gold),
// ROM C0/37C7 / 3780 / 37B1 / 37E3 / 3827 / 385B / 3874. One body, TWO callers -- CtFieldRunnerOne
// here (which is what --sweepops micro-executes against the ROM, so this is where the grade comes
// from) and the live event VM's step() in ct_vm.c, which used to have no arm for the four item ops
// at all and a paraphrase for the three gold ones. `script` + `ip` because the two runners hold the
// running script in different places (see the note on the definition); everything else is a WRAM
// address and identical for both. Returns 1 if `script[ip]` is in the family, 0 if not.
int CtVmItemFamily(uint8_t* ram, const uint8_t* script, int ip, int* outIp);

// The POKE family: 0x4A/0x4B/0x4C/0x4D, ROM C0/67D7 / 67FB / 680C / 6829 -- a byte or a word (literal, or a local
// out of $7F0200) through the 24-bit pointer C0/67E3 stages. Same two callers as CtVmItemFamily; carry always SET.
// Returns 1 if `script[ip]` is in the family.
int CtVmPokeFamily(uint8_t* ram, const uint8_t* script, int ip, int* outIp);

// The SCREEN-EFFECT ops: 0xF1 (C0/3D07, the colour-math transition: $0121/$0122 COLDATA, $0BE0 CGADSUB, $0118) and
// 0xFE (C0/3F26, the eight interpolation channels at $7F:1CE8.. that C0/21E1's entry 6 consumes). Same two callers as
// CtVmPokeFamily; the IP and the carry come back in *out. Returns 1 if `script[ip]` is one of them.
int CtVmScreenFxOp(uint8_t* ram, const uint8_t* script, int ip, CtHRet* out);

// Op 0x7A, C0/4D06: the jump toward a tile -- the arm (C0/ABA2's heading, C0/AC69's velocity into $1900/$1980, the ticket
// through the divider into $1A01, $1B80 = 1 and $1B81 = the third operand for C0/AAC7's arc) and the completion. Same two
// callers as CtVmPokeFamily; `idx` = the object's X. Returns 1 if `script[ip]` is 0x7A (the IP and the carry in *out).
int CtVmOp7A(uint8_t* ram, const uint8_t* rom, const uint8_t* script, int ip, int idx, CtHRet* out);

// The FACING family: 0x0F/0x17/0x1B/0x1D/0xA6/0xA7 (self) and 0x1E/0x1F/0x25/0x26 (the object the operand names), ROM
// C0/5535..5593 -> the one tail C0/554B-5556: `$1600,X` = the facing, `$1601,X` = 0 (the walk-anim timer). Same callers
// as CtVmPokeFamily; `idx` = the running object's X, `*outTgt` = the X written; carry always CLEAR. Returns 1 if in it.
int CtVmFacingFamily(uint8_t* ram, const uint8_t* script, int ip, int idx, int* outIp, int* outTgt);

// The VARIABLE-ASSIGNMENT grid: 0x50-0x56, 0x58, 0x59, ROM C0/68C6..6A11 -- local/global/immediate copies, 8 or 16
// bits (a local word is little-endian, as the 65816 stores it). Same callers as CtVmPokeFamily; carry always SET.
// Returns 1 if `script[ip]` is in the grid.
int CtVmVarFamily(uint8_t* ram, const uint8_t* script, int ip, int* outIp);

// The POINTER READ: 0x48/0x49, ROM C0/6792 / 67C7 -- a byte or a word through a script-supplied 24-bit pointer into a
// local. Same callers as CtVmPokeFamily; carry always SET. Returns 1 if `script[ip]` is in the family.
int CtVmReadPtrFamily(uint8_t* ram, const uint8_t* script, int ip, int* outIp);

// The LOCAL INC / DEC / SET: 0x71/0x72/0x73/0x75/0x76/0x77, ROM C0/6C69..6CF0 -- 0x72 and 0x76 are SIXTEEN-bit. Same
// callers; carry always SET. Returns 1 if `script[ip]` is in the family.
int CtVmLocalIncFamily(uint8_t* ram, const uint8_t* script, int ip, int* outIp);

// The LOCAL ARITHMETIC: 0x5B/0x5E/0x5F/0x60/0x61/0x63/0x64/0x67/0x69/0x6B/0x6F, ROM C0/6A1F..6C68 (`rom` holds the
// $FF20/$FF28 mask tables 0x63/0x64 index). Same callers; carry always SET. Returns 1 if `script[ip]` is in it.
int CtVmLocalArithFamily(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int* outIp);

// The bodies the live VM stepped over (2026-10-08 census, disasm/notes/live_vm_op_gaps.md), moved out of
// CtFieldRunnerOne so both runners run the one --sweepops grades. One signature: `script` = the $7F:2001 image the IP
// indexes, `idx` = the object's X; the carry is per op, so it comes back in `out`. Each returns 1 if `script[ip]` is its.
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

// The $C2 PARTY / ROSTER / INVENTORY-COUNT family: 0xCF/0xD0/0xD1/0xD2/0xD3/0xD4/0xD7 and
// 0xF8/0xF9/0xFA, ROM C0/388D / 38BE / 38D3 / 38E8 / 3919 / 394E / 37F9 / 3E5C / 3E6E / 3E75 --
// 896 static sites, every one of them reaching `JSL $C2:8004` with a leaf index. One body, TWO
// callers, exactly like CtVmItemFamily: CtFieldRunnerOne here (which is what --sweepops
// micro-executes against the ROM, 60 cases per opcode) and the live event VM's step() in ct_vm.c,
// which had NO arm at all for seven of the ten. ⚠ The carry is NOT uniform across the family, so it
// comes back in `out` rather than being assumed. Returns 1 if `script[ip]` is in the family.
void CtVmC2Epilogue(uint8_t* ram, const uint8_t* rom, int romLen);   // C2/834D: weapon rows, triple techs, relics
void CtVmC2LoadTail(uint8_t* ram, const uint8_t* rom, int romLen, int x, int y);   // C0/56CB `LDA #0B / JSL C2/8004`: the gate's leaf 11 = the epilogue
int CtVmPartyGateFamily(uint8_t* ram, const uint8_t* rom, int romLen,
                        const uint8_t* script, int ip, CtHRet* out);

// Op 0x88, ROM C0/4892: the per-object colour effect -- restore (48C0), rotate (4919), ramp (4970) and the
// inline palette blob (49F8). One body, TWO callers like the families above: CtFieldRunnerOne (--sweepops)
// and the live VM's step() + init pass, which until now armed no record at all. `idx` = obj*2. The carry
// comes back in `out` (CLC = the slot table was full: the op is consumed and the visit yields).
void CtVmColorFx(uint8_t* ram, const uint8_t* rom, int romLen, const uint8_t* script, int ip, int idx, CtHRet* out);

// Op 0x2E, ROM C0/4A4E: the colour-effect RECORD ($40/$50: a $0520 slot, the fades FD/E82C / FD/E5A4 walk), the
// inline palette blob ($80), else a two-byte no-op. Two callers, like CtVmColorFx; `script` = the $7F:2001 base.
void CtVmColorSlotOp(uint8_t* ram, const uint8_t* script, int ip, CtHRet* out);

// Op 0xE2, ROM C0/3B9E: op 0xE0's fade-change staged from four LOCALS ($0112 = the first's word, $0114/$0115/$0116 the
// others' low bytes), the $0117 latch, $0119 = 0F, and the park on the op. Two callers, like CtVmColorSlotOp.
void CtVmChangeLocVars(uint8_t* ram, const uint8_t* script, int ip, CtHRet* out);

// The ANIMATION family: 0xAA/0xAB/0xAC/0xAE/0xB3/0xB4/0xB7, ROM C0/2E67 / 2E9A / 2F71 / 2E8F / 2E87 /
// 2E8B / 2F01 -- 10174 static sites. One body, TWO callers, like the two families above: the runner
// --sweepops grades and the live event VM's step(), whose private animMode/animBusy copy kept the
// blocking ops' completion on a renderer signal instead of $7F0B01,X (see the definition). Writes
// $1680/$1681/$1601/$1780/$1781/$1301,X and $7F0B01,X exactly as the handlers do; the carry is NOT
// uniform (0xAC and the parking arms of 0xAB/0xB7 yield), so it comes back in `out`.
// Returns 1 if `script[ip]` is in the family.
int CtVmAnimFamily(uint8_t* ram, const uint8_t* script, int ip, int idx, CtHRet* out);

// Op 0xE4, ROM C0/3D97: the IMMEDIATE scripted tile copy -- stage $013E/$0140/$0142/$0144 and $01C7, then
// `JSR C0/AF4E`, which moves the rect through the map planes in WRAM ($7E:3000/3040/3080/30C0/7000/7040 and the
// bit-6/7 merges). One body, TWO callers like the families above: CtFieldRunnerOne (--sweepops) and the live VM's
// step(), which queued only the render's copy and never touched the $7E:3000 map the ROM's readers see. Carry CLEAR,
// eight bytes: the caller continues at ip + 8.
void CtVmTileCopyE4(uint8_t* ram, const uint8_t* script, int ip);

// Op 0xE5, ROM C0/3DC5: the QUEUED tile copy -- the same staging as 0xE4 plus `$0145 = $0144 & 7` and `TSB $17` bit 5,
// and NO copy: the main loop's C0/1770 makes it a frame later (`1790 JSR C0/AF4E`). Carry CLEAR, eight bytes. Shared by
// CtFieldRunnerOne (--sweepops) and the live VM's step().
void CtVmTileStageE5(uint8_t* ram, const uint8_t* script, int ip);
// C0/AF4E alone, on the operands already staged at $013E-$0144 -- C0/1790's call.
void CtVmTileCopyAF4E(uint8_t* ram);

// C0/65F4 / 6628 / 560E / 5423 / 61F2 / 6207 / 6225 / 623B -- `LDA $97,X` with X = the script's
// operand. $0197/$0199/$019B are the three party slots' object OFFSETS (obj*2, spilled there by op
// 0x80's arms at C0/426A); bit 7 set means the slot is EMPTY. So ops 0x22/0x24/0xA9/0x95/0xB6 name
// their target by PARTY SLOT, and the operand is the already-DOUBLED slot index -- measured over the
// 504 shipped scenes it is only ever 0, 2 or 4. Shared with ct_vm.c's step() for the same reason
// CtVmItemFamily is: --sweepops micro-executes these ops against the ROM 60 cases each, and the live
// VM should INHERIT that grade rather than carry a second copy of the rule (it carried
// `operand + playerObj`, which is wrong by a factor of two on every non-zero operand).
int ctVmPartySlotTarget(const uint8_t* ram, int slot);

// Port of the per-actor animation TIMER + PHASE advance (C0/C98A, called per actor from the movement
// loop). Decrements the anim frame-timer $1601[idx]; when it reaches 0 (and the $1100 actor state is
// in the animatable class), advances the walk-phase $1681[idx] and reloads $1601 from the bank-$E4
// speed table (rom 0x240000): index = $1681 + (mod << 2) + $1580, mod = $1680 if $1780<=1 else $1781;
// if the table entry is 0, re-look-up at the phaseless base and (unless $1780==2) reset the phase to 0.
void CtAnimTimerTick(uint8_t* ram, const uint8_t* rom, int romLen, int idx);

// Port of the field anim FRAME LOOKUP (C0/CB3A + the CBDC cfg==3 short-circuit). Computes the next
// candidate sprite-frame byte from the actor's anim state: a sequence pointer $D6 from the facing
// $1600 (0 -> $1500; 2 -> $1480*2+$1500; 1/3 -> $1480*3+$1500; >=0x80 -> $1480+$1500) plus the phase
// $1681 and the per-mode modifier ($1781 if $1780==2 else $1680, <<2); reads the frame byte from the
// bank-$E4 sequence table (rom 0x240000 + $D6). $1780==3 returns the static frame $1301 directly. On
// the $FF end sentinel it wraps the phase ($1681=$FF) and returns $FF. Returns the candidate byte.
int CtFieldFrameLookup(uint8_t* ram, const uint8_t* rom, int romLen, int idx);
// C0/5614, the move family's step-issue anim selector (idle / walk / run from |$1900|+|$1980|), on the image.
void CtVmMoveStepAnim(uint8_t* ram, int idx);

// C0/30B3, the ONE-PIXEL walk-onto-the-target step the move-toward-actor body (C0/5228 and siblings)
// takes when the mover is already ADJACENT and `$1C81,X` bit1 is set -- the party JOIN's `0E 02` case,
// where the actor must not stop a tile short. Zeroes both velocities, compares the WHOLE-PIXEL parts
// (`AND #FFF0`) of self's and the target's `$1800`/`$1880` -- the target is `$E9`, which the caller must
// have stored -- and on any mismatch publishes 1 px of velocity on BOTH axes at once ($1900/$1980 = $10
// or $F0) plus `$1A01` = `$7F0B00` = 1. Returns 1 = SEC (still snapping; the caller then runs C0/5614
// and yields) / 0 = CLC (already aligned; the caller falls through to C0/568B and advances the IP).
int CtVmMoveSnap30B3(uint8_t* ram, int idx);

// The FOUR-SLOT sprite-assembly ARENA, C0/E952-EA62. `$0BC0..$0BC3` is the owner table (bit7 set =
// FREE, else the owning object's $6D = obj*2) and `$0D80,X` receives the granted base as a WORD,
// $3800 + slot*$200 -- an offset into the $7F staging bank. `n` = 1/2/3 slots (the sprite's piece
// class), and a multi-slot claim demands CONSECUTIVE slots. Alloc returns 1 = SEC (granted) /
// 0 = CLC (pool full, and NOTHING is written, which is why a full pool makes the caller drop the
// animation frame entirely). CtArenaFreeRun is C0/EA42 -- it releases the whole CONSECUTIVE RUN
// owned by the object and is NOT interchangeable with CtArenaFree(...,1).
int  CtArenaAlloc(uint8_t* ram, int idx, int n);
void CtArenaFree(uint8_t* ram, int idx, int n);
void CtArenaFreeRun(uint8_t* ram, int idx);

// Port of the animation-queue drain's per-object VISIT, C0/CAD9. C0/CA76 pops its FIFO head into
// $6D and JSRs here; the CARRY is the protocol -- SEC = "call me again with this object still at
// the head", CLC = "I am finished, pop me". Four gate tests (all CLC) then a class dispatch on
// $1201,X & 3, which is the sprite's PIECE COUNT: 0 = 4 pieces, 1 = 8, 2 = 12, 3 = never drawn.
// Returns 0 = CLC, 1 = SEC, and -1 = A CLASS THIS PORT HAS NOT IMPLEMENTED (class 2, C0/D4F7,
// which stages across THREE resumable passes). -1 is not a ROM state; it is the port refusing to
// guess, and callers must treat it as "unknown", never as a carry. Class 1 (C0/CEF5) IS ported:
// its pass 1 is the SEC producer -- resolve, guard, claim TWO slots, commit, then ask to be
// called again -- and its pass 2 neither looks up nor commits anything.
int  CtAnimDrainVisit(uint8_t* ram, const uint8_t* rom, int romLen, int idx);

// Port of C0/B701, the arena RELEASE and the draw pass's per-object gate (its only caller, C0/B309,
// reads the carry as `BCC -> draw`). One law across the three classes: with T = (class & 3) + 1 =
// the arena arity and n = $1B00,X, `(n & 0x7F) < T` means not fully staged -- reposition-emit and
// draw when bit7 is already set, otherwise SKIP; at or above T it instantiate-emits, frees T slots
// and stamps $1B00,X = 0x80. Returns 1 = SEC (skip this object's draw), 0 = CLC (draw it).
// The six OAM emitters are NOT ported; they provably touch none of the state this models.
int  CtAnimRelease(uint8_t* ram, int idx);

// Port of C0/CA76, the animation FIFO DRAIN. Pops the head into $6D and calls CtAnimDrainVisit until
// the queue empties or the raster deadline expires; SEC keeps the object at the head, CLC pops it.
// `scanline` is the 9-bit V counter the ROM assembles from two $213D reads: >= 0xF0 (vblank) means
// no limit, otherwise the pass stops once it reaches the WORD budget $6B/$6C. The port models that
// TEST exactly but cannot model the CLOCK -- the real counter advances while the drain works, so a
// fixed `scanline` drains fully or not at all. Returns the visit count, -1 if it met an unported
// sprite class, or -2 if the list did not converge in 64 visits (a port-side guard, not the ROM's).
int  CtAnimDrainPass(uint8_t* ram, const uint8_t* rom, int romLen, int scanline);

// Port of C0/B271, the DRAW PASS -- wired for the arena LIFECYCLE, not for pixels. Walks the Y-sorted
// buckets $0E00 high to low following $0E81 and calls C0/B701 (CtAnimRelease) per object, which is the
// only arena release in the engine; on its CLC it advances that object's per-group OAM cursors and
// counts it as drawn. The OAM BYTES are deliberately not emitted here -- src/oam.c's CtBuildFieldOAM is
// this port's graded model of stage 2's copy (--checkoam) -- so the buffer stays unmodelled rather than
// half-written. Returns the number of objects drawn; `unresolved` (may be NULL) receives the count whose
// gate was undecidable, which with C0/C73A ported is expected to stay ZERO for every class.
int  CtAnimDrawPass(uint8_t* ram, int* unresolved);
extern int gCtDrawSkipped;   // how many objects the last CtAnimDrawPass gate-REFUSED (B701 SEC)
// ⚠ AND NOTHING CARRIES THAT REFUSAL TO CtBuildFieldOAM, which walks the same list again and
// emits bytes for every object in it. A refused object therefore shifts every later object's OAM
// cursor by its own size. Measured on --sweepstage2 with class-3 seeds; see the note above
// CtBuildFieldOAM in src/oam.c for why neither a flag nor a second B701 call is the fix.
extern int gCtDrawClsMask;   // bitmask of the sprite classes that reached the last draw list
extern int gCtDrawRunaway;   // cumulative trips of the draw walk's non-convergence guard (should stay 0)

// Port of the field OBJECT LOADER's WRAM-field half -- C0/4557 (the shared prologue) plus
// C0/4590 (NPC, state 4) / C0/46E1 (enemy, state 5). Consumes ONE script byte at $7F2001+ip+1
// as the sprite id, walks that id's 5-byte descriptor at $E4:F023 through the four bank-$E4
// pointer tables, and fills the tables the whole C0/CAD9 animation tree reads. Returns the new
// script IP. The graphics DECOMPRESSION the ROM queues on a cache miss is NOT modelled -- see
// disasm/notes/object_loader.md; $1280 therefore holds $71 unchanged rather than a cursor that
// advances per load.
int CtObjLoad(uint8_t* ram, const uint8_t* rom, int romLen, int idx, int ip, int state);

// The OBJ SLOT allocator/release -- C0/6F9A (dispatch), C0/6FBA (one slot), C0/7008 (a
// consecutive PAIR) and C0/7056 (release). `$0B88` is the slot table, bit7 set = FREE,
// else the owning `$6D`. Fills `$1700` (OBJ template base = slot << 5) and `$0D00` (OAM
// tile base = ((slot & $FE) << 4) + ((slot & 1) << 3)); both formulas are byte-identical
// on the one-slot and pair paths. Class 0 takes ONE slot scanning $19 down to $01 (slot 0
// is never handed out); every other class takes a consecutive pair scanning $0C up to $16.
// Alloc returns 1 = SEC (granted), 0 = CLC (table full, nothing written) and -1 for the
// states the $6FA7 dispatch table sends straight to an RTS -- only states 3 and 4
// allocate, and the others leave the carry UNTOUCHED, so -1 means "no verdict", not CLC.
// Free returns 1 = SEC (released) / 0 = CLC (the slot is not this object's); it never
// consults the class, releasing the pair iff the neighbour is also ours.
int CtObjSlotAlloc(uint8_t* ram, int idx);
int CtObjSlotFree(uint8_t* ram, int idx);

// The per-object SCREEN POSITION, C0/AB45 -- called from the movement pass (C0/A838, C0/A87E, both
// inside C0/A810) and from C0/B101. `$0A00,X = (($1800,X >> 4) - camX) & $1FF` with bit8 FORCED on
// a borrow, and likewise `$0A80,X` from `$1880,X`; camX = (($1D0A & $FF) << 3) + ($1D87 & 7), camY
// the same from $1D0E/$1D89. Bit8 is what the six OAM emitters' Y clamps read as the ">= $100" arm,
// so an object left of or above the camera is clamped as off-screen rather than wrapping.
void CtObjScreenPos(uint8_t* ram, int idx);

// The Y-SORTED SPRITE LIST's unlink/link, C0/A98A and C0/A9CD -- the two calls that BRACKET
// CtObjScreenPos inside the movement pass (`C98A / A98A / AB45 / A9CD`, at A832-A83B and again at
// A878-A881). The order is load-bearing: an object must leave the bucket keyed on its OLD screen Y
// before AB45 recomputes it, and re-enter keyed on the NEW one. `$0E00,Y` (Y even) = the bucket
// heads, bucket = `($0A80,X >> 1) & $FE`; `$0E01,X` = the per-object "out of the list" flag (bit7
// set = unlinked); `$0E80,X`/`$0E81,X` = prev/next; `$80` = "none" everywhere. The draw pass walks
// $0E00 from bucket $7E down following $0E81 -- which is what CtBuildFieldOAM already does.
// Each returns 1 if it acted, 0 if the flag said there was nothing to do.
// These CANNOT be replaced by a from-scratch rebuild: `--checksort` measured 1392 head + 3780 next
// mismatches over 180/180 frames doing exactly that. The chain order is history, not index order.
// The SCENE-ENTRY INIT of every table the animation tree allocates from -- the C0/6F79 / C0/E935 /
// C0/B204 / C0/A7E9 fills that C0/56B0 runs on a scene load. $0B88..$0BA5 = $80 (30 OBJ slots, all
// free), $0BC0..$0BC7 = $80 (the arena owners -- eight cleared, four handed out), $0E00..$0EFF = $80
// (every bucket head, out-of-list flag, prev and next), and one (00, FF) pair per object from $0F00
// (the activation latch and the 'no frame yet' sentinel). THIS IS A PRECONDITION, not a nicety:
// CtObjListLink refuses to link unless $0E01,X is already negative, and C0/B204 is where that
// negative comes from.
// C0/B1B2 -- the VRAM UPLOAD: the CONSUMER of the queue C0/CAD9 fills, and the only thing in the
// engine that clears `$09A0` -- which is C0/CA76's whole-pass gate, so the drain does nothing at
// all until this has run. Called from C0/B111 (the scene-entry sweep) and C0/EB8C (the NMI).
// Walks the queue from entry 0 (`$09A0,X` read EIGHT-bit, a zero ends it) firing DMA channel 7
// twice per entry -- pair A from `$0950`/`$0940`/`$0980` and pair B from `$0970`/`$0960`/`$0990`,
// source bank $7F, destination $2118 in mode 1 with VMAIN = $80. `vram` is a 64 KB VRAM image, or
// NULL to walk the queue and clear the gate WITHOUT copying: the gate clear is the half the field
// frame needs and the copy is the half a renderer needs, and the ROM only keeps them in one
// routine to share the channel setup. Returns the number of entries flushed.
int CtAnimUploadFlush(uint8_t* ram, uint8_t* vram);

void CtObjSceneInit(uint8_t* ram, int nobj);
// C0/571A-58AA -- the location load's PER-OBJECT DEFAULT FILL, written through the $2180 WRAM port
// (so neither a DMA nor a `STA $1000,X`: invisible to both the write census and a grep). Thirteen
// arrays get their cold value per object, and `$1000,X = 4` is the one that CLEARS the op-0x0B pause
// bit the activate guard C0/5AD1 refuses on -- without it a paused object index stays untalkable for
// the rest of the session. `body16` is the script's 32-byte-per-object fn-pointer table ($7F:2001),
// used only for the `$1180,X` fn0 seed; pass NULL to skip that one. See the block comment in move.c.
void CtObjSceneDefaults(uint8_t* ram, const uint8_t* body16, int nobj);

int CtObjListUnlink(uint8_t* ram, int idx);
int CtObjListLink(uint8_t* ram, int idx);

#endif
