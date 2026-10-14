# THE $C3 SEQUENCE TABLE — all sixteen entries, and which ones the game reaches

`title_intro.md` derives ENTRY 2 (the title) and the machine it runs on. This file is the map of the
other fifteen: what the table actually holds, which entries the ROM can reach and from where, what
each reachable one puts on screen, and what is left to do for each. It exists so the next stage of
the port starts from measurement instead of from "there are fifteen more".

---

## 1. The dispatch, and the table's real extent

```
7E:3003  LDA $0384 / AND #$1F / ASL A / TAX
7E:300A  JSR ($3043,X)            the SEQUENCE TABLE
```

⚠ **`AND #$1F` allows 32 indices; the table is SIXTEEN words**, `$7E:3043..$3062`. `$7E:3063` is the
common init's code, so indices 16..31 read code as pointers. That is not hypothetical: the census
below finds `FF 7F` in scene 79, and `$7F & $1F = 31` lands on `$7E:3081`, whose word reads `$0395`.
Whatever that operand means, it is not a sixteenth-table entry, and it needs its own derivation
before anyone ports it.

The sixteen entries, and the ELEVEN distinct routines behind them:

| entry | setup | entry | setup |
|---|---|---|---|
| 0 | `$7E:3B48` — derived (§7) | 8 | `$7E:6248` (= 7) |
| 1 | `$7E:6B04` ⭐ **PORTED** | 9 | `$7E:3B48` (= 0) |
| 2 | `$7E:5978` ⭐ **PORTED** | 10 | `$7E:8538` |
| 3 | `$7E:671F` — derived (§7) | 11 | `$7E:8538` (= 10) |
| 4 | `$7E:53BB` | 12 | `$7E:8E95` |
| 5 | `$7E:7092` | 13 | `$7E:8EA8` |
| 6 | `$7E:78DD` | 14 | `$7E:8538` (= 10) |
| 7 | `$7E:6248` | 15 | `$7E:8538` (= 10) |

---

## 2. ⭐ Who calls them: a census of every `FF` site in all 512 scenes

Instruction-ALIGNED (the `refscan.py` technique: an offset counts as an op only if three independent
staggered walks agree it is an instruction start — a naive byte scan over event scripts reports
operands as opcodes). `op 0xFF` with an operand `< $80` is a `$C3` sequence; `$80-$8F` is the gate
warp (`ct-gate-warp-fd-family`) and `$90+` the iris (`ct-field-iris-statemachine`).

| operand | entry | sites | scenes |
|---|---|---|---|
| `FF 00` | 0 | 2 | 223, 225 |
| `FF 60` | 0 | 2 | 431, 433 |
| `FF 61` | 1 | 6 | 173, 366, 431, 433 |
| `FF 02` | 2 ⭐ | 4 | 419, 420, 421, 423 |
| `FF 23` | 3 | 6 | 102, 103, 104, 105, 106, 107 |
| `FF 24` | 4 | 1 | 301 |
| `FF 26` | 6 | 1 | 432 |
| `FF 07` | 7 | 1 | 476 |
| `FF 09` | 9 | 2 | 223, 225 |
| `FF 2A` | 10 | 1 | 81 |
| `FF 2B` | 11 | 2 | 81, 261 |
| `FF 2D` | 13 | 1 | 261 |
| `FF 2E` | 14 | 1 | 90 |
| `FF 2F` | 15 | 1 | 90 |
| `FF 7F` | 31 ⚠ | 3 | 79 — past the table, see §1 |

⚠ **This is a LOWER BOUND.** The walker reaches a scene's code from its object function pointers and
from staggered offsets, and it demonstrably misses some: scene 433's own `FF 02` and `FF 63` are NOT
in the table above, yet both are MEASURED running (§3). Treat a missing entry as "no site found",
never as "no site exists" — `verify-negatives-on-proper-state`.

Entries with no site found at all: **5, 8, 12**.

---

## 3. ⭐ The attract chain, MEASURED

`--monitor 7E0384:1 --diff` from a state just past the title's own end (boot 2160), 12000 frames:

```
f0     $0384 = 02      the title (title_intro.md)
f610   $0384 = 61      entry 1
f6155  $0384 = 60      entry 0
f6595  $0384 = 61      entry 1
f6767  $0384 = 63      entry 3
f9090  $0384 = 02      ...and round again
f11766 $0384 = 61
```

So the attract loop walks **entries 2, 1, 0, 1, 3** and nothing else. Between the sequence stages the
LOCATION word keeps changing (`$0100`: `$1B1 → $1F5 → $16E → $01C → $01B → $0AD …`), i.e. the demo's
gameplay footage is the FIELD and BATTLE engines running real locations — the engines this port
already has — and the `$C3` stages are what plays between them.

What each reachable stage puts on screen (rendered with `--peek` + `ct.py fb2png` at the frames
above):

* **entry 1** (`$7E:6B04`) — the STAFF CREDITS: "PRODUCER KAZUHIKO AOKI / DIRECTOR TAKASHI TOKITA,
  YOSHINORI KITASE, AKIHIKO MATSUI" in white on black. Its setup is a graphics loader (VRAM and OAM
  DMAs out of bank `$D2`), and it installs TWO tasks.
* **entries 0 and 3** (`$7E:3B48`, `$7E:671F`) — ⚠ **NOT screen transitions. That reading was wrong**
  and section 7 replaces it: both are full set pieces with their own scripts, six tasks each and HDMA.
  Entry 0 is the MOTORCYCLE RACE on the ravaged 2300 AD highway. The "wipe" was this stage's own
  opening reveal, caught at f6230/f6850 before the picture had filled in.
* the gameplay between them — the cathedral, the Zeal palace, three battles (Flea; Hench/Vamp/
  Sorcerer; Zombor), the wasteland overworld, the Epoch — is **not a sequence at all**.

---

## 4. ⭐⭐ THE ENGINE IS ALREADY PORTED; WHAT EACH ENTRY ADDS IS ITS OWN TASKS

This is the finding that prices the rest of the work. Entry 1's live state, measured with
`--monitor "7E0920:32,7E0940:16,7E0A20:2"` inside the credits:

```
$0920 = 347A 6BE5 0000 …      ⭐ the SAME task list the title uses
$0940 = (empty)               ⚠ no vblank hooks
$0A20 = 0000                  ⚠ no $C3 script
```

So the parts `src/title_intro.c` already carries — the task list and its two dispatchers, the NMI's
own order (`C3/08D0` then `C3/09A4`), the `$C3` byte-code interpreter, the sprite-package drawers,
the ROM's palette-job runner, the brightness trio and the mailbox — are the ENGINE, and every entry
runs on them. An entry brings:

1. its **setup** (`$7E:xxxx`): graphics DMAs, PPU mode/layer registers, and the tasks it installs;
2. its **tasks**, which are the animation;
3. optionally a **`$C3` script** (`$0A20`) and **vblank hooks** (`$0940`) — the title has both,
   entry 1 has neither;
4. its **graphics**, which the setup builds and which have to be captured the way
   `tools/gen/title_intro.py` captures the title's.

⚠ That does NOT make them cheap. Entry 2 needed its own render path (mode 7, then mode 2 with
offset-per-tile), and nothing says entry 1's credits or entries 0/3's wipes use either. Each stage
is still a derivation of its own tasks plus whatever the PPU is doing while they run.

---

## 5. What is left, in the order the attract walks it

| entry | what it needs | notes |
|---|---|---|
| 1 | ⭐ **DONE** — section 6 | 0 px against the oracle on all five pages |
| 0 / 3 | ⭐ **DERIVED — section 7**, not ported. Entry 0 is the MOTORCYCLE RACE, entry 3 a night panorama; neither is a transition | each needs HDMA + windows in the render first (7.3). ⚠ entry 0 also needs the `seqReq` fix below |
| 4, 6, 7, 9, 10, 11, 13, 14, 15 | in-game cutscene stages; each needs its scene reached first | scenes 301, 432, 476, 223/225, 81, 261, 90 |
| 5, 8, 12 | no call site found (§2) — and §2 is a lower bound | do not port on the strength of the table alone |
| `FF 7F` (scene 79) | ⚠ indexes PAST the table; derive what the ROM actually does | not a sixteenth entry |

⭐ **FIXED (was: a latent defect entry 0 will hit).** `CtSceneSequence` used to be
`return sc->seqReq ? sc->seqReq : -1;`, so the request `FF 00` — exactly how scenes 223 and 225 ask
for entry 0 — was indistinguishable from "no request at all". It now carries a separate `seqHas`
flag. ⚠ A flag rather than a `-1` sentinel **because the scene is zero-initialised**: `-1` would have
to be written at every construction site, and missing one would silently request entry 0 on a scene
load. With a flag, zero still means "none" and nothing else moves.

The teardown all of them share is already derived: `$7E:34E3` (the plain brightness fade that sets
`$7E:0000`) is the same routine as the title's `$34C2` without the two sound commands — entry 1 was
measured installing it (`$0920 = 0000 34E3` at the end of the credits).

---

## 6. ⭐ ENTRY 1 — THE STAFF CREDITS (`$7E:6B04`), PORTED

Five pages of names, 105 frames each, and **0 mismatching pixels of 57344** against the oracle on
every one of them. `src/title_intro.c` runs it; `tools/gen/seq_credits.py` captures the two things
it cannot compute.

### 6.1 The common init every entry shares (`$7E:3063`)

`$7E:3000` is `JSR $3063` and then the dispatch, so this runs before EVERY entry's setup:

* `$038E = $038F = 4` (the brightness period), and `STZ` over `$0380 $0381 $0388 $038C $038D
  $0390 $0391 $0392 $0393 $0394 $0395 $0396 $0366`. ⚠ **`$0382` is not in that list** — which is
  the whole reason the credits advance a page per visit (6.5).
* two fixed-source DMAs out of **MPYL** (`$4370 = $80/$34`, i.e. direction B→A from `$2134`) that
  clear `$0000..$00FF` and `$0900..$1DFF`. Measured value at this entry: **0** — which is what
  makes `$08` start at zero, and so what makes the 90-frame hold exact.
* a full PPU reset from `$7E:3102` with `DP = $2100`: **BGMODE 0**, **BG12NBA 0**, **OBSEL `$02`**,
  `$2100 = $80`, `$2132 = $E0`, and zero into every scroll, both mode-7 halves, both windows and
  **both colour-math registers**. So a stage that wants something else has to write it itself, and
  what it does not write is this.

### 6.2 The setup (`$7E:6B04`)

| what | how |
|---|---|
| palette | `$6BF9` writes four greys — `0000 77BD 4631 1CE7` — into **both** halves of the shadow, `$7E:2200` (BG) and `$7E:2300` (OBJ). ⚠ With `DB = $7E` those are WRAM, not the PPU. |
| BG font | a 512-byte packet (ROM index `$AE`, `$7E:3691` → `C3/0557`) DMA'd to VRAM word `$0000` — **32 tiles read as 2bpp**, because the mode is 0. |
| OBJ font | `$7E:66B0` streams a 2bpp source at **`$7F:0300`** through the WMDATA port and writes 8 real words then **8 zero words** per tile into VRAM `$0100` — the same glyphs padded to 4bpp, because sprites cannot be 2bpp. 54 tiles; the last six land in the tilemap and are wiped by the next step. |
| clears | three fixed-source DMAs: `$0800` bytes of `$D2:0000` (= 0) over VRAM `$0400..$07FF` (exactly the 32×32 tilemap), `$20` bytes of the same over the OAM **high** table, and `$0200` bytes of the `$E0` at `$7E:4565` over the OAM **low** table. `$E0` parks a sprite past the bottom of a 224-line screen. |
| text | `$2181/$2183` are pointed at the page's stream and the two decoders below read it back through `$2180`. |
| registers | `$212C = $11` (BG1 + OBJ), `$2107 = $04` (map at word `$0400`), `$2101 = $00` (OBJ tiles at word `$0000`). |
| tasks | `$038E/$038F = 1`, then `$347A` and `$6BE5`. |

Then `$7E:300D` calls **`$388B`** — the same CGRAM DMA the title installs as a vblank hook, here run
once from the main dispatch.

### 6.3 The two text decoders — ⭐ the credits are not a picture

Both run with `DP = $2100` and read the text **out of WRAM through the `$2180` port**. A stream is a
list of records and `$FF` ends it.

```
$6C4D  BG:   <vword lo> <vword hi> <ASCII...> 00      tile = char - $40, and SPACE ($20) is
             VMDATAL = tile, VMDATAH = 0              remapped to $40 FIRST so it lands on tile 0
$6C71  OAM:  <x> <y> <ASCII...> 00                    tile = char - $21, attribute 0, SPACE draws
             writes x, y, tile, 0 through $2104       nothing -- and the pen advances FIVE pixels
```

⚠ `$6C71`'s `$0015/$0017` are **absolute WRAM** (the pen), not the page-`$2100` registers its
neighbour uses; and it writes through `$2104`, i.e. the PPU's own OAM — **not** the `$7E:0700`
shadow the title's tasks draw into. Entry 1 installs no vblank hook, so nothing would ever DMA that
shadow. The five-pixel pitch on an 8×8 cell is how the caption font is kerned.

The five pages (`$7E:6CAC` BG pointers, `$6CB6` OAM pointers, streams at `$6CC0..$6E41`):

| page | BG (big, white) | OAM (small caption) |
|---|---|---|
| 1 | KAZUHIKO AOKI / TAKASHI TOKITA, YOSHINORI KITASE, AKIHIKO MATSUI | PRODUCER, DIRECTER *(sic)* |
| 2 | AKIRA TORIYAMA / YUJI HORII, HIRONOBU SAKAGUCHI | CHARACTER DESIGN, SUPERVISOR |
| 3 | YASUNORI MITSUDA / MINORU AKAO / EIJI NAKAMURA | MUSIC, SOUND PROGRAM, SOUND ENGINEER |
| 4 | KEIZO KOKUBO, KATSUHISA HIGUCHI | PROGRAM |
| 5 | YASUHIKO KAMATA, MASANORI HOSHINO, TETSUYA TAKAHASHI | GRAPHIC |

### 6.4 The three tasks, and the clock

```
$7E:347A  fade IN    DEC $038F / reload from $038E / INC $038D / at $0F -> $39A7 (delete me)
$7E:6BE5  the hold   LDA $08 / CMP #$005A  ->  $39B1 (REPLACE me with $34E3)
$7E:34E3  fade OUT   the same walk down / at 0: INC $00 -> $39A7
```

⚠ `LDA $08 / CMP #$005A` is **sixteen bits** — the task dispatcher `$398A` enters with `REP #20`,
and the NMI's counter is `REP #20 / INC $08` at `C3/0910`. It reads the overlay's *global* frame
counter, which works only because 6.1's DMA zeroed it.

⭐ `$34E3` is the teardown **every** entry shares. The title's `$34C2` is the special one — it is
`$34E3` plus the two sound commands, because only the title has to stop the music.

Measured, oracle boot f2788 = the setup: fade task out of the list at **f2803** (15), swap at
**f2878** (90), `$7E:0000` at **f2893** (15). Total 105.

⭐ **The page never shows level 1** (2026-09-28, audit item 4). The task runs inside the NMI (v 228 on
the logical oracle, `--cputrace` with CPUTRACE_BEAM), after the NMI has pushed `$038D` = 01 to
`$2100`. The frame where `$038D` reaches 0, the main loop at `$7E:3026` sees `$00` on the NMI's
return and jumps to C0:0000 (v 231), and C0/0B4E stores `$2100 = 80` before line 0. Measured on
the rule's cartridge (`--monitor 7E038D:3,7E0000:1`, the page before 0A5): `$038D` 02 at c5587, 01
at c5588, 00 with `$00` = 01 at c5589. c5588 shows level 2 and c5589 is black. The same holds on
all five pages of the lap: the cartridge capture's last lit frame has max 31, then black. The port
showed level 1 for one frame on each page (`ctTaskFadeOut` now blanks `t->l.bri` in that vblank).

### 6.5 ⭐ The page counter survives the reboot

`$6B8A` is `LDA $0382 / INC A / CMP #$0006 / BNE + / LDA #$0001 / + STA $0382`, and 6.1 does not
clear `$0382`. Measured across one plain boot: the setup runs at **f2788, f4496, f5525, f7509,
f8773** with `$0382` = 1, 2, 3, 4, 5. So entry 1 runs **five times** per attract lap, not twice —
the `$0384` diff in section 3 misses the visits where the word was already `$61`.

### 6.6 What the port carries, and what it captures

`CtTitleIntroResetStage(t, 1)` is 6.1 + 6.2; the three tasks are in `src/title_intro.c` next to the
title's. **Captured** (`tools/gen/seq_credits.py`, with refusals): the two fonts, VRAM words
`$0000..$03FF`, because the boot's decompressor built them; and the overlay's own text streams and
pointer tables. **Not captured:** the tilemap, the OAM, the palette and every frame count.

⭐ Porting this entry also took the title's own PPU constants out of the render. `$2107`, `$2108`,
`$210B`, `$2101` and `$2131` are now read like registers, mode 0's 2bpp fetch and layer order are
transliterated from `snes/ppu.c` alongside mode 2's and mode 7's, and the title still measures 0 px
on all of its pinned frames. ⚠ BG3/BG4 have **no** registers here — mode 0 puts them in the layer
order but no ported stage enables them in `$212C`, so `$2109/$210A/$210C` were never derived and
`tiBgPixel` refuses them rather than guessing.

### 6.7 ⚠ The field wiring is STRUCTURAL, and unexercised

`src/field.c` now opens any ported table entry the same way the ROM does — `seqId & 0x1F` into the
dispatch, `CtTitleIntroResetStage`, tick-then-render, and the reboot when `$7E:0000` comes up. It is
the title's own path with the music taken out, and that path is graded.

⚠ **But no check reaches it for entry 1, because the port cannot get to an `FF 61` site.** All six
of them are inside the attract demo: scenes 431 and 433 issue `FF 02` on their first frames (the port
does run that — it plays the title and reboots into loc `$1B1`, the same location the oracle's
attract enters), and the credits only come *after* the demo's own footage has played. Measured: the
port runs 5000 frames of it without issuing `FF 61`. So the stage is graded standalone through
`--seqstage`, and the wiring that would reach it in play is asserted, not measured. Do not read the
"entry 1 is ported" line as "the attract works".

### 6.8 ⚠ One thing the PIXELS cannot grade

The low OAM table is filled with the `$E0` at `$7E:4565`, which parks an unused sprite past the
bottom of a 224-line screen. Zero it instead and all 112 unused sprites sit at (0,0) — and **not one
pixel changes**, because a parked sprite draws through CGRAM entries this stage never defines. The
credits write only colours 0-3 and 128-131; the rest of the `$7E:2200` shadow is whatever ran before,
and **219 of 256 entries differ between the page-1 and page-2 captures**, so it is not stable state
and seeding it would be invention *and* wrong. The bytes are gradeable where the pixels are not, so
the OAM is diffed against the oracle's own `.oam` sidecar as well (`seq-credits-oam-vs-oracle`).

Harness: `ct_field.exe --seqstage <entry> <frames> [page]` (dumps a frame to `CT_SEQSHOT`, prints the
OAM digest) and `--seqtimeline <entry> <visits>`; `tools/seq_stage_cmp.py` diffs against the oracle
and finds its own alignment by reading `$7E:0008` out of a probe run. Pinned as
`seq-credits-timeline` and `seq-credits-oam-vs-oracle` (quick), `seq-credits-vs-oracle` and
`seq-credits-pages-vs-oracle` (full).

---

## 7. ⭐ ENTRIES 0 AND 3 — WHAT THEY ACTUALLY ARE, AND WHAT THEY COST

⚠ **Section 3 called these "the demo's own screen transitions". That was wrong.** It was read off two
frames caught during each stage's opening reveal, before the picture had filled in. Neither is a
transition. Both are full set pieces — their own `$C3` script, six tasks each, and HDMA — and both
need PPU machinery the port does not have.

### 7.1 Entry 0 (`$7E:3B48`) — the MOTORCYCLE RACE

The ravaged 2300 AD highway, with the bikes as sprites on a receding road.

⭐ **One routine, two table slots.** `$3B48` opens `LDA $0384 / AND #$0F / BEQ + / INC $44` — the
operand's LOW NIBBLE, not the table index, tells it which caller it has. `$60` (the attract) gives 0
and skips; `$09` gives 9 and bumps `$44`. That is how entries 0 and 9 share one routine. The next
test is `BIT #$20`: with bit 5 clear it plays song `$2C` (`JSR $375F`), so the attract's `$60`
runs it SILENT and the in-game `$09` with music.

Callers: `FF 00` and `FF 09` in scenes 223 and 225 (the race itself), `FF 60` in 431/433 (the demo).

Live state at boot f8400, and it is still running at f8600 (239+ frames):

```
$0920 = 42CE 3749 3754 428B 4F54 4D0D     six tasks ($3749 = the script task, ported)
$0A20 = 40EA                               its own $C3 script
$038C = 3F                                 SIX HDMA channels
```

⭐ **It is MODE 7 with a per-scanline matrix** (`ct.exe --hdma`, measured, all indirect):

| ch | writes | mode | what it is |
|---|---|---|---|
| 0 | `$2126` | 0 | window 1 left edge, per line |
| 1 | `$211B` | 3 | **M7A/M7B, recomputed every line** |
| 2 | `$211D` | 3 | **M7C/M7D, every line** |
| 3 | `$210D` | 3 | BG1 H/V scroll, per line |
| 4 | `$2131` | 1 | **CGADSUB per line** — the road's haze walks `91 EE`, `91 ED`, `91 EC` … one step every two lines |
| 5 | `$2105` | 0 | ⚠ **BGMODE changed MID-FRAME** — the sky is not the same mode as the road |

`cgadsub = $50` (half + OBJ), `cgwsel = $02`. The task `$7E:6A23` is what rebuilds the matrix table
at `$F000` every frame (`LDA $08 / LSR A` alternating), and `$73C6` feeds it through the PPU's own
multiplier (`$211B`/`$211C` + `$3461` cosine, reading the product back at `$2134`).

### 7.2 Entry 3 (`$7E:671F`) — a night panorama with a reflected skyline

A red moon over a lit skyline shimmering in water. Callers: `FF 23` in scenes 102-107, `FF 63` in the
attract.

⭐ **Two scripts from one entry.** `LDA $0384 / BIT #$0040` picks `$6902/$693A` or `$6898/$68A1` into
`$0A20`/`$0B20`, and the same bit also rewrites `$2123` ($33 → $FF) and three bytes at `$7E:6A5F`,
`$6A82`, `$6AAB`, and takes `$038C` from `$0E` to `$0F`. So `FF 23` and `FF 63` are two different
views of one scene.

