#!/usr/bin/env python3
r"""Find the suite's stale mutations and re-target the ones a refactor only moved.

    python tools/mutation_retarget.py [--cross-file] [--write] [--names-out FILE]
    python tools/mutation_retarget.py --self-test

A mutation (tools/regress_checks.json, `--mutate`) names an exact `find` text in a source file; when the code around it
is refactored (say `h->pcDefer[k]` becomes `BT_PCDEFER(h, k)`) the text is gone, `--mutate` reports it STALE, and the
check is no longer proven to grade anything. For each stale edit this:

  1. finds the source lines that best match the old `find` (difflib, the same number of lines, line by line); the match
     must be good (ratio >= 0.6), clearly the best (the next best scores at most 0.9 of it), and occur once in the file;
  2. takes the drift (the token edits from the old find to those lines) and the mutation (the token edits from the
     old find to the old replace) and composes them: the mutation's edits are applied to the new lines, at the
     places the drift left unchanged. When a mutation edit touches a token the drift changed, nothing is guessed: the
     edit is tried as a substitution instead (the drift's token rewrites, `h->pcDefer[k]` -> `BT_PCDEFER(h, k)`,
     applied to the old replace, only when they turn the old find into the new one exactly), and else reported for
     a person;
  3. refuses a replace that names an identifier the file no longer has (outside comments), then prints old and new;
     with --write it rewrites the edit's find/replace and adds a line to the check's notes.

A re-targeted mutation is a claim, not a proof: run `python tools/regress.py --mutate NAME ...` on the checks it names
(--names-out writes them to a file); a mutation that survives there is the check's or the mutation's to re-derive.
An edit whose find occurs twice is reported, not re-targeted: which site it meant is for a person to say.

--cross-file follows code a refactor moved into another file (a split of field.c, ct_vm.c, menu.c, ...). The pool is
every source file under the top-level folders the suite's mutations already name (src, snes, tools, assets today), of
the same language as the edit's file (.c/.h together, .py alone). An edit whose find is gone from its file (or whose
file is gone) and occurs exactly once in the pool, with tools/regress.py's line-ending fit, keeps its find and replace
and only has its `file` rewritten; 2+ hits are reported for a person. With no exact hit, step 1 also scores the pool's
files that hold most of the find's identifiers next to the edit's own file, so an edit both moved and re-worded is
re-targeted in the file the best match is in (the same rules: clearly the best across every file scored, once in
the whole pool). Without --cross-file nothing outside the edit's own file is read, so run it with --cross-file after
a split: alone, step 1 can re-target a moved function's find onto a sibling left in the old file (the self-test's
t-moved shows it). --self-test runs both passes on synthetic files in memory (the resolver check in a temporary
folder) and touches nothing in the tree.
"""
import argparse
import difflib
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regress  # noqa: E402

IDENT = re.compile(r'\b[A-Za-z_][A-Za-z0-9_]*\b')
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
TOKEN = re.compile(r'[A-Za-z_][A-Za-z0-9_]*|0[xX][0-9A-Fa-f]+|\d+|\s+|.', re.S)


def tokens(s):
    return TOKEN.findall(s)


def count(text, find):
    n = text.count(find)
    if n == 0 and '\n' in find:                         # tools/regress.py fits the find to the file's line endings
        n = text.count(find.replace('\r\n', '\n').replace('\n', '\r\n')) or text.count(find.replace('\r\n', '\n'))
    return n


def best_window(text, find):
    """(new find, ratio, runner-up ratio): the run of whole lines of `text` most like `find`, scored line by line (the
    mean of each line's ratio against the find's line in the same place), so one line of the find that only one site
    has decides between two sites that share the rest."""
    lines = text.split('\n')
    want = [l.strip() for l in find.split('\n')]
    k = len(want)
    sm = difflib.SequenceMatcher(None, '', want[0], autojunk=False)
    firsts = []
    for i in range(0, len(lines) - k + 1):
        sm.set_seq1(lines[i].strip())
        if sm.real_quick_ratio() >= 0.5 and sm.quick_ratio() >= 0.5 and sm.ratio() >= 0.5:
            firsts.append(i)
    scored = []
    for i in firsts:
        rs = [difflib.SequenceMatcher(None, lines[i + j].strip(), want[j], autojunk=False).ratio() for j in range(k)]
        scored.append((sum(rs) / k, i))
    if not scored:
        return None, 0.0, 0.0
    scored.sort(reverse=True)
    r, i = scored[0]
    return '\n'.join(lines[i:i + k]), r, (scored[1][0] if len(scored) > 1 else 0.0)


