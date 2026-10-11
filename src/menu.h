// menu.h — the X-button MENU module boundary (ROM: field C0/1920/1923 -> entry C0/192F-1960 ->
// JSL C2/8000 with A=1 = the mode-1 menu program; exit C2/82B4 RTL -> field resume C0/1964-1984).
//
// PHASE 1 = the mode-1 MAIN SCREEN only (C2/808F mode-1 -> initial screen 00): render the per-PC
// stat panels + the TIME/G box from the canonical RAM the ROM menu reads, then wait for B. No
// cursor, no sub-screens (Item/Tech/Status/save — their C2 programs are untraced, see
// disasm/notes/menu_entry.md "Open questions"). The module reads the engine's $7E image — the
// same RAM the ROM menu program reads — so the panel data is byte-comparable against the oracle
// menu captures (m1_gato_menu: Crono LV1 HP 70/70 MP 8/8, Marle LV1 HP 65/65 MP 12/12).
//
// Data provenance (each pinned this pass against the oracle WRAM captures + ROM disasm):
//   party        $7E2980-82 (0x80 bit = empty slot)
//   char sheet   $7E2600 + charid*0x50: +3 curHP w, +5 maxHP w, +7 curMP w, +9 maxMP w (dossier-
//                pinned); +0x12 = LEVEL — pinned by elimination: the UNIQUE sheet offset that is
//                1 in BOTH observed-LV1 sheets (mm_menu.raw.wram: Crono 01@{+0x12,+0x29}, Marle
//                01@{+0x00,+0x12} -> intersection {+0x12}), cross-checked in mm_fair_menu3
//   gold         $7E2C53 24-bit LE — pinned by the exhaustive ROM scan: the read/modify/write
//                cluster C2/FCD0 (menu read), C2/FD08 (SBC = shop spend), C2/FDA1..FEDB (add/sub
//                writers), C1/D0B3/D0DB/D11B (battle reward). The other 400-valued candidate
//                ($7E27F6) has ZERO ROM references (a save-image copy inside charid-6's sheet).
//   play clock   $7E0400 frames / $0401 seconds / $0402-03 minute digits (units,tens) /
//                $0404-05 hour digits / $0406 overflow — writer C2/85AE (monitor PC C2:85BC,
//                1/frame): gate `LDA $07 BNE rts` = paused while $0407 != 0, digit wrap limits
//                C2/85D0 = [60,60,10,6,10,10], all-wrapped -> $0406=1. The menu displays HH:MM:
//                C2/F28E computes $22/$23 = tens*10+units via C2/F2CC (Y=$0402), showing 99:59
//                when $0406 != 0 — verified: pre->field +208 frames advanced 55s28f -> 58s56f.
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
  const uint8_t* glb;        // the engine's $7F image -- the Form screen needs $7F:01DF, the LOCKED-member
                             //   mask (FF/F9BB[id] = 0x80 >> id), to build C2/CD2B's panel array. NULL =
                             //   no locks, which is what the ribbon HOVER used to assume unconditionally.
  // Renderer hooks = the field's already-RE'd chrome/font, REUSED (the ROM menu's own BG tile
  // provenance is an OPEN question — menu_entry.md: the VRAM DMA sources for font/chrome/portrait
  // tiles are untraced — so byte-exact menu pixels are explicitly NOT a phase-1 deliverable):
  void (*drawWin)(uint8_t* fb, int x, int y, int w, int h);  // CT window chrome (CT_ASSET_WINDOW 9-patch)
  void (*drawGlyph)(uint8_t* fb, int code, int x, int y);    // 8x8 menu-font glyph, CT charset code (bright pal0)
  void (*drawGlyphDim)(uint8_t* fb, int code, int x, int y); // same glyph in the DIM pal4 (non-focused party panel)
  void (*drawGlyphDim2)(uint8_t* fb, int code, int x, int y);// a LIGHTER dim (140,148,148): Equip non-selected gear
  void (*drawGlyphYellow)(uint8_t* fb, int code, int x, int y);// YELLOW (ROM FF/CBB4): field-usable item names
  void (*drawGlyphCyan)(uint8_t* fb, int code, int x, int y); // BG palette 2 (0,255,255): a stat the previewed gear RAISES
  void (*drawGlyphSave)(uint8_t* fb, int code, int x, int y); // SAVE-screen text (entered-Save CGRAM pal4: 231 body;
                             // the non-cursor rows are dimmed by CtMenuRenderSave's half-math band, not a palette)
  const uint8_t* portrait;   // lead PC portrait blob ([w u8][h u8] + RGB) or NULL
  const uint8_t* arrowTri;   // the ribbon selection arrow = name-cursor triangle ([w u8][h u8] + RGBA),
                             // ROM $FF/9D70 (OAM tile 0x22) reused; NULL = skip the arrows
  // The seven characters' MENU STANDING SPRITES, indexed by charId 0..6 ([w u8][h u8] + RGBA, alpha 0
  // = transparent), or NULL where the host has no art. These are NOT a menu-specific asset: the menu
  // draws the character's own FIELD sprite. Measured on the oracle -- for all seven characters the
  // tiles the menu uploads to OBJ VRAM decode to the SAME colour index at EVERY pixel as
  // CT_ASSET_SPRITE[charId] composed at that character's standing assembly frame, and the menu's OBJ
  // palette is the sprite asset's own palette byte for byte over indices 1..12 (the only ones these
  // frames use).
  const uint8_t* partySpr[7];
  // THE MENU SPRITE ANIMATOR's two hooks. The MACHINE is the ROM's and lives in menu.c (C2/F643's
  // task + C2/F69B's advance); the ART and the animation TABLE stay with the host's assets:
  //   spriteAnim[charId] = CT_ASSET_SPRITEANIM[charId] -- [nAnims u8] then per anim [n u8] +
  //     n x {U,D,L,R assembly frame u8, delay u8}. Its bytes ARE the ROM's own tables: for Lucca,
  //     anim 1's D column and delays are E4/39A0 = 0A 0B 0C 0D 0E 0F and E4/AA24 = 07 x6, byte for
  //     byte, and anim 0x0B is E4/39C8 = 01 87 4A / E4/AA4C = 0A 05 0F.
  //   partySprFrame(charId, asmFrame, cropX, cropY) = that character's own sprite composed at an
  //     ARBITRARY assembly frame -- the ROM's block+0x03 -- in the same blob shape as partySpr,
  //     PLUS the crop's origin inside the compose canvas. The origin is what makes an animation
  //     hold still: the ROM's OBJ block is anchored to the CANVAS and the artwork moves inside it,
  //     so a frame whose ink box differs from the standing one must be offset by the difference
  //     rather than re-centred on its own ink (which would make the character jitter). NULL, or a
  //     NULL return, falls back to partySpr's standing frame.
  const uint8_t* spriteAnim[7];
  unsigned       spriteAnimLen[7];
  const uint8_t* (*partySprFrame)(int charId, int asmFrame, int* cropX, int* cropY);
  const uint8_t* handCursor; // the party-panel SELECTION hand (☞) = ROM $FF/9CF0 (CT_ASSET_NAMESPRITES id 0,
                             // [w u8][h u8] + RGBA) reused; NULL = fall back to a plain triangle marker
  const uint8_t* rom;        // the loaded ROM image (battle.c CtBattleRom) — the Item screen decodes
  long romLen;               //   item names from the $CC/0B5E table; NULL/short = names shown as ids
  // The DIALOGUE VWF (12x12 proportional, CT_ASSET_FONT). The load screen's slot descriptions and
  // its era name are drawn with it -- measured: the tiles the ROM renders them into are the SAME
  // glyphs and the SAME advances as this font. NULL = fall back to the 8x8 menu font.
  int  (*vwfWidth)(const char* s);
  void (*vwfDraw)(uint8_t* fb, const char* s, int x, int y, const uint8_t tone[4][3]);
  const uint8_t* sram;       // CT SRAM save-file image: the cart's 8 KB mapped at $30:6000-7FFF, so an
  long sramLen;              //   address in that window is at offset (addr - 0x6000). Three slots at the
                             //   ROM's own bases $FF:FBE1 = $6000/$6A00/$7400 -> 0x0000/0x0A00/0x1400
                             //   (stride 0x0A00, NOT 0x800), plus the checksum/signature tables at
                             //   $30:7FF0/$7FF8 -> 0x1FF0/0x1FF8. The field host owns one and persists it
                             //   to a file; NULL is still valid and makes every slot read EMPTY.
  const uint8_t* sramPanel;  // the SRAM the Save screen's SUMMARY PANEL was last drawn from (C2/D32C), when it lags
                             //   the rows (a frame, after a save); NULL = h->sram
  const uint8_t* menuFont2bpp; // the raw menu font (CT_ASSET_MENUFONT: 2bpp, 16 B per tile, tile = char code),
                             //   for screens that compose BG1 SCANLINE BY SCANLINE (the shop: its HDMA bands
                             //   show 8-line tile rows at 12-line pitch). NULL = those screens draw no text.
} CtMenuHost;