```
$0920 = 3749 3754 73C6 6A23 69AF 37FA     ($3749 script + $37FA mulsave are ported)
$0940 = 388B 380B 66DD                     ($388B CGRAM + $380B OAM are ported)
$0A20 = 689E                               script running
$038C = 0F                                 four HDMA channels
```

PPU (measured): **mode 2**, BG1 map `$0000` chr `$1000`, BG2 map `$6800` chr `$1000` (32×64), BG3 map
`$7000` — ⭐ **the same offset-per-tile table the title's logo ripples on**. TM `$13`, TS `$12`,
CGWSEL `$02`, CGADSUB `$01` (BG1 only). Windows: `$2123 = $33`, `$2126/$2127 = $F808`, `$2129 = $FF`,
`$212E = $1F`.

HDMA, four channels, all indirect:

| ch | writes | mode | what it is |
|---|---|---|---|
| 0 | `$2128` | 0 | window 2 LEFT edge, moving per line (`63` above line 112, then `5F 5F 5F 5C …`) |
| 1 | `$210D` | 2 | **BG1 H-scroll per line** — the water's ripple |
| 2 | `$2110` | 2 | BG2 V-scroll, one step at line 70 |
| 3 | `$2108` | 0 | ⚠ **BG2's TILEMAP BASE**, `$60` → `$6A` at line 70: sky above, city below |

The HDMA channels are installed by `$7E:36CC`, which walks a **5-byte** record table (DMAP+BBAD,
table address, bank — the bank word is stored to both `$4304` and `$4307`) until a `$FF`, with
`DP = $4300`. Entry 3's table is at `$7E:685E`.

### 7.3 ⚠ WHAT THE PORT LACKS — and why neither is a small job

The engine is shared and already ported (section 4), and entry 3 even reuses the `$C3` interpreter,
mode 2, offset-per-tile and the palette jobs. What is missing is in the RENDER:

1. **HDMA.** The port renders a whole frame from one vblank latch. Both stages change registers
   *per scanline*, and entry 0 changes **`$2105` itself** mid-frame. This is a structural change to
   `CtTitleIntroRender`, not an addition: the per-line register state has to be evaluated before each
   line's fetch.
2. **Windows.** `$2123`/`$2126`-`$2129`/`$212E` — the mask logic and its per-layer main/sub enables.
   Nothing in the port models a window today.
3. **A per-scanline mode-7 matrix** for entry 0, where the port currently latches one matrix a frame.
4. **Nine new tasks and two scripts** (`42CE 428B 4F54 4D0D` + `3754 73C6 6A23 69AF`), each to be
   derived like the title's were, plus the new vblank hook `$66DD`.
5. **Their graphics**, captured the way `gen_seq_credits.py` captures the credits'.

⚠ **Each of these two is the size of entry 2 (the title), and entry 0 is additionally an INTERACTIVE
minigame, not an animation.** Nothing here is open-ended — every item above is measured and
derivable — but it is not one sitting, and no part of it should be shipped ungraded.

---

## 8. ⭐ THE INTERPRETER IS RE-ENTRANT, AND THE PORT NOW HAS HDMA AND WINDOWS

### 8.1 ⭐⭐ `$7E:3749` and `$7E:3754` are the SAME interpreter on two register pages

Entry 3's task list holds both, and they differ by three bytes:

```
3749: LDA #$0A00 / TCD / PHK / PLB / JSL $C3:0AFF / RTS      page A
3754: LDA #$0B00 / TCD / PHK / PLB / JSL $C3:0AFF / RTS      page B
```

⚠ **This was nearly missed to a register width.** Disassembled with an 8-bit accumulator the first
reads `LDA #$00 / ASL A / TCD`, which looks like "DP = 0" and makes no sense; the task dispatcher
`$398A` enters with `REP #20`, so the operand is sixteen bits and the instruction is `TCD` of
`$0A00`. The raw bytes settle it: `A9 00 0A 5B` and `A9 00 0B 5B`.

So the `$C3` byte-code interpreter is **not a singleton**. Its "sixteen registers" are whatever page
DP points at, and entry 3 runs **two scripts concurrently** — `$671F` writes `$0A20` *and* `$0B20`,
and the two tasks step them independently. The title only ever ran page A, which is why the port has
one instance; entry 3 needs the interpreter parameterised by page.

That also explains `$0B00` in `$6A23` below: it is page B's register 0.

### 8.2 The two tasks that make the water shimmer

```
$7E:6A23   X = 0; carry = bit 0 of the frame counter $08; A = $0B00 (page B, register 0)
           carry set:   [$F000+X] = A, [$F000+X+2] = 0, ... to X = $01C0
           carry clear: [$F000+X] = 0, [$F000+X+2] = A, ... to X = $01C0
```

⭐ 224 sixteen-bit entries — one per scanline — alternating between page B's register 0 and zero,
**and which set gets the offset flips every frame**. HDMA channel 1 feeds that table to `$210D`, so
alternate lines are pushed sideways and the pattern inverts at 60 Hz: the reflection's shimmer. The
amplitude is a script register, so the script controls how rough the water is.

`$7E:66DD` is the vblank hook, and it is the title's `$6212` in a different dress: it DMAs 128 bytes
from `$7E:0043` into VRAM word `$7000` (the offset-per-tile table), then writes `$2111` from
`$003D/$003E` and `$210E` from `$0B02/$0B03` — page B's register 1.

`$7E:73C6` drives the PPU's own multiplier: `$3461` (cosine) into `$211B`, `$0A00 >> 2` into `$211C`,
and reads the product back at `$2134`, then does it again with `$3465` (sine). The port already has
both trig routines.

### 8.3 What landed: HDMA and the windows

`src/title_intro.c` now renders from a **per-line register file** seeded from the vblank latch, and
the HDMA walker rewrites it exactly where the hardware would. Transliterated from `snes/dma.c`:
`dma_initHdma` at line 0, `dma_doHdma` at the end of every visible line (so the line about to be
drawn has had one run per line above it), `bAdrOffsets`/`transferLength` verbatim, indirect mode
including the last-active-channel special case. The windows are `ppu_getWindowState` verbatim, plus
`$2130`'s clip and prevent-math tests against the colour window and the `$212E`/`$212F` gates.

⭐ **It is graded against the cartridge already** — `seq-hdma-walker-vs-oracle` replays entry 0's six
real channels over its real 64 KB `$7E` page and reproduces `ct.exe --hdma`'s own scanline trace band
for band: `math=02` to line 56, then CGADSUB `$91` with the fixed colour stepping 14,13,12…0 one
notch every two lines, then `math=10` from 87.

⚠ **Two gaps the measurement itself found.** `$2132` (COLDATA) is a second register on channel 4's
mode-1 transfer, so the fixed colour is *not* always zero — the render had it hardcoded. And the
title's colour math only ever worked because CGWSEL `$02` bit 1 means "add the SUBSCREEN": that was
implicit in the old render and is now the register `addSub`. Both were invisible until a stage other
than the title was measured.

Still not graded: a window pixel. Entry 0's trace does not report window state, so that waits for
entry 3's render.

---

## 9. ⚠ WHY THE PORT'S ATTRACT STOPS: overworld object-VM op `$05` is a no-op

Reported as "the game's presentation after the intro is still broken". It is, and it is one stubbed
opcode.

### 9.1 What the ROM does

The attract's overworld leg is a **scenic flyover, not gameplay**: measured from boot 2380 over 130
frames, the overworld mover never moves (`$7E:0D84`/`$0D88` stay 0), `$1B32` never flags an entry
cell, and `$7E:00F0` is never written — so it is **not** input-driven, **not** mover-driven, and
there is no canned input stream.

The location changes anyway:

```
--monitor 7E0100:2 --from <boot 2380> --diff --writer
  f0     7E0100=F501*            $01F5, the world-5 overworld
  f115   7E0100=6E01*(C2:113B)   $016E, a field scene -- 95 frames later
```

`C2/1138 STA $0100` sits in the overworld→field ENTRY routine at `C2/1100`, which reads a 7-byte
record through `[$58],Y`: `$0100` (masked `$01FF`), `$0104` (the arrival bits, `LSR`'d), `$0102` and
`$0103` (the arrival tile). Its prologue saves the old location into `$0105`/`$04FC` and builds
`$0108`/`$04FE`/`$04FF` from the camera cells `$E5`/`$E9`.

And `$58` is not a mover probe — it is a **script pointer**:

```
C2/0F63  LDX $4E / LDA $0009,X -> $5A / LDA $0007,X -> $58    the object's script pointer+bank
C2/0F73  LDA [$58] / AND #$00FF / ASL A / TAX / JSR ($0F91,X) the opcode dispatch
```

Measured live, `$58 = $7F:0594` — a bank-`$7F` script. And in the table at `C2:0F91`:

⭐ **entry 5 is `C2/1100`.** The attract leaves the overworld because an overworld OBJECT SCRIPT
executes **op `$05`**, the "enter this location" opcode.

### 9.2 What the port does

`src/world.c`, in `obRun` — the same VM, reading the same bank-`$7F` scripts at `ram + 0x10000 + pc`:

```c
case 0x05: adv = 5; break;                    // queue transition command: ambience no-op
```

The LENGTH is right (opcode + four operand bytes = 5, matching the `[$58],Y` reads for Y = 1..4).
The EFFECT is missing. So the port enters world 5 at f2124 and **never leaves**: no location change
at all through frame 5000, which is also why it can run five thousand frames of "demo" without ever
issuing the `FF 61` that would show the credits (section 6).

Stubbing it was reasonable when only the ambient objects — gulls, boats, the fair balloons — were in
scope; those never execute op `$05`. The attract's driver does.

### 9.3 ⚠ Two wrong turns on the way here, recorded so they are not repeated

* **"world 5's blob is missing from the assets."** False. All eight worlds are packed (world 5 is
  67 582 bytes) and `worldInit: world 5 fmt 5` runs fine. The `worldInit:` line is gated behind
  `WORLD_DBG`, so its absence from a log proves nothing — that is what the wrong inference was built
  on.
* **"the tilemap streamer is broken for world 5."** Not shown. `--checkowstream` on a state captured
  at boot 2450 reports 2 294 526 mismatches, but that state is **mid-load** — the block grid at
  `$7E:4000` has only 34 distinct bytes and opens with a long run of `$20`. The ROM passes through
  that transient in about 95 frames; the port sits in it forever, which is why the picture looks
  like a flat wash with tile fragments. Grade a streamer only on a SETTLED state.

### 9.4 ⚠ Op `$05` is implemented — and the attract STILL cannot reach it

`CtWorldObjOp05` (`src/world.c`) is the opcode, byte-for-byte: all seventeen writes `ct.exe
--callrom C21100 --writes` reports, pinned by `ow-objvm-op05-vs-rom` (five mutations, five killed).
Three quirks are in the pin — operand byte 2 is read twice (16-bit as the destination's ninth bit,
masked `$01FF`, and again 8-bit shifted right as the arrival flags); `$0107`/`$04FE` take a 16-bit
store and `$0108`/`$04FF` an 8-bit one immediately after, so the column's high byte is written and
then overwritten by the row; and `$0109` comes from the permutation at `C2:2509` (`01 00 03 02`).

**And the demo still stops.** The object that executes it is not there:

| `$0DB0` | class | script |
|---|---|---|
| the ROM, in the attract (boot 2380) | `$0F63` — a script-VM object | `$7F:0594` |
| the port's world-5 asset | `$3404` — the interactive **PLAYER** | `$7F:0471` |

The ROM builds a **different object set depending on how the world was entered**: seven blocks in the
attract's flyover, thirteen in an interactive entry. The port has one — `saves/ow_w5.live.wram`,
captured with `--owforce`, i.e. an interactive entry — and `worldEnterAt` restores it verbatim. So
the attract gets the player's own block where its driver script should be, and no object in the
port's world 5 ever executes op `$05`.

⭐ **That is the real blocker, and it is structural**: the port REPLAYS a captured object set where
the ROM BUILDS one. Fixing it means deriving the world's object initialisation — which entry path
seeds which blocks, and from what — not another opcode. Until then `ow-objvm-op05-vs-rom` is what
keeps the opcode honest.

### 9.5 ⭐ The overworld BUILDS its object set — and op `$35` was what stopped the port

9.4 said the port "replays a captured object set where the ROM builds one". Half of that was wrong:
the port has the machinery, it was parked one opcode short.

**What the ROM does at world entry**, measured from a state just before the attract's flip:

```
--monitor 7E0C37:3 --from <boot 2250> --diff --writer
  f10  7E0C37=00047F*(C2:04F7)     ONE object, class $0F63, script $7F:0400
  f13  7E0DB7=94057F*(C2:0F89)     ...and by f13 there are seven
```

So the array is not restored, it is **grown from a single bootstrap object**. Two primitives:

* **`C2/048A`** — the allocator. First-fit from **`$0C30`** (not `$0B30`) over 60 slots, class from X,
  `STZ $0003,X` (16-bit, so +3 and +4) and `STZ $0002,X`. ⭐ It ends in `MVN $00,$00` over `$3B` bytes
  from the **PARENT's** block at +5: a spawned object INHERITS its spawner's state. `TYX` leaves X one
  slot PAST the block it filled, which is why `C2/04D9` does `SBC #$0040`.
* **`C2/04D9`** — spawn a SCRIPT object: bank in A, pointer in X, class `$0F63`, pointer to +7, bank to
  +9. Op `$09` (`C2/11F6`) is its caller, and the port already had it.

⭐⭐ **`C2/1839`, op `$35`:** `LDY #$01 / LDA [$58],Y / TAX / JSR $048A / LDA #$0003` — allocate an
object whose CLASS is the 16-bit operand, with no script pointer, because a native class is
dispatched by its `$0000` word. World 5's bootstrap is `35 49 78` at `$7F:0439`, and `obRun` had no
case for it: the default yields **without advancing the pc**, so the bootstrap parked there forever.
Four blocks instead of thirteen — at 16 frames, and still four at 300.

With it, the port's own VM builds the set from the bootstrap and the CLASS in every slot matches
`objSnap`, the live oracle capture the world asset was packed from:

| world | built | vs the oracle |
|---|---|---|
| 5 (the attract's) | 13 | ⭐ **12 of 12, 0 differ** |
| 7 | 10 | 9 of 9, 0 differ |
| 0 | 1 | ⚠ 17 differ |
| 1 | 2 | ⚠ 18 differ |

⚠ **`worldEnterAt` still RESTORES the snapshot.** ⭐ SUPERSEDED BY 9.6-9.9: at the time of
writing only worlds 5 and 7 built exactly, and worlds 0 and 1 were the blockers. Six now do
(0, 2, 3, 5, 6, 7); the two left are **1 and 4**, and 9.9 shows both are yield TIMING rather than
opcode coverage. The pinned checks are the gate, and the restore can go when those two reach their
captures.

⚠ **A CORRECTION TO MY OWN FIRST READING.** I wrote that worlds 0 and 1 "park on some further
missing op". That is not supported and two things disprove it. `case 0x08` IS implemented (it was
missing from a partial `grep` of the case list, not from the code), and `obRun`'s `default` parks
safely rather than dying — yet world 0's bootstrap **dies** inside one frame, through op `$52`, where
the ROM's lives thirty-six (`$0C30` zeroed by `C2:054A` at f120, spawned at f84). It is not parking
at all.

What is measured instead: **these scripts BRANCH ON WRAM.** World 0's third op is `27 04 01 80` —
"if (`$0104` & `$80`) == 0, jump" — and `$0104` is the arrival-flags cell, which the ROM holds at
`$03` when it enters world 0 (measured on the Truce-market door). The diagnostic does not reproduce
an entry state, so it cannot be read as opcode coverage. Varying that one byte moves world 0 between
1 and 5 live blocks, and **neither branch reaches its 18-block capture**, so at the time of writing
the cause was still open. ⭐ It is not any more: 9.6 found it (the port's own uncited `$1BAB` seed),
and with that plus ops `$4D` and `$3E` world 0 now builds 18 of 18.

⭐ The same sweep is what makes the world-5 pin trustworthy: 12 of 12 at `$0104` = as-found, `$00`,
`$03`, `$80` and `$FF` alike. That result is about the opcodes, not about a lucky cell.

### 9.6 ⭐ What was actually stopping worlds 0-4: an UNCITED SEED, and a second missing opcode

9.5 left the cause of worlds 0/1 open. Tracing the bootstrap **op by op** (`CT_OWOBJTRACE=1`, added
for this — a per-FRAME pc is far too coarse, since the bootstrap runs dozens of opcodes inside one
frame) found two things, one of which was the port's own.

**⭐⭐ `$1BAB` was seeded by the port and the ROM never sets it.** `src/ct_vm.c`'s cold init carried

```c
m->s[0x0104] |= 192; m->s[0x1BA7] |= 14; m->s[0x1BAA] |= 4; m->s[0x1BAB] |= 1; m->s[0x1BA8] |= 16;
```

under a comment that already called them "UNCITED PORT SEEDS ... needs its own derivation". The
oracle holds `$1BAB` at **zero everywhere measured** — boot f96/f100/f141, the settled title at
f2060, scene 433 at f2280, the attract's own overworld, **all eight** `ow_w<n>.live` captures, and
the Truce-market door, where `--monitor 7E1BAB:1 --diff --writer` reports no write at all across the
transition. The census already said the load never writes it, so the port's bit was permanent.

It is not harmless. World 0's bootstrap runs a chain of flag tests and the sixth is
`27 AB 1B 01 08` — "if (`$1BAB` & 1) == 0, jump". With the bit set the script fell through instead,
jumped to `$04A3`, spawned ONE object and hit op `$52` (die). ⭐ That is the whole of "world 0 dies in
one frame where the ROM's lives thirty-six". Only the `$1BAB` seed was removed: it is the one with
both a measurement and a demonstrated effect.

**Op `$4D` (`C2/1566`)** — op `$4C`'s mirror. `CMP [$58],Y / BCC ->1585` takes the FALL-THROUGH when
the cell is below the immediate, so the branch is `>=`; sign-extended displacement on the taken arm
(`BPL / ORA #FF00`), `LDA #$0005` on the other. World 0's bootstrap parks on it at `$7F:0503`
(`4D A6 1B 51 09`).

| world | before | after |
|---|---|---|
| 0 | 1 block | 3 |
| 1 | 2 | 9 |
| 4 | — | 15 |
| **5, 6, 7** | — | ⭐ **exact against the oracle** |

Three worlds now build exactly and are pinned separately. ⚠ `worldEnterAt` still restores the
snapshot, because 0-4 do not.

⚠ **And a correction.** I moved op `$4D`'s mutation onto the world-6 check reasoning that world 6
"only started matching once `$4D` existed". Wrong — world 6 had simply never been run before. The
mutation survived there too and was deleted with its reason: the only bootstrap measured to execute
`$4D` is world 0's, which parks on it and does not reach its capture, and worlds 5/6/7 never run the
opcode at all. It becomes gradeable when world 0 bootstraps.

### 9.7 ⭐ World 0's bootstrap now walks the ROM's path exactly — the next gap is op `$3E`

With the `$1BAB` seed gone and op `$4D` in, world 0's bootstrap runs the cartridge's own trace:

```
ROM   f84  $0C37 = $0400     (spawned)
      f85  $0C37 = $050C
PORT  frame 0: $0400 ... $0503 (op $4D) -> $0508 (op $09 spawns $7F:06AB) -> $050C
```

⭐ The whole of the ROM's first frame, op for op, ending on the same instruction. It then parks at
`$050C` on **op `$3E`**, which the port does not have.

`C2/1947` is short — `LDA [$58],Y / STA $00 / JSR $09C5 / LDA #$0002` (length 2) — but `C2/09C5` is
not: it switches on `$00 & 3` and rewrites the scroll shadows (`$E3`/`$E7` scaled by 8 into
`$20`...), i.e. it is a CAMERA routine, not a three-line opcode. That is the next derivation, and it
belongs with the overworld camera work rather than with this chase.

⚠ Worlds 1-4 have not been traced to their own first gap yet; only world 0 has.

### 9.8 ⭐⭐ Op `$3E` — and SIX of the eight worlds now build exactly

`C2/1947` is two bytes and looks like nothing: `LDA [$58],Y / STA $00 / JSR $09C5 / LDA #$0002`.
What `C2/09C5` DOES is the point. Micro-executed (`--callrom C209C5 --from saves/ow_w0.state
--regdp 0 --regdb 00 --m8 --x16 --writes --pokewram "7E0000=01"`) it makes **over 77 000 writes** --
it rebuilds a BG's whole tilemap, into the `$7E:F000` staging area and the scroll shadows (`$E3`/
`$E7` scaled by eight into `$20`/`$26`).

⭐ The port already does that work: `CtWorldTilemapBuild`, which `worldEnterAt` runs at every world
entry and which `ow-streaming` holds byte-exact against the oracle's live VRAM. So advancing the pc
past this op is not dropping behaviour -- it leaves it with the graded owner.

⚠ **This is deliberately NOT the same as op `$05`'s old stub**, and the difference is the whole
point: `$05`'s effect (the location change) was something *nothing else in the port did*. Driving a
per-BG rebuild from the VM -- `$09C5` selects one with `$00 & 3`, and 0 takes a different path at
`$0A6A` -- is a real difference from the port's unconditional both-BG rebuild, but it is a CAMERA
change, not an opcode one. `$00` is deliberately not written either: `C2/0AAB` overwrites it with
`$0F` immediately, so mirroring only the store would leave a value the cartridge never keeps.

| world | before `$3E` | after |
|---|---|---|
| 0 | 3 blocks, 2 matching | ⭐ **19 blocks, 18 of 18, 0 differ** |
| 2 | 2, 1 matching | ⭐ **13 of 13, 0 differ** |
| 3 | 3, 2 matching | ⭐ **10 of 10, 0 differ** |
| 5, 6, 7 | exact | exact |
| 1 / 4 | -- | 2 and 21 slots still differ |

⚠ **The pins moved from 16 frames to 90, and that matters.** The ROM's bootstrap lives thirty-six
frames after it is spawned (`$0C30` allocated at f84, killed by `C2:054A` at f120 on the
Truce-market door) and spawns progressively: world 0 reads 2 differing at 16 frames and 0 at 80. A
short window grades the clock, not the opcodes.

⚠ **`$3E`'s LENGTH is not graded.** A mutation to `adv = 3` SURVIVED -- the instruction stream
re-syncs -- so the pinned mutation is the one that PARKS the object instead, which is what an
unhandled op really does. The length stands on the ROM's own `LDA #$0002 / RTS`.

Worlds 1 and 4 remain, and `worldEnterAt` still restores the snapshot because of them.

### 9.9 ⚠ Worlds 1 and 4, characterised — NEITHER is a missing opcode

Both were traced with `CT_OWOBJTRACE`, and the answer is the same shape in both: the port executes
the right opcodes and reaches the right scripts. What differs is COUNT and ORDER.

**World 4 (21 slots).** The port executes every native spawn the oracle has — `35 04 34` (`$3404`),
`35 E2 3A` twice, `35 9E 30`, `35 64 32`, `35 94 2F`, `35 28 56`, at `$04AD`..`$04D4` — i.e. exactly
the class set the capture holds. ⭐ They simply land in the WRONG SLOTS: the capture has them at
`$0E30`..`$0FB0`, the port at `$1070` and beyond, because extra script objects were allocated first
and `C2/048A` is first-fit. The three `$09` spawns at `$04DD`/`$04E1`/`$04E5` (`$7F:089E`, `$08D0`,
`$08FD`) run before the natives in the port; the gate above them is `4D A6 1B B7 14`, and `$1BA6` is
`$03` in BOTH the port and the cartridge, so both fall through. So it is not that gate either —
it is WHERE THE FRAME BOUNDARIES FALL. `obRun` executes up to 64 ops per frame; if the ROM's
bootstrap yields at a point the port runs straight through, another object's spawns interleave
differently and the first-fit order changes.

**World 1 (2 slots).** One object too many at `$0FF0` (pc `$072F`) and one missing at `$1170`. No
`09 2F 07` appears in the trace, so `$072F` is a script that ADVANCED there — and the surrounding
spawns (`$07A0`, `$07C3`, repeatedly) are a CYCLIC SPAWNER. That is a count/timing difference in a
subsystem the port already models and grades (the gull/boat spawner), not a new opcode.

⚠ So the remaining gap for both is **yield timing**, not opcode coverage — a different kind of
derivation, and one that touches how `obRun`'s 64-op budget maps onto the ROM's own per-frame
yields. Six worlds build exactly; `worldEnterAt` still restores the snapshot for these two.

### 9.10 ⭐⭐ It was never "timing": `obRun` had an OP BUDGET the cartridge does not have

9.9 called worlds 1 and 4 a yield-timing problem. It was not timing in the sense of *when* the ROM
yields -- it was a number the port invented.

The ROM's object-VM dispatcher, `C2/0F73`:

```
LDA [$58] / AND #$00FF / ASL A / TAX
JSR ($0F91,X)          ; run the op; A = its length, or 0 to yield
BEQ ->0F8E             ; A == 0 -> RTS
CLC / ADC $58 / STA $58 / LDX $4E / STA $0007,X / STZ $0032,X
BRA ->0F73             ; ...and again. NOTHING COUNTS THE OPS.
```

⭐ It runs opcodes until a handler returns ZERO, unbounded. `obRun` stopped at **64**. A bootstrap
needing more was cut short and resumed next frame, so other objects' spawns interleaved differently
and `C2/048A`'s first-fit put everything one slot along.

**World 4: 21 differing slots -> 6.** Ops in the first frame, counted across all eight: world 4 runs
**221**, worlds 0 and 1 run 97, world 2 runs 65, worlds 3/5/6/7 stay under the old cap.

The counter stays only as a runaway guard -- a script that never yields would hang the cartridge too.

⚠ **The ANIM sub-VM's own loop (`C2/0E1D`) was deliberately left at 64.** It is a different
dispatcher and was never measured; a `sed` caught both and the second was reverted. Lifting a budget
on the strength of a neighbour's derivation is the guess the mandate forbids.

⚠ **And one suspicion of mine was simply wrong**: I expected the port to be missing the dispatcher's
`STZ $0032,X` after every op. It already does it (`obPutW(B + OWOBJ_OPSTATE, 0)` in the loop tail,
and explicitly in every `continue` arm).

⚠ The removal is **not graded by any check**. Its mutation survived on world 3 (54 ops, cap never
reached) and on world 0 (97 ops, but capping only DEFERS work and the set converges over the check's
ninety frames). The one world where it changes the outcome is world 4, which is not exact yet and so
has no check to host it. It stands on `C2/0F73`'s own unbounded loop.

State: six worlds exact, world 4 at 6 differing slots (3 objects too early, 3 missing later), world 1
at 2. Both remainders are now spawn COUNT, not structure.

### 9.11 ⭐⭐ ALL EIGHT worlds build the right SET — and my last two diagnoses were both wrong

9.9 called worlds 1 and 4 "yield timing". 9.10 corrected that to an op budget and called what was
left "spawn COUNT". **Both were wrong**, and counting the multisets is what showed it:

| world | classes built | in the capture | equal |
|---|---|---|---|
| 0..7 | every class, every count | the same | ⭐ **yes, all eight** |

World 4's three "extra" objects at `$0FF0`/`$1030`/`$1070` are the SAME THREE the capture holds at
`$1270`/`$12B0`/`$12F0`. Nothing is missing and nothing is invented: **only the slot order differs**,
in worlds 1 and 4 alone. Every spawn pointer in world 4's trace is used exactly once, so there are
no duplicate spawns either.

Placement is decided by `C2/048A`'s first-fit, so it follows the order the spawns interleave across
frames. That is the one thing still not reproduced, in two of the eight worlds.

Pinned as `ow-object-bootstrap-set-vs-oracle` (all eight, content) beside the six per-slot checks
(placement, where it is exact). The `$0C30` bootstrap slot is excluded on both sides in both.

⚠ Three successive diagnoses of this same gap were wrong before the right one — "a missing opcode",
"yield timing", "spawn count". Each was a plausible reading of a partial measurement, and each
survived only until the next measurement. The multiset comparison is trivial and should have come
first.

### 9.12 ⭐⭐ Why the attract's overworld comes up as FREE ROAM — and there is no "driver object"

Reported as "after the intro a scene is stuck and bugged", with an F5 dump. The dump is what
settled it.

**It is not black** — `$0119` was 0 at the instant F5 fired, but the screen lights up. What it shows
is world 5's sky with the player at the spawn, free roam, and the "12000 B.C." placard. ⭐ The
cartridge's attract shows a scenic flyover of the Zeal palace with **no player at all**. The dump's
own `+0DB0 cls=3404` is the PLAYER class, where the cartridge's attract has `$0F63`, a script object.

Same world, same arrival tile (`$0102/$0103 = 09 09` in both), three cells different:

| cell | ROM (attract) | the port |
|---|---|---|
| `$027C` | **`00`** — cutscene | `01` — free roam |
| `$1BA9` | **`80`** | `00` |
| `$00E3` / `$00E7` | `005E` / `002A` | **`0000` / `0000`** |

**`$1BA9` — `C2/26D6`, and the port already had the bytes.**

```
REP #20 / LDX #01F0 / LDY #1BA7 / LDA #$000F / MVN $00,$7F   ; $7F:01F0 -> $7E:1BA7, 16 bytes
SEP #20 / LDA $7F0000 / STA $1BA6
```

The overworld's flag block is loaded from the **STORY page**, not written by a script (measured:
`--monitor 7E1BA9:1 --diff --writer` from boot 2180 reports `f46 7E1BA9=80*(C2:26E5)`). ⭐ The port's
own F5 dump holds `$7F:01F2 = $80` -- the same byte the cartridge has -- and simply never copied it.
Implemented in `worldEnterAt`. ⚠ It overwrites part of the 16 bytes `worldInit` copies to `$1B9B`:
the ROM sets `$1BA6` from `$7F:0000` afterwards, so the blob's byte there is not the final word.

