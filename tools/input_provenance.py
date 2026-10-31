#!/usr/bin/env python3
r"""Where every state file the suite reads comes from: nothing may exist on one machine only.

    python tools/input_provenance.py            the verdict (the suite's check `inputs-come-from-recipes-or-sources`)
    python tools/input_provenance.py --list     every saves/ input with its origin
    python tools/input_provenance.py --write    rewrite tools/private_captures.json from what the checks still read

A check's `saves/...` input must be one of:
  recipe       made from the ROM by a recipe: an entry of tools/fixtures/<domain>.json (python tools/fixtures.py make all)
  third-party  a public battery save: tools/third_party_saves.json (python tools/fetch_saves.py)
  private      a local capture still waiting for its recipe (docs/ROADMAP.md 1.1), listed in
               tools/private_captures.json -- the list only shrinks: a check that reads a file in none of the three
               fails here, and so does a list entry no check reads any more (take it off)
Inputs are found in a check's `inputs`, its argv and its env (the paths a tool takes as arguments). A path a tool opens
by itself is not seen here; such tools name their inputs in `inputs`.
"""
import argparse
import glob
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRIVATE = os.path.join(ROOT, 'tools', 'private_captures.json')
SAVE = re.compile(r'saves/[^\s,:;"\'()]+')


def jload(p):
    return json.load(io.open(p, encoding='utf-8'))


def refs(k):
    s = set(i for i in k.get('inputs', []) if i.startswith('saves/'))
    for a in k.get('argv', []) + list((k.get('env') or {}).values()):
        s |= set(SAVE.findall(str(a)))
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--write', action='store_true')
    a = ap.parse_args()
    checks = jload(os.path.join(ROOT, 'tools', 'regress_checks.json'))['checks']
    recipe = {f['path'] for p in glob.glob(os.path.join(ROOT, 'tools', 'fixtures', '*.json')) for f in jload(p)['fixtures']}
    third = set()
    for f in jload(os.path.join(ROOT, 'tools', 'third_party_saves.json'))['files']:
        third.add(f['path'])
        third |= {e['path'] for e in f.get('extract', [])}
    private = {e['path']: e for e in jload(PRIVATE)['captures']} if os.path.exists(PRIVATE) else {}

    def origin(p):
        # a fixture of kind `peek` writes sidecars beside its path (.wram/.vram/.oam): the same origin
        base = re.sub(r'\.(wram|vram|oam)$', '', p)
        if p in recipe or base in recipe:
            return 'recipe'
        if p in third:
            return 'third-party'
        if p in private or base in private:
            return 'private'
        return None

    used = {}
    for k in checks:
        for p in refs(k):
            used.setdefault(p, []).append(k['name'])
    if a.write:
        caps = [{'path': p, 'checks': len(v)} for p, v in sorted(used.items()) if origin(p) in (None, 'private')]
        doc = jload(PRIVATE)['doc'] if os.path.exists(PRIVATE) else ''
        io.open(PRIVATE, 'w', encoding='utf-8', newline='').write(
            json.dumps({'doc': doc, 'captures': caps}, indent=2, ensure_ascii=False) + '\n')
        print('input-provenance: wrote %d private capture(s)' % len(caps))
        return 0
    by = {}
    for p in sorted(used):
        o = origin(p) or 'UNCLASSIFIED'
        by.setdefault(o, []).append(p)
        if a.list:
            print('%-12s %s  (%d check(s))' % (o, p, len(used[p])))
    for p in by.get('UNCLASSIFIED', []):
        print('input-provenance: UNCLASSIFIED %s -- read by %s: make it from a recipe (tools/fixtures/) or a public '
              'source (tools/third_party_saves.json)' % (p, ', '.join(sorted(used[p])[:3])))
    stale = sorted(set(private) - set(used))
    for p in stale:
        print('input-provenance: STALE %s -- no check reads it any more: take it off tools/private_captures.json' % p)
    nprivchecks = len({c for p in by.get('private', []) for c in used[p]})
    print('input-provenance: %d check(s) read %d state file(s): %d made by recipes, %d third-party, %d private '
          'capture(s) read by %d check(s); %d unclassified, %d stale'
          % (len({c for v in used.values() for c in v}), len(used), len(by.get('recipe', [])), len(by.get('third-party', [])),
             len(by.get('private', [])), nprivchecks, len(by.get('UNCLASSIFIED', [])), len(stale)))
    return 1 if by.get('UNCLASSIFIED') or stale else 0


if __name__ == '__main__':
    sys.exit(main())
