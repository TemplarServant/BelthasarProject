#pragma once
// M3-B: the OVERWORLD camera/scroll engine (bank C2), RE'd from the live oracle on the world map.
//
// Per frame (C2/36F7, X=[$4E]=the lead object block, $0D70 on the overworld):
//   dX=[B+$1C], dY=[B+$20] (the player's movement this frame, px) ->
//   H-scroll BG1+BG2 by dX  (dispatcher C2/0568; shadows $20/$22; world wraps at 1536)
//   V-scroll BG1+BG2 by dY  (dispatcher C2/066C; shadows $26/$28; world wraps at 1024)
//   $1BF1=dX / $1BF3=dY for the BG3 (ocean) animator.
// BG3 animator (its own object, counter $0D12; C2/75CE-75FC):
//   ctr++; if ((ctr&3)==0) BG3H--;  if ((ctr&7)==0) BG3V++;   (ambient wave drift)
//   BG3H += $1BF1; BG3V += $1BF3; clear both.                 (camera follow, free-running 16-bit)
// The NMI copies shadows $20-$2B to BG1/2/3 HOFS/VOFS (C2/01D8-0211). No edge clamps: the
// overworld is a TORUS (camera == playerPos-(128,112), both mod map size).
#include <stdint.h>

typedef struct CtWorldCam {
  uint16_t bg1h, bg2h;   // $20/$21, $22/$23 -- unwrapped camera X, 0..1535
  uint16_t bg1v, bg2v;   // $26/$27, $28/$29 -- unwrapped camera Y, 0..1023
  uint16_t bg3h, bg3v;   // $24/$25, $2A/$2B -- ocean layer, free-running 16-bit
  uint8_t bg3ctr;        // the animator object's frame counter ($0D12)
  int pendDx, pendDy;    // $1BF1/$1BF3: the lead STORES its velocity there at its slot of the object pass (C2/3725),
                         // the drift object ADDS them to $24/$2A and zeroes them at its own slot (C2/75E5-75F8)
  int leadDx, leadDy, leadStore;   // this frame's lead delta, held from the host mover (CtWorldStreamFrame, which runs
                                   // before the pass) until the pass reaches the lead's block
  // The 16.16 drift objects (C2/7702 world 2, C2/77F2 world 4): the block's +2 (visited) and the two fractions
  // C2/0F38 keeps in +12 / +16 -- held here, as bg3ctr is, so the model never writes the RAM it is handed.
  uint8_t bg3drInit; uint16_t bg3fx, bg3fy;
  // The per-line BG3 H scroll objects (C2/7759 world 2, C2/7652 world 1): HDMA channel 1 to $2111 from a table they
  // rebuild every frame. waveInit = the block's +2; wavePhase = the phase ($7E:01E2 for C2/7759, the block's +22 for
  // C2/7652). bg3hLineOn = the channel armed ($4A bit 1); bg3hLine[L] = scanline L's BG3HOFS.
  uint8_t waveInit, bg3hLineOn; uint16_t wavePhase;
  // C2/7652's HDMA is INDIRECT ($4310 = 42): four 64-line blocks whose pointers the NMI copies from $7E:A916 (C2/0300-
  // 0312) into a 128-word table (the 64 words + C2/76F2's copy). The drift C2/75FD moves that pointer by twice the
  // camera's vertical step ($1BF3, wrapped in [A925, A9A5)), so the wave travels with the mist. waveTop = $A916.
  uint16_t waveTop;
  uint16_t bg3hLine[240];
} CtWorldCam;

// Seed the camera state from live WRAM (the scroll shadows + the animator counter).
void CtWorldCamSeed(CtWorldCam* c, const uint8_t* ram);

// Advance one frame: dx/dy = the player's movement this frame in pixels (signed).
void CtWorldCamFrame(CtWorldCam* c, const uint8_t* ram, int dx, int dy);

