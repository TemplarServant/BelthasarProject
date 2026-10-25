#!/usr/bin/env python3
r"""The MENU against the cartridge, EVERY frame of a drive -- the audit instrument (user request 2026-09-24: "investigate
the menu again, all of its screens and features").

    python tools/menu_audit_cmp.py <drive> [--peek F ...] [--window A B]

Each drive presses the same buttons on both sides and both sides write a 128x112 thumbnail of every frame (the
oracle's PEEK_THUMBS, the port's CT_SHOTS_THUMBS: the same records, docs/DEBUG_SURFACE.md), so a whole tour of the
menu is two runs. The cartridge side is the LOGICAL oracle (CT_ORACLE_LOGICAL=1: the menu's heavy builds overrun the
NMI on real time, CLAUDE.md's lag-frame rule). Alignment (tools/menu_shell_cmp.py measured it): the cartridge reads a
press one frame after the button goes down (the pad latch C2/84D2), so the port presses at the oracle's frame + 1; the
cartridge displays a frame's state during the NEXT frame, so port frame P = thumbnail P + 2 (SHIFT; memory
ct-frame-alignment-p2). L/R go through CT_HOLDBTN
($00F2 layout: L=20, R=10) on the frame the port's latch must see them.

Prints the differing frames in runs, with the thumbnail's bounding box of the difference (screen pixels):

    menu-audit: <drive> f<A>-f<B>: <N> of <M> frame(s) differ
      f<a>-f<b> box (x0, y0, x1, y1)

`--peek F` compares the full-resolution frame F too and writes PNGs (build/_menuaudit/<drive>/o_F.png, p_F.png; with
`--window A B` everything goes under build/_menuaudit/<drive>/wA-B/, so the suite's checks on one drive can run at once).
`--ram ADDR:LEN,...` compares WRAM bytes at the drive's end (the oracle's --peek of `end` frames, the port's frame end-2).
`--tbframe` adds, for each --peek frame, the FIELD dialogue box's frame alone (the bottom box, lines 131-210: its top
ten and bottom eight lines and its edge columns 0-7 / 248-255 -- no text lands there): the window's tiles, palette and
ramp, whatever the typewriter's timing.
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctlib import fb  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CT = os.path.join(ROOT, 'build', 'ct.exe')
FIELD = os.path.join(ROOT, 'build', 'Belthasar.exe')
REC = 6 + 128 * 112 * 3
# LakeSnes serial masks: B=1 Y=2 Sel=4 St=8 Up=10 Down=20 Left=40 Right=80 A=100 X=200 L=400 R=800
KEY = {0x100: 'a', 0x1: 'b', 0x10: 'u', 0x20: 'd', 0x40: 'l', 0x80: 'r', 0x200: 'm', 0x1100: 'a'}
# 0x1100 = a FIELD A (a talk that opens a shop): the field reads it on its own frame on both sides, so it is not shifted
# (the menu's presses are, a frame -- MENU_PRESS); the oracle gets A (0x100).
HOLD = {0x400: 0x20, 0x800: 0x10}
SHIFT = int(os.environ.get('MENUAUDIT_SHIFT', '2'))
MENU_PRESS = int(os.environ.get('MENUAUDIT_PRESS', '1'))


def P(*pairs):
    return list(pairs)


# name: (oracle state, port WRAM seed, port scene, pokes, presses [(mask, oracle frame)], F0, F1, end)
DRIVES = {
    # The ribbon's six tabs, each screen entered and left: Equip (a member's gear, the list), Item (the list, Up/Down),
    # Tech (a member, the list), Config (the rows), Form, Save -- then the close.
    'tour': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
             P((0x200, 4),
               (0x100, 60), (0x20, 90), (0x10, 115), (0x100, 140), (0x20, 175), (0x100, 205), (0x1, 245), (0x1, 280),
               (0x80, 340), (0x100, 370), (0x20, 410), (0x20, 435), (0x10, 460), (0x1, 490),
               (0x80, 520), (0x100, 550), (0x100, 590), (0x20, 630), (0x1, 660), (0x1, 690),
               (0x80, 720), (0x100, 750), (0x20, 790), (0x20, 815), (0x1, 845),
               (0x80, 875), (0x100, 905), (0x1, 950),
               (0x80, 980), (0x100, 1010), (0x1, 1055),
               (0x1, 1090)),
             20, 1130, 1150),
    # The gear panel of the SECOND and THIRD member: C2/A71E's push ($0D25 = (3 - $0412) * 8 steps, then the party
    # column scrolls up with the new one), and the way back (C2/A85C) to the member select and the ribbon.
    'equip1': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
               P((0x200, 4), (0x100, 60), (0x20, 90), (0x100, 120), (0x1, 170), (0x1, 220), (0x1, 250)),
               20, 280, 300),
    # A Tonic used from the Item screen (the user's report: "when using a tonic ..."): three Tonics poked into the empty
    # bag slot 1 on both sides; Crono is at 25/96. X, R (Item), A, Down (the Tonic), A (pick), A (use -> target),
    # A (on Crono), then B out of every level and the close.
    'itemuse': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                {0x2401: 0xBD, 0x2501: 0x03},
                P((0x200, 4), (0x80, 50), (0x100, 80), (0x20, 110), (0x100, 130), (0x100, 150), (0x100, 170),
                  (0x1, 270), (0x1, 295), (0x1, 320), (0x1, 345), (0x1, 370)),
                20, 440, 460),
    # The Item screen's MOVE: A on the Pendant (row 0), Down to the empty row, A (C2/AAE0's swap, C2/AAFF's yield),
    # Down, then B to the labels, the ribbon, the close.
    'itemmove': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
                 P((0x200, 4), (0x80, 50), (0x100, 80), (0x100, 110), (0x20, 140), (0x100, 170), (0x20, 200),
                   (0x1, 230), (0x1, 260), (0x1, 290)),
                 20, 300, 400),
    # The FORM exchange: R x4 to the Form tab, A (the picker opens on Lucca, C2/C649 skips the locked Crono), A (pick
    # her), Down (Frog), A (C2/C70C swaps them), B to the ribbon, B to close.
    'formswap': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
                 P((0x200, 4), (0x80, 50), (0x80, 60), (0x80, 70), (0x80, 80), (0x100, 110), (0x100, 140),
                   (0x20, 170), (0x100, 200), (0x1, 240), (0x1, 270)),
                 20, 290, 400),
    # CONFIG: R x3 to the Config tab, A (enter: cell 0, Stereo), A (the toggle: the value retypes), Down, Right (cell 6,
    # Window Color), A (its edit: the cursor freezes), Down, Down (inert), Right, Right (colours 1 and 2: C2/C511 reloads
    # the window tiles and CGRAM row 0), B (the edit off), B (the pack, to the ribbon), B (close).
    'config': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
               P((0x200, 4), (0x80, 50), (0x80, 60), (0x80, 70), (0x100, 100), (0x100, 130), (0x20, 160),
                 (0x80, 190), (0x100, 220), (0x20, 250), (0x20, 275), (0x80, 300), (0x80, 325), (0x1, 360),
                 (0x1, 390), (0x1, 420)),
               20, 400, 540),
    # A saved window colour: $2991 = A3 (colour 3) poked on both sides; X (C2/983F loads it at the open), R x5 over
    # every tab, B (close).
    'wincolour': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {0x2991: 0xA3},
                  P((0x200, 4), (0x80, 50), (0x80, 80), (0x80, 110), (0x80, 140), (0x80, 170), (0x1, 210)),
                  20, 230, 300),
    # The colour changed INSIDE the menu, then the dim views, twice: R x3, A (Config), Down, Right (cell 6), A (edit),
    # Right x3 (colour 3), B (edit off), B (the pack, tab 3), R (the Form tab: C2/CA27 halves the new colour into row 4),
    # A (Form), B, L (tab 3), A (Config), Down, Right, A, Right x2 (colour 5), B, B, L x3 (the Equip tab: C2/A0E2), A (the
    # member select: Lucca's and Frog's panels in row 4), B, B (close).
    'wincolour2': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
                   P((0x200, 4), (0x80, 50), (0x80, 60), (0x80, 70), (0x100, 100), (0x20, 130), (0x80, 155),
                     (0x100, 180), (0x80, 205), (0x80, 225), (0x80, 245), (0x1, 270), (0x1, 295), (0x80, 330),
                     (0x100, 360), (0x1, 400), (0x40, 430), (0x100, 460), (0x20, 490), (0x80, 515), (0x100, 540),
                     (0x80, 565), (0x80, 585), (0x1, 610), (0x1, 635), (0x40, 670), (0x40, 695), (0x40, 720),
                     (0x100, 750), (0x1, 790), (0x1, 820)),
                   20, 840, 900),
    # Config's Window Color reaching the FIELD's dialogue box: the tent keeper; X, R x3, A (Config), Down, Right (cell 6),
    # A (edit), Right x3 (colour 3), B, B (the pack), B (close: the reload runs C0/6E5C / C0/70E9 / C0/29F7 on the new
    # $2991), A (talk: the box in colour 3).
    'tbcolour': ('saves/field_re/tent_keeper_lake.state', 'saves/field_re/tent_keeper.lakewram', 5, {},
                 P((0x200, 4), (0x80, 50), (0x80, 60), (0x80, 70), (0x100, 100), (0x20, 130), (0x80, 155),
                   (0x100, 180), (0x80, 205), (0x80, 225), (0x80, 245), (0x1, 270), (0x1, 295), (0x1, 330),
                   (0x100, 440)),
                 20, 400, 600),
    # A TECH CAST from the menu: R x2 (the Tech tab), A (the member select), Down x2 (Frog), A (his list), A (Slurp: the
    # target select), A (on Crono, 25/96), then B out of every level and the close.
    'techcast': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
                 P((0x200, 4), (0x80, 50), (0x80, 60), (0x100, 90), (0x20, 120), (0x20, 140), (0x100, 170),
                   (0x100, 210), (0x100, 250), (0x1, 330), (0x1, 360), (0x1, 390), (0x1, 420)),
                 20, 440, 520),
    # The DUAL list (saves/tech_double.state, the overworld: Crono + Marle know Aura Whirl): X at 80, R x2 (Tech), A,
    # R (the Dual tab), A (the list: the participants' panels, each with its sprite), B, B (the ribbon: its preview keeps
    # the Dual tab, $0418), A (Tech again, still on Dual: no ribbon move reset it), B, B (the close).
    # (The overworld open's NMI-off stash used to cross one more frame boundary here than on ow_w0 -- the real beam's phase;
    # the logical oracle starts NMI-off work at phase 0 now, CLAUDE.md's exception, and the check grades from f100.)
    'techdual': ('saves/tech_double.state', 'saves/field_re/tech_double.lakewram', 1, {},
                 P((0x200, 80), (0x80, 130), (0x80, 140), (0x100, 180), (0x80, 210), (0x100, 240), (0x1, 300),
                   (0x1, 330), (0x100, 360), (0x1, 400), (0x1, 430)),
                 130, 450, 520),
    # ...and the Dual tech CAST from the menu: A on Aura Whirl at 270 (both casters pay, everyone is healed), then B out.
    'techdualcast': ('saves/tech_double.state', 'saves/field_re/tech_double.lakewram', 1, {},
                     P((0x200, 80), (0x80, 130), (0x80, 140), (0x100, 180), (0x80, 210), (0x100, 240), (0x100, 270),
                       (0x1, 400), (0x1, 430), (0x1, 460), (0x1, 490)),
                     130, 500, 570),
    # The same list with Frog's MP poked to 0 (+7 = $2747): string 0x20's `MP Used` in C2/BA2F's `$0D5E` palette (red,
    # the MP below the cost) and his HP row's C2/A1EF rule untouched; A on Slurp (refused), then B out.
    'techmp': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {0x2747: 0x00},
               P((0x200, 4), (0x80, 50), (0x80, 60), (0x100, 90), (0x20, 120), (0x20, 140), (0x100, 170),
                 (0x100, 210), (0x1, 250), (0x1, 280), (0x1, 310), (0x1, 340)),
               20, 360, 420),
    # The SAVE screen, on the overworld (its gate C2/CEDC lets it open there): X at 80 (the port's seed enters the world
    # through a 44-frame load the cartridge's state is past), L (the ribbon wraps to Save), A, B, B.
    'owsave': ('saves/ow_w0.state', 'saves/field_re/ow_w0.lakewram', 1, {},
               P((0x200, 80), (0x40, 130), (0x100, 170), (0x1, 250), (0x1, 290)),
               100, 370, 400),
    # A SAVE IN ANOTHER WINDOW COLOUR, on the overworld: X at 80, R x3 (Config), A, Down, Right (cell 6), A (edit), Right x3
    # (colour 3), B, B (the pack), R x2 (the Save tab), A (the screen), A (slot 1: empty, saved at once), then B, B --
    # the slot is drawn in ITS OWN colour (C2/D266 `$0D79` = the save's $2991 & 7; C2/D329 -> C2/ECDB).
    'owsavecol': ('saves/ow_w0.state', 'saves/field_re/ow_w0.lakewram', 1, {},
                  P((0x200, 80), (0x80, 130), (0x80, 140), (0x80, 150), (0x100, 180), (0x20, 210), (0x80, 235),
                    (0x100, 260), (0x80, 285), (0x80, 305), (0x80, 325), (0x1, 350), (0x1, 375), (0x80, 410),
                    (0x80, 420), (0x100, 460), (0x100, 520), (0x1, 640), (0x1, 680)),
                  100, 690, 720),
    # ...and the OVERWRITE: the same save at 520, then A on the now occupied slot at 560 (C2/D4BB's prompt), A on Yes at
    # 600 (C2/CF7B: C2/CF37 in that frame, C2/D4D5 closes the prompt after its two yields), B, B.
    'owsave2': ('saves/ow_w0.state', 'saves/field_re/ow_w0.lakewram', 1, {},
                P((0x200, 80), (0x80, 130), (0x80, 140), (0x80, 150), (0x100, 180), (0x20, 210), (0x80, 235),
                  (0x100, 260), (0x80, 285), (0x80, 305), (0x80, 325), (0x1, 350), (0x1, 375), (0x80, 410),
                  (0x80, 420), (0x100, 460), (0x100, 520), (0x100, 560), (0x100, 600), (0x1, 680), (0x1, 720)),
                100, 730, 760),
    # The Item screen's equipment INFO PANEL (substate 5, C2/AB2F -> C2/AE91): an Iron Blade poked into bag slot 3; X, R
    # (Item), A, Down x3 (its row), A (pick), A (the panel: "Can be used by" + "Attack"), A (dismissed: the list), B, B, B.
    'iteminfo': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                 {0x2403: 0x02, 0x2503: 0x01},
                 P((0x200, 4), (0x80, 50), (0x100, 80), (0x20, 110), (0x20, 130), (0x20, 150), (0x100, 180),
                   (0x100, 210), (0x100, 260), (0x1, 300), (0x1, 330), (0x1, 360)),
                 20, 350, 440),
    # A TAB used from the Item screen (C2/B228's loop): a Power Tab poked into the empty bag slot 1; X, R (Item), A, Down,
    # A (pick), A (use: the target overlay), A (on Crono: Power + 1), then B out of every level and the close.
    'itemtab': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                {0x2401: 0xCD, 0x2501: 0x01},
                P((0x200, 4), (0x80, 50), (0x100, 80), (0x20, 110), (0x100, 140), (0x100, 170), (0x100, 210),
                  (0x1, 290), (0x1, 320), (0x1, 350), (0x1, 380)),
                20, 370, 460),
    # The Config's CONTROL PAD editor (cell 8, $0D87 -> substate 2): X, Right x3 (Config), A, Right (cell 5), Down x3 (cell 8),
    # A (the editor), Down x2 (its rows), B (back to the grid), B, B.
    'cfgpad': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
               P((0x200, 4), (0x80, 50), (0x80, 60), (0x80, 70), (0x100, 100), (0x80, 140), (0x20, 160), (0x20, 180),
                 (0x20, 200), (0x100, 230), (0x20, 270), (0x20, 290), (0x1, 320), (0x1, 350), (0x1, 380)),
               20, 400, 460),
    # The Config's sub-editors WORKED: the pad editor (cell 8 -> Custom, substate 2) re-assigns the Menu row (Right x2:
    # C2/C1CC's cycle + C2/C20F's conflict passes), Down commits (C2/C1AC), Right again, Left leaves (C2/BF6D); Up to
    # cell 7, A (the 3-option row, substate 3), Right x2 (the value = the cell - 12, C2/BFAF), A back; B, B.
    'cfgpad2': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
                P((0x200, 4), (0x80, 50), (0x80, 60), (0x80, 70), (0x100, 100), (0x80, 140), (0x20, 160), (0x20, 180),
                  (0x20, 200), (0x100, 230), (0x20, 270), (0x20, 290), (0x80, 320), (0x80, 345), (0x20, 370),
                  (0x80, 395), (0x40, 420), (0x10, 450), (0x100, 480), (0x80, 510), (0x80, 535), (0x100, 560),
                  (0x1, 600), (0x1, 640)),
                20, 690, 740),
    # L/R in the Equip GEAR PANEL (C2/A00B: R = the next member, L = the previous, wrapping over $0085): X, A (Equip),
    # A (Crono's gear), R x3, L, A (the weapon list), R, L (C2/93BF's page), B, B, B. An Iron Blade in bag slot 3 (the
    # equiplist poke) gives Crono a one-row list under the panel, so the frame C2/A45D rebuilds it after a switch shows.
    'equiplr': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                {0x2403: 0x02, 0x2503: 0x01},
                P((0x200, 4), (0x100, 60), (0x100, 100), (0x800, 140), (0x800, 170), (0x800, 200), (0x400, 230),
                  (0x100, 260), (0x800, 300), (0x400, 330), (0x1, 360), (0x1, 390), (0x1, 420)),
                20, 470, 520),
    # The Equip CANDIDATE LIST paged (C2/9F05 -> C2/93BF: L = a page on, R = a page back, clamped to the ends): twelve
    # weapons (ids 02-0D) poked into bag slots 3-14 on both sides; X, A (Equip), A (Crono), A (the weapon list), Down x2,
    # L, L, R, R, Down x9 (the one-row scroll C2/EB9B), Up, B, B, B.
    'equippage': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                  dict([(0x2403 + i, 0x02 + i) for i in range(12)] + [(0x2503 + i, 0x01) for i in range(12)]),
                  P((0x200, 4), (0x100, 60), (0x100, 100), (0x100, 150), (0x20, 200), (0x20, 220), (0x400, 250),
                    (0x400, 280), (0x800, 310), (0x800, 340), (0x20, 370), (0x20, 390), (0x20, 410), (0x20, 430),
                    (0x20, 450), (0x20, 470), (0x20, 490), (0x20, 510), (0x20, 530), (0x10, 560), (0x1, 600),
                    (0x1, 640), (0x1, 670)),
                  20, 700, 760),
    # The one-row slide both ways (C2/EB9B + C2/9F90's loop): the same twelve weapons; X, A, A, A (the list), Down x10
    # (the window two rows down), Up x10 (back past the top edge twice), then held keys are not in the grammar -- B x3.
    'equipslide': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                   dict([(0x2403 + i, 0x02 + i) for i in range(12)] + [(0x2503 + i, 0x01) for i in range(12)]),
                   P(*([(0x200, 4), (0x100, 60), (0x100, 100), (0x100, 150)]
                       + [(0x20, 200 + 12 * i) for i in range(10)] + [(0x10, 340 + 12 * i) for i in range(10)]
                       + [(0x1, 480), (0x1, 520), (0x1, 550)])),
                   20, 580, 640),
    # The ITEM list scrolled (C2/AC3A): weapons 02-1D in bag slots 3-30 (1B-1D the ROM's blank names), with a Tonic, an
    # Ether and a Heal on slots 12 / 22 / 32 -- where the cursor lands after each page, so each page frame's description
    # (skipped by C2/AC4F's BRA) is graded; X, Right (Item), A (the
    # list), Down x12 (to row 9, then the one-row slide C2/AC6E twice), L, L (C2/93BF: 10 a page), R, R, Up x12, B x3.
    'itemscroll': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                   dict([(0x2403 + i, 0x02 + i) for i in range(28)] + [(0x2503 + i, 0x01) for i in range(30)]
                        + [(0x240C, 0xBD), (0x2416, 0xC0), (0x2420, 0xC6)]),
                   P(*([(0x200, 4), (0x80, 50), (0x100, 80)]
                       + [(0x20, 130 + 12 * i) for i in range(12)]
                       + [(0x400, 290), (0x400, 320), (0x800, 350), (0x800, 380)]
                       + [(0x10, 410 + 12 * i) for i in range(12)]
                       + [(0x1, 570), (0x1, 600), (0x1, 630)])),
                   20, 660, 720),
    # The Item slide with a PICKED item (the Move pin, C2/89E4's hand riding $0D9E through the slide): the same bag; X,
    # Right, A, Down x9 (row 9), A (pick), Down x2 (two slides down with the pin), Up x12 (to row 0, then two slides
    # up), B (drop the pick), B, B, B.
    'itemscrollpick': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                       dict([(0x2403 + i, 0x02 + i) for i in range(28)] + [(0x2503 + i, 0x01) for i in range(28)]),
                       P(*([(0x200, 4), (0x80, 50), (0x100, 80)]
                           + [(0x20, 130 + 12 * i) for i in range(9)]
                           + [(0x100, 250), (0x20, 280), (0x20, 300)]
                           + [(0x10, 330 + 12 * i) for i in range(12)]
                           + [(0x1, 490), (0x1, 520), (0x1, 550), (0x1, 580)])),
                       20, 610, 670),
    # A SHOP's buy list scrolled: the fair merchant (loc 5) turned into shop $09 (16 items: its script byte $7F:2866 = 89,
    # shop_screen.md), the talk (a field A), Buy, Down x10 (the list's one-row slide C2/D84F), Up x10, L, R (C2/93BF),
    # B, B.
    'shopscroll': ('saves/field_re/shop90_lake.state', 'saves/field_re/shop90.lakewram', 5, {0x12866: 0x89},
                   P(*([(0x1100, 10), (0x100, 162)]
                       + [(0x20, 230 + 12 * i) for i in range(10)] + [(0x10, 360 + 12 * i) for i in range(10)]
                       + [(0x400, 490), (0x800, 520), (0x1, 550), (0x1, 580)])),
                   20, 610, 660),
    # The SELL list scrolled: weapons 02-0F (14) poked into bag slots 1-14; the talk, Right (Sell), A, Down x10 (C2/D90E's
    # slide), Up x10, L, R, B, B.
    'shopsell': ('saves/field_re/shop90_lake.state', 'saves/field_re/shop90.lakewram', 5,
                 dict([(0x2401 + i, 0x02 + i) for i in range(14)] + [(0x2501 + i, 0x01) for i in range(14)]),
                 P(*([(0x1100, 10), (0x80, 150), (0x100, 180)]
                     + [(0x20, 240 + 12 * i) for i in range(10)] + [(0x10, 370 + 12 * i) for i in range(10)]
                     + [(0x400, 500), (0x800, 530), (0x1, 560), (0x1, 590)])),
                 20, 620, 670),
    # The WHOLE ROSTER in the member select: $7F:0000 poked to 49 (FF/F813: $85 = $75 = 4 once the storyline reaches 49
    # with $7F:00BA bit0 clear) -- party3's reserve Marle joins the list, a 3-row window that scrolls (C2/9DAF); X, A
    # (Equip), Down x4 (to Marle, the scroll, the wrap), Up x2, A (the gear panel), R (C2/A00B over $85), B, B, B.
    'equiproster': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {0x10000: 0x49},
                    P((0x200, 4), (0x100, 60), (0x20, 110), (0x20, 140), (0x20, 170), (0x20, 200), (0x10, 230),
                      (0x10, 260), (0x100, 290), (0x800, 340), (0x1, 380), (0x1, 420), (0x1, 460)),
                    20, 500, 550),
    # The member scroll BOTH ways (C2/9DAF, $0D22 +-4): the same poke; X, A, Down x3 (the window down a member), Up x3
    # (row 0, then the window back up a member), Down (row 1), B, B.
    'equiproster2': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {0x10000: 0x49},
                     P((0x200, 4), (0x100, 60), (0x20, 100), (0x20, 125), (0x20, 150), (0x10, 190), (0x10, 215),
                       (0x10, 240), (0x20, 280), (0x1, 320), (0x1, 360)),
                     20, 400, 450),
    # The TECH screen's member select over the whole roster ($0419 / $041A, C2/BA4F): the equiproster poke; X, Right x2
    # (Tech), A, Down x4 (the window a member down: Marle), Up x3 (and back up), A (a member's list), B, B, B.
    'techroster': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {0x10000: 0x49},
                   P((0x200, 4), (0x80, 50), (0x80, 70), (0x100, 100), (0x20, 150), (0x20, 175), (0x20, 200),
                     (0x20, 225), (0x10, 260), (0x10, 285), (0x10, 310), (0x100, 340), (0x1, 380), (0x1, 420), (0x1, 460)),
                   20, 500, 550),
    # A RESERVE caster's Tech list (C2/8820: $78 = FF when $71 >= $73): Marle (in reserve) scrolled in, A (her list: the
    # field-usable techs not in their bright colour, C2/BDCF), A on a tech (refused: C2/B3EB), B x3.
    'techreserve': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {0x10000: 0x49},
                    P((0x200, 4), (0x80, 50), (0x80, 70), (0x100, 100), (0x20, 150), (0x20, 175), (0x20, 200),
                      (0x100, 240), (0x100, 280), (0x1, 320), (0x1, 360), (0x1, 400)),
                    20, 440, 490),
    # The same with Marle's first two singles LEARNED ($2838 = C0: group 1, FF/F813's $7F00 copy): Aura (usable outside
    # battle) drawn without its bright colour for a reserve caster (C2/BDCF BIT $78), and A on it refused (C2/B3EB).
    'techreserve2': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                     {0x10000: 0x49, 0x2838: 0xC0},
                     P((0x200, 4), (0x80, 50), (0x80, 70), (0x100, 100), (0x20, 150), (0x20, 175), (0x20, 200),
                       (0x100, 240), (0x100, 280), (0x1, 320), (0x1, 360), (0x1, 400)),
                     20, 440, 490),
    # A RESERVE member's gear panel: Marle scrolled into the Equip column (the equiproster poke), A (C2/A71E's entry push
    # from row 2 with the window scrolled), Down (Helmet), B (C2/A85C back to the window, C2/A2CD re-seeding its bar), B, B.
    'equipreserve': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {0x10000: 0x49},
                     P((0x200, 4), (0x100, 60), (0x20, 110), (0x20, 135), (0x20, 160), (0x100, 200), (0x20, 260),
                       (0x1, 300), (0x1, 350), (0x1, 390)),
                     20, 430, 480),
    # The SHOP's equip sub-mode list scrolled (the shop's own C2/9F05 port, C2/EB9B's slide): twelve swords (02-0D) in bag
    # slots 1-12 of the fair merchant's seeds; the talk, Down (the party row), A (Crono's gear rows), A (the weapon list),
    # Down x10, Up x10, L, R, B x4.
    'shopeqscroll': ('saves/field_re/shop90_lake.state', 'saves/field_re/shop90.lakewram', 5,
                     dict([(0x2401 + i, 0x02 + i) for i in range(12)] + [(0x2501 + i, 0x01) for i in range(12)]),
                     P(*([(0x1100, 10), (0x20, 60), (0x100, 110), (0x100, 160)]
                         + [(0x20, 210 + 12 * i) for i in range(10)] + [(0x10, 340 + 12 * i) for i in range(10)]
                         + [(0x400, 470), (0x800, 500), (0x1, 530), (0x1, 560), (0x1, 590), (0x1, 620)])),
                     20, 660, 710),
    # EVERY Config cell worked: A on cells 0-4 (Stereo, Battle Mode, the three cursor memories), Right to cell 9 and A,
    # Up x4 to cell 5, A (the Battle Speed strip, cell 10), Right x2, Down (the Message Speed strip, cell 11), Left, A
    # (back), Down (cell 6), A (the colour edit), Right, A, B, B -- then the packed $2990-92 in RAM.
    'cfgall': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
               P((0x200, 4), (0x80, 50), (0x80, 60), (0x80, 70), (0x100, 100),
                 (0x100, 130), (0x20, 155), (0x100, 180), (0x20, 205), (0x100, 230), (0x20, 255), (0x100, 280),
                 (0x20, 305), (0x100, 330), (0x80, 355), (0x100, 380), (0x10, 405), (0x10, 425), (0x10, 445),
                 (0x10, 465), (0x100, 490), (0x80, 515), (0x80, 535), (0x20, 560), (0x40, 585), (0x100, 610),
                 (0x20, 640), (0x100, 665), (0x80, 690), (0x100, 715), (0x1, 745), (0x1, 780)),
               20, 820, 880),
    # The Config's CURSOR MEMORY ($0D88, cell 2): X, R x3 (Config), A, Down x2, A (on), B (packed: $2990 bit 5), B (the
    # menu closes on the Config tab), X at 340 (C2/99D9: the ribbon reopens on $0411 = 3), B.
    'cursormem': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
                  P((0x200, 4), (0x80, 50), (0x80, 60), (0x80, 70), (0x100, 100), (0x20, 150), (0x20, 170),
                    (0x100, 200), (0x1, 240), (0x1, 270), (0x200, 340), (0x1, 400)),
                  20, 460, 500),
    # An ALL-PARTY item: a Lapis (CA, $0DBD bit 3) poked into bag slot 1; X, R (Item), A, Down, A (pick), A (the overlay:
    # C2/B19B `AND #08` -> C2/8AD5), A (used on everyone), then B out of every level.
    'itemall': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                {0x2401: 0xCA, 0x2501: 0x01},
                P((0x200, 4), (0x80, 50), (0x100, 80), (0x20, 110), (0x100, 140), (0x100, 170), (0x100, 210),
                  (0x1, 290), (0x1, 320), (0x1, 350), (0x1, 380)),
                20, 370, 460),
    # A on an EMPTY bag slot (C2/AA99 stores the cursor in $0F00 with no test of what it holds): X, R (Item), A (the list),
    # Down (slot 1, empty), A (picked), Down (slot 2), A (C2/AAD5: the swap moves slot 2's item into slot 1), B x3.
    'itemempty': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
                  P((0x200, 4), (0x80, 50), (0x100, 80), (0x20, 110), (0x100, 140), (0x20, 170), (0x100, 200),
                    (0x1, 240), (0x1, 270), (0x1, 300)),
                  20, 340, 420),
    # The same with a MAGIC Tab (CE: $0DBD = 6, FF:CF2B[6] = 03 -> +0x0E, C2:B315[6] = 05 -> +0x34) on the SECOND target:
    # Down in the overlay at 190, A on the second member at 210 (party3 = Crono, Lucca, Frog: Lucca).
    'itemtab2': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                 {0x2401: 0xCE, 0x2501: 0x02},
                 P((0x200, 4), (0x80, 50), (0x100, 80), (0x20, 110), (0x100, 140), (0x100, 170), (0x20, 190),
                   (0x100, 210), (0x1, 290), (0x1, 320), (0x1, 350), (0x1, 380)),
                 20, 370, 460),
    # A SPEED Tab (CF: $0DBD = 2, cap FF:CF32[2] = 0x10) on a Crono whose Speed (+0x0D) is poked to 0x10: C2/B275 leaves
    # `$0DBC` = 0, so the A at 210 buzzes (C2/B23D) and nothing is used; B at 250 leaves the overlay, then out.
    'itemtab3': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                 {0x2401: 0xCF, 0x2501: 0x01, 0x260D: 0x10},
                 P((0x200, 4), (0x80, 50), (0x100, 80), (0x20, 110), (0x100, 140), (0x100, 170), (0x100, 210),
                   (0x1, 250), (0x1, 290), (0x1, 320), (0x1, 350)),
                 20, 360, 440),
    # The member panel's HP (string 06): a Gold Earring WORN by Crono (+0x2A = A1: the open's C2/9137 makes +0x3F = 144,
    # C2/813C `$0D38` = 14) and his HP 5 -- the panel prints 5/144 with the 5 in C2/A1EF's palette 1 (red). X, A (Equip),
    # A (Crono), Down x3 (the accessory row), then B out.
    'hpacc': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
              {0x262A: 0xA1, 0x2603: 0x05},
              P((0x200, 4), (0x100, 60), (0x100, 90), (0x20, 130), (0x20, 150), (0x20, 170), (0x1, 210), (0x1, 240),
                (0x1, 270)),
              20, 290, 320),
    # The Equip PREVIEW's max HP (string 09 `PAL $0D57 / NUM $9B25`): a Gold Earring in bag slot 3, previewed on Crono's
    # accessory row -- 96 -> 144 in the raise colour; B out of the list and the screens.
    'hpprev': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
               {0x2403: 0xA1, 0x2503: 0x01},
               P((0x200, 4), (0x100, 60), (0x100, 90), (0x20, 130), (0x20, 150), (0x20, 170), (0x100, 200), (0x1, 260),
                 (0x1, 290), (0x1, 320), (0x1, 350)),
               20, 370, 400),
    # The Equip candidate list with something to put on: an Iron Blade poked into the bag (slot 3), A (Equip), A
    # (Crono), Down, Up (the dirty-key yield on a slot change), A (the Weapon list), Down, A (commit), B (back to
    # the member select), B, B.
    'equiplist': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                  {0x2403: 0x02, 0x2503: 0x01},
                  P((0x200, 4), (0x100, 60), (0x100, 90), (0x20, 130), (0x10, 150), (0x100, 180), (0x20, 220),
                    (0x100, 250), (0x1, 300), (0x1, 340), (0x1, 370)),
                  20, 380, 460),
    # The Equip candidate list with MORE THAN ONE of a candidate: the equiplist drive with three Iron Blades (bag slot 3,
    # id 02) and twelve Steelsabers (slot 4, id 03) poked on both sides. C2/8953 copies each count to $0F62 but C2/A569
    # / C2/A59A draw the name only (disasm/notes/menu_equip_list_no_count.md): the rows carry no number.
    'equipcount': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                   {0x2403: 0x02, 0x2503: 0x03, 0x2404: 0x03, 0x2504: 0x0C},
                   P((0x200, 4), (0x100, 60), (0x100, 90), (0x20, 130), (0x10, 150), (0x100, 180), (0x20, 220),
                     (0x100, 250), (0x1, 300), (0x1, 340), (0x1, 370)),
                   20, 380, 460),
    # ... and through the one-row slide (C2/9F84's A59A draws the incoming row, the count staged in $04CA and not
    # drawn): the equipslide drive with the twelve weapons held 2-13 each.
    'equipslidecount': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130,
                        dict([(0x2403 + i, 0x02 + i) for i in range(12)] + [(0x2503 + i, 0x02 + i) for i in range(12)]),
                        P(*([(0x200, 4), (0x100, 60), (0x100, 100), (0x100, 150)]
                            + [(0x20, 200 + 12 * i) for i in range(10)] + [(0x10, 340 + 12 * i) for i in range(10)]
                            + [(0x1, 480), (0x1, 520), (0x1, 550)])),
                        20, 580, 640),
    # The FIELD the menu opens from, left alone: no button at all, so the seeds' own background runs -- the colour math
    # (the tent keeper's BG2 shadows half-added over BG3, party3's BG3 glow added over BG1/BG2), the map the seed carries
    # (party3's open door) and the ROM's animation machines (the tent flags, FD/E022; the torches, FD/E39C mode 1).
    # disasm/notes/field_bg_anim.md.
    'tkfield': ('saves/field_re/tent_keeper_lake.state', 'saves/field_re/tent_keeper.lakewram', 5, {}, P(), 0, 90, 100),
    'p3field': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {}, P(), 0, 90, 100),
    # loc 13B's orange circuits: four FD/E292 `8x` colour records (FD/E485) cycling in the live page. The state is the
    # ROM's own load (ct.exe --teleport 315 2 8 0 from party3_field), so its VRAM and CGRAM are the cartridge's.
    'c315field': ('saves/field_re/loc13B_teleport_lake.state', 'saves/field_re/loc13B_teleport.lakewram', 315, {}, P(),
                  0, 90, 100),
    'equip2': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
               P((0x200, 4), (0x100, 60), (0x20, 90), (0x20, 110), (0x100, 140), (0x1, 190), (0x1, 240), (0x1, 270)),
               20, 300, 320),
    # The OPENED-DOOR LIST across a menu close: X, then B straight back out. party3's right-hand door is open in the seed
    # (its tile index $2935 is the one entry of $7F:1CC8); the reload re-decompresses the map with the door closed
    # (C0/01A5 -> C0/A33B) and C0/286C's first JSR, C0/28E1, opens it again from the list (the $FA arm, C0/297D).
    'doorreload': ('saves/battle_re/party3_field.state', 'saves/battle_re/party3_field.lakewram', 130, {},
                   P((0x200, 4), (0x1, 60)), 0, 150, 160),
}


# a drive's extra environment for the PORT side (the oracle's own is the drive's pokes): the shop number the port's
# test hook takes where the cartridge reads its script byte (shop_screen_cmp.py does the same)
PORT_ENV = {'shopscroll': {'CT_SHOPNO': '09'}}


def oracle_script(presses, total):
    parts, t = [], 0
    for m, f in presses:
        if f >= total:
            break
        if f > t:
            parts.append('0:%d' % (f - t))
        n = min(2, total - f)
        parts.append('%X:%d' % (m & 0xFFF, n))              # (0x1100, the field A, is A to the oracle)
        t = f + n
    if total > t:
        parts.append('0:%d' % (total - t))
    return ','.join(parts)


def run_oracle(d, out, thumbs=None, total=None):
    state, _, _, pokes, presses, f0, f1, end = d
    env = dict(os.environ, CT_ORACLE_LOGICAL='1')
    if pokes:
        env['PEEK_POKE'] = ','.join('%04X=%02X' % kv for kv in pokes.items())
    if thumbs:
        env.update(PEEK_THUMBS=thumbs, PEEK_THUMBS_EVERY='1')
    subprocess.run([CT, '--peek', state, out, '--btnscript', oracle_script(presses, total or end)], cwd=ROOT,
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run_port(d, outdir, thumbs, shots=(), dump_at=None, name=None):
    _, seed, scene, pokes, presses, f0, f1, end = d
    b = bytearray(open(os.path.join(ROOT, seed), 'rb').read())
    o = b.index(b'RAM:131072:') + 11
    for a, v in pokes.items():
        b[o + a] = v
    sp = os.path.join(outdir, '_seed.lakewram')
    open(sp, 'wb').write(bytes(b))
    walk, t, hold = [], 0, []
    for m, f in presses:
        if m in HOLD:
            hold.append('%X@%d:1' % (HOLD[m], f + MENU_PRESS))
            continue
        pf = f + (1 if m in (0x1100, 0x200) else MENU_PRESS)   # the cartridge reads it a frame later (the menu: see
        # MENU_PRESS); a field talk too, as every field key (ct-frame-alignment) -- until 2026-10-19 the talk took the
        # cartridge's own frame, which the port's C8 fade, then a frame late, hid
        if pf > t:
            walk.append('n:%d' % (pf - t))
        walk.append('%s:1' % KEY[m])
        t = pf + 1
    walk.append('n:%d' % max(1, end - t))
    # the port's battery: an ERASED cart in the drive's own file (CT_SRAM_FRESH ignores what is on disk) -- a drive that
    # saves (owsavecol) must neither read nor overwrite the player's ct_sram.srm in the repo root, and must be repeatable
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', CT_NOPACE='1',
               CT_SHOTS_THUMBS=thumbs, CT_SHOTS_THUMBS_EVERY='1', CT_HOLDBTN=','.join(hold),
               CT_SRAM=os.path.join(outdir, '_p.srm'), CT_SRAM_FRESH='1')
    env.update(PORT_ENV.get(name, {}))
    if shots:
        env.update(CT_SHOTS=os.path.join(outdir, '_p'), CT_SHOTS_AT=','.join(str(f) for f in shots))
    if dump_at is not None:                             # the engine's WRAM at that frame (--ram)
        env.update(CT_DUMPWRAM=os.path.join(outdir, '_p_ram.wram'), CT_DUMPWRAM_AT=str(dump_at))
    subprocess.run([FIELD, '--assets', 'ct_assets.dat', '--scene', str(scene), '--loadwram', sp, '--selftest',
                    os.path.join(outdir, '_p_end.raw'), '--frames', str(end), '--walkscript', ','.join(walk)],
                   cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def load_thumbs(path):
    data = open(path, 'rb').read() if os.path.exists(path) else b''
    out = {}
    for i in range(len(data) // REC):
        fr, = struct.unpack_from('<I', data, i * REC)
        out[fr] = data[i * REC + 6:(i + 1) * REC]
    return out


def bbox(a, b):
    xs, ys = [], []
    for i in range(0, len(a), 3):
        if a[i:i + 3] != b[i:i + 3]:
            p = i // 3
            xs.append(p % 128)
            ys.append(p // 128)
    return (min(xs) * 2, min(ys) * 2, max(xs) * 2 + 1, max(ys) * 2 + 1) if xs else None


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'tour'
    d = DRIVES[name]
    f0, f1 = d[5], d[6]
    if '--window' in sys.argv:
        i = sys.argv.index('--window')
        f0, f1 = int(sys.argv[i + 1]), int(sys.argv[i + 2])
    peeks = []
    if '--peek' in sys.argv:
        for a in sys.argv[sys.argv.index('--peek') + 1:]:
            if a.startswith('--'):
                break
            peeks.append(int(a))
    outdir = os.path.join(ROOT, 'build', '_menuaudit', name)
    if '--window' in sys.argv:                          # the suite runs one drive under several windows at once
        outdir = os.path.join(outdir, 'w%d-%d' % (f0, f1))
    os.makedirs(outdir, exist_ok=True)
    ot, pt = os.path.join(outdir, 'o.thumbs'), os.path.join(outdir, 'p.thumbs')
    rams = []
    if '--ram' in sys.argv:                             # ADDR:LEN,... compared at the drive's end, both sides
        for spec in sys.argv[sys.argv.index('--ram') + 1].split(','):
            a, n = spec.split(':')
            rams.append((int(a, 16), int(n)))
    run_oracle(d, os.path.join(outdir, '_o_end.raw'), ot)
    run_port(d, outdir, pt, peeks, dump_at=(d[7] - 2) if rams else None, name=name)
    o, p = load_thumbs(ot), load_thumbs(pt)
    bad = []
    for f in range(f0, f1 + 1):
        if f not in p or f + SHIFT not in o:
            bad.append((f, 'missing'))
        elif p[f] != o[f + SHIFT]:
            bad.append((f, bbox(o[f + SHIFT], p[f])))
    print('menu-audit: %s f%d-f%d: %d of %d frame(s) differ' % (name, f0, f1, len(bad), f1 - f0 + 1))
    run = None
    for f, bb in bad + [(None, None)]:
        if run and f == run[1] + 1 and bb == run[2]:
            run[1] = f
            continue
        if run:
            print('  f%d-f%d box %s' % (run[0], run[1], run[2]))
        run = [f, f, bb] if f is not None else None
    if rams:                                            # the oracle's --peek of `end` frames vs the port's frame end-2
        ow = open(os.path.join(outdir, '_o_end.raw.wram'), 'rb').read()
        pw = open(os.path.join(outdir, '_p_ram.wram'), 'rb').read()
        for a, n in rams:
            same = ow[a:a + n] == pw[a:a + n]
            print('menu-audit: ram $%04X:%d oracle %s port %s -> %s' % (a, n, ow[a:a + n].hex(), pw[a:a + n].hex(),
                                                                       'same' if same else 'DIFFER'))
    for f in peeks:
        of = os.path.join(outdir, '_o_%d.raw' % f)
        run_oracle(d, of, None, f + SHIFT)
        pf = os.path.join(outdir, '_p_f%d.raw' % f)
        if not os.path.exists(pf):
            print('menu-audit: peek f%d -- the port frame was not written' % f)
            continue
        ow, oh, opx = fb.load(of)
        pw, ph, ppx = fb.load(pf)
        n = sum(1 for y in range(224) for x in range(256)
                if opx[((y + 8) * ow + x) * 3:((y + 8) * ow + x) * 3 + 3] != ppx[(y * pw + x) * 3:(y * pw + x) * 3 + 3])
        if '--tbframe' in sys.argv:                     # the bottom dialogue box's frame only
            tn = tt = 0
            for y in range(131, 211):
                for x in range(256):
                    if not (y < 141 or y >= 203 or x < 8 or x >= 248):
                        continue
                    tt += 1
                    if opx[((y + 8) * ow + x) * 3:((y + 8) * ow + x) * 3 + 3] != ppx[(y * pw + x) * 3:(y * pw + x) * 3 + 3]:
                        tn += 1
            print('menu-audit: tbframe f%d -> %d of %d px differ' % (f, tn, tt))
        fb.fb_to_png(of, os.path.join(outdir, 'o_%d.png' % f))
        fb.fb_to_png(pf, os.path.join(outdir, 'p_%d.png' % f))
        print('menu-audit: peek f%d -> %d px differ' % (f, n))


if __name__ == '__main__':
    main()
