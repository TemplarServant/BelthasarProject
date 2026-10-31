#!/usr/bin/env python3
"""Parse Chrono Trigger (SNES/USA) scene scripts (the field event bytecode) and
extract each scene's objects/actors: their sprite, initial position and facing.
Follows CTViewer's scene_script_decoder.rs, checked against the ROM.

Scene scripts: 513-entry pointer table @ 0x3CF9F0 (3-byte: u16 offset + bank), the
data is LZ-compressed. A decompressed script is: [actor_count u8][actor_count x 16
u16 function pointers (rebased - header_size)][shared bytecode body]. Each object's
16 slots: 0=init, 1=activate, 2=touch, 3..15=other. Execution starts at slot 0.

There is no static "sprite/x/y" record; those come from interpreting the init
function (slot 0). This walks it linearly (advancing by each opcode's length) and records
the sprite-load / coordinate / facing ops, which covers most static placements.

Usage:
  python assets/script.py <rom.sfc> scene <index>                 # list a scene's objects
  python assets/script.py <rom.sfc> disasm <index> [obj] [slot]   # disassemble a function
  python assets/script.py <rom.sfc> talk <index> <obj> [slot]     # simulate activate -> dialogue
"""
import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '../tools')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lz import lz_decompress
from sprites import get_entries_3byte

SCRIPT_PTRS = 0x3CF9F0

# Total instruction length (incl. opcode byte), SNES. Variable ops handled in op_len.
OPLEN = {}
def _set(n, *ops):
    for o in ops:
        OPLEN[o] = n
_set(1, 0x00, 0x08, 0x09, 0x0F, 0x17, 0x1B, 0x1D, 0x47, 0x57, 0x5C, 0x62, 0x68, 0x6A, 0x6C,
      0x6D, 0x79, 0x7E, 0x85, 0x86, 0x90, 0x91, 0xA2, 0xA3, 0xA4, 0xA5, 0xAE, 0xAF, 0xB0,
      0xB1, 0xB2, 0xB3, 0xB4, 0xB9, 0xBA, 0xBC, 0xBD, 0xDA, 0xED, 0xEE, 0xF2, 0xF3, 0xF8,
      0xF9, 0xFA, 0xFB, 0xFC, 0x2A, 0x2B, 0x32)
_set(2, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x10, 0x11, 0x19, 0x1C, 0x1E, 0x1F, 0x20, 0x25, 0x26,
      0x29, 0x33, 0x55, 0x5A, 0x71, 0x72, 0x73, 0x75, 0x76, 0x77, 0x7C, 0x7D, 0x7F, 0x80,
      0x81, 0x82, 0x84, 0x87, 0x89, 0x8A, 0x8E, 0x8F, 0x94, 0x95, 0x9E, 0x9F, 0xA6, 0xA7,
      0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xB5, 0xB6, 0xBB, 0xC1, 0xC2, 0xC5, 0xC7, 0xC8,
      0xCA, 0xCB, 0xD0, 0xD1, 0xD3, 0xD4, 0xD6, 0xE3, 0xE8, 0xEA, 0xF0, 0xF4, 0xFD,
      0x2D, 0x30, 0x31, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3B, 0x3C, 0x3F, 0x40, 0x41,
      0x42, 0x43, 0x44)
_set(3, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x18, 0x1A, 0x23, 0x24, 0x27, 0x28, 0x2C, 0x2F,
      0x3A, 0x3D, 0x3E, 0x45, 0x46, 0x4F, 0x51, 0x52, 0x5B, 0x5D, 0x5E, 0x5F, 0x61,
      0x63, 0x64, 0x65, 0x66, 0x67, 0x69, 0x6B, 0x6F, 0x70, 0x74, 0x78, 0x83, 0x8B, 0x8C,
      0x92, 0x96, 0x97, 0x98, 0x99, 0x9C, 0x9D, 0xA0, 0xA1, 0xB7, 0xC0, 0xC3, 0xC4, 0xC9, 0xCD,
      0xCE, 0xCF, 0xD2, 0xD5, 0xD7, 0xD8, 0xEB, 0xE7)   # 0xB7 ANIMEREPEAT = op + anim + count (len 3)
