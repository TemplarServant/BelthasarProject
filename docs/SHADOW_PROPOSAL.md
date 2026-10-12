# Shadow mode: routine-level validation against the oracle (proposal)

Status (2026-10-12): **S0 done; S1 done but for the menus; S2 under way** -- the harness, the `--shadow` mode,
C1/AF22, C0/ACFD, C1/DC64, the 211 field-script opcode handlers and the battle AI (walk, 41 conditions, commit); the
routes from a public battery save (the Magus fight; the X-menu's Item and Equip screens) and the inventory routines
C2/8791 / C2/87D5. Equip's apply (C2/9CC1) and the save load (FF/FAA3) are not registered yet; S3 onward are
proposals.

## S0, as built

- `src/ct_shadow.c` / `.h`: the registry (`k_entries`), the entry/return/nesting/interrupt logic, the compare and the
  report. The oracle's CPU gets a second pair of hooks (`g_cpuShadowOpHook`, `g_cpuShadowIntHook`, `snes/cpu.c`) called
  after the logical mode's own, so neither owns the other's slot.
- `ct.exe --shadow [--frames N] [--loadstate S] [--btnscript B] [--dump PREFIX]` (docs/DEBUG_SURFACE.md).
- The suite check `shadow-attract-af22` (docs/TESTING.md): the attract demo from power-on under the logical rule makes 92
  calls to C1/AF22 (from frame 3991); all 92 match `battleRngBase`, 0 interrupted, 0 exits, 0 skipped, deterministic
  across runs. Three declared mutations are all killed: the cursor's `INC $26` (84 of 92 calls red, the first at
  7E:0026), the draw in A, and X's high byte.
- About 46 s for the 9000 frames with one entry.
- The battery saves for S2 are on disk under `saves/public/` (gitignored): 51 files, copied from zdiemer's
  repository at the commit in `saves/public/UPSTREAM_COMMIT.txt`, with their origins (fantasyanime.com,
  games.technoplaza.net, zophar.net) in `saves/public/SOURCES.md` and their hashes in `saves/public/SHA256SUMS`.

## Why

Today a C routine is graded three ways:

- `--vmdiff` / `CT_FIELDLOCK`: one frame at a time, a few fields per object (`src/main.c:306-750`).
- the `--sweep*` modes (54 of them): one routine at a time, fuzzed offline (`--sweepops`, `--sweeprng`,
  `--sweepaieval`, ...).
- the regression suite: 822 checks that look for substrings in what a run prints (`tools/regress.py`).

What none of them does is check every call to a routine, in real play, as the cartridge makes it. So:

- an invention survives until some scene happens to expose it (`bedIntro = sceneNo==2`, the player-vs-NPC
  gate: CLAUDE.md);
- a divergence is found by frame, and finding which routine caused it is detective work;
- coverage stops where our engine can go and where we hold a state (237 checks need private captures).

zdiemer/chrono-trigger-reimplemented validates each hand port in a *shadow mode*: the port runs on a copy of
the machine at every call the original makes, and the two are compared when the original returns. This proposal
takes the idea, not the code (that repository has no license file, so its code is all rights reserved), and fits it
to our layout: the oracle runs alone, and our C runs beside it on copies of its WRAM.

## The idea

The oracle plays the game: from power-on, from a state, or from a battery save, driven by a pad script or a route.
Each time its CPU arrives at the entry of a **registered** ROM routine, the harness:

1. copies the oracle's WRAM and the registers the routine takes;
2. runs our C function on the copy;
3. lets the oracle run the routine to its return;
4. compares the oracle's WRAM and output registers with the copy.

The port's main loop is not involved. **The coverage is wherever the oracle can go**, not where our engine can go,
so a battery save in the Black Omen grades our battle AI there even before our engine reaches it.

## What we already have

