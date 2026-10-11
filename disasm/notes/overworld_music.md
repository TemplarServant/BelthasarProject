# THE OVERWORLD'S OWN MUSIC — zoned by a nibble grid

The overworld was silent-by-omission: the field's loader chain `C0/00F4 → C0/092B → C0/1B53` picks a
location's song from the `$F6` record, and `C0/0066`'s `CPX #01F0 / BMI / JML C2:0000` means a
location `>= $1F0` **never reaches it**. Out here the world engine has its own pair of tables, and
both were already sitting in the port's world blob with **no consumer at all**.

`ct-audio-system` had this recorded as "mechanism READ, source REFUTED — still silent", with the
remaining work listed as "the per-world script ENTRY table in `$C3` + the interpreter dispatch". That
turned out not to be the shape of it. The tables are **data the world load already carries**;
`extract_assets.py` has been packing both since the M3 world work (`w[0x7200:+3072]` "region trigger
map", `w[0x1B9B:+16]` "nibble → name id table" — the second label is wrong, it is the SONG table).

---

## 1. THE CHAIN, byte by byte

**On entry** (`C2/2C50`, the world loader):

```
2C50: LDA $7E29AE / 2C54: STA $02AE      ; the world's shadow is SEEDED from the FIELD's
```

**Then the song is issued** (`C2/2F0F`):

```
2F0F: LDA $7F01ED / BNE ->2F30           ; non-zero -> keep the field song, skip the zoning
2F15: LDA $0102 / LSR A / STA $00        ; the arrival 8px cell, halved -> a 16px REGION cell
2F1B: LDA $0103 / LSR A / STA $01
2F21: JSR C2/62ED / TAX                  ; (col,row) -> region nibble
2F25: LDA $1B9B,X / STA $1E01 / STA $02AE
2F2E: BRA ->2F3A
2F30: LDA $7E29AE / STA $1E01 / STA $02AE   ; the $7F01ED arm
2F3A: LDA #10 / STA $1E00                ; cmd $10 = PLAY
2F3F: LDA #FF / STA $1E02
2F44: LDA #80 / STA $1E03
2F49: JSL C7/0004 / 2F4D: RTS
```

**The region lookup** (`C2/62ED`) — a 96×64 nibble grid at `$7E:7200`, 48 bytes a row:

```
62ED: SEP #20
62EF: LDA #7E / STA $12                  ; the long pointer's BANK
62F3: LDA $01 / STA $4202                ; row
62F8: LDA #30 / STA $4203                ; x 48, through the PPU multiplier
62FD: REP #20
62FF: CLC / LDA #7200 / ADC $4216 / STA $10    ; [$10] = $7E:(7200 + row*48)
6308: LDA $00 / AND #00FF / LSR A / TAY  ; Y = col / 2
630F: SEP #20
6311: LDA [$10],Y
6313: LSR $00 / BCS ->631C               ; col ODD  -> 631C: AND #0F   (low nibble)
6317: LSR A x4 / RTS                     ; col EVEN -> high nibble
```

⚠ **`630A AND #00FF` is load-bearing for the CALLER.** The walking path does `3006 AND #FF / 3008
TAX` with an 8-bit A and a **16-bit X**, so `TAX` would carry a stale high byte into `LDA $1B9B,X`
if `B` were not already zeroed here.

⚠ **The ROM bounds nothing.** A row past 63 reads on into `$7E:7E00`, the entry records.

## 2. WORLD 0's TABLE, measured

`$7E:1B9B` = `01 1E 3C 00 00 00 00 00 00 00 00 03 00 00 00 00`, and over the whole 96×64 grid:

| region | cells | song |
|---|---|---|
| 0 | 5671 | `$01` |
| 1 | 392 | `$1E` |
| 2 | 81 | `$3C` |

Two independent oracle measurements, and the second is the one that matters:

| entry | 8px cell | region cell | region | oracle `$02AE`/`$1E01` | field `$29AE` |
|---|---|---|---|---|---|
| the natural Truce-market door (`--loadcensus`, transition f43) | (46,37) | (23,18) | 1 | `1E` | `1E` |
| `--owforce saves/truce_market.state 0` (mid-map) | (96,64) | (48,32) | 0 | **`01`** | `1E` |

The second **refutes "the overworld just keeps the field song"**: the ROM issues `$01` while the
field shadow still reads `$1E`. The formula predicts both.

## 3. THE PORT

`CtWorldRegion` (`src/world.c`, beside its sibling `CtWorldProp` = `C2/62AE`) + `worldEnterSong`
(`src/field.c`, called from `worldEnterAt`). The tables are read out of the WORLD's own image
(`w->wram`); the mailbox is staged in the **HOST** image, because `$1E00` is CPU-global and outlives
every scene — which is exactly why this was unportable before `b970585`: on the overworld there was
no `$7E` to stage it in. The existing unconditional `CtAudioService` then hands it to the driver.

## 4. ⚠ THE REAL DATA IS BLIND TO THE MOST LIKELY DEFECT

Swapping the even/odd nibble select — the single most likely way to mis-code this — leaves **all
three** measurable entry points reporting the same song, and leaves the histogram over all 6144 cells
of world 0 **identical**:

```
histogram correct: {0: 5671, 1: 392, 2: 81}
histogram swapped: {0: 5671, 1: 392, 2: 81}
```

A defect that no amount of real data can see is not covered by real data. Hence
**`--sweepowregion`**: it seeds the grid with a pattern whose two nibbles never match, micro-executes
`C2/62ED` per cell, and **asserts that differing-nibble count in its own report** so the sweep cannot
go blind the way the live data is.

```
sweepowregion: CtWorldRegion vs micro-executed C2/62ED -> 6144/6144 byte-exact (0 mismatch, 0 lost);
               6144/6144 cells have DIFFERING nibbles, so the even/odd select is graded
```

⚠ **`DB` must be `$00` in the call setup**: `62F5 STA $4202` is ABSOLUTE, so a `$7E` data bank writes
WRAM instead of the multiplier and the row offset comes back as whatever `$4216` last held.

Mutations, all three killed: nibble select swapped → **0/6144** (the one the live checks miss
entirely); row stride 48 → 96 → 3264/6144; dropping the `col>>1` packing → 385/6144. Control 6144/6144.

Checks: `sweepowregion-music-zoning` (the decode) and `ow-song-region-zoning` (the pipeline — pinned
on the BOAT entry, which lands in region 0 where the zoned answer and the field shadow differ; the
Truce door cannot tell them apart).

## 5. ✅ THE WALKING SWITCH — derived and ported

It is not a routine, it is an **OBJECT**. The generic dispatcher `C2/0535` loads a block's **class word**
(+0) and uses it *as the handler address* (`STA $50` / `JMP ($0050)`), so the music machine is simply the
ambient-array block carrying **`$2F94`** — block 14, `$7E:0EB0`, on world 0. The dispatcher also owns two
things the handler relies on:

```
0535: LDA $0000,X / STA $50 / SEP #20 / STX $4E      ; $4E = the block base
053E: JSR C2/0553  ->  JMP ($0050)                   ; the class word IS the handler
0543: BCC ->054C                                     ; SEC from the handler = KILL (0547 STZ $0000,X)
054E: INC $0003,X                                    ; CLC = keep, and bump a 16-bit per-object counter
```

**The cycle**, states in `$0002,X`, table at `C2/2F9D`:

| state | handler | what it does |
|---|---|---|
| 0 | `C2/2FA5` | `$EB = FF` (no region yet), state := 1, **falls through into 1** |
| 1 | `C2/2FAE` | `$7F01ED` → keep the field song. Else, gated on `$027C ∈ {0,1,7}` and `$027E == 0`: region from the player position; if `$1B9B[region] != $02AE` → state := 2, counter := 0, stage `81 0C 00 FF` (**fade**) |
| 2 | `C2/303C` | wait until the counter's low byte `>= 0x3C` (60 frames), then state := 3 and `C2/3076` stages `10 <song> FF 80` (**play**), committing `$02AE` only if accepted |
| 3 | `C2/304F` | if `$1BEC` is mid-post, hold; else stage `81 00 FF FF` (the **stop/prepare**) and state := 1 |

**The phase gate** `C2/2ED9`, shared by every overworld sound requester:

```
2ED9: LDA $1BEC / BEQ ->commit / BMI ->refuse        ; 0 idle, 1 staged, $FF being posted
2EE0: LDA $1BEE / CMP $1BED / BCC ->refuse           ; pending priority vs the new one
2EE8: $1E00-$1E03 := $1BE8-$1BEB ; $1BEE := $1BED ; $1BEC := 1 ; CLC
```

and `C2/0454` is what finally posts it: `LDA $1BEC / BEQ skip / LDA #FF / STA $1BEC / JSL C7/0004 /
STZ $1BEC`. ⚠ `C2/2ED9` writes the **command byte FIRST**, where the entry path `C2/2F3A` writes it
LAST. Kept as the ROM has it.

Ported as `CtWorldSoundRequest` + `CtWorldMusicObj` (`src/world.c`), dispatched from
`CtWorldAmbientFrame` on the class word — the ROM's own mechanism, not a slot lookup.

### ⚠ NO ORACLE CAPTURE OF A REAL CROSSING EXISTS

World 0's terrain does not let the player walk out of the region-1 pocket around Truce from any start
reachable in the oracle: **14 border cells × 4 directions** driven with `--teleport --method ow`, none
crossed — the mover stalls ~9 px short every time (`$0283` = 328, needs ≤ 319). So the machine is
graded by **micro-execution** (`--sweepowmusic`: `C2/2F94` 2304/2304 over 16 watched addresses,
`C2/2ED9` 48/48 with 22 refusals) and the live cycle is demonstrated **in the port** via `CT_OWAT`:

```
owsong: switch cmd=81 p=0C region=00 shadow=1E     the fade, the frame the region changes
owsong: switch cmd=10 p=01 region=00 shadow=01     60 frames later: play $01, shadow 1E -> 01
owsong: switch cmd=81 p=00 region=00 shadow=01     the stop/prepare, back to state 1
```

The order and timing are validated against the ROM by the sweep, **not** by an oracle capture. Said
plainly because it is the one thing here that is not measured on the original.

### Two mutations that SURVIVED until the grid was widened

- **The priority compare is unreachable through the handler** — every state writes `$1BED = 0` before
  calling, so `$1BEE < $1BED` is always false and flipping it scored a clean 768/768. `C2/2ED9` is now
  graded on its own, and the sweep asserts how many of its seeds are refused.
- **The row's `- 1` at `C2/2FFE` was ungraded** with one seeded position. The sweep's grid steps
  `48*7 = 0x150` per row, i.e. `+0x50` per byte — which moves the HIGH nibble by 5 and leaves the LOW
  nibble alone, so on an **odd** column the region is identical in `row` and `row-1`. All three original
  positions were odd columns. They are even now and the discriminating count is asserted.

### The bridge, and the real fix

`field.c` mirrors `$0283/$0285` from the mover (what the ROM does) and hand-carries `$29AE`, `$02AE`,
`$7F01ED` and the mailbox between two images, **because the world still keeps a private `$7E` while the
field's is the host's**. The real fix is to give the world the host image too; `world.c` indexes
`ram + 0x10000` for the bank-`$7F` scripts, so that wants the host `Mem` flat at 128 KB — its own change.
`$027C` is written as 0 (`C2/2C20`'s value); the init script's op-`$13` poke to 1 is not run because the
port bakes `$1B9B` instead, and both values sit inside the handler's `{0,1,7}` gate.

## 6. STILL OPEN

- `$7F01ED` (the arm that keeps the field song) is modelled and graded in the sweep, but has never been
  observed non-zero in play.
- The `$027C ∈ {0,1,7}` gate is reproduced but the *meaning* of 7 was not chased, and the port cannot
  currently produce a value outside the set, so the closed-gate arm never fires live.
- States 4 and 5 of the table (`C2/4EA6`, `C2/02FE`) are outside the music cycle and undisassembled.
