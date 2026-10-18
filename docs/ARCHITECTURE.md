# Architecture

This page describes how BelthasarProject is put together: the programs, the source files, how data flows from your ROM
into the engine, and how each subsystem is organised. For the build itself see [INFRA.md](INFRA.md). For how
behaviour is derived and checked see [METHODOLOGY.md](METHODOLOGY.md).

## The programs

`CMakeLists.txt` builds four executables into `build/`.

| Program | Sources | What it is |
|---|---|---|
| `ct.exe` | `src/main.c` + the engine + `snes/*.c` | **The oracle harness.** Headless. It loads your ROM into the vendored LakeSnes core (the *oracle*) and runs the original game next to the C engine: lockstep differentials (`--vmdiff`), the 512-scene check (`--vm`), micro-execution sweeps of single ROM routines (`--sweep*`), RAM probes (`--peek`, `--monitor`, `--read`, `--followvm`), savestates. Always built; needs no SDL. |
| `Belthasar.exe` | `src/field.c` + the engine + the frontend + `snes/*.c` | **The game.** SDL2. Boots like the cartridge (`--boot`), runs the field, battles, menus, overworld, title and attract, plays the sound, and carries the F1 overlay and the mods. Its many headless flags (`--selftest`, `--battletest`, `--menutest`, `--walkscript`, ...) are what most regression checks drive. It links the LakeSnes core for three things only: the SPC700/DSP that plays the sound driver, the translated stages, and `CT_FIELDLOCK` (the oracle running inside the game for live comparison). |
| `ct_world.exe` | `src/ct_world.c`, `src/world.c` | An overworld viewer: walks the lead over the world map with the C mover and camera. A development tool. |
| `ct_viewer.exe` | `src/viewer.c`, `src/scene.c` | A field-scene browser over `ct_assets.dat` (`--datonly`) or the ROM. A development tool. |

The three SDL programs are built only when SDL2 is present (`third_party/SDL2` on Windows, the system package on
Linux).

## Data flow

```
your ROM (verified: src/rom_verify.c, tools/ctlib/rom.py)
   |
   +--> assets/extract_assets.py --------------------> ct_assets.dat ----------+
   |       (lz.py, gfx.py, maps.py, sprites.py, text.py, script.py)            |
   |                                                                           v
   +--> oracle (ct.exe) + recipes --> oracle states --> tools/gen_*.py --> src/data/*.h --> compiled
   |                                                  \-> tools/gen/xlat.py --> src/xlat/*.c   into the
   |                                                                                          engine
   +--> read at run time by Belthasar: battle enemy data, the sound driver (bank $C7) and song data
```

- **`ct_assets.dat`** is one container (`CTAS` magic, a directory of typed entries) that holds what the field
  needs to run without the ROM: scene palettes and headers, tilesets, maps, palette animations, sprites (graphics,
  assembly, palettes), strings and dialogue, the font, decompressed scene scripts, actor tables, collision and
  exits. `ct.exe --datverify` checks that a scene rendered from the container matches one rendered from the ROM.
- **`src/data/*.h`** (51 headers) are tables compiled into the engine: battle animation records and sheets, menu
  chrome, the title sequence's data, the overworld era placard, and so on. Each comes from a generator in
  `tools/`. They are ROM-derived and are generated locally (see [ROM.md](ROM.md)).
