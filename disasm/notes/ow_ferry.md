# The ferry (Truce <-> Porre) -- the world's own bootstrap

User report 2026-09-28 (translated from Portuguese): "when travelling by boat, the text 'Ticket office' stays; in the original game it does not" and "on appearing in
Porre and asking to ride the boat again, the animation goes back to Truce".

## 1. The pay (field)

Both offices end their clerk's pay in the SAME change of location:

| office | scene | talk object | pay | direction bit | then |
|---|---|---|---|---|---|
| Truce | 18 (loc $012) | obj8 fn1 @0037 | `CC 0A 00 0E` / `CE 0A 00` (10 G) | `65 82 F0` = $7F:01F0 bit 2 | `E0 F0 03 3F 28` |
| Porre | 56 (loc $038) | obj9/10/11 fn1 (three invisible counter points round the clerk obj8) | `CC 0A 00 12` / `CE 0A 00`, `BB 16` ("Here you go") | `65 83 F0` = $7F:01F0 bit 3 | `E0 F0 03 3F 28` |

`E0 F0 03 3F 28` = loc $1F0 (world 0) at the 8-px cell (63,40) = px (504,320), which is the Truce office's own entrance
cell on the overworld (entrance record 7: cell col 31 row 20, name 13 "Ticket Office", dest $012). The Porre office's
entrance is record 23 (cell 24,59, dest $038). So the arrival tile says nothing about the direction -- only the bit does.

## 2. The crossing (overworld)

C2/26D6 copies $7F:01F0..01FF to $1BA7..$1BB6 at the world load, and world 0's bootstrap ($7F:0400, the one object C2/04F7
allocates) branches on $1BA7: with bit 2 or bit 3 set it spawns the boat and the camera scripts INSTEAD of the interactive
set. Oracle, 300 frames into the Truce crossing: blocks $0B30 $0C30 $0CB0 $0CF0 = $0F63 (scripts) and $0C70 = $75C3
(the BG3 drift) -- no $3404 (lead), no $3264 (entrance scanner), no $5628 (name banner), no $2F94 (music zone); $027C = 0,
$1B32 = FFFF. The scripts sail, pan the camera (op $13 on $E3/$E7 + op $3E's C2/09C5 rebuild, then the C2/0568/066C
steppers), clear $7F:01F0, and leave with op $05 (C2/1100) into the far office: Truce -> loc $038 tile (53,44), Porre ->
loc $012 tile (6,60).

Measured under the rule (CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1), from the pay's A:

| | $0100 = 1F0 | lit | fade-out | far office | lit |
|---|---|---|---|---|---|
| Truce -> Porre | c36 | c82 | c1796 (1714 lit) | c1827 ($038) | c1867 |
| Porre -> Truce (A to close "Here you go" at +64) | c94 | c140 | c2110 (1970 lit) | c2141 ($012) | c2183 |

## 3. The location-name banner is two objects

* **$1B32** (the entrance under the lead, record offset or FFFF) has ONE per-frame writer: the scanner object, class
  **$3264** (C2/3264): the cell of $0283/$0285 through C2/32CD, forced to FFFF while $0282 bit 1 is clear (C2/32A6).
  C2/3349 -> C2/6263 sets **$1B58** = the record's name id (`LDA $7E7E02,X`).
* The world load (C2/2C1D) zeroes $1B58 (C2/2C32) and sets $1B32/$1B34/$1B36 = FFFF (C2/2C44-2C4D).
* The banner is the object of class **$5628** (C2/5628): state 0 waits for $1B58 != 0, renders the name from $C6:F400 into
  $7E:8621 (JSL C2/57DF / C2/5823) and queues it to VRAM $1E00/$1F00; it hides again when $1B58 returns to 0.
* Both are spawned by the bootstrap's INTERACTIVE branch only. A cutscene world (the ferry, the attract's flyovers) has
  neither, so it has no banner and no entrance to enter.

## 4. The pad

The lead's handler reads the pad, and only in free roam: C2/3567 `LDA $027C / CMP #01 / BNE C2/35E0` (the tick only)
guards the d-pad AND the A that enters a location (C2/35E5 `LDA $1B32`). A world with no $3404 block reads no pad at all.

## 5. The clouds: the drift object's counter and its slot

C2/75C3 (world 0) / C2/75FD (world 1): `INC $0022,X`, `DEC $24` when +22 & 3 == 0 (and `INC $2A` when & 7 == 0 / with the
DEC), then `$24 += $1BF1`, `$2A += $1BF3`, both STZ, CLC. $1BF1/$1BF3 are STORED (not added) by the lead at its own slot:
C2/3725 `LDA $001C,X / STA $1BF1 / LDA $0020,X / STA $1BF3` (its velocity) -- and by the vehicles (C2/46B0, C2/4FB9); the
script steppers never touch them. Everything is a block of the ONE object pass, run in block order.

The drift block is spawned by op $35 in the bootstrap's pass (C2/048A: first free block from $0C30, +2/+3/+4 zeroed, MVN of
the parent's +05..+3F) and, being a later block than its parent, runs in that same pass: oracle, `$0C92` (+22 of the
$0C70 block) = 0001 on the spawn frame c76 (writer C2:75D1).

## 6. What the port did (until 2026-09-29) and what it does

* `OW_FERRY_BOAT`: a 64-byte capture of the Truce->Porre boat ($0C30 of a snes9x state) installed over $0C30 -- the
  bootstrap object itself -- for any overworld entry at (63,40) with bit 2 set; a `riding` mode that froze input, pinned the
  camera to the boat and zeroed the entry latch; and at pc >= $09D5 a walkable-cell scan that dropped the party on the
  Porre pier. Consequences: the fade-in object was never spawned (1380 black frames), the ride ended on the pier instead
  of in the Porre office, and -- bit 2 never being cleared by anything -- the Porre pay (bit 3, same tile) replayed the
  Truce->Porre boat ("the animation goes back to Truce"). REMOVED; the bootstrap plays both directions.
