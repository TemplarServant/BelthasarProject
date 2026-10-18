# Structural audit — modularising the monoliths (BEHAVIOR-PRESERVING ONLY)

**Scope.** This is a *structure* front, not an RE front. Nothing here proposes a behavioral change:
every move below is "cut a contiguous, self-contained group of definitions from A into new file B,
add header C, add B to the target's source list". No constant, no gate, no RAM read changes.
The no-invention mandate still binds: **if a move would require changing what the code computes,
it is not in this plan.**

**Method / citations.** Everything is derived from the repo itself, with the exact commands recorded:

* Function inventories: `grep -n -E '^(static |void |int |unsigned |uint[0-9]+_t |char |float |double |const |bool |[A-Za-z_][A-Za-z0-9_]* )[A-Za-z0-9_ \*]*\(' src/<file>.c`
* Cross-group coupling: a purpose-written mapper (scratchpad `dep.py`) that (a) finds file-scope
  definitions by column-0 brace matching, (b) for a set of line-range GROUPS reports, per group,
  every file-scope symbol it references that is **defined outside** the group. Comments and string
  literals are stripped first. Invocation shown per section below.
* Build wiring: `src/…` ↔ target mapping read from `CMakeLists.txt` (lines 14–32 = `ct`, line 56 = `Belthasar`,
  line 46 = `ct_viewer`, line 69 = `ct_world`).

**One known false positive** in the mapper output below: `uint8_t(fn@2477)` in `src/field.c` — the
column-0 line `static const uint8_t (*ctAnimengGatoSheet(void))[32] {` parses as a definition named
`uint8_t`. Every group therefore "needs `uint8_t`". Ignore it; it is not a real edge.

---

## 0. The measured shape of the problem

`wc -l src/*.c src/*.h` (bytes from `ls -la src/`):

| file | lines | bytes | links into |
|---|---:|---:|---|
| `src/field.c`  | 7359 | 551 715 | `Belthasar` only |
| `src/main.c`   | 5154 | 295 339 | `ct` only |
| `src/battle.c` | 5075 | 409 761 | `Belthasar` only |
| `src/ct_vm.c`  | 3051 | 231 030 | **`ct` AND `Belthasar`** |
| `src/menu.c`   | 1487 | 102 244 | `Belthasar` only |
| all other `.c` | ~3 500 | — | mixed |

Header census (`ls src/*.h | wc -l` → **67**; classified by counting `^…\(`-shaped declarations vs
`^\s*static const ` lines):

* **49 headers are pure DATA BLOBS** (zero function declarations, ≥1 `static const` array),
  totalling **2 092 321 bytes** — i.e. ~2.0 MB of baked ROM/oracle data sitting flat in `src/`.
* **18 headers are real API headers** — the complete list: `assets.h`, `battle.h`, `boot.h`,
  `collision.h`, `ct_cpu_infra.h`, `ct_lake_state.h`, `ct_owobj.h` (ABI `#define`s only), `ct_vm.h`,
  `harness_cli.h`, `harness_drive.h`, `input.h`, `lz.h`, `menu.h`, `move.h`, `oam.h`, `player.h`,
  `scene.h`, `world.h`.

So `src/` is 21 `.c` + 67 `.h`, of which **73 % of the headers are generated data**, and 5 `.c` files
carry 22 126 of ~25 600 lines of hand-written C.

**Nothing in the plan below changes any of that data.** The blobs move directory only (§6).

---

## 1. Section inventory

### 1.1 `src/field.c` (7359 lines) — SIX systems in one file

`python dep.py src/field.c '[["world",117,333],["carry",334,348],["world2",349,583],["coll",585,727],["plot",728,734],["portal",735,783],["text",784,1309],["mid",1310,1606],["battlerender",1607,3979],["bridge",3980,4406],["rest",4407,7359]]'`

| lines | group | system | contents | outward coupling (mapper) |
|---:|---|---|---|---|
| 1–79 | prologue | — | file banner + **43 project `#include`s** (`grep -cE '^#include "' src/field.c` → 44, one of which is `SDL.h`); **33 of the 43 are baked data blobs** | — |
| 80–116 | `dbg` | harness | `dbg` env-cache struct + `dbgInit()` (`getenv` once, hot loops read `dbg.*`) | none |
| 117–333 | `world` | **overworld render** | `World` struct, `worldBg3Px`, `worldPalRefresh`, `worldInit`, `worldEnterAt`, `worldOverlayIntroSeed`, `OW_FERRY_BOAT[0x40]`, `NG_PAN_END` | **none** |
| 334–348 | `carry` | field session | `g_owNoMarker`, `g_itemDemo`, `g_sheetCarry[0x280]`, `g_sheetCarryValid` — misfiled *inside* the world block | — |
| 349–583 | `world2` | **overworld render** | `worldDrawAmbientSprites`, `worldCompose` | `worldBg3Px`; **`g_owNoMarker` (line 533)** |
| 585–727 | `coll` | **field collision** | `Coll` typedef, `g_collRam[0x13000]`, `g_fc`, `fieldCollCopyTiles`, `fieldTileWalkable`, `fieldNpcTileSolid`, `fieldTileFetch`, `fieldPlayerCollide` | **none** |
| 728–734 | `plot` | render prim | `plot()` (7 lines, misfiled between collision and the portal) | none |
| 735–783 | `portal` | field render | `portalBg3Px`, `portalInside`, `footPrio` | none |
| 784–1309 | `text` | **CT text/window/menu-font** | dialogue font (`cGlyph…drawLines`, `drawChoiceBox`), CT window 9-patch (`drawCtWindow`, `drawCtWindowRect`), menu-font glyphs (`drawMenuGlyphP/…Dim/…Dim2/…Yellow`), naming screen (`drawNameScreen`, `NAME_GRID`, `asciiToCt`, `ctToAscii`), `drawAtbSelScreen`, `drawWorldLabel`, `drawOwLocName`; owns statics `tbClipY0/1`, `tbWinPx`, `tbWinH`, `tbCream`, `tbAnimBand`, `nsPortrait[7]`, `nsHand`, `nsTri`, `nsWin`, `nsMsg`, `menuFont` | `plot` |
| 1310–1443 | `sprite` | field render | `animFrames`, `AnimPlay`, `animTickActor`, `drawSprite` | `plot`, `portalInside` |
| 1443–1452 | prio | field render | `fieldPrioLayer` | none |
| 1452–1606 | `fieldload` | field scene | `Field` struct, `fieldFree`, `fieldLoad` | `fieldNpcTileSolid`, `g_fc`, `g_itemDemo`, `g_sheetCarry*` |
| **1607–3979** | **`battlerender`** | **battle render (2373 lines!)** | `CtBattleHudRender`, `battleStanceRender`, `battleGatoRender`, `battleHetakeRender`, `battleEnemyGenRender`, `battleDissolveRender`, `battleAttackRender`, `battleMarleAttackRender`, `battleEndPoseRender`, `battleFxPopupsRender`, `battleWhirlRender`, `battleIceRender`, `battleSongFxRender`/`SongNotesRender`, `battleCycloneDarkenRender`, `battleCritFlashRender`, `battleEntranceRender`/`Mask`, `battleCronoIdleRender`, `battleBracketRender`, `battleTargetCursorRender`, `battleResultsRender`, the whole **anim-engine** (`ctAnimeng*`, `ctPcPoseBind`, `g_gatoSheetRom`, `ctEnemyShCache`) + `ctAnimengSelfcheck`/`ctEnemyStageSelfcheck` | `plot`; `cGlyph`, `cDrawGlyph`, `drawCtWindow`, `drawMenuGlyphP`, `menuFont`; and `tbAnimBand/tbClipY0/tbCream` **from `battleResultsRender` only** (2438/2446 save-and-restore) |
| 3980–4406 | `bridge` | battle↔field | `fieldBattleFinish` (C0/18B9 ack + C0/0283 outcome), `battleShotAtCb`, `fieldBattleConsumer` ($0117 consumer), `MtSnap`/`mtSnapshot` | all of `battlerender`; `g_fc` (debug trace 4338–4346 only) |
| 4407–7359 | `main` | **SDL app + harness (2953 lines)** | `main()`: argv parse (4419–4496), one-shot probe/test modes (4497–5072), then the single **frame loop** (5073–7359) containing the fade machine (5104), overworld mode (5180), menu phase machine (5425–6124), ATB selector (6125), name entry (6174), battle phase (6545), compose (6793), textbox overlay (7093), stance/HUD overlays (7129/7179), screenshot dumps (7223/7264) | everything above |

