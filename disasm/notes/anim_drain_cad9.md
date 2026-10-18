# `C0/CAD9` — the animation-queue drain body, and the four-slot arena

Derived 2026-07-30. Companion to `field_tick.md`'s "Pass C" section and to
`disasm/notes/objpal_allocator.md` (the arena here is a *different* allocator with the same shape —
do not confuse `$0BC0` with `$0B00`/`$0B80`).

**ALL FOUR SPRITE CLASSES ARE PORTED AND GRADED, COMPLETE** — frame halves (§4 class 0, §8 class 1
`C0/CEF5`, §9 class 2 `C0/D4F7`), tile halves (§10, §12) and the pixel copy (§11); §7 adds the
release `C0/B701` and the FIFO order `C0/CA76`, §3 the four-slot arena `E952`–`EA62`.
`--sweepcad9` excludes **nothing**: 165888/165888 over every class and all nine staging legs, plus
567/567 arena states. `src/move.c`.

**THE ENTIRE TREE IS PORTED AND GRADED, WITH NOTHING EXCEPTED.** The six class-0/1/2 OAM emitters
land in §13 and §14; class 3’s `C0/C73A` — the last one, and the one whose carry this file twice
called underivable — lands in §15.

⚠ **AND IT IS UNREACHABLE FROM THE LIVE ENGINE.** `move.c` links into `Belthasar` but nothing calls
it: the tables it reads (`$1300/$1380/$1700/$0D00/$0A00/$0A80/$1480/$1500`) are filled by the
ROM’s OBJECT LOADER, which is not ported. See [`object_loader.md`](object_loader.md) — derived,
with the chain that porting it entails.

---

## 1. Where it sits

`C0/CA76` is the field's animation pass (`C0/00D6`). It drains a singly-linked FIFO of objects
waiting on the sprite assembler — `$76` head, `$77` tail, `$1081,X` next-pointer, any value `>= $80`
is the null — and `C0/C98A` is what pushes onto it (see `field_tick.md`).

Two gates decide whether the drain runs at all, and both matter:

```
CA76  LDA $09A0 / BEQ ->CA7C / RTS      the WHOLE pass is skipped while the upload queue is non-empty
CA85..CA9B                              per-iteration raster budget ($213D scanline vs $6B)
```

Each iteration pops the head into `$6D` and `JSR $CAD9`. **The carry is the protocol**: `SEC` = keep
this object at the head and call me again, `CLC` = I am finished, pop it.

---

## 2. `C0/CAD9` — the visit

Four per-object gate tests, then a tail-call.

| test | ROM | reject when |
|---|---|---|
| liveness | `CADB LDA $1100,X / CADE BPL ->CAE2` | **bit7 SET** (destroyed/inactive) |
| assembly enable | `CAE2 LDA $1A81,X / CAE5 BEQ / CAE7 BMI` | `== 0` or bit7 set (so `$01`–`$7F` pass) |
| camera window | `CAE9 LDA $0F00,X / CAEC BEQ` | `== 0` |
| class | `CAEE LDA $1201,X / CAF1 AND #03` | `== 3` |

**Every reject returns CLC** (`CAE0`/`CAFF` are both `18 60`). That is mandatory, not incidental: CLC
is what makes `CA76` pop the object. A reject returning SEC would jam it at the head forever.

`$1201,X & 3` is the sprite's **PIECE COUNT**, and the whole class-specific machinery follows from it:

| class | pieces | frame-record stride | staging slots | handler |
|---|---|---|---|---|
| 0 | 4 | `$28` | 1 | `C0/CBDC` |
| 1 | 8 | `$50` | 2 (consecutive) | `C0/CEF5` |
| 2 | 12 | `$78` | 3 (consecutive) | `C0/D4F7` |
| 3 | — | — | — | rejected here; `C0/E12A` handles it outside the FIFO |

### The twin at `C0/CB0A` is a different job

`CB0A` runs the same four tests and dispatches to `CDC8` / `D124` / `DD28`. It is **never `JSR`'d
anywhere in the ROM** — only two `BRL` tail-calls reach it, from `C0/086E` and `C0/0902` — and
nobody reads its carry, which is why its three gate rejects were shortened to a bare `RTS` (`CB11`)
while its class-3 default kept the `CLC` (`CB2F`). Its handlers re-derive the OBJ records for the
object's **current** `$0F01,X` straight into the LIVE buffer `$7F4BC0 + $1700,X` and stamp
`$1B00,X = $80`; they select no frame, allocate nothing and upload nothing.

> An early reading called the two families "disjoint products — tile staging vs OAM records". That
> is **wrong**: `CBDC` also builds the identical OAM record set, into `$7F4802 + $1700,X`. The real
> difference is that `CBDC` additionally resolves/commits the frame and uploads tiles, and that it
> `INC`s `$1B00,X` where `CDC8` stamps `$80`.

---

## 3. `C0/E952`–`EA62` — the four-slot arena

`$0BC0..$0BC3` is a **byte-strided** owner table: bit7 set = free (every release writes `$80`), else
the owning object's `$6D` (= `obj*2`, at most `$7E`, so never ambiguous). A grant writes the base
into `$0D80,X` as a **word**: `$3800 + slot*$200` — an offset into the `$7F` staging bank, 512 bytes
= 16 SNES 4bpp tiles per slot. Only `{3800, 3A00, 3C00, 3E00}` are producible.

| routine | what | scan bound |
|---|---|---|
| `E952` / `E97A` / `E9AA` | claim 1 / 2 / 3 **consecutive** slots; SEC = granted, CLC = pool full and **nothing is written** | 4 / 3 / 2 |
| `E9E2` / `E9FF` / `EA1F` | release 1 / 2 / 3 slots at the first slot owned by `$6D` | 4 / 3 / 2 |
| `EA42` | release the whole **consecutive run** owned by `$6D` (1..4) | 4 |

Three things a port gets wrong if it skims:

- **`EA42` is not `free(…,1)`.** It is the class-agnostic release used by the camera-window falling
  edge (`C0/A918`) and the scene-entry sweep (`C0/B117`); releasing a class-2 object through the
  1-slot path would leak two slots.
- **The n-slot frees write the followers unconditionally.** Once `E9FF`/`EA1F` find slot `k` owned
  by `$6D` they stamp `k..k+n-1` without re-checking ownership. Ported verbatim.
- **The scan bounds are `4-(n-1)`, not 4.** `E97A` stops at `k=2`, `E9AA` at `k=1`.

### The pool is NOT frame-scoped

Live monitoring of `party3_field` shows `$0BC0 = 80 80 80 80` at **every** frame boundary, and three
different objects carrying `$3800` in `$0D80` — slot 0 reused within one drain. It is tempting to
conclude the pool resets per frame. It does not, and clearing it would be invention. Two ROM
mechanisms strand a partly-staged object's slots into the next frame: `CA76`'s `$09A0` gate skips
the entire pass, and its raster budget can cut *between* the passes of a class-1/2 object — in which
case `C0/B701`'s release gate sees `$1B00,X` below the class arity and returns SEC without freeing.

