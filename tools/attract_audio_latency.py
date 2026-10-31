#!/usr/bin/env python3
r"""The attract's sound against its picture: the output FIFO's backlog through the whole demo.

    python tools/attract_audio_latency.py [points stages end]

The optional arguments (all three, e.g. `360,8040,8640,9120,10740 8100-9025 10732`) replace the output frames below; the
port's numbering moves with the attract's clock, so they keep the same moments of the demo sampled.

The port's output plays one frame of SPC per displayed frame, 534 samples; every sample the SPC produces beyond that
waits in src/audio.c's FIFO, and a backlog that only grows is sound drifting behind the picture. CT_AUDIO_FIFOTRACE=1
prints the FIFO before each output frame's take (534 = one frame queued: the frame about to play, no latency) and the
SPC's cycle carry. A song load legitimately runs the SPC ahead (carry < 0: the frames it will then stand still for,
carry/32 samples); those samples play on the frames they belong to, so the backlog graded here is the FIFO less that
debt. Run: the interactive attract (--scene 433, CT_AUDIO_LOCKSTEP=1: one SPC frame per engine frame, the paced host's
relation) to f10800.

Host paths that give the SPC time no displayed frame stands for (an unclocked handshake at a location load, a clocked
session's last opcode) are not debited from the SPC, whose state stays the cartridge's; the output drains them instead
(audio.c: a frame takes 536 samples while more than a frame waits beyond the debt). A stage's exit frame runs its
remaining SPC time (audio.c CtAudioStageExitFrame).

Printed: the backlog at a few points (the title, before the race, after the race, after entry 3, the end), the largest
one over the two stages (the race and entry 3) and the largest over the whole demo (all three on a 60-frame grid), the
samples lost (the DSP ring overrun or the FIFO full), and every output frame over the demo that came out more than 32
samples short of its 534 (the rest of it the last sample held, an audible gap).

Regression check: attract-sound-stays-with-the-picture.
"""
import os
import re
import subprocess
import sys

POINTS = (360, 8100, 8700, 9180, 10800)   # on CT_AUDIO_FIFOTRACE's 60-frame grid: before, between and after the stages, the end
STAGES = (8160, 9085)     # the race and entry 3, both whole
END = 10817


def main():
    global POINTS, STAGES, END
    if len(sys.argv) > 3:
        POINTS = tuple(int(v) for v in sys.argv[1].split(','))
        STAGES = tuple(int(v) for v in sys.argv[2].split('-'))
        END = int(sys.argv[3])
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    os.chdir(root)
    env = dict(os.environ, CT_AUDIO_FIFOTRACE='1', CT_AUDIO_LOCKSTEP='1', CT_SHOTS=os.path.join('build', '_audlat'),
               CT_SHOTS_AT=str(END), CT_SHOTS_EXIT='1', SDL_AUDIODRIVER='dummy')
    r = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '433'],
                       env=env, capture_output=True, text=True, errors='replace')
    q, lost, short = {}, 0, []
    for l in (r.stdout + r.stderr).splitlines():
        m = re.match(r'audiofifo out(\d+) queued=(\d+) .*dropped=(\d+) carry=(-?[\d.]+)', l)
        if m:
            o, n = int(m.group(1)), int(m.group(2))
            if 60 <= o <= END and n < 534 - 32:
                short.append((o, n))
            lost = int(m.group(3))
            if o % 60:
                continue                        # the backlog statistics use the 60-frame grid
            debt = int(-float(m.group(4)) / 32.0) if float(m.group(4)) < 0 else 0
            q[o] = max(0, n - debt)
    if not q:
        sys.exit('audio-latency: no audiofifo lines (no audio device?)')
    for p in POINTS:
        k = min(q, key=lambda o: abs(o - p))
        print('audio-latency: out%d %d samples behind the song loads (%.1f frames)' % (k, q[k], q[k] / 534.0))
    st = [v for o, v in q.items() if STAGES[0] <= o <= STAGES[1]]
    print('audio-latency: the two stages (out%d-%d): at most %d samples (%.1f frames)' % (STAGES[0], STAGES[1], max(st),
                                                                                          max(st) / 534.0))
    al = [v for o, v in q.items() if o >= 60]
    print('audio-latency: the whole demo (out60-%d): at most %d samples (%.1f frames)' % (END, max(al), max(al) / 534.0))
    print('audio-latency: samples lost over the demo (the DSP ring overrun or the FIFO full): %d' % lost)
    print('audio-latency: output frames more than 32 samples short (a held gap): %d%s' % (
        len(short), ''.join(' out%d=%d' % x for x in short)))


if __name__ == '__main__':
    main()
