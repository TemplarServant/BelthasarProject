// ct_vm.h -- the field event VM: the scene script's init pass, the per-frame scheduler (C0/00BF), the
// field camera, the message box machine and the WRAM bridge to the oracle (--vmdiff).
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
  int animBusy;   // a blocking anim-once/repeat (0xAB/0xB7) is playing; the renderer clears it
  int animOpState;// the op's per-actor visit state ($7F:0B01,X): 0 idle, 2 waiting
  int animOpAddr; // body addr of the 0xAB/0xB7 that armed animOpState: a different blocking anim op
                  // re-arms instead of inheriting a preempted op's "waiting" state
  int animSeedFrame; // -1 none; on a WRAM seed taken mid-AB, the $1681,X step the renderer resumes from
  int sprPrio;    // op 0x8E sprite priority byte, the old mirror (the live byte is $0F80,X, see
                  // CtSceneActorPrio; kept for v3 snapshots)
  int moveAnim;   // op 0x87: $1000,X, the script cadence period (0 -> 1, C0/42D7)
  int moveSpeed;  // op 0x89: $1A00 move speed (default 0x10). Set in fn0, which then returns, so the
                  // init runner captures it
  int moveMode;   // $1100,X move mode, set once at object load by the load op (C0/426A PC slot match,
                  // C0/447A/4559 NPC); no move op writes it. C0/A819 dispatches on it: < 3 -> C0/AAFD
                  // smooth add (lead 0, followers 1/2); 3..6 -> C0/AA07 $7F0B00 lifecycle (op 0x81 = 3,
                  // 0x82 = 4, 0x83 = 5, sprite promote = 6); 7 -> A947 only; 8 skipped; 0x80 -> inactive
                  // (character not in the party, op 0x0A). Default 0x80 until a load op sets it.
  int solid;      // $1B01,X bit 0, the player-collision solidity (C0/9923). Default 0; the load-sprite
                  // ops (0x80-0x83) set 1, op 0x84 overrides ('84 00': the player walks through)
  int coll;       // $1C80 bit 1, object collision (C0/5B95), op 0x0D. The loaders do not write $1C80
                  // (except the lead PC's STZ at C0/42A1); default 1, the cold leftover 0x03
  int collMap;    // $1C80 bit 0, map collision (C0/ACFD, $7E7040 bit 7), op 0x0D. Default 1 (0x03);
                  // a seeded scene takes $1C80 from the seed
  int cadPause;   // $1000,X bit 7 (C0/6275 ORA / C0/6290 AND): 1 = cadence paused. Op 0x0B sets, 0x0C
                  // clears. A request (C0/5FD0 BMI) refuses a paused target.
  int snap;       // $1C81 bit 0, op 0x0E: op 0x96's arrival snap to the tile centre (C0/305D); copied to
                  // sc->c1C81
  int colorFx;    // op 0x88 colour effect active (C0/4892, $7F0B80,X != 0xFFFF): set by cmd 0x20-0x5F
                  // (allocate an effect slot), cleared by cmd 0 (close)
  int px16, py16; // the 1/16 px words the placement op stores into $1800/$1880,X, or -1 if never placed.
                  // Op 0x8B (C0/4B74) synthesises the low bytes (X := 0x80 tile centre, Y := 0xFF foot);
                  // op 0x8D (C0/4C27) stores the script's words verbatim.
  int colorGated; // 1 = visibility produced by an op 0x88 colour effect: drawn only while the effect is
                  // active. The emitter and any object on its init tile (see CtSceneCreateIn).
} CtVmActor;

// Interpret each object's init (slot 0) over the packed script `raw` (the decompressed
// bytecode: [count u8][count*16 u16 ptrs][body]). Fills out[0..count) and returns count
// (clamped to maxActors). `gmem` optional (0x200 global story flags) or NULL.
int CtVmResolveActors(const uint8_t* raw, int rawLen, const uint8_t* gmem, CtVmActor* out, int maxActors);

// ---- The live scene scheduler ----
// Runs the objects' init, then each frame the queue build and runner (C0/00BF) and the movement integrate.
typedef struct CtScene CtScene;
void CtSceneSetMoverTables(const uint8_t* blob, int len);

