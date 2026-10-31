// menu.h -- the X-button menu (field C0/1920/1923 -> C0/192F-1960 -> JSL C2/8000 with A = 1, the mode-1 menu
// program; exit C2/82B4 RTL -> field resume C0/1964-1984). disasm/notes/menu_entry.md.
//
// The module reads the engine's $7E image, the same RAM the ROM menu reads:
//   party        $7E2980-82 (bit 7 = empty slot)
//   char sheet   $7E2600 + charid*0x50: +3 curHP, +5 maxHP, +7 curMP, +9 maxMP (words), +0x12 level
//   gold         $7E2C53, 24-bit LE (read C2/FCD0, shop spend C2/FD08, writers C2/FDA1..FEDB, battle C1/D0B3)
//   play clock   $7E0400 frames / $0401 seconds / $0402-03 minute digits / $0404-05 hour digits / $0406 overflow;
//                ticked by C2/85AE while $0407 == 0 (wrap limits C2/85D0 = [60,60,10,6,10,10]). C2/F28E shows
//                HH:MM, or 99:59 when $0406 != 0.
#ifndef CT_MENU_H
#define CT_MENU_H

#include <stdint.h>

typedef struct CtMenuPanel {
  int charId;                // $7E2980+slot (valid only when the slot is occupied)
  int lv;                    // sheet +0x12
  int hp, hpMax, mp, mpMax;  // sheet +3/+5/+7/+9 (16-bit LE words)
  const char* name;          // engine name table (the ROM's $7E2C23 name buffers)
} CtMenuPanel;

typedef struct CtMenuHost {
  uint8_t* mem;              // the engine's $7E image (sheets/party/gold/clock live here)
  const uint8_t* glb;        // the engine's $7F image: the Form screen reads $7F:01DF, the locked-member mask
                             //   (FF/F9BB[id] = 0x80 >> id), to build C2/CD2B's panel array. NULL = no locks.
  // Renderer hooks: the field's window chrome and font.
  void (*drawWin)(uint8_t* fb, int x, int y, int w, int h);  // CT window chrome (CT_ASSET_WINDOW 9-patch)
  void (*drawGlyph)(uint8_t* fb, int code, int x, int y);    // 8x8 menu-font glyph, CT charset code (bright pal0)
  void (*drawGlyphDim)(uint8_t* fb, int code, int x, int y); // same glyph in the dim pal4 (non-focused party panel)
  void (*drawGlyphDim2)(uint8_t* fb, int code, int x, int y);// a lighter dim (140,148,148): Equip non-selected gear
  void (*drawGlyphYellow)(uint8_t* fb, int code, int x, int y);// yellow (ROM FF/CBB4): field-usable item names
  void (*drawGlyphCyan)(uint8_t* fb, int code, int x, int y); // BG palette 2 (0,255,255): a stat the previewed gear raises
  void (*drawGlyphSave)(uint8_t* fb, int code, int x, int y); // Save-screen text (entered-Save CGRAM pal4: 231 body;
                             // the non-cursor rows are dimmed by CtMenuRenderSave's half-math band, not a palette)
  const uint8_t* portrait;   // lead PC portrait blob ([w u8][h u8] + RGB) or NULL
  const uint8_t* arrowTri;   // the ribbon selection arrow = name-cursor triangle ([w u8][h u8] + RGBA),
                             // ROM $FF/9D70 (OAM tile 0x22); NULL = skip the arrows
  // The seven characters' standing sprites by charId 0..6 ([w u8][h u8] + RGBA, alpha 0 = transparent), or NULL.
  // The menu draws each character's own field sprite: the OBJ tiles and palette (indices 1..12) the menu uploads
  // match CT_ASSET_SPRITE[charId] at its standing assembly frame.
  const uint8_t* partySpr[7];
  // The menu sprite animator's hooks. The machine (C2/F643's task, C2/F69B's advance) is in menu.c; the art and
  // the animation tables come from the host:
  //   spriteAnim[charId] = CT_ASSET_SPRITEANIM[charId]: [nAnims u8], then per anim [n u8] + n x {U,D,L,R assembly
  //     frame u8, delay u8}. These are the ROM's tables (Lucca anim 1: E4/39A0 frames, E4/AA24 delays).
  //   partySprFrame(charId, asmFrame, cropX, cropY) = the sprite composed at any assembly frame (the ROM's
  //     block+0x03), in partySpr's shape, plus the crop's origin in the compose canvas. The ROM anchors the OBJ
  //     block to the canvas, so a frame with a different ink box is offset by the difference, not re-centred.
  //     NULL, or a NULL return, falls back to partySpr.
  const uint8_t* spriteAnim[7];
  unsigned       spriteAnimLen[7];
  const uint8_t* (*partySprFrame)(int charId, int asmFrame, int* cropX, int* cropY);
  // anySprFrame(sprId, asmFrame, cropX, cropY) = CT_ASSET_SPRITE[sprId] (any sprite: the $E4:F000 header's index) composed at
  //   that assembly frame, cropped like partySprFrame; (cropX, cropY) = the crop's corner in the composer's canvas, whose
  //   (CT_SPR_OX, CT_SPR_OY) is the object's position.
  const uint8_t* (*anySprFrame)(int sprId, int asmFrame, int* cropX, int* cropY);
  const uint8_t* handCursor; // the party-panel selection hand = ROM $FF/9CF0 (CT_ASSET_NAMESPRITES id 0,
                             // [w u8][h u8] + RGBA); NULL = a plain triangle marker
  const uint8_t* rom;        // the loaded ROM image (battle.c CtBattleRom): item names from the $CC/0B5E table;
  long romLen;               //   NULL/short = names shown as ids
  // The dialogue VWF (12x12 proportional, CT_ASSET_FONT): the load screen's slot descriptions and era name use
  // these glyphs and advances. NULL = the 8x8 menu font.
  int  (*vwfWidth)(const char* s);
  void (*vwfDraw)(uint8_t* fb, const char* s, int x, int y, const uint8_t tone[4][3]);
  const uint8_t* sram;       // the 8 KB SRAM mapped at $30:6000-7FFF (offset = addr - 0x6000). Three slots at the
  long sramLen;              //   ROM's bases $FF:FBE1 = 0x0000/0x0A00/0x1400 (stride 0x0A00), checksums/signatures
                             //   at 0x1FF0/0x1FF8. The field host owns and persists it; NULL reads every slot empty.
  const uint8_t* sramPanel;  // the SRAM the Save screen's summary panel was last drawn from (C2/D32C), when it lags
                             //   the rows (a frame, after a save); NULL = h->sram
  const uint8_t* menuFont2bpp; // the raw menu font (CT_ASSET_MENUFONT: 2bpp, 16 B per tile, tile = char code),
                             //   for screens that compose BG1 line by line (the shop: its HDMA bands show 8-line
                             //   tile rows at 12-line pitch). NULL = those screens draw no text.
} CtMenuHost;

// Ribbon cursor step. C2/E9E5 reads the neighbour graph at $7E9380 (+2 Up, +3 Dn, +4 Lf, +5 Rt); for the six-in-a-
// row layout Lf/Rt are +-1 mod 6 and Up/Dn stay (menu_phase2.md R3/sec. 2). dir +1 = right, -1 = left (both wrap),
// 0 = up/down (no move). Returns 0..5.
static inline int CtMenuRibbonStep(int opt, int dir) {
  if (dir > 0) return (opt + 1) % 6;
  if (dir < 0) return (opt + 5) % 6;
  return opt;
}

