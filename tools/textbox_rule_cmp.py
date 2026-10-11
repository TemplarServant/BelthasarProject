#!/usr/bin/env python3
r"""The dialogue box against the cartridge UNDER THE RULE, every frame (ct-frame-alignment-p2: the logical oracle, the port's
frame P against the cartridge's thumbnail P + 2, the port's key one frame after the cartridge's).

    python tools/textbox_rule_cmp.py

Two drives whose picture the oracle draws right (a seed made by the ROM's own loader):
  * tent-keeper (saves/field_re/tent_keeper_lake.state / tent_keeper.lakewram, A at f11): a four-line box typed out;
  * naga (saves/field_re/m81_naga_after_fight_lake.state / m81_naga_after_fight.lakewram, A at f300): the Manoria
    cathedral after the Naga-ette fight -- 'Lucca: Pheew! That was close!' typed, then closed by the press; the room puts
    BG3 (its sunbeams) on the map's own lines and the box's text colours are the battle's (CGRAM 9-11 = 1483 4A51 7FFF).
Graded: the box's rows of every frame (thumbnails, a pixel differs when a channel differs by more than 24). What it pins:
the text typed by the typewriter is on screen in the frame the NMI that ends it uploads it (C0/EB79), in BG3 palette 2,
only on the lines HDMA channel 4 gives BG3 inside the box's lines. Two lines per drive -- the box's rows, then the whole picture (the cathedral's: Lucca, Crono and the Naga-ette under BG3's
sunbeams, which the room's sub-screen OBJ adds to them -- CtFieldSceneMainScreen):
    textbox-rule: <drive> <F> frames, <N> differ (<frames>)
    textbox-rule: <drive> whole picture <N> differ (<frames>)
The window is drawn as the PPU draws it (field.c drawCtWindowPpu): BG1's pixels in CGRAM's colours and HDMA channel 6's
per-line colour math (FD/D52D's records); the band's last line is the map's record, the window's raw colour. Every frame
of both drives is equal (until 2026-10-11 a baked bitmap left f25/26, f55/56, f302/303, .. differing at a ramp's ends).
"""
import os
import struct
import subprocess
import sys

sys.path = [p for p in sys.path if os.path.abspath(p or '.') != os.path.dirname(os.path.abspath(__file__))]
import numpy as np  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', '_tbrule')
REC = 6 + 128 * 112 * 3
DRIVES = [
    # name, oracle state, port seed, scene, press frame (oracle), box rows (thumbnail), frames
    ('tent-keeper', 'saves/field_re/tent_keeper_lake.state', 'saves/field_re/tent_keeper.lakewram', 5, 11, (65, 106), 160),
    ('naga', 'saves/field_re/m81_naga_after_fight_lake.state', 'saves/field_re/m81_naga_after_fight.lakewram', 129, 300,
     (65, 106), 760),
]


def thumbs(path):
    d = open(path, 'rb').read()
    out = {}
    for i in range(len(d) // REC):
        f, _ = struct.unpack('<IH', d[i * REC:i * REC + 6])
        out[f] = np.frombuffer(d[i * REC + 6:(i + 1) * REC], np.uint8).reshape(112, 128, 3).astype(np.int16)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, ost, seed, scene, press, rows, n in DRIVES:
        ot, pt = os.path.join(OUT, name + '_o.thumbs'), os.path.join(OUT, name + '_p.thumbs')
        for f in (ot, pt):
            if os.path.exists(f):
                os.remove(f)
        env = dict(os.environ, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1', PEEK_THUMBS=ot, PEEK_THUMBS_EVERY='1')
        subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), '--peek', ost, os.path.join(OUT, name + '_o.raw'),
                        '--btnscript', '0:%d,100:1,0:%d' % (press, n)], cwd=ROOT, env=env, capture_output=True)
        penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_SHOTS_THUMBS=pt,
                    CT_SHOTS_THUMBS_EVERY='1', CT_AUDIO_SEEDSTATE=ost)
        subprocess.run([os.path.join(ROOT, 'build', 'ct_field.exe'), '--assets', 'ct_assets.dat', '--scene', str(scene),
                        '--loadwram', seed, '--selftest', '--frames', str(n),
                        '--walkscript', 'n:%d,a:1,n:%d' % (press + 1, n - press - 2)], cwd=ROOT, env=penv,
                       capture_output=True)
        if not (os.path.exists(ot) and os.path.exists(pt)):
            print('textbox-rule: %s -- a side produced no frames' % name)
            continue
        o, p = thumbs(ot), thumbs(pt)
        y0, y1 = rows
        diff, whole, total = [], [], 0
        for f in range(1, n - 4):
            if f in p and f + 2 in o:
                total += 1
                d = np.abs(p[f] - o[f + 2]).max(axis=2) > 24
                if int(d[y0:y1].sum()):
                    diff.append(f)
                if int(d.sum()):
                    whole.append(f)
        for f in (ot, pt):
            os.remove(f)
        print('textbox-rule: %s %d frames, %d differ (%s)' % (name, total, len(diff), ' '.join(map(str, diff))))
        print('textbox-rule: %s whole picture %d differ (%s)' % (name, len(whole), ' '.join(map(str, whole))))


if __name__ == '__main__':
    main()
