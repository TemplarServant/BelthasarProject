"""One path through ROM code, priced instruction by instruction off the beam traces' costs -- how the block constants of a
hand-written cost model (src/field_cost.c) are derived.

    P = PathCost(costtab)                     # a ctlib.costtab.CostTab over the traces
    P.cost(pc, decide, m=1, x=0, d=0x0100, db=0x7E, stop=None, mvn=None, ind=None, skip=())

The walk decodes the ROM from `pc`, following REP / SEP for the widths, and charges each instruction its cost: an
executed PC's own (single) cost, else an executed instruction's with the same key (CostTab.equiv: opcode, widths, the
region its operand lands in), else (an indirect access) the single cost every executed instruction with that opcode and
widths had (they read the same FastROM tables). A conditional branch is 12 not taken / 18 taken, and `decide(pc, n)`
says which (n = how many times this branch was met on the path so far). JSR / JSL are followed, RTS / RTL return; the
walk ends at the routine's own RTS / RTL or at `stop`. `skip` = callees charged their JSR only (the model prices them
itself); a JSL into C7 (the sound driver, priced by the loader clock) is charged nothing, one into C3 (the
decompressor, priced by lz.c) its vector's JMP (18). `mvn(pc)` = the bytes a block move moves (46 a byte WRAM to WRAM,
44 from ROM, or the traces' own cost where they ran), `ind(pc)` the target of a JSR / JMP (abs,X).
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import disasm65816 as D  # noqa: E402

BR = {0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0}
ROM = open(os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                        'Chrono_Trigger_USA_SNES.sfc'), 'rb').read()


def rb(a):
    return ROM[a & 0x3FFFFF]


class PathCost:
    def __init__(self, T):
        self.T = T
        self.notes = []
        if not hasattr(T, 'mx'):
            T.mx = {}

    def one(self, pc, op, m, x, d, db, operand, ln):
        T = self.T
        if pc in T.op and T.len.get(pc) == ln:
            c = T.costs(pc)
            if len(c) == 1:
                return c[0]
            if T.dist[pc].most_common(1)[0][0] == min(c):
                return min(c)
        try:
            return T.equiv(op, m, x, d, db, operand)
        except ValueError:
            refs = collections.Counter(T.costs(p)[0] for p, o in T.op.items()
                                       if o == op and len(T.costs(p)) == 1 and T.mx.get(p) == (m, x))
            if len(refs) == 1:
                self.notes.append('%06X indirect ~ %d' % (pc, list(refs)[0]))
                return list(refs)[0]
            raise ValueError('%06X: indirect opcode %02X, executed costs %s' % (pc, op, dict(refs)))

    def cost(self, pc, decide, m=1, x=0, d=0x0100, db=0x7E, stop=None, mvn=None, ind=None, skip=(), trace=None):
        tot = 0; seen = collections.Counter(); rets = []; stk = []; a_imm = None
        for _ in range(100000):
            if stop is not None and pc == stop:
                return tot
            op = rb(pc); mnem, mode = D.TABLE[op]
            ln = 1 + D.oplen(mode, m, x)
            operand = int.from_bytes(ROM[(pc & 0x3FFFFF) + 1:(pc & 0x3FFFFF) + ln], 'little') if ln > 1 else 0
            nxt = pc + ln
            if op in (0x54, 0x44):
                c = (self.T.costs(pc)[0] if pc in self.T.op else (44 if (operand >> 8) >= 0xC0 else 46)) * mvn(pc)   # `MVN dst,src`: src the 2nd byte
                tot += c; pc = nxt
                if trace is not None: trace.append((pc, c))
                continue
            if op in BR:
                t = decide(pc, seen[pc]); seen[pc] += 1
                tot += 18 if t else 12
                if trace is not None: trace.append((pc, 18 if t else 12))
                pc = ((pc & 0xFF0000) | ((nxt + (operand - 256 if operand > 127 else operand)) & 0xFFFF)) if t else nxt
                continue
            c = self.one(pc, op, m, x, d, db, operand, ln); tot += c
            if trace is not None: trace.append((pc, c))
            if op == 0xC2: m = 0 if operand & 0x20 else m; x = 0 if operand & 0x10 else x
            elif op == 0xE2: m = 1 if operand & 0x20 else m; x = 1 if operand & 0x10 else x
            if mnem == 'LDA' and mode == 'immM': a_imm = operand
            elif mnem in ('LDA', 'TDC', 'PLA', 'TXA', 'TYA'): a_imm = None
            if mnem == 'PHA': stk.append(a_imm)
            elif mnem == 'PEA': stk += [operand >> 8, operand & 0xFF]
            elif mnem == 'PHB': stk.append(db)
            elif mnem == 'PHK': stk.append(pc >> 16)
            elif mnem == 'PHD': stk += [d >> 8, d & 0xFF]
            elif mnem == 'PLB':
                v = stk.pop() if stk else None
                db = v if v is not None else db
            elif mnem == 'PLD':
                lo = stk.pop() if stk else None; hi = stk.pop() if stk else None
                if lo is not None and hi is not None: d = lo | hi << 8
            elif mnem == 'PLA' and stk: stk.pop()
            elif mnem == 'TCD' and a_imm is not None: d = a_imm
            if mnem in ('RTS', 'RTL'):
                if not rets: return tot
                pc = rets.pop(); continue
            if mnem in ('JSR', 'JSL'):
                tgt = operand if mnem == 'JSL' else ((pc & 0xFF0000) | operand)
                if mnem == 'JSR' and mode != 'abs': tgt = (pc & 0xFF0000) | ind(pc)
                if tgt in skip or (mnem == 'JSL' and operand >> 16 == 0xC7):
                    pc = nxt; continue
                if mnem == 'JSL' and operand >> 16 == 0xC3:
                    tot += 18; pc = nxt; continue
                rets.append(nxt); pc = tgt; continue
            if mnem == 'BRA': pc = (pc & 0xFF0000) | ((nxt + (operand - 256 if operand > 127 else operand)) & 0xFFFF); continue
            if mnem == 'BRL': pc = (pc & 0xFF0000) | ((nxt + (operand - 65536 if operand > 32767 else operand)) & 0xFFFF); continue
            if mnem == 'JMP': pc = (pc & 0xFF0000) | (operand if mode == 'abs' else ind(pc)); continue
            if mnem == 'JML': pc = operand; continue
            pc = nxt
        raise RuntimeError('path too long')


def decided(table):
    """a decide() from {pc & FFFF: bool or [bool per visit]}"""
    def f(p, n):
        v = table[p & 0xFFFF]
        return v[n] if isinstance(v, (list, tuple)) else v
    return f
