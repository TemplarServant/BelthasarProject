#!/usr/bin/env python3
r"""A DIFFERENT leg replayed on the port with its pad anchored on the game's own moments.

    python tools/playthrough_events.py <leg>... [-jN] [--show]     (a leg by its number or name)

The route's pad is the cartridge's, frame by frame. A fight lasts tens of thousands of frames and its command windows
open where the battle's timing puts them: a window a frame or two off on the port (the SPC's fade lattice, a load's
length -- CLAUDE.md's timing class) and every later key lands in another window, another command is given, and the fight
parts. This tool cuts the cartridge's pad at the moments the player answered and has the port wait for the same moment:

  * a command window open and idle -- `$95D5` a seat (0..2), `$95DB` = 0 (the command row), `$9609` = 0 (no target) --
    in a fight, the pilot's decisions;
  * the control handed back after a fight (`$011F` = 1 on a lit field, the fight over);
  * a fade-in's first frame on the field (`$0119` 0 -> non-zero: a location load's end, whose length is the CPU's).

Each anchor becomes `W<addr>=<val>` tokens of the port's walkscript (src/field.c: the script holds until the byte reads
the value; FFFFF = in a fight), the idle frames the cartridge spent before it dropped. The keys after it keep their
offsets from the moment. What the port then does is graded by VALUE, every chain in its order (the frames are the port's
own), and by the end state (tools/playthrough_triage.py's regions, the port's WRAM when the script ran out):

    events: <leg> SAME-GAME (<n> anchors) -- end regions same
    events: <leg> DIFFERENT <chain>: cartridge <values> / port <values> ...
A SAME-GAME leg is the user's acceptable class (2026-11-01): what differs is when, not what.

Limit (2026-11-02, first try on leg 67): the windows are answered in the cartridge's ORDER. A port whose ATB opens them
in another order waits for a seat whose window never comes first (an open window holds the next seat's), and the fight
is lost while it waits -- 67 went worse than its plain pad, which playthrough_align.py grades SYNC-ONLY. Use align first.
"""
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import playthrough as P
import playthrough_triage as T
import playthrough_align as A

WATCH = [('95D5', 1), ('95DB', 1), ('9609', 1), ('0117', 1), ('011F', 1), ('0119', 1), ('0100', 2)]


def cart_frames(start, pad):
    """The cartridge's watched bytes after every frame of the leg: a list of dicts, index = frame."""
    mons = ','.join('7E%s:%d' % (a, n) for a, n in WATCH)
    n = sum(k for _, k in pad)
    r = subprocess.run([P.CT, P.ROM, '--monitor', mons, '--from', start, '--input', P.btnscript(pad),
                        '--frames', str(n), '--diff'], cwd=P.ROOT, env=P.RULE, capture_output=True, text=True,
                       errors='replace')
    seen = {}
    for line in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', line)
        if m:
            kv = dict((k[2:], v) for k, v in re.findall(r'\b(7E[0-9A-F]{4})=([0-9A-F]+)', m.group(2)))
            if len(kv) == len(WATCH):
                seen[int(m.group(1))] = kv
    out, cur = [], None
    for f in range(n + 1):
        cur = seen.get(f, cur)
        out.append(cur)
    return out


def anchors(fr):
    """(frame, [(addr, op, val)]) at each anchor of the cartridge's run."""
    out = []
    idle = lambda v: v and int(v['95D5'], 16) < 3 and v['95DB'] == '00' and v['9609'] == '00'
    fight = lambda v: v and (int(v['0117'], 16) & 1)
    world = lambda v: v and (int(v['0100'][2:] + v['0100'][:2], 16) & 0x1FF) >= 0x1F0
    was_fight = False
    for f in range(1, len(fr)):
        a, b = fr[f - 1], fr[f]
        if not b:
            continue
        if fight(b):
            was_fight = True
            if idle(b) and not idle(a):
                out.append((f, [('FFFFF', '=', '1'), ('95D5', '=', b['95D5']), ('95DB', '=', '00'), ('9609', '=', '00')]))
        elif was_fight and b['011F'] == '01' and b['0119'] != '00' and not world(b):
            was_fight = False
            out.append((f, [('FFFFF', '=', '0'), ('011F', '=', '01'), ('0119', '!', '00')]))
        elif a and a['0119'] == '00' and b['0119'] != '00' and not world(b) and not fight(b):
            out.append((f, [('FFFFF', '=', '0'), ('0119', '!', '00')]))
    return out


def script(pad, anc):
    """The port's walkscript: the pad (one frame late, as playthrough.walkscript) with each anchor's waits before its
    frame and the zero frames just before it (the cartridge waiting for that moment) dropped."""
    fr = A.frames_of(pad)
    at = dict(anc)
    toks, run = ['.:1'], []                     # run: the frames since the last anchor or key, to drop at an anchor

    def flush(keys):
        for b in keys:
            if toks and toks[-1].startswith(('H%X:' % b) if b else '.:'):
                k, n = toks[-1].rsplit(':', 1)
                toks[-1] = '%s:%d' % (k, int(n) + 1)
            else:
                toks.append(('H%X:1' % b) if b else '.:1')
    for f, b in enumerate(fr):
        if f in at:
            run = []                            # the cartridge's idle wait: the W replaces it
            toks.extend('W%s%s%s' % (a, op, v) for a, op, v in at[f])
        if b == 0:
            run.append(0)
            continue
        flush(run); run = []
        flush([b])
    flush(run)
    return ','.join(toks)


