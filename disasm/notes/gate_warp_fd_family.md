# op 0xFF operands $80-$83 are the GATE WARP — bank $FD, not the $C3 overlay

2026-09-02, derived. **THE FLOW IS PORTED (2026-09-10, section 6); THE WHOLE WARP RUNS AS ITS TRANSLATED PROGRAM, every frame the cartridge's (2026-09-26, section 9).**
The text below is the original derivation, kept as written except where section 6 corrects it.
When this was written: NOT PORTED, the live VM parks every `FF xx` with `xx < $90` as a "$C3 special
sequence" (`ct_vm.c` step, `sc->seqReq`), and `field.c` only ever completes sequence 2 (the title).
This note is the derivation the port will need; it is written so the next session starts from
facts, not from the wrong label.

## 1. The dispatch (C0/3FAF)

```
3FB1 LDA $7F2001,X / 3FB5 STA $DF / 3FB7 CMP #90 / 3FB9 BCS ->4000   ; >= $90: the small screen-effect table
3FBB-3FD1  the $19 (INIDISP shadow, $0119) ramp to black, ONE step per frame (DEC $19 / JSR C0/EC60)
3FD3 JSR C0/0B4E           quiesce
3FD6 LDX #06FF / TXS       RESET THE STACK -- nothing below ever returns to the script
3FDC LDA $DF / BMI ->3FE4
3FE0 JML $C3:0000          ; operand < $80: the $C3 overlay (title etc.), $0384 := operand
3FE4 CMP #80 / BNE ; 3FE8 JML $FD:FFF1   -> FD/DA00   (FF 80)
3FEC CMP #81 / BNE ; 3FF0 JML $FD:FFEB   -> FD/DA5F   (FF 81)
3FF4 CMP #82 / BNE ; 3FF8 JML $FD:FFE8   -> FD/DABE   (FF 82)
3FFC              JML $FD:FFE5           -> FD/DB1D   (FF 83 and anything else >= $80)
```

