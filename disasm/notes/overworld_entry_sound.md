# The OPENING overworld's sound — the entry song is a SCRIPT BRANCH, not a loader step

**Verdict in one line.** At the new-game 1000 A.D. pan the ROM plays **song `$11`**, staged by the
overworld sub-VM's **op `0x3D`** (`C2/18F1`) from the world-entry script at **`$7F:046D`**. Our engine
plays **song `$01`**, staged by `worldEnterSong()` — the port of `C2/2F0F`, a routine the ROM
**does not execute at all on this entry**. Wrong song, from the wrong arm of a branch our port never
models.

Everything below carries the command that produced it. Claims I could not measure are marked
**NOT DERIVED**.

---

## 0. The repro pair

The naming screen is the last common point before the opening overworld: `saves/crononame.state` is
loc 0, storyline 0, `$29AE = $02AE = 00`.

```
$ ./build/ct.exe --read "loc,story,7E29AE:1,7E02AE:1,7E1E00:4,7F01ED:1,7E027C:1" --from saves/crononame.state
  loc                  wram[7E0100+2] = 0000  (u16le=0 / 0x0)
  story                wram[7F0000+1] = 00  (u16le=0 / 0x0)
  7E29AE:1             wram[7E29AE+1] = 00
  7E02AE:1             wram[7E02AE+1] = 00
  7F01ED:1             wram[7F01ED+1] = 00
```

Two Start presses accept the default name and the world entry follows:

```
$ ./build/ct.exe --followscene --from saves/crononame.state --btnscript "0:60,8:6,0:30,8:6,0:1400" --frames 1500
  f0     loc=000 (scene 0) cmd=0 arrival=(255,0) facing=0
  f144   TRANSITION 000 -> 1F0  (world 0)  arrival=(63,56) facing=128 cmd=0
```

The CONTROL entry (a natural field→overworld door, the one the existing port was pinned on):

```
$ ./build/ct.exe --followscene --from saves/truce_market.state --btnscript "20:60,0:200" --frames 300
  f0     loc=011 (scene 17) cmd=3 arrival=(22,44) facing=4
  f33    TRANSITION 011 -> 1F0  (world 0)  arrival=(46,37) facing=3 cmd=3
```

---

## 1. WHAT THE ROM STAGES AT THE OPENING — per byte, with writers

```
$ ./build/ct.exe --monitor "7E1E00:1,7E1E01:1,7E1E02:1,7E1E03:1,7E02AE:1,7E29AE:1,\
7E1BE8:1,7E1BE9:1,7E1BEA:1,7E1BEB:1,7E1BEC:1" \
    --from saves/crononame.state --btnscript "0:60,8:6,0:30,8:6,0:1400" --frames 1500 --diff --writer

  f0      1E00=00  1E01=01  1E02=FF  1E03=05  02AE=00  29AE=00  1BE8=00 1BE9=00 1BEA=00 1BEB=00 1BEC=00
  f61     1E00=00(C7:0196)  1E01=00*(C2:EB0B)  1E02=80*(C2:EB1A)  ...
  f144    (loc word -> F001, writer C0:0CB8)
  f186    1E00=10*(C2:2EEE)  1E01=11*(C2:2EF4)  1E02=FF*(C7:0A4B)  1E03=05(C7:0A53)
          02AE=11*(C2:190B)  29AE=00
          1BE8=10*(C2:1900)  1BE9=11*(C2:1908)  1BEA=FF*(C2:1910)  1BEB=FF*(C2:1913)  1BEC=FF*(C2:23F9)
  f195    1E00=00*(C7:0196)  1BEC=00*(C2:2400)
  f572    1E01=45*(C2:2EF4)  1E02=80*(C2:2EFA)  1E03=FF*(C2:2F00)
          1BE8=18*(C2:18CE)  1BE9=45*(C2:18DF)  1BEA=80*(C2:18E5)
```

Read the writers off `$1BE8-$1BEB`, not off `$1E00`: the four staging bytes are written by
`C2/1900`, `C2/1908`, `C2/1910`, `C2/1913` — that is **`C2/18F1`, the op-`0x3D` handler**, verbatim:

```
$ python tools/dis.py 218C7 0x70
  218F1: SEP  #20
  218F3: LDA  $7F01ED
  218F7: BNE  ->21919          ; the field-song hold: non-zero -> do nothing
  218FB: LDA  #10 / STA $1BE8  ; cmd $10 = PLAY SONG
  21900: LDY  #01 / LDA [$58],Y
  21905: STA  $1BE9            ; the SONG, from the script
  21908: STA  $02AE            ; the shadow, UNCONDITIONALLY
  2190B: LDA  #FF / STA $1BEA / STA $1BEB
  21913: STZ  $1BED
  21916: JSR  C2/2ED9          ; the shared phase gate
  2191B: LDA  #0002 / RTS
```

`$1E02`/`$1E03` read `FF 05` at f186 only because the Akao driver (`C7:0A4B`/`C7:0A53`) overwrites
them after consuming the command; `C2/2ED9` wrote `FF FF` from `$1BEA`/`$1BEB`.

**`$29AE` is never touched** — it stays `00` through the whole opening. op `0x3D` writes `$02AE` only.

### The region-zoning routine NEVER RUNS on this entry

`--censusat` prints the ordered history of every write *including idempotent ones*, which is the only
instrument that can settle this (`C2/2F2B`'s `STA $02AE` is invisible to `--monitor --diff` whenever
it stores the value already there):

```
$ ./build/ct.exe --loadcensus saves/crononame.state --btnscript "0:60,8:6,0:30,8:6,0:400" --censusat 7E02AE
loadcensus: write history of 7E:02AE:
    [ 0] seq=52420   = 00  by C2/2C57      <- the world loader's seed from $7E29AE
    [ 1] seq=271576  = 11  by C2/190B      <- op 0x3D
```

Two writes. **No `C2/2F2E`.** And on the CONTROL door, the same probe:

```
$ ./build/ct.exe --loadcensus saves/truce_market.state --btnscript "20:60,0:250" --censusat 7E02AE
loadcensus: write history of 7E:02AE:
    [ 0] seq=43868   = 1E  by C2/2C57
    [ 1] seq=263161  = 1E  by C2/2F2E      <- C2/2F0F's STA $02AE at 2F2B
```

So `C2/2F0F` is real and it does run — but only on one of the two entries.

---

## 2. WHO CALLS `C2/2F0F` — op `0x34`, an inline address in the world script

There is **no static caller** anywhere in the 4 MB image:

```
$ python -c "...rom.find(...)..."
JSR 2F0F ['361401'] n=1      # file 0x361401 = bank $F6, not bank C2 code
JSL C2/2F0F [] n=0
JMP 2F0F [] n=0
JML C2/2F0F [] n=0
```

The caller is a **sub-VM opcode with a 16-bit address operand**. Handler table `C2/0F91[op*2]`:

```
$ python -c "...dump C2/0F91..."
op 34 -> C2/1807     op 1A -> C2/136A     op 27 -> C2/151C     op 3D -> C2/18F1

$ python tools/dis.py 21807 0x40 8 16
  21807: LDY #01 / LDA [$58],Y / STA $5B     ; the 16-bit operand
  21810: JSR C2/1819   ->  21819: JMP ($005B)
  21815: LDA #0003 / RTS                     ; len 3
```

**op `0x34` = "call the bank-$C2 routine named by the operand"**, and the world-entry script contains
`34 0F 2F` = **CALL `C2/2F0F`**.

### The three ops that matter, decoded from the ROM

```
$ python tools/dis.py 2151C 0x60 8 16          # op 0x27
  2151C: LDY #01 / LDA [$58],Y / TAX          ; ADDR16 operand
  21522: SEP #20 / LDY #03 / LDA [$58],Y      ; MASK8 operand
  21529: AND $0000,X
  2152C: BNE ->2153B                          ; bits SET   -> 2153D: LDA #0005 (fall through)
  2152E: INY / LDA [$58],Y  (sign-extended)   ; bits CLEAR -> advance by that SIGNED rel8
  21539: CLC / RTS
```
`27 <addr16> <mask8> <rel8>` — **`mem[addr] & mask` non-zero ⇒ advance 5; zero ⇒ advance += (int8)rel**.

