#!/usr/bin/env python3
r"""The playthrough: one route from power-on to "The End", played on the rule's cartridge and on the port, leg by leg.

    python tools/playthrough.py try  <leg> "<pad>"     play <pad> on the cartridge from the leg's start (authoring)
    python tools/playthrough.py play <leg>|all          play the route's legs on the cartridge, writing their end states
    python tools/playthrough.py cmp  <leg>|all [-v] [-jN] each leg on both sides from its start state, compared (-jN: N legs at a time)
    python tools/playthrough.py scene <leg> "<pad>"     authoring: the pad, then a scene played by its boxes (A when one waits)

THE ROUTE (tools/playthrough/route.txt) is the cartridge's pad, frame by frame, in the --btnscript grammar (`hexbtn:frames`,
LakeSnes serial order: B=1 Y=2 Sel=4 Start=8 Up=10 Down=20 Left=40 Right=80 A=100 X=200 L=400 R=800), cut into LEGS:

    ## 01_bedroom -- Crono wakes; Mom; down to the kitchen
    0:120,100:2,0:40
    ...

A leg starts where the one before it ended (the first at power-on, on an erased cartridge) and lasts exactly the frames its
pad holds. `## name -- note [oracle-only]` opens one; `#` lines are comments; a line may repeat a group: `(100:2,0:38)*20`.
Every leg is written by looking (`try`), never by guessing -- the route is the cartridge's own play, under the rule
(CT_ORACLE_LOGICAL=1). Its states are made from power-on by `play` (saves/playthrough/, local: they are the ROM's output and
`play all` rebuilds them).

THE COMPARISON (`cmp`). From the leg's start state, the cartridge plays the pad (`ct.exe --monitor`) and the port plays the
same pad from the same WRAM and sound (`Belthasar --scene <loc> --loadwram`, CT_AUDIO_SEEDSTATE, CT_AUDIO_LOCKSTEP; the pad
as the walkscript's raw `H` tokens, one frame later -- the port's keys are the cartridge's + 1, ct-frame-alignment-p2).
Graded: the chain of locations ($0100), the storyline's values ($7F:0000) and the party ($2980-$2982) in order, and the
leg's last values -- the things a player sees happen, with no frame tolerance needed. Each change's frame on both sides is
printed (the offset, informational). One line a leg:

    playthrough: 01_bedroom SAME -- locs 000 001 | story 00 01 | party 00 80 80 | end loc 001 story 01 ...
    playthrough: 3/3 legs SAME
"""
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
ROM = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')
ROUTE = os.path.join(ROOT, 'tools', 'playthrough', 'route.txt')
STATES = os.path.join(ROOT, 'saves', 'playthrough')
TMP = os.environ.get('PT_TMP') or os.path.join(ROOT, 'build', '_playthrough')   # PT_TMP: a second run's own scratch
RULE = dict(os.environ, CT_ORACLE_LOGICAL='1')
# What is graded: the location, the storyline, the party. (The port's memmon names $7F:xxxx as 1xxxx.)
MON = [('loc', '7E0100', '0100', 2), ('story', '7F0000', '10000', 1), ('party', '7E2980', '2980', 3),
       ('gold', '7E2C53', '2C53', 3), ('hp', '7E2603', '2603', 2),          # gold (C1/D0A2), Crono's current HP ($2600 + 3)
       ('ctrl', '7E011F', '011F', 1),                                        # the player's control (op E3; a fight takes it)
       ('bri', '7E0119', '0119', 1)]                                         # the brightness: not graded, it gates `ctrl`
# A LOAD'S INSIDE IS NOT GRADED. A location load runs with the NMI off: C0/0BC4 sets $011F = 1 at its start and the new
# scene's fn0 may take it back before the fade-in (002: 1 at f511, `E3 00` at f541, the ramp from f551) -- CPU time on a
# black screen with no joypad, which the port does in one go (CLAUDE.md: timing artefacts are not reproduced). So the
# control byte is graded only on frames whose $0119 is not 0: the frames a player can see and act on.
GRADED = [k for k, _, _, _ in MON if k != 'bri']