def compose(old_find, new_find, old_repl):
    """The new replace: old_find -> old_repl's token edits applied to new_find where old_find -> new_find kept the
    tokens. None if a mutation edit touches drifted tokens."""
    T, U, R = tokens(old_find), tokens(new_find), tokens(old_repl)
    drift = difflib.SequenceMatcher(None, T, U, autojunk=False)
    t2u = {}
    for a, b, size in drift.get_matching_blocks():
        for d in range(size):
            t2u[a + d] = b + d
    edits = [op for op in difflib.SequenceMatcher(None, T, R, autojunk=False).get_opcodes() if op[0] != 'equal']
    out, repl = list(U), []
    for tag, i1, i2, j1, j2 in edits:
        if i2 > i1:                                   # replaced or deleted tokens: every one must be unchanged by drift
            if any(i not in t2u for i in range(i1, i2)):
                return None
            u1, u2 = t2u[i1], t2u[i2 - 1] + 1
            if u2 - u1 != i2 - i1:
                return None
        else:                                         # an insertion before T[i1]: anchor on a kept neighbour
            if i1 in t2u:
                u1 = u2 = t2u[i1]
            elif i1 - 1 in t2u:
                u1 = u2 = t2u[i1 - 1] + 1
            else:
                return None
        repl.append((u1, u2, R[j1:j2]))
    for u1, u2, new in sorted(repl, reverse=True):
        out[u1:u2] = new
    return ''.join(out)


def substitute(old_find, new_find, old_repl):
    """The new replace when the mutation edits tokens the refactor rewrote: the drift's token substitutions
    (`h -> pcDefer [` -> `BT_PCDEFER ( h ,`) applied to the old replace. Only when the same substitutions turn the old
    find into the new find exactly -- they explain the whole drift -- else None. In the old replace a substitution is
    made only where the replace still holds the find's own text at a spot where the drift made it: a comment's
    `ChangeLocation. Applies` -> `ChangeLocation; applies` is a `.` -> `;` substitution, and applied to every `.` of the
    replace it turned the mutation's `wd.have` into `wd;have`, which does not build."""
    T, U = tokens(old_find), tokens(new_find)
    subs = [(tuple(T[i1:i2]), tuple(U[j1:j2])) for tag, i1, i2, j1, j2
            in difflib.SequenceMatcher(None, T, U, autojunk=False).get_opcodes() if tag == 'replace']
    if not subs or any(not a for a, _ in subs):
        return None
    subs = sorted(set(subs), key=lambda ab: -len(ab[0]))
    if len({a for a, _ in subs}) != len(subs):           # one old run rewritten two ways: not a substitution
        return None

    def apply(toks, allowed):
        """toks with the substitutions made, at the token indices allowed(i, a) accepts; the indices they fired at."""
        out, i, fired = [], 0, set()
        while i < len(toks):
            for a, b in subs:
                if tuple(toks[i:i + len(a)]) == a and allowed(i, a):
                    out.extend(b)
                    fired.add((i, a))
                    i += len(a)
                    break
            else:
                out.append(toks[i])
                i += 1
        return out, fired
    U2, in_find = apply(T, lambda i, a: True)
    if ''.join(U2) != new_find:
        return None
    R = tokens(old_repl)
    r2t = {}                                          # a replace token -> the same token of the find, where they align
    for x, y, size in difflib.SequenceMatcher(None, T, R, autojunk=False).get_matching_blocks():
        for d in range(size):
            r2t[y + d] = x + d
    out, _ = apply(R, lambda i, a: all(i + d in r2t and r2t[i + d] == r2t[i] + d for d in range(len(a)))
                   and (r2t[i], a) in in_find)
    return ''.join(out)


def family(f):
    """The language of a source path: .c and .h are one (a split moves code between them), others alone."""
    ext = os.path.splitext(f)[1]
    return '.c/.h' if ext in ('.c', '.h') else ext


def source_pool(root, data):
    """Where --cross-file looks: every source file under the top-level folders the suite's mutations name, with an
    extension they name, as forward-slash paths relative to root (the form of an edit's `file`). Source means what git
    tracks or would track (a new file a split has not committed yet): the ROM-derived src/data/*.h and the other
    ignored files differ from machine to machine, and tools/gen/shapes/ only repeats the generated headers' shapes."""
    files = [e['file'] for c in data['checks'] for m in c.get('mutations', []) for e in (m.get('edits') or [m])]
    tops = sorted({f.split('/')[0] for f in files if '/' in f})
    exts = {os.path.splitext(f)[1] for f in files}
    ls = subprocess.run(['git', '-C', root, 'ls-files', '--cached', '--others', '--exclude-standard', '-z', '--'] + tops,
                        capture_output=True, check=True).stdout.decode('utf-8')
    return sorted({f for f in ls.split('\0') if f and os.path.splitext(f)[1] in exts
                   and not f.startswith('tools/gen/shapes/') and os.path.isfile(os.path.join(root, f))})


