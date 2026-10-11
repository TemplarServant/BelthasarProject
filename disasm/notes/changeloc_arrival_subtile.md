# ChangeLocation ARRIVAL placement — the sub-tile low byte comes from `$0104`, not from op-0x8B

**Symptom reported (translated from Portuguese):** *"when moving from scene 113 to scene 114, I am getting stuck"* —
walking out of scene 113 the player gets STUCK.

**What is actually happening (measured):** scene 113's only exit goes to **scene 112**, never 114
(ROM exit record `@0x2508FC`, and the oracle's own `--followscene` prints `071 -> 070`). The player
does transition; what breaks is the **arrival position in scene 112**: our engine drops the lead at
`(488, 224)` where the oracle drops it at `(488, 215.9375)` — **8 px too low**. At `y=224` the
player's collision box straddles map-99 row 14, which is solid, and the player is then **refused in
all four directions, zero pixels, silently**. That is the "stuck".

The cause is general, not scene-113: our host converts the arrival TILE to pixels with a fixed
`(+8, +16)`, while the ROM picks the low byte of each 16-bit coordinate from **two flag bits of
`$0104`**. **163 of the ROM's 814 exit records** carry the Y bit and **315** carry the X bit, so
every one of those doors currently lands 8 px off.

---

## 1. The canonical mechanism — C0/42B8-430F (DP = `$0100`)

The lead PC's loader (the `$7E2980` slot-0 arm of the PC-load routine, entered at `C0/429C`)
is what places the player after a location load:

```
42B8  LDX  $6D              ; X = the lead PC's object X-index
42BA  LDA  $02              ; $0102 = arrival tile X
42BC  STA  $1801,X          ; -> the HIGH byte of the 16-bit X position
42BF  LDA  $03              ; $0103 = arrival tile Y
42C1  STA  $1881,X          ; -> the HIGH byte of the 16-bit Y position
...
42E1  LDA  $04              ; $0104 = the arrival FLAGS byte
42E3  AND  #03
42E5  STA  $1600,X          ; bits 0-1 = FACING            <-- the only bits we ported
42E8  LDA  $04
42EA  BIT  #08
42EC  BNE  ->42F5
42EE  LDA  #FF
42F0  STA  $1880,X          ; bit3 CLEAR -> Y low byte = $FF
42F3  BRA  ->42FA
42F5  LDA  #7F
42F7  STA  $1880,X          ; bit3 SET   -> Y low byte = $7F
42FA  LDA  $04
42FC  BIT  #04
42FE  BNE  ->4308
4300  LDA  #80
4302  STA  $1800,X          ; bit2 CLEAR -> X low byte = $80
4305  TYX / SEC / RTS
4308  LDA  #00
430A  STA  $1800,X          ; bit2 SET   -> X low byte = $00
430D  TYX / SEC / RTS
```

The 16-bit position is `px * 16` (4 fractional bits), so:

| `$0104` bit | ROM store | raw | pixels |
|---|---|---|---|
| bit2 = 0 | `$1800,X = $80` | `tileX*256 + 0x80` | `tileX*16 + 8` |
| bit2 = 1 | `$1800,X = $00` | `tileX*256 + 0x00` | `tileX*16 + 0` |
| bit3 = 0 | `$1880,X = $FF` | `tileY*256 + 0xFF` | `tileY*16 + 15.9375` |
| bit3 = 1 | `$1880,X = $7F` | `tileY*256 + 0x7F` | `tileY*16 + 7.9375` |
| bits 0-1 | `$1600,X` | — | facing |

`$0104` is **the same byte for all three staging paths**, built by the same three instructions
(`LSR / AND #0F / STA`, then `AND #80 / TSB`):

* **positional map exit** — `C0/9CF5-9D29` writes `$0116` (already ported, `field.c fieldStageExit`)
* **op-0xE0 deferred ChangeLocation** — `C0/3AD4-3ADF` writes `$0116`
* **op-0xDD/DE/DF/E1 immediate ChangeLocation** — `C0/3B34-3B3F` writes `$0104` directly

and the commit `C0/0CBC LDA $16 / 0CBE STA $04` copies `$0116 -> $0104`
(ported, `ct_vm.c:413 CtWramCommitLocation`).

### Where the exit records live
`0x250000` = 512 u16 local pointers, then 7-byte records; `hi = rom[rec+4]`, and
`$0116 = ((hi >> 1) & 0x0F) | (hi & 0x80)`. Our `fieldStageExit` already reproduces this
byte-exactly (verified: `CT_EXITDBG=1` prints `$0116=0A` for scene 113, and the oracle's
`$0116` reads `0A` at the same moment).

---

## 2. Oracle observations — all four arms measured

Probe binary: `cp build/ct.exe "$TMP/ctwf/exit/ct_probe.exe"`; all commands from the repo root.

### Anchor state
```
PEEK_SAVE=1 <probe> --teleport 113 8 10 0 --from saves/jogo_trava_lake.state \
  --frames 120 --out "$TMP/ctwf/exit/orc113.state"
# -> now 071  scene=LOADED player=PLACED  leadPC obj=2 pos=(136,175) storyline=0C
```
`$0197 = 04` (lead X-index -> obj2), `$1804 = 0x0880` (136.0), `$1884 = 0x0AFF` (175.9375).

### A. bit3 SET, bit2 clear — scene 113 -> 112 (`$0104 = 0x0A`)
```
<probe> --followscene --from "$TMP/ctwf/exit/orc113.state" --input "40:400" --frames 420
  f0     loc=071 (scene 113) cmd=3 arrival=(8,10) facing=0
  f137   TRANSITION 071 -> 070 (scene 112) arrival=(30,13) facing=10 cmd=3

<probe> --monitor "0197:1,1802:2,1882:2" --from "$TMP/ctwf/exit/orc113.state" \
        --input "40:400" --writer --diff --frames 200
  f156   001882=7F0D*(C0:42FA)   001802=801E*(C0:4305)
```
`$0197 = 02` -> lead = obj1. **`$1882 = 0x0D7F` = 215.9375 px** (tile 13 -> `13*16 + 7.9375`),
`$1802 = 0x1E80` = 488.0 px (tile 30 -> `30*16 + 8`). The writer PCs `C0:42FA` / `C0:4305` are the
instructions AFTER the `$7F` store at `42F7` and the `$80` store at `4302` — i.e. exactly the two
arms above.

Staged bytes at `$0112..$0119` during the drain (`--monitor "0112:8"`):
`70 00 1E 0D 0A 00 00 0F` -> `$0112=$70` (loc 112), `$0114=$1E`, `$0115=$0D`, **`$0116=$0A`**,
`$0117` bit7 latched at f121, `$0119` counts `0F..00`, loc flips at f137.
`0x0A` = bit3 set, bit2 clear, facing 2. Matches the two stores exactly.

### B. bit3 clear, bit2 clear — the teleport load (`$0104 = 0x00`)
```
<probe> --teleport 113 8 10 0 --from saves/jogo_trava_lake.state --frames 60 --out tp0.state
<probe> --read "0197,0104,1804+2,1884+2" --from tp0.state
  0104 = 00   1804 = 8008 (0x0880)   1884 = FF0A (0x0AFF)
```
X low `$80`, Y low `$FF`. (`--teleport` masks its facing argument with `& 3`
— `src/main.c:11083` — so it cannot drive bits 2/3; that is why case C below uses a real door.)

### C. bit2 SET, bit3 clear — scene 4 -> 3 (`$0104 = 0x04`)
ROM record `scene 4 rec1 @0x25044F` = `07 0a 01 03 08 08 0c` -> dest 3, tile (8,12),
`hi = 0x08` -> `$0116 = 0x04`.
```
<probe> --teleport 4 7 12 0 --from saves/jogo_trava_lake.state --frames 90 --out sc4.state
   -> leadPC obj=1 pos=(120,207)
<probe> --followscene --from sc4.state --input "10:300" --frames 320
   f54  TRANSITION 004 -> 003 (scene 3) arrival=(8,12) facing=4
<probe> --monitor "0197:1,1802:2,1882:2,0104:1,0100:1" --from sc4.state --input "10:300" --diff
   f73  000197=02  001802=0008  001882=FF0C  000104=04  000100=03
```
**`$1802 = 0x0800` = 128.0 px = `8*16 + 0`** (bit2 arm), `$1882 = 0x0CFF` = 207.9375 px
(`12*16 + 15.9375`, bit3-clear arm).

### D. bit2 SET + bit3 SET
Not observed live in this pass. ROM-derived only (the two `BIT`s are independent); 11 exit
records in the table carry both bits. See Open questions.

### Census over the whole exit table
```
python -c "...scan 0x250000, 512 pointers, 7-byte records..."
total exit records scanned: 814
bit3(Ylow 7F)=False bit2(Xlow 00)=False : 347
bit3(Ylow 7F)=False bit2(Xlow 00)=True  : 304
bit3(Ylow 7F)=True  bit2(Xlow 00)=False : 152
bit3(Ylow 7F)=True  bit2(Xlow 00)=True  :  11
```

---

## 3. Our engine — the defect, and the freeze it causes

### The site
`src/field.c:8449` (inside the `CtScenePollLocChange` consumer):
```c
pendX = lx * 16 + 8; pendY = ly * 16 + 16; pendFac = lf;    // tile -> pixel (op 0x8B convention)
```
The comment names the bug: it is the **op-0x8B SETCOORD** convention (`ct_vm.c:1311`,
`x = tile*16+8`, `y = tile*16+16`), which is a *different* ROM routine. The location-load
arrival is `C0/42B8-430F` and is flag-driven.

`lst`/`pendFac` cannot repair it either: `ct_vm.c:3652` throws bits 2-7 away —
```c
sc->lcFacing = r8(sc->mem, 0x7E0104) & 3;
```
(same at `ct_vm.c:2555` and `2571`), so **bits 2 and 3 of `$0104` never leave the VM**.
`lcStyle` is unrelated (it is the op-0xDE/0xDF `$1E` skip-fade flag).

`pendX`/`pendY` are declared `int` at `src/field.c:6232`, so even the correct value's
sub-pixel (`.9375` / `.0625`) cannot survive; they are consumed at `src/field.c:6470-6471`
into the `double px/py`.

### The reproduction (our engine)
Storyline matters: a bare `--scene 113` runs at storyline 0 and a different cast/script
takes over. Seed the oracle's own `$7F0000` page:
```
<probe> --peek "$TMP/ctwf/exit/orc113.state" "$TMP/ctwf/exit/p113.raw" --btnscript "0:1"
python -c "w=open(r'...p113.raw.wram','rb').read(); open(r'...flags113.bin','wb').write(w[0x10000:0x10200])"
# -> $7F0000 = 0C  (storyline 12), $0155 = 01
```
```
CT_EXITDBG=1 ./build/ct_field.exe --assets ct_assets.dat --scene 113 \
  --loadflags "$TMP/ctwf/exit/flags113.bin" --at 136 176 \
  --selftest out.raw --frames 800 --walkscript "l:780,."
exitstage: scene 113 exit 0 @2508FC -> $0112=0070 $0114=1E $0115=0D $0116=0A
changeloc: scene 113 -> 112 at f120 tile(30,13) fac2 style0
selftest: scene 112 ... player (488,224) ...          <-- 660 frames of LEFT, zero movement
```
`CT_DRIVEDBG=1` at the same run:
```
f121 gate=1 ask=(-1,0) pObj=1 pos=(488,224) z=1 | ctlLocked=0 explore=1 leadBusy=0 tb=0 ...
     foot(30,13) shp=0A zb=01 -> step(29,13) shp=00 zb=C1 mv=00 walkable=1
f179 gate=1 ask=(-1,0) pObj=1 pos=(488,224) ...      <-- identical for the rest of the run
```
The movement GATE is open (`gate=1`, `explore=1`, `ctlLocked=0`), the facing even updates —
it is the player tile-collision (the `C0/8A6D` port) that refuses.

All four directions, zero pixels:
```
for d in l r u d: ... --walkscript "l:125,$d:170,."   ->  player (488,224) in every case
```

### Why 224 freezes and 216 walks
Map 99 collision (`ct_assets.dat` type 11 id 99, 32x64, 3 planes), columns 26..31:
```
row12 shp 00 01 00 05 00 00 | zb 00 C1 C9 C1 01 01
row13 shp 00 00 00 00 0A 19 | zb 00 C1 C1 C1 01 08     <- (30,13) shp=0A  = the arrival tile
row14 shp 00 04 00 00 01 01 | zb 00 C8 C1 81 01 01     <- (30,14) shp=01  = SOLID
```
The `C0/8A6D` handlers build the tentative point as `base ± feeler($70 = 112 = 7 px) + velocity`
(`disasm/notes/collision.md` §2). At `py = 224` the downward feeler reaches `y = 231` -> row 14,
solid -> every arm blocks. At the canonical `py = 215.9375` it reaches `222.9` -> row 13, free.

Placed by hand at the oracle's Y, our engine matches the oracle's walk:
```
./build/ct_field.exe --assets ct_assets.dat --scene 112 --loadflags flags113.bin --at 488 216 \
   --selftest out.raw --frames 100 --walkscript "l:95,."
   -> f0 (488,216) ... 1 px/frame ... stops at x=439
oracle  --monitor "1802:2" --from orc113.state --input "40:400" --frames 420
   -> f156 0x1E80 (488) ... 1 px/frame ... last change f231 0x1B70 (439)
```
**Same start, same speed, same stop tile.**

### Second, independent case (proves it is not scene 113)
```
CT_EXITDBG=1 ./build/ct_field.exe --assets ct_assets.dat --scene 4 \
  --loadflags flags113.bin --at 120 207 --selftest out.raw --frames 200 --walkscript "u:190,."
exitstage: scene 4 exit 1 @25044F -> $0112=0003 $0114=08 $0115=0C $0116=04
changeloc: scene 4 -> 3 at f36 tile(8,12) fac0 style0     -> our px = 8*16+8 = 136
oracle (case C above)                                      -> $1802 = 0x0800 = 128.0
```
8 px off on X this time, from the *other* flag bit.

---

## 4. The minimal, mandate-safe fix

Two edits, both a straight port of `C0/42B8-430F`; **no scene gate, no heuristic, no constant
that is not in the ROM**:

1. **Carry the whole `$0104` byte out of the VM.** Add `int lcArrive;` next to
   `lcFacing` (`src/ct_vm.c:1089`) and set it to the raw byte at all three fill sites
   (`ct_vm.c:2555`, `2571`, `3652` — the last one is `r8(sc->mem, 0x7E0104)` unmasked; the
   first two are `B[addr+2]` run through the same `((hi>>1)&0x0F) | (hi&0x80)` the ROM uses at
   `C0/3B34-3B3F`). Expose it through `CtScenePollLocChange` (a new out-param, or reuse the
   already-plumbed `style` slot only if nothing else reads it). Leave `lcFacing` alone — its
   `& 3` is `C0/42E3` and is correct.

2. **Apply the ROM's low-byte rule at `src/field.c:8449`**, and widen `pendX/pendY`
   (`src/field.c:6232`) to `double` so the sub-pixel survives into `px/py`:
   ```c
   // C0/42BC-430F: the arrival TILE is the HIGH byte of the 16-bit position; the LOW byte is
   // picked from $0104 -- X: bit2 ? $00 : $80, Y: bit3 ? $7F : $ FF.
   pendX = (lx * 256 + ((la & 0x04) ? 0x00 : 0x80)) / 16.0;
   pendY = (ly * 256 + ((la & 0x08) ? 0x7F : 0xFF)) / 16.0;
   pendFac = lf;
   ```
   The `-1` sentinel test at `src/field.c:6470` (`pendX >= 0`) still works.

The same rule belongs at `src/field.c:8469` (the `CT_CHANGELOC` test hook) and
`src/field.c:6693` (the hard-coded new-game loc-271 hop) for consistency; both are literal
tile constants today.

**Do NOT** touch `f.spawnX/spawnY` (`src/field.c:2256/2263`) or op-0x8B (`ct_vm.c:858/1311`) —
those are different ROM routines with their own (correct) `+8/+16` convention.

---

## 5. Open questions

* **bit2 + bit3 both set** (11 exit records) is ROM-derived from two independent `BIT` tests but
  was **not observed live**. Worth one measurement before pinning a check.
* **The follower PCs.** `C0/429C` is only the slot-0 (lead) arm; slot 1 branches to `C0/4310`
  and slot 2 to `C0/43C3`. Whether those also read `$0104`'s bits was not derived here.
* **The overworld -> scene entry** (`pendKind == 3`, `src/field.c:6817`) uses a *third*
  convention, `destX*16+8 / destY*16+15`. That path is the C2 loader, not `C0/42B8`; it was not
  derived in this pass and may carry the same class of defect.
* **`pz` on arrival.** `src/field.c:6471-6473` sets `pz = 1` then re-derives it from `f.C.zb`
  at the arrival tile. The ROM sets `$0155 = 1` unconditionally at `C0/0C4C` (`LDA #01 / STA $55`)
  and lets `C0/9C5C` update it while walking. The oracle reads `$0155 = 01` in both scenes here,
  so nothing diverged in this scenario — but the re-derivation is not the ROM's rule.
* **Exit trigger frame.** Our exit fires at f120, the oracle latches `$0117` bit7 at f121
  (positions differ by ~1 px). Not investigated; unrelated to the freeze.
* **The user said "114".** No exit anywhere in the ROM's table goes from 113 to 114 (scene 114's
  only exit is *to* world 1). Either the destination number was misremembered, or the user saw
  the freeze after a different hop; the freeze reproduced here is 113 -> 112 and it is
  positional, so any door in the table whose `$0116` carries bit2/bit3 will show it.

