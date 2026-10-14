# TESTING — the one-command regression suite

```
python tools/regress.py               # full suite, 4 parallel jobs
python tools/regress.py --quick       # tier "quick" only
python tools/regress.py --list        # list every check + tier + command
python tools/regress.py --only vmdiff-bedslide render-scene1-snapshot
python tools/regress.py --jobs 8      # more parallelism (checks are read-only)
python tools/regress.py --build       # tools/build.py first (honest artifact check)
python tools/regress.py --pin NAME [NAME ...]   # re-derive their expected lines, in parallel (see "Pinned baselines")
python tools/regress.py --failed                # run only what failed on the last run
python tools/regress.py --repin-failed          # re-pin the last run's failures FROM THEIR SAVED OUTPUT (no re-run)
python tools/regress.py --no-oracle-cache       # run every oracle for real (see "Speed")
python tools/regress.py --clear-oracle-cache    # empty build/_oracle_cache
python tools/regress.py --check-inputs          # presence + sha256 vs tools/regress_inputs.json
python tools/regress.py --refresh-input-hashes  # rewrite sha256/size pins from disk
python tools/regress.py --mutate NAME           # prove NAME actually GRADES something
python tools/regress.py --mutate-all            # every check that declares mutations

# One-command local gate (build + inputs + quick regress → build/preflight.json):
python tools/preflight.py
python tools/preflight.py --full                # full suite instead of --quick
tools\preflight.ps1                             # Windows wrapper
```