# ------------------------------------------------------------------------------------------------------------ the route
def expand(text):
    """A pad's text -> a list of (mask, frames). `(a,b)*N` repeats a group."""
    text = re.sub(r'#.*', '', text)
    text = ','.join(''.join(line.split()) for line in text.splitlines())     # a line ends a token
    text = re.sub(r',+', ',', text).strip(',')
    while True:
        m = re.search(r'\(([^()]*)\)\*(\d+)', text)
        if not m:
            break
        text = text[:m.start()] + ','.join([m.group(1)] * int(m.group(2))) + text[m.end():]
    out = []
    for tok in filter(None, text.split(',')):
        mk, n = tok.split(':')
        out.append((int(mk, 16), int(n)))
    return out


def route():
    legs, cur = [], None
    for line in open(ROUTE, encoding='utf-8'):
        if line.startswith('##'):
            m = re.match(r'##\s*(\S+)\s*(?:--\s*(.*))?$', line.strip())
            cur = {'name': m.group(1), 'note': (m.group(2) or '').strip(), 'text': ''}
            cur['oracle_only'] = 'oracle-only' in cur['note']
            legs.append(cur)
        elif cur is not None:
            cur['text'] += line
    for lg in legs:
        lg['pad'] = expand(lg['text'])
        lg['frames'] = sum(n for _, n in lg['pad'])
    return legs


def btnscript(pad):
    return ','.join('%X:%d' % (mk, n) for mk, n in pad)


def compact(pad):
    """The pad in the route's grammar with a repeated pair folded: `(100:2,0:18)*12`."""
    toks = ['%X:%d' % (mk, n) for mk, n in pad]
    out, i = [], 0
    while i < len(toks):
        n = 1
        while i + 2 * n + 1 < len(toks) and toks[i + 2 * n] == toks[i] and toks[i + 2 * n + 1] == toks[i + 1]:
            n += 1
        if n >= 3:
            out.append('(%s,%s)*%d' % (toks[i], toks[i + 1], n)); i += 2 * n
        else:
            out.append(toks[i]); i += 1
    return ','.join(out)


def walkscript(pad):
    # one frame later on the port (its keys are the cartridge's + 1); a pad of 0 is an idle token
    return ','.join(['.:1'] + ['H%X:%d' % (mk, n) if mk else '.:%d' % n for mk, n in pad])


def state_of(name):
    return os.path.join(STATES, name + '.state')


def start_of(legs, i):
    return 'boot' if i == 0 else state_of(legs[i - 1]['name'])


def key_of(legs, i):
    """What leg i's end state is made of: every pad from power-on through it, and the oracle that played them."""
    h = hashlib.sha1()
    for lg in legs[:i + 1]:
        h.update(btnscript(lg['pad']).encode() + b'|')
    st = os.stat(CT)
    h.update(('%d:%d' % (st.st_size, int(st.st_mtime))).encode())
    return h.hexdigest()


def ensure_states(legs, upto):
    """Make (or remake) the end states of legs 0..upto from power-on, wherever the route or the oracle changed."""
    for i in range(upto + 1):
        lg = legs[i]
        st, kf = state_of(lg['name']), state_of(lg['name']) + '.key'
        key = key_of(legs, i)
        if os.path.exists(st) and os.path.exists(kf) and open(kf).read().strip() == key:
            continue
        if not oracle_play(start_of(legs, i), lg['pad'], st, png=os.path.join(STATES, lg['name'] + '.png')):
            sys.exit('playthrough: the cartridge wrote no state for %s' % lg['name'])
        open(kf, 'w').write(key)
        print('playthrough: %s played on the cartridge (%d frames)' % (lg['name'], lg['frames']))


