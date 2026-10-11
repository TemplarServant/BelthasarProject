#!/usr/bin/env python3
"""
Definitive snes9x SNAPSHOT_VERSION-11 PPU-block parser for Chrono Trigger
PPU register extraction (Telepod portal-open frame).

Synthesized from four independent investigations:
  - SnapPPU[] descriptor order (Investigator A)
  - struct SPPU / SOBJ field widths from ppu.h (Investigator B)
  - byte-order = BIG-ENDIAN (Investigator D, confirmed from FreezeStruct source)
  - OAMData@2003 / CGDATA@64 anchors (Investigator C)

The 2652-byte PPU block (snes9x SnapPPU[]) does NOT contain the screen-enable
or colour-math control registers (TM/TS/CGWSEL/CGADSUB/W12SEL/WHx/WBGLOG...).
Those are the raw last-written $21xx shadow in the FIL block (Memory.FillRAM,
32768 bytes, indexed FillRAM[address]). parse_ppu() reads the PPU block;
parse_fil() reads the FIL block; parse_state_file() does both from a .state.
"""

import struct, gzip, io

# ---------------------------------------------------------------------------
# SnapPPU[] descriptor, in serialized (array) order.
# Each entry: (name, elem_size_bytes, count). Stream width = elem_size*count.
# Multi-byte ints are BIG-ENDIAN. uint8 arrays are raw.
# Version 11 includes CGSavedByte (after CGADD) and VRAMReadBuffer (last).
# RangeTimeOver is in struct SPPU but NOT in SnapPPU[] -> omitted.
# ---------------------------------------------------------------------------
def _build_descriptor():
    d = []
    a = d.append
    # struct VMA (PPU.VMA)
    a(("VMA.High", 1, 1)); a(("VMA.Increment", 1, 1))
    a(("VMA.Address", 2, 1)); a(("VMA.Mask1", 2, 1))
    a(("VMA.FullGraphicCount", 2, 1)); a(("VMA.Shift", 2, 1))
    a(("WRAM", 4, 1))
    # BG[0..3] : SCBase(u16) HOffset(u16) VOffset(u16) BGSize(u8) NameBase(u16) SCSize(u16)
    for i in range(4):
        a((f"BG[{i}].SCBase", 2, 1)); a((f"BG[{i}].HOffset", 2, 1))
        a((f"BG[{i}].VOffset", 2, 1)); a((f"BG[{i}].BGSize", 1, 1))
        a((f"BG[{i}].NameBase", 2, 1)); a((f"BG[{i}].SCSize", 2, 1))
    a(("BGMode", 1, 1)); a(("BG3Priority", 1, 1))
    a(("CGFLIP", 1, 1)); a(("CGFLIPRead", 1, 1)); a(("CGADD", 1, 1))
    a(("CGSavedByte", 1, 1))                       # v11
    a(("CGDATA", 2, 256))                          # 512 bytes, BE words
    # OBJ[0..127] : HPos(int16) VPos(u16) HFlip(u8) VFlip(u8) Name(u16) Priority(u8) Palette(u8) Size(u8)
    for i in range(128):
        a((f"OBJ[{i}].HPos", 2, 1)); a((f"OBJ[{i}].VPos", 2, 1))
        a((f"OBJ[{i}].HFlip", 1, 1)); a((f"OBJ[{i}].VFlip", 1, 1))
        a((f"OBJ[{i}].Name", 2, 1)); a((f"OBJ[{i}].Priority", 1, 1))
        a((f"OBJ[{i}].Palette", 1, 1)); a((f"OBJ[{i}].Size", 1, 1))
    a(("OBJThroughMain", 1, 1)); a(("OBJThroughSub", 1, 1)); a(("OBJThroughAddition", 1, 1))
    a(("OBJNameBase", 2, 1)); a(("OBJNameSelect", 2, 1)); a(("OBJSizeSelect", 1, 1))
    a(("OAMAddr", 2, 1)); a(("SavedOAMAddr", 2, 1))
    a(("OAMPriorityRotation", 1, 1)); a(("OAMFlip", 1, 1)); a(("OAMReadFlip", 1, 1))
    a(("OAMTileAddress", 2, 1)); a(("OAMWriteRegister", 2, 1))
    a(("OAMData", 1, 544))                          # 512 + 32 raw bytes
    a(("FirstSprite", 1, 1)); a(("LastSprite", 1, 1))
    a(("HTimerEnabled", 1, 1)); a(("VTimerEnabled", 1, 1))
    a(("HTimerPosition", 2, 1)); a(("VTimerPosition", 2, 1))
    a(("IRQHBeamPos", 2, 1)); a(("IRQVBeamPos", 2, 1))
    a(("HBeamFlip", 1, 1)); a(("VBeamFlip", 1, 1))
    a(("HBeamPosLatched", 2, 1)); a(("VBeamPosLatched", 2, 1))
    a(("GunHLatch", 2, 1)); a(("GunVLatch", 2, 1))
    a(("HVBeamCounterLatched", 1, 1))
    a(("Mode7HFlip", 1, 1)); a(("Mode7VFlip", 1, 1)); a(("Mode7Repeat", 1, 1))
    a(("MatrixA", 2, 1)); a(("MatrixB", 2, 1)); a(("MatrixC", 2, 1)); a(("MatrixD", 2, 1))
    a(("CentreX", 2, 1)); a(("CentreY", 2, 1))
    a(("M7HOFS", 2, 1)); a(("M7VOFS", 2, 1))
    a(("Mosaic", 1, 1)); a(("MosaicStart", 1, 1))
    a(("BGMosaic", 1, 4))
    a(("Window1Left", 1, 1)); a(("Window1Right", 1, 1))
    a(("Window2Left", 1, 1)); a(("Window2Right", 1, 1))
    a(("RecomputeClipWindows", 1, 1))
    a(("ClipCounts", 1, 6))
    a(("ClipWindowOverlapLogic", 1, 6))
    a(("ClipWindow1Enable", 1, 6))
    a(("ClipWindow2Enable", 1, 6))
    a(("ClipWindow1Inside", 1, 6))
    a(("ClipWindow2Inside", 1, 6))
    a(("ForcedBlanking", 1, 1))
    a(("FixedColourRed", 1, 1)); a(("FixedColourGreen", 1, 1)); a(("FixedColourBlue", 1, 1))
    a(("Brightness", 1, 1))
    a(("ScreenHeight", 2, 1))
    a(("Need16x8Mulitply", 1, 1)); a(("BGnxOFSbyte", 1, 1)); a(("M7byte", 1, 1))
    a(("HDMA", 1, 1)); a(("HDMAEnded", 1, 1))
    a(("OpenBus1", 1, 1)); a(("OpenBus2", 1, 1))
    a(("VRAMReadBuffer", 2, 1))                     # v11, last field
    return d

