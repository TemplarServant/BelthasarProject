#!/usr/bin/env python3
r"""The battle's STATUS machinery, port against the rule's cartridge, from the same WRAM and the same ROM.

    python tools/battle_status_cmp.py lifeline      # Crono's lifeline revive inside the dead-flag sweep C1/B223
    python tools/battle_status_cmp.py sleeptail     # the ATB handler's tail after GATO's action (a sleeper's $B03A)
    python tools/battle_status_cmp.py chaosenemy    # a CONFUSED GATO: no counter, the swapped script, his turns
    python tools/battle_status_cmp.py lockdrop      # a LOCKED Crono's queued tech dropped at its dispatch (C1/D74F)
    python tools/battle_status_cmp.py blindgate     # a BLIND GATO does not counter (C1/86FF, +1E & 8F)
    python tools/battle_status_cmp.py all

GATO's fight (saves/battle_re/gato_wait.state, the field before the A that starts it) on both sides: the cartridge is
build/ct.exe under the rule (CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1) with PEEK_POKE and the scenario's ROM
patches applied in memory (CT_ROM_MEMPATCH, src/rom_verify.h) to the verified dump; the port is build/Belthasar.exe --scene 6 --loadwram <the same WRAM> --walkscript a:8 with CT_ROM
and CT_BATTLE_MEMMON (battle frames; the cartridge's frame = the port's + 227 on this fixture, disasm/notes/
battle_status_pipeline.md S9.3). The ROM patches (disasm/notes/battle_status_pipeline.md S9.5), applied in memory on both sides:
  sleep02  GATO's attack headers CC/89C6[0..1]: +1 = 63 (accuracy), +2 = 02 (SLEEP), +7 |= 04 (the rider)
  lock08   the same with +2 = 08 (LOCK)
  chaosE   Crono's physical header CC/23F7: +1 = 63, +2 = 04 (CHAOS), +7 |= 04; GATO's +23 immunity CC/5422 = 00
  blindE   the same with +2 = 01 (BLIND)
Prints one line per scenario: the frames compared and how many differ per field, and the scenario's own events.
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# One directory PER RUN: battle-status-machinery-vs-logical-oracle and -under-the-rule- run this same tool in parallel
# in the suite, and a shared directory let one run rewrite the .lakewram the other was reading (the
# post-flip full suite: blindgate read 0 frames on one side). docs/TESTING.md: a file shared by two parallel checks
# lives in each check's own directory. Removed at the end of the run.
OUT = os.path.join(ROOT, 'build', '_status_cmp', 'run%d' % os.getpid())
STATE = 'saves/battle_re/gato_wait.state'
OFF = 227                                           # cartridge frame - port battle frame on gato_wait
# ...under the rule (CT_ORACLE_NMION_HOLD=1, both sides): 228 -- the cartridge's boot holds C1/0000's setup, the port's is
# `2 + apu + 3 + 1` (battle.c battleBootBandArm), so the fight's frames meet one later (disasm/notes/battle_menu_pass.md 12).
if os.environ.get('CT_ORACLE_NMION_HOLD', '1') != '0': OFF = 228
# ...and under the infinitely fast CPU (CT_ORACLE_NMION_HOLD=1 + CT_ORACLE_IFAST=1, the rule's model, CLAUDE.md): the
# cartridge's fight comes 17 frames sooner than the valve's (C1/0000 at f222 in every model; the band seed `$CA1E = FF`
# f279 against f296, the first iteration head f355 against f372 -- the boot's setup and its upload's CPU side held) and
# the port's 11 frames sooner (its arm CT_BHUD_ARM_FRAME - 11, src/battle.c battleBootBandArm; a --selftest --loadwram
# run brings no SPC up, so the scene-6 boot takes the fixture's arm), so the offset is 221 -- the arm 58 against the seed
# f279, as tools/gato_logical_cmp.py measures on the fixture, identical on all 1564 frames -- and every frame below moves
# with its side: the cartridge's presses and windows by CS, the port's presses by PS. The same fight at the same moments.
CS, PS = 0, 0
if os.environ.get('CT_ORACLE_NMION_HOLD', '1') != '0' and os.environ.get('CT_ORACLE_IFAST', '1') != '0':
    CS, PS = -17, -11
    OFF = 227 + CS - PS
RULE = {'CT_ORACLE_LOGICAL': '1', 'CT_ORACLE_VIRTUAL_BEAM': '1'}

PATCHES = {
    'sleep02': [(0x0C89C7, b'\x63\x02'), (0x0C89CD, None), (0x0C89D3, b'\x63\x02'), (0x0C89D9, None)],
    'lock08': [(0x0C89C7, b'\x63\x08'), (0x0C89CD, None), (0x0C89D3, b'\x63\x08'), (0x0C89D9, None)],
    'chaosE': [(0x0C23F8, b'\x63\x04'), (0x0C23FE, None), (0x0C5422, b'\x00')],
    'blindE': [(0x0C23F8, b'\x63\x01'), (0x0C23FE, None), (0x0C5422, b'\x00')],
}


ROM = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')


def mempatch(tag):
    """The scenario's ROM patches as CT_ROM_MEMPATCH (src/rom_verify.h): both programs load the user's verified dump
    and apply them in memory after the check -- no altered ROM file is ever written."""
    if not tag:
        return {}
    parts = []
    for a, v in PATCHES[tag]:
        if v is None:
            parts.append('%06X|=04' % a)            # header +7 |= 04: $AE4D bit 2, the physical status rider
        else:
            parts.append('%06X=%s' % (a, v.hex().upper()))
    return {'CT_ROM_MEMPATCH': ','.join(parts)}


def regions(spec):
    out = []
    for r in spec.split(','):
        a, n = r.split(':')
        out.append((int(a, 16), int(n)))
    return out


def run_oracle(tag, pokes, regs, inp):
    env = dict(os.environ, **RULE, **mempatch(tag))
    if pokes:
        env['PEEK_POKE'] = pokes
    mon = ','.join('7E%04X:%d' % (a, n) for a, n in regs)
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), ROM, '--monitor', mon, '--from', STATE,
                        '--input', inp, '--diff'], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    return parse(r.stdout, r'\s*f(\d+)\s+(.*)')


def run_port(tag, pokes, regs, script, frames):
    w = bytearray(open(os.path.join(ROOT, STATE), 'rb').read()[0x45:0x45 + 0x20000])
    for kv in filter(None, (pokes or '').split(',')):
        a, v = kv.split('=')
        w[int(a, 16)] = int(v, 16)
    os.makedirs(OUT, exist_ok=True)
    lw = os.path.join(OUT, (tag or 'plain') + '.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + bytes(w))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_BATTLE_MEMMON=','.join('%04X:%X' % (a, n) for a, n in regs), CT_ROM=ROM, **mempatch(tag))
    if script:
        env['CT_BATTLE_SCRIPT_ABS'] = '1'
        env['CT_BATTLE_SCRIPT'] = script
    r = subprocess.run([os.environ.get('CT_STATUS_CMP_PORT') or os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '6',
                        '--loadwram', lw, '--walkscript', 'a:8', '--selftest', '--frames', str(frames)],
                       cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    return parse(r.stdout + '\n' + r.stderr, r'memmon f(\d+)\S*(?: rel=-?\d+)?\s+(.*)')


def parse(text, pat):
    """{frame: {addr: byte}} of every row, values carried forward by the caller."""
    rows = []
    for l in text.splitlines():
        m = re.match(pat, l)
        if not m:
            continue
        mem = {}
        for a, v in re.findall(r'\b(?:7E)?([0-9A-F]{4})=([0-9A-F]+)', m.group(2)):
            b = int(a, 16)
            for i in range(len(v) // 2):
                mem[b + i] = int(v[2 * i:2 * i + 2], 16)
        rows.append((int(m.group(1)), mem))
    return rows


def state_at(rows):
    """frame -> the full carried-forward map (a list of (frame, dict) with every frame filled)."""
    out, cur, last = {}, {}, None
    for f, mem in rows:
        if last is not None:
            for k in range(last + 1, f):
                out[k] = out[last]
        cur = dict(cur)
        cur.update(mem)
        out[f] = cur
        last = f
    return out, last


def value(st, a, n):
    return ''.join('%02X' % st.get(a + i, 0) for i in range(n))


def compare(o, p, fields, lo, hi):
    """Frame-for-frame at the fixed offset: per field, the frames that differ in [lo, hi] (cartridge frames)."""
    os_, olast = state_at(o)
    ps_, plast = state_at(p)
    per = {name: 0 for name, _, _ in fields}
    first = None
    n = 0
    for f in range(lo, hi + 1):
        # a --monitor / MEMMON row is printed only when something changed: past a side's last row its state stands (the
        # runs are sized to reach hi on both sides)
        om = os_.get(f, os_[olast] if olast is not None and f > olast else None)
        pm = ps_.get(f - OFF, ps_[plast] if plast is not None and f - OFF > plast else None)
        if om is None or pm is None:
            continue
        n += 1
        for name, a, ln in fields:
            if value(om, a, ln) != value(pm, a, ln):
                per[name] += 1
                if first is None:
                    first = (f, name, value(om, a, ln), value(pm, a, ln))
    return n, per, first


def events(rows, a, ln, lo=0, off=0):
    """The frames a field changes on (from lo), as 'f:value'."""
    st, last = state_at(rows)
    out, prev = [], None
    for f in sorted(st):
        if f < lo:
            continue
        v = value(st[f], a, ln)
        if v != prev:
            if prev is not None:
                out.append('%d:%s' % (f + off, v))
            prev = v
    return out


def fields_of(spec):
    out = []
    for s in spec.split(','):
        name, r = s.split('=')
        a, n = r.split(':')
        out.append((name, int(a, 16), int(n)))
    return out


def report(label, n, per, first, extra=''):
    bad = sum(per.values())
    detail = ', '.join('%s %d' % kv for kv in per.items() if kv[1]) or 'none'
    print('status-cmp %s: %d frames, %d differing frame-fields (%s)%s%s' % (
        label, n, bad, detail, '' if first is None else '; first c%d %s cartridge %s port %s' % first, extra))
    return bad


def lifeline():
    pokes = '2622=80,2603=05,2604=00'
    regs = regions('5E2D:128,A013:1,AE93:1,0200:1,020C:4,CCEC:2,B03A:3,AFAB:3,AEFF:11,0024:1,B2D3:1,AD89:2,B328:4,'
                   'AD9C:4,CA24:1,0027:1,A6D9:6,95D5:1,95D6:5,93EE:1')
    o = run_oracle(None, pokes, regs, '100:8,0:1150')
    p = run_port(None, pokes, regs, '', 920)
    f = fields_of('d0=5E4A:1,ll=5E4F:1,hp=5E30:2,frz=A013:1,act=AE93:1,who=0200:1,msg=020C:4,box=CCEC:1,flag=B03A:3,'
                  'atb=AFAB:3,ros=AEFF:11,all=0024:1,cur=B2D3:1,slot=B328:4,rec=AD9C:4,it=0027:1,q=A6D9:3,qcur=A6DD:2,chooser=95D5:1,'
                  'pend=95D6:5,rec0=93EE:1')
    # The window ENDS at c1140 in every model: the revived Crono's enqueue ($93EE = 81, $95D6 = 00) and his box at the next
    # pass ($A6D9) are what it grades last, and under the infinitely fast CPU they come 7 frames sooner, not 17 (c1124 /
    # c1128 against plain A's c1131 / c1135) -- an end at 1140 + CS (c1123) cut both off, and the mutations of C1/8562 and of
    # the close's single CC/F06B wipe survived (the post-flip mutation sweep, 2026-10-01).
    LO, HI = 605 + CS, 1140
    n, per, first = compare(o, p, f, LO, HI)
    ev = ' | cartridge: $5E4F %s, $A013 %s, $020C %s, $CCEC %s, $5E4A %s' % (
        ' '.join(events(o, 0x5E4F, 1, 600 + CS)), ' '.join(events(o, 0xA013, 1, 600 + CS)[:4]),
        ' '.join(events(o, 0x020C, 1, 600 + CS)), ' '.join(events(o, 0xCCEC, 1, 600 + CS)),
        ' '.join(events(o, 0x5E4A, 1, 600 + CS)))
    return report('lifeline c%d-c%d' % (LO, HI), n, per, first, ev)


def sleeptail():
    regs = regions('B03A:11,5E4B:1,A013:1,AFAB:11,0027:1')
    o = run_oracle('sleep02', None, regs, '100:8,0:1500')
    p = run_port('sleep02', None, regs, '', 1300)
    f = fields_of('flag=B03A:3,atb=AFAB:11,frz=A013:1,it=0027:1')
    n, per, first = compare(o, p, f, 452 + CS, 1500 + CS)
    return report('sleeptail c%d-c%d' % (452 + CS, 1500 + CS), n, per, first,
                  ' | cartridge $B03A[0] through GATO\'s song: %s' % ' '.join(e for e in events(o, 0xB03A, 1, 1005 + CS)[:4]))


def chaosenemy():
    regs = regions('614A:8,6130:2,5E30:2,5EB0:2,AE7A:11,B268:11,AEE3:3,B1D4:16,A013:1,5E0A:11,AF24:1,AFAB:11')
    o = run_oracle('chaosE', None, regs, '100:8,0:%d,100:2,0:10,100:2,0:2100' % (632 + CS))
    p = run_port('chaosE', None, regs, 'n:%d,a:2,n:10,a:2,n:3000' % (413 + PS), 2600)
    f = fields_of('st=614B:1,hpG=6130:2,hp0=5E30:2,hp1=5EB0:2,chz=AE80:1,b26e=B26E:1,op=AEE3:3,ptr=B1DA:2,req=5E10:1,'
                  'af24=AF24:1,frz=A013:1')
    n, per, first = compare(o, p, f, 787 + CS, 2700 + CS)
    return report('chaosenemy c%d-c%d' % (787 + CS, 2700 + CS), n, per, first,
                  ' | cartridge GATO actions ($A013): %s' % ' '.join(events(o, 0xA013, 1, 783 + CS)))


def seq(rows, fields, lo, off=0):
    """The ordered list of distinct states of the fields, from frame lo (timing ignored)."""
    st, last = state_at(rows)
    out, prev = [], None
    for f in sorted(st):
        if f < lo:
            continue
        v = tuple(value(st[f], a, n) for _, a, n in fields)
        if v != prev:
            out.append((f + off, v))
            prev = v
    return out


def lockdrop():
    pokes = '2837=FF,2830=08,2607=63,2608=00,2609=63,260A=00'
    regs = regions('A0D1:3,5E4B:1,A013:1,B3C8:1,5E34:2,AFAB:3,6130:2')
    o = run_oracle('lock08', pokes, regs, '100:8,0:%d,20:2,0:10,100:2,0:10,100:2,0:10,100:2,0:500' % (452 + CS))
    p = run_port('lock08', pokes, regs, 'n:%d,d:2,n:10,a:2,n:10,a:2,n:10,a:2,n:600' % (233 + PS), 800)
    f = fields_of('lock=A0D1:3,st0=5E4B:1,frz=A013:1,drop=B3C8:1,mp0=5E34:2,hpG=6130:2')
    # from GATO's locking attack's close to before his next action: the lock latch, the drop, no action, no MP spent
    os_ = [x for x in seq(o, f, 600 + CS) if x[0] <= 1000 + CS]
    ps_ = [x for x in seq(p, f, 600 + CS - OFF, OFF) if x[0] <= 1000 + CS]
    n = min(len(os_), len(ps_))
    first = next((i for i in range(n) if os_[i][1] != ps_[i][1]), None)
    names = ' '.join(k for k, _, _ in f)
    print('status-cmp lockdrop c%d-c%d: states (%s) cartridge %d port %d, %s; cartridge %s | port %s' % (
        600 + CS, 1000 + CS, names, len(os_), len(ps_), 'identical in order' if first is None and len(os_) == len(ps_) else
        'first difference at state #%s' % first,
        ' '.join('c%d:%s' % (fr, '/'.join(v)) for fr, v in os_[:6]),
        ' '.join('c%d:%s' % (fr, '/'.join(v)) for fr, v in ps_[:6])))
    return 0 if first is None and len(os_) == len(ps_) else 1


def blindgate():
    regs = regions('614A:8,6130:2,5E30:2,5EB0:2,AEE3:3,A013:1')
    o = run_oracle('blindE', None, regs, '100:8,0:%d,100:2,0:10,100:2,0:700' % (632 + CS))
    p = run_port('blindE', None, regs, 'n:%d,a:2,n:10,a:2,n:1000' % (413 + PS), 1100)
    f = fields_of('st=614B:1,hpG=6130:2,hp0=5E30:2,hp1=5EB0:2,op=AEE3:3,frz=A013:1')
    n, per, first = compare(o, p, f, 787 + CS, 1300 + CS)
    return report('blindgate c%d-c%d' % (787 + CS, 1300 + CS), n, per, first,
                  ' | cartridge $A013 after Crono\'s blinding hit: %s' % ' '.join(events(o, 0xA013, 1, 783 + CS)))


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else 'all'
    fns = {'lifeline': lifeline, 'sleeptail': sleeptail, 'chaosenemy': chaosenemy, 'lockdrop': lockdrop,
           'blindgate': blindgate}
    todo = list(fns) if what == 'all' else [what]
    bad = 0
    try:
        for k in todo:
            bad += 1 if fns[k]() else 0
    finally:
        shutil.rmtree(OUT, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
