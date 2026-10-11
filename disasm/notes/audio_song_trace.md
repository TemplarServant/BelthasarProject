> ⛔ **CORRECTION** (adversarial verify, 2026-07-25): §K.2's gap — "the instruction that loads X with
> `3*(size/3+1)` was not located" — is a FALSE GAP. No such instruction exists: the sequence padding
> is EMERGENT from `LDX rawlen` + `BPL`, not a padded count. Nothing is missing there.

# AUDIO front 3 — A REAL SONG LOAD, CAPTURED LIVE

**The empirical half of the missing arm.** `audio_boot.md` ported the IPL upload, `audio_commands.md`
derived the `$1E00` mailbox, `audio_song_selection.md` derived *which id*, `audio_song_data.md`
(front 2) derives *which ROM bytes*. This dossier is the **ground truth the port will be validated
against**: two complete, frame-by-frame recordings of the oracle actually performing a song load —
the frame window, the `$2140-$2143` traffic, every changed ARAM range, the WRAM mailbox evolution,
and the exact DSP voice state at the first frame that produces sound.

Everything below is either **(a)** a ROM address disassembled with `python tools/dis.py <flatoffset>
<len> [m] [x]` (headerless HiROM, bank `$XX` = flat `(XX-0xC0)<<16 | addr`, so `$C7:06F9` =
`0x706F9`), or **(b)** an **observed oracle value** marked ⟦OBS⟧ with the exact command. Gaps are in
§K. Nothing here is inferred from a render or from prose.

