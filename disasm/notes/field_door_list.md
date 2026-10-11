# The opened-door list at $7F:1CC8

A door opens by bumping its metatiles in the `$7E:3000` map (C0/0D4C's five handlers,
`field_doors_occluders_decisionbox.md` §1). The map is decompressed afresh whenever the field comes
back from something that clobbered it -- the X menu, a shop, the naming screen, a battle -- and that
decompress brings every door back CLOSED. What re-opens them is a sixteen-entry list the ROM keeps
of the doors opened since the location loaded.

## 1. The list

`$7F:1CC8`, sixteen 16-bit words. Each is a tile index into the map, `(row << 8) | col` -- the door
request's `$015B` (C0/9D47). `$8080` = empty. Packed: every walk stops at the first NEGATIVE word.

Only three routines touch it. `tools/refscan.py 1CC8` finds `C0/28DB STA $7F1CC8,X` (the rest are in
other banks, a different bank byte or DB-relative); the reset, the append's and the walk's loads are
`$7F1CC8,X` in the same three routines. No DMA clear covers it (the location load's four clears,
`wram_load_census.md`, stop at `$7F:1B3F`).

## 2. The three routines

### C0/28AA -- the reset

```
28AA REP #20 / LDA #8080 / LDX #0000
28B2 STA $7F1CC8,X / INX / INX / CPX #0020 / BCC ->28B2
28BD SEP #20 / RTS
```

One caller: `C0/0091`, in the location load's leg list `0085 JSR C0/0B86 / 0088 JSR C0/00F4 /
008B JSR C0/B192 / 008E JSR C0/56A6 / 0091 JSR C0/28AA / 0094 JSR C0/2848`. C0/0066 reaches it for a
FIELD location only: `0069 CPX #01F0 / BMI / 006E JML C2:0000` (the overworld) and
`0072 CPX #01FF / BMI / 007A BRL ->2E1E` (the C3 sequences) leave before `0085`. The menu's reload
(C0/01A5) and the battle's (C0/0304) do NOT run it -- which is the whole point of the list.

### C0/28C0 -- the append

```
28C0 REP #20 / LDX #0000
28C5 LDA $7F1CC8,X / BMI ->28D9          ; the first empty word
28CB CMP $5B / BEQ ->28D6                ; already listed ($5B = $015B, D = $0100)
28CF INX / INX / CPX #0020 / BCC ->28C5  ; full: falls out, nothing stored
28D6 SEP #20 / RTS
28D9 LDA $5B / STA $7F1CC8,X / BRA ->28D6
```

One caller: `0D49`, in the door consumer C0/0D3E, straight after `0D45 LDA #10 / TRB $17` and
BEFORE `0D4C LDX $5B / LDA $7E3000,X` looks at the metatile -- so every door request appends, whatever
the tile turns out to hold, and a second step onto an open door appends nothing only because the
tile is already listed.

### C0/28E1 -- the re-apply

```
28E1 REP #20 / LDX #0000
28E6 LDA $7F1CC8,X / BMI ->28F6 ; JSR C0/28F9 ; INX / INX / CPX #0020 / BCC ->28E6
28F6 SEP #20 / RTS
28F9 PHX / TAX / SEP #20 / LDA $7E3000,X ; dispatch on the CLOSED id
     E6 -> 291C   EC -> 2933   EE -> 2958   FA -> 297D   FC -> 29BA   else -> 2918 (REP #20 / PLX / RTS)
```

Each arm `INC A / STA` the first cell and writes the rest of the block as CONSTANTS -- the OPEN ids,
not INCs:

| id | arm | cells written |
|---|---|---|
| E6 | 291C | (r,c)=E7 (r-1,c)=E7 |
| EC | 2933 | (r,c)=ED (r,c+1)=EF (r-1,c+1)=EB (r-1,c)=E9 |
| EE | 2958 | (r,c)=EF (r,c-1)=ED (r-1,c-1)=E9 (r-1,c)=EB |
| FA | 297D | (r,c)=FB (r,c+1)=FD (r-1,c+1)=F9 (r-1,c)=F7 (r-2,c)=F3 (r-2,c+1)=F5 |
| FC | 29BA | (r,c)=FD (r,c-1)=FB (r-1,c-1)=F7 (r-1,c)=F9 (r-2,c)=F5 (r-2,c-1)=F3 |

The steps are 16-bit (`TXA / SEC / SBC #0100 / TAX`, `INX`, `DEX`) and the store is `$7E3000,X`
long-indexed, so a block whose upper rows sit above row 0 wraps X and lands at `$7E3000 + X` in bank
`$7F` (`$7F:2Fxx`).