def route_bfs(w, s, g, z):
    """BFS over the location's tiles and the lead's plane, as C0/9C37 gates a step: a tile's plane is 0 (solid), 3 (any
    plane, and from 3 any tile) or 1/2 (the same plane); IGNORE_Z ($04) passes on planes 1/2 and keeps the plane; any
    other step sets the plane to the tile's (C0/9D54). A partial shape ($7E:30C0 >> 2 >= 2) has two halves, `b2 & 3` and
    `(b2 >> 3) & 3` with IGNORE_Z `$20` (C0/9AD3's walk and solid tails); either may be the one stepped on (the cartridge
    decides). Shape 1 is solid. A list of tiles from s to g, or None. (The Forest Maze, 11A, is crossed only so: its
    raised path's planes 1 and 2 meet at six plane-3 tiles.)"""
    from collections import deque

    def halves(x, y):
        if not (0 <= x < 64 and 0 <= y < 64):
            return []
        o = (y << 8) | x
        sh, b = (w[0x30C0 + o] >> 2) & 0x3F, w[0x7040 + o]
        if sh == 1:
            return []
        return [(b & 3, b & 4)] + ([((b >> 3) & 3, b & 0x20)] if sh >= 2 else [])
    z = z if z in (1, 2, 3) else 1
    start = (s[0], s[1], z)
    prev, q, end = {start: None}, deque([start]), None
    while q:
        c = q.popleft()
        if (c[0], c[1]) == tuple(g):
            end = c
            break
        for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = c[0] + d[0], c[1] + d[1]
            for tz, ig in halves(nx, ny) or ([(c[2], 0)] if (nx, ny) == tuple(g) else []):
                ok = c[2] != 3 if ig else (tz != 0 and (tz == 3 or tz == c[2] or c[2] == 3))
                n = (nx, ny, c[2] if ig else tz)
                if ok and n not in prev:
                    prev[n] = c
                    q.append(n)
    if end is None:
        return None
    out = []
    while end is not None:
        out.append(end[:2])
        end = prev[end]
    return out[::-1]


def bfs(ok, s, g):
    from collections import deque
    prev = {s: None}
    q = deque([s])
    while q:
        c = q.popleft()
        if c == g:
            break
        for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (c[0] + d[0], c[1] + d[1])
            if n not in prev and (ok(*n) or n == g):
                prev[n] = c
                q.append(n)
    if g not in prev:
        return None
    out, c = [], g
    while c is not None:
        out.append(c)
        c = prev[c]
    return out[::-1]


def route_walk(cur, pad, gx, gy, want_exit, cap):
    """From the state `cur` (advanced in place), follow a BFS over the location's tiles -- the cartridge's own planes,
    $7E:30C0 (shape: 1 = solid) and $7E:7040 (z = & 3) on the lead's plane $0155 -- a few tiles at a time (16 frames a
    tile), recomputed from where the lead stands after each stretch; when control is gone ($011F = 0: a visible enemy, a
    scene) the fight is played by tools/battle_pilot.py's Pilot (attack, a Tonic under 35%) and a scene's boxes take A
    until control is back; a stretch that leaves the lead where it stood is followed by a half-tile nudge
    across it (a partial tile's corner). Returns pad + the stretches, merged."""
    from battle_pilot import Pilot, in_fight
    pilot = Pilot()
    loc0, last, nudge, it, scene, waited = None, None, 0, 0, 0, 0
    while it < cap:
        w = wram_of(cur)
        loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
        if loc0 is None:
            loc0 = loc
        if loc != loc0:
            print('route: loc %03X reached after %d stretches' % (loc, it)); break
        lead = w[0x0197]
        tx, ty, z = w[0x1801 + lead], w[0x1881 + lead], w[0x0155]
        here = (w[0x1800 + lead] | (w[0x1801 + lead] << 8), w[0x1880 + lead] | (w[0x1881 + lead] << 8))
        if w[0x011F] == 0 and in_fight(w):           # a fight: the pilot (not counted against the cap)
            step, note = pilot.step(w)
            if note:
                print('route: fight: %s' % note)
            it -= 1
        elif w[0x011F] == 0 or w[0x0129]:            # a scene (or a fight's first frames): its boxes (box_step); never an
            scene += 1                               # A on a box that is not waiting -- it re-touches what the lead faces
            step, waited = box_step(w, waited)       # (the sanctum's organ, played again)
        elif (tx, ty) == (gx, gy) and not want_exit:
            print('route: at (%d,%d)' % (tx, ty)); break
        else:
            path = route_bfs(w, (tx, ty), (gx, gy), z)
            if not path:
                print('route: no tile path from (%d,%d) on plane %d' % (tx, ty, z)); break
            if len(path) == 1 and want_exit:       # on the goal tile: push on in the last direction
                step = [(pad[-1][0] if pad and pad[-1][0] in (0x10, 0x20, 0x40, 0x80) else 0x20, 16)]
            else:
                step = path_pad(path[:4])
            if here == last:                       # stuck: half a tile across the way it wanted to go
                d = step[0][0]
                side = (0x40, 0x80) if d in (0x10, 0x20) else (0x10, 0x20)
                step = [(side[nudge & 1], 8)] + step
                nudge += 1
        last = here if w[0x011F] else None
        if w[0x011F]:
            scene = 0
        pad = pad + step
        nxt = cur + '.n'
        oracle_play(cur, step, nxt)
        os.replace(nxt, cur)
        it += 1
    merged = []
    for mk, n in pad:
        if merged and merged[-1][0] == mk and mk != 0x100:
            merged[-1][1] += n
        else:
            merged.append([mk, n])
    return [tuple(x) for x in merged]


