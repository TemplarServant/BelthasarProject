#!/usr/bin/env python3
r"""Scan a tree for copyrighted bytes before it is published: nothing from the ROM may ship.

    python tools/release/scan_rom_leaks.py <dir> [--rom ROM] [--corpus FILE ...] [--window 24] [--binaries]

The corpora are the ROM itself (always; verified) plus any FILE given with --corpus -- the extracted ct_assets.dat (the
ROM's data decompressed) and the private bakes (src/data/*.h, savestates) are the useful ones, since decompressed
graphics never appear in the ROM's raw bytes. Every file under <dir> is read:

  * a binary file (a NUL byte, or not UTF-8) is a finding by itself unless it is on the allowlist below (the README's
    screenshots and clip in docs/media/, made by tools/release/make_media.py), and its bytes are searched too;
  * a text file has its byte runs pulled out -- C/Python arrays (0x12, 0x34 ...), 16-bit word arrays (0x1234, read
    little-endian), assembler bytes ($12,$34), hex dumps ("12 34 56 ..."), long hex strings and base64 blobs -- and each
    run is searched.

A run is a leak when a window of --window bytes of it (at least 8 distinct values: a table of zeros is not content)
occurs in a corpus. Prints one line per leak and exits 1 if any, 0 when the tree is clean:

    leak: src/data/title_intro.h:12  1536 bytes == ROM at 0x3F2A10
    clean: 812 files, 1,204 byte runs searched against 3 corpora (4.0 MB ROM + 18.2 MB more)
"""
import argparse
import base64
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from ctlib import rom as romlib  # noqa: E402

BINARY_OK_DIR = 'docs/media/'                    # the only binaries the public tree may carry: the README's pictures of
BINARY_OK_EXT = ('.png', '.gif', '.mp4')         #   the port running (tools/release/make_media.py; docs/ROM.md)
KNOWN_OK = {                                     # (path, text on the line) that match, and why they are not the game's
    ('snes/dsp.c', 'gaussValues'): "the S-DSP's gaussian interpolation table: the console's hardware (LakeSnes, MIT), not the "
                        "game; a smooth ramp also occurs in the extracted assets",
    ('src/sound/mixer.c', 'ctMixGauss'): "the same S-DSP interpolation table in the port's own mixer (src/sound/mixer.c, "
                        "which replaces snes/dsp.c): the console's hardware, not the game",
    ('disasm/notes/battle_hud_names_blink.md', '// Crono'): "the seven default character names in the game's text "
                                                     "encoding (Crono, Marle, ...): names, not the game's content",
    ('tools/regress_checks.json', '"names: f5 loc=000 $2C23='): "a check's expected output: the default names in the game's text encoding",
}
SKIP_DIRS = {'.git', '__pycache__'}

HEX_LIT = re.compile(r'0[xX]([0-9A-Fa-f]{1,4})\b')
ASM_BYTE = re.compile(r'\$([0-9A-Fa-f]{2})\b')
DUMP_RUN = re.compile(r'(?:\b[0-9A-Fa-f]{2}\b[ \t]+){7,}\b[0-9A-Fa-f]{2}\b')
HEX_STR = re.compile(r'\b(?:[0-9A-Fa-f]{2}){24,}\b')
B64_STR = re.compile(r'[A-Za-z0-9+/]{64,}={0,2}')


def runs_from_text(text):
    """(line, bytes) runs found in a text file."""
    out = []
    lines = text.split('\n')
    cur, cur_line, words = [], 0, False

    def flush():
        nonlocal cur
        if len(cur) >= 8:
            out.append((cur_line, bytes(cur)))
        cur = []

    for n, line in enumerate(lines, 1):
        lits = HEX_LIT.findall(line)
        if lits:                                 # a C/Python array row continues the run across lines
            if not cur:
                cur_line = n
                words = all(len(v) > 2 for v in lits)
            for v in lits:
                x = int(v, 16)
                if words or x > 0xFF:
                    cur += [x & 0xFF, x >> 8 & 0xFF]
                else:
                    cur.append(x)
        else:
            flush()
        b = ASM_BYTE.findall(line)
        if len(b) >= 8:
            out.append((n, bytes(int(v, 16) for v in b)))
        for m in DUMP_RUN.finditer(line):
            out.append((n, bytes(int(v, 16) for v in m.group(0).split())))
        for m in HEX_STR.finditer(line):
            out.append((n, bytes.fromhex(m.group(0))))
        for m in B64_STR.finditer(line):
            try:
                out.append((n, base64.b64decode(m.group(0), validate=True)))
            except Exception:
                pass
    flush()
    return out


def is_ramp(win):
    """A window made of STRUCTURE, not content: read as 1, 2 or 4 interleaved sequences (bytes, little-endian words,
    longs), each one steps by a constant (00 01 02 .., 00 00 01 00 02 00 .., a run of one value) or holds only zero
    and powers of two (80 40 20 .. 01: bit masks). Identity, index and bit tables: any program and any data may hold
    them."""
    def seq_ok(q):
        d = (q[1] - q[0]) & 0xFF if len(q) > 1 else 0
        if all(((q[k + 1] - q[k]) & 0xFF) == d for k in range(len(q) - 1)):
            return True
        return all(v & (v - 1) == 0 for v in q)
    return any(all(seq_ok(win[r::st]) for r in range(st)) for st in (1, 2, 4))


