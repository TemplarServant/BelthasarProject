"""Field event-VM scene-script access + annotated disassembly.

Scene header: 14 bytes @ 0x360000 + scene*14; scriptId = h[8] | h[9]<<8.
Script decompression / fn-pointer tables / opcode lengths come from the
canonical assets/script.py (load_script / iter_ops / op_len) -- imported via
a sys.path bootstrap, NOT duplicated.

The ~90-entry opcode annotation table `ann()` is lifted VERBATIM from
tools/_demo_trace.py (parameterized on `body`; that script hardcoded scene 8).
Annotations are best-effort RE labels -- several ops are still '?'.

API:
  scene_header(rom, scene) -> {'raw': 14 bytes, 'scriptId': int}
  load(rom, scene)         -> (objs [16-ptr lists], body bytes, scriptId)
  ann(body, o, op, ln)     -> annotation string for the op at body[o]
  disassemble(rom, scene, obj=None, out=print)
      obj=None: print the fn-pointer table for every object
      obj=N   : table row + annotated listing of each unique fn of object N
"""
import sys

from . import ASSETS_DIR

SCENE_HDR_BASE = 0x360000
SCENE_HDR_LEN = 14


def _assets_script():
    """Import the canonical assets/script.py (module name 'script')."""
    if ASSETS_DIR not in sys.path:
        sys.path.insert(0, ASSETS_DIR)
    import script as assets_script
    return assets_script


def scene_header(rom, scene):
    o = SCENE_HDR_BASE + scene * SCENE_HDR_LEN
    h = rom[o:o + SCENE_HDR_LEN]
    if len(h) < SCENE_HDR_LEN:
        raise ValueError("scene %d header out of ROM range" % scene)
    return {'raw': bytes(h), 'scriptId': h[8] | (h[9] << 8)}


def load(rom, scene):
    """Load a scene's event script -> (objs, body, scriptId)."""
    s = _assets_script()
    si = scene_header(rom, scene)['scriptId']
    objs, body = s.load_script(rom, si)
    return objs, body, si


