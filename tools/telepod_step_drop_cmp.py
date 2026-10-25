#!/usr/bin/env python3
r"""The Telepod (loc 008) touched while Left or Right is held: the lead's step the touch's call drops, port against the
rule's cartridge.

    python tools/telepod_step_drop_cmp.py

The touch's fn2 of obj11 (`EB 30 00 / CA D6 / 04 06 36 / ...`) installs Crono's fn6 in the same event pass in which the
lead's op B0 armed this frame's step (C0/3040 `LDA #01 / STA $1A01,X`, C0/9E29 -> 9E56 the velocity into $1900). The
install ends `STZ $1A01,X` (C0/6199), so C0/A810's integrate (AB06 `BEQ`) does not move him: a lateral press on the touch
frame leaves Crono where the frame began, and the stale velocity sits in $1906 until fn7. The port's host moved the lead
before the tick and kept the step: Crono stood 1 px off, his fn7 MOVETO (`96 04 05`) took one more step, and obj11 left
its `04 06 47` 4 frames late (ct_vm.c vmTicketStop / CtSceneLeadStepFrom; field.c the dropped step).

The cartridge: saves/field_re/scene8_pod_latch.state under the rule (`--monitor`), Up 20 frames, Left or Right 5, Up 20.
The port: the same WRAM (--loadwram) and SPC (CT_AUDIO_SEEDSTATE), the pad one frame later, CT_FIELD_MEMMON. Per pad:

    telepod-step: <pad> <event> cartridge f<C> port f<P>
    telepod-step: <pad> Crono's position identical on <k>/<n> frames
    telepod-step: <n>/<m> events on the cartridge's frame              (or: ..., the rest differ)
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, os.environ.get('CT_BUILD_DIR') or 'build', 'Belthasar.exe')
STATE = os.path.join(ROOT, 'saves', 'field_re', 'scene8_pod_latch.state')
OUT = os.path.join(ROOT, 'build', '_telepod_step')
PADS = [('left', [(0x10, 20), (0x40, 5), (0x10, 20)]), ('right', [(0x10, 20), (0x80, 5), (0x10, 20)])]
FRAMES = 120
HOFF = 0x3A0                                  # the script header: 29 objects x 16 function pointers x 2
FN1_CALL, PAST_47 = 0x028F, 0x0296            # obj11: `04 06 36` (the touch's call), past `04 06 47`
FN0_IDLE, FN7_WALK = 0x00F7, 0x0115           # obj3: fn0's `00`, fn7's `96 04 05`
REGS = '1196:2,1186:2,1806:2,1886:2'
NAMES = ['obj11 parks on the call (028F)', "Crono's fn7 walks to (4,5) (0115)", "Crono's fn7 arrives, idle (00F7)",
         'obj11 past `04 06 47` (0296)']


def events(rows):
    """rows: {frame: {reg: hex}} -> {event: first frame}."""
    ev, fs = {}, sorted(rows)
    ip = lambda r, k: (int(r[k][:2], 16) | int(r[k][2:], 16) << 8) - HOFF

    def first(name, pred, after=-1):
        for f in fs:
            if f > after and pred(rows[f]):
                ev[name] = f
                return f
        return None
    first(NAMES[0], lambda r: ip(r, '1196') == FN1_CALL)
    walk = first(NAMES[1], lambda r: ip(r, '1186') == FN7_WALK)
    if walk is not None:
        first(NAMES[2], lambda r: ip(r, '1186') == FN0_IDLE, walk)
    first(NAMES[3], lambda r: ip(r, '1196') == PAST_47)
    return ev


def parse(text, rx, norm):
    rows, cur = {}, {}
    for l in text.splitlines():
        m = rx.match(l)
        if not m:
            continue
        for k, v in re.findall(r'((?:7E)?[0-9A-F]{4})=([0-9A-F]+)\*?', l):
            cur[norm(k)] = v
        if all(k in cur for k in ('1196', '1186', '1806', '1886')):
            rows[int(m.group(1))] = dict(cur)
    return rows


def fill(rows, n):
    """memmon prints only the frames that changed: carry each row forward over [0, n)."""
    out, last = {}, None
    for f in range(n):
        last = rows.get(f, last)
        if last is not None:
            out[f] = last
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    rule = dict(os.environ, CT_ORACLE_LOGICAL='1')
    raw = os.path.join(OUT, 'seed.raw')
    subprocess.run([CT, '--peek', STATE, raw, '--btnscript', '0:0'], cwd=ROOT, env=rule, capture_output=True)
    lw = os.path.join(OUT, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + open(raw + '.wram', 'rb').read())
    good = total = 0
    for name, pad in PADS:
        used = sum(n for _, n in pad)
        script = ['%X:%d' % p for p in pad] + ['0:%d' % (FRAMES - used + 10)]
        mon = subprocess.run([CT, '--monitor', ','.join('7E' + r for r in REGS.split(',')), '--from', STATE,
                              '--input', ','.join(script), '--frames', str(FRAMES)],
                             cwd=ROOT, env=rule, capture_output=True, text=True, errors='replace').stdout
        cart = fill(parse(mon, re.compile(r'\s+f(\d+)\s'), lambda k: k[-4:]), FRAMES)
        walk = ['.:1'] + ['H%X:%d' % p for p in pad] + ['.:%d' % (FRAMES - used + 10)]
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
                   CT_AUDIO_SEEDSTATE=STATE, CT_FIELD_MEMMON=REGS)
        out = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '8', '--loadwram', lw, '--selftest',
                              os.path.join(OUT, 'p_%s.raw' % name), '--frames', str(FRAMES + 10),
                              '--walkscript', ','.join(walk)],
                             cwd=ROOT, env=env, capture_output=True, text=True, errors='replace').stdout
        port = fill(parse(out, re.compile(r'fmemmon f(\d+) '), lambda k: k), FRAMES)
        ce, pe = events(cart), events(port)
        for k in NAMES:
            c, p = ce.get(k), pe.get(k)
            total += 1
            good += c is not None and p == c
            print('telepod-step: %s %s cartridge %s port %s' % (name, k, 'f%d' % c if c is not None else 'never',
                                                               'f%d' % p if p is not None else 'never'))
        same = sum(1 for f in range(FRAMES) if f in cart and f in port
                   and (cart[f]['1806'], cart[f]['1886']) == (port[f]['1806'], port[f]['1886']))
        print("telepod-step: %s Crono's position identical on %d/%d frames" % (name, same, FRAMES))
        total += 1
        good += same == FRAMES
    if good == total:
        print("telepod-step: %d/%d events on the cartridge's frame" % (good, total))
    else:
        print("telepod-step: %d/%d events on the cartridge's frame, the rest differ" % (good, total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
