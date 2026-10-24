#!/usr/bin/env python3
r"""Find the suite's STALE mutations and re-target the ones a refactor only moved.

    python tools/mutation_retarget.py [--write] [--names-out FILE]

A mutation (tools/regress_checks.json, `--mutate`) names an exact `find` text in a source file; when the code around it
is refactored -- `h->pcDefer[k]` became `BT_PCDEFER(h, k)` -- the text is gone, `--mutate` reports it STALE, and the
check is no longer proven to grade anything. For each stale edit this:

  1. finds the source lines that best match the old `find` (difflib, the same number of lines, line by line); the match
     must be good (ratio >= 0.6), clearly the best (the next best scores at most 0.9 of it), and occur once in the file;
  2. takes the DRIFT -- the token edits from the old find to those lines -- and the MUTATION -- the token edits from the
     old find to the old replace -- and composes them: the mutation's edits are applied to the new lines, at the
     places the drift left unchanged. When a mutation edit touches a token the drift changed, nothing is guessed: the
     edit is tried as a SUBSTITUTION instead -- the drift's token rewrites (`h->pcDefer[k]` -> `BT_PCDEFER(h, k)`)
     applied to the old replace, only when they turn the old find into the new one exactly -- and else reported for
     a person;
  3. refuses a replace that names an identifier the file no longer has (outside comments), then prints old and new;
     with --write it rewrites the edit's find/replace and adds a line to the check's notes.

A re-targeted mutation is a claim, not a proof: run `python tools/regress.py --mutate NAME ...` on the checks it names
(--names-out writes them to a file) -- a mutation that SURVIVES there is the check's or the mutation's to re-derive.
An edit whose find occurs twice is reported, not re-targeted: which site it meant is for a person to say.
"""
import argparse
import difflib
import json
import os
import re
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
    find into the new find exactly -- they explain the whole drift -- else None."""
    T, U = tokens(old_find), tokens(new_find)
    subs = [(tuple(T[i1:i2]), tuple(U[j1:j2])) for tag, i1, i2, j1, j2
            in difflib.SequenceMatcher(None, T, U, autojunk=False).get_opcodes() if tag == 'replace']
    if not subs or any(not a for a, _ in subs):
        return None
    subs = sorted(set(subs), key=lambda ab: -len(ab[0]))
    if len({a for a, _ in subs}) != len(subs):           # one old run rewritten two ways: not a substitution
        return None

    def apply(toks):
        out, i = [], 0
        while i < len(toks):
            for a, b in subs:
                if tuple(toks[i:i + len(a)]) == a:
                    out.extend(b)
                    i += len(a)
                    break
            else:
                out.append(toks[i])
                i += 1
        return out
    if ''.join(apply(T)) != new_find:
        return None
    return ''.join(apply(tokens(old_repl)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--names-out')
    a = ap.parse_args()
    data = regress.load_checks()
    texts = {}

    def text(f):
        if f not in texts:
            p = os.path.join(regress.ROOT, f)
            texts[f] = open(p, encoding='utf-8', errors='replace', newline='').read() if os.path.exists(p) else None
        return texts[f]

    stale = moved = manual = 0
    names = []
    stamp = time.strftime('%Y-%m-%d')
    for c in data['checks']:
        for mi, m in enumerate(c.get('mutations', [])):
            for ei, e in enumerate(m.get('edits') or [m]):
                s = text(e['file'])
                n = count(s, e['find']) if s is not None else -1
                if n == 1:
                    continue
                stale += 1
                where = '%s [%d]%s %s' % (c['name'], mi, '.%d' % ei if m.get('edits') else '', e['file'])
                if n != 0:
                    manual += 1
                    print('manual: %s -- find occurs %d times' % (where, n))
                    continue
                new_find, r, r2 = best_window(s, e['find'])
                why = None
                if new_find is None or r < 0.6:
                    why = 'no line is like it (best %.2f)' % r
                elif r2 > 0.9 * r:
                    why = 'two places are as like it (%.2f, %.2f)' % (r, r2)
                elif count(s, new_find) != 1:
                    why = 'its best match occurs %d times' % count(s, new_find)
                new_repl = None if why else (compose(e['find'], new_find, e['replace'])
                                             or substitute(e['find'], new_find, e['replace']))
                if not why and (new_repl is None or new_repl == new_find):
                    why = 'the mutation edits what the refactor changed'
                if not why:
                    gone = sorted(set(IDENT.findall(COMMENT.sub(' ', new_repl))) - set(IDENT.findall(s)))
                    if gone:
                        why = 'its replace names what the file no longer has (%s)' % ', '.join(gone[:4])
                if why:
                    manual += 1
                    print('manual: %s -- %s' % (where, why))
                    continue
                moved += 1
                print('moved: %s (%.2f)\n    find  %s\n    ->    %s\n    repl  %s' % (
                    where, r, e['find'].strip()[:150], new_find.strip()[:150], new_repl.strip()[:150]))
                if a.write:
                    e['find'], e['replace'] = new_find, new_repl
                    note = (' ==== MUTATION [%d] RE-TARGETED %s (tools/mutation_retarget.py): its find was gone after a '
                            'refactor; the same edit on the code that replaced it.' % (mi, stamp))
                    if note not in c.get('notes', ''):
                        c['notes'] = c.get('notes', '') + note
                if c['name'] not in names:
                    names.append(c['name'])
    print('retarget: %d stale edit(s): %d moved, %d for a person' % (stale, moved, manual))
    if a.write and moved:
        regress.write_checks(data)
        print('retarget: wrote %d re-targeted edit(s); now --mutate the checks they name' % moved)
    if a.names_out:
        with open(a.names_out, 'w', encoding='utf-8') as f:
            f.write(' '.join(names) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
