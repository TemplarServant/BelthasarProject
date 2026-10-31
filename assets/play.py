#!/usr/bin/env python3
"""Live, interactive field viewer: walk Crono around a Chrono Trigger scene.

A Python front-end to the field engine (engine.py) via ctypes -> SDL2.dll
(third_party/SDL2). The map + NPCs are composited once into a base image; each frame
only the player (and the camera crop) is recomposed, so it runs smoothly. The engine
ticks every frame (NPC scripts / interactions / textbox), the player walks with tile
collision + a walk-cycle animation, and talking to an NPC shows its dialogue.

Keys: arrow keys = walk, Space/Z = talk to the NPC you face / advance text, Esc = quit.
Usage: python assets/play.py <rom.sfc> [scene] [--selftest]
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '../tools')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import sys, os, ctypes
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine as E
import field
import sprites
import font

SDL_INIT_VIDEO = 0x20
SDL_WINDOW_RESIZABLE = 0x20
SDL_PIXELFORMAT_RGB24 = 386930691
SDL_TEXTUREACCESS_STREAMING = 1
SDL_RENDERER_SOFTWARE = 0x01
SDL_RENDERER_ACCELERATED = 0x02
SDL_RENDERER_PRESENTVSYNC = 0x04
SDL_QUIT = 0x100
SDL_KEYDOWN = 0x300
# scancodes (SDL_GetKeyboardState index) for held movement keys
SC_RIGHT, SC_LEFT, SC_DOWN, SC_UP = 79, 80, 81, 82
# keycodes (event keysym.sym) for one-shot keys
K_ESCAPE, K_SPACE, K_Z, K_RETURN = 27, 32, 122, 13

CAMW, CAMH, ZOOM = 256, 224, 2          # one SNES screen, 2x

def _load_sdl():
    dll = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'third_party', 'SDL2', 'SDL2.dll')
    s = ctypes.CDLL(os.path.abspath(dll))
    s.SDL_CreateWindow.restype = ctypes.c_void_p
    s.SDL_CreateRenderer.restype = ctypes.c_void_p
    s.SDL_CreateTexture.restype = ctypes.c_void_p
    s.SDL_GetKeyboardState.restype = ctypes.POINTER(ctypes.c_uint8)
    s.SDL_GetTicks.restype = ctypes.c_uint32
    return s

def _front_tile(eng):
    """Tile directly in front of the player, by facing (0=U,1=D,2=L,3=R)."""
    px, py = eng.player['pos']
    tx, ty = int(px) // 16, (int(py) - 1) // 16
    return tx + [0, 0, -1, 1][eng.player['facing']], ty + [-1, 1, 0, 0][eng.player['facing']]

def _faced_actor(eng):
    fx, fy = _front_tile(eng)
    for a in eng.actors:
        if a['visible'] and a['pos'] is not None:
            if int(a['pos'][0]) // 16 == fx and (int(a['pos'][1]) - 1) // 16 == fy:
                return a['obj']
    return None

def run(rom, scene, selftest=False):
    eng = E.SceneEngine(rom, scene)
    base, W, H, _ = field.compose_scene(rom, scene, actors=False)  # map only; NPCs drawn live
    sdl = _load_sdl()
    if selftest:
        sdl.SDL_SetHint(b'SDL_VIDEODRIVER', b'dummy')
    if sdl.SDL_Init(SDL_INIT_VIDEO) != 0:
        print('FAIL: SDL_Init'); return 3
    win = sdl.SDL_CreateWindow(b'Chrono Trigger - field', 0x2FFF0000, 0x2FFF0000,
                               CAMW * ZOOM, CAMH * ZOOM, SDL_WINDOW_RESIZABLE)
    ren = sdl.SDL_CreateRenderer(ctypes.c_void_p(win), -1,
                                 SDL_RENDERER_SOFTWARE if selftest else (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC))
    if not ren:
        ren = sdl.SDL_CreateRenderer(ctypes.c_void_p(win), -1, SDL_RENDERER_SOFTWARE)
    tex = sdl.SDL_CreateTexture(ctypes.c_void_p(ren), SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, CAMW, CAMH)
    sdl.SDL_RenderSetLogicalSize(ctypes.c_void_p(ren), CAMW, CAMH)

    frame_buf = bytearray(CAMW * CAMH * 3)
    evt = (ctypes.c_uint8 * 56)()
    running = True
    anim = 0
    moving_anim = 0
    exit_cd = 20                                                # ignore exits at spawn
    talk = os.environ.get('PLAY_TALK')                          # selftest: pop a dialogue
    if talk:
        eng.activate(int(talk))
        for _ in range(3000):
            eng.tick()
            if eng.textbox is not None:
                break
    while running:
        # ---- events (quit / talk / advance) ----
        while sdl.SDL_PollEvent(ctypes.byref(evt)):
            etype = evt[0] | (evt[1] << 8) | (evt[2] << 16) | (evt[3] << 24)
            if etype == SDL_QUIT:
                running = False
            elif etype == SDL_KEYDOWN:
                sym = evt[20] | (evt[21] << 8) | (evt[22] << 16) | (evt[23] << 24)
                if sym == K_ESCAPE:
                    running = False
                elif sym in (K_SPACE, K_Z, K_RETURN):
                    if eng.textbox is not None:
                        eng.advance()
                    else:
                        obj = _faced_actor(eng)
                        if obj is not None:
                            eng.activate(obj)

        # ---- movement (held arrows; blocked while a textbox is up) ----
        ks = sdl.SDL_GetKeyboardState(None)
        dx = dy = 0
        if eng.textbox is None:
            if ks[SC_RIGHT]: dx = 1
            elif ks[SC_LEFT]: dx = -1
            if ks[SC_DOWN]: dy = 1
            elif ks[SC_UP]: dy = -1
        walking = False
        if dx or dy:
            walking = eng.move_player(dx, dy, speed=1.5)
        moving_anim = moving_anim + 1 if walking else 0
        eng.tick()                                              # NPC scripts / interaction / textbox
        anim += 1

        # ---- doors: step onto an exit region -> load the destination scene ----
        if exit_cd > 0:
            exit_cd -= 1
        elif eng.textbox is None:
            ex = eng.exit_at()
            if ex is not None:
                scene = ex['dest']
                eng = eng.enter_scene(scene, ex['dpos'], ex['facing'])  # carry story flags
                base, W, H, _ = field.compose_scene(rom, scene, actors=False)
                exit_cd = 20                                    # don't re-trigger immediately
                continue

        # ---- camera follows the player, clamped to the map ----
        px, py = eng.player['pos']
        camx = max(0, min(W - CAMW, int(px) - CAMW // 2)) if W > CAMW else 0
        camy = max(0, min(H - CAMH, int(py) - CAMH // 2)) if H > CAMH else 0

        # ---- compose this frame: base crop + live NPCs + player (Y-sorted) + textbox ----
        cw = min(CAMW, W - camx); ch = min(CAMH, H - camy)
        for y in range(ch):
            s = ((camy + y) * W + camx) * 3
            frame_buf[y * CAMW * 3: y * CAMW * 3 + cw * 3] = base[s: s + cw * 3]
        draw = []                                           # NPCs in view + the player
        for a in eng.actors:
            if a['visible'] and a['sprite'] is not None and a['pos'] is not None:
                ax, ay = a['pos']
                if camx - 48 <= ax <= camx + CAMW + 48 and camy - 64 <= ay <= camy + CAMH + 16:
                    draw.append((ay, a['sprite'], ax, ay, a['frame']))
        pframe = sprites.pick_frame(rom, 0, eng.player['facing'], walking, moving_anim)
        draw.append((py, 0, px, py, pframe))
        draw.sort(key=lambda d: d[0])
        for _, sp, ax, ay, fr in draw:
            _blit(rom, frame_buf, sp, fr, int(ax) - camx, int(ay) - camy)
        if eng.textbox is not None:
            _textbox(rom, frame_buf, eng.textbox)

        sdl.SDL_UpdateTexture(ctypes.c_void_p(tex), None, bytes(frame_buf), CAMW * 3)
        sdl.SDL_RenderClear(ctypes.c_void_p(ren))
        sdl.SDL_RenderCopy(ctypes.c_void_p(ren), ctypes.c_void_p(tex), None, None)
        sdl.SDL_RenderPresent(ctypes.c_void_p(ren))
        sdl.SDL_Delay(33)
        if selftest:                                            # one frame + a synthetic interaction
            eng.move_player(1, 0, speed=1.5)
            obj = _faced_actor(eng)
            shot = os.environ.get('PLAY_SHOT')
            if shot:
                from palettes import write_png_rgb
                write_png_rgb(shot, frame_buf, CAMW, CAMH)
            print('selftest: scene %d base %dx%d, player %s, faced obj %s, frame composed%s'
                  % (scene, W, H, tuple(map(int, eng.player['pos'])), obj,
                     (' -> ' + shot) if shot else ''))
            running = False

    sdl.SDL_DestroyRenderer(ctypes.c_void_p(ren))
    sdl.SDL_DestroyWindow(ctypes.c_void_p(win))
    sdl.SDL_Quit()
    return 0

def _blit(rom, frame, sprite, asm_frame, sx, sy):
    """Composite a sprite (compose_frame) into the camera frame at screen (sx,sy)."""
    try:
        cv, pal, S, O = sprites.compose_frame(rom, sprite, asm_frame)
    except Exception:
        return
    for y in range(S):
        ry = sy - O + y
        if ry < 0 or ry >= CAMH:
            continue
        base = y * S
        for x in range(S):
            v = cv[base + x]
            if v == 0:
                continue
            rx = sx - O + x
            if 0 <= rx < CAMW:
                r, g, b = pal[v]
                o = (ry * CAMW + rx) * 3
                frame[o], frame[o + 1], frame[o + 2] = r, g, b

def _textbox(rom, frame, s):
    """Draw the dialogue window across the bottom + render `s` with the CT font."""
    top = CAMH - 68
    for y in range(top, CAMH - 4):
        for x in range(8, CAMW - 8):
            o = (y * CAMW + x) * 3
            edge = y < top + 2 or y > CAMH - 7 or x < 11 or x > CAMW - 12
            frame[o], frame[o + 1], frame[o + 2] = (224, 224, 248) if edge else (24, 24, 96)
    font.draw_text(rom, frame, CAMW, CAMH, s, 16, top + 8, CAMW - 32, max_lines=3)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    rom = _romlib.load(sys.argv[1])
    scene = 5
    for a in sys.argv[2:]:
        if a.isdigit():
            scene = int(a)
    sys.exit(run(rom, scene, '--selftest' in sys.argv))