Static census over the 504 shipped scenes (fn-pointer walk): `FF 80` ×1 (scene 8), `FF 81` ×7,
`FF 82` ×7, `FF 83` ×2 (scenes 466, 474); the `< $80` operands are $00/$09/$24/$26/$2A/$2B (the
real $C3 sequences, 8 sites). Every `FF 8x` site is immediately preceded by a `DD`/`DE`
ChangeLocation: the location is STAGED, the warp performs the transition, and the reboot at
its end loads it. (CORRECTION 2026-09-10: the `EA 37` is NOT part of that pattern. Scene 8's
site is `DD 71 02 08 07 / FF 80` with nothing between them -- obj11 fn1 @02F5 -- while scene
466's obj26 fn1 @0847 really is `DE D3 01 07 0A / EA 37 / FF 81`. The song is per-site.)

## 2. The four routines (bank $FD)

FD/DA00, DA5F, DABE share one shape (DB1D adds a `$35/$36` brightness pulse from `$FD:DDF5`):

```
JSR FD/DB97        setup: SEI, DP=$0500, DB=$00, INIDISP=$8F, NMI off, own NMI/IRQ vectors
                   (FD/EAF4 -> $0500/$0504 JML FD/DE15 / FD/DE87), PPU regs (FD/EB13: OBSEL $82,
                   BGMODE 7, BG1SC $48...), VRAM cleared, graphics decompressed (FD/F1ED / EF6F /
                   F24C), the 0x200 story flags SAVED to $7F74B3 (FD/EAA6) because $7F0000 is
                   scratch for the effect
loop:
  JSR FD/EAEB      wait one NMI ($051E)
  JSR FD/DCBD      window table for this frame ($7F701C..702B from $1B)
  W12SEL ($2123) := 3 / 0 / 2 / 8 by the $1B counter's band (see section 7 -- NOT CGADSUB)
  JSR FD/EB8B      HDMA table build ($7F70AC / $72AC alternating, hardware multiply/divide)
  JSR FD/F123      the per-line math ($F800 sine table, $FD:F181 amplitude table -> $7E2020 CGRAM words)
  LDY $1B / CPY #end / BNE loop
JSR FD/EAC8        force blank, NMI off
JSR FD/EAB7        story flags RESTORED from $7F74B3
JML $C0:0000       REBOOT -> C0/0066 loads $0100 (the DD's destination)
```

`$1B` (DP $0500 → `$051B`) increments once per NMI (FD/DE6E). The end constants:

| operand | routine | loop ends at `$051B` | frames |
|---|---|---|---|
| `FF 80` | FD/DA00 | `#02FF` | 767 |
| `FF 81` | FD/DA5F | `#00FF` | 255 |
| `FF 82` | FD/DABE | `#009F` | 159 |
| `FF 83` | FD/DB1D | `#02FF` + `$2100` pulse | 767 |

## 3. The oracle timeline (scene 8, Crono follows Marle; `--monitor`)

From `saves/field_re/scene8_taban_moving.state`, A every 30 frames, walk Left 45 / Up 100 onto
the pendant:

```
f100   $011F 1 -> 0          obj11 touched, cutscene starts
f2447  $0100 = 0071          DD 71 executes (obj11 fn1 @02F5)
f2451..f2465  $0119 0E -> 00 the 3FBB fade, one step per frame
f2578  $051B = 0000          the warp counter starts (113 frames of setup with NMI off)
f2578+767 = f3345            loop ends
+~16   $0197 = 80, objects reload, $0119 0 -> 0F at 1/frame   (measured from a fade-end state:
                              counter starts at +43, $0197 -> 80 at +810, fade-in +843..+857)
```

So DD → forest visible ≈ 950 frames (15.8 s). The port today: `DD` commits immediately through
`field.c`'s at-black transition (≈ 60 frames) and the warp is skipped — and, until this session,
the `CtSceneSequence(f.vm) < 0` gate at the `$0117` consumer refused the staged change while the
`FF 80` was parked, which is exactly the hang the workflow's playability judge named.

## 4. What a port needs (mandate-safe order)

1. `ct_vm.c` op 0xFF: split `< $80` (overlay, `seqReq`) from `$80..$FF` (gate warp request); park.
2. Host: run the 3FBB fade (`$0119` −1/frame to 0), then the FD warp for the routine's frame
   count, then the C0/0000 reboot into the staged `$0100` — the control flow and durations above,
   with the flags save/restore (`$7F74B3`) modelled.
3. The visuals (mode 7 + HDMA windows + the CGRAM math) are a separate derivation: FD/DCBD, EB8B,
   F123, DE15 and the tables `$FD:DD96` (32 bytes), `$FD:DDF5` (32), `$FD:F181` (32), `$F800`
   (sine). Byte-exact framebuffer grading is possible with the title-intro comparator pattern.

## 5. Also measured on the way: op 0xC8 (Special-Dialog) is mostly SHOPS

`C0/0CF2` dispatch on the operand: `$00` → mode 5 (screen $07; 15 sites in scenes 141/275/295/464),
`>= $80` → mode `(ROL×3) & 3` = **2** for `$80-$BF` and 3 for `$C0+` (naming), odd `< $80` →
mode 0 (the loader), even → mode 6. Census: 34 sites in 20 scenes carry `$80..$97` → **mode 2,
screen $0C = C2/D519, X = operand & $3F = the SHOP index** (0..$17). Zero shipped sites take the
mode-6 (`$0B`) arm. The shop screen is a 9-state `$68` machine (table at C2/D519: D52B init, D546
top menu, D58B..); its init `C2/DACA` copies the inventory `$CC:2C6D[shop]` (word pointers, 24
shops, `$1F`-byte lists, 0-terminated) to `$7E:1000`. The live VM steps over these 34 sites
(`s->addr = nxt`): no shop in the game opens.

## 6. THE FLOW IS PORTED (2026-09-10) -- the clock, the reboot, and what is still black

User report on scene 8: *"portal is not properly visible / the layer's movement
(background for the portal) is not canonical"*. That is this effect, and it was not running at all.

**What the port does now.** `ct_vm.c`'s op 0xFF splits where C0/3FDC splits (`LDA $DF / BMI ->3FE4`):
`< $80` still parks as `seqReq` (the $C3 overlay), `$80-$8F` parks as the new `warpReq`. `field.c`
runs the machine at the sequence consumer, in the ROM's four steps:

| step | ROM | port |
|---|---|---|
| ramp to black | C0/3FBB `DEC $19` once per frame, `BEQ/BMI ->3FD3` if already 0 | same, on `$0119`, driving `fadeBri` |
| setup | `JSR FD/DB97`, NMI off; `FD/EAA6` saves `$7F0000..01FF` to `$7F74B3` | `GW_SETUP` = 113 frames black + the same 0x200-byte save |
| the effect | per-NMI loop, counter `$1B` (DP $0500) = `$7E051B`, 0 .. END | 113.. counts the same counter into `$7E051B` |
| exit | `FD/EAC8` blank, `FD/EAB7` restore, `JML $C0:0000` | flags restored, then the host's existing reboot path |

END is the ROM's own constant per routine: `#02FF` DA00, `#00FF` DA5F, `#009F` DABE, `#02FF` DB1D.

**Measured a second time, on the other state.** `saves/field_re/scene8_gate_cutscene.state`,
`--monitor "7E0100:2,7E0119:1,7E051B:2,7E0197:1" --btnscript` (A three frames in every thirty):

```
f2342  $0100 = 0071      the DD executes -- and $0100 reads the destination THAT frame
f2346..f2360             $0119 0F -> 00, one step per frame (15)
f2473  $051B = 0000      the counter's first 0  ->  113 frames of FD/DB97 setup
f3240  $051B = 02FF      767 frames of loop, and $0197 = 80 (the reboot) on the same frame
f3257  $0197 = 04        the destination's loader is done
f3273..f3287             $0119 01 -> 0F, the destination fading in
```

113 is the same number section 3 measured from `scene8_taban_moving.state` (f2465 -> f2578), which
is why it is a constant in the port and not a fit to one capture.

**A second defect fell out of it.** Op 0xDD wrote only `$0104` of the trio C0/3B1B stores. With
DP = $0100, `3B28 STA $00` is the masked location word into `$0100/$0101` and `3B30 STA $02` the
tile pair into `$0102/$0103`; the port kept those in its C mirrors alone. Nothing noticed while
every consumer went through `CtScenePollLocChange` -- but the reboot does not: `C0/0066` is
`LDX $0100`, so the Telepod's warp rebooted straight back into loc 8. Both stores are ported now.
The port lands where the ROM lands: loc 071, tile (8,7), facing 1.

**What is still black.** The mode-7 tunnel itself: `FD/DCBD` (the per-frame window table
`$7F701C..702B`), `FD/EB8B` (the HDMA table build, hardware multiply/divide), `FD/F123` (the
per-line math off the `$F800` sine and the `$FD:F181` amplitude table into `$7E2020`), `FD/DE15`
(the effect's own NMI), and the `$2123` CGADSUB banding the loop selects (3 / 0 / 2 / 8 by the
counter, with `$23` from `$FD:DD96`). The screen is blank for exactly the ROM's duration instead.
The oracle reference for that work is one command: `--peek scene8_gate_cutscene.state <out> --btnscript`
truncated to a frame inside the loop (f2700 and f3100 both land in the tunnel).

**One thing the headless fixture cannot show.** The trace prints `$0119=00` because a cold
`--scene` entry skips the settle that leaves the brightness at `0F` (the load's fade-in, C0/2848,
is what writes it), so C0/3FBB's `LDA $19 / BEQ ->3FD3` correctly takes the already-black arm and
the 15 ramp frames do not run there. In interactive play they do.

**Pinned** by `field-scene8-gate-warp-flow` (quick tier, 1.2 s): the two `gatewarp:` lines, the
`reboot:` line and the destination's selftest. Three mutations, all killed -- the C0/3FDC split
undone, C0/3B28's store dropped, and FD/DA00's `#02FF` halved.

## 7. The visuals, slice 1: FD/DCBD's window parameters and the band are PORTED (2026-09-10)

The first piece of the tunnel that can be graded without a renderer at all: what the effect COMPUTES
each frame. It is a pure function of the counter, so the oracle's own WRAM is the reference.

**The register was mislabelled.** Section 2 called `$2123` CGADSUB. `$2123` is **W12SEL**, the BG1/BG2
window mask (CGADSUB is `$2131`). FD/EB13's setup makes that unambiguous: `$2105 = 07` (mode 7),
`$2107 = 48`, `$2123 = 03`, `$2126/$2127/$2128/$2129 = 00/FF/00/FF` (the two window ranges),
`$212C = 01` (BG1 on the main screen), `$212E = 01` (window enabled on it). So the loop's 3 / 0 / 2 / 8
switch which window clips BG1, and how:

| counter | `$2123` | `$23` |
|---|---|---|
| `< 0020` | 3 (W1 on BG1, inverted) | `$FD:DD96[$1B & 1F]` |
| `< 02A0` | 0 (no window) | 0 |
| `< 02C0` | 2 (W1 on BG1) | `$FD:DD96[$1B & 1F]` |
| else | 8 (W2 on BG1) | 0 |

`$23` is not a PPU value at all: FD/EB8B reads it (`LDA $23 / STA $0A`) as the amplitude its HDMA
build divides by, so the two banded arms are the effect opening and closing.

**FD/DCBD, byte for byte** (DP `$0500`, DB `$00`):

```
$7F701C = 0080          $7F7020 = 0080
$7F7026 = ($1B << 2) & 01FF                     $7F701E = $7F7026 + 0070
$7F702A = ((($1B ^ FF) << 2) - 0070) & 01FF     $7F7022 = $7F702A + 0070
```

The `EOR #FF` at 3DDCFD is 8-bit and 3DDCF6 cleared B, so the high half enters the two ASLs as 0 --
the second edge is the counter's MIRROR, which is what makes the two window edges close on each other.

**Graded against the oracle's own WRAM**, `--peek scene8_gate_cutscene.state <out> --btnscript`
truncated to f2500 / f2700 / f3100 (counters 26 / 226 / 626):

```
c=26   80 00 D8 00 80 00 94 01 00 00 68 00 00 00 24 01   $0523 = 4A
c=226  80 00 F8 01 80 00 74 00 00 00 88 01 00 00 04 00   $0523 = 00
c=626  80 00 38 02 80 00 34 02 00 00 C8 01 00 00 C4 01   $0523 = 00
```

All sixteen bytes and the `$23` byte identical to what the port publishes. `CT_GWTRACE="26,226,626"`
prints them; `field-scene8-gate-warp-flow` pins the three lines and two more mutations (the mirror's
`EOR` dropped, the `$FD:DD96` lookup dropped) both fail it.

**The tables, read out of the ROM** (all verified against the addresses the routines index):

* `$FD:DD96` (32) `[32 ROM bytes at 0x3DDD96: python tools/dis.py 0x3DDD96 32]`
* `$FD:DDF5` (32) the `FF 83` brightness pulse `0F 0E 0C 0A 08 06 04 02 01 01 02 ... 0F`
* `$FD:F181` FD/F123's amplitude: **32 WORDS = 64 bytes** (every high byte 0), not 32 bytes -- the
  loop reads the LOW byte at `F181+X` for X = 0,2,..,3E, so it is a symmetric triangle
  `00 01 02 03 04 05 06 07 08 0A 0C 0E 10 12 14 16` and the same sixteen mirrored back down to 00
* `$00:F800` = `C0/F800`, a 256-entry SIGN-MAGNITUDE sine (bit7 = sign, bits0-6 = magnitude;
  `s[0]=00 s[40]=7F s[80]=00 s[C0]=81`)

**What is left, in order.** (a) FD/F123 and FD/DCBD's tail -- both now DERIVED and VERIFIED against
the oracle, see section 8. (b) FD/EB8B: the per-scanline HDMA table built into
`$7F70AC` / `$7F72AC` (alternating on `$24` bit0) with the hardware divider, `$0100 / $23`; FD/DE15
picks up `$7F70CC`/`$7F71AC` or `$7F72CC`/`$7F73AC` by `$0524` bit0. (c) The mode-7 renderer, for
which `src/title_intro.c` is the working precedent in this repo (per-scanline matrix, 1-based
scanlines, `largeField` transparency).

## 8. RETRACTION, and the colour math verified against consecutive oracle frames (2026-09-10)

**⛔ RETRACTED, from section 7 as first written**: *"FD/DCBD's own tail rotates the colour buffer
`$7E2002..$7E207C` down by four words and wraps the four it saved to the end."* **That block is DEAD
CODE on this path.** At 3DDD1B there is an unconditional `BRA ->3DDD65` that jumps straight over it:

```
3DDD16  A9 00 00   LDA #0000
3DDD19  E2 20      SEP #20
3DDD1B  80 48      BRA ->3DDD65      <-- skips 3DDD1D..3DDD64 entirely
3DDD1D  ...        the $7E2002 rotation -- never executed here
3DDD65  A6 08      LDX $08           <-- the tail that DOES run
```

How the error happened, and the trap worth remembering: `tools/dis.py` is a **linear** decoder, so with
`m` still 8-bit it read `A9 00` as `LDA #00` and desynced by one byte -- which turned the `BRA` into
part of a bogus instruction and made a dead block look live. Re-running it as `dis.py 3DDD16 0x18 0 0`
shows the branch immediately. What CAUGHT it was not re-reading the listing but the data: two oracle
frames one apart (`--peek` at f2700 and f2701, counters 226 and 227) differ in **sixteen bytes total**,
and none of them is in `$7E2002..$7E207C`. A four-word rotation would have moved most of that buffer.

**What the tail really does** (3DDD65-3DDD8C): a **15-word ring at `$7E2102..$7E211F`, rotated left by
TWO words every frame**. `LDA $7E2106,X / STA $7E2102,X` for X = 0,2..0x18 (`CPX #001A`, 13 iterations)
shifts words 2..14 down to 0..12, then the two words saved on the way in go to `$7E211C`/`$7E211E`.
Measured: the ring reads `2000 3000 4000 5000 6000 7000 7C00 7000 6000 5000 4000 3000 2000 0000 1000`
at counter 226 and exactly that rotated left by two at 227 -- a blue ramp walking around a ring.

**FD/F123, and it predicts the oracle exactly.** For each of the 32 words at `$7E2020` (X = 0,2..0x3E):

```
p1 = ((C0/F800[$1B] + 80) & FF) * $FD:F181[X]  >> 8        3DF128-3DF13E   ($4202*$4203 -> $4217)
p2 = ((C0/F800[$1B ^ FF] + 80) & FF) * $FD:F181[X] >> 8    3DF142-3DF15A
word[X] = (word[X] & 7C00) | (((p2 << 8) >> 3) | p1)       3DF15D-3DF170   (XBA / LSR x3 / ORA)
```

`$1B` is read as a BYTE here. Fed the oracle's own `$7E2020` words at counter 226 and the counter 227,
this reproduces **all 32 of the oracle's words at 227**, including the only two that changed (w15,
w16). The `AND #7C00` is why so little moves: each word keeps its own blue and only red/green are
recomputed.

