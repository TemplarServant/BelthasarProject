"""A content-addressed cache of ORACLE runs (build/ct.exe --peek / --monitor / --hdma / --fb), so a re-run of the suite pays
only for what changed.

A run of those modes is a pure function of (the ct.exe binary, its arguments, its environment, the files it reads); every
one of them goes into the key:

  * sha256 of the ct.exe being run -- a rebuilt oracle (snes/*.c, src/main.c, any port file ct.exe links) misses;
  * the arguments and the working directory, minus the OUTPUT paths (`--peek <state> <OUT>`, and the env outputs below);
  * the environment, minus the operating system's own variables (PATH, TEMP, ...) -- CT_*, PEEK_*, VMDIFF_* ... all in;
  * the CONTENT of every argument or environment value that names an existing file (a state, a .lakewram, the ROM given
    by --rom, CT_ORACLE_SRAM ...), and of the ROM ct.exe opens by default in the working directory.

Its outputs are ENUMERATED, never guessed: `--peek`'s <OUT> and <OUT>.wram/.vram/.oam, `--fb`'s <OUT> and <OUT>.wram, PEEK_SAVE, PEEK_THUMBS, PEEK_PCM,
PEEK_ARAM, CT_DSP_RAWPCM, and PEEK_SHOTS' <prefix>_f<n>.raw (+ sidecars) for each n of PEEK_SHOTS_AT. They are stored
by ROLE (argument OUT, env PEEK_SAVE, ...) and a hit writes them to the paths THIS call names -- a tool that puts its pid
in a temporary name still hits. With stdout, stderr and the exit code, a hit returns the same CompletedProcess.

Opt-in by environment: CT_ORACLE_CACHE=<dir> turns it on (tools/regress.py sets build/_oracle_cache for its checks and
their tools, through tools/oracle_cache_site/sitecustomize.py; `--no-oracle-cache` turns it off, `--clear-oracle-cache`
empties it). Only subprocess.run / check_output calls are routed (a Popen is not), and only for the three modes above.
"""
import hashlib
import json
import os
import shutil
import subprocess
import time
import zlib

MODES = ('--peek', '--monitor', '--hdma', '--fb')


def mode_of(argv):
    """The run's mode and the index of its OUTPUT argument (None): `--peek <state> <OUT>`, `[rom] --fb <OUT>` (a ROM path
    may come first). A --fb that runs the play script (--script, harness_drive.c) is not cached."""
    for i, a in enumerate(argv[1:3], 1):
        if a in MODES:
            if a == '--peek':
                return a, (i + 2 if len(argv) > i + 2 else None)
            if a == '--fb':
                return (None, None) if '--script' in argv else (a, (i + 1 if len(argv) > i + 1 else None))
            return a, None
    return None, None
ENV_OUTS = ('PEEK_SAVE', 'PEEK_THUMBS', 'PEEK_PCM', 'PEEK_ARAM', 'CT_DSP_RAWPCM', 'PEEK_SHOTS')
_OS_VARS = {
    'PATH', 'PATHEXT', 'TEMP', 'TMP', 'TMPDIR', 'HOME', 'HOMEDRIVE', 'HOMEPATH', 'USERPROFILE', 'USERNAME', 'USERDOMAIN',
    'USERDOMAIN_ROAMINGPROFILE', 'COMPUTERNAME', 'APPDATA', 'LOCALAPPDATA', 'PROGRAMDATA', 'PROGRAMFILES',
    'PROGRAMFILES(X86)', 'PROGRAMW6432', 'COMMONPROGRAMFILES', 'COMMONPROGRAMFILES(X86)', 'COMMONPROGRAMW6432',
    'SYSTEMROOT', 'SYSTEMDRIVE', 'WINDIR', 'COMSPEC', 'OS', 'PROCESSOR_ARCHITECTURE', 'PROCESSOR_IDENTIFIER',
    'PROCESSOR_LEVEL', 'PROCESSOR_REVISION', 'NUMBER_OF_PROCESSORS', 'PUBLIC', 'ALLUSERSPROFILE', 'LOGONSERVER',
    'SESSIONNAME', 'PROMPT', 'PSMODULEPATH', 'PWD', 'OLDPWD', 'SHLVL', 'TERM', 'SHELL', 'LANG', 'MSYSTEM', 'ORIGINAL_PATH',
    'EXEPATH', 'MSYS', 'MINGW_PREFIX', 'MSYSTEM_PREFIX', 'HOSTNAME', 'INFOPATH', 'MANPATH', 'ACLOCAL_PATH',
    'PKG_CONFIG_PATH', 'CONFIG_SITE', 'DISPLAY', 'PYTHONPATH', 'PYTHONIOENCODING', 'PYTHONUTF8', 'PYTHONHASHSEED',
    'CT_ORACLE_CACHE', 'ONEDRIVE', 'ONEDRIVECONSUMER', 'DRIVERDATA', 'CHROME_CRASHPAD_PIPE_NAME', '_', 'TERM_PROGRAM',
    'TERM_PROGRAM_VERSION', 'COLORTERM', 'WT_SESSION', 'WT_PROFILE_ID', 'VSCODE_GIT_IPC_HANDLE', 'GIT_ASKPASS',
    'SDL_VIDEODRIVER', 'SDL_AUDIODRIVER',
}
DEFAULT_ROM = 'Chrono_Trigger_USA_SNES.sfc'
_hash_memo = {}