// ---- $7E / $7F ownership (disasm/notes/wram_lifetime.md, wram_load_census.md) ----
// In the ROM these bytes live for the whole process: a location change never wipes $7E; only C0/000E does,
// at reset and from the in-game load path ($7F01EC == 1 -> C0/0283 -> C0/02CA -> C0/000E). A host creates
// one CtWram for the run and passes it to every scene.
//   CtWramCreate()             = C0/000E: the cold wipe and the boot seeds.
//   CtWramLocationLoad(w)      = what a location load does to memory that survives it: the ROM's own
//                                clears, and only those.
//   CtSceneCreateIn(w, ...)    = build a scene on that memory; it is neither cleared nor freed with the scene.
// CtSceneCreate() builds a scene on its own zeroed image, freed with the scene (harness modes).
typedef struct CtWram CtWram;
CtWram*  CtWramCreate(void);
void     CtWramLocationLoad(CtWram* w);
// C0/0C9D-0CBE: apply a pending location change staged in $0112/$0114/$0116, first saving the origin into
// $0105..$0109. `leadOff2` = the lead PC's object index * 2 (the ROM takes it from $0197). WRAM-level because
// a field->overworld hop commits with no live CtScene. Callers: CtSceneExitCommit (the $0119 drain) and a
// positional exit whose fade reached black first.
void     CtWramCommitLocation(CtWram* w, int leadOff2);
// The opened-door list at $7F:1CC8 (sixteen 16-bit tile indices into the $7E:3000 map, $8080 = empty, packed;
// disasm/notes/field_door_list.md). The location load resets it (C0/28AA, in CtWramLocationLoad); a door request
// appends its tile (C0/28C0, called at C0/0D49); the first pipeline pass after a fresh map (C0/286C's first JSR,
// C0/18C4 after a battle) re-opens every listed door (C0/28E1). CtWramDoorOpen is the five door handlers'
// $7E:3000 bumps (C0/0D4C .. 149D); returns 1 when one ran.
void     CtWramDoorListAppend(CtWram* w, int tileIndex);
int      CtWramDoorListApply(CtWram* w);
int      CtWramDoorOpen(CtWram* w, int tileIndex);
uint8_t* CtWramSys(CtWram* w);                 // $7E:0000-FFFF
uint8_t* CtWramGlb(CtWram* w);                 // $7F:0000-01FF (story flags)

CtScene* CtSceneCreate(const uint8_t* raw, int rawLen, const uint8_t* gmem, int sceneNo);
CtScene* CtSceneCreateIn(CtWram* w, const uint8_t* raw, int rawLen, const uint8_t* gmem, int sceneNo);
void CtSceneTick(CtScene* sc);                 // one frame: schedule scripts + integrate moves
int CtSceneActorCount(const CtScene* sc);
// The field camera (disasm/notes/field_camera_unification.md). CtSceneCameraTick updates the DP=$1D00 scroll
// ($1D0A/$1D0E coarse, $1D93/$1D96 phase) every frame, first in CtSceneTick: 99DE deadzone/edge gate -> 91AC
// op 0xE7 seek + auto -> 93E1 accumulate and commit. CameraInput passes this frame's post-collision request and
// the lead's start-of-frame foot; CameraEntry is the C0/7506 scene-entry init from the arrival tile; CameraPx
// reads the render camera in pixels. All state lives in mem.s[$1D..].
void CtSceneCameraTick(CtScene* sc);
void CtSceneCameraRequestClear(CtScene* sc);   // the post-collision request $1D2E/$1D30 = 0 (a frame with no d-pad)
// C0/00EB, one wait of the battle return's chain (C0/881E's camera, C0/00DE's $0153 flip and FD/E39C); field.c.
void CtSceneReturnWaitFrame(CtScene* sc);
// C0/02AF-02C1, one frame of the defeat's fade (C0/881E + FD/FFF7).
void CtSceneDefeatFadeFrame(CtScene* sc);
void CtSceneCameraInput(CtScene* sc, int reqX, int reqY, int leadX, int leadY, int leadValid);
void CtSceneCameraEntry(CtScene* sc, int arrTileX, int arrTileY);
// C0/A385-A4FB: a location load's BG2/BG3 scroll setup from the map header md[1..3] (call after
// CtSceneCameraEntry), and the per-frame result: BG2/BG3 offsets from BG1 in pixels.
void CtSceneLayerScrollLoad(CtScene* sc, uint8_t md1, uint8_t md2, uint8_t md3);
// In a location load's own order: the decode's WRAM half before the object init (C0/A33B, after C0/7F7E's clear of the
// $1D page), and after the init only the C0/286C entry (the camera law and C0/74AC's window), so an init op's store into
// the page (op 0x29's `TSB $1D35 / $1D36`) stays.
void CtWramLayerScrollDecode(uint8_t* s, uint8_t md1, uint8_t md2, uint8_t md3);
void CtSceneLoadCameraEntry(CtScene* sc, int arrTileX, int arrTileY);
void CtSceneLayerOffsets(const CtScene* sc, int* bg2x, int* bg2y, int* bg3x, int* bg3y);
void CtSceneLayerOffsetsOf(const CtScene* sc, const uint8_t* page, int* bg2x, int* bg2y, int* bg3x, int* bg3y);   // the same, from a copy of the object page (a battle: $7F:2000 - $0920)
// The scene-entry OBJ palette sweep (C0/2881 -> C0/B0E6). Call it on a fresh scene entry after the lead is
// placed and the camera is set; its box is not the per-frame one.
void CtScenePalSweepEntry(CtScene* sc);
void CtSceneMenuReloadObjects(CtScene* sc);   // the menu reload's C0/286C object legs: 7155 / E935 / B204 / A7E9 / B0E6
void CtSceneCameraPx(const CtScene* sc, int* camx, int* camy);
void CtSceneCameraPxOf(const uint8_t* page, int* camx, int* camy);   // the same, from a copy of the object page (a battle)

