#!/usr/bin/env python3
"""Trace-driven 65816 disassembler for the Chrono Trigger C port.

Consumes an execution trace produced by `ct.exe --trace N` (one line per
executed opcode: "KK PPPP M X E", i.e. program bank, PC, M flag, X flag, emu
flag) plus the ROM, and emits an annotated assembly listing of the executed
path. The trace carries the M/X flags of each executed instruction, so the
immediate operand widths need no REP/SEP tracking.

Usage:
  python tools/disasm65816.py <trace.txt> [rom.sfc] > out.asm
"""
import sys, re

# opcode -> (mnemonic, addressing-mode). Full 65816 matrix.
_T = (
 "BRK:imm8 ORA:idx COP:imm8 ORA:sr TSB:dp ORA:dp ASL:dp ORA:idl "
 "PHP:imp ORA:immM ASL:acc PHD:imp TSB:abs ORA:abs ASL:abs ORA:abl "
 "BPL:rel ORA:idy ORA:idp ORA:isy TRB:dp ORA:dpx ASL:dpx ORA:idly "
 "CLC:imp ORA:aby INC:acc TCS:imp TRB:abs ORA:abx ASL:abx ORA:alx "
 "JSR:abs AND:idx JSL:abl AND:sr BIT:dp AND:dp ROL:dp AND:idl "
 "PLP:imp AND:immM ROL:acc PLD:imp BIT:abs AND:abs ROL:abs AND:abl "
 "BMI:rel AND:idy AND:idp AND:isy BIT:dpx AND:dpx ROL:dpx AND:idly "
 "SEC:imp AND:aby DEC:acc TSC:imp BIT:abx AND:abx ROL:abx AND:alx "
 "RTI:imp EOR:idx WDM:imm8 EOR:sr MVP:bm EOR:dp LSR:dp EOR:idl "
 "PHA:imp EOR:immM LSR:acc PHK:imp JMP:abs EOR:abs LSR:abs EOR:abl "
 "BVC:rel EOR:idy EOR:idp EOR:isy MVN:bm EOR:dpx LSR:dpx EOR:idly "
 "CLI:imp EOR:aby PHY:imp TCD:imp JML:abl EOR:abx LSR:abx EOR:alx "
 "RTS:imp ADC:idx PER:rell ADC:sr STZ:dp ADC:dp ROR:dp ADC:idl "
 "PLA:imp ADC:immM ROR:acc RTL:imp JMP:ind ADC:abs ROR:abs ADC:abl "
 "BVS:rel ADC:idy ADC:idp ADC:isy STZ:dpx ADC:dpx ROR:dpx ADC:idly "
 "SEI:imp ADC:aby PLY:imp TDC:imp JMP:iax ADC:abx ROR:abx ADC:alx "
 "BRA:rel STA:idx BRL:rell STA:sr STY:dp STA:dp STX:dp STA:idl "
 "DEY:imp BIT:immM TXA:imp PHB:imp STY:abs STA:abs STX:abs STA:abl "
 "BCC:rel STA:idy STA:idp STA:isy STY:dpx STA:dpx STX:dpy STA:idly "
 "TYA:imp STA:aby TXS:imp TXY:imp STZ:abs STA:abx STZ:abx STA:alx "
 "LDY:immX LDA:idx LDX:immX LDA:sr LDY:dp LDA:dp LDX:dp LDA:idl "
 "TAY:imp LDA:immM TAX:imp PLB:imp LDY:abs LDA:abs LDX:abs LDA:abl "
 "BCS:rel LDA:idy LDA:idp LDA:isy LDY:dpx LDA:dpx LDX:dpy LDA:idly "
 "CLV:imp LDA:aby TSX:imp TYX:imp LDY:abx LDA:abx LDX:aby LDA:alx "
 "CPY:immX CMP:idx REP:imm8 CMP:sr CPY:dp CMP:dp DEC:dp CMP:idl "
 "INY:imp CMP:immM DEX:imp WAI:imp CPY:abs CMP:abs DEC:abs CMP:abl "
 "BNE:rel CMP:idy CMP:idp CMP:isy PEI:dp CMP:dpx DEC:dpx CMP:idly "
 "CLD:imp CMP:aby PHX:imp STP:imp JML:ial CMP:abx DEC:abx CMP:alx "
 "CPX:immX SBC:idx SEP:imm8 SBC:sr CPX:dp SBC:dp INC:dp SBC:idl "
 "INX:imp SBC:immM NOP:imp XBA:imp CPX:abs SBC:abs INC:abs SBC:abl "
 "BEQ:rel SBC:idy SBC:idp SBC:isy PEA:abs SBC:dpx INC:dpx SBC:idly "
 "SED:imp SBC:aby PLX:imp XCE:imp JSR:iax SBC:abx INC:abx SBC:alx "
).split()
TABLE = [(e.split(':')[0], e.split(':')[1]) for e in _T]
assert len(TABLE) == 256, len(TABLE)

