# Scene scripts / event engine (M2)

Reverse-engineered from CTViewer (`scene_script_decoder.rs`, `scene_script.rs`,
`scene.rs`, the `decoder/ops_*.rs`), verified against the ROM. Implemented so far in
[`assets/script.py`](../../assets/script.py) (parser + actor extraction) and
[`assets/field.py`](../../assets/field.py) (render a scene with its actors).

## Where scripts live
- Scene header byte 8-9 (`u16`) = the **script index**.
- Script pointer table: `0x3CF9F0`, **513 entries**, 3-byte (`u16` offset + bank;
  file addr = `0x10000*(bank-0xC0) + offset`). The script bytecode is **LZ-compressed**.

## Script layout (decompressed)
```
[actor_count u8]
[actor_count × (16 × u16 function pointers)]   ; header_size = actor_count*32
[shared bytecode body]
```
Each function pointer is rebased by `- header_size`, so it indexes the body directly.
A function ends at op `0x00` (Return). Each object's 16 slots: **0=init, 1=activate,
2=touch**, 3..15 = other callable functions. Execution starts at slot 0.

## Opcodes
`op_decode` dispatches one opcode byte to a group handler; each opcode has a fixed
operand length (SNES), with 5 variable-length ops (`0x4E datawrite`, `0x2E bgcolor`,
`0x88 color` carry an inline length/`read_script_blob`; `0xF1 flash`, `0xFF special`
branch on their first byte). `script.py`'s `OPLEN` + `op_len` encode this so a parser
can walk the stream. Notable ops:
- **Load sprite**: `0x80 autobind`/`0x81 char` → `sprite=idx`; `0x82 people` →
  `idx+7` (NPC); `0x83 monster` → `idx+256` (enemy); `0x57/5C/62/68/6A/6C/6D` =
  fixed Crono..Magus (sprite 0..6). (Matches `assets/sprites.py` indexing.)
- **Coordinates**: `0x8B xy` = tile (pixel = `tile*16 + (8,16)`, bottom-center);
  `0x8D dotxy` = `u16,u16` each `>>4` (pixel). `0x8C vxy` = from a variable.
- **Facing**: `0x0F`=up, `0x17`=down, `0x1B`=left, `0x1D`=right, `0xA6 turn` = byte.

## Actor extraction — a real bytecode interpreter
There is no static actor record; the engine *executes* each object's init. `script.py`
now ships a faithful interpreter (`run_init` + `Mem`) instead of a linear walk:
- **Memory** (`Mem`): global `$7F0000` (storyline at byte 0 = 0 at game start), local
  `$7F0200` (cleared per scene), system `$7E0000` (a few game-start seeds); 16-bit
  big-endian. Local operand `i` → `$7F0200 + i*2`.
- **Flow**: decode one op at `current_address`; on a taken jump `current_address =
  op_start + offset` (offset = the decoder constant, e.g. `0x10`→`N+1`, `0x12`→`N+4`),
  else fall through by the op length. **`if` jumps when the condition is FALSE.**
  Compare byte → `== != > < >= <= & |` (0..7).
- **Data/math**: `set/on/off/sset/sets`, `plus/minus/inc/dec`, `biton/bitoff/and/or`,
  `gbiton/gbitoff` (global flags) update memory so conditions evaluate.
- **Actor**: load-sprite ops define the class and set the sprite; `xy` (tile), `dotxy`
  (pixel) and `vxy` (`0x8C`, coords from local memory) set position; turn ops set facing;
  `show/hide/dshow/dhide/kill/battlehide` set the draw mode. An actor is **placed**
  (`visible`) iff a load ran, draw mode is `Draw`, it is not dead, **and it got a
  position** — an object that loads a sprite but never receives coords is not on the field.
Each object's init runs in order over **shared memory** (cross-object calls are no-ops
for now; runs until Return / a forever-yield, capped at 6000 ops). Validated on all
**512 scenes (0 errors, 1901 placed actors)**; the interpreter resolves story-flag
branches — e.g. **Crono's house at game start shows only Mom + the cat** (the cutscene
party objects are correctly `hidden`), while the Millennial Fair stays populated.

