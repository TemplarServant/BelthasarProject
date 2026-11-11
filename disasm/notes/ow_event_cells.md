# The overworld's event cells: `$0280 = 1` (2026-10-17)

ROADMAP G1a item 3 (`$0280 != 0`, C2/345D's second row). Port: `src/world.c` (CtWorldLoadTriggers, obEntryScan,
owEventMember, the native C2/79C8, op 2C's fractions), `src/field.c` (the tables at the world load, the host's mover handing
the lead to the pass while `$0280` / `$0281` are set). Checks: `ow-event-cell-heckran-whirlpool-vs-oracle`,
`ow-event-cell-600ad-scene097-vs-oracle` (`tools/ow_event_cmp.py heckran|w1`). Follows [ow_vehicle.md](ow_vehicle.md).

## 1. The tables (C2/2A44)

The world's header byte +15 indexes C6:FFC0 (3-byte pointers); the packet, decompressed into `$7F:0400` before the world's
script replaces it, holds four lists, each a count byte and its records, copied through `$2180`:

| List | WRAM | Record | Count |
|---|---|---|---|
| entrances | `$7E:7E00` | 7 bytes: cell word (bit 7 of its low byte arms it), name, destination, place | `$1B38` |
| event cells | `$7E:8300` | 3 bytes: cell word, script index | `$1B39` |
| transition cells | `$7E:8180` | 3 bytes | `$1B3A` |
| event scripts | `$7E:8480` | words: `$7F` addresses | -- |

Every world has one event cell and one transition cell (the copy loops are `DEY / BNE`: a count of 0 would copy 65536).
Only two event cells are ever armed (op 44 sub 1, by the world's bootstrap) and no transition cell is (op 44 sub 2 is used
nowhere):

| World | Cell | Armed when | Script | Reached by |
|---|---|---|---|---|
| 0 (1000 A.D.) | (1B, 19) | `$1BA7` bit 0 (`$7F:01F0`, set by scene 02F) | `$7F:113F` | scene 02F's `E0 F0 05 37 32` -- Heckran's whirlpool |
| 1 (600 A.D.) | (1A, 1F) | `$1BAA` bit 4 (`$7F:01F3`, set by scene 097) | `$7F:0B91` | scene 097's `E0 F1 03 35 3F` |

Both cells are water: the scene's own exit is the only way onto them. `$7F:01F0` bit 0 stays set, so world 0's cell is armed
at every later load -- harmlessly.

## 2. The scanner (class $3264, C2/3264)

