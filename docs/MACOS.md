# macOS build and app bundle

## Build

Install Python 3 and Xcode Command Line Tools (`xcode-select --install`).
From the repository root, run:

```sh
brew install cmake ninja pkg-config sdl2
python3 tools/get_imgui.py
python3 tools/setup.py --rom "/path/to/Chrono Trigger (USA).sfc"
python3 tools/package_macos.py --with-game-data
open "build/macos/Chrono Trigger.app"
```

Use your own [supported ROM dump](ROM.md). Setup generates the game data and
builds the engine for the current Mac's architecture. Dear ImGui provides the
existing options overlay and controller remapping.

Playable builds contain generated game data even without a bundled ROM.
Keep them local, following the project's [distribution rules](ROM.md#what-is-never-distributed-and-why).

## App data

`--with-game-data` copies your local ROM and `ct_assets.dat` into the app.
Omit it to keep those files outside the bundle:

```sh
python3 tools/package_macos.py
```

On launch, choose the folder containing `Chrono_Trigger_USA_SNES.sfc` and
`ct_assets.dat` from setup. The app remembers the folder. You can also place
both files in `~/Library/Application Support/BelthasarProject/GameData`, or
set `CT_DATA_DIR` to their absolute folder path.

The app writes these files under `~/Library/Application Support/BelthasarProject`:

| File | Contents |
| --- | --- |
| `ct_sram.srm` | Cartridge saves |
| `ct_settings.ini` | Settings and control mappings |
| `latest.log` | Latest Finder launch output |

`CT_ROM`, `CT_SRAM` and `CT_SETTINGS` overrides remain available. Running
`build/ct_field` directly keeps the existing command line behavior. The
`build/ct*.exe` symlinks let existing setup and regression tools use the native
executables.

## Controls and window

See the [control bindings](../README.md#controls) and
[options overlay](../README.md#the-f1-options-overlay) in the README. On a Mac,
Return is Start and Delete is Select.

Choose **Chrono Trigger > Settings…** or press **F1** to open the overlay.
F1 or Escape closes it. F1 may require Fn/Globe. Command+Q quits.

Resizing keeps the aspect ratio selected in the Video settings. Fullscreen
uses the selected picture scaling mode.

## Packaging options

The packager includes the required SDL libraries and applies an ad hoc signature.
Run `python3 tools/package_macos.py --help` for output, signing and version flags.
For example:

```sh
python3 tools/package_macos.py --version 1.2.3 --build-number 2
```

The app metadata records the packaging checkout's Git revision and whether it
has tracked changes. Rebuild the engine before packaging source changes.

## Validation

Follow [CONTRIBUTING.md](../CONTRIBUTING.md) and the
[testing overview](TESTING_OVERVIEW.md) for the project gates. Mac host tools
and the packaged signature have these additional checks:

```sh
python3 -m unittest discover -s tests -v
codesign --verify --deep --strict "build/macos/Chrono Trigger.app"
```

Physical controller use, a full playthrough, Intel hardware and older macOS
versions remain unverified.