_set(4, 0x21, 0x22, 0x50, 0x53, 0x54, 0x56, 0x58, 0x59, 0x60, 0x7A, 0x9A, 0xB8, 0xCC, 0xEC)   # 0x60: C0/6AC9 word + local, INY x3
_set(5, 0x12, 0x14, 0x15, 0x16, 0x6E, 0x48, 0x49, 0x4A, 0x4C, 0x4D, 0x7B, 0x8D, 0xE2, 0xE6,
      0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1)
_set(6, 0x13, 0x4B)
_set(7, 0xD9)
_set(8, 0xE4, 0xE5)
_set(18, 0xFE)

def op_len(b, o):
    op = b[o]
    if op == 0x4E:                                   # datawrite: addr(3) + blob(L)
        return 4 + (b[o + 4] | (b[o + 5] << 8))
    if op == 0x2E:                                   # bgcolor
        cm = b[o + 1]
        if cm & 0x40: return 6
        if cm & 0x80: return 3 + (b[o + 3] | (b[o + 4] << 8))
        return 2
    if op == 0x88:                                   # color
        cmd = b[o + 1]
        if cmd == 0x00: return 2
        if cmd in (0x20, 0x30): return 4
        if 0x40 <= cmd < 0x60: return 5
        if 0x80 <= cmd < 0x90: return 2 + (b[o + 2] | (b[o + 3] << 8))
        return 2
    if op == 0xF1:                                   # flash
        return 2 if b[o + 1] == 0 else 3
    if op == 0xFF:                                   # special
        return 5 if b[o + 1] in (0x90, 0x97) else 2
    return OPLEN.get(op, 1)

def load_script(rom, script_index):
    """Decompress a scene script -> (objects[16-ptr lists], body bytes)."""
    ptrs = get_entries_3byte(rom, SCRIPT_PTRS, 513)
    if script_index >= len(ptrs) or ptrs[script_index] == 0:
        return [], b''
    raw = lz_decompress(rom, ptrs[script_index])
    if len(raw) < 1:
        return [], b''
    count = raw[0]
    header_size = count * 32
    objs = []
    o = 1
    for _ in range(count):
        objs.append([(raw[o + i * 2] | (raw[o + i * 2 + 1] << 8)) - header_size for i in range(16)])
        o += 32
    return objs, raw[1 + header_size:]

def iter_ops(body, start, limit=4000):
    """Walk ops from `start`, stopping at Return (0x00), EOF, or `limit` ops."""
    o = start
    n = 0
    while 0 <= o < len(body) and n < limit:
        op = body[o]
        try:
            ln = op_len(body, o)
        except IndexError:
            break
        yield o, op, ln
        if op == 0x00:
            break
        o += max(1, ln)
        n += 1

# The seven one-byte PC loaders that hardwire their char id. Read out of the ROM: the event-op jump
# table at C0:5D6E, then each handler's own immediate.
#   op 57 -> C0/41E4  LDA $6D / STA $8D / LDA #00   id 0 Crono
#   op 5C -> C0/41EC            STA $8E / LDA #01   id 1 Marle
#   op 62 -> C0/41F4            STA $8F / LDA #02   id 2 Lucca
#   op 6A -> C0/4204            STA $90 / LDA #03   id 3 Robo   <-- the later handler, lower id
#   op 68 -> C0/41FC            STA $91 / LDA #04   id 4 Frog   <-- the earlier one, higher id
#   op 6C -> C0/420C            STA $92 / LDA #05   id 5 Ayla
#   op 6D -> C0/4214            STA $93 / LDA #06   id 6 Magus
# Note: the mapping is not monotonic in the opcode. This dict bakes the packed actor table in ct_assets.dat,
# the reference `ct.exe --vm` compares src/ct_vm.c's pcSprite() against (disasm/notes/pc_loader_id_map_swap.md).
_PC = {0x57: 0, 0x5C: 1, 0x62: 2, 0x6A: 3, 0x68: 4, 0x6C: 5, 0x6D: 6}

