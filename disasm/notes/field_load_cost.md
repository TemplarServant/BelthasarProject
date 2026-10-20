# What a location load COSTS: pricing the black window

Derived 2026-09-06, after `field_load_fade_in.md` established the shape of a transition
(fade-out 15 f → **black: the load** → C0/2848's 16-frame fade-in → control) and left the black
window itself unported: the ROM spends 31 frames there on scene 468 and this port spent 0.
This dossier prices that window from the ROM's own listing.

## 1. Where the frames go (oracle, scene 468, `--hookpc` on each phase boundary)

`build/ct.exe --hookpc <PC> --from saves/field_re/scene468_stairs_to_120.state --btnscript "81:60"`
reports the frame **and raster line** of every visit, so the boundaries subtract exactly
(1 frame = 262 lines):

| phase | ROM | window | lines |
|---|---|---|---|
| load init | C0/0B86 | f20.240 → f20.242 | 2 |
| song | C0/1B53 | f20.242 → f20.258 | 16 (declined here: same location) |
| **tile graphics** | **C0/0960** | f20.258 → f35.14 | **3686** |
| camera/scroll setup | C0/6DCF, 7084, 7F7E | f35.14 → f35.24 | 10 |
| **map: decompress + build** | **C0/A33B** | f35.24 → f45.106 | **2702** |
| **tile properties** | **C0/09DD** | f45.106 → f47.81 | **500** |
| object/script load | C0/0A14, 56D4, B192, 56A6, 28AA | f47.81 → f47.229 | 148 |
| **first pipeline pass** | **C0/286C** (from C0/2848) | f47.229 → f52.0 | **~1085** |
| | | **total** | **~8140 = 31 frames** |

And the window is the CPU's alone: `CT_HOOK_CYCLES=1` reports **nmi=0 and hdma=0 on every frame
of it** (C0/0B4E quiesces them), leaving only the DRAM refresh, 10480 a frame. So a load frame
hands the CPU **357368 − 10480 = 346888** master cycles — exactly the `main=346894` those frames
print. That is the conversion constant, measured, not assumed.

## 2. The decompressor, C3/0557 — priced instruction by instruction

Nine of the ten decompressions in the window are the location's own packets. The routine is a
segmented LZ; the port already had its ALGORITHM (`ct_lz_decompress`), and what was missing was its
COST. The loop (large-window arm C3/05E5-0658; the 11-bit arm C3/076B is the same code with one
`LSR A` fewer in the match body and `AND #07FF`):

```
05E5 LDA #08 / STA $0B                     a group is 8 flags
05E9 CPX $09 / BEQ ->0644                  at the block end -> the chain
05ED LDA $0000,X / BEQ ->059D              a ZERO control byte = 8 unrolled literals
05F2 INX / LSR A / STA $0D / BCS ->0614    first flag; set = match
05F8 literal   LDA $0000,X / STA $002180 / INY / INX
0601 DEC $0B / BEQ ->05E5 / LSR $0D / BCS ->0614      ...the next flag
0614 match     len = (b1>>4)+2, offset = u12, PHB / MVN $7E,$7E / PLB, then BRA ->0601
0644 block     (X)&3F = the next group's flag count (0 = end), new end = base + u16
```

Timings are the SNES's, as LakeSnes charges them (`snes_getAccessTime` + `cpu_idle`): a ROM byte in
a bank ≥ $80 with FastROM **6**, an idle **6**, a direct-page or WRAM access **8**, the $2180 port
**6**, and MVN **46 per byte** (it re-fetches its three opcode bytes each time — `cpu.c` case 0x54
does `pc -= 3` — plus a WRAM read, a WRAM write and two idles). X is 16-bit throughout, so
`cpu_adrAbx` always takes its extra idle: no data-dependent page-crossing penalty anywhere.

**Result (`src/lz.c`, `ct_lz_rom_cycles`): cycle-EXACT.** Against the oracle, over 27 packets from
five locations, the difference is a single per-arm constant with **spread 0**:

| dispatch arm (C3/0580-059A) | fixed | packets |
|---|---|---|
| large window, destination bank $7E | 1030 | 20 |
| large window, bank $7F (C3/06A8) | 1042 | 1 |
| 11-bit window, bank $7E (C3/076B) | 1054 | 3 |
| 11-bit window, bank $7F (C3/082D) | 1030 | 1 |

(The first three follow from the dispatch's own branches; the fourth is 18 lower than that
arithmetic predicts — the C3/082D copy differs somewhere its cost model does not care about. Named,
not hidden.)

**How it was checked, and why the first attempt read 8% short.** `--callrom C30557 --long` runs the
real routine and now reports `cycles=` and `refresh=`; `--pokewram` seeds $0300-$0305 the way the
loader does. Two traps, both real:
1. the routine runs longer than a frame, so **the vblank NMI fires inside it** and its ~440 opcodes
   land in the total — `CT_CALLROM_NONMI=1` holds it off (this alone was the whole 8%);
2. the DRAM refresh is background, not work — subtract `refresh=`.
`CT_CALLROM_PCHIST=1` prints the per-PC execution histogram, which is how the model's claim about
which instructions run how often was checked one instruction at a time (it found a 5-op error in
the match body immediately).

## 3. What the load decompresses (the loader's own tables)

```
location record   $F6:0000 + loc*14      byte 1 = tileset id, word 4 = map id
packet list       $F6:1C00 + tileset*8   8 entries, $FF = none
packet pointers   $F6:2220 + idx*3       24-bit source
map pointer       $F6:1E00 + mapId*3
prop pointer      $F6:2100 + tileset*3
```
C0/0960 sends entries 0-5 through C0/6D2F — decompress to $7E:3000, then **C0/2DC8 DMAs $1000 bytes
to VRAM** ($0F80 for entry 5) — then falls into C0/6D65/6D97, which decompress entries 6 and 7 to
$7F:6000 and $7F:7000 with no DMA. C0/A33B decompresses the map to $7E:B500, C0/09DD the tile
properties to $7E:B000. The DMA is 8 master cycles a byte plus the channel setup, measured in the
oracle's own bucket (`dma=32792` on the frames that upload $1000 bytes = 8*4096 + 24).

`ct_lz_load_cost` (src/lz.c) walks exactly that and returns the total. Scene 468:
**6 635 264 cycles of decompression + 195 728 of DMA = 19.69 load frames.**

## 4. What the port does with it

`src/field.c`: the transition gains a phase — `fade == 3`, the load's own black window. The scene is
loaded (instantly, as this port does), then the screen stays black and **nothing runs at all** — no
VM tick, no input, exactly what the oracle shows the ROM doing — for `(cycles + dma) /
CT_LOAD_FRAME_CYCLES` frames, and only then does C0/2848's ramp start. Scene 468, running the three
flights of the castle staircase:

| | stage → next stage | of which black |
|---|---|---|
| oracle | 66 frames | 31 |
| port before | 35 | 0 |
| port with the decompressions priced | 54 | 19 |
| port with the map build too | 61 | 26 |
| port with C0/286C too | **66** | **31** |

The last row is the oracle's row: scene 468's three flights now chain at exactly the ROM's cadence,
stage to stage.

## 5. C0/A33B's map BUILD -- the second half, same treatment

The build reads the map it just decompressed back through the auto-incrementing WRAM port and
lays it out in four planes:

```
A548 -> $7E:3000   dims $0BCB x $0BCD          A57D -> $7E:3040   dims $0BCF x $0BD1
A5B9 -> $7E:3080   dims $0BD3 x $0BD5   only when map byte 1 bit 7 is set ($0BCA)
A5F0 -> $7E:30C0 + $7E:7000 + $7E:7040, dims $0BCB x $0BCD, RLE-coded:
        a byte with bit 7 set opens a RUN -- three plane bytes then a length, and the length
        byte ZERO means 256 (`A639 BNE ->A640` not taken -> `A63B LDY #0100`) -- and the run's
        own cell is written by the header at A61E/A627/A630; anything else is three literals.
A67B -> a fixed 4032-byte MVN, then the tail
```

and the dimensions are the map's own first two bytes (C0/A3C1-A408):

```
$0BCB = ((b0 & 03) + 1) << 4    $0BCD = ((b0 & 0C) + 4) << 2      (plane A and the RLE plane)
$0BCF =  (b0 & 30) + 10         $0BD1 = ((b0 & C0) >> 2) + 10     (plane B)
$0BD3 = ((b1 & 03) + 1) << 4    $0BD5 = ((b1 & 0C) + 4) << 2      (plane C, if b1 bit 7)
```

**The structure was checked against the ROM's own execution counts, not argued from the listing.**
`CT_CALLROM_PCHIST=1` on scene 468 reports A548 4096, A554 64, A57D 4096, A5F0 838, A5F5 447,
A61A 391, A641 3649, A644 3258, A657 3258, A60C 6, A649 58, A686 4032 -- and `ct_map_build_cycles`
reproduces every one of those numbers before it prices anything. (It also caught a bad fixture: a
`--teleport` state that reported 15128 cells for a 64x64 map, which no reading of the ROM could
produce. Re-made, it reports 4096.)

Measured against the oracle (`--callrom C0/A33B --regdp 100`, minus the map packet's now-known
decompression and the refresh) over five locations:

| loc | map | model | oracle | diff |
|---|---|---|---|---|
| 468 | 64x64, no layer 3 | 2 278 294 | 2 279 136 | -842 |
| 2 | 64x64 + layer 3 | 2 780 052 | 2 778 714 | +1338 |
| 5 | 48x64 + layer 3 | 2 124 836 | 2 123 762 | +1074 |
| 439 | 48x48 + layer 3 | 1 654 338 | 1 655 118 | -780 |
| 120 | 64x64, no layer 3 | 2 273 770 | 2 273 770 | 0 |

The whole variable half -- the four plane walks -- is derived. What is measured is the routine's
ONE-SHOT half (the header parse, the two fixed 64/63-iteration loops at C0/A670/A67F and the tail
past the MVN): a constant of the routine, 26 780 to 28 960 over those five locations, taken as its
median 28 118. The MVN inside it is derived (4032 bytes x 46 = 185 472; `A686` reads 4032 in every
location measured, whatever the map's size). The residual above IS that constant's spread: 0.06% of
the build, 0.004 of a load frame.

**WALKED (2026-09-25): `ct_map_load_cycles`, exact.** The one-shot half was not a constant: its header
parse branches on map byte 1's bits 7-4 and `$01BB` (C0/0BE8 clears it at every location entry, C0/6DCF's
FF arm sets it), C0/7399 maps each of the six plane dimensions to a mask through a CMP #40 / #20 / #30
chain and copies the record's bounds (bytes 10-13, or the whole plane's when byte 10 is negative), and
C0/A66B's tail walks the location's `$E5:0000` list (7-byte entries: a count with bit 7 writes a column
+$100 a step, else a row; `AND $1E` 16-bit on a run's first pass). The plane model above was also off
by 12 a run of length 0 (C0/A63B `LDY #0100` / BRA past the TAY, which it charged). Walked from the traces'
costs (tools/map_cost_proto.py), `ct.exe --loadcost` reads diff +0 on loc $082 and $005; lz.c uses it for
every location load, the battle return and the menu reload (menu_shell_timing.md section 10); the model
stands in only for a map whose header bytes 2-3 are set (C0/A463 / C0/A4B3, no trace).

## 6. C0/286C -- the pipeline pass before the ramp

C0/2848 opens with one full pass of the field's draw/upload chain (28E1, 0A50, 6F79, 7155, E935,
B204, A7E9, B0E6, 6E5C, 70E9, 29F7, 6EB0, 2B78, 6EF1, 6F0C, B262, 6F5E, 0B28) and then waits for
vblank at C0/EC60. That wait is why `--callrom` cannot time it at all -- with the NMI held off it
never returns, and with the NMI live the handler's cycles land in the total.

