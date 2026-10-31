"""snes9x ('#!s9xsnp:0011') savestate access.

Thin wrapper over tools/parse_ppu_state.py, which owns the block splitter
and the 2652-byte big-endian SnapPPU[] decode (plus the FIL $21xx shadow).

API:
  is_s9x(path)        -> bool (gzip'd or raw '#!s9xsnp' container)
  read_raw(path)      -> the decompressed container bytes
  split_blocks(path)  -> dict name -> bytes ('RAM', 'PPU', 'REG', 'FIL', ...)
  wram(path)          -> 128KB WRAM bytes (the 'RAM' block)
  ppu(path)           -> decoded PPU regs/OAM/CGRAM: {'ppu': {...}, 'fil': {...}}
  oam(path)           -> the 544-byte OAMData blob from the PPU block
"""
import gzip

from . import TOOLS_DIR  # noqa: F401  (package __init__ puts tools/ on sys.path)
import parse_ppu_state as _pps

MAGIC = b"#!s9xsnp:"
WRAM_SIZE = 0x20000


def read_raw(path):
    """Read a snes9x state, transparently gunzipping. Returns the container bytes."""
    with open(path, 'rb') as fh:
        raw = fh.read()
    if raw[:2] == b'\x1f\x8b':
        raw = gzip.decompress(raw)
    return raw


def is_s9x(path):
    """True if `path` is a snes9x snapshot container (gzip'd or raw)."""
    try:
        with open(path, 'rb') as fh:
            head = fh.read(16)
    except OSError:
        return False
    if head[:2] == b'\x1f\x8b':
        try:
            with gzip.open(path, 'rb') as gz:
                head = gz.read(16)
        except OSError:
            return False
    return head[:len(MAGIC)] == MAGIC


def split_blocks(path):
    """Split the state into its 'NAME:SIZE:' blocks -> dict name -> bytes."""
    return _pps._split_blocks(read_raw(path))


def wram(path):
    """The 128KB WRAM ('RAM' block; offset 0 = $7E0000, +0x10000 = bank $7F)."""
    ram = split_blocks(path)["RAM"]
    if len(ram) < WRAM_SIZE:
        raise ValueError("RAM block too small: %d < 0x20000" % len(ram))
    return bytes(ram[:WRAM_SIZE])


def ppu(path):
    """Decoded PPU state via parse_ppu_state.parse_state_file:
    {'ppu': {bgmode, bg[], cgdata[256], oamData(544B), window, clip, ...},
     'fil': {tm, ts, cgwsel, ...}}  ('fil' only if the FIL block is present)."""
    res, _blocks = _pps.parse_state_file(path)
    return res


def oam(path):
    """The 544-byte OAMData blob from the PPU block (feed to ctlib.oam.decode)."""
    return ppu(path)['ppu']['oamData']
