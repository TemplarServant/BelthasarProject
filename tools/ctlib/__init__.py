"""ctlib -- shared reverse-engineering helpers for the Chrono Trigger port.

Stdlib-only. These modules only read the ROM, savestates and oracle dumps;
they never change engine behaviour. tools/README.md maps the older
tools/_*.py probes to the ctlib module that replaces each.

Modules:
  s9x    - snes9x '#!s9xsnp:0011' savestate access (wraps tools/parse_ppu_state.py)
  lake   - LakeSnes .state (CTLS wrap/unwrap + legacy; WRAM @ payload+69)
  fb     - oracle framebuffer (--peek .fb / SHOT.raw) decode + minimal PNG writer
  oam    - 544-byte OAM blob (512 low-table + 32 high-table) decode
  wram   - 128KB WRAM peekers (field object arrays, OW ambient blocks)
  rom    - ROM load / HiROM mapping / caller+writer byte-pattern scan
  script - field event-VM scene-script disassembly (wraps assets/script.py)

Usage (from the repo root):
  import sys; sys.path.insert(0, 'tools')
  from ctlib import wram, oam, fb, rom, script
"""
import os
import sys

_CTLIB_DIR = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.dirname(_CTLIB_DIR)          # .../tools
REPO_DIR = os.path.dirname(TOOLS_DIR)            # repo root
ASSETS_DIR = os.path.join(REPO_DIR, 'assets')

# Make the single-file tools in tools/ (parse_ppu_state.py, disasm65816.py)
# importable wherever ctlib is imported from.
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)