Callers: `286C JSR C0/28E1` -- the FIRST JSR of C0/286C, the pipeline pass run once before a fade-in
ramp, from `C0/2848` (a location load, after C0/28AA emptied the list: a no-op) and `C0/2824` (the
reload after the X menu `C0/197E`, and after a shop or the naming screen `C0/0D3B`) -- and
`18C4 JSR C0/28E1`, straight after `18C1 JSR C0/0283` returns from the battle tail (C0/0304 ends
`038C BRL ->00EB`, a tail call). Each of them follows a fresh `C0/A33B` map decompress
(C0/01CF in the menu's reload, C0/0316 in the battle's).

### The handlers' own writes (C0/0D4C .. 149D)

C0/28E1's arms mirror the five door handlers, which bump the same blocks by INC. Every handler INC is
8-bit except one: the E6 handler's second cell,

```
0D7D REP #20 / TXA / SEC / SBC #0100 / TAX / LDA $7E3000,X / INC A / STA $7E3000,X / 0D8E SEP #20
```

is a SIXTEEN-bit read-modify-write of the word at (r-1,c): it writes (r-1,c+1) too, and carries into
it when (r-1,c) is $FF.

## 3. Measured (the logical oracle)

**The menu's reload** -- `saves/battle_re/party3_field.state` (loc $082), X at f4, B at f60. The state
holds one opened door: `$7F:1CC8 = 2935 8080 ..`, `$015B = 2935`, the $FA door on the right with its
cells `$7E:5735/5835/5935 = F3F5/F7F9/FBFD`. `--hookpc`: C0/1964 at f68, C0/0A14 f100, C0/2824 ->
C0/286C -> C0/28E1 -> C0/28F9 (A = 2935) -> C0/297D all at f102. `--monitor` (its own frame numbers):
the menu program overwrites the cells f21-f90 (its own use of `$7E:3000`), the reload's decompress
shows the door CLOSED at f91 (`F2F4/F6F8/FAFC`), and at f102 they are open again. The list never
changes.

