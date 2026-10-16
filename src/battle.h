// battle.h — the battle MODULE boundary (ROM entry JSL C1/0000 at C0/18A7; boot CF/FB65 -> C1/8000).
//
// PHASE 1 = DATA ONLY: CtBattleRun stages the canonical battle data from the field->battle
// interface (the $7E image the field's C0/1770 consumer prepared — see field.c) exactly like the
// ROM boot chain does (FD/A982: party seats FD/B274, enemy ids FD/B2A9, enemy stat blocks FD/B438
// from ROM CC/4700 + id*23, PC blocks FD/AE52 from the $7E2600 char sheets), then runs a minimal
// frame loop and supports ONE canonical exit: the ROM's own controller-2 DEBUG INSTANT-END
// (C1/8357: zero every enemy curHP $5FB0,Y + set $5FCA,Y=0x80, store outcome 0). The battle
// LOOP itself (ATB tick, damage pipeline, enemy AI, HUD) is NOT yet derivable from the ROM —
// see disasm/notes/battle_menu_synthesis.md "Open questions" — and is a later milestone.
#ifndef CT_BATTLE_H
// The battle HUD strip's iris arms on this host frame: battle_hud_render.md Q6's oracle-pinned
// settle offset (82 module frames) minus the 13-frame ladder measured in battle_hud_band_anim.md
// §5, so the arm is DETERMINED, not chosen. Armed in CtBattleTick, consumed by CtBattleHudRender.
#define CT_BHUD_ARM_FRAME 69

#define CT_BATTLE_H

#include <stdint.h>

// Input bitmask routed into CtBattleTick's `input` arg while the field is in battlePhase (the field
// pipeline is frozen in battle so these edges are single-use). Edges = "pressed THIS frame"; the *_HELD
// bits carry the raw held state so CtBattleTick can run the menu-nav auto-repeat (battle_hud_render.md
// ADDENDUM 2 §B). Headless test hooks (CT_BATTLE_MASHA / CT_BATTLE_SCRIPT) synthesize their own input
// inside CtBattleTick; the headless CtBattleRun passes input=0.
#define CT_BIN_A     0x01   // A edge  -> confirm (Att. on cursor row 0 / target-select confirm)
#define CT_BIN_UP    0x02   // Up edge   (fresh press) -> cursor -1 mod 3
#define CT_BIN_DOWN  0x04   // Down edge (fresh press) -> cursor +1 mod 3
#define CT_BIN_UPH   0x08   // Up held   (auto-repeat source)
#define CT_BIN_DNH   0x10   // Down held (auto-repeat source)
#define CT_BIN_B     0x20   // B edge  -> CANCEL target-select (X/Backspace; battle_actionfx.md P5.1)
#define CT_BIN_LEFT  0x40   // Left edge  -> target-select PREV (battle_scenario2.md T3.1, C1/176C: Up/Left
#define CT_BIN_RIGHT 0x80   //   both -1); Right edge -> target-select NEXT (Down/Right both +1). Command-
                             //   menu nav (Att/Tech/Item, battle_hud ADDENDUM 2 §B) stays Up/Down-only --
                             //   these two bits are consumed ONLY by the target-select $9614 nav (no
                             //   auto-repeat: the dossier documents none for this state, unlike §B's menu).
#define CT_BIN_LRH   0x200  // L AND R HELD (level): the run-away counter CF/E70B reads `$4218 AND #30 CMP #30` itself,
                             //   every frame, straight off the auto-read joypad -- no edge, no latch, no $00F6.
#define CT_BIN_AH    0x100  // A HELD (level, not an edge). The victory results dwell is the ONE battle
                             //   input the ROM reads from the HELD copy: CD/01F2 `LDA $F8 / BPL` tests
                             //   $00F8 bit7 = A held (battle_results_box_timing.md 1.4/PART 2). B does
                             //   NOT cut it ($F8 = 0x0A, bit7 clear -- measured byte-identical to no
                             //   input), so this bit is deliberately A-only.

// The enemy action-window ANIMATION length in render frames (battle_actionfx.md A3.1: GATO's rear-up
// f505->f554 = 49f), shared between battle.c (gates h->fxEnemyRearup: "rel < this -> animating, else
// idle for the rest of the frozen span") and field.c (battleHetakeRender scales the Hetake leap-arc's OWN
// measured trajectory, battle_scenario2.md T1.4, over this SAME already-pinned window rather than
// inventing a new per-enemy-type timing constant -- battle_loop.md's pacing/freeze spans stay untouched).
#define CT_GATO_WINDOW 49

// ======== CRONO'S CYCLONE — the TECH GRAPHIC EFFECT (disasm/notes/battle_tech_cyclone_fx.md) ========
// User report: "in the original game, when doing the cyclone the screen gets darker and the enemies hit keep
// flashing". Both halves are oracle-measured on the same gato_press_action + $2837|=0x80 cast (dispatch
// f438) and re-confirmed on the forest cast (dispatch f388), i.e. arena-independent.
// (a) THE DARKEN is not a curve: it is the script's own `80 02 F0` / `80 12 F0` (C1/621B -> D1/F67A), two objslot
//     fade records over CGRAM $10-$47 / $48-$7F stepping 8 every 2 frames FF -> 80 and back (battle.c
//     battleFxPalStage). The measured CGRAM BG1-7 sum ratios -- 0.923 at rel 38, the 0.481 plateau from rel 68,
//     1.000 again at rel 236 -- are that product's truncated sums; the constants that replayed them are gone.
// (b) THE VICTIM BLINK (4 frames white, 5 normal, then a sustained flash) is the script's own op 6C / 69 / 6A on
//     the victim track and the $A018 list track (battle.c battleAscTick, C1/5F13); the measured curve that replayed
//     it (74 + 4-in-9 through 200, 206..214) is gone.

// battle_crit.md §C.2: the Crono-crit whole-screen FLASH lifetime in render frames (sharp onset at the crit
// strike, ~linear fade to baseline over ~15f: fb mean 80->126 peak -> 80 over f137..f152). Shared: battle.c
// (battleAdvanceAction seeds critFlashLeft=this at the crit strike + counts it down); field.c (the BG-brighten
// intensity = critFlashLeft/this, so the two must agree).
#define CT_CRIT_FLASH_DUR 15
// battle_timing_audit.md §2: Flash 1 (the primary/first-slash strike, rel+50) fades faster (~7f) than Flash 2
// (the second slash, rel+91, ~15f = CT_CRIT_FLASH_DUR). Both peak at CT_CRIT_FLASH_PEAK (field.c denom = critFlashDur).
#define CT_CRIT_FLASH_DUR1 7

// ================= THE VICTORY RESULTS TIMING (disasm/notes/battle_results_box_timing.md) =================
// The FIXED, non-skippable hold between the victory DISPATCH and the first reward glyph. Shared: battle.c
// (the results phase machine) + field.c (the box render gate).
//
// `victoryHold == 1` IS the ROM's dispatch frame V (C1/8186 -> C1/356D, `INC $A0FE`). C1/356D's three per-seat removals
// take V, V+1 and V+2 (each followed by C1/007E's wait), and `C1/35CA LDA #28 / STA $5DF1,Y` arms the ladder in V+3
// (battleVictoryTeardownFrame; disasm/notes/battle_end_ui_teardown.md sec. 9). MEASURED, the rule's cartridge on GATO's
// fight (Dark Matter's kill, tools/battle_ab.py): V c1322, `$5DC9 = 40` c1325, `$5DC9 = 00` c1389, `$CCEC = 10` c1391 =
// V+69, so the glyphs land at V+71 = victoryHold 72. The account below (from the forest fight) counts from the arm:
// From there the ROM spends, byte-for-byte:
//     64  the ONLY blocking hold in the victory stager -- `C1/3681 LDA #40 / STA $5DC9`, loop
//         C1/3686-C1/3693, one real frame per DEC (`JSR C1/007E` = `INC $9E / JSL CD0009 / LDA $9E / BNE`).
//         A byte scan of the whole C1/356D..C1/3699 span for a direct-page joypad read ($F0/$F2/$F4/$F6/
//         $F8/$FA) returns ZERO hits: A cannot skip it. Oracle: $5DC9 counts 40->00 one per frame with A held.
//      5  reward apply + message build (C1/83BB -> JSL FD/AD17 -> JSL CD/0021 -> CD/01B3 -> CD/025E+CD/027F)
//      2  the $CA24 NMI tilemap upload (set CD:0294, consumed D1:FE70) before the glyphs are on screen
//   = 71, so the box first draws at victoryHold == 72. Measured on one clock (forest_battle_3hetake, no
//   input): dispatch f777 -> $5DC9==0 f841 -> $CCEC=0x10 f846 -> pixels f848. GATO (no dissolve) gives the
//   SAME 3+64+7 tail from its own dispatch, which is why the anchor is the DISPATCH and not the delist.
//
// ⚠ The old 212 came from battle_loop.md §K, which folded FOUR phases into one bucket: it measured
// kill -> FIELD-RESUMED (231f) and charged the message lines' own 16+16 ramps, ~90f of module teardown and
// a different A cadence to a "fixed celebration" that does not exist. See the dossier's TRAP 2/TRAP 3.
#define CT_BATTLE_VICTORY_CELEBRATE 72
// ...of which the celebration's own `$5DC9` hold (C1/3681-3693): a fight with $2989 bit 0 has none (C1/35A9).
#define CT_BATTLE_VICTORY_LADDER 64

// The per-line cadence of the results message machine CD/01B3. One line =
//   OPEN (CD/025E+CD/027F, $CCEC=0x10) | 16f `JSR CD/020A` | DWELL | CLOSE ($CCEC=0, CD/01FD) | 16f CD/020A
// CD/020A is `LDA #10 / ... JSR CD/3E75 ... DEC A / BNE` and CD/3E75 is ONE REAL FRAME (`JSR CD/3E7D /
// JSL C10012`, spinning on the NMI's $CA20), so both holds are genuinely 16 frames, not 16 iterations.
#define CT_BATTLE_RESULT_RAMP   16
// The gap between one line CLOSING and the next OPENING is 16 (the close ramp above) + the next line's
// build. MEASURED, not derived: 4 / 3 / 3 frames at the three forest boundaries (f1022->f1042,
// f1218->f1237, f1413->f1432 = dark 20 / 19 / 19). CD/025E+CD/027F carry two `JSR CD/3E7D` (2 derived
// frames) plus a variable-cost bank-C2 VWF pass whose length was not decomposed -- see the dossier's
// Open question 4. Pinned at the modal 3 (period 195).
#define CT_BATTLE_RESULT_BUILD  3
// CD/019D: the 8-entry message-DWELL table, indexed `($2991 >> 3) & 7` (CD/01A5: `LDA $2991 / LSR x3 /
// AND #07 / TAX / LDA $CD019D,X`) -- $2991 bits 3-5 are the config menu's Message Speed (C2/9332 unpacks
// the same field into $0D90). Read from the ROM at run time, NEVER hard-coded: the last entry is 0xFF,
// not 0x100, so `32 << i` would be an invented approximation.
#define ROM_MSG_DWELL_TBL  0x0D019Du
// D1:EE99[id]: the results message's sound, posted by CD/01B3 before the line is built (CD/01C3 `LDA $D1EE99,X / CMP #FF /
// BEQ` / CD/01CB `LDX #0080 / JSR CD/3B71`): FF FF FF FF 89 88 88 88 FF -- the reward lines are silent, a level up
// posts $89 and a learned tech $88.
#define ROM_MSG_SND_TBL    0x11EE99u
// FD/AD17 issues one line per message with no cap of its own (XP/TP/Gold, up to six items, then per seat a line per
// level gained and one per learned tech); the port's array bound, not a ROM number.
#define CT_BATTLE_RESULT_MAX 48

// One active rising damage-number popup (disasm/notes/battle_actionfx.md P1). value = the damage dealt;
// centerX/restY = the target's MEASURED per-combatant origin (P1.3/P1.6); age = render frames since birth
// (drives the P1.4 rise table + the P1.5 ~56f lifetime). battle.c owns the pool; field.c renders it.
// kind: 0 = decimal damage/heal number (ctBattleFxDigitTile); 1 = the "MISS" glyph (disasm/notes/
// battle_miss_evade.md §2 -- OBJ tiles 0x19A/0x19B pal0, spawned instead of digits on a physical MISS).
// `kind` is the PORT's 2-way render selector (0 = digits, 1 = the "MISS" glyph); `romKind` is the ROM's
// own $AD9E popup kind (1 = HP restore, 2 = MP restore, 3 = ordinary damage, 5 = miss), which C1/5B99-
// 5BAD turns into the OAM attribute = the digit PALETTE (kinds 1/2 -> 0x39 = OBJ pal 4 = green).
// `rise` is the digits' OAM Y offset from restY as the popup animator CF/E781 has stepped it so far: every frame it adds
// D1/5E10[$A3D2] to the row, or D1/5E49[$A3D2] while the combatant's `$A0DC` is set (a kind other than 1/3/5 was drawn on
// it, C1/5BA1) -- the first curve nets -15 px, the second -33 (the MP restore's number rests 18 px higher).
typedef struct { int active, value, centerX, restY, age, kind, romKind, rise; } CtFxPopup;
// The strip's VRAM image (CtBattleHost.stripVram): BG3 rows 0..8 of the tilemap at word $7800, then BG1 rows 16..21.
#define CT_BSTRIP_TEXT_SIZE 0x240   /* VRAM $7800-$791F: BG3 rows 0..8, 32 words each */
#define CT_BSTRIP_FRAME_OFS 0x240   /* VRAM $7A00-$7ABF: BG1 rows 16..21 */
#define CT_BSTRIP_VRAM_SIZE 0x3C0
#define CT_BATTLE_FX_POOL 11  // ONE NUMBER PER COMBATANT: op 0x50 (C1/5ADB) keeps its state per combatant ($92DD,X, eleven
                              // bytes cleared on its first visit) and C1/5B49-5CD4 walks the eleven $AD9C/$AD9E records --
                              // the 07D's Frog Squash shows five at once (cartridge OAM f5130); a pool of 4 lost the first

// THE COMBATANTS' OBJ PALETTE BYTES LIVE IN THE WRAM, as the ROM keeps them: $993C,X the live attribute (0x08 = flashing
// white; ops C9 / 69 / 6A and CF/FE57 rewrite it) and $9888,X the default the boot stores beside it (CC/E6F8, CC/EAAB
// `STA $9888,Y / STA $993C,Y`). They were host-side mirrors (objPal / objPalDef) the effect ops wrote instead of the
// WRAM; the routine-level shadow caught op C9 (CD/1B78) leaving $993C as it found it on the Magus fight.
#define BT_OBJPAL(h, k)    ((h)->mem[0x993C + (k)])
#define BT_OBJPALDEF(h, k) ((h)->mem[0x9888 + (k)])

// THE ENEMIES' MODE / FACING / IDLE-POSE STATE LIVES IN THE WRAM, at the ROM's own addresses (S4 of docs/SHADOW_PROPOSAL.md:
// one place per variable, so the routine-level shadow sees what the live battle runs). Each was a host-side field.
#define BT_MVMODE(h, i)       ((h)->mem[0x974A + (i)])       // $974A,X: the move mode (0 none / 1 approach / 6 return)
#define BT_MVREQ(h, i)        ((h)->mem[0x5E0D + (i)])       // $5E0D,X: the REQUESTED mode; only C1/3364 commits it
#define BT_ENFACING(h, i)     ((h)->mem[0x96DE + (i)])       // $96DE,X: the enemy's facing (zeroed at boot, CF:FB9A)
#define BT_ENFACINGPUB(h, i)  ((h)->mem[0x96E9 + (i)])       // $96E9,X: the facing the sprite was last built with (C1/336A)
#define BT_ENARRIVE(h, i)     ((h)->mem[0x9829 + (i)])       // $9829+slot: the mode-1 arrival latch
#define BT_IDLETIMER(h, i)    ((h)->mem[0x9694 + (i)])       // $9694,X: the dwell timer (C1/3409 DEC)
#define BT_IDLECUR(h, i)      ((h)->mem[0x969C + (i)])       // $969C,X: the window cursor 0..3
#define BT_IDLEPUB(h, i)      ((h)->mem[0x990A + (i)])       // $9907+3+slot: the published pose (C1/1C66; FF = empty)
#define BT_IDLEDEFER(h, i)    ((h)->mem[0x9925 + (i)])       // $9925+slot: the visit-me-first flags (C1/344C)
#define BT_IDLEPOSE(h, i, k)  ((h)->mem[0x97D9 + 8 * (i) + (k)])   // $97D9+8*slot: the window poses (C1/33DF)
#define BT_IDLEDWELL(h, i, k) ((h)->mem[0x9799 + 8 * (i) + (k)])   // $9799+8*slot: the window dwells (C1/33F4)
#define BT_SCHEDA4(h)         ((h)->mem[0x00A4])             // $A4: the 0..3 group counter (CF/E477 / CF/E47F)