// The per-line 5-bit COLDATA ramp that C2/86DD's HDMA generator builds for one ribbon option's window list
// ($FF:BBF2): 224 signed entries, +8 at each window's top down to -7 at its bottom, normalised per window (so the
// status screen steps every 12 lines and the Form screen every 3).
const int8_t* CtMenuWinGradRamp(int opt);
// The Form screen's panel geometry: the text column (x 72, tile col 9) and the standing sprite's anchor for panel
// 0..2 -- C2/CE6E's word at index $12 + 2*panel in the field convention (X = centre, Y = feet): centre 168, feet
// 56 / 104 / 152. Any pointer may be NULL.
void CtMenuFormGeom(int panel, int* textX, int* feetY, int* centreX);
// Where a party sprite's opaque box lands for (charId, centre x, feet y), as mBlitSprAnchored blits it. Returns 0
// when the host has no art for that character.
int CtMenuSprBox(const CtMenuHost* h, int charId, int cx, int feetY, int* x0, int* x1, int* y0, int* y1);
// The BG2 menu chrome (marble desktop, the 9-slice window panels, the ribbon box), replayed from the captured
// tilemap (src/menu_chrome.h). screen 0 = the ribbon (opt 0..5 picks that option's layout), 1..6 = a sub-screen.
// mode 0 = all but the ribbon box, 1 = only the ribbon box (drawn over the content), 2 = everything.
void CtMenuChrome(uint8_t* fb, int screen, int opt, int mode);
// Set the char-select context (party at $2980, the selected char) so CtMenuChrome tones the party-panel boxes by
// selection (the cursor's member is brighter). Pass (NULL, -1) to disable.
void CtMenuSetCharSel(const uint8_t* mem, int selChar);
// The Equip/Status detail chrome (three-box left column), used for screen 1 sub 1.
void CtMenuChromeEquip(uint8_t* fb);
// The entry slide (C2/A71E) is CtMenuRenderEquipEntry, below.

// Panel data for party slot 0-2 from the $7E image. Returns 1 when the slot is occupied ($2980+slot bit 7 clear).
int  CtMenuPanelData(const uint8_t* mem, int slot, CtMenuPanel* out);

// One 256x224 RGB frame of the mode-1 main screen: three PC panels (name, LV, HP, MP; an empty window for an empty
// slot), the TIME/G box, the lead portrait, and the six-icon ribbon with option menuOpt (0..5) highlighted (+0x20
// tile) between its two arrows (menu_phase2.md sec. 3).
void CtMenuRender(const CtMenuHost* h, uint8_t* fb, int menuOpt);

// Sub-screens: A commits screen = menuOpt+1 (C2/9A77-9A81 $0067 := opt+1); B returns to the ribbon (C2/9A98 STZ
// $0067).

// Screen 2 = Item (C2/AA34, builder C2/ADD0): the "Use/Move" header, then a row per held item [icon][name][xN].
// Inventory = $7E2400 ids (packed, 0-terminated, cap 0xF2) + $7E2500 counts. Names come from $CC/0B5E ("Item xx"
// without a ROM). `cell` = the cursor $0054 (rows 0x00-0x09, "Use/Move" 0x0A, "Organize" 0x0B, members 0x0C..0x0E);
// `scroll` = $0416, the top slot; `pickedSlot` = the Move-lifted slot ($0F00, -1 none); `targetMode` = the Use target
// overlay is up; `partyN` = active party size. `barBlinkOn` is the scrollbar's phase (C2/8BB8: 8 on / 8 off, $0D96),
// separate from the pinned hand's 4/4 ($0DA3). `descId`/`descN` = the description line: item $0D77 and the characters
// C2/FA49 has typed. `slideO` = the one-row slide's $0DAB (C2/AC8A: +3/6/9 Down, -3/6/9 Up; 0 = none).
void CtMenuRenderItem(const CtMenuHost* h, uint8_t* fb, int cell, int scroll,
                      int pickedSlot, int targetMode, int partyN, int pinnedBlinkOn, int barBlinkOn,
                      int descId, int descN, int slideO);
int  CtMenuItemDescLen(int id);                      // C2/FA49's steps for item id's line (decoded characters)
int  CtMenuStrLen(int kind, int id);                 // the same for table kind 0 = items ($CC:2EB1), 1 = techs ($CC:3A09)

// Item screen interactions (the ct-field-item-menu-use notes):
void CtMenuItemOpenGate(const uint8_t* mem, const uint8_t* glb);   // C2/9969 at the menu open ($77C8 bit 1)
int  CtMenuItemUsable(int id);                       // field-menu usable? ($7700[id] bit1)
int  CtMenuItemUse(uint8_t* mem, int absSlot, int targetSlot);  // apply effect + consume; 1 if used
// The two halves the Item screen runs 42+ frames apart: C2/8E2D's effect (with C2/9016's pop-up numbers, $0F5D bits
// returned) and C2/B31C's consume.
int  CtMenuItemApply(uint8_t* mem, int absSlot, int targetSlot, int popAmt[3][2]);
void CtMenuItemConsume(uint8_t* mem, int absSlot);
// The Tabs (C2/B200's loop): X = `$0DBD` (1 Power, 6 Magic, 2 Speed; 0 = not a Tab), C2/B275's validity, C2/B2E0's apply.
int  CtMenuItemTabIndex(int id);
int  CtMenuItemTabMask(const uint8_t* rom, long romLen, const uint8_t* mem, int absSlot, int tgtSlot);
void CtMenuItemTabApply(const uint8_t* rom, long romLen, uint8_t* mem, int tgtSlot, int x);
// The target overlay (C2/AF42's offset-per-tile window, C2/AFED's panels as `shown` HP/MP, the party, the hand) and
// C2/9016's pop-up digits: popCurve = the FF/CEBA index this frame (-1 none), popPool 1 HP / 2 MP.
// The equipment info panel (Item substate 5, C2/AE91): "Can be used by" + the names + the Attack/Defense value.
void CtMenuRenderItemInfo(const CtMenuHost* h, uint8_t* fb, int itemId);
void CtMenuRenderItemOverlay(const CtMenuHost* h, uint8_t* fb, int tgt, const uint16_t shown[3][2],
                             int popCurve, int popPool, const int popAmt[3][2], long popPal, int multiBits);
void CtMenuItemSwap(uint8_t* mem, int a, int b);     // Move: swap two slots (ids+counts), C2/AAE3
// C2/B870: $7E:0DBC, the target overlay's validity mask, recomputed every frame: bit 1 = the target's HP can be
// restored, bit 0 = its MP can. Zero means the A press buzzes (C2/B1BA) and nothing is consumed. All-party items
// return on the first party member that would gain something.
int  CtMenuItemTargetMask(const uint8_t* mem, int absSlot, int tgtSlot);
int  CtMenuItemAllParty(const uint8_t* mem, int absSlot);   // $0DBD bit 3: C2/8AD5's three hands, not the nav one
void CtMenuSetNavHandShown(int on);                          // C2/E954's $0D9B gate for the Item screen's nav hand
void CtMenuItemOrganize(uint8_t* mem);               // Organize: sort/merge/compact, C2/AB62

// Screen 1 = Equip/Status (C2/9C29):
//  (a) character select: the hand rests on a party panel; the cell = $7E0054 (working) / $7E0412 (committed), the
//      active-party slot; Up/Down cycle the occupied slots (wrap); A -> detail; B -> ribbon ($0067 1 -> 0).
//  (b) detail: the picked character's page. B -> character select.
// CtMenuRenderCharSelect draws the three party panels, the selected char's portrait (h->portrait) and the hand on
// `selSlot` (0..2).
void CtMenuRenderCharSelect(const CtMenuHost* h, uint8_t* fb, int selSlot, int dimSlot, int hand);   // dimSlot = $0412 (C2/9E76)
// C2/A85C: the gear panel's way back, step `j` (0 = the setup frame .. 23) -- see menu.c.
void CtMenuRenderEquipExit(const CtMenuHost* h, uint8_t* fb, int charId, int slot, int selSlot, int j);

// CtMenuRenderEquip draws the Equip screen: `charId`'s panel, the slot-type label (Weapon/Helmet/Armor/Accessory
// for cursor `slot` 0..3), the detail with the cursor slot's item bright, and the hand. `stage` 1/2 = the two
// frames the panel is built in (C2/A321, C2/9C5E), 0 = settled. `cand`/`cnt`/`n` = the candidate list substate 3
// uses: C2/9C61 builds (C2/8953) and draws (C2/A569) it while the cursor is still on the gear rows, 32 px lower than
// in substate 3. NULL/0 only where there is no bag to read.
void CtMenuRenderEquip(const CtMenuHost* h, uint8_t* fb, int charId, int slot, int stage,
                       const uint8_t* cand, const uint8_t* cnt, int n);
