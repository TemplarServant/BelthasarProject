> ⛔ **CORRECTIONS — read before using this file** (adversarial verify pass, 2026-07-25; full detail
> in `audio_song_loader_verify.md`). This dossier's structural decode SURVIVED re-derivation, but
> three claims in it are wrong and one is unsupported:
>
> 1. **`$1E03`/`$1EF0`/`$1EF2` are NOT an "SFX-BANK index" / "ARAM ceiling".** `$1E03` is the DSP
>    **ECHO DELAY (EDL)**. Measured: `$F100 − ESA*$100 == EDL*2048`, and 98.7%/95.9% of exactly that
>    region is rewritten per 40 frames. §12's rationale — "the top of ARAM is reserved for the SFX
>    bank's own samples" — is uncited and false. `audio_song_data.md` had this right.
> 2. **§17 item 9 is REFUTED**: "mid-load savestates are not a reliable resume point" was an artefact
>    of the `--trace --from` 30-settle-frame trap (`src/main.c:5222`). Resuming `cb110.state` for 23
>    frames reproduces a cold boot to f133 bit-identically (fnv32=F2DF1C42).
> 3. **§2's "`$1E03` survives because SFX callers never write it" is UNSUPPORTED**: `C0/1C20` does
>    `LDA #FF / STA $1E03` right before `$1E00=$81`. Not load-bearing (the loader refills it), but
>    the stated reason is wrong.

# AUDIO front 1/3 — THE SONG LOADER, `$C7:01A1` … `$C7:0619`

**The routine that makes sound happen.** `audio_boot.md` put the Akao driver into ARAM;
`audio_commands.md` derived the mailbox and the dispatch; `audio_song_selection.md` derived *which*
id is asked for. This dossier decodes the ~1138 bytes that were still a black box: the code that
takes a song id and pushes its **sequence, its instrument samples, its sample directory, its
per-instrument parameter blocks and its per-song parameter block** into the SPC700's ARAM.

Every claim below is either
* **[DIS]** a `python tools/dis.py <flat> <len> [m] [x]` disassembly (HiROM: bank `$C7` = flat
  `0x70000 + addr`), or
* **[OBS]** an observed oracle value with the exact `build/ct.exe` command.

Gaps are in §17. **Nothing here is inferred from how sound drivers "usually" work.**

---

## 0. HEADLINE — the eleven facts the port needs

1. **The song loader is `$C7:01A1`–`$C7:0619`. It is one long, straight-line, blocking busy-wait.**
   There is **no** frame-wait, no `WAI`, no `$42xx` access anywhere in it (§14). It holds the 65816
   for **tens of frames** (21–30 observed for song `$1E`).
2. **`$1E03` = the song's required SFX-BANK index** (`record[0] & $0F`), not a pointer. If it differs
   from `$1EF0` the loader switches banks (`$C7:09FD`) and **culls** every loaded instrument whose
   sample end reaches the new ceiling `$1EF2 << 8` (`$C7:0A12`). **`$1E02` = `record[1]`, an opaque
   per-song scalar** passed to `$2142`. Both come from the 2-byte record at ROM **`$C7:241D + id*2`**.
3. **`$1E05` is not just a mutex — it IS the song-id register** for the whole loader
   (`$C7:01DC`, `$04FF`, `$059A` all read it). Bit 7 clear ⇒ every incoming command is dropped
   (`$C7:0157 LDA $05 / BPL ->0192`) — verified reading, and since the CPU is *blocked* for the whole
   load, the only caller that can ever hit that gate is an **interrupt-context** one. `$E0` = READY.
4. **The rolling token lives in `$1E84`.** It is seeded **per song-command** from the command byte:
   `$C7:0A7B INC A / AND #7F / STA $84` ⇒ `$11` for cmd `$10`. Then `$C7:09DA` advances it
   `(t+1) & $7F` per handshake, and every sub-transfer ends by forcing it back to **`$E0`**.
   Observed end-of-load value `$6D` — predicted exactly by the model (§3).
5. **Four driver commands are used**, written to `$2141` after the ARAM address goes to `$2142/$2143`:
   **`02`** = stream 2-byte units, **`03`** = stream 3-byte units, **`05`** = stream 3-byte units
   (SFX-bank / mode-swap variant), **`07`** = ARAM block MOVE (the defragmenter).
6. **Six ARAM destinations**: instrument BRR samples at `$1E60[slot]` (base **`$4901`**), the 64-byte
   **sample directory at `$1E80`** (= DSP `DIR $1E`, source numbers **`$20`–`$2F`**), 32 bytes at
   `$1F40`, 32 bytes at `$1FC0` (**ADSR**), the **sequence at `$2000`**, and a 36-byte per-song block
   at **`$F120`**.
7. **The instrument set is DIFFED, not reloaded.** `$1E20[16]` is the resident set; the loader
   evicts what the new song doesn't want, keeps what it does, appends the rest, and — if that leaves
   holes — asks the driver to **defragment ARAM** with command `07`.