// THE PCS' IDLE-WALK STATE LIVES IN THE WRAM too (S4): the dwell timer, the window entry, the published pose (FF = none
// yet, as the ROM's own $9907 reads), the last window, the defer flags, the facing and the re-aim latch.
#define BT_PCTIMER(h, i)    ((h)->mem[0x9689 + (i)])   // $9689,X: the PC dwell timer (C1/2E20 DEC)
#define BT_PCIDX(h, i)      ((h)->mem[0x968C + (i)])   // $968C,X: the window entry (C1/2EA9 INC before the read)
#define BT_PCPUB(h, i)      ((h)->mem[0x9907 + (i)])   // $9907,X: the pose the walk last published (C1/1C66; FF none)
#define BT_PCWINLAST(h, i)  ((h)->mem[0xA103 + (i)])   // $A103,X: the last window published (C1/2EFC)
#define BT_PCDEFER(h, i)    ((h)->mem[0x9922 + (i)])   // $9922,X: the visit-me-first flags (C1/2E18)
#define BT_PCFACING(h, i)   ((h)->mem[0x96DB + (i)])   // $96DB,X: the PC's facing (read & 3)
#define BT_PCAIMLATCH(h)    ((h)->mem[0xA098])         // $A098: the round-robin re-aim latch (C1/2F22)

// THE COMBATANTS' LIVE FEET LIVE IN THE WRAM (S4): $1D0C,c / $1D23,c, the bytes every mover, the OAM compose and the
// walks read. They were the host's mvLiveX / mvLiveY, kept beside the WRAM bytes and synced at a handful of sites.
#define BT_LIVEX(h, c)      ((h)->mem[0x1D0C + (c)])   // $1D0C,c: the live foot X
#define BT_LIVEY(h, c)      ((h)->mem[0x1D23 + (c)])   // $1D23,c: the live foot Y

// THE C1/283D MIRROR OF THE FEET LIVES IN THE WRAM (S4): $A039,c / $A050,c, what the body box rows, C1/28B0's test and the
// position machine C1/2986 read. It was the host's mvMirX / mvMirY.
#define BT_MIRX(h, c)       ((h)->mem[0xA039 + (c)])   // $A039,c: the mirror's foot X
#define BT_MIRY(h, c)       ((h)->mem[0xA050 + (c)])   // $A050,c: the mirror's foot Y

// THE ENEMIES' MOVER STATE LIVES IN THE WRAM (S4), at the ROM's own addresses, X = the enemy slot (the extents: the
// combatant). Each was a host field (mvTarget, mvAngle, ...), copied in and out of the WRAM at a few sites.
#define BT_MVTARGET(h, i)                ((h)->mem[0x9752 + (i)])   // $9752,X: the move-target combatant (FF = none)
#define BT_MVANGLE(h, i)                 ((h)->mem[0x9786 + (i)])   // $9786,X: the segment bearing (0x100 = a full turn)
#define BT_MVBASEX(h, i)                 ((h)->mem[0x98AF + (i)])   // $98AF,X: the segment's base X
#define BT_MVBASEY(h, i)                 ((h)->mem[0x98B7 + (i)])   // $98B7,X: the segment's base Y
#define BT_MVPHASE(h, i)                 ((h)->mem[0x9737 + (i)])   // $9737,X: the 8-phase mover counter
#define BT_MVACTIVE(h, i)                ((h)->mem[0x98A7 + (i)])   // $98A7,X: a segment mid-advance (CF/F98F runs it)
#define BT_MVWALKING(h, i)               ((h)->mem[0x9791 + (i)])   // $9791,X: the stored-bearing latch
#define BT_MVREFX(h, i)                  ((h)->mem[0xA5E4 + (i)])   // $A5E4,X: the return's reference X
#define BT_MVREFY(h, i)                  ((h)->mem[0xA5EC + (i)])   // $A5EC,X: the return's reference Y
#define BT_MVSAVEMODE(h, i)              ((h)->mem[0x5E1D + (i)])   // $5E1D,X: the requested mode saved at the settle (C1/3B48)
#define BT_MVRECX(h, i)                  ((h)->mem[0x9831 + (i)])   // $9831,X: the victim's foot X at the settle
#define BT_MVRECY(h, i)                  ((h)->mem[0x9839 + (i)])   // $9839,X: the victim's foot Y at the settle
#define BT_MVSEGCAD(h, i)                ((h)->mem[0x9819 + (i)])   // $9819,X: the mover's per-slot frame counter (CF/F984 DEC)
#define BT_MVMODERELOAD(h, i)            ((h)->mem[0x989F + (i)])   // $989F,X: the mode machine's cadence reload (CC/EBF4)
#define BT_MVSEGPERIOD(h, i)             ((h)->mem[0x9742 + (i)])   // $9742,X: the mover counter's reload (E4/F609 >> 4)
#define BT_MVPHASESTEP(h, i)             ((h)->mem[0x98C2 + (i)])   // $98C2,X: the phase increment per wrap (E4/F609 & 7)
#define BT_MVMODECAD(h, i)               ((h)->mem[0x9897 + (i)])   // $9897,X: the mode machine's cadence counter (C1/3727 DEC)
#define BT_MVEXTENTY(h, c)               ((h)->mem[0x9729 + (c)])   // $9729,c: the box's Y half-extent, per combatant (E4/F608 for an enemy)
#define BT_MVEXTENT(h, c)                ((h)->mem[0x971E + (c)])   // $971E,c: the box's X half-extent, per combatant (E4/F607 for an enemy)

