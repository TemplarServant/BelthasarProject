"""SNES OAM decode (canonical blob: 512-byte low table + 32-byte high table).

Sources of the blob:
  - snes9x states carry it as the PPU block's OAMData field (ctlib.s9x.oam):
    exactly 544 bytes
  - ct.exe --peek <out>.oam sidecars (src/main.c): fwrite(ppu->oam, 2, 512)
    -- 1024 bytes, of which only the FIRST 512 are the low table (LakeSnes
    stores OAM as 256 uint16 words; the write over-reads the struct) --
    then the 32-byte high table at offset 1024, then 512 bytes of CGRAM
    (BGR555 words) at 1056. Total 1568. The build/ppuregs.oam variant is the
    same without CGRAM (1056). load() normalizes all of these to 544 bytes.

Decode lifted from the most complete of the six legacy copies,
tools/_oambird.py: signed 9-bit X (X -= 512 when >= 256), 9-bit tile
(attr bit0 is tile bit8), prio=(attr>>4)&3, pal=(attr>>1)&7, v/h flips.

FIXED vs _oambird: the size column. _oambird printed '%dx' % (8*(size+1)+8)
(= 16/24, nonsense); the high-table size bit only selects small/large --
the actual pixel dimensions depend on OBSEL ($2101) size-select, which is
not in this blob. We report the raw bit honestly as size=0 (small) / 1 (large).

API:
  decode(data: bytes) -> list of 128 dicts
      {idx, x, y, tile, attr, prio, pal, size, vflip, hflip, onscreen}
  load(path) -> 544 bytes (.oam sidecar 544/1056/1568, or a snes9x state)
  load_cgram(path) -> 512 CGRAM bytes (1568-byte sidecars only) or None
"""
OAM_SIZE = 544
# ct.exe --peek sidecar offsets (see module docstring)
_PEEK_LOW_OFF, _PEEK_HIGH_OFF, _PEEK_CGRAM_OFF = 0, 1024, 1056
_PEEK_SIZES = (1056, 1568)


def decode(data):
    """Decode a 544-byte OAM blob -> list of 128 sprite dicts."""
    if len(data) < OAM_SIZE:
        raise ValueError("OAM blob must be %d bytes (512 low + 32 high table), "
                         "got %d" % (OAM_SIZE, len(data)))
    lo = data[:512]
    hi = data[512:544]
    sprites = []
    for i in range(128):
        x = lo[i * 4 + 0]
        y = lo[i * 4 + 1]
        tile = lo[i * 4 + 2]
        attr = lo[i * 4 + 3]
        hb = hi[i // 4]
        shift = (i % 4) * 2
        xb8 = (hb >> shift) & 1
        size = (hb >> (shift + 1)) & 1
        X = x | (xb8 << 8)
        if X >= 256:
            X -= 512                       # signed 9-bit
        sprites.append({
            'idx': i,
            'x': X,
            'y': y,
            'tile': tile | ((attr & 1) << 8),   # 9-bit tile
            'attr': attr,
            'prio': (attr >> 4) & 3,
            'pal': (attr >> 1) & 7,
            'size': size,                        # raw bit: 0=small 1=large (OBSEL decides pixels)
            'vflip': (attr >> 7) & 1,
            'hflip': (attr >> 6) & 1,
            'onscreen': (0 <= X < 256) and (y < 224),
        })
    return sprites


def load(path):
    """Get the canonical 544-byte OAM blob from a .oam sidecar (544 raw, or
    the 1056/1568-byte ct.exe --peek layouts) or a snes9x state."""
    from . import s9x
    if s9x.is_s9x(path):
        return s9x.oam(path)
    with open(path, 'rb') as fh:
        data = fh.read()
    if len(data) == OAM_SIZE:
        return data
    if len(data) in _PEEK_SIZES:
        return (data[_PEEK_LOW_OFF:_PEEK_LOW_OFF + 512] +
                data[_PEEK_HIGH_OFF:_PEEK_HIGH_OFF + 32])
    raise ValueError("%s: not a 544/1056/1568-byte .oam sidecar (%d bytes) "
                     "and not a snes9x state" % (path, len(data)))


def load_cgram(path):
    """The 512-byte CGRAM (256 BGR555 LE words) from a 1568-byte ct.exe
    --peek .oam sidecar, else None."""
    try:
        with open(path, 'rb') as fh:
            data = fh.read()
    except OSError:
        return None
    if len(data) == 1568:
        return data[_PEEK_CGRAM_OFF:_PEEK_CGRAM_OFF + 512]
    return None