BOX_SLACK = 6                   # frames a box waits (state 7) before the press: room for a load a frame or two apart


def box_step(w, waited):
    """One step through a scene, keyed on the message box (C0/1F87, field_textbox_a_press.md): A only when the box waits
    for it -- `$0129` = 7 with `$0134` = FFFF (no auto-advance countdown), or a decision box (`$0162` != 0) -- and only
    after it has waited BOX_SLACK frames, so the press lands inside the wait on both sides when they are a frame or two
    apart (a fixed-cadence press that met a box on its first waiting frame missed it on the side a frame late, and the
    rest of the scene ran a box apart). Returns (pad, waited)."""
    waiting = w[0x0129] == 7 and ((w[0x0134] | (w[0x0135] << 8)) == 0xFFFF or w[0x0162] != 0)
    if not waiting:
        return [(0, 4)], 0
    if waited < BOX_SLACK:
        return [(0, 2)], waited + 2
    return [(0x100, 2), (0, 4)], 0


def scene_play(cur, pad, cap, quiet=60):
    """From the state `cur` (advanced in place), play a scene by its boxes (box_step) until the player has had control
    with no box for `quiet` frames, or `cap` frames. Returns pad + the scene's, merged."""
    waited, calm, n, step_pad = 0, 0, 0, []
    while n < cap:
        w = wram_of(cur)
        if w[0x011F] and w[0x0129] == 0:
            calm += 4
            if calm >= quiet:
                break
            step = [(0, 4)]
        else:
            calm = 0
            step, waited = box_step(w, waited)
        nxt = cur + '.n'
        oracle_play(cur, step, nxt)
        os.replace(nxt, cur)
        step_pad += step
        n += sum(k for _, k in step)
    merged = []
    for mk, k in pad + step_pad:
        if merged and merged[-1][0] == mk and mk != 0x100:
            merged[-1][1] += k
        else:
            merged.append([mk, k])
    return [tuple(x) for x in merged]


def path_pad(path):
    M = {(1, 0): 0x80, (-1, 0): 0x40, (0, 1): 0x20, (0, -1): 0x10}
    out = []
    for p0, p1 in zip(path, path[1:]):
        m = M[(p1[0] - p0[0], p1[1] - p0[1])]
        if out and out[-1][0] == m:
            out[-1] = (m, out[-1][1] + 16)
        else:
            out.append((m, 16))
    return out


def find(legs, name):
    for i, lg in enumerate(legs):
        if lg['name'] == name:
            return i
    sys.exit('playthrough: no leg %s in %s' % (name, ROUTE))


# --------------------------------------------------------------------------------------------------------- the cartridge
def oracle_play(start, pad, out_state, png=None):
    """Play pad from start ('boot' = power-on, erased cartridge); write the end state (and its picture)."""
    os.makedirs(os.path.dirname(out_state), exist_ok=True)
    os.makedirs(TMP, exist_ok=True)
    raw = os.path.join(TMP, 'end.raw')
    frames = sum(n for _, n in pad)
    subprocess.run([CT, '--peek', start, raw, '--frames', str(frames), '--btnscript', btnscript(pad) or '0:0'],
                   cwd=ROOT, env=dict(RULE, PEEK_SAVE=out_state), capture_output=True)
    if png:
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'ct.py'), 'fb2png', raw, png], cwd=ROOT,
                       capture_output=True)
    return os.path.exists(out_state)


def oracle_timeline(start, pad):
    frames = sum(n for _, n in pad)
    r = subprocess.run([CT, ROM, '--monitor', ','.join('%s:%d' % (a, n) for _, a, _, n in MON), '--from', start,
                        '--input', btnscript(pad), '--frames', str(frames), '--diff'], cwd=ROOT, env=RULE,
                       capture_output=True, text=True, errors='replace')
    out = []
    for line in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', line)
        if not m or int(m.group(1)) > frames:
            continue
        kv = dict(re.findall(r'\b([0-9A-F]{6})=([0-9A-F]+)', m.group(2)))
        if all(a in kv for _, a, _, _ in MON):
            out.append((int(m.group(1)), {k: kv[a] for k, a, _, _ in MON}))
    return out


