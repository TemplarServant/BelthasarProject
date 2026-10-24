# The mutation audit of 2026-10-24

A check proves it grades something only through its mutations (`python tools/regress.py --mutate NAME`: apply the
edit, rebuild, the check must FAIL). This audit looked at how much of the suite that proof still covered, re-derived
what had drifted, and gave mutations to the checks that had none. Every mutation it added or changed was run: only the
ones `--mutate` KILLED are in `tools/regress_checks.json`.

## Before and after

| | before | after |
|---|---|---|
| mutations | 2051 | 2213 |
| STALE mutations (their `find` gone from the source) | 226, in 160 checks | 25 |
| checks whose every mutation was STALE | 48 | -- |
| checks with no mutation | 188 | 28 |

363 mutations were proven this way: 201 re-targeted ones (of 223 tried: 70 by `tools/mutation_retarget.py`, 153
re-derived from the code that does the same thing now) and 162 new ones for 160 checks (of 172 tried). The ones that
did not prove are listed below and were left as they were.

## How they had drifted

- **A refactor moved the code.** The battle host's fields became WRAM macros (`h->pcDefer[k]` -> `BT_PCDEFER(h, k)`,
  `h->mvLiveY[seat]` -> `BT_LIVEY(h, seat)`), ROM reads went through `CtRomByte`, code moved between files
  (field.c -> battle.c). `tools/mutation_retarget.py` re-targets the mechanical cases: the same edit on the code that
  replaced the old text, only when the refactor's own token rewrites explain the whole difference.
- **Line endings.** 18 edits were recorded from a CRLF working copy; on an LF checkout (Linux, macOS, the CI, a cloud
  session) they read STALE and nowhere else. `--mutate` now fits a find to the file both ways.

## Still open (a person's call)

**Re-derived, not proven -- kept as they were (STALE)**: the re-targeted edit SURVIVED or did not build, so either the
re-derivation missed or the check no longer grades the mechanism. Each needs a look at the code and the check together:
field-battleloop-gato [0], field-stairs-run-three-flights [1], field-battle-entrance-ladder-hold [1] (build),
battle-gato-death-skips-the-death-routine-no-sound [0], battle-naga-ettes-walk-as-the-cartridge-through-their-turns [0],
battle-flame-toss-band-vs-rule-cartridge [0], field-savepoint-a-opens-the-save-screen-vs-rule-cartridge [0],
field-organ-door-opens-live [0], field-battle-idle-walk-boot [4] (build), field-battle-class5-flip-seed [0] [3],
field-battle-class6-rewind-seed [1] [2] [3], field-battle-hud-name-after-victory [0],
sweepmoveclass-3760-classes-2378 [0] (build), boot-load-save-sound-sequence [0] (build),
attract-battle-sfx-bracket-vs-oracle [3], attract-0ad-battle-boot-exit-return-vs-oracle [4],
attract-title-tick-with-its-pendulum-vs-cartridge [0], menu-window-colour-field-textbox-vs-oracle [2],
field-e5-copy-service-vs-logical-oracle [3].

**The mechanism is gone** (STALE, to retire or replace): field-battle-enemy-death-sfx [0] (the 2-frame countdown is now
a flag posted on its first pass), slot1-runner-battle-menu-vs-oracle [1] (the two events it ordered are frames apart
now), battle-jsl-audio-keeps-playing [1] (the per-piece take exists only on the reference SPC core, not the native sound
machine the check runs).

**A new mutation SURVIVED -- the check stays without one**: field-battle-ambush-generic-dissolve,
field-battle-atb-no-double, field-battle-crit-beatB-dead-suppressed, field-battle-crit-flash2,
field-battle-cyclone-fx-not-replayed, field-battle-gato-facing-right, seq-pic-ending-vs-oracle,
audio-song-loader-defrag, vmdiff-forest-battle-entry, rom-census-attract.

**No code of the port to mutate** (14): the checks grade the oracle against a formula written in the harness, or measure
the oracle itself -- field-camera-truce-inn, npc-move-integration, tile-prio-recompute, oam-stage1-assembly,
velocity-trig-circle, wram-loadcensus-clearlist, wram-loadcensus-natural-door, oracle-button-grammar -- or a generated
file, a manifest or a tool against itself -- tables-datafree-headers-current, gen-shapes-match-generated,
inputs-come-from-recipes-or-sources, oracle-peekshots-equal-a-peek-per-frame, field-worldtest-audio (a NULL guard:
every break is the crash), lockstep-forest-projections-clean (the drive writes none of the four projections). The first
eight test a derivation, not the port: if the port's code differs from the harness's formula, they do not notice.
seq-gwarp-81/82/83-xlat-lockstep and seq-gwarp-xlat-lockstep keep no mutation, as their notes intend.

**Checks that grade less than they say** (found while deriving):

- field-battle-atb-no-double: its expect line (`gatoHP=59 pcAtk=1`) is printed by the first attack's span end too, so
  Crono attacking twice would still pass; it needs an expect line that the double attack changes.
- field-battle-victory-settle: its anchor (strike #6 + 161 = f1348) falls mid-fight now, not on the victory settle-in
  its notes describe.
- field-battle-song-spotlight: the battleshot frame draws the sprites on black and never composes the arena, so the BG3
  cone the check is named for is not in its hash.