8. **`$1E10`/`$1E11` = the driver MODE (0 = field, 1 = battle)**, and the mode selects which of
   **two** 6144-byte ROM blocks is written to **ARAM `$2F00`**: `$C7:4309` (mode 0, the IPL's block 1)
   or **`$C7:5B0B` (mode 1)**. Proven byte-for-byte against the oracle in both modes (§8).
   This is why `C0/1BAB` writes `$1E10 = $FF` *before* the battle song's cmd `$14`.
9. **`$1EF3` = "ARAM `$2F00` is dirty"**, set when the mode changes and when a sequence is
   `>= $0F00` bytes (it would overwrite `$2F00`). Exactly **one** song in the ROM is that long: `$3F`.
10. **`$C7:0A39` can ABORT the whole load** (`$0A80 LDA $2141 / BPL`): the play command has already
    been sent, but the CPU unwinds its own frame and exits without touching `$1E05` and without
    uploading anything. Observed live (2 aborts in a boot trace), and its fingerprint —
    `$1E84 = $11` on a fully-loaded ARAM — is sitting in `candy_shop_lake.state`.
11. **Bank `$C7` is now completely mapped** (§1): every table's extent is proven by *abutment*
    (each ends exactly where the next begins), 83 songs × 4 tables and 62 instruments × 4 tables.

---

## 1. BANK `$C7` — the complete map (extent-proven)

Every boundary below is proven twice: by the indexing arithmetic in the code, and by the fact that
`base + count*stride` lands **exactly** on the next table's base.

| range | stride×count | what | derived at |
|---|---|---|---|
| `$0000-$000F` | — | 4 entry `JMP`s | `audio_boot.md` §3 |
| `$0010-$001B` | 2×6 | IPL source table | `audio_boot.md` §2 |
| `$001C-$0027` | 2×6 | IPL dest table | `audio_boot.md` §2 |
| `$0028-$0037` | 2×7 | table-address list (unused) | `audio_boot.md` §2 |
| `$0038-$0A97` | — | **code** | this dossier |
| `$0A98-$0AD7` | 4×16 | cmd `$30-$3F` macro table | `audio_commands.md` §3 |
| `$0AD8` | — | song-exception list, first byte `$FF` ⇒ inert | `C7/0740` |
| `$0AE9` | 1 | song count = **`$53`** (83) | `C7/01A5` |
| `$0AEA-$0BA3` | **3×62** | **instrument → 24-bit sample pointer** | `C7/0666-0676`, `C7/0773-0783` |
| `$0BA4-$0C1F` | **2×62** | **instrument → BRR loop offset** | `C7/03AC` |
| `$0C20-$0C9B` | **2×62** | instrument → word pushed to ARAM `$1F40` | `C7/043C` |
| `$0C9C-$0D17` | **2×62** | instrument → word pushed to ARAM `$1FC0` (**ADSR**) | `C7/048A` |
| `$0D18-$0E10` | **3×83** | **song → 24-bit sequence pointer** | `C7/050F-051F` |
| `$0E11-$1870` | **32×83** | **song → 16-word instrument list** (0-terminated) | `C7/01EA`, `C7/0304` |
| `$1871-$241C` | **36×83** | **song → 36-byte block pushed to ARAM `$F120`** | `C7/05B2` + Y=id*36 |
| `$241D-$24C2` | **2×83** | **song → (SFX-bank byte, `$1E02` byte)** | `C7/0A45`, `C7/0A4B` |
| `$24C3-$40B3` | — | IPL block 0 (driver code, ARAM `$0200`) | `audio_boot.md` |
| `$40B4-$42B6` | — | IPL block 2 (ARAM `$4700`, 513 B) | `audio_boot.md` |
| `$42B7-$42DC` | — | IPL block 3 (ARAM `$1E00`, 36 B = **DSP dir srcn 0-8**) | `audio_boot.md` |
| `$42DD-$42F2` | — | IPL block 4 (ARAM `$1F80`, 20 B) | `audio_boot.md` |
| `$42F3-$4308` | — | IPL block 5 (ARAM `$1F00`, 20 B) | `audio_boot.md` |
| `$4309-$5B0A` | 2+6144 | **ARAM `$2F00` block, MODE 0** | `C7/0956` |
| `$5B0B-$730C` | 2+6144 | **ARAM `$2F00` block, MODE 1** ← *new* | `C7/0998` |
| `$730D` … `$CA:8ACB` | — | the 62 instrument BRR samples, contiguous | table `$0AEA` |
| `$CA:8ACC` … `$CB:F163` | — | the 83 song sequences, contiguous | table `$0D18` |

Arithmetic (all exact, `python`, ROM `Chrono_Trigger_USA_SNES.sfc`):
```
0AEA + 62*3 = 0BA4    0BA4 + 62*2 = 0C20    0C20 + 62*2 = 0C9C    0C9C + 62*2 = 0D18
0D18 + 83*3 = 0E11    0E11 + 83*32= 1871    1871 + 83*36= 241D    241D + 83*2 = 24C3  (= driver blob)
4309 + 2 + 0x1800 = 5B0B     5B0B + 2 + 0x1800 = 730D  (= instrument #1's pointer $C7:730D)
last instrument $CA:8665 + 2 + 1125 = $CA:8ACC  (= song $00's sequence pointer)
```
Every block-length word is a multiple of 9 for the instruments (BRR blocks) — e.g. 3744/9 = 416.

---

## 2. `$C7:0A39` — the record lookup, the first port block, the abort

`python tools/dis.py 0x70A39 0x60 1 0`:

```
0A39  PHP / REP #20
0A3C  LDA $01 / AND #007F / ASL A / TAX          ; X = (songId & $7F) * 2
0A43  SEP #20
0A45  LDA $C7241E,X / STA $02                    ; $1E02 = rom[$C7:241E + id*2]   (record byte +1)
0A4B  LDA $C7241D,X / AND #0F / STA $03          ; $1E03 = rom[$C7:241D + id*2] & $0F  (record byte +0)
0A53  CMP $F0                                    ; vs $1EF0 = the CURRENT SFX bank
0A55  BEQ ->0A5A
0A57  JSR C7/09FD                                ; ---- SFX-BANK SWITCH + CULL (§12)
0A5A  LDA $03 / STA $2143                        ; ---- THE FIRST PORT BLOCK (same order as $061C)
0A5F  LDA $02 / STA $2142
0A64  LDA $01 / STA $2141
0A69  LDA $00 / STA $2140                        ; the command byte = the trigger
0A6E  LDX #0400
0A71  CMP $2140 / BEQ ->0A7B / DEX / BNE ->0A71
0A79  BRA ->0A5A                                 ; timeout -> RE-SEND all four bytes (unbounded)
0A7B  INC A / AND #7F / STA $84                  ; ***** $1E84 = (cmd + 1) & $7F  = THE TOKEN SEED
0A80  LDA $2141
0A83  BPL ->0A96                                 ; bit7 CLEAR -> normal: PLP / RTS, the load proceeds
0A85  LDA $84 / STA $2140                        ; ---- ABORT PATH
0A8A  DEC A
0A8B  CMP $2140 / BEQ ->0A8B                     ; spin WHILE $2140 still reads (token-1)
0A90  PLA / PLA / PLA                            ; discard JSR-return(2) + PHP(1)  == unwind $0A39
0A93  JMP C7/0192                                ; -> STZ $1E00, restore, RTL.  NOTHING is uploaded,
                                                 ;    and $1E05 is NEVER set (it is set at $01B3).
```

**Answering "what are `$1E02`/`$1E03` FOR":**
* `$1E03` is the **SFX-bank index** the song needs. Two consumers, both here: the `CMP $F0` bank
  switch, and `$2143` as parameter 3 of the play command. The `AND #0F` is a no-op on this ROM —
  the high nibble of record byte 0 is `0` for **all 83** songs and the low nibble spans `$03..$0F`
  [OBS ROM dump of `$C7:241D`, stride 2, 83 entries].
* `$1E02` is a plain per-song scalar → `$2142`. It is **not** a pointer and **not** a size: every
  table in bank `$C7` is indexed by the song id itself, and the values (`$8F`…`$FF`) correlate with
  no address. Its SPC-side meaning is **NOT DERIVED** (§17).
* Nothing later in `$01A1-$0619` reads `$1E02` or `$1E03` again (operand scan of the region).

**The 83 records** (first column of each pair = `$1E03` bank, second = `$1E02`), e.g.
`00: 05 FF · 06: 05 C8 · 0F: 0F EC · 1E: 0D E8 · 42: 0A CC · 45: 04 B0`.
[OBS] cross-checks of `$1E02/$1E03` and `$1EF0/$1EF2` on six independent states:

```
build/ct.exe --read "wram:7E29AE+1,wram:1E00+8,wram:1EF0+4,wram:1E10+2" --from <state>

bedslide.state             29AE=0F  1E02/03=80/0F  1EF0/F2=0F/79   mode 0/0
build/gato_idle400.state   29AE=00  1E02/03=80/04  1EF0/F2=04/D1   mode 1/1
build/leg1.state           29AE=1E  1E02/03=E8/0D  1EF0/F2=0D/89   mode 0/0
saves/truce_market.state   29AE=1E  1E02/03=E8/0D  1EF0/F2=0D/89   mode 0/0
saves/ow_w0.state          29AE=1E  1E02/03=E8/0D  1EF0/F2=0D/89   mode 0/0
saves/gato_inbattle.state  29AE=00  1E02/03=80/04  1EF0/F2=04/D1   mode 1/1
```
Song `$0F`'s record bank is `$0F` → `$1EF0 = 0F`, `$1EF2 = $F1-$78 = $79` ✔.
Song `$1E`'s is `$0D` → `$89` ✔. Song `$45` (battle) is `$04` → `$D1` ✔.
(`$1E02 = 80` in the two `$xx/04` rows is a *stale* SFX value — see `audio_commands.md` §10.4;
`$1E03` survives because SFX callers never write it.)

**The abort is real.** `build/ct.exe --trace 4000000` (cold boot): `$C7:0A7B` executed 4 times,
`$0A96` (normal) 2 times, **`$0A85`/`$0A90`/`$0A93` 2 times each** (first at instruction 768419;
`$0A8B` spun 2531 times). And `candy_shop_lake.state` carries the fingerprint: a fully-loaded song-6
ARAM but `$1E84 = $11` = `(cmd $10)+1`, i.e. the token was seeded and then the routine bailed at
`$0A83` — an aborted duplicate request. **What makes the SPC set `$2141` bit 7 is NOT DERIVED** (§17).

---

## 3. `$C7:09DA` / `$C7:09EA` — the rolling token

`python tools/dis.py 0x709DA 0x30 1 0`:
```
09DA  PHP / SEP #20
09DD  STA $2140                      ; write the token
09E0  CMP $2140 / BNE ->09E0         ; spin until the SPC echoes it
09E5  INC A / AND #7F                ; token' = (token+1) & $7F   <-- returned in A
09E8  PLP / RTS

09EA  PHP / SEP #20                  ; RESYNC (used only when the driver fails to ack a command)
09ED  LDA $84 / STA $2140
09F2  CMP $2140 / BNE ->09F2
09F7  LDA #E0 / STA $84              ; force the token back to $E0
09FB  PLP / RTS
```

**Per-transfer, not per-chunk, and seeded from the command byte.** The seed is set once per song
command at `$0A7B` (`$11` for cmd `$10`, `$12` for `$11`, `$15` for `$14`, `$16` for `$15`).
Every sub-transfer then **ends** with `LDA #E0 / STA $84` (`$0322`, `$0377`, `$0409`, `$0457`,
`$04A5`, `$0596`, `$0717`, `$0993`, `$09D5`), so each subsequent sub-transfer *begins* by sending
`$E0` and the token becomes `($E0+1) & $7F = $61`, then `$62`, `$63`, … rolling mod `$80`.

**Numeric proof of the model** — song `$1E` (4 instruments, sequence 1472 B):
the last sub-transfer is the 36-byte block at `$F120`: 1 command handshake + 12 data handshakes,
starting from `$E0` ⇒ `$E0 → $61 → $62 … → $6D`. Predicted final `$1E84 = $6D`.
[OBS] `build/ct.exe --read "wram:1E82+8" --from tp12.state` → `68 01 6D 00 …` ⇒
`$1E82 = $0168 = $1E*12` ✔ and **`$1E84 = $6D`** ✔.

[OBS] The token climbing *during* the load (`--teleport 12 8 8 0 --from candy_shop_lake.state
--frames N --out sN.state` then `--read "wram:1E00+8,wram:1E84+1" --from sN.state`):
```
frames  $1E00..$1E07                 $1E84
  1     10 1E E8 0D 00 1E 00 00       77      <- $1E00=cmd$10 latched, $1E05=$1E == BUSY
  2..20 10 1E E8 0D 00 1E 00 00       03,0C,16,2B,41,6B,1B,46,32 ...
  30    00 1E E8 0D 00 E0 00 00       6D      <- $1E00 consumed, $1E05=$E0 READY, token = $6D
```

`$C7:09EA` was **never executed** in either trace — the retry path is derived but untested.

---

## 4. The ARAM-write protocol (the shape every upload uses)

Every one of the six upload sites in the loader has exactly this shape:

```
setup:   $2143 = destHi ; $2142 = destLo ; $2141 = CMD
         tok = handshake(tok)                       ; JSR $09DA
         if ($2141 != CMD) { JSR $09EA ; goto setup }   ; the driver echoes the command on its port 1
stream:  repeat: $2141 = b0 [; $2142 = b1 [; $2143 = b2]] ; tok = handshake(tok)
done:    tok = $E0
```

| CMD | unit | used by | ARAM dest |
|---|---|---|---|
| `02` | 2 bytes (`$2141`,`$2142`) | `$03C8` sample directory | `$1E80`, 32 units = 64 B |
| `02` | 2 bytes | `$040D` | `$1F40`, 16 units = 32 B |
| `02` | 2 bytes | `$045B` | `$1FC0`, 16 units = 32 B |
| `03` | 3 bytes (`$2141`,`$2142`,`$2143`) | `$0655` per instrument | `$1E60[slot]` |
| `03` | 3 bytes | `$04DB` sequence | `$2000` |
| `03` | 3 bytes | `$059A` per-song block | `$F120`, 12 units = 36 B |
| `03`/`05` | 3 bytes | `$0922` mode block | `$2F00`, 2048 units = 6144 B |
| `05` | 3 bytes | `$07C3` (cmd `$71`, SFX bank) | `$1E60[$1EFC]` |
| `07` | 3×2 bytes (src, dst, len) | `$025A` defragmenter | — |

**Iteration counts are exact and differ between sites:**
* sequence (`$0571`/`$0591`): `DEX DEX DEX / **BPL**` ⇒ `floor(len/3) + 1` units, i.e. it always
  sends a multiple of 3 bytes and may read 1–2 bytes past the end of the ROM block.
* instrument sample (`$06F2`/`$0712`) and the `$2F00` block (`$098C`): `DEX DEX DEX / **BNE**` ⇒
  exactly `len/3` units (all those lengths are multiples of 9).
* `$F120` block: `LDX #000C` / `DEX / BNE` ⇒ exactly 12 units.

[OBS] Proof of the sequence formula: `--trace 4000000` counts `$C7:0578` (the fast 3-byte loop
body) **231** times across the boot's two successful loads. Songs `$00` (len 36) and `$30`
(len 651) ⇒ `(36/3+1) + (651/3+1) = 13 + 218 = 231` ✔ **exactly**.

Both the sequence and the instrument uploads pick between a **fast** path (plain `INY`) and a
**bank-crossing** path (`INY / BNE +2 / INC $14`) with
`REP #20 / TYA / CLC / ADC $0C / SEP #20 / BCC fast` — i.e. "does the block fit without the 16-bit
offset wrapping". [OBS] the slow path (`$054C`, `$06CD`) never executed in either trace.

---

## 5. `$C7:01A1-$0325` — the instrument DIFF engine

```
01A1  SEP #20 / LDA $01 / CMP $C70AE9 / BCS ->0192      ; id >= $53 -> drop
01AB  JSR C7/0734       ; cmd >= $14 -> $1E00 |= 4 ; else scan $C7:0AD8 (first byte $FF -> inert)
01AE  JSR C7/0A39       ; §2
01B1  LDA $01 / STA $05 ; ***** $1E05 = song id  ==  BUSY, and the loader's own id register
01B5  REP #20 / LDX #0020 / { STZ $1EFE,X ; STZ $1F1E,X ; DEX DEX } BNE
                        ; clears $7E:1F00[16] (the song's full set) and $7E:1F20[16] (the to-load set)
01C4  LDY #0020 / { LDA $1E1E,Y ; BNE out ; DEY DEY } BNE
01D0  STY $0C           ; $1E0C = 2*(slot+1) of the HIGHEST occupied $1E20 slot (0 if none)
01D2  $12 = $1F00 ; $14 = $1F20
01DC  LDA $04 / AND #FF00 / LSR /LSR /LSR / TAX ; X = songId*32   (the high byte of the word at
                                                ;  $1E04 IS $1E05 -- the id stored at $01B3)
01E5  ADC #0020 / STA $0E                       ; $1E0E = end index
01EA  LDA $C70E11,X / BEQ ->020F                ; walk the song's 16-word instrument list
01F0    STA ($12) / INC $12 x2                  ;   append to $1F00  (always)
01F6    LDY $0C / BEQ ->0203
01FA    { CMP $1E1E,Y ; BEQ already ; DEY DEY } BNE   ; already resident?
0203    STA ($14) / INC $14 x2                  ;   append to $1F20  (only if NOT resident)
0209  INX INX / CPX $0E / BNE ->01EA
020F  LDA $1F20 / BNE ->0217
0214  JMP C7/037B                               ; nothing new to load -> straight to the pushes
0217  { LDA $20,X ; BEQ ->0238 ;                ; EVICT: for every resident instrument
        { CMP $1F00,Y ... } not found -> STZ $20,X   ;   not wanted by the new song -> free the slot
        INX INX } CPX #0020
0238  { LDA $20,X ; BEQ ->0246 ; INX INX } ; STX $0C ; $1E0C = 2*(first FREE slot)
0248  { LDA $1E,X ; BNE ->0253 ; DEX DEX }      ; X = 2*(last OCCUPIED slot + 1)
0253  CPX $0C / BNE ->025A
0257  JMP C7/0326                               ; free space is CONTIGUOUS at the end -> just append
```

**`$1E20[16]` is the resident instrument set, `$1E40[16]` their lengths, `$1E60[17]` their ARAM
start addresses** (`$1E60[k+1]` is written as `$1E60[k] + len[k]`, so the array doubles as a
17-entry "start/end" fence). All three are byte-verified in §16.

### 5a. `$025A` — the DEFRAGMENTER (driver command `07`)
Reached when the free slots are *interleaved* with occupied ones.
```
025A  SEP #20 / LDA #07 / STA $2141 ; LDY #0000 ; STY $0C
0266  for Y = 0,2,..,$1E:
0268    if $1E20,Y != 0: continue                     ; slot Y already occupied
026D    find the next occupied slot X > Y ; none -> BRL $0320
027E    $2142/$2143 = $1E60,X            (SOURCE addr) ; tok = handshake(tok)
0290    first time only: if ($2141 != 07) -> $02E4     ; driver refuses command 07
029D    $1E20,Y = $1E20,X ; $1E20,X = 0
02A6    $2142/$2143 = $1E60,Y            (DEST addr)
02B5    $1E62,Y = $1E60,Y + $1E40,X                    ; new end fence
02BD    tok = handshake(tok)
02C6    $1E40,Y = $1E40,X ; $2142/$2143 = it (LENGTH) ; tok = handshake(tok)
02DB  next Y  ;  BRA $0320
02E4  ---- the driver did NOT ack command 07: ----
02E6    $2142 = $2143 = 0 ; tok = handshake(tok)
02F5    for Y = $20 down to 2:  $1F00+Y-2 = $1F20+Y-2 = rom[$C7:0E11 + (id+1)*32 - Y ... ]
                                $1E20+Y-2 = 0 ; $1E40+Y-2 = 0 ; $1E62+Y-2 = 0
        ==> throw the whole resident set away and rebuild both lists from the song's full 16-word list
0320  SEP #20 / LDA #E0 / STA $84   (falls through into $0326)
```
[OBS] Neither `$025A` nor `$02E4` executed in any trace taken here — **derived, untested**.

### 5b. `$C7:0326` — load every instrument in `$1F20`
(Also the target of the vestigial entry stub `$C7:0008`; `audio_boot.md` §3 called it "voice/slot
table compaction" — it is the instrument-load loop.)
```
0326  SEP #20 / STZ $0A                     ; $1E0A: written here, NO reader anywhere (§17)
032A  X = 2*(first free slot); if none, wipe $1E20[16] and X = 0
033F  for Y = 0,2,..,$1E:
0344    A = $1F20,Y ; if 0 -> $0361
0349    $1E20,X = A ; $1E1C = X ; $1E1E = Y ; JSR C7/0655      ; upload it
0356    Y += 2 ; X += 2
0361  (list exhausted)  $1EF6 = $1E60,X                        ; next free ARAM address
0367    zero $1E62,X.. and $1E40,X.. up to index $20
0375  SEP #20 / LDA #E0 / STA $84           (falls through into $037B)
```

### 5c. `$C7:0655` — upload ONE instrument
```
0655  PHP / REP #20
0658  LDX $1C ; A = $1E20,X ; X = (A-1)*3                       ; instrument ids are 1..62
0664  SEP #20 ; $12/$13/$14 = rom[$C7:0AEA + (instr-1)*3]        (24-bit sample pointer)
0678  LDY $12 ; STZ $12 ; STZ $13        ; -> [$12],Y with the 16-bit offset in Y (bank-carry via $14)
067E  LDX $1C ; $2142 = $1E60,X ; $2143 = $1E61,X ; $2141 = 03   ; DEST = the slot's ARAM start
068F  tok = handshake(tok) ; if ($2141 != 03) { JSR $09EA ; retry from $0656 }
06A2  len.lo = [$12],Y -> $1E0C, $1E40,X ; $1E62,X = $1E60,X + len.lo
06B2  len.hi = [$12],Y -> $1E0D, $1E41,X ; $1E63,X = $1E61,X + len.hi + carry
06C1  X = len ; choose fast/slow by (Y + len) carry
06CD/06F9  stream len/3 units of 3 bytes, one handshake each
0717  LDA #E0 / STA $84 ; PLP / RTS
```
[OBS] `$C7:0655` ran 6 times in the boot trace; `$C7:0326` once (reached from `$0257`).

---

## 6. `$C7:037B-$04A7` — the three parameter pushes

### 6a. `$037B` — build and push the **SAMPLE DIRECTORY** to ARAM `$1E80`
```
037B  REP #20 / $12 = $1F80                       ; a WRAM staging buffer at $7E:1F80
0385  for X = 0,2,..,$1E:
0385    A = $1F00,X ; if 0 -> BREAK to $03C8       ; <-- BREAK, so the tail of $1F80 stays STALE
038A    find its slot: { CMP $1E20,Y ; Y += 2 } CPY #0040        (note: scans to $1E5F)
0399    not found -> write 0,0
039E    found     -> A = $1E60,Y ; *$12++ = A ; A += rom[$C7:0BA4 + (instr-1)*2] ; *$12++ = A
                     i.e. (sampleStart, sampleStart + BRR loop offset)
03C8  $2143=$1E ; $2142=$80 ; $2141=02 ; handshake ; verify $2141==02 (else $09EA + retry)
03EC  for X = 0..$3E step 2: $2141 = $1F80,X ; $2142 = $1F81,X ; handshake        (32 units, 64 B)
0409  LDA #E0 / STA $84
```
**ARAM `$1E80` is the DSP sample directory for source numbers `$20`–`$2F`.**
[OBS] `build/ct.exe --aram --from candy_shop_lake.state` reports `dsp dir=$1E00` and the live
sounding voices carry `srcn=22,23,24,26,27,29` — all inside `$20-$2F`, i.e. dir entries
`$1E00 + srcn*4` ∈ `$1E80..$1EBF`. IPL block 3 (36 B at ARAM `$1E00`) is srcn `$00-$08`.

Because of the **BREAK** at `$0388`, the push always sends 64 bytes but only refreshes the first
`N*4`. [OBS] after loading song `$1E` (4 instruments) over song `$06` (10 instruments), ARAM
`$1E90-$1EAF` still holds song `$06`'s entries verbatim. **`$7E:1F80` must be modelled as
persistent WRAM.**

### 6b. `$040D` → ARAM `$1F40`, and `$045B` → ARAM `$1FC0`
```
040D  $2143=$1F ; $2142=$40 ; $2141=02 ; handshake+verify
0431  for Y = 0,2,..,$1E: X = ($1F00,Y - 1)*2 ; A = rom[$C7:0C20,X]
                          $2141 = A.lo ; $2142 = A.hi ; handshake
045B  same with $2142=$C0 and the table rom[$C7:0C9C,X]
```
For an **unused** slot (`$1F00,Y == 0`) the arithmetic gives `X = $FFFE`, and `LDA long,X` on the
65816 carries into the bank byte: `$C7:0C20 + $FFFE = $C8:0C1E` and `$C7:0C9C + $FFFE = $C8:0C9A`.
[OBS] rom `$C8:0C1E = 02 05` and `$C8:0C9A = C2 1F`, and the oracle's ARAM shows exactly
`0205` in every unused `$1F40` slot and `C21F` in every unused `$1FC0` slot — on **three**
independent states (cold boot @frames 30 with song `$00`, 0 instruments → 16×; song `$06`,
10 instruments → 6×; song `$1E`, 4 instruments → 12×). **The "garbage" is fully derived.**

`$C7:0C9C` is **ADSR**: sent as `(low, high)`, so instrument 44's word `E0FF` goes out as
`FF, E0` = ADSR1 `$FF` (enable, AR 15, DR 7) + ADSR2 `$E0` (SL 7, SR 0). [OBS] ARAM `$1FC0` for
song `$1E` = `FFE0 FFED FFE0 FFE0` ↔ ROM entries for instruments `$2C,$18,$17,$38` = `E0FF, EDFF,
E0FF, E0FF` ✔. What `$C7:0C20` means SPC-side is **NOT DERIVED**.

### 6c. `$04A9` — `$1EFC`
```
04A9  LDA $1F00 (8-bit) ; if 0 -> JMP $04C0
04B1  { LDX #0020 ; CMP $1E,X ; BEQ found ; DEX DEX } BNE
04BC  DEX DEX ; STX $FC          ; $1EFC = 2 * (highest slot holding the song's FIRST instrument)
```
**Consumers of `$1EFC`: `$C7:079F`, `$07B7`, `$083F`, `$08DA` — all inside the cmd-`$71` handler**
(load an SFX bank), which overwrites exactly that slot after checking the new sample fits inside
`[$1E60,X, $1E62,X)`. (Operand scan of bank `$C7` for direct-page `$FC`; every other hit is at
`>= $0BA4`, i.e. inside the data tables / driver blob, not code.)

---

## 7. Sizes actually moved (why the load is expensive)

For song `$1E` (`2C 18 17 38`, 4 instruments): 7461 + 1449 + 3060 + 2646 = **14,616 bytes** of BRR
(cross-check: `$1E60[4] - $1E60[0]` = `$8219 - $4901` = `$3918` = 14616 ✔), plus 1473 bytes of
sequence, plus 64 + 32 + 32 + 36 = 164 bytes of parameters ⇒ **16,253 bytes**, at
**3 bytes per two-way rendezvous** (2 for the `02` pushes).
Song `$06` moves 10 instruments totalling **31,806** bytes (`$C53F - $4901` ✔) + 3048 + 164 =
**35,018 bytes** — which is why `audio_commands.md` saw a 59-frame BUSY window for song `$45`.

---

## 8. `$04C0-$04D9` — the DRIVER MODE and the `$2F00` block swap  ⭐

```
04C0  SEP #20
04C2  LDA $10 / CLC / ADC #FF / LDA #00 / ROL A     ; A = ($1E10 != 0) ? 1 : 0   (normalize)
04CA  CMP $11 / BEQ ->04D4
04CE  STA $10 / STA $11 / INC $F3                   ; mode changed -> mark $2F00 DIRTY
04D4  LDA $F3 / BEQ ->04DB
04D8  JSR C7/0922                                   ; re-upload the $2F00 block
```
`$C7:0922` (`python tools/dis.py 0x70922 0xC0 1 0`):
```
0922  $2143=$2F ; $2142=$00                        ; ARAM $2F00
092E  $2141 = ($1E00 == $70) ? 05 : 03 ; STA $86 ; handshake ; verify against $86 (else $09EA+retry)
0952  LDA $10 / BNE ->0998
0956  MODE 0: len = rom[$C7:4309] ($1800) ; ptr = $C7:430B ; stream 2048x3 bytes
0991          STZ $F3 ; $84 = $E0 ; RTS
0998  MODE 1: len = rom[$C7:5B0B] ($1800) ; ptr = $C7:5B0D ; stream 2048x3 bytes
09D3          STZ $F3 ; $84 = $E0 ; RTS
```

**[OBS] BYTE-EXACT PROOF, both modes:**
```
build/ct.exe --read "wram:1E10+2" --from <state>
build/ct.exe --aram --from <state> --at 2F00+0x20

candy_shop_lake.state   $1E10/$1E11 = 00 00   ARAM $2F00 = 00 33 00 00 07 33 00 00 12 33 1C 33 2B 33 2E 33 ...
ROM $C7:430B                                              00 33 00 00 07 33 00 00 12 33 1C 33 2B 33 2E 33 ...   ✔
saves/gato_inbattle.st  $1E10/$1E11 = 01 01   ARAM $2F00 = 00 33 00 00 07 33 09 33 16 33 20 33 2F 33 32 33 ...
ROM $C7:5B0D                                              00 33 00 00 07 33 09 33 16 33 20 33 2F 33 32 33 ...   ✔
```
So **ARAM `$2F00` holds a mode-selected 6144-byte table**, and `C0/1BAB`'s `LDA #FF / STA $1E10`
before the battle song's cmd `$14` is precisely the request to swap it. Command `$70`
(`$C7:08E3`) performs the same swap standalone, seeding its own token with `$FE`
(`STA $2140 / CMP / INC A / AND #7F` ⇒ `$84 = $7F`) and sending `05` instead of `03`.

**`$1EF3` is a closed loop.** Operand scan of bank `$C7` code (`< $0A98`) for direct-page `$F3`:
writers `$04D2` (mode change) and `$0540` (long sequence), reader `$04D4`, cleared `$0991`/`$09D3`.
Nothing else touches it.

---

## 9. `$04DB-$0598` — THE SEQUENCE UPLOAD

```
04DB  $2143=$20 ; $2142=$00 ; $2141=03 ; handshake ; verify $2141==03 (else $09EA + retry)
04FF  REP #20 / LDA $05 / AND #00FF / STA $82 / ASL / CLC / ADC $82 / TAX   ; X = songId*3
050D  SEP #20 / $12/$13/$14 = rom[$C7:0D18 + id*3]         ; 24-bit sequence pointer
0521  LDY $12 ; STZ $12 ; STZ $13                          ; long-indirect with the offset in Y
0527  $1E0C.lo = [$12],Y ; INY(carry)                      ; the block's LENGTH word
0530  $1E0D    = [$12],Y ; INY(carry)
0539  LDX $0C / CPX #0F00 / BCC ->0542 / INC $F3           ; >= $0F00 would spill into ARAM $2F00
0542  REP #20 / TYA / CLC / ADC $0C / SEP #20 / BCC ->0578 ; fits in the bank?
054C  slow (bank-crossing) : 3 bytes -> $2141/$2142/$2143 ; handshake ; DEX x3 ; BPL
0578  fast                 : same, plain INY
0596  LDA #E0 / STA $84
```
**Source = ROM `$C7:0D18 + id*3` → `[ptr] = length word`, payload follows. Destination = ARAM
`$2000`. The length is known from the block's own header, not from a table.**

[OBS] byte-exact: song `$1E`'s pointer is `$CB:0B32`, length `$05C0`, payload
`58 0B F4 10 58 0B 1D 0C B4 0C 13 0D 5B 0D B6 0D …` — and
`build/ct.exe --aram --from tp12.state --at 2000+0x20` prints
`aram: 2000 580BF410580B1D0CB40C130D5B0DB60D` ✔.

**Only song `$3F` (length 4818 = `$12D2`) trips the `$0F00` test** (exhaustive scan of the 83
sequence lengths). `$2000 + $0F00 = $2F00` exactly — the flag exists because a long sequence
overwrites the mode block. Note the ordering: `$1EF3` is bumped **after** `$0922` has already run
this pass, so the `$2F00` restore happens on the **next** song load.

---

## 10. `$059A-$05FD` — the 36-byte per-song block → ARAM `$F120`

```
059A  REP #20 / A = $1E05 ; $82 = id ; A = id*3 ; A = id*12 ; $82 = id*12 ; A = id*36 ; TAY
05B2  SEP #20 / $12/$13/$14 = $C7:1871                     ; base pointer (literal)
05BE  $2143=$F1 ; $2142=$20 ; $2141=03 ; handshake ; verify (else $09EA + retry from $059A)
05E0  LDX #000C ; 12 x { 3 bytes -> $2141/$2142/$2143 ; handshake }
05FF  (falls into $0601)
```
[OBS] byte-exact. ROM `$C7:1871 + 6*36` =
`00 00 00 00 00 00 25 3E 2C 27 4B 11 28 4C 40 00 …` and
`build/ct.exe --aram --from candy_shop_lake.state --at F120+0x30` prints
`F120 000000000000253E2C274B11284C4000 / F130 000000000024374026446E0000000000 / F140 00243B54…` ✔.
For song `$1E` (whose 36 bytes are all zero in ROM) the oracle's ARAM `$F120` is all zero ✔.
[OBS] `$1E82 = 0x0048 = 6*12` on the song-6 state and `0x0168 = 0x1E*12` on the song-`$1E` state —
the intermediate of that multiply chain, left in WRAM ✔.

---

## 11. `$0601-$0619` — GO, release, cull

```
0601  SEP #20 / LDA #E0 / LDX #0400
0606  STA $2141 / STA $2140                 ; the "everything is uploaded" token, $E0
060C  CMP $2140 / BEQ ->0614 / DEX / BNE    ; 1024-iteration timeout, then continue ANYWAY
0614  STA $05                               ; ***** $1E05 = $E0  == READY
0616  JSR C7/0A12                           ; the cull (§12)
0619  JMP C7/0192                           ; STZ $1E00 ; PLY/PLX/PLA/PLP/PLD/PLB ; RTL
```
[OBS] boot trace: `$0601` ×2, `$060C` ×53, `$0611` ×51, `$0614`/`$0616`/`$0619` ×2 each.

---

## 12. `$C7:09FD` (bank switch) and `$C7:0A12` (the cull)

```
09FD  PHP / SEP #20
09FE  STA $F0                         ; $1EF0 = the new SFX bank (A = $1E03, or $1E01&$0F for cmd $FC)
0A02  ASL / ASL / ASL / EOR #FF / INC A / CLC / ADC #F1 / STA $F2   ; $1EF2 = ($F1 - bank*8) & $FF
0A0D  JSR C7/0A12 ; PLP / RTS

0A12  PHP / SEP #20 / LDX #0000
0A18  LDA $63,X                       ; HIGH byte of the slot's END address ($1E62 + X)
0A1A  BEQ ->0A37                      ; 0 -> empty slot -> stop
0A1C  CMP $F2 / BCS ->0A29            ; end >= ceiling -> CULL from this slot onward
0A20  INX INX / CPX #0020 / BNE ->0A18
0A29  LDA #00 ; { STA $20,X ; STA $40,X ; STA $62,X ; INX } CPX #0020   <-- note: BYTE stride
0A37  PLP / RTS
```

**Meaning, fully derived:** the SFX bank index sets a **ceiling in ARAM**, `$1EF2 << 8`
(bank 4 → `$D100`, bank 5 → `$C900`, bank 13 → `$8900`, bank 15 → `$7900`) — the top of ARAM is
reserved for the SFX bank's own samples. The cull **evicts every resident instrument whose sample
ends at or above that ceiling**, and (because the loop from `$0A2B` runs byte-wise to index `$1F`)
everything after it too.

**When does a song load trigger a bank change?** Only at `$C7:0A53`, when
`record[0] & $0F != $1EF0`. [OBS] boot trace: `$09FD` executed **once** across the whole window,
`$0A12` **three** times (once from `$09FD`, twice from `$0616`), and `$0A29` (the cull body)
**zero** times — with the real song sets the ceiling is never actually breached in that window.
Cross-check across states: song `$06` (bank 5, ceiling `$C900`) allocates up to `$1EF6 = $C53F`;
song `$1E` (bank `$0D`, ceiling `$8900`) up to `$8219`. Both fit ✔.

The **other** caller of `$09FD` is `$C7:064F`, inside the raw pass-through, for command `$FC` —
so `$FC` is "set SFX bank" and its parameter is the same nibble (`audio_commands.md` §4).

---

## 13. `$1E05` — the answer to mandate item 6

**Confirmed, and it is more than a mutex.**

* Set to the song id at **`$C7:01B3`** (`LDA $01 / STA $05`) — a value `< $53`, so **bit 7 is clear**.
* Restored to `$E0` at **`$C7:0614`** and at boot (`$C7:013B`, `audio_boot.md` §4c).
* The gate is **`$C7:0157 LDA $05 / BPL ->0192`**: bit 7 clear ⇒ jump to the common exit, which does
  `STZ $1E00` and returns. **Your reading is correct: while a song is loading, every command —
  song, SFX, fade, pause — is silently discarded and its command byte is erased.**
* **But it is also the loader's own song-id register**: `$01DC LDA $04 / AND #FF00` (the high byte of
  the word at `$1E04` *is* `$1E05`), `$04FF/$0501 LDA $05`, `$059A/$059C LDA $05`. The instrument
  list, the sequence pointer and the `$F120` block are all indexed off `$1E05`, **not** off `$1E01`.
  A port that models `$1E05` as a boolean busy flag will index the wrong tables.
* **There is no other writer in the ROM.** Exhaustive byte scan for `STA/STZ/STX/STY $1E05`
  (`8D/9C/8E/8C/9D/99 05 1E`) and the long forms (`8F 05 1E 7E|00`): the only three hits
  (`$FA:E733`, `$E6:6A65`, `$EC:7008`) are inside compressed data banks, not code.
* **When can the gate actually fire?** The loader **blocks the CPU** (§14), so no ordinary caller can
  reach `$C7:0004` during a load. The gate is therefore an **interrupt-re-entrancy guard**. This is
  load-bearing, because the loader keeps reading `$1E00` after the mailbox was "consumed"
  (`$C7:092E LDA $00 / CMP #70`) and `$C7:0734` *writes* it — an NMI-context command would corrupt
  the load in flight if it were allowed through.
* [OBS] The gate never fired in either trace (`$C7:0159` executed 6 + 5 times, branch taken 0 times),
  so the *drop* is derived from the instruction and never yet observed. The BUSY interval itself is
  observed: `$1E05 = $1E` on frames 1…20 of the teleport sweep (§3), and `audio_commands.md` §10.2
  saw `$1E05 = $45` for 59 consecutive frames.

---

## 14. Multi-frame: it is ONE LONG BUSY-WAIT — proof

**(a) There is no frame-sync instruction.** The full disassembly of `$C7:01A1-$C7:0726`
(seven contiguous `tools/dis.py` chunks, widths noted in §15) was scanned for
`\$4[0-9A-F]{3}|WAI|STP|CLI|SEI|JSL|MVN|MVP` → **zero hits**. The only hardware registers touched
in the entire region are:
```
$2140 x4    $2141 x25    $2142 x19    $2143 x17
```
(the four `$2140`s are `$0609`/`$060C` — the final GO — and `$0632`/`$0635` inside `$061C`; all the
other `$2140` traffic is inside `$C7:09DA`/`$09EA`, which are likewise `$2140`-only). Same for
`$0922`, `$09DA`, `$09EA`, `$09FD`, `$0A12`, `$0A39`.
**No NMI enable/disable, no `$4210` poll, no vblank wait, no `WAI`.**

**(b) The CPU really is parked inside it across a frame boundary.**
`build/ct.exe --savestate b90.state --frames 90` then `build/ct.exe --trace 1 --from b90.state`:
```
C7 09E3 1E00 1 0 0        <- the restored PC is the BNE of $C7:09DA's echo spin
```
i.e. the frame ended with the 65816 sitting in the token spin.

**(c) It costs tens of frames of wall time.**
* [OBS] teleport sweep (§3): song `$1E` is BUSY at frame 20 and READY by frame 30 ⇒ **21–30 frames**.
* [OBS] `audio_commands.md` §10.2: song `$45` BUSY f227 → f285 ⇒ **59 frames**.
* [OBS] `--trace 4000000` from reset: one load ran **558,803 consecutive instructions, 100 % in bank
  `$C7`** (`$C7:01B3` at index 1448842 → `$C7:0619` at 2007644).

**(d) NMI is not blocked by the loader, but was not observed preempting it.** In that 558k window
the NMI trampoline `$00:0500` fired **zero** times — but that is because the *caller* had NMI masked
(the trace has an NMI gap of 1,135,384 instructions spanning the whole load, while NMIs otherwise
tick every ~14,800 instructions). A second, shorter load (6,587 instructions, `tr120`) also ran
uninterrupted. **NOT DERIVED: whether a gameplay-context load (event-VM op `$EA`) is NMI-preempted.**

**Consequence for the C port.** The loader carries **no state across a yield** other than the 65816
registers and `$7E:1E00-$7E:1FFF`, and every byte is a two-way rendezvous (data written to
`$2141-$2143` *before* the token goes to `$2140`, then a spin on the echo — the same property proved
for the IPL in `audio_boot.md` §4b). Therefore **the ARAM outcome is invariant to how the port
schedules the APU**: it may run the whole thing in one call, provided it advances the APU inside
every spin. A state machine is *not* required for correctness — only if the port wants to keep
rendering during the 20–60 frames the ROM itself spends stalled.

---

## 15. WRAM `$7E:1E00-$7E:1FFF` — the loader's private page

Bank `$C7` runs with **`D = $1E00`** and **`DB = $00`**, so a listing's `$xx` is `$7E:1Exx` and its
`$1Fxx`/`$1Exx` absolutes are `$7E:1Fxx`/`$7E:1Exx`. **These are WRAM, not ARAM** — the numeric
coincidence with the ARAM addresses (`$1E80`, `$1F00`, `$1F40`, `$1F80`, `$1FC0`) is a naming
convention of the driver, not the same memory.

| WRAM | size | role | cited |
|---|---|---|---|
| `$1E00-$1E03` | 4 | the mailbox | `audio_commands.md` |
| `$1E05` | 1 | **BUSY mutex AND the loader's song id** | §13 |
| `$1E0A` | 1 | `STZ` at `$C7:0328`, **no reader anywhere** | §17 |
| `$1E0C/$1E0D` | 2 | scratch: block length / slot cursor | `$01D0`, `$0527`, `$06A4` |
| `$1E0E` | 2 | scratch: instrument-list end index = `id*32+32` | `$01E8` |
| `$1E12-$1E14` | 3 | scratch: 24-bit ROM source pointer | `$0513`, `$066A` |
| `$1E1C/$1E1E` | 4 | scratch: X/Y save across `JSR $0655` | `$034B` |
| `$1E20[16]` | 32 | **resident instrument ids** (1..62, 0 = free) | `$01FA`, `$0349` |
| `$1E40[16]` | 32 | **their sample lengths** | `$06A6` |
| `$1E60[17]` | 34 | **their ARAM start addresses**; `[k+1] = [k] + len[k]` | `$06A9`, `$02B5` |
| ↳ `$1E60` itself | 2 | the arena **base `$4901`** — written **once, at boot** (`$C7:0114`) and by nothing else in the ROM; every runtime write is `$1E62`-and-up (`STA $62,X`, `STA $1E60,Y` with Y≥2) | dp-`$60` operand scan of bank `$C7` code |
| `$1E82` | 2 | scratch: `id*12` | `$05A9` |
| `$1E84` | 1 | **the rolling token** | §3 |
| `$1E86` | 1 | scratch: the command byte echoed by `$0922` | `$093D` |
| `$1EF0` | 1 | current SFX bank | `$0A00` |
| `$1EF2` | 1 | `$F1 - bank*8` = the ARAM ceiling high byte | `$0A0B` |
| `$1EF3` | 1 | **"ARAM `$2F00` is dirty"** | §8 |
| `$1EF6` | 2 | next free ARAM address | `$0365` |
| `$1EF8` | 2 | the base `$4901` (set at boot, never rewritten) | `audio_boot.md` §4c |
| `$1EFC` | 2 | `2 *` the slot of the song's first instrument → cmd `$71` | §6c |
| `$1F00[16]` | 32 | scratch: **the song's full instrument set** | `$01F0` |
| `$1F20[16]` | 32 | scratch: **the instruments still to LOAD** | `$0203` |
| `$1F80[32]` | 64 | staging for the ARAM `$1E80` directory push (**tail stays stale**) | §6a |

[OBS] all four arrays verified byte-for-byte against the ROM tables — see §16.

---

## 16. Oracle verification — the whole loader, end to end

Method: `build/ct.exe --teleport 12 8 8 0 --from candy_shop_lake.state --frames 240 --out tp12.state`
(loc `$00C` → location song `$1E`), then `--read` and `--aram --at`.

**WRAM after the load** (`--read "wram:1E00+16,wram:1E20+32,wram:1E40+32,wram:1E60+34,wram:1E82+8,wram:1EF0+16,wram:1F00+32,wram:1F20+32"`):
```
1E00 = 00 1E E8 0D 00 E0 00 00 00 00 00 00 C0 05 E0 03
        ^cmd cleared  ^$1E02/$1E03 = ROM record for song $1E ($C7:241D+$3C = 0D E8)   ✔
                                  ^$1E05 = E0 READY   ✔
                                                       $1E0C = 05C0 = the sequence length ✔
                                                             $1E0E = 03E0 = $1E*32+32   ✔
1E20 = 2C00 1800 1700 3800 0000 ...     == rom[$C7:0E11 + $1E*32] = 2C 18 17 38        ✔
1E40 = 251D A905 F40B 560A 0000 ...     == the four lengths 1D25 05A9 0BF4 0A56        ✔
1E60 = 0149 2666 CF6B C377 1982 ...     == 4901, +1D25=6626, +05A9=6BCF, +0BF4=77C3, +0A56=8219 ✔
1E82 = 68 01   ($1E82 = $0168 = $1E*12) ; 1E84 = 6D  (the predicted final token)       ✔
1EF0 = 0D 00 89 00 ...  ($1EF0 = bank $0D, $1EF2 = $F1-$68 = $89)                      ✔
1EF6 = 8219  ($1E60[4], the next free ARAM address)                                    ✔
1F00 = 1F20 = 2C00 1800 1700 3800 ...   (both scratch lists)                           ✔
```

**ARAM after the load** (`--aram --at`):
```
$1E80 = 0149 1C49 | 2666 996B | CF6B 8B73 | C377 D181 | <stale from song $06> ...
        4901,491C   6626,6B99   6BCF,738B   77C3,81D1
        == (start, start + rom[$C7:0BA4 + (instr-1)*2]) for instruments $2C,$18,$17,$38
           loop offsets 001B, 0573, 07BC, 0A0E                                          ✔✔✔
$1F40 = E9F0 529C FF00 E100 | 0205 x12   == rom[$C7:0C20] for $2C,$18,$17,$38 = F0E9,9C52,00FF,00E1
                                             + the $C8:0C1E garbage for the 12 unused  ✔
$1FC0 = FFE0 FFED FFE0 FFE0 | C21F x12   == rom[$C7:0C9C] = E0FF,EDFF,E0FF,E0FF
                                             + the $C8:0C9A garbage                    ✔
$2000 = 580BF410580B1D0CB40C130D5B0DB60D  == rom[$CB:0B34]                              ✔
$F120 = all zero                          == rom[$C7:1871 + $1E*36] (all zero)          ✔
$4901 = 0200000000000000009ABD11125E9E26  == rom[$C9:BBF9] (instrument $2C payload)     ✔
$6626 = 0200000000000000009613E45C401FB2  == rom[$C8:895C] (instrument $18 payload)     ✔
$2F00 = rom[$C7:430B]  (mode 0)                                                          ✔
```
And on `candy_shop_lake.state` (song `$06`, 10 instruments) the same eight checks pass with
`$1F80` = `0149 654B DE53 5558 …` and `$1EF6 = $C53F`, plus `$F120` = the 36 ROM bytes of
`$C7:1871 + 6*36` verbatim.

**Live DSP consequence** (`build/ct.exe --aram --from tp12.state`):
```
dsp mvol=64/64 dir=$1E00 evol=20/20 voicesWithGain=6
 v0 srcn=22 pitch=0D66 gain=077F  <-- SOUNDING     v1..v4 srcn=20/21 ... <-- SOUNDING
```
Source numbers `$20`,`$21`,`$22` = directory entries `$1E80`,`$1E84`,`$1E88` — the entries this
routine just wrote. **The loader's output is what the DSP is reading.**

---

## 17. NOT DERIVED — the exact gaps, and what closes each

1. **The SPC-side meaning of `$1E02`.** Derived: it is `rom[$C7:241E + id*2]`, goes to `$2142` with
   the play command, and is read nowhere else on the CPU side (operand scan of `$01A1-$0619`).
   Values are `$8F..$FF`. **Closes it:** disassemble the driver blob's command-`$10` handler
   (SPC700 code at ARAM `$0200`, ROM `$C7:24C5`, 7151 bytes) — needs a `tools/dis_spc.py`.
2. **What sets `$2141` bit 7** — the condition that makes `$C7:0A83` abort the whole load. Observed
   firing twice in a boot trace and once (residually) in `candy_shop_lake.state`. **Closes it:**
   the same SPC700 disassembly, or a `--aram`-style per-frame log of `apu->outPorts[1]` around a
   duplicate song request.
3. **`$C7:0C20`'s semantic** (16 words → ARAM `$1F40`). Sourcing is byte-exact; the meaning is
   SPC-side. Same closer as (1).
4. **`$1E0A`.** `STZ $1E0A` at `$C7:0328` is the only access in the whole ROM (direct-page operand
   scan of bank `$C7` code + absolute/long scan of all 4 MB). **Closes it:** an SPC/driver-side
   audit, or accept it as dead.
5. **`$C7:025A` (defragment, driver command `07`) and `$C7:02E4` (the give-up rebuild) are derived
   but never executed** in any trace taken here, so the byte order of command `07`'s three
   parameter units is unconfirmed against hardware. **Closes it:** force a load whose instrument set
   leaves interior holes (load song `$06`, then a song sharing only its *later* instruments) with
   `--trace` + a `$C7:025A` counter.
6. **`$C7:09EA` (the resync) never executed.** Same style of closer: it needs the driver to fail to
   echo a command byte on `$2141`.
7. **Whether a gameplay-context song load is NMI-preempted.** Both loads I traced ran with NMI
   masked by the caller. **Closes it:** `--trace` from a state where event-VM op `$EA` is about to
   execute (a `--btnscript`-driven trace mode would be needed; `--trace` currently takes no input
   script), then count `$00:0500` inside the `$01A1`→`$0619` span.
8. **The `$1E05` BUSY-drop was never observed firing** (`$C7:0159` branch taken 0/11 times).
   Derived from the instruction; the only possible trigger is an interrupt-context caller.
   **Closes it:** identify whether any of the 59 `JSL $C7:0004` sites runs under NMI/IRQ
   (`$FF:FC82/FC97/FF24/FF39` are the likeliest candidates) and trace one.
9. **Mid-load savestates are not a reliable resume point.** `--teleport … --frames N --out` at
   N = 4 and cold-boot `--savestate --frames 120` both produce states whose WRAM says BUSY
   (`$1E05` = the song id, `$1E00` = `$10`) while the restored CPU PC is *outside* bank `$C7`
   (`C3:06DA`, `7E:3026`); resuming them does not finish that load. Frame-boundary states that
   *do* catch the loader (e.g. `--frames 90` → PC `$C7:09E3`) exist. **This is a harness/savestate
   observation, not a ROM behaviour** — flagged so nobody builds a regression pin on a mid-load
   state. **Closes it:** compare `snes_handleState`'s save point against the CPU's instruction
   boundary in `src/main.c`'s savestate path.

---

## 18. PORT SPEC (derive lane — nothing applied)

```c
// preconditions: the IPL upload of audio_boot.md has run; mem[] is the port's $7E:1E00 page;
// W(k,v)/R(k) are apu->inPorts[k]/apu->outPorts[k]; step() advances the APU by any quantum.

static uint8_t tok;
static uint8_t hs(uint8_t t){ W(0,t); while(R(0)!=t) step(); return (uint8_t)((t+1)&0x7F); }
static void    cmdblk(uint16_t dst, uint8_t c){                    // the $02/$03/$05 preamble
  for(;;){ W(3,dst>>8); W(2,dst&0xFF); W(1,c); tok=hs(tok);
           if (R(1)==c) return;                                    // $03E0/$0473/$04F3/$0698/$05D4
           /* $09EA */ W(0,tok); while(R(0)!=tok) step(); tok=0xE0; }
}

int CtAudioLoadSong(uint8_t cmd, uint8_t id)                        // cmd $10/$11/$14/$15
{
  if (id >= rom[0xC7*0x10000 ... 0x0AE9]) return 0;                 // $01A5, = $53
  if (cmd >= 0x14) mem[0x00] |= 4;                                  // $0734 (list at $0AD8 is inert)

  // ---- $0A39 -------------------------------------------------------------
  const uint8_t* rec = &rom[0x7241D + (id & 0x7F)*2];
  mem[0x02] = rec[1];  mem[0x03] = rec[0] & 0x0F;
  if (mem[0x03] != mem[0xF0]) sfxBankSwitch(mem[0x03]);             // $09FD -> $1EF0,$1EF2, then cull
  for(;;){ W(3,mem[0x03]); W(2,mem[0x02]); W(1,mem[0x01]); W(0,mem[0x00]);
           int n=0x400; while(R(0)!=mem[0x00] && --n) step(); if(n) break; }   // $0A6E timeout -> resend
  tok = (uint8_t)((mem[0x00]+1)&0x7F);                              // $0A7B
  if (R(1) & 0x80) { W(0,tok); uint8_t p=tok-1; while(R(0)==p) step(); mem[0x00]=0; return 0; } // $0A85 ABORT

  mem[0x05] = id;                                                   // $01B3  BUSY *and* the id register
  // ---- $01B5 diff --------------------------------------------------------
  //  clear $1F00[16], $1F20[16];  walk rom[$C7:0E11 + id*32] (16 words, 0-terminated):
  //    always append to $1F00; append to $1F20 only if absent from $1E20[]
  //  if $1F20[0] == 0 -> goto PUSHES
  //  evict every $1E20 slot absent from $1F00
  //  if (first free slot)*2 != (last occupied+1)*2 -> defragment via driver command 07  ($025A)
  //  for each $1F20 entry: put it in the next free slot and upload it  ($0326 -> $0655):
  //     ptr = rom24[$C7:0AEA + (instr-1)*3];  len = ptr[0]|ptr[1]<<8
  //     $1E40[s]=len; $1E62[s]=$1E60[s]+len;  cmdblk($1E60[s], 0x03)
  //     for (i=0; i<len; i+=3) { W(1,p[i]); W(2,p[i+1]); W(3,p[i+2]); tok=hs(tok); }   tok=0xE0;
  //  $1EF6 = $1E60[firstFree]                                        ($0365)
PUSHES:
  // ---- $037B: sample directory -> ARAM $1E80 (ALWAYS 64 bytes; tail of $1F80 is STALE) ----
  //   for k<16 while $1F00[k]: s=slotOf($1F00[k]); $1F80[2k]= $1E60[s];
  //                            $1F80[2k+1]= $1E60[s] + rom16[$C7:0BA4+(instr-1)*2];
  cmdblk(0x1E80,0x02); for(x=0;x<0x40;x+=2){ W(1,w1f80[x]); W(2,w1f80[x+1]); tok=hs(tok);} tok=0xE0;
  // ---- $040D / $045B: 16 words each from $C7:0C20 and $C7:0C9C (ADSR) -> $1F40 / $1FC0 ----
  //   unused slot -> the long,X wrap reads $C8:0C1E (=0x0502) resp. $C8:0C9A (=0x1FC2)
  // ---- $04A9: $1EFC = 2*slotOf($1F00[0])
  // ---- $04C0: mode ----
  uint8_t m = mem[0x10] ? 1 : 0;
  if (m != mem[0x11]) { mem[0x10]=mem[0x11]=m; mem[0xF3]++; }
  if (mem[0xF3]) uploadModeBlock();                                 // $0922: $C7:4309 or $C7:5B0B -> $2F00
  // ---- $04DB: the sequence -> ARAM $2000 ----
  const uint8_t* sq = rom24(0x70D18 + id*3); uint16_t sl = sq[0]|sq[1]<<8;
  if (sl >= 0x0F00) mem[0xF3]++;                                    // $0539  (only song $3F)
  cmdblk(0x2000,0x03);
  for (int x = sl; x >= 0; x -= 3) { W(1,*p++); W(2,*p++); W(3,*p++); tok=hs(tok); }   // floor(sl/3)+1 units
  tok = 0xE0;
  // ---- $059A: 36 bytes rom[$C7:1871 + id*36] -> ARAM $F120 ----
  cmdblk(0xF120,0x03); for(int i=0;i<12;i++){ W(1,b[0]);W(2,b[1]);W(3,b[2]); b+=3; tok=hs(tok);} 
  // ---- $0601: GO ----
  W(1,0xE0); W(0,0xE0); { int n=0x400; while(R(0)!=0xE0 && --n) step(); }
  mem[0x05] = 0xE0;                                                 // $0614  READY
  cull();                                                           // $0616 -> $0A12
  mem[0x00] = 0;                                                    // $0192
  return 1;
}
```

**Validation pins** (all byte-exact against the oracle today, `--aram --at`):
* ARAM `$2000` == `rom[seqPtr(id) + 2]` for the first 32 bytes.
* ARAM `$1E80..$1EBF` == the (start, start+loop) pairs for the song's instruments, tail preserved.
* ARAM `$1F40`/`$1FC0` == `rom[$C7:0C20]`/`rom[$C7:0C9C]` per instrument + `0205`/`C21F` fill.
* ARAM `$F120..$F143` == `rom[$C7:1871 + id*36]`.
* ARAM `$2F00..$46FF` == `rom[$C7:430B]` (mode 0) or `rom[$C7:5B0D]` (mode 1).
* ARAM `$1E60[k]` regions == the raw instrument payloads.
* WRAM `$1E20/$1E40/$1E60/$1EF0/$1EF2/$1EF6/$1E84` == the values in §16.

---

## 19. Repro commands (every one used above)

```
python tools/dis.py 0x701A1 0xA0 1 0     # $01A1 head + the instrument diff
python tools/dis.py 0x70217 0xC0 0 0     # evict / hole scan / defragmenter  (16-bit A)
python tools/dis.py 0x702D6 0xB0 1 0     # defrag tail + $02E4 rebuild + $0326
python tools/dis.py 0x7037B 0xF0 0 0     # $037B directory build + the $1E80 / $1F40 pushes
python tools/dis.py 0x70469 0x100 1 0    # $1FC0 push, $1EFC, mode, sequence head
python tools/dis.py 0x70568 0x120 1 0    # sequence stream, $F120 block, $0601 GO, $061C, $0655 head
python tools/dis.py 0x70687 0xA0 1 0     # $0655 tail + $071D
python tools/dis.py 0x70734 0x30 1 0     # $0734 (cmd |= 4)
python tools/dis.py 0x70755 0x70 0 0     # cmd $71 head (the $1EFC consumer)
python tools/dis.py 0x708E3 0x40 1 0     # cmd $70 (mode toggle)
python tools/dis.py 0x70922 0xC0 1 0     # the ARAM $2F00 mode-block re-upload
python tools/dis.py 0x709DA 0x30 1 0     # $09DA token / $09EA resync / $09FD bank / $0A12 cull
python tools/dis.py 0x70A39 0x60 1 0     # $0A39 record lookup + port block + abort

build/ct.exe --teleport 12 8 8 0 --from candy_shop_lake.state --frames 240 --out tp12.state
build/ct.exe --read "wram:1E00+16,wram:1E20+32,wram:1E40+32,wram:1E60+34,wram:1E82+8,wram:1EF0+16,wram:1F00+32,wram:1F20+32,wram:1F80+64" --from tp12.state
build/ct.exe --aram --from tp12.state --at 1E80+0x40      # (also 1F40, 1FC0, 2000, F120, 2F00, 4901)
build/ct.exe --aram --from saves/gato_inbattle.state --at 2F00+0x20     # mode 1
build/ct.exe --savestate b90.state --frames 90 ; build/ct.exe --trace 1 --from b90.state
build/ct.exe --trace 4000000 > tr_boot.txt                # PC histogram: aborts, retries, iteration counts
```
Widths: `$01A1`, `$02D6`, `$0469`, `$0568`, `$0687`, `$0734`, `$08E3`, `$0922`, `$09DA`, `$0A39`
decode with **m=1** (8-bit A); `$0217` and `$037B` must be re-disassembled with **m=0** (16-bit A)
or they come out as garbage. X/Y are **16-bit throughout** (`REP #10` at `$C7:0143`, never undone).

---

## PORT STATUS + THE EXACT REMAINING DEFECT (2026-07-25, measured by `ct.exe --songload`)

`src/audio.c` now implements steps 2-9 of `audio_song_loader_verify.md`'s plan end to end. It RUNS:
loading song `$06` on a freshly booted SPC700 leaves `$1EF6 = $C53F` -- the predicted high-water
(`$C53F - $4901` = 31806 B of BRR) -- and twelve frames later the DSP reports **three voices
sounding**. It is GATED OFF (`CT_AUDIO_SONGLOAD=1`) because it is not byte-exact yet.

**The DATA MODEL is proven correct, independently of the port.** A pure-Python check against the
oracle's ARAM shows the heap is exactly the ROM sample blocks concatenated in instrument-set order:
song `$30`'s six instruments `$2C $13 $3A $09 $27 $3B` land at `$4901 / $6626 / $7625 / $919A /
$A553 / $B5FD`, each MATCHing its ROM block byte for byte, ending at `$B939` -- and the oracle's own
`$1E60` array reads exactly those bases. Our allocator produces the SAME bases. The DSP directory
rule is confirmed too: entry k = `{start = base[k], loop = base[k] + rom16($C7:0BA4 + (id-1)*2)}`,
e.g. `$4901 + $001B = $491C`, `$6626 + $0F5D = $7583`, `$7625 + $0507 = $7B2C`.

**So the defect is in the WIRE, not the data.** Against an oracle state that loaded the same song,
with our ARAM snapshotted BEFORE the driver is released:

| region | cmd | result |
|---|---|---|
| `$F120` per-song block, 36 B | `$03` | **BYTE-EXACT** |
| `$1E80` directory, 64 B | `$02` | 26 differ, first at +2 |
| `$1F40` / `$1FC0`, 32 B | `$02` | ~21 differ, first at +2 |
| `$2000` sequence, 651 B | `$03` | 8 differ, first at +130 |
| heap `$4901`, 28728 B | `$03` | 292 differ, first at +130 |

The cmd-`$02` signature is exact and diagnostic. Lining our ARAM up against the oracle's:

```
oracle: 01 49 1C 49 | 26 66 83 75 | 25 76 2C 7B | 9A 91 9A 91
ours:   01 49 49 26 | 83 75 76 2C | 9A 91 ...
```

ours takes oracle bytes 0,1 then 3,4 then 6,7 then 9,10 -- **every THIRD byte is missing**. Exactly
two of each three-byte unit are landing. That points at the third port write (`$2143`) of each unit
not being consumed the way the ROM's is, and it is the single most likely cause of both defects.

The cmd-`$03` failures are SPARSE, not a sustained shift: 8 of 651 and 292 of 28728, clustered at
offsets 130, 386, 643 -- gaps of 256 and 257. A sustained one-byte drop would corrupt everything
after the first divergence, so the bulk path is mostly right and fails periodically, ~once per 256
bytes. The 36-byte `$F120` block never reaches that period, which is why it passes.

**NOT DERIVED / next step:** whether the driver consumes `$2143` per unit at all, or whether the
period-256 clusters come from the ROM's `INY / BNE / INC $14` source walk (`$C7:06CD`). Closing it:
dump our `$1F80` staging buffer alongside the oracle's ARAM `$1E80` -- if the buffer already has the
missing byte, the construction is right and the transfer drops it; if not, the DIR builder is wrong
and the bulk defect is separate.