So it was timed IN SITU, leg by leg, with `--hookpc` on each of the eighteen entries (scene 468,
frame.line):

```
2848 f47.229 -> 0A50 f47.229 -> 6F79 f50.154   ... C0/0A50 alone is 711 lines, 2/3 of the pass
6F79/7155/E935/B204/A7E9  f50.154-166 (12)     B0E6 -> 6E5C f50.247 (81)
6E5C/70E9/29F7/6EB0/2B78/6EF1/6F0C/B262/6F5E   f50.247 - f51.58 (72)
0B28 f51.58 -> EC60 f51.225 (167)              EC60 f51.225 -> the ramp's first INC f52.242 (279)
```

**The work is 1044 lines**, and over three real transitions (twice into 468, once into 122) it
measures 1044, 1043 and 1052 -- a spread of 0.5%. It is a constant because its loops are
screen-sized rather than map-sized: the same 4032-byte MVN at C0/A72D and the same 256-cell build
in every location measured. `LOADC_FIRST_PASS` = 1046 x 1364 = 1 426 744 master cycles.

⚠ **This one is MEASURED, not derived** -- the only such number in the model (A33B's one-shot half is
walked since 2026-09-25). AFTER A MENU it is also the wrong number: src/field_cost.c walks the same pass
object by object (menu_shell_timing.md section 10) -- C0/B0E6 draws every on-screen object there, ~52-70k
cycles each, where a door load has drawn only the PCs the loader placed. C0/0A50 dispatches between tilemap builders (C0/7B0x on four of the five states measured,
C0/7E2x on the fifth, which cost 33% more), and what selects them is not read out yet; on a map that
takes the other arm this constant is about a frame light.

