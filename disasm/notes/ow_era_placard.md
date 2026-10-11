# The overworld ERA PLACARD is an object, not a HUD overlay (defect #8)

*"invented 600AD placard"* — 2026-09-02 list, item 8. Derived from the ROM + the LakeSnes oracle only.
Everything below was re-run and cross-verified; where a prior note was wrong this file says so.

## 1. What the port had

`src/field.c` drew the era indicator itself: a hand-written `eraIdx[8]` world → string map, a captured
bitmap (`src/data/era_hud.h`) for world 0, and for **every other world** the era name stamped with the
**dialogue font** inside a hand-drawn box, gated on `owIdle >= 84`. Nothing in the ROM does any of that.
The "600 A.D." the user reported was that font fallback. Both the selection and the gate were invented.

## 2. The ROM

The placard is an ordinary overworld ambient-VM object (class `$0F63`, handler `C2/0F63`, attr `$31`)
parked at **(8,192) in SCREEN space**. Its per-world script — in the decompressed blob at `$7F:0400` —
is, for world 1 (`$7F:06CD`):

```
2C 08 00 C0 00   op 0x2C  teleport (8, 192)
02 31            op 0x02  attr = $31
01 00            op 0x01  attr low nibble = 0
30 7B            op 0x30  anim = era id $7B   <- the "?" placard
27 AD 1B 01 07   op 0x27  if (WRAM[$1BAD] & $01) == 0 -> pc += 7   (skip the real era)
30 76            op 0x30  anim = era id $76   <- "600 A.D."
4C F7 1B 78 0A   op 0x4C  if WRAM[$1BF7] < $78 -> pc += 10         (not shown yet)
39 01 / 1A DE 06 / 38 01 / 11 F7 1B / 1A DE 06     the show/hide loop
```

**So the era is NOT a function of the world number.** Worlds 1 and 2 carry TWO arms and pick between them
from WRAM at run time: world 1 on `$7E:1BAD` bit 0, world 2 on `$7E:1BA6 >= $36` (the storyline byte,
copied from `$7F:0000` at every world load by C2/26E8). 600 A.D. legitimately shows **"?"** until the
story opens it. A world → era map cannot express that, which is why the port's could never be right.

### op 0x30 == C2/160F -> C2/1CDA

```
C2/160F  LDY #$0001 / LDA [$58],Y / JSR $1CDA / REP #20 / LDA #$0002 / RTS      (advance = 2)
C2/1CDA  REP #20 / AND #$00FF / ASL A / TAX / LDA $C3E000,X / LDX $4E
         STA $000B,X / SEP #20 / LDA #C3 / STA $000D,X / STZ $000E,X / RTS
```

A **DIRECT 8-bit index** into a 166-entry (`$00..$A5`) word table at `$C3:E000`, with **no range check**.
The `AND #$00FF` only truncates the 16-bit `LDA [$58],Y` read — bits 6 and 7 of the id are live. Measured
with `--callrom C2/1CDA`: `A=$FF75` and `A=$0075` both return `$F836`, `A=$0035` returns `$E66D`.
`LDA $C3E000,X` (`BF 00 E0 C3`) occurs **exactly once in the whole ROM**, so op 0x30 is the only writer of
that pointer.

The port had `obAnimTbl[op[1] & 0x3F]` — a fabricated **6-bit** mask into a hand-written 64-entry stub
with FOUR non-zero slots. Both halves wrong. The mask aliased every era id `$75..$7B` onto `$35..$3B`,
all zeroed holes, so the placard object got anim pointer 0 and **nothing could ever be drawn**. And the
blast radius was never only the placard: across the eight captured overworlds the live objects use twelve
distinct table entries and the stub reproduced exactly one (`$22`).

### The descriptor

Each table entry points at a one-op anim script `04 <descriptor16> 00`, and the descriptor is the piece
list (decoder C2/0D08, drain C2/0CEA):

```
+0  u8 N                                    piece count
+1  N x { s8 dX, s8 dY, u16 word2 }         word2 bits0-8 = OBJ tile NAME, 14 = H-flip, 15 = V-flip
```

The seven era placards, dumped straight out of the ROM:

| id | script | descriptor | N | pieces (dX, tile) |
|---|---|---|---|---|
| `$75` | `C3:F836` | `C3:F852` | 3 | −12 `$1AC`, +4 `$1A6`, +20 `$18A` |
| `$76` | `C3:F83A` | `C3:F85F` | **2** | **0 `$1A4`, +16 `$18A`** |
| `$77` | `C3:F83E` | `C3:F868` | 3 | −12 `$1AC`, +4 `$1AA`, +20 `$18A` |
| `$78` | `C3:F842` | `C3:F87E` | 4 | −12 `$1AC`, +4 `$18C`, +20 `$18E`, +36 `$188` |
| `$79` | `C3:F846` | `C3:F88F` | 3 | −8 `$1A0`, +8 `$1A2`, +24 `$188` |
| `$7A` | `C3:F84A` | `C3:F89C` | 3 | −12 `$1AC`, +4 `$1A8`, +20 `$18A` |
| `$7B` | `C3:F84E` | `C3:F875` | 2 | −12 `$1AC`, +4 `$1AE` |