`field.c` also carries a **verbatim duplicate** of `main.c`'s dialogue-font helpers — its own comment
says so (`src/field.c:784`: "ports the cGlyph/cGlyphW/cText helpers from main.c runField").

### 1.2 `src/battle.c` (5075 lines) — ONE tightly-meshed system + one detachable data API

`python dep.py src/battle.c '[["romdata",1,412],["stage_dbg",413,542],["helpers",543,650],["ai",651,1008],["queue_menu",1009,1671],["damage",1672,2070],["fx",2071,2399],["choreo",2400,3021],["animparse",3022,3371],["trig_mover",3372,3762],["sched",3763,4322],["rewards",4323,4486],["loop",4487,5075]]'`

| lines | group | contents | outward coupling |
|---:|---|---|---|
| 1–393 | **`romdata`** | `gRom`/`gRomLen`, `CtBattleLoadRom`, `CtBattleRom`, `CtBattleFieldEnemyRecs`, `CtBattleEnemySheetSrc`, `CtBattleDecompressEnemySheet`, `romRd24/16`, `CtBattleEnemyPoseStage`, `…IdleRest`, `…IdleGather`, `CtBattlePcPoseStage`, `…PcIdleWin`, `…PcWinPose`, `…PcWinDwell`, `…PcE3Rec`, `…PcSheetTile`, `CtBattleEnemyE3Rec`, `…EnemyPalRom`, `…EnemyIdleWin`, `ctChar`, `CtBattleEnemyName` | **ZERO** |
| 394–542 | `stage_dbg` | `kPcBlock`, `kBattleStatDerive`, `ENEMY_BLOCK`, `stageAll` (FD/A982 chain), `debugEnd` (C1/8357) | `gRom` (453/454 only) |
| 543–650 | `helpers` | `battleBlock`, `battleIsPc`, `battlePresent`, `battleReload`, `battleRng`, `battleRngCrit`, `battleFirstEnemy(Slot)`, `battleTargetNav`, `battleSelectedEnemy`, `battleLeadPc`, `battleAlive` | zero out, **8 groups in** |
| 651–1008 | `ai` | the bank-0C AI interpreter (`aiCondEval`, `battleAiTarget`, `battleAiExecSection`, `battleAiRunTurn`, `battleAiEval`, `battleAiAttemptResolve`, `battleEnemyHasCounter`) | helpers, romdata |
| 1009–1671 | `queue_menu` | confirm queue ($A6D9), nav, teardown (C1/1BAA / C1/356D), trace, `battleCommitPcAction`, hit-set, Tech/Item submenus | ai, helpers, romdata |
| 1672–2070 | `damage` | `battleComputeDamage/TechDamage/ItemEffect`, `battleApplyDamage`, `battleCharmRoll`, `battleStatusRider` | ai, helpers, romdata |
| 2071–2399 | `fx` | `battleAdvanceDissolves`, `battleFlashPal`, `battleKnockVector`, `battlePopupOrigin`, `battleSpawnPopup` | helpers |
| 2400–3021 | `choreo` | PC walk/attack (`battlePcWalkArm/Tick`, `battlePcAtkArm/Tick/RunTick`), bank-CE tech route parser/ticker, `battleSkeleton` | helpers, romdata |
| 3022–3371 | `animparse` | `battleAnimOpLen/Handler/Class`, `battleAnimParse`, `battlePcAnimSpan/TechSpan/ItemSpan`, `battleActionSpan` | **ZERO** |
| 3372–3762 | `trig_mover` | `battleAtan2`, `CtBattleAimFacing`, `battleTrigMul`, box/overlap, `battleMoveArm/Approach/Return/Tick`, `CtBattlePcFacing`, `battleMoveInit` | ai, choreo, damage, helpers, queue_menu |
| 3763–4322 | `sched` | `battleOpenAction`, `battleDispatchScheduled`, `battleReactiveCounterTick`, `battleThinkTick`, `battleAdvanceAction` | 7 groups |
| 4323–4486 | `rewards` | `CtBattleLevelUp`, `CtBattleTechLearn`, `battleResolveDrops`, `battleBuildResults` | helpers, romdata |
| 4487–5075 | `loop` | `CtBattleBegin`, `CtBattleTick`, `CtBattleRun` | 6 groups |

**Verdict: `battle.c` below line 394 is a genuine single machine.** Only `romdata` (1–393) and
`animparse` (3022–3371) have zero outward edges. `helpers` is the hub everything calls. Splitting
`ai`/`damage`/`sched`/`loop` apart would require exporting ~40 internal functions and gains nothing —
**NOT SPLITTABLE cheaply; do not attempt.**

### 1.3 `src/ct_vm.c` (3051 lines)

`python dep.py src/ct_vm.c '[["core",1,104],["loadmirror",105,268],["initvm",269,421],["sched_state",422,571],["effect",572,803],["mover",804,1066],["step",1067,1573],["create",1574,1805],["wram",1806,1997],["camera",1998,2211],["tick",2212,2414],["api",2415,2488],["followers",2489,2680],["api2",2681,3051]]'`