// M3-C: the overworld PLAYER MOVER (lead-object handler C2/3404 -> input phase C2/3567 ->
// setters C2/36xx -> probe C2/3A56/3A8F -> props C2/39D5+62AE -> verdict C2/39C3 -> blocked
// C2/36B6 / integrate C2/0F38). Movement is an 8-PX QUANTIZED STEPPER: at each 8px boundary
// (phase 1) the d-pad is read with CARDINAL PRIORITY Up > Down > Left > Right (NO diagonals
// on foot -- verified live), the next 8px cell is probed (pos +-8, torus-wrapped), and its
// walkability decided from the per-block property nibbles; while traveling (phase 2) the
// player integrates 1 px/frame with NO input read until pos == probe. Velocity persists
// across arrivals; it is cleared only when a probe BLOCKS (C2/36C3) or overwritten by the
// next setter. Walkability: the 2x2 of 8px cells around the probe -> the verdict tests the
// TOP pair: blocked iff ((propUL | propUR) & 3) != 0.
typedef struct CtWorldMover {
  uint16_t posX, posY;     // world px, B+$14/$18 = $0D84/$0D88
  int16_t velX, velY;      // px/frame, B+$1C/$20 = $0D8C/$0D90
  uint16_t probeX, probeY; // current step target, B+$2A/$2C = $0D9A/$0D9C
  uint8_t phase;           // B+$02 = $0D72: 1 = at-boundary (read input), 2 = traveling
  uint8_t moving;          // B+$28 bit7
} CtWorldMover;

void CtWorldMoverSeed(CtWorldMover* m, const uint8_t* ram);

// M3 streaming: the engine keeps only a 64x32 SNES tilemap per BG in VRAM ($6000 BG1 / $6800
// BG2) and STREAMS one tile column/row of fresh metatile chips on every 8px camera crossing
// (scroll-step loops C2/0607|059D H, C2/070B|06A1 V; stream blocks 0625/05D1/0729/06D5).
// Writers: column left/right = C2/0773 (+0/+4) / 07C2 (+2/+6); row top/bottom = C2/0843
// (+0/+2) / 088F (+4/+6); 30 words per column / 34 per row are freshly written into a
// per-frame bump-allocated staging ring at $7E:F000 ($E0, +0x40 per column, +0x80 per row,
// reset each NMI) and DMA'd whole 64/128-byte slots to VRAM (queue at $0060+, mode 0x81
// column / 0x80 row x2 screens) -- so the few slot bytes NOT freshly written carry the ring's
// previous contents into VRAM (deterministic; modeled). Cursors: $E3/$E5 = per-BG 8px column
// (wrap 192), $E7/$E9 = 8px row (wrap 128).
typedef struct CtWorldStream {
  uint16_t tilemap[2][0x800];  // BG1/BG2 64x32 word maps (screen 0 at +0x000, screen 1 at +0x400)
  uint16_t e3[2], e7[2];       // per-BG streaming cursors
  uint8_t staging[1024];       // the $7E:F000 ring image (stale bytes matter)
  int stageOff;                // $E0 - $F000, reset every frame
  // The DMA queue ($0060+) is flushed by the NEXT frame's NMI -- uploads queued during frame k
  // land in VRAM at the start of frame k+1. Modeled as a 1-frame pending queue.
  struct { uint8_t bg, isRow; uint16_t addr; int stage; } pend[8];
  int nPend;
} CtWorldStream;

// Seed from live WRAM + VRAM (LakeSnes ppu->vram, word-addressed).
void CtWorldStreamSeed(CtWorldStream* st, const uint8_t* ram, const uint16_t* vram);

// Build BOTH 64x32 tilemaps fresh around the camera (what the C2 loader's full expansion
// produces) and align the cursors -- for when no live VRAM exists (e.g. ct_assets.dat mode).
// Window-correct: every tilemap slot gets the world column/row nearest the camera window.
void CtWorldTilemapBuild(CtWorldStream* st, uint8_t* ram, const CtWorldCam* c);
// The same tilemaps as a PICTURE-only build (the RAM untouched, no cursors kept): what the widescreen's sides show.
void CtWorldTilemapView(CtWorldStream* st, const uint8_t* ram, const CtWorldCam* c);

