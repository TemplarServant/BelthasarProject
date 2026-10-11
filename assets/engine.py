#!/usr/bin/env python3
"""Cooperative scene-script scheduler (M2) + player-input hooks.

Runs a scene's object scripts frame-by-frame with Chrono Trigger's real model:
per-object script state (16 function pointers, 8 priority return pointers, current
priority — 7 = init/idle), a per-frame run cycle (delay countdown, up to 5 ops or
until a yield), req/call/return priority switching between objects, wait/yield, and
a textbox state machine (a text op blocks the actor until the box is dismissed).

Player input is exposed as `activate(obj)` (talk, slot 1) and `touch(obj)` (slot 2);
a real front-end would call these on a key press near an NPC. `tick()` advances one
frame; `advance()` dismisses the current textbox. Built on script.py + text.py.

Usage:
  python assets/engine.py <rom.sfc> <scene> talk <obj>   # drive a talk interaction
  python assets/engine.py <rom.sfc> <scene> run [frames]  # tick the scene N frames
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '../tools')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import script
import text
import maps
import sprites
from script import Mem, load_script, op_len, _PC, _cmp, GLB, LOC

def solid_at(t, sx, sy):
    """Is pixel (sx,sy in 0..15) within a tile of collision type `t` blocked? Sub-tile
    resolution for the CT collision types (CTViewer parses these but applies none)."""
    if t == 0 or t == 30:                # None / Ladder
        return False
    if t == 1:                           # Full
        return True
    if t == 24: return sx < 8            # LeftHalf  (left half solid)
    if t == 25: return sy < 8            # TopHalf   (top half solid)
    if t == 26: return sx < 8 and sy >= 8   # SW quadrant solid
    if t == 27: return sx >= 8 and sy >= 8  # SE
    if t == 28: return sx >= 8 and sy < 8   # NE
    if t == 29: return sx < 8 and sy < 8    # NW
    if t == 2: return sx + sy < 16       # Corner45 NW (top-left triangle)
    if t == 3: return sx > sy            # Corner45 NE (top-right triangle)
    if t == 4: return sx < sy            # Corner45 SW (bottom-left triangle)
    if t == 5: return sx + sy >= 16      # Corner45 SE (bottom-right triangle)
    if t == 22 or t == 23:               # stairs (climbable)
        return False
    return True                          # other slopes (22/30/75 deg): treat as solid

class ScriptState:
    __slots__ = ('fp', 'addr', 'ret', 'prio', 'delay', 'dc', 'tb', 'waiting')
    def __init__(self, fp):
        self.fp = fp
        self.addr = None                 # None = idle (not executing)
        self.ret = [0] * 8
        self.prio = 7
        self.delay = 4
        self.dc = 1
        self.tb = False                  # this actor is blocked on a textbox
        self.waiting = False             # blocked on a call (reqew)

class SceneEngine:
    def __init__(self, rom, scene, gmem=None):
        self.rom = rom
        self.scene = scene
        h = rom[0x360000 + scene * 14: 0x360000 + scene * 14 + 14]
        try:
            self.objs, self.body = load_script(rom, h[8] | (h[9] << 8))
        except Exception:
            self.objs, self.body = [], b''
        self.mem = Mem()
        if gmem is not None:                             # carry story flags in (door-entry)
            self.mem.g[:len(gmem)] = gmem[:0x200]
        self.actors = [{'obj': n, 'sprite': None, 'pos': None, 'facing': 1, 'draw': 'draw',
                        'dead': False, 'defined': False, 'result': 0,
                        'move_speed': (1.0 / 18.0) * 16.0, 'task': None,
                        # animation state (CTViewer SpriteState): mode none/loop/loopcount/static
                        'anim_index': 0, 'anim_il': 0, 'anim_frame': 0, 'anim_delay': 0,
                        'anim_mode': 'none', 'anim_loops': 0, 'anim_static': 0, 'frame': 0}
                       for n in range(len(self.objs))]
        self._anim_cache = {}
        self.states = [ScriptState(fp) for fp in self.objs]
        self.subs = text.load_dict(rom)
        self.table = None
        self.textbox = None              # the string currently shown (None = no box)
        self.frame = 0
        self._init_scene()
        # field collision + a player actor (CTViewer doesn't do these; our own layer)
        try:
            self.cw, self.ch, self.coll, self.zp, self.zf, self.mv = maps.scene_props(rom, scene)
        except Exception:
            self.cw, self.ch, self.coll, self.zp, self.zf, self.mv = 16, 16, [0] * 256, [1] * 256, [0] * 256, [0] * 256
        self.scroll = (h[10], h[11], h[12], h[13])
        sx, sy = self._spawn()
        stx, sty = int(sx) // 16, (int(sy) - 1) // 16          # plane the player loads on
        pz = self.zp[sty * self.cw + stx] if 0 <= stx < self.cw and 0 <= sty < self.ch else 1
        self.player = {'sprite': 0, 'pos': (sx, sy), 'facing': 1, 'z': pz}   # Crono
        self.exits = maps.scene_exits(rom, scene)                        # doors / transitions

    # ---- collision + player walking (our own; CT collision types from the props) ----
    WALKABLE = (0, 30)                   # SceneTileCollision None / Ladder

    def walkable(self, tx, ty):
        if tx < 0 or ty < 0 or tx >= self.cw or ty >= self.ch:
            return False
        return self.coll[ty * self.cw + tx] in self.WALKABLE

    def _zsolid(self, z):
        """Z-plane gating, verified from the CT routine at C0/9C3B: a tile is solid by
        z-plane when z==0 (transition-solid) or it is on a different plane than the
        player; walkable when z==3 (transition), z==player plane, or player plane==3."""
        pz = self.player['z']
        if z == 0:
            return True
        if z == 3 or z == pz or pz == 3:
            return False
        return True

    def _walkable_foot(self, x, y):
        tx, ty = int(x) // 16, (int(y) - 1) // 16
        if tx < 0 or ty < 0 or tx >= self.cw or ty >= self.ch:
            return False
        i2 = ty * self.cw + tx
        if self._zsolid(self.zp[i2]):                          # other plane / transition-solid
            return False
        return not solid_at(self.coll[i2], int(x) % 16, (int(y) - 1) % 16)

    def _spawn(self):
        # first walkable tile spiralling out from the camera-visible centre
        l, t, r, b = self.scroll
        if l == 0x80:
            cx, cy = self.cw // 2, self.ch // 2
        else:
            cx, cy = (l + r) // 2, (t + b) // 2
        for rad in range(0, max(self.cw, self.ch)):
            for ty in range(cy - rad, cy + rad + 1):
                for tx in range(cx - rad, cx + rad + 1):
                    if self.walkable(tx, ty):
                        return (tx * 16 + 8, ty * 16 + 16)
        return (cx * 16 + 8, cy * 16 + 16)

    def exit_at(self):
        """The scene exit (door) under the player, if any (CTViewer get_exit_at: a
        +/-8 px margin around the trigger region). World-map exits are ignored here."""
        px, py = self.player['pos']
        for e in self.exits:
            if e['kind'] != 'scene':
                continue
            ex, ey = e['pos']; sx, sy = e['size']
            if ex - 8 <= px < ex + sx + 8 and ey - 8 <= py < ey + sy + 8:
                return e
        return None

    def enter_scene(self, scene, pos, facing):
        """Door-entry driver: load `scene` carrying the CURRENT story flags forward (so
        flag-gated NPCs appear exactly as the accumulated story dictates), and drop the
        player at the entry pos/facing. This is how Lucca's house etc. become populated:
        as events set global flags during play, later scene entries reflect them."""
        e = SceneEngine(self.rom, scene, gmem=bytes(self.mem.g))
        e.player['pos'] = (float(pos[0]), float(pos[1]))
        e.player['facing'] = facing & 3
        return e

    def move_player(self, dx, dy, speed=2.0):
        """Walk the player by (dx,dy)*speed with per-axis tile collision (slides)."""
        px, py = self.player['pos']
        nx, ny = px + dx * speed, py + dy * speed
        if dx and self._walkable_foot(nx, py):
            px = nx
        if dy and self._walkable_foot(px, ny):
            py = ny
        moved = (px, py) != self.player['pos']
        self.player['pos'] = (px, py)
        if moved:                                            # plane update (CT C0/9D54-9D5A):
            ftx, fty = int(px) // 16, (int(py) - 1) // 16    # player plane := tile plane, but
            if 0 <= ftx < self.cw and 0 <= fty < self.ch:    # only when IGNORE_Z ($6C) is clear
                i2 = fty * self.cw + ftx
                if not (self.zf[i2] & 1):                     # zf bit0 = COLLISION_IGNORE_Z
                    self.player['z'] = self.zp[i2]
        if dx > 0: self.player['facing'] = 3
        elif dx < 0: self.player['facing'] = 2
        elif dy > 0: self.player['facing'] = 1
        elif dy < 0: self.player['facing'] = 0
        return moved

    def _init_scene(self):
        """Static init: run each object's init + the scene init (obj 0 slot 1) to set up
        actors, memory and the dialogue string table. Objects then idle until triggered."""
        cap = {'table': None, 'dialog': []}
        for n in range(len(self.objs)):
            addr, op = script.run_func(self.body, self.objs, n, self.objs[n][0], self.mem, self.actors, cap=cap)
            # CTViewer: init runs slot 0 to its first ret; the per-frame loop resumes there.
            if op == 0x00:                       # ret -> main loop begins right after it
                self.states[n].addr = addr + 1
            elif op in (0xAF, 0xB0, 0xB1, 0xB2): # parked on a yield op -> resume on it
                self.states[n].addr = addr
            else:                                # ran off the end / op cap -> idle
                self.states[n].addr = None
            self.states[n].prio = 7
        if self.objs:
            script.run_func(self.body, self.objs, 0, self.objs[0][1], self.mem, self.actors, cap=cap, follow=True)
        self.table = cap['table']
        for a in self.actors:
            a['visible'] = a['defined'] and a['draw'] == 'draw' and not a['dead'] and a['pos'] is not None

    # ---- player input ----
    def activate(self, obj):
        self._call(obj, 1, 1)            # function 1 (activate) at priority 1
    def touch(self, obj):
        self._call(obj, 2, 2)            # function 2 (touch) at priority 2
    def _call(self, obj, function, priority):
        if obj >= len(self.states):
            return
        st = self.states[obj]
        if st.addr is not None and st.prio <= priority:
            return                       # busy with something at least as urgent
        if st.addr is not None:
            st.ret[st.prio] = st.addr    # save current to resume after
        st.addr = self.objs[obj][function]
        st.prio = priority
        st.dc = 1
        st.tb = False

    def advance(self):
        """Dismiss the current textbox (player pressed confirm)."""
        self.textbox = None

    def idle(self, obj):
        # Idle = not in a player-initiated interaction (its main loop runs at priority 7).
        return self.states[obj].addr is None or self.states[obj].prio >= 7

    def busy(self):
        # An interaction is in progress: a textbox is up, or some object is running an
        # activate/touch function (priority < 7). Idle main loops run at priority 7.
        return self.textbox is not None or any(s.addr is not None and s.prio < 7 for s in self.states)

    def _decode_dialog(self, idx):
        if self.table is None:
            return '<no string table>'
        page = self.table & 0xFF0000
        p = self.table + idx * 2
        if p + 1 >= len(self.rom):
            return '<oob>'
        ptr = self.rom[p] | (self.rom[p + 1] << 8)
        return text.decode_string(self.rom, page + ptr, self.subs)

    # ---- the scheduler ----
    def tick(self):
        self.frame += 1
        for obj in range(len(self.states)):
            st = self.states[obj]
            if st.addr is None:
                continue
            if st.dc > 1:
                st.dc -= 1
                continue
            st.dc = st.delay
            for _ in range(5):
                if self._step(obj) == 'yield':
                    break
        self.run_tasks()
        for a in self.actors:
            # keep `visible` live (a script may show/hide/move an actor mid-scene), then
            # advance every sprite's animation.
            a['visible'] = a['defined'] and a['draw'] == 'draw' and not a['dead'] and a['pos'] is not None
            if a['sprite'] is not None:
                self.tick_anim(a)

    def _anim_frames(self, sprite, ai):
        key = (sprite, ai)
        f = self._anim_cache.get(key)
        if f is None:
            try:
                f = sprites.build_anim(self.rom, sprite, ai)
            except Exception:
                f = []
            self._anim_cache[key] = f
        return f

    def tick_anim(self, a):
        """Advance one frame of actor `a`'s animation (CTViewer SpriteState.tick) and
        store the assembly frame to draw in a['frame'] (by facing)."""
        mode = a['anim_mode']
        if mode == 'static':
            a['frame'] = a['anim_static']; return
        if mode == 'none':                       # idle (0) / walking (1) auto-select
            want = 1 if (a['task'] and a['task']['cycles'] > 0) else 0
            if a['anim_index'] != want:
                a['anim_index'] = want; a['anim_frame'] = 0; a['anim_delay'] = 0
        ai = a['anim_il'] if mode == 'loopcount' else a['anim_index']
        frames = self._anim_frames(a['sprite'], ai)
        if not frames:
            a['frame'] = 0; return
        if a['anim_delay'] > 0:
            a['anim_delay'] -= 1
        if a['anim_delay'] <= 0:                  # advance to the next frame
            a['anim_frame'] += 1
            if a['anim_frame'] < len(frames):
                a['anim_delay'] = frames[a['anim_frame']][1]
            else:
                a['anim_delay'] = frames[0][1]
                if mode == 'loopcount' and a['anim_loops'] > 0:
                    if a['anim_loops'] <= 2:
                        a['anim_loops'] = 1; a['anim_frame'] = len(frames) - 1
                    else:
                        a['anim_loops'] -= 2; a['anim_frame'] = 0
                else:
                    a['anim_frame'] = 0
        fi = a['anim_frame'] if 0 <= a['anim_frame'] < len(frames) else 0
        a['frame'] = frames[fi][0][a['facing'] if 0 <= a['facing'] < 4 else 1]

    def run_tasks(self):
        """Per-frame movement integration: every actor with a move task steps pos by
        move_by and decrements its cycle count (CTViewer Actor::run_task)."""
        for a in self.actors:
            t = a['task']
            if t and t['cycles'] > 0 and a['pos'] is not None:
                t['cycles'] -= 1
                a['pos'] = (a['pos'][0] + t['mx'], a['pos'][1] + t['my'])

    def _face(self, a, mx, my):
        if abs(mx) >= abs(my):
            a['facing'] = 3 if mx > 0 else 2
        else:
            a['facing'] = 1 if my > 0 else 0

    def _move_op(self, obj, addr, op):
        """Set/advance an actor's movement task. Returns 'yield' (still moving) or
        'complete' (reached / no steps). move_by = move_speed*0.5*(cos,sin)."""
        a = self.actors[obj]
        st = self.states[obj]
        body, mem = self.body, self.mem
        if a['pos'] is None:
            return 'complete'
        t = a['task']
        if t and t['cycles'] > 0:                        # still moving toward the target
            return 'yield'
        sp = a['move_speed'] * 0.5
        if op in (0x92, 0x9C, 0x9D):                     # MoveByAngle (relative direction)
            if t is not None:                            # the previous leg finished
                a['task'] = None; return 'complete'
            angle = (body[addr + 1] if op == 0x92 else mem.r8(LOC + body[addr + 1] * 2)) * 1.40625
            steps = body[addr + 2]
            ar = math.radians(angle); mx, my = sp * math.cos(ar), sp * math.sin(ar)
            a['task'] = {'cycles': steps, 'mx': mx, 'my': my}
            self._face(a, mx, my)
            return 'yield'
        # MoveToTile (0x96/0x97/0xA0) or smove (0x9A) or MoveToActor (0x94)
        if op == 0x94:
            tgt = self.actors[body[addr + 1] // 2] if body[addr + 1] // 2 < len(self.actors) else None
            if tgt is None or tgt['pos'] is None:
                a['task'] = None; return 'complete'
            dtx, dty = int(tgt['pos'][0]) // 16, int(tgt['pos'][1]) // 16
            steps = st.delay
        elif op in (0x96, 0xA0):
            dtx, dty, steps = body[addr + 1], body[addr + 2], st.delay
        elif op == 0x97:
            dtx, dty, steps = mem.r8(LOC + body[addr + 1] * 2), mem.r8(LOC + body[addr + 2] * 2), st.delay
        else:                                            # 0x9A smove (local-mem coords + explicit steps)
            dtx, dty, steps = mem.r8(LOC + body[addr + 1] * 2), mem.r8(LOC + body[addr + 2] * 2), body[addr + 3]
        atx, aty = int(a['pos'][0]) // 16, int(a['pos'][1]) // 16
        if (atx, aty) == (dtx, dty) or steps == 0:       # destination tile reached
            a['task'] = None; return 'complete'
        ang = math.atan2(dty - aty, dtx - atx)
        mx, my = sp * math.cos(ang), sp * math.sin(ang)
        a['task'] = {'cycles': steps, 'mx': mx, 'my': my}
        self._face(a, mx, my)
        return 'yield'

    def npc_move_angle(self, obj, angle_deg, steps, speed=None):
        """Test/demo hook: give an actor a fixed-direction move task (like op 0x92)."""
        a = self.actors[obj]
        sp = (speed if speed is not None else a['move_speed']) * 0.5
        ar = math.radians(angle_deg)
        a['task'] = {'cycles': steps, 'mx': sp * math.cos(ar), 'my': sp * math.sin(ar)}
        self._face(a, a['task']['mx'], a['task']['my'])

    def _step(self, obj):
        st = self.states[obj]
        a = self.actors[obj]
        body, objs, mem = self.body, self.objs, self.mem
        addr = st.addr
        if addr is None or addr < 0 or addr >= len(body):
            st.addr = None
            return 'yield'
        try:
            op = body[addr]
            ln = op_len(body, addr)
        except IndexError:
            st.addr = None
            return 'yield'
        nxt = addr + ln

        # --- end / yield / wait ---
        if op == 0x00:                                   # Return: pop to a saved priority
            st.ret[st.prio] = 0
            if st.prio == 7:
                st.addr = None
                return 'yield'
            st.addr = None
            for p in range(st.prio + 1, 8):
                if st.ret[p] != 0:
                    st.prio = p; st.addr = st.ret[p]; break
            else:
                st.prio = 7
            return 'yield'
        if op == 0xB2 or op == 0xB0:                     # everstay / everpc: yield forever (stay put)
            return 'yield'
        if op == 0xB1 or op == 0xAF:                     # stay / pc: yield once
            st.addr = nxt
            return 'yield'
        if op == 0xAD:                                   # wait N ticks
            st.dc = body[addr + 1] + 1; st.addr = nxt; return 'yield'
        if op in (0xB9, 0xBA, 0xBC, 0xBD):
            st.dc = {0xB9: 4, 0xBA: 8, 0xBC: 16, 0xBD: 32}[op] + 1; st.addr = nxt; return 'yield'

        # --- textbox: block until dismissed ---
        if op in (0xBB, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4):
            if not st.tb:
                self.textbox = self._decode_dialog(body[addr + 1])
                st.tb = True
                return 'yield'                           # show + block (re-run next frame)
            if self.textbox is None:                     # dismissed -> continue
                st.tb = False; st.addr = nxt; return 'complete'
            return 'yield'                               # still showing
        if op == 0xB8:                                   # set string table
            self.table = (body[addr + 1] | (body[addr + 2] << 8) | (body[addr + 3] << 16)) - 0xC00000
            st.addr = nxt; return 'complete'

        # --- cross-object calls (req): priority switch / queue ---
        if op in (0x02, 0x03, 0x04, 0x05, 0x06, 0x07):
            st.addr = nxt
            if op in (0x02, 0x03, 0x04):                 # party-target variants (0x05-07) skipped
                self._req(body[addr + 1] // 2, body[addr + 2] & 0x0F, (body[addr + 2] & 0xF0) >> 4)
            return 'complete'

        # --- jumps / conditions (jump when FALSE) ---
        if op == 0x10: st.addr = addr + body[addr + 1] + 1; return 'jumped'
        if op == 0x11: st.addr = addr - body[addr + 1] + 1; return 'jumped'
        if op in (0x12, 0x13, 0x14, 0x15, 0x16, 0x18, 0x1A):
            jump, target = self._cond(obj, addr, op)
            st.addr = target if jump else nxt
            return 'jumped' if jump else 'complete'

        # --- animation ops: set the sprite's animation state (cycled by tick_anim) ---
        if op in (0xAA, 0xAB, 0xAC, 0xAE, 0xB3, 0xB4, 0xB7):
            script.apply_anim_op(a, op, body, addr)
            st.addr = nxt
            return 'complete'

        # --- movement ops: set/advance a per-frame move task (blocks until reached) ---
        if op in (0x92, 0x94, 0x96, 0x97, 0x9A, 0x9C, 0x9D, 0xA0):
            r = self._move_op(obj, addr, op)
            if r == 'complete':
                st.addr = nxt
            return r

        # --- data / math / bit / actor (same effects as the static interpreter) ---
        self._effect(obj, addr, op)
        st.addr = nxt
        return 'complete'

    def _req(self, target, function, priority):
        if target >= len(self.states) or function >= 16 or priority >= 8:
            return                                       # out-of-range req (or resumed garbage)
        ts = self.states[target]
        if ts.prio == priority:
            return
        if ts.addr is not None and ts.prio < priority:   # target busier: queue for later
            if ts.ret[ts.prio] == 0:
                ts.ret[priority] = self.objs[target][function]
            return
        if ts.addr is not None:                          # new is more urgent: save + switch
            ts.ret[ts.prio] = ts.addr
        ts.addr = self.objs[target][function]
        ts.prio = priority
        ts.dc = 1

    def _cond(self, obj, addr, op):
        body, mem, a = self.body, self.mem, self.actors[obj]
        if op == 0x12:
            jump = not _cmp(mem.r8(LOC + body[addr + 1] * 2), body[addr + 2], body[addr + 3])
            return jump, addr + body[addr + 4] + 4
        if op == 0x13:
            jump = not _cmp(mem.r16(LOC + body[addr + 1] * 2), body[addr + 2] | (body[addr + 3] << 8), body[addr + 4])
            return jump, addr + body[addr + 5] + 5
        if op == 0x14:
            jump = not _cmp(mem.r8(LOC + body[addr + 1] * 2), mem.r8(LOC + body[addr + 2] * 2), body[addr + 3])
            return jump, addr + body[addr + 4] + 4
        if op == 0x15:
            jump = not _cmp(mem.r16(LOC + body[addr + 1] * 2), mem.r16(LOC + body[addr + 2] * 2), body[addr + 3])
            return jump, addr + body[addr + 4] + 4
        if op == 0x16:
            li, rhs, cb = body[addr + 1], body[addr + 2], body[addr + 3]
            if cb & 0x80: li += 0x100
            jump = not _cmp(mem.r8(GLB + li), rhs, cb & 0x7F)
            return jump, addr + body[addr + 4] + 4
        if op == 0x18:
            jump = not (mem.r8(GLB) < body[addr + 1])
            return jump, addr + body[addr + 2] + 2
        if op == 0x1A:
            jump = not (a['result'] == body[addr + 1])
            return jump, addr + body[addr + 2] + 2
        return False, addr

    def _effect(self, obj, addr, op):
        body, mem = self.body, self.mem
        a = self.actors[obj]
        try:
            if op == 0x4F: mem.w8(LOC + body[addr + 2] * 2, body[addr + 1])
            elif op == 0x50: mem.w16(LOC + body[addr + 3] * 2, body[addr + 1] | (body[addr + 2] << 8))
            elif op == 0x75: mem.w8(LOC + body[addr + 1] * 2, 1)
            elif op == 0x77: mem.w8(LOC + body[addr + 1] * 2, 0)
            elif op == 0x55: mem.w8(GLB, mem.r8(LOC + body[addr + 1] * 2))
            elif op == 0x5A: mem.w8(GLB, body[addr + 1])
            elif op == 0x5B: i = LOC + body[addr + 2] * 2; mem.w8(i, mem.r8(i) + body[addr + 1])
            elif op == 0x5F: i = LOC + body[addr + 2] * 2; mem.w8(i, mem.r8(i) - body[addr + 1])
            elif op == 0x71: i = LOC + body[addr + 1] * 2; mem.w8(i, mem.r8(i) + 1)
            elif op == 0x73: i = LOC + body[addr + 1] * 2; mem.w8(i, mem.r8(i) - 1)
            elif op == 0x63: i = LOC + body[addr + 2] * 2; mem.w8(i, mem.r8(i) | (1 << (body[addr + 1] & 7)))
            elif op == 0x64: i = LOC + body[addr + 2] * 2; mem.w8(i, mem.r8(i) & ~(1 << (body[addr + 1] & 7)))
            elif op == 0x67: i = LOC + body[addr + 2] * 2; mem.w8(i, mem.r8(i) & body[addr + 1])
            elif op == 0x69: i = LOC + body[addr + 2] * 2; mem.w8(i, mem.r8(i) | body[addr + 1])
            elif op == 0x65:
                li = body[addr + 2] + (0x100 if body[addr + 1] & 0x80 else 0)
                mem.w8(GLB + li, mem.r8(GLB + li) | (1 << (body[addr + 1] & 7)))
            elif op == 0x66:
                li = body[addr + 2] + (0x100 if body[addr + 1] & 0x80 else 0)
                mem.w8(GLB + li, mem.r8(GLB + li) & ~(1 << (body[addr + 1] & 7)))
            elif op == 0x89: a['move_speed'] = body[addr + 1] / 18.0   # mspeed
            elif op in _PC: a['sprite'] = _PC[op]; a['defined'] = True; a['draw'] = 'draw'
            elif op == 0x80 or op == 0x81: a['sprite'] = body[addr + 1]; a['defined'] = True; a['draw'] = 'draw'
            elif op == 0x82: a['sprite'] = body[addr + 1] + 7; a['defined'] = True; a['draw'] = 'draw'
            elif op == 0x83: a['sprite'] = body[addr + 1] + 256; a['defined'] = True; a['draw'] = 'draw'
            elif op == 0x8B: a['pos'] = (body[addr + 1] * 16 + 8, body[addr + 2] * 16 + 16)
            elif op == 0x8C:                                 # vxy: coords from local memory
                vx, vy = mem.r16(LOC + body[addr + 1] * 2), mem.r16(LOC + body[addr + 2] * 2)
                if vx or vy:
                    a['pos'] = (vx, vy)
            elif op == 0x8D: a['pos'] = ((body[addr + 1] | (body[addr + 2] << 8)) >> 4, (body[addr + 3] | (body[addr + 4] << 8)) >> 4)
            elif op == 0x0F: a['facing'] = 0
            elif op == 0x17: a['facing'] = 1
            elif op == 0x1B: a['facing'] = 2
            elif op == 0x1D: a['facing'] = 3
            elif op == 0xA6: a['facing'] = body[addr + 1]
            elif op == 0x90: a['draw'] = 'draw'
            elif op == 0x91: a['draw'] = 'hidden'
            elif op == 0x7E: a['draw'] = 'removed'
            elif op == 0x7C:
                t = body[addr + 1] // 2
                if t < len(self.actors): self.actors[t]['draw'] = 'draw'
            elif op == 0x7D:
                t = body[addr + 1] // 2
                if t < len(self.actors): self.actors[t]['draw'] = 'hidden'
            elif op == 0x0A:
                t = body[addr + 1] // 2
                if t < len(self.actors): self.actors[t]['dead'] = True; self.actors[t]['draw'] = 'hidden'
            # all other ops: no state effect (advance only)
        except IndexError:
            pass

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print(__doc__); sys.exit(1)
    rom = _romlib.load(sys.argv[1])
    scene = int(sys.argv[2])
    mode = sys.argv[3]
    eng = SceneEngine(rom, scene)
    if mode == 'talk':
        obj = int(sys.argv[4])
        eng.activate(obj)
        pages, guard = [], 0
        while eng.busy() and guard < 4000:
            eng.tick(); guard += 1
            if eng.textbox is not None:
                pages.append(eng.textbox)
                eng.advance()                            # auto-confirm to step through pages
        print("scene %d: activate obj %d -> %d frame(s), %d textbox page(s):" % (scene, obj, guard, len(pages)))
        for i, p in enumerate(pages):
            print("  [%d] %s" % (i, p.replace('\n', ' / ')))
    elif mode == 'run':
        frames = int(sys.argv[4]) if len(sys.argv) > 4 else 60
        for _ in range(frames):
            eng.tick()
        vis = sum(1 for a in eng.actors if a['visible'])
        print("scene %d: ran %d frames, %d objects, %d visible actors" % (scene, frames, len(eng.objs), vis))
    elif mode == 'walk':
        # Walk the player in a direction until a wall blocks it; render a ghost trail.
        dirs = {'r': (1, 0), 'l': (-1, 0), 'u': (0, -1), 'd': (0, 1)}
        d = dirs.get(sys.argv[4] if len(sys.argv) > 4 else 'r', (1, 0))
        out = sys.argv[5] if len(sys.argv) > 5 else None
        start = eng.player['pos']
        trail = [start]
        steps = 0
        for _ in range(300):
            if not eng.move_player(*d, speed=2.0):
                break                                    # blocked by a wall
            steps += 1
            trail.append(eng.player['pos'])
        end = eng.player['pos']
        print("scene %d: player spawn %s, walked %s %d steps -> %s (%s)"
              % (scene, tuple(map(int, start)), sys.argv[4] if len(sys.argv) > 4 else 'r', steps,
                 tuple(map(int, end)), 'blocked by wall' if steps < 299 else 'open'))
        if out:
            import field, sprites
            fac = eng.player['facing']
            ghosts = [(0, trail[i], sprites.pick_frame(rom, 0, fac, True, i))
                      for i in range(0, len(trail), max(1, len(trail) // 6))]
            ghosts.append((0, end, sprites.pick_frame(rom, 0, fac, False, 0)))   # final: standing
            field.render_field(rom, scene, out, extra=ghosts)
            print("  -> rendered walk trail (facing %d, walk cycle) to '%s'" % (fac, out))
    elif mode == 'npcwalk':
        # Movement-physics demo: give a visible NPC a fixed-direction move task and
        # integrate it over frames (the same task system real move ops drive), trail it.
        obj = int(sys.argv[4])
        ang = {'r': 0.0, 'd': 90.0, 'l': 180.0, 'u': 270.0}.get(sys.argv[5] if len(sys.argv) > 5 else 'r', 0.0)
        out = sys.argv[6] if len(sys.argv) > 6 else None
        a = eng.actors[obj]
        if a['pos'] is None:
            print("obj %d has no position" % obj); sys.exit(1)
        start = a['pos']
        eng.actors[obj]['move_speed'] = 8.0                  # fast for a visible demo
        eng.npc_move_angle(obj, ang, 60)
        trail = [start]
        for _ in range(60):
            eng.run_tasks()
            trail.append(a['pos'])
        end = a['pos']
        print("scene %d: NPC obj %d moved %s from %s to %s over %d frames"
              % (scene, obj, sys.argv[5] if len(sys.argv) > 5 else 'r',
                 tuple(map(int, start)), tuple(map(int, end)), 60))
        if out:
            import field, sprites
            fac = a['facing']
            ghosts = [(a['sprite'], trail[i], sprites.pick_frame(rom, a['sprite'], fac, True, i))
                      for i in range(0, len(trail), 10)]
            ghosts.append((a['sprite'], end, sprites.pick_frame(rom, a['sprite'], fac, False, 0)))
            field.render_field(rom, scene, out, extra=ghosts)
            print("  -> rendered NPC move trail (facing %d, walk cycle) to '%s'" % (fac, out))
    elif mode == 'exits':
        print("scene %d: %d exits" % (scene, len(eng.exits)))
        for i, e in enumerate(eng.exits):
            print("  [%d] pos%s size%s -> %s %d at %s facing %d"
                  % (i, e['pos'], e['size'], e['kind'], e['dest'], e['dpos'], e['facing']))
    elif mode == 'door':
        # Simulate stepping onto the first scene-exit -> arrive in the destination.
        out = sys.argv[4] if len(sys.argv) > 4 else None
        se = [e for e in eng.exits if e['kind'] == 'scene']
        if not se:
            print("scene %d: no scene exits" % scene)
        else:
            e = se[0]
            eng.player['pos'] = (e['pos'][0] + e['size'][0] // 2, e['pos'][1] + e['size'][1] // 2)
            hit = eng.exit_at()
            print("scene %d: on exit %s -> scene %d at %s facing %d (exit_at: %s)"
                  % (scene, e['pos'], e['dest'], e['dpos'], e['facing'], 'OK' if hit else 'MISS'))
            if out:
                import field
                field.render_field(rom, e['dest'], out, extra=[(0, e['dpos'], 0)])
                print("  -> arrival in scene %d (player at entry) rendered to '%s'" % (e['dest'], out))
    elif mode == 'zmap':
        # Visualize the per-tile Z-plane + flags (CTViewer `v` debug view): plane 0=blue,
        # 1=green (normal), 2=red (other level), 3=yellow (transition); DOOR tiles=magenta.
        out = sys.argv[4] if len(sys.argv) > 4 else None
        import field, maps
        from palettes import write_png_rgb
        from collections import Counter
        px, W, H, _ = field.compose_scene(rom, scene)
        cw, ch, _coll, zp, zf, _mv = maps.scene_props(rom, scene)
        TINT = {0: (40, 80, 220), 1: (40, 200, 40), 2: (220, 40, 40), 3: (220, 220, 40)}
        for y in range(H):
            ty = y // 16
            for x in range(W):
                tx = x // 16
                if tx >= cw or ty >= ch:
                    continue
                i2 = ty * cw + tx
                tr, tg, tb = (255, 0, 255) if (zf[i2] & 8) else TINT.get(zp[i2], (0, 0, 0))
                o = (y * W + x) * 3
                px[o] = (px[o] + tr) // 2; px[o + 1] = (px[o + 1] + tg) // 2; px[o + 2] = (px[o + 2] + tb) // 2
        h = rom[0x360000 + scene * 14: 0x360000 + scene * 14 + 14]
        if h[10] != 0x80:
            cx0, cy0 = max(0, h[10] * 16), max(0, h[11] * 16)
            cx1, cy1 = min(W, (h[12] + 1) * 16), min(H, (h[13] + 1) * 16)
            cwp, chp = cx1 - cx0, cy1 - cy0
            if cwp > 0 and chp > 0 and (cwp, chp) != (W, H):
                crop = bytearray(cwp * chp * 3)
                for y in range(chp):
                    s = ((cy0 + y) * W + cx0) * 3
                    crop[y * cwp * 3:(y + 1) * cwp * 3] = px[s:s + cwp * 3]
                px, W, H = crop, cwp, chp
        if out:
            write_png_rgb(out, px, W, H)
        print("scene %d z-plane: counts %s, %d door tiles -> %dx%d '%s'"
              % (scene, dict(Counter(zp)), sum(1 for f in zf if f & 8), W, H, out))
    elif mode == 'populate':
        # Populate a scene from a story-flag state: `@file` loads the REAL global flags
        # ($7F0000) captured from the oracle (ct.exe --dumpflags), else a hex byte-fill.
        arg = sys.argv[4] if len(sys.argv) > 4 else 'FF'
        out = sys.argv[5] if len(sys.argv) > 5 else None
        if arg.startswith('@'):
            gmem = open(arg[1:], 'rb').read()[:0x200]
            src = 'oracle flags %s (%d non-zero)' % (arg[1:], sum(1 for b in gmem if b))
        else:
            gmem = bytes([int(arg, 16) & 0xFF]) * 0x200
            src = 'flags 0x%02X' % (int(arg, 16) & 0xFF)
        eng = SceneEngine(rom, scene, gmem=gmem)
        placed = [a for a in eng.actors if a['visible'] and a['sprite'] is not None and a['pos'] is not None]
        print("scene %d with %s: %d placed actors" % (scene, src, len(placed)))
        if out:
            import field
            from palettes import write_png_rgb
            px, W, H, _ = field.compose_scene(rom, scene, actors=False)
            for a in placed:
                f = sprites.pick_frame(rom, a['sprite'], a['facing'], False, 0)
                field.blit_sprite(rom, px, W, H, a['sprite'], a['pos'], f)
            h = rom[0x360000 + scene * 14: 0x360000 + scene * 14 + 14]
            if h[10] != 0x80:
                cx0, cy0 = max(0, h[10] * 16), max(0, h[11] * 16)
                cx1, cy1 = min(W, (h[12] + 1) * 16), min(H, (h[13] + 1) * 16)
                cwp, chp = cx1 - cx0, cy1 - cy0
                if cwp > 0 and chp > 0 and (cwp, chp) != (W, H):
                    crop = bytearray(cwp * chp * 3)
                    for y in range(chp):
                        s = ((cy0 + y) * W + cx0) * 3
                        crop[y * cwp * 3:(y + 1) * cwp * 3] = px[s:s + cwp * 3]
                    px, W, H = crop, cwp, chp
            write_png_rgb(out, px, W, H)
            print("  -> rendered to '%s'" % out)
    elif mode == 'idle':
        # Run the NPC main loops N frames; render the live (animated/moved) actors.
        frames = int(sys.argv[4]) if len(sys.argv) > 4 else 90
        out = sys.argv[5] if len(sys.argv) > 5 else None
        eng.tick()
        snap0 = [(a['frame'], a['pos']) for a in eng.actors]
        for _ in range(frames - 1):
            eng.tick()
        vis = [i for i, a in enumerate(eng.actors) if a['visible']]
        animc = sum(1 for i in vis if eng.actors[i]['frame'] != snap0[i][0])
        movec = sum(1 for i in vis if eng.actors[i]['pos'] != snap0[i][1])
        print("scene %d after %d frames: %d visible, %d animating (frame changed), %d moving (pos changed)"
              % (scene, frames, len(vis), animc, movec))
        if out:
            import field
            from palettes import write_png_rgb
            px, W, H, _ = field.compose_scene(rom, scene, actors=False)
            for a in eng.actors:
                if a['visible'] and a['sprite'] is not None and a['pos'] is not None:
                    field.blit_sprite(rom, px, W, H, a['sprite'], a['pos'], a['frame'])
            h = rom[0x360000 + scene * 14: 0x360000 + scene * 14 + 14]
            if h[10] != 0x80:
                cx0, cy0 = max(0, h[10] * 16), max(0, h[11] * 16)
                cx1, cy1 = min(W, (h[12] + 1) * 16), min(H, (h[13] + 1) * 16)
                cwp, chp = cx1 - cx0, cy1 - cy0
                if cwp > 0 and chp > 0 and (cwp, chp) != (W, H):
                    crop = bytearray(cwp * chp * 3)
                    for y in range(chp):
                        s = ((cy0 + y) * W + cx0) * 3
                        crop[y * cwp * 3:(y + 1) * cwp * 3] = px[s:s + cwp * 3]
                    px, W, H = crop, cwp, chp
            write_png_rgb(out, px, W, H)
            print("  -> rendered live scene to '%s'" % out)
    elif mode == 'collmap':
        # Validate walkability: tint solid sub-tile regions red over the scene.
        out = sys.argv[4] if len(sys.argv) > 4 else None
        import field, maps
        from palettes import write_png_rgb
        px, W, H, _ = field.compose_scene(rom, scene)
        cw, ch, coll = maps.scene_collision(rom, scene)
        nsolid = 0
        for y in range(H):
            ty, sy = y // 16, y % 16
            for x in range(W):
                tx = x // 16
                if tx < cw and ty < ch and solid_at(coll[ty * cw + tx], x % 16, sy):
                    o = (y * W + x) * 3
                    px[o] = min(255, px[o] // 2 + 128); px[o + 1] //= 2; px[o + 2] //= 2
                    nsolid += 1
        h = rom[0x360000 + scene * 14: 0x360000 + scene * 14 + 14]
        if h[10] != 0x80:
            cx0, cy0 = max(0, h[10] * 16), max(0, h[11] * 16)
            cx1, cy1 = min(W, (h[12] + 1) * 16), min(H, (h[13] + 1) * 16)
            cwp, chp = cx1 - cx0, cy1 - cy0
            if cwp > 0 and chp > 0 and (cwp, chp) != (W, H):
                crop = bytearray(cwp * chp * 3)
                for y in range(chp):
                    s = ((cy0 + y) * W + cx0) * 3
                    crop[y * cwp * 3:(y + 1) * cwp * 3] = px[s:s + cwp * 3]
                px, W, H = crop, cwp, chp
        if out:
            write_png_rgb(out, px, W, H)
        print("scene %d collision overlay: %d solid px -> %dx%d '%s'" % (scene, nsolid, W, H, out))
    else:
        print("unknown mode:", mode); sys.exit(1)
