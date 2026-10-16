# The Epoch on the overworld: classes $309E, $42DD, $495E and the lead's modules (2026-10-16)

ROADMAP G1a item 3. Port: `src/world.c` (obBoardManager, obLeadModule, obEpoch, obEpochShadow, folBoard,
owVehicleLeave) and the host's switch in `src/field.c` (`owVehicleModule`). Check: `ow-epoch-boarding-and-liftoff-vs-oracle`
(`tools/epoch_board_cmp.py`).

## 1. The state

| Byte | Meaning |
|---|---|
| `$027E` | The lead's MODULE: C2/3404's table C2:342D. 0 on foot (3439), 1 nothing (3479), 2 riding the Epoch (346D: `$0283 = $0290`, `$0285 = $0292`, no tick: the party is not drawn), 3 riding vehicle B (347B), 4 boarding (3489), 5 stepping out (349B). Saved (`$027E-$027F`, menu.c CT_SAVE_RECS). |
| `$027F` | `$027E` as the boarding manager last saw it (C2/30B5). The lead (C2/340E) and the followers (C2/3AF1) zero their phase when the two differ. |
| `$028F` | Which vehicle: 2 = the Epoch, 3 = vehicle B. |
| `$0290/$0292` | The Epoch's position (pixels). `$0294`: bits 0-1 members aboard, 2/3 the time-gauge script's flags, 4 in the time gauge, 5 WINGS, 6 riding, 7 present. `$029F` (word) the world it is parked on. |
| `$029A/$029C/$029E` | Vehicle B, the same layout; only on loc 1F3 (C2/315E `CPX #01F3`). |
| `$1BEF` | The Epoch is aloft (the shadow's pose). |

Every world's bootstrap spawns the three classes behind `27 94 02 80 10 / 24 9F 02 <world> 0B / 35 DD 42 / 35 5E 49 /
35 9E 30` (world 1's at `$7F:04C0`; ROM C3:8C23 and the other worlds'): only when the Epoch is present and parked on this world.

## 2. Boarding (class $309E, C2/309E)

Runs only with `$027C` 0 or 1 and `$0280 = 0`; `$027F = $027E`; then by module (C2:30C1):

- 0 (C2/30CD): A (`$00F8 & 0080`), the Epoch present on this world and the lead's box C2:324C `{8,8,8,8}` overlapping the
  Epoch's C2:3254 `{0,0,0,8}` (C2/2345: per axis the 16-bit `SBC` of the distance and both extents, `BMI` = overlap) ->
  `$028F = 2`, `$027E = 4`, `$1B58/$1B59 = 0`, `$1B32/34/36 = FFFF`, `$1B44 = $0282 = 0`. Storyline D2 (`$1BA6`) spawns
  script C2:31A8 instead (`$027C = 7`). Vehicle B the same on 1F3 with box C2:325C.
- 2 / 3: the vehicle's bit 6 clear -> 5 (it landed).
- 4 (C2/31D0): the party's size (members of `$2980-$2982` without bit 7) equal to `$0294 & 3` -> 2 (3 for B).
- 5 (C2/321C): `$0294 & 3` back to 0 -> `$027E = $028F = 0`.

The lead in module 4 (C2/37BD-391A): aim at the vehicle (`+2A/+2C`), walk the column at 1 px a frame (C2/066C on BG1 and
BG2), then the row (C2/0568), the walk pose C2/3953; on the frame it stands on the vehicle `$0294 + 1` (C2/3AB1) and it hides
(phase 3: no tick). The followers (C2/3B6A, 3DE5-3EDF): C2/3F94 aims at `$0283 & FFF8`, then the trail-walk's own path
(C2/3FA7, 3FBC, 4125, 40E4, 4184); exactly on the lead -> aboard and hidden. In module 5 every member steps onto the vehicle's
spot and counts `$0294` down every frame (C2/34AD / 3B8E); the lead also takes `$0104 = 1` and C2/3ACB's cells, and the
frame after, back in module 0, its init C2/34F8 (the host re-seeds its mover there: field.c, `CtWorldLeadReinit`).

## 3. The Epoch (class $42DD, states C2:42E6)

| State | Address | What it does |
|---|---|---|
| 0 | 42F8 | Placed on `$0290/$0292`. Parked (bit 6 clear): state 1, attr A0, facing 1, pose 18 (wings) / 2D. Riding: state 3, attr B0, facing `$0104 & 3`, `$1BEF = 1`, bits 2-3 cleared, pose C2/48A9, the map properties C2/39D5 + 4A58. In the time gauge (bit 4): state 7 with script C2:4B8C (NOT PORTED). |
| 1 | 43CB | Parked; when `$027E = 2`: bit 6. No wings: `$0294` bit 4, `$027C = 8`, state 6 (the time gauge at once). Wings: state 2, the song 13 (C2/4A2A), velocity Y = -0.5, `+26 = 30`, pose 19. |
| 2 | 443C | The lift-off: attr B0 at `+3 = 8`; 0x30 frames of C2/0F38. Then storyline D3 (`$1BA6`): `$027C = 7` and script C2:4A9B (the Black Omen; it enters loc 1DD), else state 3: pose, song, `$1BEF = 1`, y + 18. |
| 3 | 44B7 | In the air, `$027C = 1` only. `$00F8`: A (0x80) -> the time-gauge script C2:4C8F when bit 3 is set, else LAND if the six properties under it are clear (`& 0707`), not onto vehicle B (C2/4917) and outside the world's forbidden rectangle (C6/E797, the table C6:E7BA: 1F0 cells x 41-71 y 64-7E, 1F1 x 60-6D y 76-7E, 1F3 x 00-0D y 00-33, 1F4 x 00-1F y 60-7E); 0x01 the time gauge (`$027C = 8`, state 6); 0x40 `$027C = 5` (the menu); 0x04 `$027C = 6` (NOT PORTED); the d-pad: velocity C2:4899 (2 px a frame), the next cell C2/3A56 + 3A8F, its properties, state 4. |
| 4 | 4675 | The step: C2/0F38, both layers scrolled by the velocity (C2/0568 / 066C), `$1BF1/$1BF3`, `$0290/$0292`; at the target state 3 (`STA $0002,X` 16 bits). |
| 5 | 46E2 | Landing: 0x31 frames down at half a pixel (attr A0 at `+3 = 20`); bit 6 cleared -> state 0 (parked), pose 18. The region's song C2/2F4E on the way in. |
| 6 / 7 | 4728 / 4823 | The time gauge's result and its script run (NOT PORTED). |
| 8 | 4888 | Back to 0 once `$027E = 5`. |

The shadow (class $495E): only on the Epoch's world (else SEC); every frame pose 4E at y - 18, or with wings and aloft
pose 1F at y + 18.

## 4. Graded

`tools/epoch_board_cmp.py` (Jackie's ch24), four checks:

- `ow-epoch-boarding-and-liftoff-vs-oracle` (A once on the parked Epoch): every change of `$027C $027E $0294` and the
  Epoch's state, counted from the frame `$027E` leaves 0, equal on both sides (+0 4, +1 A2, +2 module 2, +3 bit 6 and state
  2, +51 `$027C = 7`, +70 `$027C = 2`); the lift-off picture and the fade into 1DD 0 px apart (P+2; the era placard masked:
  the port loads the battery save at another frame, so its counter `$1BF7` starts lower).
- `ow-black-omen-rising-vs-oracle` (`--omen`, sec. 6): five pictures of the rising, 0 px.
- `ow-epoch-flight-vs-oracle` (`--flight`): past the rising to the free flight (the cartridge's f3073), Right 40 frames, Down
  16, A: 56 changes of `$027C $027E $0294`, `$0290/$0292` and the state equal from the first step; four pictures 0 px.
- `ow-epoch-landing-vs-oracle` (`--land`, A every 32 frames): the landing, the party out, aboard again, the lift-off, twice:
  15 changes equal; four pictures 0 px and one pinned (below).

The port's world runs a few frames apart from the cartridge's after the field scenes in between (1DD, 06B, 1DD): 358 frames
behind at the flight with the boarding A 360 behind. The world's entry from 1DD takes the port 72 frames to free roam against
the cartridge's 71 (the location-load clock, ROADMAP F1), so 1F6's sea palette row (row 2, a step every 10 frames, on the
world's own clock) steps one frame later against the vehicle's events: the landing check's port f3510 differs on the sea only.

## 5. Open

- The time gauge (`$027C = 8`, states 6/7, script C2:4B8C / 4C8F), `$027C = 6`, vehicle B's ride (module 3) beyond boarding.
- A `--loadwram` of the user's flying capture (saves/epoch_re/epoch_user.state) runs the ROM's bootstrap with
  `CT_OWSEEDBOOT=1`, but the host enters the world at its default cell, not the Epoch's, so the craft is off screen.
- In loc 1DD (the cockpit) the party in the canopy is drawn with the wrong tiles (a field-side picture item).

## 6. The Black Omen's rising (1F6, script `$7F:0632`)

After the first flight's scene (loc 1DD), 1F6 comes back with the camera cells zeroed and the script:

- `3D 44` (the song), `13 41 00 15` (TM = BG1 + BG3 + OBJ), `13 42 00 12` (TS = BG2 + OBJ), `13 45 00 02` (CGWSEL: the sub
  screen), `13 46 00 04` (CGADSUB: add, BG3 only).
- `13 08 00 6C / 34 FC 7A / 03 ...` and `13 08 00 0F / 34 20 7B / 03 ...`: C2/7AFC and C2/7B20 decompress (C3/0002) the
  packets C6:FE20[`$08`] and C6:FF40[`$08`] into `$7F:9000`; op 0x03 sends each to VRAM.
- `34 56 7C / 34 97 7C`: HDMA channel 1 (DMAP 42: indirect, two writes, to `$210F` BG2HOFS) from `$7E:8A35` and channel 2
  (to `$2110` BG2VOFS) from `$7E:8A42`, both `F0 <p1> F0 <p2> 00`, `$4A |= 06`; the line buffers `$8B56`-`$8FB5` and
  `$9176`-`$95D5` zeroed. The NMI (C2/02B2-02DE) copies `$8A21/$8A23` into the first table's pointers, `$8A25/$8A27` into
  the second's.
- Two children (`$7F:084E`, `$086F`) each frame: `+22 + 1` (the amplitude), C2/7CD8 / C2/7D5E -- `+26 + 1`; the even step
  points the table at buffer A and copies buffer B shifted one line into it, the odd step the other way; the newest line (the
  buffer's last word) = sign(sine(`+24 += 10`)) x ((|sine| x `+22`) >> 8), C2/225E's table at C0/F900.

So the silhouette is BG3 (its own colours dark) ADDED to the sub screen, which is BG2 drawn with a per-line wave: the sea
seen through a rippling shape. The port's composer had the sub screen fixed as "the main screen under BG3" (TS = 13 in every
world before); it now builds the sub screen of `$0042` when it differs (field.c owComposeSub) and reads BG2's scroll per
line from an indirect HDMA channel (owHdmaWordAt; scanline s takes table line s, measured with `ct.exe --hdma`: entry 9 first
on line 9). The channels' programming is kept as the natives write it (world.c CtWorldHdmaSet; window 1's C6/E241 sets
channel 2 to WH1, the rising to BG2VOFS). Before, the six natives were skipped and the port crashed on the BG3 tile 0x3FF the
unloaded VRAM held (worldBg3Px now wraps the VRAM address at 64 KB, as the PPU does).
