import os as _ctos, sys as _ctsys
_ctsys.path.append(_ctos.path.normpath(_ctos.path.join(_ctos.path.dirname(_ctos.path.abspath(__file__)), '.')))
from ctlib import rom as _romlib  # the ROM through ctlib.rom: SHA-1 verified, a copier header stripped
#!/usr/bin/env python3
r"""PROTOTYPE of the bank-C2 VWF's CPU cost (C2/F90C -> C2/57DF / C2/5823 -> C2/58B2 -> C2/5DC4), to be ported to C.

Every instruction's cost is LakeSnes's (the trace's base value per PC: tools/cycle_costs.py); a branch is 12 not taken,
18 taken. The control flow is the ROM's, walked over the string. Validated against the oracle's beam traces with
tools/cycle_legs.py (the slot scan C2/D156 on a blank and a saved battery).

    python tools/vwf_cost_proto.py
"""
ROM = _romlib.load('Chrono_Trigger_USA_SNES.sfc')
ADV = ROM[0x260E6:0x260E6 + 0x60]           # $C2:60E6, index = code - $A0


def fo(a):
    return ((a >> 16) - 0xC0) * 0x10000 + (a & 0xFFFF)


# ---- C2/5E89: one plane-row (JSR from C2/5E36; the JSR is the caller's) -----------------------------------------
ROW_HEAD = 28 + 42 + 18 + 20 + 12            # 5E89 LDY $60, 5E8B LDA [$76],Y, 5E8D XBA, 5E8E LDA $62, 5E90 LSR A
ROW_ODD = 12 + 42 + 12 * 4 + 18              # 5E91 BCC (not taken), 5E93 LDA [$79],Y, 4 x ASL, 5E99 BRA
ROW_EVEN = 18 + 42 + 12                      # 5E91 BCC (taken), 5E9B LDA [$79],Y, 5E9D AND #F0
ROW_MID = 18 + 12 + 28 + 28 + 18 + 12 + 12 + 12 + 36   # 5E9F REP .. 5EAC JMP ($5EAF,X)
ARM0 = 18 + 28 + 36 + 44 + 36 + 18 + 44 + 40            # 5EBF: two plain stores
ARM14 = 18 + 28 + 36 + 44 + 36 + 18 + 44 + 44 + 40      # 5ED3..5EE4 after n x LSR A (12 each)
ARM57 = 18 + 28 + 36 + 44 + 36 + 18 + 44 + 36 + 20 + 44 + 44 + 40   # 5EEE..5F06 after (8-n) x (ASL 12 + ROL 50)


def row(code, pen):
    n = pen & 7
    c = ROW_HEAD + (ROW_ODD if code & 1 else ROW_EVEN) + ROW_MID
    if n == 0:
        c += ARM0
    elif n <= 4:
        c += 12 * n + ARM14
    else:
        c += 62 * (8 - n) + ARM57
    return c


# ---- C2/5E36: the 2 bpp row loop -- 4 x 2 rows, $73 += F0, 8 x 2 rows ---------------------------------------------
LOOP_PRO = 20 + 12 + 20 + 26 + 12 + 20 + 20 + 20        # 5E36 PHB .. 5E41 STZ $61
LOOP_IT = 40 + 18 + 50 + 50 + 18 + 40 + 18 + 50 + 50 + 18 + 34   # two JSRs + their INCs + DEC $68 (first loop)
LOOP_MID = 18 + 28 + 12 + 18 + 28 + 18 + 12 + 20        # 5E5D .. 5E6B
LOOP_EPI = 26 + 40                                      # 5E87 PLB, 5E88 RTS


def glyph_rows(code, pen):
    c = LOOP_PRO + LOOP_MID + LOOP_EPI
    c += 4 * LOOP_IT + 3 * 18 + 12                      # 5E5B BNE: taken 3 x, falls through once
    c += 8 * LOOP_IT + 7 * 18 + 12                      # 5E85 BNE (5E83 DEC $68 is 34 like 5E59)
    c += 24 * row(code, pen)
    return c