**`$00E3`/`$00E7` — the camera was never mirrored.** The ROM keeps both BGs' TILE scrolls there and
the object scripts read them: `C2/09C5` takes `$E3,X`/`$E7,X` and scales by eight, and op `$05`'s
return position is `($E5 + $10) % $C0` / `(($E9 + $10) & $7F)`. The port kept the camera only in
`wd.cam`, so every one of those reads got zero. Now mirrored each frame.

⭐⭐ **`$027C` — and THIS is the answer to "which object drives it": none.**

```
--monitor 7E027C:1 --from saves/truce_market.state --diff --writer
  f44   7E027C=00*(C2:2C20)    the world load ZEROES it
  f120  7E027C=01*(C2:12C8)    ...and C2/12C8 is op $13 of the OBJECT VM
```

`C2/12C8` is the instruction after op `$13`'s `STA $0000,X` -- the port's own notes identify it that
way for the colour-math shadow. So **the world's own script decides interactive vs cutscene**, with
a `13 7C 02 01`. The ROM leaves `$027C` at 0 -- cutscene is the DEFAULT -- and the interactive branch
raises it. The attract's flyover is a flyover simply because its branch never runs that op.

⚠ **The port's model is inverted**: it writes `$027C` every frame from a host flag
(`wr[0x027C] = g_owNoMarker ? 0 : 1`), over whatever the script decided. Undoing that is the
remaining link, and it is a model inversion with real blast radius (the era placard, the marker, the
new-game pan all read it), so it needs its own validation pass rather than a push at the end of a
five-measurement chain.

State: three of the four links derived and implemented, suite 465 pass. ⚠ The attract's overworld is
still NOT the flyover. `CT_OWBOOT=1` (opt-in) builds the object set from the bootstrap instead of
replaying the snapshot, and with the flags in place it does build a different set -- the placard
disappears -- but the camera still does not move.

### 9.13 ⭐⭐ The `$027C` model, inverted -- cutscene is the DEFAULT

The cartridge's order, measured end to end:

```
--monitor 7E027C:1 --from saves/truce_market.state --diff --writer
  f44   7E027C=00*(C2:2C20)    the world LOAD zeroes it
  f120  7E027C=01*(C2:12C8)    op $13 of the OBJECT VM raises it
```

`C2/12C8` is the instruction after op `$13`'s `STA $0000,X`. So **the world's own script decides**
interactive versus cutscene, with a `13 7C 02 01` -- world 0's is at `$7F:0468`, and every one of the
eight worlds' bootstraps runs exactly one of them. Cutscene is what a world comes up as;
interactivity is opted into.

The port had it the other way round: `wr[0x027C] = g_owNoMarker ? 0 : 1`, written EVERY FRAME from a
host flag, over whatever the script decided. Under `CT_OWBOOT` it now zeroes `$027C` at the world
load (`C2/2C1D`) and stops writing it per frame.

⚠ **Coupled to `CT_OWBOOT` on purpose.** Only the bootstrap executes that script; with the snapshot
replayed instead, nothing would ever raise `$027C` and the overworld would be stuck in cutscene --
no d-pad (the `C2/356A` `CMP #01` gate) and no menu. That would be trading one defect for a worse
one.

Measured: the attract's world entry now reads `$027C = 00`, the cutscene branch, and `--worldtest`
(house -> overworld -> Truce market -> overworld) PASSES in both modes with the player and camera at
identical positions. Suite 465 pass.

