"""The sound commands, port against cartridge, command for command.

  python tools/sound_post_cmp.py <state> <scene> <walkscript> <frames>
  python tools/sound_post_cmp.py --attract <frames>     # the demo: the cartridge from saves/attract_cena_inicial_rule.state,
                                                        #   the port from its boot (--scene 433 --selftest)

The cartridge: the oracle from <state> (CT_ORACLE_LOGICAL=1) under the walkscript's pad, PEEK_APUTRACE over the whole run;
every JSL C7:0004 that reaches the ports writes the command to $2140 -- at C7/0635 (the $8x driver commands, $18-$2F) or,
for a song, `10` at C7/0A6E (repeated while the SPC is busy, the run ended by C7/060C's `E0` or C7/0A8A's `11`) -- after its
first parameter to $2141. The port: Belthasar from <state>'s WRAM (<state> minus .state plus .lakewram) and its SPC, the same
walkscript, CT_SNDPOSTS=1 (audio.c CtAudioService: `sndpost f<frame> cmd= p1=` for every JSL that reaches the ports).

Prints `soundposts: <n> commands, cartridge = port` or the first difference, and each command's frame on both sides.
"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
ATTRACT = sys.argv[1] == '--attract'
if ATTRACT: state, scene, ws, frames = 'saves/attract_cena_inicial_rule.state', '433', '', int(sys.argv[2])   # power-on + 2159 (attract_rule.json)
else: state, scene, ws, frames = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
pad = {'r': 0x80, 'l': 0x40, 'd': 0x20, 'u': 0x10, 'n': 0, 'a': 0x100, 'b': 0x01, 'x': 0x200, 'y': 0x02}

def btnscript():
    out, t = [], 0
    for tok in (ws.split(',') if ws else []):
        k, n = tok.split(':'); n = min(int(n), frames - t)
        if n <= 0: break
        out.append('%x:%d' % (pad[k], n)); t += n
    if t < frames: out.append('0:%d' % (frames - t))
    return ','.join(out)

def cartridge():
    env = dict(os.environ, CT_ORACLE_LOGICAL='1', PEEK_APUTRACE='0:%d' % frames)
    raw = 'build/_sndpost_%d.raw' % os.getpid()
    r = subprocess.run([os.path.abspath('build/ct.exe'), '--peek', state, raw] + (['--frames', str(frames)] if ATTRACT else ['--btnscript', btnscript()]),
                       env=env, capture_output=True, text=True, errors='replace')
    for e in ('', '.wram', '.vram', '.oam'):
        try: os.remove(raw + e)
        except OSError: pass
    cmds, p1, song = [], 0, False
    for l in (r.stdout + r.stderr).splitlines():
        if 'CPU $214' not in l: continue
        f = int(l.split()[1][1:]); port = l.split('CPU $')[1][:4]; v = int(l.split('= ')[1][:2], 16); pc = l.split(' at ')[1].split()[0]
        if port == '2141': p1 = v
        elif port == '2140':
            if pc == 'C7/0635': cmds.append((f, v, p1))
            elif pc == 'C7/0A6E':
                if not song: cmds.append((f, 0x10, p1)); song = True
            elif pc in ('C7/060C', 'C7/0A8A'): song = False
    return cmds

def port():
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_LOCKSTEP='1', CT_SNDPOSTS='1')
    if ATTRACT:
        env['CT_AUDIO_FORCE'] = '1'
        argv = ['--scene', scene, '--selftest', '--frames', str(frames + 2200)]
    else:
        env['CT_AUDIO_SEEDSTATE'] = state
        argv = ['--scene', scene, '--loadwram', state[:-len('.state')] + '.lakewram', '--walkscript', ws, '--selftest', '--frames', str(frames)]
    r = subprocess.run([os.path.abspath('build/Belthasar.exe'), '--assets', 'ct_assets.dat'] + argv, env=env, capture_output=True, text=True, errors='replace')
    cmds = []
    for l in r.stdout.splitlines():
        if l.startswith('sndpost f'):
            p = l.split(); cmds.append((int(p[1][1:]), int(p[2][4:], 16), int(p[3][3:], 16)))
    return cmds

c, p = cartridge(), port()
if ATTRACT:
    # The port boots (title included) where the cartridge's state starts at the demo: the two command lists are aligned
    # (difflib on (command, parameter)) and each matched pair printed with its offset, port frame minus cartridge frame.
    import difflib
    sm = difflib.SequenceMatcher(None, [x[1:] for x in c], [x[1:] for x in p], autojunk=False)
    pairs = [(c[i + k], p[j + k]) for i, j, n in sm.get_matching_blocks() for k in range(n)]
    for a_, b_ in pairs:
        print('soundpost %02X %02X: cartridge f%-5d port f%-5d offset %+d' % (a_[1], a_[2], a_[0], b_[0], b_[0] - a_[0]))
    print('soundposts: attract, cartridge %d commands, port %d, %d matched in order' % (len(c), len(p), len(pairs)))
    sys.exit(0)
n = min(len(c), len(p)); first = next((i for i in range(n) if c[i][1:] != p[i][1:]), -1)
for i in range(max(len(c), len(p))):
    a = '%02X %02X f%d' % (c[i][1], c[i][2], c[i][0]) if i < len(c) else '--'
    b = '%02X %02X f%d' % (p[i][1], p[i][2], p[i][0]) if i < len(p) else '--'
    print('soundpost %3d: cartridge %-14s port %-14s%s' % (i, a, b, '' if i < n and c[i][1:] == p[i][1:] else '  <--'))
if first < 0 and len(c) == len(p):
    print('soundposts: %d commands, cartridge = port' % len(c))
else:
    print('soundposts: cartridge %d commands, port %d; first difference at %d' % (len(c), len(p), first if first >= 0 else n))
