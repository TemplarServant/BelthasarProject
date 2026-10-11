# Doors on step, prio-0 occluders, and the decision box

Three unrelated field defects, all reported together.

---

## 1. Doors open by INCREMENTING their layer-1 metatiles

User report (translated from Portuguese): *"doors like the one in scene 4 are not opening when walking through"*.

Two halves, both in the ROM.

### The latch — C0/9D35, inside the player-collision commit

```
9D35: LDA $73 / AND $1F / XBA / LDA $71 / AND $1E / TAX   ; index = (row << 8) | col
9D3F: LDA $7E7000,X
9D43: BIT #10                       ; prop1 bit 4 = the DOOR flag
9D45: BEQ ->9D52                    ; not a door
9D47: STX $015B                     ; remember which tile
9D4A: LDA $0117 / ORA #10 / STA $0117
```

`$1D71`/`$1D73` are the tile **ahead in the direction of travel** — C0/9C5E and C0/9C70 already
compute them into `$1D70`/`$1D72` (`$72 = tentY ± 0x100` by the sign of the Y velocity), and the
port already had that. `$1D1E`/`$1D1F` are the map's wrap masks, the same pair `collFetchEngine`
applies to `$1D67`/`$1D69`.

`collision.c` carried this whole leg as a comment — *"the C0/9CA1 touch-trigger / door-on-step
side-state is NOT modelled here"*. And the interactive field never fed it the flag: the asset has
carried prop1 bit 4 since `pack_collision` (zflags bit 3 → packed z-byte **bit 5**), but
`fieldTileFetch` synthesised `*b1` from bit 7 alone.

⚠ The host must also seed `$1D1E`/`$1D1F`, which it never needed before (its fetch hook takes the
tile hi-bytes directly). Unset, they mask every door index to (0,0). Measured, oracle vs the map
dims: 32 → `0x1F`, 48 → `0x3F`, 64 → `0x3F` — the next power of two minus one, per axis (loc 004
32x32 = 1F/1F, loc 200 64x32 = 3F/1F, loc 005 48x64 = 3F/3F, loc 401 64x48 = 3F/3F, loc 129/157
64x64 = 3F/3F).

### The consumer — C0/0D3E

```
0D3E: BIT #10 / BNE ->0D45          ; ($17 with D = $0100)
0D45: LDA #10 / TRB $17             ; clear the pending bit
0D49: JSR C0/28C0
0D4C: LDX $5B
0D4E: LDA $7E3000,X                 ; the L1 METATILE at the latched tile
0D52: CMP #E6 / BEQ ->0D78
0D59: CMP #EC / BEQ ->0E5F
0D60: CMP #EE / BEQ ->1014
0D67: CMP #FA / BEQ ->11C9
0D6E: CMP #FC / BEQ ->1454
0D75: BRL ->16DC                    ; not a door metatile it knows
```

Every handler is the same shape — `INC A / STA $7E3000,X` over a small block, walking X by ±1 (a
COLUMN step; the index is `(row << 8) | col`) and by −0x100 (one ROW up). So **open = closed + 1**
in the metatile table, and the block shape is what distinguishes a 1-wide door from a 2-wide one:

| id | tiles | block |
|---|---|---|
| 0xE6 | 2 | (r,c) (r-1,c) |
| 0xEC | 4 | (r,c) (r,c+1) (r-1,c+1) (r-1,c) |
| 0xEE | 4 | (r,c) (r,c-1) (r-1,c-1) (r-1,c) |
| 0xFA | 6 | (r,c) (r,c+1) (r-1,c+1) (r-1,c) (r-2,c) (r-2,c+1) |
| 0xFC | 6 | (r,c) (r,c-1) (r-1,c-1) (r-1,c) (r-2,c) (r-2,c-1) |

It is a ONE-SHOT — the bumped ids match no case, so a second step is inert — and it does **not
close**: 60 frames of walking away leave the door open (measured). The ids survive until the
location reloads, which is the same "opened doors" room state the VM's `$7E` lifetime note lists.

**Measured** on loc 004, `--monitor "7E320A:2,7E0117:1,7E015B:2" --writer` walking into the top door:
```
f16  7E0117=10 (C0:9D52)   7E015B=0A04 (C0:9D4A)      <- 0x040A = row 4, col 10
f17  7E320A=F3F5 (C0:1212)                            <- inside the 0xFA handler
f21  7E0117=00 (C0:0D49)                              <- consumed
```
and the full WRAM diff across the open shows exactly six changed bytes in `$7E2000-7FFF`:
`$7E320A/320B F2→F3 F4→F5`, `$7E330A/330B F6→F7 F8→F9`, `$7E340A/340B FA→FB FC→FD`.
**Nothing in the collision planes moves** — the door tile's walkability is unchanged.

