#!/usr/bin/env python3
r"""Yakra's fight against the logical-frame oracle.

    python tools/yakra_cmp.py

No capture holds a Yakra fight, so both sides stage one the ROM's way: the field object GATO stands on (object 8,
X = $10) carries the enemy id the battle's staging copies -- C0/0557 `LDA $1101,Y / STA $7E29C4,X` -- and it is
poked from GATO's 0x92 to Yakra's 0x90 on both sides: the cartridge's saves/battle_re/gato_wait.state with
PEEK_POKE=7E1111=90 (`--input 100:8,0:3000`), the port's saves/battle_re/gato_t0.raw.wram copied to build/ with the
same byte. The battle then loads Yakra from the ROM on both: its stats, its AI (CC:A6FF), its walk record (E4/F600 +
0x90 * 10), its sheet. Port frame p is oracle frame p + 227 (the GATO fixture's own offset, tools/gato_logical_cmp.py).

Graded:
  * the fight up to the first tech's turn (oracle f372 .. f1289): $A013 (the actions), $AEE3 (the AI's turns: attack 0
    twice, the op 00 move, tech 0E), Crono's and Marle's HP -- per frame;
  * tech 0E (NeedleSpin, CD/61F0[0E] = CD/9CEB, sixteen tracks): from each side's own $A013 rise, every change of every
    track's cursor $5DBD + 4t (CT_BATTLE_ASCIP), and the span to $A013's fall. The needles fly on op A2 (C1/6751, walk
    until the grid ahead blocks) and slide to their siblings (`12 1E` -- C1/77EC, effect object k = mode - 1C);
  * the CD/0033 name box ("NeedleSpin / Yakra's needle attack.", table CC:CBC9[06]): the tech's turn -> $A013 gap, and
    from the turn the post ($CE18), the band ($CCEC = 10), the dwell ($CE17, $CCED) and the close ($CE17 = FF). The
    cartridge's are 3 frames later from the band on: C2/0009's render, CPU time the port does not price (battle_yakra.md
    sec. 4).
Under the rule (CT_ORACLE_NMION_HOLD=1 + CT_ORACLE_IFAST=1): the rule's oracle (+ CT_ORACLE_VIRTUAL_BEAM=1), the fixture's
offset 221 and the same window from the rule's first iteration head (f355 .. f1272) -- tools/gato_logical_cmp.py says why.
"""
import os
import re
import shutil
import subprocess
import sys