// ---- WRAM bridge for the lockstep oracle differential (--vmdiff) ----
// Maps the VM's per-object scheduler state to and from the ROM's WRAM layout (0x20000 bytes):
//   $1180,X IP (relative to $7F2001: body addr + header), $1C00,X priority,
//   $0F00,X active, $1800/$1880,X pos, $7F0580 return stack, $7F0980 wait flag.
typedef struct { int obj; const char* field; int ours; int oracle; } CtVmDiff;
void CtSceneSeedFromWram(CtScene* sc, const uint8_t* wram);   // seed scheduler state from an oracle WRAM image
int  CtSceneScriptDiffWram(const CtScene* sc, const uint8_t* wram, int* firstDiff); // raw vs $7F2000; 0 = identical
int  CtSceneDiffWram(CtScene* sc, const uint8_t* wram, int withPos, CtVmDiff* out, int cap); // per-obj divergences
// VMDIFF_DRAW coverage: objects the draw compare grades (C0/B0F3's class guard passes) out of nobj.
int  CtSceneDrawGraded(const CtScene* sc, const uint8_t* wram, int* total);
// C0/5B95's gate chain, the one copy (--sweepsolid checks these against the ROM).
int  CtVmSolidMoverGate(const uint8_t* ram, int mover2);          // 5B99 $0F00 + 5BA0 $1C80 bit1
int  CtVmSolidCandGate(const uint8_t* ram, int mover2, int cand2);// 5BC3 $0F00, 5BD5 $1100.7, 5BD7 self, 5BDB $1B01.0
// C0/5BFA/5C20/5C47/5C6D: the per-heading 16 px distance box on $1800/$1880 (heading = $1600,X).
int  CtVmSolidBox(const uint8_t* ram, int mover2, int cand2, int heading);
int  CtVmSolidBoxAt(int mx, int my, int cx, int cy, int carry0, int heading);  // the coordinate core
// C0/5B95's complete scan. `pos` supplies 12.4 coordinates per object index (NULL = read $1800/$1880 from
// `ram`). The scan starts at nobj*2, one past the last object (5BBD), and 5BC8's `DEX DEX / BNE` exits at 0,
// so index 0 is never tested.
typedef void (*CtSolidPosFn)(void* ctx, int idx, int* x12, int* y12);
int  CtVmSolidScan(const uint8_t* ram, int nobj, int mover2, int heading,
                   CtSolidPosFn pos, void* ctx);
// Liveness of the object-solid path: entries, how many the C0/5B95 head gated, how many deflected.
int  CtSceneSolidLive(int* gated, int* deflected);
int  CtSceneObjIp(const CtScene* sc, int obj, int* prio);  // the projected $1180 IP for one obj (-1 = idle)
// Per-object live state: placed = 1 if drawn ($0F00 latch), the floored pixel position and facing.
void CtSceneActor(const CtScene* sc, int obj, int* placed, int* sprite, int* x, int* y, int* facing);
void CtSceneSetActorPos(CtScene* sc, int obj, int x, int y);  // set an actor's pos (the lead = the player)
void CtSceneSetActorPosF(CtScene* sc, int obj, double x, double y);  // subpixel variant (the per-frame lead
                                                              // sync; $1800/$1880 keep the fraction)
// The lead's step: the host moved him from (x, y) this frame (C0/881E's step, before the tick). C0/A810 keeps it only
// on the ticket his op B0 armed this pass (C0/3040) if nothing dropped it (`STZ $1A01,X`); otherwise the tick takes it
// back (LeadStepDropped = 1). A song upload that parks the pass keeps the ticket to the return frame.
void CtSceneLeadStepFrom(CtScene* sc, double x, double y);
int  CtSceneLeadStepDropped(const CtScene* sc);
void CtSceneLeadDeferStep(CtScene* sc, double x, double y);   // a busy lead's step, taken by his B0 if he reaches it
int CtSceneLeadScriptPlaced(const CtScene* sc, int obj, double* x, double* y);  // right after create: 1 = the lead's
                                                              // fn0 placed it after its C0/42B8 arrival arm (fx/fy)
