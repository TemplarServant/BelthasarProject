#!/usr/bin/env python3
"""Talk to the snes9x-rr Lua MCP bridge via its file IPC (command.json/response.json).
The bridge polls command.json each frame and writes response.json. Reads live emulator state
(CGRAM/VRAM/WRAM, the framebuffer), e.g. while the player stands on the overworld.
"""
import json, os, time

IPC = r"C:\Users\Icaro Ferris\Documents\Claude\Projects\Chrono Trigger Remake\tools\mcp\ipc"
_CMD = os.path.join(IPC, "command.json")
_TMP = os.path.join(IPC, "command.tmp.json")
_RES = os.path.join(IPC, "response.json")
_id = int(time.time()) % 100000


def call(op, args=None, timeout=20):
    global _id
    _id += 1
    cmd = {"id": _id, "op": op, "args": args or {}}
    with open(_TMP, "w") as f:
        json.dump(cmd, f)
    os.replace(_TMP, _CMD)
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            r = json.load(open(_RES))
            if r.get("id") == _id:
                if r.get("ok") is False:
                    raise RuntimeError("bridge error: " + str(r.get("error")))
                return r.get("result", r)
        except (ValueError, IOError):
            pass
        time.sleep(0.03)
    raise TimeoutError("no response for id %d op %s" % (_id, op))


def capture(tag='cap'):
    """Capture the live framebuffer -> numpy (224,256,3) RGB via the GD truecolor format."""
    import struct
    import numpy as np
    p = 'C:/Users/Icaro Ferris/Desktop/Crono Trigger Decompile/build/%s.png' % tag
    r = call('capture_framebuffer', {'path': p})
    gdp = r.get('path', p + '.gd')
    d = open(gdp, 'rb').read()
    _, w, h = struct.unpack('>HHH', d[:6])
    px = np.frombuffer(d[11:11 + w*h*4], dtype=np.uint8).reshape(h, w, 4)
    return px[:, :, 1:4].copy()   # drop alpha -> RGB


if __name__ == '__main__':
    import sys
    print('ping:', call('ping'))
    cg = call('read_cgram', {'addr': 0, 'length': 512})
    b = bytes(cg['bytes'])
    open('build/ow_cgram.bin', 'wb').write(b)
    def rgb(v): return ((v & 31)*8, ((v >> 5) & 31)*8, ((v >> 10) & 31)*8)
    print('CGRAM method=%s len=%d' % (cg.get('method'), len(b)))
    print('CGRAM[0] backdrop =', rgb(b[0] | (b[1] << 8)))
    print('pal2:', [rgb(b[(32+c)*2] | (b[(32+c)*2+1] << 8)) for c in range(8)])
    print('wrote build/ow_cgram.bin')
