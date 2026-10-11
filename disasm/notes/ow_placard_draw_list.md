# The overworld DRAW LIST — why the era placard is not on screen the whole time

*"on the overworld, the sign placard should not show all the time"* — 2026-09-07. Derived from the
ROM + the LakeSnes oracle only. Companion to `ow_era_placard.md`, which got the placard's **identity**
right (op 0x30 → `$C3:E000[id]`) and named its counter, but not the thing that actually gates the draw.

## 1. What the port had

`src/field.c` drew the placard whenever the class-`$0F63` attr-`$31` object held an anim pointer whose
`$C3:E000` id was in `$75..$7B`. That pointer is written **once**, by the `op 0x30` at the top of the
world's own script, and is never cleared — so the placard was permanent. The same was true of every
other ambient object: boats, gulls and fair balloons were drawn straight off their block state on every
frame the block existed.

`ow_era_placard.md` §4 had already measured that `$7E:1BF7` gates the placard, and even wrote *"since
our object VM runs that script, the anim pointer IS the gate"*. **That sentence is wrong**, and this file
is the correction: both arms of the script hold the identical anim pointer.

## 2. The two arms, and that they are indistinguishable in the block

World 0's placard script, read out of the live `$7F:0400` blob (`saves/ow_w0.state` → `--peek`, wram
`+0x10000+0x068E`); world 1's is the same shape at `$06CD`:

```
068E  2C 08 00 C0 00            op 0x2C  teleport (8, 192) -- SCREEN space
0693  02 31                     op 0x02  attr high  = $31
0695  01 00                     op 0x01  attr low   = 0
0697  30 75                     op 0x30  anim = era id $75 ("1000 A.D.")
0699  4C F7 1B 78 0A            op 0x4C  if $7E:1BF7 < $78 -> pc += 10   (to the HIDE arm)
069E  39 01                     SHOW arm: wait 1 frame WITH the C2/0E1D anim tick
06A0  1A 98 06                            jump $0699
06A3  38 01                     HIDE arm: wait 1 frame WITHOUT the tick
06A5  11 F7 1B                            INC $1BF7
06A8  1A 98 06                            jump $0699
```

`op 0x38` (C2/186B) and `op 0x39` (C2/188E) are the SAME wait; 0x39's only difference is one extra
instruction, `218AC JSR C2/0E1D`. Nothing else in the two arms differs.

**Measured** (`build/ct.exe --peek saves/ow_w0.state OUT --btnscript …`, block `$0D30`):

| drive | script pc | anim +0B | OAM at y=192 |
|---|---|---|---|
| `80:60` (walking/pressing) | `$06A3` (HIDE) | `$F836` | **none** |
| `80:60,0:200` (then still) | `$069E` (SHOW) | `$F836` | `$1AC@252`, `$1A6@12`, `$18A@28` |

Same anim pointer, same position, same attr — only the pc differs. **A renderer keyed on the block
cannot be right.**

## 3. The mechanism: a per-frame draw list

```
C2/032B  JSR C2/0C4D     <- EMPTY the list: head $7E:B500 self-linked, the 64 nodes at $7E:B000
C2/0330  JSR C2/034D         re-threaded from the $7E:B400 template, free head $7E:B504
C2/0333  JSR C2/051D     <- the object pass (the ambient VM + the lead's C2/3404 both live here)
C2/0336  JSR C2/0CEA     <- DRAIN the list into the OAM shadow $7E:0700/$0900 (decoder C2/0D08)
```

The only writer of that list is the anim sub-VM's **op-4 SHOW FRAME** handler:

```
C2/0EA3  SEP #20 / LDX $4E / LDA $000F,X / BIT #40 / BNE ->0EC9      ; HOLD -> park
0EAE     LDA $000E,X / BNE ->0EBD
0EB3     LDY #03 / LDA [$52],Y / STA $000E,X / BRA ->0EC9            ; load dwell -> park
0EBD     DEC $000E,X / BNE ->0EC9                                    ; still dwelling -> park
0EC2     REP #20 / LDA #0004 / CLC / RTS                             ; dwell EXPIRED: advance 4, NO enqueue
0EC9     LDA $54 / STA $57 / REP #20 / LDY #01 / LDA [$52],Y / STA $55
0ED6     JSR $0B53                                                   ; <-- ENQUEUE
0ED9     TDC / CLC / RTS                                             ; advance 0 = park on this op
```

`C2/0B53` unlinks a node from the free list and copies the object's **X (+14)**, **Y (+18)**, **attr
(+0F)**, **tile base (+10)** and the descriptor pointer (`$55`) into it, then Y-sorts it into the list.