**Independent-derivation cross-check:** §D-§E were derived here *from the ARAM side only* (diff the
oracle's ARAM, then search the ROM for the bytes that appeared). Front 2 derived the same six tables
*from the 65816 disassembly*. **The two agree on every table base, stride and destination**
(`$0AEA`/`$0BA4`/`$0C20`/`$0C9C`/`$0D18`/`$0E11`, heap base `$4901`, sequence dest `$2000`,
DIR at `$1E80`). That agreement is itself a proof.

---

## HEADLINE — the ten facts

1. **Two reproducible captures.** *(A)* cold boot — song **`$30`** loads over **frames 98-132**,
   first sound at **f139**. *(B)* a field location change (`--teleport 5` from `truce_market.state`)
   — song **`$06`** loads over **T+0..T+43**, first sound at **T+50**. Both from one command each (§A, §H).
2. **A song load is ONE BLOCKING `JSL $C7:0004` that owns the CPU for ~35 frames with NMI OFF.**
   ⟦OBS⟧ 300 000 traced instructions from mid-load are **100 % bank `$C7`, zero NMI entries, zero
   dispatcher entries, zero exits** (§C.3). This is why `$1E00` stays `$10` and `$1E05` stays = song
   id for the whole window — not because the loader is re-entered per frame.
3. **The wire protocol is 3 data bytes per rolling token**, byte-for-byte
   `C7/06F9: LDA [$12],Y → $2141 ; → $2142 ; → $2143 ; LDA $84 ; JSR C7/09DA ; STA $84 ; DEX×3 ; BNE`
   (§C.2). The token is the 7-bit counter of `C7/09DA` (`STA $2140 / CMP $2140 / BNE / INC A / AND #7F`).
4. **Rate ≈ 286 tokens = 858 bytes per frame** (cold boot) / **≈ 266 tokens = 798 B/frame** (field).
   Measured two independent ways (ARAM diff span vs. token delta) that agree exactly in 3 of 4
   sampled frames and within one token in the 4th (§C.4).
5. **Per-frame port sampling CANNOT count the handshakes** — the token wraps mod 128 two-plus times
   per frame. Stated, not fabricated; the one-line harness fix is in §C.5.
6. **Load order is: all instrument samples (ascending from ARAM `$4901`) → the per-instrument tables
   (`$1E80` DIR, `$1F40`, `$1FC0`) → the sequence at `$2000`.** ⟦OBS⟧ from a sliding-window ARAM diff
   frame by frame (§D.2).
7. **THE SEQUENCE IS PADDED, THE SAMPLES ARE NOT.** Bytes written to ARAM `$2000` =
   `3 * (sizeword/3 + 1)` — i.e. **the smallest multiple of 3 STRICTLY GREATER than the block's size
   word**. Confirmed 4/4 on songs `$00`, `$1E`, `$06`, `$30` (§E.3). Sample blocks transfer exactly
   `sizeword` bytes (§E.2).
8. **Six ROM tables reproduce the ARAM image byte-for-byte** for 20 instruments across 3 songs, with
   zero mismatches (§E.1).
9. **First sound, cold boot f139** — `v1 srcn=$25 pitch=$0BFC gain=$07F7 adsrState=2
   decodeOff=$B606`, `mvol=64/64 evol=20/20 dir=$1E00`. That is the acceptance signature (§F).
10. **The loader leaves stale state**: DIR rows past the new instrument count keep the *previous*
    song's values, and sample bytes past the new `$1EF6` are never cleared (§I).

---

# A — THE TWO CAPTURES (reproducible in one command each)

## A.1 Scenario A — cold boot (no state needed)

```
build/ct.exe --aram --frames N            # N = 0..400, sweeps the whole boot
```
The ROM boots to the title/intro. `--aram --frames N` re-runs the boot from reset every time, so the
sweep is deterministic. ⟦OBS⟧ savestate resume is faithful mid-load — `--savestate build/boot90.state
--frames 90` then `--aram --from build/boot90.state --frames 10` prints `fnv32=2E35ECCA`, identical
to `--aram --frames 100`.

## A.2 Scenario B — a field location change (THE path the port needs)

```
build/ct.exe --teleport 5 8 8 0 --from saves/truce_market.state --frames 1 --out build/tp5_f1.state
build/ct.exe --aram --from build/tp5_f1.state --frames T          # T = 0..100
```
`truce_market.state` is loc `$011`, song shadow `$1E`. Teleporting to loc 5 runs the ROM's own loader
chain `C0/00F4 → C0/00F7 → C0/1B53`, which reads the location record byte `$F6:[5*14] = $06` and
issues `cmd $10` (`audio_song_selection.md` §B.2). The resulting `tp5_f1.state` is captured **while
the load is in flight** (`$1E05 = $06`). `T` below always means `--aram --from build/tp5_f1.state
--frames T`.

## A.3 Clock discipline (a real trap — three modes, three clocks)

| mode | what frame it shows |
|---|---|
| `--aram --frames N` / `--aram --from S --frames N` | after **N** frames |
| `--savestate out --frames N` then `--read --from out --settle 0` | after **N** frames (same clock) |
| `--monitor --from S --frames N` | prints rows `f0..f(N-1)`; **row `fK` ≡ K+1 frames after S** |
| `--trace --from S` | ⚠ **runs 30 settle frames first** (`src/main.c:5222`) — traces from **S+30** |

⟦OBS⟧ verified: `--monitor --from build/tp5_f1.state --frames 44` last row `f43` shows the release,
and `--aram --from build/tp5_f1.state --frames 44` shows `spc->cpu[0]=E0`. Same instant.

---

# B — THE WINDOW

## B.1 Cold boot (absolute frames from reset)

⟦OBS `build/ct.exe --aram --frames N`, N swept 22..400⟧ and
⟦OBS `--savestate build/bN.state --frames N` + `--read "wram:1E00+8,wram:1EF0+4,wram:1EF6+2" --from build/bN.state --settle 0`⟧:

| frame | `$2140-43` cpu→spc | `spc→cpu` | ARAM nonzero | WRAM `$1E00..$1E05` | `$1EF6` | event |
|---|---|---|---|---|---|---|
| 23-26 | `10 00 FF 05` | `81 BB 00 00` | 12838 | `10 00 FF 05 00 E0` | `4901` | **the FIRST song command of the boot, visible on the ports** — cmd `$10`, song `$00` |
| 28 | `00 00 00 00` | `00 00 00 00` | 12983 | — | — | song `$00` done; **DSP `mvol` 0/0 → 64/64** |
| 80-81 | `00 00 FF 05` | `00 00 00 00` | 12982 | `00 00 FF 05 00 E0` | `4901` | idle |
| **82-97** | `00 00 FF 05` | `00 00 00 00` | ~12970 | **`81 00 FF 05 00 E0`** | `4901` | cmd `$81` **staged and never dispatched** (§G.2) |
| **98** | `63 AA F2 1D` | `62 EE 00 00` | 13212 | **`10 30 C0 06 00 30`** | `4901` | **cmd `$10`, song `$30`, `$1E05` := `$30` = BUSY** |
| 99-131 | rolling token + 3 data | `<tok> EE 00 00` | 14052 → 40709 | `10 30 C0 06 00 30` | `4901` | streaming, ~858 B/frame |
| **132** | `5B C5 39 39` | `5B EE 00 00` | 41290 | `10 30 C0 06 00 30` | **`B939`** | last streaming frame; next-free pointer committed |
| **133** | `00 00 00 00` | **`E0 00 00 00`** | 41598 | **`00 30 C0 06 00 E0`** | `B939` | **`$1E00` cleared (`C7/0196`), `$1E05` := `$E0` = READY** |
| 134-138 | `00 00 00 00` | `E0 00 00 00` | 41599 | `00 30 C0 06 00 E0` | `B939` | driver initialising voices; `evol` still 0/0 |
| **139** | `00 00 00 00` | `00 00 00 0F` | 41665 | `00 30 C0 06 00 E0` | `B939` | **FIRST SOUNDING VOICE** (§F.1); `evol` 0/0 → 20/20 |

**Load window = frames 98-132 inclusive (35 frames). Ready at 133. Audible at 139.**

## B.2 Field location change (T = frames after `build/tp5_f1.state`)

⟦OBS `--aram --from build/tp5_f1.state --frames T`⟧ +
⟦OBS `--monitor "1E00:1,1E01:1,1E02:1,1E03:1,1E05:1,1EF0:1,1EF2:1,1EF6:2,29AE:1,0100:2" --from build/tp5_f1.state --frames 70 --diff --writer`⟧:

```
  f0      001E00=10*  001E01=06*  001E02=C8*  001E03=05*  001E05=06*  001EF0=05*  001EF2=C9*  001EF6=1982*  0029AE=06*  000100=0500*
  f39     001E00=10   001E01=06   001E02=C8   001E03=05   001E05=06   001EF0=05   001EF2=C9   001EF6=3FC5*(C7:0367)  0029AE=06  000100=0500
  f43     001E00=00*(C7:0196)     001E02=C8   001E03=05   001E05=E0*(C7:0616)     001EF0=05   001EF2=C9   001EF6=3FC5  0029AE=06  000100=0500
```
(monitor row `fK` ≡ T = K+1.)

| T | event |
|---|---|
| 0 .. 43 | streaming; `$1E05 = $06` (BUSY); `$1E01/$1E02/$1E03 = 06 / C8 / 05` |
| 40 | `$1EF6` := `$C53F` (writer `C7:0367`) = `$4901 + 31806`, the new next-free ARAM |
| **44** | `$1E00` := 0 (`C7:0196`), `$1E05` := `$E0` (`C7:0616`); `spc→cpu[0] = E0` |
| **50** | **first sounding voices — three at once** (§F.2) |

`$1E02/$1E03 = C8/05` is exactly `rom[$C7:241E + 6*2] / rom[$C7:241D + 6*2] & $0F`
⟦OBS python over the ROM⟧ — the song-record law of `audio_commands.md` §5, re-confirmed.
Note `$1EF0` stays `05` here (no SFX-bank switch, because `$1E03 = 5 == $1EF0`), whereas at cold boot
`$1E03 = 6 ≠ 5` and the driver switched: `$1EF0` := `06` (writer `C7:0A02`), `$1EF2` := `C1`
(writer `C7:0A0D`) `= 0xF1 - 6*8` ✔.

---

# C — THE PORT TRAFFIC (`$2140-$2143`), reconstructed

## C.1 What `--aram` gives us

`build/ct.exe --aram` prints **both directions** of the four APU I/O ports:
```
aram: ports cpu->spc $2140-43 = 1D 35 DF 52 | spc->cpu = 1D EE 00 00
```
`cpu->spc` is `apu->inPorts[0..3]` = the last values the CPU wrote to `$2140/$2141/$2142/$2143`;
`spc->cpu` is `apu->outPorts[0..3]` = what a CPU read of those addresses would return
(`src/main.c:910-912`). This supersedes `audio_commands.md` §12's "no existing ct.exe mode can
observe the ports".

## C.2 The streaming loop, from the ROM

The `--trace` PC histogram of a mid-load window (§C.3) puts the overwhelming majority of executed
instructions in one 3-byte loop plus one handshake. `python tools/dis.py 706D0 0x60 1 0`:

```
C7/06F9  LDA [$12],Y      ; $1E12/13/14 = 24-bit ROM source pointer
C7/06FB  STA $2141        ; data byte 0
C7/06FE  INY
C7/06FF  LDA [$12],Y
C7/0701  STA $2142        ; data byte 1
C7/0704  INY
C7/0705  LDA [$12],Y
C7/0707  STA $2143        ; data byte 2
C7/070A  INY
C7/070B  LDA $84          ; $1E84 = the rolling 7-bit token
C7/070D  JSR C7/09DA      ; STA $2140 / CMP $2140 / BNE -3 / INC A / AND #7F   <- the handshake
C7/0710  STA $84
C7/0712  DEX / DEX / DEX  ; 3 bytes consumed
C7/0715  BNE ->706F9
C7/0717  LDA #E0 / STA $84 / PLP / RTS
```
`python tools/dis.py 709DA 0x30 1 0` confirms `C7/09DA` = `PHP / SEP #20 / STA $2140 /
CMP $2140 / BNE / INC A / AND #7F / PLP / RTS` and `C7/09EA` = the `$E0` reset.
A second, near-identical loop at `C7/06C1-C7/06F5` carries the same 3-byte payload but bumps
`$14` (`INC $14`) so the 24-bit source pointer can cross a ROM bank.

**So: 3 payload bytes on `$2141/$2142/$2143`, then the token on `$2140`, then spin until the SPC
echoes the token, then `token = (token+1) & $7F`.**

## C.3 The load is ONE blocking call — the CPU never leaves bank `$C7`, NMI never fires

⟦OBS `build/ct.exe --trace 300000 --from build/b80.state`⟧ — remember `--trace --from` adds 30 settle
frames, so this window is **absolute frame 110**, squarely mid-load:

```
bank histogram over 300000 instructions:   C7 = 300000   (nothing else at all)
C7/0004 dispatcher entries : 0
C7/0140 prologue entries   : 0
C7/0192 exits              : 0
NMI entries ($00:0500)     : 0
C7/06F9 (3-byte loop)      : 5314
C7/09DA (handshake)        : 5317
distinct C7 PCs            : 0342-035D, 0655-06CB, 06F9-071C, 09DA-09E9
```
5317 handshakes × 3 = 15951 bytes ⇒ ≈18.6 frames covered ⇒ **~18 consecutive frames with zero NMI
and zero non-`$C7` instructions.** Cross-check at the tail: `--trace 100000 --from build/b99.state`
(= absolute f129) is 54687 `C7` / 45313 `7E`, still inside the loop, `950` handshakes.
Cross-check after the load: `--trace 400000 --from build/b130.state` (= absolute f160) has **153**
`C7` instructions total — one ordinary `C7/061C` SFX pass-through — and 41 NMI entries.

**Port consequence:** the C port cannot model a song load as "a command, then N frames of background
transfer". The ROM stops the world. (What the *emulated* port does about that — spread it out or
block — is an implementation decision, but it must not let the field VM advance 35 frames' worth of
game logic that the ROM never ran.)

