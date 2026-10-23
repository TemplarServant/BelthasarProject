# Credits

BelthasarProject stands on other people's work. This file names it, says what each piece is used for, and gives its license.

## The game

**Chrono Trigger** (SNES, 1995) is by Square Co., Ltd., now Square Enix. Every byte of the game belongs to them.
BelthasarProject contains none of it: the build reads the program and data from a copy of the cartridge that you own
(see [docs/ROM.md](docs/ROM.md)). "Chrono Trigger" is a trademark of Square Enix; this project is not affiliated with
or endorsed by Square Enix or Nintendo.

## Code included in this repository

| Component | Where | Used for | License |
|---|---|---|---|
| [LakeSnes](https://github.com/elzo-d/LakeSnes) by angelo_wf (elzo-d) and contributors | `snes/` | The SNES core inside the *oracle* (`ct.exe`) that runs your ROM to grade the port; its SPC700/DSP are the reference the port's own sound machine was built and graded against (`CT_AUDIO_SPC=ref`) | MIT ([snes/LICENSE.LakeSnes.txt](snes/LICENSE.LakeSnes.txt)) |

## Libraries fetched at build time (not in the repository)

| Library | Fetched by | Used for | License |
|---|---|---|---|
| [SDL2](https://github.com/libsdl-org/SDL) 2.32.10 | `tools/get_sdl2.ps1` (Linux/macOS: the system package) | Window, input, audio, the 2D presenter | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.91.5 | `tools/get_imgui.ps1` | The F1 options overlay | MIT |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) 1.3.296 | `tools/get_vulkan_headers.ps1` | The optional Vulkan presenter | Apache-2.0 / MIT |

## Methodology

- **[snesrev/zelda3](https://github.com/snesrev/zelda3)** -- the model this project follows: a C re-implementation
  checked frame by frame against the original running in an embedded emulator, with the CPU-timing artefacts removed on
  both sides (`zelda_cpu_infra.c`'s deterministic RNG and one main-loop iteration per frame). Our "rule" for timing is
  their approach, adopted literally ([docs/DECISIONS.md](docs/DECISIONS.md)).
- **[Ship of Harkinian](https://github.com/HarbourMasters/Shipwright)** -- the example for the name and for the shape of
  an opt-in enhancement layer on top of a faithful port.

## Reverse-engineering references

Everything taken from these was treated as a *lead* and verified against the ROM and the oracle before use
([docs/METHODOLOGY.md](docs/METHODOLOGY.md)); none of their text or code is copied here except where stated.

- **[CTViewer](https://github.com/GitExl/CTViewer)** by GitExl (MIT) -- the data formats of scenes, tilesets, maps,
  sprites, text and scene scripts. `assets/lz.py`, `assets/script.py`, `assets/text.py` and `src/lz.c` are ports of its
  decoders (`lz_decompress.rs`, `scene_script_decoder.rs`, `text_decoder.rs`), later corrected against the ROM.
- **Michael Springer ("Evil Peer")**, author of Temporal Flux -- the reverse-engineered LZ compression format.
- **[ctrando](https://github.com/Pseudoarc/ctrando)** by Pseudoarc (MIT) -- battle data layouts (techs, attack
  animations, PC stats, memory map), used as leads.
- **[Eternal Nightmare](https://github.com/abyssonym/eternalnightmare)** by abyssonym -- referenced as an older
  randomizer's view of the data.
- **[Data Crystal / TCRF](https://datacrystal.tcrf.net/wiki/Chrono_Trigger)** -- enemy AI, tech and attack
  documentation, the RAM map.
- **[Chrono Compendium](https://www.chronocompendium.com/)**, Geiger's offset documents on romhacking.net, and the
  StrategyWiki / GameFAQs mechanics guides -- battle formulas and AI, as leads.
- **Bisqwit** -- the text encoding.

The list of community sources consulted for the battle system, with what was taken from each, is
[disasm/notes/community_leads.md](disasm/notes/community_leads.md).

## Tools

Built and tested with Visual Studio 2022 Build Tools (MSVC), CMake and Ninja on Windows; gcc/clang build the same
sources on Linux. Python 3 runs the asset pipeline and the test suite. Much of the reverse engineering, the C and the
documentation was written with [Claude Code](https://claude.com/claude-code) (Anthropic) under the project owner's
direction; the decision log records which choices were the owner's.
