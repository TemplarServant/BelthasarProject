// ct_vm.h — the scene-script init VM in C (port of assets/script.py run_func).
// Resolves each object's actor state by interpreting its init bytecode over shared
// script memory — the same computation the Python VM does to place field actors.
#ifndef CT_VM_H
#define CT_VM_H

#include <stdint.h>

typedef struct {
  int sprite;     // -1 = none
  int x, y;       // pixel position (valid only if hasPos)
  int facing;     // 0 up, 1 down, 2 left, 3 right
  int draw;       // 0 draw, 1 hidden, 2 removed
  int dead;
  int defined;    // a load-sprite op ran
  int hasPos;
  int result;
  // scripted animation state (event ops 0xAA/AB/AC/AE/B3/B4/B7; engine.py apply_anim_op)
  int animMode;   // 0 none (idle/walk auto) | 1 loop | 2 loopcount | 3 static
  int animIndex;  // loop anim id (mode 1)
  int animIl;     // loopcount anim id (mode 2)
  int animLoops;  // loopcount remaining (mode 2; quirky CT semantics, see field tick)
  int animStatic; // frozen assembly frame (mode 3)
  int animGen;    // bumped on every anim op (viewers reset playback on change)
  int animBusy;   // a BLOCKING anime-once/repeat (0xAB/0xB7) is playing; the renderer clears
  int animOpState;// the op's per-actor visit state (ROM $7F:0B01,X): 0 idle, 2 waiting
  int animOpAddr; // body addr of the 0xAB/0xB7 that armed animOpState -- a DIFFERENT blocking anim op
                  // (e.g. fn2's stumble B7 after Marle's pacing B7 was preempted by the touch) must
                  // re-arm, not inherit the stale "waiting" state of the preempted one (= a hang).
  int animSeedFrame; // -1 none; on WRAM seed of mid-AB, $1681,X step so the renderer resumes
                     // mid-play instead of restarting the once-anim from frame 0 (loc 112 pre-D8).
  int sprPrio;    // op 0x8E sprite priority byte (bit 7 = AUTO position-based; else fixed
                  // OAM prio per half: 0/1 = behind all of BG1, 2 = under prio-1 BG, 3 = over)
  int moveAnim;   // op 0x87: animation played WHILE MOVING (0 = the default walk anim 1);
                  // the wake-up's '87 10' = the held leap pose of the slide out of bed
  int moveSpeed;  // op 0x89: $1A00 move speed (engine default 0x10). Set in the STARTUP fn (like
                  // moveAnim) -- fn0 sets it then RETURNs, so the live stepper never re-reaches it;
                  // the init runner must capture it or the NPC moves at the default (scene-13 kids).
  int moveMode;   // $1100,X per-object MOVE-MODE, set ONCE at object-LOAD by the load opcode (ROM
                  // C0/426A PC slot-match + C0/447A/4559 NPC). Stable at runtime (no move/chase/arrival
                  // op writes it). The per-frame dispatcher C0/A819 routes on it: <3 -> C0/AAFD pure
                  // smooth add (lead PC slot0=0, follower slots 1/2); 3..6 -> C0/AA07 $7F0B00 lifecycle
                  // (op-0x81=3, op-0x82=4, op-0x83=5, sprite-promote=6); 7 -> camera; 0x80 -> idle (char
                  // not in the active party / op-disable). DEFAULT 0x80 (inactive) until a load op sets it.
  int solid;      // op 0x84 sets $1B01,X (the 9923 player-collision solidity, bit 0). Default
                  // 1 (the object-place code C0/4293 forces $1B01=1); '84 00' clears it -> the
                  // player walks THROUGH the actor (the fair's wandering green-haired kid).
  int coll;       // op 0x0D -> $1C80 bit1 = OBJECT collision (C0/5B95 +0x30-once). ROM place/loaders
                  // do NOT write $1C80 (except lead PC STZ at C0/42A1); default 0 = zeroed WRAM.
                  // op 0x0D overrides (fair movers '0D 01' -> map1; cat/Mom init '0D 03' -> both).
  int collMap;    // op 0x0D -> $1C80 bit0 = MAP collision (C0/ACFD $7E7040 bit7). Default 0.
                  // A default of 1 invented map-bounce for field enemies (op-0x83) that oracle has
                  // $1C80=0 -- their pre-battle MOVETO hung (loc 112 obj14) and loc[6] never hit 4.
  int cadPause;   // $1000,X bit7 (ROM C0/6275 ORA / C0/6290 AND): 1 = cadence PAUSED. op 0x0B sets,
                  // op 0x0C clears. REQ/CALL (C0/5FD0 BMI) refuses a paused target -- so approach
                  // scripts must 0x0C-unpause battle-enemy slots before 0x02-REQ'ing their fn3.
  int snap;       // op 0x0E -> $1C81 bit0 = op-0x96 arrival SNAP-to-tile-centre (C0/305D). Set in the
                  // INIT fn (obj10/Taban's fn0 '0E 01'); copied to sc->c1C81. Without it = no snap.
  int colorFx;    // op 0x88 COLOR effect active (ROM C0/4892, $7F0B80,X != 0xFFFF): set by cmd
                  // 0x20-0x5F (allocate effect slot), cleared by cmd 0 (close). The Telepod vortex
                  // (spr 94/95) has NO palette until op 0x88 loads one, so it is invisible until set.
  int px16, py16; // the RAW 16-sub-pixel words the placement op stores into $1800/$1880,X, or -1 when
                  // the object was never placed. op 0x8B (C0/4B74) synthesises the low bytes (X := 0x80
                  // tile centre, Y := 0xFF foot) and op 0x8D (C0/4C27) stores the script's two words
                  // verbatim -- so the "-1/16 foot anchor" belongs to 0x8B's 0xFF, NOT to every setter.
                  // Carrying the words is what keeps a 0x8D-placed object on the ROM's TILE.
  int colorGated; // 1 = this object's visibility is PRODUCED by an op-0x88 color effect (the vortex):
                  // drawn only while the effect group is active. Emitter (runs op 0x88) + any object
                  // sharing its init tile (the paired effect sprite). See CtSceneCreate.
} CtVmActor;

