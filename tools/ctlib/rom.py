"""ROM access: load (copier-header strip), HiROM addressing, xref scanning.

Addressing comes from the proven tools/disasm65816.py hirom_off (imported,
not re-derived -- the legacy _find*/_refscan scripts each re-invented this
math, inconsistently).

refs() generalizes tools/_refscan.py: for a target address it finds
  callers: JSR abs (0x20 lo hi; a JSR only reaches its OWN bank, so the call
           site's bank names the real target) and JSL long (0x22 lo hi bk)
  writers: STA abs/abs,X/abs,Y (8D/9D/99) and STZ abs/abs,X (9C/9E)
NOTE: this is a heuristic byte-pattern scan (can false-positive on data /
operand coincidences) -- confirm hits by disassembling around them.

API:
  load(path=None) -> ROM bytes (auto-strips a 512-byte copier header)
  hirom_off(addr, romsize) -> file offset (re-export)
  off_to_snes(off) -> (bank, addr16)
  refs(rom, addr) -> {'note', 'callers': [...], 'writers': [...]}
"""
import os

from . import REPO_DIR, TOOLS_DIR  # noqa: F401
import disasm65816 as _d65

hirom_off = _d65.hirom_off

DEFAULT_ROM = 'Chrono_Trigger_USA_SNES.sfc'
NOTE = 'heuristic byte-pattern scan (can false-positive on data)'


def find_default(name=DEFAULT_ROM):
    """Resolve the default ROM: CWD first, then the repo root."""
    for cand in (name, os.path.join(REPO_DIR, name)):
        if os.path.isfile(cand):
            return cand
    raise FileNotFoundError("ROM not found: %s (looked in CWD and repo root)" % name)


# The one ROM this project supports: Chrono Trigger (USA), the No-Intro dump, headerless.
ROM_SHA1 = 'de5822f4f2f7a55acb8926d4c0eaa63d5d989312'
ROM_CRC32 = 0x2D206BF7
ROM_SIZE = 4 * 1024 * 1024


class RomError(Exception):
    pass


def verify(data):
    """Raise RomError unless `data` (header already stripped) is the supported dump."""
    import hashlib
    if len(data) != ROM_SIZE or hashlib.sha1(data).hexdigest() != ROM_SHA1:
        raise RomError("this is not the supported ROM: Chrono Trigger (USA), SHA-1 %s, %d bytes (got %d bytes, "
                       "SHA-1 %s). Dump your own cartridge; see docs/ROM.md."
                       % (ROM_SHA1, ROM_SIZE, len(data), hashlib.sha1(data).hexdigest()))
    return data


def load(path=None, check=True):
    """Read the ROM; auto-strip a 512-byte copier header (size % 1024 == 512); verify it unless check=False."""
    if path is None:
        path = os.environ.get('CT_ROM') or find_default()
    with open(path, 'rb') as fh:
        d = fh.read()
    if len(d) % 1024 == 512:
        d = d[512:]
    return verify(d) if check else d


def off_to_snes(off):
    """HiROM file offset -> (bank, addr16). Inverse of hirom_off for the
    canonical $C0-$FF mapping (bank = 0xC0 + off>>16)."""
    return (0xC0 + (off >> 16), off & 0xFFFF)


def _scan(rom, pat):
    res = []
    i = 0
    while True:
        j = rom.find(pat, i)
        if j < 0:
            break
        res.append(j)
        i = j + 1
    return res


def refs(rom, addr):
    """Cross-references to `addr`.

    addr <= 0xFFFF: bank-agnostic -- JSR callers from ANY bank are reported
    (each labelled with the site's own bank, which IS the target bank for a
    JSR); JSL matches any ROM operand bank >= 0xC0.
    addr > 0xFFFF (24-bit, e.g. 0xC05A93): JSR callers only from that bank,
    JSL only with that exact operand bank.
    Returns {'note': NOTE, 'callers': [...], 'writers': [...]}; each entry is
    {'kind', 'bank', 'addr', 'off'} for the SITE of the reference."""
    a16 = addr & 0xFFFF
    bank = (addr >> 16) & 0xFF if addr > 0xFFFF else None
    lo, hi = a16 & 0xFF, (a16 >> 8) & 0xFF
    out = {'note': NOTE, 'callers': [], 'writers': []}

    for off in _scan(rom, bytes([0x20, lo, hi])):                 # JSR abs
        b, a = off_to_snes(off)
        if bank is None or b == bank:
            out['callers'].append({'kind': 'JSR', 'bank': b, 'addr': a, 'off': off})
    for off in _scan(rom, bytes([0x22, lo, hi])):                 # JSL long
        bk = rom[off + 3] if off + 3 < len(rom) else -1
        if (bank is None and bk >= 0xC0) or (bank is not None and bk == bank):
            b, a = off_to_snes(off)
            out['callers'].append({'kind': 'JSL', 'bank': b, 'addr': a,
                                   'off': off, 'target_bank': bk})

    for op, nm in [(0x8D, 'STA abs'), (0x9D, 'STA abs,X'), (0x99, 'STA abs,Y'),
                   (0x9C, 'STZ abs'), (0x9E, 'STZ abs,X')]:
        for off in _scan(rom, bytes([op, lo, hi])):
            b, a = off_to_snes(off)
            out['writers'].append({'kind': nm, 'bank': b, 'addr': a, 'off': off})
    return out