// Ribbon cursor step (PHASE 2A). The ROM's directional mover C2/E9E5 looks up a per-option neighbor
// graph at $7E9380 (+2 Up,+3 Dn,+4 Lf,+5 Rt) whose Lf/Rt = +-1 mod 6 and Up/Dn = self for the 6-in-a-
// row layout (menu_phase2.md R3/§2), so it collapses to a plain mod-6 step. dir +1 = RIGHT edge,
// dir -1 = LEFT edge (wrap both ways), dir 0 = UP/DOWN (INERT — verified no-op, R1). Returns 0..5.
static inline int CtMenuRibbonStep(int opt, int dir) {
  if (dir > 0) return (opt + 1) % 6;   // RIGHT: (menuOpt+1)%6
  if (dir < 0) return (opt + 5) % 6;   // LEFT:  (menuOpt+5)%6
  return opt;                          // UP/DOWN: inert
}

// Draw the BYTE-EXACT BG2 menu chrome (marble desktop + dark-blue 9-slice window panels + the bright
// dark-blue ribbon box) into fb, replayed from the oracle-captured tilemap (src/menu_chrome.h). This
// REPLACES the old invented chrome (the reused field-dialogue window + a flat-purple ribbon box).
//   screen 0 = the ribbon (opt 0..5 picks that option's window layout); screen 1..6 = entered sub-screen.
//   mode 0 = marble + panels, SKIP the ribbon box (content draws between them); 1 = ONLY the ribbon box
//            (redraw on top of content); 2 = everything. Callers lay mode-0/2 first as the background,
//            render content, then (ribbon) mode-1 + icons. See field.c menu dispatch.
// The per-scanline 5-bit COLDATA ramp the ROM's HDMA generator C2/86DD produces for one ribbon
// OPTION's window list ($FF:BBF2). 224 signed entries, +8 at the top of each window down to -7 at its
// bottom, RESET and height-normalised per window -- which is why the status screen steps every 12
// scanlines (one 192px window) and the Form screen every 3 (four 48px windows).
const int8_t* CtMenuWinGradRamp(int opt);
// The FORM screen's panel geometry: the text column (x 72, the ROM's tile col 9, measured on the
// oracle Form capture) and the standing sprite's anchor for panel 0..2 -- C2/CE6E's word at index
// $12 + 2*panel, read as the field engine's convention (X = centre, Y = feet): centre 168 for every
// panel, feet 56 / 104 / 152. Any pointer may be NULL.
void CtMenuFormGeom(int panel, int* textX, int* feetY, int* centreX);
// Where a party sprite's opaque box lands on screen for a given (charId, centre x, feet y) -- the same
// arithmetic mBlitSprAnchored blits with. Returns 0 when the host has no art for that character.
int CtMenuSprBox(const CtMenuHost* h, int charId, int cx, int feetY, int* x0, int* x1, int* y0, int* y1);
void CtMenuChrome(uint8_t* fb, int screen, int opt, int mode);
// Set the char-select context (party $2980 mem + SELECTED char) so CtMenuChrome tones the party-panel boxes
// BY SELECTION (the cursor's member's box is brighter). Call before the char-select chrome (screen 1 + ribbon
// opt0 preview); pass (NULL,-1) to disable. Verified vs the real game via ct.exe.
void CtMenuSetCharSel(const uint8_t* mem, int selChar);
// Entered Equip/Status DETAIL chrome (3-box left column) -- byte-exact oracle BG2 tilemap; used for
// screen 1 sub 1 instead of the char-select chrome (which has the party stat panels).
void CtMenuChromeEquip(uint8_t* fb);
// The entry slide (C2/A71E) is CtMenuRenderEquipEntry, below.

// Panel data for party slot 0-2 straight from the $7E image (see provenance above).
// Returns 1 when the slot is occupied ($2980+slot bit7 clear), 0 for an empty slot.
int  CtMenuPanelData(const uint8_t* mem, int slot, CtMenuPanel* out);

// Compose one 256x224 RGB frame of the mode-1 main screen into fb: left column = 3 PC panels
// (name, LV, HP cur/max, MP cur/max; an empty window for an empty slot), TIME/G box bottom-left,
// lead portrait top-right — the oracle layout (m1_gato_menu.raw). PHASE 2A adds the 6-icon command
// ribbon with the selected-option (menuOpt 0..5) highlight (+0x20 tile) and the two straddling
// arrows (menu_phase2.md §3). menuOpt is the current ribbon option; B still closes the whole menu.
void CtMenuRender(const CtMenuHost* h, uint8_t* fb, int menuOpt);

// PHASE 3 (part 1) — the SUB-SCREENS reached from the ribbon (A commits screen = menuOpt+1, ROM
// C2/9A77-9A81 $0067 := opt+1; B in a sub-screen returns to the ribbon, C2/9A98 STZ $0067). Only
// the ITEM screen ($0067=2, ROM handler C2/AA34) renders real content this phase; the other five
// open a titled "not implemented" placeholder (no crash, B returns).

// SCREEN 2 = ITEM (C2/AA34 / builder C2/ADD0). Header window [ITEM icon]"Use/Move", then one list
// row per held item: [icon][name][xN for stackables]. Inventory = $7E2400 item-ids (packed, 0x00-
// terminated, cap 0xF2) + $7E2500 parallel counts (h->mem). Names from the ROM $CC/0B5E table
// (h->rom); if the ROM is absent each name falls back to "Item xx" (the hex id). `cell` = the ROM cursor
// $0054 (list rows 0x00-0x09, "Use/Move"=0x0A, "Organize"=0x0B, target members 0x0C..0x0E); `scroll` =
// $0416 top slot; `pickedSlot` = the Move-lifted slot ($0F00, -1 none); `targetMode` = Use target overlay
// up; `partyN` = active party size. The interactive Use/Move/Organize logic lives in the CtMenuItem* below.
// `barBlinkOn` is the SCROLLBAR's own phase: C2/8BB8 blinks its two triangles together 8 frames on /
// 8 off ($0D96), a different period from the pinned hand's 4/4 ($0DA3) -- they are separate counters.
// `descId`/`descN` = the description line: item $0D77, the characters C2/FA49 has typed so far. `slideO` = the one-row
// slide's $0DAB this frame (C2/AC8A's loop: +3/6/9 a Down, -3/6/9 an Up; 0 = none), `scroll` already the new window.
void CtMenuRenderItem(const CtMenuHost* h, uint8_t* fb, int cell, int scroll,
                      int pickedSlot, int targetMode, int partyN, int pinnedBlinkOn, int barBlinkOn,
                      int descId, int descN, int slideO);
