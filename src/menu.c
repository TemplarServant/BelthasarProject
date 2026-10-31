// menu.c -- the menu program (bank C2): the X-button menu and its sub-screens, the save and load screens, the
// shop and the exchange (menu.h; disasm/notes/menu_entry.md and the menu2_*.md dossiers).
//
// The menu's data layer reads and writes the ROM's own RAM; the pictures are composed from captured chrome and the
// host's font and sprite hooks.
#include <string.h>
#include "romview.h"
#include <stdio.h>
#include <stdlib.h>   // atoi (config number-strip values)
#include "menu.h"
#include "lz.h"            // ct_lz_decompress_bounded: the exchange's BG3 characters (FF/F677's C3/0002)
#include "scene.h"         // CT_SPR_OX / CT_SPR_OY: the sprite composer's canvas origin (the exchange's figure)
#include "vwf.h"           // CtVwfBegin / CtVwfRun: C2/57DF / C2/5823, the exchange prompt's proportional text
#include "tables/menu_icons.h"   // captured OBJ ribbon icons (6 base + 6 selected-highlight, 16x16 RGBA)
#include "tables/menu_chrome.h"  // BG2 chrome: marble desktop + dark-blue window panels + ribbon box
#include "tables/menu_shop.h"    // the SHOP screen ($0C): its BG2 chrome, narrow digits and text palettes (captured)
#include "tables/menu_loader.h"  // the LOAD screen (mode 0 / screen 0x0E): its BG2 plane + the two ROM
                               //   string tables it shows (chapter title, era name)
#include "tables/menu_wingrad.h"  // the ROM's window-gradient lists ($FF:BBF2), run through C2/86DD below
#include "tables/menu_desc.h"   // the ROM's stored item/equip description strings (CC/2EB1, C2/F2F3)
#include "tables/menu_arrow.h"   // the ribbon selection cursor arrow (OBJ tile 0x22, 16x16, pal7)
#include "tables/menu_saveform.h" // Save-screen (opt5) BG palette: the slot buttons' grey-marble CGRAM
#include "tables/menu_opt0.h"     // opt0 Equip/Status detail: element plaques + 48x48 portrait + equip icons
#include "tables/menu_configgrid.h" // Config screen icon grid: 10 setting icons + checkboxes + speed faces (RGBA)
#include "tables/menu_itemoverlay.h" // the Item target overlay's BG2 window cells ($FF:C0A6, captured from VRAM)
#include "tables/menu_itemdetail.h" // Item screen: the grey selection hand cursor (OBJ tile 0x20, pal0, RGBA)
#include "tables/menu_tech.h"     // Tech screen Single/Dual/Triple category tabs (BG1 tiles 0x164-0x16F)
#include "tables/menu_romdata.h" // baked menu ROM data (item/tech names, tech tables, config, categories)
#include "tables/menu_itemuse.h" // baked item-menu Use/Organize data ($7700 props + $CC effect tables)
#include "tables/menu_equip.h"   // baked equip data: the $7A00/$7B00/$7C00 item pages + the bonus/stat tables
#include "tables/menu_techrom.h" // baked tech data: the 37-group boundary/mask tables + the MP index page
#include "ct_vm.h"   // CtGetName (the engine's $7E2C23 name-buffer analogue)

// Byte at a ROM file offset, served from the baked menu_romdata.h tables (item/tech names, tech range/MP/maxcnt,
// config), so the menu needs no runtime ROM. Offsets outside the baked ranges return 0.
static uint8_t mnuRom(long off) {
  if (off >= (long)CTROMCC_BASE     && off < (long)CTROMCC_BASE     + (long)sizeof ctRomCC)     return ctRomCC[off - (long)CTROMCC_BASE];
  if (off >= (long)CTROMC2_BASE     && off < (long)CTROMC2_BASE     + (long)sizeof ctRomC2)     return ctRomC2[off - (long)CTROMC2_BASE];
  if (off >= (long)CTROMCFG_BASE    && off < (long)CTROMCFG_BASE    + (long)sizeof ctRomCfg)    return ctRomCfg[off - (long)CTROMCFG_BASE];
  if (off >= (long)CTROMMAXCNT_BASE && off < (long)CTROMMAXCNT_BASE + (long)sizeof ctRomMaxCnt) return ctRomMaxCnt[off - (long)CTROMMAXCNT_BASE];
  if (off >= (long)CTITEMNAV_BASE     && off < (long)CTITEMNAV_BASE     + (long)sizeof ctItemNav)     return ctItemNav[off - (long)CTITEMNAV_BASE];
  if (off >= (long)CTCFGNAV_BASE      && off < (long)CTCFGNAV_BASE      + (long)sizeof ctCfgNav)      return ctCfgNav[off - (long)CTCFGNAV_BASE];
  if (off >= (long)CTCFGDEFAULTS_BASE && off < (long)CTCFGDEFAULTS_BASE + (long)sizeof ctCfgDefaults) return ctCfgDefaults[off - (long)CTCFGDEFAULTS_BASE];
  return 0;
}
// An equipped item's menu icon is byte 0 of its $CC:0B5E name record (0x20 sword .. 0x29 accessory), so the weapon
// slot shows the weapon type. Returns the 8x8 icon tile (pal0), or NULL for an unknown byte.
static const uint8_t* mEquipIcon(int itemId) {
  int t = mnuRom((long)CTROMCC_BASE + (long)itemId * 11);   // 11-byte stride; byte 0 = icon tile id
  return (t >= 0x20 && t <= 0x29) ? gen_opt0_icon_tiles[t - 0x20] : NULL;
}

#define MW 256
#define MH 224

// Alpha-blit an [w x hh] RGBA8888 bitmap (defined with the party sprites).
static void mBlitRGBA(uint8_t* fb, const unsigned char* rgba, int w, int hh, int x0, int y0);

// ---- BG2 chrome (menu_chrome.h) ----
// The menu background is the mode-1 BG2 layer: a grey marble desktop, dark-blue 9-slice window panels and the
// ribbon box, captured from the oracle (tiles 0x300-0x3EF, CGRAM palettes, the per-screen tilemaps) and replayed.
// mode: 0 = marble + panels without the ribbon-box tiles (content draws between, the box goes on top later);
//       1 = only the ribbon box; 2 = everything.
// Char-select context: the party ($2980) and the selected char, so the chrome tones the party boxes by selection
// (C2/9E76, mChromeApplyCharSel). (NULL, -1) = no toning.
static const uint8_t* g_charSelMem = NULL;
static int g_charSelChar = -1;
void CtMenuSetCharSel(const uint8_t* mem, int selChar) { g_charSelMem = mem; g_charSelChar = selChar; }
// The member window (the Equip screen, C2/A051 / C2/A1B2): its three panels are C2/816A's roster list from the scroll
// $0413 on -- $0D5F[$0413 + row] the charId, $0D68[..] its $2980 slot -- over the list's length $85 (FF/F813: the
// active party, or the whole roster from storyline 49). -1 = the three active slots in order (the screens whose own
// window is not ported: the Tech screen's $041A).
static int mPanelScroll = -1;
void CtMenuSetPanelWindow(int scroll) { mPanelScroll = scroll; }
static int mPanelRowSlot(const uint8_t* mem, int row) {
  if (mPanelScroll < 0) return row;
  int i = mPanelScroll + row;
  if (!mem || i < 0 || i >= mem[0x0085] || i >= 9 || mem[0x0D5F + i] == 0xFF) return -1;
  return mem[0x0D68 + i];
}
static int mRowChar(const uint8_t* mem, int row, int fallback) {
  int sl = mPanelRowSlot(mem, row);
  return (mem && sl >= 0 && sl < 9) ? (mem[0x2980 + sl] & 7) : fallback;
}
// The member panel the cursor is on ($0412): the window row holding the selected character (with no window, the first
// of $2980..$2982).
static int mCharSelSlot(void) {
  if (!g_charSelMem) return -1;
  for (int k = 0; k < 3; k++) {
    int sl = mPanelRowSlot(g_charSelMem, k);
    if (sl < 0 || sl > 8) continue;
    int e = g_charSelMem[0x2980 + sl];
    if (!(e & 0x80) && (e & 7) == g_charSelChar) return k;
  }
  return -1;
}
// C2/9E76, the member column's selection: for panels k = 0..2, C2/EC93 over six rows from 2 + 6k and twelve columns
// from 2, on BG1 ($2E84 + 0x180k) and BG2 (+$1000): attr = (attr & 0F) | (k == $0412 ? 00 : 10). The cursor's panel
// keeps its palette, the others take palette | 4 (the dim CGRAM rows); the box's right frame column (14) is outside
// the twelve and stays.
static const uint16_t* mChromeApplyCharSel(const uint16_t* tm, uint16_t* scratch, int selSlot) {
  memcpy(scratch, tm, sizeof(uint16_t) * CT_MENU_CHROME_TMW * CT_MENU_CHROME_TMH);
  for (int k = 0; k < 3; k++)
    for (int r = 2 + 6 * k; r < 8 + 6 * k && r < CT_MENU_CHROME_TMH; r++)
      for (int c = 2; c < 14; c++) {
        uint16_t w = scratch[r * CT_MENU_CHROME_TMW + c];
        int attr = ((w >> 8) & 0x0F) | (k == selSlot ? 0x00 : 0x10);             // C2/ECC2 AND #0F / ORA
        scratch[r * CT_MENU_CHROME_TMW + c] = (uint16_t)((w & 0x00FF) | (attr << 8));
      }
  return scratch;
}
// SNES 5-bit -> 8-bit channel expand (c8 = (c5<<3)|(c5>>2)).
static int mExp5(int c5) { if (c5 < 0) c5 = 0; if (c5 > 31) c5 = 31; return (c5 << 3) | (c5 >> 2); }

// ---- The window gradient (C2/86DD; disasm/notes/menu2_window_gradient.md) ----
// The dark windows are shaded by an HDMA channel over $2131/$2132 (CGADSUB + COLDATA): a grey fixed colour added on
// the way down each window and subtracted on the way out, one 16-step ramp +8..-7 in 5-bit colour, restarted and
// height-normalised per window (the status screen: one 192 px window, 12 lines a step; the Form screen: four 48 px
// windows, 3 lines a step from lines 16/64/112/160).
// C2/86DD walks a window list of [kind, height] pairs (0x80 = end; kind 0 = a flat band, 1 = a ramp) and emits the
// HDMA line counts: $A019 = height * 16; per step $A01B += $A019, lines = hi($A01B), $A01C = 0, so the remainder is
// carried. The lists are ROM data at $FF:BBF2 (ctMenuWinList, per ribbon option).
static void mWinRamp(const uint8_t* lst, int8_t* d5) {
  int y = 0, acc = 0;
  for (int i = 0; i < 16 && y < MH; i += 2) {
    if (lst[i] & 0x80) break;                        // C2/871A BMI -- the one-byte terminator
    int h = lst[i + 1];
    if (lst[i] == 0) {                               // C2/875B: a flat band of `h` lines
      for (int k = 0; k < h && y < MH; k++) d5[y++] = 0;
    } else {
      int step = h * 16;                             // C2/8720: $A019 = height * 16
      for (int s = 0; s < 16; s++) {
        acc = (acc + step) & 0xFFFF;                 // C2/8733: $A01B += $A019
        int n = acc >> 8;                            // C2/873D: the line count is the integer part
        acc &= 0x00FF;                               // C2/8743: STZ $A01C -- keep the fraction
        for (int k = 0; k < n && y < MH; k++) d5[y++] = (int8_t)(8 - s);
      }
    }
  }
  while (y < MH) d5[y++] = 0;
}
// The ramp for one ribbon option, built on demand and memoised.
const int8_t* mWinGrad(int opt) {
  static int8_t cache[CT_MENU_WINLIST_N][MH];
  static int built[CT_MENU_WINLIST_N];
  if (opt < 0) opt = 0; else if (opt > CT_MENU_WINLIST_N - 1) opt = CT_MENU_WINLIST_N - 1;
  if (!built[opt]) { mWinRamp(ctMenuWinList[opt], cache[opt]); built[opt] = 1; }
  return cache[opt];
}
// Public, for --wingradtest (graded against the bands `ct.exe --hdma` reads off the oracle).
const int8_t* CtMenuWinGradRamp(int opt) { return mWinGrad(opt); }
// C2/CC4F's per-panel dim, applied to the captured chrome:
//     2CC78  LDA $0F02,X / AND #00C0 / BEQ ->next     ; locked (0x40) or empty (0x80)
//     2CC80  TXA / JSR C2/CCB3 / LDX $04 / STX $61
//     2CC88  LDX #0006 / LDA $02 / JSR C2/EC93        ; six tile rows (C2/EC93 adds 0x40 per pass)
//     2CC90  LDA $04 / ADC #1000 / STA $61 / (again)  ; the map's other 32x32 page, not modelled
//     2CCA0  LDA $04 / ADC #0180 / STA $04            ; 0x180 bytes = 6 rows = the next panel
// C2/ECC2 writes only the attribute byte, so a dim panel differs from a bright one by the palette nibble alone. Panel
// r covers tile rows 2 + 6r .. 7 + 6r, columns 8..23. The ribbon capture came from a two-member party (panel 2 dim),
// so the live array decides instead. Only the Form views run it (C2/CA3F, the screen and its ribbon hover);
// Equip/Status tones its panels through the char select, and Tech dims nothing.
static const uint8_t* g_formPanelDim = 0;   /* C2/CD2B's $7E:0F02 array, or NULL = leave the map alone */
void CtMenuSetFormPanelDim(const uint8_t* panel9) { g_formPanelDim = panel9; }
// Rewrite the three panel blocks' palette from the live entries. Returns `tm` unchanged when there is nothing to do.
static const uint16_t* mChromeApplyPanelDim(const uint16_t* tm, uint16_t* scratch) {
  if (!g_formPanelDim) return tm;
  memcpy(scratch, tm, sizeof(uint16_t) * CT_MENU_CHROME_TMW * CT_MENU_CHROME_TMH);
  for (int r = 0; r < 3; r++) {
    int dim = (g_formPanelDim[r] & 0xC0) != 0;                 // 2CC7B AND #00C0
    for (int ty = 2 + 6 * r; ty < 8 + 6 * r && ty < CT_MENU_CHROME_TMH; ty++)
      for (int tx = 8; tx <= 23; tx++) {
        uint16_t* e = &scratch[ty * CT_MENU_CHROME_TMW + tx];
        int t = *e & 0x3FF;
        // Only the window's own tiles: the ribbon box (0x3A4-0x3AC) and the marble are laid over this map afterwards
        // and keep their palettes (C2/ECC2 writes the attribute blindly; the ROM restores the box on top).
        if (!((t >= 0x3DC && t <= 0x3EF) || (t >= 0x300 && t <= 0x313))) continue;
        *e = (uint16_t)((*e & ~0x1C00) | (dim ? (4 << 10) : 0));
      }
  }
  return scratch;
}
// The ribbon's colour window 2. The ribbon's builder (C2/9B92: $FF:BD2E programs HDMA channel 1, mode 4, onto
// $2126-$2129 from $9080; the NMI thunk copies $9750 there) keeps colour math off inside window 2 -- the command box,
// x 64..191 on lines 96..143 -- through the table at FF/BAAA ([lines, WH0, WH1, WH2, WH3] from line 0; the colour
// window is W1 XOR W2). A screen whose build leaves $0D13 alone keeps that channel for its build frame (Config: C2/BF3D
// -> C2/C3B5; $0D13 is first written by substate 1, C2/BF47), so its window tiles under the box draw without the ramp
// that one frame. disasm/notes/menu2_config_toggles.md.
static int mRibbonW2On;
static int16_t mRibbonW2[MH][2];
void CtMenuChromeRibbonWindow(const CtMenuHost* h, int on) {
  mRibbonW2On = 0;
  if (!on || !h || !h->rom || h->romLen < 0x3FBAAAL + 64) return;
  for (int y = 0; y < MH; y++) { mRibbonW2[y][0] = 1; mRibbonW2[y][1] = 0; }
  long a = 0x3FBAAAL; int line = 0;
  for (int g = 0; g < 16 && CtRomByte(h->rom, 0x400000L, (long)(a)); g++, a += 5) {
    int n = CtRomByte(h->rom, 0x400000L, (long)(a)) & 0x7F, l = CtRomByte(h->rom, 0x400000L, (long)(a + 3)), r = CtRomByte(h->rom, 0x400000L, (long)(a + 4));
    for (int k = 0; k < n && line < MH; k++, line++) { mRibbonW2[line][0] = (int16_t)l; mRibbonW2[line][1] = (int16_t)r; }
  }
  mRibbonW2On = 1;
}
// The window colour (Config's cell 6, and every open: C2/983F `LDA $2991 / AND #07 / STA $0D8C / JMP C2/C511`).
// C2/C511 MVNs 0x281 bytes from FF/(9E10 + v * 0x280) to $7E:8800; C2/962E makes plane 3 of each of the 20 tiles the
// OR of planes 0-2 (every inked pixel takes colours 8-15); the display list C2/C548 (`C0 3D 00 88 7E 80 02`) uploads
// them to VRAM word 3DC0 -- tiles 0x3DC-0x3EF, the window's frame and fill. C2/ED08 then MVNs FF/(B210 + v * 16) onto
// CGRAM row ($0D48 & 1C) / 4, colours 8-15, and C2/9839 `LDX #0010 / STX $0D47` makes that row 0. The captures are
// colour 0.
// The dim row is separate: C2/A0E7 (`LDA $9480,X / LSR A / AND #3DEF / STA $0000,Y`, X = 0..0x30, Y = $9500) halves
// CGRAM row 0 and colours 0-7 of row 1 into row 4 / the first half of row 5. Only four builders run it: Equip
// (C2/A0E2), the Form tab (C2/CA27), the Form screen (C2/CB71) and the shop (C2/DB46), the views whose maps use
// palette 4. So row 4's colours 8-15 are half of the colour of the last A0E7 (`dimV`, tracked by the caller); its
// colours 0-7 and row 5's are halves of fixed colours, already in the captures. disasm/notes/menu2_config_toggles.md.
static int mWinLoaded, mWinColour = -1, mWinDim = -1;   // tiles 0x3DC-0x3EF + row 0's colours 8-15 = the ROM's colour
static uint8_t mWinTile[20][64], mWinRow[8][3], mWinDimRow[8][3];
void CtMenuWindowColourLoad(const CtMenuHost* h, int v, int dimV) {
  v &= 7;
  if (!h || !h->rom || h->romLen < 0x3FB210L + 0x80) { mWinLoaded = 0; mWinColour = mWinDim = -1; return; }
  if (dimV >= 0 && (dimV & 7) != mWinDim) {                                  // C2/A0E7 over FF/B210's words
    const uint8_t* c = CtRomPtr(h->rom, h->romLen, 0x3FB210L + (long)(dimV & 7) * 16, 16);
    for (int i = 0; i < 8; i++) {
      int w = ((c[2 * i] | (c[2 * i + 1] << 8)) >> 1) & 0x3DEF;
      mWinDimRow[i][0] = (uint8_t)mExp5(w & 31); mWinDimRow[i][1] = (uint8_t)mExp5((w >> 5) & 31);
      mWinDimRow[i][2] = (uint8_t)mExp5((w >> 10) & 31);
    }
  }
  mWinDim = dimV < 0 ? -1 : (dimV & 7);
  if (mWinLoaded && v == mWinColour) return;
  const uint8_t* t = CtRomPtr(h->rom, h->romLen, 0x3F9E10L + (long)v * 0x280, 0x280);
  for (int k = 0; k < 20; k++)
    for (int y = 0; y < 8; y++) {
      int p0 = t[k * 32 + 2 * y], p1 = t[k * 32 + 2 * y + 1], p2 = t[k * 32 + 16 + 2 * y];
      int p3 = p0 | p1 | p2;                                                // C2/962E
      for (int x = 0; x < 8; x++) {
        int b = 7 - x;
        mWinTile[k][y * 8 + x] = (uint8_t)(((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) | (((p2 >> b) & 1) << 2)
                                           | (((p3 >> b) & 1) << 3));
      }
    }
  const uint8_t* c = CtRomPtr(h->rom, h->romLen, 0x3FB210L + (long)v * 16, 16);                     // C2/ED08
  for (int i = 0; i < 8; i++) {
    int w = c[2 * i] | (c[2 * i + 1] << 8);
    mWinRow[i][0] = (uint8_t)mExp5(w & 31); mWinRow[i][1] = (uint8_t)mExp5((w >> 5) & 31);
    mWinRow[i][2] = (uint8_t)mExp5((w >> 10) & 31);
  }
  mWinLoaded = 1; mWinColour = v;
}
// A chrome tile's pixels / a menu CGRAM colour, through the loaded window colour.
static const uint8_t* mChromeGlyph(int ti) {
  int w = ti - (0x3DC - CT_MENU_CHROME_TILE0);
  return (mWinLoaded && w >= 0 && w < 20) ? mWinTile[w] : ctMenuChromeTile[ti];
}
// The Save slots in their own colours. The open's graphics build lays all eight colour sets as tiles 0x300-0x39F
// (C2/9724: FF/9E10, 0x1400 bytes, C2/962E's plane 3 over 0xA4 tiles); C2/D266 reads each valid save's window colour
// (`LDA $300591,X / AND #07 / STA $0D79,Y`, the save's own $2991); C2/D329 draws the slot with `$0D8C` = that colour:
// C2/ECDB -> C2/EDF6's tile base `$0D47` = colour * 20 (C2/ECE4), palette row slot + 1 (`$0D48` = ($79 + 1) * 4 | 3),
// and C2/ED08 puts FF/B210 + colour * 16 on that row's colours 8-15. An empty slot keeps 0. So a slot's buttons are
// tiles 0x300 + 20 v and row (slot + 1)'s colours 8-15 of colour v.
static int mSlotColour[3];
static uint8_t mSlotTile[8][20][64], mSlotRow[8][8][3];
static int mSlotSetOk[8];
static void mSlotSetLoad(const CtMenuHost* h, int v) {
  if (v <= 0 || v > 7 || mSlotSetOk[v] || !h || !h->rom || h->romLen < 0x3FB210L + 0x80) return;
  const uint8_t* t = CtRomPtr(h->rom, h->romLen, 0x3F9E10L + (long)v * 0x280, 0x280);
  for (int k = 0; k < 20; k++)
    for (int y = 0; y < 8; y++) {
      int p0 = t[k * 32 + 2 * y], p1 = t[k * 32 + 2 * y + 1], p2 = t[k * 32 + 16 + 2 * y], p3 = p0 | p1 | p2;
      for (int x = 0; x < 8; x++) {
        int b = 7 - x;
        mSlotTile[v][k][y * 8 + x] = (uint8_t)(((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) | (((p2 >> b) & 1) << 2)
                                               | (((p3 >> b) & 1) << 3));
      }
    }
  const uint8_t* c = CtRomPtr(h->rom, h->romLen, 0x3FB210L + (long)v * 16, 16);
  for (int i = 0; i < 8; i++) {
    int w = c[2 * i] | (c[2 * i + 1] << 8);
    mSlotRow[v][i][0] = (uint8_t)mExp5(w & 31); mSlotRow[v][i][1] = (uint8_t)mExp5((w >> 5) & 31);
    mSlotRow[v][i][2] = (uint8_t)mExp5((w >> 10) & 31);
  }
  mSlotSetOk[v] = 1;
}
void CtMenuSaveSlotColours(const CtMenuHost* h, const int* colour3) {
  for (int i = 0; i < 3; i++) {
    int v = 0;
    if (colour3) v = colour3[i] & 7;
    else if (h && h->sram && CtMenuSaveSlotValid(h->sram, h->sramLen, i)) v = h->sram[CtMenuSaveSlotBase(i) + 0x0591] & 7;
    mSlotColour[i] = v;
    mSlotSetLoad(h, v);
  }
}
static const uint8_t* mSlotGlyph(const uint8_t (*pal)[16][3], int ti, int pl) {
  int k = ti - (0x300 - CT_MENU_CHROME_TILE0);
  if ((pal != ctMenuChromeSavePal && pal != ctMenuChromeLoaderPal) || k < 0 || k >= 20 || pl < 1 || pl > 3) return NULL;
  int v = mSlotColour[pl - 1];
  return (v > 0 && mSlotSetOk[v]) ? mSlotTile[v][k] : NULL;
}
static const uint8_t* mChromeInk(const uint8_t (*pal)[16][3], int pl, int v) {
  if (v >= 8 && (pal == ctMenuChromeSavePal || pal == ctMenuChromeLoaderPal) && pl >= 1 && pl <= 3) {   // a slot's
                                                                            //   own colour (C2/ED08; the boot Load screen too)
    int c = mSlotColour[pl - 1];
    if (c > 0 && mSlotSetOk[c]) return mSlotRow[c][v - 8];
  }
  if (v >= 8 && (pal == ctMenuChromePal || pal == ctMenuChromeSavePal)) {
    if (mWinLoaded && pl == 0) return mWinRow[v - 8];
    if (mWinDim >= 0 && pl == 4) return mWinDimRow[v - 8];
  }
  return pal[pl][v];
}
// The windowed half colour math (`math=07 half=1` inside W1 = 16..239): on a line of this band the PPU adds the fixed
// colour to the layer's colour, clamps at 0, halves, and only then caps at 31 -- one ALU pass, so a white border (31)
// + 8 reads 19, not the 15 that halving the capped sum gives. The boot Load screen's scroll chrome and the X menu's
// Save list use it.
static uint8_t mHalfBand[MH];
static uint8_t* mChromeZero;   // when set, mChromeTMPal2L marks the pixels its tiles leave at colour 0 (the layers below show)
static void mChromeTMPal2L(uint8_t* fb, const uint16_t* tm, int mode, const uint8_t (*pal)[16][3],
                           const int8_t* d5, int csTone) {
  // d5 = the per-line 5-bit COLDATA delta mWinGrad() generated for this screen's window list.
  // csTone is unused (the member boxes' tone comes from C2/9E76's palette rows).
  (void)csTone;
  if (!d5) d5 = mWinGrad(0);
  for (int ty = 0; ty < CT_MENU_CHROME_TMH; ty++)
    for (int tx = 0; tx < CT_MENU_CHROME_TMW; tx++) {
      uint16_t e = tm[ty * CT_MENU_CHROME_TMW + tx];
      int tile = e & 0x3FF, pl = (e >> 10) & 7;
      int isBox = (tile >= 0x3A4 && tile <= 0x3AC);    // the 9 ribbon-box pieces
      if (mode == 0 && isBox) continue;
      if (mode == 1 && !isBox) continue;
      int ti = tile - CT_MENU_CHROME_TILE0;
      if (ti < 0 || ti >= CT_MENU_CHROME_NTILE) continue;
      const uint8_t* g = mSlotGlyph(pal, ti, pl);
      if (!g) g = mChromeGlyph(ti);
      // The whole dark window -- the frame bevel (0x3DC-0x3E7), the panel fill (0x3E8-0x3EF) and the Save buttons
      // (0x300-0x313) -- takes the colour-math ramp (mWinGrad); the marble and the ribbon box stay flat (the ROM
      // keeps them outside the colour window, HDMA ch1 -> $2126).
      int isWin  = (tile >= 0x3DC && tile <= 0x3EF) || (tile >= 0x300 && tile <= 0x313);
      for (int yy = 0; yy < 8; yy++)
        for (int xx = 0; xx < 8; xx++) {
          int v = g[yy * 8 + xx], px = tx * 8 + xx, py = ty * 8 + yy;
          if (px < MW && py < MH) {                    // index 0 = backdrop black (opaque background)
            if (mChromeZero) mChromeZero[py * MW + px] = (uint8_t)(v == 0);
            const uint8_t* C = mChromeInk(pal, pl, v);
            int r = C[0], gg = C[1], b = C[2];
            if (isWin && !(mRibbonW2On && px >= mRibbonW2[py][0] && px <= mRibbonW2[py][1])) {
              // COLDATA carries a 5-bit intensity that CGADSUB adds to or subtracts from the 5-bit palette colour:
              // conv5(clamp(v5 + d5)). Palette entries are stored as conv5(v5), so >> 3 recovers v5.
              int dd = d5[py];
              int c0 = (r >> 3) + dd, c1 = (gg >> 3) + dd, c2 = (b >> 3) + dd;
              if (mHalfBand[py] && px >= 16 && px <= 239) {  // clamp at 0, halve, then cap (the band's order)
                c0 = (c0 < 0 ? 0 : c0) >> 1; c1 = (c1 < 0 ? 0 : c1) >> 1; c2 = (c2 < 0 ? 0 : c2) >> 1;
              }
              c0 = c0 < 0 ? 0 : c0 > 31 ? 31 : c0;
              c1 = c1 < 0 ? 0 : c1 > 31 ? 31 : c1;
              c2 = c2 < 0 ? 0 : c2 > 31 ? 31 : c2;
              r = (c0 << 3) | (c0 >> 2); gg = (c1 << 3) | (c1 >> 2); b = (c2 << 3) | (c2 >> 2);
            }
            uint8_t* d = fb + ((size_t)py * MW + px) * 3;
            d[0] = (uint8_t)r; d[1] = (uint8_t)gg; d[2] = (uint8_t)b;
          }
        }
    }
}
static void mChromeTMPal2(uint8_t* fb, const uint16_t* tm, int mode, const uint8_t (*pal)[16][3], const int8_t* d5) {
  mChromeTMPal2L(fb, tm, mode, pal, d5, 0);
}
static void mChromeTMPal(uint8_t* fb, const uint16_t* tm, int mode, const uint8_t (*pal)[16][3]) { mChromeTMPal2(fb, tm, mode, pal, NULL); }
// Draw the chrome for a menu screen. screen 0 = the ribbon (opt picks the per-option layout); 1..6 = the entered
// sub-screen; mode as in mChromeTMPal. The Save screen (opt 5) uses ctMenuChromeSavePal, the grey-marble palettes of
// its slot buttons (pal1/2/3). The char-select boxes are toned by C2/9E76's palette rows (mChromeApplyCharSel).
void CtMenuChrome(uint8_t* fb, int screen, int opt, int mode) {
  if (!fb) return;
  if (opt < 0) opt = 0; else if (opt > 5) opt = 5;
  int si = screen - 1; if (si < 0) si = 0; else if (si > 5) si = 5;
  const uint16_t* tm = (screen == 0) ? ctMenuChromeRibbonTM[opt] : ctMenuChromeEnteredTM[si];
  const uint8_t (*pal)[16][3] = (opt == 5) ? ctMenuChromeSavePal : ctMenuChromePal;
  // The window list: the ROM sets $7E:9D99 from the ribbon hover and does not rewrite it on entry, so an entered
  // screen N uses option N-1's list.
  int li = (screen > 0) ? (screen - 1) : opt;
  // The char select (entered screen 1 and its ribbon opt-0 preview) tones its party-column boxes by selection; both
  // views use the same list.
  int charSel = (screen == 1) || (screen == 0 && opt == 0);
  // The Form views re-apply C2/CC4F over the captured map (mChromeApplyPanelDim); the member select and its ribbon
  // preview re-apply C2/9E76 (mChromeApplyCharSel); every other map passes through.
  { static uint16_t scratch[CT_MENU_CHROME_TMW * CT_MENU_CHROME_TMH];
    const uint16_t* use = ((screen == 5) || (screen == 0 && opt == 4)) ? mChromeApplyPanelDim(tm, scratch)
                        : (charSel && g_charSelMem) ? mChromeApplyCharSel(tm, scratch, mCharSelSlot()) : tm;
    // The ribbon's builder lays its command box after the member panels are toned: $FF:BD2E's `02` copies the box
    // (16 x 6 cells) into (8,12) over the map C2/9E76 recoloured, so the box keeps its own palette even inside the
    // selected panel's rows (where C2/9E76 clears palette bit 2).
    if (screen == 0 && use == scratch)
      for (int r = 12; r < 18; r++)
        for (int c = 8; c < 24; c++) scratch[r * CT_MENU_CHROME_TMW + c] = tm[r * CT_MENU_CHROME_TMW + c];
    mChromeTMPal2L(fb, use, mode, pal, mWinGrad(li), 0); }
}

// The entered Equip/Status detail chrome: after A on a character the left column becomes three boxes -- [char stat]
// [slot type "Weapon"] [item list] -- plus one tall right box (portrait, gear, stats). A different BG2 tilemap from
// the char select's (ctMenuChromeEnteredTM[0]), captured at word 0x6000 (_gen_menuchrome_equip.py). Mode 0.
void CtMenuChromeEquip(uint8_t* fb) {
  if (!fb) return;
  mChromeTMPal(fb, ctMenuChromeEquipTM, 0, ctMenuChromePal);
}
// The candidate-list chrome (substate 3, C2/9CA5), a different BG2 layout from the gear panel's. Entering it,
// C2/A41A runs $FF:BEA4 (through C2/A4AF) and $FF:BEEC; both set `$61 = $3E00` (the BG2 buffer) and issue the
// interpreter's `02` block copy (C2/EF05):
//     $FF:BEA4   (col 2, row 12) 12w x 14h -> (col 2, row  8)   the item-list box climbs 4 tile rows
//     $FF:BEEC   (col 2, row 32) 28w x  4h -> (col 2, row 22)   the description bar, from the backup
// so the [slot type] box is swallowed and the right box shortens with it (no EXP/NEXT). The 32 px is the same delta
// the ROM writes into the list scrollbar's Y base $0D93 (0x62 in substate 2 -> 0x42 in substate 3, read by C2/8BE9).
// Captured at word 0x6000 (_gen_menuchrome_equiplist.py).
// The gear panel's window runs from line 16 to 207: its bottom tile rows 26-27 (y >= 208) are the marble desktop.
#define CT_MENU_EQUIP_MARBLE_Y  (26 * 8)   /* top of the bottom marble strip */
// One band of a captured chrome tilemap (`tm`: tile rows rowLo..rowHi, columns colLo..colHi) drawn shifted down by
// yShift and clipped to screen rows [clipTop, clipBot), the window tiles taking the ramp of the destination line.
// `csDim` >= 0 re-tones rows 2-7 (a member box) by C2/9E76's palette | 4; -1 keeps the map's own palettes.
static const int8_t* mChromeRowsGrad;   // the window ramp mChromeRowsTM paints with (0 = option 0's list)
static void mChromeRowsTM(uint8_t* fb, const uint16_t* tm, int rowLo, int rowHi, int colLo, int colHi, int yShift,
                          int csDim, int clipTop, int clipBot);
// The settled candidate list's window ramp. Through the climb the colour-math bands are option 0's list (flat 16, one
// 192-line ramp in 12-line steps); once the list is open the ramp stops at line 176 and the description bar gets its
// own, lines 177-208 in 2-line steps 8 .. -7 (C2/86DD over a 32-line ramp; C2/A41A re-points the band table after the
// loop: $9694 = $969A, $9696 = $9300, $9698 = $0025). In this port's numbering (HDMA line - 1) that is y 176..207.
static const int8_t* mWinGradEquipList(void) {
  static int8_t g[MH]; static int built;
  if (!built) {
    const int8_t* base = mWinGrad(0);
    for (int y = 0; y < MH; y++) g[y] = base[y];
    for (int y = 176; y < 208 && y < MH; y++) g[y] = (int8_t)(8 - (y - 176) / 2);
    built = 1;
  }
  return g;
}
static void mChromeRowsTM(uint8_t* fb, const uint16_t* tm, int rowLo, int rowHi, int colLo, int colHi, int yShift,
                          int csDim, int clipTop, int clipBot) {
  for (int ty = rowLo; ty <= rowHi; ty++)
    for (int tx = colLo; tx <= colHi; tx++) {
      uint16_t e = tm[ty * CT_MENU_CHROME_TMW + tx];
      int tile = e & 0x3FF, pl = (e >> 10) & 7, ti = tile - CT_MENU_CHROME_TILE0;
      if (ti < 0 || ti >= CT_MENU_CHROME_NTILE) continue;
      const uint8_t* g = mChromeGlyph(ti);
      int isWin = (tile >= 0x3DC && tile <= 0x3EF) || (tile >= 0x300 && tile <= 0x313);
      if (isWin && csDim >= 0 && ty >= 2 && ty < 8 && tx < 14)       // a member box: C2/9E76's palette | 4 when dim
        pl = (pl & 3) | (csDim ? 4 : 0);
      for (int yy = 0; yy < 8; yy++)
        for (int xx = 0; xx < 8; xx++) {
          int v = g[yy * 8 + xx], px = tx * 8 + xx, py = ty * 8 + yy + yShift;
          if (px >= MW || py < clipTop || py >= clipBot) continue;   // clip to the box window
          const uint8_t* C = mChromeInk(ctMenuChromePal, pl, v);
          int r = C[0], gg = C[1], b = C[2];
          if (isWin) {                                    // the ramp at the destination line
            int dd = (mChromeRowsGrad ? mChromeRowsGrad : mWinGrad(0))[py];   // (screen 1 keeps option 0's list)
            int c0 = (r >> 3) + dd, c1 = (gg >> 3) + dd, c2 = (b >> 3) + dd;
            c0 = c0 < 0 ? 0 : c0 > 31 ? 31 : c0;
            c1 = c1 < 0 ? 0 : c1 > 31 ? 31 : c1;
            c2 = c2 < 0 ? 0 : c2 > 31 ? 31 : c2;
            r = (c0 << 3) | (c0 >> 2); gg = (c1 << 3) | (c1 >> 2); b = (c2 << 3) | (c2 >> 2);
          }
          if (isWin) { r = r < 0 ? 0 : r > 255 ? 255 : r; gg = gg < 0 ? 0 : gg > 255 ? 255 : gg; b = b < 0 ? 0 : b > 255 ? 255 : b; }
          uint8_t* d = fb + ((size_t)py * MW + px) * 3;
          d[0] = (uint8_t)r; d[1] = (uint8_t)gg; d[2] = (uint8_t)b;
        }
    }
}

// ---------------------------------------------------------------- data layer
int CtMenuPanelData(const uint8_t* mem, int slot, CtMenuPanel* out) {
  if (!mem || slot < 0 || slot > 8) return 0;
  int id = mem[0x2980 + slot];                       // $7E2980-82 active party, $2983-88 the reserve
  if (id & 0x80) return 0;                           // bit7 = empty slot
  const uint8_t* sh = mem + 0x2600 + (size_t)(id & 7) * 0x50;   // the char sheet
  out->charId = id & 7;
  out->hp    = sh[3] | (sh[4] << 8);                 // +3 curHP
  out->hpMax = sh[0x3F] | (sh[0x40] << 8);           // +0x3F: the effective max (string 06 prints $9ACF; +5 is
                                                     //   the base -- a Silver/Gold Earring's +25/50% is only here)
  out->mp    = sh[7] | (sh[8] << 8);                 // +7 curMP
  out->mpMax = sh[9] | (sh[10] << 8);                // +9 maxMP
  out->lv    = sh[0x12];                             // +0x12 level
  out->name  = CtGetName(out->charId);
  return 1;
}

// (The play clock, C2/85AE, is player.c's CtMenuClockTick: the third routine of the pad latch C2/84D2.)

// -------------------------------------------------------------- text helpers
// ASCII -> CT charset code (the menu font is indexed by CT code; the name-entry grid's mapping: 'A' = 0xA0,
// 'a' = 0xBA, '0' = 0xD4, ':' = 0xE3, '/' = 0xE0).
static int mCode(char a) {
  if (a >= 'A' && a <= 'Z') return 0xA0 + (a - 'A');
  if (a >= 'a' && a <= 'z') return 0xBA + (a - 'a');
  if (a >= '0' && a <= '9') return 0xD4 + (a - '0');
  switch (a) { case ':': return 0xE3; case '/': return 0xE0; case '.': return 0xE8;
               case '+': return 0xEC; case '!': return 0xDE; case '?': return 0xDF;
               case '%': return 0xED; case '&': return 0xE4;
               case '(': return 0xE5; case ')': return 0xE6; case '\'': return 0xE7;
               case ',': return 0xE9; case '=': return 0xEA; case '-': return 0xEB;
               case '#': return 0xF3; }   // the CT charset (assets/text.py's table; 0xEC '+' serves the
                                          // equip bonus line "Magic +2")
  return -1;                                         // space / unmapped: skip 8px
}
// When mDim != 0 the text/number helpers draw through the dim glyph (non-focused party panel, pal4). Default 0
// (bright pal0); mCharPanel sets and clears it around a panel.
static int mDim = 0;
static int mNavHandOk = 1;   // C2/E954's $0D9B gate for the Item screen's nav hand (CtMenuSetNavHandShown, below)
// ---- C2/A5C3 / C2/A5E7: the candidate list's live preview (menu2_equip_change.md sec. 2.16) ----
// While the list's cursor is on a candidate, the ROM patches that id into the sheet copy's gear byte ($9A90 + $0D26;
// the live sheet is untouched until the commit), recomputes it (C2/916E; the Third Eye's `ASL $9B21`) and compares
// every row of C2/A642 -- Atk +3D, Def +3E, Power +36, Speed +38, Evade +3B, Magic +39, Stamina +37, Hit +3A, M.Def
// +3C (and the max-HP word into $0D57) -- with the current value: $0D4D+row = 00 unchanged / 08 higher (BG palette 2)
// / 1C lower (palette 7). The rows are redrawn with the new values in those palettes.
static int mEqPrevOn = 0;
static uint8_t mEqPrevNew[11], mEqPrevOld[11];   // CtMenuRecalcDerived's 11 bytes: +36..+3C, +3D, +3E, +3F/+40
static void mEqPrevSet(const CtMenuHost* h, int charId, int gearSlot, int candId) {
  static const uint8_t kSheetOff[4] = { 0x29, 0x27, 0x28, 0x2A };   // C2/A4AB by slot: weapon helm armor accessory
  const uint8_t* sh = h->mem + 0x2600 + (size_t)(charId & 7) * 0x50;
  uint8_t tmp[0x50];
  for (int i = 0; i < 0x50; i++) tmp[i] = sh[i];
  for (int i = 0; i < 11; i++) mEqPrevOld[i] = sh[0x36 + i];       // the committed values (C2/A600 LDA $9A90,X)
  tmp[kSheetOff[gearSlot & 3]] = (uint8_t)candId;                   // C2/A5D6 STA $9A90,X
  CtMenuRecalcDerived(tmp, mEqPrevNew);                             // C2/A5EA JSR C2/916E (+ the $A8 doubling)
  mEqPrevOn = 1;
}
static int mEqPrevDim(int k) {                                      // C2/A609: 00 / 08 higher / 1C lower
  if (!mEqPrevOn || mEqPrevNew[k] == mEqPrevOld[k]) return 0;
  return mEqPrevNew[k] > mEqPrevOld[k] ? 5 : 2;                    // palette 2 (cyan) / palette 7 (= dim2's tones)
}
static int mEqPrevVal(const uint8_t* sh, int k) { return mEqPrevOn ? mEqPrevNew[k] : sh[0x36 + k]; }
// A top clip line: mText/mNumR/mBlitIndexed skip glyphs and blits whose top is above it (the equip entry push, so a
// stat box scrolled off the window's top does not draw over the marble). 0 = no clip.
static int mClipTop = 0;
// The matching bottom clip line (exclusive). The ROM masks BG1 to the window boxes, so text the tilemap still holds
// does not show when a box shrinks: the candidate list's chrome ends the right box at tile row 21 (y176) while EXP/NEXT
// stay in the BG1 buffer ($7E:0345E/$7E:0349E). MH = no clip.
static int mClipBot = MH;
// mDim 6 / 7 = BG1 palette 1 / 5 of the menu CGRAM (ctMenuChromePal rows 1 / 5, the text's colours 5-7: a 2bpp glyph
// laid as 4bpp with plane 2 = plane 0 | plane 1, pixel v -> colour 4 + v): C2/A1EF's `$0D5D` = 4 -- a member's
// current HP at or under a tenth of the maximum -- and that row dimmed (C2/9E76, palette | 4).
static void mGlyphPalRow(const CtMenuHost* h, uint8_t* fb, int code, int x, int y, int row) {
  if (!h->menuFont2bpp) return;
  const uint8_t* g = h->menuFont2bpp + (code & 0xFF) * 16;
  for (int yy = 0; yy < 8; yy++) {
    int b0 = g[yy * 2], b1 = g[yy * 2 + 1];
    for (int xx = 0; xx < 8; xx++) {
      int bit = 7 - xx, v = ((b0 >> bit) & 1) | (((b1 >> bit) & 1) << 1);
      int px = x + xx, py = y + yy;
      if (!v || px < 0 || px >= MW || py < 0 || py >= MH) continue;
      memcpy(fb + ((size_t)py * MW + px) * 3, ctMenuChromePal[row & 7][4 + v], 3);
    }
  }
}
static void mGlyphSel(const CtMenuHost* h, uint8_t* fb, int code, int x, int y) {
  if (mDim == 6 || mDim == 7) { mGlyphPalRow(h, fb, code, x, y, mDim == 6 ? 1 : 5); return; }
  void (*g)(uint8_t*, int, int, int) = (mDim == 5 && h->drawGlyphCyan) ? h->drawGlyphCyan : (mDim == 4 && h->drawGlyphSave) ? h->drawGlyphSave : (mDim == 3 && h->drawGlyphYellow) ? h->drawGlyphYellow : (mDim == 2 && h->drawGlyphDim2) ? h->drawGlyphDim2 : (mDim && h->drawGlyphDim) ? h->drawGlyphDim : h->drawGlyph;
  if (g) g(fb, code, x, y);
}
static void mText(const CtMenuHost* h, uint8_t* fb, const char* s, int x, int y) {
  if (y < mClipTop || y >= mClipBot) return;           // whole row outside the box window -> clip
  for (; *s; s++, x += 8) {
    int c = mCode(*s);
    if (c >= 0) mGlyphSel(h, fb, c, x, y);
  }
}
// One raw glyph code in mText's palette (mDim), for a ROM string's non-ASCII bytes (an item record's icon).
static void mCodeAt(const CtMenuHost* h, uint8_t* fb, int code, int x, int y) {
  if (y < mClipTop || y >= mClipBot) return;
  mGlyphSel(h, fb, code & 0xFF, x, y);
}
static void mNumR(const CtMenuHost* h, uint8_t* fb, int v, int xRight, int y);
// C2/F0D1, the menu string's code 0C [cap, addr16]: the byte in two digits (C2/F114 fmt 11), or, once it reaches
// FF:CF33[cap], tile 2F twice (two stars) in the same attr. The strings name the cap per field: LV (FF:C4ED string 01,
// FF:C535 string 04, FF:C7DF string 17) cap 7; the seven stats of strings 04 / 05 / 09 -- Power +0x36 cap 0, Stamina
// +0x37 cap 2, Speed +0x38 cap 1, Magic +0x39 cap 5, Hit +0x3A cap 3, Evade +0x3B cap 4, M.Def +0x3C cap 6 (kStatCap,
// by offset - 0x36). FF:CF33 = 63 10 63 63 63 63 63 63: Speed stars at 16, the rest and the level at 99.
static const uint8_t kStatCap[7] = { 0, 2, 1, 5, 3, 4, 6 };
static void mStatCap(const CtMenuHost* h, uint8_t* fb, int v, int cap, int xRight, int y) {
  int lim = (h->rom && h->romLen > 0x3FCF33L + 8) ? CtRomByte(h->rom, 0x400000L, (long)(0x3FCF33L + (cap & 7))) : 0x100;
  if (v >= lim) { mCodeAt(h, fb, 0x2F, xRight - 16, y); mCodeAt(h, fb, 0x2F, xRight - 8, y); }   // 2F0FA-2F107
  else mNumR(h, fb, v, xRight, y);
}
// Right-aligned decimal (every number field in the panels is right-aligned).
static void mNumR(const CtMenuHost* h, uint8_t* fb, int v, int xRight, int y) {
  if (y < mClipTop || y >= mClipBot) return;           // whole row outside the box window -> clip
  char b[12]; int n = 0;
  if (v < 0) v = 0;
  do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11);
  for (int i = 0; i < n; i++) {
    int c = mCode(b[i]);
    if (c >= 0) mGlyphSel(h, fb, c, xRight - 8 * (i + 1), y);
  }
}

// ------------------------------------------------------------------- compose
// Layout of the main screen (opt0 ribbon): the left column is three 12-tile (96 px) panels, each 6 tiles (48 px)
// tall, stacked from tile row 2 (y8 in the 224-line frame), then the TIME/G box at tile rows 20-25 (y152). Panel
// text rows sit at box+8/+16/+24 (name / HP / MP).
#define MP_X   16                                    /* panel column: tile col 2 = x16 */
#define MP_W   96                                    /* 12 tiles */
#define MP_H   48                                    /* 6 tiles */
#define MP_PITCH 48
#define MT_X   16                                    /* TIME/G box: tile rows 20-25, cols 2-13 */
#define MT_Y   152
#define MT_W   96
#define MT_H   48

// ---------------------------------------------------------------- ribbon
// The six-icon command row and its selection marker, at the ROM's OAM: icons in slots 0-5 at X = 80+i*16, Y = 112,
// pal0, size 1, the selected one tile base+0x20; the arrows in slots 6/7, tile 0x22 pal7, X = 81+opt*16, Y = 96
// (vflip) / 128 (menu_phase2.md sec. 3(A)(B)).
#define RIB_X0    80                                  // first icon screen X (16px pitch: 80/96/.../160)
#define RIB_Y    112                                  // icon row screen Y (all six)
#define RIB_PITCH 16
#define RIB_N     6
static void mBlitIcon(uint8_t* fb, const uint8_t* rgba, int x0, int y0) {   // 16x16 RGBA, skip alpha 0
  for (int j = 0; j < 16; j++)
    for (int i = 0; i < 16; i++) {
      const uint8_t* p = rgba + ((size_t)j * 16 + i) * 4;
      int dx = x0 + i, dy = y0 + j;
      if (p[3] && dx >= 0 && dx < MW && dy >= 0 && dy < MH)
        memcpy(fb + ((size_t)dy * MW + dx) * 3, p, 3);
    }
}
// The ribbon selection arrow (name-cursor triangle asset $FF/9D70): [w u8][h u8]+RGBA, `vflip` for the upper arrow
// (one tile, two flips, as the OAM: slot 7 vflip=1 above, slot 6 vflip=0 below).
static void mBlitTri(uint8_t* fb, const uint8_t* spr, int x0, int y0, int vflip) {
  if (!spr) return;
  int w = spr[0], hh = spr[1]; const uint8_t* px = spr + 2;
  for (int j = 0; j < hh; j++)
    for (int i = 0; i < w; i++) {
      const uint8_t* p = px + ((size_t)(vflip ? hh - 1 - j : j) * w + i) * 4;
      int dx = x0 + i, dy = y0 + j;
      if (p[3] && dx >= 0 && dx < MW && dy >= 0 && dy < MH)
        memcpy(fb + ((size_t)dy * MW + dx) * 3, p, 3);
    }
}
// Draw the ribbon over the chrome: the box is the BG2 tiles $3A4-$3AC (CtMenuChrome mode 1, redrawn over the
// preview content); this lays the six command icons (the selected one the +0x20 variant) and the two arrows.
static void mDrawRibbon(const CtMenuHost* h, uint8_t* fb, int menuOpt) {
  if (menuOpt < 0) menuOpt = 0; else if (menuOpt >= RIB_N) menuOpt = RIB_N - 1;
  CtMenuChrome(fb, 0, menuOpt, 1);                   // the ribbon box, on top of content
  // (A) the six command icons (selected = highlight tile base+0x20 = ctMenuIconSel[i]).
  for (int i = 0; i < RIB_N; i++)
    mBlitIcon(fb, (i == menuOpt ? ctMenuIconSel[i] : ctMenuIconBase[i]), RIB_X0 + i * RIB_PITCH, RIB_Y);
  // (B) the two arrows around the selected icon: OBJ tile 0x22 (16x16, menu_arrow.h), both at X = 81 + opt*16; the
  // lower one at Y = 128 as is, the upper one at Y = 96 flipped. The arrow sits in rows 1-12 of its cell, so the tip
  // lands 1 px off the icon.
  {
    int ax = RIB_X0 + 1 + menuOpt * RIB_PITCH;                   // OAM X = 81 + opt*16
    for (int j = 0; j < 16; j++)
      for (int i = 0; i < 16; i++) {
        const uint8_t* pt = ctMenuArrow + ((size_t)j * 16 + i) * 4;         // top arrow (v-flip): read row 15-j
        const uint8_t* pf = ctMenuArrow + ((size_t)(15 - j) * 16 + i) * 4;
        int dx = ax + i;
        if (dx < 0 || dx >= MW) continue;
        if (pf[3]) { int dy = 96 + j; if (dy >= 0 && dy < MH) memcpy(fb + ((size_t)dy * MW + dx) * 3, pf, 3); }  // down arrow @Y96 flipped
        if (pt[3]) { int dy = 128 + j; if (dy >= 0 && dy < MH) memcpy(fb + ((size_t)dy * MW + dx) * 3, pt, 3); } // up arrow @Y128
      }
  }
  (void)mBlitTri;
}

// ---------------------------------------------------------- Item screen
// The item-name table: ROM $CC/0B5E (file offset 0x0C0B5E), fixed 11-byte records, name(id) = the 11 bytes at
// 0x0C0B5E + id*11. CT encoding (assets/text.py _font8): 0xA0-0xB9 'A'-'Z', 0xBA-0xD3 'a'-'z', 0xD4-0xDD '0'-'9',
// 0xE7 '\'', 0xE8 '.', 0xEF and 0xFF space, 0x20-0x2F an equip-type icon (no menu-font glyph -> a space). Records are
// space-padded with no terminator (a consumable's leading byte is 0xFF, the tail 0xEF), so all 11 are decoded and
// leading and trailing spaces stripped.
#define ROM_ITEM_NAMES 0x0C0B5Eu   /* SNES $CC/0B5E, the item-name table (menu_open_items.md 2): the pointer-table
                                      builder C2/98ED ($7E7D00[id] = 0x0B5E + id*11, `ADC #$000B` / `CPY #$01E4`
                                      = 0xF2 entries) and the fetch C2/F2E2 (`LDA #$CC0B` = bank $CC, 11-byte
                                      record). One uniform table, key items included. */
#define ROM_ITEM_STRIDE 11         /* fixed record width (items and techs) */
// Tech tables (screen 3, see CtMenuRenderTech):
#define ROM_TECH_NAMES  0x0C15C4u  /* SNES $CC/15C4, the single-tech name table; name = base + techId*11
                                      (1 = Cyclone .. 8 = Luminaire), the item table continued
                                      (0xC0B5E + 242*11 == 0xC15C4). C2/BDD9 LDA #15C4. */
#define ROM_TECH_MP     0x0C253Bu  /* SNES $CC/253B, tech MP cost, 1 byte per techId. C2/BC22 LDA $CC253B,X.
                                      ids 1..8 = 2,2,2,4,8,10,12,20. */
#define ROM_TECH_RANGE  0x02BD40u  /* SNES $C2/BD40, per-char cumulative tech-id boundary (C2/BD11/BD17):
                                      char X's single techs = ids [tbl[X], tbl[X+1]) =
                                      01,09,11,19,21,29,31,39 (Crono id0 -> ids 1..8). */
#define ROM_TECH_MAXCNT 0x03FF951u /* SNES $FF/F951, per-char single-tech "add" threshold (FF/F893). */

// Decode one fixed-width (11-byte) CT name record at file offset `off` into `out` (encoding: see the item table
// above), stripping leading and trailing spaces.
static int mMapName(const uint8_t* rom, long romLen, size_t off, char* out, int cap) {
  (void)rom; (void)romLen;                          // the name bytes come from the baked ctRomCC
  if (out && cap > 0) out[0] = 0;
  if (cap < 2) return 0;
  char tmp[ROM_ITEM_STRIDE + 1]; int n = 0;
  for (int i = 0; i < ROM_ITEM_STRIDE; i++) {
    uint8_t c = mnuRom((long)off + i); char ch;
    if      (c >= 0xA0 && c <= 0xB9) ch = (char)('A' + c - 0xA0);
    else if (c >= 0xBA && c <= 0xD3) ch = (char)('a' + c - 0xBA);
    else if (c >= 0xD4 && c <= 0xDD) ch = (char)('0' + c - 0xD4);
    else if (c == 0xE7)              ch = '\'';
    else if (c == 0xE8)              ch = '.';
    else                             ch = ' ';   // 0xEF/0xFF pad, 0x20-0x2F (equip/star icon), unmapped
    tmp[n++] = ch;
  }
  tmp[n] = 0;
  int s = 0; while (tmp[s] == ' ') s++;           // strip leading (0xFF lead byte / 0x2F star icon -> space)
  int e = n; while (e > s && tmp[e - 1] == ' ') e--;   // strip trailing 0xEF padding
  int w = 0; for (int i = s; i < e && w < cap - 1; i++) out[w++] = tmp[i];
  out[w] = 0;
  return w;
}

int CtMenuItemName(const uint8_t* rom, long romLen, int id, char* out, int cap) {
  if (out && cap > 0) out[0] = 0;
  if (id < 0 || id > 255) return 0;
  return mMapName(rom, romLen, ROM_ITEM_NAMES + (size_t)id * ROM_ITEM_STRIDE, out, cap);
}

// The item category label (WEAPON/HELMET/ARMOR/ACCESSORY/ITEM) shown after the count on each item row: the pointer
// table $FF/B310 + id*2 (file 0x3FB310) -> a NUL-terminated CT string in bank $FF (file 0x3F0000 + ptr).
static int mCatName(const uint8_t* rom, long romLen, int id, char* out, int cap) {
  (void)rom; (void)romLen;                          // resolved at build time into ctItemCat (menu_romdata.h)
  if (out && cap > 0) out[0] = 0;
  if (cap < 2 || id < 0 || id > 255) return 0;
  const char* s = ctItemCat[id]; int w = 0;
  for (; w < 11 && s[w] && w < cap - 1; w++) out[w] = s[w];
  out[w] = 0;
  return w;
}

// Screen 3: decode single tech `techId`'s name from ROM $CC/15C4 + techId*11 (1 = Cyclone). The elemental techs
// carry a leading 0x2F star icon that the decoder folds to a space (then stripped).
int CtMenuTechName(const uint8_t* rom, long romLen, int techId, char* out, int cap) {
  if (out && cap > 0) out[0] = 0;
  if (techId < 0 || techId > 0xFF) return 0;
  return mMapName(rom, romLen, ROM_TECH_NAMES + (size_t)techId * ROM_ITEM_STRIDE, out, cap);
}

// The Item screen's layout: a full-width header window across the top ([Item icon] then "Use/Move"), then a tall
// list window with one row per held item.
#define MI_HX  8
#define MI_HY  8
#define MI_HW  240
#define MI_HH  32
#define MI_LY  72                                  /* item-list window top = BG2 tile row 10 (y72) */
#define MI_ROW0 (MI_LY + 16)                       /* first item baseline y88 */
#define MI_ROWH 12                                 /* row pitch = the nav graph's list pitch (Y = 87 + 12*row) */

// The item list is direct-indexed (C2/AE43): visible row r = inventory slot ($0416 scroll + r) for 10 rows, and an
// empty slot (id 0) draws a blank row (C2/B04B `BEQ`), so a used-up item leaves a gap until Organize. Returns the
// extent = 1 + the highest non-empty slot (the cursor clamps to the last real item); ids[]/cnts[] are the raw
// $2400/$2500 slots.
// The Item screen's cursor graph: the build copies FF/B53E -> $7E:9380 (the Config screen fills the same place from
// FF/B5A0), and C2/E964 `LDA $9380,X / STA $0770` places the hand sprite at record[cell]'s first two bytes. The
// hand's ink starts 3 px inside the sprite, so record 0 (24, 87) inks at y90.
#define ROM_ITEM_NAV 0x3FB53Eu                              /* FF/B53E -> $7E:9380 (menu_romdata.h) */
#define MI_NAVX(c)  mnuRom((long)ROM_ITEM_NAV + (long)(c) * 6)
#define MI_NAVY(c)  mnuRom((long)ROM_ITEM_NAV + (long)(c) * 6 + 1)
#define MI_ROWS 10                                          /* visible list rows (window $0DDB=0..$0DDC=9) */
// The one-row slide, shared by every list (C2/EB9B + C2/EB1F; menu2_equip_change.md sec. 11): the list is a layer of
// 16 px slots shown 12 lines each per 12-line band of an HDMA V-scroll table, and each step moves every band $0DAB
// lines, so the rows (the incoming one in its extra slot too) slide as one strip, cut to the list's band lines. The
// rows are drawn at their static places and shown shifted: fb(y) = strip(y + o).
static uint8_t mListStrip[MH * MW * 3];
static void mListStripClear(void) {                 // (1,2,3) cannot be a PPU colour: (v<<3)|(v>>2) is 0 or >= 8
  for (int i = 0; i < MH * MW; i++) { mListStrip[i * 3] = 1; mListStrip[i * 3 + 1] = 2; mListStrip[i * 3 + 2] = 3; }
}
static void mListStripBlit(uint8_t* fb, int y0, int y1, int o) {
  for (int y = y0 < 0 ? 0 : y0; y < y1 && y < MH; y++) {
    int sy = y + o;
    if (sy < 0 || sy >= MH) continue;
    for (int x = 0; x < MW; x++) {
      const uint8_t* s = mListStrip + ((size_t)sy * MW + x) * 3;
      if (s[0] == 1 && s[1] == 2 && s[2] == 3) continue;
      memcpy(fb + ((size_t)y * MW + x) * 3, s, 3);
    }
  }
}
static void mCharPanel(const CtMenuHost* h, uint8_t* fb, int x, int y, const CtMenuPanel* p, int textInset, int dim);
static void mHandCursor(const CtMenuHost* h, uint8_t* fb, int x, int y);
static void mGlyph(const CtMenuHost* h, uint8_t* fb, int code, int x, int y);
static void mBlitSprAnchored(uint8_t* fb, const uint8_t* blob, int cx, int feetY, int dim);
static const uint8_t* mPartyBlob(const CtMenuHost* h, int charId);
static void mBlitPartySpr(const CtMenuHost* h, uint8_t* fb, int charId, int cx, int feetY, int dim);  // the blob at its animation pose, canvas-anchored
static void mBlitIndexed(uint8_t* fb, const uint8_t* idx, int w, int hh, int x0, int y0, const uint32_t* pal);
static const uint32_t* mDimPal(const uint32_t* pal);
static const uint8_t gen_opt0_icon_def[8 * 8];            // the shield/DEF icon
static int mInventoryRaw(const uint8_t* mem, int* ids, int* cnts, int cap) {
  int extent = 0;
  for (int i = 0; i < cap && i < 0xF2; i++) {
    ids[i] = mem ? mem[0x2400 + i] : 0;
    cnts[i] = mem ? mem[0x2500 + i] : 0;
    if (ids[i]) extent = i + 1;
  }
  return extent;
}

// The item description shown in the second box is stored in the ROM. C2/F2F3, the description stager:
//    2F308 STA $020C                      the item id
//    2F317 LDX #2EB1 / STX $020D
//    2F31D LDA #CC   / STA $020F          -> the 24-bit source $CC:2EB1
//    2F32D JSR C2/FA49                    -> render
// so $CC:2EB1 is a table of 16-bit pointers into bank $CC indexed by item id; src/data/menu_desc.h is that table
// decoded (tools/_gen_menudesc.py). An empty entry means the item has no description (137 of the 242 ids, e.g.
// the Pendant). disasm/notes/menu_item_descriptions.md. Returns 1 if a description was written.
static int mItemDesc(int id, char* out, int cap) {
  if (out && cap) out[0] = 0;
  if (!out || cap <= 0 || id < 0 || id >= (int)(sizeof ctItemDesc / sizeof ctItemDesc[0])) return 0;
  if (!ctItemDesc[id][0]) return 0;
  snprintf(out, (size_t)cap, "%s", ctItemDesc[id]);
  return 1;
}
// The number of C2/0009 steps that type item `id`'s line (one decoded character each; C2/FA49's task).
int CtMenuItemDescLen(int id) {
  char d[48];
  return mItemDesc(id, d, sizeof d) ? (int)strlen(d) : 0;
}
// The decoded length of string `id` of C2/FA49's table `kind`: 0 = $CC:2EB1 (the item descriptions, C2/F2F3),
// 1 = $CC:3A09 (the tech descriptions and, from $76, the tab names -- C2/BE33 / C2/BE16).
int CtMenuStrLen(int kind, int id) {
  if (kind == 1) return (id >= 0 && id < (int)(sizeof ctTechDesc / sizeof ctTechDesc[0])) ? (int)strlen(ctTechDesc[id]) : 0;
  return CtMenuItemDescLen(id);
}
// Alpha-blit an RGBA bitmap flipped vertically (the scrollbar's up arrow = the down-arrow tile V-flipped, ROM attr
// 0x30 vs 0xB0). Same format as mBlitRGBA.
static void mBlitRGBAFlipV(uint8_t* fb, const unsigned char* rgba, int w, int hh, int x0, int y0) {
  for (int j = 0; j < hh; j++)
    for (int i = 0; i < w; i++) {
      const unsigned char* p = rgba + ((size_t)(hh - 1 - j) * w + i) * 4;
      int dx = x0 + i, dy = y0 + j;
      if (p[3] && dx >= 0 && dx < MW && dy >= 0 && dy < MH) memcpy(fb + ((size_t)dy * MW + dx) * 3, p, 3);
    }
}

// One list row (C2/AE65): slot `slot`'s icon, name, ":" count and category at text line y; empty slots draw nothing.
static void mItemRow(const CtMenuHost* h, uint8_t* fb, const int* ids, const int* cnts, int slot, int y) {
  if (slot < 0 || slot >= 0xF2) return;
  int id = ids[slot];
  if (id == 0) return;                                    // empty slot -> blank row (C2/B04B BEQ)
  char nm[16];
  CtMenuItemName(h->rom, h->romLen, id, nm, sizeof nm);   // a blank record (ids 1B-1D ..) draws no name
  // Field-usable items (Tonic, ...) draw yellow, the rest white ($7700[id] & 0x02 -> BG palette 2, C2/AE83). Only
  // the name is coloured; the ":" and the count stay white.
  mDim = CtMenuItemUsable(id) ? 3 : 0;
  if (h->rom && h->romLen > 0x0C0B5EL + 11L * id)         // C2/B05A-B061: the whole 11-byte record $CC:0B5E + 11 id
    mCodeAt(h, fb, CtRomByte(h->rom, 0x400000L, (long)(0x0C0B5EL + 11L * id)), 40, y);  //   from x40 in the row's attr; byte 0 = the type icon
                                                          //   (a weapon's sword 0x20 ..; FF = a blank tile)
  mText(h, fb, nm, 48, y);                                // name at x48 (yellow if usable)
  mDim = 0;
  if (h->drawGlyph) h->drawGlyph(fb, 0x2D, 128, y);       // ": count CATEGORY"; under target select the overlay
  mNumR(h, fb, cnts[slot], 152, y);                       //   covers these columns (CtMenuRenderItemOverlay); on
  char cat[16];                                           //   C2/AF42's two build frames they are still on screen
  if (mCatName(h->rom, h->romLen, id, cat, sizeof cat)) mText(h, fb, cat, 160, y);  // category at x160
}
#define MI_BAND0 84                                        /* the list's first band line (channel 3, BG1 V: 40 + 44) */
#define MI_LISTTOP 88   /* but BG1 is on from line 88 only: channel 4's TM table ($212C) is 12 (BG2 + OBJ) on lines
                          72-87 and 13 on 88-207, so the slide's first rows cut there */
// The Item screen (C2/AA34); the parameters are described in menu.h.
void CtMenuRenderItem(const CtMenuHost* h, uint8_t* fb, int cell, int scroll,
                      int pickedSlot, int targetMode, int partyN, int pinnedBlinkOn, int barBlinkOn,
                      int descId, int descN, int slideO) {
  if (!h || !fb) return;
  // the background (the BG2 chrome) is laid by the caller (CtMenuChrome)
  // The header window: the Item ribbon icon and the two action labels, "Use/Move" (the list/move context) and
  // "Organize" (the sort). Both labels stay white; the hand marks the selected one.
  int onLabels = (cell == 0x0A || cell == 0x0B);
  mBlitIcon(fb, ctMenuIconSel[1], MI_HX + 16, MI_HY + 16);  // the selected (orange) Item icon at (24,24)
  mText(h, fb, "Use/Move", MI_HX + 48, MI_HY + 24);
  mText(h, fb, "Organize", MI_HX + 144, MI_HY + 24);
  if (onLabels && mNavHandOk)                              // the hand on the selected label (C2/AA4A), from nav
    mBlitRGBA(fb, itemCursorRGBA, ITEMCURSOR_W, ITEMCURSOR_H,  // graph FF/B53E's record like the list rows
              MI_NAVX(cell), MI_NAVY(cell));                // (0x0A = (40,31), 0x0B = (136,31))

  // The description box: C2/F2F3's line -- item `descId` ($0D77), the first `descN` characters C2/FA49's string
  // task has typed (one a frame from the C2/F2F3 frame on; the caller runs the task).
  { char desc[48];                        // >= the ROM table's longest line (35 chars + NUL)
    if (descN > 0 && mItemDesc(descId, desc, sizeof desc)) {
      if (descN < (int)strlen(desc)) desc[descN] = 0;
      if (h->vwfDraw) h->vwfDraw(fb, desc, 24, 60, ctLoaderVwfTone);   // C2/FA49 is a VWF render (the shop's too),
                                                          // lines 60-67
      else mText(h, fb, desc, 24, 60);
    } }

  int ids[0xF2], cnts[0xF2];
  mInventoryRaw(h->mem, ids, cnts, 0xF2);
  if (slideO) {                                             // C2/AC8A's slide (11 bands: 10 rows + the incoming one
    mListStripClear();                                      //   drawn at $344A), `scroll` already the new window
    int r0 = slideO > 0 ? scroll - 1 : scroll, k0 = slideO > 0 ? 0 : -1;
    for (int k = 0; k < MI_ROWS + 1; k++) mItemRow(h, mListStrip, ids, cnts, r0 + k, MI_ROW0 + (k0 + k) * MI_ROWH);
    mListStripBlit(fb, MI_LISTTOP, MI_BAND0 + MI_ROWS * MI_ROWH, slideO);
  } else {
    int y = MI_ROW0;
    for (int r = 0; r < MI_ROWS && y <= MH - MI_ROWH; r++, y += MI_ROWH)   // direct-indexed rows (C2/AE43)
      mItemRow(h, fb, ids, cnts, scroll + r, y);
  }
  if (!targetMode) {
    // The pinned hand first, then the nav hand on top, so the steady nav hand wins when both land on the same row.
    // The pinned item's blinking hand (the Move source $0F00, a separate OAM sprite $0774, C2/89FA): anchored to
    // the picked row, 4 on / 4 off, offset (+2,-1) from the nav hand.
    if (pickedSlot >= 0 && pinnedBlinkOn) {
      int prow = pickedSlot - scroll;
      int dy = slideO ? (slideO > 0 ? 12 - slideO : -12 - slideO) : 0;   // $0D9E: +-12 less $0D22 a step (C2/ACD0)
      if (prow >= 0 && prow < MI_ROWS)
        mBlitRGBA(fb, itemCursorRGBA, ITEMCURSOR_W, ITEMCURSOR_H,
                  MI_NAVX(prow) + 2, MI_NAVY(prow) - 1 + dy);
    }
    // the steady nav hand on the current list row: record[cell] of FF/B53E.
    if (!onLabels && cell >= 0 && cell < MI_ROWS && mNavHandOk)
      mBlitRGBA(fb, itemCursorRGBA, ITEMCURSOR_W, ITEMCURSOR_H, MI_NAVX(cell), MI_NAVY(cell));
    // The scrollbar (C2/8BB8, task slot 4): two sprites that move -- the up and down arrows are the thumb. Both sit
    // at X $0D92 = 0xE8 (232), 8 px apart, and the pair's Y is an 8.8 ramp $0D93 + (($0D95 * $0D98.w) >> 8): C2/ADD0
    // writes Ybase $0D93 = 0x50, and C2/EA81 derives the scroll max $0D94 = 0xE8 (242 slots - 10 visible) and the step
    // $0D98 = 0x0072 (0x6800 / 0xE8, a 104 px track). The up arrow hides at scroll 0, the down arrow at the max. They
    // blink together, 8 frames on / 8 off ($0D96 toggling $0D97 bit 0), apart from the pinned hand's 4/4. The tile is
    // the ribbon arrow's (0x22, a 16x16 OBJ; attr 30 = up, B0 = down, vflipped), and OBJ pal 0 has pal 7's three inks.
    // C2/8C0B parks both sprites when $0D94 is zero, as on the ribbon's hover; the port does not stage $0D92..$0D9A,
    // so `cell < 0` (the preview marker) stands in for that gate. disasm/notes/menu2_item_rest.md.
    if (barBlinkOn && cell >= 0) CtMenuRenderScrollbar(h, fb);
  }

  // Target select (C2/B17F): the hand on the target's nav record (cells 0x0C-0x0E: (120, 99 + 40k)) from the A on
  // (C2/B18B $54 := 0C), through C2/AF42's two build frames too; the overlay itself is CtMenuRenderItemOverlay's.
  if (targetMode && cell >= 0x0C && cell < 0x0C + 3 && mNavHandOk)
    mBlitRGBA(fb, itemCursorRGBA, ITEMCURSOR_W, ITEMCURSOR_H, MI_NAVX(cell), MI_NAVY(cell));
}

// ---- The target overlay (C2/B17F -> C2/AF42), offset-per-tile ----
// C2/AFB5 writes BG3 buffer rows 0-1 from column 15 -- 0x6090 (H: BG1 and BG2 + 144) and 0x60AF (V: + 175) -- and FC37
// uploads them to map rows 62-63, the rows BG3 V 496 (channel 5, FF/B8F4) fetches on the lines channel 2 (FF/BAD8)
// puts in mode 2: 80-207. So screen columns 16-28 (x 128..231) there show BG1/BG2 at (x + 144, y + 176): the window
// $FF:C0A6 built at (2,32) 14 x 16 (menu_itemoverlay.h) and the three member panels C2/AFED writes into BG1 from row
// 30, five rows apart (text x 136, name rows y 88 + 40k), all bright. Column 29 stays the list box's border. The
// party's sprites stand at C2:B045's words (centre E4, feet 78/A0/C8, the Form's convention, C2/CE6E). `shown` = the
// HP/MP C2/AFED last drew (it redraws on the build's second frame and in C2/B31C, not when C2/8E2D changes the
// sheet); `pop*` = C2/9016's digits (below). disasm/notes/menu2_item_rest.md.
static void mItemPopDigits(const CtMenuHost* h, uint8_t* fb, int value, int positions, int cx, int y, long palOff);
static void mPanelAtkDefOne(const CtMenuHost* h, uint8_t* fb, int textInset, int y, int charId, int dim);
static void mItemOverlayWindow(const CtMenuHost* h, uint8_t* fb);
// C2/AFB5's offset-per-tile window ($FF:C0A6 at (2,32) 14 x 16, shown at map x = (x + 144) & 255, map y = y + 176 over
// x 128..231, y 80..207): the target overlay's and the equipment info panel's (C2/AE91 starts with the same JSR).
static void mItemOverlayWindow(const CtMenuHost* h, uint8_t* fb) {
  const int8_t* d5 = mWinGrad(1);                           // the Item screen's window list (C2/86DD, option 1)
  for (int y = 80; y < 208; y++) {
    int my = y + 176, r = (my >> 3) - 32;
    if (r < 0 || r >= 16) continue;
    for (int x = 128; x < 232; x++) {
      int mx = (x + 144) & 255, c = (mx >> 3) - 2;
      if (c < 0 || c >= 14) continue;
      uint16_t e = ctItemOverlayWinTM[r][c];
      int ti = (e & 0x3FF) - CT_MENU_CHROME_TILE0;
      if (ti < 0 || ti >= CT_MENU_CHROME_NTILE) continue;
      int v = mChromeGlyph(ti)[(my & 7) * 8 + (mx & 7)];
      const uint8_t* P = mChromeInk(ctMenuChromePal, (e >> 10) & 7, v);
      int c0 = (P[0] >> 3) + d5[y], c1 = (P[1] >> 3) + d5[y], c2 = (P[2] >> 3) + d5[y];   // a window tile: the ramp
      c0 = c0 < 0 ? 0 : c0 > 31 ? 31 : c0; c1 = c1 < 0 ? 0 : c1 > 31 ? 31 : c1; c2 = c2 < 0 ? 0 : c2 > 31 ? 31 : c2;
      uint8_t* d = fb + ((size_t)y * MW + x) * 3;
      d[0] = (uint8_t)mExp5(c0); d[1] = (uint8_t)mExp5(c1); d[2] = (uint8_t)mExp5(c2);
    }
  }
}
void CtMenuRenderItemOverlay(const CtMenuHost* h, uint8_t* fb, int tgt, const uint16_t shown[3][2],
                             int popCurve, int popPool, const int popAmt[3][2], long popPal, int multiBits) {
  if (!h || !fb || !h->mem) return;
  mItemOverlayWindow(h, fb);
  for (int k = 0; k < 3; k++) {                             // BG1: C2/AFED's panels
    CtMenuPanel p;
    if (!CtMenuPanelData(h->mem, k, &p)) continue;
    if (shown) { p.hp = shown[k][0]; p.mp = shown[k][1]; }
    mCharPanel(h, fb, MP_X + 112, 72 + 40 * k, &p, 8, 0);
    mPanelAtkDefOne(h, fb, 120, 112 + 40 * k, p.charId, 0);
  }
  for (int k = 0; k < 3; k++) {                             // OBJ: the party at C2:B045, prio 2
    CtMenuPanel p;
    if (!CtMenuPanelData(h->mem, k, &p)) continue;
    int w = (h->rom && h->romLen > 0x2B045 + 6) ? (CtRomByte(h->rom, 0x400000L, (long)(0x2B045 + 2 * k)) | (CtRomByte(h->rom, 0x400000L, (long)(0x2B046 + 2 * k)) << 8)) : 0;
    mBlitPartySpr(h, fb, p.charId, (w >> 8) & 0xFF, w & 0xFF, 0);
  }
  if (multiBits >= 0) {
    // An all-party item: C2/8AD5's task (slot 6, in place of the pinned hand's) and no nav hand. Every frame C2/8AFD
    // steps three 2-bit counters $0DA3 / $0DA4 / $0DA5 (seeded 3 / 1 / 2) and toggles bit k of $0DA0 (seeded 02) when
    // counter k wraps; a hand (tile 20, attr 30) is drawn for each member whose bit is clear, at its nav cell's x + 2
    // and the same y: C2/8B85 runs with A 16-bit, so its `LDA $9381,X / DEC A / XBA` is overwritten by the following
    // `LDA $9380,X` (the pinned hand's C2/89FA, in 8-bit, keeps its y - 1). The nav hand ($0770) is still up on the
    // task's first frame and gone after ($0D9B, above).
    for (int k = 0; k < 3; k++) {
      CtMenuPanel p;
      if ((multiBits >> k) & 1) continue;
      if (!CtMenuPanelData(h->mem, k, &p)) continue;
      mBlitRGBA(fb, itemCursorRGBA, ITEMCURSOR_W, ITEMCURSOR_H, MI_NAVX(0x0C + k) + 2, MI_NAVY(0x0C + k));
    }
  }
  if (tgt >= 0 && tgt < 3 && mNavHandOk)                    // the nav hand (prio 3)
    mBlitRGBA(fb, itemCursorRGBA, ITEMCURSOR_W, ITEMCURSOR_H, MI_NAVX(0x0C + tgt), MI_NAVY(0x0C + tgt));
  // C2/9016's pop-up: every member with an amount, the digits at the member's sprite word minus the curve FF/CEBA
  if (popCurve >= 0 && popAmt && h->rom && h->romLen > 0x3FCEBA + 0x60) {
    int dy = CtRomByte(h->rom, 0x400000L, (long)(0x3FCEBA + popCurve));
    for (int k = 0; k < 3; k++) {
      int amt = popAmt[k][popPool == 2 ? 1 : 0];
      if (amt < 0) continue;
      int w = CtRomByte(h->rom, 0x400000L, (long)(0x2B045 + 2 * k)) | (CtRomByte(h->rom, 0x400000L, (long)(0x2B046 + 2 * k)) << 8);
      mItemPopDigits(h, fb, amt, popPool == 2 ? 1 : 3, (w >> 8) & 0xFF, (w & 0xFF) - dy, popPal);
    }
  }
}
// The equipment info panel (Item substate 5: C2/AB2F -> C2/AE91 on A over a picked weapon / helm / armour /
// accessory). C2/AE91 = C2/AFB5 (the overlay's window) + C2/AEB6 + C2/AF0B + the list $FF:C0D5: string $22 of
// $FF:C457 (FF:C880) at (4, 2): "Can be used" / "by" + tile 2D, then at row 6 the 6-byte names C2/AEB6 copied
// ($7A00[id] & $29AF, bit 7 = character 0; two to a row), at (12, 12) the value $7E:0D4D (C2/AF0B: $7C00[id], built
// for ids below 0x94 only, so an accessory reads 0; three digits, leading blanks) and at (4, 12) the label from
// C2:AF3A[type] (C2/8881: thresholds 5A / 7B / 94 / BC -> "Attack", "Defense", "Defense", blanks). Map (col, row)
// shows at x = 8 col - 144, y = 8 row - 176 (mod 256). disasm/notes/menu2_item_rest.md sec. 7.
void CtMenuRenderItemInfo(const CtMenuHost* h, uint8_t* fb, int itemId) {
  if (!h || !fb || !h->mem || itemId < 0 || itemId > 0xF1) return;
  mItemOverlayWindow(h, fb);
#define IIX(c) ((8 * (c) - 144) & 255)
#define IIY(r) ((8 * (r) - 176) & 255)
  mText(h, fb, "Can be used", IIX(4), IIY(2));
  mText(h, fb, "by", IIX(4), IIY(3));
  mGlyph(h, fb, 0x2D, IIX(6), IIY(3));
  int mask = ctEqMask[itemId] & h->mem[0x29AF], n = 0;       // C2/AEB6
  for (int c = 0; c < 8 && n < 7; c++)
    if (mask & (0x80 >> c)) {
      mText(h, fb, CtGetName(c), IIX(4 + (n % 2) * 7), IIY(6 + n / 2));
      n++;
    }
  int type = itemId < 0x5A ? 0 : itemId < 0x7B ? 1 : itemId < 0x94 ? 2 : 3;   // C2/8881
  if (type == 0) { mText(h, fb, "Attack", IIX(4), IIY(12)); mGlyph(h, fb, 0x2D, IIX(10), IIY(12)); }
  else if (type <= 2) { mText(h, fb, "Defense", IIX(4), IIY(12)); mGlyph(h, fb, 0x2D, IIX(11), IIY(12)); }
  mNumR(h, fb, itemId < 148 ? ctEqPower[itemId] : 0, IIX(15), IIY(12));   // $0D4D, three digits at cols 12-14
#undef IIX
#undef IIY
}
// C2/9093: one member's number as OBJ digits -- positions `positions`..0 of its BCD value (C2/F13F), leading zeros
// skipped (position 0 always drawn), the first drawn at the centre + C2:90F8[position] (FC F8 F4 F0), then 8 px apart;
// tile 0x174 + digit (FF/F7D9 laid FF/9390's 2bpp digits there as 4bpp with plane 2 = plane 0 | plane 1, so a pixel
// is colour 4 + (p0 + 2 * p1): OBJ palette 0's colours 5-7, which the caller loads -- `palOff` = the ROM offset of
// its four-colour MVN into $9588: C2/8F55 (a restore) FF/CB9C, C2/B2A7 (a Tab) FF/CBAC), attr 31.
static void mItemPopDigits(const CtMenuHost* h, uint8_t* fb, int value, int positions, int cx, int y, long palOff) {
  if (!h->rom || h->romLen < 0x3F9390L + 160 || palOff < 0 || h->romLen < palOff + 8) return;
  int dig[4] = { value % 10, (value / 10) % 10, (value / 100) % 10, (value / 1000) % 10 };
  uint8_t pal[4][3];
  for (int i = 1; i < 4; i++) {
    int c = CtRomByte(h->rom, 0x400000L, (long)(palOff + 2 * i)) | (CtRomByte(h->rom, 0x400000L, (long)(palOff + 1 + 2 * i)) << 8);
    pal[i][0] = (uint8_t)mExp5(c & 31); pal[i][1] = (uint8_t)mExp5((c >> 5) & 31); pal[i][2] = (uint8_t)mExp5((c >> 10) & 31);
  }
  int drawn = 0, x = 0;
  for (int pos = positions; pos >= 0; pos--) {
    int d = dig[pos];
    if (!d && pos && !drawn) continue;
    if (!drawn) x = cx + (int8_t)CtRomByte(h->rom, 0x400000L, (long)(0x290F8 + pos));
    const uint8_t* t = CtRomPtr(h->rom, h->romLen, 0x3F9390L + 16L * d, 16);
    for (int r = 0; r < 8; r++)
      for (int b = 0; b < 8; b++) {
        int p0 = (t[2 * r] >> (7 - b)) & 1, p1 = (t[2 * r + 1] >> (7 - b)) & 1;
        if (!(p0 | p1)) continue;
        int px = x + b, py = y + r;                         // OAM Y is the port's y (the sprite's first line)
        if (px < 0 || px >= MW || py < 0 || py >= MH) continue;
        memcpy(fb + ((size_t)py * MW + px) * 3, pal[p0 + 2 * p1], 3);
      }
    drawn = 1;
    x += 8;
  }
}

// The party-exchange swap, C2/C70C. $0F00/$0F01 = the two picked roster slots; the two bytes are swapped in place.
//   C2/C70F  LDX $0F00 ;  C2/C712  LDA $2980,X ;  C2/C715  STA $00        (tmp = party[a])
//   C2/C717  LDY $0F01 ;  C2/C71A  LDA $2980,Y ;  C2/C71D  STA $2980,X    (party[a] = party[b])
//   C2/C720  LDA $00   ;  C2/C722  STA $2980,Y                            (party[b] = tmp)
// X and Y are 8-bit and index $2980..$2988 uniformly, so a reserve <-> active exchange is the same swap (screen 5
// never selects a reserve: $0D36 = 0xFF in the X menu, so cells 3..5 hold 0x80).
void CtMenuFormSwap(uint8_t* mem, int a, int b) {
  if (!mem || a < 0 || a > 8 || b < 0 || b > 8) return;
  uint8_t t = mem[0x2980 + a];                       // C2/C712 LDA $2980,X / STA $00
  mem[0x2980 + a] = mem[0x2980 + b];                 // C2/C71A LDA $2980,Y / C2/C71D STA $2980,X
  mem[0x2980 + b] = t;                               // C2/C720 LDA $00     / C2/C722 STA $2980,Y
}

// ---- The Form screen's entry gate (C2/C627) and the roster builder behind it (C2/CD2B) ----
// disasm/notes/menu_open_items.md sec. 3.1/3.2. Screen 5's substate-0 handler decides whether the screen opens:
//   C2/C62B  LDA $0F0B / CLC / ADC $0F0C / CMP #$02
//   C2/C634  BCS ->C2/C63D      ; ($0F0B + $0F0C) >= 2 -> open (picker init C2/C649)
//   C2/C636  STZ $67            ; else refuse: screen back to 0 ...
//   C2/C638  INC $68            ;   ... and $0068 = 1, the ribbon's idle substate
// On a refusal $0067 goes 05 (C2:9A83, the A handler) and back to 00 (C2:C638) on the next frame.
// The two counts come from C2/CD2B:
//   $0F0B = active members not locked: per slot, skip if $2980+i has bit 7 (empty); else count it, and un-count it
//           when $7F01DF & $FF/F9BB[charId] (the party-lock bitfield; FF/F9BB = 80 40 20 10 08 04 02 01).
//   $0F0C = non-empty reserve slots $2983..$2988, scanned only when $0D36 == 0 (C2/CD72 `LDA $0D36 / BNE`).
// Example, storyline 08 (Crono + Marle): Crono is counted then locked by $7F01DF = 0x80; Marle is free; slot 2 is
// empty. $0F0B = 1, $0F0C = 0, sum 1 < 2 -> refuse.
#define MFORM_LOCKMASK(id) ((uint8_t)(0x80u >> ((id) & 7)))   // FF/F9BB[id]
#define MFORM_INSET  56    /* the Form panel's text column: MP_X + 56 = x 72 = tile col 9 */

void CtMenuFormRoster(const uint8_t* mem, const uint8_t* glb, int* activeFree, int* reserveN) {
  int free_ = 0, res = 0;
  if (mem) {
    for (int i = 0; i < 3; i++) {                      // C2/CD4A-CD72, the active party
      uint8_t a = mem[0x2980 + i];
      if (a & 0x80) continue;                          // C2/CD4F BMI: empty slot, no panel entry
      free_++;                                         // C2/CD51 INC $0F0B
      uint8_t lock = glb ? glb[0x01DF] : 0;            // C2/CD55 LDA $7F01DF (the $7F0000 flag page)
      if (lock & MFORM_LOCKMASK(a)) free_--;           // C2/CD59-CD5F: locked -> DEC $0F0B
    }
    if (mem[0x0D36] == 0)                              // C2/CD72 LDA $0D36 / BNE: skip the reserve scan
      for (int x = 3; x < 9; x++)                      // C2/CD81-CD8D: $2983..$2988
        if (!(mem[0x2980 + x] & 0x80)) res++;          // C2/CD96 STY $0F0C
  }
  if (activeFree) *activeFree = free_;
  if (reserveN)   *reserveN   = res;
}

// C2/CD2B's output: the 9-entry panel array $7E:0F02..$0F0A. 0x80 = an empty slot, 0x40|i = a member locked by the
// story ($7F:01DF & (0x80 >> charId)), i = selectable. The picker skips locked members too.
// $0D36 is 0xFF for the whole X menu (C2/989E `LDA #FF / STA $0D36` at the open), so C2/CD72 skips the reserve scan
// and entries 3..8 stay 0x80. Reserves belong to menu mode 5 / screen $07 (C2/C7A6), which the field opens on Y from
// storyline 0x49 (C0/1985 -> C0/19CE `LDA #05 / JSL C2/8000`).
void CtMenuFormPanels(const uint8_t* mem, const uint8_t* glb, uint8_t* panel9) {
  if (!panel9) return;
  for (int i = 0; i < 9; i++) panel9[i] = 0x80;
  if (!mem) return;
  // C2/CD43-CD6A runs two cursors: $00 is the roster slot and Y the panel position, advanced only for an occupied
  // slot (C2/CD4F BMI skips the STA/INY). So the list is compacted: the value is the roster slot, the position the
  // count of occupied slots so far. Field op 0xD6 (C0/3A07) can leave a hole by writing #80 into a slot in place.
  int y = 0;
  for (int i = 0; i < 3; i++) {
    uint8_t a = mem[0x2980 + i];
    if (a & 0x80) continue;                            // C2/CD4F BMI: empty, Y not advanced
    uint8_t lock = glb ? glb[0x01DF] : 0;
    panel9[y++] = (uint8_t)((lock & MFORM_LOCKMASK(a)) ? (0x40 | i) : i); // C2/CD64: value = slot, position = Y
  }
}

// C2/C632: the gate. 1 = the ROM opens screen 5; 0 = it refuses and returns to the ribbon.
int CtMenuFormCanOpen(const uint8_t* mem, const uint8_t* glb) {
  int f = 0, r = 0;
  CtMenuFormRoster(mem, glb, &f, &r);
  return (f + r) >= 2;
}

// The Save screen's entry gate (C2/CEDC + C2/9875, menu_open_items.md 4.1). At the menu open C2/9875 computes the
// save flag $0D34 from the location word $0100:
//   $0100 >= 0x1F0 (the overworld)      -> $0D34 = 0      (save allowed; C2/98A4 CMP #$01F0 / BCS)
//   field map with $7F01CE bit15 set    -> $0D34 = 0      (a save point; C2/98AC LDA $7F01CE / BMI)
//   field map otherwise                 -> $0D34 = 0xFFFF (C2/98B2 DEC $0D34, from the STZ)
// C2/CEDC then refuses (STZ $67 / $68 = 1, back to the ribbon) when bit 6 of the 8-bit OR ($0D34 | $0294 | $0299 |
// $029E) is set. $0D34 is computed here rather than read, since C2/9875 rewrites it at every open and the location
// cannot change while the field is frozen.
int CtMenuSaveCanOpen(const uint8_t* mem, const uint8_t* glb) {
  if (!mem) return 0;
  int loc = mem[0x0100] | (mem[0x0101] << 8);            // C2/98A1 LDA $0100 (the location word)
  int savePt = glb ? (glb[0x01CF] & 0x80) : 0;           // C2/98AC $7F01CE bit15 = byte $01CF bit7
  int d34 = (loc >= 0x01F0) ? 0x00 : (savePt ? 0x00 : 0xFF);
  int v = d34 | mem[0x0294] | mem[0x0299] | mem[0x029E]; // C2/CEDC LDA $0D34 / ORA / ORA / ORA
  return (v & 0x40) == 0;                                // C2/CEE8 AND #$40 / BEQ -> open
}

// ---- Item screen Use / Move / Organize (the ct-field-item-menu-use notes) ----
// Field-menu usable gate (C2/AB14): $7E7700[id] bit 1 (0x02). Consumables only (id >= 0xBC); the rest (equipment, key
// items, the battle-only Heal/Revive/Shelter) make the A press do nothing.
// C2/9969, the last step of C2/9875's $7700 build at every open: `LDA $0D34 / BEQ / LDA #02 / TRB $77C8` -- off a
// save point ($0D34 != 0, CtMenuSaveCanOpen's rule) the Shelter (C8) loses its field-usable bit. ctItemProp holds
// the table before that step (tools/gen/menu_itemuse.py takes C8's bit from $CC), so the step is applied here.
static int g_mItemShelterGate = 0;                       // 1 = $0D34 == 0 at the last menu open
void CtMenuItemOpenGate(const uint8_t* mem, const uint8_t* glb) {
  if (!mem) { g_mItemShelterGate = 0; return; }
  int loc = mem[0x0100] | (mem[0x0101] << 8);            // C2/98A1-98B2, as CtMenuSaveCanOpen
  int savePt = glb ? (glb[0x01CF] & 0x80) : 0;
  g_mItemShelterGate = (loc >= 0x01F0 || savePt) ? 1 : 0;
}
int CtMenuItemUsable(int id) {
  if (id < ITFX_BASE || id > 0xF2) return 0;
  int p = ctItemProp[id];
  if (id == 0xC8 && !g_mItemShelterGate) p &= ~0x02;    // C2/996E LDA #02 / TRB $77C8
  return (p & 0x02) ? 1 : 0;
}

// Apply a consumable's field effect to one char sheet ($7E2600 + charId*0x50), as the heal paths C2/8E82 (HP) and
// C2/8EAB (MP): the amount (HP: capped at 999) is added to the current pool and clamped to the max (+0x3F effective
// max HP / +0x09 max MP); full restores set the pool to its max. A Tab is C2/B200's own loop (CtMenuItemTabApply = C2/B2E0). The caller
// consumes the count separately (C2/87D5).
static void mItemApplyOne(uint8_t* mem, int idx, int charId) {
  uint8_t* sh = mem + 0x2600 + (size_t)(charId & 7) * 0x50;
  int kind = ctItemFxKind[idx], amt = ctItemFxAmt[idx], pool = ctItemFxPool[idx];
  if (kind == ITFX_HP || (kind == ITFX_FULL && (pool & 1))) {          // HP pool
    int cur = sh[3] | (sh[4] << 8), mx = sh[0x3F] | (sh[0x40] << 8);   // +3 curHP, +0x3F eff maxHP
    int nv = (kind == ITFX_FULL) ? mx : cur + (amt > 999 ? 999 : amt);
    if (nv > mx) nv = mx;
    sh[3] = nv & 0xFF; sh[4] = (nv >> 8) & 0xFF;
  }
  if (kind == ITFX_MP || (kind == ITFX_FULL && (pool & 2))) {          // MP pool
    // +7 curMP, capped at +0x09 (C2/8EBF/C2/8EC4). The MP arm has no 999 clamp: C2/8EAB has no `CMP #03E7`, unlike
    // the HP applier C2/8E82 (disasm/notes/menu2_tech_effect.md sec. 5.3).
    int cur = sh[7] | (sh[8] << 8), mx = sh[9] | (sh[10] << 8);
    int nv = (kind == ITFX_FULL) ? mx : cur + amt;
    if (nv > mx) nv = mx;
    sh[7] = nv & 0xFF; sh[8] = (nv >> 8) & 0xFF;
  }
}

// ---- The Tabs (C2/B200 -> C2/B228's loop -> C2/B2A7 -> C2/B2E0 -> C2/B31C) ----
// C2/B0E9 dispatches on the item's effect type; the sixth entry, C2/B200, is the Tabs': `$0DBD` = 1 for id CD (Power
// Tab), 6 for CE (Magic Tab), 2 for anything else there (CF, Speed Tab), then C2/AF42's overlay and C2/B228's loop --
// its own, not C2/B1A5's: the validity is C2/B275, the A runs C2/B2A7 (the pop-up) then C2/B2E0 (the stat) then C2/B31C.
// Returns that X, 0 = not a Tab.
int CtMenuItemTabIndex(int id) {
  int idx = id - ITFX_BASE;
  if (idx < 0 || idx >= ITFX_N || ctItemFxKind[idx] != ITFX_TAB) return 0;
  return id == 0xCD ? 1 : id == 0xCE ? 6 : 2;                          // C2/B202-B213
}
// C2/B275, every loop frame: `$0DBC` = 2 while the target's stat is below the Tab's cap, else 0 (the A buzzes, C2/B23D).
// C2/8820 copies the target's sheet to $9A90; the stat is `$9A9B + FF:CF2B[X]` = sheet + 0x0B + FF:CF2B[X] and the cap
// FF:CF32[X] (X 1 = Power 0x63, 2 = Speed 0x10, 6 = Magic 0x63). `$0DBD` = 0 (the slot used up: C2/B341) -> 0.
int CtMenuItemTabMask(const uint8_t* rom, long romLen, const uint8_t* mem, int absSlot, int tgtSlot) {
  if (!rom || romLen < 0x3FCF3AL || !mem || absSlot < 0 || absSlot >= 0xF2 || tgtSlot < 0 || tgtSlot > 2) return 0;
  int x = CtMenuItemTabIndex(mem[0x2400 + absSlot]);
  if (!x || !mem[0x2500 + absSlot]) return 0;
  int pid = mem[0x2980 + tgtSlot];
  if (pid & 0x80) return 0;
  const uint8_t* sh = mem + 0x2600 + (size_t)(pid & 7) * 0x50;
  return sh[0x0B + CtRomByte(rom, 0x400000L, (long)(0x3FCF2BL + x))] < CtRomByte(rom, 0x400000L, (long)(0x3FCF32L + x)) ? 2 : 0;    // C2/B293-B2A3
}
// C2/9137 on a sheet whose working copy C2/8820 just re-took: C2/916E's eleven derived bytes to +0x36..+0x40 and its
// clamped current HP to +3 (no C2/813C: the HP bar's scale is not this routine's).
static void mSheet9137(uint8_t* sh) {
  uint8_t d[11]; CtMenuRecalcDerived(sh, d);
  for (int i = 0; i < 7; i++) sh[0x36 + i] = d[i];
  sh[0x3D] = d[7]; sh[0x3E] = d[8]; sh[0x3F] = d[9]; sh[0x40] = d[10];
  int cur = sh[3] | (sh[4] << 8), mx = d[9] | (d[10] << 8);
  if (cur > mx) { sh[3] = (uint8_t)(mx & 0xFF); sh[4] = (uint8_t)(mx >> 8); }
}
// C2/B2E0, after C2/8F6C's pop-up (C2/B2D6 returns only when C2/9016 is over): with M = 16 `INC $000B,X` -- the word at
// sheet + 0x0B + FF:CF2B[X], the stat the recompute starts from -- then the byte at sheet + 0x2F + C2:B315[X] (60 00 02
// 01 03 04 05: Power 0x2F, Speed 0x31, Magic 0x34) + 1 unless it is already 0x63, then C2/8820 + C2/9137. It lands 42
// frames after the A, with the count's C2/87D5, like the restores.
void CtMenuItemTabApply(const uint8_t* rom, long romLen, uint8_t* mem, int tgtSlot, int x) {
  if (!rom || romLen < 0x3FCF3AL || !mem || x < 1 || x > 6 || tgtSlot < 0 || tgtSlot > 2) return;
  int pid = mem[0x2980 + tgtSlot];
  if (pid & 0x80) return;
  uint8_t* sh = mem + 0x2600 + (size_t)(pid & 7) * 0x50;
  int o = 0x0B + CtRomByte(rom, 0x400000L, (long)(0x3FCF2BL + x));
  int w = (sh[o] | (sh[o + 1] << 8)) + 1;                              // C2/B2FC INC $000B,X (16-bit)
  sh[o] = (uint8_t)(w & 0xFF); sh[o + 1] = (uint8_t)((w >> 8) & 0xFF);
  int b = 0x2F + CtRomByte(rom, 0x400000L, (long)(0x2B315L + x));
  if (sh[b] < 0x63) sh[b]++;                                           // C2/B303-B30B
  mSheet9137(sh);                                                      // C2/B30E JSR C2/8820 / C2/B311 JSR C2/9137
}

// C2/8E2D, the effect half of a use (C2/8F55 runs it on the A; C2/B31C consumes 42+ frames later). `popAmt[k]` = the
// numbers C2/9016 floats over party slot k: [0] HP = the item's amount capped at 999 (C2/8E82 `CMP #03E7`) or the max
// when it restores to full ($0DBF bit 6) -- not the HP actually gained --, [1] MP = its amount or the max (C2/8EAB);
// -1 = none. Returns $0F5D: bit 0 = an HP number, bit 1 = an MP number.
int CtMenuItemApply(uint8_t* mem, int absSlot, int targetSlot, int popAmt[3][2]) {
  for (int k = 0; k < 3; k++) popAmt[k][0] = popAmt[k][1] = -1;
  if (!mem || absSlot < 0 || absSlot >= 0xF2) return 0;
  int id = mem[0x2400 + absSlot];
  if (!CtMenuItemUsable(id)) return 0;
  int idx = id - ITFX_BASE;
  if (idx < 0 || idx >= ITFX_N || ctItemFxKind[idx] == ITFX_NONE || ctItemFxKind[idx] == ITFX_TAB) return 0;
  int kind = ctItemFxKind[idx], amt = ctItemFxAmt[idx], pool = ctItemFxPool[idx], bits = 0;
  for (int m = 0; m < 3; m++) {
    if (!ctItemFxAll[idx] && m != (targetSlot & 3)) continue;
    int pid = mem[0x2980 + m];
    if (pid & 0x80) continue;
    const uint8_t* sh = mem + 0x2600 + (size_t)(pid & 7) * 0x50;
    if (kind == ITFX_HP || (kind == ITFX_FULL && (pool & 1))) {
      popAmt[m][0] = (kind == ITFX_FULL) ? (sh[0x3F] | (sh[0x40] << 8)) : amt;
      if (popAmt[m][0] > 999) popAmt[m][0] = 999;
      bits |= 1;
    }
    if (kind == ITFX_MP || (kind == ITFX_FULL && (pool & 2))) {
      popAmt[m][1] = (kind == ITFX_FULL) ? (sh[9] | (sh[10] << 8)) : amt;
      bits |= 2;
    }
    mItemApplyOne(mem, idx, pid & 7);
  }
  return bits;
}
// C2/B31C's C2/87D5: one unit of the used slot, the id zeroed in place at 0.
void CtMenuItemConsume(uint8_t* mem, int absSlot) {
  if (!mem || absSlot < 0 || absSlot >= 0xF2) return;
  int c = mem[0x2500 + absSlot];
  if (c > 0) c--;
  mem[0x2500 + absSlot] = c & 0xFF;
  if (c == 0) mem[0x2400 + absSlot] = 0;
}
// Use a consumable from the item menu: apply its effect to `targetSlot` (party slot 0..2) or the whole party
// (all-party items), then decrement the source slot and zero it in place at 0. `absSlot` = the inventory slot holding
// the item ($0F00). Returns 1 if consumed. Heal C2/8E82 + consume C2/87D5.
int CtMenuItemUse(uint8_t* mem, int absSlot, int targetSlot) {
  if (!mem || absSlot < 0 || absSlot >= 0xF2) return 0;
  int id = mem[0x2400 + absSlot];
  if (!CtMenuItemUsable(id)) return 0;
  int idx = id - ITFX_BASE;
  if (idx < 0 || idx >= ITFX_N || ctItemFxKind[idx] == ITFX_NONE) return 0;
  if (CtMenuItemTabIndex(id)) return 0;                                // a Tab: C2/B2E0 needs the ROM (CtMenuItemTabApply)
  if (ctItemFxAll[idx]) {                                              // all-party (scope bit3, $0DBD)
    for (int m = 0; m < 3; m++) { int pid = mem[0x2980 + m]; if (pid & 0x80) continue; mItemApplyOne(mem, idx, pid & 7); }
  } else {
    int pid = mem[0x2980 + (targetSlot & 3)];
    if (pid & 0x80) return 0;                                         // empty target slot: no-op
    mItemApplyOne(mem, idx, pid & 7);
  }
  int c = mem[0x2500 + absSlot];                                      // consume 1 (C2/87D5, $04CB=1)
  if (c > 0) c--;
  mem[0x2500 + absSlot] = c & 0xFF;
  if (c == 0) mem[0x2400 + absSlot] = 0;                              // count 0 -> zero the id slot in place
  return 1;
}

// ---- Equip change (screen 1, substate 3; disasm/notes/menu2_equip_change.md) ----
// C2/9C29 = { C2/9C31 build, C2/9C39 char select, C2/9C70 gear-slot select, C2/9CA5 candidate list }. A on a gear
// row enters the list when the candidate count $0FC6 is non-zero and buzzes (id $01, C2/9C8D JMP C2/EACC) when it is
// zero.

// C2/87FA: find the slot holding `id` with a non-zero count. Returns the slot or -1.
//   87FA LDX #00 / 87FC CMP $2400,X / BNE / 8801 LDA $2500,X / BNE found / 8806 LDA $04C9 / 8809 INX / BNE loop
//   880C SEC / RTS                      -- X is 8-bit (both callers SEP #30): all 256 slots, not the bag's $F2
//   880E STX $04C7 / 8811 STZ $04C8 / 8814 CLC / RTS
// The found arm records the slot: $04C7 = the slot, $04C8 = 0.
static int mInvFind(uint8_t* mem, int id) {
  for (int x = 0; x < 0x100; x++)
    if (mem[0x2400 + x] == (uint8_t)id && mem[0x2500 + x]) { mem[0x04C7] = (uint8_t)x; mem[0x04C8] = 0; return x; }
  return -1;
}

int CtMenuInvAdd(uint8_t* mem, int id, int qty) {            // C2/8791
  if (!mem || !id) return 0;                                 // C2/8795: id 0 -> nothing, A = 0
  if (id == 0x5A || id == 0x7B || id == 0x94) return id;     // C2/879A/879E/87A2: the three "none" sentinels
                                                             // are never added -- the engine's whole unequip
                                                             // rule. A keeps the id.
  int x = mInvFind(mem, id);                                 // C2/87A6
  if (x < 0) {                                               // C2/87AB: first slot with id 0, from X = 0 (87FA's
    for (x = 0; x < 0x100 && mem[0x2400 + x]; x++) {}        //   loop wrapped there) to the 8-bit X's wrap
    if (x >= 0x100) return 0xFF;                             // C2/87B3 inventory full
    mem[0x2400 + x] = (uint8_t)id; mem[0x2500 + x] = 0;      // C2/87B7
  }
  int n = (mem[0x2500 + x] + qty) & 0xFF;                    // C2/87C0 LDA / CLC / ADC $04CB, 8-bit
  mem[0x2500 + x] = (uint8_t)(n >= 0x63 ? 0x63 : n);         // C2/87C7 CMP #63 / BCC: the stack cap is 99
  return 0;
}

int CtMenuInvRemove(uint8_t* mem, int id, int qty) {         // C2/87D5
  if (!mem || !id) return 0;
  int x = mInvFind(mem, id);
  if (x < 0) return 0xFF;                                    // not held
  int n = mem[0x2500 + x] - qty;
  if (n < 0) return 0xFE;                                    // C2/87E5 borrow -> not enough, no write
  mem[0x2500 + x] = (uint8_t)n;
  if (n == 0) mem[0x2400 + x] = 0;                           // C2/87EE STZ $2400,X
  return 0;
}

int CtMenuEquipCandidates(const uint8_t* mem, int gearSlot, int charId, uint8_t* cand, uint8_t* cnt) {
  if (!mem || !cand || !cnt || gearSlot < 0 || gearSlot > 3) return 0;
  int lo = ctEqRange[gearSlot * 2], hi = ctEqRange[gearSlot * 2 + 1];   // C2/89DC, interleaved [lo,hi)
  int bit = 0x80 >> (charId & 7);                                       // FF/F9BB
  int n = 0;
  for (int i = 0; i < 0xF2; i++) {                                      // C2/8953: walk the whole bag
    int id = mem[0x2400 + i];
    if (!id || id < lo || id >= hi) continue;
    if (!(ctEqMask[id] & bit)) continue;                                // C2/93AF: equippable by this char
    cand[n] = (uint8_t)id; cnt[n] = mem[0x2500 + i]; n++;
  }
  cand[n] = 0;
  // C2/899C's selection sort, as the ROM's two loops run:
  //   outer  289A2: LDY $0F00,X / BEQ done            -- a zero id ends the list
  //          289A7: LDA $7C00,Y / STA $00             -- the running key
  //   inner  289AA: INX / LDY $0F00,X / LDA $7C00,Y
  //          289B1: BEQ ->289D6                       -- a zero key ends the inner scan
  //          289B3: CMP $00 / BCC ->289AA             -- smaller: skip
  //          289B7: STA $00 + swap                    -- >= : it becomes the running key
  // Each pass leaves the maximum of the tail at the front, so the sort is descending (the strongest piece is row 0).
  // Note: $7C00 is built only for ids 0x00..0x93 (C2/994C stops at #94), so every accessory key is 0 and the inner
  // scan stops at once: accessory rows keep bag order. The key is read as 0 for those ids (ctEqPower has 148 entries).
  for (int a = 0; a < n; a++) {
    if (!cand[a]) break;
    int key = (cand[a] < 0x94) ? ctEqPower[cand[a]] : 0;
    for (int b = a + 1; b <= n; b++) {
      int k2 = (b < n && cand[b] && cand[b] < 0x94) ? ctEqPower[cand[b]] : 0;
      if (!k2) break;                                  // C2/89B1: the zero-key terminator
      if (k2 < key) continue;                          // C2/89B5 BCC
      key = k2;
      { uint8_t t = cand[a]; cand[a] = cand[b]; cand[b] = t;
        t = cnt[a]; cnt[a] = cnt[b]; cnt[b] = t; }
    }
  }
  return n;
}

void CtMenuRecalcDerived(const uint8_t* sheet, uint8_t* out11) {        // C2/916E + the C2/9137 tail
  if (!sheet || !out11) return;
  int bon[7] = { 0, 0, 0, 0, 0, 0, 0 }, wpnPow = 0, defSum = 0;
  int curHP = sheet[3] | (sheet[4] << 8);
  int hpMax = sheet[5] | (sheet[6] << 8);
  for (int pass = 3; pass >= 0; pass--) {                               // accessory, helm, armor, weapon
    int off = (pass == 3) ? 0x2A : (pass == 2) ? 0x27 : (pass == 1) ? 0x28 : 0x29;
    int id = sheet[off];
    if (pass == 3) {                                                    // C2/9295: the accessory scales maxHP
      int add = (id == 0xA1) ? (hpMax >> 1) : (id == 0xA0) ? (hpMax >> 2) : 0;
      hpMax += add; if (hpMax > 999) hpMax = 999;
      if (curHP > hpMax) curHP = hpMax;
    } else if (pass == 0) wpnPow = ctEqPower[id];                       // C2/9276
    else                  defSum += ctEqPower[id];                      // C2/9281 / C2/9286
    int b = ctEqBonusIdx[id];                                           // C2/92C4: the bonus-set index
    if (b) {
      unsigned w = ctEqBonusSet[b] & 0xFFFE;
      int mask = w & 0xFF, amt = (int)(w >> 8);
      for (int k = 0; mask; k++) {                                      // walked from bit7 downward
        if (mask & 0x80) bon[ctEqStatOff[k] & 7] += amt;
        mask = (mask << 1) & 0xFF;
      }
    }
  }
  for (int k = 6; k >= 0; k--) {                                        // 7 capped stats
    int y = ctEqStatOff[k] & 7;
    int v = sheet[0x0B + y] + bon[y];
    if (v > ctEqStatCap[k]) v = ctEqStatCap[k];                         // Note: the cap is indexed by k
    out11[y] = (uint8_t)v;
  }
  int atk;                                                              // C2/91DB, per character class
  switch (sheet[0] & 7) {
    case 1: case 2: atk = (2 * (out11[4] + wpnPow)) / 3;                              break;  // Marle, Lucca
    case 5:         atk = (16 * out11[0] + ctEqAyla[sheet[0x12] % 100]) / 9;          break;  // Ayla
    default:        atk = (5 * wpnPow + 12 * out11[0]) / 9;                           break;  // Crono/Robo/Frog/Magus
  }
  out11[7] = (uint8_t)(atk > 255 ? 255 : atk);                          // +0x3D Attack
  { int df = out11[1] + defSum; out11[8] = (uint8_t)(df > 255 ? 255 : df); }   // +0x3E Defence
  if (sheet[0x2A] == 0xA8) out11[5] = (uint8_t)(out11[5] << 1);         // C2/9142: the Third Eye doubles Evade,
                                                                        // in the caller, i.e. after the cap
  out11[9] = (uint8_t)(hpMax & 0xFF); out11[10] = (uint8_t)(hpMax >> 8);
  (void)curHP;
}

// Write the 11 derived bytes back where C2/9137 puts them: the 7 stats at +0x36..+0x3C, Attack +0x3D, Defence +0x3E,
// the effective max HP word +0x3F/+0x40 -- and clamp curHP (+0x03) to it.
void CtMenuEquipWriteBack(uint8_t* mem, int charId) {
  if (!mem) return;
  uint8_t* sh = mem + 0x2600 + (size_t)(charId & 7) * 0x50;
  uint8_t d[11]; CtMenuRecalcDerived(sh, d);
  for (int i = 0; i < 7; i++) sh[0x36 + i] = d[i];
  sh[0x3D] = d[7]; sh[0x3E] = d[8]; sh[0x3F] = d[9]; sh[0x40] = d[10];
  int cur = sh[3] | (sh[4] << 8), mx = d[9] | (d[10] << 8);
  if (cur > mx) { sh[3] = (uint8_t)(mx & 0xFF); sh[4] = (uint8_t)(mx >> 8); }
  mem[0x0D38 + (charId & 7) * 2] = (uint8_t)(mx / 10);                  // C2/813C: the HP-bar scale
  mem[0x0D39 + (charId & 7) * 2] = 0;
}

// The menu open's bag and sheet pass: C2/8139 `JSR C2/9875`, in the shell's init chain (menu_shell_timing.md
// sec. 2.2):
//   C2/9878 JSR C2/9988: every bag slot whose count is 0, or whose id is 0, $5A, $7B, $94 (the three "none"
//            sentinels) or >= $F2, is zeroed, id and count;
//   C2/9977 JSR C2/99B2: for charId 0..6, C2/8816 (the sheet -> the $9A90 working copy, $6F = the sheet) and C2/9137
//            (C2/916E's recalc, then `$9A93 -> +$03` and the 11 derived bytes $9B1C -> +$36..+$40).
// So every menu opens with each sheet's derived block recomputed, including +$3F/+$40, the effective max HP the item
// appliers clamp to (C2/8E9E `CMP $003F,Y`); outside the menu that word can hold something else.
// C2/9895-98B2, the head of C2/9875 (A 16-bit): `STZ $0D32 / STZ $0D34`, `$0D36 = FFFF`, then on a field map ($0100 <
// $01F0) `DEC $0D32` -- FFFF -- and, off a save point ($7F01CE bit 15 clear), `DEC $0D34` too. $0D32 picks the song
// screen $07's C2/CB96 keeps (the field's $29AE, the overworld's $02AE); $0D36 is the Form's reserve gate (screen $07
// sets it to $71).
void CtMenuOpenFlags(uint8_t* mem, const uint8_t* glb) {
  if (!mem) return;
  mem[0x0D32] = mem[0x0D33] = 0; mem[0x0D34] = mem[0x0D35] = 0;      // 29895 / 29898
  mem[0x0D36] = mem[0x0D37] = 0xFF;                                 // 2989B-2989E
  if (((mem[0x0100] | (mem[0x0101] << 8)) & 0xFFFF) < 0x01F0) {    // 298A1-298A7
    mem[0x0D32] = mem[0x0D33] = 0xFF;                               // 298A9 DEC $0D32
    if (!(glb && (glb[0x01CF] & 0x80))) mem[0x0D34] = mem[0x0D35] = 0xFF;   // 298AC-298B2
  }
}
void CtMenuOpenSheets(uint8_t* mem) {
  if (!mem) return;
  for (int x = 0; x < 0x100; x++) {                                    // C2/9988, X 8-bit: the whole 256-slot page
    int id = mem[0x2400 + x];
    if (!mem[0x2500 + x] || !id || id == 0x5A || id == 0x7B || id == 0x94 || id >= 0xF2) {
      mem[0x2400 + x] = 0; mem[0x2500 + x] = 0;
    }
  }
  for (int c = 0; c < 7; c++) CtMenuEquipWriteBack(mem, c);            // C2/99B2 -> C2/8816 + C2/9137
}

int CtMenuEquipApply(uint8_t* mem, uint8_t* glb, int charId, int gearSlot, int newId) {  // C2/9CC1
  if (!mem || gearSlot < 0 || gearSlot > 3) return 0;
  uint8_t* sh = mem + 0x2600 + (size_t)(charId & 7) * 0x50;
  int off = ctEqSheetOff[gearSlot];                                     // C2/A4AB
  int old = sh[off];
  CtMenuInvAdd(mem, old, 1);           // C2/9CCE: the "full" return is ignored, as in the ROM
  sh[off] = (uint8_t)newId;
  CtMenuInvRemove(mem, newId, 1);      // C2/9CD9
  if (gearSlot == 0 && glb && (charId & 7) < 6) glb[0x00B4 + (charId & 7)] = 0;   // C2/9CFC: a weapon change
  CtMenuEquipWriteBack(mem, charId);   // the tail: recompute + rewrite +0x03/+0x36..+0x40 + the HP scale
  return 1;
}

// The stored-description table's shape, for --desctest: the entry count and how many the ROM fills. ctItemDesc lives
// in this translation unit only.
int CtMenuDescTable(int* nonEmptyOut) {
  int n = (int)(sizeof ctItemDesc / sizeof ctItemDesc[0]), ne = 0;
  for (int k = 0; k < n; k++) if (ctItemDesc[k][0]) ne++;
  if (nonEmptyOut) *nonEmptyOut = ne;
  return n;
}

// The gear description line: the same stored table as an item's ($CC:2EB1, one id space; see mItemDesc). Its words
// are the ROM's (Vigor/Strike/Magic/Speed); the bonus mask (ctEqBonusIdx/ctEqBonusSet) is the source of the effect,
// not of the text. Returns the length written, 0 if this id has no line.
int CtMenuEquipDesc(int id, char* out, int cap) {
  if (out && cap > 0) out[0] = 0;
  if (!out || cap <= 0 || id < 0 || id >= (int)(sizeof ctItemDesc / sizeof ctItemDesc[0])) return 0;
  if (!ctItemDesc[id][0]) return 0;
  snprintf(out, (size_t)cap, "%s", ctItemDesc[id]);
  return (int)strlen(out);
}

// C2/E92D, at the top of every frame: all OAM hidden, then `LDA #FF / TRB $0D9B / BNE` -- the nav hand ($0770 at $54's
// record) is drawn only when $0D9B was 0, and $0D9B is cleared. C2/8AFD (an all-party item's hands) sets it to 20 on
// each run, so the nav hand is gone on every frame after one, and back a frame late after the B, whose frame still ran
// it.
void CtMenuSetNavHandShown(int on) { mNavHandOk = on; }
// $0DBD bit 3 (the item's scope, $CC:1BB5): an all-party item. C2/B19B `LDA $0DBD / AND #08 / BEQ / JSR C2/8AD5`.
int CtMenuItemAllParty(const uint8_t* mem, int absSlot) {
  if (!mem || absSlot < 0 || absSlot >= 0xF2) return 0;
  int idx = mem[0x2400 + absSlot] - ITFX_BASE;
  return (idx >= 0 && idx < ITFX_N) ? ctItemFxAll[idx] : 0;
}
// C2/B870: $7E:0DBC, the target validity mask the Item overlay recomputes every frame: bit 1 = this target's HP can be
// restored, bit 0 = its MP can. A with the mask zero is refused (C2/B1B5 `LDA $0DBC / BNE` -> C2/B1BA `JSR C2/EACC`,
// the buzz) and nothing is consumed. Note the ROM's asymmetry: the HP test is a 16-bit compare of +3 against the
// effective max +0x3F, the MP test an 8-bit compare of +7 against +9. The HP/MP/all-party bits come from the baked
// effect tables CtMenuItemUse applies (the ROM reads them from $0DBD, rebuilt by C2/B31C from the item record).
int CtMenuItemTargetMask(const uint8_t* mem, int absSlot, int tgtSlot) {
  if (!mem || absSlot < 0 || absSlot >= 0xF2) return 0;
  int id = mem[0x2400 + absSlot];
  int idx = id - ITFX_BASE;
  if (idx < 0 || idx >= ITFX_N || ctItemFxKind[idx] == ITFX_NONE) return 0;
  int kind = ctItemFxKind[idx], pool = ctItemFxPool[idx];
  int doHp = (kind == ITFX_HP) || (kind == ITFX_FULL && (pool & 1));       // $0DBD bit7
  int doMp = (kind == ITFX_MP) || (kind == ITFX_FULL && (pool & 2));       // $0DBD bit6
  if (!doHp && !doMp) return 0;                                            // a Tab / status item: C2/B870
                                                                           //   leaves the mask at 0
  int all = ctItemFxAll[idx];                                              // $0DBD bit3
  int n = 1, i = tgtSlot;
  if (all) { n = 0; for (int k = 0; k < 3; k++) if (!(mem[0x2980 + k] & 0x80)) n++; i = 0; }
  for (; n-- > 0; i++) {
    if (i < 0 || i > 2) break;
    int pid = mem[0x2980 + i];
    if (pid & 0x80) continue;                                              // an empty slot restores nothing
    const uint8_t* sh = mem + 0x2600 + (size_t)(pid & 7) * 0x50;
    int m = 0;
    if (doHp && (sh[3] | (sh[4] << 8)) < (sh[0x3F] | (sh[0x40] << 8))) m = 1;   // C2/B8AC, 16-bit
    m <<= 1;                                                                    // C2/B8B9 ASL
    if (doMp && sh[7] < sh[9]) m |= 1;                                          // C2/B8C3, 8-bit
    if (m) return m;                                                            // C2/B8CE
  }
  return 0;
}

// ---- The field restore, C2/8F55, shared by a tech cast and an item use (disasm/notes/menu2_tech_effect.md) ----
// C2/8F55 has two callers, C2/B1BF (the Item screen) and C2/B4FC (the Tech screen); inside it only $0DBF differs.

// C2/8ECC, the amount: six words ($22/$24/$26 = HP for party index 0/1/2, $28/$2A/$2C = MP). $0DBF bit 7 picks the arm:
//   set   (every item: $0DBF is the constant $B2 for an HP item / $8A for an MP item, C2/B17F)
//         -> flat, the same for all six: ($0DBF & 0x7F) * ($0DC0 & 0x0F)            <- four bits here
//   clear (every tech: $0DBF is the caster's derived Magic, sheet +0x39, C2/B469)
//         -> magic-scaled with a per-target spread, the six rolls taken in reverse:
//            amount[k] = ((($0DBF + 1) & 0xFF) * rnd[cur + (5 - k)]) >> 8  +  $0DBF * ($0DC0 & 0x1F)
//            rnd = the 256-byte permutation at C0/FE00, cursor $7E:0D00; the index is not masked, so at
//            cursor >= 0xFB the ROM reads on into C0/FF00 (the baked table carries that tail).  <- five bits here
// $0DC0 bit 7 negates every amount (C2/8F41's BIT is 16-bit, so it tests $0DC0, not $0DBF): the damage arm, which no
// field tech or item reaches.
void CtMenuRestoreAmounts(int amtSrc, int nib, unsigned cur, int* out6) {
  if (!out6) return;
  if (amtSrc & 0x80) {                                        // C2/8EDA, the flat arm
    int v = (amtSrc & 0x7F) * (nib & 0x0F);
    for (int i = 0; i < 6; i++) out6[i] = v;
  } else {                                                    // C2/8EF9, the magic + RNG arm
    int base = amtSrc * (nib & 0x1F);
    for (int k = 0; k < 6; k++) {
      unsigned ix = cur + (unsigned)(5 - k);                  // no & 0xFF: the over-read is the ROM's
      int r = ctTechRng[ix < sizeof ctTechRng ? ix : (ix % sizeof ctTechRng)];
      out6[k] = ((((amtSrc + 1) & 0xFF) * r) >> 8) + base;
    }
  }
  if (nib & 0x80) for (int i = 0; i < 6; i++) out6[i] = (-out6[i]) & 0xFFFF;   // C2/8F41
}

// C2/8E82 (HP) / C2/8EAB (MP), one pool of one character. `full` = $0DC0 bit 6 -> the amount is the max. The two
// differ in two places: the HP one clamps the shown amount to 999 (`CMP #03E7`) and the MP one does not, and the MP
// maximum is the sheet's +0x09/+0x0A. Returns what is displayed (before the clamp to the max).
static int mPoolApply(uint8_t* mem, int charId, int mp, int amount, int full) {
  uint8_t* sh = mem + 0x2600 + (size_t)(charId & 7) * 0x50;
  int curO = mp ? 7 : 3, maxO = mp ? 9 : 0x3F;
  int cur = sh[curO] | (sh[curO + 1] << 8), mx = sh[maxO] | (sh[maxO + 1] << 8);
  int shown = full ? mx : amount;
  if (!mp && shown > 999) shown = 999;                        // C2/8E97, HP only
  int nv = cur + shown;
  if (nv > mx) nv = mx;
  if (nv < 0) nv = 0;
  sh[curO] = (uint8_t)(nv & 0xFF); sh[curO + 1] = (uint8_t)((nv >> 8) & 0xFF);
  return shown;
}

// The nine ids FF/F82E marks usable outside battle -- 09 0C 0F 1A 1D 21 24 27 29, all single techs, so no double or
// triple is field-castable (C2/B401 `LDA $7700,X / BMI`). Note: FF/F876's "next tech to learn" marker writes
// (prop & 0x1E) | 0x40 over the same byte, i.e. bit 7 clear, so a tech the character has not learned yet is listed
// (dim) and refused.
int CtMenuTechFieldUsable(const uint8_t* mem, int charId, int techId) {
  static const int kNine[9] = { 0x09, 0x0C, 0x0F, 0x1A, 0x1D, 0x21, 0x24, 0x27, 0x29 };
  int ok = 0;
  for (int i = 0; i < 9; i++) if (kNine[i] == techId) { ok = 1; break; }
  if (!ok) return 0;
  uint8_t bits[37], nxt[7];
  CtMenuTechBits(mem, bits, nxt);                             // FF/F876's preview marker clears bit7
  return nxt[charId & 7] != (uint8_t)techId;
}

// $C2:B519[charId]: the caster pose the cast arms in $19E1. Seven entries, one per character (Ayla, charId 5, owns
// the field-usable tech $29).
int ctTechCastPoseByte(int charId) { return ctTechCastPose[charId & 7]; }

// C2/B870 for a tech: the same two-bit validity mask over the same pools, with the HP/MP bits from the tech's $0DBD
// (ctTechFx), plus C2/B839's rule (the item path has none): once the caster's MP is below the cost, $0DBC is STZ'd and
// every further A buzzes.
int CtMenuTechTargetMask(const uint8_t* mem, int casterCharId, int techId, int tab, int tgtSlot) {
  if (!mem || techId < 0 || techId >= 0x75) return 0;
  const uint8_t* csh = mem + 0x2600 + (size_t)(casterCharId & 7) * 0x50;
  if (csh[7] < CtMenuTechMp(casterCharId, techId, tab, csh[0x2A])) return 0;   // C2/B839
  int fx = ctTechFx[techId];
  int n = 1, i = tgtSlot;
  if (fx & 0x08) { n = 0; for (int k = 0; k < 3; k++) if (!(mem[0x2980 + k] & 0x80)) n++; i = 0; }
  for (; n-- > 0; i++) {
    if (i < 0 || i > 2) break;
    int pid = mem[0x2980 + i];
    if (pid & 0x80) continue;
    const uint8_t* sh = mem + 0x2600 + (size_t)(pid & 7) * 0x50;
    int m = 0;
    if ((fx & 0x80) && (sh[3] | (sh[4] << 8)) < (sh[0x3F] | (sh[0x40] << 8))) m = 1;   // C2/B8AC
    m <<= 1;
    if ((fx & 0x40) && sh[7] < sh[9]) m |= 1;                                          // C2/B8C3
    if (m) return m;
  }
  return 0;
}

// C2/B3E6's gate ladder + C2/8F55's effect. Returns 1 when the tech was cast (the caller then plays the 10-frame
// pre-roll, the chime cmd $19 id $06/$07 and the popup), 0 when it is refused (the caller plays buzz id $01). shown6
// receives what the popup floats: [0..2] HP per party index, [3..5] MP. `rngCur` is $7E:0D00, which the menu advances
// once per frame (the whole menu, not just this screen).
int CtMenuTechUse(uint8_t* mem, int casterCharId, int techId, int tab, int tgtSlot,
                  unsigned rngCur, int* shown6) {
  if (shown6) for (int i = 0; i < 6; i++) shown6[i] = -1;
  if (!mem || techId < 0 || techId >= 0x75) return 0;
  if (!CtMenuTechFieldUsable(mem, casterCharId, techId)) return 0;     // C2/B401
  uint8_t* csh = mem + 0x2600 + (size_t)(casterCharId & 7) * 0x50;
  int cost = CtMenuTechMp(casterCharId, techId, tab, csh[0x2A]);       // C2/B427 -> C2/BC22
  if (csh[7] < cost) return 0;                                         // C2/B42D, an 8-bit compare
  int fx = ctTechFx[techId], nib = ctTechNib[techId];                  // $0DBD / $0DC0
  int amt[6]; CtMenuRestoreAmounts(csh[0x39], nib, rngCur, amt);       // $0DBF = the derived Magic
  int start = (fx & 0x08) ? 0 : tgtSlot, count = 1;                    // C2/8E2D: bit 3 = all party
  if (fx & 0x08) { count = 0; for (int k = 0; k < 3; k++) if (!(mem[0x2980 + k] & 0x80)) count++; }
  for (int i = 0; i < count; i++) {
    int slot = start + i;
    if (slot < 0 || slot > 2) break;
    int pid = mem[0x2980 + slot];
    if (pid & 0x80) continue;
    if (fx & 0x80) { int s = mPoolApply(mem, pid & 7, 0, amt[slot], nib & 0x40); if (shown6) shown6[slot] = s; }
    if (fx & 0x40) { int s = mPoolApply(mem, pid & 7, 1, amt[3 + slot], nib & 0x40); if (shown6) shown6[3 + slot] = s; }
  }
  return 1;                                                            // (the MP is C2/B50C's, after the popup)
}
// C2/B502-B516, after C2/8F55 has returned (the popup over): `LDA $0007,X / SEC / SBC $0F4F / STA $0007,X` on the
// caster's live sheet, 8-bit, no borrow.
void CtMenuTechPay(uint8_t* mem, int casterCharId, int techId, int tab) {
  if (!mem || techId < 0) return;
  uint8_t* csh = mem + 0x2600 + (size_t)(casterCharId & 7) * 0x50;
  csh[7] = (uint8_t)((csh[7] - CtMenuTechMp(casterCharId, techId, tab, csh[0x2A])) & 0xFF);
}

// Move: swap two inventory slots (ids and counts), C2/AAE3. a = $0F00 (lifted), b = $0D26 (destination). Empty slots
// take part normally (a 0 is left behind; the list is not compacted).
void CtMenuItemSwap(uint8_t* mem, int a, int b) {
  if (!mem || a < 0 || a >= 0xF2 || b < 0 || b >= 0xF2 || a == b) return;
  uint8_t ti = mem[0x2400 + a]; mem[0x2400 + a] = mem[0x2400 + b]; mem[0x2400 + b] = ti;  // swap ids  (C2/AAE3)
  uint8_t tc = mem[0x2500 + a]; mem[0x2500 + a] = mem[0x2500 + b]; mem[0x2500 + b] = tc;  // swap counts (C2/AAF1)
}

// Organize: rebuild $2400/$2500 in the ROM's order, merging duplicate stacks (cap 99) and compacting out empties
// (C2/AB62): accumulate presence/count per id, then emit consumables (BC..CF, non-key) -> key items (bit 3,
// ascending) -> weapons (00-59) -> helmets (5A-7A) -> armor (7B-93) -> accessories (94-BB), ascending id within each
// range; zero-fill the tail.
static void mOrgEmit(uint8_t* mem, int* present, int* cnt, int* py, int gstart, int glen) {
  for (int id = gstart; id < gstart + glen && id <= 0xF2; id++) {
    if (!present[id] || (ctItemProp[id] & 0x08)) continue;            // skip absent and key items (bit 3)
    if (*py < 0xF2) { mem[0x2400 + *py] = (uint8_t)id; mem[0x2500 + *py] = (uint8_t)cnt[id]; (*py)++; }
    present[id] = 0;
  }
}
void CtMenuItemOrganize(uint8_t* mem) {
  if (!mem) return;
  int present[0x100] = { 0 }, cnt[0x100] = { 0 };
  for (int i = 0; i < 0xF2; i++) {                                    // accumulate (C2/AB87): merge stacks, cap 99
    int id = mem[0x2400 + i]; if (!id) continue;
    present[id] = 1; cnt[id] += mem[0x2500 + i]; if (cnt[id] > 99) cnt[id] = 99;
  }
  int y = 0;
  mOrgEmit(mem, present, cnt, &y, ctOrgStart[0], ctOrgLen[0]);        // group0 consumables (non-key)
  for (int id = 0; id <= 0xF1; id++)                                  // key-item pass (bit 3 set), ascending
    if (present[id] && (ctItemProp[id] & 0x08)) {
      if (y < 0xF2) { mem[0x2400 + y] = (uint8_t)id; mem[0x2500 + y] = (uint8_t)cnt[id]; y++; }
      present[id] = 0;
    }
  for (int g = 1; g < 5; g++) mOrgEmit(mem, present, cnt, &y, ctOrgStart[g], ctOrgLen[g]);  // weapons/helm/armor/acc
  for (; y < 0xF2; y++) { mem[0x2400 + y] = 0; mem[0x2500 + y] = 0; } // zero-fill the tail
}

// A titled "not implemented" placeholder for an unexpected sub-screen id.
void CtMenuRenderPlaceholder(const CtMenuHost* h, uint8_t* fb, int screen) {
  static const char* nm[7] = { "", "EQUIP", "ITEM", "TECH", "CONFIG", "FORM", "SAVE" };
  if (!fb) return;
  // the background (the BG2 chrome) is laid by the caller (CtMenuChrome)
  if (h && h->drawWin) h->drawWin(fb, MI_HX, MI_HY, MI_HW, MI_HH);
  mText(h, fb, (screen >= 1 && screen <= 6) ? nm[screen] : "?", MI_HX + 12, MI_HY + 8);
  if (h && h->drawWin) h->drawWin(fb, MI_HX, MI_LY, MI_HW, MH - MI_LY - 8);
  mText(h, fb, "Not Implemented", MI_HX + 40, MI_LY + 60);
}

// One character panel (name / LV / HP cur|max / MP cur|max) at (x,y) from a CtMenuPanel. Shared by the party column
// (CtMenuRender / char select) and the status detail header, so they draw the same geometry.
// `textInset` = px from the panel x to the text column (8 on the main screen; 24 on Tech, where the standing sprite
// takes the panel's left 16 px). `dim` = draw the panel dim (non-focused member).
static int mPanelHpMaxDim = -1;                      // >= 0: the max HP's own palette (the Equip preview's $0D57)
static void mCharPanel(const CtMenuHost* h, uint8_t* fb, int x, int y, const CtMenuPanel* p, int textInset, int dim) {
  int x0 = x + textInset;
  // C2/A1EF (every panel builder: C2/A216, C2/AFED's B002, the Tech screen's BA32 / BEEF): `$0D5D` = 4 when
  // `$0D38[char]` (C2/813C: the effective max / 10) >= the current HP; string 06 prints the current HP in that
  // palette (BG1 palette 1, red; palette 5 when C2/9E76 dims the panel).
  int lowHp = 0;
  if (h->mem) {
    int sc = h->mem[0x0D38 + 2 * (p->charId & 7)] | (h->mem[0x0D39 + 2 * (p->charId & 7)] << 8);
    lowHp = sc >= p->hp;
  }
  mDim = dim;                                        // route mText/mNumR through the dim/bright glyph
  mText(h, fb, p->name, x0, y + 16);                 // row +16 (BG1 tile row 3): name ...... LV n
  mText(h, fb, "LV", x0 + 48, y + 16);
  mStatCap(h, fb, p->lv, 7, x0 + 80, y + 16);        // string 01: `LV` + code 0C cap 7
  // The numeric fields are tile-aligned: the current value right-aligned to x0+48, the "/" at x0+48 and the maximum
  // right-aligned to x0+80.
  mText(h, fb, "HP", x0, y + 24);                    // row +24: HP cur / max
  if (lowHp) mDim = dim ? 7 : 6;                     // string 06: `PAL $0D5D` before the current HP ..
  mNumR(h, fb, p->hp, x0 + 48, y + 24);
  mDim = dim;                                        //   .. `PAL $0051` after it
  mText(h, fb, "/", x0 + 48, y + 24);
  if (mPanelHpMaxDim >= 0) mDim = mPanelHpMaxDim;
  mNumR(h, fb, p->hpMax, x0 + 80, y + 24);
  mDim = dim;
  mText(h, fb, "MP", x0, y + 32);                    // row +32: MP cur / max
  mNumR(h, fb, p->mp, x0 + 48, y + 32);
  mText(h, fb, "/", x0 + 48, y + 32);
  mNumR(h, fb, p->mpMax, x0 + 80, y + 32);
  mDim = 0;
}

// The three party panels down the left column. The TIME/G box (mTimeGold) is drawn separately, since not every screen
// has it. `focusChar` < 0 dims no panel: the dim belongs to the Equip/Status char select, which tones every panel but
// the one it stands on. The Tech screen has a char-select cursor too and dims nothing.
static void mPartyPanels(const CtMenuHost* h, uint8_t* fb, int textInset, int focusChar) {
  for (int row = 0; row < 3; row++) {              // focus = the ribbon lead, or the char-select cursor's char:
    int y = 8 + row * MP_PITCH;                     //   it stays bright, the others dim (pal4)
    CtMenuPanel p;
    if (!CtMenuPanelData(h->mem, mPanelRowSlot(h->mem, row), &p)) continue;
    mCharPanel(h, fb, MP_X, y, &p, textInset, focusChar >= 0 && p.charId != focusChar);
  }
}
// Alpha-blit a [w x hh] RGBA8888 (bytes R,G,B,A; A==0 = transparent) bitmap at (x0,y0).
static void mBlitRGBA(uint8_t* fb, const unsigned char* rgba, int w, int hh, int x0, int y0) {
  for (int j = 0; j < hh; j++)
    for (int i = 0; i < w; i++) {
      const unsigned char* p = rgba + ((size_t)j * w + i) * 4;
      int dx = x0 + i, dy = y0 + j;
      if (p[3] && dx >= 0 && dx < MW && dy >= 0 && dy < MH)
        memcpy(fb + ((size_t)dy * MW + dx) * 3, p, 3);
    }
}
// ---- The party-panel standing sprite (disasm/notes/menu2_party_sprites.md) ----
// It is the character's own field sprite: the menu's OBJ tiles decode to the same colour index at every pixel as
// CT_ASSET_SPRITE[charId] composed by CtComposeSprite, and the OBJ palette equals the asset's at the twelve indices the
// frames use. The host composes the frame into h->partySpr[charId]. The standing frame (charId -> frame: Crono 0,
// Marle 1, Lucca 1, Robo 0, Frog 1, Ayla 2, Magus 0) is CT_ASSET_SPRITEANIM[charId] byte 3.
// Anchoring is by the sprite's opaque box against two numbers the ROM gives per screen, a centre x and a feet y,
// because the frame shape varies (Crono assembles as a 16x64 column of 16x16 sprites, Lucca and Frog as a 2x2) and the
// assembler (C2/F657 over the $7E:1800 records) is not ported. Tech: centre x 28, feet 56 + 48*panel; Form: centre
// 168, feet 56 / 104 / 152 (CtMenuFormGeom). A locked or empty panel's sprite is dim: its palette halved in 5-bit.
#define MP_SPR_CX_TECH 28   /* Tech-screen sprite centre x */
// The placement, shared by the blitter and by the probe that grades it: the opaque box, then the blob's top-left such
// that the box is centred on `cx` and its bottom row is feetY-1.
static int mSprPlace(const uint8_t* blob, int cx, int feetY, int* ox, int* oy,
                     int* ix0o, int* ix1o, int* iy0o, int* iy1o) {
  if (!blob) return 0;
  int w = blob[0], hh = blob[1];
  const uint8_t* px = blob + 2;
  int ix0 = w, ix1 = -1, iy0 = hh, iy1 = -1;
  for (int j = 0; j < hh; j++)
    for (int i = 0; i < w; i++)
      if (px[((size_t)j * w + i) * 4 + 3]) {
        if (i < ix0) ix0 = i; if (i > ix1) ix1 = i;
        if (j < iy0) iy0 = j; if (j > iy1) iy1 = j;
      }
  if (ix1 < ix0) return 0;
  if (ox) *ox = cx - (ix0 + ix1 + 1) / 2;      // X = the sprite centre
  if (oy) *oy = feetY - 1 - iy1;               // Y = the feet line, one row below the last ink row
  if (ix0o) *ix0o = ix0; if (ix1o) *ix1o = ix1;
  if (iy0o) *iy0o = iy0; if (iy1o) *iy1o = iy1;
  return 1;
}
// The party art carries each pixel's OBJ colour index in its alpha byte (field.c's compose). Colours 1-12 are the
// character's own ($E4; FF/F628 halves them for a dim), 13-15 its weapon's: C2/F5ED loads them from $D1 by the sheet's
// +0x29 when a screen runs C2/F5A7, and until then they hold what the menu's init left (black, the art's own). A
// caller sets mSprWpn (three BGR555 words) around the blit when the weapon colours are loaded.
static const uint16_t* mSprWpn = NULL;
static void mBlitSprAnchored(uint8_t* fb, const uint8_t* blob, int cx, int feetY, int dim) {
  int w, hh, x0, y0;
  if (!mSprPlace(blob, cx, feetY, &x0, &y0, NULL, NULL, NULL, NULL)) return;
  w = blob[0]; hh = blob[1];
  const uint8_t* px = blob + 2;
  for (int j = 0; j < hh; j++)
    for (int i = 0; i < w; i++) {
      const uint8_t* s = px + ((size_t)j * w + i) * 4;
      if (!s[3]) continue;
      int dx = x0 + i, dy = y0 + j;
      if (dx < 0 || dx >= MW || dy < 0 || dy >= MH) continue;
      uint8_t* d = fb + ((size_t)dy * MW + dx) * 3;
      int idx = s[3] & 0x0F;
      if (mSprWpn && idx >= 13) {                                  // C2/F5ED's weapon colours (never dimmed)
        uint16_t c = mSprWpn[idx - 13];
        int r = c & 31, g = (c >> 5) & 31, bl = (c >> 10) & 31;
        d[0] = (uint8_t)((r << 3) | (r >> 2)); d[1] = (uint8_t)((g << 3) | (g >> 2)); d[2] = (uint8_t)((bl << 3) | (bl >> 2));
      } else if (dim && idx <= 12)                                 // FF/F628 halves colours 1-12 only
        for (int k = 0; k < 3; k++) { int v = (s[k] >> 3) >> 1; d[k] = (uint8_t)((v << 3) | (v >> 2)); }
      else { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
    }
}
// The screen box a party sprite lands in, computed as mBlitSprAnchored places it, so a probe grades the frame choice,
// the crop and the anchor together. It reports the standing frame's box on purpose: every animated pose is placed as
// an offset from it (mBlitPartySpr), and a probe that followed the live pose would depend on the animation phase.
int CtMenuSprBox(const CtMenuHost* h, int charId, int cx, int feetY, int* bx0, int* bx1, int* by0, int* by1) {
  const uint8_t* blob = (h && charId >= 0 && charId < 7) ? h->partySpr[charId] : NULL;
  int x0, y0, ix0, ix1, iy0, iy1;
  if (!mSprPlace(blob, cx, feetY, &x0, &y0, &ix0, &ix1, &iy0, &iy1)) return 0;
  if (bx0) *bx0 = x0 + ix0; if (bx1) *bx1 = x0 + ix1;
  if (by0) *by0 = y0 + iy0; if (by1) *by1 = y0 + iy1;
  return 1;
}
// ---- The menu sprite animator (disasm/notes/menu2_party_sprites.md, "The menu sprite animator") ----
// C2/F643 spawns one task (slot 0x28, C2/F657) that walks the nine sprite blocks at $1800 + 0x40*n once a frame;
// C2/F69B advances a block: a changed request ($11 != $12) restarts at frame 0 and re-points the pose / duration tables,
// a zero duration ends the animation (restart at +0x21 when the request has bit 7, else at 0), and both exits
// decrement the countdown again, so a pose lasts `duration` frames. The tables are the character's ordinary sprite
// animation set (CT_ASSET_SPRITEANIM): the menu plays the walk cycle facing down, so block+0x03 is the field
// composer's assembly frame and anim 0 frame 0 is the standing frame.
// The state is kept here (g_mspr) instead of at $7E:1800, which is the field's per-object array in this engine
// (CtObjSceneDefaults writes $1800,X) and must resume unchanged after the menu.
// Not modelled: the VRAM DMA at 2F707 (the host composes the frame instead).
typedef struct { uint8_t want, cur, count, frame, pose, shown, loop; int chr; } CtMenuSprBlk;   // shown = on screen
// Blocks 0-6 are the characters' own ($1800 + 0x40 * charId, C2/F626); block 7 ($19C0) is a copy whose character is
// set at run time, the Tech screen's caster (C2/BA09). `loop` = block +0x21, the restart point of a request with
// bit 7 (C2/B4E6's $19D1 = 90; 2F6E0 `LDA $11 / BPL / LDA $21 / STA $16`).
static CtMenuSprBlk g_mspr[8];

// CT_ASSET_SPRITEANIM: [nAnims u8] then per anim [n u8] + n x {U,D,L,R asm u8, delay u8}. Returns the frame array for
// `ai` (and its count), or NULL. The same walk as field.c's animFrames.
static const uint8_t* msprFrames(const CtMenuHost* h, int c, int ai, int* n) {
  *n = 0;
  if (!h || c < 0 || c > 6) return NULL;
  const uint8_t* an = h->spriteAnim[c];
  unsigned al = h->spriteAnimLen[c];
  if (!an || al < 2) return NULL;
  if (ai < 0 || ai >= an[0]) return NULL;
  const uint8_t* p = an + 1;
  const uint8_t* end = an + al;
  for (int k = 0; k < ai; k++) { if (p >= end) return NULL; p += 1 + (size_t)p[0] * 5; }
  if (p >= end || p + 1 + (size_t)p[0] * 5 > end) return NULL;
  *n = p[0];
  return p + 1;
}
#define MSPR_D     1   /* the D (facing-down) column of a {U,D,L,R,delay} record, the menu's facing */
#define MSPR_DELAY 4

int CtMenuSprPose(const CtMenuHost* h, int charId) {
  if (charId < 0 || charId > 6) return 0;
  (void)h;
  { const char* f = getenv("CT_FORMPOSE");   /* debug: pin an animating block to one frame */
    if (f && g_mspr[charId].cur) return atoi(f); }
  return g_mspr[charId].pose;
}
int CtMenuSprShown(int charId) {
  if (charId < 0 || charId > 6) return 0;
  { const char* f = getenv("CT_FORMPOSE");   /* debug: pin an animating block to one frame */
    if (f && g_mspr[charId].cur) return atoi(f); }
  return g_mspr[charId].shown;
}
void CtMenuSprReset(const CtMenuHost* h) {
  for (int c = 0; c < 7; c++) {
    int n = 0; const uint8_t* fr = msprFrames(h, c, 0, &n);      // anim 0 = the standing pose
    g_mspr[c].want = g_mspr[c].cur = 0;
    g_mspr[c].count = 0; g_mspr[c].frame = 0;
    g_mspr[c].pose = (fr && n > 0) ? fr[MSPR_D] : 0;
    g_mspr[c].shown = g_mspr[c].pose;
    g_mspr[c].loop = 0; g_mspr[c].chr = c;
  }
  memset(&g_mspr[7], 0, sizeof g_mspr[7]); g_mspr[7].chr = -1;   // block 7: no character until C2/BA09
}
// C2/BA09 -> C2/F422: block 7 takes `charId` (only when it differs: `CMP $00 / BEQ`), at its standing pose.
void CtMenuSprBlock7(const CtMenuHost* h, int charId) {
  if (charId < 0 || charId > 6 || g_mspr[7].chr == charId) return;
  int n = 0; const uint8_t* fr = msprFrames(h, charId, 0, &n);
  g_mspr[7].chr = charId; g_mspr[7].want = g_mspr[7].cur = 0; g_mspr[7].count = 0; g_mspr[7].frame = 0;
  g_mspr[7].pose = (fr && n > 0) ? fr[MSPR_D] : 0; g_mspr[7].shown = g_mspr[7].pose;
}
// Block 7's request (+0x11, $19D1) and loop frame (+0x21, $19E1); `loop` < 0 leaves +0x21 as it is.
void CtMenuSprRequest7(int animId, int loop) {
  g_mspr[7].want = (uint8_t)animId;
  if (loop >= 0) g_mspr[7].loop = (uint8_t)loop;
}
int CtMenuSprChar7(void) { return g_mspr[7].chr; }
int CtMenuSprShown7(void) { return g_mspr[7].shown; }
// C2/C997: `LDA $2980,X / BMI ->rts / ... TAX / TYA / STA $1811,X` -- the roster byte for cell x names the character
// whose block gets the request; bit 7 (an empty cell) writes nothing.
static void msprRequestCell(const CtMenuHost* h, int cell, int animId) {
  if (!h || !h->mem || cell < 0 || cell > 8) return;
  int e = h->mem[0x2980 + cell];
  if (e & 0x80) return;                                          // 2C99B BMI
  g_mspr[e & 7].want = (uint8_t)animId;
}
// C2/C973: everyone still, the cursor's member walking, the picked member on anim 0x0B.
//   2C975  LDA #08 / STA $00 / LDY #00 / (loop) LDX $00 / JSR C2/C997 / DEC $00 / BPL
//   2C984  LDY #01 / LDX $0F0D / JSR C2/C997          ; $0F0D = the cursor cell
//   2C98C  LDA $0F00 / BPL / LDY #0B / LDX $0F00 / JSR C2/C997   ; $0F00 = the picked cell, FF = none
// Indexed by the cell, not the panel row: C2/C997 reads $2980 with the cell, while the panel row shows C2/CD2B's
// compacted entry, so on a party with a leading hole the two differ (not checked against the oracle).
void CtMenuSprFormAnims(const CtMenuHost* h, int cursorCell, int pickedCell) {
  for (int x = 8; x >= 0; x--) msprRequestCell(h, x, 0);
  msprRequestCell(h, cursorCell, 1);
  if (pickedCell >= 0) msprRequestCell(h, pickedCell, 0x0B);
}
// The shop's writer: C2/DD7C `JSR C2/F626` (Y = charId * 0x40) then `STA $1811,Y`, by character, not by cell.
void CtMenuSprRequest(int charId, int animId) {
  if (charId < 0 || charId > 6) return;
  g_mspr[charId].want = (uint8_t)animId;
}
void CtMenuSprTick(const CtMenuHost* h) {
  if (getenv("CT_SPRDBG")) {   /* debug: the block state this frame, per character */
    for (int c = 0; c < 7; c++) if (g_mspr[c].want || g_mspr[c].cur)
      fprintf(stderr, "[mspr] c%d want=%02X cur=%02X frame=%d count=%d pose=%02X\n",
              c, g_mspr[c].want, g_mspr[c].cur, g_mspr[c].frame, (int8_t)g_mspr[c].count, g_mspr[c].pose);
  }
  // The upload budget (2F65D STZ $0D73 / 2F678 CMP $0D75 / BCS): a block advances only while $0D73 < $0D75, and a new
  // pose's tile upload (C2/F75C, reached when the frame byte differs, 2F701) adds the block's $1802 byte to $0D73
  // (2F820 LDA $02 / 2F826 ADC $0D73), 01 for every PC block. $0D75 is the screen's: 7 in the shop's build (C2/DC26)
  // and 1 on every frame of its Buy/Sell substate (C2/D548), 2 from the Form tab's builder (C2/CA39), 1 / 2 in the
  // Item overlay (C2/AF64 / AF72 / B86B / B34F), 2 for the Tech list (C2/B9E6), 0 after the open's fill. So $0D75 new
  // poses a frame, in block (charId) order; a blocked block keeps the count it already decremented and retries next
  // frame.
  int spent = 0, budget = (h && h->mem) ? h->mem[0x0D75] : 1;
  for (int c = 0; c < 8; c++) {
    CtMenuSprBlk* b = &g_mspr[c];
    if (b->chr < 0) continue;                                    // block 7 with no character
    int adv = 0;
    if (b->want != b->cur) adv = 1;                              // 2F66E BNE
    else if (b->want != 0) {                                     // 2F670 CMP #00 / BEQ
      b->count--;                                                // 2F674 DEC $13
      if ((int8_t)b->count < 0) adv = 1;                         // 2F676 BPL
    }
    if (!adv) continue;
    if (spent >= budget) continue;                               // 2F67B CMP $0D75 / BCS ->2F683
    b->frame++;                                                  // 2F69B INC $16
    int changed = (b->want != b->cur);                           // 2F69D CMP $12
    b->cur = b->want;                                            // 2F6A1 STA $12
    if (changed) b->frame = 0;                                   // 2F6A5 STZ $16
    int n = 0; const uint8_t* fr = msprFrames(h, b->chr, b->cur & 0x7F, &n);
    if (!fr || n <= 0) continue;
    int dead = 0, guard = 0;
    for (;;) {                                                   // 2F6F7 LDY $16 / LDA [$1E],Y
      // The ROM's table ends in a 0 duration; ours is length-counted, so "past the end" is that 0.
      if (b->frame < n && fr[(size_t)b->frame * 5 + MSPR_DELAY]) break;
      if ((b->cur & 0x80) && ++guard < 4) { b->frame = b->loop; continue; }   // 2F6E0 BPL / LDA $21 / STA $16
      if (b->frame != 0) { b->frame = 0; continue; }              // 2F6EE STZ $16 (restart)
      b->frame--; dead = 1; break;                                // 2F6F2 DEC $16 / RTS (empty anim)
    }
    if (dead) continue;                                           //   -- and no extra DEC $13
    b->count = fr[(size_t)b->frame * 5 + MSPR_DELAY];             // 2F6FD STA $13
    if (fr[(size_t)b->frame * 5 + MSPR_D] != b->pose) spent++;    // 2F701 CMP $03 / BEQ: a new pose is uploaded
    b->pose  = fr[(size_t)b->frame * 5 + MSPR_D];                 // 2F705 STA $03
    b->count--;                                                   // 2F752 DEC $13
  }
  // The picture shows this tick's pose: the new pose's OAM (C2/F871) and its tiles (C2/F75C's queue) both go out in
  // this frame's NMI.
  for (int c = 0; c < 8; c++) g_mspr[c].shown = g_mspr[c].pose;
}

static const uint8_t* mPartyBlob(const CtMenuHost* h, int charId) {
  return (charId >= 0 && charId < 7) ? h->partySpr[charId] : NULL;
}
// Draw a party sprite at its current animation pose. mSprPlace anchors a blob by its own ink box (centre at cx, bottom
// at feetY-1), which suits the standing frame; the ROM anchors the OBJ block to the compose canvas and lets the art
// move inside it, so re-centring every frame on its own ink would make the character jitter. The pose's blob is
// placed with the standing frame's anchor plus the difference of the two crops:
//     dx = (cropX + w/2)   - (standCropX + standW/2)        [ink centres, mSprPlace's rounding]
//     dy = (cropY + h)     - (standCropY + standH)          [ink bottoms]
// With no animator hook, or at the standing frame, this is the plain anchored blit.
static void mBlitPartySprPose(const CtMenuHost* h, uint8_t* fb, int charId, int pose, int cx, int feetY, int dim);
static void mBlitPartySpr(const CtMenuHost* h, uint8_t* fb, int charId, int cx, int feetY, int dim) {
  mBlitPartySprPose(h, fb, charId, -1, cx, feetY, dim);
}
// The same at an explicit assembly frame `pose` (-1 = the animator's pose on screen): for a block the ROM drives by
// hand, like the Tech caster's block 7 while `$19D1` = 90 holds `$19E1`'s frame (C2/B4E6).
static void mBlitPartySprPose(const CtMenuHost* h, uint8_t* fb, int charId, int pose, int cx, int feetY, int dim) {
  const uint8_t* blob = mPartyBlob(h, charId);
  int dx = 0, dy = 0;
  if (h->partySprFrame && charId >= 0 && charId < 7) {
    int n = 0; const uint8_t* fr = msprFrames(h, charId, 0, &n);      // anim 0 = standing
    int stand = (fr && n > 0) ? fr[MSPR_D] : -1;
    if (pose < 0) pose = CtMenuSprShown(charId);                    // the pose on screen
    if (stand >= 0 && pose != stand) {
      int fx = 0, fy = 0, sx = 0, sy = 0;
      const uint8_t* pb = h->partySprFrame(charId, pose, &fx, &fy);
      const uint8_t* sb = h->partySprFrame(charId, stand, &sx, &sy);
      if (pb && sb) {
        dx = (fx + pb[0] / 2) - (sx + sb[0] / 2);
        dy = (fy + pb[1])     - (sy + sb[1]);
        blob = pb;
      }
    }
  }
  mBlitSprAnchored(fb, blob, cx + dx, feetY + dy, dim);
}
// The Form panel geometry, in one place so a probe grades what the renderer uses: the text column and the sprite
// anchor C2/CE6E gives each panel.
void CtMenuFormGeom(int panel, int* textX, int* feetY, int* centreX) {
  if (textX)   *textX   = MP_X + MFORM_INSET;        // x 72 = tile col 9
  if (feetY)   *feetY   = 56 + panel * MP_PITCH;     // C2/CE6E[$12 + 2*panel] low byte: 56 / 104 / 152
  if (centreX) *centreX = 168;                       //   ... high byte, 0xA8, the same for all three
}
// The Form screen's three panels: the panel array is compacted, so row r shows roster slot pan[r]&0x3F, and a locked
// entry (bit 6) is dim.
static void mFormSprites(const CtMenuHost* h, uint8_t* fb, const uint8_t* pan) {
  if (!h || !h->mem) return;
  for (int r = 0; r < 3; r++) {
    if (pan[r] & 0x80) continue;
    int cid = h->mem[0x2980 + (pan[r] & 0x3F)] & 7;
    int feet, cx; CtMenuFormGeom(r, NULL, &feet, &cx);
    mBlitPartySpr(h, fb, cid, cx, feet, (pan[r] & 0x40) ? 1 : 0);
  }
}
// The Tech screen's three panels (the sprite sits left of the text there, centre x 28).
static void mPartySprites(const CtMenuHost* h, uint8_t* fb) {
  for (int row = 0; row < 3; row++) {                   // (the window's rows: $041A on the Tech screen)
    CtMenuPanel p;
    if (!CtMenuPanelData(h->mem, mPanelRowSlot(h->mem, row), &p)) continue;
    mBlitPartySpr(h, fb, p.charId, MP_SPR_CX_TECH, 56 + row * MP_PITCH, 0);
  }
}
// The Tech screen's Single/Dual/Triple tabs (top right, BG1 tiles 0x164-0x16F). Tab `active` (0/1/2) is drawn in the
// cream selected palette, the others grey. Each tab is a 2x2 group of 8x8 tiles.
static void mTechTabs(uint8_t* fb, int active) {
  for (int t = 0; t < 3; t++) {
    int sel = (t == active) ? 0 : 1;
    for (int q = 0; q < 4; q++) {                    // TL,TR,BL,BR
      const uint8_t* g = techTabTiles[t * 4 + q];
      int ox = (q & 1) * 8, oy = (q >> 1) * 8;
      for (int yy = 0; yy < 8; yy++)
        for (int xx = 0; xx < 8; xx++) {
          int v = g[yy * 8 + xx]; if (!v) continue;  // index 0 transparent
          const uint8_t* c = techTabPal[sel][v & 15];
          int dx = techTabX[t] + ox + xx, dy = TECH_TAB_Y + oy + yy;
          if (dx >= 0 && dx < MW && dy >= 0 && dy < MH) {
            uint8_t* d = fb + ((size_t)dy * MW + dx) * 3; d[0] = c[0]; d[1] = c[1]; d[2] = c[2];
          }
        }
    }
  }
}
// One panel's "MP Used" row (Tech): the label and, when mpUsed >= 0, that participant's own cost; a Dual list draws two
// with different numbers (C2/BC22 recomputes $9890 per panel). String 0x20's last row, `MP Used <PAL $0D5E><NUM $9890
// f19>`: C2/BA2F sets `$0D5E` = 4 (BG1 palette 1, red) when the member's current MP (`LDA $9A97 / CMP $9890`, 8-bit) is
// below the cost; C2/BBB9 runs it for every participant of a Dual / Triple with its own cost (C2/BC22), the builds
// (C2/B5E6, C2/BA88) with `$9890` = 0.
static void mTechMpUsedOne(const CtMenuHost* h, uint8_t* fb, int panel, int mpUsed, int curMp) {
  int y = 8 + panel * MP_PITCH + 40;                 // the 4th text row (name/HP/MP at +16/+24/+32)
  mText(h, fb, "MP Used", 40, y);
  if (mpUsed >= 0) {
    if ((curMp & 0xFF) < (mpUsed & 0xFF)) mDim = 6;
    mNumR(h, fb, mpUsed, 120, y);
    mDim = 0;
  }
}

// The Tech screen's "MP Used" rows draw undimmed for every panel, lead or not.
static void mTechMpUsed(const CtMenuHost* h, uint8_t* fb, int mpSlot, int mpUsed) {
  for (int slot = 0; slot < 3; slot++) {                 // (rows of the window)
    CtMenuPanel p;
    if (!CtMenuPanelData(h->mem, mPanelRowSlot(h->mem, slot), &p)) continue;
    int y = 8 + slot * MP_PITCH + 40;                // the 4th text row (name/HP/MP at +16/+24/+32)
    mText(h, fb, "MP Used", 40, y);
    if (slot == mpSlot && mpUsed >= 0) mNumR(h, fb, mpUsed, 120, y);
  }
}
// The TIME / G box (bottom left of the main screen), as C2/F28E: HH:MM from the digit pairs $0404-05 / $0402-03
// (tens*10+units, C2/F2CC), capped at 99:59 when $0406 != 0.
static void mTimeGold(const CtMenuHost* h, uint8_t* fb) {
  const uint8_t* m = h->mem;
  int hh = m[0x405] * 10 + m[0x404], mm = m[0x403] * 10 + m[0x402];
  if (m[0x406]) { hh = 99; mm = 59; }                // overflow cap (C2/F290-F296 default 63/3B)
  mText(h, fb, "TIME", MT_X + 8, MT_Y + 24);         // TIME row (y 176)
  // The separator is tile 0x2D, not the charset ':' (0xE3): C2/F28E builds the clock into the tilemap and writes the
  // middle cell itself (2F2B2 `LDA $7D / AND #FF00 / ORA #002D / STA $0000,X`). The two are different glyphs.
  { char t[3]; t[2] = 0;
    t[0] = (char)('0' + hh / 10); t[1] = (char)('0' + hh % 10);
    mText(h, fb, t, MT_X + 48, MT_Y + 24);
    mGlyph(h, fb, 0x2D, MT_X + 64, MT_Y + 24);
    t[0] = (char)('0' + mm / 10); t[1] = (char)('0' + mm % 10);
    mText(h, fb, t, MT_X + 72, MT_Y + 24); }
  int gold = m[0x2C53] | (m[0x2C54] << 8) | (m[0x2C55] << 16);   // $7E2C53 24-bit LE
  mNumR(h, fb, gold, MT_X + 80, MT_Y + 40);          // gold row (y 192), the value then G on the right
  mText(h, fb, "G", MT_X + 80, MT_Y + 40);
}

static void mDetailRight(const CtMenuHost* h, uint8_t* fb, int charId, int equipSel);  // opt0 detail (below)
static void mPanelAtkDef(const CtMenuHost* h, uint8_t* fb, int textInset, int focusChar);  // party-panel 4th row

// The ribbon main screen. The ROM shows the selected option's own screen live behind the floating icon box, so:
//   1) lay the BG2 chrome for this option (marble + that option's window panels), box skipped;
//   2) draw the option's preview content between the panels;
//   3) redraw the ribbon box on top, with the six icons and the two arrows.
void CtMenuRender(const CtMenuHost* h, uint8_t* fb, int menuOpt) {
  if (!h || !fb) return;
  if (menuOpt < 0) menuOpt = 0; else if (menuOpt > 5) menuOpt = 5;
  int lead = h->mem ? (h->mem[0x2980] & 7) : 0;      // (2) the selected option's screen, live:
  CtMenuChrome(fb, 0, menuOpt, 0);                   // (1) marble + this option's panels (ribbon box later)
                                                     //   (the caller sets the char-select context, so the opt-0
                                                     //    preview's bright box follows the kept cursor)
  // opt0 (Equip/Status) is a full sub-screen behind the ribbon, and it follows the kept cursor member: after Marle's
  // Equip and B back to the ribbon, her box stays bright and her detail stays on the right. The caller sets
  // g_charSelChar to that member.
  int selCh = (g_charSelChar >= 0) ? g_charSelChar : lead;
  switch (menuOpt) {
    case 0: mPartyPanels(h, fb, 8, selCh); mPanelAtkDef(h, fb, 8, selCh); mTimeGold(h, fb); mDetailRight(h, fb, selCh, -1); break;  // Equip/Status
    case 1: CtMenuRenderItem(h, fb, -1, 0, -1, 0, 0, 1, 1, 0, 0, 0); break; // Item preview: no cursor (not entered yet)
    case 2: mPartyPanels(h, fb, 24, -1); mPartySprites(h, fb);
            mTechTabs(fb, (h->mem && h->mem[0x0418] < 3) ? h->mem[0x0418] : 0);   // $0418: 0 after a ribbon move (C2/9A5C),
            mTechMpUsed(h, fb, -1, -1); break;   // the screen's last tab after its B (C2/9B71 -> C2/B960 redraws no tab)
    case 3: CtMenuRenderConfig(h, fb, -1, 1, 0, -1, -1); break;   // Config preview: no hand, no bottom bar
    // Form: the ribbon hover has no hand (the entered screen is this frame plus one hand sprite; cell = -1 as for the
    // Item preview).
    case 4: CtMenuRenderForm(h, fb, -1, -1, NULL, 0); break;
    case 5: CtMenuRenderSave(h, fb, -1); break;              // Save preview: no cursor -- the hover draws every row
                                                             //   bright and no hand
  }
  (void)lead;
  mDrawRibbon(h, fb, menuOpt);                       // (3) ribbon box + icons + arrows
}

// ------------------------------------------------------- Screen 1 = Equip/Status
// Char sheet $7E2600 + charId*0x50.
#define SH_STRIDE   0x50
#define SH_STATS    0x36   /* 7 effective stats (base + equipment), Power,Stamina,Speed,Magic,Hit,Evade,M.Def --
                            * the derived block the status page displays (0x0B is the base). e.g. Crono 0x38
                            * SPD = 13 (base 12 + Wood Sword +1). Recomputed by C2/916E (CtMenuRecalcDerived). */
#define SH_LEVEL    0x12
#define SH_XP       0x13   /* EXP, 3-byte LE */
#define SH_HELM     0x27   /* equipped Helm id      (name-record icon byte 0x27) */
#define SH_ARMOR    0x28   /* equipped Armor/body   (icon byte 0x28) */
#define SH_WEAPON   0x29   /* equipped Weapon       (icon byte 0x20-0x26) */
#define SH_ACCESSORY 0x2A  /* equipped Accessory    (icon byte 0x29) */
#define SH_NEXT     0x2B   /* XP to next level, word (Crono/Marle LV1=20, Lucca LV2=40 = displayed NEXT) */

// stat indices within +0x0B: 0 Power, 1 Stamina, 2 Speed, 3 Magic, 4 Hit, 5 Evade, 6 M.Def
enum { ST_PWR = 0, ST_STAM, ST_SPD, ST_MAG, ST_HIT, ST_EV, ST_MDEF };

// The party-panel selection cursor: the ROM hand sprite ($FF/9CF0), the same hand the name-entry grid uses
// (h->handCursor, [w u8][h u8] + RGBA; alpha 0 = transparent). (x,y) = the sprite's top left. A plain white triangle
// stands in only when the asset is absent. (mHandFlip: the same 16 x 16 OBJ with its attribute's H flip, bit 6.)
static int mHandFlip;
static void mHandCursor(const CtMenuHost* h, uint8_t* fb, int x, int y) {
  const uint8_t* s = h ? h->handCursor : NULL;
  if (s) {
    int w = s[0], hh = s[1]; const uint8_t* px = s + 2;
    for (int j = 0; j < hh; j++)
      for (int i = 0; i < w; i++) {
        const uint8_t* p = px + ((size_t)j * w + (mHandFlip ? w - 1 - i : i)) * 4;
        int dx = x + i, dy = y + j;
        if (p[3] && dx >= 0 && dx < MW && dy >= 0 && dy < MH)
          memcpy(fb + ((size_t)dy * MW + dx) * 3, p, 3);
      }
    return;
  }
  for (int j = 0; j < 11; j++) {                      // fallback triangle (no asset)
    int wgt = j <= 5 ? j : 10 - j;
    for (int i = 0; i <= wgt; i++) {
      int dx = x + i, dy = y + j;
      if (dx >= 0 && dx < MW && dy >= 0 && dy < MH) {
        uint8_t* d = fb + ((size_t)dy * MW + dx) * 3;
        d[0] = d[1] = d[2] = 255;
      }
    }
  }
}

// (a) Character select (the entered Equip/Status screen): the opt0 layout without the ribbon box -- party panels,
// ATK/DEF, TIME/G and the selected member's full right detail (element plaque, portrait, name/level, 4 equips, 7
// stats, EXP/NEXT) -- plus the hand on the selected active-party slot (0..2). The cursor is $0054/$0412.
void CtMenuRenderCharSelect(const CtMenuHost* h, uint8_t* fb, int selSlot, int dimSlot, int hand) {
  if (!h || !fb) return;
  // the background (the BG2 chrome) is laid by the caller (CtMenuChrome)
  // selSlot / dimSlot are window ROWS ($0412): the member is the window's (mPanelRowSlot)
  int selChar = (h->mem && selSlot >= 0 && selSlot < 3) ? mRowChar(h->mem, selSlot, 0) : 0;
  // The panels' dim is C2/9E76's (BG1 AND BG2), which reads $0412 BEFORE C2/9D17 stores the new cursor: it follows the
  // hand one frame late. The detail (C2/A22F) is drawn after the store: the new member at once.
  int dimChar = (h->mem && dimSlot >= 0 && dimSlot < 3) ? mRowChar(h->mem, dimSlot, selChar) : selChar;
  mPartyPanels(h, fb, 8, dimChar);                     // $0412's member is bright, the others dim
  mPanelAtkDef(h, fb, 8, dimChar);                     // the ATK/DEF 4th panel row (Equip main screen)
  mTimeGold(h, fb);
  mDetailRight(h, fb, selChar, -1);                    // the selected member's full detail (all equips bright)
  if (hand && selSlot >= 0 && selSlot < 3)             // the hand at (8,35) for slot 0, pitch 48
    mHandCursor(h, fb, 8, 8 + selSlot * MP_PITCH + 27);
}

// Alpha-blit an indexed [w x hh] bitmap (index 0 = transparent) through a 16-entry 0xAABBGGRR palette.
static void mBlitIndexed(uint8_t* fb, const uint8_t* idx, int w, int hh, int x0, int y0, const uint32_t* pal) {
  for (int j = 0; j < hh; j++)
    for (int i = 0; i < w; i++) {
      uint32_t c = pal[idx[j * w + i] & 15];
      if (!(c >> 24)) continue;                        // alpha 0 = transparent
      int dx = x0 + i, dy = y0 + j;
      if (dx >= 0 && dx < MW && dy >= mClipTop && dy < mClipBot && dy < MH) {   // clip to the box window
        uint8_t* d = fb + ((size_t)dy * MW + dx) * 3;
        d[0] = c & 0xFF; d[1] = (c >> 8) & 0xFF; d[2] = (c >> 16) & 0xFF;   // 0xAABBGGRR -> R,G,B
      }
    }
}
// C2/EC38 + C2/A273: the detail's portrait and element plaque, from the ROM. The menu's BG1 characters hold the seven
// portraits from tile 0x201 (FF/0000 on, 36 tiles = 0x480 bytes a character) and the four plaques from tile 0x100
// (FF/D5E4 on, 12 tiles = 0x180 each). C2/EC38 lays the portrait as tile 0x200 + FF:CE70[id], 6x6 in row order from
// BG1 (15,3) = (120,24), palette 3 = the 16 colours at the FF:CE78[id] pointer; C2/A273 takes x = C2:A2C6[id] and lays
// the word at FF:CE88+x (the tile, palette 6) 6x2 from (22,3) = (176,24), palette 6 = the colours at the FF:CE92+x
// pointer. Robo, Ayla and id 7 get word 0000: tiles 0-11, which are blank, so no plaque. Colour 0 is BG1's transparent.
static void mRomTiles4(uint8_t* fb, const CtMenuHost* h, long src, int cols, int rows, int x0, int y0, long palSrc) {
  if (!h->rom || src < 0 || src + 32L * cols * rows > h->romLen || palSrc < 0 || palSrc + 32 > h->romLen) return;
  uint8_t pal[16][3];
  for (int i = 0; i < 16; i++) {
    int c = CtRomByte(h->rom, 0x400000L, (long)(palSrc + 2 * i)) | (CtRomByte(h->rom, 0x400000L, (long)(palSrc + 2 * i + 1)) << 8);
    pal[i][0] = (uint8_t)mExp5(c & 31); pal[i][1] = (uint8_t)mExp5((c >> 5) & 31); pal[i][2] = (uint8_t)mExp5((c >> 10) & 31);
  }
  for (int t = 0; t < cols * rows; t++) {
    const uint8_t* b = CtRomPtr(h->rom, h->romLen, src + 32L * t, 32);
    for (int r = 0; r < 8; r++)
      for (int x = 0; x < 8; x++) {
        int bit = 7 - x;
        int v = ((b[2 * r] >> bit) & 1) | (((b[2 * r + 1] >> bit) & 1) << 1) |
                (((b[16 + 2 * r] >> bit) & 1) << 2) | (((b[16 + 2 * r + 1] >> bit) & 1) << 3);
        if (!v) continue;
        int dx = x0 + (t % cols) * 8 + x, dy = y0 + (t / cols) * 8 + r;
        if (dx < 0 || dx >= MW || dy < mClipTop || dy >= mClipBot || dy >= MH) continue;
        memcpy(fb + ((size_t)dy * MW + dx) * 3, pal[v], 3);
      }
  }
}
static void mDetailArt(const CtMenuHost* h, uint8_t* fb, int charId) {
  if (!h->rom || h->romLen < 0x3FD5E4L + 4 * 0x180) return;
  const long FF = 0x3F0000L;
  int id = charId & 7;
  int tile = CtRomByte(h->rom, 0x400000L, (long)(FF + 0xCE70 + id));                                      // + 0x200
  long pp = CtRomByte(h->rom, 0x400000L, (long)(FF + 0xCE78 + 2 * id)) | (CtRomByte(h->rom, 0x400000L, (long)(FF + 0xCE79 + 2 * id)) << 8);
  mRomTiles4(fb, h, FF + 32L * (tile - 1), 6, 6, 120, 24, FF + pp);       // tile 0x201 = FF/0000
  int x = CtRomByte(h->rom, 0x400000L, (long)(0x2A2C6 + id));
  int w = CtRomByte(h->rom, 0x400000L, (long)(FF + 0xCE88 + x)) | (CtRomByte(h->rom, 0x400000L, (long)(FF + 0xCE89 + x)) << 8);
  long ep = CtRomByte(h->rom, 0x400000L, (long)(FF + 0xCE92 + x)) | (CtRomByte(h->rom, 0x400000L, (long)(FF + 0xCE93 + x)) << 8);
  if ((w & 0x3FF) >= 0x100) mRomTiles4(fb, h, FF + 0xD5E4 + 32L * ((w & 0x3FF) - 0x100), 6, 2, 176, 24, FF + ep);
}
// The opt0 (Equip/Status) detail in the right window: the portrait and plaque (mDetailArt), then name (176,48), LEVEL
// (176,64, value x232), the 4 equipped items (8x8 type icon x136 + name x144), the 7 stats in two columns (labels
// x120/x176, values x168/x232), then EXP and NEXT.
static const int eqRowY[4] = { 80, 92, 104, 116 };  // equip rows, 12 px pitch
// equipSel = the Equip screen's slot cursor (0..3): that slot's item stays bright, the other three dim (pal4).
// equipSel < 0 = all four bright.
static void mDetailRight(const CtMenuHost* h, uint8_t* fb, int charId, int equipSel) {
  if (!h || !h->mem) return;
  const uint8_t* sh = h->mem + 0x2600 + (size_t)(charId & 7) * SH_STRIDE;
  mDetailArt(h, fb, charId);
  mText(h, fb, CtGetName(charId & 7), 176, 48);      // name (below the plaque)
  mText(h, fb, "LEVEL", 176, 64);  mStatCap(h, fb, sh[SH_LEVEL], 7, 232, 64);   // string 04: code 0C cap 7
  int eq[4] = { sh[SH_WEAPON], sh[SH_HELM], sh[SH_ARMOR], sh[SH_ACCESSORY] };
  // On the Equip screen the non-cursor rows dim the type icon too (icon indices 1/2/3 take the dim text's grey ramp):
  // pal0 {282018,909088,F8F8F8} -> {49,66,74 / 90,107,107 / 140,148,148}.
  static const uint32_t eqIconDimPal[16] = { 0x00000000, 0xFF4A4231, 0xFF6B6B5A, 0xFF94948C, 0xFF000000,
    0xFF4A4231, 0xFF6B6B5A, 0xFF94948C, 0xFF7B7373, 0xFFE7FFFF, 0xFF73736B, 0xFF635A5A, 0xFF4A4239, 0xFF423931, 0xFF393129, 0xFF101018 };
  for (int i = 0; i < 4; i++) {
    int y = eqRowY[i];
    int rowDim = (equipSel >= 0 && i != equipSel);     // non-cursor Equip row -> dim icon + text
    const uint8_t* ic = eq[i] ? mEquipIcon(eq[i]) : NULL;   // the item's own type icon
    if (ic) mBlitIndexed(fb, ic, 8, 8, 136, y, rowDim ? eqIconDimPal : gen_opt0_pal0);   // type-icon (x136)
    char nm[16] = "";
    if (eq[i] && !CtMenuItemName(h->rom, h->romLen, eq[i], nm, sizeof nm)) snprintf(nm, sizeof nm, "%02X", eq[i]);
    mDim = rowDim ? 2 : 0;                             // Equip: the non-cursor slots use the lighter dim2
    mText(h, fb, nm, 144, y);                          // equipped-item name (x144)
    mDim = 0;
  }
  const uint8_t* st = sh + SH_STATS;                 // stats: label column x120, right column x176, 12 px pitch
  // (under the candidate list's preview each value is the NEW one, in C2/A5E7's palette for its row)
  #define ST_NUM(i, x, y) do { mDim = mEqPrevDim(i); mStatCap(h, fb, mEqPrevOn ? mEqPrevNew[i] : st[i], kStatCap[i], x, y); \
                               mDim = 0; } while (0)
  mText(h, fb, "PWR.", 120, 132);  ST_NUM(ST_PWR, 168, 132);   // labels carry a trailing '.', except HIT;
  mText(h, fb, "SPD.", 176, 132);  ST_NUM(ST_SPD, 232, 132);   // the values are the effective stats
  mText(h, fb, "HIT", 120, 144);   ST_NUM(ST_HIT, 168, 144);
  mText(h, fb, "EV.",  176, 144);  ST_NUM(ST_EV, 232, 144);
  mText(h, fb, "MAG.", 120, 156);  ST_NUM(ST_MAG, 168, 156);
  mText(h, fb, "STAM.",176, 156);  ST_NUM(ST_STAM, 232, 156);
  mText(h, fb, "M DEF.",120, 168); ST_NUM(ST_MDEF, 232, 168);   // "M DEF." (space + trailing dot)
  #undef ST_NUM
  int xp = sh[SH_XP] | (sh[SH_XP + 1] << 8) | (sh[SH_XP + 2] << 16);
  int next = sh[SH_NEXT] | (sh[SH_NEXT + 1] << 8);
  mText(h, fb, "EXP", 120, 184);   mNumR(h, fb, xp, 232, 184);
  mText(h, fb, "NEXT",120, 192);   mNumR(h, fb, next, 232, 192);
}

// The Equip/Status party panel's 4th row (BG1 row 6): [weapon icon 0x20] attack [shield icon 0x2E] defence, the
// sheet's +0x3D / +0x3E. The Tech screen draws "MP Used" in this row instead (mTechMpUsed); the Status detail draws
// nothing here.
static const uint8_t gen_opt0_icon_def[8*8] = {   // BG1 tile 0x2E (shield), from the oracle's VRAM
  3,3,3,3,3,3,2,1, 3,2,3,2,2,3,2,1, 3,2,3,2,2,3,2,1, 3,3,3,3,3,3,2,1,
  3,2,3,2,2,3,2,1, 3,2,3,2,2,3,2,1, 1,3,3,3,3,2,1,0, 0,1,1,1,1,1,0,0 };
// A dim panel's icons: they are BG1 text tiles (palette 0) like its letters, and the ROM dims a panel by moving its
// cells to palette | 4 (C2/9E76 / C2/EC93), so the icons take CGRAM row 4 like the dim text. `pal` keeps index 0's
// transparency; the colours are ctMenuChromePal[4] (the menu's CGRAM), as 0xAABBGGRR.
static const uint32_t* mDimPal(const uint32_t* pal) {
  static uint32_t d[16];
  for (int i = 0; i < 16; i++) {
    const uint8_t* c = ctMenuChromePal[4][i];
    d[i] = (pal[i] & 0xFF000000) | ((uint32_t)c[2] << 16) | ((uint32_t)c[1] << 8) | c[0];
  }
  return d;
}
// The party panel's 4th text row at x0 = MP_X + textInset: weapon-type icon at x0+8, attack right-aligned to x0+40,
// armour icon at x0+48, defence right-aligned to x0+80 -- the ROM's tile columns 10 / 11-13 / 15 / 16-18
// (menu2_form_screen.md 4.4). The Form screen draws the same row at x0 = 72.
static void mPanelAtkDefOne(const CtMenuHost* h, uint8_t* fb, int textInset, int y, int charId, int dim) {
  const uint8_t* sh = h->mem + 0x2600 + (size_t)(charId & 7) * SH_STRIDE;
  int x0 = MP_X + textInset;
  mDim = dim;
  const uint32_t* ipal = mDim ? mDimPal(gen_opt0_pal0) : gen_opt0_pal0;  // icons dim with the text
  const uint8_t* wic = mEquipIcon(sh[SH_WEAPON]);  // weapon-type icon (Marle's bow, not a sword)
  if (wic) mBlitIndexed(fb, wic, 8, 8, x0 + 8, y, ipal);                 // weapon icon (attack)
  mNumR(h, fb, sh[0x3D], x0 + 40, y);               // attack  (sheet +0x3D)
  mBlitIndexed(fb, gen_opt0_icon_def, 8, 8, x0 + 48, y, ipal);           // shield icon (defence)
  mNumR(h, fb, sh[0x3E], x0 + 80, y);               // defence (sheet +0x3E)
  mDim = 0;
}
static void mPanelAtkDef(const CtMenuHost* h, uint8_t* fb, int textInset, int focusChar) {
  if (!h || !h->mem) return;
  for (int row = 0; row < 3; row++) {
    CtMenuPanel p;
    if (!CtMenuPanelData(h->mem, mPanelRowSlot(h->mem, row), &p)) continue;
    mPanelAtkDefOne(h, fb, textInset, 8 + row * MP_PITCH + 40, p.charId, p.charId != focusChar);
  }
}

// ---------------------------------------------------- Equip screen (A on a party member, C2/9C29 slot select)
// After picking a character the other party panels collapse, a slot-type label panel (Weapon/Helmet/Armor/Accessory)
// rises into the second left panel, and the hand moves right onto the cursor slot's equipped item (that item bright,
// the other three dim).
// C2/A38B: the slot's label = the 10-byte record C2/A3BA + slot*10, written by C2/EF65 (the 8x8 menu font, not the
// VWF) from BG1 cell $3086 = column 3 (x 24) of the label row; the records carry their own spaces ("  Weapon  ",
// "  Helmet  ", "  Armor   ", "Accessory"), so Weapon/Helmet/Armor start at x 40 and Accessory at x 24.
static void mSlotLabel(const CtMenuHost* h, uint8_t* fb, int slot, int y) {
  if (!h || !h->rom || h->romLen < 0x2A3BA + 40 || !h->drawGlyph) return;
  if (y < mClipTop || y >= mClipBot) return;
  const uint8_t* r = CtRomPtr(h->rom, h->romLen, 0x2A3BA + (slot & 3) * 10, 10);
  for (int k = 0; k < 10; k++) {
    if (!r[k]) break;                                                   // C2/EF7E: 00 ends the string
    if (r[k] != 0xFF && r[k] != 0xEF) h->drawGlyph(fb, r[k], 24 + 8 * k, y);
  }
}
// One char panel (name/HP/MP) at the top and its ATK/DEF row, from a charId (the Equip screen shows only the picked
// member, not the party column).
static void mEquipCharPanel(const CtMenuHost* h, uint8_t* fb, int charId, int yOff, int dim) {
  const uint8_t* sh = h->mem + 0x2600 + (size_t)(charId & 7) * SH_STRIDE;
  CtMenuPanel p;                                       // build the panel from the char sheet
  p.charId = charId & 7; p.name = CtGetName(p.charId); p.lv = sh[0x12];
  p.hp = sh[3] | (sh[4] << 8); p.hpMax = sh[0x3F] | (sh[0x40] << 8);   // +0x3F (string 06)
  p.mp = sh[7] | (sh[8] << 8); p.mpMax = sh[9] | (sh[10] << 8);
  // The candidate list's preview: string 09 `goto 0114 / PAL $0D57 / NUM $9B25 f22` -- the new effective max over
  // the panel's, in C2/A61F's colour ($9B25 against $9ACF, 16-bit: 00 same / 08 higher / 1C lower).
  if (mEqPrevOn && !dim) {
    int nw = mEqPrevNew[9] | (mEqPrevNew[10] << 8), od = mEqPrevOld[9] | (mEqPrevOld[10] << 8);
    p.hpMax = nw;
    mPanelHpMaxDim = (nw == od) ? 0 : (nw > od) ? 5 : 2;
  }
  mCharPanel(h, fb, MP_X, 8 + yOff, &p, 8, dim);       // top panel (yOff animates it up on entry; dim = pushed box)
  mPanelHpMaxDim = -1;
  int y = 8 + 40 + yOff;                               // row6 = the ATK/DEF row (weapon icon + ATK, shield + DEF)
  const uint32_t* ipal = dim ? mDimPal(gen_opt0_pal0) : gen_opt0_pal0;
  const uint8_t* wic = mEquipIcon(sh[SH_WEAPON]);      // weapon-type icon (per equipped weapon)
  mDim = dim;
  if (wic) mBlitIndexed(fb, wic, 8, 8, 32, y, ipal);
  if (!dim) mDim = mEqPrevDim(7);
  mNumR(h, fb, dim ? sh[0x3D] : mEqPrevVal(sh, 7), 64, y);         // Attack (the preview's value and palette)
  mDim = dim;
  mBlitIndexed(fb, gen_opt0_icon_def,    8, 8, 72, y, ipal);
  if (!dim) mDim = mEqPrevDim(8);
  mNumR(h, fb, dim ? sh[0x3E] : mEqPrevVal(sh, 8), 104, y);        // Defence
  mDim = 0;
}
// ---- The candidate list, drawn by the ROM in both gear substates ----
// C2/A569 writes the 8 rows into the BG1 buffer at `$7E:4FC6 + row*0x80` and neither substate rewrites them; what
// moves is the window. $7E:4FC6 is BG1 map row 7 col 3 ($7E:4E00 -> VRAM word $6800, descriptor $C2:FC1B) = x24, and
// the list's box climbs 32 px when substate 3 opens (the $FF:BEA4 block copy and the $0D93 scrollbar-Y delta):
//     substate 2 (gear select)   rows at y = 108 + row*12
//     substate 3 (candidate list) rows at y =  76 + row*12
// Row pitch 12 = the nav graph's list cells 4..11 ($7E:9380: y 75,87,...,159), where the hand sits in substate 3.
#define CT_MENU_EQ_LIST_Y     76   /* substate-3 row 0 (the list box top, tile row 8, + 12) */
#define CT_MENU_EQ_LIST_RISE  32   /* $FF:BEA4's 4-tile-row block copy == $0D93 0x62 -> 0x42 */
/* The substate-3 right box ends at chrome tile row 21 (y168..175), see ctMenuChromeEquipListTM. */
#define CT_MENU_EQ_LIST_DETAIL_BOT (22 * 8)
static void mEquipCandRows(const CtMenuHost* h, uint8_t* fb, const uint8_t* cand, const uint8_t* cnt,
                           int n, int cell, int scroll, int yOff, int hand) {
  if (!cand || !cnt) return;
  long rl = h->romLen; const uint8_t* rom = h->rom;
  for (int row = 0; row < 8; row++) {                       // up to eight visible rows ($0DD8 = page 8),
    int i = scroll + row;                                   //   scrolled by $0FC8
    if (i >= n) break;
    char nm[16] = ""; CtMenuItemName(rom, rl, cand[i], nm, sizeof nm);
    mText(h, fb, nm, 24, CT_MENU_EQ_LIST_Y + yOff + row * 12);   // the name only: C2/A59A draws no count ($0F62,
                                                                  //   C2/8953's, is never shown; menu_equip_list_no_count.md)
  }
  // The hand on the highlighted candidate, at its nav record: C2/A321's $FF:BE6D loads the graph $FF:B731 = a length
  // byte (0x48, twelve records) then [x, y, up, down, left, right]; cells 4-11 are the list's rows, (8, 75 + 12 * row),
  // one line above the row's text origin.
  if (hand && rom && rl > 0x3FB732L + 12 * 6) { int row = cell - scroll;
    if (row >= 0 && row < 8) { long r = 0x3FB732L + (long)(4 + row) * 6;
                               mHandCursor(h, fb, CtRomByte(rom, 0x400000L, (long)(r)), CtRomByte(rom, 0x400000L, (long)(r + 1)) + yOff); } }
}
// `stage` = where the panel's build stands. 0: the gear select (substate 2). 1: C2/A321's frame, the last of the entry
// slide -- the left column built (BE6D's copies, uploaded by FBE3/FBFF), the right detail still the member select's
// (all four rows bright), no hand (the loop's INC $0D9B still holds it) and no list yet. 2: the next frame, C2/9C5E-9C67
// -- the list drawn (C2/A569, FC1B) and the hand on cell 0 of the nav graph BE6D loaded; the rows dim on the frame
// after, when substate 2's C2/A3E2 first runs.
void CtMenuRenderEquip(const CtMenuHost* h, uint8_t* fb, int charId, int slot, int stage,
                       const uint8_t* cand, const uint8_t* cnt, int n) {
  if (!h || !fb) return;
  if (slot < 0) slot = 0; else if (slot > 3) slot = 3;
  // the background (the BG2 chrome) is laid by the caller (CtMenuChrome)
  mEquipCharPanel(h, fb, charId, 0, 0);                // only the picked member's panel (others collapsed)
  // (the Equip screen has no TIME/G box, unlike the char select)
  mSlotLabel(h, fb, slot, 80);                         // the slot-type label (C2/A38B)
  // C2/9C61 builds (C2/8953) and draws (C2/A569) the candidate list while the cursor is still on the gear rows (nav
  // cells 0..3), so no hand here. The list is BG3, which shows on lines 73-200 only through HDMA channel 3 (the TM
  // table at $9180: 72 lines TM 13, then TM 17); C2/9ECD turns it on every frame of the gear select (`LDA #4D / STA
  // $0D13`, C2/9EE6), while C2/A321's build left $0D13 = C5. So on C2/9C5E's frame (stage 2) the rows are in VRAM but
  // their layer is off; they show with the dims, from C2/9ECD's first frame.
  if (stage == 0) mEquipCandRows(h, fb, cand, cnt, n, -1, 0, CT_MENU_EQ_LIST_RISE, 0);
  mDetailRight(h, fb, charId, stage ? -1 : slot);      // full detail, the cursor slot bright, the others dim
  if (stage != 1) mHandCursor(h, fb, 120, eqRowY[slot] - 1);   // the hand on the cursor slot's item (pitch 12)
}
// The list box's climb, substate 2 -> 3 (C2/A41A -> C2/A4AF). On A over a gear row the ROM clears the list rows and
// runs one pass per frame (C2/A518 then `JSR C2/821E` while `$96A3 < 0x20`): each adds 4 to the two offset-per-tile
// tables it laid (`$5CC2` = the left column's 11 columns from $60FF, `$5D42` = 27 columns from $402F) and to `$96A3`,
// and takes 4 off the band's line `$96A0` (0x70). Eight passes of 4 px: the [slot label][list] column climbs 32 px in
// under the picked member's box (which stays on top: what passes y64 is hidden), and the description bar climbs behind
// it from the window's bottom; then C2/A463 draws the list (C2/A569) with the hand. The right-hand detail does not
// move. `s` = the pass count * 4 (4..28); the background is the gear panel's chrome.
static uint8_t mSlideTxt[MH * MW * 3];                  // the BG1 layer the slides move and cut per line (below)
static void mSlideTxtClear(void);
static void mSlideTxtBlit(uint8_t* fb, int srcOff, int y0, int y1);
static void mSlideTxtBlitX(uint8_t* fb, int srcOff, int y0, int y1, int x0, int x1);
void CtMenuRenderEquipListRise(const CtMenuHost* h, uint8_t* fb, int charId, int slot, int s, int gearHand) {
  if (!h || !fb) return;
  if (slot < 0) slot = 0; else if (slot > 3) slot = 3;
  if (s < 0) s = 0; else if (s > CT_MENU_EQ_LIST_RISE) s = CT_MENU_EQ_LIST_RISE;
  mChromeRowsTM(fb, ctMenuChromeEquipTM, 8, 25, 2, 13, -s, -1, 64, CT_MENU_EQUIP_MARBLE_Y);   // the column, list cleared
  // the slot-type label rides it and goes under the box line by line: the band's lines from 64 show the column, the
  // lines above show the member's box
  mSlideTxtClear(); mSlotLabel(h, mSlideTxt, slot, 80); mSlideTxtBlit(fb, s, 64, CT_MENU_EQUIP_MARBLE_Y);
  if (s < CT_MENU_EQ_LIST_RISE) {                              // (when settled the list's own render draws these
    mEquipCharPanel(h, fb, charId, 0, 0);                       //  two, once: a glyph does not clear its cell, so a
    mDetailRight(h, fb, charId, slot);                          //  second pass would leave old digits under the new)
  }                                                             // the picked member's box on top; the detail stays put
  if (gearHand) mHandCursor(h, fb, 120, eqRowY[slot] - 1);   // the gear hand (not while the list holds the cursor)
  // the description bar: the candidate list chrome's tile rows 22-25 (C2/A41A's $FF:BEEC block copy puts them there
  // from the off-screen backup), climbing from the window's bottom, its top at 176 + (32 - s); once the loop has exited
  // (s = 32) the bar has its own ramp (mWinGradEquipList)
  if (s >= CT_MENU_EQ_LIST_RISE) mChromeRowsGrad = mWinGradEquipList();
  mChromeRowsTM(fb, ctMenuChromeEquipListTM, 22, 25, 2, 29, CT_MENU_EQ_LIST_RISE - s, -1, 16, CT_MENU_EQUIP_MARBLE_Y);
  mChromeRowsGrad = 0;
}
// The candidate list with the cursor in it (substate 3): the same rows risen 32 px, the hand on the highlighted row,
// and the bottom bar with its line (C2/A44A -> C2/F2F3). The right-hand detail is clipped to the shortened right box:
// the ROM leaves "EXP"/"NEXT" in the BG1 buffer and the window masks them.
// The one-row slide (C2/EB9B + C2/EB1F, the list's own copy of the channel-6 BG3-V table): the list is a BG3 buffer of
// 16 px slots -- the eight rows at slots 0-7, the incoming row at slot 8 ($53C6) -- shown 12 lines of each slot per
// 12-line band (V + 4 a band). C2/EB9B copies the bands with one more below (V + 4) and one above (mapped to slot 8
// too), and each step C2/EB1F rebuilds the live table with every V + $0DAB and the first band $0DAB lines shorter, so
// the rows, the incoming one included, move $0DAB px as one strip, cut to the list's lines 72-167.
#define CT_MENU_EQ_LIST_BAND0 72       /* the list's first band line (the channel-6 table: 72 lines at 1F0 first) */
static void mEqListSlideRows(const CtMenuHost* h, uint8_t* fb, const uint8_t* cand, const uint8_t* cnt, int n,
                             int scroll, int slideO) {
  long rl = h->romLen; const uint8_t* rom = h->rom;
  mListStripClear();
  int r0 = slideO > 0 ? scroll - 1 : scroll, k0 = slideO > 0 ? 0 : -1;   // Down: the old window + the new bottom row;
  for (int k = 0; k < 9; k++) {                                          //   Up: the new top row + the old window
    int i = r0 + k;
    if (i < 0 || i >= n) continue;
    char nm[16] = ""; CtMenuItemName(rom, rl, cand[i], nm, sizeof nm);
    int y = CT_MENU_EQ_LIST_Y + (k0 + k) * 12;
    mText(h, mListStrip, nm, 24, y);                                   // (C2/9F84's A59A: the name only)
  }
  mListStripBlit(fb, CT_MENU_EQ_LIST_BAND0, CT_MENU_EQ_LIST_BAND0 + 96, slideO);   // fb(y) = strip(y + $0DAB)
}
void CtMenuRenderEquipList(const CtMenuHost* h, uint8_t* fb, int charId, int gearSlot,
                           const uint8_t* cand, const uint8_t* cnt, int n, int cell, int scroll, int hand, int prev,
                           int descN, int prevId, int slideO) {
  if (!h || !fb || !cand) return;
  if (gearSlot < 0) gearSlot = 0; else if (gearSlot > 3) gearSlot = 3;
  if (prev && prevId >= 0) mEqPrevSet(h, charId, gearSlot, prevId);   // C2/A5C3's last preview (the slide skips it)
  else if (prev && cell >= 0 && cell < n) mEqPrevSet(h, charId, gearSlot, cand[cell]);   // C2/A5C3 (the cursor is live)
  mEquipCharPanel(h, fb, charId, 0, 0);                     // the picked member's panel stays on top
  mClipBot = CT_MENU_EQ_LIST_DETAIL_BOT;
  mDetailRight(h, fb, charId, gearSlot);                    // ... and so does the right-hand detail
  mClipBot = MH;
  mEqPrevOn = 0;
  if (slideO) {                                             // the strip moves; the hand stays on its nav record
    mEqListSlideRows(h, fb, cand, cnt, n, scroll, slideO);
    mEquipCandRows(h, fb, cand, cnt, 0, cell, scroll, 0, hand);
  } else mEquipCandRows(h, fb, cand, cnt, n, cell, scroll, 0, hand);
  // the bottom bar: C2/A5DF's C2/F2F3, the description of $0D77 (the last id it staged), typed by C2/FA49's string
  // task one character a frame in the VWF, as on the Item screen
  { char d[40];
    if (descN > 0 && h->mem && CtMenuEquipDesc(h->mem[0x0D77], d, sizeof d) > 0) {   // 35 + NUL
      if (descN < (int)strlen(d)) d[descN] = 0;
      if (h->vwfDraw) h->vwfDraw(fb, d, 24, 188, ctLoaderVwfTone);
      else mText(h, fb, d, 24, 188);
    } }
}

// ---- C2/A71E + C2/A7B7: the gear panel's entry slide (disasm/notes/menu2_shell_exit.md, "The gear panel's entry
// slide") ----
// C2/A71E builds the gear panel's left column off-screen and uploads it; BG mode 2's offset-per-tile brings it in:
// each of 24 steps C2/A7B7 adds 6 to $5CC2 (row 59, the new column) and $0DAB to $5D42 (row 61, the party column) and
// rewrites the channel-6 BG3-V table, so the new column rises 6 px a frame over the TIME box and the lower panels;
// after $0D25 = (3 - $0412) * 8 steps the party column scrolls up with it. The cursor is off for the whole loop.
// `k` = the steps done (1..23), `selSlot` = $0412, `dimSlot` = the member C2/9E76 last left bright. The caller lays the
// member select's chrome (CtMenuChrome screen 1).
// mSlideTxt: a BG1 layer drawn apart, so it can be moved and cut per line (declared above CtMenuRenderEquipListRise).
static void mSlideTxtClear(void) {                      // (1,2,3) cannot be a PPU colour: (v<<3)|(v>>2) is 0 or >= 8
  for (int i = 0; i < MH * MW; i++) { mSlideTxt[i * 3] = 1; mSlideTxt[i * 3 + 1] = 2; mSlideTxt[i * 3 + 2] = 3; }
}
// fb(x, y) = the layer's (x, y + srcOff) over lines [y0, y1) and the pixels [x0, x1)
static void mSlideTxtBlitX(uint8_t* fb, int srcOff, int y0, int y1, int x0, int x1) {
  for (int y = y0 < 0 ? 0 : y0; y < y1 && y < MH; y++) {
    int sy = y + srcOff;
    if (sy < 0 || sy >= MH) continue;
    for (int x = x0 < 0 ? 0 : x0; x < x1 && x < MW; x++) {
      const uint8_t* s = mSlideTxt + ((size_t)sy * MW + x) * 3;
      if (s[0] == 1 && s[1] == 2 && s[2] == 3) continue;
      memcpy(fb + ((size_t)y * MW + x) * 3, s, 3);
    }
  }
}
// fb(x, y) = the layer's (x, y + srcOff) over lines [y0, y1) and screen columns 2-13
static void mSlideTxtBlit(uint8_t* fb, int srcOff, int y0, int y1) {
  for (int y = y0 < 0 ? 0 : y0; y < y1 && y < MH; y++) {
    int sy = y + srcOff;
    if (sy < 0 || sy >= MH) continue;
    for (int x = 16; x < 112; x++) {
      const uint8_t* s = mSlideTxt + ((size_t)sy * MW + x) * 3;
      if (s[0] == 1 && s[1] == 2 && s[2] == 3) continue;
      memcpy(fb + ((size_t)y * MW + x) * 3, s, 3);
    }
  }
}
// C2/9DAF: C2/A216 writes the incoming member's panel into the off-screen slot, C2/9EB0 tones its box dim (Y = 100C),
// the column is block-copied (C2/EF05, BG1 and BG2) so that it and the new box form one strip, and each of twelve steps
// adds $0D22 (+-4) to the column's offset-per-tile V ($5D42, row 61: screen columns 2-13) and yields: the column slides
// 4 px a frame, rigid, cut to lines 16-159 (the TIME box below is row 63's, still). The dim, $0412 / $71, the blip and
// the right-hand detail (C2/A22F) wait for the twelfth step.
void CtMenuRenderEquipMemberSlide(const CtMenuHost* h, uint8_t* fb, int oldScroll, int dir, int o, int dimRow,
                                  int handRow, int detailChar) {
  if (!h || !fb || !h->mem) return;
  int keep = mPanelScroll;
  mPanelScroll = oldScroll < 0 ? 0 : oldScroll;          // the buffer still holds the old window
  int dimChar = mRowChar(h->mem, dimRow, detailChar);
  int d = dir > 0 ? o : -o;                              // fb(y) = the strip (y + d)
  // BG2: the three boxes as C2/9E76 left them, and the incoming one (dim) past the end the column moves towards
  static uint16_t scratch[CT_MENU_CHROME_TMW * CT_MENU_CHROME_TMH];
  const uint16_t* cs = mChromeApplyCharSel(ctMenuChromeEnteredTM[0], scratch, dimRow);
  mChromeRowsTM(fb, cs, 2, 19, 2, 13, -d, -1, 16, 160);
  mChromeRowsTM(fb, cs, 2, 7, 2, 13, (dir > 0 ? 144 : -48) - d, 1, 16, 160);
  // BG1: the old window's panels and the incoming member's (row 3 below, row -1 above), drawn apart and moved
  mSlideTxtClear();
  int base = dir > 0 ? 0 : MP_PITCH;                     // (an Up's row -1 would sit above the layer: all one row lower)
  for (int row = dir > 0 ? 0 : -1; row <= (dir > 0 ? 3 : 2); row++) {
    CtMenuPanel p;
    int sl = mPanelRowSlot(h->mem, row);
    if (!CtMenuPanelData(h->mem, sl, &p)) continue;
    mCharPanel(h, mSlideTxt, MP_X, 8 + row * MP_PITCH + base, &p, 8, p.charId != dimChar);
    mPanelAtkDefOne(h, mSlideTxt, 8, 8 + row * MP_PITCH + 40 + base, p.charId, p.charId != dimChar);
  }
  mSlideTxtBlit(fb, d + base, 16, 160);
  mPanelScroll = keep;
  mTimeGold(h, fb);
  mDetailRight(h, fb, detailChar, -1);
  if (handRow >= 0 && handRow < 3) mHandCursor(h, fb, 8, 8 + handRow * MP_PITCH + 27);
}
// C2/B5C8: the incoming member's panel into BG1 (C2/BAFC its sprite block, C2/BA2F its "MP Used"), the BG1 column
// block-copied beside it (C2/EF05 from (5,2), 10 x 18), then twelve steps of $0D22 (+-4): $0D30 -= $0D22 (the OAM Y the
// party sprites are drawn with), $0DAB, the offset-per-tile word $24 += $0D22 into $5D44 / $5DC4 (bit 13 alone: BG1,
// not BG2, so the boxes stay while the text moves), $0D95 +-1, a yield; the twelfth settles and C2/B54D goes on.
void CtMenuRenderTechMemberSlide(const CtMenuHost* h, uint8_t* fb, int oldScroll, int dir, int o, int handRow) {
  if (!h || !fb || !h->mem) return;
  int keep = mPanelScroll;
  mPanelScroll = oldScroll < 0 ? 0 : oldScroll;
  int d = dir > 0 ? o : -o;
  const uint16_t* tm = ctMenuChromeEnteredTM[2];
  // The layers' band narrows for the slide: C2/B5C8 switches the HDMA set ($0D13 = 6B), and the TM table it enables
  // shows BG1 and OBJ on lines 24-151 only (TM 17 there, 02 -- BG2 alone -- above and below, against 17 on lines 16-160
  // when settled). So lines 16-23 and 152-159 are the boxes alone, and in between the boxes carry the moved text and
  // sprites.
  const int8_t* gk = mChromeRowsGrad; mChromeRowsGrad = mWinGrad(2);   // (the Tech screen's window ramp: option 2's)
  mChromeRowsTM(fb, tm, 2, 2, 0, 31, 0, -1, 16, 24);
  mChromeRowsTM(fb, tm, 18, 19, 0, 31, 0, -1, 152, 160);
  mChromeRowsTM(fb, tm, 2, 19, 2, 14, 0, -1, 24, 152);
  mChromeRowsGrad = gk;
  // BG1 (columns 3-14: $5D44's twelve words, bit 13 = BG1 only) and the sprites ($0D30 = -d on their OAM Y) move
  // together: the old window's panels and "MP Used" and the incoming member's, drawn apart
  mSlideTxtClear();
  int base = dir > 0 ? 0 : MP_PITCH;
  for (int row = dir > 0 ? 0 : -1; row <= (dir > 0 ? 3 : 2); row++) {
    CtMenuPanel p;
    if (!CtMenuPanelData(h->mem, mPanelRowSlot(h->mem, row), &p)) continue;
    mCharPanel(h, mSlideTxt, MP_X, 8 + row * MP_PITCH + base, &p, 24, 0);
    mText(h, mSlideTxt, "MP Used", 40, 8 + row * MP_PITCH + 40 + base);
    mBlitPartySpr(h, mSlideTxt, p.charId, MP_SPR_CX_TECH, 56 + row * MP_PITCH + base, 0);
  }
  mSlideTxtBlitX(fb, d + base, 24, 152, 16, 120);
  mPanelScroll = keep;
  if (handRow >= 0 && handRow < 3) mHandCursor(h, fb, 8, 8 + handRow * MP_PITCH + 27);
}
void CtMenuRenderEquipEntry(const CtMenuHost* h, uint8_t* fb, int selSlot, int dimSlot, int k) {
  if (!h || !fb || !h->mem) return;
  if (selSlot < 0) selSlot = 0; else if (selSlot > 2) selSlot = 2;
  if (k < 1) k = 1; else if (k > 24) k = 24;
  int selChar = mRowChar(h->mem, selSlot, h->mem[0x2980] & 7);          // (rows of the member window)
  int dimChar = (dimSlot >= 0 && dimSlot < 3) ? mRowChar(h->mem, dimSlot, selChar) : selChar;
  int b26 = 6 * k;                                      // $26
  int b24 = 0x30 - 6 * k; if (b24 < 0) b24 = 0;         // $24
  int b22 = 0x90 - (6 * k > 0x30 ? 6 * k - 0x30 : 0);   // $22
  int push = (3 - selSlot) * 8;                         // $0D25
  int d = k > push ? 6 * (k - push) : 0;                // $5D42 - 0x61FF
  int topA = 16 + b22 + b24;                            // the $26 band's first line (= 208 - 6k)
  // the member select as it stands, without its hand
  mPartyPanels(h, fb, 8, dimChar);
  mPanelAtkDef(h, fb, 8, dimChar);
  mTimeGold(h, fb);
  mDetailRight(h, fb, selChar, -1);
  if (d > 0) {                                          // the party column, lines 16 .. 16+$22: its own map d lines down
    static uint16_t scratch[CT_MENU_CHROME_TMW * CT_MENU_CHROME_TMH];
    const uint16_t* cs = mChromeApplyCharSel(ctMenuChromeEnteredTM[0], scratch, dimSlot);
    mChromeRowsTM(fb, cs, 2, 19, 2, 13, -d, -1, 16, 16 + b22);
    mSlideTxtClear();
    mPartyPanels(h, mSlideTxt, 8, dimChar);
    mPanelAtkDef(h, mSlideTxt, 8, dimChar);
    mSlideTxtBlit(fb, d, 16, 16 + b22);
  }
  // the new column: BG2 rows 40-57 = the built panel's rows 8-25, BG1 = "Weapon" alone
  mChromeRowsTM(fb, ctMenuChromeEquipTM, 8, 25, 2, 13, topA - 64, -1, topA, topA + b26);
  mSlideTxtClear();
  mSlotLabel(h, mSlideTxt, 0, 80);                      // string 7 at (5,42) = the Weapon record's cells from column 3
  mSlideTxtBlit(fb, 64 - topA, topA, topA + b26);
}
// ---- C2/A85C: the way back, the entry slide's machine run the other way (disasm/notes/menu2_shell_exit.md, "The gear
// panel's way back") ----
// B (or Left) on the gear select yields at once; the next frame builds in the buffers only; the frame after is the setup
// ($5CC2 = 0x60FF, $5D42 = 0x6000 | (48 * $0412 - 1), C2/9E76's tone, the hand suppressed). Then 24 steps (C2/A970):
// $5CC2 -= 6, $5D42 -= $0DAB (6 for $0D25 = 8 * $0412 steps, 0 for the lead), and the channel-6 table rewritten with
// $22 = 0x30 + 6 per step: the gear column sinks 6 px a frame, cut at line 207, while the party column opens above it.
// The right side keeps the gear panel's detail.
// `j` = the steps done (0 = the setup frame .. 23; the 24th is also C2/A2CD's rebuild, drawn as the member select).
void CtMenuRenderEquipExit(const CtMenuHost* h, uint8_t* fb, int charId, int slot, int selSlot, int j) {
  if (!h || !fb || !h->mem) return;
  if (slot < 0) slot = 0; else if (slot > 3) slot = 3;
  if (selSlot < 0) selSlot = 0; else if (selSlot > 2) selSlot = 2;
  if (j < 0) j = 0; else if (j > 24) j = 24;
  int selChar = mRowChar(h->mem, selSlot, h->mem[0x2980] & 7);          // (rows of the member window)
  int push = 8 * selSlot;                               // $0D25 when $0412 != 0 ($0DAB = 6 for those steps)
  int d = selSlot ? 48 * selSlot - 6 * (j < push ? j : push) : 0;   // ($5D42 & 0x1FF) + 1
  int b22 = 0x30 + 6 * j;                               // $22
  int topA = 16 + b22;                                  // the 0x1D0 band's first line
  mDetailRight(h, fb, charId, slot);                    // (the caller laid the gear panel's chrome)
  // the party column: BG2 rows 2-25 as BE5B + C2/9E76 left them
  static uint16_t tm[CT_MENU_CHROME_TMW * CT_MENU_CHROME_TMH];
  memcpy(tm, ctMenuChromeEquipTM, sizeof tm);
  for (int r = 8; r <= 25; r++)
    for (int c = 2; c < 14; c++) tm[r * CT_MENU_CHROME_TMW + c] = tm[(2 + (r - 2) % 6) * CT_MENU_CHROME_TMW + c];
  for (int k = 0; k < 3; k++)                           // C2/9E76 over panels 0-2 (rows 2-19), BG1 and BG2
    for (int r = 2 + 6 * k; r < 8 + 6 * k; r++)
      for (int c = 2; c < 14; c++) {
        uint16_t w = tm[r * CT_MENU_CHROME_TMW + c];
        int attr = ((w >> 8) & 0x0F) | (k == selSlot ? 0x00 : 0x10);
        tm[r * CT_MENU_CHROME_TMW + c] = (uint16_t)((w & 0x00FF) | (attr << 8));
      }
  mChromeRowsTM(fb, tm, 2, 25, 2, 13, -d, -1, 16, topA);
  mSlideTxtClear();
  mPartyPanels(h, mSlideTxt, 8, selChar);
  mPanelAtkDef(h, mSlideTxt, 8, selChar);
  mTimeGold(h, mSlideTxt);
  mSlideTxtBlit(fb, d, 16, topA);
  // the gear column: BG2 rows 40-57, BG1 rows 41-42 = C2/A38B's copy of the label rows 9-10
  mChromeRowsTM(fb, ctMenuChromeEquipTM, 8, 25, 2, 13, topA - 64, -1, topA, CT_MENU_EQUIP_MARBLE_Y);
  mSlideTxtClear();
  mSlotLabel(h, mSlideTxt, slot, 80);
  mSlideTxtBlit(fb, 64 - topA, topA, CT_MENU_EQUIP_MARBLE_Y);
}

// ---------------------------------------------------- Screen 5 = Form / party exchange (C2/C621)
// The three active-party panels and the exchange picker. `cursor` = the slot the hand rests on ($0054); `picked` = the
// first-picked slot (-1 = none). See CtMenuRenderForm in menu.h and CtMenuFormSwap.
// C2/C649, where the Form picker's cursor starts:
//     2C649  LDA $0F02,X / AND #C0 / BNE ->loop / STX $54
// over C2/CD2B's compacted panel array: skip every cell that is locked (0x40) or empty (0x80). With Crono locked by
// $7F01DF the screen opens with the hand on cell 1 (cursor OAM $0770 = (0x38, 0x53) = nav record 1).
int CtMenuFormEntryCell(const uint8_t* mem, const uint8_t* glb) {
  uint8_t fp[9];
  CtMenuFormPanels(mem, glb, fp);
  for (int k = 0; k < 3; k++) if (!(fp[k] & 0xC0)) return k;
  return 0;
}
// ---------------------------------------------------- Screen 7 = the exchange with reserves (C2/C7A6)
// Menu mode 5 (C2/808F[5] = $07), opened by the field's Y button at storyline >= 0x49 (C0/1985 -> C0/19CE) and by the
// script op `C8 00` (C0/0D04 -> C0/19C7), both with $71 = 0. It is the Form screen's machinery (C2/C6B9, C2/C805,
// C2/C70C are shared) run with $0D36 = $71 = 0, so C2/CD2B scans the reserves and the graph is the two-column one.
// These run on the menu's WRAM, as the ROM does (disasm/notes/menu2_form_screen.md 2.3-2.11).

// C2/CD2B: `$0F02..$0F0A = 80`, `STZ $0F0B`; the actives compacted from $0F02 (`slot | 40` when $7F01DF & FF/F9BB[id]
// locks the character, which also takes it out of $0F0B); with $0D36 = 0 the reserves $2983..$2988 compacted from $0F05
// and their count in $0F0C (C2/CD72-CD96). With $0D36 != 0 the reserve scan is skipped and $0F0C is left alone.
void CtMenuExchRoster(uint8_t* mem, const uint8_t* glb) {
  if (!mem) return;
  for (int i = 0; i < 9; i++) mem[0x0F02 + i] = 0x80;             // 2CD2E-2CD3D
  mem[0x0F0B] = 0; mem[0x0F0C] = 0;                                // 2CD40 STZ $0F0B (16-bit: $0F0C too)
  int y = 0;
  for (int x = 0; x < 3; x++) {                                    // 2CD45-2CD70
    uint8_t a = mem[0x2980 + x];
    if (a & 0x80) continue;                                        // 2CD4F BMI
    mem[0x0F0B]++;                                                 // 2CD51
    uint8_t v = (uint8_t)x;
    if (glb && (glb[0x01DF] & MFORM_LOCKMASK(a))) { mem[0x0F0B]--; v |= 0x40; }   // 2CD55-2CD62
    mem[0x0F02 + y++] = v;                                         // 2CD64-2CD69
  }
  if (mem[0x0D36]) return;                                         // 2CD72 LDA $0D36 / BNE ->2CD99
  int r = 0;
  for (int x = 3; x < 9; x++) {                                    // 2CD7D-2CD90
    if (mem[0x2980 + x] & 0x80) continue;                          // 2CD84 BMI
    mem[0x0F05 + r++] = (uint8_t)x;                                // 2CD86-2CD8C (`ORA $00`, $00 = 0 on this entry)
  }
  mem[0x0F0C] = (uint8_t)r;                                        // 2CD96 STY $0F0C
}
// C2/C649 (screen 7 enters it with X = 0, C2/C7BE): the first panel entry that is neither locked nor empty, over all
// nine (with every active member locked the hand starts on the reserve column), then both picks to FF and `INC $68`.
void CtMenuExchEntry(uint8_t* mem) {
  if (!mem) return;
  int x = 0;
  while (x < 9 && (mem[0x0F02 + x] & 0xC0)) x++;                  // 2C648-2C64E
  mem[0x0054] = (uint8_t)(x < 9 ? x : 0);                          // 2C650 STX $54
  mem[0x0F00] = 0xFF; mem[0x0F01] = 0xFF;                          // 2C652-2C657
}
// The screen's setup list copies the cursor graph $FF:B77A (a length byte 2A, then seven 6-byte cells: hand X, hand Y,
// up, down, left, right; bit 7 of a neighbour = "ran off the reserve list") to $7E:9380 verbatim. The X menu's Form
// overwrites the hand X with 38 (C2/CB18); screen 7 keeps the table's 08 / E8.
void CtMenuExchGraph(uint8_t* mem, const uint8_t* rom, long romLen) {
  if (!mem || !rom || romLen < 0x3FB77A + 43) return;
  CtRomCopy(mem + 0x9380, rom, romLen, 0x3FB77B, 42);
}
// The exchange's BG3 wave. C2/C7AC's C2/CE86 (at P) runs `JSL FF/F677` and queues two VRAM DMAs through C2/8385
// (C2/CE96: $7E:B000 -> VRAM $7000, $600 bytes, BG3's characters; $FF:FC14: $7E:4E00 -> VRAM $6800, $1000 bytes, BG3's
// map); its C2/CE9D sets BG3SC = $68, clears $0F10-$0F11, copies $FF:D57A (12 bytes) to $0F12 and spawns the task
// C2/CECE: `$0D13 |= 10`, two frames, `JSL FF/F716`, again. BG3 scrolls at VOFS $0D0E, HOFS by HDMA through an indirect
// table at $9200 -- fourteen `90 40 92` (16 lines each, repeat) -- so line y reads the word at $9240 + 2 * (y & 15).
// FF/F716 runs at P and then every second frame through the close's fade and `$2141` wait, none from the `11` post's
// JSLs on (disasm/notes/menu2_form_screen.md 2.11).
// FF/F677: C3/0002 decompresses $FF:E8DC to $7E:B000; `$0D0E = 01FF`; the map at $7E:4E00: six rows of 32, each 16
// words from $FF:F6F2[k] counting up and the same 16 again (`MVN` of the row's first $20 bytes), then `MVN` $4E00 ->
// $4F80, $680 bytes -- a forward copy over itself, so the six rows repeat to the map's end; $FF:F706 (16 bytes) to
// $94B0 (CGRAM 24-31, BG3's palettes 6 and 7) and $FF:F6FE (8 bytes) to $9100.
void CtMenuExchWaveInit(uint8_t* m, const uint8_t* rom, long romLen) {
  if (!m || !rom || romLen < 0x3FF7D8) return;
  { static uint8_t out[0x10000]; size_t n = ct_lz_decompress_bounded(rom, (size_t)romLen, out, sizeof out, 0x3FE8DC);
    if (n > 0x5000) n = 0x5000;
    memcpy(m + 0xB000, out, n); }                                  // 3FF678-3FF690 JSL C3/0002
  m[0x0D0E] = 0xFF; m[0x0D0F] = 0x01;                              // 3FF694-3FF697
  int y = 0x4E00;
  for (int k = 0; k < 6; k++) {                                    // 3FF69A-3FF6CA
    int row = y, v = CtRomByte(rom, 0x400000L, (long)(0x3FF6F2 + 2 * k)) | (CtRomByte(rom, 0x400000L, (long)(0x3FF6F3 + 2 * k)) << 8);
    for (int i = 0; i < 16; i++, v++, y += 2) { m[y] = (uint8_t)v; m[y + 1] = (uint8_t)(v >> 8); }
    for (int i = 0; i < 0x20; i++) m[y + i] = m[row + i];          // 3FF6BA-3FF6BF MVN, $20 bytes
    y += 0x20;
  }
  for (int i = 0; i < 0x680; i++) m[0x4F80 + i] = m[0x4E00 + i];   // 3FF6CC-3FF6D5: byte by byte, over itself
  CtRomCopy(m + 0x94B0, rom, romLen, 0x3FF706, 16);                          // 3FF6D8-3FF6E1
  CtRomCopy(m + 0x9100, rom, romLen, 0x3FF6FE, 8);                           // 3FF6E4-3FF6ED
  m[0x0F10] = 0; m[0x0F11] = 0;                                    // 2CEA8 STZ $0F10 (16-bit)
  CtRomCopy(m + 0x0F12, rom, romLen, 0x3FD57A, 12);                          // 2CEB4-2CEBD MVN $7E,$FF
}
// The prompt's text (C2/C9E5, substate 0 at S0: C2/CA6D-CAD5). The string -- 4, "Who'll you" 05 "replace?", or 5 when
// `$0D7C` (16-bit) is set, "Can't" 05 "do that." -- from the table $FF:CF3B, drawn by C2/F905 into the 4bpp strip
// $7E:5E00: `$0214 = 81`, `$0210-$0212 = $0DC5 / 7E`, JSL C2/57DF, then JSL C2/5823 with `$0213 = 20` again while it
// stops on the line break `05` (`$0215`), each time 4 added to `$0211` (the next line $400 bytes on, C2/F923-F93F).
// C2/FBB4 lays each line's tiles in BG1's map buffer ($7E:2E00 = VRAM $5800): at (6, 21) eight from $0100 over eight
// from $0110, at (6, 23) from $0121 / $0131 (`AND #33EF`, the lower row `ORA #10`) -- the strip's tiles, VRAM $1000 on
// (C2/CAD9's DMA).
// The prompt's figure (C2/C9E5: C2/CA9E-CAB7 with D = $19C0, block 7): C2/F422 with `$00 = 48`, `$01 = 01` -- the
// type-1 table's record 0x48 (C2/F55C[1] = $E4:F023, 5 bytes a record: the $E4:F000 header index 7 + 48 = 0x4F) --
// `$0E-$0F = CA 20` (y 202, x 32), `$1A = 20` (attr: priority 2, palette 0). C2/F871 places each 16 x 16 unit at
// (x + dx, y + dy) of the assembly's offsets ($19E2.. = 15 D6 / F8 E4 / F8 D4 / F8 F4), the frame the block's +3 names
// (0); the tiles go up at P (the task C2/F657).
#define MEXCH_FIG_SPR 0x4F
static int mExchFigOn;
void CtMenuExchFigure(int on) { mExchFigOn = on; }
void CtMenuExchPrompt(uint8_t* m, const uint8_t* rom, long romLen) {
  if (!m || !rom || romLen < 0x3FD010) return;
  memset(m + 0x5E00, 0, 0x800);                                    // (the strip as the init left it: zero)
  m[0x020C] = (m[0x0D7C] | m[0x0D7D]) ? 5 : 4;                     // 2CA72-2CA7B
  m[0x0DC5] = 0x00; m[0x0DC6] = 0x5E;                              // 2CA7E-2CA81
  m[0x020D] = 0x3B; m[0x020E] = 0xCF; m[0x020F] = 0xFF;            // 2CA84-2CA8C
  m[0x0214] = 0x81;                                                // 2F905-2F911 (C2/F905: LDA #81)
  m[0x0210] = m[0x0DC5]; m[0x0211] = m[0x0DC6]; m[0x0212] = 0x7E;  // 2F914-2F91C
  CtVwfBegin(m, rom, romLen);                                      // 2F91F JSL C2/0003
  for (int guard = 0; guard < 16; guard++) {
    m[0x0213] = 0x20;                                              // 2F923-2F925
    if (CtVwfRun(m, rom, romLen) < 0) break;                       // 2F928 JSL C2/0009
    if (m[0x0215] != 5) break;                                     // 2F92C-2F931
    m[0x0211] = (uint8_t)(m[0x0211] + 4);                          // 2F933-2F93F
  }
  static const struct { int x, a; } lines[2] = { { 0x334C, 0x0100 }, { 0x33CC, 0x0121 } };   // 2CABA-2CACF
  for (int l = 0; l < 2; l++) {                                    // C2/FBB4
    int x = lines[l].x, a = lines[l].a;
    for (int k = 0; k < 8; k++, x += 2) {
      int v = a & 0x33EF, u = v | 0x10;
      m[x] = (uint8_t)v; m[x + 1] = (uint8_t)(v >> 8); m[x + 0x40] = (uint8_t)u; m[x + 0x41] = (uint8_t)(u >> 8);
      a = (u + 1) & 0xFFEF;
    }
  }
}
// FF/F716, one step of the task: `$0F10`, `$0F11` up (8-bit); every fourth (`$0F11 & 3` = 0) `$0D0C` up, `$0D0E` down and
// `$0F10` back by two (all 8-bit). Then 16-bit: `$00 = $0D0C`, X = `($0F10 * 2) & 3E` -- the `ASL` leaving bit 15 of the word
// in the carry -- Y = `$51`, and while Y < $20: `$9240,Y = $FF:F759,X + $00 + C` (the first sum takes the ASL's carry, the rest
// the CPY's clear one), X += 4, Y += 2.
void CtMenuExchWaveTick(uint8_t* m, const uint8_t* rom, long romLen) {
  if (!m || !rom || romLen < 0x3FF7D8) return;
  m[0x0F10]++; m[0x0F11]++;                                        // 3FF719-3FF71C
  if (!(m[0x0F11] & 3)) { m[0x0D0C]++; m[0x0D0E]--; m[0x0F10] -= 2; }   // 3FF71F-3FF72F
  int base = m[0x0D0C] | (m[0x0D0D] << 8);                         // 3FF734-3FF737
  int w = m[0x0F10] | (m[0x0F11] << 8), c = (w >> 15) & 1;         // 3FF739-3FF73C ASL A
  int x = (w << 1) & 0x3E, y = m[0x0051] | (m[0x0052] << 8);       // 3FF73D-3FF741
  while (y < 0x20) {                                               // 3FF743-3FF755
    int v = (CtRomByte(rom, 0x400000L, (long)(0x3FF759 + x)) | (CtRomByte(rom, 0x400000L, (long)(0x3FF75A + x)) << 8)) + base + c;
    m[0x9240 + y] = (uint8_t)v; m[0x9241 + y] = (uint8_t)(v >> 8);
    c = 0; x += 4; y += 2;
  }
}
// The table the picture reads. A frame shows what its NMI uploaded -- the OAM, BG3's VOFS ($0D0E), INIDISP, the colour
// math's double-buffered tables: the main thread's work of the frame before -- but the HDMA reads $9240 while the frame
// is drawn, after this frame's main thread (the rule's CPU runs before line 0) has stepped the task. So the port's
// picture of a frame takes BG3's HOFS from the step the next frame makes when one is due (CtMenuExchWaveNext, from
// field.c's schedule of the task).
static int mExchWaveNext;
void CtMenuExchWaveNext(int due) { mExchWaveNext = due; }
static void mExchWaveTable(const uint8_t* m, const uint8_t* rom, long romLen, uint8_t* out) {
  memcpy(out, m + 0x9240, 32);
  if (!mExchWaveNext || !rom || romLen < 0x3FF7D8) return;
  uint8_t f10 = (uint8_t)(m[0x0F10] + 1), f11 = (uint8_t)(m[0x0F11] + 1), d0c = m[0x0D0C];   // FF/F716 on copies
  if (!(f11 & 3)) { d0c++; f10 -= 2; }
  int base = d0c | (m[0x0D0D] << 8), w = f10 | (f11 << 8), c = (w >> 15) & 1;
  int x = (w << 1) & 0x3E, y = m[0x0051] | (m[0x0052] << 8);
  while (y < 0x20) {
    int v = (CtRomByte(rom, 0x400000L, (long)(0x3FF759 + x)) | (CtRomByte(rom, 0x400000L, (long)(0x3FF75A + x)) << 8)) + base + c;
    out[y] = (uint8_t)v; out[y + 1] = (uint8_t)(v >> 8);
    c = 0; x += 4; y += 2;
  }
}
// C2/C9AE, the re-drive when the landed entry is not selectable: a Left/Right press hops to the other column when the
// side it is on has nobody selectable (`$0F0B` / `$0F0C` = 0); otherwise the graph is walked once more along Up (bit 3
// of $5A) or Down (anything else, Left/Right included): C2/C9CD-C9E0.
static void mExchRedrive(uint8_t* m) {
  if (m[0x5A] & 0x03) {                                            // 2C9AE-2C9B2
    if (m[0x54] < 3) { if (!m[0x0F0B]) { m[0x54] = (uint8_t)(m[0x54] + 3); return; } }   // 2C9B4-2C9CA
    else             { if (!m[0x0F0C]) { m[0x54] = (uint8_t)(m[0x54] - 3); return; } }   // 2C9C3-2C9CA
  }
  int c = m[0x54] & 0x0F; if (c > 6) c = 6;                        // 2C9CD JSR C2/EA27: X = cell * 6
  uint8_t v = (m[0x5A] & 0x08) ? m[0x9382 + c * 6] : m[0x9383 + c * 6];   // 2C9D2-2C9E0
  m[0x54] = (uint8_t)(v & 0x0F);
}
// C2/E9E2 + C2/C805, one call per input frame: `dir` is the pad latch's auto-repeat byte $F5 (bit 0 R, 1 L, 2 D, 3 U).
// E9E2: $81 = $54; the first set bit picks the neighbour (R, L, D, U in that order: 2E9F4's LSR loop); a neighbour with
// bit 7 sets $5A bit 7 (scroll) and lands on its low bits. C805: the reserve scroll C2/C85B (only with five reserves or
// more), then the validity loop -- C2/C819: `$0F0B | $0F0C` both 0 -> $54 = 0 and the entry is 40; else the entry under
// the cursor ($0F02 + $54, + the scroll $80 on the reserve column) re-drives (C2/C9AE) while it is locked or empty --
// and finally $0F0D = the entry, $7F = $54, $56 = 1 on the reserve column. Returns 1 when the cursor moved ($81 != $54:
// the blip C2/EAC2 of C2/C7EA / C2/C6C6).
int CtMenuExchMove(uint8_t* m, int dir) {
  if (!m) return 0;
  m[0x81] = m[0x54];                                               // 2E9E5
  m[0x0D1E] = 0x80;                                                // 2E9E9
  m[0x5A] = (uint8_t)(dir & 0x0F);                                 // 2E9F0 LDA $F5 / STA $5A
  int x = 0;
  while (x < 4 && !((dir >> x) & 1)) x++;                          // 2E9F4-2E9FA
  if (x < 4 && !m[0x0D1F]) {                                       // 2E9FE-2EA04
    m[0x0D1E] = (uint8_t)x;
    int c = m[0x54] & 0x0F; if (c > 6) c = 6;
    m[0x55] = m[0x54];                                             // 2EA0C
    static const int off[4] = { 5, 4, 3, 2 };                      // 2EA1F: EA45 R, EA40 L, EA3B D, EA36 U
    uint8_t v = m[0x9380 + c * 6 + off[x]];
    m[0x54] = v;                                                   // 2EA48
    if (v & 0x80) { m[0x5A] |= 0x80; m[0x54] &= 0x7F; }            // 2EA4A-2EA50
  }
  m[0x0D1F] = 0;                                                   // 2EA1A
  CtMenuExchValidate(m);
  return m[0x81] != m[0x54];
}
// C2/C805 alone: what the screen's build (C2/CAF3) runs on the entry cell, with no move before it.
void CtMenuExchValidate(uint8_t* m) {
  if (!m) return;
  m[0x0D9E] = 0;                                                   // 2C808
  if (m[0x0F0C] >= 5 && (m[0x5A] & 0x80)) {                        // 2C80B-2C816: C2/C85B
    if (m[0x5A] & 0x08) {                                          //   Up off the top of the reserve column
      m[0x54] = 3;
      if (m[0x80]) { m[0x80]--; m[0x0D9E] = 0xD0; }                //   (C2/C8BA's redraw is the picture's)
    } else {                                                       //   Down off its bottom
      int lim = m[0x0F0C] - 3;
      m[0x54] = 6;
      if (m[0x80] + 1 < lim) { m[0x80]++; m[0x0D9E] = 0x30; }
    }
  }
  uint8_t e;
  for (int guard = 0; ; guard++) {                                 // 2C819-2C83B
    if (!(m[0x0F0B] | m[0x0F0C])) { m[0x54] = 0; e = 0x40; break; }
    int k = m[0x54] < 3 ? m[0x54] : m[0x54] + m[0x80];
    e = (k < 9) ? m[0x0F02 + k] : 0x80;
    if (!(e & 0xC0) || guard > 16) break;
    mExchRedrive(m);
  }
  m[0x0F0D] = e;                                                   // 2C83D
  m[0x0D13] &= (uint8_t)~0x04;                                     // 2C843 TRB $0D13
  m[0x56] = 0; m[0x7F] = m[0x54];                                  // 2C848-2C84C
  if (m[0x54] >= 3) m[0x56] = 1;                                   // 2C84E-2C852
}
// C2/C674 (screen 7's A, C2/C7F7: refused while $0F0D carries the lock bit): the first pick. $0DA8 = the scroll, $54 =
// $0D9C = $7F, $0F00 = $0F0D. Returns 0 when refused (no blip: C2/C7FA's BVS goes straight to the RTS).
int CtMenuExchPick(uint8_t* m) {
  if (!m || (m[0x0F0D] & 0x40)) return 0;                          // 2C7F7 BIT $0F0D / BVS
  m[0x0DA8] = m[0x80];                                             // 2C677
  m[0x54] = m[0x7F]; m[0x0D9C] = m[0x7F];                          // 2C67C-2C680
  m[0x0F00] = m[0x0F0D];                                           // 2C6A8-2C6AB
  return 1;
}
// C2/C6D0 + C2/C6DE-C70A (the second-pick substate, C2/C6B9, shared with the Form screen): A on a selectable entry
// stores it in $0F01 and runs the swap C2/C70C over $2980..$2988 (then C2/816A, C2/CD2B -- the roster rebuilt from the
// new party -- and the panels' redraw); A on a locked one, or B, cancels. Either way the picks go back to FF and the
// cursor to $7F. Returns 1 when the swap ran.
int CtMenuExchSecond(uint8_t* m, const uint8_t* glb, int a) {
  if (!m) return 0;
  int swapped = 0;
  if (a && !(m[0x0F0D] & 0x40)) {                                  // 2C6D0 BIT $0F0D / BVS ->2C6DE
    m[0x0F01] = m[0x0F0D];                                         // 2C6D5
    int i = m[0x0F00] & 0x3F, j = m[0x0F01] & 0x3F;
    CtMenuFormSwap(m, i, j);                                       // 2C70F-2C722
    CtMenuRosterLists(m);                                          // 2C725 JSR C2/816A
    CtMenuExchRoster(m, glb);                                      // 2C77C JSR C2/CD2B
    swapped = 1;
  }
  m[0x54] = m[0x7F];                                               // 2C6E1
  m[0x0F00] = 0xFF; m[0x0F01] = 0xFF;                              // 2C6EA-2C6EF
  return swapped;
}

// Screen $07's picture, from the menu's WRAM:
//   BG2  the captured chrome (ctMenuChromeExchTM: the two columns and the prompt box), the window ramp of list 0 (one
//        192-line ramp from line 17, 12 lines a step, as the status screen's), and C2/CC4F's dim re-applied over the
//        three active panels' rows (2 + 6r .. 7 + 6r, tile columns 2-15; C2/CC4F walks $0F02..$0F04 only, so the
//        reserve column never dims);
//   BG1  each panel at C2/C784's (column, row): column 3 for the actives, 0x13 for the reserves, row (index & 7) * 6 + 2,
//        the Form's rows. A first pick's C2/C688-C6A5 does not blank the reserve panel: C2/EF05 is a rectangle copy
//        (`MVN` row by row from the panel at (0x13, row) to (2, 0x20), off screen), and the panel stays;
//   OBJ  the members at C2/CDE3's places, C2/CE6E[$02]: the actives from index 0 (x 116, feet 56 + 48r), the reserves
//        from 6 (x 140) for the window's four rows from the scroll $80; the hand at the graph's cell $54 ($7E:9380), and
//        the picked cell's second hand ($0D9C, +2 / -1) on the blink's visible half, as on the Form screen;
//   BG3  the wave (CtMenuExchWaveInit / Tick); BG1 the prompt's text (CtMenuExchPrompt); OBJ the prompt's figure (block
//        7, CtMenuExchFigure). tools/menu_exchange_pic_cmp.py compares the picture with the oracle's.
void CtMenuRenderExchange(const CtMenuHost* h, uint8_t* fb, int pickBlink) {
  if (!h || !fb || !h->mem) return;
  const uint8_t* m = h->mem;
  { static uint16_t scratch[CT_MENU_CHROME_TMW * CT_MENU_CHROME_TMH];
    memcpy(scratch, ctMenuChromeExchTM, sizeof scratch);
    for (int r = 0; r < 3; r++) {
      int dim = (m[0x0F02 + r] & 0xC0) != 0;                          // 2CC7B AND #00C0
      for (int ty = 2 + 6 * r; ty < 8 + 6 * r; ty++)
        for (int tx = 2; tx <= 15; tx++) {
          uint16_t* e = &scratch[ty * CT_MENU_CHROME_TMW + tx];
          int t = *e & 0x3FF;
          if (!((t >= 0x3DC && t <= 0x3EF) || (t >= 0x300 && t <= 0x313))) continue;
          *e = (uint16_t)((*e & ~0x1C00) | (dim ? (4 << 10) : 0));
        }
    }
    static uint8_t zero[MW * MH];
    memset(zero, 1, sizeof zero); mChromeZero = zero;            // (a cell BG2 does not draw is transparent too)
    mChromeTMPal2L(fb, scratch, 0, ctMenuChromePal, mWinGrad(0), 0);
    mChromeZero = NULL;
    // BG3, the wave (CtMenuExchWaveInit / Tick), where BG2's windows leave colour 0. Its map $7E:4E00 (32 x 32, VRAM
    // $6800), its 2bpp characters $7E:B000 (VRAM $7000), CGRAM from $7E:9480; line y at VOFS $0D0E + 1 and HOFS
    // $9240[y & 15] (the HDMA). (TMW = 04 masks BG3 by window 2 alone, which is empty here, 255..0; window 1, 16..239,
    // is the colour math's.)
    if (m[0x0D0E] | m[0x0D0F]) {
      int vofs = (m[0x0D0E] | (m[0x0D0F] << 8)) & 0x3FF;
      uint8_t tbl[32]; mExchWaveTable(m, h->rom, h->romLen, tbl);    // (the HDMA's table: the next main-thread step's)
      for (int y = 0; y < MH; y++) {
        int hofs = (tbl[2 * (y & 15)] | (tbl[2 * (y & 15) + 1] << 8)) & 0x3FF;
        int by = (y + 1 + vofs) & 0xFF;                               // (the PPU fetches line y + 1 of the BG)
        for (int x = 0; x < MW; x++) {
          if (!zero[y * MW + x]) continue;
          uint8_t* d = fb + (y * MW + x) * 3;
          d[0] = d[1] = d[2] = 0;                                       // the backdrop (CGRAM 0) unless BG3 has a colour here
          int bx = (x + hofs) & 0xFF;
          int e = m[0x4E00 + ((by >> 3) * 32 + (bx >> 3)) * 2] | (m[0x4E01 + ((by >> 3) * 32 + (bx >> 3)) * 2] << 8);
          int t = e & 0x3FF, pl = (e >> 10) & 7, fx = bx & 7, fy = by & 7;
          if (e & 0x4000) fx = 7 - fx;
          if (e & 0x8000) fy = 7 - fy;
          if (t * 16 + 16 > 0x5000) continue;
          const uint8_t* g = m + 0xB000 + t * 16 + fy * 2;
          int ci = ((g[0] >> (7 - fx)) & 1) | (((g[1] >> (7 - fx)) & 1) << 1);
          if (!ci) continue;
          int cw = m[0x9480 + (pl * 4 + ci) * 2] | (m[0x9481 + (pl * 4 + ci) * 2] << 8);
          int r5 = cw & 31, g5 = (cw >> 5) & 31, b5 = (cw >> 10) & 31;
          d[0] = (uint8_t)((r5 << 3) | (r5 >> 2)); d[1] = (uint8_t)((g5 << 3) | (g5 >> 2)); d[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
      }
    } }
  // BG1, the prompt's text (CtMenuExchPrompt): the map buffer's cells (6..13, 21..24) whose tile is the strip's
  // ($0100-$013F, $7E:5E00 + 32 per tile, 4bpp), in BG1's palette (CGRAM 0 on: the menu's palettes).
  for (int r = 21; r <= 24; r++)
    for (int c = 6; c <= 13; c++) {                                   // (the cells C2/FBB4 lays; the init cleared the rest)
      int e = m[0x2E00 + (r * 32 + c) * 2] | (m[0x2E01 + (r * 32 + c) * 2] << 8), t = e & 0x3FF, pl = (e >> 10) & 7;
      if (t < 0x100 || t >= 0x140) continue;
      const uint8_t* g = m + 0x5E00 + (t - 0x100) * 32;
      for (int yy = 0; yy < 8; yy++)
        for (int xx = 0; xx < 8; xx++) {
          int fy = (e & 0x8000) ? 7 - yy : yy, fx = (e & 0x4000) ? xx : 7 - xx;
          int ci = ((g[fy * 2] >> fx) & 1) | (((g[fy * 2 + 1] >> fx) & 1) << 1) | (((g[16 + fy * 2] >> fx) & 1) << 2) |
                   (((g[17 + fy * 2] >> fx) & 1) << 3);
          if (!ci) continue;
          uint8_t* d = fb + ((r * 8 + yy) * MW + c * 8 + xx) * 3;
          memcpy(d, ctMenuChromePal[pl][ci], 3);
        }
    }
  if (mExchFigOn && h->anySprFrame) {                                  // OBJ, priority 2: over BG1 and BG2 (CtMenuExchFigure)
    int cx = 0, cy = 0; const uint8_t* b = h->anySprFrame(MEXCH_FIG_SPR, 0, &cx, &cy);
    if (b) {
      int bw = b[0], bh = b[1], x0 = 32 - CT_SPR_OX + cx, y0 = 202 - CT_SPR_OY + cy;
      for (int yy = 0; yy < bh; yy++)
        for (int xx = 0; xx < bw; xx++) {
          const uint8_t* s = b + 2 + ((size_t)yy * bw + xx) * 4;
          int px = x0 + xx, py = y0 + yy;
          if (!s[3] || px < 0 || px >= MW || py < 0 || py >= MH) continue;
          memcpy(fb + (py * MW + px) * 3, s, 3);
        }
    }
  }
  for (int r = 0; r < 3; r++) {                                       // the actives, C2/C784 column 3
    uint8_t e = m[0x0F02 + r];
    if (e & 0x80) continue;
    CtMenuPanel p;
    if (!CtMenuPanelData(h->mem, e & 0x3F, &p)) continue;
    int y = 8 + r * MP_PITCH, dim = (e & 0x40) ? 1 : 0;
    mCharPanel(h, fb, MP_X, y, &p, 3 * 8 - MP_X, dim);
    mPanelAtkDefOne(h, fb, 3 * 8 - MP_X, y + 40, p.charId, dim);
    mBlitPartySpr(h, fb, p.charId, 116, 56 + r * MP_PITCH, dim);    // C2/CE6E[0 + 2r]
  }
  for (int k = 0; k < 4; k++) {                                       // the reserves, column 0x13, from the scroll
    int ix = m[0x80] + k;
    if (ix >= 6) break;
    uint8_t e = m[0x0F05 + ix];
    if (e & 0x80) continue;
    CtMenuPanel p;
    if (!CtMenuPanelData(h->mem, e & 0x3F, &p)) continue;
    int y = 8 + k * MP_PITCH;
    mCharPanel(h, fb, MP_X, y, &p, 0x13 * 8 - MP_X, 0);
    mPanelAtkDefOne(h, fb, 0x13 * 8 - MP_X, y + 40, p.charId, 0);
    mBlitPartySpr(h, fb, p.charId, 140, 56 + k * MP_PITCH, 0);      // C2/CE6E[6 + 2k]
  }
  // The hand: the graph's (x, y), its attribute $58-$59 with bit 6 (H flip) set when C2/C805 left $56 != 0 -- the
  // reserve column (C2/E964-E977 `LDA $56 / BEQ / LDA #40 / TSB $0773`): it points left there. The picked cell's second
  // hand takes the word the first had at the pick (C2/8A31 `LDA $0772 / STA $0DA1`, C2/8A8B `LDX $0DA1 / STX $0776`):
  // flipped when the pick was on the reserve column.
  int c = m[0x54] & 0x0F; if (c > 6) c = 6;
  mHandFlip = m[0x56] != 0;
  mHandCursor(h, fb, m[0x9380 + c * 6], m[0x9381 + c * 6]);
  if (m[0x0F00] != 0xFF && pickBlink) {
    int pc = m[0x0D9C] & 0x0F; if (pc > 6) pc = 6;
    mHandFlip = pc >= 3;
    mHandCursor(h, fb, m[0x9380 + pc * 6] + 2, m[0x9381 + pc * 6] - 1);
  }
  mHandFlip = 0;
}

void CtMenuRenderForm(const CtMenuHost* h, uint8_t* fb, int cursor, int picked, const uint8_t* panel9,
                      int pickBlink) {
  if (!h || !fb) return;
  // the background (the BG2 chrome) is laid by the caller (CtMenuChrome)
  // The entered-Form BG1 tilemap equals the ribbon's opt-4 hover, and the BG2 map differs only in rows 12-17 x cols
  // 8-23, where the ribbon icon box was: the Form frame is the opt-4 frame minus the box.
  // The panel text starts at x = 72 (tile col 9) on every panel (inset 56); the hand (x 56..71, the $7E:9380 nav
  // column) sits left of it.
  // The dim is per panel: C2/CC4F loops $0F02,X for X = 0..2 and acts only where `AND #00C0` is non-zero, locked
  // (bit 6) or empty (bit 7). The panel array is compacted (C2/CD2B), so row r shows roster slot panel9[r] & 0x3F.
  { uint8_t local[9]; const uint8_t* pan = panel9;
    if (!pan) { CtMenuFormPanels(h->mem, h->glb, local); pan = local; }  // ribbon hover: build it here, with the
                                                                         //   locks (C2/CA3F runs C2/CC4F too)
    for (int r = 0; r < 3; r++) {
      if (pan[r] & 0x80) continue;                                        // empty panel: nothing drawn
      CtMenuPanel p;
      if (!CtMenuPanelData(h->mem, pan[r] & 0x3F, &p)) continue;
      int y = 8 + r * MP_PITCH, dim = (pan[r] & 0x40) ? 1 : 0;            // C2/CC4F: locked -> palette 4
      int tx; CtMenuFormGeom(r, &tx, NULL, NULL);
      mCharPanel(h, fb, MP_X, y, &p, tx - MP_X, dim);
      mPanelAtkDefOne(h, fb, tx - MP_X, y + 40, p.charId, dim);           // the 4th row (tile row 6n+6)
    }
    mFormSprites(h, fb, pan); }                      // the standing sprites, at C2/CE6E's own anchor
  // The Exchange icon is the highlight variant of the ribbon's swap-arrows icon (tile base + 0x20), the coloured one.
  // It is a BG widget, at tile column 11 (MI_HX + 80), flush against the text at column 13.
  mBlitIcon(fb, ctMenuIconSel[4], MI_HX + 80, 176);
  mText(h, fb, "Exchange", MI_HX + 96, 184);
  // The picked member gets a second hand as well as its sprite pose (anim 0x0B, C2/C973): OAM slot 29 at (+2, -1) from
  // the nav hand, its staging word $7E:0774 alternating with the parked 00E0 every four frames in step with $7E:0DA0
  // (the counter the Item screen's pinned hand uses). Visible on the $0DA0 == 0 half (C2:8A8B places it, C2:E942 parks
  // it).
  if (picked >= 0 && picked < 3 && pickBlink)
    mHandCursor(h, fb, 56 + 2, 35 + picked * 48 - 1);
  // The anchor is the nav record: the three party cells of $7E:9380 are (0x38, 0x23), (0x38, 0x53), (0x38, 0x83) =
  // (56, 35 + 48*r), the x from C2/CB18 (`LDA #38 / STA $9380 / STA $9386 / STA $938C`). The table is built at run
  // time (the four reserve cells at x = 0xE8 carry roster-dependent neighbours), so the port does not bake it.
  if (cursor >= 0 && cursor < 3) mHandCursor(h, fb, 56, 35 + cursor * 48);
}

// The bottom-bar VWF run, defined with the Config screen below (both bars go through C2/F943's renderer); the Tech bar
// is the first caller in file order.
static void mBarVwf(const CtMenuHost* h, uint8_t* fb, const char* s, int x, int y,
                    const uint8_t tone[4][3]);
// ---------------------------------------------------------- Screen 3 = Tech (menu2_tech_screen.md)
// One list machine over a 37-entry tech-group space. C2/BD40 is a 38-byte cumulative id boundary: groups 0-6 = the
// seven characters' 8 single techs (ids $01..$38), 7-21 = the fifteen character pairs with 3 double techs each
// ($39..$65), 22-36 = the fifteen triples with 1 each ($66..$74). The tabs "1 2 3" are Single/Double/Triple, and
// C2/BD65 is the (firstGroup, count) window each one selects. Names come from $CC/15C4, single costs from $CC/253B.

// FF/F863 + FF/F876 + C2/B979: the menu's tech bitfield $7E:7F00[37], built at the open from the persistent $7E:2837.
// A group is copied only when every member is in the roster (the ROM leaves the others stale; the port writes 0), and
// then one not-yet-learned tech per character is added so the screen can show it dim: nextIdx7[c] carries that
// tech's id.
void CtMenuTechBits(const uint8_t* mem, uint8_t* bits37, uint8_t* nextIdx7) {
  if (!bits37) return;
  for (int g = 0; g < 37; g++) bits37[g] = 0;
  for (int k = 0; k < 7; k++) if (nextIdx7) nextIdx7[k] = 0xFF;
  if (!mem) return;
  int absent = 0xFE;                                   // FF/F865: bit per charId, set = not in the roster
  for (int slot = 0; slot < 9; slot++) {
    int cch = mem[0x2980 + slot];
    if (!(cch & 0x80)) absent &= ~(0x80 >> (cch & 7));
  }
  for (int g = 36; g >= 0; g--) {
    if (ctTechGroupMask[g] & absent) continue;         // a member is missing -> the ROM skips the copy
    bits37[g] = mem[0x2837 + g];
  }
  for (int cch = 0; cch < 7; cch++) {                  // FF/F876: the "next to learn", drawn dim
    int n = 8, a = bits37[cch];
    for (;;) { int b = a & 1; a >>= 1; if (b) break; if (--n == 0) break; }
    if (n < ctTechNextCap[cch] && n < 8) {
      bits37[cch] = (uint8_t)(bits37[cch] | (0x80 >> n));
      if (nextIdx7) nextIdx7[cch] = (uint8_t)(ctTechBound[cch] + n);   // the tech id that is only previewed
    }
  }
  for (int k = 0; k < 5; k++) bits37[32 + k] = mem[0x2857 + k];        // C2/B979's MVN (the rock triples)
}

// CC/3A09 + id*2: the tech's description, decoded at bake time. Returns the length written; ids 0x76..0x78 are the
// Single/Double/Triple tab names, which the bar shows when no row is highlighted.
int CtMenuTechDesc(int techId, char* out, int cap) {
  if (out && cap > 0) out[0] = 0;
  if (!out || cap < 2 || techId < 0 || techId >= (int)(sizeof ctTechDesc / sizeof ctTechDesc[0])) return 0;
  int n = 0;
  for (const char* s = ctTechDesc[techId]; *s && n < cap - 1; s++) out[n++] = *s;
  out[n] = 0;
  return n;
}

// C2/BC22 + C2/BC59: what this tech costs this character. A single indexes CC/253B by the tech id; a double/triple
// indexes it through $7E:1607 + charId*64 + techId, so the same tech shows a different number in each participant's
// panel. The accessory halves it: $A2 once, $A3 twice, both rounding up.
int CtMenuTechMp(int charId, int techId, int tab, int accessoryId) {
  int ix = techId;
  if (tab != 0) {
    int o = (charId & 7) * 64 + techId - 0x39;
    ix = (o >= 0 && o < (int)sizeof ctTechMpIdx) ? ctTechMpIdx[o] : 0;
  }
  int mp = ctTechMpCost[ix & 0xFF];
  if (accessoryId == 0xA3) mp = (mp + 1) >> 1;
  if (accessoryId == 0xA2 || accessoryId == 0xA3) mp = (mp + 1) >> 1;
  return mp;
}

int CtMenuTechList(const CtMenuHost* h, int charId, int tab, int* techId, int* mp, int* group,
                   int* dim, int cap) {
  if (!h || !h->mem || cap <= 0) return 0;
  if (tab < 0) tab = 0; else if (tab > 2) tab = 2;
  uint8_t bits[37], nxt[7];
  CtMenuTechBits(h->mem, bits, nxt);
  int acc = h->mem[0x2600 + (size_t)(charId & 7) * 0x50 + 0x2A];
  int first = ctTechTabWin[tab * 2], cnt = ctTechTabWin[tab * 2 + 1];
  int mask = 0x80 >> (charId & 7), n = 0;
  for (int g = first; g < first + cnt && g < 37; g++) {               // C2/BCCD: only the groups this
    if (!(ctTechGroupMask[g] & mask)) continue;                       //   character belongs to
    int b = bits[g];
    // C2/BD00's do-while: `id != BD40[g+1]` is what makes group 36 emit exactly one id (BD40[37] = 0).
    for (int id = ctTechBound[g]; ; ) {
      if (b & 0x80) {
        if (n < cap) {
          techId[n] = id;
          if (group) group[n] = g;
          if (mp)    mp[n] = CtMenuTechMp(charId, id, tab, acc);
          if (dim)   dim[n] = (nxt[charId & 7] == id) ? 1 : 0;        // the previewed, unlearned one
          n++;
        }
      }
      b = (b << 1) & 0xFF;
      id++;
      if (id == ctTechBound[g + 1]) break;
    }
  }
  return n;
}

// The hand's nav cell when it is not the list row's: C2/B471 moves $54 to cell 8 on the list's A, and that frame's
// picture is still the list (C2/BE79 has only yielded) with the hand already on the target cell. -1 = the list row.
static int g_techHandCell = -1;
void CtMenuTechHandCell(int cell) { g_techHandCell = cell; }
static int g_techListRan = 1;                  // the list's handler has run (C2/BDEB is its call: none on the A's frame)
void CtMenuTechListRan(int ran) { g_techListRan = ran; }
void CtMenuRenderTech(const CtMenuHost* h, uint8_t* fb, int charId, int tab, int row, int scroll,
                      int selSlot, int fresh, int barId, int barX, int barN) {
  if (!h || !fb) return;
  // the background (the BG2 chrome) is laid by the caller (CtMenuChrome)
  // The party panels, the standing sprites and the Single/Dual/Triple tabs (top right, where there is no portrait).
  if (tab < 0) tab = 0; else if (tab > 2) tab = 2;
  int tids[64], mps[64], grp[64], dim[64];
  int n = CtMenuTechList(h, charId, tab, tids, mps, grp, dim, 64);
  // In the list the left column is the participant list, not the party: a Dual list draws the two set bits of
  // CC/2963[group], each with its own "MP Used" (C2/BC22's per-character $7E:1607 index), and leaves the other panels
  // blank; a Single list draws the one participant. Outside the list (selSlot >= 0) it is the party.
  int part[3], np = 0;
  if (row >= 0 && row < n) {
    part[np++] = charId & 7;                                    // C2/BBD6: panel 0 = the selected char
    int gm = ctTechGroupMask[grp[row] & 0x3F];
    for (int k = 0; k < 7 && np < 3; k++)
      if ((gm & (0x80 >> k)) && k != (charId & 7)) part[np++] = k;
  }
  if (np) {
    for (int i = 0; i < np; i++) {
      CtMenuPanel pp; int slot = -1;
      for (int s2 = 0; s2 < 9; s2++) if (h->mem && (h->mem[0x2980 + s2] & 7) == part[i]   // (a reserve caster's
                                        && !(h->mem[0x2980 + s2] & 0x80)) { slot = s2; break; }  //  slot too)
      if (slot < 0 || !CtMenuPanelData(h->mem, slot, &pp)) continue;
      mCharPanel(h, fb, MP_X, 8 + i * MP_PITCH, &pp, 24, 0);    // every participant draws bright
      mTechMpUsedOne(h, fb, i, mps[row < n ? row : 0] >= 0
                     ? CtMenuTechMp(part[i], tids[row], tab,
                                    h->mem[0x2600 + (size_t)part[i] * 0x50 + 0x2A]) : -1, pp.mp);
      // Panel 0's sprite is block 7, the caster's copy (C2/BA09: $19CE = 381C = feet 0x38, centre 0x1C). A Dual /
      // Triple partner's is its own block: C2/BBA7's loop runs C2/BAFC for participant i -- `STA $1800,Y` (the char),
      // `STZ $1818,Y` (+0x18 = 0: C2/F657 emits its OAM), +0x0F = 1C, +0x0E = C2:BB1A[i] (38 / 68 / 98) -- while the
      // caster's own block and the other members' stay at +0x18 = 40 (not drawn).
      if (h->rom && h->romLen > 0x2BB1A + 3) {
        if (i == 0 && CtMenuSprChar7() == part[0])
          mBlitPartySprPose(h, fb, part[0], CtMenuSprShown7(), 0x1C, CtRomByte(h->rom, 0x400000L, (long)(0x2BB1A)), 0);
        else if (i > 0)
          mBlitPartySprPose(h, fb, part[i], CtMenuSprShown(part[i]), 0x1C, CtRomByte(h->rom, 0x400000L, (long)(0x2BB1A + i)), 0);
      }
    }
    // C2/BDEB (every frame of the list, C2/B724): on the Single tab, with the highlighted tech the not-learned one
    // ($7700 bit 6, the list's dim entry), C2/ED31 FF:C026 prints string #$21 at (5,9): "Skill Pts", a line down "Need "
    // and the word at the caster's sheet + $2D (C2/F011 -> C2/F114 fmt $42: five cells, right-aligned), the TP still
    // needed.
    if (tab == 0 && row >= 0 && row < n && dim[row] && h->mem && g_techListRan) {
      const uint8_t* sh = h->mem + 0x2600 + (size_t)(charId & 7) * 0x50;
      mText(h, fb, "Skill Pts", 40, 72);
      mText(h, fb, "Need", 40, 88);
      mNumR(h, fb, sh[0x2D] | (sh[0x2E] << 8), 120, 88);
    }
  } else {
    mPartyPanels(h, fb, 24, -1);                     // char select: the Tech screen dims no panel
    mPartySprites(h, fb);
    mTechMpUsed(h, fb, -1, -1);                      // the "MP Used" label, no value
  }
  mTechTabs(fb, tab);                                // $7E:0418
  // The list is eight rows (C2/BD73 `LDA #0008`) at x144, y = 60 + 12*row, scrolled by $7E:0F4A; a row past the end
  // draws blank (C2/BDA6). The next-to-learn tech is present but dim (C2/BDC0's $0C). Names only: the cost is in the
  // panels' "MP Used".
  for (int i = 0; i < 8 && !fresh; i++) {         // (the build frame, C2/B979, has no list yet: C2/BC6F draws it
    int k = scroll + i;                              //  on substate 2's first frame, the $84 = FF it left)
    if (k >= n) break;
    char nm[16] = "";
    if (!CtMenuTechName(h->rom, h->romLen, tids[k], nm, sizeof nm))
      snprintf(nm, sizeof nm, "Tech %d", tids[k]);   // ROM absent: show the tech id
    // C2/BDB2 picks the row's text attribute from the per-tech flag byte $7E7700 + techId and hands it to the C2/EF65
    // blitter in DP $7E:
    //     2BDC0 LDX #0C / LDA $7700,Y / BIT #40 / BNE ->store      ; bit6 = the next-to-learn tech
    //     2BDC9 LDX #00 / BIT #80 / BEQ ->store                    ; bit7 = usable outside battle
    //     2BDCF BIT $78 / BMI ->store / LDX #08
    // 0x0C / 0x08 / 0x00 are BG tilemap palette bits (attr bits 2-3), three brightnesses. Bit 6 is set by FF/F8AE (`LDA
    // $7701,X / AND #1E / ORA #40`) on the tech CtMenuTechBits computes as the preview (FF/F876), so `dim[k]` is that
    // gate; it draws in drawGlyphDim2's tones. Attr 0x08: the nine techs FF/F830-F84B marks usable outside battle (ids
    // 09 0C 0F 1A 1D 21 24 27 29) draw the third, bright way while DP $78 bit 7 is clear ($78 = FF only for a reserve
    // caster, which the X menu's three-member column never loads).
    mDim = dim[k] ? 2 : (h->mem && h->mem[0x0071] < h->mem[0x0073]                     // 2BDCF BIT $78 / BMI
                         && CtMenuTechFieldUsable(h->mem, charId, tids[k])) ? 3 : 0;
    mText(h, fb, nm, 144, 60 + i * 12);
    mDim = 0;
  }
  // the list cursor, when the list is entered, at its nav record: the screen's graph $FF:B7A5 (a length byte, 0x54 =
  // fourteen records, then [x, y, up, down, left, right]); cells 0-7 = the rows, (128, 59 + 12 i), a line above the
  // text's origin
  { int i = row - scroll;
    if (g_techHandCell >= 0) i = g_techHandCell;              // (C2/B471's frame: $54 is already the target cell)
    if (row >= 0 && i >= 0 && i < 14 && row < n && h->rom && h->romLen > 0x3FB7A6L + 14 * 6)
      mHandCursor(h, fb, CtRomByte(h->rom, 0x400000L, (long)(0x3FB7A6L + i * 6)), CtRomByte(h->rom, 0x400000L, (long)(0x3FB7A6L + i * 6 + 1))); }
  // Char select (selSlot >= 0): the hand rests on a party panel instead, and the list is a preview. There is no
  // right-hand detail column; the three tabs fill it.
  if (selSlot >= 0 && selSlot < 3) mHandCursor(h, fb, 8, 8 + selSlot * MP_PITCH + 27);
  // The description bar (C2/BE16 / C2/BE33): the highlighted tech's string, text top y180; 0x76..0x78 are the tab
  // names, which the bar shows with no row highlighted. Both paths render through the VWF, staging the run into the
  // buffer at $7E:5340 and calling C2/FA49 -> JSL C2:0003:
  //     C2/BE33 (description): LDX #5344 / STX $0DC5 ; LDA #01 / STA $0DC9 ; LDA $0F4C
  //     C2/BE16 (tab name):    LDX #5356 / STX $0DC5 ; LDA #0A / STA $0DC9 ; LDA $0418 / ADC #76
  // $0DC5 is a byte offset into that buffer, two bytes per 8 px cell, and C2/FA81 starts the pen at $0DD2 = 8, so the
  // ink lands at x = ($0DC5 - ($0DC5 & FFC0)) * 4 + 8: the description at 24, the tab name at 96 (menu_phase2.md,
  // menu2_tech_screen.md). `barId` = $0D77 (a tech id from C2/BE33 at x 24, one character a step, or $76 + tab from
  // C2/BE16 at x 96, ten a step: $0DC9), `barN` = the characters typed so far.
  if (!fresh && barN > 0 && barId >= 0 && barId < (int)(sizeof ctTechDesc / sizeof ctTechDesc[0])) {
    char line[40]; int k = 0;
    for (const char* p = ctTechDesc[barId]; *p && k < barN && k < (int)sizeof line - 1; p++) line[k++] = *p;
    line[k] = 0;
    if (line[0]) mBarVwf(h, fb, line, barX, 180, ctLoaderVwfTone);
  }
}

// The tech target overlay (substate 4, C2/B48E), as C2/BE79 builds it: the caster's panel on the left (C2/BA2F at
// $2ECA, his "MP Used", his sprite in block 7 at C2/BAFC's (0x1C, 0x38)), and in the list's window the whole party --
// C2/BEDC: C2/A1EF's panel at $3662 + 0x100 k (name/LV, HP, MP: text rows 64/72/80 + 32 k), the member's sprite at
// C2:BF2F[k] (feet 58/78/98, centre E4). The hand is nav cell 8 + tgt of $FF:B7A5 ((120, 71 + 32 tgt)); the list rows
// are not drawn. `shown` = the HP/MP C2/BEDC last drew (the redraw after a cast is C2/BE79's, not the heal's frame);
// `casterMp` likewise the caster panel's MP. `castPose` >= 0 = block 7's fixed frame ($19D1 = 90, $19E1 =
// C2:B519[charId]); the popup = C2/9016's number at each member's anchor ($0F57 = the block's $180E = C2:BF2F[k])
// minus FF/CEBA[popCurve].
void CtMenuRenderTechTarget(const CtMenuHost* h, uint8_t* fb, int charId, int tab, int row, int tgt,
                            const uint16_t shown[3][2], int casterMp, int castPose,
                            int popCurve, int popPool, const int popAmt[3][2], int barId, int barX, int barN) {
  if (!h || !fb || !h->mem) return;
  if (tab < 0) tab = 0; else if (tab > 2) tab = 2;
  int tids[64], mps[64], grp[64], dim[64];
  int n = CtMenuTechList(h, charId, tab, tids, mps, grp, dim, 64);
  int slot = -1;
  for (int s2 = 0; s2 < 3; s2++) if ((h->mem[0x2980 + s2] & 7) == (charId & 7) && !(h->mem[0x2980 + s2] & 0x80)) slot = s2;
  CtMenuPanel pp;
  if (slot >= 0 && CtMenuPanelData(h->mem, slot, &pp)) {          // C2/BA2F: the caster, panel 0
    if (casterMp >= 0) pp.mp = casterMp;
    mCharPanel(h, fb, MP_X, 8, &pp, 24, 0);
    mTechMpUsedOne(h, fb, 0, (row >= 0 && row < n)
                   ? CtMenuTechMp(charId & 7, tids[row], tab, h->mem[0x2600 + (size_t)(charId & 7) * 0x50 + 0x2A]) : -1,
                   pp.mp);
  }
  if (CtMenuSprChar7() == (charId & 7) && h->rom && h->romLen > 0x2BB1A + 3)   // block 7 at $19CE (0x1C, 0x38): its
    mBlitPartySprPose(h, fb, charId & 7, CtMenuSprShown7(), 0x1C, CtRomByte(h->rom, 0x400000L, (long)(0x2BB1A)), 0);   //   own animation (the cast's)
  (void)castPose;
  mTechTabs(fb, tab);
  for (int k = 0; k < 3; k++) {                                    // C2/BEDC: the party in the list's window
    CtMenuPanel p;
    if (!CtMenuPanelData(h->mem, k, &p)) continue;
    if (shown) { p.hp = shown[k][0]; p.mp = shown[k][1]; }
    mCharPanel(h, fb, MP_X + 112, 48 + 32 * k, &p, 8, 0);
    if (h->rom && h->romLen > 0x2BF2F + 6) {
      int w = CtRomByte(h->rom, 0x400000L, (long)(0x2BF2F + 2 * k)) | (CtRomByte(h->rom, 0x400000L, (long)(0x2BF30 + 2 * k)) << 8);
      mBlitPartySpr(h, fb, p.charId, (w >> 8) & 0xFF, w & 0xFF, 0);
    }
  }
  if (tgt >= 0 && tgt < 3 && h->rom && h->romLen > 0x3FB7A6L + 14 * 6)
    mHandCursor(h, fb, CtRomByte(h->rom, 0x400000L, (long)(0x3FB7A6L + (8 + tgt) * 6)), CtRomByte(h->rom, 0x400000L, (long)(0x3FB7A6L + (8 + tgt) * 6 + 1)));
  if (popCurve >= 0 && popAmt && h->rom && h->romLen > 0x3FCEBA + 0x60) {   // C2/9016 at each anchor
    int dy = CtRomByte(h->rom, 0x400000L, (long)(0x3FCEBA + popCurve));
    for (int k = 0; k < 3; k++) {
      int amt = popAmt[k][popPool == 2 ? 1 : 0];
      if (amt < 0) continue;
      int w = CtRomByte(h->rom, 0x400000L, (long)(0x2BF2F + 2 * k)) | (CtRomByte(h->rom, 0x400000L, (long)(0x2BF30 + 2 * k)) << 8);
      mItemPopDigits(h, fb, amt, popPool == 2 ? 1 : 3, (w >> 8) & 0xFF, (w & 0xFF) - dy, 0x3FCB9CL);   // C2/8F55's
    }
  }
  if (barN > 0 && barId >= 0 && barId < (int)(sizeof ctTechDesc / sizeof ctTechDesc[0])) {   // the bar, as the list's
    char line[40]; int k = 0;
    for (const char* p = ctTechDesc[barId]; *p && k < barN && k < (int)sizeof line - 1; p++) line[k++] = *p;
    line[k] = 0;
    if (line[0]) mBarVwf(h, fb, line, barX, 180, ctLoaderVwfTone);
  }
}

// ---------------------------------------------------------- Screen 4 = Config (menu2_config_toggles.md)
// C2/BF35 draws a grid of icon toggles and a bottom bar with the highlighted setting's name and value, computed by
// C2/C4BC:
//   name string    = masterTable[cursor]                       (the first 12 entries are the names)
//   value string   = masterTable[ $0D86[cellRam[cursor]] + cellBase[cursor] ]   (-> $0DCF)
// masterTable = the 16-bit LE pointer table at FF/D3FE (bank-FF pointers into FF/D462+), cellRam = C2/C4F9 (per-cell
// offset into the $0D86 config bytes), cellBase = C2/C505 (per-cell value-string base). The port draws the settings
// as a text list rather than the ROM's icon grid.
#define ROM_CFG_CELLRAM  0x02C4F9u   /* C2/C4F9, per-cell offset into the unpacked config bytes (12) */
#define ROM_CFG_CELLBASE 0x02C505u   /* C2/C505, per-cell value-string base index (12) */
#define ROM_CFG_STRTAB   0x03FD3FEu  /* FF/D3FE, master string pointer table (16-bit LE, bank-FF rel) */
#define ROM_CFG_STRBANK  0x3F0000u   /* bank FF file base: string @ STRBANK + tableEntry */
#define CFG_PACKED       0x2990      /* $7E2990-92, the persistent packed config (3 bytes, save data) */
#define CFG_WORK         0x0D86      /* $7E0D86-91, the 12 working bytes (C2/92F4 unpack; menu lifetime) */
#define CFG_MIRROR       0x0F09      /* $7E0F09-12, the checkbox mirror; +4/+5/+6 are the sub-mode flags */
#define CFG_BTN_LIVE     0x0408      /* $7E0408-10, the custom button map (9 bytes, save record 2) */
#define CFG_BTN_WORK     0x0F00      /* $7E0F00-08, the editor's working copy (C2/C11D MVN) */
#define CFG_BTN_MERGED   0x2993      /* $7E2993-9B, the live merged map the game reads (C2/C54F) */
#define ROM_CFG_NAV      0x3FB5A0u   /* FF/B5A0, the 21x6 cursor graph, copied to $7E:9380 */
#define ROM_CFG_DEFAULT  0x02FCA6u   /* C2/FCA6, 12 new-game bytes -> $2990..$299B (+3 = C2/FCA9 -> $0408) */
#define ROM_CFG_BTNCYCLE 0x02C207u   /* C2/C207, the assignable-button cycle order, FF-terminated (7) */
#define ROM_CFG_ROWIDX   0x02C2BDu  /* C2/C2BD, row -> map index (identity 00..07) */
#define ROM_CFG_CONFLICT 0x02C2C5u   /* C2/C2C5, per-row conflict-list pointer table (16-bit LE, bank C2) */
#define CFG_NAV_RAM      0x9380      /* $7E9380, where the screen builder puts the nav table */

// Unpack the packed config $7E2990-92 into the 12 working bytes as C2/92F4 does, into `out`: the fallback for a
// caller that renders before the menu has opened (the working copy at $0D86 is not set up then).
static void mCfgUnpack(const uint8_t* mem, uint8_t out[12]) {
  for (int i = 0; i < 12; i++) out[i] = 0;
  if (!mem) return;
  uint8_t a = mem[CFG_PACKED], b = mem[CFG_PACKED + 1], c = mem[CFG_PACKED + 2];
  out[0x00] = (a >> 3) & 1;      // $0D86 = $2990 bit3          (ROL chain C2/9308-931C)
  out[0x01] = (a >> 4) & 1;      // $0D87 = $2990 bit4
  out[0x02] = (a >> 5) & 1;      // $0D88 = $2990 bit5
  out[0x03] = (a >> 6) & 1;      // $0D89 = $2990 bit6
  out[0x04] = c & 3;             // $0D8A = $2992 & 3           (C2/9345)
  out[0x05] = a & 7;             // $0D8B = $2990 & 7           (C2/931F)
  out[0x06] = b & 7;             // $0D8C = $2991 & 7           (C2/9327)
  out[0x07] = (b >> 6) & 1;      // $0D8D = $2991 bit6          (C2/933D ROL chain)
  out[0x08] = (b >> 7) & 1;      // $0D8E = $2991 bit7
  out[0x09] = (a >> 7) & 1;      // $0D8F = $2990 bit7
  out[0x0A] = (b >> 3) & 7;      // $0D90 = $2991 bits3-5       (C2/9332)
  out[0x0B] = 0;                 // $0D91 = not in packed source (stays 0 -> "Battle Speed" blank)
}

// Decode one config string: masterTable[strIdx] -> a NUL-terminated CT string at FF/D462+.
static int mCfgStr(const uint8_t* rom, long romLen, int strIdx, char* out, int cap) {
  (void)rom; (void)romLen;                           // the config strings are baked into ctRomCfg
  if (out && cap > 0) out[0] = 0;
  if (cap < 2 || strIdx < 0) return 0;
  long pe = (long)ROM_CFG_STRTAB + (long)strIdx * 2;
  unsigned p = mnuRom(pe) | (mnuRom(pe + 1) << 8);
  long so = (long)ROM_CFG_STRBANK + p;
  int w = 0;
  for (int i = 0; i < 24; i++) {
    uint8_t c = mnuRom(so + i);
    if (c == 0x00) break;                            // NUL terminator
    char ch;
    if      (c >= 0xA0 && c <= 0xB9) ch = (char)('A' + c - 0xA0);
    else if (c >= 0xBA && c <= 0xD3) ch = (char)('a' + c - 0xBA);
    else if (c >= 0xD4 && c <= 0xDD) ch = (char)('0' + c - 0xD4);
    else if (c == 0xDF)             ch = '?';        // the font's '?' ("Save Menu Cursor?", cells 2-4)
    else if (c == 0xE0)             ch = '/';
    else if (c == 0xE7)             ch = '\'';
    else if (c == 0xE8)             ch = '.';
    else if (c == 0xEF)             ch = ' ';
    else                            continue;        // unmapped: skip
    if (w < cap - 1) out[w++] = ch;
  }
  out[w] = 0;
  return w;
}

int CtMenuConfigItem(const CtMenuHost* h, int idx, char* name, int nameCap, char* val, int valCap) {
  if (name && nameCap > 0) name[0] = 0;
  if (val && valCap > 0) val[0] = 0;
  if (!h || idx < 0 || idx >= CT_MENU_CONFIG_COUNT) return 0;   // the tables are baked into menu_romdata.h
  mCfgStr(NULL, 0, idx, name, nameCap);              // name = masterTable[idx] (C2/C4BC cursor name)
  { int off  = mnuRom(ROM_CFG_CELLRAM + idx) & 0x0F; // which config byte (C2/C4F9[cursor], 0..0x0B)
    int base = mnuRom(ROM_CFG_CELLBASE + idx);       // value-string base  (C2/C505[cursor])
    // The live working byte: C2/C4BC reads `$0D86,Y`, and those twelve bytes are the only copy the screen edits (the
    // packed $2990-92 is written once, by the B-exit pack C2/934F). The packed bytes are the fallback for a caller
    // that renders without opening the menu.
    uint8_t fallback[12]; int cv;
    if (h->mem && (h->mem[CFG_WORK] | h->mem[CFG_WORK + 1] | h->mem[CFG_WORK + 5] | h->mem[CFG_WORK + 6]
                   | h->mem[CFG_WORK + 9] | h->mem[CFG_WORK + 10] | h->mem[CFG_WORK + 3])) cv = h->mem[CFG_WORK + off];
    else { mCfgUnpack(h->mem, fallback); cv = fallback[off]; }
    mCfgStr(NULL, 0, base + cv, val, valCap);         // value = masterTable[base + configByte] (-> $0DCF)
  }
  return 1;
}

// ---- Config (screen 4), the toggle machine (disasm/notes/menu2_config_toggles.md) ----
// A 21-cell cursor machine over 12 settings packed into three saved bytes. Every frame substate 1 (C2/BF45) runs the
// cursor mover C2/E9E2, decodes A/B (C2/E984), and calls C2/BFD4: `LDA $54 / STA $0417 / ASL A / TAX / JSR
// ($BFFF,X)`, a 12-entry jump table, one routine per cell 0-11. Nine cells are `INC A / AND #01` two-state toggles;
// $0D8B/$0D8C/$0D90 are 8-step wrapping cycles driven by Left/Right through the shared C2/C164 (`AND #07`); $0D8A is
// a 3-option row reached through a sub-cursor (substate 3); $0D87 opens the Control-Pad button editor (substate 2).

void CtMenuConfigUnpack(uint8_t* mem) {                 // C2/92F4, from the menu open C2/8133
  if (!mem) return;
  for (int i = 0; i < 12; i++) mem[CFG_WORK + i] = 0;   // C2/92FA: an overlapping MVN clears $0D86..$0D91
  uint8_t a = mem[CFG_PACKED], b = mem[CFG_PACKED + 1], d = mem[CFG_PACKED + 2];
  mem[CFG_WORK + 0x09] = (uint8_t)((a >> 7) & 1);       // ROL chain C2/9308-931C, top bit first
  mem[CFG_WORK + 0x03] = (uint8_t)((a >> 6) & 1);
  mem[CFG_WORK + 0x02] = (uint8_t)((a >> 5) & 1);
  mem[CFG_WORK + 0x01] = (uint8_t)((a >> 4) & 1);
  mem[CFG_WORK + 0x00] = (uint8_t)((a >> 3) & 1);
  mem[CFG_WORK + 0x05] = (uint8_t)(a & 7);              // C2/931F $0D8B
  mem[CFG_WORK + 0x06] = (uint8_t)(b & 7);              // C2/9327 $0D8C
  mem[CFG_WORK + 0x0A] = (uint8_t)((b >> 3) & 7);       // C2/9332 $0D90
  mem[CFG_WORK + 0x08] = (uint8_t)((b >> 7) & 1);       // C2/933D ROL chain
  mem[CFG_WORK + 0x07] = (uint8_t)((b >> 6) & 1);
  mem[CFG_WORK + 0x04] = (uint8_t)(d & 3);              // C2/9345 $0D8A
}

void CtMenuConfigPack(uint8_t* mem) {                   // C2/934F, from the B-exit C2/BF67
  if (!mem) return;
  const uint8_t* w = mem + CFG_WORK;
  mem[CFG_PACKED]     = (uint8_t)((w[0x09] << 7) | (w[0x03] << 6) | (w[0x02] << 5)
                                | (w[0x01] << 4) | (w[0x00] << 3) | (w[0x05] & 7));
  mem[CFG_PACKED + 1] = (uint8_t)((w[0x08] << 7) | (w[0x07] << 6) | ((w[0x0A] & 7) << 3) | (w[0x06] & 7));
  mem[CFG_PACKED + 2] = (uint8_t)(w[0x04] & 3);
}

// The button map's six rows in the menu's BG tilemap buffer $7E:2E00 (C2:C5A7: 0374 03F4 0474 04F4 0574 05F4 -- tile
// column 26, a row every two tile rows): the label word at +0, then +2 / +4 = the "-> new" pair C2/C5C7 lays.
#define CFG_LABEL_BUF 0x2E00
static unsigned cfgLabelOff(int row) {
  return (unsigned)mnuRom(0x02C5A7u + (unsigned)row * 2) | ((unsigned)mnuRom(0x02C5A8u + (unsigned)row * 2) << 8);
}
static void cfgPutWord(uint8_t* mem, unsigned a, unsigned w) { mem[a] = (uint8_t)w; mem[a + 1] = (uint8_t)(w >> 8); }
// C2/C5B3: a single-bit mask -> its glyph word: SEC, then INX INX / ROL A until the carry comes out (8-bit A), and the
// word at FF/D554,X (A0 'A', B7 'X', AB 'L', B1 'R', A1 'B', B8 'Y', B2 'S' for bits 7..1).
static unsigned cfgMaskGlyph(int mask) {
  unsigned a = (unsigned)mask & 0xFF, c = 1, x = 0xFFFE;
  do { x = (x + 2) & 0xFFFF; unsigned out = a >> 7; a = ((a << 1) | c) & 0xFF; c = out; } while (!c);
  return (unsigned)mnuRom(0x3FD554u + x) | ((unsigned)mnuRom(0x3FD555u + x) << 8);
}
// C2/C57A: the six labels from the live map $2993+0..5, and the two words after each blanked (00FF).
static void cfgLabels(uint8_t* mem) {
  for (int row = 0; row < 6; row++) {
    unsigned y = CFG_LABEL_BUF + cfgLabelOff(row);
    cfgPutWord(mem, y, cfgMaskGlyph(mem[CFG_BTN_MERGED + row]));
    cfgPutWord(mem, y + 2, 0x00FF); cfgPutWord(mem, y + 4, 0x00FF);
  }
}
// C2/C5C7 (the tail of an assignment, C2/C237 JMP): every row whose working mask differs from the committed map
// ($0408.. copied to $9890.., the row being edited forced to FF so it always differs) gets tile 31 (the arrow) and the
// working mask's glyph after its label; the others get FFFF twice.
static void cfgMarkChanges(uint8_t* mem, int editRow) {
  for (int i = 0; i < 8; i++) mem[0x9890 + i] = mem[CFG_BTN_LIVE + i];   // C2/C5CA MVN, 8 bytes
  mem[0x9890 + editRow] = 0xFF;                                         // C2/C5E3
  for (int row = 5; row >= 0; row--) {                                  // C2/C5D6 $00 = 5 .. DEC / BPL
    int m = mem[CFG_BTN_WORK + row];
    unsigned w = (m == mem[0x9890 + row]) ? 0xFFFF : 0x0031;
    unsigned y = CFG_LABEL_BUF + cfgLabelOff(row);
    cfgPutWord(mem, y + 2, w); cfgPutWord(mem, y + 4, w);
    if (w != 0xFFFF) cfgPutWord(mem, y + 4, cfgMaskGlyph(m));
  }
}

const uint8_t* CtMenuFontOverlayC9FC(void) { return ctMenuFontC9FC; }

void CtMenuListWindow(uint8_t* mem, int page, int count, unsigned dividend) {
  if (!mem) return;
  page &= 0xFF; count &= 0xFF;
  mem[0x0DD8] = (uint8_t)page;                          // 2EA84 LDA $00 / STA $0DD8
  int a = count - page;                                 // 2EA89 TYA / SEC / SBC $00
  uint8_t div = (uint8_t)a;                             // 2EA8D STA $4206 (the 8-bit divisor, as written)
  int n = page, range = div;                            // 2EA91 STA $01
  if (a < 0) { range = 0; n = count; }                  // 2EA93 BCS / STZ $01 / STY $00
  mem[0x0DDC] = (uint8_t)((n ? n - 1 : 0) + mem[0x0DDB]);   // 2EA99 LDA $00 / BEQ / DEC A / CLC / ADC $0DDB
  mem[0x0057] = mem[0x0D94] = mem[0x0DDA] = (uint8_t)range;   // 2EAA5
  unsigned q = div ? (dividend & 0xFFFF) / div : 0xFFFF;   // $4214 (a divide by 0 gives FFFF)
  mem[0x0D98] = (uint8_t)q; mem[0x0D99] = (uint8_t)(q >> 8);   // 2EAB1 LDA $004214 / STA $0D98
}

int CtMenuListPage(uint8_t* mem, int pageBits, int* cell) {
  if (!mem || !(pageBits & 3)) return 0;               // 293C2 LDA $0D1D / AND #03 / BEQ
  int s = mem[0x0DD9], a;
  if (pageBits & 2) {                                   // bit1 (R): a page back
    a = (s - mem[0x0DD8]) & 0xFF;                       // 293CD SBC $0DD8 (8-bit)
    if (!(a < mem[0x0DDA])) { if (cell) *cell = mem[0x0DDB]; a = 0; }   // 293D4 CMP $0DDA / BCC; $54 := $0DDB
  } else {                                              // bit0 (L): a page on
    a = s + mem[0x0DD8];                                // 293E2 ADC $0DD8 / BCS
    if (a > 0xFF || !(a < mem[0x0DDA])) { if (cell) *cell = mem[0x0DDC]; a = mem[0x0DDA]; }   // $54 := $0DDC
  }
  if (a == s) return 0;                                 // 293F8 CMP $0DD9 / BEQ
  mem[0x0DD9] = (uint8_t)a; mem[0x0D95] = (uint8_t)a; mem[0x0081] = 0xFF;   // 293FD
  return 1;
}

// C2/8BB8: the scrollbar task (slot 4) draws two OBJ sprites, the up and the down triangle (tile 22, attr 30 and B0 =
// V-flipped), both at X = $0D92, the up one at Y = $0D93 + hi($0D95 x $0D98) + lo($0D95 x $0D99) (8-bit adds, the
// carry kept) and the down one 8 below; only in the blink's on phase ($0D97 bit 0, stepped by the task), with $0D9A
// clear and a range ($0D94 != 0); no up arrow at scroll 0, no down arrow once $0D95 >= $0D94. Each screen seeds the
// bytes (C2/ADD0, C2/A45D, C2/A41A, C2/BA4F, C2/BB50 through C2/EA81); the caller draws this while the task lives.
void CtMenuRenderScrollbar(const CtMenuHost* h, uint8_t* fb) {
  if (!h || !h->mem || !fb) return;
  const uint8_t* m = h->mem;
  if (!(m[0x0D97] & 1) || m[0x0D9A] || !m[0x0D94]) return;   // 28BD0 / 28BD6 / 28C0B
  unsigned s = m[0x0D95];
  unsigned a = ((s * m[0x0D98]) >> 8) & 0xFF;           // 28BF0 PLA: hi(scroll x step.lo)
  a += m[0x0D93];                                      // 28BFC CLC / ADC $0D93
  unsigned c = a >> 8; a &= 0xFF;
  a += ((s * m[0x0D99]) & 0xFF) + c;                    // ADC $4216: lo(scroll x step.hi)
  c = a >> 8; a &= 0xFF;
  int x = m[0x0D92], yUp = (int)a, yDn = (int)((a + 8 + c) & 0xFF);   // 28C07 ADC #08
  // OAM 30 (the up arrow) is in front of OAM 31 where the two overlap (the lower index wins), so it is painted last.
  if (s < m[0x0D94]) mBlitRGBAFlipV(fb, ctMenuArrow, 16, 16, x, yDn); // 28C1A / 28C1F: OAM 31
  if (s) mBlitRGBA(fb, ctMenuArrow, 16, 16, x, yUp);                  // 28C10 / 28C15: OAM 30
}

void CtMenuRosterLists(uint8_t* mem) {
  if (!mem) return;
  for (int i = 0; i < 9; i++) mem[0x0D5F + i] = 0xFF;   // C2/8115: $0D5F..$0D67 := FF
  int y = 0; mem[0x0073] = 0; mem[0x0075] = 0;          // C2/816D STX $73 / STX $75
  for (int x = 0; x < 9; x++) {                         // C2/8174 LDA $2980,X / BMI skip
    if (mem[0x2980 + x] & 0x80) continue;
    mem[0x0D5F + y] = mem[0x2980 + x]; mem[0x0D68 + y] = (uint8_t)x; y++;
    if (x < 3) mem[0x0073]++;                           // C2/8182 CPX #03 / BCS / INC $73
    mem[0x0075]++;                                      // C2/8187 INC $75
  }
  // FF/F813: the list is the active party -- or the whole roster once $7F:0000 >= 0x49 with $7F:00BA bit0 clear
  int n = mem[0x0073];
  if (mem[0x10000] >= 0x49 && !(mem[0x100BA] & 1)) n = mem[0x0075];
  mem[0x0085] = (uint8_t)n; mem[0x0086] = 0;             // FF/F82A STX $85 / STZ $86
}

// C2/C3E4: the ten checkboxes into the tilemap buffer: for i = 9..0 the 2x2 block at C2:C41B[i] (+0, +2, +40, +42)
// gets tiles $1530.. + 4 * ((C2:C42F[i] ^ $0F09[i]) & 1). Run by C2/BFD4 after the cell's handler and by the screen
// build (C2/C340), not by the substate-3 / substate-2 handlers, so leaving the 3-option row (C2/BFC5 clears $0F0D)
// shows the old mark until the next frame's C2/BFD4 redraws it.
static unsigned cfgBoxAddr(int i) {
  return (unsigned)mnuRom(0x02C41Bu + (unsigned)i * 2) | ((unsigned)mnuRom(0x02C41Cu + (unsigned)i * 2) << 8);
}
static void cfgCheckboxes(uint8_t* mem) {
  for (int i = 9; i >= 0; i--) {
    unsigned y = cfgBoxAddr(i);
    unsigned t = 0x1530u + 4u * ((unsigned)(mnuRom(0x02C42Fu + (unsigned)i) ^ mem[CFG_MIRROR + i]) & 1u);
    cfgPutWord(mem, y, t); cfgPutWord(mem, y + 2, t + 1); cfgPutWord(mem, y + 0x40, t + 2); cfgPutWord(mem, y + 0x42, t + 3);
  }
}

void CtMenuConfigRebuildLive(uint8_t* mem) {            // C2/C54F = C2/C555 + C2/C57A
  if (!mem) return;
  for (int i = 0; i < 9; i++)                           // ROM defaults C2/FCA9 -> $2993..$299B
    mem[CFG_BTN_MERGED + i] = mnuRom(ROM_CFG_DEFAULT + 3 + i);
  if (mem[CFG_WORK + 0x01])                             // Control Pad == Custom -> the player's map wins
    for (int i = 0; i < 9; i++) mem[CFG_BTN_MERGED + i] = mem[CFG_BTN_LIVE + i];
  cfgLabels(mem);                                       // C2/C552 JMP C2/C57A
}

void CtMenuConfigDefaults(uint8_t* mem) {               // C2/95D4..C2/960A (a new game)
  if (!mem) return;
  for (int i = 0; i < 12; i++) mem[CFG_PACKED + i] = mnuRom(ROM_CFG_DEFAULT + i);
  for (int i = 0; i < 9; i++)  mem[CFG_BTN_LIVE + i] = mnuRom(ROM_CFG_DEFAULT + 3 + i);
}

void CtMenuConfigBuild(uint8_t* mem) {                  // C2/C3B5 (nav copy) + C2/C43A (mirror)
  if (!mem) return;
  for (int i = 0; i < CT_MENU_CONFIG_CELLS * 6; i++)    // FF/B5A0 -> $7E:9380, verbatim
    mem[CFG_NAV_RAM + i] = mnuRom(ROM_CFG_NAV + i);
  for (int i = 0; i < 10; i++) mem[CFG_MIRROR + i] = mem[CFG_WORK + i];   // $0D86..$0D8F -> $0F09..$0F12
  mem[CFG_MIRROR + 4] = 0; mem[CFG_MIRROR + 5] = 0; mem[CFG_MIRROR + 6] = 0;  // the three sub-mode flags
  mem[0x0D1F] = 0;                                      // the cursor freeze (cell 6's edit mode) starts clear
  cfgCheckboxes(mem);                                   // C2/C340 JSR C2/C3E4
  CtMenuConfigRebuildLive(mem);
}

int CtMenuConfigNav(const uint8_t* mem, int cell, int dir) {
  if (!mem || cell < 0 || cell >= CT_MENU_CONFIG_CELLS || dir < 0 || dir > 3) return cell;
  static const int off[4] = { 5, 4, 3, 2 };             // C2/EA1F: dir 0 Right=+5, 1 Left=+4, 2 Down=+3, 3 Up=+2
  int n = mem[CFG_NAV_RAM + cell * 6 + off[dir]];
  return (n < CT_MENU_CONFIG_CELLS) ? n : cell;
}

void CtMenuConfigCellPos(const uint8_t* mem, int cell, int* x, int* y) {
  int cx = 8, cy = 27;
  if (mem && cell >= 0 && cell < CT_MENU_CONFIG_CELLS) {
    cx = mem[CFG_NAV_RAM + cell * 6 + 0]; cy = mem[CFG_NAV_RAM + cell * 6 + 1];
  }
  if (x) *x = cx; if (y) *y = cy;
}

// C2/C164: cycle a byte 0..7 with Left/Right, wrapping both ways. Up/Down fall through to the nav table (`$5A & 3 ==
// 0` -> the routine does nothing), which is how cells 10/11 let Up/Down swap strips.
static void cfgCycle(uint8_t* mem, int addr, int dirBits, CtMenuConfigIO* io) {
  int d = dirBits & 3;                                   // bit0 = Right, bit1 = Left
  if (!d) return;
  int v = mem[addr];
  v = (d & 1) ? (v + 1) : (v - 1);                       // C2/C16F..C175
  mem[addr] = (uint8_t)(v & 7);                          // C2/C177 AND #07
  io->blip = 1;                                          // C2/C17F
}

// C2/C26C: the conflict pass. Every row in `row`'s list that holds the new mask inherits the old (live) one, and $04
// records the last row that had to give it up.
static void cfgConflict(uint8_t* mem, int row, int newMask, int* lastSwapped) {
  int y = mnuRom(ROM_CFG_ROWIDX + row);                  // C2/C23A: row -> map index (identity 00..07)
  mem[CFG_BTN_WORK + y] = (uint8_t)newMask;              // the assignment
  int oldMask = mem[CFG_BTN_MERGED + y];                 // C2/C247 LDA $2993,Y = the live mask it had
  unsigned pe = ROM_CFG_CONFLICT + (unsigned)row * 2;
  unsigned lp = (unsigned)mnuRom(pe) | ((unsigned)mnuRom(pe + 1) << 8);   // a bank-C2 pointer
  int len = mnuRom(0x020000u + lp);                      // list = [len, rows...]
  for (int i = len - 1; i >= 0; i--) {                   // C2/C26C walks it downward (LDX $02 / DEX / BPL)
    int r2 = mnuRom(0x020000u + lp + 1 + (unsigned)i);
    if (mem[CFG_BTN_WORK + r2] == (uint8_t)newMask) {
      mem[CFG_BTN_WORK + r2] = (uint8_t)oldMask;
      if (lastSwapped) *lastSwapped = r2;
    }
  }
}

// C2/C20F: assign `newMask` to `row`, settle the conflicts twice, then repair the two page slots.
static void cfgAssign(uint8_t* mem, int row, int newMask) {
  for (int i = 0; i < 9; i++) mem[CFG_BTN_WORK + i] = mem[CFG_BTN_LIVE + i];   // reset the working copy from the live map
  int last = 0;                                          // $04
  cfgConflict(mem, row, newMask, &last);                 // pass 1
  cfgConflict(mem, last, mem[CFG_BTN_WORK + last], &last);   // pass 2: re-settle the row that gave way
  {                                                      // C2/C286: repair the L/R page slots
    int m = mem[CFG_BTN_WORK + 0] | mem[CFG_BTN_WORK + 1];
    if (mem[CFG_BTN_WORK + 7] == 0x10) mem[CFG_BTN_WORK + 7] = mem[CFG_BTN_WORK + 6];
    if (!(0x10 & m))                   mem[CFG_BTN_WORK + 6] = 0x10;
    if (mem[CFG_BTN_WORK + 6] == 0x20) mem[CFG_BTN_WORK + 6] = mem[CFG_BTN_WORK + 7];
    if (!(0x20 & m))                   mem[CFG_BTN_WORK + 7] = 0x20;
  }
  cfgMarkChanges(mem, row);                              // C2/C237 JMP C2/C5C7
}

// C2/BFFF: the 12-entry cell -> handler table, as one switch. $54 <= 11 holds while the substate is 1 (cells 12-20
// live only inside substates 3 and 2, which never reach C2/BFD4).
static void cfgCellDispatch(uint8_t* mem, int* cell, int* sub, int a, int b, int dirBits,
                            CtMenuConfigIO* io) {
  mem[0x0417] = (uint8_t)*cell;                          // C2/BFDA STA $0417 = the cell the bar describes
  switch (*cell) {
    case 0:  if (a) { uint8_t v = (uint8_t)((mem[CFG_WORK + 0x00] + 1) & 1);   // Stereo (C2/C017)
                      mem[CFG_WORK + 0x00] = v; mem[CFG_MIRROR + 0] = v;
                      io->apuStereo = v; }               // C2/C028: APU cmd $F3, param 0 stereo / 1 mono
             break;
    case 1:  if (a) { uint8_t v = (uint8_t)((mem[CFG_WORK + 0x03] + 1) & 1);   // Battle Mode (C2/C06E)
                      mem[CFG_WORK + 0x03] = v; mem[CFG_MIRROR + 3] = v; } break;
    case 2:  if (a) { uint8_t v = (uint8_t)((mem[CFG_WORK + 0x02] + 1) & 1);   // Save Menu Cursor (C2/C038)
                      mem[CFG_WORK + 0x02] = v; mem[CFG_MIRROR + 2] = v; } break;
    case 3:  if (a) { uint8_t v = (uint8_t)((mem[CFG_WORK + 0x07] + 1) & 1);   // Save Battle Cursor (C2/C04A)
                      mem[CFG_WORK + 0x07] = v; mem[CFG_MIRROR + 7] = v; } break;
    case 4:  if (a) { uint8_t v = (uint8_t)((mem[CFG_WORK + 0x08] + 1) & 1);   // Save Skill/Item (C2/C05C)
                      mem[CFG_WORK + 0x08] = v; mem[CFG_MIRROR + 8] = v; } break;
    case 9:  if (a) { uint8_t v = (uint8_t)((mem[CFG_WORK + 0x09] + 1) & 1);   // Skill/Item Info (C2/C095)
                      mem[CFG_WORK + 0x09] = v; mem[CFG_MIRROR + 9] = v; } break;
    case 5:  if (a) { *cell = 10; mem[0x0417] = 10; mem[CFG_MIRROR + 5] = 1; } break;   // C2/C0A7 header
    case 10: cfgCycle(mem, CFG_WORK + 0x05, dirBits, io);                       // Battle Gauge Speed
             if (a || b) { *cell = 5; mem[0x0417] = 5; mem[CFG_MIRROR + 5] = 0; } break;   // C2/C0B9
    case 11: cfgCycle(mem, CFG_WORK + 0x0A, dirBits, io);                       // Battle Mes. Speed
             if (a || b) { *cell = 5; mem[0x0417] = 5; mem[CFG_MIRROR + 5] = 0; } break;   // C2/C0D1
    case 6:                                                                     // Window Color (C2/C12C)
             if (mem[CFG_MIRROR + 6]) { cfgCycle(mem, CFG_WORK + 0x06, dirBits, io);
                                        io->winLoad = 1; }                      // C2/C13D JSR C2/C511, every frame
             if (a || b) {                                                      // A or B toggles edit mode
               mem[CFG_MIRROR + 6] ^= 1;
               // C2/C157 STX $93A4: the ROM rewrites cell 6's own nav record, so the hand moves beside the colour
               // strip while editing and back to the grid slot after.
               mem[CFG_NAV_RAM + 6 * 6 + 0] = (uint8_t)(mem[CFG_MIRROR + 6] ? 0x88 : 0x38);
               mem[CFG_NAV_RAM + 6 * 6 + 1] = (uint8_t)(mem[CFG_MIRROR + 6] ? 0x37 : 0x3B);
               io->blip = 1;                                                    // C2/C15A
             }
             mem[0x0D1F] = mem[CFG_MIRROR + 6];          // C2/C160: != 0 -> C2/E9E2 refuses to move next frame
             break;
    case 7:  if (a) { *cell = 12 + (mem[CFG_WORK + 0x04] & 3);                   // C2/C080 -> the 3-option row
                      mem[CFG_MIRROR + 4]++; *sub = 3; } break;
    case 8: {                                                                   // Control Pad (C2/C0E9)
             if (a) {
               uint8_t v = (uint8_t)((mem[CFG_WORK + 0x01] + 1) & 1);
               mem[CFG_WORK + 0x01] = v; mem[CFG_MIRROR + 1] = v;
               if (v) { *cell = 15; *sub = 2;                                    // became Custom -> the editor
                        for (int i = 0; i < 9; i++) mem[CFG_BTN_WORK + i] = mem[CFG_BTN_LIVE + i]; }
             } else if (mem[0x007F] == 8 && (dirBits & 1)) {                     // C2/C0EE: Right, twice
               if (mem[CFG_WORK + 0x01]) { *cell = 15; *sub = 2;
                        for (int i = 0; i < 9; i++) mem[CFG_BTN_WORK + i] = mem[CFG_BTN_LIVE + i]; }
               else { *cell = 3; mem[0x0417] = 3; }                              // Standard -> hop to cell 3
             }
             CtMenuConfigRebuildLive(mem);                                       // C2/C128, always
             break; }
    default: break;
  }
  mem[0x007F] = mem[0x0417];                             // C2/BFE4: $7F = $0417 after the handler ran
  cfgCheckboxes(mem);                                    // C2/BFEC JSR C2/C3E4
  if (a) io->blip = 1;                                   // C2/BFFA: a blip on every A press, whatever it did
}

int CtMenuConfigFrame(uint8_t* mem, int* cell, int* sub, CtMenuConfigIO* io) {
  if (!mem || !cell || !sub || !io) return 0;
  io->blip = 0; io->apuStereo = -1;
  int prevCell = *cell;                                  // C2/E9E5 STA $81: the cell before the mover
  int dirBits  = (io->dir >= 0 && io->dir <= 3) ? (1 << io->dir) : 0;   // the ROM's $5A
  // ---- C2/E9E2: the cursor moves first, unless cell 6's edit mode froze it ($0D1F, C2/EA01)
  if (io->dir >= 0 && mem[0x0D1F] == 0) *cell = CtMenuConfigNav(mem, *cell, io->dir);
  mem[0x0D1F] = 0;                                       // C2/EA1A STZ $0D1F
  mem[0x0D1E] = (uint8_t)(io->dir < 0 ? 0x80 : io->dir);
  // ---- C2/E98D: the frame a substate changes eats its own button, so entering a sub-mode never re-fires the A
  //      that opened it.
  int a = io->a, b = io->b;
  if (mem[0x0D17] != (uint8_t)*sub) { a = 0; b = 0; }
  mem[0x0D17] = (uint8_t)*sub;

  if (*sub == 3) {                                       // C2/BF9F: the Battle-Gauge 3-option row
    if (a || b) { mem[CFG_MIRROR + 4] = 0; *cell = mem[0x0417]; *sub = 1; io->blip = 1; }   // C2/BFC5
    else if (*cell != prevCell) {                        // C2/BFAF: the value is the cursor cell - 12
      mem[CFG_WORK + 0x04] = (uint8_t)(*cell - 12); io->blip = 1;
    }
    return 0;
  }
  if (*sub == 2) {                                       // C2/BF6D: the Control-Pad button editor
    if ((dirBits & 2) || b) {                            // Left or B: commit working -> live and leave
      for (int i = 0; i < 9; i++) mem[CFG_BTN_LIVE + i] = mem[CFG_BTN_WORK + i];
      io->blip = 1; *cell = mem[0x0417]; *sub = 1;       // $0417 is still 8 -> back to the Control Pad cell
      CtMenuConfigRebuildLive(mem);
      return 0;
    }
    if (io->start) {                                     // C2/C188: Start restores the ROM defaults
      io->blip = 1;
      for (int i = 0; i < 9; i++) {
        mem[CFG_BTN_LIVE + i] = mnuRom(ROM_CFG_DEFAULT + 3 + i);
        mem[CFG_BTN_WORK + i] = mnuRom(ROM_CFG_DEFAULT + 3 + i);
      }
      CtMenuConfigRebuildLive(mem);
    } else if (dirBits & 0x0C) {                         // C2/C1AC: a row change commits working -> live
      io->blip = 1;
      for (int i = 0; i < 9; i++) mem[CFG_BTN_LIVE + i] = mem[CFG_BTN_WORK + i];
      CtMenuConfigRebuildLive(mem);
    }
    { int row = *cell - 15;                              // C2/C1C6
      if (row >= 0 && row < 6) {
        int x = mem[0x0051];                             // C2/C1CC LDX $51 (0 in practice, kept as written)
        int m;
        while ((m = mnuRom(ROM_CFG_BTNCYCLE + (unsigned)x)) != 0xFF && !(m & mem[CFG_BTN_WORK + row])) x++;
        if (m != 0xFF && io->dir >= 0 && io->dir < 2) {   // C2/C1E1: Up/Down never re-assign
          if (io->dir == 1) { x--; if (x < 0) x = 6; }    // Left  (C2/C1EF DEX / BPL / LDX #06)
          else              { x++; if (x >= 7) x = 0; }   // Right (C2/C1E8 INX / CPX #07 -> LDX #01 then DEX)
          cfgAssign(mem, row, mnuRom(ROM_CFG_BTNCYCLE + (unsigned)x));
          io->blip = 1;                                   // C2/C1FD
        }
      } }
    return 0;
  }
  // ---- substate 1: the 2x5 grid (C2/BF45)
  if (b && !(mem[CFG_MIRROR + 5] | mem[CFG_MIRROR + 6])) {   // C2/BF5F: not inside a sub-mode
    CtMenuConfigPack(mem);                                    // C2/BF67 -> $2990-92
    return 1;                                                 // C2/BF6A JMP C2/9A98 (back to the ribbon)
  }
  cfgCellDispatch(mem, cell, sub, a, b, dirBits, io);         // C2/BF52 JSR C2/BFD4
  if (*cell != prevCell) io->blip = 1;                        // C2/BF5B: a blip when the cursor moved
  return 0;
}

// Draw a "12345678" number strip (n digits, 8 px pitch) at (sx,sy): all in the light dim ramp (drawGlyphDim2), then
// the current value cv (1..n) redrawn bright (pal0).
static void mCfgStrip(const CtMenuHost* h, uint8_t* fb, int sx, int sy, int cv, int n) {
  mDim = 2;
  for (int d = 1; d <= n; d++) { char s[2] = { (char)('0' + d), 0 }; mText(h, fb, s, sx + (d - 1) * 8, sy); }
  mDim = 0;
  if (cv >= 1 && cv <= n) { char s[2] = { (char)('0' + cv), 0 }; mText(h, fb, s, sx + (cv - 1) * 8, sy); }
}
// Config (opt3): the two-panel layout. Left = a 2-col x 5-row grid of 16x16 setting icons, each with a checkbox; right =
// a value panel (number strips with the current step bright, Fast/Slow, Off/1/2, and the Control-Pad button map). The
// 12 settings' names and values come from CtMenuConfigItem.
// The bottom bar's two runs: C2/C4BC stages the cell into $0DCE and the value index into $0DCF and calls C2/F943,
// which renders the name through the VWF (`JSL C2:0003`) and places the value on a tile boundary past it:
//     2F9ED  LDA $0DD2 / AND #F8 / LSR A / LSR A / INC A / INC A / STA $0DC1
// i.e. the pen the name left, rounded down to a tile, plus two tiles (menu_phase2.md). The name is the plain menu
// ramp (ctLoaderVwfTone) and the value yellow (drawMenuGlyphYellow's ramp: body 255,255,0 / shadow 33,33,0 / mid
// 140,74,0).
static const uint8_t ctMenuVwfYellow[4][3] = { {0,0,0}, {33,33,0}, {140,74,0}, {255,255,0} };
// Both renderers ink from the y they are handed (the bar's ink rows 188..196).
static void mBarVwf(const CtMenuHost* h, uint8_t* fb, const char* s, int x, int y,
                    const uint8_t tone[4][3]) {
  if (h->vwfDraw) { h->vwfDraw(fb, s, x, y, tone); return; }
  mText(h, fb, s, x, y);            // no VWF on this host -> the 8x8 menu font
}
// $0DCF of setting `idx`: the value string's index (C2/C4BC: $0D86[C2C4F9[idx]] + C2C505[idx]).
int CtMenuConfigBarVal(const CtMenuHost* h, int idx) {
  if (!h || idx < 0 || idx >= CT_MENU_CONFIG_COUNT) return 0;
  int off = mnuRom(ROM_CFG_CELLRAM + idx) & 0x0F, base = mnuRom(ROM_CFG_CELLBASE + idx);
  uint8_t fallback[12]; int cv;
  if (h->mem && (h->mem[CFG_WORK] | h->mem[CFG_WORK + 1] | h->mem[CFG_WORK + 5] | h->mem[CFG_WORK + 6]
                 | h->mem[CFG_WORK + 9] | h->mem[CFG_WORK + 10] | h->mem[CFG_WORK + 3])) cv = h->mem[CFG_WORK + off];
  else { mCfgUnpack(h->mem, fallback); cv = fallback[off]; }
  return (base + cv) & 0xFF;
}
void CtMenuRenderConfig(const CtMenuHost* h, uint8_t* fb, int cell, int sub, int fresh, int nameN, int valN) {
  if (!h || !fb) return;
  (void)sub;                                   /* the substate only decides which panel the hand is on,
                                                  and the cell id already carries that (12-14 / 15-20) */
  // the background (the BG2 chrome) is laid by the caller (CtMenuChrome)
  // cell < 0 = the ribbon's opt-3 hover: the grid is there, but no hand and no bottom bar (the bar is $0417, and its
  // two strings are staged by C2/C4BC, which the screen's handler runs; the ribbon has not entered it yet). Cell/cursor
  // -1 = no cursor, as on the Item, Form and Save previews.
  int hover = (cell < 0);
  if (cell < 0) cell = 0;
  else if (cell >= CT_MENU_CONFIG_CELLS) cell = CT_MENU_CONFIG_CELLS - 1;
  // The bottom bar describes $0417, not the cursor: the sub-modes leave it on the cell you came from (5, 7 or 8)
  // while the cursor sits on 10/11, 12-14 or 15-20 (C2/BFDA / C2/C0B0 / C2/C0AE).
  int bar = (h->mem && cell > 11) ? (h->mem[0x0417] & 0x0F) : cell;
  if (bar >= CT_MENU_CONFIG_COUNT) bar = CT_MENU_CONFIG_COUNT - 1;
  // -- left grid: 10 cells (cursors 0-9). Col A icon x24/box x40, col B icon x72/box x88; rows y{24..152}.
  static const uint8_t* const icon[10] = {
    CFG_ICON_STEREO, CFG_ICON_BATTLEMODE, CFG_ICON_SAVEMENU, CFG_ICON_SAVEBATTLE, CFG_ICON_SAVESKILL,
    CFG_ICON_BATTLESPEED, CFG_ICON_WINDOWCOLOR, CFG_ICON_BATTLEGAUGE, CFG_ICON_CONTROLPAD, CFG_ICON_SKILLINFO };
  static const int rowY[5] = { 24, 56, 88, 120, 152 };
  for (int c = 0; c < 10; c++) {
    int col = c / 5, iy = rowY[c % 5];
    int ix = col ? 72 : 24, bx = col ? 88 : 40;
    mBlitRGBA(fb, icon[c], 16, 16, ix, iy);
    // Checkbox (C2/C3E4): tile = $1530 + 4*((C2C42F[i] ^ $0F09[i]) & 1), so the mirror decides, not the setting, and
    // the mirror slot for grid cells 5/6/7 is a sub-mode flag (C2/C43A zeroes $0F0D/$0F0E/$0F0F after the copy).
    // Slot 0's polarity is 1 because "Stereo On" is the zero value. cfgBox maps the grid slot to the ROM's checkbox
    // index (the order of C2/C41B's tilemap addresses).
    static const int cfgBox[10] = { 0, 3, 2, 7, 8, 5, 6, 4, 1, 9 };
    static const int cfgPol[10] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };   /* C2/C42F */
    int bi = cfgBox[c];
    int on = cfgPol[bi] & 1;                                           // (no WRAM: the polarity alone)
    if (h->mem) { unsigned a = cfgBoxAddr(bi);                         // the block C2/C3E4 last laid
                  on = (((unsigned)h->mem[a] | ((unsigned)h->mem[a + 1] << 8)) - 0x1530u) >> 2 & 1; }
    mBlitRGBA(fb, on ? CFG_CHECKBOX_CHECKED : CFG_CHECKBOX_EMPTY, 16, 16, bx, iy);
  }
  // -- right value panel: category icons (x120) + value rows. Read the multi-value settings' values.
  char v5[12], v6[12], v7[12], v8[12], v10[12], v11[12], junk[24];
  CtMenuConfigItem(h, 5, junk, sizeof junk, v5, sizeof v5);   // Battle Speed
  CtMenuConfigItem(h, 6, junk, sizeof junk, v6, sizeof v6);   // Window Color
  CtMenuConfigItem(h, 7, junk, sizeof junk, v7, sizeof v7);   // Battle Gauge
  CtMenuConfigItem(h, 8, junk, sizeof junk, v8, sizeof v8);   // Control Pad
  CtMenuConfigItem(h, 10, junk, sizeof junk, v10, sizeof v10); // Battle Gauge Speed
  CtMenuConfigItem(h, 11, junk, sizeof junk, v11, sizeof v11); // Battle Mes. Speed
  mBlitRGBA(fb, CFG_ICON_BATTLESPEED, 16, 16, 120, 28);      // cassette heads the 3 speed rows
  mBlitRGBA(fb, CFG_ICON_WINDOWCOLOR, 16, 16, 120, 52);      // window heads the window-color row
  mBlitRGBA(fb, CFG_ICON_BATTLEGAUGE, 16, 16, 120, 76);      // capsule heads the battle-gauge row
  mBlitRGBA(fb, CFG_ICON_CONTROLPAD,  16, 16, 120, 100);     // gamepad heads the button map
  mBlitRGBA(fb, CFG_FACE_LEFT,  16, 8, 136, 24);             // faces flank the Battle-Gauge-Speed strip
  mBlitRGBA(fb, CFG_FACE_RIGHT, 16, 8, 216, 24);
  mCfgStrip(h, fb, 152, 24, atoi(v10), 8);                   // Battle Gauge Speed
  mText(h, fb, "Fast", 144, 32); mText(h, fb, "Slow", 192, 32);  // Battle Speed (no marker: not ported)
  mCfgStrip(h, fb, 152, 40, atoi(v11), 8);                   // Battle Mes. Speed
  mCfgStrip(h, fb, 152, 56, atoi(v6), 8);                    // Window Color
  // Battle Gauge: Off / 1 / 2 on row y80 (its icon at y76).
  { int bg = atoi(v7);
    mDim = (bg != 0) ? 2 : 0; mText(h, fb, "Off", 144, 80); mDim = 0;   // same light dim ramp as
    mDim = (bg != 1) ? 2 : 0; mText(h, fb, "1", 184, 80); mDim = 0;     //   mCfgStrip above
    mDim = (bg != 2) ? 2 : 0; mText(h, fb, "2", 208, 80); mDim = 0; }
  // Control Pad button map (the Standard mapping for this state; the letters follow the Control-Pad value).
  static const char* const bmL[6] = { "Confirm", "Cancel", "Menu", "Dash", "Map", "Warp" };
  // The right column is the ROM's own buffer: the three words C2/C57A / C2/C5C7 keep at $2E00 + C2:C5A7[row] (the
  // live map's glyph, then the arrow and the working glyph after an assignment), tiles 26-28 of the row.
  for (int i = 0; i < 6; i++) { int by = 100 + i * 12;   // the button map, 12 px pitch from y100
    mText(h, fb, bmL[i], 144, by);
    if (h->mem) for (int k = 0; k < 3; k++) {
      unsigned a = CFG_LABEL_BUF + cfgLabelOff(i) + (unsigned)k * 2;
      mCodeAt(h, fb, h->mem[a], 208 + k * 8, by);      // (tile FF, C2/C57A's 00FF, is the font's blank)
    } }
  // -- the hand, placed from the cell's own nav record: its first two bytes are the cursor position (C2/E964 `LDA
  // $9380,X / STA $0770`), and mHandCursor's sprite inks 3 px inside it.
  if (!hover) { int hx, hy; CtMenuConfigCellPos(h->mem, cell, &hx, &hy); mHandCursor(h, fb, hx, hy); }
  // -- bottom bar: the setting $0417 describes -- its icon + name + value.
  if (hover || fresh) return;                         // the hover and the build frame (C2/C3B5): bare bar row
  // C2/C4BC lays the bar's 2x2 icon from FF/D562[$0417]: cells 10 / 11 (the two strips) carry cell 5's word (1538, the
  // speed icon), so the grid cell whose word it is names the picture.
  { unsigned want = (unsigned)mnuRom(0x3FD562L + bar * 2) | ((unsigned)mnuRom(0x3FD563L + bar * 2) << 8);
    for (int c = 0; c < 10; c++)
      if (((unsigned)mnuRom(0x3FD562L + c * 2) | ((unsigned)mnuRom(0x3FD563L + c * 2) << 8)) == want) {
        mBlitRGBA(fb, icon[c], 16, 16, 24, 184); break; } }
  char nm[24], vl[12]; CtMenuConfigItem(h, bar, nm, sizeof nm, vl, sizeof vl);
  // C2/F943's task types the name one character a frame and then the value (nameN / valN typed so far; -1 = whole);
  // the value's column comes from the whole name's pen ($0DC1 from $0DD2 after the name's terminator)
  int pen = MI_HX + 40 + (h->vwfWidth ? h->vwfWidth(nm) : (int)strlen(nm) * 8);
  if (nameN >= 0 && nameN < (int)strlen(nm)) nm[nameN] = 0;
  if (valN >= 0 && valN < (int)strlen(vl)) vl[valN] = 0;
  if (nm[0]) mBarVwf(h, fb, nm, MI_HX + 40, 188, ctLoaderVwfTone);
  if (vl[0]) mBarVwf(h, fb, vl, (pen & ~7) + 16, 188, ctMenuVwfYellow);
}

// -------------------------------------------------------------- Screen 6 = Save (disasm/notes/save_write.md)
// The validity test is FF/F9C4 and the summary reader C2/D19F, both with DB = $30 (PEA $3030/PLB/PLB at FF/F9C9), the
// 8 KB SRAM at $30:6000-7FFF (bank $30 wraps every 0x2000, so $30:7FF8 is image offset 0x1FF8).
//   Validity: X = (slot & 3)*2; signature word $30:7FF8,X == 0xE41B (FF/F9D8) and checksum word $30:7FF0,X == FF/FBE7's
//     sum. $00 := 0x0000 valid / 0x8080 bad signature / 0x4040 bad checksum; C2/D171 `LDA $00 / BEQ ->D17F` reads the
//     summary on 0 (sets $0D49,X, C2/D19F), else draws the no-data marker (LDA #1A / STA $020C / JSR C2/D296).
//   Summary (C2/D19F, slot base $7B): party $+0x0580 (bit 7 = empty member); level at +0x0200+id*0x50+0x12 (C2/D1D3
//     `LDA #$0200 / ADC $7B / ADC $4216`, C2/D1DD `LDA $300012,X`); leader name +0x05B0+id*6 (C2/D1F4); the save
//     number +0x059C (C2/D21B, capped at 0x3E7); location +0x05F3 & 0x1FF (C2/D22C); play time +0x05E0 (C2/D23F). Not
//     modelled in CtMenuSaveSummary: the words +0x059E (C2/D247) and +0x0793 (C2/D24E) the ROM also copies.
// The slot bases are a ROM table: FF/FBD3 `LDA $79 / AND #0003 / ASL A / TAX / LDA $FFFBE1,X / STA $7B / RTS`, and
// $FF:FBE1 = `00 60 00 6A 00 74` = $30:6000 / $6A00 / $7400, a stride of $0A00 (the span FF/FBE7's LDY #$09FE sums).
static const unsigned CT_SAVE_SLOT_BASE[CT_MENU_SAVE_SLOTS] = { 0x0000u, 0x0A00u, 0x1400u };
#define CT_SAVE_SLOT_SPAN 0x0A00u          /* FF/FBE7: the checksum covers a whole slot */

// The pack: the record table at $FF:FA7B, 8 x {srcAddr:2, srcBank:1, len:2}, raw bytes
//   00 24 7E B0 05 | 23 2C 7E 33 00 | 00 04 7E 10 00 | 00 01 00 10 00 |
//   00 00 7F 00 02 | 7E 02 00 02 00 | 90 02 00 11 00 | 7C 2C 7E 1E 00
// copied into $7E:9890 by FF/FA1A and walked five bytes at a time (FF/FA4B `ADC #0005`, FF/FA4F `CMP #0028` = 8
// records). Each record patches the source bank into a `MVN $30,$xx / RTL` trampoline the routine builds at $7E:9690
// (FF/FA0E `LDA #3054` / FF/FA14 `LDA #6B7E`) and MVNs len bytes to the slot, Y running on consecutively, so record k
// lands at base + the sum of the preceding lengths. Bank $00's low addresses are the $7E mirror: every source is WRAM.
static const struct { unsigned short addr; unsigned char bank; unsigned short len; }
CT_SAVE_RECS[8] = {
  { 0x2400, 0x7E, 0x05B0 },   // slot+0x0000  inventory + char sheets + party
  { 0x2C23, 0x7E, 0x0033 },   // slot+0x05B0  the 8 name records, then gold at $2C53 -> slot+0x05E0
  { 0x0400, 0x7E, 0x0010 },   // slot+0x05E3  the play clock ($0400-06, CtMenuClockTick)
  { 0x0100, 0x00, 0x0010 },   // slot+0x05F3  location word $0100 -> the summary's locId at +0x05F3
  { 0x0000, 0x7F, 0x0200 },   // slot+0x0603  the story flags
  { 0x027E, 0x00, 0x0002 },   // slot+0x0803
  { 0x0290, 0x00, 0x0011 },   // slot+0x0805
  { 0x2C7C, 0x7E, 0x001E },   // slot+0x0816  (total 0x0834 bytes)
};

// FF/FBE7. Raw bytes 08 8B C2 30 F4 30 30 AB AB A5 7B 85 00 A0 FE 09 A9 00 00 18 71 00 88 88 10 FA AB 28 60:
//     PHP / PHB / REP #30 / PEA $3030 / PLB / PLB      ; DB = $30
//     LDA $7B / STA $00                                 ; the slot base
//     LDY #09FE / LDA #0000
//     CLC                                               ; once, outside the loop
//     ADC ($00),Y / DEY / DEY / BPL -> the ADC          ; so the carry propagates between words
// Note: the CLC is outside the loop and the walk goes downward, from $09FE to 0, with the carry kept.
static unsigned ctSaveChecksum(const unsigned char* slot) {
  unsigned a = 0, c = 0;
  for (int y = (int)CT_SAVE_SLOT_SPAN - 2; y >= 0; y -= 2) {
    unsigned w = (unsigned)slot[y] | ((unsigned)slot[y + 1] << 8);
    unsigned v = a + w + c;
    c = (v > 0xFFFFu) ? 1u : 0u;
    a = v & 0xFFFFu;
  }
  return a;
}

// Exposed so a check can run the ROM's checksum over a save the ROM made and compare it with the word the ROM stored.
unsigned CtMenuSaveChecksum(const unsigned char* sram, int slot) {
  if (!sram || slot < 0 || slot >= CT_MENU_SAVE_SLOTS) return 0;
  return ctSaveChecksum(sram + CT_SAVE_SLOT_BASE[slot]);
}
unsigned CtMenuSaveSlotBase(int slot) {
  return (slot >= 0 && slot < CT_MENU_SAVE_SLOTS) ? CT_SAVE_SLOT_BASE[slot] : 0;
}

int CtMenuSaveWrite(unsigned char* sram, long sramLen, int slot, unsigned char* wram) {
  if (!sram || !wram || sramLen < 0x2000 || slot < 0 || slot >= CT_MENU_SAVE_SLOTS) return 0;
  // The counter is bumped before the pack, and it lands inside the save: FF/F9FE `LDA $79 / STA $0414`, then FF/FA05
  // `LDA $299C / INC A / BEQ ->FA0E / STA $299C`, a saturating increment (a wrap to 0 is discarded). $7E:299C is in
  // record 0 at slot offset +0x059C, the field C2/D21B shows as "Save #".
  wram[0x0414] = (unsigned char)slot;                 // FF/F9FE-FA00
  {
    unsigned n = (unsigned)wram[0x299C] | ((unsigned)wram[0x299D] << 8);
    unsigned inc = (n + 1) & 0xFFFFu;                 // FF/FA08 INC A (16-bit: REP #30 at FF/FA03)
    if (inc != 0) { wram[0x299C] = (unsigned char)(inc & 0xFF);      // FF/FA09 BEQ ->FA0E skips the
                    wram[0x299D] = (unsigned char)(inc >> 8); }      //   store when it wrapped to 0
  }
  unsigned base = CT_SAVE_SLOT_BASE[slot];
  unsigned off = 0;
  for (int i = 0; i < 8; i++) {                       // FF/FA2D..FA52, the eight MVNs
    // bank $7F is the flat image's second 64 KB; bank $00's low addresses are the $7E mirror.
    unsigned long src = (CT_SAVE_RECS[i].bank == 0x7F ? 0x10000u : 0u) + CT_SAVE_RECS[i].addr;
    memcpy(sram + base + off, wram + src, CT_SAVE_RECS[i].len);
    off += CT_SAVE_RECS[i].len;
  }
  unsigned sum = ctSaveChecksum(sram + base);         // FF/FA54 JSR FF/FBE7
  sram[0x1FF0 + slot * 2] = (unsigned char)(sum & 0xFF);          // FF/FA58 STA $30:7FF0,X
  sram[0x1FF1 + slot * 2] = (unsigned char)(sum >> 8);
  sram[0x1FF8 + slot * 2] = 0x1B;                                  // FF/FA5C LDA #E41B
  sram[0x1FF9 + slot * 2] = 0xE4;                                  // FF/FA5F STA $30:7FF8,X
  sram[0x1FE0] = (unsigned char)slot;                              // FF/FA65 LDA $79 / STA $30:7FE0
  // FF/FA6B `LDA $30:7FE1 / CMP $299E / BNE / INC A / STA $30:7FE1`. $299E has two sites in the ROM: C2/E66A's `LDA
  // $30:7FE1 / STA $299E` on the loader's New Game row, and this compare. So the pair is a playthrough counter with
  // an idempotence guard: New Game copies the SRAM byte into $299E, and a save advances the SRAM byte only while it
  // still matches, so only the first save of a playthrough bumps it. (The per-save counter is $299C, FF/FA05.)
  if (sram[0x1FE1] == wram[0x299E])                                // FF/FA6F CMP $299E / FA72 BNE
    sram[0x1FE1] = (unsigned char)(sram[0x1FE1] + 1);              // FA74 INC A / FA75 STA $30:7FE1
  return (int)off;                                    // bytes packed: 0x0834 when the table is intact
}

// ---- FF/F9C4, the slot validity gate ----
//   PHX/PHB/PHP / REP #30 / PEA $3030 / PLB / PLB      ; DB = $30 (the SRAM window)
//   JSR FF/FBD3                                        ; $7B = the slot base
//   LDA $79 / AND #0003 / ASL A / TAX
//   LDA $7FF8,X / CMP #E41B / BNE -> A = $8080         ; bad signature
//   JSR FF/FBE7 / CMP $7FF0,X / BNE -> A = $4040       ; bad checksum
//   -> A = $0000                                       ; used
// This is the full gate, signature and checksum; CtMenuSaveSlot (the summary view) stops at the signature. The 8 KB
// cart mirrors bank $30 every 0x2000, so $7FF8/$7FF0 land at 0x1FF8/0x1FF0.
int CtMenuSaveSlotValid(const unsigned char* sram, long sramLen, int slot) {
  if (!sram || sramLen < 0x2000 || slot < 0 || slot >= CT_MENU_SAVE_SLOTS) return 0;
  size_t mask = (size_t)sramLen - 1;
  size_t sigOff = (0x7FF8u + (size_t)(slot & 3) * 2) & mask;      // FF/F9D8
  size_t sumOff = (0x7FF0u + (size_t)(slot & 3) * 2) & mask;      // FF/F9E3
  if ((sram[sigOff] | (sram[sigOff + 1] << 8)) != 0xE41B) return 0;        // -> $8080
  unsigned sum = ctSaveChecksum(sram + CT_SAVE_SLOT_BASE[slot]);           // FF/F9E0 JSR FF/FBE7
  if (sum != (unsigned)(sram[sumOff] | (sram[sumOff + 1] << 8))) return 0; // -> $4040
  return 1;                                                               // -> $0000 = used
}

// ---- FF/FAA3, the load, the inverse of FF/F9FB ----
// It walks the same table: FF/FAB2 copies $FF:FA7B (8 x {addr:2, bank:1, len:2}) to $7E:9890, FF/FABE `JSR FF/FBD3`
// puts the slot base in A and `TAX` makes it the MVN source index, and the trampoline it builds at $7E:9690 is `LDA
// #3054 / STA $9690` + `LDA #6B30 / STA $9692` = `54 30 30 6B` = `MVN $30,$30 / RTL`. Per record FF/FAC6 `LDA
// $9892,Y / STA $9691` patches only the destination bank, so the source bank stays $30 = SRAM; Y = the record's
// destination address (FF/FACE), A = len-1 (FF/FAD3 `DEC A`). X runs on across the records as in the writer, so record
// k reads from base + the sum of the preceding lengths. The cursor advances by 5 to 0x28 (FF/FAE2/FAE6) = 8 records.
// Note: FF/FAA3 does not validate (FF/F9C4 is the caller's: the load screen lets the cursor rest only on an occupied
// row, C2/E743, before its `JSL FF/FAA3` at C2/E657), and it does not reboot (C2/E694's tail `JMP C2/8320` ends `JML
// $C0:0000`, the field engine's re-entry, which re-reads the $0100 this load overwrote).
int CtMenuSaveLoad(const unsigned char* sram, long sramLen, int slot, unsigned char* wram) {
  if (!sram || !wram || sramLen < 0x2000 || slot < 0 || slot >= CT_MENU_SAVE_SLOTS) return 0;
  unsigned base = CT_SAVE_SLOT_BASE[slot], off = 0;
  // The scratch it leaves (its one caller, C2/E657, runs with D = 0): D1/E7A1's `MVN $7E,$7E` copies $0000-$002A to
  // $CC65 in the next fight, so the $00 this load leaves reaches the battle.
  //   FF/FAA6-FAB0  $9690 = 54 30 / $9692 = 30 6B         the `MVN $30,$dd / RTL` trampoline ($9691 patched per record)
  //   FF/FAB2-FABB  MVN FF:FA7B -> $9890, $28 bytes      the 8-record table, five bytes each: addr, bank, len
  //   FF/FBD3-FBDE  $7B = FF/FBE1[slot] = $6000 + base   the slot's base in the battery RAM's bank $30
  //   FF/FAC6-FAD1  $9691 = the record's bank, $00 = its address -- the last record's stand
  wram[0x9690] = 0x54; wram[0x9692] = 0x30; wram[0x9693] = 0x6B;
  wram[0x007B] = (unsigned char)(0x6000 + base); wram[0x007C] = (unsigned char)((0x6000 + base) >> 8);
  for (int i = 0; i < 8; i++) {                       // FF/FAC4..FAE9, the eight MVNs
    unsigned char* t = wram + 0x9890 + i * 5;
    t[0] = (unsigned char)CT_SAVE_RECS[i].addr; t[1] = (unsigned char)(CT_SAVE_RECS[i].addr >> 8);
    t[2] = CT_SAVE_RECS[i].bank; t[3] = (unsigned char)CT_SAVE_RECS[i].len; t[4] = (unsigned char)(CT_SAVE_RECS[i].len >> 8);
  }
  for (int i = 0; i < 8; i++) {
    unsigned long dst = (CT_SAVE_RECS[i].bank == 0x7F ? 0x10000u : 0u) + CT_SAVE_RECS[i].addr;
    memcpy(wram + dst, sram + base + off, CT_SAVE_RECS[i].len);
    off += CT_SAVE_RECS[i].len;
    wram[0x9691] = CT_SAVE_RECS[i].bank;               // FF/FAC6-FAC9
    wram[0x0000] = (unsigned char)CT_SAVE_RECS[i].addr; wram[0x0001] = (unsigned char)(CT_SAVE_RECS[i].addr >> 8);   // FAD1
  }
  return (int)off;                                    // 0x0834 with the ROM's table intact
}

// ---- C2/95BB, the light wipe (flags + clock + the $2990 config block) ----
// Run by both boot-scan arms:
//   $7F:0000-01FF = 0                         (the story flags; 95BF-95CF)
//   $7E:0400-04FF = 0                         (the play-clock page; 95D3-95DF)
//   $7E:2990-29AF = 0                         (95E2-95EE)
//   $7E:2990 += 12 bytes from $C2:FCA6        (95F1-95FA) = 84 A0 01 80 08 40 08 02 04 10 20 40
//   $7E:0408 +=  9 bytes from $C2:FCA9        (95FD-9606) = 80 08 40 08 02 04 10 20 40
// $2990 = 0x84 has bit 6 clear until the Battle-Mode screen sets it.
static const unsigned char CT_WIPE_FCA6[12] = { 0x84,0xA0,0x01,0x80,0x08,0x40,0x08,0x02,0x04,0x10,0x20,0x40 };
void CtMenuNewGameWipeLight(unsigned char* wram) {
  if (!wram) return;
  memset(wram + 0x10000, 0, 0x200);                   // $7F:0000-01FF
  memset(wram + 0x0400, 0, 0x100);                    // $7E:0400-04FF
  memset(wram + 0x2990, 0, 0x20);                     // $7E:2990-29AF
  memcpy(wram + 0x2990, CT_WIPE_FCA6, 12);            // $C2:FCA6, 12 bytes
  memcpy(wram + 0x0408, CT_WIPE_FCA6 + 3, 9);         // $C2:FCA9 == FCA6+3, 9 bytes
}

// ---- C2/956E, the full new-game wipe ----
//   JSR C2/95BB                                       (the light wipe above)
//   $7E:2400-25FF = 0                                 (9574-9580)
//   $7E:2600     += 0x280 bytes from $CC:0000         (9583-958C) -- the char-sheet template
//   $7E:2C53/2C54 = 0                                 (958F-9592) -- gold
//   $7E:2980-2988 = 0x80, $7E:29AF = 0x80             (9595-95A7)
//   $7E:2C7C-2C99 = 0                                 (95AA-95B6)
// `charBase` is the caller's baked $CC:0000 image (field.c's ctCharBase); NULL leaves $2600 alone, for a host that
// seeds the sheets itself.
void CtMenuNewGameWipe(unsigned char* wram, const unsigned char* charBase, long charBaseLen) {
  if (!wram) return;
  CtMenuNewGameWipeLight(wram);                       // 9571 JSR C2/95BB
  memset(wram + 0x2400, 0, 0x200);                    // 9574-9580
  if (charBase && charBaseLen >= 0x280) memcpy(wram + 0x2600, charBase, 0x280);   // 9583-958C
  wram[0x2C53] = wram[0x2C54] = 0;                    // 958F-9592  (gold)
  memset(wram + 0x2980, 0x80, 9);                     // 9595 STA $2980 (#0080) then 959E-95A7 MVN
                                                      //   $2980 -> $2981 x8: the 0x80 propagates over
                                                      //   $2981..$2988, so all nine roster slots read empty
  wram[0x29AF] = 0x80; wram[0x29B0] = 0x00;           // 959B STA $29AF (the same 16-bit #0080)
  memset(wram + 0x2C7C, 0, 0x1E);                     // 95AA-95B6
}

// ---- C2/8D6A, the boot SRAM scan, leaf 9 of the $C2 gate ----
// Run right after the sound driver is up: `C0/0057 LDA #09 / JSL $C2:8004`; leaf 9 of the table at $C2:8C5B:
//   STZ $79 ; loop: JSL FF/F9C4 / ORA #00 / BEQ -> 8D85 ; INC $79 / CMP #03 / BCC loop
//   all three invalid:  JSR C2/956E ; A = 0x00
//   at least one valid: JSR C2/95BB ; A = 0x02
//   STA $7F0061
// So $7F0061 is the cart's verdict: 0 = blank cart, 2 = at least one valid save (1, written later by C2/804B /
// C2/E662, means a new game was started). Scene 0's boot script branches on it, and it decides whether the mode-0
// shell draws the load screen or goes straight into a new game.
int CtMenuSaveBootScan(const unsigned char* sram, long sramLen,
                       unsigned char* wram, const unsigned char* charBase, long charBaseLen) {
  int any = 0;
  for (int s = 0; s < CT_MENU_SAVE_SLOTS && !any; s++) any = CtMenuSaveSlotValid(sram, sramLen, s);
  if (any) { CtMenuNewGameWipeLight(wram); return 2; }           // 8D85 JSR C2/95BB / LDA #02
  CtMenuNewGameWipe(wram, charBase, charBaseLen); return 0;      // 8D7E JSR C2/956E / LDA #00
}

int CtMenuSaveSlot(const CtMenuHost* h, int slot, CtMenuSaveSummary* out) {
  if (out) memset(out, 0, sizeof *out);
  if (!h || slot < 0 || slot >= CT_MENU_SAVE_SLOTS) return 0;
  const uint8_t* sr = h->sram;
  // No SRAM image -> no valid save; the signature read is the FF/F9C4 gate.
  if (!sr || h->sramLen < 0x2000) return 0;
  // The ROM reads $30:7FF8, but the cart has 8 KB of SRAM, so bank $30's addresses wrap every 0x2000 and $7FF8 lands
  // at 0x1FF8. Mask to the image given.
  size_t mask = (size_t)h->sramLen - 1;                             // 8 KB image -> 0x1FFF
  size_t sigOff = (0x7FF8u + (size_t)(slot & 3) * 2) & mask;        // FF/F9D8: $30:7FF8 + (slot&3)*2
  int sig = sr[sigOff] | (sr[sigOff + 1] << 8);
  if (sig != 0xE41B) return 0;                                      // bad signature -> empty (0x8080)
  // Not checked here: the FF/FBE7 checksum (the summary view gates on the signature; CtMenuSaveSlotValid is the full
  // gate).
  size_t base = CT_SAVE_SLOT_BASE[slot];             // the ROM's table (FF/FBE1)
  int leader = sr[base + 0x0580];                                  // C2/D1C1 $30:0580 = party slot0 (leader)
  if (leader & 0x80) return 0;                                     // empty leader -> treat slot as empty
  if (out) {
    out->used     = 1;
    out->leaderId = leader & 7;
    out->lv       = sr[base + 0x0200 + (size_t)(leader & 7) * 0x50 + 0x12]; // C2/D1D3 sheet base +0x0200,
                                                                   //   C2/D1DD +0x12 = level
    // Gold is the 24-bit value at +0x05E0, the head of the 10-byte block C2/D238 MVNs (+0x059C is the save number).
    out->gold     = sr[base + 0x05E0] | (sr[base + 0x05E1] << 8) | (sr[base + 0x05E2] << 16);
    out->saveNo   = sr[base + 0x059C] | (sr[base + 0x059D] << 8);  // C2/D21B, capped at 999
    if (out->saveNo > 0x3E7) out->saveNo = 0x3E7;                  //   (CMP #$03E8 / BCC / LDA #$03E7)
    out->locId    = (sr[base + 0x05F3] | (sr[base + 0x05F4] << 8)) & 0x1FF;  // C2/D22C $30:05F3 & 0x1FF
    // The play clock: the digits after gold in the same MVN block (+0x05E7 hours, +0x05E6/+0x05E5 minute digits),
    // matched against one saved game only; +0x05E3/+0x05E4 are not identified.
    out->hh       = sr[base + 0x05E7];
    out->mm       = sr[base + 0x05E6] * 10 + sr[base + 0x05E5];
  }
  return 1;
}

// The Save screen's overwrite prompt (screen 6 sub-state 2; disasm/notes/menu2_save_rest.md). The display-list
// interpreter C2/ED31 runs $FF:C16F to open it (back up the 28x12-tile BG2 chrome region, draw an 8x10-tile window at
// tile (22,2) = px (176,16), re-arm three HDMA channels) and $FF:C191 to close it. The text is written into the BG1
// buffer when the slot list is built (C2/D0E5 -> $FF:C138) and kept invisible by a per-line $212C HDMA that holds BG1
// off for lines 0..128; the prompt frame stops running that channel. $FF:C787 is five lines:
//     0A 20 "Erase" 01 "old" 01 "save?" 01 FF FF "Yes" 01 FF FF "No" 0A 00
// (01 breaks the line; the answers are indented), inked at "Erase" (188, 28), "old" (188, 40), "save?" (188, 52),
// "Yes" (200, 64), "No" (200, 80). The hand is on records 3 and 4 of the slot list's $7E:9380 graph, (184, 63) and
// (184, 79) (`B8 3F 04 04 03 03` / `B8 4F 03 03 04 04`). The window is C2/ED31's op 04 from $FF:C16F
// (mRomWindowCells), in the window colour's tiles and out of the colour math under its own W2.
// `sel` is $54: 3 = Yes, 4 = No, anything else = the prompt is not up.
// A menu window as C2/ED31's op 04 lays it (C2/EDAA = C2/EDB0 + C2/EE7F), into a BG map of CT_MENU_CHROME_TMW cells a
// row. `a` = the op's six bytes: C2/EDD3 turns the first into the tile base `$0D47` -- bit 7 set: ((a << 2) & FF) * 5
// with the ASLs' carry (0x8B -> 0xDC: the window colour's own tiles 0x3DC-0x3EF), clear: `$0D8C` & 7 times 20 (a Save
// slot's colour set) -- and the second is its high byte `$0D48`; then x, y, w, h in cells. C2/EDF6's frame: the top
// row t, (t+1, t+2) repeated, t | 3; the sides t' / t'+1 with t' ^= 3 each row (from (t | 3) + 1); the bottom row like
// the top from (t' | 3) + 1. C2/EE7F's fill: C2:EEF5[4 (r & 3) + (c & 3)] + the base's low byte, the pattern of four
// rows repeated (C2/EF2B's MVN).
static void mRomWindowCells(const CtMenuHost* h, uint16_t* tm, const uint8_t* a, int d8C) {
  int A = (a[0] & 0x80) ? a[0] : (d8C & 7);
  int a1 = (A << 2) & 0xFF;                                   // ASL / ASL / STA $00
  int a2 = (a1 << 2) & 0xFF;                                  // ASL / ASL: the second's carry-out = bit 6 of a1
  int base = (a[1] << 8) | ((a2 + a1 + ((a1 >> 6) & 1)) & 0xFF);   // ADC $00 / STA $0D47; $0D48 = a[1]
  int x = a[2], y = a[3], w = a[4] & 0x1F, hh = a[5] & 0x3F;
  if (w < 3 || hh < 3 || !h->rom || h->romLen < 0x2EF05L) return;
#define WC(c, r) do { int cc_ = x + (c), rr_ = y + (r); \
    if (cc_ >= 0 && cc_ < CT_MENU_CHROME_TMW && rr_ >= 0 && rr_ < CT_MENU_CHROME_TMH) tm[rr_ * CT_MENU_CHROME_TMW + cc_] = (uint16_t)(v_); } while (0)
  int t = base, v_;
  for (int c = 0; c < w; c++) { v_ = c == 0 ? t : c == w - 1 ? (t | 3) : ((c & 1) ? t + 1 : t + 2); WC(c, 0); }
  t = (t | 3) + 1;                                            // C2/EE23's tail: $08 = (t | 3) + 1
  for (int r = 1; r < hh - 1; r++) {                          // C2/EE58
    v_ = t; WC(0, r); v_ = t + 1; WC(w - 1, r);
    t = (t + 1) ^ 3;
  }
  t = (t | 3) + 1;
  for (int c = 0; c < w; c++) { v_ = c == 0 ? t : c == w - 1 ? (t | 3) : ((c & 1) ? t + 1 : t + 2); WC(c, hh - 1); }
  for (int r = 1; r < hh - 1; r++)                            // C2/EE7F + C2/EF2B
    for (int c = 1; c < w - 1; c++) {
      v_ = (base & 0xFF00) | ((CtRomByte(h->rom, 0x400000L, (long)(0x2EEF5L + 4 * ((r - 1) & 3) + ((c - 1) & 3))) + (base & 0xFF)) & 0xFF);
      WC(c, r);
    }
#undef WC
}
static int mSavePromptOn = 0, svLastCursor = -1, mSaveHoverPanel = -1;
void CtMenuSavePromptUp(int on) { mSavePromptOn = on; }
// B off the Save screen (C2/9A98 -> C2/9B71 -> C2:9B83[5] = C2/D0DB) only sets $0D13 = 19: nothing is redrawn, so the
// ribbon's Save preview is the screen's last picture -- the rows (bright: the preview's HDMA has no band channel), no
// hand, and the summary panel of the slot the cursor left. A preview built by the ribbon (its row loop C2/D10D leaves
// `$79` = 3, which C2/D36C draws as no panel) has none. `slot` = that panel's slot, -1 = a built preview.
void CtMenuSetSaveHoverPanel(int slot) { mSaveHoverPanel = slot; }
void CtMenuRenderSavePrompt(const CtMenuHost* h, uint8_t* fb, int sel) {
  if (!h || !fb || (sel != 3 && sel != 4)) return;
  // $FF:C16F through C2/ED31 (C2/D4BB): op 08 (the BG2 buffer $3E00), op 02 (the backup of (2, 2) 28 x 12), op 04,
  // the window. It is BG2, like the Save chrome, so it takes the chrome's ramp and, on the non-cursor rows' lines, the
  // band's half colour math (C2/D4BB leaves the band where the list's cursor put it); its cells are the window
  // colour's (C2/C511 / C2/ED08).
  static uint16_t tm[CT_MENU_CHROME_TMW * CT_MENU_CHROME_TMH];
  memset(tm, 0, sizeof tm);
  if (h->rom && h->romLen > 0x3FC16FL + 17 && CtRomByte(h->rom, 0x400000L, (long)(0x3FC16FL + 10)) == 0x04)
    mRomWindowCells(h, tm, CtRomPtr(h->rom, h->romLen, 0x3FC16FL + 11, 6), h->mem ? h->mem[0x0D8C] : 0);
  memset(mHalfBand, 0, sizeof mHalfBand);
  if (svLastCursor >= 0)
    for (int i = 0; i < CT_MENU_SAVE_SLOTS; i++)
      if (i != svLastCursor) for (int yy = 16 + i * 32; yy < 48 + i * 32; yy++) mHalfBand[yy] = 1;
  // ...but not under the prompt: C2/D4BB sets $0D13 = 75 (C2/D4D5 puts back 59), switching on HDMA channel 5 ->
  // $2129 (window 2's right edge) from FF/BAE3's table (`10 00 50 EF 01 00`: lines 16-95 = 0xEF), and the Save screen's
  // register record (FF:BCE6 = $2123..$212F: W1 16..239, W2 left FF:BCEB = 0xB0, $212B = 08 -> the colour window is W1
  // XOR W2) takes x 176..239 of those lines out of the colour math, the ramp and the band.
  int w2On = mRibbonW2On; int16_t w2Save[MH][2]; memcpy(w2Save, mRibbonW2, sizeof w2Save);
  if (h->rom && h->romLen > 0x3FBCEBL) {
    for (int y = 0; y < MH; y++) { mRibbonW2[y][0] = 1; mRibbonW2[y][1] = 0; }
    long t = 0x3FBAE3L; int line = 0;
    for (int g = 0; g < 16 && CtRomByte(h->rom, 0x400000L, (long)(t)) && line < MH; g++, t += 2) {
      int n = CtRomByte(h->rom, 0x400000L, (long)(t)) & 0x7F, r = CtRomByte(h->rom, 0x400000L, (long)(t + 1));
      for (int k = 0; k < n && line < MH; k++, line++) { mRibbonW2[line][0] = CtRomByte(h->rom, 0x400000L, (long)(0x3FBCEBL)); mRibbonW2[line][1] = (int16_t)r; }
    }
    mRibbonW2On = 1;
  }
  mChromeTMPal2L(fb, tm, 2, ctMenuChromeSavePal, mWinGrad(5), 0);
  mRibbonW2On = w2On; memcpy(mRibbonW2, w2Save, sizeof w2Save);
  memset(mHalfBand, 0, sizeof mHalfBand);
  mText(h, fb, "Erase", 188, 28);
  mText(h, fb, "old",   188, 40);
  mText(h, fb, "save?", 188, 52);
  mText(h, fb, "Yes",   200, 64);
  mText(h, fb, "No",    200, 80);
  mHandCursor(h, fb, 184, (sel == 3) ? 63 : 79);       // $7E:9380 records 3 / 4
}

// Forward declarations: both live with the boot Load screen further down, and this screen draws the same glyphs.
static void mBlit16(uint8_t* fb, const uint32_t blob[16][16], int x0, int y0);
static void mGlyph(const CtMenuHost* h, uint8_t* fb, int code, int x, int y);
// The Save screen's VWF fields. `C2/D296` (slot rows) and `C2/D3AD` (the era caption) render through the ROM's
// variable-width font: C2/F90C -> the C2/5DC4 glyph emitter, whose per-glyph advance is the byte at C2/60E6 + (code -
// 0xA0):
//     25E21 SEC / LDA $35 / SBC #A0 / TAX / 25E27 CLC / LDA $C260E6,X / ADC $34 / STA $34
// over the 24-byte 12 px glyphs at C2/2060 + code*24 (CT_ASSET_FONT carries the same table). The strings from
// CtMenuLoaderChapter / CtMenuLoaderEra are the ROM's pre-padded ones, and the padding is the centring, so they are
// drawn left-aligned at the field (spaces advance 4 px).
static void svVwf(const CtMenuHost* h, uint8_t* fb, const char* s, int x0, int y) {
  if (!s || !*s) return;
  if (h->vwfWidth && h->vwfDraw) { h->vwfDraw(fb, s, x0, y, ctLoaderVwfTone); return; }
  mText(h, fb, s, x0, y);        // no VWF on this host -> the 8x8 menu font
}
static uint8_t svSnap[MH * MW * 3];
void CtMenuRenderSave(const CtMenuHost* h, uint8_t* fb, int cursor) {
  if (!h || !fb) return;
  svLastCursor = cursor;
  int panelSlot = cursor >= 0 ? cursor : mSaveHoverPanel;   // the summary panel's slot (the hover's: the one left)
  // the background (the BG2 chrome) is laid by the caller (CtMenuChrome)
  // cursor < 0 = the ribbon's hover preview: every row bright, no hand
  if (cursor >= CT_MENU_SAVE_SLOTS) cursor = CT_MENU_SAVE_SLOTS - 1;
  // The two non-cursor slot rows are in the ROM's windowed half colour math band, which it moves with the cursor
  // (`math=07 half=1` on the non-cursor rows' 32 lines, W1 = 16..239), so the chrome is laid again with the band
  // (mChromeTMPal2L), and the text below takes the same math.
  if (cursor >= 0) {
    memset(mHalfBand, 0, sizeof mHalfBand);
    for (int i = 0; i < CT_MENU_SAVE_SLOTS; i++)
      if (i != cursor) for (int yy = 16 + i * 32; yy < 48 + i * 32; yy++) mHalfBand[yy] = 1;
    CtMenuChrome(fb, 6, 5, 0);
    memset(mHalfBand, 0, sizeof mHalfBand);
    memcpy(svSnap, fb, sizeof svSnap);
  }
  // The three slot buttons are the BG2 chrome (beveled tiles 0x300-0x313 at tile rows 2-5/6-9/10-13 = y 8/40/72, each
  // 32 px tall, cols 2-29), laid by CtMenuChrome; only the per-slot text is drawn here: the slot number and the chapter
  // title or "No data". The bottom panels are the cursor slot's, as on the boot Load screen, and stay blank while the
  // cursor sits on an empty one.
  CtMenuSaveSummary cur; int curUsed = 0; memset(&cur, 0, sizeof cur);
  for (int i = 0; i < CT_MENU_SAVE_SLOTS; i++) {
    int y = 16 + i * 32;                                  // slot i button top = BG2 tile rows 2/6/10 (y16/48/80)
    mDim = 4;                                             // Save text = the entered-Save BG3 palette 4 (231 body)
    char num[2] = { (char)('1' + i), 0 };
    // The slot number is VWF too, centred in the button's first four tiles, x[16,48), with the expression the row text
    // uses in x[80,208) (the advances are C2/60E6's).
    { int nw = h->vwfWidth ? h->vwfWidth(num) : (int)strlen(num) * 8;
      svVwf(h, fb, num, 16 + (32 - nw) / 2, y + 12); }
    CtMenuSaveSummary s;
    // The slot row is the chapter title: `C2/D26D-D288` reads the save file's storyline byte (slot+0x0603 = record 4 =
    // $7F:0000), walks the threshold table FF/D024 for the largest index whose entry it is >=, and draws string
    // FF/D03E[X] (index 0x1A = "No data"), the pair CtMenuLoaderChapter ports for the boot Load screen. `C2/D296`
    // renders it through the VWF (C2/F90C) into the buffer at $0DC5, at y+12, in the x[80,208) field.
    int used = CtMenuSaveSlot(h, i, &s) && CtMenuSaveSlotValid(h->sram, h->sramLen, i);
    int story = used ? h->sram[CtMenuSaveSlotBase(i) + 0x0603] : 0;   // C2/D276 LDA $300603,X
    svVwf(h, fb, CtMenuLoaderChapter(story, used), 80, y + 12);
    if (used && i == panelSlot) { cur = s; curUsed = 1; }
    mDim = 0;
    // The hand's ink box lands at (x8..23, y31..42 + 32*i): the sprite's opaque pixels sit at local (x0..15, y2..13)
    // in the FF/9CF0 tile, so the top left is (MI_HX, y+13) (menu_open_items.md 4.2).
    if (cursor >= 0 && i == cursor && !mSavePromptOn) mHandCursor(h, fb, MI_HX, y + 13);   // (the prompt holds the
                                                                                            //  nav hand: $54 = 3 / 4)
  }
  // The two bottom panels: the fields the boot Load screen renders (CtMenuRenderLoader), from the same save-file
  // offsets and x, nine pixels higher:
  //            era      TIME     gold     Save #   party row 0/1/2
  //   Load     149      169      185      201      145 / 169 / 193
  //   Save     140      160      176      192      136 / 160 / 184
  // The era caption is C2/D3AD: $98C2 = savefile+0x05F3 & 0x1FF -> the FF/D296 nibble table -> FF/D396[era]
  // (CtMenuLoaderEra), pre-padded for the x[32,104) field.
  if (h->sramPanel && panelSlot >= 0) {       // the panel C2/D32C last drew: after a save it is a frame behind the rows
    CtMenuHost hp = *h; hp.sram = h->sramPanel;
    memset(&cur, 0, sizeof cur);
    curUsed = CtMenuSaveSlot(&hp, panelSlot, &cur) && CtMenuSaveSlotValid(hp.sram, hp.sramLen, panelSlot);
  }
  if (curUsed) {
    mDim = 0;
    svVwf(h, fb, CtMenuLoaderEra(cur.locId), 32, 140);
    mText(h, fb, "TIME", 24, 160);
    { char t[3]; t[2] = 0;
      t[0] = (char)('0' + cur.hh / 10); t[1] = (char)('0' + cur.hh % 10);
      mText(h, fb, t, 64, 160);
      mGlyph(h, fb, 0x2D, 80, 160);                                  // the ROM's ':' separator tile
      t[0] = (char)('0' + cur.mm / 10); t[1] = (char)('0' + cur.mm % 10);
      mText(h, fb, t, 88, 160); }
    mNumR(h, fb, cur.gold, 96, 176);
    mText(h, fb, "G", 96, 176);
    mText(h, fb, "Save", 24, 192);
    mGlyph(h, fb, 0xEE, 64, 192);                                    // the ROM's '#' tile
    mNumR(h, fb, cur.saveNo, 104, 192);
    // ...and the saved party (C2/D1C1 reads slot+0x0580, C2/D1D3 the sheet at slot+0x0200 + id*0x50).
    { const uint8_t* sr = h->sramPanel ? h->sramPanel : h->sram; unsigned base = CtMenuSaveSlotBase(panelSlot);
      static const int PY[3] = { 136, 160, 184 };
      for (int j = 0; j < 3; j++) {
        int id = sr[base + 0x0580 + j];
        if (id & 0x80) continue;
        const uint8_t* sheet = sr + base + 0x0200 + (size_t)(id & 7) * 0x50;
        int py = PY[j];
        mBlit16(fb, ctLoaderIcon[id & 7], 120, py);
        mText(h, fb, CtGetName(id & 7), 144, py);
        mText(h, fb, "LV", 192, py);       mStatCap(h, fb, sheet[0x12], 7, 232, py);   // string 17: cap 7
        mText(h, fb, "HP", 144, py + 8);   mNumR(h, fb, sheet[3] | (sheet[4] << 8), 184, py + 8);
        mText(h, fb, "MP", 192, py + 8);   mNumR(h, fb, sheet[7] | (sheet[8] << 8), 232, py + 8);
      } }
  }
  // The band's text: `math=07` has BG3 in the mask, so a non-cursor row's text takes the fixed colour (the list's
  // ramp, mWinGrad(5)) before the halve, in the Load screen's order (clamp at 0, halve, cap). The text is what differs
  // from the chrome laid above.
  if (cursor >= 0) {
    const int8_t* d5 = mWinGrad(5);
    for (int i = 0; i < CT_MENU_SAVE_SLOTS; i++) {
      if (i == cursor) continue;
      for (int yy = 16 + i * 32; yy < 48 + i * 32; yy++)
        for (int xx = 16; xx <= 239; xx++) {
          size_t o = ((size_t)yy * MW + xx) * 3;
          if (!memcmp(fb + o, svSnap + o, 3)) continue;
          for (int c = 0; c < 3; c++) {
            int v = (fb[o + c] >> 3) + d5[yy];
            if (v < 0) v = 0;
            v >>= 1;
            if (v > 31) v = 31;
            fb[o + c] = (uint8_t)((v << 3) | (v >> 2));
          }
        }
    }
  }
}

// ---- The load screen: menu mode 0, screen $0E (C2/E60B, builder C2/E7C3; disasm/notes/boot_loader.md) ----
//   rows            cursor $0054 = 0..2 the three slot buttons, 3 = "New Game". The buttons are BG2 tile rows 2-5 /
//                   6-9 / 10-13 (y16/48/80, 32 px each) and "New Game" is the BG3 text row above them; the two
//                   bottom windows are rows 16-25.
//   occupancy       $0D49+row: nonzero = the row may hold the cursor. Slots come from the FF/F9C4 gate (C2/D10D's
//                   per-slot scan); $0D4C, the New Game row, is 0xFF except on the New-Game+ path (C2/E67A `STZ
//                   $0D4C`). Not ported: the New-Game+ path.
//   cursor settle   C2/E743: while the row under $0054 is empty, step (DEX, or INX INX DEX = +1 when $005A bit 3
//                   says the press was Up) and AND #03, so the cursor skips empty slots.
//   default row     C2/E7CF: $79 = $30:7FE0, the last slot saved; if all three slots are empty ($0D49|$0D4A|$0D4B ==
//                   0) $79 = 3 = New Game. Then $0054 = $79.
//   chapter / era   the two ROM string tables baked in menu_loader.h (citations there).

// C2/E7C3's occupancy scan, as the four bytes the ROM keeps at $0D49..$0D4C. Returns the number of occupied save
// slots (0..3).
int CtMenuLoaderOccupancy(const CtMenuHost* h, unsigned char* occ) {
  int n = 0;
  for (int s = 0; s < CT_MENU_SAVE_SLOTS; s++) {
    int used = h && CtMenuSaveSlotValid(h->sram, h->sramLen, s);
    if (occ) occ[s] = (unsigned char)(used ? 1 : 0);
    n += used;
  }
  if (occ) occ[3] = 0xFF;                    // $0D4C: "New Game" is always selectable here
  return n;
}

// ---- The screen's node graph ($7E:9380) ----
// C2/EA27 turns the row into the node offset (`AND #00FF / ASL / PHA / ASL / ADC $01,S / TAX` = row*6), so nodes are
// six bytes: +0 the hand's OAM X, +1 its OAM Y, then the four neighbours the direction dispatch reads (C2/EA36 `LDA
// $9382,Y` (+2), EA3B (+3), EA40 (+4), EA45 (+5)). C2/E9E5 picks the direction from the lowest set bit of $F5 (the
// new-press d-pad byte) -- Right 0x01 -> 0, Left 0x02 -> 1, Down 0x04 -> 2, Up 0x08 -> 3 -- and the jump table at
// $C2:EA1F (`45 EA 40 EA 3B EA 36 EA`) maps 0->+5, 1->+4, 2->+3, 3->+2. So +2 is Up and +3 is Down:
//   node0  slot 0     X 8  Y  53   Up 3  Dn 1  Lf 0  Rt 0
//   node1  slot 1     X 8  Y  85   Up 0  Dn 2  Lf 1  Rt 1
//   node2  slot 2     X 8  Y 117   Up 1  Dn 3  Lf 2  Rt 2
//   node3  New Game   X 8  Y  21   Up 2  Dn 0  Lf 3  Rt 3
//   (nodes 4/5 are the New-Game+ prompt pair, (184,87)/(184,103), not ported)
// A four-node vertical ring: New Game is the row above slot 0, and Up/Down wrap through it. Left/Right stay put.
static const unsigned char CT_LOADER_NODE[4][6] = {
  { 8,  53, 3, 1, 0, 0 },
  { 8,  85, 0, 2, 1, 1 },
  { 8, 117, 1, 3, 2, 2 },
  { 8,  21, 2, 0, 3, 3 },
};
// The neighbour a direction press moves to. `dir`: 0 Right, 1 Left, 2 Down, 3 Up (the ROM's index from the $F5
// scan); the node byte is +5-dir.
int CtMenuLoaderNeighbour(int row, int dir) {
  if (row < 0 || row > 3 || dir < 0 || dir > 3) return row;
  return CT_LOADER_NODE[row][5 - dir] & 3;
}
// The hand's OAM position for a row, from the node (+0 X, +1 Y).
void CtMenuLoaderHandOam(int row, int* x, int* y) {
  if (row < 0 || row > 3) row = 0;
  if (x) *x = CT_LOADER_NODE[row][0];
  if (y) *y = CT_LOADER_NODE[row][1];
}

// C2/E743, the cursor settle: it runs after the graph move and walks off any empty row.
//     E746 LDX $54 / LDA $0D49,X / BNE -> done
//     E74D LDA $5A / AND #08 / BNE ->E755      ; the press was Up -> skip the INX INX
//     E753 INX / INX
//     E755 DEX / TXA / AND #03 / STA $54 / BRA ->E746
// So the step is -1 on Up and +1 otherwise; `up` is $5A bit 3, the byte C2/E9F2 latched from $F5.
int CtMenuLoaderStep(const CtMenuHost* h, int row, int up) {
  unsigned char occ[4];
  CtMenuLoaderOccupancy(h, occ);
  int x = row & 3;
  for (int guard = 0; guard < 8; guard++) {            // the ROM's loop is unbounded; $0D4C != 0
    if (occ[x]) return x;                              //   guarantees it terminates within 4 steps
    x = up ? ((x + 3) & 3) : ((x + 1) & 3);            // E753-E755: DEX on Up, INX INX DEX otherwise
  }
  return 3;
}

// C2/E7CF-E7E6: the row the screen opens on.
int CtMenuLoaderDefaultRow(const CtMenuHost* h) {
  unsigned char occ[4];
  int n = CtMenuLoaderOccupancy(h, occ);
  if (!n) return 3;                          // E7DE BNE / E7E0 LDA #03: nothing to load
  int last = (h && h->sram && h->sramLen >= 0x2000) ? h->sram[0x1FE0] : 0;   // E7CF LDA $307FE0
  if (last < 0 || last > 3) last = 0;
  return occ[last] ? last : CtMenuLoaderStep(h, last, 0);
}

// The chapter title a slot shows: C2/D266's storyline -> C2/D27C's threshold walk. `storyline` is the slot's own
// $+0x0603 byte. An empty slot takes C2/D28D's arm -> index 26 = "No data".
const char* CtMenuLoaderChapter(int storyline, int used) {
  if (!used) return ctLoaderChapter[CT_LOADER_CHAPTERS - 1];   // C2/D28D LDA #1A
  int x = 26;                                                  // C2/D27C LDX #1A
  do { x--; } while (x > 0 && storyline < ctLoaderChapThr[x]);  // C2/D27F CMP / D283 BCC
  return ctLoaderChapter[x];
}
// The era name for a location id (C2/D3AD's nibble table + C2/D3D4's pointer table).
const char* CtMenuLoaderEra(int locId) {
  return ctLoaderEra[ctLoaderEraOf[locId & 0x1FF] & 7];
}

// ---- The per-line chrome composer (disasm/notes/boot_loader.md) ----
// The other menu screens replay their BG2 plane flat (tile row R at y = R*8). This screen drives BG2VOFS ($2110) per
// line in three bands, so the map line a scanline shows is `(y + vofs(y)) & 0x1FF` over the 32x64 map, and four bars
// come out of three button blocks:
//   band 1  y   0.. 39  vofs 0x00FF  -> the second screen's rows 1-4 = the "New Game" bar, y9..40
//   band 2  y  40..135  vofs 0x01E7  -> first-screen rows 2-5 / 6-9 / 10-13 = the three save slots
//                                       at y41..72 / y73..104 / y105..136
//   band 3  y 136..215  vofs 0x01F7  -> rows 16-25 = the two bottom windows, y137..216
// The colour math is the window-gradient law (C2/86DD: +8 at a window's top down to -7 at its bottom, 16 steps,
// normalised per window) over this screen's partition, not the X menu's ribbon list:
//   lines   0..  8   math=00                     no colour math
//   lines   9.. 40   fixed 8,7,6..0 then 1..7     one step every 2 lines   (the New Game row)
//   lines  41.. 72 / 73..104 / 105..136           the same ramp            (slots 0, 1, 2)
//   lines 137..216   the same ramp in 5-line steps                         (the two bottom windows)
// The non-cursor rows run `math=07 half=1` (the same gradient with the halve, BG1/BG3 also in the mask), the cursor's
// row and the bottom windows `math=02 half=0`. So the gradient is composed here and the halve applied over the whole
// band afterwards, where the text (BG1/BG3) picks it up too.
static uint8_t ldrSnap[MW * MH * 3];   /* the chrome as composed, so the text's halve can be told apart */
#define CT_LDR_WIN0   9      /* the first row's top; the four rows are 32 lines each */
#define CT_LDR_WINBOT 137    /* the two bottom windows, 80 lines, one band */
static int ldrGrad(int y) {
  int y0, h;
  if (y < CT_LDR_WIN0) return 0;                       // lines 0..8: math=00, no math at all
  if (y < CT_LDR_WINBOT) { y0 = CT_LDR_WIN0 + ((y - CT_LDR_WIN0) / 32) * 32; h = 32; }
  else                   { y0 = CT_LDR_WINBOT; h = 80; }
  int k = ((y - y0) * 16) / h;                         // 16 steps across the window's own height
  if (k > 15) k = 15;
  return 8 - k;                                        // +8 at the top .. -7 at the bottom
}
// The visual row a scanline belongs to, as a cursor row (3 = New Game on top, then slots 0/1/2), or -1 for the bottom
// windows and the dead lines above the list.
static int ldrRowOfLine(int y) {
  if (y < CT_LDR_WIN0 || y >= CT_LDR_WINBOT) return -1;
  int r = (y - CT_LDR_WIN0) / 32;                       // 0 = the New Game bar
  return r ? r - 1 : 3;
}
static void mChromeTMScroll(uint8_t* fb, const uint16_t* tm, const uint8_t (*pal)[16][3],
                            int cursor) {
  memset(fb, 0, (size_t)MW * MH * 3);                  // index 0 / unmapped tiles = backdrop black
  for (int py = 0; py < MH; py++) {
    int v = ctMenuLoaderBand[CT_MENU_LOADER_NBAND - 1].vofs;   // past the table the register holds
    for (int b = 0; b < CT_MENU_LOADER_NBAND; b++)
      if (py >= ctMenuLoaderBand[b].y0 && py <= ctMenuLoaderBand[b].y1) { v = ctMenuLoaderBand[b].vofs; break; }
    int ml = (py + v) & 0x1FF;                         // 32x64 map = 512 lines, second screen at +32 rows
    int dim = ldrRowOfLine(py) >= 0 && ldrRowOfLine(py) != cursor;   // `math=07 half=1`
    const uint16_t* row = tm + (ml >> 3) * 32;
    int sub = ml & 7;
    for (int tx = 0; tx < 32; tx++) {
      uint16_t e = row[tx];
      int tile = e & 0x3FF, pl = (e >> 10) & 7;
      int ti = tile - CT_MENU_CHROME_TILE0;
      if (ti < 0 || ti >= CT_MENU_CHROME_NTILE) continue;
      const uint8_t* g = mSlotGlyph(pal, ti, pl);     // a saved slot in its own colour (C2/D266 $0D79 -> C2/ECDB)
      if (!g) g = ctMenuChromeTile[ti];
      const uint8_t (*P)[3] = pal[pl];
      int isWin = (tile >= 0x3DC && tile <= 0x3EF) || (tile >= 0x300 && tile <= 0x313);
      int gy = (e & 0x8000) ? (7 - sub) : sub;          // V flip
      for (int xx = 0; xx < 8; xx++) {
        int px = tx * 8 + xx;
        if (px >= MW) break;
        int gx = (e & 0x4000) ? (7 - xx) : xx;          // H flip
        int val = g[gy * 8 + gx];
        const uint8_t* Pv = (val >= 8) ? mChromeInk(pal, pl, val) : P[val];   // (the slot's own row, C2/ED08)
        int r = Pv[0], gg = Pv[1], b2 = Pv[2];
        if (isWin) {                                    // COLDATA carries a 5-bit intensity and
          int dd = ldrGrad(py);                         //   CGADSUB adds/subtracts it in 5 bits
          int c0 = (r >> 3) + dd, c1 = (gg >> 3) + dd, c2 = (b2 >> 3) + dd;
          // Note the order: the sum is clamped at zero, then halved when this row is not the cursor's (`math=07
          // half=1`), and only then capped at 31 -- one ALU pass.
          if (c0 < 0) c0 = 0; if (c1 < 0) c1 = 0; if (c2 < 0) c2 = 0;
          if (dim) { c0 >>= 1; c1 >>= 1; c2 >>= 1; }
          if (c0 > 31) c0 = 31; if (c1 > 31) c1 = 31; if (c2 > 31) c2 = 31;
          r = (c0 << 3) | (c0 >> 2); gg = (c1 << 3) | (c1 >> 2); b2 = (c2 << 3) | (c2 >> 2);
        }
        uint8_t* d = fb + ((size_t)py * MW + px) * 3;
        d[0] = (uint8_t)r; d[1] = (uint8_t)gg; d[2] = (uint8_t)b2;
      }
    }
  }
}

// The four rows the screen draws, in the geometry the bands above produce. Row 3 (New Game) is band 1's bar; rows
// 0-2 (the save slots) are band 2's three blocks.
#define CT_LDR_ROW_H 32
static int ldrRowTop(int row) { return (row == 3) ? 9 : 41 + 32 * (row & 3); }

// The screen's BG2 chrome: the same tiles and gradient as the other menu screens, through this screen's per-line
// scroll.
void CtMenuChromeLoaderAt(uint8_t* fb, int cursor) {
  if (!fb) return;
  mChromeTMScroll(fb, ctMenuChromeLoaderTM, ctMenuChromeLoaderPal, cursor);
}

// A 16x16 RGB blob (the face icons and the big slot numerals). CT_LOADER_ICON_TRANSPARENT marks
// palette index 0, which the PPU leaves showing whatever is underneath.
static void mBlit16(uint8_t* fb, const uint32_t blob[16][16], int x0, int y0) {
  for (int y = 0; y < 16; y++) {
    int py = y0 + y;
    if (py < 0 || py >= MH) continue;
    for (int x = 0; x < 16; x++) {
      uint32_t c = blob[y][x];
      int px = x0 + x;
      if (c == CT_LOADER_ICON_TRANSPARENT || px < 0 || px >= MW) continue;
      uint8_t* d = fb + ((size_t)py * MW + px) * 3;
      d[0] = (uint8_t)(c >> 16); d[1] = (uint8_t)(c >> 8); d[2] = (uint8_t)c;
    }
  }
}

// Draw one glyph by its raw CT font index. Two cells on this screen do not use the code the ASCII mapping would pick:
// the clock's separator is tile 0x2D (not ':' = 0xE3) and the Save line's hash is 0xEE (not '#' = 0xF3), as in the
// BG1 tilemap `0B3 0A8 0AC 0A4 000 0D4 0DC 02D 0D8 0D7` and `0B2 0BA 0CF 0BE 0EF 0EE 0EF ...`.
static void mGlyph(const CtMenuHost* h, uint8_t* fb, int code, int x, int y) {
  if (h->drawGlyph) h->drawGlyph(fb, code, x, y);
}

// The two proportional fields, the slot description and the era name, in the dialogue VWF's glyphs and advances. They
// are not centred: the pen starts at the block's left edge and the ROM string's own leading spaces position it
// ('  The Queen is Gone', 2 spaces of 4 px, inks at x8), so the strings are baked verbatim. The glyph top sits 4 px
// into the 16 px block. Falls back to the 8x8 menu font when the host has no VWF.
static void ldrVwf(const CtMenuHost* h, uint8_t* fb, const char* s, int x0, int ncols, int y) {
  if (!h->vwfWidth || !h->vwfDraw) { mText(h, fb, s, x0, y); return; }
  (void)ncols;
  h->vwfDraw(fb, s, x0, y, ctLoaderVwfTone);
}

// Draw the load screen. `cursor` 0..2 = a save slot, 3 = New Game.
// Every y below is a BG map row put through its layer's own band table; the text layers are scrolled too, by other
// amounts than BG2 ($210E BG1VOFS: 40 lines @ $FFFF then $01F7; $2112 BG3VOFS: 40 @ $01F1, 96 @ $01E7, 80 @ $01F7).
// Inverting `map_line = (y + vofs) & 0x1FF` for the row a field sits on gives:
//   BG3 row  1  ("New Game")      band 1, vofs 497 -> y  23
//   BG3 rows 3+4i / 4+4i (slots)  band 2, vofs 487 -> y  49 + 32i  and +8
//   BG3 rows 17,18 (era)          band 3, vofs 503 -> y 145, 153
//   BG1 rows 20 / 22 / 24         band 3, vofs 503 -> y 169 / 185 / 201   (TIME / gold / Save #)
//   BG1 rows 17,18 / 20,21 / 23,24 (party)          -> y 145,153 / 169,177 / 193,201
// The slot descriptions and the era go through the VWF (C2/EF65 in the ROM; ldrVwf here).
void CtMenuRenderLoader(const CtMenuHost* h, uint8_t* fb, int cursor) {
  if (!h || !fb) return;
  if (cursor < 0) cursor = 0; else if (cursor > 3) cursor = 3;
  // The boot Load screen's rows are the Save screen's: C2/D266 reads each valid save's colour into $0D79 and C2/D329
  // draws the slot in it (tiles 0x300 + 20 v, row slot + 1).
  CtMenuSaveSlotColours(h, NULL);
  CtMenuChromeLoaderAt(fb, cursor);        // the chrome finishes its own colour math per line
  memcpy(ldrSnap, fb, sizeof ldrSnap);     // ...so only what the text writes still needs halving
  mDim = 0;
  mText(h, fb, "New Game", 104, 23);                  // BG3 row 1, cols 13-20
  CtMenuSaveSummary cur; int curUsed = 0; memset(&cur, 0, sizeof cur);
  for (int i = 0; i < CT_MENU_SAVE_SLOTS; i++) {
    // BG3 rows 3 / 7 / 11 inverted through BG3's band 2 (vofs 487): (24 - 487) & 0x1FF = 49 (the modulo, not the
    // subtraction).
    int y = 49 + i * 32;
    mBlit16(fb, ctLoaderNumeral[i], 24, y);           // BG3 tiles at cols 3-4, rows 3-4 of the row
    CtMenuSaveSummary s;
    int used = CtMenuSaveSlot(h, i, &s) && CtMenuSaveSlotValid(h->sram, h->sramLen, i);
    int story = used ? h->sram[CtMenuSaveSlotBase(i) + 0x0603] : 0;    // C2/D276 LDA $300603,X
    ldrVwf(h, fb, CtMenuLoaderChapter(story, used), 80, 16, y + 4);   // BG3 cols 10-25, 16 tiles
    if (used && i == cursor) { cur = s; curUsed = 1; }
  }
  // The bottom-left summary window and the party window are the cursor slot's, blank while the cursor sits on New
  // Game or an empty slot.
  if (curUsed) {
    ldrVwf(h, fb, CtMenuLoaderEra(cur.locId), 32, 9, 149);            // BG3 rows 17-18, cols 4-12
    mText(h, fb, "TIME", 24, 169);                                   // BG1 row 20, cols 3-6
    { char t[3]; t[2] = 0;                                            //   cols 8-12
      t[0] = (char)('0' + cur.hh / 10); t[1] = (char)('0' + cur.hh % 10);
      mText(h, fb, t, 64, 169);
      mGlyph(h, fb, 0x2D, 80, 169);                                  //   the ROM's separator tile
      t[0] = (char)('0' + cur.mm / 10); t[1] = (char)('0' + cur.mm % 10);
      mText(h, fb, t, 88, 169); }
    mNumR(h, fb, cur.gold, 96, 185);                                 // BG1 row 22, 'G' at col 12
    mText(h, fb, "G", 96, 185);
    mText(h, fb, "Save", 24, 201);                                   // BG1 row 24, cols 3-6
    mGlyph(h, fb, 0xEE, 64, 201);                                    //   col 8, the ROM's hash tile
    mNumR(h, fb, cur.saveNo, 104, 201);                              //   number right-aligned at col 12
    // The three party panels (BG1 rows 17-18 / 20-21 / 23-24, cols 18-28).
    const uint8_t* sr = h->sram; unsigned base = CtMenuSaveSlotBase(cursor);
    static const int PY[3] = { 145, 169, 193 };
    for (int j = 0; j < 3; j++) {
      int id = sr[base + 0x0580 + j];                                // C2/D1C1: the saved party
      if (id & 0x80) continue;
      const uint8_t* sheet = sr + base + 0x0200 + (size_t)(id & 7) * 0x50;   // C2/D1D3
      int y = PY[j];
      mBlit16(fb, ctLoaderIcon[id & 7], 120, y);                     // BG1 cols 15-16
      mText(h, fb, CtGetName(id & 7), 144, y);
      mText(h, fb, "LV", 192, y);       mStatCap(h, fb, sheet[0x12], 7, 232, y);   // string 17: cap 7
      mText(h, fb, "HP", 144, y + 8);   mNumR(h, fb, sheet[3] | (sheet[4] << 8), 184, y + 8);
      mText(h, fb, "MP", 192, y + 8);   mNumR(h, fb, sheet[7] | (sheet[8] << 8), 232, y + 8);
    }
  }
  // The hand, from the node table: OAM (8, 53/85/117/21). An OBJ renders one line below its OAM Y
  // (ct-field-sprite-pos), so the top left is OAM_Y + 1.
  { int hx, hy; CtMenuLoaderHandOam(cursor, &hx, &hy); mHandCursor(h, fb, hx, hy + 1); }
  // ...and the windowed half colour math band the ROM moves with the cursor: every row but the cursor's is halved in
  // 5-bit space (as on the Save screen's slot list). All four rows are in the window; the New Game bar dims too.
  for (int i = 0; i < 4; i++) {
    if (i == cursor) continue;
    int y0 = ldrRowTop(i);
    for (int yy = y0; yy < y0 + CT_LDR_ROW_H && yy < MH; yy++)
      for (int xx = 16; xx <= 239 && xx < MW; xx++) {
        size_t o = ((size_t)yy * MW + xx) * 3;
        if (!memcmp(fb + o, ldrSnap + o, 3)) continue;      // chrome: its math is done above
        // `math=07` puts BG1/BG3 in the mask too, so the text on a non-cursor row takes the same gradient before the
        // halve, in the chrome's order: clamp at 0, halve, then cap at 31.
        for (int c = 0; c < 3; c++) {
          int v = (fb[o + c] >> 3) + ldrGrad(yy);
          if (v < 0) v = 0;
          v >>= 1;
          if (v > 31) v = 31;
          fb[o + c] = (uint8_t)((v << 3) | (v >> 2));
        }
      }
  }
}

// ---- The shop: menu mode 2, screen $0C (disasm/notes/shop_screen.md) ----
// Opened by event op 0xC8 $80-$BF (C0/0D09: X = operand & $3F -> $0A01 -> $71 = the shop number). The substate table
// C2/D519 is { D52B build, D546 Buy/Sell, D58B buy list, D645 sell list, D690 sell qty, D618 buy qty, D6C3/D715/D778
// the equip sub-mode }. Field names are the ROM's RAM (see CtMenuShop in menu.h). The item data the ROM builds at
// every menu open (C2/98B7: $7700 flags, $7800 price, $7A00 equip mask, for ids 0..$F1) is read from the record it
// copies (C2/88AD).
static const uint8_t kShopClassLim[4]   = { 0x5A, 0x7B, 0x94, 0xBC };           // C2/88A8
static const uint8_t kShopClassStart[5] = { 0x00, 0x5A, 0x7B, 0x94, 0xBC };     // C2/88A7
// C2/894F / C2/DE1D: the sheet offset of an item's class (weapon/armor/helm/accessory). The ROM indexes it with the
// class byte $04CC, and class 4 (consumables and key items) reads one byte past the four-entry table -- the `08` that
// is C2/8953's PHP -- so a consumable is compared against sheet +8 (current MP, high byte).
static const uint8_t kShopSheetOff[5]   = { 0x29, 0x28, 0x27, 0x2A, 0x08 };
static int mShopClass(int id, int* idx) {                                        // C2/8881
  int c = 0;
  while (c < 4 && (id & 0xFF) >= kShopClassLim[c]) c++;                          // 28887 CMP / BCC
  if (idx) *idx = ((id & 0xFF) - kShopClassStart[c]) & 0xFF;                     // 28892 SBC $C288A7,X
  return c;
}
// C2/88AD: the record = $CC:( FF/CF0E[class] + index * FF/CF18[class] ). -1 without a ROM.
static long mShopItemRec(const uint8_t* rom, long romLen, int id) {
  if (!rom || romLen < 0x400000) return -1;
  int idx, c = mShopClass(id, &idx);
  long base = CtRomByte(rom, 0x400000L, (long)(0x3FCF0E + c * 2)) | (CtRomByte(rom, 0x400000L, (long)(0x3FCF0E + c * 2 + 1)) << 8);        // 288CA LDA $FFCF0E,X
  long stride = CtRomByte(rom, 0x400000L, (long)(0x3FCF18 + c));                                               // 288BD LDA $FFCF18,X
  return 0x0C0000L + ((base + idx * stride) & 0xFFFF);                           // 288CE ADC $4216
}
static int mShopRawPrice(const uint8_t* rom, long romLen, int id) {             // $7800[id*2] (C2/98DD)
  long a = mShopItemRec(rom, romLen, id);
  return a < 0 ? 0 : (CtRomByte(rom, 0x400000L, (long)(a + 1)) | (CtRomByte(rom, 0x400000L, (long)(a + 2)) << 8));
}
static int mShopFlags(const uint8_t* rom, long romLen, int id) {                // $7700[id] (C2/98C0 AND #1E)
  long a = mShopItemRec(rom, romLen, id);
  return a < 0 ? 0 : (CtRomByte(rom, 0x400000L, (long)(a)) & 0x1E);
}
static int mShopEqMask(const uint8_t* rom, long romLen, int id) {               // $7A00[id] (C2/98C9 +3)
  if ((id & 0xFF) >= 0xBC) return 0;                                             // C2/9906 STZ $7ABC + a 0x34-byte
  long a = mShopItemRec(rom, romLen, id);                                        //  MVN: ids BC..F1 equip no one
  return a < 0 ? 0 : CtRomByte(rom, 0x400000L, (long)(a + 3));                                                 //  (measured: Tonic, $1811 stays 0)
}
// C2/DB9F: price = ceil(P * M / 256) with M = word $7F:01CD (0x0100 in every game state), >= 1, and 65000 when
// the product reaches 2^24. `INC $3F` is a 16-bit INC, so ceil() of 0xFFFF.xx wraps to 0, and then becomes 1.
int CtMenuShopBuyPrice(const uint8_t* rom, long romLen, const uint8_t* glb, int id) {
  uint32_t p = (uint32_t)mShopRawPrice(rom, romLen, id);
  uint32_t m = glb ? (uint32_t)(glb[0x1CD] | (glb[0x1CE] << 8)) : 0x100u;       // C2/DBA2 LDA $7F01CD
  uint32_t q = p * m;                                                            // C2/FD58 -> $3E..$41
  if ((q >> 24) & 0xFF) return 65000;                                            // C2/DBC5 LDA $41 / BNE -> $FDE8
  uint32_t r = (q >> 8) & 0xFFFF;
  if (q & 0xFF) r = (r + 1) & 0xFFFF;                                            // C2/DBD0 INC $3F (16-bit)
  if (!r) r = 1;                                                                 // C2/DBDC BNE / INC A
  return (int)r;
}
int CtMenuShopSellPrice(const uint8_t* rom, long romLen, int id) {              // C2/DF51 LSR / ADC $51
  int p = mShopRawPrice(rom, romLen, id);                                        //   (the raw price: no $01CD)
  return ((p >> 1) + (p & 1)) & 0xFFFF;
}
static uint32_t mShopGold(const uint8_t* mem) {
  return (uint32_t)mem[0x2C53] | ((uint32_t)mem[0x2C54] << 8) | ((uint32_t)mem[0x2C55] << 16);
}
static void mShopGoldClamp(uint8_t* mem) {                                       // C2/FDDF: gold <= 9,999,999
  int hi = mem[0x2C55], lo = mem[0x2C53] | (mem[0x2C54] << 8);
  if (hi < 0x98) return;                                                         // 2FDED BCS / PLP
  if (hi == 0x98 && lo < 0x967F) return;                                         // 2FDF4 CMP #967F / BCC
  mem[0x2C53] = 0x7F; mem[0x2C54] = 0x96; mem[0x2C55] = 0x98;                    // 2FDF9
}
static void mShopGoldSub(uint8_t* mem, uint32_t t) {                             // C2/FDBB
  int lo = (mem[0x2C53] | (mem[0x2C54] << 8)) - (int)(t & 0xFFFF);
  int borrow = lo < 0; lo &= 0xFFFF;
  int hi = mem[0x2C55] - (int)((t >> 16) & 0xFF) - borrow;                     // 2FDCD SBC $1046
  mem[0x2C53] = (uint8_t)lo; mem[0x2C54] = (uint8_t)(lo >> 8); mem[0x2C55] = (uint8_t)hi;
  if (hi < 0) mem[0x2C55] = 0x98;                                                // 2FDD3 BCS / LDA #98
  mShopGoldClamp(mem);
}
static void mShopGoldAdd(uint8_t* mem, uint32_t t) {                             // C2/FD97
  int lo = (mem[0x2C53] | (mem[0x2C54] << 8)) + (int)(t & 0xFFFF);
  int carry = lo > 0xFFFF; lo &= 0xFFFF;
  int hi = mem[0x2C55] + (int)((t >> 16) & 0xFF) + carry;                      // 2FDA9 ADC $1046
  mem[0x2C53] = (uint8_t)lo; mem[0x2C54] = (uint8_t)(lo >> 8); mem[0x2C55] = (uint8_t)hi;
  if (hi > 0xFF) mem[0x2C55] = 0x98;                                             // 2FDAF BCC / LDA #98
  mShopGoldClamp(mem);
}
static void mShopSfx(CtMenuShopOut* o, int id) { if (o && o->nSfx < 4) o->sfx[o->nSfx++] = (uint8_t)id; }

// ---- The text buffers (CtMenuShop.b1 / b3) and what reaches VRAM ------------------------------------------------
// The ROM writes BG1 into $7E:2E00 and BG3 into $7E:4E00 and shows a write only after a C2/8385 record queued its rows;
// the NMI (C2/85D6) runs the queue. Records (C2:xxxx = VRAM word, source, size): FBCE $5800 <- $2E00 $0C00 (rows 0-47),
// FBE3 $5840 <- $2E80 $0640 (rows 2-26), FBF1 $5B00 <- $3400 $0600 (rows 24-47), FC1B $6800 <- $4E00 $0680 (BG3 0-25).
enum { SHOP_UP_FBE3 = 1, SHOP_UP_FBF1 = 2, SHOP_UP_FC1B = 4, SHOP_UP_FBCE = 8 };
static void mShopB1(CtMenuShop* s, int row, int col, int word) {
  if (row >= 0 && row < CT_SHOP_BG1_ROWS && col >= 0 && col < 32) s->b1[row][col] = (uint16_t)word;
}
static void mShopB1Attr(CtMenuShop* s, int row, int col, int attr) {           // the attr byte alone (STA $0001,X)
  if (row >= 0 && row < CT_SHOP_BG1_ROWS && col >= 0 && col < 32)
    s->b1[row][col] = (uint16_t)((s->b1[row][col] & 0x00FF) | ((attr & 0xFF) << 8));
}
static void mShopNmi(CtMenuShop* s) {
  if (s->upQ & SHOP_UP_FBCE) memcpy(s->v1, s->b1, sizeof s->v1);
  if (s->upQ & SHOP_UP_FBE3) memcpy(s->v1[2], s->b1[2], 25 * sizeof s->v1[0]);
  if (s->upQ & (SHOP_UP_FBCE | SHOP_UP_FBE3)) {         // the list rows (5-20) go up with them: C2/DD02 redraws them in
    s->shownBuyScroll = s->buyScroll;                    //   the buffer on a page and C2/D7E7 yields; the upload is the
    s->shownSellScroll = s->sellScroll;                  //   next frame's C2/D7F1
  }
  if (s->upQ & SHOP_UP_FBF1) memcpy(s->v1[24], s->b1[24], 24 * sizeof s->v1[0]);
  if (s->upQ & SHOP_UP_FC1B) memcpy(s->v3, s->b3, sizeof s->v3);
  s->upQ = 0;
}
// C2/E058 -> C2/EC93: `n` cells of rows 24/25 from roster member `k`'s column ($3404 + 8k): attr = (attr & 0x0F) | hi;
// hi 0x10 dims a member (palette | 4, priority off), hi 0 keeps the palette's low bits and clears the priority.
static void mShopMask(CtMenuShop* s, int k, int n, int hi) {
  for (int r = 24; r <= 25; r++)
    for (int i = 0; i < n; i++) {
      int col = 2 + 4 * k + i;
      if (col < 32) mShopB1Attr(s, r, col, ((s->b1[r][col] >> 8) & 0x0F) | hi);
    }
}
// C2/DE21 + C2/DE56: the panel cells of roster member `k`. The "E" over it (tile 38, the tile byte alone) when
// $1056[$71] is set, then per row the attr 21 equal / 3D lower / 29 higher on both cells, the tens cell 0x6E + v/10 and
// the second cell v%100 (v >= 10) or 0x64 + v; attr bit 0 makes both tile numbers 0x100 higher. $4204/$4205 keep the
// dividend between the two divides, so v%100 is the remainder of the same v.
static void mShopPanel(CtMenuShop* s, int k, int eqFlag, int newAtk, int oldAtk, int newDef, int oldDef) {
  int col = 2 + 4 * k;
  if (eqFlag) mShopB1(s, 2, col, (s->b1[2][col] & 0xFF00) | 0x38);             // 2DE2D STA $2E84,Y
  for (int rr = 0; rr < 2; rr++) {
    int nv = (rr ? newDef : newAtk) & 0xFF, ov = (rr ? oldDef : oldAtk) & 0xFF;
    int attr = (nv == ov) ? 0x21 : (nv < ov) ? 0x3D : 0x29;                       // 2DE62..2DE6E
    int q = nv / 10;                                                             // $4214
    mShopB1(s, 24 + rr, col + 1, (attr << 8) | ((0x6E + q) & 0xFF));             // 2DE85 STA $3406,Y
    mShopB1(s, 24 + rr, col + 2, (attr << 8) | ((q >= 1 ? nv % 100 : 0x64 + nv) & 0xFF));   // 2DE94 STA $3408,Y
  }
}
// FF/F628: a member's sprite palette, colours 1-12 copied from bank $E4 ($02 high byte 0) or halved per channel
// ((c >> 1) & 0x3DEF, $02 high byte != 0). The port's sprite art carries the palette applied; sprDim selects the halving.
static void mShopSprPal(CtMenuShop* s, int charId, int dim) { s->sprDim[charId & 7] = (uint8_t)(dim ? 1 : 0); }
// C2/F2F3: the description line of item `id`, redrawn only when the id differs from the last one ($0D77) or a redraw is
// forced ($0D78 bit 7). C2/FA49 clears the line and starts the string task (task slot 8, C2/FA9F): its C2/0009 step runs
// later in the same frame and once per frame after, one decoded character each (a dictionary code costs one frame per
// character it expands to), and it ends on the step that reads the terminator ($0215 = 0).
static void mShopDesc(CtMenuShop* s, int id) {
  if (!(s->descForce & 0x80) && id == s->descCur) return;                        // 2F2F8 BIT $0D78 / CMP $0D77
  s->descCur = id; s->descForce = 0;                                             // STA $0D77 / STZ $0D78
  s->descN = 0; s->descRun = 1;                                                  // JSR C2/FA49
}
static int mItemDesc(int id, char* out, int cap);
static void mShopDescTick(CtMenuShop* s) {
  if (!s->descRun) return;
  char d[48]; int n = mItemDesc(s->descCur, d, sizeof d) ? (int)strlen(d) : 0;
  if (s->descN < n) s->descN++;                                                  // one C2/0009 step
  else s->descRun = 0;                                                           // the terminator: $0215 = 0
}
// C2/F5A7's second loop: for charId 0..6, colours 13-15 of OBJ palette charId+1 = three words at $D1:(4B00 +
// 6 * CD6CEC[weapon]) through C2/F5ED, the weapon being the sheet's +0x29 at that moment.
static void mShopWeaponPal(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen) {
  for (int c = 0; c < 7; c++) {
    int wpn = mem[0x2600 + c * 0x50 + 0x29];
    long a = (rom && romLen > 0x0D6CEC + 0x100) ? 0x110000L + 0x4B00 + 6L * CtRomByte(rom, 0x400000L, (long)(0x0D6CEC + wpn)) : -1;   // $D1:xxxx
    for (int k = 0; k < 3; k++) s->wpnPal[c][k] = (a > 0 && a + 2 * k + 1 < romLen) ? (uint16_t)(CtRomByte(rom, 0x400000L, (long)(a + 2 * k)) | (CtRomByte(rom, 0x400000L, (long)(a + 2 * k + 1)) << 8)) : 0;
  }
  s->wpnOn = 1;
}
// C2/E072: the party members 0..$85-1 (`nParty`): the panel cells masked with `hi` and the palette dimmed, except the
// member $71 (`keep`), which gets hi 0 and its own palette.
static void mShopDimParty(CtMenuShop* s, int keep, int hi) {
  for (int k = 0; k < s->nParty && k < s->nRoster; k++) {
    int h = (k == keep) ? 0 : hi;                                                // 2E07F CMP $71 / STZ $03
    mShopMask(s, k, 4, h);                                                       // JSR C2/E058
    mShopSprPal(s, s->roster[k], h);                                             // JSL FF/F628
  }
}

// C2/DCDA: the Owned count of every listed id. A hit whose count is 0 continues the scan, but with A now holding that
// 0, so it goes on matching empty slots instead of the id.
static void mShopOwned(CtMenuShop* s, const uint8_t* mem) {
  for (int y = 0; y < 32 && s->list[y]; y++) {                                   // 2DCE0 LDA $1000,Y / BEQ
    int a = s->list[y], x = 0;
    for (;;) {
      int hit = 0;
      for (;;) {                                                                 // 2DCE7 CMP $2400,X / BEQ
        if (mem[0x2400 + x] == a) { hit = 1; break; }
        x = (x + 1) & 0xFF; if (!x) break;                                       // 2DCEC INX / BNE
      }
      if (!hit) { s->owned[y] = 0; break; }                                      // 2DCEF LDA #00 / STA
      int c = mem[0x2500 + x];                                                   // 2DCF6 LDA $2500,X
      if (c) { s->owned[y] = (uint8_t)c; break; }                                // 2DCFB STA $1020,Y
      a = 0;                                                                     // 2DCF9 BEQ ->2DCEC, A = 0
      x = (x + 1) & 0xFF; if (!x) { s->owned[y] = 0; break; }
    }
  }
}
// C2/8900: # Equipped, every roster member (up to 7) whose sheet byte at the class offset is the item.
static void mShopEquipped(CtMenuShop* s, const uint8_t* mem) {
  int off = kShopSheetOff[s->cls];                                               // 28906 LDA $C2894F,X
  s->equipped = 0;                                                               // 2890E STZ $0D82
  for (int k = 0; k < 7; k++) {                                                  // 28948 CPX #0007
    if (k >= s->nRoster) break;                                                  // 28925 BMI (the FF end)
    s->eqBy[k] = 0;                                                              // 2892B STZ $1056,X
    int c = s->roster[k] & 7;
    if (mem[0x2600 + c * 0x50 + off] == (uint8_t)s->curId) { s->eqBy[k] = 1; s->equipped++; }
  }
}
// C2/DD7C: the Owned/Equipped box and the party panel for the item $04C9. Per roster member: the current Attack/Defense
// ($9ACD/$9ACE -> $9B23/$9B24), anim 0; if the item is equippable by that member (C2/93A8: $7A00[id] &
// FF/F9BB[charId]) anim 0x0B, the item placed at the class offset of a copy of the sheet and C2/916E recomputes the
// pair.
static void mShopBox(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen) {
  for (int c = 2; c < 30; c++) mShopB1(s, 2, c, 0x2000);                         // 2DD7F $2E84..$2EBB = 2000
  s->boxOwned = s->curMax1 & 0xFF;                                               // what 2DDC1 prints ($04CA now)
  s->cls = mShopClass(s->curId, NULL);                                           // 2DDA3 JSR C2/8881
  mShopEquipped(s, mem);                                                         // 2DDA6 JSR C2/8900
  int off = kShopSheetOff[s->cls];                                               // 2DDB0 LDA $C2DE1D,X
  int mask = mShopEqMask(rom, romLen, s->curId);
  for (int k = 0; k < s->nRoster; k++) {                                         // 2DDDA LDA $104D,X / BMI
    int c = s->roster[k] & 7;
    const uint8_t* sh = mem + 0x2600 + c * 0x50;                                 // 2DDDF JSR C2/8816
    s->atkOld[k] = s->atkNew[k] = sh[0x3D];                                      // 2DDE2 LDX $9ACD / STX $9B23
    s->defOld[k] = s->defNew[k] = sh[0x3E];
    s->anim[c] = 0;                                                              // 2DDEE STA $1811,Y
    if (mask & (0x80 >> c)) {                                                    // 2DDF4 JSR C2/93A8
      uint8_t tmp[0x50]; memcpy(tmp, sh, sizeof tmp);
      s->anim[c] = 0x0B;                                                         // 2DDFB LDA #0B
      tmp[off] = (uint8_t)s->curId;                                              // 2DE05 STA $9A90,X
      uint8_t d[11]; CtMenuRecalcDerived(tmp, d);                                // 2DE08 JSR C2/916E
      s->atkNew[k] = d[7]; s->defNew[k] = d[8];
    }
    CtMenuSprRequest(c, s->anim[c]);
    mShopPanel(s, k, s->eqBy[k], s->atkNew[k], s->atkOld[k], s->defNew[k], s->defOld[k]);   // 2DE0D JSR C2/DE21
  }
}
// C2/DD40 via C2/EA81: the page params of the list on screen: 8 rows, the last live cell, the scroll range.
static void mShopPageFrom(CtMenuShop* s, int n, int first) {                    // C2/EA81, $00 = 8 rows
  int m = n < 8 ? n : 8;                                                         // 2EA93 BCS / STY $00: min(n, 8)
  s->firstCell = first;                                                          // $0DDB
  s->lastCell = (first + (m ? m - 1 : 0)) & 0xFF;                                // 2EA9B BEQ / DEC A / ADC $0DDB
  s->scrollMax = n > 8 ? n - 8 : 0;                                              // $0DDA (= $57)
}
static void mShopPage(CtMenuShop* s, int n) { mShopPageFrom(s, n, 0); }          // C2/DD40: STZ $0DDB
// C2/DF10-DF2C, C2/DECC's tail: C2/DD40's window over the sell list, the cursor and the scroll clamped into it, and
// $0DD9 = $0D95 = $1043 (the scrollbar's place).
static void mShopSellWindow(CtMenuShop* s, uint8_t* mem) {
  mem[0x0DDB] = 0; CtMenuListWindow(mem, 8, s->nSell, 0x5400);                 // 2DF10 JSR C2/DD40
  if (s->cell > s->lastCell) s->cell = s->lastCell;                              // 2DF13 $0DDC / CMP $54
  if (s->sellScroll > s->scrollMax) s->sellScroll = s->scrollMax;               // 2DF1C $57 / CMP $1043
  mem[0x0DD9] = mem[0x0D95] = (uint8_t)s->sellScroll;                          // 2DF26
}
// C2/DECC: the sell list. The bag in slot order, skipping id 0, ids >= $F2 and every id whose $7700 has bit 2
// (accessories, key items and a few others). $5700,Y is stored before the skip test, as the ROM does.
static void mShopSellList(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen) {
  memset(s->sellId, 0, 6);
  int y = 0;
  for (int x = 0; x < 256; x++) {
    s->sellCnt[y] = mem[0x2500 + x];
    int id = mem[0x2400 + x];
    if (!id || id >= 0xF2 || (mShopFlags(rom, romLen, id) & 0x04)) continue;
    s->sellId[y++] = (uint8_t)id;
  }
  s->nSell = y; s->sellId[y] = 0;
}

static void mShopNavLoad(CtMenuShop* s, const uint8_t* rom, long romLen, long rec);   // (below, with the graph step)
void CtMenuShopOpen(CtMenuShop* s, const uint8_t* mem, const uint8_t* glb, const uint8_t* rom, long romLen, int shopNo) {
  if (!s) return;
  memset(s, 0, sizeof *s);
  s->shopNo = shopNo & 0x3F;
  s->sub = 1;                                                                    // D541 INC $68 (after the build)
  s->cell = 8;                                                                   // C2/DAD1 $54 = 8 (Buy)
  s->prevCell = 8;
  s->hiIdx = 0xFF;
  // C2/DBE8: the roster = the non-negative entries of $0D5F, which C2/816A packs from $2980..$2988.
  int nActive = 0;                                                               // C2/816A: $73 counts $2980..$2982
  for (int x = 0; x < 9 && mem; x++) {
    int e = mem[0x2980 + x];
    if (!(e & 0x80)) { s->roster[s->nRoster++] = (uint8_t)e; if (x < 3) nActive++; }
  }
  s->roster[s->nRoster] = 0xFF;
  // FF/F813: $85 = $73, or $75 (every member) once $7F0000 >= $49 and $7F00BA bit0 is clear.
  s->nParty = (glb && glb[0x0000] >= 0x49 && !(glb[0x00BA] & 1)) ? s->nRoster : nActive;
  // $FF:C213's `0E` copy puts the graph $FF:B63D in $9380; C2/DBE8 wraps the party row at $85 (cell 12's Left =
  // the last member, the last member's Right = cell 12); C2/DACA backs the first 0x48 bytes up at $9990.
  mShopNavLoad(s, rom, romLen, 0x3FB63DL);
  if (s->nParty >= 1) {
    s->nav[12][4] = (uint8_t)(s->nParty - 1 + 0x0C);                             // 2DC0C STA $93CC
    if (s->nParty - 1 + 12 < 19) s->nav[12 + s->nParty - 1][5] = 0x0C;          // 2DC19 STA $93CD,X
  }
  memcpy(s->navBak, s->nav, sizeof s->navBak);                                   // MVN $9380 -> $9990, 0x48 bytes
  s->partyCell = 0x0C;                                                           // C2/DACA: $79 = 0C
  // C2/DC1E: per member, row 24 = the weapon icon (sheet +0 | 20, attr 20), row 25 = the shield (tile byte 2E).
  for (int k = 0; k < s->nRoster && k < 7; k++) {
    mShopB1(s, 24, 2 + 4 * k, 0x2000 | (0x20 | (s->roster[k] & 7)));
    mShopB1(s, 25, 2 + 4 * k, 0x002E);
  }
  // C2/DAF5: the list = 32 bytes from $CC:[$CC:2C6D + 2*shop] to $7E:1000; C2/DCBE counts to the first 0.
  if (rom && romLen >= 0x400000) {
    long t = 0x0C2C6DL + s->shopNo * 2;
    long a = 0x0C0000L + (CtRomByte(rom, 0x400000L, (long)(t)) | (CtRomByte(rom, 0x400000L, (long)(t + 1)) << 8));
    for (int k = 0; k < 32; k++) s->list[k] = CtRomByte(rom, 0x400000L, (long)(a + k));
  }
  while (s->nList < 32 && s->list[s->nList]) s->nList++;
  for (int k = s->nList; k < 32; k++) s->list[k] = 0;                            // C2/DCBE zeroes the tail
  for (int k = 0; k < s->nList; k++)                                           // C2/DB9F: $6E00[id*2], only
    s->price[s->list[k]] = (uint16_t)CtMenuShopBuyPrice(rom, romLen, glb, s->list[k]);   //  for the listed ids
  s->curId = 0;                                                                  // C2/DB33 STZ $04C9
  if (mem) mShopBox(s, mem, rom, romLen);                                        //  JSR C2/DD7C
  s->curMax1 = 0;                                                                // STZ $04CA
  s->arrowRun = 1;                                                               // C2/8A98's task, $E0 = 2
  s->arrowOn = 0; s->arrowCtr = 0;                                               // $0DA0 = $0DA3 = 0
  s->d0b = 0xFF;                                                                 // on the open: $0D0A/$0D0B = FFFF
  s->upQ |= SHOP_UP_FBCE;                                                        // C2/DB7F LDX #FBCE / JSR C2/8385
  mShopNmi(s);
}

// C2/8B69: the blink, run by the down-arrow task while $E0 != 0: 8 frames per phase, drawn when $0DA0 bit 0 = 0.
static void mShopArrowTick(CtMenuShop* s) {
  if (!s->arrowRun) return;
  s->arrowCtr = (s->arrowCtr & 7) - 1;                                           // TRB $0DA3 (F8) / DEC
  if (s->arrowCtr < 0) { s->arrowCtr = 0xFF; s->arrowOn ^= 1; }                  // BPL / EOR #01
  // arrowOn holds $0DA0 bit 0: 1 = hidden (the task skips its OAM write on BCS)
}
// C2/E9E2 (inside C2/E984, the head of every handler): $81 = $54, then one step over the node graph $9380 (s->nav, 6
// bytes per cell: x, y, up, dn, lf, rt). The lowest set bit of the d-pad's new presses picks the byte (Right, Left,
// Down, Up = +5, +4, +3, +2); $54 = that byte & 0x7F, and bit 7 marks an edge ($5A bit 7: the byte names the cell
// itself). The shop's graph ($FF:B63E): cells 0-7 the list rows (0x80 / 0x87 at the ends), 8/9 Buy/Sell, 10 the
// quantity, 11 the down arrow, 12-18 the party row; the equip sub-mode's ($FF:B6B1): 0-3 the gear rows (wrapping),
// 4-11 the candidate list (0x84 / 0x8B). Returns 1 on an edge.
static int mShopGraphStep(CtMenuShop* s, const CtMenuShopIn* in) {
  s->prevCell = s->cell;                                                         // 2E9E7 STA $81
  int b = in->right ? 5 : in->left ? 4 : in->down ? 3 : in->up ? 2 : -1;        // 2E9F4 LSR / BCS: the lowest bit
  if (b < 0 || s->cell < 0 || s->cell >= 19) return 0;
  int v = s->nav[s->cell][b];                                                    // 2EA36.. LDA $9382+b,Y
  s->cell = v & 0x7F;                                                            // 2EA48 STA $54 / TRB $54
  return (v & 0x80) ? 1 : 0;                                                     // 2EA4C TSB $5A
}
// $FF:B63D / $FF:B6B0, a node-graph record: a byte count, then the cells (C2/ED31's `0E` copy, C2/F364: `DEC A / MVN`
// = that many bytes over $9380: 0x72 = 19 cells, 0x48 = 12 cells, the rest of $9380 untouched).
static void mShopNavLoad(CtMenuShop* s, const uint8_t* rom, long romLen, long rec) {
  if (!rom || romLen <= rec + 1) return;
  int n = CtRomByte(rom, 0x400000L, (long)(rec));
  for (int i = 0; i < n && i < (int)sizeof s->nav; i++) ((uint8_t*)s->nav)[i] = CtRomByte(rom, 0x400000L, (long)(rec + 1 + i));
}
// C2/EA53 on a list: with no scroll range an edge wraps (Down -> $0DDB, Up -> $0DDC) and a cell past the last item
// returns to $0DDB; with a scroll range the edge is reported (the caller scrolls by one row).
static int mShopListWrap(CtMenuShop* s, const CtMenuShopIn* in, int edge) {
  if (!s->scrollMax) {                                                           // 2EA53 LDA $57 / BEQ
    if (edge) s->cell = in->down ? s->firstCell : s->lastCell;                   // 2EA6A AND #04
    if (s->lastCell < s->cell) s->cell = s->firstCell;                           // 2EA78 CMP / BCC -> $0DDB
    return 0;
  }
  return edge;                                                                   // 2EA61 LDA $5A / BPL
}
// C2/93BF: L = one page on, R = one page back; a page that cannot move puts the cursor on the end cell.
static int mShopPageKey(CtMenuShop* s, const CtMenuShopIn* in, int* scroll) {
  if (!in->l && !in->r) return 0;
  int a;
  if (in->r) {                                                                   // bit1 (R)
    a = (*scroll - 8) & 0xFF;                                                    // 293D1 SBC $0DD8
    if (!(a < s->scrollMax)) { s->cell = s->firstCell; a = 0; }                  // 293D9 $54 = $0DDB
  } else {                                                                       // bit0 (L)
    a = *scroll + 8;                                                             // 293E6 ADC $0DD8
    if (a > 0xFF || !(a < s->scrollMax)) { s->cell = s->lastCell; a = s->scrollMax; }   // 293F0 $54 = $0DDC
  }
  if (a == *scroll) return 0;                                                    // 293F8 CMP / BEQ
  *scroll = a; s->prevCell = 0xFF;                                               // 293FD .. $81 = FF
  return 1;
}
// C2/D7F1 / C2/D8D4: the row under the cursor becomes $04C9/$04CA; a new row re-runs C2/DD7C; a moved cursor blips.
static void mShopRowUpdate(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen, CtMenuShopOut* o, int sell) {
  int x = (s->cell + (sell ? s->sellScroll : s->buyScroll)) & 0xFF;
  if (sell) { s->sellRow = s->cell; s->curMax1 = s->sellCnt[x]; s->curId = s->sellId[x]; }
  else      { s->buyRow  = s->cell; s->curMax1 = x < 32 ? s->owned[x] : 0; s->curId = x < 32 ? s->list[x] : 0; }
  mShopDesc(s, s->curId);                                                        // 2D80D / 2D8F0 JSR C2/F2F3
  if (x != s->hiIdx) { s->hiIdx = x; mShopBox(s, mem, rom, romLen); }           // 2D813 CMP $83 / BEQ
  if (s->cell != s->prevCell) mShopSfx(o, 0x00);                                 // 2D81E CMP $81 / JSR C2/EAC2
  s->upQ |= SHOP_UP_FBE3;                                                        // 2D825 / 2D908 C2:FBE3
}
// C2/D82B / C2/D90E: a scroll by one row past an edge, when there is a row beyond it.
static void mShopEdgeScroll(CtMenuShop* s, const CtMenuShopIn* in, CtMenuShopOut* o, int sell) {
  int n = sell ? s->nSell : s->nList;
  int* sc = sell ? &s->sellScroll : &s->buyScroll;
  int lim = (n - 2) & 0xFF;
  if (((s->cell + *sc - 1) & 0xFF) >= lim) return;                              // 2D83A CMP $00 / BCS
  if (in->down) (*sc)++; else (*sc)--;                                           // 2D852 $5A & 04
  s->sbPos = *sc;                                                                // 2D863 / 2D946 STA $0D95
  mShopSfx(o, 0x00);                                                             // 2D878 JSR C2/EAC2
}
// C2/DA01: the quantity picker (table C2/DA35 = Right, Left, Down, Up). bound = $04CA = max + 1.
static void mShopPicker(CtMenuShop* s, const CtMenuShopIn* in, CtMenuShopOut* o) {
  int q0 = s->qty, q = s->qty, b = s->curMax1;
  if (in->right)     { if (q + 1 < b) q = q + 1; }                               // DA3D
  else if (in->left) { if (q - 1 != 0 && q - 1 < b) q = q - 1; }                 // DA50
  else if (in->down) { int t = (q - 10) & 0xFF; q = (t >= 1 && t < b) ? t : 1; } // DA83
  else if (in->up)   { q = (q + 10 < b) ? q + 10 : b - 1; }                      // DA64
  else return;
  s->total = (s->total + (uint32_t)((q - q0) * s->unit)) & 0xFFFFFF;            // C2/FD68: the running sum
  s->qty = q;
  if (q != q0) mShopSfx(o, 0x00);                                                // C2/DA1E: $04CB != $83
}

// ---- The equip sub-mode (substates 6-8) ----
// The string writer C2/EF65 -> C2/EF97 into the BG1 buffer: glyph + the attr byte $7E; an FF glyph (a blank) also goes
// to the cell above while $88 bit 7 is clear. $7E is 0 on every path here (A59A's $7D = 2000 is undone by its own STZ
// $7D), so these rows carry attr 0 and the palette code 06 sets.
static void mShopGlyph(CtMenuShop* s, int* pos, int c, int attr, int flag88) {
  int row = *pos / 32, col = *pos % 32;
  if (!(flag88 & 0x80) && c == 0xFF) mShopB1(s, row - 1, col, (attr << 8) | 0xFF);   // 2EFA5 STA $7DFFC0,X
  mShopB1(s, row, col, (attr << 8) | (c & 0xFF));                                // 2EFB0 STA $7E0000,X
  (*pos)++;
}
// C2/F114 -> C2/F1DA: `digits` decimal digits of v (tile D4 + d), leading zeros FF, the last digit always printed.
static void mShopDigits(CtMenuShop* s, int* pos, unsigned v, int digits, int attr) {
  int d[8];
  for (int i = digits - 1; i >= 0; i--) { d[i] = (int)(v % 10); v /= 10; }
  int lead = 1;
  for (int i = 0; i < digits; i++) {
    if (d[i] || i == digits - 1) lead = 0;
    mShopB1(s, *pos / 32, *pos % 32, (attr << 8) | (lead ? 0xFF : 0xD4 + d[i]));
    (*pos)++;
  }
}
// The RAM a shop string reads: $0D4D.. (C2/E18A's compares) and the sheet copy $9A90.., else the $7E page.
static int mShopRam(const CtMenuShop* s, const uint8_t* mem, int addr) {
  if (addr >= 0x0D4D && addr < 0x0D4D + 7) return s->cmp7[addr - 0x0D4D];
  if (addr >= 0x9A90 && addr < 0x9A90 + 0x50) return s->copy[addr - 0x9A90];
  return mem ? mem[addr & 0xFFFF] : 0;
}
// C2/ED31's `06` string command -> C2/EF48 -> C2/EF7E: string `id` of $FF:C457 at (x, y) from the base $61, the codes
// the equip sub-mode's two strings use: 00 end, 01 next line ($65 += 0x80, or 0x40 once $88 bit7 is set), 02 goto
// ($61 + word), 06 palette from RAM (attr bits 2-4 = byte & 1C), 08 n blanks, 09 $88 = byte, 0A attr = byte, 0C a stat
// [cap index, addr16] in 2 digits, or tile 2F twice at its FF/CF33 cap.
static void mShopStr(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen, int id, int base, int x, int y) {
  if (!rom || romLen < 0x400000) return;
  long t = 0x3FC457L + id * 2;
  long a = 0x3F0000L + (CtRomByte(rom, 0x400000L, (long)(t)) | (CtRomByte(rom, 0x400000L, (long)(t + 1)) << 8));
  int pos = base + y * 32 + x, line = pos, attr = 0, f88 = 0;                     // C2/ED90; $7E = 0 (see mShopGlyph)
  for (int guard = 0; guard < 256 && a < romLen; guard++) {
    int c = CtRomByte(rom, 0x400000L, (long)(a++));
    if (c == 0x00) return;
    if (c >= 0x10) { mShopGlyph(s, &pos, c, attr, f88); continue; }
    switch (c) {
      case 0x01: line += (f88 & 0x80) ? 32 : 64; pos = line; break;             // 2EFF4 (words: 0x40 / 0x80 bytes)
      case 0x02: line = pos = base + (CtRomByte(rom, 0x400000L, (long)(a)) | (CtRomByte(rom, 0x400000L, (long)(a + 1)) << 8)) / 2; a += 2; break;
      case 0x06: attr = (attr & ~0x1C) | (mShopRam(s, mem, CtRomByte(rom, 0x400000L, (long)(a)) | (CtRomByte(rom, 0x400000L, (long)(a + 1)) << 8)) & 0x1C); a += 2; break;
      case 0x08: { int n = CtRomByte(rom, 0x400000L, (long)(a++)); while (n--) mShopGlyph(s, &pos, 0xFF, attr, f88); } break;
      case 0x09: f88 = CtRomByte(rom, 0x400000L, (long)(a++)); break;
      case 0x0A: attr = CtRomByte(rom, 0x400000L, (long)(a++)); break;
      case 0x0C: {                                                               // C2/F0D1
        int ci = CtRomByte(rom, 0x400000L, (long)(a)), ad = CtRomByte(rom, 0x400000L, (long)(a + 1)) | (CtRomByte(rom, 0x400000L, (long)(a + 2)) << 8); a += 3;
        int v = mShopRam(s, mem, ad);
        if (v >= CtRomByte(rom, 0x400000L, (long)(0x3FCF33L + ci))) {                                          // 2F0E9 CMP $FFCF33,X / BCS
          mShopB1(s, pos / 32, pos % 32, (attr << 8) | 0x2F); pos++;
          mShopB1(s, pos / 32, pos % 32, (attr << 8) | 0x2F); pos++;
        } else mShopDigits(s, &pos, (unsigned)v, 2, attr);                       // F114 fmt 11
      } break;
      default: return;                                                           // (codes these strings never use)
    }
  }
}
#define SHOP_EQ_BASE ((0x33C2 - 0x2E00) / 2)                                     // C2:C309/C318 `08 C2 33`
// C2/A6F0 X=3664: the member's four gear bytes from the copy (weapon +29, helm +27, armor +28, accessory +2A), each
// an 11-byte name record (the type icon first, C2/F2DC) at rows 33/35/37/39 from column 18.
static void mShopGearRows(CtMenuShop* s, const uint8_t* rom, long romLen) {
  static const uint8_t kGear[4] = { 0x29, 0x27, 0x28, 0x2A };
  for (int i = 0; i < 4; i++) {
    int pos = (0x3664 - 0x2E00) / 2 + i * 64;
    long rec = 0x0C0B5EL + 11L * s->copy[kGear[i]];
    for (int k = 0; k < 11; k++) mShopGlyph(s, &pos, (rom && rec + k < romLen) ? CtRomByte(rom, 0x400000L, (long)(rec + k)) : 0xFF, 0, 0x0B);
  }
}
// C2/A3ED X=3624: rows 32+2i / 33+2i, eleven cells from column 18; attr 00 on the gear row $0077, 1C on the others.
static void mShopGearHi(CtMenuShop* s) {
  for (int i = 0; i < 4; i++)
    for (int r = 0; r < 2; r++)
      for (int k = 0; k < 11; k++) mShopB1Attr(s, 32 + 2 * i + r, 18 + k, i == s->slot ? 0x00 : 0x1C);
}
// C2/8820 -> C2/8839: the sheet of $0D5F[$71] into $9A90.
static void mShopCopyLoad(CtMenuShop* s, const uint8_t* mem) {
  memcpy(s->copy, mem + 0x2600 + (size_t)(s->roster[s->member] & 7) * 0x50, 0x50);
}
// C2/A569: eight list rows into the BG3 buffer at $4FC6 + 0x80r (row 7 + 2r, column 3), from $0F00[$0FC8]; a 0 id ends
// the list (the rest read the same 0). C2/A59A: attr $7E = 20, the name's 10 characters after its icon, or 10 FFs.
static void mShopEqRows(CtMenuShop* s, const uint8_t* rom, long romLen) {
  int y = s->eqScroll;
  for (int r = 0; r < 8; r++) {
    int id = (y < 0xF3) ? s->eqCand[y] : 0;
    if (id) y++;                                                                 // 2A583 BEQ / INY
    int row = 7 + 2 * r;
    for (int k = 0; k < 10; k++) {
      int c = id ? ((rom && 0x0C0B5EL + 11L * id + 1 + k < romLen) ? CtRomByte(rom, 0x400000L, (long)(0x0C0B5EL + 11L * id + 1 + k)) : 0xFF) : 0xFF;
      if (c == 0xFF) s->b3[row - 1][3 + k] = 0x20FF;                            // EF97's FF: the cell above too
      s->b3[row][3 + k] = (uint16_t)(0x2000 | c);
    }
  }
}
// C2/A463 (via C2/E0F1): the candidates for gear row $0077 (C2/8953), $0FC8 = 0, $0D26 = C2/A4AB[$0077], the page from
// cell 4, the rows (C2/A569) and C2:FC1B.
static void mShopEqList(CtMenuShop* s, uint8_t* mem, const uint8_t* rom, long romLen) {
  static const uint8_t kSheetOff[4] = { 0x29, 0x27, 0x28, 0x2A };             // C2/A4AB
  s->slot = s->cell & 3;                                                         // C2/E150: $0077 = $54
  mShopGearHi(s);                                                                //   JSR C2/A3ED
  s->eqCount = CtMenuEquipCandidates(mem, s->slot, s->roster[s->member] & 7, s->eqCand, s->eqCnt);
  s->eqScroll = 0;                                                               // 2A478 STZ $0FC8
  s->sheetOff = kSheetOff[s->slot];                                              // 2A485 STA $0D26
  mShopPageFrom(s, s->eqCount, 4);                                               // $0DDB = 4 / C2/EA81
  if (mem) {                                                                     // 2E0F7 LDA #2A68 / STA $0D92, then
    mem[0x0D92] = 0x68; mem[0x0D93] = 0x2A;                                      //   C2/A463's window: $0FC8 = 0,
    mem[0x0FC8] = 0; mem[0x0DDB] = 4;                                            //   first cell 4, 1500 over the
    CtMenuListWindow(mem, 8, s->eqCount, 0x1500);                                //   count - 8, $0D94 x 4 (quarter
    mem[0x0D94] = (uint8_t)(mem[0x0D94] * 4); mem[0x0D95] = 0;                   //   rows), $0D95 = 0, the task
    s->sbTask = 1; mem[0x0D9A] = 0;                                              //   (C2/8B93)
  }
  mShopEqRows(s, rom, romLen);
  s->upQ |= SHOP_UP_FC1B | SHOP_UP_FBF1;                                         // C2:FC1B in A463, C2:FBF1 in E0F1
}
// C2/E18A: C2/916E on the copy (the Third Eye doubles Evade), the seven stat rows compared through C2/E1DC =
// 36 38 3B 39 37 3A 3C (00 equal / 08 higher / 1C lower, the new value written into the copy), the member's panel cells
// (C2/DE21: Attack/Defense new vs the copy's +3D/+3E), the stat numbers (C2:C318 = string $05) and the description.
static void mShopCompare(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen) {
  static const uint8_t kRow[7] = { 0x36, 0x38, 0x3B, 0x39, 0x37, 0x3A, 0x3C };
  uint8_t d[11]; CtMenuRecalcDerived(s->copy, d);
  for (int i = 0; i < 7; i++) {
    int old = s->copy[kRow[i]], nw = d[kRow[i] - 0x36];
    s->copy[kRow[i]] = (uint8_t)nw;                                              // 2E1AA STA $9A90,X
    s->cmp7[i] = (uint8_t)(nw == old ? 0x00 : nw >= old ? 0x08 : 0x1C);           // 2E1B0 CPX $00 / BCS
  }
  mShopPanel(s, s->member, s->eqBy[s->member], d[7], s->copy[0x3D], d[8], s->copy[0x3E]);   // C2/DE21, Y = 8*$71
  mShopStr(s, mem, rom, romLen, 0x05, SHOP_EQ_BASE, 22, 6);                      // C2:C318
  mShopDesc(s, s->curId);                                                        // JSR C2/F2F3 ($04C9)
}
// C2:C309 = the stat labels (string $03 at (15,18)), the gear-mode node graph $FF:B6B0, then C2:C318.
static void mShopEqPanels(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen) {
  mShopStr(s, mem, rom, romLen, 0x03, SHOP_EQ_BASE, 15, 18);
  mShopNavLoad(s, rom, romLen, 0x3FB6B0L);
  mShopStr(s, mem, rom, romLen, 0x05, SHOP_EQ_BASE, 22, 6);
}
// C2/E017 (Up/Down on Buy/Sell): $80 = $54, substate 6, the cursor on the party row ($79); the members past $85 dimmed.
static void mShopEquipEnter(CtMenuShop* s) {
  s->retCell = s->cell; s->sub = 6; s->cell = s->partyCell;
  for (int k = s->nParty; k < s->nRoster; k++) {                                 // 2E02F LDA $104D,X / BMI
    mShopSprPal(s, s->roster[k], 1);                                             //   JSL FF/F628, $02 = 1004
    mShopMask(s, k, 4, 0x10);                                                    //   JSR C2/E058
  }
  s->upQ |= SHOP_UP_FBE3;                                                        // C2:C2D5 (" Equip   "), C2:FBE3
}
// C2/E0A5 up to its yield (C2/821E): the compares cleared, $71, the copy, C2:C309, the gear rows. The rest is next frame.
static void mShopEquipOpen1(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen) {
  memset(s->cmp7, 0, sizeof s->cmp7);                                            // STZ $0D4D + MVN
  s->member = s->cell - 0x0C;                                                    // $71 = $54 - 0C
  mShopCopyLoad(s, mem);                                                         // C2/8820
  mShopEqPanels(s, mem, rom, romLen);                                            // C2:C309
  mShopGearRows(s, rom, romLen);                                                 // C2/A6F0 X=3664
}
static void mShopEquipOpen2(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen) {
  s->cell = 0;                                                                   // STZ $54
  mShopEqList(s, mem, rom, romLen);                                              // C2/E0F1
  s->equipHdma = 1;                                                              // $0D13 = 55
  s->d0b = 0;                                                                    // STZ $0D0B: BG2 V 255
  s->descForce = (s->descForce - 1) & 0xFF;                                      // DEC $0D78
  mShopDesc(s, 0);                                                               // LDA #00 / JSR C2/F2F3
  mShopDimParty(s, s->member, 0x10);                                             // C2/E072, $02 = 1004
  s->sub = 7;                                                                    // D6E3 INC $68
}
// The commit C2/D792 = the equip screen's C2/9CC1 without C2/9CFC: the old piece back to the bag (C2/8791, the "none"
// ids never), the copy's byte into the sheet and out of the bag (C2/87D5), SFX B5, C2/9137 (the recomputed block into
// the sheet +03 / +36..+40 and into the copy) and the gear rows (C2/A6F0).
static void mShopEquipCommit(CtMenuShop* s, uint8_t* mem, const uint8_t* rom, long romLen, CtMenuShopOut* o) {
  uint8_t* sh = mem + 0x2600 + (size_t)(s->roster[s->member] & 7) * 0x50;
  CtMenuInvAdd(mem, sh[s->sheetOff], 1);                                         // 2D79F JSR C2/8791
  sh[s->sheetOff] = s->copy[s->sheetOff];                                        // 2D7A8 STA ($6F),Y
  CtMenuInvRemove(mem, s->copy[s->sheetOff], 1);                                 // 2D7AA JSR C2/87D5
  mShopSfx(o, 0xB5);                                                             // LDA #B5 / JSR C2/EABA
  uint8_t d[11]; CtMenuRecalcDerived(s->copy, d);                                // C2/9137 -> C2/916E
  int mx = d[9] | (d[10] << 8), cur = s->copy[3] | (s->copy[4] << 8);
  if (cur > mx) cur = mx;                                                        //   ($9A93: C2/9295's clamp)
  sh[3] = (uint8_t)cur; sh[4] = (uint8_t)(cur >> 8);                             // 29149 STA $0003,Y
  for (int i = 0; i < 11; i++) sh[0x36 + i] = s->copy[0x36 + i] = d[i];          // $9B1C -> +36..+40, -> $9AC6
  mShopGearRows(s, rom, romLen);                                                 // C2/A6F0 X=3664
}
// C2/D7BB, the way back from the list (B, or after the commit): blip, the copy reloaded, $04C9 = 0, C2/E18A,
// substate 7 on gear row $0077. No upload is queued here: the next substate-7 frame's C2:FBF1 carries it.
static void mShopEquipBack8(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen, CtMenuShopOut* o) {
  mShopSfx(o, 0x00);
  mShopCopyLoad(s, mem);
  s->curId = 0;
  mShopCompare(s, mem, rom, romLen);
  s->sub = 7; s->cell = s->slot;
}
// C2/9F05, the list cursor: L/R pages (C2/93BF), a wrap or a scroll by one row past an edge (C2/EA53), refused at the
// first row and past the last candidate, with C2/EB9B's one-row slide (shop_screen.md sec. 15; the loop is in
// CtMenuShopFrame).
static int mShopPageKey(CtMenuShop* s, const CtMenuShopIn* in, int* scroll);
// C2/A59A into slot 8 ($53C6: BG3 row 23): the row coming in during a slide, candidate `i`.
static void mShopEqRow8(CtMenuShop* s, const uint8_t* rom, long romLen, int i) {
  int id = (i >= 0 && i < 0xF3) ? s->eqCand[i] : 0;
  for (int k = 0; k < 10; k++) {
    int c = id ? ((rom && 0x0C0B5EL + 11L * id + 1 + k < romLen) ? CtRomByte(rom, 0x400000L, (long)(0x0C0B5EL + 11L * id + 1 + k)) : 0xFF) : 0xFF;
    if (c == 0xFF) s->b3[22][3 + k] = 0x20FF;
    s->b3[23][3 + k] = (uint16_t)(0x2000 | c);
  }
}
static int mShopEqCursor(CtMenuShop* s, uint8_t* mem, const CtMenuShopIn* in, int edge, const uint8_t* rom,
                         long romLen, CtMenuShopOut* o) {
  if (mShopPageKey(s, in, &s->eqScroll)) {                                       // 29F0D JSR C2/93BF
    mShopEqRows(s, rom, romLen); mShopSfx(o, 0x00); s->upQ |= SHOP_UP_FC1B;      // C2/A569, EAC2, C2:FC1B
    if (mem) { mem[0x0FC8] = (uint8_t)s->eqScroll; mem[0x0D95] = (uint8_t)(s->eqScroll * 4); }   // 29F14
    return 0;
  }
  if (mem && s->scrollMax && (mem[0x00F2] & 1)) mem[0x0D18]--;                  // C2/EA53's head (START held)
  if (!mShopListWrap(s, in, edge)) return 0;                                     // 29F2F JSR C2/EA53 / BMI
  int abs = (s->cell - 4) + s->eqScroll;
  if (abs == 0 || abs + 1 >= s->eqCount) return 0;                               // 29F41 BEQ / 29F47 BCC
  int dir = in->down ? 1 : -1, was = s->eqScroll;
  s->eqScroll += dir;                                                            // 29F5E $5A & 04
  mShopSfx(o, 0x00);                                                             // 29F8D JSR C2/EAC2
  if (mem) { mem[0x0FC8] = (uint8_t)s->eqScroll; mem[0x0D22] = (uint8_t)(3 * dir); }
  if (mem && !(mem[0x0D18] & 0x80)) {                                            // 29F90: the loop, its first step
    mShopEqRow8(s, rom, romLen, dir > 0 ? was + 8 : was - 1);                    //   now (the rows still the old
    s->upQ |= SHOP_UP_FC1B;                                                      //   window, the new one in slot 8)
    s->eqSlideStep = 1; s->eqSlideO = 3 * dir;
    mem[0x0DAB] = (uint8_t)s->eqSlideO; mem[0x0D24] = 3;
    mem[0x0D95] = (uint8_t)(was * 4 + dir);
    return 1;                                                                    // (C2/9F05 has not returned)
  }
  mShopEqRows(s, rom, romLen); s->upQ |= SHOP_UP_FC1B;                           // 29F84 A59A, C2:FC1B
  if (mem) mem[0x0D95] = (uint8_t)(s->eqScroll * 4);
  return 0;
}
// C2/E162: the copy reloaded, the candidate under the cursor ($0F00[$0FC8 + $54 - 4]) patched in at $0D26, C2/E18A,
// C2:FBF1.
static void mShopPreview(CtMenuShop* s, const uint8_t* mem, const uint8_t* rom, long romLen) {
  mShopCopyLoad(s, mem);
  int i = s->eqScroll + s->cell - 4;
  s->curId = (i >= 0 && i < 0xF3) ? s->eqCand[i] : 0;                            // $04C9
  s->copy[s->sheetOff] = (uint8_t)s->curId;                                      // 2E17C STA $9A90,X
  mShopCompare(s, mem, rom, romLen);
  s->upQ |= SHOP_UP_FBF1;
}
// C2/E108: L/R cycles $71 over the party (wrapping); a new member: blip, the copy, C2:C309, the gear rows, C2/E072,
// the gear highlight (C2/E150), then a yield. Returns 1 when it yielded.
static int mShopMemberSwitch(CtMenuShop* s, const CtMenuShopIn* in, const uint8_t* mem, const uint8_t* rom, long romLen,
                             CtMenuShopOut* o) {
  if (!in->l && !in->r) return 0;                                                // 2E110 AND #03 / BEQ
  int x = s->member;
  if (in->r) { x++; if (x >= s->nParty) x = 0; }                                 // 2E11F INX / CPX $85
  else { x--; if (x < 0) x = s->nParty - 1; }                                    // 2E118 DEX / BPL / LDX $85, DEX
  if (x == s->member) return 0;
  s->member = x;
  mShopSfx(o, 0x00);                                                             // 2E12E JSR C2/EAC2
  mShopCopyLoad(s, mem);
  mShopEqPanels(s, mem, rom, romLen);                                            // C2:C309
  mShopGearRows(s, rom, romLen);                                                 // C2/A6F0
  mShopDimParty(s, s->member, 0x10);                                             // C2/E072, $02 = 1004
  s->slot = s->cell & 3; mShopGearHi(s);                                         // C2/E150
  return 1;                                                                      // C2/821E
}

static void mShopFrameSub(CtMenuShop* s, uint8_t* mem, const uint8_t* rom, long romLen, const CtMenuShopIn* in,
                          CtMenuShopOut* o);
void CtMenuShopFrame(CtMenuShop* s, uint8_t* mem, const uint8_t* glb, const uint8_t* rom, long romLen,
                     const CtMenuShopIn* inRaw, CtMenuShopOut* o) {
  (void)glb;
  if (!s || !mem || !inRaw || !o) return;
  o->nSfx = 0; o->close = 0;
  // A handler that yielded (C2/821E) resumes where it stopped: no C2/E984 this frame.
  if (s->resume) {
    int r = s->resume; s->resume = 0;
    if (r == 1) mShopEquipOpen2(s, mem, rom, romLen);                            // the rest of C2/E0A5
    else if (r == 3) mShopRowUpdate(s, mem, rom, romLen, o, s->sub == 3);         // after a page's yield: C2/D7F1
    else {                                                                       // after C2/E108: D722 JSR C2/E0F1
      mShopEqList(s, mem, rom, romLen);
      if (s->cell != s->prevCell) mShopSfx(o, 0x00);
    }
    mShopArrowTick(s);                                                           // task slot 6, after slot 1
    mShopDescTick(s);                                                            // task slot 8
    mShopNmi(s);
    return;
  }
  // C2/D82B's loop (C2/D880): one C2/EB1F step a frame and a yield, no input; the fourth step leaves it (C2/D995 shifts
  // the buffer a slot, $9694 = $9710) and the handler goes on into C2/D7F1: the row, the description, the box.
  if (s->eqSlideStep > 0) {                                                      // C2/9F90's loop in the equip list
    int dir = s->eqSlideO > 0 ? 1 : -1;
    if (s->eqSlideStep < 3) { s->eqSlideStep++; s->eqSlideO = 3 * dir * s->eqSlideStep;
                              mem[0x0DAB] = (uint8_t)s->eqSlideO; mem[0x0D24] = (uint8_t)(4 - s->eqSlideStep);
                              mem[0x0D95] = (uint8_t)(mem[0x0D95] + dir); }
    else { s->eqSlideStep = 0; s->eqSlideO = 0; mem[0x0DAB] = 0x0C; mem[0x0D24] = 0; mem[0x0D22] = 0;
           mem[0x0D95] = (uint8_t)(s->eqScroll * 4);                            // 29FB9 $0FC8 x 4
           mShopEqRows(s, rom, romLen); s->upQ |= SHOP_UP_FC1B;                  // C2/9FDE's shift, 29FD8 FC1B
           mShopPreview(s, mem, rom, romLen);                                     // C2/D778 goes on: C2/E162
           if (s->cell != s->prevCell) mShopSfx(o, 0x00); }
    mShopArrowTick(s);
    mShopDescTick(s);
    mShopNmi(s);
    return;
  }
  if (s->slideStep > 0) {
    int dir = s->slideO > 0 ? 1 : -1;
    if (s->slideStep < 3) { s->slideStep++; s->slideO = 3 * dir * s->slideStep;
                            mem[0x0DAB] = (uint8_t)s->slideO; mem[0x0D24] = (uint8_t)(4 - s->slideStep); }
    else { s->slideStep = 0; s->slideO = 0; mem[0x0DAB] = 0x0C; mem[0x0D24] = 0; mem[0x0D22] = 0;
           mShopRowUpdate(s, mem, rom, romLen, o, s->sub == 3); }
    mShopArrowTick(s);
    mShopDescTick(s);
    mShopNmi(s);
    return;
  }
  // C2/E984: A, B, L and R are read only in a substate the previous call also ran in ($0D17).
  CtMenuShopIn inv = *inRaw; const CtMenuShopIn* in = &inv;
  if (s->sub != s->d17) { inv.a = inv.b = inv.l = inv.r = 0; }
  s->d17 = s->sub;
  mShopFrameSub(s, mem, rom, romLen, in, o);
  // B on Buy/Sell (o->close): C2/D553 BVS -> C2/D588 JMP C2/82B2 -> `LDA #FF / JSR C2/8255`, which zeroes the status
  // byte $B0,X of all eight slots before the scan (C2/81CF-81E1) reaches the ones after slot 1, so the blink and the
  // string task do not step on the B frame.
  if (!o->close) {
    mShopArrowTick(s);                                                           // task slot 6 runs after slot 1:
    mShopDescTick(s);                                                            //   a STZ $E0 this frame stops it now;
  }
  mShopNmi(s);                                                                   // task slot 8
}
// The frame C2/D52B spawns the down-arrow task (C2/8A98, just before C2/83B2 spawns the fade-in = the shell's P): the
// scheduler reaches slot 6 after slot 1 in that same frame, so the blink's first step is P's, a frame before the first
// handler call.
void CtMenuShopSpawnFrame(CtMenuShop* s) {
  if (s) mShopArrowTick(s);
}

static void mShopFrameSub(CtMenuShop* s, uint8_t* mem, const uint8_t* rom, long romLen, const CtMenuShopIn* in,
                          CtMenuShopOut* o) {
  switch (s->sub) {
    case 1: {                                                                    // C2/D546: Buy / Sell
      mem[0x0D75] = 1;                                                           // 2D546 LDA #01 / STA $0D75
      mShopGraphStep(s, in);                                                     // C2/E984 -> C2/E9E2 (L/R: 8 <-> 9)
      if (in->a) {                                                               // 2D551 BMI
        s->arrowRun = 0;                                                         // STZ $E0
        s->hiIdx = 0xFF; mShopSfx(o, 0x00);                                      // $83 = FF / JSR C2/EAC2
        if (s->cell == 8) {                                                      // 2D573 SBC #08 / BNE
          s->sub = 2;                                                            // C2/DC7B
          s->descForce = 0xFF;                                                   // 2DC7E LDA #FF / STA $0D78
          mem[0x0D92] = 0xF0; mem[0x0D93] = 0x2A;                                // 2DC85 LDX #2AF0 / STX $0D92
          if (!mem[0x0D88]) { s->buyRow = 0; s->buyScroll = 0; }                 // config "Save Menu Cursor" off
          s->cell = s->buyRow;
          mem[0x0D95] = (uint8_t)s->buyScroll;                                   // 2DCA1 LDA $1041 / STA $0D95
          mShopPage(s, s->nList);                                                // C2/DD40
          mem[0x0DDB] = 0; CtMenuListWindow(mem, 8, s->nList, 0x5400);           //   ($0DDB 0, 8 a page, $4204 5400)
          mShopOwned(s, mem);                                                    // C2/DCDA
          s->sbTask = 1; mem[0x0D9A] = 0;                                        // 2DCB3 JSR C2/8B93
        } else {
          s->sub = 3;                                                            // C2/DE98
          mem[0x0D92] = 0xF0; mem[0x0D93] = 0x2A;                                // 2DE9B LDX #2AF0 / STX $0D92
          if (!mem[0x0D88]) { s->sellRow = 0; s->sellScroll = 0; }
          s->cell = s->sellRow;
          mShopSellList(s, mem, rom, romLen);                                    // C2/DECC
          mShopPage(s, s->nSell);                                                // C2/DF31
          mShopSellWindow(s, mem);                                               //   (C2/DF10-DF2C, C2/DECC's tail)
          s->sbTask = 1; mem[0x0D9A] = 0;                                        // 2DEC2 JSR C2/8B93
        }
        return;
      }
      if (in->b) { o->close = 1; return; }                                       // 2D553 BVS -> C2/82B2, silent
      if (in->up || in->down) {                                                  // 2D555 LDA $5A / AND #0C
        s->arrowRun = 0;                                                         // STZ $E0
        mShopEquipEnter(s);                                                      // JSR C2/E017
      }
      if (s->cell != s->prevCell) mShopSfx(o, 0x00);                             // 2D560 CMP $54 / JSR C2/EAC2
      return;
    }
    case 2: case 3: {                                                            // C2/D58B buy list / C2/D645 sell list
      int sell = (s->sub == 3);
      int edge = mShopGraphStep(s, in);                                          // C2/E984 -> C2/E9E2
      if (in->a) {
        if (!sell) {                                                             // C2/D598
          s->cell = s->buyRow;
          int room = 0x62 - (s->curMax1 + s->equipped);                          // 2D5A6 LDA #62 / SBC $00
          if (room < 0) { mShopSfx(o, 0x01); return; }                           // BCC -> C2/EACC (buzz)
          s->curMax1 = room + 1;
          s->qty = 1; s->unit = s->price[s->curId & 0xFF];                       // C2/FCB2 ($1047 = $6E00[id*2])
          s->total = (uint32_t)s->unit;
          uint32_t g = mShopGold(mem);
          uint32_t qt = s->unit ? g / (uint32_t)s->unit : 0x1000000u;            // C2/FD1D, 24/16 -> $36..$38
          if (qt < 256) {
            if (qt == 0) { mShopSfx(o, 0x01); return; }                          // 2D5BC BEQ -> buzz
            if ((int)qt < s->curMax1) s->curMax1 = (int)qt;                      // 2D5BE CMP / BCS
          }
          mShopSfx(o, 0x00); s->curMax1++;                                       // 2D5C6 blip / INC $04CA
          mem[0x0D9A]--;                                                         // 2D5CC DEC $0D9A (the arrows off)
          s->sub = 5; s->cell = 0x0A;                                            // C2/DF76: $7F = $54, $54 = $0A
        } else {                                                                 // C2/D652
          s->cell = s->sellRow;
          if (!s->curId || !s->curMax1) { mShopSfx(o, 0x01); return; }
          s->qty = 1; s->unit = CtMenuShopSellPrice(rom, romLen, s->curId);       // C2/FCE1
          s->total = (uint32_t)s->unit;
          int32_t room = 0x98967F - (int32_t)mShopGold(mem);                     // 9,999,999 - gold
          if (room >= 0) {                                                        // borrow -> the divide is skipped
            uint32_t qt = s->unit ? (uint32_t)room / (uint32_t)s->unit : 0x1000000u;
            if (qt < 256) {
              if (qt == 0) { mShopSfx(o, 0x01); return; }
              if ((int)qt < s->curMax1) s->curMax1 = (int)qt;
            }
          }
          mShopSfx(o, 0x00); s->curMax1++;
          mem[0x0D9A]--;                                                         // 2D67C DEC $0D9A
          s->sub = 4; s->cell = 0x0A;
        }
        return;
      }
      if (in->b) {                                                               // C2/D5D9 / D69? -> back to Buy/Sell
        s->cell = sell ? 9 : 8;
        s->curId = 0; mShopBox(s, mem, rom, romLen);                             // STZ $04C9 / JSR C2/DD7C
        s->upQ |= SHOP_UP_FBE3;                                                  // 2D5F2 C2:FBE3
        mShopSfx(o, 0x00);
        for (int c = 0; c < 7; c++) { s->anim[c] = 0; CtMenuSprRequest(c, 0); }  // C2/D605: $1811 = 0, ids 0..6
        s->sbTask = 0;                                                           // 2D5F5 LDA #10 / JSR C2/8255
        s->arrowRun = 1;                                                         // $E0 = 2
        s->sub = 1;
        mShopDesc(s, 0);                                                         // 2D602 JMP C2/E012
        return;
      }
      int* sc = sell ? &s->sellScroll : &s->buyScroll;
      if (mShopPageKey(s, in, sc)) { mem[0x0DD9] = mem[0x0D95] = (uint8_t)*sc;   // 293FD STA $0DD9 / $0D95
                                     s->resume = 3; return; }                    // C2/DD02's rows, then C2/D7E7's yield:
                                                                                 //   C2/D7F1 runs the next frame
      s->sbPos = -1;
      if (s->scrollMax && (mem[0x00F2] & 1)) mem[0x0D18]--;                      // C2/EA53's head: START held
      if (mShopListWrap(s, in, edge)) mShopEdgeScroll(s, in, o, sell);         // C2/EA53 BMI -> C2/D82B
      if (s->sbPos >= 0) {
        mem[0x0D95] = (uint8_t)s->sbPos;
        int dir = in->down ? 1 : -1;                                            // C2/EB9B: $0D22 +-3, $0D24 4
        mem[0x0D22] = (uint8_t)(3 * dir);
        if (!(mem[0x0D18] & 0x80)) {                                            // 2D87B LDA $0D18 / BMI: the loop,
          s->slideStep = 1; s->slideO = 3 * dir;                                //   its first step this frame
          mem[0x0DAB] = (uint8_t)s->slideO; mem[0x0D24] = 3;
          return;                                                                // (C2/D7F1 after the loop)
        }
      }
      mShopRowUpdate(s, mem, rom, romLen, o, sell);
      return;
    }
    case 4: case 5: {                                                            // C2/D690 sell qty / C2/D618 buy qty
      int sell = (s->sub == 4);
      if (in->a) {
        if (!sell) {                                                             // C2/D62A
          CtMenuInvAdd(mem, s->curId, s->qty);                                   // C2/8791 (the "full" return is ignored)
          mShopGoldSub(mem, s->total);                                           // C2/FDBB
          mShopOwned(s, mem);                                                    // C2/DCDA
        } else {                                                                 // C2/D6A2
          CtMenuInvRemove(mem, s->curId, s->qty);                                // C2/87D5
          mShopGoldAdd(mem, s->total);                                           // C2/FD97
        }
        mShopSfx(o, 0x55);                                                       // LDA #55 / JSR C2/EABA
      } else if (!in->b) { mShopPicker(s, in, o); return; }                      // JMP C2/DA01
      mShopSfx(o, 0x00);                                                         // the tail's JSR C2/EAC2
      mem[0x0D9A] = 0;                                                           // 2D63B / 2D6B7 STZ $0D9A
      s->hiIdx = 0xFF;                                                           // C2/DFCF: $83 = FF
      if (!sell) { s->sub = 2; s->cell = s->buyRow; }
      else {
        s->sub = 3; s->cell = s->sellRow;
        mShopSellList(s, mem, rom, romLen); mShopPage(s, s->nSell);             // C2/DECC
        mShopSellWindow(s, mem);                                                 //  $54 <= $0DDC, $1043 <= $57
      }
      return;
    }
    case 6: {                                                                    // C2/D6C3: the party row
      mShopGraphStep(s, in);                                                     // L/R along cells 12.. (wrap at $85)
      if (in->a) {                                                               // 2D6DD
        mShopSfx(o, 0x00);
        mShopEquipOpen1(s, mem, rom, romLen);                                    // JSR C2/E0A5 ... C2/821E
        s->resume = 1;
        return;
      }
      if (in->b || in->up || in->down) {                                         // 2D6CB BVS / $5A & 0C -> C2/D6E6
        mShopSfx(o, 0x00);
        memset(s->sprDim, 0, sizeof s->sprDim);                                  // C2/F5A7: every palette back,
        mShopWeaponPal(s, mem, rom, romLen);                                     //   and the weapon colours loaded
        mShopMask(s, 0, 0x1C, 0x00);                                             // E058, $00 = 0, $02 = 001C
        s->partyCell = s->cell;                                                  // $79 = $54
        s->cell = s->retCell;                                                    // $54 = $80
        s->arrowRun = 1;                                                         // $E0 = 2
        s->sub = 1;
        s->upQ |= SHOP_UP_FBE3;                                                  // C2:C2B0, C2:FBE3
        return;
      }
      if (s->cell != s->prevCell) mShopSfx(o, 0x00);                             // 2D6D3 CMP $54 / JSR C2/EAC2
      return;
    }
    case 7: {                                                                    // C2/D715: the gear rows
      mShopGraphStep(s, in);
      if (in->a) {                                                               // 2D72F
        s->cell = s->prevCell;                                                   // $54 = $81
        if (!s->eqCount) { mShopSfx(o, 0x01); return; }                          // $0FC6 == 0 -> C2/EACC
        mShopSfx(o, 0x00);
        s->cell = 4; s->sub = 8;                                                 // $54 = 4 / INC $68
        return;
      }
      if (in->b) {                                                               // 2D745
        mShopSfx(o, 0x00);
        memcpy(s->nav, s->navBak, sizeof s->navBak);                             // MVN $9990 -> $9380, 0x48 bytes
        mShopDimParty(s, s->member, 0x00);                                       // C2/E072, $02 = 0004
        s->cell = s->member + 0x0C;                                              // $54 = $71 + 0C
        s->sub = 6;                                                              // DEC $68
        s->equipHdma = 0;                                                        // $0D13 = 2F
        s->d0b = (s->d0b + 1) & 0xFF;                                            // INC $0D0B: BG2 V 511 (= -1)
        s->sbTask = 0;                                                           // 2D76D LDA #50 / JSR C2/8255: slots 4, 6
        s->upQ |= SHOP_UP_FBE3;                                                  // C2:FBE3
        return;
      }
      if (mShopMemberSwitch(s, in, mem, rom, romLen, o)) { s->resume = 2; return; }   // JSR C2/E108 (a yield)
      mShopEqList(s, mem, rom, romLen);                                          // JSR C2/E0F1
      if (s->cell != s->prevCell) mShopSfx(o, 0x00);
      return;
    }
    case 8: {                                                                    // C2/D778: the candidate list
      int edge = mShopGraphStep(s, in);
      if (in->a) {                                                               // 2D792: the commit, then C2/D7BB
        mShopEquipCommit(s, mem, rom, romLen, o);
        mShopEquipBack8(s, mem, rom, romLen, o);
        return;
      }
      if (in->b) { mShopEquipBack8(s, mem, rom, romLen, o); return; }           // 2D780 BVS -> C2/D7BB
      if (mShopEqCursor(s, mem, in, edge, rom, romLen, o)) return;              // JSR C2/9F05 (a slide: no preview yet)
      mShopPreview(s, mem, rom, romLen);                                         // JSR C2/E162
      if (s->cell != s->prevCell) mShopSfx(o, 0x00);
      return;
    }
    default: return;
  }
}

// ---- The shop's picture (disasm/notes/shop_screen.md) ----
// BG2 = the captured window chrome (menu_shop.h) under the window-gradient list FF/BC53. BG1 = the ROM's text buffer
// $7E:2E00 (32x64 words, VRAM $5800), written at the ROM's addresses and composed line by line as the PPU shows it
// (L = the 1-based scanline, our row y = L - 1). The per-line registers come from the six HDMA tables C2:C213 installs
// (`0C` sets a channel, `0E` copies its table, count byte first); the NMI enables $0D13 & 7F of them (C2/8442): $2F =
// channels 0/1/2/3/5 on every screen but the equip sub-mode, $55 = channels 0/2/4/6 there (C2/E0D7):
//   * BG1 V scroll: ch1 $FF:B8A4 (L1-48 207, 49-60 499, 61-72 503, 73-84 507, 85-96 -1, 97-108 3, ..., 133-144 15,
//     145-160 63, 161-176 367, 177- -1 -- the list's 16-line row pairs at a 12-line pitch) or ch4 $FF:B9CA (215, 219,
//     223, 227, 227, 231, 235, 239 over the same bands: buffer rows 33-47, the gear and stat rows);
//   * BG1 H scroll: ch2, 252 from L193 in both sets (the panel's two number rows sit 4 px right);
//   * BG3 V scroll: ch3 $FF:B8CA (-1 to L40, 480, 255 from L153) or ch6 $FF:B9F0 (-1 to L48, then 3, 7, ... 31 at the
//     12-line pitch: the candidate list's rows 7-21, then 255);
//   * BG mode 2 on L41-152 (ch5 $FF:BB33, main set only), whose offset-per-tile row (BG3 at V 480 = $5D40..) gives
//     screen column c the entry c-1: entries 1-11 = 61FF (C2/DB49: STA $5D42 + a 20-byte MVN; the left column shows
//     buffer line y), entries 13-28 = 216F (C2/DF8C: STA $5D5A + a 30-byte MVN) while the quantity window is up
//     (C2/DF76; C2/DFCF zeroes them): the window is buffer rows 55-61 brought over the list.
#define SHOP_DESC_X 24
#define SHOP_DESC_Y 20
#define SHOP_HDMA_BG1V_MAIN  0x3FB8A4L
#define SHOP_HDMA_BG1V_EQUIP 0x3FB9CAL
#define SHOP_HDMA_BG3V_MAIN  0x3FB8CAL
#define SHOP_HDMA_BG3V_EQUIP 0x3FB9F0L
#define SHOP_HDMA_MODE_MAIN  0x3FBB33L
// One HDMA table (the count byte first): the value line L (1-based) receives. Entries [n, value] (1 byte, mode 0) or
// [n, lo, hi] (2 bytes, mode 2) cover n lines each (n <= 0x80, no repeat entries here); 0 ends it and the last value holds.
static int shopHdma(const CtMenuHost* h, long rec, int bytes, int L) {
  if (!h->rom || h->romLen <= rec + 2) return 0;
  long a = rec + 1; int line = 1, v = 0;
  for (int g = 0; g < 40 && a + bytes < h->romLen; g++) {
    int n = CtRomByte(h->rom, 0x400000L, (long)(a));
    if (!n) break;
    v = (bytes == 2) ? (CtRomByte(h->rom, 0x400000L, (long)(a + 1)) | (CtRomByte(h->rom, 0x400000L, (long)(a + 2)) << 8)) : CtRomByte(h->rom, 0x400000L, (long)(a + 1));
    if (L < line + n) return v;
    line += n; a += 1 + bytes;
  }
  return v;
}
typedef struct { uint16_t w[64][32]; } ShopBg1;
static void sbPut(ShopBg1* b, int col, int row, int word) {
  if (col >= 0 && col < 32 && row >= 0 && row < 64) b->w[row][col] = (uint16_t)word;
}
static void sbCodes(ShopBg1* b, int col, int row, const uint8_t* codes, int n, int attr) {
  for (int i = 0; i < n; i++) sbPut(b, col + i, row, (attr << 8) | codes[i]);
}
// C2/F114: `digits` cells, right-aligned, leading zeros blank (the fmt's bit 7 is clear in every shop call).
static void sbNum(ShopBg1* b, int col, int row, uint32_t v, int digits, int attr) {
  int d[10], n = 0;
  do { d[n++] = (int)(v % 10); v /= 10; } while (v && n < 10);
  for (int i = 0; i < digits; i++) {
    int k = digits - 1 - i;                                                      // digit index from the right
    sbPut(b, col + i, row, (attr << 8) | (k < n ? 0xD4 + d[k] : 0xEF));
  }
}
// C2/F2DC: the 11-byte name record $CC:0B5E + 11*id (its first byte is the category icon glyph).
static void sbItemName(ShopBg1* b, const CtMenuHost* h, int col, int row, int id, int attr) {
  if (!h->rom || h->romLen < 0x0C0B5EL + 11L * 256) return;
  sbCodes(b, col, row, CtRomPtr(h->rom, h->romLen, 0x0C0B5EL + 11L * (id & 0xFF), 11), 11, attr);
}
// The menu's string interpreter as the shop uses it (C2/ED31's `06` string command -> the $FF:C457 table, ids
// $33-$3C): 00 end, 02 goto [word, relative to $2E00], 03 number [addr16, fmt, bank] (C2/F114), 04 include
// [addr16, bank], 08 blanks [n], anything else a glyph code at the cursor. The numbers the shop's strings name
// are read from where this port keeps them: $2C53 the gold (WRAM), $04CB / $1044 / $0D4D the picker's state.
static uint32_t sbRomNumVal(const CtMenuHost* h, const CtMenuShop* s, int addr) {
  switch (addr) {
    case 0x2C53: return h->mem ? (uint32_t)(h->mem[0x2C53] | (h->mem[0x2C54] << 8) | (h->mem[0x2C55] << 16)) : 0;
    case 0x04CB: return (uint32_t)s->qty;
    case 0x1044: return s->total;
    case 0x0D4D: return (s->curId < 148) ? ctEqPower[s->curId] : 0;         // C2/DF7C: $7C00[id]
    default:     return 0;
  }
}
static void sbRomStr(ShopBg1* b, const CtMenuHost* h, const CtMenuShop* s, long a, int col, int row, int depth) {
  if (!h->rom || depth > 3) return;
  int pos = row * 32 + col;
  for (int guard = 0; guard < 96 && a > 0 && a < h->romLen; guard++) {
    int c = CtRomByte(h->rom, 0x400000L, (long)(a++));
    if (c == 0x00) return;
    if (c == 0x02) { pos = (CtRomByte(h->rom, 0x400000L, (long)(a)) | (CtRomByte(h->rom, 0x400000L, (long)(a + 1)) << 8)) / 2; a += 2; continue; }
    if (c == 0x03) {
      int addr = CtRomByte(h->rom, 0x400000L, (long)(a)) | (CtRomByte(h->rom, 0x400000L, (long)(a + 1)) << 8), fmt = CtRomByte(h->rom, 0x400000L, (long)(a + 2)); a += 4;
      int digits = (((fmt ^ 0x80) >> 4) & 7) + 1;
      sbNum(b, pos % 32, pos / 32, sbRomNumVal(h, s, addr), digits, 0x20);
      pos += digits; continue;
    }
    if (c == 0x04) {
      long inc = ((long)CtRomByte(h->rom, 0x400000L, (long)(a + 2)) - 0xC0) * 0x10000L + (CtRomByte(h->rom, 0x400000L, (long)(a)) | (CtRomByte(h->rom, 0x400000L, (long)(a + 1)) << 8)); a += 3;
      sbRomStr(b, h, s, inc, pos % 32, pos / 32, depth + 1);
      while (inc < h->romLen && CtRomByte(h->rom, 0x400000L, (long)(inc))) inc++;                               // (the include's cursor is
      continue;                                                                    //  not carried: its callers goto)
    }
    if (c == 0x08) { int n = CtRomByte(h->rom, 0x400000L, (long)(a++)); for (int k = 0; k < n; k++) sbPut(b, (pos + k) % 32, (pos + k) / 32, 0x20FF); pos += n; continue; }
    sbPut(b, pos % 32, pos / 32, (0x20 << 8) | c); pos++;
  }
}
static long sbRomStrAddr(const CtMenuHost* h, int id) {                        // $FF:C457 word table
  long t = 0x3FC457L + id * 2;
  if (!h->rom || h->romLen <= t + 1) return -1;
  return 0x3F0000L + (CtRomByte(h->rom, 0x400000L, (long)(t)) | (CtRomByte(h->rom, 0x400000L, (long)(t + 1)) << 8));
}
static int shopBg1V(const CtMenuHost* h, const CtMenuShop* s, int L, int col, int qtyOpen) {
  if (!s->equipHdma && shopHdma(h, SHOP_HDMA_MODE_MAIN, 1, L) == 0x02 && col >= 1) {   // mode 2: entry col-1
    int e = col - 1;
    if (e >= 1 && e <= 11) return 0x1FF;
    if (qtyOpen && e >= 13 && e <= 28) return 0x16F;                          // C2/DF8F STA $5D5A + MVN 30 B
  }
  // C2/EB1F's live table over the list's bands (lines 48-143: 9 lines at +3, bands of 12, 3 lines of a ninth band at
  // the bottom): row space u = y - 48 + $0DAB; band k shows slot k's line u - 12k, and the band past either end is slot
  // 8 (C2/EB9B's extra entries: V7 + 4 below, V0 + 16n - 4 above, the same buffer lines)
  if (s->slideO && !s->equipHdma && L - 1 >= 48 && L - 1 < 144) {
    int u = L - 1 - 48 + s->slideO, k = u >= 0 ? u / 12 : -1, t = u - 12 * k;
    if (k < 0 || k > 7) k = 8;
    int vk = shopHdma(h, SHOP_HDMA_BG1V_MAIN, 2, 48 + 12 * (k > 7 ? 7 : k) + 1) + (k > 7 ? 4 : 0);
    return (48 + 12 * k + t + vk - (L - 1)) & 0x1FF;
  }
  return shopHdma(h, s->equipHdma ? SHOP_HDMA_BG1V_EQUIP : SHOP_HDMA_BG1V_MAIN, 2, L);
}
// One 2bpp text cell of a line: glyph = the menu font (tiles < 0x100) or the narrow digits (0x100-0x187), in BG
// palette p's colours 0-3 (a 2bpp glyph in a 4bpp BG1 tile, or BG3's own 2bpp palette 0 -- the same four colours).
static void shopCellLine(const CtMenuHost* h, uint8_t* fb, int y, int x0, uint16_t w, int gy) {
  int tile = w & 0x3FF, pl = (w >> 10) & 7;
  if (tile == 0 || tile == 0xEF || tile == 0xFF) return;                        // blank cells
  const uint8_t* g = (tile < 0x100) ? h->menuFont2bpp + tile * 16
                   : (tile < 0x188) ? ctShopNarrowTiles[tile - 0x100] : NULL;
  if (!g) return;
  const uint8_t (*P)[3] = ctShopTextPal[pl];
  int b0 = g[gy * 2], b1 = g[gy * 2 + 1];
  for (int xx = 0; xx < 8; xx++) {
    int bit = 7 - xx, v = ((b0 >> bit) & 1) | (((b1 >> bit) & 1) << 1);
    if (!v) continue;
    int x = (x0 + xx) & 0xFF;
    if (x >= MW) continue;
    uint8_t* d = fb + ((size_t)y * MW + x) * 3;
    d[0] = P[v][0]; d[1] = P[v][1]; d[2] = P[v][2];
  }
}
// prio = the tilemap word's priority bit (bit 13) to draw: mode 1 stacks OBJ prio 2 between BG1 prio 0 and prio 1.
static void shopDrawBg1(const CtMenuHost* h, uint8_t* fb, const CtMenuShop* s, const ShopBg1* b, int qtyOpen, int prio) {
  if (!h->menuFont2bpp) return;
  for (int y = 0; y < MH; y++) {
    int L = y + 1, H = (L >= 193) ? 252 : 0;                                     // ch2 (both sets)
    for (int col = 0; col < 32; col++) {
      int line = (L + shopBg1V(h, s, L, col, qtyOpen)) & 0x1FF;
      uint16_t w = b->w[(line >> 3) & 63][col];
      if (((w >> 13) & 1) != prio) continue;
      shopCellLine(h, fb, y, col * 8 - H, w, line & 7);
    }
  }
}
// BG3 (mode 1 with $2105 bit3: its priority-1 tiles over everything): the candidate list rows the equip sub-mode keeps
// in $4E00 (s->v3), through ch3 or ch6. Mode 2's lines show no BG3 (it is the offset-per-tile data there).
static void shopDrawBg3(const CtMenuHost* h, uint8_t* fb, const CtMenuShop* s) {
  if (!h->menuFont2bpp) return;
  for (int y = 0; y < MH; y++) {
    int L = y + 1;
    if (!s->equipHdma && shopHdma(h, SHOP_HDMA_MODE_MAIN, 1, L) == 0x02) continue;
    int line = (L + shopHdma(h, s->equipHdma ? SHOP_HDMA_BG3V_EQUIP : SHOP_HDMA_BG3V_MAIN, 2, L)) & 0x1FF;
    if (s->eqSlideO && s->equipHdma && y >= 48 && y < 144) {                    // C2/EB1F's table over the list's
      int u = y - 48 + s->eqSlideO, k = u >= 0 ? u / 12 : -1, t = u - 12 * k;   //   bands (9 lines at 006 .. 3 at
      if (k < 0 || k > 7) k = 8;                                                //   026 before line 144)
      int vk = shopHdma(h, SHOP_HDMA_BG3V_EQUIP, 2, 48 + 12 * (k > 7 ? 7 : k) + 1) + (k > 7 ? 4 : 0);
      line = (48 + 12 * k + t + 1 + vk) & 0x1FF;
    }
    int row = line >> 3;
    if (row >= CT_SHOP_BG3_ROWS) continue;
    for (int col = 0; col < 32; col++) shopCellLine(h, fb, y, col * 8, s->v3[row][col], line & 7);
  }
}
// The hand rides the live node graph $9380 (s->nav: $FF:B63E, or $FF:B6B1 in the gear rows and the list) and is drawn
// at the node's (x, y), as on the Item screen.
static void shopNode(const CtMenuShop* s, int cell, int* x, int* y) {
  *x = (cell >= 0 && cell < 19) ? s->nav[cell][0] : 0;
  *y = (cell >= 0 && cell < 19) ? s->nav[cell][1] : 0;
}

void CtMenuRenderShop(const CtMenuHost* h, uint8_t* fb, const CtMenuShop* s) {
  if (!h || !fb || !s) return;
  // The backdrop first (CGRAM 0, black): the chrome's map leaves columns 31-32 transparent, and the PPU shows the
  // backdrop there (where the list's scrollbar arrow reaches x 252 on its off frames).
  memset(fb, 0, (size_t)MW * MH * 3);
  int bg2v = (0xFF | (s->d0b << 8)) & 0x1FF;                                     // C2/85D6: $0D0A/$0D0B -> $2110
  mChromeTMPal2(fb, bg2v == 0xFF ? ctMenuChromeShopEquipTM : ctMenuChromeShopTM, 2, ctMenuChromePal,
                mWinGrad(CT_MENU_WINLIST_SHOP));                                 // (line y + 256: map rows 32-59)
  static ShopBg1 b; memset(&b, 0, sizeof b);
  // C2/DACA's display list $FF:C213 draws string $3C at (3,6): it includes $36 "Buy  Sell" there, then goes to (3,9)
  // for the gold {$2C53 fmt 63} + "G", (2,13) "# Owned" and (2,16) "# Equipped". The label is then rewritten by the
  // list entries: $34 "  Buy    " (C2C1), $35 "  Sell   " (C2CB), $33 " Equip   " (C2D5, the equip sub-mode), back to
  // $36 (C2B0), all at (3,6).
  sbRomStr(&b, h, s, sbRomStrAddr(h, 0x3C), 3, 6, 0);
  if (s->sub == 2 || s->sub == 5) sbRomStr(&b, h, s, sbRomStrAddr(h, 0x34), 3, 6, 0);
  if (s->sub == 3 || s->sub == 4) sbRomStr(&b, h, s, sbRomStrAddr(h, 0x35), 3, 6, 0);
  if (s->sub >= 6) sbRomStr(&b, h, s, sbRomStrAddr(h, 0x33), 3, 6, 0);
  if (s->curId && s->sub <= 5) {                                                 // C2/DDB7: only for a real item
    sbNum(&b, 9, 14, (uint32_t)(s->boxOwned & 0xFF), 2, 0x20);                   // {$04CA fmt11}, as C2/DD7C drew it
    sbNum(&b, 10, 17, (uint32_t)s->equipped, 1, 0x20);                           // {$0D82 fmt01}
  }
  // The list rows (C2/DD02 buy / C2/DF31 sell): row r at buffer (14, 5+2r) = name then the price, 5 digits.
  // They stay in the buffer under the quantity window, which is how the ROM keeps them.
  int showSell = (s->sub == 3 || s->sub == 4);
  if (s->sub >= 2 && s->sub <= 5) {
    // during C2/D82B's slide the buffer still holds the old window, and the incoming row sits in slot 8 ($335C, C2/DD20)
    int sdir = s->slideO > 0 ? 1 : s->slideO < 0 ? -1 : 0;
    for (int r = 0; r < (sdir ? 9 : 8); r++) {
      int x = r + (sdir ? (showSell ? s->sellScroll : s->buyScroll) : (showSell ? s->shownSellScroll : s->shownBuyScroll)) - sdir;
      if (r == 8) x = (showSell ? s->sellScroll : s->buyScroll) + (sdir > 0 ? 7 : 0);
      if (x < 0) continue;
      int id = showSell ? (x < 257 ? s->sellId[x] : 0) : (x < 32 ? s->list[x] : 0);
      if (!id) continue;                                                         // C2/DD56: 16 blanks
      sbItemName(&b, h, 14, 5 + 2 * r, id, 0x20);
      int pr = showSell ? CtMenuShopSellPrice(h->rom, h->romLen, id) : s->price[id];
      sbNum(&b, 25, 5 + 2 * r, (uint32_t)pr, 5, 0x20);                           // F114 fmt $42
    }
  }
  // The quantity window (C2/DF76): name (15,55), qty (27,55) 2 digits, total (21,58) 7 digits + G, stat row 61.
  int qtyOpen = (s->sub == 4 || s->sub == 5);
  if (qtyOpen) {
    sbItemName(&b, h, 15, 55, s->curId, 0x20);
    sbRomStr(&b, h, s, sbRomStrAddr(h, 0x37), 0, 0, 0);                           // C2/E001 -> $FF:C2DE: $37 @(0,0)
    int cls = mShopClass(s->curId, NULL);                                        // C2/DFC5: C2E8 C2F5 C2F5 C302 C302
    if (cls <= 2) {
      sbRomStr(&b, h, s, sbRomStrAddr(h, cls == 0 ? 0x39 : 0x3A), 17, 61, 0);    // "Attack: " / "Defense:"
      sbRomStr(&b, h, s, sbRomStrAddr(h, 0x3B), 26, 61, 0);                      // {$0D4D fmt 21}
    } else sbRomStr(&b, h, s, sbRomStrAddr(h, 0x38), 14, 61, 0);                 // 16 blanks
  }
  // The rows the ROM writes once and reads back, as VRAM holds them (CtMenuShop.v1): row 2 (the "E" markers) and rows
  // 24/25 (the party panel: C2/DC1E's icons, C2/DE21's numbers, C2/EC93's dims), rows 32-47 (the equip sub-mode's gear
  // and stat rows, shown only by the equip set's bands).
  for (int c = 0; c < 32; c++) { b.w[2][c] = s->v1[2][c]; b.w[24][c] = s->v1[24][c]; b.w[25][c] = s->v1[25][c]; }
  for (int r = 32; r < CT_SHOP_BG1_ROWS; r++) for (int c = 0; c < 32; c++) b.w[r][c] = s->v1[r][c];
  shopDrawBg1(h, fb, s, &b, qtyOpen, 0);                                         // BG1 prio 0 (the shield cells)
  // The description (C2/F2F3 -> C2/FA49 into the BG3 buffer $4E84; mShopDesc / mShopDescTick): the ROM's stored line
  // ($CC:2EB1) in the BG3 menu text palette, as many characters as the string task has typed.
  if (s->descCur && s->descN > 0) {
    char desc[48];
    if (mItemDesc(s->descCur, desc, sizeof desc)) {
      if (s->descN < (int)strlen(desc)) desc[s->descN] = 0;                     // the characters typed so far
      if (h->vwfDraw) h->vwfDraw(fb, desc, SHOP_DESC_X, SHOP_DESC_Y, ctLoaderVwfTone);   // C2/FA49 is a VWF render
      else { mDim = 4; mText(h, fb, desc, SHOP_DESC_X, SHOP_DESC_Y); mDim = 0; }
    }
  }
  // OBJ prio 2: the party (C2/DC1E: block x = C2/DC74[k] = 0x20 * (k + 1), y = 0xC0; OAM attr 22/24; FF/F628's halved
  // palette on a dimmed member), then BG1 prio 1 (every text cell, attr 20/21/29/3D) over them, then OBJ prio 3: the
  // hand (attr 30) and the down arrow (B0), then BG3's priority-1 text.
  for (int k = 0; k < s->nRoster && k < 7; k++) {
    int c = s->roster[k] & 7;
    mSprWpn = s->wpnOn ? s->wpnPal[c] : NULL;                                    // C2/F5A7 has run: its weapon colours
    mBlitPartySpr(h, fb, c, 0x20 * (k + 1), 0xC0, s->sprDim[c]);
    mSprWpn = NULL;
  }
  shopDrawBg1(h, fb, s, &b, qtyOpen, 1);
  // the lists' scrollbar (C2/8BB8, OAM 30 / 31, priority 3), behind the hand and the down arrow (lower OAM entries)
  if (s->sbTask) CtMenuRenderScrollbar(h, fb);
  int hx, hy;
  shopNode(s, s->cell, &hx, &hy);
  mBlitRGBA(fb, itemCursorRGBA, ITEMCURSOR_W, ITEMCURSOR_H, hx, hy);
  if (s->arrowRun && !s->arrowOn) {                                              // C2/8ABA: OAM 29 at node 11 +2/-1
    int ax, ay; shopNode(s, 11, &ax, &ay);
    mBlitRGBAFlipV(fb, ctMenuArrow, 16, 16, ax + 2, ay - 1);                     // tile 22, attr B0 (v-flip)
  }
  shopDrawBg3(h, fb, s);
}