// Trail the active-party followers (move mode 1/2) behind the lead: the ROM's velocity ring, run in each
// follower's op 0xB0 visit. CtSceneTick calls it after the runner. No-op when the party has no follower.
void CtSceneUpdateFollowers(CtScene* sc);
// C0/597D: re-run one object's init function (fn0) and reset its script state, the respawn the post-menu
// reconcile C0/1A03 performs, so the PC loader in fn0 re-reads the party and re-derives this object's slot
// ($1100,X) and sprite. Rewrites its entry in the $0197/$0199/$019B slot -> object table. The caller owns
// positions: the ROM puts the snapshotted X/Y/prio back into the objects the new table names.
void CtSceneReinitObj(CtScene* sc, int obj);
// C0/1A03's reconcile: respawn every character's PC object in the ROM's order 0,1,2,4,3,5,6, so a re-ordered
// $7E:2980-82 re-derives every slot, including a character who had none. Returns how many were respawned. Call
// only when the party changed; the caller restores the snapshotted positions by slot through the new table.
int  CtSceneReconcileParty(CtScene* sc);
// battle_forest_return.md AD.5, the post-victory return. CtSceneBattleRemoveObj dead-marks a killed enemy's
// field object ($1000 bit 7, $1A81 = 0: never drawn or reactivated). CtSceneBattleReturn is the resume (C0/0330
// -> C0/5B1F): seeds the arrival barrier and calls the regroup script on each follower (walk to the lead, then
// E3 01 restores control); no followers -> control at once. Call once, at the battle-return handoff.
void CtSceneBattleRemoveObj(CtScene* sc, int obj);
void CtSceneBattleReturn(CtScene* sc);
// C0/595C: run obj0's slot-1 function synchronously to its 00 terminator. Callers in the ROM: the location load
// C0/56C8, the menu-exit reload chain C0/0206 and the battle-return tail C0/0330, each right after the $7F:2000
// script image is built. obj0's own state ($1180/$1C00/$1001/$7F0580) is untouched.
// `site` names the caller for CT_SLOT1TRACE=1 (`slot1: <site> ops 16@0032 ... end@006B`).
void CtSceneRunObj0Slot1(CtScene* sc, const char* site);
// The active party $7E2980/81/82 (-1 each without a scene). It persists in WRAM across a load.
void CtSceneGetParty(const CtScene* sc, int* a, int* b, int* c);
// ---- Sound sink (disasm/notes/audio_commands.md sec. 6) ----
// The VM's six sound ops ($E8/$EA/$EB/$EC/$ED/$EE) `JSL $C7:0004` after staging a command at $7E:1E00-$1E03,
// and poll the APU's out ports ($2141, $2143). The VM reaches both through this hook. `post` is the JSL and is
// synchronous, as the ROM's is (two objects can stage commands in one frame, each serviced before the next).
// `port` reads APUIO k (SPC -> CPU). With no sink `port` reads 0, so the $ED/$EE waits fall through.
typedef struct {
  void*   ctx;
  int   (*post)(void* ctx, uint8_t* mem7E, int startLine);   // JSL $C7:0004 on the staged $1E00 mailbox.
                                              // Returns what the JSL cost the parked field, frames*1024 + the
                                              // raster line it returned on (audio.c's loader clock), given the
                                              // line it began on; -1 = unpriced (no clock behind this sink).
  uint8_t (*port)(void* ctx, int k);          // read $2140+k (k = 0..3)
} CtSoundSink;
void CtSceneSetSoundSink(const CtSoundSink* sink);   // NULL detaches
// A location load's last JSLs (its init runner and obj0's slot 1), queued while the mode is on and posted by the flush
// in the load's last frame (ct_vm.c gLoadPost).
void CtSceneLoadPostsMode(int on);
void CtSceneLoadOpsPriced(int on);                   // a host that prices its loads: the init pass's ops are CPU time
int  CtSceneLoadPostsPending(void);
long CtSceneLoadPostCycles(int k);                   // the k-th pending one's object-pass cycles (g_ctLzSpriteCycles) when it ran
long CtSceneLoadOpCycles(int* n);                   // ...the ops' share of it (and how many)
const char* CtSceneLoadCostWhy(void);                // the first op of the last priced load whose arms are not walked (NULL: none)
int  CtSceneLoadPostsFlush(CtScene* sc, int count);  // the next `count` pending ones (< 0: all); frames << 10 | line, -1 = none priced
// The rule's infinitely fast CPU for the sound ops' JSLs (on by default; CLAUDE.md, the zelda3 model). The host passes 0
// for a run whose SPC does not stand for the cartridge's (Belthasar's WRAM-seeded runs); the VM then keeps the
// real-time tail rule.
void CtSceneSetRuleIfast(int on);
int  CtSceneMainLoopParked(const CtScene* sc);   // before the tick: the CPU is still inside a script's `JSL C7/0004` this frame
int  CtSceneFrameLegsOn(void);                // the ROM's frame legs run in the tick: $0F01,X (the object's OAM frame) is the drain's
void CtSceneSetCarrySong(int song);   // C0/1B53 location song, staged before the init pass (-1 = record declined)
int  CtSceneLoadSongPrice(void);      // the last scene load's C0/1B8B post: frames << 10 | end line (-1 = none, or no clock)
// New-game ATB-mode selector, A confirm (C2/E24C): sets $2990 bit 6 in the scene's live WRAM when `active`
// (row 0, Active), clears it for Wait (row 1). battle.c's boot unpack (CC/E387) derives $99E1 from it.
void CtSceneSetAtbMode(CtScene* sc, int active);
// Debug/test: make `obj` active-party member `member` (1 or 2): registers charId in $7E2980+member and sets
// its move mode, so following can be exercised without the join scene (the game uses op 0xD3 + op 0x5C).
void CtSceneDebugMakeFollower(CtScene* sc, int obj, int member, int charId);
// Joypad bytes for --vmdiff (Belthasar's host runs the NMI's pad latch instead): $00F2 = held (A=0x80 X=0x40
// L=0x20 R=0x10 B=0x08 Y=0x04 Sel=0x02 St=0x01, low byte of heldF2), $00F0 = newly pressed. The d-pad pair
// $00F1/$00F3 is not written. Call once per frame before CtSceneTick; 0 (nothing held) by default.
void CtSceneSetButtons(CtScene* sc, uint16_t heldF2);
// $01EB, the touch hit latch. The player's proximity scan (C0/9923, CtCollisionActorBlock) records the object it
// overlapped this frame; op 0xB0's lead branch (C0/3054, the only caller of the touch invoke C0/3154) consumes it
// and puts that object on its fn2. Pass -1 for the ROM's 0x80 "nothing". Call once per frame before
// CtSceneTick: the main loop runs the player (C0/00A7) ahead of the event VM (C0/00C4).
void CtSceneSetTouchHit(CtScene* sc, int obj);