int  CtMenuItemDescLen(int id);                      // C2/FA49's steps for item id's line (decoded characters)
int  CtMenuStrLen(int kind, int id);                 // the same for table kind 0 = items ($CC:2EB1), 1 = techs ($CC:3A09)

// ITEM screen interactions (byte-exact ROM, see the ct-field-item-menu-use RE notes):
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
// C2/B870 -- $7E:0DBC, the target overlay's VALIDITY MASK, recomputed every frame: bit1 = the target's
// HP can still be restored, bit0 = its MP can. Zero means the A press must BUZZ (C2/B1BA) and the item
// must NOT be consumed. All-party items scan the whole active party and return on the first target
// that would gain something.
int  CtMenuItemTargetMask(const uint8_t* mem, int absSlot, int tgtSlot);
int  CtMenuItemAllParty(const uint8_t* mem, int absSlot);   // $0DBD bit 3: C2/8AD5's three hands, not the nav one
void CtMenuSetNavHandShown(int on);                          // C2/E954's $0D9B gate for the Item screen's nav hand
void CtMenuItemOrganize(uint8_t* mem);               // Organize: canonical sort/merge/compact, C2/AB62

// SCREEN 1 = EQUIP/STATUS (C2/9C29), rendered READ-ONLY (the interactive equip-change is deferred).
// Two substates, exactly the ROM's char-detail flow (verified on the oracle, saves/menu_open_gato2):
//  (a) CHARACTER-SELECT: the hand cursor rests on a party panel; the picker cell = $7E0054 (working) /
//      $7E0412 (committed) = the active-party SLOT; Up/Down cycle the OCCUPIED slots (wrap, skip empty);
//      A confirms -> detail; B -> ribbon (verified $0067 1->0).
//  (b) DETAIL: the picked character's status page (read-only). B -> character-select.
// CtMenuRenderCharSelect draws the 3 party panels + portrait of the selected char + a hand marker on
// `selSlot` (the active-party slot 0..2). h->portrait must be the SELECTED char's portrait.
void CtMenuRenderCharSelect(const CtMenuHost* h, uint8_t* fb, int selSlot, int dimSlot, int hand);   // dimSlot = $0412 (C2/9E76)
// C2/A85C: the gear panel's way back, step `j` (0 = the setup frame .. 23) -- see menu.c.
void CtMenuRenderEquipExit(const CtMenuHost* h, uint8_t* fb, int charId, int slot, int selSlot, int j);

// CtMenuRenderEquip draws the EQUIP screen (after A on a party member): only `charId`'s panel + the slot-type LABEL
// (Weapon/Helmet/Armor/Accessory for cursor `slot` 0..3) + the detail with the cursor slot's item bright (others dim)
// + the ☞ hand on it. `stage` 1/2 = the two frames the panel is built in (menu.c: C2/A321 and C2/9C5E), 0 = settled.
// `cand`/`cnt`/`n` = the SAME candidate list substate 3 uses: C2/9C61 builds it (C2/8953) and draws
// it (C2/A569) while the cursor is still on the gear rows, 32 px lower than in substate 3. Pass NULL/0
// only where there is no bag to read.
void CtMenuRenderEquip(const CtMenuHost* h, uint8_t* fb, int charId, int slot, int stage,
                       const uint8_t* cand, const uint8_t* cnt, int n);
// C2/A71E + C2/A7B7: the gear panel's entry slide, step `k` (1..23) -- the member select with the new column risen
// through the offset-per-tile band. selSlot = $0412, dimSlot = the member C2/9E76 last left bright.
void CtMenuRenderEquipEntry(const CtMenuHost* h, uint8_t* fb, int selSlot, int dimSlot, int k);
// C2/9DAF -- the member select's one-member scroll: the column (lines 16-159, screen columns 2-13) moved `o` px (1..44,
// 4 a frame) over the OLD window (`oldScroll`) and the member coming in (below for dir +1, above for -1); `dimRow` the
// row C2/9E76 last left bright, `handRow` the cursor, `detailChar` the member C2/A22F last drew.
void CtMenuRenderEquipMemberSlide(const CtMenuHost* h, uint8_t* fb, int oldScroll, int dir, int o, int dimRow,
                                  int handRow, int detailChar);
// C2/A4AF: the list box's 8-frame climb into the candidate list (s = 4..28), over the gear panel's chrome.
void CtMenuRenderEquipListRise(const CtMenuHost* h, uint8_t* fb, int charId, int slot, int s, int gearHand);
#define CT_MENU_EQ_LIST_RISE_PX 32   /* the climb's length: C2/A4AF loops while $96A3 < 0x20, 4 px a pass */

// ==== SCREEN 1 IS INTERACTIVE: THE GEAR SWAP (disasm/notes/menu2_equip_change.md) ====
// Screen 1 has FOUR substates (C2/9C29), not two: 0 build, 1 char-select, 2 gear-slot select,
// 3 CANDIDATE LIST. A on a gear row enters substate 3 when the candidate count is non-zero and
// BUZZES (id $01, C2/9C8D -> C2/EACC) when it is zero.
// Fill cand[]/cnt[] (cap 0xF2 each) with the items in $2400 that fall inside the gear slot's ROM
// range (C2/89DC, interleaved [lo,hi): Weapon [00,5A) Helm [7B,94) Armor [5A,7B) Accessory [94,BC))
// AND that `charId` may equip ($7E:7A00[id] & (0x80 >> charId), C2/93AF), selection-sorted by the
// gear power byte $7E:7C00 -- DESCENDING, measured on the oracle. Returns the count ($0FC6).
int  CtMenuEquipCandidates(const uint8_t* mem, int gearSlot, int charId, uint8_t* cand, uint8_t* cnt);
// ROM C2/916E: the 11 DERIVED bytes of a 0x50-byte char sheet -- 7 capped stats (out[0..6], sheet
// order Power/Stamina/Speed/Magic/Hit/Evade/M.Def), Attack out[7], Defence out[8], effective max HP
// out[9..10] -- from the four equipped pieces' $7C00 power and $7B00 bonus sets. Pure: it reads only
// the sheet, so it doubles as the candidate PREVIEW (copy the sheet, patch one gear byte, call this).
void CtMenuRecalcDerived(const uint8_t* sheet, uint8_t* out11);
// ROM C2/9137 + C2/813C: run CtMenuRecalcDerived on the LIVE sheet and store it back at +0x36..+0x40,
// clamping curHP (+0x03) to the new effective max and rebuilding the HP-bar scale $0D38 + charId*2.
void CtMenuEquipWriteBack(uint8_t* mem, int charId);
// C2/9875's pass at every menu open: the bag cleanup C2/9988 and all seven sheets recomputed + written back (C2/99B2).
void CtMenuOpenSheets(uint8_t* mem);
// ROM C2/9CC1: give the old piece back, write `newId` into the sheet at C2/A4AB[gearSlot]
// (Weapon +0x29, Helm +0x27, Armor +0x28, Accessory +0x2A), take the new piece out, zero
// $7F:00B4+charId on a WEAPON change (C2/9CFC), then recompute. Returns 1 when it equipped.
int  CtMenuEquipApply(uint8_t* mem, uint8_t* glb, int charId, int gearSlot, int newId);
// ROM C2/8791 / C2/87D5. Add returns 0 ok / 0xFF full and REFUSES the three "none" sentinels
// $5A/$7B/$94 -- which is the ROM's entire unequip semantics: equipping into an empty helm/armor/
// accessory slot consumes the new item and puts nothing back. Remove returns 0 / 0xFF not held /
// 0xFE not enough, and zeroes the id slot in place when the count reaches 0.
int  CtMenuInvAdd(uint8_t* mem, int id, int qty);
int  CtMenuInvRemove(uint8_t* mem, int id, int qty);
// The candidate's bottom-bar line, decoded from its bonus set (oracle: the Red Katana prints
// "Magic +2"). Returns the length written; 0 (and out="") when the item carries no bonus.
int  CtMenuEquipDesc(int id, char* out, int cap);
// The stored-description table's shape (entries, and how many are non-empty) -- ROM CC/2EB1.
int  CtMenuDescTable(int* nonEmptyOut);
// The CANDIDATE LIST panel: the ROM replaces the left column with the list (rows at y = 76 + row*12,
// name x24, hand x8, measured off a --peek of an equip screen driven onto a bag with spare gear) and
// puts the highlighted item's bonus line in the bottom bar. `cell` = the highlighted row. `descN` = the characters
// C2/F2F3's string task has typed of item $0D77's description (C2/A5DF runs C2/F2F3 on every preview). `prevId` = the
// candidate C2/A5C3 last previewed (-1: the cursor's own); `slideO` = the one-row slide's offset this frame (C2/EB1F:
// +3/6/9 a Down, -3/6/9 an Up; 0 = none), `scroll` already the new window.
void CtMenuRenderEquipList(const CtMenuHost* h, uint8_t* fb, int charId, int gearSlot,
                           const uint8_t* cand, const uint8_t* cnt, int n, int cell, int scroll, int hand, int prev,
                           int descN, int prevId, int slideO);