## C.4 The byte rate, measured two independent ways

Per-frame ARAM diff span vs. per-frame token delta (`token` = `inPorts[0]`), all ⟦OBS⟧. The "span" is
first-changed…last-changed and is therefore a *lower bound* by 0-2 bytes (a written byte can coincide
with the old one at either end); the token count is the unique multiple-of-3 reading that contains it:

| capture | frames | changed span | ⇒ tokens × 3 B | `tokens mod 128` | observed token delta |
|---|---|---|---|---|---|
| cold boot | 110→111 | `$721E-$7576` (857 B) | 286 → 858 B | **30 (`$1E`)** | `$5E → $7C` = **+`$1E`** ✔ |
| cold boot | 120→121 | `$9383-$96DC` (858 B) | 286 → 858 B | **30 (`$1E`)** | `$04 → $22` = **+`$1E`** ✔ |
| field | T19→T20 | `$86E7-$8A02` (796 B) | 266 → 798 B | **10 (`$0A`)** | `$50 → $5A` = **+`$0A`** ✔ |
| field | T30→T31 | `$A91B-$AC3C` (802 B) | 268 → 804 B | **12 (`$0C`)** | `$7E → $09` = **+`$0B`** ⚠ |

Three exact, one off by one token (the span 802 admits 268 tokens = 804 B, the observed delta says
267 = 801 B — i.e. the run's last byte is a coincidence and the true span is 801). Either reading is
within the ±2 boundary uncertainty. **The 3-bytes-per-token law and the per-frame rate are confirmed
empirically**, and the wrap count (2 wraps per frame in both captures) is recovered from the ARAM
diff, not guessed.

## C.5 The honest limit, and the one-line harness fix

**Per-frame sampling of `inPorts[0]` is too coarse to see the rolling token.** It advances ~270-290
times per frame while the counter is 7-bit, so a frame boundary shows only `Σ mod 128`; the wrap
count is unrecoverable from the ports alone (§C.4 recovers it only by combining with the ARAM diff).
Likewise the initial 4-byte command block sent by `C7/0A5A` (`audio_commands.md` §5) is invisible at
cold-boot f98 — it flew past inside one frame. It *is* visible at cold-boot **f23-f26** as
`cpu->spc = 10 00 FF 05` (cmd `$10`, song `$00`, `$1E02=FF`, `$1E03=05`), because the Akao driver had
only just been handed control at f23 and did not consume the command for four frames
(⟦OBS⟧ `spc->cpu = 81 BB 00 00` throughout f23-26); the song-`$00` transfer itself — 39 bytes to
ARAM `$2000-$2026`, §E.3 — then completes between f26 and f28.

**No finer trace is fabricated here.** The one-line addition that would close it, for whoever owns
`src/`: in the oracle's CPU **write** hook (the same place `--monitor --writer` filters through
`ctWramOff()`, `src/main.c:4454` + its callers at 4544/4551/4664), log unfiltered when `(adr & 0xFFFF) >= 0x2140 && (adr & 0xFFFF)
<= 0x2143` — i.e. a `--monitor "reg:2140+4"` spec that bypasses `ctWramOff`. That yields the exact
`(frame, addr, value, writerPC)` stream and turns §C.4's arithmetic into a direct log.