**The one thing still missing before either can be PORTED: their INITIAL state.** Neither buffer is a
blob in the ROM (the implied counter-0 ring, `0000 1000 2000 ... 7C00 ... 2000`, does not appear in it
at any rotation) and **nothing in bank $FD stores to them**: the only two writers anywhere in the bank
are 3DDD79 and 3DF170, the two routines above. So the fill arrives through the `$2180` WRAM port or a
DMA -- invisible to a grep, the same class of writer as C0/571A-58AA's per-object fill (see
[[ct-location-load-object-defaults]]).

MEASURED with the oracle's writer hook (`--monitor "7E2102:2"` and `"7E210A:2"`, `--diff --writer`),
which names the last writer's bank:pc where there is one:

```
f2367  the ring reads 0000                       (zeroed -- NO writer attributed)
f2368  $7E210A = 0040                            THE FILL -- again NO writer attributed
f2474  first rotation, and every frame after     (FD:DD7D, the loop at 3DDD79)
```

So the fill lands ONE frame after the clear, eight frames into FD/DB97's setup window, and neither it
nor the clear is a CPU store the hook can see -- which is the positive evidence for the `$2180`/DMA
route rather than an inference from the grep. Finding the code itself needs a DMA-side instrument
(`--loadcensus --censusat` is the one that produced the writer PC for C0/571A-58AA's fill). Port the
transition only once that fill is derived: computing the right deltas from a wrong base would publish
wrong bytes.
## 9. THE WARP RUNS AS ITS PROGRAM, TRANSLATED -- every frame the cartridge's (2026-09-26)

User defects 2026-09-26 #2 ("the temporal flux does not appear") and #20 ("no portal on the way back"): sections 7-8 had
begun the idiomatic port (FD/DCBD's window table, F123 verified) and stopped at the buffers' initial state; the screen was
black for the effect's 880 frames. The warp is the same class as the race (user decision 2026-09-14,
[[ct-seq-race-translation-decision]]): its own NMI/IRQ vectors, HDMA over a mode-7 BG1, the hardware multiplier -- a
program whose output is its bus. So it is ported the same way: its program translated from the ROM (tools/gen/xlat.py)
and run on the vendored snes.c/dma.c/ppu.c.

**The trace.** `CPUTRACE_UNIQUE=1 CPUTRACE_FROM=FDFFF1 ct.exe --cputrace saves/field_re/scene8_gate_cutscene.state 3230`
with the cutscene's A mash (A = 0x100 in the drive grammar), and for the other three routines the same Telepod state with
the script's operand poked: `FF 80` sits at `$7F:269B` (obj11 fn1 `DD 71 02 08 07 / FF 80`), and C0/3FB1 reads the operand
from the script image through `LDA $7F2001,X`, so `PEEK_POKE=7F269C=81/82/83` sends C0/3FEC.. to FD/DA5F / DABE / DB1D.
The union is src/xlat/gwarp_fd.c: 1141 instructions (1136 executed, 5 static). The warp never touches the SPC (no
`$2140` access): the music plays on. (2026-10-10: `python tools/gen/xlat_all.py gwarp_fd` remakes it byte for byte,
under the rule, from scene8_gate_cutscene.state alone -- the f2300 state is its `--peek` of the mash cut to 2300 frames,
which without the rule IS saves/field_re/scene8_gate_warp_f2300.state byte for byte; under the rule 183 WRAM bytes
differ and every trace translates to the same program.)

**The lockstep.** `XLATCMP_AT=FDFFF1 XLATCMP_PROG=gwarp ct.exe --xlatcmp saves/field_re/scene8_gate_warp_f2300.state 900`
(the state: the cutscene 2300 frames in -- `--peek` runs a btnscript to ITS end, so the drive is truncated to 2300 frames
-- with FD:FFF1 at its f17): IDENTICAL, registers, flags, clock and beam every instruction, WRAM/VRAM/CGRAM/OAM and the
picture every frame -- FF 80 14112515 instructions / 880 frames, FF 81 5890817 / 368, FF 82 4349247 / 272, FF 83 14112900
/ 880 (checks seq-gwarp-xlat-lockstep, seq-gwarp-8{1,2,3}-xlat-lockstep).

**The host.** field.c's warp machine keeps C0/3FBB's ramp and opens the stage where C0/3FD3 jumps: CtXlatStageOpen with
the operand selects the program and the entry (C0/3FE4-3FFC's table: $FD:FFF1 / FFEB / FFE8, else $FD:FFE5; the flags the
last `CMP` left) and the warp's own handoff clock, measured like C3:0000's at the game's first visit (scene 8's Telepod,
`--xlatentries` now watches the four FD entries too): v=239 h=646, cycles mod 8 = 2, an odd frame, open bus FD, the WRAM port
at $007E0 (CT_XLAT_GWARP_*). It is handed the field's VRAM and CGRAM, runs under the rule, and its `JML $C0:0000` hands the
WRAM (flags restored by FD/EAB7, `$0100` staged) and the VRAM back to the reboot. The ramp's frame count: 3FC1 `DEC $19`,
3FCA `JSR C0/EC60` (the NMI wait), 3FCD `CMP #01 / BPL` -- the decrement that reaches 0 is followed by one more frame before
3FD3; an already-black `$19` jumps in the op's own frame. The old partial FD/DCBD port (gateWarpEffectFrame,
CT_GWTRACE) is gone; CT_GWARP_FLOWONLY=1 keeps the flow-only machine (black).

