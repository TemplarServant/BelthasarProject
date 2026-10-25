#!/usr/bin/env python3
r"""A fight the party may lose -- and the game goes on: the field's return after it, port against the rule's cartridge.

    python tools/battle_lost_continue_cmp.py

GATO's fight is one of them: its `D8 92 20` stages `$2A1F = 20` (bit 5), and C0/0289-029E sends a lost fight (`$7F01EC` = 1)
with bit 5 set to C0/0304 -- the same return tail a won fight takes: the map rebuilt, obj0's slot 1, and C0/0343-038C, the
followers' regroup (`$7F03FE` 1 / 2, then 3 when they are back, C0/6C81) and the control's return (`$011F`). tools/
battle_ab.py's enemy B6 wipes the party in GATO's arena (saves/battle_re/gato_wait.state, the same pokes on both sides: the
three PCs, their techs, 99 MP, `$1111 = B6`); the port runs its own fight and return (CT_AUDIO_SEEDSTATE + CT_AUDIO_LOCKSTEP).

Compared: every change of `$7F01EC`, `$7F03FE` and `$011F` from the fight's end to f1100, both sides, frame for frame.
Prints `lost-continue: cartridge <changes> | port <changes> | IDENTICAL` (or DIFFERENT). Roadmap G1a item 4.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'saves/battle_re/gato_wait.state'
FROM, TO = 800, 1100
OUT = os.path.join(ROOT, 'build', '_lost_continue')


def pokes():
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import battle_ab as B
    return B.pokes_for([0, 1, 2], extra={0x1111: 0xB6})


def changes(rows, keys):
    out, last = [], {}
    for f in sorted(rows):
        for k in keys:
            v = rows[f].get(k)
            if v is not None and v != last.get(k):
                if FROM <= f <= TO:
                    out.append('%d:%s=%s' % (f, k, v))
                last[k] = v
    return out


def main():
    pk = pokes()
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1',
               PEEK_POKE=','.join('%X=%02X' % kv for kv in sorted(pk.items())))
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc'),
                        '--monitor', '7F01EC:1,7F03FE:1,7E011F:1', '--from', os.path.join(ROOT, STATE),
                        '--input', '100:8,0:%d' % TO, '--diff'], cwd=ROOT, env=env, capture_output=True, text=True,
                       errors='replace')
    orows = {}
    for line in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', line)
        if m:
            orows[int(m.group(1))] = {a: v for a, v in re.findall(r'\b(7F01EC|7F03FE|7E011F)=([0-9A-F]+)', m.group(2))}
    w = bytearray(open(os.path.join(ROOT, STATE), 'rb').read()[0x45:0x45 + 0x20000])
    for a, v in pk.items():
        w[a] = v
    os.makedirs(OUT, exist_ok=True)
    seed = os.path.join(OUT, 'seed%d.lakewram' % os.getpid())
    open(seed, 'wb').write(b'#lakewram\nRAM:131072:' + bytes(w))
    penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                CT_AUDIO_SEEDSTATE=os.path.join(ROOT, STATE), CT_FIELD_MEMMON='101EC:1,103FE:1,011F:1',
                CT_SHOTS=os.path.join(OUT, '_p%d' % os.getpid()), CT_SHOTS_AT=str(TO), CT_SHOTS_EXIT='1')
    p = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '6',
                        '--loadwram', seed, '--walkscript', 'a:8'], cwd=ROOT, env=penv, capture_output=True, text=True,
                       errors='replace')
    for f in (seed, os.path.join(OUT, '_p%d_f%d.raw' % (os.getpid(), TO))):
        try:
            os.remove(f)
        except OSError:
            pass
    prows = {}
    names = {'101EC': '7F01EC', '103FE': '7F03FE', '011F': '7E011F'}
    for line in p.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\w+\s+(.*)', line)
        if m:
            prows[int(m.group(1))] = {names[a]: v for a, v in re.findall(r'\b(101EC|103FE|011F)=([0-9A-F]+)', m.group(2))}
    keys = ['7F01EC', '7F03FE', '7E011F']
    oc, pc = changes(orows, keys), changes(prows, keys)
    print('lost-continue: cartridge %s | port %s | %s' % (' '.join(oc), ' '.join(pc),
                                                          'IDENTICAL' if oc == pc else 'DIFFERENT'))


if __name__ == '__main__':
    main()