| Piece | Where | Use |
|---|---|---|
| Per-opcode hook | `g_cpuOpHook`, `snes/cpu.c:85,129` | entry detection (today owned by the logical mode, `snes/snes.c:422,435`) |
| Interrupt hook | `g_cpuIntHook`, `snes/cpu.c:87,125` | mark a call as interrupted |
| Return by stack climb | `CtOracleCallRoutine`, `src/ct_cpu_infra.c:162-226` | the same return test |
| WRAM-pure routine bodies | `CtFieldRunnerOne`, `src/move.h:129`; the families in `move.h:143-252` | the field VM ops, already shared by the live VM and `--sweepops` |
| Op → ROM handler table | `OPS[211]`, `src/main.c:4767`; entry convention `main.c:6386-6399` | registry rows for every VM op |
| Battle and menu functions on `uint8_t* mem` | `battleRngBase` (C1/AF22), `battleHitCore` (C1/DC64), `aiWalk`/`aiCond`/`aiCommit` (C1/AFD2, C1/B80D, C1/8C3E); `CtMenuInvAdd` (C2/8791), `CtMenuInvRemove` (C2/87D5), `CtMenuEquipApply` (C2/9CC1) | the first rows outside the field |
| Battery save into the oracle | `CT_ORACLE_SRAM`, `src/ct_cpu_infra.c:33-40` | the starting points for late game |
| State container | `src/ct_lake_state.c` | a repro state written at the first mismatch |

## Design

### The registry

One table, sorted by address. Each row has:

- `addr`: the 24-bit entry address.
- `name`.
- `pMask` / `pValue`: the M/X widths the routine is entered with.
- An optional `guard`: a peek-only predicate, for entries that the ROM shares between callers with different
  conventions.
- An `adapter`: it translates the oracle's registers into the C call and the C results into expected registers.
  For example, a VM op takes Y = IP and X = op×2 and calls `CtFieldRunnerOne` (its result's IP goes back to Y, the
  carry to P).
- `outRegs`: the registers the caller reads after the return.
- `deadBytes`: see "What is compared".

A 2 MB bitmap over the 24-bit address space keeps the per-opcode cost to one bit test.

### Entry and return

- **The hook.** `g_cpuOpHook` is a single pointer, and `lfOpHook` owns it. Add a second slot, called after the
  logical mode's, so that the logical rule is not touched and the shadow runs in both modes.
- **The return.**
  - At entry, the harness records S and the return address the caller pushed.
  - The call ends at the first opcode fetch with S above the entry S, the test `CtOracleCallRoutine` uses.
  - If the PC there is not the pushed return address, the call counts as *left without returning* and is reported,
    not compared. Jump-table tails and PLA/PLA returns show up in this count and are dealt with entry by entry.
- **Nesting.** A stack of up to 16 active shadows. A registered routine called inside another is checked on its own.
- **Interrupts.** If `g_cpuIntHook` fires during a call, the call counts as *interrupted* and is not compared. Under
  the logical rule the NMI only lands at waits, so a routine with no wait should never be interrupted; the count
  says whether that holds.

### What is compared

- **WRAM: all 128 KB**, except the stack bytes below the entry S that the routine pushed and popped.
- **The `outRegs`.**
- **No memory is "don't care" by default.** Our C does not always write the ROM's temporaries (direct-page scratch).
  A byte may be put in an entry's `deadBytes` only with evidence from the oracle:
  - `--shadow-poison` runs the oracle twice from the return, once with the byte poisoned;
  - it compares WRAM and the picture over N frames;
  - the declaration stands only if the two runs agree.

  This is a read of the ROM, not a guess.
- **Hardware side.** Routines that write VRAM or the APU ports are compared through the sinks the port already
  has (`g_ctVramSink`, `g_ctApuPort1`, `src/move.h:105,200`), against the oracle's write log over the call
  (`CtOracleInstallWriteHook`). That log does not see DMA, so a routine that starts a DMA is compared on the VRAM
  image at its return instead.
- **Timing is not compared**, in line with the no-emulation rule.

### Reporting

- **One line per entry:**
  `C1AF22 battleRngBase  calls 5012  matched 5012  mismatched 0  interrupted 0  exits 0`
- **A suite check** passes on `shadow: mismatched 0`, in the suite's substring style.
- **At the first mismatch the harness writes:**
  - the detail, e.g. `WRAM[7E:1DC7] oracle 73 port 42 (C0/68C6 var family, call #1234, frame 5021)`;
  - a CTLS state at the routine's entry, which is a ready-made repro for a sweep or for `--vmdiff`.

### Where the oracle goes: inputs

1. **Power-on and the attract demo.** These need only the ROM.
2. **The recipe states** (`tools/recipes.py`).
3. **Battery saves.**
   - zdiemer's repository carries 51 public saves that cover the whole story, from the kidnapping to Lavos and the
     New Game+ endings. They come from fantasyanime.com, games.technoplaza.net and zophar.net, and are listed in
     its `oracle/saves/README.md`.
   - They hold progress, not cartridge data, but their redistribution terms are unstated. So they stay out of the
     repository and are listed in `tools/regress_inputs.json` with a sha256 and a `how` that names the source.