# fixed operand lengths by mode (immM/immX resolved per-instruction via M/X)
FIXLEN = {'imp':0,'acc':0,'imm8':1,'dp':1,'dpx':1,'dpy':1,'idp':1,'idx':1,
          'idy':1,'idl':1,'idly':1,'sr':1,'isy':1,'rel':1,'abs':2,'abx':2,
          'aby':2,'ind':2,'iax':2,'ial':2,'rell':2,'bm':2,'abl':3,'alx':3}

def oplen(mode, m, x):
    if mode == 'immM': return 1 if m else 2
    if mode == 'immX': return 1 if x else 2
    return FIXLEN[mode]

# SNES hardware register names (for readability + porting).
REGS = {
 0x2100:'INIDISP',0x2101:'OBSEL',0x2102:'OAMADDL',0x2103:'OAMADDH',0x2104:'OAMDATA',
 0x2105:'BGMODE',0x2106:'MOSAIC',0x2107:'BG1SC',0x2108:'BG2SC',0x2109:'BG3SC',0x210A:'BG4SC',
 0x210B:'BG12NBA',0x210C:'BG34NBA',0x210D:'BG1HOFS',0x210E:'BG1VOFS',0x210F:'BG2HOFS',0x2110:'BG2VOFS',
 0x2111:'BG3HOFS',0x2112:'BG3VOFS',0x2113:'BG4HOFS',0x2114:'BG4VOFS',0x2115:'VMAIN',0x2116:'VMADDL',
 0x2117:'VMADDH',0x2118:'VMDATAL',0x2119:'VMDATAH',0x211A:'M7SEL',0x211B:'M7A',0x211C:'M7B',0x211D:'M7C',
 0x211E:'M7D',0x211F:'M7X',0x2120:'M7Y',0x2121:'CGADD',0x2122:'CGDATA',0x2123:'W12SEL',0x2124:'W34SEL',
 0x2125:'WOBJSEL',0x2126:'WH0',0x2127:'WH1',0x2128:'WH2',0x2129:'WH3',0x212A:'WBGLOG',0x212B:'WOBJLOG',
 0x212C:'TM',0x212D:'TS',0x212E:'TMW',0x212F:'TSW',0x2130:'CGWSEL',0x2131:'CGADSUB',0x2132:'COLDATA',
 0x2133:'SETINI',0x2134:'MPYL',0x2135:'MPYM',0x2136:'MPYH',0x2137:'SLHV',0x2138:'OAMDATAREAD',
 0x2139:'VMDATALREAD',0x213A:'VMDATAHREAD',0x213B:'CGDATAREAD',0x213C:'OPHCT',0x213D:'OPVCT',
 0x213E:'STAT77',0x213F:'STAT78',0x2140:'APUIO0',0x2141:'APUIO1',0x2142:'APUIO2',0x2143:'APUIO3',
 0x2180:'WMDATA',0x2181:'WMADDL',0x2182:'WMADDM',0x2183:'WMADDH',
 0x4200:'NMITIMEN',0x4201:'WRIO',0x4202:'WRMPYA',0x4203:'WRMPYB',0x4204:'WRDIVL',0x4205:'WRDIVH',
 0x4206:'WRDIVB',0x4207:'HTIMEL',0x4208:'HTIMEH',0x4209:'VTIMEL',0x420A:'VTIMEH',0x420B:'MDMAEN',
 0x420C:'HDMAEN',0x420D:'MEMSEL',0x4210:'RDNMI',0x4211:'TIMEUP',0x4212:'HVBJOY',0x4213:'RDIO',
 0x4214:'RDDIVL',0x4215:'RDDIVH',0x4216:'RDMPYL',0x4217:'RDMPYH',0x4218:'JOY1L',0x4219:'JOY1H',
 0x421A:'JOY2L',0x421B:'JOY2H',0x421C:'JOY3L',0x421D:'JOY3H',0x421E:'JOY4L',0x421F:'JOY4H',
}
def regname(eff):
    eff &= 0xffff
    if eff in REGS: return REGS[eff]
    if 0x4300 <= eff <= 0x437f:
        ch = (eff >> 4) & 7
        nm = {0:'DMAP',1:'BBAD',2:'A1TL',3:'A1TH',4:'A1B',5:'DASL',6:'DASH',
              7:'DASB',8:'A2AL',9:'A2AH',0xa:'NTRL'}.get(eff & 0xf)
        return '%s%d' % (nm, ch) if nm else None
    return None