Run from anywhere — the script resolves the repo root from its own location and runs
every check from the repo root. Exit status is 1 iff any check FAILed (SKIPs don't fail).

## Speed (2026-10-08)

- **The oracle cache** (`tools/ctlib/oracle_cache.py`). A `ct.exe --peek / --monitor / --hdma / --fb` run (--fb without --script) is a pure function
  of the ct.exe binary (sha256), its arguments, its environment and the CONTENT of every file an argument or an env
  value names (+ the ROM); its outputs are enumerated (`--peek`'s OUT + .wram/.vram/.oam, `--fb`'s OUT + .wram, PEEK_SAVE, PEEK_THUMBS,
  PEEK_PCM, PEEK_ARAM, CT_DSP_RAWPCM, PEEK_SHOTS' frames). regress.py keys every such run of a check -- its own argv and
  its python tools' (`tools/oracle_cache_site/sitecustomize.py` on PYTHONPATH, CT_ORACLE_CACHE) -- in
  `build/_oracle_cache`, zlib-compressed, LRU-capped at `CT_ORACLE_CACHE_GB` (default 8). A change to the port alone
  (src/field.c, the menu, ...) leaves ct.exe's bytes alone, so the cartridge side of every `-vs-oracle` check is
  replayed; a change to anything ct.exe links (snes/*.c, main.c, battle.c, ct_vm.c, ...) misses, as it must.
- **One oracle run for many frames** (`PEEK_SHOTS`, `tools/ctlib/peekshots.py`): a tool that graded N frames by running
  `--peek` N times from the state (N^2/2 emulated frames) runs it once. Byte-identical to the per-frame peeks
  (`tools/peekshots_selftest.py`). menu_shell_cmp 716 s -> 18 s, menu_equip_list_cmp 352 s -> 5 s.
- **Longest first**: the parallel batch is submitted in the order of the last run's durations
  (`build/_regress_times.json`), so the slowest check never starts last.
- **Re-pin without re-running**: every run saves its failures with their full output in `build/_regress_last.json`;
  `--repin-failed` rewrites each failed check's `expect` from it (old -> new lines printed), skipping a check with no
  `pin` regexes, a timeout, or a check whose argv/env changed since. Review the printed diff -- a re-pin is a claim
  that the new output is the right one. `--failed` re-runs just those checks.

The suite is **data-driven**: every check lives in `tools/regress_checks.json`
(`{name, argv, env, inputs, expect, pin, tier, timeout_s, notes}`), plus a top-level
`excluded` list recording candidates that were triaged OUT and why. `regress.py`
contains no per-check logic. An optional `env_rule` object is laid over `env` ONLY when the
infinitely fast CPU's migration switches are both set (`CT_ORACLE_NMION_HOLD=1` and
`CT_ORACLE_IFAST=1`, CLAUDE.md): for a check whose premise changes under them (the gate
cutscene's lockstep needs its oracle under the rule then). Without both switches it is not read.

## The one rule that matters: signatures, not exit codes

Selfchecks now return a meaningful process exit (0 = success criteria, 1 = mismatch,
2+ = setup). The suite **still** never trusts exit codes for PASS/FAIL: a check
**PASSes iff every literal substring in its `expect` list appears in the combined
stdout+stderr**. That keeps known baselines like `--vm` `507 OK, 5 mismatch` (process
exit 1) green until the pin is intentionally changed. On FAIL the runner prints the
first missing expectation and the actual nearby output lines.

## Prerequisites (all in the repo root)

- `Chrono_Trigger_USA_SNES.sfc` — your own dump, verified by SHA-1 wherever it is read (`src/rom_verify.c`,
  `tools/ctlib/rom.py`); `python tools/setup.py --rom <file>` puts it in place and does everything below.
- `ct_assets.dat` and the generated files (`src/data/*.h`, `src/xlat/*.c`) — made by `tools/setup.py` from the ROM
  and the power-on recipes. `ct_field.exe` checks pass the .dat explicitly via `--assets` (there is no default).
- Oracle states and WRAM seeds: the recipe-made ones come from `python tools/fixtures.py make all` (the recipes in
  `tools/recipes/`, listed per domain in `tools/fixtures/<domain>.json`); the rest are private captures from the
  owner's playthrough, which SKIP with their name in a fresh clone. The catalogue, with sha256 pins and each file's
  provenance, is **`tools/regress_inputs.json`**; [TESTING_OVERVIEW.md](TESTING_OVERVIEW.md) counts the checks by
  class (ROM only, recipe fixtures, private captures).
- Built binaries `build/ct.exe`, `build/ct_field.exe` (via `tools\build.bat` /
  `python tools/build.py`).

**Verify the capture kit before a full run:**

```
python tools/regress.py --check-inputs
```

MISS lines print the `how` field from the manifest (ROM source, extract command,
or RE capture recipe). After intentionally replacing a pinned file, re-hash with
`--refresh-input-hashes` and commit the updated `regress_inputs.json`.

A check whose declared `inputs` are missing is reported as **SKIP with the missing
file named** — it stays in the suite so the needed capture stays discoverable.

**An oracle state a tool makes belongs to the oracle that made it.** A tool that saves
its own state (`PEEK_SAVE=… ct.exe --peek …`) makes it on every run when that is cheap
(owwin_step_cmp, wipe_step_cmp, reload_cost_cmp, naming_cmp's boot drives: under 5 s), or
keys it to the exe that made it when it is not (stage_rule_cmp: two power-on runs, kept
under `build/_stagerule/` by ct.exe's sha1) — never `if not os.path.exists(state)`. A kept
state replays the rule that made it: ow-window-object-routine-and-mask-vs-oracle sat green
three days on a 2026-09-18 state after a07ca0f / 30ccdce had moved the call's beam
(disasm/notes/field_window_wipe.md, "The virtual beam", 2026-09-30). A state shared by two
checks the suite runs in parallel lives in each check's own directory.

`--build` runs `python tools/build.py` (not the old banner-only path). `build.py`
fails if a `ct*.exe` from this `build/` is still running (file lock). Kill any
running `ct_field.exe`/`ct.exe` before `--build`.

## Tiers

Tier assignment is by **measured runtime**: roughly <20 s → quick, else full.
Timeouts are ~5× observed time (minimum 30 s). Counts from `tools/regress_checks.json`
(2026-10-12: **831** checks, **2** excluded; `--list` prints the current set).

- **quick** (**588** checks, about 4 minutes on 20 cores): the micro-execution sweeps of ROM routines, the
  `--vmdiff` / field / battle / menu lockstep checks against the oracle, scene renders, and the engine's pinned
  behaviours. Pre-commit tier.
- **full** (all of them): adds **241** slower checks — whole fights and attract legs against the logical oracle, the
  long menu and shop audits, the widescreen and other mod checks, the ROM-boot drive validators — and the two with no
  tier (`lockstep-talk-box-machine-clean`, `lockstep-scene120-stairs-entry-lucca`), which run only here. A full run
  takes 15-80 minutes, mostly depending on the oracle cache.

### shadow-attract (`ct.exe --shadow --frames 9000`, the routine-level shadow)

The routine-level shadow (docs/SHADOW_PROPOSAL.md, phases S0 and S1's field VM). The oracle plays the attract demo
from power-on under the logical rule; at each call to a registered routine the harness runs our C function on a copy
of the oracle's WRAM and compares, at the return, all 128 KB (less the pushed stack bytes, and C3/0002's workspace for
the VM handlers, as `--sweepops`) and the registers the caller reads. Registered: C1/AF22 (`battleRngBase`; 92 calls,
A's low byte and X), C0/ACFD (`CtBuildVelocityPriority`; 3150 calls), C1/DC64 (not reached by the attract), the 211
field-script opcode handlers of `g_ctVmOps` through `CtFieldRunnerOne` (the IP back in X and the carry), and the battle
AI's walk C1/AFD2, its 41 condition handlers and the commit C1/8C3E, run whole (`CtBattleAiWalkWhole` / `CondWhole`).
30100 calls reach 99 of the 257 entries; 0 mismatched. `skipped` counts handler bodies another handler runs
as a subroutine with the same X and Y (op 95's C0/5410 inside op B6), graded at the caller's entry; `unported` counts
the cases `CtFieldRunnerOne` traps (the sound posts E8/EA/EB/EC, some FF and 83). Needs only the ROM. Eight declared
mutations (`--mutate shadow-attract`) all turn it red: the RNG cursor, the draw in A, X's high byte, op 10's jump
base, op 2D's carry, the $01F0 staging of the move-toward-an-actor body, C0/ACFD's velocity store and the AI commit's
$B242 record. Re-pin only a change in the counts with
mismatched still 0, after confirming the oracle's run itself changed.

### shadow-route-magus-fight (`ct.exe --shadow --btnfile tools/shadow_routes/magus_fight.btn`, a route)

The shadow's first route (docs/SHADOW_PROPOSAL.md, S2). The oracle boots with a public battery save,
`saves/public/fantasyanime/ctsave07-azala.srm` (`CT_ORACLE_SRAM`; the 51 public saves under `saves/public/` are
gitignored, and `SOURCES.md` / `SHA256SUMS` there say where each came from), loads slot 1 (Magus' Castle) and plays the
route file for 16270 frames: down the dark stairway (the Vamp ambush, fought by attacking), up into Magus's hall, the
summoning, and the fight, attacked until the party falls. Every timing is this project's, written leg by leg with
`tools/shadow_route.py`. 88644 calls reach 87 entries, 0 mismatched; it grades what the attract cannot reach, C1/DC64's
hit roll (11 calls) and AI conditions 08, 0B, 11 and 18. One declared mutation (the hit roll's `$18` record) turns it
red. The route is frame-timed
under the logical rule: a change in the counts with mismatched 0 means the oracle's run changed; re-pin only after
confirming that, and the last line's location (`00AC`) says whether the route still ends in the fight's room.

### shadow-route-menu-items-equip (`ct.exe --shadow --btnfile tools/shadow_routes/menu_items_equip.btn`, a route)

The shadow's menu route: from the same save, the X-menu's Item screen (four Tonics on Crono) and Equip screen (the Flint
Edge in place of the Slasher and back, the first helm and armour candidates), 3360 frames, quick tier. It grades the
inventory routines C2/8791 (4 calls) and C2/87D5 (8) on all 128 KB, A and X. Its first run found a port defect: C2/87FA,
the inventory's find, records the slot it found in `$04C7` and zeroes `$04C8`, which `mInvFind` did not (now fixed with
the 256-slot scans and the add's 8-bit sum, menu.c). Two declared mutations turn it red: the `$04C7` record and the
remove's emptying of a slot's id.

### field-battletest-gato (`ct_field.exe --battletest`)

The battle-infrastructure phase-1 end-to-end check (op 0xD8 → the C0/1770 transfer
consumer → C0/024C/0617/0557 staging → the battle module boot → the ROM's own
C1/8357 debug instant-end → op-0x16 victory branch → Silver Points). It seeds the
whole VM from the pinned oracle capture `saves/battle_re/gato_t0.raw.wram` (raw
128 KB WRAM, f145 = the last frame before the GATO cutscene sequence) and verifies
**countable signatures** against the oracle's staging capture (`gato_t2.raw.wram`):

- staging 18/18: the GATO slot-3 record at `$29E3` == `08 06 08 0f 04 92 00 42 01
  00 80 7f` byte-for-byte (including the live `$0F01` pose byte 04 = GATO's idle-bob
  anim-1 row 1), `$019D` = `10 80` (eligible list = obj8\*2 + terminator), `$2989/
  $2A1F` = `92/20` (the D8 operands), lead respawn cell `$0102/03` = (8,11), and the
  slot-3 back-map `$01A6` = `10`;
- stats: enemy block `$612D` id 0x92 with maxHP 76 staged from ROM `CC/4700 + id*23`
  and the name "Gato" decoded from `CC/6500 + id*11`;
- points: post-battle `$7F0052 == 0x0F` (op-0x5B `+= 15` then op-0x58 persist).

`CT_BATTLE_DEBUGEND=60` (declared in the check) fires the debug end after 60 module
frames. The `battletest: battle fN` line is engine-op-timing sensitive and NOT an
expectation; the PASS line is the pin. Needs the ROM (enemy stat/name tables).

### field-battlehud-gato (`ct_field.exe --battletest` + `CT_BATTLESHOT`)

Pins the battle-mode HUD strip render (`CtBattleHudRender`, `src/field.c`; geometry/
palettes/tilemaps baked in `src/data/battle_hud.h`; full derivation in
`disasm/notes/battle_hud_render.md`). Same GATO seed as `field-battletest-gato`;
`CT_BATTLESHOT=build/bhud_check_p0.raw` composes the field frame + draws the strip
(phase 0 = ENEMY-NAME, the default — `CT_BATTLEHUD_PHASE` unset) once the battle
resolves, dumps it (`[u32 w][u32 h][RGB]`, the same format `CT_MENUSHOT` uses), and
prints `battlehud: fnv=XXXXXXXX phase=0` (fnv-1a over the dump) — that line plus the
existing `battletest: PASS` line are the pin. Marked `"exclusive": true` since it
writes a file (the suite's other checks are read-only and run in parallel).

The composed frame also draws the **combatant STANCE** now: the battle compose
SUPPRESSES the field OBJECT sprites (the module wipes the field object arrays at boot,
`C1:CE04`) and draws the baked stance OBJs (`battleStanceRender` + `src/data/battle_stance.h`;
derivation `disasm/notes/battle_stance.md`) — so the dump replays the oracle battle OAM
(Crono sword-ready / Marle ready / GATO battle pose) instead of the frozen field sprites.
This re-pinned the fnv (`70D3486A → 4FA1B68A`). Implementer-verified vs the regenerated
oracle f460 fb (`tools/_gen_battlestance.py`, btnscript `100:8,0:452`): the 2672
combatant-OBJ opaque pixels are **0/2672 byte-exact** (and 0/2672 vs the baked color); the
BG behind aligns to the oracle at a 2px camera-scroll offset (the two capture sources
differ) with no leftover field sprites. Re-pin only after re-confirming that byte-exact
stance diff.

Pixel-truth (verified by the implementer, not re-run by this check every time): the
same C dump, diffed against the oracle targets via
`tools/_gen_battlehud.py --diffc-enemy/--diffc-cmd` (an additive diff-only mode; the
oracle derivation logic is untouched) — the strip is **12288/12288 byte-exact on BOTH
phases** (mean|err| 0.00, max 0; glyph-INK 1262/1262 enemy-name + 2221/2221 command;
box-interior body 10162/10162 + 8904/8904). Chrome = the full-strip opaque BG1 layer
(`ctBattleHudBg1*`, baked from the BG1-only oracle render `PEEK_ISOBG=0`) with the
BG3 text drawn on top — see the dossier's Q2/D4. Phase 1 (COMMAND) is exercised the
same way with `CT_BATTLEHUD_PHASE=1` but is not pinned in this check (one argv+env
per check can't produce two phases' output from a single run); re-run manually to
re-verify phase 1, or add a second check if that becomes worth the extra
exclusive-run cost.

### Screen $0D: the ATB selector and the naming (`tools/naming_cmp.py`, `field-atbsel-*`, `field-naming-*`)

The new-game Battle Mode selector and every naming screen are ONE screen of the menu shell, $0D (op C8 $C0+ ->
C0/0CC7 -> `JSL C2:8000` mode 3; C2/E1E3's sub-states: 0 the router C2/E1ED, 2 the selector C2/E236, 3 the naming
build C2/E201, 4 the prompt C2/E209, 1 the keyboard C2/E21E). The port runs it through the same shell as the X menu
and the shop (`src/field.c`, `menuScreen == 0x0D`), so a headless run meets it exactly as the cartridge does and has
to press through it -- the selector no longer auto-resolves under `--selftest`. Under `--newgame` the op triggers at
f13, the selector's P is f45 (first call, A dropped, f46), and an A at f60 puts the prompt up from f74: that is why
every `field-naming-*` drive starts `wait:60; a:1;`.

* The PICTURE is graded against the cartridge frame by frame by `tools/naming_cmp.py`: `menu-naming-screen-vs-oracle`
  (the keyboard), `menu-naming-boot-selector-vs-oracle` (a blank cart from power-on through the selector, the prompt,
  the keyboard and the close), `menu-naming-boot-confirm-in-fade-vs-oracle` (A inside the fade-in),
  `menu-naming-marle-direct-vs-oracle` (a later naming: no selector, no prompt). These replaced the own-reference
  `field-atbsel-shot-*` / `field-atbsel-typewriter-step*` pins (CT_ATBSELSHOT is gone).
* `field-atbsel-flag-*` drive the real selector (Down or not, then A) and pin the `atbselcfg:` line C2/E24C's
  confirm prints; `field-atbsel-cursor-sfx*` its row blip and the $0D76 throttle; `field-naming-*` the keyboard's
  sounds and silences. A run that ends with the screen up prints its tallies from the menu block's frame cap.
* `CT_NAMEAS=<name>` is the harness BYPASS for runs that do not grade the screen (field-newgame-pan and the like): the
  op is stepped over at its park with the RAM the screen would leave ($2990 bit 6 from `CT_ATBSEL` when the router
  would run the selector, $299F, the name record) and no frame of the screen is drawn.

The selector's picture is still src/data/atbsel.h's bake (tools/_gen_atbsel.py, untracked); the typewriter's empty
box is held to the description window ($FF:C429 op 04, tile row 16) and its line-0 x = 53 column comes from the ACTIVE
reveal's first frame (see the header). Derivations: disasm/notes/battle_atbmode.md, disasm/notes/naming_screen_sfx.md.

### field-battleloop-gato (`ct_field.exe --battletest` + `CT_BATTLE_MASHA=1`)

Pins the minimal canonical ATB battle loop (`src/battle.c CtBattleTick`, derived in
`disasm/notes/battle_loop.md` PART 2 + the two closed ADDENDUM equations) resolving the
GATO fight by a REAL HP-kill instead of the phase-1 debug end. Same GATO seed;
`CT_BATTLE_MASHA=1` auto-presses A every frame (a PC attacks the instant its command
opens) AND suppresses the `--battletest` debug-end auto-fire, so the fight plays out.
The loop runs the ATB timers (`$AFAB+idx` −1/tick with the FD/B807 speed reload
`$B158`), opens a PC command (`$95D5`/`$95DB`/`$9609`), applies the closed damage
equation (`$AEEF` from ROM `CC/213F[0x3A]`/`CC/89C6[1]`, `base·(256−def)/256 + rand`),
HP/death, and the real victory writer (`$7F01EC=0`, C1/817A). It prints
`battleloop: victory f296 pcAttacks=6 cronoHP=44 marleHP=65` — victory at module frame
296, 6 PC attacks landed, final party HPs (Crono took two GATO counters, Marle
untouched, matching dossier S4) — plus the existing `battletest: PASS` line (the field
script's op-0x16 victory branch pays the 15 Silver Points `$7F0052=0x0F`). Per-hit
damage is byte-exact vs the dossier's observed table (Crono→GATO 16/17, Marle→GATO 8,
GATO→Crono 13). Deterministic (the RNG index `$26` is inherited from WRAM=0 and the
omitted miss-roll never advances it). The victory-frame/attack-count/HP line is the
pin; re-pin via `--pin` ONLY after an intentional loop-timing/damage change re-verified
against `battle_loop.md`'s per-hit numbers.

### field-battle-{confirm-boundary,nameseed,lowhp-blink-*,victory-*} (the derived-fixes round)

Six checks pinning the four behaviors added in the derived-fixes round (dossiers
`disasm/notes/battle_confirm_queue.md`, `battle_hud_names_blink.md`,
`battle_hp_timing.md`, `battle_gato_idle_victory.md`):

- **field-battle-confirm-boundary** — the target-select CONFIRM must COMMIT even on the
  enemy-dispatch / span-end BOUNDARY frames (the old deferred-latch drop, "sometimes A
  does nothing"). `CT_BATTLE_SCRIPT="n:20,a:1,n:391,a:1,n:600"` enters target-select then
  confirms at the OLD span-end drop frame (f572); the per-frame commit + `battleDispatch-
  Scheduled` land Crono's hit (`gatoHP=59 pcAtk=1` — GATO 76→59, from the `CT_BATTLE_TRACE`
  stderr). A regression drops the confirm → pcAtk stays 0 → the substring vanishes. Byte-
  deterministic; re-pin only if the confirm damage legitimately changes.
- **field-battle-nameseed** — the battle-HUD party NAME seed `$7E2C23` (the blank-names-in-
  fresh-play fix). `CT_NAMESEED=1 --scene 19` loads a FRESH scene (NO wram overwrite, unlike
  the wram-seeded battle checks), verifies the Crono/Marle/Lucca 6-byte CT-glyph records
  byte-exact vs the dossier §C1 table, prints `nameseed: PASS`, and exits. Re-pin only if
  the seed table changes.
- **field-battle-lowhp-blink-white / -grey** — the low-HP HP/MP-digit FLASH. `CT_BATTLE_
  CRONOHP=5` (critical, ≤ maxHP/8) + a HUD battleshot at f240 (WHITE phase) vs f252 (GREY
  phase, 12f later). The two `battleshot: fXXX fnv=` values MUST DIFFER = the 12-frame blink
  toggles (white 96744109 ≠ grey 1AD3A30C — re-pinned in the WAVE 1 computed-PC-anchor pass,
  see below); an always-grey/white regression collapses them. fnv32 render canaries (brittle)
  — re-pin the PAIR together via `--pin` after an intended HUD render change, re-verifying
  pal2↔pal3 on HP/MP only (name/bar static).
- **field-battle-victory-settle / -hold** — the victory SETTLE-IN ladder (the "stuck" fix,
  `src/data/battle_settle.h`) + GATO's dead-standing FREEZE. `CT_BATTLE_MASHA=1` battleshots at
  f1984 (victoryHold≈20, the animated C1/M1 settle keyframe) vs f2054 (victoryHold≈90, the
  held battle_end kf0/kf1). The two fnvs DIFFER = the celebration ANIMATES (settle→hold), not
  the old frozen snap; GATO renders frozen in both (the `deadStandFrame` latch). fnv32
  canaries; TIMELINE-sensitive — re-pin the PAIR if the fight retimes (settle poses were
  verified vs oracle `view_190`, Crono footprint IoU 0.95; re-pinned 1422DB73/64E28C28 in the
  WAVE 1 computed-PC-anchor pass, see below).

**Victory REWARDS sub-round** (dossiers `battle_results_window.md`, `battle_victory_rewards.md`,
`battle_tech_learn.md`) — all three drive the forest fight (`--scene 19 --battletest
saves/battle_re/forest_pre.raw.wram`, `CT_BATTLE_TRACE=1 CT_BATTLE_MASHA=1`) to a real HP-kill victory
and pin a `CT_BATTLE_TRACE` line (byte-exact vs an oracle `--monitor --writer` capture):

- **field-battle-results-reward** — the accumulated per-kill rewards from `CC/5E00` applied at victory:
  `xp=9 tp=3 gold+48 drops=1 itemBD`, both PCs `xp=9`, Crono `tpnext=2` / Marle `tpnext=7` (no learn:
  block+0x2D 5/10 > earned 3). Baseline for the two forced checks below.
- **field-battle-levelup-growth** (`+CT_BATTLE_FORCELVL=1`) — stages both PCs to L10/NEXT-XP 5 so the 9
  XP crosses → L11 with the byte-exact stat growth (`battle-levelup pc0(g=1 L=11 hp=85 …)`), vs the
  oracle forced-level-up capture.
- **field-battle-tech-learn** (`+CT_BATTLE_FORCETECH=1`) — stages both PCs' `block+0x2D` (TP-to-next-tech)
  to 2 so the 3 TP crosses → Crono learns **Cyclone** (tid 01), Marle learns **Aura** (tid 09):
  `battle-techlearn pc0(n=1 bits=80 lc=1 next=86 tid=01 list=01) pc1(n=1 bits=80 lc=1 next=46 tid=09
  list=09)`. `bits`=`$2837+cid` learned bit, `lc`=`$2830+cid` learnCount, `next`=`block+0x2D` after the
  reload+CANONICAL double-subtract (Crono Slash 90−2·3+2=86, Marle Provoke 50−2·3+2=46 — oracle
  `forest_learn2.state` both `block+0x2D`=2, `$5E5A`=0056/`$5EDA`=002E EXACT). `list=` replays the
  battle tech-list staging scan on the post-learn `$2837`, proving the learned tech stages next battle for
  FREE (composes with `field-battle-tech-cyclone-menu`). The second pin `battle-results seq=…,5:1,5:109`
  confirms the kind-5 "{name} learned {tech}!" results line is built. Re-pin only after re-validating the
  learn law vs the oracle (`battle_tech_learn.md` PART 3).

**battle-results-text-origin** (`CT_BATTLE_RENDERWRAM=saves/battle_re/bat9.raw.wram
`CT_BATTLE_RENDER_RESULTLINE=0:9`) — WHERE the reward line sits inside the results window, as opposed
to `battle-results-band-24-lines`, which grades the window CHROME and samples text-free columns only.
`renderwram: msgtext=x8..154 dy=6..15` is read back out of the rendered pixels and reported RELATIVE to
the band top, so the identical line is pinned at the opposite band position by
`battle-results-band-24-lines-bottom`. X and the advance are ROM-DERIVED: `$0234` (the pen X, set to 8
by `C2/57DF` and used as the pixel X by `C2/5DE7`/`C2/5E02`) and the `$C2:60E6` width table `C2/5E27`
advances by, whose SPACE entry (CT `$EF` → index 79) is 4, not the 5 the port hardcoded. The band-row-6
offset is an ORACLE MEASUREMENT at both positions (`forest_battle_3hetake` line 1: band 8, ink rows
14..23; `bat4_lake` line 1: band 184, ink rows 190..199) — the `$CA24` upload's VRAM row is not derived,
and the dossier says so. Three mutations killed. Re-pin only against a fresh oracle capture of the same
line, never to bless a shifted line. Dossier: `disasm/notes/battle_results_text_origin.md`.

**ATB-latch + GATO-moveset sub-round** (dossiers `battle_atb_reload.md`,
`battle_gato_moveset.md`, `battle_forest_return.md`), three checks:

- **field-battle-atb-no-double** — the ATB turn latch (`pcInTurn[3]`, the `$93EE` port).
  The double-attack repro `CT_BATTLE_SCRIPT="n:20,a:1,n:3,a:1,n:135,a:1,n:3,a:1,n:400"`
  presses A twice, then twice again; only the FIRST attack lands (`gatoHP=59 pcAtk=1`, GATO
  76→59). The old LEVEL-check re-enqueue let the second attack fire on the commit→dispatch
  tick (`gatoHP=59→27 pcAtk=2`). Re-pin only if the per-hit damage changes.
- **field-battle-gato-song-attackA** — the HOLD fight (`n:900`, GATO un-hit → attack-A +
  song). attack-A now reads enemy record #0 (pow 12): Crono 70→62 (−8, was −13 on the
  hardcoded rec#1). The SONG (type-0/power-0 tech) drains 1 via the universal min-1 floor
  (C1/DCF0-DCF6): Marle 65→64 with a "1" popup, superseding the `isTech?0` "0"/no-drain bug.
  Two `CT_BATTLE_TRACE` act-end lines are the pin. (attack-A applied 8 vs oracle 9 is a
  pre-existing enemy-physical def-scaling residual — GATO A36=0, Crono def 16 — not this fix.)
- **field-battle-gato-flurry** — the attack-B FLURRY render (CD/7B8F "several punches"). MASH →
  GATO block2 attack-B ($9877=0x34) at f811; the battleshot at f813 (rel 2, cycle-0 flash-beat
  = GS2 fist-out + white palette-flash) pins `fnv=987EFFDF`, distinct from the retract frame
  and from attack-A's single swipe (7 cycles over the 49f rear-up window). fnv32 canary; re-pin
  via `--pin` after re-verifying the flurry render intent.

- **field-battle-animeng-idle-gather** (`ct_field.exe` + `CT_ANIMENG_SELFCHECK=1`) — the ANIM-ENGINE
  STAGE 1 acceptance (`battle_anim_engine.md` §B.1 STEP-4). The strongest engine regression + the ONLY
  non-canary battle pin of the stage: the actual `ctAnimengGather` C code, fed the baked E3 cell-words
  (`src/data/animeng_e3_gato.h`) + the `$7F8000` sheet fixture (`src/data/animeng_sheet_gato.h`), must reproduce
  the RETIRED idle bake `ctBattleAnimTiles[6]`/`[7]` (kf6 rest / kf7 swap) byte-for-byte over the GATO
  tile window 0x68-0x9F (`56/56` each) — and those tiles ARE the oracle idle VRAM (validated 48/48 vs
  the LakeSnes oracle). Also asserts the facing-1 idle WINDOW is the period-120 claw-swap (`idx04` rest
  cursor / `idx05` swap cursor). Byte-exact-vs-fixture pin: legitimately changes ONLY if the E3 record
  layout (`ADDENDUM 1`) or the sheet capture is re-derived — never a `--pin`-away brittle canary.

### WAVE: ANIM-ENGINE PORT STAGE 1 (GATO idle/walk/facings from the real E3 pose engine)

Dossiers `disasm/notes/battle_anim_engine.md` (§B.1 + ADDENDUM 1 = contradiction C1 resolved) +
`battle_animeng_{script,builder,tiles,corpus}.md`. `battleGatoRender`'s idle + facing branches now
run the REAL enemy pose engine (`ctAnimengPoseIdx` tick → `$967B` → `ctAnimengGather` sheet gather →
`ctAnimengGatoBlit` compose), driven by `src/animeng_{e3,e4,sheet}_gato.h`, RETIRING `ctGatoIdleTrajV2`
(kf6/kf7) + `battle_gato_faces.h` (kept as fixtures). GATO's idle emerges as the E3 assemblies
`idx04/idx03/idx04/idx05` (period 120, 87f rest / 33f swap — the oracle STEADY idle, confirmed on
`gato_noatk` + `gato_inbattle`); the walk facings emerge as their own leg-cycle assemblies (LEFT
`idx08/07/06/07`, RIGHT `idx0B/0A/09/0A` — corpus §3.1 walk poses matched 12/12). SIX GATO-idle fnv
canaries RE-PINNED (`-lowhp-blink-white/-grey`, `-crit-flash1/2`, `-victory-settle/-hold`) + the TWO
walk-facing canaries (`-gato-facing-left/-right`): the retired bake collapsed the rest phase into a
single `idx00` pose + a SYNTHETIC uniform ±1px dy-bob, while the engine renders the real per-cell
assemblies + their real gathered tile stream (each validated 48/48 VRAM + 12/12 OAM vs the oracle).
The swap-cursor idle frames stayed BYTE-IDENTICAL (`-pc-approach-early/-late`, phase 90/99 = swap
cursor, unshifted), confirming the engine is byte-exact where the bake was faithful — the shifts are
all the sanctioned bake→engine improvement, NOT a wiring regression.

### WAVE 1 (dissolve gate/palette, battle camera, computed PC anchors, op-0xD9 handoff)

Dossiers `disasm/notes/battle_death_dissolve_gate.md` + `battle_entry_camera.md`. Five
GATO-fight fnv canaries above (`field-battlehud-gato`, `-lowhp-blink-white/-grey`,
`-victory-settle/-hold`) were RE-PINNED in this round: `src/field.c` replaced the baked
absolute Crono/Marle stance anchors with a COMPUTED anchor (`battlePcAnchorDelta`, the same
`16·rel+fine+extent` law already used for the enemies), and `src/ct_vm.c`'s op-0xD9
(MOVEPARTY) handler now keeps each PC's WRAM position mirror (`$1800/$1880`) genuinely live
during a scripted walk (previously only the interactively-controlled lead got this, so a
follower's mirror went stale mid-walk) plus a per-frame arrival check (previously gated on
the issuing object's own script cadence, letting a follower overshoot its target tile). The
GATO fight's staged PC tiles are now byte-exact vs the oracle (was: Marle co-located with
Crono, ~39px off — see `field-battle-gato-staging-handoff` below); a residual few-px
SUB-tile offset (the D9 mover's exact landing precision, not yet byte-exact vs the ROM) is
what shifted the 5 fnvs — visually re-verified sane, not a wiring regression. New checks:

- **field-battle-forest-lastkill-dissolve** — the FIGHT-ENDING enemy must dissolve (dossier
  `battle_death_dissolve_gate.md` PART 1: the stand-vs-dissolve gate now keys on the enemy's
  ROM anim-set byte, `$E4/F600+id*10+4`, not the old "another enemy still alive" position
  gate that wrongly made the last Hetake stand). A MASHA forest fight battleshot at f1425
  (kill+18f, inside the dissolve dither window) must show the red/orange+white dither
  pattern, not a static idle mushroom.
- **field-battle-gato-staging-handoff** — `CT_BATTLE_STAGEDBG=1` on the GATO fight pins
  Marle's staged tile at `rel(0B,06)` = `(11,6)`, matching the oracle exactly (was `(08,08)`,
  co-located with Crono). Guards the op-0xD9 WRAM-mirror-sync fix.
- **field-battle-forest-camera-composite** — the arena-origin battle render camera (dossier
  `battle_entry_camera.md` D.1: `camx=$1D0A·8, camy=$1D0E·8` during `battlePhase`, replacing
  the player-follow camera that previously kept running through battle on a stale position).
  Exercised via a GENUINELY INTERACTIVE forest entry (`--loadwram` + `SDL_VIDEODRIVER=dummy`,
  NOT `--battletest`, which resolves synchronously and never sets `battlePhase`) using the new
  `CT_BATTLESHOT_FIELD[_AT]` hook (self-exits once armed, so the check terminates
  deterministically). Confirms the 3 Hetakes + Crono + Marle composite ON the forest terrain.

### field-menutest-gato (`ct_field.exe --menutest`)

The X-menu phase-1 end-to-end check (the C0/18D9 X edge → the C0/192F gates
`$011F!=0 && $0162==0 && $0110==0` → 15-frame entry fade + the C0/011B save block →
the mode-1 main screen (`src/menu.c`) → B → the C0/1A03-style restore → 15-frame
resume fade). Seeds the VM from the same `saves/battle_re/gato_t0.raw.wram` capture
(scripted X at f0 — the ROM polls buttons at C0/00A1 BEFORE the VM pass at C0/00BF,
and the seeded gato script E3-locks explore on its first VM visit). Verifies:

- panel DATA against the oracle menu observation (`m1_gato_menu`): Crono LV1
  HP 70/70 MP 8/8, Marle LV1 HP 65/65 MP 12/12, from party `$7E2980-82` + the char
  sheets `$7E2600+id*0x50` (`+3/+5/+7/+9` HP/MP words, `+0x12` LV);
- TIME 08:28 (`$7E0400-06` play clock, HH:MM per the C2/F28E display rule) and
  400G (`$7E2C53` 24-bit);
- `fnv32` of the composed menu frame — a render canary over OUR reused chrome/font
  (the ROM menu's own BG tiles are an open question; menu pixels are not byte-exact
  by design). Re-pin only after an intended menu-render change;
- **resume byte-identical**: a press-instant vs resume-instant snapshot (player
  pos/facing/plane, camera, every actor position+facing, every VM IP, party)
  compared memcmp-equal — the menu must not perturb the live field.

No ROM needed (everything reads the seeded WRAM + `ct_assets.dat`).
`--menutest <wram> --selftest out.raw` additionally dumps the composed menu frame
for visual layout checks.

The scripted drive also asserts the two ROM ENTRY GATES (`menugate:` /
`menusavegate:` refusal signatures): screen 5 (Form) refuses when
`$0F0B + $0F0C < 2` (C2/C627 + the roster builder C2/CD2B with the `$7F01DF`
party-lock bitfield), and screen 6 (Save) refuses when bit6 of
`($0D34|$0294|$0299|$029E)` is set, `$0D34` computed at menu-open from the
location word (C2/CEDC + C2/9875: overworld or `$7F01CE` bit15 = save allowed).
On the gato field state both REFUSE, matching the oracle's `--monitor` traces
(menu_open_items.md §3.1/§4.1).

### menusave-ow-open (`ct_field.exe --menutest` + `CT_MENUOPT=5 CT_MENUSCREEN=6`)

Pins the ENTERED Save screen render as an fnv32 canary (only the f34 fnv line —
`CT_MENUSCREEN` deliberately breaks the scripted sub-screen drives, the
`menu-ribbon-select-colors` pattern). Seeded by the same gato WRAM; the OPENED
screen itself was byte-compared at pin time against the oracle's real entered
Save screen (`saves/ow_w0.state` + X, Right×5, A — regen recipe
`tools/_gen_menuchrome_save.py`): chrome, save CGRAM palettes, the per-window
16-step 5-bit COLDATA ramp (`ctMenuSaveGrad5`), the windowed-half-color-math dim
of the non-cursor bands, and the hand ink box are byte-exact; the slot
summary-text glyphs are the documented 8x8-font boundary (the ROM rasterises
through C2/EF65's VWF). Re-pin only after an intended save-render change,
re-verified against the oracle captures.

### camera-reenter-scene5 (`ct_field.exe --scene 1 --reentertest 5`)

`--reentertest N` drives a REAL door transition (pendKind=1 — the same at-black
block + `camX = px-128` scroll re-seed as an overworld entry, pendKind=3) from
single-screen scene 1 into a chosen destination scene N, using N's own spawn as
the arrival. It pins that the scroll camera RE-CENTRES on scene entry: entering
multi-screen scene 5 (Leene Square, W=768) at spawn (344,512) must produce
`cam(216,400)` so the player lands on-screen at (128,112). The pre-fix bug left
`camX` holding scene 1's stale value → `cam(440,400)`, player off-screen at x=-96
(only visible in a MULTI-screen scene; the `$1D1A` clamp masked it in single-screen
rooms, so `worldtest`'s Truce-market entry never caught it). Not an fnv pin (the
fair has ambient NPCs) — it pins the camera coordinates; re-pin only after an
intentional, oracle-validated camera-model change.

### field-battle-forest-return (`ct_field.exe --scene 19 --loadwram … + CT_RETURN_PROBE_AT`)

The INTERACTIVE post-victory RETURN (battle_forest_return.md ADDENDUM AD.5) — the
check the headless `--battletest` path structurally cannot host (it never runs the
per-frame field resume). Boots the genuinely-interactive forest fight
(`forest_pre_wrapped.lakewram`, battlePhase path), mashes to victory
(`CT_BATTLE_MASHA`), and `CT_RETURN_PROBE_AT=120` snapshots the field 120 frames
after the resolve, printing `returnprobe: fN explore=E placed=P placed_enemies=X
fieldfnv=F` then exiting. Pins: control restored by the regroup script's own `E3 01`
(explore=1), the 3 dead Hetakes epilogue-dead-marked and gone (placed=2,
placed_enemies=0), and — by the probe firing at all — no post-battle hang (the
pre-fix engine deadlocked forever). Seeded by the derive lane's forest capture;
re-pin the fnv only for an intentional field-render change.

### field-battle-crit-flash1 / -flash2 (crit double-flash, battle_timing_audit.md §2)

Crono's 0x01 crit is a DOUBLE slash: two whole-screen washes at rel+50 (primary
strike, ~7f fade) and rel+91 (second slash, ~15f fade); rel+87 — the old,
refuted single-flash pin — is dark between them. Forced crit (`CT_BATTLE_CRITRATE=255`)
+ a fixed confirm script dispatches at f185; the two checks pin the shot fnv at f235
(rel+50) and f276 (rel+91). Re-pin only with a brightness sweep re-showing the two
onsets.

### field-battle-miss-forced (physical MISS presentation, battle_miss_evade.md)

The physical to-hit MISS (`C1/DC64`: miss iff `hit×100/evade < rand(0..99)`, hit =
attacker block+0x3A, evade = target block+0x3B). `CT_BATTLE_ENEMYEVADE=255` (a test
hook mirroring `CT_BATTLE_CRITRATE`) forces every alive enemy's staged evade high so
Crono's first physical MISSES GATO deterministically. The `battlemiss:` line pins all
four presentation laws at once: `tgtHP=76(dmg0,flat)` (no HP write — `C1/EC39` zeros
the display-only stage), `missPopup=1` (the "MISS" glyph, OBJ tiles 0x19A/0x19B pal0
via `C1/E8E2`, instead of a damage number), `hurtPose=0`+`flash=0` (the victim gets no
hurt-flash/knockback — it stays idle), `span=125` (the attacker's full swing still
plays; the turn is consumed). The when-hit counter is not armed (dmg=0 never reaches
`battleApplyDamage`). CRUCIAL: the hit roll draws `$26` only when a miss is possible
(`hit<evade`); every normal fight has `hit≥evade` → no draw → `$26` unperturbed → all
other battle pins stay byte-identical (this check adds a miss WITHOUT moving a pin).
f305 = the first-miss span-end (shares the mash cadence/intro with field-battleloop-gato
— re-pin together if that timing changes). The glyph pixels are separately pixel-verified
byte-exact by `tools/_gen_battlefx.py --verify` (119/119 opaque px vs a forced-miss fb).
Seed `gato_t0.raw.wram`. Re-pin only after re-verifying the miss law + presentation.

### field-battle-pc-approach-early / -late (Crono runs to the victim)

The PC physical-attack APPROACH mover (battle_pc_approach_facing.md PART 1): Crono's
swing travels to the victim at 2px/f on the baked CC/F494 track primitives
(`src/data/battle_tracks.h`), stops adjacent (victim foot+8), strikes, springs home 4px/f.
Forest MASHA fight, first attack dispatch f168 (32px LEFT): shots at f172 (rel 4,
offset 26px out) and f181 (rel 13, 8px out) pin two DISTINCT mid-travel frames — the
travel itself (the old baked hop drew one fixed position). The C1/5480 grid
pathfinder stays the dossier P1 OPEN (dominant-cardinal form implemented).

### field-battle-gato-facing-left / -right (GATO turns while walking)

GATO's four directional pose sets (battle_pc_approach_facing.md PART 2), selected per
mover segment by `mvFacing = ctBattleFaceTab[bearing]` (the C1/37E9 $96DE stamp) and
drawn from `src/data/battle_gato_faces.h` (the oracle facing captures verbatim) at his
live foot. HOLD fight (no input): he attacks Marle to his right (f2830, facing 3)
and returns leftward (f3080, facing 2). MASH fights never walk (when-hit counter),
so every older pin is untouched by construction. DOWN (facing 1) remains the
committed idle bake; the within-facing walk-cycle tile stream is the P4 boundary.

### field-battle-song-spotlight (the GATO song darken + BG3 beam)

The song tech ($9877=0x10) screen effect (battle_song_spotlight.md): BG palettes
half-faded (k=0.476) while sprites/HUD read-out stay full, plus the real BG3
light-cone graphic (`src/data/battle_songbeam.h`, decoded from the effect-ON oracle VRAM
at tileAdr $5000/map $7C00, bigTiles 2bpp) colour-math-ADDED over the darkened arena,
anchored to the singer's live foot. Window rel 42–236 of the 311f span (24f in /
145f hold / 22f out). The check pins the mid-hold shot (f1100, rel 113). The
bank-C3/FD tech-effect dispatch entry is the dossier E.1 boundary; our command-box
chrome stays bright (E.4 residual, documented at `battleSongFxRender`).

### field-scene120-lucca-e0-fadechange (op-0xE0 fade-changeloc, the "She's Princess Nadia!" loop)

Pins the canonical op-0xE0 model (field_op_e0_fadechange.md): stage $0112-$0116 +
latch $0117.bit7 + fade $0119=0x0F + PARK-ON-OP (ROM C0/3AB7 ends TYX/CLC; the
C0/5AB1 runner stores the ip on CLC), with the commit raised by the per-frame drain
(C0/0C8A -> locChange at fire+16). Scene 120 (Guardia Castle 600 corridor), flags
storyline=0x10 + glb[169]=0x02 (`saves/s120_lucca_flags.bin`), player placed at tile
row Y=11 (`--at 600 184`) — obj0's op-0x22 PC-tile mirror (loc[9]/[10]) arms obj17's
branch-B gate, the Lucca "Crono!".."She's Princess Nadia!" cutscene plays under an
A-mash walkscript, and its `E0 6C 02 00 00` must FADE-CHANGE to scene 108 (expect
`changeloc: scene 120 -> 108` + `selftest: scene 108`). Pre-fix the engine skipped/
deferred the E0 and the autorun JUMPBACKed into the dialogue forever (the reported
loop). Deterministic (f957 both pin runs). Re-pin only if textbox pacing or the
walkscript grammar intentionally changes.

### field-scene8-telepod-touch-trigger (`--scene 8` + `CT_TRIGDBG=1`)

Pins that a CLASS-7 object (`$1100 = 07`: positioned, sprite-less, no loader op) gets
its `$0F00` activation latch **with the camera parked at the map origin**. That is the
one case C0/A947 covers and C0/A88D does not: A947's four window compares are
CMP+BMI/BPL = SIGNED on UNCLAMPED edges, A88D's are BCC/BCS on saturated ones, and the
port used A88D's form — so `$1D0A - 4` wrapped to 0xFC, read unsigned as 252, and every
class-7 object fell outside the window. Both ROM object scans gate on `$0F00` (activate
C0/1CFC, touch C0/9923), so scene 8's Telepod DEMO trigger (obj12, tile 4,4) was dead:
the reported "Crono steps on the pod and nothing happens". The `camcells` line is pinned
WITH the verdict on purpose — it is the precondition, and a check pinning only
`touchable=1` would still pass if the camera moved off the origin. Oracle:
`out_s8.state` obj12 `$1100 = 07`, `$0F18 = 80` every frame, writer `C0:A983` = the
instruction after A980 `STA $0F00,X` inside A947. Dossier:
`disasm/notes/field_trigger_latch_a947.md`.

### field-scene129-class7-action-probe (`--scene 129 --at 46 148 --play "…a:1…"` + `CT_ACTDBG=1`)

THREE fixes in one signature, all from the Manoria Cathedral report. `hit=13`: the ROM's
own action probe (C0/1CFC) now reaches obj13, the class-7 trigger at tile(2,8) — same
A947 latch as above (the sibling obj15 is the PENDANT pickup whose fn1 ends in the
naga-ette `D8 92 80` battle). `obj13 at(46,128)`: op 0x8D (C0/4C27) stores the script's
two 16-sub-pixel words VERBATIM — the −1/16 foot anchor belongs to op 0x8B's synthesised
`0xFF` low byte, not to every setter — so a tile-aligned word (`8D E0 02 00 08`,
y = 0x0800) no longer lands a row high (oracle `$1880/$1881 = 0x0800` exactly).
`fgpx=543134`: the BG1/BG2 priority-1 mask is built on the SUB-screen compose pass too;
scene 129's `screen_flags = 0xB4` puts BG1/BG2 on the sub screen, so the cathedral used
to report `fgpx = 0` and nothing could occlude a sprite (the nuns drew over the pews).
Deterministic (`[act f9]` both pin runs).

### field-scene157-fixed-sprite-prio (`--scene 157 --selftest`)

`fgskip` — the count of foreground pixels a sprite covered — went 6 → 102 when the
front/back-of-foreground decision started honouring the op-0x8E byte. `$0F80` bit 7 is
the AUTO switch: SET = the foot tile's prop1/prop2 bit 6, CLEAR = the byte's own nibbles
(`$0C01 = pv & 0x30`, `$0C00 = (pv << 4) & 0x30`, C0/4CB7-4CC8). The port read the foot
tile unconditionally with a 0x33-only override, which agrees wherever the tile happens
to say the same thing — scene 18's ticket vendor, the same `$8E = 0x23`, sits on a tile
that already had the head-front bit, which is why the wrong rule survived. Scene 157's
barman (obj8, also 0x23) sits on a tile with neither, so his head drew behind the bar's
foreground row; oracle `o157` has `$0C00 = 0x30 / $0C01 = 0x20`. Brittle-by-design pixel
canary as well as a counted one — re-pin only after an oracle-validated render change.

### field-scene240-no-pc-loaded (`--scene 240 --selftest`)

A location whose script loads NO PC draws no player. Locations 92 and 240 are pure
LOADMON cutscene maps and the ROM leaves the lead-object offset `$0197` at its 0x80
"none" sentinel (`--teleport 240 …` reports `player=UNPLACED, leadPC obj=64 [sentinel]`
and the oracle's frame is entirely black). This host renders the lead PC itself, at its
own px/py, so it needs that answer explicitly: `CtScenePlayerLoaded` (the init scan's
`found`) gates the draw. `fnv32=E5C51DC5` IS the all-black frame.

### ow-world1-colour-math (`--scene 112 --at 304 470 --walkscript "d:40,.:260"`)

Walks our own engine out of loc 112 into the 600 A.D. overworld and pins
`cgadsub=04` + the whole-frame fnv at the same camera, mover and frame number. `$0046`
is the CGADSUB shadow the overworld NMI pushes to `$2131` every frame (C2/0296) and each
world's LOAD SCRIPT sets it with ambient-VM op 0x13 at `$7F:0405` — world 0 `44` (HALF
add on BG3), world 1 `04` (FULL add), world 2 `3F`. The port hardcoded world 0's `0x44`,
so 600 A.D.'s BG3 mist and ocean composited half-bright ("the 600ad smoke looks
strange, dark"). The shadows now travel with the world snapshot (blob **fmt 5**,
`$0045-$0049`), because the load script has already retired by the time those snapshots
are captured. ⚠ The `cgadsub=` and `fnv32=` fields were APPENDED to the `selftest-ow:`
line so the older `selftest-ow: world 0` substring pins keep matching. Dossier:
`disasm/notes/ow_colour_math_per_world.md`.

### ow-era-placard-hides-while-walking (`--scene 1 --loadwram ow_w0.lakewram --walkscript "l:200"`)

Pins the era placard's SHOW/HIDE gate, i.e. the overworld's per-frame **draw list**.
`ow-era-placard-world0` only ever idles, so it cannot tell a gated placard from an
always-on one -- both are drawn on its last frame, and the port shipped the always-on one
("on the overworld, the sign placard should not show all the time"). This drive walks
into the coast and then stands still, and the signature is the whole mechanism:
`start=show`, `f45=hide` on the step whose `C2/36E7 STZ $1BF7` resets the counter,
`f228=show` when the script's own `4C F7 1B 78 0A` finally sees `$78`, and
**`showAt$1BF7=$78`** -- the op-0x4C operand itself, so the check grades the ROM's number
and not this port's frame numbering. ORACLE (`saves/ow_w0.state`, `--btnscript 40:300`):
last `STZ` at f65, `$1BF7` reaches `$78` at f185, and the OAM shadow `$7E:0700` gains the
placard (`FC C0 AC 31`) on that same frame -- 185-65 = 120 = `$78`. The frame COUNT between
step and show is deliberately NOT pinned: it is 119 or 120 depending where in its own
2-frame op-38 cycle the object sits when the step lands, so it is not a ROM constant. The
`f45`/`f228` numbers ARE this port's timeline and are a tripwire only -- re-pin them when a
derived change to the world boot or the mover legitimately moves the whole drive, NEVER to
make a lost hide look intentional. Three mutations (drop the gate, drop the `STZ $1BF7`,
move the enqueue onto the expiring dwell) are all killed. Dossier:
`disasm/notes/ow_placard_draw_list.md`.

### field-scene5-bottom-edge-draw-window (`--scene 5 --at 376 488`)

Pins that the draw list is the ROM's camera window and NOTHING narrower. `placed` is
already `$0F00,X`, and C0/B127 — the ROM's own draw-list gate — runs literally the same
four compares as C0/A88D against the object's TILE doubled: in pixels
`camX-32..camX+CAMW+31`, `camY-16..camY+CAMH+47`. A hand-rolled box ANDed on top allowed
only `+16px` below the camera, 31px tighter than the ROM on the one edge that shows, so an
NPC entering from below was culled until its FOOT came within 16px of the screen — by which
point ~16px of it was already due, and it popped in instead of sliding in. The fixture is
chosen so TWO objects sit in exactly the rejected band: at cam(240,352) the oracle's `$0F00`
active set is {12,14,15,16,17,21,22,23,24} and ours was missing obj24 (foot y 596, head 12px
on screen) and obj15 (foot y 623). `9 NPCs` (it read 7) is the countable; the fnv is the
canary. A camera anywhere else in this scene grades nothing — re-pin only with the same
`--at`.

### field-scene4-door-opens-on-step (`--scene 4 --at 168 111 --walkscript u:30`)

Pins both halves of the door machine. C0/9D35, inside the player-collision commit,
builds the `(row << 8) | col` index of the tile AHEAD in the direction of travel from
`$1D71`/`$1D73`, tests `$7E7000,X & 0x10` (prop1 bit 4 = the DOOR flag), and on a hit
stores it in `$015B` and raises `$0117` bit 4. collision.c carried that leg as a comment
("side-state only"), and `fieldTileFetch` never handed prop1 bit 4 back even though
`pack_collision` has always carried it (zflags bit 3 → packed z-byte bit 5). C0/0D3E is
the consumer: clear the bit, read the L1 metatile at `$015B` and dispatch on its VALUE —
0xE6/0xEC/0xEE/0xFA/0xFC — into five handlers that are each just `INC A / STA $7E3000,X`
over a fixed 2/4/6-tile block. Open = closed + 1; it is a one-shot and it never closes.
MEASURED on loc 004: `$7E320A/320B F2→F3 F4→F5`, `$7E330A/330B F6→F7 F8→F9`,
`$7E340A/340B FA→FB FC→FD` at f17, writer C0:1212, with NOTHING in the collision planes
moving. The `[door] tile(10,4) opened=1` line (CT_TRIGDBG) is the countable; the fnv is
the canary. ⚠ The host also has to seed `$1D1E`/`$1D1F` — unset, they mask every door
index to (0,0). Dossier: `disasm/notes/field_doors_occluders_decisionbox.md`.

### field-stairs-fall-camera-follows-vs-rule-cartridge (`--scene 131 --loadwram fall_stairs083.lakewram --walkscript d:200`)

The user's snes9x capture at the top of loc 083's stairs, which a script has turned into a conveyor (`$7E:7000` = 0D).
Down held: the cartridge's lead falls while `$011F` is 0, and the camera follows through the momentum C0/88E5 turns
into the scroll request (the collision C0/8A6D still runs on `$0120`). Pins the port's camera steps during the fall
(38 f5, 39 f11, 3C f23, 40 f39; the cartridge's 1-2 frames later). Two mechanisms: the host runs the controller with
explore off (no d-pad; the lead integrated only while its script sits at op B0), and the collision fetch reads the
prop planes from the scene's WRAM (the host's packed copy had lost the conveyor bits). Needs the private capture.

### battle-hypno-wave-rings-rotate-vs-rule-cartridge

Lucca's Hypno Wave on tools/battle_ab.py's synthetic fight (`tech 2 1`, the same WRAM on both sides). Its BG3 rings widen
by a colour rotation: the program's op 8E arms record 11 (mode 3, CGRAM $19-$1B up one every four frames, FD/E4E8). FD/E39C
runs the colour modes 1, 2, 3 and 8 in the fight as on the field; the port's battle service had left them off, and the rings
stood at the load's colours, drawn smaller than the cartridge's (worst 4714 px a frame). Pins the A/B summary (worst 1611 px:
the rings equal; ~80 px a frame and the name box's close are left). Needs gato_wait.state.

### field-defeat-holds-the-room-and-the-fight-sprites-vs-rule-cartridge

A lost fight (`$7F01EC = 1`, `$2A1F` bit 5 clear): C0/0283 goes to the defeat sequence C0/02A0, whose passes are
`JSR C0/00EB` until a button edge and then the fade -- no C0/00BF, so no script, mover, drain or draw pass, and the NMI
keeps uploading the fight's last OAM (the rule's cartridge in Yakra's room: OAM in the fight and in the wait differ in
entries 0-3 only, C0/B262's hides). The owner's F5 at the Chancellor (loc 0C6), every PC at 1 HP: pins no object
change between the ack (f2412) and f2700, the room's own colour animation going on from the ack's next frame (C0/00EB,
`$2278` a step every 7 frames -- no C0/0304 reload hold), and the f2700 frame's FNV (the party fallen, Yakra standing). Before, the VM
ticked through the wait -- the scene played on as if the fight were won -- and the field's objects were drawn standing.
Needs the owner's F5 snapshot (a CTFS image, tied to sizeof(CtScene)).

### field-battle-discards-an-open-box-vs-rule-cartridge

A fight requested while a dialogue box is open: op D8 (C0/3A83) and C0/1770 do not look at the box, and the staging
C0/024C drops it -- C0/0257-0263 `STZ $0129 / $0126 / $0127`, no close ramp; the box's script advances after the return.
The user's magus_scene capture (the cult's box opens by itself as the party walks down), the request poked at f87 with
`CT_POKEWRAM_AT`: pins the three bytes cleared at f92 (the rule's cartridge, the same poke: at the poke + 6). Before, the
port kept the box through the fight and drew it over the arena -- the user's report. Needs the private capture.

### battle-end-pc-stands-where-the-fight-left-it-vs-rule-cartridge

A PC an enemy pushed: the battle's epilogue C0/08A1-08C8 stores its field words (`$1800/$1880`) from the battle record,
so it stands where the fight left it the moment the field resumes. GATO's fight (battle_ab's seed, Crono solo): GATO's
blow leaves Crono three lines lower; pins `$1882 = 300B` at the port's ack (the cartridge's `0B30`). Before, the restored
entry page kept the pre-battle word (090B) and the lead was drawn there on the return's first frames -- the user's
"blink in the previous place". The damage number's origin is the same live position (C1/5B4D-5B6E), which re-pinned
field-battle-status-render. Needs gato_wait.state.

### battle-lost-fight-continues-vs-oracle (`tools/battle_lost_continue_cmp.py`)

A fight the party may lose (`$2A1F` bit 5, which GATO's `D8 92 20` stages) goes on after a defeat: C0/0289-029E sends it
to C0/0304, the won fight's return, the followers' regroup and the control's return included. battle_ab's enemy B6 wipes
the party in GATO's arena on both sides; every change of `$7F01EC`, `$7F03FE` and `$011F` from f800 to f1100 must be the
cartridge's (`830 01EC=01`, `967 03FE=02`, `1007 03FE=03 / 011F=01`, `1046 011F=00`). Before 2026-10-14 the port ran that
tail for a won fight only and the player never regained control. Quick tier (~7 s). Needs gato_wait.state.

### menu-exchange-vs-oracle (`tools/menu_exchange_cmp.py`)

The party exchange (menu mode 5, screen `$07`): the field's Y at storyline 0x49 opens it, the song task posts `14 3E` and
`81 01 FF`, the cursor walks the two columns, Robo is picked from the reserve and swapped for Marle, B closes it (the song put
back, the field reloaded with the new party). The recipe gato_wait on both sides (`7F0000=49, 2982=02, 2983=03, 0126=00`;
the port's SPC is the state's, in lockstep): the party, the roster `$0F02-$0F0C`, the picks, the cursor, the song bytes,
`$0D10` and `$0119`, each while the screen owns it -- 0 differing over 2267 frame-fields. Quick tier (~2 s). The picture is
not graded (its BG3 wave, the prompt text and the hooded figure are OPEN). disasm/notes/menu2_form_screen.md 2.14.

### battle-victory-teardown-frames-vs-oracle (`tools/battle_victory_teardown_cmp.py`)

The victory's C1/356D takes four frames: one C1/1BAA queue removal per seat in V, V+1 and V+2 (each closed by C1/007E's
wait), then `$95D5 = FF`, the C1/0C2D repaint and the pose ladder's arm in V+3; the first victory pose publishes in V+5.
GATO's fight made into a Dark Matter kill with tools/battle_ab.py's pokes, Magus solo and Magus + Crono + Marle, on the
rule's cartridge and the port: the queue, the chooser, the menu bytes, `$E5`, `$A1` (from V) and `$9907` from V-2 to V+40,
0 differing on both parties (the engine before 2026-10-13: 95 and 114). Full tier (~35 s). Needs gato_wait.state. Re-pin
only after looking at both sides' frames; the zeros must stay zeros. disasm/notes/battle_end_ui_teardown.md sec. 9.

### battle-xstrike-death-fade-waits-for-the-effect-vs-rule-cartridge

An enemy a tech kills: CD/0043, the death routine, waits until `$CCEA` is 0 (the tech's effect program has ended and
CD/0453 has cleaned up) before the #95 post and the red fade record 10 (`$0598 = 40 C0 0F 10`); the clean-up's own colour
record is the same record 10. The user's magus_scene capture (the Diablos fight), X-Strike learned by `CT_POKEWRAM`:
pins `$CCEA` cleared at f795 and the death's record f797-812 (the rule's cartridge: f796, f798-813). Before, the port
waited a fixed two frames, its fade started at once and the clean-up's record overwrote it: the corpse dissolved in its
own colours. Needs the private capture.

### field-savepoint-a-opens-the-save-screen-vs-rule-cartridge / field-shelter-only-on-the-save-point-vs-rule-cartridge

The save point. Its object (the same script in every location that has one) sets `$7F01CF` bit 7 while the lead stands
on its tile and, on A (the pad's `$00F8` bit 7, op `31`), runs `C8 40`: mode 6, screen `$0B` (C2/D4F0), the Save screen
without the ribbon, whose B closes the menu (C2/82B2). The first check holds A on the tile of
`saves/field_re/savept084_lake.state` (the user's game state on loc 084's save tile, made through the ROM's loader) and
pins the park, the menu's C frame, the save's jingle (slot 1 written) and the script's resume; the frames are the
rule's cartridge's (the park 3 frames apart, every step after it equal; the screen 0 px from the cartridge's). The second
tries the Shelter off the tile (refused: C2/9969 clears `$77C8` bit 1 when `$0D34 != 0`), then on it: the party's HP
and MP restored on the cartridge's values, a frame ahead (the port's input convention). Both need the private capture.

### sweep-band-25a3 (`ct.exe --sweepband25a3`) / battle-flame-toss-band-vs-rule-cartridge

The band C1/25A3 (C1/1FDD's routine 0: Slash, Lucca's Flame Toss, the enemies' area 08) against the ROM micro-executed,
8000 fuzzed cases over four battle states, all of WRAM compared but the direct page; the tally counts the cases with two
victims or more on each side and each `$9608` arm, and the check fails if any is zero. The live check plays the Manoria
nuns fight with Flame Toss learned (`CT_POKEWRAM 2839=80`) and pins the staging `$9604-$9608 = 00 01 04 02 01` and the
victim `$AD8E = 04` the rule's cartridge shows for the same target. Both need private battle captures.

### field-organ-door-opens-live (`--scene 129 --loadwram m81_naga_after_fight.lakewram --walkscript …`)

The same machine on a door a SCRIPT drew, played live. From the WRAM after the Naga-ette fight the drive mashes A
through the scene and Frog's naming (`CT_HOLDBTN` Start accepts it), plays the Manoria cathedral's organ from below
(obj13: `E5 27 01 28 04 0C 04 3B` copies the door's rect over the wall on all the planes, C0/AF4E) and walks right
and up into the door. The countable is `doorlist: f3634 C0/28C0 tile 070D` (`CT_DOORLIST`): C0/9D35 found prop1
bit 4 on the tile ahead and C0/0D3E took the request; `changeloc: scene 129 -> 130` says the walk went through. The
host's collision planes (`f->C`) take a scripted copy by re-reading the destination rect from the WRAM
(`fieldCollFromWramRect`); its old copy of its own packed cells dropped the door flag (zb bit 5), so the exit took
the party through the door shut. A `--loadstate` / F9 hides this (it re-reads every plane from the WRAM), which is
why `field-loadstate-keeps-the-door-the-organ-opened` passed throughout. Needs the private capture
`saves/field_re/m81_naga_after_fight.lakewram`. The request's frame is the port's own drive's; re-pin it only for a
change that moves the walk or the scene before it.

### field-scene200-prio0-occluder-order (`--scene 200 --at 632 143`)

Pins that a `$0F80` priority-0/1 occluder blanks a sprite pixel ONLY where it is in front
of it. On hardware OBJ-vs-OBJ is settled first by OAM index and only the winner's priority
is weighed against the BGs, so the front/back answer is the same `(group, Y)` key the draw
list already sorts by. The port cut them AFTER every sprite instead, i.e.
unconditionally — and scenes 200/401 have three companion sprites at `$0F80 = 0x04`
(group B, OAM base 0x1A0, the BACKMOST group) sitting on the knight, Crono and the
princess, eating most of all three. The oracle's OAM has those companions at entries
104-115 (priority 0) BEHIND the characters at 28-43 (priority 2) and draws all three
whole. Occluders now cut at their own turn in the sorted list, which also retires the
`ctlLocked`/player-mask exception: the intro blanket (group C, the FRONT group) still cuts
sleeping Crono and `render-scene1-snapshot`'s fnv is unchanged. The NPC count in the
signature includes the occluders now (they are entries in the draw list); the fnv is the
canary.

### field-obj-unit-priority-seated-soldiers-vs-oracle (`tools/frame_region_cmp.py`, loc 07B)

Pins that every OBJ UNIT takes the priority byte the anim drain gives it and that a
priority-0/1 unit loses to BG1/BG2 even when the object's other half does not. The 600 A.D.
castle's seated soldiers are `8E 12` ($0C00 = 20, $0C01 = 10): the cartridge hides their
lower unit behind the chair, the port drew it over the chair (the user's F5 capture
ct_debug_022). Port seeded with `saves/field_re/loc07B_soldiers_seated.lakewram` against the
oracle on the `.state` it was cut from: the whole frame identical. The fixture's recipe is in
regress_inputs.json; disasm/notes/field_obj_unit_priority.md.

### field-scene434-decision-box (`--scene 434 --talk 11` + `TB_AUTOADV=1`, frame 500)

Norstein Bekkler's lab, captured on the OPTION page of its `C0 40 03` box (operand1 = the
string, operand2 = the option-line range per C0/367C, so 0x03 = lines 0..3 = four
options). Pins two render facts measured against the oracle at the same box: the decision
window is the ORDINARY 80-row dialogue window on every page (it used to be auto-sized to
the page's line count and 9-patched, giving a 2-line prompt a 53px box — the oracle's
prompt and option pages occupy identical rows and the window texture maps 1:1 onto them,
checked pixel-for-pixel against `saves/ct_window.bin`), and the cursor is the ROM's
pointing HAND, not a triangle and not the menu's hand (`src/data/menu_itemdetail.h` agrees
on 63 of 138 opaque pixels). `src/data/dlg_cursor.h` is generated by
`tools/gen_dlgcursor.py` from an oracle frame minus the window texture — the same fb-lift
that produces `T_WINDOW` itself. Brittle-by-design pixel canary; the capture frame (500)
is chosen because the box's own pages advance on a timer under TB_AUTOADV.

### field-battle-zorder-bat9-oam / -forest-live (the battle draw list)

Two halves of the same law. The ROM has no "PCs over enemies" rule: CF/E83F builds a list of the
render objects `0x0C..0x16` (= combatants 0..10) keyed on `LDA $1D17,Y` = `$1D23+cmb` = the LIVE
SCREEN Y, SELECTION-SORTS it DESCENDING, and hands out OAM blocks from ENTRY 4 up (`LDY #0010 / STY
$C9`; entries 0-3 are the bracket/hand). Lower OAM index = in front, so the largest Y is FRONTMOST
and the two kinds INTERLEAVE. `-bat9-oam` grades the sort against the ORACLE'S OWN NUMBERS: the
`CT_BATTLE_RENDERWRAM` vehicle reads the seed's `$1D23`, and `bat9_lake.state`'s OAM blocks decode to
objects 8, 6, 0, 2, 7, 1 — including the cmb0-before-cmb2 TIE at Y=128, which is the ROM's
`BCS`-keeps-the-earlier rule. `-forest-live` grades the same order built from `h->mvLiveY` in a
RUNNING fight, so it also pins the claim that our mover's Y IS `$1D23` (both read Marle 144, Hetake
127/95, Crono 71). Dossier: `disasm/notes/battle_sprite_zorder.md`.

### field-battle-cyclone-holds-its-facing-until-the-idle-reaim

The Cyclone's script has no op 72 (C1/5FDE), so Crono keeps the spin's last facing (down) after the act end and the
idle walk's re-aim latch C1/2F22 turns him up and publishes the pose (cartridge: `$96DB` 01 at f627, 00 at f951,
C1:2F94). Pins the spin's last `ascface` and the first seat-0 `pcwalk ... aim=1`. Mutation: an act-end re-aim of
every PC attacker (the removed SETTLE) leaves the latch nothing to change -- no aim=1 line, the check fails.
Dossier: disasm/notes/battle_facing_engine.md section 4 (correction).

### field-battle-item-{elixir-script,ether-mp-row,heal-op43,revive-op43}-vs-oracle / field-battle-tech-cyclone-front-entry-vs-oracle

The anim-script ops a7b7a5d ported on a branch, reconciled with main (`disasm/notes/battle_asc_inert_ops.md`). Each
is a `--battletest` drive on a `saves/battle_re/*.raw.wram` seed whose trace lines carry the LOGICAL oracle's own
numbers at the same rel (frame - `$A013` rise): the Elixir's C0/C2 orbits (mover mode 5, the `$A3DF` depth flip),
ShowDamage's `$2300/$2380` copy, record set 1 and the popup animator CF/E781 (`$AC5A`, `$A0E8`, `$A0DC` set and
cleared, the MP number resting 18 px higher on D1/5E49); the Ether's kind-2 number on the same curve; Heal's six and
Revive's one op-43 stores and the objects' positions after them; Crono's always-front FE entry through the Cyclone
(the list sees `$A5CD` one frame after ops 27/28). `attract-0ad-battle-turn-machinery-vs-oracle` carries the
X-Strike's four FE edges (CT_BATTLE_ZTRACE). Re-pin only after re-measuring the oracle side
(gato_elixir.state / gato_cyclone.state / attract_demo_battle_f1850.state, the recipes in the checks' notes).

### field-battle-idle-dwell-rate (`CT_BATTLE_RENDERWRAM` + `CT_BATTLE_IDLETRACE`)

An idle-window entry lasts `dwell` VISITS of the C1/3407 cursor tick, and a visit is NOT a frame —
C1/3232 dispatches a rotating group. Measured on the oracle's own counters (`--monitor "$9695:1"`
etc.), the DECs alternate **13 / 7 frames** in both bat2 and bat4 = exactly **2 dwell units per 20
frames**, and the outputs agree (the Beetle's cursor steps every 20f at dwell 2, the Avian's every
~30f at dwell 3). The port used 2 frames per unit — five times too fast, which is the user report
this closes. **The line pins `dt=` (the frames the previous entry held), not just the cursor**: the
first version of this check pinned `(frame, cursor)` pairs and its mutation SURVIVED, because 20
frames at the wrong divider is still 2 cursor steps modulo the Beetle's 8-unit cycle, so every
sampled frame lands on the same cursor. Pin the QUANTITY, not a sample of a periodic function.

### field-battle-enemy-name-list / -hud-name-after-victory (the strip's enemy names)

The name box is built ONCE, at staging, by CC/ECB2 — dedupe the staged slots' ids into the 3-entry
list `$98FF` (counts in `$98CC`), copy each type's 11-byte `$CC:6500` record into the strip's text
buffer `$7EACBC + k*24` — and **never re-read**: `--monitor "ACBC:12" --writer` over a whole forest
fight logs ONE write (CC:ED66) and nothing after, and at the victory results screen the oracle reads
`$96F8` all-00 and `$AF02` all-FF while `$ACBC` still holds "Hetake" and its strip still shows it.
`-enemy-name-list` pins the BUILD (dedupe + per-type counts + the fact that 0xEF is the record's
SPACE, so "Avian Chaos" and "Blue Imp" survive whole); `-hud-name-after-victory` pins the RENDER
~1000 frames after the last kill (the box still reads "Gato"), which is the user report: an empty
box read as "this menu should have disappeared". Dossier: `disasm/notes/battle_hud_enemy_names.md`.

### battle-strip-upload-{main,item,cancel}-vs-oracle (the strip's upload pipeline)

`tools/battle_strip_upload_cmp.py main|item|cancel` runs GATO's fight from the same WRAM on both sides (gato_wait.state,
Crono alone with every tech and 99 MP) through the drives of `disasm/notes/battle_hud_upload_pipeline.md` and grades, from
the band's settled frame to the drive's end: the WRAM images the menu pass writes (`$0CC0`, `$A6E1`, `$0E80`, `$0B40`,
`$99E3`; port f = cartridge f), the strip's VRAM (`$7820` / `$7A00`: the port's `CtBattleStripVram` at f against the
oracle's `--monitor V7820:384,V7A00:384` at f+1 -- the port's upload is the NMI that opens the next frame) and the strip's
thumbnail rows (port P = cartridge P+2), then lists the frames where the strip's picture changes on each side. The pinned
events are the dossier's: the HP refresh c596, the hand c648, the Tech list c660, its hand cleared c668, the confirm's band
c686 (main); the item list c668 and its cursor c680 (item); the cancel c672. A change that moves one of them is a timing
change of the menu pass or of the NMI's copy, never a re-pin without a cartridge run that shows it.

### attract-party-leave-visit-vs-logical-oracle — a leg's length is a visit count

USER REPORT (translated from Portuguese): *"the demo scene is ending a little before the music"*. The attract's legs are graded against the
LOGICAL oracle (`disasm/notes/attract_leg_clock.md`), and the first hub was 8 frames short because op 0xD4 (C0/394E)
scanned the membership array `$2980` instead of the party OBJECT slots' `$1101,X` -- so its exit carry, which `5AB4
BCC` turns into the end of a visit, came out 1 where the cartridge's is 0. The check pins the visits themselves, on
`$2980`'s writes: the hub's leaves at post+38 / +42 and the 1F5 at +66 (the oracle's f39 / f43 / f67), and the 0AD's
closing swap as three ops in one visit then one. ⚠ **Re-anchoring after a change like this is MEASURED per vehicle**
(`--selftest`, lockstep, unpaced interactive each number the attract differently): run the old and the new binary,
diff their `$0100/$0119/$1E00` timelines, and move each check by the offset of ITS frame. Taking the first shift under
which a check passes is not enough -- on a still frame several shifts pass (field-07d-obj8-spawns-on-lead passed at +10;
its frame had moved +12).

### menu-x-refused-off-the-field-main-loop (`CT_MENUGATE`) — X only works where C0/00A1 runs

USER REPORT (translated from Portuguese): *"the menu can be opened on the save selection screen, in the intro, etc"*. C0/00A1 `JSR C0/18D9` is the
only caller of the field's X poll in bank $C0, inside the main loop C0/0097..00BD; op 0xC8 (every mode: naming, the
load shell, a shop), op 0xFF < $90 (the title, the credits, a gate warp) and op 0xDF/0xE1 all leave that loop, so the
cartridge cannot open the menu under any of them. The port excluded the naming screen alone. `CtSceneOffMainLoop` now
answers for all of them, and `CT_MENUGATE` prints every X press with the gate's inputs -- the check pins a press on the
title (`seq=2`) and one on the load screen (`shell=65`), both `explore=1 $0162=00 $0110=00` (C0/192F-193D alone would
have opened the menu) and both `refused`; the loader row line is the liveness.

### fair-silver-points-shown-vs-oracle / fair-silver-points-tally-persists-on-entry — the Silver Points chain

USER REQUEST: *"implement the silver points count in leene's square"*. The count is three ROM mechanisms the
port lacked (`disasm/notes/fair_silver_points.md`): the init pass never ran op 0x53 (C0/693D), so loc 5's obj0 fn0
`53 52 00 1B` never loaded `$7F0052` into the tally local and every entry restarted the count at 0 -- the first win
then wrote 1 over the saved total through fn3's `58 1B 52 00`; the live VM stepped over op 0x4C (C0/680C), so the
keepers' `4C 00 02 7E 1B` never put the count into `$0200`; and the dialogue carried a literal `{num}` where code
0x0D (C2/5953) prints `[$3D]` = `$0200` in decimal. `-shown-` grades the keeper of the Tent of Horrors from a fresh
load seeded with the ORACLE's flags (`saves/field_re/fair_sp1.flags`, `$7F0052 = 01`): local `$1B` = 01 at f0, `$0200`
= 01 at the A press, and the box text; `-tally-persists-on-entry` wins the bell on the same fresh load and pins 01 ->
02 in the local, then in `$7F0052`. `CT_FIELD_MEMMON` reads bank $7F through FLAT addresses (`10052` = `$7F0052`).
The oracle side of each (`tent_keeper_lake.state`, `s5_bell_marle.state`) is in the check's notes.

### shop-buy-sell-ram-vs-oracle / shop-screens-vs-oracle — the shop, RAM and picture

USER REQUEST: *"implement the shops"*. Event op 0xC8 $80-$BF is menu mode 2 = screen $0C (the shop number is the
operand & $3F); the dossier is `disasm/notes/shop_screen.md` (section 12 = the port). `-ram-` drives the fair
merchant's shop $10 from `saves/field_re/shop90.lakewram` (the oracle state's WRAM) through a buy and a sell of a
Karate Gi and pins the windows ($04CA max+1, $1044 total), the bag and the gold after each commit, the $55 trade
sound with its throttled blip, and the script resuming at the close -- the values the oracle's bs_a run measured
(C2:FDD3 / 87BD / 87D0 / FDAF / 87F3). `-screens-` runs `tools/shop_screen_cmp.py`, which drives BOTH sides the same way
and compares eleven whole frames at 0 px. ⚠ **The frames are ALIGNED, not equal**: the port opens the shop 22 frames
sooner (the menu shell's black and its C2/83B2 fade-in are not modelled -- the X menu's neither), an oracle `--peek`
picture lags its logic by one frame, and the cartridge's menu reads a press one frame after the pad latch; the
tool's docstring states each offset and where it was measured. If the shell's timing is ever ported, the tool's
offsets are what changes, not its expectations.

The same work re-pinned seven menu checks for one colour (the hand's and the arrow's OBJ mid grey is 107,99,99 on
the oracle's own Item, ribbon and shop frames; the shared assets had 106,98,98) and two Form checks for the sprite
machine's picture lag (a new pose shows one frame after the OAM -- measured on the shop and on the Form screen).

### field-arrival-gate-vs-oracle — the event VM's local words are little-endian

USER REQUEST (queued 2026-09-24): unify the VM's local-word byte order. The event VM stored a local WORD big-endian
(`ct_vm.c` r16/w16, the CTViewer convention) where every ROM word op is a plain 16-bit load/store: op 0x13 is C0/6313
`REP #20 / LDA $7F0200,X`, and C0/42C9's `LDA #0008 / STA $0D00,X` went through the same w16 (so
`field-pc-slot-arm-stores` had pinned the bug: `0D00=00`, re-pinned to the oracle's `08`). Op 0x49 (the pointer read,
C0/67C7) had no arm at all, so the twelve locations that open with `49 05 01 7E nn` + `13 nn <location> ..` -- where
did the party come from -- never matched. `tools/arrival_gate_cmp.py` pokes `$0105` into `saves/ow_w0.state`,
teleports the ORACLE into location 65 through its own loader, then hands that WRAM (graded bytes cleared) to the port
and reloads 65 with `CT_CHANGELOC` so the port's init pass derives `loc[0B]` and `$7F01A3` itself (`--loadwram` alone
overlays `$7E` after the init pass). Both arrivals ($01F0 -> bit 3, $01F1 -> bit 4) must match. The fix routes the
whole variable grid, the pointer read 0x48/0x49 and the local arithmetic 0x5B..0x6F through move.c's
`--sweepops`-graded bodies in step() AND the init pass; `assets/script.py` mirrors it (`ct.exe --vm` 512 OK), and op
0x60's length is the ROM's 4 (C0/6AC9), not 3.

### field-textbox-window-vs-oracle / lockstep-textbox-four-lines-tent-keeper / lockstep-decision-box-s434-clean — the dialogue window

USER REQUEST (queued 2026-09-24): port the textbox's 4-line window (`$0131`). `disasm/notes/textbox.md` section 4 has
the derivation: four lines a page (C0/2126's line cursor `$0131` wraps 3 -> 0; a page CLEARS, nothing scrolls), a
typewriter that renders on every state-6 iteration with a break costing its own, the line buffer `$7E:F000` reaching
the screen through the field NMI (C0/EB79 -> C0/6ECB, into window line `$0131` while `$0136 != 0`), the text row at
window top + 10, the closing quote's own glyph, and a pressed page whose clear (1FB4) is the next frame's.
`-window-` runs `tools/textbox_window_cmp.py`: the tent keeper's four-line box and Marle's page turn, both drives
ALIGNED (the port's press on the frame the cartridge reads its own), the text rows compared at 0 px on 21 frames --
typing, breaks, the end, the old page leaving line by line, the new page's first characters. The two lockstep checks
grade the machine (`$0129`/`$0126`/`$0134`/`$0131`) every frame with `CT_FIELDLOCK` + `CT_PROJECT=box`.
⚠ The same work re-pinned ten checks whose frames or pictures moved with those facts (the notes of each say which);
`field-textbox-typewriter-vs-oracle` went from "apart f101..f479" to CLEAN over 1099 frames. Open, graded nowhere: the
decision hand is on screen two frames after `$0162 = 2` on the cartridge (the port draws it from the arming frame;
the routine is not found), the window's open-ramp pixels, and its left-edge weave.

### assets-dialogue-every-script-box-reads-the-rom / field-loc010-obj9-last-string-types — the table's last string

USER REPORT 2026-09-30 (F5 capture `build/ct_debug_003.state`, loc 010): a window opened with no text and stayed.
obj9's `BB 26` asked for string 38 of table `$FF:8400`, and the pack held 38 strings (0..37): no string, so the
typewriter never finished, state 7 never came, and no press could dismiss it. The ROM keeps no string count --
C2/57E7 `LDA $0C / AND #00FF / ASL / TAY / LDA [$0D],Y` reads string idx as the u16 at table + 2*idx -- but the
packer bounded each table with a CTViewer heuristic that needs the NEXT pointer, so every table lost its last string,
and 16 boxes in 16 locations ask for exactly that one. `tools/dialogue_refs_check.py` walks every location script
(from every fn pointer on through internal RETURNs), pairs each box op with its script's 0xB8 table and compares the
string with that ROM read, twice: in `ct_assets.dat` (`dat` -- a stale pack fails here) and in the extractor's own
output (`extractor` -- the mutation). The field check pins the reported box through the engine (`--talk 9`: typed
from f16, state 7 at f44). Re-pin the first only if the script walk legitimately finds a different box census.

### field-inn-sleep-vs-rule-cartridge — sleeping at an inn: loc 10F's way back and the wake-up

USER REPORT 2026-09-30 (F5 capture `build/ct_debug_004.state`, the Truce Inn keeper's "Check in?"): after sleeping
the screen stayed black. The keeper's `E0 0F 03 08 08` sends the party to loc 10F with the way back stored in
`$7F0166..016C`; 10F's obj0 counts, copies it into locals 0A-0D and leaves with op 0xE2 (C0/3B9E: op 0xE0's
fade-change with every operand a LOCAL), which the live VM stepped over. `tools/inn_sleep_cmp.py` runs the cartridge
under the rule and the port seeded from its WRAM (`saves/field_re/inn_sleep_10f.state` / `.lakewram`, made by
teleporting `base_loc6` to the counter, poking the keeper's five stores and teleporting to his destination) and grades
the return (frame, location, tile, facing), the beds and the wake-up walk of obj1/obj2 frame for frame. The walk found
two more mechanisms: the init pass lacked the result-word loads 0x19/0x1C and wrote the PC loader's own `$1100,X` only
after every fn0 (Marle took Crono's bed), and the host's scripted-lead follow rounded the lead's words. PINNED
RESIDUAL: the walk starts one frame early in the port (loc 00C's reload is priced short, the open load-clock item); the
comparison aligns each side on its first step. Op 0xE2's own handler is graded by `sweepops-player-script-opcodes`
(its seeding was blind at first: two mutations survived a zeroed local array).

### menu-shell-open-close-vs-logical-oracle — the menu shell's open and close

USER REQUEST (queued 2026-09-24): model the menu shell's timing. `disasm/notes/menu_shell_timing.md` (sections 1-6
derived, section 9 the port) is the whole timeline: the caller's 15-frame fade, the switch, 16 black init frames with
the NMI off, the screen's substate 0 (the ribbon builder's yields, the shop's C2/DACA), the fade-in C2/83B2, the first
input call at P+1 dropping A/B/L/R, and on the way out C2/83CA's fade-out over the frozen image, the RTL, the field's
reload and C0/2824's fade-in. `tools/menu_shell_cmp.py` compares the brightness level of every frame on the X menu and
the shop against the LOGICAL oracle (the real-time one has lag frames in both builds) -- 0 frames differ. The reload
window is deliberately not graded: its length comes from the location load model, which prices C0/286C 2-3 frames
short after a menu (loc $082: 38 vs 40; loc $005: 35 vs 38), an open item of that model. ⚠ The same work re-anchored
46 menu checks: every drive's first wait after the menu opens grew by the shell's delta (19 field X, 18 overworld, 22
shop) and their frame-only lines moved with it -- nothing inside the menu changed.

### menu-open-sheets-and-tonic-clamp-vs-oracle / menu-wram-around-the-x-menu-vs-logical-oracle — the menu's RAM

USER REPORT 2026-09-24 (translated from Portuguese): *"when using a tonic, it is possible to exceed the character's maximum HP"*. The item appliers
clamp to the sheet's +$3F (effective max HP), which only the menu's open pass (C2/8139 -> C2/9875 -> C2/99B2: all
seven sheets recomputed and written back) makes true; outside the menu the word holds something else (1028-1029 on
party3_field). `-sheets-` compares the seven sheets after the open and a Tonic on Lucca (40 -> 88, both sides).
`-wram-` grades the menu program's own bytes every frame of one X menu against the logical oracle: the RNG cursor
$0D00 (C2/842F, from S0), the brightness shadow $0D10, and $0D38 -- which also pins the callers' RAM discipline (the
field's C0/011B / C0/0905, the overworld's C2/2E23 / C2/2E72) the port did not have.

### menu-equip-list-climb-and-preview-vs-oracle — the Equip screen's candidate list

USER REPORT 2026-09-24 (translated from Portuguese): *"when opening to equip weapons, the box does not rise with the canonical animation"*.
`disasm/notes/menu2_equip_change.md` section 5: C2/A4AF's eight-frame climb, the settled bar's own ramp, C2/A697's
descent after C2/A64D's yield, and the live preview C2/A5C3 / C2/A5E7 (new values in palette 2 / 7) the port never had.
`tools/menu_equip_list_cmp.py` runs one drive over all of it and grades three things on every frame f138-f330 against
the logical oracle: the rows of the window rules, the bar's pixels, the Attack digits' pixels.

### menu-equip-list-draws-no-count-vs-oracle / menu-equip-list-slide-draws-no-count-vs-oracle

USER REPORT 2026-10-09 (translated from Portuguese): *"the equipment count shows on the Equip screen when there is more than one"*. C2/8953 copies
each candidate's bag count into `$0F62` (and C2/9F75 stages it in `$04CA` for the slide's incoming row), but the row
writer C2/A569 -> C2/A59A puts the name's 10 cells and nothing else into the BG buffer; the port drew the count at x116.
Every earlier equip drive poked count-1 items, so nothing graded it. `tools/menu_audit_cmp.py equipcount` (Iron Blade x3,
Steelsaber x12) and `equipslidecount` (weapons 02-0D held 2-13, the one-row slide) grade every frame against the logical
oracle; `disasm/notes/menu_equip_list_no_count.md` has the oracle's BG buffer / VRAM rows. Re-pin only for an
oracle-verified change to the drives.

### shop-equip-mode-vs-oracle — the shop's equip sub-mode, every frame

Task 4 of the 2026-09-24 queue: Up/Down on Buy/Sell (C2/E017 -> substates 6-8) ran nothing in the port.
`disasm/notes/shop_screen.md` section 13 is the port and what its picture needed: the NMI's HDMA mask `$0D13` (the
equip set's scroll tables, all read from the ROM tables C2:C213 installs), BG2 V = `$0D0A | $0D0B << 8` (the second
window layout in BG2 map rows 32-59), the C2/8385 upload records (a redraw no record carries shows a frame late), the
description TYPING one decoded character per frame (C2/FA49's task; the buy and sell lists too) and the weapon's
colours 13-15 that C2/F5A7 loads into the party's OBJ palettes on the way back to Buy/Sell.
`tools/shop_equip_cmp.py` drives both sides through every path (the fair merchant, a Power Ring and a Third Eye poked
into the bag; the drive is in its docstring) and compares a THUMBNAIL of every frame (the oracle's `PEEK_THUMBS`, the
port's `CT_SHOTS_THUMBS`, the same records) on the LOGICAL oracle: port frame P = oracle thumbnail P+2, a press one frame
later on the port. Four lines: the mode f50-f501 = 0 frames differ; the buy list after it = 0 frames; the bag and
Crono's sheet after the shop closes = 0 bytes. `--peek F ...` adds full-resolution frames and PNGs under build/_shopeq/.

**THE ALIGNMENT (2026-09-24), for every menu tool.** The cartridge displays a frame's state during the NEXT frame (the
NMI at the end of frame P uploads its tiles, sprites and brightness together), so the port's frame P is the oracle's
`--peek`/thumbnail P+2 on the LOGICAL oracle, with the port's press one frame after the oracle's drive
(`tools/menu_audit_cmp.py` measured it on the member select's cursor, the screen switches, a field walk and the
menu's sprites). The menu tools had used P+1 (and the shop's the real-time oracle, whose lag frame in C2/DACA makes its
P+2 the logical P+1), and four behaviours had been modelled with one compensating frame: the shell's brightness
("the previous frame's value"), the Equip list's climb/descent start and its "commit frame shows the list as it was",
and the menu sprite machine's "the pose shows one frame late". All four are removed; menu-shell-open-close,
menu-equip-list-climb-and-preview, shop-equip-mode and shop-screens are 0 at P+2 without them. Memory
ct-frame-alignment-p2.

### menu-audit (tools/menu_audit_cmp.py) -- the audit instrument, not a check (yet)

User request 2026-09-24: *"investigate the menu again, all of its screens and features"*. One drive through the
ribbon's tabs (Equip's member select, its gear panel and list; Item's list; Tech; Config; Form; the close), thumbnails of
every frame on both sides at the alignment above, the differing frames listed in runs with the difference's bounding
box. It found the chrome palette's floor(v*255/31) conversion (every menu frame), the member boxes' dim (C2/9E76's
palette | 4, one frame behind the hand), the dimmed panels' icons (CGRAM row 4, not halved RGB) and the alignment
itself. What it still lists is the audit's queue.

### audio-pacing-no-drift (`CT_AUDIO_PACE`) — grading a symptom you cannot hear

USER REPORT (translated from Portuguese): *"the sound sometimes cracks"*. The crackle is a real-time property of a windowed host, so
the check grades its CAUSE instead: the producer emitted one 1/60 s of audio per LOOP ITERATION (a hard
`if (audioAcc < 1.0) audioAcc = 1.0;` floor that also destroyed the fractional remainder) while the frame
governor budgets 16 ms — 800 samples at 48 kHz IS 16.6667 ms, so production outran the device by up to
4.17 %, and the only relief valve discarded **266.7 ms** in one `SDL_ClearQueuedAudio`. The fix is the
vendored LakeSnes reference's own shape (`_vendor_LakeSnes/main.c:323`): no floor, plus a queue LEVEL GATE
at 6 frames. **The expectation is `verdict=OK`, a comparison of two quantities measured in the same run
(ticks advanced vs wall clock elapsed)** — not an absolute timing, so it is not flaky on a slow host; with
the floor restored the same run reads `ticks == iters` and `verdict=DRIFT`, which is the pinned mutation.

⭐ 2026-09-23 — ONE HOST CLOCK (USER REPORT (translated from Portuguese): *"the demo scene is ending a little before the music"*). The
fix above clocked the SPC by the wall clock but left the ENGINE on `if (el < 16) SDL_Delay(16 - el)` (at least
16 whole ms, never paid back): 60.58 f/s on a 170 Hz screen, 44-56 f/s headless under a coarse timer, against
the SPC's 60.00 -- the attract's closing fade reached song `$18` ~80 frames early. Now `hostPace` (field.c top,
the vendored LakeSnes loop's shape) puts frame k at T0 + k/60 s and a paced host runs ONE SPC frame per engine
frame; the check's line grades `frames` against the wall clock AND `ticks == frames`, `irregular=0`,
`reanchors=0`. Its mutation is the old governor. **audio-pacing-coarse-timer** is its twin under
`CT_HOST_TIMER_MS=16` (a minimised window's timer, the case that had moved the SPC off the engine's frames):
an oversleep must be paid back. Its three mutations: drop every late pass instead, the old governor (~31 f/s),
and the wall-clock SPC -- which only a jittery host can tell apart, since on a perfectly paced run it too runs
one frame a pass. End to end: the paced attract's audio output is sample-identical to the lockstep run's over
all 11151 frames.

### field-battle-item-script-sfx-* / -heal-number-green (items)

An item use is a bank-CE anim SCRIPT (`CD/63F0[$9877*2]`, loaded by C1/47C6 + C1/47D7 exactly as a tech is
by C1/4617 + C1/4623) and nothing ran it, so items posted the caster's WEAPON hit sound instead of their
own `78/79/7A` ops. The `-sfx-*` checks pin the ids, tracks and opcodes the script itself carries (the
Elixir's middle post is an **op 0x79**, the panned family). `-heal-number-green` pins the OTHER half: the
popup's palette comes from the ROM's KIND byte `$AD9E` (C1/5B99-5BAD: kinds 1/2 → OAM attr 0x39 = OBJ
palette 4 = the green ramp at D1/4D88), and the line names `romKind` + the palette so the check says WHICH
rule fired rather than only that some pixels changed. Dossier:
`disasm/notes/battle_item_anim_sfx_and_heal_colour.md`.

### vmdiff-scene8-taban-moveto / -moveto2 (the scripted MOVETO, from the user's own savestates)

USER REPORT (translated from Portuguese): *"taban crosses the teleport (blocked tiles) when running in scene 8"*. Taban is **obj10**,
running op 0x96. Two port inventions made him walk a different path from the ROM's — a straight diagonal
that crosses the Telepod: (1) the heading was HELD while both axes were short of the target tile, but
`C0/4F61` re-reads `$1801/$1881` and recomputes at `4FAB` on every step issue (oracle: obj10 re-aims at
f2/f6/f14); (2) the arrival snap corrected ONE AXIS per visit, but `C0/305D` sets both velocities in one
call (`3079` falls into the same Y test at `3089`) — it is diagonal. Both fixtures are the user's snes9x
captures converted with `tools/s9x_to_lake.py`; `--withpos` grades POSITION, so this pins the mover
sub-pixel for sub-pixel over 600 frames. **-moveto2 is the control**: it was already CLEAN before the fix,
so it proves the change did not move a case that was already right. Dossier:
`disasm/notes/field_moveto_reaim_and_snap.md`, which also records what came back clean first (1728
`--stairtest` runs, all 256 collision tiles, the follower, the run magnitude).

⚠ **A re-pin is not always a re-pin.** `field-scene434-decision-box` failed in the same pass and its
frame was re-ANCHORED (500 → 470), not re-pinned: the conversation clock moved with the scene's NPC and
f500 had advanced past the option page, so pinning what f500 now shows would have kept the check green
while grading a page with no decision box in it. At f470 every textbox field is byte-identical to the
original expectation.

### field-chest-* (the treasure-chest subsystem, `--scene N --at X Y --walkscript` + `CT_CHESTDBG=1`)

Five checks for a subsystem the port simply did not have — user report *"chests do not open"*. A chest is
not an event object and uses no opcode: it is the `$F5:F000` table + the `$7E:70C0` plane + the probe
`C0/1DF4` that the A handler *falls through* into (`C0/1ADC BRL ->1DF4`, which our own
`field_action_probe.md:26` had annotated "nothing happens"). Dossier:
`disasm/notes/field_treasure_chests.md`.

| check | grades | its mutation |
|---|---|---|
| `field-chest-opens-item` | the whole path: flag, item, bump, message (loc 26, item 202 Lapis) | delete the probe call — **no chest in the game opens** |
| `field-chest-gold` | payload bit15 → gold `payload << 1` = 100G, and the `{num}` string | drop the `ASL` — every gold chest pays half |
| `field-chest-already-open-is-empty` | a re-press awards nothing and shows "Empty!" | `fieldTreasureOpened` → 0 — chests re-award forever |
| `field-chest-preopened-at-load` | `C0/A782`: an opened chest is drawn open **at load**, no press | disable the pre-bump — re-entering redraws it closed |
| `field-chest-e0-metatile-family` | the second closed id (`$E0 → $E1`), loc 47 | keep only `$FE` — the `$E0` chest never opens on screen |

Three things worth copying from these:

1. **The trace is the check.** `CT_CHESTDBG` prints the ROM's own quantities — probed tile, global
   flag index, raw payload word, result code, decoded message — so four of the five grade *semantics*,
   not a framebuffer hash, and a wrong item id or a wrong gold amount is legible in the FAIL diff.
   The message is printed with its centring spaces stripped precisely so the text is gradable.
2. **The pre-bump check must not press A.** With a press it would pass on the runtime bump alone and
   the load-time pre-bump could rot green. Its walkscript is `u:2,.:40` — nothing but a load — so the
   only thing that can produce the expected frame is `C0/A782`. It is the one of the five that has to
   be graded by render, because the pre-bump has no other observable.
3. **Its fixture is a flag image, not a savestate.** `saves/field_re/chest26_opened.flags` is a 0x200
   `$7F` page with byte 1 = 0x40 — global chest index 6 (loc 26's first record) opened and nothing
   else — fed with `--loadflags`. Cheap to regenerate and readable in one line, unlike a state blob.

All included checks were verified read-only (no file writes), so they run as
parallel subprocesses. A check can opt out with `"exclusive": true` (none currently
needs it).

### field-battle-{pcblock-derive,lucca-type2,marle-type2} (the PC damage pass)

Three checks for "lucca's damage too high"; the interesting part is what makes them un-rottable.

1. **The fixture must carry the ROM's `FF` tail.** The battle block is a copy of the field sheet, and
   `$CC:0000 + cid*0x50` ships `FF` at `+0x41..+0x4F` for all seven characters. On a seed whose sheet
   has already been derived, deleting the equipment derive changes nothing and the check goes green
   over a broken engine — which is exactly how the defect shipped, because every battle fixture in
   this suite is an oracle capture of a Crono/Marle fight, i.e. the two charIds the old bake covered.
   `gato_t0.raw.wram` carries the tail for both PCs; `party3_freshsheet.lakewram` is `party3_field`
   with the tail restored on Lucca's and Frog's sheets.
2. **A per-formula VARIANCE RANGE needs its own observable.** The first version of
   `field-battle-marle-type2` pinned the base and the fight length, and the mutation that swaps the
   type-2 range for the type-1 one SURVIVED: both draw exactly one value from the same stream
   position, so the base is unchanged and the attack count is unchanged. The `atkdmg` line (base,
   bonus, def, crit, dmg) is what kills it.
3. ⚠ **A mutation whose `find` string is not in the file is reported as SURVIVED, not as an error.**
   That happened here: the fix renamed the line the mutation targeted (`dmg += battleRng(...)` became
   `bonus = battleRng(...)`), and the run printed a plain `SURVIVED` that looks exactly like a blind
   check. Before believing a survivor, `grep` the `find` string. This is the fourth way a check can
   lie, beside the three already listed above.

### field-battle-endteardown-mode6 (the battle-end survivor law)

The lesson here is **an arm no fixture can reach is an arm no check can grade**. The ROM's law is
"a staged enemy's field object survives iff it was staged MOVE-MODE 6" (C1/B3BB masks `$29C6` to
`& 0x40` at every battle exit, C0/07ED reads it back). Every battle fixture in this suite stages
move-mode 5 enemies and kills them all, so the law and the invented axis it replaced
(`alive-list != FF || deadStanding`) agree on every one of them, and BOTH mutations survived by
construction. Two test surfaces make the disagreement reachable, and neither changes engine behaviour:

* `CT_STAGE_MODE6=<hex slot mask>` forces the staging bit for the named slots, so the check can pin a
  slot that is `alive=0 stand=0` and still survives — which is exactly the case the old axis gets wrong.
* `saves/battle_re/forest_teardown.lakewram` is `forest_pre_wrapped` with ONE byte changed (obj22
  `$1201 = 0x01`, the `(… & 3)` source of the staging record's `+7`), which is what makes `C1/B3BB`
  ITSELF observable: the `endclear` line reads `01 -> 00`, and a run without the mask reads `01 -> 01`
  and keeps a dead enemy. With the stock fixture all three slots stage `0x00` and the mask is a no-op.

The control is `field-battle-forest-return` — the same run without the env var, whose pinned
`fieldfnv=D72503D8` is unchanged by the whole pass.

A second trap from the same pass: `--battletest`'s pinned GATO staging record checks `$29E3+7 == 0x42`,
and that byte now legitimately reads `0x40` after a fought-out victory because the teardown runs. The
harness samples AFTER the run, so the check accepts both values with the moment spelled out — the same
shape as the `$AF0D` comment beside it, which exists because `$AF02` "legitimately drains to all-FF".

### vmdiff-scene254-textbox-unpark / -phase1 (a lockstep A/B, and how it lied twice)

Scene 254 is CT's combat tutorial — dense op-0xC2 dialogue plus scripted movers — and running a lockstep
on it produced two harness lessons worth more than the fix they found.

1. **A `CLEAN` line is worthless without a LIVENESS line.** `--vmdiff` used to take ONE HELD button word
   (`CT_BTN`), and a held A dismisses nothing — the ROM's box machine advances on an EDGE. The scene froze
   on its first textbox and 3000 frames of `CLEAN` compared two stopped machines: `oracle ip-steps=12`
   over 1200 frames. `VMDIFF_BTNSCRIPT` gives the mode a real per-frame drive script, and every run now
   prints `liveness: oracle ip-steps=N ours=M`, with an explicit "NOTHING RAN" when N is 0. **Pin the
   liveness line BESIDE the CLEAN line, never instead of it.**
2. **A lockstep whose verdict depends on the PHASE of its own input is not a lockstep.** The first version
   of this check was green — and shifting its drive script by ONE frame turned 4 of 8 phases red, always
   in the same direction, while the liveness line stayed byte-identical in all eight. Two causes: the port
   modelled the unpark as a fixed 12-tick countdown (the resume is really "the first scheduler visit with
   `$0129 == 0`", which lands at press+11..+15 depending on the object's visit lattice), and the harness
   keyed its dismiss on OUR injected press, which reaches the ROM a frame later than it reaches us (the
   joypad auto-read latches at vblank). **`-phase1` is the same run shifted one frame**, and it exists
   because a single phase cannot grade a phase-invariance.
3. **Put each mutation on the check that can see it.** The arm-frame mutation (the close ramp starting one
   frame early) is INVISIBLE at shift 0 and a 4-frame divergence at shift 1, so it belongs on `-phase1`.
   The op-0xFF mutation diverges at f435, so `-unpark`'s window had to grow 400 -> 460 to grade it — 460
   being the largest window that is still CLEAN on this fixture. A mutation left on a check whose window
   or phase cannot reach it reports SURVIVED and reads exactly like a blind check.

### The door and chest SOUNDS, folded into the checks that already prove the open

`field-scene4-door-opens-on-step` and `field-chest-opens-item` each gained one `CT_FIELDSFX` line
rather than getting a check of their own. That is deliberate: a sound whose only reason to exist is an
event should be pinned ON the check that proves the event, or the two can drift apart silently.

What the two lines encode is the DERIVATION, not just "a sound happened" -- the conditionalities are
opposite and each check's drive discriminates its own. The door's `JSR C0/1B90` is straight-line inside
each of the five handlers, so it follows the HANDLER: the scene-4 drive raises the request ten times
(ten `[door]` lines, one `opened=1`) and posts exactly ONE sound. The chest's `JSR C0/1BA7` sits BEFORE
the metatile test, so it follows the REQUEST -- and `field-chest-already-open-is-empty`, which presses A
three times, still posts once, because the treasure probe stops raising the request.

Mutations, both verified: pan `$0A00 + $0197` -> a constant `0x80` turns BOTH checks red; deleting the
door post turns the door check red and leaves the chest one green.

⚠ These grade STAGING. Measured, not assumed: the checks run `SDL_AUDIODRIVER=dummy`, so
`SDL_OpenAudioDevice` never opens and no `CtAudio` exists -- with `CT_AUDIO_FORCE=1` +
`CT_AUDIO_VOICETRACE=1`, `CtAudioVoicesSounding` reads 0 for all 90 frames of a scene-2 run, i.e. for
the scene's own MUSIC too. Output grading needs an environment with a real audio device; it is not a
property of these two posts.

### menu-item-descriptions-from-rom (a change that turned NO check red)

`ct_field --assets ct_assets.dat --desctest` -> `desctest: C8 -> "Recovers HP/MP at Save Pts."` etc.

This check exists for a reason worth repeating: the menu's item and equipment DESCRIPTION lines were
being SYNTHESISED (`"Restores %d HP"` from the effect record, `"Power +2"` from a bonus mask) where the
ROM stores them at `$CC:2EB1`, and replacing the whole thing with the real strings **turned no existing
check red**. The item/equip checks pin trace lines, MP caps and SFX posts; none of them renders a
description. A swap from fabricated text to real text that nothing notices is the definition of an
ungraded change, so the probe was written before the commit, not after.

The eleven ids are chosen to span every arm the synthesiser got wrong -- one it happened to match (BD,
also the entry a previous session confirmed against the ORACLE, which cross-checks the text decoder),
three it had no effect-kind for and printed nothing (C6/C7/C8), a tab whose ROM form carries QUOTES
around the stat word (CD), two whose vocabulary AND amount it invented (96 "Strike +2", 98 "Vigor +2"),
an id whose ROM string is EMPTY where it printed text anyway (B6), and the table's longest string (74),
which the old 24-byte caller buffers would have truncated. The trailing line
`table 242 entries, 105 non-empty` is the anti-vacuity half.

It calls `CtMenuEquipDesc` itself rather than reading the baked array, so it grades the code path.
Mutation: returning a fixed `"Power +1"` from that function takes it red.

Re-pin only after re-deriving from C2/F2F3 -- and note that routine needs M=8/X=16 to disassemble at
all; with 8-bit index registers it desyncs two instructions in and the `LDX #2EB1` disappears.

### field-menu-equip-gearpanel-shows-its-list / field-menu-equip-list-window-rises

`ct_field --scene 8 --play "..."` with `CT_EQUIPDEMO=1 CT_MENUGEOM=1` (+ `CT_MENUGEOM_AT`) ->
`menuequipgeom: sub=N slot=0 cand=3 rules=... rows=...`.

These pin the EQUIP screen's two window LAYOUTS, which the port had collapsed into one: it reused the
gear-panel chrome for the candidate list, so the first two candidate rows were drawn inside the
slot-type ("Weapon") box and the rest spilled over its bottom edge, and the gear panel showed no list
at all. The ROM's substate 3 runs `$FF:BEA4`/`$FF:BEEC` through `C2/ED31`, whose `02` BLOCK COPY moves
the item-list box up four tile rows (32 px) and restores the description bar from the off-screen row-32
backup (disasm/notes/menu2_equip_change.md F).

The signature GRADES THE COMPOSED PIXELS, not the constants: `rules` = the horizontal chrome edges in
x118..132 (a column no item name or count reaches) and `rows` = the first scanline of every text band
in the left box. Both were measured on the ORACLE's own renders of `saves/eq_spare.state`
(`ct.exe --peek ... --btnscript "0:20,200:2,0:60,100:2,0:40,100:2,0:60"`, plus one more `100:2,0:60`
for substate 3) and cross-checked against the user capture `divergence_equip.state` (600 A.D.,
accessory row, one candidate) -- all three profile identically, because the scanned column is
data-independent. Five mutations, all killed: dropping the 32 px, not drawing the gear panel's list,
reusing the gear chrome, not shortening the right box (EXP/NEXT reappear at y184/192), and rising the
substate-3 rows.

Re-pin only after re-measuring the oracle renders -- these are oracle numbers, not ours.

### field-party-slot-table (a PC is named by SLOT, and what to do when nothing observable changes)

`CT_SLOTTRACE=1 CT_CARRYPARTY=0,1,2 CT_CALLFN="1:6:3:10,..." ct_field --scene 21 --frames 160`
-> `slotop: op=A9 n=04 -> $019B=06 obj=3`.

Second slice of the same delegation as `field-item-op-live-vm`. Ops 0x22/0x24/0xA9/0x95/0xB6 name a PC
by PARTY SLOT through `$0197/$0199/$019B` (`LDA $97,X`, C0/65F4 / 6628 / 560E), and the live VM resolved
them as `operand + playerObj`. The operand is the ALREADY-DOUBLED slot index -- measured, it is only ever
0, 2 or 4 -- so the paraphrase was off by a factor of two on all 40 non-zero-operand sites.

**This check exists in the shape it does because the behaviour is NOT observable, and that was measured
rather than assumed.** The party spawns stacked (scene 21 with a three-member party puts objects 1, 2 and
3 on the same tile), `--scene` skips the entry settle so no walkscript spreads them, and the objects the
paraphrase picks instead are unplaced ones that the ROM's own `$1100` bit-7 gate rejects. So the facing
op 0xA9 produces is identical either way. Per `grade-invariant-when-no-behavior`: when nothing moves a
behavioural signature, pin the INVARIANT plus a liveness line -- here the resolution trace itself, which
`CT_SLOTTRACE` prints deduped so it is short and stable.

Two-level grading, both verified: the RESOLVER is `move.c`'s `ctVmPartySlotTarget`, covered by
`sweepops-player-script-opcodes` at 60 micro-executed cases each for 0x22/0x24/0xA9 -- mutating its stride
gives **12251/12600 (349 mismatch)**. The WIRING is this check -- reverting op 0xA9 to `operand + playerObj`
makes BOTH `op=A9` lines vanish. Neither mutation survives.

`CT_CALLFN` is what makes the site reachable at all: every non-zero-operand site lives in an fn slot 3..15,
and before it no headless driver could enter one.

### field-party-gate-live-vm (the $C2 family, and a census that chose the fixture)

`CT_BAGDUMP=2 CT_ITEMDEMO=1 CT_ACTIVATE=19:5 ct_field --scene 71 --frames 90`
-> `party: 000180 res=808080808080 0:70/70,8/8 1:65/65,12/12  partyops=1`, where the control run
without CT_ACTIVATE prints the wounded pair and `partyops=0`.

Third slice of the delegation. Seven of the ten $C2 opcodes (0xCF/0xD0/0xD1/0xD7/0xF8/0xF9/0xFA, 153
sites) had NO arm in the live VM while --sweepops reported all ten byte-exact -- because it grades
move.c's harness-only runner. The BODY is now shared (move.c CtVmPartyGateFamily, 600 micro-executed
cases); this check grades the four-line wiring that reaches it. Mutation: restoring the old
advance-the-IP-and-do-nothing turns it red.

**The fixture was chosen by census, and that is the part worth copying.** The obvious fixture is
Crono's bed -- and it does not work: scene 2 obj11 slot1 parks forever on the op-0xC0 DECISION BOX two
instructions before its 0xF8 (traced: `[run obj11] @0571 op=C0 p1` repeating), and neither
`TB_AUTOADV=1` nor a `--play` A-press sequence dismisses it. Rather than guess at another scene, a
walk of all 504 scenes looked for family ops with NO box (0xBB/0xC0-0xC4/0xC8) in front of them on the
path from an fn entry: 244 (scene, object, slot) entries qualify, and scene 71 obj19 is the cleanest
heal among them. That same census is the honest reason this check grades ONE of the seven opcodes --
the rest are only reachable through dialogue a headless driver cannot answer.

### field-item-op-live-vm (the $C1 item family, and grading the runner the GAME executes)

`CT_BAGDUMP=4 CT_ACTIVATE="15:5" ct_field --scene 439 --frames 60` -> `bag: D6:01 ... gold=400 famops=1`.

This one exists for a structural reason, not a scene-specific one. The tree has TWO field-VM opcode
implementations (`disasm/notes/vm_two_implementations.md`) and the dozen `--sweep*` checks all grade
`move.c`'s `CtFieldRunnerOne`, which **no player ever executes**. The seven `$C1` opcodes
(0xC7/0xC9/0xCA/0xCB items, 0xCC/0xCD/0xCE gold -- 375 static sites over 105 of the 504 shipped
scenes) were the sharpest case: four of them had NO arm in the live `ct_vm.c` runner at all, so every
"you got an item" in the game advanced the IP and did nothing, and `--sweepops` was a perfect 12600/12600
throughout. **A sweep score on a runner nothing calls cannot see that.**

What it pins: scene 439 obj15 slot1 is the fair pendant pickup, script `CA D6` at body `0486`
(C0/37B1 -> leaf 1 = C1/D005), so the activate must leave `$7E:2400=D6 / $7E:2500=01` -- the same pair
the real capture `two_member.state` carries. `famops` is the anti-vacuity half: the identical command
WITHOUT `CT_ACTIVATE` prints an empty bag and `famops=0`, so the check cannot rot green by ceasing to
reach the opcode. `gold=400` is the `fieldLoad` purse seed, not a script effect -- it pins that AddItem
leaves `$2C53` alone.

Mutation, verified in both directions: mapping op 0xCA to leaf 2 instead of 1 takes
`sweepops-player-script-opcodes` to **12490/12600 (110 mismatch)** AND empties this bag. Re-pin only
after re-deriving from `C0/37B1` and `$C1:D126`; a changed item id is a red flag, not a baseline drift.

### title-intro-swing-vs-oracle / title-intro-logo-vs-oracle (the whole title animation)

`python tools/title_intro_cmp.py <swing frame> ...` runs the engine to a frame of the title
sequence, runs the ORACLE to the boot frame that swing frame IS (k + 141), and diffs all 57344
pixels of the composed 256x224 frame. It is graded against the cartridge, not against itself -- a
hash of our own output only grades our agreement with ourselves (memory: check-vs-own-reference).

The two checks split the sequence by cost, not by kind. `title-intro-swing-vs-oracle` (quick, ~18 s)
takes frames 0/10/100/150/400/500/588/620/745: the mode-7 pendulum, the copyright block, the clock
spiralling out of the bob and the first mode-2 frame. `title-intro-logo-vs-oracle` (full, ~46 s)
takes 800/1040/1290/1500/1989: the letter slide, the TRIGGER logo scrolled in with the clock hands
in their yellow flash, the offset-per-tile ripple damping out, and the settled title -- the frame
the port used to BAKE (`src/data/title_screen.h`, deleted) and now computes.

**Frame choice is load-bearing here, and one round of mutation testing proved it.** The first
version of the swing check used 0/100/150/300/500/588 and its `$F8`-carry mutation SURVIVED: the
carry inside `$7E:31BF` moves the matrix by one unit, which at those frames moves no pixel at all.
At frame 10 the same mutation is 9939 pixels and at 400 it is 1151, so those two replaced 300. Two
other frames are in the list for their own reason: 150 is where the swing moves fastest and is the
only frame that notices a one-frame slip in the matrix, and 745 is the frame `$2105` becomes 2.
Re-pinning is legitimate only after a render change verified against oracle framebuffers; a changed
pixel count is a red flag, not baseline drift.

A fourth, **title-sequence-sound-reaches-driver** (quick, ~2 s), grades the sequence's MUSIC end to
end: the setup's own song load (`$7E:5AA5 LDA #$30 / JSR $375F`), then `$7E:34AB` staging
`$1E00=$81 $40 $00` and, fifteen frames later, `$7E:34D5` staging `$1E00=$F0` alone -- all through
the real `$7E:1E00` mailbox and the real emulated Akao driver. `posted -> $1E00=00` is the
anti-vacuity half: the driver clears the command byte only when it has actually read it, so the check
cannot pass by staging into a mailbox nobody services.

The check also pins a COUNT -- `title-sound: 3 post(s), $2143 wait 6 frame(s)` -- because an extra
`staged` line would not fail a substring match. Three posts is the invariant: the setup's song,
`$34AB`'s fade and `$34D5`'s stop. The `6` is the setup's own spin (`$7E:5AAA LDA $2143 / AND #$0F /
BEQ`) and is **measured by the port's own emulated driver, not baked** -- the oracle spins boot
133..139, just over six frames, and the port's SPC answers on the same schedule.

⚠ **All four bytes are pinned, `$1E03` included -- and an earlier version of this check deliberately
did NOT pin it, for a reason that was wrong twice over.** The note then read: "`$1E03` is persistent
RAM neither routine writes, the ROM leaves `$06` and this port leaves `$05`, and pinning it would pin
the song loader's unmodelled record write-back." The write-back (`$C7:0A39`) had been in
`src/audio.c` all along, and the `$05` was **song 0's** record byte -- the port had simply never
played the title's song, so it was still carrying the field's. The lesson is the general one: when a
value is wrong, find who wrote it LAST before blaming the routine that would have written it.

A third check, **title-intro-machine-vs-oracle-wram** (quick, ~18 s), grades the machine's CELLS
instead of its pixels: `tools/ti_sim.py` runs the model against a `--monitor` stream of the oracle's
live WRAM in the three ways the sequence can end -- the script's own handover, a button press, and
the same press with L held (which still fades but leaves `$7F0060` at zero). 94105 values, 0
mismatches. It exists because the pixel checks drive no buttons: without it the START, the L/R
suppression and `$7E:34C2`'s fade have no cell-level pin at all. ⚠ It grades the DERIVATION, not the
shipped C -- the pixel checks and `boot-natural-blank-cart`'s fade line do that.

Nine mutations across the four, all KILLED: the 1-based scanline, the `$F8` carry and `C3/0D5E`'s
size bit (the copyright block goes 8x8) on the swing; the offset-per-tile "this is BG1" bit, the
letter-slide clamp, the palette job's final restore-the-base-palette copy and the brightness latch on
the logo; `BIT #$30`, `$39B1`'s replace-in-my-own-slot and `$34AB`'s `$1E01` parameter on the WRAM check;
and, on the sound check, dropping `$34D5`'s command and dropping `$34AB`'s `JSL $C7:0004`.

Five more were WRITTEN, SURVIVED, and were then deleted rather than kept as decoration: the sine
routine's `CPX #$0200` carry (the spiral's angle never reaches `$100`, so it is always 0), the PPU's
per-line sprite limits (20 is the most any scanline of this sequence carries), the `largeField` clip,
the fade's step period `$038E` (which is 1), and `$375F`'s `STA $0389` (the port SEEDS `$0389` from a
snapshot taken after the setup, so C3/09A4's watcher cannot be made to fire here). All five stay in
the code because that is what the hardware and the ROM do; none of them is pinned, because a mutation
nothing can kill is not a grade. The notes on the checks say so.

### seq-credits-* (sequence-table ENTRY 1, and grading a stage the game only reaches at f2788)

The `$C3` sequence table has sixteen entries and the title is one of them (`disasm/notes/
sequence_table.md`). Entry 1, the staff credits, is the second one ported, and it needed a different
kind of harness: **the oracle cannot be driven to it.** The stage is reached only from inside the
attract demo, thousands of frames into a plain boot, so there is no savestate to start from and no
button script that gets there.

So the reference is a plain boot run to a frame that happens to be inside the page, and
`tools/seq_stage_cmp.py` **finds its own alignment** rather than counting frames by hand: a probe run
reads the overlay's own 16-bit frame counter `$7E:0008` out of the `.wram` sidecar and derives
`setup = probe - $0008`, after checking that the probe really landed on this entry (`$0384`) and this
page (`$0382`). If the boot's timing ever moves, the probe moves with it. The engine side is
`ct_field.exe --seqstage <entry> <frames> [page]`, which runs one entry standalone -- the common init,
the setup, then that many NMIs -- and dumps the composed frame to `CT_SEQSHOT`.

Four checks, split by cost:

* **seq-credits-timeline** (quick, ~0 s) needs no oracle at all. `--seqtimeline 1 6` opens the stage
  six times and prints each visit's clock: `fadein=15 hold=90 teardown=15 total=105`. Those three
  numbers are the task list's own -- `$347A` stepping `$038D` to `$0F`, `$6BE5` comparing the NMI's
  sixteen-bit counter against `#$005A`, `$34E3` walking back down to `INC $00` -- and they match the
  oracle's own f2803 / f2878 / f2893. The **sixth** line is the point of the sixth visit: `$0382` is
  not in the common init's clear list, so the page wraps 5 -> 1.
* **seq-credits-vs-oracle** (full, ~100 s) is the pixel truth: page 1 at frames 1/5/15/40/89/95/104 --
  both fades, the frame each ends, the hold and its last frame -- all 0 px of 57344.
* **seq-credits-oam-vs-oracle** (quick, ~0 s) grades the OAM the stage BUILDS, as bytes, against
  the oracle's own `.oam` sidecar: an fnv32 over the 544 bytes the PPU keeps, plus the count of
  sprites that are not parked. ⚠ The pinned digest is the **cartridge's**, recomputed from
  `o[0:512] + o[0x400:0x420]` of a `--peek` capture -- a hash of our own output would only grade our
  agreement with ourselves.
* **seq-credits-pages-vs-oracle** (full, ~70 s) is page **5**, eight thousand frames into the boot.
  It grades what page 1 cannot: that `$0382` really survives the overlay's reboot, that the fifth
  pair of pointers is read, and that the font is a boot asset rather than something a page builds.
  ⚠ Pages 2-4 were checked the same way by hand and are deliberately NOT pinned -- each costs its own
  multi-thousand-frame oracle boot, and page 5 can only be right if all four before it were.

Twelve mutations across the four, all KILLED: the fade-in's `$0F` target, the hold's `#$005A`,
`$39B1` vs `$39A7` (replace-in-my-slot vs delete-me -- deleting drops the teardown and the stage
never ends) and the page wrap on the timeline; the tilemap's `- $40` bias, the OAM's `- $21` bias,
the sprite pen's five-pixel pitch and mode 0's 2bpp depth on the pixel check; the `$E0` park, the
high table's fill and writing to the `$7E:0700` shadow instead of real OAM on the byte check; and
`$0382` cleared with the rest of the common init on the pages check.

⭐ **The OAM check exists because a mutation SURVIVED.** `$7E:6B04` fills the low OAM table with the
fixed `$E0` at `$7E:4565`, which parks an unused sprite past the bottom of a 224-line screen. Zeroing
it moves all 112 unused sprites to (0,0) and changes **not one pixel** -- a parked sprite draws
through CGRAM entries this stage never defines. The credits write only colours 0-3 and 128-131, and
the rest of the `$7E:2200` shadow is leftover from whatever ran before; **219 of 256 entries differ
between the page-1 and page-2 captures**, so it is not stable state and seeding it would be both
invention and wrong. The bytes can be graded where the pixels cannot, so the mutation moved onto a
check that can kill it instead of being deleted as ungradeable.

⚠ **Porting this entry changed the TITLE's render, and that is the interesting part.** `$2107`,
`$2108`, `$210B`, `$2101` and `$2131` used to be constants inside `tiBgPixel`/`tiEvalSprites`/the
colour-math table -- the title's own values, baked. They are registers, and the two stages disagree
about every one of them, so they now come off the machine and mode 0's 2bpp fetch and layer order sit
next to mode 2's and mode 7's, transliterated from `snes/ppu.c`. The title's nine pinned frames stayed
at 0 px through the change, which is the only reason to believe the generalisation was neutral.

## Mutation testing — proving a check GRADES something

A green check proves nothing until you have seen it go red for the right reason. That step used to
be a manual ritual done from memory; it is now data, declared next to the check it is supposed to
falsify:

```json
"mutations": [
  { "why": "the same-world skip comes back at BOTH overworld-entry sites",
    "edits": [ { "file": "src/field.c", "find": "<exact substring>", "replace": "<substring>" },
               { "file": "src/field.c", "find": "<the other site>",  "replace": "<substring>" } ] }
]
```

`python tools/regress.py --mutate NAME` (or `--mutate-all`) runs the baseline, then for each
mutation applies every edit, rebuilds, re-runs the check, and **requires it to FAIL**. The source is
always restored and rebuilt (`finally`). Exit status is 1 if any mutation SURVIVED.

- **KILLED** — the check noticed. It also prints which `expect` line went missing, which is usually
  the fastest description of what the check actually grades.
- **SURVIVED** — the check does not grade that line, whatever its notes claim. Fix the check, not
  the report.
- **STALE** — the `find` text no longer matches exactly once. The mutation drifted from the source;
  re-derive it. (Treated as a survivor: an unrunnable proof is not a proof.)

A single `find`/`replace` pair is sugar for the one-site case. Use `edits` when one behaviour lives
at more than one site — the first full `--mutate-all` run caught exactly that: a mutation patching
only the door-exit overworld entry SURVIVED because the scenario enters through the other site.

**It has already caught a false check.** `field-ow-reentry-reloads-tilemap` passed, and its
mutation survived, because `expect` is a SUBSTRING match: the probe printed one unnumbered
`ENTRY tilemap ... fnv=` line per overworld entry, so the expectation was satisfied by the FIRST
(pristine) entry while the second was corrupt. The probe now numbers entries (`ENTRY#1`, `ENTRY#2`)
and the check requires both. Hand-verification had "confirmed" that check two hours earlier by
eyeballing the output — the harness disagreed and was right. **When a mutation survives, believe the
harness over your memory of having checked.**

## Event-anchored snapshots — `CT_BATTLESHOT_ON` instead of `CT_BATTLESHOT_AT`

A snapshot check has to name a MOMENT. Naming it by frame number (`CT_BATTLESHOT_AT=272`)
pins the moment to a timeline that legitimate work moves, and the check does not fail when
that happens — it keeps passing on whatever the frame now contains. That is the worst
failure mode a check has: green, and grading something other than its own name.

`CT_BATTLESHOT_ON=<event>[#N][+F]` takes the shot `F` frames after the `N`th occurrence of
a named battle event, so the moment travels with the fight. The vocabulary is `battleTrace`'s
own — `strike`, `death`, `act-end`, `enemy-arrive`, `enemy-think`, `victory`, … — and it is
recorded whether or not `CT_BATTLE_TRACE` is set. Defaults: `#1`, `+0`.

**Pin this line, and only this line:**

    battleshot: on enemy-arrive#1+13 fnv=B681508E

It carries the moment's identity and its render, and no frame number. The human-readable
`anchor '…' fired f293, shooting f306` and `f306 fnv=…` lines also print — do NOT pin them.
Pinning a frame there puts the clock straight back into the expectation and hands back the
brittleness the anchor exists to remove: a legitimate re-time would fail the check while the
moment stayed exactly right. What must not change is WHICH moment is graded and WHAT it looks
like. An anchor that never fires is loud on its own (`NEVER FIRED — saw N occurrence(s) in M
frames`), so a silent no-shot is not a way to pass.

### Converting one

Run the scenario with `CT_BATTLE_TRACE=1`, find the event the moment actually belongs to,
measure the offset, re-pin, and **mutate** — the conversion is not done until a mutation of
the mechanism the check is named for kills it. Three of the four checks `cbdd2e4` flagged
were converted this way, and every one of them taught the same lesson twice:

- **`field-battle-hetake-attack-pose`** was `_AT=272`, named for the attack-pose ladder.
  This session's damage and AI fixes moved the fight ~20 frames and f272 landed *before the
  run's first enemy event* (`enemy-noop` f285 … `enemy-arrive` f293) — before the attack
  began. Green the whole time. Now `enemy-arrive+13`.
- **`field-battle-dissolve-serial`** was `_AT=790`, named for one corpse dithering while the
  next waits. The Cyclone strike is f279 and a corpse takes ~41 frames, so both were long
  gone: it was grading a static cleared frame ~430 frames past its subject. The PARALLEL
  mutation — the user's actual bug — sailed through it.
- **`field-battle-forest-lastkill-dissolve`** was `_AT=1118`, which is after `victory` (f935)
  and inside the results screen.

**Deaths do not land on the strike.** The first replacement anchors used `strike+K` and were
also wrong: victims die at ACT-END (Cyclone strike f279, both deaths f563). `strike#3+18` for
the last-kill check landed 65 frames before the fight-ender died — and still *killed* its
mutation, for the wrong reason: the two earlier corpses stand there under it. A kill is not a
pass mark; check WHICH line went missing and why. Both now anchor on `death#N`, added for this.

**One event slot cannot hold a frame.** `death`-anchored shots first reported `saw 0
occurrences` while the trace printed both deaths: a Cyclone double-kill emits `death`,
`death`, `act-end` on the same frame, and polling a most-recent-event slot once per frame only
ever sees `act-end`. `battleTrace` now counts matches against a watched name as they fire, so
`#N` indexes occurrences, not frames.

### When NOT to anchor

The fourth flagged check, the `field-battle-lowhp-blink-grey`/`-white` pair, stays on `_AT` on
purpose. Its moment is a phase of a blink keyed to the absolute frame counter (`(frameCtr/12)&1`);
no event expresses it, and anchoring would fix the offset from the event and let the parity fall
wherever a re-timed fight put it — a fake anchor that reads as rigorous and grades a coin flip.
An honest frame number beats that. It was mutation-tested rather than assumed sound: killing the
blink kills `-grey`, forcing constant grey kills `-white`, so both frames still sit on live,
opposite phases.

### slot1-runner-battle-menu-vs-oracle (`python tools/slot1_runner_cmp.py`)

C0/595C runs obj0's slot-1 function to its terminator after every rebuild of the `$7F:2000` script
image: the location load, the menu exit (C0/0206) and the battle return (C0/0330)
(disasm/notes/obj0_slot1_runner.md). The tool runs both sides live for three scenarios — the
attract's loc 0AD battle, the loc 082 two-Diablos battle, the loc 082 X/B menu — and grades the
cartridge's `--lifetrace` (C0/5977 dispatches) against the port's `CT_SLOT1TRACE`: the op trail and
where it stops, obj0's IP/priority untouched, and for the battles the mailbox from the first
C0/1BE6 driver-mode post through the first post after the runner (`70 00`, `10 18`, `18 BA` at 0AD).
Frames are NOT compared: the cartridge spends ~13-19 frames in the reload chain, the port none.
Seeds: `saves/attract_loc0AD_flea.state`, `saves/battle_re/party3_field.{state,lakewram}`,
`saves/grupo_3_mapa.state`, `saves/battle_re/party3_form.raw.wram`. Re-pin only if the oracle side
itself changes (a new trail means the cartridge ran different ops — find out why first).

### attract-battle-rng-stream-vs-oracle (`python tools/attract_battle_rng_cmp.py`)

The attract's three demo battles (0AD, 07D, 087) share ONE battle RNG stream: C1/800A seeds `$26` from `$29AD`,
the boot's two shuffles and the AI init (C1/80E8 -> C1/8C09) draw from it, and C1/8443 hands the final `$26` back.
A draw one fight misses rotates the next fight's turn order. The tool runs both sides live -- the port's attract
(`--scene 433 --selftest`, `CT_BATTLE_MEMMON` + `CT_BATTLE_TRACE`) and the cartridge's `--monitor
7EB163:11,7EB185:1,7E29AD:1 --writer` from `saves/attract_cena_inicial.state` -- and grades per fight the turn order
`$B163`, the first cursor pass's `$B185` (C1:82EE) and the `$29AD` handback (C1:8448): nine values. Frames are NOT
compared. Its three mutations are the three mechanisms it pins (disasm/notes/battle_boot_turns.md §7): the boot
eval drawing before the demo bit comes back, op 04 (C1/98C5), and C1/FD5E's multiplies leaving `$2E` = 0 for the
first shuffle divide. Re-pin never: the expected values are the cartridge's.

### attract-07d-playanim-list-vs-logical-oracle (`python tools/attract_playanim_list_cmp.py`)

The 07D's Frog Squash (tech $28) plays its hurt anims with `06 05` / `06 03` on track 3 (the primary) and track 4
(the `$A018` list). C1/4D08 arms EVERY member of that list on track 4 (C1/4D79); the port armed `$A018[0]` alone,
so two enemies flinched and three stood idle (disasm/notes/battle_playanim_list.md). The tool runs both sides live:
the port's attract (`--scene 433 --selftest`, `CT_BATTLE_ASCTRACE`: `ascposearm` / `ascpose` of the `sel=28`
action) and the cartridge on `CT_ORACLE_LOGICAL=1` (the effect load makes real-time lag frames) with `--monitor
7EA124:11,7E9907:11,7EA13A:11` from `saves/attract_07d_battle_f4540.state`. The frame counters are tied at the
action's first arm (Frog's anim 10), then it grades every arm (frame, combatants, mode -- on the cartridge `$A124`
:= mode and `$A13A` := FF) and the published pose `$9907[0..7]` on every frame to the action's end. Its mutation
is the old single-combatant arm. The 03/26 wait-on-the-whole-list path is NOT graded (tech $28 plays only 06 on
track 4). Re-pin only if the action's length changes with an oracle-verified reason; the counts are the cartridge's.

### attract-07d-enemy-staging-vs-oracle / vmdiff-07d-field-sorcerer-placement / field-07d-obj8-spawns-on-lead-vs-oracle

The 07D's field places its enemies and its sprite-133 object with op 0x8C (C0/4BC3: the TILE is the low byte of a
scene var, `$1800` is kept) out of vars that op 0x21/0x22 fill with an object's tile bytes, and a PC's load op
writes the party table `$0197` itself (C0/42B3). disasm/notes/field_op8c_place_from_vars.md. The first check runs
the attract and the cartridge (`--monitor 7E29BF:60` from `saves/attract_cena_inicial.state`) and compares the five
staged enemy records byte for byte; the second is the field lockstep from `saves/attract_07d_field_f4320.state`
(PEEK_SAVE of the attract at f4320), whose first divergence is now f81 -- the party's op 0xD9 walk, an OPEN
residual (the port's walk ends 8 frames early); a LATER first divergence after a D9 port is an improvement to
re-pin. The third peeks the port's WRAM just after the 07D load (selftest f5830) for obj8 and `$0197`, the
cartridge's load values. Re-pin none of them on the port's word: the values are the cartridge's.

### vmdiff-gato-d9-release-vs-oracle / vmdiff-forest-ambush-d9-vs-oracle / attract-demo-battle-midwalk-seed-stages-vs-oracle

Op 0xD9 (C0/326C) releases the party onto the walk programs C0/5CEC copies to `$7F3720/3740/3760`, and the scheduler
queue (C0/59D9) runs the lead first (disasm/notes/field_d9_party_walk.md). The two locksteps run the GATO approach
(`saves/gato_t0.state`, first divergence f144 = the battle boot) and the forest ambush
(`saves/battle_re/forest_wait_ambush_active.state`, f282) through the walk; before the port they diverged at f41 and
f247, on the release. The third seeds the attract's demo battle from the cartridge's WRAM taken mid-walk and pins the
staged party records (`$29B0`), the cartridge's bytes. A LATER first divergence is an improvement to re-pin; an earlier
one is a regression.

### field-forest-ambush-stages-vs-oracle

The forest ambush lockstep above stops at f282, which is `--vmdiff`'s reach: op D8 runs at f279 on both sides, the
battle consumer (C0/1770) arms at f280 and the cartridge's objects stand still, and `ct.exe` links no field.c. This
check continues in the real host: `ct_field --scene 19 --loadwram saves/battle_re/forest_wait_ambush_active.lakewram`
(the state's own WRAM) with `CT_FIELD_MEMMON`, pinning the staged interface at the port's f281 (the end of its frame =
the cartridge's first act of f282): `$29B0`, the eight `$29BF` records, `$019D`, `$0102` -- the logical oracle's bytes.
Its mutation stages a frame later. Re-pin never: the values are the cartridge's.

### battle-hud-strip-command-windows-vs-oracle (`python tools/battle_hud_strip_cmp.py --suite`) / battle-hud-live-forest-ambush-vs-oracle

The battle HUD strip is two tilemaps the ROM rebuilds from the command queue `$A6D9`: BG1's frame rows `$0B40` (the
base `$D1:5800` + C1/081E's frame per enqueued seat) and BG3's text `$0CC0` (the name list C1/04DC under C1/0785's
command windows, the hand at 6 x `$A6DD`) -- disasm/notes/battle_hud_command_windows.md. The first check renders the
port's strip from the cartridge's own WRAM (`CT_BATTLE_RENDERWRAM`) in seven queue states and compares it with the
cartridge's picture of the same frame, pixel for pixel over the 48 strip lines. Residual cells at cols 26..29 are
running ATB gauges (the cartridge's picture is its last HUD rebuild, a few frames old); bat9-frog-active's cols 20..21
are the HP-column reflow O3. The second runs the live host from the forest ambush and pins, with `CT_BATTLE_MEMMON`,
the boot's clear of a stale band mode (D1/F2AC `STZ $CA1E`), the band's open, and the frame row `$0B80` the port writes
for Crono and for Crono + Marle -- the cartridge's `$0B80` at f440 and f515. Re-pin only with an oracle-verified reason;
a change in the residual cells means the ATB gauge moved, not the windows.

### sweep-bganim-machines / field-bg-seed-{tent-keeper,party3,loc13B-colour-cycle}-vs-oracle (the field's background)

The field BG as the ROM draws it (disasm/notes/field_bg_anim.md): the PPU's colour math in 5 bits, the animated tiles
(FD/DE98 + FD/E022, from the field's IRQ), the colour animation (FD/E292 + FD/E39C modes 1/2/3/8, the live page $7E:2200
the NMI uploads) and a seed's own map ($7E:3000). `sweep-bganim-machines` (`ct.exe --sweepbganim`) micro-executes the
three routines against the port -- the two loaders for all 512 locations, the runner on 3000 seeded states (VRAM
compared whole), the record service on 6000 -- and pins the byte-exact counts with their liveness. The two seed checks
run `tools/menu_audit_cmp.py`'s no-input drives `tkfield` / `p3field` against the LOGICAL oracle for 90 frames, P+2:
the tent keeper's shadows (BG2 half-added over BG3) and waving flags, party3's torch glow (BG3 added over BG1/BG2), its
open door; `field-bg-seed-loc13B-colour-cycle-vs-oracle` (drive `c315field`) the colour animation in the picture --
loc 13B's orange circuits, four `8x` records through the live page. Its fixture is the ROM's own load
(`ct.exe --teleport 315 2 8 0`, saves/field_re/loc13B_teleport_lake.state + its WRAM as the seed), because party3's
torch records colour nothing on screen (turning the colour machine off changes no pixel there). PINNED RESIDUALS, both sprite-side and older than this work (the HEAD binary shows the
same frames): the tent keeper's bottom-left NPC group blinks on f1/f9/f10/f30/f34/f70, and one pixel of Crono at
(68,110) on party3 and (42,130) (thumbnail) on loc 13B. A count that grows is the background; the full-resolution peeks (f2, f45) are the exact grade.

THE SAME CHANGE RE-PINNED 28 CANARIES (2026-09-24), each attributed by diffing the HEAD binary's frame against the new
one: pure rounding-size deltas (<= 8 per channel: every shadow, light and glow the 5-bit math moves) in render-scene1/5,
the 16E parallax, op41/42, scenes 2/129/200/434; tile and colour animation frames (the seed's machine, or the ROM's from a
cold `--scene` load) in scenes 5/6/26/315/439 and the battle arena; three are oracle-graded and IMPROVED:
attract-fade-rate-01B 51827 -> 53248/53248, vmdiff-wmask-party3 / -framelegs-party3 seven more modelled bytes (the
torches' colour words) with the divergence list unchanged. menu-x-refused-off-the-field-main-loop now reads $0110 = 01
at both presses, the cartridge's value (the init pass runs obj0 fn0's `4A 10 01 00 01`); see its notes.

### sweep-door-list-routines / field-door-list-vs-logical-oracle (the opened-door list, $7F:1CC8)

disasm/notes/field_door_list.md. `sweep-door-list-routines` (`ct.exe --sweepdoorlist`) micro-executes C0/28E1 (the
re-apply) and C0/28C0 (the append) against ct_vm.c over the whole 128 KB, and a door request through C0/0C76 (append +
the five handlers' `$7E:3000` bumps) on the list and the map span, with every arm, the append's three exits, the E6
handler's 16-bit carry and the bank-`$7F` wrap counted in its liveness line. `field-door-list-vs-logical-oracle`
(`python tools/door_list_cmp.py`) runs three drives on both sides and compares the list and the `$2935` door's six
cells EVERY frame: `reload` (party3_field's open door across X/B), `walk` (the door closed and the list emptied by
PEEK_POKE, opened in the drive, then X/B), `load` (a poked list across the scene-468 stairs' location loads). Its
differing runs are PINNED RESIDUALS of the port's frame model, each named in the check's notes (the menu program's own
use of `$7E:3000`, the reload's and the load's NMI-off frames, the door request's pre-existing two-frame lead); the end
lines are the exact grade. Re-pin only when one of those frame models is ported -- and then the end lines must still
read `same`. The drive `doorreload` of tools/menu_audit_cmp.py (`--ram 11CC8:32,5735:2,5835:2,5935:2`) is the same
reload through the menu audit's thumbnails; its WRAM compare reads bank `$7F` since `CT_DUMPWRAM` writes the flat
image whole.

### battle-enemy-census-mechanisms-vs-oracle (six ROM mechanisms of the enemy census, one enemy each)

disasm/notes/battle_ai_interp.md sec. 25. `python tools/battle_ab.py enemies 03,3C,F6,9A,3B,9F -j6`: each enemy's fight in GATO's
arena (synthetic: `$1111` poked on both sides), every monitored change for 1500 frames, against the rule's cartridge. One
enemy per mechanism: 03 the class-0 re-acquire's order (C1/3774 before C1/3779), 3C the `$AE6D` identity after FD/A982
plus the absent-enemy-target arm C1/8DBC, F6 the turn gate C1/84C0 (`$B247`), 9A the entrance facing pass C1/350F
through C1/2986 (`$9873`), 3B C1/4093's `$A0B3` (the script header's word 2) and C1/4844's third-upload gate, 9F the
physical's `$AE93` (C1/D6AE). Six mutations, each reverting one, all killed. ⚠ `battle_ab.py` writes a thumbnail per frame
per side (~130 MB per enemy): a FULL census (`enemies` with no ids) fills ~33 GB -- run it in chunks and delete
`build/_battle_ab` between them.

### field-dash-run-vs-logical-oracle (the lead's RUN = the Dash row, `$00F8` bit 1)

disasm/notes/field_dash_run.md. `python tools/dash_run_cmp.py` drives party3_field on both sides and compares the lead's
`$1802/$1882`, its velocity `$1D32/$1D33`, its anim mode `$1682` and the latched `$00F8` EVERY frame: `hold` (B held
over Left for 30 frames via CT_HOLDBTN, then Left alone -- run, then walk), `ctrun` (the headless CT_RUN hook, which ORs
B into the controller word while the drive holds a direction) and `remap` (`$2996` poked to 04 on both sides: Dash on
Y, so Y runs and B walks). All three are 0 frames differ; the run-frame lines name f5-f34 on both sides. Two mutations
kill it: the retired Shift binding (hold and remap walk) and a CT_RUN that holds no B (ctrun walks). Re-pin only when
the drives or the seed change.

### field-slot1-map-copies-vs-logical-oracle (obj0's slot-1 tile copies on the WRAM map)

disasm/notes/obj0_slot1_runner.md section 6. `python tools/slot1_map_copy_cmp.py` checks that the copies obj0's slot 1
makes with `E4` (C0/3D97 -> C0/AF4E) land on the freshly built `$7E:3000` map and stay there. The seed is loc 002's
curtain (`saves/field_re/loc002_curtain_ctl.state` / `.lakewram`, whose recipe is in regress_inputs.json). It runs two
drives:

- `reload` (X f4, B f60) compares the curtain rect on the five planes the `E4` writes EVERY frame, and prints the port's
  C0/0206 trail and the cartridge's C0/3D97 visits.
- `load` loads the location through each side's own loader (the cartridge's `--teleport` against the port's
  `--reentertest 2`) and compares the curtain and the parked rect, 110 rows.

Its one differing run (reload f21-f99: the menu program's own use of `$7E:3000` and the reload's NMI-off frames) is a
PINNED RESIDUAL of the port's frame model, the same one field-door-list-vs-logical-oracle names. The end lines are the
grade. Three mutations each kill it: the E4 without its WRAM copy, the reload's stage after C0/0206, and the load's stage
after CtSceneCreateIn.

### field-e5-copy-service-vs-logical-oracle (op 0xE5 and the `$0117` bit-5 service C0/1770)

disasm/notes/field_tile_copy_e5.md. `python tools/e5_copy_cmp.py` grades the queued copy against `CT_ORACLE_LOGICAL=1`:
op 0xE5 only stages `$013E-$0145`; a frame later C0/1770 copies the rect in the WRAM map (C0/1790's AF4E) and holds the
main loop in its style branch (2 frames, 4 for BG1+BG2) while the tilemaps are rebuilt. Two drives, every frame:

- `curtain` (`saves/field_re/loc002_curtain_ctl.state`, walk up + A: obj10's `E5 ..3C` then `E5 ..3B`) -- `$0117 $0138
  $013E-$0146 $0153`, obj10's cadence and ip, the layer words, and the curtain rect on the six planes, plus the picture
  (thumbnails, the port's P against the cartridge's P+2, from f5: f1-f4 differ in the lead's sprite rows at the seed
  either way).
- `intro` (`saves/field_re/loc002_intro_curtain.state`, the new game's opening: Mom opens the curtain) -- the same bytes;
  WRAM only, because that seed's port picture is black (a seed issue that predates this check).

Both read `0 frame(s) differ`; the `copies`/`held` lines name the frames. Re-pin only after re-measuring both drives on
the logical oracle. Four mutations each kill it: E5 as an immediate copy, the fitted 8-frame hold, style 3 shown on its
first upload, and the picture copied inside the render's own layers.

### field-e4-visit-end-vs-logical-oracle (op 0xE4 ends the object's visit)

disasm/notes/field_tile_copy_e5.md section 5. C0/3D97 (op 0xE4) copies the map at once and exits `CLC`, and the runner's
C0/5AB4 `BCC` ends the visit on it, so in a LIVE script the op after an E4 runs on the object's NEXT visit.
`python tools/e4_visit_cmp.py` grades that against `CT_ORACLE_LOGICAL=1` on scene 236's lever
(`saves/field_re/s236_lever.state` / `.lakewram`, the oracle's own `--teleport` from truce_market; the recipe is in
regress_inputs.json). A is pressed twice, once for each arm of obj9's fn1 (E5, E5, then E4, then a flag write and
`E3 01`). Every frame, both sides: obj9's ip and cadence, `$011F`, `$0117`, `$7F:00EC`, and the destination rows of all
six copies on the six planes. `wram ... 0 frame(s) differ` is the grade. The `e4-end` lines (f95 f199) name the visits
that stopped right after an E4, and `control-on` (f99 f203) names the next visits, which run the ops that follow.
Before the fix the port read `control-on f95 f199`. The picture is not graded here: it matches on every E4 frame, but
this scene shows an E5 style-hold residual (see the dossier). Two mutations each kill it: E4 continuing the visit, and E4
staged like E5. Re-pin only after re-measuring on the logical oracle.

### field-plane-survives-a-cutscene-vs-logical-oracle (the lead's plane `$0155` is one byte)

disasm/notes/field_plane_0155.md. User report 2026-10-09 (F5 `build/ct_debug_023.state`, loc 07B): after the soldiers'
table event Crono could only turn. The ROM writes `$0155` only at the load (C0/0C4C), the arrival tile (C0/9DC3) and the
step commit (C0/9D54); the port re-derived a host copy as `zb & 3` under a scripted lead and fed it to the probe.
`python tools/field_plane_cutscene_cmp.py` grades the meal against `CT_ORACLE_LOGICAL=1` from
`saves/field_re/s07b_table_seat.state` / `.lakewram` (the oracle's own `--teleport` with the capture's story page; the
recipe is in regress_inputs.json): A at the table, A through the boxes, op 0xD9 seats Crono at 0704,0C7F, control back at
f1099, then Left 30 / Right 60. Every frame, both sides: `$1802 $1882 $0155 $011F`; `wram ... 0 frame(s) differ` is the
grade. Before the fix 97 frames differ and the port's lead never leaves 0704,0C7F. One mutation (the old code, both
halves) kills it. Re-pin only after re-measuring on the logical oracle.

### field-nmi-tilemap-holds-cgram-vs-logical-oracle (a style tilemap takes the NMI: no CGRAM upload)

disasm/notes/field_tile_copy_e5.md section 2. The field NMI starts `EAD1 LDA $46 / BNE`: a tilemap the style branch left
in `$46` (1-4) is uploaded and the NMI `BRL EC15`s past C0/EB79, so C0/712E does not copy the page `$7E:2200` to CGRAM.
`python tools/nmi_cgram_cmp.py` grades it against `CT_ORACLE_LOGICAL=1` on scene 236's lever
(`saves/field_re/s236_lever.state` / `.lakewram`, A twice: four E5 copies of style 3). The room's lights (CGRAM 55-59)
flip bright/dim on the page every 2 frames. `no-cgram` names the NMIs that skip C0/712E, the cartridge's by
`--hookpc 712E` and the port's by its CT_XFERTRACE `nmi:` line. `page-held` names the frames on which CGRAM stayed behind
a changed page. `picture ... 0 frame(s) differ` compares the port's P against the cartridge's P+2 over the whole run.
Before the fix the port baked the page every frame: its P18-19, P57-58, P122-123 and P161-163 showed the dim lights.
Two mutations each kill it: the NMI always reaching EB79, and the bake reading the live page. Re-pin only after
re-measuring on the logical oracle.

### battle-fx-shape-window-sweep (`ct --sweepfxshape`: program ops 8A / 89 / AB / 8B)

The battle effect programs' SHAPE WINDOW: 8A sets a shape record (`$CAD1+k` shape, `$CAA6/$CAA8+k` centre, `$CAA4+k`
radius), 89 steps it, AB centres it on bound point 0/1, and 8B draws it -- the midpoint circle's octant CD/31AA, then one
of six shapes (CD:319E: three table builders through the `$2180` port, four placers and the unrolled copy CE/E1C7-E7C7)
into window k&1's pair of the window buffer not on screen. The sweep micro-executes the four handlers in the oracle on
8000 seeded cases (radii 00..FF, centres anywhere, shape 2 with cy = 0, records 0..3, both `$7C` parities) and compares
the whole 128 KB image. It is a sweep because the synthetic battle can only run the single techs (Haste, Napalm, Mega
Bomb, Area Bomb: shapes 0-2); shapes 3-5 belong to dual/triple techs. `refused` counts the port's STOP where the ROM
would jump into the copy outside its block -- the count is part of the pin. Six mutations, all killed. A FAIL with
mismatches is a port error in these routines; re-pin only if the seeding in main.c changed.
### battle-fx-ops-quad-tri-sweep (`ct.exe --sweepfxquad`, effect-program ops 85 86 A1 A3 A4 AC BD C3 D2 DA E0)

disasm/notes/battle_fx_program_census.md section 3. battle.c's `battleFxOpsQuadTri` against the ROM's own handlers,
3000 seeded cases per op: each handler is read from the table CD:16B5 and entered as CD/16A6 enters it (M 8-bit, X/Y
16-bit, DB 7E, DP 0, `$43` = the slot, A = X = op << 1, Y = `$40` = the operands at $7E:0300) over a poisoned WRAM, and
all of WRAM past `$004F` (the battle NMI restores `$00-$2A` around the runner) and outside the stack is compared, plus
the `$40` the handler leaves (the op's length). The two rasterizers ride it -- CD/2466 + D1/F9AF (AC) and the triangle
fill CD/3685 (DA, A1) -- and CD/28D2's projection (A3). Seeded because, when they were ported, most of these ops sat
behind ops the port did not run yet (Fire 2's 85/86/C3 after 89/8A/8B, Luminaire's A1/A3/A4 after A2/A5/A6). Three mutations kill it (85
without its `STZ $CA3A`, CD/3DC3 never negating, D2 without `JMP CD/1004`). Re-pin only with the handlers re-read.
### battle-fx-program-ops-sweep (`ct --sweepfxops`: effect-program ops against the ROM's handlers)

Byte-exact against the ROM, no capture needed. Each graded op (88.01 88.02 80.00 80.13 C4 C5 A2 A5 A6 A7 B1 B9 D7 EA
82) is micro-executed through the runner's own dispatch CD/16A6 at one D1 site per distinct operand tuple the 148
programs hold, on 64 seeded fills of the whole WRAM, and all 128 KB (less `$40-$44` and the stack) plus the operand
pointer are compared with battle.c's `battleFleeProgOp`. `CT_SWEEPFXOPS_SEED=<128 KB WRAM>` replays one real visit
instead (a `ct --hookpc <handler>` + `CT_HOOK_DUMPWRAM` dump). A new op joins by adding its sites to the SITES list;
the line changes then, and that re-pin is legitimate. A mismatch on an op already listed is a port bug, never a re-pin.
### sweep-battle-fx-op88-subops (`ct --sweepfxops`, its second line `sweepfxops-88:`)

Op 88's four sub-ops through D1/FBCE's own table (X = sub * 2, as CD/29DA enters it): 00 D1/FCF4, 01 FC38, 02 FC96 and
03 D1/FBDA -- the one no program reaches, so no site-based line can grade it -- on 64 seeded whole-WRAM fills each (both
`$7C` parities), all 128 KB but the stack against battle.c `CtBattleFxOp88Mem`. Its own seed: the first line stands.
### battle-fx-obj-colour-math-{lightning2,heal}-vs-oracle (`ct --sweepfxcompose`, its second line `sweepfxcompose-obj:`)

The sprites' share of the effect rows' colour math (battle.c `CtBattleFxObjMath`): the real frame's main layer and CGRAM
index per pixel (snes/ppu.c `g_ppuMainPix`) stand in for the port's sprite pass -- the OBJ colour pasted raw, then
FrontApply and ObjMath as field.c runs them -- and every OBJ pixel of a modelled row must equal the real frame. The line
also carries the non-sprite grade outside the battle V-IRQ's forced blank (lines 212.., drawn by nobody; the first line
keeps counting those rows as it was pinned) and each drawn row's CGWSEL / CGADSUB / COLDATA against the PPU's own
fields. Seeds: saves/battle_re/fx_lightning2_c920.state (Lightning 2: the fixed colour inside the colour window, GATO on
palette 5, Crono on 1) and fx_frog_heal_c975.state (Heal: BG3 on the sub screen added to Frog), battle_ab's GATO fight
under the rule. A difference is a port bug; re-pin only if the harness itself changes. fx_aura_whirl_c955.state is the
measured exception (not a check): records written mid-frame (disasm/notes/battle_effect_orbit.md 11).
### sweep-bganim-white-submodes (`ct --sweepbganim`, its second line `sweepbganim-white:`)

FD/E82C's sub-modes 0-6 (mode 4, toward white) against objslot.c on 4000 seeded record tables, its own seed.
### battle-fx-3d-objects-sweep (`ct --sweepfx3d`: effect-program ops A8 BA BC BE against the ROM's handlers)

Byte-exact against the ROM, no capture needed. The 3-D object ops -- A8 (CD/27CA: step + CD/2843's projection through
the rotation CD/3C8F), BC (CD/1E45: rotation steps), BE / BA (CD/1E27 / CD/1E91: a model of D1:EC5F rotated by
CD/1EAA, scaled toward the centre by CD/2173's CD/211B and filled into a window pair by CD/3685) -- run through the
runner's dispatch CD/16A6 at every one of the 68 D1 sites the programs hold them, on 64 seeded whole-WRAM fills each;
all 128 KB except `$00-$2A` (the NMI's scratch, restored around the runner), `$40-$44` and the stack are compared --
so the direct-page locals `$45-$51` the handlers leave (and CD/38B6's line base `$45/$47`) are graded -- plus the
op's length. Seven hand mutations (the rotation's last product, A8's scale add, CD/38B6's `$47`, CD/1EF7's 16-bit
`STY`, CD/211B's multiplier, BA's steps, BC's `$CAD8`) all fail it. `CT_SWEEPFX3D_SEED=<128 KB WRAM>` replays one real
visit (`ct --hookpc <handler>` + `CT_HOOK_DUMPWRAM`); the combos T41 T6B T70 T73, which the port's menu cannot reach,
were graded that way. The site list is the static decode's; a mismatch is a port bug, never a re-pin.
### battle-fx-misc-ops-sweep (`ct --sweepfxmisc`: effect-program ops D0 D1 D3 D4 D5 D6 DF E7 E8 E9 EE F0 F4)

Byte-exact against the ROM, no capture needed: the last ops of T3E T44 T3B T4A T5D T68 T71 (the computed jumps, the
shape-radius steps and branches, the anchor jitter, the BG3 split records, the band / point windows, the swaying quad)
micro-executed through CD/16A6 at the 23 distinct (op, operand) sites of the static decode, 256 seeded whole-WRAM fills
each; all 128 KB (less `$40-$44`, the stack, and for F4 D1/F9AF's `$00-$2A` scratch) and `$40` compared with
`battleFxOpsMisc`, the direct-page locals `$45-$4A` included. `CT_SWEEPFXMISC_SEED` replays a real visit
(`CT_HOOK_DUMPWRAM(_AT)`). Three mutations kill it (E7 FF resetting `$CD37`, EE 01 without its D4 cap, D1/F9AF's `$47`
lost). A new site or op changes the line (a legitimate re-pin); a mismatch on a listed op is a port bug.

### battle-fx-op80-family-sweep (`ct --sweepfx80`: ops 80.04 80.05 80.12 AF B3 B8 E3, and A1's shared raster)

Byte-exact against the ROM, no capture needed: the same harness as the sweep above, 256 seeded fills per site, over
all of WRAM except `$0000-$002A` -- the direct page the battle NMI saves and restores around the runner (D1/E7A9 /
D1/E885), where B8/A1's slope scratch `$00-$17` and CD/3924's `STZ $0020` die -- so the DP words above it the handlers
leave ($45-$4A: B3's counters and $49, CD/38B6's / CD/3B10's `$45` `$47`) are graded. A1 rides it because B8's fill
CD/3952 is the parametrized CD/3685 (battle.c `battleFxTriRasterV`). Five mutations kill it (the wedge's corner, the
band loop's $45, CD/38B6's `STA $45`, B3's 16-bit `STX` high bytes, 80.12's D4). A new site or op changes the line
legitimately; a mismatch on a listed one is a port bug, never a re-pin.

### sweep-ai-condition-walk phases 4-5 / battle-enemy-turn-handlers-vs-oracle (the enemy AI's resolvers and turn handlers)

`ct --sweepaieval` grades, besides the condition walk (phases 1-3), C1/AC14 with every C1/B8BB resolver value 00-38
(phase 4; 06 07 17 18 excused: C1/2986's host positions) and every C1/B88D turn handler but 05 from C1/8D88's JSR
(phase 5; CD/0033 stubbed on the ROM side and its A compared, 02/12/15 stopped at C1/C1DD) -- all of WRAM, byte-exact;
FAIL when any value / handler is never graded. `python tools/battle_ab.py enemies 3A,3E,C0,04,0A,7B` grades what the
turn does with a handler's verdict in a running fight against the rule's cartridge (gato_wait.state, the id poked into
`$1111` on both sides): 0A's C1/AC57 at once, 07's no-seat miss, the message box holding a 0F turn, the 0B / 11 / 12
writes, an enemy tech's `$9877` from CD/5526. Every line must read IDENTICAL; a line that stops being identical is a
port bug, not a re-pin (disasm/notes/battle_ai_interp.md sec. 23).

### battle-module-tail-vs-oracle (the module's end tails, `python tools/battle_module_tail_cmp.py`)

battle_ab's GATO fight with Magus solo and his Black Hole (gato_wait.state, both sides from the same WRAM, the rule's
cartridge): the frame and the value of each write of the module's end chain -- C1/F93E's XP on the results' first frame,
then C1/83F9-8448 (`$29AD`, the C1/B442 sheet, CF/FC08's `$29B0` record) on the frame FD/AD17 returns, and the ack. The
values must read IDENTICAL (byte-exact); the frames are a pinned residual: +4 / +3 are the port's results machine
(battleResultsTick), -17 at the ack is the exit tail's (CF/FBE5's waits, CD/03B4). Re-pin a frame only when one of those
is fixed; a value that differs is a port bug (disasm/notes/battle_module_tail.md). (2026-10-03: the ack is +3 now -- the
exit from the tail to the ack is the cartridge's, frame for frame; the +3 is the results machine's, carried.)

### battle-defeat-exit-vs-oracle (the defeat's exit, `python tools/battle_defeat_exit_cmp.py`)

battle_ab's enemy B6 on GATO's arena (gato_wait.state, both sides from the same WRAM, the rule's cartridge) wipes the
party -- a scripted-loss continue ($2A1F bit5). Every change from f900 to f970 of the exit's bytes, on both sides: the
module's tail (`$29AD`, the RNG cursor), CF/FBE5's `$A4` loop, CF/FCCF's HP floor on Crono's and Marle's sheets, CD/03B4
(`$CE16`, `$CA25` 0C / 0E / 08), CD/0415-0438's waits and SPC poll before C0/1BE6 (`$1E10`), the ack (`$0118`). Every line
must read SAME (`8 / 8 quantities frame-exact`): a DIFFER is a port bug (disasm/notes/battle_defeat_flow.md).

### battle-status-colour-cycle-ends-with-the-fight-vs-oracle (`python tools/battle_status_palette_after_cmp.py`)

The Naga-ettes' fight on both sides (A mashed): Slow's colour cycle on Crono and Marle, then the victory. Crono's and
Marle's OBJ palette rows (OBJ5 / OBJ6) on the field after each side's ack must read SAME -- the victory's C1/356D puts
each seat's row back from the base page (C1/30B6). A DIFFER with colours 1 / 12 on a ramp value is the outline staying
on the field (disasm/notes/battle_status_render.md, last section).

### battle-enemy-area-band-vs-oracle (an enemy tech's area pass, `python tools/battle_ab.py enemies 09,BA,9E,76`)

Four enemies whose techs carry the area byte 08 (C1/C586 -> the band C1/25A3: the victims inside a 16-px strip from the
attacker toward the target), every monitored change over 1500 frames against the rule's cartridge in battle_ab's arena.
Each line must read IDENTICAL; a difference at an HP change of the wrong PC is the band (disasm/notes/battle_ai_interp.md
sec. 24).

### field-follower-on-op-af-follows-vs-oracle (a follower parked on op 0xAF)

Scene 129's Lucca loops on op 0xAF (C0/2FFD = `JSR C0/3002 / INX`), not on 0xB0: the same follower body, the ip one byte
past it. From the cartridge's own load of loc 081 (saves/field_re/lucca_af_s129.lakewram, party Crono/Lucca/Robo), the
port walks Up/Left/Right 40 frames each; the four pinned lines are the cartridge's `$1802/$1882/$1806/$1886` at its
f31/f63/f71/f128 (the monitors number one frame apart). A `$1806/$1886` that holds 8008/FF19 is the follower not
running (disasm/notes/party_follower_ring.md sec. 10).

### field-inn-wake-party-slot-calls-vs-rule-cartridge (`python tools/inn_wake_cmp.py`)

The 600 A.D. inn with Crono and Lucca (the user's saves/field_re/inn_lucca_007.state at "Register? Yes"): the bedroom
(loc 0x75), from each side's first lit frame, 800 thumbnails against the rule's cartridge. They must read `0 differ`.
obj0 wakes the party with the party-slot calls `05 00 13` / `07 02 13`, whose target is the object hosting the slot
(`$0197 + slot`, C0/61E9 / 6214), not 1 + slot / 2. A difference from about +60 with Lucca gone from her bed is that
mapping. The SPC is seeded under the rule (CT_AUDIO_SEEDSTATE), because the song load `EA 02` is 44 frames of SPC.

### field-tent-hostage-80pt-counter-switch-flames-vs-rule-cartridge (`python tools/tent_hostage_cmp.py`)

The tent of horrors' 80-point game (loc 1B2, scene 434) from saves/field_re/s1b2_hostage_lake.state (Crono + Marle,
Bekkler's choice 2 taken), two drives against the rule's cartridge: `lose` (nothing pressed: Marle sinks, the blue switch
shows, the flames, the fair) and `switch` (Crono walks to the left wall and presses A on the switch: she rises, then sinks
again). Per drive, nine regions (loc, the counter local[12] = $7F0224, local[19], Crono's and Marle's X/Y, obj8's IP and
$1B10) must read `identical` up to the cartridge's location change, and each event (counter starts, switch shown, switch
pressed, flames, loc 005) on the cartridge's frame. The counter is `72 12`, C0/6C85's SIXTEEN-bit INC: a counter stuck at
0 with every event 'never' is the live VM without that op (the user's report: the hostage below the fire, no switch).
The $1180 IP mirror and the fair's load after the change are not graded (the tool's docstring says why).
A third drive, `escape` (2026-10-09, the user's F5: "she changes a little after rising"), pushes the three monsters
back from f780 (A on obj13 / obj12 / obj14) and touches the switch at f1097, so fn8's RETURN (f1215) comes before the
catch (f1260) with the control open; `$1684` / `$1784` / `$011F` are graded in every drive (35 regions). The hostage's
pose `AA 09` must hold through the RETURN (`hostage off anim 9 ... never`): the ROM's RETURN C0/5F74 resumes fn7 and
touches no anim byte -- the port's RETURN-time reclaim (ct_vm.c, the stand-in for C0/3002's reset) put her on anim 0.

### ow-party-follower-walks-the-trail-vs-rule-cartridge (`python tools/ow_follower_cmp.py`)

Crono and Lucca leave the 600 A.D. cathedral and walk the overworld (south, east, back west and north). Each side's walk
starts the same number of frames after its own world init. Per frame the lead's block, Lucca's block (X/Y, +28, +02,
+0B) and the trail cursor `$1B5A` must read `0 differ`. Lucca is the class-$3AE2 follower C2/3AE2: she walks the lead's
ring of step arrivals (C2/3934), idles inside 16 px and re-decides every 8 frames on a cell edge
(disasm/notes/ow_party_followers.md). The first field to differ names the part: `ring` is the lead's push or reset,
`folAnim` early or late at the fidget is the idle counters `$02B1-$02B3` (zeroed by the world load).

### ow-party-follower-slot2-walks-the-trail-vs-rule-cartridge (`python tools/ow_follower_cmp.py 400 party3`)

The same walk with Marle poked into party slot 2 on both sides (`$2982 = 01`: PEEK_POKE on the cartridge,
saves/field_re/catedral_party3.lakewram for the port). Her block `$0E70` runs C2/3AE2 with slot 2's constants (the 32-px
box C2/3F27, the ring entry before Lucca's, `$02B3`); her X/Y, +28, +02 and +0B are compared beside the lead's and
Lucca's. Mutation: slot 2 given slot 1's 16-px box.

### battle-roly-rider-death-recolor-is-its-own-palette-vs-oracle

The Roly Rider ambush (scene 119): the dying Roly's OBJ palette 4 on the port's live page, frame by frame from CF/FE20's
copy of its own colours (f442) to the end of CD/00A5's red fade record (f459), CT_BATTLE_MEMMON `2380:20,0598:C`. Every
value is the cartridge's (roly_rider_battle.state f324-f341). A FAIL with white colours is the copy gone; a FAIL with
the Hetake's colours is a baked recolor back. disasm/notes/battle_death_dissolve_gate.md PART 5.

### field-caption-plane-staff-roll-vs-rule-cartridge (`python tools/caption_plane_cmp.py`)

The endings' staff roll, BG3 written through WRAM (disasm/notes/field_caption_plane.md): from
saves/field_re/s5b_roll_f2280.state (the cartridge in scene 05B after `--teleport 91`, 30 frames before its exit) the port
runs 05B's exit, 05C's and 05D's loads and their rolls itself -- op 0x29's font DMA, C0/274D's clears and lines, the NMI's
uploads into its VRAM image, BG3 drawn from it -- and every frame of 05C (port f40-500) and 05D (f560-905) must be
pixel-identical to the cartridge's at that scene's offset d. d is the E1 exit's C0/3B71 spin (2 and 7 frames on the
cartridge, the port's next vblank; OPEN, the CPU-timing class): if it is ever ported, d moves to 0 -- re-pin then, after
checking the identical counts stay whole. A count below the whole with captions in the differing frames is the plane;
without, look at the fades and sprites first (05D after f905 has an NPC the port does not draw, which is why the span
stops there). Mutations: no 274D leg, the attribute's palette, the plane never flagged.

### field-chest-box-closed-by-camera-vs-rule-cartridge (`python tools/chest_box_close_cmp.py`)

A chest's message box closed by the camera -- C0/274D's bit-5 arm C0/2758 (disasm/notes/field_caption_plane.md sec. 8):
from saves/field_re/s49_chest_walk.state (the cartridge below loc 031's chest (12,37)) A opens the chest and the lead walks
away with the box up; when the camera's top row reaches the chest's, the arm writes `$0129` = 0D and the box machine
ramps it out. Two lines grade the arm: the cartridge's `$0129` timeline (`--monitor 0129 --diff`, 0D at f280, 00 at
f291) and the port's (CT_TBTRACE, 0D at f281, 00 at f292: the +1 key alignment every earlier edge shows). The rest are
the whole picture f1-320 at P + 2 with their residuals PINNED (the chest box's typewriter and ramps -- the cartridge
draws it with C2/0003 --, a 1-px camera X while the lead stands at a wall, Marle's follower sprite): re-pin only when
one of them is fixed, after checking the frames against the cartridge. Mutations: the store, the 2753 branch, the tile
back in the collision RAM.
### lockstep-box-y-flips-the-window-vs-oracle / field-textbox-y-flip-and-menu-rerequest

The message window's side `$0126` under the buttons (disasm/notes/field_textbox_y_flip.md). Y (`$00F6` bit 0 = config
slot `$2998`, C0/1916 -> C0/1985) moves an open window to the other half (`EOR #03`); the X menu's C0/011B re-requests
the box (`$0129 = 1`, `$0126 = $0127 = 0`), so after the menu the window is re-resolved and retyped from the start.
The first check is the scene 7 talker's box projection against the oracle with Y at f40/f60 (`CLEAN over 149
frames`); the second is Taban's box in scene 8 with Y, then the menu, read through `CT_TBSIDE` (the side the host
draws) and the reveal count. A FAIL on the first is the flip; on the second, the flip, the window drawn from anything
but `$0126`, or the menu's head. Re-pin only after re-measuring the oracle's `$0126` timeline.

## Pinned baselines — what a FAIL means, and when to re-pin

Three different kinds of `expect` line exist. Know which one you broke:

1. **Oracle lockstep first-divergence frames** (`vmdiff-*`): `--vmdiff` seeds our VM
   from the oracle's WRAM and reports the FIRST scheduler divergence. The pinned
   frames (im_so_sorry **f141**, bedslide **f51**, DEMO8 **f109**, S5 **f114**) are
   oracle baselines recorded across many commits. **An EARLIER first divergence is a
   regression** — full stop. A LATER divergence (or CLEAN) is an improvement: verify
   it against the oracle, then re-pin with `--pin` and say so in the commit.
2. **fnv32 render snapshots** (`render-scene1/5-snapshot`): the hash pins the whole
   composed 256x224 frame. They are **brittle by design** — canaries that trip on ANY
   render change. Re-pin via `python tools/regress.py --pin render-scene1-snapshot`
   **ONLY after an intentional, oracle-validated render change** (per the project
   mandate, "looks right" is not validation — compare against the oracle capture).
   Older baselines (scene 1 `7EAEEFD1`, scene 5 `38EA80E2`) are superseded history.
3. **Pinned residuals / baselines under investigation** (below): the pinned counts are
   the *currently observed* output, not a byte-exact claim. PASS means "unchanged",
   not "correct".

`--pin NAME` runs the check and rewrites its `expect` list from the full output lines
matching its `pin` regexes. It refuses to rewrite if any regex matches nothing.

### The playermove open question (do not "fix" the pin)

`playermove-input-velocity` (`ct.exe --playermove`) is pinned at the line observed
2026-07-01:

```
intX$2C 269/270  intY$2D 270/270  accX$2E 222/270  accY$30 134/270  velX$32 269/270  velY$33 270/270
```

Memory and commit messages record this check as **270/270 byte-exact**, but the
current build diverges on the accumulator fields. Two siblings show the same drift
and are pinned the same way: `playerframe-endtoend` (applied-velocity **196/270**,
position still 270/270) and `porttick-autonomous-mover` (**128/136** owned-actor
frames). These pins are **baselines under investigation** — they exist so any
*further* drift is caught. PASS is not a pass claim. Someone needs to determine
whether this is a real engine regression or harness drift; until then, do not re-pin
these to make a red run green.

Two more pinned residuals are *documented-expected* (not under investigation):
`tile-prio-recompute` (94/94 mismatches — the ROM's $0C00/$0C01 is stateful for idle
actors; a stateless recompute can't hit it, per the check's own code comments) and
`velocity-trig-circle` (89 of 1166 samples off the trig circle, all with a zeroed
component = collision-clipped, per the check's own tally).

## Missing-input checks (currently SKIP)

These stay in the suite so the runner names exactly what capture would revive them:

- `follower-conga-2member` — `ct.exe --followtest --from two_member.state --input 81`
  (the scene-8 2-member conga-follower byte-exact check from commits f33346d/0fdc830).
  **Seed it**: capture a Snes9x savestate in scene 8 with a 2-member party, convert
  with `python tools/s9x_to_lake.py`, save as `two_member.state` in the repo root.
- `stairs-scene13-slope` — `ct.exe --stairtest saves/scene13_stairs.wram`
  (commit 78670ce; clean output is `stairtest: first velocity divergence at frame -1`).
  **Seed it**: stand the player at the scene-13 stairs and dump the 128 KB WRAM
  (e.g. `ct.exe --peek <state> out.raw` writes `out.raw.wram`), save it as
  `saves/scene13_stairs.wram`. `build/ow_wram.bin` is an overworld capture — wrong
  scene, do not use it.

Known-but-unseedable (recorded in `excluded`/notes, no suite entry possible today):
the Porre boat-ride oracle states (`Chrono_Trigger_USA_SNES.001-.009`) are absent, and
several per-commit interactive repros (candy shop, pillar bump) lack verbatim command
lines.

## Excluded candidates

`tools/regress_checks.json` `excluded` records what was triaged out and the observed
reason — currently `--checksort` (a statefulness *experiment* that by design prints
1392/3780 known mismatches; no success signature exists) and `--movetest` (an
interactive can-the-player-move diagnostic with no verdict line; ~101 s; superseded by
checkctrl/playermove/playerframe).

## Determinism

Every included check produced an identical signature across two consecutive full-suite
runs at `--jobs 4` (2026-07-01). New checks must clear the same bar: run the suite
twice; a check whose signature differs between runs is excluded (or its unstable
fields dropped from `expect`).

## Micro-execution sweeps: the NMI trap

A sweep that MICRO-EXECUTES a ROM routine (set the CPU up by hand, `PC = C0/xxxx`, run to an RTS
sentinel) must clear `nmiWanted` / `irqWanted` / `intWanted` **before every opcode**, not just once
before the call. In LakeSnes the CPU advances cycles through its own memory accesses, so the PPU
keeps running *inside* the micro-call: an NMI can fire mid-routine, run the ROM's entire frame
handler, and clobber WRAM under the routine being measured. Whether it fires depends on the PPU
phase inherited from the previous combo, so the sweep silently becomes non-deterministic.

This was measured, not theorised: `--sweepclamp` reported **133 mismatches on one run and 118 on
the next** from the same binary and inputs, all of them on `$01EB` — the touch latch the NMI path
rewrites. With interrupts suppressed per opcode the same sweep is 16562/16562 byte-exact and
identical across runs. The 133 "port bugs" were entirely manufactured by the harness.

The older sweeps (`--sweepcollide`, `--sweepactor`, `--sweepacfd`, …) are not exposed only because
they micro-execute after **2 boot frames**, before the game enables NMI. Any new sweep that settles
a live scene first (`HarnessDriveScript` + N frames) is exposed and must suppress interrupts.

Related: pin the CPU's arithmetic flags too (`c`/`n`/`v`/`z`). Leaving them at the previous combo's
values makes the entry state depend on iteration order; `--sweepclamp` sweeps the entry carry
explicitly as an input dimension instead.

## Environment notes

- `field-newgame-pan` requires `CT_NAMEAS=Crono` and `TB_AUTOADV=1` (declared in its
  `env` — the runner sets them for you). Without `CT_NAMEAS` the naming screen never
  confirms and the check silently captures scene 0 instead of scene 2.
- A typo'd `ct.exe` mode flag is **silently ignored** and falls through to the default
  M0 harness — keep `argv` exact in the JSON.
- Timeouts (`timeout_s`) are set at roughly 5x the observed runtime (minimum 30 s);
  a TIMEOUT is reported as FAIL.

## Suite wall-clock (2026-09-06)

The suite is a few hundred independent subprocesses, so it is bound by the machine and by the slowest
single check. Three things were leaving most of that on the table; all three are measured, none of them
changes a single pinned byte:

| step | full tier |
|---|---|
| Debug build, `--jobs 4` (the old defaults) | ~500 s |
| `--jobs min(12, CPUs-2)` (regress.py default) | 252 s |
| `CMAKE_BUILD_TYPE=RelWithDebInfo` (tools/build.py default; `CT_BUILD_TYPE` overrides) | 135 s |
| `CT_NOPACE=1` on every non-`--selftest` ct_field check | **102 s** |

- **Parallelism.** 4 jobs on a 20-core box. Measured on the quick tier: 4 -> 145 s, 12 -> 88 s, 18 -> 84 s;
  the floor is the slowest single check, and only 4% of the tier is the serial `exclusive` tail.
- **Optimisation.** The tree built `Debug` (`/Od`) -- every check is an emulator or engine run. RelWithDebInfo
  keeps full symbols and line info. The slowest check (`verifyframe-oam-ourlist`) went 57.2 s -> 21.6 s, and
  the whole suite stayed byte-identical: 374/374 with no re-pin.
- **Frame pacing.** The 60 fps governors in the render loops are gated on `!selftest`, so a check that drives
  with `--walkscript` + a probe instead of `--selftest` slept its way through 2270 frames at exactly 60 fps
  (that is also why the machine plays the game's music at normal speed while the suite runs).
  `CT_NOPACE=1` drops the governor and vsync; the loop is the same sequence of ticks.
  `field-battle-bat9-return`: 38.1 s -> 3.4 s, same output. `audio-pacing-no-drift` is deliberately excluded.

Rule of thumb after this: `--quick` ~35 s, the full tier ~100 s. If a check suddenly costs ~60 fps worth of
wall clock (frames/60 seconds), it is missing `CT_NOPACE` or `--selftest`.

### battle-cyclone-every-victim-after-an-enemy-attack-vs-rule-cartridge (`python tools/cyclone_victims_cmp.py`)

Crono's Cyclone on two Blue Imps + a Roly (loc 070, saves/battle_re/loc070_imps_roly_pre_lake.state / .lakewram), the
target confirmed while a Blue Imp's attack runs. The dispatch's victim list (`$AD8E`, `$AD8D` of them) must read the
same on both sides and each victim must take damage (and die or not) as on the cartridge: `3/3 victims identical`. A
list of three where the port reads `took damage ... port no` for the Imps is the one-victim latch `$B1FC` bit 1 left
set by the Imp's attack (C1/99C2) -- the PC tech arm clears it (C1/BB66). The HP values are printed as
`cyclone-victims-info` and not graded. Before it the ambush (obj22's fn1 and the three enemies' walk up to the `D8`,
f130) must read `ambush 10/10 regions identical` and Crono be ready on f271 on both sides: an ambush 4 frames early is
op 0xDA without a follower not ending its visit (C0/3463 `INX / CLC`). disasm/notes/battle_multitarget.md sec. 9.

### options-free-sync-skips-the-hardware-waits (`python tools/sync_free_check.py`)

The FREE sync mode (settings `sync=free`, field.c `hostSyncFree`): the attract's first 3600 frames under both modes, every
location load listed. Graded: the same six loads in the same order, 6/38/16/31/37/23 black frames frame-perfect and none
free, the free run reaching the last load 158 frames sooner. What free drops: the song uploads that park the field's
scripts, a load's and the menu reload's black, the battle's boot / exit / in-fight upload stalls, the overworld's inline
sound JSL -- the SNES's busy time, not anything the game does.

### options-overlay-reads-the-settings-file (`python tools/options_overlay_check.py`)

The player's options: a `ct_settings.ini` written to a temp folder (4:3, window x3, integer scale, A on Space, sync
free) is read through `CT_SETTINGS` -- a headless run never reads the file on its own, so the rest of the suite cannot
depend on a player's saved options -- and `CT_OVERLAY_SHOT` draws the F1 overlay (Dear ImGui, src/overlay.cpp) on the
Controls tab at f40 under the dummy video driver and saves the window. Graded: the window is 896x672, the picture fills
its width on the middle line, the overlay's tabs are drawn. Needs `third_party/imgui` (`tools/get_imgui.ps1`).

### battle-enemy-census-defeat-exit-tail-hdma-vs-oracle (`python tools/battle_ab.py enemies 6E,E3,F4 -j3`)

Three enemies of the census in GATO's arena whose fights end in the party's defeat: every monitored change, `$AE93` (the
hand-back) included, against the rule's cartridge -- each line must read `IDENTICAL`. They grade the exit chain C0/1BE6
(five `JSL C7:0004`, the field's song among them) priced on the HDMA the battle's exit tail leaves: none until an NMI is
taken inside C7 (D1/F4C0's absolute `STA $420C` goes through the interrupted code's bank), then the field's six channels
on lines 0-210 (FD/C1EE's program, the tables the ROM's builder FD/C2C1 writes -- src/xlat/hdma_fd.c -- and the V-IRQ's
cut at 211). With the battle's 8-channel mean the chain ended past a vblank and the hand-back came a frame late.

### battle-enemy-census-movers-vs-oracle (`python tools/battle_ab.py enemies 16,1B,4C,D8 -j4`)

Four enemies of the census in GATO's arena, the party idle, every monitored change against the rule's cartridge: each
line must read `IDENTICAL`. They grade the enemy walk: the stepper CF/F978 runs a segment in flight whatever the mode
(16, 4C: a no-action turn commits mode 00 mid-segment), the pending enemy keeps walking until C1/4058 counts `$A0FD`
up (1B), and CC/EC0B stages D8 / 94 / AD one pixel lower (D8). A span a few frames long or short is a walk that began
somewhere else (disasm/notes/battle_ai_interp.md sec. 25.3).

### battle-enemy-census-counter-eval-vs-oracle (`python tools/battle_ab.py enemies C1 -j1`)

Enemy C1 confuses the party; the line must read `IDENTICAL`. A forced (confused) attack on an enemy arms its counter
(C1/C011); when the counter takes no action, C1/881C's eval still runs on the same frame (C1/8C09, its draws). Without it
the RNG cursor runs one along and the next confused PC's target (FD/AB40) differs -- C1's fourth action walks to Lucca
instead of the enemy. disasm/notes/battle_ai_interp.md sec. 25.3.
