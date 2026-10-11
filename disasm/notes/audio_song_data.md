> ⛔ **CORRECTION — do NOT implement from §I's port spec** (adversarial verify, 2026-07-25; detail in
> `audio_song_loader_verify.md`). This dossier's DATA claims held under independent re-derivation —
> song id → ROM address was re-verified for ids $06/$1E/$08 with ZERO mismatched bytes against the
> oracle's ARAM — and its EDL reading of `$1E03` is the correct one. But its §I **port spec has five
> fatal omissions**: it does the port block BEFORE the EDL switch (the ROM does `$0A53 CMP $F0 /
> JSR $09FD` FIRST), misidentifies the command block as `$C7:061C`, and drops the token seed
> (`$0A7B`), the abort path (`$0A80`) and the GO block (`$0601`). **The missing token seed alone
> would hang a port in an infinite handshake spin with no diagnostic.** Use the verify dossier's
> ordered plan instead.

# AUDIO front 4 — WHERE THE SONG DATA LIVES IN ROM (the bytes `C7/01A1` moves)

**Front 2 of 3 on the missing arm.** `audio_boot.md` ported the IPL upload; `audio_commands.md`
derived the `$1E00` mailbox; `audio_song_selection.md` derived *which id*. This dossier derives the
**DATA**: every ROM table `$C7:01A1-$C7:0613` reads, the exact bytes it streams, and the exact ARAM
addresses they land at — so the port can point at the right bytes.

Every claim is either (a) a ROM address disassembled with `python tools/dis.py <flatoffset> <len> [m] [x]`
(ROM = `Chrono_Trigger_USA_SNES.sfc`, 0x400000 bytes, headerless HiROM; bank `$XX` = flat
`(XX-0xC0)<<16 | addr`, so `$C7:0D18` = `0x70D18`), or (b) an **observed oracle value** marked ⟦OBS⟧
with the exact command (`build/ct.exe --aram / --read / --teleport`). Gaps are in §J.

---

## HEADLINE — the eight facts the port needs

1. **All of it is ONE contiguous, length-prefixed chain in ROM: flat `0x0724C3 .. 0x0BF164`
   (`$C7:24C3` → `$CB:F164`), 314 529 bytes, 152 blocks, ZERO gaps and ZERO overlaps** ⟦OBS ROM
   arithmetic, §A.3⟧. Every block is `[uint16 payload length][payload]`.
2. **`$C7:0D18 + id*3` is a 24-bit pointer to the song's SEQUENCE block** (`C7/050F-0521`). The
   payload is streamed **raw** to **ARAM `$2000`** (dest set at `C7/04DD`: `$2143=$20,$2142=$00,$2141=$03`).
   ⟦OBS⟧ byte-identical ROM↔ARAM for songs `$1E`, `$06`, `$08`, `$0C`, `$45`.
3. **`$C7:0E11 + id*32` is the song's INSTRUMENT SET**: up to 16 zero-terminated 16-bit instrument ids
   (`C7/01DC-020D`). Instrument ids run `$01..$3E` (62 of them).
4. **`$C7:0AEA + (instr-1)*3` is a 24-bit pointer to that instrument's BRR SAMPLE block**
   (`C7/0666-0676`), streamed raw into the ARAM sample heap. **Heap base = `$4901`**, computed at boot
   as `rom16[$C7:40B4] + $4700` = `$0201 + $4700` (`C7/010A-0114`, `STA $1E60`).
5. **`$C7:241D + id*2` is NOT a pointer.** Low nibble → `$1E03` → **the ECHO DELAY (EDL, 0-15)**;
   the driver derives **ESA = `$F1 - EDL*8`** (`C7/0A02-0A0B`) and the echo buffer is
   **`[ESA*0x100, $F100)`** — ⟦OBS⟧ confirmed as the *only* per-frame-changing multi-KB ARAM region in
   7 states. High byte → `$1E02` → SPC zero-page `$004D`; **its meaning is SPC-side, NOT DERIVED** (§J.1).
6. **`$C7:1871 + id*36` is a 36-byte per-song block uploaded to ARAM `$F120-$F143`** (`C7/05B2-05FD`)
   — 12 × 3 bytes whose first byte is always in the song's own `srcn` range (`$20+slot`): the
   **percussion / drum key map**.
7. **NOTHING IS COMPRESSED.** The loader reads ROM through `LDA [$12],Y` and writes straight to
   `$2141/$2142/$2143` — there is no buffer and no decompressor anywhere in `$C7:01A1-$C7:0613`
   (§G). `src/lz.c` / `--lz` are irrelevant to audio.
8. **The ARAM sample layout is HISTORY-DEPENDENT, not a function of the song id.** Instruments already
   resident keep their addresses; only the new ones are appended (`C7/0217-0375`). The port must model
   the WRAM arrays `$7E:1E20/$1E40/$1E60` + `$7E:1F00/$1F20/$1F80` (§D.4), or it will place samples at
   the wrong ARAM addresses and the DSP directory will point at garbage.

---

# A — THE `$C7` AUDIO DIRECTORY: eight tables, proved by adjacency

## A.1 The tables and how the code indexes them

| ROM (bank `$C7`) | flat | stride | entries | end | what | cited at |
|---|---|---|---|---|---|---|
| `$0AE9` | `0x70AE9` | 1 | 1 | `$0AEA` | song **count** = `$53` | `C7/01A5 CMP $C70AE9` |
| `$0AEA` | `0x70AEA` | 3 | `$3E`=62 | `$0BA4` | **sample pointer** (24-bit) per instrument | `C7/0666`, `C7/0773` |
| `$0BA4` | `0x70BA4` | 2 | 62 | `$0C20` | sample **loop offset** (added to the ARAM base → DIR word 2) | `C7/03AC ADC $C70BA4,X` |
| `$0C20` | `0x70C20` | 2 | 62 | `$0C9C` | per-instrument param word → ARAM `$1F40+slot*2` | `C7/043C` |
| `$0C9C` | `0x70C9C` | 2 | 62 | `$0D18` | per-instrument param word → ARAM `$1FC0+slot*2` | `C7/048A` |
| `$0D18` | `0x70D18` | 3 | `$53`=83 | `$0E11` | **sequence pointer** (24-bit) per song | `C7/050F-0521` |
| `$0E11` | `0x70E11` | 32 | 83 | `$1871` | **instrument set**: ≤16 zero-terminated words | `C7/01EA LDA $C70E11,X` |
| `$1871` | `0x71871` | 36 | 83 | `$241D` | **percussion block** → ARAM `$F120` | `C7/05B2-05B8`, `Y=id*36` |
| `$241D` | `0x7241D` | 2 | 83 | `$24C3` | `(EDL nibble, $1E02 byte)` | `C7/0A45/0A4B` |
| `$24C3` | `0x724C3` | — | — | — | boot block 0 = the driver blob (`audio_boot.md`) | `C7/0010` |

**Every table's end is exactly the next table's base.** That chain (`$0AE9`→`$0AEA`→`$0BA4`→`$0C20`→
`$0C9C`→`$0D18`→`$0E11`→`$1871`→`$241D`→`$24C3`) is an independent extent proof for all eight strides
and both entry counts, and it means there is **no ninth table hiding between them**.
(`$C7:0028`'s seven-word list — `$0D18, $0AEA, $0BA4, $0C9C, $0C20, $0E11, $0AE9` — names seven of
these eight; `audio_boot.md` §2 called it "NOT DERIVED: what consumes this list". It is exactly this
directory. Nothing dereferences the list itself; the code hardcodes each base.)

## A.2 The index arithmetic, quoted

```
tools/dis.py 0x704FF 0x30 1 0        (SEQUENCE pointer)
  704FF REP #20 / 70501 LDA $05 (=$1E05 = the song id) / AND #00FF
  70506 STA $82 / 70508 ASL A / CLC / ADC $82 / TAX        ; X = id*3
  7050F LDA $C70D18,X -> $12 ; 70515 LDA $C70D19,X -> $13 ; 7051B LDA $C70D1A,X -> $14
  70521 LDY $12 / 70523 STZ $12 / STZ $13                  ; [$12],Y = bank:offset, byte-walked

tools/dis.py 0x701DC 0x34 1 0        (INSTRUMENT SET)
  701DC LDA $04 (16-bit) / AND #FF00 / LSR A x3 / TAX      ; X = id*32   ($1E05 is the high byte)
  701E5 ADC #0020 / STA $0E                                ; $0E = id*32+32 = the row end
  701EA LDA $C70E11,X / 701EE BEQ ->7020F                  ; zero terminates the row
  70209 INX INX / CPX $0E / BNE ->701EA

tools/dis.py 0x70655 0x30 1 0        (SAMPLE pointer)
  7065A LDA $20,X (= $1E20,X, the instrument id) / DEC A / STA $82 / ASL A / CLC / ADC $82 / TAX
  70666 LDA $C70AEA,X -> $12 ; $C70AEB -> $13 ; $C70AEC -> $14        ; X = (instr-1)*3

tools/dis.py 0x7059A 0x30 1 0        (PERCUSSION block)
  7059C LDA $05 & 00FF / ASL/ADC -> id*3 / ASL A / ASL A -> id*12 / STA $82
  705AB ASL A / CLC / ADC $82 / TAY                        ; Y = id*36
  705B2 $12/$13/$14 = $C7:1871                             ; [$12],Y

tools/dis.py 0x70A39 0x28 1 0        (the $1E02/$1E03 pair)
  70A3C LDA $01 (16-bit) / AND #007F / ASL A / TAX
  70A45 LDA $C7241E,X / STA $02                            ; $1E02
  70A4B LDA $C7241D,X / AND #0F / STA $03                  ; $1E03
```