def content(win):
    return len(set(win)) >= 8 and not is_ramp(win)


def find_leak(data, corpora, w):
    """(corpus name, offset, window start) of the first content window of `data` found in a corpus, else None."""
    step = max(1, w // 2)
    for i in range(0, max(1, len(data) - w + 1), step):
        win = data[i:i + w]
        if len(win) < w or not content(win):
            continue
        for name, blob in corpora:
            off = blob.find(win)
            if off >= 0:
                return name, off, i
    return None


def corpus_index(corpora, w, stride):
    """Every content window of the corpora at `stride`, for searching large binaries: a run the binary shares with a
    corpus is found when it is at least w + stride - 1 bytes long."""
    idx = {}
    for name, blob in corpora:
        for o in range(0, len(blob) - w + 1, stride):
            win = blob[o:o + w]
            if content(win) and win not in idx:
                idx[win] = (name, o)
    return idx


def find_leak_indexed(data, idx, w):
    for i in range(0, len(data) - w + 1):
        hit = idx.get(data[i:i + w])
        if hit and content(data[i:i + w]):
            return hit[0], hit[1], i
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('dir')
    ap.add_argument('--rom')
    ap.add_argument('--corpus', action='append', default=[])
    ap.add_argument('--window', type=int, default=24)
    ap.add_argument('--binaries', action='store_true',
                    help='a release folder: binaries (the executables, the libraries, a bundled runtime) are not '
                         'findings by themselves; their bytes are searched against an index of the corpora (every '
                         'shared run of window + 15 bytes or more)')
    ap.add_argument('--third-party', action='append', default=[], metavar='PREFIX',
                    help='a path (relative, a file or a folder prefix) that is an upstream runtime shipped as it is -- '
                         'SDL2.dll, a bundled Python: listed, not searched')
    a = ap.parse_args()

    corpora = [('ROM', romlib.load(a.rom))]
    for c in a.corpus:
        if os.path.isdir(c):
            for root, _, files in os.walk(c):
                for f in files:
                    p = os.path.join(root, f)
                    data = open(p, 'rb').read()
                    try:                         # a baked header: its arrays are the corpus, not its text
                        data = b''.join(r for _, r in runs_from_text(data.decode('utf-8'))) or data
                    except UnicodeDecodeError:
                        pass
                    corpora.append((os.path.relpath(p, c), data))
        else:
            corpora.append((os.path.basename(c), open(c, 'rb').read()))

    idx = corpus_index(corpora, a.window, 16) if a.binaries else None
    leaks = nfiles = nruns = nthird = 0
    for root, dirs, files in os.walk(a.dir):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            p = os.path.join(root, f)
            rel = os.path.relpath(p, a.dir).replace('\\', '/')
            if any(rel == t or rel.startswith(t.rstrip('/') + '/') for t in a.third_party):
                nthird += 1
                continue
            data = open(p, 'rb').read()
            nfiles += 1
            text = None
            if b'\0' not in data:
                try:
                    text = data.decode('utf-8')
                except UnicodeDecodeError:          # a Latin-1 comment is still text; control bytes are not
                    t = data.decode('latin-1')
                    if not any(c < ' ' and c not in '\t\r\n\f' for c in t):
                        text = t
            lines = (text or '').split(chr(10))
            if text is not None:
                runs = runs_from_text(text)
            else:
                if not a.binaries and not (rel.startswith(BINARY_OK_DIR) and rel.lower().endswith(BINARY_OK_EXT)):
                    print('binary: %s  %d bytes (not on the allowlist)' % (rel, len(data)))
                    leaks += 1
                runs = [(0, data)]
            for line, run in runs:
                nruns += 1
                hit = find_leak_indexed(run, idx, a.window) if (idx is not None and text is None) else find_leak(run, corpora, a.window)
                ok = any((rel == kp or rel.endswith('/' + kp)) and line and ks in "".join(lines[max(0, line - 2):line])
                         for kp, ks in KNOWN_OK)                 # (a release's kit holds the tree under setup/kit/)
                if hit and not ok:
                    leaks += 1
                    print('leak: %s:%d  %d-byte run, a window at +%d == %s at 0x%X'
                          % (rel, line, len(run), hit[2], hit[0], hit[1]))
    more = sum(len(b) for _, b in corpora[1:])
    if leaks:
        print('NOT CLEAN: %d finding(s) in %d files' % (leaks, nfiles))
        return 1
    print('clean: %d files, %d byte runs searched against %d corpora (%.1f MB ROM + %.1f MB more)%s'
          % (nfiles, nruns, len(corpora), len(corpora[0][1]) / 2**20, more / 2**20,
             '; %d third-party files not searched (%s)' % (nthird, ', '.join(a.third_party)) if nthird else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