class Mem:
    """Scene-script WRAM: global $7F0000 (storyline at 0), local $7F0200 (cleared per
    scene), system $7E0000. 16-bit values are little-endian, as the 65816 stores them (not CTViewer's
    big-endian convention), matching src/ct_vm.c r16/w16 and move.c CtVmVarFamily."""
    def __init__(self):
        self.g = bytearray(0x200)
        self.l = bytearray(0x200)
        self.s = bytearray(0x10000)
        for a, b in [(0x0104, 192), (0x1BA7, 14), (0x1BAA, 4), (0x1BAB, 1), (0x1BA8, 16)]:
            self.s[a] |= b                           # game-start system seeds (CTViewer main.rs)
        for a in (0x0197, 0x0199, 0x019B):           # C0/0C1E: the party-slot table starts empty (0x80),
            self.s[a] = 0x80                         #   src/ct_vm.c memBoot
        for k, v in enumerate(_PARTY + (0x80,) * 6): # $7E2980-88, the party + reserve the static resolve assumes
            self.s[0x2980 + k] = v                   #   (memBoot); op 0x48 copies it (scene 432's `48 80 29 7E 08`)
    def r8(self, a):
        if 0x7E0000 <= a < 0x7F0000: return self.s[a - 0x7E0000]
        if 0x7F0000 <= a < 0x7F0200: return self.g[a - 0x7F0000]
        if 0x7F0200 <= a < 0x7F0400: return self.l[a - 0x7F0200]
        return 0
    def w8(self, a, v):
        v &= 0xFF
        if 0x7E0000 <= a < 0x7F0000: self.s[a - 0x7E0000] = v
        elif 0x7F0000 <= a < 0x7F0200: self.g[a - 0x7F0000] = v
        elif 0x7F0200 <= a < 0x7F0400: self.l[a - 0x7F0200] = v
    def r16(self, a): return self.r8(a) | (self.r8(a + 1) << 8)
    def w16(self, a, v): self.w8(a, v & 0xFF); self.w8(a + 1, (v >> 8) & 0xFF)

LOC = 0x7F0200
GLB = 0x7F0000

# The placements' WRAM words, as src/ct_vm.c writes them in its init pass: op 0x21/0x22 read an object's tile
# bytes back ($1801/$1881), op 0x8C keeps $1800, so the packer has to hold them too or the two copies disagree.
_PARTY = (0x00, 0x80, 0x80)                          # $7E2980-82 in the static resolve (src/ct_vm.c memBoot)

def _pos_words(mem, obj, wx, wy):
    X = obj * 2
    mem.s[0x1800 + X] = wx & 0xFF; mem.s[0x1801 + X] = (wx >> 8) & 0xFF
    mem.s[0x1880 + X] = wy & 0xFF; mem.s[0x1881 + X] = (wy >> 8) & 0xFF

def _pc_lead_arm(mem, obj):
    """C0/42B3 `STX $97` (16-bit) + the lead's arrival arm C0/42B8-430F: the tile from $0102/$0103, the half-tile
    low bytes from $0104 bits 3/2. The pixel stays the host's (no 'pos'), as src/ct_vm.c pcLeadInitArm leaves it."""
    X = obj * 2
    mem.s[0x0197] = X & 0xFF; mem.s[0x0198] = 0
    b4 = mem.s[0x0104]
    mem.s[0x1801 + X] = mem.s[0x0102]; mem.s[0x1881 + X] = mem.s[0x0103]
    mem.s[0x1880 + X] = 0x7F if b4 & 0x08 else 0xFF
    mem.s[0x1800 + X] = 0x00 if b4 & 0x04 else 0x80

def _cmp(l, r, c):
    c &= 7
    return [l == r, l != r, l > r, l < r, l >= r, l <= r, (l & r) > 0, (l | r) > 0][c]