// Interpret each object's init (slot 0) over the packed script `raw` (the decompressed
// bytecode: [count u8][count*16 u16 ptrs][body]). Fills out[0..count) and returns count
// (clamped to maxActors). `gmem` optional (0x200 global story flags) or NULL.
int CtVmResolveActors(const uint8_t* raw, int rawLen, const uint8_t* gmem, CtVmActor* out, int maxActors);

// ---- the live scene scheduler (ports engine.py tick / _step / run_tasks) ----
// Runs object init, resumes each main loop, and advances movement tasks per frame.
typedef struct CtScene CtScene;
void CtSceneSetMoverTables(const uint8_t* blob, int len);

// ---- $7E / $7F ownership (disasm/notes/wram_lifetime.md, wram_load_census.md) ----
// In the ROM these bytes are PROCESS-lifetime: a location change never wipes $7E, and only C0/000E
// does -- at reset, and from the in-game load-game path ($7F01EC==1 -> C0/0283 -> C0/02CA -> C0/000E).
// The port used to give the same 64 KB SCENE lifetime, which is why the sound mailbox $1E00-$1E03
// vanished on a field->overworld hop (the overworld has no scene) and why the party, the battle-mode
// config byte and so on had to be smuggled across a door in carry statics. A host that wants the ROM's
// lifetime creates ONE CtWram for the run and passes it to every scene.
//
//   CtWramCreate()             = C0/000E: the cold wipe + the boot seeds.
//   CtWramLocationLoad(w)      = what a LOCATION LOAD actually does to memory that survives it:
//                                the ROM's own clears, and only those. NOT a fresh zeroed image.
//   CtSceneCreateIn(w, ...)    = build a scene ON that memory. It is not cleared, and it is not freed
//                                with the scene.
// CtSceneCreate() keeps the old self-contained behaviour (its own zeroed image, freed with the scene)
// for the harness modes that build one throwaway scene and exit.
typedef struct CtWram CtWram;
CtWram*  CtWramCreate(void);
void     CtWramLocationLoad(CtWram* w);
// C0/0C9D-0CBE: apply a pending location change staged in $0112/$0114/$0116 -- saving the location
// we came from into $0105..$0109 first. `leadOff2` = the lead PC's object index * 2 (the ROM takes it
// from $0197). WRAM-level because the ROM's copy is, and because a field->overworld hop commits with
// no live CtScene. ONE implementation, two callers: the $0119 drain in CtSceneTick, and a positional
// exit whose fade reached black before that drain expired.
void     CtWramCommitLocation(CtWram* w, int leadOff2);
// The OPENED-DOOR LIST at $7F:1CC8 (sixteen 16-bit tile indices into the $7E:3000 map, $8080 = empty, packed). The
// location load resets it (C0/28AA, inside CtWramLocationLoad); a door request appends its tile (C0/28C0, called at
// C0/0D49 before the handler dispatch); and the first pipeline pass after a fresh map -- C0/286C's first JSR, and
// C0/18C4 after a battle -- re-opens every listed door in the map (C0/28E1). CtWramDoorOpen is the five door handlers'
// own $7E:3000 bumps (C0/0D4C .. 149D; returns 1 when one ran). disasm/notes/field_door_list.md.
void     CtWramDoorListAppend(CtWram* w, int tileIndex);
int      CtWramDoorListApply(CtWram* w);
int      CtWramDoorOpen(CtWram* w, int tileIndex);
uint8_t* CtWramSys(CtWram* w);                 // $7E:0000-FFFF
uint8_t* CtWramGlb(CtWram* w);                 // $7F:0000-01FF (story flags)

CtScene* CtSceneCreate(const uint8_t* raw, int rawLen, const uint8_t* gmem, int sceneNo);
CtScene* CtSceneCreateIn(CtWram* w, const uint8_t* raw, int rawLen, const uint8_t* gmem, int sceneNo);
void CtSceneTick(CtScene* sc);                 // one frame: schedule scripts + integrate moves
int CtSceneActorCount(const CtScene* sc);
// The ONE field camera (field_camera_unification.md). CtSceneCameraTick maintains the DP=$1D00 scroll
// ($1D0A/$1D0E coarse + $1D93/$1D96 phase) every frame (called first in CtSceneTick -- every VM host drives
// it): 99DE deadzone/edge gate -> 91AC op-0xE7 seek + auto -> 93E1 accumulate+commit. CameraInput hands in
// this frame's post-collision request + the lead's start-of-frame foot; CameraEntry does the C0/7506 scene-
// entry init from the arrival tile; CameraPx reads the render camera in pixels. All state in mem.s[$1D..].
void CtSceneCameraTick(CtScene* sc);
// C0/00EB, one wait of the battle return's chain (C0/881E's camera, C0/00DE's $0153 flip and FD/E39C) -- field.c.
void CtSceneReturnWaitFrame(CtScene* sc);
// C0/02AF-02C1, one frame of the defeat's fade (C0/881E + FD/FFF7).
void CtSceneDefeatFadeFrame(CtScene* sc);
void CtSceneCameraInput(CtScene* sc, int reqX, int reqY, int leadX, int leadY, int leadValid);
void CtSceneCameraEntry(CtScene* sc, int arrTileX, int arrTileY);
// C0/A385-A4FB: a location load's BG2/BG3 scroll setup from the map header md[1..3] (call after
// CtSceneCameraEntry), and the per-frame result: BG2/BG3 offsets from BG1 in pixels.
void CtSceneLayerScrollLoad(CtScene* sc, uint8_t md1, uint8_t md2, uint8_t md3);
void CtSceneLayerOffsets(const CtScene* sc, int* bg2x, int* bg2y, int* bg3x, int* bg3y);
void CtSceneLayerOffsetsOf(const CtScene* sc, const uint8_t* page, int* bg2x, int* bg2y, int* bg3x, int* bg3y);   // the same, from a copy of the object page (a battle: $7F:2000 - $0920)
// The SCENE-ENTRY OBJ palette sweep (ROM C0/2881 -> C0/B0E6). Call it on a fresh scene entry AFTER
// the lead is placed and the camera is set -- it needs both, and its box is NOT the per-frame one.
void CtScenePalSweepEntry(CtScene* sc);
void CtSceneMenuReloadObjects(CtScene* sc);   // the menu reload's C0/286C object legs: 7155 / E935 / B204 / A7E9 / B0E6
void CtSceneCameraPx(const CtScene* sc, int* camx, int* camy);
void CtSceneCameraPxOf(const uint8_t* page, int* camx, int* camy);   // the same, from a copy of the object page (a battle)

