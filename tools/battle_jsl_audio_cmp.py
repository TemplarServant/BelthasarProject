#!/usr/bin/env python3
r"""Does the music keep playing through the battle's two sound JSLs? The attract's demo battle, cartridge vs port.

    python tools/battle_jsl_audio_cmp.py

USER REPORT: "when battles start/end the music stutters". The battle's driver-mode posts (C0/1BAB at entry,
C0/1BE6 at the end -- on the attract's battle `70 01` and `70 00`) are SYNCHRONOUS `JSL $C7:0004`s: the field's CPU
waits in C7's handshake for 14 and 11 frames while the SPC keeps running, and the S-DSP keeps sounding. The port prices
that time on its loader clock and used to output the frames it had paid for as SILENCE (and to lose all but the last
two frames of the samples the DSP had emitted meanwhile). This grades the output STREAM on both machines -- the 534
DSP samples a frame, ct.exe --peek PEEK_PCM against ct_field CT_AUDIO_PCM -- over the 33 frames from each post:
  * SILENT frames (all zero): the old port's output on every frame the JSL had paid for;
  * CONTINUITY: a stream that advances by exactly its frame joins each frame to the last with a step no bigger than
    the steps inside a frame; one that re-reads the DSP's newest samples (the frame is not the NEXT 534) jumps at every
    boundary. The window's mean boundary step over its mean in-frame step is ~0.6-1.1 on the cartridge and on a
    continuous port; re-reading the ring gives 3.7. Continuous = below 2.
The timelines are aligned by the posts themselves (the cartridge's C0:1BDF / C0:1C89 writer frames, the port's
`[snd] field fN C0/1BDF` lines), not by a frame number.
"""
import os
import re
import struct
import subprocess
import sys

CT = os.path.abspath('build/ct.exe')
FIELD = os.path.abspath('build/ct_field.exe')
STATE = 'saves/attract_cena_inicial.state'
TMP = 'build/_jslaudio'
WINDOW = 33
FRAME = 534 * 2 * 2


def pcm_frames(path):
    d = open(path, 'rb').read()
    return [d[i * FRAME:(i + 1) * FRAME] for i in range(len(d) // FRAME)]


def window(frames, at):
    """(silent frames, boundary-step / in-frame-step ratio) over WINDOW frames from `at`. A SILENT frame is one that does
    not move: all zero, or each channel one held value (a DC hold is silence too -- 2026-09-27, audio.c's short read pads
    a frame with its last sample, and a stream starved to 1-2 samples a frame became 13 flat frames the all-zero test and
    the step ratio both passed)."""
    silent = 0
    bd = wd = 0
    nb = nw = 0
    for k in range(at, at + WINDOW):
        a = struct.unpack('<%dh' % (534 * 2), frames[k - 1])
        b = struct.unpack('<%dh' % (534 * 2), frames[k])
        if len(set(b[0::2])) == 1 and len(set(b[1::2])) == 1:
            silent += 1
        for ch in (0, 1):
            bd += abs(b[ch] - a[1066 + ch]); nb += 1
            for i in range(1, 534):
                wd += abs(b[2 * i + ch] - b[2 * i - 2 + ch]); nw += 1
    ratio = (bd / nb) / (wd / nw) if wd else 99.0
    return silent, ratio


def main():
    os.makedirs(TMP, exist_ok=True)
    # The cartridge: the posts' frames, then its stream.
    mon = subprocess.run([CT, '--monitor', '7E1E00:2', '--from', STATE, '--btnscript', '0:2300', '--frames', '2300', '--diff',
                          '--writer'], capture_output=True, text=True, timeout=900).stdout
    opost = {}
    for ln in mon.splitlines():
        m = re.match(r'\s*f(\d+)\s+7E1E00=70(0[01])\*\(C0:(1BDF|1C89)\)', ln)
        if m:
            opost[m.group(3)] = int(m.group(1))
    if set(opost) != {'1BDF', '1C89'}:
        sys.exit('jslaudio: REFUSING -- the cartridge run did not post both battle commands (%r)' % opost)
    opcm = os.path.join(TMP, 'o.pcm')
    subprocess.run([CT, '--peek', STATE, os.path.join(TMP, 'o.raw'), '--btnscript', '0:2300'],
                   env=dict(os.environ, PEEK_PCM=opcm), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)
    # The port: the interactive loop (a --selftest battle is fought synchronously, with no audio ticks), one SPC frame
    # per engine frame.
    ppcm = os.path.join(TMP, 'p.pcm')
    if os.path.exists(ppcm):
        os.remove(ppcm)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_PCM=ppcm, CT_SNDTRACE='1', CT_SHOTS=os.path.join(TMP, 'sh'), CT_SHOTS_AT='4332', CT_SHOTS_EXIT='1')
    out = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], env=env, capture_output=True, text=True,
                         timeout=900)
    ppost, nmis = {}, {}
    for ln in (out.stdout + out.stderr).splitlines():
        m = re.match(r'\[snd\] field f(\d+) C0/(1BDF|1C89) cmd=70', ln)
        if m:
            ppost[m.group(2)] = int(m.group(1))
        m = re.match(r'\[snd\] field C0/(1BDF|1C89) priced: \d+ master cycles, (\d+) NMI', ln)
        if m:
            nmis[m.group(1)] = int(m.group(2))
    if set(ppost) != {'1BDF', '1C89'}:
        sys.exit('jslaudio: REFUSING -- the port run did not post both battle commands (%r)' % ppost)
    of, pf = pcm_frames(opcm), pcm_frames(ppcm)
    ok = True
    verdict, detail = [], []
    for site, name in (('1BDF', 'start'), ('1C89', 'end')):
        osil, orat = window(of, opost[site] + 1)
        psil, prat = window(pf, ppost[site] + 1)
        def word(sil, rat):
            return 'silent' if sil else ('continuous' if rat < 2.0 else 'broken')
        ow, pw = word(osil, orat), word(psil, prat)
        ok = ok and ow == 'continuous' and pw == 'continuous'
        verdict.append('battle %s C0/%s: cartridge %s, port %s' % (name, site, ow, pw))
        # the price is the port's loader clock on the SPC's phase of the moment -- informative, not the grade
        detail.append('%s (%d NMIs priced): cartridge %d silent, step ratio %.2f; port %d silent, step ratio %.2f'
                      % (name, nmis.get(site, -1), osil, orat, psil, prat))
    print('jslaudio: %s -- over the %d frames after each post' % ('; '.join(verdict), WINDOW))
    print('jslaudio-detail: %s' % '; '.join(detail))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
