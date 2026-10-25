# The overworld's colour math is PER WORLD ($0046, set by the world's own load script)

User report (translated from Portuguese): *"the 600ad smoke (on leaving scene 113) looks strange, dark"* — leaving
Guardia Forest into the 600 A.D. overworld, the drifting BG3 mist and the ocean texture
composited far too dark.

## What pushes the registers

The overworld NMI writes the colour-math registers from direct-page shadows every frame,
**C2/0296**:
```
0296: LDA $46 / STA $2131        ; CGADSUB
029B: LDA $47 / ORA #80 / STA $2132   ; fixed colour, BLUE  select
02A2: LDA $49 / ORA #40 / STA $2132   ; fixed colour, GREEN select
02A9: LDA $48 / ORA #20 / STA $2132   ; fixed colour, RED   select
```
(`STA $2131` occurs at exactly four sites in the ROM: C0/A518, **C2/0298**, C3/5B69, FD/EB71.)

## Who sets `$0046`

The world's OWN LOAD SCRIPT, through the ambient-object VM's absolute-store opcode.

```
$ ct.exe --monitor "7E0046:1" --from <a loc-112 state> --input "20:60,0:200" --frames 260 --writer --diff
  f0      7E0046=44*
  f33     7E0046=00*(C2:0140)      <- the location flip clears it
  f73     7E0046=04*(C2:12C8)      <- the world's load script sets it
```

`C2:12C8` is the instruction after the store inside **op 0x13** of the VM whose dispatch table is
`C2:0F91` (`JSR (C2:0F91 + op*2)`):

```
C2/12B8 (op 0x13)   A0 01 00  LDY #$0001
                    B7 58     LDA [$58],Y      ; operand word = the absolute address
                    AA        TAX
                    E2 20     SEP #$20
                    A0 03 00  LDY #$0003
                    B7 58     LDA [$58],Y      ; operand byte = the value
                    9D 00 00  STA $0000,X
                    C2 20     REP #$20
                    A9 04 00  LDA #$0004       ; advance 4 bytes
                    60        RTS
```
Its neighbours, same shape: **0x11** INC abs (C2/1296, len 3), **0x12** DEC abs (C2/12A7, len 3),
**0x14** `abs |= imm` (C2/12CE, len 4). Only 0x10/0x11 were ported; 0x12/0x13/0x14 are new.

Every world's load script starts at `$7F:0400` and its first two instructions are the colour-math
setup (bytes read straight out of the packed script blob):

| world | `$7F:0400` … |
|---|---|
| 0 (1000 A.D.) | `00` `13 45 00 02` `13 46 00 44` `36 69 06` … |
| 1 (600 A.D.)  | `00` `13 45 00 02` `13 46 00 04` `09 E6 07 7F` … |
| 2             | `00` `10 45 00` `13 46 00 3F` `09 02 06 7F` … |

Confirmed live by `--hdma`:
```
saves/ow_w0.state : cgadsub=44 (half=1 sub=0 | bg3=1) cgwsel=02 fixed=(0,0,0) TM=17 TS=13
600 A.D. state    : cgadsub=04 (half=0 sub=0 | bg3=1) cgwsel=02 fixed=(0,0,0) TM=17 TS=13
```
and by the captured world WRAM, which is where the port gets them:
`saves/ow_w<n>.live.wram[0x46]` = `44 04 3F 44 44 44 44 3F` for worlds 0-7.

## The defect

`field.c worldCompose` hardcoded CGADSUB `0x44` — the value **world 0** happens to hold — in
both of its colour-math sites (the prio-0 BG3 ocean texture and the prio-1 cloud/mist overlay),
halving both. In 600 A.D. the ROM does a FULL add, so everything BG3 touched rendered at half
brightness: the ocean, the mist streaks over it, and the villages under them.

## The fix

1. **world.c**: ported VM ops `0x12` / `0x13` / `0x14`.
2. **The shadows travel with the world snapshot.** `CT_ASSET_WORLD` blobs are captured *after*
   the load script has retired (their object blocks contain no object parked at `$7F:0400`), so
   replaying the snapshot can never re-run the store. Blob **fmt 5** appends
   `w[0x0045:0x004A]` — CGWSEL, CGADSUB, fixed B/R/G — and `worldInit` keeps them in
   `World.cmath`, applying them in **`worldEnterAt`** (not `worldInit`: that runs at the START of
   the fade with the FIELD still alive and still using `$0045-$0049` as direct-page scratch).
   This mirrors the ROM's own order — C2/0140 clears at the location flip, the script sets 40
   frames later.
   ⚠ While adding fmt 5 it turned out the fmt-4 block never advanced the blob cursor
   (`memcpy(w->vram + 0xE000, p, 0x2000);` with no `p += 0x2000`); fixed, or fmt 5 reads BG3 char
   bytes.