// ---- Write-mask byte compare (after zelda3's VerifySnapshotsEq) ----
// The port models only part of WRAM, so instead of comparing all 128 KB against a "don't care" list it compares
// the bytes it claims: every address it wrote since the seed. The mask is taken two ways: (1) a hook in w8(), and
// (2) a per-frame shadow diff of the modelled image (snapshot before the tick, XOR after), which catches the raw
// `m->s[...] =` stores and other units' writes through CtSceneMem7E(). It is bit-granular: w8 claims 0xFF, the
// shadow diff only the bits that changed.
// It adds to CtSceneDiffWram and cannot replace it: the scheduler state (IP $1180, priority $1C00, mover
// $1A00/$1A01) lives in C fields that are not written to Mem.
// Scope: ct.exe does not link field.c/battle.c/menu.c, so there it grades the field VM's Mem only, not the
// battle staging ($019D/$01A3) or boot ($AEC6).
typedef struct {
  uint32_t addr;              // 24-bit CPU address ($7E.... or $7F0000-$7F03FF)
  uint8_t mask, ours, oracle, seed;
  int frame;                  // the frame it first diverged
} CtVmByteDiff;

void CtVmWmaskEnable(int on);                                 // <0 = read VMDIFF_WMASK
int  CtVmWmaskOn(void);
void CtVmWmaskReset(const CtScene* sc, const uint8_t* w);     // call after CtSceneSeedFromWram
void CtVmWmaskExclude(uint32_t addr, int len);                // called by the injecting code
void CtVmWmaskFramePre(const CtScene* sc);                    // after any harness injection
void CtVmWmaskFramePost(const CtScene* sc, const uint8_t* w);
int  CtSceneDiffWramMasked(const CtScene* sc, const uint8_t* w, int frame,
                           CtVmByteDiff* out, int cap);       // latching: each addr once
// The same four, addressed by the WRAM instead of a scene (sc->mem is the CtWram), so a lockstep run keeps
// grading across a hop with no scene (the overworld). Same buffers, same results in field mode.
void CtVmWmaskResetW(CtWram* w7e, const uint8_t* w);
void CtVmWmaskFramePreW(CtWram* w7e);
void CtVmWmaskFramePostW(CtWram* w7e, const uint8_t* w);
int  CtSceneDiffWramMaskedW(CtWram* w7e, const uint8_t* w, int frame,
                            CtVmByteDiff* out, int cap);
void CtVmWmaskStats(int* masked, int* excluded, int* oracleChanged, int* unmodelled, int* dropped);
void CtVmWmaskCensusReport(int toStdout);   // the unmodelled bytes, folded into runs
void CtSceneSetTileSolid(CtScene* sc, int (*fn)(void*, int, int), void* ctx);  // NPC map-collision hook (C0/ACFD walls)
int CtScenePlayerLoaded(const CtScene* sc);              // 0 = the scene loads no PC ($0197 stays the 0x80
                                                         // "none" sentinel): draw no player
// The one-byte PC loaders' op -> char-id map (the C0:5D6E table and each handler's immediate). Exposed so --vm
// can print it (a reference built from the same map could not catch a shared error).
int CtScenePcLoaderId(int op);
int CtScenePlayerObj(const CtScene* sc);                 // index of the lead-PC object ($0197); the engine
                                                          // renders it as the player
int CtSceneActorActivatable(const CtScene* sc, int obj);  // 1 = positioned, $0F00 latch set, non-empty fn1
// C0/1CFC: the action-button probe. Scans objects from high to low and returns the first one inside the faced
// 28 x 12 px box (12.4 limits #01C0 / #00C0), or -1. `facing` = the player's $1600 byte.
int CtSceneActionProbe(const CtScene* sc, int playerObj, int facing);
int CtVmActBoxAt(int mx, int my, int cx, int cy, int carry0, int facing);   // one handler, for tests
int CtSceneActorTouchable(const CtScene* sc, int obj);    // 1 = positioned, $0F00 latch set, non-empty fn2 (touch);
                                                          // a sprite is not required
int CtSceneActorHidden(const CtScene* sc, int obj);      // 1 = a positioned sprite explicitly hidden (op 0x91/0x7E)
int CtSceneActorMoving(const CtScene* sc, int obj);   // 1 if the actor has an active move task (walk vs idle)
int CtSceneActorVecMove(const CtScene* sc, int obj);  // 1 if the active move is a vector slide (op 0x9C/0x9D):
                                                      // the actor keeps its idle pose (no walk cycle)
