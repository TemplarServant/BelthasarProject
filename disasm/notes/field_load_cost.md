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

### The object pass's own work changes the game, through the play clock (2026-10-20)

Leg 31 of the playthrough (the trial) measured what the open item above costs. Each location load runs with the NMI off,
and the NMI is what counts the play clock (`$0400`, frames): the clock stands for exactly the load's NMI-off stretch, and
C0/0C70 seeds the field's random cursor `$01F8` (op 0x7F, C0:FE00) from `$0400` at the next load. A load the port prices a
frame short leaves its clock a frame ahead for good. The trial's four loads (the fair's flashbacks, 1B7 and back to the
courtroom 01B), the clock's standing spans on the rule's cartridge against the port:

| load | cartridge | port | port's price |
|---|---|---|---|
| 01B -> 1B7 (scene 439) | f3469-f3513, 44 | 43 | 13,799,774 + 228,520 DMA = 41 black |
| 1B7 -> 01B (scene 27) | f3849-f3890, 41 | 39 | 12,391,294 + 228,520 DMA = 37 black |
| 01B -> 1B7 | f6378-f6423, 45 | 43 | 41 |
| 1B7 -> 01B | f6664-f6705, 41 | 39 | 37 |

By the jury the port's `$0400` reads 05 where the cartridge's reads 00, the courtroom's load seeds `$01F8` = 0D for 09, and
the juror obj18's fn3 (`7F 06 / 12 06 80 02 ..`, the vote when neither `glb[84]` bit decides) rolls "guilty" where the
cartridge's rolls "not guilty": obj18 walks left to (3,14) at f7754 where the cartridge's walks right to (12,14) at
f7749, and every box of the trial after it comes later.

WHERE THE FRAMES ARE (`--cputrace` CPUTRACE_BEAM=1 of the 1B7 load from C0/56D4, tools/cycle_prof.py C0:56D4 -> C0:2848):
3,824,670 cycles, of which bank C3 -- the script's and thirteen sprite sheets' decompressions, which the port prices --
3,424,900, and the rest 400,000: C0 334,222 (C0/2E1x the sprite sheets' VRAM DMA 51k; C0/5C9x the per-scene dedup 35k;
C0/58Fx-591x the init runner 36k; the loaders C0/45xx-46xx, 4Bxx ~70k; C0/57xx, B2xx ...), FD 52,260 (FD/DF7x-DFBx), C2 and
FF 13k. That 400k is 1.15 load frames -- what the port is short here. Pricing it means walking those routines per object
the way lz.c walks the decompressor; until then the playthrough's legs with several big loads drift on the play clock.

## 9. The whole load, walked (2026-10-21)

The open item above is closed: every leg of the load from the NMI-off write to C0/2848's first vblank wait is walked from
the ROM, its branches decided on the WRAM they read, each instruction LakeSnes's cost off the cartridge's beam traces
(src/field_cost.c `ct_field_load_cycles` + `ct_vm_op_cycles`; the constants re-derived by tools/load_cost_consts.py; graded by
tools/load_cost_cmp.py). Per leg, against the cartridge's traces of the route's loads:

| leg | what decides it | grade |
|---|---|---|
| C0/0B5D -> 0B86, 0B86's variables, C0/00F4's and C0/56A6's own instructions, 092B, 7084, 7F7E, 6F79, 7155, E935, 5929, C2/8004's JSL, 595C's head (206), 28AA, the JSRs | nothing: LOAD_FIXED 9,070 (+18 on a location >= $100: C0/092F) | the remainder, 9,057 / 9,093 on 1B7 / 005 (the DMAs' sync) |
| C0/1B53 | `$7F01ED` set: 252; the record's song FF: 372; else 516 | exact |
| C0/0960 -> C0/56D4 (the packets) | loadPackets, shared with the menu reload: lz.c's decompressions, the map build, the uploads | exact to the DMA sync (+-10) on every load |
| the map's header bytes 2-3 (C0/A463 / C0/A4B3) | 182 a non-zero byte; its low field 118 + 54 a step (+58 bit 3), its high field 166 + 54 a step (+58 bit 7) | loc 008: exact |
| FD:DE98 (FD:FFFA) | the record's byte 1 & 3F -> 12 entries at FD:F310: 4194 + 3258 a kind 2, 5096 a kind 4, 3812 any other | exact on every load (8 configurations) |
| FD:E292 (FD:FFF4) | the record's byte 3 -> 6 entries at FD:FA77: 2202 + 632 a 10, 1020 an 80, 370 any other | exact (5 configurations) |
| C0/B192, B204, A7E9 | 578 + 80 N, 11966 + 78 N, 326 + 66 N (N = $7F:2000) | exact |
| C0/5709's fills | 8190 + 1124 N + its three DMAs 50,886 | exact |
| the init runner C0/58ED-5924 | 460 N + 12 (an object whose $1100/$1101 word is negative: 214) | exact |
| each op it dispatches | CT_VM_DISPATCH 152 + the handler's arm (below) | exact on every op of the traced loads |
| C0/9DC3 (5926's BRL) | the arrival tile's shape (C0/9AD3 with $66 = 80 / $68 = FF) and $6A: a 64-entry table | exact |
| obj0's slot 1 (C0/595C) | 206 + 152 an op + the handler | exact |
| C2/8004 (A = 0B) | the party: each slot 98 / 1174, the double techs (FF:F958: 5 pairs over the learned bits $2837), C2/82E1's seven weapons | exact |
| C0/2848 -> C0/0B38 (the pass) | pass286C, now with class 1 (below) | exact to the DMA sync on the traced loads |

The ops. A handler with no branch is its straight path (VM_OP_STRAIGHT, 107 of them). The branchy ones walked: 82 (C0/5C90's
scans, 92 a slot: a hit at k 3064 + 92 k, a miss at the free slot j 5166 + 92 j incl. the decompressor vector's JMP; $73's
three stores -6 / +6), 83 (3778 + 92 k / 5916 + 92 j / 3812 from the table; +58 for the 06 arm), the placements 8B 8C 8D
($0F80 bit 7) and 8E (the operand's), the PC loaders 57 5C 62 6A 68 6C 6D 80 (the entry; the party slot -- lead 3470, slot
1 3662, slot 2 3712, none 370 -- the arrival's bits 2-3, a follower copying the lead +102, $78 +58), the comparisons 12-16
(the op's own cycles + the $6467 comparator's arm), 18, 0B, 1A, AC, F0, 6F (64 a shift), 4E (46 a byte), 33, E4 (C0/AF4E:
per layer 778 + 46 dx (dy + 1) + 440 dy, per property bit 822 + 266 dx (dy + 1) + 558 dy) and D2 (C2/8004 with leaf 0: the
epilogue's cost + 522 / 630 / 738 / 960 by the slot found), and 83's size-3 arm (C0/47E9 JSR C0/E12A: +74, then 6690 +
the $0C00-byte DMA at 8 a byte + frame 0's 96 tiles through the pieces' model + 48 template reads, 12 more a negative row). Any other branchy op is charged its fall-through path and named
(`loadwalk: ... -- op: op XX`).

The pass's sprites. C0/B0E6 loops `JSR C0/CAD9` while it returns carry set (C0/B10C), so a class-1 object (16x32) runs
BOTH halves inside the pass: C0/D28A (the frame, C0/E97A's two arena slots, the first 16 tiles) and C0/D30D (the other 16,
the queue's entry -- two $200-byte rows, 4096 cycles more in C0/B1B2 than class 0's two $100-byte ones -- and the template's
8 rows, 12 more each negative one). C0/B701 then draws it through C0/BCDC (class 1's B8CA: 8 rows, three arms by the screen
Y) and C0/B3DF's eight OAM entries; C0/E9FF finds nothing (C0/EA42 has released the pair). The arena grants are general
(C0/E952: 340 + 86 a taken slot; C0/E97A: 410 + 86 / 124 a position passed), and C0/EA42's release too. Canyon 070 (a class-1
object): the pass 1,317,307 against the cartridge's 1,317,306.

THE PORT'S WRAM. The walk reads the port's image once the location is loaded (field.c, after the map header's layer bytes
are in -- $0BCA decides C0/286C's BG3 builder, 315,802 cycles on loc 002 -- and before the host's halves of the pass write
the doors, the chests and the tilemap). Two port divergences it found, both fixed to the ROM's stores: C0/4557 (ops 82 and
83) `STA $1101,X` = the operand and `LDA #7F / STA $1200,X`, and C0/4476 (op 81) `STA $1101,X` -- the NPC mirror made none,
so a location's objects kept a previous location's $1200 (1B7's objects 29-31: 00 / E6 / 07 against 7F; C0/E687 reads it).
$0173 is kept as the ROM keeps it (C0/58DC STZ, op 82's INC).

THE FRAMES. The stretch starts at phase 0 (CLAUDE.md), so its CPU work W crosses floor(W / F) boundaries before C0/0B38's
RDNMI read, which falls through inside the vblank's first 37 lines; C0/0B3F's NMI, then C0/EC60: the first ramp INC is
`ct_nmi_wait_frames(W) + 1` frames after the commit (field_cost.h). The port loads one iteration after its commit and ramps
the frame after its last black one: loadHold = ct_nmi_wait_frames(W) - 1. W adds the sound driver's time on the loader
clock (the song posts, now both: C0/1B73's $81 was posted and its price dropped), and the init scripts' and slot 1's
queued posts when they are flushed in the load's last frames: their SPC time joins W whole and the hold is recomputed
(it used to add their whole frames only -- Yakra's 0C6, a 12.68M-cycle song upload in its init, read a frame long).

WHAT IS LEFT. On the route's loads the CPU work agrees with the cartridge's to tens of cycles (the DMA sync); the frames
that still differ are the SOUND DRIVER's: C0/1B8B's $10 on the cartridge waits in C7/0A8B (`CMP $2140 / BEQ`) until the
SPC echoes -- 60 lines on the forest's 074 from the world map (79,352 cycles), 10 on others -- where the port's SPC answers in
2-4 whatever line it is posted on: the two SPCs are not in the same state at the load. And a song upload (loc 001's,
3.07M cycles) prices 2% long on the port's SPC (+63k). Those decide d on 074, 075, 081, 070 and 001. The pass's arms still
refused (charged ct_lz_load_cost instead): $1D35 bit 2 (C0/7CB5's wide BG3 build, C0/7F77, C0/87F1's arms: loc 008),
class 2 (C0/D4F7), class 1 with $1780 = 3 (C0/CF21), a drawn object with $1B00 bit 7.

## 10. The world load, walked (2026-10-21)

C2's loader, C0/0B5A (the NMI off, on the commit's frame or the reboot's C0/0000) -> C2/23CC (`LDA #81 / STA $4200`),
beam-traced on the attract's five world loads under the rule (`--peek boot --frames <commit-10>` with PEEK_SAVE, then
`--cputrace` CPUTRACE_BEAM=1, 60 frames): worlds 5, 7, 2, 3, 0 at c2225, c5021, c7846, c9984, c10587 (`--monitor 7E0100:2
--from boot --settle 0`). W = 12,182,050 / 12,557,060 / 11,161,000 / 12,402,158 / 14,266,940 non-refresh cycles, of which
C3 (the decompressions) 10,125,456 / 10,506,734 / 9,101,598 / 10,339,704 / 12,176,280 -- ct_lz_world_cost + 18 a packet
(the vector's JMP), exact -- and the rest C2's own code and DMA, 2.05-2.09M. The old price charged the decompressions plus
CT_OW_LOAD_REST = 3,087,224 (the window's remainder measured on world 0 under the valve, which carried a frame): every world
1.0M (3 frames) long, and the black ceil(C / F) - 1, 3-4 frames longer than the cartridge's on all five.

WHAT VARIES (per-PC counts and costs across the five traces): only
- the packets: a site's C2 path is 282 cycles (C2/278C-27A2 and its copies), its skip 6 more than the BMI's not-taken arm, so
  a packet costs 276 over a skip. Only C2/2CC1's sites 27A2 / 280C / 284F ever skip (bit 7 of the world header's byte, read
  off the ROM for all eight worlds: 2 skips three, 3 / 5 / 7 one, 0 / 1 / 4 / 6 none);
- C2/2A44's lists after its packet (site 2A6D: header byte $15 into $C6:FFC0, to $7F:0400): four counted lists copied through
  $2180, entries of 7, 3, 3 and 2 bytes at 506 / 234 / 234 / 166 cycles (C2/2A8E, 2AE4, 2B1A, 2B4D) -- counts 14/1/20/25/80
  in the first list, 2 in world 0's last;
- C2/09C5's two first-screen builds (C2/2C81 / 2C88, one a BG) over the camera's cells $E3/$E7: the start column's and row's
  odd/even arms, the row wrap at $0A >= 48 (6 in each of the 18 columns), the column wrap at $08 >= 78, and C2/0ADC's second
  screen (24 a column at VRAM column 32 or past) -- 366 on world 5's cells B9/7B, 510 on B0/72;
- the DMAs' 8-cycle alignment: up to ~30 cycles a call (a timing artefact).
The rest is fixed: WORLD_LOAD_FIXED = 2,041,796, world 5's trace less the above. It then predicts worlds 7, 2, 3, 0 to -22,
-36, -6, +60 cycles (src/lz.c ct_lz_world_load_cycles).

THE FRAMES. The stretch starts at phase 0; C2/23CC turns the NMI on, and inside the vblank's first 37 lines RDNMI is still up
and the NMI is taken at once (world 5: C2/23CC on line 255, the play clock ticks that frame), past them at the next vblank --
the field's C0/0B38 edge, so the first world NMI is ct_nmi_wait_frames(W) frames after the commit: 35 / 37 / 33 / 36 / 41,
the cartridge's play clock ($0400) to the frame, and the port's black is one less (field.c). The port's five world legs now
match the cartridge's stall and visible span exactly (the attract under the rule, `--monitor 7E0100:2,7E0400:1`).

A TRACE ARTEFACT on the way: C2/23A8's VRAM clear is ONE DMA of 540,076 cycles, past a frame, inside one traced instruction,
so tools/ctlib/beam.py's unwrap (v going down = one frame) lost a frame there and its refresh count read 264 lines short --
10,560 cycles too many, ~8 lines, on every world. beam.load now also counts the frames the step's cycle stamp says it
crossed. (No field load has such a DMA; its traces read the same.)

Still measured: CT_OW_RETURN_REST (C2/2C93's rebuild after the X menu, field.c) and the loader's song (above).

### The pass's wide layers and three more arms (2026-10-21)

Traced on the attract under the rule (`--peek boot` states, CPUTRACE_FROM=C00B86): 16E (c2492, $1D35 bits 1 and 2, BG3 on),
087 (c9115, bit 2, no BG3), 01C (c2880 + its load, a class-3 object).
- $1D35 bit 1 (BG2 wide): besides C0/794E's build (b78EC), C0/0AB4 uploads through C0/7F77 -- `LDX #1000 / STX $4E / BRA
  7F67`, C0/7F62's 0x800 bytes doubled, +16384 + 18 -- with the BEQ not taken and the BRA (+12), and C0/87F1 skips its BG2
  row C0/8365 (-9936: 087 against 16E). Unpriced before (16E, 0AD).
- $1D35 bit 2 (BG3 wide): C0/7CB5 -> C0/7CE7, 32 rows of C0/7D66 at column 0 then, by $0BD3 / $0BD5, 7D0D (both $10: the
  same 32), 7D28 (32 at column $20) or 7D45 (rows 32-63, `$1D86 := 2`). Rows start on even columns: 9114 even, 9132 odd,
  583,872 for the 64; the loops 11,576 (7D0D, 16E's trace to the cycle), 11,916 (7D28) and 11,998 (7D45) from the same
  instructions. C0/0AE1's upload is C0/7F77 (+16402, the BRA -6), and C0/87F1 skips its BG3 row C0/83A5 (-8996: the
  standard against 087). 16E's whole load: the port 6,034,257 cycles, the cartridge 6,034,256; its d 19 on both.
- $0BC9 bit 7 with bit 1 clear: C0/87F1 runs C0/8385 (C0/8365 on the half camera $1D14 / $1D16): +12 when its C0/7BA9 row
  ($1D16 - 1) is odd like the standard's; an even row is 460 less (priced 2026-10-29: C0/7BAD's BEQ taken goes straight
  into the 15-row loop at 7BB2, where the odd arm's BEQ not taken + BRL runs 7C45 -- the same prologue plus the first
  metatile's lower half, 7C56-7C7E, 418 -- and BRL 7BC3; traced on the Tyranno Lair's reload after Lavos's fall,
  ff24_handoff_f27 f1735, $5A = 04, every instruction one cost). That reload walked: 9,770,791 cycles against the
  cartridge's 9,787,229 (C0/0B5D -> C0/0B38 net of refresh), 28 black frames where the fallback's rounding held 29.
- C0/72B4 (palette classes 5 / 6) is C0/721C instruction for instruction with other bounds: its share scan costs C0/721C's
  (087's pass: the port 1,585,059, the cartridge 1,585,082).
- C0/E687's default arm (a graphics bank but 7F / D2-D4): C0/E6B6, the D4 arm's body reading $D5, 18 less (a taken BNE
  for the not-taken BNE + BRL).
- C0/CAD9 class 3: `CMP #02 / BEQ` not taken, `CLC / RTS`, 308 cycles, nothing drawn (01C's object $1A).
STILL REFUSED: class 2 (C0/D4F7), C0/7283 (a full share scan without a match), $0126 / $1DF9 / $1DFD / $0153 bit 7
($0BE9 and the channel-7 parity, which is $7F:0351, are priced since: sec. 13). Priced since (2026-10-21, src/field_cost.c):
- class 3's draw, C0/B701 -> C0/B785 `BRL C0/C73A`: 7,272 to C0/C7C9 on every run traced (01C's object $1A, seven frames),
  then the 24 template Y's by the screen Y $C5 / $C6 -- C0/C7DE (5,950 traced), C0/C7FB (the clamp to E0) or C0/C820 (24
  unrolled) -- and the OAM leg by the group `$0F80 & 0C`: C0/B6AA (9,932 traced), C0/B5CB (-72), C0/B653 (-6); the cursors
  $0181/5/9 += 6 and $01DB/D/F += $60.
- C0/7008, the VRAM slot pairs (a class 1-3 object, 1B0): the pairs $0C/$0D .. $16/$17 of $0B88 in order, the claim
  C0/6FCC's 618 with the second `STA $0B89,X` (+26).

### The attract against the cartridge, after sections 9-10 (2026-10-21)

tools/attract_divergence.py with the cartridge from power-on (ATTRACT_DIV_STATE=boot ATTRACT_DIV_CART_FRAMES=11500): every
visit's length is now the cartridge's to a frame (1F5 267/267, 16E 275/275, 01C 487/487, 0AD 858/858, 1F7 389/389, 07D
975/975, 1F2 322/322, 1B1 602/603, 087 831/832, 1F3 268/268, 1F0 266/266); HEAD had 1F5 267/271, 1F7 389/392, 1B1 602/609,
1F2 322/325, 1F3 268/271, 1F0 266/269. The same 25 picture events on both builds. Commit -> first ramp INC per field load
(the cartridge's `--monitor 7E0100:2,7E0119:1 --from boot --settle 0` against the port's CT_FIELD_MEMMON): equal on all but
four -- 112 +1, the 0A5 leg +1, one 1B1 reload -1, 087 +1 -- so the port runs 91 -> 87 frames ahead of the cartridge
across the demo (HEAD: 86 -> 61). The route (tools/playthrough.py cmp): 28/32 legs SAME (25 before); every world entry on it
equals the cartridge's stall (001 -> 1F0 41, 070 -> 1F1 40, 078 -> 1F1 40 twice).
LEFT: those four +-1s; class 3's draw C0/C73A (01C) and the VRAM slot arm C0/7008 (1B0), still on the old price; the
cold reset's load (`fromReset`) not graded against a cartridge reset here (its fixture state is not in this checkout).

## 11. The load's sound handshakes, against the cartridge's SPC (2026-10-21)

With the CPU work of sections 9-10 exact, the loads left a frame off (route: 001 +1, 008 -3, 070 -1, 078 -1) were all the
C7 handshake: the driver answers a command by where it stands in its own loop, so a post a few hundred APU cycles off can
take a different path (an echo missed in C7/0A6E's $400 polls, a re-post, a wait on the token until the driver's timer
routine writes port 0 at SPC $02DD). Measured with PEEK_APUTRACE (the oracle) against CT_AUDIO_PORTTRACE (the port) from the
same seed (a route leg's CT_AUDIO_SEEDSTATE), store by store on the SPC's cycle count:
- **The load's posts come before the frame's SPC slice.** The cartridge commits after a vblank and its load (NMI off, phase 0)
  reaches C0/1B73's `JSL C7/0004` 8,811 master cycles past it (5,751 to C0/0B86 -- the pad latch's 4,224, then the NMI-off
  stretch's start -- and 3,018-3,102 to the JSL); the port loads one iteration after the commit, so it posted after that
  iteration's whole slice, a frame late in SPC time. The iteration that loads now runs its slice after the load
  (CtAudioAdvance for the CPU work before the JSL, the slice owed to the next iteration). 070: the $81 at APU 487,669,848
  against the cartridge's 669,749; its refused $10 re-posted at 673,594 against 673,662.
- **The refused song's token spin is clocked on the loader too** (C7/0A85-0A8E, `DEC A / CMP $2140 / BEQ`): 070's 24 lines
  until $02DD writes the port.
- **From the world** (pendKind 3) the same stretch starts at C2's state 2: `STA $4200` = 00 at C2/246D (line 228 h 152, the
  pad latch's C2/84F9 poll), the $82 at C2/24F0 (C7:0004 on line 229 h 966 = 6,422 past the vblank), the $83 156 cycles past
  its RTL, and C0/1B73's JSL 3,868 past the $83's RTL (C2/2505 -> C0/0B5A -> C0/0B86). The port posted the pair at the field
  pad latch's position and loaded on the next iteration with a whole slice between them.
- **The song loader's head, instruction by instruction**: C7/0734 (the exception scan, 214 on the empty table), C7/0A39's
  record (238), C7/09FD + C7/0A12 (the echo-buffer page and the slot cull: 3,812 on 078's song, 6 slots passed and 26 bytes
  zeroed BYTE by byte) -- the port had a flat 340 for the last; C7/0A96's PLP / RTS and C0/01B1's `LDA $01 / STA $05` now
  charged after the token, where they run. The cull's empty-slot test is `$63,X` (the sample end's high byte), as the ROM's.
- **The world pass's own upload** (C2/2F0F's song, posted from inside the world's NMI): the NMIs it meets are C2/0178's skip
  (`$4C` bit 0 clear) at no beam cost, and the HDMA is `$420C = $4A` -- 0 there: the transfer's lines lose the DRAM refresh
  only. The port charged the field's mean HDMA, and leg 15's 4,935-unit upload ran 9,800 APU cycles long; the driver stood a
  step off the cartridge's for the rest of that world visit, and the castle's $83 / $81 echoed ~80 cycles sooner. After:
  the upload ends 44 cycles off, and the castle's $82 / $83 / $81 / $10 / re-post / first token within 5-33.

Results (tools/load_cost_cmp.py over the route, commit -> first ramp INC): 44 of 50 loads take the cartridge's frames (37
before) -- 001, 008, 070, 078, 081, 077 and leg 31's 1B7 now equal; left 0C6 +1 (after Yakra), 071 +2 (leg 29), 015 -1
(leg 30), 047 +1 (leg 31), and 083 / 084, which the port's walk does not reach (legs 24-25, DIFFERENT before this too). The
attract from power-on (`--boot`, its SPC the cartridge's own history): every load equal but 1201 +1 (the 0AD fight's exit);
HEAD's B101@7324 -1 and 8700 +1 are gone. The `--scene 433` vehicle started its SPC cold inside 1B1, so its driver's phase
was not the cartridge's: there 1C00 and 7D00 read +1 against the cartridge (01C's `10 FF` lands where its slot cull and
handshake fall differently) -- a vehicle artefact, the same handshakes equal on `--boot`. Since then (same day) a cold
`--scene 433` plays the power-on itself (docs/TESTING.md, "The attract vehicle"): it is `--boot` on a blank cart numbered
from that run's frame 27, and every attract load takes the cartridge's frames but 1201's.

## 12. Stage ends, the title's opening and the attract from power-on (2026-10-21)

- **An overlay machine's END is its own frame's** (src/field.c, beside the native stages' END): `$34C2` / `$34E3` set
  `$7E:0000` in the NMI that took the brightness to 0 and the main loop at `$7E:3026` -- free under the rule -- leaves for
  `$302A` and `JML C0:0000` before its next wait. The rule's cartridge on the attract's third credits: `$038D = 00` and the
  field's NMI vector on f4550, the location load's `10` at f4551. The port took the END at the next frame's head: 112's
  visit a frame long, and each credits' a frame late (three in the attract).
- **...and its load is that frame's** (STAGE_END_SPC_LEAD, $3177 skipped by `$0384` bit 5): the NMI's work to `7E:302A`
  4,252, 302A -> 303B 218, C0:0000 -> C0/0B86 -> the first JSL 4,008 (C0/0B86 on line 229 h 40, C7:0004 on 231 h 348).
- **The title's opening, priced** (`FF 02`, entry 2): one NMI-off stretch from C3:0014 (line 228 h 376) to `$7E:3019`'s
  NMI on -- C3:0014 -> the first `$377E` 5,950,182, the last `$377E`'s return -> `$3775`'s song JSL 5,669,358, the spin's
  end -> `$3019` 302,722; the reset's five sends, the song and the `$5AAA` spin on the loader clock's QUIET background (no
  NMI, no HDMA: the song 12,412,290 against the cartridge's 12,415,078, where the field background gave 13,308,887). The
  hold is the stretch's frames, floor((4,468 + W) / 357,368): the cartridge's 74 (f65 -> f139). The port held 43 (the
  song and the spin only): the title's swing and everything after it ran 31 frames early.

The attract from power-on (`--boot`, its SPC the cartridge's since boot_loader.md sec. 10), commit -> first ramp INC of
every load: on the cartridge's frames from B101 (f50) to B101 (f7324) -- 6E01, 1C00, 1B00, AD00, 1201, DB00, A500 --
then 7D00 +1 (the 07D load's refused `10`: the driver echoes it 175 APU cycles after the post on the cartridge, 3,073 in
the port, the `10` itself +72) and 8700 +1, both visits with a fight. The SPC's frame grid drifts ~0.01 APU cycle a frame
against the cartridge's (+65 by f6300): located in section 13.

## 13. The SPC's frame grid, the init scripts' posts and the DF / E1 reboot (2026-10-22)

Measured on the attract from power-on: the rule's cartridge (PEEK_APUTRACE, each frame's END) against the port
(CT_AUDIO_PORTTRACE, each slice's `FRAME n=<frame> apuc=.. carry=..`; the port's grid is apuc + carry).

- **The grid drift was an opcode's overshoot.** apuRun ends an opcode past what it is asked (LakeSnes's apu_cycle
  counts the opcode's cycles one by one, apuCatchupCycles carries the remainder: no drift). The frame slice and
  CtAudioAdvance carried the overshoot; the loader clock's catch-ups (ldCatch) did not -- a JSL, the title's $2143 spin
  and the driver's cold upload debited `clk x APU/master` from the frame's budget, not the cycles the SPC ran -- so
  every sound handshake left the SPC a few cycles ahead for good: +100 APU cycles over the attract's 7,616 frames
  (5-9 a load). The stretch now debits what its catch-ups ran (ld.ran): +3 cycles over the same frames.
- **A load's init-script posts sit where the ROM runs them.** Without the drift 0AD's load read +1: its two `10 18`
  (op EA, C0/3C41, from the objects' init) post on the cartridge 10 and 7 frames before the load's end (f3586 / f3589,
  main loop f3596), the port flushed them 4 and 2 frames before -- it counted only the object pass's work after each
  post (CtSceneLoadPostCycles), and the ROM runs C0/9DC3's arm, slot 1, C2/8004 and C0/2848's pass to C0/0B38 after the
  init runner too. The walk prices all of them (loadWalk.party / .pass, load9dc3): on a walked load they join the
  work after each post. The second post then finds the driver free (the cartridge's 56,196 APU cycles between them,
  the port's 51,265 had met it busy: three re-posts and a frame).
- **Ops DF / E1's reboot leads like a commit** (REBOOT_SPC_LEAD): C0/3B71's spin returns after the field NMI's pad
  latch, then C0/3B79-3B8F, C0/0B4E, `BRL C0:0000`, C0/0B5A and C0/0B86 on line 229 h 190 (5,646 past the vblank;
  the rule's cartridge, the attract's hub DF at f7324, --cputrace), C0/1B8B's JSL on 231 h 498. The port posted the
  load's `10 18` after the frame's slice: 75 APU cycles past the next frame's vblank against the cartridge's 491 past
  this one's, and 1B1's load a frame short (B101@7324 -1).

Result, the attract from power-on, commit -> first ramp INC: every load on the cartridge's frames but 8700 (+1). That
one is the 087 fight's exit upload (C0/1BE6's chain, f9912-9924): it starts 2 APU cycles from the cartridge's and
carries the same 8,200 port writes, but the battle NMI's waits split it at other points (the cartridge's two pauses of
69 and 47 lines, the port's three of 83, 47 and 70) and it ends ~4,000 APU cycles late, past the vblank. Not modelled.

## 14. The staff roll's and the ending's loads: op 29, `$0BE9`, `$7F:0351` and the E1 reboot (2026-10-22)

Three loads the walk refused (`pass: $0153 bit 7 / $0BE9 / $0351 bit 0`, and `op 29: its arms are not walked`) and charged
the old estimate, which crossed a frame the cartridge's stretch does not since 5fde1d2 put the load's song handshake on the
loader clock: 05C / 05D (the staff roll, field-caption-plane-staff-roll-vs-rule-cartridge, d = -1 / -2) and 059 (the
ending, ending-story-plays-through-in-the-port, 32 black frames for the cartridge's 31-frame equivalent). Beam-traced on
the rule's cartridge (`--cputrace` CPUTRACE_BEAM=1 CPUTRACE_FROM=C00B86; 05C from saves/field_re/s5b_roll_f2280.state, 05D
from its f538, 059 from saves/ending_re/ending_pre_ff2b.state's f5600) and summed as tools/load_cost_cmp.py does:

- **Op 29 (C0/3E84)**, the caption plane's setup, from the dispatch's JSR to its RTS: the head 264; C0/6E27's $0300-$0305
  234, the font `$FD:0000 -> $7F:5080` (the vector's JMP + C3/0557: 149,744 on the trace = 18 + ct_lz_rom_cycles), 242,
  C0/2DC8's $0400 bytes (8,624 traced, dma2DC8 8,628); 80; C0/75A0 94,536 (`MVN $7E,$D2` of $20 at 44 a byte, six doublings
  to $800 at 46); C0/3E99-3EB9 244 (the operand's BMI: 18 + 70 against 12 + 76, the same); C0/3EBB's scan of `$FD:A000` for
  record xx & 7F -- 52 a record passed, 72 a byte, 112 its end byte -- then 58 + 114. 05C (record 8, 296 bytes scanned):
  276,188 traced, the walk 276,192 (the DMA's sync).
- **`$0BE9`** is read once, at C0/0AF6 (an absolute-operand scan of banks C0 / C2 / FD; its writers are op 29's C0/3EA6 /
  3EB1 and C0/0C6D's clear): `LDA $0BE9 / BEQ` not taken and `STA $1D86` for the taken BEQ PASS_REST holds, +20.
- **`$0351 bit 0`** was the wrong byte: nothing in the pass reads `$7E:0351` (the wipe's); FD:C2C1's channel 7 reads
  `$7F:0351` (C439 / C477, DB = $7F; src/hdma_table.c). Its arm with C2C1's set: set B (the BNE not taken, 12) takes C477's
  BCC on an even parity (18), set A (taken, 18) leaves C439's BCS (12) -- 3,306 cycles both ways on 05C's pass and the main
  loop's next call, the configuration PASS_REST and C0/01A5's call were measured on; an odd parity is +6 on set A and -6 on
  set B (field_cost.c hdmaCh7Arm, both calls). `$0153 bit 7` (FD/C3F8's battle-exit arm) stays refused.
- **The port's `$1D35` was not the cartridge's.** C0/7F7E clears the $1D page and C0/A33B's A385-A4FB decodes the header
  into it (C0/A3AD `STZ $36`, C0/A45B `STA $35`: trace lines 3584 / 3669) BEFORE the object init, whose `29 88` then sets
  `TSB $1D36 / $1D35` (line 18662), and C0/286C reads `$1D35` = 04 (C0/0AE1; C0/87F1 skips its BG3 row). The port decoded the
  header and re-cleared the page after the init (CtSceneCameraEntry + CtSceneLayerScrollLoad at the load), so its pass saw
  00 and the walk priced the BG3 row (+8,996). Now the decode is fieldLoad's, before the init (CtWramLayerScrollDecode), and
  after it only the C0/286C entry runs (CtSceneLoadCameraEntry: the camera law and C0/74AC's window). The menu's reload
  (C0/01A5, field.c's mSave path) keeps the old order: its slot-1 run is the only script between; so does a `--loadstate`
  start's entry (a harness: the snapshot's page re-cleared as before -- field-defeat-holds-the-room-... is pinned on it).

Result, the cartridge's CPU work against the walk's (non-refresh, C7 apart): 05C 5,964,794 / 5,964,793 (the pass 1,109,430
/ 1,109,431), 05D 6,407,990 / 6,407,999, 059 11,051,750 / 11,051,655 (its pass -47: eight drawn objects' DMAs; the walk on
the cartridge's own 286C WRAM gives the same). 05C 17 black frames, 05D 18, 059 31 (resume f5652): both checks at their
original pins.

**The DF / E1 reboot's posts before the frame's slice** (field.c REBOOT_SPC_LEAD, g_dfRebootLoad -- sec. 13's mechanism; this is a second instance of it). What runs before C0/3B71's
spin is NMI-on work, free under the rule, so the stretch starts where the vblank's NMI returns: C0/EC5F's RTI on line 228 h
162 (4,254 from line 225: the pad latch), C0/3B76 `JSR C0/0B4E` -> C0:0000 -> C0/0B86 on line 229 h 200 (+1,402), C0/1B73's
`JSL C7:0004` on line 231 h 480 (+3,008): 8,664. (DF is `LDA #01 / STA $1E / BRA 3B4B`: the same path.) PEEK_APUTRACE against
CT_AUDIO_PORTTRACE: 05C's `81` 287,621,647 on the cartridge (f24's END 287,621,197), the port 287,638,328 after its whole
slice before, 287,621,664 now; the `10` 287,621,817 / 287,621,835. 05D's `81` 296,507,355 / 296,507,367, its `10`
296,507,575 on both.

## 15. The SPC's frame grid, and the DF / E1 reboot's posts (2026-10-22)

**The drift of sec. 12, located** (found apart from sec. 13, the same fix: sec. 13's ld.ran). The port's SPC runs a frame's slice per frame (CtAudioFrame: 17,088 APU cycles an even
frame, 17,087.81 an odd one, the last opcode's overshoot carried in `cycleCarry`) and, inside a clocked JSL (the loader
clock `ld`: a load's posts, the rule's posts, a spin), as far as the CPU's clock has run (ldCatch, snes_catchupApu's twin).
The next slices are then debited by the JSL's SPC time -- and that debit was the CLOCK's, `ld.clk x the rate`, not what
the SPC ran: ldCatch leaves the SPC up to an opcode past the clock (`ld.owed` < 0) or a fraction short, and the remainder
was dropped with the stretch. LakeSnes's catch-up keeps it (apuCatchupCycles): the cartridge's SPC stands at master x the
rate forever. MEASURED, CT_AUDIO_PORTTRACE's slice starts (`FRAME apuc`) against PEEK_APUTRACE's frame ENDs, the attract
from power-on (vehicle frame + 91 = the cartridge's): the grid +9 APU cycles off the cartridge's for 2,000 frames, then +16
after the posts of f2148-2150, +22 after 1B1's `10 18` upload (f2159-2190), ... +76 by f6000 -- 0.0166 cycle a frame, all
of it at JSLs (the cartridge's own frame: 17,087.904 cycles on both stretches).
At the 07D load (f6349) the location's `10 18` (C0/1B8B; refused, the song plays) reached the driver 86 cycles late. The
cartridge's driver read it in its idle poll ($0B98, 18 cycles after the post) and echoed it at $0D3E 168 cycles after; the
port's driver had just begun its timer tick (the 17 DSP writes of $02B0, then $02DD) and echoed 3,055 cycles after, the
token spin (C7/0A85-0A8E) ran to the tick after, and the NMI-off stretch crossed one more vblank: 49 black frames for 48.
The whole 07D visit then ran a frame late on the vehicle -- the iterations' grid, $0153's parity (FD/C2C1) and with it the
frames FD/E022 steps the tiles on.
FIX (src/audio.c, ld.ran -- sec. 13): the slices are debited by the cycles the SPC ran, the remainder carried as CtAudioFrame carries
a slice's. The grid stays within 3 cycles of the cartridge's through the whole demo; the 07D `10 18` lands 11 cycles after
the cartridge's (the idle poll, the echo +168 on both sides) and the load takes the cartridge's 48 frames.

**The DF / E1 reboot's load.** With the grid exact, the 07D -> 1B1 exit (f7324) came out a frame short (6 black frames
for 7): its load's `10 18` went to the driver after the frame's whole slice -- a frame of SPC late -- since an op DF / E1
reboot kept the plain order (sec. 11 measured C0/0C9A commits, the world's exit and the overlay END only). The cartridge
(--cputrace boot, CPUTRACE_BEAM): C0/3B71's spin leaves on the vblank after the visit (line 228 h 148, after the NMI's pad
latch), C0/3B76 `JSR C0/0B4E` (the NMI off), 3B79-3B95 store the location left and the staged one and `BRL C0:0000`,
C0/005D's C0/0B4E, C0/0B86 on line 229 h 186 = 5,642 master cycles past the vblank -- 102 sooner than a C0/0C9A commit's
(C0/0C9D-0CC4 store the facing and the $1801 / $1881 words first; 07D's load reaches C0/0B86 on 229 h 288) -- and C0/1B8B's
JSL on 231 h 440. The port defers that iteration's slice as it does the others' (field.c g_dfRebootLoad, sec. 13's REBOOT_SPC_LEAD (8,706),
sec. 14's staff roll 05C: 8,664; this exit's own measure, 5,642 + LOAD_SPC_LEAD's C0/0B86 -> JSL mean = 8,702, is
2 APU cycles from it -- the two traces were derived apart, the same mechanism): 1B1's load 7 frames, the cartridge's. (Op DF is `LDA #01 / STA $1E / BRA` into
E1's C0/3B4B: the same stretch.)

Results, the attract from power-on against the rule's cartridge (tools/attract_divergence.py with ATTRACT_DIV_STATE=boot
ATTRACT_DIV_CART_FRAMES=11500): every visit's length the cartridge's but 087's (831 / 832); every load's commit and
commit -> first ramp INC the cartridge's (HEAD: 7D00 +1, 1B1 602 / 603 after it). The Frog Squash's box at port f6820
against --peek boot f6912 (the vehicle's diagonal: picture P = peek P + 92): 12,320 / 12,320, the candles included.
- attract_cena_inicial.state is NOT this history. Its $051D (FD/E022's step count) has the other parity at the 07D load (4A
  against the power-on's F5), so its candles step on the other frames: its f4755 against --peek boot's f6912 is 12,297 /
  12,320 in that box, the 23 pixels of the left flame -- no port frame can give it the 12,320. Its SPC differs as well: the
  driver's $02DD that ends its 07D `10 18`'s token spin comes 4,759 APU cycles after the post (the power-on's 767): a
  49-frame load with a black frame between
  C0/286C's $0153 flip (f4238) and the first ramp INC (f4240), and its commit a frame earlier than the power-on's: the two
  cartridges agree again from the ramp (f4240 = power-on f6397). A check that grades a field animation's phase on the
  --scene 433 vehicle compares with --peek boot (attract-07d-frog-squash-colours-vs-oracle, re-targeted).
- LEFT: 087's visit +1, at its exit. C0/1C89's `70 00 88 00` (C7/08E3's transfer) crosses 13 NMIs on the port, 12 on the
  cartridge. Its `FE` is posted on the same APU cycle on both sides (169,355,849 / 850) and the driver's ticks fall on the
  same cycles (+-13), but the cartridge's tick is still running when the `FE` arrives (read in the tick's own poll, $0894,
  echoed 1,399 cycles later) where the port's has ended (echo +88): the battle song stands a sequence step apart -- the
  fight's own sound timing (the battle entry's upload, its posts), not a load's.

## 16. 087's exit, found: four sound timings the drivers' paths showed (2026-10-22)

Sec. 15's LEFT item, followed to its causes. The method: every driver port event of both sides from power-on (the
cartridge's PEEK_APUTRACE `SPC reads $F4 = .. at pc ..` / `SPC $F4 := ..`, the port's CT_AUDIO_SPC=ref
CT_AUDIO_PORTTRACE), aligned as sequences of (value, PC) with their cycles beside them. Where the two drivers first take
different paths -- the idle poll $0B98 on one side, a tick's poll $0894 or the timer routine $02DD on the other -- the CPU
side of that post is the defect. Four, in the order they appeared:

- **The NMI's wait before a post is wall time** (audio.c ldStartRule). A post the NMI leaves at a raster position -- the field
  pad latch's line 228, the battle NMI's line-64 cut (CD/09AA) -- started C7 after `ldRun(postAt - at)`: the clock walked to
  postAt as CPU work and charged the DRAM refresh and the HDMA it met (4,360 + 5,729 cycles on the battle's walk to line 64),
  so the 087 fight's `18 72` / `18 A3` / `18 75` / `18 76` / `18 FF` reached C7 on line 71, 474 APU cycles after the rule's
  cartridge's (C7:0004 on line 64 h 220, its `STA $2143` on h 874: --cputrace of f9494). The beam now walks there
  (ldWalk), its stolen time dropped: the posts within 10 cycles. The pad latch's posts lose the 160 they were charged.
- **C7's dispatch by the command's path** (disasm/notes/patches/c7_dispatch_preamble_listing.diff, the half not in since
  5fde1d2): the flat 686 was the song path's; a `$18-$2F` post is 552 (the cartridge's `18 72`: 654 from C7:0004 to its
  first store = 552 + the refresh + C7/061C's head), `>= $80` 480, and the epilogue C7/0192-01A0 304 (charged 296).
- **The boot's post on C1/0000's frame** (disasm/notes/patches/battle_boot_post_c1_0000.diff, now in: field.c
  fieldBattleBootPost, battleBootPostAt). The three demo fights' `70 01` -- the battle's sample set -- reached the driver
  34,172-34,180 APU cycles (two frames) before the cartridge's: the port posted on the consumer's frame, the cartridge's
  C0/1BAB runs from C1/0000 two frames on (the 087: `$0138` = 00 f9359, the records staged f9361, C0/1BDF's JSL f9362 line
  228 h 146). battleBootBandArm's `2 + apu + 3 + 1` already counted those frames: the band's arm stays, the post moves. Now
  the three entries' `FE` within 3 cycles, the first battle post (`18 08`) within 4. (Sec. 15 of battle_ai_interp.md read it
  as needing the dispatch's exact price with it: both are in.)
- **A title exit that runs the teardown** (field.c STAGE_TEARDOWN_SPC_LEAD): the attract's first title at f2149 posts
  $3177's five resets, then the reboot's load posts `81` / `10` -- 1,443 APU cycles after the last reset on the cartridge,
  after the frame's whole slice in the port (14,566 late): the first point where the two drivers parted, the world-5 song
  read in the timer routine instead of the idle poll. From the last reset's echo poll (`7E:37A5 RTS`, line 260 h 934) to
  C0/1B73's `JSL C7:0004` (line 1 h 990) is 4,148 cycles: the load's posts now follow the teardown by that, the frame's
  slice owed as for the other stage ENDs.
- **C0/0304's object init and slot 1 at ack + 2** (field.c fieldBattleResume stage 2): kept on the ack's frame while the
  SPC after `70 00` was not the cartridge's; run where the ROM runs them (C0/032D / C0/0330 after C0/0327's wait), the
  0AD's slot-1 `EA 18` posts at f4304 as the cartridge's -- it had been two frames early, the 0AD's return the next
  divergence.

Result, the attract from power-on (tools: the loads' commit -> first ramp INC against the rule's cartridge): every load on
the cartridge's frames, 8700 included -- the 087 exit's `FE` now finds the driver in the cartridge's state. Left, frame-free:
a walked load's init-script posts flush at a frame's granularity (0AD's `10 18` 8,000 cycles after the cartridge's, mid-frame
there), and the driver paths part again at f4944 / f5377 / f5410 without moving a frame of the demo.

## 17. The prison escape: an opened chest, the init posts inside their frame, a menu over 048 (2026-10-25)

The playthrough's leg 33 (tools/playthrough/route.txt, the escape from Guardia's prison: chests, cell levers, guards,
two menus) put ten loads and a menu reload against the rule's cartridge (tools/load_cost_cmp.py, the play clock `$0400`
and the field cursor `$01F8` it seeds at every load). Five gaps, each the ROM's:

- **An opened chest in the walk** (field_cost.c chestsA712). C0/A712's loop took the closed arm only and refused an opened
  one (`chests: an opened chest (C0/A7E6)`): every load into 048 after its Mid Tonic fell back to ct_lz_load_cost, a frame
  short. The opened arm is A7E1's BNE taken (+6), A785's BCC not taken (-6), then A787 `LDA $7E30C0,X / BIT #01 / BEQ`
  and on bit 0 the tile: FE -> FF, E0 -> E1, else untouched. MEASURED (CPUTRACE_BEAM, loc 048's load after the chest):
  the record's iteration 1,088 cycles past its refresh against the closed one's 926, the FE arm's 162 = 32 12 12 32 12 18
  12 32 (FastROM C0: 6 a fetch, WRAM 8); the bit-0-clear arm 62, E0 198, any other tile 142.
- **A load's init posts inside their frame** (field.c, the load's last JSLs; audio.c CtAudioAdvanceTo). Sec. 16's LEFT
  item: a walked load flushed its init runner's posts at a frame's granularity, after the frame's whole SPC slice. On the
  cartridge each JSL comes where its CPU reaches it: the vblank, C0/0B5D's NMI-off write 5,035 master cycles on
  (LOAD_NMIOFF_AT: LOAD_SPC_LEAD's 5,751 to C0/0B86 less 0B5D -> 0B86's 716), then the walk's work before it -- W less the
  work after it (the object pass's after the post, slot 1, C2/8004, C0/9DC3, the pass) -- on the wall clock. The SPC now
  runs to that cycle and the post goes there. MEASURED (048 -> 01C, the init `EA 16`'s `10 16` after C0/1B8B's): the
  cartridge posts at f1688 line 106 (APU 3,623,702,102), inside the driver's loop (SPC $0894), the echo 726 cycles later;
  the port posted at the frame's end (3,623,710,125), met the idle poll ($0B98) and had its echo in 168 -- 73,374 master
  cycles less on the loader clock, every 01C load a black frame short (d 36 / 37). Now 3,623,702,874 (the walk's W before
  the post 20,000 cycles over the cartridge's: not found) and all four 01C loads on the cartridge's frames.
- **obj0's slot 1 in the menu reload** (field_cost.c vm595C). The reload's slot-1 walk priced ops 12 and 16 and refused any
  other: 048's slot 1 copies the guards' tiles with E4 (C0/3D97 -> C0/AF4E), the handler the load's runners price
  (ct_vm_op_cycles). MEASURED (the reload's trace): `E4 3C 1A 3F 1C 28 0E 3B` C0/3D97 -> C0/597A 11,246 past nine refreshes
  against the formula's 11,298, `E4 3E 1D 3E 1D 26 0F 3B` 4,776 / 4,828 (the DMA sync's tens).
- **The menu's open: the frame's own tick first** (field.c, the X and Y paths). C2/85AE ticks `$0400` in the NMI at a
  frame's head; C0/1946's `$0407 = FF` is that frame's main loop. MEASURED (X in 048 at f9176): `$0400` 33 -> 34 at f9176
  with `$0119` = 0E, then still; the port wrote `$0407` before its tick and stood a frame behind through the menu.
- **C0/011B's save** (field.c, the menu's black frame). The routine also stores the party's position and $0C00 words to
  `$7F:1D09 / 1D0F / 1D15`, `$01AB-$01AD` to `$7F:1D1B-1D`, the words `$7F:3728 / 3748 / 3768 / 3781` to `$7F:1D1E-25` and the
  window wave `$1DF9` to `$7F:1D26` -- what C0/01FF puts back at the reload. MEASURED: `$7F1D26` 42 -> 00 by C0:01A4 at
  f9191; the port kept the stale 42, its reload walk read a live wave (`pass: $1DF9 / $1DFD`) and charged the estimate.
  (The page's MVN C0/0918 is not made: the port keeps the field's page live under the menu.)
