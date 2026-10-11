# The battle's pad latch and the 4-frame MENU PASS

Every key the battle menu takes goes through two ROM pieces: the NMI's edge latch (every frame) and the main loop's
menu pass (every fourth frame). The port used to take each key on the frame it came, with its own hold-repeat; it now
runs both pieces (src/battle.c `battlePadLatch`, `battleMenuPass`, `battleMenuPassKey`, `battleMenuInput`).

All measurements: the rule's oracle (`CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1`), saves/battle_re/gato_wait.state
made into Lucca alone by `tools/battle_ab.py pokes_for([2])` (PEEK_POKE `26A7=63,26A8=00,26A9=63,26AA=00,2832=08,
2839=FF,2980=02,2981=80,2982=80`), input `100:8` (the A that starts the fight) then the listed presses.

## 1. The NMI: C2/84D2, then CD/0340

Both battle NMI paths end in `JSL C2/8002` (CD/091A, CD/09C6) then `JSR CD/0340` (CD/091E, CD/09CA). C2/84D2 turns the
auto-read word into the decoded held word `$F8` (player.c `CtJoypadPoll`; `CtJoypadHeldWord` computes the same word
without writing `$00F0-$00FF`). CD/0340-0398, 16-bit A:

```
D0342 LDA $CC99 / STA $CC97        prev := last frame's word
D0348 LDA $F8   / STA $CC99        cur
D034D ORA $CC97 / BNE D035D        nothing held, before or now:
D0352 STA $CC97 / LDA #000F / STA $CC9B     latch 0, the repeat delay 15
D035D LDA $CC97 / AND $CC99 / BNE D0373     nothing held on from last frame:
D0365 EOR #FFFF / AND $CC99 / STA $CC97     the new presses
D0373 DEC $CC9B / BNE D0365        a key held on: the new presses, until the delay runs out --
D0378 LDA #0001 / STA $CC9B / LDA $CC99 / STA $CC97   then the whole held word, every frame from then on
D0384 LDA $CC97 / ORA $EE / STA $EE          ORed into the latch $EE/$EF
D038E (8-bit) LDA $CC97 / BPL / LDA #18 / STA $CC9B   an A in the latch: the delay 24
```

The decoded bits (the default config `$2993-$299A` = `80 08 40 08 02 04 10 20`, battle_ui_sfx_sites.md 2.4): `$EE`
A 80, X 40, L 20, R 10, B 08|02, Y 01, Select 04; `$EF` Up 08, Down 04, Left 02, Right 01. The auto-read is the frame
before's controller: a press held over frames N..N+1 reaches `$EE` at N+1 (A f652 -> `$EE = 80` f653; B f680 -> `0A`
f681; Down f692 -> `$EF = 04` f693).

Hold: Down held from f692: `$CC9B` 0F at f693, 0E f694 ... 01 f707, the repeat f708 (`$CC9B := 1`, the latch 0004) and
every frame after (`$EE` re-armed f709, f713, f717, f721 -- each consumed by the next pass).

## 2. The pass

CF/E73B-E745, in the NMI's frame service: `INC $9931 / LDA $9931 / AND #03 / BNE / INC $993A` -- a request every fourth
frame (CC/F0E6 restarts `$9931` at every action). The main loop consumes it in three places, all
`LDA $993A / BEQ / STZ $993A / JSR C1/1B67 / JSR C1/0C2D / JSR C1/1153 / JSR C1/17DD`:

* **C1/10C6** -- the idle loop's frame B, right after C1/10C4 `INC $E5` and before the iteration returns to C1/8130
  (the dead-flag sweep, the timers, the cursors, the handlers); it ends `STZ $EE`.
* **C1/10FA** -- the action loops, each after its frame wait: the pre-roll C1/4943 (x8, C1/489B-48B0), C1/40A3 (before
  `INC $A013`), the script loop C1/40B0-40CE (C1/40B6), the `$5D9B` wait C1/40E1-40EA (C1/40E4), C1/485E/4867/4890.
  `--hookpc C110FF` over GATO's first action: a visit every fourth frame, the last before the tail.