**And the window rounds UP.** It ends inside C0/286C at `JSR C0/EC60`, so whatever fraction of a
frame the work leaves over is spent waiting -- measured on scene 468, the load starts at f20 line 242
and the ramp's first INC lands at f52 line 242, exactly 32 frames to the line. The port's hold is
`ceil((load + first pass) / 346888)` for that reason, not as a fudge.

## 6b. The rest of C0/00F4's packets, and the object pass's sprites (2026-09-15)

Section 7 used to open by naming "the object/script tail, the load's other DMA and the song upload, ~1.5 frames that
the rounding absorbs". On scene 468 that was true; on a location with a BG3 tileset and a real script it is not. The
load is `092B / 1B53 / 0960 / 6DCF / 7084 / 7F7E / A33B / 09DD / 0A14 / 56D4`, and three of those decompress packets
the model did not price:

| routine | source | destination | gate |
|---|---|---|---|
| C0/6DCF | `$F6:2220 + b*3`, b = record byte **+2** (the BG3 tileset) | `$7F:5080`, then C0/2DC8 DMAs `$1000` bytes to VRAM `$5000` | `b == $FF` -> C0/6E22 `LDA #01 / STA $BB`, no packet |
| C0/0A14 | `$F6:21C0 + b*3`, the same byte | `$7E:C000`, no upload | `$01BB` (so the same test) |
| C0/56D4 | `$FC:F9F0 + id*3`, id = record **word +8** | `$7F:2000` -- the location's SCRIPT | none |

