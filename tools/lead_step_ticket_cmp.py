#!/usr/bin/env python3
r"""The lead walks only on the ticket his op B0 armed: three seeds of the Telepod capture, port against the rule's cartridge.

    python tools/lead_step_ticket_cmp.py

C0/881E computes the lead's step before the event pass; C0/A810 moves him after it, on `$1A01,X` (AB06 `BEQ`), and the
only arm of that ticket for a free lead is his own op B0: C0/3002 -> 3040 `LDA #01 / STA $1A01,X` (9E29 the velocity).
The port's host moves the lead before the tick; ct.exe vmLeadStepAtA810 takes the step back when no ticket stands.
Each case is the Telepod state (saves/field_re/scene8_pod_latch.state, made by tools/recipes/fxr_scene8_pod.json) with
WRAM poked on both sides (the cartridge's PEEK_POKE, the port's seed), Left held from the first frame:

  b2      Crono's $1180 on a `B2` (0473): C0/3543 `TYX / CLC / RTS`, no arm -- the cartridge never moves him.
  period4 Crono's $1000 = 04: the queue build (C0/59F0) enqueues him one frame in four -- one step every four frames.
  song    obj4 on obj11's `EA 18` (063D), queued mid-walk: the song upload parks the pass after Crono's B0 armed the step;
          C0/A810 integrates it on the frame the JSL returns, 39 frames later.

The state is the rule's oracle's own (PEEK_SAVE at a frame's end), so the cartridge's first --monitor frame runs an event
pass as the port's first tick does: the poked countdowns are the same on both sides, and the port's walk starts one
frame later ('.:1': its keys are the oracle's + 1, as every *_cmp tool here). (The owner's snes9x capture this replaced,
2026-10-22, was saved mid-frame: its first --monitor frame ran no pass, and the port's countdowns were poked one higher.)
The song case's countdown is 06: with 04, 05 or 07 the upload of song 18 ends one frame early on the port (38 frames
against the cartridge's 39) -- the song-load pricing's known residual (C7/0A12's cull priced flat), not this mechanism.
Per case:

    lead-step: <case> Crono's position identical on <k>/<n> frames (moved on <m> frames)
    lead-step: <k>/<n> cases identical
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, os.environ.get('CT_BUILD_DIR') or 'build', 'Belthasar.exe')
STATE = os.path.join(ROOT, 'saves', 'field_re', 'scene8_pod_latch.state')
OUT = os.path.join(ROOT, 'build', '_lead_step')
FRAMES = 70
REGS = '1806:2,1886:2'
# (name, cartridge pokes, port pokes) -- flat WRAM offsets
CASES = [('b2', '1186=73,1187=04', '1186=73,1187=04'),
         ('period4', '1006=04', '1006=04'),
         ('song', '1188=3D,1189=06,1009=06', '1188=3D,1189=06,1009=06')]


def parse(text, rx, norm):
    rows, cur = {}, {}
    for l in text.splitlines():
        m = rx.match(l)
        if not m:
            continue
        for k, v in re.findall(r'((?:7E)?[0-9A-F]{4})=([0-9A-F]+)\*?', l):
            cur[norm(k)] = v
        if '1806' in cur and '1886' in cur:
            rows[int(m.group(1))] = (cur['1806'], cur['1886'])
    out, last = {}, None
    for f in range(FRAMES):
        last = rows.get(f, last)
        out[f] = last
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    good = 0
    for name, cpoke, ppoke in CASES:
        rule = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_POKE=cpoke)
        mon = subprocess.run([CT, '--monitor', ','.join('7E' + r for r in REGS.split(',')), '--from', STATE,
                              '--input', '40:%d' % (FRAMES + 10), '--frames', str(FRAMES)],
                             cwd=ROOT, env=rule, capture_output=True, text=True, errors='replace').stdout
        cart = parse(mon, re.compile(r'\s+f(\d+)\s'), lambda k: k[-4:])
        raw = os.path.join(OUT, 'seed.raw')
        subprocess.run([CT, '--peek', STATE, raw, '--btnscript', '0:0'], cwd=ROOT,
                       env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True)
        w = bytearray(open(raw + '.wram', 'rb').read())
        for kv in ppoke.split(','):
            k, v = kv.split('=')
            w[int(k, 16)] = int(v, 16)
        lw = os.path.join(OUT, '%s.lakewram' % name)
        open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + bytes(w))
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
                   CT_AUDIO_SEEDSTATE=STATE, CT_FIELD_MEMMON=REGS)
        out = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '8', '--loadwram', lw, '--selftest',
                              os.path.join(OUT, '%s.raw' % name), '--frames', str(FRAMES + 10),
                              '--walkscript', '.:1,H40:%d' % (FRAMES + 10)],
                             cwd=ROOT, env=env, capture_output=True, text=True, errors='replace').stdout
        port = parse(out, re.compile(r'fmemmon f(\d+) '), lambda k: k)
        same = sum(1 for f in range(FRAMES) if cart[f] is not None and cart[f] == port[f])
        moved = sum(1 for f in range(1, FRAMES) if cart[f] != cart[f - 1])
        print("lead-step: %s Crono's position identical on %d/%d frames (moved on %d frames)" % (name, same, FRAMES, moved))
        good += same == FRAMES
    print('lead-step: %d/%d cases identical' % (good, len(CASES)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