# ---- C2/5DC4: the glyph ------------------------------------------------------------------------------------------
GLYPH_PRO = (18 + 28 + 12 + 12 + 12 + 28 + 12 + 28 + 12 + 18 + 28 + 28 + 12 + 12 + 12 + 12 + 28 + 12 + 28 + 12 + 18
             + 28 + 28 + 18 + 28 + 28 + 12 + 18 + 28 + 18 + 20 + 20 + 12 + 20 + 20 + 20 + 12 + 12 + 12 + 20 + 20
             + 20 + 20 + 20 + 12 + 12 + 40)             # 5DC4 .. 5E14 BNE (not taken: 2 bpp), 5E16 JSR
GLYPH_EPI = 18 + 12 + 18 + 12 + 20 + 12 + 12 + 12 + 30 + 20 + 20 + 34 + 12 + 18 + 40   # 5E19 BRA .. 5E35 RTS


def glyph(code, pen):
    return GLYPH_PRO + glyph_rows(code, pen) + GLYPH_EPI


# ---- C2/58B2: the string loop, per byte ----------------------------------------------------------------------------
BYTE_HEAD = 42 + 18 + 50 + 18 + 12                      # 58B2 LDA [$31] .. 58BA CMP #A0
GLYPH_CALL = 12 + 18 + 28 + 18 + 40                     # 58BC BCC (not taken), 58BE REP, 58C0 STA $35, 58C2 SEP, 58C4 JSR
GLYPH_TAIL = 34 + 18                                    # 58C7 DEC $13, 58C9 BNE (taken)
TERM = 18 + 12 + 18 + 12 + 12 + 12 + 36 + 12 + 20 + 40  # 58BC BCC (taken) .. 5900 JMP ($5903,X), 594F TYA / STA $15 / RTS


def string_loop(codes, pen=0):
    c = 0
    for b in codes:
        if b == 0:
            return c + BYTE_HEAD + TERM, pen
        if b < 0xA0:
            raise ValueError('control/dictionary code %02X not modelled' % b)
        c += BYTE_HEAD + GLYPH_CALL + glyph(b, pen) + GLYPH_TAIL
        pen = (pen + ADV[b - 0xA0]) & 0xFF
    raise ValueError('no terminator')


# ---- C2/5823 (C2:0009), one call with $15 = 4 and $30 = 0 ----------------------------------------------------------
VEC = 18                                                # the bank vector's JMP (C2:0003 -> 57DF, C2:0009 -> 5823)
CALL9_HEAD = 20 + 18 + 28 + 18 + 12 + 18 + 18 + 40      # 5823 PHP .. 5830 JSR C2/584A
PRE_584A = 20 + 12 + 12 + 36 + 18 + 12 + 40             # 584A .. 584E JMP (5851,X) -> 5895 BRA -> 58B0 CLC / 58B1 RTS
CALL9_DISP = 12 + 20 + 12 + 12 + 52                     # 5833 BCS (not taken), 5835 LDA $30, ASL, TAX, 5839 JSR ($5842,X)
CALL9_TAIL = 12 + 18 + 34 + 26 + 42                     # 583C LDA #00, XBA, PLD, PLP, RTL

# ---- C2/57DF (C2:0003) with $14 = 80 (the pen to 0) -----------------------------------------------------------------
INIT3 = (20 + 18 + 28 + 18 + 12 + 28 + 18 + 12 + 12 + 48 + 28 + 18 + 18 + 20 + 20 + 20 + 20 + 12
         + 18 + 12 + 20 + 20 + 12 + 20 + 12 + 20 + 18 + 28 + 12 + 20 + 20 + 34 + 26 + 42)
# 57DF PHP .. 57FF CMP #02, 5801 BNE (taken) -> 5807 LDA #08 / 5809 STA $34 / 580B LDA $14 / 580D BPL (not taken)
# / 580F STZ $34 / 5811 LDA #04 / 5813 STA $15 / 5815 LDX #0200 / 5818 STX $3D / 581A LDA #00 / 581C STA $3F /
# 581E STZ $17 / 5820 PLD / 5821 PLP / 5822 RTL