**2026-09-28: the ramp is the OP's, for both targets, and its first DEC is in the op's frame.** 3FBB-3FD1 run before 3FDC
splits on the operand, so the $C3 sequences (< $80) have the same fade as the warp -- the port had it for the warp only
(gwPhase 1/4) and opened a sequence on the frame after the op whatever `$0119` held. And the loop is entered from the op's
own visit: `3FC1 DEC $19` runs in that frame (the host's copy took it one frame later). Now ONE machine: the VM's op 0xFF
takes the first DEC on its first visit (ct_vm.c, `sc->ffFade` / CtSceneFfFading), field.c's `ffPro` runs the rest at the
head of each frame (DEC, FD/E39C, the VM frozen), holds the frame whose head finds 0 (the jump's, 3FD3) and lets either
target start on the frame after it -- the frame a target starts on when the op found the screen black and jumped inside
its own frame. A C0/2848 ramp that ran the op stops (its INC never comes back). MEASURED on the logical oracle (the attract,
--monitor $0100/$0119, --hookpc C30000/C03FD3): the hub's `FF 61` before 0A5 met the ramp at 0D -- 0C at the op's frame
f3294, 00 at f3306, C3:0000 at f3307 line 239; before 0E6 at 0F (C3:0000 at f5271); the race's `FF 00` and its credits'
`FF 61` at 05. Every credits page ticks first at J + 18 and reboots at J + 122 (J = the jump's frame) whatever `$0119` was.
Check attract-ff-own-fade-before-the-jump-vs-logical-oracle (3 mutations). The scene-8 warp meets `$0119 = 00` and is
unchanged (880/880) -- in the check's COLD drive only; the cartridge meets 0F and fades (sec. 10).

**Graded every frame.** `python tools/gwarp_cmp.py`: field-scene8-gate-warp-flow's own drive against the Telepod state
under the rule -- 880 / 880 frames identical (and in real time). The jump is in the vblank of the state's f17 (v=239), so
the program's first picture is the cartridge's f18: the relation entry 3 has against its real-time reference (C3:0000 at
f17, the port's first stage frame = +18, 88/88) -- against the rule's power-on reference entry 3's hand-off lands on its own
f18 and matches at +0; a frame label's relation to the vblank is the reference's, not the port's.

## 10. The whole transition from the cartridge's own WRAM; op 0xDD moves nobody (2026-09-29)

User request 2026-09-29: "After that, in the scene transition, there is a full-screen time-flow animation. If it has not been
ported, port it." (translated from Portuguese) The tunnel is sec. 9's program. What was not graded is everything around it: gwarp_cmp starts at the
program's first picture, and field-scene8-gate-warp-flow's drive enters scene 8 COLD -- `--scene 8`, no C0/2848 ramp, so
`$0119` is 00 under a lit picture. Its `FF 80` therefore "met `$0119 = 00`" (sec. 9) and jumped in its own frame. The
cartridge does not: from saves/field_re/scene8_gate_cutscene.state under the rule, `DD 71 02 08 07` writes `$0100 = 0071`
at c2297 (C0:3B2A) with `$0119 = 0F`, C0:3FC3's `DEC $19` takes it 0E..00 over c2301-c2315, and the program opens at c2318.

**Seeded, not driven.** saves/field_re/scene8_gate_c2200.state is that run's c2200 (the A mash truncated at 2200 frames --
`--peek` runs a btnscript to its end) and scene8_gate_c2200.lakewram its WRAM. The port seeded with it
(`--scene 8 --loadwram`) runs the DD at its f97 = c2297 and the first DEC at f101 = c2301: the scheduler's four-frame gap
between the DD and the `FF 80` is the ROM's, and the port's.

**The defect it found: op 0xDD raised a location change.** C0/3B1B is `$01D9` = the word, `$0100` = word & 01FF,
`$0102/03` = the tile pair, `$0104` = the facing and arrival flags, `SEC / RTS`. Nothing else: no latch, no fade, no load --
C0/0066 reads `$0100` only when something reboots. The census over every location script: 65 DD/DE sites; 40 are followed
by a gate warp (`FF 80-83`), 23 by a $C3 sequence (`FF` < $80), 1 by the loader (scene 0's `DD 00 00 00 00 / C8 41`), and
scene 172's obj9 fn1 `DD 0F 03 08 08 / E8 C3 / FF 9B / RETURN` stages an ORIGIN: its fn0's `DF CC 03` reboots into 1CC and
C0/3B79 files the DD's 10F in `$0105`. The port's op also set `locChange`, and field.c then ran its own door fade in the
frames before the reboot op -- on the Telepod, four steps of dimming in c2298-c2302 that the cartridge does not take
(10538 px at c2302), then a jump back up to C0/3FBB's ramp; at scene 172 it would have loaded 10F. ct_vm.c's DD/DE now make
the stores alone; `locChange` is the commit's (C0/0C9A, op 0xE0 and the positional exits).

**Graded.** `python tools/gate_transition_cmp.py` (check field-gate-transition-vs-cartridge-from-its-wram):

    gate-transition: DD c2297; scene 8 + the op's fade at P+2 c2203..c2317: 0 / 115 differ, mean within 0.0;
                     tunnel + load c2318..c3229: 0 / 912 differ; arrival from the lit c3230 at P+1 (the load one frame
                     long): 0 / 300 differ, max 0 px

Each stretch at its own relation: scene 8 and the arrival are the field (logic P = the cartridge's thumbnail P+2,
ct-frame-alignment-p2), the fade, the tunnel and the load the stage's hardware frames. Graded so, scene 8 is exact -- the
pods' $0F01 commits (C0:CC17) and the portal's palette writes (FD:E55D) land on the same labels on both sides and
show on the same frames; the "40 frames at the pods" of this section's first version were the tool reading scene 8 at
the stage's relation. The arrival matches one frame after P+2: its logic is one frame LATE (the iris's $0139 = 1 at
label 1065 against 1064) -- the load is one frame long; the first version said "early" and read the relation backwards.
Crono's face during the open is exact since the lead is drawn from $0F01,X (field_iris_window.md sec. 7).
The op's fade is the field's (C0/3FBB-3FD1 run in its frame) and is graded at P+2 as well, by the mean brightness too
(one INIDISP step is below the 24 threshold): identical, mean within 0.0. Until 2026-09-29 it was one frame late -- field.c
held the brightness one pass back (brShown) for the window wipe's first frame; that frame now takes the window the next
C0/22D8 call writes, which channel 7 reads live (field_window_wipe.md, last section), and the brightness is the frame's own.
