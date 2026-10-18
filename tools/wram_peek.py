#!/usr/bin/env python3
r"""Print WRAM bytes of a Belthasar run at a chosen frame, as a greppable signature.

    python tools/wram_peek.py <frame> <addr:len>[,<addr:len>...] -- <Belthasar args...>

e.g.  python tools/wram_peek.py 2880 21BA:6 -- --assets ct_assets.dat --scene 433 --selftest
prints `wrampeek: f2880 $21BA = 9D 23 3B 0E B4 09`.

Belthasar's CT_DUMPWRAM/CT_DUMPWRAM_AT write the engine's LIVE 128 KB image ($7E page, then the $7F
story page) to a file; this runs it with --frames set just past <frame> and reads the bytes back, so a
regression check can pin a WRAM value the engine never prints -- and pin it against a number measured
on the oracle, not against the engine's own output.
"""
import os
import subprocess
import sys


def main():
    if '--' not in sys.argv or len(sys.argv) < 5:
        sys.exit(__doc__)
    cut = sys.argv.index('--')
    frame = int(sys.argv[1])
    spans = []
    for part in sys.argv[2].split(','):
        a, n = part.split(':')
        spans.append((int(a, 16), int(n)))
    args = [a for a in sys.argv[cut + 1:]]
    if '--frames' not in args:
        args += ['--frames', str(frame + 2)]
    dump = 'build/_wrampeek_%d.wram' % os.getpid()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_DUMPWRAM=dump, CT_DUMPWRAM_AT=str(frame))
    subprocess.run([os.path.abspath('build/Belthasar.exe')] + args, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(dump):
        sys.exit('wrampeek: no dump at f%d (not a field frame?)' % frame)
    d = open(dump, 'rb').read()
    os.remove(dump)
    for a, n in spans:
        print('wrampeek: f%d $%04X = %s' % (frame, a, ' '.join('%02X' % b for b in d[a:a + n])))


if __name__ == '__main__':
    main()