---

# D — THE ARAM DIFF: every changed range

## D.1 Whole-window diff, cold boot

```
build/ct.exe --aram --frames 97  --out f97.bin
build/ct.exe --aram --frames 133 --out f133.bin
python <diff, runs coalesced with a 64-byte gap>
```
⟦OBS⟧ — **ten ranges, and they account for 28735 of 28735 changed bytes; nothing else in the 64 KB
moved** (`changed < len` only because some new bytes coincide with the old ones, mostly zeros inside
BRR data):
```
$0000-$00F4  len=245    changed=53      driver page-0 scratch / SPC HW mirrors
$01F9-$01FD  len=5      changed=5       driver stack
$1DCD-$1DCE  len=2      changed=2       driver scratch
$1E80-$1E97  len=24     changed=24      *** DSP sample DIRECTORY, 6 entries x 4 bytes ***
$1F40-$1F4B  len=12     changed=12      *** per-instrument param word A, 6 x 2 ***
$1FC0-$1FCB  len=12     changed=12      *** per-instrument param word B (ADSR pair), 6 x 2 ***
$2000-$228D  len=654    changed=641     *** SEQUENCE (song $30), 654 bytes = the padded count ***
$4901-$B938  len=28728  changed=27970   *** 6 INSTRUMENT SAMPLE BLOCKS ***
$F220-$F22E  len=15     changed=8       8 bytes at EVEN offsets (one per DSP voice) = FF
$F301-$F30F  len=15     changed=8       8 bytes at ODD  offsets (one per DSP voice) = 10
accounted 28735 == whole-image 28735
```
The two `$Fxxx` runs are driver per-voice tables, not part of the upload (8 entries each, written
in the same frame the sequence lands). **NOT DERIVED: what `$F220`/`$F301` mean** (§K.3).

Everything after the load is DSP-side only: `f133→f145` changes `$C100-$DA37` and `$EC78-$F0FF`,
`f145→f200` changes `$C100-$E397` — the **echo buffer**, which the DSP writes itself. `$C100` is
immediately consistent with `ESA = $F1 - EDL*8 = $F1 - 6*8 = $C1` (song `$30` has `$1E03 = 6`).

## D.2 Load ORDER — the sliding window, frame by frame

⟦OBS⟧ single-frame diffs, cold boot:
```
f97 →f99    $4901-$4D4B  (1099 B)   <- the sample heap starts at $4901
f99 →f101   $4D4C-$5401  (1718 B)   <- contiguous continuation
f110→f111   $721E-$7576  ( 857 B)
f120→f121   $9383-$96DC  ( 858 B)
f131→f132   $B840-$B938  ( 249 B)  + $1E80-$216F   <- samples END, then DIR/$1F40/$1FC0, then the sequence begins
f132→f133   $2170-$228D  ( 286 B)                  <- sequence tail
```
**Order: samples (ascending from `$4901`) → `$1E80`/`$1F40`/`$1FC0` → sequence at `$2000`.**

## D.3 Whole-window diff, field change (loc `$011` song `$1E` → loc 5 song `$06`)

⟦OBS `--aram --from saves/truce_market.state --frames 0 --out pre.bin` vs
`--aram --from build/tp5_f1.state --frames 50 --out post.bin`⟧:
```
$1E82-$1EA7  38 B    DIR (10 entries; entry 0's start word $4901 was already there)
$1F40-$1F53  20 B    10 param words
$1FC3-$2BE7  3109 B  the $1FC0 table tail + the SEQUENCE $2000-$2BE7
$490A-$F143  43066 B 10 sample blocks ($4901-$C53E) + the echo buffer above it
$F202..$F5FB          driver per-voice tables / scratch
```

---

# E — WHAT THE NEW ARAM CONTENT IS (ROM matches, found from the ARAM side)

