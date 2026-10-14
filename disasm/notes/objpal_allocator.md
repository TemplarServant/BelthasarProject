# The OBJ palette allocator — C0/7170 / C0/734C, derived whole

Derived 2026-07-29 to close three write-mask divergences that are all one mechanism (loc `$082`,
walking left, 130 frames):

```
[wmask] f1    $7E0B80 m=FF ours=00 oracle=80 seed=80
[wmask] f126  $7E0B81 m=FF ours=42 oracle=40 seed=80
[wmask] f126  $7E0FC3 m=FF ours=02 oracle=00 seed=00
```

**LANDED.** The comparator goes from 4 masked divergences to **1** — the survivor is `$7F020C`, the
unrelated intra-frame ordering defect — and the whole suite is green, `render-scene6-gato` included, so
the cold-load slots now come out of the canonical mechanism rather than a boxless stand-in. §8 records
the ordering question that blocked the first two attempts and how it resolved.

---

## 1. RAM map

| RAM | meaning | init |
|---|---|---|
| `$0B80..$0B87` | 8 slot owners; `0x80` = free, else the owner's `obj*2`. **Names the NEWEST chain member**, not the original claimer. | `C0/7155`, eight unrolled `STA #80` |
| `$0B00,X` | sharer link **PREV** (toward the original claimer); `0x80` = nil | **never initialised by the ROM** |
| `$0B01,X` | sharer link **NEXT** (toward the newest joiner); `0x80` = nil | likewise |
| `$0F81,X` | OAM palette attribute = `(slot<<1) & 0x0E`. **Never cleared on release** — it is the slot-recovery record the release reads back. | — |
| `$1400,X` | 16-bit share key = `paletteId * 24` (`loadGfxMirror` already writes `hdr[2]*24`) | — |
| `$0154` bit7 | "the script owns the palette table" — aborts **both** allocate and release | `C0/0BF2 STZ` on every location entry |

`$7E0B00` is **not** `$7F0B00` (the per-tile clamp plane). One letter apart, unrelated.

## 2. The gate and the dispatch — C0/7170

```
7170: LDA $54 / BMI ->7180                ; bit7 -> RTS, no writes
7174: LDX $6D / LDA $1100,X / BMI ->7180  ; class bit7 -> RTS, no writes
717B: ASL A / TAX / JSR ($7181,X)         ; dispatch on CLASS*2, NO bounds check
7180: RTS
```

`$7181..$7198` is a **12-entry word table**, byte-verified:
`99 71 | 12 72 | 17 72 | 1C 72 | 1C 72 | B4 72 | B4 72 | 1C 72 | 1C 72 | 1C 72 | 1C 72 | 1C 72`

| class | handler | slots | bound |
|---|---|---|---|
| 0 | `C0/7199` | **{5}** | `LDX #05`, no loop |
| 1 | `C0/7212` | **{6}** | `LDX #06 / BRL ->719B` |
| 2 | `C0/7217` | **{7}** | `LDX #07 / BRL ->719B` |
| 3, 4, 7..11 | `C0/721C` | **0..4** | `LDX #00`, `CPX #05` |
| 5, 6 | `C0/72B4` | **1..3** | `LDX #01`, `CPX #04` |

> The three distinct behaviours make a *range test* arithmetically equal for classes 0..8, which is
> why the port's old three-range stand-in looked correct. The ROM has a **table**, and it covers
> 9/10/11, so the port must have a table. Class ≥ 12 would dispatch through the word at `$7199`; our
> generator only emits `{0..7, 0x80}`, so that is a **tripwire**, never a silent clamp.

## 3. Pass structure

**Fixed-slot (0/1/2), `C0/7199`** — one slot, and **no other slot is ever tried**:
free → claim (`719E BPL` not taken); occupied → share iff the `$1400` keys match, else `71EB CLC/RTS`.

**Scan (`C0/721C` lo=0 end=5, `C0/72B4` lo=1 end=4)** — two passes:
pass 1 SHARE over `[lo,end)` (does **not** skip self, so a re-entry self-shares); on exhaustion falls
through to pass 2 CLAIM, first free slot; exhausted → `72B2`/`734A CLC/RTS`, nothing written.

