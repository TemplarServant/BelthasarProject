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

## What the build does with it

Everything derived from the ROM is generated on your machine:

| Output | Made by | Used for |
|---|---|---|
| `ct_assets.dat` | [`assets/extract_assets.py`](../assets/extract_assets.py) | The asset container: scene palettes, tilesets, maps, sprites, the font, strings and dialogue, scene scripts, collision, exits. The field and the menus draw from it without the ROM. |
| `src/data/*.h` | the generators in `tools/` | 47 tables the engine compiles in: battle animation records, menu chrome, the title sequence, the overworld placard, and so on. Of these, 12 are generated from the ROM alone and 35 by oracle runs from power-on (the recipes in `tools/recipes/`). |
| `src/xlat/*.c` | [`tools/gen/xlat_all.py`](../tools/gen/xlat_all.py) (traces) + [`tools/gen/xlat.py`](../tools/gen/xlat.py) | Five timing-sensitive ROM programs translated instruction by instruction into C (see [ARCHITECTURE.md](ARCHITECTURE.md#translated-stages)). |
| Oracle states | recipes replayed by the oracle | The savestates the generators and the regression suite start from. Each recipe is a pad script played from power-on, optionally on top of a battery save that another recipe produced. |

At run time `Belthasar` reads `ct_assets.dat`. It reads the ROM itself for battle enemies (graphics and stats) and
for the sound driver. Without the ROM it still renders the field and the menus, and says what it is missing.

The single entry point is `python tools/setup.py --rom <path>`: verify the ROM, build a bootstrap oracle, replay
the recipes, generate, build ([README.md](../README.md#2-set-up-from-your-rom)).

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
  demo, and they are the only binary files the leak scanner accepts;
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
- **Known coincidences.** A short list of `(path, line)` pairs is accepted with a written reason. Today it has one
  entry: the S-DSP's interpolation table in `snes/dsp.c`, which is the console's hardware and not the game's data.

Run it on an exported tree before every publication, and on your branch before you open a pull request (see
[CONTRIBUTING.md](../CONTRIBUTING.md)).