⚠ **The attract is still not the flyover**, and the reason is one layer up and the SAME SHAPE as the
bug just fixed: the port draws the lead on its own host path, outside the object list (`world.c`
says so in as many words), and creates a MOVER unconditionally -- where the cartridge's cutscene has
the mover at (0,0) and no player at all (`$0D84`/`$0D88` both zero in the attract, `(8,192)` in the
port's F5 dump). Another decision the port takes in the host that belongs to the script. It gets its
own measurement rather than a push at the end of this chain.

### 9.14 ⭐ No player object, no player -- and 9.15 the camera is the CELLS

**The lead.** On the cartridge the player IS an object: class `$3404`, spawned by the world's own
bootstrap on its INTERACTIVE branch. A cutscene entry has none -- the attract's world-5 leg holds
`$0F63` at `$0DB0` and its mover reads (0,0), against `$3404` and (8,192) in the port's F5 dump.
⚠ `CtWorldLeadBlock` FALLS BACK to `$0D70` when nothing is tagged, which is right for "where would
the mover seed read" and wrong for "is there a player" -- that fallback is what stood a Crono in the
middle of the demo's flyover. `CtWorldHasLead` answers the second question, and the draw is
suppressed when it says no. ⚠ Suppressing only the descriptor left a RED MARKER BOX: the branch
below draws one when no atlas sprite is picked, so the whole block has to go, via the `g_owNoMarker`
path that already existed for the new-game pan.

### 9.15 ⭐⭐ `$00E3`/`$00E7` ARE the camera, and a script writes them

The attract's flyover jumps the camera from tile (185,123) to (96,42) ten frames in:

```
--monitor 7E00E3:1 --diff --writer
  f0    7E00E3=B9
  f11   7E00E3=60*(C2:12C8)     the instruction after op $13's `STA $0000,X`
  f13   7E00E3=5F*(C2:05BD)     ...and then the pan
```

A plain **`13 E3 00 60`** -- an opcode the port already had. The driver script sets all four cells
(`$E3`/`$E5` = `$60`, `$E7`/`$E9` = `$2A`, each with its high byte zeroed by op `$10`) and then two
`3E`s to rebuild both tilemaps.

⚠ **Two mistakes of mine had to be undone to see it.** I had added a one-way mirror (camera ->
`$E3`) because the scripts read those cells -- but the truth is stronger: on the cartridge they are
not a mirror of anything, THEY ARE the camera, so writing without reading back made the mirror
swallow the script's write every frame. And when I added the read-back I put it on the wrong side of
the object VM, comparing my own write against itself; the opcode had been executing the whole time.
The read-back now sits immediately after the VM, adopts on CHANGE only, and takes only the tile part
(`wd.cam` carries sub-tile pixels the cells do not).

⭐ Result: the port's attract shows the Zeal palace on its mountain, the clouds and the floating
island -- the cartridge's scene, offset by however far the pan has run.

### 9.16 ⚠ The PAN is where byte-code stops: op `$4E` calls NATIVE code

`C2/05A6` is "scroll left by N": it decrements `$20,X` and, on a tile boundary (`$20,X & 7`),
decrements `$E3,X` wrapping at `$BF`. That is the camera the port already holds byte-exact
(`ow-camera`). What is missing is the CALLER, and following the driver's script to it ends the
byte-code trail:

```
$7F:0555  3E 01 / 3E 02        rebuild both tilemaps
$7F:0559  0D 22 02             block +$22 = 2
$7F:055C  0D 24 76 / 0D 25 91  block +$24/+$25
$7F:0562  4E 41 E2 C6          ⭐ op $4E
```

**Op `$4E` (`C2/181C`) is an indirect FAR CALL** -- `LDA [$58],Y / STA $5B`, the bank into `$5D`,
then `JSL $C2:1836` which is `JML [$005B]`; length 4. So the script calls **`$C6:E241`**, native ROM
code -- and that routine installs an HDMA channel over `$2127` (window 1's right edge) with its
table at `$7E:8A42`, taking a parameter from the object's `+$22` (the `0D 22 02` three ops earlier).

⚠ So the flyover is not driven by byte-code alone; from here it is bank-`$C6` effect code. The
opcode itself is four bytes and trivial, but implementing it without its destination would send the
script jumping into nothing. Named, not stubbed.

## 9.17 The attract was stopped on TWO OPCODES, not on a scene

The user's report was "the scene after the intro is stuck and bugged". It was neither a scene nor a
render: the overworld object VM was **parked on an opcode it had no case for**, and a parked object
re-reads the same byte every frame forever. Two of them, four bytes apart.

### The shape of the attract (measured, not assumed)

`build/ct_field.exe --scene 433` and the ROM's own script for that location:

* **Location `$1B1` = 433 is not a place, it is the attract's RETURN POINT.** Its script body is a
  chain of compares on the story byte `$7F:0062`, and each arm dispatches the next leg and advances
  the byte. The chain runs in obj0's autorun continuation at body offset `0x000F` (obj0's fn0 ends
  at `0x000E`; the fn table never points at `0x000F`).
* The first two guards are `16 60 00 00 B0` (skip the whole chain if `$7F:0060`, the START latch, is
  set) and `16 62 00 00 09` → `56 01 62 00` (`$62 := 1`) → `EA 00` → **`FF 02`**, the title sequence.
* `$0384` is **not** an internal counter: `C3/0038` is the only writer in the ROM and it is
  `PLA / STA $0384` at the sequence machine's entry — the byte the caller pushed. `FF 02` pushes 2.
* The chain's later arms dispatch `FF 61` (twice), `FF 60`, and location changes. `FF 61` is the
  site entry 1 (the staff credits) was ported for and could never be reached.

### The freeze: op `$4E` (C2/181C)

The world-5 flyover's driver reaches `4E 41 E2 C6` at `pc=0562`. `world.c` had no `case 0x4E`, so it
fell to `default: adv = -1` — park. Measured: `CT_OWOBJTRACE=1` shows `pc=0562 op=4E 41 E2 C6`
re-read on every frame of a 3400-frame run while the other objects keep ticking.

The handler table is the word at `C2:0F91 + op*2` (self-checked: `op 4E` → `C2/181C`, the address the
earlier section had already named). `C2/181C` in full:

```
LDY #0001 / LDA [$58],Y / STA $5B     ; the target's low WORD (M=16 here)
SEP #20 / LDY #0003 / LDA [$58],Y / STA $5D   ; the BANK
JSL $C21836        ($C2/1836: JML [$005B])    ; the FAR call; the target RTLs
REP #20 / LDA #0004 / RTS                     ; the advance -- a literal, unconditional 4
```

⭐ **The advance is not a function of the callee.** The dispatcher returns 4 whatever the target did,
and no target reports anything back (the call is `JML`, the return `RTL`; there is no result the
dispatcher reads). So the script's control flow is independent of whether a given target is ported —
which is what makes advancing past an unported target correct rather than a guess.

Targets the attract uses, both in bank `$C6`:

* **`$C6:E241`** — installs an *indirect* HDMA over `$2127` (WH1, window 1's right edge) on channel 2:
  `LDA #F0 / STA $7E8A42 / STA $7E8A45 / TDC / STA $7E8A48` (the table's two line counts and its
  terminator), `LDY #2740 / STY $4320` (DMAP2 `$40` = indirect/1-register, BBAD2 `$27`),
  `LDY #8A42 / STY $4322`, `LDA #7E / STA $4324 / STA $4327`, `LDA #04 / TSB $4A` (the HDMAEN
  shadow gains channel 2), then `LDX $4E` and the object's `+$22`/`+$24` into `$0350`-`$0354`,
  `JSL $C3000E` (= `JMP $C3:01E4`), and finally `$7E:8A25` = `+$24` plus `$0000` or `$00E0`
  depending on bit 0 of `$0351`, with `$7E:8A27` = that + `$0070`.
* **`$C6:E27E`** — the same routine's tail (the `JSL $C3000E` onwards), called once per pan frame to
  recompute the window edges from the object's current position.

✅ 2026-09-16: BOTH TARGETS RUN, translated from the ROM (src/xlat/owwin_c6.c) through the object VM's far call, and
the window is drawn -- field_window_wipe.md section 6. The paragraph below is the state before that.

⚠ **Neither target's body was ported then, deliberately and not half-ported.** The tail's choice between
`+0` and `+$E0` is `C3/01E4`'s result in `$0351`, and `C3/01E4` (a bank-`$C3` helper running with
`D=$0300`) is not derived. Writing only the unambiguous prologue would put half a routine's bytes
into cells nothing in the port reads. `CtWorldObjOp4E` names each target once on stderr instead.
Consequence: the flyover runs its whole script, without window 1's per-scanline right edge.

### The leg's ending: op `$15` (C2/12E7)

Implementing `$4E` moved the park four bytes on, to `pc=0580 op=15 A9 1B 80` — 667 re-reads in a
3000-frame run. `C2:0F91[0x15*2]` → `C2/12E7`:

```
LDY #0001 / LDA [$58],Y / TAX          ; X = the 16-bit address, operand bytes 1-2
SEP #20 / LDY #0003 / LDA [$58],Y      ; the mask, operand byte 3
EOR #FF / AND $0000,X / STA $0000,X    ; mem[addr] &= ~mask
REP #20 / LDA #0004 / RTS
```

So op `$15` is a four-byte **bit CLEAR**, and the flyover's instance is `$1BA9 &= ~$80` — it clears
the very story-page bit `C2/26D6`'s MVN brings in to select the attract's cutscene branch (section
9.12). **The opcode immediately after it, at `pc=0584`, is `05`** — `C2/1100`, ENTER A LOCATION. The
port was parked four bytes short of the leg's own ending.

### Measured before / after

`--scene 433 --frames 14000 CT_OWBOOT=1`, the attract driven end to end:

| | before | after |
|---|---|---|
| world-5 flyover | never ends (9900+ frames, no further output) | ends via `ow-op05: C2/1100 -> loc 16E tile(16,23) ret=1F5/(112,58)` |
| scene 366 | never reached | reached, renders 57344 non-black px |
| attract | one leg, then frozen | cycles title → flyover → 366 → title |

⭐ **The cartridge's own chain, measured.** `build/ct.exe --monitor "7F0062:1,7E0384:1,7E0100:2"
--from` the user's own `cena_inicial` capture (snes9x, spliced with `tools/s9x_to_lake.py`), 9000
frames, `--diff`:

```
f0     $62=01  $384=02  loc=01B1     scene 433, the title has just run
f35    $62=2C                        the chain advances
f67    $62=2C           loc=01F5     WORLD 5 -- the flyover
f337   $62=2C           loc=016E     scene 366
f612   $62=2C  $384=61  loc=001C     FF 61 = entry 1, the CREDITS, over field scenes
f1098/f1415/f2320/f2753/f2925        loc 001B / 00AD / 0112 / 00DB / 01F7
f3317                   loc=01B1     back to 433
f3324  $62=38 ... f5308 $62=39 ... f6150 $62=40 ($384=60) ... f6590 $62=48
f6727  $62=4A ... f7075 $62=4C ... f7983 $62=50 ... f8292 $62=58
```

The progression `2C -> 38 -> 39 -> 40 -> 48 -> 4A -> 4C -> 50 -> 58` is exactly the arm order read
out of the 433 script above, and `$384` takes the values `02`, `61`, `60`, `63` -- so `FF 61`
(entry 1, the credits) really is on the attract's path, twice, as the chain said.

**The port matches the cartridge's first three legs exactly**: 433 -> `01F5` (world 5) -> `016E`
(scene 366). That is the stretch that used to be frozen.

⚠ **Where the port still diverges:** the cartridge spends f337-f612 inside scene 366 and leaves it
into the credits leg (`$384=61`, loc `001C`); the port returns from 366 to 433 and replays the title
instead. Scene 366's own dispatch is the next thing to derive -- not the flyover, which now agrees.

⚠ **`default:` no longer parks silently.** An opcode with no case now names itself once on stderr
with its pc and its handler's table slot. That silence is precisely what hid `$15` and `$4E`: the
symptom presented as "a scene is stuck", not as "the VM stopped on an opcode".

## 9.18 The field VM's handler table, op `$16` derived, and the third park

`sequence_table.md` 9.17 left the port cycling title -> flyover -> scene 366 -> title where the
cartridge goes on into the credits. Scene 366 turned out to have TWO arms on what looks like the
same guard, so the first job was to stop guessing at op `$16`.

### The field VM's handler table is `C0/5D6E`

Found by anchoring on three handlers this repo had already cited by another route -- ops `$CC`/`$CD`/
`$CE` at `C0/3827`/`C0/385B`/`C0/3874`, which are CONSECUTIVE opcodes, so their table entries are the
byte string `27 38 5B 38 74 38`. One hit in the ROM, at a file offset that puts the table base at
`C0/5D6E`. Validated on five independently-cited handlers before being used for anything:

| op | table says | cited elsewhere in the port |
|---|---|---|
| `$AD` | `C0/2F96` | `C0/2F96` |
| `$CC` | `C0/3827` | `C0/3827` |
| `$CD` | `C0/385B` | `C0/385B` |
| `$CE` | `C0/3874` | `C0/3874` |
| `$D2` | `C0/38E8` | `C0/38E8` |

### op `$16` = `C0/63E6` -- and the port was already right

Five bytes, `16 [idx] [val] [mode] [skip]`:

```
63E6: INY / TYX
63E8: LDA $7F2001,X / STA $D9      ; operand 1 -- the flag INDEX (low byte)
63EF: LDA $7F2001,X / STA $DB      ; operand 2 -- the VALUE / mask
63F6: LDA $7F2001,X / BPL ->6418   ; operand 3 -- the MODE, bit 7 picks the arm
  bit7 SET : AND #7F -> $DD ; LDA $D9 / AND #00FF / ORA #0100 / TAX ; LDA $7F0000,X
  bit7 CLR :          -> $DD ; LDA $D9 / AND #00FF /            TAX ; LDA $7F0000,X
642D: LDA $DD / ASL A / TAX / JSR ($6467,X)
6436: BCC ->643B                   ; CLC = the condition HOLDS -> fall into the guarded block
6438: BRL ->6349                   ; SEC = it failed -> take the jump
```

⭐ Bit 7 of operand 3 is not part of the mode, it is `ORA #0100` on the INDEX: the flag is read from
`$7F:0100+idx` instead of `$7F:0000+idx`. The eight comparators at `C0/6467`, each returning `CLC`
(via `C0/64B3`) for "holds" and `SEC` for "jump":

| mode | routine | code | means |
|---|---|---|---|
| 0 | `C0/6487` | `CMP / BEQ` | `==` |
| 1 | `C0/648F` | `CMP / BNE` | `!=` |
| 2 | `C0/6497` | `CMP / BEQ->SEC / BCS` | `>` |
| 3 | `C0/64A1` | `CMP / BCC` | `<` |
| 4 | `C0/64A9` | `CMP / BEQ / BCS` | `>=` |
| 5 | `C0/64B5` | `CMP / BEQ / BCC` | `<=` |
| 6 | `C0/64BF` | `AND / BNE` | `(flag & val) != 0` |
| 7 | `C0/64C7` | `ORA / BNE` | `(flag \| val) != 0` |

`C0/6349`, the jump arm, reads the skip byte at `$C3` and adds it to `X` -- so the target is counted
**from the skip byte**, i.e. `addr + 4 + skip`, not `addr + 5 + skip`.

⚠ **The port's implementation was already byte-exact** -- `cmp_`'s eight modes, the `+$100`, and
`target = addr + B[addr+4] + 4`. It simply carried no citation, in either of its two sites (the init
prescan and the live step). Both now do. ⭐ **So the two arms of scene 366 genuinely are the same
test (`$7F:0057 & $04`) and both genuinely fire.** The divergence was elsewhere.

### op `$2D` = `C0/66A5` -- the third park, and this one ran instead of stopping

```
66A5: INY
66A6: LDA $00F8 / BNE ->6701      ; 6701 = INY / TYX / SEC / RTS -- FALL THROUGH into the block
66AB: LDA $00F9 / BEQ ->66D0      ; 66D0 = TYX / LDA $7F2001,X / TXA / CLC / ADC -- TAKE THE JUMP
66B0: BRA ->6701
```

"Skip unless something is held": `$00F8` is the button latch, `$00F9` the direction latch. Two bytes,
and the jump base is `addr+1` because `C0/66D0` is a bare `BB TYX` with no `E8 INX` (ops `$27`/`$28`
do `TYX / INX` and are three bytes).

⚠ **This was already derived, with these exact citations, in `move.c`'s `--sweepops` micro-executor
-- and the LIVE VM in `ct_vm.c` had no case for it at all.** It fell to the generic advance, which
runs the guarded block unconditionally. In scene 366 that block is obj3's
`2D 0A / 56 00 62 00 / E0 B1 01 00 00`: **the demo's ABORT-ON-INPUT handler.** Firing it with no
input made the attract abort itself the instant it reached scene 366, beating obj2's credits arm
(which is waiting out an `AD 6E` = 110 visits) and sending everything back to the title.

Confirmed on the cartridge -- `ct.exe --monitor "7E00F8:2,7E0100:2" --from` the user's `cena_inicial`
capture:

```
f0     7E00F8=0000  loc=01B1      f337   7E00F8=0000  loc=016E
f67    7E00F8=0000  loc=01F5      f612   7E00F8=0000  loc=001C
                                  f1098  7E00F8=0000  loc=001B
```

Both latches are zero for the whole attract while the location walks the chain, so the ROM takes the
jump arm every time.

### After

`--scene 433 --frames 2800`, no environment variables:

```
overworld entry: scene 433 -> world 5      ow-op05: C2/1100 -> loc 16E
nopc: visit 3 scene 366                    sequence: op-0xFF 61 -> ($0384=97 = $61) entry 1
sequence: entry 1 done after 105 frames    reboot: JML C0:0000 -> loc 01C
```

`$0384 = 97` is decimal -- `$61`, the cartridge's own value at f612 -- and loc `001C` is where the
cartridge goes. Entry 1 (the staff credits) had been ported and graded to 0 px five sessions ago
against a site the port could not reach; this is the first time it runs on its own path.

⚠ **Open:** the cartridge visits `001B` and `00AD` before `0112`; the port goes straight to `0112`.
And its entry-1 leg lasts 105 frames against the cartridge's ~486 (f612 -> f1098).

## 9.19 The `001B`/`00AD` "skip" did not exist — and the instrument that proved it

9.18 closed with an open item: *"the cartridge visits `001B` and `00AD` before `0112`; the port goes
straight to `0112`."* **That claim was wrong, and it was wrong for a specific, repeatable reason.**

The port's only visible evidence for a chain of scenes was the host's `reboot:` line, and **`reboot:`
fires when a SEQUENCE ends** (`$7E:0000` set, `JML $C0:0000`) — never on a field-to-field hop. The
attract's `01C -> 01B -> 0AD` legs are ordinary `E0`/`DF` location changes with no sequence between
them, so they leave no `reboot:` line at all. Reading the chain off that log drops every leg that
does not happen to end a sequence, which turned a correct `01C -> 01B -> 0AD -> 112` into an apparent
`01C -> 112`.

### What the port actually executes

`CT_VMTRACE_ALL=-1` filtered to the change-location family (`$DD`-`$E1`), then each hit decoded
against its own script body:

| script | op | target |
|---|---|---|
| scene 433 `@010A` (obj1) | `E0 F5 03 09 09` | loc `1F5` — world 5 |
| scene 366 `@007A` (obj2) | `DE 1C 02 09 09` | loc `01C` |
| scene 28  `@0185` (obj3) | `E0 1B 00 08 08` | loc `01B` |
| scene 27  `@0369` (obj16)| `DF AD 00 07 18` | loc `0AD` |
| scene 173 `@01AF` (obj9) | `DE 12 03 08 0D` | loc `112` |

That an op belonging to scene 27's body RAN is itself the proof scene 27 was loaded; likewise 173.
Scene 28's `E0` shows 17 visits, which is op `$E0`'s own park-on-op fade (commit at fire+16), not a
stall.

### `CT_LOCTRACE`

Reconstructing a chain from opcode archaeology three times in one session is a sign the instrument
is missing, so it now exists: one stdout line per COMMITTED location change, drained where
`CtScenePollLocChange` hands the host its one-shot. It is the like-for-like counterpart of the
oracle's `--monitor 7E0100:2 --diff`.

⚠ It prints to **stdout** deliberately. The first version used `fprintf(stderr, ...)` and, under a
pipe, every trace line landed ahead of the printf-based `reboot:`/`sequence:`/`ow-op05:` lines
regardless of when it happened — reproducing, in the tool built to prevent it, exactly the
out-of-order reading that caused the original mistake.

With it, `--scene 433 --frames 40000`, no environment variables, in true order:

```
 1  sequence: op-0xFF 02   (the title)
 2  reboot:   -> loc 1B1        scene 433
 3  loctrace: -> loc 1F5        world 5
 4  overworld entry -> world 5, f2100
 5  ow-op05:  -> loc 16E        scene 366
 6  sequence: op-0xFF 61        entry 1, the credits
 7  reboot:   -> loc 01C        scene 28
 8  loctrace: -> loc 01B        scene 27
 9  loctrace: -> loc 0AD        scene 173
10  sequence: op-0xFF 61
11  reboot:   -> loc 112        scene 274
```

Against the cartridge's `01B1 -> 01F5 -> 016E -> 001C -> 001B -> 00AD -> 0112`: **all seven legs
agree.** Nothing is skipped.

### What IS still open

The chain **stops at `0112`**. The cartridge leaves scene 274 for `00DB` at f2753 (433 frames after
arriving), then `01F7`, then back to `01B1` to loop. The port sits in 274 for 40000 frames and never
commits.

The dispatch is scene 274's obj0 `fn0`, whose tail is
`... 28 04 06 28 / D4 03 / D3 01 / E0 DB 02 07 1C / B2` at `@0055`-`@0066`; `CT_VMTRACE_ALL=0` shows
obj0 walking `@002D -> @0032 -> @0035 -> @003A/3D/40/43 -> @0046 -> @0049 (op AD, a wait) -> @004B`
and not yet reaching `@0061`. Not diagnosed further here.

Also open, unchanged from 9.18: the port's entry-1 leg runs 105 frames against the cartridge's ~486
(f612 -> f1098), so every leg's DURATION is still its own.

## 9.20 The attract's last leg: a mover DEADLOCK, and the flag that caused it

9.19 left the chain stopping at `0112`. Scene 274's obj0 runs `04 08 17` at `@004B` and never advances.

### The wait is real, and so is what it waits for

`op $04` = `C0/6109` (the validated table `C0:5D6E + op*2`): it opens `LDX $6D / LDA $7F0980,X / BEQ`
— **call AND WAIT**, with the wait latch at `$7F:0980,X`. Operands `08`/`17` = object 4, priority 1,
function 7. obj4 runs at p1 (`CT_VMTRACE_ALL` confirms `[run obj4] @0124 op=96 p1`) and its function
parks on `96 08 16` — a MOVETO to tile `(8,22)` from the `(8,9)` its `8B 08 09` placed it at.

**It never moved.** Position identical at f5000 and f20000 — `obj4(136,159)`, 15000 frames apart.

Three suspects were eliminated by measurement, not by argument:

* **collision** — `$7E7040`, the mover-solid plane, reads `00` down the whole column x=8, y=6..26;
* **`hasPos`** — false would make `moveOp` return 0 (complete) and the object would move on;
* **a stale position mirror** — `atx = (int)sc->fx[obj] >> 4` reads the LIVE float, not `$1800/$1880`.

### `CT_MVTRACE`

The mover's real state — `fx/fy`, `mx/my`, the `cycles` ticket, `hasTask`, `mspd` — lives in
`CtScene`, not in WRAM, so a `CT_DUMPWRAM` image shows only seed-time mirrors (`$1A00`/`$1A01`/
`$7F0B00`) that say nothing about why a step fails to persist. `CT_MVTRACE=<obj>` prints one line per
`moveOp` visit and per integration pass for one object. It answered the question on its first run:

```
mv[4] enter op=96 @0124 cyc=1 task=0 mspd=50 mode=01 b00=1 pos=(136.00,159.94) v=(0.000,2.438) ...
mv[4] skip  op=FF @0000 cyc=1 task=0 ...
```

⭐ **`cycles = 1` with `hasTask = 0` is a state nothing can leave.** `moveOp` opens with
`if (sc->cycles[obj] > 0) return 1;` so it yields before it can re-arm; the integration loop gates on
`hasTask[n] && cycles[n] > 0` so it skips before it can decrement. The ticket never expires, the
velocity never applies, and the actor holds a live 2.438 px/frame forever.

### The producer: the conga's handover

obj4 is a party FOLLOWER (`mode = 1`). The follower ring is gated on the follower's own script op
`$B0` — the ROM's `C0/3002` -> `C0/A26B`/`C0/A2CE`, and the port models that gate correctly
(`CT_FOL_ON_B0`). obj4's script is `0E 01 / B0 / AA 06 / 0E 01 / 89 50 / 96 08 16 / ...`: it sits on
the `B0`, then leaves it for the MOVETO. On that one transition frame the port did

```c
if (sc->folRoamPrev[o] && !roaming) {   // script/cutscene takes over: release the walk cycle
  sc->hasTask[o] = 0;                   // <-- and the move that had just armed is orphaned
```

**`hasTask` and `cycles` are ONE unit** — the ROM's `$1A80,X` (velocity/task) and `$1A01,X` (step
ticket). Every other site in `ct_vm.c` clears them together and cites exactly that pair
(`3526 STZ $1A80,X / 3529 STZ $1A01,X`, and `5B59`/`5B5C`). The conga's handover was the one place
that split them. Clearing both is the fix; the handover fires once, on the transition, so the move
simply re-arms on the following visit.

### After

`--scene 433 --frames 4800`, no environment variables, `CT_LOCTRACE` on:

```
1B1 -> 1F5 -> 16E -> 01C -> 01B -> 0AD -> 112 -> 0DB -> 1F7 (world 7) -> 1B1
    -> 0A5 -> 07D -> 1B1 -> 0E6 -> 1F2 (world 2) -> 1B1
```

The first ten are exactly the cartridge's measured chain (9.17's `--monitor` run: `01B1 01F5 016E
001C 001B 00AD 0112 00DB 01F7 01B1`), and the rest are the `$62` = `38`/`39`/`40`/... arms following
on. The attract now runs end to end and loops.

VISUALLY: scene 274 used to render with **no characters at all** — the party frozen at tiles
`(8,9)`/`(8,10)` while the camera sat at y=800 — against the cartridge's party walking in a line down
the path. The port's frame at f4300 now matches the cartridge's f4700: same terrain, same formation,
same NPC to the right.

⚠ **Still open, different cause:** the throne-room leg draws its stone chamber where the cartridge
draws a purple starfield (port f3500 vs cartridge f4400). Not diagnosed.

## 9.21 The throne room's starfield: located, not yet fixed

The attract's `00AD` leg (scene 173, Zeal's throne room) draws a stone chamber where the cartridge
draws a purple starfield. **NOT FIXED.** What follows is what is now measured, so the next attempt
starts from facts rather than from the render.

### The starfield is already in the port

`CT_SCENEISO` (added here — the field counterpart of the oracle's `PEEK_ISOBG`; `1` = L1, `2` = L2,
`4` = L3) settles the first question in one run:

| | port |
|---|---|
| **L1 alone** | the stone chamber **plus** the carpets and the throne |
| **L2 alone** | **the starfield** — correct, complete, right palette |
| **L1 + L2** | the chamber; L2 contributes nothing |

So nothing is missing from the data and nothing is wrong with L2. L1 simply covers it, which is the
correct SNES order (BG1 priority-0 over BG2 priority-0; `order_bit[] = { BL3, BL2, BL1, ... }`).

### The cartridge's BG1 does not contain the chamber

`--teleport 173 7 24 0 --from` the user's capture, then `PEEK_ISOBG` on the resulting state:

| | cartridge |
|---|---|
| **BG1** | ONLY the carpets and the throne |
| **BG2** | the starfield |
| **BG3** | empty (and `TM` confirms BG3 is not even enabled; `TS=00000`) |

And its live VRAM says the same thing in bytes. BG1's tilemap (`tilemapAdr=6000`) reads

```
BG1 row 6: 21FF 01FF 21FF 01FF ... 21FF 01FF 1624 5624
BG1 row 9: 21FF 01FF 21FF 01FF ... 91C1 91C2 162E 562E
```

— tile `$1FF`, the blank filler, everywhere except the carpet columns.

### It is not the camera, and not the +256 bank

* **Camera**: the cartridge's `$1D0A`/`$1D0E` at this location read `0`/`32` = **(0,256) px**, which is
  exactly the port's `cam(0,256)`. Both are looking at the same rows of the same map (and the scroll
  mask `h[10..13]` = `00 10 0F 1D` allows only rows 16..29 anyway).
* **The metatile upper bank**: forcing every L1 entry's bit 8 off, and forcing it on, both make the
  render WORSE (curtain strips / a smeared starfield) — the normal decode is the coherent one.
* **The tilesets**: `ts_l12 = $23` loads normally, and the `FF` slots (this location's header is
  `FF 23 FF 30 5D 00 ...`) are already skipped by `cs == 0xFF || cs >= 204`.
* **Layer designations**: `CT_SCENEDBG` gives `screen=0B effect=00 -> main_t=3 sub_t=0 blend_en=0`,
  and `has_l3=0`. Two layers, no blending, no L3 — matching the cartridge's `TM`.

⇒ **The port's L1 map data genuinely carries the chamber at the rows both cameras show, and the ROM
puts `$1FF` blanks there instead.** The difference is made at RUNTIME, not in the data.

### The most likely producer, and where it stops

Scene 173's script has four tile-copies inside its DEMO branch (`16 57 04 06 25`, the same
`$7F0057 & $04` gate the whole attract runs on), each eight bytes (`OPLEN[0xE4] = 8`):

```
E4 00 00 08 18 00 10 3B      E4 08 00 0F 18 08 10 3B
E4 00 08 08 1F 00 18 3B      E4 08 08 0F 1F 08 18 3B
```

Four quadrants, source rows `00..18`/`08..1F`, destination row `$10` = **16** — the first visible
row. The map is `L1 = 16x48`, three 16-row bands: the room as authored lives in the low band and the
demo copies a variant over the visible one. op `$E4` = `C0/3D97` and the port DOES implement it
(`ct_vm.c`, queued into `tcQ`).

⚠ **The port never executes them.** `CT_VMTRACE_ALL=0` across the attract's whole `00AD` leg shows no
`op=E4`, `op=E5`, `op=7C` or `op=7D` at all; obj0 sits in its idle loop, `@002C op=22` ->
`@0030 op=11` -> back, forever (op `$22` = `C0/65EB`, `LDA $7F2001,X / TAX / LDA $97,X` — a read of
the party-slot table `$0197+n`, four bytes; `11 05` jumps back onto it unconditionally).

So the block holding the copies is a function of obj0 that **something else is supposed to call**, and
whatever that is has not been found yet. That call is the next thing to derive — not the renderer,
which is doing exactly what its data says.

## 9.22 Per-layer scroll: the map header's md[1..3], never packed

User report, attract item 2: *"background is not animating"* (loc 16E, the Blackbird over clouds).

### Measured on the oracle (the user's `cena_inicial` capture)

`--ppuregs` on PEEK_SAVE states 16 frames apart, loc 16E:

| | f450 | f466 | rate |
|---|---|---|---|
| BG1 vS | 932 | 916 | −1 px/f |
| BG2 vS | 839 | 807 | −2 px/f |
| BG3 vS | 560 | 480 | −5 px/f |

Loc 0AD: BG2 `(182,91)` → `(362,181)` in 90 frames = `(+2,+1)` px/f while BG1 sits at `(0,0)`.
The port's field compositor bakes ONE canvas and crops it at ONE camera, so all three were locked.

### The ROM

* **Per frame, C0/9238-92C8** accumulates a signed 4.4 fraction per layer axis (`$1D3B..$1D40`):
  BG1 from the camera terms `$2E/$30 + $24/$25 + $4E/$4F`; BG2/BG3 from their own speed `$37..$3A` plus
  the op-E6 drift `$26..$29`, **and** the camera terms only if the layer's bit in `$1D36` is clear
  (bit 1 = BG2, bit 2 = BG3 detached). `9312..` extracts whole pixels (`>>4` of the magnitude, signed
  nibble kept) and `C0/9579..` steps each layer's counter per pixel: `$1D87/$1D89` BG1, `$1D8B/$1D8D`
  BG2, `$1D8F/$1D91` BG3 — writers measured with `--monitor --writer`: `C0/94EB`, `C0/95BB`, `C0/9773`.
* **At the load, C0/A385-A4FB** off the decompressed header at `$7E:B500`: `md[1]` bit `$20` →
  `$1D36 = 4`, bit `$10` → `|= 2`; `md[2]` → `$1D37/$1D38`, `md[3]` → `$1D39/$1D3A`, each a 3-bit
  magnitude code (speed = `1 << (code−1)`, `A46B DEY` first) with bit 3 / bit 7 as X / Y sign.
* **Verified on live bytes**: 0AD `md[2]=$56` → `$1D37=20 $1D38=10`; 16E `md[2]=$D0 md[3]=$F0` →
  `$1D38=F0 $1D3A=C0`, `$1D36=02`. Leaving 16E for 01C zeroes `$1D35..$1D3A` together; the load of 0AD
  zeroes all six counters together.
* The three PPU scrolls share one base once the counters are subtracted (16E f450:
  `932+92 = 839+185 = 560+464 = 1024`, the PPU one frame behind the counters), so a layer's offset
  from the camera **is** its counter minus BG1's.

### The port

* `assets/extract_assets.py` appends `md[1..3]` after the tprio plane (an older reader sizes tprio by
  length). A fresh extract of the unchanged tree was first checked byte-identical to the shipped
  `ct_assets.dat` (sha `b6b358e3…`) before anything was changed.
* `CtSceneLayerScrollLoad` ports the decode; `camCommit93E1` gains the BG2/BG3 accumulate + counters;
  `CtSceneCameraEntry` zeroes that half of the block; `draw_layer` samples BG2/BG3 at their offset.
* Result: 16E `off2=(0,−1,−2,…)`, `off3=(0,−4,−8,…)` — the oracle's relative rates; 0AD `(+2,+1)`.
  `field-layer-scroll-parallax-vs-oracle` and `-drift-vs-oracle`, 5 mutations, 5 killed.