def port_run(start, tmp, ws, cap):
    w = T.wram_of(start, tmp)
    lw = os.path.join(tmp, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + w)
    loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
    dump = os.path.join(tmp, 'ev_end.wram')
    if os.path.exists(dump):
        os.remove(dump)
    wsf = os.path.join(tmp, 'ev.ws')
    open(wsf, 'w').write(ws)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=start, CT_WS_BATTLE_LEAD='1', CT_SRAM_FRESH='1', CT_SRAM=os.path.join(tmp, 'port.srm'),
               CT_FIELD_MEMMON=','.join('%s:%d' % (p, n) for _, _, p, n in P.MON),
               CT_DUMPWRAM=dump, CT_DUMPWRAM_AT='end')
    p = subprocess.Popen([P.FIELD, '--assets', 'ct_assets.dat', '--scene', str(loc), '--loadwram', lw,
                          '--walkscript', ws], cwd=P.ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                         text=True, errors='replace')
    out, end = [], None
    for line in p.stdout:
        m = re.match(r'walkscript-end f(\d+)', line)
        if m:
            end = int(m.group(1))
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', line)
        if m:
            kv = {k: v.rstrip('*') for k, v in re.findall(r'\b([0-9A-F]+)=([0-9A-F*]+)', m.group(2))}
            if all(q in kv for _, _, q, _ in P.MON):
                out.append((int(m.group(1)), {k: kv[q] for k, _, q, _ in P.MON}))
            if int(m.group(1)) > cap:                   # a wait never met: the port went another way
                break
        if end is not None and os.path.exists(dump) and os.path.getsize(dump) >= 0x20000:
            break
    p.kill(); p.wait()
    return out, end, (open(dump, 'rb').read() if os.path.exists(dump) else None)


def values(ch):
    return [v for _, v in ch]


def grade(legs, i, show):
    lg = legs[i]
    start = P.start_of(legs, i)
    if lg['oracle_only'] or start == 'boot' or not os.path.exists(start):
        return 'events: %s skipped (no start state)' % lg['name']
    tmp = os.path.join(P.TMP, 'events_%s' % lg['name'])
    os.makedirs(tmp, exist_ok=True)
    fr = cart_frames(start, lg['pad'])
    anc = anchors(fr)
    ws = script(lg['pad'], anc)
    o = P.chains(P.oracle_timeline(start, lg['pad']))
    tl, end, w = port_run(start, tmp, ws, 2 * len(fr) + 3000)
    p = P.chains(tl)
    if end is not None:
        p = {k: [(f, v) for f, v in ch if f <= end] for k, ch in p.items()}
    o['ctrl'], p['ctrl'] = A.deblip(o['ctrl']), A.deblip(p['ctrl'])
    bad = [k for k in o if values(o[k]) != values(p[k])]
    cart_end = T.wram_of(P.state_of(lg['name']), tmp) if os.path.exists(P.state_of(lg['name'])) else None
    rep = T.diff(cart_end, w) if (cart_end and w) else ['no end dump (the script did not run out: the port stalled)']
    head = 'events: %s %s (%d anchors, port end f%s / cart f%d)' % (
        lg['name'], 'SAME-GAME' if not bad and not rep else 'DIFFERENT', len(anc), end, len(fr) - 1)
    lines = [head + ' -- end regions ' + ('same' if not rep else ' | '.join(rep))]
    for k in bad:
        lines.append('    %-5s cartridge %s' % (k, ' '.join('%s@%d' % (v, f) for f, v in o[k][:24])))
        lines.append('    %-5s port      %s' % (k, ' '.join('%s@%d' % (v, f) for f, v in p[k][:24])))
    if show:
        lines.append('    anchors: ' + ' '.join('f%d' % f for f, _ in anc[:60]))
    return '\n'.join(lines)


def main():
    a = sys.argv[1:]
    jobs = 1
    for x in list(a):
        if x.startswith('-j'):
            jobs = int(x[2:] or os.cpu_count()); a.remove(x)
    show = '--show' in a
    a = [x for x in a if x != '--show']
    if not a:
        sys.exit(__doc__)
    legs = P.route()

    def leg(x):
        hits = [i for i, lg in enumerate(legs) if lg['name'] == x or lg['name'].split('_')[0] == x]
        return hits[0] if hits else P.find(legs, x)
    with ThreadPoolExecutor(jobs) as ex:
        for line in ex.map(lambda i: grade(legs, i, show), [leg(x) for x in a]):
            print(line, flush=True)


if __name__ == '__main__':
    main()
