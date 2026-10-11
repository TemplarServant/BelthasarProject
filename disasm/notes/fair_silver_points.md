# The fair's Silver Points -- the tally, the {num} it prints, and three ops the port lacked (2026-09-24)

User request: *"implement the silver points count in leene's square"*. The count is not a subsystem of its own:
it is three general mechanisms -- a global<->local copy, a poke into the text's number slot, and the dialogue's number
code -- and the port was missing one half of each.

## 1. Where the count lives

| what | where | ROM |
|---|---|---|
| the persisted count | `$7F0052` (story-flag bank) | written by op 0x58 (C0/69C4) |
| the scene's working copy | local `$1B` = `$7F0236` (loc 5), local `$21` (loc 439), local `$0B` (Gato) | loaded by op 0x53 (C0/693D) |
| the number a box prints | `$7E0200` | poked by op 0x4C (C0/680C) |

loc 5 (Leene Square), obj0:

```
fn0  0000  53 52 00 1B      loc[1B] := byte $7F0052        (C0/693D, 8-bit store)
fn3  01CE  12 1B C8 02 06   if loc[1B] >= 200 ...          (the cap)
     01D3  BB 11            "Can't carry any more Silver Points."
     01D5  4F C8 1B         loc[1B] := 200
     01D8  58 1B 52 00      byte $7F0052 := loc[1B]        (C0/69C4)
```

A game win adds to `$1B` and CALLs obj0 fn3 (the bell: `02 00 33`). The keepers print it:

```
obj18 (Tent of Horrors)  050B  4C 00 02 7E 1B   byte $7E0200 := loc[1B]   (C0/680C)
                         0510  BB 33            "Spend your {num} Silver Points here!"
loc 439 obj (exchange)   0694  4C 00 02 7E 21 / C0 49  (the Silver Points exchange question)
```

Seven scenes poke `$0200` this way (2, 5, 71, 223, 225, 277, 439; 223/225 with the 16-bit op 0x4D).

## 2. The number code

The dialogue decoder's control table is C2/5903 (`JMP ($5903,X)` at C2/5900); 0x0D / 0x0E / 0x0F go to C2/5953 /
5985 / 59C8. Each reads 1 / 2 / 3 bytes little-endian from `[$3D]` and advances `$3D` past them; C2/0003's init
C2/57DF points `$3D` at `$00:0200` (C2/5815 `LDX #0200 / STX $3D`, `$3F` = 0) ONCE PER STRING, so a string with
several `{num}` reads consecutive bytes (four such strings in the ROM, e.g. the Petal/Fang/Horn price list). The value
is split into 3 / 5 / 8 decimal digits (C2/615E / 6180 / 61BD, restoring division by the powers of ten) and C2/5DAD /
5D91 / 5D56 drop the leading zeros down to one digit -- the plain decimal. `$02A1` != 0 selects C2/5CC0's hex arm
instead; the oracle reads `$02A1 = 00` at the keeper. The first digit is drawn in the control code's own iteration
(C2/5C3E), exactly as a name code's first letter is (C2/5B15 -> C2/5BF5), so the port expands `{num}` to its digits
before layout, like the names.

## 3. What the port lacked, and the fix

* **The init pass (`runFunc`) had no op 0x53 or 0x58.** fn0 runs through the init resolver at the load, so `$1B`
  started at 0 on every entry, and the first win wrote 1 OVER the saved count. The captured fixtures
  (`s5_bell_marle.lakewram`) come with the locals already loaded, which is why the bell checks never saw it.
* **The live VM had no op 0x4C/0x4D** (0x4A/0x4B were paraphrases). The keeper's poke was stepped over.
* **The dialogue carried a literal `{num}`** (assets/text.py `_NAMES`).

Fixes (structural, shared bodies graded by `--sweepops`):
* move.c `CtVmPokeFamily` (0x4A-0x4D, C0/67D7..6841) and `CtVmVarFamily` (the variable grid 0x50-0x56/0x58/0x59,
  C0/68C6..6A11) are extracted from `CtFieldRunnerOne`, which calls them; the live VM's `step()` routes 0x4A-0x4D and
  the grid's EIGHT-BIT members (0x51/0x53/0x55/0x56/0x58) to them, and so does the init pass. The 16-bit members stay
  on the live VM's paraphrase: its local WORD model is big-endian (r16/w16, the CTViewer convention ops 0x13/0x15
  read back) where the ROM and the shared body are little-endian -- open, separate.
* assets/text.py emits the render-time marker `\x01` + code for 0x0D-0x0F (the names' scheme); `CtResolveNames`
  reads `$0200 + cursor` from the scene's WRAM when the box opens.

## 4. Measured

Oracle, `saves/field_re/tent_keeper_lake.state` (= `s5_bell_marle.state` + `--teleport 5 41 31 0`, Crono under obj18
facing up), `--monitor 7E0200:1,7E02A1:1,7F0052:1,7F0236:1 --btnscript 0:10,100:2,0:120 --writer`: f0 `$7F0052 = 01`,
`$7F0236 = 01`; f11 `$0200 = 01` (C0:6824); `$0129` 01 -> 07 f11-f127. `--peek ... --frames 152`: the box reads "Spend
your 1 Silver Points here!". The port, from a fresh `--scene 5` load with that state's `$7F0000-01FF`
(`saves/field_re/fair_sp1.flags`): f0 `$1B = 01`, f10 `$0200 = 01` (the walkscript's one-frame input phase), the same
text. Bell (`s5_bell_marle.state`, `0:10,10:24,0:10,100:4,0:153,100:4,0:155,100:4,0:116`): f373 `$7F0236 = 02`
(C0:6C81), f374 `$7F0052 = 02` (C0:69E6); the port on a fresh load: f372 / f376.

Pinned by `fair-silver-points-shown-vs-oracle` (4 mutations) and `fair-silver-points-tally-persists-on-entry` (2).

## 5. Open, seen here

* The keeper's box has FOUR lines; the port's window shows three and waits for A before the fourth, where the
  cartridge types all four (the line cursor `$0131`, textbox.md -- unported).
* The port draws the closing quote with the opening glyph (`0xE1`/`0xE2` both become `'"'`).