// ---- WRAM bridge for the lockstep oracle differential (--vmdiff) ----
// Maps our VM's per-object scheduler state to/from the ROM WRAM layout (0x20000):
//   $1180,X IP (raw $7F2000-rel = body addr + 1 + header), $1C00,X priority,
//   $0F00,X active, $1800/$1880,X pos, $7F0580 return stack, $7F0980 wait flag.
typedef struct { int obj; const char* field; int ours; int oracle; } CtVmDiff;
void CtSceneSeedFromWram(CtScene* sc, const uint8_t* wram);   // seed scheduler state from an oracle WRAM image
int  CtSceneScriptDiffWram(const CtScene* sc, const uint8_t* wram, int* firstDiff); // raw vs $7F2000; 0 = identical
int  CtSceneDiffWram(CtScene* sc, const uint8_t* wram, int withPos, CtVmDiff* out, int cap); // per-obj divergences
// VMDIFF_DRAW coverage: objects the draw compare grades (C0/B0F3's class guard passes) out of nobj.
int  CtSceneDrawGraded(const CtScene* sc, const uint8_t* wram, int* total);
// Object-solid scan: destination-tile candidates seen, and how many the ROM's $0F00 test and the
// retired `draw` test decide differently (the ungraded line in npcSolidTile).
// C0/5B95's gate chain, the ONE copy (--sweepsolid micro-executes the ROM against these).
int  CtVmSolidMoverGate(const uint8_t* ram, int mover2);          // 5B99 $0F00 + 5BA0 $1C80 bit1
int  CtVmSolidCandGate(const uint8_t* ram, int mover2, int cand2);// 5BC3 $0F00, 5BD5 $1100.7, 5BD7 self, 5BDB $1B01.0
// C0/5BFA/5C20/5C47/5C6D: the per-heading 16px distance box on $1800/$1880 (heading = $1600,X).
int  CtVmSolidBox(const uint8_t* ram, int mover2, int cand2, int heading);
int  CtVmSolidBoxAt(int mx, int my, int cx, int cy, int carry0, int heading);  // the coordinate core
// C0/5B95's complete scan. `pos` supplies 12.4 coordinates per object index (NULL = read the
// ROM's $1800/$1880 out of `ram`). The scan RANGE is the ROM's: it starts at nobj*2, ONE PAST the
// last object (5BBD), and 5BC8's `DEX DEX / BNE` exits at 0, so index 0 is never tested.
typedef void (*CtSolidPosFn)(void* ctx, int idx, int* x12, int* y12);
int  CtVmSolidScan(const uint8_t* ram, int nobj, int mover2, int heading,
                   CtSolidPosFn pos, void* ctx);
// Liveness of the object-solid path: entries, how many the C0/5B95 head gated, how many deflected.
int  CtSceneSolidLive(int* gated, int* deflected);
int  CtSceneObjIp(const CtScene* sc, int obj, int* prio);  // our projected $1180 IP for one obj (-1 = idle)
// Per-object live state: 1 if placed (drawn), the (float-floored) pixel pos + facing.
void CtSceneActor(const CtScene* sc, int obj, int* placed, int* sprite, int* x, int* y, int* facing);
void CtSceneSetActorPos(CtScene* sc, int obj, int x, int y);  // sync an actor's pos (PC1 = the player)
void CtSceneSetActorPosF(CtScene* sc, int obj, double x, double y);  // sub-pixel variant (the per-frame lead
                                                              // sync; the ROM's $1800/$1880 keep the fraction)
// The lead's step: the host moved him from (x, y) this frame (C0/881E's step, before the tick); C0/A810 integrates it only
// on the ticket his op B0 armed this pass (C0/3040) and nothing dropped (`STZ $1A01,X`) -- else the tick takes it back --
// and a song upload that parks the pass keeps the ticket to the return frame. Dropped = the tick took the step back.
void CtSceneLeadStepFrom(CtScene* sc, double x, double y);
int  CtSceneLeadStepDropped(const CtScene* sc);
int CtSceneLeadScriptPlaced(const CtScene* sc, int obj, double* x, double* y);  // 1 = the lead's fn0 placed it
                                                              // AFTER its C0/42B8 arrival arm (fx/fy returned)