def hirom_off(addr, romsize):
    """Map a 24-bit CPU address to a ROM file offset (HiROM), or None if not ROM."""
    bank = (addr >> 16) & 0xff
    a = addr & 0xffff
    b = bank & 0x7f
    if a >= 0x8000 or b >= 0x40:
        return (((b & 0x3f) << 16) | a) & (romsize - 1)
    return None

def fmt(mnem, mode, k, pc, ob, m, x, dp):
    """Return (text, target, reg): disassembled text, resolved jump/branch dest
    (or None), and a hardware register name for direct register access (or None)."""
    val = 0
    for i, by in enumerate(ob):
        val |= by << (8 * i)
    n = len(ob)
    t = None
    if mode in ('immM', 'immX', 'imm8'):
        o = ('#$%02X' % val) if n == 1 else ('#$%04X' % val)
    elif mode == 'imp':  o = ''
    elif mode == 'acc':  o = 'A'
    elif mode == 'dp':   o = '$%02X' % val
    elif mode == 'dpx':  o = '$%02X,X' % val
    elif mode == 'dpy':  o = '$%02X,Y' % val
    elif mode == 'idp':  o = '($%02X)' % val
    elif mode == 'idx':  o = '($%02X,X)' % val
    elif mode == 'idy':  o = '($%02X),Y' % val
    elif mode == 'idl':  o = '[$%02X]' % val
    elif mode == 'idly': o = '[$%02X],Y' % val
    elif mode == 'sr':   o = '$%02X,S' % val
    elif mode == 'isy':  o = '($%02X,S),Y' % val
    elif mode == 'abs':  o = '$%04X' % val
    elif mode == 'abx':  o = '$%04X,X' % val
    elif mode == 'aby':  o = '$%04X,Y' % val
    elif mode == 'abl':  o = '$%06X' % val
    elif mode == 'alx':  o = '$%06X,X' % val
    elif mode == 'ind':  o = '($%04X)' % val
    elif mode == 'iax':  o = '($%04X,X)' % val
    elif mode == 'ial':  o = '[$%04X]' % val
    elif mode == 'bm':   o = '$%02X,$%02X' % (ob[0], ob[1])  # dst,src banks
    elif mode == 'rel':
        rel = val if val < 0x80 else val - 0x100
        t = (k << 16) | ((pc + 2 + rel) & 0xffff)
        o = '$%04X' % (t & 0xffff)
    elif mode == 'rell':
        rel = val if val < 0x8000 else val - 0x10000
        t = (k << 16) | ((pc + 3 + rel) & 0xffff)
        o = '$%04X' % (t & 0xffff)
    else:
        o = '?'
    # resolve absolute control-flow targets for a comment
    if mnem in ('JSR', 'JMP') and mode == 'abs': t = (k << 16) | val
    elif mnem in ('JSL', 'JML') and mode == 'abl': t = val
    # name direct hardware-register accesses
    reg = None
    if mode in ('dp', 'dpx', 'dpy'):     reg = regname((dp + val) & 0xffff)
    elif mode in ('abs', 'abx', 'aby'):  reg = regname(val)
    text = mnem + (' ' + o if o else '')
    return text, t, reg