DESCRIPTOR = _build_descriptor()


def descriptor_total_width():
    return sum(sz * cnt for _, sz, cnt in DESCRIPTOR)


def build_offset_table():
    """name -> (offset, elem_size, count)."""
    table = {}
    off = 0
    for name, sz, cnt in DESCRIPTOR:
        table[name] = (off, sz, cnt)
        off += sz * cnt
    return table, off


def _be(b):
    v = 0
    for x in b:
        v = (v << 8) | x
    return v


def parse_ppu(ppu_bytes):
    """Parse the 2652-byte snes9x v11 PPU block. Returns a dict."""
    tot = descriptor_total_width()
    if tot != len(ppu_bytes):
        raise ValueError(f"descriptor width {tot} != PPU block {len(ppu_bytes)}")
    tbl, _ = build_offset_table()

    def u(name):
        off, sz, cnt = tbl[name]
        assert cnt == 1
        return _be(ppu_bytes[off:off + sz])

    def arr_u8(name):
        off, sz, cnt = tbl[name]
        return list(ppu_bytes[off:off + sz * cnt])

    def arr_u16(name):
        off, sz, cnt = tbl[name]
        return [_be(ppu_bytes[off + 2 * k:off + 2 * k + 2]) for k in range(cnt)]

    out = {}
    out["bgmode"] = u("BGMode")
    out["bg3priority"] = u("BG3Priority")

    bgs = []
    for i in range(4):
        bgs.append({
            "scBase": u(f"BG[{i}].SCBase"),
            "hofs": u(f"BG[{i}].HOffset"),
            "vofs": u(f"BG[{i}].VOffset"),
            "bgSize": u(f"BG[{i}].BGSize"),
            "nameBase": u(f"BG[{i}].NameBase"),
            "scSize": u(f"BG[{i}].SCSize"),
        })
    out["bg"] = bgs

    out["cgdata"] = arr_u16("CGDATA")          # 256 BGR555 words
    out["oamData"] = bytes(arr_u8("OAMData"))  # 544 bytes

    out["window"] = {
        "win1Left":  u("Window1Left"),  "win1Right":  u("Window1Right"),
        "win2Left":  u("Window2Left"),  "win2Right":  u("Window2Right"),
    }
    out["clip"] = {
        "counts":       arr_u8("ClipCounts"),
        "overlapLogic": arr_u8("ClipWindowOverlapLogic"),
        "w1Enable":     arr_u8("ClipWindow1Enable"),
        "w2Enable":     arr_u8("ClipWindow2Enable"),
        "w1Inside":     arr_u8("ClipWindow1Inside"),
        "w2Inside":     arr_u8("ClipWindow2Inside"),
    }
    out["forcedBlanking"] = u("ForcedBlanking")
    out["brightness"] = u("Brightness")
    out["fixedColour"] = (u("FixedColourRed"), u("FixedColourGreen"), u("FixedColourBlue"))
    out["screenHeight"] = u("ScreenHeight")
    out["objNameBase"] = u("OBJNameBase")
    out["objNameSelect"] = u("OBJNameSelect")
    out["objSizeSelect"] = u("OBJSizeSelect")
    out["m7"] = {"matrixA": u("MatrixA"), "matrixD": u("MatrixD"),
                 "hofs": u("M7HOFS"), "vofs": u("M7VOFS")}
    out["vramReadBuffer"] = u("VRAMReadBuffer")
    return out