# --------------------------------------------------------------------------------------------------------------- the port
def wram_of(state):
    raw = os.path.join(TMP, 'seed.raw')
    subprocess.run([CT, '--peek', state, raw, '--btnscript', '0:0'], cwd=ROOT, env=RULE, capture_output=True)
    return open(raw + '.wram', 'rb').read()


SHOTS = range(-6, 7)            # the port's frames shot around the leg's end: the picture's offset is reported, not assumed


def port_timeline(start, pad):
    """The port's graded bytes on change, and its pictures around the leg's last frame ({offset: rgb bytes})."""
    w = wram_of(start)
    lw = os.path.join(TMP, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + w)
    loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
    end = sum(n for _, n in pad)
    at = [end + k for k in SHOTS if end + k > 0]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=start, CT_WS_BATTLE_LEAD='1', CT_FIELD_MEMMON=','.join('%s:%d' % (p, n) for _, _, p, n in MON),
               CT_SHOTS=os.path.join(TMP, 'p'), CT_SHOTS_AT=','.join(map(str, at)), CT_SHOTS_EXIT='1')
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', str(loc), '--loadwram', lw,
                        '--walkscript', walkscript(pad)], cwd=ROOT, env=env, capture_output=True, text=True,
                       errors='replace')
    pics = {}
    for f in at:
        fn = os.path.join(TMP, 'p_f%d.raw' % f)
        if os.path.exists(fn):
            d = open(fn, 'rb').read()
            pw = struct.unpack('<I', d[:4])[0]
            pics[f - end] = b''.join(d[8 + y * pw * 3:8 + y * pw * 3 + 768] for y in range(224))
            os.remove(fn)
    out = []
    for line in r.stdout.splitlines():
        m = re.match(r'fmemmon f(\d+) loc=\S+\s+(.*)', line)
        if not m:
            continue
        kv = {k: v.rstrip('*') for k, v in re.findall(r'\b([0-9A-F]+)=([0-9A-F*]+)', m.group(2))}
        if all(p in kv for _, _, p, _ in MON):
            out.append((int(m.group(1)), {k: kv[p] for k, _, p, _ in MON}))
    return out, pics


def oracle_picture(start, pad):
    """The cartridge's picture on the leg's last frame (256x224 rgb)."""
    oracle_play(start, pad, os.path.join(TMP, 'cmp_end.state'))
    o = open(os.path.join(TMP, 'end.raw'), 'rb').read()
    px = bytearray()
    for y in range(224):
        r = (y + 8) * 1024
        for x in range(256):
            i = (r + x * 2) * 4
            px += bytes((o[i + 3], o[i + 2], o[i + 1]))
    return bytes(px)


def picture_grade(c, pics):
    """(offset, differing pixels) of the port picture closest to the cartridge's last frame."""
    best = None
    for k, p in sorted(pics.items(), key=lambda t: abs(t[0])):
        n = sum(1 for i in range(0, len(c), 3) if c[i:i + 3] != p[i:i + 3])
        if best is None or n < best[1]:
            best = (k, n)
    return best or (None, -1)


# ------------------------------------------------------------------------------------------------------------ the grade
def loc_of(v):
    return '%03X' % ((int(v[2:4] + v[0:2], 16)) & 0x1FF)


def chains(tl):
    """The distinct values of each graded byte, in order, with the frame each one appeared."""
    ch = {k: [] for k in GRADED}
    for f, kv in tl:
        for k in ch:
            if k == 'ctrl' and kv['bri'] == '00':
                continue
            v = loc_of(kv[k]) if k == 'loc' else kv[k]
            if not ch[k] or ch[k][-1][1] != v:
                ch[k].append((f, v))
    return ch


def le(v):
    """A little-endian byte string (the monitors print memory order) as a decimal number."""
    return str(int(''.join(v[i:i + 2] for i in range(len(v) - 2, -2, -2)), 16))


def fmt(ch, k):
    return ' '.join(v for _, v in ch[k])