// M3-D: LOCATION ENTRY. 7-byte records at $7E:7E00 (count $1B38, from desc[0x15]):
//   [0]=cell col | 0x80(active)  [1]=cell row  [2]=name id  [3]/[4]=dest word
//   (&0x1FF = location index; >>9 = facing/flags -> $0104)  [5]=dest tile X  [6]=dest tile Y
// A system object (C2/3264) rescans every frame: $1B32 = the offset of the record whose cell
// matches (playerX>>4, playerY>>4) (0xFFFF none; scan C2/32CD). Walking onto an entry also
// sets $0282 via the prop-nibble bit 2 of BOTH probe corners (C2/39AE, AND #$0404). Pressing
// A in the mover's phase 1 with $1B32 valid -> $027C=3 (C2/35E5) -> the loader sets
// $0100=dest, $0102/$0103=dest tile, $0104=flags, and the RETURN point $0105=old loc,
// $0107=(BG2 Hcursor+0x10)%0xC0, $0108=(BG2 Vcursor+0x10)&0x7F.
// C2/1100 -- object-VM op $05, ENTER A LOCATION. Writes the seventeen cells the ROM writes
// ($0100-$0109, $04FC-$04FF and $027C = 2, the transition request). `op` points at the opcode.
void CtWorldObjOp05(uint8_t* ram, const uint8_t* op);
void CtWorldLocationStores(uint8_t* ram, const uint8_t* op);   // C2/1100's stores = state 3's C2/2470 (no $027C)
void CtWorldSetRom(const uint8_t* rom, long len);            // the ROM image, for VM objects running bank-$C2 scripts
void CtWorldSpawnVm(uint8_t* ram, uint8_t bank, uint16_t pc); // C2/04D9 -- a VM object started from outside the pass
void CtWorldLoadTriggers(uint8_t* ram, int world);     // C2/2A44: the event / transition cells and the event scripts
void CtWorldObjOp4E(uint8_t* ram, const uint8_t* op);
typedef int (*CtWorldOp4EHook)(uint8_t* ram128, uint32_t target);   // op 4E -> $C6:E241 / $C6:E27E: 0 = ran
void CtWorldSetOp4EHook(CtWorldOp4EHook fn);
void CtWorldNmiLatch(uint8_t* en, uint8_t* wh0, uint8_t* wh1);   // HDMAEN / WH0 / WH1 as the last NMI pushed them   // C2/181C -- indirect FAR call, advance 4
uint16_t CtWorldEntryScan(const uint8_t* ram, int posX, int posY);   // -> the $1B32 value

typedef struct CtWorldEntry {
  uint16_t destLoc;    // $0100 after entry
  uint8_t flags;       // $0104 (dest word >> 9)
  uint8_t destX, destY;// $0102/$0103 (field entry tile)
  uint8_t nameId;      // record[2]
} CtWorldEntry;

// Decode the record at a CtWorldEntryScan offset. Returns 0 if off == 0xFFFF.
int CtWorldEntryDecode(const uint8_t* ram, uint16_t off, CtWorldEntry* out);

// M3 AMBIENT OBJECTS (birds/smoke/boat): the script VM (handler C2/0F63) that drives the
// overworld's life. Object blocks (0x40 bytes, $0B30-$1B30) run bytecode from the $7F:0400
// blob; the dispatcher executes ops until one yields (returns 0). Opcode map RE'd via a
// 5-agent workflow + script-walk verification (see disasm/notes/ow_objvm.md). Key ops:
// 2E/2F set 16.16 velocity, 31 = move+animate n frames (pos += vel, world wrap, anim tick),
// 38/39 wait (39 ticks the anim; '39 00' parks forever), 1A jump (target-1), 1B DJNZ,
// 22/23/25/26/27/4C conditional branches on WRAM bytes, 36/37 one-deep gosub, 30 set anim,
// 2C teleport, 3F/40 fly to absolute X/Y (auto-orient + auto-anim), 0D/0E/0F/0A block byte
// ops, 10/11 absolute WRAM zero/inc, 09/43 spawn VM clone children, 04/28 spawn NATIVE
// children (sprite effects; not run here), 34 call native (78A1 = on-screen visibility ->
// $00, others stubbed), 05 queue a transition command, 52 die (block freed), 00 reset state.
// Runs IN PLACE over the wram view (objects + blob packed in T_WORLD fmt 2).
// `c`/`st` are MUTABLE: an object can scroll the camera (op 0x32 = C2/164B, native C2/7849), through
// the same C2/0568 / C2/066C steppers the player's camera uses. `st` may be NULL (camera only, no
// tilemap streaming -- the --checkowobj harness).
void CtWorldAmbientFrame(uint8_t* ram, CtWorldCam* c, CtWorldStream* st);