---

## 6. Exact repro command lines (copy-paste, from the repo root)

```bash
mkdir -p "$TMP/ctwf/exit"
cp build/ct.exe "$TMP/ctwf/exit/ct_probe.exe"
P="$TMP/ctwf/exit/ct_probe.exe"; S="$TMP/ctwf/exit"

# oracle anchor
PEEK_SAVE=1 "$P" --teleport 113 8 10 0 --from saves/jogo_trava_lake.state --frames 120 --out "$S/orc113.state"
"$P" --peek "$S/orc113.state" "$S/p113.raw" --btnscript "0:1"
python -c "w=open(r'$S/p113.raw.wram'.replace('/','\\\\'),'rb').read();open(r'$S/flags113.bin'.replace('/','\\\\'),'wb').write(w[0x10000:0x10200])"

# oracle: transitions and walks
"$P" --followscene --from "$S/orc113.state" --input "40:400" --frames 420
"$P" --monitor "0197:1,1802:2,1882:2,0104:1,0112:8" --from "$S/orc113.state" --input "40:400" --writer --diff --frames 260

# ours: transitions, then FROZEN
CT_EXITDBG=1 CT_DRIVEDBG=1 ./build/ct_field.exe --assets ct_assets.dat --scene 113 \
  --loadflags "$S/flags113.bin" --at 136 176 --selftest "$S/o.raw" --frames 400 --walkscript "l:390,."

# ours at the ORACLE's arrival Y -> walks, and stops on the oracle's own tile
./build/ct_field.exe --assets ct_assets.dat --scene 112 --loadflags "$S/flags113.bin" \
  --at 488 216 --selftest "$S/o2.raw" --frames 100 --walkscript "l:95,."

# the bit2 arm (scene 4 -> 3)
"$P" --teleport 4 7 12 0 --from saves/jogo_trava_lake.state --frames 90 --out "$S/sc4.state"
"$P" --monitor "0197:1,1802:2,1882:2,0104:1,0100:1" --from "$S/sc4.state" --input "10:300" --diff --frames 160
CT_EXITDBG=1 ./build/ct_field.exe --assets ct_assets.dat --scene 4 --loadflags "$S/flags113.bin" \
  --at 120 207 --selftest "$S/o3.raw" --frames 200 --walkscript "u:190,."

# ROM
python tools/dis.py 42B8 0x58      # the arrival placement
python tools/dis.py 3B1B 0x30      # op-0xDD  -> $0104
python tools/dis.py 3AB7 0x40      # op-0xE0  -> $0116
python tools/dis.py 9CF5 0x38      # positional exit -> $0116
python tools/dis.py 0C9A 0x28      # the commit $0116 -> $0104
```

