# The object LOADER — what populates the animation tree's inputs

Derived 2026-07-31. This is the routine family that blocks wiring `src/move.c` into `field.c`: the
whole `C0/CAD9` tree is byte-exact but reads WRAM tables the live engine never fills. **Not ported.**

## The four ROM tables it reads

`C0/0BB0`–`0C0E` sets four long pointers, all bank `$E4`, from a table at `$E4:FFE0`:

| DP | value | contents |
|---|---|---|
| `$AF` | `$E4:2000` | graphics-packet pointers, **3 bytes** per entry |
| `$B2` | `$E4:2300` | animation frame-record pointers, **3 bytes** per entry |
| `$B5` | `$E4:2600` | sequence base, **2 bytes** per entry |
| `$B8` | `$E4:2800` | speed-table base, **2 bytes** per entry |

The sprite **descriptor** array is `$E4:F000`, 5-byte records. Records 0..6 are the seven PCs, which
is why the NPC loader indexes from `$E4:F023` = `$F000 + 7*5`. Enemies use `$E4:F600`, 10-byte
records (`C0/46DF`).

## `C0/4557` — the shared prologue

Not the descriptor loader, despite its name in earlier notes. It writes the defaults and stages the
sprite id into the hardware multiplier:

```
4557  LDX $6D / STA $1100,X          state: 4 = NPC (C0/4590), 5 = enemy (C0/46E1)
455C  INY / TYX / LDA $7F2001,X      the NEXT SCRIPT BYTE = the sprite id
4562  LDX $6D / STA $1101,X / STA $4202
456A  $1280,X = $71 (word)           the graphics staging address
4571  $0C00,X = $2020                both OBJ priority bytes
4579  $1200,X = $7F                  the graphics BANK
457E  $1600,X = 1                    facing
4583  $1680,X = 0 ; $1681,X = 0      anim modifier and phase
458D  INY / CLC / RTS
```

## `C0/4590` — the NPC loader

```
4590  LDA #04 / JSR C0/4557
4595  LDA #05 / STA $4203            record stride 5 -> $4216 = id*5
459A  STY $C7                        save the script IP
459C  $1B01,X = 1
45A3  LDX $4216 / STX $BF            $BF = the record offset

      field +1  ($E4F024,X)  -> *3 -> [$B2],Y -> $1380,X (word) and $1300,X (bank)
      field +2  ($E4F025,X)  -> *$18          -> $1400,X
      field +3  ($E4F026,X)  -> *2 = Y:
            $1480,X = ([$B5],Y+2  -  [$B5],Y) >> 2      <- DERIVED, not read
            $1500,X = [$B5],Y
            $1580,X = [$B8],Y
      field +4  ($E4F027,X)  -> $1201,X                  <- THE CLASS BYTE
      field +0  ($E4F023,X)  -> $E3, then JSR C0/5C90
```

`$1480` being a **difference of adjacent table entries divided by 4** is the kind of thing a port
would otherwise read straight from a table and get wrong.

## `C0/5C90` — the graphics dedup cache

`$0920` is a 16-entry table of sprite ids already loaded this scene. `C0/5C90` scans it for `$E3`:
a hit returns carry set with `X` = the slot, and the caller copies that object's `$1280` instead of
loading again (`4631 LDA $1280,X / STA $1280,X` across the two indices). A miss claims the first
`$FF` slot, stores the id, and returns carry clear — the caller then pushes a decompression request
(`[$AF],Y -> $0300`, `$71 -> $0303`) and the graphics land at `$7F:$71`.

## Why porting this is a CHAIN, not a routine

The loader alone does **not** fill everything `move.c` needs:

| still missing | who fills it | status |
|---|---|---|
| `$1700` (OBJ template base) | the OBJ slot allocator, `C0/6F9A` | **ported**, `--sweepslot` |
| `$0D00` (OAM tile base) | same | **ported**, `--sweepslot` |
| `$0A00` / `$0A80` (screen position) | the movement/camera pass | open |
| `$71` (graphics staging cursor) | advanced by the decompression the loader queues | open |

