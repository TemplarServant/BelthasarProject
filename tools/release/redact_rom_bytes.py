#!/usr/bin/env python3
r"""Replace byte runs copied from the ROM, in notes and text files, with a pointer to where they are in the ROM.

    python tools/release/redact_rom_bytes.py <file>... [--rom ROM] [--window 24] [--dry-run]

A reverse-engineering note may quote the bytes it talks about; a public one must not carry the game's bytes. For each
hex dump ("A6 EB BD 01 ..."), long hex string or assembler byte list in the file whose bytes occur in the ROM (a
window of --window bytes with at least 8 distinct values, the same test as scan_rom_leaks.py), the quoted bytes are
replaced by

    [N ROM bytes at 0xOFFSET: python tools/dis.py 0xOFFSET N]

so the reader can see them in their own copy. Only the matched text changes; everything around it is kept.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from ctlib import rom as romlib  # noqa: E402

PATTERNS = [
    re.compile(r'(?:\b[0-9A-Fa-f]{2}\b[ \t]+){7,}\b[0-9A-Fa-f]{2}\b'),      # 12 34 56 ...
    re.compile(r'\b(?:[0-9A-Fa-f]{2}){24,}\b'),                              # 123456...
    re.compile(r'(?:\$[0-9A-Fa-f]{2}\s*,\s*){7,}\$[0-9A-Fa-f]{2}\b'),         # $12,$34,...
]


def to_bytes(s):
    return bytes(int(h, 16) for h in re.findall(r'[0-9A-Fa-f]{2}', s.replace('$', '')))


def rom_hit(data, rom, w):
    for i in range(0, max(1, len(data) - w + 1), max(1, w // 2)):
        win = data[i:i + w]
        if len(win) == w and len(set(win)) >= 8:
            off = rom.find(win)
            if off >= 0:
                whole = rom.find(data)
                return (whole, len(data)) if whole >= 0 else (off - i, len(data))
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('files', nargs='+')
    ap.add_argument('--rom')
    ap.add_argument('--window', type=int, default=24)
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    rom = romlib.load(a.rom)
    total = 0
    for p in a.files:
        raw = open(p, 'rb').read()
        text = raw.decode('utf-8')
        n = 0

        def repl(m):
            nonlocal n
            data = to_bytes(m.group(0))
            hit = rom_hit(data, rom, a.window)
            if not hit:
                return m.group(0)
            n += 1
            off, ln = hit
            return '[%d ROM bytes at 0x%X: python tools/dis.py 0x%X %d]' % (ln, off, off, ln)

        for pat in PATTERNS:
            text = pat.sub(repl, text)
        if n:
            total += n
            print('%s: %d run(s) replaced' % (p, n))
            if not a.dry_run:
                open(p, 'wb').write(text.encode('utf-8'))
    print('redact: %d run(s) in %d file(s)%s' % (total, len(a.files), ' (dry run)' if a.dry_run else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