```
$ python tools/dis.py 2136A 0x20 8 16          # op 0x1A
  2136A: LDY #01 / LDA [$58],Y / STA $58      ; absolute JUMP (target-1 convention)
  21371: LDA #0001 / RTS
```

op `0x36` = GOSUB, op `0x37` = RETURN (writers `C2/185B` / `C2/1867` below), op `0x3D` = SONG (§1).

---

## 3. THE BRANCH — `$7E:0104 & $80`, byte-exact on both entries

The world-entry script lives in WRAM at `$7F:0400` and is **identical on both entries** (read out of
the `.wram` companion `--peek` writes):

```
$ PEEK_SAVE=<scratch>/ow_open_f200.state ./build/ct.exe --peek saves/crononame.state <scratch>/ow_open.raw \
      --btnscript "0:60,8:6,0:30,8:6,0:140"
$ python -c "...hexdump ow_open.raw.wram[0x10400:]..."
7F:0400  00 13 45 00 02 13 46 00 44 36 69 06 27 04 01 80
7F:0410  08 1A 6C 04 27 04 01 40 08 1A 7E 04 27 A7 1B 04
7F:0420  08 1A 7F 04 27 A7 1B 08 08 1A 8E 04 27 AA 1B 04
7F:0430  08 1A 9D 04 27 AB 1B 01 08 1A A2 04 09 11 0E 7F
7F:0440  26 94 02 50 08 34 0F 2F 36 D3 04 35 C3 75 09 8E
...
7F:0460  09 46 0E 7F 29 02 38 20 13 7C 02 01 52 3D 11 35
7F:0470  C3 75 09 11 0E 7F 43 BC 06 7F 09 E3 0D 7F 52 52
```

(the truce-door capture prints the same `7F:0400..0420` bytes — same script, one machine).

Decoded:

| addr | bytes | meaning |
|---|---|---|
| `$0409` | `36 69 06` | GOSUB `$0669` |
| `$040C` | `27 04 01 80 08` | test `$7E:0104 & $80` → set: adv 5; clear: adv +8 |
| `$0411` | `1A 6C 04` | JUMP `$046C` (→ executes at `$046D`) |
| `$0414` | `27 04 01 40 08` | test `$0104 & $40` |
| `$041C`/`$0424`/`$042C`/`$0434` | `27 A7 1B …` / `27 AA 1B …` / `27 AB 1B …` | further tests on `$1BA7`/`$1BAA`/`$1BAB` |
| **`$0445`** | **`34 0F 2F`** | **CALL `C2/2F0F` — the region-zoned entry song** |
| `$044B` | `35 C3 75` | the INTERACTIVE object loader |
| **`$046D`** | **`3D 11`** | **SONG `$11`** |
| `$046F` | `35 C3 75` | the INTRO object loader |

`$7F:046D..0472 = 3D 11 35 C3 75 09` — read directly out of the capture.

### The branch byte, measured on both entries

```
$ python -c "...ow_open.raw.wram[0x100:0x109]..."
NEWGAME opening: 7E:0100..0108 = F0 01 3F 38 80 00 00 FF 00      -> $0104 = 0x80
$ python -c "...ow_truce.raw.wram[0x100:0x109]..."
TRUCE door:      7E:0100..0108 = F0 01 2E 25 03 11 00 16 2E      -> $0104 = 0x03
```

`0x80 & 0x80` ≠ 0 → advance 5 → the JUMP → SONG `$11`.
`0x03 & 0x80` = 0 → +8, and the rest of the chain falls through to `$0445` → `C2/2F0F`.

### The walk itself, proven by the script-pointer census

`$58/$59/$5A` is the sub-VM's long script pointer; `$4E` is the block base.