## A.3 The contiguity proof (the strongest single check in this dossier)

Take all 152 length-prefixed blocks the tables above point at — 5 of the 6 boot blocks + the two
`$2F00` SFX sets (§F) + 62 samples + 83 sequences — sort them by flat offset, and walk
`next == off + 2 + len`:

```
blob: first=0x0724C3 last_end=0x0BF164  total=0x4CCA1 (314529 bytes) items=152 gaps=0
```
⟦OBS ROM arithmetic — script under "Repro"⟧. **Zero gaps, zero overlaps.** If any pointer table or
stride were wrong, the walk would break. It also proves the length word is the *stored* size, i.e.
**the payload is not compressed** (a compressed store's ROM size and ARAM size would differ — and
§C.3 / §D.3 show they are equal, byte for byte).

ROM order inside the blob: `blk0(driver) | blk2 | blk3 | blk4 | blk5 | SFXSET-A | SFXSET-B |
samples $01..$3E | sequences $00,$01,…` (songs `$0E`/`$0F` are the one pair stored out of id order:
`$0F` at `0x0AC67A` precedes `$0E` at `0x0AC8C0`).

---

# B — WHAT `$C7:241D + id*2` ENCODES

All 83 entries dumped (appendix 1). **Every `rom[$C7:241D+id*2]` has its high nibble = 0** in this ROM
(so the `AND #0F` is a no-op in practice), and `rom[$C7:241E+id*2]` is always in `$8F..$FF`.
It is **not** a pointer, not a bank/offset pair, and not a size: the pointers are elsewhere (§A).

## B.1 `$1E03` = the ECHO DELAY (EDL). Derived, then observed.

ROM side (`tools/dis.py 0x709FD 0x30 1 0` and `0x70A12 0x30 1 0`):
```
C7/0A53  LDA $03 / CMP $F0 / BEQ +5 / JSR C7/09FD        ; the song's nibble vs $1EF0
C7/09FD  STA $F0                                          ; $1EF0 = the nibble
  0A02   ASL A / ASL A / ASL A / EOR #FF / INC A / CLC / ADC #F1
  0A0B   STA $F2                                          ; $1EF2 = $F1 - nibble*8   *
  0A0D   JSR C7/0A12
C7/0A12  LDX #0000
  0A18   LDA $63,X          ; $1E63,X = HIGH byte of slot X's END address (base+len)
  0A1A   BEQ done
  0A1C   CMP $F2 / BCC next ; slot ends BELOW the $1EF2 page -> keep
  0A29   {STZ $1E20,X ; STZ $1E40,X ; STZ $1E62,X} for this slot and ALL following   ; * EVICT
```
`C7/0A12` is unambiguous: **`$1EF2` is the page a sample may not reach** — i.e. the start of the echo
buffer. Boot seeds the same pair (`C7/0128`: `LDA #05 / STA $F0` … `ADC #F1 / STA $F2` → `$1EF0=5`,
`$1EF2=$C9`). Command `$FC` re-derives it from `$1E01 & $0F` (`C7/0643-064F`).

Oracle side ⟦OBS⟧ — dump ARAM at two frame counts and diff; the echo buffer is the only multi-KB
region the SPC rewrites every frame:
```
build/ct.exe --aram --from <state> --frames 300 --out a.bin
build/ct.exe --aram --from <state> --frames 360 --out b.bin      (diff in Python)
```

| state | song | ROM nibble | ARAM `$0081` | changed region | size | EDL | ESA | `$F1-EDL*8` |
|---|---|---|---|---|---|---|---|---|
| `saves/truce_market.state` | `$1E` | `D` | `0D` | `$8900-$F0FF` | 26624 | 13 | `$89` | `$89` OK |
| `saves/base_loc6.state` | `$06` | `5` | `05` | `$C900-$F0FF` | 10240 | 5 | `$C9` | `$C9` OK |
| `saves/gato_inbattle.state` | `$45` | `4` | `04` | `$D100-$F0FF` | 8192 | 4 | `$D1` | `$D1` OK |
| `saves/battle592.state` | `$45` | `4` | `04` | `$D100-$F0FF` | 8192 | 4 | `$D1` | `$D1` OK |
| teleport loc `$013` | `$08` | `4` | `04` | `$D100-$F0FF` | 8192 | 4 | `$D1` | `$D1` OK |
| teleport loc `$078` | `$0C` | `8` | `08` | `$B100-$F0FF` | 16384 | 8 | `$B1` | `$B1` OK |
| `saves/menu_open.state` | (see note) | — | `05` | `$C900-$F0FF` | 10240 | 5 | `$C9` | `$C9` OK |

