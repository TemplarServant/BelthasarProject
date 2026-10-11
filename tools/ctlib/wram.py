"""WRAM peekers over a raw 128KB blob (offset 0 = $7E0000, +0x10000 = bank $7F).

Two dump sources, one layout:
  - .wram sidecars from ct.exe --peek (raw 0x20000-byte file)
  - savestates: LakeSnes .state (ctlib.lake) or snes9x .state (ctlib.s9x)

Field-object array lifted from the canonical tools/_rdstate.py, extended with
the cadence/speed/ticket/facing slots documented in the memory notes:
  $1801/$1881,X tile   $1800/$1880,X foot px   $1B01,X solid   $0F00,X active
  $1100,X status(move-mode)   $1C00,X prio   $1180,X script ptr (word)
  $1780,X anim mode   $1000/$1001,X cadence/phase   $1A00/$1A01,X speed/ticket
  $1600,X facing      (X = obj*2)
Globals: $0197 player obj index (*2), locks $0162/$011F/$0120.

Overworld ambient blocks lifted from tools/_oracleamb.py (its only decoder):
  $0B30-$1B30 stride 0x40, live iff word@+0 == 0x0F63;
  pc @+0x07 (word), attr @+0x0F, pos @+0x14/+0x18 (words), vel @+0x1A/+0x1E.

API:
  detect(path) -> 'wram' | 's9x' | 'lake'
  load(path)   -> 128KB bytes (auto-detected)
  u8/u16(w, addr)  -- addr may be $7Exxxx / $7Fxxxx or a bare 17-bit offset
  player(w)    -> dict (player obj + control locks)
  objs(w, count=30) -> list of per-object dicts
  ow_ambient(w) -> list of live ambient-block dicts
"""
import os

from . import lake, s9x

WRAM_SIZE = 0x20000


def detect(path):
    """Classify a dump/savestate: raw 128KB 'wram' sidecar, 's9x', or 'lake'."""
    try:
        size = os.path.getsize(path)
    except OSError:
        raise ValueError("cannot stat %s" % path)
    if size == WRAM_SIZE:
        return 'wram'
    if s9x.is_s9x(path):
        return 's9x'
    if lake.is_lake(path):
        return 'lake'
    raise ValueError("%s: not a 128KB .wram sidecar, snes9x state, or "
                     "LakeSnes state" % path)


def load(path):
    """Auto-detect the container and return the 128KB WRAM bytes."""
    kind = detect(path)
    if kind == 'wram':
        with open(path, 'rb') as fh:
            return fh.read(WRAM_SIZE)
    if kind == 's9x':
        return s9x.wram(path)
    return lake.wram(path)


def _off(addr):
    return addr - 0x7E0000 if addr >= 0x7E0000 else addr


def u8(w, addr):
    return w[_off(addr)]


def u16(w, addr):
    o = _off(addr)
    return w[o] | (w[o + 1] << 8)


def player(w):
    """Player object index ($0197, stored *2) + the control locks."""
    return {
        'playerObjRaw': u8(w, 0x7E0197),
        'playerObj': u8(w, 0x7E0197) // 2,
        'lock0162': u8(w, 0x7E0162),      # cutscene lock
        'ctrl011F': u8(w, 0x7E011F),      # explore/control flag
        'path0120': u8(w, 0x7E0120),      # player-path flag
    }


def objs(w, count=30):
    """Per-object field rows (X = obj*2), the tools/_rdstate.py set + extras."""
    rows = []
    for ob in range(count):
        X = ob * 2
        rows.append({
            'obj': ob,
            'xtile': u8(w, 0x7E1801 + X),
            'ytile': u8(w, 0x7E1881 + X),
            'xfoot': u8(w, 0x7E1800 + X),
            'yfoot': u8(w, 0x7E1880 + X),
            'solid': u8(w, 0x7E1B01 + X) & 1,
            'active': u8(w, 0x7E0F00 + X),
            'status': u8(w, 0x7E1100 + X),
            'prio': u8(w, 0x7E1C00 + X),
            'scriptptr': u16(w, 0x7E1180 + X),
            'anim': u8(w, 0x7E1780 + X),
            'cadence': u8(w, 0x7E1000 + X),
            'cadphase': u8(w, 0x7E1001 + X),
            'speed': u8(w, 0x7E1A00 + X),
            'ticket': u8(w, 0x7E1A01 + X),
            'facing': u8(w, 0x7E1600 + X),
        })
    return rows


OW_AMB_START, OW_AMB_END, OW_AMB_STRIDE = 0x0B30, 0x1B30, 0x40
OW_AMB_SIG = 0x0F63


def ow_ambient(w):
    """Live overworld ambient-object blocks ($0B30-$1B30 stride 0x40,
    signature word 0x0F63). Layout per tools/_oracleamb.py.
    CAVEAT: only meaningful for OVERWORLD-mode WRAM -- the field engine
    reuses this RAM, so on a field-scene dump the signature can match
    stale/aliased data (same behavior as the legacy tool)."""
    out = []
    for b in range(OW_AMB_START, OW_AMB_END, OW_AMB_STRIDE):
        if u16(w, b) != OW_AMB_SIG:
            continue
        out.append({
            'off': b,
            'pc': u16(w, b + 0x07),
            'attr': u8(w, b + 0x0F),
            'x': u16(w, b + 0x14),
            'y': u16(w, b + 0x18),
            'vx': u16(w, b + 0x1A),
            'vy': u16(w, b + 0x1E),
        })
    return out
