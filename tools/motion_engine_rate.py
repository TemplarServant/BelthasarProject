"""The frames between's rates against the engine's own (src/motion.c CtMotEngine), over the attract.

Runs the attract (scene 433) headless twice with CT_MOTFITTRACE=2 -- the rates from the engine (the default) and the
fit alone (CT_MOTENGINE=0) -- and grades each run's continuous places against the engine's true step: the camera's
place in sixteenths ($1D0A/$1D93 whole + the remainder in $1D3B/$1D3C) and each object's 12.4 words ($1800/$1880,X),
on the frames whose drawn place stepped as the engine's whole part did. Prints one line per run and kind:

    motion-rate: <kind> <source>: <n> steps, mean |err| <px>, >0.1px <pct>%, >0.25px <pct>%

usage: python tools/motion_engine_rate.py [frames]
"""
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "Belthasar.exe")


def run(frames, engine):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", CT_NOPACE="1", CT_MOTFITTRACE="2")
    env.pop("CT_MOTENGINE", None)
    if not engine:
        env["CT_MOTENGINE"] = "0"
    out = subprocess.run([EXE, "--assets", "ct_assets.dat", "--scene", "433", "--frames", str(frames), "--selftest"],
                         cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900).stdout
    return out.decode("utf-8", "replace")


CAM = re.compile(r"motfit: g(\d+) n\d+ cam (-?\d+),(-?\d+) q (-?[\d.]+),(-?[\d.]+) .* \| e (\d) (-?\d+),(-?\d+)")
OBJ = re.compile(r"motobj: g(\d+) k(\d+) at (-?\d+),(-?\d+) q (-?[\d.]+),(-?[\d.]+) w (-?\d+),(-?\d+)")


def parse(text):
    cam, obj = {}, {}
    for ln in text.splitlines():
        m = CAM.match(ln)
        if m:
            v = [int(m.group(i)) if i not in (4, 5) else float(m.group(i)) for i in range(1, 9)]
            cam[v[0]] = v[1:]
            continue
        m = OBJ.match(ln)
        if m:
            g, k = int(m.group(1)), int(m.group(2))
            obj.setdefault((g, k), (int(m.group(3)), int(m.group(4)), float(m.group(5)), float(m.group(6)),
                                    int(m.group(7)), int(m.group(8))))
    return cam, obj


def s16(d):
    return ((d + 32768) & 0xFFFF) - 32768


def grade(truth, shown):
    """truth: the engine run's records (they carry the words); shown: the run whose continuous places are graded."""
    tc, to = truth
    sc, so = shown
    cam, obj = [], []
    for g, v in tc.items():
        p = tc.get(g - 1)
        if not p or not v[4] or not p[4] or g not in sc or g - 1 not in sc:
            continue
        for a in (0, 1):
            d = v[5 + a] - p[5 + a]
            if abs(d) > 32 * 16 or abs(v[a] - p[a]) > 32 or (d == 0 and v[a] == p[a]):   # (a cut, or still)
                continue
            cam.append(abs((sc[g][2 + a] - sc[g - 1][2 + a]) - d / 16))
    for (g, k), v in to.items():
        p = to.get((g - 1, k))
        if not p or (g, k) not in so or (g - 1, k) not in so:
            continue
        for a in (0, 1):
            if v[4 + a] < 0 or p[4 + a] < 0:
                continue
            d = s16(v[4 + a] - p[4 + a])
            if d == 0 or abs(d) > 32 * 16 or v[a] - p[a] != ((p[4 + a] + d) >> 4) - (p[4 + a] >> 4):
                continue
            obj.append(abs((so[(g, k)][2 + a] - so[(g - 1, k)][2 + a]) - d / 16))
    return cam, obj


def line(kind, source, errs):
    if not errs:
        return "motion-rate: %s %s: no steps" % (kind, source)
    n = len(errs)
    return "motion-rate: %s %s: %d steps, mean |err| %.3f px, >0.1px %.1f%%, >0.25px %.1f%%" % (
        kind, source, n, sum(errs) / n, 100.0 * sum(e > 0.1 for e in errs) / n, 100.0 * sum(e > 0.25 for e in errs) / n)


def main():
    frames = int(sys.argv[1]) if len(sys.argv) > 1 else 11300
    with ThreadPoolExecutor(2) as ex:
        eng, fit = ex.map(lambda e: parse(run(frames, e)), (True, False))
    ec, eo = grade(eng, eng)
    fc, fo = grade(eng, fit)
    print(line("camera", "engine", ec))
    print(line("camera", "fit", fc))
    print(line("objects", "engine", eo))
    print(line("objects", "fit", fo))


if __name__ == "__main__":
    main()
