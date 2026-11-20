#!/usr/bin/env python3
r"""The sound commands of a playthrough leg, the port against the cartridge, command for command.

    python tools/leg_sound_cmp.py [name ...]

Both sides play the leg from its start (the cartridge's state at the leg's start, made from the route by
tools/playthrough.py; the port seeded from that state's WRAM and SPC with the leg's own pad) for the case's frames.
The cartridge's commands are its stores to $2140 at the two command PCs (C7/0635, a command's; C7/0A6E, a song's) with
the $2141 before each (`ct.exe --peek` PEEK_APUTRACE); the port's are its CT_SNDPOSTS lines (every JSL that reaches the
ports), less commands $70 / $71, whose stores are not at those PCs, and `10 FF` / `11 FF` (no song: C7 stores nothing). What is graded is the ORDER and the two bytes -- which
sound, after which -- not the frame (the legs' frames are tools/playthrough.py's and tools/leg_cells_cmp.py's matter).

    legsound: <name> leg <n> +<frames>: <c> commands on the cartridge, <p> on the port, <d> differing

Regression checks: the `leg-sound-*` family (docs/TESTING.md).
"""
import difflib
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import playthrough as P  # noqa: E402

WORK = os.environ.get('LEGSOUND_TMP') or os.path.join(ROOT, 'build', '_legsound')

# name: leg, the frames played.
CASES = {
    # The field menu's Equip screen. A on a gear row blips (C2/9C90) and starts the candidate list's climb (C2/A4AF, eight
    # passes of 4 px); the climb's end makes the list live -- C2/A44D `INC $00C1 / LDA #DF / STA $0D13 / LDA #04 / STA $54 /
    # PLP / JMP C2/EAC2` -- and blips again, seven frames after the first. The commit is `LDA #B5 / JSR C2/EABA` and then
    # C2/9CE4 `JSR C2/EAC2`, which the $B5's throttle ($0D76, C2/EB0E) leaves staged in $1E00 / $1E01 and unposted. Leg 39
    # equips three pieces after the Arris shop: 87 commands, the cartridge's `18 00` on f5434, f5516 and f5662.
    'equip-list-blips-when-drawn': dict(leg='39_arris_shop', frames=5700),
    # A fight's action script is the main thread's: an action's loops come back from their frame's wait into C1/10FA -- the
    # menu pass, whose handlers blip (C1/1B55, `19 00`) -- and only then go on with the script, whose sound ops are C1/619E-
    # 61F5. Leg 42, f2249: the cartridge's `19 00` (C1/1B5D, line 64) and then `18 04` (C1/61B0, line 68) in one frame.
    'action-script-sounds-after-the-menu-pass': dict(leg='42_arris_beyond', frames=2400),
    # The naming screen (C2/CB8B): with another song playing, C2/CBAB `LDX #0108 / STX $02 / JSR C2/CBF7` posts `81 08 01 01`
    # -- the room's song down -- before the task C2/CBBF is spawned, whose `14 3E` / `81 01 FF FF` follow. Leg 75, f976.
    'naming-screen-turns-the-song-down-first': dict(leg='75_ayla_joins', frames=1100),
    # The world's main loop inside C2/0462's JSL (a song its request queue uploads): `$1BEC` = FF refuses every request
    # (C2/2EDE) and a `$027C` = 3 the pass sets waits for the JSL. Leg 72 (1F0): `10 3C` f577-f599, the script's `81 00 FF
    # FF` staged on f600 and never posted, the exit's `82` / `83` on f618.
    'world-main-loop-stands-in-a-song-upload': dict(leg='72_mystic_mountain', frames=700),
    # Op 0xFF < $90 never returns to the runner: no object is visited again. The same leg's Gate out of loc 024 (`FF 82`
    # after `10 37` on f1234): nothing is posted during the fade and the tunnel (the port ran obj12's `E8 53` on f1318).
    'no-script-runs-after-op-ff': dict(leg='72_mystic_mountain', frames=1400),
}