`$0BC0..$0BC3` is also **aliased** by `C0/71B0 STA $0B80,X` (the palette allocator's share branch,
where X is the *occupant's* `obj*2` and can reach `$0BFE`). Back it with the flat `$7E` array, not a
private `uint8_t[4]`.

---

## 4. `C0/CBDC` — class 0, the `$0F01` commit

```
CBF2  $1780,X == 3 ?  ->  A = $1301,X        (static pose; folded into CtFieldFrameLookup)
CBFE                  ->  JSR C0/CB3A ; CC01 BCS ->CC04 ; CC03 RTS  (carry clear = no frame)
CC04  CMP $0F01,X / BNE ->CC0B               the EQUALITY GUARD: unchanged -> CC09 CLC/RTS,
                                             and no slot is claimed
CC0B  STA $EE / CC0D JSR C0/E952 / CC10 BCC ->CC09     pool full -> the frame is DROPPED
CC12  LDA $EE / CC14 STA $0F01,X             THE COMMIT
CC17..CDBE                                   the tile half (not ported, §6)
CDBF  LDX $6D / CDC1 INC $1B00,X / CDC4 SEP #10 / CDC6 CLC / CDC7 RTS
```

**Class 0 never returns SEC.** Its only three RTS are `CC03`, `CC09` and `CDC7`, all carry-clear; a
raw scan of `CBDC..CDC7` finds no `38 60`. So the drain pops a class-0 object after exactly **one**
visit. The resumable SEC protocol is real but belongs to classes 1 and 2.

> The first derivation asserted the opposite — that the drain "keeps re-entering the object exactly
> while CB3A/E952 keep producing work" — and three independent verifiers refuted it.

**`CDC1`'s index is the WORD `$6D/$6E`**, not the byte: `REP #10` at `CCBD`, and the only `SEP #10`
in `CCB0..CDC7` is at `CDC4`, i.e. *after* it. Confirmed by direct byte inspection.

`$1B00,X` is the staged-pass counter the whole handshake rides on — `STZ` at the two rising edges
(`C0/A8FD`, `C0/B180`), `INC` per staged slot (`CDC1`; `D11A`+`D11D` for class-1 direct, `+2` in one
visit; `D308`, `D4F0`; `D603`, `D686`, `D733`, `D7E0`, `DA62`, `DD21`), and `$80` after each release.

---

## 5. Two semantic corrections worth carrying

- **Tile-word bit14 is HORIZONTAL FLIP, not a "compressed format" selector.** Both decoders do
  `AND #07FF` and differ only in that `C0/E534` passes every byte through `$C0:FD00`, which is an
  exact **bit-reversal** table (`ROM[$C0FD00+i] == bitreverse8(i)` for all 256 `i`, verified
  exhaustively). Reversing each byte of a 4bpp tile mirrors each pixel row. **H-flip is baked into
  VRAM**, which is why the attribute byte built at `CD47`–`CDBB` carries no flip bits.
- **`$1700,X = slot << 5`, not `<< 3`** — five `ASL A` at `C0/47D2`–`47D6` (and `C0/6FF4`). Class 0
  writes 4 groups × 8 bytes = 32 bytes, so the stride must be 32. At `<< 3` every object's records
  would overlap the next three slots.
- **`$0C00`/`$0C01` are a full 2-bit OAM priority field** (`0x00/0x10/0x20/0x30`), with two writer
  arms split by `$0F80,Y` bit7 (`C0/AEE3 BPL ->AF04`). The `{0x20,0x30}`-only reading describes the
  bit7-SET arm alone.

---

## 6. What is NOT ported, and what it costs

**Class 0's tile half, `CC17`–`CDBE`.** The 16-tile blit through the `$2180` WRAM port into
`$7F:3800+`, the 7-table VRAM upload queue (`$0940` srcA / `$0950` dstA / `$0960` srcB / `$0970`
dstB / `$0980` lenA / `$0990` lenB / `$09A0` used, 8 entries, 2-byte cursor `$79`), and the 4-entry
OBJ template at `$7F4802 + $1700,X`.

**Classes 1 and 2.** Mapped in full but **not adversarially verified** and not ported;
`CtAnimDrainVisit` returns `-1` for them rather than guessing.

- **Class 1, `C0/CEF5`** (8 pieces, `$50` record, 2 slots). Non-static work is split across **two**
  visits selected by `$1B00,X & 0x7F`: `C0/D28A` allocates via `E97A`, commits, stages the first 16
  tiles, `INC`, returns **SEC**; `C0/D30D` stages the other 16 at `$0D80,X + $200`, `INC`, returns
  CLC. The `$1780,X == 3` path (`C0/CF21`, reached by `CF10 BNE ->CF14 / CF12 BRA`) does **not**
  split — it never reads `$1B00,X`, blits 32 tiles (`CF6B LDA #0020`) in one visit and does
  `$1B00,X += 2`.
- **Class 2, `C0/D4F7`** (12 pieces, `$78` record, 3 slots). Three visits (SEC, SEC, CLC) sequenced
  by `$1B00,X & 0x7F`, and everything is duplicated into two parallel implementations selected by
  `low($0D00,X) == 0x68`. ~2 KB. Note `D50D`–`D514` returns a **bare RTS with carry SET** when
  `$1780,X == 3` — an object that would be re-dispatched until the raster budget expires.

**`C0/B701`, the release gate** (draw pass, `C0/00DB`). Corrected per class — and the correction
matters, because the below-threshold case with bit7 clear returns **SEC**, not the re-emit path:

```
n = $1B00,X
class 0: n == 0 -> SEC ; n == 0x80 -> B788, CLC ; else -> B8CA, free1, n := 0x80, CLC
class 1: n == 0 -> SEC ; bit7 set && (n&0x7F) < 2 -> BA65, CLC ; bit7 clear && n < 2 -> SEC ;
                                                    else -> BCDC, free2, n := 0x80, CLC
class 2: same with threshold 3, emitters BFF2 / C2BF, free3
class 3: BRL C0/C73A -- no free
```

### The harness lesson

Wiring `CtAnimDrainVisit` into `--portrun`'s per-object loop was tried and **measured**:
`1592/1592 -> 156/1592`. `C0/CAD9` is not a per-object per-frame function — it is FIFO-driven, and
visits an object only when `C0/C98A` enqueued it. Called speculatively for every object every frame,
every object claims an arena slot; the release lives in `B701`, which is unported, so from the fifth
claim on `E952` refuses and `CC10`'s `BCC` drops the frame. **The port was right and the harness
around it was wrong.** Closing it needs the FIFO order and `B701`'s release, not a different call
site — which is also the shape of what `$0F01` proper still needs.

---

## 7. The other two ends: `C0/B701` (release) and `C0/CA76` (order) — ported 2026-07-30

`--sweepdrain`, 192/192 release states + 2088/2088 clock-independent drains. This closes the arena's
lifecycle, which is a loop inside **one** frame:

```
00D3 JSR C0/A810   movement  -> C0/C98A per object   = ENQUEUE
00D6 JSR C0/CA76   the DRAIN -> C0/CAD9 per pop      = the E952 GRANT
00DB BRL C0/B271   the DRAW  -> C0/B309 -> C0/B701   = the FREE
```

`B701`'s three `JSR E9E2/E9FF/EA1F` are the **only** callers of the 1/2/3-slot frees in the ROM.

### `C0/B701` is one law, not three arms

`B716`/`B737`/`B75E` are the same instruction sequence differing only in a threshold and two
addresses. With `T = (class & 3) + 1` = the arena arity and `n = $1B00,X`:

```
if ((n & 0x7F) < T) { n & 0x80 ? (reposition-emit, CLC) : SEC }
else                { instantiate-emit, free T slots, $1B00,X = 0x80, CLC }
```

The carry is the **draw gate** — its only caller `C0/B309` does `JSR B701 / BCC -> draw / RTS`. bit7
means "already released": the reposition emitters re-derive the on-screen OAM X/Y from templates
already in the live buffer, while the instantiate ones first copy the staged template from
`$7F:4802 + $1700,X` into `$7F:4BC0 + $1700,X` — which is precisely why the slot may be freed at
that instant. Class 3 tail-calls `C0/C73A`, whose carry is **not explicit**: `C73A` has no `SEC/RTS`
or `CLC/RTS` anywhere, only a bare `RTS` at `C7FA`, so it is whatever `C7EF`'s `ADC #0008` left.
Measured rather than asserted.

**The six emitters are not ported, and that is safe rather than convenient**: a scan of all seven
(`B788`/`B8CA`/`BA65`/`BCDC`/`BFF2`/`C2BF`/`C73A`, 4216 bytes) finds **zero** reads and **zero**
writes of `$0BC0..$0BC3`, `$0D80`, `$1B00`, `$0F01` or `$0F00` in any addressing mode, and no call
into the arena or the frame lookup. What is missing is the drawing, not the bookkeeping.

### `C0/CA76` — the order, and the boundary of what a cycle-free port can model

Deterministic and ported: the `$09A0` prologue gate (the whole pass is skipped while the VRAM upload
queue holds work), `CA80 STZ $79`, the `$6D` latch, the visit order, and the two pop shapes —
mid-list (`$76 = $1081,head`) vs `head == tail` (both ends nulled).

Not portable: **the deadline's clock** (2026-09-27: it is the zelda3 model's virtual beam now, sec. 16). `CA88`–`CA91` assembles the 9-bit V counter from two `$213D`
reads; `CA94 CMP #00F0 / BPL` is signed (>= 240 = vblank = no limit) and `CA99 CMP $6B / BCS` is
unsigned against a **word** (`$6B/$6C`, measured `0x0078` everywhere; an event-VM op at `C0/678E`
can set it from a script operand). The port models the **test** exactly and cannot model the
**clock**. Its cost, measured over 90 frames — frames whose queue is still non-empty at the frame
boundary: 0/90 (bedslide, S5_gts5), 1/90 (party3_field, candy_drive, base5_ag1), **24/90**
(DEMO8_gtu). On a busy scene the real deadline bites on roughly one frame in four.