3. **worldCompose** now reads `$0046`: `mathBg3 = bit2`, `mathHalf = bit6`, `mathSub = bit7`,
   applied in 5-bit space with the PPU's clamp — matching LakeSnes' own
   `snes/ppu.c` colour-math block, which is what produces the oracle's pixels.

## Repro

```
ct.exe --teleport 112 19 29 1 --from saves/truce_market.state --out o112.state --frames 240
ct.exe --followscene --from o112.state --input "20:200,0:120" --frames 320     # 070 -> 1F1 at f33
PEEK_SAVE=ow600o.state ct.exe --peek o112.state ow600o.raw --btnscript "20:40,0:260"
ct.exe --hdma ow600o.state
ct.exe --monitor "7E0046:1" --from o112.state --input "20:60,0:200" --frames 260 --writer --diff
SDL_VIDEODRIVER=dummy build/Belthasar.exe --assets ct_assets.dat --scene 112 --at 304 470 \
    --walkscript "d:40,.:260" --frames 300 --selftest ow600.raw       # the same view, our side
```
Regression check: `ow-world1-colour-math` (pins `cgadsub=04` + the frame fnv at the same camera,
mover and frame number).

## Open questions

- Only `half` / `subtract` / `bg3-enable` are consumed. Worlds 2 and 7 hold `cgadsub = 0x3F`
  (math on BG1-BG4 + OBJ + backdrop) with `cgwsel = 0x00` (operand = the FIXED colour, not the
  sub screen); those two arms are packed but not yet honoured by the compose. Whatever they
  render is unverified.
- The fixed colour is (0,0,0) in all eight captures, so the add/subtract arms of the prio-0
  BG3 path currently reduce to "half or nothing". A world with a non-zero `$2132` would expose
  whether the port's operand selection is right.
- LakeSnes suppresses the half when the sub-screen pixel is the backdrop AND CGWSEL bit1 is set
  (`halfColor && (secondLayer != 5 || !addSubscreen)`), which the prio-0 BG3 path should hit —
  yet world 0's halving there was measured as correct in earlier work. Not re-derived; the
  residual hue difference vs the oracle in the deep sea is the place to look.

## The FIXED-COLOUR operand (2026-09-14)

The overworld NMI's register block (C2/027A-02AD) pushes TM `$41`, TS `$42`, TMW `$43`, TSW `$44`, CGWSEL `$45`, CGADSUB `$46`
and the fixed colour: `LDA $47 / ORA #80 / STA $2132` (blue), `LDA $49 / ORA #40` (green), `LDA $48 / ORA #20` (red). CGWSEL bit 1
clear makes the FIXED colour the operand. The attract's 1999 A.D. (world 7) uses exactly that -- `$45 = 00`, `$46 = 8F` (subtract on
BG1-BG4), `$47-$49 = 10 10 10`, oracle --peek f3036 of saves/attract_cena_inicial.state -- so the map is dimmed by 16 per channel
while the fires (OBJ, not enabled) keep their colour. worldCompose did math only on BG3 against the sub screen and took the fixed
colour as black, drawing world 7 at full brightness (tools/attract_survey.py: the leg's median difference 80.6).

Port: worldCompose records each pixel's main layer as snes/ppu.c does (0-3 BG, 4 OBJ palettes 4-7, 6 OBJ palettes 0-3, 5
backdrop) and, when CGWSEL bit 1 is clear, applies ppu_handlePixel's fixed-colour arm after the painters: add/subtract the fixed
colour on the layers CGADSUB enables, halve when bit 6 is set (always, with the fixed operand), clamp in 5 bits; clip/prevent
modes 0 and 3 are evaluated, 1 and 2 need the colour window, which is not built, and are named on stderr. The sub-screen cases
keep their graded code. Check attract-world7-colour-math-vs-oracle: 55279/57344 identical (median 80.6 -> 3.8).