```
$ ./build/ct.exe --loadcensus saves/crononame.state --btnscript "0:60,8:6,0:30,8:6,0:700" --censusat 7E0058
    [ 1] seq=271368  = 00  by C2/0F73     ; ($59=04, $5A=7F)  -> $7F:0400
    [ 2] seq=271428  = 01                 ; 0401
    [ 3] seq=271437  = 05                 ; 0405
    [ 4] seq=271446  = 09                 ; 0409
    [ 5] seq=271456  = 68  by C2/185B     ; GOSUB -> 0669 …
    [16] seq=271546  = 0B  by C2/1867     ; RETURN -> 040C
    [17] seq=271548  = 0C
    [18] seq=271556  = 11                 ; 040C +5  == the CONDITION WAS TRUE
    [19] seq=271564  = 6C  by C2/1371     ; op 0x1A JUMP $046C
    [20] seq=271566  = 6D                 ; -> $046D
                     ( $02AE = 11 by C2/190B at seq 271576 )
                     ( $1E01 = 11 by C2/2EF4 at seq 271583 )
    [21] seq=271588  = 6F                 ; +2 == op 0x3D's own advance
```

```
$ ./build/ct.exe --loadcensus saves/truce_market.state --btnscript "20:60,0:250" --censusat 7E0058
    [17] seq=262996  = 0C
    [18] seq=263004  = 14   [19] 1C   [20] 24   [21] 2C   [22] 34   [23] 3C    ; +8, +8, +8, +8, +8, +8
    [24] seq=263130  = 40
    [25] seq=263138  = 45                       ; $0445 = 34 0F 2F
                     ( $02AE = 1E by C2/2F2E at seq 263161 )
    [26] seq=263198  = 48                       ; +3 == op 0x34's own advance
```

Both arms, both walks, both songs. Nothing here is inferred.

### Who writes `$7E:0104`

```
$ ./build/ct.exe --loadcensus … --censusat 7E0104
NEWGAME:     [ 0] seq=50663  = 80  by C0/0CC0
TRUCE DOOR:  [ 0] seq=42111  = 03  by C0/0CC0

$ python tools/dis.py 0CA8 0x30 8 16        (field code runs with D = $0100)
  0CB4: LDX $12 / 0CB6: STX $00    ; $0100 = the loc word   (writer PC 0CB8 — matches the monitor)
  0CB8: LDX $14 / 0CBA: STX $02    ; $0102/$0103 = the arrival cell
  0CBC: LDA $16 / 0CBE: STA $04    ; $0104 = the entry/FACING byte  (writer PC 0CC0 — matches)
```

`$0104` is the changeloc staging's facing byte, carried from `$0116`. `--followscene` labels it
`facing=128` (new game) / `facing=3` (door), consistent with both censuses. **NOT DERIVED:** what
bit 7 of that byte *means* to the rest of the engine, and which field-script operand supplies `$80`
for loc 496.

---

## 4. WHAT OUR ENGINE DOES — first divergence

```
$ CT_SNDTRACE=1 CT_NAMEAS=Crono TB_AUTOADV=1 CT_OWSONG=1 SDL_VIDEODRIVER=dummy \
    ./build/ct_field.exe --assets ct_assets.dat --newgame --selftest --frames 2600

overworld entry: scene 0 -> world 0 at (568,564) ngIntro=1 f32
owsong: music object at $0EB0 state=01 ctr=00C5
owsong: cell8=(71,70) region=0 song=01 field$29AE=00        <-- WE PLAY SONG $01
owsong: switch cmd=18 p=45 region=00 shadow=01   (x38)
ngintro: pan complete (1533 f) -> loc 271 (wake-up intro)
[snd] blen=2075 … cmd=10 p1=12 …                            (loc 271, at the END of the pan — correct scene)
```

**No `cmd=10 p=11` anywhere in the run.** The trace site is `src/field.c:5864`
(`if (wr[0x1E00] && getenv("CT_OWSONG"))`, fired after `CtWorldAmbientFrame` with `$1E00` cleared
first), so any op-`0x3D` post would be printed. It never is.

### THE FIRST DIVERGENCE

