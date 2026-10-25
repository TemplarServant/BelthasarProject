# Manoria Cathedral (loc $081 / scene 129): the nuns catch fire, Frog's sword, and what a mid-battle capture can and cannot seed

*Defects #15, #19 (fixed), #16, #18, #22 (measured) of the 2026-09-02 list. Everything below is measured on the
ROM + the oracle; the derivation ran as a 9-agent workflow (3 derivations, 6 adversarial refutations) on
2026-09-04/05 and every claim survived re-measurement, with the corrections folded in here.*

## 0. The four captures and how they became fixtures

The user supplied four snes9x states (all loc $081, storyline $12): `animacao_freiras_naga_ettes` (field, the
nuns about to transform), `frog_espada_preta` (field, Frog's leap), `anim_heal_crono` and `beijo_naga_ette`
(both MID-BATTLE, Crono+Lucca vs Naga-ette ×4). They were spliced (`tools/s9x_to_lake.py`) onto a loc-$081
base made through the ROM's own loader (`--teleport 0x81 127 191 2 --from candy_shop_lake.state`) for the
field pair, and onto `bat8_lake.state` (the same fight) for the battle pair:

| fixture | what it holds |
|---|---|
| `saves/field_re/m81_nuns_lake.state` / `.lakewram` | f0 = the nuns walking to their spots; battle request at f332 |
| `saves/field_re/m81_frog_lake.state` / `.lakewram` | f0 = Frog (obj4) about to leap; his palette op already executed (ip $050F > $04F4) |
| `saves/battle_re/m81_kiss_lake.state` / `m81_kiss.raw.wram` | mid-battle, the Naga-ette's "Slow spell" about to start (cmb5 pose 04→09 at f75) |
| `saves/battle_re/m81_heal_lake.state` / `m81_heal.raw.wram` | mid-battle, Crono's item/heal action about to start (seat0 pose 4F→2C at f21) |

`.lakewram` = `#lakewram\nRAM:131072:` + the raw 128 KB from the capture's own RAM block.

## 1. Defect #15 — "the nun does not catch fire"

### 1.1 What the ROM does (disassembled, oracle-measured)

- The four **flame objects are obj16-19** (op-0x82 sprite $73 → asset 0x7A), parked on the nuns' destination tiles
  from scene entry, **in the draw list** (`$1A81=01`, `$0F00=80`) but at **`$0F80 = 04`** = OAM group B (write cursor
  $08A0 = entry 104) with `$0C00/$0C01 = 00` = OBJ priority 0 → behind the opaque BG → invisible (OAM entries
  104-119 at f2, priority 0).
- Each flame is revealed by its own fn3 **`8E 33 B1 8E 04 B1 8E 33 00`** (called at prio 3, obj16 from f117, then
  17/18/19), a 2-frame flicker ending at `$0F80 = 33` = group A + priority 3. Writer C0:4C7F = **op 0x8E, C0/4C74**:
  `4C7C STA $0F80,X` then `BIT #80`: AUTO arm (C0/4C83-4CB6) takes the foot tile's props `$7E7000/$7E7040` bit6
  `>>2 | #20` into `$0C01,X` (via $D9, 4CA8) / `$0C00,X` (4CA3); FIXED arm (C0/4CB7-4CD4) `$0C01,X := op & #30`,
  `$0C00,X := (op << 4) & #30`; both arms end `$0F01,X := #FF / STZ $1601,X`.
- The nuns (obj9-12) are deactivated at f224 by the class-7 controller obj15 (`0A 12 / 0A 14 / 0A 16 / 0A 18`):
  **op 0x0A = C0/6254 stores exactly two bytes**, `$1100,X := #80` (625F) and `$1A81,X := #00` (6264); the next
  C0/A810 pass drops them (A88D `BMI` → A910 `STZ $0F00,X` + unlink, same frame).
- **The draw-list gate is C0/A88D** (per frame) / **C0/B127** (rebuild): `$1100,X` bit7 clear, `$1A81,X` in 1..7F,
  tile inside the camera window (A88D: `$1D0A-4`/`$1D0C+4` saturated, `$1D0E-2`/`$1D10+5`; B127: `$1D0A-4` floored,
  `$1D0C+4`, `$1D0E`, `$1D10+5`, no saturation) → latched in `$0F00,X`. C0/B0F8 never draws class 7. The drain
  C0/CAD9 re-tests the same triple. **`$0F80` plays no part in membership**; it selects the OAM group (C0/B334
  `AND #0C`) and, through `$0C00/$0C01`, the two halves' OBJ priority (C0/CE84/CE8F).
- The flames' **BLUE** is not their palette record: `$1400 = 1038` → `$E4:1038` is ORANGE. obj16's prio-7 loop
  `$07C8 27 / $07CB 88 82 0C 00 FF7F 2A7F 0000 0075 0054 / $07D9 11` runs **op 0x88's blob arm C0/49F8-4A43**:
  `$DB = 0x80 + ($0F81,X >> 1)*16`, `$DD = 2*($DB | (cmd & 0x0F))`, count = word(ip+2) − 2 bytes, MVN from the
  script (ip+4) into `$7E:2200+$DD` (writer C0:4A35) and `$7E:2000+$DD` (C0:4A43) — CGRAM shadow **slot 1 colours
  2-6, shared by all four flames** (the allocator gave obj16-19 the same slot, `$0F81 = 02`).
- The location load fills the whole `$0F80` row with `80 00` (AUTO / attr 0) before any script runs: measured
  across `base_loc6 → 439` (block write at f83, no per-byte PC; afterwards only C0:4C7F and the allocator's
  C0:724B touch it). `gato_t0` (scene 6) reads `80 02` for GATO, `80 0A/0C` for the PCs.

### 1.2 What the port did wrong (four independent things)

1. **`CtSceneActor` had a `colorGated && portalCtr <= 0 → hidden` term** — "an object whose script carries an
   `88 8x` blob is the Telepod vortex, drawn only while the iris is open". A heuristic with no ROM counterpart:
   it hid obj16 (fn5 `90 / 88 82 ..`) and the co-located Naga-ette obj23 for the whole scene. **Removed**; the
   swirl is hidden by the PPU window = field.c's iris clip, unchanged.
2. **`Belthasar --loadwram` handed the create-time prescan NO story flags**, so scene 129's obj0 fn0 (`18 12 09`,
   C0/6442 `LDA $7F0000 / CMP / BCS`) took the storyline-0 arm and executed `0A 1E`: the controller obj15 was
   **dead from frame 1**, the nuns were never hidden, the Naga-ettes never shown. The lockstep host always passed
   the state's $7F page, which is why `--vmdiff` was CLEAN throughout. **Fixed**: the seed's `$7F:0000` page is
   read before `fieldLoad` (`fieldReadLakeWram`), and the seed now derives `dead` from `$1100` bit7 (the byte
   op 0x0A stores).