## E.1 The six tables — reproduced byte-for-byte, 20 instruments / 3 songs

Method ⟦OBS⟧: take the ARAM DIR at `$1E80`, take the instrument-id list at `rom[$C7:0E11 + song*32]`
(16 zero-terminated words), and for each id check `rom[$C7:0AEA + (id-1)*3]` as a 24-bit pointer to a
`[size:word][payload]` block.

**Song `$30` (cold boot, 6 instruments `$2C $13 $3A $09 $27 $3B`):**
```
srcn $20 inst $2C ARAM $4901..$6625 ( 7461 B, 829 BRR) <- $C9:BBF7+2  BYTE-EXACT | loop $491C = +27   (ROM 27)   OK | tune OK adsr OK
srcn $21 inst $13 ARAM $6626..$7624 ( 4095 B, 455 BRR) <- $C8:467B+2  BYTE-EXACT | loop $7583 = +3933 (ROM 3933) OK | tune OK adsr OK
srcn $22 inst $3A ARAM $7625..$9199 ( 7029 B, 781 BRR) <- $CA:6122+2  BYTE-EXACT | loop $7B2C = +1287 (ROM 1287) OK | tune OK adsr OK
srcn $23 inst $09 ARAM $919A..$A552 ( 5049 B, 561 BRR) <- $C7:CBD9+2  BYTE-EXACT | loop $919A = +0    (ROM 0)    OK | tune OK adsr OK
srcn $24 inst $27 ARAM $A553..$B5FC ( 4266 B, 474 BRR) <- $C9:5959+2  BYTE-EXACT | loop $AD9F = +2124 (ROM 2124) OK | tune OK adsr OK
srcn $25 inst $3B ARAM $B5FD..$B938 (  828 B,  92 BRR) <- $CA:7C99+2  BYTE-EXACT | loop $BB04 = +1287 (ROM 1287) OK | tune OK adsr OK
total 28728 B -> next free $B939  == observed WRAM $1EF6 = $B939  ✔
```
**Song `$06` (field, 10 instruments `$32 $1D $1C $1A $02 $03 $04 $1F $01 $1B`):** all 10 byte-exact,
`$4901..$C53E`, total 31806 B → next free **`$C53F` == observed `$1EF6` ✔**.
**Song `$1E` (loc 12, 4 instruments `$2C $18 $17 $38`):** all 4 byte-exact, `$4901..$8218`,
total 14616 B → next free **`$8219` == observed `$1EF6` ✔**.

Tables used, all confirmed from the ARAM side and all matching front 2's disassembly:

| ROM table | stride / index | produces |
|---|---|---|
| `$C7:0D18 + song*3` | 3, by song id | 24-bit ptr → `[size][seq]` → ARAM `$2000` |
| `$C7:0E11 + song*32` | 32, by song id | up to 16 zero-terminated instrument ids |
| `$C7:0AEA + (inst-1)*3` | 3, by instrument | 24-bit ptr → `[size][BRR]` → ARAM heap from `$4901` |
| `$C7:0BA4 + (inst-1)*2` | 2 | **loop offset**, added to the ARAM start → DIR word 2 |
| `$C7:0C20 + (inst-1)*2` | 2 | word → ARAM `$1F40 + slot*2` |
| `$C7:0C9C + (inst-1)*2` | 2 | word → ARAM `$1FC0 + slot*2` (the ADSR pair, e.g. `FF E0`) |

⟦OBS⟧ 20/20 instruments matched **all five** per-instrument facts (payload, start address, loop, both
param words) with zero mismatches.

**Note (real ROM datum, not a bug to normalise away):** instrument `$3B` is 828 bytes but its loop
offset is 1287, i.e. **the DIR loop pointer `$BB04` lies past the end of its own sample**. That is
what `rom[$C7:0BA4 + $3A*2] = 07 05` says, and the driver stores it faithfully. The port must copy the
table, not sanitise it.

**All seven words of `$C7:0028-$0037`** (`$0D18, $0AEA, $0BA4, $0C9C, $0C20, $0E11, $0AE9`) are now
accounted for: six loader tables + the song count. This closes `audio_boot.md` §8.2's "NOT DERIVED:
what, if anything, consumes this list" — the answer is **`C7/01A1`'s loader**, and each word is that
table's *first indexable entry* (the code addresses `$0AEA`-style bases with a `(id-1)` index).

## E.2 Sample blocks transfer EXACTLY `sizeword` bytes — no padding

⟦OBS⟧ each instrument's ARAM start equals the previous instrument's start + its `sizeword` exactly
(20/20 above), and after the **last** instrument the very next ARAM byte is untouched:
song `$30` last changed byte = `$B938`, `$1EF6 = $B939`; song `$1E` last changed = `$8218`,
`$1EF6 = $8219`. Every sample size is a whole number of 9-byte BRR blocks (829, 455, 781, 561, 474,
92, …) — 20/20, remainder 0.

## E.3 The sequence IS padded — `3 * (sizeword/3 + 1)` — CONFIRMED 4/4

⟦OBS⟧ for each song, "written" = how far the ARAM image at `$2000` matches
`rom[blockptr+2 …]` contiguously:

```
song | block        | written | sizeword | 3*(size//3+1) | match
 $00 | $CA:8ACC     |    39   |    36    |      39       | OK
 $30 | $CB:4796     |   654   |   651    |     654       | OK
 $06 | $CA:9EC3     |  3048   |  3045    |    3048       | OK
 $1E | $CB:0B32     |  1473   |  1472    |    1473       | OK
```
Not "size+3": song `$1E`'s 1472 is not a multiple of 3 and gets **+1**. The law is
**"round up to the smallest multiple of 3 strictly greater than `sizeword`"**, which is exactly what a
`DEX/DEX/DEX ; BNE` loop preloaded with that padded count produces (§C.2). The extra 1-3 bytes are
the ROM bytes that physically follow the block — i.e. the head of the *next* block in the chain — and
they really do land in ARAM. **A port that transfers exactly `sizeword` bytes will differ from the
oracle in the last 1-3 bytes of every sequence.**

