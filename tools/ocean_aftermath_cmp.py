#!/usr/bin/env python3
r"""The Ocean Palace's aftermath, port against the rule's cartridge: Lavos rising to the party's new leader.

    python tools/ocean_aftermath_cmp.py

From saves/field_re/pre_scene_19f.state (the user's own snes9x capture before the Mammon Machine scene, spliced into a lake
state at loc 19F, storyline CB), no input to f6000, then A every 40 frames to f9600 (the 10F box and the 1A9 scene), on both
sides. The cartridge runs 19F -> 1F4 (Lavos rises) -> 1F5 -> 1EE -> 1F5 -> 165 -> 1F4 -> 182 (the Last Village) -> 1ED ->
10F -> 1A9, storyline CB -> CC -> CD, and the party becomes Lucca, Frog, Marle ($2980 = 02 04 01 | 80 85 81 03 05 80) for the
exchange screen `C8 00` opens.

What each leg needed (2026-10-14): the world VM's op 03 (C2/10BF, class $1CF5: a VRAM upload through the NMI queue), the C6
far calls E028/E04D/E64A/E66F (C3/0002 into $7F:9000 / $7F:8C00) and E63D/E644 ($7F01ED), the native C2/7BD1, ops 0C, 46,
47, 4A, 51; a world's exit into another world (state 2 -> C0:0000 -> the overworld loader); E3 in the fn0 pass writing
$011F; the lead's words read back before C0/8A6D; the dialogue's code 03 (one argument byte: the timed pause C0/21A8 and
`03 00`'s self-closing end, 2096); op D3 through C2/8C79 whole (the joiner's reserve entry `| 80`).

Compared: the location chain (every distinct $0100 in order), the storyline and the party at f9600, and the 182 box's own
timing -- the frames from its state 7 to 06 (the pause, $0216 * 15 + 1), to 07 and to 0D (no press).
Prints `aftermath: chain <locs> | story <s> | party <bytes> | box <gaps>` for each side, then IDENTICAL or DIFFERENT.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = 'saves/field_re/pre_scene_19f.state'
LAKEWRAM = 'saves/field_re/pre_scene_19f.lakewram'
END = 9600
NPRESS = 90                       # A at f6000 + 40k, k < 90


def box_gaps(seq):
    """seq: [(frame, $0129)] on change. The 182 box: from the first 07, the frames to each later change while it is up."""
    out = []
    for i, (f, v) in enumerate(seq):
        if v != 7:
            continue
        nxt = seq[i + 1:i + 4]
        if len(nxt) == 3 and [x[1] for x in nxt] == [6, 7, 0x0D]:
            out = ['%02X+%d' % (x[1], x[0] - f) for x in nxt]
            break
    return ' '.join(out) or 'none'


def main():
    inp = '0:6000,' + ','.join(['100:2,0:38'] * NPRESS)
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    r = subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc'),
                        '--monitor', '7E0100:2,7F0000:1,7E2980:9,7E0129:1', '--from', os.path.join(ROOT, STATE),
                        '--input', inp, '--diff'], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    ochain, obox, ostory, oparty = [], [], None, None
    for line in r.stdout.splitlines():
        m = re.match(r'\s*f(\d+)\s+(.*)', line)
        if not m:
            continue
        f, rest = int(m.group(1)), m.group(2)
        kv = dict(re.findall(r'\b(7E0100|7F0000|7E2980|7E0129)=([0-9A-F]+)', rest))
        if f > END:
            break
        loc = int(kv['7E0100'][2:4] + kv['7E0100'][0:2], 16)
        if not ochain or ochain[-1] != loc:
            ochain.append(loc)
        ostory, oparty = kv['7F0000'], kv['7E2980']
        if not obox or obox[-1][1] != int(kv['7E0129'], 16):
            obox.append((f, int(kv['7E0129'], 16)))

    ws = 'n:6000,' + ','.join(['a:2,n:38'] * NPRESS)
    penv = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
                CT_AUDIO_SEEDSTATE=os.path.join(ROOT, STATE), CT_LOCTRACE='1', CT_TBTRACE='1',
                CT_FIELD_MEMMON='0100:2,10000:1,2980:9', CT_SHOTS=os.path.join(ROOT, 'build', '_aftermath_%d' % os.getpid()),
                CT_SHOTS_AT=str(END), CT_SHOTS_EXIT='1')
    p = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--scene', '415',
                        '--loadwram', os.path.join(ROOT, LAKEWRAM), '--walkscript', ws], cwd=ROOT, env=penv,
                       capture_output=True, text=True, errors='replace')
    try:
        os.remove(os.path.join(ROOT, 'build', '_aftermath_%d_f%d.raw' % (os.getpid(), END)))
    except OSError:
        pass
    pchain, pbox, pstory, pparty = [], [], None, None
    for line in p.stdout.splitlines():
        m = (re.match(r'reboot: JML C0:0000 -> loc ([0-9A-F]+)', line) or re.match(r'ow-op05: C2/1100 -> (?:world|loc) ([0-9A-F]+)', line)
             or re.match(r'loctrace: -> loc ([0-9A-F]+)', line))
        if m:
            loc = int(m.group(1), 16)
            if not pchain or pchain[-1] != loc:
                pchain.append(loc)
            continue
        m = re.match(r'fmemmon f(\d+) loc=(\w+)\s+(.*)', line)
        if m:
            if not pchain:
                pchain.append(int(m.group(2), 16))
            kv = dict(re.findall(r'\b(0100|10000|2980)=([0-9A-F]+)', m.group(3)))
            pstory, pparty = kv.get('10000', pstory), kv.get('2980', pparty)
            continue
        m = re.match(r'tbtrace: f(\d+) vmTb=\d+ \$0129=([0-9A-F]+)', line)
        if m:
            v = int(m.group(2), 16)
            if not pbox or pbox[-1][1] != v:
                pbox.append((int(m.group(1)), v))
    o = 'chain %s | story %s | party %s | box %s' % (' '.join('%03X' % x for x in ochain), ostory, oparty, box_gaps(obox))
    q = 'chain %s | story %s | party %s | box %s' % (' '.join('%03X' % x for x in pchain), pstory, pparty, box_gaps(pbox))
    print('aftermath: cartridge %s' % o)
    print('aftermath: port      %s' % q)
    print('aftermath: %s' % ('IDENTICAL' if o == q else 'DIFFERENT'))


if __name__ == '__main__':
    sys.exit(main())