# --- opcode annotator, VERBATIM from tools/_demo_trace.py ann() -------------
# (only change: `body` is a parameter instead of a module global)
def ann(body, o, op, ln):
    b = body
    args = [b[o+i] for i in range(1, ln) if o+i < len(body)]
    def hx(a): return ' '.join('%02X'%x for x in a)
    s = ''
    if op in (0x02,0x03,0x04):
        s = 'CALL/REQ obj%d fn%d (cont=%d)' % (b[o+1]//2, b[o+2]&0x0F, b[o+2])
    elif op in (0x05,0x06,0x07):
        s = 'CALLpc%d fn%d' % (b[o+1], b[o+2]&0x0F)
    elif op == 0x00: s='RETURN'
    elif op == 0x08: s='END(?)'
    elif op == 0x10: s='JUMPFWD +%d -> 0x%04X'%(b[o+1], o+b[o+1]+1)
    elif op == 0x11: s='JUMPBACK -%d -> 0x%04X'%(b[o+1], o-b[o+1]+1)
    elif op == 0x12: s='IF loc[%d]?%d cmp%d else+%d'%(b[o+1],b[o+2],b[o+3],b[o+4])
    elif op == 0x16: s='IFFLAG glb[%d]bit/cmp%d else+%d'%(b[o+1],b[o+3],b[o+4])
    elif op == 0x18: s='IF storyline<%d else+%d'%(b[o+1],b[o+2])
    elif op == 0x1A: s='CASE result==%d else+%d'%(b[o+1],b[o+2])
    elif op == 0x1C: s='Set $7E byte? %s'%hx(args)
    elif op == 0x5A: s='SETSTORYLINE=%d'%b[o+1]
    elif op == 0x55: s='loc[%d]=STORYLINE'%b[o+1]
    elif op == 0x63: s='SETLOCBIT loc[%d].bit%d'%(b[o+2],b[o+1]&7)
    elif op == 0x65: s='SETGLBFLAG glb[%d].bit%d (+0x100 if hi)'%(b[o+2],b[o+1]&7)
    elif op == 0x66: s='CLRGLBFLAG glb[%d].bit%d'%(b[o+2],b[o+1]&7)
    elif op == 0x4F: s='loc[%d]=%d'%(b[o+2],b[o+1])
    elif op == 0x50: s='loc[%d]=%d (16)'%(b[o+3],b[o+1]|(b[o+2]<<8))
    elif op == 0x75: s='loc[%d]=1'%b[o+1]
    elif op == 0x77: s='loc[%d]=0'%b[o+1]
    elif op in (0x57,0x5C,0x62,0x68,0x6A,0x6C,0x6D): s='LOADPC sprite (PC%d)'%({0x57:0,0x5C:1,0x62:2,0x68:3,0x6A:4,0x6C:5,0x6D:6}[op])
    elif op == 0x80: s='LOADNPC spr=%d'%b[o+1]
    elif op == 0x81: s='LOADNPC2 spr=%d'%b[o+1]
    elif op == 0x82: s='LOADPCsprite spr=%d'%(b[o+1]+7)
    elif op == 0x83: s='LOADMON spr=%d'%(b[o+1]+256)
    elif op == 0x84: s='SETSOLID %s'%hx(args)
    elif op == 0x8B: s='SETCOORD tile(%d,%d) -> px(%d,%d)'%(b[o+1],b[o+2],b[o+1]*16+8,b[o+2]*16+16)
    elif op == 0x8C: s='SETCOORDvar loc[%d],loc[%d]'%(b[o+1],b[o+2])
    elif op == 0x8D: s='SETCOORDpx (%d,%d)>>4=(%d,%d)'%(b[o+1]|(b[o+2]<<8), b[o+3]|(b[o+4]<<8), (b[o+1]|(b[o+2]<<8))>>4, (b[o+3]|(b[o+4]<<8))>>4)
    elif op == 0x8E: s='SETPRIO/group %s'%hx(args)
    elif op == 0x8F: s='? %s'%hx(args)
    elif op == 0x0F: s='FACE up(0)'
    elif op == 0x17: s='FACE down(1)'
    elif op == 0x1B: s='FACE left(2)'
    elif op == 0x1D: s='FACE right(3)'
    elif op == 0xA6: s='FACE dir=%d'%b[o+1]
    elif op == 0xA8: s='FACETOWARD obj%d'%(b[o+1]//2)
    elif op == 0xA9: s='FACETOWARD pc%d'%(b[o+1])
    elif op == 0x90: s='SHOW (draw on)'
    elif op == 0x91: s='HIDE (draw off)'
    elif op == 0x7E: s='REMOVE actor'
    elif op == 0x7C: s='SHOW obj%d'%(b[o+1]//2)
    elif op == 0x7D: s='HIDE obj%d'%(b[o+1]//2)
    elif op == 0x0A: s='KILL obj%d'%(b[o+1]//2)
    elif op == 0x92: s='MOVE/VECTOR %s'%hx(args)
    elif op == 0x94: s='MOVE toward obj? %s'%hx(args)
    elif op == 0x95: s='MOVE toward pc? %s'%hx(args)
    elif op == 0x96: s='MOVETO tile(%d,%d)'%(b[o+1],b[o+2])
    elif op == 0x97: s='MOVETOvar loc[%d],loc[%d]'%(b[o+1],b[o+2])
    elif op == 0x98: s='MOVEangle? %s'%hx(args)
    elif op == 0x99: s='MOVE %s'%hx(args)
    elif op == 0x9A: s='MOVE %s'%hx(args)
    elif op == 0x9C: s='MOVE %s'%hx(args)
    elif op == 0x9D: s='MOVE %s'%hx(args)
    elif op == 0xAA: s='ANIMESET %d (loop)'%b[o+1]
    elif op == 0xAB: s='ANIME %d (once)'%b[o+1]
    elif op == 0xAC: s='SHAPE frame %d (freeze)'%b[o+1]
    elif op == 0xAE: s='AUTOANIME (idle)'
    elif op == 0xB3: s='ANIMESET0'
    elif op == 0xB4: s='ANIMESET1'
    elif op == 0xB5: s='CHASE/move? %s'%hx(args)
    elif op == 0xB6: s='CHASE? %s'%hx(args)
    elif op == 0xB7: s='ANIMEREPEAT %d x%d'%(b[o+1],b[o+2])
    elif op == 0xAD: s='WAIT %d/16 s'%b[o+1]
    elif op == 0xB8: s='STRINGTABLE @%06X'%((b[o+1]|(b[o+2]<<8)|(b[o+3]<<16)))
    elif op == 0xBB: s='TEXTBOX auto str=%d'%b[o+1]
    elif op == 0xC0: s='TEXTBOX str=%d'%b[o+2]
    elif op == 0xC1: s='TEXTBOX top str=%d'%b[o+1]
    elif op == 0xC2: s='TEXTBOX bottom str=%d'%b[o+1]
    elif op == 0xC3: s='TEXTBOX_C3 str=%d'%b[o+1]
    elif op == 0xC4: s='TEXTBOX_C4 str=%d'%b[o+1]
    elif op == 0xAF: s='YIELD_FOREVER(0xAF end)'
    elif op == 0xB0: s='YIELD(0xB0)'
    elif op == 0xB1: s='YIELD(0xB1)'
    elif op == 0xB2: s='HANDTOPLAYER(0xB2)'
    elif op == 0xB9: s='? %s'%hx(args)
    elif op == 0xDC: s='CHANGELOC %s'%hx(args)
    elif op == 0xDD: s='CHANGELOC %s loc=0x%X tileX=%d tileY=%d facing=%d'%(hx(args), (b[o+1]|(b[o+2]<<8))&0x1FF, b[o+3], b[o+4], ((b[o+2]>>1)&3) if ln>2 else 0)
    elif op == 0xDE: s='CHANGELOC %s'%hx(args)
    elif op == 0xDF: s='CHANGELOC %s'%hx(args)
    elif op == 0xE0: s='CHANGELOC %s'%hx(args)
    elif op == 0xE1: s='CHANGELOC %s'%hx(args)
    elif op == 0xE3: s='SETCTRL $011F %s'%hx(args)
    elif op == 0xE7: s='SCROLL? %s'%hx(args)
    elif op == 0xEA: s='? %s'%hx(args)
    elif op == 0xEB: s='? %s'%hx(args)
    elif op == 0x88: s='COLOR/PAL %s'%hx(args)
    return s
# --- end verbatim ann() ------------------------------------------------------


def disassemble(rom, scene, obj=None, out=print):
    """Print the fn-pointer table (all objects, or just `obj`) and, when `obj`
    is given, an annotated listing of each of its unique functions."""
    s = _assets_script()
    objs, body, si = load(rom, scene)
    out('scene %d -> script %d : %d objects, body %d bytes'
        % (scene, si, len(objs), len(body)))
    if not objs:
        out('(empty/unused script entry)')
        return
    rng = range(len(objs)) if obj is None else [obj]
    out('### Object fn-pointer table (slot:addr) ###')
    for ob in rng:
        pts = objs[ob]
        out('obj%2d: %s' % (ob, ' '.join('%d=0x%04X' % (i, pts[i]) for i in range(16))))
    if obj is None:
        return
    # annotated listing of each unique function of `obj`
    pts = objs[obj]
    seen = {}
    for fn, start in enumerate(pts):
        seen.setdefault(start, []).append(fn)
    for start, fns in seen.items():
        out('')
        out('==== obj%d fn%s @0x%04X ====' % (obj, '/'.join(str(f) for f in fns), start))
        if start < 0 or start >= len(body):
            out('  (out of range / empty)')
            continue
        for o, op, ln in s.iter_ops(body, start, limit=3000):
            a = ann(body, o, op, ln)
            raw = ' '.join('%02X' % body[o + i] for i in range(1, ln) if o + i < len(body))
            out('  0x%04X: %02X %-16s %s' % (o, op, raw, a))