# ---- C2/F90C with one C2:0009 call (no newline) ----------------------------------------------------------------------
F90C_HEAD = 20 + 18 + 12 + 26 + 34 + 34 + 12 + 26 + 54  # F90C PHP .. F91F JSL C2:0003
F90C_LINE = 12 + 26 + 54                                 # F923 LDA #20, F925 STA $0213, F928 JSL C2:0009
F90C_TAIL = 26 + 12 + 18 + 26 + 40                       # F92C LDA $0215, CMP #05, F931 BNE (taken), F941 PLP, F942 RTS


def f90c(codes):
    loop, _ = string_loop(codes)
    return (F90C_HEAD + VEC + INIT3 + F90C_LINE + VEC + CALL9_HEAD + PRE_584A + CALL9_DISP + loop + CALL9_TAIL
            + F90C_TAIL)


# ---- C2/D296: the slot's title line (the clear, then C2/F90C) -------------------------------------------------------
D296_HEAD = 20 + 18 + 12 + 26 + 18 + 18 + 34 + 28 + 18 + 12 + 18 + 34 + 12 + 40 + 12 + 12 + 12 + 18   # D296 .. D2B9
D296_MVN = 510 * 46                                     # D2BC MVN $7E,$7E, $01FD + 1 bytes
D296_TAIL = 40 + 26 + 40                                # D2BF JSR C2/F90C, D2C2 PLP, D2C3 RTS


def string_at(idx):
    tab = fo(0xFFD03E)
    p = ROM[tab + 2 * idx] | (ROM[tab + 2 * idx + 1] << 8)
    o, out = fo(0xFF0000 | p), []
    while True:
        out.append(ROM[o])
        if ROM[o] == 0:
            return out
        o += 1


def d296(idx):
    return D296_HEAD + D296_MVN + D296_TAIL + f90c(string_at(idx))


# ---- C2/D156's per-slot body: FF/F9C4, C2/D19F or C2/D296, C2/D2C4 ------------------------------------------------
SLOT_EMPTY_PICK = 20 + 12 + 12 + 26 + 40 + 18           # D171 LDA $00, D173 BEQ (not taken), D175 LDA #1A, D177 STA $020C,
                                                        #   D17A JSR C2/D296, D17D BRA
SLOT_VALID_PICK = 20 + 18 + 46 + 40                     # D171 LDA $00, D173 BEQ (taken), D17F INC $0D49,X, D182 JSR C2/D19F

D19F_HEAD = 20 + 18 + 28 + 18 + 12 + 12 + 18 + 28 + 28 + 28 + 18 + 12 + 20   # D19F PHP .. D1B6 STA $24
MEMBER_HEAD = 28 + 12 + 32 + 28 + 32 + 30               # D1B8 LDY $61 .. D1C5 STA $004202
MEMBER_FULL = (12 + 12 + 30 + 18 + 18 + 28 + 36 + 12 + 40 + 40 + 18 + 9 * 46 + 18 + 12 + 30 + 18 + 18 + 28 + 36 + 12
               + 18 + 6 * 46)                           # D1C9 BMI (not taken) .. D201 MVN (6 bytes)
MEMBER_EMPTY = 18                                       # D1C9 BMI (taken) -> D204
MEMBER_TAIL = 18 + 20 + 12 + 12 + 12 + 20 + 34 + 34     # D204 SEP .. D211 DEC $24
D19F_MID = (18 + 28 + 28 + 40 + 18                       # D215 REP .. D21F CMP #03E8
            + 40 + 12 + 12 + 40 + 18 + 40 + 12 + 12 + 28 + 12 + 18 + 12 + 18 + 10 * 46 + 28 + 40 + 40 + 40 + 18)
                                                        # D227 STA .. D252 AND #0080 (D242 MVN 10 bytes)
D19F_TAIL = 40 + 26 + 40                                # D261 JSR C2/D266, D264 PLP, D265 RTS
D266_HEAD = 20 + 18 + 28 + 28 + 32 + 12 + 32 + 32 + 18 + 12   # D266 PHP .. D27C LDX #1A
D266_TAIL = 26 + 40 + 26 + 40                           # D285 STX $020C, D288 JSR C2/D296, D28B PLP, D28C RTS