# ---------------------------------------------------------------------------
# FIL block = Memory.FillRAM (32768 B); raw last-written $21xx/$42xx shadow.
# ---------------------------------------------------------------------------
def parse_fil(fil_bytes):
    f = fil_bytes

    def r(addr):
        return f[addr]

    bg3sc = r(0x2109)
    bg34nba = r(0x210C)
    regs = {
        "inidisp": r(0x2100),
        "bgmode":  r(0x2105),
        "mosaic":  r(0x2106),
        "bg1sc":   r(0x2107), "bg2sc": r(0x2108), "bg3sc": bg3sc, "bg4sc": r(0x210A),
        "bg12nba": r(0x210B), "bg34nba": bg34nba,
        "w12sel":  r(0x2123), "w34sel": r(0x2124), "wobjsel": r(0x2125),
        "wh0": r(0x2126), "wh1": r(0x2127), "wh2": r(0x2128), "wh3": r(0x2129),
        "wbglog": r(0x212A), "wobjlog": r(0x212B),
        "tm":  r(0x212C), "ts": r(0x212D), "tmw": r(0x212E), "tsw": r(0x212F),
        "cgwsel": r(0x2130), "cgadsub": r(0x2131), "coldata": r(0x2132),
        "setini": r(0x2133),
    }
    regs["bg3_tilemap_base_word"] = (bg3sc & 0xFC) << 8          # SC base word addr
    regs["bg3_tilemap_size"] = bg3sc & 0x03                      # 0=32x32,1=64x32,2=32x64,3=64x64
    regs["bg3_char_base_word"] = (bg34nba & 0x0F) << 12          # BG3 CHR base word addr
    regs["bg4_char_base_word"] = ((bg34nba >> 4) & 0x0F) << 12
    return regs


# ---------------------------------------------------------------------------
def _split_blocks(raw):
    assert raw[:13] == b"#!s9xsnp:0011", raw[:13]
    i = raw.index(b"\n") + 1
    blocks = {}
    while i + 11 <= len(raw):
        name = raw[i:i + 3]
        if raw[i + 3:i + 4] != b":":
            break
        size = int(raw[i + 4:i + 10])
        body = raw[i + 11:i + 11 + size]
        blocks[name.decode("latin1")] = body
        i += 11 + size
    return blocks


def parse_state_file(path):
    with open(path, "rb") as fh:
        head = fh.read(2)
        fh.seek(0)
        raw = gzip.decompress(fh.read()) if head == b"\x1f\x8b" else fh.read()
    blocks = _split_blocks(raw)
    res = {"ppu": parse_ppu(blocks["PPU"])}
    if "FIL" in blocks:
        res["fil"] = parse_fil(blocks["FIL"])
    return res, blocks


