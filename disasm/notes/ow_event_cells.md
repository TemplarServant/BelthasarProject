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

Left: `$027C = 1` comes a frame early relative to the event (+31 against +32) -- the world's entry from the load, where the
cartridge's first NMI is raised at once inside the vblank (C2:23CF, V=258) and not counted (the load clock, ROADMAP F1). The
world's own clocks (the sea's palette cycle, 600 A.D.'s BG3 mist, the gulls) stand a frame apart from the event with it, and
the pinned pictures differ there.
