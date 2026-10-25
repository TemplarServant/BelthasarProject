#!/usr/bin/env python3
r"""The Telepod (loc 008) climbed with A pressed as Crono raises his arms, port against the rule's cartridge.

    python tools/telepod_latch_cmp.py

The user, 2026-10-19 (translated from Portuguese): "on climbing the pod in scene 8, picking up the pendant to go after
Marle, sometimes Crono does the arms-raising animation, lowers them and raises them again". obj11's fn1 (A) and fn2
(touch) are the same code, `EB 30 00 / CA D6 / 04 06 36 / ...` -- `04 06 36` calls Crono's fn6 (the raised arms) at
priority 3 and waits, the latch $7F0980 set. The touch runs it at priority 2; an A press on the frame Crono's fn6 starts
pre-empts it with fn1 at priority 1 (C0/5AC5), and fn1's own `04 06 36` meets the latch still set: C0/6109 `BRL 61A7`,
the POLL on its own operands -- it waits for the fn6 already playing and goes on, ONE raise. The port installed the call
afresh and Crono raised his arms a second time. (ct_vm.c: the 0x02-0x07 arm's latch branch, ssPushAddr;
disasm/notes/call_wait_latch_preempt.md sec. 4.)

The cartridge: saves/field_re/scene8_pod_latch.state (tools/recipes/fxr_scene8_pod.json: Crono below the left pod after Marle vanished),
`--monitor` with Up held 25 frames and A on the 22nd. The port: the same WRAM (--loadwram) and SPC (CT_AUDIO_SEEDSTATE),
the pad one frame later (its keys are the oracle's + 1), CT_FIELD_MEMMON. Each event's frame on both sides, then the
count of Crono's fn6 runs:

    telepod-latch: <event> cartridge f<C> port f<P>
    telepod-latch: Crono's fn6 runs cartridge <n> port <m>
    telepod-latch: <n>/<m> events on the cartridge's frame                (or: ..., the rest differ)
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
STATE = os.path.join(ROOT, 'saves', 'field_re', 'scene8_pod_latch.state')
OUT = os.path.join(ROOT, 'build', '_telepod_latch')
PAD = [(0x10, 21), (0x110, 1), (0x10, 3)]     # Up to the pod; A on the frame Crono's fn6 starts
FRAMES = 140
HOFF = 0x3A0                                  # the script header: 29 objects x 16 function pointers x 2
FN1_CALL, FN1_SND2, PAST_CALL = 0x028F, 0x028D, 0x0293   # obj11: `04 06 36`, `CA D6`, `04 06 47` (past the 91)
FN6_ARMS, FN0_IDLE, FN7_WALK = 0x010C, 0x00F7, 0x0115    # obj3: fn6's `B7 20 06`, fn0's `00`, fn7's `96 04 05`
NAMES = ['obj11 parks on the call (028F)', "Crono's fn6 holds the raised arms (010C)", 'A: obj11 runs fn1 again (028D)',
         "Crono's fn6 returns (00F7)", 'obj11 past the call, on its next (0293)', "Crono's fn7 walks to (4,5) (0115)"]


def events(rows):
    """rows: [(frame, ip11, ip3)] -> ({event: first frame}, fn6 runs)."""
    ev = {}

    def first(name, pred, after=-1):
        for f, a, b in rows:
            if f > after and pred(a, b):
                ev[name] = f
                return f
        return None
    first(NAMES[0], lambda a, b: a == FN1_CALL)
    arms = first(NAMES[1], lambda a, b: b == FN6_ARMS)
    first(NAMES[2], lambda a, b: a == FN1_SND2, arms - 1 if arms is not None else -1)   # from fn6's own frame on
    if arms is not None:
        first(NAMES[3], lambda a, b: b == FN0_IDLE, arms)
    first(NAMES[4], lambda a, b: a == PAST_CALL)
    first(NAMES[5], lambda a, b: b == FN7_WALK)
    runs, prev = 0, None
    for f, a, b in rows:
        runs += b == FN6_ARMS and prev != FN6_ARMS
        prev = b
    return ev, runs


def parse(lines, rx, hexkey):
    rows, cur = [], {}
    for l in lines:
        m = rx.match(l)
        if not m:
            continue
        for k, v in re.findall(r'((?:7E)?[0-9A-F]{4})=([0-9A-F]+)\*?', l):
            cur[k.replace(hexkey, '')] = bytes.fromhex(v)
        if not all(k in cur for k in ('1196', '1186')):
            continue
        ip = lambda k: (cur[k][0] | cur[k][1] << 8) - HOFF
        rows.append((int(m.group(1)), ip('1196'), ip('1186')))
    return rows


def main():
    os.makedirs(OUT, exist_ok=True)
    rule = dict(os.environ, CT_ORACLE_LOGICAL='1')
    used = sum(n for _, n in PAD)
    script = ['%X:%d' % (b, n) for b, n in PAD] + ['0:%d' % (FRAMES - used)]
    mon = subprocess.run([CT, '--monitor', '7E1196:2,7E1186:2', '--from', STATE, '--input', ','.join(script)],
                         cwd=ROOT, env=rule, capture_output=True, text=True, errors='replace').stdout
    cart = parse(mon.splitlines(), re.compile(r'\s+f(\d+)\s'), '7E')
    raw = os.path.join(OUT, 'seed.raw')
    subprocess.run([CT, '--peek', STATE, raw, '--btnscript', '0:0'], cwd=ROOT, env=rule, capture_output=True)
    lw = os.path.join(OUT, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + open(raw + '.wram', 'rb').read())
    walk = ['.:1'] + ['H%X:%d' % (b, n) for b, n in PAD] + ['.:%d' % (FRAMES - used)]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_SEEDSTATE=STATE,
               CT_FIELD_MEMMON='1196:2,1186:2')
    out = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '8', '--loadwram', lw, '--selftest',
                          os.path.join(OUT, 'p.raw'), '--frames', str(FRAMES), '--walkscript', ','.join(walk)],
                         cwd=ROOT, env=env, capture_output=True, text=True, errors='replace').stdout
    port = parse(out.splitlines(), re.compile(r'fmemmon f(\d+) '), '')
    (ce, cr), (pe, pr) = events(cart), events(port)
    good = 0
    for k in NAMES:
        c, p = ce.get(k), pe.get(k)
        good += c is not None and p is not None and p == c
        print('telepod-latch: %s cartridge %s port %s' % (k, 'f%d' % c if c is not None else 'never',
                                                          'f%d' % p if p is not None else 'never'))
    print("telepod-latch: Crono's fn6 runs cartridge %d port %d" % (cr, pr))
    if good == len(NAMES) and cr == pr:
        print("telepod-latch: %d/%d events on the cartridge's frame" % (good, len(NAMES)))
    else:
        print("telepod-latch: %d/%d events on the cartridge's frame, the rest differ" % (good, len(NAMES)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