| | ROM | ours |
|---|---|---|
| world-entry frame | f144 (`$0100 → F001`, writer `C0:0CB8`) | f32 |
| entry-song frame | **entry + 42** (f186) | **entry + 0** (f32) |
| staged by | `C2/18F1` op `0x3D` @ `$7F:046D` | `worldEnterSong()` = the `C2/2F0F` port |
| mailbox | `10 11 FF FF` | `10 01 FF 80` |
| `$02AE` | `11` | `01` |

**The first frame the two disagree is the world-entry frame itself**: our engine stages
`cmd=10 song=$01` there, and the ROM stages **nothing at all** at its entry frame — it stages
`cmd=10 song=$11` 42 frames later, from the script. So it is *both* the wrong song *and* the wrong
frame, and both follow from the same cause.

### The defect, named

- `src/field.c:401` — `worldEnterAt()` calls `worldEnterSong(w, px>>3, py>>3)` **unconditionally**.
- `src/field.c:357-395` — `worldEnterSong()` is a faithful transcription of `C2/2F0F` (its comment
  block cites `2C50 / 2F0F / 2F15 / 2F21 / 2F25 / 2F3A / 2F3F` line by line). The *transcription* is
  right; the *invocation* is invented. The ROM has no "the world entry issues the region song" step:
  `C2/2F0F` is one arm of a script branch, reached only through op `0x34` at `$7F:0445`.
- `src/world.c:622` — `case 0x34: obNative(ram, B, op[1]|(op[2]<<8), c)` already exists, and
  `obNative` (`src/world.c:514`) handles `0x78A1 / 0x7575 / 0x754D / 0x7598` — **`0x2F0F` is not
  among them.** The one native the entry song needs is the one missing.
- `src/world.c:670-681` — `case 0x3D` is already byte-exact against `C2/18F1` (gate `$7F01ED`,
  `$1BE8=10`, `$1BE9=op[1]`, `$02AE=op[1]`, `$1BEA=$1BEB=FF`, `$1BED=0`, `adv 2`). It is never
  reached because the script that contains `3D 11` is never run.
- `src/field.c:414` (called from `src/field.c:5740`) — `worldOverlayIntroSeed()` replaces the branch
  with a **baked object snapshot**.
  Its own comment names both arms — *"the two ROM loaders (intro 7F:046F vs interactive 7F:044B)
  genuinely produce different object sets"* — i.e. the port already found `$046F` and `$044B`, the
  two `35 C3 75` loader calls, and baked their results. It baked the *object-loading half* of each
  arm and dropped the *sound half*: `$046D 3D 11` sits **two bytes before** `$046F`, and `$0445
  34 0F 2F` three bytes before `$0448`.
- `src/ct_vm.c:111` — `m->s[0x0104] |= 192;` is flagged in its own comment as an **UNCITED PORT
  SEED**. The branch byte the whole mechanism turns on is therefore a constant `0xC0` in our engine,
  not the ROM's `C0/0CBC-0CBE` store from `$0116`. `C0/0CB4-0CC0` is not ported.

---

## 5. A SECOND, SEPARATE DIVERGENCE IN THE SAME SPAN (measured, not chased)

The ambient SFX. The ROM's staging bytes are proof against repeats here, because they held a
*different* value (`10 11`) across the whole window — any `18 45` post would have changed them:

```
$ ./build/ct.exe --monitor "7E1BE8:1,7E1BE9:1" --from saves/crononame.state \
      --btnscript "0:60,8:6,0:30,8:6,0:1400" --frames 1500 --diff --writer
  f186    1BE8=10*(C2:1900)  1BE9=11*(C2:1908)
  f572    1BE8=18*(C2:18CE)  1BE9=45*(C2:18DF)
```

**The ROM stages no sound request whatsoever between f186 and f571** — 386 frames of silence —
then one `18 45 80 FF` at f572 (entry + 428).

Ours posts **38** `cmd=18 p=45` requests, and all of them are already out by frame 60:

```
$ for f in 40 60 120 300 500; do … | grep -c "owsong: switch"; done
frames=40  posts=0
frames=60  posts=37
frames=120 posts=37
frames=300 posts=37
frames=500 posts=37     (2600 frames -> 38 total)
```