int CtSceneActorSolid(const CtScene* sc, int obj);    // 1 if the actor blocks the player (C0/9923, $1B01,X bit 0)
void CtSceneActivate(CtScene* sc, int obj);           // run obj's activate (slot 1) at priority 1 (talk)
// The invoke prologue C0/5AC5 (activate, CMP #02) and C0/3154 (touch, CMP #03) both open with: 1 = the invoke
// is accepted. `w` = the flat $7E image (NULL tests the priority alone); `prio` = the object's live script
// priority, a stand-in for $1C00,X, which the port does not keep in WRAM; `minPrio` = the ROM's immediate.
// Checked by `ct.exe --sweepact`.
int CtVmInvokeGate(const uint8_t* w, int obj, int prio, int minPrio);
// Textbox state: returns 1 if a dialogue box is up, with its string-table file addr and index
// (decode via CtAssetString(CT_ASSET_DIALOGUE, *tableAddr, *strIndex, ...)).
int CtSceneTextbox(const CtScene* sc, int* tableAddr, int* strIndex);
// 1 while a dismissed box still parks the event VM ($0129 = 0x0D during the close ramp, or 0 until the op's
// next visit). The host draws the window ramping out; the script resumes on its first visit with $0129 == 0.
int CtSceneTextboxUnparking(const CtScene* sc);
void CtSceneAdvance(CtScene* sc);              // dismiss the current textbox (player confirmed)
void CtSceneTextboxRevealDone(CtScene* sc, int pendingCode);  // typewriter drew the page's last char: 6 -> 7 (C0/21D8)
void CtSceneTextboxPause(CtScene* sc, int n);            // the typewriter met `03 n` (C0/21A8): a timed pause, or `03 00`'s close
int  CtSceneTextboxResumeArmed(const CtScene* sc);       // a pause ran out this frame (C0/2096 RTL): no typing until the next
                                              // pendingCode -> $0215: non-zero = another page follows (C0/2024)
void CtSceneDecisionInput(CtScene* sc, int aPressed, int upPressed, int downPressed);  // C0/1ADF: the
                                              // decision box's cursor arm; owns the input frame while $0162 != 0
void CtSceneTextboxPage(CtScene* sc);          // a page advance from state 7 (C0/2086): 8 -> ... -> 6
void CtSceneTextboxLineStep(CtScene* sc, int code);  // C0/2126's head: the pending code steps the line cursor $0131
int  CtSceneTextboxChoice(const CtScene* sc);  // 1 if the open box is a decision/choice box
int  CtSceneTextboxOptLines(const CtScene* sc, int* first, int* last);  // decision-box option-line range (op2)
int  CtSceneTextboxOp(const CtScene* sc);      // the opening opcode: 0xBB/0xC0 auto-position,
                                               // 0xC1/0xC3 top, 0xC2/0xC4 bottom