// C2/A71E + C2/A7B7: the gear panel's entry slide, step `k` (1..23) -- the member select with the new column risen
// through the offset-per-tile band. selSlot = $0412, dimSlot = the member C2/9E76 last left bright.
void CtMenuRenderEquipEntry(const CtMenuHost* h, uint8_t* fb, int selSlot, int dimSlot, int k);
// C2/9DAF: the member select's one-member scroll: the column (lines 16-159, screen columns 2-13) moved `o` px
// (1..44, 4 a frame) over the old window (`oldScroll`) and the incoming member (below for dir +1, above for -1);
// `dimRow` = the row C2/9E76 last left bright, `handRow` = the cursor, `detailChar` = the member C2/A22F last drew.
void CtMenuRenderEquipMemberSlide(const CtMenuHost* h, uint8_t* fb, int oldScroll, int dir, int o, int dimRow,
                                  int handRow, int detailChar);
// C2/A4AF: the list box's 8-frame climb into the candidate list (s = 4..28), over the gear panel's chrome.
void CtMenuRenderEquipListRise(const CtMenuHost* h, uint8_t* fb, int charId, int slot, int s, int gearHand);
#define CT_MENU_EQ_LIST_RISE_PX 32   /* the climb's length: C2/A4AF loops while $96A3 < 0x20, 4 px a pass */

// ---- Screen 1, the gear swap (disasm/notes/menu2_equip_change.md) ----
// C2/9C29 has four substates: 0 build, 1 char select, 2 gear-slot select, 3 candidate list. A on a gear row enters
// substate 3 when the candidate count is non-zero and buzzes (id $01, C2/9C8D -> C2/EACC) when it is zero.
// Fill cand[]/cnt[] (cap 0xF2) with the $2400 items in the gear slot's range (C2/89DC: Weapon [00,5A) Helm [7B,94)
// Armor [5A,7B) Accessory [94,BC)) that `charId` may equip ($7E:7A00[id] & (0x80 >> charId), C2/93AF), sorted by
// the power byte $7E:7C00, descending. Returns the count ($0FC6).
int  CtMenuEquipCandidates(const uint8_t* mem, int gearSlot, int charId, uint8_t* cand, uint8_t* cnt);
// C2/916E: the 11 derived bytes of a char sheet -- 7 capped stats (out[0..6]: Power/Stamina/Speed/Magic/Hit/Evade/
// M.Def), Attack out[7], Defence out[8], effective max HP out[9..10] -- from the four pieces' $7C00 power and $7B00
// bonus sets. Reads only the sheet, so it also serves the candidate preview (patch a copy's gear byte).
void CtMenuRecalcDerived(const uint8_t* sheet, uint8_t* out11);
// C2/9137 + C2/813C: CtMenuRecalcDerived on the live sheet, stored at +0x36..+0x40; clamps curHP (+0x03) to the new
// max and rebuilds the HP-bar scale $0D38 + charId*2.
void CtMenuEquipWriteBack(uint8_t* mem, int charId);
// C2/9875's pass at every menu open: the bag cleanup C2/9988 and all seven sheets recomputed + written back (C2/99B2).
void CtMenuOpenFlags(uint8_t* mem, const uint8_t* glb);   // C2/9895-98B2: $0D32 / $0D34 / $0D36 at the open
void CtMenuOpenSheets(uint8_t* mem);
// C2/9CC1: give the old piece back, write `newId` into the sheet at C2/A4AB[gearSlot] (Weapon +0x29, Helm +0x27,
// Armor +0x28, Accessory +0x2A), take the new piece out, zero $7F:00B4+charId on a weapon change (C2/9CFC), then
// recompute. Returns 1 when it equipped.
int  CtMenuEquipApply(uint8_t* mem, uint8_t* glb, int charId, int gearSlot, int newId);
// C2/8791 / C2/87D5. Add returns 0 ok / 0xFF full and refuses the three "none" sentinels $5A/$7B/$94: that is the
// ROM's whole unequip rule (equipping into an empty helm/armor/accessory slot consumes the new item and returns
// nothing). Remove returns 0 / 0xFF not held / 0xFE not enough, and zeroes the id slot when the count reaches 0.
int  CtMenuInvAdd(uint8_t* mem, int id, int qty);
int  CtMenuInvRemove(uint8_t* mem, int id, int qty);
// The candidate's bottom-bar line: its stored description ($CC:2EB1, the item table). Returns the length written; 0 (and
// out = "") when the item has no description.
int  CtMenuEquipDesc(int id, char* out, int cap);
// The stored-description table's shape (entries, and how many are non-empty) -- ROM CC/2EB1.
int  CtMenuDescTable(int* nonEmptyOut);
// The candidate list panel: the list replaces the left column (rows at y = 76 + row*12, name x24, hand x8) and the
// highlighted item's bonus line fills the bottom bar. `cell` = the highlighted row. `descN` = the characters
// C2/F2F3's task has typed of item $0D77's description (C2/A5DF runs it on every preview). `prevId` = the candidate
// C2/A5C3 last previewed (-1: the cursor's own); `slideO` = the one-row slide's offset (C2/EB1F: +3/6/9 Down,
// -3/6/9 Up; 0 = none), `scroll` already the new window.
void CtMenuRenderEquipList(const CtMenuHost* h, uint8_t* fb, int charId, int gearSlot,
                           const uint8_t* cand, const uint8_t* cnt, int n, int cell, int scroll, int hand, int prev,
                           int descN, int prevId, int slideO);

// Screen 3 = Tech (C2/B353): char select (hand on a party panel) -> the picked char's tech list.
//   learned bitfield  $7E7F00[charId] (MSB = the char's first tech), read by the list builder C2/BD00; built at
//                     the menu open (FF/F850) from the persistent $7E2837+charId (CtMenuTechBits).
//   tech-id range     $C2/BD40[charId] cumulative boundaries (01,09,11,19,21,29,31,39); bit (0x80>>k) set ->
//                     tech id (start+k) learned (C2/BD00 ASL scan).
//   tech name table   ROM $CC/15C4 + techId*11 (1 = Cyclone ... 8 = Luminaire), the item name table continued.
//   tech MP table     ROM $CC/253B + techId (C2/BC22): ids 1..8 = 2,2,2,4,8,10,12,20.
// CtMenuRenderTech draws the two party panels, the Single/Double/Triple tabs (`tab` = $7E:0418) and the eight-row
// list (C2/BD73) at x144, y = 60 + 12*i, scrolled by `scroll` ($0F4A), with each row's MP for this character and
// the hand on `row` ($0F48; -1 while the list is not entered). `selSlot` >= 0 puts the hand on that party panel
// instead (the char-select level). There is no right-hand detail column; the tabs fill that corner.
// `fresh` = the build frame (C2/B979: no list, no bar yet); `barId`/`barX`/`barN` = C2/FA49's line ($0D77, where it
// types, how many characters it has typed).
// C2/B5C8: the Tech member column's one-member scroll, over a picture CtMenuRenderTech drew for the old member: the
// column's text (BG1 only: the offset-per-tile words are 20xx) and the sprites ($0D30) moved `o` px (4..44) over the
// old window (`oldScroll`) and the incoming member (below for dir +1); the boxes (BG2) stay.
void CtMenuRenderTechMemberSlide(const CtMenuHost* h, uint8_t* fb, int oldScroll, int dir, int o, int handRow);
void CtMenuRenderTech(const CtMenuHost* h, uint8_t* fb, int charId, int tab, int row, int scroll,
                      int selSlot, int fresh, int barId, int barX, int barN);
void CtMenuTechHandCell(int cell);   // the list's hand on nav cell `cell` instead of its row (-1 = the row)
// The Tech list's own handler has run since the A that opened it (C2/B724 JSR C2/BDEB; the A's frame has none).
void CtMenuTechListRan(int ran);

