# OAM / sprite rendering — byte-exact port (in progress)

Goal: port CT's sprite/OAM build to C, validated frame-by-frame against the oracle (the
project's core method). The native field engine composites sprites *approximately*; this
milestone reproduces the *exact* OAM the game builds, so the half-priority split and every
sprite attribute match the real machine.

## Tooling built for this (all in `ct.exe`)
- `--dumpoam <out> [--frames N] [--mash]` — run the oracle N frames (optionally mashing into a
  new game), then dump the **canonical OAM** (128 sprites x {X,Y,tile,attr} = 512 bytes, then
  the 32-byte high table) to `<out>`, plus the full WRAM to `<out>.wram`. The ground truth.
- `--findoam [--frames N] [--mash]` — mash N frames, then log 60 frames of CPU writes and
  report which PCs write the shadow OAM buffer, and the DMA channels' state. Finds the routine.
- The CPU **write log now captures the writer's PC** (`CtWrite.bank/pc`, set in the write hook
  from `mem`=the `Snes*`) — reusable for locating *any* routine that writes a target region.

## Fixed: the mash input was pressing the wrong buttons
LakeSnes `input_read` shifts out **bit 0 first** (B), and the auto-joypad read maps
`currentState` bit *i* -> register bit `15-i` (snes.c:244-251). So the serial-order bits are
**Start=0x08, A=0x100, Down=0x20** — but the old mash used the `$4218` register-order bits
(0x1000 / 0x80 / 0x400), i.e. it pressed Right + L + nothing and never started the game. Fixed
in `runPlayFlags` / `runSaveState` / `runDumpOam` / `runFindOam`. (This also explains why the
old "mash to flags / savestate" only ever captured the boot state.) With the fix, mashing now
reaches a new game (the 1999 AD intro flyby is visible by frame ~3000, with real character
sprites in the OAM — e.g. spr 28-35, and spr 30-31 at OAM priority 3 = the bottom-half-in-front
case our position-based priority reproduces).

## Found + confirmed: the shadow OAM is at `$7E0700`
Confirmed THREE ways:
1. Matching the live PPU OAM against the WRAM dump is an **exact 512/512 + 32/32** hit at
   `$7E0700` (512-byte main, 128 x {X,Y,tile,attr}) + `$7E0900` (32-byte high table).
2. The **OAM DMA source is `$00:0700`** (channel 7, B-bus `$2104`/OAMDATA), set up in the NMI
   at **C0/ECCB** — and `$00:0700` is the low-WRAM mirror of `$7E0700` (banks `$00-$3F`/`$80-$BF`
   `$0000-$1FFF` mirror `$7E0000`). So the game DMAs the 544-byte OAM from `$7E0700` each frame.

## Reaching gameplay: the timed input script (`--script`)
Mashing only ever lands in static cutscenes (the OAM is not rebuilt there). A **user-provided
timed input script** drives the oracle from boot to **1000 AD gameplay** (Crono's house, Mom:
"I want you to behave yourself today!") where the player is controllable and the OAM is rebuilt
every frame. Encoded in `ctDriveScript` (`CT_PLAY_SCRIPT`, ~60 fps, serial-order bits): wait 4s,
B, wait 3s, A, wait 2s, Start, wait 1s, Start, wait 40s, then A x8 with the given waits.
`--dumpoam`/`--findoam`/`--fb` take `--script` to use it.