typedef struct CtBattleHost {
  uint8_t* mem;              // the engine's $7E image (64KB): staged interface in, battle blocks out
  uint8_t* glb;              // the $7F0000 page (0x200 bytes): outcome $7F01EC = glb[0x1EC]
  int (*frame)(void* ctx);   // optional per-frame host callback used ONLY by the synchronous CtBattleRun
                             // (headless); return nonzero = the C1/8357 debug instant-end was requested
                             // (the ROM reads controller 2; we bind a debug key/env). The per-frame
                             // interactive path (CtBattleTick) does NOT use this -- the host pumps events.
  void* ctx;
  int debugEndAfter;         // >= 0: auto-fire the debug end after N module frames (CT_BATTLE_DEBUGEND);
                             // < 0: only an endRequest can end the battle
  int maxFrames;             // hard safety cap on the module loop (0 = default 36000)
  int frames;                // module RENDER frames elapsed (CtBattleBegin resets to 0; CtBattleTick +1/frame).
                             // Real-frame clock: drives the intro delay (CT_BATTLE_INTRO) + the HUD settle
                             // (field.c `frames>=82`), which battle_hud Q6/ADDENDUM 2 pin to REAL frames.
  // --- THE MAIN LOOP'S ITERATION (battle.c battleIterFrameA / the gate in battleTickBody): two frames, the
  //     idle tick C1/106E's two waits. Frame A = C1/109D's wait returned (the walk dispatch, C1/10BB STZ $E5),
  //     frame B = C1/10BD's returned (C1/10C4 INC $E5, the tail: timers/cursors/handlers, the next head). ---
  int iterPh;                // running idle frames since the last iteration head (0 = the head's frame:
                             //   combat start, an action's end); odd = frame A, even > 0 = frame B; -1 = none
  int actPhysScript;         // this PC physical runs its CE script (CD/5EC6) on the interpreter, op 01 closing it
  int iterHead;              // frame B ran the tail: the next head (C1/8120 INC $0027, C1/1076-1099) closes the
                             //   frame -- unless the tail opened an action, whose end is then the head's frame
  int pendEdges;             // CT_BIN_* A/Up/Down/B edges LATCHED across tick-skipped frames (joypad latch)
  int navDir, navRepeat;     // menu-nav auto-repeat state (now TICK-based; battle_hud ADDENDUM 2 §B)
  int mashaRelease;          // CT_BATTLE_MASHA alternate press/release toggle (TICK-based): the mandatory
                             // target-select confirm (battle_actionfx.md P5) needs TWO distinct A-edges
                             // per attack (Att->target-select, then confirm), so MASHA emits A on "press"
                             // ticks and nothing on "release" ticks -- deterministic, walks menu->target->
                             // confirm. Reset each battle in CtBattleBegin.
  // --- phase-2 loop instrumentation (set by CtBattleTick; read by the --battletest `battleloop:` line) ---
  int pcAttacks;             // number of PC physical attacks that LANDED this battle
  int enemyAttacks;          // number of ENEMY physical attacks that LANDED (dossier S4; drives the D1 idle proof)
  int pcMisses;              // battle_miss_evade.md: PC physical attacks that MISSED (0 in every normal fight,
                             //   hit>=evade -> always hit; nonzero only when evade is high, e.g. the forced-miss check)
  int missReported;          //   one-shot: the "battlemiss:" trace line has been emitted for the first miss
  int missPopupSpawned;      //   1 once a "MISS" popup (kind 1) has been spawned (the popup ages out ~6f before
                             //   span-end, so the trace reads this latch, not the live pool)
  int endReason;             // 0 = still running / debug-end / cap ; 1 = victory (HP-kill) ; 2 = defeat ; 4 = the demo pass (C1/83AE)
  // --- ACTION FEEDBACK (disasm/notes/battle_actionfx.md): the DEFERRED action WINDOW + the render-side
  //     damage-number popup / hurt-flash / attacker skeleton the field composites over the baked stance.
  //     battle.c owns the state machine (dispatch gating + the strike/window timing, all RENDER-frame
  //     paced -- P1.4/P2/P3 measurements are render frames); field.c READS these to draw. Reset each
  //     battle in CtBattleBegin. F8/CT_BATTLE_DEBUGEND still end instantly from ANY window state. ---
  int actActive;             // 1 while an action window animates ($95DB=1; NO new dispatch until it closes)
  int actAttacker;           // attacking combatant idx 0..10 (-1 none)
  int actTarget;             // target combatant idx (-1 none) -- the PRIMARY victim (== actTgtList[0])
  int actValue;              // DEFERRED damage: computed at DISPATCH (RNG draw), applied at the strike frame
  // --- MULTI-TARGET (disasm/notes/battle_multitarget.md): an AoE tech (group 0x12 Cyclone / 0x1A) hits the
  //     cursor PRIMARY + every alive enemy within a radius of it (C1/2701: dist = (Δ>>4)² summed vs the CC/FB6F
  //     squares table, <= $9607 radius² = 9). The action carries a per-victim LIST + per-victim damage (each an
  //     INDEPENDENT variance rand -- the C1/D221 `DEC $AD8D` loop). actTgtN = 1 for EVERY single-target action
  //     (physical / item / enemy / single-enemy tech like Ice) so the apply loop reduces to the old one call =
  //     byte-identical. Built at dispatch (battleBuildHitSet); applied per-victim at span-end (battleAdvanceAction).
  int actTgtN;               // number of victims this action hits (1 = single-target)
  int actTgtList[8];         // the victim combatant indices (actTgtList[0] = actTarget = the primary)
  int actTgtVal[8];          // per-victim computed damage (parallel to actTgtList)
  int actTgtMp[8];           // per-victim: the MP a group item restores (C1/DA37's second half), the span end adds it
  int actTgtApply[8];        // per-victim: 1 if a slot was staged for it (C1/EBF8 without the 0x20 no-apply flag) --
                             //   a tech's missed or HP-0 victim gets none, and the span end writes it nothing
  int actStart;              // h->frames at window-open (rel = h->frames - actStart, in render frames)
  int actApplied;            // VISUAL strike fired? (popup + flash + knockback ONCE at the anim strike offset).
                             //   The HP-apply/death/dissolve is DEFERRED to the span's LAST frame (battle_loop.md
                             //   §I: C1:ED1A lands on the last frame), NOT here -- the popup rises/clears first.
  int actSpan;               // §I per-attack $A013 frozen-span length (render frames), keyed on the $9877 anim
                             //   selector at DISPATCH (PC 125 / GATO 157|224 via $AE93 / other-enemy avg 202).
                             //   Stored (not recomputed): the GATO $AE93 toggle mutates state at dispatch.
  int actIsTech;             // battle_span_emergence.md §3: 1 if this action is a TECH (op 0x02, e.g. GATO's
                             //   song) vs a physical attack (op 0x01). Set at dispatch (battleActionSpan). Gates
                             //   the ShowDamage-driven hurt-flash: a physical attack lands its 50 at the strike
                             //   pose (GATO's claw), a tech's 50 is late/abstract -> no premature rel-27 flash.
  int actCrit;               // battle_crit.md: 1 if the current PC-physical action rolled a CRITICAL (x2 dmg).
                             //   Set in battleComputeDamage from the SEPARATE crit RNG stream ($B3E6-equiv,
                             //   independent of the $26 damage stream), reset per dispatch. Drives the crit
                             //   anim selector (Crono 0x01 span 177) + the doubled popup + the screen flash.
  int actCritFlash;          // 1 if this crit uses a DEDICATED crit anim that FLASHES: CD/400E[cid] != the
                             //   char's normal selector (Crono 0x01!=0x00 yes / Marle 0x0F==0x0F no, §C).
  int critFlashLeft;         // whole-screen crit FLASH render frames remaining (counts down from critFlashDur
                             //   at each crit slash; field.c brightens the BG toward white). Crono's 0x01 crit is
                             //   a DOUBLE-slash -> TWO flashes (battle_timing_audit.md §2): Flash 1 @rel+50 (fade
                             //   CT_CRIT_FLASH_DUR1~7f, the primary strike) then Flash 2 @rel+91 (fade CT_CRIT_FLASH_DUR~15f).
  int critFlashDur;          // the CURRENT flash's total duration (= the render denominator, so both slashes peak
                             //   at CT_CRIT_FLASH_PEAK regardless of their different fade lengths).
  int critFlash2Done;        // one-shot: Flash 2 (the second slash, rel+91) has fired this action.
  int actMiss;               // disasm/notes/battle_miss_evade.md: 1 if this PHYSICAL attack MISSED (the
                             //   C1/DC64 to-hit roll: miss iff hit*100/evade < rand(0..99); hit = attacker
                             //   block+0x3A, evade = target block+0x3B). Set in battleComputeDamage from the
                             //   $26 hit roll (drawn ONLY when a miss is POSSIBLE, hit<evade -- guaranteed-hit
                             //   fights never draw, so every existing pin stays byte-identical, §5). Reset per
                             //   dispatch. Suppresses HP-apply/hurt-flash/knockback/counter (the miss does NOT
                             //   arm the when-hit counter) + spawns the "MISS" popup (kind 1) instead of digits.
  // --- battle_ai_interp.md §11 (SESSION-18): the PC attack route is the bank-CE engage choreography, LIVE
  //     on mvLive (single source). The engage block's op `18 03` = WALK-TO-role-3 (the victim): the C1/5480
  //     GRID PATHFINDER -- $7CFD walk grid = the arena grid + 0x40 per other-combatant cell, BFS flood
  //     (C1/4A71: self 0x20, goal 0xFF, waves 1..0x1E) + path backtrace (cells |= 0xE0), then per-segment
  //     (8 phases, CC/F494/F594[track*8+phase] deltas = 2px/f) the walker follows the adjacent 0xE0 path
  //     cell (probe order up/down/left/right, no-backtrack $A548 = CC/F414[track], facing $96DB =
  //     CC/F434[track]), STOPPING when a 2-ahead probe (CC/F454/55/56[dir*8]) reads the 0xFF goal (= rests
  //     one clear cell from the victim). The strike block then runs `A8 01` (a 1px nudge -> 0px net) and
  //     `98 02 19` = the trig-mover RETURN to the $99AF/$99B2 staged HOME (speed 4 -> 4px/f, step count =
  //     dominant-dist/4) + `1B 19` snap-home-exact. All of it mutates mvLive[0]; the render reads the live
  //     foot directly (no offset model).
  int pcAtkMvActive;         // 1 while the walker/return owns Crono's swing position (seat0 physical only)
  int pcAtkMvArrived;        // walk stopped (goal-adjacent) -- the strike plays here
  int pcAtkMvReturning;      // the op-98 home slide armed (4px/f + snap)
  int pcWalkActive;          // $A15B[cmb]: the grid walker is live
  int pcWalkCmb0;            // the walking combatant (the arm's role->cmb resolve; 0 = Crono today)
  int pcWalkCounter;         // $A500: 7..0 = segment phase counter (7-counter = the delta-table phase); -1 = segment boundary
  int pcWalkTrack;           // $A517: dir*8 + variant (indexes CC/F494/F594/F414/F434)
  int pcWalkForbid;          // $A548: the forbidden probe dir next segment (no-backtrack); -1 = none
  int pcWalkCell;            // $A53D: the walker's current 16x16 grid cell (row*16+col)
  uint8_t pcWalkGrid[256];   // the flooded $7CFD image (obstacles + waves + E0 path + FF goal)
  int pcAtkStartX, pcAtkStartY;   // live foot at arm (the crit leap's trig base)
  int pcAtkArrX, pcAtkArrY;  // arrival latch (walker stop / crit box-overlap) -- the return's from-point
  int pcAtkHomeX, pcAtkHomeY;// $99AF/$99B2[seat]: the STAGED home the op-98/1B-19 return targets (snap-exact)
  int pcAtkApFac;            // the walker's CC/F434 facing stamp (render approach window); -1 = unset
  int pcAtkRetCount;         // op-98 remaining mover steps (dominant-dist/4 at arm; 0 = done -> snap)
  // ---- THE ARRIVAL ANCHOR (disasm/notes/battle_pc_attack_sequencing.md). op 0x18 PARKS the CE script
  //      thread for the whole walk (C1/55FC -> C1/560C `LDA #00 / JMP C1/75BB` = advance the cursor by
  //      ZERO; only the stop probe takes C1/55FE -> C1/5603 and advances by $A4F0[role]). So EVERYTHING
  //      after the walk -- the swing poses, the victim flash, the ShowDamage popup, the home slide and the
  //      span end -- is anchored HERE, not at dispatch.
  int pcAtkAnchored;         // 1 = this action armed a real PC route, so the flash/popup/span/pose ladder
                             //   are ARRIVAL-anchored (0 = no route: Marle/enemy/tech -> the legacy
                             //   dispatch-relative constants stand)
  int pcAtkArrRel;           // rel (frames since actStart) of the ARRIVAL frame; -1 while walking/pre-roll
  int pcAtkCritN;            // the crit's `98 02 28` slide: step count (dominant/4, truncated)
  int pcAtkCritStepX, pcAtkCritStepY;   //   8.8 per-frame deltas (battleSlideArmSteps)
  int pcAtkCritAccX, pcAtkCritAccY;     //   the CF/F113 accumulators (seeded 0x80)
  int pcAtkRetEndRel;        // rel of the frame the op-98 home slide completed; -1 until then
  int pcAtkRunCur;           // $A13A: the RUN window's pose cursor (loop mode, op `02 06`); -1 = pre-seed
  int pcAtkRunDwell;         // $A12F: frames left on the current RUN entry (reload = the raw ptrB byte)
  int pcAtkRetHold;          // frames still to wait between the op-98 ARM and its first position change
                             //   (crit: the measured 2f gap A+92 -> A+94; melee: 0)
  int pcAtkSnapHold;         // frames the `1B 19` snap is HELD after the last travel frame R (crit: 8)
  int pcAtkRetStepX, pcAtkRetStepY;  // the return slide's per-frame 8.8 step = delta_axis / count
  int pcAtkRetAccX, pcAtkRetAccY;  // the CF/F113 16-bit trig accumulators (product speed*sin per frame,
                             //   hi-byte applied to live; seeded 0x80 = the arm's rounding half -- the
                             //   oracle's byte-exact 4/4/4/4 cardinal sequence, f494-497)
  int pcAtkMvCritLeap;       // CRIT variant (battle_facing_engine.md ADDENDUM-CRIT, bat3crit oracle): the
                             //   approach is the TRIG LEAP -- no movement until rel 23 (the windup window),
                             //   then live = start + trig(bearing)*4*phase per FRAME (X -4/f, Y -1/f observed
                             //   for bearing 0x82) to the arrival box, strike ladder plays adjacent, return
                             //   at rel 113 (AFTER the second slash; the normal 60 sent him home mid-crit).
  int pcAtkMvCritAngle;      // the leap bearing (atan2 home->victim, latched at arm)
  int pcAtkMvCritPhase;      // trig phase within the current leap segment (1..8, re-based like CF/F999)
  char lastEvent[16];        // the most recent battleTrace event NAME, recorded whether or not tracing is
  int  lastEventFrame;       //   on, and the frame it fired -- debug/diagnostic only.
  int  eventSeq;
  // The CT_BATTLESHOT_ON anchor: the WATCHED event name, how many times it has fired, and the frame
  // of the most recent one. A counter here rather than a read of `lastEvent` from outside, because a
  // single most-recent slot cannot represent a frame that fires more than one event -- and that is
  // not an edge case: a Cyclone double-kill emits `death`, `death`, `act-end` all on the same frame,
  // so the deaths were clobbered by act-end and an anchor on them "saw 0 occurrences" while the trace
  // printed both. battleTrace counts matches as they happen, so N indexes OCCURRENCES, not frames.
  char watchEv[16];
  int  watchSeen;
  int  watchFrame;
                             //   number, which is the class of check that rots when timings move.
  int sfxAtkPosted;          // per-action anim-script SFX progress: 0 = none yet, 1 = the 0x7C fired and a
                             //   SECOND op is still due, 2 = this action's sound is done. Reset at dispatch
                             //   alongside popupSpawned; only PC PHYSICALS carry a measured sound opcode.
                             //   (disasm/notes/battle_sfx_ops_inputs.md §5 + the crit script CE/03CC.)
  int sfxAtkRel;             // rel of the frame the 0x7C fired -- the crit's 0x7D is anchored on THIS, not
                             //   on the dispatch, because the leap length moves the first post but not the gap.
  int popupSpawned;          // battle_hp_timing.md §c: per-action one-shot -- the damage-number popup is now
                             //   born LATE (rel >= actSpan - lead, op-50's real script position) so it dies
                             //   ~5-7f before the span-end HP write, NOT at the early visual strike. Reset at
                             //   dispatch (battleOpenAction); the strike block keeps only flash+knockback.
  // --- battle_confirm_queue.md §C.1: COMMITTED-but-not-yet-animated PC actions. The target-select CONFIRM
  //     registers IMMEDIATELY the instant the A edge is seen (any frame, frozen or not -- ROM C1/1626:
  //     DEC $9609 + dequeue + advance head), but the action ANIMATES only when the execution engine is free
  //     ($A013 clears). battleCommitPcAction pushes here; battleDispatchScheduled pops + runs the dispatch
  //     body (RNG draw at ANIMATE time, B.2). Party-size FIFO (Crono then Marle can both confirm during one
  //     enemy freeze -- back-to-back commits). Reset in CtBattleBegin. Fixes the deferred-confirm boundary
  //     race (the "sometimes A does nothing" drop on the 3 dispatch/span-end frames).
  int schedAtk[3];           // committed attacker combatant idx (FIFO, oldest at [0])
  int schedTgtSlot[3];       // the enemy SLOT (0..7) captured at confirm (the cursor's enemy - 3), per entry
  int schedN;                // FIFO depth (0..3)
  int schedDispatchDelay;    // battle_pacing_v2.md FIX D: the C1/1B19->C1/4090 executor reach -- a scheduled
                             //   PC action animates ~8f after the freeze clears in the oracle (f636->f644);
                             //   our pop path takes ~3f, so the pop holds the remaining frames. Re-armed
                             //   whenever the FIFO is blocked (actActive/empty); drains once eligible.
  // --- battle_atb_reload.md §D (option B): the per-seat TURN LATCH = the literal ROM $93EE port (B.2). It
  //     is SET when step E enqueues a seat (the readiness edge, C1:1B2E) and stays set through the whole
  //     ready->committed->acting span, CLEARED only at that seat's OWN span-end reload (C1:85D8, the twin of
  //     the C1:BCEB $AFAB<-$B158 reload). Step E is now EDGE-guarded on it (`timer==0 && !queued &&
  //     !pcInTurn`): a COMMITTED seat is dequeued at confirm yet its timer stays 0 until span-end, so without
  //     this latch step E RE-OFFERS it on the tick between commit and dispatch -> the "player can attack
  //     twice" bug (§B/§C). With the latch a committed seat is never re-enqueued until its ATB reloads
  //     (span-end) and counts back down to 0 -- one turn per readiness edge, exactly the ROM (§B.2 NET).
  //     Reset in CtBattleBegin; per-seat so a multi-PC one-freeze double-commit is covered (§D, per-seat).
  int pcInTurn[3];           // 1 = seat has an in-flight turn (enqueued..committed..acting), cleared at reload
  int demoPass;              // $2989 bit 5 (the attract's demo battle): 1 = the C1/8461 pass that took C1/B725's
                             //   commands is in flight -- it completes at C1/83AE, which ENDS the fight (no victory)
  int demoSeat;              // the seat C1/8592 popped (whose $B188/$93EE C1/85D1 clears when that pass returns)
  int demoOpenAt;            // C1/B967 runs the demo's command SYNCHRONOUSLY: its staging spills into the next frame, where
  int demoDispatchAt;        //   C1/4058 wipes the ladder (CC/F06B), and three waits later C1/40A6 sets $A013 -- the two
                             //   frames (-1 = none) at which the port does each; see battleCommitDemoTech
  // --- ANIM-SCRIPT INTERPRETER (disasm/notes/battle_interp_verify.md T2 + PART 2 P2.B): the ACTION-
  //     TIMING layer WALKS the REAL ROM anim script (CD/5FF0[$9877] attack / CD/61F0[$9877] tech, via
  //     CtBattleRom) at dispatch -- opcode lengths READ from the ROM handlers at parse time (the C1/7A6B
  //     jump table + the C1/75BB `E6+=len` advance tail) -- to drive the strike/ShowDamage EVENTS + the
  //     coverage report. SPLIT (spec POSE/SPRITE note): the interpreter drives WHEN (timing events), the
  //     baked keyframes drive WHAT (pixels). ⚠ The action-window DURATION does NOT emerge from the script:
  //     it is DOMINATED by the 03/06/02 playAnim PLAYBACK = the sprite-anim/tile-streamer BOUNDARY (a
  //     frame-by-frame sim of C1/4CA2 with playAnim=instant gives spans 6/18/66 vs observed 125/157/311;
  //     the derivable 20-pauses are only 2/15/95 of them -- they ORDER-match, GATO-B's PAUSE(80)+PAUSE(15)
  //     is the real source of 224>157, but the per-anim-id playback durations are underdetermined + anim-
  //     speed dependent). So the TOTAL span stays oracle-pinned per $9877 (the dossier-§I copyFreeze
  //     boundary); these per-action flags (filled by battleAnimParse at dispatch, read at render) make the
  //     strike/ShowDamage SCRIPT-DRIVEN + VALIDATE the pins against the real ROM script. ---
  int animParsed;            // 1 = the dispatched action's anim script WALKED cleanly to its terminators
  int animHasStrike;         // the script carries a 1E SUPER (the visual strike) -> gates the strike event
  int animHasShowDmg;        // the script carries a 50-55 ShowDamage -> gates the damage-number popup
  int animPauseSum;          // DERIVABLE explicit 20-pause frames on the span-determining track (emergent part)
  int animPlayAnims;         // count of 02/03/04/05/06 playAnim ops (the boundary-duration ops)
  int animKnockDist;         // the dispatched tracks' move-forward operand (A9/A8 dd, battle_ai_interp §10d-e):
                             //   the strike KNOCK -- the victim's LIVE position walks dd-1 px along the attack
                             //   bearing, PERSISTENT (oracle `A9 04` -> +3). 0 = no knock op in this script.
  int animScriptPtr;         // the resolved CD script pointer word[CD/5FF0|61F0 + $9877*2] (0 = unresolved)
  int fxAtkPal;              // attacker's OBJ palette while a swing renders (Crono pal5 only; -1 = none)
  int fxAtkPose;             // "a swing renders" flag + the FACING CLASS (0/1 = approach -> the walk facing,
                             //   2/3 = strike -> the victim aim, 4/5 = recover -> the home aim; -1 = idle).
                             //   The POSE ITSELF is now (fxAtkWin, fxAtkEnt) below -- battle_pc_attack_
                             //   sequencing.md §6.4 retires the fixed 6-value ctCronoAttackTraj ladder so the
                             //   RUN window can loop for however long the distance-dependent walk lasts.
  int fxAtkWin, fxAtkEnt;    // the published pose = CtBattlePcWinPose(0, fxAtkWin, fxAtkEnt, facing-class):
                             //   RUN 0x18 / STRIKE 0xC4 / RECOVER 0x38 (battle_facing_engine.md §4). win<0 =
                             //   fall back to the legacy fxAtkPose->window map (crit ladder / fixtures).
  int fxAtkDX, fxAtkDY;      // per-frame pose offset = trajectory drift from the pose's rep frame (A2.1)
  int fxEnemyRearup;         // 1 while an ENEMY action is in its rear-up ANIMATION phase (rel < the anim
                             //   length CT_GATO_WINDOW). field.c draws GATO's held rear-up (kf8) ONLY then;
                             //   for the rest of the (now longer) $A013 frozen span he is idle (A3.1 return
                             //   @rel49). battle.c owns the timing (parallels Crono's table-driven auto-idle).
  int fxTechDarkKRetired;    // (was the Cyclone darken curve; the arena fade is the objslot page now -- unused)
                             //   (it must advance every frame, not only on the frames a shot composes);
                             //   field.c just scales the BG composite by it before any sprite draws.
  uint8_t objPalCmp[11];     // $993C as the OAM compose read it (CF/EA11 -- the IRQ chain, after the NMI head) ...
  uint8_t objPalShown[11];   // ... and as the NMI uploaded it: what the picture shows, two frames after the script
  uint8_t frontCmp[11];      // $A5CD as the draw-list build CF/E83F -> CF/E855 read it (the same compose; ops 27/28 write it)
  // The selection bracket: OAM entries 0-3 as the last menu-sprite rebuild left them (C1/1153 -> C1/1115, consumed
  // from $993A) and as the next NMI uploaded them; `on` = the rebuild wrote the bracket and nothing replaced it.
  struct { uint8_t x, y, name, attr; } brkOam[4], brkOamShown[4];
  uint8_t brkOn, brkOnShown;
  // The target hand: OAM entries 0-3 as the last C1/1979 placed them (x, y, the combatant each points at; bit k of handVis
  // = entry k shown, its $0900 X-high bit clear) and as the next NMI uploaded them (battle.c battleMenuPassTail -- the
  // bracket and the hand share the four entries, so a write of one ends the other). handPage = the `$960B` the multi arm
  // started its sprites 1-3 at (C1/1AA7), FF for the single arm; the rebuild leaves the NEXT page in `$960B` (C1/1AF8).
  struct { uint8_t x, y, cmb; } handOam[4], handOamShown[4];
  uint8_t handVis, handVisShown;
  uint8_t handPage, handPageShown;
  int fxScriptFlashMask;     // bit k = combatant k's UPLOADED attribute is palette 4 (a script's op 69): drawn from CGRAM row 4
                             //   of the single flashTarget below. Cyclone pulses its WHOLE hit set
                             //   (disasm/notes/battle_tech_cyclone_fx.md): 4 frames on / 5 off from rel 74,
                             //   then one sustained 9f flash -- oracle-proven to cover exactly the victims
                             //   the radius selected (the out-of-radius enemy never flashes).
  int flashTarget;           // combatant idx currently hurt-flashing (-1 none)
  int fxFlashPal;            // that target's stance OBJ palette to SWAP to ctBattleFxFlashPal white (-1 none)
  int flashLeft;             // hurt-flash RENDER frames remaining (dossier P3, ~9)
  // ---- $993C / $9888: the ROM's OWN hurt-flash state (disasm/notes/battle_hurt_flash_sync.md).
  // The white flash is NOT a timed effect. It is a per-combatant OAM-palette field that the action's own
  // anim script flips: op 0x69 (C1/5DFA `LDA #08 / STA $993C,X`) sets pal4 (the all-white bake, which the
  // same opcode stages from D1/4D90 = FF7F x12 = our ctBattleFxFlashPal), op 0x6A (C1/5EA8 `LDA $9888,X /
  // STA $993C,X`) restores the default. Both the ON frame AND the OFF frame come out of the bytecode, so
  // the flash rides whatever clock the interpreter is on -- there is no constant anywhere in the ROM.
  // Boot fill: PCs CC/E6F8-E70D (three unrolled stores, 0x0A/0x0C/0x0E, SEAT-indexed); enemies the
  // CC/EA5B loop ($29C7 of block CC/F939[i] = $29BF + i*12, `ASL A`, slot = i+3, absent iff $29C5 bit7).
  // Three combatants deep, `>> 1` is the OBJ palette number the renderer already speaks.
  uint8_t deathPal[11];      // CF/FE20 set this corpse's $993C to 08 (its palette moved to OBJ palette 4)
  int ascOwnsFlash;          // 1 while the anim interpreter drives this action's flash (see battleAdvanceAction)
  int fxKnockTarget;         // combatant idx displaced by an enemy's physical (battle_actionfx.md A3.3; -1 none)
  int hurtPoseLeft;          // battle_anim_polish2.md ITEM 1: frames remaining to hold the struck ENEMY's distinct
  int hurtPoseTgt;           //   HURT pose (GATO E3 0x0C) at his stationary foot (armed ~15f at the incoming PC
                             //   strike; hurtPoseTgt = the struck combatant, -1 = none). Consumed by battleGatoRender.
  int critAnim;              // battle_anim_polish2.md ITEM 4: 1 while Crono's DEDICATED crit anim ($9877=0x01) owns
                             //   his render for the whole 177f span (drives the crit pose ladder + holds the
                             //   "Crono attacking" gates past the normal-swing table's rel-79 idle clamp).
  // --- VICTORY REWARDS (battle_results_window.md): accumulated per-kill from the ROM table CC/5E00 (7-byte
  //     records id*7), applied to the PC blocks / gold / inventory at victory (fieldBattleFinish), then
  //     shown as a top MESSAGE-BOX line sequence advanced by A. ---
  int rewXp, rewGp, rewTp;   // running XP / Gold / TP totals (mirror $B28C / $B2A5 / $B2DB), accumulated at
                             //   each enemy death (battleApplyDamage), applied at victory.
  int rewDrops[6], rewDropN; // granted item ids, the ROM's ONE 6-slot array $B2A7[0..5]: slots 0..2 =
                             //   the ROLLED half ($B2A7, 90%% each, dedup within), 3..5 = the GUARANTEED
                             //   drop==charm half ($B2AA, FD/AC4D: no roll, dedup within) -- the drops that
                             //   PASSED the 90% roll, granted to the inventory at victory (fieldBattleFinish).
  int dropPend[16], dropPendN; // battle_drop_charm.md: CANDIDATE drops recorded one-per-drop-eligible-death
  int charmPend[16], charmPendN; // the drop==charm deaths (FD/AC10 CMP $CC5E05,X / BEQ -> the $B2AA arm):
                             //   granted UNCONDITIONALLY at victory (no roll), deduped within their half
                             //   (ordered, with dups), rolled rand(0..99)<90 at victory (battleResolveDrops).
  uint8_t charmTaken[8];     // battle_drop_charm.md: per-enemy-slot charm-consumed flags (bit0 = charm item
                             //   rec+5 taken, bit1 = drop item rec+4 taken) -- the $AE5A bit7/bit6 clear (C1/E9D5).
  int rewLevels[3];          // battle_victory_rewards.md: levels GAINED per PC seat at victory (drives the
                             //   "{name} level up!" result line; mirror the ROM's $B311 per-seat level count).
  int resultKind[CT_BATTLE_RESULT_MAX];   // results-box line kinds built at victory = the ROM's message ids (0=XP,
                             //   1=TP,2=Gold,3=item,4=level-up,5=tech-learn); 0-value lines skipped (§V2). resultVal =
                             //   the number (xp/tp/gold) or item/char id (kind5 packs (charId<<8)|techId).
  int resultVal[CT_BATTLE_RESULT_MAX];
  int resultN;               // number of active result lines
  int resultLine;            // currently-displayed line (0..resultN-1); -1 = sequence not started yet
  int resultPhase;           // CD/01B3's per-line phase, -1 = the sequence has not opened yet:
                             //   0 = the 16f post-open hold (CD/01D8 JSR CD/020A)   -- box VISIBLE
                             //   1 = the DWELL (CD/01EE-01FB, CD/019D[$2991>>3&7])  -- box VISIBLE
                             //   2 = the 16f post-close hold (CD/0206 JSR CD/020A)  -- box HIDDEN
                             //   3 = the next line's build (CD/025E+CD/027F)        -- box HIDDEN
  int resultPhaseT;          // real frames elapsed inside resultPhase
  int defeatT;               // >0 = the DEFEAT sequence (C1/813F-8164, C1/354D): frames since the check
  int victoryHold;           // >0 = the victory sequence is running (surviving PCs in the baked victory pose)
                             //   -- a REAL-FRAME counter; 1 == the ROM's victory DISPATCH frame (C1/35CA).
                             //   The first reward line opens at CT_BATTLE_VICTORY_CELEBRATE.
  int handoffLeft;           // §K: post-dismiss field-handoff frames remaining (C0:18C1->A730->3AB1, ~11f); the
                             //   battle holds this many render frames after A before ending (fieldBattleFinish)
  int endTeardownDone;       // battle_end_ui_teardown.md: the C1/1BAA x3 + C1/35A3 command-UI teardown fired
  int rewardsApplied;        // C1/83D2-83F2 ran (the victory's gold / XP / TP, before FD/AD17's messages)
  int moduleTailDone;        // the module's own tail ran (C1/83F9 / C1/8423 / C1/8443 -> CF/FBE5's records)
  int fbe5St;                // CF/FBE5's frame waits: 1 = INC $E5 next, 2 = the $A4 test next, 3 = CF/FCCF next
  // ---- THE HUD-STRIP BAND ANIMATOR (disasm/notes/battle_hud_band_anim.md, defect #23). The machine
  // itself lives in the ROM's own WRAM -- $CA1E (mode) / $CA1F (steps left) / $CA24-25 (preset request)
  // -- so `mem` alone tells the renderer the band, and a --renderwram fixture captured mid-open renders
  // it with no extra plumbing. These two are the parts that are NOT RAM:
  int hudLiveOpen;           // the HDMA tables on screen are the ANIMATOR'S, not the settled preset's.
                             //   §5: the last open step raises the preset-2 request on frame N, D1/FE70
                             //   consumes it on N+1, and the preset's tables are not displayed until N+2 --
                             //   so the full band is drawn with the opening ramp for ONE frame past $CA1E=0.
  int hudPresetPend;         // that request, seen by the NEXT tick (the install lag above)
  int hudClosePosted;        // the CD/03D6 close has been armed at the handoff (CD/03DA then spins)
  // ---- THE PC POSE-WINDOW LADDER (disasm/notes/battle_pc_pose_ladder.md). ONE ROM machine drives the
  // battle ENTRANCE, the VICTORY celebration and the DEFEAT pose; they differ only by a window OFFSET
  // written into $5DF1,Y -- 0x30 / 0x28 / 0x20 -- by the three stagers C1/34DB, C1/356D, C1/354D. A
  // shared copier (C1/35DD-1367E) then reads, per PRESENT seat, the pose row `E4:ptrA[cid] + facing*
  // stride + win + k` and the dwell row `E4:ptrB[cid] + win + k` (NO facing term, `LSR` at C1/363D),
  // and a shared stepper (C1/369A) advances one entry every OTHER render frame. These fields are that
  // machine: win/cursor/dwell/facing per seat == $5DF1,Y / $5DC1,X / $5DBD,X, and `pose` is what the
  // ROM publishes into $9907. They replaced two BAKED Crono+Marle keyframe fixtures, which is why
  // Lucca was invisible for the whole victory and a dead Lucca vanished entirely.
  int pcLadderWin[3];        // $5DF1,Y: 0x28 victory / 0x20 defeat / 0x30 entrance; -1 = disarmed
  int pcLadderCur[3];        // $5DC1,X: the step cursor, armed at -1 (the ROM's 0xFF), capped at 8
  int pcLadderDwell[3];      // $5DBD,X: ticks left on this entry; armed 1+seat (the C1/3665 stagger)
  int pcLadderFacing[3];     // $96DB resolved ONCE at arm time, as the copier's `$A9 = facing*stride` is
  int pcLadderPose[3];       // the published pose index (-1 = nothing published; a fallen PC HOLDS one)
  int pcLadderTick;          // $00E5: the every-OTHER-frame toggle (C1/369A)
  int sysMsgOpenT;           // CD/04DF's box tick: frames the C2/0009 renderer has reported state $10 since CD/0310's
                             //   post (measured 18, gato_t0 f243-f260); the ROM keeps this inside the C2 bank
  int fleeTriggered;         // one-shot for the flee-trigger trace (C1/81F3 reads $99CD every tick; stage 1 only traces)
  int fleeHeldPrev;          // the previous frame's L+R level, for the hold/release trace edges
  uint16_t joyAutoRead;      // $4218 | $4219 << 8 as the auto-joypad read left them at this frame's vblank (the host's
                             //   pad of the frame before) -- CF/E6E0 reads the hardware itself. 0 headless.
  int resetReq;              // CF/E702 `JML C0:FF00` was taken (the attract's demo battle, a button): the host reboots
  int pcLadderLoop;          // $5DC9: the copier's OWN frame loop (C1/3681-3693: 0x40 iterations of frame-wait /
                             //   frame-service / JSR C1/369A) -- the stepper's ONLY caller in the image. 0 = idle.
  int pcLadderDefeat;        // $5DDE: 1 = the defeat arm, the ONLY one that does not skip a dead seat
                             //   (one-shot; queue/$95D5/$9609/$95DB cleared -> the command strip + bracket go)
  // ---- THE RUN-AWAY END (disasm/notes/battle_flee.md 5.2). C1/81EC-821E is a BLOCKING sequence in the ROM: the
  // "Ran away!" box, the C1/EA9D clears, the teardown, the run-off action $79 through the action state, the
  // writebacks and the module exit all run inside it, with only the frame service alive between its waits.
  int fleeStage;             // 0 idle / 1 = inside C1/81EC (box, dispatch, the action) / 2 = the exit tail (CD/03C9) / 3 = CD/040D
  int exitDemo;              // the demo end (C1/83B8 -> C1/8423) runs the same exit tail: endReason stays 4
  int exitCd0c;              // CD/0453's frames at CD/03B4 (21 when $CD0C held the effect load's sheets, else 0)
  int fxTeardownT;           // CD/0453 run by the main loop's CD/0009 (CD/04B5-04C4): its frame + 1, 0 when not running
                             //   (0 = idle, so a host zeroed without CtBattleBegin -- the mover sweeps -- is idle too)
  int fxTeardownLong;        // ...and its path: 1 = the full one (CD/0473-0494), 0 = $99D2 bit 7 / $CD07 skip to CD/0497
  int fxTeardownRuns;        // teardowns run this battle (the battle-end trace prints it)
  int bootApuFrames;         // frames the field's C0/1BAB post (JSL C7:0004) cost: the boot's first wait, before its 18 setup passes
  int bootSyncFree;          // the free sync mode: that post was priced at NO frames (not left unpriced -- a fixture's case)
  int renderFramesAbs;       // a host that never ran a boot (field.c's WRAM render host): its frames ARE the render clock
  int sndStall;              // THE RULE (CtBattleSoundSink.postRule): frames the main thread still sits in a JSL C7/0004 the
                             //   SPC kept past the vblank -- the NMI's service runs, the main loop does not
  int fleeT;                 // frames since the trigger (C1/81F3's frame = 0)
  int fleeExitT;             // frames since the action's $A013 drop (CD/03C9's sound at +5, CD/0516 at +20..22)
  int actEndByScript;        // this action's window ends when its script's op 01 lands (C1/40CE), not at a pinned span
  int actEndRel;             // the rel of that end-action frame (0 = not yet); the close is CT_ACT_END_TAIL later
  int actScriptEnd;          // a PC tech's $A013 close is its script's op 01 too (C1/40CE), damage and turn kept
  int actEndHold;            // frames C1/40E1's `LDA $5D9B / BNE` re-test held the close (the programs still running)
  int actCloseWiped;         // C1/4119's CC/F06B already ran at this close (the script-ended tail): the act-end does not repeat it
  int actOpenWiped;          // C1/405B's CC/F06B already ran at this turn's C1/4058 (two frames before C1/40A6): the open does not repeat it
  // --- THE STATUS TIMERS' ACTION (C1/88C5 poison, C1/8B10 the MP restore): a timer handler stages C1/895B with $AE93 = 7F
  //     and runs it inside the dispatch loop C1/8324, then resumes that loop where it left off (the ATB turn included).
  int actStatusFx;           // 1 = the live action is a timer handler's: no turn, no ATB reload; the close applies the
                             //   staged slots (C1/AC85 -> C1/EC7F) and clears the handler's $B186 bit
  int stActOpenAt;           // the frame C1/40A6's INC $A013 lands for a staged status action (C1/4058 in between; -1 none)
  int stActHandler;          // the handler (1 or 9) whose action it is -- its C1/894C / C1/8B56 bit is cleared at the close
  int stResumeX;             // the C1/8324 dispatch index to run next once that action has closed (-1 = none pending)
  int stSweepAtEnd;          // the ATB handler took a turn (an enemy's, or a confused / berserk PC's): its C1/860A -> C1/883D
                             //   disabled-seat sweep runs when that action returns -- the actor + 1 (0 = none)
  int h12TailPc;             // the ATB handler dispatched a committed PC command (C1/85CE): its tail C1/85D1 -> C1/860D
                             //   runs when that command has run (the close, or battleThinkTick if C1/B967 ran none)
  int stActKind;             // what stActOpenAt opens: 0 = a status timer's action 7F, 1 = FD/AB30's forced physical (C1/BFAA)
  int stForcedAtk, stForcedTgt;   // ...its attacker (the confused / berserk PC) and the target FD/AB30 drew
  int actForced;             // the live action is FD/AB30's: its return is where `INC $AF23` (FD/AB72 / AB9E) lands
  int actDropTech;           // the live action is C1/C66A's 7B-7D for this PC tech, whose area pass found nobody: its close is
                             //   C1/C72B (`$AF23 = 1`) and C1/BB8B -> C1/BC5A (C1/BDB3's reloads of this tech's participants)
  // --- THE DEAD-FLAG SWEEP C1/B223 SUSPENDED (LIFELINE, C1/B30F-B390): a fallen PC with +22 bit 7 is revived INSIDE the
  //     sweep -- action 7A (C1/AC57) and the CD/0021 #9 message run there, and the sweep and its caller go on after them.
  int llSeat;                // the seat being revived (-1 = no sweep suspended); the sweep resumes at llSeat + 1
  int llCont;                // what runs after the sweep: 1 the iteration's C1/8133 on (C1/8130), 2 the ATB handler's tail
                             //   after an action (C1/8626 at the span end, then C1/881C's eval llSlot), 3 the dispatch's end
  int llSlot;                // (llCont 2) the enemy slot whose C1/881C eval waits for the sweep (-1 none)
  int llMsgT;                // CD/01B3's frame count for message 9, from its post (-1 = not running)
  int llMsgClose;            // ...the frame of that count its dwell ended on (CD/01FD; -1 = still up)
  int palK[16];             // FD/E5A4's live CGRAM page ($7E2200) relative to the base ($7E2000), one factor per
  int palKind[16];           // ...and what the factor does: 0 = FD/E64F (mode 5, (acc * c) >> 8), 1 = FD/E8D7 (mode 4, c + (acc * (31 - c)) >> 8)
                             //   16-word palette (0-7 BG, 8-15 OBJ): -1 = the base page, else the record's acc
                             //   (every 5-bit component = (acc * c) >> 8, FD/E64F). field.c applies it at draw time.
  // ---- THE EFFECT LAYER's HARDWARE SIDE (battle.c battleNmiHead / battleFxLoad): what the ROM puts in VRAM and the
  // PPU registers rather than in WRAM, so the host holds it. Everything else of the effect (the programs, the window
  // buffers $C161/$C4E1, the HDMA tables, the shadows $BE89..$BE9A, CGRAM pages $2000/$2200) is the ROM's own RAM.
  uint8_t fxBg3Map[0x800];   // VRAM $F800-$FFFF = BG3 tilemap words $7C00-$7FFF (32x32): CD/0FFD's DMA, CD/0607's blocks
  uint8_t fxBg3Chr[0x1000];  // VRAM $A000-$AFFF = words $5000-$57FF: the four 2bpp sheets CD/0FDA uploads
  uint8_t fxObjChr[0x1800];  // VRAM $1800-$2FFF = words $0C00-$17FF (OBJ names $0C0-$17F): the six effect sheets C1/4833
                             //   builds (CD/1368) -- what the effect's sprites are drawn from
  uint8_t fxOamCloud[0x40];  // OAM entries 4-19 as this frame's NMI uploaded them ($7E:0710-074F) ...
  uint8_t fxOamCloudHi[4];   // ... and their high-table bytes ($7E:0901-0904)
  int fxOamCloudOn;          // that upload carried a program's sprite cloud: CD/2BFC ran after the frame's OAM compose
  // THE EFFECT OBJECTS' OAM (CF/EDB8 / CF/EDF7 / CF/EEEB) as the compose built it (Cmp) and the next NMI uploaded it
  // (Shown): per object whether it draws ($A1D8 and a non-zero dwell $A1B0), its point ($A240+$A260, $A250+$A270,
  // 16-bit), its frame ($A2C0 & 3F) and its priority bits ($AB5F); the before/after matrix $A3DF (obj*11 + combatant);
  // and the two gates (before: an action or $A0FE, and no ShowDamage; after: an action and no ShowDamage).
  struct { uint8_t on, frame, attr; int16_t x, y, ox, oy; } fxObjCmp[8], fxObjShown[8];
  uint8_t fxObjMatCmp[0x58], fxObjMatShown[0x58];
  uint8_t fxObjGateCmp, fxObjGateShown;     // bit 0 = before, bit 1 = after
  int fxCloudRan;            // CD/2BFC ran in this frame (the next NMI head reads and clears it)
  uint8_t fxWinSel[5];       // $2123 $2124 $2125 $212A $212B as CD/08B8 wrote them at this frame's NMI ($C861..$C865)
  uint16_t fxWinTbl;         // HDMA channel 7's table at this frame's NMI (CD/0AEE): `$7C & 1` ? $C9CF : $C9A2
  uint8_t fxCgram[0x200];    // CGRAM as CF/E3C0 uploaded it at this frame's NMI (the $7E:2200 page before FD/E39C)
  // THE STRIP'S VRAM (disasm/notes/battle_hud_upload_pipeline.md): what the PPU holds of the strip's one tilemap at word
  // $7800 -- BG3 rows 0..8 (stripVram[0..CT_BSTRIP_TEXT_SIZE), VRAM $7800-$791F) and BG1 rows 16..21 (stripVram
  // [CT_BSTRIP_FRAME_OFS..], VRAM $7A00-$7ABF) -- written only where the ROM writes that VRAM: the boot's CC/E0AF / CC/E1B0
  // and the fight NMI's CF/E338 (the `$A0` record: $0CC0, $A6E1 or $0E80 into rows 1..6) and CF/E384 (`$99E2`: $0B40).
  // field.c draws the strip from it, never from the live buffers.
  uint8_t stripVram[CT_BSTRIP_VRAM_SIZE];
  // THE AFTERIMAGE (op A4 C1/691B, CF/ED18, CF/E92C): the frame's compose drew combatant c's current cells at its four
  // recorded positions (trailCmp[c] = 1, the 16-bit screen points newest first), published at the NMI like the rest.
  uint8_t trailCmp[11], trailShown[11];
  int16_t trailXCmp[11][4], trailYCmp[11][4], trailXShown[11][4], trailYShown[11][4];
  CtFxPopup popups[CT_BATTLE_FX_POOL];
  // --- DEATH DISSOLVE (disasm/notes/battle_dissolve.md PART 2, src/battle_dissolve.h): per ENEMY SLOT
  //     (0..7, matching $AF02+slot) render-only state that lets a dead enemy's corpse keep drawing
  //     (pal1->pal4 recolor + the 8x8 ordered-dither clear) after the CANONICAL death bookkeeping
  //     ($AF02=FF + block+0x1D bit7, battleApplyDamage) already delisted it -- targeting/AI/victory read
  //     ONLY $AF02 (untouched), so this never feeds back into gameplay logic, purely a render overlay.
  int dissolveActive[8];     // 1 while slot's corpse is still drawing (hold + dither-clear in progress)
  int dissolveRunning;       // the ONE slot whose dither is currently advancing (-1 = none). CT runs the
                             //   death routines SERIALLY -- highest combatant index first, the next
                             //   starting the frame the previous clears (battle_death_dissolve_order.md);
                             //   every other dissolveActive slot is QUEUED and renders undissolved.
  uint8_t deathCceaSeen;     // $CCEA as CD/0043's wait loop sees it: the value the previous frame left (CD/3E75 first)
  int deathSfxDue[8];        // per enemy slot: 1 while its routine CD/0043 waits on $CCEA (set at the routine's start,
                             //   the #95 post clears it); 0 = nothing pending
  int dissolveStep[8];       // the running routine's dither level: -(CD/00BF's waits) at its start, +1 a frame from
                             //   the post; 1..CT_DISSOLVE_STEPS = levels cleared (CD/00CA's loop, one per CD/3ED2 upload);
                             //   > CT_DISSOLVE_STEPS -> the routine returns, dissolveActive cleared (CF/FE7B)
  int dissolveStepShown[8];  // ...as the NMI shipped it: CD/3ED2 uploads the ANDed tiles at the NMI, so the picture draws
                             //   the level of the frame before (battleNmiHead, beside the CGRAM page)
  int dissolvePkt[8];        // CD/3ED2's packets the running level has shipped (a level is CE:F156[$967C] / $400 of them)
  int deathFxAfter[8];       // the routine's CD/0E05 effect still to run after its dissolve (D0073 `LDA #40`), 0 = none
  int deathFxSlot;           // the slot whose routine is inside CD/0E05 (-1 = none) ...
  int deathFxId;             // ...the effect it loads (D008E #38 / D0073 #40)
  int deathFxT;              // ...frames into it: 0 = CD/0DFA's $5D9B wait, then 1 + the load's frame, then the run's wait
  int deathFxW;              // ...the load's palette wait (0, 15 or 31; CD/0EFD-0F25)
  int deathFxTd;             // ...its loop's CD/04AA: 1 while CD/0453 runs, 2 on the frame it returned (CD/04A2's wait)
  int headPend;              // the loop's head C1/8120 asked for while a death routine runs: it waits for the scan
  int deathHeadF;            // the frame the scan returned and that head ran (the idle tick starts on the next)
  int deadStanding[8];       // battle_dissolve.md ADD (T2 CORRECTION) X5: the FIGHT-ENDING kill does NOT
                             //   dissolve -- the enemy keeps rendering its idle stance (pal1) through the
                             //   victory celebration ("GATO stands undissolved"). Set when a death empties the
                             //   alive-list (no other enemy alive); the render keeps drawing that slot.
  int deadNoCmd[8];          // died in a DEMO battle: C1/AC75 skips the death scan CF/FE20 ($2989 bit 5), so no death
                             //   command (CD/0043: the dissolve, the #95) ever runs and the object stays drawn as it was
  uint32_t deadStandFrame[8];// battle_gato_idle_victory.md D.2: h->frames LATCHED at the deadStanding[slot]
                             //   transition. The dead enemy's idle clock FREEZES on his death-frame pose (the
                             //   oracle stops ticking his OBJ anim slot at the kill, §B.1: OAM byte-static
                             //   through the whole victory); the render reads this instead of the live,
                             //   still-advancing h->frames so GATO doesn't keep swapping his claws while dead.
  // --- BANK-CC ENEMY-AI INTERPRETER: the ATB turn's state is the ROM's own WRAM -- $B1D4/$B1E4 (the action
  //     pointer and its committed copy), $B26B (the section chosen), $B2B6 (evaluated since the turn), $5E15 /
  //     $B16E (the target the eval stored) -- see battleAiTurn. What stays host-side is the one step the ROM
  //     takes AFTER a synchronous action returns, which this engine animates over many frames:
  int aiTurnAdv[8];           // 1 = an ATB turn opened an action and C1/8E4A's step is due at its span end
  // --- REACTIVE WHEN-HIT COUNTER (disasm/notes/battle_counter_gating.md FIX C): GATO's attack-B ($9877=0x34)
  //     is his AI block-2 §1, fired REACTIVELY ~4 render frames after a QUALIFYING PC hit (NOT on his ATB
  //     turn -- §3 timer-independent), gated by cond-1F (the nearest living PC within 48px, §1). Per enemy
  //     SLOT: armed at the hit (battleApplyDamage) for a counter enemy (battleEnemyHasCounter), drained each
  //     render frame, dispatched by the tick (battleReactiveCounterTick) via the block-2 battleOpenAction
  //     path; his ATB-driven block-2 turn is a no-op. >0 = counting down; <=0 = inactive. Reset in CtBattleBegin.
  int enCounterDelay[8];
  int enCounterBy[8];         // the combatant whose action armed it ($B3B7, C1/8637) -- the block-2 walk's $B16E,X
  int enCounterK[8];          // its place in that action's victim list ($B315, C1/8671-880F)
  int enCounterWait[8];       // frames before its decode: 1 after a PC's command (C1/B967 returns a frame on), 0 after a turn
  int actCounter;             // the open action is a counter's (C1/8780): its span end arms no counter of its own
  int ctrPendSlot;            // a counter DECODED (C1/8686-8E15 on the first free frame) whose window has not opened: -1
  int ctrTgt, ctrIsTech;      //   none; its target and kind, for battleOpenAction's reactive open
  int ctrDecodeF;             // the frame its decode ran (the countdown starts on the next)
  int ctrMsgHold, ctrMsgSlot, ctrMsgOp, ctrMsgTgt;   // a counter's tech name box (C1/9B35 -> CD/0295) still up: the rest of
                              //   its decode (C1/8DA4-8E15) waits for it -- frames left, the slot, its op and target
  // --- ENEMY THINK HANDSHAKE (disasm/notes/battle_pacing_v2.md FIX B): the C1/28B0-arrival -> C1/40A6-
  //     dispatch handshake -- an enemy idles a fixed CT_BATTLE_ENEMY_THINK render frames between becoming
  //     ready+arrived and his action window OPENING (§3.B: mode-machine stop + AI cursor decode + dispatch,
  //     ~24f). Per enemy SLOT: armed on box-overlap (walk-in) or at the in-place decode (song/non-walk), then
  //     drained each render frame; on expiry battleArrive OPENS the deferred dispatch. The REACTIVE counter
  //     has its own ~4f latency (enCounterDelay), so it skips this. Reset in CtBattleBegin.
  int enThinkDelay[8];
  // THE ENEMY TECH'S NAME BOX (CD/0033 -> CD/0295; disasm/notes/battle_yakra.md sec. 4): the message byte the turn read
  // (C1/9B2A: `LDX $B1D2 / LDA $CC0001,X`, 0 = none) and the frames since the box was posted (-1 = no box in flight).
  int aiTurnTgtN;             // the decode's victim list (op 01: the target; op 02: C1/C1DD's, battleEnemyVictimsC1DD)
  int aiTurnTgtList[8];
  int aiTurnMsg;
  int nameBoxT;
  // THE TURN HANDLERS' OTHER TWO VERDICTS (C1/B88D, battle_ai_interp.md sec. 23): `aiTurnHdr` = the handler set $B3B8 = 1
  // (07 09 0A 10 16: C1/8E47 JSR C1/AC57 at once on the header it wrote, no C1/8DA4); `enMsgHold` = a turn with no action
  // whose handler posted a message (0B-0D 0F 11 14 ...): the frames CD/0295's box still holds it inside the turn (the
  // step and the handler's tail run when it returns). `mvPendHdr` / `mvPendAee3` carry the first to the window's open.
  int aiTurnHdr;
  int enMsgHold[8];
  int enGoneHold[8];          // the held turn ends in C1/8DA4-8DCF: its victim is an absent ENEMY (header, no action)
  int msgHoldEndF;            // the frame such a hold ended on (the loop's head ran there; no iteration frame A/B)
  int mvPendHdr, mvPendAee3;
  // --- THE READY SCANNER (battle_ai_interp.md §7, C1/847E): ATB-0 only FLAGS an enemy ready; the turn
  //     starts when the scanner's cursor -- walking the boot-shuffled $B163 turn order DOWNWARD, one
  //     entry per battle tick, reloading to $AEC7 -- lands on a ready+gated actor. The scan phase IS the
  //     old "think variance" (oracle latencies 24/8/3/0 = latch-wait + scan-distance + 3 processing).
  int scanCur;                // the $B185 cursor (index into $B163, counts down; reload scanTotal-1)
  int scanTotal;              // $AEC7 = alive PCs + 8 (the boot shuffle-1 entry count)
                              //   pass that detects the stop (oracle f461), cleared at turn start (184BD);
                              //   a mode-1 enemy's turn is GATED on it (C1/84A3-84BB)
  int aiCursor[8][2][4];      // per (slot, block 1 = when-hit, section): the REACTIVE path's cursor over block 2
                              //   (battleAiRunReactive). Row 0 is unused -- block 1 runs on $B1D4 (battleAiTurn).
  // --- ENEMY BATTLEFIELD MOVEMENT (disasm/notes/battle_enemy_move_target.md §A + Wave-2 ADDENDUM): the
  //     per-enemy-SLOT (0..7; combatant = slot+3) mover state machine. GATO walks to his victim before the
  //     block-1 physical (approach), strikes ADJACENT, then walks away (return); the song is sung in place.
  //     The mechanism is byte-derived from the ROM: the mover CF/F98F (8-phase trig segments), the mode
  //     machine C1/3714/373B (32f segment cadence), approach C1/379A (angle = atan2 C1/0222 toward the
  //     LIVE target, arrival = bounding-box overlap C1/28B0) and return C1/3A3D (away bearing + the derived
  //     C1/3AA0 stop at the ref = target + step). The LIVE position lives in the $7E image at the canonical
  //     $1D0C+cmb (X) / $1D23+cmb (Y) so render + the box test read it; these arrays hold the mover's own
  //     per-slot scratch (mirrors $974A/$9752/$9786/$98AF/$98B7/$9737/$98A7/$9791/$9819/$9897). All movement
  //     is gated on the pre-move MODE byte in the AI script, which is 0 for every MASH (when-hit) action, so
  //     a mash fight never walks -> the pinned mash signatures stay byte-identical. Reset in CtBattleBegin.
  int mvInited;               // 1 once battleMoveInit staged the live positions (render gate; 0 = pre-init)
  // The LIVE per-combatant battlefield position is the WRAM's $1D0C+cmb / $1D23+cmb (BT_LIVEX / BT_LIVEY, §A.2). The
  // field's camera words share that page; in a fight the field reads its page from C0/0918's copy at $7F:2000 (field.c),
  // as the cartridge does, so the battle owns the live bytes from the entry to C0/0905.
  // (was mvExtentY, now the WRAM byte below) $9729,cmb: the Y half-extent (E4/F600 record +8; the box TOP, C1/2889)
  // (the C1/283D mirror of the feet, $A039/$A050,cmb, is the WRAM's: BT_MIRX / BT_MIRY above -- refreshed before each
  //  handler loop, overwritten by a slot's PROJECTED step within it; see battleBox)
  // (was mvTarget, now the WRAM byte below) $9752,X: the move-target COMBATANT idx (approach walks to it / return from it)
  // (was mvAngle, now the WRAM byte below) $9786,X: the current segment bearing (0..255, 0x100 = full turn)
  // (was mvBaseX, now the WRAM byte below) $98AF/$98B7,X: the segment's base position (re-read from live pos each segment)
  // (was mvPhase, now the WRAM byte below) $9737,X: the 8-phase mover counter (offset = trig(angle)*phase>>8, 0..8)
  // (was mvActive, now the WRAM byte below) $98A7,X: 1 while a segment is mid-advance (mover runs); 0 = segment done/settled
  // (was mvWalking, now the WRAM byte below) $9791,X: the STORED-BEARING latch (battle_ai_interp.md §9): set by a BLOCKED
                              //   return step (the bearing axis-quantized), reused on later passes (skip the
                              //   recompute/reverse); cleared by an unblocked walking pass. Also the mode-1
                              //   arrival hold flag (historical name).
  // (was mvRefX, now the WRAM byte below) $A5E4/$A5EC,X: the return REF = victim + trig(away)*$AE (persists across
                              //   passes; the 2B13 settle test measures against it)
  // ---- what class 4's SETTLE records for class 0's RE-ACQUIRE (C1/3B45-3B58 -> C1/3772) ----
  // ---- the $A4 GROUP SCHEDULER (battle_facing_engine.md, the 2026-09-08 section) ----
                              //   (CF/E477 INC / CF/E47F STZ on ==4); bank C1 borrows and restores it.
  int pcEntranceArmed;        // one-shot: the boot's C1/0042 `JMP C1/34DB` has armed the ENTRANCE ladder
  int pcBandPrev;             // previous $CA1F (CD/0534's predicate), to catch the entry-fade loop's completion edge
  // (was mvSaveMode, now the WRAM byte below) $5E1D,X: the requested move mode saved at the settle (C1/3B48)
  // (was mvRecX, now the WRAM byte below) $9831/$9839,X: the victim's live foot AT the settle (C1/3B52/3B58) --
                              //   class 0 compares the victim's CURRENT foot against these and, when it
                              //   has moved off, hands the saved mode back (C1/3790).
  int mvFacing[8];            // $96DE,X (battle_pc_approach_facing.md §2.2): per-enemy FACING =
                              //   ctBattleFaceTab[walk bearing] (C1/37E9), stamped when the mover (re)aims a
                              //   segment; HELD while idle/attacking (§2.6). 0=up 1=down/front 2=left 3=right;
                              //   default 1 (faces the party). battleGatoRender selects the pose bake by it.
                              //   C1/350F = ctBattleFaceTab[atan2(nearest present+alive enemy)] on the live
                              //   feet (validated 11/11 across bat1-9); re-stamped by the attack phases
                              //   (arrival aims at the victim, return aims home, settle re-runs the entry law
                              //   -- §4). Selects the per-facing E4 idle/run/strike windows in the render.
  // ---- THE IDLE-TICK CURSOR WALK (C1/2D9F -> C1/3234 -> C1/333D..34A6; battle.c battleIdleWalk). One enemy
  //      GROUP per dispatch of C1/10A9 ($A4 = the group); per visited slot: the $5E0D->$974A commit + sprite
  //      rebuild (the change detector), then the idle window's dwell/cursor/publish tick. ----
                              //   mode 0 INCLUDED), class 0's hand-back (C1/3793). ONLY the walk's commit
                              //   (C1/3364) moves it into mvMode ($974A) -- measured lead 1..6 frames (bat9
                              //   f175->f181, forest3 f228->f231, f243->f244).
                              //   raw $96DE (enFacing) at every visit -- a changed facing rebuilds the rows too
                              //   (C1/344C), consumed by the roster builders (C1/3252..332C) and by C1/1C5A
  int idleDeathPose[8];       // the published pose at the death frame (the dissolve keeps drawing it); FF = none
  int schedWalk;              // this frame is a DISPATCH frame of the idle tick (C1/10A9) -- see battleSchedAdvance
  // ---- THE PC GROUP of the same walk ($A4 == 0: C1/2DA9 -> C1/2F1E; battle.c battlePcWalk) ----
                              //   reload = max(1, ptrB dwell / 5) (C1/2ECB-2ED7, the C1/00D7 divide)
                              //   whose next expiry re-aims its facing at the nearest enemy (C1/2986 + C1/0222)

                              //   mover aims / attack script (same sites as mvFacing). The GENERAL enemy
                              //   render keys the ROM windows on this (bat1/3/4 staged facings all 0).
                              //   mvFacing above keeps its default-1 GATO-render interim (§2.6) -- the two
                              //   models merge when GATO's window-vs-facing wrinkle is derived.
  // (was mvSegCad, now the WRAM byte below) $9819,X: the mover's per-slot frame counter -- DEC'd EVERY frame by the mover loop
                              //   CF/F984, reloaded from $9742,X on the wrap (CF/F989-F98C); written by nobody else
                              //   (the loader CC/EBE0 seeds it = the period; a segment start does NOT re-seed it)
  // (was mvModeReload, now the WRAM byte below) $989F,X: the pass counter's reload = CC/F544[((period >> 1) << 2) + (step >> 1)]
                              //   (CC/EBE3-EBF4): 0x20 for a record byte 0x41 (GATO, Hetake), 0x10 for 0x21 (Yakra)
  // (was mvSegPeriod, now the WRAM byte below) $9742,X: the reload = E4/F609[id] >> 4 (CC/EBD5-EBDD): 4 for a Hetake/Avian, 6 for a Beetle
  // (was mvPhaseStep, now the WRAM byte below) $98C2,X: the phase increment per wrap = E4/F609[id] & 7 (CC/EBCC-EBD2)
  // (was mvModeCad, now the WRAM byte below) $9897,X: mode-machine cadence counter. Staged $989F + slot (CC/EBF7-EBFA, the
                              //   per-slot STAGGER); counts down on every idle frame (writer C1:3727) while the
                              //   live mode is not 0 (C1/371F `BEQ`); wrap -> reload $989F + run the mode
                              //   handler. Frozen with the
                              //   rest of the mover by $A013 spans; the pending-turn think skips the HANDLER
                              //   only (oracle: counter decrements f454-478, the f461 pass never arms).
  // (was mvExtent, now the WRAM byte below) per-combatant box HALF-EXTENT $971E/$9729 (oracle-read: GATO 16, PC 8): the
                              //   arrival box is [live +/- ext] X, [live-ext .. live+8] Y (writer CC/E285A).
  // E.3 RESOLVED (battle_enemy_move_target.md addendum + battle_facing_engine.md SESSION-7/8): the enemy
  // dispatch is POSITION-INDEPENDENT -- it never waits for arrival. An enemy turn latches here between the
  // ATB-0 decode and the window opening (the ~24f think, oracle f454->f478); the walk runs CONCURRENTLY on
  // its own cadence, and the strike fires from wherever the enemy rests (a remote attack is canonical).
  // While a turn is pending the acting enemy's own mode HANDLER holds (position constant through the think).
  int mvPendAtk;              // combatant idx whose dispatch is pending through the think (-1 = none)
  int mvPendDecoded;          //   ...and its command already decoded (C1/8DA4-8E15 at C1/4058's frame, battleArriveDecode)
  int mvPendTgtN;             //   ...its VICTIM LIST ($AD8E / $AD8D, C1/C1DD at the decode), the target first
  int mvPendTgtList[8];
  int mvPendTgt;              // the resolved victim combatant (single RNG draw at decode; reused at dispatch)
  int mvPendTech;             // the pending action is a TECH (op 02) vs a physical (op 01)
  // --- THE TECH COMMAND (disasm/notes/battle_tech_slice.md): Crono's Cyclone vertical slice -- the per-seat
  //     Tech submenu ($95DB=1) + the PC-tech dispatch (MP-at-animate, physical-coef damage, bank-CE span).
  //     A-on-Tech opens the submenu; the learned-tech list is built from the RAW $7E2837 seed (NO field-menu
  //     threshold-add: oracle-proven $2837=0 -> empty list, =0x80 -> Cyclone only, §5); the resolver fills
  //     $9EE3-9 + greys curMP<MP; confirm -> target-select with the tech's group; the target-confirm threads
  //     the tech id through the SAME schedule FIFO as a physical (schedTech parallel to schedAtk). ---
  int techOpen;              // 1 = the per-seat Tech submenu is OPEN ($95DB=1, list navigable, §2.2)
  int techSel;               // selected slot within the learned-tech list (0..techListN-1; the $95DF cursor)
  int techListN;             // number of learned techs for the current seat (= $A02A, the nav cap)
  int techListId[8];         // per-slot learned tech id (compact; the $94D0/$9551 list source, §2.2)
  int pendTechId;            // the tech id CHOSEN in the submenu, carried through target-select to the commit
  int actTechId;             // the DISPATCHING action's tech id (0 = physical); read by battleActionSpan to
                             //   route a PC tech through CD/45A6->CD/5EF0->bank CE instead of the melee tables
  int schedTech[3];          // FIFO parallel to schedAtk/schedTgtSlot: the committed action's tech id (0=phys)
  // --- THE ITEM COMMAND (disasm/notes/battle_item_slice.md): Crono's Tonic vertical slice -- the Item
  //     submenu ($95DB=2) + the PC-item dispatch (NO MP, heal = negative-delta damage, bank-CD/CE anim).
  //     A-on-Item opens the submenu; the list is the battle item STAGING $1580 (a FILTERED COPY of the field
  //     inventory $2400/$2500 built at battle entry, FD/B01C); the cursor is GLOBAL ($95E5, NOT per-seat like
  //     techs, §2.3); confirm-item resolves $9F35/$960D/$9F36 gated on count!=0 (the ONLY in-list block --
  //     items have NO grey rule); the target-confirm CONSUMES (DEC $1583 on the copy, §2.5) + threads the item
  //     id through the SAME schedule FIFO (schedItem parallel to schedTech). Effect = the universal HP writer
  //     with a NEGATIVE delta ($B328=-amount, curHP-=that=+amount, clamped to maxHP). ---
  int itemOpen;              // 1 = the Item submenu is OPEN ($95DB=2, list navigable, §2.2)
  // (the item list is the ROM's own: the 256 records at $1580, the cursor $95E5 over the window's top $95E6)
  int pendItemId;            // the item id CHOSEN in the submenu, carried through target-select to the commit
  int pendItemSlot;          // the CHOSEN record (0..255; its offset is $9F36), for the traces
  int actItemId;             // the DISPATCHING action's item id (0 = not an item); routes battleActionSpan to
                             //   the PC-item anim (CD/5C26 config) + the heal effect
  int actItemMpAmt;          // O1: the SECOND (MP) restore component of a dual item (Elixir/MegaElixir,
                             //   record b1 = 0xC2 = HP bit7 AND MP bit6). 0 for every single-component item,
                             //   so those stay byte-identical; > 0 only when the ROM stages two hit-slots.
  int actItemMp;             // battle_items_nonhp.md §2: 1 = this item's restore delta targets curMP (Ether,
                             //   record b1 bit6) not curHP (Tonic/Revive, b1 bit7); set in battleComputeItemEffect,
                             //   read by the span-end apply to pick the MP-clamp path (block+7 vs block+3).
  int schedItem[3];          // FIFO parallel to schedTech: the committed action's item id (0 = not an item)
  int schedItemTgt[3];       // FIFO: an item's healed PC combatant idx (the target-select result; -1 = n/a)
  int itemTgtSel;            // battle_items_nonhp.md §0.5 (item lane O2 partial-close): the item target-select's
                             //   chosen PC seat (0..2), navigable over PRESENT seats (Down/Right +1, Up/Left -1;
                             //   default = the chooser). Committed as schedItemTgt so an item heals/revives the
                             //   SELECTED ally, not just the chooser (proven: oracle Down-nav → seat1 Marle).
  // ---- THE LIVE ANIM-SCRIPT INTERPRETER (disasm/notes/battle_enemy_anim_script.md). The ROM's C1/4CA2
  // loop, ported: 16 tracks, MSB-first from the header mask, ONE opcode per track per frame unless the
  // handler sets the `$A00F` continue flag (which is READ FROM THE ROM, never tabulated here). Drives the
  // ENEMY action window's script-owned events -- the approach/return slides, the sync barriers and the
  // sound opcodes. Track index doubles as the C1/762E resolver ROLE (`$A014,X`).
  int ascOn;                  // 1 = a script is loaded and running for the current action window
  int ascSel;                 // the $9877 selector the loaded script came from (trace/pin)
  int ascRole[16];            // $A014,X: the track's COMBATANT (0..10), or -1 for a scratch/effect object
  int ascLive[16];            // $5DAC,X: track active
  uint32_t ascIp[16];         // $5DBD,X (24-bit): the track's opcode pointer, flat ROM offset
  uint32_t ascRet[16];        // the 1-deep `1E`/`1F` super return ($A569)
  int ascPause[16];           // $5DFD,X: the per-track frame countdown (op 0x20)
  int ascSpeed[16];           // $A324,X: the move speed set by ops 08-0F (CC/F4BC)
  int ascMvN[16];             // remaining slide steps (the track parks while > 0)
  int ascMvOn[16];            // $A337 / $A342: the track's move op is ARMED and parked on it until the release
  int ascMvDone[16];          // $A19D / $A1E0 for a slide: the mover's tick that ran the count out (the visit releases on it)
  int ascMvTicked[16];        // the mover ticked this track's slide THIS frame (battleAscMoverFrame, before the tracks)
  int ascFwdOn[16];           // $A337 / $A342 for a move-FORWARD (A8/A9/A2): parked on the op until the visit releases it
  int ascParkLen[16];         // $A3A9,X: what the release advances the cursor by (the arm stores its op's length)
  int ascFootWas[11];         // CT_BATTLE_ASCIP's last printed live foot per combatant ((x << 8) | y; -1 = none yet)
  int ascStepX[16], ascStepY[16];   // 8.8 per-frame slide delta (battleSlideArmSteps)
  int ascAccX[16], ascAccY[16];     // the CF/F04E 16-bit accumulators (seeded 0, C1/63C9)
  int ascBaseX[16], ascBaseY[16];   // $A35D/$A370: the foot latched at the arm; live = base + acc-hi
  int ascScrX[16], ascScrY[16];     // a scratch object's own position (roles with no combatant)
  int ascHead[16];            // $A5AA,X: the object's stored heading (op 0x76), used by op 0xA8/0xA9
  int ascFwdN[16];                  // op 0xA8/0xA9 move-forward: $A639,X the count (DEC'd by the service, and by A9's visit)
  int ascFwdAcc[16];                // $A311,X the accumulator (+= the speed per service pass)
  int ascFwdDone[16];               // $A19D,X the service's FINISHED flag
  int ascFwdOp[16];                 // the op that armed it: A8 (waits for the service), A9 (re-tests the way ahead
                                    //   and counts down again) or A2 (re-tests the grid ahead only)
  int ascFwdX[16], ascFwdY[16];     // $A35D/$A370,X: the latched start the polar offset is applied to
  int ascRate[16];            // $A2FE / $A309: the mover's per-object RATE DIVIDER (CC/F5BC[op-8])
  int ascDiv[16];             // $A2EB / $A2F6: its live countdown -- the mover runs only when it hits 0
  int ascHomeX[3], ascHomeY[3];     // $99AF/$99B2: resolver modes 0x19-0x1B = the actor's dispatch position
  int ascWalkOn[16];          // op 18 armed its grid walk on this track (C1/5480), the walker runs until it stops
  // THE SLIDE'S HOP (ops 98/99/9A/9B, C1/6299 arm + the mode-1 service CF/F087). Per track:
  int ascHopOn[16];           // the slide is a mode-1 one (ops 11/12 are mode 0: no hop, $A4AF untouched)
  int ascHopMm[16];           // $AA04,X: the `98 mm tt` operand -- or $90's value for 99/9B, which never read one
  int ascHopDown[16];         // $AA2A,X = $8F: 9A/9B set it (the offset is +h, drawn BELOW the foot)
  int ascHopDec[16];          // $A9B8,X: (mm == 2 ? 7FFF : FFFF) / (dominant >> 1), C1/00D7
  int ascHopInit[16];         // $AA3D,X: the velocity a fall reloads when it carries out
  int ascHopVel[16];          // $A920,X (16-bit; its high word $A922 is only ever zeroed)
  uint32_t ascHopH[16];       // $A96C/$A96E,X: the 32-bit height; its third byte is the pixel offset
  int ascHopFall[16];         // $A9DE,X: 0 rising, 1 falling
  int ascScr90;               // $90 as the script visits see it: C1/4212 (every loop frame, before C1/4BBE) leaves 06
  int ascMv90[16];            // the `mm` a parked 98/9A re-stores into $90 on each re-visit (-1 = not a 98/9A)
  int ascLoaded;              // this action's script was loaded (reset at every action open): its op 0x50 shows the numbers
  int ascHopY[11];            // $A4AF,X: the combatant's vertical draw offset (CF/EAF8 adds it to $1D23)
  int ascShadow[11];          // $A5D8,X: D0 INCs / D1 clears it; CF/EA8A draws the ground shadow while it is set
  int ascStall;               // 1 = the interpreter HALTED on an underivable op (traced; span owns the rest)
  int ascGateSaid;            // CT_BATTLE_ASCTRACE: bit 0 = "track 4 not run" said, bit 1 = "effect tracks wait" said
  // ---- THE POSE ENGINE (C1/423A + C1/424C), per COMBATANT, not per track. The playAnim opcodes only ARM
  // it; this is the machine that walks the cursor and publishes the displayed assembly index.
  int ascPmMode[11];          // $A124,cmb: 0 = idle / 1 = one-shot (parks its track) / 2 = loop
  int ascPmDwell[11];         // $A12F,cmb: the current entry's frame countdown (C1/423C decrements it)
  int ascPmCur[11];           // $A13A,cmb: the entry cursor; the arm sets it 0xFF so the first ++ gives 0
  int ascPmHold[11];          // $A150,cmb: the HOLD entry+1 (ops 04/05/06 set 1, op 26 sets nn)
  int ascPmLatch[11];         // $A145,cmb: op 0x03's own "already armed" latch
  int ascPmAnim[11];          // the playAnim operand -- our stand-in for the $A1E8/$A1FE pointer pair,
                              //   which the ROM advances by operand*4 (anim_window_offset.md)
  int ascPmPose[11];          // $967B: the PUBLISHED assembly index, or -1 while nothing is armed
  int ascPmRR;                // $A21F: the round-robin cursor -- ONE combatant advances per frame
  int ascPmBlock;             // $A012: set on every publish, cleared at the frame boundary (C1/4133)
  uint8_t composeHeld;        // $A028 as this frame's CF/E764 read it (before the frame service's CD/09B2 copies $CD29 in):
                              //   non-zero = the combatants' compose CF/E83A and CF/E775's `DEC $A012` do not run
  unsigned holdBtnPrev;       // CT_HOLDBTN's $00F2 mask of the frame before: the battle NMI reads the auto-read of frame N-1
  uint8_t menuPassHold;       // an action's tail from C1/4116 on: no C1/10FA menu pass until $A013 clears
  int pcThinkDelay;           // a PC's committed command inside C1/B967 -> C1/4058 (its two waits before $A013)
  int dispOpenPend;           // C1/B967 has decoded the command (its WRAM, the MP, the effect) and C1/4058's waits run:
  int dispActor, dispTgt;     //   C1/40A6 opens it (battleDispatchOpen) with these
  int dispTech, dispItem;
  int fxStageNext;            // the action's staging (battleFxActionLoadFrame): the next rel it runs, -1 = a new action
  int fxStageAct;             //   ...and the actStart it belongs to
} CtBattleHost;

