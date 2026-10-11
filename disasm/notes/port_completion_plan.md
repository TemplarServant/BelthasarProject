# Plan: porting everything that is left

Written 2026-09-06, right after the `$0162` decision-cursor port. Suite at the time: **384 pass,
0 fail**. This is the ordered plan for closing the remaining gap between the port and the ROM.

It is deliberately not a wish list. Every item below names the ROM routine, the dossier that holds
its derivation (or says that no derivation exists yet), and the shape of the check that would pin
it. Items with neither a derivation nor a way to reach them from a fixture are listed as **blocked**
with what would unblock them, not padded with guesses.

---

## 0. The method, per item — non-negotiable

Every item follows the same five steps, which is what the last four ports in this repo did:

1. **Disassemble** the routine (`tools/dis.py <hexaddr> <len> [m] [x]` — pass the m/x flags, the
   default decode silently garbles 16-bit stretches, which cost a wrong reading of `C0/1AB8` today).
   1a. **To find who touches an address, use `tools/refscan.py <hexaddr> [bank]`, never a hand-rolled
   byte scan.** Three wrong derivations in one session (2026-09-11) came from ad-hoc scans, and all
   three were confidently written down before being caught: a `STZ $299E` at `C1/33BC` that is really
   the `9C` inside `LDX $9C / STZ $9829,X`; five "callers" of `C2/3009` in banks `C4`/`E7`/`EF`/`F1`/
   `F3` (**`JSR`/`JMP` absolute are BANK-LOCAL** — those call `C4/3009`, `E7/3009`, …); and a
   "central expiry consumer" at `C1/B0EF` that turned out to be an `AND #FE`/`ORA #01` rebuild.
   `refscan.py` decodes forward from staggered anchors so it only reports real instruction starts, and
   labels every hit with **where the address actually lives** (bank-local / explicit long bank / "whatever
   DB holds"). ⚠ A routine with **no** direct caller is normally reached through a jump table, not
   missing — and a scan finding a READER has found an address, not a mechanism: disassemble it.
2. **Measure the oracle's own RAM** across the behaviour — `--monitor <addr:len> --diff`,
   `--followvm`, `--peek` sidecars, or `CT_FIELDLOCK` + `CT_PROJECT` for a live lockstep.
3. **Implement it gated on the same RAM**, in the ROM's own frame slot. No scene gates, no
   heuristics standing in for a flag, no eyeballed constants.
4. **Validate** against the oracle, not against ourselves: a lockstep, or a byte compare against a
   `--monitor` run. A check whose reference is our own output grades agreement, not correctness.
5. **Pin** with a regression check **plus a surviving-mutation proof**, then commit.

### 0.1 Do the verification pass FIRST

The single most repeated lesson in this repo's memory is that *"open" items in dossiers are often
already landed and only the comment is stale* — it has happened at least three times (the `C0/8A6D`
diagonals, `99DE`, the scheduler queue predicate; and in this session's own audit,
`--sweepsolid` already prints "nothing of C0/5B95 is left unported"). So phase 0 is:

> For every item in this plan, read the current code before writing any. Budget: one pass over the
> ~70 self-declared markers (`grep -rniE "is an invention|NOT PORTED|unported|not modelled|heuristic"
> src/*.c`), reclassifying each as **landed / really open / not portable**. Expect a meaningful
> fraction to be already done. This pass costs little and prevents re-deriving finished work.

#### 0.1a The pass, run — 2026-09-11, `move.c` + `main.c`

Done for the two files Phase B names, and §0.1 was right about the hit rate: **three of the four
markers examined were already landed and only the comment was stale.** Verdicts, each verified by
running the grader rather than by reading:

| marker | verdict | evidence |
|---|---|---|
| `move.c` CC17-CDBE "NOT PORTED, deliberately: the TILE half" | **LANDED** | `ctAnimBlit` / `ctAnimAppend` / `ctAnimTemplate`. `--sweepcad9` compares `$7F:3800-3FFF`, `$7F:4802+` and `$0940-$09B3`: 165888/165888, `0 class1/2-tilehalf`. Mis-scaling the blit's tile offset one bit → **8704 mismatches**, so it is graded, not merely present. |
| `move.c` CF5F/D2C9/D33C "NOT PORTED: the tile halves" | **LANDED** | the same three, reached through the `$1B00,X` pass selector; the `CtBlitSeg` descriptors sit ten lines below the comment that denied them. |
| `move.c` 0xE8 "the #1 unported opcode" | **LANDED (handlers)** | its own body already said "byte-exact and graded"; what is unmodelled is the DRIVER, which is §4's permanent floor, not a gap. |
| `move.c` `C0/A26B` / `C0/A2CE` "BOTH UNPORTED" | **REALLY OPEN, and correctly worded** | they ARE ported — in `src/ct_vm.c` (`[[ct-field-party-follower]]`). `move.c`'s runner is the SECOND VM in the tree (`[[ct-ab-scene254-lockstep]]`) and still traps. Wiring one to the other is its own item, not a comment fix. |

`C0/C73A` was the fourth item examined and was genuinely open; it is now done (above). Marker counts
after the pass: `move.c` 13 → 12, `main.c` 18 → 15.

### 0.2 The second law: find what is not marked at all

Markers only find what somebody already noticed. Two mechanisms found this week were invisible to
every marker and to the whole suite:

* the **line-break token's indent** (every indented line typed three frames late) — found only by
  running the oracle alongside and comparing *durations*;
* **`$0162`** (the whole decision-cursor machine absent, replaced by a host re-implementation that
  happened to pick the right option) — found only by byte-diffing the user's savestate against the
  oracle at the same station.

So each phase below ends with a **byte census**, not just its own checks: take a fixture in that
subsystem, run `--monitor` over the ROM's whole state page for it, and diff every byte against ours.
Anything that differs is either a port defect or an unmodelled byte; both need a verdict. This is
the only method that has actually found the last three defects.

---

## 1. Where the gap is, by subsystem

Self-declared unported/invention/heuristic markers, `grep -rniE "is an invention|NOT PORTED|unported|
not modelled|heuristic" src/*.c` — **re-measured 2026-09-11**:

| file | markers | 09-06 | character of what is left |
|------|---------|-------|---------------------------|
| `battle.c` | 31 | 14 | `C1/A4E0`, the `$F0` status-timer arms, `C1/E9D5`, a few SFX posters, the PC idle machine |
| `main.c` | 18 | 18 | mostly the sweep harnesses reporting what *they* cannot grade, not engine gaps |
| `move.c` | 16 | 13 | the remaining graphics-staging notes — `C0/C73A` and class 3 **landed** (see Phase B) |
| `field.c` | 13 | 9 | title/intro phases, `C0/17A0-1866` camera variants — the overworld song change **landed** |
| `ct_vm.c` | 10 | 9 | the **C2 text engine**, `C0/9F6F` hold path, one live invention (`$7F0054`) |
| `menu.c` | 3 | 3 | `$7E:1800` records pass, slot text (VWF `C2/EF65`) — `FF/FA6B` **landed** |
| `collision.c` | 2 | — | |
| `audio.c` | 2 | 2 | by design — the driver is *run*, not modelled |

⚠ **THIS NUMBER IS NOT A DEBT METER, AND IT MOVED THE WRONG WAY ON PURPOSE.** Every count above
either held or ROSE across a week in which `C0/C73A`, sprite class 3, all nine `C1/3760` move classes,
the `C1/B8BB` resolvers and `FF/FA6B` were ported. It rose *because* they were: the marker counts
**self-declared honesty**, so landing a routine that names the two sibling routines it does not reach
adds two markers and removes none. Read each row's character column, never the integer. The counts
that do measure debt are the suite's size and its killed mutations.

The field **event-VM opcodes are effectively finished**: `--sweepops` is 217 opcodes / 99.9% of
sites, and the 88 remaining sites are provably not SWEEPABLE (`0xDF`/`0xE1` never return to the
runner -- the live VM runs them as the C0/0000 reboot they are, field_op_e0_fadechange.md section 4;
`0xA3` is the ROM's panic stub). That is the one subsystem that needs no plan, only
maintenance.

⚠ The subsystem percentages in `[[ct-backlog-2026-08]]` (field ~95%, battle ~80%, menu ~80%,
overworld ~72%, audio ~65%) are from **2026-08-14**, when the suite had 193 checks. It now has 439
and a lot has landed since. Treat them as stale until phase 0 re-measures.

---

## 2. The phases, in order

The ordering is by **how likely the gap is to be visible to a player**, then by how much it unblocks
other work. That is the same criterion that made `$0162` worth doing before, say, the battle status
timers: the user kept hitting it.

### Phase A — the text engine `C0/2126` / `$C2:0009` (the last of the textbox)

The one subsystem the user's reports keep landing on, and now the only part of it still standing on
host code. Three ports today (`TB_IND`, `$0162-$0166`, `$0215`) each had to work *around* the fact
that the ROM's text engine is not modelled.

* **`$0131`** — the ROM's own LINE cursor, written by the handlers *and* by `C0/2126`. Not ported,
  so **no check can compare the LAYOUT itself, only how long it takes**. It is what made all
  fourteen line timings in `textbox.md` readable on the oracle side; without it our page/line split
  is graded only indirectly.
* **`$0215` / `$0216`** — `$0215` is now written by the host at one point (the reveal's end, which is
  the only place `C0/2024` reads it). The real byte is the control code the engine last fetched, and
  `$0216` is the in-string pause operand (`$0134 = $0216 * 15`, `C0/21A8`) — no string measured so
  far carries one, so it is derived-but-dormant.
* **`$0133` / `$0136`** — written by `2126` and by the state handlers; currently claimed by neither.
* the **state 8-C page re-decode** (`C0/20B2` + the `MVN $7E,$D2` block at `20B9`): the ROM re-decodes
  the page over five frames where the host still scrolls one line over eight.
* state 7's own `$0134` reader (`C0/2056 -> 20A5 -> 2058`) — the host currently owns the page/close
  decision, which is why the `$0162` cursor arm had to be ordered after the press block.

**Grading**: `CT_FIELDLOCK` + `CT_PROJECT=box` already runs the oracle alongside; add `$0131` to that
projection and the layout becomes gradable for the first time. Fixtures exist
(`saves/field_re/s439_marle_choice_lake.state`, `s434_decision*`).
**Blocked on**: nothing — ⚠ **but the first step is NOT `$0131`, and this item's ordering was wrong.**
`C0/2126` is transcribed in full as of 2026-09-11 (`textbox.md`, "C0/2126 TRANSCRIBED IN FULL"): 190
bytes, two dispatches around the `JSL $C2:0009`, and `$0131` is the **page-vs-line decision** — a line
break waits for a press only when `$31` has already reached 3 (`21CB`), so the engine counts lines to
four and never asks "is there more text". The port already hand-writes this routine's *effects*
(`CtSceneTextboxRevealDone` is literally `21D8` + `21C2`). What blocks `$0131` is that
`src/field.c` ends the reveal with `CtSceneTextboxRevealDone(f.vm, tbMore ? (isChoice ? 0x0B : 0x05)
: 0x00)` — **the host DECIDING a control code**, a heuristic standing in for a ROM byte — because the
text model normalises the codes into `'
'` / `''` / `TB_IND` and `05` vs `07` (and `09/0A` vs
`0B/0C`) are not recoverable from it, though the second dispatch splits on exactly that. So step 1 is
**carry the real control byte through the asset decode**; `$0131` then falls out in four lines and the
synthesised `$0215` deletes itself. `$C2:0009` is NOT needed for any of it.
⚠ **2026-09-28: not a behaviour defect.** `07`/`08`/`09` occur in none of the 5099 dialogue strings, and
every reader of `$0215` (C0/2024, C0/2058, both of C0/2126's dispatches) treats `05`=`06`, `09`=`0A` and
`0B`=`0C` alike -- exactly the classes the text model keeps -- so the host's code is the ROM's behaviour on
every shipped string; only `$0215`'s value differs in a lockstep (textbox.md, the 2026-09-28 paragraph of
"The blocker is NOT"). And the machine around it has landed since (tbMachineTick, `$0131` written by the
handlers and the head). What is left of this phase is the VALUE, not the behaviour.

### Phase B — the graphics staging halves (`move.c`)

The largest single coherent block left, and the one that keeps other sweeps from grading anything.

* ~~`C0/CC17-CDBE`~~ — the **tile half** of the sprite stage. **ALREADY LANDED** when this plan was
  written; only the comment said otherwise. See §0.1a.
* ~~`C0/CF5F-CF98` (32 tiles), `C0/D2C9-D302` and `C0/D33C-D370` (16 each)~~ — likewise **LANDED**.
* the **OAM bytes** of stage 2 (`$7F:4Bxx`), likewise through `$2180`.
* ~~`C0/C73A`~~ — **DONE (2026-09-11)**, and the item's premise was wrong twice. Its carry is not
  underivable, only data-dependent (path A's last `ADC $C5`, paths B/C's last `ADC #0008`); and class
  3 is **six groups of four — 24 pieces, not 16**, so the `(class & 3) + 1` entry count the draw pass
  and `src/oam.c` both used would have been wrong the moment class 3 reached a draw list. `C0/B5AF`
  *is* class 3's draw arm (six high bytes, `+$60`), not a dead end. `--sweepdrain` 144 → **192/192**
  with `emitters-left-unported` at 0; `--sweepdraw` 162 → **270/270** with class 3 in `CLSPAT`, which
  is what grades the 1/2/3/6 table. 6 mutations, 6 killed. See `anim_drain_cad9.md` §15.

Doing `C0/C73A` first unblocked the class-3 sweep, which is why it led this phase even though it was
the smallest piece — and it paid the way that ordering predicted: the entry-count defect it exposed
sat in three separate consumers.

**Grading**: the existing `--sweepdrain` / `--sweepanim` harnesses already report `unported-class`
counts; the exit condition is those counters reaching zero, the way `--sweepsolid` now prints
"nothing of C0/5B95 is left unported".
**Blocked on**: nothing.

### Phase C — battle

Ranked inside the phase by visibility:

1. ~~**Enemy move classes 0/2/3/5/6/7/8**~~ — ✅ **ALL NINE OF `C1/3760` ARE DONE (2026-09-11)**.
   Classes 5, 6 and 0 landed earlier; 2 (`C1/38BF`), 3 (`C1/3959`), 7 (`C1/3F5A`) and 8 (`C1/4021`)
   landed with `--sweepmoveclass` — **810/810 byte-exact**, eight mutations / eight killed, pinned as
   `sweepmoveclass-3760-classes-2378`. The "no capture can grade them" blocker was dissolved by the
   bank-`$C1` harness, exactly as item 2 predicted. See `battle_enemy_move_modes.md` V19, including
   the two setup defects it exposed (X/Y are SIXTEEN bits; `C1/28B0` reads pre-built box rows).
   The original item text follows, for the record. **Enemy move classes 0/2/3/5/6/7/8** (`battle.c:4538`). All four of the reachable ones are already
   derived in `disasm/notes/battle_enemy_move_modes.md` — class 5 (`C1/3BC2`, modes 0D/0E/0F, the
   forest Hetakes' wander), class 6 (`C1/3E33`, mode 0x0A, the ambush formation), class 0 (`C1/3772`,
   class 4's re-acquire half, **not** a no-op), class 2/3/7/8. Derived, not landed: this is the
   cheapest large win in battle.
2. **The AI target resolvers** — ⚠ THIS ITEM'S WORDING WAS WRONG and is corrected (2026-09-08,
   `battle_enemy_move_target.md` §F). The unresolved values fall through to the **LEAD PC**, not to
   NEAREST-PC, and NEAREST (value 06) is now DERIVED from `C1/A4AF → C1/2986 → C1/29B2` rather than
   fitted to captures. `C1/B8BB` holds **at least 20** entries, not 16; the targeting byte measured over
   nine captures takes `00/01/04/05/06/0A/0E/0F/10`, with **06 in all nine**. The port resolves 05 and
   06; the rest still fall back to the lead PC — a port decision, since `C1/A4AF` leaves `$AECB = 0`
   and lets the caller handle "unresolved". THAT fallback is what is left here.
   ⚠ **AND IT IS NOT "MORE CASES" — the whole family is disassembled as of 2026-09-11**
   (`battle_enemy_move_target.md` §G). X is **sixteen bits** in that block, and decoding it 8-bit turns
   it into `BRK`/`WAI` garbage, which is what had made it look unreadable. `C1/B8BB` is **24** entries,
   not "at least 20". The ROM's resolvers do not return a target — they **fill a LIST** at `$AECC` with
   the count in `$AECB`: 01 = ALL LIVING PCs, 02 = all living combatants, 03 = SELF, 04 = a stored
   target out of `$B19E`, and `0A`-`10` are one candidate filter (`C1/AE70`: the seat's block byte at
   `+$0A` tested against the mask `$AF1F`, skipping `$AEFF,Y == $FF` and block `+$1D` bit7) plus one
   reduction (`C1/ADA1` against `$B23A`). So `battleAiTarget`'s `int` return is the **wrong shape** and
   has to become a list before any of them can land. **§G.5's prerequisite comes first**: every
   `runSweep*` in `main.c` drives bank `$C0`/`$C2` and there is no bank-`$C1` micro-execution harness at
   all, so nothing on disk could grade a new resolver today. Build the harness, then port.
   ✅ **BOTH DONE, 2026-09-11** (`battle_enemy_move_target.md` §H). `--sweepaitarget` is the first
   bank-`$C1` micro-execution harness in the tree, and the twelve ported resolvers (00/01/02/03/04 and
   0A-10) are **1065/1065 byte-exact**, ten mutations / ten killed, pinned as
   `sweepaitarget-b8bb-resolvers`. ⚠ **Ported and graded is not WIRED**: `battleAiTarget` still falls
   back to the lead PC, because carrying the LIST into the action layer is §G.4's four costs and its own
   pass. The harness is also what unblocks move classes 2/3/7/8 in item 1 — same bank, same shape.
3. **Status TIMER arms** `$F0` (`C1/E006`) — currently *counted* (`g_btStatusTimedUnported`), not
   silenced, which is the right holding pattern; and the `$AE4D` bit1 sibling rider (`C1/E9D5`).
   ✅ **MAPPED 2026-09-11** (`battle_status_pipeline.md`, "THE TIMED-STATUS ARMS"), and it is **three
   pieces, not one**: the arms (`C1/E006`), a tick (`C1/823C`-`8284`) and an expiry consumer
   (`C1/B0EF`-`B1CF`). ⭐ A full-ROM operand scan shows **only bit `$80`'s timer `$AF27` is ever READ
   back** — `$AF32`/`$AF3D`/`$AF7F` are written by their arms and by nothing else in bank `$C1`, so
   three of the four arms are byte-faithful but observationally inert. Expiry is **bit 7 of the run
   flag**, set at `C1/826F`, not the timer hitting zero. ⚠⚠ **AND THE "CONSUMER" WAS A MISREAD, since
   RETRACTED in the dossier**: `C1/B0EF`-`B1CF` is a bit-0 REBUILD of the run flag from the block's own
   status byte, not a test of the expiry. Every one of the eleven run-flag reads in bank `$C1` is an
   `AND #FE`/`ORA #01` pair except the ticker's own three, and the ONLY site testing bit 7 is
   `C1/823F`'s own re-entry `BMI`. So two of the three pieces exist and the third does not resolve to
   anything in this bank yet — `g_btStatusTimedUnported` stays the right answer until it does. The
   bank-`$C1` harness would grade the arms and the tick today; it cannot invent the ending.
4. ~~**Battle entrance pose**~~ — **DONE (2026-09-08)**, and this item's wording was wrong twice:
   window **0x20 is armed nowhere in the ROM** ($5DF1 has four writers: #00/#30 at C1/34EB/34F7 and
   #00/#28 at C1/35BE/35CA), and `C1/0042` is not a vector but the BOOT'S TAIL — `JSL CD/0012 / BNE /
   JMP C1/34DB`, so the entrance ladder is armed the instant the entry-fade loop finishes. The arm alone
   broke 31 checks because the ladder stays armed after its last step; the fix is `CC/F071`'s wipe of
   $5DF1, which this engine had only at battle-BEGIN and which the oracle shows landing at COMBAT START
   (ambush: armed f360 by C1:34FF, cleared f531 by CC:F071). Pinned by
   `field-battle-entrance-pose-ladder` (2 mutations, 2 killed). See `battle_pc_pose_ladder.md`.
5. `C1/8555`'s `$2989 bit5` branch, the victory early-out, the accessory-effect pass
   (`C1/FC7F -> FD/B14D`), and the `CD/0E05` death-SFX poster — each currently traces rather than
   guesses, which is correct; they land when a fixture reaches them.
6. The dissolve gate is still flagged as invention (`[[ct-battle-end-survivor-mode6]]`).

**Blocked on**: items 5 and 6 need fixtures that reach them; see §3.

### Phase D — overworld and menu

* overworld: the op-0x30 table (`AND #00FF`, **167** words — the `_gen_owanim.py` docstring is
  wrong), the op-0x32 camera, the landing-tile heuristic in the Porre ferry
  (`[[ct-overworld-boat-ride]]`), the `C3:FFFF` reach of the anim bytecode.
* menu: the `$7E:1800` records pass (`menu.c:1353`), the save-slot text (VWF `C2/EF65`), op 0xC8's 47
  non-naming sites (⚠ operand `$00` is **not** the X menu — it goes to mode 5, screen `$07`, which
  the port does not have).

✔ **The overworld's own song change came off this list on 2026-09-11 — both halves were already
ported, and the entry was stale.** Chasing "who calls `C2/3009`" was the wrong question twice over.
There is no caller: `C2/2FDA` is state 1's body inside `C2/2F94`, **the music-zone object**, which is
not called by name at all — the generic ambient dispatcher `C2/0535` loads a block's CLASS WORD (+0)
and uses it *as* the handler address, so the music machine is simply the block carrying `$2F94`
(block 14, `$7E:0EB0`, on world 0). That is why no `JSR $2FDA`, no `JSL`, and no word `2FDA` anywhere
in bank `$C2` exists to find. `src/world.c:142` has the four-state cycle and
`src/world.c:942` runs it live under the dispatcher's own contract (CLC bumps `+3`, SEC kills the
class word); the ENTRY path `C2/2F0F-2F49` is `src/field.c:440`.
⭐ What was actually missing was the **grading**: `sweepowmusic-zone-switch` and
`sweepowregion-music-zoning` were two green micro-execution sweeps with **zero declared mutations**,
so nothing proved they were still wired to the code. The mutations their notes described had been run
by hand once and never pinned. Eight are declared now and all eight are killed — including the
even/odd nibble swap, which no live overworld data can see at all (all three measurable entry points
still report the same song, and the histogram over all 6144 cells of world 0 is byte-identical).

### Phase E — boot, title, and the long tail

* `src/boot.c`'s `$FD:C000` stub (the only real TODO in the tree besides one in `field.c`).
* the title screen's mode-2 logo phase and the intro animation before it.
* `C0/17A0-1866` (DP-`$1D00` zoom/camera variants + `JSL FD/FFF7 -> FD/E39C`).
* `$7F:1CC8` door-list replay (`C0/28E1`, called from `C0/18C4` right after the battle-return ack the
  port already has — narrowed, still open).
* the `$7F0054` gate in `ct_vm.c:3018`, flagged in-code as **an invention**: `$7F0054` appears
  nowhere in the ROM's own logic. Deleting it is a candidate before porting anything.

### Phase F — the census sweep (the part that finds what nobody marked)

Per §0.2, and the reason it is a phase and not a footnote. For each subsystem, one fixture, one
`--monitor` over its whole state page, one byte-for-byte diff against ours, one verdict per differing
byte. Budget it as real work: this is what found `$0162`.

---

## 3. Blocked on captures

These cannot be derived from anything on disk. Each needs a snes9x savestate at a specific moment
(`.claude/skills/capture-oracle-state` has the conversion recipe):

1. a battle reaching the **victory early-out** (a fight with no celebration) — `battle.c:7216` says
   it measured 0 in every fixture available;
2. a battle where the `CD/0E05` **death-SFX** poster fires;
3. a party member wearing an **accessory with an effect** (`C1/FC7F` bit6);
4. a **status timer** actually running (`$F0` arms);
5. an **op-0xC8 operand `$00`** screen (mode 5, screen `$07`);
6. the **new-game boat cutscene** (`[[ct-field-newgame-boot]]`, still open).

⚠ Two lessons that apply to all of them: a capture taken **mid-battle does not seed the port**
(`[[ct-defects-2026-09]]`), and a converted state carries **WRAM + CPU only** — positions and RAM are
sound, sprite **pixels** are not (`[[capture-oracle-state-splice]]`). As of today our own F5
snapshots can also drive the oracle (the `C0/5ABF` script-pointer writeback landed this session), so
for anything reachable in our engine a capture may not be needed at all — but the ROM's **text-decode
cursor** is still unported, so a state captured mid-string still will not reproduce the box machine's
position.

---

## 4. What is provably NOT portable — the honest floor

"Everything" has a floor, and it should be stated so completion can be declared rather than chased:

* **88 of the field VM's opcode sites** — `0xDF`/`0xE1` never return to the runner; `0xA3` is the
  ROM's panic stub. Recorded in `[[ct-vm-opcode-coverage]]`.
* **DMA is structurally invisible to the write census** — `snes/dma.c` calls `snes_write` directly,
  never `cpu->write`, so `--loadcensus` reports it as a class rather than resolving it.
* **`ct_lz_decompress` is a rewrite**, graded on its OUTPUT (the whole `$7F:8000-$FFFF` heap plus the
  `$0306/$0307` size report), not on the ROM's scratch. That is a deliberate, recorded exception.
* the **SPC700 driver is run, not modelled** (`audio.c`), which is the correct call and not a gap.

---

## 5. Exit criterion

The project is "fully ported" when, and only when:

1. every marker in `grep -rniE "is an invention|NOT PORTED|unported|not modelled|heuristic" src/*.c`
   is either gone or reclassified into §4;
2. every `--sweep*` harness prints its "nothing left unported" line, the way `--sweepsolid` does;
3. the §0.2 census diff is empty (or every differing byte has a written verdict) for one fixture per
   subsystem;
4. the suite is green **and** every check that pins a mechanism has a killed mutation — a green check
   with no mutation grades nothing (`[[grade-invariant-when-no-behavior]]`).

Point 4 is the one most easily skipped and the one that has caught the most defects.