// The group machine (disasm/notes/menu2_tech_screen.md). Fills techId[]/mp[]/group[]/dim[] with the techs `charId`
// sees under `tab` (0 Single / 1 Double / 2 Triple) and returns the count. C2/BCCD + C2/BD00: walk the tab's group
// window (C2/BD65), keep the groups whose CC/2963 character mask holds charId, and emit the ids set in the menu
// bitfield, so a double shows in both participants' lists. mp[] is this character's cost (C2/BC22: a single indexes
// CC/253B by id, a double/triple $7E:1607 + charId*64 + techId), halved by an $A2/$A3 accessory. dim[] marks the
// one not-yet-learned tech FF/F876 previews per character. Any out pointer may be NULL.
int  CtMenuTechList(const CtMenuHost* h, int charId, int tab, int* techId, int* mp, int* group,
                    int* dim, int cap);
// FF/F863 + FF/F876 + C2/B979: the menu's tech bitfield $7E:7F00[37], copied from the persistent $7E:2837 only for
// groups whose every member is in the roster, plus each character's previewed next tech (nextIdx7[charId], 0xFF =
// none; nextIdx7 may be NULL).
void CtMenuTechBits(const uint8_t* mem, uint8_t* bits37, uint8_t* nextIdx7);
// C2/BC22 + C2/BC59: the MP `charId` pays for `techId` under `tab`, halved once by accessory $A2 and twice by $A3
// (both rounding up).
int  CtMenuTechMp(int charId, int techId, int tab, int accessoryId);
// The tech's description (CC/3A09 + id*2, decoded at bake time), C2/BE16's bottom bar. Ids 0x76..0x78 are the
// Single/Double/Triple tab names. Returns the length written.
int  CtMenuTechDesc(int techId, char* out, int cap);
// ---- The field restore (disasm/notes/menu2_tech_effect.md) ----
// C2/8F55 is shared by the field tech cast (C2/B4FC) and the field item use (C2/B1BF); only $0DBF differs.
// C2/8ECC's amount, six words: out6[0..2] = HP per party index, [3..5] = MP. amtSrc = $0DBF (bit 7 set = an item's
// flat constant, clear = the caster's Magic), nib = $0DC0, cur = $7E:0D00.
void CtMenuRestoreAmounts(int amtSrc, int nib, unsigned cur, int* out6);
// FF/F82E's nine field-usable tech ids, minus FF/F876's previewed not-yet-learned one (its marker clears bit 7 of the
// same byte). No double or triple is field-castable.
int  CtMenuTechFieldUsable(const uint8_t* mem, int charId, int techId);
// CtMenuTechUse: C2/B3E6's gate ladder + C2/8F55. 1 = cast (the caller runs the 10-frame pre-roll, the chime and
// the popup), 0 = refused (the caller plays buzz id $01). shown6 receives the floated numbers, -1 for a pool the tech
// does not touch. rngCur = $7E:0D00, advanced once per menu frame.
// CtMenuTechTargetMask: C2/B870 for a tech, the same two-bit $0DBC mask with the pools from the tech's $0DBD, plus
// C2/B839: the mask is zero once the caster's MP is below the cost, so a second A on an emptied caster buzzes.
int  ctTechCastPoseByte(int charId);   // $C2:B519[charId], the pose $19E1 gets (seven entries)
int  CtMenuTechTargetMask(const uint8_t* mem, int casterCharId, int techId, int tab, int tgtSlot);
int  CtMenuTechUse(uint8_t* mem, int casterCharId, int techId, int tab, int tgtSlot,
                   unsigned rngCur, int* shown6);
// C2/B502-B516: the caster pays the MP after C2/8F55 has returned, i.e. after the popup (CtMenuTechUse does not).
void CtMenuTechPay(uint8_t* mem, int casterCharId, int techId, int tab);
// The target overlay (substate 4) as C2/BE79 / C2/BEDC build it; see menu.c.
void CtMenuRenderTechTarget(const CtMenuHost* h, uint8_t* fb, int charId, int tab, int row, int tgt,
                            const uint16_t shown[3][2], int casterMp, int castPose,
                            int popCurve, int popPool, const int popAmt[3][2], int barId, int barX, int barN);

// Decode single tech `techId`'s name from ROM $CC/15C4 + techId*11 (1 = Cyclone), with CtMenuItemName's decoder.
// Returns the length written (0/"" if rom is NULL/short). cap should be >= 12.
int  CtMenuTechName(const uint8_t* rom, long romLen, int techId, char* out, int cap);

// Screen 4 = Config (C2/BF35). The ROM screen is a grid of icon toggles and a bottom bar with the highlighted
// setting's name and value; the port draws a text list (name + value per row) and the same bar.
//   settings bytes   $7E0D86..$0D8F+ (the working copy the cell handlers C2/C017.. toggle, mirrored to $0F09
//                    for the checkbox draw C2/C3E4).
//   per-cell offset  C2/C4F9[cell]: which $0D86 byte holds the setting (C2/C4BC `LDA $0D86,Y`).
//   per-cell base    C2/C505[cell]: value string index = config byte + base (C2/C4BC `ADC $C2C505,X` -> $0DCF).
//   string table     FF/D3FE, 16-bit LE pointers (bank FF): entry i (i < 12) = the setting name, entry
//                    (base + config byte) = the value string (On/Off/N/Y/1..8/Standard/Custom/Wait/Active/...).
//                    Strings at FF/D462+, NUL-terminated, CT encoding (0xE0 '/', 0xE8 '.', 0xDF a cursor icon,
//                    dropped in the text list).
#define CT_MENU_CONFIG_COUNT 12
// Decode config setting `idx` (0..11, grid order): name -> name[nameCap], value -> val[valCap], from h->rom and the
// live $7E0D86 bytes. Returns 1 for a valid idx, else 0 (buffers cleared).
int CtMenuConfigItem(const CtMenuHost* h, int idx, char* name, int nameCap, char* val, int valCap);

// ---- Screen 4 input (disasm/notes/menu2_config_toggles.md) ----
// A 21-cell cursor machine over 12 settings packed into three saved bytes. $0054 = the cell, $0068 = the substate
// (1 = the 2x5 grid, 2 = the Control-Pad button editor, 3 = the Battle-Gauge row), $0D86..$0D91 = the twelve working
// bytes (valid while the menu is open), $0F09..$0F12 = the checkbox mirror (its slots $0F0D/$0F0E/$0F0F are the
// sub-mode flags, not settings), and $2990/$2991/$2992 = the persistent packed config, inside save record 0.
// Nothing reaches the packed bytes while on the screen: B (its only exit; X/L/R/Select/Start do nothing) runs the
// C2/934F pack on the way out.
#define CT_MENU_CONFIG_CELLS 21          /* ROM nav table FF/B5A0: 21 records of 6 bytes */

