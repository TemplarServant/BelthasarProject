#!/usr/bin/env python3
r"""The block costs src/field_cost.c is written with, computed from the beam traces the way tools/pass_cost_proto.py
walks them (ctlib/costtab.py). Run it after re-tracing to check the C file's numbers:

    python tools/pass_cost_consts.py

Every name below is a straight-line run (branches not taken) or a branch-inclusive path, in bank C0 unless noted,
and each line prints `NAME = value   (range)`; field_cost.c carries the same names with the same numbers.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pass_cost_proto as P  # noqa: E402

L, c, br, LR, mvn, long_read, one = P.L, P.c, P.br, P.LR, P.mvn, P.long_read, P.one

K = []


def k(name, v, rng=''):
    K.append((name, v, rng))


def main():
    # --- C0/B0E6 loop ---
    k('B0E6_PRO', L(0xB0E6, 0xB0F1), 'B0E6-B0F0')
    k('B0E6_OBJ', L(0xB0F1, 0xB0F6), 'B0F1-B0F5 STX/LDA')
    k('B0E6_DEAD', br(0xB0F6, 1), 'B0F6 BMI taken')
    k('B0E6_CLS', br(0xB0F6, 0) + L(0xB0F8, 0xB0FA), 'B0F6 nt, CMP #07')
    k('B0E6_CLS7', br(0xB0FA, 1), 'B0FA BEQ taken')
    k('B0E6_TEST', br(0xB0FA, 0) + c(0xB0FC), 'BEQ nt, JSR B127')
    k('B0E6_HIDDEN', br(0xB0FF, 1), 'B0FF BCC taken')
    k('B0E6_DRAW', br(0xB0FF, 0) + c(0xB101) + c(0xB104) + L(0xB107, 0xB109) + c(0xB109) + br(0xB10C, 0)
      + L(0xB10E, 0xB111) + c(0xB111) + L(0xB114, 0xB117) + c(0xB117), 'B0FF..B117 minus the callees')
    k('B0E6_TAIL', L(0xB11A, 0xB120), 'B11A-B11F')
    k('B0E6_END', L(0xB122, 0xB124) + c(0xB124), 'B122 REP, BRL B271')
    # --- C0/B127 ---
    k('B127_HEAD', L(0xB127, 0xB12A), 'LDA $1A81,X')
    k('B127_OUT', L(0xB190, 0xB192), 'CLC RTS')
    k('B127_CAM', L(0xB12E, 0xB134), 'LDA $1D0A SEC SBC')
    k('B127_C3NEG', one(0xB136, 1, 1, 0x0100), 'LDA #00')
    k('B127_CAM2', L(0xB138, 0xB145), 'STA $C3 .. LDA $1D0E')
    k('B127_C5NEG', one(0xB147, 1, 1, 0x0100), 'LDA #00')
    k('B127_CAM3', L(0xB149, 0xB15B), 'STA $C5 .. CMP $C3')
    k('B127_CMPC4', c(0xB15D), 'CMP $C4')
    k('B127_Y', L(0xB161, 0xB167), 'LDA $1881,X ASL CMP $C5')
    k('B127_CMPC6', c(0xB169), 'CMP $C6')
    k('B127_VIS', L(0xB16D, 0xB190), 'resets, JSR 7170, JSR 6F9A, SEC RTS')
    # --- C0/7170 palette ---
    k('P7170_HEAD', L(0x7170, 0x7172), 'LDA $54')
    k('P7170_RTS', c(0x7180), 'RTS')
    k('P7170_CLS', L(0x7174, 0x7179), 'LDX $6D LDA $1100,X')
    k('P7170_JMP', L(0x717B, 0x7180), 'ASL TAX JSR (7181,X)')
    k('P7199', c(0x7199), 'LDX #05')
    k('P7212', L(0x7212, 0x7217), 'LDX #06 BRL')
    k('P7217', L(0x7217, 0x721C), 'LDX #07 BRL')
    k('P719B', L(0x719B, 0x719E), 'LDA $0B80,X')
    k('P71A0_CLAIM', L(0x71A0, 0x71D6) + mvn(0x71D6, 24) + L(0x71D9, 0x71E3) + mvn(0x71E3, 24) + L(0x71E6, 0x71EB),
      '71A0-71EA with two 24-byte MVNs from E4')
    k('MVN_E4_BYTE', T().base(0xC071D6), 'MVN $7E,$E4 one byte')
    k('P721C_TAIL', L(0x723D, 0x7265) + mvn(0x7265, 24) + L(0x7268, 0x7272) + mvn(0x7272, 24) + L(0x7275, 0x727A), '')
    k('P72B4_TAIL', L(0x72D5, 0x72FD) + mvn(0x72FD, 24) + L(0x7300, 0x730A) + mvn(0x730A, 24) + L(0x730D, 0x7312), '')
    for nm, a in (('P721C', 0x721C), ('P72B4', 0x72B4)):
        if a == 0x721C:
            P_ = dict(ini=0x721C, top=0x721E, bpl=0x7221, inx=0x7223, cpx_bmi=0x7226, ini2=0x7228, top2=0x722A,
                      bpl2=0x722D, claim=0x722F, claim_end=0x723D, nxt=0x727A, nxt_bmi=0x7281, cmp=0x7285,
                      cmp_bne=0x7294, share=0x7296, share_bra=0x72A8, inx2=0x72AA, cpx2_bcs=0x72AD, brl2=0x72AF)
        else:
            P_ = dict(ini=0x72B4, top=0x72B6, bpl=0x72B9, inx=0x72BB, cpx_bmi=0x72BE, ini2=0x72C0, top2=0x72C2,
                      bpl2=0x72C5, claim=0x72C7, claim_end=0x72D5)
        k(nm + '_INI', c(P_['ini']))
        k(nm + '_TOP', L(P_['top'], P_['bpl']))
        k(nm + '_FREE', br(P_['bpl'], 0) + L(P_['inx'], P_['cpx_bmi']))
        k(nm + '_INI2', c(P_['ini2']))
        k(nm + '_TOP2', L(P_['top2'], P_['bpl2']))
        k(nm + '_CLAIM', br(P_['bpl2'], 0) + L(P_['claim'], P_['claim_end']))
        if a == 0x721C:
            k(nm + '_USED', br(P_['bpl'], 1) + L(P_['cmp'], P_['cmp_bne']))
            k(nm + '_SHARE', br(P_['cmp_bne'], 0) + L(P_['share'], P_['share_bra']) + c(P_['share_bra']))
            k(nm + '_NEXT', br(P_['cmp_bne'], 1) + L(P_['nxt'], P_['nxt_bmi']))
            k(nm + '_USED2', br(P_['bpl2'], 1) + L(P_['inx2'], P_['cpx2_bcs']))
            k(nm + '_BRL2', br(P_['cpx2_bcs'], 0) + c(P_['brl2']))
    # --- C0/6F9A VRAM slot ---
    k('V6F9A', L(0x6F9A, 0x6F9F) + br(0x6F9F, 0) + L(0x6FA1, 0x6FA6) + c(0x6FA6), 'dispatch + RTS')
    k('V6FB9', c(0x6FB9), 'RTS')
    k('V6FBA', L(0x6FBA, 0x6FC1) + br(0x6FC1, 1) + c(0x6FC5), 'class test, LDX #19')
    k('V6FC7', L(0x6FC7, 0x6FCA), 'LDA $0B88,X')
    k('V6FCC', br(0x6FCA, 0) + L(0x6FCC, 0x7003), 'claim')
    k('V7003', br(0x6FCA, 1) + c(0x7003) + br(0x7004, 1), 'DEX BNE')
    # --- AB45 / A9CD ---
    k('AB45_HEAD', L(0xAB45, 0xAB7F))
    k('AB45_ORA', one(0xAB81, 0, 1, 0x0100))
    k('AB45_MID', L(0xAB84, 0xAB94))
    k('AB45_ORA2', one(0xAB96, 0, 1, 0x0100))
    k('AB45_END', L(0xAB99, 0xABA2))
    k('A9CD_HEAD', L(0xA9CD, 0xA9D4))
    k('A9CD_LINKED', c(0xAA06))
    k('A9CD_MID', L(0xA9D6, 0xA9E0))
    k('A9CD_EMPTY', L(0xA9E2, 0xA9F2))
    k('A9CD_INSERT', L(0xA9F2, 0xAA07))
    # --- CAD9 class 0 ---
    k('CAD9_GATE', L(0xCAD9, 0xCADE) + br(0xCADE, 1) + L(0xCAE2, 0xCAE5) + br(0xCAE5, 0) + br(0xCAE7, 0)
      + L(0xCAE9, 0xCAEC) + br(0xCAEC, 0) + L(0xCAEE, 0xCAF3) + br(0xCAF3, 1) + c(0xCB01), 'CAD9-CB01 class 0')
    k('CBDC_HEAD', L(0xCBDC, 0xCBF7) + br(0xCBF7, 1) + c(0xCBFE), 'CBDC-CBF7, JSR CB3A')
    k('CB3A_HEAD', L(0xCB3A, 0xCB3D))
    k('CB3A_M0', br(0xCB3D, 1) + L(0xCB52, 0xCB59) + c(0xCB59))
    k('CB3A_NZ', br(0xCB3D, 0) + L(0xCB3F, 0xCB41))
    k('CB3A_M2', br(0xCB41, 1) + L(0xCB6E, 0xCB7A))
    k('CB3A_M3', br(0xCB41, 0) + br(0xCB43, 1) + L(0xCB5B, 0xCB6C) + c(0xCB6C))
    k('CB3A_M1', br(0xCB41, 0) + br(0xCB43, 0) + L(0xCB45, 0xCB50) + c(0xCB50))
    k('CB3A_TAIL', L(0xCB7A, 0xCB8B) + br(0xCB8B, 1) + L(0xCBB1, 0xCBC6) + br(0xCBC6, 1) + L(0xCBDA, 0xCBDC))
    k('CC01_CC10', br(0xCC01, 1) + L(0xCC04, 0xCC07) + br(0xCC07, 1) + L(0xCC0B, 0xCC10), 'CC01-CC0F, JSR E952')
    k('E952', L(0xE952, 0xE957) + br(0xE957, 0) + L(0xE959, 0xE973))
    k('CC10_CC5D', br(0xCC10, 0) + L(0xCC12, 0xCC53) + c(0xCC53))
    k('PIECE_NEXT', L(0xCC55, 0xCC5D))
    k('PIECE_READ_OP', L(0xCC5F, 0xCC62), 'BIT #4000 (the LDA [$D3],Y is priced by bank)')
    k('PIECE_534', br(0xCC62, 1) + c(0xCC6F) + L(0xCC72, 0xCC76), '+ BNE')
    k('PIECE_687', br(0xCC62, 0) + c(0xCC64) + L(0xCC67, 0xCC6B), '+ BNE')
    k('PIECE_687_LAST', c(0xCC6D), 'BRA CC78')
    rd8 = lambda pc, op: 0 if op == 0xB7 else None
    k('CC78_TEMPL', L(0xCC78, 0xCCCB, rd8), 'CC78-CCCA without its [$D3],Y read')
    for i, (bpl, zero, nxt, end) in enumerate(((0xCCCB, 0xCCD1, 0xCCD3, 0xCCE5), (0xCCE5, 0xCCEB, 0xCCED, 0xCCFF),
                                               (0xCCFF, 0xCD05, 0xCD07, 0xCD19), (0xCD19, 0xCD1F, 0xCD21, 0xCD2C))):
        k('TPL%d_NEG' % i, br(bpl, 0) + L(bpl + 2, zero))
        k('TPL%d_POS' % i, br(bpl, 1) + c(zero))
        k('TPL%d_REST' % i, L(nxt, end, rd8), 'without the [$D3],Y reads')
    k('CD2C', L(0xCD2C, 0xCD55))
    for i, (bcc, lo, join, nxt) in enumerate(((0xCD55, 0xCD62, 0xCD6B, 0xCD71), (0xCD71, 0xCD7E, 0xCD87, 0xCD8D),
                                              (0xCD8D, 0xCD9A, 0xCDA3, 0xCDA9), (0xCDA9, 0xCDB6, 0xCDBF, None))):
        k('ATTR%d_LO' % i, br(bcc, 1) + L(lo, join))
        k('ATTR%d_HI' % i, br(bcc, 0) + L(bcc + 2, lo))
        if nxt:
            k('ATTR%d_JOIN' % i, L(join, nxt))
    k('CDBF', L(0xCDBF, 0xCDC8))
    k('E534_FIXED', L(0xE534, 0xE687, rd8), 'without its 32 [$CD],Y reads')
    k('E687_HEAD', L(0xE687, 0xE69D))
    k('E687_7F', br(0xE69D, 0) + c(0xE69F) + L(0xE8C2, 0xE935))
    k('E687_D2', br(0xE69D, 1) + L(0xE6A2, 0xE6A5) + br(0xE6A5, 0) + c(0xE6A7) + L(0xE7BC, 0xE83F))
    k('E687_D3', br(0xE69D, 1) + L(0xE6A2, 0xE6A5) + br(0xE6A5, 1) + c(0xE6AA) + br(0xE6AB, 0) + c(0xE6AD)
      + L(0xE83F, 0xE8C2))
    k('E687_D4', br(0xE69D, 1) + L(0xE6A2, 0xE6A5) + br(0xE6A5, 1) + c(0xE6AA) + br(0xE6AB, 1) + c(0xE6B0)
      + br(0xE6B1, 0) + c(0xE6B3) + L(0xE739, 0xE7BC))
    k('B1B2_HEAD', L(0xB1B2, 0xB1D1))
    k('B1B2_TEST', L(0xB1D1, 0xB1D3))
    k('B1B2_END', br(0xB1D3, 1) + L(0xB201, 0xB204))
    k('B1B2_ENTRY', br(0xB1D3, 0) + L(0xB1D5, 0xB1E9) + P.mean_cost(0xB1E9) + L(0xB1EB, 0xB1FD)
      + P.mean_cost(0xB1FD) + L(0xB1FE, 0xB201), 'two $0100-byte DMAs, the traces\' mean sync')
    k('EA42', L(0xEA42, 0xEA49) + br(0xEA49, 1) + L(0xEA51, 0xEA59) + br(0xEA59, 0) + L(0xEA5B, 0xEA60)
      + br(0xEA60, 0) + c(0xEA62))
    # --- B271 / B309 / B8CA ---
    k('B271_HEAD', L(0xB271, 0xB2A8))
    k('B271_BUCKET', L(0xB2A8, 0xB2AB))
    k('B271_EMPTY', br(0xB2AB, 1))
    k('B271_HEADOBJ', br(0xB2AB, 0) + L(0xB2AD, 0xB2B2))
    k('B271_CHAIN', L(0xB2B2, 0xB2B7))
    k('B271_CHAIN_END', br(0xB2B7, 1))
    k('B271_CHAIN_OBJ', br(0xB2B7, 0) + L(0xB2B9, 0xB2BE), '+ BRA after the call: B271_CHAIN_BRA')
    k('B271_CHAIN_BRA', c(0xB2BE))
    k('B271_DEY', L(0xB2C0, 0xB2C2))
    k('B271_FILLS', L(0xB2C4, 0xB2C8))
    for nm, (top, bcs, lp, bcc, tail, nxt) in (('F0', (0xB2C8, 0xB2CA, 0xB2CC, 0xB2D6, 0xB2D8, 0xB2DE)),
                                               ('F1', (0xB2DE, 0xB2E0, 0xB2E2, 0xB2EC, 0xB2EE, 0xB2F4)),
                                               ('F2', (0xB2F4, 0xB2F6, 0xB2F8, 0xB302, 0xB304, 0xB309))):
        k(nm + '_TOP', L(top, bcs))
        k(nm + '_BODY', br(bcs, 0) + L(lp, bcc))
        k(nm + '_TAIL', L(tail, nxt))
    k('B309_HEAD', c(0xB309) + L(0xB701, 0xB708) + br(0xB708, 1) + L(0xB716, 0xB719) + br(0xB719, 1) + br(0xB71D, 0)
      + c(0xB71F), 'JSR B701 .. JSR B8CA')
    k('B309_MID', c(0xB722) + L(0xB725, 0xB72E) + br(0xB30C, 1) + L(0xB30F, 0xB316) + br(0xB316, 1)
      + L(0xB329, 0xB339), 'JSR E9E2 .. AND #0C')
    k('B309_G0', br(0xB339, 1) + L(0xB341, 0xB363))
    k('B309_G1', br(0xB339, 0) + LR(0xB33B, 0xB33D, 1, 0, 0x2100) + br(0xB33D, 1) + LR(0xB397, 0xB3BB, 1, 0, 0x2100))
    k('B309_G2', br(0xB339, 0) + LR(0xB33B, 0xB33D, 1, 0, 0x2100) + br(0xB33D, 0) + LR(0xB33F, 0xB341, 1, 0, 0x2100)
      + LR(0xB3BB, 0xB3DF, 1, 0, 0x2100))
    k('B309_OAM', L(0xB363, 0xB36E) + 4 * L(0xB36E, 0xB393) + 3 * br(0xB393, 1) + br(0xB393, 0) + L(0xB395, 0xB397))
    k('E9E2', L(0xE9E2, 0xE9E8) + 4 * (L(0xE9E8, 0xE9ED) + br(0xE9ED, 0) + L(0xE9EF, 0xE9F2)) + 3 * br(0xE9F2, 1)
      + br(0xE9F2, 0) + L(0xE9F4, 0xE9F7))
    k('B8CA_X', L(0xB8CA, 0xB8EC) + 4 * L(0xB8EC, 0xB908) + br(0xB908, 1)
      + 3 * (br(0xB908, 0) + L(0xB90A, 0xB913) + c(0xB913)) + L(0xB915, 0xB920), 'prologue + the X half')
    k('B8CA_BA47', L(0xBA47, 0xBA65))
    for arm, rows in (('A', ((0xB922, 0xB92B, 0xB92F, 0xB931), (0xB936, 0xB93F, 0xB943, 0xB945),
                             (0xB94A, 0xB953, 0xB957, 0xB959), (0xB95E, 0xB967, 0xB96B, 0xB96D),
                             (0xB972, 0xB978, 0xB97C, 0xB97E))),):
        k('B8CA_A_ENTRY', br(0xB920, 0))
        for i, (st, bcc, bcs, clamp) in enumerate(rows):
            k('B8CA_A%d_HEAD' % i, LR(st, bcc, 1, 0, 0x0100, 0x7F))
            k('B8CA_A%d_NC' % i, br(bcc, 1) + LR(clamp, clamp + 2, 1, 0, 0x0100, 0x7F))
            k('B8CA_A%d_CMP' % i, br(bcc, 0) + LR(bcc + 2, bcs, 1, 0, 0x0100, 0x7F))
            k('B8CA_A%d_HI' % i, br(bcs, 1))
            k('B8CA_A%d_CLAMP' % i, br(bcs, 0) + LR(clamp, clamp + 2, 1, 0, 0x0100, 0x7F))
            k('B8CA_A%d_ST' % i, LR(clamp + 2, clamp + 5, 1, 0, 0x0100, 0x7F))
        k('B8CA_A_END', LR(0xB983, 0xB9A1, 1, 0, 0x0100, 0x7F))
    k('B8CA_BC', br(0xB920, 1) + L(0xB9A1, 0xB9A3))
    for i, (st, bcc, bcc2, clamp, store) in enumerate(((0xB9A5, 0xB9AE, 0xB9B2, 0xB9B4, 0xB9B6),
                                                      (0xB9B9, 0xB9C2, 0xB9C6, 0xB9C8, 0xB9CA),
                                                      (0xB9CD, 0xB9D6, 0xB9DA, 0xB9DC, 0xB9DE),
                                                      (0xB9E1, 0xB9EA, 0xB9EE, 0xB9F0, 0xB9F2))):
        k('B8CA_B%d_HEAD' % i, L(st, bcc))
        k('B8CA_B%d_NC' % i, br(bcc, 1))
        k('B8CA_B%d_CMP' % i, br(bcc, 0) + L(bcc + 2, bcc2))
        k('B8CA_B%d_LO' % i, br(bcc2, 1))
        k('B8CA_B%d_CLAMP' % i, br(bcc2, 0) + LR(clamp, store, 1, 0, 0x0100, 0x7F))
        k('B8CA_B%d_ST' % i, L(store, store + 3))
    k('B8CA_B_ENTRY', br(0xB9A3, 0))
    k('B8CA_B_BRA', c(0xB9F5))
    k('B8CA_C_ENTRY', br(0xB9A3, 1))
    for i, (st, bpl, bcs, clamp, store) in enumerate(((0xB9F7, 0xBA00, 0xBA04, 0xBA06, 0xBA08),
                                                     (0xBA0B, 0xBA14, 0xBA18, 0xBA1A, 0xBA1C),
                                                     (0xBA1F, 0xBA28, 0xBA2C, 0xBA2E, 0xBA30),
                                                     (0xBA33, 0xBA3C, 0xBA40, 0xBA42, 0xBA44))):
        k('B8CA_C%d_HEAD' % i, L(st, bpl))
        k('B8CA_C%d_POS' % i, br(bpl, 1))
        k('B8CA_C%d_CMP' % i, br(bpl, 0) + LR(bpl + 2, bcs, 1, 0, 0x0100, 0x7F))
        k('B8CA_C%d_HI' % i, br(bcs, 1))
        k('B8CA_C%d_CLAMP' % i, br(bcs, 0) + LR(clamp, store, 1, 0, 0x0100, 0x7F))
        k('B8CA_C%d_ST' % i, L(store, store + 3))
    # --- camera (C0/7506 + C0/7553), D = $1D00 ---
    k('CAMX_HEAD', L(0x7506, 0x7512))
    k('CAMX_EQ', br(0x7512, 1) + L(0x7522, 0x752C) + c(0x752C))
    k('CAMX_GT', br(0x7512, 0) + br(0x7514, 1) + L(0x752E, 0x753C))
    k('CAMX_GT_IN', br(0x753C, 1) + L(0x754B, 0x7552))
    k('CAMX_GT_CLAMP', br(0x753C, 0) + LR(0x753E, 0x754B, 1, 0, 0x1D00))
    k('CAMX_LT', br(0x7512, 0) + br(0x7514, 0) + LR(0x7516, 0x7522, 1, 0, 0x1D00))
    k('CAMY_HEAD', c(0x7552) + L(0x7553, 0x755F), 'X RTS + 7553-755E')
    k('CAMY_EQ', br(0x755F, 1) + LR(0x756F, 0x757B, 1, 0, 0x1D00))
    k('CAMY_GT', br(0x755F, 0) + br(0x7561, 1) + L(0x757B, 0x7589))
    k('CAMY_GT_IN', br(0x7589, 1) + L(0x7598, 0x759F))
    k('CAMY_GT_CLAMP', br(0x7589, 0) + LR(0x758B, 0x7598, 1, 0, 0x1D00))
    k('CAMY_LT', br(0x755F, 0) + br(0x7561, 0) + LR(0x7563, 0x756F, 1, 0, 0x1D00))
    k('CAMY_RTS', c(0x759F))
    # --- C0/28E1 doors ---
    k('D28E1_HEAD', L(0x28E1, 0x28E6))
    k('D28E1_ENTRY', L(0x28E6, 0x28EA))
    k('D28E1_END', br(0x28EA, 1) + L(0x28F6, 0x28F9))
    k('D28E1_CALL', br(0x28EA, 0) + c(0x28EC) + L(0x28F9, 0x2901))
    for tv, beq, arm, end in ((0xE6, 0x2903, 0x291C, 0x2933), (0xEC, 0x2907, 0x2933, 0x2958),
                              (0xEE, 0x290B, 0x2958, 0x297D), (0xFA, 0x290F, 0x297D, 0x29BA)):
        k('D28F9_CMP_%02X' % tv, LR(beq - 2, beq, 1, 0, 0x0100))
        k('D28F9_HIT_%02X' % tv, br(beq, 1) + LR(arm, end, 1, 0, 0x0100))
        k('D28F9_MISS_%02X' % tv, br(beq, 0))
    k('D28F9_CMP_FC', LR(0x2911, 0x2913, 1, 0, 0x0100))
    k('D28F9_OTHER', br(0x2913, 1), 'BNE ->2918')
    k('D28F9_RET', L(0x2918, 0x291C))
    k('D28E1_NEXT', L(0x28EF, 0x28F4))
    k('D28E1_LOOP', br(0x28F4, 1))
    k('D28E1_FULL', br(0x28F4, 0) + L(0x28F6, 0x28F9))
    # --- C0/A712 ---
    k('A712_FILL', L(0xA712, 0xA717) + 0x40 * L(0xA717, 0xA71F) + 0x3F * br(0xA71F, 1) + br(0xA71F, 0)
      + L(0xA721, 0xA726) + 63 * (L(0xA726, 0xA72D) + mvn(0xA72D, 64) + L(0xA730, 0xA73A)) + 62 * br(0xA73A, 1)
      + br(0xA73A, 0) + L(0xA73C, 0xA756), 'A712-A755: the $70C0 page and the chest range lookup')
    k('A712_NONE', br(0xA756, 1) + L(0xA7B9, 0xA7BC))
    k('A712_SOME', br(0xA756, 0) + L(0xA758, 0xA761) + br(0xA761, 1) + L(0xA769, 0xA76D))
    k('A712_TEST', L(0xA76D, 0xA76F))
    k('A712_DONE', br(0xA76F, 1) + L(0xA7B9, 0xA7BC))
    k('A712_CLOSED', br(0xA76F, 0) + L(0xA771, 0xA785) + L(0xA7BC, 0xA7E1) + br(0xA7E1, 0) + L(0xA7E3, 0xA7E6)
      + br(0xA785, 1) + L(0xA7A9, 0xA7B7) + c(0xA7B7), 'one closed chest')
    # --- A7E9 / B204 (n = $7F2000) ---
    k('A7E9_HEAD', L(0xA7E9, 0xA805))
    k('A7E9_ITER', L(0xA805, 0xA80A))
    k('A7E9_END', L(0xA80C, 0xA810))
    k('B204_HEAD', L(0xB204, 0xB21B) + 16 * L(0xB21B, 0xB220) + 15 * br(0xB220, 1) + br(0xB220, 0) + L(0xB222, 0xB22E)
      + mvn(0xB22E, 32) + L(0xB231, 0xB237) + mvn(0xB237, 64) + L(0xB23A, 0xB240) + mvn(0xB240, 128)
      + L(0xB243, 0xB259))
    k('B204_ITER', L(0xB259, 0xB25E))
    k('B204_END', L(0xB260, 0xB262))
    k('BR_T', 18)
    k('BR_NT', 12)
    for n, v, r in K:
        print('%-18s %8d   %s' % (n, v, r))


T = P.T

if __name__ == '__main__':
    main()
