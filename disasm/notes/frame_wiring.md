# The field FRAME — where every leg lives, and what the port calls

Derived 2026-07-31, first-hand from `tools/dis.py` plus a byte scan for every call site. This is the
map that says where `src/move.c` plugs in, and it supersedes the guesswork in earlier notes.

## The frame, verbatim

```
C0/00C1  JSR C0/59D9    the VM queue build
C0/00C4  JSR C0/5A46    the event VM  -> C0/5A93
C0/00C7  JSL FD/C2C1    [unported]
C0/00CD  JSL FD/FFF7    [unported]
C0/00D3  JSR C0/A810    MOVEMENT
C0/00D6  JSR C0/CA76    the DRAIN            <- ONCE per frame, not per object
C0/00DB  BRL C0/B271    the DRAW pass
```

`00DB`'s operand is `82 93 B1`; `dis.py` prints the displacement (`-4D8F`) where a target is
expected, so read it as `$00DE + $B193 = C0/B271`.

**Call-site census** (byte scan for `20 xx xx` across bank `$C0`), which is what makes "once per
frame" a fact rather than a reading:

| routine | JSR'd from |
|---|---|
| `C0/C98A` anim timer / enqueue | `A832`, `A878` — both inside `A810` |
| `C0/A98A` list unlink | `A835`, `A87B` |
| `C0/AB45` screen position | `A838`, `A87E`, `B101` |
| `C0/A9CD` list link | `A83B`, `A881`, **`A90B`** |
| `C0/CA76` the drain | **`00D6` and nowhere else** |
| `C0/B309` per-object draw | `B2AF`, `B2BB` — both inside `B271` |
| `C0/B701` the release | `B309` |
| `C0/6F9A` OBJ slot alloc | `A908`, `B18B` |
| `C0/B1B2` the VRAM upload | `B111`, `EB8C` (the NMI) |

## `C0/A810` — the movement pass

```
A810  LDA $7F2000 / ASL A / STA $BD          $BD = object count * 2
A817  LDX #00 / STX $6D
A81B  LDA $1100,X
A81E  CMP #03 / BCC ->A870                   state < 3  -> the AAFD arm
A822  CMP #08 / BEQ ->A83E                   state 8    -> skipped entirely
A826  CMP #07 / BEQ ->A86B                   state 7    -> JSR C0/A947
A82A  JSR C0/AA07                            states 3..6: the tile-clamp integrate
A82D  JSR C0/A88D / BCC ->A83E               THE ACTIVATION GATE
A832  JSR C0/C98A                              anim timer / FIFO enqueue
A835  JSR C0/A98A                              UNLINK from the OLD screen-Y bucket
A838  JSR C0/AB45                              recompute $0A00/$0A80
A83B  JSR C0/A9CD                              RELINK into the NEW bucket
A83E  INX INX / CPX $BD / BMI ->A819

A870  JSR C0/AAFD                            state < 3: the simple integrate
A873  JSR C0/A88D / BCC ->A884               the SAME gate
A878-A881                                    the same four calls
```

Two facts a port gets wrong by default:

- **The four calls are gated on `C0/A88D`'s carry**, not on any per-object flag a renderer would
  reach for. An object outside the camera window is neither animated nor positioned.
- **The unlink/relink bracket `AB45` for a reason.** The bucket is keyed on the screen Y, so the
  object must leave the OLD bucket before `AB45` overwrites `$0A80` and re-enter on the NEW one.

The two sites are the *state ≥ 3* and *state < 3* arms — **not** PC vs NPC.

## `C0/A88D` — the activation gate, and its rising edge

The carry is the camera-window test (`$1D0A-4 .. $1D0C+4`, `$1D0E-2 .. $1D10+5` against the object's
tile). But `A8E5 LDA $0F00,X / BMI ->A90E` makes the *previous* latch value the edge detector, and
the rising edge runs a one-shot setup that is the whole precondition set for the animation tree:

```
A8EC  $0E80,X = $80        prev  -- out of the draw list
A8EF  $0E81,X = $80        next
A8F2  $0F00,X = $80        the activation latch itself
A8F7  $1681,X = 0          anim phase
A8FA  $1601,X = 0          anim timer
A8FD  $1B00,X = 0          THE STAGING COUNTER C0/B701 compares against the arena arity
A902  $0F01,X = $FF        THE "no frame yet" SENTINEL C0/CAD9 gates on
A905  JSR C0/7170          the OBJ palette allocator
A908  JSR C0/6F9A          the OBJ slot allocator  ($1700, $0D00)
A90B  JSR C0/A9CD          LINK into the draw list
```

Four of those six stores plus both calls are things `C0/CAD9` and `C0/B701` read. Wiring the tree
without them means every object arrives with a staging counter and a frame sentinel left over from
whatever was in WRAM.

## `C0/B271` — the draw pass

```
B2A5  LDY #7E
B2A7  LDA $0E00,Y / BMI ->B2C0      empty bucket -> next
B2AD  STA $6D
B2AF  JSR C0/B309                   -> C0/B701
B2B2  LDX $6D / LDA $0E81,X         walk the chain
B2B7  BMI ->B2C0
B2B9  STA $6D / JSR C0/B309 / BRA ->B2B2
B2C0  DEY ...                       buckets HIGH to LOW
```

`src/oam.c`'s `CtBuildFieldOAM` already mirrors this loop exactly — same `$0E00` heads, same `$0E81`
chains, same `$7E` down to 0 — which is why the Y-sorted list was the missing link and not the draw.

## The scene-entry init — `C0/56B0`

```
56B0  STZ $09A0        the drain's per-scene counter
56B3  JSR C0/6F79      $0B88..$0BA5 = $80        30 OBJ slots, all free
56B6  JSR C0/7155      $0B80..$0B87 = $80        the palette slots
56B9  JSR C0/E935      $0BC0..$0BC7 = $80        the arena owners -- EIGHT cleared, FOUR handed out
56BC  JSR C0/5929      $0920..$092F = $FF        the gfx-set dedup table
56BF  JSR C0/B204      $0E00..$0EFF = $80        every head, flag, prev and next
56C2  JSR C0/A7E9      ($0F00,X, $0F01,X) = (00, FF) per object
56C5  JSR C0/5709      places the objects
56C8  JSR C0/595C
```

`C0/B204` is load-bearing: `C0/A9CD` refuses to link unless `$0E01,X` is already negative, and this
fill is the only thing that makes it so. Three of these write through the PPU's **WRAM port**
(`$2180` with `DP = $2100`) and `B204` finishes with three chained `MVN`s (0x20, 0x40, 0x80 bytes,
`Y` carrying on) that double the fill out to `$0EFF`.

## What the port has, after this session

| ROM | port | graded |
|---|---|---|
| `C0/C98A` | `CtAnimTimerTick` | `--sweepc98a` 116640 |
| `C0/A98A` / `C0/A9CD` | `CtObjListUnlink` / `CtObjListLink` | `--sweeplist` 288 + 288 |
| `C0/AB45` | `CtObjScreenPos` | `--sweepscreen` 720 |
| `C0/CA76` / `C0/CAD9` | `CtAnimDrainPass` / `CtAnimDrainVisit` | `--sweepdrain` / `--sweepcad9` 165888 |
| `C0/B701` | `CtAnimRelease` | `--sweepdrain` |
| `C0/6F9A` / `C0/7056` | `CtObjSlotAlloc` / `CtObjSlotFree` | `--sweepslot` 440 + 32 |
| `C0/4557`+`4590` | `CtObjLoad` | `--sweeploader` 720 |
| `6F79`+`E935`+`B204`+`A7E9` | `CtObjSceneInit` | `--sweepinit` 8 |
| `C0/B271` draw loop | `CtBuildFieldOAM` (`src/oam.c`) | `--checkoam` |

Wired into the live VM (`src/ct_vm.c`): **the scene-entry init only**, at `CtSceneCreate`.

## The wiring is ALL-OR-NOTHING, and that is measured

`C0/A88D`'s rising edge was wired too — the six stores plus `CtObjSlotAlloc` and `CtObjListLink` —
and the write mask rejected it. `--vmdiff party3_field --frames 130` went from *no masked
divergence* to five, all on object 32, which activates at f73:

```
f74  $7E1641  ours=00  oracle=0A     $1601+$40 = obj32's anim TIMER
f74  $7E16C1  ours=00  oracle=01     $1681+$40 = obj32's anim PHASE
f75  $7E0F41  ours=FF  oracle=07     $0F01+$40 = obj32's FRAME sentinel
f75  $7E1B40  ours=00  oracle=80     $1B00+$40 = obj32's STAGING counter
f74  $7E0E0A  ours=40  oracle=80     a bucket HEAD
```

Every one of those is the port writing the **correct initial value** and then never advancing it.
The oracle activates the object and, within the same frame, `C0/C98A` has already stepped the timer
and phase, the drain has committed a frame (`$0F01 = $07`) and `C0/B701` has stamped
`$1B00 = $80`. The bucket head diverges for the same reason one step earlier: without `C0/AB45` in
the loop, `$0A80` is stale, so `A9CD` links into the wrong bucket.

**So the rising edge cannot land before the frame legs that move those values.** Claiming a byte in
the write mask is a promise to keep it correct every frame, and initialising it is only the first
half of that promise. The scene-entry init is exempt because it writes straight into the image
rather than through `w8`, so it claims nothing — and the full suite stays green with it in.

The blocker for the frame legs is named below: `C0/CA76`'s gate reads `$09A0`, and the only thing
that clears it per frame is `C0/B1B2` — which also DMAs `$7F:3800` into VRAM that neither host
keeps. Porting `B1B2` without a VRAM model would be modelling half a routine.

> **RESOLVED for the harness (2026-08-01).** The two halves *are* separable: `B1B2` takes a
> `vram = NULL` mode that walks the queue and clears the gate without copying. The gate clear is the
> half the field frame needs; the copy is the half a renderer needs, and the ROM keeps them in one
> routine only to share the channel-7 setup. `--portrun` now runs the whole chain. See below.

## `$0F01` IS COMMITTED ONLY ON A FIFO POP — measured, not inferred

This is what `--portrun`'s idx-02 residual was, and it is worth stating first-hand because two
plausible mechanisms were derived and refuted before a probe answered it directly.

A writer probe on `$0F03` (`PORTRUN_HOOKW`, inside `--portrun`'s own per-opcode stepping loop, so the
probe's frame numbering *is* the divergence line's frame numbering):

```
TICK   f266 C98A  ora[cfg01 fc01 1480=0128 1500=3000 ph02 mod01]  t03 0F12  q[hd80 tl80 nx80]
DRAIN  f266 CA76  ora[...]                                        t02 0F12  q[hd80 tl80 nx80]
   ... f267 the same, queue still EMPTY, $0F03 still 12, C0/CB3A never visited at all ...
TICK   f268 C98A  ora[... ph02 ...]                               t01 0F12  q[hd80 tl80 nx80]
DRAIN  f268 CA76  ora[... ph03 ...]                               t07 0F12  q[hd02 tl02 nx80]
VISIT  f268 CAD9  ora[... ph03 ...]                               t07 0F12  q[hd02 tl02 nx80]
LOOKUP f268 CB3A  ora[... ph03 ...]                               t07 0F12  q[hd02 tl02 nx80]
W      f268 $0F03 12->07 @ C0/CC14
```

Over `f264`–`f267` the ROM **never visits `C0/CB3A`** and never writes `$0F03`. On `f268` the timer
reaches 0, `C98A` enqueues, the drain pops, and the whole chain fires at once. Identical shape at
`f276`–`f282`. So the animation frame advances at the rate the **assembler drains**, not the rate the
timer fires — the same law `C0/C98A`'s phase-INC-on-accept encodes, now observed end to end.

**Why the wrong model looked so much like an animation bug.** A harness that calls the lookup
speculatively every frame commits a frame from whatever facing row is current *between* pops. The
walk rows are 6 frames long, so every such divergence is off by exactly **6 or 12** — while phase,
timer and facing all agree at the frame boundary. That agreement is also the refutation of the
`C0/9E84` hypothesis (commit `3648128`): the lookup's inputs were never wrong.

### The `$09A0` phase trap

Wiring the drain fixed the row family and introduced three *new* off-by-one lines, where the port now
**under**-committed. Same probe, pointed at `$09A0`:

```
W f323 $09A0 00->01 @ C0/CCAC     the drain's own append raises it
W f324 $09A0 01->00 @ C0/B201     the NEXT frame's NMI lowers it, BEFORE 00D6 runs
W f324 $09A0 00->01 @ C0/CCAC     and that frame's drain raises it again
```

`C0/CA76`'s prologue is `LDA $09A0 / BEQ / RTS` — the **whole pass** is refused while the upload queue
is non-empty. A host that seeds its frame from an *end-of-frame* WRAM snapshot lands on the one
instant in the frame when `$09A0` is set, and so refuses the drain on every frame that follows a frame
which uploaded. The fix is not a gate change: it is calling `B1B2` at its canonical position
(`C0/EB8C`, the NMI, i.e. the top of the frame). This is the concrete reason the frame legs cannot be
wired in an arbitrary order — **`$09A0` is a phase, not a flag.**

## THE FRAME STARTS AT `C0/00A7`, NOT `C0/00C1` — and that is `--portrun`'s whole residual

`C0/00BF` is not the frame. It is the *second half* of it. Raw `C0/0097`:

```
0097  LDA $00F0 / TSB $51 / LDA $00F6 / TSB $50
00A1  JSR C0/18D9
00A4  JSR C0/0C76
00A7  JSR C0/881E        <-- THE PLAYER CONTROLLER
00AA  JSR C0/1AAC   /  00AD JSL C0/1F87  /  00B1 JSR C0/21E1  /  00B4 JSR C0/274D
00B7  JSR C0/00BF        <-- 00C1 sched / 00C4 VM / 00D3 A810 / 00D6 CA76 / 00DB draw
00BA  JSR C0/EC60 / 00BD BRA ->0097
```

`--portrun` models `00BF` and nothing above it. **Every one of its 15 remaining divergences is a byte
produced in the three legs it skips**, and three separately-investigated "families" collapse to this
one fact:

| residual | the byte | produced at | inside |
|---|---|---|---|
| 9 position/velocity lines | `$1D32`/`$1D33` | `C0/9175` (`STZ $32`/`STA $32`, `STZ $33`/`STA $33`) | `C0/884C`, in `881E` |
| the `face` term on 8 of them | `$1600` | `C0/A09B` via `JSR ($9FF7,X)` at `C0/9ECD` | `C0/9E84`, from `9E29`'s head |
| 4 `prio`/`prio2` lines | `$0C00`/`$0C01` | `C0/9D62`/`C0/9D67` (`LDX $0197`) | `C0/9C5C` ← `C0/8AFE` ← `C0/8A6D` ← `881E` |
| idx-12 f71 `ip` | `$1180` | VM op `0x02` (priority call), unported | `C0/00C4` |

`C0/881E`'s own order, and it matters — the wall clamp runs **between** the d-pad and the publish:

```
882C JSR C0/88E5    seed $1D2E/$1D30 from $1D2A/$1D2B
883D JSR C0/88EE    d-pad dispatch, LDA $00F9 / AND #0F / ASL A / TAX / JSR ($8902,X)
8840 LDA $0120 / BEQ ->884C
8847   JSR C0/8A6D  the WALL clamp   (its 8B15 `STZ $2E` is what kills a blocked step)
884C JSR C0/9175    PUBLISH $1D32/$1D33
```

### Two things this explains that a one-frame-lag story does not

- **The phase is TWO frames, not one.** `snes_runFrame` ends at the start of vblank and
  `snes_doAutoJoypad` fires there, so the controller word assigned before harness frame *f* is the one
  the NMI of frame *f+1* reads. Oracle velocity at frame *f* is `f(D(f-1))`; the port's is `f(D(f-2))`.
  Divergence whenever `D(f-1) != D(f-2)` — i.e. `f = 12k+1`, which is exactly 265/277/289/301/313/
  325/337/349, and the sign of every printed `velX`/`velY` matches the *previous* direction.
- **f260 is the same defect with a different trigger**, and it is the one that proves the fault is
  "stale `$1D32`" rather than "lagged input". The direction does not change there. The probe:
  ```
  W f259 $1D2E 00->10 @ C0/8941   then 10->00 @ C0/99ED   (camera kill, AFTER 9175 -- harmless)
  W f260 $1D2E 00->10 @ C0/8941   then 10->00 @ C0/8B15   (8A6D's WALL clamp, BEFORE 9175)
  ```
  Crono's rightward walk meets a solid tile at `x = $1904`; `9175` publishes `$1D32 = 0` and the port,
  holding the previous frame's `$10`, steps 16 sub-pixels into the wall.

> Derived 2026-08-01 by three independent agents, each checked by three adversarial verifiers
> (execution / harness-vs-engine / coverage lenses). **0 of 9 refuted.** The two-frame phase and the
> f260 wall-clamp account are corrections to the lead's original one-frame reading.

**What NOT to do about it.** `--portrun` owns `$0C00`/`$0C01` in `OFF[]` with no producer for a PC —
the same broken promise `C0/A88D`'s rising edge made. The tempting repairs are both wrong: dropping
the claim hides a real coverage gap, and re-feeding `$1D32` from the oracle after its frame would make
the check grade a `memcpy` (the mistake `--playerframe` already made once).

### The `00A7` leg, wired — and the blocker that never existed

> A first pass reported that `CtPlayerControl` and `CtPlayerCollide` were **not composable in the
> ROM's order**, so `CtPlayerControl` would have to be *split* at `8847`. **That was false.**
> `player.c` already calls `CtPlayerCollide` under exactly the ROM's `8840 LDA $0120 / BEQ ->884C`
> gate, between `88EE` and `9175`. The claim came from `player.h`'s header comment, which still said
> `8A6D` was "not yet modelled — no-ops in open field" long after the code modelled it.
> **Docs that describe missing work outlive the work, and a stale one reads exactly like a derived
> fact.** The fix was a one-line call, not a refactor.

`--portrun` now calls `CtPlayerControl(cram)` at the `00A7` position: **6451/6466 → 6454/6466**, 15
divergences → 12. It closes exactly the three the derivation predicted — `f300`/`f348` (the pure
`$0C00`/`$0C01` lines, whose producer `C0/9D62`/`9D67` sits inside `9C5C` ← `8AFE` ← `8A6D`) and
`f260` (the wall clamp at `C0/8B15`, which kills a step into a solid tile *before* `9175` publishes).

⚠ Still unported one level down: `C0/8A6D`'s four **diagonal** handlers (`8E21`/`8EF1`/`8FC1`/`909C`),
harmless here only because this scenario drives pure cardinals. Also unmodelled: `C0/99DE`, `C0/91AC`
and `C0/93E1`, the three legs after `9175` inside `881E`.

### ⛔ The snapshot point cannot be moved past the NMI — tried, measured, reverted

The eight remaining `f = 12k+1` lines are a phase problem, and the obvious fix is wrong. The joypad
reaches `$00F9` through the NMI, so a memcpy taken where `snes_runFrame` stops — the **start** of
vblank — hands the port a d-pad nibble one oracle-frame stale. Draining vblank *before* snapshotting
looks like the cure. It is not: **the ROM's main loop is free-running**, and the NMI interrupts it
wherever it happens to be, so the instant vblank ends the CPU sits at an *arbitrary* point inside
`C0/0097`'s body — frequently mid-way through `00D3`'s object loop. Snapshotting there splices half a
field frame into the port's start state.

Measured: **6454/6466 → 6382/6467**, 12 divergences → 48+, with idx 10 breaking at `f1` on the move
ticket (`$1A01 C=00 O=04`). A phase-**shifted but consistent** snapshot beats one that is correctly
phased only sometimes.

## `C2/84F0` + `C2/8545` — the joypad chain (PORTED, `CtJoypadPoll`)

```
284FE  LDX $F2 / STX $FE                 the PREVIOUS held word
28502  LDA $4218 / AND #F0 / STA $F2     A X L R          -> $F2 bits 7..4
28509  LDA $4219 / AND #0F / STA $F3     Up Dn Lf Rt      -> $F3 bits 3..0
28510  LDA $4219 / LSR x4 / TSB $F2      B Y Sel St       -> $F2 bits 3..0
2851B  LDA $4218 / CMP #3030 / JML C0FF00    L+R+Sel+St = the soft-RESET combo (not modelled)
28527  LDA $F2 / EOR $FE / AND $F2       16-bit: the NEWLY-PRESSED edge
2852D  STA $F0 / 2852F STA $F4
28531..28542                             autorepeat: TSB $F4 when the $FC/$FD counter expires
```

then `C2/8545` runs `C2/8555` **three times** — two `JSR`s at `2854F`/`28552` and then a
**fall-through** into the same body at `28555` — with X = 0, 2, 4. Each pass builds `$F6,X` from eight
`AND mask / CMP mask / ROR` steps over the remappable config at `$7E2993-$299A`, in the order
`2998, 2996, 2997, 2994, 2999, 299A, 2995, 2993` (the **first** term rotates furthest and lands in
bit 0). `CMP` after `AND` sets carry iff **every** bit of the mask is held — that is what makes one
config entry able to mean a button *combination*. Finally `285A7 LDA $F1,X / STA $F7,X`:

> **`$00F9` is a straight copy of `$00F3`** — the held d-pad nibble, not a computed value. That is
> the whole reason `CtPlayerDpadIntent`'s nibble order is Up=8 Down=4 Left=2 Right=1.

**Why this dissolves the snapshot-phase problem rather than working around it.** The input the ROM
decodes this frame is *not in WRAM at all* when the harness snapshots: it is in the auto-joypad
**hardware register**, latched at the start of vblank (`snes/snes.c:180-188`) — exactly where
`snes_runFrame` stopped. Reading `portAutoRead[0]` at the top of the port's frame gets precisely the
word the oracle's own NMI is about to read: no phase to correct, no guess about which `dirs[]` entry
is current. That is the harness supplying **input**, the boundary of the port's scope — unlike
re-feeding `$1D32`, which would be reading back an engine **result**.

Result: **6454/6466, unchanged — but every position and velocity divergence is gone.** All eight
idx-02 lines now differ in `face($1600)` *alone*. Graded: mutating the `$F3` nibble mask to `& $0E`
drops the mode to 6428/6466.

### What is left, and it is now one named routine

The PC's facing has exactly one writer: **`C0/A09B`**, via `JSR ($9FF7,X)` at `C0/9ECD` inside
**`C0/9E84`**, called from `C0/9E29`'s head. The port never runs it, so the facing is simply the
*previous* direction's — observed mapping, consistent across all eight lines: **Up→0, Down→1, Left→2,
Right→3.** Derivation is already written up in `player_anim_9e84.md`. ⚠ The one attempt to port it
(3648128) was reverted for good reason — it was aimed at `$0F01` when `$0F01`'s inputs were already
correct — and its `--sweepanim` scored **4380/4860**, so it is *not* byte-exact yet. Fix that against
micro-execution first; only then wire it, now that the `face` term gives it a direct grading signal.

### The numbers

| harness order | `--portrun` | divergences |
|---|---|---|
| speculative per-object commit (old) | 6434/6466 | 32 |
| `00D6` drain, no `B1B2` | 6448/6466 | 18 |
| `B1B2` at the NMI **+** `00D6` drain | **6451/6466** | **15** |

Both halves are acceptance-checked by their own removal. The residual 15: nine are idx 02 with
position off by exactly `0x10` on one axis while `$0F01`/phase/timer agree (a movement-timing
question, not an animation one), three are the known idx-16 anim timer, and three print every
compared field as equal — they diverge in a field the line does not print.

## `C0/B1B2` — the VRAM upload (PORTED, `--sweepvram`)

The consumer of the queue `C0/CAD9` fills, and the only thing in the engine that clears `$09A0`.
`DP = $0900` throughout, so every `$xx` below is `$09xx`.