// C2/92F4, once per menu open from C2/8133: $2990/$2991/$2992 -> the twelve $0D86..$0D91 bytes (cleared first).
// $0D91 has no packed source and is never written, so cell 5's value (base 0x1E + $0D91) is the empty entry and
// "Battle Speed" shows no value.
void CtMenuConfigUnpack(uint8_t* mem);
// C2/934F, run only from C2/BF67 (B out of the config screen): the twelve bytes -> $2990-92.
void CtMenuConfigPack(uint8_t* mem);
// C2/C3B5 + C2/C43A, the screen build: copy the nav table FF/B5A0 to $7E:9380 (the ROM goes through the display-list
// interpreter C2/ED31), mirror $0D86..$0D8F -> $0F09..$0F12, and zero the sub-mode flags $0F0D/$0F0E/$0F0F.
void CtMenuConfigBuild(uint8_t* mem);
// C2/95D4..C2/960A, the new-game defaults: 12 bytes from C2/FCA6 -> $2990..$299B and 9 bytes from C2/FCA9 ->
// $0408..$0410 (the same nine; the ROM blocks overlap at +3).
void CtMenuConfigDefaults(uint8_t* mem);
// C2/C54F: rebuild the live button map $2993..$299B -- the defaults C2/FCA9, overwritten by the custom map
// $0408..$0410 when Control Pad ($0D87) is Custom.
void CtMenuConfigRebuildLive(uint8_t* mem);
// C2/968D's second C2/9542 copy: FF/C9FC (0x100 bytes) over tiles 0x30-0x3F of the menu program's font -- the
// C2/8096 init every C2:8000 entry runs. A host lays these 256 bytes over its FF/8C60 font at tile 0x30.
const uint8_t* CtMenuFontOverlayC9FC(void);
// C2/816A + FF/F813's length: the roster lists the open builds -- $0D5F[] the present charIds packed (FF-filled first,
// C2/8115), $0D68[] their $2980 slots, $73 the active count, $75 the whole roster, $85 the char-select list length.
void CtMenuRosterLists(uint8_t* mem);
// The member window the panel column draws: the scroll into C2/816A's roster list ($0413 on the Equip screen), or -1
// for the three active slots in order. Set by the host before a frame's render, like CtMenuSetCharSel.
void CtMenuSetPanelWindow(int scroll);
// The list machinery every scrolling menu list shares (Item, Equip, Tech, the shop), on the ROM's WRAM bytes.
// C2/EA81: the window of a list of `count` rows seen `page` at a time, the first cell $0DDB set by the caller:
// $0DD8 page, $0DDC last cell, $57 / $0D94 / $0DDA the scroll range, $0D98 = the caller's $4204 `dividend` over
// (count - page) -- the scrollbar's 8.8 step.
void CtMenuListWindow(uint8_t* mem, int page, int count, unsigned dividend);
// C2/93BF: the L/R page on $0D1D's page bits (`pageBits`: bit1 R = a page back, bit0 L = a page on) from the scroll
// the caller staged in $0DD9; a clamp at either end also puts the cursor (*cell = $54) on $0DDB / $0DDC. Returns 1
// when the scroll moved ($0DD9 / $0D95 = the new scroll, $81 = FF).
int CtMenuListPage(uint8_t* mem, int pageBits, int* cell);
// C2/8BB8's picture: the scrollbar task's two arrows from $0D92-$0D9A (see menu.c).
void CtMenuRenderScrollbar(const CtMenuHost* h, uint8_t* fb);
// C2/E9E2 + the nav records at $7E:9380 (6 bytes per cell: [x, y, up, down, left, right]).
// dir: 0 = Right, 1 = Left, 2 = Down, 3 = Up (the ROM's $0D1E index order). Returns the new cell.
int  CtMenuConfigNav(const uint8_t* mem, int cell, int dir);
// The cell's cursor anchor, from its nav record's first two bytes (C2/E964 `LDA $9380,X / STA $0770`, the hand's
// x/y). The port's hand sits at (x, y - 8).
void CtMenuConfigCellPos(const uint8_t* mem, int cell, int* x, int* y);

// One frame of the config screen, as C2/BF45: the cursor moves first (C2/E9E2), then A/B are decoded (and eaten on
// a frame the substate changed, C2/E98D), then the cell handler runs from the C2/BFFF table. Returns 1 when B has
// left the screen (the pack has run); the caller then does the C2/9A98 return to the ribbon.
typedef struct CtMenuConfigIO {
  int a, b;          // in: A / B this frame (edges: this screen has no auto-repeat)
  int dir;           // in: 0 Right / 1 Left / 2 Down / 3 Up, -1 = none ($0D1E, 0x80 = none)
  int start;         // in: Start (the button editor's "restore defaults", C2/C188)
  int blip;          // out: post the menu blip (C2/EAC2 id $00). This screen has no buzz.
  int apuStereo;     // out: >= 0 -> post APU cmd $F3 with this parameter (cell 0 only, C2/C028)
  int winLoad;       // out: C2/C511 ran -- the window tiles + colours are reloaded from $0D8C (cell 6's edit)
} CtMenuConfigIO;
int CtMenuConfigFrame(uint8_t* mem, int* cell, int* substate, CtMenuConfigIO* io);

// CtMenuRenderConfig draws the Config screen: a header, the 12 settings as a name + value list with the hand on
// `cell` (0..20, $0054, placed from that cell's nav record), and a bottom bar for the setting `$0417` describes.
// `sub` = $0068 (1 grid / 2 button editor / 3 the Battle-Gauge row) picks the panel the hand may sit on. The ten
// checkboxes read the $0F09..$0F12 mirror with C2/C42F's polarity (slot 0 inverted: Stereo On is 0).
// Not ported: the ROM's icon grid pixels.
// `fresh` = the build frame (no bar); `nameN`/`valN` = the characters C2/F943's task has typed (-1 = the whole string).
void CtMenuRenderConfig(const CtMenuHost* h, uint8_t* fb, int cell, int sub, int fresh, int nameN, int valN);
int  CtMenuConfigBarVal(const CtMenuHost* h, int idx);   // $0DCF of setting idx (C2/C4BC)
// The ribbon's colour window 2 (FF/BAAA via HDMA channel 1) kept for a build frame that leaves $0D13 alone.
void CtMenuChromeRibbonWindow(const CtMenuHost* h, int on);
// The window colour v (0-7) in VRAM/CGRAM: C2/C511 + C2/ED08 (tiles 0x3DC-0x3EF from FF/9E10, row 0 colours 8-15 from
// FF/B210); dimV = the colour the last C2/A0E7 halved into row 4 (-1: none this session). Cached.
void CtMenuWindowColourLoad(const CtMenuHost* h, int v, int dimV);
// The Save slots' own window colours (C2/D266 `$0D79`): colour3 = the three, or NULL = read from h->sram (a valid slot's
// +0x0591 & 7, an empty one 0). Drawn by the Save chrome (tiles 0x300 + 20 v, row slot + 1's colours 8-15).
void CtMenuSaveSlotColours(const CtMenuHost* h, const int* colour3);

// Screen 5 = Form, the party exchange (C2/C621): the three active-party panels and a picker that swaps two members.
// The substates ($0068):
//   picker idle (C2/C65D): Up/Down move over the active slots; A -> first pick (C2/C674: the cursor slot $0F0D ->
//     $0F00); B -> ribbon (C2/C6B3 -> C2/9A98).
//   second-pick idle (C2/C6B9): A -> second pick (C2/C6D5: $0F0D -> $0F01) then the swap; B cancels the pick.
// `cursor` = the active slot (0..2) under the picker; `picked` = the first pick (-1 = none). Reserves are screen $07's.
// `panel9` is C2/CD2B's $7E:0F02..$0F0A array (CtMenuFormPanels): it decides which panels draw dim (locked or
// empty). The entered-Form BG1 tilemap equals the ribbon's opt-4 hover, and BG2 differs only where the ribbon box
// was, so the Form frame is that frame minus the box.
// `pickBlink` is the 4-on/4-off phase of the picked member's second hand ($7E:0DA0, the counter the Item screen's
// pinned hand also uses): non-zero = drawn.
void CtMenuRenderForm(const CtMenuHost* h, uint8_t* fb, int cursor, int picked, const uint8_t* panel9,
                      int pickBlink);