// Trail the active-party FOLLOWERS (move-mode 1/2) behind the lead PC along its path (conga-line).
// The follower is the ROM PLAYER CONTROLLER's job (every field frame), so call this AFTER CtSceneTick,
// once the lead's pos for the frame is set. No-op when the party has no roaming follower.
void CtSceneUpdateFollowers(CtScene* sc);
// C0/597D: re-run ONE object's init function (fn0) and reset its per-object script state -- the
// respawn the post-menu reconcile C0/1A03 performs on every PC object when $7E:2980-82 changed, so
// the PC loader inside fn0 re-reads the party and re-derives this object's slot ($1100,X) and sprite.
// Rewrites the $0197/$0199/$019B slot->object table for it. The caller owns positions: the ROM puts
// the snapshotted X/Y/prio back into the objects the NEW table names.
void CtSceneReinitObj(CtScene* sc, int obj);
// C0/1A03's reconcile: respawn EVERY character's PC object (fn0 re-run) in the ROM's own order
// 0,1,2,4,3,5,6, so a re-ordered $7E:2980-82 re-derives every slot -- including a character who had
// no slot before the exchange. Returns how many were respawned. Call it only when the party changed;
// the caller then restores the snapshotted positions BY SLOT through the new $0197/$0199/$019B.
int  CtSceneReconcileParty(CtScene* sc);
// battle_forest_return.md AD.5: the derived post-victory RETURN. CtSceneBattleRemoveObj dead-marks a killed
// enemy's field object (the $1000 bit7 analog: excluded from draw + reactivation). CtSceneBattleReturn is the
// resume (C0/0330 -> C0/5B1F): seeds the arrival barrier + VM-CALLs the ROM regroup script on each follower
// (walk to the lead, then its E3 01 restores control); no followers -> explore=1 directly. Call once, at the
// battle-return handoff.
void CtSceneBattleRemoveObj(CtScene* sc, int obj);
void CtSceneBattleReturn(CtScene* sc);
// C0/595C: run obj0's SLOT-1 function synchronously to its 00 terminator. The ROM has exactly three callers --
// the location load C0/56C8, the menu-exit reload chain C0/0206 and the battle-return tail C0/0330 -- each right
// after the $7F:2000 script image is (re)built. obj0's live state ($1180/$1C00/$1001/$7F0580) is untouched.
// `site` names the caller for CT_SLOT1TRACE=1 (`slot1: <site> ops 16@0032 ... end@006B`).
void CtSceneRunObj0Slot1(CtScene* sc, const char* site);
// Active-party carry across scene transitions ($7E2980/81/82 persist in the ROM). Read the live party
// before a door, stash it via CtSetCarryParty; the next CtSceneCreate seeds from it (-1 a,b,c = default).
void CtSceneGetParty(const CtScene* sc, int* a, int* b, int* c);
// ---- SOUND SINK (disasm/notes/audio_commands.md §6) -------------------------
// The field VM's six sound opcodes ($E8/$EA/$EB/$EC/$ED/$EE) do exactly two things the VM
// itself cannot: they `JSL $C7:0004` after staging a command at $7E:1E00-$1E03, and they poll
// the APU's OUT ports ($2141, $2143). Both are the sound hardware's side of the wire, so the
// VM asks for them through this hook rather than growing an audio dependency. `post` is the
// JSL -- it must be SYNCHRONOUS, because the ROM's is (two objects can stage commands in the
// same frame and each is serviced before the next is staged). `port` reads APUIO k, the
// SPC->CPU direction. With no sink installed `port` reads 0, which makes the $ED/$EE waits fall
// straight through instead of spinning forever on a machine with no APU.
typedef struct {
  void*   ctx;
  int   (*post)(void* ctx, uint8_t* mem7E, int startLine);   // JSL $C7:0004 on the staged $1E00 mailbox.
                                              // Returns what the JSL cost the parked field, frames*1024 + the
                                              // raster line it returned on (audio.c's loader clock), given the
                                              // line it began on; -1 = unpriced (no clock behind this sink).
  uint8_t (*port)(void* ctx, int k);          // read $2140+k (k = 0..3)
} CtSoundSink;
void CtSceneSetSoundSink(const CtSoundSink* sink);   // NULL detaches
// A location load's last JSLs (its init runner and obj0's slot 1), queued while the mode is on and posted by the flush in
// the load's last frame (ct_vm.c gLoadPost).
void CtSceneLoadPostsMode(int on);
int  CtSceneLoadPostsPending(void);
long CtSceneLoadPostCycles(int k);                   // the k-th pending one's object-pass cycles (g_ctLzSpriteCycles) when it ran
void CtSceneLoadPostsFlush(CtScene* sc, int count);  // the next `count` pending ones (< 0: all)
// THE RULE's infinitely fast CPU for the sound ops' JSLs (CT_ORACLE_NMION_HOLD=1 + CT_ORACLE_IFAST=1 by default): the host
// says 0 for a run whose SPC does not stand for the cartridge's (Belthasar's WRAM-seeded runs), and the VM keeps the
// real-time tail rule there.
void CtSceneSetRuleIfast(int on);
int  CtSceneMainLoopParked(const CtScene* sc);   // before the tick: the CPU is still inside a script's `JSL C7/0004` this frame
int  CtSceneFrameLegsOn(void);                // the ROM's frame legs run in the tick: $0F01,X (the object's OAM frame) is the drain's
void CtSceneSetCarrySong(int song);   // C0/1B53 location song, staged BEFORE the init pass (-1 = record declined)
int  CtSceneLoadSongPrice(void);      // the last scene load's C0/1B8B post: frames << 10 | end line (-1 = none, or no clock)
// New-game ATB-mode selector A-confirm (ROM C2/E24C): write $2990 bit6 into the scene's LIVE WRAM --
// TSB (set) when `active` (row0/ACTIVE), TRB (clear) when WAIT (row1). battle.c's boot unpack
// (CC/E387) reads this bit to derive $99E1 (0=ACTIVE/0x40=WAIT) for the Wait freeze gate (73dfa42).
void CtSceneSetAtbMode(CtScene* sc, int active);
// DEBUG/test: force `obj` to become active-party member `member` (1 or 2) -- registers the char in
// $7E2980+member and sets its move-mode -- so the conga-line follow can be exercised in Belthasar without
// playing through the join dialogue. (The real game does this via op 0xD3 + op 0x5C.)
void CtSceneDebugMakeFollower(CtScene* sc, int obj, int member, int charId);
// Held joypad for the script VM ($00F2 in CT layout: A=0x80 X=0x40 L=0x20 R=0x10 B=0x08 Y=0x04
// Sel=0x02 St=0x01), read by the op 0x34-0x39 button-branch ops (the bell minigame's "press A").
// Call once per frame before CtSceneTick; defaults to 0 (nothing held) in headless use.
void CtSceneSetButtons(CtScene* sc, uint16_t heldF2);
// ROM $01EB, the TOUCH HIT LATCH. The player's proximity scan (C0/9923, ported as
// CtCollisionActorBlock) records the object it overlapped this frame; op 0xB0's LEAD branch
// (C0/3054 -- the only caller of the touch invoke C0/3154 in the whole ROM) consumes it and yanks
// that object onto its fn2. Pass -1 for the ROM's 0x80 "nothing". Call once per frame BEFORE
// CtSceneTick: the ROM's main loop runs the player (C0/00A7) ahead of the event-VM (C0/00C4), so
// the VM reads a value produced earlier in the same frame.
void CtSceneSetTouchHit(CtScene* sc, int obj);

