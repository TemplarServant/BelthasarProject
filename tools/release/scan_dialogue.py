#!/usr/bin/env python3
r"""Scan a tree for the game's TEXT: passages of the script quoted in notes, comments or docs.

    python tools/release/scan_dialogue.py <dir> [--rom ROM] [--chars 40]

scan_rom_leaks.py finds the game's bytes; dialogue is stored encoded, so a quoted line of it never matches them.
This decodes every dialogue table from the ROM (assets/text.py, the same decoder the engine's assets use), folds
case and whitespace, and reports any run of --chars characters of a file in <dir> that is also a run of the script.
Short names (an item, a place, a character) are far below the threshold. Exits 1 on a finding:

    dialogue: disasm/notes/textbox.md:120  "...<40 characters of the script>..."
    clean: 812 files, no passage of 40 characters from the script
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from ctlib import rom as romlib  # noqa: E402


def fold(s):
    return re.sub(r'[^a-z0-9]+', ' ', s.lower())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('dir')
    ap.add_argument('--rom')
    ap.add_argument('--chars', type=int, default=40)
    a = ap.parse_args()
    rom_path = a.rom or os.environ.get('CT_ROM') or romlib.find_default()
    romlib.load(rom_path)                                   # the supported dump only
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, 'dlg.txt')
        subprocess.run([sys.executable, os.path.join(ROOT, 'assets', 'text.py'), rom_path, 'dialogue', out],
                       capture_output=True)
        script = open(out, encoding='utf-8', errors='replace').read()
    script = re.sub(r'=== table @0x[0-9A-F]+ \(\d+ strings\) ===|\[\d+\]', ' ', script)
    script = fold(script)
    n = a.chars
    shingles = {script[i:i + n] for i in range(len(script) - n + 1)}
    hits = files = 0
    for root, dirs, fs in os.walk(a.dir):
        dirs[:] = [d for d in dirs if d not in ('.git', '__pycache__')]
        for f in fs:
            p = os.path.join(root, f)
            try:
                lines = open(p, encoding='utf-8').read().split('\n')
            except (UnicodeDecodeError, OSError):
                continue
            files += 1
            for ln, line in enumerate(lines, 1):
                t = fold(line)
                if len(t) < n:
                    continue
                for i in range(0, len(t) - n + 1, 4):
                    if t[i:i + n] in shingles:
                        hits += 1
                        print('dialogue: %s:%d  "...%s..."' % (os.path.relpath(p, a.dir).replace('\\', '/'), ln,
                                                              t[i:i + n + 10].strip()))
                        break
    if hits:
        print('NOT CLEAN: %d line(s) quote the script' % hits)
        return 1
    print('clean: %d files, no passage of %d characters from the script' % (files, n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
