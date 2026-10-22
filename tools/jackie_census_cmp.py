#!/usr/bin/env python3
r"""Real battery saves across the story (Jackie's, saves/downloaded/ctjackie) loaded and run by the port, against the cartridge.

    python tools/jackie_census_cmp.py

Each save is booted the player's way (`--boot`, CT_SRAM, the Load screen: `n:198,s:1,n:125,a:1`), then left 300 frames and
given A every 40 frames for 1200 (on a world map that enters what the party stands on). Graded:

- `census chNN <loc> clean` -- the run printed no "PARKED" / "UNDERIVED" / "not run" line. The saves are the ones whose world
  scripts needed what the 2026-10-14 census found missing: op 24 (C2/14AD, ch24), ops 44/45 (C2/1A52 / 1AAC, ch13), op 4F
  (C2/1B2F, ch16 / ch19 / ch21), the native C2/7BC4 (ch02 / ch10).
- `map ch16 diffs N` -- the world's two map buffers ($7E:4000-$6FFF, what op 4F copies) 160 frames into the world, against the
  cartridge's 120 frames past the same save's load (saves/downloaded/ctjackie/states/ch16.state): 0.
- `lead ch23 ...` -- loc 173 after the Ocean Palace (party Marle / Lucca / Frog), walked Down: the frame's hash, with the
  lead drawn. The lead is $0197's object (C0/426A), not the first fn0 with op 0x57 (Crono's own load, a no-op here).

Needs the downloaded saves (local, gitignored): SKIPs without them.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JK = os.path.join(ROOT, 'saves', 'downloaded', 'ctjackie')
OUT = os.path.join(ROOT, 'build', '_jackie')
LOAD = 'n:198,s:1,n:125,a:1'   # Start on the power-on's title (boot_loader.md sec. 10): 133 + 65


def port(n, ws, extra_env=None, args=()):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SRAM=os.path.join(JK, 'srm', 'ch%s.srm' % n))
    env.update(extra_env or {})
    r = subprocess.run([os.path.join(ROOT, 'build', 'Belthasar.exe'), '--assets', 'ct_assets.dat', '--boot',
                        '--walkscript', ws] + list(args), cwd=ROOT, env=env, capture_output=True, text=True,
                       errors='replace')
    return r.stdout + r.stderr


def main():
    os.makedirs(OUT, exist_ok=True)
    mash = LOAD + ',n:300,' + ','.join(['a:2,n:38'] * 30)
    for n in ('02', '10', '13', '16', '19', '21', '24'):
        shot = os.path.join(OUT, 'c%s_%d' % (n, os.getpid()))
        out = port(n, mash, {'CT_SHOTS': shot, 'CT_SHOTS_AT': '1865', 'CT_SHOTS_EXIT': '1'})
        bad = sorted({l.strip() for l in out.splitlines()
                      if 'PARKED' in l or 'UNDERIVED' in l or ' not run' in l})
        loc = [l.split('loc=')[1].split()[0] for l in out.splitlines() if l.startswith('shots: f1865')]
        print('census ch%s %s %s' % (n, loc[0] if loc else '?', 'clean' if not bad else 'GAPS: ' + ' | '.join(bad)))
        try:
            os.remove(shot + '_f1865.raw')
        except OSError:
            pass

    # op 4F: the world's map buffers against the cartridge
    ow = os.path.join(OUT, 'ow16_%d' % os.getpid())
    port('16', LOAD + ',n:300', {'CT_OWDUMP': ow, 'CT_OWDUMP_AT': '485', 'CT_SHOTS': ow + 's', 'CT_SHOTS_AT': '486',
                                 'CT_SHOTS_EXIT': '1'})
    oraw = os.path.join(OUT, 'o16_%d.raw' % os.getpid())
    subprocess.run([os.path.join(ROOT, 'build', 'ct.exe'), os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc'), '--peek',
                    os.path.join(JK, 'states', 'ch16.state'), oraw, '--btnscript', '0:120'], cwd=ROOT,
                   env=dict(os.environ, CT_ORACLE_LOGICAL='1'), capture_output=True)
    try:
        p = open(ow, 'rb').read()[:0x20000]
        o = open(oraw + '.wram', 'rb').read()
        print('map ch16 diffs %d' % sum(1 for a in range(0x4000, 0x7000) if p[a] != o[a]))
    except OSError as e:
        print('map ch16 missing (%s)' % e)
    for f in (ow, ow + 's_f486.raw', oraw, oraw + '.wram', oraw + '.vram', oraw + '.oam'):
        try:
            os.remove(f)
        except OSError:
            pass

    # the lead after the Ocean Palace
    out = port('23', LOAD + ',n:300,d:40,n:30,d:200', None,
               ('--selftest', os.path.join(OUT, 's23_%d.raw' % os.getpid()), '--frames', '965'))   # (900 + 65: the power-on boot, 404971a)
    line = [l for l in out.splitlines() if l.startswith('selftest: scene 371')]
    print('lead ch23 %s' % (line[0].split('player')[1].strip() if line else 'none'))
    try:
        os.remove(os.path.join(OUT, 's23_%d.raw' % os.getpid()))
    except OSError:
        pass


if __name__ == '__main__':
    sys.exit(main())