def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    trace_path = sys.argv[1]
    rom_path = sys.argv[2] if len(sys.argv) > 2 else 'Chrono_Trigger_USA_SNES.sfc'
    import os as _ctos  # ctlib.rom imports this module: the import has to wait for main()
    sys.path.append(_ctos.path.dirname(_ctos.path.abspath(__file__)))
    from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
    rom = _romlib.load(rom_path)
    romsize = len(rom)

    line_re = re.compile(
        r'^([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{4}) ([0-9A-Fa-f]{4}) ([01]) ([01]) ([01])\s*$')
    order = []         # first-seen (k,pc) in execution order
    seen = {}          # (k,pc) -> (m,x,dp)
    count = {}         # (k,pc) -> exec count
    for ln in open(trace_path, 'r', errors='ignore'):
        mo = line_re.match(ln)
        if not mo: continue
        k = int(mo.group(1), 16); pc = int(mo.group(2), 16); dp = int(mo.group(3), 16)
        m = int(mo.group(4)); x = int(mo.group(5))
        key = (k, pc)
        count[key] = count.get(key, 0) + 1
        if key not in seen:
            seen[key] = (m, x, dp)
            order.append(key)

    # disassemble each unique address; collect branch/call targets for labels
    rows = []
    targets = set()
    for (k, pc) in order:
        m, x, dp = seen[(k, pc)]
        off = hirom_off((k << 16) | pc, romsize)
        if off is None:
            rows.append((k, pc, '??', 'db (non-ROM / RAM code)', None, None, count[(k, pc)]))
            continue
        op = rom[off]
        mnem, mode = TABLE[op]
        ln_ = oplen(mode, m, x)
        ob = list(rom[off + 1: off + 1 + ln_])
        text, t, reg = fmt(mnem, mode, k, pc, ob, m, x, dp)
        raw = ' '.join('%02X' % b for b in rom[off:off + 1 + ln_])
        if t is not None: targets.add(t)
        rows.append((k, pc, raw, text, t, reg, count[(k, pc)]))

    labels = {t: 'L_%02X_%04X' % ((t >> 16) & 0xff, t & 0xffff) for t in targets}

    print('; Chrono Trigger (USA) - boot/init disassembly (trace-driven)')
    print('; %d unique instructions executed during boot.' % len(order))
    print('; Columns: BANK:PC  raw-bytes        MNEMONIC            ; notes')
    print()
    prev = None
    for (k, pc, raw, text, t, reg, c) in rows:
        addr = (k << 16) | pc
        if prev is not None and addr != prev:
            print('             ...')  # discontinuity (jump/branch landing)
        lbl = labels.get(addr)
        if lbl: print('%s:' % lbl)
        cmt = ''
        if reg is not None:
            cmt = '  ; %s' % reg
        if t is not None and t in labels and labels[t] != lbl:
            cmt += '%s-> %02X:%04X' % ('  ; ' if not cmt else ' ', (t >> 16) & 0xff, t & 0xffff)
        if c > 1: cmt += '   (x%d)' % c
        print('%02X:%04X  %-14s  %-20s%s' % (k, pc, raw, text, cmt))
        prev = (addr + len(raw.split())) if raw != '??' else None

if __name__ == '__main__':
    main()