**Share predicate** = a **16-bit** compare of `$1400` (`7324-732C`, and the copies at `728A`, `71F2`).
The port's two-byte compare is equivalent.

## 4. CLAIM — four stores (`71A0-71B9` / `722F-7248` / `72C7-72E0`, identical)

```
$0B00[me] = 0x80            # 72CD  PREV := nil
$0B01[me] = 0x80            # 72D0  NEXT := nil
$0B80[slot] = me            # 72D7  (writer PC reports C0:72DA)
$0F81[me] = (slot<<1)&0x0E  # 72E0  (writer PC reports C0:72E3)
```

## 5. SHARE — five stores, and **two different variants**

Scan handlers (`7285-72A8`, `731D-7340`) — slot-derived, correct:
```
$0B00[me]       = occupant   # 7332  my PREV := the old newest
$0B01[me]       = 0x80       # 7337  my NEXT := nil
$0B01[occupant] = me         # 733D  old newest's NEXT := me
$0B80[slot]     = me         # 7340 -> 72D5 (LDX $C3 reloads the SLOT) -> 72D7
$0F81[me] = (slot<<1)&0x0E   # 72E0
```

⚠ **The fixed-slot handler is NOT slot-derived, and this is a ROM anomaly to port verbatim.**
`7210` = bytes `80 9E` → target `C0/71B0`, which is **two bytes past** `71AE: LDX $C3` (`A6 C3`). X
therefore still holds `$DB` = the **occupant's** `obj*2` from `720B`. So:

```
$0B80 + occupant*2 = me           # 71B0  *** NOT $0B80+slot -- can land in $0B80..$0BFE ***
attr = (occupant*2 << 1) & 0x0E   # 71B4/71B5  *** derived from the OCCUPANT index ***
$0F81[me] = attr                  # 71B9
```

Do not "correct" it and do not unify the three tails.

## 6. The 48-byte upload, and RELEASE

Every claim/share tail uploads palette data (`71BC` / `724B` / `72E3`, all with MVN operand bytes
`54 7E E4`). **Both MVNs copy the SAME 24 source bytes** — the `PHX`/`PLX` pair around the first
restores the source pointer, so the second block is a re-copy at `+0x200`, **not** `source[24..47]`.
`dest = 0x2100 + attr*16 + 2 = 0x2100 + slot*32 + 2`; `src = ROM $E4:0000 + the $1400 word`.

**C0/734C — a doubly-linked unlink**, four branches (three oracle-measured; the middle unlink at
`7383-738E` needs a 3-member chain no available state builds):

```
gate: $54 bit7 -> return                                  # 734C-734E
nxt = $0B01[me]  prv = $0B00[me]  slot = $0F81[me] >> 1
nxt is nil:  prv nil -> $0B80[slot] = 0x80                # 7370-7377  sole owner -> FREE
             else    -> $0B80[slot] = prv; $0B01[prv]=0x80 # 7365/736B  hand it to my PREV
nxt not nil: prv nil -> $0B00[nxt] = 0x80                 # 7390-7394  I was the ORIGINAL
             else    -> $0B00[nxt]=prv; $0B01[prv]=nxt     # 7385/738B  middle unlink
```

`$0F81` is deliberately not cleared — `7361`/`7373` recover the slot from it, and `src/oam.c` ORs it
into every sprite's OAM attribute.

## 7. THE TRIGGER — and the one thing still open

**Assignment and release are two exits of ONE routine, `C0/A88D`**, chosen by reading `$0F00,X`
*before* overwriting it:
`A8E5 LDA $0F00,X / BMI ->A90E` skips assign when already active → assign is the **rising edge**, and
`A905` (the instruction after the latch store) is the only caller of `C0/7170` on this path;
`A910 LDA $0F00,X / BPL ->A934` skips release when already inactive → release is the **falling edge**,
at `A91B`. Callers, by whole-ROM scan: `7170` ← **`A905`, `B188`** only; `734C` ← **`0802`, `A91B`,
`A942`**. (`C0/0802` is the battle epilogue; `A942` has no reachable caller.)

