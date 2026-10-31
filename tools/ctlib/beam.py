"""Beam traces (`ct.exe --cputrace` with CPUTRACE_BEAM=1): each instruction's PC, its beam line on a linear axis and
its master-cycle stamp, and the non-refresh cycles between two stamps. LakeSnes charges the 40-cycle DRAM refresh
when an instruction carries the beam across h = 536 (snes.c snes_runCycles), so the refreshes between two instruction
starts are the h-536 crossings between them: lines + (h2 >= 536) - (h1 >= 536)."""
import re

RX = re.compile(r'^([0-9A-F]{2}:[0-9A-F]{4}) ([0-9A-F]{2})[0-9A-F]* .* P=(\S+) v=\s*(\d+) h=\s*(\d+) c=(\d+)')
F = 357366 - 262 * 40            # one frame's non-refresh master cycles


def load(path):
    """[(pc, opcode, flags, line, h, cycles)] with `line` linear (v unwrapped across frames). One instruction can carry
    the beam past a whole frame (a DMA longer than 262 lines, e.g. the world loader's C2/26D3), so v going down is not
    the only wrap: the cycle stamp bounds how many lines the step crossed (1364 a line), and each further whole frame
    that fits counts as one more wrap."""
    out, wraps, last = [], 0, None
    for line in open(path):
        m = RX.match(line)
        if not m:
            continue
        v, c = int(m.group(4)), int(m.group(6))
        if last is not None:
            if v < last[0]:
                wraps += 1
            while (wraps + 1) * 262 + v - last[1] <= (c - last[2]) // 1364:
                wraps += 1
        lin = wraps * 262 + v
        last = (v, lin, c)
        out.append((m.group(1), m.group(2), m.group(3), lin, int(m.group(5)), c))
    return out


def nonrefresh(a, b):
    """Non-refresh master cycles from the start of instruction a to the start of instruction b."""
    refreshes = (b[3] - a[3]) + (1 if b[4] >= 536 else 0) - (1 if a[4] >= 536 else 0)
    return b[5] - a[5] - 40 * refreshes