⟦OBS⟧ the cleanest single case: song `$00` is a 36-byte sequence with **zero** instruments
(`rom[$C7:0E11+0]` = `00 00`), and the boot's f26→f28 diff writes exactly `$2000-$2026` = **39** bytes.

---

# F — PROOF THAT IT SOUNDS (the acceptance signature)

## F.1 Cold boot — first sounding frame = **139**

`build/ct.exe --aram --frames 139`:
```
aram: src=(ROM boot) frames=139
aram: spc pc=02B8 a=66 x=68 y=0A sp=FF running  apuCycles=2372809 romReadable=0
aram: ports cpu->spc $2140-43 = 00 00 00 00 | spc->cpu = 00 00 00 0F
aram: fnv32=E6CAB787 nonzero=41665/65536 first16@0200=20C0CDFFBDE8005DAFC8F0D0FB8B801A
aram: dsp mvol=64/64 dir=$1E00 evol=20/20 flg=00 voicesWithGain=1
aram:  v1 srcn=25 pitch=0BFC gain=07F7 adsrState=2 decodeOff=$B606  <-- SOUNDING
        (v0,v2..v7 all srcn=00 pitch=0000 gain=0000 adsrState=3 decodeOff=$0000)
```
**Self-consistency chain, all observed:** `dir = $1E00` ⇒ voice 1's directory row is
`$1E00 + $25*4 = $1E94` = DIR slot 5 = `start $B5FD / loop $BB04` = instrument `$3B` (§E.1);
`decodeOff = $B606` = `$B5FD + 9`, i.e. one BRR block into that very sample. `evol` flips 0/0 → 20/20
on this same frame.

Continuation ⟦OBS⟧: f140 `gain=07EF decodeOff=$B6E7`, f141 `07E7/$B7C8`, f142 `07DF/$B8A9`
(the decode pointer advances ~0xE1 bytes/frame, gain decaying) — a real, streaming note. Later notes:
f160 `v2 srcn=25`, f300 `v0 srcn=21 pitch=1523 gain=07B7 decodeOff=$686F` (inside instrument `$13`'s
`$6626-$7624` ✔), f400 `v0 srcn=21 pitch=0E19`.

## F.2 Field change — first sounding frame = **T+50**, three voices

`build/ct.exe --aram --from build/tp5_f1.state --frames 50`:
```
aram: spc pc=0B96 a=10 x=00 y=00 sp=FD running  apuCycles=145193510 romReadable=0
aram: ports cpu->spc $2140-43 = 00 00 3B 54 | spc->cpu = 00 00 00 0F
aram: fnv32=DC8532B8 nonzero=50264/65536
aram: dsp mvol=64/64 dir=$1E00 evol=15/15 flg=18 voicesWithGain=3
aram:  v0 srcn=22 pitch=17E0 gain=0000 adsrState=3 decodeOff=$7637
aram:  v1 srcn=23 pitch=0796 gain=07C7 adsrState=2 decodeOff=$76FD  <-- SOUNDING
aram:  v2 srcn=22 pitch=1F3C gain=07CF adsrState=2 decodeOff=$6476  <-- SOUNDING
aram:  v3 srcn=20 pitch=0312 gain=0000 adsrState=3 decodeOff=$6C5F
aram:  v4 srcn=20 pitch=024D gain=0000 adsrState=3 decodeOff=$5D77
aram:  v5 srcn=21 pitch=1A9E gain=0000 adsrState=3 decodeOff=$5ADD
aram:  v6 srcn=23 pitch=1F8C gain=0000 adsrState=3 decodeOff=$8285
aram:  v7 srcn=26 pitch=162E gain=07FF adsrState=2 decodeOff=$8DF2  <-- SOUNDING
```
Every **sounding** voice's `decodeOff` lands inside the matching srcn's ARAM range from §E.1:
`v1 srcn $23` → range `$7676-$8296` ⊇ `$76FD` ✔; `v2 srcn $22` → `$6248-$7675` ⊇ `$6476` ✔;
`v7 srcn $26` → `$8C66-$9C49` ⊇ `$8DF2` ✔. (`v0`/`v6` also land in their own ranges.)
The two `srcn $20` rows do **not**: srcn `$20` is `$4901-$53DD`, but `v3` reads `$6C5F` (inside
srcn `$22`'s range) and `v4` reads `$5D77` (inside srcn `$21`'s). Both have `gain=0000` /
`adsrState=3` — released and silent — i.e. stale voice records left from the previous song, not live
playback. **Only the three `SOUNDING` rows are part of the signature.**

## F.3 How to use these as an acceptance test

The port's target is not "ARAM matches" (that is `--audiotest`, already green for the boot upload) —
it is **`voicesWithGain != 0` with the same `srcn/pitch/gain/decodeOff`**. Minimum bar, in order:

1. `--aram --frames 133` equivalent: the 6 sample ranges + `$1E80`/`$1F40`/`$1FC0` + `$2000+654`
   byte-identical (§E), `$1EF6 = $B939`, `$1E05 = $E0`, `$1E00 = 0`.
2. `--aram --frames 139` equivalent: `mvol 64/64`, `evol 20/20`, `dir $1E00`,
   **`v1 srcn=$25 pitch=$0BFC gain=$07F7 adsrState=2 decodeOff=$B606`**.
