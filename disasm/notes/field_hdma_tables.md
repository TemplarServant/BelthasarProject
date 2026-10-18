# FD/C2C1 — the field's HDMA table builder (native since 2026-10-18)

`JSL FD/C2C1` builds, once a frame, the eight HDMA channels' tables of ONE of two sets in bank $7F; the NMI's FD/C1EE
(field_window_wipe.md, "The channel") points the channels at the other set. Native in `src/hdma_table.c`
(`CtHdmaBuildNative`), called by `CtHdmaTableBuild` (src/ct_vm.c) at every site the ROM calls it. Before 2026-10-18 the
port ran the ROM's instructions translated (src/xlat/hdma_fd.c) on the vendored 65816 core; that translation is now ct.exe's
reference only (`--hdmafuzz`), and Belthasar does not link it.

## Callers (each with D = $0100, DB = 0, M = X = 1; the routine reads no register it does not set first)

| ROM | what | port |
|---|---|---|
| C0/00C7 | the field main loop, every frame | ct_vm.c CtSceneTick |
| C0/00DE | the fades' frame (CtSceneReturnWaitFrame, ctXferStyleFrame C0/1770, C0/264F's 32 frames) | ct_vm.c |
| C0/0B28 | C0/2848, the location load's fade entry | field.c |
| C0/2824 | the menu / shop resume fade-in, the X-menu fade-out (C0/1949) | field.c |
| C0/0AFF | the battle's exit tail (D1/F4C0 -> JSL C0/0005), twice, with `$53` bit 7 set | battle.c |

## The routine (disassembled with a flow-following M/X-tracking pass: 1840 instructions, FD:C2C1-D52C)

```
C2C1 LDA $53 / BIT #01 / BNE C2D3
C2C7 LDA $26 / ASL A / TAX / JSR ($C2E5,X) / LDA #01 / TSB $53 / RTL     ; bit clear: SET B
C2D3 LDA $26 / ASL A / TAX / JSR ($C2DF,X) / LDA #01 / TRB $53 / RTL     ; bit set:   SET A
FD:C2DF  C2EB C995 CFCF | FD:C2E5  C847 CD0C D27E
```

`$0126` = the textbox window: 0 none, 1 top, 2 bottom (field_textbox_y_flip.md; C0/1985 `EOR #03` flips 1 <-> 2 and never
touches 0). `$0127` = its height 0..$28 (field_textbox_unpark.md: the open / close ramps step it by 4). Modes 1 and 2 with
`$0127` = 0 `BRL` into mode 0 (C995 / CD0C / CFCF / D27E). Set A's table runs on into set B's, so `$0126` = 3..5 under bit 0
set would reach set B's handlers -- the native port keeps that; past index 5 (set A) / 2 (set B) the ROM jumps into code
bytes, which no writer of `$0126` produces: the port builds nothing and the caller flips the bit (as before the builder).

Every handler: `PHB / LDA #7F / PHA / PLB / REP #10` -> an absolute operand is **$7F**:xxxx (the tables, `$0351`, `$3467..`,
`$22C0`), a direct-page one $7E:01xx (`$21`, `$53`, `$D9`, `$DB`, `$EE`), a long `$00xxxx` one $7E:xxxx (`$0127`, `$1D87..`,
`$1DF9`, `$1DFB`, `$1DFD`, `$0BE0`). Note the parity byte channel 7 reads is **$7F:0351** (`LDA $0351` under DB = $7F,
C439 / C477) -- not the wipe's DP $0300 counter $7E:0351. The fuzz below proves which byte the ROM reads (every byte of
both banks random): a builder reading $7E:0351 differs.

### The two sets

Set B's channel tables sit $2B8 past set A's; within a set the eight channel tables are $57 apart:
ch0 $0F80/$1238, ch1 $0FD7/$128F, ch2 $102E/$12E6, ch3 $1085/$133D, ch4 $10DC/$1394, ch5 $1133/$13EB, ch6 $118A/$1442,
ch7 $11E1/$1499. Entries are `<count> <addr16>` (these channels are indirect: FD/C1EE programs DMAP 44 43 43 43 44 41 41 40),
`00` ends a table. Per-set data pointers: ch1 $1508/$150C, ch2 $1510/$1514, ch3 $1518/$151C, the window wave $7F:1D27/$1DA7,
ch5 $17C8,$1890,$17D6,$18CA,$1534 / $1AC8,$1B90,$1AD6,$1BCA,$1536, ch6 $152C,$1530,$1538 / $152E,$1532,$1600.