### Two setup defects the sweep found in its own earlier revisions

Both had made it green for the wrong reason.

1. **`DB` was `$7E`.** The field engine runs with `DB = $00`, where bank 0 mirrors the first 8 KB of
   WRAM *and* maps the PPU registers. With `$7E` the drain's `LDA $2137` read WRAM instead of
   latching the counter, so `vCount` stayed 0 and the deadline **never fired** — measured
   `vPos=120 vCount=0`, then `A=0078 C=1` once fixed. The other three sweeps in this family were
   changed to `DB = $00` too and are unmoved: every address they compare is below `$2000`, where the
   two banks coincide.
2. **The queue held class-0 objects described as "real sprites of this state".** There is no state —
   this family's oracle is a fresh boot, so `$1200`/`$1280`/`$1300`/`$1380` are zero and `CBDC`'s
   tile blit dereferences a null 24-bit pointer (`$CF = 00`, `$CD = 0000`) and segfaults. The queue
   is now built from objects that reach CLC **without entering a handler**.

And the **clock runs** during micro-execution — `cpu_runOpcode`'s callbacks drive `snes_runCycles` —
which I had assumed frozen. A case is graded only when clock-independent; the other 504 are counted
and reported. The unreachable budget `0x0180` in the sweep is also what makes `CA99`'s word compare
observable: with only zero-high-byte budgets, a byte read of `$6B` goes unnoticed.

### One thing that is derived but NOT verified

The **SEC branch** — "keep the head, do not pop". The only `CAD9` paths returning SEC are the
class-1/2 multi-pass handlers, which `CtAnimDrainVisit` refuses with `-1`, so every object the sweep
can legally queue returns CLC and deleting the branch changes nothing observable. Porting `C0/CEF5`
or `C0/D4F7` makes it gradable.

---

## 8. `C0/CEF5` — class 1, ported 2026-07-31

`--sweepcad9`, now 80640/80640 (the sweep gained `$1B00,X` as a swept input, which is what class 1's
pass select reads). Class 2 remains unported: 2304 refusals.

### It is not class 0 with bigger numbers — the dispatch is three-way

```
CEF5  LDA $1200,X / STA $CF ; LDA #7F / STA $D2 ; LDA $1280,X / STA $CD ; LDA #E4 / STA $D8
CF0B  LDA $1780,X / CMP #03 / BNE ->CF14 / BRA ->CF21
CF14  LDA $1B00,X / AND #7F / BEQ ->CF1E / BRL ->D30D
CF1E  BRL ->D28A
```

| path | what it does | `$1B00,X` | carry |
|---|---|---|---|
| `CF21` (`$1780,X == 3`) | **single visit**, stages the whole two-slot run (32 tiles, `CF6B LDA #0020`); never reads `$1B00,X` | `+= 2` (`D11A`+`D11D`) | CLC (`D122`) |
| `D28A` (`($1B00,X & $7F) == 0`) | **pass 1**: `JSR CB3A`, equality guard, claim **two consecutive** slots (`C0/E97A`), commit `$0F01,X`, stage 16 tiles | `+= 1` (`D308`) | **SEC** (`D30B`) |
| `D30D` (otherwise) | **pass 2**: no lookup, no guard, no allocation, **no commit** — only stages the other 16 tiles at `$0D80,X + $200` | `+= 1` (`D4F0`) | CLC (`D4F5`) |

So the entire frame decision lives in pass 1 and the mode-3 path, which is why porting the frame
half alone still gets the arena and queue behaviour right. `D30B` is also **the first ported path
that returns SEC** — the ROM asking `C0/CA76` to keep the object at the FIFO head.

### The three `INC $1B00,X` disagree on index width

A ROM fact, and not a tidy one:

- `D308` (pass 1) runs after `D304 SEP #30` → indexed by the **byte** `$6D`.
- `D4F0` (pass 2) and `D11A`/`D11D` (mode 3) run while X is still 16-bit (`D3B4`/`CFDE REP #10`,
  with their `SEP #10` at `D4F3`/`D120` — i.e. *after*) → indexed by the **word** `$6D/$6E`.

Mutating that one asymmetry alone moves `--sweepcad9` by 288.

### A decode trap worth recording

A raw scan for `60` (RTS) in the pass bodies reports exits at `CFC1` and `D399` that **do not
exist**: both are the operand byte of `STA $0960,X` (`9D 60 09`), the same false positive as `CCA1`
in class 0. Pass 2 has **no early exit**. `dis.py` desyncs in that stretch, so it was read from raw
bytes.

### Acceptance

10 single-fact mutations, **all 10** turn `--sweepcad9` red: the mode-3 select (576), the `AND #7F`
pass select (384), pass 2 not committing (768), `E97A` claiming two slots (960), the equality guard
(192), the `+2` in mode 3 (384), pass 1's SEC (576), `D308`'s byte index (288), mode 3's CLC (384),
and dispatching class 1 at all (1728).

### What this did NOT close

The drain's **SEC branch** in `CtAnimDrainPass` is still derived-not-verified, and the reason
changed rather than disappeared. There is now a ported path that returns SEC, but queueing a
class-1 object in `--sweepdrain` **segfaults the oracle**: a fresh-boot state leaves the sprite
pointer tables zero, and `C0/E687`'s bank dispatch falls through to the `C0/E6B6` copier, whose
`STA $000n,Y` writes bank 0 at an arbitrary `Y` — straight into the DMA registers. Seeding a real
ROM bank (`$D2`, which selects the `C0/E7BC` copier) did not help either. `--sweepcad9`
micro-executes the same handler safely because it runs ONE visit on ONE object, where the drain runs
three back-to-back over accumulated state. **The SEC return is verified; the loop's response to it
is not.**

---

## 9. `C0/D4F7` — class 2, ported 2026-07-31. All four classes are now graded.

`--sweepcad9`, 165888/165888 with **zero** unported. Frame half only, like classes 0 and 1.

### `D50D`–`D514` is a trap, and it is canonical

```
D50D  LDA $1780,X / CMP #03 / BNE ->D515 / RTS        BD 80 17 C9 03 D0 01 60
```

`CMP` with `A == 3` leaves the **carry set**, so a class-2 object in anim mode 3 returns **SEC having
done nothing at all**. `C0/CA76` reads SEC as "keep this object at the FIFO head and call me again",
so the ROM spins on it until the raster deadline expires.

