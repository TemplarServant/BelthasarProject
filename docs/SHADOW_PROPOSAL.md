# Shadow mode: routine-level validation against the oracle (proposal)

Status: **S0 done, S1 under way** (2026-10-11): the harness, the `--shadow` mode, C1/AF22 and the 211 field-script
opcode handlers. The rest of S1 (C0/ACFD, the battle and menu routines) and S2 onward are proposals.

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
| S2 | Battery saves as inputs, plus the first three routes (a boss fight, a shop, a late-game dungeon) | the suite runs the shadow over them; mismatches become ROADMAP entries |
| S3 | Census and `--shadow-poison` | a ranked list of ungraded routines; every `deadBytes` declaration has evidence |
| S4 | Make the live engine run the registered functions where it still runs its own copy (`step()`'s mirrors in `CtScene`, `src/ct_vm.c:1675-1764`) | what the shadow proves is what the port runs |

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

## Fit with the mandate

- **The oracle is the truth at every call.** The registry carries no scene gate, and a `deadBytes` declaration
  needs oracle evidence.
- **The mods are untouched**: the shadow grades the vanilla routines.
- **The no-emulation rule holds**, because the harness is test tooling over the oracle the project already has.

## Decisions

1. **The battery saves**: taken from zdiemer's repository, which he offered, and kept out of ours (2026-10-11).
2. **S0**: done (2026-10-11).
3. **Open: offer our routes for the endings, the dactyl ride and the bosses back** to zdiemer's project?