**Measured, and it predicts the oracle exactly:**
```
f0    7E0F40=00 FF                7E0B80=80 80 80 80 80 02 06 08
f73   7E0F40=80 FF (by C0:A905)   7E0B80=80 40 80 80 80 02 06 08 (by C0:72DA)
```
obj32's latch goes `00 → 80` at f73 written by the JSR site itself, the slot is claimed that same
frame, and **never again in 200 frames**. `$7E0154 = 0x00` throughout, so the gate never fires here.
The seed's `$0B85/86/87 = 02/06/08` are objs 1/3/4 = the three PCs in their fixed slots 5/6/7.

**The scene-entry sweep is a DIFFERENT routine with a DIFFERENT box** — `C0/B0E6 → C0/B127`, and it
must not be unified with the per-frame pass:

| term | per-frame `C0/A88D` | scene-entry `C0/B127` |
|---|---|---|
| `$C3` | `$1D0A - 4`, clamp 0 | same |
| `$C4` | `$1D0C + 4`, clamp `0xFF` | `+4`, **no clamp** |
| `$C5` | `$1D0E` **− 2**, clamp 0 | `$1D0E`, **no −2** |
| `$C6` | `$1D10 + 5`, clamp `0xFF` | `+5`, **no clamp** |
| class 8 | skipped (`A822`) | **admitted** (`B0F8` tests only `#07`) |

On a pass `B127` stores `$0F00,X = 0x80` and calls `C0/7170`; on a fail it does neither. Its other six
stores (`$0E80`, `$0E81`, `$1681`, `$1601`, `$1B00`, `$0F01 = 0xFF`) belong to draw-list/anim
machinery the port does not model — writing them would claim write-mask addresses we cannot keep
correct (the oracle animates `$0F01` every few frames).

### ⛔ THE BLOCKER, and it is ours, not the ROM's

The implementation above was written and measured: **write-mask 4 → 1 divergence**, only the unrelated
`$7F020C` surviving, mask 47 → 41. Then `render-scene6-gato` went red, and the probe says why:

```
[palsweep] obj1 cls=00 vis=01 t=(0,0)   box=[0,36)x[0,33) pass
[palsweep] obj8 cls=06 vis=01 t=(16,16) box=[0,36)x[0,33) pass
```

