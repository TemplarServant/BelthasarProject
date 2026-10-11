#!/usr/bin/env python3
r"""Shapes: the generated files with their declarations and sizes but none of the game's data -- the bootstrap.

    python tools/gen/shapes.py make      write tools/gen/shapes/ from today's src/data/*.h and src/xlat/*.c
    python tools/gen/shapes.py check     the shapes in the tree match today's files' declarations (exit 1 if not)
    python tools/gen/shapes.py install   copy a shape into src/data / src/xlat for every generated file that is missing

The oracle (build/ct.exe) that replays the recipes is compiled with the rest of the engine, and the engine includes the
generated tables (src/data/*.h) and translated stages (src/xlat/*.c) -- which the oracle itself makes. A fresh clone
has none of them, so tools/setup.py first installs these SHAPES, builds a bootstrap oracle from them (the oracle's own
modes -- --peek, --fb, --cputrace, --teleport, ... -- never read the tables), generates the real files with it, and
rebuilds everything.

A shape keeps a header's include guard, #defines of numbers, types and every array's dimensions; every initializer is
replaced by {0} (an array whose first dimension the data implied gets the count written out) and every comment is
dropped. A translated stage's shape is its entry point returning CT_XLAT_MISS. So a shape holds no byte of the game:
tools/release/scan_rom_leaks.py checks them like every other file.
"""
import glob
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHAPES = os.path.join(ROOT, 'tools', 'gen', 'shapes')


def strip_comments(s):
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c in '"\'':                                   # a literal: copy it whole
            j = i + 1
            while j < n and s[j] != c:
                j += 2 if s[j] == '\\' else 1
            out.append(s[i:j + 1]); i = j + 1
        elif s.startswith('//', i):
            j = s.find('\n', i); i = n if j < 0 else j
        elif s.startswith('/*', i):
            j = s.find('*/', i + 2); i = n if j < 0 else j + 2
        else:
            out.append(c); i += 1
    return ''.join(out)


def top_count(body):
    """How many elements an initializer's outer braces hold (commas at depth 1, a trailing comma ignored)."""
    depth, n, item, i = 0, 0, False, 0
    while i < len(body):
        c = body[i]
        if c in '"\'':
            j = i + 1
            while j < len(body) and body[j] != c:
                j += 2 if body[j] == '\\' else 1
            i = j + 1; item = True; continue
        if c == '{':
            depth += 1
            if depth == 2: item = True
        elif c == '}':
            depth -= 1
        elif c == ',' and depth == 1:
            n += item; item = False
        elif not c.isspace() and depth >= 1:
            item = True
        i += 1
    return n + item


DECL = re.compile(r'((?:static\s+)?(?:const\s+)?[A-Za-z_][\w\s\*]*?\b[A-Za-z_]\w*\s*((?:\[[^\]]*\])+)?)\s*=\s*', re.S)


def shape_header(text):
    s = strip_comments(text)
    out, i = [], 0
    while True:
        m = DECL.search(s, i)
        if not m:
            out.append(s[i:]); break
        j = m.end()
        out.append(s[i:m.start()])
        decl, dims = m.group(1), m.group(2)
        if s[j] == '{':                                   # an aggregate initializer: find its end
            depth, k = 0, j
            while True:
                c = s[k]
                if c in '"\'':
                    q = k + 1
                    while s[q] != c:
                        q += 2 if s[q] == '\\' else 1
                    k = q
                elif c == '{':
                    depth += 1
                elif c == '}':
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            body = s[j:k + 1]
            if dims and dims.startswith('[]'):
                decl = decl.replace('[]', '[%d]' % top_count(body), 1)
            out.append(decl + ' = {0}')
            i = k + 1
        else:                                             # a scalar: keep numbers, blank anything else
            k = s.find(';', j)
            val = s[j:k].strip()
            out.append(decl + ' = ' + (val if re.fullmatch(r'[-+]?(0x[0-9A-Fa-f]+|\d+)[uUlL]*', val) else '0'))
            i = k
    s = ''.join(out)
    s = re.sub(r'(#define\s+\w+\s+)"[^"]*"', r'\1""', s)  # a #define'd string keeps its name only
    s = re.sub(r'\n\s*\n+', '\n', s)
    return ('/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.\n'
            '   tools/setup.py replaces it with the file generated from your ROM. */\n' + s.strip() + '\n')


def shape_xlat(text):
    names = re.findall(r'^int (CtXlat_\w+)\(CtXlat\* x\)', text, re.M)
    body = '\n'.join('int %s(CtXlat* x) { (void)x; return CT_XLAT_MISS; }' % n for n in names)
    return ('/* SHAPE of a generated file (tools/gen/shapes.py): the entry point only.\n'
            '   tools/setup.py replaces it with the stage translated from your ROM. */\n'
            '#include "../seq_xlat.h"\n' + body + '\n')


def generated():
    return sorted(glob.glob(os.path.join(ROOT, 'src', 'data', '*.h')) + glob.glob(os.path.join(ROOT, 'src', 'xlat', '*.c')))


def shape_of(path):
    text = open(path, encoding='utf-8', errors='replace').read()
    return shape_xlat(text) if path.endswith('.c') else shape_header(text)


def shape_path(path):
    return os.path.join(SHAPES, os.path.relpath(path, os.path.join(ROOT, 'src')).replace('\\', '/'))


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'check'
    if cmd == 'install':
        n = 0
        for sp in glob.glob(os.path.join(SHAPES, '*', '*')):
            dst = os.path.join(ROOT, 'src', os.path.relpath(sp, SHAPES))
            if not os.path.exists(dst):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(sp, dst); n += 1
        print('shapes: %d installed' % n)
        return 0
    bad = 0
    for p in generated():
        sp, sh = shape_path(p), shape_of(p)
        if cmd == 'make':
            os.makedirs(os.path.dirname(sp), exist_ok=True)
            open(sp, 'w', encoding='utf-8', newline='\n').write(sh)
        elif not os.path.exists(sp) or open(sp, encoding='utf-8').read() != sh:
            bad += 1
            print('shapes: %s is stale (python tools/gen/shapes.py make)' % os.path.relpath(sp, ROOT))
    print('shapes: %d files %s' % (len(generated()), 'written' if cmd == 'make' else ('match' if not bad else '-- %d stale' % bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