A burst of 37 gull SFX in the first frames after entry, against the ROM's one, 428 frames in.
Whether that is the same root cause (the intro arm's object set being baked rather than scripted)
is **NOT DERIVED**.

Third observation, also **not chased**: the arrival cell. ROM `$0102/$0103 = 3F 38` = **(63,56)**;
ours enters at (568,564) px = cell **(71,70)**. It does not affect the song (the ROM does not run
the region lookup on this entry) but it is a real disagreement at the same moment.

---

## 6. WHAT THE FIX IS (spec, not code — this pass may not edit `src/`)

Structural, one mechanism, no scene gate:

1. **Delete the `worldEnterSong()` call at `src/field.c:401`.** No ROM site issues the region song at
   world-entry time; `C2/2F0F` has zero static callers (§2).
2. **Move `worldEnterSong()`'s body into `obNative()` (`src/world.c:514`) as the `addr == 0x2F0F`
   arm**, reached by the existing `case 0x34` (`src/world.c:622`). The body is already cited
   line-by-line against `C2/2F0F`; only its call site moves.
3. **Run the world-entry script at `$7F:0400` in the world sub-VM instead of baking its result.**
   Every op it uses is already cased in `world.c` (`0x00, 0x09, 0x13, 0x1A, 0x26, 0x27, 0x34, 0x35,
   0x36, 0x37, 0x3D, 0x50`); `case 0x27` (`src/world.c:600`) already implements
   `mem[addr]&mask ? adv 5 : pc += (int8)rel` exactly as `C2/151C` does. The intro arm then runs
   `$046D 3D 11` through the already byte-exact `case 0x3D`, and the door arm runs `$0445 34 0F 2F`
   through `case 0x34`.
4. **Port `C0/0CB4-0CC0`** so `$7E:0104` carries the ROM's entry byte (`$80` on the loc-496 intro
   entry, `$03` at the Truce door) instead of `ct_vm.c:111`'s uncited `|= 192`. Without this the
   branch in (3) is decided by a made-up constant.

If (3) is judged too large for one change, the mandate-safe *minimum* is still (1)+(2)+(4): that
removes the wrong song `$01`. It does **not** by itself produce `$11` — only running `$046D` does.
Do not special-case the intro to emit `$11`; that is the same invention in a new place.

**Grading.** `--loadcensus … --censusat 7E02AE` on the two states is the discriminating oracle: it
must read `[1] = 11 by C2/190B` on `crononame.state` and `[1] = 1E by C2/2F2E` on
`truce_market.state`. The existing `ow-song-region-zoning` check is pinned on the BOAT entry and
cannot see this: it grades the zoning arm only.

---

## 7. NOT DERIVED — stated plainly

- **What song `$11` is** (its name/track). Only the ID is measured.
- **The meaning of `$0104` bit 7**, and the field-script operand that supplies `$80` for loc 496.
- **The remaining op-`0x27` tests** at `$041C/$0424/$042C/$0434` (`$1BA7`, `$1BAA`, `$1BAB` with
  masks `04/08/04/01`) — the truce-door walk falls through all of them, so their *taken* arms
  (`$047E`, `$047F`, `$048E`, `$049D`, `$04A2`) were never exercised in either capture.
- **Whether the 37-post SFX burst (§5) shares this root cause.**
- **The arrival-cell disagreement (§5).**
- **The `$7F01ED` hold** on op `0x3D` — measured `00` on both entries, so its non-zero arm is
  unexercised here as well.

## 8. 2026-10-17: the bootstrap's entry has no host post; the exit's two posts; the SPC's exact clock