def _file_sha(path):
    st = os.stat(path)
    k = (os.path.abspath(path), st.st_size, st.st_mtime_ns)
    h = _hash_memo.get(k)
    if h is None:
        d = hashlib.sha256()
        with open(path, 'rb') as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b''):
                d.update(chunk)
        h = _hash_memo[k] = d.hexdigest()
    return h


def _abs(p, cwd):
    return p if os.path.isabs(p) else os.path.join(cwd, p)


def _outputs(argv, env, cwd):
    """{role: absolute path} of every file this run may write."""
    outs = {}
    mode, oi = mode_of(argv)
    if oi is not None:
        o = _abs(argv[oi], cwd)
        for ext in (('', '.wram', '.vram', '.oam') if mode == '--peek' else ('', '.wram')):
            outs['out' + ext] = o + ext
    for k in ENV_OUTS:
        v = env.get(k)
        if not v:
            continue
        if k == 'PEEK_SHOTS':
            side = env.get('PEEK_SHOTS_SIDECARS') not in (None, '', '0')
            for n in (env.get('PEEK_SHOTS_AT') or '').split(','):
                if n.strip().lstrip('-').isdigit():
                    for ext in (('', '.wram', '.vram', '.oam') if side else ('',)):
                        outs['shot%s%s' % (int(n), ext)] = _abs('%s_f%d.raw%s' % (v, int(n), ext), cwd)
        else:
            outs['env:' + k] = _abs(v, cwd)
    return outs


# The exact size of every dump whose format fixes it (src/main.c runPeek / runDumpFb / PEEK_SHOTS): 512x480 [pad,B,G,R],
# the 128 KB WRAM, the 64 KB VRAM, OAM 544 + CGRAM 512 (+ the high table: 2*512 + 32 + 2*256). A file of another size is
# a run that did not finish writing it (a full disk, 2026-10-08: 11 truncated dumps were stored and replayed as truth).
_SIZES = {'': 512 * 480 * 4, '.wram': 0x20000, '.vram': 0x10000, '.oam': 2 * 512 + 32 + 2 * 256}


def _want_size(role):
    if role.startswith('env:'):
        return None
    for ext in ('.wram', '.vram', '.oam'):
        if role.endswith(ext):
            return _SIZES[ext]
    return _SIZES['']