def apply_anim_op(a, op, body, addr):
    """Set an actor's animation state from an animation opcode (CTViewer exec_animation*).
    modes: 'none' (idle anim 0/walk), 'loop' (anim cycles forever), 'loopcount' (N times),
    'static' (one frozen frame). Loop count is stored as loops+1 (drains by 2 per wrap)."""
    if op == 0xAA:                                  # animeset N: loop anim N
        a['anim_index'] = body[addr + 1]; a['anim_mode'] = 'loop'
    elif op == 0xB3:                                # animeset0
        a['anim_index'] = 0; a['anim_mode'] = 'loop'
    elif op == 0xB4:                                # animeset1
        a['anim_index'] = 1; a['anim_mode'] = 'loop'
    elif op == 0xAB:                                # anime N: play N once
        a['anim_il'] = body[addr + 1]; a['anim_mode'] = 'loopcount'; a['anim_loops'] = 2
    elif op == 0xB7:                                # animerepeat N L
        a['anim_il'] = body[addr + 1]; a['anim_mode'] = 'loopcount'; a['anim_loops'] = body[addr + 2] + 1
    elif op == 0xAC:                                # shape F: freeze frame F
        a['anim_static'] = body[addr + 1]; a['anim_mode'] = 'static'
    elif op == 0xAE:                                # autoanime: back to idle
        a['anim_index'] = 0; a['anim_mode'] = 'none'; a['anim_loops'] = 0
    a['anim_frame'] = 0; a['anim_delay'] = 0

# C0:FF20 / C0:FF28 -- the bit masks ops 0x63/0x64 (and 0x65/0x66) index (read out of the ROM)
_MASKS = bytes.fromhex('0102040810204080fefdfbf7efdfbf7f')
_ARITH = (0x5B, 0x5E, 0x5F, 0x60, 0x61, 0x63, 0x64, 0x67, 0x69, 0x6B, 0x6F)

def _flat24(bank, off):
    """ctFlatAddr24: the 24-bit pointer of ops 0x48/0x49 as a Mem address (None = not WRAM)."""
    if bank == 0x7E: return 0x7E0000 + off
    if bank == 0x7F: return 0x7F0000 + off
    if (bank <= 0x3F or 0x80 <= bank <= 0xBF) and off < 0x2000: return 0x7E0000 + off
    return None

def _local_arith(mem, op, body, addr):
    """move.c CtVmLocalArithFamily: 8-bit except 0x5E/0x60 (16-bit); 0x60 is four bytes (C0/6AC9)."""
    o1 = body[addr + 1]
    if op == 0x60: src, di = o1 | (body[addr + 2] << 8), body[addr + 3]
    elif op in (0x5E, 0x61): src, di = mem.r16(LOC + o1 * 2), body[addr + 2]
    elif op in (0x63, 0x64):                         # $FF20 / $FF28 indexed by the whole byte (no AND #0F)
        k = (8 if op == 0x64 else 0) + o1
        src, di = (_MASKS[k] if k < 16 else 0), body[addr + 2]
    else: src, di = o1, body[addr + 2]
    a = LOC + di * 2
    wide = op in (0x5E, 0x60)
    cur = mem.r16(a) if wide else mem.r8(a)
    if op in (0x5B, 0x5E): res = cur + src
    elif op in (0x5F, 0x60, 0x61): res = cur - src
    elif op in (0x63, 0x69): res = cur | src
    elif op in (0x64, 0x67): res = cur & src
    elif op == 0x6B: res = cur ^ src
    else:                                            # 0x6F: C0/6C5B `LSR A / DEC $D9 / BNE` -- a do-while
        res, n = cur, src
        while True:
            res = (res & 0xFF) >> 1; n = (n - 1) & 0xFF
            if n == 0: break
    if wide: mem.w16(a, res & 0xFFFF)
    else: mem.w8(a, res)