def title_index(chapter):
    thr = ROM[fo(0xFFD024):fo(0xFFD024) + 0x1A]
    x = 0x1A
    while True:
        x -= 1
        if chapter >= thr[x]:                           # D27F CMP $FFD024,X / D283 BCC ->D27E
            return x


def d19f(save, base):
    """save: the SRAM image (bank $30), base: the slot's offset ($7B)."""
    c = D19F_HEAD
    for k in range(3):
        c += MEMBER_HEAD + (MEMBER_EMPTY if save[base + 0x580 + k] & 0x80 else MEMBER_FULL) + MEMBER_TAIL
        c += 18 if k < 2 else 12                        # D213 BNE
    lv = save[base + 0x59C] | (save[base + 0x59D] << 8)
    c += D19F_MID + (18 if lv < 0x3E8 else 12 + 18)     # D222 BCC (taken), else LDA #03E7 (18)
    flag = (save[base + 0x793] | (save[base + 0x794] << 8)) & 0x80
    c += 18 if not flag else 12 + 28 + 28 + 40 + 40     # D255 BEQ (taken) / D257 LDY $61 .. D25E STA $0005,Y
    idx = title_index(save[base + 0x603])
    c += D19F_TAIL + D266_HEAD + (0x1A - idx) * (12 + 30) + (0x1A - idx - 1) * 18 + 12 + D266_TAIL + d296(idx) - 0
    return c


# ---- FF/F9C4: a slot's verdict (FF/FBD3 the base, the signature at $30:7FF8, FF/FBE7 the checksum) ----------------
F9C4_HEAD = 28 + 20 + 20 + 18 + 34 + 26 + 26 + 40 + (28 + 18 + 12 + 12 + 36 + 28 + 40) + 28 + 18 + 12 + 12 + 40 + 18
F9C4_TAIL = 28 + 26 + 26 + 34 + 42                      # F9F5 STA $00 .. F9FA RTL
FBE7 = (20 + 20 + 18 + 34 + 26 + 26 + 28 + 28 + 18 + 18 + 12 + 1280 * (50 + 12 + 12) + 1279 * 18 + 12
        + 26 + 26 + 40)                                 # 1280 words of the slot summed (Y = 09FE down by 2)


def slot_sum(sram, slot):
    """FF/FBE7: CLC once, then ADC ($00),Y for Y = 09FE down to 0 -- the carry chains through all 1280 words."""
    base, acc, carry = 0x0A00 * slot, 0, 0
    for y in range(0x9FE, -1, -2):
        acc += (sram[base + y] | (sram[base + y + 1] << 8)) + carry
        carry = 1 if acc > 0xFFFF else 0
        acc &= 0xFFFF
    return acc


def slot_verdict(sram, slot):
    """FF/F9C4's $00: 8080 = no signature, 4040 = the sum disagrees, 0000 = a save."""
    if (sram[0x1FF8 + 2 * slot] | (sram[0x1FF9 + 2 * slot] << 8)) != 0xE41B:
        return 0x8080
    return 0x0000 if slot_sum(sram, slot) == (sram[0x1FF0 + 2 * slot] | (sram[0x1FF1 + 2 * slot] << 8)) else 0x4040


def f9c4(sram, slot):
    v = slot_verdict(sram, slot)
    if v == 0x8080:
        return F9C4_HEAD + 18 + 18 + 18 + F9C4_TAIL     # F9DE BNE (taken), F9ED LDA #8080, F9F0 BRA
    c = F9C4_HEAD + 12 + 40 + FBE7 + 40                 # F9DE BNE (not taken), F9E0 JSR FF/FBE7, F9E3 CMP $7FF0,X
    return c + (12 + 18 + 18 if v == 0 else 18 + 18) + F9C4_TAIL   # F9E6 BNE, F9E8/F9EB (ok) | F9F2 (bad)