---

# Adversarial review (second agent, read-only, 2026-08-22)

Verdict: **the core derivation SURVIVES — and is now better supported than the dossier made it.**
Every cited ROM address was re-disassembled and every cited oracle value re-measured from a fresh
probe (`$TMP/ctwf/exit-adv/ct_probe.exe`). Two of the dossier's supporting arguments do NOT
survive, one open question is now CLOSED by measurement, and the proposed fix has three concrete
defects.

## A. Re-verified: every ROM citation is byte-correct

`python tools/dis.py 42B8 0x60` reproduces the section-1 listing instruction for instruction,
including both `BIT` arms and the `TYX / SEC / RTS` tails at `4305` and `430D`. Also
re-disassembled and confirmed verbatim: `C0/9CF5-9D29` (`LSR / AND #0F / STA $0116`,
`AND #80 / TSB $0116`), `C0/3AD4-3ADF` (into `$16`), `C0/3B34-3B3F` (into `$04`),
`C0/0CB4-0CBE` (`LDX $12 / STX $00`, `LDX $14 / STX $02`, `LDA $16 / STA $04`).

Justification the dossier omitted for the exit-record layout: the field offsets are not a guess —
`C0/9D0F` reads `$E50003,X` (dest), `C0/9CF5` reads `$E50004,X` (the flags byte), `C0/9D16` reads
`$E50005,X` (tile X), `C0/9D1D` reads `$E50006,X` (tile Y). Bank `$E5` of a HiROM = file `0x250000`.