3. **op 0x8E set only the C mirror `sprPrio`**; nothing ever stored `$0F80/$0C00/$0C01` after the loader, and the
   priority read used 0 as "unset" and promoted it to AUTO. **Fixed**: `vmOp8E` performs C0/4C74's stores (both
   arms), `CtSceneCreateIn` performs the loader's `80 00` row fill, `CtSceneActorPrio` returns the byte. This is
   what turned scene 8's Telepod vortex pair (obj27/28, `$0F80 = 00` in every oracle state — in the list,
   invisible) from "drawn" back into occluders once the colorGated term was gone.
4. **op 0x88's blob arm was gated on the Telepod flag and kept in a side struct**; the renderer composed every
   sprite from the baked asset palette. **Fixed**: the blob is C0/49F8's write into both CGRAM shadows for every
   object, and `drawSprite` takes the sprite's 16 colours from the shadow row of its slot (`$7E:2100 +
   ($0F81,X>>1)*32`, the `$2000` page). The cmd-0 close also re-uploads the object's 24-byte record (C0/48C0).
   The renderer's C-side blob overlay survives only as the fallback for an unpopulated row (a host with no ROM
   image); with a populated row it is skipped, so a missing shadow write cannot hide behind it.

### 1.3 Measured after

`field-manoria-nuns-ignite` pins the f240 frame (all four flames, blue, nuns gone). OBJ-layer compare against the
oracle's isolated OBJ plane: f240 1625 matching / 378 differing pixels (was 458 / 1545). Lockstep on the state:
CLEAN; **OAM built from our image vs the oracle's: 1407 main + 0 high byte mismatches over 330 frames (was
26887 + 803)**; obj16's DRAW-DIFF gone. Pinned as `vmdiff-manoria-nuns`.

### 1.4 Residuals (stated, not hidden)

- **The flame's 3-step cycle can sit one step off**: at f130/f240 obj16/17 read `$1681` = 02/01 vs the oracle's
  01/00, with `$1601` drifting on objects that never ran `8E` too (`$1603/$1607/$1619` at f36-f64 = the nuns
  walking). This is the port's anim-countdown drift the memory already lists as open (`$1601/$1681`); it
  predates this work and the write-mask count (99 addresses) is pinned so it can only move down.
- **The map**: the oracle shows the REVEALED dark hall at storyline $12 while the port renders the illusion
  tileset (stained glass / organ). obj0 fn0 carries no E4/E5 tile-copy ops, so it is not the storyline gate —
  a separate map-state derivation.
- The remaining 1407 OAM byte mismatches are attr bytes; not derived here.

## 2. Defect #19 — "Frog's sword is black"

- Frog is **obj4, a non-party PC** (`$1100 = 03`, charId 4, `$2980 = 00/02/80`), drawn on OBJ palette 0 (`$0F81 = 00`).
  The oracle's CGRAM pal 0: colours 1-12 = his record `$E4:0060`, **colours 13-15 = `7FFF 6F0B 01FF`**
  (F8F8F8 / 58C0D8 / F87800) = the sword's white/cyan/orange.
- In BATTLE those three colours come from **CC/E716-E753**: per seat, `$1C48[seat*5]` (weapon) → `$CD:6CEC[X]`
  (gfx index) → 6 bytes of `$D1:4B00 + idx*6` → `$7E:23BA + seat*32` (pal 5+seat colours 13-15). The menu exit
  does the same into `$21BA/$21DA/$21FA` (C2/8358 + C2/F5ED, `menu2_shell_exit.md`). Neither is bank C0, and a
  plain scene load writes nothing there (the party seats' rows persist in the shadow).
- **In the field the writer is Frog's own script**: driving the nuns state through the battle and into this
  cutscene (A-mash, ~9500 frames), `$7E:231A` then `$7E:211A` := `FF7F 0B6F FF01` by **C0:4A35 / C0:4A43** = the two
  MVNs of the op-0x88 blob arm, executed by obj4 at ip `$04F4`: **`88 8D 08 00 FF7F 0B6F FF01`** — literal script
  data (equal to his default weapon's `$D1:4B00` row, but no table is consulted; the PC loader arms C0/42A6 /
  4310 / 43C3 and their common C0/448D never touch CGRAM). The skeptic's correction: the battle module's
  C1:5D85 (14-byte `$D1:4400` row → `$2302,Y`) also writes `$231A` mid-battle — a battle writer, not a field one.
- **Boundary**: the PPU's CGRAM equals the `$2200` page (frog f60), which differs from `$2000` in 47 bytes the
  per-frame effect processors write; the port models those effects (rotation, blob) on top of the base, so the
  renderer reads the `$2000` page. The `$2000→$2200` propagation is not derived.
- **Fixed** by 1.2 (4): with the shadow read, Frog's 33×32 box is **321/321 oracle OBJ pixels identical** at f120
  (`field-manoria-frog-sword`). On this fixture obj4 is already past the op (the seed carries the shadow); the
  ungated blob write is what covers a fresh run.
- **The fresh-run half is graded too**: `saves/field_re/m81_frog_pre_lake.state/.lakewram` is the oracle one op BEFORE the `88 8D` (obj4 on `AD 03` at ip `$04F2`, shadow tail still `001000200030`); the oracle writes the tail at f5 (C0:4A43) and at f70 the port, which must perform the write itself, matches all 352 of Frog's OBJ pixels (`field-manoria-frog-blob`, mutations: the `$2000`-page copy, the shadow read).
- **Residual**: Crono's frame `$38` (`$1301 = $0F01 = 38`, the knocked-flat burst) is placed ~6 rows lower and
  shaped differently by the port's baked field asset (sprite 0 frame 56: 350 opaque px, 9 colour matches), while
  the lockstep OAM built from the ROM assembly is byte-exact (0 mismatches / 2636 sprites over 200 frames,
  `vmdiff-manoria-frog`). An asset-bake defect, not an engine one.

## 3. Defects #18 / #22 (and #16): what the mid-battle captures established

- **No port vehicle can continue a mid-battle WRAM.** `--battletest` is a PRE-battle pipeline
  (`fieldBattleConsumer` rebuilds the eligible list from the FIELD object rows, then `CtBattleBegin` restages
  everything: timers, queue, `$A013`, `$9877/$AEE3`, pose engine, ladders); `CT_BATTLE_RENDERWRAM` is a
  single-frame synthetic-host render. And the ROM **destroys the field object tables at battle entry**: f235
  `$1A80..$1C23 := FF` (FD/AD0B), f236 `$1580..$1A7F := 0` (C1/CDFF), f239 `$0CC0..$117F := $2900` (CC/E4DD,
  `CPX #$04C0`), while `$0117` bit0 stays 1 for the whole battle. Both m81 battle seeds carry exactly that image,
  so the consumer's list is empty and the battle is silently skipped (and without `--scene 129` the run
  segfaults: field.c forces scene 6). **First divergence = f0 on both fixtures.**