// The CURRENT anim frame of an ambient object, straight from the ROM descriptor its anim sub-VM
// pointer (+0B) is parked on -- the canonical replacement for guessing a frame from a counter.
// Returns the descriptor's PIECE COUNT (0 = not parked on a "show frame" op) and fills piece 0:
// dx/dy = the signed per-frame anchor offsets the ROM adds to the object position (C2/0D82-0DB2),
// name = the 9-bit tile name, flip = bit0 H / bit1 V (word2 bits 14/15).
int CtWorldAnimFrame(const uint8_t* B, int* dx, int* dy, int* name, int* flip);
int CtWorldAnimPiece(const uint8_t* B, int piece, int* dx, int* dy, int* name, int* flip);  // piece i of the descriptor's list
uint16_t CtWorldAnimPtrForId(unsigned id);         // op 0x30 == C2/1CDA: $C3:E000[id], the DIRECT 8-bit index
int CtWorldAnimIdForPtr(int ptr, int lo, int hi);   // inverse of op 0x30: the $C3:E000 id that yields ptr, or -1
int CtWorldAnimDesc(const uint8_t* B);          // the op-4 descriptor pointer the anim sub-VM is parked on, or -1
int CtWorldAnimDescPiece(int desc, int piece, int* dx, int* dy, int* name, int* flip);   // piece i of the bank-$C3 descriptor at `desc`