`C0/42B8` is reached only from `C0/429C`, which is the fallthrough of the `$7E2980` compare at
`C0/426A-4274`. So it is unambiguously the **lead PC (party slot 0)** arm; slots 1/2 branch to
`C0/4310` / `C0/43C3`, and the not-in-party arm is `C0/428C`.

## B. Re-verified: every oracle value, from a fresh state

    PEEK_SAVE=1 <probe> --teleport 113 8 10 0 --from saves/jogo_trava_lake.state --frames 120 --out orc113.state
    <probe> --monitor "0197:1,1802:2,1882:2,0104:1,0112:8,0117:1,0155:1" --from orc113.state \
            --input "40:400" --writer --diff --frames 175
      f121   000112=70001E0D0A80000F*(C0:9D2C)  000117=80*(C0:9D2C)   <- the positional exit stages
      f137   000197=80*(C0:0C22)  000104=0A*(C0:0CC0)  000100=..*(C0:0CB8)  <- the commit $0116 -> $0104
      f156   000197=02*(C0:42B5)  001802=801E*(C0:4305)  001882=7F0D*(C0:42FA)

`$1802 = 0x1E80` = 488.0, `$1882 = 0x0D7F` = **215.9375**. The writer PCs `C0:4305` and `C0:42FA`
are the instructions after the `$80` store at `4302` and the `$7F` store at `42F7`. Confirmed.
Exit record `@0x2508FC = 00 07 83 70 14 1E 0D` -> `hi = 0x14` -> `$0116 = 0x0A`. Confirmed.