### What static extraction can and can't recover (actor positions)
Of the 4820 objects that load a sprite, **1901 get a static position** (the placed set
above). The other ~2919 never receive coords at init: **2884 are the party-member slots**
(sprites 0-6 — placed only when the party arrives, by a runtime event) and **35 are NPCs**
positioned at runtime. Investigated thoroughly: those 35 are *not* statically recoverable —
they use `vxy` reading local memory that is **0 at init** (set later by an event), have **no
coord op of their own** (positioned by another object / the door-entry flow), or are gated
behind a memory flag. Re-running at every storyline (0…0xFF), running the scene init
(obj 0 slot 1) with cross-object follow, and ticking the live engine 180 frames each
recover **none** of scene 4's (Lucca's house) NPCs — they need the actual door-entry event
that seeds their position memory. So 1901 is the static maximum; the rest require the live
event flow (the future C/Python field driver with `vxy` resolving real runtime memory).

## Interactions — cross-object calls + activate/touch → dialogue
`run_func` follows `req`/call ops (`0x02`-`0x04`): it runs the target object's
function inline (recursion-guarded), so calls between objects actually execute. With
`cap`, it records textbox ops: `0xB8` (the 24-bit string-table address, `addr -
0xC00000`) and `0xBB`/`0xC0`-`0xC4` (the shown string index). `simulate_interaction`
(CLI `script.py <rom> talk <scene> <obj> [slot]`) runs the object inits + scene init
(object 0 slot 1, which sets the string table), then runs the object's **activate**
(slot 1) following calls, and decodes the shown strings with [`text.py`](text.md).
Validated: talking to **Taban** (Lucca's house) returns his real lines (*"TABAN: Hey,
Lucca!"*, the password hint), and gated dialogue is correctly skipped at storyline 0.

## Cooperative scheduler + player input (`assets/engine.py`)
`SceneEngine` runs a scene frame-by-frame with CT's real model: per-object
`ScriptState` (16 function pointers, 8 priority return pointers, current priority —
7 = init/idle), a per-frame cycle (delay countdown, up to 5 ops or until a yield),
`req`/return priority switching between objects (`_req` = the exec_call logic:
more-urgent calls save+switch, busier targets queue), wait/yield/stay, and a
**textbox state machine** — a text op (`0xBB`/`0xC*`) shows the decoded string and
**blocks** the actor until `advance()` dismisses it. Player input is `activate(obj)`
(talk → slot 1 at priority 1) and `touch(obj)` (slot 2). `tick()` = one frame.
Validated: `engine.py <rom> 4 talk 8` runs Taban's activate over 9 frames and steps
through both textbox pages (*"TABAN: Hey, Lucca!"* …); `run <scene> N` ticks all
objects N frames without stalling. A front-end (the SDL viewer) would call
`activate`/`touch` on a key press and render `engine.textbox`.

## NPC idle / main loops + per-frame animation
CTViewer's object model: slot 0 holds **both the init and the repeating main loop**.
`run_object_initialization` runs slot 0 until the first `ret` (`0x00`) and leaves
`current_address` **just past it**; the per-frame `run()` then resumes there, so the main
loop (wander / scripted animation) keeps executing. We match this: `run_func` now returns
where it stopped, and `_init_scene` sets each object's `ScriptState.addr` to resume the
loop (past the ret, or parked on a forever-yield) at priority 7. Animation is **decoupled**
from the script (CTViewer `SpriteState.tick`): every frame `tick_anim` advances each
visible actor's animation and stores the assembly frame in `a['frame']`. Modes: `none`
(idle anim 0, or walk anim 1 while a move task runs), `loop` (cycles forever), `loopcount`
(N times), `static` (one frozen frame); the script ops `0xAA animeset` / `0xB3`/`0xB4`,
`0xAB anime`, `0xB7 animerepeat`, `0xAC shape`, `0xAE autoanime` set the mode
(`script.apply_anim_op`, shared by init and the scheduler). `busy()` now means an
interaction is active (a textbox, or an object below priority 7) — idle loops run at
priority 7. `engine.py <rom> <scene> idle <frames> [out]` runs the loops and renders the
live actors; `play.py` composites NPCs **live** (animated, Y-sorted) instead of baking
them. Validated across all 512 scenes (init + 40 ticks, 0 crashes): **832 NPCs animate
and 350 wander**; visibility is byte-for-byte unchanged vs the static init, and talking
still resolves in 9 frames (no regression).

## Collision + player walking
The scene's per-tile **collision** comes from the tile-property byte (`(prop[0]>>2)
& 0x1F`): 0=None, 1=Full, 2-29 = slopes/halves/quadrants, 30=Ladder (`maps.scene_collision`
returns the grid). CT engines compute walkability from these; CTViewer does not, so we do.
`engine.solid_at(type, sx, sy)` resolves it at **sub-tile** resolution (the pixel within
the 16×16 tile): None/Ladder/stairs = open, Full = solid, `LeftHalf`/`TopHalf` = that half
solid, the four quadrants (`SW/SE/NE/NW`) = that quadrant solid, and `Corner45*` = the
matching diagonal triangle (other 22/30/75° slopes are treated as solid). `_walkable_foot`
tests the player's foot pixel against this, so the player walks right up to walls and into
doorways. Validated with `engine.py <rom> <scene> collmap out.png`, which tints solid
sub-regions red over the scene: in Crono's house the walls/counter/table/bed/stairs are
solid and the floor open; in the Millennial Fair the hedges/tents/stalls/fountain are
solid and the stone paths walkable.
The engine adds a **player** actor (Crono) spawned on a walkable tile near
the camera centre; `move_player(dx,dy)` steps it with **per-axis tile collision** (slides
along walls). `engine.py <rom> <scene> walk <r|l|u|d> [out.png]` walks until a wall blocks
it and renders a ghost trail. Validated: Crono walks across the Millennial Fair and stops
at a tent/wall; in Crono's house he is boxed in by the room walls.

## NPC movement-op physics
Each actor has a `move_speed` (default `(1/18)*16`) and a `task` (`move_by`, `cycles`).
`run_tasks()` (every frame) does `pos += move_by; cycles -= 1` (CTViewer Actor::run_task).
The script move ops set/advance the task and **block (yield) until it reaches the
target**, matching CTViewer's exec: `move_by = move_speed*0.5*(cos a, sin a)`,
`cycles = step operand or the script delay (4)`:
- `0x92 movi` / `0x9C`/`0x9D` → MoveByAngle (byte*1.40625°, N steps; floor+complete when done).
- `0x96 move` / `0xA0 fmove` / `0x97 vmove` / `0x9A smove` → MoveToTile (recompute the
  tile→tile angle each leg until the actor's tile == the destination tile).
- `0x94 mova` → MoveToActor (target another object's tile). `0x89 mspeed` sets speed.
`engine.py <rom> <scene> npcwalk <obj> <r|l|u|d> [out]` integrates a fair NPC's move and
trails it across the map. (CTViewer applies no collision to NPC movement; we match that —
only the player walk collides.)

## Sprite animation (facing + walk cycle)
A sprite's header byte 3 is its **animation-set** index. The 7-pointer meta @`0x24FFE0`
gives the anim-frame (slot) and anim-duration tables (194 each); a set's slot data is
`[facing0][facing1][facing2][facing3]` blocks of `data_per_facing` bytes, where animation
A (0=idle, 1=walk, 6=run) sits at `A*4` and frame f at `A*4+f` (≤4 frames; a 0 duration
ends it). **Facing does not pick a different animation** — each frame stores 4 assembly-frame
numbers (one per facing U=0,D=1,L=2,R=3) and the facing picks one. `sprites.pick_frame(rom,
sprite, facing, walking, tick)` returns the assembly frame; `field.py`/`engine.py` use it so
actors face their script direction and the player/NPCs play a walk cycle while moving. e.g.
Crono: idle frames U/D/L/R = asm 3/0/1/2; walking Down cycles asm 4,5,6,7.

## Doors / scene transitions (exits)
Per-scene **exit** table @`0x250000` (512 scenes, local `u16` pointers, ordered);
`maps.scene_exits` parses the 7-byte SNES records: `x,y` (×16 px), `size_bits`
(`side = ((bits&0x7F)+1)*16`; bit7 ⇒ vertical 16×side else horizontal side×16),
`dest_index+facing` (`u16`: index `&0x1FF`, facing `(&0x600)>>9`, ∓8 px shift bits
`0x800`/`0x1000`), `dest tile x,y`. `index` `0x1F0..0x1FF` ⇒ overworld, else a scene
at `(tx*16+8, ty*16+15)`. `engine.exit_at()` returns the exit under the player (a ±8 px
margin, CTViewer's test). The live viewer reloads the engine for the destination scene
and drops the player at the entry pos/facing (20-frame cooldown so it doesn't bounce).
`engine.py <rom> <scene> exits` lists them; `door <scene> out.png` simulates the first
exit and renders the arrival. Validated: Crono's house → scene 2 (the player arrives by
the stairs); the fair links to the overworld + adjacent scenes.

## Z-plane (layered collision) — VERIFIED against the game + enabled
The runtime gating rule is now confirmed from the **real CT routine** (bank C0, found via
the documented z-plane WRAM var `$7E0155`); see [`zplane.md`](zplane.md) for the full
disassembly. Tile z-plane = `prop_byte2 & 3` (C0/9B41). Gating (C0/9C3B): a tile is solid
by z-plane iff `z==0` or (`z != player_plane` and `z != 3` and `player_plane != 3`) — the
level you are not on is solid, transition-solid (0) always solid, your plane / transition
(3) passable. The player's plane (`$7E0155`) is set to the plane of the tile walked onto
unless it has `IGNORE_Z` (`byte2 & 0x04`) — so crossing a `z==3` tile flips you 1→3→2.
`engine._zsolid` implements this; `_walkable_foot` = `zplane_solid OR shape_solid`;
`move_player` updates the plane. Validated on scene 7 (two-level court): a plane-1 player
is blocked from plane-2 floor and free on plane-1 floor, and the same tile becomes
passable on plane 2; single-plane scenes (1, 5) are unchanged.

## Z-plane (parsed + visualized)
The tile-property byte 2 also carries a 2-bit **z_plane** and three flags
(`COLLISION_IGNORE_Z` 0x04, `COLLISION_INVERTED` 0x08, `Z_NEUTRAL` 0x20); byte 1 bit
0x10 = `DOOR_TRIGGER`. `maps.scene_props` returns `(coll, zplane, zflags)`. CTViewer only
*parses* these (its sole consumer is the debug overlay), so the runtime rule is the
author's annotation, **unverified**. `engine.py <rom> <scene> zmap out.png` renders the
CTViewer-style overlay (plane 0=blue, 1=green, 2=red, 3=yellow; door tiles=magenta).
What it shows: **plane 1 is the normal/ground plane**, **planes 1 & 2 are the two stacked
levels** (e.g. scene 7 is a two-level court — plane 1 upper garden, plane 2 lower yard,
plane 0 the transition band between them), and **0/3 are transition seams** (in Crono's
house the staircase + door thresholds are plane 0).
Inspection of the data (scene 1: `{1:3850, 0:223, 3:11, 2:12}`) shows plane 0 also covers
ordinary floor edges, so the naive "stepping on plane 0/3 toggles the player's plane"
rule would wrongly flip the plane on normal ground and break movement. Gating collision
on the player's plane therefore needs the real SNES routine (oracle) to pin down (a) the
compare against the player's stored plane and (b) when the plane is updated — until then
collision stays shape-based (the validated behaviour) and the z-plane data is exposed +
visualized for that future work. 128 scenes use both planes 1 and 2.

## Door-entry / event-state driver (populating runtime NPCs)
The 35 runtime NPCs (and the empty-looking rooms like Lucca's house) are gated on **global
story flags** (`ifg` on `$7F0000+` bits), *not* the storyline byte — their init only runs
the coord op when the flag is set. So a scene populates as the **accumulated story state**
dictates. `SceneEngine(rom, scene, gmem=...)` seeds the global flags; `enter_scene(scene,
pos, facing)` is the **door-entry driver** — it loads the destination scene **carrying the
current global flags forward** and drops the player at the entry. `play.py` uses it on every
door transition, so flags set during play persist across scenes (the architecturally-correct
way rooms become populated). `engine.py <rom> <scene> populate [flagsHex] [out]` seeds the
flags to show a scene's populated state: **Lucca's house (scene 4) goes from 0 actors at
flags 0x00 to 5 (incl. Lucca, sprite 2) at flags 0xFF**, placed sensibly in the room. (Some
flags also *hide* NPCs in other scenes — the exact per-scene story state is what the live
event flow accumulates; `populate` with all-flags is a maximal-reveal demo, not that exact
state.)

## Real story state from the oracle
The exact per-scene population is whatever global story flags ($7F0000) hold at the moment
you're there — which the real game accumulates as you play. `ct.exe --dumpflags <out>
[--frames N]` boots the embedded oracle (LakeSnes) and dumps its `$7F0000-$7F0200` (0x200
bytes) — the actual game flags. `engine.py <rom> <scene> populate @<file> [out]` seeds the
engine with them, so a scene renders at the **real** state, not a synthetic all-flags fill.
Cross-validated: the oracle's game-start flags (boot to title: only 1 byte set) feed back
to the engine and reproduce the default population **exactly** — Crono's house = 2 (Mom +
cat), the fair = 19, Lucca's house = 0 (empty at game start). So the field engine's
flag-gated placement matches the real game's WRAM at game start. To populate a later scene
(e.g. Lucca's house once you visit it), capture flags from the oracle at that story point
and `populate @` with them — the same mechanism.
`ct.exe --playflags <out> [--frames N]` drives the oracle (mashing Start to begin a new
game, A to advance text, Down to walk) and dumps the resulting flags — a self-contained way
to reach a *real* in-game state: validated, the flags progress past boot (1 → 4 non-zero
bytes as the intro runs) and `populate @` renders consistently with them. Button-mashing
only reaches the very early game, though (precise navigation to a deep story point needs a
recorded input movie), so the early-game flags don't yet change the field scenes vs boot.
**So there are two paths to a populated scene**: `atlas.py` (self-contained greedy
approximation, available now — see above) and this oracle-flag capture (exact, but a deep
state needs a user playthrough / input movie; any emulator's `$7F0000` dump works too).
Both feed the same `populate @<file>`.

### Savestates (resumable story snapshots)
`ct.exe --savestate <out> [--frames N] [--mash]` saves the oracle's **full emulator state**
(LakeSnes `snes_handleState` — CPU/PPU/DMA/APU + the whole 0x20000 WRAM) to a file, and
`ct.exe --loadstate <state> --dumpflags <out>` restores it and dumps `$7F0000`. So a story
point is snapshotted once and re-loaded any time to read its flags — the populate input.
The savestate is deterministic (two identical runs produce byte-identical states) and the
save→load→flags round-trip reproduces the in-place flags exactly. The **native field
consumes a captured state directly**: `Belthasar --assets ct_assets.dat --scene N --loadflags
<flags>` seeds the scene's story flags, so you walk the scene populated as it really is at
that savestate. Workflow for a deep populated scene: drive/play the oracle to the story
point → `--savestate` → `--loadstate … --dumpflags` → `Belthasar --loadflags` (or
`engine.py populate @`).

## Population atlas (`assets/atlas.py`)
For an exact populated state you need the story flags from a savestate at that point; as a
self-contained approximation, `atlas.py build` does a **greedy search over the flags each
scene actually checks** (`ifg`/0x16 operands) and keeps each that increases the placed-actor
count — the fullest sensibly-reachable population per scene. Result: **+267 actors across 92
scenes** (game-start 1901 → 2168). `atlas.py render <scene> <out>` draws a scene at its atlas
state. The top gainers (e.g. scene 12 an inn 6→16, 449 10→17, Lucca's house 4 0→5) were
**adversarially verified by a 14-agent vision workflow**: 11/14 are coherent populated rooms
(NPCs on walkable floor), and 3 were correctly flagged — 449 over-populates (~8 NPCs in the
void, a cutscene state the greedy max-placed wrongly triggers), 163 has a 2-sprite overlap,
320 reports a phantom count (no NPC sprites actually visible). So the greedy atlas is a good
*approximation* (most rooms populate believably) but "max placed" can over-trigger; the exact
per-scene state still wants real flags (`ct --dumpflags` on a savestate). The atlas + renders
are derived artifacts (git-ignored).

## Live interactive viewer (`assets/play.py`)
A Python front-end to the engine via **ctypes → SDL2.dll** (`third_party/SDL2`, no new
deps). `field.compose_scene` renders the map + NPCs once into a base image; each frame
only the camera crop + the player are recomposed (so it's smooth in pure Python). The
loop: poll events, read held arrow keys → `move_player` (tile collision), `tick()` the
engine (NPC scripts / interaction / textbox), pick the player's walk/idle frame by
facing, blit it, follow the camera (one SNES screen, clamped), upload to an SDL texture,
present at ~30 fps. **Arrows walk, Space/Z talks to the faced NPC / advances text, Esc
quits.** Talking activates that object (slot 1) and the textbox blocks until advanced.
Validated headless via `--selftest` (dummy driver); `PLAY_SHOT=out.png … --selftest`
captures the camera view (Crono standing in his house with Mom + the cat).

**On-screen dialogue font** (`assets/font.py`): the real CT 12px dialogue font glyphs live
at ROM `0x3F2F60` as SNES 2bpp planar tiles (8×12 px, 24 B/glyph; `glyph = byte - 0xA0`).
`font.draw_text` maps each decoded character back to its glyph (inverting
`text.parse_character`), advances by the glyph's inked width, word-wraps, and blits with
the CT light text + a 1px drop shadow. `play.py` now draws the dialogue window + the
rendered text instead of a bare panel. Validated: talking to Taban shows
*"TABAN: Hey, Lucca!"* on-screen in the authentic font (`PLAY_TALK=<obj> … --selftest`).

## Field assets packed into ct_assets.dat
The field data the engine computes is now packed into `ct_assets.dat` (container bumped
to **version 3**) so a native C field engine can run ROM-free, via five packers in
`extract_assets.py` + types in `assets.h`:
- `CT_ASSET_ACTORS` (10, per scene): `[count u8]` then `{obj u8, sprite u16, x u16, y u16,
  facing u8}` — the interpreted, visible+positioned actors.
- `CT_ASSET_COLLISION` (11, per map): `[w u8][h u8]` + w*h collision-shape bytes + w*h z
  bytes (zplane bits0-1, IGNORE_Z/INV/Z_NEUTRAL/DOOR bits2-5).
- `CT_ASSET_EXITS` (12, per scene): `[count u8]` then `{x,y,w,h u16, kind u8, dest u16,
  dx,dy i16, facing u8}`.
- `CT_ASSET_SPRITEANIM` (13, per sprite): idle then walk — `[n u8]{U,D,L,R asm u8, delay u8}`.
- `CT_ASSET_FONT` (14): the dialogue 12px glyphs 0..83, raw SNES 2bpp (2016 bytes).
The C loader reads them generically (`CtAssetGet(type, id)`); `ct.exe --datfield <dat>
[--scene N]` summarizes a scene's packed field (e.g. the fair = 19 actors, 3 exits,
48×64 collision; sprite-anim idle/walk counts). All five round-trip byte-for-byte against
the Python source. A sixth, `CT_ASSET_SPRITE` (15, per used sprite), packs the **sprite
graphics + assembly + palette** (decoded 8×8 tiles, the per-frame chip list + signed
offsets, the 16-colour palette; bounded to the tiles/frames the idle+walk anims touch —
323 sprites). `CtComposeSprite` (scene.c) ports `compose_frame`: it lays the assembly's
chips (each a quadrant of a 16×16 unit, with flip-x/y) into a 96×96 index buffer at the
O=40 anchor. Build: 8.5 MB, 3031 entries. NOTE: actor *positions* come only from the
`xy`/`dotxy` init ops, so scenes that place NPCs another way (e.g. Lucca's house) pack 0
positioned actors — a pre-existing actor-extraction gap, not a packing one.

## C scene-script VM (`src/ct_vm.c`)
The scene-script interpreter is now ported to C — the foundation of a fully native (no
Python) field engine. `pack_scripts` packs each scene's **decompressed bytecode** into the
.dat (`CT_ASSET_SCRIPT`, 489 scripts). `CtVmResolveActors` is a faithful port of
`script.py run_func` (init subset): the `Mem` model ($7E/$7F, 16-bit big-endian, the
game-start system seeds), `op_len` (the OPLEN table + the 5 variable-length ops), and the
op interpreter — flow (jumps + the `if`/`ifg`/`sif`/`case` conditions, jump-when-false),
the data/math/bit ops (so conditions evaluate), and the actor ops (load-sprite, `xy`/`vxy`/
`dotxy`, facing, `show`/`hide`/`kill`). `ct.exe --vm <dat>` runs it. **Validated: across all 512 scenes the C VM resolves the
identical placed-actor set as the Python VM — 512/512 scenes, 1901 actors, byte-for-byte
(sprite, x, y, facing).**
On top of the interpreter, `CtSceneCreate`/`CtSceneTick` port the **per-frame scheduler**
(engine.py `tick`/`_step`/`run_tasks`): the init→main-loop resume, the `ScriptState` (delay
countdown, 5-op budget, Return/priority pop, yield/wait, `req` priority switch), the
movement tasks (`move`/`movi`/`mova`/… set a per-frame `move_by`+cycles, `run_tasks`
integrates), and `mspeed`. `ct.exe --vm <dat> --scene N --frames F` ticks the live scene.
**Validated: the C scheduler reproduces the Python engine's live state byte-for-byte
(sprite, x, y, facing) across scenes 1/2/4/5/8/80/170/176 over 90 ticks** (after fixing the
last divergence — the move-speed op). Surfaced + fixed a latent engine.py bug along the way:
`a['visible']` was set at init and not recomputed during ticking, so an actor shown/hidden
mid-scene rendered wrong; `tick()` now refreshes it live. The remaining layer (an SDL input
loop in C) is the last step to a self-contained interactive C field.

## Native C field (`src/field.c`, `Belthasar`)
The whole field now runs in native C + SDL2 with no Python — `play.py` ported. `Belthasar
--assets ct_assets.dat [--scene N]` opens an SDL window and per frame: reads held arrow
keys and walks Crono with the verified sub-tile + z-plane **collision**, ticks the **C VM
scheduler** (`CtSceneTick` — NPCs run their scripts + move), composites the camera view
(map base from `CtRenderSceneFromAssets` + every live NPC and the player as **real sprites**
via `CtComposeSprite`, Y-sorted), follows the camera, and presents at ~30 fps. Everything
reads `ct_assets.dat` — no ROM, no Python. Built as `Belthasar` (CMake, when SDL2 is present).
**Now feature-complete vs play.py — textbox + doors + walk animation are native:**
- **Walk/idle animation**: `pickAnimFrame` reads the packed `CT_ASSET_SPRITEANIM` (idle then
  walk frames) and selects the assembly frame by facing + a per-actor tick, choosing walk vs
  idle from `CtSceneActorMoving` (an active move task). NPCs free-run their anim (idle breathing
  + walk cycle); the player resets its tick on idle (play.py `moving_anim`). Validated: the C
  frame-pick is byte-identical to `sprites.pick_frame` over the ROM (all walk/idle × 4 facings
  × 24 ticks; Crono walk-down cycles 4→5→6).
- **Talk / dialogue**: press Space/Z/Enter facing an NPC → `CtSceneActivate` runs its activate
  fn (slot 1, prio 1); the scheduler hits a text op, captures the string index + the table
  (snapshotted at op time), and the field draws the CT-font textbox window, decode via
  `CtAssetString(CT_ASSET_DIALOGUE, table, index)`. **Polished to CT feel**: a **typewriter
  reveal** (~2 chars/frame; pressing the key fast-forwards the reveal), **multi-page** paging
  for long strings (`paginate` breaks at the CT page-clear codes `0x0B`/`0x0C` — decoded to a
  `\f` sentinel so the box clears exactly where the game does — and otherwise wraps to 3
  lines/box honoring `\n`; a blinking **▼** marks more; the key turns the page) and finally
  dismisses on the last page. (Player
  faces the NPC implicitly — you talk in your facing direction — and the NPC's turn-to-face is
  script-driven by the VM.)
  The dialogue table is pre-seeded from obj 0's init/scene-init (`captureTable` scan of the 0xB8
  set-table op — which slot varies by scene). Validated: talking to Taban (Lucca's house) shows
  "TABAN: Hey, Lucca!", the exact line `engine.py 4 talk 8` returns.
- **Decision / choice boxes** (textbox v2): op **`0xC0`** (len 3) is a decision box — the decoded
  string is a prompt then indented option lines ("Yes."/"No."). The VM flags it
  (`CtSceneTextboxChoice`) and remembers the owning object; `drawChoiceBox` renders an
  auto-height window with the options typed out, then a **▶ cursor** on the current option that
  Up/Down move; pressing select calls `CtSceneChoose(option)`, storing the 0-based option as the
  owner's **`result`** and dismissing the box — so the owner's script branches down the chosen
  path via op `0x1A` (`if result == N`). RE'd from the ROM scripts (the inn "Lodging is 10 G. /
  Check in? / No, thanks." has `result==1` = decline) — confirmed by the indented-option layout
  across 1- and 2-line prompts — and validated in `Belthasar` (selftest `--talk`/`--choose`/
  `--select`): the ▶ renders/moves on both options and selecting dismisses + continues the script.
  **Multi-page decisions** (textbox v3): when the prompt has a page break (`\f`), the box pages
  through the prompt segments (▼) and shows the cursor on the options segment. Validated: scene 29
  (the line about a secret document) → (a Yes/No question) —
  choosing Yes runs the read-it branch (the scene gains an NPC), proving result→`0x1A` end-to-end.
- **Doors**: walking onto a scene exit (`CT_ASSET_EXITS`, ±8 px) reloads the destination
  (`fieldLoad`) carrying the live story flags forward (`CtSceneGetFlags`), drops the player at
  the entry pos/facing, with a 20-frame cooldown. Validated: Crono's house → scene 2.
- **Tile Z-priority** (foreground over sprites): the metatile priority bit (tilemap `0x2000`)
  was already parsed but the renderer baked everything flat, so sprites drew over all tiles.
  Now `CtComposeScene` also outputs a **priority mask** of the BG1/BG2 priority-1 pixels
  (`CtRenderFieldBase`); the field re-draws those over the sprites after the Y-sorted pass, so
  the player/NPCs walk **behind** counters, doorway arches, stairs, treetops, etc. (BG3
  priority-1 stays background, matching the mode-1 order vs the OBJ-priority-2 field sprites.)
  Verified by tinting the mask (`--showprio`): in Crono's house the walls/stairs/counter are
  foreground while the floor and rug stay walkable, and Crono placed at the stairs is correctly
  occluded; the fair's NPCs stay visible on the ground. (`--at X Y` places the player to test.)
- **Position-based sprite priority** (verified disasm C0/9B4D-9B61 + C0/9D5D-9D67): the *static*
  foreground above is only the default. The same field routine that does z-plane collision also
  sets each sprite's OAM priority **from the tile it stands on**, per half: top half =
  `$6E = ((prop0>>2)&0x10)|0x20`, bottom half = `$6F = ((prop2>>2)&0x10)|0x20` — `0x20` = OAM
  priority 2 (behind the foreground, the default) or `0x30` = priority 3 (IN FRONT of it) when
  that byte's bit `0x40` is set. So *where you stand* decides, per sprite half, whether you're
  in front of or behind the foreground tiles (head behind a railing, feet in front). The two
  flags are packed into the collision asset (top = `shape & 0x10` = prop0&0x40, already present;
  bottom = a new `zb & 0x40` = prop2&0x40); `footPrio` reads them and `drawSprite` marks those
  sprite-half pixels so the foreground overlay skips them. Selective (~2-19% of tiles; scene 7
  the throne room 19% bottom). Validated: Crono on a prio-3 tile (scene 1 @440,127) draws in
  front of the bookshelf (55 foreground px skipped); on a normal tile below, behind. The
  half-split (sprite buffer y=24) is approximate pending the byte-exact OAM/sprite port.

Adversarially verified by a 5-reviewer workflow (textbox/door/anim/safety/completeness); the
real bugs it found were fixed: a `CtAssetString` out-of-bounds read (now bounds-checked), NPC
idle anim freezing (tick now free-runs), and the textbox reading the table at display time
instead of op time (now snapshotted). The VM changes are read-only w.r.t. the scheduler, so the
512/512 init + live-tick cross-checks against the Python engine still pass.

## C field engine (headless demo) — consumes the packed field (no ROM)
`ct.exe --field <dat> [--scene N] [--out f.raw]` (src/main.c `runField`) drives the field
entirely from `ct_assets.dat`: it renders the scene via `CtRenderSceneFromAssets`, places
the actors from `CT_ASSET_ACTORS` (markers), outlines the doors from `CT_ASSET_EXITS`,
walks a player with **collision ported from the engine** (`cSolidShape` = the sub-tile
shapes, `cZSolid` = the verified C0/9C3B z-plane gating) reading `CT_ASSET_COLLISION`, and
renders a status line + a real dialogue string (`CT_ASSET_DIALOGUE`) in the **CT font**
decoded from `CT_ASSET_FONT`. Actors are drawn as **real composited sprites** via
`CtComposeSprite` (the standing frame picked per facing from `CT_ASSET_SPRITEANIM` + the
`CT_ASSET_SPRITE` graphics), not markers. Validated: scene 5 (the fair) renders from the
.dat with **19 real NPC sprites**, the exit outlined, a 148 px collision-stopped walk
trail, and on-screen dialogue — no ROM touched. (A full native field would still add input
+ the script VM in C.)

## TODO (next steps)
- Sound (the SPC700/DSP is still emulator-only — the C side would feed sequences); or a new
  subsystem milestone: menus + save (M3) or battle (M4).
- Native field nicety: smooth scroll within a page (CT scrolls line-by-line) vs the current
  page-flip; shop/equipment menus behind the "buy?" decisions.
- Deeper populated states: capture flags from an oracle savestate / recorded input movie at a
  story point, then `Belthasar --loadflags` (mash only reaches the early game).