3. The field case: `$1EF6 = $C53F` at T+40, released T+44, three voices at T+50 as in §F.2.

---

# G — THE WRAM SIDE, WITH WRITER PCs

## G.1 Cold boot

⟦OBS `build/ct.exe --monitor "1E00:1,1E01:1,1E02:1,1E03:1,1E05:1,1EF0:1,1EF2:1" --from build/boot60.state --frames 90 --diff --writer`⟧
(rows are relative; absolute frame = 60 + row + 1):

```
  f0      001E00=00*  001E01=00*  001E02=FF*  001E03=05*  001E05=E0*  001EF0=05*  001EF2=C9*
  f21     001E00=81*(7E:31AD)  001E01=00(7E:31A8)  001E02=FF(7E:31A8)  001E03=05  001E05=E0  001EF0=05  001EF2=C9
  f37     001E00=10*(7E:376F)  001E01=30*(7E:376A)  001E02=C0*(C7:0A4B)  001E03=06*(C7:0A53)
          001E05=30*(C7:01B5)  001EF0=06*(C7:0A02)  001EF2=C1*(C7:0A0D)
  f72     001E00=00*(C7:0196)  001E01=30  001E02=C0  001E03=06  001E05=E0*(C7:0616)  001EF0=06  001EF2=C1
```
Every driver-side writer PC is exactly the one `audio_commands.md` §5 predicts:
`C7:0A4B`/`C7:0A53` = the `$1E02`/`$1E03` fill from the song record, `C7:01B5` = the `STA $05` at
`C7/01B3` (BUSY), `C7:0616` = the `STA $05` at `C7/0614` (READY), `C7:0196` = the `STZ $00` at
`C7/0194`, `C7:0A02`/`C7:0A0D` = the SFX-bank switch inside `C7/09FD`.

**The *caller* is RAM-resident code in bank `$7E`** (`7E:31A8/31AD` for the `$81`, `7E:376A/376F` for
the `$10`) — the title/intro engine runs from WRAM, so it is not one of the 59 ROM `JSL $C7:0004`
sites catalogued in `audio_commands.md` §9. It stages the *same* mailbox bytes.

## G.2 A staged command that was DROPPED — observed, not theorised

`$1E00 = $81` from **f82 through f97** (16 consecutive frames, ⟦OBS⟧ per-frame savestate reads), then
`$10` at f98. It cannot have been dispatched and re-staged: ⟦OBS `--findoam --watch 7E1E00 8 --frames 1`⟧
over 240 boot frames reports `writer PC 7E/31AD : 2 writes` total, while `C7/0196` (the dispatcher's
exit `STZ $00`) fires **12** times and `C7/01B5` (song loads) **3** times. So the cmd `$81` staged at
f82 was **overwritten by cmd `$10` without ever reaching `JSL $C7:0004`**. A port that assumes
"write mailbox ⇒ command executes" will emit a fade the ROM never emitted.

## G.3 Field change

See §B.2. Additionally `$7E29AE = 06` (the song shadow) and `$7E0100 = 0005`, i.e. the whole
`C0/1B53` law of `audio_song_selection.md` §B.2 firing end to end.

---

# H — REPRO: every command used, in order

```bash
# --- scenario A: cold boot ---
build/ct.exe --aram --frames 97                       # last idle frame
build/ct.exe --aram --frames 98                       # first streaming frame
build/ct.exe --aram --frames 133                      # load complete ($E0 on spc->cpu[0])
build/ct.exe --aram --frames 139                      # FIRST SOUNDING VOICE
build/ct.exe --aram --frames 97  --out f97.bin
build/ct.exe --aram --frames 133 --out f133.bin       # then diff the two 64KB images
build/ct.exe --savestate build/b98.state --frames 98
build/ct.exe --read "wram:1E00+8,wram:1EF0+4,wram:1EF6+2" --from build/b98.state --settle 0
build/ct.exe --savestate build/boot60.state --frames 60
build/ct.exe --monitor "1E00:1,1E01:1,1E02:1,1E03:1,1E05:1,1EF0:1,1EF2:1" \
             --from build/boot60.state --frames 90 --diff --writer
build/ct.exe --findoam --watch 7E1E00 8 --frames 1    # 240-frame writer census
build/ct.exe --savestate build/b80.state --frames 80
build/ct.exe --trace 300000 --from build/b80.state    # NB: +30 settle -> absolute f110

# --- scenario B: field location change ---
build/ct.exe --teleport 5 8 8 0 --from saves/truce_market.state --frames 1 --out build/tp5_f1.state
build/ct.exe --aram --from build/tp5_f1.state --frames T          # T = 0..100
build/ct.exe --aram --from saves/truce_market.state --frames 0 --out pre.bin
build/ct.exe --aram --from build/tp5_f1.state --frames 50 --out post.bin
build/ct.exe --monitor "1E00:1,1E01:1,1E02:1,1E03:1,1E05:1,1EF0:1,1EF2:1,1EF6:2,29AE:1,0100:2" \
             --from build/tp5_f1.state --frames 70 --diff --writer

# --- scenario C: a SMALLER song after a bigger one ---
build/ct.exe --teleport 5  8 8 0 --from saves/truce_market.state --frames 200 --out build/tp5s.state
build/ct.exe --teleport 12 8 8 0 --from build/tp5s.state          --frames 200 --out build/tp12s.state

# --- ROM side ---
python tools/dis.py 706D0 0x60 1 0        # C7/06C1-C7/071C : the 3-byte streaming loops
python tools/dis.py 709DA 0x30 1 0        # C7/09DA / C7/09EA / C7/09FD : the handshakes
```

---

# I — WHAT THE LOADER LEAVES BEHIND (staleness rules the port must copy)