// Load + cache the ROM file (the enemy stat table CC/4700, name table CC/6500 and the field enemy
// records E4/F600 are read from it). Returns 1 on success; without it the module still runs but
// stages zeroed enemy stats.
int CtBattleLoadRom(const char* path);
void CtBattleAscCensus(void);                 // --asccensus: every PC anim script's opcodes (debug)
const uint8_t* CtBattleRom(long* lenOut);        // the cached ROM (NULL if not loaded)
// How many times an effect program slot was killed by an opcode this port has not derived, over the whole process.
// Zero is the assertion attract-0ad-xstrike-program-vs-rom pins (the host prints it at exit under CT_BATTLE_TRACE).
int CtBattleFxProgStops(void);
const uint8_t* CtBattleFieldEnemyRecs(void);     // ROM $E4/F600 (256 x 10B field enemy records) or NULL

// C1/B8BB's targeting resolvers in their LIST form (battle_enemy_move_target.md §G). Fills `$AECC[0..]`
// in `mem` and the count in `$AECB`, exactly as the ROM's handler for `value` does -- these do NOT
// return a target, which is why battleAiTarget's `int` cannot carry values 01 and 02. Returns 0 when the
// list is valid, -1 when this value's handler is not ported (05/06 have their own wrapper; 07-09 and
// 11-17 are underived), and -2 on the ROM's OWN non-termination in C1/ADA1. Graded by --sweepaitarget.
int CtBattleAiResolveList(uint8_t* mem, const uint8_t* rom, int value);
void CtBattleAreaBandMem(uint8_t* mem);   // C1/25A3, the band of C1/1FDD routine 0 (ct.exe --sweepband25a3)
// Program op D9 n k (CD/19B1 -> CD/2433, CE/E7CF, CD/2466, D1/F9AF) on a bare $7E page; graded by ct --sweepfxline.
void CtBattleFxLineMem(uint8_t* mem, int n, int k);
// The shape-window ops 8A / 89 / AB / 8B (CD/2FED, CD/2FBC, CD/24F3, CD/2FB7 -> the six shapes) on a bare 128 KB
// $7E/$7F image, operands b[0..3]; returns 1 where the port refuses (8B only: a shape above 5, or a placer's copy the
// ROM would enter outside CE/E1C7..E7C7), else 0; *adv = the bytes the handler moved $40 by (8A/89: 3). ct --sweepfxshape.
int CtBattleFxShapeOpMem(uint8_t* mem, int op, const uint8_t* b, int* adv);
// Program ops 85 86 A1 A3 A4 AC BD C3 D2 DA E0 on a bare $7E page, operand bytes `b` as if at $40 = 0300; returns the
// $40 the handler leaves (-1: not one of them). Graded by ct --sweepfxquad.
int CtBattleFxOpMem(uint8_t* mem, int X, int op, const uint8_t* b);
// One effect-program op at D1:opAddr (slot X = 0 / 0x10) on a bare WRAM image, as CD/16A6 dispatches it; returns $40
// after the handler, -1 on a STOP. Graded by ct --sweepfxops.
int CtBattleFxProgOpMem(uint8_t* mem, int X, int opAddr);
// Op 88's sub-op `sub` (D1/FBCE -> D1/FBD2[sub]: FCF4 FC38 FC96 FBDA) on a bare $7E page; -1 past the four. --sweepfxops.
int CtBattleFxOp88Mem(uint8_t* mem, int sub);
void CtBattleShakeNmiMem(uint8_t* mem);
void CtBattleArenaScrollDelta(const uint8_t* mem, int H, int16_t* d, uint8_t* arena);   // per fb row: BG1 dx dy, BG2 dx dy (HDMA ch1/ch2 - staged); arena = which rows read CD/06B4's records   // ct --sweepshake: CD/06B4 + CD/0B51 (the scroll records, the screen shake, CD/0C48)
// The battle effect layer over an arena composite `fb` (W x H RGB888, row y = PPU line y+1): the HDMA tables in `mem`,
// BG3 from h->fxBg3Map/fxBg3Chr, windows + colour math. field.c draws it before the sprites; ct --sweepfxcompose grades it.
void CtBattleFxLayerCompose(uint8_t* fb, int W, int H, const uint8_t* cov, const uint8_t* mem, const CtBattleHost* h);
// The widescreen's arena (a mod): the same compose over a wider picture whose view starts at column x0 -- see battle.c.
void CtBattleFxLayerComposeWide(uint8_t* fb, int W, int H, const uint8_t* cov, const uint8_t* mem, const CtBattleHost* h, int x0);
// ...and after the sprites: BG3's front pixels come back from `atCompose` (the fb as the compositor left it), and those
// whose colour math adds the sub screen (the main screen less BG3) are finished against the pixel now under them.
void CtBattleFxFrontApply(uint8_t* fb, int W, int H, const uint8_t* atCompose, const uint8_t* cov);
// ...and last, the sprites' own share of the rows the compositor evaluated: the clip, and the colour math of the OBJ
// pixels of palettes 4-7 (CGADSUB bit 4). `objPal` = per pixel the OBJ palette of the sprite on top (0xFF = none).
// Returns the pixels it changed. ct --sweepfxcompose grades it on the cartridge's own OBJ layer.
long CtBattleFxObjMath(uint8_t* fb, int W, int H, const uint8_t* objPal, const CtBattleHost* h);
int CtBattleFxRowRegs(int y, uint8_t* cgwsel, uint8_t* cgadsub, uint16_t* fix);   // the last compose's row y (1 = its row)
// C1/AC14 -- the TARGETING REQUEST: advance the bank-CC cursor $B1D2, read the targeting value there,
// run C1/B8BB's resolver (or take the literal from $AED8 when bit7 is set), mirror the list into
// $AD8E and put its FIRST entry in the single-target shadow $B2AE. Same return code as the resolver.
int CtBattleAiTargetRequest(uint8_t* mem, const uint8_t* rom);
// --sweepaieval: C1/AFD2 (the AI condition walk) on `mem` with $B252 set; stops at the first commit that commits
// (C1/8C53). 1 = stopped there, 0 = the walk returned, -1 = an unported condition/resolver (flags in *unported).
int CtBattleAiWalkSweep(uint8_t* mem, const uint8_t* rom, int id, int* unported);
int CtBattleAiCondSweep(uint8_t* mem, const uint8_t* rom, int op, int* unported);   // one C1/B80D handler
int CtBattleAiCommitSweep(uint8_t* mem, const uint8_t* rom, int* unported);         // C1/8C3E + its eval handlers
int CtBattleIdleWalkProbe(uint8_t* mem);
int CtBattleModeMachineProbe(uint8_t* mem); // C1/3714 on a WRAM image (the mode machine: cadence, $9752, the nine handlers)    // C1/2D9F on a WRAM image (enemy groups; -1 = the PC group)
int CtBattleAiWalkWhole(uint8_t* mem, const uint8_t* rom, int id, int* unported);    // the walk, its commit run whole
int CtBattleAiCondWhole(uint8_t* mem, const uint8_t* rom, int op, int* unported);    // one handler, the same
int CtBattleAiAC14Sweep(uint8_t* mem, const uint8_t* rom, int* unported);           // C1/AC14 + the C1/B8BB resolver
int CtBattleAiTurnSweep(uint8_t* mem, const uint8_t* rom, int op, const uint8_t* c1dd, int* unported, int* msg);   // one
                                                                                   //   C1/B88D turn handler (c1dd: the
                                                                                   //   stubbed C1/C1DD's 13 output bytes)
