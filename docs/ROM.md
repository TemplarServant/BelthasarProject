# The ROM

BelthasarProject ships no part of Chrono Trigger. It needs a copy of the game that you own, and it runs only on one
specific dump of it.

## The supported ROM

| | |
|---|---|
| Game | Chrono Trigger, SNES, USA release (1995) |
| Size | 4,194,304 bytes (32 Mbit, 4 MiB), no copier header |
| SHA-1 | `de5822f4f2f7a55acb8926d4c0eaa63d5d989312` |
| CRC32 | `2D206BF7` |
| MD5 | `a2bc447961e52fd2227baed164f729dc` |
| Mapping | HiROM + FastROM, 8 KB battery SRAM, NTSC |

These are the hashes of the No-Intro database entry for the USA cartridge. Other revisions and regions (the
Japanese release, the DS and PC versions, translation or randomizer patches) are **not supported**: the code
addresses routines and tables by their location in this exact image.

A 512-byte copier header (a file of 4,194,816 bytes) is accepted. It is stripped before the hash is computed.

## Dumping your cartridge

You make the ROM yourself, from your own Chrono Trigger (USA) cartridge, with a cartridge reader that copies the ROM
chip to a file on your computer.

**The guide to follow:** <https://dumping.guide/carts/nintendo/snes> -- the preservation community's step-by-step
guide for SNES cartridges (it is based on No-Intro's own instructions). It covers which readers work, how to clean the
contacts, how to dump, and how to check that two dumps agree.

Readers it covers, any of which reads Chrono Trigger (a plain HiROM cartridge with battery SRAM):

| Reader | Where |
|---|---|
| Open Source Cartridge Reader (OSCR) -- an Arduino-based reader that writes the dump to an SD card, no PC needed | <https://github.com/oscartreader/oscr-firmware> (hardware: <https://github.com/oscartreader/oscr-hardware>) |
| RetroBlaster 2.0 | <https://github.com/retrostage/RetroBlaster> |
| Retrode 2 -- the cartridge appears as a USB drive with the ROM as a file on it | see the guide above |
| Analogue Super Nt with the CopySNES jailbreak | <https://github.com/SmokeMonsterPacks/Super-NT-Jailbreak> |

In short:

1. Clean the cartridge's contacts (isopropyl alcohol on a cotton swab, let it dry).
2. Dump the ROM with your reader, following its own instructions. Save it as `Chrono_Trigger_USA_SNES.sfc`.
3. Dump it a second time and compare the two files; if they differ, clean the contacts and dump again.
4. Check the hash (next section). A good dump of the USA cartridge has SHA-1
   `de5822f4f2f7a55acb8926d4c0eaa63d5d989312`; if yours does not, reseat and clean the cartridge and dump again.
5. Run `python tools/setup.py --rom Chrono_Trigger_USA_SNES.sfc` ([README.md](../README.md#2-set-up-from-your-rom)).

The battery save can be dumped the same way, but BelthasarProject does not need it.

Downloading a ROM you do not own is not a substitute, and the project will not help you find one.

## Verifying the dump

BelthasarProject verifies the dump itself, but you can check it before you start:

```sh
# Windows (PowerShell)
Get-FileHash -Algorithm SHA1 .\Chrono_Trigger_USA_SNES.sfc

# Linux / macOS
sha1sum Chrono_Trigger_USA_SNES.sfc
```

If the file is 4,194,816 bytes, it has a copier header and the plain file hash will not match. BelthasarProject strips the
header for you. To check by hand, hash the file without its first 512 bytes
(`tail -c +513 file.sfc | sha1sum`).

Every program that reads the ROM checks it first and refuses anything else, with a message that names the expected
hash:

| Where | What checks |
|---|---|
| [`src/rom_verify.c`](../src/rom_verify.c) | `CtRomVerify`: size, optional header strip, SHA-1. Called by the oracle (`CtOracleCreate` in `src/ct_cpu_infra.c`) and by the battle and sound ROM loader (`CtBattleLoadRom` in `src/battle.c`). |
| [`tools/ctlib/rom.py`](../tools/ctlib/rom.py) | `verify()` / `load()`: the same check for the Python tools that load the ROM through `ctlib`. |

One gap: some Python scripts still open the ROM file themselves instead of going through `ctlib`, so they do not
check it yet. These include [`assets/extract_assets.py`](../assets/extract_assets.py), `tools/dis.py`, and
several comparison scripts. The setup flow verifies the ROM before it runs any of
them. Routing every ROM read through `ctlib.rom.load()` is on the [roadmap](ROADMAP.md#12-smaller-release-leftovers-s-each).

## Where the ROM goes

By default the programs look for `Chrono_Trigger_USA_SNES.sfc` in the current directory. `Belthasar` also looks next
to the `ct_assets.dat` it was given. `CT_ROM=<path>` overrides the location for `Belthasar`, and `ct.exe` takes the
ROM path as a positional argument. The `.gitignore` excludes `*.sfc`, `*.smc`, `*.srm` and `*.dat`, so a ROM,
save or asset container in the working tree is never committed by accident.

### The player's start: the ROM screen

A player opens `Belthasar` with no arguments, or drops their ROM on it (src/launcher.h; Windows and Linux -- macOS has
its folder chooser, Android its setup activity). The game then runs from its data folder: `CT_DATA_DIR` when set, else
**the executable's own folder** -- the game, its data, the save and the options travel together (the owner's decision,
2026-10-25). A folder that cannot be written (under Program Files, say) is refused with a message asking the player to
move Belthasar; nothing is written anywhere else. Versions up to 0.5.0 used a per-user folder
(`%APPDATA%\BelthasarProject\Belthasar\`, `~/.local/share/BelthasarProject/Belthasar/`): on its first start, a folder
that holds none of the player's files copies that one's save, options and dump in, and its `ct_assets.dat` and
`ct_wide.dat` when they are this version's (src/launcher.c `takeOldData`). The old folder is left as it was.

It holds `ct_assets.dat` (with the tables a data-free build loads inside it: one file, which the Android app takes
as it is, [ANDROID.md](ANDROID.md)), the battery save `ct_sram.srm`, `ct_settings.ini`,
`setup.log` and the copy of the dump the setup read (`Chrono_Trigger_USA_SNES.sfc`; the game itself does not need it
once a ROM-free `ct_assets.dat` is there). While any of the game's files is missing, the setup screen
(src/launcher_ui.cpp: a desktop form drawn with Dear ImGui -- the ROM file with Browse... and drag-and-drop, the data
folder, the progress, Make the game data / Cancel / Try again / Play; without ImGui the file dropped on the executable
and the console) asks for the dump, verifies it (`CtRomVerify`), writes it to the folder and runs the setup with no
compiler ([RELEASE.md](RELEASE.md) builds the folder that holds the pieces below):

    <python> <kit>/tools/setup.py --rom <folder>/Chrono_Trigger_USA_SNES.sfc --no-build --oracle <ct.exe>
        --workdir <folder>/work --out <folder>

| Piece | A release | A developer's `build/` | Override |
|---|---|---|---|
| Python | `setup/python/python.exe` (`setup/python/bin/python3`) beside the executable | `python` / `python3` from the PATH | `CT_PYTHON` |
| the kit (the repository's tools) | `setup/kit/` | the repository (`build/..`) | `CT_SETUP_KIT` |
| the oracle (compiled from the shapes) | `setup/ct.exe` | `build/ct.exe` | `CT_SETUP_ORACLE` |

The setup also looks for an installed Steam copy of Chrono Trigger and, when it finds one, writes `ct_wide.dat` (the
widescreen's optional Steam margins, [WIDESCREEN.md](WIDESCREEN.md#getting-the-steam-margins)) beside the game's data;
without one it says so and goes on (`--steam <folder>` names one, `--no-steam` skips the step).

The screen's bar follows the setup's `setup: progress K/N` lines; the whole output goes to `setup.log`. A run with any
argument starting with `-`, or with `CT_NOPACE` set (the suite's), never reaches the launcher.

## What the build does with it

Everything derived from the ROM is generated on your machine:

| Output | Made by | Used for |
|---|---|---|
| `ct_assets.dat` | [`assets/extract_assets.py`](../assets/extract_assets.py) | The asset container: scene palettes, tilesets, maps, sprites, the font, strings and dialogue, scene scripts, collision, exits. The field and the menus draw from it without the ROM. |
| `src/data/*.h` | the generators in `tools/` | 48 tables the engine compiles in: battle animation records, menu chrome, the title sequence, the overworld placard, the field collision's slope tables, and so on. Of these, 13 are generated from the ROM alone and 35 by oracle runs from power-on (the recipes in `tools/recipes/`). |
| `ct_tables.dat` | [`tools/gen/pack_tables.py`](../tools/gen/pack_tables.py) | The same 48 headers' tables (226 arrays) as a data file, which a data-free `Belthasar` loads at startup instead of compiling them in (see below). The player's setup (`--out`) writes them inside the `ct_assets.dat` it leaves in the data folder (`pack_tables.py into`). |
| `src/xlat/*.c` | [`tools/gen/xlat_all.py`](../tools/gen/xlat_all.py) (traces) + [`tools/gen/xlat.py`](../tools/gen/xlat.py) | Five timing-sensitive ROM programs translated instruction by instruction into C, compiled into `ct.exe` only, as references for their native replacements (see [ARCHITECTURE.md](ARCHITECTURE.md#translated-stages)). |
| Oracle states | recipes replayed by the oracle | The savestates the generators and the regression suite start from. Each recipe is a pad script played from power-on, optionally on top of a battery save that another recipe produced. |

At run time `Belthasar` reads only `ct_assets.dat`: the container the setup makes is a ROM-free one, and the game
needs no dump beside it ([The ROM-free runtime](#the-rom-free-runtime)). An older container, made before the ROM-free
runtime, still needs the ROM beside it.

The single entry point is `python tools/setup.py --rom <path>`: verify the ROM, build a bootstrap oracle, replay
the recipes, generate, pack the tables, build ([README.md](../README.md#2-set-up-from-your-rom)).

## The data-free Belthasar

A prebuilt `Belthasar` has to work for everyone without carrying anything of the game, so it can be built with none of
the generated tables compiled in: `CT_DATAFREE=1 python tools/build.py Belthasar` (CMake `-DCT_DATAFREE=ON`). It then
loads them at startup from the table pack the setup makes from your dump: inside the player's `ct_assets.dat`
(`tools/gen/pack_tables.py into`, what the setup's `--out` writes), or `ct_tables.dat` beside it in a developer's tree.

- **How the engine includes a table.** Always through `src/tables/<name>.h`. In a normal build (`ct.exe`, a developer's
  `Belthasar`) that file only includes `src/data/<name>.h`, so nothing changes. In a data-free build it declares the
  tables instead, `extern const T name[dims];`, with the header's own types and sizes, and
  `src/tables/tables_storage.c` defines each one once, zeroed. [`src/datapack.c`](../src/datapack.c) fills them from the
  pack before `main` reads any. `src/tables/` is generated from the shapes
  (`python tools/gen/pack_tables.py headers`; `check` says whether it is current) and holds no data.
- **Where the pack is found.** In order: `--tables <path>`, `$CT_TABLES`, the `--assets` file itself if it carries the
  table entries, `ct_tables.dat` beside the `--assets` file, `ct_tables.dat` in the working directory.
- **The pack.** A CTAS container (the `ct_assets.dat` format) holding one entry per table with its bytes exactly as the
  compiler lays them out for the release's targets (x86-64 and arm64: little-endian, natural alignment, 8-byte
  pointers), a string pool, and relocations for the pointer members (the strings in `battle_anim.h`, `battle_stance.h`,
  `battle_settle.h` and five other headers, and the loader's chapter and era names). The full format is in
  `pack_tables.py`'s docstring.
- **What the loader refuses.** Any mismatch with the build: another pointer size or byte order, another set of
  tables, a table of another size, another layout (each table's layout string, every member's name, type and offset,
  is hashed on both sides), relocations that are not exactly the build's pointer members, another set of `#define`s, a
  compiled-in `#define` whose value differs from the one in your headers, and a run-time one whose value does not fit
  its type. It says why and exits; it never runs on a partial or shifted table.
- **The proof.** `python tools/gen/tables_proof.py` (a tree that holds the generated headers) packs `src/data/*.h`,
  loads the pack into the data-free storage with the loader the data-free `Belthasar` links, and compares every table
  with the same table compiled from the real header, byte for byte and padding included (a pointer by its string), and
  every `#define` with the real header's value (a run-time one as the loader filled its variable). Its controls: packs
  with one table byte flipped or a run-time `#define` changed must differ, and packs with a wrong pointer size, a wrong
  layout hash or a compiled-in `#define` changed must be refused.
- **The `#define`s.** The shapes' numeric `#define`s (163: tile bases, foot positions, stance ids, the new-game pan's
  start and length, ROM base addresses, the title's seed registers, the tables' dimensions) are values of your ROM too.
  In a data-free build each one the compiler does not need as a constant is a variable: `src/tables/<name>.h` says
  `#define NG_PAN_START_X ctDef_NG_PAN_START_X` and declares `ctDef_NG_PAN_START_X` with the literal's own C type (`int`,
  or `unsigned` for a `u`-suffixed or past-`INT_MAX` hex literal), and the loader fills it from the pack's `#define`
  entry. 99 are run-time this way, 73 of them used by the engine's code. The MSVC build refuses any constant-expression
  use of a run-time one (an array bound, a case label, a static initializer), so a new such use is a compile error that
  names the `#define`, to be listed in `pack_tables.py`'s `COMPILED_IN` with its reason -- never a silent change.
- **What stays compiled in.** 64 `#define`s, with the shapes' values (the loader checks each against the pack):
  - the 62 that size a table (a dimension, directly or through another `#define`): the storage a data-free build
    compiles has those sizes, so the executable states them whatever the headers say. `animeng_e3_gato.h`
    `CT_ANIMENG_E3_GATO_N`; `animeng_e3_pc.h` `CT_ANIMENG_E3_{CRONO,MARLE,LUCCA,FROG}_N`; `animeng_e4_pc.h`
    `CT_ANIMENG_CRONO_CRIT_N CT_ANIMENG_LUCCA_ATK_N`; `animeng_sheet_gato.h` `CT_ANIMENG_SHEET_GATO_N`;
    `animeng_sheet_pc.h` `CT_ANIMENG_SHEET_{CRONO,MARLE,LUCCA,FROG}_N`; `atbsel.h` `CT_ATBSEL_W CT_ATBSEL_HGT
    CT_ATBSEL_NPAL CT_ATBSEL_PATCH_W CT_ATBSEL_PATCH_H CT_ATBSEL_NLINES CT_ATBSEL_MAXSTEP CT_ATBSEL_EMPTY_W
    CT_ATBSEL_EMPTY_H`; `battle_anim.h` `CT_ANIM_NOBJ CT_ANIM_NKF CT_ANIM_NTILE CT_CRONO_ATK_N CT_GATO_IDLE_N
    CT_CRONO_BOB_N`; `battle_anim_add.h` `CTCRONOIDLEADD_NOBJ CTCRONOIDLEADD_NKF CTGATOSWIPE_NOBJ CTGATOSWIPE_NKF
    CT_GATO_IDLE2_N`; `battle_fx.h` `CT_FX_RISE_N`; `battle_gato_atkfaces.h` `CT_GATO_ATK_NFACE CT_GATO_ATK_NTILE`;
    `battle_gato_faces.h` `CT_GATO_FACE_NTILE`; `battle_hetake.h` `CT_HETAKE_NSLOT CT_HETAKE_NTILE CT_HETAKE_ATK_N`;
    `battle_hud_chrome.h` `CT_BHUD_CHROME_NT`; `battle_marleatk.h` `CT_MARLEATK_NOBJ CT_MARLEATK_NKF CT_MARLESPARK_NKF
    CT_MARLEATK_TRAJ_N`; `battle_settle.h` `CT_SETTLE_NOBJ CT_SETTLE_NKF CT_SETTLE_NTILE CT_SETTLE_CRONO_NSTEP
    CT_SETTLE_MARLE_NSTEP`; `battle_stance.h` `CT_STANCE_NOBJ CT_STANCE_NACTOR CT_STANCE_NTILE`; `menu_chrome.h`
    `CT_MENU_CHROME_NTILE CT_MENU_CHROME_TMW CT_MENU_CHROME_TMH`; `menu_itemuse.h` `ITFX_N`; `menu_loader.h`
    `CT_MENU_LOADER_TMH CT_MENU_LOADER_NBAND CT_LOADER_CHAPTERS`; `ow_anim.h` `CT_OWANIM_SIZE`; `ow_era_placard.h`
    `CT_PLACARD_N CT_PLACARD_MAXW`. Found from the shapes by `pack_tables.py` (`dim_defines`).
  - 2 that the engine's code uses as an array bound (`COMPILED_IN`): `CT_BHUD_H` (`battle_hud.h`; `src/field.c`
    `battleHudRenderStrip`'s `mathOp[]`/`mathFix[]` and `CtBattleHudRender`'s static `keep[]`) and `CT_MENU_WINLIST_N`
    (`menu_wingrad.h`; `src/menu.c` `mWinGrad`'s static `cache[]` and `built[]`). Both equal a table's dimension
    (`CT_MENU_WINLIST_N` is `ctMenuWinList`'s first), so they say nothing the storage does not.

## The ROM-free runtime

The goal (2026-10-18): once `ct_assets.dat` is extracted, the game needs the ROM for nothing -- each system reads
DECODED assets made from the dump at setup, never the ROM image. Where it stands is measured, not guessed:
`CT_ROM_CENSUS` guards the ROM images by page and records which code reads each one, and
`python tools/rom_census_attract.py` prints how many of the ROM's 1024 pages the whole attract still reads (the
check rom-census-attract; it only goes down).

| Done | Asset types | Checked by |
|---|---|---|
| the sound: the driver as C, songs, samples, instruments, effect sets, its cost table | CT_ASSET_SND_* (28-35) | `ct.exe --sndassets`, the snd-assets-* and snddrv-* checks |
| the location load's LZ streams (maps, tilesets, assemblies), decompressed with both of C3/0557's costs | CT_ASSET_LZ (36) | `ct.exe --lzassets` |
| the small tables: C0's trig, noise and wave tables, the location records, FD's tile-animation and colour-slot programs (their closure over every location), FF's window tiles, text palettes and naming grid | CT_ASSET_TABLE (37) | `ct.exe --tabassets` |
| the location load (package A): every location's map, tilesets, assemblies, BG3 assemblies and script streams by the closure over the 512 records (assets/rom_closure.py), the pointer tables, the scene palettes, bank F6's colour tables, the exits (E5) and the treasure (F5); lz.c's, field_cost.c's and field.c's readers through romview.h | CT_ASSET_LZ, CT_ASSET_TABLE | `ct.exe --lzassets` / `--tabassets`, `tools/rom_free_cmp.py` (the attract's picture and sound, container vs ROM) |
| the sequence stages and the gate warp (package B): the $FE:0000 packet table's streams (lz_pack.py) and raw blocks (the targets `ct.exe --lzpack` reports as not streams, each to the next target), $D2:0000, the gate warp's FD tables (their index ranges from src/seq_gwarp.c) | CT_ASSET_LZ, CT_ASSET_TABLE | `ct.exe --lzassets` / `--tabassets`, the stage checks, `tools/rom_free_cmp.py` |
| the field's sprites and objects (package D): the sprite sheets ($E4:FFE0's gfx table: its LZ streams and raw tile blocks), the PC/NPC and enemy records, the meta, palettes, the four pointer tables, the frame and duration tables, the assemblies; the walk-anim dispatcher C0/9ECD's jump table and handler operands, the triple-tech tables (CC, FF/F9BB), the weapon palettes (CD/6CEC -> D1/4B00); move.c, oam.c and the VM's readers through romview.h | CT_ASSET_LZ, CT_ASSET_TABLE | `ct.exe --lzassets` / `--tabassets`, `tools/rom_free_cmp.py` |
| the text and the menus (package E): the menu strings ($FF:C457, walked by C2/EF7E's codes with their includes), the VWF's strings, font and tables, the window programs (C2/ED31, its operand lengths), the cursor graphs, the menus' HDMA tables, the portraits and plaques, the pop-up, the item names and records, the shops' lists, the exchange screen, the caption records ($FD:A000), the treasure messages and the text dictionary ($DE:FA00); menu.c, menu_cost.c, vwf.c and field.c's text through romview.h. Overlapping extents are packed as one table (extract_assets.py table_spans) | CT_ASSET_LZ, CT_ASSET_TABLE | `ct.exe --lzassets` / `--tabassets`, the menu checks, `tools/rom_free_cmp.py` |
| the overworld (package F): every packet of the ten pointer tables $C6:FDF0-FFFF (each to the next, zero-padded) and the time gauge's three (C2:7220), the world header table $C6:FD00 and the eight 0x17-byte headers; the object VM's scripts walked from the bank-C2 entries the engine starts and from every world's script packet ($C6:FFE0[header +16] at $7F:0400), with each op's length and branch operand read off its own handler (C2:0F91; the anim sub-VM's C2:0E4A for the map markers' C2:6A18 / 6A20), and the ROM data its ops 03 / 04 name; bank C2's and C6's tables by their readers' index ranges (scroll rebuild, row fade, boxes, the Epoch's and Dactyls' tables, the banner's frame, the world map, the time gauge); the place names ($C6:F400) for every entrance's name byte and the map labels; world.c, the world loader in field.c and lz.c's world costs through romview.h | CT_ASSET_LZ, CT_ASSET_TABLE | `ct.exe --lzassets` / `--tabassets`, the overworld checks, `tools/rom_free_cmp.py` |
| the battle (package G, assets/rom_closure_battle.py): the anim scripts by the interpreter's own walk (every dispatch table to the next, both track masks, `1E ss` into CE/0000, the lengths battleAnimOpLen reads from C1/7A6B's handlers -- tools/gen/anim_oplen.py, `ct.exe --animoplen`) and the handlers it scans; what the anim configs stage (the D1 frame streams, the CD palette selections, the CE animation sets by C1/48EC's walk); the enemies' stats, names, rewards and AI scripts (C1/AFE6's two sections each); the PC, tech, item and area records; the effects' programs, layouts (CD/10AB's reading), BG3 and OBJ sheets with their tiles, HDMA presets; the messages it posts and their dictionary; CC:F38C-FCFF's small tables; battle.c's and field.c's battle readers through romview.h | CT_ASSET_LZ, CT_ASSET_TABLE | `ct.exe --lzassets` / `--tabassets`, the quick suite and the full tier's 89 battle checks, `tools/rom_free_cmp.py` |

Each is served in place of the ROM's bytes (src/lz.h `ct_lz_use_assets`, src/romview.h `CtRomByte`/`CtRomPtr`) when
the container holds it; `CT_LZ_FROM_ROM` / `CT_TABLES_FROM_ROM` / `CT_AUDIO_FROM_ROM` keep the ROM as the reference.
The census after these: 0 of 1024 pages -- the whole attract reads nothing from the ROM (it was 406; 183 before the overworld, 130 before the battle). Over the quick suite and the full tier's battle, menu and overworld checks, `CT_ROMVIEW_MISSLOG` + `tools/rom_census.py --viewmiss` (the reads past the tables, by caller) and `CT_LZ_MISSLOG` + `--lzmiss` (the LZ streams taken from a ROM image) find only debug modes that load a ROM file of their own (`--passcost`, the cost probes). CT_ROM_MEMPATCH (a measuring tool's in-memory instrumentation of the image) reaches the tables too (CtRomViewPatch). Package H, the gate: Belthasar runs with NO dump. When no ROM is found and the container is a ROM-free one (src/assets.c CtAssetsRomFree: it holds CT_ASSET_TABLE entries), the engine takes an EMPTY image (src/battle.c CtBattleUseEmptyRom: zeros, never read, guarded by CT_ROM_CENSUS) and reads everything from ct_assets.dat. `python tools/rom_free_gate.py` (the check rom-free-no-dump-gate) runs the whole attract with CT_NO_ROM=1 and with the dump: the pictures and the sound are identical and no page of the empty image is read. The launcher and the Android app start on a ROM-free ct_assets.dat alone; the dump is the setup's input only (tools/setup.py, the ROM screen), as in Ship of Harkinian. The ROM stays the oracle's and the tools' input (ct.exe, tools/dis.py).

## What is never distributed, and why

The game's program, graphics, music, text and data are copyrighted by Square Enix. Anything that contains them, in
whole or in part, cannot be published in this repository:

- the ROM, in any form or format;
- `ct_assets.dat` and any extracted graphics, palettes, maps, scripts, text or dialogue dumps;
- the generated `src/data/*.h` tables and `src/xlat/*.c` translations: most are verbatim copies of ROM bytes or
  of data decoded from them, and the rest are composed from such data;
- savestates, WRAM/VRAM/OAM dumps and framebuffers: an emulator state holds the
  cartridge's RAM, which holds decompressed game data, and a battery save holds game text;
- screenshots and video of the game, with one exception: the handful of pictures and the short clip of the port
  running that the README shows, in [`docs/media/`](media/). They are made by
  [`tools/release/make_media.py`](../tools/release/make_media.py) from the engine's own output during the attract
  demo (the widescreen ones with the Steam margins on, so they show that release's art as the port draws it), and
  they are, with the files named in `tools/release/scan_rom_leaks.py`'s `UPSTREAM` and `export_public.py`'s `KEEP`,
  the only binary files the leak scanner accepts;
- trace and disassembly output that reproduces the ROM's bytes.

The repository holds the code that **produces** these from your dump, and documentation that describes the game's
mechanisms by address and behaviour. The notes in [`disasm/notes/`](../disasm/notes/) cite routines by address
(`C0/5A46`) and describe what they do; they do not reproduce byte dumps or game text.

The public history starts with a fresh commit, so the project's private history, which did contain such
artefacts, is not part of the published repository.

## The leak scanner

[`tools/release/scan_rom_leaks.py`](../tools/release/scan_rom_leaks.py) checks a tree for game bytes before it is
published:

```sh
python tools/release/scan_rom_leaks.py <dir> [--rom ROM] [--corpus ct_assets.dat ...] [--window 24]
```

- **Corpora.** The ROM (always, after verification) plus any file given with `--corpus`. Pass `ct_assets.dat`:
  decompressed graphics never appear in the ROM's raw bytes, so only the extracted container catches them. The
  private generated headers and savestates are also useful corpora.
- **Binary files.** Any binary file in the tree is a finding by itself, except the README's PNG/GIF/MP4 media under
  `docs/media/`, and its bytes are searched too.
- **Text files.** Byte runs are pulled out of C and Python arrays, 16-bit word arrays, assembler byte lists, hex
  dumps, long hex strings and base64 blobs, and each run is searched.
- **Threshold.** A run is a leak when a window of `--window` bytes (24 by default) with at least 8 distinct values
  occurs in a corpus. A table of zeros is not content.
- **Result.** One line per leak, with the file, line, length and corpus offset. The exit code is 1 when anything
  leaked and 0 when the tree is clean.
- **Known coincidences.** A short list of `(path, line)` pairs is accepted with a written reason (`KNOWN_OK`). Today
  it has four entries: the S-DSP's interpolation table in `snes/dsp.c` and the same table in the port's mixer
  (`src/sound/mixer.c`), which are the console's hardware and not the game's data, and the default character names
  in the game's text encoding in one note and in one check's expected line.

Run it on an exported tree before every publication, and on your branch before you open a pull request (see
[CONTRIBUTING.md](../CONTRIBUTING.md)).