...and the object pass decompresses **sprite sheets**: C0/4590's miss arm and C0/46DF's both `JSL $C3:0002` into the
$7F:8000 heap, and which ones run depends on the objects' load ops and on C0/5C90's per-scene dedup, so no table can
give them. The port's own loaders (move.c CtObjLoad, ct_vm.c's op-0x82/0x83 mirror) add each one's ct_lz_rom_cycles
into `g_ctLzSpriteCycles`; the host zeroes it before the load and charges it with the rest (field.c).

MEASURED (`--cputrace` of the attract's reboot into loc 112, from f2435, and of the door into 0DB, from f2740): the
cartridge's 112 load runs THIRTEEN decompressions -- six tiles (C0/6D5D), `$E5:3C21 -> $7F:5080` (C0/6E03), the map
(C0/A366), `$ED:EFBF -> $7E:B000` (C0/0A0F), `$EF:8F48 -> $7E:C000` (C0/0A4A), the script `$FB:8021 -> $7F:2000`
(C0/56FF) and two sprite sheets (C0/4825) -- between C0:0000 at f2442 and the first C0:0097 at f2473, 31 frames.
0DB's door load is the same shape with four tiles and four sprites.

GRADED: `--loadcost` now runs the three new packets against the real C3/0557 too -- 468 **10/10 exact**, scene 5 (a
BG3 location) **12/12 exact**, the new entries `bg3 E52000 404582`, `asm3 EF7F41 351416`, `scr DB05DE 329340`.
Scene 468's own window is unchanged (its byte +2 is $FF and its script is 30654 cycles: 30.37 -> 30.46 frames, still
31 black), so the calibration of section 4 still holds and field-stairs-run-three-flights still chains at f18/f84/f150.

