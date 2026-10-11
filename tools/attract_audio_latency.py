#!/usr/bin/env python3
r"""The attract's sound against its picture: the output FIFO's backlog through the whole demo (the user, 2026-09-27: "in
the game's demo, the sound is falling behind after a certain point, probably the mode 7 race scene").

    python tools/attract_audio_latency.py [points stages end]

The optional arguments (all three: `360,8040,8640,9120,10740 8100-9025 10732`) replace the output frames below: the port's
numbering moves with the attract's clock (the rule's migration: the stages 60 frames earlier, the demo's end 85, under the
infinitely fast CPU), so the same moments of the demo are sampled.

The port's output plays one frame of SPC per displayed frame, 534 samples; every sample the SPC produces beyond that
waits in src/audio.c's FIFO, and a backlog that only grows is sound drifting behind the picture. CT_AUDIO_FIFOTRACE=60
prints the FIFO before each output frame's take (534 = one frame queued: the frame about to play, no latency) and the
SPC's cycle carry. A song load legitimately runs the SPC AHEAD (carry < 0: the frames it will then stand still for,
carry/32 samples); those samples play on the frames they belong to, so the backlog graded here is the FIFO less that
debt. Run: the interactive attract (--scene 433, CT_AUDIO_LOCKSTEP=1: one SPC frame per engine frame, the paced host's
relation) to f10800.

The user's second report the same day, "after the race scene, the curtains' sound is damaged", was the same
defect: the window wipe's whoosh (18 BA) is the cartridge's on all 15 curtains, alone (tools/sfx_voice_cmp.py: level
and noise rate), but before the fix it played 1.85 s after its picture, and the full FIFO (65536) threw away the
oldest samples when the 087 battle's JSLs pre-ran on top of it -- 841 samples lost by the demo's end, 396 in the race
(the stage ran more SPC than the DSP's 0x400 ring between two reads). Now: none.

Printed: the backlog at a few points (the title, before the race, after the race, after entry 3, the end), the largest
one over the two translated stages (the race f8150-8580, entry 3 f8760-9070) and the largest over the whole demo.

What was wrong (1.8 s behind the picture by the end): the race's logical-frame holds ran the APU (snes.c
g_ctLogicalHoldApu -- the zelda3 model runs one SPC frame per displayed frame, ct_field turns it off), the field's
mailbox kept posting through a translated stage (field.c), a stage's partial last frame stayed queued (audio.c's short
read), and the host paths that give the SPC time no displayed frame stands for (an unclocked handshake at a location
load, a clocked session's last opcode: ~100-250 samples a load) piled up. Those last ones are NOT debited from the SPC
(its state is the cartridge's: the song loads' handshakes and the stages are graded on it) -- the output DRAINS them
(audio.c: a frame takes 536 samples while more than a frame waits beyond the debt).
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
    env = dict(os.environ, CT_AUDIO_FIFOTRACE='60', CT_AUDIO_LOCKSTEP='1', CT_SHOTS=os.path.join('build', '_audlat'),
               CT_SHOTS_AT=str(END), CT_SHOTS_EXIT='1', SDL_AUDIODRIVER='dummy')
    r = subprocess.run([os.path.abspath('build/ct_field.exe'), '--assets', 'ct_assets.dat', '--scene', '433'],
                       env=env, capture_output=True, text=True, errors='replace')
    q, lost = {}, 0
    for l in (r.stdout + r.stderr).splitlines():
        m = re.match(r'audiofifo out(\d+) queued=(\d+) .*dropped=(\d+) carry=(-?[\d.]+)', l)
        if m:
            debt = int(-float(m.group(4)) / 32.0) if float(m.group(4)) < 0 else 0
            q[int(m.group(1))] = max(0, int(m.group(2)) - debt)
            lost = int(m.group(3))
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


if __name__ == '__main__':
    main()