### Mode 0 (C2EB / C847): no window
`64 p 70 p 00` (100 + 112 lines from one place) on ch0 ($14F0), ch1, ch2 (C4B5 / C52E: by the 8-bit `$1DFD`), ch3 (by the
8-bit `$1DF9`), ch4 ($1520), ch6; ch5 = `E4 $17C8 F0 $1890 00`; ch7 (C439 / C477) = `E4 a F0 b 00` with a/b = $18A8/$190C
or $17C8/$182C by $7F:0351's parity (set A: even -> $18A8; set B: odd -> $18A8). Then `$53` bit 7 (the battle's exit tail):
C401 / C95D copy the battle's six words $7F:3467..3471 into ch1..3's data and `$21` / $7F:22C0 into $152C.. (ch6's data);
else the field tail C5A7 / C6F7.

A channel the wave owns (ch2 by `$1DFD`, ch3 by `$1DF9`) is instead C34E / C4D7 / C550 / C8AA: thirteen `90 <wave>`
(16 lines each, repeat mode: one dword a line from $7F:wave..) and `00`.

### Modes 1 (C995 / CD0C, top) and 2 (CFCF / D27E, bottom), n = `$0127`, all 8-bit arithmetic
- ch0, ch1, ch3: mode 1 `07 p | 29-n p | 2n p2 | 64-n p | 1E p | 00`; mode 2 `64 p | 1D p | 29-n+1 p | 2n p2 | 29-n-1 p | 00`
  (p2: ch0 $14F4; ch1 $14F8 mode 1, $1500 mode 2; ch3 $14FC mode 1, $1504 mode 2).
- ch3 with `$1DF9` set (CA8A / CE01 / D0CA / D379): the same runs over the wave as a variable-length list at $1085,Y /
  $133D,Y -- CC5E / CCB8 split a run into `90` entries of 16 lines plus one `80|r` remainder, each pointed at the wave's dword
  `$EE & 0F` ($01EE counts the lines handed out, $01EF zeroed by `STY $EE`); the 2n run is a plain entry ($14FC / $1504).
  With a large n the list runs past ch3's $57 bytes into ch4's table, which is written AFTER it: the store order is kept.
- ch4: the box's middle lines from $1528 `n-1` (at most 10: `CMP #0A / BEQ / BCC`), $1524 for 2 lines (n <= 10) or 2n-20,
  $1528 again -- around mode 1's `07 / 29-n ... 29-n / 64-n / 1E` and mode 2's `64 / 1D / 29-n+1 ... 29-n-1`.
- ch5: mode 1 `87 $17C8 | (29-n)|80 $17D6 | 2n $1534 | (64-n)|80 D9 | C7 D9 | 00`; mode 2 `E4 $17C8 | 9D $1890 |
  (29-n+1)|80 $18CA | 2n $1534 | (29-n-1)|80 D9 | 00`; D9 = DP $01D9 (a word) = the running address, +2 a line of each run
  (`$01DB` holds the run's count).
- ch6: `.. | (2n-1)|80 ((28-n)&3F)*2+$1538 | 01 $1530 | ..` -- the box's per-line colour math (textbox.md).
- then ch7 and the field tail (modes 1 / 2 do not look at `$53` bit 7).

### The field tail (C5A7 / C6F7)
`$1D87 / 89 / 8B / 8D` (16-bit) -> ch1's / ch2's data; if the 16-bit `$1DF9` and `$1DFD` are both 0, `$1D8F / 91` -> ch3's
data; else the WAVE (C5E2 / C732): `$1DFB` += 1 (8-bit), X = `$1DFB & 3E`, sixteen `LDA $C0FF30+4k,X / ADC $001D8F /
STA wave+4k` (**no CLC**: the first ADC takes the carry ch7's `LSR A` left -- $7F:0351 bit 0 -- each next one the last
one's), sixteen `$1D91` at wave+2+4k, then `MVN 7F,7F` copies the 64 bytes to wave+$40. Finally `$21` -> $152D/$152F and
`$0BE0` -> $152C,$1530 / $152E,$1532. The offsets C0:FF30.. are the ROM's own table, read from the user's ROM at run time.

What moves the wave: `$1DF9` is a location property (restored by the menu's reload from $7F:1D26, C0/01FF); the End of
Time (Jackie's ch08 save, loc 1D0) has it. `$1DFD` = $11 is set by the field effect `$0139` = 0C (C0/21EE's 12th entry,
C0/264F: `$1DFD` = 11, 32 frames of C0/00DE).

## Validation (2026-10-18)

- **Against the cartridge** -- `python tools/hdma_build_cmp.py` (check `field-hdma-builder-native-vs-cartridge-census`):
  `ct.exe --hookpc FDC2C1` with `CT_HOOK_DUMPCALLS` / `CT_HOOK_DUMPEXIT=FDC2D2,FDC2DE` dumps the oracle's (rule on) WRAM at
  every call's first instruction and at its RTL; `ct.exe --hdmabuildcmp` runs the native builder on each entry image:
  all 128 KB equal outside the stack page, and with the tables and DP bytes poisoned twice every byte the builder writes is
  the cartridge's. Ten drives -- the tent keeper's and the Naga-ette's boxes (mode 2), Mom's box flipped to the top
  (mode 1), the attract around loc 16E's wipe, the 07D demo battle's exit tail (`$53` bit 7, two calls a frame inside the
  NMI), the End of Time (wave on ch3 + tail), `$0139` = 0C (wave on ch2), the wave under both boxes (the run lists), and
  `$7F:0351` = 1 (poked: ch7's other pair and the wave's carry). **1487 calls, every one exact; 240k written bytes, none
  wrong.**
- **Against the translation, every branch** -- `ct.exe --hdmafuzz 40000 1` (check
  `field-hdma-builder-native-vs-translation-fuzz`): random 128 KB images (both sets, `$0126` 0..5 / 0..2, `$0127` 0..FF, the
  wave flags low / high bytes, `$53` bit 7, `$0351` parity) through both; the translation runs every path it lacks on the
  core (interp = 1), so it is the ROM's own code. 0 images differ; all 60 reachable branch keys of each of the six handler
  pairs. (300,000 images over three seeds, 0 differ, at the port.)

## Open
- Nothing in the routine is open. Whether any location ever leaves $7F:0351 odd was not looked for (no state in hand has
  it: every real drive above had it even); the odd path is graded on the cartridge by the poked drive and by the fuzz.
- field_window_wipe.md ("The channel") and the comment over field.c fieldWipeWindow tie channel 7's parity to the wipe's
  counter `$0351` (C3/01E4 runs with D = $0300: that is **$7E**:0351). The builder reads **$7F**:0351, a different byte, so
  the two channel-7 pointer pairs never move with the wipe -- set A $18A8/$190C, set B $17C8/$182C -- which is what
  fieldWipeWindow's code already does (its "the two sets' POINTERS never move", measured). Only the prose is off; not
  changed here (field.c belongs to another stream of work).