What it buys, in the attract's own legs (port against cartridge, `$0100` to `$0100`):

| | 1F5 | 16E | 01C | 01B | 112 | 0DB | 1F7 | 0A5 | 0E6 | 1F2 | 06B |
|---|---|---|---|---|---|---|---|---|---|---|---|
| before | 272/270 | 277/275 | 477/486 | 304/317 | 411/433 | 171/172 | 393/392 | 914/925 | 493/499 | 326/324 | 20/23 |
| after | 272/270 | 277/275 | **486/486** | 319/317 | 417/433 | 176/172 | 393/392 | 918/925 | 496/499 | 326/324 | 22/23 |

The load window itself now lands within a frame of the cartridge's on both traced loads (112: 30 frames against 31;
0DB: 25 against 24 -- the ceiling's own rounding). What is still NOT priced: the object pass's non-decompression work
(C0/5CC7/5CEC and the loaders' walks, 148 lines on scene 468), C0/0A50's builder dispatch (LOADC_FIRST_PASS is one
constant for both arms), and the song upload when a location changes it.

## 7. What is still missing (measured, not modelled)



**The OVERWORLD loader (bank C2) is priced only as a measured constant.** Leaving a location for the
world map runs C2's own loader, and it is the same shape as the field's -- **24 `JSL C3/0002` calls**,
the very same decompressor (there are 23 such sites in bank $C2). Measured on the oracle
(`saves/truce_market.state`, walked out of the Truce market's one exit at tile (21,46) -> location
$1F0):

| frame | what |
|---|---|
| f27 | the exit stages ($0117 bit7) |
| f28-f42 | `$0119` `0F`->`00` -- the field's own fade-out, unchanged |
| f43 | `$0100` = `$01F0`: bank C2's loader takes over |
| f44-f81 | its decompressions run (`--hookpc C30557 --hookwatch 0300`: 24 packets) |
| f87 | the world engine's own counter starts ticking |

44 frames from the commit, against the field load's 47 -- the two directions cost about the same in
the ROM. In this port the overworld entry was INSTANT (`ct_lz_load_cost` is only called for a field
destination), which is exactly the asymmetry the user reported: "going from scene 1 out to 1000ad
is faster than from 1000ad to scene 1". It now holds `CT_OW_LOAD_HOLD` = 29 black frames
before its own 15-frame fade-in, 44 in total. -- SUPERSEDED 2026-09-14: the port holds all 44 with no object pass and the
world's own fade-in object (op 0x29, ow_objvm.md) lights it; the 15-frame host ramp is gone.

**Its packet list is derived now** (2026-09-06). The world's header is `$C6:FD00 + world*2` (C2/2757:
`ASL A / TAX / LDA $C6FD00,X`, a WORD per world), and sixteen of bank $C2's twenty-three
`JSL C3/0002` sites run a load, each taking an index byte out of that header -- **bit 7 set means no
packet** (the loader's own `BMI`, not a compare with $FF) -- and multiplying it by three into one of
ten pointer tables:

| site | header index | table | dst bank |
|---|---|---|---|
| C2/27A2 x4 | +0C..+0F | $C6:FDF0 | $7F |
| C2/2B7C | fixed entry 0 | $C6:FDF0 | $7F |
| C2/280C x7 | +00..+06 | $C6:FE20 | $7F |
| C2/284F | +08 | $C6:FE20 | $7F |
| C2/287D | +14 | $C6:FF40 | $7F |
| C2/28AB | +0A | $C6:FEA0 | $7F |
| C2/28CC | fixed entry 0 | $C6:FEA0 | $7F |
| C2/27D9 | fixed entry 1 | $C6:FDF0 | **$7E** |
| C2/2987 | +10 | $C6:FF00 | $7E |
| C2/29B5 | +11 | $C6:FF20 | $7E |
| C2/29E3 | +12 | $C6:FF80 | $7E |
| C2/2A11 | +13 | $C6:FFA0 | $7E |
| C2/2A6D | +15 | $C6:FFC0 | $7F |
| C2/2A3F | +16 | $C6:FFE0 | $7F |
| C2/2959 | +0B | $C6:FEA0 | $7E |

The list, its ORDER and each site's destination bank are measured rather than read off a call graph:
`--hookpc C2xxxx` on all 23 sites says which ran and on which frame, and
`--hookpc C30557 --hookwatch 0300/0302/0305` gives every call's source and destination.
`ct_lz_world_packets` reproduces world 0's 24 packets **in the oracle's own order, byte for byte**,
and `--loadcost 496..503` prices each against the real routine: **24/24, 24/24, 21/21, 23/23, 24/24,
23/23, 24/24, 23/23 exact**. The decompression half varies by world -- 35.10 load frames on world 0,
26.24 on world 2 -- where before it was one measured number for all eight.

### What the REST of C2's window is

Broken down with the oracle's own cycle buckets over the window (`CT_HOOK_CYCLES=1`, frames f43-f86
of the world-0 entry; like the field load it runs with **nmi = 0 and hdma = 0**):