def retarget(data, text, pool, cross=False, write=False, stamp='', out=print):
    """One pass over data's stale edits: (stale, moved, manual, names). text(f) is the source at path f (None when it
    is gone); pool the paths --cross-file may look in. With write the edits and notes of data change in place; saving
    them is the caller's."""
    idents = {}

    def has(f):                                     # every identifier of pool file f, comments too: a cheap filter
        if f not in idents:
            idents[f] = set(IDENT.findall(text(f) or ''))
        return idents[f]

    def note(c, line):
        if line not in c.get('notes', ''):
            c['notes'] = c.get('notes', '') + line

    stale = moved = manual = refiled = 0
    names = []
    for c in data['checks']:
        for mi, m in enumerate(c.get('mutations', [])):
            for ei, e in enumerate(m.get('edits') or [m]):
                s = text(e['file'])
                n = count(s, e['find']) if s is not None else -1
                if n == 1:
                    continue
                stale += 1
                where = '%s [%d]%s %s' % (c['name'], mi, '.%d' % ei if m.get('edits') else '', e['file'])
                if n > 0 or (n < 0 and not cross):
                    manual += 1
                    out('manual: %s -- find occurs %d times' % (where, n))
                    continue
                others = [f for f in pool if f != e['file'] and family(f) == family(e['file'])] if cross else []
                if cross:                               # moved verbatim: the find, once in the whole pool
                    hits = [(f, k) for f, k in ((f, count(text(f), e['find'])) for f in others) if k]
                    total = sum(k for _, k in hits)
                    if total > 1:
                        manual += 1
                        out('manual: %s -- find gone from it but occurs %d times elsewhere (%s)' % (
                            where, total, ', '.join(f for f, _ in hits[:4])))
                        continue
                    if total == 1:
                        to = hits[0][0]
                        moved += 1
                        refiled += 1
                        out('file moved: %s -> %s\n    find  %s' % (where, to, e['find'].strip()[:150]))
                        if write:
                            note(c, ' ==== MUTATION [%d] FILE MOVED %s (tools/mutation_retarget.py): file moved: '
                                    '%s -> %s; its find is there verbatim, the same edit.' % (mi, stamp, e['file'], to))
                            e['file'] = to
                        if c['name'] not in names:
                            names.append(c['name'])
                        continue
                # (ratio, runner-up in the same file, file, window): the edit's own file, and with --cross-file the
                # pool's files that hold most of the find's longer code identifiers (moved and re-worded)
                scored = []
                if s is not None:
                    w, r, r2 = best_window(s, e['find'])
                    scored.append((r, r2, e['file'], w))
                want = {x for x in IDENT.findall(COMMENT.sub(' ', e['find'])) if len(x) >= 4}
                if cross and len(want) >= 2:
                    for f in others:
                        if len(want & has(f)) >= 0.8 * len(want):
                            w, r, r2 = best_window(text(f), e['find'])
                            if w is not None:
                                scored.append((r, r2, f, w))
                scored.sort(key=lambda t: -t[0])
                if scored:
                    r, r2, to, new_find = scored[0]
                    r2 = max([r2] + [t[0] for t in scored[1:]])
                else:
                    r, r2, to, new_find = 0.0, 0.0, e['file'], None
                st = text(to)
                hits = (count(st, new_find) if not cross else
                        sum(count(text(f), new_find) for f in [e['file']] + others if text(f) is not None)) \
                    if new_find is not None else 0
                why = None
                if new_find is None or r < 0.6:
                    why = 'no line is like it (best %.2f)' % r
                elif r2 > 0.9 * r:
                    why = 'two places are as like it (%.2f, %.2f)' % (r, r2)
                elif hits != 1:
                    why = 'its best match occurs %d times' % hits
                new_repl = None if why else (compose(e['find'], new_find, e['replace'])
                                             or substitute(e['find'], new_find, e['replace']))
                if not why and (new_repl is None or new_repl == new_find):
                    why = 'the mutation edits what the refactor changed'
                if not why:
                    gone = sorted(set(IDENT.findall(COMMENT.sub(' ', new_repl))) - set(IDENT.findall(st)))
                    if gone:
                        why = 'its replace names what the file no longer has (%s)' % ', '.join(gone[:4])
                if why:
                    manual += 1
                    out('manual: %s -- %s' % (where, why))
                    continue
                moved += 1
                hop = '' if to == e['file'] else ', file moved: %s -> %s' % (e['file'], to)
                refiled += 1 if hop else 0
                out('moved: %s (%.2f)%s\n    find  %s\n    ->    %s\n    repl  %s' % (
                    where, r, hop, e['find'].strip()[:150], new_find.strip()[:150], new_repl.strip()[:150]))
                if write:
                    note(c, ' ==== MUTATION [%d] RE-TARGETED %s (tools/mutation_retarget.py): its find was gone after a '
                            'refactor; the same edit on the code that replaced it%s.'
                            % (mi, stamp, hop.replace(',', ';', 1)))
                    e['find'], e['replace'] = new_find, new_repl
                    e['file'] = to
                if c['name'] not in names:
                    names.append(c['name'])
    out('retarget: %d stale edit(s): %d moved, %d for a person%s' % (
        stale, moved, manual, ' (%d of the moved to another file)' % refiled if cross else ''))
    return stale, moved, manual, names


