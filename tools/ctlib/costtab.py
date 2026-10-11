"""Instruction costs read off beam traces, for hand-walked cost models (tools/pass_cost_proto.py).

Each executed instruction is charged the non-refresh master cycles to the next traced one (ctlib/beam.py). A PC's
costs are then its base -- the one value an instruction with fixed timing always has -- or, for a branch, two (not
taken / taken). A PC whose cost depends on the data (a long read from ROM or from WRAM, a DMA's sync) shows more,
and `base()` refuses it: the walker has to price that access itself.

    T = CostTab([trace, ...])
    T.lin(0xC0B0E6, 0xC0B0F1)   the straight-line run from B0E6 up to (not including) B0F1, branches not taken
    T.nt(0xC0B0F6), T.tk(0xC0B0F6)

`lin` steps instruction by instruction with the widths the CPU really had (the trace's M/X flags) and refuses a run
that holds an instruction no trace executed, or a block move (MVN/MVP repeat per byte: price them explicitly).

`lin_rom(a, b, m, x, d, db)` prices code NO trace executed (an arm the fixtures never took): it decodes the ROM and
charges each instruction the cost of an EXECUTED one with the same key -- the opcode, the M/X widths, and the
region its operand lands in (LakeSnes's snes_getAccessTime depends on the address, never on the PC: every
instruction of this engine runs from FastROM bank C0). A key no trace holds is refused.
"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import beam  # noqa: E402
import disasm65816 as D  # noqa: E402

_REGS = re.compile(r' D=([0-9A-F]{4}) B=([0-9A-F]{2}) P=(\S{8})')
ROM = open(os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                        'Chrono_Trigger_USA_SNES.sfc'), 'rb').read()


def region(bank, addr):
    """the access-time class LakeSnes gives bank:addr (MEMSEL set)"""
    if bank in (0x7E, 0x7F):
        return 'w8'
    if 0x40 <= bank < 0x7E:
        return 's8'
    if bank >= 0xC0:
        return 'f6'
    if addr < 0x2000:
        return 'w8'
    if addr < 0x4000:
        return 'i6'
    if addr < 0x4200:
        return 'i12'
    if addr < 0x6000:
        return 'i6'
    if addr < 0x8000:
        return 's8'
    return 'f6' if bank >= 0x80 else 's8'


# modes whose data access is at a fixed address computable from the operand
_DP = {'dp', 'dpx', 'dpy'}
_ABS = {'abs', 'abx', 'aby'}
_LONG = {'abl', 'alx'}
_NODATA = {'imp', 'acc', 'immM', 'immX', 'imm8', 'rel', 'rell'}


def key_of(op, m, x, d, db, operand):
    mnem, mode = D.TABLE[op]
    if mnem in ('JSR', 'JSL', 'RTS', 'RTL', 'BRA', 'BRL', 'JMP', 'JML', 'PHB', 'PLB', 'PHD', 'PLD', 'PHA', 'PLA',
                'PHX', 'PLX', 'PHY', 'PLY', 'PHP', 'PLP', 'PHK', 'TCD', 'TDC', 'XBA', 'SEP', 'REP'):
        return (op, m, x)
    if mode in _NODATA:
        return (op, m, x)
    if mode in _DP:
        return (op, m, x, region(0, (d + operand) & 0xFFFF), d & 0xFF)
    if mode in _ABS:
        return (op, m, x, region(db, operand))
    if mode in _LONG:
        return (op, m, x, region(operand >> 16, operand & 0xFFFF))
    return None                                                   # indirect: the pointer decides -- no key


class CostTab:
    BRANCHES = {0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0}

    def __init__(self, paths):
        self.dist = collections.defaultdict(collections.Counter)
        self.op = {}
        self.len = {}
        self.keycost = collections.defaultdict(collections.Counter)
        for p in paths:
            t = beam.load(p)
            regs = [m.groups() for m in (_REGS.search(line) for line in open(p) if beam.RX.match(line)) if m]
            raw = [line.split()[1] for line in open(p) if beam.RX.match(line)]
            for k in range(len(t) - 1):
                pc = int(t[k][0].replace(':', ''), 16)
                cost = beam.nonrefresh(t[k], t[k + 1])
                self.dist[pc][cost] += 1
                op = int(t[k][1], 16)
                self.op[pc] = op
                dreg, breg, fl = regs[k]
                m, x = (1 if fl[2] == 'M' else 0), (1 if fl[3] == 'X' else 0)
                mnem, mode = D.TABLE[op]
                ln = 1 + D.oplen(mode, m, x)
                prev = self.len.get(pc)
                if prev is not None and prev != ln:
                    self.len[pc] = -1                     # executed with two widths: `lin` refuses it
                elif prev is None:
                    self.len[pc] = ln
                b = bytes.fromhex(raw[k][:2 * ln]) if len(raw[k]) >= 2 * ln else None
                if b is not None and op not in self.BRANCHES and op not in (0x54, 0x44):
                    operand = int.from_bytes(b[1:ln], 'little') if ln > 1 else 0
                    kk = key_of(op, m, x, int(dreg, 16), int(breg, 16), operand)
                    if kk is not None:
                        self.keycost[kk][cost] += 1

    def costs(self, pc):
        return sorted(self.dist[pc])

    def base(self, pc):
        c = self.costs(pc)
        if not c:
            raise KeyError('%06X never executed in the traces' % pc)
        if len(c) > 1:
            raise ValueError('%06X has %s: data-dependent, price it explicitly' % (pc, c))
        return c[0]

    # A conditional branch in a FastROM bank: the opcode and its operand (6 + 6), plus one idle (6) when taken --
    # LakeSnes charges no page-crossing penalty in native mode. Checked against every trace that ran it.
    def _branch(self, pc):
        c = self.costs(pc)
        if c and not set(c) <= {12, 18}:
            raise ValueError('%06X branch costs %s' % (pc, c))

    def nt(self, pc):
        self._branch(pc)
        return 12

    def tk(self, pc):
        self._branch(pc)
        return 18

    def lin(self, a, b, over=None):
        """Straight-line run [a, b): every instruction in it, a branch counted not taken. `over(pc, op)` may price an
        instruction itself (a data-dependent access) by returning its cost, or None to take the base."""
        tot, pc = 0, a
        while pc < b:
            if pc not in self.op:
                raise KeyError('%06X (in the run %06X..%06X) never executed in the traces' % (pc, a, b))
            op = self.op[pc]
            v = over(pc, op) if over else None
            if op in (0x54, 0x44) and v is None:
                raise ValueError('%06X is a block move: price it per byte' % pc)
            ln = self.len[pc]
            if ln < 0:
                raise ValueError('%06X ran with two widths' % pc)
            tot += v if v is not None else (self.nt(pc) if op in self.BRANCHES else self.base(pc))
            pc += ln
        if pc != b:
            raise ValueError('run %06X..%06X overshoots to %06X' % (a, b, pc))
        return tot

    def equiv(self, op, m, x, d, db, operand):
        kk = key_of(op, m, x, d, db, operand)
        if kk is None:
            raise ValueError('opcode %02X: an indirect access, no key' % op)
        cs = self.keycost.get(kk)
        if not cs:
            mnem, mode = D.TABLE[op]
            if mode in ('immM', 'immX', 'imm8') and mnem in ('ORA', 'AND', 'EOR', 'ADC', 'SBC', 'CMP', 'CPX', 'CPY',
                                                             'LDA', 'LDX', 'LDY', 'BIT'):
                # an immediate ALU op: its fetches only, 6 each from FastROM (LakeSnes cpu_readOpcode per byte)
                return 6 * (1 + D.oplen(mode, m, x))
            # the read-ALU group shares one addressing path per mode (LakeSnes cpu.c: cpu_adr* then one read):
            # an executed member with the same mode, widths and region costs the same
            grp = ('LDA', 'ORA', 'AND', 'EOR', 'ADC', 'SBC', 'CMP')
            if mnem in grp:
                for op2 in range(256):
                    mn2, md2 = D.TABLE[op2]
                    if mn2 in grp and md2 == mode and op2 != op:
                        c2 = self.keycost.get((op2,) + tuple(kk[1:]))
                        if c2:
                            return min(c2)
            raise KeyError('no executed instruction with key %s' % (kk,))
        vals = sorted(cs)
        if len(vals) > 1:
            # a refresh straddle adds 40 to one sample; anything else is a real spread
            if len(vals) == 2 and vals[1] - vals[0] == 40:
                return vals[0]
            # the instruction after a DMA trigger carries the transfer (RTS / LDY / INX after `STA $420B`): its
            # base is the common, smallest value
            if cs.most_common(1)[0][0] == vals[0]:
                return vals[0]
            raise ValueError('key %s costs %s' % (kk, vals))
        return vals[0]

    def lin_rom(self, a, b, m, x, d, db, over=None):
        """Straight-line [a, b) decoded from the ROM with widths m/x (REP/SEP followed), D and DB as given; executed
        PCs take their own base, the rest their key's."""
        tot, pc = 0, a
        while pc < b:
            op = ROM[pc & 0x3FFFFF]
            mnem, mode = D.TABLE[op]
            ln = 1 + D.oplen(mode, m, x)
            operand = int.from_bytes(ROM[(pc & 0x3FFFFF) + 1:(pc & 0x3FFFFF) + ln], 'little') if ln > 1 else 0
            v = over(pc, op) if over else None
            if v is None:
                if op in self.BRANCHES:
                    v = 12
                elif pc in self.op and self.len.get(pc) == ln and len(self.costs(pc)) == 1:
                    v = self.base(pc)
                else:
                    v = self.equiv(op, m, x, d, db, operand)
            tot += v
            if op == 0xC2:
                m = 0 if operand & 0x20 else m
                x = 0 if operand & 0x10 else x
            elif op == 0xE2:
                m = 1 if operand & 0x20 else m
                x = 1 if operand & 0x10 else x
            pc += ln
        if pc != b:
            raise ValueError('run %06X..%06X overshoots to %06X' % (a, b, pc))
        return tot