That last one is the sharp edge: `$1280` is just `$71` at load time, so without modelling the
decompression cursor every object would share one graphics address. So the honest order is

1. ~~the loader's WRAM-field half~~ — **done**, `CtObjLoad`, `--sweeploader` 180/180,
2. ~~the OBJ slot allocator (`$1700` / `$0D00`)~~ — **done**, below,
3. the `$71` cursor and the `$0300` decompression queue,
4. `$0A00`/`$0A80` from the movement pass,

and only then does calling `CtAnimTimerTick` / `CtAnimDrainPass` / `CtAnimRelease` from `field.c`
produce anything but garbage.

## Item 2 — the OBJ SLOT allocator

### `C0/6F9A` is a DISPATCH, not an allocator

```
6F9A  LDX $6D / LDA $1100,X / BMI ->6FA6      bit7 = do nothing
6FA1  ASL A / TAX / JSR ($6FA7,X)             <- an indirect jump table
6FA6  RTS
6FA7  B9 6F  B9 6F  B9 6F  BA 6F  BA 6F  B9 6F  B9 6F  B9 6F  B9 6F
6FB9  60                                      <- the byte the seven "B9 6F" entries point AT
```

Nine entries, and `$6FB9` holds a single `60` = **RTS**. Only states **3 and 4** allocate; the other
seven do nothing and leave the carry untouched. `CtObjSlotAlloc` returns `-1` for those — not `0` —
because "no verdict" and CLC are different answers and a caller must not confuse them.

The `ASL A` is **eight-bit**, so bit7 falls out: state `$83` would index entry 3 and allocate were it
not for the `BMI` at `6F9E`. That is why the port computes `(st << 1) & 0xFF` instead of range-checking
the state — a `state > 8` guard silently subsumes the branch and makes it ungradable. (It did; the
acceptance driver caught it.)

### Two scans, one pair of formulas

| | class 0 (`6FBA`) | class 1/2/3 (`7008`) |
|---|---|---|
| gate | `$1201,X & 3 == 0` | otherwise |
| slots | one | a **consecutive pair** |
| scan | `$19` **down** to `$01` | `$0C` **up** to `$16`, step 2 |
| full | `CLC`, nothing written | `CLC`, nothing written |

Slot **0 is never handed out**: the single scan ends on `BNE` at `X = 0`, so the last slot it tests is
`$01`. `$0B88` is the table — bit7 set = FREE, else the owning `$6D`, the same convention as the
`$0BC0` arena.

Both paths then run the *same* arithmetic (`6FD1`-`6FE6` and `701C`-`7031`, byte for byte):

```
$0D00,X = ((slot & $FE) << 4) + ((slot & 1) << 3)      a WORD
$1700,X = (slot & $FF) << 5
```

so a pair shares one 16-tile row, the odd half sitting 8 tiles into it.

### `C0/7056` — the release

```
7056  LDX $6D / LDA $1700,X / LSR x5 / TAX       recover the slot from the template base
7065  LDA $6D / CMP $0B88,X / BNE -> CLC RTS     not ours: nothing written
706E  CMP $0B89,X / BNE ->707D
7073  $0B89,X = $80 ; $0B88,X = $80 / SEC RTS    the PAIR
707D  $0B88,X = $80 / SEC RTS                    just the one
```

It never consults the class: it frees the pair **iff the neighbour is also ours** — the same shape as
the arena's `C0/EA42` run-release, and for the same reason (the class byte can change under an object).

### Grading

`--sweepslot`: micro-execute `C0/6F9A` over 11 states x 4 classes x 10 pool patterns = 440 cases, and
`C0/7056` over 8 template values x 4 ownership shapes = 32, comparing `$0B88[0..$1F]`, `$0D00`, `$1700`
and the carry. **440/440 and 32/32 byte-exact.** Acceptance: 27 mutations, all 27 red, no holes
(`scratchpad/accept_slot.py`).

