# The overworld's party followers: class $3AE2 (C2/3AE2) and the lead's trail ring

Derived 2026-10-04 from the user's report "in the original game the party also follows crono on the world map". The port drew
only the lead. The follower blocks existed, but nothing ran them. Validated by `tools/ow_follower_cmp.py` (check
`ow-party-follower-walks-the-trail-vs-rule-cartridge`): 396 frames identical to the rule's cartridge, out of the
600 A.D. cathedral with Crono and Lucca.

## 1. The objects

The world's bootstrap allocates the lead (class $3404) and two follower blocks of class **$3AE2**. Their +24 holds
the party slot (1, 2). Measured on world 1 out of the cathedral (saves/field_re/catedral_user.state, the port's
CT_OWDUMP at f200): the lead is in $0DF0, followers in $0E30 (+24 = 1) and $0E70 (+24 = 2). The port's bootstrap
allocates the same blocks; until now `CtWorldAmbientFrame` skipped the class ("the other native classes are still
not run").

## 2. The lead's trail ring ($1B5A / $1B5B / $1B7B)

- 16 words of X at `$1B5B`, 16 of Y at `$1B7B`, cursor (byte offset, step 2, `& $1F`) at `$1B5A`.
- **Reset** by the lead's init (C2/34F8): `353B STZ $1B5A / 3540 JSR C2/391C`. C2/391C writes `$0283/$0285` (the
  arrival point, already in the block at 3515) into all 16 entries.
- **Push** at every step arrival: `C2/3757 JSR C2/3934`, on the frame the lead's position equals its target
  +2A/+2C (3745-3755). That is after its tick (3731) and its `$0283/$0285` store (3736). C2/3934:
  `LDA $1B5A / AND #00FF / TAY / LDA $0014,X / STA $1B5B,Y / LDA $0018,X / STA $1B7B,Y / INY x2 / AND #001F /
  STA $1B5A`.
- Port: `CtWorldLeadAnimFrame`'s init arm (the reset), and `CtWorldLeadRingPush` from field.c on the mover's
  phase 2 -> 1 frame (the push).

## 3. The handler (C2/3AE2)

- **3AE2:** `LDA $0024,X / TAX / LDA $7E2980,X / BPL`. An empty slot returns CLC and nothing is drawn.
- **3AEF:** `$027E != $027F` -> +2 := 0; `$0280 != $0281` -> +2 := 0. 3B07 `JMP ($3B0F,X)` on $027E:
  - 0 -> 3B1B.
  - 1-3 -> 3B68 CLC.
  - 4 -> 3B6A (phases 3DE5 3E17 3E7B 3EDF).
  - 5 -> 3B7C ($028F).
  - The port pins $027E to 0 and does not port 4/5.
- **3B1B:** `$027C` not 0/1 -> +28 &= $7F, the STAND id (C2/3987), the tick (C2/0E1D), CLC.
- **3B44:** `JMP ($3B58,X)` on `$0280 * 8 + phase * 2`. Mode 0 is 3BC2 / 3C39 / 3CDA / 3D3B. Mode 1 (3D9C / 3DC1 /
  3DE3) runs a $1B3B script and is not ported.

### Phase 0, 3BC2 (falls through into 3C39 the same frame)

- +2 := 1.
- +0F := $A0 ($B0 at loc $1F5), then `| $0A` with +10 := $20 for slot 1, or `| $0C` with +10 := $40 for slot 2
  (palette 5 / 6, own tile base).
- +14/+18 := `$0283/$0285`, fractions 0, velocities 0, +3 := 0.
- +27 := +28 := `$0104 & 3`, +26 := 0.
- `JSR C2/416C` (+28 &= $7F, the STAND id).

### Phase 1, 3C39 (decide)