def run_func(body, objs, obj, start, mem, actors, cap=None, follow=False, depth=0,
             visited=None, limit=6000):
    """Execute object `obj`'s function at `start`, following jumps/conditions/memory
    and updating actor states until Return / a forever-yield. If `cap` is given,
    record textbox ops (0xB8 string-table address, 0xBB/0xC* shown indices). If
    `follow`, recursively run req/call targets (cross-object calls) so an interaction
    actually executes the called functions."""
    a = actors[obj]
    addr = start
    ops = 0
    stop_op = None
    if visited is None:
        visited = set()
    while 0 <= addr < len(body) and ops < limit:
      try:
        op = body[addr]
        ln = op_len(body, addr)
        nxt = addr + ln
        if op == 0x00 or op in (0xAF, 0xB0, 0xB1, 0xB2):     # Return / yield-forever / hand to player
            stop_op = op; break
        # --- control flow (jump when condition false) ---
        elif op == 0x10: nxt = addr + body[addr + 1] + 1
        elif op == 0x11: nxt = addr - body[addr + 1] + 1
        elif op == 0x12:
            if not _cmp(mem.r8(LOC + body[addr + 1] * 2), body[addr + 2], body[addr + 3]):
                nxt = addr + body[addr + 4] + 4
        elif op == 0x13:
            if not _cmp(mem.r16(LOC + body[addr + 1] * 2), body[addr + 2] | (body[addr + 3] << 8), body[addr + 4]):
                nxt = addr + body[addr + 5] + 5
        elif op == 0x14:
            if not _cmp(mem.r8(LOC + body[addr + 1] * 2), mem.r8(LOC + body[addr + 2] * 2), body[addr + 3]):
                nxt = addr + body[addr + 4] + 4
        elif op == 0x15:
            if not _cmp(mem.r16(LOC + body[addr + 1] * 2), mem.r16(LOC + body[addr + 2] * 2), body[addr + 3]):
                nxt = addr + body[addr + 4] + 4
        elif op == 0x16:
            li, rhs, cb = body[addr + 1], body[addr + 2], body[addr + 3]
            if cb & 0x80: li += 0x100
            if not _cmp(mem.r8(GLB + li), rhs, cb & 0x7F):
                nxt = addr + body[addr + 4] + 4
        elif op == 0x18:                                     # sif: storyline < imm
            if not (mem.r8(GLB) < body[addr + 1]):
                nxt = addr + body[addr + 2] + 2
        elif op == 0x1A:                                     # case: actor result == imm
            if not (a.get('result', 0) == body[addr + 1]):
                nxt = addr + body[addr + 2] + 2
        elif op == 0xD2:                                     # party-membership conditional (ROM C0/38E8 ->
            pc = body[addr + 1]                              # C2/8C36 mode 0): scan the 3 active-party char-ids
            if not any(mem.r8(0x7E2980 + k) == pc for k in range(3)):  # at $7E2980/81/82; absent -> take the
                nxt = addr + 2 + body[addr + 2]              # jump, present falls through (addr+3). E.g. Marle's
                                                             # fair meeting (obj3 @439): without it her init takes
                                                             # the in-party follower branch and skips placement.
        # fn0's has-character / has-item tests, as the engine's init pass runs them through move.c
        # (disasm/notes/live_vm_op_gaps.md). Both jump from their displacement byte (spill + disp).
        elif op == 0xCF:                                     # C0/388D -> C2/8C36 leaf 3 (C2/8CCF): the char id among
            if not any(mem.r8(0x7E2980 + k) == body[addr + 1] for k in range(9)):   # $7E2980..2988; absent jumps
                nxt = addr + 2 + body[addr + 2]
        elif op == 0xC9:                                     # C0/3780 -> C1/CFE9 HasItem over $7E2400..24FF (an id
            it = body[addr + 1]                              #   of 0 returns 0 before the scan); absent jumps
            if it == 0 or not any(mem.s[0x2400 + k] == it for k in range(0x100)):
                nxt = addr + 2 + body[addr + 2]
        # --- data / math / bit (operate on local memory) ---
        elif op == 0x4F: mem.w8(LOC + body[addr + 2] * 2, body[addr + 1])
        elif op == 0x50: mem.w16(LOC + body[addr + 3] * 2, body[addr + 1] | (body[addr + 2] << 8))
        elif op == 0x75: mem.w8(LOC + body[addr + 1] * 2, 1)
        elif op == 0x77: mem.w8(LOC + body[addr + 1] * 2, 0)
        elif op == 0x55: mem.w8(LOC + body[addr + 1] * 2, mem.r8(GLB))   # C0/698B: loc[n] := storyline (STA $7F0200,X), not the reverse
        # the rest of the variable grid (C0/68C6..6A11) -- the engine's init pass runs every member through move.c
        # CtVmVarFamily, and this resolver mirrors that pass (ct.exe --vm compares the two)
        elif op == 0x51: mem.w8(LOC + body[addr + 2] * 2, mem.r8(LOC + body[addr + 1] * 2))
        elif op == 0x52: mem.w16(LOC + body[addr + 2] * 2, mem.r16(LOC + body[addr + 1] * 2))
        elif op == 0x54: mem.w16(LOC + body[addr + 3] * 2, mem.r16(GLB + (body[addr + 1] | (body[addr + 2] << 8))))
        elif op == 0x59: mem.w16(GLB + (body[addr + 2] | (body[addr + 3] << 8)), mem.r16(LOC + body[addr + 1] * 2))
        elif op == 0x53: mem.w8(LOC + body[addr + 3] * 2, mem.r8(GLB + (body[addr + 1] | (body[addr + 2] << 8))))
        elif op == 0x56: mem.w8(GLB + (body[addr + 2] | (body[addr + 3] << 8)), body[addr + 1])
        elif op == 0x58: mem.w8(GLB + (body[addr + 2] | (body[addr + 3] << 8)), mem.r8(LOC + body[addr + 1] * 2))
        elif op == 0x5A: mem.w8(GLB, body[addr + 1])
        # the pointer read 0x48/0x49 (C0/6792 / 67C7) and the local arithmetic (C0/6A1F..6C68) -- mirrors of move.c
        # CtVmReadPtrFamily / CtVmLocalArithFamily, which the engine's init pass runs
        elif op in (0x48, 0x49):
            bank, off = body[addr + 3], body[addr + 1] | (body[addr + 2] << 8)
            d = LOC + body[addr + 4] * 2
            fa = _flat24(bank, off)
            if fa is not None:
                mem.w8(d, mem.r8(fa))
                if op == 0x49:
                    fb = _flat24(bank, (off + 1) & 0xFFFF)
                    mem.w8(d + 1, mem.r8(fb) if fb is not None else 0)
        elif op in _ARITH: _local_arith(mem, op, body, addr)
        elif op == 0x71: i = LOC + body[addr + 1] * 2; mem.w8(i, mem.r8(i) + 1)
        elif op == 0x73: i = LOC + body[addr + 1] * 2; mem.w8(i, mem.r8(i) - 1)
        elif op == 0x65:                                     # gbiton (global)
            li = body[addr + 2]; bit = body[addr + 1]
            if bit & 0x80: li += 0x100
            mem.w8(GLB + li, mem.r8(GLB + li) | (1 << (bit & 7)))
        elif op == 0x66:
            li = body[addr + 2]; bit = body[addr + 1]
            if bit & 0x80: li += 0x100
            mem.w8(GLB + li, mem.r8(GLB + li) & ~(1 << (bit & 7)))
        # --- actor: load sprite (defines class -> visible) ---
        elif op in _PC or op == 0x80:
            cid = _PC[op] if op in _PC else body[addr + 1]
            a['sprite'] = cid; a['defined'] = True
            if cid == _PARTY[0]:                             # in-party slot 0 (C0/426A); 1/2 are 0x80 here
                a['draw'] = 'draw'; a['dead'] = False        # only an in-party arm writes $1A81 (42A6) and a live
                _pc_lead_arm(mem, obj)                       # $1100; C0/428C stores the kill's own 80 (ct_vm.c)
        # 0x81/0x82/0x83, the NPC/monster loaders (C0/4476, C0/4559+4590, C0/46DF), do not write the
        # visibility byte $1A81, so a `91 HIDE / 8B SETCOORD / 82 LOADSPRITE` init stays hidden (e.g. scene
        # 113's obj11, the Truce Canyon gate). They do rewrite $1100, which op 0x0A's kill (C0/6254) also
        # sets, so a load after a kill puts the object back in the scheduler's queue, still hidden.
        # Mirrors src/ct_vm.c's loader arms; disasm/notes/gate_scene113_hidden_portal.md sec. 2.
        elif op == 0x81: a['sprite'] = body[addr + 1]; a['defined'] = True; a['dead'] = False
        elif op == 0x82: a['sprite'] = body[addr + 1] + 7; a['defined'] = True; a['dead'] = False
        elif op == 0x83: a['sprite'] = body[addr + 1] + 256; a['defined'] = True; a['dead'] = False
        # --- actor: coords / facing ---
        elif op == 0x8B:                                     # C0/4B74: X = tile centre, Y = tile foot
            a['pos'] = (body[addr + 1] * 16 + 8, body[addr + 2] * 16 + 16)
            _pos_words(mem, obj, body[addr + 1] * 256 + 0x80, body[addr + 2] * 256 + 0xFF)
        elif op == 0x8C:                                     # C0/4BC3: the tiles are the low bytes of two scene
            X = obj * 2                                      #   vars; $1801/$1881 := them, $1880 := FF and $1800
            tx, ty = mem.r8(LOC + body[addr + 1] * 2), mem.r8(LOC + body[addr + 2] * 2)   # kept, no zero guard
            mem.s[0x1801 + X] = tx; mem.s[0x1881 + X] = ty; mem.s[0x1880 + X] = 0xFF
            a['pos'] = (((tx << 8) | mem.s[0x1800 + X]) >> 4, (((ty << 8) | 0xFF) + 1) >> 4)   # src/ct_vm.c vmPos8C
        elif op == 0x8D:                                     # C0/4C27: the raw words
            wx, wy = body[addr + 1] | (body[addr + 2] << 8), body[addr + 3] | (body[addr + 4] << 8)
            a['pos'] = (wx >> 4, wy >> 4)
            _pos_words(mem, obj, wx, wy)
        elif op in (0x21, 0x22):                             # C0/65AA / C0/65EB: an object's tile pair -> two vars
            X = body[addr + 1] if op == 0x21 else mem.s[0x0197 + body[addr + 1]]   # 0x22: by party slot, $0197+op1
            tx, ty = mem.s[(0x1801 + X) & 0xFFFF], mem.s[(0x1881 + X) & 0xFFFF]
            mem.w8(LOC + body[addr + 2] * 2, tx); mem.w8(LOC + body[addr + 3] * 2, ty)
        elif op == 0x0F: a['facing'] = 0
        elif op == 0x17: a['facing'] = 1
        elif op == 0x1B: a['facing'] = 2
        elif op == 0x1D: a['facing'] = 3
        elif op == 0xA6: a['facing'] = body[addr + 1]
        # --- actor: draw state / visibility (may target another actor) ---
        elif op in (0xAA, 0xAB, 0xAC, 0xAE, 0xB3, 0xB4, 0xB7): apply_anim_op(a, op, body, addr)
        elif op == 0x90: a['draw'] = 'draw'
        elif op == 0x91: a['draw'] = 'hidden'
        elif op == 0x7E: a['draw'] = 'removed'
        elif op == 0x7C:
            t = body[addr + 1] // 2
            if t < len(actors): actors[t]['draw'] = 'draw'
        elif op == 0x7D:
            t = body[addr + 1] // 2
            if t < len(actors): actors[t]['draw'] = 'hidden'
        elif op == 0x0A:
            t = body[addr + 1] // 2
            if t < len(actors): actors[t]['dead'] = True; actors[t]['draw'] = 'hidden'
        # --- cross-object calls (req): run the target's function inline ---
        elif follow and op in (0x02, 0x03, 0x04):
            t = body[addr + 1] // 2
            fn = body[addr + 2] & 0x0F
            if depth < 8 and t < len(objs) and (t, fn) not in visited:
                visited.add((t, fn))
                run_func(body, objs, t, objs[t][fn], mem, actors, cap, follow, depth + 1, visited, limit)
        # --- textboxes (capture the string table + shown indices) ---
        elif cap is not None and op == 0xB8:
            cap['table'] = (body[addr + 1] | (body[addr + 2] << 8) | (body[addr + 3] << 16)) - 0xC00000
        elif cap is not None and op in (0xBB, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4):
            cap['dialog'].append(body[addr + 1])
      except IndexError:                                  # ran past end of body
        break
      addr = nxt
      ops += 1
    return addr, stop_op                                 # where execution stopped + the stop op