Three of the 27 were holes first, and all three were **seeding**: no pattern isolated slot 0, so a port
that walked down to it looked identical; no pattern put a free pair at an **odd** start, so stepping by
one and by two always landed on the same slot; and the port's own range check had swallowed the `BMI`.
Patterns 8 and 9 and the eight-bit `ASL` closed them.

## Item 3 — the `$71` cursor and the `$0300` decompression queue

### The request record and the call

A dedup MISS falls to `C0/466B`, which builds a **6-byte request** and calls the shared decompressor:

```
466B  LDA $E3 / *3 / TAY                  the 3-byte packet-pointer table [$AF]
467A  LDA [$AF],Y  -> $0300               source, low word
46B2  LDA [$AF],Y+2 -> $0302              source, bank
467F  LDA $71      -> $0303               destination, low word = THE CURSOR
46B7  LDA #7F      -> $0305               destination, bank
46BC  JSL $C3:0002                        decompress
46C2  LDA $0306 / CLC / ADC $71 / STA $71  <- THE ONLY THING THAT MOVES $71
46CA  CMP #8000 / BPL ->46D7
46CF  LDX #7C1F / BRL C0/2E1E             HALT
46D7  INC $73 / LDX $C7 / SEC / RTS
```

`$C3:0002` is the "Evil Peer" LZ, already ported as `ct_lz_decompress`. It writes the sheet to
`$7F:$71` and **reports the byte count in `$0306`** — that report is the entire cursor mechanism.

### The heap is `$7F:8000`–`$FFFF`, and below it the machine HALTS

`C0/2E1E` is `SEI` / force blank / NMI and DMA off — a diagnostic halt, the same one `C0/5C90`'s
full-table arm reaches with `LDX #4010`. So `CMP #$8000 / BPL` is not a range check with a graceful
arm: **below `$8000` the game stops.** Measured live rather than argued:

| state | `$71` |
|---|---|
| `DEMO8_gtu` | `$B860` |
| `bedslide` | `$9000` |
| `S5_gtu` | `$FE20` |

All three in `[$8000, $FFFF]`. The cursor only ever moves up, and a wrap past `$FFFF` is the halt.
That arm is therefore **not gradable by micro-execution** — the ROM never returns from it.

### `$73` and the three party slots

Both arms — hit (`4639`) and miss (`4686`) — run the same three-way store of `$71` and then `INC $73`:

```
LDA $73 / CMP #01 / BMI -> $7E29C8 / BEQ -> $7E29D4 / else -> $7E29E0
```

`CMP` + `BMI` is a **signed** test, so `$73 = $81` takes the `$29C8` arm exactly as `$73 = 0` does.
Writing it as `p == 0` matches the ROM for every value the engine produces and is still wrong.

Note the hit arm stores `$71` — the *next free* address — not the owner's `$1280`, which is where the
sprite's graphics actually are. That reads as a ROM slip; it goes in verbatim, because what those
slots hold is what the rest of the engine reads.

### Grading

`--sweeploader` now runs both arms to the **real RTS**, so the oracle executes `JSL C3/0002` for
real and the compare includes the whole `$7F:8000-$FFFF` heap byte for byte — which grades
`ct_lz_decompress` against the ROM's own decompressor, not just its size report. 10 sprite ids x 3
cursors x 3 cache shapes x 4 `$73` values x 2 IPs = **720/720 byte-exact**. Acceptance: 35 mutations,
all red, no holes.

**Two of the 35 were real defects in the port**, not seeding. `$E3` was written to `$00E3` when DP is
`$0100` (so `$01E3`) — invisible until `$E3` entered the compare set. And the mutated packet pointers
**segfaulted**: `ct_lz_decompress` is unbounded on both buffers, and a pointer that is not a graphics
packet drives its cursors wherever the bytes say. Fixed with `ct_lz_decompress_bounded`, which returns
identical output for every in-bounds stream (full suite 156/0/0 confirms `battle.c` and `ct_vm.c` are
unmoved). The third was the signed `BMI` above.