**The echo buffer always ENDS at `$F100`**; only its start moves. Note on `menu_open.state`: its CPU
shadow is `$1EF0=$0A / $1EF2=$A1` while the SPC is still at EDL 5 — a state where the nibble was
latched by a command that did **not** reload a song. So the DSP geometry follows the SPC's own
`$0081` (the last *loaded* song's nibble), and `$1EF0/$1EF2` are the CPU's forward-looking shadow.
Cross-check with `audio_commands.md` §1's three states (which labelled `$1EF0` an "SFX bank"):
`bedslide.state` `$1EF0=0F, $1EF2=79` with song shadow `$29AE=$0F` → `rom[$C7:241D+$0F*2]&0F = $0F` OK;
`candy_shop_lake.state` `$1EF0=05, $1EF2=C9`, loc 439 → song `$06`, nibble `5` OK.
**`audio_commands.md` §1 and `audio_song_selection.md` §J.6 are corrected here: `$1EF0` is the echo
delay, `$1EF2` the echo start page — not an "SFX bank index" and not a "voice threshold".**

## B.2 `$1E02` — where it goes, and what is NOT derived

`$1E02` is written to `$2142` with the play command and, ⟦OBS⟧, ends up **verbatim in SPC zero-page
`$004D`**. Found by intersecting three ARAM dumps for the three distinct values (`$E8` truce /
`$C8` loc6 / `$B0` gato) — **exactly one** of the 65536 addresses satisfies all three:
```
addresses holding exactly (E8,C8,B0): ['$004D']
addresses holding (0D,05,04) = the nibble: ['$0081']
```
Confirmed again on the two fresh teleports (`$004D = B0` for both songs `$08` and `$0C`, matching
`rom[$C7:241E+id*2]`). **What the SPC driver does with `$004D` is NOT DERIVED** (§J.1). Four
hypotheses were tested against the data and **refuted**:

* *page above the sample heap* — song `$08`'s samples end at `$D0D9` but its byte is `$B0`; 18 of 83
  songs have `byte*0x100 < heapEnd` (computing each song's heap end as `$4901 + sum(sample lens)`).
* *echo start (ESA)* — §B.1 owns that, and it is derived from the **nibble**, not this byte.
* *DSP echo volume* — the same byte `$B0` gives EVOL 20 / 25 / 15 in `gato_inbattle` / `tp19` /
  `tp120` ⟦OBS `--aram` `evol=`⟧.
* *used-track bitmask* — matches in **only 9/83** songs in either bit order: song `$00` uses 0 of its
  8 track slots yet carries `$FF`, while songs `$06`, `$1E`, `$3F` all use 8 yet carry `$C8`, `$E8`,
  `$E0` (a track counts as used when its group-1 pointer ≠ the end pointer, §C.2).

---

# C — THE SEQUENCE DATA

## C.1 Pointer → block → ARAM

`ptr = rom24[$C7:0D18 + id*3]`; the block at that address is `[uint16 len][len bytes]`; the payload is
streamed to **ARAM `$2000`**. Full 83-row table in appendix 1. Range check in the ROM:

```
7053B  LDX $0C / CPX #0F00 / BCC ->70542 / INC $F3     ; a sequence >= $0F00 spills past $2EFF
```
`$2000 + $0F00 = $2F00` is exactly the SFX-set block (§F) — the guard is "did I trample it".
**Only song `$3F` (`0x12D2` = 4818 bytes) trips it**; all other 82 sequences are ≤ `0x0D29`.

## C.2 The block header (arithmetic, all 83 songs)

```
   (block)  +0    uint16 len                          <- consumed by the loader, NOT uploaded
   (payload, the bytes that reach ARAM $2000):
   +0x00  uint16  = dataStart + 0x24      -> the first byte after the header
   +0x02  uint16  = dataStart + len       -> one past the last byte of the payload
   +0x04  8 x uint16   per-track pointers  (group 1)
   +0x14  8 x uint16   per-track pointers  (group 2)
   +0x24  ...          track byte-code
```
All values are ROM addresses in the block's own bank (`dataStart` = the payload's own 16-bit ROM
address). ⟦OBS ROM arithmetic, all 83 songs⟧:

* `+0x00 == dataStart + 0x24` and `+0x02 == dataStart + len` — **83/83, zero exceptions**.
* Both 8-word groups lie inside `[dataStart, dataEnd]` — 1312/1328 words; the 16 apparent misses are
  the two blocks that straddle a bank boundary (song `$18` at `$CA:F5FD`), where the 16-bit value
  legitimately wraps.
* Group 2 == group 1 for **81/83** songs; they differ for songs `$2F` and `$43` only. That asymmetry
  is what makes group 1 = **track start** and group 2 = **track loop point** the natural reading — but
  the naming is an **interpretation**: what the SPC driver does with the two groups is SPC-side and is
  not decoded here.
* `+0x00` equals group-1 entry 0 for 80/83 (exceptions `$05`, `$36`, `$43`, where it equals a later
  track's pointer) — i.e. `+0x00` is "the end of the header", not "track 0".

The pointers are stored **un-relocated**: ARAM `$2000` holds the ROM values verbatim, so the SPC
driver relocates at play time — SPC-side, out of scope here.

## C.3 Byte-exact ROM ↔ ARAM ⟦OBS⟧

```
build/ct.exe --aram --from saves/truce_market.state --out aram_truce.bin
build/ct.exe --aram --from saves/base_loc6.state    --out aram_loc6.bin
build/ct.exe --teleport 19  8 8  0 --from saves/truce_market.state --frames 300 --out tp19.state
build/ct.exe --teleport 120 8 11 0 --from saves/truce_market.state --frames 300 --out tp120.state
build/ct.exe --aram --from tp19.state  --frames 200 --out ar_tp19.bin
build/ct.exe --aram --from tp120.state --frames 200 --out ar_tp120.bin
```

| state | loc | song | ROM block | payload len | ARAM range | mismatches |
|---|---|---|---|---|---|---|
| `truce_market.state` | `$011` | `$1E` | `$CB:0B32` (`0x0B0B32`) | `0x05C0` (1472) | `$2000-$25BF` | **0 / 1472** |
| `base_loc6.state` | `$006` | `$06` | `$CA:9EC3` (`0x0A9EC3`) | `0x0BE5` (3045) | `$2000-$2BE4` | **0 / 3045** |
| `tp19.state` | `$013` | `$08` | `$CA:ABF4` (`0x0AABF4`) | `0x05BC` (1468) | `$2000-$25BB` | **0 / 1468** |
| `tp120.state` | `$078` | `$0C` | `$CA:BC34` (`0x0ABC34`) | `0x0518` (1304) | `$2000-$2517` | **0 / 1304** |
| `gato_inbattle.state` | battle | `$45` | `$CB:AFA3` (`0x0BAFA3`) | `0x0660` (1632) | `$2000-$265F` | **0 / 1632** |

## C.4 The 3-byte-granularity OVERSHOOT (a real, byte-visible quirk)

```
70571 DEX / DEX / DEX / 70574 BPL ->7054C     ; sequence: loop while X >= 0
```
X starts at `len`, so the loader sends **`(len/3 + 1) * 3` bytes** — it always overshoots by 1..3
bytes, pulling the head of the *next* ROM block into ARAM behind the sequence. (The sample loader uses
`BNE` instead — §D.2 — and its lengths are multiples of 9, so samples are exact.)
⟦OBS⟧ the overshoot bytes are in ARAM, byte-identical:

| song | len | iterations | sent | overshoot | ROM bytes | ARAM bytes |
|---|---|---|---|---|---|---|
| `$08` | 1468 | 490 | 1470 | 2 | `6e 03` | `6e 03` OK |
| `$0C` | 1304 | 435 | 1305 | 1 | `2a` | `2a` OK |
| `$1E` | 1472 | 491 | 1473 | 1 | `fc` | `fc` OK |
| `$06` | 3045 | 1016 | 3048 | 3 | `48 01 d0` | `48 01 d0` OK |

A port that streams exactly `len` bytes will differ from the oracle in those 1-3 ARAM bytes.

---

# D — THE INSTRUMENT SET (samples, DIR, allocation)

## D.1 Song → instrument list

`rom[$C7:0E11 + id*32]` = up to 16 little-endian **words**, zero-terminated (appendix 1, last column).
Observed range `$01..$3E`; counts 0..11. Eight songs have an **empty** list
(`$00, $1A, $22, $35, $43, $47, $4A, $4B`) — they play on whatever samples are already resident.

## D.2 Instrument → BRR sample block

`ptr = rom24[$C7:0AEA + (instr-1)*3]` → `[uint16 len][len bytes of raw BRR]` (appendix 2).
`rom[$C7:0BA4 + (instr-1)*2]` is the **loop offset** added to the sample's ARAM base to form the
second word of its DSP directory entry (`C7/03AC CLC / ADC $C70BA4,X`).

**Structural proof that the payload is raw BRR** ⟦OBS ROM arithmetic⟧: for **all 62** instruments,
`len % 9 == 0`, `loopOffset % 9 == 0`, and the BRR END bit (bit 0 of each 9-byte block's header) is
set on **exactly one** block — the last:
```
BRR structural check: 0/62 instruments deviate from
   "len%9==0, loop%9==0, END bit only on the LAST 9-byte block"
```

Streaming loop (`tools/dis.py 0x70678 0xA8 1 0`):
```
70680 LDA $60,X -> $2142 ; 70685 LDA $61,X -> $2143 ; 7068A LDA #03 -> $2141   ; ARAM dest, cmd 03
706A2 LDA [$12],Y -> $0C, and -> $40,X (the length)   ; $62,X = $60,X + len (next free)
706C1 LDX $0C ; {3 bytes -> $2141/$2142/$2143 ; C7/09DA handshake ; DEX DEX DEX} BNE
```

## D.3 The sample heap and the DSP directory

* **Heap base = ARAM `$4901`**, computed at boot (`tools/dis.py 0x70108 0x14 1 0`):
  ```
  7010A LDA $C740B4 (16-bit)      ; = the SIZE word of boot block 2 = $0201
  7010E CLC / ADC #4700           ; + block 2's ARAM destination
  70114 STA $60                   ; $7E:1E60 = $4901   * the allocator's origin
  ```
  ⟦OBS⟧ `--read "wram:7E1E60+34"` on both teleport states → first word `01 49` = `$4901`.
* Samples are packed **contiguously**: `base[k+1] = base[k] + len[k]`.
* **DSP DIR page = `$1E`** ⟦OBS `--aram` prints `dir=$1E00` in every state⟧. Boot block 3 fills
  `$1E00-$1E23` (9 entries, `srcn 0..8`). The song's 16 instruments occupy **`srcn $20..$2F`**, whose
  directory rows are `$1E80 + slot*4` — matching the upload destination
  (`C7/03CA: $2143=$1E, $2142=$80, $2141=$02`, then `0x40` bytes from the WRAM staging buffer
  `$7E:1F80`). ⟦OBS⟧ in all 7 states, **every voice with non-zero gain carries `srcn` in `$20..$29`**
  (= `$20 + slot`, and no song here has more than 10 instruments); the only voices with `srcn < $20`
  are silent SFX channels (`srcn=03` in `gato_inbattle`, `srcn=04` in `tp19`, both `gain=0000`),
  which read the boot directory at `$1E00`. Each voice's `decodeOff` also falls inside its slot's
  ARAM range — e.g. `tp19` `v5 srcn=23 decodeOff=$998C`, and slot 3 (instr `$19`) is `$9338-$9AC6`.

⟦OBS⟧ ROM → ARAM, every instrument of the four freshly-loaded songs, **0 mismatched bytes**:

| state | song | slot | instr | ROM block | len | ARAM range | DIR loop delta == ROM loop |
|---|---|---|---|---|---|---|---|
| tp19 | `$08` | 0 | `$17` | `$C8:7D64` | `0x0BF4` | `$6BCF-$77C2` | OK |
| tp19 | `$08` | 1 | `$2C` | `$C9:BBF7` | `0x1D25` | `$4901-$6625` | OK |
| tp19 | `$08` | 2 | `$3A` | `$CA:6122` | `0x1B75` | `$77C3-$9337` | OK |
| tp19 | `$08` | 3 | `$19` | `$C8:8F05` | `0x078F` | `$9338-$9AC6` | OK |
| tp19 | `$08` | 4 | `$18` | `$C8:895A` | `0x05A9` | `$6626-$6BCE` | OK |
| tp19 | `$08` | 5 | `$13` | `$C8:467B` | `0x0FFF` | `$9AC7-$AAC5` | OK |
| tp19 | `$08` | 6 | `$02` | `$C7:81AF` | `0x07A1` | `$AAC6-$B266` | OK |
| tp19 | `$08` | 7 | `$0C` | `$C7:F540` | `0x0E8E` | `$B267-$C0F4` | OK |
| tp19 | `$08` | 8 | `$04` | `$C7:8B82` | `0x0FE4` | `$C0F5-$D0D8` | OK |
| tp120 | `$0C` | 0-5 | `17 2C 24 09 25 27` | — | — | `$6626 $4901 $721A $78DA $8C93 $9AD9` | OK |
| truce | `$1E` | 0-3 | `2C 18 17 38` | — | — | `$4901 $6626 $6BCF $77C3` | OK |
| loc6 | `$06` | 0-9 | `32 1D 1C 1A 02 03 04 1F 01 1B` | — | — | `$4901` … `$C53E` | OK |

Note song `$08`'s heap high-water is `$D0D9` and its echo start is `$D100` — **39 bytes of headroom**.
The fit is by design; §B.1's eviction pass is what enforces it.

## D.4 The allocator is INCREMENTAL — the ARAM layout depends on the PREVIOUS song

Look at tp19 above: the song's list order is `17 2C 3A 19 18 13 02 0C 04`, but `2C` sits at the heap
base `$4901` and `17` at `$6BCF`. That is because the previous state (truce, song `$1E`) had
`2C, 18, 17` resident at `$4901 / $6626 / $6BCF` — **they were kept in place**, and only the six new
instruments were appended, starting where song `$1E`'s instrument `38` used to be (`$77C3`).

The CPU-side state, all in WRAM
⟦OBS `--read "wram:7E1E20+32,wram:7E1E40+32,wram:7E1E60+34,wram:7E1F00+32,wram:7E1F20+32,wram:7E1F80+64" --from tp19.state`⟧:

| WRAM | size | meaning | tp19 value |
|---|---|---|---|
| `$7E:1E20` | 16 words | **resident** instrument ids, in allocation order | `2C 18 17 3A 19 13 02 0C 04 0…` |
| `$7E:1E40` | 16 words | each resident sample's **length** | `1D25 05A9 0BF4 1B75 078F 0FFF 07A1 0E8E 0FE4 0…` |
| `$7E:1E60` | 17 words | each resident sample's ARAM **base**; entry `[n]` = the heap high-water | `4901 6626 6BCF 77C3 9338 9AC7 AAC6 B267 C0F5 **D0D9**` |
| `$7E:1F00` | 16 words | the **song's** list, verbatim from `$C7:0E11` | `17 2C 3A 19 18 13 02 0C 04 0…` |
| `$7E:1F20` | 16 words | the **diff**: entries not already resident | `3A 19 13 02 0C 04 0…` |
| `$7E:1F80` | 32 words | DIR **staging** (start, loop) per song slot → ARAM `$1E80` | `6BCF 738B / 4901 491C / 77C3 7CCA / …` |
| `$7E:1EF0` | byte | EDL | `04` |
| `$7E:1EF2` | word | ESA page | `$00D1` |
| `$7E:1EF6` | word | heap high-water | `$D0D9` |
| `$7E:1EF8` | word | heap base | `$4901` |
| `$7E:1EFC` | word | word index of the song's 1st instrument in the resident array (`C7/04B1-04BE`); cmd `$71`'s target slot | `0004` |

Code: `C7/01C4-020D` builds `$1F00` / `$1F20`; `C7/0217-0257` marks residents the new song does *not*
need; `C7/025A-0325` **compacts** — it re-sends surviving samples with driver command `$07`
(`$2141=07`, old address on `$2142/$2143`, new on the next pair) and rebuilds `$1E20/$1E40/$1E60`;
`C7/033F-0359` appends the `$1F20` list via `C7/0655`; `C7/037B-03C6` builds the DIR staging.

## D.5 The two per-instrument parameter tables (and their ROM-wrapping quirk)

```
7040D  $2143=$1F, $2142=$40, $2141=$02      ; ARAM dest $1F40
70431  {Y=0..0x1E step 2}  LDA $1F00,Y / DEC A / ASL A / TAX / LDA $C70C20,X -> $2141,$2142
7045B  $2143=$1F, $2142=$C0, $2141=$02      ; ARAM dest $1FC0
7047F  {Y=0..0x1E step 2}  LDA $1F00,Y / DEC A / ASL A / TAX / LDA $C70C9C,X -> $2141,$2142
```
Both loops run the **full 16 entries** — they do **not** stop at the list's zero terminator. For an
empty slot, `LDA #0000 / DEC A / ASL A` gives `X = $FFFE`, and `LDA $C70C20,X` (long,X) wraps into the
**next bank**, reading `rom[$C8:0C1E]` (resp. `rom[$C8:0C9A]`). ⟦OBS⟧ the garbage constants are in
ARAM, byte-exact, in every state: `$0502` fills the tail of `$1F40`, `$1FC2` the tail of `$1FC0`:

```
tp120 / song $0C   C7/0C20 -> $1F40  ROM = 00FF F0E9 6CE1 0043 007C 00AF | 0502 x10
                                    ARAM = (identical)   match=True
                   C7/0C9C -> $1FC0  ROM = E0FF E0FF E0FF E0FF E0FF EEFF | 1FC2 x10   match=True
```
A port that stops at the terminator will diverge in ARAM `$1F40-$1F5F` / `$1FC0-$1FDF`.

---

# E — THE 36-BYTE PERCUSSION BLOCK → ARAM `$F120`

```
705BE  $2143=#F1, $2142=#20, $2141=#03      ; ARAM dest $F120
705E0  LDX #000C ; {3 bytes from [$C7:1871],Y -> $2141/$2142/$2143 ; handshake ; DEX} BNE
```
12 x 3 bytes = 36, exact (no overshoot: `DEX / BNE`). ⟦OBS⟧ `rom[$C7:1871+id*36] == aram[$F120..$F143]`
for songs `$1E`, `$06`, `$45`, `$08`, `$0C` — 5/5 exact.

Layout, from the data: 12 slots of `(b0, b1, b2)`; `b0` is always either `$00` (slot unused) or a value
in **the song's own `srcn` range `$20+slot`**. Song `$06` (10 instruments → `srcn $20-$29`):
```
C7/1949: 000000 000000 253E2C 274B11 284C40 000000 000000 243740 26446E 000000 000000 243B54
```
`b0` in {`$24`,`$25`,`$26`,`$27`,`$28`} — inside `$20..$29`. Song `$1E` (4 instruments) has all 36
bytes zero. **The per-field meaning of `b1`/`b2` is SPC-side and NOT DERIVED** (§J.2).

---

# F — THE `$2F00` BLOCK IS *TWO* SFX SETS, SWITCHED BY `$1E10`

`audio_boot.md` lists boot block 1 as `$C7:4309`, 6144 bytes → ARAM `$2F00`. There is a **second,
identical-sized block immediately after it in ROM**, and the driver swaps them:

```
tools/dis.py 0x70922 0xB8 1 0
70952  LDA $10 / BNE ->70998                 ; $1E10 (the cmd-$70 mode byte)
70956  LDA $C74309 / $C7430A -> $0C          ; size word of SET A
70962  $12/$13/$14 = $C7:430B                ; payload
7096E  {3 bytes -> ports ; handshake ; DEX DEX DEX} BNE     ; -> ARAM $2F00 (dest set at 70924)
70991  STZ $F3
70998  LDA $C75B0B / $C75B0C -> $0C          ; size word of SET B
7099A  $12/$13/$14 = $C7:5B0D                ; payload
```

| set | ROM header | flat | payload | ARAM | selected when |
|---|---|---|---|---|---|
| A | `$C7:4309` | `0x074309` | 6144 | `$2F00-$46FF` | `$7E:1E10 == 0` (field) |
| B | `$C7:5B0B` | `0x075B0B` | 6144 | `$2F00-$46FF` | `$7E:1E10 != 0` (battle) |

⟦OBS⟧ proof: in `saves/gato_inbattle.state` and `saves/battle592.state`, ARAM `$2F00` differs from
boot block A in **5581 / 6144** bytes — and a ROM search for the first 24 ARAM bytes lands at
`0x075B0D` with a **0x1800-byte contiguous match**. In every field state (`truce_market`, `base_loc6`,
`menu_open`, both teleports) ARAM `$2F00` is **0 / 6144** different from set A.

The switch is driven from the CPU: `C0/1BC1 LDA #FF / STA $1E10` at battle start and `C0/1C11
$1E10 = 0` at battle end (`audio_song_selection.md` §E), then command `$70` (`C7/08E3`) does
`$1E10 = $1E11 = (param != 0); JSR C7/0922`. The song loader re-checks it every time:
```
704C2 LDA $10 / CLC / ADC #FF / LDA #00 / ROL A   ; A = ($1E10 != 0) ? 1 : 0
704CA CMP $11 / BEQ ->704D4                        ; $1E11 = which set is resident
704CE STA $10 / STA $11 / INC $F3                  ; changed -> flag a reload
704D4 LDA $F3 / BEQ ->704DB / JSR C7/0922          ; also fires after a >$0F00 sequence trampled it
```

---

# G — IS IT COMPRESSED? **NO.**

Four independent proofs:

1. **Byte-exact ROM ↔ ARAM.** 5 sequences (1472 / 3045 / 1468 / 1304 / 1632 bytes) and 29 sample
   blocks (up to 7461 bytes) match with **0 differing bytes** (§C.3, §D.3). A compressed store cannot.
2. **The ROM length word == the ARAM byte count.** The loader's counter `X` is loaded straight from
   the block's own length word (`C7/0539 LDX $0C`, `C7/06C1 LDX $0C`).
3. **There is no buffer in the path.** The only ROM reads between `C7/01A1` and `C7/0613` are
   `LDA [$12],Y` (a byte-walked 24-bit ROM pointer) writing directly into `$2141/$2142/$2143`, plus
   the table reads of §A. No `MVN`, no decompressor call, no scratch destination.
4. **The BRR structure survives in ROM** (§D.2): all 62 samples are valid 9-byte-block BRR *as stored*.

`src/lz.c` / `build/ct.exe --lz` (the "Evil Peer" LZ used for graphics) is **not applicable** to any
byte in `0x0724C3-0x0BF164`.

---

# H — THE SONG-DEPENDENT ARAM DIFF MAP (the implementer's checklist)

⟦OBS⟧ `aram_truce.bin` (song `$1E`) vs `aram_loc6.bin` (song `$06`), both at frame 300, runs merged
across gaps ≤ 96:

| ARAM range | bytes | what it is | reproduced by |
|---|---|---|---|
| `$0000-$015B` | 348 | SPC driver zero page. Includes **`$004D` = `$1E02`** and **`$0081` = `$1E03`** | the play command's `$2142/$2143` |
| `$01FA-$01FF` | 6 | driver scratch | driver runtime |
| `$0200-$1DEE` | 11-31 differ | boot block 0 = the driver blob; the few differing bytes are its own variables | not part of a song load |
| `$1DCC-$1DDC` | 17 | driver runtime | driver runtime |
| **`$1E80-$1EBF`** | 64 | **DSP sample DIRECTORY, `srcn $20-$2F`** (start, loop per slot) | staged in WRAM `$7E:1F80`, uploaded `C7/03C8-0407` |
| **`$1F40-$1F5F`** | 32 | per-instrument param words from `$C7:0C20` (incl. the `$0502` wrap fill) | `C7/040D-0455` |
| **`$1FC0-$1FDF`** | 32 | per-instrument param words from `$C7:0C9C` (incl. the `$1FC2` wrap fill) | `C7/045B-04A5` |
| **`$2000` + len** | ≤ `$0F00` | **the SEQUENCE** (+1..3 overshoot bytes) | `C7/04DB-0596` |
| **`$2F00-$46FF`** | 6144 | the SFX set (A or B) — changes on an `$1E10` flip or after a `>$0F00` sequence | `C7/0922` |
| **`$4901` …** | up to ~`$8800` | **the BRR sample heap**, incrementally allocated | `C7/0655` per new instrument |
| `ESA*0x100 - $F0FF` | EDL x 2048 | **echo buffer** — DSP-written, never uploaded | geometry from `$1E03` only |
| **`$F120-$F143`** | 36 | **percussion block** | `C7/05B2-05FD` |
| `$F200-$F48F`, `$F500-$F63B` | ~970 | driver runtime (sequencer + per-voice work) | driver runtime |

Identical across every state examined (never touched by a song load): boot blocks
**2 (`$4700`, 513 B)**, **3 (`$1E00`, 36 B)**, **4 (`$1F80`, 20 B)**, **5 (`$1F00`, 20 B)** — 0/N
differing bytes in all 5 states.

---

# I — PORT SPEC (derive lane; nothing applied)

`CtAudioService`'s `$10/$11/$14/$15` arm must, in this order:

1. `if (id >= rom[0x70AE9]) return;` (`$53`).
2. `$1E02 = rom[0x7241E + id*2]; $1E03 = rom[0x7241D + id*2] & 0x0F;` then `C7/061C`'s port write
   (`$2143, $2142, $2141, $2140`, echo-wait) — **already ported**.
   If `$1E03 != $1EF0`: `$1EF0 = $1E03; $1EF2 = 0xF1 - $1E03*8;` then run the eviction pass
   (`C7/0A12`): from slot 0 up, the first slot whose `(base+len) >> 8 >= $1EF2` and every slot after it
   are cleared (`$1E20 / $1E40 / $1E62`).
3. `$1E05 = id` (busy mutex — every other command is dropped until it clears).
4. Build `$1F00` from `rom[0x70E11 + id*32]` (≤16 words, zero-terminated) and `$1F20` = those not
   already in `$1E20`.
5. Reconcile residency: drop residents the song does not need, **compact** the survivors (driver
   command `$07` per moved block), then append each `$1F20` entry — for instrument `i`,
   `ptr = rom24[0x70AEA + (i-1)*3]`, `len = rom16[ptr]`, dest = the running `$1E60` cursor,
   `$1E40[slot] = len`, `$1E60[slot+1] = dest + len`.
6. Build the 16-entry DIR staging `$1F80` from `$1F00`: for each entry present in `$1E20`,
   `(base, base + rom16[0x70BA4 + (i-1)*2])`; for one that is not, `(0,0)`; **stop at the first zero
   entry, leaving the rest of `$1F80` stale**. Upload 0x40 bytes → ARAM `$1E80`.
7. Upload 16 words from `0x70C20` → ARAM `$1F40` and 16 from `0x70C9C` → ARAM `$1FC0`,
   **all 16 slots, with the `X=$FFFE` bank-wrap read for the empty ones** (§D.5).
8. If `(($1E10 != 0) ? 1 : 0) != $1E11` → set both, set `$1EF3`. If `$1EF3` → upload SFX set
   A (`0x074309`) or B (`0x075B0B`) → ARAM `$2F00`, clear `$1EF3`.
9. Sequence: `ptr = rom24[0x70D18 + id*3]`, `len = rom16[ptr]`; if `len >= 0x0F00` set `$1EF3`;
   stream `(len/3 + 1)*3` bytes from `ptr+2` → ARAM `$2000` (dest command `03`).
10. Percussion: stream 36 bytes from `0x71871 + id*36` → ARAM `$F120` (dest command `03`).
11. `$1E05 = $E0`, then the `C7/0A12` eviction pass again (`C7/0616`).

**Constants a port must not invent:** heap base `$4901` = `rom16[0x740B4] + 0x4700`; sequence dest
`$2000`; DIR dest `$1E80`; percussion dest `$F120`; echo top `$F100`; song count `rom[0x70AE9]`;
instrument count 62 = `(0x70BA4 - 0x70AEA) / 3`.

---

# J — NOT DERIVED

1. **What `$1E02` (SPC zero-page `$004D`) means.** Located and proven to be the byte from
   `$C7:241E + id*2`, but its use is inside the driver blob. Four candidate meanings were tested
   against the data and refuted (§B.2). **Closes it:** disassemble ARAM `$0200-$1DEE` as SPC700 (the
   blob is in any `--aram --out` dump, or ROM `0x0724C5+`) and find the reads of direct-page `$4D`.
   No SPC700 disassembler exists in `tools/` today.
2. **The `(b0, b1, b2)` fields of the 36-byte percussion block.** `b0` is proven to lie in the song's
   `srcn` range; `b1`/`b2` are consumed SPC-side. **Closes it:** the same SPC disassembly, following
   the reads of ARAM `$F120`.
3. **The `>= $0F00` big-sequence path is ROM-derived but NOT OBSERVED.** Only song `$3F` (4818 bytes)
   trips it, and ⟦OBS⟧ **no** location record selects `$3F` (a histogram of all 512
   `rom[$F6:loc*14]` byte-0 values contains no `$3F`). **Closes it:** find a state where song `$3F`
   plays (a script op-`$EA 3F`, or a bank-`$C3` cue), then `--aram --at 2F00+0x40` before and after.
4. **The compaction path `C7/025A-0325`** (driver command `$07` = move a block inside ARAM) was read
   but never exercised under observation: all four freshly-loaded songs here either kept residents in
   place or appended. **Closes it:** two teleports in a row between songs whose instrument lists
   overlap only in the *middle* of the resident array, with `--read "wram:7E1E60+34"` before and after.
5. **Command `$71`'s target-slot rule.** `C7/07A1` checks `newLen + $1E60[X] - 1 < $1E62[X]` with
   `X = $1EFC`, i.e. it overwrites the song's *first* instrument's sample in place if the new one
   fits, else drops the request (`JMP C7/0192`). Read, not observed. **Closes it:**
   `--monitor "1E00:4,7E1EFC:2" --writer` across a battle that issues cmd `$71`.
6. **Two ARAM anomalies in the s9x-imported battle states.** In `gato_inbattle.state` and
   `battle592.state`, DIR slot 0 points at `$4901` but the bytes there are a fragment of instrument
   `$1A`, not instrument `$05` (1943 / 1962 differ, stable over 1200 frames; the other 9 slots are
   0-mismatch). This is an **artifact of the states, not a mechanism**: `tools/s9x_to_lake.py` splices
   only WRAM + the CPU register file into a LakeSnes base, so ARAM is inherited from the base state's
   history. The two states the oracle produced itself (`tp19`, `tp120`) are 0-mismatch in every slot.
   **Do not pin a regression check on the ARAM sample content of an s9x-imported state.**

---

## Corrections to existing notes

* `audio_commands.md` §1: "`$7E:1EF0` current **SFX bank** index" and "`$1EF2` derived **voice
  threshold** `= (0xF1 - bank*8)`" — **wrong labels**. `$1EF0` is the **echo delay (EDL, 0-15)** taken
  from `rom[$C7:241D+id*2] & $0F`; `$1EF2` is the **echo-buffer start page**, and `C7/0A12` uses it to
  evict samples that would reach into the echo buffer. That note's three observed values are
  re-explained exactly by the songs those states were playing (§B.1). The *arithmetic* there is right;
  only the names were wrong. Same for `audio_commands.md` §5's "bank switch if the song needs another
  SFX bank" at `C7/0A53` — it is an echo-delay change.
* `audio_commands.md` §5 and `audio_song_selection.md` §A.2 / §G call `$C7:241D + id*2` "the song's
  ARAM parameters" / "two parallel 83-entry tables". True but under-specified: it is one 2-byte record
  (low nibble = EDL, high byte = the `$004D` parameter) — and **it is not where the song data lives**.
  The pointer tables are `$C7:0D18` (sequence) and `$C7:0AEA` (samples).
* `audio_boot.md` §2: "`$C7:0028-$0037` … **NOT DERIVED**: what, if anything, consumes this list" —
  the seven words are seven of the eight bases of the audio directory in §A.1. Nothing dereferences
  the list itself (the code hardcodes each base), so it is a build-time manifest.
* `audio_boot.md`'s block table calls `$C7:4309` "block 1". It is **SFX set A**; `$C7:5B0B` is set B,
  and the driver swaps them at ARAM `$2F00` on `$1E10` (§F). The boot upload legitimately only ever
  installs A.

---

## Repro commands

```
python tools/dis.py 0x701A1 0x180 1 0    # C7/01A1 song handler: range check + instrument diff
python tools/dis.py 0x70321 0x200 1 0    # compaction, DIR staging, the two param tables
python tools/dis.py 0x704C0 0x160 1 0    # SFX-set reload, SEQUENCE upload, PERCUSSION upload
python tools/dis.py 0x70655 0xF0 1 0     # C7/0655 sample upload
python tools/dis.py 0x70922 0xB8 1 0     # C7/0922 the two SFX sets ; 0x709DA = the handshakes
python tools/dis.py 0x709FD 0x30 1 0     # C7/09FD EDL -> ESA ; 0x70A12 = the eviction pass
python tools/dis.py 0x70108 0x14 1 0     # boot: $1E60 = rom16[$C7:40B4] + $4700 = $4901

build/ct.exe --aram --from saves/truce_market.state --frames 300 --out a.bin
build/ct.exe --aram --from saves/truce_market.state --frames 360 --out b.bin   # diff a/b -> echo buffer
build/ct.exe --teleport 19  8 8  0 --from saves/truce_market.state --frames 300 --out tp19.state
build/ct.exe --teleport 120 8 11 0 --from saves/truce_market.state --frames 300 --out tp120.state
build/ct.exe --aram --from tp19.state --frames 200 --out ar_tp19.bin --at 1E80+64
build/ct.exe --read "loc,wram:7E29AE+1,wram:001E00+8,wram:001EF0+16" --from tp19.state
build/ct.exe --read "wram:7E1E20+32,wram:7E1E40+32,wram:7E1E60+34,wram:7E1F00+32,wram:7E1F20+32,wram:7E1F80+64" --from tp19.state
```

The contiguity walk (§A.3), verbatim:
```python
d=open('Chrono_Trigger_USA_SNES.sfc','rb').read()
flat=lambda b,o:((b-0xC0)<<16)|o
items=[0x0724C3,0x0740B4,0x0742B7,0x0742DD,0x0742F3,0x074309,0x075B0B]
items+=[flat(d[0x70AEA+(i-1)*3+2], d[0x70AEA+(i-1)*3]|(d[0x70AEA+(i-1)*3+1]<<8)) for i in range(1,0x3F)]
items+=[flat(d[0x70D18+s*3+2],     d[0x70D18+s*3]|(d[0x70D18+s*3+1]<<8))         for s in range(0x53)]
items.sort(); prev=None
for off in items:
    assert prev is None or off==prev, (hex(off),hex(prev))
    prev=off+2+(d[off]|(d[off+1]<<8))
print(hex(items[0]),hex(prev),len(items))     # 0x724c3 0xbf164 152
```

---

## Appendix 1 — all 83 songs

| id | sequence ptr | payload len | `$1E02` | `$1E03`=EDL | #instr | instrument set (`$C7:0E11 + id*32`) |
|---|---|---|---|---|---|---|
| `$00` | `$CA:8ACC` (`0x0A8ACC`) | `0x0024` | `$FF` | `5` (ESA $C9) | 0 | (none) |
| `$01` | `$CA:8AF2` (`0x0A8AF2`) | `0x04BF` | `$F8` | `D` (ESA $89) | 3 | 2C 13 17 |
| `$02` | `$CA:8FB3` (`0x0A8FB3`) | `0x0499` | `$DC` | `7` (ESA $B9) | 7 | 2C 15 0A 27 04 17 1A |
| `$03` | `$CA:944E` (`0x0A944E`) | `0x0669` | `$FF` | `5` (ESA $C9) | 9 | 2F 3A 2D 1A 0C 0D 0E 10 04 |
| `$04` | `$CA:9AB9` (`0x0A9AB9`) | `0x02CA` | `$C0` | `6` (ESA $C1) | 10 | 1F 03 0D 0C 10 0E 04 0A 36 2B |
| `$05` | `$CA:9D85` (`0x0A9D85`) | `0x013C` | `$FF` | `A` (ESA $A1) | 3 | 23 14 29 |
| `$06` | `$CA:9EC3` (`0x0A9EC3`) | `0x0BE5` | `$C8` | `5` (ESA $C9) | 10 | 32 1D 1C 1A 02 03 04 1F 01 1B |
| `$07` | `$CA:AAAA` (`0x0AAAAA`) | `0x0148` | `$FF` | `F` (ESA $79) | 1 | 21 |
| `$08` | `$CA:ABF4` (`0x0AABF4`) | `0x05BC` | `$B0` | `4` (ESA $D1) | 9 | 17 2C 3A 19 18 13 02 0C 04 |
| `$09` | `$CA:B1B2` (`0x0AB1B2`) | `0x036E` | `$90` | `4` (ESA $D1) | 9 | 2C 3A 13 04 0A 09 1F 24 14 |
| `$0A` | `$CA:B522` (`0x0AB522`) | `0x0429` | `$F4` | `6` (ESA $C1) | 10 | 2B 19 23 11 05 06 02 03 07 20 |
| `$0B` | `$CA:B94D` (`0x0AB94D`) | `0x02E5` | `$B0` | `6` (ESA $C1) | 7 | 2C 24 09 25 0B 27 26 |
| `$0C` | `$CA:BC34` (`0x0ABC34`) | `0x0518` | `$B0` | `8` (ESA $B1) | 6 | 17 2C 24 09 25 27 |
| `$0D` | `$CA:C14E` (`0x0AC14E`) | `0x052A` | `$A0` | `6` (ESA $C1) | 7 | 2C 14 18 3A 27 09 16 |
| `$0E` | `$CA:C8C0` (`0x0AC8C0`) | `0x082A` | `$90` | `7` (ESA $B9) | 8 | 12 37 11 19 06 07 08 2C |
| `$0F` | `$CA:C67A` (`0x0AC67A`) | `0x0244` | `$EC` | `F` (ESA $79) | 3 | 2C 18 22 |
| `$10` | `$CA:D0EC` (`0x0AD0EC`) | `0x0125` | `$96` | `F` (ESA $79) | 2 | 3A 13 |
| `$11` | `$CA:D213` (`0x0AD213`) | `0x0140` | `$FF` | `5` (ESA $C9) | 2 | 2A 2E |
| `$12` | `$CA:D355` (`0x0AD355`) | `0x0057` | `$FF` | `5` (ESA $C9) | 1 | 28 |
| `$13` | `$CA:D3AE` (`0x0AD3AE`) | `0x09D1` | `$B8` | `8` (ESA $B1) | 8 | 12 1C 11 33 08 07 06 05 |
| `$14` | `$CA:DD81` (`0x0ADD81`) | `0x0371` | `$B8` | `5` (ESA $C9) | 9 | 35 22 2C 18 0C 10 0D 0E 04 |
| `$15` | `$CA:E0F4` (`0x0AE0F4`) | `0x05E7` | `$D0` | `7` (ESA $B9) | 10 | 32 1A 1C 2B 0C 02 03 1F 04 0A |
| `$16` | `$CA:E6DD` (`0x0AE6DD`) | `0x0675` | `$C4` | `6` (ESA $C1) | 10 | 2C 33 34 05 06 08 07 27 02 03 |
| `$17` | `$CA:ED54` (`0x0AED54`) | `0x08A7` | `$B8` | `5` (ESA $C9) | 7 | 17 1C 2C 14 15 04 3B |
| `$18` | `$CA:F5FD` (`0x0AF5FD`) | `0x0B0D` | `$C8` | `6` (ESA $C1) | 8 | 12 2C 19 18 09 25 27 1F |
| `$19` | `$CB:010C` (`0x0B010C`) | `0x0086` | `$FF` | `5` (ESA $C9) | 1 | 04 |
| `$1A` | `$CB:0194` (`0x0B0194`) | `0x008B` | `$FF` | `5` (ESA $C9) | 0 | (none) |
| `$1B` | `$CB:0221` (`0x0B0221`) | `0x03C0` | `$FF` | `7` (ESA $B9) | 8 | 24 2C 19 34 05 25 07 08 |
| `$1C` | `$CB:05E3` (`0x0B05E3`) | `0x01A3` | `$EC` | `B` (ESA $99) | 5 | 24 2C 19 25 27 |
| `$1D` | `$CB:0788` (`0x0B0788`) | `0x03A8` | `$FF` | `D` (ESA $89) | 5 | 37 2C 19 18 17 |
| `$1E` | `$CB:0B32` (`0x0B0B32`) | `0x05C0` | `$E8` | `D` (ESA $89) | 4 | 2C 18 17 38 |
| `$1F` | `$CB:10F4` (`0x0B10F4`) | `0x02FC` | `$FF` | `4` (ESA $D1) | 9 | 2C 15 13 14 2B 31 1F 04 1A |
| `$20` | `$CB:13F2` (`0x0B13F2`) | `0x0062` | `$FF` | `5` (ESA $C9) | 3 | 2E 08 3B |
| `$21` | `$CB:1456` (`0x0B1456`) | `0x007E` | `$FF` | `5` (ESA $C9) | 2 | 2E 39 |
| `$22` | `$CB:14D6` (`0x0B14D6`) | `0x0094` | `$FF` | `5` (ESA $C9) | 0 | (none) |
| `$23` | `$CB:156C` (`0x0B156C`) | `0x04C3` | `$A0` | `8` (ESA $B1) | 6 | 2C 13 14 17 09 27 |
| `$24` | `$CB:1A31` (`0x0B1A31`) | `0x00BF` | `$FF` | `F` (ESA $79) | 1 | 16 |
| `$25` | `$CB:1AF2` (`0x0B1AF2`) | `0x0122` | `$FF` | `F` (ESA $79) | 3 | 34 17 2C |
| `$26` | `$CB:1C16` (`0x0B1C16`) | `0x0141` | `$FF` | `F` (ESA $79) | 1 | 37 |
| `$27` | `$CB:1D59` (`0x0B1D59`) | `0x0586` | `$E0` | `5` (ESA $C9) | 11 | 24 1C 34 1A 05 25 36 0B 07 08 01 |
| `$28` | `$CB:22E1` (`0x0B22E1`) | `0x0923` | `$A8` | `4` (ESA $D1) | 9 | 2C 32 24 26 19 09 27 04 30 |
| `$29` | `$CB:2C06` (`0x0B2C06`) | `0x0807` | `$A8` | `6` (ESA $C1) | 10 | 24 34 1E 33 07 08 27 05 06 0B |
| `$2A` | `$CB:340F` (`0x0B340F`) | `0x0627` | `$B8` | `8` (ESA $B1) | 6 | 24 2C 17 09 25 27 |
| `$2B` | `$CB:3A38` (`0x0B3A38`) | `0x00D1` | `$FF` | `F` (ESA $79) | 1 | 37 |
| `$2C` | `$CB:3B0B` (`0x0B3B0B`) | `0x0319` | `$C8` | `3` (ESA $D9) | 8 | 33 1E 34 05 06 07 08 27 |
| `$2D` | `$CB:3E26` (`0x0B3E26`) | `0x044D` | `$F0` | `8` (ESA $B1) | 6 | 37 1A 12 3A 20 2F |
| `$2E` | `$CB:4275` (`0x0B4275`) | `0x0097` | `$FF` | `7` (ESA $B9) | 6 | 18 13 2C 15 17 3A |
| `$2F` | `$CB:430E` (`0x0B430E`) | `0x0486` | `$98` | `7` (ESA $B9) | 6 | 39 1C 15 2C 32 22 |
| `$30` | `$CB:4796` (`0x0B4796`) | `0x028B` | `$C0` | `6` (ESA $C1) | 6 | 2C 13 3A 09 27 3B |
| `$31` | `$CB:4A23` (`0x0B4A23`) | `0x04E9` | `$A8` | `5` (ESA $C9) | 9 | 2C 38 24 26 14 09 1F 04 27 |
| `$32` | `$CB:4F0E` (`0x0B4F0E`) | `0x0B5A` | `$A0` | `4` (ESA $D1) | 9 | 3A 34 1E 05 06 07 08 0B 27 |
| `$33` | `$CB:5A6A` (`0x0B5A6A`) | `0x0645` | `$E0` | `4` (ESA $D1) | 9 | 23 19 05 06 07 08 0B 27 01 |
| `$34` | `$CB:60B1` (`0x0B60B1`) | `0x008C` | `$FF` | `5` (ESA $C9) | 2 | 1B 30 |
| `$35` | `$CB:613F` (`0x0B613F`) | `0x00BA` | `$FF` | `5` (ESA $C9) | 0 | (none) |
| `$36` | `$CB:61FB` (`0x0B61FB`) | `0x038D` | `$CE` | `9` (ESA $A9) | 7 | 0F 09 19 0B 02 10 0C |
| `$37` | `$CB:658A` (`0x0B658A`) | `0x009B` | `$FF` | `D` (ESA $89) | 2 | 3A 23 |
| `$38` | `$CB:6627` (`0x0B6627`) | `0x08FD` | `$A2` | `3` (ESA $D9) | 9 | 12 2C 1C 34 13 33 1F 01 0B |
| `$39` | `$CB:6F26` (`0x0B6F26`) | `0x0D28` | `$EC` | `3` (ESA $D9) | 9 | 17 2C 16 24 26 09 25 27 04 |
| `$3A` | `$CB:7C50` (`0x0B7C50`) | `0x0105` | `$F0` | `D` (ESA $89) | 2 | 1B 3A |
| `$3B` | `$CB:7D57` (`0x0B7D57`) | `0x06A8` | `$C8` | `5` (ESA $C9) | 8 | 13 18 2C 27 14 1D 09 15 |
| `$3C` | `$CB:8401` (`0x0B8401`) | `0x074D` | `$B0` | `6` (ESA $C1) | 6 | 1C 13 3A 32 2C 19 |
| `$3D` | `$CB:8B50` (`0x0B8B50`) | `0x010F` | `$C8` | `8` (ESA $B1) | 5 | 24 2C 09 25 27 |
| `$3E` | `$CB:8C61` (`0x0B8C61`) | `0x0401` | `$FF` | `9` (ESA $A9) | 6 | 2C 18 21 15 13 14 |
| `$3F` | `$CB:9064` (`0x0B9064`) | `0x12D2` **>0F00**| `$E0` | `5` (ESA $C9) | 9 | 2C 17 13 18 19 26 1F 3B 3A |
| `$40` | `$CB:A338` (`0x0BA338`) | `0x011A` | `$A0` | `F` (ESA $79) | 2 | 2C 30 |
| `$41` | `$CB:A454` (`0x0BA454`) | `0x0115` | `$FF` | `6` (ESA $C1) | 3 | 3A 3C 3D |
| `$42` | `$CB:A56B` (`0x0BA56B`) | `0x0160` | `$CC` | `A` (ESA $A1) | 7 | 24 1D 1A 09 05 01 1F |
| `$43` | `$CB:A6CD` (`0x0BA6CD`) | `0x005A` | `$FF` | `5` (ESA $C9) | 0 | (none) |
| `$44` | `$CB:A729` (`0x0BA729`) | `0x0878` | `$A8` | `5` (ESA $C9) | 9 | 3A 18 13 19 09 05 06 07 08 |
| `$45` | `$CB:AFA3` (`0x0BAFA3`) | `0x0660` | `$B0` | `4` (ESA $D1) | 10 | 05 06 07 08 27 0B 19 3A 2B 34 |
| `$46` | `$CB:B605` (`0x0BB605`) | `0x09CF` | `$8F` | `5` (ESA $C9) | 10 | 2C 24 1E 19 27 07 08 06 05 37 |
| `$47` | `$CB:BFD6` (`0x0BBFD6`) | `0x01AE` | `$FF` | `A` (ESA $A1) | 0 | (none) |
| `$48` | `$CB:C186` (`0x0BC186`) | `0x0171` | `$A8` | `8` (ESA $B1) | 5 | 2C 3A 26 09 30 |
| `$49` | `$CB:C2F9` (`0x0BC2F9`) | `0x0958` | `$DC` | `4` (ESA $D1) | 9 | 32 1D 1A 1C 2B 05 25 04 1B |
| `$4A` | `$CB:CC53` (`0x0BCC53`) | `0x00F7` | `$FF` | `A` (ESA $A1) | 0 | (none) |
| `$4B` | `$CB:CD4C` (`0x0BCD4C`) | `0x006F` | `$FF` | `7` (ESA $B9) | 0 | (none) |
| `$4C` | `$CB:CDBD` (`0x0BCDBD`) | `0x0074` | `$FF` | `5` (ESA $C9) | 3 | 3A 1B 3E |
| `$4D` | `$CB:CE33` (`0x0BCE33`) | `0x04E0` | `$D8` | `A` (ESA $A1) | 6 | 21 2C 17 18 13 37 |
| `$4E` | `$CB:D315` (`0x0BD315`) | `0x0D29` | `$AC` | `7` (ESA $B9) | 7 | 34 24 26 19 09 25 27 |
| `$4F` | `$CB:E040` (`0x0BE040`) | `0x00FC` | `$FF` | `6` (ESA $C1) | 6 | 24 17 26 2C 09 27 |
| `$50` | `$CB:E13E` (`0x0BE13E`) | `0x074F` | `$D0` | `6` (ESA $C1) | 7 | 12 2C 19 18 09 25 27 |
| `$51` | `$CB:E88F` (`0x0BE88F`) | `0x0590` | `$FF` | `5` (ESA $C9) | 8 | 05 06 07 27 24 2C 2B 33 |
| `$52` | `$CB:EE21` (`0x0BEE21`) | `0x0341` | `$FF` | `A` (ESA $A1) | 3 | 3A 18 13 |

## Appendix 2 — all 62 instruments

| instr | sample ptr | payload len | BRR blocks | loop off (`$0BA4`) | `$0C20` word | `$0C9C` word |
|---|---|---|---|---|---|---|
| `$01` | `$C7:730D` (`0x07730D`) | `0x0EA0` (3744) | 416 | `0x0000` | `0000` | `E0FF` |
| `$02` | `$C7:81AF` (`0x0781AF`) | `0x07A1` (1953) | 217 | `0x0000` | `0020` | `E0FF` |
| `$03` | `$C7:8952` (`0x078952`) | `0x022E` (558) | 62 | `0x0000` | `00A0` | `E0FF` |
| `$04` | `$C7:8B82` (`0x078B82`) | `0x0FE4` (4068) | 452 | `0x0000` | `0078` | `E0FF` |
| `$05` | `$C7:9B68` (`0x079B68`) | `0x07AA` (1962) | 218 | `0x0000` | `0000` | `E0FF` |
| `$06` | `$C7:A314` (`0x07A314`) | `0x1194` (4500) | 500 | `0x0000` | `0000` | `E0FF` |
| `$07` | `$C7:B4AA` (`0x07B4AA`) | `0x09FC` (2556) | 284 | `0x0000` | `0000` | `E0FF` |
| `$08` | `$C7:BEA8` (`0x07BEA8`) | `0x0D2F` (3375) | 375 | `0x0000` | `0000` | `E0FF` |
| `$09` | `$C7:CBD9` (`0x07CBD9`) | `0x13B9` (5049) | 561 | `0x0000` | `0043` | `E0FF` |
| `$0A` | `$C7:DF94` (`0x07DF94`) | `0x0414` (1044) | 116 | `0x0000` | `0000` | `E0FF` |
| `$0B` | `$C7:E3AA` (`0x07E3AA`) | `0x1194` (4500) | 500 | `0x0000` | `0000` | `E0FF` |
| `$0C` | `$C7:F540` (`0x07F540`) | `0x0E8E` (3726) | 414 | `0x0000` | `0000` | `E0FF` |
| `$0D` | `$C8:03D0` (`0x0803D0`) | `0x0759` (1881) | 209 | `0x0000` | `00C1` | `E0FF` |
| `$0E` | `$C8:0B2B` (`0x080B2B`) | `0x0870` (2160) | 240 | `0x0000` | `00C0` | `E0FF` |
| `$0F` | `$C8:139D` (`0x08139D`) | `0x0948` (2376) | 264 | `0x0000` | `001E` | `E0FF` |
| `$10` | `$C8:1CE7` (`0x081CE7`) | `0x114C` (4428) | 492 | `0x0000` | `0000` | `E0FF` |
| `$11` | `$C8:2E35` (`0x082E35`) | `0x09C6` (2502) | 278 | `0x057C` | `00FE` | `E0FF` |
| `$12` | `$C8:37FD` (`0x0837FD`) | `0x0E7C` (3708) | 412 | `0x0DF5` | `00D3` | `E0FE` |
| `$13` | `$C8:467B` (`0x08467B`) | `0x0FFF` (4095) | 455 | `0x0F5D` | `9C52` | `EEFF` |
| `$14` | `$C8:567C` (`0x08567C`) | `0x03E7` (999) | 111 | `0x037B` | `0052` | `EAFF` |
| `$15` | `$C8:5A65` (`0x085A65`) | `0x1356` (4950) | 550 | `0x0000` | `0000` | `E0FF` |
| `$16` | `$C8:6DBD` (`0x086DBD`) | `0x0FA5` (4005) | 445 | `0x0F6F` | `27A9` | `E0FF` |
| `$17` | `$C8:7D64` (`0x087D64`) | `0x0BF4` (3060) | 340 | `0x07BC` | `00FF` | `E0FF` |
| `$18` | `$C8:895A` (`0x08895A`) | `0x05A9` (1449) | 161 | `0x0573` | `9C52` | `EDFF` |
| `$19` | `$C8:8F05` (`0x088F05`) | `0x078F` (1935) | 215 | `0x0642` | `FCAD` | `ECFF` |
| `$1A` | `$C8:9696` (`0x089696`) | `0x0C21` (3105) | 345 | `0x0BAC` | `00B6` | `F0FF` |
| `$1B` | `$C8:A2B9` (`0x08A2B9`) | `0x0F5D` (3933) | 437 | `0x0000` | `0000` | `E0FF` |
| `$1C` | `$C8:B218` (`0x08B218`) | `0x142E` (5166) | 574 | `0x13D4` | `74BB` | `EEFF` |
| `$1D` | `$C8:C648` (`0x08C648`) | `0x0E6A` (3690) | 410 | `0x0477` | `7FA0` | `E0FF` |
| `$1E` | `$C8:D4B4` (`0x08D4B4`) | `0x0D0B` (3339) | 371 | `0x0546` | `CC9F` | `E0FF` |
| `$1F` | `$C8:E1C1` (`0x08E1C1`) | `0x0AF8` (2808) | 312 | `0x0000` | `00C8` | `E0FF` |
| `$20` | `$C8:ECBB` (`0x08ECBB`) | `0x0EF1` (3825) | 425 | `0x0A3B` | `0000` | `F3FF` |
| `$21` | `$C8:FBAE` (`0x08FBAE`) | `0x1287` (4743) | 527 | `0x0F6F` | `A4D7` | `EFFF` |
| `$22` | `$C9:0E37` (`0x090E37`) | `0x0D02` (3330) | 370 | `0x0A5F` | `EC51` | `E0FF` |
| `$23` | `$C9:1B3B` (`0x091B3B`) | `0x1E7B` (7803) | 867 | `0x001B` | `0054` | `E0FF` |
| `$24` | `$C9:39B8` (`0x0939B8`) | `0x06C0` (1728) | 192 | `0x069C` | `6CE1` | `E0FF` |
| `$25` | `$C9:407A` (`0x09407A`) | `0x0E46` (3654) | 406 | `0x0000` | `007C` | `E0FF` |
| `$26` | `$C9:4EC2` (`0x094EC2`) | `0x0A95` (2709) | 301 | `0x0A71` | `00E1` | `E0FF` |
| `$27` | `$C9:5959` (`0x095959`) | `0x10AA` (4266) | 474 | `0x084C` | `00AF` | `EEFF` |
| `$28` | `$C9:6A05` (`0x096A05`) | `0x0DA4` (3492) | 388 | `0x0678` | `00C9` | `EEFF` |
| `$29` | `$C9:77AB` (`0x0977AB`) | `0x2E32` (11826) | 1314 | `0x0024` | `0000` | `E0FF` |
| `$2A` | `$C9:A5DF` (`0x09A5DF`) | `0x0C72` (3186) | 354 | `0x0000` | `0000` | `E0FF` |
| `$2B` | `$C9:B253` (`0x09B253`) | `0x09A2` (2466) | 274 | `0x0000` | `0043` | `E0FF` |
| `$2C` | `$C9:BBF7` (`0x09BBF7`) | `0x1D25` (7461) | 829 | `0x001B` | `F0E9` | `E0FF` |
| `$2D` | `$C9:D91E` (`0x09D91E`) | `0x0B76` (2934) | 326 | `0x0A9E` | `74E1` | `E0FF` |
| `$2E` | `$C9:E496` (`0x09E496`) | `0x08CA` (2250) | 250 | `0x0000` | `0000` | `E0FF` |
| `$2F` | `$C9:ED62` (`0x09ED62`) | `0x0AD4` (2772) | 308 | `0x0A83` | `00FE` | `EEFF` |
| `$30` | `$C9:F838` (`0x09F838`) | `0x0F5D` (3933) | 437 | `0x0000` | `0000` | `E0FF` |
| `$31` | `$CA:0797` (`0x0A0797`) | `0x0F5D` (3933) | 437 | `0x0000` | `0000` | `E0FF` |
| `$32` | `$CA:16F6` (`0x0A16F6`) | `0x0ADD` (2781) | 309 | `0x0264` | `00EE` | `E0FF` |
| `$33` | `$CA:21D5` (`0x0A21D5`) | `0x081F` (2079) | 231 | `0x0747` | `00A9` | `ECFF` |
| `$34` | `$CA:29F6` (`0x0A29F6`) | `0x0615` (1557) | 173 | `0x05FA` | `00A9` | `E0FF` |
| `$35` | `$CA:300D` (`0x0A300D`) | `0x0D2F` (3375) | 375 | `0x0000` | `0023` | `F4FE` |
| `$36` | `$CA:3D3E` (`0x0A3D3E`) | `0x057C` (1404) | 156 | `0x0000` | `0040` | `F4FF` |
| `$37` | `$CA:42BC` (`0x0A42BC`) | `0x05A9` (1449) | 161 | `0x0573` | `C851` | `EDFF` |
| `$38` | `$CA:4867` (`0x0A4867`) | `0x0A56` (2646) | 294 | `0x0A0E` | `00E1` | `E0FF` |
| `$39` | `$CA:52BF` (`0x0A52BF`) | `0x0E61` (3681) | 409 | `0x001B` | `00E1` | `E0FF` |
| `$3A` | `$CA:6122` (`0x0A6122`) | `0x1B75` (7029) | 781 | `0x0507` | `003C` | `E0FF` |
| `$3B` | `$CA:7C99` (`0x0A7C99`) | `0x033C` (828) | 92 | `0x0507` | `0000` | `E0FF` |
| `$3C` | `$CA:7FD7` (`0x0A7FD7`) | `0x0345` (837) | 93 | `0x0000` | `0000` | `E0FE` |
| `$3D` | `$CA:831E` (`0x0A831E`) | `0x0345` (837) | 93 | `0x0000` | `0000` | `E0FE` |
| `$3E` | `$CA:8665` (`0x0A8665`) | `0x0465` (1125) | 125 | `0x0000` | `0000` | `E0FE` |