⚠ Not graded: the detached bit's effect, because neither fixture moves the camera.

### Not this mechanism: loc 01C (attract item 3)

Loc 01C's `md` is `B0 00 00` — no layer speed — and its BG scroll latches **and** its HDMA scroll tables
(`$7F:128F/12E6/133D`, channels 1–3 → `$210D/$210F/$2111`, indirect) read zero for the whole leg. Yet the
cloud band changes 458/599/464 px at +2/+4/+8 frames and returns to a 217-px residual at +16 and +32:
a **16-frame cycle**, i.e. tile or palette animation, not scroll. The port's band shows a constant ~360 px
at every gap (sprites only) — it does not cycle. Separate derivation.

## 9.23 Loc 01C: the camera entry tile, and a detached layer is not on the camera

Attract item 3. Side by side with the cartridge (`saves/attract_cena_inicial.state`, the user's own
capture) the tower matched but the bridge, machine, cloud band and mountains sat ~16–33 px high and
~16 px left. Two mechanisms, both derived:

1. **The entry law reads the arrival tile.** `C0/7553 LDA $0103 / CMP $52` (and `C0/7506` on `$0102`).
   The port passed `py / 16`, and the foot anchor `py = (tile*256 + $FF + 1)/16 = (tile+1)*16` whenever
   arrival bit 3 is clear, so the camera landed a tile low. Measured at the reboot into 01C:
   `$0102/$0103 = 09/09`, oracle `$1D0E = 2 = (9−8)*2`, port 4. Each of the seven staging sites now
   carries the tile it staged (`pendTileX/Y`). Re-pinned one liveness count
   (`field-pc-slot-arm-stores`, gated/reached over the camera window), nothing it grades against the
   oracle moved.
2. **A detached layer draws at screen + its own counter, base 0.** md[1] = `$B0` detaches BG2 and BG3
   (`$1D36 = 06`); the oracle's camera is (16,16) while its BG2 draws with scroll (0,0) — the latch and
   the HDMA table (ch2 → `$7F:12E6` → `$1514`) both read 0. Consistent with 0AD (BG2 scroll = its counter)
   and 16E (BG2 839 + 185 = 1024 ≡ 0 mod 256). The field crops one canvas at the camera, so a detached
   layer's sample offset is `counter − camera`.

`tools/attract_bg_cmp.py` runs both sides live and compares row bands no sprite enters: 01C rows 0–70
and 160–224 are **100% identical** (98.9% of the whole frame; the rest are sprites a few frames apart).
16E's cloud band matches **100.0%** at the best-aligned frame under a uniform one-scanline shift (48.8%
unshifted).

⚠ MEASURED AGAIN 2026-09-17, and that one scanline is NOT a line-offset law (the overworld's was, and is
fixed — disasm/notes/ow_bg_vline.md; this is a FIELD scene and the field composite already adds its line).
The Blackbird itself matches; the CLOUDS are what differ, and they move **1 px per frame on BOTH sides**
(cartridge `--peek` f584/f588/f592: dy = -4 per 4 frames; port f2652..f2656 against cartridge f588: the best
dy walks -2, -3, -4, -5, -6, one per frame). So a whole-frame shift and a frame offset are the SAME thing
here, and what the shift really measured was that the port's 16E leg runs about **3 frames** out of phase
with the cartridge's — the same family as 1F3's one frame (ow_bg_vline.md section 5).
The phase is not the whole story: even at its best-aligned frame (port f2650 against cartridge f588) the
frame is only **73.6%** identical unshifted, and the cloud band alone tops out at ~82% under any single
(dx, dy). Something per-layer remains — a second cloud layer at its own rate, or the cloud tilemap itself.
Next pass: isolate the layers (`PEEK_ISOBG` on the oracle) instead of shifting the composed frame.

### Still open on 01C: Crono's sword is black

Cartridge OAM #32–35 (Crono) use OBJ palette 5, whose entries 13–15 are `EEE641 DE8B18 A46A10` (gold);
palettes 0–4 carry magenta "unused" there. Those three are the **weapon row**, written into the CGRAM
shadow `$7E:21BA..21BF` by the `$C2` gate's epilogue (`C2/834D`, `C2/8358 LDY #21BA`, `C2/F5ED` maps the
PC to its weapon block in bank `$D1`). On the cartridge the write is `C2:836C` at f35 of the capture,
still in loc 433, and it survives three loads and a sequence reboot. In the port Crono is on slot 5
(`$0F81 = 0A`) with colours 1–12 in place, but `$21BA..BF` read zero at every sampled frame
(f2090 in 433, f2105, f2350 in 1F5, f2620 in 01C) — so the epilogue's write never reaches the page the
renderer reads. `ctVmC2Epilogue` itself is ported (`move.c`).

## 9.24 Loc 01B: the lead starts where its own fn0 last put it, and op 0xF0's rate

**User item 4** ("tile priority, and crono walking over blocked tiles"). Side by side with the
cartridge the whole defect was ONE misplaced lead: the cartridge's Crono enters the throne room from
the bottom and walks to the chair; ours started under the stained glass and walked down through the
throne and the balustrades.

Measured (`PEEK_SAVE` at f1080 of `saves/attract_cena_inicial.state`, `--monitor
"7E0197:1,7E1804:2,7E1884:2" --writer`): the load writes `$0197 = 04` at `C0:42B5` (op 0x57's lead arm
`C0/42B8`, which stores the ARRIVAL tile (8,8) into `$1801/$1881`), then `C0:4B86/4B98` — op 0x8B in
the same fn0, `57 16 59 00 00 06 8B 08 17 ...` — writes `0880/17FF` = tile (8,23). Last writer wins.
The port's init pass ran both, but the host placed the lead on the arrival pixel after `CtSceneCreate`
and its per-frame lead sync held it there. Now `pcLeadInitArm` records the arm's placement as the
arrival's and both entry sites read a later script placement back (`CtSceneLeadScriptPlaced`). The
camera still takes the arrival tile (`C0/7553` reads `$0103`), and the plane still comes from
`$0102/$0103` (`C0/9DDE` → `STA $0155`). Port f3100 = cartridge obj2 `0793/166A`, on the same walk.
Scene 2 moved with it, toward the cartridge: from `saves/wake.state` Crono leaves the bed and stops at
`18A4/08FF` = (394.25,143.94); the port used to end at (390,128), now (394,143).

**The fade.** The leg's tail is `F0 01 / F2 / D3 01 / D3 04 / DF AD 00 07 18`. Op 0xF0 = `C0/3CD0`: a
non-zero low nibble stores `$011C = $011D =` rate, `$011B =` high nibble, `TSB $18` (bit 1). The main
loop's `C0/00A4 JSR C0/0C76` calls `C0/1F24` while that bit is set: `$011D` counts down, and only at
zero is it reloaded from `$011C` and `$0119` stepped; at the target the bit clears (darkening clears it
on the step that reaches 0). Rate r = a step every r+1 frames. Op 0xF2 = `C0/3CFD` parks on itself
while the bit is set. The port stepped every frame whatever the rate and advanced past 0xF2 at once.
Cartridge: `$011D` alternates at `C0:1F46/1F4B`, `$0119` 0E→00 on every other frame f292–f320; `$0100`
flips to 00AD at f1415, black until f1450. Port f3290 ↔ cartridge f1384: 50425/53248 identical (was
3328); f3340 ↔ f1434: 57344/57344.

Lit time per leg now, port vs cartridge (`CT_SHOTS_LUMA` vs `$0119`): 01B 265/262. Still short,
not yet derived: 01C ≈ −29, credits overlay ≈ −30, 0AD 742/858 (the demo battle, item 5), 0DB 82/147.
Residual in 01B itself: the stained-glass glow runs the cartridge's colour cycle ~3 frames out of
phase, with low-bit colour-math rounding (`94EE31` vs `94EF31`).

**One brightness byte.** The same change exposed that the port kept $0119 twice: the host's C0/2848
ramp INCs the WRAM byte while the VM kept `sc->bright` for op 0xF0. On the cartridge they are one:
loc 2 (new game, `saves/intro_pre.state`) runs `F0 00` then `F0 F4` in its init, C0/2848 ramps the
same byte 01..0F (f61–f76), and C0:1F59 clears the armed bit on the first main-loop frame (f77) because
the target is already met. With a real rate countdown and two copies, the port faded the bedroom in
over 75 frames. Now op 0xF0's immediate arm writes $0119 (`3CF9 STA $19`), C0/1F24 writes back what it
steps, and the host's INC re-reads it into the VM (`CtSceneSyncBright`). Bedroom, port vs cartridge
from the end of the ramp: box request +40, typewriter +15, done +34 — identical.

**Still open: the $011E skip.** C0/286C ends `LDA $1E / RTS` and C0/2848 `BNE`s past its whole ramp
when 0xDE/0xDF (`C0/3B45`, `C0/3B98`: `LDA #01 / STA $1E`) staged the change. The port now stores
and clears $011E but does not branch on it, because its two cases in the attract need machines the
live VM lacks: 0AD (via `DF`) fades in from its own `F0 F4` (cartridge $0119 00 until f1450, then one
step per five frames to f1520 — the port's ramp lights it in 15), and 01C (via `DE`) is lit by a
WIPE — `FF 93` (C0:412F sets $0350 = 02) and C0/22D8 forcing `$19 = 0F` (C0:2302) every frame while
C3/000E builds the window, until C3:023F clears $0350 (f769–f801). Taking the skip without FF 92-95
and C0/22D8 turned 01C black.

## 9.25 Loc 0AD: the load runs obj0's slot 1 (C0/595C), carry and all

**User item 5, the starfield.** 4ce7295 left the chamber's blanking unexplained: four `E4` copies in
obj0's slot 1 with no caller in the scene. The caller is the load. C0/56A6 does `JSR C0/5709` (the
init runner) then `JSR C0/595C`: `STZ $6D / LDA $7F2003` (obj0's slot-1 pointer, the only absolute
read of it in bank C0) and `5967 LDA $7F2001,X / BEQ ->597C / ... / JSR ($5D6E,X) / BRA ->5967` —
every op to the 00 terminator, whatever carry it returns. `--hookpc 3D97` from PEEK_SAVE f1400:
four visits, `$6D = 00`, Y = `0206/020E/0216/021E` (body 0x46..0x5E), with obj0's `$1180` never
leaving its idle loop. The port had the hook but stopped on the first visit-ending op; 0AD's slot 1
opens with four C0/4CD9-tail CLC ops (`7D 0A / 7D 16 / 7D 1A / 7C 18`). BG1-only, port f3600 vs
cartridge f1694: rows 0-100 and 180-224 100% identical (2032/25600 and 3328/11264 before). C0/595C
has two more callers (C0/0206, C0/0330), not examined here.

## 9.26 The world-5 flyover pans: op 0x32 scrolls, class $7849 nudges BG1, op 0x3E sets the camera

**User item 1** ("the passing clouds look strange"). Side by side, the cartridge pans across Zeal
for the whole leg; the port's camera stood still over the right part of the map. The driver
(`$7F:0540..`) sets the camera cells (`13 E3 00 60` ...), rebuilds with `3E 01 / 3E 02`, and spawns a
child `2E 00 00 FF FF / 2F 00 00 00 00 / 32 F6`.

- **op 0x32 (C2/164B) scrolls.** After C2/1759 + C2/0F38, `C2/1784` (X) / `C2/17AB` (Y) test for a whole
  pixel (for a negative int word: the high word of the 32-bit negation) and hand its low byte to
  `C2/0568` / `C2/066C` for BG1 (`$00 = 1`) and BG2 (`$00 = 2`), then `$24/$2A += step` unless
  `$1BA7` bit4 / `$1BAA` bit5 / `$1BAF` bit1. The port integrated and dropped the pixels.
- **Class $7849 (C2/7849), a native object** allocated by world 5's bootstrap (`35 49 78` at
  `$7F:0439`, C2/1839) and dispatched by C2/0535 each frame: `INC $22,X`, and on every fourth, scroll
  BG1 right one pixel (`$00 = $01 = 1`, JSR C2/0568). Hence BG1 0.75 px/frame against BG2's 1.0.
  The port ran no native class but music.
- **op 0x3E (C2/09C5)** derives `$20,X = $E3,X * 8` / `$26,X = $E7,X * 8` inside the object pass. The
  port left that to a read-back after the pass, so the pan child (same pass) stepped the old camera
  (tile 185) and wrote 184 over the script's `$60`.

