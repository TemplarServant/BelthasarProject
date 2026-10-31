#!/usr/bin/env python
"""Regenerate the two Save-screen test fixtures under saves/ (which is gitignored).

  saves/savepoint_flags.bin   story8.bin with $7F01CE bit15 set -- the ROM's own save-point flag.
                              C2/9875 builds $0D34 at menu-open (`LDA $0100 / CMP #01F0 / BCS` for the
                              overworld, else `LDA $7F01CE / BMI` for a save point), and C2/CEDC's
                              `AND #40 / BEQ` is what lets the Save screen open at all. Without this
                              flag every field map refuses the screen, so the save checks cannot run.

  saves/ct_real_save.srm      the cart's 8 KB SRAM lifted out of saves/com_jogo_salvo.state -- a game
                              the ROM itself saved. It is the reference the checksum is graded
                              against (`Belthasar --sramcheck`): slot 0 carries signature $E41B and the
                              checksum word 0x6B8C that the ROM wrote, and the port of FF/FBE7 has to
                              reproduce that number (a file the port wrote would agree with any algorithm).

Both inputs (story8.bin, saves/com_jogo_salvo.state) are local; a checkout without them cannot run
the two checks that need the fixtures.

Usage:  python tools/gen_save_fixtures.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

CART_SRAM_SIZE = 0x2000


def gen_savepoint_flags(src="story8.bin", dst="saves/savepoint_flags.bin"):
    if not os.path.exists(src):
        print("SKIP %s: %s not present" % (dst, src))
        return False
    d = bytearray(open(src, "rb").read())
    if len(d) != 0x200:
        print("SKIP %s: %s is %d bytes, expected 0x200" % (dst, src, len(d)))
        return False
    d[0x1CF] |= 0x80                      # $7F01CE bit15 -> byte $01CF bit7
    open(dst, "wb").write(bytes(d))
    print("wrote %s ($01CF = %02X, save-point bit set)" % (dst, d[0x1CF]))
    return True


def gen_real_save(src="saves/com_jogo_salvo.state", dst="saves/ct_real_save.srm"):
    if not os.path.exists(src):
        print("SKIP %s: %s not present" % (dst, src))
        return False
    from ctlib import lake
    payload, _fmt = lake.read_payload(src)
    # cart_handleState is the last block a LakeSnes payload carries, so the cart's SRAM is its tail
    # (the same splice tools/s9x_to_lake.py performs in the other direction).
    sram = payload[len(payload) - CART_SRAM_SIZE:]
    sig = sram[0x1FF8] | (sram[0x1FF9] << 8)
    if sig != 0xE41B:
        print("SKIP %s: %s carries no saved game (slot-0 signature %04X, expected E41B)" % (dst, src, sig))
        return False
    open(dst, "wb").write(sram)
    print("wrote %s (%d B; slot0 sig=%04X stored checksum=%04X)"
          % (dst, len(sram), sig, sram[0x1FF0] | (sram[0x1FF1] << 8)))
    return True


if __name__ == "__main__":
    if not os.path.isdir("saves"):
        os.makedirs("saves")
    ok = gen_savepoint_flags()
    ok = gen_real_save() and ok
    sys.exit(0 if ok else 1)