Census re-run independently: **814 records; 347 / 304 / 152 / 11; bit3 set = 163, bit2 set = 315.**
Identical to the dossier's table. Confirmed.

## C. CLOSED: open question 1 (bit2 AND bit3 both set) is now MEASURED

The dossier left case D "ROM-derived only". It is now observed live. The reverse of case C is one
of the 11 both-bits records: `scene 3 rec0 @0x250425 = 07 0E 01 04 1A 08 0C` -> dest 4, tile (8,12),
`hi = 0x1A`, `$0116 = 0x0D`.

    <probe> --teleport 4 7 12 0 --from saves/jogo_trava_lake.state --frames 90 --out sc4.state
    <probe> --followscene --from sc4.state --input "10:60,00:40,20:250" --frames 360
      f54   TRANSITION 004 -> 003  arrival=(8,12) facing=4
      f133  TRANSITION 003 -> 004  arrival=(8,12) facing=13
    <probe> --monitor "0197:1,1802:2,1882:2,0104:1,0100:1" --from sc4.state \
            --input "10:60,00:40,20:250" --writer --diff --frames 360
      f73    001802=0008*(C0:430D)  001882=FF0C*(C0:42F3)  000104=04    <- bit2 SET, bit3 clear
      f154   001802=0008 (C0:430D)  001882=7F0C*(C0:42FA)  000104=0D    <- bit2 SET, bit3 SET

With `$0104 = 0x0D`: `$1802 = 0x0800` = **128.0** (`8*16 + 0`) and `$1882 = 0x0C7F` = **199.9375**
(`12*16 + 7.9375`). All four arms of `C0/42E8-430F` are now measured; the two `BIT`s are indeed
independent.

## D. REFUTED — "Why 224 freezes and 216 walks" is a rationalization, not a derivation

The dossier explains the freeze as `base + feeler ($70 = 7 px)` reaching `y = 231` -> map-99 row 14.
That arithmetic predicts blocking for every `y >= 217`. Measured, it is not what our engine does:

    for y in 215 216 217 218 219 220 221 222 223 224; do
      ./build/ct_field.exe --assets ct_assets.dat --scene 112 --loadflags flags113.bin \
         --at 488 $y --selftest t.raw --frames 40 --walkscript "l:35,."
    done
      y=215 -> (453,216)   y=216 -> (453,216)   y=217 -> (453,217)   y=218 -> (453,218)
      y=219 -> (453,219)   y=220 -> (453,220)
      y=221 -> (488,221)   y=222 -> (488,222)   y=223 -> (488,223)   y=224 -> (488,224)

