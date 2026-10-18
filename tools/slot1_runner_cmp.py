#!/usr/bin/env python3
r"""C0/595C -- obj0's SLOT 1 run to its terminator -- at the battle return and the menu exit, against the cartridge.

    python tools/slot1_runner_cmp.py

The ROM calls C0/595C from three places, each right after the $7F:2000 script image is (re)built: the location load
(C0/56C8), the menu-exit reload chain (C0/0206) and the battle-return tail (C0/0330). The port ran only the load's.
disasm/notes/obj0_slot1_runner.md is the dossier.

For each scenario, both sides run live:
  cartridge  build/ct.exe --lifetrace (opcode-stepped) with --pcwatch on the call site, C0/595C, the dispatch
             C0/5977 (X = op*2, Y = the live IP) and C0/597C (the terminator), and --wwatch on the mailbox
             $1E00-$1E03, obj0's $1180/$1C00 and the object count $7F:2000 (live IP = body + count*32)
  port       build/Belthasar.exe with CT_SLOT1TRACE=1 (`slot1: <site> ops OP@ADDR ... end@ADDR obj0 ...`) and
             CT_SNDTRACE=1 (every JSL $C7:0004, field-side and VM-side, in order on stderr)
Graded, per scenario:
  trail    the ops the runner dispatched, in order, and where it stopped -- identical
  obj0     the cartridge's runner writes neither $1180 nor $1C00 (writes=0); the port's obj0 comes out as it went in
  mailbox  (battles) the posts from the first C0/1BE6 driver-mode post through the first post after the runner --
           cartridge and port, same commands in the same order
"""
import concurrent.futures
import os
import re
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')

MASH = ','.join(['100:20,0:40'] * 45)
SCENARIOS = [
    dict(label='0AD battle', site='C0/0330',
         oracle=['--from', 'saves/attract_loc0AD_flea.state', '--frames', '330'],
         port=['--assets', 'ct_assets.dat', '--scene', '433'],
         penv=dict(CT_AUDIO_FORCE='1', CT_AUDIO_PROBE='4420'), mailbox=True),   # 4382 until 2026-09-16: the return now holds the VM (battle-return)
    dict(label='082 battle', site='C0/0330',
         oracle=['--from', 'saves/battle_re/party3_field.state', '--btnscript', '40:150,' + MASH + ',0:250'],
         port=['--assets', 'ct_assets.dat', '--scene', '130', '--loadwram', 'saves/battle_re/party3_field.lakewram',
               '--walkscript', 'l:150'],
         penv=dict(CT_BATTLE_MASHA='1', CT_RETURN_PROBE_AT='120'), mailbox=True),
    dict(label='082 menu', site='C0/0206',
         oracle=['--from', 'saves/grupo_3_mapa.state', '--btnscript', '0:20,200:2,0:90,1:2,0:150'],
         port=['--assets', 'ct_assets.dat', '--scene', '130', '--menutest', 'saves/battle_re/party3_form.raw.wram'],
         penv={}, mailbox=False),
]

BRACKET = (0x1BE6, 0x1CF6)          # C0/1BE6's arms; a write's reported PC is the next instruction


def run_oracle(sc):
    args = [CT, '--lifetrace'] + sc['oracle'] + [
        '--wwatch', '7E1E00:4,7E1180:2,7E1C00:1,7F2000:1',
        '--pcwatch', '%s,C0/595C,C0/5977,C0/597C' % sc['site']]
    r = subprocess.run(args, capture_output=True, text=True, errors='replace', timeout=900)
    return r.stdout


def run_port(sc):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_SLOT1TRACE='1',
               CT_SNDTRACE='1', **sc['penv'])
    r = subprocess.run([FIELD] + sc['port'], env=env, capture_output=True, text=True, errors='replace', timeout=900)
    return r.stderr


def parse_oracle(text, site):
    mail = [0, 0, 0, 0]
    posts = []                       # (cmd, p1, seq, pc)
    count = None
    armed = inrun = False
    trail, end, obj0w, done = [], None, 0, False
    run_seq = None
    seq = 0
    for line in text.splitlines():
        seq += 1
        m = re.match(r'--- f\d+ END .*7F2000=([0-9A-F]{2})', line)
        if m and count is None and done:
            count = int(m.group(1), 16)
        m = re.match(r'\s+W (?:7E|00)(1E0[0-3]|1180|1181|1C00) <- ([0-9A-F]{2}) @([0-9A-F]{2})/([0-9A-F]{4})', line)
        if m:
            reg, val, bank, pc = m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)
            if reg.startswith('1E0'):
                mail[int(reg[3])] = val
                if reg == '1E00' and val and bank != 0xC7:
                    posts.append((val, mail[1], seq, (bank << 16) | pc))
            elif inrun:
                obj0w += 1
            continue
        m = re.match(r'\s+X ([0-9A-F]{2})/([0-9A-F]{4}) A=([0-9A-F]{4}) X=([0-9A-F]{4}) Y=([0-9A-F]{4})', line)
        if not m or done:
            continue
        pc = '%s/%s' % (m.group(1), m.group(2))
        x, y = int(m.group(4), 16), int(m.group(5), 16)
        if pc == site:
            armed = True
        elif pc == 'C0/595C' and armed:
            inrun = True
        elif pc == 'C0/5977' and inrun:
            trail.append((x >> 1, y))
        elif pc == 'C0/597C' and inrun:
            end, inrun, done, run_seq = x, False, True, seq
        else:
            armed = False if pc != 'C0/5977' else armed
    return dict(trail=trail, end=end, obj0w=obj0w, count=count, posts=posts, run_seq=run_seq)