| lines | group | contents | outward coupling |
|---:|---|---|---|
| 1–104 | `core` | `OPLEN[256]`, `opLen`, `dbgFlags`, `Mem`, `memInit`, `r8/w8/r16/w16`, `cmp_`, `pcSprite` | fwd decls only |
| 105–268 | `loadmirror` | op-0x81/82/83 WRAM load mirrors + gfx/palette allocators (`palSlotMirror`, `loadGfxMirror`, `npcLoadMirror`, `loadMonMirror`) | core |
| 269–421 | `initvm` | `runFunc`, `CtVmResolveActors` (the init-subset interpreter) | core, loadmirror, mover |
| 422–571 | `sched_state` | `CtSS`, **`struct CtScene`** (the whole private state) | none |
| 572–803 | `effect` | `fpPtr`, `faceMove`, `applyEffect` (the op-0x88 / effect ops) | core, loadmirror, mover |
| 804–1066 | `mover` | C0/4E73–5215 + C0/ABA2 atan + C0/ACFD step, `npcSolidTile`, `moveOp` | core, effect |
| 1067–1573 | `step` | `reqOp`, `palAssignVisible`, **`step()` = the 463-line opcode switch** | core, effect, loadmirror, mover |
| 1574–1805 | `create` | `scanForTable`, `captureTable`, `CtSceneCreate`, party/config carry, `CtSceneSetAtbMode` | core, effect, initvm, step |
| **1806–1997** | **`wram`** | the `--vmdiff` WRAM bridge: `CtSceneScriptDiffWram`, `CtSceneSeedFromWram`, `CtSceneDiffWram`, `CtSceneObjIp` | **`opLen` only** |
| **1998–2211** | **`camera`** | the UNIFIED field camera: `camS8/Get16/Put16`, `camGate99DE`, `camSeek91AC`, `camCommit93E1`, `camAlign885A`, `CtSceneCameraTick/Entry/Input/Px` | **ZERO** |
| 2212–2414 | `tick` | `CtSceneTick` (scheduler cadence, visit budget) | camera, core, mover, step |
| 2415–2488 | `api` | actor accessors (`CtSceneActorCount/Actor/PalRot/PalBlob/PortalDisc/…`, `CtSceneSetActorPos(F)`) | `w8` |
| **2489–2680** | **`followers`** | `CtSceneDebugMakeFollower`, `CtSceneUpdateFollowers` (conga-line), `CtSceneSetButtons`, `CtSceneRegroupFollowers` | `w8`, `CtSceneSetActorPos` |
| 2681–3051 | `api2` | battle bridge (`CtSceneBattleRemoveObj/Return`), activate/touch, textbox, naming, names, poll, F5/F9 state, anim accessors, `CtSceneFree` | `fpPtr`, `w8` |

**The single blocker for every `ct_vm.c` split is that `struct CtScene` is private to the `.c`
(`src/ct_vm.c:431`; `src/ct_vm.h:74` only forward-declares it).** Move that declaration to a private
internal header and three groups become trivially separable.

### 1.4 `src/main.c` (5154 lines) — the ORACLE HARNESS

`python dep.py src/main.c '[["datmodes",1,378],["fbdraw",379,444],["render",445,601],["datutil",602,737],["oracle_dump",738,985],["fieldchecks",986,2537],["frame_oam_state",2538,2777],["navworld",2778,2972],["peek",2973,3065],["owprobes",3066,4302],["readwatch",4303,4419],["stream_follow",4420,4752],["dispatch",4753,5154]]'`

| lines | group | modes | outward coupling |
|---:|---|---|---|
| 1–378 | `datmodes` | `--assetinfo`, `--vm`, `--vmdemo`, `--vmdiff`, `--datfield` | `ctLoadStateOracle` |
| 379–444 | `fbdraw` | (helpers) `fbPlot`, `fbBox`, `cSolidShape`, `cZSolid`, `cGlyph`, `cGlyphW`, `cDrawGlyph`, `cText` | **ZERO** |
| 445–601 | `render` | `--field`, `--scene`, `--datonly` | fbdraw |
| 602–737 | `datutil` | `--datverify`, `--textdump`, `--lz`, `--sheetcheck`, `DEFAULT_ROM` | **ZERO** |
| 738–985 | `oracle_dump` | `dumpWram`, `--portcheck`, `--bootcheck`, `--cgram`, `--fb`, `--vram`, `--dumpflags`, `--playflags` | **ZERO** |
| 986–2537 | `fieldchecks` | 30 selfchecks: `--checkstage1 … --checksort` (collision/mover/player/actor/OAM-xform family) | **ZERO** |
| 2538–2777 | `frame_oam_state` | `--verifyframe`, `--checkoam`, `--dumpoam`, `--findoam`, `--savestate`, `--loadstate` | **ZERO** |
| 2778–2972 | `navworld` | `ctLeadPass`, `ctFindLeadSlot`, `ctEventWait`, `--navworld` | **ZERO** |
| 2973–3065 | `peek` | `ctLoadStateOracle`, `--peek` | **ZERO** |
| 3066–4302 | `owprobes` | 21 overworld modes (`--owwatch … --cgramseq`, `--checkfieldcam`, `--ppuregs`) | `ctFindLeadSlot`, `ctLoadStateOracle` |
| 4303–4419 | `readwatch` | `ctWramOff`, `ctParseAddr`, `ctParseWatch`, `ctHexRegion`, `--read` | `ctLoadStateOracle` |
| 4420–4752 | `stream_follow` | `ctStream`, `--followscene`, `--monitor`, `--followvm`, `--followtest`, `--teleport` | readwatch, `ctLoadStateOracle` |
| 4753–5154 | `dispatch` | `main()` — pure argv→`run*()` dispatch | everything |

**Why `main.c` is still 295 KB despite `harness_cli.c`/`harness_drive.c` existing.** Those two files
took out only the *declarative* parts:

* `harness_cli.c` (303 lines) = the **flag table + parser** only — 108 `{"--flag", nargs, kind, summary}`
  rows (`grep -c '^\s*{ *"--' src/harness_cli.c` → 108; kinds: 38 `selfcheck`, 27 `dump`, 25 `mod`
  (modifiers like `--frames`/`--out`), 18 `interactive` ⇒ ~83 real modes) plus `CtHarnessOpts` and
  `CtHarnessFlagCount/At`.
* `harness_drive.c` (50 lines) = the boot input script only.

**Every mode BODY (~100 `runXxx()` functions) is still in `main.c`.** That is the 295 KB. The extraction
that happened was "argv parsing"; the extraction that did *not* happen is "the modes themselves".
The good news: the mapper shows the mode bodies are almost perfectly independent — 7 of 13 groups
have **zero** outward edges, and the rest depend only on 4 shared helpers. `main.c` is by far the
easiest of the five files to split.

Also noted: `src/main.c:668` `static const char* DEFAULT_ROM = "Chrono_Trigger_USA_SNES.sfc";` is a
byte-identical duplicate of `src/harness_cli.h:11` `#define CT_DEFAULT_ROM`. Both are live
(`main.c:4827` uses the local, `harness_cli.c:122` uses the macro).

---

## 2. Coupling that BLOCKS a cheap split (state, not functions)

The constraint "the split must not require exporting new mutable global state" is violated by
exactly four things. Each has a behavior-preserving remedy; where it does not, the group is marked
NOT SPLITTABLE.