The threshold is **y >= 221**, not y >= 217. (`221 + 3 = 224`, the first pixel of row 14, fits; a
7-px feeler does not.) The freeze is real and position-dependent, but the stated *mechanism of the
freeze* is not derived. Treat that subsection as HYPOTHESIS.

Related mislabelled evidence: the dossier offers
`f121 ... foot(30,13) shp=0A zb=01 -> step(29,13) shp=00 zb=C1 mv=00 walkable=1`
as proof that "collision refuses". That line says **walkable=1** — it is a coarse pre-probe, not
the gate that refuses. The conclusion holds, but not from this citation.

Also note `y = 223` (the canonical bit3-CLEAR value, `tile*16 + 15.9375`) is **also frozen**. So the
`+16` vs `+15.9375` rounding is NOT what unsticks scene 112 — only the bit3 arm is. That closes off
the obvious "it is just a rounding bug" competing explanation.

## E. NEW EVIDENCE the dossier never ran — the control that actually settles it

Place the **oracle itself** at the same tile with our engine's low byte (`--teleport` leaves
`$0104 = 0`, so Y low = `$FF` -> y = 223.9375) and hold LEFT for 200 frames:

    <probe> --teleport 112 30 13 0 --from saves/jogo_trava_lake.state --frames 90 --out o112.state
       -> leadPC obj=1 pos=(488,223)
    <probe> --monitor "1802:2,1882:2" --from o112.state --input "40:200" --diff --frames 200
       f0  001802=801E*  001882=FF0D*      <- and NOTHING else. 200 frames, zero movement.

Controls, same state family, same input:

| start | oracle | ours (`--at`) |
|---|---|---|
| tile (30,13), y=223.9375, x=488 | **FROZEN**, 0 rows in 200 f | **FROZEN** at (488,224) |
| tile (29,13), y=223.9375, x=472 | walks -> x=439 (`$1802=0x1B70`) | walks -> (439,224) |
| tile (30,12), y=207.9375, x=488 | walks -> x=439, Y snaps to `$1882=0x7F0D` (215.9375) | walks -> (439,216) |
| the real door arrival, y=215.9375 | walks -> x=439 | walks -> (439,216) |

**The ROM itself is stuck at (488, 223.9375).** So (a) our collision is NOT over-blocking — it
matches the oracle at all four probes, including the Y snap to 215.9375 during the (30,12) walk;
and (b) the only divergence in this scenario is the arrival low byte. This is a stronger proof of
the dossier's thesis than anything in the dossier, and it kills the competing hypothesis
"our `C0/8A6D` port is 8 px too fat".

## F. The bug site is correctly identified — but the "different cause" list is incomplete

Verified in the tree: `src/field.c:8449`, `:6232`, `:6470-6471`, `:6483`, `:6693`, `:6817`, `:8469`;
`src/ct_vm.c:1089`, `:2551`, `:2555`, `:2571`, `:3640`, `:3652`, `:4732-4736`, `:413`.
`CT_EXITDBG=1` reproduces `$0116=0A` and `changeloc: scene 113 -> 112 at f120 tile(30,13)`, and the
390-frame LEFT run ends at `player (488,224)`. Reproduced exactly.

Two things the dossier did not weigh:

1. **The `+16` is wrong even on the bit3-CLEAR arm.** The ROM's clear arm is `$FF` =
   `tile*16 + 15.9375`; ours is `tile*16 + 16.0`. That is 1/16 px, but it crosses a TILE boundary
   under integer division, and `src/field.c:6474` feeds
   `CtSceneCameraEntry(f.vm, (int)px / 16, (int)py / 16)` — today `224/16 = 14`, i.e. the camera is
   seeded from row **14** when the ROM's arrival tile is **13**. The fix silently repairs that for
   all 651 bit3-clear records. Blast radius the dossier does not quantify.
2. **`src/field.c:6817` (overworld -> scene) uses `destY * 16 + 15`.** That constant is in neither
   arm of `C0/42E8-42F7` (`$FF` = 15.9375, `$7F` = 7.9375) and is cited to no ROM address anywhere
   in this repo. Whether the C2 entry path also ends in `C0/42B8` was not derived (the dossier's
   open question 3). Until it is, the fix as scoped leaves one of the two arrival paths on an
   underived constant — a partial, not a structural, repair.

## G. Three defects in the proposed fix

1. **`src/field.c:6693` must be dropped from the change.** It is the synthetic new-game loc-271
   hop (tile 31,31, facing 1) — a host-side shortcut with no `$0104` behind it. Applying the flag
   rule there requires *choosing* an `la`, which is an invented constant. Leave it alone.
2. **Widening `pendX`/`pendY` to `double` breaks `src/field.c:6483`:**
   `printf("worldtest: ENTERED location %03X at tile (%d,%d)\n", pendDest, pendX / 16, (pendY - 15) / 16);`
   would pass two doubles through `%d` (undefined behaviour, garbled line). The same edit must add
   explicit `(int)` casts there. `pendX = -1` at `:6423` and `pendX >= 0` at `:6470` survive the
   widening.