def parse_port(text, site):
    posts, slot1, seq = [], None, 0
    for line in text.splitlines():
        seq += 1
        m = re.match(r'\[snd\] field (?:f\d+ )?C0/([0-9A-F]{4}) cmd=([0-9A-F]{2}) p1=([0-9A-F]{2})', line)
        if m:
            cmd = int(m.group(2), 16)
            if cmd:
                posts.append((cmd, int(m.group(3), 16), seq, 0xC00000 | int(m.group(1), 16)))
            continue
        m = re.match(r'\[snd\] blen=.*cmd=([0-9A-F]{2}) p1=([0-9A-F]{2})', line)
        if m:
            cmd = int(m.group(1), 16)
            if cmd:
                posts.append((cmd, int(m.group(2), 16), seq, -1))
            continue
        if slot1 is None and line.startswith('slot1: %s ops' % site):
            slot1 = (line, seq)
    return posts, slot1


def bracket_start(posts):
    for i, p in enumerate(posts):
        if p[3] >= 0 and (p[3] >> 16) == 0xC0 and BRACKET[0] <= (p[3] & 0xFFFF) <= BRACKET[1]:
            return i
    return None


def fmt_posts(ps):
    return ' '.join('%02X:%02X' % (c, p) for c, p, _, _ in ps) or '-'


def grade(sc, otext, ptext):
    tag = 'slot1-cmp: %s %s' % (sc['label'], sc['site'])
    o = parse_oracle(otext, sc['site'])
    if o['end'] is None or o['count'] is None:
        print('%s FAIL -- the cartridge never ran C0/595C from %s' % (tag, sc['site']))
        return
    hdr = o['count'] * 32
    otrail = ' '.join('%02X@%04X' % (op, (y - hdr) & 0xFFFF) for op, y in o['trail'])
    print('%s cartridge ops %s end@%04X obj0-writes=%d' % (tag, otrail, (o['end'] - hdr) & 0xFFFF, o['obj0w']))
    posts, slot1 = parse_port(ptext, sc['site'])
    if slot1 is None:
        print('%s port FAIL -- no `slot1: %s` line' % (tag, sc['site']))
        return
    m = re.match(r'slot1: \S+ ops ?(.*?) ?(\w+)@([0-9A-F]{4}) obj0 @([0-9A-F]{4}) p(\d+) -> @([0-9A-F]{4}) p(\d+)$',
                 slot1[0])
    if not m:
        print('%s port FAIL -- unreadable: %s' % (tag, slot1[0]))
        return
    kept = (m.group(4), m.group(5)) == (m.group(6), m.group(7))
    print('%s port      ops %s %s@%s obj0 kept=%d' % (tag, m.group(1), m.group(2), m.group(3), 1 if kept else 0))
    same = m.group(1) == otrail and m.group(2) == 'end' and int(m.group(3), 16) == ((o['end'] - hdr) & 0xFFFF)
    print('%s trail identical=%d obj0 unchanged=%d' % (tag, 1 if same else 0, 1 if (kept and o['obj0w'] == 0) else 0))
    if not sc['mailbox']:
        return
    ob = bracket_start(o['posts'])
    pb = bracket_start(posts)
    if ob is None or pb is None:
        print('%s mailbox FAIL -- no C0/1BE6 post (cartridge %s, port %s)' % (tag, ob, pb))
        return
    ow = []
    for p in o['posts'][ob:]:
        ow.append(p)
        if p[2] > o['run_seq']:
            break
    pw = posts[pb:pb + len(ow)]
    same = [(c, p) for c, p, _, _ in ow] == [(c, p) for c, p, _, _ in pw]
    print('%s mailbox cartridge %s | port %s | identical=%d' % (tag, fmt_posts(ow), fmt_posts(pw), 1 if same else 0))


def main():
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
        futs = [(sc, ex.submit(run_oracle, sc), ex.submit(run_port, sc)) for sc in SCENARIOS]
        for sc, fo, fp in futs:
            grade(sc, fo.result(), fp.result())


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
