#!/usr/bin/env python3
r"""The ROM-free half of the leak check: no tracked file is one of the game's files by its name or its kind.

    python tools/release/check_tracked.py

Reads `git ls-files` (the index: a file staged for the next commit counts) and fails on

  * a path the public export leaves out (tools/release/export_public.py EXCLUDE: the ROM, ct_assets.dat, savestates,
    WRAM captures, battery saves, src/data's tables, src/xlat's translated programs, ...);
  * a binary file (tools/release/scan_rom_leaks.py's decode_text) outside the scanner's allowlist -- the README's
    pictures in docs/media/ -- the upstream files named in UPSTREAM below and the files export_public.py's KEEP
    publishes by name.

It needs no ROM, so it runs everywhere (the GitHub CI, a fresh clone). It does NOT search the files' bytes: a table
pasted into a source file is found only by tools/release/scan_rom_leaks.py, which needs the ROM (CONTRIBUTING.md).

Prints one line per finding and exits 1, or

    tracked: clean: 1140 files, 10 binaries (9 in docs/media/, 1 upstream)
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from export_public import KEEP, excluded  # noqa: E402
from scan_rom_leaks import BINARY_OK_DIR, BINARY_OK_EXT, UPSTREAM, decode_text  # noqa: E402


def main():
    p = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, capture_output=True)
    if p.returncode != 0:
        print('tracked: git ls-files failed: %s' % p.stderr.decode(errors='replace').strip())
        return 2
    files = [f for f in p.stdout.decode('utf-8').split('\0') if f]
    findings = nbin = nmedia = nup = 0
    for f in files:
        pat = excluded(f)
        if pat:
            print('tracked: %s  matches %s (tools/release/export_public.py: never published)' % (f, pat))
            findings += 1
            continue
        path = os.path.join(ROOT, f)
        if not os.path.isfile(path):             # staged for deletion, or a submodule
            continue
        with open(path, 'rb') as fh:
            data = fh.read()
        if decode_text(data) is not None:
            continue
        nbin += 1
        if f.startswith(BINARY_OK_DIR) and f.lower().endswith(BINARY_OK_EXT):
            nmedia += 1
        elif f in UPSTREAM or f in KEEP:
            nup += 1
        else:
            print('tracked: %s  a binary file, %d bytes (only docs/media/ pictures and the upstream files in '
                  'tools/release/check_tracked.py may be binaries)' % (f, len(data)))
            findings += 1
    if findings:
        print('tracked: NOT CLEAN: %d finding(s) in %d files' % (findings, len(files)))
        return 1
    print('tracked: clean: %d files, %d binaries (%d in %s, %d upstream)' % (len(files), nbin, nmedia, BINARY_OK_DIR,
                                                                             nup))
    return 0


if __name__ == '__main__':
    sys.exit(main())