def lf(s):
    """A source as the finds are written: LF. A find must match an LF checkout too (the CI, Linux, a cloud session);
    regress.py fits an LF find to a CRLF working copy, but never strips the \\r a find copied from one carries."""
    return s.replace('\r\n', '\n') if s is not None else None


def self_test():
    """Both passes on synthetic files in memory: a function moved verbatim to a new file (CRLF there, an LF find), a
    find that two files now hold, a function moved AND re-worded, an edit whose file is gone; then the re-targeted
    mutations through tools/regress.py's own resolver, in a temporary folder."""
    import copy
    import tempfile
    gato = ('static int battleGatoRender(Ctx *ctx, int frame)\n{\n'
            '    int poseIndex = ctx->gatoPose[frame & 7];   // C1/2A40 pose table\n'
            '    ctx->spriteBank = poseIndex + 0x40;\n'
            '    return drawGatoSprite(ctx, poseIndex);\n}\n')
    tank = gato.replace('Gato', 'Tank').replace('gato', 'tank').replace('& 7', '& 3') \
               .replace('2A40', '2B10').replace('0x40', '0x20')
    texts = {
        'src/a.c': '#include "a.h"\n\n' + tank,                         # after the split: only the sibling is left
        'src/b.c': ('#include "b.h"\n\n' + gato).replace('\n', '\r\n'),   # the moved function, CRLF
        'src/c.c': 'int gatoAgain(Ctx *ctx, int poseIndex)\n{\n    return drawGatoSprite(ctx, poseIndex);\n}\n',
        'src/d.c': ('void battleDissolveStep(Ctx *ctx)\n{\n'
                    '    ctx->dissolveStep = ctx->dissolveBase + 2;   // CD/1A20 adds two lines each frame\n}\n'),
        'tools/x.py': 'ctx.dissolveStep = ctx.dissolveBase + 2\n',
    }
    find1 = ('    int poseIndex = ctx->gatoPose[frame & 7];   // C1/2A40 pose table\n'
             '    ctx->spriteBank = poseIndex + 0x40;')
    data = {'checks': [
        {'name': 't-moved', 'notes': '', 'mutations': [
            {'file': 'src/a.c', 'find': find1, 'replace': find1.replace('0x40', '0x41'), 'why': 'bank +1'}]},
        {'name': 't-twice', 'notes': '', 'mutations': [{'edits': [
            {'file': 'src/a.c', 'find': 'return drawTankSprite(ctx, poseIndex);',
             'replace': 'return 0;'},
            {'file': 'src/a.c', 'find': 'return drawGatoSprite(ctx, poseIndex);', 'replace': 'return 0;'}]}]},
        {'name': 't-reworded', 'notes': '', 'mutations': [
            {'file': 'src/a.c',
             'find': '    ctx->dissolveStep = ctx->dissolveBase + 2;   // CD/1A20: two lines a frame',
             'replace': '    ctx->dissolveStep = ctx->dissolveBase + 3;   // CD/1A20: two lines a frame'}]},
        {'name': 't-gone', 'notes': '', 'mutations': [
            {'file': 'src/gone.c', 'find': 'ctx->spriteBank = poseIndex + 0x40;',
             'replace': 'ctx->spriteBank = poseIndex + 0x00;'}]},
    ]}
    pool = sorted(texts)
    fails = []

    def check(what, ok):
        print('self-test: %s %s' % ('PASS' if ok else 'FAIL', what))
        if not ok:
            fails.append(what)

    def edit(d, name, ei=0):
        m = [c for c in d['checks'] if c['name'] == name][0]['mutations'][0]
        return (m.get('edits') or [m])[ei]

    plain = copy.deepcopy(data)
    print('self-test: without --cross-file')
    retarget(plain, texts.get, pool, cross=False, write=True, stamp='TEST', out=lambda l: print('    ' + l))
    check('no edit leaves its file without --cross-file',
          all(edit(plain, n)['file'] == edit(data, n)['file'] for n in ('t-moved', 't-reworded', 't-gone')))
    moved = copy.deepcopy(data)
    print('self-test: with --cross-file')
    stale, nmoved, manual, names = retarget(moved, lambda f: lf(texts.get(f)), pool, cross=True, write=True,
                                            stamp='TEST', out=lambda l: print('    ' + l))
    check('no re-targeted find or replace carries a CR (src/b.c is CRLF)',
          not any('\r' in e[k] for n in ('t-moved', 't-reworded', 't-gone') for e in [edit(moved, n)]
                  for k in ('find', 'replace')))
    check('a comment-only drift does not rewrite the replace\'s code (`.` -> `;` would make wd;have)',
          substitute('  init(&w);   // a ChangeLocation. Applies', '  init(&w);   // a ChangeLocation; applies',
                     '  if (!w.have) init(&w);   // MUT') == '  if (!w.have) init(&w);   // MUT')
    e = edit(moved, 't-moved')
    check('moved verbatim: file src/a.c -> src/b.c, find and replace unchanged',
          e['file'] == 'src/b.c' and e['find'] == find1 and e['replace'] == find1.replace('0x40', '0x41'))
    check('moved verbatim: the note says "file moved: src/a.c -> src/b.c"',
          'file moved: src/a.c -> src/b.c' in moved['checks'][0]['notes'])
    check('2+ hits elsewhere: refused, the edit untouched', edit(moved, 't-twice', 1) == edit(data, 't-twice', 1))
    e = edit(moved, 't-reworded')
    check('moved and re-worded: src/a.c -> src/d.c, the new line, the same edit on it',
          e['file'] == 'src/d.c' and e['find'] == texts['src/d.c'].split('\n')[2]
          and e['replace'] == e['find'].replace('+ 2', '+ 3'))
    check('a gone file: src/gone.c -> src/b.c', edit(moved, 't-gone')['file'] == 'src/b.c')
    check('counts: 4 stale, 3 moved, 1 for a person; names t-moved t-reworded t-gone',
          (stale, nmoved, manual, names) == (4, 3, 1, ['t-moved', 't-reworded', 't-gone']))
    with tempfile.TemporaryDirectory() as root:
        for f, s in texts.items():
            os.makedirs(os.path.dirname(os.path.join(root, f)), exist_ok=True)
            with open(os.path.join(root, f), 'w', encoding='utf-8', newline='') as fh:
                fh.write(s)
        for n in names:
            why = regress._resolve_mutation(root, [c for c in moved['checks'] if c['name'] == n][0]['mutations'][0])[0]
            check('regress.py resolves %s (%s)' % (n, why or 'one hit'), why is None)
    print('self-test: %s' % ('FAILED: %d' % len(fails) if fails else 'OK'))
    return 1 if fails else 0


def main():
    if hasattr(sys.stdout, 'reconfigure'):            # a find may carry a glyph the console's code page has not
        sys.stdout.reconfigure(errors='backslashreplace')
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--cross-file', action='store_true', help='follow finds moved verbatim (or moved and re-worded) '
                    'into another source file; rewrites only the edit\'s file')
    ap.add_argument('--names-out')
    ap.add_argument('--self-test', action='store_true', help='run both passes on synthetic files in memory')
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    data = regress.load_checks()
    texts = {}

    def text(f):
        if f not in texts:
            p = os.path.join(regress.ROOT, f)
            texts[f] = lf(open(p, encoding='utf-8', errors='replace', newline='').read() if os.path.exists(p) else None)
        return texts[f]

    pool = source_pool(regress.ROOT, data) if a.cross_file else []
    stale, moved, manual, names = retarget(data, text, pool, cross=a.cross_file, write=a.write,
                                           stamp=time.strftime('%Y-%m-%d'))
    if a.write and moved:
        regress.write_checks(data)
        print('retarget: wrote %d re-targeted edit(s); now --mutate the checks they name' % moved)
    if a.names_out:
        with open(a.names_out, 'w', encoding='utf-8') as f:
            f.write(' '.join(names) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