4. **Routes, not movies.**
   - zdiemer's `.ctm` movies are frame-timed inputs from power-on on his runtime's timing. Under our logical rule
     (no lag frames, NMI-off loads at phase 0, one frame of SPC per frame) they desync.
   - So the routes are written as Python scripts on `tools/ct.py`. They read the oracle's RAM (dialog open, battle
     menu ready, location) and press buttons, so they survive timing changes.
   - His movie list is a good map of what to cover. By his own account it still lacks the endings, the dactyl ride
     and some boss fights; our routes for those could be offered back.

### The coverage measure

- **The census.** A `--shadow-census` run counts the JSR/JSL targets the oracle reaches and the cycles spent under
  each, and subtracts the registered entries. The result is a ranked list of what real play runs that no C
  function is graded against, which is our burndown.
- **The progress number** is the share of cycles that run under a registered entry.

## Phases

| | Work | Done when |
|---|---|---|
| S0 | Hook slot, registry, return/nesting/interrupt logic, report, one entry (C1/AF22 battle RNG) | the attract demo runs with `mismatched 0` and the counts are plausible |
| S1 (field VM done) | Register what the sweeps already grade: the 211 VM ops through `CtFieldRunnerOne`, C0/ACFD, C1/DC64, the AI walk/conditions/commit, the menu inventory and equip | each one shadowed over the attract demo and the recipe states, and a suite check pinned |
| S2 (first route done) | Battery saves as inputs, plus the first three routes (a boss fight, a shop, a late-game dungeon) | the suite runs the shadow over them; mismatches become ROADMAP entries |
| S3 (liveness watch done) | Census and `--shadow-poison` | a ranked list of ungraded routines; every `deadBytes` declaration has evidence |
| S4 (first mirror removed) | Make the live engine run the registered functions where it still runs its own copy (`step()`'s mirrors in `CtScene`, `src/ct_vm.c:1675-1764`) | what the shadow proves is what the port runs |

## What it does not do

- **It does not grade integration**: the scheduler, the mirrors, the render, the order of things in a frame.
  `--vmdiff`, `CT_FIELDLOCK` and the suite stay.
- **A routine that waits for frames cannot be shadowed whole.** Its pieces between waits can.
- **It grades only the calls the runs make**, which is why S2's routes matter.
- **It adds nothing to the port.** The harness lives in `ct.exe`, on the oracle's side.

## S1, the field VM's opcode handlers, as built

- The `--sweepops` table moved to `move.c` (`g_ctVmOps`, `CT_VM_NOPS`) so the sweep and the shadow register the same
  211 handlers. Each is entered `JSR ($5D6E,X)` from the runner C0/5AB1 with D = $0100, X = op*2 and Y = the IP; the
  adapter runs `CtFieldRunnerOne` and hands back the IP in X and the carry; C3/0002's workspace $0308-$0316 is not
  compared, as in the sweep, and the report says so.
- The attract demo: 26926 calls reach 91 of the 212 entries; 24781 matched, **0 mismatched**, 0 interrupted, 0 exits.
- **What the first run taught.** It reported 1879 mismatches on ops 94 and 95, every one on the IP alone. They were the
  harness's: op B6's handler (C0/354F) runs op 95's body (C0/5410) as a subroutine with the same X and Y, and op B5 op
  94's, so the guard that only matched the byte at the IP against X accepted a call that is part of another handler.
  The guard now asks that X carry the entry's own opcode; those 2093 calls are `skipped`, and B5/B6 match at their own
  entries.
- **What it found unported.** 48 calls `CtFieldRunnerOne` traps: the sound posts E8/EA/EB/EC, 8 cases of op FF and 1 of
  op 83. The live VM (`step()`) runs those elsewhere; S4 is where the two meet.
- **What a mutation found.** A carry mutation on op 2D's held-button arm survived: the attract presses nothing, so that
  arm never runs. The check's mutation now sits on the arm the demo takes, and the held-button arm waits for S2's routes.

## S1, C0/ACFD and the battle routines, as built

- C0/ACFD (`CtBuildVelocityPriority`, X = the object, D = $0100), C1/DC64 (`battleHitCore`), and the battle AI: the walk
  C1/AFD2 (A = the AI id), the 41 condition handlers read from the ROM's own table at C1/B80D (X = the op doubled), and
  the commit C1/8C3E. Several entries may share an address; the first whose convention holds takes the call.
- **What the first run taught.** `--sweepaieval` stops its comparison at C1/8C53, inside the commit (condition 00 is
  `JSR C1/8C3E / RTS`), and the first adapters asked the C for that: 6 "mismatches" that were the sweep's cut, not the
  cartridge's. Every one of these calls returns, so the shadow runs the C WHOLE, commit included
  (`CtBattleAiWalkWhole` / `CtBattleAiCondWhole`, battle.c).
- **What it found unported.** Commits whose target resolver (05/06/07/17) only the live battle host has: the C flags
  them (battle.c's unported flag 1) and the shadow counts them `unported` at the walk, the commit and condition 00.
- The attract: 30100 calls reach 99 of 257 entries, 0 mismatched; C1/DC64 is not reached (the demo makes no physical
  attack).

## S2, the first route, as built

- `--shadow` reads a route file (`--btnfile`, the `--btnscript` grammar over many lines with `#` comments), writes the
  oracle's state at the end (`--stateout`) and names the frame and location it ended at, so a route is built one leg at
  a time and looked at between legs (`--peek` + `tools/ct.py fb2png`).
- `tools/shadow_route.py leg` plays one stretch from a state and shows where it ended (the picture, the frame, the
  location); `check` plays a whole route. Route files repeat a pad pattern with `(tokens)*N`.
- **Every route's timings are this project's** (the user's decision, 2026-10-12): a route is written leg by leg from the
  oracle's own pictures. A first version replayed zdiemer's `battle-magus` movie input aligned on the slot load -- it
  stayed in step -- and was replaced.
- `tools/shadow_routes/magus_fight.btn`: from `saves/public/fantasyanime/ctsave07-azala.srm` (slot 1, Magus' Castle),
  the load screen (it opens on the save's last slot, 3), down the dark stairway (the Vamp ambush, fought by attacking),
  up into Magus's hall, the summoning, and the fight, attacked until the party falls.
- The check `shadow-route-magus-fight`: 88644 calls to 87 entries, 0 mismatched; C1/DC64 11 calls, AI conditions 08,
  0B, 11 and 18 reached for the first time.
- `tools/shadow_routes/menu_items_equip.btn` (check `shadow-route-menu-items-equip`, quick tier): the same save's X-menu,
  four Tonics on Crono and four equipment changes; with it C2/8791 (inventory add) and C2/87D5 (remove) are registered
  (`$04C9` the item, `$04CB` the count, A the verdict).
- **The first port defect the shadow found.** On the first equip change it graded, C2/87D5 left 7E:04C7 at 13 on the
  cartridge and 00 in the port: C2/87FA, the inventory's find, ends its found arm with `880E STX $04C7 / 8811 STZ
  $04C8` -- it records the slot -- and `mInvFind` did not; it also scans all 256 slots (an 8-bit X that wraps), and the
  add's empty-slot search with it, where the port stopped at the bag's $F2, and the add's `ADC $04CB` is 8-bit before
  its cap of 99. menu.c now does all of it; the suite (quick 588/0/0, the 67 full-tier menu/shop/item checks) stays green,
  and two mutations pin the find's record and the remove's emptied slot.
- Next for S2: a shop (buying and selling reach the inventory routines with counts above 1), the dactyl ride, the
  endings, and the boss fights zdiemer's library lacks. Equip's apply C2/9CC1 is not a routine to shadow (after its
  two inventory calls it ends in `JMP C2/8255`, the menu's state machine); the save load FF/FAA3 builds an MVN in WRAM
  ($9690) and stages its table at $9890, scratch the C does not keep -- it waits for the liveness watch to reach past
  the direct pages.

## S1, the rest of the sweeps' routines, as built

A survey of every `--sweep*` mode (its ROM entry, C function, convention, compare and hazards) picked the routines a
live call can be graded on. Registered: C0/ABA2 (the move heading), C0/AB45 (the screen position), C0/A98A (the draw
list's unlink), C0/C98A (the animation timer), C0/9E84 (the player's facing and frame), C0/B701 (the animation
release), C0/CAD9 (one drain visit), C0/305D (the arrival snap), C1/C90B and C1/C92A (the battle's multiply and divide),
C1/C8F7 (the seat redirect), C1/D174 (the stat fetch), C1/B279 (the dead flag) and C1/F205 (the victory's tech pass).
The field's per-object routines read their object from $016D (`LDX $6D`), as the ROM does, and their widths are
the live calls' (C0/AB45 is entered with X 8-bit, where its sweep used 16).

- **What it found.** C0/B701's OAM emitters keep their high-table accumulator in the direct page -- `STA $E6` (the
  piece's X bit 8), `A = ($E5 << 2) | $E6`, `STA $E5` but on the group's last piece -- and from the second group on
  the loop's end too (`$E7` = the record base + $20 per group). The port computed both in locals: 404 calls left
  $01E5/$01E7 otherwise. move.c's ctOamEmit now stores them; C0/B701 matches 18573/18573 on the attract and
  37534/37534 on the Magus route.

## S3, the liveness watch, as built

A call whose registers match but whose WRAM differs only in the direct pages ($0000-$01FF, the engine's scratch, which
no DMA reads) is not graded at once. The harness hooks the oracle CPU's reads and writes (`cpu->read` / `cpu->write`,
the WRAM port $2180 resolved through its latch) and watches the differing bytes: the cartridge's next access decides.
A write to every one of them -- nothing ever read what our C left there -- makes the call `dead`, and its addresses
are listed (`dead scratch <entry>: ...`); a read first, a read-modify-write included, makes it `mismatched`, with the
reading PC. Calls the run ends on before their bytes are touched are `unresolved`. A difference outside the direct
pages, or in a register, is graded at once as before.

- C0/CAD9's 2905 attract differences are all dead: $01C5-$01C6, $01CD-$01D9 and $01EE, the drain visit's temporaries
  (the frame lookup C0/CB3A's $D6/$D9, the template builder's $C9-$D8, the copier's $C5), rewritten by the next
  user every time; the same set on the Magus and menu routes. The evidence is the cartridge's own access order on
  every call the runs make, not an assumption -- and the mutations that change a byte some code does read (the RNG's
  cursor $26) stay killed through the watch's read arm.
- The watch is the S3 mechanism the proposal asked for (`--shadow-poison`), measured instead of poisoned: no second run
  of the oracle, so no question of the logical rule's state between two runs.

## S3, the watch over all of WRAM, as built

- The DMA reads and writes WRAM without the CPU (the OAM buffer, the WRAM port), so the first watch stopped at the
  direct pages. `snes/dma.c` now calls `g_dmaBusHook` before every A-bus access a DMA or HDMA transfer makes (and an
  HDMA table read), and before a B-bus WMDATA access; the watch hears those as it hears the CPU's, so it covers all
  128 KB. Nothing else reads WRAM (the SPC and the PPU have no path to it). A call may now differ in up to 96 bytes and
  still be decided by the watch.
- **An assumption replaced by a measurement.** The VM handlers no longer exclude C3/0002's workspace $0308-$0316 (the
  exclusion --sweepops makes because ct_lz_decompress keeps none of the ROM's scratch): on the attract the watch finds
  every differing byte there rewritten before any read (ops 82 / 83, 41 calls).
- **FF/FAA3, the save load, registered** (JSL, DB = $7E, the slot from $79 & 3, the records out of the oracle's own
  battery RAM). Its records matched at once; its scratch did not, and the watch caught one byte of it LIVE: the next
  fight's D1/E7A1 `MVN $7E,$7E` copies $0000-$002A to $CC65, so the $00 the load leaves (the last record's address,
  $2C7C) reaches the battle. A first-access rule cannot follow a copy -- so it is not argued dead: CtMenuSaveLoad now
  leaves the ROM's scratch byte for byte (the `MVN $30,$dd / RTL` trampoline at $9690, the record table at $9890, $7B =
  the slot's base, $9691 / $00 = the last record's bank and address), and the load matches exactly on both routes.

## S3, the census, as built

- `--census N` counts every routine the run calls -- the target of each JSR / JSL / JSR (abs,X), taken at the
  instruction fetched right after it (an interrupt in between clears the mark) -- and reports the share of the calls
  whose target a registered entry sits at, then the N routines called most that none does. `shadow-attract` pins its
  line: the progress number.
- **The attract, 2026-10-12: 8,835,267 calls to 1039 routines; 143,152 (1.6%) to the 113 routines an entry sits at.**
  Three targets carry most of the volume: 7E/3176 (code in WRAM, 6.5 M calls), CD/3E44 (742 k) and CD/0036 (546 k).
  Below them the list is the work order: C0/A88D (the activation gate, a partial port), C0/AA07, C2/225E, C0/E687,
  C0/5A93 (the VM's runner), C0/B309 (the draw per object), C0/A947, ...
- C0/A9CD (`CtObjListLink`) registered from it: 22407 calls, 10506 matched and 11901 dead -- its only difference,
  $01C5 (A9F2 `STY $C5`, which the port does not store), measured dead on every call.

## S4, what the census and the first mirror point at next

The battle host (`CtBattleHost`, battle.h) keeps dozens of WRAM variables in fields of its own -- the enemies' movers
($974A, $9752, $9786, $9737, $98A7, ...), the pose timers ($9694, $969C, $9689, $968C, ...), the PC grid walker ($A500,
$A517, $A53D, ...), the victory ladder ($5DC1, $5DBD, ...). Each is a place where the live engine can differ from the
ROM out of the shadow's sight, as `objPal` did. Making them the WRAM is a rewrite of the battle engine's state, mechanism
by mechanism; it is the next large step and is to be agreed before it starts. The way in is the same as for `objPal`:
register the routine that owns the variables, let the shadow show the WRAM the host never wrote, and move the variable
into the WRAM.

## S4, the enemies' mode / facing / idle-pose state in the WRAM, as built

- Twelve host fields of `CtBattleHost` became the WRAM bytes they mirrored, behind named accessors (battle.h): the move
  mode `$974A` and its request `$5E0D`, the facing `$96DE` and the built facing `$96E9`, the arrival latch `$9829`, the
  idle walk's timer `$9694`, cursor `$969C`, published pose `$990A`, defer flags `$9925`, window poses `$97D9` and
  dwells `$9799`, and the group counter `$A4` -- about 140 uses in battle.c and field.c.
- **The ROM's own writes now reach the engine.** The AI's counter (C1/87FE `STA $5E0D`) and move decode (C1/982D /
  988B) wrote the request into the WRAM, where the mirror never saw it. On the bat9 fight the counter at f671 is now
  committed by the walk at f672 (mode 01 -> 0A, as on the cartridge), and the fight ends 8 frames later
  (`field-battle-bat9-return`, our own numbers, re-pinned with the measurement). Every oracle-compared check is
  unchanged; full suite otherwise green.
- **The idle walk C1/2D9F, shadowed (enemy groups).** Three mechanisms had to become the ROM's before it matched:
  1. **The gather C1/1C4A**, byte for byte (`battleCellGather`): a published pose's cells into the WRAM sprite mirror
     (CC:F941 + the layout's rows, tiles from the sheet `$1C57`, bit-14 cells through C0:FD00), the cells' dx/dy into
     `$9F47` (sign-extended), `INC $9912`. `$9912` is the WRAM byte now (STZ at C1/2D9F), and `$967B` / `$9679` are
     stored where C1/3428 / 3430 store them. (C1/1F75 is the gather's own tail, `INC $9912 / RTS`.)
  2. **The presence flag.** The walk gates on `$96F8,X` (C1/3349); the port read a stand-in -- the staged record's
     byte +6 bit 7, which a death never changes -- and kept visiting delisted slots (48 of the 50 mismatches). The port
     already staged `$96F5` at the boot; it now clears it as CF/FE7B does (when a corpse's death routine ends, or at
     once for the `$9FF7` / `$99B5` arms), and `battleSlotStaged` / `battleMirrorPresent` read the flag itself.
  3. **The commit's direct page.** C1/3370-3405 leaves `$C0`-`$C5` (the pose / dwell pointers), `$A5`/`$A7`/`$A8` and
     the product of C1/00A7 (`$77`/`$78` included), `$86`, `$82`, `$80`, `$84`, `$9C` -- and D1/FE8A's multiply reads
     `$77`/`$78` as they stand the next frame (the watch measured both read).
  On the Magus route: 248 calls, 0 mismatched, 185 dead (`$80`, `$9C`, `$9841`, `$992D`-`$992F`), 62 unported (the PC
  group, still the host's `pc*` fields). The live battle follows the cartridge more closely: the walk no longer commits
  dead slots, which moved two drives of our own (field-battle-end-uiteardown, battle-slow-doubles-reload-and-cuts-damage,
  re-pinned with the measurement; every oracle-compared check unchanged). Two mutations pin the flag and the gather.
- `--dumpcall N` (with `--dump` / `--dumpaddr`) dumps an entry's Nth call, whatever its grade.
- **The PCs' idle-walk state, moved too** (`BT_PCTIMER` $9689, `BT_PCIDX` $968C, `BT_PCPUB` $9907 -- FF = none, as the
  ROM's own byte reads, where the host used -1 --, `BT_PCWINLAST` $A103, `BT_PCDEFER` $9922, `BT_PCFACING` $96DB,
  `BT_PCAIMLATCH` $A098): a pure copy, the full suite unchanged (831/0/0).
- **The PC group of C1/2D9F, tried and not yet matching** (54 of its 62 calls on the Magus route), for three reasons the
  trial measured, the next step: (1) the PC walk's own direct page -- its C1/00A7 / C1/00D7 operands, `$77` read by
  D1/FE8C the next frame; (2) the gather's dx/dy table for a PC (`$9F49` one off, E8 against E9), read by the OAM
  compose CF/EB42; (3) whole PC cells (up to ~490 bytes) -- the PCs' sheet / record, which the gather reads as it does
  an enemy's. It also needs the feet from the WRAM ($1D0C / $1D23): the host's `mvLiveX` / `mvLiveY` (~220 uses) are
  the largest mirror left.
- **The feet are NOT a plain copy -- tried and reverted (2026-10-12).** Moving `mvLiveX` / `mvLiveY` onto $1D0C / $1D23
  turned 32 checks red. The cause, measured on field-battle-entrance-initial-pose: at the forced battle end (f83) the
  feet read from the WRAM are (0,0) / (32,15) -- in the port the FIELD engine also writes that page (D = $1D00 is the
  field collision's direct page) while or right after the battle runs, where the cartridge's two engines do not share
  those bytes that way. The host mirror shields the battle from an overlap only the port has. Moving the feet needs
  that overlap resolved first -- which engine owns $1D00-$1DFF when, as the ROM has it -- an architecture step of its
  own.

## S1, the battle effect program's dispatch, as built

- CD/16A6 (`STX $43 / ASL / TAX / LDY $40 / INY / STY $40 / JSR ($16B5,X) / LDX $43 / RTS`): one entry grades every
  ported effect op through `CtBattleFxProgOpMem`, the adapter storing the $40 it hands back and $43/$44 = the slot, as
  the dispatch does. `--shadow` loads the battle ROM (`CtBattleLoadRom`) for it.
- The attract: 4988 calls, 4039 matched, 949 dead -- the dead scratch is $0003-$002A and $0045-$004D, exactly the ranges
  the effect sweeps excluded by assumption; the watch measures them dead on every call. The Magus fight: 1704 calls.

## S4, the first mirror removed, as built

- **What the shadow found.** On the Magus fight, effect op C9 (CD/1B78: `INC $A101`, EOR #08 on $993C-$9946 and $9F46)
  left $993C-$9946 as it found them in the port: the battle host kept the combatants' OBJ palette bytes in a mirror
  (`objPal`, and `objPalDef` for $9888) and the op EORed the mirror, not the WRAM.
- **The fix is the mechanism, not the op.** The mirror is gone: `BT_OBJPAL(h, k)` / `BT_OBJPALDEF(h, k)` (battle.h) are the
  WRAM bytes $993C,X / $9888,X, and the 33 uses in battle.c and field.c go through them -- the boot's CC/E6F8 / CC/EAAB
  stores, ops C9 / 69 / 6A, CF/FE57's flash, the compose snapshot. The full suite is unchanged (828 of 828 outside the
  shadow's own three checks, which gained the entry), so the mirror was only ever a copy; now the shadow can grade
  what the port runs. A mutation that drops op C9's EOR on the WRAM is killed by `shadow-route-magus-fight`.

## Fit with the mandate

- **The oracle is the truth at every call.** The registry carries no scene gate, and a `deadBytes` declaration
  needs oracle evidence.
- **The mods are untouched**: the shadow grades the vanilla routines.
- **The no-emulation rule holds**, because the harness is test tooling over the oracle the project already has.

## Decisions

1. **The battery saves**: taken from zdiemer's repository, which he offered, and kept out of ours (2026-10-11).
2. **S0**: done (2026-10-11).
3. **Open: offer our routes for the endings, the dactyl ride and the bosses back** to zdiemer's project?
