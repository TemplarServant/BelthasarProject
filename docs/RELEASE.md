# Release packages

A player's release of Belthasar is a folder -- zipped on Windows, a `.tar.gz` on Linux -- with **no byte of the
game** inside. The player opens Belthasar, chooses their own dump of Chrono Trigger (USA) on its setup screen, and the
game's data is made from it on their computer, once. From then on Belthasar reads only that data; the ROM is not needed
to play ([ROM.md](ROM.md#the-rom-free-runtime)).

## Building one

    python tools/release/package.py [--out build/release] [--rom ROM] [--python DIR | --no-python] [--keep]

It runs on the platform it packages for (a Windows package on Windows, a Linux one on Linux):

| Step | What | Checked by |
|---|---|---|
| 1 | the public tree: `git archive HEAD` into `<out>/tree`, plus `third_party/` (SDL2, Dear ImGui, the Vulkan headers -- git-ignored, copied from the developer's tree) | -- |
| 2 | the shapes (`tools/gen/shapes.py install`): every generated file's declarations with every array zero, in place of `src/data/*.h` and `src/xlat/*.c` | `shapes.py check` (the suite) |
| 3 | the build in that tree: `tools/build.py ct` (the setup's oracle, from the shapes) and `CT_DATAFREE=1 tools/build.py Belthasar` (no generated table compiled in) | `tools/build.py`'s per-target verification |
| 4 | the release folder, then `tools/release/scan_rom_leaks.py --binaries` over it against the ROM and `ct_assets.dat` | the scan must say `clean` |
| 5 | the archive: `Belthasar-<git describe>-<os>-<arch>.zip` / `.tar.gz` | -- |

The release must be built from a **committed** tree: step 1 takes `HEAD`, not the working copy.

On Linux the build takes the system SDL2 (`libsdl2-dev`), cmake, ninja and gcc; `third_party/` holds only Dear ImGui
1.91.5 (`python3 tools/get_imgui.py`) and the Vulkan headers 1.3.296 (`include/` of KhronosGroup/Vulkan-Headers). The
executables need the glibc of the machine that built them or newer, so build on the oldest distribution the package is
for (Ubuntu 24.04: glibc 2.38 for Belthasar). The player installs `libsdl2-2.0-0` and `python3`; `zenity` or `kdialog`
gives the setup screen its Browse... (dropping the file on the window works without them).

## What is inside

    Belthasar-<version>-windows-x64/
        Belthasar.exe            the game (data-free)
        SDL2.dll                 SDL2's runtime (zlib licence)
        setup/ct.exe             the oracle the setup runs (built from the shapes), and its SDL2.dll
        setup/kit/               the public tree: tools/setup.py and everything it runs
        setup/python/            a Python runtime: the packager's own, pruned of what the setup never imports (PSF licence)
        LICENSE, CREDITS.md, README.txt

On Linux the Python is the player's `python3` unless `--python DIR` names one to bundle. macOS keeps its own start
(src/macos_paths.m, a folder chooser) and is not packaged by this script yet.

## The leak scan of a release

`scan_rom_leaks.py --binaries` reads every file of the folder:

- text files as in the public tree's scan (their arrays, hex dumps, base64 ...), the kit's known exceptions included
  (`KNOWN_OK`, matched under `setup/kit/` too);
- binaries -- the executables -- are not findings by themselves; their bytes are searched against an index of the
  corpora (every 16th window), which finds any run of 40 bytes or more they share with the ROM or `ct_assets.dat`;
- windows that are structure, not content, never count: one value repeated, a ramp (`00 01 02 ..`), a word ramp
  (`00 00 01 00 ..`), bit masks (`80 40 20 ..`), two such pieces end to end (a padding run, then a ramp:
  `00 00 .. 00 01 02 ..`, found in the Linux `ct` against `ct_assets.dat`) -- identity, index and bit tables any
  program holds. Made two pieces: 12 of the ROM's 243,536 content windows (every 16th) and 128 of `ct_assets.dat`'s
  505,857 stop counting;
- the upstream runtimes shipped as they are -- `SDL2.dll`, the bundled Python -- are listed with `--third-party` and
  not searched (their own matches are ramps and zero runs against the container).

The scanner's two controls: the developer's `src/data/*.h` (tables made from the ROM) must come out NOT CLEAN, and an
executable with 64 ROM bytes inside must be found.

## The setup screen

The setup the screen runs also makes `ct_wide.dat` when the player has the Steam release installed (the widescreen's
Steam margins, [WIDESCREEN.md](WIDESCREEN.md)): an optional step that never fails the setup. The release ships none of
the Steam data, only the kit's `tools/steam_wide.py` that reads the player's own copy.

The first run shows the setup screen (src/launcher_ui.cpp): a desktop form with the system's UI font -- the ROM file
(Browse... opens the system's own chooser; dropping the file on the window works too), the game data's folder (Open
folder), the progress with the setup's step and the elapsed time, and Make the game data / Cancel / Try again / Play.
`CT_LAUNCHER_SHOT=<file.bmp>` with `CT_LAUNCHER_DEMO=choose|bad|verified|running|failed|done` renders a page to a
picture without running anything (docs/DEBUG_SURFACE.md).