## Found: the OAM-build routine at C0/B330-B3FF
With `--script`, `--findoam` catches **960 writes to the OAM buffer from 3 PCs** (C0/B34B,
C0/B3A1, C0/B3C5 — i.e. the `STA $0000,X` at B348/B39E/B3C2). The routine copies **assembled
sprite OAM bytes** from `$7F4F00,X` into the OAM buffer (`$00:0700` via the write pointer), one
priority group per pointer:
```
B341: LDA $7F4F00,X ; B345: LDX $0181 ; B348: STA $0000,X ; INX ; STX $0181   ; group A
B397: LDA $7F4F00,X ; B39B: LDX $0185 ; B39E: STA $0000,X ; INX ; STX $0185   ; group B
B3BB: LDA $7F4F00,X ; B3BF: LDX $0189 ; B3C2: STA $0000,X ; INX ; STX $0189   ; group C
```
plus a loop (B36E) that writes 4 high-OAM bytes from `$7F4BC0,X` via `$2180`/WMDATA (DP=$2100,
`STA $80`). Driven by the per-object sprite state at `$0F80,X` (visibility/priority bits, the
`AND #$0C` test) + `$1700,X` (sprite OAM-data pointers) + `$016D` (object index). So the field
sprite -> OAM pipeline is:
1. **assemble** `$7F4F00` (main OAM bytes) + `$7F4BC0` (high OAM) from the sprite objects — an
   earlier stage, not yet disassembled;
2. **copy** `$7F4F00`/`$7F4BC0` -> the OAM buffer `$7E0700` with priority grouping (C0/B330);
3. **DMA** `$7E0700` -> PPU OAMDATA in NMI (C0/ECCB, ch7).

## Stage 1 disassembled: the field sprite engine (C0/B701, per-sprite C0/B788)
Disassembled accurately via the oracle trace at gameplay (`ct.exe --trace N --script` ->
`disasm65816.py`, which carries the real M/X flags). The per-frame OAM build entry is **C0/B309**,
which runs both stages:
- **`JSR $B701` = stage 1 (assemble).** `C0/B701` loops the sprite objects by index `$6D`: it
  reads the object type/visibility (`$1201,X & 3`, `$1B00,X`) and, for each live sprite, calls
  **`C0/B788`**. B788 sets DB=`$7F` and reads that sprite's state — **X = `$0A00,X`** (-> `$C3`),
  **Y = `$0A80,X & $1FF`** (9-bit, -> `$C5`), **OBJ-tile base = `$1700,X`** (-> `$D9`) — then loops
  the sprite's OBJ tiles (step 8, `B7AA`): each tile's X = a per-tile offset `$4BC2,X` + the sprite
  X (`$C3`) -> stored to the assembled-OAM scratch `$7F4BC0,X`, accumulating the X-high bit into a
  per-sprite high byte stored at `$7F4F00 + $6D` (`B7D4`); then the Y bytes = `$4BC4/$4BCC/$4BD4/
  $4BDC,X` + the sprite Y (`$C5`) -> `$7F4BC1/$7F4BC9/$7F4BD1/$7F4BD9,X` (`B882-B8C3`). So stage 1
  turns the per-object sprite state into the assembled-OAM working buffers (`$7F4Bxx` per-tile
  X/Y, `$7F4F00` high bits).
- **stage 2 (copy, C0/B30F-BA61)** then sets DP=`$2100` and, grouped by the object priority
  (`$0F80,X & $0C`; `BIT #$04` selects group A/B/C -> the write pointers `$0181/$0185/$0189`),
  copies `$7F4F00` -> the OAM buffer `$00:0700` (main) and the high bytes via `$2180`/WMDATA.
- the NMI (C0/ECCB, ch7) DMAs `$7E0700` -> PPU OAMDATA.
The full trace-driven listing is regenerable to `disasm/notes/oam_stage1.asm` (gitignored).

## Inputs pinned — the sprite-engine I/O map (authoritative; supersedes the main/high guess above)
Confirmed by cross-referencing the live OAM with WRAM at gameplay (`--dumpoam --script`) and by
`--findoam --watch`. **Note: `$7F4BC0` is the assembled MAIN OAM and `$7F4F00` is the HIGH OAM**
(the earlier sections had these swapped — the static disassembler had mis-tracked the flags).