def _key(argv, env, cwd):
    oi = mode_of(argv)[1]
    out_args = {oi} if oi is not None else set()
    keyenv = {k: v for k, v in env.items() if k.upper() not in _OS_VARS and k not in ENV_OUTS}
    parts = {'exe': _file_sha(argv[0]), 'argv': [a for i, a in enumerate(argv[1:], 1) if i not in out_args], 'cwd': cwd,
             'env': sorted(keyenv.items()), 'files': []}
    for tag, v in [('a%d' % i, a) for i, a in enumerate(argv[1:], 1) if i not in out_args] + sorted(keyenv.items()):
        if isinstance(v, str) and v and len(v) < 1024:
            p = _abs(v, cwd)
            if os.path.isfile(p):
                parts['files'].append((str(tag), _file_sha(p)))
    rom = os.path.join(cwd, DEFAULT_ROM)
    if os.path.isfile(rom):
        parts['files'].append(('rom', _file_sha(rom)))
    return hashlib.sha256(json.dumps(parts, sort_keys=True).encode()).hexdigest()


def _streams(kwargs, out, err):
    so, se, cap = kwargs.get('stdout'), kwargs.get('stderr'), kwargs.get('capture_output')
    want_out = cap or so == subprocess.PIPE
    want_err = cap or se == subprocess.PIPE
    if se == subprocess.STDOUT:
        out, err, want_err = (out or b'') + (err or b''), None, False
    text = kwargs.get('text') or kwargs.get('universal_newlines') or kwargs.get('encoding') or kwargs.get('errors')

    def dec(b):
        if b is None or not text:
            return b
        return b.decode(kwargs.get('encoding') or 'utf-8', kwargs.get('errors') or 'strict').replace('\r\n', '\n')
    return (dec(out) if want_out else None), (dec(err) if want_err else None)


def _finish(argv, kwargs, rc, out, err):
    o, e = _streams(kwargs, out, err)
    if kwargs.get('check') and rc:
        raise subprocess.CalledProcessError(rc, argv, o, e)
    return subprocess.CompletedProcess(argv, rc, o, e)


def run(argv, kwargs, real_run, cache=None):
    """subprocess.run(argv, **kwargs) through the cache `cache` (default: CT_ORACLE_CACHE), when this is a cacheable
    oracle run."""
    cache = cache or os.environ.get('CT_ORACLE_CACHE')
    if (not cache or not isinstance(argv, (list, tuple)) or len(argv) < 2 or not isinstance(argv[0], str)
            or os.path.basename(argv[0]).lower() != 'ct.exe' or mode_of(argv)[0] is None or not os.path.isfile(argv[0])
            or not all(isinstance(a, str) for a in argv)):
        return real_run(argv, **kwargs)
    if any(kwargs.get(k) for k in ('stdin', 'input', 'shell', 'timeout')):
        return real_run(argv, **kwargs)
    if not all(kwargs.get(k) in (None, subprocess.PIPE, subprocess.DEVNULL, subprocess.STDOUT) for k in ('stdout', 'stderr')):
        return real_run(argv, **kwargs)
    argv = list(argv)
    env = kwargs.get('env')
    env = dict(env) if env is not None else dict(os.environ)
    cwd = os.path.abspath(kwargs.get('cwd') or os.getcwd())
    try:
        key = _key(argv, env, cwd)
    except OSError:
        return real_run(argv, **kwargs)
    outs = _outputs(argv, env, cwd)
    ent = os.path.join(cache, key[:2], key)
    meta = os.path.join(ent, 'meta.json')
    if os.path.isfile(meta):
        try:
            with open(meta, encoding='utf-8') as fh:
                m = json.load(fh)
            blobs = {}
            for role in m['roles']:
                with open(os.path.join(ent, 'r_' + role.replace(':', '_')), 'rb') as fh:
                    blobs[role] = zlib.decompress(fh.read())
                if _want_size(role) not in (None, len(blobs[role])):
                    raise ValueError('a truncated dump in the cache')
            for role, data in blobs.items():
                dst = outs[role]
                os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
                with open(dst, 'wb') as fh:
                    fh.write(data)
            with open(os.path.join(ent, 'stdout'), 'rb') as fh:
                out = fh.read()
            with open(os.path.join(ent, 'stderr'), 'rb') as fh:
                err = fh.read()
            os.utime(meta)                                     # the entry's last use, for prune()
            return _finish(argv, kwargs, m['rc'], out, err)
        except (OSError, ValueError, KeyError, zlib.error):
            shutil.rmtree(ent, ignore_errors=True)            # a bad entry is dropped and the run made for real
    kw = {k: v for k, v in kwargs.items() if k not in ('stdout', 'stderr', 'capture_output', 'text', 'universal_newlines',
                                                       'encoding', 'errors', 'check')}
    t0 = time.time()
    r = real_run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)
    roles, whole = [], True
    for role, p in outs.items():
        try:
            if os.path.isfile(p) and os.stat(p).st_mtime >= t0 - 2:
                roles.append(role)
                if _want_size(role) not in (None, os.stat(p).st_size):
                    whole = False                             # a dump cut short: never stored
        except OSError:
            whole = False
    if mode_of(argv)[1] is not None and 'out' not in roles:
        whole = False                                         # --peek / --fb wrote no dump: nothing to replay
    if not whole:
        return _finish(argv, kwargs, r.returncode, r.stdout, r.stderr)
    tmp = '%s.tmp%d_%d' % (ent, os.getpid(), int(t0 * 1e6) % 1000000)
    try:
        os.makedirs(tmp, exist_ok=True)
        for role in roles:
            with open(outs[role], 'rb') as fh:
                data = zlib.compress(fh.read(), 1)             # a frame buffer is mostly runs: ~1 MB -> ~30 KB
            with open(os.path.join(tmp, 'r_' + role.replace(':', '_')), 'wb') as fh:
                fh.write(data)
        with open(os.path.join(tmp, 'stdout'), 'wb') as fh:
            fh.write(r.stdout or b'')
        with open(os.path.join(tmp, 'stderr'), 'wb') as fh:
            fh.write(r.stderr or b'')
        with open(os.path.join(tmp, 'meta.json'), 'w', encoding='utf-8') as fh:
            json.dump({'rc': r.returncode, 'roles': roles, 'argv': argv, 'cwd': cwd}, fh)
        if os.path.exists(ent):
            shutil.rmtree(tmp, ignore_errors=True)
        else:
            os.makedirs(os.path.dirname(ent), exist_ok=True)
            os.replace(tmp, ent)
    except OSError:
        shutil.rmtree(tmp, ignore_errors=True)
    return _finish(argv, kwargs, r.returncode, r.stdout, r.stderr)


