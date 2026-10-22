#!/usr/bin/env python3
r"""The title's first pendulum tick against the title's picture, port against cartridge, on every pass.

    python tools/attract_title_sound_sync_cmp.py

The user, 2026-09-27: "after the demo, when the pendulum repeats, apparently the sound is still out of sync". The title's
setup posts its song ($3775 `JSL $C7:0004`, synchronous: the loader keeps the CPU until the driver has it) and spins on
$2143 ($5AAA), both with the NMI off, so the pendulum starts when they end -- and how long the upload takes depends on
what the driver already holds (power-on 35 + 6 frames, after the demo 15 + 6). The port held a fixed 43 (the power-on
count) and the second title's tick came ~20 frames before its pendulum; it now holds the loader clock's price of the
post plus the spin (src/field.c, the title's opening).

Port: the interactive attract (--scene 433, CT_AUDIO_LOCKSTEP=1): CT_DSP_KONTRACE's key-ons at their own sample frame
(from the first presented frame) and CT_SHOTS_LUMA's picture: for each title, the
frame of the first tick (`02 v1:srcn=25`) less the first lit frame (mean > 3). Cartridge: the attract state's second
title (the demo's end; the state begins after the power-on one): a state 8950 frames in (PEEK_SAVE), then its key-ons
(CT_DSP_KONTRACE, 534 samples a frame) and thumbnails (PEEK_THUMBS) over 400 frames.
"""
import os
import re
import struct
import subprocess
import sys

FIELD = os.path.abspath('build/Belthasar.exe')
CT = os.path.abspath('build/ct.exe')
STATE = 'saves/attract_cena_inicial.state'
TICK = '02 v1:srcn=25'
LIT = 3


def main():
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    tmp = os.path.join('build', '_titlesync')
    os.makedirs(tmp, exist_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_DSP_KONTRACE='1', CT_AUDIO_FIFOTRACE='1', CT_SHOTS_LUMA='1', CT_SHOTS=os.path.join(tmp, 'p'),
               CT_SHOTS_AT='11200', CT_SHOTS_EXIT='1')
    pr = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], env=env, capture_output=True, text=True,
                        errors='replace')
    # The tick: the key-on's own sample frame (`dspkon s=.. f=..`, as on the cartridge's side), counted from the first
    # presented frame. Since 404971a the title's opening runs its SPC time as one priced stretch -- a single slice queues
    # the whole upload's output, played over the muted frames after it -- so the output index a key-on is generated in
    # is not the frame it is heard on. The power-on run's frames start below 0 (f-91): the patterns take a sign.
    ticks, lum, titles = [], {}, []
    for l in pr.stderr.splitlines():
        m = re.search(r'dspkon s=\d+ f=(\d+) ', l)
        if m and TICK in l:
            ticks.append(int(m.group(1)))
    for l in pr.stdout.splitlines():
        m = re.match(r'shotluma: f(-?\d+) loc=\S+ \S+ mean=(\d+)', l)
        if m:
            lum[int(m.group(1))] = int(m.group(2))
        m = re.search(r'op-0xFF 02 -> JML \$C3:0000 .* \(at f(-?\d+)\)', l)
        if m:
            titles.append(int(m.group(1)))
    f0 = min(lum) if lum else 0
    ticks = [t + f0 for t in ticks]
    res = []
    for t0 in titles:
        lit = next((f for f in range(t0, t0 + 200) if lum.get(f, 0) > LIT), None)
        tick = next((f for f in ticks if t0 <= f < t0 + 200), None)
        if lit is None or tick is None:
            sys.exit('title-sync: REFUSING -- the port title at f%d has no %s' % (t0, 'picture' if lit is None else 'tick'))
        res.append((t0, tick, lit))
    st = os.path.join(tmp, 'c8950.state')
    subprocess.run([CT, '--peek', STATE, os.path.join(tmp, 'c8950.raw'), '--frames', '8950'],
                   env=dict(os.environ, PEEK_SAVE=st), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    th = os.path.join(tmp, 'cart.thumbs')
    if os.path.exists(th):
        os.remove(th)
    cr = subprocess.run([CT, '--peek', st, os.path.join(tmp, 'c9350.raw'), '--frames', '400'],
                        env=dict(os.environ, CT_DSP_KONTRACE='1', PEEK_THUMBS=th, PEEK_THUMBS_EVERY='1'),
                        capture_output=True, text=True, errors='replace')
    ctick = next((int(m.group(1)) for m in re.finditer(r'dspkon s=\d+ f=(\d+) val=([0-9A-F]+)((?: v\d:srcn=[0-9A-F]+)*)',
                                                       cr.stdout + cr.stderr)
                  if (m.group(2) + m.group(3)).startswith(TICK)), None)
    clit = None
    if os.path.exists(th):
        d = open(th, 'rb').read(); rec = 4 + 2 + 112 * 128 * 3
        for i in range(0, len(d) - rec + 1, rec):
            f = struct.unpack('<I', d[i:i + 4])[0]; img = d[i + 6:i + rec]
            if sum(img) / len(img) > LIT:
                clit = f; break
    if ctick is None or clit is None:
        sys.exit('title-sync: REFUSING -- the cartridge title has no %s' % ('tick' if ctick is None else 'picture'))
    print('title-sync: cartridge, the title after the demo: tick c%d, picture c%d -> tick - picture = %d' %
          (8950 + ctick, 8950 + clit, ctick - clit))
    for k, (t0, tick, lit) in enumerate(res):
        print('title-sync: port title %d (opened f%d): tick f%d, picture f%d -> tick - picture = %d' %
              (k + 1, t0, tick, lit, tick - lit))


if __name__ == '__main__':
    main()