**The entry.** Under the bootstrap (owBootstrapObjects: the world's loader script runs) the script is the only thing that
posts at a world entry: its dispatch either falls through to the zoning arm `34 0F 2F` (C2/2F0F, world.c obNative, which
stages and posts in the world's first pass) or jumps to another loader, which plays its own song (`3D`) or none. The port
used to keep its host post (the region's song) whenever the dispatch did not reach `34 0F 2F` -- a post the ROM does not have.
Measured on the cartridge: Jackie's ch24 (1DD -> 1F6 by op DF at f493, and the return from `FF 23` at f1559) posts no `10`
at those entries (the loader plays `10 44` 39 frames on, by its own op); the Lavos chain's 1F7 entry
(saves/lavos_re/lavos_1d2_arrival.state, the pad of `lavos-day-chain-in-the-port`): `--monitor 7E1E00:4` shows `$1E00`
unchanged at f961. The host post stays only for a replayed snapshot (which skipped the script). Graded by
lavos-day-chain-in-the-port's mutation: the post's upload had held the port 8 frames over 1D2's `10 18` stretch (cartridge
f2684 -> f2798, 114 frames; the port 122 with the post, 114 without).

**The exit.** The world's main loop, state 2 (C2/24D5-2505, every exit into a location or another world): `JSR C2/26F0`,
then two posts, `82 00 FF` (C2/24E3) and `83 00 FF` (C2/24F4), then `JML C0:0000` -- whose field load posts its `81`. The
cartridge, the same Lavos chain at the 1F7 -> 1D2 exit (PEEK_APUTRACE=2050:2062): `$2140 = 82` at C7/0635 line 229, `83` line
240, `81` line 247, all in f2058. The port had neither of the first two (field.c owExitSoundPosts, now before the reboot in
both state-2 branches). No check grades them yet: they move no frame of any drive in the suite.

**The clock.** The port's SPC ran 17,088 APU cycles every frame; the console's odd (non-interlaced) frames have line 240 four
master cycles short (snes.c), 17,087.81 APU cycles. audio.c's CtAudioFrame now takes each frame's own (apuFrameCycles: the
frame's parity, flipped every frame, seeded from a state's ppu->evenFrame, power-on even; CtAudioFrameSkip flips it for a frame a
translated stage's bus ran). Measured: the ending's `FF 2A` logic exact for 11956 frames (5 frames a frame early before), the
scene-8 vmdiff's long upload returns on line 137 (oracle 139, before 149). The attract moved a frame or two from its first
long song handshakes on (its SPC's phase luck: a cold-booted port against the captured cartridge); those checks were
renumbered with their content unchanged.

**The stage's END in its own frame.** Under the rule's CPU the NMI that ENDs a `$C3` stage ($7E:0000 = 1) is followed, in
the same frame, by the main loop's teardown ($302A) and `JML $C0:0000` (the Epoch's landing into the world: `$7E:0000 = 1` and
C0/0000 both at f2158). The port's native stages (the race, the ending, entries 3 / 4 / 6) closed one frame later and loaded
the next; field.c now closes them and pends the reboot in the END frame (after the frame's picture), so the load is the next
host frame, as the cold reset's (the Day of Lavos' console reset keeps its own path). `ow-world-map-vs-oracle` (the Epoch's
dive, then Select's map) is on the cartridge's frames again.

**A load's last JSLs in its last frame (2026-10-18).** tools/sound_post_cmp.py (the cartridge's `$2140` command writes --
C7/0635, a song's `10` at C7/0A6E -- with their `$2141`, against the port's CT_SNDPOSTS lines) found every command of the
Lavos chain the cartridge's, one 25 frames early: the 1D3 load's `18 FF`, obj0 slot 1's `E8 FF` -- C0/56B0's `JSR C0/595C`
(C0/56C8) at f2249 line 71, C0/2848 at f2249 line 89 (--cputrace from a PEEK_SAVE at f2244): the load's init runner (C0/5709)
and slot 1 run at its END, NMI off, where the port ran them in the load's first frame and held the black after. ct_vm.c now
queues those JSLs (gLoadPost: the mailbox as each left it, `$1E03` included; the load's own song post at C0/1B8B stays at its
start) and field.c posts them in the load's last frame (CtSceneLoadPostsFlush when the hold ends): port f2250. The attract
(tools/attract_divergence.py): the 112 visit one frame longer, 422 against the cartridge's 433 (421 before); no other visit
moved. Left: a song the init posts begins its upload on the cartridge the upload's own length before the load's end
(the attract's f4220 `10 18`: 14 frames before C0/2848); the port posts it at the end.
