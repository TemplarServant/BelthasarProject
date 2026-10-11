#!/usr/bin/env python3
r"""The tent of horrors' 80-point game (loc 1B2, the hostage over the flames), port against the rule's cartridge.

    python tools/tent_hostage_cmp.py [lose|switch ...]          (no argument: both)

The user, 2026-10-08: "the tent of horrors minigame for 80 silver points is bugged when you lose: marle as the hostage
goes down past the fire, and the sparkle that makes her rise when pressed is not appearing". Bekkler's choice 2 (obj11 fn1, 05BB)
calls the hostage's fn7 (`05 02 37`, the PCs' 00C3), whose loop is

    00D3  13 12 20 01 01 49   IF local[12] (16-bit) != 0120 ... else -> 0121 (the flames, then CHANGELOC to the fair)
    00D9  9C 40 01            one vector step down
    00DC  72 12               local[12] += 1, SIXTEEN-bit (C0/6C85: the REP #20 is still on when 6C92-6C97 run)
    00DE  13 12 B2 00 00 05   IF local[12] == 00B2: `E8 0B` + SHOW obj8 -- the blue switch
    00E8  12 19 07 00 31      IF local[19] == 7 (the monsters back in the cage): the win

and the switch (obj8 fn1, 01C3) calls the hostage's fn8 (`9C C0 70` up, `60 70 00 12` local[12] -= 0070). The live VM had
no arm for op 0x72 (nor 0x76): the counter never moved, so the switch never appeared and the flames never came -- the
hostage sank forever. (move.c CtVmLocalIncFamily, the body --sweepops grades, now runs in ct_vm.c too.)

The cartridge: saves/field_re/s1b2_hostage_lake.state (Crono + Marle in the tent, 96 silver points poked into $7F021A,
Bekkler's 80-point game chosen; his text 82 is on screen). Two drives, A on f0/f302/f424/f546 (2 frames each) to close
the rules:
  lose   -- nothing else: the counter starts f652, the switch shows at local[12] = B2, the flames at 0120, loc 005;
  switch -- then Left 96, Up 8, A at f1119 (Crono at the left wall, facing the switch): obj8 fn1 lifts the hostage.
            Its A talks to nothing: at f1124 obj10 sends the monsters' fn7 (`E3 00`, the catch) -- the monsters reached
            row 10 -- and the control is shut before fn8's RETURN;
  escape -- from f780 Crono pushes the three monsters back (A on obj13, obj12, obj14 -- their fn1 `02 1E 35` runs the
            monster's fn5), walks to the left wall and touches the switch (obj8 fn1 at f1097): the catch moves to f1260
            and fn8 RETURNs at f1215 with $011F = 1. The ROM's RETURN (C0/5F74) resumes fn7 and the hanging pose `AA 09`
            holds; the port's RETURN-time reclaim put her on anim 0 (user F5 2026-10-09, "she changes a little after
            rising"). The obj8 IP is not graded here: the touch lands its call on the frame the cartridge mirrors the
            function's first byte (01C3), the $1180 mirror below.
The port runs the same WRAM (--loadwram) and SPC (CT_AUDIO_SEEDSTATE), each key one frame after the cartridge's,
CT_FIELD_MEMMON. Every region's change sequence is compared up to the cartridge's location change (or its last frame):

    tent-hostage: <drive> <region> identical (<n> changes)
    tent-hostage: <drive> <region> differs from f<F>: cartridge <c> port <p>
    tent-hostage: <drive> <event> cartridge f<C> port f<P>
    tent-hostage: <k>/<n> regions identical
($1180, the IP mirror, is not graded: the cartridge shows a called function's first byte on the frame the call installs
it, the port from its first visit -- and a song upload `EA` parks the cartridge's main loop with the IP in a register.)
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'ct_field.exe')
STATE = os.path.join(ROOT, 'saves', 'field_re', 's1b2_hostage_lake.state')
OUT = os.path.join(ROOT, 'build', '_tent_hostage')

# (name, $7E/$7F address, length)
REGIONS = [('loc', 0x7E0100, 2), ('counter local[12]', 0x7F0224, 2), ('caged local[19]', 0x7F0232, 1),
           ('Crono x', 0x7E1802, 2), ('Crono y', 0x7E1882, 2), ('Marle x', 0x7E1804, 2), ('Marle y', 0x7E1884, 2),
           ('switch obj8 ip', 0x7E1190, 2), ('switch obj8 $1B10', 0x7E1B10, 1),
           ('Marle anim $1684', 0x7E1684, 2), ('Marle anim mode $1784', 0x7E1784, 2), ('control $011F', 0x7E011F, 1)]
RULES = [0, 302, 424, 546]                       # the four A presses that close Bekkler's text 82
DRIVES = {
    # name: (cartridge tail after the rules, port tail, frames)
    'lose': ([], [], 1500),
    'switch': (['40:96', '10:8', '0:2', '100:2'], ['l:96', 'u:8', '.:2', 'a:1'], 1800),
    'escape': (['10:10', '100:2', '0:20', '40:32', '10:2', '100:2', '0:20', '80:64', '10:2', '100:2', '0:20', '40:140',
                '10:20'],
               ['u:10', 'a:1', '.:21', 'l:32', 'u:2', 'a:1', '.:21', 'r:64', 'u:2', 'a:1', '.:21', 'l:140', 'u:20'], 1400),
}
SWITCH_AT = {'switch': 1013, 'escape': 780}      # the cartridge frame each drive's tail starts
UNGRADED = {'escape': ('switch obj8 ip',)}


def cart_script(name):
    tail, _, frames = DRIVES[name]
    s, t = [], 0
    for p in RULES:
        if p > t:
            s.append('0:%d' % (p - t))
        s.append('100:2')
        t = p + 2
    if tail:
        s.append('0:%d' % (SWITCH_AT[name] - t))
        s += tail
        t = SWITCH_AT[name] + sum(int(x.split(':')[1]) for x in tail)
    s.append('0:%d' % (frames - t))
    return ','.join(s), frames


def port_script(name):
    _, tail, frames = DRIVES[name]
    s, t = [], 0
    for p in RULES:                              # the port's key lands one frame after the cartridge's
        s += ['.:%d' % (p + 1 - t), 'a:1']
        t = p + 2
    if tail:
        s.append('.:%d' % (SWITCH_AT[name] + 1 - t))
        s += tail
        t = SWITCH_AT[name] + 1 + sum(int(x.split(':')[1]) for x in tail)
    s.append('.:%d' % (frames - t))
    return ','.join(s), frames


def parse(text, port):
    rx = re.compile(r'fmemmon f(\d+) ') if port else re.compile(r'\s+f(\d+)\s')
    seq = {}
    for l in text.splitlines():
        m = rx.match(l)
        if not m:
            continue
        f = int(m.group(1))
        for name, a, n in REGIONS:
            if port:
                key = '%05X' % (a - 0x7E0000) if a >= 0x7F0000 else '%04X' % (a & 0xFFFF)
            else:
                key = '%06X' % a
            mm = re.search(r'(?<![0-9A-F])%s=([0-9A-F]{%d})' % (key, 2 * n), l)
            if mm:
                s = seq.setdefault(name, [])
                if not s or s[-1][1] != mm.group(1):
                    s.append((f, mm.group(1)))
    return seq


def word(v):
    return int(v[2:4] + v[0:2], 16) if len(v) == 4 else int(v, 16)


def first(seq, name, pred, after=0):
    for f, v in seq.get(name, []):
        if f >= after and pred(v):
            return f
    return None


def run(name):
    os.makedirs(OUT, exist_ok=True)
    rule = dict(os.environ, CT_ORACLE_LOGICAL='1')
    cs, frames = cart_script(name)
    spec = ','.join('%06X:%d' % (a, n) for _, a, n in REGIONS)
    mon = subprocess.run([CT, '--monitor', spec, '--diff', '--from', STATE, '--btnscript', cs, '--frames', str(frames)],
                         cwd=ROOT, env=rule, capture_output=True, text=True, errors='replace').stdout
    cart = parse(mon, False)
    # Graded up to the location change: the load of loc 005 after it is the location-load clock's business (the
    # cartridge's NMI-off load lands the fair's objects at +71 under a black screen, the port's in one frame).
    last = first(cart, 'loc', lambda v: word(v) == 0x005)
    if last is None:
        last = max((f for s in cart.values() for f, _ in s), default=0)
    raw = os.path.join(OUT, 'seed.raw')
    subprocess.run([CT, '--peek', STATE, raw, '--btnscript', '0:0'], cwd=ROOT, env=rule, capture_output=True)
    lw = os.path.join(OUT, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + open(raw + '.wram', 'rb').read())
    ps, pframes = port_script(name)
    mm = ','.join(('%05X:%X' % (a - 0x7E0000, n)) if a >= 0x7F0000 else ('%04X:%X' % (a & 0xFFFF, n))
                  for _, a, n in REGIONS)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_SEEDSTATE=STATE,
               CT_FIELD_MEMMON=mm)
    out = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '434', '--loadwram', lw, '--selftest',
                          os.path.join(OUT, 'p.raw'), '--frames', str(pframes), '--walkscript', ps],
                         cwd=ROOT, env=env, capture_output=True, text=True, errors='replace').stdout
    port = parse(out, True)
    good = 0
    graded = [r for r in REGIONS if r[0] not in UNGRADED.get(name, ())]
    for rname, _, _ in graded:
        # $011F on the change frame is the location load's (C0/0BC8 re-opens the control inside the cartridge's NMI-off
        # load; the port's load writes it on its own frame) -- the location-load clock, graded elsewhere.
        end = last - 1 if rname == 'control $011F' else last
        c = [x for x in cart.get(rname, []) if x[0] <= end]
        p = [x for x in port.get(rname, []) if x[0] <= end]
        if c and c == p:
            good += 1
            print('tent-hostage: %s %s identical (%d changes)' % (name, rname, len(c) - 1))
            continue
        k = 0
        while k < min(len(c), len(p)) and c[k] == p[k]:
            k += 1
        cf = c[k] if k < len(c) else None
        pf = p[k] if k < len(p) else None
        f = min(x[0] for x in (cf, pf) if x) if (cf or pf) else 0
        print('tent-hostage: %s %s differs from f%d: cartridge %s port %s' % (
            name, rname, f, '%s@f%d' % (cf[1], cf[0]) if cf else 'none', '%s@f%d' % (pf[1], pf[0]) if pf else 'none'))
    ev = [('counter starts', 'counter local[12]', lambda v: word(v) == 1),
          ('switch shown (local[12] = 00B2)', 'switch obj8 $1B10', lambda v: int(v, 16) & 0x80),
          ('flames (local[12] = 0120)', 'counter local[12]', lambda v: word(v) == 0x120),
          ('location 005', 'loc', lambda v: word(v) == 0x005)]
    if name == 'switch':
        ev.insert(2, ('switch pressed (obj8 fn1 01C3)', 'switch obj8 ip', lambda v: word(v) - 0x300 >= 0x1C3))
    if name in ('switch', 'escape'):
        ev.insert(-2, ('the catch ($011F = 0 after the switch shows)', 'control $011F', lambda v: v == '00', 1009))
    ev.append(('hostage off anim 9 (after her fn7 sets it)', 'Marle anim $1684', lambda v: v[0:2] != '09', 600))
    for e in ev:
        ename, rname, pred, after = e if len(e) == 4 else e + (0,)
        c, p = first(cart, rname, pred, after), first(port, rname, pred, after)
        if c is not None and c > last:
            c = None
        if p is not None and p > last:
            p = None
        print('tent-hostage: %s %s cartridge %s port %s' % (name, ename, 'f%d' % c if c is not None else 'never',
                                                            'f%d' % p if p is not None else 'never'))
    return good, len(graded)


def main():
    names = [a for a in sys.argv[1:] if a in DRIVES] or list(DRIVES)
    good = n = 0
    for name in names:
        g, k = run(name)
        good += g
        n += k
    print('tent-hostage: %d/%d regions identical' % (good, n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