- **Idle** (3C8C), when +3 < 8, or when the LEAD (`$0283/$0285`) is inside the box `[p - r, p + r + 1)` on both axes
  (3EE1 r = $10 for slot 1, 3F27 r = $20 for slot 2; the near arm zeroes +3):
  - `INC $02B2` (slot 1) or `$02B3` (slot 2).
  - Below $F0: the STAND id (C2/416C).
  - At $F0: the fidget, id $A3 / $A4, then the counter parks at $FE.
  - Then the tick.
- **Otherwise:**
  - 3F6D: the target +2A/+2C := ring[(`$1B5A` - 2) & $1F] for slot 1, ring[(`$1B5A` - 4) & $1F] for slot 2.
  - 3FA7: +2E := +2A - +14, +30 := +2C - +18.
  - 3FBC -> C2/41B5, the two L-shaped paths from the follower's cell to the **lead's** cell (8-px cells,
    8-bit):
    - bit 0 = row-first blocked: C2/4205 along row y1 - 1 from x1 to x2, then C2/425A down column x2.
    - bit 1 = column-first blocked: 425A at x1, then 4205 at row y2.
    - A cell blocks when `C2/62AE & 3`. 425A reads columns $05 - 1 and $05 and parks the first prop in +22.
  - Then `JMP ($3FE8,X)` on that result:
    - **0 -> 3FF0:** dx = 0 -> $21; dy = 0 -> $11; else |dy| >= |dx| -> $22, otherwise $12.
    - **1 -> 404C:** $22.
    - **2 -> 4056:** $12.
    - **3 -> 4060:** walk the ring back from `$1B5A - 2`, at most 16 entries, for a point either path reaches.
      - Found: target := that entry & $FFF8, 3FA7, then this table again on its result.
      - Not found: 3F6D / 3FA7 and 3FF0.
  - +29: low nibble = legs, bit 4 = row first, bit 5 = column first.
  - 4125: +29 bit 5 -> 4152 (vy = sign +30, 0 -> +1), else 4138 (vx = sign +2E).
  - 40E4: +27 := old facing, +28 := (+28 & $80) | the velocity's direction. Zero velocity is a `BRA` to itself;
    every caller has set one.
  - 4184: `STZ $02B1,slot`. Unless the same way and already walking: +28 |= $80 and the WALK id (C2/3953).
  - +3 := 0; +29 bit 4 -> +2 := 2 and on into 3CDA, else +2 := 3 and 3D3B.

### Phases 2 and 3, 3CDA / 3D3B (the same shape, X against +2A / Y against +2C)

- +3 >= 8 and the axis on a cell edge -> +2 := 1, `JMP C2/3C39` (decide again, this frame).
- Not at the target -> `JSR C2/0F38 / JSR C2/1CB8`, 1 px.
- At the target, n := (+29 & $0F) - 1:
  - Non-zero -> +29 := n, the other phase, its velocity (4152 / 4138), 40E4, 4184.
  - Zero -> +2 := 1, the STAND id.
- Then the tick (C2/0E1D).

The anim ids are the lead's: STAND C2:39AA, WALK C2:397F, +$0C for Magus. The member comes from the block's tile
base and palette.

## 4. The world load's counters

C2/2C1D zeroes `$02B1 / $02B2 / $02B3` (2C3B-2C41) with `$027C`. Without that, Lucca's idle counter came out of
the field nonzero (`$2D` from the cathedral) and her fidget fired 46 frames early. Port: field.c's bootstrap
zeroing block.

## 5. Not covered

- Mode 1 (`$0280 = 1`, the scripted party on the map) and `$027E` 4/5: not ported. The handler returns without the
  CLC tail, as before.
- Slot 2 (a third member) runs the same code with its own constants ($20 box, ring - 4, `$02B3`, $A4, $40 tiles,
  palette 6). **Covered 2026-10-04:** the slot-1 drive with Marle poked into party slot 2 on both sides (`$2982 = 01`,
  PEEK_POKE / saves/field_re/catedral_party3.lakewram): her block $0E70 walks phases 1-3 behind Lucca, and the port is
  identical on every frame (`python tools/ow_follower_cmp.py 400 party3`: 408 frames, 0 differ; check
  ow-party-follower-slot2-walks-the-trail-vs-rule-cartridge).