# ---- C2/D2C4: the slot's header line (string #0E + slot of $FF:D3FE) and its $C2:D329[slot] one-pixel shifts --------
D2C4_HEAD = 20 + 18 + 20 + 30 + 12 + 12 + 26 + 12 + 30 + 12 + 26 + 18 + 18 + 34 + 36 + 18 + 34 + 40   # .. D2F0 JSR
D2C4_MID = 18 + 28 + 30 + 20                            # D2F3 SEP .. D2FB STA $00
D306 = 34 + 18 + 16 * (8 * 46 + 12 + 12) + 15 * 18 + 12 + 40   # 16 columns x 8 read-modify-writes, D328 RTS
D2C4_TAIL = 26 + 40


def string_in(table, idx):
    tab = fo(0xFF0000 | table)
    p = ROM[tab + 2 * idx] | (ROM[tab + 2 * idx + 1] << 8)
    o, out = fo(0xFF0000 | p), []
    while True:
        out.append(ROM[o])
        if ROM[o] == 0:
            return out
        o += 1


def d2c4(slot):
    n = ROM[0x2D329 + slot]
    c = D2C4_HEAD + f90c(string_in(0xD3FE, 0x0E + slot)) + D2C4_MID
    c += n * (40 + D306 + 34) + (n - 1) * 18 + 12      # D2FD JSR C2/D306, D300 DEC $00, D302 BNE
    return c + D2C4_TAIL


# ---- C2/D156: the slot scan (the menu init's one battery-dependent leg) --------------------------------------------
D156_HEAD = 20 + 18 + 28 + 34 + 26 + 28 + 12 + 26       # D156 PHP .. D165 STA $0D4C
SLOT_TOP = 28 + 32 + 54                                 # D168 LDX $79, D16A STZ $0D49,X, D16D JSL FF/F9C4
SLOT_BOTTOM = 40 + 34 + 20 + 12                         # D185 JSR C2/D2C4, D188 INC $79, D18A LDA $79, D18C CMP #03
D156_TAIL = 18 + 788 + 26 + 40                          # D190 LDX #D198, D193 JSR C2/8385 (its record: 788 with the JSR),
                                                        #   D196 PLP, D197 RTS


def d156(sram):
    """C2/D156 from its first instruction to its RTS."""
    c = D156_HEAD
    for slot in range(3):
        c += SLOT_TOP + f9c4(sram, slot)
        ok = slot_verdict(sram, slot) == 0              # D171 LDA $00 / D173 BEQ
        c += (SLOT_VALID_PICK + d19f(sram, 0x0A00 * slot)) if ok else (SLOT_EMPTY_PICK + d296(0x1A))
        c += SLOT_BOTTOM + d2c4(slot) + (18 if slot < 2 else 12)   # D18E BCC
    return c + D156_TAIL


# ---- the whole NMI-off stretch of a menu open: C0/0B5D (NMI off) .. C2/8087 (NMI on) --------------------------------
# The legs are tools/menu_init_legs.py's. Fixed ones are the ROM's own cost as traced (colsave3's open); the DMA legs
# (C2/940D, C2/968D, C2/8663) jitter by a few tens of cycles with the 8-cycle DMA sync phase, and the five sound posts
# wait on the SPC's echo -- the APU's phase: +-200 with the driver idle, up to ~25,000 more with a song playing.
L_0B4E_REST = 24 + 24 + 40                              # 0B5D STA $4200, 0B60 STA, 0B63 RTS
L_0918 = 18 + 18 + 18 + 18 + 20 + 0x14E0 * 46 + 26 + 18 + 40   # C0/0918: the 0x14E0-byte MVN save
L_HEAD_POSTS = 26072                                    # 8068 JSR C2/8096 .. 80EE: the MVN head + five posts, idle driver
L_940D = 1018364                                        # 80EE REP .. 80F3: C2/940D
L_FILLS = 2072                                          # 80F3 .. 8124
L_968D = 1977700                                        # 8124 JSR C2/968D .. 8127
L_984A, L_8663, L_92F4, L_F3CA = 25242, 10464, 1418, 289874   # each with its JSR
L_9875 = 831482                                         # C2/9875 -- not walked yet (party / bag: +-1,800)
L_HP, L_806B = 2630, 236