3. **A simpler, more canonical plumbing exists — no new struct field, no ABI change.**
   `CtWramCommitLocation` (`src/ct_vm.c:413`) already writes `$7E0104 = $7E0116`, and the op-0xE0
   arm (`src/ct_vm.c:2551`) already writes the full flags byte to `$7E0116`. The ONE hole is the
   immediate arm (`src/ct_vm.c:2565-2577`, ops 0xDD/DE/DF/E1): the ROM's `C0/3B39 STA $04` /
   `C0/3B3F TSB $04` puts the byte in RAM and our port does not. Port that missing store —
   `w8(sc->mem, 0x7E0104, ((B[addr + 2] >> 1) & 0x0F) | (B[addr + 2] & 0x80));` — and then
   `src/field.c:8449` can read `$7E0104` out of WRAM at the consume site. That is strictly closer
   to the ROM (which keeps the byte in `$0104`, not in a C mirror) and avoids touching
   `CtScenePollLocChange`'s signature (`src/ct_vm.c:4729`, `src/ct_vm.h:318`). Either plumbing is
   mandate-safe; this one is smaller and adds a store the port is currently missing.

## H. Regression risk (`tools/regress_checks.json` read; suite NOT run)

- **`field-newgame-kitchen-exit-no-replay`** (tier full) — THE ONE AT REAL RISK. Its
  `changeloc: scene 2 -> 1` goes through `field.c:8449`, and scene 2's only exit record
  `@0x25041E = 17 0D 81 01 16 0C 06` has `$0116 = 0x0B` (**bit3 SET**): the arrival in scene 1 moves
  from y = 112 to y = 103.9375. Its pins are prefix-only (`^changeloc: scene 2 `,
  `^overworld entry: `), so coordinates alone will not break them — but the check then drives
  `goto:20,13 / r:60 / goto:10,13` from that new spot, and a stalled `--play` goal prints
  `play: goto FAILED`. Must be re-run.
- **`field-scene120-lucca-e0-fadechange`** (quick) — its changeloc is scripted with `$0104 = 0x01`
  (measured: `changeloc: scene 120 -> 108 at f1046 tile(0,0) fac1 style0`), so only the 1/16-px
  shift applies: `player (8,16)` becomes `(8,15)` and the camera-entry tile becomes row 0 instead
  of 1. Its two `expect` strings are prefixes, so it should hold; the 1300-frame battle drive
  downstream is not free, though.
- **Unaffected as scoped:** `camera-reenter-scene5` and `render-scene1/5/6-snapshot`
  (`--reentertest` takes the `pendX = -1` sentinel -> `f.spawnX/Y`; direct `--scene` loads never set
  pendX); `field-worldtest-roundtrip`, `field-ow-reentry-reloads-tilemap`,
  `field-exit-to-overworld-unaided`, `field-drive-forest-exit-to-overworld` (`pendKind` 2/3 —
  `lx*8` and `field.c:6817`, untouched); `vm-512-scene-actors`, `sweepsnap-arrival-305d`,
  `wram-loadcensus-*`, `wram-lifetime-survives-load` (no field arrival).
  WARNING: every `pendKind`-3 check above **would** move if `field.c:6817`'s `+15` is corrected later.

## I. Forbidden-pattern audit of the proposed change

No scene gate, no per-location constant, no stand-in heuristic, no eyeballed value: the low-byte
constants `$00 / $80 / $7F / $FF` and the test masks `#04 / #08` are literally `C0/42EA-430A`.
**Clean** — provided defect G.1 is removed (that one WOULD introduce an invented constant at
`field.c:6693`).

## J. Open questions added by this review