def cmp_leg(legs, i, verbose):
    lg = legs[i]
    start = start_of(legs, i)
    if lg['oracle_only'] or start == 'boot':
        print('playthrough: %s oracle-only (%s)' % (lg['name'], lg['note']))
        return None
    if not os.path.exists(start):
        sys.exit('playthrough: %s has no start state (%s): run `play all` first' % (lg['name'], start))
    ptl, pics = port_timeline(start, lg['pad'])
    o, p = chains(oracle_timeline(start, lg['pad'])), chains(ptl)
    k, npx = picture_grade(oracle_picture(start, lg['pad']), pics)
    same = all([v for _, v in o[k2]] == [v for _, v in p[k2]] for k2 in o) and npx == 0
    print('playthrough: %s %s -- locs %s | story %s | party %s | gold %s | hp %s (%d) | ctrl %d | end picture %s' % (
        lg['name'], 'SAME' if same else 'DIFFERENT', fmt(o, 'loc'), fmt(o, 'story'), fmt(o, 'party'),
        le(o['gold'][-1][1]), le(o['hp'][-1][1]), len(o['hp']), len(o['ctrl']),
        '%d px at port %+d' % (npx, k) if k is not None else 'none'))
    if not same or verbose:
        for k2 in o:
            print('  %-5s cartridge %s' % (k2, ' '.join('%s@%d' % (v, f) for f, v in o[k2])))
            print('  %-5s port      %s' % (k2, ' '.join('%s@%d' % (v, f) for f, v in p[k2])))
    return same