| bucket | cycles | frames |
|---|---|---|
| main (CPU) | 13 494 266 | 38.90 |
| of which the decompressions (modelled) | 12 175 848 | 35.10 |
| **other CPU** | **1 318 418** | **3.80** |
| **DMA** | **1 064 486** | **3.07** |
| refresh (background) | 461 120 | 1.33 |

So the rest is ~6.9 frames of setup and transfers -- three big DMA bursts at f43, f49 and f67
(524 310, 131 100 and 229 396 cycles = about 64 KB, 16 KB and 28 KB at the 8 cycles a byte C0/2DC8
also charges) plus the loader's own loops. `CT_OW_LOAD_REST` = 8.90 frames is that, calibrated so the
whole window comes out at the measured 44; the difference is the partial frames at each end.

### ...and the one thing that makes a world entry MUCH longer: the song

Entering world 1 from location 112 (the 600 A.D. forest edge) measures **109 frames** to free roam
where world 0 measures 77, and its decompressions are if anything *shorter* (33.97 frames against
35.10). The gap is not loader work at all: `--hookpc C709E5` reports **243 to 287 polls per frame
from f74 to f108** -- the APU token poll of a synchronous SONG UPLOAD (`song_load_freeze.md`), ~35
frames of it. World 0's entry from the Truce market shows not one such poll: same song, no upload.