// THE PER-FRAME DRAW LIST -- the ROM's own VISIBILITY gate for every ambient object.
// C2/032B `JSR C2/0C4D` EMPTIES the 64-node list ($7E:B000, head $7E:B500) at the top of every
// overworld frame; the object pass then runs, and only the anim sub-VM's op-4 SHOW FRAME handler
// (C2/0EA3) puts an object back on it -- its park/hold path at C2/0EC9 `JSR C2/0B53` enqueues a node
// carrying the object's X (+14), Y (+18), attr (+0F), tile base (+10) and the descriptor pointer;
// C2/0336 `JSR C2/0CEA` then drains the list into the OAM shadow. So an object is drawn on exactly
// the frames its anim sub-VM RAN: a script that yields on op 0x38 (wait WITHOUT the C2/0E1D tick,
// unlike op 0x39) enqueues nothing and emits no OAM at all, even though its +0B anim pointer still
// names a frame. That is how the ERA PLACARD hides while the player walks -- its script sits in the
// `38 01` arm until $7E:1BF7 reaches $78. Measured on the oracle (saves/ow_w0.state): after
// `--btnscript 80:60` block $0D30 is pc=$06A3 anim=$F836 and NOTHING is in OAM at y=192; after
// `80:60,0:200` it is pc=$069E with the SAME anim=$F836 and OAM carries $1AC@252 $1A6@12 $18A@28.
// The anim pointer is identical in both, so it cannot be the gate; the enqueue is.
void CtWorldDrawListInit(uint8_t* ram);     // C2/0BF6 -- the $B400 template, once per world load (C2:23AE)
void CtWorldDrawListReset(uint8_t* ram);    // C2/0C4D -- at the top of the world frame, BEFORE the object pass (C2/032B)
// C2/032E + C2/034D + C2/0CEA/0D08: park the OAM shadow and decode the list into $7E:0700/$0900 -- after the
// object pass (C2/0336). The next NMI DMAs it to OAM, so the raster shows the previous frame's build.
void CtWorldOamBuild(uint8_t* ram, uint16_t scrollH, uint16_t scrollV);
// THE PICTURE'S OWN RECORD OF THAT BUILD (render-only, no RAM): which object's piece each OAM entry is (block * 16 + piece,
// -1 = none) -- a key that survives the entries moving between slots -- and every piece without C2/0D97's -15..255 X cut
// (the widescreen's sides), in OAM order (the first in front).
typedef struct CtWorldPiece { int16_t x, y; uint16_t word; uint8_t size; int key; int16_t ox, oy; } CtWorldPiece;   // ox/oy: its object's place
#define CT_OW_WIDE_MAX 256
const int* CtWorldOamKeys(void);                                    // [128], the last build's (the OBJECT: block)
const int16_t* CtWorldOamObjPos(void);                              // [128][2], that object's place on screen
int CtWorldOamWide(const CtWorldPiece** out);                       // the last build's pieces, their count   // the $22/$28 BG2 scroll the port keeps in CtWorldCam
int  CtWorldObjShown(int blockAddr);        // != 0 iff that block enqueued a frame THIS frame
// THE LEAD BLOCK'S OWN ANIMATION -- C2/3404's C2/1CDA installs (stand every idle frame, walk on a press, the
// fidget once at $02B1 == $F0, stand again on a blocked step) and its per-frame C2/0E1D tick. `dir` = the
// pressed cardinal (0 Up, 1 Down, 2 Left, 3 Right; the ROM's priority Up > Down > Left > Right), -1 = no
// input, -2 = non-interactive ($027C != 1: tick only). `phaseBefore` = the mover's phase before this frame's
// CtWorldMoverFrame, `blocked` = that call refused the press (C2/39C3). `facingInit` seeds +28 the first time
// (C2/34F8 takes $0104 & 3). The block: the object whose handler word is $3404 ($0D70 / $0DF0).
int  CtWorldHasLead(const uint8_t* ram);   // is a class-$3404 player object present at all?
int  CtWorldHasObjClass(const uint8_t* ram, uint16_t cls);   // is a block of this handler word live?
int  CtWorldLeadBlock(const uint8_t* ram);
void CtWorldLeadAnimFrame(uint8_t* ram, int dir, int phaseBefore, int blocked, int facingInit);
// The overworld pass's inline `JSL C7/0004` (C2/2F0F): the host posts the staged mailbox on its SPC and returns the NMIs
// the call crossed. A positive count SUSPENDS the pass (CtWorldTakePassSuspend); the next CtWorldAmbientFrame resumes it.
typedef int (*CtWorldJslFn)(uint8_t* ram);
void CtWorldSetJsl(CtWorldJslFn fn);
int  CtWorldTakePassSuspend(void);    // NMIs crossed by this frame's inline JSL (0 = the pass completed), consumed
void CtWorldHdmaSet(int ch, uint8_t dmap, uint8_t bbad, uint16_t tbl);   // an HDMA channel as a native programs it
int  CtWorldHdmaGet(int ch, uint8_t* dmap, uint8_t* bbad, uint16_t* tbl);
void CtWorldGaugeEnter(uint8_t* ram, uint8_t* vram);   // C2/6A34's NMI-off half: the time gauge set up (mode 7, loads, spawns)
void CtWorldGaugeLeave(uint8_t* ram);                  // C2/6AA6: the gauge's fade-out script
int  CtWorldGaugeTakeSound(void);
void CtWorldTraceFrame(int f);                          // the host frame number CT_OWVEHTRACE prints
void CtWorldTraceEnd(const uint8_t* ram);               // ...and its line as the host frame ends
void CtWorldMapEnter(uint8_t* ram, uint8_t* vram);     // C2/631F's NMI-off half: the world map ($027C = 6) set up
void CtWorldLeadReinit(uint8_t* ram);     // C2/34F8: the lead's init again (stepping out of a vehicle, $027E 5 -> 0)
void CtWorldLeadRingPush(uint8_t* ram);   // C2/3934: the lead's step arrival onto its trail ring ($1B5A/$1B5B/$1B7B)
void CtWorldLeadArrive(uint8_t* ram);     // C2/375F: the arrival's cell test -> $0282 (the scanner's request)