# the caller's code between C0/011B's RTS and the JSL, per C0/0CC7 / C0/1958 arm, with the JSL
CALLER_POST = {'x': 12 + 18 + 54,                          # C0/195E LDA #01, C0/195F LDX, C0/1960 JSL C2:8000
               'c8hi': 12 + 18 + 20 + 12 + 18 + 18 + 18 + 12 + 18 + 20 + 4 * 12 + 54,   # op C8 $80+: 0D09 .. 0D16
               'c8odd': 12 + 18 + 20 + 12 + 12 + 12 + 18 + 12 + 18 + 54}               # op C8 odd: 0D00 LDA #00


def l_011b(wram, caller):
    """C0/0B4E's tail after the NMI-off write, the caller's JSR C0/011B, C0/011B (with C0/0918's save), the caller's
    code to its JSL C2:8000 (included)."""
    c = L_0B4E_REST + 40
    c += 20 + (18 if not wram[0x0129] else 12 + 12 + 20 + 12 + 20 + 20)   # 011B LDA $29 / 011D BEQ (the $29 arm)
    c += 40 + L_0918 + 28 + 32 + 20 + 32 + 20 + 32 + 20 + 18            # 0129 JSR 0918 .. 013D LDX #0000
    for k in range(3):
        present = not (wram[0x0197 + 2 * k] & 0x80)
        c += 12 + 18 + 26 + (12 + 12 + 18 + 6 * 40 + 18 if present else 18) + 12 + 12 + 18 + (18 if k < 2 else 12)
    c += 20 + 32 + 20 + 32 + 20 + 32 + 18 + 40 + 40 + 6 * 40 + 18 + 26 + 32 + 40   # 0167 .. 01A4 RTS
    return c + CALLER_POST[caller]


def l_816a(wram):
    c = 20 + 18 + 12 + 12 + 20 + 20 + 26 + 40             # 816A PHP .. 8172 STX $75, 818E PLP, 818F RTS
    for x in range(9):
        c += 26                                         # 8174 LDA $2980,X
        if wram[0x2980 + x] & 0x80:
            c += 18                                     # 8177 BMI (taken)
        else:
            c += 12 + 32 + 12 + 32 + 12 + 12 + (12 + 34 if x < 3 else 18) + 34
        c += 12 + 12 + (18 if x < 8 else 12)            # 8189 INX, 818A CPX #09, 818C BCC
    return c + 40                                       # (with 8130's JSR C2/816A)


def stretch(wram, sram, mode, caller):
    """Non-refresh cycles C0/0B5D -> C2/8087; the prologue's mode-0 arm (28042 `LDA $7F0061 / BNE`) costs 44 more."""
    return (l_011b(wram, caller) + 580 + (44 if mode == 0 else 0) + L_HEAD_POSTS + L_940D + L_FILLS + L_968D
            + 40 + d156(sram) + L_984A + L_8663 + l_816a(wram) + L_92F4 + L_F3CA + L_9875 + L_HP + L_806B)


if __name__ == '__main__':
    print('D296 #1A (No data): %d   oracle 441210 (D296 .. D2C2)' % (d296(0x1A) - 26 - 40))
    print('empty slot D171 -> D185: %d   oracle 441404' % (SLOT_EMPTY_PICK + d296(0x1A)))
    sv = open('saves/field_re/colsave3.srm', 'rb').read()
    print('valid slot D171 -> D185 (colsave3 slot 0): %d   oracle 609832' % (SLOT_VALID_PICK + d19f(sv, 0)))
    for s in range(3): print('D2C4 slot %d: %d   oracle %s' % (s, d2c4(s) + 40, (45934, 38044, 39196)[s]))
    print('F9C4 empty: %d  oracle 778 (D16D->D171 incl. JSL)' % (54 + f9c4(bytes(8192), 0)))
    print('F9C4 colsave3 slot 0: %d  oracle 118958' % (54 + f9c4(sv, 0)))
    print('D156 blank: %d  oracle 1451250' % (40 + d156(bytes(8192))))
    print('D156 colsave3: %d  oracle 1737858' % (40 + d156(sv)))
