"""LakeSnes .state layout (the states written/read by build/ct.exe).

Two on-disk formats:

  CTLS (what ct.exe writes):
    magic[4]      = b'CTLS'
    version u32LE = 1
    length  u32LE = payload bytes
    crc32   u32LE = IEEE CRC-32 of the payload (zlib/binascii compatible)
    payload[...]  = raw snes_handleState stream

  Legacy (older .state files): bare handleState stream, no header.
    Detected by size + the 11 leading 0/1 bool bytes.

Payload layout (snes_handleState -- see snes/snes.c / snes/cpu.c):

  off 0            : snes_handleState header = 11 bools + 2 bytes +
                     12 LE words + 2 LE ints + 2 LE int64 + 1 double = 69 bytes
  off 69           : WRAM byte array, 0x20000
  off 69 + 0x20000 : cpu_handleState = 15 bools (c z v n i d xf mf e waiting
                     stopped irqWanted nmiWanted intWanted resetWanted)
                     + 2 bytes (k, db) + 6 LE words (a x y sp pc dp) = 29 bytes
  ...              : dma/ppu/apu/input/cart state follows.

Note: Belthasar F5/F9 states are a different format (the raw CtScene struct,
handled in C); this module is only for the ct.exe (LakeSnes oracle) states.
"""
import binascii
import os
import struct

WRAM_OFF = 69
WRAM_SIZE = 0x20000
CPU_OFF = WRAM_OFF + WRAM_SIZE
CPU_SIZE = 29
MIN_PAYLOAD = CPU_OFF + CPU_SIZE
# the PPU's VRAM (0x8000 words, little-endian), 396 bytes after the WRAM block (the CPU's and the DMA's blocks, then the
# PPU's, which opens with it): measured -- byte-equal to the oracle's --peek .vram on the attract's rule state and on
# saves/battle_re/party3_field.state (2026-11-17)
VRAM_OFF = 131537
VRAM_SIZE = 0x10000

CTLS_MAGIC = b"CTLS"
CTLS_VERSION = 1
CTLS_HDR_SIZE = 16

_CPU_BOOLS = ("c", "z", "v", "n", "i", "d", "xf", "mf", "e", "waiting",
              "stopped", "irqWanted", "nmiWanted", "intWanted", "resetWanted")
_CPU_WORDS = ("a", "x", "y", "sp", "pc", "dp")


def crc32(data: bytes) -> int:
    """IEEE CRC-32, same as binascii.crc32 & C CtLakeCrc32 (unsigned 32-bit)."""
    return binascii.crc32(data) & 0xFFFFFFFF


def is_wrapped(data: bytes) -> bool:
    return len(data) >= CTLS_HDR_SIZE and data[:4] == CTLS_MAGIC


def looks_legacy(data: bytes) -> bool:
    if len(data) < MIN_PAYLOAD:
        return False
    if data[:2] == b"\x1f\x8b" or data[:9] == b"#!s9xsnp:":
        return False
    if data[:4] in (b"CTFS", b"LSSF", CTLS_MAGIC):
        return False
    return all(v in (0, 1) for v in data[:11])


def unwrap(data: bytes):
    """Return (payload_bytes, fmt) where fmt is 'CTLS' or 'legacy'.

    Raises ValueError on truncated/CRC-mismatched CTLS or unrecognised input.
    """
    if is_wrapped(data):
        ver, plen, want = struct.unpack_from("<III", data, 4)
        if ver != CTLS_VERSION:
            raise ValueError("CTLS version %u unsupported (need %u)" % (ver, CTLS_VERSION))
        if plen < MIN_PAYLOAD:
            raise ValueError("CTLS payload too small (%u)" % plen)
        expect = CTLS_HDR_SIZE + plen
        if len(data) != expect:
            raise ValueError(
                "CTLS length mismatch (file %d B, expected %d B)" % (len(data), expect)
            )
        payload = data[CTLS_HDR_SIZE:CTLS_HDR_SIZE + plen]
        got = crc32(payload)
        if got != want:
            raise ValueError(
                "CTLS CRC mismatch (got %08X, header %08X)" % (got, want)
            )
        return payload, "CTLS"
    if looks_legacy(data):
        return data, "legacy"
    raise ValueError(
        "not a CTLS or legacy LakeSnes state (size=%d head=%r)" % (len(data), data[:4])
    )


def wrap(payload: bytes) -> bytes:
    """Build a CTLS-wrapped file image from a raw handleState payload."""
    if len(payload) < MIN_PAYLOAD:
        raise ValueError("payload too small for LakeSnes state: %d" % len(payload))
    return (
        CTLS_MAGIC
        + struct.pack("<III", CTLS_VERSION, len(payload), crc32(payload))
        + payload
    )


def read_payload(path: str):
    """Read path -> (payload, fmt). See unwrap()."""
    with open(path, "rb") as fh:
        return unwrap(fh.read())


def write_wrapped(path: str, payload: bytes) -> int:
    """Write payload as CTLS. Returns total file size."""
    blob = wrap(payload)
    with open(path, "wb") as fh:
        fh.write(blob)
    return len(blob)


def is_lake(path):
    """Sniff: CTLS (CRC-checked) or legacy LakeSnes state.

    Returns False for gzip / snes9x / CTFS / LSSF / CRC-failed CTLS / too-small files.
    """
    try:
        with open(path, "rb") as fh:
            data = fh.read()
        unwrap(data)
        return True
    except (OSError, ValueError):
        return False


def wram(path):
    """The 128KB WRAM (offset 0 = $7E0000, +0x10000 = bank $7F)."""
    payload, _ = read_payload(path)
    w = payload[WRAM_OFF:WRAM_OFF + WRAM_SIZE]
    if len(w) != WRAM_SIZE:
        raise ValueError("state too small for WRAM @69+0x20000: %s" % path)
    return w


def vram(path):
    """The PPU's 64KB VRAM (word w at byte 2*w)."""
    payload, _ = read_payload(path)
    v = payload[VRAM_OFF:VRAM_OFF + VRAM_SIZE]
    if len(v) != VRAM_SIZE:
        raise ValueError("state too small for VRAM @%d+0x10000: %s" % (VRAM_OFF, path))
    return v


def seed_vram_env(state_path, out_path):
    """The env a Belthasar run seeded from a state's WRAM (--loadwram) needs for the state's VRAM as well: a WRAM image
    carries no VRAM, and the field's sprites drawn from OAM + VRAM (CT_OAMRENDER=1) take their tiles from it.
    {CT_SEED_VRAM: out_path} with the state's VRAM written there while the OAM renderer is on; {} otherwise (the asset
    drawer draws the objects from the assets, and the run stays as it was)."""
    if os.environ.get("CT_OAMRENDER") != "1":
        return {}
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as fh:
        fh.write(vram(state_path))
    return {"CT_SEED_VRAM": out_path}


def cpu(path):
    """Decode the 29-byte cpu_handleState block -> dict of flags + registers
    (bools c..resetWanted, bytes k/db, LE words a x y sp pc dp)."""
    payload, _ = read_payload(path)
    blk = payload[CPU_OFF:CPU_OFF + CPU_SIZE]
    if len(blk) != CPU_SIZE:
        raise ValueError("state too small for CPU block: %s" % path)
    out = {}
    for i, name in enumerate(_CPU_BOOLS):
        out[name] = blk[i]
    out["k"] = blk[15]
    out["db"] = blk[16]
    for i, name in enumerate(_CPU_WORDS):
        o = 17 + i * 2
        out[name] = blk[o] | (blk[o + 1] << 8)
    return out