// C1/F205, the victory's tech pass (single, dual and triple techs) on the WRAM, with $7F:01E0 passed in.
void CtBattleTechPass(uint8_t* mem, const uint8_t* rom, uint8_t m7f01e0);
void CtBattleVictoryTechPass(uint8_t* mem, const uint8_t* rom, int tp, uint8_t m7f01e0);   // + the pool $B2DB = tp
// C1/B279 for ONE combatant -- the dead-flag refresh the 11-slot sweep C1/B223 runs over, and which
// C1/A452 (targeting value 05) calls three times by hand. Returns which arm ran: 0 absent, 1 living,
// 2 dead-or-hidden PC (whose tail runs on into the death-effect dispatch and is NOT ported),
// 3 dead-or-hidden ENEMY (complete: the stores plus C1/B3A0's own five instructions).
int CtBattleDeadFlagProbe(uint8_t* mem, int cmb);
// C1/C90B and C1/C92A -- the battle engine's software MULTIPLY and DIVIDE, on the direct-page
// words $28/$2A/$2C/$2E/$32. The multiply returns its carry out and DESTROYS $2A; the divide
// takes $2E as the middle word of a 48-bit register and restores the caller's flags.
int  CtBattleMul16Probe(uint8_t* mem, int carryIn);
void CtBattleDiv32Probe(uint8_t* mem, int carryIn);
int CtBattleRngBaseProbe(uint8_t* mem, const uint8_t* rom, int maxv, int base);
// C1/DC64 -- the physical TO-HIT roll, on the ROM's interface: hit in $16, evade in $18; the
// verdict comes back in $16, the roll in $18, and $AE4F is zeroed. Returns 1 = HIT, 0 = MISS.
int  CtBattleHitCoreProbe(uint8_t* mem, const uint8_t* rom, int* pctOut, int* rollOut);
// C1/E9A8 / C1/E9C5 -- stage a combatant's block base ($B1F4 attacker, $B1F6 target) as
// index * 0x80, through the engine's own multiply. `which` picks the destination.
void CtBattleStageBaseProbe(uint8_t* mem, int idx, int which);
// C1/D174 -- fetch $16/$18 from $AEE9/$AEEA through those bases, zeroing $16-$19 first.
void CtBattleStatFetchProbe(uint8_t* mem);
void CtBattleRedirectInitProbe(uint8_t* mem);
int CtBattleRedirectProbe(const uint8_t* mem, int seat);
int CtBattleRevalidateProbe(uint8_t* mem, const uint8_t* rom);
extern int gBtAiReduceNonTerm;                   // seeds that reached C1/ADA1's non-terminating branch