// SCREEN 3 = TECH (C2/B353), rendered READ-ONLY, SINGLE techs only (Double/Triple deferred). Same two-
// level flow as screen 1: char-select (hand on a party panel) -> the picked char's SINGLE-tech list.
// Verified on the oracle (saves/menu_open_gato2, Crono's learned singles = [Cyclone] MP 2). Provenance
// (each pinned this pass from the ROM tech-menu builders + oracle RAM, byte-validated):
//   learned bitfield  $7E7F00[charId] (MSB = the char's first tech), the buffer the ROM list-builder
//                     C2/BD00 reads; COMPUTED at menu-open (FF/F850) from the persistent seed
//                     $7E2837+charId -> we recompute it the same way (a raw field capture zeroes the
//                     $7F00 scratch), VALIDATED byte-exact vs the oracle's $7F00 (= 0x80 for Crono).
//   tech-id range     $C2/BD40[charId] cumulative boundary (=01,09,11,19,21,29,31,39); char's singles
//                     = ids [t,t+1). Bit (0x80>>k) set -> tech id (start+k) learned (C2/BD00 ASL scan).
//   tech NAME table   ROM $CC/15C4 + techId*11 (techId 1=Cyclone,2=Slash,3=Lightning,4=Spincut,
//                     5=Lightning2,6=Life,7=Confuse,8=Luminaire) — the item mapped-name table continued.
//   tech MP table     ROM $CC/253B + techId (C2/BC22): ids 1..8 = 2,2,2,4,8,10,12,20.
// CtMenuRenderTech draws the two party panels + the Single/Double/Triple tabs (`tab` = $7E:0418) + the
// EIGHT-row list (C2/BD73) at x144, y = 60 + 12*i, scrolled by `scroll` ($0F4A), with the MP each row
// costs THIS character and the hand on `row` ($0F48; pass -1 while the list is not entered).
// `selSlot` >= 0 puts the hand on that party panel instead of on a list row (the char-select level);
// the Tech screen has no right-hand detail column -- the tabs occupy that corner.
// `fresh` = the build frame (C2/B979: no list, no bar yet); `barId`/`barX`/`barN` = C2/FA49's line ($0D77, where
// it types and how many characters it has typed).
// C2/B5C8 -- the Tech member column's one-member scroll over a picture CtMenuRenderTech already drew for the old member:
// the column's TEXT (BG1 only: the offset-per-tile words are 20xx) and the sprites ($0D30) moved `o` px (4..44) over the
// old window (`oldScroll`) and the incoming member (below for dir +1); the boxes (BG2) stay.
void CtMenuRenderTechMemberSlide(const CtMenuHost* h, uint8_t* fb, int oldScroll, int dir, int o, int handRow);
void CtMenuRenderTech(const CtMenuHost* h, uint8_t* fb, int charId, int tab, int row, int scroll,
                      int selSlot, int fresh, int barId, int barX, int barN);
void CtMenuTechHandCell(int cell);
// the Tech list's own handler has run since the A that opened it (C2/B724 JSR C2/BDEB is its: the A's frame has no block)
void CtMenuTechListRan(int ran);   // the list's hand on nav cell `cell` instead of its row (-1 = the row)

// THE GROUP MACHINE (disasm/notes/menu2_tech_screen.md). Fill techId[]/mp[]/group[]/dim[] with the
// techs `charId` can see under `tab` (0 Single / 1 Double / 2 Triple) and return the count. This is
// C2/BCCD + C2/BD00 verbatim: walk the tab's group window (C2/BD65), keep the groups whose CC/2963
// character mask contains charId, and emit the ids whose bit is set in the menu bitfield -- so a
// DOUBLE appears in both participants' lists and a TRIPLE in all three. mp[] is what the tech costs
// THIS character (C2/BC22: a single indexes CC/253B by id, a double/triple through $7E:1607 +
// charId*64 + techId), already halved by an $A2/$A3 accessory. dim[] marks the one NOT-YET-LEARNED
// tech FF/F876 adds per character so the screen can preview it. Any out pointer may be NULL.
int  CtMenuTechList(const CtMenuHost* h, int charId, int tab, int* techId, int* mp, int* group,
                    int* dim, int cap);