Per-object sprite tables, parallel arrays indexed by `$6D` = object*2 (16-bit X/Y; live objects
in Crono's house were idx 0x10=Crono, 0x12, 0x16):
- `$7E0A00 + idx` = sprite **screen X** (16-bit) — verified == the OAM X (idx 0x10 -> 137 = spr 28).
- `$7E0A80 + idx` = sprite **screen Y** (9-bit, `& $1FF`).
- `$7E1700 + idx` = the **OBJ-assembly base** (offset into `$7F4Bxx`; 0x320 for Crono). Quasi-static
  (0 writes/frame — set when the animation frame changes), so a read input for the port.
- `$7E1201 + idx` = type / visibility (`& $03`); `$7E1B00 + idx` = a flag; `$7E0F80 + idx` =
  **priority** (`& $0C` -> 3 groups; ties to the position-based OAM priority RE'd at C0/9B4D).

OBJ-assembly scratch at `$7F4BC0 + base`, **8 bytes per OBJ tile** (verified vs Crono's OAM):
`[0]=final X`, `[1]=final Y` (written by stage 1 = sprite X/Y + the offset), `[2]=X offset`,
`[4]=Y offset`, `[6]=tile`, `[7]=attr` (offset/tile/attr = the template loaded upstream by the
animation system from the sprite's frame). The HIGH-OAM byte is assembled to `$7F4F00 + $6D`.

Pipeline (all confirmed): **stage 1** (C0/B701 -> B788) reads the object tables + the `$7F4Bxx`
template and writes `[0]/[1]` (final X/Y) + `$7F4F00` (high); **stage 2** (C0/B309 -> B330) copies
`$7F4BC0` `[0][1][6][7]` -> `$7E0700` (main) via `$2180`/WMDATA and `$7F4F00` -> `$7E0900` (high)
via the 3 priority-group pointers; **NMI** (C0/ECCB ch7) DMAs `$7E0700` (544 B) -> PPU OAMDATA.

## Stage 1 ported + validated BYTE-EXACT (`--checkstage1`)
The per-sprite assembly (C0/B788) is ported to C and proven against the oracle: for each real
OBJ slot (base = `$1700[idx]`, 0x20-aligned), for each of its 4 tiles t (entry at `$7F4BC0 +
base + t*8`): **finalX = (objX(`$0A00[idx]`) + Xoff(`[+2]`, 16-bit signed)) & 0xFF**, **finalY =
(objY(`$0A80[idx]`&$1FF) + Yoff(`[+4]`, 8-bit signed)) & 0xFF**, and the per-object high byte =
**`0xAA | E5`** where E5 accumulates each tile's X-bit-8 two-bits-at-a-time over the B7AA loop
(tile 3 first). Validated by recomputing from the WRAM inputs and comparing to the oracle's
assembled `$7F4BC0[0]/[1]` + `$7F4F00` over 180 gameplay frames: **540 objects / 2160 tiles, 0
mismatches** (X, Y, and high). `ct.exe --checkstage1` (drives `--script`, walks, checks). (Real
objects are at low idx with 0x20-aligned bases like Crono idx 0x10 base 0x320; junk `$1700`
slots have tiny/odd bases and are filtered.)

## Stage 2 ported + validated BYTE-EXACT — the full field OAM build (`--checkoam`)
The OAM-build entry (C0/B276) inits the per-group OAM write pointers to **constants** (DP=$0100):
main `$01DB=$0770` (default -> OAM spr 28), `$01DD=$08A0` (group B -> spr 104), `$01DF=$0710`
(group C -> spr 4); high `$0181=$0907`, `$0185=$091A`, group C high `$0901`. The objects are
iterated as **linked chains**: the **Y-sorted sprite list `$7E0E00`** (byte entries, `LDY #$7E`
down by 2, `BMI` skips empty) gives each chain's HEAD, and `$7E0E81[obj]` is the NEXT object in
the chain (negative = end) — C0/B2A8 + C0/B2B4. Per object, C0/B309 runs stage 1 (`JSR $B701`,
assemble) + stage 2 copy (B30F-BA61): pick the group by priority (`$0F80[idx] & $0C`: 0=default,
&4=B, else=C) and copy the object's 4 tiles `[0][1][6][7]` (X,Y,tile,attr) -> the group's main
pointer (+0x10) and its high byte (`$7F4F00[idx]`) -> the group's high pointer (+1). (OBJ slot
bases `$1700[idx]` are 0x20-aligned; **base 0 is valid** — the first slot, e.g. the group-B obj.)
`ct.exe --checkoam` ports the whole pipeline (stage 1 recompute + stage 2 grouped copy via the
`$0E00`/`$0E81` chains) and compares the built OAM to the oracle's `$7E0700`/`$7E0900`: **12240
bytes over 180 gameplay frames, 0 mismatches, 100% coverage (2880 built == 2880 oracle visible
sprites; all 3 priority groups)**. The field sprite -> OAM build (stages 1+2) is reproduced
byte-for-byte against the real machine.

## Production function `CtBuildFieldOAM` (`src/oam.c`)
The validated build is now a reusable C function `CtBuildFieldOAM(ram, oamMain, oamHigh,
wroteMain, wroteHigh)` (`src/oam.h/.c`) — it reads the sprite-object WRAM and writes the visible
field sprites' OAM slots, marking which it wrote. `--checkoam` calls it and compares to the
oracle: still **0 mismatches, 100% coverage** (11520 main bytes == the oracle's visible sprites).
The `--vm` 512/512 regression is unaffected.

The full 512-byte `$7E0700` is NOT a clean target for the per-frame build: only ~91 unused slots
are the true clear `{0,0xE0,0,0xE0}`; the rest of the hidden slots are **stale** (Y forced to
0xE0 but X/tile/attr left over from when the object was last visible). So the unused-slot bytes
are persistent OAM state across frames, not output of this build — `CtBuildFieldOAM` correctly
writes only the live objects' slots (which it reproduces byte-for-byte).

## Upstream RE: the Y-bucket sprite-order list (`$0E00`/`$0E81`)
The OAM build's iteration order comes from a **doubly-linked Y-bucket list** built by C0/A988..AA06:
- `$0E00[Y>>1 & $FE]` = the head object of the bucket for sprite Y (`$0A80` low byte). 0x80 = empty.
- `$0E81[obj]` = next object in the bucket chain (0x80 = end); `$0E80[obj]` = prev (0x80 = head);
  `$0E01[obj]` = in-list flag (0 = in list, 0x80 = not). Active gate: `$1B80[obj] != 0`.
- **INSERT (C0/A9CD)**: bucket = `$0A80>>1 & $FE`; insert obj at the bucket HEAD (old head's prev
  -> obj, obj's next -> old head, head -> obj, obj.flag = 0). Empty bucket: head -> obj, prev/next
  = 0x80. Skips if already in list (`$0E01 >= 0`).
- **REMOVE (C0/A98A)**: unlink obj from its bucket (head case: head -> obj.next; middle case:
  prev.next -> obj.next), mark removed.
- Caller (C0/AA07): per object, gated by `$1B80`/`$1A01`; removes from the old bucket + re-inserts
  into the new as the sprite's Y changes -> the list is **incrementally maintained**, not rebuilt.

**The list is STATEFUL** (experiment `ct.exe --checksort`): rebuilding it from scratch each frame
(insert all active sprites at the bucket head in index order) gives 1440 head + 3780 next
mismatches over 180 frames (every frame differs) — the intra-bucket chain order is history-
dependent (which sprite was re-bucketed most recently lands at the head). So a byte-exact port
must **maintain the list state across frames** and apply the same remove/insert ops in lockstep,
seeded from the oracle's initial `$0E00`/`$0E81`/`$0E80`/`$0E01` — not recomputed per frame.
(Caveat: `--checksort`'s active set used `$1B80 != 0` over idx 0..0xFF, which is the wrong gate —
the real sprites are the chain objects, not `$1B80` — so the "stateful" verdict should be
re-confirmed with the correct object set, but the intra-bucket head-insert is history-dependent
either way.)

## Upstream port: actor -> screen transform (DONE, byte-exact) — `CtActorScreenCoords`
The sprite-object screen coords the OAM build consumes are computed by C0/AB45..ABA1, now ported
to `src/oam.c` (`CtActorScreenCoords`) and validated: **`$0A00[idx]` = ((`$1800[idx]` >> 4) -
camX) & $1FF**, **`$0A80[idx]` = ((`$1880[idx]` >> 4) - camY) & $1FF**, where the camera origin is
**camX = (`$1D0A` << 3) + (`$1D87` & 7)** (tile scroll * 8 + fine scroll), **camY = (`$1D0E` << 3)
+ (`$1D89` & 7)**. `$1800`/`$1880` are the actor world position (sub-pixel; >> 4 = pixels).
`ct.exe --checkxform` validates it over the live sprites (the `$0E00`/`$0E81` chains): **720
coords over 180 frames, 0 mismatches** (X and Y). So the actor-position -> sprite-screen-coord
step is reproduced byte-for-byte; the OAM build now needs only `$1800`/`$1880` + the scroll regs
(not the oracle's `$0A00`/`$0A80`) for the position path.

## Next steps
1. Wire `CtBuildFieldOAM` (+ `CtActorScreenCoords`) into the C port's per-frame path (over a
   persistent OAM the caller owns, with the hide/clear of unused slots), once the field loop is ported.
2. Port the Y-bucket list **statefully**: RE the full caller C0/AA07 (the remove-vs-insert trigger
   + iteration order) with the correct object set, port INSERT/REMOVE maintaining the C-side
   `$0E00`/`$0E81`/`$0E80`/`$0E01`, and validate it tracks the oracle frame-by-frame.
## Upstream port: OBJ-assembly decoder (DONE, byte-exact) — `CtDecodeSpriteAssembly`
The sprite-frame OBJ assembly (the `$7F4802` template the engine consumes) is decoded by C0/CC22..
CDxx, now ported to `src/oam.c` and validated. The frame's per-tile **X/Y offsets** come from ROM
at the long pointer **`$D3` = bank `$1300[idx]` : (`$1380[idx]` + frame(`$0F01[idx]`) * 40)** — each
frame record is 40 bytes, the 4 tiles' offsets at +0x20 (X0,Y0,X1,Y1,...; X sign-extended to 16-bit,
the decoder's `$D3` is built by a hardware multiply frame*0x28 + base). The **tile#** is `$0D00[idx]
+ t*2` (sequential even tiles); the **attr** is `$0F81[idx] | $0D01[idx] | $0C00/$0C01[idx]` (`$0C00`
for tiles 0-1, `$0C01` for 2-3). `ct.exe --checkdecode` (over the live chains, walking): **1704
offsets / 120 frames — 0 X, 0 Y, 0 tile, 0 base-attr mismatches**. The only attr diff is the **0x10
position-based OBJ-priority bit** (prio 2->3 for the player on a foreground tile), applied later by
the C0/9B4D stage (already RE'd in the native field), not by the decoder. (Free-roam reached so this
is testable: `ctDriveScript` mashes A ~22x to player control — `ct.exe --movetest`.)

So the sprite OBJ-assembly chain is reproduced byte-for-byte: `CtDecodeSpriteAssembly` (ROM frame ->
template) -> the validated `CtBuildFieldOAM` (stage 1+2) -> `$7E0700`. The remaining oracle inputs
are now just: the **animation-frame timing** (`$0F01` — which frame to show; the anim-state update),
the per-sprite **setup tables** (`$1300/$1380` assembly ptr, `$0D00` tile base, `$0C00/$0C01/$0F81/
$0D01` attr — set at sprite creation), the **position** (`$1800/$1880` world XY from the movement
system, `$0F80` priority), and the stateful **Y-bucket sort**.

## Upstream port: actor movement integration (validated) — C0/AAFD..AB44
The actor world position is advanced from its velocity each frame. Entry C0/AAFD: `LDX $6D; LDA
$1B80,X; BEQ AB06` — the field sprites (`$1B80 == 0`) take the simple path AB06; `$1B80 != 0` takes
a different path (C0/AAC7). AB06: if the move counter `$1A01[idx] != 0`, integrate:
**`$1800 += sign_extend($1900)`** (X velocity), **`$1880 += sign_extend($1980)`** (Y velocity),
**`$1A01 -= 1`**. `ct.exe --checkmove` predicts frame N+1's position from frame N's pos+velocity
over the live actors: **139 same-step frames, 0 X / 0 Y mismatches**. The 16 step-boundary frames
(skipped) are where the input/AI handler issues a NEW move step — it resets `$1900`/`$1A01`, so the
velocity is no longer the captured one. That handler (input/AI -> a move step: direction -> velocity
+ counter, with collision) is the layer above and the actual player/NPC movement logic.

**Velocity handler (layer 2, C0/AD02..ADF2) — trig formula validated.** The velocity is
**vector/angle based**: it reads the per-actor **speed `$1A00[idx]`** and a **direction `$EE`** (an
8-bit angle 0-255, set by the caller), and computes **`$1900` (X vel) = speed × `$F800[dir+0x40]`**
(cos) and **`$1980` (Y vel) = speed × `$F800[dir]`** (sin), each the unsigned-magnitude product
`>> 8` with the sign applied after (the HW multiply is unsigned; `CPX #$80` gives the per-quadrant
sign). **`$F800` is a 256-entry signed sin table in ROM at `$C0:F800`** (file 0xF800), not WRAM.
Then a collision step (tentative pos `$1800 + vel` vs the `$7E7000` map) can clip a component.
`ct.exe --checkvel` validates the formula by consistency (no per-actor direction needed): every
moving actor's `(velX,velY)` must equal the handler's output for SOME direction. **1077/1166 lie
exactly on the speed×trig circle; the 89 that don't ALL have a zeroed component (collision-clipped)**
— so the trig formula is confirmed and the misses are the wall check.

**Direction angle (layer 3, C0/ABA2..AC68) — ported + validated byte-exact.** RE'd via a
multi-agent workflow with adversarial verification (which caught two formula bugs the first pass
missed). The move heading `$EE` is **atan2 of a (dx,dy) delta**: for the scripted move command
0xA0 (`C0:5129`, "move toward target"), `dx = tgtX - curX`, `dy = tgtY - curY` (target = the event
script operand `$7F2001[$1180[idx]+1/+2]`; cur = the high bytes of `$1800/$1880`). Then **`$EE =
quadrant_base(sign dx,sign dy) + F300[idx]`**, where: the quadrant base is `0x00/0x40/0x80/0xC0`
from the signs, with an **axis swap of the table row/col** in the `dx<0,dy>=0` and `dx>=0,dy<0`
quadrants; `F300` is a 32×32 atan2 brad table at ROM `$C0:F300`; and the index is **folded by a
per-bit magnitude dispatch** (bit6 set -> `>>2`, bit5 -> `>>1`, else `>>0`) to keep it in range.
This is now `CtMoveDirAngle(rom, dx, dy)` in `src/oam.c`. A persistent facing field `$1600,X =
F700[$EE]` (`F700` = a 256-entry angle->cardinal quantizer at ROM `$C0:F700`) is the 4-way shadow,
but it is **stale under the dominant move opcode 0xA0** (it is only rewritten by the directional
opcodes 0x97/0x99/0x9E/0x9F and the explicit-facing 0xA6) — so the velocity, not the facing, is the
valid frame-end shadow of `$EE`. `ct.exe --checkdir` validates the full chain end-to-end: for each
0xA0 actor it computes `$EE` from the target+position and checks `$1900/$1980 == speed·trig($EE)`,
latching the heading at each step-issue (the velocity is constant between recomputes): **60/60
samples exact, 0 mismatches.** So target -> atan2 -> velocity is reproduced byte-for-byte.

**Layer 4 (collision + move-command source) — RE'd via a second workflow (adversarially verified;
two premises corrected).** There are TWO independent collision systems (the premise "C0/5B95 reads
$7E7000" was wrong):
- **C0/5B95 = actor-vs-actor** (a one-tile box test ahead, by facing), the move command's
  "movability" test — NO tile map. Gated by `$1C80,$6D & 0x02` (OFF for the field NPCs in the trace
  -> 5B95 returns "movable" as a stub, so actor-vs-actor collision is dormant here). When enabled it
  scans solid active non-self objects (`$0F00` occupied, `$1100` bit7 = dead, `$1B01 & 1` = solid),
  and the facing handler (jump table @C0/5BF2 by `$1600` facing 0-3) sets carry (blocked) if another
  object is within `< $0100` sub-pixels (one 16px tile) on both axes and on the facing side. When
  blocked, the 0xA0 command does a **single `$EE += 0x30` nudge (NO retry loop)** and **always
  moves** (counter set unconditionally). (A subtlety for a byte-exact port: the directional `SBC`s
  run with carry uninitialized.)
- **Tile map `$7E7000`** (in the velocity handler) is read EVERY frame, but for these NPCs it feeds
  the **sprite-priority bytes `$0C00`/`$0C01`** (the ADA8 clip/border branch is the gated-off part,
  not the `$7E7000` read). cell = `((tileY<<8)|tileX) & $1D1E` (tileX/Y = world high bytes; mask
  `$3F3F` for 64x64; three planes `$7E30C0`/`$7E7000`/`$7E7040` = the runtime form of maps.py
  scene_props b0/b1/b2). The priority is `0x20` (prio2) normally, `0x30` (prio3, foreground) only
  when the sprite is **half-on a `bit 0x40` tile** — the C0/9B4D position-based-priority check
  already RE'd in `field.c footPrio`. `ct.exe --checkprio` confirms the structure: the non-fg cells
  match `0x20`; the bare cell-bit over-predicts `0x30` because the half-on gate is required (so the
  simple formula is necessary-not-sufficient). **This closes the loop with `--checkdecode`: the OBJ
  attr's deferred `0x10` bit IS this tilemap-driven `$0C00`/`$0C01` priority.**
- **Move-command source** = the per-object event-script VM (runner C0/5A93: IP `$1180`, opcode
  `$7F2001`, dispatch table @C0/5D6E; carry SET = advance IP). The 0xA0 target = static script
  immediates `$7F2001[IP+1/+2]`; speed `$1A00` set by opcode 0x89. **This is the same VM faithfully
  partly ported in `src/ct_vm.c`** (moveOp) — the C version uses host trig + has no collision yet.

So the field movement is fully mapped: layers 1-3 (integration, velocity-trig, direction atan2)
validated byte-exact; layer 4 RE'd (actor-vs-actor collision dormant here; tile map -> sprite
priority, the half-on gate = the existing field.c logic; move-source = the event VM in ct_vm.c).

## Integration: the field render path wired into the C-port frame (`--verifyframe`)
The validated render functions are now composed and wired into the C port's per-frame path (the
zelda3 lockstep model — `src/ct_cpu_infra.c`: `g_ram` = the port's WRAM, copy-stub for un-ported
routines, verify `g_ram == oracle`). ⚠ **SUPERSEDED 2026-08-08 — `CtBuildFieldOAMFull` IS GONE.** It composed `CtActorScreenCoords`
(world->screen) + `CtDecodeSpriteAssembly` (anim frame + ROM -> OBJ assembly) + a placement of its own,
building the OAM from the upstream WRAM + ROM and reading none of the intermediate `$7F4Bxx`. That was a
COPY-STUB standing in for the record pipeline, and it carried the same two defects `CtBuildFieldOAM` did
(one OAM entry per object regardless of piece class, and a plain Y add where `C0/B788` clamps). Both
halves of the real pipeline are graded now — the emit by `--sweepdrain`, `C0/B309`'s stage-2 copy by
`--sweepstage2` — so `CtPortRunFrame` (`src/main.c`) runs the real chain instead: copy-stub the upstream
into `g_ram`, then `CtAnimDrawPass` (the emit, which recomputes each piece's placed X and clamped Y into
the record) and then `CtBuildFieldOAM` (the copy). The emit-before-copy order is load-bearing: without it
the stubbed records would make `--verifyframe` a memcpy grade, which is why its report line now asserts
how many objects the emit ran on. `ct.exe --verifyframe` runs the oracle + the C-port frame in
lockstep over 180 frames and compares the OAM region of `g_ram` to the oracle: **0 mismatches —
byte-exact**. The position, OBJ offsets, tile, and base attr are all computed from the upstream WRAM
+ ROM; the only remaining oracle-fed input is the OBJ-priority bits (attr `0x30`), taken from the
assembly template `g_ram` holds, because the position-based-priority engine that caches them at
anim-decode time is not yet ported (the bare half-on logic is in `field.c footPrio`, but the
decode-time caching/timing is the un-ported part). So the **field-sprite render path is end-to-end
byte-exact in the C port** (writing `g_ram`), with one narrow copy-stub (the priority bits).

## The position-priority engine, fully RE'd (the attr `0x30` source)
The per-sprite OBJ-priority bytes `$0C00`/`$0C01` (which feed the OAM attr `0x30`) are set by the
velocity handler (C0/AD61..AE25), gated on `$0F80[idx]` bit 7:
- **bit 7 clear** (C0/ADD7) — fixed from `$0F80`: `$0C00 = ($0F80 << 4) & 0x30`, `$0C01 = $0F80 & 0x30`
  (so `$0F80` packs both priorities: bits 0-1 -> `$0C00`, bits 4-5 -> `$0C01`, bits 2-3 = the OAM
  group, bit 7 = this gate). **Validated byte-exact** — `--checkprio` matches every bit-7-clear sprite.
- **bit 7 set** (C0/AE0D) — tilemap-cell priority: `$0C00 = (($7E7000[cell] & 0x40) >> 2) | 0x20`,
  `$0C01` likewise from `$7E7040[cell]`; `cell = ((($1880+velY)>>8 << 8) | ($1800+velX)>>8) & $1D1E`
  (the tentative tile, C0/AD85..AD9E).
- A separate **half-on override** (C0/9B4D, writers at C0:9D62/9E06/9E11/9E1A) refines this for
  sprites straddling foreground tiles — its geometry is already in `field.c footPrio`. It did NOT run
  in the field-walk trace (it fires only when a sprite half is on a prio-3 tile).

**Why it can't be a stateless port (the `--checkprio` 94/606 residual):** `$0C00`/`$0C01` are
STATEFUL + event-driven — the velocity handler recomputes them only at a **step-issue** (when the
move handler issues a new step), from the tentative cell at THAT frame, and they persist between
steps; idle sprites keep an init/last-move value. The OAM attr then uses the copy that the **decoder
cached at the last anim-frame** (a second event timing). So a per-frame recompute matches only the
sprites updated that frame (the bit-7-clear fixed path, and bit-7-set sprites at their step frame) —
the residual is sprites carrying a stale value. Retiring the OAM priority copy-stub byte-exact is
therefore **coupled to the move-handler** (drives the step-issue `$0C00` update) **and the anim
engine** (drives the decode-time caching) — it is not a standalone function. The render path is
already byte-exact (`--verifyframe`) with that one narrow stub.

## Next steps
1. Port the move-step handler (input/AI -> `$1900`/`$1A01`/`$1A00`, with the 5B95 actor collision)
   and the anim-frame update (`$0F01`) + per-sprite setup — these drive the step-issue `$0C00` update
   and the anim-decode template caching, so porting them lets the priority engine above run statefully
   in C and retires the last OAM copy-stub (the attr `0x30`) as a side effect.
2. Port the stateful Y-bucket sort (`$0E00`/`$0E81`). Each step shrinks the copy-stub toward
   `g_run_without_emu` (oracle fully retired for the field sprite path).