* **Why `y >= 221` and not `y >= 217`** at (488, ·) in map 99 — the freeze threshold our engine and
  the oracle share is not explained by the feeler arithmetic quoted in the dossier. Derive the real
  box (`C0/9923`'s 14-px box? a `+3` bottom offset?) before pinning any check on that number.
* **`src/field.c:6817` (`destY * 16 + 15`)** — an underived constant on the other arrival path.
  Does the C2 overworld-entry loader also end in `C0/42B8`? If it does, `+15` is wrong for BOTH arms
  and `CtWorldEntryDecode`'s `flags` needs the same bit2/bit3 treatment.
* **The follower PCs** (`C0/4310`, `C0/43C3`) — the dossier's open question 2 stands; not derived
  here. Both arms exist and are separate code (`4310 LDX $6D / STA $1100,X / STZ $1C80,X`).
* **The user's "scene 114"** — I re-scanned the whole exit table and all 17 objects of scene 113's
  script: no exit record and no `0xDD/0xE0` op anywhere goes 113 -> 114. Scene 113 obj1 holds the
  only two ChangeLocations in the scene, `DD 08 02 08 03` (loc 8, `$0104 = 0x01`) and
  `DD D0 03 0C 07` (loc 0x1D0 = 464, `$0104 = 0x01`) — both bit2/bit3 CLEAR, so neither would show
  this defect. The reproduced freeze is 113 -> 112. **What the user actually walked through is
  still unconfirmed**, and a sibling dossier (`gate_scene113_hidden_portal.md`) reports a
  *different* scene-113 defect (obj11, the gate, spuriously drawn by our port). Neither report has
  been matched to the user's own session.

## K. Exact commands added by this review

    mkdir -p "$TMP/ctwf/exit-adv"; cp build/ct.exe "$TMP/ctwf/exit-adv/ct_probe.exe"
    P="$TMP/ctwf/exit-adv/ct_probe.exe"; S="$TMP/ctwf/exit-adv"

    # case D, both bits (closes the dossier's open question 1)
    "$P" --teleport 4 7 12 0 --from saves/jogo_trava_lake.state --frames 90 --out "$S/sc4.state"
    "$P" --monitor "0197:1,1802:2,1882:2,0104:1,0100:1" --from "$S/sc4.state" \
         --input "10:60,00:40,20:250" --writer --diff --frames 360

    # THE CONTROL: the ROM itself, frozen at the tile with the wrong low byte
    "$P" --teleport 112 30 13 0 --from saves/jogo_trava_lake.state --frames 90 --out "$S/o112.state"
    "$P" --monitor "1802:2,1882:2" --from "$S/o112.state" --input "40:200" --diff --frames 200
    "$P" --teleport 112 29 13 0 ...   # walks to x=439
    "$P" --teleport 112 30 12 0 ...   # walks to x=439, and Y snaps to 0x0D7F

    # the freeze-threshold sweep that refutes the 7-px feeler story
    for y in 215 216 217 218 219 220 221 222 223 224; do
      ./build/ct_field.exe --assets ct_assets.dat --scene 112 --loadflags "$S/flags113.bin" \
        --at 488 $y --selftest "$S/t.raw" --frames 40 --walkscript "l:35,."; done

    # the scene-120 changeloc's own flags byte
    SDL_VIDEODRIVER=dummy CT_EXITDBG=1 ./build/ct_field.exe --assets ct_assets.dat --scene 120 \
      --loadflags saves/s120_lucca_flags.bin --at 600 184 --selftest "$S/s120.raw" \
      --walkscript "a:2,i:14, ... x72" --frames 1300

## The lead's ARRIVAL ARM, C0/42B8-430F (ported 2026-09-03; defect #5, 'Crono always starts looking down')

The op-0x80 PC loader's slot-0 (in-party, mode 0) branch is C0/429C: `STA $1100,X / STZ $1C80,X / $1A81 := 1 /
$1B01 := 1 / STA $1101,X / STX $97 / JSR C0/448D` -- the generic tables, whose tail is `4543 $1680 := 0 / 4548
$1681 := 0 / 454D $1600 := 1` -- and then it FALLS INTO 42B8, which places the lead on the location loader's
arrival bytes and overwrites that facing:

    42B8 LDX $6D / LDA $02 / STA $1801,X / LDA $03 / STA $1881,X     the arrival TILE ($0102/$0103)
    42C4 STX $0B89                                                     the lead's object offset
    42C7 REP / LDA #0008 / STA $0D00,X / LDA #0000 / STA $1700,X
    42D7 LDA $1000,X / AND #80 / ORA #01 / STA $1000,X                 cadence 1, pause bit kept
    42E1 LDA $04 / AND #03 / STA $1600,X                               FACING = $0104 bits 0-1
    42E8 LDA $04 / BIT #08 / BNE -> $1880,X := $7F  else $FF            the Y half-tile
    42FA LDA $04 / BIT #04 / BNE -> $1800,X := $00  else $80            the X half-tile
    4305 TYX / SEC / RTS

The port had the tables (pcLoadMirror) and the host's pixel placement from the same bits, but never the
arm, so every lead arrived facing DOWN (the tail's 1). MEASURED on saves/predoor_f2f_s5.state: Up through
scene 5's north exit into 439, the oracle's lead reads `$0104 = 00`, `$1600 = 00` (facing up) at f150;
ours read 01. Ported as `pcArrivalArm` at the four lead-PC load sites (ct_vm.c). A cold `--scene N`
start and the --vm resolver have no previous location, so they seed `$0104 = 1` (the tail's facing) and
the spawn tile into $0102/$0103 BEFORE the init pass -- the arm reads real bytes on every real load.
Check: `field-door-up-arrival-facing` (fixture saves/field_re/predoor_f2f_s5.lakewram), mutation killed.


## The world exit too (2026-10-04)

The overworld's exit into a location (C2/24D5's state 2 -> `JML $C0:0000`) reaches the same loader, so C0/42B8 places
the lead from `$0102-$0104` exactly as for a door. field.c staged that path's host position as `tile*16+8 / tile*16+15`
(the op-0x8B convention) and pushed it into the lead when control opened: entering the 600 A.D. cathedral (`$0104` = 04)
Crono stood at 0008/FF1C on the load and jumped to 8008/F01C at the port's f151, off Lucca. The rule's cartridge
(saves/field_re/catedral_world.state, Up 8 then A) keeps both at 0008/FF1C, as the user's own capture
(saves/field_re/catedral_user.state) shows. The world exit now stages by the law; pinned by
`field-world-exit-places-the-lead-by-the-arrival-law-vs-oracle`.