def extract_actors(rom, scene):
    """Interpret every object's init and return their resolved actor states."""
    h = rom[0x360000 + scene * 14: 0x360000 + scene * 14 + 14]
    script_index = h[8] | (h[9] << 8)
    try:
        objs, body = load_script(rom, script_index)
    except Exception:                                    # unused/invalid script entry
        objs, body = [], b''
    mem = Mem()
    actors = [{'obj': n, 'sprite': None, 'pos': None, 'facing': 1,
               'draw': 'draw', 'dead': False, 'defined': False} for n in range(len(objs))]
    for n in range(len(objs)):
        run_func(body, objs, n, objs[n][0], mem, actors)
    for a in actors:
        # placed = a sprite was loaded, draw mode is on, not dead, and it got a position.
        # Objects that load a sprite but never get coords (the party-member slots, and NPCs
        # positioned later by a runtime event) are not on the field.
        a['visible'] = a['defined'] and a['draw'] == 'draw' and not a['dead'] and a['pos'] is not None
    return actors, script_index

def _new_actors(objs):
    return [{'obj': n, 'sprite': None, 'pos': None, 'facing': 1,
             'draw': 'draw', 'dead': False, 'defined': False} for n in range(len(objs))]

def simulate_interaction(rom, scene, obj, slot=1):
    """Run object `obj`'s function (slot 1 = activate, 2 = touch), following calls,
    and return the dialogue strings it shows. Mirrors talking to / touching an NPC."""
    import text
    h = rom[0x360000 + scene * 14: 0x360000 + scene * 14 + 14]
    try:
        objs, body = load_script(rom, h[8] | (h[9] << 8))
    except Exception:
        objs, body = [], b''
    if obj >= len(objs):
        return None, []
    mem = Mem()
    actors = _new_actors(objs)
    cap = {'table': None, 'dialog': []}
    for n in range(len(objs)):                       # object init (slot 0)
        run_func(body, objs, n, objs[n][0], mem, actors, cap=cap)
    if objs:                                         # scene init (object 0 slot 1) sets the string table
        run_func(body, objs, 0, objs[0][1], mem, actors, cap=cap, follow=True)
    cap['dialog'] = []                               # collect only what the interaction shows
    run_func(body, objs, obj, objs[obj][slot], mem, actors, cap=cap, follow=True)
    subs = text.load_dict(rom)
    lines = []
    if cap['table'] is not None:
        page = cap['table'] & 0xFF0000
        for idx in cap['dialog']:
            p = cap['table'] + idx * 2
            if p + 1 < len(rom):
                ptr = rom[p] | (rom[p + 1] << 8)
                lines.append((idx, text.decode_string(rom, page + ptr, subs)))
    return cap['table'], lines

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print(__doc__); sys.exit(1)
    rom = _romlib.load(sys.argv[1])
    mode, scene = sys.argv[2], int(sys.argv[3])
    if mode == 'scene':
        actors, si = extract_actors(rom, scene)
        vis = sum(1 for a in actors if a['visible'])
        print("scene %d -> script %d : %d objects, %d visible" % (scene, si, len(actors), vis))
        for a in actors:
            print("  obj %2d: sprite=%s pos=%s facing=%d draw=%s%s%s"
                  % (a['obj'], a['sprite'], a['pos'], a['facing'], a['draw'],
                     ' DEAD' if a['dead'] else '', ' [VIS]' if a['visible'] else ''))
    elif mode == 'disasm':
        h = rom[0x360000 + scene * 14: 0x360000 + scene * 14 + 14]
        si = h[8] | (h[9] << 8)
        objs, body = load_script(rom, si)
        obj = int(sys.argv[4]) if len(sys.argv) > 4 else 0
        slot = int(sys.argv[5]) if len(sys.argv) > 5 else 0
        print("scene %d script %d obj %d slot %d @0x%X:" % (scene, si, obj, slot, objs[obj][slot]))
        for o, op, ln in iter_ops(body, objs[obj][slot]):
            print("  0x%04X: %02X %s" % (o, op, ' '.join('%02X' % body[o + i] for i in range(1, ln) if o + i < len(body))))
    elif mode == 'talk':
        # simulate talking to (slot 1) / touching (slot 2) an object
        obj = int(sys.argv[4]) if len(sys.argv) > 4 else 0
        slot = int(sys.argv[5]) if len(sys.argv) > 5 else 1
        table, lines = simulate_interaction(rom, scene, obj, slot)
        kind = {1: 'activate', 2: 'touch'}.get(slot, 'slot %d' % slot)
        print("scene %d obj %d %s -> table @0x%X, %d textbox(es):"
              % (scene, obj, kind, table or 0, len(lines)))
        for idx, s in lines:
            print("  [str %d] %s" % (idx, s.replace('\n', ' / ')))
    else:
        print("unknown mode:", mode); sys.exit(1)
