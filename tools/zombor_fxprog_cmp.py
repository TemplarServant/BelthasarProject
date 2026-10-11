#!/usr/bin/env python3
r"""The attract Zombor's EFFECT PROGRAM (bank D1, program 4D at D1/74F8 and its continuation at D1/A208) and what it drives,
against the cartridge, frame by frame -- op AA (CD/1C32: the slot's anchor = minus bound point k), the objslot record
service's mode 4 (FD/E82C: the fade toward white, sub-mode 0 = FD/E8D7), op BF's wait on the records, and the script's
screen-shake requests (`80 45 ..` = D1/F6B4, `80 51` = D1/F692).

    python tools/zombor_fxprog_cmp.py

  * cartridge: build/_z_f7085.state (the 087 leg's start; tools/zombor_orbit_cmp.py makes it), `ct.exe --monitor` of the
    slot ($CA32 active, $CA35 pointer, $CA5A..$CA61 anchor + offset), the records $0580..$05AF, $CD3C and the shake bytes
    $CCB0..$CCC0 over 800 frames (cached as build/_z_orc_fxprog2.txt);
  * port: saves/attract_zombor_pre.lakewram under CT_AUDIO_LOCKSTEP (the battle boot priced as the cartridge's) with
    CT_BATTLE_SHAKETRACE=1 (the same bytes as the frame leaves the objslot service).

Three alignments, each on its own event so the script's timing residual does not mask a mechanism:
  1. the program from op AA (the first frame the anchor reads FF78): slot, pointer and anchor per frame until the slot ends;
  2. the two fade records from the frame they go live (mode 40): the twelve bytes of each per frame until both are dead;
  3. the shake arm: $CCB0..$CCC0 on the first frame $CCC0 is set, and the frames from the arm to the stop on each side.
"""
import os
import re
import subprocess

CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/ct_field.exe')
SRC = 'saves/attract_cena_inicial.state'
FIX = 'saves/attract_zombor_pre.lakewram'
STATE = 'build/_z_f7085.state'
ORC = 'build/_z_orc_fxprog2.txt'
MON = '7ECA32:1,7ECA35:2,7ECA5A:8,7E0580:48,7ECD3C:1,7ECCB0:17'


def run(argv, env=None, timeout=1200):
    return subprocess.run(argv, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout).stdout


def rows(text, pat):
    out = {}
    for ln in text.splitlines():
        m = re.match(pat, ln)
        if not m: continue
        d = dict(re.findall(r'(?:7E)?([0-9A-F]{4})=([0-9A-F]+)', m.group(2)))
        if 'CA32' in d and '0580' in d and 'CCB0' in d:
            out[int(m.group(1))] = d
    return out


def first(r, pred, after=0):
    for f in sorted(r):
        if f >= after and pred(r[f]): return f
    return None


def main():
    if not os.path.exists(SRC) or not os.path.exists(FIX):
        print('zombor-fxprog: SKIP -- missing %s or %s' % (SRC, FIX)); return
    if not os.path.exists(STATE):
        run([CT, '--peek', SRC, 'build/_ztmp.raw', '--frames', '7085'], env=dict(os.environ, PEEK_SAVE=STATE))
    if not os.path.exists(ORC):
        open(ORC, 'w').write(run([CT, '--monitor', MON, '--from', STATE, '--frames', '800']))
    o = rows(open(ORC).read(), r'\s*f(\d+)\s+(.*)')
    pt = run([FIELD, '--assets', 'ct_assets.dat', '--scene', '135', '--loadwram', FIX, '--frames', '700'],
             env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                      CT_BATTLE_SHAKETRACE='1', CT_SHOTS='build/_zfx', CT_SHOTS_AT='700', CT_SHOTS_EXIT='1'))
    p = rows(pt, r'shake f(\d+) (.*)')
    # 1. the program from op AA
    fo, fp = first(o, lambda d: d['CA5A'][:4] == '78FF'), first(p, lambda d: d['CA5A'][:4] == '78FF')
    if fo is None or fp is None:
        print('zombor-fxprog: FAIL -- op AA never ran (cartridge %s, port %s)' % (fo, fp)); return
    n = same = 0; ended = False
    key = lambda d: (d['CA32'], d['CA35'], d['CA5A'])
    while (fo + n) in o and (fp + n) in p:
        a, b = o[fo + n], p[fp + n]
        same += key(a) == key(b)
        if os.environ.get('FXPROG_DEBUG') and key(a) != key(b): print('  +%d cart %s | port %s' % (n, key(a), key(b)))
        n += 1
        if a['CA32'] == '00' and b['CA32'] == '00': ended = True; break
    print('zombor-fxprog: program 4D from op AA (cartridge f%d / port f%d), %d frames: slot, pointer, anchor + offset %d identical; '
          'the slot %s' % (fo, fp, n, same, 'ends on both sides' if ended else 'does NOT end on both sides'))
    # 2. the fade records (objslot mode 4) from their own start
    ro, rp = first(o, lambda d: d['0580'][:2] == '40', 600), first(p, lambda d: d['0580'][:2] == '40', 300)
    if ro is None or rp is None:
        print('zombor-fxprog: FAIL -- no mode-4 fade record (cartridge %s, port %s)' % (ro, rp)); return
    n = same = 0; dead = False
    while (ro + n) in o and (rp + n) in p:
        a, b = o[ro + n]['0580'][:48], p[rp + n]['0580'][:48]
        same += a == b
        n += 1
        if a[:2] == '00' and a[24:26] == '00' and b[:2] == '00' and b[24:26] == '00': dead = True; break
    print('zombor-fxprog: fade records 8/9 (objslot mode 4, FD/E82C) from their start, %d frames: %d identical; %s' % (
        n, same, 'both dead' if dead else 'NOT both dead'))
    # 3. the shake request
    so, sp = first(o, lambda d: d['CCB0'][-2:] != '00', 600), first(p, lambda d: d['CCB0'][-2:] != '00', 300)
    if so is None or sp is None:
        print('zombor-fxprog: FAIL -- no shake arm (cartridge %s, port %s)' % (so, sp)); return
    eo, ep = first(o, lambda d: d['CCB0'][-2:] == '00', so), first(p, lambda d: d['CCB0'][-2:] == '00', sp)
    # $CCB0..$CCC0 on the arm frame, without the two random bytes $CCB1/$CCB9 (the shake's own, graded by --sweepshake)
    arm = lambda d: [d['CCB0'][2 * i:2 * i + 2] for i in range(17) if i not in (1, 9)]
    print('zombor-fxprog: shake arm (80 45 ..) $CCB0..$CCC0 %s; stop (80 51) %s frames after it on the cartridge, %s on the port' % (
        'identical' if arm(o[so]) == arm(p[sp]) else 'DIFFERENT (cartridge %s, port %s)' % (arm(o[so]), arm(p[sp])),
        (eo - so) if eo else '?', (ep - sp) if ep else '?'))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