// FF/F863 + FF/F876 + C2/B979 -- the menu's own tech bitfield $7E:7F00[37], copied from the persistent
// $7E:2837 only for groups whose every member is in the roster, plus the previewed next-to-learn tech
// per character (its id lands in nextIdx7[charId], 0xFF = none). nextIdx7 may be NULL.
void CtMenuTechBits(const uint8_t* mem, uint8_t* bits37, uint8_t* nextIdx7);
// C2/BC22 + C2/BC59 -- the MP `charId` pays for `techId` under `tab`, halved once by accessory $A2 and
// twice by $A3 (both rounding up).
int  CtMenuTechMp(int charId, int techId, int tab, int accessoryId);
// The tech's DESCRIPTION (CC/3A09 + id*2, dictionary-decoded at bake time) -- what C2/BE16 puts in the
// bottom bar. ids 0x76..0x78 are the Single/Double/Triple tab names. Returns the length written.
int  CtMenuTechDesc(int techId, char* out, int cap);
// ==== THE FIELD RESTORE (disasm/notes/menu2_tech_effect.md) ====
// C2/8F55 is ONE routine shared by the field TECH cast (C2/B4FC) and the field ITEM use (C2/B1BF) --
// exactly two callers in the whole ROM -- and the only byte that differs between them is $0DBF.
// C2/8ECC's amount, six 16-bit words: out6[0..2] = HP per party index, [3..5] = MP. amtSrc = $0DBF
// (bit7 set = an item's flat constant, clear = a tech's caster MAGIC), nib = $0DC0, cur = $7E:0D00.
void CtMenuRestoreAmounts(int amtSrc, int nib, unsigned cur, int* out6);
// FF/F82E's nine field-usable tech ids, minus FF/F876's previewed not-yet-learned one (whose marker
// writes bit7 CLEAR over the same byte). No double or triple is ever field-castable.
int  CtMenuTechFieldUsable(const uint8_t* mem, int charId, int techId);
// C2/B3E6's gate ladder + C2/8F55. 1 = cast (the caller runs the 10-frame pre-roll, the chime and the
// popup), 0 = REFUSED (the caller plays the buzz id $01). shown6 receives the floated numbers, -1 for
// a pool this tech does not touch. rngCur = $7E:0D00, advanced once per menu FRAME.
// C2/B870 for a TECH: the same 2-bit $0DBC mask (bit1 HP restorable, bit0 MP), with the pools taken
// from the tech's own $0DBD -- plus C2/B839's rule that the mask goes to ZERO once the caster's MP has
// dropped below the cost, so a second A on an emptied caster buzzes.
int  ctTechCastPoseByte(int charId);   // $C2:B519[charId], the pose $19E1 gets (SEVEN entries)
int  CtMenuTechTargetMask(const uint8_t* mem, int casterCharId, int techId, int tab, int tgtSlot);
int  CtMenuTechUse(uint8_t* mem, int casterCharId, int techId, int tab, int tgtSlot,
                   unsigned rngCur, int* shown6);
// C2/B502-B516: the caster pays the MP -- after C2/8F55 has returned, i.e. after the popup (CtMenuTechUse does not).
void CtMenuTechPay(uint8_t* mem, int casterCharId, int techId, int tab);
// The target overlay (substate 4) as C2/BE79 / C2/BEDC build it; see menu.c.
void CtMenuRenderTechTarget(const CtMenuHost* h, uint8_t* fb, int charId, int tab, int row, int tgt,
                            const uint16_t shown[3][2], int casterMp, int castPose,
                            int popCurve, int popPool, const int popAmt[3][2], int barId, int barX, int barN);

// Decode single-tech `techId`'s name from ROM $CC/15C4 + techId*11 (techId 1=Cyclone). Same CT mapped
// decoder as CtMenuItemName; returns the length written (0/"" if rom NULL/short). cap should be >= 12.
int  CtMenuTechName(const uint8_t* rom, long romLen, int techId, char* out, int cap);

// SCREEN 4 = CONFIG (C2/BF35), rendered READ-ONLY (toggling is deferred). The ROM screen is a GRID of
// icon toggles + a bottom bar showing the highlighted setting's NAME + VALUE; we render the same data as
// a layout-faithful TEXT LIST (name + value per row) + the highlighted bar (the menu standard is layout-
// faithful, reuse chrome, not pixel-exact icons). Provenance (each pinned this pass from the ROM config
// handler + oracle RAM, byte-validated vs the live $0DCF the ROM computes at C2/C4BC):
//   settings bytes   $7E0D86..$0D8F+ (12 live config bytes, persistent; the working copy the cell A-
//                    handlers C2/C017.. toggle, mirrored to $0F09 for the checkbox draw C2/C3E4).
//   per-cell RAM off  ROM C2/C4F9[cell] -> which $0D86 byte holds this setting's value (the C2/C4BC
//                    `LDA $0D86,Y` where Y = $C2C4F9[cursor]).
//   per-cell base     ROM C2/C505[cell] -> value-string base; value string index = configByte + base
//                    (C2/C4BC `ADC $C2C505,X` -> $0DCF), byte-exact vs the oracle's live $0DCF.
//   string table      ROM FF/D3FE = master 16-bit-LE pointer table (bank-FF relative); entry i (i<12) =
//                    the setting NAME, entry (base+configByte) = the VALUE string (On/Off/N/Y/1..8/
//                    Standard/Custom/Wait/Active/...). Strings at FF/D462+, NUL-terminated, CT mapped
//                    encoding (0xE0='/', 0xE8='.', 0xDF = a cursor-icon glyph -> dropped in the text list).
#define CT_MENU_CONFIG_COUNT 12
// Decode config setting `idx` (0..11, ROM/grid order): NAME -> name[nameCap], VALUE -> val[valCap] (both
// from h->rom + the live $7E0D86 config bytes in h->mem). Returns 1 if idx is a valid setting, else 0
// (and clears the buffers). Used by CtMenuRenderConfig AND the menutest assertion.
int CtMenuConfigItem(const CtMenuHost* h, int idx, char* name, int nameCap, char* val, int valCap);

// ==== SCREEN 4 IS INTERACTIVE (disasm/notes/menu2_config_toggles.md) ====
// A 21-CELL 2-D cursor machine over 12 settings packed into THREE saved bytes. $0054 = the cell id,
// $0068 = the substate (1 = the 2x5 grid, 2 = the Control-Pad button editor, 3 = the Battle-Gauge
// 3-option row), $0D86..$0D91 = the twelve WORKING bytes (valid only while the menu is open),
// $0F09..$0F12 = the checkbox mirror -- three of whose slots ($0F0D/$0F0E/$0F0F) are NOT settings but
// the sub-mode flags -- and $2990/$2991/$2992 = the PERSISTENT packed config, which already sits
// inside save record 0, so config survives save/load with no extra code.
// Nothing is written to the packed bytes while you are on the screen: B (the screen's ONLY exit --
// X/L/R/Select/Start are inert, measured) runs the C2/934F pack on the way out.
#define CT_MENU_CONFIG_CELLS 21          /* ROM nav table FF/B5A0: 21 records of 6 bytes */

// C2/92F4, run ONCE per menu-open from C2/8133: $2990/$2991/$2992 -> the twelve $0D86..$0D91 bytes
// (which it clears first). $0D91 has no packed source and is never written -- which is why cell 5's
// value string (base 0x1E + $0D91) is always the EMPTY entry and "Battle Speed" shows no value.
void CtMenuConfigUnpack(uint8_t* mem);
// C2/934F, run ONLY from C2/BF67 (B out of the config screen): the twelve bytes -> $2990-92.
void CtMenuConfigPack(uint8_t* mem);
// C2/C3B5 + C2/C43A screen BUILD: copy the ROM nav table FF/B5A0 to its live home $7E:9380 (the ROM
// does this through the bank-FF display-list interpreter C2/ED31), mirror $0D86..$0D8F ->
// $0F09..$0F12, and zero the three sub-mode flags $0F0D/$0F0E/$0F0F.
void CtMenuConfigBuild(uint8_t* mem);
// C2/95D4..C2/960A, the NEW-GAME defaults: 12 bytes from C2/FCA6 -> $2990..$299B and 9 bytes from
// C2/FCA9 -> $0408..$0410 (the same nine, which is why the ROM block overlaps at +3).
void CtMenuConfigDefaults(uint8_t* mem);
// C2/C54F: rebuild the LIVE merged button map $2993..$299B -- the ROM defaults C2/FCA9, overwritten by
// the custom map $0408..$0410 when Control Pad ($0D87) is Custom. This is what makes that setting bite.
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
// THE LIST MACHINERY every scrolling menu list shares (Item, Equip, Tech, the shop), on the ROM's own WRAM bytes.
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
// The cell's cursor anchor, straight out of its nav record's first two bytes (C2/E964 `LDA $9380,X /
// STA $0770` is the hand's own x/y). Our hand sits at exactly (x, y - 8) -- an identity, not a tuning:
// the placement the grid cells already used matches every one of the 21 records at that offset.
void CtMenuConfigCellPos(const uint8_t* mem, int cell, int* x, int* y);