- Every `$9907` change in the oracle is the builder C1/1C4A's compare-and-store (writer C1:1C69), driven by the
  pose engine C1/423A (publish C1:4307), the action-end restore C1/41A1-41BD (`$D1:5F42` table), the PC idle
  machine C1/2E87-2EF2 (dwell = E4 byte / 5 via the C1/00D7 hardware divide, min 1) and the enemy idle-window
  tick C1/333D-348C; both idle machines are serialized by the `$9912` per-pass counter with `$9922,X` deferrals
  (C1/2E11, C1/3434-3454); the mid-action idle silence is structural (C1/2D9F is reached only from the idle
  step C1/106E, never during the synchronous action C1/40A0). The port has **no `$9907` model** (host fields only).
- **#16**: the STATIC generic enemy render is byte-exact — at frames where the oracle's Naga-ette holds a settled
  idle pose the port's render matches 656/656 pixels (two of four at f18 of bat8), palette (E4:0B58) included.
  Whatever "transparency" is, it is dynamic (during her action), i.e. the same lane as #18.
- Therefore #16/#18/#22 need a PRE-battle fixture (the nuns state IS one, battle at f332) plus the O6 lane:
  the enemy/PC action pose engine, `$9907`, the idle serializer. The kiss/heal seeds remain valuable as
  oracle timelines (`$9907`, `$974A`, `$5DBD`, `$A6D9` values are in the workflow reports).

## 4. Tooling notes learned here

- `CT_DUMPWRAM_AT=<frame>` needs `CT_DUMPWRAM=<path>` (the load-time dump's path); without it the MSVC CRT
  aborted the process silently on `fopen(NULL)` — now guarded. The dump is the raw `$7E` page + the `$7F:0000`
  story page zero-padded, not the `#lakewram` format.
- `VMDIFF_OAM=1` prints its summary only alongside `VMDIFF_WMASK=1` (the block sits inside the mask report).
- `tools/regress.py --pin` needs a `pin` regex list on the check; the runner's "actual nearby output" is empty
  when the mismatch is a changed selftest line, so run the check by hand to see it.
- `tools/dis.py <off> <len>` parses `<len>` with `int(x, 0)`: `0x60` is hex, `60` is decimal; 16-bit-A regions
  need `m=0` (e.g. `004A35 12 0 0`).
- The `.oam` sidecar's CGRAM block is at byte 1056, not 544.