**The entry's own music issue is C2/2F15**, and it is derived:

```
2F15 LDA $0102 / LSR / STA $00        the arrival cell, halved -- the region column
2F1B LDA $0103 / LSR / STA $01        ...and the row. NO -1 here; the music OBJECT's compare at
                                      C2/2FF7 subtracts one, this path does not
2F21 JSR C2/62ED / TAX                -> the region
2F25 LDA $1B9B,X / STA $1E01 / STA $02AE     its song, and the shadow the music object compares
2F3A LDA #10 / STA $1E00 / $1E02 = FF / $1E03 = 80 / JSL $C7:0004
```

⚠ **STILL NOT CHARGED, and the attempt says why.** Wiring that post into the port's entry was tried
(2026-09-07) and REVERTED. The song RESOLVES correctly -- entering world 1 from location 112 the port
computes song $02, which is the byte the oracle writes to `$02AE` at f73 -- but the load itself is
**refused by the driver**: `CtAudioService` -> `CtAudioLoadSong` returns 0 through `$C7:0A80`'s
refusal arm (port 1 comes back with bit 7 set), streaming **0 bytes**, with the driver page intact
(`$1E05` = E0, `$1EF0` = 07, `$1EF2` = B9) and with C0/1B6E's `$81` stop/prepare posted first, as the
field loader does. The clock itself is fine: `--songload 0x02 --from` the same-location state prices
that very upload at **36.97 frames** (26.88 from the Truce-market state -- it depends on what is
resident, as it should).

**Which refusal, and what the two machines look like at that instant** (probed 2026-09-07):

* it is `CtAudioLoadSong`'s FIRST abort -- `$C7:0A80`, the driver answering the command block with
  port 1 bit 7 set -- and not the second (the `$07` defrag refusal, which prints and did not);
* the ROM makes **no other driver call at all** between the commit and that load: `--hookpc C70004`
  over the whole window reports exactly two, one at f21 (cmd $18) and the load at f74. So there is no
  preparation step the port is skipping;