So **visibility is "did this object's anim sub-VM run this frame"**. An object parked on `op 0x38`
never reaches op 4, enqueues nothing, and emits no OAM at all.

## 4. The counter, and who resets it

`$7E:1BF7` is ramped by the placard's own `op 0x11` (handler `C2/12A1`) once per frame while it sits in
the HIDE arm, and zeroed by **`C2/36E7 STZ $1BF7`** — which the lead handler reaches only through
`C2/36B4 BCC ->36E7`, i.e. **only when `C2/39C3` actually ISSUED the step**. A press refused by
collision does not reset it, which is why walking into a cliff still surfaces the placard.

`--monitor 7E1BF7:1 --from saves/ow_w0.state --btnscript "40:80,0:160" --diff --writer`:

```
f1  00 (C2:36EA)   f9  00 (C2:36EA)   f17 00 (C2:36EA)   f25 00 (C2:36EA)   f33 00 (C2:36EA)
f3..f8, f10..f16, … 01,02,03,… (C2:12A1)      <- +1 EVERY frame, 8-frame step cadence
```

And on a walk that runs into the coast (`40:300`): last `STZ` at **f65**, `$1BF7` reaches `$78` at
**f185**, and the OAM **shadow** `$7E:0700` gains `FC C0 AC 31` (x252 y192 tile `$1AC` attr `$31`) on
that **same f185**. `185 − 65 = 120 = $78`. The player stops at `$0D84 = $0170` (x = 368).

The PPU's own OAM lags the shadow by one DMA (`--peek` at 186 frames still shows nothing, at 187 shows
the three entries) — the same one-frame lag `world.c CtWorldLeadAnimFrame` already documents for the
lead. Not introduced here, and not closed here.

## 5. What the port does now

- **`src/world.c`** keeps the list: `CtWorldDrawListReset()` (C2/0C4D) runs at the top of
  `CtWorldAmbientFrame`, and `owDrawListEnqueue()` is called from the op-4 case on exactly the paths
  the ROM falls through `0EC9` on (`adv == 0`), never on the expiring dwell (`adv == 4`).
  `obAnimTick` now takes `ram` so it can name the block. `CtWorldObjShown(blockAddr)` /
  `CtWorldObjShownDesc(blockAddr)` are the readers.
- **`src/world.c` `CtWorldLeadAnimFrame`** ports `36E7 STZ $1BF7` onto the step-issued arm (the `else`
  of the existing `blocked` test, which is the port's `C2/39C3` carry).
- **`src/field.c`**: every ambient draw loop (boat, gull, balloon, placard) starts with
  `if (!CtWorldObjShown(b)) continue;`, and the placard's `g_placardEra/X/W/Px` are reset per frame
  instead of being sticky.

The list ORDER (the Y-sort at C2/0B98) is **not** reproduced — this host has its own draw order and no
OAM table — and the node's position snapshot is not kept either, because the renderer reads the same
block on the same frame. What the port takes from the list is the visibility gate and the descriptor.

## 6. Graded

`ow-era-placard-hides-while-walking` (quick tier): drive `l:200` on `saves/field_re/ow_w0.lakewram` →

```
selftest-ow-placard-gate: start=show flips: f45=hide f228=show showAt$1BF7=$78
selftest-ow-placard: era=$75 x=0 w=44 px=577
```

`showAt$1BF7=$78` is the op-0x4C operand itself, so the check grades the ROM's number rather than this
port's frame numbering. The frame COUNT between step and show is deliberately NOT pinned: it is 119 or
120 depending on where in its own 2-frame op-38 cycle the object sits when the step lands, so it is not
a ROM constant. Three mutations (drop the gate; drop the `STZ $1BF7`; move the enqueue to the expiring
dwell) are all KILLED.

## 7. Open

1. The one-frame OAM-DMA lag of §4 is engine-wide for the overworld renderer, not placard-specific.
   Closing it means giving the overworld composer the ROM's shadow-then-DMA order, not special-casing
   this sprite.
2. `ow_era_placard.md`'s open items (world-1 colour math over the OBJ layer, the world-boot arm order,
   era `$77` never observed) are unchanged by this.

---

## 6. The OBJ LAYER: every overworld sprite from the OAM the ROM builds (2026-09-14)

Item 1 of §5 is closed, and not for the placard alone: the attract's 2300 A.D. party was invisible because the
renderer only knew captured atlases (lead, gull, boat, balloon) and the baked placard. The pipeline is now the ROM's,
end to end, graded by `ow-objlayer-2300ad-walkers-vs-oracle` (tools/ow_objlayer_cmp.py):

