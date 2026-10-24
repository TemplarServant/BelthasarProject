# The mutation audit of 2026-10-24

A check proves it grades something only through its mutations (`python tools/regress.py --mutate NAME`: apply the
edit, rebuild, the check must FAIL). This audit looked at how much of the suite that proof still covered, re-derived
what had drifted, and gave mutations to the checks that had none. Every mutation it added or changed was run: only the
ones `--mutate` KILLED are in `tools/regress_checks.json`.

## Before and after

| | before | after |
|---|---|---|
| checks | 954 | 946 (8 removed, below) |
| mutations | 2051 | 2193 |
| STALE mutations (their `find` gone from the source) | 226, in 160 checks | 3 |
| checks with no mutation | 188 | 27 |

365 mutations were proven: 201 re-targeted ones (of 223 tried: 70 by `tools/mutation_retarget.py`, 153 re-derived
from the code that does the same thing now), 162 new ones for 160 checks (of 172 tried) and 2 more for the checks
fixed below.

## How they had drifted

- **A refactor moved the code.** The battle host's fields became WRAM macros (`h->pcDefer[k]` -> `BT_PCDEFER(h, k)`,
  `h->mvLiveY[seat]` -> `BT_LIVEY(h, seat)`), ROM reads went through `CtRomByte`, code moved between files
  (field.c -> battle.c). `tools/mutation_retarget.py` re-targets the mechanical cases: the same edit on the code that
  replaced the old text, only when the refactor's own token rewrites explain the whole difference.
- **Line endings.** 18 edits were recorded from a CRLF working copy; on an LF checkout (Linux, macOS, the CI, a cloud
  session) they read STALE and nowhere else. `--mutate` now fits a find to the file both ways.

## What was decided (the owner, 2026-10-24)

- **Removed: 22 mutations** that were STALE and, re-derived on today's code, survived or did not build -- neither form
  proved anything. Each check's notes name what was removed; eight checks are left with no mutation by it.
- **Removed: 8 checks** that graded the oracle against a formula written in the harness, or measured the oracle itself,
  and ran no code of the port (a port that differs from the formula passed them): field-camera-truce-inn,
  npc-move-integration, tile-prio-recompute, oam-stage1-assembly, velocity-trig-circle, wram-loadcensus-clearlist,
  wram-loadcensus-natural-door, oracle-button-grammar. They are in `excluded` with that reason.
- **Fixed: 3 checks that graded less than they said.**
  - field-battle-atb-no-double: its expect (`gatoHP=59 pcAtk=1`) was printed by the first attack's end too, so Crono
    attacking twice still passed. It now expects the state after the enemy's third attack (`gatoHP=59 pcAtk=1
    enAtk=3`); its mutation removes the turn latch and Crono does attack twice (GATO 59 -> 25, pcAtk=2): the old
    expect was still printed five times, the new one is not.
  - field-battle-victory-settle: anchored on `strike#6+161`, which the retimed fight had moved mid-fight. It is
    anchored on the moment itself now, `victory+19` (victoryHold 20, as first pinned); the shots at victory+1..+55
    differ from the held pose in Crono's and Marle's boxes and +72 on equal it, so +19 is a pose of the settle-in
    ladder. A second mutation plays the ladder at double speed.
  - field-battle-song-spotlight -> **field-battle-song-pose-and-notes**: its shot draws the combatants on black and
    never the arena, so the spotlight -- the arena's own colour math since 2026-10-07 -- was not in its hash. It is
    named for what it grades (the singer's pose and the notes); the spotlight is graded against the cartridge by
    battle-gato-song-spotlight-is-the-arena-colour-math-vs-rule-cartridge.
- **Fixed in the harness:** an anchored shot (`CT_BATTLESHOT_ON`) had its picture overwritten by the battle's
  post-resolve dump, which only stood aside for `CT_BATTLESHOT_AT`. The printed fnv the checks grade was right; the
  `.raw` a person opens was the resolve frame. It stands aside for both now.

## Still open

**The mechanism is gone** (3 STALE mutations, to retire or replace): field-battle-enemy-death-sfx [0] (the 2-frame
countdown is now a flag posted on its first pass), slot1-runner-battle-menu-vs-oracle [1] (the two events it ordered
are frames apart now), battle-jsl-audio-keeps-playing [1] (the per-piece take exists only on the reference SPC core,
not the native sound machine the check runs).

**A new mutation SURVIVED -- the check has none** (9): field-battle-ambush-generic-dissolve,
field-battle-crit-beatB-dead-suppressed, field-battle-crit-flash2, field-battle-cyclone-fx-not-replayed,
field-battle-gato-facing-right, seq-pic-ending-vs-oracle, audio-song-loader-defrag, vmdiff-forest-battle-entry,
rom-census-attract. The edit broke the mechanism the check is named for and the check still passed: either the edit
missed the path the check's run takes, or the check does not see that mechanism. Each needs the check's output read
under the edit to tell which.

**No code of the port to mutate** (6): tables-datafree-headers-current, gen-shapes-match-generated,
inputs-come-from-recipes-or-sources and oracle-peekshots-equal-a-peek-per-frame grade a generated file, a manifest or a
tool against itself; field-worldtest-audio guards a NULL dereference (every break is the crash);
lockstep-forest-projections-clean's drive writes none of the four projections it compares. seq-gwarp-81/82/83-xlat-lockstep
and seq-gwarp-xlat-lockstep keep no mutation, as their notes intend.