# ------------------------------------------------------------------------------------------------------------- commands
def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    legs = route()
    if a[0] == 'try':
        i = find(legs, a[1])
        if i > 0:
            ensure_states(legs, i - 1)
        pad = expand(a[2])
        start = start_of(legs, i)
        out = os.path.join(TMP, 'try.state')
        oracle_play(start, pad, out, png=os.path.join(TMP, 'try.png'))
        for k, v in chains(oracle_timeline(start, pad)).items():
            print('%-5s %s' % (k, ' '.join('%s@%d' % (x, f) for f, x in v)))
        print('try: %d frames from %s -> %s (picture build/_playthrough/try.png)' % (sum(n for _, n in pad), start, out))
    elif a[0] == 'route':
        # route <leg> "<prefix pad>" <tile x> <tile y> [--exit] [--max N]: the prefix from the leg's start, then route_walk.
        i = find(legs, a[1])
        if i > 0:
            ensure_states(legs, i - 1)
        pad = expand(a[2]) or [(0, 1)]
        cur = os.path.join(TMP, 'route.state')
        oracle_play(start_of(legs, i), pad, cur)
        pad = route_walk(cur, pad, int(a[3]), int(a[4]), '--exit' in a,
                         int(a[a.index('--max') + 1]) if '--max' in a else 300)
        print('pad: %s' % compact(pad))
    elif a[0] == 'scene':
        # scene <leg> "<prefix pad>" [--max N]: the prefix from the leg's start, then the scene played by its boxes
        # (scene_play) until the player has control again; prints the whole pad.
        i = find(legs, a[1])
        if i > 0:
            ensure_states(legs, i - 1)
        pad = expand(a[2]) or [(0, 1)]
        cur = os.path.join(TMP, 'scene.state')
        oracle_play(start_of(legs, i), pad, cur)
        pad = scene_play(cur, pad, int(a[a.index('--max') + 1]) if '--max' in a else 30000)
        print('pad: %s' % compact(pad))
        print('scene: %d frames, end state %s' % (sum(n for _, n in pad), cur))
    elif a[0] == 'goto':
        # goto <state> <tile x> <tile y> <out.state> [--exit] [--max N]: route_walk from any state (authoring between
        # stretches); writes the end state, prints the stretch's pad.
        shutil.copyfile(a[1], a[4])
        pad = route_walk(a[4], [], int(a[2]), int(a[3]), '--exit' in a,
                         int(a[a.index('--max') + 1]) if '--max' in a else 300)
        print('pad: %s' % compact(pad))
    elif a[0] == 'walk':
        # walk <leg> "<prefix pad>" <tx> <ty> [--exit] [--lead=HEX]: the prefix from the leg's start, then tools/walkto.py
        # (the oracle's own collision, A* order) to the pixel or through the exit box; prints the whole pad.
        i = find(legs, a[1])
        if i > 0:
            ensure_states(legs, i - 1)
        pre = expand(a[2])
        tmp = os.path.join(TMP, 'walk_pre.state')
        oracle_play(start_of(legs, i), pre or [(0, 1)], tmp)
        extra = [x for x in a[5:] if x.startswith('--')]
        if not any(x.startswith('--lead') for x in extra) and '--ow' not in extra:
            rd = subprocess.run([CT, '--read', '7E0197+1', '--from', tmp], cwd=ROOT, env=RULE, capture_output=True,
                                text=True).stdout
            m = re.search(r'\] = ([0-9A-F]{2})', rd)
            if m:
                extra.append('--lead=' + m.group(1))        # $0197: the lead's object offset
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'walkto.py'), tmp, a[3], a[4],
                            os.path.join(TMP, 'walk_end.state'), '400', '--astar'] + extra, cwd=ROOT,
                           capture_output=True, text=True)
        print(r.stdout.strip().splitlines()[-3:] and '\n'.join(r.stdout.strip().splitlines()[-3:]))
        m = re.search(r'script: (\S+)', r.stdout)
        if m:
            print('pad: %s' % ','.join(filter(None, [btnscript(pre or [(0, 1)]), m.group(1)])))
    elif a[0] == 'diff':
        # diff <leg>: build/_playthrough/<leg>_diff.png -- the cartridge's last frame, the port's closest, the pixels apart
        i = find(legs, a[1])
        if i > 0:
            ensure_states(legs, i - 1)
        start = start_of(legs, i)
        _, pics = port_timeline(start, legs[i]['pad'])
        c = oracle_picture(start, legs[i]['pad'])
        k, n = picture_grade(c, pics)
        p = pics[k]
        rows = []
        for y in range(224):
            row = bytearray(b'\0')
            cr, pr = c[y * 768:(y + 1) * 768], p[y * 768:(y + 1) * 768]
            dr = bytearray()
            for x in range(0, 768, 3):
                dr += b'\xff\x00\x00' if cr[x:x + 3] != pr[x:x + 3] else bytes(v // 3 for v in cr[x:x + 3])
            row += cr + pr + dr
            rows.append(bytes(row))
        import zlib

        def chunk(t, d):
            return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
        out = os.path.join(TMP, '%s_diff.png' % legs[i]['name'])
        open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 768, 224, 8, 2, 0, 0, 0))
                              + chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))
        print('diff: %s %d px at port %+d -> %s' % (legs[i]['name'], n, k, out))
    elif a[0] == 'play':
        todo = range(len(legs)) if a[1] == 'all' else [find(legs, a[1])]
        for i in todo:
            lg = legs[i]
            ok = oracle_play(start_of(legs, i), lg['pad'], state_of(lg['name']),
                             png=os.path.join(STATES, lg['name'] + '.png'))
            if ok:
                open(state_of(lg['name']) + '.key', 'w').write(key_of(legs, i))
            print('play: %s %d frames -> %s' % (lg['name'], lg['frames'], 'ok' if ok else 'NO STATE'))
            if not ok:
                sys.exit(1)
    elif a[0] == 'cmp':
        todo = range(len(legs)) if a[1] == 'all' else [find(legs, a[1])]
        ensure_states(legs, max(todo) - 1) if max(todo) > 0 else None
        jobs = [int(x[2:] or os.cpu_count() or 1) for x in a if x.startswith('-j')]
        if jobs and jobs[0] > 1 and len(todo) > 1:
            # -jN: the legs graded N at a time, each its own `cmp <leg>` with its own scratch (PT_TMP), printed in route
            # order. The start states are made above, once; a child finds them keyed and current.
            from concurrent.futures import ThreadPoolExecutor
            def one(i):
                env = dict(os.environ, PT_TMP=os.path.join(TMP, 'j%d' % i))
                os.makedirs(env['PT_TMP'], exist_ok=True)
                return subprocess.run([sys.executable, os.path.abspath(__file__), 'cmp', legs[i]['name']] +
                                      (['-v'] if '-v' in a else []), cwd=ROOT, env=env, capture_output=True,
                                      text=True, errors='replace').stdout
            res = []
            with ThreadPoolExecutor(jobs[0]) as ex:
                for out in ex.map(one, todo):
                    m = re.search(r'^playthrough: (\d+)/(\d+) legs SAME$', out, re.M)
                    sys.stdout.write(out[:m.start()] if m else out); sys.stdout.flush()
                    res.append(None if not m or m.group(2) == '0' else m.group(1) == '1')
        else:
            res = [cmp_leg(legs, i, '-v' in a) for i in todo]
        graded = [r for r in res if r is not None]
        print('playthrough: %d/%d legs SAME' % (sum(graded), len(graded)))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