```
B1BA  $2115 = $80        VMAIN: increment after the $2119 (HIGH) write, step 1 word, no translation
B1BF  $4371 = $18        BBAD7  = $2118 (VMDATAL)
B1C4  $4370 = $01        DMAP7  = mode 1: two registers written alternately, A->B
B1C9  $4374 = $7F        A1B7   = the source bank, fixed -- always the staging bank
B1CE  LDX #0000
B1D1  LDA $A0,X / BEQ ->B201        the flag, read EIGHT-bit; zero ends the walk
B1D5  $0950,X -> $2116 | $0940,X -> $4372 | $0980,X -> $4375 | $420B = $80    pair A
B1E9  $0970,X -> $2116 | $0960,X -> $4372 | $0990,X -> $4375 | $420B = $80    pair B
B1FD  INX INX / BRA ->B1D1          NO upper bound -- it trusts the terminator
B201  STZ $A0                       8-bit, and it clears ENTRY 0 ONLY
```

Three things a port gets wrong by default:

- **Producer and consumer disagree on width.** `ctAnimAppend` (`CCAC`) writes the flag with a
  **16-bit** `INC` and terminates the next slot with a **16-bit** `STZ`; `B1D1` reads it **8-bit**,
  exactly as `C0/CA76`'s prologue does. An entry whose count reached `$0100` would stop the walk.
- **`B201` clears entry 0 only**, not the array. The next append restarts from cursor `$79`, so
  entry 0's terminator is all `C0/CA76` needs.
- **A DMA size of 0 means `$10000` bytes**, not none — hardware, and the sweep drives it.

### Grading

`--sweepvram` micro-executes `C0/B1B2` so the **oracle performs the real channel-7 DMAs into its own
PPU**, and compares all 64 KB of VRAM byte for byte against a buffer the port filled. That grades the
DMA semantics themselves, not just the queue walk: mode 1's alternating ports, VMAIN `$80` advancing
after the high byte, the byte index folding at 64K, the `$7F` source bank and the zero-length rule.
Seven queue shapes (multi-entry, odd lengths, a destination crossing the top of VRAM, a zero length,
an empty queue, and a flag whose low byte is zero) — **7/7 byte-exact**. Acceptance 18/18 red, no
holes.

Two of the eighteen were holes first, and **neither was seeding**: a `& 0x7FFF` on the word address
was *redundant*, because the byte index's `& 0xFFFF` already folds word `$8000` onto `$0000`. The
right response to an unobservable expression is to delete it, not to keep an ungraded claim.

> ⚠ **LakeSnes keeps VRAM as 32768 `uint16` WORDS.** Comparing `s->ppu->vram[k]` against a byte
> compares a whole word and reports every shape — including the ones that copy nothing — as a
> mismatch at offset 0. Index through a `const uint8_t*`.

`CtAnimUploadFlush(ram, vram)` takes `vram = NULL` to walk the queue and clear the gate **without**
copying. The gate clear is the half the field frame needs and the copy is the half a renderer needs;
the ROM only keeps them in one routine to share the channel setup.

## What blocks the FRAME LEGS — CORRECTED

> An earlier revision of this section claimed the blocker was that the live VM's **float** position
> integration is "not sub-pixel exact", and that the cure was migrating to the fixed-point
> integrator. **Both halves were wrong**, and adversarial verification plus the measurements below
> refuted them. The corrected account follows.

### The divergences are whole STEPS, not rounding

`--vmdiff candy_shop_lake.state --frames 5 --withpos` against the object's own velocity byte:

| field | ours | oracle | delta | velocity byte |
|---|---|---|---|---|
| obj1 posX | `1958` | `194A` | **+14** | `$1902` = `0E` = 14 |
| obj1 posY | `057D` | `0561` | **+28** | `$1982` = `1C` = 28 |
| obj11 posY | `1023` | `102A` | **−7** | `$1996` = `F9` = −7 |

Byte-identical. Every delta is exactly one application of that object's `$1900`/`$1980` velocity, so
the port applies a step on a frame the oracle does not (or vice versa — obj11 is the other sign).
28 sub-pixels is not a rounding error, and the doubles never leave the 1/16 grid.

### Wiring the legs does NOT require claiming `$1800`

`CtObjScreenPos` (`C0/AB45`) **reads** `$1800`/`$1880` and writes only `$0A00`/`$0A80` plus the DP
scratch. The only `$1800` *writes* in `move.c` are inside the integrator (`aa07Axis`), which is not
one of the legs. `$0A00`/`$0A80` are modelled nowhere in `ct_vm.c` or `field.c` today, so they enter
the write mask clean. The earlier "mirroring would claim `$1800` and diverge" argument does not
apply — the legs never write it.

### Defect 1 — the movement dispatch was missing two arms (FIXED)

`C0/A81E`-`A82A` is a **four**-arm dispatch and `ct_vm.c` had two:

```
A81E  CMP #03 / BCC ->A870   mode < 3  -> C0/AAFD
A822  CMP #08 / BEQ ->A83E   mode 8    -> SKIPPED ENTIRELY, no integrate
A826  CMP #07 / BEQ ->A86B   mode 7    -> JSR C0/A947 ONLY, no integrate
A82A  JSR C0/AA07            modes 3..6
```

The port's comment asserted modes 7/8 "never reach here with an active ticket". Measured false:
`party3_field` obj35 has `$1146 = 07` and `$1A47 = $18`, a live ticket, and the port walked it
(`3327 → 3312 → 3296 …`) while the oracle held it still.

`C0/A947` is `A88D`'s camera-window test **without the clamps** (`$C3 = $1D0A-4`, `$C4 = $1D0C+4`,
`$C5 = $1D0E` with no `-2`, `$C6 = $1D10+5`) whose only effect is `$0F00,X = $80` inside the box,
`STZ` outside. Since the `A88D` pass skips modes 7/8, `A947` is the **only** writer of `$0F00,X` for
a mode-7 object.

**Result:** `party3_field`'s first `--withpos` position divergence moved from **frame 1 (obj35)** to
**frame 12 (obj27)**, and the write mask went 41 → **42** owned addresses with **still zero masked
divergence** — one more byte owned and the promise kept.

### Defect 2 — the `--vmdiff` SEED PHASE (FIXED)

`snes_runFrame` (`snes/snes.c:115`) is not a fixed quantum:

```c
while (snes->inVblank) cpu_runOpcode(snes->cpu);            // run OUT of vblank
uint32_t frame = snes->frames;
while (!snes->inVblank && frame == snes->frames) cpu_runOpcode(snes->cpu);   // run UNTIL vblank
```

It runs **from wherever the CPU is to the start of the next vblank**. So from a savestate captured
at an arbitrary point, the FIRST call advances only the *remainder* of that frame — and `--vmdiff`
seeded the port from WRAM immediately after the load, then compared one full port tick against that
partial oracle frame. If the capture landed after the engine's movement pass, the pass does not run
again before vblank and the oracle appears not to move on frame 1; if it landed before, it does.

**The fix:** `--vmdiff` now settles **one** frame before `CtSceneSeedFromWram`, so the seed is taken
at a true frame boundary. `VMDIFF_SETTLE=0` restores the old behaviour; `=N` for anything else.

**Proof it is an alignment and not a shift** — the ABSOLUTE frame of the first divergence stops
depending on the settle count:

| state | settle 0 | settle 1 | settle 2 | absolute |
|---|---|---|---|---|
| `bedslide` (`--withpos`) | f1 | f4 | f3 | 1 *(artifact)* vs **5, 5** |
| `DEMO8_gtu` (`--withpos`) | f49 | f48 | f47 | **49, 49, 49** *(never had an artifact)* |

And the suite-wide movement, every direction checked before re-pinning:

| check | before | after |
|---|---|---|
| `vmdiff-im_so_sorry` | f141 | **CLEAN over 300 frames** |
| `vmdiff-bedslide` | f51 | **f85** |
| `vmdiff-DEMO8` / `vmdiff-S5` | f109 / f114 | f108 / f113 — *same absolute frame* |
| `vmdiff-withpos-bedslide` | f1, obj1+obj8 | f4, obj14+obj15 |

**No state got worse.** `bedslide`'s obj1/obj8 frame-1 pair is gone entirely: what survives at f4 is
obj14/obj15, different objects, off by 16-17 sub-pixels — a real defect the artifact had been
hiding.

> ⚠ `base5_ag1` and `base5_curva` are **not** evidence for anything here: they report **6 seed
> divergences at frame 0** (`ip ours=FFFFFFFF oracle=0000`) at every settle count. An earlier
> analysis cited one of them as showing the opposite-sign offset; it shows a broken seed instead.
> (memory: `verify-negatives-on-proper-state`.)

### Do NOT migrate the position representation

> **CORRECTION (2026-08-01):** this paragraph used to say `--porttick` runs the *ported* fixed-point
> `CtActorIntegrate`. It did not. `runPortTick` hand-inlined an unconditional `AAFD`-shaped add with
> no state gate and no `$7F0B00` clamp, and `grep -rn CtActorIntegrate src/` showed the ported
> `C0/AA07` had exactly ONE caller — `runPortRun`. So `128/136` was grading a harness stub. Both
> sites now use the ROM's four-arm dispatch and it reads **137/143** (8 divergences → 6). The
> conclusion below still stands, but on the corrected evidence.

`--porttick` runs `CtActorMoveStep` directly on `$1800`/`$1880`/`$1A01`, and it showed the same
defect shape:
`idx 10 f2 diverge C(1600,0D09 v5,5 c03) ora(15FB,0D09 v5,5 c00)` — same velocity, position off by
exactly one step, tickets `c03` vs `c00`. Converting `sc->fx/fy` to fixed point would not clear a
single measured divergence. The disease is step TIMING, not representation.

Also: `--portrun`'s `1592/1592` is **6 actors on one scenario** and re-feeds the oracle's WRAM every
frame; it is not a warrant that the mover is solved. The residual lives in `porttick` **128/136** and
`playerframe` applied-velocity **196/270**, both pinned open, and now in
`vmdiff-withpos-bedslide`.

## What is still NOT ported

- **class 2's `C0/D4F7`** (`CtAnimDrainVisit` returns `-1`) and **class 3's `C0/C73A`**
  (`CtAnimRelease` returns `-1`).
- **four of the six OAM emitters** (`BA65`/`BCDC`/`BFF2`/`C2BF`).
- `C0/A947` (the state-7 arm) and `C0/AAFD`'s callers beyond the integrate.

## The trap that is worth repeating

The first attempt at this wiring called `C0/CAD9` per object per frame and collapsed `--portrun`
from 1592 to 156. The census above is why: `CA76` is JSR'd from `00D6` and **nowhere else**. The
drain is one call per frame that pops a FIFO; calling the *visit* speculatively makes every object
claim an arena slot, and with only four slots the fifth claim fails and the frame is dropped.

---

# ONE FIELD BODY CAN STRADDLE THE HARNESS FRAME BOUNDARY — and that was 6 of `--portrun`'s last 8

`C0/00BF` is **straight-line**. There is no branch anywhere in it:

```
00BF SEP #10
00C1 JSR C0/59D9      scheduler
00C4 JSR C0/5A46      the event VM
00C7 JSL FD/C2C1      \ the two bank-$FD sync calls
00CB REP #10          |
00CD JSL FD/FFF7      /
00D1 SEP #10
00D3 JSR C0/A810      the movement pass  <- everything --portrun grades is written at or after here
00D6 JSR C0/CA76      the drain
00D9 REP #10 / 00DB BRL   the draw
```

So **"`00C1` was visited and `00D3` was not" cannot be the ROM taking a different path.** It can only
mean the body BLOCKED inside those two `JSL FD/...` calls and the harness's frame boundary fell in
the middle of it. Measured directly with `PORTRUN_HOOKW=1617` (object `$16`), which prints each stage
head as it is reached:

```
f157..f160   00C1 00C4 00D3 00D6      four ordinary frames
f161         00C1 00C4 . . . . . .    the FRONT half only
f162..f169   (no stage head at all)   nine frames parked in the bank-$C7 sound driver
f170         . . . . 00D3 00D6        the SAME body's BACK half
f171         00C1 00C4 00D3 00D6      ordinary again
```

`--portrun` gated its skip rule on `00C1`, the **first** stage, and that is wrong in **both**
directions at once:

- **f161** — `00C1` ran, so the rule said "the field ran" and the port executed a WHOLE frame. The
  ROM had moved nothing. The port's `$1601` tick, its integrate and its drain all ran against an
  engine that had only run the scheduler and the VM.
- **f170** — `00C1` did NOT run, so the rule said "the field did nothing" and the port's own tick was
  **undone** — on the one frame the ROM actually ticked.

Hence the symmetric residual: obj `$16` read `C=09 O=0A` at f161 (port one ahead) and `C=09 O=08` at
f171 (port one behind), with obj `$02` and obj `$12` showing the same shape.

**Every byte this mode compares — `$1800/$1880` position, `$1900/$1980` velocity, `$1A01` ticket,
`$1601` timer, `$1681` phase, `$0F01` frame — is written by the BACK half.** The front half only
advances the script IP, and that IP is still sitting in RAM when the back half lands. So gating on
`00D3` aligns the port's one-lump frame with the body that actually produced the state being graded,
and on every non-straddled frame the two rules are identical *because the body is straight-line*.

Result: `6891/6899` → `6896/6898`. Reverting this one line alone puts it back to `6893/6899`.

The report now prints `%d graded on a straddled body's back half` (1 over 360 frames) and WARNS if a
harness frame ever contains two movement passes — a silent cap here would read as coverage later.

## The same straddle is the LAST frame of `--playerframe`

`--playerframe` feeds `C0/881E`'s gates (`$011F`/`$0162`/`$0138`) from the END of the previous frame,
which is right: `881E` sits at `00A7`, three JSRs AHEAD of `00BF`, so within one body it reads the
`$011F` the PREVIOUS body's VM wrote (op `0xE3`, `C0/3C05`). On a straddled body both halves land in
one harness frame and that delay is off by one. It is exactly **one** frame in 270 — f73, port vel
`(00,00)` vs oracle `(F9,00)`.

⚠ **Tested, not assumed:** feeding the CURRENT frame's `$011F` instead makes it strictly WORSE
(position `270 → 264`, velocity `269 → 268`). The one-frame delay is correct in general; f73 is the
exception. The real fix is the in-situ `C0/881E` hook that `--checkctrl` already proves at 400/400,
not a re-timing of the copy-stub.

# THE VM OP BUDGET WAS READ AT THE WRONG DP — `$7E0068` instead of `$0168`

`C0/5A93`, the runner's prologue and its loop tail:

```
5A93 LDA $68        the RELOAD
5A95 STA $67        the live COUNTER
...
5AB1 JSR ($5D6E,X)  dispatch
5AB4 BCC ->5ABA     carry clear = yield, exit the visit
5AB6 DEC $67        8-bit (SEP #20 at 5AAF)
5AB8 BNE ->5AA3     budget left -> next opcode
5ABA ... STA $1180,X / RTS
```

Both `$67` and `$68` are DP-relative and **DP = $0100** in the field engine, so they are `$0167` and
`$0168`. `CtFieldRunner` read `r[0x0068]` — `$7E0068`, a byte no field code owns — and covered for it
with an invented `if (budget <= 0) budget = 5;`. **That fallback never fired**: `$7E0068` held ≥ 8.
(And it is wrong on its own terms: when the ROM's `$68` is 0 the first `DEC $67` wraps to `$FF` and
`BNE` loops, giving **256** opcodes, not 5.)