int  CtSceneTextboxObj(const CtScene* sc);     // the speaker (object that opened the box)
// Active-party char-id at slot 0/1/2 (ROM $7E:2980/81/82); 0x80 = empty slot. Read by the
// dialogue name-substitution codes 0x1B-0x1D (party member 1/2/3).
int  CtSceneParty(const CtScene* sc, int slot);
// Name-entry screen (event op 0xC8, operand 0xC0+char-id): returns the char-id whose name the
// player should enter, or -1 if no screen is up. The script blocks until CtSceneNameDone.
int  CtSceneNaming(const CtScene* sc);
void CtSceneNameDone(CtScene* sc);             // confirm: resume the blocked 0xC8 op next tick
// The load / new-game shell (event op 0xC8 with an odd operand < $80 -> the launcher's C0/0D00 arm = menu mode 0 =
// screen $0E). Returns the raw operand, or -1 when no shell is up; the script blocks as for naming. In the ROM it
// never unblocks (both of the shell's exits are a `JML $C0:0000` reboot), so the request stands until the host reboots.
int  CtSceneLoader(const CtScene* sc);
// The shop (event op 0xC8 with an operand $80-$BF -> the launcher's C0/0D09 arm = menu mode 2 = screen $0C; the shop
// number is operand & $3F). Returns the raw operand, or -1 when no shop is up. The script blocks until the host calls
// CtSceneShopDone (the shop closed and the field resumed), then steps over the op (C0/3757's done arm).
int  CtSceneShop(const CtScene* sc);
int  CtSceneSavePoint(const CtScene* sc);   // the same park with an even operand below $80: mode 6, screen $0B
int  CtSceneExchange(const CtScene* sc);    // ...and operand $00: mode 5, screen $07, the party exchange (1 = parked)
void CtSceneShopDone(CtScene* sc);
// A $C3 special sequence (event op 0xFF with an operand < $90: C0/3FBB fades, C0/3FD6 resets the stack and
// C0/3FE0 `JML $C3:0000`). Returns the sequence id (2 = the title screen) or -1. The script stays parked: the
// sequence ends by rebooting into C0/0000, so the host performs that reboot and discards the scene.
int  CtSceneSequence(const CtScene* sc);
void CtSceneSequenceDone(CtScene* sc);
// Op 0xFF operand $80-$8F: the bank-$FD gate warp. Parked like a sequence, but it ends in the C0/0000 reboot
// that loads the location the preceding DD/DE staged.
int  CtSceneGateWarp(const CtScene* sc);
void CtSceneGateWarpDone(CtScene* sc);
// Op 0xFF < $90 (either target) found $0119 lit: the op took the first `DEC $19` of its own fade loop (C0/3FC1) in its
// frame, and the host owes the rest: one DEC a frame until 0, the jump on the frame after.
int  CtSceneFfFading(const CtScene* sc);
// 1 while a parked op 0xC8 (any mode), op 0xFF < $90 or op 0xDF/0xE1 has left the field main loop C0/0097, the
// only loop that polls the X button (C0/00A1 `JSR C0/18D9`).
int  CtSceneOffMainLoop(const CtScene* sc);
// Op 0xDF/0xE1 (C0/3B98/3B4B): the location is staged in $0112/$0114/$0116 and the machine spins on $4210 until
// the vblank, then reboots through C0/0000. 1 = the host owes that reboot; Done marks it taken. The scene's main
// loop is dead from the op on (CtSceneTick does nothing), and the reboot throws the scene away.
int  CtSceneReboot(const CtScene* sc);
int  CtSceneColdReset(const CtScene* sc);
void CtSceneRebootDone(CtScene* sc);
const char* CtGetName(int id);                 // current name text (ASCII) for char-id 0-7
void CtSetName(int id, const char* name);      // set char-id's name (<=5 chars) -- the naming screen
// Resolve the dialogue substitution markers ('\x01' + code, emitted by assets/text.py): the name codes
// 0x13-0x20 into the current name text (ASCII), as the ROM's decoder copies the RAM name buffers at $7E:2C23
// (C2/5B15); party codes 0x1B-0x1D through `sc`'s active party (NULL = the new-game default, Crono only);
// {num} (0x0D-0x0F) and {item} (0x1F) from the scene's RAM. Other bytes copy through.
// Returns the written length; `out` is NUL-terminated. `in` and `out` must not overlap.
int  CtResolveNames(const char* in, int len, char* out, int cap, const CtScene* sc);
// Scripted animation state of actor i (see CtVmActor.anim*); returns animGen.
int  CtSceneActorAnim(const CtScene* sc, int i, int* mode, int* index, int* il, int* loops, int* staticFrame);
void CtSceneSetActorAnim(CtScene* sc, int i, int mode, int index);  // the controller stamps the lead's $1680 (idle 0)
int  CtScenePollTileCopy(CtScene* sc, int out[7]); // drain scripted 0xE4/0xE5 map tile copies
#define CT_TC_RENDER 0x100   // out[6] flag: the copy reaches the picture only (op 0xE5's re-upload, C0/179A's styles)
#define CT_TC_COLL   0x200   // out[6] flag: the copy reaches the collision planes only (C0/1790's AF4E on WRAM)
// The $0117 bit 5 service, C0/1770: what kind of main-loop frame the coming one is. NORMAL runs every leg; ENTER is the
// dispatch's frame (C0/0097, C0/18D9 and C0/0C76's fades, then C0/1770: 885A + C0/00BF, no 881E/1AAC/1F87/21E1); ALIGN a
// further align-loop frame (885A + C0/00BF only); STYLE a frame of C0/179A's branch (C0/00DE only); RESUME the frame
// the branch returns into (C0/00A7-00BA: 881E, 1AAC, 1F87, 21E1, 274D, 00BF). CtSceneXferBegin is declared to latch it
// for a host whose legs run before CtSceneTick, but ct_vm.c does not define it.
enum { CT_XF_NORMAL = 0, CT_XF_ENTER, CT_XF_ALIGN, CT_XF_STYLE, CT_XF_RESUME };
int  CtSceneXferFrame(const CtScene* sc);          // the coming frame's kind (pure)
int  CtSceneXferBegin(CtScene* sc);                // ...latched for this frame
// C0/0C76's view of $0117 at C0/00A4, the frame's head: a host whose player controller (C0/881E, which stages positional
// exits) runs before CtSceneTick latches the byte first, so the tick's C0/0C8A drain sees what the ROM's head saw.
void CtSceneHeadLatch(CtScene* sc);
// C0/0C9A-0CC4: commit the staged change ($0112-$0116 -> $0100-$0104, the origin -> $0105-$0109) and leave the main loop
// (CtSceneTick does nothing more; CtScenePollLocChange reports the destination). Once per scene.
void CtSceneExitCommit(CtScene* sc);
// C0/EAD1, the field NMI's first branch, on the $46 the frame left: 1 = a tilemap upload takes the NMI (`BRL EC15`) and
// C0/EB79-EC13 are skipped: the text line (6ECB), the CGRAM (712E), the OAM (ECA3), the VRAM queue (B1B2), the edges.
int  CtFieldNmiTilemapArm(uint8_t v46);
// Drain a committed location change (op 0xDD-0xE1 or a map exit): returns 1 once, with the destination
// location id, tile x/y, facing (0-3) and transition style (0 normal, 1 = the 0xDE warp). The field carries
// the story flags and fades into the load.
int  CtScenePollLocChange(CtScene* sc, int* loc, int* x, int* y, int* facing, int* style);
void CtSceneTouch(CtScene* sc, int obj);           // fire the touch slot (walk-into; battles)
// Test surface (CT_CALLFN): issue the same request ops 0x02..0x07 issue (target, fn slot, priority) into the
// scheduler's request path, so fn slots 3..15 can be driven headless.
void CtSceneRequestFn(CtScene* sc, int obj, int slot, int prio);
void CtSceneActorSetFacing(CtScene* sc, int i, int facing);
int  CtSceneExplore(const CtScene* sc);            // 0 = control locked (op 0xE3 00)
void CtSceneSetLoadFadeIn(CtScene* sc, int on);   // 1 = this frame is a C0/2848 fade-in ramp frame,
                                                  // not a C0/0097 main-loop frame (see CtScene.loadFadeIn)