def install():
    """Route subprocess.run / check_output through the cache (tools/oracle_cache_site/sitecustomize.py calls this)."""
    if getattr(subprocess, '_ct_oracle_cache', False):
        return
    real_run = subprocess.run

    def cached_run(*args, **kwargs):
        if len(args) > 1:
            return real_run(*args, **kwargs)
        argv = args[0] if args else kwargs.pop('args')
        return run(argv, kwargs, real_run)

    def cached_check_output(*args, **kwargs):
        if len(args) > 1 or 'stdout' in kwargs:
            return real_check_output(*args, **kwargs)
        argv = args[0] if args else kwargs.pop('args')
        return cached_run(argv, stdout=subprocess.PIPE, check=True, **kwargs).stdout

    real_check_output = subprocess.check_output
    subprocess.run = cached_run
    subprocess.check_output = cached_check_output
    subprocess._ct_oracle_cache = True


def prune(cache, max_bytes):
    """Drop the least recently used entries until the cache holds at most max_bytes. Returns (entries, bytes) kept."""
    ents = []
    for d1 in (os.listdir(cache) if os.path.isdir(cache) else []):
        p1 = os.path.join(cache, d1)
        for d2 in (os.listdir(p1) if os.path.isdir(p1) else []):
            ent = os.path.join(p1, d2)
            meta = os.path.join(ent, 'meta.json')
            if not os.path.isfile(meta):
                if '.tmp' in d2 and time.time() - os.stat(ent).st_mtime > 3600:
                    shutil.rmtree(ent, ignore_errors=True)
                continue
            size = sum(os.stat(os.path.join(ent, f)).st_size for f in os.listdir(ent))
            ents.append((os.stat(meta).st_mtime, size, ent))
    ents.sort(reverse=True)
    total, kept = 0, 0
    for _, size, ent in ents:
        if total + size > max_bytes:
            shutil.rmtree(ent, ignore_errors=True)
            continue
        total += size
        kept += 1
    return kept, total