// C2/CD2B's panel array $7E:0F02..$0F0A: 0x80 empty, 0x40|i locked ($7F:01DF & (0x80 >> charId)), i selectable.
// Entries 3..8 are always 0x80 in the X menu: $0D36 is 0xFF there (C2/989E), so C2/CD72 never runs the reserve
// scan; reserves are menu mode 5 / screen $07.
void CtMenuFormPanels(const uint8_t* mem, const uint8_t* glb, uint8_t* panel9);
// C2/C649: the cell the Form picker opens on, the first panel entry with neither the locked (0x40) nor the empty
// (0x80) bit.
int CtMenuFormEntryCell(const uint8_t* mem, const uint8_t* glb);
// Screen 7 = the exchange with reserves (menu mode 5; C2/C7A6): the field's Y at storyline >= 0x49 and the script's
// `C8 00`. The Form machinery on the menu's WRAM with $0D36 = 0 (disasm/notes/menu2_form_screen.md 2.11):
//   CtMenuExchRoster  C2/CD2B: $0F02..$0F0A (actives from $0F02, reserves from $0F05), $0F0B, $0F0C
//   CtMenuExchEntry   C2/C649 from X = 0: $54, both picks FF
//   CtMenuExchGraph   the setup list's copy of the cursor graph $FF:B77A -> $7E:9380
//   CtMenuExchMove    C2/E9E2 + C2/C805 (C2/C85B, C2/C9AE): `dir` = $F5; returns 1 when $54 moved (the blip)
//   CtMenuExchPick    C2/C674 (refused on a locked entry, C2/C7F7): returns 0 when refused
//   CtMenuExchSecond  C2/C6D0 / C2/C6DE-C70A: `a` = A (else B); the swap C2/C70C + C2/816A + C2/CD2B; returns 1 if swapped
void CtMenuExchRoster(uint8_t* mem, const uint8_t* glb);
void CtMenuExchEntry(uint8_t* mem);
void CtMenuExchGraph(uint8_t* mem, const uint8_t* rom, long romLen);
int  CtMenuExchMove(uint8_t* mem, int dir);
void CtMenuExchValidate(uint8_t* mem);      // C2/C805 alone (the build's, on the entry cell)
int  CtMenuExchPick(uint8_t* mem);
int  CtMenuExchSecond(uint8_t* mem, const uint8_t* glb, int a);
void CtMenuExchPrompt(uint8_t* mem, const uint8_t* rom, long romLen);   // C2/C9E5's prompt: C2/F905 + C2/FBB4 (at S0)
void CtMenuExchWaveNext(int due);   // the task steps next frame: the picture's HDMA reads that step's table
void CtMenuExchFigure(int on);   // block 7's figure on screen (from P, when the task C2/F657 uploads its tiles)
void CtMenuExchWaveInit(uint8_t* mem, const uint8_t* rom, long romLen);   // C2/CE86's FF/F677 + C2/CE9D: BG3's wave set up
void CtMenuExchWaveTick(uint8_t* mem, const uint8_t* rom, long romLen);   // FF/F716: the task's step (every second frame)
// Screen $07's picture from the menu's WRAM (the chrome, the panels, the members, the hands); menu.c lists what is open.
void CtMenuRenderExchange(const CtMenuHost* h, uint8_t* fb, int pickBlink);
// C2/CC4F: give CtMenuChrome the live panel array so the Form views' per-panel dim follows the current roster, not
// the one the chrome tilemap was captured on. NULL leaves the map alone.
void CtMenuSetFormPanelDim(const uint8_t* panel9);
// ---- The menu sprite animator ----
// C2/F643 spawns one task (C2/F657) that walks the nine sprite blocks at $7E:1800 + 0x40*n once a frame; C2/F69B is
// its advance. Only the Form screen asks for an animation (C2/C973): the exchange cursor's member walks on the spot.
//   CtMenuSprReset      -- every block back to anim 0 and its standing pose (call on menu open)
//   CtMenuSprFormAnims  -- C2/C973: everyone still, the cursor's member walking (anim 1), the picked member on
//                          anim 0x0B
//   CtMenuSprTick       -- one frame of C2/F657 + C2/F69B, over all seven characters
//   CtMenuSprPose       -- the assembly frame that character is showing right now (block+0x03)
void CtMenuSprReset(const CtMenuHost* h);
void CtMenuSprFormAnims(const CtMenuHost* h, int cursorCell, int pickedCell);
void CtMenuSprTick(const CtMenuHost* h);
int  CtMenuSprPose(const CtMenuHost* h, int charId);

// The Form screen's entry gate (C2/C627) and the roster builder behind it (C2/CD2B):
// activeFree = $0F0B (active members not locked by $7F01DF & (0x80>>charId)); reserveN = $0F0C (non-empty
// $2983..$2988, scanned only when $0D36 == 0). CtMenuFormCanOpen is the ROM's predicate ($0F0B + $0F0C >= 2); when
// false the ROM refuses screen 5 ($0067 back to 0, $0068 = 1). `glb` is the $7F0000 story-flag page (CtSceneMemGlb).
void CtMenuFormRoster(const uint8_t* mem, const uint8_t* glb, int* activeFree, int* reserveN);
int  CtMenuFormCanOpen(const uint8_t* mem, const uint8_t* glb);
// The exchange swap, C2/C70C: swap the active-party bytes $7E2980+a and $7E2980+b (LDX $0F00; LDA $2980,X; STA $00;
// LDY $0F01; LDA $2980,Y; STA $2980,X; LDA $00; STA $2980,Y). a/b outside 0..2 (or NULL mem) = no-op.
// Not ported here: the field's PC-object reconcile after the menu (respawn C0/597D); the new order reaches the field
// at the next scene load through $2980.
void CtMenuFormSwap(uint8_t* mem, int a, int b);

// The Save screen's entry gate (C2/CEDC, fed by the menu-open pass C2/9875): saving is allowed on the overworld
// ($0100 >= 0x1F0), or on a field map when $7F01CE bit 15 (the save-point flag) is set, and only while none of
// $0294/$0299/$029E has bit 6. When false the ROM refuses screen 6 ($0067 back to 0, $0068 = 1).
int  CtMenuSaveCanOpen(const uint8_t* mem, const uint8_t* glb);

// Screen 6 = Save (C2/CED4 -> slot loader C2/D0E5; slot scan C2/D156-D197): the three slots with their summary
// (leader/LV/location/time) or "Empty". FF/F9C4 (DB = $30) decides: a slot is used iff its signature word
// $30:7FF8+slot*2 == 0xE41B (FF/F9D8) and its checksum word $30:7FF0+slot*2 matches FF/FBE7; it returns $00 =
// 0x0000 used / 0x8080 bad signature / 0x4040 bad checksum, and C2/D171 `LDA $00 / BEQ` reads the summary (C2/D19F)
// on 0, else draws the no-data marker (C2/D296). Summary fields (C2/D19F, slot base $7B in bank $30): leader
// +0x0580, char sheet +0x0200+id*0x50 (+0x12 level, C2/D1D3/D1DD), leader name +0x05B0+id*6, save number +0x059C
// (word, capped at 999 by C2/D21B), location +0x05F3 & 0x1FF, and the 10-byte block at +0x05E0 (24-bit gold, then
// the play clock). Not modelled: the ROM's copies of words +0x059E and +0x0793.
//
// The save write (FF/F9FB) packs the eight records of $FF:FA7B ({srcAddr:2, srcBank:1, len:2}, 0x0834 bytes) into
// the slot, then the FF/FBE7 checksum, the $E41B signature and the last-slot byte $30:7FE0. The save counter
// $7E:299C (slot +0x059C, the "Save #") is incremented before the pack (FF/FA05). disasm/notes/save_write.md.
#define CT_MENU_SAVE_SLOTS 3

// Write the live game into save slot `slot` of the bank-$30 image `sram` (>= 0x2000 bytes), packing from `wram` = the
// flat 128 KB WRAM ($7E at 0, $7F at 0x10000, CtWramSys's view). Returns the bytes packed (0x0834), 0 on failure.
// This is FF/F9FB; its caller C2/CF37 posts the save jingle first.
int CtMenuSaveWrite(unsigned char* sram, long sramLen, int slot, unsigned char* wram);

// FF/FBE7 over slot `slot` of a bank-$30 image, and that slot's base offset. Exposed so a check can run the checksum
// over a save the ROM made and compare it with the word the ROM stored.
unsigned CtMenuSaveChecksum(const unsigned char* sram, int slot);
unsigned CtMenuSaveSlotBase(int slot);