// --sweepmoveclass's entry point: run ONE enemy move-class handler (C1/3760's classes 2, 3, 7 and 8) out
// of a flat $7E image and write its results back, so a sweep can seed WRAM, run this, micro-execute the
// ROM's own handler on the same seed and compare WRAM to WRAM. `slot` is 0..7; the class comes from
// $A2B5 through CC/FBAB, exactly as C1/373B dispatches it. Returns the class run, or -1 when the mode's
// class is not one of the four (the other five have their own grading).
int CtBattleMoveClassProbe(uint8_t* mem, int slot);

// battle_victory_rewards.md: XP level-up + stat growth for ONE PC battle block, byte-exact per ROM
// C1/F623 (-> F7B9 level threshold + F871 HP/MP growth + the F633 core-stat recompute). Operates on the
// block $5E2D+seat*0x80 (all oracle writers block-relative), so the battle-end block->sheet writeback
// carries the new level/stats to $2600 (the menu shows them for free). `xpEarned` = the fight's XP total.
// apply!=0 writes the block (level+0x12, maxHP+0x5, maxMP+0x9, stats+0x0B.., NEXT+0x2B); apply==0 is a
// read-only prediction (for the message). Returns the number of levels gained. ROM tables read via `rom`.
int CtBattleLevelUp(uint8_t* mem, const uint8_t* rom, long romLen,
                    uint32_t block, int charId, int xpEarned, int apply);