def run(name):
    c = CASES[name]
    legs = P.route(); i = P.find(legs, c['leg']); lg = legs[i]
    tmp = os.path.join(WORK, 'run%d' % os.getpid()); os.makedirs(tmp, exist_ok=True)
    P.TMP = tmp
    st = P.start_of(legs, i)
    n = c['frames']; pad = []
    for b, q in lg['pad']:
        if n <= 0:
            break
        pad.append((b, min(q, n))); n -= q
    frames = sum(q for _, q in pad)
    pr = subprocess.Popen([P.CT, '--peek', st, os.path.join(tmp, 'peek.raw'), '--btnscript', P.btnscript(pad) or '0:0',
                           '--frames', str(frames)], cwd=ROOT, env=dict(P.RULE, PEEK_APUTRACE='0:%d' % frames),
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors='replace')
    co = []; p1 = None
    for ln in pr.stdout:
        if ' CPU $214' not in ln:
            continue
        m = re.search(r'aputrace f(\d+) CPU \$214([01]) = ([0-9A-F]{2}) at (\S+)', ln)
        if not m:
            continue
        if m.group(2) == '1':
            p1 = m.group(3)
        elif m.group(4) in ('C7/0635', 'C7/0A6E'):
            co.append((int(m.group(1)), m.group(3), p1))
    pr.wait()
    w = P.wram_of(st)
    lw = os.path.join(tmp, 'seed.lakewram')
    open(lw, 'wb').write(b'#lakewram\nRAM:131072:' + w)
    loc = (w[0x100] | (w[0x101] << 8)) & 0x1FF
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1',
               CT_AUDIO_SEEDSTATE=st, CT_WS_BATTLE_LEAD='1', CT_SHOTS=os.path.join(tmp, 'pe'), CT_SHOTS_AT=str(frames),
               CT_SHOTS_EXIT='1', CT_SRAM_FRESH='1', CT_SRAM=os.path.join(tmp, 'port.srm'), CT_SNDPOSTS='1')
    if P.seed_in_load_fade(st, w):
        env['CT_SEED_LOADFADE'] = '1'
    wf = os.path.join(tmp, 'walk.txt')
    open(wf, 'w').write(P.walkscript(pad, P.last_key(legs, i)))
    r = subprocess.run([P.FIELD, '--assets', 'ct_assets.dat', '--scene', str(loc), '--loadwram', lw, '--walkfile', wf],
                       cwd=ROOT, env=env, capture_output=True)
    po = [(int(m.group(1)), m.group(2), m.group(3)) for m in
          re.finditer(r'sndpost f(-?\d+) cmd=([0-9A-F]{2}) p1=([0-9A-F]{2})', (r.stdout + r.stderr).decode('latin-1'))]
    po = [x for x in po if x[1] not in ('70', '71') and not (x[1] in ('10', '11') and x[2] == 'FF')]   # (`10 FF`: no song, no store)

    def collapse(L):                                 # a command C7 sends again (C7/0A5A's resend on a timeout) is one
        out = []
        for x in L:
            if out and out[-1][1:] == x[1:] and x[0] - out[-1][0] <= 1:
                out[-1] = (x[0],) + out[-1][1:]
                continue
            out.append(x)
        return out
    co, po = collapse(co), collapse(po)
    sm = difflib.SequenceMatcher(None, [x[1:] for x in co], [x[1:] for x in po], autojunk=False)
    ops = [o for o in sm.get_opcodes() if o[0] != 'equal']
    d = sum(max(i2 - i1, j2 - j1) for _, i1, i2, j1, j2 in ops)
    print('legsound: %s leg %s +%d: %d commands on the cartridge, %d on the port, %d differing' % (
        name, c['leg'].split('_')[0], frames, len(co), len(po), d))
    for tag, i1, i2, j1, j2 in ops[:6]:
        print('  %s: cartridge %s | port %s' % (tag, ' '.join('%s %s@f%d' % (x[1], x[2], x[0]) for x in co[i1:i2][:6]) or '-',
                                              ' '.join('%s %s@f%d' % (x[1], x[2], x[0]) for x in po[j1:j2][:6]) or '-'))
    return 0


def main():
    names = sys.argv[1:] or sorted(CASES)
    os.chdir(ROOT)
    for nm in names:
        if nm not in CASES:
            print('legsound: no case %s' % nm); return 1
        run(nm)
    return 0


if __name__ == '__main__':
    sys.exit(main())