int  CtSceneBright(const CtScene* sc);             // op 0xF0 screen fade level 0..15 (15 = full)
void CtSceneSyncBright(CtScene* sc);               // re-read it from $0119 (after the host's C0/2848 INC)
int  CtSceneActorMoveAnim(const CtScene* sc, int i);   // op 0x87 move-animation override (0 = walk)
int  CtSceneActorPrio(const CtScene* sc, int i);   // the op 0x8E priority byte $0F80,X (0x80 = auto)
int  CtSceneActorPalRot(const CtScene* sc, int obj, int* start, int* span, int* phase); // op 0x88 palette rotation
int  CtSceneActorPalBlob(const CtScene* sc, int obj, int* start, uint16_t* bgr);  // op 0x88 blob palette overwrite; returns count
int CtSceneActorAtB0(const CtScene* sc, int i);   // obj i's last op was 0xB0 (C0/3002 -> C0/9E84 for the lead)
void CtIrisCircleImage(uint8_t* w128, uint8_t arg);   // C3/0077 (the iris window table) on a flat 128 KB image: ct.exe --sweepiris
void CtPolyQuadImage(uint8_t* w128);                  // C3/0EFA (the quad window table) on a flat 128 KB image: ct.exe --sweeppoly
int  CtSceneActorColorGated(const CtScene* sc, int obj);  // 1 = an op 0x88 colour-gated sprite: clip it to the iris
int  CtSceneActorScripted(const CtScene* sc, int i); // 1 = a called/scripted fn is running
int  CtSceneActorMode(const CtScene* sc, int i);     // $1100 move-mode probe (debug)
int  CtSceneSaveState(const CtScene* sc, const char* path, const double player[4]);  // debug: snapshot the scene + the field player state {px,py,pz,pfac} (Belthasar F5)
int  CtSceneLoadState(CtScene* sc, const char* path, double player[4]);  // debug: restore (F9); same scene only. 0 = fail, 2 = VM, WRAM and player[] restored
int  CtSceneStateLoc(const char* path, uint8_t* flags);  // debug: the location ($0100) a snapshot was taken in (+ its $7F:0000 page into flags[0x200] if non-NULL); -1 if not loadable
void CtSceneAnimFinished(CtScene* sc, int i);       // renderer: the once-anim of actor i ended
void CtSceneChoose(CtScene* sc, int option);   // pick option N: raise the confirm on its line; `result` (op 0x1A) is set on the completion visit
void CtSceneGetFlags(const CtScene* sc, uint8_t* out0x200);  // copy the live $7F0000 story flags
// ---- Battle transfer (op 0xD8 / the C0/1770 consumer) ----
// The VM's system memory is the engine's $7E image. The $0117 transfer consumer (field.c, C0/1770-18D6) and
// battle.c read and write the staged interface ($019D list, $29B0 PC block, $29BF enemy slots, $2989/$2A1F
// config, $0102-4 respawn cell) directly in it, as the ROM routines do. Glb = the $7F0000 page (0x200 bytes;
// $7F01EC battle outcome = glb[0x1EC]).
uint8_t* CtSceneMem7E(CtScene* sc);
void CtSceneScriptImage(CtScene* sc);   // C0/56D4 after a battle: the script image back at $7F:2000
void CtSceneArrivalPriorities(CtScene* sc, int onMap, uint8_t z6a, uint8_t p6e, uint8_t p6f);   // C0/9DC3's WRAM half
uint8_t* CtSceneMemGlb(CtScene* sc);
uint8_t* CtSceneMemLoc(CtScene* sc);   // the $7F0200 script-locals page (0x200 bytes)
// Op 0x83's $1201,X source: the field enemy table $E4/F600 (10 bytes per record; byte 4 -> $1201,X,
// C0/4762-476A), 256*10 bytes. Optional: without it a fresh op 0x83 load leaves $1201 as is.
void CtSceneSetEnemyRecTable(const uint8_t* tab, int len);
// The 4 MB ROM image for the load ops' gfx/palette allocators (ops 0x81/0x82/0x83: sprite meta pointer tables
// $E4:FFE0, NPC headers $E4:F000, enemy records $E4:F600, LZ gfx blocks) and other ROM reads. Optional: without
// it fresh loads leave $1280/$1380/$1400../$0F81 zero (a seeded scene carries the oracle's values).
void CtSceneSetRomImage(const uint8_t* rom, long len);
void CtHdmaTableBuild(uint8_t* wram128);   // `JSL FD/C2C1`: the field's HDMA table builder (src/hdma_table.c), $0153 bit 0 flips
void CtSceneFree(CtScene* sc);

#endif