Port: `CtFieldSceneDoorOpen` (scene.c) bumps the block; field.c consumes `$0117` bit 4 next to the
scripted 0xE4/0xE5 tile copies and shares their re-bake (a door IS a tile copy — one the collision
path asks for instead of a script). The +256 bank bit lives in the top bits of our `l1t` word, and
the ROM's INC is 8-bit on the plane byte, so it must not carry into it.

---

## 2. A prio-0/1 occluder only cuts what it is IN FRONT of

User report (translated from Portuguese): *"sprites in scene 200 look strange, check it (scene 401 similar effect)"*.

On hardware OBJ-vs-OBJ is settled **first**, by OAM index, and only the winning pixel's priority is
then weighed against the BGs. So a `$0F80` priority-0/1 sprite blanks a sprite pixel back to the map
**only where it is in front of it** — and front/back is the very same `(group, Y)` key the draw list
is already sorted by (`fieldPrioLayer`: `$0F80 & 0x0C` → gC at OAM 0x10 front, g0 at 0x70, gB at
0x1A0 back).

The port collected those occluders into a side array and cut **after** every sprite was drawn, i.e.
unconditionally, with a `pmask && !ctlLocked` patch to stop them eating the player. Scenes 200 and
401 have three companion sprites at `$0F80 = 0x04` — group B, the BACKMOST group — co-located with
the knight, Crono and the princess, and they ate most of all three.

**Oracle** (`o200.raw.oam`, same camera): the companions are OAM entries **104-115 at OBJ priority
0** — i.e. behind the characters at entries **28-43 (priority 2)** — and all three characters draw
whole. 104 × 4 = 0x1A0 and 28 × 4 = 0x70, the two group bases exactly.

Fix (structural): occluders stay **in** the sorted draw list and cut at their own turn, so
everything painted before them (behind) is blanked and everything after paints back over the hole.
That also retires the `ctlLocked` / player-mask exception — the intro blanket (`8E 08` = group C,
the FRONT group) still cuts sleeping Crono with no special case, and `render-scene1-snapshot`'s fnv
is unchanged by the rewrite. Scene 18's counter creature (`$0F80 = 0x11`, the player's own group)
now resolves against the player by Y, which is what the old exception was approximating.

---

## 3. The decision box is the ordinary dialogue window

User report (translated from Portuguese): *"when the dialogues ask for options, like in scene 434"*.

Op 0xC0 (C0/3643 → C0/3674) reads its two operands as:
```
367C: LDA $7F2001,X / STA $2A        ; operand1 = the STRING index
3683: LDA $7F2001,X / STA $D9        ; operand2 = the option-line config
3689: AND #03 / STA $65              ; $65 = LAST option line
368D: LSR / LSR / AND #03 / STA $64  ; $64 = FIRST option line
3695: LDA #01 / STA $62              ; decision mode
```
which the VM port already had right. Two RENDER defects, both measured at Norstein Bekkler's lab
(loc 1B2, `C0 40 03` → four options):

1. **The window was auto-sized** to the page's line count and the 80-row texture 9-patched down to
   fit, so a 2-line prompt got a 53px box. The oracle's prompt page and option page occupy the
   **same rows**, and the window texture's 80 rows map 1:1 onto them — verified pixel-for-pixel
   against `saves/ct_window.bin` (fb row = window row + 16 on a 240-row peek dump).
2. **The cursor was a hand-drawn triangle.** The ROM draws a pointing **hand** into the window: the
   oracle's OAM at the option page holds only the party sprites, so it is no OBJ, and it is not the
   MENU's hand either — `src/data/menu_itemdetail.h` (OBJ tile 0x20) agrees on only 63 of 138 opaque
   pixels. `src/data/dlg_cursor.h` is the real one, lifted from an oracle frame by subtracting the
   window texture (`tools/gen_dlgcursor.py` — the same fb-lift method `T_WINDOW` itself uses). Its
   cell is 16x12 at window (16,10): x 16 is one glyph cell left of the six-space option indent,
   which lands at x 32.

---

## Repro / pins