That is why `CtAnimDrainPass`'s 64-visit guard is no longer merely a malformed-list backstop: the
ROM's own terminator here is the clock, which a clockless port does not have. `-2` is the port
saying so rather than inventing an exit the ROM does not take.

### Six legs, two of which matter

`$1B00,X & $7F` picks the pass (`D515`–`D51E`), `$0D00,X == $68` picks one of two staging
geometries (`D520`/`D52D`/`D539`):

| pass | non-`$68` | `$68` | `INC` | carry |
|---|---|---|---|---|
| 0 | `D546` | `D608` | `D603` / `D686` | **SEC** |
| 1 | `D68B` | `D738` | `D733` / `D7E0` | **SEC** |
| 2 | `D7E5` | `DA69` | `DA62` / `DD21` | CLC |

The two phase-0 bodies are **byte-identical** through the entire frame half
(`[25 ROM bytes at 0xD546: python tools/dis.py 0xD546 25] …`), so the `$68` split
never touches the frame decision — it only changes the staging offset (`$0100`/`$0200` for phase 1,
`$0400`/`$0300` for phase 2). `JSR CB3A` appears only at `D546`/`D608` and `JSR E9AA` only at
`D555`/`D617`, so the lookup, the guard, the allocation and the commit **all live in phase 0**;
phases 1 and 2 are pure staging with no early exit (their bodies contain no `RTS` at all — the stray
`60` bytes are operands of `STA $0960,X` and `LDY #$0060`).

### The index-width asymmetry is a rule, not an accident