// ---- WRITE-MASK BYTE COMPARE (the zelda3 VerifySnapshotsEq idea, adapted) ----------------
// zelda3 memcmp's all 128KB of WRAM every frame against an explicit "don't care" list. We
// cannot: our port models only a SUBSET of WRAM, so most bytes are simply unwritten by us.
// Instead we compare exactly the bytes WE CLAIM TO OWN -- every address the port wrote since
// the seed -- which is self-maintaining and needs no curated field list.
//
// THE MASK IS TAKEN TWO WAYS, and it must be both:
//   (1) a hook inside w8(), and
//   (2) a per-frame SHADOW DIFF of the whole modelled image (snapshot pre-tick, XOR post-tick).
// (2) is not optional. w8() is only 148 of the write sites in ct_vm.c; ~100 more are raw
// `m->s[...] =` stores, and they cover the entire unified camera, $011F, $0117, $0138 and the
// op-0xD8 staging bytes -- the mechanisms of the most recent RE work. Every other translation
// unit writes through the raw pointer CtSceneMem7E() hands out. A w8-only mask is blind to all
// of it. The shadow diff observes EFFECTS, not call sites, so it has no such blind spot.
//
// The mask is BIT-granular: w8 claims 0xFF (a whole-byte store is a whole claim), the shadow
// diff claims only the bits that actually changed. Byte granularity produces false positives
// on bytes the ROM and the port each own different bits of.
//
// STRICTLY ADDITIVE -- it can never replace CtSceneDiffWram. The port's scheduler state (the
// IP $1180, priority $1C00, facing $1600, mover $1A00/$1A01) lives in C struct fields and is
// NEVER written to Mem, so those score zero mask bits forever. They are also the only fields
// the existing pinned lockstep checks grade. Deleting the curated list in favour of the mask
// would silently drop the project's only oracle-graded signal.
//
// SCOPE CEILING, stated because a green run here is easy to over-read: ct.exe does not link
// field.c/battle.c/menu.c, so this grades the field VM's Mem only -- NOT the battle staging
// ($019D/$01A3) or the battle boot ($AEC6). Those need a different host.
typedef struct {
  uint32_t addr;              // 24-bit CPU address ($7E.... or $7F0000-$7F03FF)
  uint8_t mask, ours, oracle, seed;
  int frame;                  // the frame it FIRST diverged
} CtVmByteDiff;

void CtVmWmaskEnable(int on);                                 // <0 = read VMDIFF_WMASK
int  CtVmWmaskOn(void);
void CtVmWmaskReset(const CtScene* sc, const uint8_t* w);     // call AFTER CtSceneSeedFromWram
void CtVmWmaskExclude(uint32_t addr, int len);                // emitted BY the injecting code
void CtVmWmaskFramePre(const CtScene* sc);                    // AFTER any harness injection
void CtVmWmaskFramePost(const CtScene* sc, const uint8_t* w);
int  CtSceneDiffWramMasked(const CtScene* sc, const uint8_t* w, int frame,
                           CtVmByteDiff* out, int cap);       // latching: each addr once
// ---- the SAME four, addressed by the WRAM instead of by a scene ----------------------------
// The mask tracks $7E/$7F BYTES, and those belong to the host CtWram for the whole process
// (see the ownership note above); a CtScene is merely one way to reach the pointer --
// CtSceneCreateIn stores the CtWram straight into sc->mem, so sc->mem IS the CtWram.
// Taking the wram directly is what lets a lockstep run keep grading across a hop that has NO
// scene at all: Belthasar frees the field scene when it enters the overworld, and every
// scene-addressed call above then goes quiet on a NULL and grades nothing while the run
// carries on. Identical results in field mode: same buffers, same bits.
void CtVmWmaskResetW(CtWram* w7e, const uint8_t* w);
void CtVmWmaskFramePreW(CtWram* w7e);
void CtVmWmaskFramePostW(CtWram* w7e, const uint8_t* w);
int  CtSceneDiffWramMaskedW(CtWram* w7e, const uint8_t* w, int frame,
                            CtVmByteDiff* out, int cap);
void CtVmWmaskStats(int* masked, int* excluded, int* oracleChanged, int* unmodelled, int* dropped);
void CtVmWmaskCensusReport(int toStdout);   // PHASE F: the unmodelled bytes, folded into runs
void CtSceneSetTileSolid(CtScene* sc, int (*fn)(void*, int, int), void* ctx);  // NPC map-collision hook (ACFD walls)
int CtScenePlayerLoaded(const CtScene* sc);              // 0 = the scene loads NO PC ($0197 stays the ROM's
                                                         // 0x80 "none" sentinel): draw no player at all.
// The one-byte PC loaders' op -> char-id map (C0:5D6E table + each handler's immediate). Exposed so
// --vm can print it: comparing the engine against a reference built from the SAME map cannot catch a
// shared error.
int CtScenePcLoaderId(int op);
int CtScenePlayerObj(const CtScene* sc);                 // index of the lead-PC object (op 0x57 LOADPC slot 0);
                                                          // the engine renders THIS as the player (px/py).
                                                          // Telepod = obj3 (obj1 empty); most scenes = obj1