* and the two drivers are in **different states**: the oracle's is PLAYING (`--aram --from ... --frames
  70`: v0/v1 sounding, srcn=$21, spc->cpu ports `00 00 00 CF`) while the port's is **SILENT** -- zero
  voices with gain, ports out `E0 00 00 CF` -- even though its own boot load of that same song $21
  streamed 2250 + 3681 + ... bytes successfully (`CT_AUDIO_LOADTRACE=1`).

So the port's driver has stopped playing somewhere after the boot load, and a driver in that state
refuses the next song. That is an audio-module question, not a loader one, and it has to be answered
before the entry's upload can be either performed or priced: charging the frames for an upload the
port does not perform would leave its overworld music as the field's and the clock as decoration.
Once the driver takes the song, the frames come straight from `CtAudioLastPostRaster`, the way
`ct_vm.c` already consumes them for op 0xEA.

What the model does not charge at all, measured on scene 468:

* the object/script load tail (C0/0A14, 56D4, B192, 56A6, 28AA) -- 148 lines, and 56A6/56D4 scale
  with the scene's script rather than its map;
* the DMA the load issues beyond the six tile uploads (the map, the properties, the object
  graphics) -- about 120 000 cycles here, visible in the oracle's `dma=` bucket on f31/f35/f47-f51;
* ~~the location song (C0/1B53) when it actually loads one~~ -- CONSUMED 2026-09-14, see below.

Together ~1.5 frames of the 31, and the rounding above absorbs most of it: the port holds 31 black
frames on scene 468, which is what the oracle holds.

Also unpriced, and small: the object/script load tail (C0/56A6/56D4, 148 lines here).

### The location song IS the load's now (2026-09-14)

C0/1B53 ends in `JSL C7:0004` twice (C0/1B73 the `$81`, C0/1B8B the `$10`), inside the load. The port posted the `$81`
and left the `$10` to the frame's service, which ran it DURING the black frames -- so a load that uploads a song cost
the same as one that does not. Measured where it is big: the attract's demo-battle reset into loc 0 (the reset's fresh
driver must take song 0): `--hookpc C70140/C70194` the `10 00` JSL from f57 line 246 to f68 line 142 = **10.6 frames of
the load's 12.2** (C0:0066 f57 line 241 -> C0:0094 f70 line 22), with `CT_HOOK_CYCLES` nmi=0 hdma=0 throughout.

* `CtSceneCreateIn` posts the `$10` inline (both arms: C0/1B59's held re-post and C0/1B7F's record) and keeps the
  sink's price (`CtSceneLoadSongPrice`); `fieldLoad` prices it on the loader clock's quiet background (engine mode 2:
  no NMI, no HDMA -- the load's own), the load hold adds it.
* ⚠ Two kinds of time: `ct_lz_load_cost` and the DMA are CPU work (346888 a load frame, the refresh excluded); a JSL
  priced on the clock is WALL time (it walked the raster through the refresh) and goes in at 357368 a frame.
* A reset-initiated load (boot_loader.md 8) counts the reset's own frame as the window's first.

Result: loc 0 after the reset holds 38 black frames (2,460,030 CPU + 11,307,920 on the clock) and 1B1 arrives 56
frames after the reset, as on the cartridge. Loads that do not change the song re-post in ~5,500 cycles and do not
move. Check: attract-battle-abort-reset.

### ...and an init script's song is the load's too (2026-10-20)

The load's LAST JSLs -- the init runner's (C0/5709) and obj0's slot 1 (C0/56C8) -- are queued while the port loads and
posted by `CtSceneLoadPostsFlush` as many frames before the load's end as the object pass's work after them takes. Their
price was thrown away: a song one of those scripts plays is uploaded inside the load, NMI off, and the cartridge's black
lasts as long as the upload. MEASURED, the playthrough's leg 29 (the rule's cartridge, `--monitor` 0100/0119/011F/1E00/29AE
from leg 28's end): 070 -> 071 commits at f7362; the clearing has no song of its own (its record's byte is FF, C0/1B7D
`BMI`), its obj init's `EA 1B` posts `10 1B` at f7378 and `$1E00` holds it to f7408 (the upload), slot 1's `18 FF` then,
and the ramp's first INC at f7425 -- 63 frames. The port held its 30 (`loadcost: scene 113 -- 9900948 cycles + 228520 DMA`),
flushed both JSLs at f7385 and lit the clearing at f7394: the gate's scene and the warp to 1000 A.D. 31 / 39 frames early.
The flush now prices them like every other JSL (vmSoundPostAcc, each from the line the one before returned on) and the
host's load hold adds the frames they crossed (field.c, `loadpost:` under CT_LOADCOST): the ramp at f7425 and 008 at f10149,
the cartridge's frames. Not covered: a priced post that falls due on the load's very last frame (the hold has already
ended when the flush runs; the 1D3 load's `18 FF` is such a post, and costs nothing).

## 6. Repro

```
build/ct.exe --loadcost 468 --from saves/field_re/scene468_stairs_to_120.state
build/ct.exe --hookpc C30557 --from saves/field_re/scene468_stairs_to_120.state --btnscript "81:60" --frames 60
CT_CALLROM_NONMI=1 build/ct.exe --callrom C30557 --long --from saves/field_re/scene468_stairs_to_120.state \
    --pokewram "0300=A8,0301=D3,0302=E5,0303=00,0304=30,0305=7E"
CT_HOOK_CYCLES=1 build/ct.exe --hookpc 0097 --from saves/field_re/scene468_stairs_to_120.state --btnscript "81:55" --frames 55
python tools/regress.py --only field-load-cost-vs-rom
python tools/regress.py --mutate field-load-cost-vs-rom
```