Measured (PEEK_SAVE f100 of `saves/attract_cena_inicial.state`): `$0022` 02FF, 02FE.. from f5 (C2:05C7),
`$0020` holding every fourth frame, `--hookpc C20607` once per four frames with X = 0; at op 05 (f237)
`$0020/22/26/28` = 0251/0217/0150/0150 and `$0107` = 53. The port now prints exactly those.
Still open in this leg: the translucent garbage rectangles (BG3), the Blackbird's colours, and
the `$C6:E241/E27E` window HDMA (the cartridge's black left-edge wipe).

## 9.27 The flyover's garbage rectangles were BG3 drawn with the main screen's BG3 OFF

`CT_OWDUMP` at port f2200 beside the cartridge's `--peek` f206: chr, block grids, block defs and the
BG3 tiles+map are byte-identical (0 differences), and BG2's tilemap differs only at the pan's edge.
The cartridge's `--ppuregs` at f206 reads `TM=11001` — BG3 is off the main screen. The flyover's own
script writes the TM shadow `$0041 = $13` at f103 (C2:1290); the world NMI copies it (C2/027D
`LDA $41 / STA $212C`). The port's composer ignored `$0041`. It now gates BG1/BG2/BG3 on its bits 0/1/2.
The world init C2/012F stores `$41 = 17` / `$42 = 13` (C2/0155/0159, seen as C2:015D at f67); the port
never made those stores, so worlds entered from the field held TM=0. Only these two stores are ported;
C2/012F's `$00-$EE` zero-fill is not.
Port f2200 vs cartridge f206: 56100/56320 pixels identical — **one scanline up** (port row y =
cartridge row y+7, not the field's y+8). ✅ EXPLAINED AND CLOSED 2026-09-17
(disasm/notes/ow_bg_vline.md): the overworld composite sampled BG line `vofs + y` where the PPU shows
`vofs + y + 1` (snes/ppu.c: ppu_runLine takes scanlines 1..224, ppu_handlePixel adds vScroll to that
scanline, ppu_putPixels lands scanline S at output row S-1), and `worldObjLine` read `y - 1` to pull the
sprites down with the map. Both take their own line now; this frame is 56320/56320 UNSHIFTED and the
check's `ATTRACT_CMP_DY=-1` is gone. Loc 16E's one-line residual (9.23) is a FIELD scene and is NOT this
one — the field composite already adds its line (`camy += 1`) — still open. Still open in this leg: the docked Blackbird sprite (not drawn) and the `$C6:E241`
window HDMA.

## 9.28 Loc 0AD's demo battle plays itself: $2989 bit 5 and FD:B957 (PORTED since c6384cc -- see 9.29)

**User item 5** ("the battle is actually happening, with the hud left controllable"). The field's `D8 B0 C0`
(C0/3A83: imm1 -> `$2989`, imm2 -> `$2A1F`) starts a battle whose `$2989` bit 5 is the whole mechanism.
Measured from `build/_orc/s1850.state` (attract f1850, the battle's first frame) with `--monitor`/`--hookpc`:

- **Boot (f5).** C1/801C saves bit 5 to `$B31F` and clears it; C1/80EB restores it (`$2989` = B0 at C1:80F4),
  and C1/80F4 `BIT #20` sets `$AFAB-AD = 01` (the PCs' ATB fills on the first tick) and `$AFAE-B5 = FF`
  (enemies never act).
- **The PC turn (f8).** C1/8461 -> C1/8511 (combatant < 3) -> C1/8541 -> C1/854F `LDA $93EE,X / BNE` ->
  **C1/8555 `LDA $2989 / BIT #20`**. Clear: C1/8562 `$93EE,X = 01`, `JSR C1/0003` A=1 = the menu enqueue
  C1/1B19 (and its ready chime). Set: `JSR C1/B70E` (`$B188-8A = 81`, `$93EE/$93F5/$93FC = 41`) -> C1/8588
  -> **C1/B725**: `$99D4 = 0`, `$99D8 = 1`, `$99DD-DF = 0`, `$9F22-24 = 30`, and a 0x15-byte copy from
  `FD:[FD:B957 + $29AC*2]` into `$93EE..$9402`. `$29AC` = 00 here -> FD:B95D = `41 2F 01 05 00 F1 42` /
  `01 00 ..` / `01 00 ..` (seats 1 and 2 get `01`, so C1/8552 keeps them out of the menu). No writer of
  `$29AC` was found (byte scan: reads only, C1/B743 and FD/B2EC).
- **Commit.** The same path a menu confirm takes: C1/8592 -> C1/B967 (`$93EE` bit 6 cleared; partner nibbles
  `F1` feed only the FD/A8A5 status pre-check) -> C1/BA33 -> C1/BB49 (class `2F` = tech): `+3 = 05` ->
  `$AD8E/$B2AE`, `+6 = 42` -> `$B18C/$AE93`. Tech `$42` = X-Strike (CC/3A09[$42] "Attack enemy w/ X
  Strike"); participants from CC:249F (`$B2EA/EB` = 00/04, Crono and Frog). Target: C1/C85A `BIT #20`
  skips the RNG pick C1/C861 for the upward scan C1/C883 -> combatant 07. Then C1/BFA4 -> C1/4058 (f9),
  `$A013 = 01` at f12, `00` at f276.
- **End (f277).** C1/83AE `BIT #20 / BNE`: instead of `JMP C1/8120` it restores `$5E32/$5EB2/$5F32` and
  C1/83D2 jumps to C1/8423 -- no victory. Flea is NOT killed ($61B0 1018 -> 0F09). `$0117` clears at f337.
- Normal battle (`saves/battle_re/bat3_lake.state`, `$2989 = 02`): C1/8562 runs, C1/855C / C1/B725 never.

**Why it is not ported.** The port's battle has no tech execution at all (battle.c: Tech/Item rows are an
"underived boundary"), and C1/8555's bit-5 arm is recorded there as unported. Skipping the menu without
the X-Strike action and C1/83AE's no-victory end would only trade one wrong battle for another.

## 9.29 X-Strike's motion: the script's roles 1/2 and ONE driver for a PC tech

Ported 2026-09-13 (check `attract-demo-battle-xstrike-motion-vs-oracle`). The demo's X-Strike used to deal its
271 with Crono and Frog standing at home: the anim interpreter ran CE/3C9A for its sounds only.

- **Roles 1/2 are the tech's co-actors.** C1/C759-C77F (the target setup, reached through C1/C1DD at C1/C59A)
  stores participant 1's and 2's SEAT (`$B2EB/$B2EC` through `$B1BE`, FF = none) into `$AE97/$AE98`; the
  executor's C1/D6C0 stores them again. CC/F156 copies them to `$A015/$A016` and, for a party actor, replaces a
  negative one with CC:FBC8[actor*2] (01 02 | 02 00 | 00 01). The enemy command (C1/8D1F) and the PC physical
  (C1/BFC8) write FF; FD/A989 zeroes `$AE91..$AE9B` at boot, which is what an ITEM reads (no item path writes
  them: gato_elixir f457 `$A014 = 00 00 00 01`). The AI's own co-actor stores (C1/9A77, C1/A086, C1/A2CE) are
  not ported. Demo: `$AE97 = 02 FF` (C1:D6D7) -> `$A014 = 00 02 02 07`, homes `78 D0 D0 / B1 90 90`.
- **The move ops do not ask who moves.** C1/5135 (ops 11/12) and C1/6299 (98/99/9A/9B) split on the track
  (`CMP #08` effect object / `CMP #04` every victim / else `$A014,X`) and write that combatant's `$1D0C/$1D23`
  through CF/F03E (mode 0) / CF/F087 (mode 1); both arms stamp `$96DB,Y = C0/F700[$DB]` (C1/51A6, C1/6380).
  The port's two scope gates on the setter (non-actor, PC caster) and the Crono-only tech ROUTE parser that
  owned a PC tech's foot are gone. Every step of both PCs is the cartridge's (f94-f210, `$1D0C/$1D23`), and
  the four `$96DB` changes are its own (C1:69F8 f171/f172, C1:600C f223/f229). The forest Cyclone through the
  same path reproduces battle_tech_cyclone_choreography.md 4.2's legs and facings exactly.
- **Timing, measured, not closed.** Per-track cursors (`$5DBD`, C1:4CEF) match op for op except on frames
  where C1/40B0's body crosses vsync (`--hookpc C14CA2` visits=0 at f122/127/134/158/162/242/254/266; split
  1+2 at f170/171, f174/175) -- CPU cost. The first op is at action+34 on the cartridge, +33 in the port
  (the pre-roll's U packet count).
- **Not yet ported.** Op 0x50 = C1/5ADB, the damage popup as a three-visit state machine (`$A3D1` 0 -> 1
  init, 1 -> 2 digit OAM for every combatant with `$AD9E` != 0 and `$AC5A := 1`, then park until `$AC5A`
  clears, advance 1) -- on the cartridge the victim track sits on it f163-f222. The slide's Z-hop: C1/635F
  stores the `98 mm tt` operand in `$AA04`, `$A920 = $AA3D = 7FFF` (mm = 2) or FFFF and `$A9B8 = that /
  $84` (`$84` = the dominant distance / 2, C1/6605); CF/F0C1 runs a rise/fall velocity into `$A96C/$A96E`
  `speed` times per tick and CF/F149 writes `$A4AF` = -h or -2h. The X-slash effect objects, the white flash
  and the BG dim.
- **Update, same day: the hop, the shadows and the slide's exact arithmetic are PORTED** (checks
  `attract-demo-battle-xstrike-hop-vs-oracle`, `attract-demo-battle-shadow-oam-vs-oracle`). `$A4AF` for both
  returns matches the cartridge value for value and hold for hold; ops 99/9B read `$90` = 06 because C1/4212
  (called from C1/40C1 every loop frame, before the script visits) exits with it, and a parked 98/9A re-stores
  its `mm` on each re-visit. CF/EAF8 adds `$A4AF` to the combatant's cells only. The shadow: CF/EA8A, one large
  OAM at ($1D0C-8, $1D23-4), name $02, attribute `$A5E3 | $9F46` (CF/FBAE, CC/E37D); tiles = ROM D1/5E82
  (byte-equal to VRAM; upload routine not derived), colour 15 of OBJ palette 1 = FF:9E0E (CC/E6D5). Both shadows'
  OAM entries match `$0770/$0780` for eleven steps. That comparison exposed the slide law: C1/65BA computes
  `unit = |minor| << 8 / dominant` (C1/00D7), C1/6331 multiplies by speed (C1/00A7, low 16 bits) and C1/63C9
  starts both accumulators at 0 -- the port's `minor*256*speed/dominant` from 0x80 matched endpoints only.
  The PC physical return and the crit leap use the same law now (bat3crit's (132,65) is unchanged).
- **Update: the number (op 0x50) and the victim track's counters are PORTED** (check
  `attract-demo-battle-xstrike-showdamage-vs-oracle`). C1/5ADB is a three-visit machine on `$A3D1` (INC; the
  digits of every combatant with an `$AD9E` kind + `$AC5A := 1`; park until the animator CF/E781 clears `$AC5A`
  at D1/5E10's 0x80, then STZ `$A3D1` and advance 1). An interpreted script now shows its numbers from its own
  `50`; the span-derived birth (span - 58 - tail) stays only for actions with no script. `23 nn` (C1/5743) waits
  on `$5D9C`, `35` (C1/5940) INCs it, `42 xx` copies it, and `3D..40/45..46 tt` (C1/59B9) store a resolved POINT
  at `$5DA0+2k` -- the port had read `3D nn` as `$5D9C := nn`. `$5D9C`/`$5D9D` are the `$5D80` file's own bytes.
  The X-Strike victim track now matches the cartridge op for op (3D0F/3D13 at op-frames 55/70; the popup 57
  frames). Residual named: GATO's flurry `50` lands rel 149 against the oracle's 157 (gato_cyclone continued),
  the flurry's own pre-roll (24 vs 21) + spill frames.

---

## 10. ENTRY 0 ($60) IN THE ATTRACT -- the motorcycle race, and why the demo stops after 2300 A.D.

Measured 2026-09-14. The attract's timeline (saves/attract_cena_inicial.state, --peek loc trace):
`1F2` (2300 A.D. map) f5816 -> `1B1` f6140 -> `$0384 = 60` f6155 (C3:003B) -> entry 0 runs 440 frames ->
`$0384 = 61` (credits) f6595 -> `06B` f6744. The port reaches 1B1 after 1F2, holds on the unported entry and
renders black for the rest of the attract.

### 10.1 The setup's schedule (from build/_att/f6150.state, `--monitor ... --diff --writer`)

| rel | what |
|---|---|
| f5  | `$0384 = 60` (C3:003B) |
| f21 | the common init: `$0920` = 0, `$038C/$038D` = 0 (7E:308F/308C), `$2105 = 22` (7E:311C) |
| f48 | setup `$7E:3B48` done: tasks `$0920 = 42CE 3749 3754 428B 4F54 4D0D 3D72 4447` (C3:0CF6), `$0A20 = 40CF` (C3:0CB7), `$0B20 = 415E` (7E:3CD7), `$038C = 3F` (7E:3C51); vblank `$0940 = 380B 3843 5221 43F2 44FE` |
| f51 | the first NMI tick: `$0008 = 1` (C3:0911) -- so the post-setup SEED is f50 |
| f59 | `$038D = 0F` (C3:0C28, a script op) |

### 10.2 The setup ($7E:3B48, M=8 X=16 on entry)

`$0384 & 0F` -> `INC $44` (entry 9); `BIT #20` clear -> `LDA #2C / JSR $375F` (song $2C -- the attract's
$60 has bit 5 set: no song); `JSR $3D8C`; decompress packets 6 and 9 through `$3691` into `$7F:0A00` /
`$7F:8A00`; `MVN $7F,$7F` $7F:0A00 -> $7F:3800 ($2E00 bytes); `$3EB7` x5 (Y = 3808/3E08/4408/4C0C/520C);
with DP = $2100 streams through `$3ECD`/`$3ED3` to VRAM ($0200 = 20/18 counts, `$81` = 5A00/7800/AE00);
packet $0C; two DMA clears from $2134 (`$7E:F000` $1000 bytes, `$7E:0C00` $1200 bytes); `$36CC` installs the
HDMA from the record table `$7E:3D45`; `$038C = 3F`; `JSR $45D6`; the `$0384 & 40` branch (entry $60 vs $09/
$49 ...) sets `$0A20/$0B20`, `$038E`, windows `$2123-$2125 = 22`, `$2127 = FF`, `$212E/$212F = 17/04`, and
`JSR $3749` once; `$35FD := 45B0`, `$3024 := 51D1` (the main loop's hook), a WMDATA stream from `$7E:B574`.

### 10.3 What runs every frame (to port)

- tasks: `42CE` (watches page-A reg 0/5/6/8/11 against `$0A30..$0A38`; on change clears `$1446..$1545` and
  runs `$357B`, the SELF-MODIFYING mode-7 table builder: sine/cosine of reg 5 scaled by reg 0 are patched into
  the operands of its own per-line loop at `$3603..$3639`, which writes `$F0FD+Y/$F487+Y/$F484+Y/...` for
  Y < $0380), `428B` (page A reg 1 += reg 9 & 3FF, `$1420/$142C/$1422/$142E` from `$1440/$1442`), `4F54` and
  `4D0D` (the two bikes: signed multiplies C3/094C, divides `$32E2`, the PPU's `$4202/$4206/$4214`, sprite
  packages through C3/0D5E from `$7E:528B/52EB/52FB/530B` tables), `3D72`, `4447`.
- vblank hooks: `380B`/`3843` (ported), `5221`, `43F2`, `44FE`.
- scripts: page A from `$40CF` (loops `$40EA <-> $40F8` every 4 frames), page B from `$415E`.
- render: mode 7 per line (HDMA ch1/ch2 `$211B/$211D`), `$2105` per line (ch5), CGADSUB/COLDATA per line (ch4),
  window 1 left edge per line (ch0), BG1 scroll per line (ch3). The port's sequence render has modes 7/0/2
  only, and its mode-7 fetch reads the title's BAKED map (ctTitleM7Map), not VRAM.

### 10.4 Plan

1. capture the f50 seed (full $7E page, VRAM, CGRAM, PPU + HDMA registers) -- the title's own precedent;
2. render the seed's first frame and grade it against the oracle (modes per line, VRAM-backed mode 7);
3. port the tasks/hooks/scripts frame by frame, graded against oracle frames;
4. wire `FF 60` so the attract continues (entry 1 f6595, 06B f6744).

### 10.5 The picture: the vendored PPU, graded (user decision 2026-09-14)

The stage is drawn by snes/ppu.c through src/seq_ppu.c (a Ppu on a zeroed Snes shell; it reads only hPos, vPos,
openBus and palTiming), fed with what the stage's code writes -- the audio precedent -- instead of extending
title_intro.c's hand transliteration. `ct.exe --seqppucmp <state> <n>` grades that path alone: copy the oracle's
PPU + DMA channels as it leaves vblank, draw with the port's HDMA walk, compare every line byte for byte.
⚠ The race's NMI OVERRUNS into the visible frame: 1180 CPU writes into $7E:F232..$F807 after line 0, all from
the self-modifying table builder $357B (loop at $3634). Drawn with the tables as they are when the frame ends,
the picture is exact on 320 frames (200 from f6150, 120 from f6300), so the builder always finishes a line's
entry before HDMA reads it -- a port whose NMI completes the tables before the frame draws is exact as well.
Check `seq-ppu-race-vs-oracle`.

---

## 11. ENTRY 0 AS A TRANSLATED PROGRAM -- the race depends on the CPU clock (2026-09-14)

### 11.1 Why section 10.4's plan (a hand port on a seeded RAM model) cannot be exact

Measured on the oracle before any porting:

* **The bike frames land by cycle count.** The main loop's hook `$7E:51D1` copies the request `$1400..$140D`
  (or `$140E..`) to `$001F`, decompresses the next bike frame through `C3:0307` into `$7F:0200`, waits for the
  vblank hook to clear `$39`, reformats it into `$7E:0C00..$13FF` with the unrolled `$7E:B574` (eight pages of
  `LDA abs / STA dp`) and answers `$1402/$1410 = 2`. Task `4FAD/5047` sees the 2 and sets `$AA/$AC`, and hook
  `$5221` DMAs the 2048 words to VRAM `$4000/$4400`. How many frames that takes is how many CPU cycles it costs
  next to the NMI's own work: `--monitor 7E1402:2` shows the answers 6, 5, 14, 15, 9, 9, 9, 8, 9, 8, 7 frames apart.
* **The opening and closing transitions read the beam.** Task `3D72` calls `C3:000E` -> `C3/01E4`, a 31-frame
  effect keyed on `$0350` (race frames 8..38 and 345..375) that walks a 224-byte buffer at `$7E:E1C0` by amounts
  from `C3/02DD`: `ROL / ADC $0386 / STA $0386 / TXA / ADC $0386 / ADC $0008 / CMP $2137 / ADC $213C / SBC $213D /
  ... / SBC $213D / STA $0386` -- the PPU's latched H and V counters, in the middle of the NMI -- then chained
  through `[$55]` over `$C0:FExx`. The buffer is HDMA channel 0's window-1 left edge (`$7E:3D64` -> `$E000/$E070`)
  and is also DMAed to VRAM `$5180` by `$5261`.

### 11.2 User decision: translate the program, run it on the vendored hardware

`tools/gen/xlat.py` turns every instruction a `ct.exe --cputrace` run executed (CPUTRACE_UNIQUE=1: the bytes and
the M/X widths at execution) into one labelled line of C in `src/xlat/seq_c3.c`, plus the untaken side of each
executed branch (48 instructions at most, inside the routine). Each line is `X_BOUNDARY` (control is exactly here;
the host's hook; a pending interrupt taken the way `cpu_runOpcode` takes it) and `X_EXEC` / `X_EXEC_RAM` (fetch
the opcode through the bus, check it -- and for code in WRAM its operand bytes -- against the translation, run
`cpu_xlatExec`). The semantics and every bus access are `snes/cpu.c`'s; the clock, DMA, HDMA, PPU and APU are
`snes.c / dma.c / ppu.c / apu.c`. 5940 instructions (5622 executed, 318 static, 6 with a self-modified operand:
the builder `$357B` patches `$3603..$3639`). It is ONE program for entries 0 and 3 (11.6). The translation is re-entrant at every boundary -- all of its state is
the CPU's registers and the RAM -- so a host runs it one frame at a time with no coroutine.

Regenerate: `CPUTRACE_UNIQUE=1 CPUTRACE_FROM=C30000 build/ct.exe --cputrace <state> <frames> <out>` for
`saves/attract_race_handoff_boot8300.state` (450), `saves/attract_race_handoff_f6150.state` (440) and
`saves/attract_entry3_handoff_f6750.state` (330); a `--peek` WRAM image of each stage (the race 440 frames in,
entry 3 300 frames in); then `python tools/gen/xlat.py --name seq_c3 --trace A --wram imgA --trace B --wram imgA
--trace E3 --wram imgE3 --from C3:0000 --stop C0:0000`. ⚠ CPUTRACE_FROM is not optional: a unique trace that starts
in the field hides every later run of an instruction the field already executed (C0/FD library code); the first
translation made that way stopped at FD:E3A6 after 254,401 instructions of entry 3.
**2026-10-10: from power-on.** `python tools/gen/xlat_all.py seq_c3` makes the file from the recipes
attract_race_handoff / attract_entry3_handoff (power-on + 8177 / 8776 under the rule: 5 and 17 frames before C3:0000,
as f6150 / f6750 were) with the same nine runs (A and B are one run on one oracle) and the images on the stage's last
frame (race +430, entry 3 +300): byte for byte the file above. Each trace, one by one, translates to the same program as
its capture's (the states' WRAM differs -- 5348 / 2002 bytes, `$0386` 7B against 3D -- the history before C3:0000).

### 11.2b A button ends the stage (2026-09-14)

Both stages have an abort path, and the cartridge takes it: race (saves/attract_race_handoff_f6150.state) A at f170 -> `$7E:0000 = 1` f203, `$0384 = 02` f224; A at f350 -> f383 / f400; A at f40 (26 frames into the race) is IGNORED. Entry 3 (saves/attract_entry3_handoff_f6750.state) A at f250 -> f283 / f298; A at f100 -> `$0384 = 02` f149; A at f50 ignored. The stage reads the pad through its own hardware (auto-joypad at vblank -> `$4218/$4219` -> its NMI), so the host hands the stage's `input1` the frame's controller word (field.c, beside CtXlatStageFrame). The abort posts a sound command ($7E:377E: `$1E00..03` -> `$2140..43`, then CMP `$2140` for the echo, forever) -- a hosted stage without the audio system runs on its own driverless SPC and never gets the echo -- CLOSED the same day: a silent host now lends the stage an SPC with the driver uploaded
(field.c, 'THE STAGE'S SPC'; check attract-race-abort-silent-host, the same 190 frames as with the audio system up).

Regenerated with the press paths: add `CPUTRACE_BTN=0:40,100:10,0:400` / `0:170,..` / `0:350,..` traces of the f6150 race handoff (200/300/440 frames, WRAM image = the race's) and `0:50,..` / `0:100,..` / `0:250,..` of the entry-3 handoff (200/250/330, the entry-3 image) to the three traces above: 5960 instructions (5658 executed, 302 static, 7 SMC). The original three alone still reproduce the old file byte for byte.

⚠ A PATH NO TRACE TOOK. C3/08D0 (the per-frame vblank routine) points `$0501` at a bare `SEP #20 / CMP $4210 / RTI` (C3:08C9) while it works and back at C3:08B3 when done; a press lengthens the frame and an NMI can land inside that window (the port's headless race 156 frames in). A hosted translation now runs such instructions through the core (`ctXlatInterp`: cpu_xlatFetch + cpu_xlatExec, after the host hook and a pending interrupt, named once on stderr) and returns to the labels at the next boundary; `--xlatcmp` stays strict. Checks: attract-race-abort-to-title, attract-entry3-abort-to-title, attract-race-abort-xlat-core-fallback.

### 11.3 Graded

* `ct.exe --xlatcmp` (check `seq-race-xlat-lockstep`): the same state loaded twice, the oracle run to C3:0000,
  then lockstep -- at every instruction all registers, flags, interrupt latches, the master-cycle count and the
  beam; at every frame all of WRAM, VRAM, CGRAM, OAM and the line buffer. **IDENTICAL for 5,238,700 instructions,
  from C3:0000 to the `JML $C0:0000` at frame 425.**
* `XLATCMP_SEED=stage` (check `seq-race-xlat-stage-handoff`): the port's hardware built by `CtXlatStageOpen` the
  way a host builds it (11.4), against the power-on oracle's first visit. **IDENTICAL, 5,238,668 instructions.**
* `XLATCMP_WRAM=<the port's own handoff image>` (ct_field `CT_XLAT_HANDOFF_WRAM`): 68,699 bytes differ from the
  cartridge's, and the lockstep still holds for 721,309 instructions -- the overlay decompression, the common init,
  the setup and 54 frames -- until the first `C3/02DD` reads `$0386`. With `$0386` and `$1E05` (the sound driver's
  present latch, which only an audio-on host seeds) patched, **the whole race is IDENTICAL on the port's own
  WRAM**: none of the other 68,697 bytes is ever read.

### 11.4 What a host hands over (seq_xlat.h), and the part that is history

C0/3FD3 `JSR $0B4E` (INIDISP $80, NMITIMEN 0, MDMAEN 0, HDMAEN 0) -> `LDX #$06FF / TXS / TDC / XBA / LDA $DF /
JML $C3:0000`: A = operand, X = S = $06FF, D = $0100, B = 0, M = 1, X = 0, I = 1. A scrambled Y/D/C/V changes stack
bytes for 37 frames and no pixel. A POWER-ON PPU and DMA are enough (the stage reads nothing the field left in
either). The SPC is the host's.

⚠ **The clock is history.** `ct.exe --xlatphase` delays the handoff by N master cycles: N = 2..1000 never moves a
bike frame (`$1402/$1410` identical every frame), but the transition edges and the odd NMI-overrun line fall into
at least three outcome classes (N = 0/2/16/256/1000, 4/6/8/64, 32/128); `XLATPHASE_APU` shows the SPC's phase does
the same, and so does `$0386`, which the common init does not clear. `ct.exe --xlatentries` over 30000 attract
frames: the cartridge's own three race visits start at h = 582/582/604, cyc%8 = 2/6/0, $0386 = 27/02/5B, and the
attract loop itself is 11158 and then 11166 frames long. ⚠ Even two POWER-ON runs of the oracle disagree on
`$0386` at the race (1C when stepped per instruction, 7D through `snes_runFrame`): LakeSnes' APU catch-up cadence
moves the SPC's phase. So CT_XLAT_HANDOFF_* is the first visit of the runFrame-driven power-on run (boot f8314:
v=239 h=590 cyc%8=6 evenFrame=1, WMADD $0F200, open bus $C3), and the transition pixels are not a port property.

### 11.5 In the attract

field.c opens the stage at `FF 60` (CtXlatStageHas), runs `CtXlatStageFrame` + `CtXlatStagePicture` once per frame,
lends it the CtAudio SPC (and stops advancing that SPC itself), and at the exit copies the stage's 128 KB back and
reboots. That exposed a host defect the stage depends on: the fade-to-black re-ran the FIELD's location commit
(C0/0C9D) on the overworld's exit, with the stale latch of the hop into the world, so the race was handed
`$0100 = 1F2 / $0105 = 1B1` (cartridge: 1B1 / 1F2) and the port rebooted into the overworld after it. The commit is
now skipped when the transition leaves the overworld. The chain reads 1F2 -> 1B1 -> race (425 frames) -> 1B1 ->
credits -> 06B -> entry 3 (299 frames) -> 087 -> 1B1 -> 1B0 -> 1F0 -> 1B1 -> the title, and round again.

⚠ OPEN (not caused by the translation, first reachable now): on the SECOND loop the port leaves 1F5 by op05 to 1B1
and plays the credits there, skipping 16E/01C/01B/0AD/112/0DB/1F7; the cartridge's second loop has the same four
credits pages and race as its first (--xlatentries: f11767, f13481, f14509, f16489, f17313).

### 11.6 Entry 3 ($7E:671F) is the same program

Entry 3 (`FF 63`) draws its transition with C3/02DD too. It needs no second translation: the overlay, the NMI, the
interpreter and the library are shared, so seq_c3.c is the union of both stages' traces, each with its own WRAM image
for the static extension (the code is shared, the data around it is not). Check `seq-entry3-xlat-lockstep`:
IDENTICAL for 3,199,762 instructions, C3:0000 to its `JML $C0:0000` at frame 299.

### 11.7 The overlay's common init touches the HOST's WRAM (2026-09-14)

`$7E:3000 JSR $3063`, before the entry dispatch, for every `JML $C3:0000`:

```
3063 LDA #04 / STA $038E / STA $038F / STZ $0380 $0381 $0390 $0392 $0394 $0391 $0393 $0395 $0396 $0388 $038D $038C $0366
3092 PEA $4300 / PLD / M7A = M7B = 0 / $4370 = 80 (B->A), $4371 = 34 (MPYL), A1T7 $0000 bank 0, DAS7 $0100 / MDMAEN 80
30BD LDA $0384 / BIT #80 / BEQ ->30D8
30C4   (bit 7 set)   A1T7 $0920, DAS7 $14E0, fire, RTS
30D8   (bit 7 clear) A1T7 $0900, DAS7 $1500, fire
30E9 LDA $0384 / BIT #20 / BNE ->30F6 / STZ $038A / JSR $3177 (driver commands $85 $89 $F2 $81)
30F6 JSR $3843       M7A = $0001, M7B = $E0 (MPYL = E0), A1T7 $0700 bank 0, DAS7 $0200, fire -> $0700-$08FF = E0
30F9 LDX #0900 / STX $13, then the PPU reset through DP $2100
```

A translated stage (the race, entry 3) runs this on its own hardware and hands its WRAM back at the exit. The native machines
(title_intro.c: the title, the credits) modelled it inside their own struct only, so the host's image kept whatever the field and
the overworld had left there -- and one of those bytes mattered: world 7's exit fade `28 00` (a rate-0 cut, C2/20D1) leaves
`$1BF6 = 02`, the cartridge's credits clear it at f3365 (a DMA, no CPU writer), and without the clear 2300 A.D.'s `29 01` fade-in
object died on it (C2/2116) and the whole leg stayed black. field.c seqCommonInitWram now makes the WRAM half at the native
openings; `$3177`'s commands are not posted there. Check attract-2300ad-fade-in-after-credits-init.

⚠ ENTRY 3 DRAWS WITH THE FIELD'S VRAM. Measured from saves/attract_entry3_handoff_f6750.state: of the 23047 VRAM words entry 3
shows at f150, 21991 are the ones present when it opened (loc 06B's load put them there); it changes ~1000. The host hands a
stage a power-on PPU, and this port's field keeps no SNES VRAM image, so the port's entry 3 runs correctly and draws BLACK
(VRAM 352 nonzero words). OPEN: porting the location loader's VRAM uploads is what fixes it.

### 11.8 Entry 3's inputs from the field, measured (2026-09-15) -- NOT ported

What the black picture needs, from saves/attract_entry3_handoff_f6750.state (`--peek` +1 and +150, VRAM in words):
- **Characters.** At f150 entry 3's BG1 tilemap ($0000, 32x32, BG chr $1000) references only $2000-$3FFF and its BG2 ($6800,
  32x64) $2400-$2BFF plus its own $4000-$47FF. $2000-$4F80 are loc 06B's six BG packets, C0/0960 + C0/6D2F: the record's tileset
  (loc 06B = $17) names packets `67 68 69 6A 6B CA` at `$F6:1C00 + ts*8`; each is decompressed to $7E:3000 and DMA'd ($1000
  bytes, $0F80 for the sixth) to VMADD $2000/$2800/$3000/$3800/$4000/$4800 (`--cputrace` of 06B's load: C0:2DCA with those X).
  The same load then writes $5000 (A=5001), the tilemaps $6000/$6800/$7000, OBJ blocks at $1F00/$4FC0/$1D00/$1C00/$0000/$0100/
  $0030/$0130/$57E0 and the streamer's $67E0/$6400/$77E0/$7400.
- **The seed alone is not enough.** A host seed of those six packets into the stage's PPU (tried, reverted) gave the stage 8871
  VRAM words and 251 CGRAM colours, TM = 11001, brightness 15 -- and a black picture: the stage's own uploads never happen in the
  port (its VRAM count stays constant from f50), where the cartridge's entry 3 changes ~6000 words by f150.
- **The WRAM the host hands over differs by tens of KB.** Port handoff (CT_XLAT_HANDOFF_WRAM) vs cartridge: $7E:3000-3FFF, $7E:7000-
  $FFFF and $7F:1000-17FF, $7F:D000-FFFF (the cartridge full, the port zero) among others -- field buffers the port's field does not
  keep. Entry 3's uploads presumably read some of them; which ones is the next measurement (a write/read watch on the stage's DMA
  sources). Note the cartridge's `$0100` is already 1B1 at the hand-off: the loaded location is the field's `scene`, not `$0100`.

### 11.9 A native entry's OPENING costs frames, and the field is gone under it (2026-09-15)

`FF 61` is not instant. C3/0014 sets the vectors and `LDA #01 / STA $4200` (the NMI off), C3/0056-006F decompress the
overlay from `$C3:[$FE:0003]` (= C3:1313, 26381 bytes) into $7E:3000 through JSL C3/0557, and only then does
`JML $7E:3000` run the common init ($3063, three DMAs), the entry's setup (`JSR ($3043,X)`; $7E:6B04 for the credits)
and enable the NMI. All of it is black. MEASURED (saves/attract_cena_inicial.state): $0100 = 0112 at f2320, C3:0014 in
the request's frame + 2, `JML $7E:3000` and $7E:6B04 in frame 18, $038D = 01 at f2338.

Priced in field.c (`seqHold`) on the location load's clock: ct_lz_rom_cycles for the overlay (5728360, cycle-exact for
C3/0557), 8 cycles a byte for the $3092/$30BD/$3843 DMAs (49152 for `FF 61`), and the setup's own cycles from
CtTitleIntroStageSetupCycles (267940 for entry 1: `CT_CALLROM_NONMI=1 ct.exe --callrom 7E6B04` = 276020 less 8080 of
refresh), over CT_LOAD_FRAME_CYCLES = **17 frames**. The port's credits now step $038D on the cartridge's frames relative
to the flip, all thirty of them (check attract-credits-opening-cost-vs-oracle). Each attract leg that holds a credits
sequence came 17 frames closer: 01C 477/486, 112 411/433, 0A5 914/925, 0E6 493/499.

**The field does not run under a sequence.** C0/3FD3-3FE0 is `JSR C0/0B4E / LDX #06FF / TXS / JML $C3:0000`: the field
main loop is left for good and nothing of it runs again until C0/0000 loads what $0100 says. The port kept ticking the
parked location under the title and the credits (the parked `FF xx` re-parks every tick, so on the reboot frame the
request could come back after its done had cleared it). CtSceneTick is now gated on no parked sequence and no reboot
this frame. Measured consequence: the VM's tick count between 16E's `FF 94` completion and 01C's `FF 93` is 4, not 127.

**Still short, measured, not yet ported:** from the credits' last fade step to 112's first main-loop frame the cartridge
takes 31 frames (C0:0000 f2442, first C0:0097 f2473; `--cputrace` from f2435) and the port 23. The cartridge's load
decompresses thirteen packets: the six tiles and the map the port's ct_lz_load_cost prices, plus `$E5:3C21 -> $7F:5080`
(C0/6E03), `$ED:EFBF -> $7E:B000` (C0/0A0F) and `$EF:8F48 -> $7E:C000` (C0/0A4A), the location's SCRIPT `$FB:8021 ->
$7F:2000` (C0/56FF), and two sprite packets into the $7F:8000 heap (C0/4825, from C0/4590's object loader). The same
shape on the door load into 0DB (C0:0000 f2753): four tiles, C0/6D93, the map, C0/0A0F, the script and four sprites.
The model prices neither the script nor the sprites (field_load_cost.md section 7). loc 112's remaining residual after
that is its two `04` waits: `04 08 17` (obj4 fn7) 203 frames on the cartridge against the port's 192, `04 06 28`
(obj3 fn8) 48 against 44.

## 11.10 The second demo battle (loc 07D): the tech's ACTOR and the action's CLOSE (2026-09-16)

Measured against saves/attract_07d_battle_f4540.state (41 frames after the `70 01`), `--monitor 7EA013:1,7E0100:2,7E29C6:1`:
ONE action, `$A013` 1 at f80 and 0 at f595 (515 frames), `$29C6 = 40` f596, the cut `$0119 = 00` f748, `$0100 = B101` f754.
The leg is 1033 frames (f4261 -> f5294 of attract_cena_inicial). The port held 814.

- **The actor is participant 0's seat, not the confirming seat.** FD/B957[$29AC = 02] = FD/B987 puts the demo's
  command (`41 20 01 03 00 FF 28`, tech $28) in SEAT 2's record ($93FC), but C1/BB9A-BBA1 `LDA $B2EA / TAX /
  LDA $B1BE,X / STA $AE91` makes the actor the seat of the tech's first participant (CC/249F[group*3] -> charId 4,
  Frog). Oracle: `$AE91 = 01` f75 (C1:BBA4), `$A014 = 01 02 00 03` f80 (CC:F168). The port animated Robo with
  Robo's pose windows, so the script's `03 10` park was 7 frames instead of 52 (`$A124[1]` armed f133, five
  dwells of $0A, released f184). battle.c: the tech branch of the scheduled dispatch.
- **The close is the script's.** C1/40CE `LDA $9876 / BEQ ->40B0` is the action loop's only exit, written by
  op 01 (C1/4CF7 -> C1/4D00). The tail (dis.py 140D3): #FF post, C1/40E1 one wait (+ one per frame `$5D9B`
  still holds), C1/4116 one wait + CC/F06B + the C1/305C loop (no wait), C1/4138 walks `$AC5C`: a PC entry 0/1/2
  takes C1/4148 (C1/414B, C1/41B4, C1/41B7 = three more), the FF terminator STZ $A013 at once. X-Strike (list
  `07 FF`) `$9876 = 1` f274 / `$A013 = 0` f276; tech $28 (`03 04 FF`) f592 / f595; the run-off (`00 FF`) f946 /
  f951. The pinned per-tech span table (313 for anything but Cyclone/Ice/X-Strike) is gone for PC techs.
- **What the cartridge's script does that the port's cannot.** `--hookpc C14CA2` on the fixture: the
  interpreter is NOT visited on 234 of the 515 frames -- from rel 189 the frame service runs the effect
  programs (CD/1609, one `C1 80` raster wait per frame, the sprite cloud, the COLDATA ramp) and ~12k
  instructions fill the frame, so C1/40B0's body crosses the NMI. The waits on program flags (`22 0F 01`
  etc.) are program-paced and match; the pauses and plays stretch. Port 492 + 2 against 515: the 19 is the
  pre-roll (below), the rest CPU. X-Strike 253 + 2 against 264 (the eight visits=0 frames of 9.29).
- **The pre-roll of tech $28 is 52, counted** (`--cputrace` from the fixture at f78): CD/0015 -> CD/132E:
  `JSR CD/3E75` (1) then four sheets, each CD/1368 (the 32-tile build into $7E:2D00) + one $400 packet (CD/3ED2
  in $400-chunk mode, `$CE0F = 0`) -- 8 frames, three of them the build spilling over a frame; CD/002A the same
  for two sheets (4); C1/4841 (1); CD/0018 -> CD/0D28 (`$987C = 41`): `LDA #0F / JSR CD/3E3B` 15 packets, CD/0F30
  and CD/0F3C one each, four sheet packets (CD/0FD5), three frames of CD/1001's block build, two map packets
  (CD/0FF8), one more = 27; then the counted 13 (C1/485B, 4864, 488D, 8x 4943, 40B0) with C3/0557 inside C1/487D.
  So U = 39 = 5 + 3 + 27 + 4 spill. The packet walk CD/3EFD-3F9D: a $1000 chunk per CD/3E75 when `$CE0F`,
  else a $400 chunk; each CD/3E75 = CD/3E7D `INC $CA20 / JSR CD/3E44 / LDA $CA20 / BNE` = one NMI.
- **The exit, counted** (`--cputrace` from the fixture at f594): C1/83AE `BIT #20` -> C1/83B8 (C1/B3D2's
  `$29C6 |= 40`) -> C1/8423 -> five frames -> CD/03B4 (E+5); CD/0453 when `$CD0C` (the effect load's sheets,
  CD/0FDD): `LDX #0F80 / LDY #5000 / LDA #7F / JSR CD/3ED2` = 4 packets, `JSR CD/3E75` 1, `LDA #0F / JSR CD/3E3B`
  15, CD/0E23 + 1 = 21 (E+26); CD/039E only with `$2A1F` bit6 clear (the demo's is set); CD/0516 `$CE16 = 1`
  (+1), `$CA25 = 0C` (+1), the band's close arm `$CA1F = 17 / $CA1E = 80`, the CD/0534 loop until shut (12),
  `$CA25 = 0E` (+1), `= 08` (+1), CD/044A (+1) -> `JSL C0/000B` = C0/1BE6's `70` -> C7/0004 = TWELVE frames of
  sample upload -> CD/0444 (+1) -> the module returns (`$0117 = 0` at E+56). Then C0/18C1 `JSR C0/0283` ->
  C0/0304: C0/0715, C0/A33B (the map, 6 frames to decompress + 9 to build), C0/00EB, C2/29F7, C0/09DD (3),
  C0/00EB, C0/0A14 (1), C0/56D4 (1), C0/0330 (C0/595C, 1), C0/A712, C0/00EB, C2/2B78: the runner C0/5A46 is
  back 24 frames after `$0117` clears (f675), obj11's `AD 08` at f676. The port left the module on the end
  frame and ran the whole field tail on it: the field script after the battle ends 76 frames early.
- **The boot, counted** (`--cputrace` from attract_cena_inicial at f4497): C1/0000 + CD/0000 (f2), C0/1BAB's
  `70 01` -> C7/0004 = 13 frames of sample-set upload (f3-15), eighteen setup passes of one frame each (f16-33:
  CF/FBxx, FD/A9xx, C1/FAxx, FD/ADxx, C1/CExx, C1/FBxx, C1/C9xx, CC/E4xx-EExx, CD/07xx, FD/E4xx, C1/1Dxx),
  C1/0038 -> CD/000C -> CD/04FD (the band arm + the battle-start sound, f34), the band's 13 frames, C1/3681's
  64-frame PC entrance loop (f48-111), C1/8C09's AI init (5), C1/80F4 (f117: `$AFAB = 01`), C1/8461 -> C1/B725
  (f120), C1/4058 (f122), `$A013` (f123). `70 01` -> dispatch = 122 (0AD: 116). The port's CT_BATTLE_INTRO 159 /
  band arm 69 / ladder 81 are a song-battle's measurements (the song upload is ~55 frames where the demo's
  `70 01` set is 13).
- Legs after this commit (audio lockstep): 07D 995 / 1034 (-2 before the `70`, +61 boot, -21 action, -77
  after), 0AD 919 / 906.

### 11.10b Ported (2026-09-16): the exit tail, the return, the boot's arm

- battle.c: the demo end enters the run-off's stage-2/3 machine (`fleeStage`), which now counts CD/03B4's parts:
  CD/0453 = 21 when `$CD0C` (the effect load's sheets, then `STZ $CD0C`), CD/039E's sound and its 15 only with
  `$2A1F` bit6 clear, then CD/0516's three, the band's close, `0E`, `08`, CD/044A. The run-off's own numbers
  (sound +5, `$CE16` +20) are the same formula with no sheets.
- field.c: `battleReturnHold` = the C0/1BE6 chain's measured frames + 1 + ceil(ct_lz_return_cost / 346888) + 4,
  the VM frozen meanwhile (`battle-return:` line, docs/DEBUG_SURFACE.md).
- battle.c: `battleBootBandArm` = 1 + the C0/1BAB post's frames + 18, `battleBootIntro` = arm + 84 (C1/8461's
  first turn: 13 band + 64 entrance + the AI init + 3; the forest's 159 kept, the demo's 116 against the
  cartridge's 118), and C1/80F4's bit-5 arm (`$AFAB..AD = 01`, `$AFAE..B5 = FF`) at the combat start. With no
  measured post (a --battletest fixture) the pinned 69/159 stand.
- Legs (audio lockstep): 07D 1025 / 1034, 0AD 938 / 906. Left in 07D: the pre-roll's packets (U = 39 against the
  port's 20: the effect load's 27 and the four spill frames are not priced) and the port's 13 frames from the demo
  turn to `$A013` (C1/B725 -> C1/BFA4 -> C1/4058 is two on the cartridge). Left in 0AD (+32): the same, and the
  post-battle regroup -- obj9's `04 0A 45` waits for the party's walk (objs 1/2/5, `00 AB` at 0242/0262/02A6),
  32 frames on the cartridge (f24 -> f56 of s1850 + 330) against the port's 56.

- **2026-09-28: the upload is the BATTLE'S tail, not the field's hold.** On the rule's cartridge (the 0AD fight,
  `--hookpc` + `--monitor 7E1D8B:4,7E2C74:4,7E0117:1`) CD/043E's C0/1BE6 runs at c4306, CD/0444 at c4321, and
  C0/18B9's ack with C0/0905's restore at c4322 -- the battle's NMI (D1/E7E3 -> C0/2C41) drifting the field's
  layers on the stash every frame through the ack's own, so the page comes back as `$1D8B/$1D8D = 05A8/02D4`. Then
  C0/0304 (c4323: 0715, A33B, 00EB, JSL 29F7, 09DD, 00EB, 0A14, 56D4, 595C, A712, 00EB, JSL 2B78, the 034B gate,
  BRL 00EB) and the runner at c4331. The port acked on the C0/1BE6 frame and froze the field for all 25 frames: the
  starfield came back 22 steps (44 px) behind. Now field.c runs `battleTailLeft` = the chain's frames + 1 of
  battle.c's CtBattleTailFrame (the NMI's head and tail) before fieldBattleFinish, and holds only the reload
  (`ceil(ct_lz_return_cost / F) + 4`); the ack lands on the cartridge's exact values, the VM on the same frame as
  before. OPEN: the six camera ticks inside C0/0304 (each C0/00EB is JSR C0/881E + JSR C0/00DE; C0/9585 steps the
  drift) -- the hold stands still, 12 px behind; which frame each lands on is the reload's cycle schedule.

### 11.10c The first turn of a demo battle (2026-09-16)

- **The boot's shuffle runs with bit 5 CLEARED.** C1/8019-8029 saves `$2989 & 20` to `$B31F` and clears it before the
  two C1/AF22 shuffles (C1/8043-80E6), C1/80EB puts it back. C1/AF22's `BIT #20 / TXA` answers X (0) to every draw
  with the bit set and does not advance `$26`, so the port's demo battles filled no `$B163` entry and the scanner
  walked the seed's stale table. Ported: the port's table is now the cartridge's (`01 09 06 04 02 0A 05 08 00 07 03`,
  s1850+20). The demo's commit moved from intro+21 to intro+7.
- **The scan cursor `$B185` is not walked by the scanner.** C1/847E reads `$B185 - 1` (or `$AEC7 - 1` when 0) and
  never writes it. It is one of THIRTEEN cursors `$B179..$B185` that C1/82A2-8311 advances every other tick (`$0027`
  bit 0, C1/8121 `INC $0027`): for Y = 0..12, from `$B179,Y` INC through `$B163` entries until one whose byte in the
  Y-th flag table (C1/B947: `$AFB6 $AFC1 $AFCC $AFD7 $AFE2 $AFED $AFF8 $B003 $B00E $B019 $B024 $B02F $B03A`, eleven
  bytes each) has bit 7 (`$B186 |= $AEC3`, the Y-th bit), cursor = that index + 1; a wrap at 11 leaves it 0. The flags'
  bit 7 comes from C1/8230-828E, the timer loop over the 143 `$AF27` bytes (`DEC $AF27,X`, at 0 `$AFB6,X |= 80`).
  Oracle (boot state f4497): `$B03A[0..2] |= 80` f117 (C1:8277), `$B185 = 05` f118 (C1:82EE, from 0 through entry 4
  = seat 2), the scanner's first visit f119 lands on seat 2 -> C1/8511 `LDA $B03A,X / BMI` -> the demo turn. The
  port walks its own cursor one entry per tick from `$AEC7 - 1` (three ticks here); the timers and the thirteen
  cursors are NOT ported (the AI's condition framework, battle_ai_interp.md).
- **After C1/B725, C1/B967 is synchronous**: C1/BA33's staging (two frames), C1/BFA4 -> C1/4058 (f122), `$A013` f123.
  The port queues the command and dispatches it CT_BATTLE_DISPATCH_LAT ticks later (12 frames).
- **The 0AD regroup** (obj9's `04 0A 45` waiting for objs 1/2/5): each follower's `AB 0C` (C0/2E9A: arm `$1781,X`,
  wait for the drain's `$7F0B01,X = 1`, then `INX INX`) holds 32 frames on the cartridge (obj1 at 0242 f354-386 of
  s1850, `$1781 = 02` armed f355) and five visits in the port.

### 11.10d The object page around a battle, and the scripted battle's return (2026-09-16)

- **C0/0918 saves `$0920..$1DFF` to `$7F:2000`** (`LDX #0920 / LDY #2000 / LDA #14DF / MVN $7F,$00`, called from
  C0/0268 in the D8 staging and from C0/0129 for the menu) and **C0/0905 puts it back** (the reverse MVN, `JSR` from
  C0/0286 right after C0/B262's four OAM hides) before C0/0304's tail -- which is why the tail re-decompresses the
  script (C0/56D4): the backup sat on it. The battle clobbers that page (C1/CE04 clears `$1580` for the item list),
  so without the restore the followers' post-battle `AB 0C` read their E4 dwells from the item list: obj1's `$1583`
  A8 before, 00 during, A8 after (writer C0:0914, s1850+337); the port's dwell was 5 a step where the cartridge's is 10.
  Ported as a host-side copy (field.c g_battleObjSave): the script image stays, as the cartridge's does after C0/56D4.
- **C0/034B `LDA $7E2A1F / BIT #80 / BNE ->035D`**: with D8's imm2 bit 7 (the demo's `D8 B0 C0`) the return writes
  only `$7F03FE = 3` -- no C0/5B1F regroup for the followers and no `$1F = 1`. The port ran the regroup on every
  outcome-0 return, so after the X-Strike obj5 walked to the lead (op 95, ~90 frames) while obj9's `04 0A 45`
  waited on it; the cartridge's obj5 runs only the called fn (`AB 0C`, f354-386 of s1850) and obj9 continues at f60.

### 11.10e The pre-roll's U, counted and ported (2026-09-16)

- CD/0015 -> CD/1323 stages sheets `$CACF = 0..3` and CD/002A -> CD/1314 sheets `4..5`, both through CD/132E:
  `JSR CD/3E75` (one frame) then, per sheet, CD/1368's 32-tile build into $7E:2D00 and ONE $400 packet (CD/3ED2 in
  $400-chunk mode, `$CE0F = 0`) -- 5 + 3 frames, plus the frames a build spills over (CPU, 0..4).
- CD/0018 -> CD/0D28 -> CD/0EBD only with `$987C != FF` (C1/4850). CD/0E9D: ids `86, 7B..7F` carry CLEAR -> CD/0F42
  at once (the run-off's `86`: its programs' arm is the one frame). Otherwise `$2A21 & 01` -> CD/0E79 + `LDA #1F /
  JSR CD/3E3B` (31 packets), else `($2C66 | $2C67) & 04` -> CD/0E79 + `#0F` (15); then CD/0F33 and CD/0F3F (one
  each), CD/0FD5 four sheet packets, CD/0FF8 the $800 map (2), CD/0D53's arm (1) = 9.
- `$2C66/$2C67` are C0/0691's copies of `$0BD7/$0BD8`, which C0/A415-A456 pack from BYTE 4 of the map image at
  $7E:B500 (C0/A33B): `$0BD7 = (b & 07) | ((b & 08) << 1)`, `$0BD8 = ((b & 70) >> 4) | ((b & 80) >> 3)`, both
  `AND #13` under `$01BB`. Loc 07D's byte 4 = 4B (`$0BD8 = 04`, the fifteen), loc 0AD's = 0B (`00`). The port reads
  the byte through ct_lz_map_header_byte at each load (fieldMapHeaderStage) and stages `$2C66..$2C6F` as C0/0691
  does (`$2C68/69 = 0`, `$2C6A = 02`, `$2C6B = $0BDF`, `$2C6C = $21`, `$2C6D = 0`, `$2C6E = 09`, `$2C6F = 0`).
- battleActPreroll = 13 + 8 + (fx ? 1 | 9 + 31 | 9 + 15 | 9 : 0): tech $28 45 (cartridge 52, seven spill frames),
  X-Strike 30 (34), GATO's song 30 (33), the `FF` group 21 (21..24). The pinned 21/33 are gone.

## 11.11 The field's VRAM and CGRAM for a translated stage (2026-09-16)

Entry 3 needs the PPU as the field left it: `ct.exe --xlatcmp saves/attract_entry3_handoff_f6750.state 60` with
`XLATCMP_SEED=apu` (a power-on PPU) diverges at its 17th frame at `$7E:6753`, the instruction after `LDX #$6140 / STX
$2116 / LDA $213A` -- the program DMA-reads BG1's tilemap rows from word $6140 ($580 bytes into $7E:1680, DMAP7 $81,
BBAD $39) and keeps the field's characters $2000-$37FF for its own BG1 (BG12NBA $11); `ppu,apu` is IDENTICAL.

WHEN the hand-off is, on the cartridge (`--cputrace` of that state, CPUTRACE_UNIQUE=1, 160 frames): the state is
SEVENTEEN frames before C3:0000, inside 06B's load -- C0/7F7E f5, C0/09DD f7 (the assembly -> $7E:B000), C0/0A14 f8
(the BG3 assembly -> $7E:C000), C0/56D4 f9 (the script), the object pass C0/5709-58AA f9, the entry's tilemap fill
C0/0A9D f11 (BG1), C0/2DCA f13 (BG2, $6800) and f14 (BG3, $7000), the main loop's first tick C0/5A64 f16, `FF 63` ->
C0/0B4E -> JML $C3:0000 f17. So the reference image is `--peek --frames 17` (an earlier note took `--frames 5`, a
mid-load picture, and graded the port's stale tilemaps as identical to it).

THE ENTRY'S FILL, C0/0A50-0AF3 (the order the port now follows: after the object pass and the camera entry, before
the first tick -- field.c fieldEntryTilemapFill):
- C0/0A73-0A88 `$1D7C/$1D7E/$1D80/$1D82 = $6000/$6800/$7000/$7800`; C0/75E9 builds BG1's 64 rows into $7E:C800 through
  $2180, C0/0A9F uploads ONE half (C0/7F62: `$4E = $0800, $4B = $C800/$7E`, -> C0/2DC8 at `$48 = $1D7C`).
- C0/78EC / C0/794E builds BG2 the same way ($3040 cells, $30C0 bit0 -> +256, $7E:B000 + id*8: B000/B002 on the even
  tilemap row, B004/B006 on the odd -- C0/79CF / C0/7AC1); C0/0AB4 `LDA $1D35 / BIT #02`: detached -> C0/7F77 uploads
  BOTH halves ($1000), else one. The second half's rows: C0/7976 the same 32 rows again when $0BCF = $0BD1 = $10,
  C0/7991 columns 32.. when wider, C0/79AE rows 32.. when taller. 06B: $1D35 = 02, dims 10/10 -> $6C00 = $6800.
- BG3 only with `$0BCA & 1` and `$01BB == 0` (C0/0ACC-0AD6): C0/7CB5 builds it from $3080 / $7E:C000, C0/0AE1 `BIT #04`
  picks one or both halves.
- BG1's second half ($6400) is NEVER uploaded by the entry: it keeps what VRAM held. On the attract that is the
  credits' text tilemap (`183B 1832 1833 ...`, entry 1's page under 1B1) -- ONE VRAM, so the host copies the stage's
  PPU image back into its field image when a translated stage closes (field.c, before CtXlatStageClose).