Across all three multi-pass classes: every **SEC** (intermediate) tail runs after a `SEP #30` and is
**byte**-indexed (`D603`/`D686`/`D733`/`D7E0`, and class 1's `D308`); every **CLC** (final) tail runs
with X still 16-bit and is **word**-indexed (`DA62`/`DD21`, class 1's `D4F0` and `D11A`/`D11D`, and
class 0's `CDC1`).

### Acceptance — and a real hole in the check, found first

13 single-fact mutations, **all 13** turn `--sweepcad9` red. But one of them did not, at first:
deleting class 2's equality guard changed nothing, because `D514` sends mode 3 out with SEC *before*
the guard, so the only `$0F01` value that could have matched (`$1301` = `0x14`) never reached it.
Adding `0x10` to the swept `$0F01` values — what `C0/CB3A` actually returns here, since a fresh-boot
oracle leaves `$1480`/`$1500`/`$1580` zero and the lookup lands on `$E4:0001` — made it visible
(192). Class 1's guard had been graded all along, via its mode-3 path `CF21`.

The class-0 and class-1 acceptance drivers were re-run against the widened sweep and both still
report zero holes — after four of their anchors had to be made unique, because classes 1 and 2
introduced textually identical lines and **an ambiguous anchor silently skips a mutation**.

---

## 10. The TILE HALF — class 0 ported 2026-07-31, minus the pixel copy

`C0/CC17`–`CDBE` is four separable jobs, and three of them are now ported and compared by
`--sweepcad9` (165888/165888).

### `CC17`–`CC3B` — the frame RECORD pointer

On the SNES **hardware multiplier**: `$4202` = the frame `$0F01,X`, `$4203` = the class stride
(`$28`), product read from `$4216`, plus `$1380,X`; the bank is `$1300,X`. This is where the
per-class stride is actually applied.

### `CC3F`–`CC76` — the 16-tile PIXEL COPY. **NOT PORTED.**

`$2183` = 1 (bank `$7F`), `$2181` = the arena base, then per tile: read the word `[$D3],Y`, and
`BIT #4000` selects `C0/E687` (verbatim 32-byte copy) or `C0/E534` (every byte reversed through the
`$C0:FD00` bit-reversal table = **horizontal flip baked into VRAM**). Its output lands in
`$7F:3800+`, which is deliberately **outside** the compare. Nothing else in the handler depends on
it. Cost, measured: `E534` is 339 bytes, `E687` 715 (a five-arm bank dispatch — `$7F`→`E8C2`,
`$D2`→`E7BC`, `$D3`→`E83F`, `$D4`→`E739`, else the `E6B6` copier), plus a WRAM-port model.

### `CC78`–`CCB4` — the upload-queue append

Seven parallel 16-bit tables, cursor `$79` advancing by 2:

| table | value |
|---|---|
| `$0940` srcA | the arena base `$0D80,X` |
| `$0950` dstA | `($0D00,X & $1FF) << 4` |
| `$0960` srcB | srcA + `$100` |
| `$0970` dstB | dstA + `$100` |
| `$0980` / `$0990` | `$0100` each |
| `$09A0` | **`INC`, 16-bit**; then a 16-bit `STZ` terminates the NEXT slot |

That `INC` and `STZ` being 16-bit while `C0/CA76`'s prologue reads `$09A0` **8-bit** is exactly why
`$09A1` accumulates stale values.

### `CCB6`–`CDBE` — the OBJ template at `$7F4802 + $1700,X`

Only five of each entry's eight bytes are written here (`+0`/`+1`, the final on-screen X, and `+5`
are left alone):

- `+2`/`+3` — the record's X displacement at `rec[$20 + e*2]`, **sign-extended**
- `+4` — its Y displacement at `rec[$21 + e*2]`
- `+6` — `($0D00,Y & $FF) + e*2`
- `+7` — `$0F81,Y | $0D01,Y | ($0C01,Y` when **that entry's own `+4` byte** is `>= $E8`, else
  `$0C00,Y)`

### Classes 1 and 2 are counted, not compared

They append to the queue **once per pass** with per-pass lengths (measured: `C0/D394 ADC #0200`
inside class 1's pass 2), so a shared helper with different constants would be a guess. 82944 cases
carry their own number; porting either moves it.

### Acceptance — four holes, all of them in the sweep's seeding

17 more single-fact mutations, **all 17** red. Four were not, at first, and none was a port defect:

- With `$09A0` starting at **zero**, a 16-bit `INC` is indistinguishable from an 8-bit one, and the
  16-bit terminator `STZ` writes zeros over zeros. Fixed by seeding `$00FF` plus a poisoned next
  slot.
- With a **fixed** record base the sweep never reached a record whose X displacement was negative or
  whose Y displacement was `>= $E8`, so the sign extension and the priority split were both
  unreachable. Fixed by varying `$1380` across the swept axes.

The 16-bit `INC` took two attempts: the first fix was silently overwritten by an **older seeding
block further down** that re-zeroed `$79` and `$09A0`. It was found by probing the live values, not
by re-reasoning — the second time in this file that a green line came from a stale seed rather than
from correct code.

All four earlier acceptance drivers were re-run against the widened sweep and still report zero
holes: **71 mutations across five drivers**, with one known exception — `--sweepdrain`'s SEC branch,
stated in `src/move.c`.

---

## 11. The PIXEL COPY — ported 2026-07-31. Class 0's tile half is complete.

`--sweepcad9` now compares the 2 KB staging pool `$7F:3800`–`$3FFF` byte for byte, and it is exact.

### Why it ports as one loop despite two different write paths

Per tile the handler reads a word from the frame record. Bits 0–10 are the tile index, the source is
`$CF:($CD + index*32)`, and **bit14 selects the copier**. Bits 11–13 and 15 are masked off by both
(`AND #07FF`) and are provably ignored.

- **`C0/E534`** (bit14 **set**) pushes 32 bytes through the `$2180` WRAM port, which auto-increments.
- **`C0/E687`** (bit14 clear) sets `DB = $7F` and stores **directly** to `$0000,Y` with `Y = $D0`,
  then **resyncs the port** with `TYA / CLC / ADC #0020 / STA $2181` (`E72E`–`E735`) so a following
  `E534` continues from the right place.

The caller keeps `$D0` in step by adding `$20` per tile (`CC55`), so both land tile *t* at
`$7F:($0D80,X + t*$20)` — which is what collapses the whole thing into one loop.

### `E534` is the horizontal flip

Every byte goes through `$C0:FD00`, an exact bit-reversal table. A 4bpp SNES tile stores one pixel
row per byte per bitplane and the byte **order** is preserved, so reversing each byte mirrors each
row. H-flip is baked into VRAM — which is why the OBJ attribute built at `CD47`–`CDBB` carries no
flip bits.

### `E687`'s bank dispatch is NOT a pass-through

This is the fact most likely to be got wrong. Only `$7F` (`E8C2`), `$D2` (`E7BC`), `$D3` (`E83F`)
and `$D4` (`E739`) read their own bank; **any** other value of `$CF` falls through to `C0/E6B6`,
which reads bank **`$D5` hardcoded**. `E534` has no such dispatch — its `[$CD],Y` honours `$CF` for
every bank — so the two copiers genuinely read **different data** when `$CF` is outside that set.

They also differ in width: `E534`'s `[$CD],Y` is a 24-bit add that can carry out of the bank;
`E687`'s `CLC / ADC $CD` is 16-bit and wraps.

### Acceptance

14 more single-fact mutations, **all 14** red. Three were not, at first, all from one seeding
defect: `$CF` was fixed at `$D2`, so the `E6B6` fallback never fired, the two copiers never
disagreed, and `E691`'s 16-bit add never wrapped. Varying `$CF` over `{$D2,$D6,$D3,$D4}` and `$CD`
over `{0000,F000}` made all three visible (864 / 792 / 1248) — **and the port stayed byte-exact
through that change**, which is the corroboration that the dispatch and both addressing modes are
right rather than merely untested.

**Not swept**: `$CF == $7F` (`E8C2` reads WRAM rather than ROM) is modelled but not exercised — a
`$7F` base plus a high tile index would carry into bank `$80`, which `romRead24` does not map.

Across the whole `C0/CAD9` family there are now **85 acceptance mutations in six drivers**, with one
known exception: `--sweepdrain`'s SEC branch, stated in `src/move.c`.

---

## 12. Classes 1 and 2's tile halves — ported 2026-07-31. Nothing is excluded any more.

`--sweepcad9` 165888/165888 with **0 unported and 0 class1/2-tilehalf**: all four classes, all nine
staging legs, both class-2 geometries, the 2 KB pixel pool, the upload queue and the OBJ templates.

### Two laws, found by scanning every leg

- **The append and the template happen once per sprite**, in the pass that returns CLC.
  `STA $0950,X` exists only at `CCA8`, `CFA8`, `D380`, `D858`, `DB17`. Passes that return SEC stage
  tiles and nothing else.
- **The piece data always begins right after the tile-word array**: `LDY #$20` / `#$40` / `#$60` for
  16 / 32 / 48 words.

### The staging legs

| leg | runs (destOff, wordOff, tiles) | append (dstB, srcB, lenA, lenB) | template |
|---|---|---|---|
| c0 `CBDC` | (0, 0, 16) | +$100, +$100, $100, $100 | 4 pieces @ $20, E8 test |
| c1 `CF21` mode 3 | (0, 0, 32) | +$100, +$200, $200, $200 | 8 @ $40 |
| c1 `D28A` pass 1 | (0, 0, 16) | — | — |
| c1 `D30D` pass 2 | ($200, $20, 16) | +$100, +$200, $200, $200 | 8 @ $40 |
| c2 `D546` p0 | (0,0,8) ($200,$10,8) | — | — |
| c2 `D608` p0/$68 | (0, 0, 16) | — | — |
| c2 `D68B` p1 | ($100,$20,8) ($300,$30,8) | — | — |
| c2 `D738` p1/$68 | ($200,$20,8) ($400,$30,8) | — | — |
| c2 `D7E5` p2 | ($400, $40, 16) | +$300, +$500, **$500**, **$100** | 12 @ $60, jump@8 |
| c2 `DA69` p2/$68 | ($300,$40,8) ($500,$50,8) | +$100, +$100, **$100**, **$500** | 12 @ $60, jump@4 |

### Five things an extrapolation from class 0 would have got wrong

1. **The `>= $E8` priority test is class-0 only.** Classes 1 and 2 contain **zero** `CMP #E8` and
   split by piece *index* instead: 0–3 take `$0C00,Y`, 4 and up take `$0C01,Y`. Counted from the
   ROM: class 0 has 24 template stores for 4 pieces because the E8 test duplicates the `+7` store;
   classes 1/2 have exactly 5 per piece.
2. **The append's `dstB` and `srcB` do not share an offset**, and the two **lengths differ and are
   swapped** between the class-2 variants.
3. **The second run of every two-segment leg reads `$0D80,X` with the WORD `$6D/$6E`**, because
   those legs do a `REP #30` between runs (`D5C4`, `D6F4`, `D7A1`, `DACE`). Every first run and every
   append uses the byte.
4. **The tile number's extra `$10` moves.** Exactly one `CLC / ADC #10` per class-2 template: `DA02`
   (non-`$68`) before the piece-8 store, `DCA9` (`$68`) before the piece-4 store. Classes 0 and 1
   have none.
5. **The record pointer must be re-resolved after the commit** — `CC22`/`D2A3` feed the hardware
   multiplier from `$0F01,X`, which the commit just changed. Resolving it at entry (correct for the
   passes that do not commit) cost 2112 mismatches until it was fixed.

### Acceptance

21 more single-fact mutations, **all 21** red, zero holes — and the `$68` geometry had to be put
*into* the sweep first (`$0D00`'s low byte `== $68`), or three of class 2's six legs were
unreachable. That is the fourth sweep running whose gap was a constant input.

The family is now **106 mutations across seven drivers**, with one known exception:
`--sweepdrain`'s SEC branch, stated in `src/move.c`. Four of the older drivers' anchors broke again
when `ctAnimStage` was split into `ctAnimRec`/`ctAnimAppend`/`ctAnimTemplate`; they were re-anchored
and re-verified.

### What is left

The six OAM emitters (`B788`/`B8CA`/`BA65`/`BCDC`/`BFF2`/`C2BF`, ~4 KB) — which provably touch none
of the state modelled here (§7) — and `$CF == $7F` in the pixel copy, modelled but not swept.

---

## 13. The six OAM emitters — class 0's pair PORTED, the other four not (2026-07-31)

`C0/B701` dispatches to two emitters per class: a **reposition** pair (`B788` / `BA65` / `BFF2`)
that re-derives the on-screen position from records already in the live buffer, and an
**instantiate** pair (`B8CA` / `BCDC` / `C2BF`) that first copies the staged template across and
then does the same arithmetic. `C73A` is class 3's. 4216 bytes in total; **none of it is ported.**

This section records the class-0 pair completely so the port is mechanical. Classes 1 and 2 follow
the same shape with more pieces, but their branch structure has NOT been read.

### The 8-byte record, finally complete

Combining §10's template writes with what the emitters compute:

| byte | written by | meaning |
|---|---|---|
| +0 | **emitter** | final on-screen X, low 8 bits |
| +1 | **emitter** | final on-screen Y |
| +2/+3 | template | X displacement, sign-extended |
| +4 | template | Y displacement |
| +5 | — | never written by either |
| +6 | template | OAM tile number |
| +7 | template | OAM attribute |

The staged copy lives at `$7F:4802 + $1700,X`; the **live** buffer the emitters work in is
`$7F:4BC0 + $1700,X`.

### `C0/B788` — class 0, reposition

```
B788  PHB / LDA #7F / PHA / PLB          DB = $7F
B78F  $C5 = $0A80,X & $01FF              the object's screen Y (9 bits)
B798  $C3 = $0A00,X                      the object's screen X (16 bits)
B79E  STZ $E5                            the OAM high-table accumulator
B7A0  $D9 = $1700,X ; X = $D9 + $18      start at the LAST piece (3 * 8)
B7AB  A = $4BC2,X + $C3                  piece X displacement + screen X
B7B3  STA $4BC0,X                        -> +0
B7B6  XBA / AND #01 / $E6 = bit 8
B7BB  A = ($E5 << 2) | $E6               2 bits per sprite, walking BACKWARDS
B7C1  CPX $D9 / BEQ -> done              piece 0 reached: A holds the finished byte, NOT stored
B7C5  $E5 = A ; X -= 8 ; loop
B7D0  ORA #$AA                           set the SIZE bit of all four sprites
B7D2  LDX $6D / STA $4F00,X              the OAM high-table byte, per object
```

Walking backwards and shifting left by 2 puts piece 0 in bits 0-1, piece 3 in bits 6-7 — the SNES
OAM high-table layout. Only bit 8 of X is OR'd in; the size bits come from that single `ORA #$AA`.

> **CORRECTION, and it was mine.** The paragraph below used to say `$C6` here is the camera
> window's bottom edge left over from `C0/A8BF`. **It is not.** `B796 STA $C5` runs with M
> **sixteen-bit**, so it stores `(screen Y & $01FF)` across `$C5` *and* `$C6` — the emitter
> clobbers the camera value two instructions in. `$C6` is the screen Y's **bit 8**. The sweep
> settled it: seeding `$C6` independently produced 48 mismatches until the port read bit 8 of
> the screen Y instead. (`$C6` genuinely *is* the camera bound elsewhere — that part stands.)

**Then one of THREE Y halves**, a Y-RANGE CLASSIFIER on the object's own screen Y:

| path | gate | rule for `y = $4BC4,X + $C5` |
|---|---|---|
| `B7DD` | Y >= `$100` | carry clear → `$E0`; else `y >= $E0` → keep; else `$E0` |
| `B83A` | `$80` <= Y <= `$FF` | carry clear → keep; `y < $E0` → keep; else `$E0` |
| `B882` | Y <= `$7F` | `BPL` → keep; `y >= $E0` → keep; else `$E0` |

Three `PLB / RTS` exits, at `B834`, `B880`, `B8C8`.

**And the three paths do not write the same number of records.** Counting `+1` stores per path,
identically in both emitters: path A writes **five** (`$4BC1`/`$4BC9`/`$4BD1`/`$4BD9`/`$4BE1`),
paths B and C write four. The X half is a loop over four. So the `Y >= $100` case writes one
record more than the template ever filled; that spare's `+4` is whatever was left there.

### `C0/B8CA` — class 0, instantiate

Byte-for-byte the same arithmetic, with the staged→live copy folded in: `B8ED LDA $4802,X /
STA $4BC2,X / CLC / ADC $C3 / STA $4BC0,X`. Across the routine it copies staged `+2/+3`, `+4` and
`+6/+7` into the live record for all four pieces.

> **A second correction.** This section used to say B8CA has "two exits and a single `LDA $C6`,
> so its Y-variant structure is not the same as B788's" — and that was the stated reason for
> not porting. Wrong: `B920 BEQ ->B9A1` and `B9A3 BPL ->B9F7` give it the **same three-way**
> split, with the same clamp rules (`B922` / `B9A5` / `B9F7`). Its middle path simply `BRA`s
> into the tail of the third at `BA47` instead of carrying its own epilogue, which is why a
> raw `AB 60` scan finds only two. Both are now ported.

### Graded

`--sweepdrain` compares the live records at `$7F:4BC0 + $1700,X` and the high-table byte, over
144 release states: **144/144**. 14 acceptance mutations, all red — one of which was a hole
first, because path C's `BPL` and a carry test never disagreed until the screen Y was
decoupled from the `$1B00` sweep index.

**Class 3 is now reported as underived rather than assumed.** `B785` tail-calls `C0/C73A`, which
has no `SEC/RTS` or `CLC/RTS` anywhere — only a bare `RTS` at `C7FA` — so its carry is
whatever its last `ADC` left. This file used to pin `CLC` for it, *measured*; that
measurement only held while `$1700` happened to be zero, and seeding `$1700` for the
emitters exposed it returning SEC. `CtAnimRelease` returns `-1` there now.

### What porting the REST needs

The four class-1/2 emitters (`BA65` 631 B, `BCDC` 790 B, `BFF2` 717 B, `C2BF` 1147 B) and class
3's `C73A` (198 B). The grading path already exists — they are counted as
`144 cls1-3-emitters` in `--sweepdrain`, so porting any of them moves that number.

Nothing here contradicts §7's finding that the emitters touch none of the arena/queue state: they
read `$0A00`/`$0A80`/`$1700`/`$C3`/`$C5`/`$C6` and write only the live OAM records and `$4F00`.

---

## 14. The class-1/2 emitters — PORTED, and why they are not a parameterised family

`BA65` (631 B) / `BCDC` (790 B) for class 1, `BFF2` (717 B) / `C2BF` (1147 B) for class 2 — all four
**ported and graded**. `--sweepdrain` 144/144, and its `cls1-3-emitters` count falls 144 → 48:
only class 3's `C0/C73A` is still counted.

### They DO share the three-way Y classifier

All four dispatch exactly like class 0's pair:

```
A5 C6        LDA $C6            <- the screen Y's bit 8, written by the emitter's own 16-bit STA $C5
F0 03        BEQ +3
82 xx xx     BRL -> path A      (a FAR block, unlike class 0 where path A falls through)
A5 C5        LDA $C5
10 xx        BPL -> path C      (BA65)
30 03 / 82   BMI +3 / BRL -> C  (BCDC, BFF2, C2BF -- same meaning, different encoding)
```

So the classifier is uniform: `Y >= $100` → A, `$80..$FF` → B, `<= $7F` → C. An earlier reading of
`--sweepdrain`'s scan said the structure "varies per emitter"; it does not — the scan was looking
for `BPL` and three of the four use `BMI` + `BRL`.

### Record counts, settled

| | X half | Y half, per path |
|---|---|---|
| class 0 | 1 group of 4 | **5** on path A, 4 on B and C |
| class 1 | 2 groups of 4 | 8 on every path |
| class 2 | 3 groups of 4 | 12 on every path |

The spare fifth record is **class 0 only**.

### The high-table bytes are NOT contiguous

Each group of four pieces ends with its own `ORA #$AA` and one store:

| class | groups | destinations |
|---|---|---|
| 0 | 1 | `$4F00,X` |
| 1 | 2 | `$4F00,X`, `$4F01,X` |
| 2 | 3 | `$4F00,X`, `$4F01,X`, **`$4B40,X`** (`C0BA` / `C310`-family) |

Class 2's third byte landing at `$4B40` rather than `$4F02` is the kind of thing a parameterised
port would get silently wrong.

### The Y clamp is SEVEN hand-written rules, not one with a mode

This is why they needed a pass of their own. Class 0's three rules do not carry over, and class 1's
path C differs between its **own two emitters** — read from the raw bytes at each path entry:

| | rule |
|---|---|
| c0 A | `ADC $C5 / BCC ->$E0 / CMP #E0 / BCS keep / else $E0` |
| c0 B | `ADC $C5 / BCC keep / CMP #E0 / BCC keep / else $E0` |
| c0 C | `ADC $C5 / BPL keep / CMP #E0 / BCS keep / else $E0` |
| **c1 B** | `ADC $C5 / CMP #E0 / BCC keep / else $E0` — **no carry test at all** |
| **c1 C** (reposition only) | `LDA +4 / BMI ->tail / CLC / ADC $C5 / BCC keep / BRA ->tail` — a non-negative displacement that does not carry is kept **outright**. `BE46`, its own instantiate twin, has no such pre-test. |
| **c2 A** | `ADC $C5 / BCS +2 / BPL +2 / LDA #E0` — a different two-test shape |
| **c2 B** | `ADC $C5 / STA $4BC1,X` — **no clamp whatsoever** |

So the port has `ctOamClamp(cls, mode, isRep, d, sy)` with seven explicit arms, not a shared rule
with a parameter. Everything *else* does generalise — the classifier, the groups of four, the
`ORA #$AA` per group — which is exactly the trap: the structure invites a parameterised port and
the clamps punish one.

### The grading path is already in place

`--sweepdrain` micro-executes `C0/B701` and compares from **`$7F:4B40`** — not `$4BC0` — plus the
high-table byte. That start address matters: class 2's third high byte lands at `$4B40`, below the
record window, and swapping it for `$4F02` changed nothing visible until the window was widened.

**Acceptance**: 15 single-fact mutations, all 15 red. Four were holes first, and all four were
*coverage*: `$4B40` outside the window, and three clamp rules needing input ranges the seeds did
not produce — a sum straddling `$E0` without a carry, and a non-negative displacement landing in
`$80..$DF`. Widening the screen-Y set to include `$F0` and giving the staged and live buffers
different full-range patterns closed all four.

**Still counted**: class 3's `C0/C73A` (198 B), whose carry is data-dependent and underived.
*(Superseded by §15 — it is ported, and the carry is derived.)*

---

## 15. `C0/C73A` — class 3's emitter, PORTED, and two corrections this file owes (2026-09-11)

`--sweepdrain` **192/192 byte-exact, 0 emitters-left-unported**. The counter that read
`144 cls1-3-emitters`, then `48`, now reads zero and is renamed.

### It is not a member of the family

`B701`'s class-3 arm is `B712 BNE ->B785 / B785 BRL C0/C73A` — a **tail call**, so `C73A`'s own
`RTS` carries straight back to `B309`'s `BCC` draw gate. There the resemblance stops:

* there is **one** class-3 emitter, not a reposition/instantiate pair. `C0/C6E7` reads `$4BC2,X`
  and never `$4802,X`, so class 3 always re-derives from the **live** record;
* `C73A` reads `$1B00,X` **nowhere**, calls no arena free and never stamps `0x80`.

So class 3 is outside the arena release law entirely — which is the real reason `T = (class & 3) + 1`
has no class-3 arm, and it is not an omission.

### ⚠ Class 3 is SIX groups of four — 24 pieces, not 16

The arithmetic every other class obeys (`(cls+1)*4`) is **wrong here**, in both halves:

| | value | read from |
|---|---|---|
| C6E7 calls (groups) | **6** | `C757 / C76B / C77F / C793 / C7A7 / C7BB`, X stepped `+$20` between |
| high-table bytes | `$4F00, $4F01, $4B40, $4B41, $4F80, $4F81` | `C75E / C772 / C786 / C79A / C7AE / C7C2` |
| Y-half records | **24** on every path | `C820` unrolls 24; `C7DE`/`C7FB` load `#18` |
| draw-arm advance | `+6` high, `+$60` main | `B5CB` pushes six; `B619 / B6A1 / B6F8` |

`C0/B5CB` pushes the same six high bytes in the same order (`PHA` them backwards, `PLA` them
forwards), so the draw pass's per-class entry count is a table read off the four arms — **1 / 2 / 3
/ 6** — never computed from the class number. That is now `ENT[4]` in `CtAnimDrawPass`, `GRP[4]` in
`src/oam.c` and in `--verifyframe`'s priority injection.

### `C0/C6E7` walks FORWARD

```
C6E7  LDA $4BC2,X / CLC / ADC $C3 / SEP #20 / STA $4BC0,X / XBA / AND #01 / STA $E5   piece 0
C6F9  ... $4BCA,X -> $4BC8,X, bit 8 -> $E6                                            piece 1
C70B  ... $4BD2,X -> $4BD0,X, bit 8 -> $E7                                            piece 2
C71D  ... $4BDA,X -> $4BD8,X, bit 8 stays in A                                        piece 3
C72B  ASL ASL / ORA $E7 / ASL ASL / ORA $E6 / ASL ASL / ORA $E5 / ORA #AA / RTS
```

The **byte** is the same one `B788`'s backwards loop builds — piece 0 in bits 0-1, piece 3 in bits
6-7, every size bit from the single `ORA #$AA`. The **direct-page bytes it leaves behind** are not:
`B788` leaves one accumulator in `$E5`, `C6E7` leaves three individual bits in `$E5/$E6/$E7`. That
is why this is written as its own routine rather than folded into `ctOamEmit`.

### The three clamps were read, not inherited

| path | gate | rule, from the bytes at the entry |
|---|---|---|
| A `C820` | `$C6 != 0` (Y >= `$100`) | `ADC $C5 / BCS ->#E0 / BPL keep` |
| B `C7DE` | `$80` <= Y <= `$FF` | `ADC $C5 / STA` — **no clamp at all** |
| C `C7FB` | Y <= `$7F` | `ADC $C5 / BPL keep / CMP #E0 / BCS keep / else #E0` |

They come out **identical to class 2's three**. That is a measurement, not an inheritance: the port
carries them as the tenth, eleventh and twelfth arms of `ctOamClamp`, each citing its own address,
and a mutation that gives path B path C's rule is red.

### The carry — derived, and both earlier answers retracted

`C73A` has no `SEC/RTS` or `CLC/RTS` anywhere, only bare `RTS`s, so the flag is whatever its last
`ADC` left. **Which `ADC` is not a mystery:**

* **path A** is 24 unrolled records with **no X advance**, so the flag at `C989 RTS` is the carry out
  of the last record's `ADC $C5` at `C97D` (`LDA #E0`, `STA` and `PLB` leave it alone);
* **paths B and C** fall out of their loop epilogue, so the flag is the carry out of the final
  16-bit `ADC #0008` on X (`C7EF` / `C814`) — set only when the record base is within eight bytes
  of wrapping bank `$7F`.

This file used to pin `CLC` here, *measured* — true only while `$1700` was zero. Then it pinned
`-1`, which refused to answer. Both are wrong and both are retired.

### Grading, and what had to be widened to get it

`--sweepdrain` compares from `$4B40` over **0x1A0** bytes (was 0x180: a class-3 object at base `$60`
puts its last record's `+4` at `$4CDC`, past the old end) plus `$4F00/$4F01/$4F80/$4F81`.
`--sweepdraw` carries class 3 in two of its five `CLSPAT` rows — **270/270** — which is the only
thing that grades the 1/2/3/6 entry table, and it compares 0x1C0 from `$4B40` plus `$4F80-$4F8F`.

**Acceptance**: 6 single-fact mutations, all 6 red — the 24-record count (16), the `$4B41`
destination (`$4F02`), the high-table fold direction, path A's carry pinned to `CLC`, path B given a
clamp it does not have, and `ENT[3] = 4`.

`--verifyframe` is unmoved where it matters (0 non-attr mismatches, 0 high, 0 frames differ over 180
frames) and moves where it should: the priority injection now covers class 3, `41760 -> 59040`
pieces and `classes 0x7 -> 0xF`. Class 3 never reaches **that** fixture's draw list
(`DRAWN classes 0x1`), so its emission there is still ungraded by scenario — the sweeps are what
grade it.

---

## 16. The deadline's clock: the zelda3 model's virtual beam that ADVANCES (2026-09-27)

The user's list, items #7 and #8: *"at the entrance of Magus's castle, some bats are rising with a different animation
from the cartridge"* and *"on the bridge, Zombor's legs are moving less than on the cartridge, before the battle
starts"* (translated from Portuguese). Located by the attract-divergence agent (`build/_divergence/user_2026-09-27b.md`), and ONE mechanism: sec. 7's
"not portable" clock. The port called `CtAnimDrainPass(..., 0xF0)` — vblank, no deadline — so a pass emptied the queue.
The first objects took the four arena slots (`E952/E97A/E9AA`, released only by the draw pass `B701`), the rest were
popped by `CC10 BCC -> CC09 CLC` (class 0) / `D558 BCC -> D551 CLC` (class 2) and lost their frame, on every step, since
nothing ever de-phased their timers:

* **0A5, the bats** obj15-26 (sprite 133, class 0, 1 slot each): obj15-18 took the four slots, obj19-22 flew the whole
  climb (~455 frames) in the hanging pose `$0F01 = 07`; on the cartridge every one flaps 08/01/02/00.
* **087, Zombor** obj11 (torso, class 1, 2 slots) + obj12 (legs, class 2, 3 slots): 2 + 3 > 4, the legs never committed
  after the first frame (152 field frames in pose 01, and the battle staged that pose).

On the cartridge (`--hookpc C0CAD9 / CAD6`) the pass enters between lines 56 and 141, a bat's visit costs 31-36 lines, a
pass makes 0-4 visits, and the queue is non-empty at every frame boundary of the climb: the deadline, not the arena,
decides who is served.

### The rule (the user's decisions, 2026-09-27)

A beam read is the one thing a C engine cannot answer, and the answer the project takes for it is snesrev/zelda3's: the
read returns a VIRTUAL beam, the same on both sides. Offered the literal zelda3 value (V = 0, what C3/02DD's seed reads),
the measurement said it makes the deadline never bite — the oracle with V = 0 at the drain freezes obj19 in pose 06 like
the port did, i.e. it would make the defect canonical — and the user chose **the beam that advances**: V grows with the
pass's own visits.

```
CA82 LDA $213F          ; the pass's entry: the virtual counter restarts (K = 0)
CA85 LDA $2137          ; latch                     \
CA88 LDA $213D ; low    ;  V = CT_DRAIN_VBEAM_ENTRY  |  per iteration, before each visit
CA8C LDA $213D ; high   ;    + K * CT_DRAIN_VBEAM_STEP, then K += 1
CA94 CMP #00F0 / BPL    ; unchanged                  |
CA99 CMP $6B / BCS CAD6 ; the ROM's own line         /
```

`src/ct_vbeam.h`: ENTRY = 0x38 (56), STEP = 0x20 (32) — values of the rule, of the cartridge's own order (the entry
lines and a visit's cost above), not tuned to a picture. At `$6B = 0x78` a pass makes **two** visits. The oracle reads
them at C0/CA82-CA90 under `CT_ORACLE_VIRTUAL_BEAM` (snes/snes.c `snes_readBBus`, any `g_ctVirtualBeam` mode); the port's
live VM calls `CtAnimDrainPass(..., -1)` (src/ct_vm.c, C0/00D6), whose V is the same expression.

### What the deadline needed that the port never stored: `$6B` itself

The first run froze the attract at 0AD: `$016B/$016C` read `0000` in the port (the cartridge's `0078`), so V >= 0 cut
every pass before its first visit. Nothing had read the byte while the port passed 0xF0. Two ROM writers:

```
C0/56A6  LDX #0048 / STX $69    ; the object init's head (C0/008E, every location load): the event runner's line
C0/56AB  LDX #0078 / STX $6B    ;   ...and the drain's
C0/56B0  STZ $09A0              ;   ...and the upload queue's gate
C0/6788  INY / TYX / LDA $7F2001,X / STA $6B    ; op 0x47: a script moves the drain's line (2 bytes, the visit goes on)
```

`memLocationLoad` stores the three (ct_vm.c), and op 0x47 runs in the init pass and in `step()`.

### Measured after (build/_divergence/real_vb, z3_vb; tools/attract_anim_drain_cmp.py)

* **0A5**: all eight flying bats go through 00/01/02 from their first flap, 75-83 pose changes over the climb on both
  sides, the port's longest hold 8 frames against the cartridge's 9-11. The frames do not pair one to one — the
  cartridge's schedule is its real beam's — and that is the rule, not a residual.
* **087**: the legs' committed poses from the torso's first 02: cartridge `02 01 02 00 01 00 02 01 00`, port
  `01 02 01 00 01 00 02 01 00` — identical from c7250 on (the legs' box 6160 / 6160 at c7294 / 7310 / 7326; before, 62 of
  125 frames were 874-976 px off). The battle stages the cartridge's pose 00: the Ice Sword boxes +900 px
  (attract-battle-colours, attract-087-ice-crystal).
* **The lockstep against the rule's own oracle** (`CT_ORACLE_VIRTUAL_BEAM=1`): see the facing tail below.

### What the deadline exposed: C0/554B's `STZ $1601,X` (the facing tail)

With the queue emptied every frame, the ORDER objects were queued in never mattered; with two visits a pass it decides
who is served, and an old one-frame drift became the first divergence of every lockstep with walking PCs. MEASURED, the
Manoria nuns (saves/field_re/m81_nuns_lake.state, `--monitor 7E1602:2 --writer`): obj1's facing 02 -> 03 by C0:5550 at
f36, `$1603` reloaded to 05 by C0:CA48 the same frame; the port counted 05 -> 04 on. Ten opcodes share one tail:

```
0x0F/0x17/0x1B/0x1D  C0/5535/5539/553D/5541  LDA #f / BRA 554B                       self, 1 byte
0xA6                 C0/5545  INY / TYX / LDA $7F2001,X                            self, 2 bytes
0xA7                 C0/5557  the operand indexes $7F0200 (stride 2)                 self, 2 bytes
0x1E/0x1F/0x25/0x26  C0/556C/5579/5586/5593  ... TAX / LDA #f / BRA 554D           the object the operand names
554B LDX $6D / 554D STA $1600,X / 5550 STZ $1601,X / 5553 TYX / INX / CLC / RTS
```

The live VM stored the facing of five of them and nothing else (`vmSetFacing`); the runner behind --sweepops had all ten
byte-exact. One body now, `move.c CtVmFacingFamily`, called by both. The nuns' lockstep against the rule's oracle: first
WRAM divergence f36 -> f107 (110 -> 49 addresses), first OAM divergence f36 -> f258. Marle's join (scene 439,
field-marle-join-walk-anim): the drawn frames are the rule's oracle's on every frame of the walk.

### What is still open, stated

* **The event runner's deadline** (C0/5A46-5A5F, `$213D` against `$69` = 0x48) read the REAL beam in the oracle while
  the port's runner never cut (except after a priced song upload, ct_vm.c `sndCut`): the oracle cut it 253 times over
  the demo (0A5 115, 0AD/112 65; the bats' take-off frame visited 17 of 24 objects). The user's decision (2026-09-27):
  the zelda3 literal, the FIXED virtual beam V = 0 -- the gate never cuts, on either side (snes.c for the oracle; the
  port's song-load cut `sndCut` is gone). Measured after: 0 cuts over the demo.
* ~~**The followers' walk-anim phase**~~ (the conga ring's C0/A171 walk tail, party3_field f24/f47) -- **NOT the
  engine's, measured 2026-09-28**: Belthasar (`--loadwram saves/battle_re/party3_field.lakewram --walkscript n:1,l:150`,
  CT_FIELD_MEMMON 1606:4 / 1686:4 / 0F06:4) against the oracle under the rule (`--btnscript 40:130`,
  CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1): obj3/obj4's timers, anim/phase and DRAWN frames are identical on every
  one of 130 frames -- obj3 steps f24/31/38.., obj4 f47/54.., two apart like the cartridge's (and like the real-beam
  oracle's). What `--vmdiff`'s [wmask] shows ($1603 f2, $1607 f25, $1609 f48) is that harness: ct.exe has no host, so
  the lead's anim (the player's C0/9E84 tails, the field's controller) never runs there and the lead sync shifts where
  the followers start.
* **The 07D battle staging** is the rule's: the five enemy records ($29BF, the field poses at the battle's entry) are
  the rule's oracle's byte for byte (`--monitor 7E29BF:60` under `CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1`); slot 4
  is 00 there and 01 on the real cartridge, whose schedule is its real beam's (the 07D battle frames: -15 px).