`$0281 = $0280`, then by `$0280` (C2:3275): 0 -- on a cell the lead has just reached (`$0282` bit 0: C2/34F8 at its init and
C2/3766 after a step set bits 0 and 1 when C2/39AE finds bit 2 on the cell's top pair of properties), the 16 x 16 cell into the
three tables (C2/32CD: `$1B32` / `$1B34` / `$1B36`, FFFF for none) and C2/3349; with `$0282` bit 1 clear, `$1B58 = 0` and the
three FFFF. 1 -- an event runs: once its script sets `$1B43`, `$0280 = 0`.

C2/3349 (`$1B44` = bit 0 entrance, bit 1 transition, bit 2 event): an entrance names the banner (C2/6263, `$1B58`); a
transition cell sets `$1B47` and `$027C = 4` (C2/250D, not ported -- unreachable); an event cell (C2/626E) spawns its script
(`LDA #7F / LDX $1B45 / JSR C2/04D9`), clears `$1B3B-$1B40`, disarms the record and sets `$0280 = 1`.

## 3. The members (C2:345D / 3B58, second rows)

The lead (`$0280 = 1`, its phase reset by C2/3419 when `$0280 != $0281`): phase 0 (C2/3772) waits for `$1B3B`, then runs
it INLINE on its own block (pc, bank 7F, C2/0F63) and takes C2/39D5's properties; phase 1 (C2/37A1) runs it each frame, copies
the block's place to `$0283/$0285`, and on the script's end (SEC) goes to phase 2 (nothing). A follower the same with
`$1B3B + 2 x slot` and `$0283 + 4 x slot` (C2/3D9C-3DE3; the slot read as a word, +24/+25). The script names the
sub-scripts with op 08 by member id (C2/786F copied the party to `$1BA3`), then pans the camera (op 32) and sets `$1B43`.
When `$0280` falls back to 0 the lead's phase is 0 again: C2/34F8 from `$0283/$0285`, and the host's mover restarts there.

The arcs use the native C2/79C8 (gravity: `+1E/+20 += 0x2000`) and op 2C (C2/15C3) -- whose `STZ $0012,X / STZ $0016,X`
clears the fractions with the place: the port kept them, and 600 A.D.'s second arc landed a pixel low.

## 4. Graded, and what is left

No battery save stands on these cells: `tools/ow_event_cmp.py` edits Jackie's ch04 / ch10 slot 0 (the place at slot +5F5 /
+5F6, the story bit in the copy of `$7F:0000` at slot +603, the sum recomputed) and loads it on both sides -- the same world
entry the scene's exit makes, at the same cell. From `$0280 = 1`: every change of `$0280`, `$0283-$028E` the same, frame for
frame, to the end (+265 on world 0, +193 on world 1); the party's sprites the cartridge's.

`$027C = 1` comes at +32 from the event on both sides and the pinned pictures are the cartridge's (0 px), since 2026-11-11
(sec. 5). Until then the port's event started a pass late (+31) and the world's own clocks (the sea's palette cycle, 600
A.D.'s BG3 mist, the gulls) stood a frame apart from it in every picture -- filed here as the load clock. It was not.

## 5. The lead's init is its block's own visit, in the pass that spawns it (2026-11-11)

The world's bootstrap allocates the lead (op 35, class `$3404`) and the followers (`$3AE2`) in its first pass. Their blocks
come after the script's, so that same pass visits them (C2/0535 walks the table in order and reads each class word when it
gets there). The lead's first visit is phase 0 of C2:345D's first row:

```
3439 LDA $027C / CMP #01 / BEQ ->3449 / CMP #00 / BEQ ->3449 / JSR C2/0E1D / CLC / RTS
3449 LDA $0280 / ASL x3 / STA $00 / LDX $4E / LDA $0002,X / ASL / ADC $00 / TAX / JMP ($345D,X)
345D 34F8 3567 36ED 0000 / 3772 37A1 37BB 0000
34F8 SEP #20 / LDX $4E / INC $0002,X / LDY $0100 / CPY #01F5 / .. / STA $000F,X / STZ $0010,X
3515 LDA $0283 / STA $0014,X / LDA $0285 / STA $0018,X / STZ $001A,X .. $0020,X / JSR C2/3ACB
3530 STZ $0026,X / LDA $0104 / AND #03 / STA $0028,X / STZ $1B5A / JSR C2/391C / .. / JSR C2/3987
354C .. JSR C2/39D5 / JSR C2/39AE / BCC ->3567 / LDA #03 / TSB $0282
3567 LDA $027C / CMP #01 / BNE ->35E0 / .. (the input frame) / 35E0 JSR C2/0E1D / CLC / RTS
054C REP #20 / INC $0003,X                      ; the pass's tail: a CLC visit is counted
```

The init falls into the input frame -- with `$027C` = 0 (the script has not opened free roam yet) the tick alone -- and the
scanner's block comes later in the same pass: an armed cell under the arrival point starts its event in the pass that made
the lead.