// FIELD -> OVERWORLD RETURN: a field 'world' exit jumps to the overworld with $0102/$0103 =
// the arrival 8px cell (the exit record's dpos>>3) and $0104 = facing. The loader places the
// player at cell*8 with the camera centred (pos-(128,112), torus) and the stream cursors at
// camera/8. (Symmetrically, $0105/$0107/$0108 are re-saved with the SOURCE of every
// transition.) This seeds a fresh camera+mover state the way the C2 loader does.
void CtWorldEnter(CtWorldCam* c, CtWorldMover* m, int cell8x, int cell8y);

// Advance one frame of scroll + streaming: applies the player step (dx,dy) to the camera
// (exactly like CtWorldCamFrame) AND maintains the two VRAM tilemaps byte-exact.
// ram = live WRAM (block defs $3000/$3800, grids $4000/$5800).
void CtWorldStreamFrame(CtWorldStream* st, CtWorldCam* c, uint8_t* ram, int dx, int dy);
// C2/0405, the NMI's VRAM queue ($5E lock, $5F length, 8-byte entries at $60): the anim op 6 uploads into the world's
// byte-addressed VRAM image, drained once per frame before the object pass.
void CtWorldNmiVramQueue(uint8_t* ram, uint8_t* vram);

// The per-8px-cell property nibble (C2/62AE): block = BG2grid[$7E5800 + (row>>1)*96 + (col>>1)],
// byte = $7E7000[block*2 + (row&1)], nibble = high if (col&1)==0 else low.
uint8_t CtWorldProp(const uint8_t* ram, int col8, int row8);
// The per-16px-cell REGION nibble (C2/62ED): a 96x64 grid packed two cells per byte at $7E:7200,
// 48 bytes per row. `$7E:1B9B + region` is that region's SONG (C2/2F25) -- both tables already ride
// in the world blob (extract_assets.py pack_worlds), they simply had no consumer until now.
uint8_t CtWorldRegion(const uint8_t* ram, int col, int row);
// C2/2ED9 -- the shared overworld sound-request commit + its $1BEC/$1BED/$1BEE phase gate. The caller
// stages $1BE8-$1BEB (the four mailbox bytes) and $1BED (priority); returns 1 = accepted (the ROM's
// CLC), 0 = refused (SEC).
int CtWorldSoundRequest(uint8_t* ram);
// C2/18F1: the ambient script's PLAY SONG op (0x3D) -- stage cmd $10 <song> FF FF, write the $02AE
// shadow, request via C2/2ED9. Gated on $7F01ED like the ROM. Exposed so a host can replay a
// loader script's effect whose object has already died in the seed (the new-game intro's `3D 11`).
void CtWorldScriptPlaySong(uint8_t* ram, uint8_t song);
// C2/2F94 -- the overworld MUSIC-ZONE object: fade out, wait 60 frames, play the new region's song.
// A normal entry in the ambient-object array, found by its CLASS WORD $2F94 (the generic dispatcher
// C2/0535 uses the class word AS the handler address). `b` = the block base ($4E). Returns 1 = keep
// the object (CLC), 0 = kill it (SEC) -- the caller owns `INC $0003,X` on keep, per C2/054E.
int CtWorldMusicObj(uint8_t* ram, int b);

// Advance one frame. held* = the $F8-word d-pad bits. ram = live WRAM (for the BG2 grid +
// property tables). Outputs the player's pixel step this frame (feed it to CtWorldCamFrame).
void CtWorldMoverFrame(CtWorldMover* m, const uint8_t* ram,
                       int heldUp, int heldDown, int heldLeft, int heldRight,
                       int* dxOut, int* dyOut);