- The field -> overworld entry from this door used to come up 31 frames sooner in the port (world init f75 vs f107
  from the same press): section 6 found it (the entry song posted from the pass). The check still aligns each side
  on its own init.


## 6. The world entry's song is posted from inside the pass (C2/2F0F), and the world waits for it

Found while measuring the follower check's alignment. Out of the cathedral, the port's world came up 31 frames sooner
than the cartridge's.

**Cartridge** (the rule's oracle, catedral_user.state, `20:80` down; `--hookpc C30002 / C223CC / C223D2 / C70004`,
`--monitor 7E1E00:4 --writer`):
- Decompressions: f35-f71.
- C2/23CC turns the NMI on at f73. The first NMI's pass runs the loader script, and its zoning arm `34 0F 2F` posts
  `$1E00 = 10 02` at f73 (vPos 228, C2/2F49 `JSL C7/0004`).
- The CPU stays in bank C7 until the driver's `STZ $1E00` at f107 (C7:0196).
- Every NMI in between is C2/0178 `JMP C2/033D` (`INC $4B` / RTI), because the pass is incomplete (`$4C` bit 0,
  cleared by C2/01CC).
- At f107 the same pass goes on: the lead and the followers are allocated (C2:04AA), then the OAM build.
- C2/0346's wait in the loader is released at f108, and the fade-in object steps from f109.

**Port, before:**
- The host staged and posted the song at the entry (worldEnterSong), unpriced.
- C2/2F0F was an unported native: obNative fell through it.

**Port, now:**
- `obNative(0x2F0F)` zones (or takes `$29AE` when `$7F01ED != 0`), stages `$1E00 = 10 song FF 80` and posts through
  the host's priced JSL (field.c owSoundJsl: under the rule it starts at the vblank and the NMIs met wait for nothing).
  A positive count suspends the pass after the native (obRun returns 2, CtWorldAmbientFrame remembers the block).
- The host holds the world that many frames less one (owPassHold: nothing of the world runs, no C2/0454 post), then
  runs the rest of the pass on the return frame (owPassResume: no pad latch, no draw-list reset, no lead; the OAM
  build and C2/033B close it).
- worldEnterSong only zones and posts for a replayed snapshot (no bootstrap).

**Measured:**
- Port: post f75, 35 NMIs crossed, the lead and the followers at f110 (cartridge: f73, 34, f107).
- The 2 frames before the post are the world load's price: CT_OW_LOAD_REST was fitted on world 0's Truce exit. The
  1 frame is the SPC's own response; the port's seeded SPC is 73 frames younger than the oracle's at the post.
- Forest -> world 0: 11 NMIs.

**Not done:** C2/0454's queued posts (the main loop's) are still unpriced. They post from the main loop, outside the
NMI, so a long one would hold the main thread, not the pass.

**Which entries (2026-10-04, after the suite).** Only a loader that reaches the zoning arm posts from the pass.
- The attract's worlds take a cutscene loader. The cartridge makes a single C7 call over all of them (f104, from block
  $0C70) and never runs C2/2F0F (`--hookpc C22F0F` over 9000 frames: zero visits).
- The host's entry post stays for those loaders, as it was.
- field.c `worldLoaderZones` walks the loader script from $7F:0400 with the VM's own op semantics:
  - stores 10-14 go into an overlay;
  - 09 / 43 / 35 / 00 advance;
  - 36 / 37 call and return;
  - 26 / 27 / 4C / 4D branch;
  - it reads C2's loader state first (C2/2C1D's zeroing, C2/2C50, C2/26D6's flag copy, which the host performs after
    the call).
- It answers "zones" only on reaching `34 0F 2F`: the cathedral and forest exits do, the attract's five world legs do not.
- Without this gate the attract lost its stand-in post and 17 cartridge-graded attract checks failed. With it they all
  pass.