Measured on leg 67's end (the 600 A.D. map after scene 097; the blocks `$0DF0` / `$0E30` / `$0E70`): the cartridge's first
frame with the blocks (f36956) has +02 / +03 = 01 01 on all three and `$0280 = 01`. The port's host ran the lead -- its
mover and its init -- BEFORE the pass, found no lead on that frame and initialised it a frame later: the scanner read
`$0282` a pass late and the event started a pass late against every other clock of the world. That was the +31 above, the
frame between the event and the mist / the sea / the gulls, and at leg 67's end the second follower's walk frame (126 px).

Fixed: phase 0 runs in the lead's slot of the pass (world.c CtWorldAmbientFrame, on `+02 = 0`; with `$027C` neither 0
nor 1 the visit is the tick alone, C2/3439-3448), and the visit counter +03 ticks for the lead in free roam too.

With it, the dispatcher's pc store. C2/0F80-0F86 `ADC $58 / STA $58 / LDX $4E / STA $0007,X` runs after every advance, so
the block's +07 is the running pc, and C2/048A's MVN (+05..+3F of the parent) hands a child its own op's address: `$0482` /
`$0488` / `$048D` on that map. The port kept the pc in a local until the visit ended (`$042C` in all three; three ops stored
it by hand before allocating). It is stored at the VM loop's head now. The three blocks are then the cartridge's byte for
byte through the stay (170 frames) but for +3E, the port's own animation counter (OWOBJ_ANIMCTR).

Checks: `leg-window-map-entry-event-spawn-pass-vs-cartridge` (the first 44 frames of the stay, the blocks' first nine
bytes and `$0280` / `$0281`), `playthrough-leg-67-map-event-in-the-spawn-pass-vs-cartridge`, and the two of sec. 4.

### The whole visit is in the slot

The lead's later visits were the host's too, before the pass, and so read what the pass's earlier blocks write a pass
late. Measured on leg 68 (the 600 A.D. map out of Porre's office; the script stores `$027C = 1` at f1516):

- The frame the script opens free roam (`13 7C 02 01`), the cartridge's lead already takes its input frame. With no button
  that is the idle arm, `INC $02B1` (C2/35BD): `$02B1` = 01 on f1516. With Right held from 60 frames before the opening
  (the leg's start state, its pad edited), `$0283` moves on f1516. The port's lead read the pad a frame later: f1517.
- `$02B1` counted in the port while `$027C` = 0 (20 at f1516): the host took the idle arm where C2/356C `BNE ->35E0`
  only ticks. The fidget (`$02B1` = F0) came about half a second early after an entry.

- The other way round, a block BEFORE the lead's sees the lead's writes a pass later. The era placard (world 0: block
  `$0D30`, the lead `$0D70`) counts `$1BF7` up and shows itself at `$78`; the lead's step zeroes it (C2/36E7). Measured
  (saves/ow_w0.state, idle 50 frames then Left, one peek a frame): the frame `$1BF7` turns 00 and `$0283` moves, the OAM
  shadow `$7E:0700` still holds the placard (FC C0 AC 31); it is gone the next frame. And from the last step's zero the
  counter reaches `$78` 120 passes later (the script's INC comes before the lead's STZ). The port hid the placard on the
  step's own frame and showed it after 119.

Fixed: the host's mover is called from the pass, in the lead's slot (field.c owLeadSlotVisit through
CtWorldSetLeadSlot): the pad's gate (`$027C` = 1 as it stands there), C2/3567's other arms, the step, the camera apply
C2/36F7 (CtWorldStreamApply; the NMI's flush stays before the pass, CtWorldStreamNmi), the pose machine, the trail
ring and the commit's mirrors (`$0283` / `$0285`, `$0102` / `$0103`). A pass its inline JSL suspends before the lead's
slot runs the visit on the frame it resumes (the pass stands inside the JSL; by construction, not measured). Only a replayed snapshot without a `$3404` block
keeps the host's own frame before the pass.

Check: `leg-window-map-opening-input-frame-vs-cartridge` (leg 68, f1500 + 60: `$027C`, `$0283`-`$0286`, `$02B1` and the
lead block's first five bytes).