- The streamer's first frame (f16) writes column 0 of the $6400 and $7400 halves (C0/86C9 `$6400`, C0/8747 `$7400`,
  one word per row): 32 + 32 words the port does not model (its field draws from the map, not from VRAM). Not graded.

What the field's VRAM holds at C3:0000, and what the port hands (tools/entry3_stage_cmp.py, region by region):
- $2000-$4F7F: loc 06B's six tile packets (tileset $17: `67 68 69 6A 6B CA`, C0/0960 -> C0/6D2F -> C0/2DC8, $1000 bytes
  each, $0F80 the sixth): 12160/12160. $5000-$57FF the BG3 tileset (C0/6DCF, $7F:5080 -> $5000, $1000 bytes):
  2025/2048 -- the last 23 words ($57E4-$57FF) are what the ROM's decompression buffer held PAST the packet's end
  (the DMA is $1000 long whatever the packet decompressed to); the port writes zeros there. Not modelled.
- $6000-$63FF, $6800-$6FFF, $7000-$73FF: 1024/1024 each (the fill above). $6400 993/1024 and $7400 992/1024 (the
  streamer's column 0).
- $0000-$1FFF: the object pass's sprite sheets and the window's tiles (1B1's entry: $1F00 <- $9E10 $200, $4FC0 <- $A010
  $80, $1D00/$1C00 <- $F000, $0000/$0100/$0030/$0130 <- $9CF0.. $40 each, $57E0 <- $9260 $40, then $57E0 <- $2200/$0700
  every frame). Not modelled; the program rewrites $0000-$03FF before it draws.
- CGRAM: C0/7084 puts the location's palette (`$F6:24C0 + rec[3] * $D2`, seven rows of thirty bytes) at $2022..$20E2,
  C0/70E9 the text palette (`$FF:B210 + ($2991 & 7) * 16 + 2`) at $2002 with $0C63/$18C6/$739C at $2012-$2016 and
  C0/711D-712A copies the WHOLE base page $2000 onto the live page $2200 (`LDA #01FF / MVN`) -- the OBJ rows included,
  which is where C2/834D's weapon colours ($21BA/$21DA/$21FA) reach the PPU. 256/256 -- after the second half of
  the derivation: the port's $21BA held 152EEE0C2A20 at 06B (the cartridge 9D233B0EB409). `--monitor 7E21BA:6
  --writer`: C3:0636 (the race's setup) overwrites the row at f6171 on both machines; C2:836C repaints it at f6581 /
  f6718 / f6759 = the 1B1 and 06B LOADS. C0/56CB-56D3 `TDC / XBA / LDA #0B / JSL C2/8004` after the slot-1 runner:
  leaf 11 of the $C2 party gate is C2/8C5A, a bare RTS -- the gate is called for its epilogue alone, and every
  location load repaints the three weapon rows (move.c CtVmC2LoadTail; the port's gate had folded every leaf above
  7 into leaf 12).

THE PICTURE: with the fill in its slot, the BG3 assembly, both BG2 halves and the reverse hand-off, the port's f8984
(stage frame 132 after the hand-off at f8852) is IDENTICAL to the cartridge's `--peek --frames 150` of the state on
all 57344 pixels (an earlier grade of 4768/57344 was the picture tool decoding the oracle's [pad,B,G,R] bytes in the
wrong order on top of the stale tilemaps). Tools: tools/entry3_stage_cmp.py (both sides live), CT_XLAT_HANDOFF_VRAM
(the image the stage got), CT_XLAT_STAGE_VRAM=<path>,<frame> (the program's own uploads after stage frame N).

## 11.12 The host's stop is snes_runFrame's, not "the counter moved" (2026-09-25)

Found from `attract-entry3-stage-picture-vs-oracle` going red after bb50d3b (the field's BG machines): 37367/57344,
the Blackbird's palace (rows 100-224) 4 px behind the cartridge's -- symmetric colour swaps, no tile, CGRAM or VRAM
difference at the hand-off.

- The WRAM the host hands over changed in about 100 bytes, none of them for the worse: `$0111`, `$051D`, FD/E292's
  colour records `$0520-$05AF` (now the cartridge's for records 0-3), FD/DE98's tile records `$05B0-$05EA`. Patching
  bytes into the stage at the hand-off (a temporary host hook): the colour records back to the old bytes gave
  57344 again -- and so did records 3-5 alone, or records 10-11 alone, at their cartridge values. Entry 3's setup
  ($7E:6808) calls FD/E275, which zeroes only the twelve MODE bytes, then fills records 0-1 itself and calls FD/E39C;
  the overlay's NMI calls FD/E39C every frame ($7E:37FA). FD/E39C increments EVERY record's tick (+4) and takes the
  longer path when it meets the period (+5), mode 0 or not -- so the records the field hands over change the stage's
  cycle count by a few dozen cycles a frame. The records only moved a CLOCK.
- The frames that differ are the stops. Per-frame thumbnails from the hand-off (PEEK_THUMBS against CT_SHOTS_THUMBS),
  port frame = cartridge frame + 8745: in bb50d3b, stage frames 43/45/47/81/121/127/129/133 differ, each by
  half the picture, the frames around them identical; the pre-bb50d3b binary had the same kind at 79/83/91/101/103/
  125 -- its graded frame (stage 133) was clean by chance. Each is exactly a frame where CT_XLAT_FRAMETRACE's stop
  PC is inside the palace-table loop (7E:69C6/69C7) instead of the main loop's wait -- and the stop's beam there is
  v=0 h=0 (51 of the 562 stops of a run through the race and entry 3 to f8900; the rest at v=225).
- snes.c: the frame counter increments when line 261 ends (vPos -> 0), and `inVblank` drops on the NEXT step, at
  hPos 0 of line 0. seq_xlat.c stageHook stopped on `inVblank && frames != stopFrame`, which is true on that one
  2-master-cycle step too: an instruction boundary there stopped the stage 225 lines early, with the previous frame
  still in the PPU's buffer -- the host showed it twice, and the next stop ran a frame and 225 lines. The cartridge's
  `--peek` runs snes_runFrame, whose `while (inVblank)` loop steps through that point.
- Now: stageHook is snes_runFrame's two loops (`inFrame` = past the first). Measured: stage frames +62..+150 all
  identical to the cartridge's; with the hand-off's `$0520-$05AF` patched to the pre-bb50d3b bytes or to the
  cartridge's own, the picture stays 57344/57344. The frames before +62 (the opening transition, which C3/02DD draws
  on the beam) differ as before -- 11.4's clock history, unchanged by this.
- Consequences. One early stop fell shortly before entry 3's `JML $C0:0000`, so the host had needed a frame more:
  every attract frame after entry 3 comes one earlier (087 at f9087, was f9088); over the timeline to f10800 every
  frame is identical at -1 but loc 087's window wipe (f9129-9156), which C3/02DD seeds from the `$0386` the stage
  hands back -- 95 now, 11 before, EB on the cartridge (`--monitor 7E0386:1` from saves/attract_cena_inicial.state,
  f7063): history either way. The race keeps its length (425), its early stops gone too.
- Graded: the check's new line `entry3-frames` (every stage frame +62..+150 as thumbnails) and its mutation (the old
  stop condition put back: KILLED).

## 11.13 The seed's history under the rule -- no "history" left (2026-09-26, user: "do not open any exception")

11.4 and 11.12 called the transition C3/02DD draws "history, not a port property". That predates CLAUDE.md's rule
(2026-09-18): C3/02DD's beam reads are a VIRTUAL BEAM on BOTH sides, and the zelda3 model it copies then demands the
two sides AGREE -- the deterministic replacement removes the non-determinism, it does not license a difference. So
the stage frames before +62, +62 itself, and every `$0386` the stages are handed are graded, not excused.

**The oracle under the rule.** `CT_ORACLE_VIRTUAL_BEAM=1 CT_ORACLE_LOGICAL=1` (phase-0 loads on by default), from
POWER-ON: every state-loading mode takes `boot` (ctLoadStateOracle). `--xlatentries boot 12000`: C3:0000 at f64 (02),
f2767/f4451/f5480/f7443 (61), f8265 (60, `$0386` = 7B), f8704 (61), f8876 (63, `$0386` = E4); each stage's own code
reaches `7E:3000` at v=98 h=350/354 wherever its hand-off fell -- the phase-0 rule does remove the hand-off's clock.
The port hands the race `$0386` = CD and entry 3 31.

**What C3/02DD reads** (dis 302DD): A and C (`ROL A`), X (`TXA`), `$0008`, the carry `CMP $2137` leaves (open bus),
`$213C` (virtual: `$0351`), `$213D` (virtual: 00), `$00F0 $00F2 $00F4`. At the first call (1F5, the world's window
object) every input matched but `$0008`: 41 on the cartridge, 00 in the port.

**`$0008` is scratch, and the seed reads whoever wrote it last.** Cartridge writers met so far:
- C2/09C5's row loop (0A55 `STA $08`), the world's per-layer rebuild behind object op `$3E` -- PORTED as its direct-page
  words (world.c owRebuildScratch); the first seed call now reads 41 and yields 53 as the cartridge does.
- C2/048A `STX $08` (the object allocator), C2/0D92 `STA $08` (the sprite builder), and the object pass's class words
  (1DD4 = the palette-row object, 641F) -- NOT yet mirrored; the second call reads 00 on the cartridge.
- C3/090F `INC $08` (16-bit) in the C3 overlay's NMI: every `$C3` sequence counts its NMIs there. The translated
  stages run it; the port's native title/credits machine (title_intro.c) does not -- the field wipe at 01C (f2927)
  reads 69, the credits' last INC (f2891).
Per location the call COUNTS agree (31/32 a wipe or window) except 16E (cartridge 32, port 31).

**Tools.** Cartridge: `--hookpc C302DD --from boot --frames N --hookwatch 7E0008` (the word at every call),
`--monitor "7E0386:1,7E0351:1,7E0100:2" --from boot --diff --writer`, `PEEK_WRITEWATCH=0008:1 --peek boot`.
Port: `CT_XLAT_SEEDTRACE=1` (every input at every C3/02DD entry on its translated hardware), `CT_BYTEWATCH=386,351,100`
(the host WRAM per frame). Order of work: the calls in sequence, each until its inputs agree -- then the stages under
the rule (the virtual beam on their own C3/02DD, logical frames, phase-0 start) and the entry-3 check without its
+62 exception.

**2026-09-26 (c): 93 of 377.** Ported since: the world's streaming (C2/0568 / C2/066C and their pixel loops, the writers
C2/0773 / C2/07C2 / C2/0843 / C2/088F with C2/0811 / C2/08DB, the queues C2/090D / C2/0961 -- the direct page, the staging
bytes at ($E0), the records in the NMI's one queue at $60 + $5F, the cells $E3/$E7 and the scroll words $20/$26 in WRAM;
C2/0405's flush resets $E0; the loader's C2/03EF sets it) and the $C3 overlay NMI's `INC $08` (the native title/credits
machine, field.c seqNmiCount; the credits before 01C end on 0069 as the cartridge's do). Calls 0-92 -- 1F5, 16E, 01C -- take
the cartridge's inputs and give its `$0386`; field-wipe-virtual-beam-walk is graded against them. Call 93 (0AD, f4408)
reads `$0008` = 00: the demo battle's main code last wrote it in its AI condition handlers (C1/8F11 cond 02 `STA $08`,
C1/95FA cond 18/23-28, C1/96A5 cond 1B -- the D1 NMI saves and restores $0000-$002A around itself, D1/E7AC / E885). The
port's AI evaluates only conditions 00/17/1F and treats every other as closed; 139 enemy ids use others. NEXT: port
C1/AFD2's section walker and the C1/B80D condition handlers (their $04/$06/$08/$0A and the candidate lists), graded by a
micro-execution sweep like --sweepaitarget.