// Enemy battle-sprite SHEET, ROM-driven (disasm/notes/battle_sheet_decomp.md — the anim-engine O3/C5
// close). CtBattleEnemySheetSrc: the CT-LZ compressed source's HiROM file offset for enemy `gfxid`
// (id $1101; GATO 0x92 -> $DF:0000) via E4:F600[+0] -> E4:2000; -1 if ROM absent/out-of-range.
// CtBattleDecompressEnemySheet: decompress it into `out` (<= maxBytes of 8x8 4bpp tiles); returns bytes.
long CtBattleEnemySheetSrc(int gfxid);
int  CtBattleDecompressEnemySheet(int gfxid, uint8_t* out, int maxBytes);

// The enemy POSE-SCRIPT staging, resolved from the ROM by the id-keyed chain (disasm/notes/battle_pose_
// staging.md; the pose analogue of the sheet lane): E4:F600[id*10] record -> the per-slot pose-script
// pointers $1CA2/$1CC3, the E3 assembly base $1C78, the window stride $9671, and the sheet source. All
// SNES 24-bit addresses (as the oracle stores them). Verified byte-exact vs the live staging for GATO/
// Naga-ette/Hetake/Diablos. This makes the enemy pose STAGING id/ROM-driven (no capture) -- Diablos's
// idle SCRIPT + BASE are now derived, closing O6's script/base half.
typedef struct {
  uint8_t animId, packet, palIdx, sizeByte;   // record +3 (initial animId), +1 (packet), +2 (palette), +4 (size/anim-set)
  long    ptrA, ptrB;                          // pose-script A/B: E4:2600/2800[animId*2] | bank E4
  long    e3base;                              // E3 assembly base: E4:2300[packet*3]
  long    sheetSrc;                            // compressed sheet source: E4:2000[record+0 * 3]
  int     stride;                              // window stride $9671 = (E4:2600[animId+1] - E4:2600[animId]) >> 2
} CtEnemyPoseStage;
int CtBattleEnemyPoseStage(int id, CtEnemyPoseStage* out);   // 1 on success, 0 if ROM absent / id out of range

// The idle REST assembly index (cursor 0 of the idle pose window), given the staged mode ($974A) + facing
// ($96E9): $82 = classbase(CC/FB91[mode]) + facing*stride, rest = ptrA[$82]. Byte-exact vs the oracle-staged
// $97D9 window. Returns the E3 index, or -1 on failure.
int CtBattleEnemyIdleRest(int id, int mode, int facing);

// Idle-pose GATHER coverage: how many of the idle rest E3 record's non-blank cell-words are covered by the
// enemy's decompressed ROM sheet (proves the pose renders fully from ROM). `ncell` = OBJ-cell count (type-0
// = 4, type-2 = 12). Returns the covered count; *nonblank = non-blank cell-word count; -1 on failure.
int CtBattleEnemyIdleGather(int id, int mode, int facing, int ncell, int* nonblank);

// Decode enemy `id`'s name from ROM CC/6500 + id*11 (11-byte records, pad byte 0xEF) into ASCII.
// Returns the length written (out NUL-terminated; cap should be >= 12). 0 if the ROM is absent.
int CtBattleEnemyName(int id, char* out, int cap);