⟦OBS⟧ loc 5 (song `$06`, 10 instruments) → loc 12 (song `$1E`, 4 instruments), settled states:

```
after song $06:  $1E80 dir  = 0149 654B | DE53 5558 | 4862 1C76 | 7676 2282 | 9782 9782 | 388A 388A | 668C 668C | 4A9C 4A9C | 42A7 42A7 | E2B5 E2B5 | 0000 0000 ...
after song $1E:  $1E80 dir  = 0149 1C49 | 2666 996B | CF6B 8B73 | C377 D181 | 9782 9782 | 388A 388A | 668C 668C | 4A9C 4A9C | 42A7 42A7 | E2B5 E2B5 | 0000 0000 ...
                 (rows 0-3 rewritten for song $1E's four instruments; rows 4-9 are still song $06's
                  values, byte-identical to the line above, i.e. UNTOUCHED)
after song $06:  $1F40      = EE00 A07F BB74 B600 2000 A000 7800 C800 0000 0000 | 0205 x6
after song $1E:  $1F40      = E9F0 529C FF00 E100 | 0205 x12          <- tail RE-DEFAULTED to $0502
after song $06:  $1FC0      = FFE0 FFE0 FFEE FFF0 FFE0 FFE0 FFE0 FFE0 FFE0 FFE0 | C21F x6
after song $1E:  $1FC0      = FFE0 FFED FFE0 FFE0 | C21F x12          <- tail RE-DEFAULTED to $1FC2
```
So: **`$1F40` and `$1FC0` are fully rewritten (count entries + a default tail of `$0502` / `$1FC2`);
the `$1E80` DIR is written for `count` rows only and the rest stays stale.** Sample bytes above the
new `$1EF6` are likewise never cleared (⟦OBS⟧ the loc5→loc12 diff touches `$490A-$8218` and stops —
`$8219-$C53E` still holds song `$06`'s samples).

**Harness trap found here (not a game behaviour):** `--teleport` out of a state whose load is still in
flight aborts the blocking `JSL` mid-body and leaves `$1E05` = the old song id forever, which
*permanently wedges the driver mutex* (every later command is dropped at `C7/0157`). Always teleport
from a settled state (`--frames 200`), and check `$1E05 == $E0` before trusting a capture.

---

# J — CORRECTIONS / CONFIRMATIONS TO EXISTING NOTES

1. **`audio_commands.md` §12 "No existing `ct.exe` mode can observe the hardware ports" is STALE.**
   `--aram` prints both `inPorts[0..3]` and `outPorts[0..3]`. What remains unobservable is the
   *sequence* of writes within a frame (§C.5).
2. **`audio_boot.md` §8.2 "`$C7:0028-$0037` — no reader found ... NOT DERIVED what consumes this
   list" is CLOSED.** All seven words are the song loader's tables (§E.1).
3. **`audio_commands.md` §5's sketch of `C7/01A1` is confirmed and sharpened**: the "multi-frame
   upload" is one blocking call with NMI off, not a per-frame resumable one (§C.3).
4. **`docs/DEBUG_SURFACE.md` omits that `--trace --from STATE` runs 30 settle frames first**
   (`src/main.c:5222`). This cost real time here — a trace "from f130" is actually from f160.
   Same for `--monitor`'s row/frame offset (§A.3) and `--teleport --frames 0` falling back to a
   default frame count.
5. **Front 2's `audio_song_data.md` and this dossier were derived independently** (65816 disassembly
   vs. ARAM-diff-then-ROM-search) and **agree on every table, stride, base and destination**.

---

# K — NOT DERIVED (explicit gaps, with the exact thing that closes each)

1. **The intra-frame `$2140-$2143` write sequence.** Only frame-boundary samples exist; the rolling
   token wraps 2-3× per frame (§C.4-C.5). **Closes it:** the port-write log described in §C.5
   (one `if` in the CPU write hook). Everything about the protocol asserted here is either ROM
   disassembly (§C.2) or an ARAM-diff/token cross-check (§C.4) — never an interpolated trace.
2. **Where the padded transfer count is computed.** §E.3 proves the *law* (4/4 songs) and §C.2 shows
   the `DEX/DEX/DEX ; BNE` loop it implies, but the instruction that loads `X` with
   `3*(size/3+1)` was not located. **Closes it:** `python tools/dis.py 0x704C0 0x80 1 0` around
   `C7/04DD` (front 2's sequence-dest site) and follow the write to `$0C`.
3. **ARAM `$F220`/`$F301`** (8 bytes each, one per DSP voice, written in the same frame as the
   sequence: `FF` and `$10`). **Closes it:** an SPC700 disassembly of the driver at ARAM `$0200+`
   (there is still no SPC700 disassembler in `tools/`), or an `--at F200+0x120` sweep across a note
   on/off to see them track key-on.
4. **Which frame inside f98 the initial 4-byte command block hit the ports.** The `10 30 C0 06`
   block from `C7/0A5A` was never caught at a frame boundary in scenario A; it *was* caught for the
   song-`$00` load (`10 00 FF 05`, frames 23-26). **Closes it:** §C.5's log.
5. **Why `evol` goes 0/0 → 20/20 exactly at f139** (cold boot) and reads 15/15 in the field capture.
   Observed, not explained; it is a driver-side (SPC) decision. **Closes it:** the same SPC700
   disassembly.
6. **The `$7E:31A8/31AD/376A/376F` intro caller.** It is RAM-resident code, so `tools/dis.py` cannot
   reach it. **Closes it:** dump WRAM `$7E:3100+0x800` from `build/b80.state` and disassemble the
   image offline, or find the ROM source of the block copy.
7. **Whether a load can ever be interrupted/resumed in real play.** Never observed — both captures
   are single blocking calls. The only interruption seen was harness-induced (§I).