Measured at f253 with the two dispatch traces side by side (`PORTRUN_OPTRACE` hooks the ROM at
`5AB1`; `PORTRUN_PDISP` prints `CtFieldRunner`'s own sequence):

```
oracle f253:  AD 5A 87 E3 4A        $0167 = 05 04 03 02 01  -> DEC to 0, visit ENDS, IP parks at 0452
oracle f254:  0A 00 B0              $0167 = 05 04 03
port   f253:  AD 5A 87 E3 4A 0A 00 B0     -- eight in ONE visit, ending parked on B0 at 0408
```

Op `0x0A` is DESPAWN (`C0/6254`: `$1100,X = $80`, `$1A81,X = 0` on a TARGET object). Running it a
frame early is what disabled object `$16` at f253, after which `C0/A88D` rejected it on the state
bit-7 test and `C0/C98A` was never reached — so its anim timer froze one frame before the oracle's.
That is why a budget bug surfaced as a *timer* divergence on a *different object*.

`$0167` is real memory and the port now writes it (`STA $67` and each `DEC`), not just a local.

Result: the f253 pair closed, `--portrun` → **6898/6898**, and `--playerframe` applied-velocity
`268/270` → `269/270`. Reverting this one line alone puts both back.

⛔ Same class as the `$006D`/`$016D` latch: **a DP-relative address ported as if DP were 0.** Both
were invisible to every scenario check for the same reason — the wrong byte held a value that
happened to keep the engine running.

---

## ⭐ STEP 1 OF THE WIRING LANDED (2026-08-08): the harness now runs the same ENGINE

`--vmdiff` never called `CtSceneSetRomImage`, while `field.c` always has (`field.c:4903`). `ct_vm.c`
gates **every ROM-table read** on it, so the lockstep harness was running the field VM with those
tables blank while the live engine had them filled — the two hosts were not running the same engine,
which is a worse problem than any single divergence.

Fixed in `runVmDiff`, and the blast radius was **measured, not assumed**: of the eight `vmdiff` checks
it moves exactly **one**, and it moves it the right way.

```
vmdiff-wmask-party3:  mask 42 addr  ->  mask 90 addr
                      [wmask] no masked divergence over 130 frames   (unchanged)
                      oracle-changed 493 / unmodelled 482 / dropped 0 (unchanged)
```

The port now **claims 48 more bytes and every one of them still matches the oracle**. That is why
re-pinning was legitimate here: the `mask` counter exists to catch the mask SHRINKING toward a vacuous
zero, and it grew.

## THE REMAINING BLOCKER IS NOW EXACT: the RELEASE lives in the DRAW pass

The recipe for the frame legs is fully assembled — `A810`'s order above, every leg ported and graded
(the table in "What the port has"), the gate being `C0/A88D`'s carry, the drain **once** per frame, and
`--portrun`'s own call form as the reference:

```c
int st = ram[0x1100 + idx];
if (st != 7 && st != 8 && CtObjActiveGate(ram, idx))
  CtAnimTimerTick(ram, rom, romLen, idx);        // then A98A / AB45 / A9CD, in that order
...
CtAnimDrainPass(ram, rom, romLen, 0xF0);         // once; 0xF0 = "vblank" -> drain fully
```

⚠ **But `C0/B701`, the arena RELEASE, is called from `C0/B309`, which is inside the DRAW pass
`C0/B271` — and `ct_vm.c` has no draw pass.** The four-slot arena therefore never releases, and the
pool exhausts. That is not a prediction: `--portrun` hit exactly this and collapsed to **156/1592**
before `B701` was ported, and the reason `--portrun` is fine *now* is written in its own comment —
**it re-seeds `cram` from the oracle every frame**, so the pool starts each frame at the oracle's state.
A live `CtSceneTick` has no such reset.

So wiring the frame legs into the VM tick requires the **draw pass in the same loop** (`CtBuildFieldOAM`
in `src/oam.c`, graded by `--checkoam`), not just the movement legs and the drain. That is what
"ALL-OR-NOTHING" means concretely, and it is the sentence this dossier was missing: the all-or-nothing
set includes the DRAW pass, because that is where the release lives.

**Consequence for op 0xAB's completion** (`vmdiff_pinned_triage.md` §5): keying it on `$7F0B01,X`
needs `C0/C98A` advancing that byte, which needs the gate, which needs the drain, which needs the
release, which needs the draw pass. One chain, one pass, and the failure mode of doing half of it is
measured twice over — five masked divergences for the rising edge alone, 156/1592 for the drain
without the release.

---

# ⭐ THE DEDICATED PASS (2026-08-08): the chain is wired, and what "all-or-nothing" costs is MEASURED

`CtSceneTick` now runs the whole thing, behind `CtSceneEnableFrameLegs` / `CT_FRAMELEGS`:

```
C0/EB8C  CtAnimUploadFlush(ram, NULL)   at the TOP -- $09A0 is a PHASE, not a flag
C0/A810  per object: the A88D edges, then C98A / A98A / AB45 / A9CD
C0/A846  the party RE-LINK TAIL ($0199 then $0197) -- past the loop's own BMI, easy to miss
C0/CA76  CtAnimDrainPass(..., 0xF0)     ONCE
C0/B271  CtAnimDrawPass                 because C0/B701 lives inside it
```

## Five things the harness was never handing the VM

Step 1 found one (`CtSceneSetRomImage`). Chasing the position compare found four more of the same class
-- `field.c` always supplied them and `runVmDiff` never did, so the two hosts ran different engines and
the difference was being read as engine defects:

| missing | consequence, measured |
|---|---|
| `CT_ASSET_MOVER` tables | ct_vm.c fell back to FORMULA tables, "within 1 LSB of the ROM" -- and one LSB of a velocity is a position divergence a few frames later |
| `CtSceneSetTileSolid` | `sc->tileSolid == NULL` short-circuits ct_vm.c's whole ACFD test, so **every mover walked through walls the oracle stops at** |
| the two FOLLOWERS' position | the lead was injected; members 2 and 3 were left standing where the seed put them while the oracle walked them |
| `$7E01F8` | see below |

## `$F8` IS `$7E01F8` -- the fifth DP-relative byte ported as if D were 0

```
AE27  SEP #10
AE29  INC $F8          the cursor advances FIRST
AE2B  LDX $F8
AE2D  LDA $FE00,X      so the index is the POST-increment value
```

A never-reset GLOBAL, shared by C0/ACFD's wall bounce and VM op `0x7F` (C0/6D09). The port kept it in a
C field starting at 0, so it rolled angles 1, 2, 3... where the ROM rolled 0x37, 0x38... Same class as
the `$68` op budget (`$0168`), the `$71` gfx-heap cursor (`$0171`) and the `$6D` latch (`$016D`).

**What it cost, end to end.** obj27 on `party3_field` is parked on `96 37 1D`, a move into a tile that IS
mover-solid, so `C0/AEE1 BMI ->AF27` zeroes both velocity components (`STZ $DB` / `STZ $DD`) and
republishes them at AF18. Measured with `--monitor --writer`:

```
f0..f22   $1936=00 $19B6=00   ticket $1A37 drains 09..00, reloads 0C, position FROZEN at (14079,7516)
f23       $1936=FF (C0:AF1F)  $19B6=07 (C0:AF24)   $01F8 0x37 -> 0x38 (C0:AE2B) -- THIS roll is passable
f24..     it walks at (-1,+7)
```

The port stalled it forever (wrong roll), and before the collision hook was wired it walked it straight
through the wall. Three faults stacked on one object.

## The position commit: `$1800`/`$1880` for EVERY object

The ROM has ONE position storage; the port mirrored it for PC-mode actors only, so `$1800` sat frozen at
the seed for every NPC -- and `C0/AB45`, the leg the entire draw list is keyed on, reads exactly those
bytes. Committing all of them is the ROM's own invariant, and it is what turns `$1800` into a graded byte
instead of an unmaintained one. The interleaving is inert: A810 runs obj k's legs before obj k+1's
integrate, but the integrate touches only per-object tables.

**`vmdiff-wmask-party3`: mask 90 -> 235 owned addresses, unmodelled 482 -> 453, still `no masked
divergence over 130 frames`.** The position compare the README called "the real gap" now exists, and it
is clean.

## WHY THE LEGS ARE DEFAULT OFF -- 23 divergences: three harness families and one structural one

`vmdiff-framelegs-party3` runs the identical command with `CT_FRAMELEGS=1` and pins all of it; the two
checks are the before/after. Coverage still moves the right way: **mask 239, unmodelled 425** (28 more
oracle-driven bytes claimed).

1. **the three PCs' `$0A00`** (`$0A02`/`$0A06`/`$0A08`, f2/f26/f49) and
2. **their `$1601` anim timer** (`$1603`/`$1607`/`$1609`) -- both downstream of state the ROM's PLAYER
   layer produces (C0/881E for the position, C0/9E84 for the anim mode C98A's reload table is indexed
   by), and this mode deliberately does not run it. `$1601` could only be "closed" here by injecting the
   very byte under test. The live engine runs the player layer and does not have these.
3. **obj32's activation frame, f74/f75 -- `C0/CA76`'s RASTER CLOCK, and this is the real limit.**
   CA85-CA9B compares a V counter that ADVANCES WHILE THE DRAIN WORKS, so a clockless port either drains
   fully or not at all. Measured on the oracle: at f74 the ROM runs class 0's pass 1, appends the upload
   (`$09A0`=1, `$0179`=2) and STOPS; pass 2 lands at f75. The port does both and commits `$0F01`=07 /
   `$1B00`=80 **one frame early**. Every f74 line plus the f75 pair is that one frame of lead, and they
   re-converge at f75.

Family 3 was already measured before this wiring existed ("the queue is still non-empty at a frame
boundary on 1/90 frames for this scenario"), so the wiring did not introduce it -- it exposed it. It
cannot be closed by choosing a `scanline` argument: 0xF0 drains fully, anything `>= $016B` refuses the
pass entirely, and there is no third option without the per-visit raster cost, which is not derivable
without cycle-counting the drain.

## ⭐ THE ARENA LEAK, OBSERVED IN WRAM -- the mutation that proves the draw pass belongs in the set

Deleting the `CtAnimDrawPass` call and re-running:

```
f74   $7E0BC0  ours=40  oracle=80      slot 0 claimed, never returned
f84   $7E0BC1  ours=40  oracle=80      slot 1
f94   $7E0BC2  ours=40  oracle=80      slot 2
f104  $7E0BC3  ours=40  oracle=80      slot 3   -- all four, ten frames apart
```

With the draw pass in, none of those four lines appear: `$0BC0..$0BC3` are CLAIMED and MATCH the oracle
for 130 frames. That is the direct evidence the release works, and it upgrades the old argument (which
rested on `--portrun` collapsing to 156/1592) from an inference to a measurement.

## The mutation ledger, including the survivor

| mutation | caught by |
|---|---|
| `$01F8` cursor `+1` -> `+2` | `vmdiff-wmask-party3` |
| the position commit restricted to `mode < 3` (the old behaviour) | `vmdiff-wmask-party3` |
| the draw pass's main-cursor stride `0x10` -> `0x20` | `vmdiff-framelegs-party3` only (correctly NOT the legs-off check) |
| the draw pass call deleted | `vmdiff-framelegs-party3` (the arena rows above) |
| `C0/B309`'s gate inverted (draw even when B701 says SEC) | ✅ **`sweepdraw-b271-gate`** -- see the section at the end; it survived when this table was written |

✅ **CLOSED, and the diagnosis in this paragraph was half wrong.** It is not that this scenario happens
to miss the arm -- `$1B00` is 0x80 at steady state in EVERY scenario, because the clockless drain always
finishes staging, so no scenario can reach it. The answer was a micro-execution sweep, not a better
scenario: see "THE SURVIVOR IS CLOSED" at the end of this dossier.

## What is deliberately NOT in `CtAnimDrawPass`

`C0/B309`'s **stage 2 OAM byte emission**. Stage 2 copies the bytes through the `$2180` WRAM port out of
the `$7F:4Bxx`/`$7F:4F00` records, and `src/oam.c`'s `CtBuildFieldOAM` is this port's graded model of
that copy (`--checkoam`). Emitting them here would be a second, ungraded emitter of the same bytes, so
the buffer stays UNMODELLED rather than half-written -- an unwritten byte claims nothing, a half-written
one breaks a promise every frame. What IS ported is the CURSOR bookkeeping (B341/B397/B3BB: one high slot
+ 0x10 main bytes per drawn object, the group from `$0F80,X & 0x0C`) and the three hide-the-stale-tail
loops (B2C4-B306), because those are state.

Also newly ported, and the reason `$01D9` was diverging: `C0/B788`'s four direct-page stores
(`B796 STA $C5` / `B79C STA $C3` / `B79E STZ $E5` / `B7A4 STA $D9`, all SIXTEEN-bit under REP #20). A
live frame ENDS on those, because A810 runs before the draw pass -- measured `$01D9` = 0x0040, writer
`C0:B7A6`. `$01C3`-`$01C6` are shared with A88D's window edges (also now stored, A8A5/A8B1/A8BD/A8C9),
and both stores are in the all-or-nothing set for that reason: with no emit running, claiming those four
means holding the window edges where the oracle holds a screen position -- 4 masked divergences at f1,
measured.

## A tooling trap fixed on the way

`vmdiff-wmask-party3`'s `pin` regexes required `first masked divergence at frame` to be present, which
only happens when the check is RED. `regress.py --pin` refuses to rewrite anything if ANY pin regex
matches no output line, so re-pinning that check from a CLEAN run silently did nothing and printed the
nearby-lines dump -- which reads like success. The regexes now accept either state.

## Consequence for op 0xAB

`vmdiff_pinned_triage.md` section 5 wanted its completion keyed on `$7F0B01,X`. That byte now has both of
its ROM writers in the port: `C0/A934` (the deactivate path forces it back to 2, so an off-camera
object's blocking anime can never complete) and the drain. But it is only live under `CT_FRAMELEGS=1`, so
the op-0xAB fix is still gated on family 3 above.

## ⭐ THE SURVIVOR IS CLOSED — `--sweepdraw`, and it found a real defect on its first run

The mutation that inverted `C0/B309`'s gate (draw even when `B701` returns SEC) passed the whole suite,
and **not because the scenarios were unlucky.** Measured with a counter inside `CtAnimDrawPass`:

```
[probe] B701 SEC refusals over 130 frames: 0     party3_field
[probe] B701 SEC refusals over 130 frames: 0     bedslide
```

The reason is structural, and it is the SAME root cause as divergence family 3. `B701` refuses only
while `($1B00,X & 0x7F) < T` with bit7 **clear** — an object caught mid-staging — and the only thing
that leaves staging incomplete across a frame boundary is `C0/CA76`'s raster deadline. Our drain loops
on SEC until the queue empties, so every object arrives at the draw pass with `$1B00 = 0x80` and takes
the reposition-emit arm. **The refusal arm is unreachable from any scenario**, so seeding the input
space directly was the only honest way to grade it — sweep first, wire second, the lesson `C0/9E84`
already taught here.

`--sweepdraw` micro-executes `C0/B271` on a synthetic draw list: four objects, two of them CHAINED in
one bucket (so `$0E81` must be followed and `$6D` re-read after the JSR, as `B2B2` does), swept over
6 staging patterns × 3 class patterns × 3 priority patterns × 3 high-water configs = 162 cases.
**162/162 byte-exact**, with the arm tally asserted in the report (243 refuse / 216 reposition /
189 instantiate) so it cannot go blind the way the scenarios did.

### ⚠ THE STRIDE IS PER-CLASS, AND THE PORT HAD A CONSTANT — 81/162 on the first run

`C0/B309`'s **first** dispatch is on the sprite CLASS, not the priority group:

```
B316 BEQ ->B329    class 0
B31C BRL ->B3DF    class 1
B323 BRL ->B4B8    class 2
B326 BRL ->B5AF    class 3   (C0/C73A unported)
```

and each class body then picks the priority group and carries **its own** advance:

| class | group-0 arm | high bytes written | main cursor |
|---|---|---|---|
| 0 | `B341` | ONE — `$7F4F00` | `B35D ADC #0010` |
| 1 | `B3FB` | TWO — `$4F00` + `$4F01` (INX twice) | `B421 ADC #0020` |
| 2 | `B4D4` | THREE — `$4F00` + `$4F01` + **`$4B40`** | 0x30 |

So the advance is `($1201,X & 3) + 1` entries — exactly `ctOamEmit`'s own shape (`pieces = (cls+1)*4`
in groups of four, one high-table byte per group, which is why its `HIDST` table holds those three
destinations). The port advanced by one entry always. **No scenario could have caught it**: the failing
case had a class-1 and a class-2 sprite in the same priority group, where the ROM advances 5 entries and
the port advanced 2.

### ✅ `src/oam.c` HAD THE SAME ASSUMPTION — FIXED, with the sweep that licenses it

Done the second way: a micro-execution sweep of stage 2. See "STAGE 2 IS A **COPY**" at the end of this
dossier — and the entry count turned out to be the SMALLER of the two defects sitting there.
`CtBuildFieldOAMFull` stays 4-piece on purpose; the reason is there too.

### The mutation ledger, redone

| mutation | `--sweepdraw` |
|---|---|
| `C0/B309`'s gate inverted (the former survivor) | 162 → **54** |
| the per-class stride replaced by a constant | 162 → **81** |
| the two non-zero priority groups swapped | 162 → **90** |
| the high-water publish (`B2DA STX $7B`) dropped | 162 → **9** |
| the buckets walked low-to-high | 162 → **0** |

Still ungraded and said so: `CtAnimDrawPass`'s `unresolved` path, because class 3's `C0/C73A` carry is
not derivable — the same reason `--sweepdrain` counts class 3 instead of comparing it.

## ⭐ STAGE 2 IS A **COPY** — `--sweepstage2`, and modelling it as a computation hid two defects

The `--sweepdraw` finding (the per-class entry count) was only half of it. Reading `C0/B309`'s emission
properly settles what the routine *is*:

```
B34F  LDX $01DB / STX $81      the group cursor -> $2181, the WRAM-port ADDRESS
B369  LDA #04 / STA $01C9      the PIECE COUNT: 04 class 0 / 08 at B42D / 0C at B510
B36E  LDA $7F4BC0,X / STA $80      the piece's placed X   (+0)
B374  LDA $7F4BC1,X / STA $80      its CLAMPED Y          (+1)
B37A  LDA $7F4BC6,X / STA $80      its tile               (+6)
B380  LDA $7F4BC7,X / STA $80      its attr               (+7)
B386  TXA / ADC #0008 / TAX        the next 8-byte piece record
B390  DEC $01C9 / BNE ->B36E
```

with `ADC #0010 / #0020 / #0030` on the main cursor and ONE / TWO / THREE high-table bytes
(`$7F4F00`, `+$4F01`, `+$4B40` — the third BELOW the record window, not `$4F02`).

`CtBuildFieldOAM` had **two** defects, and both were invisible to every scenario:

- **It emitted one OAM entry per object.** A second object in the same priority group landed on top of
  the first.
- **It RE-DERIVED the placement `C0/B788` had already done** — `finalX = objX + xoff`,
  `finalY = objY + yoff`, and a rebuilt high byte. The ROM copies `+0` and `+1` out of the record. The
  **Y** is the one that bites: `ctOamEmit`'s Y half is seven hand-written clamp rules, and a plain add
  agrees with it only while no clamp binds. Two models of one computation, disagreeing exactly where the
  ROM bothered to have rules.

### Why `--checkoam` could not see either, and what it grades now

It runs the builder on the **oracle's live WRAM**, so the records are the oracle's own (values pass
through) and every object on that scenario is class 0 (the counts never differ). `--sweepstage2` seeds
the records with per-byte-distinguishable junk and sweeps the class over a 3-object list (two chained in
one bucket): **48/48 byte-exact, 26112 OAM bytes compared**, both `B701` arms seeded (72 reposition / 72
instantiate — the reposition arm leaves `+6/+7` as seeded junk, which is what makes a mis-addressed tile
or attr byte visible).

The rewrite was checked in **both** directions, because "it went green" is not evidence on its own:

- `oam-build-field`, `verifyframe-oam-lockstep` and `oam-stage1-assembly` stay **byte-identical** (they
  are pinned to exact strings) — which is the evidence that the copy agrees with the old re-derivation
  wherever the clamp does not bind, i.e. it is a fix and not a behaviour change on the graded scenario.
- `--checkoam` still reports **2424 mismatches** when the copy is deliberately mis-addressed, so it did
  **not** degenerate into a memcpy grade. It lost the placement MATH (which moved to where the ROM keeps
  it, graded by `--sweepdrain`) and kept the ADDRESSING.

### Mutation ledger

| mutation | `--sweepstage2` |
|---|---|
| the piece count back to 4 | 48 → **12** |
| the cursor stride back to a constant `0x10` | 48 → **28** |
| one high byte instead of `(cls+1)` | 48 → **12** |
| reading the record's `+4` instead of `+1` for Y | 48 → **0** |
| class 2's third high destination `$4B40` → `$4F02` | 48 → **24** |

That last one matters beyond this routine: `--sweepdrain`'s own notes record that swapping `$4B40` for
`$4F02` "changed nothing the sweep could see". It is graded now.

### `CtBuildFieldOAMFull` is a DIFFERENT model, and it stays 4-piece

It replaces the whole record pipeline — deriving the screen position from the world position and decoding
tiles straight out of the ROM — for a host that never ran `C0/B788`. Its 4-piece assumption is part of
that stub, not a stride to patch: 8 or 12 pieces would need the per-class FRAME-RECORD stride too
(`$28`/`$50`/`$78`, so `frame * 40` in `CtDecodeSpriteAssembly` is the class-0 case) and 8 or 12 tile
slots. `--verifyframe`, the only check that runs it, has nothing but class-0 objects, so extending it
would be ungraded — and the real answer is to **retire** it now that both halves of the real pipeline
(`ctOamEmit` via `--sweepdrain`, stage 2 via `--sweepstage2`) are graded.


## The last copy-stub in the OAM path: `CtBuildFieldOAMFull` retired, and `cTplAttr` widened

`CtPortRunFrame` (the `--verifyframe` harness) ended in `CtBuildFieldOAMFull`, a model that replaced the
whole record pipeline by deriving the screen position from the world position and decoding tiles out of
the ROM. Both halves of the real pipeline are graded now, so it is deleted and the real chain runs:
`CtAnimDrawPass` (the emit) then `CtBuildFieldOAM` (the copy).

⚠ **The obvious swap would have made the check VACUOUS.** `CtPortCopyWramFromOracle` pulls the oracle's
entire WRAM into `g_ram`, records included, so a bare stage-2 copy would read the oracle's `$7F:4BC0` and
write back the oracle's own OAM. Running the emit first is what keeps it honest — `B701`'s reposition arm
recomputes each piece's placed X and clamped Y into `+0`/`+1`. **Before replacing a stub with a port, ask
where the port's INPUT comes from: if it comes from the oracle, the port is a memcpy.**

Then `cTplAttr`, which cached the per-tile OBJ priority for **four** pieces. The tempting repair was to
teach `CtDecodeSpriteAssembly` the class-1/2 geometry — the per-class frame-record stride (`$28`/`$50`/
`$78`, applied by the ROM through the hardware multiplier at `C0/CBDC`: `$4202` = the frame, `$4203` = the
stride), the offsets at `stride - pieces*2` (`$20`/`$40`/`$60`), and the two class-2 tile-numbering
variants whose extra `$10` jumps at a different piece in each. **All of that is already ported and graded
as `C0/CAD9`'s frame half** (`--sweepcad9`, 165888/165888, four classes and both class-2 geometries), so
re-deriving it here would have been a second model of one computation — the defect just removed from
`CtBuildFieldOAM`, one file over.

So the per-tile Y offset now comes from where that producer staged it: `$7F:4800 + $1700,X + e*8`, `+4`.
The piece count is `$1201,X & 3`, and no record geometry has to be known here at all.

### ⚠ The wide arm is DERIVED BUT NOT OBSERVED, and that is measured

The report line now carries three denominators, and the third is the honest one:

```
C0/B701 emit ran on 606 objects; priority injected on 41760 pieces of classes 0x7, DRAWN classes 0x1
```

The injection walks every object slot and sees classes 0, 1 and 2. **Only class 0 ever reaches the
`$0E00`/`$0E81` draw list**, and the draw list is the only thing whose OAM bytes any lockstep check
compares. Proven directly rather than argued: writing `0xAA` into pieces 4..11 changes **not one compared
byte**, and so does reverting the count to a constant 4.

The three denominators do different jobs, which is why all three are pinned:

| counter | what it asserts |
|---|---|
| `emit ran on N objects` | a producer whose ABSENCE is invisible in the bytes (a correct emit only rewrites the stubbed records with the same values) |
| `pieces of classes 0xM` | how far the injection reaches |
| `DRAWN classes 0xD` | what the comparison can actually see |

A check reporting only the first two would read as coverage it does not have. What IS graded is the
class-0 path and the source change: mutating the staged-Y read (`$14804` → `$14802`) gives 212 attr-0x10
rows, swapping `$0C00`/`$0C01` gives 424 — both move the pinned line, though neither trips the exit code
(attr-0x10 is classified as expected priority timing).

## ⛔ TURNING THE LEGS ON BY DEFAULT — TRIED, MEASURED, REVERTED. Two real defects, both new.

The switch was flipped (default ON, `CT_FRAMELEGS=0` to disable) and the full suite run. **174 pass,
7 fail** — and not seven divergences: **five TIMEOUTS**, plus two immediate failures.

This corrects what the switch's own comment used to say. OFF was described as "measurement discipline,
not correctness". That was wrong. It is now a block by two defects with a one-line repro each.

### Defect A — the Y-sorted draw list CYCLES in the live engine

`ct_field --assets ct_assets.dat --scene 1 --worldtest` with `CT_FRAMELEGS=1`, instrumented at the
`B2B2` walk:

```
[drawpass] RUNAWAY bucket 7E head=00: 00(f00,p04) 04(f00,p80) 00(f00,p04) 04(f00,p80) ...
[drawpass] RUNAWAY bucket 7C head=00: (the same chain)
[drawpass] RUNAWAY bucket 7A head=00: (the same chain)
```

obj00 ⟷ obj04. Two things in that dump, and the second is the useful one:

- obj04's prev-link is `$80`, so **obj04 believes it is the head** — while `$0E00+7E` says the head is
  obj00. The head pointer and the prev-links disagree.
- **Six different buckets all name obj 0 as their head.** With two objects in the scene that is
  impossible in a well-formed list: each object is in exactly one bucket. Those are STALE heads.

⚠ **The scene-entry fill is NOT the culprit** — the obvious suspect, checked first: `CtObjSceneInit`
does write `$0E00..$0EFF = $80` at `CtSceneCreateIn`, `C0/B204`'s fill included. The list is corrupted
**during the run**, by unlink/relink leaving old heads behind.

**Why no check could have caught it before.** `--vmdiff` re-seeds the entire WRAM — the draw list with
it — from the oracle every frame, so a list our own link/unlink corrupts over time is repaired before it
can ever cycle. `--sweepdraw` builds a synthetic, acyclic list on purpose. The corruption is only
reachable in a host that maintains the list across frames, i.e. only in `ct_field`.

`C0/B2B2`'s loop is unbounded in the ROM because the ROM's chain is always well-formed. A **port-side
non-convergence guard** now bails at 128 (above any real chain — the list holds at most 64 objects), so
`CT_FRAMELEGS=1` on a live host produces a visibly wrong frame instead of a hang. That is a diagnostic,
**not a fix**, and the default was NOT flipped on the strength of it: shipping the default with a
corrupt list and a guard over it is exactly the spot-fix the mandate forbids.

### Defect B — the battle staging record, independent of A

`ct_field --scene 19 --battletest saves/battle_re/forest_pre.raw.wram` with `CT_BATTLE_MASHA=1`:

```
battletest: MISMATCH $29D7 slot2 rec mismatch (want 0A 07 08 0F 04 5E...)
battletest: FAIL (staging 6/7, stats OK, ack clean)
```

Still fails with the guard in, so it is not a consequence of A. Untriaged.

### What this changes about the earlier account

The four divergence families pinned by `vmdiff-framelegs-party3` are still accurate, and three of them
are still harness scope. But they were never the whole cost of turning the legs on — they were only the
cost *the lockstep harness can see*. The live engine has its own, larger price, and no `--vmdiff` number
hinted at it. **A switch measured only in the harness is measured in one host out of two.**

### Defect A, narrowed to a two-call window — NOT yet fixed

Four things eliminated, each by measurement rather than reading:

1. **`CtObjListLink` / `CtObjListUnlink` are not the fault.** Both were re-read against `C0/A9CD` and
   `C0/A98A` instruction by instruction, including the two quirks worth keeping: the head-path tail
   stores the *next* value (not always `$80`) into `$0E81,X`/`$0E01,X`, and `A9BC-A9C0` has no sign
   test so a tail unlink writes `$0E80 + $80` = `$0F00`. The port matches both. The bucket index cannot
   overflow the 64-entry head array either — `LDA $0A80,X` is 8-bit under `SEP #30`, so
   `(0xFF >> 1) & 0xFE = 0x7E`.
2. **The scene-entry fill runs and reaches the live image.** Probed directly at the call:
   `CtObjSceneInit ran: nobj=16 $0E7E=80 $0E80=80`.
3. **The init scripts inside `CtSceneCreateIn` do not do it.** A validator at the end of that function
   stays silent.
4. **It is already broken at `t1 frame-START`** — the first tick, before any leg:
   `bucket 7E head 00 prev=00 (not 80)`.

⟹ The writer is between `CtSceneCreateIn` returning and the first `CtSceneTick`. ⚠ **CORRECTED
BELOW — it is NOT the load path**, it is the per-frame pre-tick work. And the shape says *zeroing*, not mis-linking: `$0E7E` and `$0E80` read `00` where the
init left `80`, and `00` is a valid object index, so the walker then treats obj 0 as the head of every
bucket it finds — which is why the first dump showed six buckets naming obj 0 and a 00 ⟷ 04 "cycle"
that was never a cycle anyone built.

The validator used is worth rebuilding rather than reinventing: for every bucket, check the head's
prev is `$80`, that each node's next's prev points back, and that each member's own `$0A80` maps to the
bucket it is actually in. That last predicate is the one that localises the fault — it is what turned
"the list is broken" into "obj 00 is in bucket 7E but its `$0A80` wants bucket 00".


### Defect A, second pass: it is the PER-FRAME pre-tick work, not the load path

Six probes on the `$0E00` page (all-`$80` is the post-init invariant, so any non-`$80` byte before the
first tick is the fault), placed across the whole window and reporting only the first hit:

| probe | verdict |
|---|---|
| A — just after `CtSceneCreateIn` (fires on EVERY load, incl. the worldtest's location changes) | clean |
| B — after `fieldLoad` returns | clean |
| B2/C/D — after the lead placement, `CtSceneCameraEntry`, `CtScenePalSweepEntry` | clean |
| D2/D3/D4 — the entry table dump, the settling `fieldPlayerCollide`, before the main loop | clean |
| D5 — **top of the main loop** | clean |
| E — **right before the first `CtSceneTick`** | ⛔ `$0E01 = 00 (expected 80)` |

So the corruption happens **inside one iteration of the main loop, before the tick** — the per-frame
pre-tick work (player input, collision, camera/scroll, doors and exits, the X-menu check). The earlier
note's "load path" was wrong and is corrected above.

**The byte is the tell.** `$0E01` is object 0's *out-of-list* flag, and `00` is the value
`C0/A9CD` writes when it LINKS (`A9E6`/`AA03 STZ $0E01,X`). So the page is not being bulk-zeroed after
all — something is making object 0 look linked.

⚠ And it is **not a call to the list API**: `CtObjListLink`, `CtObjListUnlink`, `CtObjScreenPos`,
`CtAnimDrawPass`, `CtAnimRelease`, `CtObjSlotAlloc` and `CtBuildFieldOAM` have **zero callers** across
`field.c`, `scene.c`, `battle.c`, `menu.c` and `world.c`, and no engine file outside `move.c` writes a
literal address in the `$0E00` page. So it is an **indirect write** — a computed offset from some other
base landing in the page. Finding it is the next step, and the cheap way is a watch on `$7E:0E01`
(narrow the loop body by bisection the same way, or hook the one write) rather than more grepping.


### Defect A, third pass: the WATCH retired my own premise — `$0E01 = 0` is not corruption

A watch on `$7E:0E01` (object 0's out-of-list flag) at twelve points spread through the main loop's
pre-tick body, with the iteration number printed:

```
[w01] f2 L5497: $0E01 = 00        <- the TOP of frame 2
```

Nothing fired at any of the eleven later points during frame 1. So `$0E01` is `$80` throughout frame
1's pre-tick work and `00` by the top of frame 2 — which means it is written **inside `CtSceneTick`**.
And that is exactly what `C0/A9CD` is supposed to do: with the legs on, the frame legs LINK object 0,
and `AA03 STZ $0E01,X` is the link. **`$0E01 = 0` is normal operation, not damage.**

⛔ **So the previous pass's conclusion was wrong, and so was the reasoning that produced it.** The
"all-`$80`" property holds only until the FIRST tick; after that, a linked object legitimately has
`$0E01 = 0` and its bucket legitimately has a head. A whole-page "first byte that is not `$80`" scan is
therefore not a corruption detector at all — it reports healthy state — and the two claims built on it
("something is making object 0 look linked", "the writer is in the per-frame pre-tick work") are
withdrawn. They were an artifact of the probe, not a property of the engine.

Worse, the probe placements in that pass were inserted by raw line index into a file already shifted by
earlier insertions, so at least one of them ("D5: top of the main loop") did not land where its label
says. **A bisection is only as good as the placement arithmetic**, and mine was not checked.

### What still stands, and what the next pass should use

The ONE reproducible fact is the structural one, and it comes from the validator in `ct_vm.c`, not from
any byte value: with `CT_FRAMELEGS=1`, `ct_field --scene 1 --worldtest` reaches a list where

- a bucket head's object does not have `prev == $80`, and
- an object sits in a bucket its own `$0A80` does not map to (`obj 00 in bucket 7E but $0A80 wants 00`),

and the `B2B2` walk then runs away. That third predicate — **every member's `$0A80` must map to the
bucket it is in** — is the one that localises the fault, and it is the one to instrument next, sampled
per phase inside the tick rather than around it. The fault is somewhere in the leg sequence's
interaction with a `$0A80` that changes without the unlink/relink bracket; that is where to look, and
this pass did not find it.


### Defect A, fourth pass: the fault is present at P0 — the leg sequence is RULED OUT

The `$0A80`-maps-to-its-bucket predicate, sampled at nine phases INSIDE the tick (top of tick; after the
position commit; the rising-edge link; pre-legs; after unlink; after relink; the falling-edge unlink;
the party tail; after the drain), object index printed with each:

```
[bk] CtObjSceneInit image = 000002CED500E140 ownMem=0
[bk] validator image     = 000002CED500E140
[bk] t1 P0 top-of-tick (obj FFFFFFFF): obj 00 in bucket 7E but $0A80=00 wants 00, flag=00 prev=00
```

**P0 is the first statement of the first tick.** The list is already inconsistent before the tick does
anything, so **the leg sequence cannot be the cause** — which retires the hypothesis the third pass
ended on ("the leg sequence's interaction with a `$0A80` that changes without the unlink/relink
bracket"). None of P1-P8 ever fired.

The two-images theory is also dead: the validator and `CtObjSceneInit` print the SAME pointer and
`ownMem = 0`, i.e. one shared host image, as intended.

So the state at the first tick is `$0E7E = $0E01 = $0E80 = $0A80 = 00`, while `CtObjSceneInit`
provably writes `$0E00..$0EFF = $80` on that same pointer and the end of `CtSceneCreateIn` validates
clean. Something zeroes that page in between. That is where the second pass already pointed, but it is
established now on the real structural predicate instead of a byte value, and with the whole leg
sequence eliminated.

⚠ **Honest caveat on this whole investigation.** Across four passes the intermediate probe readings
have NOT been mutually consistent — an earlier `$0E01` watch reported the byte still `$80` throughout
the first main-loop frame, which cannot be reconciled with P0 seeing it as `00` on the first tick. At
least one placement in an earlier pass is known to have landed off its label. Treat only the P0 result
as load-bearing: it is the one taken with a validator that tests the structural invariant, prints its
own image pointer, and fires on the first call. **The writer is still not found.**

### Defect A, fifth pass: the WRITE WATCH — and it kills the corruption premise outright

`CT_WATCH=<hex offset>` (documented in `docs/DEBUG_SURFACE.md`) protects the page holding one WRAM byte
and reports every store to it with function + file:line. It answered in one run what four passes of
bisection could not.

**`CT_WATCH=0E7E`** — the bucket head the validator kept flagging:

```
[watch] WRITE ... from CtObjSceneInit +0x8f    src/move.c:5850
[watch] WRITE ... from CtObjSceneInit +0x8f    src/move.c:5850
```

**Two writes in the whole run, both the `$80` fill, one per scene load. NOTHING ELSE EVER WRITES IT.**
So `$0E7E` is never corrupted, and the premise every pass since the second has been built on — "something
zeroes or overwrites the `$0E00` page" — is FALSE. Whatever the validators were reporting, it was not a
write to that byte.

**`CT_WATCH=0E81`** — object 0's next-pointer, the byte that actually formed the observed cycle:

```
CtObjSceneInit   move.c:5850     the $80 fill
w8               ct_vm.c:331     <- the only VM-side writer
CtObjListUnlink  move.c:5888
CtObjListLink    move.c:5920
CtObjListUnlink  move.c:5899
...
```

Everything is a legitimate list operation except the `w8` line, and that resolves to exactly one place:
`ct_vm.c:3466-3467`, the A88D rising-edge one-shot (`A8EC STA $0E80,X` / `A8EF STA $0E81,X`).

### The one question left, and it is now single

Does that one-shot ever fire on an object that is **still linked**? If it does, clobbering
`$0E80`/`$0E81` to `$80` orphans the chain and leaves the object looking like a head while a bucket head
still points at it — which is precisely the shape of the original dump (`obj04 prev=80` "believes it is
the head" while `$0E7E` names obj00). In the ROM that cannot happen, because a linked object is an
active one and an active object has no rising edge. The port has at least one path that could break
that pairing: `palSweepB0E6` sets `$0F00,X = 0x80` at scene entry without linking, so entry-time
activation and list membership are set by different code. **That is the next thing to test, and it is a
single predicate: assert `$0E01,X` is negative whenever the one-shot runs.**

I did not get that far. What this pass delivers is the tool, two hard writer censuses, and the
retirement of a premise that had survived three passes because nobody made the store speak.

### Defect A, sixth pass: the predicate FAILS — and the watch's own false negative was the reason it looked impossible

**The predicate `$0E01,X` negative at the one-shot fails, comprehensively.** On tick 1 the A8EC one-shot
runs on EVERY object with `$0E01 = 00`:

```
[oneshot] t1 obj 00 STILL LINKED at A8EC: $0E01=00 prev=00 next=00, head of bucket 7E, $0A80=00
[oneshot] t1 obj 02 ... head of bucket 54 ...
[oneshot] t1 obj 04 ... head of bucket 76 ...
```

But read the OTHER columns before concluding anything: `prev`, `next` and `$0A80` are **also** `00`, for
every object. That is not "linked" — that is the page sitting in its ZEROED state, which makes
`$0E01 = 0` look like "linked" to any predicate written in terms of the flag. So the hypothesis this
test was built on (an activation/link pairing broken by `palSweepB0E6`) is **not** the cause either.

⛔ **AND THE PREVIOUS PASS'S WRITER CENSUS WAS WRONG, because of a false negative in my own watch.** It
filtered on `hit == gWatchAddr`, but a WIDE store — a `memset`, or any vectorised fill — that covers the
watched byte faults at the store's FIRST address, not at our byte. So every bulk write was silently
stepped through, and the tool reported "the only writer of `$0E7E` is `CtObjSceneInit`" when it was not.
That single line of filtering is what made the contradiction look unresolvable for two passes.

Fixed by not filtering on the address at all: remember the byte, single-step, compare after. Width-
independent, and it caught it immediately — per scene load, in this order:

```
[watch] 00 -> 80 by CtObjSceneInit +0x8f      src/move.c:5850
[watch] 80 -> 00 by __NLG_Return2 +0xd97      <- a CRT bulk store, no line info
[oneshot] t1 ... $0E7E=00
```

**So the page IS zeroed after the init and before the first tick, by a CRT bulk store** (`__NLG_Return2`
is the nearest preceding symbol, i.e. an unnamed static — a `memset`/`memcpy` thunk). The engine-side
candidates are few (`memBoot`'s `memset(m, 0, sizeof *m)` at ct_vm.c:104 and
`CtSceneSeedFromWram`'s `memcpy(sc->mem->s, w, 0x10000)` at 2675), and it is NOT `worldInit`'s
`memset(w, 0, sizeof *w)`, which clears the World struct and preserves `keepWram`.

**The one remaining step is mechanical**: the handler knows the faulting PC but not its CALLER, so add
`RtlCaptureStackBackTrace` at the fault and print the first frame outside the CRT. That names the call
site outright, and it is the same "make the store speak" move that has been the only thing working here.

The watch's wide-store fix is landed; the throwaway predicate probe is not.

## ⭐ DEFECT A, FOUND: `worldInit` copies the OVERWORLD's object blocks over the FIELD's draw list

The watch's handler now walks the FAULTING frame's stack (`StackWalk64` on a copy of `ContextRecord`,
resolved with dbghelp), so the CRT bulk store names its caller:

```
[watch] 00 -> 80 by CtObjSceneInit +0x8f
[watch]     #0 CtObjSceneInit   src/move.c:5850
[watch]     #1 CtSceneCreateIn  src/ct_vm.c:2528
[watch]     #2 fieldLoad        src/field.c:1725

[watch] 80 -> 00 by __NLG_Return2 +0xd97
[watch]     #1 worldInit        src/field.c:236       <- THE WRITER
[watch]     #2 main             src/field.c:7064
```

`src/field.c:236` is

```c
memcpy(w->wram + OWOBJ_BASE, p, 0x1000);   // the 64 object blocks (snapshot)
```

and `OWOBJ_BASE` is **`0x0B30`** (`src/ct_owobj.h:8`), 64 blocks of `$40` = `0x1000` bytes, i.e.
`$7E:0B30 .. $7E:1B2F`. **That span contains `$0E00..$0EFF` whole** — every bucket head, every
out-of-list flag, every prev and next of the field's Y-sorted draw list.

So on a world entry the overworld's ambient-object snapshot is bulk-copied straight over the field's
draw list, leaving that page holding whatever the asset has there (zeros). Everything observed follows
from it: the page reading all-zero at the first tick after a world entry, `$0E01 = 00` looking like
"linked" for every object, six buckets naming obj 0, and the obj00 ⇔ obj04 "cycle" that was never a
cycle anyone built — just zero bytes read as object indices.

### Why it only shows now, and what it is NOT

The overlap is the ROM's own layout — the overworld really does keep its object blocks at `$7E:0B30`,
and field and overworld really do share WRAM. It became reachable here because (a) `$7E` became
process-lifetime and the world now runs on the HOST image (the ownership move), and (b) nothing read
`$0E00` in a live host until the draw pass was wired. Two correct changes intersecting.

It is therefore **not** a bug in `CtObjListLink`/`CtObjListUnlink` (byte-faithful), not the A88D
one-shot, not the leg order, and not the scene-entry fill — all four were eliminated by measurement
across the earlier passes.

### What the fix has to answer first (NOT yet done)

The ROM does not run the field engine while the overworld is up, so it never reads that page while the
world owns it. Before changing anything, derive: does `ct_field` tick the field VM during a world, and
is the field scene re-created (re-running `C0/B204`) on the way back? If the answer is "the field VM
should not be ticking", the fix is a gate, not a relocation — and moving `OWOBJ_BASE` would be pure
invention, since `$0B30` is the ROM's address.

### The tooling lesson, which cost six passes

Bisection by probe never found this and twice produced confident, wrong answers. What found it was
making the store speak — and even then the first version of the watch lied, because filtering on
`hit == gWatchAddr` misses any wide store. **When a byte changes and you cannot say who changed it: trap
the page, compare the value across a single step (never the faulting address), and walk the faulting
frame's stack.** All three parts were needed; the third is what turned "a CRT bulk store" into a line
number.

### ⚠ DEFECT A: the GATE is right but it is NOT the fix — corrected below, see "the gate, graded"

The question the fix turned on was whether the ROM runs the field engine while the overworld is up.
Measured from `saves/ow_w0.state`, visits PER FRAME:

| routine | on the overworld |
|---|---|
| `C0/0097` the field main-loop head | **0** |
| `C0/00C4` / `C0/5A46` the event VM | **0** |
| `C0/A810` movement | **0** |
| `C0/B271` the draw pass | **0** |
| `C0/B204` the draw-list fill | **0** |

and the CPU ends every frame in bank `$C2`, the overworld's own engine. **Positive control** (this
project's own rule about negatives): the same probe on `saves/truce_market.state` reports 1 visit/frame
to `C0/0097` with `$6D = 16`, so the hook is live and the zero is a real zero.

So the ROM never reads the field's tables while the world owns that RAM, and the port did:
`field.c`'s tick was gated on `!battlePhase` alone. The fix is one term:

```c
if (!battlePhase && !inWorld && f.vm) CtSceneTick(f.vm);
```

A GATE, not a relocation — `OWOBJ_BASE = $0B30` is the ROM's own address and moving it would be
invention. Full suite 181/0/0, and `ct_field --scene 1 --worldtest` with `CT_FRAMELEGS=1` passes.

⚠ **THE GATE IS DERIVED BUT UNGRADED, and the green run does not say otherwise.** The suite was
181/0/0 *before* this change too, so no check goes red when the `!inWorld` term is removed: the
corruption it prevents is only observable with the frame legs on, and they are default OFF, and the one
check that exercises them (`vmdiff-framelegs-party3`) runs `ct.exe`, which never links `field.c`. Closing
that needs a `ct_field` check with `CT_FRAMELEGS=1` that asserts `CtAnimDrawPass`'s non-convergence guard
never trips — today the guard silently absorbs the very fault this gate fixes, so with the guard in
place even the unfixed engine "passes" the worldtest. That is the next honest piece of work here.

## ⭐ DEFECT B TRIAGED: it is not a second defect — it is family 4 wearing a different hat

`ct_field --scene 19 --battletest forest_pre` with `CT_BATTLE_MASHA=1` reported `staging 6/7` with the
legs on. The check only printed what it WANTED, which is a real weakness in a check, so it now prints
both:

```
MISMATCH $29D7 slot2 rec mismatch: got 0A 07 08 0F 05 5E... want 0A 07 08 0F 04 5E...
```

**One byte, `+4`, and it is `05` where the oracle has `04`.** `field.c`'s stager builds that byte as

```c
r[4] = ((s[0x1201 + X] & 7) >= 3) ? 0 : s[0x0F01 + X];
```

— `$0F01,X`, the **animation FRAME**. And `$0F01` is committed by exactly one thing: a FIFO pop in
`C0/CA76`'s drain (written up above: "`$0F01` IS COMMITTED ONLY ON A FIFO POP"). `04 -> 05` is one frame.

**Confirmed by removal, not by argument.** Leaving the legs ON and passing the drain a scanline at or
past its budget (`0x7F` vs `$016B` = `$0078`), so `CA76`'s deadline refuses the whole pass:

```
battletest: PASS (staging 7/7, stats OK, ack clean)
```

So the staging record is fine and the enemy's anim frame is one step ahead — which is **family 4**, the
raster clock: `CA85-CA9B` compares a V counter that advances WHILE the drain works, so a clockless port
drains fully where the ROM's deadline splits the work, and commits `$0F01` early. Exactly the shape
already measured on obj32 at f74 (`$0F01` ours=07, oracle=FF).

⚠ **This corrects the earlier framing.** Defect B is independent of defect A — that part held, and the
overworld gate does not touch it — but it is NOT an independent DEFECT. There is one engine-side blocker
left for the frame legs, not two, and it is the raster clock.

### What that leaves for turning the legs on

One item, and it is the one that is not derivable without cycle-counting the drain. Note the two
scanline values are both wrong in a knowable direction: `0xF0` drains fully (commits too early, breaks
the battle staging), `>= $016B` refuses entirely (would strand the queue). The ROM sits between them by
consuming real raster time per visit. Anything else is a tuned constant, i.e. invention.

## THE SAME RASTER CLOCK DECIDES THE EVENT VM's VISITS -- measured on the attract's loc 112 (2026-09-15)

`C0/5A46`, the runner, has the identical deadline one screen up from `C0/CA76`'s:

```
5A46 LDA $213F / LDA $2137 / LDA $213D (x2, AND #01)   ; the live scanline, exactly as CA85-CA9B reads it
5A58 CMP #00F0 / BPL ->5A61                            ; past 240: treat as the top of the next frame
5A5D CMP $69   / BCS ->5A90                            ; >= $0169 -> RTS: the REST OF THE QUEUE DOES NOT RUN
5A61 ... dequeue the next object, JSR C0/5A93 (its visit), BRA ->5A49 (re-read the scanline)
```

$0169 reads $0048 = **line 72**, and the runner starts around line 12. So a frame gives the whole queue
about sixty scanlines, and an object far enough down a long queue does not get a visit at all that frame.

MEASURED (`--hookpc 4FAB --from <the attract at f2475>`, the op-0x96 handler, which is where a scripted
move ADVANCES): in loc 112 the cartridge visits between 0 and 8 movers a frame -- `$6D` = 02 06 08 0C 12
14 16 18 1C 1E 26 28 2A 2C, fourteen of them queued -- and **obj4 ($6D = 08) gets a visit every OTHER
frame**, with whole frames (f7, f11, f15, f19, f23 of that window) where the handler is not reached at
all. Its position shows it: `$7E:1888` steps 2.4375 px every two frames from f2482 to f2508 and then
every frame from f2509, as the queue shortens. The port visits every eligible object every frame, so its
obj4 walks the whole way at the double rate: its `04 08 17` wait is 192 frames against the cartridge's
203, `04 06 28` 44 against 48, and loc 112's leg is 417 frames against 433.

So the residual that is left in the attract's field legs after the load window and the sequence opening
were priced (112 -16, 01B +2, 0DB +4, 0A5 -7, 0E6 -3) is this clock, and it is the same one named in the
section above: it needs the CPU time of every VM visit, i.e. a cycle cost for each of the 217 field-script
opcodes' handlers plus the runner's own per-visit overhead. `--sweepops` already micro-executes every one
of them against the ROM, so the costs are measurable the same way `--loadcost` measures C3/0557's; what is
NOT available is a shortcut, and a per-scene constant would be invention.

## ⛔ COUNTING THE DRAIN'S CYCLES: the counting says it is NOT a counting problem

`--hookpc` now reports the PPU scanline (`snes->vPos`, the same quantity `CA85-CA9B` assembles from two
`$213D` reads) at every visit, so both inputs to `C0/CA76`'s deadline can be measured instead of guessed.
The deadline is `v >= $016B`, and `$016B` reads `$0078` = **120** on every state tried.

**Input 1 — WHERE THE DRAIN STARTS (`vPos` at `C0/00D6`).** It is not a constant, and it is not even
constant per scene:

| state | vPos at the drain entry |
|---|---|
| `truce_market` | 13, 18, 13, 13, 13 |
| `party3_field` (walking) | 56, 57, 61, 66, 68, 69, 73, 77, 80 … |

**Input 2 — WHAT A VISIT COSTS.** From the frames that take two visits (`--hookpc CAD9`, party3_field):

```
f5   vPos 69 -> 70    Δ 1
f10  vPos 68 -> 70    Δ 2
f15  vPos 58 -> 59    Δ 1
f20  vPos 68 -> 69    Δ 1
f25  vPos 73 -> 112   Δ 39      <- the expensive pass
```

So a cheap staging visit costs **1-2 scanlines** and an expensive one — the pass that commits, blits the
512-byte tile pool and appends the upload — costs about **39**. That single number explains the split
this whole investigation started from: at a start of ~73 there is room for one expensive pass (73 + 39 =
112 < 120) and not two, so the ROM does pass 1 and stops, exactly as observed on obj32 at f74.

### Why this cannot be turned into a model, stated as a result rather than a difficulty

A per-visit cost TABLE is derivable — oracle-anchored per pass type, the same standing as
`copyFreeze = 8`. **The start is not.** `vPos` at `00D6` is emergent from everything the frame did
before it: the scheduler, the whole event VM, the two bank-`$FD` calls, the movement pass. Ranging 13
to 80 across two states, it is a function of the port's OWN execution time, which the port does not
model and cannot without becoming a cycle-counting interpreter. Picking a start would be a tuned
constant, and a tuned constant here is invention — the thing the mandate forbids and the thing that
would make every downstream `$0F01` "match" for the wrong reason.

**So the honest verdict on family 4 is: not closeable at this layer.** The two options the current
`scanline` argument offers are both wrong in a known direction (`0xF0` drains fully and commits `$0F01`
early — that is defect B; `>= $016B` refuses and strands the queue), and the ROM sits between them by
consuming real time.

### What would actually unblock the frame legs

Not a clock. Either
1. make the port's drain stop where the ROM's does by a DERIVED rule rather than a timer — which needs
   evidence that the split is structural (e.g. always after the pass that appends an upload) and not
   merely temporal; the f5/f10/f15/f20 frames, which fit two cheap visits comfortably, are the cases
   that would refute a "one pass per frame" rule; or
2. accept the one-frame lead and stop grading `$0F01` at frame granularity in the checks it breaks —
   which is a scoping decision, not a fix, and should be labelled as one.

Both are decisions for a human. What is settled is that counting cycles does not produce a model.

## ⛔ THE STRUCTURAL RULE DOES NOT EXIST — derived, with the numbers

**From the code first.** `C0/CA76`'s loop has exactly TWO exits, and neither is structural:

```
CA85-CA9B   the raster deadline   -> exit
CAA2 LDA $76 / CAA4 BMI           -> exit, queue empty
CAAF JSR CAD9 / CAB1 BCS ->CA85   SEC: loop WITHOUT popping
CAB3..CAD3                        CLC: pop, loop
```

There is no "stop after the pass that appends an upload", no per-frame cap, no count. So a structural
rule could only be an emergent property of the QUEUE, not of the loop — i.e. it would have to be true
that the queue simply never holds more work than one frame's worth. That is a measurable claim.

**Measured** (`--hookpc` at `C0/00D6` and `C0/00D9`, party3_field walking, 129 frames paired):

| whole-pass cost, scanlines | frames |
|---|---|
| 0-4 | 83 |
| 39-41 | 15 |
| 50-55 | 31 |

A small discrete cost table — that half IS derivable and oracle-anchorable. But the exit position kills
the structural reading:

```
drain EXIT vPos: max = 153,  >= 120 (the budget $016B) on 13 of 129 frames
ends >= 110: 110 110 111 111 112 113 114 115 116 117 117 118 118 118 118 119 119
             120 120 122 125 126 130 130 133 134 138 145 151 153
```

**The deadline really bites, on ~10% of frames**, and the ends past 120 (up to 153) are the last visit
overshooting before the next check. So the ROM is stopping on the CLOCK, not on the queue running dry.

⚠ A correction I nearly published: looking only at the highest-COST frames (which end at 109-119) it
looks as though every pass ends by queue-empty and the deadline never fires. That reading is wrong — it
samples the wrong statistic. The cost and the end position are different things, and it is the END that
answers the question.

### Verdict

No structural rule. Reproducing where the ROM stops needs the START (`vPos` at `00D6`, measured 13-19 on
`truce_market` and 45-65 on `party3_field`), which is emergent from everything the frame did before the
drain and is a function of the port's own execution time. The cost table alone is not enough.

So of the two options this dossier left open, option 1 is now CLOSED as underivable, and what remains is
option 2: **accept the one-frame lead as a stated scope limit** rather than model it. That is a scoping
decision, and it should be labelled as one wherever it bites — today that is `vmdiff-framelegs-party3`'s
family 4 and the battle staging's `$0F01` byte.

## ⚠ THE GATE, GRADED — and it turns out NOT to fix defect A

`CtAnimDrawPass`'s non-convergence guard was absorbing the corruption in silence, so the worldtest passed
either way and the `!inWorld` gate was ungraded. The guard now counts its trips and `ct_field --worldtest`
reports them:

```
worldtest: draw-pass runaway trips: 15 (0 = the $0E00 draw list stayed well-formed)
```

**With the gate: 15. Without the gate: 15.** Byte-identical. So the gate makes NO difference to the draw
list, and the earlier "DEFECT A FIXED" claim was wrong — caught by the grading on its very first run,
which is exactly what grading is for and exactly why "the suite is green" was never evidence here.

**The gate stays**, on its own merit: it is derived ROM fidelity (`--hookpc` from an overworld state
reports zero visits/frame to `C0/0097`, `00C4`, `5A46`, `A810`, `B271`, `B204`, with the CPU always in
bank `$C2`). It is correct; it is just not the repair.

**What survives of the diagnosis:** `CT_WATCH` did name `worldInit`'s
`memcpy(w->wram + OWOBJ_BASE, p, 0x1000)` (field.c:236, `OWOBJ_BASE = $0B30`) as a real writer of
`$7E:0E7E`, and `$0B30..$1B2F` does contain the `$0E00` list whole. That fact stands. What does NOT stand
is that it is the only path — 15 trips survive the gate.

**Next step, and it is mechanical:** run `CT_WATCH=0E7E` with the gate IN and read the backtrace of
whatever still writes it. The tool already resolves function + file:line; this is one run.

The count is pinned by `field-worldtest-roundtrip` as a KNOWN-BAD RATCHET — 15 is a defect count, not a
target, and must only ever go down.

### The SECOND writer: `worldEnterAt`, field.c:384 — same overlap, every entry

`CT_WATCH=0E7E` with the gate in, backtraced:

```
00 -> 80  CtObjSceneInit   <- field scene created
80 -> 00  worldInit        field.c:236   (first world entry, the one-time load)
00 -> 80  CtObjSceneInit   <- returning to a field re-creates the scene, list refilled
80 -> 00  worldEnterAt     field.c:384   (EVERY world entry)
```

`field.c:384` is `memcpy(w->wram + OWOBJ_BASE, w->objSnap, 0x1000)` — the flock-state restore. Same base
`$0B30`, same 0x1000 length, same overlap with `$0E00..$0EFF`. So the wipe is not a one-off at world
LOAD: it happens on every world ENTRY, from a second call site the first census never saw because that
census stopped at the first writer it found.

⚠ **This still does not explain the 15 trips**, and I will not claim it does. The sequence above shows
the list being refilled by `CtObjSceneInit` on the way back, and the field VM is gated off while
`inWorld`, so on this trace there is no window where a wiped list is walked. Either `inWorld` is not set
for the whole interval the world owns that RAM (a transition frame on either edge would be enough), or
the trips come from somewhere the `$0E7E` watch does not see. **Where the 15 come from is open.**

The next probe is the same tool pointed at the consumer rather than the producer: count the trips per
frame and print the frame number alongside `inWorld`, which places them on the timeline in one run.

### ⛔ THE 15 TRIPS ARE NOT THE OVERWORLD AT ALL — they are frames 0-14 of the FIRST scene

Counting the guard trips per frame with `inWorld` beside them:

```
[trip] f0  +1 (total 1)  inWorld=0 scene=1 wtStage=0
[trip] f1  +1 (total 2)  inWorld=0 scene=1 wtStage=0
 ... one per frame ...
[trip] f14 +1 (total 15) inWorld=0 scene=1 wtStage=0
worldtest: on the OVERWORLD at (440,280), stage 1     <- the first world entry, AFTER all 15
```

**One trip per frame, frames 0 to 14, on the opening field scene, before any world entry, and then it
stops for the rest of the run.** So the overworld is not involved in the trips at all.

⚠ This retires the causal story two passes built up. `worldInit` (field.c:236) and `worldEnterAt`
(field.c:384) DO both memcpy `0x1000` bytes over `$0B30..$1B2F`, which does contain `$0E00..$0EFF` — that
is measured and stands as a fact about those call sites. But they happen after frame 14, so **they are
not the cause of the malformed list**, and `worldInit` being the first non-`CtObjSceneInit` writer the
watch found made it look like the culprit when it was only the first thing looked at.

It also explains why the `!inWorld` gate changed the count by zero: the gate suppresses ticks during the
world, and every trip happens before the world exists.

**Where this points instead:** the first ~15 ticks of a fresh scene, which is exactly the window the
fourth pass already isolated (`t1 P0`, the very first tick, saw the `$0E00` page reading all-zero even
though `CtObjSceneInit` provably wrote `$80` to it and the end of `CtSceneCreateIn` validated clean).
Those two observations are the same phenomenon seen from opposite ends, and the self-heal at f15 is the
new datum: whatever is wrong resolves once the scene settles.

**Next probe:** the page contents at the top of ticks 1 and 15 on a PLAIN `--scene 1` run with no
worldtest, which removes the world from the picture entirely and asks what changes at the boundary.

### The plain-scene control: ZERO trips, list well-formed — so it is input, not the world

`ct_field --scene 1 --selftest --frames 18`, dumping the page at the top of every tick:

```
[pg] t1  heads=0  firstBad=-1 | $0E7E=80 $0E01=80 $0E80=80
[pg] t2..t18  heads=3  firstBad=-1 | $0E7E=80 $0E01=80 $0E80=80
```

Three bucket heads from t2 on, no malformed bucket, page intact for eighteen ticks. **A plain field scene
does not corrupt the list at all.**

And `worldInit` is called from four sites (field.c 5239, 5260, 7071, 7300), every one of them inside the
main loop's world-transition handling and none at startup — so it runs after f14, which independently
confirms the previous entry: **the world code is not the cause of the trips.**

So the difference between the two runs is not the overworld. It is that `--worldtest` DRIVES THE PLAYER
from frame 0 (it has to walk him out of the house) while the plain selftest leaves the scene static. A
moving lead churns the draw list every frame — activations, deactivations, bucket changes as screen Y
moves — and the static run exercises none of that.

**Next probe, and it should be the last localisation step:** the same page dump on a plain `--scene 1`
with driven input. If the trips appear, the fault is in the churn — the unlink/relink bracket under a
moving object — and the world drops out of the story entirely. That also makes it reproducible without
the worldtest, which is what a check would need.

### The input-churn hypothesis is refuted too — both controls are clean

Driving the lead on a plain scene (`CT_DRIVE=d ct_field --scene 1 --selftest --frames 20`, forcing
`dx/dy` the way `--worldtest` does):

```
[pg] t1        heads=0  firstBad=-1
[pg] t2..t20   heads=3  firstBad=-1
```

Twenty ticks with the player walking, and the list stays well-formed. So **it is not the churn either**.

Where that leaves it, stated as a ledger rather than a story, because three hypotheses have now died to
controls:

| suspect | status |
|---|---|
| `CtObjListLink` / `CtObjListUnlink` | eliminated — byte-faithful to A9CD / A98A |
| the A88D one-shot / leg order | eliminated — the fault is present at P0, before any leg runs |
| the scene-entry fill | eliminated — `CtObjSceneInit` provably writes `$0E00..$0EFF = $80` |
| `worldInit` / `worldEnterAt` overwriting the page | REAL WRITERS, but all four call sites are world-transition sites and run after f14, while the trips are f0-f14 |
| the `!inWorld` gate | correct ROM fidelity, but 15 trips with it and 15 without |
| input churn from a moving lead | eliminated — 20 driven ticks on a plain scene, clean |

**The trips are worldtest-only and happen in its first fifteen frames, and nothing yet explains what is
different about that path before frame 0.** The remaining difference to chase is the `--worldtest`
STARTUP path itself (what it sets up that a plain `--scene 1 --selftest` does not), not the world
transitions, and not the field engine.

⚠ The methodological point is worth more than the bug at this stage: **a confirmed writer is not a
confirmed cause.** `worldInit` survived two passes as "the answer" because it was the first thing the
watch reported and nobody ran the control that removes it from the scene. Three suspects have now died
to controls that took one command each.

## ⭐ MINIMAL REPRO FOUND: it is the START POSITION, not the worldtest

The only thing `--worldtest` does before the main loop that a plain run does not is **field.c:5291**:

```c
if (worldTest) { px = 176; py = 216; }   // start ON scene 1's world exit (the front door)
```

and that line sits at 5291, a hundred lines AFTER the fresh-entry block at 5183-5195 which places the
lead, runs `CtSceneCameraEntry` (5191) and `CtScenePalSweepEntry` — all with the DEFAULT spawn. So the
harness sets the camera and sweeps the palette for one position and then teleports the player to another
without re-running either.

**Confirmed by control.** Forcing the same start on a plain scene (no `--worldtest`, no overworld, no
input):

```
default spawn      -> [pg] t2..t20  heads=3   firstBad=-1     clean
CT_STARTAT=176,216 -> [pg] t1..t15  heads=62  firstBad=126    CORRUPT from tick 1
```

62 of the 64 buckets carrying a head, and bucket `$7E` malformed, from the very first tick — the same
shape the whole investigation has been chasing, now reachable in one process with no world, no
transition and no player movement.

### What this settles and what it does not

It settles the LOCALISATION: everything blamed so far — the overworld memcpys, the `!inWorld` gate, input
churn, the list routines, the leg order, the scene-entry fill — is off the hook, and the trigger is an
entry-order defect in the harness's own start-position handling.

It does NOT yet say WHY a displaced start yields 62 occupied buckets rather than a few mis-bucketed
objects; a stale camera alone should mis-sort, not populate the whole page. That is the next question,
and it is now askable on a two-second repro instead of a 1200-frame worldtest.

⚠ And a `CT_STARTAT`-style knob is worth adding for real: this bug was invisible for six passes partly
because the only path that triggered it also dragged in an overworld, a location change and a scripted
walk. **A repro that isolates one variable is worth more than any amount of instrumentation on one that
does not.**

### Why 62 buckets — the arithmetic rules out links, and the repro was not what I called it

**62 occupied buckets is impossible from linking.** The scene has 16 objects, so at most 16 buckets can
carry a head that `C0/A9CD` wrote. 62 means ~46 buckets hold bytes that no link ever placed — i.e. the
page is NOT in its post-`CtObjSceneInit` `$80` state at tick 1. A mis-aimed camera can only MIS-SORT the
objects it has; it cannot populate the page. So "the stale camera scatters the buckets" is arithmetically
dead, and with it the reading I gave the repro last commit.

**And the repro is not "a displaced start".** field.c:5291's own comment says what (176,216) is:

```c
if (worldTest) { px = 176; py = 216; }   // start ON scene 1's world exit (the front door)
```

That position IS the exit tile. So `CT_STARTAT=176,216` did not isolate the start position — it put the
player on a door, which fires a LOCATION CHANGE on frame 0. Everything then lines up:

- the trips are frames 0-14, the length of a transition;
- a location change tears the scene down and rebuilds it while the draw list is live;
- 62 buckets = the page mid-teardown, before the new scene's `CtObjSceneInit` refills it;
- it is worldtest-only because only the worldtest starts on an exit;
- and the `!inWorld` gate cannot help, because the trips happen during the transition, before `inWorld`
  is ever set — which is finally a clean explanation for "15 either way".

**LEADING EXPLANATION, NOT A FINDING.** The one-command test that separates the two readings: start at a
displaced position that is NOT an exit (e.g. 176,180). Clean there means the trigger is the
door/transition, not the displacement, and the search moves to the location-change teardown — which is
where `CtSceneTick` runs against a list whose scene is going away.

⚠ Fourth time this investigation that a "reproduction" carried an uncontrolled second variable. The
pattern is consistent enough to name: I keep confirming that a suspect is PRESENT and treating that as
proof it is RESPONSIBLE. The cheap discipline that has caught it every time is the control that removes
the suspect while keeping everything else — and it costs one command.

## ✅ THE TRIGGER IS THE DOOR, NOT THE DISPLACEMENT — controlled

Same scene, same X, 36 pixels apart in Y, one on the exit tile and one not:

```
CT_STARTAT=176,180   [pg] t1 heads=0  t2.. heads=3   firstBad=-1     CLEAN
CT_STARTAT=176,216   [pg] t1 heads=63 t2.. heads=62  firstBad=126    CORRUPT from tick 1
```

**The displacement is not the trigger. Standing on the exit is.** Starting on the door fires a LOCATION
CHANGE on frame 0, and that is what leaves the `$0E00` page holding 62 bucket heads no link could have
written.

So the fault lives in the LOCATION-CHANGE path: the scene is torn down and rebuilt while `CtSceneTick`
is still walking its draw list. The 15-frame duration is the transition, the self-heal at f15 is the new
scene's `CtObjSceneInit`, and `!inWorld` never helped because all of it happens before `inWorld` is set.

### The whole causal chain, finally straight

1. Start on an exit -> a location change fires at frame 0.
2. During the transition the field VM keeps ticking against a draw list whose scene is going away.
3. The `$0E00` page is not `$80` in that window, so the bucket walk reads stale bytes as object indices,
   builds "chains" nobody linked, and `CtAnimDrawPass` hits its non-convergence guard — once per frame,
   15 times, until the new scene's init refills the page.

### What to fix, and what NOT to

The repair belongs at the transition: either the tick is gated while a location change is pending (the
ROM does not run a field frame against a half-torn scene either), or the list is re-initialised at
teardown rather than only at build. **Which of those is canonical has to come from the ROM** — `C0/00BF`'s
behaviour across a location load — and I have not derived it. Do not pick one by taste.

⚠ And do not "fix" it by moving field.c:5291: starting the harness on a door is a legitimate thing for a
test to do, and the engine should survive it.

### Deriving `C0/00BF` across a location load — NOT DONE, and what it needs

The question the fix turns on: **does the ROM run a field frame while a location change is in flight?**
It is answerable exactly the way the overworld question was — `--hookpc` on `C0/00C1` and `C0/00D3`
across the load frame, with `C0/000E` (the loader) or `C0/B204` (the scene-load `$0E00` fill) marking
where the load lands. Zero visits during the transition would settle it: gate the tick. Non-zero would
mean the ROM does tick, and the repair moves to re-initialising the list at teardown.

**I could not run it: I have no oracle state that crosses a load.** Measured rather than assumed —
`party3_field` walked right for 160 frames reports ZERO visits to both `C0/000E` and `C0/B204`, so no
location change happens anywhere in it. The other pinned vmdiff states are single-room scenarios.

So the blocking step is a CAPTURE, not an analysis: an oracle state positioned a few frames before a
door crossing (see `.claude/skills/capture-oracle-state`), or a `--btnscript` on an existing state that
reaches one. Once that exists the derivation is two `--hookpc` runs and a control, the same shape that
settled the overworld in one turn.

⚠ Until then the repair stays underived, and both candidate fixes stay candidates. Gating the tick
during a transition is the one that "looks obviously right", which is exactly why it should not be
written before the measurement — the same instinct produced the `!inWorld` gate, which was also
obviously right, also derived, and moved the trip count by zero.

### The capture attempt — not obtained, and here is what was ruled out

Following `capture-oracle-state` section B (drive an existing state with `--btnscript`, save with
`PEEK_SAVE`). The drive has to actually reach a door, and `C0/000E` visits are the test:

| donor | drive | `C0/000E` visits |
|---|---|---|
| `saves/truce_market.state` (loc 0x11) | Down `20:240`, 200 frames | 0 |
| same | Up `10:240` | 0 |
| same | Left `40:300`, 260 frames | 0 |
| same | Right `80:300`, 260 frames | 0 |
| same | compound `20:120,80:120,20:200`, 380 frames | 0 |
| `saves/battle_re/party3_field.state` | Right `80:200`, 160 frames | 0 |

**No single-direction or simple compound drive from the available field states reaches a location load.**
The player either starts too far from an exit or is walled off along those axes; nothing here is evidence
that the doors do not work, only that these drive scripts do not find one.

What the next attempt should do differently, in order of cost:
1. **Look before driving.** `python tools/ct.py objs <state>` prints the player tile, and the scene's
   exit list is in `CT_ASSET_EXITS` — read the exit tiles first and drive AT one, instead of guessing a
   compass direction. Six blind drives cost more than one lookup would have.
2. Or pick a donor whose player already stands near a door (`--teleport <loc> <tx> <ty>` through the
   ROM's own loader puts him wherever you want, which is section A step 2).
3. Or drive `ct_field` instead: it reaches doors routinely (that is what `--worldtest` does), but it is
   the PORT, so it cannot answer a question about what the ROM does — only the oracle can.

### Looking before driving: the player DOES move, and stops 17px short

`tools/ct.py objs saves/truce_market.state` -> player is obj1 at tile (22,44) = pixel (352,704).
Driving the diagonal at the nearest known exit (`--btnscript "A0:150"`, Down|Right) and watching his
position rather than only the loader hook:

```
f13  $1802=16D0  $1882=2DCF     (365, 733)
f16  $1802=1700  $1882=2DFF     (368, 735)   <- and then it STOPS changing
```

So the drive is real — he walks — and he halts at **(368,735)**. Scene 5's world exit is at (368,752),
17 px further down. Two readings fit and one lookup separates them: he is walled just short of the door,
or `truce_market.state` is not scene 5 at all and I was aiming at the wrong exit list.

⚠ **That ambiguity is my own doing.** `--datfield ct_assets.dat 17` printed *scene 5* — the flag took
the dat path as its only argument and fell back to a default scene — and I used its exit list anyway
instead of stopping to get scene 17's. The state's `loc` is 0x11 = 17; whether its SCENE is 17 is exactly
what I never confirmed.

**Next, and it is two lookups before any more driving:** resolve the state's scene number from its loc
(the `--vmdiff` banner prints `loc=... scene=...` for any state, which is the cheapest way), then dump
THAT scene's exits and drive at one of them. The movement half is already proven to work.

### The lookup, done properly — and the drive now lands ON the exit without firing it

Resolving the scene first, as it should have been done three attempts ago:

```
ct.exe --vmdiff saves/truce_market.state --frames 1
  -> vmdiff: loc=011 scene=17 script=330, 12 objects
ct.exe --datfield ct_assets.dat --scene 17
  -> scene 17 (map 60): 1 actors, 1 exits, collision 64x64
     exit at (336,736) -> world 0
```

**Scene 17 has exactly ONE exit, at (336,736)** — and the previous attempt drove Down|RIGHT, away from
it, which is why it stalled at (368,735): right Y, 32 px past the door. (The earlier "scene 5" exit list
was the wrong scene entirely; `--datfield` takes only the dat path and had defaulted.)

Driving Down|LEFT (`--btnscript "60:150"`) instead:

```
f16  $1802=1500  $1882=2DFF  $0100=1100     -> (336, 735), loc still 0x11
```

He reaches **the exit's exact X and stops one pixel above its Y**, and `C0/000E` is still visited zero
times. So the capture is one pixel away and the remaining question is the trigger condition, not the
navigation: the exit almost certainly fires on the FOOT tile rather than the position `$1800/$1880`
holds, and the walk is being stopped by collision at y=735.

**Next:** either drive with a longer Down-only tail once he is at x=336 (the diagonal may be losing the
last pixel to the wall feeler), or read the exit's own trigger test rather than guessing — `loc[9]/[10]`
is documented in `ct-field-changelocation` as the mirror of the PC's tile that positional triggers
compare against, which says the comparison is on the TILE, so the foot needs to reach tile row 46.

### `loc[9]/[10]` read — and it is the WRONG mechanism for this door

From `field_op_e0_fadechange.md` section 5, which already had it derived:

> `loc[9]`/`loc[10]` (`$7F0212`/`$7F0214`) are NOT script state: obj0's autorun continuation runs
> `22 00 09 0A` @body 0x0028 = GetPcCoords(PC0) -> `loc[9]=tileX`, `loc[10]=tileY` EVERY visit — a live
> mirror of the lead's tile. Scene scripts use them as positional triggers (obj15 checks `loc[9]==42`;
> obj17 checks `loc[10]==11`).

So `loc[9]/[10]` is a **script-side** positional trigger: an object's own script compares the mirrored
tile and then issues a ChangeLocation op. That is how the scene-120 cutscene fires its `E0` fade-change.

**Scene 17's door is not that.** Its exit came from `CT_ASSET_EXITS` — `exit at (336,736) -> world 0` —
which is the engine's own data-driven exit table, a different mechanism entirely. Chasing `loc[9]/[10]`
for it would have been reading the right dossier for the wrong door, and the plan I wrote last turn
("the comparison is on the tile, so the foot needs to reach tile row 46") assumed the two were the same
mechanism without checking.

What the geometry does say, and it is consistent either way: (336,736) is tile (21,46); the drive stalls
with the foot (`$1880`) at 735 = tile row 45, one row short, held by collision.

**So the open question is the ASSET-EXIT trigger, not the script one:** what test the engine applies to
the exit table — which is `field.c`'s own exit handling on the port side and a ChangeLocation entry
(`C0/3B1B`, ops 0xDD-0xE1, `ct-field-changelocation`) on the ROM side. Read that before driving again.

## ✅ THE PRE-DOOR CAPTURE, AND `C0/00BF` ACROSS THE LOAD

`saves/predoor_s17.state`, captured with

```bash
PEEK_SAVE=saves/predoor_s17.state build/ct.exe --peek saves/truce_market.state out.raw   --btnscript "60:24,20:16"
```

It sits 7 frames before scene 17's only exit fires. `--hookpc 00C1` across it:

```
f0..f3   visits=1    the field frame runs normally
f4       visits=0    end=C0/80FC
f5       visits=2    end=C0/59FD          $6D: 16 16
f6       visits=1
f7       visits=0    end=C2/1DC2          <- $0100 flips 0x0011 -> 0x01F0 HERE
f8..f13  visits=0    end=C2/..., C3/...
```

**From the load frame on, the field frame never runs again** — every frame ends in bank `$C2`/`$C3`,
the loader and the overworld. So the ROM's answer is the same as for the overworld itself: a location
change stops `C0/00BF`, and the repair is a GATE.

### f4 is the known STRADDLE, not a second stop

`C0/80FC` is `STA $D1` inside the `$01CD`/`$01CF`/`$01D1` pointer setup — a graphics copy routine, which
a door transition runs and which outlasts a frame. And the arithmetic settles it: **f4 (0 visits) + f5
(2 visits) = 2**, exactly two field bodies across two harness frames. Nothing was skipped; the boundary
fell mid-body. That is the class this dossier already documents ("ONE FIELD BODY CAN STRADDLE THE
HARNESS FRAME BOUNDARY"), and `$6D: 16 16` on f5 is its signature.

So f4 is not evidence against the gate reading. The only real stop is f7 onward.

⚠ **Scope of this derivation: it is a field->WORLD exit.** Scene 17 has exactly one exit and it goes to
world 0, so what is derived is the world case. A field->field door needs its own capture before the gate
is written for that path.

⚠ **And `C0/000E` never fires on this crossing** — a world exit does not go through the field location
loader. That invalidates the reachability test used in the six earlier drives: they were graded on a hook
that cannot fire for the exit they were aiming at, so "0 visits" never meant "no door was reached".

## ✅ THE GATE CONDITION, DERIVED — and it is NOT "a change is pending"

`--monitor` on the pending-change latch across `saves/predoor_f2f_s5.state`, beside the `--hookpc 00C1`
visit counts from the same drive:

```
        $0117   $0119   $0100     field frames (00C1)
f1..f6   0080    05..00  0500      visits = 1 each
f7       0000    00      01B7      visits = 1     <- the COMMIT
f8..f17  0000    00      01B7      visits = 0
```

`$0117` bit7 is **already set** at the captured state and stays set through f6, counting `$0119` down
5,4,3,2,1,0 — the op-0xE0 fade machinery. It clears at f7, the same frame `$0100` flips to 0x01B7.

**And the field frame runs the whole time it is set.** f1 through f7 are all `visits = 1`. The ROM does
NOT freeze the field VM while a change is pending; it freezes it only AFTER the commit, from f8.

⛔ So gating on `$0117` bit7 — the obvious reading of "a location change is pending", and the one the
previous entry proposed — **would be wrong**: it would freeze the VM for the whole fade, which the ROM
spends running field frames.

### The condition

**Tick the field VM until the change COMMITS; stop from the commit until the new scene is up.** The
commit is the instant `$0100` is rewritten and `$0117` bit7 clears — which in the port is exactly where
`ct_vm.c`'s op-0xE0 drain raises `sc->locChange` (it rewrites `$0100`-`$0104` from the staged trio and
sets the flag in the same block). So `sc->locChange` is the signal, and the window to suppress is from it
being raised until `fieldLoad` has built the replacement scene.

That also predicts the observed damage precisely: the port currently keeps ticking across exactly that
window, which is when the old scene's `$0E00` page is no longer valid and the new one's has not been
filled — the 15 frames, and the self-heal when `CtObjSceneInit` runs.

⚠ Still to verify before writing it: that `sc->locChange` is raised on the ASSET-EXIT path too, not only
on op-0xE0. The two states captured here both went through a fade (`$0119` counting down), so the
asset-exit path is derived only as far as "the ROM stops after the commit" — whether the port's flag
tracks it is a port-side question, and one `grep` plus one run answers it.

## ⛔ `pendDest` MEASURED — and it retires the GATE conclusion entirely

Per-frame across the corrupting window (`CT_STARTAT=176,216`, plain scene 1):

```
[gate] f0  pendDest=0  pendKind=2  fade=1  tick=1  runaway+0
[gate] f1  pendDest=0  pendKind=2  fade=1  tick=1  runaway+1
 ... identical ...
[gate] f14 pendDest=0  pendKind=2  fade=1  tick=1  runaway+1
```

**`pendDest` is 0 for all fifteen frames** — never armed. So gating on it would fire on none of them:
the FOURTH correct-looking, completely inert candidate, after `!inWorld`, `$0117` bit7 and
`sc->locChange`.

What the window actually is: `fade = 1`, `pendKind = 2`. **The corruption happens DURING THE FADE**, and
`pendDest` is only filled at the commit, when the fade completes.

### And that breaks the gate reading, not just this candidate

The ROM derivation two entries up measured that **the ROM runs field frames all through the fade**
(`$0117` bit7 set, `$0119` counting 5..0, `visits = 1` on every one) and stops only after the commit.
The port corrupts in exactly that window — the fade — where the ROM is happily ticking.

So the port is **not** ticking when the ROM would not. It is doing what the ROM does, and its LIST is
invalid where the ROM's is not. **Freezing the tick would therefore be a deviation from the ROM, not a
fix**, and every gate candidate chased for the last several passes was aimed at the wrong half of the
problem.

The question that remains is the one the gate hypothesis displaced: **why is the `$0E00` page invalid
during a fade in the port when it is valid in the ROM?** The scene is entered mid-fade in this repro
(the player starts on the door, so the transition is already running at frame 0), which makes the
suspect the scene-entry ORDER under a fade — what `fieldLoad`/`CtSceneCreateIn` do, and in what order,
when a transition is already in flight — not whether the VM ticks.

⚠ Four inert candidates in a row is a pattern, not bad luck: each was derived from a true statement
about the ROM and then applied to a window nobody had measured. **Measure the window first, then find a
signal that is true in it.** Every one of these would have died to the one command run above.

## ⭐⭐ THE CHAIN CLOSES: `worldInit` runs at the START of the fade, not the end

`CT_WATCH=0E7E` on the MINIMAL repro (`CT_STARTAT=176,216`, plain scene 1, no worldtest):

```
00 -> 80  CtObjSceneInit   <- the scene is built, page filled
80 -> 00  worldInit        field.c:236   <- and nothing writes it again
```

Two writes, and the second is the last. So the page is zero for every one of the 15 corrupting frames,
and `worldInit` must run at or before f0.

**It does, and field.c's own comment at the exit handler says so:**

```c
if (!p[8]) {                          // -> the OVERWORLD: load + validate the world data
  int destWorld = p[9] | (p[10] << 8);//    NOW, then fade OUT and switch at black
  if (!wd.have || wd.world != (uint8_t)destWorld)
    haveWorld = worldInit(&assets, &wd, destWorld);      // field.c:7071
```

Standing on the door fires the exit at frame 0; the handler eagerly loads the world **before** the fade;
`worldInit` memcpy's the 64 object blocks over `$7E:0B30..$1B2F`, which contains `$0E00..$0EFF`; and the
field VM then ticks 15 frames of fade against a wiped draw list until the world takes over.

⚠ **My earlier retraction was itself wrong.** Two entries back I wrote that `worldInit` "is NOT the
cause" because its call sites are world transitions that "run after f14". That ordering was inferred
from where the `worldtest: on the OVERWORLD` message appeared in the log, not measured — and the message
prints when the world becomes ACTIVE, which is the end of the fade, not when `worldInit` was called. The
original identification was right; the retraction was an inference dressed as a measurement.

### The defect, stated

**The port loads the world eagerly, at the start of the fade, into RAM the field engine is still using.**
The ROM has the same `$0B30` overlap and no problem with it because it does not populate the world's
object blocks while a field scene is still running. So the fix belongs in the exit handler — defer the
`worldInit` payload (or at least the `OWOBJ_BASE` memcpy) until the field is actually gone — and NOT in
the VM tick, which is what four inert gate candidates were aimed at.

⚠ What is still not derived: WHERE the ROM does its equivalent load. "Not during the fade" is what the
overlap implies, but the frame-accurate answer wants a `--hookpc` on the world loader across
`saves/predoor_s17.state`, which now exists precisely for that.

## The deferral WORKS on the draw list and BREAKS the overworld music — reverted

Tried: in `worldInit`, send the base seed straight to `w->objSnap` instead of through WRAM, leaving
`worldEnterAt`'s existing `memcpy(w->wram + OWOBJ_BASE, w->objSnap, 0x1000)` to populate the blocks at
the moment the field is actually gone.

**On the draw list it is exactly right:**

```
worldtest: draw-pass runaway trips: 0        (was 15)
worldtest: PASS
```

**And it costs the overworld its music:**

```
owsong: NO class-$2F94 music object in this world's blocks
```

The reason is the same overlap read from the other side: **the music object lives at `$0EB0`**, which is
inside `$0B30..$1B2F` — it IS one of the 64 object blocks. So some consumer reads the blocks out of WRAM
on a path that does not go through `worldEnterAt`, and the eager write in `worldInit` was feeding it.

Reverted rather than left red. The suite is back to 181/0/0 with the 15 trips pinned as before.

### What the next attempt needs

The direction is confirmed — 15 -> 0 is not a coincidence and the eager write is the cause. What is
missing is the OTHER consumer: find who reads `OWOBJ_BASE` from WRAM without `worldEnterAt` having run
(`ow-song-zone-switch` is the check that catches it, and `--sweepowregion`/`--sweepowmusic` are its
sweeps), and either route it through `objSnap` too or have it force the restore. Then the deferral can
land whole.

⚠ Worth noting against my own last message: I called this "one line" and "does not touch anything
derived from the ROM". The first was true and the second was not — `$0B30` holds the overworld's live
object state, and moving WHEN it is populated changes who can see it. A one-line change with a
two-subsystem blast radius is still a two-subsystem change.

## ⭐ THE OTHER CONSUMER: `worldEnterSong`, and it is a DIAGNOSTIC ordering bug

`worldEnterSong` (field.c:345) runs BEFORE `worldEnterAt` (373, whose restore is at 384) and scans the
blocks straight out of WRAM:

```c
if (getenv("CT_OWSONG")) {                       // field.c:359-366
  for (int b = OWOBJ_BASE; b < OWOBJ_END; b += OWOBJ_STRIDE)
    if ((w->wram[b] | (w->wram[b + 1] << 8)) == 0x2F94) { found = b; break; }
  if (found < 0) printf("owsong: NO class-$2F94 music object in this world's blocks
");
```

So with the deferral the blocks are not in WRAM yet when this runs, and it prints the NO-object line
that `ow-song-zone-switch` fails on.

**And the whole scan is inside `if (getenv("CT_OWSONG"))`** — it is a diagnostic, not engine logic. The
`worldEnterSong` code that actually chooses the song works off the region grid, not off the object
blocks. So the deferral very likely does NOT break the engine at all; it breaks the PROBE, and the probe
was only ever correct because `worldInit`'s eager write happened to have run.

### So the landing is smaller than it looked

Two candidate fixes, and the second is better:
1. Have the `CT_OWSONG` scan read `w->objSnap` instead of `w->wram` — the authoritative seed, valid
   whenever it is called.
2. Or restore the blocks BEFORE `worldEnterSong` runs, i.e. order the entry as
   restore -> song -> place, which is also what "the blocks are live state of the world" implies.

⚠ Which of those is right is not a taste call: the ROM's own order at a world entry decides it, and
`saves/predoor_s17.state` reaches a world entry, so `--hookpc` on the song-select vs the block load
answers it. **That measurement has not been done**, and after four inert candidates in this same
investigation I am not going to write the ordering from what looks sensible.

What IS established: the deferral fixes the draw list (15 -> 0), the only thing it broke is reached
through one `getenv`-gated diagnostic, and the engine path for song selection does not read those blocks.

---

# ⭐ DEFECT A CLOSED: 15 → 0, and the deciding fact was a MEASUREMENT

The measurement the section above refused to guess at got made. It answers the ordering question, and
it does not split the difference between the two candidates — **it refutes candidate 2 outright.**

## 1. The instrument

`--censusat` now prints each write's index in the GLOBAL ordered write log, not just its index among
that address's own hits (`src/main.c`, `runLoadCensus`). Two `--loadcensus` runs of the same state +
same script drive the machine identically and so produce the same log, which makes two histories
directly comparable on `seq`. Without it each history restarts at `[0]` and the single question a write
ORDER can settle — which came first — is exactly the one it cannot.

## 2. Where the music object lives

```
ct.exe --read "wram:7E0B30+1024" --from saves/ow_w0.state     ; class word per 0x40 block
  $0C30 C2/0F63   $0C70 C2/0F63   $0CB0 C2/0F63   $0CF0 C2/75C3   $0D30 C2/0F63
  $0D70 C2/3404   $0DB0 C2/3AE2   $0DF0 C2/3AE2   $0E30 C2/309E   $0E70 C2/3264
  $0EB0 C2/2F94  <-- THE MUSIC-ZONE OBJECT           $0EF0 C2/5628
```

`$0EB0` sits inside `$0E00..$0EFF`. That is the collision, stated as an address: the overworld's music
object and the field's draw list are the same bytes.

## 3. WHEN the ROM writes those blocks — f43 / f44 / f87

```
ct.exe --monitor "7E0100:2,7E0B30:2,7E0EB0:2" --from saves/truce_market.state \
       --btnscript "0:10,20:60,0:200" --frames 110 --settle 10 --diff --writer

  f0    7E0100=1100*  7E0B30=0000*  7E0EB0=8080*
  f43   7E0100=F001*(C0:0CB8)                     the location word flips 011 -> 1F0
  f44   7E0B30=0000(C2:047E)  7E0EB0=0000*(C2:047E)
  f87   7E0EB0=942F*(C2:04AA)
```

The two writers, disassembled (`dis.py 20471 0x1A 0 0` / `dis.py 2048A 0x45 0 0` — **both need 16-bit
M/X or the linear decoder desyncs into garbage**):

```
C2/0471  THE CLEAR                    C2/048A  THE ALLOCATOR
  0471: LDX #0B30                       048A: STX $08          ; class word = handler address
  0474: STX $4E                         048E: LDX #0C30        ; 60 general slots
  0476: LDY #0040     ; 64 blocks       0491: LDY #003C
  047B: STZ $0000,X   ; zero the CLASS  0494: LDA $0000,X / BEQ ->04A5   ; class==0 -> free
  047E: TXA/CLC/ADC #0040/TAX           0499: advance $40, DEY, BNE
  0484: DEY / BNE ->047B                04A5: LDA $08 / STA $0000,X      ; INSTALL  <- logs as 04AA
  0489: RTS                             04AA: STZ $0003,X ... MVN #003A bytes of template
```

**The ROM's first touch of `$0B30..$1B2F` is ONE FRAME AFTER the location word flips.** It never writes
that span while the field is alive. `worldInit` did — at the START of the fade, ~15 frames early — and
the span contains `$0E00..$0EFF` whole. One wiped list per fade frame. That is the 15.

## 4. And the ROM sings BEFORE the object exists

Same drive, the ordered log (`--censusat`, `seq` = global log position):

| seq | address | value | writer | what |
|---|---|---|---|---|
| 50991 | `7E:0B30` | 00 | `C2/047E` | the 64 blocks are CLEARED |
| 51019/51020 | `7E:0EB0/0EB1` | 00 00 | `C2/047E` | ...including the music object's slot |
| 51179 | `7E:02AE` | 1E | `C2/2C57` | `C2/2C50`'s head: the world shadow seeded from the field's |
| **270471** | `7E:1E01` | 1E | `C2/2F2B` | **the SONG id** |
| **270472** | `7E:02AE` | 1E | `C2/2F2E` | the shadow commits |
| **270473** | `7E:1E00` | 10 | `C2/2F3F` | cmd $10 = PLAY, then `JSL C7/0004` |
| 270508 | `7E:1E00` | 00 | `C7/0196` | the Akao driver consumes the mailbox |
| **282259** | `7E:0EB0` | 94 | `C2/04AA` | **the music object is ALLOCATED** (f87) |

The song is issued **~11,800 logged writes BEFORE** the object that later switches it exists. At the
instant `C2/2F0F` runs, `$0EB0` reads `00 00`.

⚠ `C2/2F0F` has **no caller anywhere in bank $C2** — no `JSR $2F0F`, no `JMP $2F0F`, not one occurrence
of the bare word `0F 2F` in all 64 KB, and no `JSL/JML C2/2F0F` in the whole ROM. Reading the caller
chain was never going to answer this; the write log did, in one run.

## 5. What that settles

- **Candidate 2 — "restore the blocks, then sing, then place" — is REFUTED.** The ROM installs the
  music object AFTER the song, not before. Reordering the entry to satisfy the probe would have
  written in the one ordering the ROM rules out.
- **Candidate 1 is right**, and it is smaller than a reorder: the `CT_OWSONG` scan reads `w->objSnap`,
  the seed, which is valid whenever it is asked — and is *deterministic on a RE-entry*, where WRAM
  still holds the previous visit's mutated blocks. The old probe was order-dependent as well as wrong.
- The deferral itself **moves the port TOWARD the ROM's order**, not away from it. `worldInit`'s eager
  copy was the divergence; `worldEnterAt`'s restore is where the ROM writes them.

## 6. Landed

- `worldInit` (field.c) sends the object seed to `w->objSnap` and no longer writes `$0B30`.
- `worldEnterSong`'s `CT_OWSONG` probe and `worldInit`'s `WORLD_DBG` object count both read `objSnap`.
- `field-worldtest-roundtrip` re-pinned **`draw-pass runaway trips: 15` → `0`** — the one ratchet move
  in the right direction this whole investigation earned. `ow-song-zone-switch` goes GREEN again with
  its four pinned lines byte-identical.
- The `!inWorld` comment no longer claims to be the fix. It is not, and it stays as independently
  derived ROM fidelity — with the fact that **removing it turns no check red** written down rather than
  quietly left as coverage.

**Two mutations, both killed:**
- restore the eager `memcpy(w->wram + OWOBJ_BASE, p, 0x1000)` in `worldInit` → **15 trips again**.
- drop `worldEnterAt`'s `memcpy(w->wram + OWOBJ_BASE, w->objSnap, 0x1000)` (the blocks then never
  reach WRAM at all) → `ow-song-zone-switch` **RED**, quick tier, `156 pass / 1 fail`. So the blocks
  ARRIVING is graded too, by the check that needs the music object live to run its switch cycle.

## 7. And a tooling defect found on the way

`regress.py --pin` wrote the checks file with `json.dump(..., indent=1)` and default `ensure_ascii`,
while the file is `indent=2` UTF-8. Every single-line re-pin therefore rewrote all 4902 lines and
escaped 40 `⚠` into `\u26a0` — the actual change invisible inside a whole-file diff. Fixed to
`indent=2, ensure_ascii=False`; the re-pin above shows as a 2-line diff.

## 8. The four inert candidates, kept on the record

Each was a TRUE statement about the ROM applied to a window nobody had measured, and each cost a pass:
the `!inWorld` gate (15 with, 15 without — byte-identical), the `$0117` bit7 latch, `sc->locChange`,
and `pendDest` (measured 0 for all 15 frames). The fifth candidate is the only one that began with a
measurement instead of ending with one. **The lesson is the ORDER of the two steps, not the number of
them.**

---

# ⭐ THE DRAW COMPARE — it exists now, and it found a CONFLATED FIELD

"A draw compare still does not exist" was the last open item in the README: `draw` is a C struct field,
so a port that shows an object the ROM hides was invisible to every compare we had. The scheduler diff
grades `$1C00`/`$1180`/position; the write-mask compare only owns bytes we WRITE.

## 1. The ROM's own draw predicate

```
C0/B0E6  LDA $7F2000 / ASL A / STA $BD / LDX #00        ; nobj*2
C0/B0F1  STX $6D
         LDA $1100,X / BMI ->B11A                        ; class bit7 = an unloaded slot -> skip
         CMP #07 / BEQ ->B11A                            ; class 7 -> skip
C0/B0FC  JSR C0/B127 / BCC ->B11A                        ; CARRY CLEAR = not drawn
C0/B101  JSR C0/AB45 / JSR C0/A9CD                       ; screen pos, then LINK into the draw list
C0/B127  LDA $1A81,X / BEQ ->B190 / BMI ->B190           ; hidden / removed
         ...camera-window test...        B190: CLC / RTS
```

The same head gates the drain visit (`C0/CAE2`, `C0/CB12`) and the activation pass (`C0/A894`).
Encoding, from the three ops that write it — all three falling into the ONE tail `C0/4CD9`:

| op | ROM | writes |
|---|---|---|
| `0x90` SHOW | `C0/4CD5 LDX $6D / LDA #01` | `$1A81,X = 01` |
| `0x91` HIDE | `C0/4CE0 LDX $6D / LDA #00` | `$1A81,X = 00` |
| `0x7E` REMOVE | `C0/4CE6 LDX $6D / LDA #80` | `$1A81,X = 80` |

`VMDIFF_DRAW=1` compares the ENCODED C field against `$1A81,X`. Deliberately the field and not our own
mirrored byte: the byte is already inside the write-mask compare, and what was ungraded is whether the
FIELD agrees with the ROM.

## 2. ⚠ The guard is the ROM's, and without it the compare reports LEFTOVERS

First run, before the guard: six bedslide objects reported `ours=0000 oracle=0001` — obj2..obj7, all
with `$1100 = 80` and `$0F00 = 00`. **Objects that do not exist in that scene.** `$1A81` is
process-lifetime: no location load clears the page (the four clears are `$7E:1D00`, `$7F:0200`,
`$7E:1900`, `$7F:0580` — `wram_load_census.md`), so an empty slot still holds the PREVIOUS scene's
value, and `C0/B0F3`'s class test is what stops the ROM ever reading it. Transcribing that test is what
makes the compare a compare.

## 3. WHAT IT FOUND: `draw` was written as `$1A81` and read as `$0F00`

Two different ROM quantities in one C field. `$1A81` is script visibility; `$0F00` is what `C0/A810`
derives from `$1A81` **and** the camera window. The ops wrote `draw` with the first meaning; the seed
and the consumers read it with the second. Every loaded object merely OFF-CAMERA at a seed therefore
came back marked hidden:

| state | objects reporting `ours=0000 oracle=0001` at frame 0 |
|---|---|
| party3_field | 15 |
| S5_mvfix | 17 |
| forest_holdleft_pre | 9 |
| im_so_sorry | 6 |
| DEMO8_mvfix | 1 |

The sharpest instance was already labelled and nobody had read the label: `npcSolidTile`'s line said
`// ROM C0/5BC3: skip $0F00 low==0` while the code tested `o->draw != 0`. `C0/5BC3` is literally
`LDA $0F00,X / BNE ->5BD2`.

## 4. The split, applied to all four sites

| site | ROM anchor | now reads |
|---|---|---|
| the `--vmdiff` seed | the ops at `C0/4CD9` | `draw` from `$1A81` |
| `npcSolidTile` | `C0/5BC3 LDA $0F00,X / BNE` | `$0F00` |
| `CtSceneActor` `*placed` | `C0/B0FC JSR B127 / BCC` | `$0F00` |
| `CtSceneActorHidden` | "did a script explicitly hide it" | `draw` (= `$1A81`) |

All six pinned `--vmdiff` states then grade **clean at seed**; the three that still diverge do so at
their pre-existing pinned `ip` frames (bedslide f85, DEMO8 f108, S5 f113), untouched.

## 5. ⚠ A SURVIVING MUTATION, and it was a defect in the INSTRUMENT

Dropping the class guard left both states clean AND left the coverage line reading `25 of 36` — the
mutation survived. Cause: `CtSceneDrawGraded` re-derived the guard in its own loop. **Two copies of one
rule cannot grade each other.** The count is now incremented by the compare itself, inside the one `if`
that holds the guard, so deleting it reads `36 of 36` and the pinned line goes red.

(It survived for a second reason worth recording: once `draw` is seeded from `$1A81`, an empty slot's
leftover agrees with our copy of the same leftover, so no *divergence* appears even ungraded. The
coverage count is the only thing that can see the guard at all.)

## 6. Two re-pins, `9 NPCs` -> `8 NPCs`, with `fnv32` UNCHANGED

`render-scene5-snapshot` and `camera-reenter-scene5`. A throwaway per-object probe on that scene showed
**twelve** objects flipping `placed` (obj8-13, 18-20, 25, 26) — every one off-camera with `$0F00 = 00`
and `$1A81 = 01`, every one already culled by the renderer. `837BCC29` and `571C8700` are identical
before and after. The diagnostic got tighter; no pixel moved. **Re-pinning a count while the image hash
holds is the only shape of count re-pin worth accepting.**

## 7. Graded

`vmdiff-draw-visibility` (quick): seed clean + `CLEAN: no scheduler divergence over 130 frames` +
`25 of 36 objects graded`. Mutations: seed from `$0F00` again -> 15 divergences; class guard dropped ->
`36 of 36`; `*placed` back to `draw` -> `field-battle-forest-return` and `field-battle-bat9-return` red
(`placed 2 -> 15`, `placed_enemies 0 -> 8`) — that one was hit for real before the change was made.

⚠ **`npcSolidTile` reverted to `draw` SURVIVES the suite.** Off-camera objects become solid again and
nothing measures it. The line is right — `C0/5BC3` is unambiguous — but it is **derived, not graded**,
and that is stated here rather than left as implied coverage. A scenario that walks a mover into the
tile of an off-camera solid NPC is what would close it.

## 8. What the compare still does NOT cover

`$1A81` is script visibility, not pixels. This grades the FIELD against the ROM's byte; it does not
compare an emitted sprite against the oracle's OAM. The camera-window half now rides on `$0F00`, which
`VMDIFF_ACT` grades separately. A true OAM-level draw compare remains unbuilt.

---

# ⭐ CLOSING `npcSolidTile`: the scenario existed, and porting the ROM correctly destroyed it

The previous section left one line derived but ungraded — `npcSolidTile`'s `$0F00` test, whose revert
survived the full suite. This is how it got covered, and the route matters more than the destination.

## 1. The scenario DID exist — three scenes out of 512

A divergence counter went inside `npcSolidTile`: every candidate standing on the mover's destination
tile, and how many of those the ROM's `$0F00` test and the retired `draw` test decide **differently**
(the only way to disagree is an object shown but not activated — off-camera). Swept over all 512 field
scenes at 200 frames each:

```
scene 285  cand=5   diverge=5
scene 294  cand=3   diverge=3
scene 298  cand=25  diverge=25      <- every candidate diverges
```

And it was observable: an actor-position hash over all objects moved on all three
(`298: 4917B487` correct vs `4FFCD1A5` retired). Three scenes in 512 is exactly why nothing measured it.

## 2. Then reading `C0/5B95` properly killed the scenario

```
5B95: STZ $EA / LDX $6D
5B99: LDA $0F00,X / BNE ->5BA0      the MOVER's OWN activation latch
5B9E: CLC / RTS                     not activated -> NO BLOCK AT ALL
5BA0: LDA $1C80,X / BIT #02 / BEQ   the object-collision bit -- the port had ONLY this one
```

An off-camera mover never consults the object plane in the ROM. Porting `5B99` left all three
actor-position hashes **identical** (the two gates agree on that data) — and took `diverge` to **0 in
all 512 scenes**, with `calls` to 0 in two of the three.

**The faithful port makes the line unreachable by real data.** Keeping the port less faithful to
preserve a test would be backwards, so the coverage moved to where it can exist. This is the
`--sweepowregion` situation, arrived at from the opposite direction: there the real data was blind from
the start, here porting the ROM *made* it blind.

## 3. `--sweepsolid`

Micro-executes `C0/5B95` and grades six gates against the ROM's own carry:

| ROM | gate |
|---|---|
| `5B99` | mover `$0F00` |
| `5BA0` | mover `$1C80` bit1 |
| `5BC3` | candidate `$0F00` ← the long-ungraded line |
| `5BD5` | candidate `$1100` bit7 |
| `5BD7` | candidate == mover |
| `5BDB` | candidate `$1B01` bit0 |

`768/768 exact`. The port side is `CtVmSolidMoverGate`/`CtVmSolidCandGate` — **the same functions
`npcSolidTile` calls**, not a copy, because a sweep that grades a private copy grades the copy. That is
not a hypothetical: it is exactly how the draw compare's class guard survived its own mutation one
commit earlier. Geometry is held constant and passing so a gate verdict is never a geometry verdict.

**Anti-blindness is asserted in the output, both halves pinned:** `$1A81` is seeded as the exact
INVERSE of `$0F00` on 192 seeds, so a test reading the script-visibility byte flips on every one of
them; and the BLOCK/PASS split (16/752) is printed, so a sweep producing only one verdict cannot read
clean.

Mutations, all six killed: cand `$0F00`→`$1A81` **736/768**; mover `$0F00` dropped 752/768; cand
`$1100` bit7 dropped 760/768; cand==mover dropped 672/768; cand `$1B01` bit0 dropped 752/768; mover
`$1C80` bit1 dropped 752/768.

⚠ **The self-test mutation SURVIVED the first attempt** — the mover's poisoned `$1B01 = 0` rejected it
before `5BD7` could. The seed now marks the mover solid so only `5BD7` can reject it. A sweep is not a
sweep until each gate is the *only* thing that can produce its verdict.

## 4. Three divergences found on the way, named rather than rediscovered later

1. **The geometry.** `C0/5BFA / 5C20 / 5C47 / 5C6D` (selected by `$1600,X`, the FACING byte) compare
   `$1800/$1880` against `#0100` = 16.0 px in 12.4 — a directional, half-open 16 px box. The port
   compares the destination TILE. They agree on tile-aligned actors and differ off-grid.
2. **The scan range.** `5BBD LDA $7F2000 / ASL A / TAX` starts **one past** the last object, and
   `5BC8 DEX / DEX / BNE` exits at 0 — so the ROM probes index N and **never tests index 0**, while
   the port walks N-1..0.
3. **The carry.** Those `SBC`s run on the carry left by `5BD7 CPX $6D`, so the effective distance
   depends on whether the candidate's index sits above or below the mover's. Not something a hand-port
   would ever guess, and not ported.

`npcSolidTile` also now scans **downward** (`5BC8`) and takes its gates from the ROM's bytes
(`$1100`/`$1B01`) instead of the actor struct.

---

# ⭐ THE 16px BOX, PORTED — and its three asymmetries are the whole content

The previous section closed `npcSolidTile`'s gates and left the geometry named-but-unported. This ports
it. `C0/5BE6 JSR ($5BF2,X)` picks one of four routines by `$1600,X * 2` — the FACING byte, and
`C0/5290 STA $1600,X` writes `F700[angle]` **immediately before** the `JSR C0/5B95`, so the port's own
angle→facing lookup is the same value (checked, not assumed):

```
5281: JSR C0/ABA2 / LDA $EE / TAX / LDA $F700,X / LDX $6D / STA $1600,X
5293: JSR C0/5B95 / BCC ->52A8          ; blocked -> 5298 LDA $EE / ADC #30 / re-derive the facing
```

## The four arms

```
5BFA up     LDA $1880,X / SBC $DD / BPL rej / EOR #FFFF / CMP #0100 / BCS rej   then the X axis
5C20 down   LDA $1880,X / SBC $DD / BMI rej /             CMP #0100 / BCS rej   then the X axis
5C47 left   LDA $1800,X / SBC $DB / BPL rej / EOR #FFFF / CMP #0100 / BCS rej   then the Y axis
5C6D right  LDA $1800,X / SBC $DB / BMI rej /             CMP #0100 / BCS rej   then the Y axis
cross axis  LDA .. / SBC .. / BPL +3 / EOR #FFFF / CMP #0100 / BCS rej
5C43 reject SEP #20 / CLC / RTS          accept: SEP #20 / SEC / RTS
```

`$DB`/`$DD` are the MOVER's `$1800`/`$1880`, latched 16-bit at `5BB1`/`5BB6`. `#0100` = 16.0 px in 12.4.

## Three asymmetries, each with its own killing mutation

| | what the ROM does | why a hand-port loses it | mutation |
|---|---|---|---|
| 1 | the primary axis is **half-open and lopsided**: the negative-required arms reject via `EOR #FFFF`, and `~A = |A|-1`, so they accept `|d| <= 0x100`; the positive-required arms have no EOR and accept `d <= 0x00FF` | "a 16 px box" reads symmetric | symmetric → **32** |
| 2 | the first `SBC` runs on the carry left by **`5BD7 CPX $6D`** — nothing between them touches C — so a candidate with a LOWER index measures one subpixel further away | nobody writes an index-dependent distance on purpose | carry forced → **64** |
| 3 | the second `SBC` always runs with **C = 0**, because the last flag-setter before it is the primary axis's `CMP` with its BCS not taken | the `-1` looks like a typo | `-1` dropped → **64** |

Also killed: cross axis symmetric 32, threshold `0x101` primary 64 and cross 64, heading sign flipped
512, primary/cross axes swapped 284. Control and restore both 1352/1352.

⚠ **BOTH INDEX ORDERS ARE DRIVEN AND THE COUNT IS PINNED (676/676).** Asymmetry 2 is invisible to a
sweep that only ever puts the candidate above the mover — the sweep would read clean and the mutation
would live.

## One deliberate deviation, stated rather than hidden

`npcSolidTile` feeds the box the port's **live** `sc->fx/fy` scaled to 12.4, not `$1800/$1880`. In the
ROM those ARE the live values — it writes them as it moves — while the port commits the mirror once per
frame AFTER the movement loop, so reading WRAM there would hand the box **last frame's** positions.
That commit phase is a separate mechanism; the box is exact and the phase question is left where it is.

## What landing it moved, and why that is a fix

Exactly one check: `field-battle-ambush-generic-dissolve`'s render canary, `F3265283 -> CD0E9659`. A
temporary probe on that run counts **91 candidate evaluations, 8 decided differently** by the box, and
the frames differ by **745 pixels in one bbox** (x 80..177, y 70..152) — a 1 px sprite shift, not a
changed composition.

The evidence that it is a fix is not the render hash, it is the **oracle-anchored neighbours holding**:
`field-battle-gato-staging-handoff` still reads `rec=08 09 06 00` (the record this dossier calls
byte-identical to the oracle capture), `field-battle-enemy-stage` still reads gato/naga/hetake/diablos
BYTE-EXACT, and `field-battle-bat2-enemy-visible` — the **same** bat2 Beetle/Avian formation as that
fight — still reads `fnv=6BF1ACCB`. The fight itself is identical: `victory f1695 pcAttacks=5
cronoHP=70 marleHP=65`, pinned alongside the shot for exactly this reason.

## Still not ported, still printed by the mode

The **scan range**: `5BBD LDA $7F2000 / ASL A / TAX` starts one PAST the last object and `5BC8`'s
`DEX / DEX / BNE` exits at 0, so the ROM probes index N and never tests index 0, while the port walks
N-1..0. Now the only remaining line of `C0/5B95` that is not the ROM's.

---

# ⭐ THE SCAN RANGE — and now `C0/5B95` has nothing left unported

```
5BBD: LDA $7F2000 / ASL A / TAX      X starts at nobj*2 -- ONE PAST the last object
5BC3: LDA $0F00,X / BNE ->5BD2       the candidate gates
5BC8: DEX / DEX / BNE ->5BC3         ...and the loop EXITS AT ZERO
5BCC: REP #10 / LDY $C7 / CLC / RTS  nothing blocks
```

Two facts the port had backwards at both ends:

- the ROM **probes the slot one past the array** — a real read of whatever the next object's bytes
  hold, not a bounds bug it avoids;
- the ROM **never tests index 0**. In every field scene that is the scene's controller object, so it
  can never block a mover. The port walked `nobj-1..0`.

## Why it needed its own sweep phase

Phases 1 and 2 evaluate **one candidate the caller already picked**, so the loop bounds never enter
their verdict — the range is structurally invisible to both. It was reported as unported for that
reason rather than assumed covered.

**Phase 3 drives the whole scan** with exactly one qualifying blocker walked across every slot from 0
to `nobj` inclusive, over `nobj = 3/4/5`, every mover index, all four headings: **152/152 exact**, with
**36 seeds placing the only blocker at index 0** and **36 at index nobj**. Both counts are pinned,
because those two slots are the entire point.

Mutations, all three killed, and the counts land exactly on the seeds that target each slot:

| mutation | result |
|---|---|
| start at the last object (drop the one-past probe) | 116/152 — exactly the 36 index-`nobj` seeds |
| exit after index 0 (test it) | 116/152 — exactly the 36 index-0 seeds |
| walk upward `0..nobj-1` (**the port's old range**) | 80/152 — both at once |

## One copy, via a position accessor

`npcSolidTile` is now a four-line wrapper over `CtVmSolidScan` — the same function the sweep drives.
The callback is what makes that possible: the sweep passes `NULL` and the scan reads `$1800/$1880`; the
live mover passes its own `sc->fx/fy` scaled to 12.4. **The bounds, the gate chain and the box are one
copy shared by both**, so a mutation of any of them moves the port and the sweep together and the ROM
comparison catches it. That is the lesson from the draw compare's class guard, applied up front instead
of after a surviving mutation.

`C0/5B95` is now ported end to end: mover gates, candidate gates, distance box, scan range.

---

# ⭐ THE COMMIT PHASE — and the regression I had already shipped three commits earlier

The task was the `$1800/$1880` commit phase. It resolved quickly, and then measuring it uncovered
something much worse that three green suite runs had not.

## 1. The commit phase itself: the ROM reads LAST FRAME'S positions

All nine `JSR C0/5B95` sites — `4FBD, 504F, 50FC, 5170, 51F4, 5293, 5329, 53DC, 54C8` — are in the
MOVE-OP bodies, i.e. the event VM at `C0/00C4`. `$1800/$1880` are written only by `C0/A810`'s integrate
arms (`AA3B/AA43/AA4E/AA95/AADA/AB25` + the Y siblings), which run at `C0/00D3`. `C0/ACFD`, the step the
move op *does* issue, writes velocity and the ticket and **never the position**.

So the ROM's object-solid test reads the previous frame's committed positions, uniformly, for every
object. **There is no partial update to model.** The port's phase is the same: `CtSceneTick` runs the
scheduler (where `moveOp → npcMoveAngle → npcSolidTile` live) before the integrate and before the
commit.

⚠ **`803925d`'s "deliberate deviation" note was therefore BACKWARDS** and is retracted. Feeding the box
live `sc->fx/fy` was not protecting against staleness — last frame's values are exactly what the ROM
feeds it — and the float path added a rounding mismatch with no reason to exist: the commit truncates
`(int)(fx * 16.0)` while the accessor used `lround()`, and the box decides on single-subpixel margins.
`npcSolidTile` reads the bytes now.

## 2. The commit TIMING is inert — derived, not asserted

The comment at the commit site claimed this without evidence. Checked against the legs themselves:

| leg | what it reads |
|---|---|
| `C98A` | no `$1800`/`$1880`/`$0A80` operand at all |
| `AB45` | its **own** object's `$1800/$1880`, then writes `$0A80` |
| `A9CD` (A9CD..AA06) | `$0E01,X`, `$0A80,X`, `$0E00,Y`, `$0E80/$0E81` — **no `$1800`** |

The `$1800` operands that a naive byte-scan attributes to `A9CD` sit past its `RTS`: they belong to
`AA07`, the integrate. No leg reads another object's position, so all-integrates-then-all-legs produces
the ROM's leg sequence.

## 3. ⛔ AND THEN: object collision had been DEAD for three commits

Measuring the above meant instrumenting `npcMoveAngle`. It reported **entered 450, gated 450 — 100%**.

`edb6d43` moved the mover gate onto `CtVmSolidMoverGate`, which reads `$1C80` from WRAM as `C0/5BA0`
does. But op `0x0D` wrote only `sc->coll`/`sc->collMap` and **never the byte**. Bit1 was 0 forever, so
`C0/5B95`'s head refused every mover in every freshly loaded scene — through `edb6d43`, `803925d` and
`f84e5fb`, each at **183 pass / 0 fail**.

`--sweepsolid` grades what `C0/5B95` DECIDES over 2272 micro-executed seeds. Nothing graded that the
engine still REACHES it. Same vacuity class this dossier keeps recording, walked into from the inside.

### `$1C80` is storage, like `$1A81` and like `draw`

- `C0/629F` / `C0/62AE` — op `0x0D`/`0x0E` write the WHOLE operand byte
- `C0/42A1 / 4315 / 43C8` — STZ `$1C80` in **all three** party arms; the port zeroed only the lead's
- it is **process-lifetime**, and its cold value is a LEFTOVER the port had already measured (0x03 for
  every generic object in the oracle's scene 8) but kept only in the struct

Fixed by writing it where the ROM writes it, plus a cold-leftover seed **before** the init pass — that
order matters and is the ROM's: the leftover exists, then fn0's `0D`/`0E` overwrite it. Seeding after
the init pass instead clobbers those ops back to the reconstructed two bits, which is what let a
mutation of the init handler survive on the first attempt.

Scene 285 reaches the scan **87** times again — the exact count measured before the regression.

## 4. Two liveness checks, because one scene cannot grade both writers

| check | grades | mutation |
|---|---|---|
| `field-solid-liveness` (scene 285) | the cold leftover — nothing there runs `0D` | drop the seed → `reached 87 → 0` |
| `field-solid-liveness-fair` (scene 5) | op `0x0D` itself — the fair's movers run `0D 01`, bit1 CLEAR | drop the byte write → `reached 0 → 60` |

Scene 5's pinned `reached=0` is not vacuous: `entered=397 gated=397` is on the same line, so the path is
demonstrably exercised and correctly refused 397 times.

⚠ **NOT GRADED:** reverting the party arms from `moveMode <= 2` to lead-only changes nothing on any of
twelve scenes tested (1/2/5/7/8/13/19/130/151/285/298/439). Right by citation (`C0/42A1 / 4315 / 43C8`
all STZ), unmeasured by anything.

## 5. The lesson, stated at the cost of admitting it

A sweep that grades a routine's LOGIC says nothing about whether the engine still calls it. Every
mechanism ported by micro-execution needs a liveness assertion next to it, or it can be switched off
silently by a change three files away — and the suite will keep reading green while it happens.

---

# ⭐ THE PARTY ARMS: ungradable by scenario, and the reason is structural

The `$1C80` fix left one line derived but unmeasured — `C0/42A1 / 4315 / 43C8` all `STZ $1C80,X`, so
all three in-party PC loader arms zero it, while the port had zeroed only the lead's. This is the
attempt to close that with a scenario, and the verdict is that none exists.

## The measurement

Reverting `moveMode <= 2` to lead-only changes **nothing on any of 490 loadable scenes**, swept twice
with a 3-PC party forced via `CT_CARRYPARTY=0,1,3`, the full `solidlive` line diffed scene by scene.

## Why — three closed doors, not a weak sweep

1. **A party alone is not enough.** The arms differ only for slots 1 and 2. The first 12-scene attempt
   ran the default party (Crono alone), so those arms never executed at all.
2. **Followers do not run move ops.** A follower reaches `npcMoveAngle` only if a script runs a MOVE OP
   on that object; in the port they are driven by `CtSceneUpdateFollowers`, the conga line, which never
   goes through `moveOp`. Re-running all 12 scenes at 2-PC and 3-PC changed nothing.
3. **The one op path runs on seeded WRAM.** Op 0xD9 MOVEPARTY does drive followers through move ops —
   `npcMoveAngle` is entered 13 times in the gato staging walk — but `--battletest` loads a 128 KB WRAM
   image, so `$1C80` is the oracle's own byte and the loader arms never execute. Control and mutation
   are identical there by construction.

The scenario that WOULD exercise them is a party member walking under script on a fresh load: scene 7's
party walk-in. **Scene 7 reports `entered=0`** because its obj0 autorun halts at op 0x42 — a separate,
already-recorded open defect (`ct-vm-party-ops`). Closing this waits on that autorun, not on a better
sweep.

The line stays: right by citation, inert in practice, and recorded as unmeasured.

## What the detour produced: the only two scenes that actually BLOCK

Every liveness pin so far reads `deflected=0`, so none of them proves the `+0x30` rotate at `C0/5298`
ever fires. Sweeping the counters over all 512 scenes found exactly two that deflect, both stable
across solo / 2-PC / 3-PC:

```
scene 252   entered=305 gated=258 reached=47 deflected=4
scene 287   entered=200 gated=32  reached=168 deflected=2
```

`field-solid-deflect` pins scene 252's whole chain.

⚠ **Its limits are measured, not assumed.** The deflect count is INSENSITIVE to the mechanism's
details: the box threshold (`0x100` to `0x101`), dropping the second `SBC`'s `-1`, walking the scan
range upward, and swapping the candidate `$0F00` test for `$1A81` all leave 252 and 287 at 4 and 2.
Those belong to `sweepsolid-object-solid-gates` and its 2272 micro-executed seeds. This check guards
the one thing the sweeps structurally cannot: that the engine still calls the routine AND that its
blocking arm still fires.

## Method note, recorded because it cost a false result

A "follower sweep" reported mid-investigation ran on a **stale binary** — the build had failed — and
its filter grepped for the ABSENCE of a column, so a missing field read as a clean zero. Confirm the
build succeeded before believing a sweep's silence.

---

# ⭐ THE BUTTON-POLL OPS 0x3F-0x44 — and scene 7 was never "halted at op 0x42"

The party-arm investigation ended on "closing this waits on scene 7's obj0 autorun, which halts at op
0x42". That note (`ct-vm-party-ops`) turns out to describe a symptom under the wrong opcode, and the
real thing is a whole op family the port had stubbed out.

## 1. What the port had

```c
if (op >= 0x40 && op <= 0x44) return 1;    // park forever
```
with a comment claiming these are WAIT ops and that the masks are `40=Up 41=Y 42=Down 43=Select
44=Start`. **Both halves are wrong.**

## 2. The shape: these are BRANCHES, not waits

```
C0/675C (op 0x42):  INY / LDA $51 / AND #04 / BNE ->6766 / BRL ->66D0
C0/6766 taken:      TRB $51 / BRA ->6784       ; consume the bit, SKIP the operand, continue
C0/66D0 not taken:  TYX / LDA $7F2001,X / IP = X + operand
```

`66D0` is the **same not-taken tail ops 0x34-0x39 use** — the ordinary `target = addr + operand + 1`
encoding. So 0x3F-0x44 are branch-if-not-newly-pressed, differing from 0x34-0x39 only in WHICH byte
they test.

Dispatch table (base `C0/5D6E`, resolved from the `6740/674E/675C/676A/6778` run at `C0/5DEE`):
`3F -> 6732, 40 -> 6740, 41 -> 674E, 42 -> 675C, 43 -> 676A, 44 -> 6778`.

## 3. The masks: the LATCH is fed from the BUTTON byte, not the D-pad

`C0/0097` — the first instruction of the field frame — is `LDA $00F0 / TSB $51`, so `$0151` stickily
accumulates `$00F0` and only these ops' `TRB $51` clears it. Measured on the oracle
(`--monitor 7E00F0:4` across a press edge, all twelve buttons):

| | edge | held |
|---|---|---|
| buttons | `$00F0` | `$00F2` | A=0x80 X=0x40 L=0x20 R=0x10 B=0x08 Y=0x04 Sel=0x02 St=0x01 |
| D-pad | `$00F1` | `$00F3` | Up=0x08 Down=0x04 Left=0x02 Right=0x01 |

So the six masks are **exactly ops 0x34-0x39's, in the same order, on the edge byte**:
`3F=A 40=B 41=X 42=Y 43=L 44=R`. The retired comment had read the D-pad byte's layout into the button
byte. **Op 0x42 waits on Y, not Down.**

## 4. What scene 7 actually does

Traced, not assumed. obj0's autorun is a **polling ring**:

```
[op42] addr=000A operand=10 -> 001B     test Y
[op44] addr=001B operand=10 -> 002C     test R
[op43] addr=002C operand=10 -> 003D     test L
[op41] addr=003D operand=10 -> ...      test X, then back to 000A
```

It was never "halted at op 0x42" — it was parked on the *first* of them because the port never
advanced. With the latch fed it polls, and on a real edge it consumes the bit and leaves the ring for
good. Scene 7 now produces three distinct frames where it produced one:
no press `9C300A1B`, Y `C399A3B5`, X `A509D550`.

`CT_HOLDBTN="mask@from:len"` exists because nothing headless could reach it: the walkscript grammar
covers the d-pad and A, and this ring polls Y/R/L/X.

## 5. ⚠ THE WRITE-MASK COMPARE CAUGHT AN INVENTION OF MINE

The first cut had `CtSceneTick` writing `$00F0-$00F3` from `btnHeld`, **including a D-pad pair derived
from the high byte**. Nobody supplies that: field.c passes only A/B/X, and `--vmdiff` passes
`injBtn & 0xFF` *deliberately* — it forwards the direction to the ORACLE and syncs our lead from it
rather than running our player controller. Ours wrote 00 where the oracle had a live 02 (Left), and
`vmdiff-wmask-party3` / `vmdiff-framelegs-party3` went RED on `$00F1/$00F3` at f2.

The fix is the rule, not a suppression: **a layer claims only the bytes it is given.** The input bytes
belong to the input path (`CtSceneSetButtons`, the port's stand-in for C2/8500), not to the tick, which
only does `LDA $00F0 / TSB $51`.

Re-pinned at owned addresses **235 -> 238** and **239 -> 242** with the divergence counts UNCHANGED
(0 and 23) — three newly maintained bytes at the same zero divergence, the same ratchet shape as the
earlier 90 -> 235 move.

## 6. Graded

`field-op42-poll-y` and `field-op41-poll-x`. Three mutations killed: the park-forever stub and dropping
`TSB $51` both collapse all three button cases to the no-press hash; **swapping ops 0x41/0x42's masks
swaps exactly those two hashes**, which grades the mask ASSIGNMENT rather than its presence.

⚠ **NOT GRADED:** removing the `TRB $51` consume survives — scene 7's ring exits the moment it fires
and never re-tests the bit, so a latch that stays set is indistinguishable there.

---

# ⛔ RETRACTION: the party arms ARE gradable — scenes 226 and 399

The section "THE PARTY ARMS: ungradable by scenario, and the reason is structural" is **wrong** and is
retracted here. They are now graded by `field-solid-party-arms-226` and `-399`.

## What the retracted verdict got right and wrong

It was not wrong about the measurement — the 490-scene control-vs-mutation diff really did show nothing.
It was wrong about **why**, and the why is what made it look permanent:

> that sweep ran **BEFORE the button-poll ops 0x3F-0x44 were ported**.

Every script that leads to a follower move op was still parked on a poll op the port never advanced
past. Fixing the ring changed the reachable set.

The scene-7 story in that verdict was wrong too. I wrote that the blocking scenario was "scene 7's party
walk-in". Measured after the fix: **scene 7 reaches `npcMoveAngle` ZERO times on every button of its
poll ring, out to 1800 frames.** obj0 consumes the button, calls at `$0011`, waits on op 0xAD, and no
move op ever runs. "Scene 7's party walk-in" was an inference I layered on a memory note about Marle
walking 439 -> 7 — where Marle is an NPC (obj3), not a party member — and then stated as derived.

## What actually grades them

The precondition is a **follower (slot 1 or 2) running a scripted MOVE OP on a fresh load**. A
512-scene sweep counting `npcMoveAngle` entries with `$1100` in {1,2} finds **eight** such scenes:

```
80 (20)  226 (123)  227 (5)  267 (3)  288 (2)  311 (2)  344 (46)  399 (5)
```

Of those, exactly **two** decide differently when the arms narrow to lead-only:

| scene | control | arms narrowed to lead-only |
|---|---|---|
| 226 | `entered=277 gated=277 reached=0` | `entered=299 gated=154 **reached=145**` |
| 399 | `entered=5 gated=5 reached=0` | `entered=5 gated=0 **reached=5**` |

The other six are gated by `5B99`'s `$0F00` half first — which is why eight scenes reach the path and
only two can tell the arms apart.

Both pins carry their own party-dependence evidence: scene 226 reads `entered=154` solo (so the 123
extra entries ARE the followers) and scene 399 reads `entered=0` solo (so all five entries are
followers, with no lead traffic mixed in). 2-PC and 3-PC agree on both.

**Two mutations, both killed by both checks:** narrowing the arms to `moveMode == 0`, and letting the
arms write only the struct fields instead of the `$1C80` byte.

## The lesson, which this dossier keeps re-learning

**"No scenario can reach this" is a claim about the CURRENT engine, not about the ROM.** Every unported
mechanism upstream shrinks the set of reachable scenarios, so a negative reached under a
known-incomplete engine expires the moment anything upstream lands. The honest form of that verdict
would have been "not reachable *today*, and here is what would have to be ported first" — which is
exactly what the op-0x42 family turned out to be.

---

# ⭐ THE OAM-LEVEL DRAW COMPARE — the compare was not missing, the COMPOSITION was

The README's long-standing "an OAM-level draw compare against the oracle remains unbuilt" was **stale**.
Every link of the draw chain was already pinned. What was missing was the composition of them, and that
turned out to be a real hole with a real defect behind it.

## 1. The chain, link by link, all already graded

| link | check |
|---|---|
| `$1A81` script visibility | `vmdiff-draw-visibility` |
| `$0F00` activation latch | `vm-activation-pass-0F00` |
| the draw LIST `$0E00/$0E80/$0E81` | `vmdiff-framelegs-party3`'s write mask |
| list -> OAM bytes | `verifyframe-oam-lockstep`, `--sweepstage2`, `--sweepdrain`, `--checkstage1`, `--checkoam` |

The list's ownership was **proven, not assumed**: forcing the wrong bucket in `CtObjListLink`
(`bucket ^= 2`) takes `$0Exx` divergences **0 -> 7** and the wmask total **23 -> 36**, first divergence
f2 -> f1.

## 2. The hole

`CtPortRunFrame` opens with `CtPortCopyWramFromOracle`, which pulls the whole 128 KB into `g_ram` —
**draw list included**. So `verifyframe-oam-lockstep` grades list -> OAM *given the oracle's list*. A
port whose own list is right but whose OAM builder walks the WRONG list reads clean there. Each piece
graded, the composition not: the same shape that hid the dead object-collision path for three commits.

## 3. Why it cannot live in `--vmdiff`

That harness's memory model is `$7E` plus `$7F:0000-01FF` and `$7F:0200-03FF` only, and `w8()`
**silently drops** `$7F:0400+` — counted as `gWmDropped`, which is why the wmask line reports
`dropped 0`. The staged OAM records at `$7F:4BC0` and the assembly templates at `$7F:4800` are not
addressable in the lockstep harness at all.

## 4. `VERIFYFRAME_OURLIST=1`

The oracle's copied draw list is discarded and rebuilt by OUR legs in `C0/A810`'s exact loop order:

```
A81B/A822/A826   skip $1100 bit7 and class 7
A82D  JSR C0/A88D / BCC ->A83E     the activation gate
A838  JSR C0/AB45                  screen position -> $0A80
A83B  JSR C0/A9CD                  link into the bucket chain
```

Then the emit and stage-2 copy run over a list this port produced. Positions stay the oracle's; the
**selection and the bucket order become ours**. Result: `0 NON-attr mismatches, 0 high, 0 frames differ`
over 180 frames with `ourList linked 606`.

⚠ **`ourList linked` IS THE ANTI-VACUITY ASSERTION and is pinned.** The sibling check reads
`ourList linked 0`, so the two lines together prove the rebuild ran here and did not run there. It is
also what caught the first cut: omitting the `A88D` gate linked **109 objects too many** (emit 715 vs
606) and produced **28243 mismatches that were the HARNESS's fault, not the port's**. A 0-mismatch line
beside a 0 link count would have meant nothing.

## 5. The mutation split, which is the actual result

| mutation | `verifyframe-oam-lockstep` | `verifyframe-oam-ourlist` |
|---|---|---|
| link inserts as bucket TAIL not head (drop `AA00`) | 0 | **720 mismatches** |
| `bucket ^= 2` | 0 | 0 |

The first is a defect **only the new check can see**. The second is invisible to both OAM runs and is
caught by the write mask instead. So **ORDER is graded by the OAM composition and BUCKET ASSIGNMENT by
the write mask** — neither check subsumes the other, and both are needed. Written down because a single
green line invites the assumption that one of them is redundant.

---

# ⭐ THE OAM COMPARE INSIDE `--vmdiff` — and the 63 KB the seed was leaving at zero

`verifyframe-oam-ourlist` grades our draw list through our OAM production, but its UPSTREAM is the
oracle's: `CtPortRunFrame` copies the whole 128 KB in every frame. This closes the last version of the
question — the scene is seeded ONCE and every frame after is this VM's own scheduler, movement, legs and
drain, so it asks whether our engine puts the same sprites in the same OAM slots as the ROM given only
a seed.

## 1. What unblocked it: the seed was covering 3 of 4 regions

The earlier verdict was "it cannot live in `--vmdiff`, whose memory model stops at `$7F:03FF`". That was
half right and half wrong. `w8()` does drop `$7F:0400+` — but `Mem` **is** the flat 128 KB image:

```c
typedef struct CtWram {
  uint8_t s[0x10000];    // $7E:0000-FFFF  (and the flat base)
  uint8_t g[0x200];      // $7F:0000-01FF          flat +0x10000
  uint8_t l[0x200];      // $7F:0200-03FF          flat +0x10200
  uint8_t hi[0xFC00];    // $7F:0400-FFFF   <-- exists, and was never seeded
} Mem;
```

with a compile-time assert on `sizeof(Mem) == 0x20000`. And the drain/draw passes take `m->s`, the flat
base, and index it raw — which is exactly why `dropped` reads 0: they never go through `w8()` at all.

`CtSceneSeedFromWram` copied `s`, `g` and `l` and **not `hi`**, leaving 63 KB of bank `$7F` at zero —
including the sprite assembly templates at `$7F:4800` and the staged OAM records at `$7F:4BC0` that
`C0/CAD9`'s drain reads and `C0/B309`'s stage 2 copies out. Measured: our image held **0** nonzero bytes
across `$7F:4BC0+0x540` where the oracle held **1158**.

⚠ Extending the seed left both pinned wmask counts **EXACTLY unchanged** (242 addr / excluded 16 /
23 divergences), which is how it is known to be a gap-fill rather than a behaviour change.

## 2. The result — a RATCHET, not a clean line

```
2559 main + 6 high byte mismatches in 129 of 130 frames
visible sprites: ours 1788 / oracle 1784
by byte:  X=1707   Y=328   tile=4   attr=520
```

**Tile is essentially exact** — 4 mismatches across ~232k compared sprite-bytes — and the sprite
POPULATION matches (1788 vs 1784). So the selection and the tile decode are right; the residue is
placement and attribute.

## 3. Where X and Y come from — traced, not guessed

The wmask in the SAME run reports `$7E0A02` (obj1, f2, ours 47 vs oracle 3F), `$7E0A06` (obj3, f26) and
`$7E0A08` (obj4, f49). `$0A00,X` **is** the screen-X table `C0/AB45` writes, and `C0/B701`'s reposition
arm computes each piece's placed X as `xd + $0A00`. So the X column is the **pixel cost of three
divergences the write-mask compare already pins inside its 23** — this check is the first instrument
that prices them.

`attr=520` is expected here and is NOT a new defect: the C-maintained per-tile OBJ priority (the
`cTplAttr` caching in `CtPortRunFrame`) is a `--verifyframe` fixture and is not wired into this harness,
so the attribute byte is unmaintained. Excluded from the interpretation and PRINTED, per the `$0F01`
precedent — never tolerated by a fudge rule.

## 4. Sensitivity, measured rather than asserted

With `CT_FRAMELEGS=0` the same run reads `X=1748 Y=546 tile=224 attr=740` with **1560** visible sprites
against the oracle's 1784. So the frame legs move tile **224 -> 4**, Y **546 -> 328** and the population
**1560 -> 1788**. That is the proof the compare is driven by the engine under test and not by the seed.

## 5. Two modelling errors, both caught by the anti-vacuity counter

1. First cut built into a **zeroed scratch buffer**. Unwritten slots then read Y = 0, which counts as
   visible, so the sprite count came out 16640 = 128 x 130 — every slot "visible" in every frame.
2. The OAM buffer is **persistent WRAM**. Modelling it needs our own image's `$7E:0700`/`$0900`, not a
   scratch copy, or slots we never write drift away from the oracle's forever.

Both were visible only because the report carries `ours N / oracle M` beside the mismatch count. A
mismatch total on its own would have read as a plausible defect in the engine instead of a bug in the
harness.

---

# ⭐ THE `$0A0x` SCREEN-X DIVERGENCES — a harness gap, and the routine was innocent

`vmdiff-oam-lockstep` priced three `$0A0x` entries in the wmask's 23 at `X=1707` OAM mismatches. This
chases them, and the first useful step was NOT to look at the routine.

## 1. `C0/AB45` was already exact — so the fault is in its inputs

```
AB45: $D9 = $1D87 & 7        ; the FINE camera phase, X
      $DB = $1D89 & 7        ;                        Y
      $DD = ($1D0A & FF)*8 + $D9
      $DF = ($1D0E & FF)*8 + $DB
      $0A00,X = (($1800,X >> 4) - $DD) & 01FF
```

`CtObjScreenPos` is graded **720/720 byte-exact** against micro-executed `C0/AB45`
(`sweepscreen-obj-position`) and `--checkxform` compares 606 coords with 0 mismatches. So the arithmetic
is not the defect; the inputs are `$1800`, `$1D0A/$1D0E` and `$1D87/$1D89`.

`ours 47 vs oracle 3F` is **+8**, and the camera term is `coarse*8 + phase` — so +8 is one coarse unit,
or a full 8 of fine phase.

## 2. Measured: the coarse cells agreed, the STEP COUNTERS did not

`runVmDiff` already injects the oracle's `$1D0A/$1D0C/$1D0E/$1D10` each frame and excludes them, on the
stated grounds that the scroll is driven by the player layer this mode never calls. **It missed
`$1D87`/`$1D89`.** A per-frame probe:

```
[cam] f2   $1D0A ours=4D ora=4D | $1D0E 48/48 | $1D87 90/8F | $1D89 F0/F0
[cam] f3   $1D0A ours=4D ora=4D | $1D0E 48/48 | $1D87 90/8E | $1D89 F0/F0
[cam] f4   ...                                 | $1D87 90/8D
```

Ours frozen at the seed's `0x90`; the oracle's walking down one per frame of the held Left
(`CT_BTN=200`). Fine term 0 against the oracle's 7. The port DOES maintain `$1D87` — `camPut16(s,
0x1D87, ±1)` per scroll step, zeroed at entry — it simply has nothing driving it here.

**A harness-scope gap, not an engine defect**: the same argument the `$1D0A` injection already rests on,
applied to the two cells it forgot.

## 3. Everything downstream moved at once

| | before | after |
|---|---|---|
| `[cam]` divergent frames | every frame from f2 | **none** |
| wmask divergences | 23 | **21** |
| OAM main / high mismatches | 2559 / 6 | **2108 / 1** |
| OAM `X` column | 1707 | **1256** |

## 4. What remains — a hypothesis with its evidence, not a finding

The three `$0A0x` entries persist, now off by exactly **+1** instead of +8:

```
f2   $0A02  ours=40 oracle=3F      f2   $1603  ours=01 oracle=07
f25  $0A06  ours=57 oracle=56      f25  $1607  ours=04 oracle=07
f48  $0A08  ours=6E oracle=6D      f48  $1609  ours=01 oracle=07
```

Each pairs with a `$16xx` **anim-timer** divergence at the same frame. The lead's and the followers'
`$1800/$1880` are themselves injected and excluded in this mode, so a +1 in the screen X is our movement
sitting one sub-step ahead *within* the frame — the shape of the one-frame cadence lag already pinned in
`vmdiff_pinned_triage.md` §4, not a second defect. Recorded as a hypothesis.

## 5. The transferable part

**When a routine is already graded byte-exact against micro-execution, a divergence in its output is a
statement about its INPUTS.** Reading `C0/AB45` again would have found nothing; the +8 was arithmetic on
a frozen byte. And the byte was frozen because an injection list was one pair short — the kind of gap
that only shows up when something downstream finally prices it.

---

# The one-frame cadence lag: op 0x87 re-arms the countdown (C0/488B)

`vmdiff_pinned_triage.md` §4.4 pinned it as a phase, not an op: on `party3_field` **both** sides sit on
obj33's `0xE8` from f126 to f128, and then "the oracle visits obj33 at f129 and the port at f130."

## Why every 8-bit xref for the countdown came up empty

The queue build `C0/59D9` opens `REP #20 / LDA #1000 / TCD` -- **DP = $1000**. So its `LDA $01,X` and
`DEC $01,X` are `$1001,X` in direct-page form, and `LDA $00,X` is the period `$1000,X`. An absolute
`BD 01 10` xref finds nothing. This is the SIXTH time this project has been misled by the field
engine's non-zero DP ($68 -> $0168, $71 -> $0171, $6D -> $016D, $F8 -> $7E01F8, and the scheduler's
$1000 here). **When a bank-$C0 routine's operand looks like a scratch byte, find its `TCD` first.**

## The build, whole (C0/59D9-5A44)

```
59D9: PHD / REP #20 / LDA #1000 / TCD / SEP #20     ; DP = $1000
59E2: LDA $7F2000 / ASL A / STA $01BD               ; loop bound = nobj*2
59EA: LDX #00
59EC: LDA $00,X   ($1000,X)  the PERIOD, bit7 = paused
59EE: BMI ->59F8                                    ; paused -> skip, NO decrement
59F0: LDA $01,X   ($1001,X)  the COUNTDOWN
59F2: BEQ ->5A01                                    ; ALREADY zero -> enqueue
59F4: DEC $01,X
59F6: BEQ ->5A01                                    ; JUST reached zero -> enqueue
59F8: INX / INX / CPX $01BD / BNE ->59EC / PLD / RTS
```

Two `BEQ`s around one `DEC`, and then a gate chain before anything is enqueued:

```
5A01: LDA $1100,X / BEQ ->5A31   ; move-mode 0     -> PREPEND at the head
5A06:              BMI ->59F8    ; move-mode bit7  -> SKIP
5A08: CPX $0175 / BEQ ->59F8     ; already the tail -> skip
5A0D: LDA $80,X ($1080,X) / BPL ->59F8   ; link byte non-negative = already IN the list -> skip
5A11..5A2F  append at the tail ($0175), or seed head+tail if the list is empty
5A31..5A44  prepend at the head ($0174)
```

`$0174` = head, `$0175` = tail, `$1080,X` = the next-link (negative = not linked). The runner `C0/5A46`
drains that list and yields on the raster (`CMP $69`), so it is a **carry-over work queue**, not a
per-frame rebuild.

**The reload `LDA $00,X / STA $01,X` appears at 5A1F, 5A2B and 5A40 -- inside every enqueue arm, and
nowhere else.** That is what the `BEQ` before the `DEC` is for: a countdown that expires while its
object is gated stays at 0 and retries on every later build.

## The defect

op 0x87 (`C0/4876`) stores the new period **twice**:

```
4878: LDA $7F2001,X / LDX $6D / INC A / STA $D9    ; $D9 = operand + 1
4881: LDA $1000,X / AND #80 / ORA $D9
4888: STA $1000,X        ; the period, pause bit preserved
488B: STA $1001,X        ; and the COUNTDOWN -- the same byte
```

The build (`00C1`) runs before the runner (`00C4`), so when obj33's `87 02` executes, PASS 1 has already
reloaded the countdown from the **old** period 4. The ROM's second store overwrites it with the new 3.
Writing `$1000` alone -- which the port already did -- cannot move a countdown that was reloaded a
sub-step earlier; the period is not read again until the next expiry. Hence 4 frames instead of 3.

The port's own comment at the handler had stated the condition for fixing this and then declined it:
*"Port the per-frame maintenance or leave $1001 unmodelled; do not do the half."* So the countdown is
now the WRAM byte: `ctSetDc()` writes `$1001,X` at every site that touches it, PASS 1 is the literal
`BEQ / DEC / BEQ`, and the nine `STZ $1001,X` sites (`C0/31AA`, 3264, 3301, 3386, 340D, 34D9, 352C,
5B1B, 5B5F) now store **0** where the port stored 1 -- identical visit timing, but the ROM's byte.

Measured after: obj33 runs `0xE8` at **f129**.

## And the gate that fell out of it

With the countdown in WRAM the write mask could finally price it, and four addresses survived:
`$1001,X` for objects 2, 5, 6 and 7, ours 4 where the oracle holds 0 for all 130 frames. All four have
`$1100 = 0x80`, so `5A06 BMI` skips them -- *after* the `DEC`, so they drain to 0 and never reload.
`$1100 = 0x80` is not "idle": it is a REMOVED object (op 0x0A) or **a PC slot for a character not in the
active party** (the load op's `: 0x80` fallback). party3 is a 3-member party, so objects 2/5/6/7 are the
four absent PC slots, parked on op 0xB0, and the ROM never visits them. Porting `5A06` took the check
from 20 masked divergences to **zero on a LARGER mask** (238 -> 264 claimed addresses).

Three mutations, all caught by that mask:

| mutation | result |
|---|---|
| drop op 0x87's `STA $1001,X` | first divergence f124, 2 addr, + a scheduler divergence |
| drop the `5A06` `$1100` bit7 skip | first divergence f1, 4 addr |
| decrement-then-test (drop `59F2`'s `BEQ`) | first divergence f1, 5 addr, + a scheduler divergence |

The third one matters: it proves party3 actually *exercises* the already-zero arm, so the two-`BEQ`
shape is graded, not merely transcribed.

## RETRACTION: the $0A0x +1 was NOT this

The section above this one recorded a hypothesis: that the three `$0A0x` screen-X divergences, each
+1 and each paired with a `$16xx` anim-timer divergence, were "the shape of the one-frame cadence lag
already pinned in `vmdiff_pinned_triage.md` §4". **They are not.** The cadence lag is fixed and
`vmdiff-framelegs-party3` still reports the identical 21 addresses starting at f2, `$0A02 ours=40
oracle=3F` included. The `$16xx` anim timers are a separate defect and still open.

## Still unported here, and named rather than implied

- The **linked queue itself** (`$0174` / `$0175` / `$1080,X`, arms 5A11-5A44). The port keeps an
  index-ordered eligibility array, so it does not model move-mode 0's head-**prepend** (those objects
  run FIRST in the ROM, not in index order), nor the `$1080` "already queued" skip, nor a queue that
  survives a frame because the runner hit its raster deadline mid-list.
- The **period** still lives in `moveAnim`, not `$1000`: the fresh-spawn default 4 is a modelled value
  the port never stores, so `$1000` is only authoritative on a WRAM-seeded scene.
- The live load ops (0x80-0x83) update `sc->mode[]` but **not** WRAM `$1100`. The `5A06` gate therefore
  reads the mirror. `C0/5BD5`'s ported scan reads the WRAM copy, so on a scene that live-loads an
  object those two disagree. Pre-existing, and now written down.

## The plumbing bug that came with it, and why the suite caught it and the oracle did not

Moving a counter from a C field into WRAM means EVERY writer has to be converted, and one was missed:
the fresh-scene init still set `st[].dc` without writing `$1001,X`. PASS 1 then read a zero, which
`C0/59F2` correctly interprets as "already expired", so on a cold-loaded scene every object whose fn0
has no op 0x87 ran up to three frames EARLY on its first visit. Six checks went red -- scene 5's two
render hashes, scene 7's two poll hashes, `field-solid-liveness` (`deflected 0 -> 2`) and
`field-battle-bat9-return` -- and the party3 lockstep stayed perfectly clean the whole time, because
party3 is WRAM-SEEDED and its `$1001` came in from the savestate.

**A byte that is seeded from the oracle is invisible to the oracle compare on the path that INITIALISES
it.** The lockstep harness can only grade the frames it runs; it cannot grade a cold boot it never
performs. The render/self-test tier is what covers that path, which is the argument for keeping both
tiers rather than trusting the byte-exact one.

Bisecting it also disposed of two wrong suspects in one step: disabling the `$1100` gate alone and
disabling op 0x87's store alone BOTH left scene 7 at the wrong hash, which is what pointed at the
shared plumbing instead of either derived mechanism.

## ⛔ RETRACTED 2026-09-28: the zero WAS the cartridge's -- the first build after a load visits EVERY object

The section above graded the fresh-scene path against the port's own earlier output (six port hashes and
a port tally), not against the cartridge, and "fixed" it by writing each object's PERIOD into `$1001,X`
after the init pass. The cartridge does the opposite. The load's per-object fill `C0/571A-58AA` stores
`$1000,X = 04 / $1001,X = 00` (`C0/5786`, `CtObjSceneDefaults`), nothing else in the load writes the
countdown except an fn0 `87` (`C0/488B`), and the first build reads `59F0 LDA $01,X / BEQ ->5A01`: every
object is enqueued and reloaded on the first frame after the load. Measured on the logical oracle, every
load looked at (`--monitor 7E1000:n --diff --writer`):

| load | after the fill | first build |
|---|---|---|
| 16E (attract, from power-on under the rule) | c2505 `04 00 04 00 04 00 04 00` | c2513 `04 04 04 04 02 02 04 04` (obj2's `87 01`) |
| 0AD / 0E6 | c3588 / c7582 all counts 00 | c3599 / c7591 all reloaded |
| 112 (`rule_s4440` +155 / +165) | `04 00 01 00 .. 02 02 ..` | every non-`$1100`-bit-7 object reloaded |
| 468 -> 468, stairs (`saves/field_re/scene468_stairs_to_120.state`, Right+B) | f46 `04 00 01 00` (C0:42E1) | f52 `04 04 01 01` |

With the period written, a period-4 object made its first visit 3 frames late -- the attract's 16E clouds
3 px (BG2) and 12 px (BG3) off their layer scroll for the whole leg, 0E6's robots and 112's curtain 3
frames after their scene, 16E's and 112's window wipes armed on the other `$0153` parity (the first slice
of the curtain lost). Scene 7's `fgskip 658 -> 646` is simply what the correct count produces
(`attract-count-after-load-16e-vs-logical-oracle`, `build/_divergence/audit_2026-09-28.md` item 1).

The same audit found the kill latch behind it: op 0x0A (`C0/6254`) is `$1100 = 80 / $1A81 = 00` and
nothing else, so a later load op in the object's own fn0 (loc 112's obj10/obj12, `83 88 ..`) puts it back
in the queue; the port's `dead` never cleared. A killed object is skipped at `5A06`, AFTER the `DEC`, and
the runner (`5A66`) runs whatever was queued with no look at `$1100`
(`attract-kill-revive-and-pc-arms-112-vs-logical-oracle`; the Manoria nuns lockstep lost seven divergent
counts with it).