// ---- THE FACING ENGINE (disasm/notes/battle_facing_engine.md) ----
// PC pose staging is the SAME E4 table walk as enemies, keyed by charId 0..6 (CC/E97E):
// sheet = E4:2000[cid*3] (RAW 4bpp), E3 base = E4:2300[cid*3], ptrA/ptrB = E4:2600/2800[cid*2],
// stride = (ptrA[cid+1]-ptrA[cid])>>2. ptrA = 4 facing ROWS; window = row + class base byte offset.
int CtBattlePcPoseStage(int charId, long* e3base, long* sheetBase, long* ptrA, long* ptrB, int* stride);
// The IDLE window (+0x0C, mode 0 -> class 0) poses[4] + ptrB dwell bytes for `facing` (0=up 1=down
// 2=left 3=right). Returns 1 on success (ROM present, charId 0..6).
// The per-combatant STATUS SCAN (ROM C1/2F97, disasm/notes/battle_status_render.md): one table-driven
// pass over CC/F77F emits BOTH status outputs -- the POSE CLASS ($A440, which shifts the pose window by
// cls*4) and the ICON STATE ($A441,X). Defaults cls=3 / icon=-1, which is what every pinned fight sees
// because stageAll zeroes the status byte at combat start. No sleep-specific path exists in the ROM.
void CtBattleStatusScan(const uint8_t* mem, int cmb, int* poseCls, int* iconState);
// The render layer's clock (battle.c, beside the boot): h->frames, carried with a pinned fixture boot's move so GATO's
// pose cursor, the low-HP blink, the combat-live gate and the target page stay locked to the fight as the cartridge's are.
int CtBattleRenderClock(const CtBattleHost* h);
// ...with the scan's no-record end (C1/302C): *lowHp = 1 when block +$02 bit 0 (curHP <= maxHP/8) gave the class $12.
void CtBattleStatusScanEx(const uint8_t* mem, int cmb, int* poseCls, int* iconState, int* lowHp);
// The STATUS ICON (C1/308C -> ($3216,X) -> CC/F6D4). dx/dy are offsets from the combatant's live foot;
// `tile` is the full VRAM name (0x1xx). NOTE: dx/dy are CONSTANT for sleep -- the animation is a 2-frame
// tile swap, not a float (see battle_status_render.md).
int CtBattleStatusIconLive(const uint8_t* mem, int cmb, int* dx, int* dy, int* tile, int* pal);   // CF/EE2C's record for
                                                                     //   the combatant's live $A4D0 group + $A4DB cursor
const uint8_t* CtBattleStatusIconTile(int vramTile);   // 4bpp 32B tile, ROM 0x113900 + (name-0x1A0)*32
const uint8_t* CtBattleShadowTile(int q);              // the D0 ground shadow's 16x16 cell, q = 0..3 (names $02 $03 $12 $13)
const uint8_t* CtBattleUiObjTile(int name);            // the boot's UI OBJ tiles (CC/E047-E147): names 02 03 06 07 12 13 16 17
// The bracket as a menu-sprite rebuild writes it (C1/1115 + C1/17DD) for the WRAM `w` (live feet from the host when it
// has them): returns 1 and the four entries when it is drawn, 0 when entries 0-3 are hidden or carry another sprite.
typedef struct { uint8_t x, y, name, attr; } CtBattleOamEntry;
int CtBattleBracketBuild(const CtBattleHost* h, const uint8_t* w, CtBattleOamEntry out[4]);
int CtBattleObjPal1Tail(uint16_t out[16]);             // OBJ palette 1 colours 13..15 (FF:9E0A), others 0

// The PC pose window for status class 3 (offset = cls*4 = 0x0C, the ROM's C1/2E00 `ptrA += $A440*4`).
int CtBattlePcIdleWin(int charId, int facing, uint8_t poses[4], uint8_t dwells[4]);
// Read PC E3 record `idx` (base + idx*0x28: [16 cell-words][4 signed dx,dy]) straight from ROM.
int CtBattlePcE3Rec(int charId, int idx, uint16_t cw[16], int8_t pos[8]);
// Pointer into the ROM's RAW PC sheet for 8x8 tile `src` (32 bytes), or NULL.
const uint8_t* CtBattlePcSheetTile(int charId, int src);
// The PC entry facing law (C1/350F): faceTab[atan2(nearest present+alive enemy)] on the live feet.
// Computed from the staged records when the host's staged value is absent. 0..3.
int CtBattlePcFacing(const uint8_t* mem, const CtBattleHost* h, int seat);
// The pose the PC pose-window ladder currently publishes for `seat`, or -1 when it publishes nothing
// (disarmed, an absent seat, or a window entry whose dwell byte is the 0 terminator = HOLD).
void CtBattlePcLadderArm(CtBattleHost* h, int win, int defeatArm);   // C1/34DB / C1/356D / C1/354D
void CtBattlePcLadderFallen(CtBattleHost* h, int seat);              // D1/5F42, published once and held
void CtBattlePcLadderTick(CtBattleHost* h);                          // C1/369A, one step every 2 frames
void CtBattlePcLadderWipe(CtBattleHost* h);                          // CC/F06B: the action open/close zeroes $5D80..$5E0C
// The idle-tick cursor walk, exposed for the RENDERWRAM sweep: seed the walk's state from a 128KB WRAM image
// (the seed's own $A4 / $9694 / $969C / $9907 / $9925 / $97D9 / $9799 / $974A / $5E0D / $96DE / $96E9;
// and $E5, whose value says which of the idle tick's two waits the CPU sits in) and step it one frame (the NMI's
// $A4 service + the walk on frame A of the iteration). Returns 1 on a dispatch frame.
void CtBattleIdleSeed(CtBattleHost* h, const uint8_t* w);
int  CtBattleIdleStep(CtBattleHost* h);
// CF/FC08-FC71, on the battle's exit (C1/845E -> CF/FBE5): each combatant's live foot and displayed pose ($9907)
// into its staging record, +0..+4 -- what the field's epilogue reads back (C0/080D / C0/08A0).
void CtBattleStagerRecords(CtBattleHost* h);
// One frame of the exit tail (CD/043E's C0/1BE6 upload, CD/0444): only the battle's NMI runs -- D1/E7AF's head and
// D1/E7DE's tail, whose C0/2C41 keeps drifting the field's layers on the stash.
void CtBattleTailFrame(CtBattleHost* h);
// The MOVER from a mid-battle seed (the RENDERWRAM sweep CT_BATTLE_MVSWEEP): seed every mover byte the ROM keeps
// per slot ($974A/$9752/$9786/$98AF/$98B7/$9737/$98A7/$9791/$9819/$9742/$98C2/$9897/$A5E4/$A5EC/$9829/$5E1D/
// $9831/$9839, the live feet $1D0C/$1D23 and the record extents) and step the mode machine + mover one frame.
// The mode pass C1/3714 runs on both frames of the iteration, so it has no phase to seed.
void CtBattleMoverSeed(CtBattleHost* h, const uint8_t* w);
void CtBattleMoveStep(CtBattleHost* h);
int CtBattlePcLadderPose(const CtBattleHost* h, int seat);
// Enemy E3 record read (type 0 = 4-cell/stride 0x28, type 2 = 12-cell/stride 0x78; staged rec +7 & 7).
// Returns ncell, fills cw (ncell*4 words) + pos (ncell dx,dy pairs); 0 on failure.
int CtBattleEnemyE3Rec(int id, int idx, int type, uint16_t cw[96], int8_t pos[48]);
// One 16x16 cell of a combatant's pose: its four tile words (TL, TR, BL, BR; 0 = blank, bit 14 = flip), its offset
// from the foot and its OAM name. CtBattleEnemyCells builds them for any record type (0-3) the way the ROM gathers
// (C1/1C4A), uploads (CF/E414) and names (CC:FAF3 via CF/EB94) them; returns the cell count, cell 0 in front.
typedef struct { uint16_t w[4]; int8_t dx, dy; uint8_t name; } CtEnemyCell;
int CtBattleEnemyCells(int cmb, int id, int pose, int type, CtEnemyCell* out, int max);
// One pose byte from a PC facing-row WINDOW: ptrA[cid] + facing*stride + winOff + entry (battle_facing_
// engine.md §4 -- run +0x18, recover +0x38, crit-windup +0x98, strike +0xC4, strike-follow +0xC8, idle
// +0x0C; every old baked ladder value == its facing-0/1 instance). -1 on failure/0xFF terminator.
int CtBattlePcWinPose(int charId, int winOff, int entry, int facing);
// The per-entry DWELL (frames) of that same window, from ptrB -- NO facing term (C1/42C9 vs C1/42F6).
// 0 = the window's terminator; -1 on failure.
int CtBattlePcWinDwell(int charId, int winOff, int entry);
// facing = ctBattleFaceTab[atan2((tx,ty) - (sx,sy))] (C1/0222 + C0/F700) -- the aim law shared by the
// PC entry facing, the strike re-aim, and the enemy mover stamps.
int CtBattleAimFacing(int sx, int sy, int tx, int ty);
// Enemy OBJ palette from ROM: E4:0000 + record[+2]*24 = colors 1..12 (idle cells use nothing above 12
// -- verified across Imp/Beetle/Avian/Hetake sheets). out[0]/[13..15] are zeroed. 1 on success.
int CtBattleEnemyPalRom(int id, uint16_t out[16]);
// The enemy idle WINDOW (8-byte $97D9 copy law): ptrA[classbase(CC/FB91[mode]) + facing*stride ..+7]
// poses + the ptrB dwells at the same offset. Returns 1 on success.
int CtBattleEnemyIdleWin(int id, int mode, int facing, uint8_t poses[8], uint8_t dwells[8]);

// The action's HIT SET from its target-group byte $960D (low 7 bits index the C1/1FF8 jump table) + the
// cursor PRIMARY: single-target groups -> {primary}; 0x12/0x1A -> the radius scan C1/2701; 0x08/0x0A -> all
// alive enemies. Fills out[0..n-1] with COMBATANT indices; returns n (>=1). The target-select cursor and
// the dispatch damage loop share it -- the ROM's highlight table $A62D IS the $99C0 the handler built.
int CtBattleHitSet(CtBattleHost* h, int group, int primary, int* out, int cap);
// 1 when target group $960D's handler (C1/1FF8[grp & 7F]) walks the PC side: 00 / 01 / 03 / 04, bit 7 or not.
int CtBattleGroupPcSide(int grp);
// 1 when group $960D's handler is the radius around a party member (C1/1FF8[11] / [13] / [14] / [1B]): the select
// publishes its whole set in $A62D (`$960C = 80`) and the hand lights it.
int CtBattleGroupCasterRadius(int grp);
// The enemy under the target cursor: the entry `$9614` indexes in the list C1/20A9 -> C1/2045 builds from live WRAM
// (`$96F5` / `$9FF7` / `$A0A8` / `$A09B`, combatants 3..0A in order); -1 when the index is past the list's end.
int CtBattleEnemyCursor(const uint8_t* mem);
// The per-actor target-cursor OFFSET $9708,cmb / $9713,cmb (battle_multitarget_cursor.md §1): PCs = (0,-16)
// (CC/E3C7/CC/E3CA), enemies = bytes +1/+2 of the E4/F604 + id*10 graphics record (CC/EBB0/CC/EBB7).
// The hand's origin is then (live + ofs - (0x10,0)) & 0xFF (C1/19C0-19D4 / C1/1ABA-1ACE). 0 if unavailable.
int CtBattleCursorOfs(const CtBattleHost* h, int cmb, int* dx, int* dy);
// The target hand's actors, C1/1979's `$A62D` for the select in `mem` (battle_multitarget_cursor.md): the PC-side and
// caster-radius groups read the list their select published, the other enemy groups the hit set around the cursor's
// slot. Returns the count (0: no hand -- the cursor's slot is empty); 2 or more is C1/19AC's multi arm (`$A62E` lit).
int CtBattleTargetHandSet(CtBattleHost* h, const uint8_t* mem, int* set, int cap);

// The number of characters the top box's typewriter types for the posted string ($020C over the $020D-F table).
int CtBattleMsgLen(const uint8_t* mem);
// The strip's images for the queue in `mem` as the ROM's next pass head rebuilds them (C1/0C4E: C1/1C3A + C1/0299, posted)
// -- for a render host that patches the queue of a captured WRAM.
void CtBattleHudChromeRebuild(uint8_t* mem);
// The strip's VRAM image of a live battle (CtBattleHost.stripVram: CT_BSTRIP_VRAM_SIZE bytes), what the renderer draws.
const uint8_t* CtBattleStripVram(const CtBattleHost* h);
// The same image for a WRAM-only fixture (no host, no VRAM): BG3 row 0 from $81BD and rows 7..8 the boot's (CC/E554 /
// CC/E4DC), rows 1..6 from the buffer the one-slot record names ($9EF6/$9EFC: $0CC0, $A6E1 or $0E80 -- what the NMI
// uploads, CF/E338) and the frame rows from $0B40 (CF/E384).
void CtBattleStripVramSeed(const uint8_t* mem, uint8_t* out);
// The last ticked battle host's strip VRAM image (NULL outside a fight): CT_FIELD_MEMMON's `V` regions.
const uint8_t* CtBattleStripVramLive(void);

// The battle module (the ROM's JSL C1/0000), split so the interactive host can drive it ONE frame
// per outer-loop iteration (the field stays responsive) while the headless path runs it synchronously.
//   CtBattleBegin  — the boot head: stage the battle data (stageAll) + reset the frame counter.
//   CtBattleTick   — one module frame. endRequest != 0 = the ROM's controller-2 debug instant-end
//                    (C1/8357) request; `input` = the CT_BIN_* bitmask routed from the field this frame
//                    (A/Up/Down edges + Up/Down held). Headless test hooks add their own input: MASHA
//                    (CT_BATTLE_MASHA=1) forces A every frame; SCRIPT (CT_BATTLE_SCRIPT="btn:frames,..")
//                    plays a timed button string. Ends when: endRequest, or debugEndAfter reached, or the
//                    frame cap (-> C1/8357 debugEnd, outcome 0), OR the minimal ATB loop resolves (all
//                    enemies dead -> $7F01EC=0 victory / all PCs dead -> $7F01EC=1 defeat). Fires the exit
//                    EXACTLY ONCE and returns 1 on the end frame, else 0.
//   CtBattleRun    — synchronous convenience (Begin, then loop { Tick(frame ? frame(ctx) : 0, 0) }); the
//                    input comes from CT_BATTLE_MASHA / CT_BATTLE_SCRIPT (headless). Returns glb[0x1EC].
void CtBattleBegin(CtBattleHost* h);
int  CtBattleTick(CtBattleHost* h, int endRequest, int input);
int  CtBattleRun(CtBattleHost* h);

// ---- SOUND SINK (disasm/notes/battle_sfx_ops.md + battle_sfx_ops_inputs.md) ------------------
// The battle module reaches the SPC exactly the way the field VM does: it stages a command at
// $7E:1E00-$1E02 and then needs the `JSL $C7:0004` that flushes it. That JSL is the sound
// hardware's side of the wire, so the module asks for it through this hook rather than growing an
// audio dependency -- the same shape as ct_vm.h's CtSoundSink, mirrored here. `post` MUST be
// SYNCHRONOUS (the ROM's is: the driver consumes the command inside the frame -- $1E00 reads back 00
// on the very next oracle row, writer $C7:0196's `STZ $00` teardown). With no sink installed the
// battle stages nothing and is byte-identical to the pre-sound engine, which is what every headless
// regression run does.
typedef struct {
  void*  ctx;
  void (*post)(void* ctx, uint8_t* mem7E);   // JSL $C7:0004 on the staged $1E00 mailbox
  // OPTIONAL, may be NULL: how many DSP voices are sounding right now. Staging a command and
  // POSTING it prove nothing about whether a sound came OUT -- a byte-perfect mailbox with no
  // key-on is silence, which is exactly the gap behind "no SFX on the crit's second hit".
  int  (*voices)(void* ctx);
  // OPTIONAL, may be NULL: the same JSL under THE RULE (the infinitely fast CPU, CT_ORACLE_NMION_HOLD=1 +
  // CT_ORACLE_IFAST=1; the host installs it only then). The post happens where the battle NMI's own waits left the
  // beam -- `nmiLine` = the last raster wait of this frame's service (CD/09AA's line 64, the program's op C1), 0 for none;
  // `padLatch` = the service ran the pad latch C2/8002 (the auto-joypad wait, `$7E` = 1/2) -- and the same NMI comes in
  // every frame the JSL spans. Returns the frame boundaries the JSL crossed (the main thread's stall), -1 unpriced.
  int  (*postRule)(void* ctx, uint8_t* mem7E, int nmiLine, int padLatch);
  // OPTIONAL, may be NULL (the rule only): one frame of SPC, run by CtBattleRun after each tick of a HEADLESS fight (it
  // runs inside one host frame; the live host's loop runs the SPC itself).
  void (*frameSpc)(void* ctx);
  // OPTIONAL, may be NULL: the APU out-port k ($2140+k) as the CPU reads it now -- CD/0432's `LDA $002141` poll in the
  // exit tail (CtBattleExitPoll). NULL reads 0, so a headless fight never spins on an APU that is not there.
  uint8_t (*port)(void* ctx, int k);
} CtBattleSoundSink;
void CtBattleSetSoundSink(const CtBattleSoundSink* sink);   // NULL detaches
int  CtBattleExitPoll(CtBattleHost* h);   // CD/042B-0438, after a CD/044A wait: 1 = on to CD/043E `JSL C0/000B`

#endif