| # | state | defined | read/written by | remedy |
|---|---|---|---|---|
| S1 | `g_owNoMarker` (`int`) | `field.c:334` | `worldCompose` (533) **and** `main()` (5154, 5166, 5212, 5223, 5264, 5314) | **Relocate into the `World` struct** (`w->noMarker`). There is exactly ONE `World` instance (`field.c:4865 World wd;`) and it is `memset(&wd,0,sizeof wd)` at 4866, so the initial value 0 is preserved bit-for-bit. Not a new global — an existing shared-by-pointer struct. |
| S2 | `menuFont`, `tbWinPx`, `tbWinH`, `nsPortrait[7]`, `nsHand`, `nsTri`, `nsWin`, `nsMsg` (asset pointers) | `field.c:831/832/1090–1095` | assigned once in `main()` in one contiguous block, `field.c:4527–4543` (window 4528, portraits 4529–4532, hand 4534, triangle 4536, name-window 4538, name-msg 4540, menu font 4542); read by the text group and by `main:5689` (`mh.portrait = nsPortrait[portChar]`) | Keep them `static` **inside the new text TU** and add explicit setters (`CtFieldTextSetWindow/Portrait/NameAssets/Font`) + one getter (`CtFieldTextPortrait`) for `main:5689`. The whole assignment block moves into a single `CtFieldTextSetAssets(&assets)` call. This *reduces* global exposure; it does not create any. |
| S3 | `tbClipY0/1`, `tbCream`, `tbAnimBand` (textbox style) | `field.c:829/833/906` | written by `drawLines` (1007/1016/1021) and by `main` (7099–7113); **saved+restored** by `battleResultsRender` (2438/2446) | Add a 2-function accessor pair `CtTbStyleGet/Set(CtTbStyle*)` in the text header (the existing code already does exactly get-4 / set-4). Keeps the battle-render cut contiguous. |
| S4 | `g_fc` (`const Coll*`) | `field.c:639` | written 670 + 1497; read by `fieldTileFetch` and by the battle-bridge **debug trace only** (4338–4346) | Ship the `Coll` typedef in `field_coll.h` + a read accessor `FieldCollCur(void)`. Read-only export of an existing pointer. |
| — | `g_collRam[0x13000]`, `g_itemDemo`, `g_sheetCarry[0x280]`, `g_sheetCarryValid` | `field.c:638/335/347/348` | `g_collRam` is collision-private; the other three are used only by `fieldLoad` + `main` — **both stay in `field.c`** | no action |
| — | `battle.c` `helpers` group (543–650) | 11 tiny functions, called by 8 of 13 groups | — | **NOT SPLITTABLE cheaply.** Any battle.c split past `romdata` would export the hub. Leave it. |
| — | `ct_vm.c` `struct CtScene` | `ct_vm.c:431` | every group | **Enabler, not a blocker**: move the *declaration* to a private `ct_vm_internal.h`. A type is not state. |

---

## 3. Ordered split plan (each step independently buildable + verifiable)

**Validation after EVERY step, no exceptions:**

```
python tools/build.py            # trust the per-target artifact table, never the banner
python tools/regress.py --quick  # 113 of the 137 checks (43 drive build/ct.exe, 94 drive build/Belthasar.exe)
```

and, before starting, `taskkill`/`Stop-Process` any running `build\Belthasar.exe`
(memory: *Belthasar.exe auto-close (build lock)*) — a locked exe silently produces a stale binary.

A step is DONE only when the quick suite's pass/fail set is **identical** to the pre-step baseline.
Capture that baseline once, before step 1. Because every move is a pure textual relocation, any
regression = a real mistake in the move, never an "expected re-pin". **No `--pin` is legitimate
anywhere in this plan.**