int CtSceneActorActivatable(const CtScene* sc, int obj);  // 1 = positioned + VISIBLE + real fn1 (sprite optional)
// C0/1CFC: the ACTION-button probe. Scans objects HIGH -> LOW and returns the FIRST one inside the
// faced 28 x 12 px box (12.4 limits #01C0 / #00C0), or -1. `facing` = the player's $1600 byte.
int CtSceneActionProbe(const CtScene* sc, int playerObj, int facing);
int CtVmActBoxAt(int mx, int my, int cx, int cy, int carry0, int facing);   // one handler, for tests
int CtSceneActorTouchable(const CtScene* sc, int obj);    // 1 = positioned + VISIBLE + real fn2 (touch); fires
                                                          // sprite-less-but-visible triggers (the Telepod demo obj12)
int CtSceneActorHidden(const CtScene* sc, int obj);      // 1 = a real positioned sprite EXPLICITLY hidden (op 0x91/0x7E)
int CtSceneActorMoving(const CtScene* sc, int obj);   // 1 if the actor has an active move task (walk vs idle)
int CtSceneActorVecMove(const CtScene* sc, int obj);  // 1 if the active move is a vector slide (op 0x9C/0x9D):
                                                      // the actor keeps its idle pose (no walk cycle)
int CtSceneActorSolid(const CtScene* sc, int obj);    // 1 if the actor blocks the player (9923 $1B01,X bit0)
void CtSceneActivate(CtScene* sc, int obj);           // run obj's activate (slot 1) at priority 1 (talk)
// The INVOKE PROLOGUE both C0/5AC5 (activate, CMP #02) and C0/3154 (touch, CMP #03) open with -- the
// ONE copy: 1 = the invoke is accepted. `w` = the flat $7E image (NULL tests the priority alone);
// `prio` is the object's live script priority, which stands in for $1C00,X because this engine does
// not maintain that byte (see the comment on the definition); `minPrio` is the ROM's immediate.
// Graded against BOTH micro-executed ROM entries by `ct.exe --sweepact`.
int CtVmInvokeGate(const uint8_t* w, int obj, int prio, int minPrio);
// Textbox state: returns 1 if a dialogue box is up, with its string-table file-addr + index
// (decode via CtAssetString(CT_ASSET_DIALOGUE, *tableAddr, *strIndex, ...)).
int CtSceneTextbox(const CtScene* sc, int* tableAddr, int* strIndex);
// 1 while a DISMISSED box is still parking the event VM (the ROM's $0129 close ramp, 12 ticks). The
// host draws the window ramping out during this; the script resumes only at its end.
int CtSceneTextboxUnparking(const CtScene* sc);
void CtSceneAdvance(CtScene* sc);              // dismiss the current textbox (player confirmed)
void CtSceneTextboxRevealDone(CtScene* sc, int pendingCode);  // typewriter drew the page's last char: 6 -> 7 (C0/21D8)
void CtSceneTextboxPause(CtScene* sc, int n);            // the typewriter met `03 n` (C0/21A8): a timed pause, or `03 00`'s close
int  CtSceneTextboxResumeArmed(const CtScene* sc);       // a pause ran out THIS frame (C0/2096 RTL): no typing until the next
                                              // pendingCode -> $0215: non-zero = another page follows (C0/2024)
void CtSceneDecisionInput(CtScene* sc, int aPressed, int upPressed, int downPressed);  // C0/1ADF: the
                                              // decision box's cursor arm; owns the input frame while $0162 != 0
void CtSceneTextboxPage(CtScene* sc);          // a page advance from state 7 (C0/2086): 8 -> ... -> 6
void CtSceneTextboxLineStep(CtScene* sc, int code);  // C0/2126's head: the pending code steps the line cursor $0131
int  CtSceneTextboxChoice(const CtScene* sc);  // 1 if the open box is a decision/choice box
int  CtSceneTextboxOptLines(const CtScene* sc, int* first, int* last);  // decision-box option-line range (op2)
int  CtSceneTextboxOp(const CtScene* sc);      // the opening opcode: 0xBB/0xC0 auto-position,
                                               // 0xC1/0xC3 top, 0xC2/0xC4 bottom
