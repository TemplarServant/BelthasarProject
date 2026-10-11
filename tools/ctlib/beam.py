"""Beam traces (`ct.exe --cputrace` with CPUTRACE_BEAM=1): each instruction's PC, its beam line on a linear axis and
its master-cycle stamp, and the non-refresh cycles between two stamps -- LakeSnes charges the 40-cycle DRAM refresh
when an instruction carries the beam across h = 536 (snes.c snes_runCycles), so the refreshes between two instruction
starts are the h-536 crossings between them: lines + (h2 >= 536) - (h1 >= 536)."""
import re

RX = re.compile(r'^([0-9A-F]{2}:[0-9A-F]{4}) ([0-9A-F]{2})[0-9A-F]* .* P=(\S+) v=\s*(\d+) h=\s*(\d+) c=(\d+)')
F = 357366 - 262 * 40            # one frame's non-refresh master cycles


def load(path):
    """[(pc, opcode, flags, line, h, cycles)] with `line` linear (v unwrapped across frames)."""
    out, wraps, lastv = [], 0, None
    for line in open(path):
        m = RX.match(line)
        if not m:
            continue
        v = int(m.group(4))
        if lastv is not None and v < lastv:
            wraps += 1
        lastv = v
        out.append((m.group(1), m.group(2), m.group(3), wraps * 262 + v, int(m.group(5)), int(m.group(6))))
    return out


def nonrefresh(a, b):
    """Non-refresh master cycles from the start of instruction a to the start of instruction b."""
    refreshes = (b[3] - a[3]) + (1 if b[4] >= 536 else 0) - (1 if a[4] >= 536 else 0)
    return b[5] - a[5] - 40 * refreshes