// One frame of the config screen. Mirrors C2/BF45 exactly: the cursor moves FIRST (C2/E9E2), then A/B
// are decoded (and eaten on any frame the substate changed, C2/E98D), then the per-cell handler runs
// off the C2/BFFF table. Returns 1 when B has left the screen (the pack has already run) -- the caller
// then does the C2/9A98 return to the ribbon.
typedef struct CtMenuConfigIO {
  int a, b;          // in: A / B this frame (EDGES -- there is no auto-repeat on this screen)
  int dir;           // in: 0 Right / 1 Left / 2 Down / 3 Up, -1 = none ($0D1E, 0x80 = none)
  int start;         // in: START (the button editor's "restore defaults", C2/C188)
  int blip;          // out: post the menu blip (C2/EAC2 id $00). No buzz exists on this screen.
  int apuStereo;     // out: >= 0 -> post APU cmd $F3 with this parameter (cell 0 only, C2/C028)
  int winLoad;       // out: C2/C511 ran -- the window tiles + colours are reloaded from $0D8C (cell 6's edit)
} CtMenuConfigIO;
int CtMenuConfigFrame(uint8_t* mem, int* cell, int* substate, CtMenuConfigIO* io);

// CtMenuRenderConfig draws the CONFIG screen (read-only): a header, the 12 settings as a name+value list
// with the hand on `cell` (0..20, the ROM's $0054 -- placed from that cell's nav record), and a bottom
// bar echoing the setting `$0417` describes. `sub` is the ROM's $0068 (1 grid / 2 button editor / 3 the
// Battle-Gauge row) and only selects which panel the hand is allowed to sit on. The ten checkboxes are
// sourced from the $0F09..$0F12 MIRROR with the C2/C42F polarity (slot 0 inverted, because Stereo On is
// the ZERO value), so the three sub-mode flags show the way the ROM shows them. Deferred (marked): the
// exact ROM icon grid pixels.
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

// SCREEN 5 = FORM / PARTY-EXCHANGE (handler C2/C621). The party-order overlay: the 3 active-party panels
// + a picker that selects two members to SWAP. Two-substate flow (ROM $0068), verified from the disasm
// (the overlay is a task-spawned slide-in that never settles in the headless --peek, so the layout is a
// documented APPROXIMATION; the MECHANISM below is byte-exact from the ROM):
//   picker idle (C2/C65D): Up/Down move the cursor over the active-party slots; A -> first pick (C2/C674:
//     the cursor slot $0F0D -> $0F00); B -> ribbon (C2/C6B3 -> C2/9A98).
//   second-pick idle (C2/C6B9): A -> second pick (C2/C6D5: $0F0D -> $0F01) then the SWAP; B -> cancel pick.
// `cursor` = the active-party slot (0..2) the picker rests on; `picked` = the first picked slot (-1 = none;
// the second pick fires the swap). Reserve roster (chars NOT in the active 3 — none available at storyline
// 08) + the exact ROM slide-in animation / icon-grid pixels are DEFERRED. Reuses the party panels + chrome.
// `panel9` is C2/CD2B's own $7E:0F02..$0F0A array (see CtMenuFormPanels): it decides which panels draw
// DIM -- locked or empty, not "not the lead". The MEASURED identity behind this screen: the entered-Form
// BG1 tilemap is byte-identical to the ribbon's opt-4 hover and BG2 differs only where the ribbon box
// was, so the Form frame is that frame minus the box. There is no slide-in overlay and no second layout.
// `pickBlink` is the 4-on/4-off phase of the PICKED member's second hand ($7E:0DA0, the same
// counter the Item screen's pinned-row hand uses): non-zero = the frame it is drawn on.
void CtMenuRenderForm(const CtMenuHost* h, uint8_t* fb, int cursor, int picked, const uint8_t* panel9,
                      int pickBlink);
// C2/CD2B's panel array $7E:0F02..$0F0A: 0x80 empty, 0x40|i LOCKED ($7F:01DF & (0x80 >> charId)),
// i selectable. Entries 3..8 are ALWAYS 0x80 in the X-menu -- $0D36 is 0xFF for its whole life
// (C2/989E), so C2/CD72 never runs the reserve scan; reserves live in menu mode 5 / screen $07.
void CtMenuFormPanels(const uint8_t* mem, const uint8_t* glb, uint8_t* panel9);
// C2/C649: the cell the Form picker opens on -- the first panel entry with neither the LOCKED
// (0x40) nor the EMPTY (0x80) bit set.
int CtMenuFormEntryCell(const uint8_t* mem, const uint8_t* glb);
// C2/CC4F: hand CtMenuChrome the LIVE panel array so the FORM views' per-panel dim comes from the
// current roster instead of the one the chrome tilemap was captured on. NULL leaves the map alone.
void CtMenuSetFormPanelDim(const uint8_t* panel9);
// ---------------------------------------------------------------- THE MENU SPRITE ANIMATOR
// C2/F643 spawns ONE task (C2/F657) that walks the nine sprite blocks at $7E:1800 + 0x40*n once a
// frame; C2/F69B is its advance. Only the Form screen ever asks for an animation (C2/C973), so this
// is what makes the exchange cursor's member walk on the spot.
//   CtMenuSprReset      -- every block back to anim 0 and its STANDING pose (call on menu open)
//   CtMenuSprFormAnims  -- C2/C973: everyone still, the CURSOR's member walking (anim 1), the
//                          PICKED member on anim 0x0B
//   CtMenuSprTick       -- one frame of C2/F657 + C2/F69B, over all seven characters
//   CtMenuSprPose       -- the assembly frame that character is showing right now (block+0x03)
void CtMenuSprReset(const CtMenuHost* h);
void CtMenuSprFormAnims(const CtMenuHost* h, int cursorCell, int pickedCell);
void CtMenuSprTick(const CtMenuHost* h);
int  CtMenuSprPose(const CtMenuHost* h, int charId);

// The party-exchange SWAP, byte-exact ROM C2/C70C: exchange the two active-party slot bytes at $7E2980+a
// and $7E2980+b (LDX $0F00; LDA $2980,X; STA $00; LDY $0F01; LDA $2980,Y; STA $2980,X; LDA $00; STA $2980,Y).
// This is the FORM screen's ONE intentional RAM write, made only when the player confirms an exchange.
// a/b out of 0..2 (or NULL mem) = no-op. A swap of the ACTIVE-party ORDER only reorders $2980-82; the ROM's
// on-field PC-object reconcile (respawn C0/597D, run by the menu-exit party-reconcile when $2980 changed) is
// deferred here — the new order propagates to the field on the next scene load via the carry-party ($2980).
// The FORM screen's ENTRY GATE (ROM C2/C627) + the roster builder behind it (C2/CD2B).
// activeFree = $0F0B (active members that are NOT locked by $7F01DF & (0x80>>charId));
// reserveN   = $0F0C (non-empty $2983..$2988, scanned only when $0D36 == 0).
// CtMenuFormCanOpen = the ROM's own predicate ($0F0B + $0F0C >= 2). When it is false the ROM
// REFUSES to open screen 5 -- $0067 back to 0, $0068 = 1 -- which is the case on every oracle
// state this repo owns. `glb` is the $7F0000 story-flag page (CtSceneMemGlb).
void CtMenuFormRoster(const uint8_t* mem, const uint8_t* glb, int* activeFree, int* reserveN);
int  CtMenuFormCanOpen(const uint8_t* mem, const uint8_t* glb);
void CtMenuFormSwap(uint8_t* mem, int a, int b);