int  CtSceneTextboxObj(const CtScene* sc);     // the SPEAKER (object that opened the box)
// Active-party char-id at slot 0/1/2 (ROM $7E:2980/81/82); 0x80 = empty slot. Read by the
// dialogue name-substitution codes 0x1B-0x1D (party member 1/2/3).
int  CtSceneParty(const CtScene* sc, int slot);
// Name-entry screen (event op 0xC8, operand 0xC0+char-id): returns the char-id whose name the
// player should enter, or -1 if no screen is up. The script blocks until CtSceneNameDone.
int  CtSceneNaming(const CtScene* sc);
void CtSceneNameDone(CtScene* sc);             // confirm: resume the blocked 0xC8 op next tick
// The LOAD / NEW-GAME shell (event op 0xC8 with an ODD operand < $80 -> the launcher's C0/0D00 arm =
// menu MODE 0 = screen $0E). Returns the raw operand, or -1 when no shell is up; the script blocks
// exactly as it does for naming. In the ROM it never unblocks -- both of the shell's exits are a
// `JML $C0:0000` reboot -- so the request stands until the host performs that reboot.
int  CtSceneLoader(const CtScene* sc);
// The SHOP (event op 0xC8 with an operand $80-$BF -> the launcher's C0/0D09 arm = menu MODE 2 = screen $0C; the shop
// number is operand & $3F). Returns the raw operand, or -1 when no shop is up. The script blocks until the host calls
// CtSceneShopDone (the shop closed and the field resumed), then steps over the op -- C0/3757's done arm.
int  CtSceneShop(const CtScene* sc);
int  CtSceneSavePoint(const CtScene* sc);   // the same park with an even operand below $80: mode 6, screen $0B
int  CtSceneExchange(const CtScene* sc);    // ...and operand $00: mode 5, screen $07, the party exchange (1 = parked)
void CtSceneShopDone(CtScene* sc);
// A $C3 SPECIAL SEQUENCE (event op 0xFF with an operand < $90 -- C0/3FBB fades, C0/3FD6 resets the
// stack and C0/3FE0 `JML $C3:0000`). Returns the sequence id (2 = the title screen) or -1. The
// script is PARKED FOREVER: in the ROM the sequence ends by rebooting into C0/0000, so the host
// performs that reboot and the parked scene is thrown away with it.
int  CtSceneSequence(const CtScene* sc);
void CtSceneSequenceDone(CtScene* sc);
// op 0xFF operand $80-$8F: the bank-$FD gate warp. Parked like a sequence, but it is a different
// machine -- it ends in the C0/0000 reboot that loads the location the preceding DD/DE staged.
int  CtSceneGateWarp(const CtScene* sc);
void CtSceneGateWarpDone(CtScene* sc);
// op 0xFF < $90 (either target) found $0119 lit: the op took the first `DEC $19` of its own fade loop (C0/3FC1) in its
// frame, and the host owes the rest -- one DEC a frame until 0, the jump on the frame after.
int  CtSceneFfFading(const CtScene* sc);
// 1 while a parked op 0xC8 (any mode), op 0xFF < $90 or op 0xDF/0xE1 has left the field main loop C0/0097 -- the
// loop that alone polls the X button (C0/00A1 `JSR C0/18D9`).
int  CtSceneOffMainLoop(const CtScene* sc);
// op 0xDF/0xE1 (C0/3B98/3B4B): the location is staged in $0112/$0114/$0116 and the machine spins on $4210 until
// the vblank, then reboots through C0/0000. 1 = the host owes that reboot; Done marks it taken. The scene's main
// loop is dead from the op on (CtSceneTick does nothing), and the reboot throws the scene away.
int  CtSceneReboot(const CtScene* sc);
int  CtSceneColdReset(const CtScene* sc);
void CtSceneRebootDone(CtScene* sc);
const char* CtGetName(int id);                 // current name text (ASCII) for char-id 0-7
void CtSetName(int id, const char* name);      // set char-id's name (<=5 chars) -- the naming screen
// Resolve the dialogue character-NAME variables: expand the name-substitution markers
// ('\x01'+code, emitted by assets/text.py for the CT control codes 0x13-0x20) into the
// current name text (ASCII), exactly like the ROM's text decoder copies them from the RAM
// name buffers at $7E:2C23 (C2/5B15). Party codes 0x1B-0x1D resolve through `sc`'s active
// party (pass NULL for the new-game default = Crono only). Other bytes copy through verbatim.
// Returns the written length; `out` is NUL-terminated. Safe in place-free use (in != out).
int  CtResolveNames(const char* in, int len, char* out, int cap, const CtScene* sc);
// Scripted animation state of actor i (see CtVmActor.anim*); returns animGen.
int  CtSceneActorAnim(const CtScene* sc, int i, int* mode, int* index, int* il, int* loops, int* staticFrame);
void CtSceneSetActorAnim(CtScene* sc, int i, int mode, int index);  // controller stamps the lead's $1680 (idle=0)
int  CtScenePollTileCopy(CtScene* sc, int out[7]); // drain scripted 0xE4/0xE5 map tile copies
#define CT_TC_RENDER 0x100   // out[6] flag: the copy reaches the picture only (op 0xE5's re-upload, C0/179A's styles)
#define CT_TC_COLL   0x200   // out[6] flag: the copy reaches the collision planes only (C0/1790's AF4E on WRAM)
// THE $0117 BIT-5 SERVICE, C0/1770 -- what kind of main-loop frame the coming one is. NORMAL runs every leg; ENTER is
// the dispatch's frame (C0/0097, C0/18D9 and C0/0C76's fades, then C0/1770: 885A + C0/00BF, no 881E/1AAC/1F87/21E1);
// ALIGN a further align-loop frame (885A + C0/00BF only); STYLE a frame of C0/179A's branch (C0/00DE only); RESUME the
// frame the branch returns into (C0/00A7-00BA: 881E, 1AAC, 1F87, 21E1, 274D, 00BF). CtSceneXferBegin latches it for
// the frame -- a host whose own legs run before CtSceneTick calls it first, since the ROM dispatches before 881E.
enum { CT_XF_NORMAL = 0, CT_XF_ENTER, CT_XF_ALIGN, CT_XF_STYLE, CT_XF_RESUME };
int  CtSceneXferFrame(const CtScene* sc);          // the coming frame's kind (pure)
int  CtSceneXferBegin(CtScene* sc);                // ...latched for this frame
// C0/0C76's view of $0117 at C0/00A4, the frame's head: a host whose player controller (C0/881E, which stages positional
// exits) runs before CtSceneTick latches the byte first, so the tick's C0/0C8A drain sees what the ROM's head saw.
void CtSceneHeadLatch(CtScene* sc);
// C0/0C9A-0CC4: commit the staged change ($0112-$0116 -> $0100-$0104, the origin -> $0105-$0109) and leave the main loop
// (the scene's CtSceneTick does nothing more; CtScenePollLocChange reports the destination). Once per scene.
void CtSceneExitCommit(CtScene* sc);
// C0/EAD1, the field NMI's first branch, on the $46 the frame left: 1 = a tilemap upload takes the NMI (`BRL EC15`) and
// C0/EB79-EC13 are skipped -- the text line (6ECB), the CGRAM (712E), the OAM (ECA3), the VRAM queue (B1B2), the edges.
int  CtFieldNmiTilemapArm(uint8_t v46);
// Drain a staged event ChangeLocation (op 0xDD-0xE1, e.g. the Telepod's obj11 -> 600 A.D.):
// returns 1 once, with the destination location id, tile x/y, facing (0-3), and transition
// style (0 normal, 1 = the 0xDE warp). The field carries the story flags + fades into the load.
int  CtScenePollLocChange(CtScene* sc, int* loc, int* x, int* y, int* facing, int* style);
void CtSceneTouch(CtScene* sc, int obj);           // fire the TOUCH slot (walk-into; battles)
// TEST SURFACE (CT_CALLFN): issue the same request op 0x02..0x07 issues -- (target, fn slot,
// priority) straight into the scheduler's request path. Exists because fn slots 3..15 are
// reachable only from another object's call op, which left 40 real opcode sites ungradable.
void CtSceneRequestFn(CtScene* sc, int obj, int slot, int prio);
void CtSceneActorSetFacing(CtScene* sc, int i, int facing);
int  CtSceneExplore(const CtScene* sc);            // 0 = control locked (op 0xE3 00)
void CtSceneSetLoadFadeIn(CtScene* sc, int on);   // 1 = this frame is a C0/2848 fade-in ramp frame,
                                                  // not a C0/0097 main-loop frame (see CtScene.loadFadeIn)
