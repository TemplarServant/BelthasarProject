#!/usr/bin/env python3
r"""The attract demo battle's SOUND BRACKET against the cartridge: what the field posts around the battle, and the
SFX set the driver holds in ARAM during and after it.

    python tools/attract_battle_sfx_cmp.py

Loc 0AD sets $2A1F bit6, which sends the field's two battle routines down their `70` arms (C0/1BAB -> 1BD5
`70 01`, C0/1BE6 -> 1C7F `70 00`): the battle SFX set without a battle song, and back. Command $70 is C7/08E3,
which uploads the $2F00 set through C7/0922. The port used to run C0/1BE6's restore chain unconditionally, whose
`81 00 00 FF` ... `81 40 FF FF` pulled the master volume to 0 and faded it back -- the music dipped when the battle
ended.

Port: the LIVE battle path (no --selftest; the headless --selftest battle resolves synchronously and runs neither
routine), CT_SNDTRACE=1 for the posts, CT_AUDIO_PROBE(+_ARAM) for the 64 KB ARAM at a frame inside the battle and one
after it. Cartridge: saves/attract_demo_battle_f1850.state (inside the battle, after its `70 01`) and
saves/attract_demo_battle_f1850.state + 400 frames (after its `70 00`: the JSL at f322 line 238 returns at f337
line 43, $1E10 1 -> 0 by C7/08F5), ARAM via PEEK_ARAM. NOT attract_loc0AD_flea.state: its driver latch $1E05 is $18
(bit7 clear), so C7/0157 skips every command in that run and set A would be there without any upload.
Compared: ARAM $2F00-$4702 -- the 6144-byte set plus the three bytes a one-unit-too-long stream would write.
"""
import os
import re
import subprocess
import sys

FIELD = os.path.abspath('build/ct_field.exe')
CT = os.path.abspath('build/ct.exe')
ROM = 'Chrono_Trigger_USA_SNES.sfc'


def port(frame, tag):
    aram = os.path.join('build', '_bsfx_port_%s.bin' % tag)
    if os.path.exists(aram):
        os.remove(aram)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1', CT_AUDIO_FORCE='1',
               CT_SNDTRACE='1', CT_AUDIO_PROBE=str(frame), CT_AUDIO_PROBE_ARAM=aram)
    r = subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', '433'], env=env, capture_output=True,
                       text=True, errors='replace', timeout=900)
    posts = re.findall(r'\[snd\] field f\d+ (C0/[0-9A-F]{4}) cmd=([0-9A-F]{2}) p1=([0-9A-F]{2})', r.stdout + r.stderr)
    return (open(aram, 'rb').read() if os.path.exists(aram) else None), posts


def oracle(state, frames, tag):
    aram = os.path.join('build', '_bsfx_orc_%s.bin' % tag)
    fb = os.path.join('build', '_bsfx_orc_%s.raw' % tag)
    env = dict(os.environ, PEEK_ARAM=aram)
    subprocess.run([CT, '--peek', state, fb, '--frames', str(frames)], env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=900)
    return open(aram, 'rb').read() if os.path.exists(aram) else None


def which(img, rom):
    seg = img[0x2F00:0x2F00 + 6144]
    return 'A' if seg == rom[0x7430B:0x7430B + 6144] else 'B' if seg == rom[0x75B0D:0x75B0D + 6144] else '?'


def main():
    rom = open(ROM, 'rb').read()
    pin, posts_in = port(3840, 'in')
    pout, posts_out = port(4392, 'out')
    oin = oracle('saves/attract_demo_battle_f1850.state', 1, 'in')
    oout = oracle('saves/attract_demo_battle_f1850.state', 400, 'out')
    if not (pin and pout and oin and oout):
        sys.exit('attract-battle-sfx: FAIL -- missing ARAM dump')
    print('attract-battle-sfx: field posts ' + ' | '.join('%s %s %s' % p for p in posts_out))
    for name, p, o in (('in battle', pin, oin), ('after battle', pout, oout)):
        same = p[0x2F00:0x4703] == o[0x2F00:0x4703]
        print('attract-battle-sfx: %s ARAM $2F00-$4702 port set %s, cartridge set %s, identical=%d'
              % (name, which(p, rom), which(o, rom), 1 if same else 0))


if __name__ == '__main__':
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    main()
