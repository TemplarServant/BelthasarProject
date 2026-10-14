# Roadmap

What is left, grouped by area. Each item says what it is, why it matters, where to start, and a rough size:

- **S**: hours to a day
- **M**: days
- **L**: weeks
- **XL**: a project of its own

The items come from the project's notes and history as of 2026-10-10. The notes often call something open after it
has already landed, so an item marked **needs re-check** has newer evidence (a later commit, or code that looks like
the fix) that it may be done. Confirm it on the oracle before you start. Item numbers are for reference only, not
priority.

Contents:

1. [The public release and what it left open](#1-the-public-release-and-what-it-left-open)
2. [Engine](#2-engine)
3. [Debt: emulation and bakes to retire](#3-debt-emulation-and-bakes-to-retire)
4. [Mods](#4-mods)
5. [Tooling and documentation](#5-tooling-and-documentation)
6. [What is not portable (the floor)](#6-what-is-not-portable-the-floor)

---

## 1. The public release and what it left open

**Done (2026-10-11).** The public tree builds from the ROM alone, proved by a clean-room run: the exported tree
(`tools/release/export_public.py`: 809 files, the byte and dialogue scanners clean) set up with
`python tools/setup.py --rom` in a fresh directory, 17 minutes: all 54 generated files (52 since B14 retired two) byte-identical to the private
tree, `ct_assets.dat` identical, every program built; `python tools/fixtures.py make all`; the quick suite 462 pass,
0 fail, 116 skip (the private captures of 1.1).

| Generated files | Total | Made from the ROM only | From power-on recipes | Still from a capture |
|---|---|---|---|---|
| `src/data/*.h` | 47 | 12 | 35 | -- |
| `src/xlat/*.c` | 5 | -- | 5 | -- |
| `ct_assets.dat`'s captures | 6 sections | -- | all six (asset-stage generators) | -- |
| Regression checks | 817 | 581 need only the ROM and the recipes (`tools/fixtures.py`: 118 fixture files) | | 236 still read one of 68 private captures (1.1) |

How it works: [README.md](../README.md#2-set-up-from-your-rom) (the setup flow, the bootstrap from
`tools/gen/shapes/`), [TESTING_OVERVIEW.md](TESTING_OVERVIEW.md#where-a-checks-state-comes-from) (fixtures), the
`proof_doc` of each manifest entry whose file is not byte-identical to its old capture-made version (seven files:
bytes nothing reads, or the rule's cartridge's value where the capture carried real-time lag). The rule stays: a
recipe's output reproduces the file byte for byte, or every difference is explained there and no check gets worse.

### 1.1 Checks still on private captures (L)

236 checks read one of 68 captures from the owner's playthrough; in a fresh clone they SKIP with the file named. Each
fixture's entry in `tools/regress_inputs.json` and each blocked attempt in `tools/fixtures/<domain>.json` /
`tools/recipes/fx*_*.json` (`"NOT YET USED"`) says why. The families, largest first:

| Family | Checks | Why a recipe twin does not pass yet | Next step |
|---|---|---|---|
| The cathedral's three-member party (`saves/battle_re/party3_field.*`, `bat9*`, `party3_form`, `party3_freshsheet`) | ~80 | The closest twin (`fx_party3_field`: the owner's party bytes poked into `cathedral_hall`) passes 49 of 80; the other 31 show **real port-vs-oracle differences** on the new state (menu pictures, the door list, the reload cost, the follower ring, render hashes, enemy cells) | Fix those differences (they are engine bugs the old capture hid), then switch the fixtures |
| The attract's first cut (`saves/attract_cena_inicial.state`) | 42 + 7 tools | Those checks run the cartridge in **real time** (no `CT_ORACLE_LOGICAL`) and pin the owner's real-time frames; a rule-made twin diverges at the first beam read (f105) and the random-object legs differ outright | Move their oracle side to the rule (where the power-on twin `fxa_attract_cena_inicial` is exact) and re-anchor |
| GATO's fight before the first A (`saves/battle_re/gato_wait.state`) | 35 | Three checks depend on the sound processor's phase inside the capture (enemy D9's close, the module tail's ack, the song spotlight's residual pixels); a hybrid of the capture with the twin's WRAM passes, so the cause is the APU part | Find which SPC state the three depend on, or make them independent of it |
| Fair decisions and 600 A.D. scenes (`s434_decision`, `s439_marle_choice`/`_join`, `scene468_stairs_to_120`, `m81_frog` (its `.lakewram`; `vmdiff-manoria-frog` moved to the twin 2026-10-11), `m81_nuns`, `m81_naga_after_fight`) | ~24 | Each is a port finding on the twin (2.F14-F17) or pins a battle RNG the twin does not reach | Fix the findings; reach the Naga-ette fight's RNG |
| The ferry (`ferry_re` x4), flag pages (`formlock_flags.bin`, `chest26_opened.flags`, `savepoint_flags.bin`, `s120_lucca_flags.bin`), forest ambush/exit, `bat2`/`bat4`/`bat8`, `loc070_imps_roly_pre`, `hud_close_s13`, `hetake_dissolve_anim_ppu`, `atb_partial` | ~30 | Not attempted, or the provenance is lost (snes9x splices); the ferry clerk does not answer on recipe states | Write the recipes |
| Missing even privately (`catedral_*`, `door_009`, `inn600_*`, `inn_lucca_007`, `lucca_af_s129`, `roly_rider_*`) | ~15 | The files are gone | Recipes from scratch |

### 1.2 Smaller release leftovers (S each)

- ~~**The recipe step `["sram", <recipe>]`**~~ **Done (2026-10-11):** every recipe writes `<name>.state.srm`, its state's
  last 8 KB block (the battery; `name_saved`'s equals `saves/ct_real_save.srm` byte for byte), and the step boots the
  oracle with it.
- **Python ROM reads.** `assets/extract_assets.py`, `tools/dis.py` and several comparison scripts open the ROM
  themselves instead of `ctlib.rom.load()` (setup verifies it first; this is defence in depth).
- **Generated headers' comments** still name the old one-off scripts and captures (menu_shop, menu_chrome,
  menu_saveform, dlg_cursor, menu_loader, ow_gullmap, ow_era_placard); changing the text means regenerating them.
- **`saves/ct_window.bin`** deliberately keeps 41 pixels its first hand lift repaired wrongly (x 5-7 of rows 8-32), so
  the port stays graded against the same bytes; `tools/gen/ct_window.py --cartridge` writes the cartridge's plain BG1.
  Switch to it and re-validate the dialogue-window checks.
- **The battle generators run their oracle peeks in real time** (only the recipe states are built under the rule).
  Moving them under the rule may move bytes; do it with the same proof as 1.
- ~~**`--navworld`**~~ **Retired (2026-10-11):** the heuristic waypoint walk no longer reached the overworld and nothing
  used it; the recipes replay the pad it once played (`ow_nav_downstairs`, `ow_nav_w0`).
- **Long paths on Windows**: the build fails under a deep directory (MAX_PATH); the README asks for a short path.
  CMake's own object-path shortening or a `CT_BUILD_DIR` would remove the constraint.

---

## 2. Engine

Known differences from the cartridge, and unported pieces. Under the 2026-10-09 decision
([DECISIONS.md](DECISIONS.md)), a residual that is **timing only** (a frame or two, from hardware speed, with no
effect on what happens) is documented and left alone unless it changes the game. Those are marked *timing only*.

### Battle

| # | Item | Why it matters | Where to start | Size |
|---|---|---|---|---|
| B1 | **Census: enemy 07** returns to the field one frame late. The cause is the boot's song post landing on `C1/0000`'s frame. A saved patch (`disasm/notes/patches/battle_boot_post_c1_0000.diff`) fixes 07 but moves the attract's 087 by a frame. It is coupled to `c7_dispatch_preamble_listing.diff`, and both must go in together with the arm formula re-measured. | *Timing only.* Probably close as documented under the no-emulation decision. | [disasm/notes/battle_ai_interp.md](../disasm/notes/battle_ai_interp.md) sec. 25.3 | S to document, M to land |
| B2 | **Census: enemy B9** returns one frame early. The cartridge's exit chain returns 242 cycles past a vblank, a knife edge decided by the CPU/SPC phase. | *Timing only.* No cost model can settle it without emulation. Document it. | same | S |
| B3 | **The battle menu's picture** appears one menu pass after the logic on the cartridge (the command hand, the tech list). The engine draws from the current state, 4 to 7 frames early. | What the player sees. | [disasm/notes/battle_menu_pass.md](../disasm/notes/battle_menu_pass.md), `tools/battle_ab.py` | M |
| B4 | **Unwritten battle WRAM**: `$AE91`/`$AE96` on some paths, `$A014..` for items and techs, `$AD89`, and `$AE95`/`$AE99` from `C1/AD35`, plus `$AE95` from the PC dispatch (`C1/BBCE`/`BFED`). | A later reader of these bytes would see stale values. Find the readers first. | `tools/battle_ab.py`, `CT_BATTLE_TRACE` | M |
| B5 | ~~**The victory's celebration ladder** starts 3 frames early~~ **Done (2026-10-13):** C1/356D runs as its four frames (`battleVictoryTeardownFrame`): seat 0's `C1/1BAA` removal in the victory's frame V, seat 1's in V+1, seat 2's in V+2, then `$95D5 = FF`, the `C1/0C2D` repaint and the ladder's arm in V+3 (first publish V+5). Graded against the rule's cartridge by `battle-victory-teardown-frames-vs-oracle` (solo and three-member parties, 0 differing frame-fields from V-2 to V+40). Left: the ladder's bytes (`$5DF1`, `$5DC9`, `$5DBD`, `$5DC1`) are still host state, and the exit after the results runs 3 frames behind the cartridge (CF/FBE5's `$A4`, not derived). | -- | [disasm/notes/battle_end_ui_teardown.md](../disasm/notes/battle_end_ui_teardown.md) sec. 9 | -- |
| B6 | **Per-enemy renderers**: GATO still has his own render path next to the generic enemy renderer (the Hetake was moved to the generic one on 2026-10-07). | Debt: one renderer should draw every enemy from the published pose. **Needs re-check.** | `battleGatoRender`, `battleEnemyGenRender` in `src/battle.c` | M |
| B7 | **Smaller picture items** from the 2026-10-02 sprite-latency work (the battle NMI composes OAM after its DMA, `CF/E58A` -> `CF/EAC5`): the damage digits' outline, and the tile/OAM split when an upload is skipped. That measurement was never written up as a dossier in the tree; writing it is part of the item. | Pictures in fights. **Needs re-check.** | [disasm/notes/battle_sprite_zorder.md](../disasm/notes/battle_sprite_zorder.md), `CT_BATTLE_ZTRACE` | S each |
| B8 | **The enemy tech name box's render time** (`C2/0009`, for example Yakra's "NeedleSpin" takes 3 frames), op `0F` box-only turns, and the names of reactive techs. | Battle text timing and content. | [disasm/notes/battle_yakra.md](../disasm/notes/battle_yakra.md) sec. 4 | M |
| B9 | **The battle-menu items from 2026-10-01**, re-checked 2026-10-11: the `$99E0` Wait gate is ported (`C1/1745-1748`); still open: the pending latch is the port's `pcInTurn` beside `$B188` (`C1/8570-8585`'s stores are not the latch yet, about 20 readers), and the boot's `$9931` runs one behind (the band's arm and first step share one tick; shared with the attract's boot, `battleBootBandArm`). | A box enqueued before the first action can come two frames early. | [disasm/notes/battle_menu_pass.md](../disasm/notes/battle_menu_pass.md) sec. 7 | M |
| B10 | **Status edge cases**, re-checked 2026-10-11: the Lifeline revive, an enemy's chaos or berserk turn, the lock's consumer and handler 12's tail are ported (S9.5, graded by `battle-status-machinery-vs-logical-oracle`). Left: the icon re-arm at an action's close lands 2-3 frames after the cartridge's (`C1/411D` runs before `C1/41BD` drops `$A013`). | A short icon timing. | [disasm/notes/battle_status_pipeline.md](../disasm/notes/battle_status_pipeline.md) S9.4 | S |
| B11 | **The AI condition walk's `C1/0003` entries.** **Done (2026-10-11)** for `0C`, `0D`, `0E`, `10` and `1F`: `C1/2986`'s simple sub-modes ported on WRAM or the live host, so `--sweepaieval` grades them (byte-exact). Left: `0F` (sub-mode 08, `C1/2C02`'s path test), which no script uses. | Only if a script ever needs `0F`. | [disasm/notes/battle_ai_interp.md](../disasm/notes/battle_ai_interp.md) sec. 19 | S |
| B12 | **The attract's 07D fight** was 10 frames short of the cartridge. The battle boot and exit were reworked afterwards (2026-10-03). | **Needs re-check.** | `tools/attract_divergence.py --visit 07D` | S to M |
| B13 | ~~**The status icons draw with a baked palette**~~ **Done (2026-10-11):** `battleStatusIconRender` reads the record's OBJ palette row from the uploaded CGRAM (`h->fxCgram`, or the `$2300` page with no host), as the bracket and the seats do. Both icon checks kept their picture hashes. | -- | -- | -- |
| B14 | ~~**Two generated headers have no reader**~~ **Done (2026-10-11):** `battle_end.h` and `battle_gato_song.h`, their generators and manifest entries are gone (no source read their tables; the animation engine draws those poses). Their recipes stay: other generators use them. | -- | -- | -- |

### Field

| # | Item | Why it matters | Where to start | Size |
|---|---|---|---|---|
| F1 | **The location-load clock.** The cartridge turns the NMI on at frame I + floor((225 x 1364 + W) / F), where W is the NMI-off work. The engine charges (I+1) + ceil, so each load runs one or two frames long. The missing pieces: price the init pass's ops (the VM ops each object runs at load, `C0/5910`), price its song posts (`C0/3C43`), and walk the reload pass's remaining arms (`C0/794E`, `C0/8385`, ...). | Mostly *timing only*, but it is the main remaining source of the attract demo drifting against its music. | [disasm/notes/field_load_cost.md](../disasm/notes/field_load_cost.md), [disasm/notes/attract_leg_clock.md](../disasm/notes/attract_leg_clock.md) sec. 7, `ct_field --passcost` | L |
| F2 | **Location 0E6: object 6 (Robo)** switches to idle (`$168C` 08 -> 00) one frame earlier than the cartridge, which pushes three NPCs' first frames one frame late. The engine's writer of `$168C` on that frame has not been found. | An animation frame in the attract. | `ct.exe --followvm --obj 6` on the cartridge under the rule; the follower arm `C0/A26B` and the idle chain `C0/A1E9`-`A21F` | M |
| F3 | **Location 120**: on the stairs, Lucca's text box opens 4 frames early (the order of the lead's move against the runner). | Script timing. | [disasm/notes/field_tick.md](../disasm/notes/field_tick.md) | M |
| F4 | **Pictures from the 2026-09-28 audit**: the ice crystal's content at 087, the Frog Squash backdrop colours alternating in pairs at 07D, and the 01C explosions' screen shake one phase off. | Pictures. Not derived yet. | `tools/attract_divergence.py`, the `attract-divergence` agent | M each |
| F5 | ~~**The Y menu when no box is open**~~ **Done (2026-10-14):** it is the party exchange, screen `$07` (G1a item 2). Left: scene 7's Y-reacting box is 3 frames late. | -- | [disasm/notes/menu2_form_screen.md](../disasm/notes/menu2_form_screen.md) 2.14 | S |
| F6 | **Op `FF 96`** is ported from the ROM (2026-10-11) but **not graded**. `C0/4181` spins on `$4210`, then `C0/0B4E` and `JML FD/C000`: the reset entry, so the game reboots cold. The VM parks on it (`rebootReq` 3) and the host takes the cold init `CF/E702`'s reset uses, at the vblank the DF/E1 reboot uses. The only site is scene 0AC obj9 fn1 `0x0416`, behind its battle (`D8 93 E0`) when `$7F01EC` (lost) is set. A cold `--scene 172` run (`CT_ACTIVATE=9:40 CT_POKEWRAM=7F01EC=01 TB_AUTOADV=1`) stops earlier, with obj9 waiting on `AB 0C` at `0x03CF`. | Grade it once a recipe reaches that fight; the `AB 0C` wait on a cold seed may be a finding of its own. | `src/ct_vm.c` (FF handler), `src/field.c` ('THE DF/E1 REBOOT') | S |
| F7 | **The lead's d-pad** still comes from the host's keys, not from the latched pad byte `$00F9`. | Faithfulness of input timing. | [disasm/notes/field_dash_run.md](../disasm/notes/field_dash_run.md) | S to M |
| F8 | **Two textbox counters**: the host's `tbAnim` and the ROM's `$0127` count the same thing. | Debt that can drift. | [disasm/notes/textbox.md](../disasm/notes/textbox.md) | S |
| F9 | **Leftover host re-derivations of the lead's plane** (`pz = zb & 3` at `--scene` spawn, door arrival, an old snapshot fallback).  | Debt that can strand the lead. | `src/field.c` | S |
| F10 | **Per-unit priority, unchecked cases**: the hidden pixel assumes BG1/BG2 are opaque there; offsets that are not multiples of 16, and sprite class 3, are not checked on the oracle.  | Pictures. | [disasm/notes/field_obj_unit_priority.md](../disasm/notes/field_obj_unit_priority.md) | S to M |
| F11 | **One VM, not two.** The game runs `ct_vm.c`'s `step()`; the micro-execution sweep grades `move.c`'s `CtFieldRunnerOne`. Op families are being moved into shared bodies (`vmSharedOp`). Finish so that what is graded is what runs. | Removes a whole class of "graded but not live" bugs. | [disasm/notes/vm_two_implementations.md](../disasm/notes/vm_two_implementations.md), [disasm/notes/live_vm_op_gaps.md](../disasm/notes/live_vm_op_gaps.md) | L |
| F12 | **The `$16xx` animation-timer divergence** in the `--vmdiff` OAM ratchet (`vmdiff-oam-lockstep`, which must only go down). | Placement residue. **Needs re-check** against the current count. | `VMDIFF_OAM=1` | M |
| F13 | **Owner's 2026-09-26 list**: the cathedral door that did not open properly (#16), and the door half of #18. The rest of that list is fixed or needs a save to reproduce. | User-visible. **Needs re-check.** | [disasm/notes/manoria_cathedral.md](../disasm/notes/manoria_cathedral.md), [disasm/notes/field_doors_occluders_decisionbox.md](../disasm/notes/field_doors_occluders_decisionbox.md) | S to M |
| F14 | **Location 049 (a chest walk)**: from a recipe-made state the lead takes one more diagonal step at f206 than the cartridge. | Movement. Found by the fixture twins (2026-10-11). | `tools/fixtures/field.json` (`s49_chest_walk`), `tools/chest_box_close_cmp.py` | S to M |
| F15 | **Location 434's decision box**: obj10's 4-frame idle loop is not followed from a seeded state; the box opens at f583 on the port, f582 on the cartridge. Note (2026-10-11): `CT_FIELDLOCK`'s in-process oracle always runs in REAL TIME (only `ct.exe` calls `snes_setLogicalFrames`), so this check grades against the real-time cartridge; on it the twin reconverges after f582-f626. Turning the rule on there (a one-line prototype) makes even the capture diverge at f99 (the box one frame apart): the in-process lockstep has no P+2 / key+1 alignment for the logical oracle. Give `CT_FIELDLOCK` that alignment first, then re-grade the twin under the rule. | A frame of script timing. | `fxf_s434_decision` (NOT YET USED), `lockstep-decision-box-s434-clean`, `field.c` `CT_FIELDLOCK` | M |
| F16 | ~~**The cathedral's frog (m81)**~~ **Done (2026-10-11), not a port bug:** the twin diverged only against the real-time cartridge (Frog's `EA 00` song post at f67 cost that run one frame of SPC wait; the rule does not charge it). Under the rule (`CT_ORACLE_LOGICAL=1`) the twin and the capture are both CLEAN; `vmdiff-manoria-frog` now runs there on `saves/field_re/m81_frog.state`. Seen in passing: `$1180,X` of a called object is published when it is next visited, where the ROM stores it in the call (`C0/6027`); not pinned. | -- | -- | -- |
| F17 | **Location 468's stairs**: the port's load clock does not follow a load that costs one frame more (27 frames apart where the capture had 26). | Part of F1. | `fxf_scene468_stairs` (NOT YET USED) | M |
| F18 | **The attract's entry 3 stage** runs one frame later from hand-off +38 on today's rule cartridge than on the old rule capture the port matches. | A real port-vs-current-rule divergence, one frame. | `attract-entry3-stage-picture-vs-oracle`, `saves/attract_entry3_handoff_rule_f8859.state` | M |
| F19 | **`C0/21E1`'s field window effects 4, 5 and 7-0D** have no port (found 2026-10-11). Entry 4 (`C0/232A`, set by `FF 97`: `C0/4191` = `FF 90`'s stores with `$0139 = 04`) is the iris's growing circle (`C3/0008` with A = 1, which `ctIrisCircle` already ports) made the COLOUR window (`$0BDB = 20`, `$0BDE = 12`); entry 5 (`C0/238A`, `FF 98`) shrinks it and ends in `C0/23E2` (ported, `ctLineClose`). Entries 7-0D (`C0/2514`, `2589`, `260E`, `264F`, `2329`) come from `FF 99`-`9B` and the parking `FF 9E` (`C0/41C8`: waits for entry 0C to end in 0D). Script sites: `FF 97` x4 (scenes 051, 055, 0A3), `FF 98` (051), `FF 99` (109), `FF 9B` and `FF 9E` (0AC). The VM side is small; the picture needs the colour-window renderer (`fieldColWindowRows`) to read the table HDMA channel 5 reads (`$7F:1133 = E4 <buf> F0 <buf+200> 00`, built by the translated `FD/C2C1`, the set `$0153` picks) instead of the quad's `$0350` pointer, since entry 4 puts its circle where the iris does (`$0356`). Also extend `--sweepiris` to `C3/0077`'s argument 1. | Cutscene pictures in several scenes. | `src/ct_vm.c` (`ctIrisStep`, the FF handler), `src/field.c` (`fieldColWindowRows`, `fieldBgWindowRows`), [disasm/notes/field_iris_window.md](../disasm/notes/field_iris_window.md) | M |

### Menus and saving

| # | Item | Why it matters | Where to start | Size |
|---|---|---|---|---|
| M1 | **Saving works.** `CtMenuSaveWrite` (`FF/FA..`) packs the game into the cartridge's SRAM layout, with its checksum, in `ct_sram.srm`, and the load screen reads it back. The two gaps recorded when it landed now appear in the code: the overwrite prompt (screen 6, sub-state 2, `C2/CF19`) and the saturating `$299C` increment before the pack (`FF/FA05`). | **Re-checked 2026-10-11, done:** `menu-save-overwrite-prompt-frames-vs-oracle` grades the prompt against the logical oracle, and `field-menu-save-jingle-and-write` pins the `$299C` increment (`FF/FA05`) with the pack. | [disasm/notes/save_write.md](../disasm/notes/save_write.md) secs. 0 and 5.4; `src/menu.c` `CtMenuSaveWrite` | -- |
| M2 | **Equip changes, Config toggles, and dual and triple techs** were listed as "not done" in older documents. They are now implemented (`CtMenuEquipApply` `C2/9CC1`, the Config toggle machine, the dual and triple tech lists in battle and menu). | **Re-checked 2026-10-11:** Equip and Config are pinned against the oracle (`menu-equip-*-vs-oracle`, `menu-config-*-vs-oracle`, `shop-equip-mode-vs-oracle`); dual techs by `field-menu-tech-dual-list` and the attract's X-Strike checks. Left: no check runs a TRIPLE tech; it needs a three-member party with the techs learned (a recipe). | [disasm/notes/menu2_equip_change.md](../disasm/notes/menu2_equip_change.md), [disasm/notes/menu2_config_toggles.md](../disasm/notes/menu2_config_toggles.md), [disasm/notes/battle_dual_triple_techs.md](../disasm/notes/battle_dual_triple_techs.md) | S |

### Story coverage

| # | Item | Why it matters | Where to start | Size |
|---|---|---|---|---|
| G1 | **No end-to-end playthrough has been graded.** Each subsystem is checked on its scenes; later chapters were reached only where the owner played or a capture existed. | The goal is a game that plays start to finish. | Recipes (1.1) make long drives possible; a chain of recipes through the story is the natural vehicle. | XL |
| G1a | **What stops a playthrough today (census 2026-10-14).** Four structural gaps, found by a marker census of `src/` and checked against the ROM: (1) **`$C3` sequences**: only entries 1 (credits), 2 (title) and the attract's `$60`/`$63` run (plus the gate warp `$80-$8F`); every other `FF nn` (`nn` < `$80`) prints `overlay not ported: holding` and the game stops there for good -- `FF 00`/`FF 09` (scenes 223/225, the motorcycle race, entry 0), `FF 23` (102-107, Queen Zeal's scenes), `FF 24` (301), `FF 26` (432 = loc 1B0), `FF 07` (476), `FF 2A`/`2B`/`2D` (81, 261), `FF 2E`/`2F` (90), `FF 7F` (79, past the table). (2) ~~**The reserve exchange**~~ **Ported 2026-10-14** (menu mode 5, screen `$07`, `C2/C7A6`: the Y button at storyline >= 0x49 and the scripts' `C8 00`), graded byte for byte by `menu-exchange-vs-oracle`; its picture lacks the BG3 wave, the prompt's text and the hooded figure ([menu2_form_screen.md](../disasm/notes/menu2_form_screen.md) 2.14). (3) **Vehicles on the overworld**: `$027E` 4/5 and `$0280` != 0 (`C2/342D` / `345D`) are not derived, so the Epoch never flies; no capture rides one. (4) **A lost scripted fight** (`$2A1F` bit 5, `C0/0304`'s continue) has never been exercised. Checked and NOT blocking: every PC tech (types 1/3/5/6), every enemy attack and tech (types 0/3/4, >8), every extra effect a PC tech names (2, 10, 11, 12, 18) and every effect kind the ROM uses (0-3, 5-8) are ported; `battle_ab.py census` gives `effect SAME` on all 56 single techs. Twenty PC techs (Cyclone 01, Fire 2 14, Dark Matter 37, several combos) name an area shape `battleAreaC586` does not run: the menu's target list is kept, a fidelity item, not a blocker. | Each of (1)-(4) ends a playthrough or loses a story choice. | `field.c` (`overlay not ported`), `title_intro.c` `ctSeqEntry`, `menu.c` (Form, screen 5), `world.c` 1316, `field.c` 8661 | (1) XL, (2) M, (3) L + a capture, (4) S to M |
| G2 | **Scenarios blocked on captures** (from 2026-09-06): a battle reaching the victory early-out, the `CD/0E05` death-sound poster, an accessory with an effect (`C1/FC7F` bit 6), a status timer actually running (`$F0` arms), op `C8` with operand `$00`, the new-game boat cutscene. | Unreached code. Recipes can now reach them. **Needs re-check**: some may have been reached since. | [disasm/notes/port_completion_plan.md](../disasm/notes/port_completion_plan.md) sec. 3 | M each |
| G3 | **The self-declared marker pass.** About 170 comments in `src/*.c` say "not ported", "unported", "is an invention", "not modelled" or "heuristic". Many are stale. Classify each as landed, really open, or not portable. | It finds both real gaps and stale claims cheaply. | [disasm/notes/port_completion_plan.md](../disasm/notes/port_completion_plan.md) sec. 0.1 | M |

---

## 3. Debt: emulation and bakes to retire

The 2026-10-09 decision: nothing in the port should be emulation, and simpler mechanisms are preferred where they
keep the behaviour. Ask the owner before a large rewrite.

| # | Item | Why it matters | Where to start | Size |
|---|---|---|---|---|
| D1 | **The SPC700 and DSP.** `ct_field` plays the music by running the ROM's sound driver on LakeSnes's SPC700/DSP core. A native implementation would interpret the driver's song format and voices in C. What counts as acceptable (sample-exact output, or the same notes and timing) is the owner's call. | The largest piece of emulation left, and it carries most of the timing machinery (handshake pricing, the bank `$C7` exception). | [disasm/notes/audio_boot.md](../disasm/notes/audio_boot.md), [audio_song_data.md](../disasm/notes/audio_song_data.md), [audio_commands.md](../disasm/notes/audio_commands.md), [audio_driver_mode.md](../disasm/notes/audio_driver_mode.md) | XL |
| D2 | **The `$C3` sequence stages** (`src/xlat/seq_c3.c`): the motorcycle race and the other sequences, translated instruction by instruction over the vendored 65816 and PPU (`src/seq_ppu.c`). A native port has to reproduce the race's sprite cadence and the transitions' edges as behaviour, without a cycle clock. | Emulation; and no mod (frames between) works there. | [disasm/notes/sequence_table.md](../disasm/notes/sequence_table.md) sec. 11 | L |
| D3 | **The window wipe** (`wipe_c3.c`) and **the overworld window** (`owwin_c6.c`): the same routine `C3/01E4` with its beam-reading seed. Under the virtual beam the seed is deterministic, so a native port is feasible. | Emulation. | [disasm/notes/field_window_wipe.md](../disasm/notes/field_window_wipe.md) | M |
| D4 | **The gate warp** (`gwarp_fd.c`, 880 frames). | Emulation. | [disasm/notes/gate_warp_fd_family.md](../disasm/notes/gate_warp_fd_family.md) | M to L |
| D5 | **The HDMA table builder** `FD/C2C1` (`hdma_fd.c`), added for a battle exit's timing. `ct.exe --hdmabuild` already grades it, so a native version can be checked directly. | Emulation for a timing-only purpose. | [disasm/notes/battle_ai_interp.md](../disasm/notes/battle_ai_interp.md) sec. 25.3 | S to M |
| D6 | **Cycle pricing** of loads, the menu open and the sound transfer (`field_cost.c`, `menu_cost.c`, the listing walk in `audio.c`). Under the rule, timing comes from the ROM's waits. Where a priced stretch only moves a frame boundary and changes no behaviour, a simpler model may do. | Simplification the owner asked for. Measure first which behaviours depend on it. | [disasm/notes/field_load_cost.md](../disasm/notes/field_load_cost.md) | M to L |
| D7 | **Baked battle tables to native rendering.** Some `src/data/` headers are pictures or poses captured from the oracle (battle stance, idle and attack keyframes, the HUD strip's chrome, the GATO poses). Several are already retired to fixtures by the animation engine. Drawing everything from ROM data through the engine's own renderers removes captured states from the build. | Fewer generated headers that need recipes; no special cases. | the `#include "data/..."` comments in `src/field.c`; [disasm/notes/battle_anim_engine.md](../disasm/notes/battle_anim_engine.md) | L |
| D8 | **Split the monoliths.** `field.c` (~18,800 lines), `battle.c` (~22,300) and `main.c` (~16,400) hold several systems each. A behaviour-preserving split plan exists (written when the files were smaller). | Readability for contributors. | [disasm/notes/structure_audit.md](../disasm/notes/structure_audit.md) | M per step |

---

## 4. Mods

Mods are outside the ROM mandate but must leave the vanilla game byte-identical when off.

| # | Item | State | Where to start | Size |
|---|---|---|---|---|
| X1 | **Frames between for translated stages** (the race, the gate warp, other `$C3` sequences). They run on the vendored PPU and are shown as still frames between logic frames. | Not started; easier after D2/D4. | `src/seq_xlat.c`, `CT_NATIVE_CENSUS` | M to L |
| X2 | **The race's wipes in widescreen** are not scaled to the wide picture. | Open. | `snes/ppu.c` (`g_ppuWideSide`), `CT_WIDESHOTS` | S to M |
| X3 | **Menus, shops, the naming and load screens** are pillarboxed in widescreen. | By design so far; a wide layout is a design question. | `src/menu.c`, `hostFitWidth` in `src/field.c` | M |
| X4 | **Rooms no wider than 256 pixels** show black sides in widescreen, because the room's scroll mask ends there. Filling them needs a creative choice (the owner's call). | Open. | `fieldWideMask` in `src/field.c` | M |
| X5 | **Battle effect layers in widescreen.** The arena, the colour-math flashes and the HUD band now reach the sides. Full-screen BG3 effects, row shakes and other effects built 256 wide may not; each effect is its own work. | Partly done. **Needs re-check** per effect. | `CtBattleFxLayerComposeWide`, `tools/widescreen_check.py forest` | M |
| X6 | **The overworld's frames between flatten sprite layers**: a sprite moving off another uncovers the map, not the sprite under it. The field already keeps every sprite's pixels. | Fix if reported. | `src/motion.c` (`spx`) | S |
| X7 | **Zero-latency frames between** (extrapolation instead of interpolation). | Skipped on purpose: extrapolation overshoots. Only if the owner asks. | `src/motion.c` | M |
| X8 | **Tooltips.** Each mod must say in the overlay what it changes. | **Re-checked 2026-10-11, done:** Widescreen, Narrow rooms, Frames between and Internal resolution carry tooltips, the Free sync mode its inline text; the other options (scale, aspect, filter, VSync, backend, audio, controls) change no game behaviour. Keep it so for each new mod. | `src/overlay.cpp` | -- |

---

## 5. Tooling and documentation

| # | Item | Why it matters | Where to start | Size |
|---|---|---|---|---|
| T1 | **Continuous integration.** No CI exists; `tools/preflight.py` is a local gate. A public CI cannot hold the ROM, so it can build (Windows and Linux), run the leak scanner on the tree, and run whatever needs no ROM. | Protects the tree from ROM leaks and build breaks. | `tools/preflight.py`, `tools/release/scan_rom_leaks.py` | M |
| T2 | **Linux parity.** `tools/build.py` and the `get_*.ps1` fetchers are Windows-only; `regress_checks.json` calls `build/ct*.exe` (Linux needs symlinks); OpenMP is enabled only for MSVC. A Linux contributor should get the same one-command flow. | Contributors on Linux. | [docs/INFRA.md](INFRA.md#build) | M |
| T3 | **The suite's attract vehicle.** The suite starts the attract cold at scene 433, so its play-clock seeds sit a constant 22 ticks off the cartridge's; only `--boot` matches. Switching re-anchors about 66 checks. | The owner's decision ([DECISIONS.md](DECISIONS.md#decisions-waiting-for-the-owner)). | [disasm/notes/field_f8_cursor_play_clock.md](../disasm/notes/field_f8_cursor_play_clock.md) | M |
| T4 | ~~**The full battle census writes about 33 GB**~~ **Done (re-checked 2026-10-11):** `tools/battle_ab.py` deletes each case's every-frame thumbnails, oracle sidecars and seed once it has read them (since 2026-10-08; `BATTLE_AB_KEEP=1` keeps them); only the two final frames stay. | -- | -- | -- |
| T5 | **`docs/TESTING.md`**: its prerequisites and tiers are current (2026-10-12: setup, fixtures, 587 quick / 240 full). Left: a SKIP does not tell a recipe-made fixture (class B) from a private capture (both print the missing file); it could name `python tools/fixtures.py make <path>` when a recipe exists. | Accuracy. | `tools/regress.py`, `tools/fixtures/*.json` | S |
| T6 | **Root clutter in the private tree**: the tracked suite logs are gone (2026-10-10); tools still drop `.state` captures and probe output at the repository root (ignored, never exported). Tools should write under `build/`. | Hygiene. | `.gitignore` | S |
| T7 | **Notes written in English.** The dossiers are English; the agents' working memory is not part of the repository. Keep it that way: decisions and open items that matter belong in `disasm/notes/`, this roadmap, or [DECISIONS.md](DECISIONS.md). | Contributors can read everything that matters. | | ongoing |
| T8 | ~~**`options-widescreen-view-equals-the-256-picture` fails**~~ **Done (2026-10-11):** bisected to `32e37124`, a vanilla battle fix that moved the forest fight's poses at f300 in both pictures; the count at the view's own position is unchanged (12687). Re-pinned. | -- | -- | -- |

---

## 6. What is not portable (the floor)

Stated so that completion can be declared rather than chased:

- **Field ops `DF`/`E1`** reset the stack pointer and never return to the runner. The engine runs them as the
  `C0/0000` reboot they are, but `--sweepops` cannot grade them. **Op `A3`** is the ROM's panic stub. Together these
  are 88 of the VM's static sites.
- **DMA is invisible to the write census** (`snes/dma.c` writes the bus directly), so a census reports DMA as a
  class.
- **`ct_lz_decompress` is a rewrite**, graded on its output (the whole decompressed heap and the size report), not
  on the ROM's scratch bytes.
- **Timing-only residuals from hardware speed** (B1, B2, part of F1) are documented rather than reproduced,
  per the 2026-10-09 decision.