1. **The list lives in WRAM.** `C2/0BF6` (world load) writes the prev/next template at `$7E:B400`; `C2/0C4D` (C2/032B,
   before the WHOLE object pass -- the lead block's tick included) empties the list (head `$B500`) and re-threads the 64
   nodes at `$B000` from the template onto the free list (`$B504`); `C2/0B53` unlinks a free node, copies X (+14), Y (+18),
   attr (+0F), tile base (+10), the descriptor and its bank, and inserts it: attr bit 0 = front group, tile-base bit 15 =
   tail, otherwise **descending Y** with the later of two equal Ys behind. world.c `CtWorldDrawListInit/Reset`,
   `owDrawListEnqueue`.
2. **The OAM shadow.** `C2/032E STZ $4D`, `C2/034D` parks every entry (Y = `$E0`, high table 0 -- X, tile and attribute
   are left as history), and after the pass `C2/0CEA` walks the list into `C2/0D08`: per descriptor piece, X/Y plus the
   signed dX/dY (minus the BG2 scroll `$22/$28` wrapped into the torus when attr bit 7 marks world coordinates), dropped
   outside x -15..255 / y -15..223, word2 + tile base OR the attr's bits 1-5, and the high table's X bit 8 + size (tile
   base bit 14). world.c `CtWorldOamBuild`.
3. **The raster shows the PREVIOUS build** (`C2/0184` DMAs `$0700` at the top of the next NMI), so field.c copies the shadow
   into `World.oamShow` before building the next one -- the lag §4 measured, now structural.
4. **Graphics and palettes from the ROM, for the party** (field.c `worldLoadObjFromRom`, at world entry): `C2/2768` four
   packets (`$C6:FDF0`, ids `[$ED+$0C..$0F]`, bit 7 = keep) into `$7F:9000-$CFFF` and `$4000` bytes to VRAM 0; `C2/2B61`
   packet 0 into `$7F:9000` then each member's `$400` block (`$9000 + id<<10`, a 16-bit MVN: an empty `$80` slot wraps) and
   two `$40` strips to `$7F:B000..`, `$1000` bytes to VRAM `$2000`; `C2/2882` the world palette (`$C6:FEA0`, id `[$ED+$0A]`)
   into `$7F:8C00` and the characters' into `$7F:8E00`, `C2/28ED` each member's first 8 colours into OBJ palettes 4/5/6,
   `C2/2D13` OBJ palette 0's first four. The captured world-2 blob had other members in palettes 5 and 6.
5. **The picture:** field.c `worldObjLine` is `snes/ppu.c ppu_evaluateSprites` (32 in range, then backwards, 34 slivers,
   lowest index in front) over OBSEL `$60` (`C2/0048`), composited by `ppu.c layersPerMode[8]` (mode 1 + BG3 priority,
   `$1E = $09`): OBJ3 over BG1p1/BG2p1, OBJ2 under them, OBJ1/OBJ0 under every BG1/BG2 pixel, BG3's priority-1 clouds on
   top. Row phase: this compositor draws BG row y+V at fb row y where `ppu_runLine(L)` draws L+V at row L-1, so the OBJ
   line for fb row y is y-1 -- the +1 the lead's atlas draw always carried. `CT_OW_ATLAS=1` draws the old way.
6. **A silent native was the walkers' route:** op 0x34 `34 6F 78` = `JSR C2/786F` (`$2980-$2982` -> `$1BA3-$1BA5`); the
   scripts read `$1BA4`/`$1BA5` (`17 22 A4 1B` / `1F 22 03 07`) to give Robo his own route (`$7F:0ADA/0AF6/0B12`). The port's
   obNative ignored unknown targets without a word; they are named now (C2/79E0 is one).

Measured on the attract (port f6250 = cartridge `saves/attract_2300ad_f5800.state` + 131, aligned by the walker blocks):
OAM 128/128 entries, VRAM `$2000-$3FFF` 8192/8192, `$7F:8C00` 256/256.

OPEN, seen in the same scenes: the 2300 A.D. reddish scanline haze is an HDMA (`$4A = 02`) installed by the underived
op-4E target `$C6:E27E`; the attract's world-0 leg leaves after ~40 frames where the cartridge stays 270; and the port
skips the cartridge's 65 M B.C. visit (087 -> 1B1 -> **1F3** -> 1B1 -> 1B0) -- CLOSED 2026-09-14: op 0xDF ran as
0xDD and the hub fell through into its next state (field_op_e0_fadechange.md section 4).