// ---- The loader (disasm/notes/boot_loader.md) ----
// FF/F9C4, the full slot gate: signature $E41B at $30:7FF8+slot*2 and the FF/FBE7 checksum matching $30:7FF0+slot*2.
// Returns 1 = used (the ROM's A = $0000), 0 = empty/corrupt ($8080 / $4040).
int CtMenuSaveSlotValid(const unsigned char* sram, long sramLen, int slot);
// FF/FAA3, the load: unpack slot `slot` of the bank-$30 image into the flat 128 KB `wram` through the writer's
// eight-record table. Returns the byte count (0x0834), 0 on failure. It neither validates (FF/F9C4, the caller's)
// nor reboots (C2/8320's `JML $C0:0000`, also the caller's).
int CtMenuSaveLoad(const unsigned char* sram, long sramLen, int slot, unsigned char* wram);
// C2/95BB and C2/956E, the two wipes the boot scan and the mode-0 shell run. `charBase` is the baked
// $CC:0000 char-sheet template (0x280 bytes); NULL leaves $7E:2600 alone.
void CtMenuNewGameWipeLight(unsigned char* wram);
void CtMenuNewGameWipe(unsigned char* wram, const unsigned char* charBase, long charBaseLen);
// C2/8D6A (leaf 9 of the $C2 gate, from the boot at C0/0057 `LDA #09 / JSL $C2:8004`): scan slots 0..2 with
// FF/F9C4; a valid one -> light wipe and return 2, none -> full wipe and return 0. The return is what the ROM stores
// in $7F0061, which scene 0's boot script branches on. wram = the engine's flat 128 KB image.
int CtMenuSaveBootScan(const unsigned char* sram, long sramLen,
                       unsigned char* wram, const unsigned char* charBase, long charBaseLen);

// ---- The load screen (menu mode 0 / screen $0E, C2/E60B). Rows 0..2 = the save slots, 3 = "New Game". The
// citations are in the block comment over CtMenuRenderLoader in menu.c.
int  CtMenuLoaderOccupancy(const CtMenuHost* h, unsigned char* occ);   // $0D49..$0D4C (occ may be NULL)
int  CtMenuLoaderStep(const CtMenuHost* h, int row, int up);           // C2/E743 (up = $005A bit3)
// The screen's own $7E:9380 node graph (C2/EA27, stride 6). `dir` is the ROM's index from its $F5 scan: 0 Right,
// 1 Left, 2 Down, 3 Up; Left/Right stay put.
int  CtMenuLoaderNeighbour(int row, int dir);
void CtMenuLoaderHandOam(int row, int* x, int* y);                    // node +0 / +1
int  CtMenuLoaderDefaultRow(const CtMenuHost* h);                      // C2/E7CF ($30:7FE0, else 3)
const char* CtMenuLoaderChapter(int storyline, int used);              // C2/D266 -> $FF:D024/$FF:D03E
const char* CtMenuLoaderEra(int locId);                                // C2/D3AD -> $FF:D296/$FF:D396
void CtMenuChromeLoaderAt(uint8_t* fb, int cursor);   // cursor row = the one not halved (-1 = none)
void CtMenuRenderLoader(const CtMenuHost* h, uint8_t* fb, int cursor);
typedef struct CtMenuSaveSummary {
  int used;      // 1 = valid save present (FF/F9C4 signature $E41B), 0 = empty / no-data
  int leaderId;  // party leader char id (save-file $+0x0580, bit7 = empty slot)
  int lv;        // leader level (save-file char sheet $+0x0200 + id*0x50, +0x12)
  int locId;     // location / area id (save-file $+0x05F3 & 0x1FF)
  int gold;      // gold, 24-bit at save-file $+0x05E0 (head of the 10-byte block C2/D238 MVNs)
  int saveNo;    // the save number at $+0x059C (word, capped at 999 by C2/D21B), the screen's "Save # n"
  int hh, mm;    // play-time hours:minutes (save-file $+0x05E0 clock block, $0400-06 digit layout)
} CtMenuSaveSummary;

// Read the summary of save slot `slot` (0..2) from h->sram (the bank-$30 image). Returns 1 (out->used = 1, fields
// filled) for a valid save, else 0 (out zeroed). h->sram == NULL reads 0 for every slot.
int CtMenuSaveSlot(const CtMenuHost* h, int slot, CtMenuSaveSummary* out);

// CtMenuRenderSave draws the Save screen: the three slot rows (leader/LV/time, else "No data") with the hand on
// `cursor` (0..2). The cursor's row is bright (entered-Save CGRAM palette 4, body 231); the others draw the same
// palette through the half colour math (body 115). The hand's ink box sits at x8..23, y(31+32*cursor)..(42+32*cursor).
// cursor < 0 = the ribbon's hover preview: every row bright, no hand. B exits to the ribbon.
void CtMenuRenderSave(const CtMenuHost* h, uint8_t* fb, int cursor);
// The overwrite prompt is up (C2/D4BB): it holds the screen's one nav hand, so the slot list draws none.
void CtMenuSavePromptUp(int on);
// The ribbon's Save preview after B off the Save screen keeps that screen's summary panel (C2/D0DB redraws nothing):
// `slot` = the slot the cursor left, -1 = a preview the ribbon built (no panel).
void CtMenuSetSaveHoverPanel(int slot);
// Screen 6's overwrite prompt (sub-state 2, opened by C2/CF19 when A lands on an occupied slot). `sel` is $0054 over
// the slot list's $7E:9380 node graph: 3 = Yes (OAM 184,63), 4 = No (OAM 184,79), seeded to 3; anything else draws
// nothing. A saves only on 3.
void CtMenuRenderSavePrompt(const CtMenuHost* h, uint8_t* fb, int sel);

// A placeholder for an unexpected sub-screen id: a titled window and "Not Implemented". `screen` is the $0067 id
// (1..6). B returns to the ribbon.
void CtMenuRenderPlaceholder(const CtMenuHost* h, uint8_t* fb, int screen);

// Decode item `id`'s name from the ROM $CC/0B5E table into `out` (NUL-terminated ASCII). Fixed 11-byte records,
// name(id) = rom[0x0C0B5E + id*11 .. +11] (e.g. 189 Tonic, 200 Shelter, 0xD6 Pendant). CT encoding (0xA0-0xB9 'A'-'Z',
// 0xBA-0xD3 'a'-'z', 0xD4-0xDD '0'-'9', 0xE7 '\'', 0xE8 '.', 0xEF and 0xFF space, 0x20-0x2F equip-type icon -> space);
// records are space-padded, so leading and trailing spaces are stripped. Returns the length written (0 and out = ""
// if rom is NULL or too short). cap should be >= 12.
int CtMenuItemName(const uint8_t* rom, long romLen, int id, char* out, int cap);