## Item 4 — `$0A00` / `$0A80`, the screen position

`C0/AB45`, called per object from the movement pass (`C0/A838`, `C0/A87E` — both inside `C0/A810`)
and from `C0/B101`. Nine instructions of arithmetic:

```
AB45  REP #20 / LDX $6D
AB49  $D9 = $1D87 & 7                      the camera's sub-tile PHASE
AB51  $DB = $1D89 & 7
AB59  $DD = (($1D0A & $FF) << 3) + $D9     camera X
AB67  $DF = (($1D0E & $FF) << 3) + $DB     camera Y
AB75  A = ($1800,X >> 4) - $DD  ;  BCC -> ORA #0100
AB84  $0A00,X = A & $01FF
AB8A  A = ($1880,X >> 4) - $DF  ;  BCC -> ORA #0100
AB99  $0A80,X = A & $01FF
AB9F  SEP #20 / RTS
```

Two things matter beyond the arithmetic.

**The camera is base·8 + phase**, the same shape `field_camera_unification.md` records — but off
`$1D87`/`$1D89`, not the `$1D93`/`$1D96` pair the commit uses. And only the **low byte** of
`$1D0A`/`$1D0E` survives (`AND #00FF`).

**Bit 8 is forced on a borrow, and it is not decoration.** It is exactly the `>= $100` arm the six
`C0/B701` OAM emitters' Y clamps branch on (see `anim_drain_cad9.md` §11), so an object left of or
above the camera is clamped as off-screen instead of wrapping around. That single `ORA #0100` is the
link between this routine and the animation tree, and it is why item 4 had to be closed before the
tree could be called for real.

### Grading

`--sweepscreen`: 5 phases x 4 cameras x 6 x-positions x 6 y-positions = **720/720 byte-exact**,
comparing `$0A00`/`$0A80` and the four DP scratch words the routine leaves behind. Acceptance: 22
mutations, all red, no holes.

Two seeding details the driver forced. The X and Y inputs are drawn from **different indices** into
the same arrays — driving both from one index makes `camX == camY` and `wx == wy` in every case, and
a port that computed Y from the X inputs would be byte-identical to the ROM. And the world seeds
have to produce a bit8 that is set **without** a borrow (`$1F00 >> 4 = $1F0` against a small camera),
or `ORA #0100` and `AND #01FF` cannot be told apart — the ORA would look like it was doing the AND's
work.

## The chain is closed

All four items are ported and graded:

| item | routine | check | cases |
|---|---|---|---|
| 1 the loader's WRAM fields | `C0/4557` + `C0/4590` | `--sweeploader` | 720 |
| 2 the OBJ slot allocator | `C0/6F9A`, `C0/7056` | `--sweepslot` | 440 + 32 |
| 3 the `$71` cursor + decompression | `C0/466B`, `JSL C3/0002` | `--sweeploader` | (same 720) |
| 4 the screen position | `C0/AB45` | `--sweepscreen` | 720 |

What remains before `field.c` can call `CtAnimTimerTick` / `CtAnimDrainPass` / `CtAnimRelease` is no
longer a derivation problem but a **wiring** one: the calls must go in the ROM's frame order
(`C0/00D3` movement → `C0/00D6` drain → `C0/00DB` draw), and the last attempt to wire the drain
per-object-per-frame collapsed `--portrun` from 1592 to 156 because the FIFO discipline was not
respected. See `anim_drain_cad9.md`.

## Grading path (item 1)

The same micro-execution the rest of the family uses: seed a script byte at `$7F2001+Y`, the four
base pointers, `$71`/`$73`, run `C0/4590` to its RTS and compare `$1100`/`$1101`/`$1200`/`$1280`/
`$1300`/`$1380`/`$1400`/`$1480`/`$1500`/`$1580`/`$1201`/`$1600`/`$1680`/`$1681`/`$1B01`/`$0C00`/
`$0920`. Every input is a table or a byte the sweep can drive.