RULE = os.environ.get('CT_ORACLE_NMION_HOLD', '1') != '0' and os.environ.get('CT_ORACLE_IFAST', '1') != '0'
OFF = 221 if RULE else 227
FIRST, LAST = (355, 1272) if RULE else (372, 1289)


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    os.chdir(root)
    os.makedirs('build', exist_ok=True)
    fx = os.path.join('build', '_yakra_t0.raw')
    for ext in ('', '.oam', '.vram'):
        shutil.copyfile('saves/battle_re/gato_t0.raw' + ext, fx + ext)
    w = bytearray(open('saves/battle_re/gato_t0.raw.wram', 'rb').read())
    if w[0x1111] != 0x92:
        sys.exit('yakra: the fixture\'s object 8 is not GATO ($1111 = %02X)' % w[0x1111])
    w[0x1111] = 0x90
    open(fx + '.wram', 'wb').write(w)

    env = dict(os.environ, CT_BATTLE_SCRIPT='n:3000', CT_BATTLE_DEBUGEND='1520', CT_NOPACE='1', SDL_AUDIODRIVER='dummy',
               SDL_VIDEODRIVER='dummy', CT_BATTLE_MEMMON='A013:1,AEE3:3,5E30:2,5EB0:2,CE17:2,CCEC:1,CCED:1', CT_BATTLE_ASCIP='1')
    env.pop('CT_ORACLE_LOGICAL', None)
    pr = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--battletest', fx + '.wram'],
                        env=env, capture_output=True, text=True, errors='replace')
    oenv = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_POKE='7E1111=90')
    if RULE:
        oenv['CT_ORACLE_VIRTUAL_BEAM'] = '1'
    orr = subprocess.run([os.path.abspath('build/ct.exe'), '--monitor', '7EA013:1,7EAEE3:3,7E5E30:2,7E5EB0:2,7E5DBD:64,7ECE17:2,7ECCEC:1,7ECCED:1',
                          '--from', 'saves/battle_re/gato_wait.state', '--input', '100:8,0:3000', '--frames', '1750',
                          '--diff'], env=oenv, capture_output=True, text=True, errors='replace')

    port, pips = {}, {t: [] for t in range(16)}
    for l in (pr.stdout + pr.stderr).splitlines():
        m = re.match(r'memmon f(\d+) rel=\S+\s+(.*)', l)
        if m:
            port[int(m.group(1)) + OFF] = dict(re.findall(r'([0-9A-F]{4})=([0-9A-F]+)', m.group(2)))
        m = re.match(r'ascip f(\d+) rel=\S+ t(\d+) ([0-9A-F]{4})', l)
        if m:
            pips[int(m.group(2))].append((int(m.group(1)) + OFF, m.group(3)))
    orc = {}
    for l in orr.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', l)
        if m:
            orc[int(m.group(1))] = {k[2:]: v for k, v in re.findall(r'(7E[0-9A-F]{4})=([0-9A-F]+)', m.group(2))}
    if not port or not orc:
        sys.exit('yakra: a side printed nothing (port %d lines, oracle %d)' % (len(port), len(orc)))

    def fill(d, keys, last):
        out, cur = {}, {}
        for f in range(0, last + 1):
            if f in d:
                cur = dict(cur); cur.update({k: v for k, v in d[f].items() if k in keys})
            out[f] = dict(cur)
        return out
    keys = ('A013', 'AEE3', '5E30', '5EB0')
    P, O = fill(port, keys, 1730), fill(orc, keys, 1730)
    bad = [f for f in range(FIRST, LAST + 1) if any(P[f].get(k) != O[f].get(k) for k in keys)]
    if not bad:
        print('yakra: oracle f%d-f%d (port f%d-f%d): $A013, $AEE3 and both HPs identical on all %d frames' % (
            FIRST, LAST, FIRST - OFF, LAST - OFF, LAST - FIRST + 1))
    else:
        f = bad[0]
        print('yakra: %d frames differ from oracle f%d: port %s oracle %s' % (len(bad), f, P[f], O[f]))

    def tech(D):                                 # the turn that writes 02 0E, then $A013's rise and fall after it
        turn = next((f for f in range(FIRST, 1730) if D[f].get('AEE3') == '020E04'
                     and D[f - 1].get('AEE3') != '020E04'), None)
        if turn is None:
            return None
        rise = next((f for f in range(turn, 1730) if D[f].get('A013') == '01'), None)
        fall = next((f for f in range(rise, 1730) if D[f].get('A013') == '00'), None) if rise else None
        return turn, rise, fall
    to, tp = tech(O), tech(P)
    if not to or not tp or None in to or None in tp:
        sys.exit('yakra: tech 0E not found (oracle %s, port %s)' % (to, tp))
    print('yakra: tech 0E turn -> $A013: cartridge %d frames (the CD/0033 name box: C2/0009 + 11), port %d' % (
        to[1] - to[0], tp[1] - tp[0]))
    print('yakra: tech 0E span (rise -> fall): cartridge %d, port %d' % (to[2] - to[1], tp[2] - tp[1]))
    # the name box's bytes from the turn: the post ($CE18 = 06), the band ($CCEC = 10), the dwell ($CE17 = 06, $CCED),
    # the close ($CE17 = FF again)
    B = fill(orc, ('CE17', 'CCEC', 'CCED'), 1750), fill(port, ('CE17', 'CCEC', 'CCED'), 1750)
    def box(D, t):
        post = next((f for f in range(t, t + 40) if D[f].get('CE17', '')[2:] == '06'), None)
        band = next((f for f in range(t, t + 40) if D[f].get('CCEC') == '10'), None)
        dwell = next((f for f in range(t, t + 40) if D[f].get('CE17') == '0606' and D[f].get('CCED') == '01'), None)
        close = next((f for f in range(dwell or t, t + 400) if D[f].get('CE17', '')[:2] == 'FF'), None) if dwell else None
        return [None if x is None else x - t for x in (post, band, dwell, close)]
    bo, bp = box(B[0], to[0]), box(B[1], tp[0])
    print('yakra: name box from the turn -- post/band/dwell/close: cartridge %s, port %s' % (
        '/'.join('%s' % x for x in bo), '/'.join('%s' % x for x in bp)))
    ocur = {t: [] for t in range(16)}
    last = {}
    for f in sorted(orc):
        h = orc[f].get('5DBD')
        if not h:
            continue
        for t in range(16):
            c = h[t * 8 + 2:t * 8 + 4] + h[t * 8:t * 8 + 2]
            if last.get(t) != c:
                last[t] = c
                if to[1] < f < to[2] and c != '0000':
                    ocur[t].append((f - to[1], c))
    same, n, first = 0, 0, None
    for t in range(16):
        a = ocur[t]
        b = [(f - tp[1], c) for f, c in pips[t] if tp[1] < f < tp[2]]
        n += max(len(a), len(b))
        if a == b:
            same += len(a)
        elif first is None:
            k = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
            first = 't%d #%d cartridge %s port %s' % (t, k, a[k:k + 2], b[k:k + 2])
    print('yakra: tech 0E cursors (16 tracks, from each rise): %d/%d changes identical%s' % (
        same, n, '' if first is None else '; first difference ' + first))


if __name__ == '__main__':
    main()