**A door opened in the drive** -- the same state with the door closed and the list empty (`PEEK_POKE
11CC8=80,11CC9=80,5735=F2,5736=F4,5835=F6,5836=F8,5935=FA,5936=FC`: the values the reload's own
decompress writes, and C0/28AA's), running right 86 frames and up: `$0117` bit 4 is raised at f99
(C0/9D47, at `$1882 = 2AE0`), and at f100 the list reads `2935` and all six cells are bumped.
`--hookpc`: C0/9D47 in the iteration that moves `$1882` from `2B00` to `2AE0`, C0/0C76 -> C0/0D45
one iteration later (C0/0C76 runs at `00A4`, BEFORE the controller C0/881E at `00A7`).

**A location load** -- `saves/field_re/scene468_stairs_to_120.state` with `$7F:1CC8 = 2935` poked,
running right: C0/28AA and C0/28E1 at f47, f113 and f186 (the stairs reload 468 twice, then 07A);
the list reads `8080 ..` from f46 (monitor).

## 4. The port

`src/ct_vm.c` (WRAM-level, beside `CtWramCommitLocation`):

- `memDoorListReset` inside `memLocationLoad` = C0/28AA. That pass is the port's list of the load's own
  clears; it runs before the object load where the ROM runs C0/28AA after it, and nothing in between
  touches the list.
- `CtWramDoorListAppend(w, tile)` = C0/28C0.
- `CtWramDoorListApply(w)` = C0/28E1 + C0/28F9.
- `CtWramDoorOpen(w, tile)` = the five handlers' `$7E:3000` bumps, the E6 carry included.

`src/field.c`:

- the door consumer (`$0117` bit 4, the port's C0/0D3E) calls `CtWramDoorListAppend` then
  `CtWramDoorOpen` on the host WRAM, then `CtFieldSceneDoorOpen` on the render's copy of the map as
  before. The tile is the `$015B` the port keeps in `g_collRam` (the player controller's RAM).
- `fieldDoorListApply(site, frame)` = C0/28E1 at its three port sites: the location entry's C0/286C
  (both the cold `--scene` start and a transition, after `fieldMapHeaderStage` = the map in WRAM, before
  `fieldEntryTilemapFill` = C0/0A50), the menu/shop/naming reload's C0/2824 (the same place in the
  RTL block), and the end of `fieldBattleResume` (C0/18C4).
- `CT_DOORLIST=1` prints `doorlist: f<N> C0/28E1 <load|reload|battle> list <words|-> reopened <n>` and
  `doorlist: f<N> C0/28C0 tile <XXXX> list <words>`.
- `CT_DUMPWRAM` now writes the flat 128 KB image whole (bank `$7F` was the story page, the rest zero).

The render keeps its own copy of the map (`SceneData.l1t`), which the reload never rebuilt: the door
stayed OPEN on screen after a menu close, but the WRAM the ROM's readers see held it CLOSED. Before
this port: `menu_audit_cmp.py doorreload --ram 11CC8:8,5735:2,5835:2,5935:2` read the list the same
(the seed carried it) and all three cell pairs `F2F4/F6F8/FAFC` against the cartridge's
`F3F5/F7F9/FBFD`.

## 5. Grades

`ct.exe --sweepdoorlist` (check `sweep-door-list-routines`) micro-executes the ROM against the port:

- C0/28E1 vs `CtWramDoorListApply` -- 3000/3000 over the whole 128 KB;
- C0/28C0 vs `CtWramDoorListAppend` -- 2000/2000 over the whole 128 KB;
- the door request, C0/0C76 with `$0117 = $10` (-> C0/0D45 -> C0/28C0 -> the handler), vs
  `CtWramDoorListAppend` + `CtWramDoorOpen` -- 3000/3000 on the list and the map span
  `$7E:3000-$7F:2FFF`;

with every arm of both dispatches, all three exits of the append (new / already listed / full), tiles
with bit 15, the E6 carry and blocks wrapping into bank `$7F` counted in its liveness line.

`tools/door_list_cmp.py` (check `field-door-list-vs-logical-oracle`) runs three drives on both sides
and compares the list and the six cells EVERY frame:

| drive | list | cells | end |
|---|---|---|---|
| `reload` | 0 frames differ | f21-f101 | same |
| `walk` | 0 frames differ | f147-f231 | same |
| `load` | f19-f45 | (another location's map) | same |

The runs are the port's frame model, not the list:

- f21-f90 (`reload`) / f147-f220 (`walk`): the menu program's own use of `$7E:3000` (not modelled; the
  port's menu leaves the map alone);
- f91-f101 / f221-f231: the reload's NMI-off frames -- the ROM decompresses across them (the door
  closed from C0/A33B on) and reaches C0/28E1 at f102 / f232; the port does the reload's work at the
  RTL frame (f69 / f199) and holds black (`menuReloadW`);
- f19-f45 (`load`): the same for a location load -- the port runs the load's legs at the start of the
  black window and holds for the priced frames (`field_load_cost.md`), the ROM reaches C0/28AA at its
  end;
- (`walk`, until 2026-09-25: f98-f99, the door request two frames early -- fixed, section 7.)

## 6. Boundaries (named, not fixed here)

- The port's battle return does not re-decompress the map (C0/0316 `JSR C0/A33B`); C0/28E1 at C0/18C4
  runs over the map the battle left. A door the handler opened is still open there, so its arm does
  not match and the result is the ROM's (open); a listed door inside the rows the battle's stat blocks
  overwrite (`$5E2D-$63AC`) is garbage either way until the reload is ported.
- ~~The port's menu reload stages the map AFTER obj0's slot-1 re-run.~~ MEASURED and FIXED 2026-09-25
  (obj0_slot1_runner.md section 6): on loc 002 the reload and the location load both lost slot 1's `E4` copies, and
  the live VM's E4 had never written the WRAM map at all; both paths now stage before the slot-1 run, E4 runs
  C0/AF4E on WRAM, and C0/28E1 still follows the stage (check field-slot1-map-copies-vs-logical-oracle).
- The render's copy of the map is not rebuilt from the reloaded WRAM: the port's WRAM map does not
  carry the chest pre-bump (C0/A782 is applied to the render's copy only, `fieldLoad`), so following
  the WRAM would close opened chests on screen.

## 7. The door request's timing (fixed 2026-09-25)

The `walk` drive's list, cells, door sound and request count used to lead the cartridge by two frames
(`walk list f0-f290: 2 frame(s) differ [f98-f99]`, `port 5 append(s)` against `C0/28C0 4 visit(s)`). Lead
position (`$1802/$1882`) and camera (`$1D0E`) agreed frame by frame, so it was two separate mechanisms.

**(1) Which step raises the request.** C0/9C5C builds the destination from the lead's own words -- C0/8AB5..
load `$1800,X` / `$1880,X` into `$62/$64`, `9C5E` `$66=$70=$62+$2E`, `9C67` `$68=$64+$30`, `9C70` `$72=$68 -/+
$100` by the sign of `$30` -- and C0/9D35 tests prop1 bit 4 at `($73 & $1F, $71 & $1E)`. The port's transcription
(src/collision.c) is that, but `fieldPlayerCollide` seeded `$1880` as `py*16 - 1`: a "-1/16 foot anchor" from
when the host's `py` was the actor's integer foot pixel (op 0x8B's `$1880 = tile*256+$FF` = foot `tile*16+16`).
The lead sync went sub-pixel (ff73c06: `CtSceneSetActorPosF` writes `fy = py`, the A810 commit mirrors
`$1880 = fy*16`, `--loadwram` reads `py = $1880/16`), and the probe stood one sub-pixel above the lead's word.
MEASURED, the logical oracle `--monitor 7E1882:2,7E1D68:2,7E1D72:2,7E0117:1` against the port's `g_collRam`:

| frame | cartridge `$1882` step | `$1D72` (door row) | port probe `$1882` / lead word | port `$1D72` |
|---|---|---|---|---|
| f98 | 2B20 -> 2B00 | 2A00 | 2B1F / 2B20 | **29FF** -> request |
| f99 | 2B00 -> 2AE0 | 29E0 -> request | 2AFF / 2B00 | 29DF |

So the port saw row $29 one step early. The same off-by-one lived in both directions of the host: the three
C0/42B8 arrival sites built `pendY = (word + 1)/16` to feed the anchor, and the VM's `$1880` then came out
`word + 1` (`tile*256+$100` for `$FF` -- the next row's tile byte) on every unscripted arrival.

The fix is ONE storage: the host's `(px, py)` is the lead's `($1800, $1880) / 16`, the VM's `fx/fy`. The anchor
readers read the word (the collision seed, C0/9DDE's arrival tile, the treasure probe's `$1881`, `footPrio`, the
foot-tile exit scan, the plane lookups, the cold start's `$0103`); the anchor sources write it (the three C0/42B8
sites drop their `+ 1`; the default spawn and the two host hops -- loc 271's wake-up, `CT_CHANGELOC` -- become
`(tile*256+$FF)/16`, the word C0/42B8 writes for a `$0104` with bit 3 clear, which is what the cold start seeds).
The render follows: the lead is drawn at `(int)py` = `$1880 >> 4` like every other object (`CtSceneActor` hands
out `(int)fy`), and the camera's `leadStartY` = C0/9A55's `$0A80`, built from the same word.

**(2) When the request is consumed.** C0/0C76 -> `0C8A LDA $17 / BNE` -> `0C8F BPL ->0CC7` -> `0CC7 BIT #40 /
BNE` -> C0/0D3E (door, bit 4) `BRL ->16DC` (chest, bit 1) `BRL ->1770` (bit 5) runs at `C0/00A4`, the HEAD of
the main loop, ahead of the controller `C0/00A7 JSR C0/881E` (C0/9D35 raises bit 4) and the A handler
`C0/00AA JSR C0/1AAC` (C0/1E4C raises bit 1). A request is consumed by the next iteration. The port ran the door
and chest arms at the end of the frame, after both raisers. They now run at the frame's head, gated as the chain
is: no main loop in a battle, on the overworld, under a menu or on a C0/2848 ramp frame; `$0117` bit 7 up (a
staged exit) takes the exit arm; the shop's parked op takes 0CC7's. C0/0B96's `STZ $17` (the location load's
first leg) now clears the host's half of `$0117` too, so a request raised on the way out dies with the map.

MEASURED after the fix:

- door (`walk`): cartridge `$0117` bit 4 at f99-f102 (`--peek` + `PEEK_WRITEWATCH=0117:1`: C0/9D52 sets,
  C0/0D49 clears the next frame), append at f100; port request f99, appends f100-f103 (4). The door SOUND:
  cartridge `PEEK_WRITEWATCH=1E00:3` `$1E01=53 / $1E02=81 / $1E00=19` at `--peek` f101 = `--monitor` f100
  (`--peek` numbers one above: `$1882 = 2AE0` lands at peek f100 / monitor f99); the port posts
  `cmd=19 id=53 pan=81` at f100.
- chest: `--teleport 26 30 38 0 --method jmp` from party3_field (the lead at (488,623) under loc 26's chest
  (30,37)), A at f10: cartridge `$0117 = 02` at f11 (C0/1E50), cell `$7E:551E` FE -> FF and `$1E00 = 19`
  (id 65, pan 88) at f12 (C0/16EB); the port, seeded from that state's WRAM (`n:11,a:4`), raises at f11 and
  bumps + posts `19/65/88` at f12.

`door-list: walk list f0-f290: 0 frame(s) differ`, `walk port 4 append(s)`, `cartridge C0/28C0 4 visit(s)`.

### 7.1 The exit arm reads the head's `$0117` too (relanded 2026-09-26)

The first cut of (1)+(2) was reverted from main (e9f4cdf) because two locksteps moved:
`lockstep-forest-hop-crossing` (`project: loc` one frame early, f27 against the cartridge's f28) and
`lockstep-scene120-stairs-entry-lucca` (the box at f212 against f210). Both are the same mechanism, one the
anchor had been hiding. The foot-tile exit scan read one sub-pixel late under the old anchor, so a positional
exit staged one frame late -- and the port's C0/0C8A drain, which runs at the top of `CtSceneTick`, AFTER the
host's controller, then took its first DEC in the staging frame itself. Late + early = on time.

The ROM: C0/0C76 (`0C8F BPL`, `0C91 LDA $19 / BEQ / BMI / DEC $19 / RTS`) runs at `C0/00A4`, before
`C0/881E`, whose accepted step stages the exit (C0/9CA1 -> 9D24 `ORA #80 / STA $0117`). The staging iteration
has already passed its drain. MEASURED on the logical oracle (`saves/forest_exit.state`, Down from f10,
`--monitor 7E1884:2,7E0117:1,7E0119:1,7E0100:2`): the lead's `$1884` reaches `2F00` and `$0117 = 80` at f12,
`$0119` reads `0E` at f13 and `00` at f27, and `$0100 = 01F0` at f28 -- first DEC stage+1, commit stage+16.

With the anchor gone the port stages at f12 like the cartridge, and the drain now reads `$0117` as the frame's
HEAD saw it: `CtSceneHeadLatch` (field.c, beside the C0/0D3E arms, before the controller) latches the byte and the
tick's drain gates on the latch; hosts without a controller of their own (`--vm`, `--vmdiff`) read the live byte,
and the VM's own stagers (op 0xE0 & co.) run after the drain either way. Both locksteps read as before the first
cut: `project: loc CLEAN over 399 frames` (and `pos` now first differs at f74 on the overworld, was f28) and the
box at f210. The door and chest arms also wait out C0/1770's hold (`mlHead`): C0/00A4 does not run on the ALIGN
or STYLE frames, nor on the RESUME frame that re-enters at C0/00A7.

### 7.2 One commit, and the load one iteration after it

Measuring the stairs with the drain fixed showed two more things the early commit had been hiding.

**The commit ran twice.** The host's fade block committed (`CtWramCommitLocation`) on the frame it read `$0119 = 0`,
and the same frame's tick then ran the drain's own commit, so C0/0C9D's `$0105 = $0100` filed the DESTINATION as the
origin. Logical cartridge, the stairs run to 07A: `$0100 = 007A / $0105 = 01D4` (and `$0107/08/09 = 09 06 02`); the
port read `007A / 007A`. The commit is one routine now, `CtSceneExitCommit` (ct_vm.c, C0/0C9A-0CC4, idempotent), which
the fade block calls at the frame's head -- where the lockstep samples `$0100`, as the cartridge's C0/0C76 is at the
head of its iteration -- and after which the scene's `CtSceneTick` returns at once, as 0CC0 `LDX #06FF / TXS /
BRL C0/0000` leaves the main loop (the frame still counts on the VM's tick counter). Check
`field-exit-commit-files-the-origin`: the three flights' words, the cartridge's.

**The load's frame.** C0/0C9A commits and loads in the same iteration, so the cartridge's window starts at the commit
(468: commit f20, `C0/0B4E` at line 239, C0/2848 at f47, the first ramp INC at f52). The port learns the destination
from the tick's poll and loads on the next iteration. Charging the window from the commit (one frame less) made the
three stairs flights exact but read ~40 attract checks one frame EARLY: every scripted (op 0xE0) change the suite grades
against the logical oracle runs on "load one iteration after the commit, full price" -- because the price is short by
about that much (section 7: the object/script load C0/0A14, 56D4, B192, 56A6, 28AA is not priced; 148 lines on 468,
more with more objects). A positional exit reached the same frames only through its early commit. So both kinds now
run on that ONE accounting, and what it leaves is named, per location: 468's price over-rounds (30.46 -> 31) and the
second and third flights read +1/+2; the world-0 REST was fitted on the Truce market's positional exit and reads +1
there. Closing both = pricing the object/script load exactly, then charging every load from its commit.