// The SAVE screen's ENTRY GATE (ROM C2/CEDC, fed by the menu-open computation C2/9875): save is
// allowed on the overworld ($0100 >= 0x1F0) or on a field map only when $7F01CE bit15 (the
// save-point flag) is set, and only while none of the block bytes $0294/$0299/$029E carry bit6.
// When it is false the ROM REFUSES to open screen 6 -- $0067 back to 0, $0068 = 1 -- which is the
// case on every FIELD state this repo owns; saves/ow_w0.state (overworld) opens it.
int  CtMenuSaveCanOpen(const uint8_t* mem, const uint8_t* glb);

// SCREEN 6 = SAVE (handler C2/CED4 -> slot-loader C2/D0E5; 3-slot scan loop C2/D156-D197), rendered
// READ-ONLY: the 3 numbered save-file slots with their summary (leader/LV/location/time) or "Empty".
// The empty-vs-used decision is byte-exact ROM (validity check FF/F9C4, run with DB=$30 = CT's SRAM
// save-file image): a slot is USED iff its signature word $30:7FF8+slot*2 == 0xE41B (FF/F9D8) AND its
// checksum word $30:7FF0+slot*2 matches the computed checksum (FF/FBE7); FF/F9C4 returns $00 = 0x0000
// used / 0x8080 bad-signature / 0x4040 bad-checksum, and the handler C2/D171 `LDA $00 / BEQ` treats 0 as
// USED (reads the summary C2/D19F) else EMPTY (draws the 'N' no-data marker C2/D296). Summary fields
// (reader C2/D19F, per-slot base $7B in bank $30): party/leader $+0x0580, char sheet $+0x0200+id*0x50
// (+0x12 LEVEL -- C2/D1D3 `LDA #$0200 / ADC $7B / ADC $4216`, then C2/D1DD `LDA $300012,X`), leader name
// $+0x05B0+id*6, SAVE NUMBER $+0x059C (word, ROM-capped at 999 -- C2/D21B), location/area
// $+0x05F3 & 0x1FF, and the 10-byte block at $+0x05E0 that opens with 24-bit GOLD and continues into
// the play clock. The ROM also copies words +0x059E and +0x0793 (not modelled here). Our engine has
// no SRAM (h->sram == NULL) so all 3 slots read EMPTY — this MATCHES the oracle fresh state
// (saves/menu_open_gato2: three no-data 'N' slots, scratchpad sav1.png).
//
// ==== THE SAVE-WRITE, IMPLEMENTED (was deferred) ==== A on a slot now performs the ROM's own write.
// FF/F9FB packs EIGHT records named by the table at $FF:FA7B — {srcAddr:2, srcBank:1, len:2} —
// consecutively into the slot: $7E:2400+0x5B0 (inventory/sheets/party), $7E:2C23+0x33 (names, gold),
// $7E:0400+0x10 (the play clock), $00:0100+0x10 (location), $7F:0000+0x200 (story flags),
// $00:027E+2, $00:0290+0x11, $7E:2C7C+0x1E = 0x0834 bytes. Then the FF/FBE7 checksum at
// $30:7FF0+slot*2, the $E41B signature at $30:7FF8+slot*2 and the last-slot byte at $30:7FE0.
// Slot bases are the ROM table $FF:FBE1 = $30:6000/$6A00/$7400 (image 0x0000/0x0A00/0x1400).
// The SAVE COUNTER $7E:299C is incremented (saturating) BEFORE the pack, per FF/FA05 -- it lies
// inside record 0 at slot offset +0x059C, the field the summary shows as "Save #", so bumping it
// afterwards would record a stale number in every save.
// VALIDATED END TO END against saves/com_jogo_salvo.state, a game saved by the ROM itself: repacking
// that state's own WRAM reproduces its SRAM slot 0 in 2098 of 2100 bytes — the two that differ are
// $7E:0400/0401, the play clock, which ticked between the ROM's save and the state capture — and the
// checksum comes out 0x6B8C against the 0x6B8C the ROM stored.
// STILL NOT IMPLEMENTED: the Equip-change / Config-toggle writes of the other read-only sub-screens.
#define CT_MENU_SAVE_SLOTS 3

// Write the live game into save slot `slot` of the bank-$30 image `sram` (>= 0x2000 bytes), packing
// from `wram` = the flat 128 KB WRAM ($7E at 0, $7F at 0x10000 — CtWramSys's own view). Returns the
// number of bytes packed (0x0834 with the ROM's table intact), 0 on failure. This is ROM FF/F9FB;
// its caller C2/CF37 posts the save jingle FIRST and then calls it.
int CtMenuSaveWrite(unsigned char* sram, long sramLen, int slot, unsigned char* wram);

// FF/FBE7 over slot `slot` of a bank-$30 image, and that slot's base offset. Exposed so a check can
// run the checksum over a save the ROM ITSELF made and compare with the word the ROM stored — the
// algorithm graded against the cartridge, not against our own output.
unsigned CtMenuSaveChecksum(const unsigned char* sram, int slot);
unsigned CtMenuSaveSlotBase(int slot);

// ==== THE NATURAL LOADER (disasm/notes/boot_loader.md) ==========================================
// FF/F9C4, the FULL slot gate: signature $E41B at $30:7FF8+slot*2 AND the FF/FBE7 checksum matching
// $30:7FF0+slot*2. Returns 1 = USED (the ROM's A = $0000), 0 = empty/corrupt ($8080 / $4040).
int CtMenuSaveSlotValid(const unsigned char* sram, long sramLen, int slot);
// FF/FAA3, the LOAD: unpack save slot `slot` of the bank-$30 image back into the flat 128 KB `wram`
// ($7E at 0, $7F at 0x10000) through the SAME 8-record table the writer packs with. Returns the byte
// count (0x0834), 0 on failure. It neither validates (that is FF/F9C4, run by the caller) nor
// reboots (that is C2/8320's `JML $C0:0000`, also the caller's job).
int CtMenuSaveLoad(const unsigned char* sram, long sramLen, int slot, unsigned char* wram);
// C2/95BB and C2/956E, the two wipes the boot scan and the mode-0 shell run. `charBase` is the baked
// $CC:0000 char-sheet template (0x280 bytes); NULL leaves $7E:2600 alone.
void CtMenuNewGameWipeLight(unsigned char* wram);
void CtMenuNewGameWipe(unsigned char* wram, const unsigned char* charBase, long charBaseLen);
// C2/8D6A (leaf 9 of the $C2 gate, called from the boot at C0/0057 `LDA #09 / JSL $C2:8004`):
// scan slots 0..2 with FF/F9C4; a valid one -> light wipe and return 2, none -> full wipe and
// return 0. The return IS the value the ROM stores in $7F0061, which is what scene 0's boot script
// branches on. Pass wram = the engine's flat 128 KB image.
int CtMenuSaveBootScan(const unsigned char* sram, long sramLen,
                       unsigned char* wram, const unsigned char* charBase, long charBaseLen);