int  CtSceneBright(const CtScene* sc);             // op 0xF0 screen fade level 0..15 (15 = full)
void CtSceneSyncBright(CtScene* sc);               // re-read it from $0119 (after the host's C0/2848 INC)
int  CtSceneActorMoveAnim(const CtScene* sc, int i);   // op 0x87 move-animation override (0 = walk)
int  CtSceneActorPrio(const CtScene* sc, int i);   // the 0x8E priority byte (0x80 = AUTO)
int  CtSceneActorPalRot(const CtScene* sc, int obj, int* start, int* span, int* phase); // op-0x88 swirl palette rotation
int  CtSceneActorPalBlob(const CtScene* sc, int obj, int* start, uint16_t* bgr);  // op-0x88 blob palette overwrite (contour); returns count
int CtSceneActorAtB0(const CtScene* sc, int i);   // obj i's last op was 0xB0 (C0/3002 -> C0/9E84 for the lead)
void CtIrisCircleImage(uint8_t* w128, uint8_t arg);   // C3/0077 (the iris window table) on a flat 128 KB image -- ct.exe --sweepiris
void CtPolyQuadImage(uint8_t* w128);                  // C3/0EFA (the quad window table) on a flat 128 KB image -- ct.exe --sweeppoly
int  CtSceneActorColorGated(const CtScene* sc, int obj);  // 1 = an op-0x88 colour-gated sprite (Telepod swirl) — clip it to the iris
int  CtSceneActorScripted(const CtScene* sc, int i); // 1 = a called/scripted fn is running
int  CtSceneActorMode(const CtScene* sc, int i);     // $1100 move-mode probe (debug)
int  CtSceneSaveState(const CtScene* sc, const char* path, const double player[4]);  // DEBUG: snapshot the scene + the field player state {px,py,pz,pfac} (Belthasar F5)
int  CtSceneLoadState(CtScene* sc, const char* path, double player[4]);  // DEBUG: restore (F9); same-scene only. 0=fail, 1=VM only (old/no player), 2=VM+player[] restored
int  CtSceneStateLoc(const char* path, uint8_t* flags);  // DEBUG: the location ($0100) a snapshot was taken in (+ its $7F:0000 page into flags[0x200] if non-NULL); -1 if not loadable
void CtSceneAnimFinished(CtScene* sc, int i);       // renderer: the once-anim of actor i ended
void CtSceneChoose(CtScene* sc, int option);   // pick option N: set the owner's `result` (op 0x1A) + dismiss
void CtSceneGetFlags(const CtScene* sc, uint8_t* out0x200);  // copy the live $7F0000 story flags (door carry)
// ---- battle transfer (op 0xD8 / the C0/1770 consumer) ----
// The VM's system memory IS the engine's $7E image (seeded wholesale by CtSceneSeedFromWram).
// The field frame tick's $0117 transfer consumer (field.c, mirroring ROM C0/1770-18D6) and the
// battle module (battle.c) read/write the staged interface ($019D list, $29B0 PC block, $29BF
// enemy slots, $2989/$2A1F config, $0102-4 respawn cell) directly in this image, exactly like
// the ROM routines do. Glb = the $7F0000 page (0x200 bytes; $7F01EC battle outcome = glb[0x1EC]).
uint8_t* CtSceneMem7E(CtScene* sc);
void CtSceneScriptImage(CtScene* sc);   // C0/56D4 after a battle: the script image back at $7F:2000
void CtSceneArrivalPriorities(CtScene* sc, int onMap, uint8_t z6a, uint8_t p6e, uint8_t p6f);   // C0/9DC3's WRAM half
uint8_t* CtSceneMemGlb(CtScene* sc);
uint8_t* CtSceneMemLoc(CtScene* sc);   // the $7F0200 SCRIPT-LOCALS page (0x200 bytes)
// op-0x83 LOADMON $1201,X source: the field enemy table $E4/F600 (10 bytes/record; byte 4 ->
// $1201,X, ROM C0/4762-476A). Optional -- without it a FRESH op-0x83 load leaves $1201 as-is
// (a WRAM-seeded scene already carries the oracle's value). 256*10 bytes.
void CtSceneSetEnemyRecTable(const uint8_t* tab, int len);
// The full 4MB ROM image for the load-op gfx/palette allocator mirrors (ops 0x81/0x82/0x83:
// sprite meta ptr tables $E4:FFE0, NPC headers $E4:F000, enemy records $E4:F600, LZ gfx blocks).
// Optional like the rec table -- without it fresh loads leave $1280/$1380/$1400../$0F81 zero
// exactly as before (WRAM-seeded scenes carry the oracle's values regardless).
void CtSceneSetRomImage(const uint8_t* rom, long len);
void CtHdmaTableBuild(uint8_t* wram128);   // `JSL FD/C2C1`: the field's HDMA table builder (src/hdma_table.c), $0153 bit 0 flips
void CtSceneFree(CtScene* sc);

#endif
