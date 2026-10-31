#!/usr/bin/env python3
r"""One SFX's sound, alone, on the cartridge and on the port (by default the window wipe's whoosh after the race,
`18 BA`, keyed on voices 6/7 with sources 02/07 then 04).

    python tools/sfx_voice_cmp.py [--mask C0] [--kon "val=C0 v6:srcn=02 v7:srcn=07"] [--window 16000]
                                  [--oframes 7400] [--pframes 9300]

Both sides run the attract with CT_DSP_VOICEMASK (snes/dsp.c: only the masked voices reach the mix and the echo input)
and CT_DSP_RAWPCM (every sample the DSP makes, in order) and CT_DSP_KONTRACE (each key-on stamped with the DSP's own
sample count). Each key-on matching --kon is one occurrence of the sound; the i-th on the cartridge is paired with the
i-th on the port, and the --window samples from each key-on are cut out of each side's raw stream and compared: peak,
RMS, the samples identical, the largest difference, and where the first difference falls. The cartridge: ct.exe --peek
from saves/attract_cena_inicial_rule.state under the rule; the port: Belthasar --scene 433 under CT_AUDIO_LOCKSTEP.
"""
import argparse
import os
import re
import struct
import subprocess

CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/Belthasar.exe')
STATE = 'saves/attract_cena_inicial_rule.state'   # power-on + 2159 under the rule (tools/fixtures/attract_rule.json)
TMP = os.path.join('build', '_sfxvoice')


def cut(path, start, n):
    with open(path, 'rb') as f:
        f.seek(start * 4)
        d = f.read(n * 4)
    return struct.unpack('<%dh' % (len(d) // 2), d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mask', default='C0')
    ap.add_argument('--kon', default='val=C0 v6:srcn=02 v7:srcn=07')
    ap.add_argument('--window', type=int, default=16000)
    ap.add_argument('--oframes', type=int, default=7400)
    ap.add_argument('--pframes', type=int, default=9300)
    a = ap.parse_args()
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
    os.chdir(root)
    os.makedirs(TMP, exist_ok=True)
    oraw, praw = os.path.join(TMP, 'o.raw16'), os.path.join(TMP, 'p.raw16')
    env = dict(os.environ, CT_DSP_VOICEMASK=a.mask, CT_DSP_KONTRACE='1')
    o = subprocess.Popen([CT, '--peek', STATE, os.path.join(TMP, 'o.raw'), '--btnscript', '0:%d' % a.oframes],
                         env=dict(env, CT_DSP_RAWPCM=oraw, CT_ORACLE_LOGICAL='1', CT_ORACLE_VIRTUAL_BEAM='1'), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                         errors='replace')
    p = subprocess.Popen([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'],
                         env=dict(env, CT_DSP_RAWPCM=praw, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                                  CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1', CT_SHOTS=os.path.join(TMP, 'sh'),
                                  CT_SHOTS_AT=str(a.pframes), CT_SHOTS_EXIT='1', CT_SNDTRACE='1'),
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    otxt, _ = o.communicate(timeout=1500)
    ptxt, _ = p.communicate(timeout=1500)

    def kons(txt):
        out = []
        for l in txt.splitlines():
            m = re.match(r'dspkon s=(\d+) f=(\d+) (.*)', l)
            if m and m.group(3).startswith(a.kon):
                out.append((int(m.group(1)) - 1, int(m.group(2))))
        return out
    ok, pk = kons(otxt), kons(ptxt)
    print('sfxvoice: mask %s, "%s" -- cartridge %d key-ons to f%d, port %d to f%d' % (a.mask, a.kon, len(ok), a.oframes,
                                                                                    len(pk), a.pframes))
    for i in range(min(len(ok), len(pk))):
        (os_, of), (ps, pf) = ok[i], pk[i]
        ow, pw = cut(oraw, os_, a.window), cut(praw, ps, a.window)
        n = min(len(ow), len(pw))
        same = sum(1 for k in range(n) if ow[k] == pw[k])
        md = max((abs(ow[k] - pw[k]) for k in range(n)), default=0)
        first = next((k // 2 for k in range(n) if ow[k] != pw[k]), None)
        orms = (sum(x * x for x in ow[:n]) / max(1, n)) ** 0.5
        prms = (sum(x * x for x in pw[:n]) / max(1, n)) ** 0.5
        def zc(w):   # zero crossings of the left channel per 1000 samples: the noise clock's rate, whatever its phase
            l = w[0::2]
            return 1000.0 * sum(1 for k in range(1, len(l)) if (l[k] >= 0) != (l[k - 1] >= 0)) / max(1, len(l))
        print('sfxvoice #%d: cart f%d port f%d -- rms %.0f / %.0f, peak %d / %d, zero-crossings %.0f / %.0f per 1000, '
              '%d/%d identical, max diff %d, first diff at sample %s'
              % (i, of, pf, orms, prms, max(map(abs, ow[:n]), default=0), max(map(abs, pw[:n]), default=0), zc(ow[:n]),
                 zc(pw[:n]), same, n, md, first))


if __name__ == '__main__':
    main()
