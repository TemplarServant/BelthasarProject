# Bake the TECH screen's ROM data into src/data/menu_techrom.h, so the menu stays ROM-free.
# Derived in disasm/notes/menu2_tech_screen.md. The screen is ONE list machine over a 37-entry
# "tech GROUP" space: groups 0-6 = the seven characters' 8 SINGLE techs (ids $01..$38), 7-21 = the
# fifteen PAIRS with 3 DOUBLE techs each ($39..$65), 22-36 = the fifteen TRIPLES with 1 each
# ($66..$74). The tabs "1 2 3" are Single/Double/Triple and index C2/BD65 for the group window.
#
#   C2/BD40  38-byte cumulative id boundary over the 37 groups
#   C2/BD65  (firstGroup, groupCount) per tab = 00 07 | 07 0F | 16 0F
#   CC/2963  per-group CHARACTER MASK (bit 0x80 = charId 0), the filter C2/BCCD applies
#   CC/253B  the MP cost table, indexed by techId for a SINGLE and by $7E:1607[char*64+tech] for a
#            double/triple (C2/BC22)
#   FF/F951  per-character cap on the "next tech to learn" that FF/F876 adds to the menu bitfield
#
# The $7E:1607 index table is REBUILT here exactly as FF/F8BD-F94B builds it at menu-open, out of
# CC/24B4 (pair participants, stride 3), CC/28DB (the two per-tech indices per double, stride 6),
# CC/2979 (the triple participation masks) and CC/2935 (the triple indices).
import sys, glob, os
sys.path.insert(0, "assets")
import text as CTTEXT   # the repo's own dictionary decoder

ROM = open("Chrono_Trigger_USA_SNES.sfc", "rb").read()
def r8(a):  return ROM[a]

BD40 = ROM[0x2BD40:0x2BD40 + 38]           # C2/BD40
BD65 = ROM[0x2BD65:0x2BD65 + 6]            # C2/BD65
MASK = ROM[0x0C2963:0x0C2963 + 37]         # CC/2963
F951 = ROM[0x3FF951:0x3FF951 + 8]          # FF/F951
MPTAB = ROM[0x0C253B:0x0C253B + 0x100]     # CC/253B

# ---- FF/F8BD..F94B: the per-character MP INDEX table, laid out as $7E:1607 + charId*64 + techId ----
# The address C2/BC46 forms is $7E:1607 + charId*64 + techId, and the per-character 64-byte blocks
# therefore OVERLAP: the whole thing is one flat region running $7E:1640 (char 0's first double,
# id $39) to $7E:17FB (char 6's triple, id $74). We bake exactly that region, and the C side indexes
# it as [charId*64 + techId - 0x39].
IDX_LO, IDX_HI = 0x1640, 0x17FC
idx = bytearray(IDX_HI - IDX_LO)
def put(charId, techId, v):
    a = 0x1607 + charId * 64 + techId
    if IDX_LO <= a < IDX_HI: idx[a - IDX_LO] = v

tech = 0x39                                     # double ids start at $39 ($1640 = $1607 + $39)
for pair in range(15):
    a = ROM[0x0C24B4 + pair * 3]
    b = ROM[0x0C24B5 + pair * 3]
    for k in range(3):
        put(a, tech, ROM[0x0C28DB + pair * 6 + k * 2 + 0])
        put(b, tech, ROM[0x0C28DB + pair * 6 + k * 2 + 1])
        tech += 1
run = 0
tech = 0x66                                     # triple ids start at $66
for tri in range(15):
    m = ROM[0x0C2979 + tri]
    for c in range(8):
        if m & (0x80 >> c):
            put(c, tech, ROM[0x0C2935 + run]); run += 1
    tech += 1

# CC/3A09 -- the per-tech DESCRIPTION pointer table (16-bit, bank $CC). The strings are
# DICTIONARY-COMPRESSED, so they go through assets/text.py's decoder (the same one the item and
# location names use) and are baked as fixed-width ASCII, the way ctItemCat already is.
# Entries 0x76..0x78 are the three TAB names the screen puts in the bar next to the description.
SUBS = CTTEXT.load_dict(ROM)
DESC_N = 0x7A
desc = []
for tid in range(DESC_N):
    ptr = ROM[0x0C3A09 + tid*2] | (ROM[0x0C3A0A + tid*2] << 8)
    try: t = CTTEXT.decode_string(ROM, 0x0C0000 + ptr, SUBS)
    except Exception: t = ""
    t = "".join(ch for ch in t.split(chr(10))[0] if 32 <= ord(ch) < 127)[:31]
    desc.append(t)

# ---- the CAST tables (disasm/notes/menu2_tech_effect.md) --------------------------------------
# C2/B3E6 builds three bytes at the A press and C2/8ECC/C2/8E2D consume them:
#   $0DC0 = $CC:2140[id*12 + 0] & 0x1F      -- the amount NIBBLE (and bits 6/7 = FULL / negate)
#   $0DBD = ($CC:2140[id*12 + 1] & 0xC0)    -- b7 touch HP, b6 touch MP
#           | ((($CC:1ACB[id*2] & 1) + 1) << 2)   -- b3 = ALL PARTY
#   $0DBF = the caster's DERIVED MAGIC (sheet +0x39), so the tech arm is magic-scaled
# and the per-target spread reads $C0:FE00 with a cursor $7E:0D00 and NO wrap on the index, so the
# table has to be baked with the over-read tail (cursor 0xFF + 5).
TECH_N = 0x75
fxb = bytearray(TECH_N); nib = bytearray(TECH_N)
for t in range(TECH_N):
    rec = 0x0C2140 + t * 12
    nib[t] = ROM[rec] & 0x1F
    allp = ((ROM[0x0C1ACB + t * 2] & 1) + 1) << 2
    fxb[t] = (ROM[rec + 1] & 0xC0) | allp
