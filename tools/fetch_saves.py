#!/usr/bin/env python3
r"""The third-party battery saves the suite and the recipes start from, downloaded from their public sources.

    python tools/fetch_saves.py             download what is missing or wrong, verify every file
    python tools/fetch_saves.py --verify    only check the files on disk against tools/third_party_saves.json

A battery save (the cartridge's 8 KB SRAM) is a player's progress -- the party, the items, the story flags -- not game
data, but these are other people's files: they are never committed. tools/third_party_saves.json lists each one with its
public URL and SHA-256 (an archive also lists the member it extracts and that member's SHA-256), so any machine that runs
this gets the same bytes, and a file that differs from its hash is downloaded again rather than trusted.

Exit status: 0 when every file is present and verified, 1 otherwise.
"""
import argparse
import hashlib
import io
import json
import os
import sys
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, 'tools', 'third_party_saves.json')


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def good(rel, want):
    p = os.path.join(ROOT, rel)
    return os.path.exists(p) and sha256(open(p, 'rb').read()) == want


def write(rel, data):
    p = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'wb') as f:
        f.write(data)


def fetch(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'BelthasarProject fetch_saves.py'})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--verify', action='store_true', help='check the files on disk only; download nothing')
    a = ap.parse_args()
    files = json.load(io.open(MANIFEST, encoding='utf-8'))['files']
    ok = got = bad = 0
    for f in files:
        ex = f.get('extract', [])
        if good(f['path'], f['sha256']) and all(good(e['path'], e['sha256']) for e in ex):
            ok += 1
            continue
        if a.verify:
            print('fetch-saves: MISSING or WRONG %s' % f['path'])
            bad += 1
            continue
        data = open(os.path.join(ROOT, f['path']), 'rb').read() if good(f['path'], f['sha256']) else None
        if data is None:
            try:
                data = fetch(f['url'])
            except Exception as e:   # (a network error is reported, not fatal: the others may still come)
                print('fetch-saves: FAILED %s (%s)' % (f['url'], e))
                bad += 1
                continue
            if sha256(data) != f['sha256']:
                print('fetch-saves: HASH MISMATCH %s -- the source changed; not written' % f['url'])
                bad += 1
                continue
            write(f['path'], data)
        for e in ex:
            m = zipfile.ZipFile(io.BytesIO(data)).read(e['member'])
            if sha256(m) != e['sha256']:
                print('fetch-saves: HASH MISMATCH %s in %s' % (e['member'], f['path']))
                bad += 1
                break
            write(e['path'], m)
        else:
            got += 1
            print('fetch-saves: %s' % f['path'])
    print('fetch-saves: %d file(s) verified, %d fetched, %d failed (of %d)' % (ok, got, bad, len(files)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