// ---- The shop (menu mode 2 = screen $0C, disasm/notes/shop_screen.md) ----
// Opened by event op 0xC8 $80-$BF (the shop number = operand & $3F). The fields carry the ROM's RAM names.
// Every substate is ported: 0-5 (build, Buy/Sell, buy list, sell list, sell quantity, buy quantity) and the
// equip sub-mode 6-8 (Up/Down on Buy/Sell -> C2/E017: the party row, the gear rows, the candidate list).
#define CT_SHOP_BG1_ROWS 48                   // the BG1 buffer rows the shop's DMA records reach ($2E00.., rows 0-47)
#define CT_SHOP_BG3_ROWS 26                   // the BG3 buffer rows C2:FC1B uploads ($4E00.., rows 0-25)
typedef struct CtMenuShop {
  int shopNo;                 // $71 (= $0A01)
  int sub;                    // the live substate $7E:00BE
  int cell;                   // $54: 8 Buy / 9 Sell on substate 1; the list ROW 0..7 on 2/3
  int prevCell;               // $81 (C2/E9E2), the blip test
  int hiIdx;                  // $83 (the last row index C2/DD7C drew; 0xFF forces a redraw)
  uint8_t list[32]; int nList;                 // $1000[] / $104A
  uint16_t price[256];                         // $6E00[id*2], filled for the listed ids only (C2/DB9F)
  uint8_t owned[32];                           // $1020[] (C2/DCDA)
  int buyRow, buyScroll, sellRow, sellScroll;  // $1040 / $1041 / $1042 / $1043
  uint8_t sellId[257], sellCnt[257]; int nSell;  // $5600[] / $5700[] / $1049 (C2/DECC)
  int lastCell, scrollMax;                     // $0DDC / $0DDA (= $57) of the list on screen (C2/DD40)
  int curId, curMax1, qty, cls;                // $04C9 / $04CA / $04CB / $04CC
  uint32_t total; int unit;                    // $1044..$1046 (24-bit) / $1047
  int equipped; uint8_t eqBy[8];               // $0D82 / $1056[] (C2/8900)
  int boxOwned;                                // the Owned count C2/DD7C last printed ($04CA at that call)
  uint8_t roster[9]; int nRoster;              // $104D[] / $1055 (C2/DBE8)
  int nParty;                                  // $85 (FF/F813): the members the party row walks -- $73, or $75 late in the story
  int anim[7];                                 // $1811 + charId*0x40: 0 idle / 0x0B "can equip" (C2/DD7C)
  int atkOld[8], defOld[8], atkNew[8], defNew[8];   // per roster member: $9ACD/$9ACE vs $9B23/$9B24
  int arrowRun, arrowOn, arrowCtr;             // the down-arrow task ($E0 != 0), its 8/8 blink ($0DA0 bit0, $0DA3)
  int sbPos;                                   // the $0D95 a one-row scroll wrote (-1: none this frame)
  int slideStep, slideO;                       // C2/D82B's one-row slide: the step on screen (1..3) and its $0DAB
  int eqSlideStep, eqSlideO;                   // the equip sub-mode list's (C2/9F05 -> C2/EB9B, as the Equip screen's)
  int shownBuyScroll, shownSellScroll;         // the window whose rows the last FBE3 / FBCE upload took to VRAM
  int sbTask;                                  // the list scrollbar task (slot 4, C2/8BB8) lives: spawned by the Buy /
                                               //   Sell entries (C2/DCB3, C2/DEC2), killed by their B (C2/D5F5)
  int d17;                                     // $0D17: the substate C2/E984 last ran in (a new one reads no A/B/L/R)
  uint8_t nav[19][6];                          // $7E:9380: the live node graph (x, y, up, dn, lf, rt; bit7 = an edge)
  uint8_t navBak[12][6];                       // $7E:9990: C2/DACA's copy of its first 0x48 bytes (C2/D745 puts it back)
  // ---- the equip sub-mode (substates 6-8, C2/E017 .. C2/E1DC)
  int retCell, partyCell;                      // $80 (the Buy/Sell cell to go back to) / $79 (the party cursor, 0x0C + k)
  int member, slot, sheetOff;                  // $71 (the roster index) / $0077 (gear row 0-3) / $0D26 (C2/A4AB)
  uint8_t copy[0x50];                          // $9A90: the member's sheet copy (C2/8820), the preview patched in
  uint8_t cmp7[7];                             // $0D4D..$0D53: 00 equal / 08 higher / 1C lower (C2/E18A)
  uint8_t eqCand[0xF3], eqCnt[0xF3];           // $0F00[] / $0F62[] (C2/8953)
  int eqCount, eqScroll;                       // $0FC6 / $0FC8 (its page: C2/EA81 into firstCell/lastCell/scrollMax)
  int firstCell;                               // $0DDB: the list's first node (0 for Buy/Sell's rows, 4 for the gear list)
  int equipHdma;                               // $0D13 = $55: HDMA channels 0/2/4/6 instead of 0/1/2/3/5
  int d0b;                                     // $0D0B: BG2 V = $0D0A (FF) | $0D0B << 8 (C2/85D6) -- FF / 01 the shop's
                                               //   windows, 00 (C2/E0DC) the equip sub-mode's (map rows 32-59)
  uint8_t sprDim[8];                           // FF/F628 per charId: OBJ palette colours 1-12 halved
  uint16_t wpnPal[7][3]; int wpnOn;            // C2/F5A7 -> C2/F5ED: colours 13-15 per charId (the weapon's), once loaded
  // The two text buffers the ROM keeps ($7E:2E00 BG1, $7E:4E00 BG3) where the equip sub-mode reads back what it
  // wrote, and what VRAM holds of them: a buffer write shows only once a C2/8385 record has queued its rows for the
  // NMI (upQ). Rows 2, 24, 25 (the party panel) and 32-47 (the gear and stat rows) of BG1, rows 7-21 of BG3 (the list).
  uint16_t b1[CT_SHOP_BG1_ROWS][32], v1[CT_SHOP_BG1_ROWS][32];
  uint16_t b3[CT_SHOP_BG3_ROWS][32], v3[CT_SHOP_BG3_ROWS][32];
  int upQ;                                     // bit0 FBE3 (BG1 rows 2-26), bit1 FBF1 (24-47), bit2 FC1B (BG3 0-25), bit3 FBCE (0-47)
  int resume;                                  // a handler that yielded (C2/821E): 1 = the rest of C2/E0A5, 2 = after C2/E108
  // The description line (C2/F2F3 -> C2/FA49): the id it shows ($0D77), a forced redraw ($0D78 bit7), and the string
  // task's progress -- one decoded character per frame (C2/0009, $0217), running until the terminator ($0215 = 0).
  int descCur, descForce, descN, descRun;
} CtMenuShop;
typedef struct CtMenuShopIn {                  // this frame's NEW presses (the ROM's $0D1D / $F5 edges)
  int a, b, l, r, up, down, left, right;
} CtMenuShopIn;
typedef struct CtMenuShopOut {                 // what the frame did, for the host
  uint8_t sfx[4]; int nSfx;                    // C2/EB03 ids in order: 00 blip, 01 buzz, 55 trade
  int close;                                   // B on Buy/Sell: C2/82B2, the shop exits (silent)
} CtMenuShopOut;
// C2/D52B + C2/DACA: build the screen for shop `shopNo` from the live $7E/$7F images and the ROM.
void CtMenuShopOpen(CtMenuShop* s, const uint8_t* mem, const uint8_t* glb, const uint8_t* rom, long romLen, int shopNo);
// One menu frame of substates 1-5. Writes the bag ($2400/$2500) and the gold ($2C53..55) on a commit.
void CtMenuShopFrame(CtMenuShop* s, uint8_t* mem, const uint8_t* glb, const uint8_t* rom, long romLen,
                     const CtMenuShopIn* in, CtMenuShopOut* out);
// The shell's P frame (C2/D52B spawns the down-arrow task with the fade-in): that task's first step.
void CtMenuShopSpawnFrame(CtMenuShop* s);
void CtMenuRenderShop(const CtMenuHost* h, uint8_t* fb, const CtMenuShop* s);
// The ROM's per-item price rule C2/DB9F (word $7F:01CD multiplier) and the sell price C2/DF51.
int  CtMenuShopBuyPrice(const uint8_t* rom, long romLen, const uint8_t* glb, int id);
int  CtMenuShopSellPrice(const uint8_t* rom, long romLen, int id);
// The menu sprite machine's per-character request ($1811 + charId*0x40 = anim), as C2/DD7C writes it.
void CtMenuSprRequest(int charId, int animId);
// Block 7 ($19C0), the Tech caster's copy: C2/BA09's character, its request/loop ($19D1 / $19E1), its character and pose.
void CtMenuSprBlock7(const CtMenuHost* h, int charId);
void CtMenuSprRequest7(int animId, int loop);
int  CtMenuSprChar7(void);
int  CtMenuSprShown7(void);
// The pose the picture shows: the machine's pose of the previous tick (the tile DMA goes out one frame late).
int  CtMenuSprShown(int charId);

// The play-clock tick (C2/85AE, once per frame from the shared NMI path): +1 on the $0400 frame digit, rippling
// through the wrap limits [60,60,10,6,10,10]; all six wrapped -> $0406 = 1 (the 99:59 cap). Inert while $0407 != 0
// (the menu entry writes $0407 = FF at C0/1946; the field resume clears it, C0/2844).
void CtMenuClockTick(uint8_t* mem);

#endif