**2026-09-26 (d): 377 of 377 -- the whole attract, power-on to f8899, the stages included.** Three pieces:
- the battle AI's condition walk ported whole (C1/AFD2, the C1/B80D handlers, C1/8C3E and its eval handlers, resolver 05
  C1/A452 with its C1/B279 calls; battle_ai_interp.md sec. 19, graded by `ct --sweepaieval` over all of WRAM) -- calls 93-216;
- the Frog Squash's formula run on the direct page (C1/E369: three C1/C90B, C1/C92A, C1/C1B2 -- `$08` = d/1024, `$30`) --
  call 217 (f7371) reads the 00 C1/C1D3 left in the 07D and the D1 NMI kept restoring;
- the translated stage under the rule: CtXlatStageFrame runs CtXlat_seq_c3 with g_ctVirtualBeam = 2 (the oracle's own
  mode: virtual only inside C3/02DD-0306; the translation keeps c->pc exact at every instruction) -- calls 310-376, the
  race's and entry 3's seeds.
`python tools/seed_history_cmp.py` runs both sides and prints `seed-history: 377/377 calls equal ($0386, $0008, caller X)
from power-on to cartridge f8899; first divergence: none`.

**2026-09-26 (e): the stages under the whole rule, graded from their hand-off.** `python tools/stage_rule_cmp.py race|entry3`
runs the cartridge from a state saved under the rule from power-on (saves/attract_race_handoff_rule_f8247.state, C3:0000 at
its f19; saves/attract_entry3_handoff_rule_f8859.state, f18) and the port's attract (the hand-off located by
CT_XLAT_HANDOFF_WRAM's print, every frame by CT_SHOTS_THUMBS_FROM/_TO) and compares every stage frame from the hand-off to
the program's exit. The translated stage now runs the rule's second half too: logical frames and phase-0 NMI-off stretches
(snes.c snes_logicalResume, per stage frame, opt-in through CtXlatStageSetRule -- ct.exe --xlatcmp locksteps against a
real-time oracle and keeps it off). X_BOUNDARY calls g_cpuOpHook (the wait detector) and re-dispatches when the valve rolls
the machine back. A stage's LAST NMI-off write (its exit, 7E:3032) notes a phase hold for the host: snes_logicalFresh clears
the rule's state for the next stage's machine (without it entry 3 started 192328 cycles late).
- entry 3: 299 / 299 frames identical, the opening transition included (attract-entry3-stage-picture-vs-oracle re-pinned:
  134 / 134 from +18, the +62 cut gone).
- the race: 63 / 425. Its holds now fall on the cartridge's frames (rollbacks at stage frames 47-50, holds at 51 / 52 / 54,
  all released at 7E:5214) but a few hundred cycles apart (51: 211496 vs 212068; 52: 336698 vs 336894 -- after 51's hold
  both sides had re-synchronised at the same wait loop), and the first seed call (310) finds the beam 38 cycles later (v=43
  h=1074 vs 1036). So the race's per-frame CPU work differs, not only its start: the prime suspect is its SPC handshakes
  (the stage borrows the host's SPC, whose state at the hand-off is the port's audio system's, not the cartridge's).
  OPEN: the SPC's state at the race hand-off against the cartridge's (PEEK_ARAM / CT_AUDIO_PROBE_ARAM), then the hand-off
  clock under phase 0.

**2026-09-30: the hand-offs moved with the rule; the states are made per ct.exe.** `ct.exe --xlatentries boot` under the
rule, C3:0000 at power-on frame (race / entry 3): ba39487 8265 / 8876; a07ca0f (the drain's advancing virtual beam) 8267 /
8879; 30ccdce (the runner's fixed V = 0) 8252 / 8863, the same at 863a18f. The two states above are the 2026-09-26
oracle's. stage_rule_cmp.py now makes its own: the hand-off P from that walk, the state at power-on P - H + 1 (a state's
own count starts one frame after power-on's: the old race state f8247 ran C3:0000 at its f19 = power-on f8265), H
re-measured on it; both power-on runs kept under build/_stagerule/ by ct.exe's sha1. On the new states both stages grade as
before (entry 3 299 / 299, the race 63 / 425 with the same frames differing) and $0386 at the hand-offs is 7B / E4 as
before. attract-entry3-stage-picture-vs-oracle (entry3_stage_cmp.py) still reads the committed entry-3 state.

## 12. THE STORY'S RACE (`FF 00` / `FF 09`, scene 223) -- what it reads and what it leaves (2026-10-14)

The user's capture at Lab 32's west end (loc 0DF, storyline BD, the lead facing Johnny), spliced to
`saves/race_re/race_user.state` (+ `.bin`, `.lakewram`); `saves/race_re/race_f300.wram` is the cartridge's WRAM 300
frames in (the overlay at $7E:3000, for `tools/wdis.py`). User decision: a NATIVE port, in stages -- the logic first, then
the picture -- not the translated stage of section 11.

**The call.** A at f30 starts obj8 (Johnny) fn1. Its second-meeting arm (`$7F0054` bit 7 set): `D2 00 20`, `$7F0054`
bits 1-3, `CHANGELOC 10F (8,8)` ($0100 = 10F at f47 by C0:3B2A), then `16 36 80 86 05`: `$7F0136` bit 7 set -> `FF 09`
(song $2C, `$44` bumped), else `FF 00`. The capture has `$7F0136` = 01 -> `FF 00` at f66 ($0384 = 00 by C3:003B); the
task list is cleared at f82 and rebuilt at f136 (`$0920` = 42CE by C3:0CF6); at f300 it is `42CE 42BD 3749 3754 428B
4F54 4D0D 4339`.

**The controls** (`$4218` -> `$0002/$0003` in the sequence NMI, C3/0A8E; Start with `$0390` set skips a skippable
stage): **B** is the boost (the "W.P." counter and Johnny's distance move: 104 against 168 at f600 with B every 20
frames, the dash gauge empties), **X** changes the camera (the horizon moves), **A**, Y, L, R and the d-pad change
nothing measured. "REST" counts the course down the same in every run (908 at f600, 528 at f1500).

**The end.** The race runs to the reboot: the field loads loc 10F at **f2877** with no input, with A held, with A tapped
-- and at **f2845** with B every 20 frames: the boost shortens the race, so its length is the race's own state. Loc 10F's
script then writes `$29C9` (C0:46A2) and `$7F0154` bit 2 (C0:6BCE) and exits to 0DF at tile (39,14) (C0:0CB8, 36 frames
later), where a robot says "You're worthless!" in both runs.

**What it leaves.** Against the hand-off (f65), the race itself changes nothing in `$7F:0000-01FF`, `$7E:2600-2FFF`
(party, items) or `$7F:0200-02FF`, with or without play; only the field bytes `$0119`, `$0152`, `$0158`. After the return
a played race and an idle one differ in no story flag, party byte or local -- only in the system page, 32 frames apart.
So for the playthrough the race decides WHEN control comes back, not what changes; the "BEST" record (1000 shown) is not
in those pages and is not located yet.

**Stage 1 (the logic)** therefore needs the race's own state machine -- the course counter, the player's speed with the
boost and its gauge, Johnny's speed, the finish -- and the hand-back (`JML C0:0000`, the staged loc 10F). Stage 2 is the
picture (mode 7 with the per-line matrix, the six HDMA channels of section 7.1, the sprites and the HUD).

**The tasks, first pass.** `$7E:4339` (page A, DP $0A00): with `$0044` set (the in-game call, `FF 09`'s INC) it steers
from the pad word `$0006` (JSR 43B8 / 43D5, then `$0A += ($3A >> 2) - ($3C >> 2)`); then `$00B6/$00B8` from the tables
`$7E:4319,X` / `$4329,X` on `($0A0A + $40) >> 6 & $0E`; and every second frame (`$0008` bit 0) `$0006` bit 6 (**X**)
raises `$0A00/$0A04/$0A0C` up to $7F and bit 7 (**A**) lowers them down to $60 -- the camera, which is why A held changed
454 low-page bytes and nothing in the race. `$7E:428B`: `$0A02 += $0A12` (mod $400), then BG positions `$1420/$142C` and
`$1422/$142E` from it and `$0B04` plus `$1440/$1442`. `$7E:42BD`: rotates `$02 -> $06 -> $04 -> $02` (`$F0` scratch).
`$7E:42CE`: compares `$0A00/$0A0A/$0A0C/$0A10/$0A16` with their shadows `$0A30-$0A38` and recomputes what moved (4309..).
Words that climb ~0.89 a frame with no input (position candidates, 8.8): `$0051/52`, `$0076/77`, `$009C/9D`, `$00F0`,
`$00F2`; page A `$0A13/14` +0.25 a frame; `$00BB/BC` +1 a frame. The REST digits' source is not found yet.

**The finish (found).** The page-A script sits on `FD 07 40` at $4007 (jump-to-self and yield) from f266 to f2716: the
race runs entirely in the tasks. At f2716 `$7E:48A6` (a task's state machine on `$78`) writes `$0A20 = $4211`: with
`$78 == 8` (486B) it sets `$0380 = 2`, removes task `$4339` (the steering, `JSR $3934`), `$7E:510D = $60`,
`$0A12 = (($4A + $6F) >> 8) >> 3`, swaps task `$449C` for `$44CE` (`$3974` / `$395E`), points the script at $4211 and
returns through `$39A7`. From $4211 the script runs the finish (task `$44CE` f2716-2717, `$3495` at f2749 -- the fade) and
ENDS at f2877 (`$7E:0000 = 01`, then the teardown's `JML $C0:0000` into the staged loc 10F). Right after the finish test,
48A9 reads the course position: `LDA $9E / BNE / LDA $9C / CMP #1000` -- the 24-bit `$9C-$9E` (`$9B` the fraction)
climbs ~224 a frame with no input. Next: the task that owns `$78` and its phases, the position's speed law (the boost),
Johnny's, and REST's source.