`tools/build.py` re-reads the true per-target source lists straight out of `CMakeLists.txt`
(its own docstring: *"it reads CMakeLists.txt to learn the true per-target source lists (including
the file(GLOB) of snes/*.c for ct.exe)"*, and `parse_cmakelists()` at the end of the header), so
adding a source line is picked up automatically — **no extra build-script edit** and the
content-hash/skew trap keeps working across the move.

---

### Step 1 — `src/battle_romdata.c` (+ no new header) — **RISK: LOW**

* **Cut**: `src/battle.c` lines **34–393** (from `static uint8_t* gRom;` through the closing `}` of
  `CtBattleEnemyName`), plus the `#include`s it needs (`<stdio.h>`, `<stdlib.h>`, `<string.h>`,
  `"battle.h"`, `"lz.h"`).
* **New file**: `src/battle_romdata.c`. **No new header** — all 18 entry points are already declared
  in `src/battle.h:557–648`.
* **One required edit in the remainder**: `battle.c:453–454` currently reads the file-static `gRom`/
  `gRomLen`. Replace with the existing public accessor:
  `long rl; const uint8_t* rr = CtBattleRom(&rl); if (rr && rl >= (long)(ROM_ENEMY_STATS + 256*23)) { const uint8_t* rec = rr + ROM_ENEMY_STATS + (size_t)id*23;`
  — `CtBattleRom` (`battle.c:49`) returns exactly `gRom`/`gRomLen`, so this is provably identical.
* **Keep in `battle.c`**: `kPcBlock` (396), `kBattleStatDerive` (408), `ENEMY_BLOCK` — they belong to
  staging, not the ROM API.
* **CMake**: add `src/battle_romdata.c` to the **`Belthasar`** target only (line 56). *(If a future
  headless battle-data check is wanted, the same file could also join `ct` — do not do it now.)*
* **Result**: `battle.c` 5075 → ~4715 lines; a clean, dependency-free "ROM battle data" module.

### Step 2 — `src/ct_vm_internal.h` (header only, ZERO code moves) — **RISK: LOWEST**

* **Move** (textually, same TU semantics) out of `src/ct_vm.c` into a new private header:
  `Mem` (line 60), `CtSS` (426), **`struct CtScene` (431–571)**, `#define GLB/LOC`, `CTW_IPOFF` (1814);
  and add prototypes for the shared micro-helpers `opLen`, `r8/w8/r16/w16`, `cmp_`, `dbgFlags`,
  `fpPtr`, `w8`. Those helpers stay `static` in `ct_vm.c` **for now** (the header declares them only
  once a later step needs them; see steps 3–5, which each drop the `static` on exactly what they need).
* `ct_vm.c` gains `#include "ct_vm_internal.h"`. **Nothing else changes; no new .c file, no CMake edit.**
* This is the enabler for steps 3–5. Verify with build + quick suite anyway (it proves the header
  is self-contained and both `ct` and `Belthasar` still compile).

### Step 3 — `src/ct_vm_camera.c` — **RISK: LOW**

* **Cut**: `src/ct_vm.c` lines **1998–2211** (banner `UNIFIED FIELD CAMERA` through `CtSceneCameraPx`).
* **Outward deps: ZERO** (mapper output above). Entry points `CtSceneCameraTick/Entry/Input/Px` are
  already public in `ct_vm.h`; the four `cam*` helpers stay `static` in the new file.
* **New header**: none needed — `ct_vm_internal.h` (step 2) + `ct_vm.h`.
* **CMake**: `src/ct_vm_camera.c` must be added to **BOTH** `ct` (line 14–32 block) **and** `Belthasar`
  (line 56). This is the one trap in the whole plan: `ct_vm.c` is the only monolith linked into two
  targets. Forgetting `ct` gives `LNK2019 CtSceneCameraTick` on the headless build.
* **Result**: ~214 lines out; `field_camera_unification.md`'s mechanism becomes one addressable file.

### Step 4 — `src/ct_vm_wram.c` — **RISK: LOW**

* **Cut**: `src/ct_vm.c` lines **1806–1997** (the `--vmdiff` WRAM bridge: `CtSceneScriptDiffWram`,
  `CtSceneSeedFromWram`, `CtSceneDiffWram`, `CtSceneObjIp`).
* **Only outward dep**: `opLen` (`ct_vm.c:35`). Drop its `static` and declare it in
  `ct_vm_internal.h` as `int CtVmOpLen(const uint8_t*, int);`, or simply un-`static` `opLen` and
  declare it there. (A rename is optional cosmetics — prefer un-`static` + declare, fewer touched lines.)
* **CMake**: add to **BOTH** `ct` and `Belthasar`.
* **Result**: ~192 lines out; the oracle-differential bridge stops living inside the engine core.

### Step 5 — `src/ct_vm_followers.c` — **RISK: LOW**

* **Cut**: `src/ct_vm.c` lines **2489–2680** (`CtSceneDebugMakeFollower`, the conga-line comment block,
  `CtSceneUpdateFollowers`, `CtSceneSetButtons`, `CtSceneRegroupFollowers`).
* **Outward deps**: `w8` (un-`static` + declare, as in step 4) and `CtSceneSetActorPos` (already public
  in `ct_vm.h`).
* **CMake**: **BOTH** targets.
* **Result**: ~192 lines out. `ct_vm.c` after steps 3–5: 3051 → ~2450 lines.

### Step 6 — `src/field_coll.c` + `src/field_coll.h` — **RISK: LOW-MEDIUM**

* **Cut**: `src/field.c` lines **585–727** (the `Coll` typedef, `g_collRam`, `g_fc`,
  `fieldCollCopyTiles`, `fieldTileWalkable`, `fieldNpcTileSolid`, `fieldTileFetch`,
  `fieldPlayerCollide`) **and** `footPrio` (769–783, which also takes `const Coll*`).
* **Outward deps: ZERO.**
* **New header `field_coll.h`** exports: the `Coll` typedef (needed because `Field` embeds `Coll C;`
  at `field.c:1456`), the 6 functions, and `const Coll* FieldCollCur(void);` (remedy **S4**, for the
  debug trace at 4338–4346). `g_collRam` and `g_fc` stay `static` in the new TU.
* **Includes**: the new TU needs `"collision.h"`, `"player.h"`, `"ct_vm.h"`.
* **CMake**: `Belthasar` only.
* **Risk note**: medium only because `Coll` becomes a published type; there is no state export beyond
  the read accessor.

### Step 7 — `src/field_world.c` + `src/field_world.h` — **RISK: MEDIUM**

* **Cut**: `src/field.c` lines **117–333** and **349–583** (two ranges — the four session globals at
  334–348 stay behind).
* **Requires remedy S1 first**: relocate `g_owNoMarker` into the `World` struct as `w->noMarker`
  and rewrite its 8 use sites (533 + the seven in `main`). Justification for byte-identity is in §2.
* **New header `field_world.h`** exports: the `World` typedef, `worldInit`, `worldEnterAt`,
  `worldOverlayIntroSeed`, `worldPalRefresh`, `worldCompose`, `OW_FERRY_BOAT`, `NG_PAN_END`.
  `worldBg3Px` and `worldDrawAmbientSprites` stay `static` in the new TU.
* **Includes**: `"world.h"`, `"ct_owobj.h"`, `"assets.h"`, `"era_hud.h"`, `"ct_ng_pan.h"`,
  `"ow_idle.h"`, `"ct_portal_bg3.h"` move out of `field.c` with it.
* **CMake**: `Belthasar` only.
* **Result**: ~450 lines out; the overworld *presentation* layer sits next to `world.c` (the engine).

### Step 8 — `src/field_text.c` + `src/field_text.h` — **RISK: MEDIUM**

* **Cut**: `src/field.c` lines **728–734** (`plot`) and **784–1309** (the whole CT text/window/
  menu-font/naming/ATB-selector block).
* **Requires remedies S2 + S3.** New header exports:
  * primitives: `plot`, `cGlyph`, `cGlyphW`, `cDrawGlyph`, `cDrawGlyphP`, `drawCtWindow`,
    `drawCtWindowRect`, `drawMenuGlyph`, `drawMenuGlyphP`, `drawMenuGlyphDim`, `drawMenuGlyphDim2`,
    `drawMenuGlyphYellow`;
  * layout/text: `tbConvert`, `paginate`, `pageChars`, `layoutLines`, `drawLines`, `choiceOptions`,
    `drawChoiceBox`, `wordW`, `drawWorldLabel`, `drawOwLocName`;
  * screens: `drawNameScreen`, `drawAtbSelScreen`; charset: `ctToAscii`, `asciiToCt`, `NAME_GRID`;
  * setters/getter (S2) + `CtTbStyleGet/Set` (S3).
* **Includes** that move with it: `"atbsel.h"`, plus the CT window/menu-font asset plumbing.
* **CMake**: `Belthasar` only.
* **Note**: the new TU is **SDL-free** (`awk 'NR<4407 && /SDL_/' src/field.c` → only the
  `#define SDL_MAIN_HANDLED` at line 14). Same for steps 6, 7, 9, 10.

### Step 9 — `src/battle_render.c` + `src/battle_render.h` — **RISK: MEDIUM (biggest, but mechanical)**

* **Cut**: `src/field.c` lines **1607–3979** — one contiguous 2373-line range (contiguity is what
  remedy S3 buys; without it the range would have to be split around `battleResultsRender`).
* **Depends on**: `field_text.h` (step 8) only. Nothing else.
* **New header `battle_render.h`** exports the ~26 symbols `main()` and the bridge call:
  `CtBattleHudRender`, `battleStanceRender`, `battleGatoRender`, `battleHetakeRender`,
  `battleEnemyGenRender`, `battleDissolveRender`, `battleAttackRender`, `battleMarleAttackRender`,
  `battleEndPoseRender`, `battleFxPopupsRender`, `battleWhirlRender`, `battleIceRender`,
  `battleSongFxRender`, `battleSongNotesRender`, `battleCycloneDarkenRender`, `battleCritFlashRender`,
  `battleEntranceRender`, `battleEntranceMask`, `battleCronoIdleRender`, `battleBracketRender`,
  `battleTargetCursorRender`, `battleResultsRender`, `battleHudSeedName7E`, `pcStanceIdx`,
  `ctAnimengSelfcheck`, `ctEnemyStageSelfcheck`. Everything else (the whole anim engine,
  `g_gatoSheetRom`, `ctEnemyShCache`, `ctAnimengGatoTileOrder`) stays `static` inside the new TU.
* **27 of `field.c`'s 43 project `#include`s move with it** (`battle_hud.h`, `battle_hud_sub.h`,
  `battle_stance.h`, `battle_entrance.h`, `battle_anim.h`, `battle_anim_add.h`, `battle_gato_faces.h`,
  `animeng_e3/e4/sheet_gato.h`, `animeng_e3/e4/sheet_pc.h`, `battle_pc_pal.h`,
  `battle_gato_atkfaces.h`, `battle_gato_song.h`, `battle_songbeam.h`, `battle_bracket.h`,
  `battle_end.h`, `battle_settle.h`, `battle_fx.h`, `battle_fx_whirl.h`, `battle_fx_ice.h`,
  `battle_fx_song.h`, `battle_hetake.h`, `battle_dissolve.h`, `battle_marleatk.h`).
* **CMake**: `Belthasar` only.
* **Result**: `field.c` loses 2373 lines and 27 data-blob includes in one move. Verify carefully:
  this step touches the largest set of regression checks (the `field-battle*` family in
  `tools/regress_checks.json`, all driving `build/Belthasar.exe --battletest`).

### Step 10 — `src/field_battle_bridge.c` + header — **RISK: LOW (after 9)**

* **Cut**: `src/field.c` lines **3980–4406** (`fieldBattleFinish`, `battleShotAtCb`,
  `fieldBattleConsumer`, `MtSnap`/`mtSnapshot`).
* **Depends on**: `battle_render.h` (step 9) + `field_coll.h`'s `FieldCollCur()` (step 6).
* **New header** exports `fieldBattleFinish`, `fieldBattleConsumer`, `MtSnap`, `mtSnapshot`.
* **CMake**: `Belthasar` only.
* **Result**: `field.c` 7359 → **~3600 lines**, of which `main()` is 2953.

### Steps 11-17 — `src/main.c` → per-family harness TUs — **RISK: LOW each**

The mapper shows 7 of 13 groups with **zero** outward edges, so these can be done in any order after
step 11. **Every new file goes into the `ct` target only** (`CMakeLists.txt` lines 14–32).

| # | new file | cut from `src/main.c` | outward deps | risk |
|---|---|---|---|---|
| 11 | `src/harness_util.c` + `.h` | 379–444 (`fbdraw`), 2778–2826 (`ctLeadPass`/`ctFindLeadSlot`/`ctEventWait`), 2973–2991 (`ctLoadStateOracle`), 4303–4419 (`ctWramOff`/`ctParseAddr`/`ctParseWatch`/`ctHexRegion` — **not** `runRead`), 4420–4451 (`CtFrameCb`/`ctStream`) | none | LOW — do this first; it is the shared substrate for 12–17 |
| 12 | `src/harness_selfcheck.c` + `.h` | **986–2537** (30 field/collision/mover/player selfcheck modes) | none | LOW — 1552 lines, the single biggest win |
| 13 | `src/harness_ow.c` + `.h` | 3066–4302 (21 overworld modes) + 2827–2972 (`runNavWorld`) | harness_util | LOW — 1382 lines |
| 14 | `src/harness_dump.c` + `.h` | 738–985 (`dumpWram`, `--portcheck`, `--bootcheck`, `--cgram`, `--fb`, `--vram`, `--dumpflags`, `--playflags`) + 2538–2777 (`--verifyframe`, OAM, save/load state) + 4270–4302 (`--cgramseq`) | none | LOW |
| 15 | `src/harness_dat.c` + `.h` | 1–378 (`--assetinfo`/`--vm`/`--vmdemo`/`--vmdiff`/`--datfield`) + 445–601 (`--field`/`--scene`/`--datonly`) + 602–737 (`--datverify`/`--textdump`/`--lz`/`--sheetcheck`) | harness_util | LOW |
| 16 | `src/harness_probe.c` + `.h` | 2992–3065 (`--peek`), 4359–4419 (`runRead`), 4452–4752 (`--followscene`/`--monitor`/`--followvm`/`--followtest`/`--teleport`) | harness_util | LOW |
| 17 | `src/main.c` (what remains) | 4753–5154 only | all of the above headers | — |

**Result**: `main.c` 5154 → ~410 lines of pure dispatch, exactly matching what `harness_cli.c`
already promises structurally. While in step 15, delete `main.c:668`'s `DEFAULT_ROM` and use
`CT_DEFAULT_ROM` from `harness_cli.h` (byte-identical string; verified above) — or keep it and note
the duplication. Prefer the deletion, it is one line and provably inert.

### NOT in the plan (explicitly)

* **Splitting `field.c`'s `main()` (4407–7359).** The frame loop's ~2950 lines share a large
  function-local state set (the fade machine, `menuPhase`, `battlePhase`, `ngIntro*`, `wd`, `f`, `px/py/pz/pfac`).
  Any split needs a "field app state" struct, i.e. a *design* change with real regression surface.
  **Do it as its own front, after steps 1–10, with its own baseline.**
* **Splitting `battle.c` past `romdata`.** See §1.2 — the hub (`helpers`, 543–650) is called by 8 of
  13 groups; `animparse` (3022–3371) is technically dep-free but is 350 lines and only reachable via
  `sched`/`choreo`, so the cut buys little for the churn. **Optional bonus step, not recommended now.**
* **Merging the duplicated dialogue-font helpers** between `main.c` (399–443) and `field.c` (784–…).
  They live in different targets today (`ct` vs `Belthasar`), and unifying them means one shared TU
  linked into both, which changes nothing but widens the blast radius. **Do it only after step 8 and 15
  have both landed and are green.**

---

## 4. Where a NEW audio module goes — and what it costs

### 4.0 AUTHORITY — read `disasm/notes/audio_boot.md` first, not this section

A parallel front has already produced the byte-exact audio-boot derivation:
**`disasm/notes/audio_boot.md`** (536 lines) — it *supersedes and corrects*
`disasm/notes/apu_upload.md` (its §0 lists four load-bearing corrections: `$C7:001C` is a **6-entry
destination table**, not one address; there are **6 blocks / 13884 bytes**; the multi-block
continuation protocol; the upload costs ~23 emulated frames). It also already covers §6(e)
"What must change in CMakeLists.txt" with the same conclusion I reached independently.

**Everything below is the STRUCTURAL half only** — where the file lives, which targets link it, and
how it interleaves with steps 1–17. For the mechanism, the ARAM fingerprint pin
(`sha256 b56fa7e7…`, 13884 bytes, crc32 `021CEA18`), and the boot sequence, use `audio_boot.md`.
The two independent findings agree exactly, which is worth stating: `dsp.c:586`'s `Snes`
back-pointer dereference and the 4-file minimal link set were derived twice, separately.

### 4.1 The only mandate-legal shape

The canonical path is: the 65816 side uploads the **Akao driver** into SPC700 ARAM through the IPL
handshake (`JSL $C7:0000`, called exactly once, at `$C0:0054`), then pokes song/SFX ids into APU
I/O `$2140-$2143`. The zelda3 model this repo follows: **keep the SPC700/DSP emulated, perform the
upload once, then drive it with the real port writes.** No SPC700 or DSP code gets written here —
`snes/apu.c`, `snes/spc.c`, `snes/dsp.c` ARE the canonical hardware and are already in-tree.

### 4.2 Proposed placement

> **STATUS AT WRITE TIME:** `src/audio.h` (85 lines, opaque `CtAudio` handle: `CtAudioFree`,
> `CtAudioReady`, `CtAudioFrame`, `CtAudioSamples`, `CtAudioPost`, `CtAudioService`,
> `CtAudioSeedMailboxPage`, `CtAudioAram`/`CtAudioAramFnv`/`CtAudioPorts`/`CtAudioVoicesSounding`)
> **already exists**, untracked, produced by the parallel audio front alongside `audio_boot.md`.
> The placement below matches it. Do **not** design a second API — adopt that header.

* `src/audio.c` + `src/audio.h` — a thin, canonical wrapper over the **vendored** APU, exposing an
  **opaque `CtAudio*`** (no struct in the header, so no state is exported at all): create/free,
  the `$C7` IPL upload (`audio_boot.md` §7), a `$2140-$2143` post/service pair, a per-frame tick, a
  sample pull, and read-only ARAM/port introspection for regression pins.
* It owns the `Snes` shell required by `dsp.c:586` (see §4.4) — that shell must NOT be exported;
  it is an implementation detail of `audio.c`, exactly like `g_collRam` is of the collision TU.
* Top level `src/`, **not** `src/audio/` — a single `.c`/`.h` pair does not warrant a directory,
  and `src/data/` (§5) is the only subdirectory this plan introduces.

### 4.3 Exact `CMakeLists.txt` edits

Today (`CMakeLists.txt:12`) `file(GLOB SNES_SRC … snes/*.c)` is added **only** to `ct` (line 31), and
`target_include_directories(ct PRIVATE snes src)` (line 33) is the only target with `snes` on the
include path. `Belthasar` (lines 56–57) links **none** of `snes/` and does not have `snes` in its
include dirs. So:

```cmake
# line 56 — add the audio module + the minimal vendored APU set
  add_executable(Belthasar src/field.c src/scene.c src/lz.c src/assets.c src/ct_vm.c
    src/collision.c src/player.c src/world.c src/battle.c src/menu.c src/oam.c
    src/audio.c                                  # NEW
    snes/apu.c snes/spc.c snes/dsp.c snes/statehandler.c)   # NEW (minimal APU set)

# line 57 — 'snes' must join the include path (apu.h pulls snes.h/spc.h/dsp.h/statehandler.h)
  target_include_directories(Belthasar PRIVATE src snes ${SDL2_DIR}/include)
```

`ct` needs **only** `src/audio.c` added to its source list (line 14–32); `snes/*.c` and the `snes`
include dir are already there via the GLOB and line 33.

`ct_viewer` and `ct_world` need **no** change (they do not run the engine loop).

**Include-order hazard (structural, must not be got wrong):** `snes/input.h` and `src/input.h` are
distinct headers with the same basename. Putting `snes` on `Belthasar`'s include path creates the
same ambiguity `ct` already lives with (`target_include_directories(ct PRIVATE snes src)`, line 33 —
and `ct` compiles *both* `src/input.c` and `snes/input.c` today without incident, because a quoted
`#include "input.h"` resolves relative to the including file first on MSVC and GCC alike).
**Keep `src` BEFORE `snes` in `Belthasar`'s list** as written above, and do not convert any
`#include "input.h"` to an angle-bracket include.

**Sequencing against §3:** land `audio.c` **after step 10**. Steps 1–10 restructure `field.c` and
its CMake line; doing both at once means a red `--quick` run cannot be attributed. Steps 11–17 touch
only `ct`/`main.c` and are independent of the audio work, so they may interleave freely.
`SDL_INIT_AUDIO` must be added to `Belthasar`'s `SDL_Init` — that is a behavioral change and belongs
to the audio front, **not** to this plan.

### 4.4 The two hard constraints I verified (read these before writing a line of `audio.c`)

1. **`snes/dsp.c:586` dereferences the `Snes` back-pointer**:
   `float wantedSamples = (dsp->apu->snes->palTiming ? 641.0 : 534.0);`
   `apu_init(Snes* snes)` (`snes/apu.c:23-25`) stores it and **nothing else in `apu.c` uses it**
   (`grep -n 'snes' snes/apu.c` → lines 9, 23, 25 only; `snes/spc.c` → 0 hits). So a standalone
   `Apu` is viable **only** if `audio.c` supplies a `Snes` object whose `palTiming` is set — a
   zero-initialised `static Snes` shell is enough for `dsp_getSamples`; a NULL back-pointer crashes
   there. State this in `audio.h`; do not let it be discovered at runtime.
2. **Link closure is exactly four files.** Symbol scan
   (`grep -oE '\b(spc|dsp|sh|apu|snes|cpu|ppu|dma|cart|input)_[a-zA-Z]+\(' snes/{apu,dsp,spc}.c`)
   shows the external calls are only `spc_*`, `dsp_*`, `sh_*` (statehandler) — **no** `cpu_*`,
   `ppu_*`, `dma_*`, `cart_*`, `snes_*`. So `apu.c + spc.c + dsp.c + statehandler.c` links cleanly
   without dragging the whole emulator into `Belthasar.exe`.

### 4.5 What is NOT derived (structural view; see `audio_boot.md` §8 for the mechanism gaps)

`grep -rn -i '\bapu\b|spc700|dsp_getSamples|SDL_OpenAudio' src/*.c src/*.h` → **zero hits**: the C
engine today has *no* APU state, *no* sound-command call sites, and no audio-related global. That
is good news structurally — **audio is greenfield: it adds a module, it does not split one.** But it
means the *runtime* half (which song/SFX id gets written to `$2140-$2143` on which scene entry,
which battle event, which VM op) is **not derived**; `audio_boot.md` covers the BOOT half only and
its §8 gap 5 explicitly leaves the runtime mailbox path to "front 2".

The derivation tooling for the runtime half already exists and is exact:

* `CtOracleInstallWriteHook(s)` + `CtWriteLogStart/Stop` + `CtWriteLogData`
  (`src/ct_cpu_infra.h:50-56`), filtered to `adr & 0xFFFF` in `0x2140..0x2143`. This is the same
  mechanism `runPortCheck` (`src/main.c:758`) already uses, with the filter
  `(a >= 0x2100 && a <= 0x21FF)` — which **already covers the APU ports**, so the capture is a
  filter-narrowing away, not new infrastructure.
* `python tools/dis.py <hexaddr> <len>` on `$C7:0140` / the `$C7:0AD9` handler table
  (`audio_boot.md` §8 gaps 3 and 5) for the command wrapper the field/battle code calls.

Until that capture exists, a runtime `CtAudioPlaySong(id)` would be invention. **Ship the CMake
wiring + the boot module (which IS fully derived) and stop there.**

---

## 5. Data-blob clutter (§6 of the brief)

### 5.1 Census

**49 of the 67 `src/*.h` are generated data** (`code_fns == 0 && static_const_lines > 0`), 2 092 321
bytes total:

`animeng_e3_gato.h`, `animeng_e3_pc.h`, `animeng_e4_gato.h`, `animeng_e4_pc.h`,
`animeng_sheet_gato.h`, `animeng_sheet_pc.h`, `atbsel.h`, `battle_anim.h`, `battle_anim_add.h`,
`battle_bracket.h`, `battle_dissolve.h`, `battle_end.h`, `battle_entrance.h`, `battle_fx.h`,
`battle_fx_ice.h`, `battle_fx_song.h`, `battle_fx_whirl.h`, `battle_gato_atkfaces.h`,
`battle_gato_faces.h`, `battle_gato_song.h`, `battle_hetake.h`, `battle_hud.h`, `battle_hud_sub.h`,
`battle_marleatk.h`, `battle_pc_pal.h`, `battle_settle.h`, `battle_songbeam.h`, `battle_stance.h`,
`battle_tracks.h`, `battle_trig.h`, `ct_charbase.h`, `ct_ng_pan.h`, `ct_portal_bg3.h`,
`ct_regroup.h`, `era_hud.h`, `menu_arrow.h`, `menu_chrome.h`, `menu_configgrid.h`,
`menu_gradient.h`, `menu_icons.h`, `menu_itemdetail.h`, `menu_itemgrad.h`, `menu_itemuse.h`,
`menu_opt0.h`, `menu_partyport.h`, `menu_romdata.h`, `menu_saveform.h`, `menu_tech.h`, `ow_idle.h`.

**Generator provenance** (scan: for each data header, does any `tools/*.py` contain the literal
string `src/<header>`? `ls tools/_gen_*.py` → 28 scripts):

* **34 of the 49 are referenced by a generator**: 32 by a `tools/_gen_*.py`, plus
  `menu_icons.h` ← `tools/_menu_icons.py` and `ct_portal_bg3.h` ← `tools/extract_portal_bg3.py:33`.
* **15 have NO generator in `tools/`** and are baked-once artefacts:
  `battle_hud_sub.h`, `battle_pc_pal.h`, `ct_ng_pan.h`, `era_hud.h`, `menu_arrow.h`,
  `menu_configgrid.h`, `menu_gradient.h`, `menu_itemdetail.h`, `menu_itemgrad.h`, `menu_itemuse.h`,
  `menu_opt0.h`, `menu_partyport.h`, `menu_saveform.h`, `menu_tech.h`, `ow_idle.h`.
  (Note `tools/_isohud.py:13` only *reads* `era_hud.h`'s colours — it does not write it;
  `ow_idle.h` is regenerable via `ct.exe --owidle`, `docs/DEBUG_SURFACE.md:40`.)
  **Flag: these 15 are unreproducible without their capture recipe.** A follow-up front should
  either add the missing `_gen_` script or record the exact `ct.exe` command in the header banner.
  This is orthogonal to the directory move, but the move is the natural moment to do it.

`src/ct_owobj.h` is **not** a data blob — it is a pure `#define` ABI header shared by `world.c` and
`field.c`. It stays in `src/`.

### 5.2 Is `src/data/` safe? — YES, with two caveats

Every `#include` of every one of the 49 was enumerated (`for f in src/*.c; do grep -oE '#include "[a-z0-9_]+\.h"' $f; done`):

* `field.c` includes **33** of them, `menu.c` **13**, `battle.c` **4** (`battle_fx.h`, `battle_anim.h`,
  `battle_dissolve.h`, `battle_trig.h`), `main.c` **1** (`animeng_sheet_gato.h`), `ct_vm.c` **1**
  (`ct_regroup.h`). **Exactly 52 `#include` lines** to rewrite as `#include "data/xxx.h"` (counted
  programmatically over all `src/*.c`). No other `.c` includes a data blob.
* **No CMake change needed**: `target_include_directories(... PRIVATE src ...)` is already on every
  target, and quoted includes resolve relative to the including file (`src/`) first, so
  `"data/battle_hud.h"` resolves either way.
* **No machine-readable reference breaks**: `grep -c 'src/' tools/regress_inputs.json` → **0**;
  the only mentions of these paths in `tools/regress_checks.json` and `docs/TESTING.md` are inside
  **`notes` prose fields**, never in `argv`/`inputs`.
* **Caveat 1**: the ~30 generator scripts (28 `_gen_*.py` + `_menu_icons.py` + `extract_portal_bg3.py`)
  hard-code the output path `src/xxx.h` (`grep -rn 'src/' tools/*.py`). Each needs its one
  output-path constant updated, or the move silently un-generates itself on the next re-bake.
  This is the real cost of the move.
* **Caveat 2 (worth fixing while you are in there)**: because these are `static const` in headers,
  a blob included by two TUs is **compiled twice**. Today that is `battle_fx.h`, `battle_anim.h`
  (259 824 bytes) and `battle_dissolve.h` (`field.c` + `battle.c`), and `animeng_sheet_gato.h`
  (59 231 bytes; `field.c` + `main.c` — different targets, so harmless). After **step 9** the
  `field.c` side of the first three moves to `battle_render.c`, so the duplication persists
  (`battle.c` + `battle_render.c`, same target). Converting those three to `extern const` + a
  single definition TU is a *separate*, later, behavior-preserving step — **not** part of this plan.

**Recommended sequencing**: do the `src/data/` move **AFTER step 9**, so the include lines are
rewritten once, in their final homes, instead of twice.

---

## 6. Summary scoreboard (if all 17 steps land)

| file | before | after |
|---|---:|---:|
| `src/field.c` | 7359 | ~3600 (of which `main()` = 2953) |
| `src/main.c` | 5154 | ~410 |
| `src/battle.c` | 5075 | ~4715 |
| `src/ct_vm.c` | 3051 | ~2450 |
| new engine TUs | — | `field_coll.c`, `field_world.c`, `field_text.c`, `battle_render.c`, `field_battle_bridge.c`, `battle_romdata.c`, `ct_vm_camera.c`, `ct_vm_wram.c`, `ct_vm_followers.c` |
| new harness TUs | — | `harness_util.c`, `harness_selfcheck.c`, `harness_ow.c`, `harness_dump.c`, `harness_dat.c`, `harness_probe.c` |
| `src/*.h` at top level | 67 | 18 (49 → `src/data/`) |

No behavior changes. No re-pins. Seventeen green `regress.py --quick` runs.