// ---- the LOAD screen (menu mode 0 / screen $0E, C2/E60B). Rows: 0..2 = the three save slots,
// 3 = "New Game". See the block comment over CtMenuRenderLoader in menu.c for every citation.
int  CtMenuLoaderOccupancy(const CtMenuHost* h, unsigned char* occ);   // $0D49..$0D4C (occ may be NULL)
int  CtMenuLoaderStep(const CtMenuHost* h, int row, int up);           // C2/E743 (up = $005A bit3)
// The screen's own $7E:9380 node graph (C2/EA27 stride 6). `dir` is the ROM's direction index
// from its $F5 scan: 0 Right, 1 Left, 2 Down, 3 Up -- Left/Right are self, i.e. inert.
int  CtMenuLoaderNeighbour(int row, int dir);
void CtMenuLoaderHandOam(int row, int* x, int* y);                    // node +0 / +1
int  CtMenuLoaderDefaultRow(const CtMenuHost* h);                      // C2/E7CF ($30:7FE0, else 3)
const char* CtMenuLoaderChapter(int storyline, int used);              // C2/D266 -> $FF:D024/$FF:D03E
const char* CtMenuLoaderEra(int locId);                                // C2/D3AD -> $FF:D296/$FF:D396
void CtMenuChromeLoaderAt(uint8_t* fb, int cursor);   // cursor row = the one NOT halved (-1 = none)
void CtMenuRenderLoader(const CtMenuHost* h, uint8_t* fb, int cursor);
typedef struct CtMenuSaveSummary {
  int used;      // 1 = valid save present (FF/F9C4 signature $E41B), 0 = empty / no-data
  int leaderId;  // party leader char id (save-file $+0x0580, bit7 = empty slot)
  int lv;        // leader level (save-file char sheet $+0x0200 + id*0x50, +0x12)
  int locId;     // location / area id (save-file $+0x05F3 & 0x1FF)
  int gold;      // gold, 24-bit at save-file $+0x05E0 (head of the 10-byte block C2/D238 MVNs) --
                 //   pixel-verified: a real saved game reads "1105G" with $+0x05E0 = 1105
  int saveNo;    // the SAVE NUMBER at $+0x059C (word, ROM-capped at 999 by C2/D21B). menu.h used to
                 //   call this "gold"; the same capture shows it is 1 next to the screen's "Save # 1"
  int hh, mm;    // play-time hours:minutes (save-file $+0x05E0 clock block, $0400-06 digit layout)
} CtMenuSaveSummary;

// Read save-file SLOT `slot` (0..2) summary from h->sram (the bank-$30 image) per the ROM validity/summary
// citations above. Returns 1 (out->used=1, fields filled) if the slot holds a valid save, else 0 (out
// zeroed = empty). h->sram == NULL (our engine — no SRAM) => 0 for every slot. Used by the render + test.
int CtMenuSaveSlot(const CtMenuHost* h, int slot, CtMenuSaveSummary* out);

// CtMenuRenderSave draws the SAVE screen (read-only): the 3 numbered slot rows (leader/LV/time if used,
// else "No data") with the hand on `cursor` (0..2). The cursor's whole row is BRIGHT (entered-Save CGRAM
// palette 4, body 231) and the other rows draw the same palette through the SNES half color math (body
// 115); the hand's ink box lands on the oracle's x8..23, y(31+32*cursor)..(42+32*cursor). cursor < 0 =
// the ribbon HOVER PREVIEW: every row bright, no hand (oracle g_opt5 OAM). B exits to the ribbon; the
// save-WRITE is DEFERRED (see boundary note). All slots render empty in our SRAM-less engine.
void CtMenuRenderSave(const CtMenuHost* h, uint8_t* fb, int cursor);
// The overwrite prompt is up (C2/D4BB): it holds the screen's one nav hand, so the slot list draws none.
void CtMenuSavePromptUp(int on);
// The ribbon's Save preview after B off the Save screen keeps that screen's summary panel (C2/D0DB redraws nothing):
// `slot` = the slot the cursor left, -1 = a preview the ribbon built (no panel).
void CtMenuSetSaveHoverPanel(int slot);
// Screen 6's OVERWRITE PROMPT (sub-state 2, opened by C2/CF19 when A lands on an occupied slot). `sel`
// is the ROM's $0054 over the same $7E:9380 node graph as the slot list: 3 = Yes (OAM 184,63), 4 = No
// (OAM 184,79), pre-seeded to 3; anything else draws nothing. A saves ONLY on 3.
void CtMenuRenderSavePrompt(const CtMenuHost* h, uint8_t* fb, int sel);

// A placeholder for any unexpected sub-screen id (all 1..6 are now built):
// a titled window + "Not Implemented". `screen` is the $0067 id (1..6). B returns to the ribbon.
void CtMenuRenderPlaceholder(const CtMenuHost* h, uint8_t* fb, int screen);

// Decode item `id`'s name from the ROM $CC/0B5E item-name table into `out` (NUL-terminated ASCII).
// Table = FIXED 11-byte records, name(id) = rom[0x0C0B5E + id*11 .. +11] (ROM-verified via
// assets/text.py item_names(): id 189=Tonic, 200=Shelter, 214/0xD6=Pendant). CT "mapped" encoding
// (0xA0-0xB9='A'-'Z', 0xBA-0xD3='a'-'z', 0xD4-0xDD='0'-'9', 0xE7='\'', 0xE8='.', 0xEF & 0xFF=space,
// 0x20-0x2F=equip type icon -> space); records are space-padded, so leading+trailing spaces are
// stripped. Returns the length written (0 and out="" if rom is NULL or too short). cap should be >= 12.
int CtMenuItemName(const uint8_t* rom, long romLen, int id, char* out, int cap);

// ---- THE SHOP (menu mode 2 = screen $0C, disasm/notes/shop_screen.md) -----------------------------
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
  int arrowRun, arrowOn, arrowCtr;             // the ▼ task ($E0 != 0), its 8/8 blink ($0DA0 bit0, $0DA3)
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
// The shell's P frame (C2/D52B spawns the ▼ task with the fade-in): that task's first step.
void CtMenuShopSpawnFrame(CtMenuShop* s);
void CtMenuRenderShop(const CtMenuHost* h, uint8_t* fb, const CtMenuShop* s);
// The ROM's per-item price rule C2/DB9F (word $7F:01CD multiplier) and the sell price C2/DF51.
int  CtMenuShopBuyPrice(const uint8_t* rom, long romLen, const uint8_t* glb, int id);
int  CtMenuShopSellPrice(const uint8_t* rom, long romLen, int id);
// The menu sprite machine's per-CHARACTER request ($1811 + charId*0x40 = anim), as C2/DD7C writes it.
void CtMenuSprRequest(int charId, int animId);
// Block 7 ($19C0), the Tech caster's copy: C2/BA09's character, its request/loop ($19D1 / $19E1), its character and pose.
void CtMenuSprBlock7(const CtMenuHost* h, int charId);
void CtMenuSprRequest7(int animId, int loop);
int  CtMenuSprChar7(void);
int  CtMenuSprShown7(void);
// The pose the PICTURE shows: the machine's pose of the previous tick (the tile DMA goes out one frame late).
int  CtMenuSprShown(int charId);

// The play-clock tick (ROM C2/85AE, called once per frame from the shared NMI path): +1 on the
// $0400 frame digit, rippling through the wrap limits [60,60,10,6,10,10]; all six wrapped ->
// $0406 = 1 (the menu then displays the 99:59 cap). Inert while $0407 != 0 (the pause gate the
// menu entry's $0407=FF write uses, C0/1946; cleared by the field resume's STZ $0407, C0/2844).
void CtMenuClockTick(uint8_t* mem);

#endif