`CtWorldAnimFrame` returned N but decoded only **piece 0**, so every one of these was truncated to its
left cap. It now takes a piece index (`CtWorldAnimPiece`).

## 3. The prior note's GAP was false

The 2026-09-03 note said *"no savestate has `$7F:01F0..01FF` != 0, so the oracle only ever shows the '?'
placard; 600 A.D. has NEVER been observed"*. Both halves are wrong. The gate is `$7E:1BAD` bit 0, not
`$7F:01F0`, and **`saves/com_jogo_salvo.state` reaches it**: loc `$01F1`, storyline `$15`, `$1BAD = $01`.
Its OAM at y=192 is exactly

```
idx0  x=8   tile $1A4  attr $31
idx1  x=24  tile $18A  attr $31
```

= descriptor `$F85F`, base 8 + dX 0 and +16. **The real 600 A.D. placard, observed.** Six of the seven
placards are reachable with states already in the tree; only `$77` (world 2) is not, because its gate is
the storyline `>= $36` and nothing in the tree is past `$15`.

## 4. The show gate, measured

Not an idle timer. `--monitor 7E1BF7:1 --from saves/com_jogo_salvo.state --btnscript "0:20,80:60,0:200"
--diff --writer`:

- **C2/36E7 `STZ $1BF7`** zeroes it on every player move (f21, f29, f37, … while walking)
- **C2/1229E `INC $0000,X`** ramps it +1 per frame once the player stops
- `$1BF7` reaches `$78` (120) at **f165** and the placard's OAM appears at **f166**

The port's `owIdle >= 84` had the wrong mechanism and the wrong number.

> **CORRECTION (2026-09-07).** The sentence below in §5 -- *"the anim pointer IS the gate -- an object
> whose script has not reached its op 0x30 has no era descriptor and nothing is drawn"* -- is WRONG. The
> `op 0x30` runs at the TOP of the script, before the `4C` test, so BOTH arms carry the same pointer
> (`$F836`) and the block is indistinguishable between them. The real gate is the ROM's per-frame DRAW
> LIST: only the SHOW arm's `op 0x39` ticks the anim sub-VM (C2/0E1D), and only that tick reaches op 4's
> `JSR C2/0B53` enqueue. Derived and ported in **`ow_placard_draw_list.md`**; the placard used to show
> the whole time because of exactly this.

## 5. What the port does now

- `src/world.c` op 0x30 is `CtWorldAnimPtrForId(op[1])` = the ROM's own `$C3:E000[id]`, unguarded, over
  `ctOwAnimC3` — which was **already in the tree**, byte-for-byte `$C3:E000..FFFF`. `obAnimTbl` is gone.
- `CtWorldAnimPiece(B, i, …)` walks the whole piece list.
- `src/field.c` finds the class-`$0F63` attr-`$31` object, resolves its anim pointer back to an era id
  through the ROM's table (`CtWorldAnimIdForPtr`), walks the descriptor's pieces for the strip's span,
  and blits pixels captured per era id from the oracle's isolated OBJ layer
  (`tools/_gen_ow_placard.py` → `src/data/ow_era_placard.h`). `era_hud.h`, `eraIdx[]`, the dialogue-font
  fallback and `owIdle >= 84` are all deleted.

Only the PIXELS are baked, because this port keeps no overworld OBJ VRAM. They are keyed by **era id**,
not by tile name: the tile names are per-world VRAM **slots**, so the same name is different art in
different worlds — cropping `$1AC` from world 0 and from world 1's "?" disagrees in 381 of 1024 bytes.

**Byte-exact on world 0**: the 577 pixels the blit plots are identical to `saves/ow_w0.state`'s own
frame, 0 differing, while the map around them already differs by 66–86% for unrelated reasons.

## 6. Open

1. **Colour math.** The ROM composites this OBJ *inside* the world's colour math; this host stamps it
   after `worldCompose`. Measured (isolated OBJ vs the full render of the SAME state, placard pixels
   only): world 0 `cgadsub=44` → **577/577 identical**, so world 0 is exact; world 1 `cgadsub=04` →
   **151 of 321 differ** (`B5B5AD` → `C6C6CE`, `949C84` → `9CA5A5`), and the deltas are not uniform, so
   the blend is against the map behind it and no flat bake can reproduce it. The fix is to composite the
   placard before the math pass.
2. **The port's world boot picks the arm too early.** Seeding `com_jogo_salvo`'s WRAM (where `$1BAD` = 1)
   still parks the port on the "?" arm: traced with `CT_PLACARDDBG`, **no op 0x30 executes at all** and
   the block keeps the pointer the boot resolved, so the op-0x27 gate was evaluated before the loaded
   save's `$1BAD` was live. A world-load ORDER defect, separate from the placard.
3. **Era `$77` (world 2) has never been observed** — gate `$7E:1BA6 >= $36`, and every state in the tree
   is at storyline `<= $15`. Its pixels are therefore NOT baked and the port draws nothing rather than
   invent them. Settle it with any late-game snes9x state converted by `tools/s9x_to_lake.py`.
4. Which era each descriptor SPELLS is only confirmed for `$75` ("1000 A.D.", it matches the old
   `era_hud.h` capture) and `$76` (rendered and read). The rest are identified by world, not by reading
   the glyphs.