- **`src/xlat/*.c`** are ROM programs translated into C (see [Translated stages](#translated-stages)).
- **Battery saves** go to `ct_sram.srm`, in the cartridge's own 8 KB layout and checksum, so a save made by the
  engine and one made by the cartridge are interchangeable.

## Source layout

Every C source in `src/`, one line each. The headers mostly match the `.c` names; two stand alone:
`ct_vbeam.h` (the virtual beam's constants, shared by the oracle and the engine) and `ct_owobj.h` (the overworld
ambient-object block layout).

| File | Role |
|---|---|
| `main.c` | `ct.exe`: the oracle harness. Every probe, lockstep, sweep and self-check mode. |
| `harness_cli.c` | `ct.exe`'s flag table and argument parser; unknown flags fail hard. |
| `harness_drive.c` | Timed pad scripts that drive the oracle from power-on (for example, to Crono's bedroom). |
| `ct_cpu_infra.c` | The comparison core: loads the ROM into the oracle (`CtOracleCreate`), snapshots, verification, micro-execution of single routines. |
| `ct_lake_state.c` | The `CTLS` savestate container (magic, length, CRC-32) around LakeSnes's state stream. |
| `rom_verify.c` | SHA-1 check of the supported dump; strips a copier header. |
| `boot.c` | The reset path: native-mode entry and the boot register init. |
| `input.c` | The per-frame joypad read (`C2/8500`), ported and checked against the oracle. |
| `assets.c` | Loader for `ct_assets.dat`. |
| `lz.c` | The game's LZ decompressor (the format reverse-engineered by Michael Springer, via CTViewer), graded on its output. |
| `scene.c` | Field-scene rendering: one load path (ROM or `ct_assets.dat`) feeding one compose path (layers, priorities, colour math). |
| `tileanim.c` | The field's animated background tiles (`FD/DE98`, `FD/E022`). |
| `ct_vm.c` | The live field event-script VM and its scheduler, the textbox and naming machines, and `CtWram` (the engine's `$7E`/`$7F` work RAM). |
| `move.c` | The mover chain, the object loader, the animation drain (`C0/CA76`), and the graded field-op runner `CtFieldRunnerOne` that `--sweepops` checks. Shared op families are called from both runners. |
| `collision.c` | Field tile collision and z-planes. |
| `player.c` | The lead's input-to-velocity controller (`C0/881E` and its helpers). |
| `oam.c` | Field sprite assembly into OAM (`C0/B276` onward), checked byte for byte against the oracle. |
| `objslot.c` | The palette-effect record service (`FD/E39C`), one body shared by the field and the battle. |
| `field.c` | `Belthasar.exe`: the host. The frame loop, the field compositor, location loads, the battle and menu hand-offs, the overworld bridge, the sound host, the widescreen and frames-between composition, and the headless test modes. |
| `field_cost.c` | What the field's reload after a menu costs, priced from the ROM's control flow (`C0/01A5`, `C0/286C`). |
| `world.c` | The overworld: camera and scroll engine (bank `$C2`), mover, tilemap streaming, the ambient-object VM, party followers. |
| `battle.c` | The battle module: boot and staging, the ATB and menu pass, the action pipeline, techs and items, the enemy AI, status, effects, rendering, rewards, the exit back to the field. |
| `menu.c` | The X-menu screens, shops and the load screen; the save write and load (`FF/FA..`). |
| `menu_cost.c` | What the menu's open costs, priced instruction by instruction. |
| `title_intro.c` | The title sequence as the overlay's own machine (the `$C3` script interpreter and its NMI hooks), not a recording. |
| `audio.c` | The sound system: the bank `$C7` driver upload, the song loader, the command channel, and pricing of each transfer on SPC time. It drives the vendored SPC700/DSP. |
| `seq_xlat.c` | The runtime for translated stages: runs a `src/xlat/*.c` program over the vendored 65816, DMA and PPU, with the timing rule applied. |
| `seq_ppu.c` | The picture of a `$C3` sequence stage, drawn by the vendored LakeSnes PPU. |
| `settings.c` | `ct_settings.ini`: the player's options. Headless runs never read it. |
| `overlay.cpp` | The F1 options overlay (Dear ImGui) over the paused game. |
| `overlay_stub.c` | The no-overlay build, used when `third_party/imgui` is absent. |
| `present.c` | The presenter: the window and its backend behind one interface; the SDL_Renderer and OpenGL 3.3 backends. |
| `present_vk.c` | The Vulkan backend (headers only; the driver's loader is reached through SDL at run time). |
| `motion.c` | Mods: what each pixel of the picture moves with (its owner), and the redraw at a higher internal scale or between two frames. |
| `fieldnative.c` | Mods: the field's frames between drawn natively from the map's layers and the sprites' own pixels. |
| `ct_world.c` | `ct_world.exe`. |
| `viewer.c` | `ct_viewer.exe`. |

Other top-level directories:

| Path | Contents |
|---|---|
| `snes/` | Vendored LakeSnes (MIT): the oracle's 65816, PPU, DMA, SPC700 and DSP, with the timing rule's hooks added. Every check's ground truth runs on it, so change it with care. A change to its state layout invalidates every `.state` file. |
| `assets/` | The Python asset pipeline (no dependencies outside the standard library). |
| `tools/` | Build, the regression suite, the disassembler, the oracle wrapper `ct.py` + `ctlib`, generators, and about a hundred `*_cmp.py` comparison tools that grade one behaviour against the oracle. Index: [tools/README.md](../tools/README.md). |
| `disasm/notes/` | The reverse-engineering dossiers. |
| `docs/` | This documentation. |

## The field

The field is the bulk of the game and of the engine.

- **Event-script VM.** Every object in a location has a script. The ROM's scheduler builds a queue each frame
  (`C0/59D9`) and its runner (`C0/5A46`) gives each object a *visit* that runs ops until one yields. Ops dispatch
  through the jump table at `C0/5D6E`, with the direct page at `$0100`. The engine reproduces the queue, the
  visits and the yields, and keeps the scripts' state in the same WRAM addresses the ROM uses, so the oracle's RAM
  and the engine's can be compared byte for byte.
- **Two runners.** `ct_vm.c`'s `step()` is the VM the game runs. `move.c`'s `CtFieldRunnerOne` is a second
  implementation that `--sweepops` grades against micro-execution of each ROM handler (211 opcodes, 12,660 of
  12,660 cases byte-exact over the whole 128 KB of work RAM). Op bodies are being moved into shared families
  called by both (`vmSharedOp`), so that what the sweep grades is what the game runs
  ([disasm/notes/vm_two_implementations.md](../disasm/notes/vm_two_implementations.md),
  [disasm/notes/live_vm_op_gaps.md](../disasm/notes/live_vm_op_gaps.md)).
- **Location loads** follow the ROM's own sequence: the default fill of every object (`C0/571A`), the init pass,
  the four WRAM clears (all DMA), the map and collision planes. Work RAM is process-lifetime, as on the console
  ([disasm/notes/wram_load_census.md](../disasm/notes/wram_load_census.md)).
- **Movement, collision, drawing.** The mover chain, the z-planes and slopes, the solid scan, the draw list (the
  ROM's linked list in 4-line bands, not a sort by Y), per-unit sprite priority, and OAM assembly are each ported
  and checked by their own sweep.
- **Windows and effects.** The textbox machine (`C0/1F87`), the iris (`C0/21E1` + `C3/0077`), the window wipe, the
  caption plane, colour math, animated tiles and palettes.

## Battle

`battle.c` runs the battle module on the same work RAM image the ROM uses, so staged blocks and per-frame state can
be compared with the oracle. It covers the boot and staging from the field (`CF/FB65`, `C1/8000`), the main loop
with its two waits per iteration, the ATB, the menu pass (one key every four frames), the action pipeline (one
effect tail `C1/DCFD` for every attack), techs including dual and triple techs, items, the status machine, the enemy
AI (the ROM's condition walk and resolvers), counters, death dissolves, rewards and level-ups, and the exit back to
the field. The census tool `tools/battle_ab.py` fights every enemy on both sides and compares the result.

## Menus

`menu.c` holds the X-menu (Item, Equip, Tech, Config, Save, Form), shops, the naming screen's data and the load
screen. Each screen is a port of its ROM handler in bank `$C2`, including the slides, scrollbars, cursors and the
frames each transition takes.

## Overworld

`world.c` ports the bank `$C2` world engine: the camera and its streaming of the tilemap ring, the lead's mover and
collision on a torus, the ambient-object VM (birds, boats, the fair), the ferry, the party followers, the zoned music
grid, the colour math per world, and the window effects.

## Audio

The game's sound program runs on the SNES's sound processor (the SPC700). `audio.c` does the CPU's side natively:
it uploads the bank `$C7` driver, loads songs and samples into sound RAM the way the ROM's loader does, sends
commands through the four I/O ports, and prices each transfer on SPC time under the timing rule. The SPC700 and its
DSP themselves are LakeSnes's core, run one frame per displayed frame.

That is emulated hardware inside the port. The project has decided that nothing in the port should be emulation
([DECISIONS.md](DECISIONS.md), 2026-10-09), so replacing it with a native implementation of the driver's behaviour
is on the [roadmap](ROADMAP.md#3-debt-emulation-and-bakes-to-retire).

## Translated stages

A few ROM programs depend on the CPU's clock in a way a clockless port cannot reproduce. Examples are the
motorcycle race, whose bike frames land in VRAM 5 to 15 frames after their decompression by cycle count, and
transitions whose scanline edges come from a generator that reads the PPU's beam counters. For these the project
chose (2026-09-14) to translate the program: `tools/gen/xlat.py` turns each instruction the stage executes on the
oracle into one labelled line of C, and `seq_xlat.c` runs it over the vendored 65816, DMA and PPU.

| File | Program |
|---|---|
| `src/xlat/seq_c3.c` | The `$C3` sequence stages (the race and others), with their overlay in `$7E`. |
| `src/xlat/wipe_c3.c` | The field's window wipe (`C3/01E4` and its seed routine `C3/02DD`). |
| `src/xlat/owwin_c6.c` | The same window routine called from the overworld (`$C6:E241`). |
| `src/xlat/gwarp_fd.c` | The gate warp effect (bank `$FD`). |
| `src/xlat/hdma_fd.c` | The HDMA table builder `FD/C2C1`. |

`tools/gen/xlat_all.py` makes all five from the ROM: it runs the oracle's unique traces from the recipe states
(`tools/recipes/attract_*`) and then `tools/gen/xlat.py` with the options each file was made with; `gwarp_fd.c` and
`hdma_fd.c` still need two captures of the Millennial Fair (`tools/gen/manifest.json`, "needs").

These are exact by construction and graded per instruction against the oracle (`ct.exe --xlatcmp`). They are also
emulation of the CPU and PPU, so they are debt: the decision of 2026-10-09 asks for native C that keeps the
behaviour, replaced as each one is touched.

## The frontend and the mods

- **Presenter.** `field.c` composes a 256x224 picture (wider in widescreen) and hands it to `present.c`, which
  draws it through SDL_Renderer, OpenGL 3.3 or Vulkan, placed by the scaling and aspect options. A backend that
  fails to start falls back to SDL_Renderer. With nearest filtering the three backends give identical pixels
  (`tools/backends_compare.py`, which needs a GPU and is not in the suite).
- **Overlay.** `overlay.cpp` draws the options with Dear ImGui over the paused game; `settings.c` stores them.
- **Mods.** These are the options that go beyond the cartridge. They are opt-in, and the ROM-accuracy rule does not
  bind them ([DECISIONS.md](DECISIONS.md), 2026-10-10):
  - *Internal resolution* (1x to 4x) and *frames between*. `motion.c` records which owner each pixel moves with:
    a map layer at its own scroll, the caption plane, an object, or a screen-fixed element. It then redraws the
    frame at k times the resolution, or at a fraction t between two logic frames. `fieldnative.c` and the
    overworld's native drawer go further and redraw from the layers and sprite pixels directly. At t = 256 and 1x
    each must equal the composed frame (`CT_NATIVECHECK`).
  - *Widescreen*. The field, overworld, title and battle arena are composed wider than 256 columns, and the middle
    256 columns must equal the vanilla picture (`tools/widescreen_check.py`). Menus are pillarboxed.
  - *Free sync*. Waits that exist only because the hardware was busy (song uploads, load stalls, the menu reload's
    black frames) cost no frames.

## Where the reverse-engineering notes live

- [`disasm/notes/`](../disasm/notes/): about 260 dossiers, one per mechanism, named by subsystem (`battle_*`,
  `field_*`, `menu*`, `ow_*`, `audio_*`, ...). Each cites ROM addresses and oracle measurements and lists its open
  questions. Start with [events.md](../disasm/notes/events.md), [scenes.md](../disasm/notes/scenes.md),
  [field_tick.md](../disasm/notes/field_tick.md), [battle_loop.md](../disasm/notes/battle_loop.md) and
  [oracle_logical_frames.md](../disasm/notes/oracle_logical_frames.md).
- [`disasm/notes/patches/`](../disasm/notes/patches/): measured changes that are saved but not applied, with the
  reason.
- Comments in the C cite the ROM routine each block ports (`C0/5A46`, `CD/0340`), and usually the dossier.
- [`MEMORY.md`](../MEMORY.md) at the repository root is a navigation index for agents and humans.
