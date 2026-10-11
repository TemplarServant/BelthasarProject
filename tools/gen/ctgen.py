"""Shared plumbing for the generators in tools/gen/ that read the oracle (build/ct.exe) from recipe states.

    from ctgen import ROOT, ROM, recipe, peek, rgb_of_fb

recipe(name)            -> the absolute path of build/recipes/<name>.state, made (or taken from the cache) by
                           tools/recipes.py; exits if the recipe fails.
peek(state, out, script, env=None, rule=True, frames=None)
                        -> runs `ct.exe --peek <state> <out>.raw --btnscript <script>` from the repository root and
                           returns <out> (the dumps are <out>.raw, <out>.raw.wram, <out>.raw.vram, <out>.raw.oam; with
                           save=True also <out>.state, the PEEK_SAVE of the last frame). `script` takes the recipes'
                           grammar (`(a:n,b:m)*K` groups). rule=True runs the cartridge under the rule
                           (CT_ORACLE_LOGICAL=1, CLAUDE.md), as every recipe is built.
rgb_of_fb(raw)          -> the 256x240 RGB picture of an oracle framebuffer dump (512x480 [pad,B,G,R], 2x doubled).
cgram_of(raw)           -> the 512-byte CGRAM of a peek (the .raw.oam sidecar at 1056).

Every generator runs from the repository root with the ROM at Chrono_Trigger_USA_SNES.sfc (tools/setup.py).
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CT = os.path.join(ROOT, 'build', 'ct.exe')
ROM = os.path.join(ROOT, 'Chrono_Trigger_USA_SNES.sfc')
SCRATCH = os.path.join(ROOT, 'build', 'gen')

sys.path.insert(0, os.path.join(ROOT, 'tools'))
from recipes import expand  # noqa: E402


def recipe(name):
    if subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'recipes.py'), 'make', name], cwd=ROOT,
                      stdout=subprocess.DEVNULL).returncode:
        sys.exit('ctgen: the recipe %s failed' % name)
    return os.path.join(ROOT, 'build', 'recipes', name + '.state')


def scratch(name):
    os.makedirs(SCRATCH, exist_ok=True)
    return os.path.join(SCRATCH, name)


def peek(state, out, script, env=None, rule=True, frames=None, save=False):
    e = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    if rule:
        e['CT_ORACLE_LOGICAL'] = '1'
    if save:
        e['PEEK_SAVE'] = out + '.state'
    e.update(env or {})
    args = [CT, ROM, '--peek', state, out + '.raw']
    if script is not None:
        args += ['--btnscript', expand(script)]
    if frames is not None:
        args += ['--frames', str(frames)]
    for ext in ('.raw', '.raw.wram', '.raw.vram', '.raw.oam'):
        if os.path.exists(out + ext):
            os.remove(out + ext)
    p = subprocess.run(args, cwd=ROOT, env=e, capture_output=True, text=True, errors='replace')
    if not os.path.exists(out + '.raw'):
        sys.exit('ctgen: ct.exe --peek %s wrote nothing: %s' % (state, (p.stdout + p.stderr)[-600:]))
    return out


def rgb_of_fb(raw):
    d = open(raw, 'rb').read() if isinstance(raw, str) else raw
    out = bytearray(256 * 240 * 3)
    for y in range(240):
        for x in range(256):
            o = (2 * y * 512 + 2 * x) * 4
            i = (y * 256 + x) * 3
            out[i:i + 3] = (d[o + 3], d[o + 2], d[o + 1])
    return bytes(out)


def cgram_of(raw):
    return open(raw + '.oam', 'rb').read()[1056:1056 + 512]