* `--loadwram` used to force the captured object array for EVERY later world entry; now only a seed that is itself an
  overworld ($0100 >= $1F0) does. A field seed's world entry bootstraps like the game's.
* The host's entry latch (the banner and the A-to-enter) ran from the host mover, which sat on the Truce office's entrance
  cell where the pay lands: the "Ticket Office" banner over the whole crossing. Now: no $3264 block -> latch FFFF (the
  load's value); no $5628 block -> no banner; no $3404 block or $027C != 1 -> no pad (field.c `owPad`).
* The BG3 drift and the per-line wave were ticked in CtWorldStreamFrame, BEFORE the object pass: on the spawn frame the
  port's +22 did not step, so world 0's clouds ran one frame behind for every entry (the crossing: every fourth frame one px
  apart). Now CtWorldAmbientFrame runs them at their blocks, the lead's $1BF1/$1BF3 store lands at the $3404 block, and
  the drift counts in the block's own +22. The read-only validators (CtWorldCamFrame) keep the one-call world-0 order.
  The same fix, measured where it mattered most -- world 2, the attract's 1F2 leg (ash: C2/7702 + C2/7759): against the rule's
  cartridge from power-on (`--monitor 7E0100:2,7E0020:2,7E0026:2,7E0024:2,7E002A:2 --from boot`) and CT_OWCAMTRACE, loc 1F2 is
  c7914.. and f7855..; at port = cartridge - 93 all 289 port frames carry the cartridge's $20/$26 and $24/$2A (288 differ at
  -92 / -94). The cartridge's first drift is the spawn frame c7947 and c7948 REPEATS it (the pass running the camera's op $3E
  rebuild spans a displayed frame) -- the old port matched only because its missed spawn frame cancelled that repeat, and
  ow-bg3-2300ad-ash-drift-and-wave-vs-oracle had pinned the value one frame early (FEA2/00CA = c8048; t101 pairs with c8049 =
  FE9E/00CC). The wave's odd lines stay the known $01E2 seed (p - 3 against c8049).
* Op $20 / $21 (C2/1420 / C2/1444, branch on any / no bit of block byte & mask) were missing: an object of the ferry's
  bootstrap parked on `20 22 80 53` at $7F:0A3D.

* --worldtest walked from the entry frame, into a world whose bootstrap had not yet spawned the lead nor run its `13 7C 02 01`;
  its drive is now timed from the first frame the pad is read (field.c wtPad0). The location-entry fade keeps its shape from
  the press (ow-location-entry-fade-vs-oracle re-anchored +33), and the return column $0107 now reads 45, the cartridge's.

Result (`python tools/ferry_ride_cmp.py`): both crossings 0 differing frames over the whole overworld stretch (1791 and
2047), same lengths (1714 / 1970), same far office. Open, not the ferry's: the Truce office after the Porre crossing
lights one frame early in the port (the field load clock, [[ct-load-clock-floor-rule]]) and its walking NPC and the
clerk's pose run a frame apart after that.

## 7. The fixtures (saves/ferry_re, gitignored) and how they were made

* `ow_at_truce_office.state`: saves/ow_w0.state walked by an oracle best-first search (8-px probes) to (496,328), path
  `DDLLLLDDDRRRRDRRRRRRR`; then right 8, up 8 -> (504,320) shows the banner; A enters loc $012.
* `office_live.state` -> `office_at_clerk.state`: tools/_walkto.py to (120,927) (the clerk at (72,918) was out of the ROM's
  28x12 action box from the cells tried -- beside the counter and in front of the desk; how the real game reaches him was
  not pursued. The prompt state pokes him beside Crono: `PEEK_POKE=1810=80,1811=06`, then `40:2,0:10,100:4`).
* `truce_pay_prompt.state` = the "It's 10 G to Porre. Hop aboard?" box, cursor on Yes. `truce_ride_end.state` = 2004
  frames after the A, inside the Porre office.
* `porre_office_at_counter.state`: truce_ride_end walked to (888,672) (beside obj10); `80:2,0:10,100:4,0:80` opens "A
  one-way ticket is 10G. Want one?" = `porre_pay_prompt.state`.
* The `.lakewram` seeds are the same two office states ("#lakewram\nRAM:131072:" + the 128 KB WRAM).

## 8. The direction bit after the crossing (2026-10-03)

The user: after the Truce -> Porre ferry, leaving the Porre office by its door played the boat leaving Truce again.
C2/24D5 (the world's state 2, every exit into a location: op $05's and an entrance's) `JSR C2/26F0` at C2/24E0: `LDX
#1BA7 / LDY #01F0 / LDA #000F / MVN $7F,$00` -- the world's flag block back to the story page, the reverse of the load's
C2/26D6 (one other caller, C2/2518). The crossing's script clears its direction bit in the BLOCK (C2:12FC, op $05's
tail), so only this copy clears $7F:01F0. Cartridge (rule) from truce_pay_prompt: `$7F01F0 = 04` f15 (C0:6BA0),
`$1BA7 = 04` f36 (C2:26E5), `$1BA7 = 00` (C2:12FC) and `$7F01F0 = 00` (C2:26FF) f1825. The port copied in but never back:
bit 2 stood in Porre and the next world entry took the bootstrap's crossing branch. Ported (field.c, the world exit);
check ow-ferry-clears-its-direction-after-the-crossing.