RNG = ROM[0x00FE00:0x00FE00 + 0x110]          # C0/FE00 + the 5-byte over-read the ROM performs
POSE = ROM[0x2B519:0x2B519 + 7]               # C2/B519, indexed by charId -- SEVEN entries, not five

def emit(f, name, data, note):
    f.write("// %s\n" % note)
    f.write("static const uint8_t %s[%d] = {\n" % (name, len(data)))
    for i in range(0, len(data), 16):
        f.write("  " + ",".join("0x%02X" % b for b in data[i:i + 16]) + ",\n")
    f.write("};\n")

with open("src/data/menu_techrom.h", "w") as f:
    f.write("// GENERATED by tools/gen/menu_techrom.py -- do not edit. The TECH screen's ROM data.\n")
    f.write("// See the script header and disasm/notes/menu2_tech_screen.md for every address.\n")
    f.write("#ifndef CT_MENU_TECHROM_H\n#define CT_MENU_TECHROM_H\n#include <stdint.h>\n")
    emit(f, "ctTechBound", BD40, "C2/BD40 -- cumulative tech-id boundary over the 37 groups (38 bytes)")
    emit(f, "ctTechTabWin", BD65, "C2/BD65 -- (firstGroup, count) per tab: Single 0..6, Double 7..21, Triple 22..36")
    emit(f, "ctTechGroupMask", MASK, "CC/2963 -- the CHARACTERS in each group (bit 0x80 = charId 0); C2/BCCD's filter")
    emit(f, "ctTechNextCap", F951, "FF/F951 -- per-character cap on the not-yet-learned tech FF/F876 adds to the menu bitfield")
    emit(f, "ctTechMpCost", MPTAB, "CC/253B -- MP cost, indexed by techId (Single) or by ctTechMpIdx (Double/Triple)")
    f.write("// CC/3A09 + id*2 -> the dictionary-compressed DESCRIPTION string, decoded here.\n")
    f.write("// Entries 0x76..0x78 are the Single/Double/Triple TAB names.\n")
    f.write("static const char ctTechDesc[%d][32] = {\n" % DESC_N)
    for t in desc:
        f.write('  "%s",\n' % t.replace(chr(92), chr(92)*2).replace(chr(34), chr(92) + chr(34)))
    f.write("};\n")
    emit(f, "ctTechFx", bytes(fxb), "$0DBD per tech id (C2/B3E6): b7 restore HP, b6 restore MP, b3 ALL PARTY")
    emit(f, "ctTechNib", bytes(nib), "$0DC0 per tech id = CC/2140[id*12] & 0x1F -- the amount multiplier")
    emit(f, "ctTechRng", RNG, "C0/FE00 -- the per-target spread table; C2/8ECC indexes it with NO wrap, hence the tail")
    emit(f, "ctTechCastPose", POSE, "C2/B519 -- $19E1, the caster pose, indexed by charId (SEVEN entries: Ayla is 5)")
    emit(f, "ctTechMpIdx", bytes(idx), "$7E:1640..17FB, rebuilt per FF/F8BD-F94B: index it [charId*64 + techId - 0x39] to get the CC/253B index a DOUBLE/TRIPLE costs THIS character")
    f.write("#endif\n")

print("wrote src/data/menu_techrom.h")
print("  C2/BD40:", BD40.hex(' '))
print("  C2/BD65:", BD65.hex(' '), "(expect 00 07 07 0f 16 0f)")
print("  CC/2963[0..6]:", MASK[:7].hex(' '), "(the seven singles: one bit each)")
print("  MP singles 1..8:", [MPTAB[i] for i in range(1, 9)], "(expect 2,2,2,4,8,10,12,20)")
print("  ctTechMpIdx non-zero:", sum(1 for v in idx if v))
print("  fx/nib for the nine field ids:", [(hex(t), hex(fxb[t]), hex(nib[t])) for t in (0x09,0x0C,0x0F,0x1A,0x1D,0x21,0x24,0x27,0x29)])
print("  castPose:", POSE.hex(" "), "(expect 03 05 04 00 03 06 04)")
print("  desc[01]=%r desc[09]=%r desc[39]=%r desc[76..78]=%r" % (desc[1], desc[9], desc[0x39], desc[0x76:0x79]))

# Cross-check the rebuilt index table against a live oracle page when one is around.
for p in sorted(glob.glob("scratchpad/menu/*.wram")):
    w = open(p, "rb").read()
    if len(w) < 0x8000: continue
    live = w[IDX_LO:IDX_HI]
    if not any(live): continue
    bad = sum(1 for i in range(len(idx)) if idx[i] != live[i])
    print("  vs %s: $7E:%04X..%04X %d mismatches in %d (%d non-zero live)"
          % (p, IDX_LO, IDX_HI - 1, bad, len(idx), sum(1 for v in live if v)))
    break