* **C1/10E3** (an RTL; no caller found by byte scan).

From C1/4116 on (CC/F06B, the C1/305C loop, the waits C1/414B / 41B4 / 41B7) there is no pass: a request raised there
waits for the idle loop (oracle: `$993A` raised f603, `$A013` cleared f606, consumed f608 by C1:10CE). In the idle loop
the request raised on frame A is consumed on frame B (`$993A` f639 by CF:E748, f640 by C1:10CE; every 4 frames).

## 3. C1/1153: one key per pass

`JSL CF/FAE2` (the item-return service), then `LDA $95D5 / BPL` (no chooser: C1/179C), `$9609` -> C1/1561 (the target),
`JSR C1/1115` (the bracket), `$95DB` 1 -> C1/1320 (tech list), 2 -> C1/143D (item list), else the command row C1/1184.
Each handler tests the latch in a fixed order and acts on the FIRST key it finds; every path leaves through C1/179C
`STZ $EE / STZ $EF` -- the other keys of the same pass are dropped.

| handler | order |
|---|---|
| C1/1184 command | X (`$EE & 40`, only with `$A0D4`: `INC $A114`, `$95DC,X := 0`, JMP C1/127B), Left (`$EF & 02`, C1/11E1 the previous ready PC), Up (08, C1/1250), Down (04, C1/1264), Right (01, C1/1218), A (`$EE & 80`, C1/127B) |
| C1/1320 tech | A (C1/1369), B (`$EE & 08`, C1/138F), Up\|Left (`$EF & 0A`, C1/13AD), Down\|Right (05, C1/13F4) |
| C1/143D item | A (C1/1498), B (C1/14DB), Up\|Left (C1/14EC), Down\|Right (C1/1502), L (`$EE & 20`, C1/151C), R (`$EE & 10`, C1/1537), `$A0D0` (C1/154B) |
| C1/1561 target | (C1/1F79 rebuilt; `$9613` bit 7 -> the cancel tail), B (C1/156E), A\|X (`$EE & C0`, C1/161A), `$960C` bit 7 skips the nav, Down\|Right (C1/176C), Up\|Left (C1/1786) |

## 4. Timeline (the oracle)

| press | latch | pass | effect |
|---|---|---|---|
| Down f640-641 | `$EF = 04` f641 (CD:038B) | f644 (C1:10CE) | `$95DC = 01` (C1:126B) |
| A f652-653 | `$EE = 80` f653 | f656 | `$95DB = 01` (C1:12E4); the description posted at the next pass f660 |
| A f664-665 | f665 | f668 | `$9609 = 01` (C1:138C) |
| A f676-677 | f677 | f680 | `$9609 = 00` (C1:1629), `$95D5 = FF` (C1:1745), `$95DB = 00` (C1:1719); `$A013` f684 |

## 5. What follows for a drive