Both objects **pass the box** — but the check's `[alloc]` dump is sampled *before* the sweep runs, and
obj1's tile reads `(0,0)` because **the lead's position is not yet established** at the point the port
can first evaluate the box. The port's entry sequence is
`CtSceneCreate → CtSceneCameraEntry → (field.c positions the lead) → render`,
and the box needs the camera (so it cannot run in `CtSceneCreate`, where `$1D0A..$1D10` are still zero
from `memInit` — that collapses the box to `[0,4)×[0,5)` and rejects everything, losing GATO's slot).

There is **no correct partial**: the per-frame edge hook alone drops the cold-load assignment, and any
boxless scene-entry call is a stand-in.

## 8. RESOLVED — where `C0/B0E6` sits, and the port's hook point

Two distinct routines, and the split is the answer. Both read at m=1 with x=1 **and** x=0; the streams
agree, so there is no width ambiguity here.

```
scene LOAD      C0/56B0:  6F79 -> 7155 -> E935 -> 5929 -> B204 -> A7E9
                          -> 5709   <-- THE SCRIPT-INIT RUNNER = object placement
                          -> 595C -> (JSL C2/8004) -> RTS

location ENTRY  C0/286C:  28E1
                          -> 0A50 ... 0A6D: JSR C0/74A6 -> 7506 (X) / 7553 (Y)  <-- THE CAMERA
                          -> 6F79 -> 7155 -> E935 -> B204 -> A7E9
                          -> B0E6   <-- THE SWEEP, 0x12 bytes after the camera
                          -> 6E5C -> 70E9 -> (JSL C0/29F7) -> 6EB0 -> ...
```

**Placement lives in the LOAD; the camera and then the sweep live in the ENTRY, in that order.** The
sweep is the last member of the `6F79/7155/E935/B204/A7E9` group, and `C0/0A50` — which contains the
`JSR C0/74A6` camera-entry pair at `0A6D` — runs *before* that whole group.

Independent confirmation from the oracle, which does not depend on the call graph at all: on a cold
scene-6 load GATO receives slot 1 (`$0B80 = 80 10 80 80 80 02 80 80`, `$0F81 = 0x02`). If placement
ran *after* the sweep, GATO could not have been inside `C0/B127`'s box. So placement precedes it.

### The port's hook point

Narrower than §7 suggested. Measured with a probe inside the sweep:

```
[palsweep] obj1 cls=00 vis=01 t=(0,0)   box=[0,36)x[0,33) pass     <- the LEAD, unplaced
[palsweep] obj8 cls=06 vis=01 t=(16,16) box=[0,36)x[0,33) pass     <- GATO, script-placed, correct
```

Script-placed objects **are** positioned by the time `CtSceneCreate` returns. Only the **lead** is not:
`CtSceneSetActorPosF` for the player object is called exclusively at `field.c:6709`, *inside the
per-frame loop* — there is no scene-entry sync. In the ROM the lead is placed by the script during
`C0/5709` like everything else, so it is positioned before the sweep.

So the sweep belongs in `field.c`'s fresh-entry branch (the `!camSeeded` site, ~`:5018`), in this order:

1. push the lead into the VM (`CtSceneSetActorPosF(f.vm, pObj, px, py)`) — the ROM's `5709` placement,
2. `CtSceneCameraEntry(...)` — the ROM's `0A50` -> `74A6` -> `7506`/`7553`,
3. `palSweepB0E6(sc)` — the ROM's `2881`.

It must NOT sit at the end of `CtSceneCreate` (no camera yet: `$1D0A..$1D10` are still zero from
`memInit`, collapsing the box to `[0,4)x[0,5)`) nor at the end of `CtSceneCameraEntry` (no lead yet).
Both were tried; both fail, differently.

**Acceptance:** `render-scene6-gato` must report `[alloc] ... 0B80=8010808080028080` and
`[objrow 8] 0F80=0002`, and `vmdiff-wmask-party3` should go 4 -> 1 address (the measured result of the
reverted patch). Everything else in this dossier is implementable as written.


## Ported, measured, and the two things that cost a rewrite each

Ported whole in `82b186b`; write-mask divergence 4 -> 1, then **0** in `2ea4370`.

### THE ORDER (this blocked two attempts)

`LOAD C0/56B0` -> `5709` **places the objects**; only then does `ENTRY C0/286C` put the camera
(`0A50` -> `74A6` -> `7506`/`7553`), and only after that does it sweep (`2881` -> `B0E6`). The sweep
needs BOTH. It cannot sit at the end of `CtSceneCreate` (camera still zero -> the box collapses) nor
at the end of `CtSceneCameraEntry` (the lead is not placed yet). It runs in `field.c`'s
fresh-entry branch.

### The `CT_DUMPOBJROW` probe was sampling at the wrong time

It advertised itself as an "oracle fresh-load comparison" but sampled **before** scene entry, while
the oracle's value is **post**-entry (loc-6 settled: `$0B80 = 80 10 80 80 80 02 04 80`). It only
looked correct while the port was assigning at the wrong moment too — two wrongs agreeing. Moved.

### The last write-mask divergence was the HARNESS, not the engine

`$7F020C` was reported as ours and diverging. It is not an engine defect: `--vmdiff` was injecting
the lead's **post**-commit position where the ROM's VM reads the **pre**-commit one. `C0/881E` only
computes a velocity; it is `C0:AB28`, inside `C0/A810` and therefore **after** the VM, that writes
`$1800`. Fixing the injection point took the write-mask to zero divergences.

> ⚠ **OPEN:** `field.c`'s own loop has the SAME pre/post inversion, and nothing in the suite
> evaluates it today.

## 9. The sweep's inputs were the port's, not the ROM's (2026-09-10 -- the scene-8 palettes)

User report on scene 8 (the Telepod): the demo's sparkles and some NPCs drawn with the wrong palette,
"since the recent commits". Measured on the port's own demo (`CT_CALLFN=12:2:2:100`, the touch request
for obj12's fn2, storyline 09) against the oracle's mid-demo states (`scene8_taban_moving.state`,
`scene8_taban_moving2.state`):

* **Why it appeared with `6f77b68`** (the load's per-object default fill). Before it the allocator never
  ran at all -- the pre-fill build at demo f600 has `$0B80` all free and every `$0F81 = 00`, the CGRAM
  rows empty -- so every sprite drew from its own baked record and looked right by accident. The fill
  made `$1100`/`$1A81`/`$1000` real, the trigger fired, the rows got uploaded, the sprite blit switched
  to the slot rows (`2875247`), and every wrong slot decision became visible. The fill is correct (HEAD's
  bytes match the oracle's for every object checked); the allocator's INPUTS were not.
* **Divergence 1 -- the tile.** `palSweepB0E6` and the per-frame live test derived the tile from the
  host float (`fx / 16 * 2`) where the ROM reads the BYTE: `B155 LDA $1801,X / ASL` and `A8D0` the same.
  An object the script has not placed yet carries the fill's `$FF` (tile `$FE` after the ASL, outside
  every box) while its float is 0 (inside). Scene 8's obj21 (`82 56 / AA 00 / 00`, placed only by its fn3)
  claimed slot 2 at the load in the port; the ROM's obj21 gets a slot only at its `8B 06 05 / 90`.
* **Divergence 2 -- the bytes were not there yet.** Reading the byte exposed that the init pass
  (`runFunc`, the C0/5709 analogue) placed objects host-side only; the WRAM words landed at the first
  tick, AFTER the sweep. Scene 6's GATO (`83 92 86 / 8B 08 08`) lost slot 1 the moment the sweep read the
  byte the ROM reads. The placement ops now store what their handlers store, at the init pass and in the
  live path: `8B` (C0/4B74) `$1800 = $80, $1801 = tile; $1880 = $FF, $1881 = tile`; `8D` (C0/4C27) the two
  raw words; `8C` (C0/4BC3) the loc's LOW byte into `$1801`/`$1881` and `$FF` into `$1880`, `$1800`
  untouched. (Writing `8C`'s loc as a word put scene 439's soda-stand object on the wrong tile for one
  frame and the object-solid test C0/5B95 moved the player a pixel -- the suite caught it.)
* **Result.** Scene 8's load table is now `30 1a 38 80 80 06` -- obj24, obj13, obj28, free, free, obj3 --
  the ROM's structure (the taban state reads `30 1a 38 16 80 06` after obj11's later claim; obj28 in
  slot 2 in both). The demo's sparkles (key `$0D98`, the white/blue E4:0D98 record) now claim their own
  slot (`$0F81 = 06` here, `08` in the oracle's phase: obj11 takes slot 3 first there) instead of
  falling back to attribute 0 = Taban's slot-0 greens.
* **The load-time storyline matters.** obj14 (Marle)'s fn0 is `16 00 0A 03 02 / 91`: mode 3 (C0/64A1,
  `CMP / BCC -> CLC`) FALLS THROUGH to the HIDE while storyline < $0A. A test seeded with a mid-demo
  flags file (storyline $0A) keeps her visible and she claims a slot at the load; the ROM enters loc 8 at
  $08/$09 (the fair states read $08; obj13's `5A 0A` sets $0A inside the scene). Seed scene-8 tests with
  the entry storyline.
* **Not regressions, checked on the way:** Crono resting at (111,144) vs the oracle's (111,160) in Marle's
  phase is the approach side of `96 06 09` (the headless drive left him on the right pod, the player
  walks up from below); obj11 (spr `$63` at (64,80)) is the PENDANT, not the warp field -- shown when Marle
  vanishes, hidden by its own fn1 at the pickup; the gate travel is the bank-FD tunnel (oracle
  `scene8_gate_cutscene.state`: black at f2400, the tunnel at f3000, storyline `$40` while it runs), still
  the recorded unported boundary.
* Instrument: `CT_PALTRACE=1` prints one line per object at the sweep (class, vis, tile, box, verdict).
* **Pinned** by `field-scene8-objpal-sweep-demo-sparkle` (quick tier): the nine sweep verdicts at the load, the
  sparkle obj15's row at demo f699 (`$0F81 = 06`, key `980D`, visible), the `$0B80` table and the f700 frame;
  two mutations (the init-pass mirror removed; the float tile back) both fail it. Fixture
  `saves/field_re/telepod_entry09.flags` = the taban state's flag page with the storyline set back to `$09`
  (under the real fair flags at `$08` the demo loops on message 82 and never reaches the pods).
* The soda-stand check `field-soda-minigame-drink-anim` re-pinned 257 -> 258 on the way: its 257 was the
  port's (the stand's `$1800/$1880` words not in WRAM at the first up-probe of C0/9923, so the player slipped
  a pixel before the second probe hit); the ROM stores them inside C0/5709 before any input frame.

## ADDENDUM (2026-10-13) — C0/B0E6 on EVERY location load, and ported whole

The user (translated from Portuguese): back in scene 8 from 600 A.D. through the gate, "I guess even HER inventions have to
work, sometimes." appeared with no one talked to, then Marle's "Phew! We're back!". Capture: F5 beside the 600 A.D. gate
(saves/field_re/gate_return_600ad_to_008.state), Down into it.

**The ROM.** Scene 8's obj22 (fn0 `... 16 00 0C 04 05 / 0A 2C / 10 02 / B2`, fn1 `16 56 02 06 05 / BB 84 ...`) kills itself on
its first visit; C0/6254 ends `SEC`, so the visit runs on through `10 02` (C0/62B5: ip + 1 + 2) into fn1 and its `BB 84`
arms a box (`$0129 = 1`, `$7F0A00,X = 1`, string 132). C0/A910's FALLING edge then cancels it (`LDA $7F0A00,X / BEQ / LDA $30
/ BNE / LDA #0D / STA $29`) -- but only for an object that WAS latched. On the rule's cartridge it was: C0/2881 -> C0/B0E6 at
the location's entry stores `$0F2C = 80` (C0:B178) before any script runs, the death comes two frames later, `$0129 = 0D`
(C0:A934), and the machine is idle again 15 frames after that. No box is ever seen.

**The port** ran C0/B0E6 only on the harness's fresh entry (sec. 8), never on a location change in play: obj22 was not latched,
its box opened and waited for A, and obj4's "Phew" parked behind it (`$0129 = 07`). Now:
- `palSweepB0E6` is C0/B0E6 whole: on a pass the rising edge's stores (`$0E80 / $0E81 / $0F00 = 80`, `$1681 / $1601 / $1B00 =
  0`, `$0F01 = FF`), C0/7170, C0/6F9A, then C0/AB45, C0/A9CD, `STZ $79`, C0/CAD9 until it pops the object, C0/B1B2, C0/EA42,
  and the sweep's `BRL C0/B271`. With the legs off (legsOn) it keeps the latch and the palette, as the per-frame pass does.
- field.c's location change owes it (`entrySweepPend`) and makes it on the entry ramp's first frame -- C0/2848's
  `JSR C0/286C`, the frame of the first scheduler pass: the port builds the location at the commit and holds the black after
  it, and the cartridge claims the slots there (the attract's loc 112: `$0B80` at f4492).
- Not over a restored F5 snapshot (`--loadstate`): the snapshot already holds what the sweep wrote; running it again reset the
  animation timers of a scene in progress (field-defeat-holds-the-room-and-the-fight-sprites-vs-rule-cartridge).

Check field-gate-return-008-dead-npc-box-cancelled-vs-rule-cartridge. Re-pinned (port-only): render-scene1/5-snapshot,
field-scene8-objpal-sweep-demo-sparkle (`$0D00` of the sparkle 88 -> 40: the entry now takes the OBJ slots in the ROM's
order), ow-600ad-motion-owners-under-the-mist. Every vs-oracle check holds; suite 841/841.