```
# doors
ct.exe --teleport 4 10 6 0 --from saves/truce_market.state --out o4.state --frames 240
ct.exe --monitor "7E320A:2,7E0117:1,7E015B:2" --from o4.state --input "10:40,0:20" --frames 60 --writer --diff
python tools/dis.py 9CA1 0x90 ; python tools/dis.py 0D3E 0x50 ; python tools/dis.py 11C9 0x50
# occluders
ct.exe --teleport 200 39 8 0 --from saves/truce_market.state --out o200.state --frames 240
ct.exe --peek o200.state o200.raw --btnscript "0:60" ; python tools/ct.py oam o200.raw.oam
# decision box
ct.exe --teleport 434 8 8 0 --from saves/truce_market.state --out o434.state --frames 240
ct.exe --peek o434.state o434e.raw --btnscript "10:8,0:6,100:6,0:180,100:6,0:60,100:6,0:60,100:6,0:120,100:6,0:120"
python tools/gen_dlgcursor.py o434e.png saves/ct_window.bin src/data/dlg_cursor.h
```
Checks: `field-scene4-door-opens-on-step`, `field-scene200-prio0-occluder-order`,
`field-scene434-decision-box` — five mutations, all killed.

## Open questions

- The door handlers also `STZ $60 / JSR C0/1B90`, which stages sound command 0x19 with `$FC` as its
  parameter (`C0/1B90: LDA $FC / STA $1E01 / LDY $97 / LDA $0A00,Y / STA $1E02 / LDA #19 / STA $1E00`).
  Where `$01FC` gets the door's sound id was not chased — the port opens the door silently.
- The five metatile ids are the whole of C0/0D4C's dispatch, so they should cover every door in the
  game, but only loc 004's 0xFA door has been driven end-to-end. The other four blocks are read off
  the disassembly, not off a render.
- Our option lines sit ~1-2px lower than the oracle's within the same window; the glyph row origin
  (`y0 = top + 9`) has not been re-derived against the ROM's text layout.
- The oracle-side dialogue drive is fragile: A presses only register once the script's own MOVETO
  beats finish (loc 1B2's box does not open until f99), so any capture script has to be timed
  against `--monitor 7E0129:1` rather than guessed.

## The decision box, frame by frame against the oracle (2026-09-06)

User: *"when the dialogue gives me options to choose, on selecting the choice I get the impression that another
text 'blinks' quickly / as if my press of A skipped a dialogue besides selecting the option."*

Repro fixture built through the ROM's own loader, so the render is gradable too:

```
ct.exe --teleport 434 8 8 0 --from saves/truce_market.state --out saves/field_re/s434_decision_lake.state --frames 240
ct.exe --monitor 7E0129:1,7E012A:1 --from saves/field_re/s434_decision_lake.state --diff --frames 976   --btnscript "10:8,0:6,100:6,0:180,100:6,0:60,100:6,0:60,100:6,0:120,100:6,0:100,100:6,0:200"
```

| station | oracle | ours |
|---|---|---|
| string 0x3F opens (`$0129` 1 -> 5) | f99 | f99 |
| typewriter / waiting / dismissed / gone | 114 / 191 / 201 / 212 | 115 / 195 / 203 / 215 |
| decision string 0x40 (op 0xC0) opens | f395 | f391 |
| typewriter / prompt waits | 410 / 455 | 407 / 455 |
| page turn `08 09 0A 0B 0C` | f459-463 | f459-463 |
| option page types / waits | 464 / 515 | 463 / 527 |
| the pick closes it / gone | 565 / 576 | 567 / 579 |
| string 0x42 opens / waits / dismissed / gone | 583 / 627 / 771 / 782 | 583 / 631 / 771 / 783 |

**One box per press on both sides**: the prompt page is turned by its own press and the option picked by
the next, and exactly ONE box follows the pick. **The reported skip did not reproduce here.**

The mechanism it would take is known and is the thing the check guards: `C0/3635`'s completion arm
clears the object's own latch `$7F0A00,X` before advancing the IP. Leave it set and the next textbox op
on that object takes the COMPLETION arm on its first visit -- no box posted, the op just advances, the
line is swallowed. That is exactly "a text flashes and my press skipped a dialogue". Pinned as
`field-decision-box-flow-vs-oracle` with that as its mutation: KILLED.

The one station we are slower on is the OPTION page's typewriter, 64 frames against the oracle's 51.
It is recorded here and not yet derived.

Still open: the report itself. Scene 434 is the only decision box with a driveable repro on hand; a
savestate of the choice the user actually saw would settle it the way `marle_no_caminho_bell.state`
settled the bell (see [[ct-field-bell-minigame]]).