* A press lands on the first pass after its NMI -- 1 to 4 frames after it, never on its own frame.
* Two presses that reach the latch between the same two passes are ONE key (the first in the handler's order); the
  other is dropped. Presses 4 frames apart can merge where no pass runs between them (the action's tail from C1/4116,
  the enemy's C1/4058 think frames).
* An A that reaches the latch on the frame B where a PC becomes ready is dropped: the pass (C1/10C6) runs before the
  cursor (C1/82A2 -> C1/8511) that enqueues him, finds no chooser and C1/179C clears the latch.
* Holding a key repeats it at every pass after 15 frames (24 after an A).

## 6. Port

`battlePadLatch` runs in battleTickBody's NMI part from the battle's first frame (the pad word: the host's
`h->joyAutoRead`, or headless the CT_BATTLE_SCRIPT button / CT_HOLDBTN mask of the previous frame). `battleMenuPass` =
battleMenuPassHead (C1/0C2D: the tech list's grey pass, resolver and description), battleMenuPassKey (the table above,
then C1/179C), battleMenuInput (the handlers), battleMenuPassTail (C1/17DD: the bracket, the Att picker's name box). It
runs at the idle frame B (after the victory gate, before the timers) and on every `$A013` frame until the action's tail
(`h->menuPassHold`, set where the port counts C1/4116). CT_BATTLE_MASHA, the headless masher, now sets the A bit for
every idle pass that has a chooser.

Measured against the oracle (tools: scratchpad tb2/tb3/tb4_cmp.py; check field-battle-topbox-tech-description): the
latch (`$EE`, `$CC97-$CC9C`) and `$9931`/`$993A` are the cartridge's frame for frame; the list opens on the pass after
the A. On the gato_wait Lucca fixture the port's pass falls one frame before the cartridge's because its iteration
parity is one frame off: GATO's first action runs f450-f603 in the port against f453-f606 (the enemy dispatch's
pre-existing lead), so the idle loop restarts three frames early and frame B lands on the raise frame.

## 8. The confirmed command's dispatch (ported)

C1/1561's confirm (C1/161A) only queues: `$93EE,X` bit 6, the FIFO `$99D4-$99D6` / `$99D8`. The command runs from the
ATB's handler C1/8461 (C1/B92D[12], an odd `$0027` pass): the cursor's entry `$B163[$B185 - 1]` -- a ready PC
(C1/8511 -> C1/851B FD/AB30 -> C1/854F, in its turn already or enqueued just now, falling into C1/8570), or an enemy /
the FF entry that does not take its turn while a PC is pending (C1/84EC `$B188 | $B189 | $B18A` bit 7 -> C1/8570);
a PC entry that is not ready leaves by C1/8518 -> C1/860A. C1/8570 pops `$99D4` (C1:85B1) and C1/B967 runs the command:
C1/4058 `INC $A0FD` (C1:405B), C1/405F's wait, C1/4062 `STZ $E5`, C1/4090, C1/40A0's wait, `INC $A013` (C1:40A9).
Oracle (Lucca): confirm f680; f682's tail ($0027 = 4D) pops the FIFO; `$E5 = 00` f683 (C1:4064); `$A013` f684.
Port: battleTurnHandler12 arms `h->pcThinkDelay` (2), battleThinkTick runs battleDispatchScheduled when it expires; the
fixed latency CT_BATTLE_DISPATCH_LAT is no longer on this path.

## 9. OPEN: the port begins the module two frames early (measured, not ported)

The field's side of a fight, the rule's oracle: op D8 `$0117 = 21` (C0:3AA8) -> the next frame C0/1770 (bit 5): `TRB`,
the align loop C0/1781 (C0/885A; already aligned: C0:88A4 clears `$0138` at once), C0/1790 AF4E, FD/FFF7, C0/EC60 ->
the branch's return frame -> C0/188C (bit 0): C0/024C (the list `$019D`, C0:063E) and C0/0440 (the `$29B0` records,
C0:045A), C0/2C41, C0/00DE, C0/EC60 -> `JSL C1/0000` (C0/18A7). gato_wait: D8 f218, `$0138` f219, records f221, the
module's first stores f222 (`$1E01 = 45` C0:1BC9, `$CA1E = 00` D1:F2AF). The 0AD: f1740 / f1741 / f1743 / f1744. D8 + 4
both. The port (field.c fieldBattleConsumer) stages at the end of the return frame (= the start of the C0/188C frame,
right) but begins the module there too -- two printed frames early (its sound 45 and `$CA1E` at f220).

A two-frame hold (C0/188C's frame and C1/0000's, no field main-loop leg) puts GATO's band seed on the cartridge's
f296 exactly -- and moves every demo battle two frames later, because the port's boot after C1/0000 is wrong in two
places that cancelled the entry: C1/0000 -> the band seed is C0/1BAB's upload (`$1E00` posted -> cleared by C7:0196)
plus the setup passes, cartridge / port:

| fight | upload | setup (clear -> seed) | total |
|---|---|---|---|
| GATO (song 45) | 59 / 55 | 15 / 19 | 74 / 74 |
| 0AD (the demo's `70 01`) | 14 / 13 | 17 / 19 | 31 / 32 |

The port's `1 + apu + 18` is the formula; the upload is the emulated driver's handshake (4 frames short on the song),
the setup passes are CPU work (the NMI-off pricing class). With the entry fixed alone, 28 cartridge-graded attract checks
drift two frames per demo battle. Next step: price the upload and the setup against the cartridge (both classes are the
zelda3 model's), then take the entry.

## 10. The command row per seat, the chooser, the cursor memory and the stash (ported 2026-09-30)

**The rows.** `$95DC,X` is each seat's command row (0 Att, 1 Tech, 2 Item), X = the seat:
- the enqueue sets it from the seat's cursor memory, C1/1B37-1B3B `$95DC,X := $9916,X`, and marks `$A6DF := FE`
  (C1/1B83-1B85);
- Up / Down move the CHOOSER's row (C1/1250 / C1/1264, X = `$95D5`);
- the L/R switch (C1/11E1 / C1/1218) cycles `$A6DD` and, at C1/11FE-1215 with X = the new seat and Y = `$95D5`, hands the
  old chooser's row to the new one (`$A0D4` = 0), or sends the old seat's row back to its memory (`$A0D4` != 0);
- the confirm, C1/16A8-16D4: unless the X shortcut chose (`$A114`), `$9916,X := $95DC,X` (X = `$95D5`); with `$A0D4` = 0
  all three rows and memories are zeroed; then `STZ $A114 / $960E / $9614`.

**The switches** (CC/E39C-E3A9): `$A0D4 := $2991 & 40` (the command row), `$A0D5 := $2991 & 80` (the Tech and Item lists:
the boot's CE8D2 loop, the confirm's C1/163B tech memory and C1/169D, which clears `$95E5/$95E6` only with `$A0D5` = 0).
Every save in the repo has `$2991` = A0: the list memory on, the row memory off. The item list opens at `$95E6` without
clearing either byte (C1/12ED-131A, behind `LDA $A0A7 / BNE`, the whole byte).

**The chooser.** `$95D5` is written by the pass head, not by the switch: C1/0C49-0C5C (with `$95DB` = 0) compares `$A6DD`
with `$A6DF`; when they differ, `$A6DF := $A6DD` and C1/0299 runs, whose C1/0560 (with `$A6DE` != 0, `$A86B` = 0,
C1/0547/054C/0564) sets `$95F1 := $A6DE - 1` and `$95D5 := $A6D9,$A6DD` (the first queued seat when that slot is empty,
C1/057A-0586). So after an L/R, `$95D5` follows one pass later -- oracle (Crono, Marle, Lucca from gato_wait): `$A6DD`
f655 (C1:1224), `$95D5` f659 (C1:058D). The other writers are the confirm's C1/1732 and the dequeue's C1/1C2D.

**The stash** (disasm/notes/battle_target_select_pc_side.md law 2): a target group with bit 7 (Tonic, Aura 80; Lapis 81)
runs C1/1FA2-1FB9 on the builder's first pass -- `$A86B := $95DB + 1`, `STZ $95DB`, `$A6DF := FE`, `INC $A09A` -- and the
bottom window becomes the base (enemy names + party). While it holds, the pass head's rebuild takes C1/054C-055E:
`$A62D != $A0D7` (`$A0D7` = FF from every command A, C1/127B) -> `$A6DF := FE` again and C1/08E8 repaints the `$A62D`
row lit (attr 0x29, the rest 0x2D). B (C1/1577-158C) and the confirm (C1/161A-1623) bring `$95DB` back from `$A86B`.
The cancel tail also zeroes `$9614` and `$960E`.

**The list during its target select.** C1/1369 / C1/1498 open the target select without touching the window, so the
Tech or Item list stays up ($95DB kept) for every group without bit 7. The tech path of the pass head clears the list's
hand (C1/0EBA) and draws it back only with `$9609` = 0 (C1/0E5A-0E8A). The names are the records' 11 raw bytes
(C1/0B57-0B96: CC:15C4 + 11 x id, MVN to `$94A0`), an element icon first where the name has one (Lightning: 0x2F).

**Grading** (tools/battle_ab.py `cursor` / `strip`, both sides from the same WRAM, the rule's oracle): the menu bytes
(`$95D5 $95DB $95DC-DE $9916-18 $A6DD $95E5/6 $9609 $A6DF $A86B`) 14/14 in value and in timing (from each side's first
change) over three drives -- the L/R copy, the Tech list + Aura stash with both cancels, the Tonic stash with its cancel --
and the strip pixel for pixel on Cyclone's target select (f690) and Aura's (f700). Checks: battle-menu-cursor-per-seat-,
battle-menu-target-stash-, battle-menu-item-stash-, battle-hud-tech-target-strip-, battle-hud-aura-stash-strip-vs-oracle.

**Measured, not the menu's:** after an action the port answers a key one pass later than the cartridge, because its
`$0027` is one main-loop iteration ahead (cartridge `$0027` = 89 at f703, port at f700), so the dispatch's odd-`$0027` pass
(C1/8461) falls elsewhere; and the bracket's grey pulse `$A4EF` starts 3 frames early (cartridge f297, port f294), from the
band's arm. Both are the entry/boot item of sec. 9.

## 11. The item list (ported 2026-09-30)

**The staging** (C1/CDFF-CE35, FD/AF80, FD/B01C): C1/CDFF clears `$1580..$1A7F` (256 five-byte records); C1/CE14 walks 256
inventory slots (`$2400,X` / `$2500,X`, `$08` wrapping): an empty slot is FD/B01C's empty record; FD/AF80 (`STZ $AF23`)
writes a battle-usable consumable (id >= BC, FD/AFD9; CC/0ABC[sub*3] bit 7, FD/AFF2) as [id, CC/1BB5[sub], CC/0ABC[sub*3]
& 7F, count, 0]; anything else takes no record (FD/B02B `INC $AF23`). FD/AF85's `CMP #5A` goes to FD/AFD7 both ways (the
equipment arm is dead). So the list keeps the bag's gaps and order.

**The rows** (C1/095D -> C1/09B0 per row, record `$95E6 + k`): drawn only with an id AND a count (C1/09B2-09BA): a spent
stack is a blank row. The name is the record's bytes (CC:0B5E + 11 x id), then ':' (0x5F), the count, "Item".

**The cursor** (C1/143D, one key per pass: A, B, Up|Left, Down|Right, L `$EE & 20`, R `$EE & 10`, then the `$A0D0`
redraw C1/154B): `$95E5` is the row (0..2), `$95E6` the window's top record (0..FA). Up on row 0 scrolls up one (C1/17A1),
Down on row 2 down one (C1/17BF, not past FA), L pages down 3 (C1/151C, to FA), R up 3 (C1/1537, to 0). A (C1/1498) reads
record `$95E6 + $95E5` (8-bit, x5 by C1/0089); `$1582,X` bit 7 or `$1583,X` = 0 does nothing; else `$9F35 / $960D` and
`STX $9F36` (the offset, 16-bit), and the confirm's C1/1685 `LDX $9F36 / DEC $1583,X`. The refund C1/B6D1 scans all 256
records for the first with the id.

**The arrows and the hand** (C1/18B0-1976, the pass tail): the up arrow (`$0EFC`, row 2 col 30, attr 29) unless `$95E6` =
0, the down arrow (`$0FFC`, row 6 col 30, the same glyph with attr A9 = V-flipped) unless `$95E6` = FA; the hand at `$95E5`'s
row, rewritten only when `$95E5` / `$95E6` change (`$991F` / `$9920`) -- so it stays up through the item's target select.

Graded (the rule's oracle, both sides from the same WRAM, a bag with a gap and a spent stack): the menu bytes 14/14 over L,
R, Down and Up scrolls; the strip pixel for pixel just opened and scrolled to record 3; Lapis chosen from record 4 restores
both seats with the cartridge's span. Checks: battle-menu-item-list-scroll-, battle-hud-item-list-gap-strip-,
battle-hud-item-list-scrolled-strip-, battle-item-scrolled-record-vs-oracle.

## 12. The enqueue is two steps; the action's wipes set the pass's phase (ported 2026-09-30)

**The enqueue** (C1/856D `LDA #01 / JSR C1/0003` -> C1/1B19, after C1/8562 `LDA #01 / STA $93EE,X`): C1/1B1C `LDA $A6D9,X /
BPL` (boxed already), C1/1B21-1B2B `$93EE + CC/FAF0[seat] |= 80` (81 with C1/8562's 01), C1/1B2E-1B34 `$95D6,$95DA := seat`,
C1/1B37 `$95DC,X := $9916,X`, C1/1B3E `INC $95DA`, and the ready chime C1/1B41. The seat is PENDING: its box `$A6D9,X` is
C1/1B67's, the FIRST call of every menu pass (C1/10CE / 10ED / 1104): the list's head gets `$9F38,X := 0`, the Tech list's
row and top back from their memory (`$95DF,X := $A863,X`, `$95EB,X := $A866,X`), `$A6D9,X := X`, `$A6DF := FE`, the list
shifted (`$95D6 := $95D7`, `$95D7 := $95D8`, `$95D9 := $95D8` -- as written), `DEC $95DA`, `INC $A6DE`, and `$A6DD := X`
only while it is negative. The teardown's pending arm (C1/1BB6-1BD5) takes a seat off the list with no box. Oracle, the
lifeline fixture under the rule: `$93EE = 81` by C1:1B2E and `$95D6 = 00` by C1:1B41 at c1131, `$A6D9 = 00` by C1:1B83 and
`$95D6 = FF` by C1:1B9D at c1135 -- the pass after.

**The pass's phase** is `$9931`'s (CF/E73B, `$993A` every fourth NMI), and CC/F06B restarts it (CC/F0E6 `STZ $9931`) at the
action's two wipes: C1/405B (the open, C1/4058 -- two waits, C1/405F and C1/40A0, before C1/40A6 `INC $A013`) and C1/4119
(the close, C1/4116 -- before the target walk's waits to C1/41BA `STZ $A013`). Each runs ONCE per action. The port ran the
close's wipe twice (at C1/4116 for a script-ended tail and again at the act-end, three frames later) and the open's on the
`$A013` frame; both moved every later pass. Now `battleOpenWipe` runs where the port arms C1/4058's two waits
(`pcThinkDelay`, an enemy's think at 2) and the act-end does not repeat C1/4116's. Graded on the rule's oracle: gato_wait
Lucca (Down / A -> the Tech list) -- `$9931` restarts c439 / c591, `$993A` every raise, the list c656, the description
c660, `$A013` c441 / c594, all identical; the lifeline fixture identical from GATO's first action to the box at c1135.
On the old valve both fixtures run a constant 3 frames early (the enemy dispatch's pre-existing lead), each event at its
cartridge distance from the action.

## 7. Open

* C1/8570-8585 (`LDA $93EE / BPL / STA $B188`, x3 seats): the port's pending latch is still `pcInTurn`, not `$B188`
  (the cartridge: `$B188 = 81` at c364, kept through the PC's action; FD:A934 clears it at a death).
* Before the first action the boot's `$9931` runs one behind the cartridge's (lifeline fixture under the rule: 87 / 88 at
  c420), so a box enqueued there can come two frames early (Marle c430 / c432). MEASURED (both switches, offset 228):
  the cartridge clears `$9931` at c281 (CF:FB9A), posts #08 at c284 (C7:0196), arms the band at c285 (`$CA1E = FF17`,
  CD:0512) and its NMI's CF/E73B / CF/E5AA count from c285; its band steps from c286 (`0116`, two CD/05C4 steps an NMI,
  CD/08F5 + CD/08F8). The port arms and makes its first step in one tick (c286), so its steps and everything after them
  meet the cartridge's while its counters start a frame late (c286). The fix is the boot tail's order inside the port's
  tick (the arm is main-thread work of the frame before the first step) -- shared with the attract's boot under the
  rule (battleBootBandArm), left to that migration.
* C1/10E3's caller.
* The end-of-battle write-back of the cursor memory (`$2C7C..`, `$2C98/99`) is not checked.
