#!/usr/bin/env python
"""The overworld's event cells ($0280 = 1), the port against the cartridge (disasm/notes/ow_event_cells.md).

Two scenes end on a world cell that runs a script on arrival: scene 02F (Heckran's cave: the whirlpool) sets $7F:01F0 bit 0
and exits to 1F0 at (37, 32), where world 0's bootstrap has armed event 0 (C2:8300's record 1B 19 01 -> script $7F:113F, the
party out of the water onto the shore by Lucca's house); scene 097 sets $7F:01F3 bit 4 and exits to 1F1 at (35, 3F), world 1's
event 0 ($7F:0B91). No battery save stands there, so the tool makes one: Jackie's ch04 / ch10 slot 0 with the saved place
(slot +5F5 / +5F6, the 8 x 8 cell) moved onto the event's cell and the story bit set (slot +603 = $7F:0000), its sum
recomputed (FF/FBE7's). The cartridge boots it (CT_ORACLE_SRAM, Start, the Load list, A), the port too (ct_field --boot).

Graded, from the frame `$0280` becomes 1: every change of `$0280`, the lead's `$0283/$0285` and the followers' `$0287-$028D`
(cartridge --monitor, port CT_OWVEHTRACE), and pictures (the port's P against the cartridge's --peek P + 1, both counted from
that frame).

    python tools/ow_event_cmp.py [heckran|w1]
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vwf_cost_proto as V  # noqa: E402  (slot_sum: FF/FBE7's checksum)
import epoch_board_cmp as E  # noqa: E402  (raw_rgb)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JK = os.path.join(ROOT, 'saves', 'downloaded', 'ctjackie')
OUT = os.path.join(ROOT, 'build', '_owevent')
SCEN = {
    'heckran': dict(srm='ch04', cell=(0x37, 0x32), flag=(0x1F0, 0x01), shots=(40, 95, 130, 200, 262)),
    'w1': dict(srm='ch10', cell=(0x35, 0x3F), flag=(0x1F3, 0x10), shots=(30, 60, 90)),
}
DRIVE_O = '0:300,8:2,0:120,100:2,0:%d'      # Start f300, the Load list, A f422 (memory: ctjackie-saves)
DRIVE_P = 'n:133,s:1,n:125,a:1,n:%d'


def make_srm(tag, sc):
    s = bytearray(open(os.path.join(JK, 'srm', sc['srm'] + '.srm'), 'rb').read())
    s[0x5F5], s[0x5F6] = sc['cell']
    s[0x603 + sc['flag'][0]] |= sc['flag'][1]
    c = V.slot_sum(bytes(s), 0)
    s[0x1FF0:0x1FF2] = bytes([c & 0xFF, c >> 8])
    p = os.path.join(OUT, tag + '.srm')
    open(p, 'wb').write(s)
    return p


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'heckran'
    sc = SCEN[tag]
    os.makedirs(OUT, exist_ok=True)
    srm = make_srm(tag, sc)
    oenv = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_SRAM=srm)
    boot = os.path.join(OUT, tag + '_boot.state')
    subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--fb', os.path.join(OUT, tag + '_boot.raw'), '--frames', '133'],
                   cwd=ROOT, env=dict(oenv, PEEK_SAVE=boot), capture_output=True)
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--monitor', '7E0280:1,7E0283:12,7E027C:1', '--from', boot,
                        '--btnscript', DRIVE_O % 700, '--frames', '1120', '--diff'], cwd=ROOT, env=oenv, capture_output=True,
                       text=True, errors='replace')
    oe = []
    for l in r.stdout.splitlines():
        m = re.match(r'\s+f(\d+)\s+7E0280=(..)\*?\s+7E0283=(.{24})\*?\s+7E027C=(..)', l)
        if m:
            q = m.group(3)
            w = lambda i: q[4 * i + 2:4 * i + 4] + q[4 * i:4 * i + 2]
            oe.append((int(m.group(1)), (m.group(2),) + tuple(w(i) for i in range(6)) + (m.group(4),)))
    pe = []
    def port(shots):
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_SRAM=srm, CT_OWVEHTRACE='1',
                   CT_SHOTS=os.path.join(OUT, tag + '_p'), CT_SHOTS_AT=','.join(str(f) for f in shots), CT_SHOTS_EXIT='1')
        return subprocess.run([os.path.join(ROOT, 'build', 'ct_field.exe'), '--assets', 'ct_assets.dat', '--boot', '--walkscript',
                               DRIVE_P % 900], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    r = port([900])
    for l in r.stdout.splitlines():
        m = re.match(r'owveh: pass \d+ f(\d+) 027C=(..) .* 0280=(..) 0283=(....),(....),(....),(....),(....),(....)', l)
        if m:
            pe.append((int(m.group(1)), tuple(m.group(k) for k in (3, 4, 5, 6, 7, 8, 9, 2))))
    for l in sorted(set(x for x in (r.stdout + r.stderr).splitlines() if 'UNDERIVED' in x)):
        print('ow-event %s: port says %s' % (tag, l))

    def rel(ev):
        t0 = next((f for f, v in ev if v[0] == '01'), None)
        out, last = [], None
        for f, v in ev:
            if t0 is None or f < t0 or v == last:
                continue
            out.append((f - t0, v)); last = v
        return out, t0
    o, ot = rel(oe)
    p, pt = rel(pe)
    strip = lambda ev: [(d, v[:7]) for d, v in ev]          # the event's own bytes; `$027C` reported apart
    def dedupe(ev):
        out, last = [], None
        for d, v in ev:
            if v != last:
                out.append((d, v)); last = v
        return out
    os_, ps = dedupe(strip(o)), dedupe(strip(p))
    end = lambda ev: next((d for d, v in ev if v[0] == '00'), None)
    print('ow-event %s: timeline %s -- %d changes from `$0280 = 1` (cartridge f%s, port f%s); `$0280 = 0` at +%s / +%s' % (
        tag, 'same' if os_ == ps else 'DIFFERENT', len(os_), ot, pt, end(os_), end(ps)))
    if os_ != ps:
        for a, b in zip(os_, ps):
            if a != b:
                print('ow-event %s:   first difference cartridge +%d:%s port +%d:%s' % (tag, a[0], '/'.join(a[1]), b[0], '/'.join(b[1])))
                break
    c1 = lambda ev: next((d for d, v in ev if v[7] == '01'), None)
    print('ow-event %s: `$027C = 1` at +%s on the cartridge, +%s in the port' % (tag, c1(o), c1(p)))
    if os.environ.get('OWEVENT_DUMP'):
        oi, pi = dict(o), dict(p)
        for d in sorted(set(oi) | set(pi)):
            print('ow-event %s:   +%d %s %s' % (tag, d, '/'.join(oi[d]) if d in oi else '-', '/'.join(pi[d]) if d in pi else '-'))
    if ot is None or pt is None:
        return 0
    port([pt + d for d in sc['shots']] + [pt + max(sc['shots']) + 2])
    for d in sc['shots']:
        f = ot + d + 1
        raw = os.path.join(OUT, '%s_o%d.raw' % (tag, f))
        subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', boot, raw, '--btnscript', DRIVE_O % (f - 424)],
                       cwd=ROOT, env=oenv, capture_output=True)
        po = E.raw_rgb(os.path.join(OUT, '%s_p_f%d.raw' % (tag, pt + d)), False)
        oo = E.raw_rgb(raw, True)
        n = sum(po(x, y) != oo(x, y) for y in range(224) for x in range(256))
        print('ow-event %s: picture +%d %d px differ' % (tag, d, n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