if __name__ == "__main__":
    import sys, json
    total = descriptor_total_width()
    print(f"descriptor total width = {total} (expect 2652) -> {'OK' if total==2652 else 'MISMATCH'}")
    tbl, _ = build_offset_table()
    print(f"  BGMode      @ {tbl['BGMode'][0]}")
    print(f"  BG[2].SCBase@ {tbl['BG[2].SCBase'][0]}")
    print(f"  CGDATA      @ {tbl['CGDATA'][0]} (expect 64)")
    print(f"  OAMData     @ {tbl['OAMData'][0]} (expect 2003)")

    path = sys.argv[1] if len(sys.argv) > 1 else \
        r"C:/Users/Icaro Ferris/Desktop/scene_8_portal_opened.state"
    res, blocks = parse_state_file(path)
    p, fil = res["ppu"], res["fil"]

    # ---- anchor validation ----
    swirl = [0x0004,0x4084,0x3043,0x2823,0x3043,0x4084,0x4CE4,0x6164,
             0x7E24,0x7F29,0x7FF9,0x7FEE,0x7F29,0x7E24,0x6164,0x4CE4]
    got = p["cgdata"][160:176]
    print("\nCGDATA[160:176] swirl match:", "OK" if got == swirl else f"FAIL {got}")
    print("CGDATA[65] rim = %04X (expect 7FDE) -> %s" %
          (p["cgdata"][65], "OK" if p["cgdata"][65]==0x7FDE else "FAIL"))
    oam = p["oamData"]
    s56 = [(oam[4*s], oam[4*s+1], oam[4*s+2]) for s in range(56,72)]
    pals_ok = all(((oam[512 + s//4] ) is not None) for s in range(56,72))
    print("OAM[56..71] (X,Y,tile):", s56[:4], "...")

    print("\n=== PPU block (SPPU) ===")
    print("bgmode       =", p["bgmode"], " bg3priority =", p["bg3priority"])
    for i,b in enumerate(p["bg"]):
        print(f"  BG{i+1}: scBase=0x{b['scBase']:04X} nameBase=0x{b['nameBase']:04X} "
              f"hofs={b['hofs']&0x3FF} vofs={b['vofs']&0x3FF} "
              f"bgSize={b['bgSize']} scSize={b['scSize']} (raw hofs=0x{b['hofs']:04X} vofs=0x{b['vofs']:04X})")
    print("  forcedBlanking =", p["forcedBlanking"], " brightness =", p["brightness"],
          " screenHeight =", p["screenHeight"])
    print("  window L/R:", p["window"])
    print("  clip w1Enable[BG1,BG2,BG3,BG4,OBJ,COL] =", p["clip"]["w1Enable"])
    print("  clip w1Inside =", p["clip"]["w1Inside"],
          " overlapLogic =", p["clip"]["overlapLogic"])

    print("\n=== FIL block (raw $21xx shadow) ===")
    for k in ("inidisp","bgmode","bg1sc","bg2sc","bg3sc","bg4sc","bg12nba","bg34nba",
              "w12sel","wh0","wh1","wbglog","wobjlog","tm","ts","tmw","tsw",
              "cgwsel","cgadsub","coldata","setini"):
        print(f"  ${ {'inidisp':0x2100,'bgmode':0x2105,'bg1sc':0x2107,'bg2sc':0x2108,'bg3sc':0x2109,'bg4sc':0x210A,'bg12nba':0x210B,'bg34nba':0x210C,'w12sel':0x2123,'wh0':0x2126,'wh1':0x2127,'wbglog':0x212A,'wobjlog':0x212B,'tm':0x212C,'ts':0x212D,'tmw':0x212E,'tsw':0x212F,'cgwsel':0x2130,'cgadsub':0x2131,'coldata':0x2132,'setini':0x2133}[k]:04X} {k:8s} = 0x{fil[k]:02X}")
    print("  -> BG3 tilemap base word = 0x%04X (size code %d)" %
          (fil["bg3_tilemap_base_word"], fil["bg3_tilemap_size"]))
    print("  -> BG3 char    base word = 0x%04X" % fil["bg3_char_base_word"])
