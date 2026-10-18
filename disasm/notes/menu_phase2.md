# Menu phase 2/3 RE — interactive main menu + sub-screens

## PHASE 2A IMPLEMENTATION SPEC (interactive ribbon)

═══════════════════════════════════════════════════════════════════════════════
CHRONO TRIGGER — MENU PHASE 2A: THE INTERACTIVE MAIN MENU (X-menu ribbon) — IMPLEMENTATION SPEC
Synthesis of 3 RE dossiers, every conflict re-verified this pass against the oracle
(ct_probe.exe --peek/--monitor/--read on saves/menu_open_gato2.state) and ROM (dis.py).
State facts VERIFIED this pass: story=08 ($7F0001), party=00 01 80 (Crono id0, Marle id1,
slot2 empty), loc $0100=0x006 (field), menu-mode $0A00=0x0106, screen $0067=00 (ribbon).
Scope = ONLY the main ribbon (screen 0). All six SUB-screens (screen 1..6) = PHASE 3 (§6).
═══════════════════════════════════════════════════════════════════════════════

─────────────────────────────────────────────────────────────────────────────
CONFLICT RESOLUTIONS (three dossiers cross-checked; I re-ran the oracle to break every tie)
─────────────────────────────────────────────────────────────────────────────
R1. Up/Down do NOTHING; the cursor is LEFT/RIGHT only. RE-VERIFIED: monitor $0054/$0411 with
    Up(0x10) then Down(0x20) from idx0 → ZERO change (only the f0 baseline printed under --diff).
    The recon's "Up 0x10 / Down 0x20 scroll" is WRONG. Dossiers 1 & 3 correct.
R2. Cursor RAM = TWO cells that track together. RE-VERIFIED: Right press stepped BOTH $0054 and
    $0411 0→1→2→3 in lockstep; they settle EQUAL. $0054=WORKING (per-frame scratch, reseeded from
    $0411 at C2/9A1B), $0411=COMMITTED (the persistent selection). Reconciles dossier1 ($0054)
    and dossier3 ($0411): both right, different roles.
R3. Left/Right WRAP mod 6. RE-VERIFIED: Left from idx0 → 5 → 4 (wrap); Right 0→1→2→3, and
    idx5+Right→0 (dossier3). 6 options total.
R4. A commits screen = idx+1. RE-VERIFIED: idx0+A → $0067=01 (persists), idx3+A → 04 (persists),
    idx5+A → 06 for one frame then reverts to 0 (Form/Save spawn a task). $0068 zeroed by A-handler.
R5. B on the ribbon closes the WHOLE menu. RE-VERIFIED: B(0x01) drove $0A00 0106→0000 while loc
    $0100=0600 was preserved → menu closes, field resumes.
R6. ⚠ HP/MP BARS DO NOT EXIST in this menu. RE-VERIFIED on the composite render AND BG1 layer
    isolation: HP/MP are numeric right-aligned "cur/max" only ("70/70", "8/8"); no bar primitive on
    any layer (BG1=text, BG2=chrome/marble, BG3=blank, OBJ=ribbon). The task's "HP/MP bars (geometry
    + fill formula)" premise has NO oracle basis in the mode-1 field menu. Per the no-invention
    mandate: DO NOT add a proportional bar — that would be INVENTION. (An in-battle HUD is a different
    module, out of phase-2A scope.)
R7. ⚠ There is NO right-edge "option label" widget. RE-VERIFIED: the vertical letters the recon read
    at the right edge ("P H M M E N" reading down, seen clearly under BG1 isolation) are the FIRST
    LETTERS of the right STATUS panel's stat column (PWR/HIT/MAG/M.DEF/EXP/NEXT) peeking out past the
    floating ribbon box — NOT a per-option label. The option labels (ITM, TEC, …) are baked into the
    icon sprite bitmaps. DO NOT implement a right-edge option-name label.
R8. The ribbon is a FIXED 6-icon row with a MOVING cursor, NOT a scrolling window. RE-VERIFIED via
    OAM at idx0/idx1/idx5: the six icon sprites stay pinned at X=80,96,112,128,144,160; only the
    selected icon's tile (+0x20) and the two arrow sprites' X move. (The "only 3 icons visible" look
    is the purple BG2 box clip; all six sprites are live.)

─────────────────────────────────────────────────────────────────────────────
1. THE OPTION LIST (ordered) + SELECTABILITY AT STORYLINE 08
─────────────────────────────────────────────────────────────────────────────
Six options, index 0..5, left→right (order read directly from the oracle, NOT assumed):
  idx0  EQUIP/STATUS  icon=framed PC face   → A: $0067 := 1  (character detail: Status view + Equip)
  idx1  ITEM          icon="ITM"            → A: $0067 := 2
  idx2  TECH          icon="TEC"            → A: $0067 := 3
  idx3  CONFIG        icon=grid/screen      → A: $0067 := 4
  idx4  FORM          icon=two curved arrows→ A: $0067 := 5  (party-order/exchange, task-spawned)
  idx5  SAVE          icon=pencil/quill     → A: $0067 := 6  (file slots, task-spawned)
There is NO separate "Status" icon — character status is the read-only face of the idx0 screen.
SELECTABLE AT STORYLINE 08: ALL SIX. RE-VERIFIED every option responds to A (idx0→$0067=01,
idx3→04, idx5→06 all fired). There is NO per-option enable/grey mask on the ribbon in this state; the
ribbon never disables an entry. (Any real-world Save gating — save-point-only rules — is enforced
INSIDE the Save screen by location, which is phase-3 Save territory, not a ribbon-level disable.)

─────────────────────────────────────────────────────────────────────────────
2. SELECTION / CURSOR STATE our C menu must maintain
─────────────────────────────────────────────────────────────────────────────
Canonical cells (menu runs D=$0000, DB=$7E):
  $0411  = COMMITTED selection index 0..5  ← MODEL THIS as the one persistent "current option".
  $0054  = WORKING index (per-frame scratch): C2/9A1B does `LDA $0411 / STA $54` at the top of every
           idle frame; the mover writes the new neighbor to $54; on a detected move C2/9A45 does
           `STA $0411` to commit. Settles == $0411.
  $0081  = render shadow, `LDA $54 / STA $81` at C2/E9E7 (1-frame lag; the draw task reads it).
FOR THE C PORT (single-threaded — collapse the coroutine dance): keep ONE `menuOpt` (0..5). The
render reads it directly (no shadow needed). Movement, byte-exact to the observed behavior:
  RIGHT edge  → menuOpt = (menuOpt + 1) % 6
  LEFT  edge  → menuOpt = (menuOpt + 5) % 6      (wrap both ways)
  UP / DOWN   → no-op
This modulo is provably identical to the ROM's table-driven mover for the 6-in-a-row layout: the ROM
looks up a per-option neighbor graph at $7E9380 (6 bytes/opt: +2 Up,+3 Dn,+4 Lf,+5 Rt) where every
option's Lf/Rt = ±1 mod 6 and Up/Dn = self; dispatch is C2/E9E5 (LSR-scan $00F5 → $0D1E: bit0=Right,
bit1=Left, bit2=Down, bit3=Up) → jump table C2/EA1F=[EA45 Rt,EA40 Lf,EA3B Dn,EA36 Up] → all STA $54.
INPUT semantics: moves fire on the button EDGE (SNES auto-joypad is pipelined 1 frame — the known
runCheckJoy gotcha). On a move the ROM also plays cursor tick SFX #74 (C2/9A6B `LDA #74 / JSR
C2/8255`) + rebuilds the hover preview (C2/9B54/EAC2). Sound is optional for the port; note it.

─────────────────────────────────────────────────────────────────────────────
3. RENDER ADDITIONS (on top of the PC panels + TIME/G + portrait our menu already draws)
─────────────────────────────────────────────────────────────────────────────
Per menu_entry.md our menu already draws the 4 left windows (Crono/Marle/empty/TIME-GOLD), the
numeric HP/MP, and the portrait. Phase 2A ADDS the ribbon. All positions/tiles below are the LITERAL
OAM the PPU renders (verified byte-for-byte from oam0/oam1/oam5 dumps) — reproducing this OAM makes
the OBJ layer byte-identical to the oracle.

(A) SIX COMMAND ICONS — OBJ sprites, all identical attrs, fixed positions:
    size = 1 (16×16), prio = 3, OBJ palette = 0 (CGRAM 128–143), Y = 112 for all six.
    X       = 80, 96, 112, 128, 144, 160   (16-px pitch)
    baseTile= 004,006, 008, 00A, 00C, 00E
    SELECTED-ICON HIGHLIGHT: the selected slot draws baseTile + 0x20. VERIFIED: idx0→tile 024,
    idx1→tile 026, idx5→tile 02E; all non-selected slots keep their base tile.

(B) TWO CURSOR ARROWS — OBJ sprites straddling the selected icon vertically:
    tile = 022 (both), OBJ palette = 7 (CGRAM 240–255), prio = 3, size = 1.
    X    = 81 + menuOpt*16   (i.e. 81,97,113,129,145,161 for opt 0..5 — VERIFIED at idx0/1/5)
    ▼ (above icon): Y = 96,  attr vflip=1   (points down at the icon)
    ▲ (below icon): Y = 128, attr vflip=0   (points up  at the icon)
    Together they sandwich the 16-px icon (Y112–127). This IS the "up/down arrows" the recon saw —
    it is the SELECTION MARKER, not an Up/Down affordance.

(C) BG2 PURPLE RIBBON BOX (chrome behind the icons): BG2 map $6000 rows ~12–17 cols ~6–22, tiles
    $3A4–$3AC ($3AC = solid purple fill; $3A4/5/6 top, $3A7/AC/8 mid, $3A9/AA/AB bottom). Screen ≈
    x72–184 / y96–140 behind the icon row. These chrome tiles are NOT raw in ROM (compressed, built by
    the C2/8096 init chain) → reproduce by CAPTURE from BG2 chr $0000 VRAM (same "capture, don't
    derive" verdict menu.c already reached for window chrome).

(D) GRAPHICS SOURCES:
    • Arrow tile 022 = ROM FF/9D70 UNCOMPRESSED (byte-match; identical triangle to the name-entry
      cursor — the port already has this asset).
    • The six icon tiles (004/006/008/00A/00C/00E and their +0x20 highlight variants) are NOT raw in
      ROM (compressed/dynamically built) → CAPTURE from OBJ VRAM chr $4000, or trace the menu-init DMA.

(E) DO NOT ADD (verified non-existent / misread — see R6/R7):
    • NO HP/MP proportional bars. HP/MP stay numeric cur/max (already drawn).
    • NO right-edge option label. (The "PHMMEN" is the status-panel stat column peeking past the box.)

(F) OPTIONAL FIDELITY the current menu.c is genuinely missing (from dossier 2, not required for 2A):
    a 4th per-panel EQUIP row [weapon-icon $20/$21][weapon-Atk][armor-icon $2E][armor-Def]
    (Crono "\ 8 [box] 16", Marle "X 8 [box] 14") — equipment-derived; note as an enhancement.

─────────────────────────────────────────────────────────────────────────────
4. HOW TO VALIDATE PHASE 2A AGAINST THE ORACLE
─────────────────────────────────────────────────────────────────────────────
States (layout-identical; cross-check): saves/menu_open_gato2.state (primary), saves/menu_open.state,
saves/menu_open_fair2.state.
Drive with ct_probe.exe --peek/--monitor + LakeSnes button grammar (Right=0x80, Left=0x40, Up=0x10,
Down=0x20, A=0x100, B=0x01; script 'hexbtn:frames,...'; edge needs a press then release).

RIBBON-AT-EACH-OPTION signatures (the exact oracle values I captured this pass):
  • RAM after settle at option N: $0054 == $0411 == N ; $0067 == 0 ; $0068 == 0.
    Right-step scripts confirmed 0→1→2→3 and Left 0→5→4 (wrap). Up/Down: no change.
  • OAM signature at option N (compare our emitted OAM slots 0–7):
       slots0–5: X=80+i*16,Y=112,prio3,pal0,size1, tile = baseTile[i] (+0x20 iff i==N)
       slot6: X=81+N*16, Y=128, tile 022, pal7, vflip=0    (▲)
       slot7: X=81+N*16, Y= 96, tile 022, pal7, vflip=1    (▼)
    Repro per index: --peek gato2 out.raw --btnscript "0:2,80:2,0:2,...(N times)...,0:8" then
    `python tools/ct.py oam out.raw.oam`.
  • Layer isolation: PEEK_ISOBG=4 --peek → OBJ-only render must match the ribbon (6 icons + 2 arrows,
    everything else parked at Y=224). PEEK_ISOBG=0 → BG1 text must match panels (no bars).

TRANSITION signatures:
  • A at option N: $0067 becomes N+1. Persists for N=0..3; transient (1 frame then back to 0) for
    N=4 (Form) and N=5 (Save). $0068 → 0. (VERIFIED idx0→01, idx3→04, idx5→06.)
  • B on the ribbon: $0A00 goes 0x0106 → 0x0000 (menu closes) while loc $0100 stays 0x0600.
Handler citations (for the C port): ribbon idle C2/9A1B (`LDA $0411/STA $54; JSR C2/E984 action decode;
BIT $0D1D; BMI→9A77 A; BVS→9A95 B; CMP $0411/BNE→9A45 commit`); A-handler C2/9A77 (`…STA $67 / STZ
$68`, confirm SFX #BD51 via C2/ED31); B-exit C2/9A95→JMP C2/82B2→C2/82B4 (fade, party-gfx rebuild bank
D1, `LDX $0920;TXS` restore field stack, RTL → field). Action decoder C2/E984 sets $0D1D bit7=A/bit6=B.

─────────────────────────────────────────────────────────────────────────────
5. PANEL DATA our menu reads (all byte-VERIFIED this pass — unchanged from prior menu.c)
─────────────────────────────────────────────────────────────────────────────
Char sheet $7E2600 + id*0x50: +3/4 curHP, +5/6 maxHP, +7/8 curMP, +9/A maxMP, +0x12 level (LE).
  Crono $2600 = 00 86 80 46 00 46 00 08 00 08 00 05 08 0C 05 08 08 02 01 00 → HP 70/70, MP 8/8, LV1. ✓
  Marle $2650 = 01 2A 80 41 00 41 00 0C 00 0C 00 …                          → HP 65/65, MP 12/12, LV1. ✓
Active party $7E2980-82 = 00 01 80 (bit7=empty slot). Gold $7E2C53 = 90 01 00 = 400. Clock $7E0400:
HH=$0405*10+$0404, MM=$0403*10+$0402 → 08:28 (render showed 08:29 after the peek advanced ~1 min).

─────────────────────────────────────────────────────────────────────────────
6. DEFERRED TO PHASE 3 (each sub-screen — DO NOT build in 2A)
─────────────────────────────────────────────────────────────────────────────
Everything reached via $0067 != 0 is phase 3. Phase 2A stops at "A sets $0067=idx+1 / $0068=0" and
"B (sub) returns via C2/9A98 STZ $0067". Deferred screens (input-handler-array C2/8292 → per-screen):
  $0067=1  Equip/Status  (handler C2/9C29)  — char sheet $2600+id*0x50; Status view + Equip change.
  $0067=2  Item          (handler C2/AA34)  — inventory $2400 ids / $2500 counts, names ROM bank $CC.
  $0067=3  Tech          (handler C2/B353)  — char sheet + learned-tech bitfield + tech tables.
  $0067=4  Config        (handler C2/BF35)  — settings bitfield (address unpinned).
  $0067=5  Form          (handler C2/C621)  — party $2980-82 + reserve roster; task-spawned overlay.
  $0067=6  Save          (handler C2/CED4)  — SRAM slots (banks $70/$F0); task-spawned overlay.
Also deferred: the shared action decoder C2/E984 (already needed in 2A for A/B only), the two-level
input dispatch C2/8279-828E via table C2/8292, and the sub-screen SFX/redraw (C2/9A98, C2/8255).

─────────────────────────────────────────────────────────────────────────────
7. RECOMMENDED PHASE-3 SUB-SCREEN ORDER (easiest / most-valuable first)
─────────────────────────────────────────────────────────────────────────────
1. STATUS (screen 1, read-only view) — CLEANEST FIRST TARGET: a pure format of the byte-verified
   $2600 char sheet (HP/MP/LV/stats/XP + portrait table bank FF). No interaction, no writes.
2. ITEM (screen 2) — data model FULLY pinned: parallel arrays $2400 (ids, 0-term) / $2500 (counts),
   builder C2/ADD0 (cap 0xF2, pagination C2/EA81), names ROM $CC (Pendant id 0xD6 @ $CC/1491). Adds a
   scroll list + Use(target PC)/Move(swap both arrays, C2/AAE3) interactions.
3. EQUIP (screen 1, interactive) — same $2600 sheet + inventory ($2400/$2500) filtered by slot type;
   adds stat-delta preview + apply. Builds on 1 and 2.
4. TECH (screen 3) → CONFIG (screen 4) → FORM (screen 5) → SAVE (screen 6, SRAM read/write, hardest).

═══════════════════════════════════════════════════════════════════════════════
PHASE-2A DEFINITION OF DONE: our C menu draws the 6-icon ribbon + selection highlight (+0x20) + the
two straddling arrows at 81+opt*16 over the existing panels; Left/Right cycle the option 0↔5 with
wrap; Up/Down inert; A commits screen=opt+1 (+ zero substate) and B closes the menu — validated by the
OAM/RAM signatures in §4 on the three menu_open states. No HP/MP bars, no right-edge label (§R6/R7).
═══════════════════════════════════════════════════════════════════════════════

### Key addresses
- $7E0411 = COMMITTED ribbon option index 0..5 (model THIS as the persistent selection; Right=(i+1)%6, Left=(i+5)%6, Up/Down no-op, wrap) — VERIFIED tracks 0→1→2→3 / 0→5→4
- $0054 (DP $54) = WORKING index: reseeded `LDA $0411/STA $54` each idle frame at C2/9A1B, committed back `STA $0411` at C2/9A45 on a move; settles == $0411
- $0081 (DP $81) = 1-frame-lag render shadow of the index, `LDA $54/STA $81` at C2/E9E7 (draw task reads it; C port can skip and read the index directly)
- $7E0067 = SCREEN id: 0=ribbon (phase 2A); A sets := opt+1 (1 Equip/2 Item/3 Tech/4 Config/5 Form/6 Save) — VERIFIED idx0→01,idx3→04,idx5→06
- $7E0068 = SCREEN substate: A-handler zeroes it (STZ $68 @ C2/9A83)
- $0A00 = menu-mode word = 0x0106 while open; B on ribbon clears it to 0x0000 (menu closes) — VERIFIED, loc $0100=0x0600 preserved
- $7E9380 = nav-graph table, 6 bytes/opt (+2 Up,+3 Dn,+4 Lf,+5 Rt neighbor); Lf/Rt=±1 mod6, Up/Dn=self — behaviorally == mod-6 for this row
- $0D1D = decoded action flags from C2/E984: bit7=A/confirm, bit6=B/cancel
- $0D1E = decoded direction (0=Right,1=Left,2=Down,3=Up), set by C2/E9E5 from edge byte $00F5 (bit0 R/bit1 L/bit2 D/bit3 U)
- C2/9A1B = per-frame ribbon idle handler (seed $54←$0411; JSR E984; BMI→9A77 A / BVS→9A95 B; CMP $0411/BNE→9A45 commit)
- C2/9A45 = STA $0411 commit-on-move (+ cursor SFX #74 via C2/8255, preview rebuild 9B54/EAC2)
- C2/9A77 = A/CONFIRM handler: confirm SFX #BD51 (C2/ED31); STA $0067 (=opt+1 @ C2/9A81); STZ $0068 (C2/9A83)
- C2/9A95 = B/CANCEL on ribbon → JMP C2/82B2 → C2/82B4 exit (fade; party-gfx rebuild bank D1; LDX $0920;TXS restore field stack; RTL→field C0/1964)
- C2/9A98 = SUB-screen B-return (STZ $0067 back to ribbon; $0068=1; SFX #74; redraw) — PHASE 3
- C2/E984 = shared A/B action decoder → $0D1D (used in 2A for confirm/cancel only)
- C2/E9E5 = directional cursor mover (mirror $54→$81; read $00F5; dispatch via jump table C2/EA1F=[EA45 Rt,EA40 Lf,EA3B Dn,EA36 Up]→STA $54)
- OAM ICON RIBBON (fixed row): 6 sprites Y=112, X=80/96/112/128/144/160, prio3, OBJ-pal0, size1(16x16), baseTile 004/006/008/00A/00C/00E; SELECTED slot tile = base+0x20 (VERIFIED 024/026/02E)
- OAM CURSOR ARROWS: 2 sprites tile 022, OBJ-pal7, prio3, size1, X=81+opt*16; Y=96 vflip=1 (▼ above) + Y=128 vflip=0 (▲ below) straddling the selected icon — VERIFIED X 81/97/161 at opt 0/1/5
- Arrow tile 022 gfx = ROM FF/9D70 UNCOMPRESSED (byte-match, = name-entry cursor; port already has asset); the 6 icon tiles + BG2 ribbon-box chrome $3A4-$3AC are compressed → CAPTURE from OBJ VRAM chr $4000 / BG2 chr $0000
- Panel data (VERIFIED bytes): char sheet $7E2600+id*0x50 (+3/4 curHP,+5/6 maxHP,+7/8 curMP,+9/A maxMP,+0x12 level); party $7E2980-82=00 01 80; gold $7E2C53=0x190=400; clock $7E0400 (HH=$0405*10+$0404, MM=$0403*10+$0402 =08:28); story $7F0001=08

### Open questions
- HP/MP BARS: verified ABSENT in the mode-1 field menu (numeric cur/max only; layer isolation shows no bar on any layer). The task's 'fill formula' has no oracle basis here — do NOT implement one (would be invention). Only a different module (in-battle HUD, out of scope) could have bars.
- RIGHT-EDGE OPTION LABEL: verified NOT a menu widget — the vertical 'PHMMEN' letters are the right STATUS panel's stat-column first-letters (PWR/HIT/MAG/M.DEF/EXP/NEXT) peeking past the ribbon box. Do NOT draw a right-edge option label; the ribbon icons carry baked labels (ITM/TEC).
- A-handler idx+1 arithmetic (C2/9A77-9A81) disassembles garbled due to M/X flag width, so screen=opt+1 is pinned EMPIRICALLY (idx0→01, idx3→04, idx5→06), not from a clean instruction decode. The Form/Save task-spawn call site (why $0067=5/6 revert to 0) is unconfirmed — irrelevant to 2A (they are phase 3).
- Six icon tile bitmaps (004/006/008/00A/00C/00E + their +0x20 highlight) and the BG2 ribbon-box chrome ($3A4-$3AC) are compressed/dynamic — not pinned to a ROM address; reproduce by VRAM capture (OBJ chr $4000 / BG2 chr $0000) or by tracing the C2/8096 init DMA. Only the arrow tile 022 (ROM FF/9D70) is byte-derivable.
- Cursor-move SFX (#74 via C2/8255) and A-confirm SFX (#BD51 via C2/ED31) are noted from disasm but audio is presumably out of the current C port's scope; flag whether phase 2A should emit them.
- All six options respond to A at storyline 08 with no ribbon-level disable; whether SAVE is actually functional here (CT normally gates saving to save-points/overworld) is enforced INSIDE the Save screen by location — a phase-3 Save concern, not a 2A ribbon gate.
- Per-window HDMA scroll means BG tilemap*8+scroll does NOT linearly map to screen; absolute BG positions (panels, box) must come from the framebuffer, but the OBJ ribbon is exempt — OAM X/Y ARE the literal render coords (validate the OBJ layer by matching OAM directly).


---

## TRACE: menu_ribbon_cursor

═══════════════════════════════════════════════════════════════════════════════
CHRONO TRIGGER — MAIN-MENU OPTION RIBBON + CURSOR/SELECTION (RE dossier)
State: saves/menu_open_gato2.state (mode1 main screen $0A00=0106, loc $0100=006).
All claims cite a ROM disasm addr (tools/dis.py, flat C2 offset = 0x2xxxx) or an
observed oracle value (ct_probe.exe --peek/--monitor, LakeSnes button grammar).
Every capture cross-checked on menu_open_fair2.state (identical → NOT scene-gated).
═══════════════════════════════════════════════════════════════════════════════

──────────────────────────────────────────────────────────────────────────────
0. ⚠ CORRECTION TO PRIOR RECON: Up/Down do NOTHING; the ribbon is LEFT/RIGHT.
──────────────────────────────────────────────────────────────────────────────
The recon claim "Up 0x10 / Down 0x20 scroll the option icons" is WRONG. Pressing
Up (0x10) or Down (0x20) leaves the OAM ribbon byte-identical (verified: rib_up /
rib_down captures == rib_base). The cursor moves ONLY with LEFT (0x40) / RIGHT
(0x80). This is confirmed structurally: in the navigation table every option's
Up- and Down-neighbor is ITSELF (see §1). The up/down TRIANGLE arrow sprites are
just the cursor marker straddling the selected icon; they are not Up/Down affordances.

──────────────────────────────────────────────────────────────────────────────
1. THE OPTION SET, ORDER, AND NAVIGATION GRAPH (6 options, horizontal, wrapping)
──────────────────────────────────────────────────────────────────────────────
Six options, index 0..5, laid out left→right. Icons are OBJ sprites (16×16, size1,
prio3, OBJ-pal0) at Y=112, X=80,96,112,128,144,160; base tiles 004,006,008,00A,
00C,00E. The SELECTED icon's tile is highlighted by += 0x20 (e.g. idx0 shows 024,
idx5 shows 02E). Two cursor-arrow sprites (tile 022, OBJ-pal7, prio3): down-arrow
at (X = 81+idx*16, Y=128), up-arrow (v-flipped) at (X = 81+idx*16, Y=96). Verified
arrow X across idx0..5 = 81,97,113,129,145,161 (OAM slots 6,7).

Decoded icon graphics (OBJ VRAM byte 0x8000 + CGRAM) + the sub-screen each A opens:
  idx0  icon=framed face/helm   → A opens EQUIP  (sub-screen shows "Weapon" slot on a PC)
  idx1  icon="ITM"              → A opens ITEM   (banner "ITEM / Use/Move", item "Pendant")
  idx2  icon="TEC"              → A opens TECH   (banner "Single", "MP Used")
  idx3  icon=little screen/grid → A opens CONFIG (settings grid, "Stereo On")
  idx4  icon=two circular arrows→ A opens FORM/party-order (party panels + green swap icon)
  idx5  icon=pencil/quill       → A opens SAVE   (numbered file slots "1 2 3 …")
There is NO separate "Status" icon; character status is folded into the Equip screen
(idx0). Order is Equip, Item, Tech, Config, Form, Save — read directly from the oracle,
not assumed.

NAVIGATION IS TABLE-DRIVEN (a per-option neighbor graph in RAM, NOT hard-coded, NOT
scene-gated). Table lives at $7E9380, 6 bytes per option (built once at screen init;
byte-identical on gato2 and fair2). Record[idx] @ $7E9380+idx*6:
  +0,+1 = cursor-arrow draw param (constant 0xE300 for all six)
  +2 = Up-neighbor   +3 = Down-neighbor   +4 = Left-neighbor   +5 = Right-neighbor
Live table (verified byte-for-byte from WRAM):
  idx0: Up=0 Dn=0 Lf=5 Rt=1     idx3: Up=3 Dn=3 Lf=2 Rt=4
  idx1: Up=1 Dn=1 Lf=0 Rt=2     idx4: Up=4 Dn=4 Lf=3 Rt=5
  idx2: Up=2 Dn=2 Lf=1 Rt=3     idx5: Up=5 Dn=5 Lf=4 Rt=0
→ Left/Right cycle 0↔5 with WRAP; Up/Down map to self (inert). Every capture matches:
  Right from 0→1, Left from 0→5(wrap), Down/Up from 0→0 (no move).

──────────────────────────────────────────────────────────────────────────────
2. SELECTION / CURSOR RAM (menu runs D=$0000, DB=$7E — set at C2/81C1-81C9)
──────────────────────────────────────────────────────────────────────────────
Authoritative selection index (values 0..5, tracked exactly across idx0..5 captures):
  $0054  (= direct-page $54)  WORKING option index. Live cursor. Updated by the
                              direction handlers (§3). THIS is "the current selection".
  $0411                       COMMITTED index. Seeded into $54 on entry (C2/9A1B),
                              and re-written from $54 on a detected move (C2/9A45),
                              which also fires the cursor "beep" + rebuilds the hover
                              preview. Equals $54 when settled.
  $0081  (= direct-page $81)  RENDER SHADOW of $54, copied every frame at C2/E9E7
                              (`LDA $54; STA $81`) — lags $54 by one frame; used by
                              the draw task. (writer-PC C2:E9E9 seen every frame)
  $0055  (= DP $55)           scratch copy of $54 taken just before the move dispatch
                              (C2/E9E5: `LDA $54; STA $55`).
  $0AFC  (observed = idx*6)   a coroutine-stack copy of the Y register (index*6),
                              writer C2:821F (scheduler yield) — NOT authoritative;
                              the real idx*6 is computed on demand by C2/EA27.
CURSOR POSITION is DERIVED, not separately stored: the arrow sprites sit at
X = 81 + $54*16 and the selected icon is highlighted (+0x20). The index IS the cursor.

Writer PCs proving the above (--monitor 0054:1,0081:1,0411:1 --writer, Right then Left):
  Right edge: $0054 ← C2:EA4A (neighbor store), $0411 ← C2:9A48 (commit, STA @9A45)
  Left  edge: $0054 ← C2:A07A (last writer; $54 is a shared scratch so this PC is a
              reuse — the canonical mover is the $7E9380/EA36-EA48 path, see §3),
              $0411 ← C2:9A48
  Idle:       $0054 ← C2:9A20/9BA2 (re-seed same value), $0081 ← C2:E9E9 (shadow)

──────────────────────────────────────────────────────────────────────────────
3. HANDLER ADDRESSES — input decode, cursor mover, A, B
──────────────────────────────────────────────────────────────────────────────
Per-frame ribbon handler entry: C2/9A1B (reached via task RTS-trampoline / JMP; it
is NOT JSR'd anywhere — 0 callers of 20 1B 9A). Flow:
  C2/9A1B  LDA $0411 / STA $54          ; seed working index from committed
  C2/9A20  JSR C2/E984                  ; ACTION decoder → sets $0D1D flags
  C2/9A23  BIT $0D1D
  C2/9A26  BMI ->C2/9A77                ; $0D1D bit7 = A/CONFIRM
  C2/9A28  BVS ->C2/9A95                ; $0D1D bit6 = B/CANCEL
  C2/9A2A  LDA $54 / CMP $0411 / BNE ->C2/9A45   ; cursor MOVED?
  C2/9A45  STA $0411                    ; commit new index
  C2/9A6B  LDA #74 / JSR C2/8255        ; play cursor "tick" sound; rebuild preview
           (JSR C2/9B54, C2/EAC2 = redraw hover panel + arrow)
  C2/9A76  RTS

ACTION DECODER  C2/E984 (shared by the ribbon AND every sub-screen — 37 JSR sites):
  reads joypad via C2/E9E2, SEP #30, STZ $0D1D, then sets $0D1D bits from $2993/$2994
  (table C2/E9D9 = 01 00 08 07) and $00F4 masked by $2999/$299A, plus $0D83→bit6.
  Net: $0D1D bit7 = confirm(A), bit6 = cancel(B). Verified empirically (A=0x100 →
  confirm branch; B=0x01 → cancel branch).

CURSOR MOVER  C2/E9E5 (directional; reached by JMP/fall-through — 0 JSR callers):
  C2/E9E5  LDA $54 / STA $81            ; render shadow (the $0081 writer)
  C2/E9EE  reads $00F5 (edge/repeat byte), LSR-loop X=0..3 to find the set direction
           bit → $0D1E. Bit order (SNES high-byte): bit0=Right, bit1=Left, bit2=Down,
           bit3=Up.
  C2/E9FE..EA17  LDA $54 / JSR C2/EA27  ; C2/EA27 computes Y = idx*6 (ASL;PHA;ASL;ADC)
  C2/EA0C  LDA $54 / STA $55
  C2/EA10  LDA $0D1E / ASL A / TAX
  C2/EA17  JSR ($EA1F,X)                ; DIRECTION JUMP TABLE
      table C2/EA1F (words) = [C2/EA45, C2/EA40, C2/EA3B, C2/EA36]
        dir0 Right → C2/EA45  LDA $9385,Y  (record +5 = Right-neighbor)
        dir1 Left  → C2/EA40  LDA $9384,Y  (record +4 = Left-neighbor)
        dir2 Down  → C2/EA3B  LDA $9383,Y  (record +3 = Down-neighbor)
        dir3 Up    → C2/EA36  LDA $9382,Y  (record +2 = Up-neighbor)
      all fall into  C2/EA48  STA $54  (writer-PC C2:EA4A) → new working index.
  So a Left/Right press looks up the neighbor index in the $7E9380 graph and stores
  it to $54; Up/Down store the same index (self) = no visible move.

A / CONFIRM handler  C2/9A77:
  C2/9A77  LDX #$BD51 / JSR C2/ED31     ; confirm "chime" sound
  C2/9A7D  LDA $0411 / INC A / STA $67  ; ★ SCREEN id = option index + 1
  C2/9A83  STZ $68                      ; screen substate = 0
  C2/9A86  LDX #9DF0 / LDY #9580 / LDA #1F / MVN $7E,$FF   ; copy 0x1F bytes
           $FF9DF0→$7E9580 (next-screen scratch template)
  C2/9A91  STA $0D17 / RTS
  → $0067 is the ACTIVE-SCREEN selector; initial value comes from C2/8076
    (`LDX $0A00; LDA $C2808F,X; STA $67`; mode table C2/808F = 0E 00 0C 0D 0F 07 0B;
    mode1 → screen 00 = the ribbon). A on option N drives $67 to N+1.

B / CANCEL handler  C2/9A95:
  C2/9A95  JMP C2/82B2  → the menu EXIT routine (C2/82B4 fade/rebuild/`LDX $0920;TXS`
           RTL back to the field at C0/1964). Verified: B on the ribbon clears the
           menu-mode word $0A00 (0106→0000 at f17) while loc $0100=0006 is preserved
           → menu closes, field resumes.
  Sub-screen "back to ribbon" (B inside Item/Equip/…): C2/9A98 STZ $67 / LDA #01 STA
           $68 / LDA #74 JSR C2/8255 (cursor sound) → returns to screen 0.

──────────────────────────────────────────────────────────────────────────────
4. PER-OPTION A-ACTION (does A open a sub-screen, or prompt character-select first?)
──────────────────────────────────────────────────────────────────────────────
Mechanically A always sets $0067 = idx+1 (C2/9A77). What that screen shows (from
--peek renders + --monitor 0067):
  opt0 EQUIP  → $67=01, PERSISTS. Immediately shows a HAND CURSOR on a PC panel
               (character-select FIRST; Up/Down move Crono↔Marle). Picking a PC
               (A again) opens that PC's equipment page ("Weapon" slot, stats). This
               is the "hand on a PC" the recon saw.
  opt1 ITEM   → $67=02, PERSISTS. Opens the ITEM LIST directly (no char-select),
               cursor on first item ("Pendant"); banner "ITEM / Use/Move". (Char-
               select happens later, only when you USE an item.)
  opt2 TECH   → $67=03, PERSISTS. Character-select FIRST (hand on Crono) + "Single"
               tech list + "MP Used".
  opt3 CONFIG → $67=04, PERSISTS. Opens the settings GRID directly (no char-select),
               cursor on first setting ("Stereo On").
  opt4 FORM   → $67=05, then returns to $67=0. Party-panel screen with the green
               swap-arrows icon; A did NOT rotate the party ($7E2980-82 = 00 01 80
               unchanged), i.e. it is a member-select/swap screen you must complete,
               not an instant action.
  opt5 SAVE   → $67=06, then returns to $67=0. Numbered file-slot list ("1 2 3 …").
Summary: EQUIP and TECH prompt character-select immediately; ITEM, CONFIG, SAVE open
their screen directly; FORM opens a party/member screen. (Screens 05/06 flip $67 back
to 0 the next frame and their renders freeze mid-wipe under the headless harness — see
open questions; identity from icon + partial render is high-but-not-interactive-proven.)

──────────────────────────────────────────────────────────────────────────────
5. RENDER/SCHEDULER CONTEXT (for the C port)
──────────────────────────────────────────────────────────────────────────────
Mainloop C2/8190 = cooperative scheduler (dossier menu_entry.md): spawns input task
C2/826F and render task C2/E91B; frame flag $7E0D01. Ribbon draw = C2/E92D: reads
$54, JSR C2/EA27 (Y=idx*6), `LDA $9380,X → $0770` (arrow param) + `$58 → $0772`,
then builds the 6 icon sprites (selected += 0x20) and the two tile-022 arrows. Only
~8 live OAM sprites total on the main screen (rest parked at Y=224). No horizontal
scroll: the six icon slots are fixed; the window BG frame just clips the view.
═══════════════════════════════════════════════════════════════════════════════

### Key addresses
- $0054 (DP $54) = WORKING option index 0..5 (the live cursor / current selection)
- $0411 = COMMITTED option index (seeded→$54 on entry C2/9A1B; re-written from $54 on move at C2/9A45)
- $0081 (DP $81) = render shadow of the index, copied `$54→$81` at C2/E9E7 (1-frame lag)
- $0055 (DP $55) = scratch copy of $54 taken before the move dispatch (C2/E9E5)
- $7E9380 = navigation-graph table, 6 bytes/option: +0/+1 arrowParam(0xE300), +2=Up +3=Down +4=Left +5=Right neighbor index
- $0067 = active-screen selector (A sets = option index+1); initial = C2/808F[mode] via C2/8076
- $0068 = screen substate (STZ by A-handler C2/9A83)
- $0D1D = decoded action flags (bit7=A/confirm, bit6=B/cancel), set by C2/E984
- $0D1E = decoded direction (0=Right,1=Left,2=Down,3=Up), set by C2/E9E5 from edge byte $00F5
- $00F5 = joypad edge/repeat byte read for direction (bit0 R, bit1 L, bit2 D, bit3 U)
- C2/9A1B = per-frame ribbon handler (seed $54←$0411; JSR E984; detect+commit move)
- C2/9A45 = STA $0411 commit-on-move (+ cursor sound #74 via C2/8255 + preview rebuild 9B54/EAC2)
- C2/9A77 = A/CONFIRM handler: sound #BD51 (C2/ED31); $0067 = $0411+1; $0068=0
- C2/9A95 = B/CANCEL handler: JMP C2/82B2 (menu exit → RTL to field C0/1964)
- C2/9A98 = sub-screen→ribbon back ($67=0,$68=1, cursor sound #74)
- C2/E984 = shared action/button decoder → $0D1D (37 call sites: ribbon + every sub-screen)
- C2/E9E5 = directional cursor mover (mirror $54→$81; read $00F5; dispatch)
- C2/EA27 = index→Y (Y = index*6)
- C2/EA1F = direction jump table = [EA45 Right, EA40 Left, EA3B Down, EA36 Up]
- C2/EA36/EA3B/EA40/EA45 = Up/Down/Left/Right neighbor-read handlers (LDA $9382..$9385,Y) → C2/EA48 STA $54
- C2/8076 = initial screen: LDX $0A00; LDA $C2808F,X; STA $67 (mode table 0E 00 0C 0D 0F 07 0B)
- C2/8190 = menu mainloop / cooperative task scheduler (input C2/826F + render C2/E91B; ribbon draw C2/E92D)

### Open questions
- Screens 05 (Form/party-order) and 06 (Save) flip $0067 back to 0 the next frame and their renders freeze mid-wipe under the headless --peek harness; their full persistent behavior (and whether Save is gated off in a field map, as CT normally restricts saving to save-points/overworld) was not interactively drivable. Identities (Form via swap-arrows icon + party render; Save via pencil icon + '1 2 3' file-slot render) are high-confidence but not completed interactively.
- opt4 (Form) exact function — party-member reorder vs. some other swap — inferred from the two-circular-arrows icon + party-panel screen; A did NOT change $7E2980-82, so it is a member-select screen you must complete, but the completion flow was not exercised.
- opt0 EQUIP vs a distinct STATUS: the sub-screen shows a 'Weapon' slot on the selected PC (equip), and there is no separate Status icon in the ribbon; the weapon/armor/accessory slot-editing itself was not exercised, so the exact Equip-vs-Status labeling is from the 'Weapon' slot header only.
- Call-graph entry of the ribbon handler C2/9A1B and the cursor mover C2/E9E5: neither is JSR'd anywhere (0 sites for 20 1B 9A and 20 E5 E9), so they are reached by JMP / fall-through / task RTS-trampoline — the exact per-frame invocation order of E984 (action) vs E9E5 (direction) within the screen-0 task was not fully traced (the writers/addresses are pinned, the sequencing is not).
- The full $0067→screen-handler pointer table (entries for screens 00..0F, reached both via A=idx+1 and via the mode table C2/808F) was not located; only the initial-screen source (C2/8076 reading C2/808F[mode]) and the A-handler's $67=idx+1 are pinned.
- Navigation record +0/+1 = constant 0xE300 (arrow draw param → $0770); its exact bitfield meaning (OAM attr vs. position) is undecoded. record +4/+5 in the raw dump correspond to the NEXT record's +0/+1 — confirmed by re-basing to $7E9380; harmless but worth a note when porting.
- $0AFC was observed to equal index*6 with writer C2:821F (scheduler yield) — treated as a coroutine-stack copy of Y, NOT an authoritative selection cell; not independently confirmed as such.

---

## TRACE: menu_layout_widgets

CT MAIN-MENU LAYOUT RE DOSSIER (mode-1 field menu, screen $0067=00). All positions in NATIVE 256x224 measured from the oracle render of saves/menu_open_gato2.state via ct_probe.exe --peek (fb 512x480, 2x-downsampled). Every layer/tile claim is from PEEK_ISOBG layer-isolation, --ppuregs, VRAM-tilemap decode, OAM decode, or a ROM byte-for-byte match. menu_open.state is byte-identical in layout (cross-check); menu_open_fair2.state = same layout, different loc/clock context. SP = scratchpad dir. Artifacts: SP/mlay_gato.raw(+.wram/.vram/.oam), iso_bg1/bg2/bg3/obj.png, bgrec_bg1.png, z_*.png.

==================================================================
0. SCREEN COMPOSITION (which layer draws what) — PEEK_ISOBG + --ppuregs
==================================================================
--ppuregs menu_open_gato2: mode=1, brightness=15. BG1 map $5800/chr $0000; BG2 map $6000/chr $0000; BG3 map $6800/chr $7000; OBJ chr $4000/$5000, objSize=0. (BG1/BG2 are 4bpp in mode 1; BG3 is 2bpp.)
Layer isolation (PEEK_ISOBG=0/1/2/4 -> iso_bg1/2/3/obj.png) DEFINITIVELY assigns roles:
 - BG1 (chr $0000, 4bpp) = ALL MENU TEXT (names, LV, HP/MP labels+numbers, the 4th equip row, the entire right status/equipment panel, TIME + GOLD) AND the 48x48 status PORTRAIT tile-block ($201-$224). 282 nonzero map cells.
 - BG2 (chr $0000, 4bpp) = the WINDOW CHROME (beveled frames), the full-screen MARBLE background, AND the floating command-ribbon purple box. Tiles $3A0-$3EF.
 - BG3 (chr $7000) = draws NOTHING visible (isolation = black); its map $6800 has only 24 nonzero cells. NOTE/CORRECTION to menu_entry.md: the font glyphs ARE uploaded to chr $7000 too, but the main-screen text renders on BG1 (chr $0000), not BG3. So "BG3 chr 7000 = menu font" is only half-true — the visible text is BG1.
 - OBJ (chr $4000, 4bpp) = the 6 command icons + 2 scroll arrows of the ribbon (8 live sprites). Portraits and all windows are BG, not sprites.
Font glyph index == CT charset code (same table the name-entry keyboard uses, already ported as CtGetName/drawGlyph): 'A'=$A0..'Z'=$B9, 'a'=$BA.., '0'=$D4..'9'=$DD, '/'=$E0, ':'=$E3, '.'=$E8, space=$FF. Verified by decoding the BG1 tilemap: row of "Crono" = A2 CB C8 C7 C8 ('C''r''o''n''o'), "HP 70/70" = A7 AF .. DB D4 E0 DB D4, "LEVEL" = AB A4 B5 A4 AB.

==================================================================
1. PC PANELS (left column) — and THE GAUGE-BAR ANSWER
==================================================================
Left column = FOUR stacked windows, ~48px vertical pitch, first window interior top ~y24; window left border ~x32, right border ~x160 (marble margin fills x0-31; text is indented to x48). Windows top-to-bottom: PC-slot0 (Crono), PC-slot1 (Marle), PC-slot2 (EMPTY here — its empty frame IS still drawn, matching current menu.c), TIME/GOLD.
Party = active-party $7E2980-82 (GATO = Crono id0, Marle id1, slot2 = $80 empty). Per-panel data (already pinned, menu.h/menu.c): char sheet $7E2600 + (id&7)*0x50 -> +3 curHP, +5 maxHP, +7 curMP, +9 maxMP, +$12 LEVEL; name via CtGetName.

>>> GAUGE BARS — VERIFIED ABSENT (reporting the canonical truth per the no-invention mandate). There are NO proportional HP/MP fill bars anywhere in this menu. Layer isolation (BG1 text, BG2 chrome, BG3 blank, OBJ = ribbon only) shows the PC panels contain ONLY glyphs — HP and MP are rendered as right-aligned decimal cur/max pairs, identical to what src/menu.c already draws. Confirmed on BOTH menu_open_gato2 and menu_open (identical). No routine draws a bar; the "fill length maps to HP/maxHP" premise does not exist in this ROM's field main menu. If a proportional bar is still wanted it would be an INVENTION — do not add one. (Caveat per verify-negatives rule: this is the mode-1 field menu; the in-BATTLE HUD is a different module and out of scope here.)

>>> WHAT THE PANELS ACTUALLY HAVE THAT menu.c LACKS = a 4th "equip" row per panel (menu.c draws only 3 rows). Structure per panel (screen-x, each glyph on an 8px cell; cur/max fields RIGHT-ALIGNED; measured off Crono panel y32-63):
  Row 1 (name/LV),   Crono y32-38: name at x48 (up to 5 glyphs), "LV" label ~x96, LV value right-aligned, ones-digit cell = x144 (the '1').
  Row 2 (HP),        Crono y40-46: "HP" label at x48-63; cur-HP right-aligned with ones-digit in the cell ending at x128; "/" fixed at x128 (cell 16); max-HP right-aligned ones-digit ending at x152. (Crono "70/70": cur "70" x112-127, "/" x128, max "70" x136-151.)
  Row 3 (MP),        Crono y48-54: "MP" label x48-63; same number geometry as HP ("/" at x128, cur ends x128, max ends x152). (Crono "8/8" single-digit; Marle "12/12".)
  Row 4 (EQUIP),     Crono y56-62: [weapon-type icon][weapon-attack][armor icon][armor-defense]. Icons are BG1 font tiles: weapon = $20 (a diagonal sword "\" for Crono) / $21 (an "X"/bow glyph for Marle) — these are the SAME slot-icons the right panel prefixes onto the weapon name ("\Wood Sword"); armor icon = $2E (a grid box). Values are equipment-derived (Crono "8 / 16", Marle "8 / 14"; these do NOT equal any base stat in the right panel, so they are weapon Atk / armor Def computed from equipped gear). Approx x: weapon-icon ~x64, atk value ~x112-120, armor-icon ~x144, def value ~x152.
Panel-2 (Marle) sits +48px below panel-1: name y80, HP y88, MP y96, equip y104. OBSERVED DETAIL: the lead/active char (Crono) text is BRIGHT WHITE (BG1 pal0, entry3=$7FFF); the non-active char (Marle) is rendered in a DIMMER GRAY sub-palette — the panels are palette-tinted by active/selected state (worth reproducing; sub-palette index selected per tilemap cell).

==================================================================
2. TIME / GOLD box (bottom-left window, slot-4)
==================================================================
Bottom-left window, interior ~y176-216 (marble border texture fills cells 1-3 = x8-31 on its rows). "TIME" label at x48-79; time value "HH:MM" at ~x88-152 (TIME row y184-191). GOLD "ddddG" is right-aligned on the row below (y200-206), 'G' glyph ends ~x160 ("400G" ones/G in cells 18-19 = x144-159). Data (already pinned): clock $7E0400-06 (display HH:MM via C2/F28E rule = $0405*10+$0404 : $0403*10+$0402, cap 99:59 if $0406!=0, clock paused while $0407!=0); GOLD = $7E2C53 24-bit LE. menu.c already renders these correctly; only the absolute X/Y differ from the oracle (menu.c uses MT_X=24,MT_Y=172; oracle window text is at x48/y184).

==================================================================
3. PORTRAIT (top-right of the status panel)
==================================================================
- WHICH CHAR: the currently-viewed status char = the lead (Crono, id0) here. 48x48 px.
- WHERE: top-right corner of the right status panel; orange face-core measured at x240-255 / y32-77 (clipped at the x255 screen edge), i.e. a 6x6-tile block whose right edge sits at/near the screen right, top ~y32.
- HOW DRAWN: it is a BG1 tile block, VRAM tiles $201..$224 (36 tiles, 6 wide x 6 tall, row-major), 4bpp. (It is NOT a sprite.)
- GFX SOURCE = ROM, UNCOMPRESSED, at file offset 0x3F0000 (HiROM FF/0000). VERIFIED: VRAM tile $201 == ROM FF/0000, tile $210 == FF/01E0, tile $220 == FF/03E0 (exact 32-byte matches, consecutive at +0x1E0/+0x3E0), and rendering ROM 0x3F0000 as a 6x6 4bpp grid produces Crono's face (SP/z_rom_portrait.png). => ROM-DERIVABLE. This is Crono (id0); a per-character portrait table lives in bank FF (each portrait 36 tiles = 0x480 bytes; the exact table base / per-char stride is not yet pinned — needs a non-Crono-lead menu state to confirm id1..).
- There is also a SECOND, separate 16x16 OBJ portrait icon (OBJ tile $024) used as the leftmost ribbon command icon (see 4) — different graphic, dynamically built per lead (not found raw in ROM).

==================================================================
4. COMMAND ICON RIBBON (floating, center) — OBJ sprites + BG2 box
==================================================================
This is a floating popup that OVERLAYS the right status panel (that is why the equipment/stats are hidden in the composite but visible under BG1 isolation). Three parts:
 (a) The purple BOX = BG2 chrome. BG2 map $6000 rows 12-17, cols 6-22 use tiles $3A4/$3A5/$3A6 (top edge), $3A7/$3AC/$3A8 (left/fill/right; $3AC = the solid purple fill), $3A9/$3AA/$3AB (bottom edge). On screen the purple box brackets the icon row (composite purple detected ~x128-199/y105-149; the box extends left behind the opaque icons, roughly x72-184 / y96-140).
 (b) 6 COMMAND ICONS = OBJ sprites, 16x16 (objSize=0), all Y=112 (top), X = 80, 96, 112, 128, 144, 160 (16px pitch, span x80-175), priority 3, OBJ palette 0 (CGRAM 128-143). OAM tiles $024, $006, $008, $00A, $00C, $00E. Decoded appearance (SP/z_objicons.png): $024 = a framed CHARACTER PORTRAIT icon (Crono face — the "status/character" command, currently the selected/leftmost slot); $006 = "ITEM" (label baked into the tile); $008 = "TEC" (Tech/special); $00A = a lined list-page icon; $00C = a "2"-with-recycle-arrows icon (party/double-tech); $00E = a diagonal-slash/config icon. The labels ITEM/TEC are part of the sprite bitmaps, NOT separate text.
 (c) 2 SCROLL ARROWS = OBJ sprites, tile $022, X=81 (over the leftmost/selected slot), palette 7 (CGRAM 240-255), priority 3: up-slot at Y=96 (attr $BE, vflip=1 -> points DOWN, a ▼ above the row) and Y=128 (attr $3E, vflip=0 -> points UP, ▲ below). They bracket the selection slot; Up/Down (joypad) scrolls which command sits under them.
 SOURCES: arrow tile $022 = ROM FF/9D70 UNCOMPRESSED (exact byte match; same triangle glyph as the name-entry cursor, already noted in ct-field-naming-screen) => ROM-DERIVABLE. The 6 command-icon tiles ($006/$008/$00A/$00C/$00E and the $024 portrait-icon) are NOT found raw in ROM => compressed/dynamic; reproduce by CAPTURE from OBJ VRAM (chr $4000) or by tracing the menu-init DMA.
 NOTE: the "right-edge vertical LABEL text" the task recon saw ("H M M E N" reading down at x232+) is NOT a rotated label and NOT the ribbon labels — it is the FIRST letters of the right status panel's left stat column (PWR/HIT/MAG/M.DEF/EXP/NEXT) peeking out past the right edge of the floating ribbon box. See section 5.

==================================================================
5. RIGHT STATUS / EQUIPMENT PANEL (bonus context; normally hidden behind the ribbon)
==================================================================
One TALL window on the right (BG2 map cols 14-29, rows 2-25; screen x~150-255). Contents on BG1 (from bgrec_bg1.png / iso_bg1.png):
 - Portrait (section 3) top-right; an element/name label ("Lightning" for Crono) sits by it (likely the 24 BG3 cells and/or a small tile block).
 - "Crono" (name), "LEVEL 1".
 - Equipment list, each line prefixed by its slot icon: "\Wood Sword" (weapon, icon $20), "?Hide Cap" (helm), "?Hide Tunic" (armor, icon $2E), "0Bandana" (accessory, icon $2F).
 - Stats, two sub-columns: LEFT "PWR. 8 / HIT 8 / MAG. 5 / M.DEF. 2 / EXP 0 / NEXT 20" (label first-letters P,H,M,M,E,N = the "vertical text" artifact); RIGHT "SPD. 13 / EV. 8 / STAM. 8". Values right-aligned to ~x253. Stat rows measured at y140,152,164,176,184,192,200. This whole panel is BG1 font text; its numbers are char-sheet/equip-derived (ROM-derivable from the data + font); no bars.

==================================================================
6. WINDOW CHROME + BACKGROUND (BG2)
==================================================================
BG2 map $6000, chr $0000, 4bpp. Two visual systems:
 - MARBLE BACKGROUND: a 2x2 repeating tile pattern $3A0/$3A1 over $3A2/$3A3 fills the ENTIRE screen behind everything (this is the "grey brick/weave BG tile" menu.c leaves black in phase 1). Palette = grays in CGRAM entries 8-15 ($3DCE $73FF $39CD $316B $2507 $20E6 $1CC5 $0843).
 - WINDOW FRAMES: a full 9-slice window tileset in $3DC-$3EF: top border $3DC/$3DD/$3DE/$3DF, textured interior $3E8-$3EF, bottom border $3E4-$3E7, plus the ribbon-box tiles $3A4-$3AC. Each left-column window = 12 cols wide (rows: Crono 2-7, Marle 8-13, empty 14-19, TIME/GOLD 20-25); the right status window = cols 14-29 rows 2-25.
 SOURCE: chrome tiles ($3A0-$3EF) are NOT found raw in ROM (searched tile $3EB etc. = 0 hits) => COMPRESSED, uploaded during menu init (the C2/8096 build chain: C2/940D,968D,D156,984A,8663,816A,92F4,F3CA,9875). To reproduce byte-exact: CAPTURE the BG2 chr $0000 tiles + the CGRAM from VRAM (this is the same "capture, don't derive" verdict menu.c already reached for chrome), OR trace/decompress the init DMA. This menu's chrome is its OWN tileset — it is NOT the field textbox chrome that current menu.c reuses.
 IMPORTANT SCROLL CAVEAT: --ppuregs reports BG1 scroll (0,15), BG2 (0,1), BG3 (0,1023) but these do NOT map the tilemaps to the screen linearly — the menu uses per-window HDMA scroll (the left panels, the right status panel and the ribbon each sit at different effective scrolls; e.g. the portrait tile-block is at map cols 14-19 yet renders at screen x~240). => derive tile CONTENT/SOURCE from the tilemap, but derive absolute POSITIONS from the framebuffer (as done above).

==================================================================
7. ROM-DERIVABLE vs CAPTURE-ONLY (implementer cheat-sheet)
==================================================================
ROM-DERIVABLE (compute/emit from ROM + data):
  - All TEXT (names, LV, HP/MP labels+numbers, equip row, TIME/GOLD, right-panel stats): data all pinned ($7E2600 sheets, $2980 party, $0400 clock, $2C53 gold); font already ported (glyph index = CT code). Positions per this dossier.
  - Status PORTRAIT (48x48): ROM FF/0000 uncompressed (Crono); per-char table in bank FF (base/stride TBD).
  - Ribbon SCROLL ARROW: ROM FF/9D70 uncompressed.
CAPTURE-ONLY (compressed/dynamic; grab from VRAM/OAM or trace init DMA to reproduce byte-exact):
  - Window CHROME + marble background tiles (BG2 chr $0000, $3A0-$3EF) + the CGRAM palettes.
  - The 6 ribbon COMMAND-ICON tiles (OBJ $006/$008/$00A/$00C/$00E) and the 16x16 portrait-icon $024.
  - The menu FONT glyph bitmaps themselves (uploaded to chr $0000 and $7000; not found raw) — but the port already has the font asset, so this is a non-issue for reproduction.
NOT PRESENT (do not implement): proportional HP/MP fill bars.

### Key addresses
- --ppuregs menu_open_gato2: mode 1; VRAM BG1 map $5800/chr $0000 (text+portrait,4bpp); BG2 map $6000/chr $0000 (chrome+marble+ribbon-box,4bpp); BG3 map $6800/chr $7000 (near-empty,24 cells); OBJ chr $4000/$5000 (ribbon,4bpp), objSize 0
- PEEK_ISOBG layer roles: BG1(0)=all text+48x48 portrait; BG2(1)=window chrome+full-screen marble bg+ribbon purple box; BG3(2)=blank; OBJ(4)=6 icons+2 arrows
- Status portrait (Crono id0): ROM file 0x3F0000 = HiROM FF/0000, 48x48 4bpp UNCOMPRESSED, VRAM tiles $201-$224 (byte-exact verified: $201=FF/0000,$210=FF/01E0,$220=FF/03E0); per-char portrait table in bank FF, stride ~0x480, base/stride unpinned
- Ribbon scroll arrow: ROM FF/9D70 UNCOMPRESSED (byte match; = name-entry cursor triangle), OBJ tile $022, palette 7
- Ribbon command icons: OBJ tiles $024(char/status portrait-icon),$006(ITEM),$008(TEC),$00A(list),$00C(2/party),$00E(config) at Y=112 X=80/96/112/128/144/160, prio3 pal0 — NOT found raw in ROM (compressed/dynamic)
- Window chrome/marble tiles: BG2 chr $0000 tiles $3A0-$3A3(marble 2x2),$3A4-$3AC(ribbon box, $3AC=purple fill),$3DC-$3EF(9-slice window frame) — NOT found raw (compressed); built by init chain C2/940D,968D,D156,984A,8663,816A,92F4,F3CA,9875
- Font: glyph bitmaps at VRAM chr $0000(BG1) AND chr $7000(BG3); index = CT charset code 'A'=$A0 'a'=$BA '0'=$D4 '/'=$E0 ':'=$E3 '.'=$E8 space=$FF (decoded from BG1 tilemap)
- Panel data (RAM, pre-pinned): char sheet $7E2600+(id&7)*0x50 -> +3 curHP,+5 maxHP,+7 curMP,+9 maxMP,+$12 LEVEL; active party $7E2980-82 (bit7=empty); clock $7E0400-06 (HH:MM=$0405*10+$0404:$0403*10+$0402, cap if $0406, pause if $0407); gold $7E2C53 24-bit LE
- Menu module (pre-pinned menu_entry.md): entry JSL C2/8000 (A=1); screen table C2/808F=[0E 00 0C 0D 0F 07 0B] -> $7E0067 (mode1=00); mainloop C2/8190; task scheduler slots $7E00B0 tick $7E0D01; input task C2/826F; render task C2/E91B (VRAM pump C2/E92D via MVN to $0740/$0904)
- Measured native-256x224 positions: left column 4 windows, ~48px pitch, first top ~y24, left border ~x32/right ~x160, text indented x48. Crono name y32/HP y40/MP y48/equip y56; Marle +48 (y80..). Number fields RIGHT-ALIGNED: '/' fixed x128, curHP/MP ends x128, maxHP/MP ends x152. TIME label x48 y184 / value x88-152; GOLD right-aligned 'G' ends x160 y200. Portrait ~x208-255 y32-79. Ribbon icons x80-175 y112, arrows x81 y96/y128
- CGRAM (ppuregs): pal0 = {0000,1483,4A51,7FFF} text white=entry3 $7FFF; active-char text bright white, non-active char (Marle) rendered in a DIMMER GRAY sub-palette (selected-state tint)

### Open questions
- GAUGE BARS: verified ABSENT in the mode-1 field menu (isolation across 2 states shows numeric HP/MP only). The task premise ('fill length maps to HP/maxHP') has no oracle basis here; do not invent one. If bars are truly required they must come from a different screen/module (e.g. the in-battle HUD, out of scope) — otherwise omit.
- Per-character portrait table: only Crono (id0 = ROM FF/0000) is pinned by byte-match. The table base and per-char stride in bank FF are unconfirmed — needs a menu state whose lead/selected char is not Crono to capture id1.. offsets.
- 4th equip-row semantics: shown as [weapon-type icon $20/$21][value][armor icon $2E][value] (Crono 8/16, Marle 8/14). Values do not equal any base stat in the right panel, so they are equipment-derived weapon-Atk / armor-Def — but the exact ROM formula/source bytes were not traced (the character-window builder inside the C2 render chain was not disassembled to instruction level).
- Compressed graphics sources not pinned to a ROM address: the menu window/marble chrome tileset (BG2 $3A0-$3EF) and the 6 OBJ command-icon tiles ($006/$008/$00A/$00C/$00E,$024). They are uploaded (likely LZ-decompressed) by the C2/8096 init chain; the exact ROM offsets + which init routine DMAs each region were not traced — reproduce by VRAM/OAM capture until traced.
- Per-window HDMA scroll: --ppuregs scroll values (BG1 0,15 / BG2 0,1 / BG3 0,1023) do NOT linearly map the tilemaps to screen (left panels, right status panel, and ribbon render at different effective scrolls). The exact HDMA scroll table/mechanism was not decoded; absolute positions here come from the framebuffer, not from tilemap*8+scroll.
- BG3 role: map $6800 has 24 nonzero cells and draws nothing under isolation, yet the font is uploaded to its chr $7000 — likely staging for the menu SUB-screens (Item/Tech/Equip/Status). The 24 cells (possibly the 'Lightning' element label by the portrait) were not identified.
- Character-window drawing routine in bank C2 (the code that formats name/LV/HP/MP + the equip row into the BG1 tilemap) was not located at instruction level; render task C2/E91B is the low-level VRAM DMA pump, not the content builder. Screen-0 content build likely dispatches near the C2/0932/0984 $0067 writers — untraced.

---

## TRACE: menu_subscreens

＝＝ CHRONO TRIGGER — FIRST MENU SUB-SCREENS (X-menu, mode 1) RE DOSSIER ＝＝
All claims cite a ROM disasm addr (tools/dis.py flat offset; C2 bank = 0x2xxxx) or an observed oracle value (ct_probe --peek/--monitor on saves/menu_open_gato2.state, screen id $7E0067). Captures live in SP = C:/Users/ICAROF~1/AppData/Local/Temp/claude/c--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/0659808f-a8c2-4887-9cf9-157a9540e206/scratchpad/. Oracle harness driven from menu_open_gato2 (mode1, main screen $0067=00, party=Crono+Marle, GATO/Leene's-Square early game, TIME 08:28, 400G).

＝＝ 1. MENU DISPATCH ARCHITECTURE (verified) ＝＝
The menu is a cooperative task scheduler (C2/8190, per menu_entry.md). Two state bytes drive every screen:
 • $7E0067 = SCREEN ID (0=main menu ribbon; 1..6 = the six sub-screens; other ids belong to non-X-menu modes).
 • $7E0068 = SUB-STATE (coroutine phase within the current screen: 0=init, 1=idle-input, 2+=deeper interaction).
The input task C2/826F dispatches EVERY frame via TWO-LEVEL indirection (disasm C2/8279-828E):
   LDA $67; ASL; TAX; LDA $C28292,X -> $16   (per-screen sub-handler-array ptr)
   LDA $68; ASL; TAY; LDA [$16],Y -> $00 ; JMP ($0000)   (sub-state handler)
INPUT-DISPATCH TABLE C2/8292 (raw bytes verified: CA99 299C 34AA 53B3 35BF 21C6 D4CE A6C7 9182×3 F0D4 19D5 E3E1 0BE6 0AFE). Per-screen handler-array pointers (all bank C2): screen0=C2/99CA (MAIN), screen1=C2/9C29 (EQUIP/STATUS), screen2=C2/AA34 (ITEM), screen3=C2/B353 (TECH), screen4=C2/BF35 (CONFIG), screen5=C2/C621 (FORM), screen6=C2/CED4 (SAVE), screens 8/9/A=C2/8291(=RTS,null), plus screens 7/B/C/D/E/F for the other menu modes (mode table C2/808F=[0E 00 0C 0D 0F 07 0B] -> screens are ENTRY points for modes 0/2/3/4/5/6, NOT X-menu sub-screens).

＝＝ 2. THE RIBBON (main screen 0) ＝＝ [SP/sub_base.png, SP/opt*.png]
Layout: 3 character panels top-left (name/LV/HP/MP; Crono LV1 70/70 8/8, Marle LV1 65/65 12/12), a floating command box centre-right = a horizontal row of 6 command icons, ▲▼ cursor bracket, portrait top-right, TIME hh:mm + gold bottom-left.
OAM (SP/sub_base.raw.oam): 6 icon sprites idx0-5 at Y=112, X=80/96/112/128/144/160, tiles 0x24,0x06,0x08,0x0A,0x0C,0x0E, prio3; plus a ▲▼ cursor pair idx6/7 (tile 0x22, X=81, Y=96 v-flip / Y=128) that straddles the SELECTED icon (the recon's "up/down arrows" = this cursor, NOT a scroll control).
RIBBON CURSOR = $7E0411. LEFT (0x40) / RIGHT (0x80) move it (verified: Left 0->5, Right 5->0, so 6 options 0..5, wrap; Right=+1 mod6). Up/Down do NOTHING on the main screen (verified: no change to $0411/$0060-$00FF). Idle handler C2/9A1B: LDA $0411->$54; JSR C2/E984 (reads dpad->$0D1D bit7=A/bit6=B and adjusts cursor); BIT $0D1D; BMI->C2/9A77 (A), BVS->C2/9A95 (B).
A-DISPATCH (C2/9A77): stores new screen id -> STA $0067 (C2/9A81) then STZ $0068 (C2/9A83). EMPIRICAL MAP (each = press-Right N + A, then read $0067): cursor c -> screen (c+1) for c=0..3; c=4/5 briefly set $0067=5/6 for ONE frame then it reverts to 0 (Form/Save SPAWN a task via the scheduler and let the main render continue underneath). So the ribbon left-to-right = [Equip/Status, Item, Tech, Config, Form, Save].
B on the MAIN screen (C2/9A95 -> JMP C2/82B2) FALLS INTO the menu EXIT C2/82B4 (LDA #FF fade; JSR C2/834D party-gfx rebuild from bank D1; force-blank; LDX $0920;TXS restore FIELD stack; RTL -> field). This is the whole-menu close (per menu_entry.md).
B on ANY SUB-screen -> JMP C2/9A98: STZ $0067 (back to main screen 0), LDA#01;STA $0068 (main idle), LDA#74;JSR C2/8255 (palette fade), JSR C2/9B71 (redraw), JMP C2/EAC2. Confirmed shared: Item's B handler C2/AA83 = JMP C2/9A98; Equip's B (C2/9C6D) = JMP C2/9A98.

＝＝ 3. SCREEN 1 — CHARACTER DETAIL = STATUS (view) + EQUIP (change)  [$0067=01]  [SP/eq5.png, SP/eq_full.png] ＝＝
CT has NO separate Status ribbon icon (only 6 icons/6 screens exist). The FIRST icon (cursor0, tile 0x24) opens screen 1, which is the character detail: the read-only STAT DISPLAY = "Status", and pressing A on an equipment slot enters the interactive EQUIP change. Both share $0067=01, distinguished by $0068 (handler table C2/9C29 = {9C31 init->C2/A11A layout, 9C39 status-idle, 9C70 slot-select->C2/A41A, 9CA5 item-select}). Headless render only draws the top panel + "Weapon" slot label (Helm/Armor/Accessory + the stat panel don't composite in --peek for this screen; likely a mid-frame HDMA/BG the peek misses, or this early state has no swappable gear so the slot list is inert — A never advanced $0068 in any probe).
DRIVING RAM = the CHARACTER SHEET: $7E2600 + charId*0x50 (0x50=80 bytes/char, 7 chars $2600-$282F). VERIFIED byte layout (Crono id0 @$2600 = 00 86 80 46 00 46 00 08 00 08 00 05 08 0C 05 08 08 02 01 ...; Marle id1 @$2650; Lucca id2 @$26A0):
   +0x00 charId | +0x01 name/gfx idx | +0x02 flag(0x80=recruited) | +0x03..04 curHP(LE) | +0x05..06 maxHP | +0x07..08 curMP | +0x09..0A maxMP | +0x0B..0x11 = 7 battle stats (Power/Stamina/Speed/Magic/Hit/Evade/Mag.Def per CT convention; exact order not visually pinned) | +0x12 LEVEL (Crono/Marle=01, Lucca=02) | +0x13 XP | +0x27..0x3F a second data cluster (equipment ids + tech/skill points + base-stat copy; e.g. Crono +0x28..2B=5B 01 95 14) | +0x40..0x4F = FF (reserved). HP/MP/Level offsets and the +0x0B stat cluster are byte-verified across all 3 sheets; the equipment-slot byte offsets within +0x24..0x2B are present but NOT individually pinned this pass.
Also read for the panels: party $7E2980-82 (=00 01 80 -> Crono,Marle,none), names $7E2C23 (6-byte CT-encoded strings: "Crono"/"Marle"/"Lucca"), gold $7E2C53 (24-bit LE =400). EQUIP-change mode filters the inventory ($2400/$2500, see §4) by slot type and rewrites the char-sheet equipment id + recomputes derived stats.
DIFFICULTY: STATUS view = EASY (pure data-format of the $2600 sheet — the task's "most self-contained"). EQUIP change = MEDIUM (slot cursor, inventory filter by equip type, stat-delta preview + apply).

＝＝ 4. SCREEN 2 — ITEM  [$0067=02]  [SP/opt1.png] ＝＝
Layout: header "ITEM", a "Use / Move" action tab, then a scrollable item list (this state shows one row: "Pendant"). B returns to main.
DRIVING RAM = the persistent INVENTORY, TWO parallel arrays (VERIFIED identical in the field-menu base AND with the item screen open — they are the real held-item store, not a rebuilt buffer):
   $7E2400 = item-ID list (packed, 0x00-terminated; this state = D6 00 00...; id 0xD6 renders "Pendant")
   $7E2500 = quantity list (parallel; = 01 00 00...)
Capacity 0xF2 (242) slots (builder C2/ADD0: LDY #00F2; JSR C2/EA81 pagination via HW mult/div $4204/$4206/$4214). The "Move" action swaps two rows in BOTH arrays in place (C2/AAE3-AAFC: LDA $2400,X/PHA/LDA $2400,Y/STA $2400,X/PLA/STA $2400,Y and the same for $2500) — proving $2400/$2500 ARE the canonical inventory. "Use" targets a party member.
ITEM NAMES = ROM table in bank $CC (CT text encoding: 0xA0='A'..0xB9='Z', 0xBA='a'..0xD3='z', 0xEF=space, 0xFF=terminator). Verified by searching the ROM for the encoded strings: "Pendant"(AF BE C7 BD BA C7 CD) at ROM 0xC1491 = SNES $CC/1491; "Tonic" $CC/137E; "Shelter" $CC/13F7. Consumables sit in a fixed-width block (~$CC/1373 base, ~11-byte entries -> Tonic/Shelter 11 ids apart); key items (Bike key/Key/Pendant/Gate key) are packed variable-length from ~$CC/1486. The screen indexes the name table by the id from $2400.
DIFFICULTY: MEDIUM — list build + scroll/pagination + Use(target a PC)/Move(swap) interactions; but the data model ($2400/$2500 + $CC name table) is fully pinned.

＝＝ 5. SCREEN 3 — TECH  [$0067=03]  [SP/opt2.png char-list, SP/tech1.png Crono detail] ＝＝
Layout: character list with portraits + LV/HP/MP + "MP Used" + a "Single" (single/dual/triple) category tab; after selecting a PC, shows their tech list, MP cost, "Sk 11 Pts / Need 5" (skill/tech points to next tech), and a tech description ("Spincut nearby e[nemies]"). Handler C2/B353={B35D,B365,B36D,B3AE,B48E,...}.
DRIVING RAM: char sheet $7E2600+id*0x50 (LV/HP/MP + the tech/skill-point bytes in the +0x28..0x2B cluster) + the character's learned-tech bitfield; tech names/MP-cost/descriptions from ROM tables (same $CC-region string mechanism as items).
DIFFICULTY: MEDIUM — tech tables + description strings + use-in-menu targeting.

＝＝ 6. SCREEN 4 — CONFIG  [$0067=04]  [SP/opt3.png] ＝＝
Layout: a grid of icon toggles (checkbox rows with red check-marks) + a highlighted setting line "Stereo   On". Handler C2/BF35. These are the game settings (battle mode Active/Wait, battle speed, message speed, stereo/mono, save-menu/window frame, etc.).
DRIVING RAM: the persistent config settings byte(s) (part of the saved config block; not pinned to an exact $7Exxxx address this pass — opening the screen without toggling changed only the clock $0400/01 and the ribbon cursor $0411, so no setting byte was isolated). Applies immediately (writes PPU/APU/engine flags).
DIFFICULTY: EASY–MEDIUM — a fixed toggle grid over a settings bitfield.

＝＝ 7. SCREEN 5 — FORM / PARTY-EXCHANGE  [$0067=05 transient]  [SP/opt4.png, SP/form2.png] ＝＝
Layout: the character panels slide right and a party-arrange overlay ("Exc" = Exchange) comes in with a red action icon. $0067 flips to 5 for ONE frame (verified f48) then reverts to 0 — the option SPAWNS a scheduler task that renders the overlay atop the main screen (why $0067/$0068 read 0 while the overlay is up). Handler C2/C621. Interactive; did not fully settle headlessly (slide/wait animation).
[CORRECTED 2026-07-24, menu_open_items.md 3: there is NO task-spawned overlay. The one-frame $0067
flip is the screen's own ENTRY GATE C2/C627 REFUSING to open (($0F0B+$0F0C) < 2 -- Crono is locked
by $7F01DF) and bouncing to the ribbon; what settled after was the ribbon's opt-4 HOVER PREVIEW.]
DRIVING RAM: active party $7E2980-82 + the reserve-member roster + char sheets $2600 (for the swap panels). Reorders/ swaps active vs reserve members.
DIFFICULTY: MEDIUM — task-spawned overlay + party-array manipulation.

＝＝ 8. SCREEN 6 — SAVE  [$0067=06 transient]  [SP/opt5.png, SP/save2.png] ＝＝
Layout: three numbered file slots ("1 / 2 / 3") each with a party-leader name column; a slide-in like Form. Same transient-$0067 + spawned-task pattern. Handler C2/CED4 -> setup C2/D0E5.
[CORRECTED 2026-07-24, menu_open_items.md 4: no spawned task here either -- the ENTRY GATE C2/CEDC
refused (field map, $7F01CE bit15 clear -> $0D34=FFFF from C2/9875). The screen OPENS on the
overworld (saves/ow_w0.state) and was captured byte-exactly there.]
DRIVING RAM: the 3 SRAM save-file slots (CT SRAM, banks $70/$F0; per menu_entry.md the module also clears SRAM $30:7FE1/2) read for the slot summaries (leader/location/play-time) and written on confirm; char sheets $2600 + names $2C23 for the display.
DIFFICULTY: MEDIUM–HARD — SRAM read/write + per-slot summary render + confirm flow.

＝＝ 9. SUMMARY TABLE ＝＝
cursor0 -> $0067=1 Equip/Status (char sheet $2600+id*0x50) — Status=data-display(EASY), Equip=interactive(MED)
cursor1 -> $0067=2 Item (inventory $2400 ids/$2500 counts, names ROM $CC) — MED
cursor2 -> $0067=3 Tech (char sheet + tech tables) — MED
cursor3 -> $0067=4 Config (settings bits) — EASY/MED
cursor4 -> $0067=5(transient) Form/party-exchange (party $2980-82 + reserves) — MED
cursor5 -> $0067=6(transient) Save (SRAM slots $70/$F0) — MED/HARD
B: sub-screen -> C2/9A98 (STZ $67 back to main); main -> C2/82B4 (exit to field).
IMPLEMENTATION ORDER SUGGESTION: STATUS (screen1 view) is the cleanest first target — a pure format of the byte-verified $2600 char sheet; then ITEM (fully-pinned $2400/$2500 + $CC names); then EQUIP (adds inventory-filter+stat-apply on top of the same two data sources).

### Key addresses
- $7E0067 = menu SCREEN ID (0 main / 1 Equip+Status / 2 Item / 3 Tech / 4 Config / 5 Form-transient / 6 Save-transient)
- $7E0068 = menu SUB-STATE (coroutine phase within a screen: 0 init,1 idle,2+ interaction)
- $7E0411 = ribbon cursor (0..5, LEFT 0x40/RIGHT 0x80 move it, wrap; Up/Down inert on main)
- C2/8279-828E = two-level input dispatch: $C28292[screen*2]->$16 ; [$16],[$68*2]->JMP
- C2/8292 = per-screen input-handler-array table (s0=99CA s1=9C29 s2=AA34 s3=B353 s4=BF35 s5=C621 s6=CED4)
- C2/9A1B = main ribbon idle (LDA $0411->$54; JSR C2/E984; BIT $0D1D bit7=A/bit6=B)
- C2/9A81 = STA $0067 (option->screen commit); C2/9A83 = STZ $0068 ; empirical map screen=cursor+1
- C2/9A98 = SUB-SCREEN B-return: STZ $0067 (to main), $0068=1, C2/8255 fade, C2/9B71 redraw
- C2/82B2->C2/82B4 = MAIN B = menu EXIT to field (C2/8255 fade, C2/834D party-gfx from bank D1, LDX $0920;TXS restore field stack, RTL)
- $7E2600 + charId*0x50 = CHARACTER SHEET (0x50 bytes): +3/4 curHP,+5/6 maxHP,+7/8 curMP,+9/A maxMP,+0xB..0x11 7 stats,+0x12 level,+0x13 XP,+0x24..0x2B equip ids/tech pts
- $7E2400 = inventory item-ID list (packed,0-term,0xF2 slots); $7E2500 = parallel quantity list (persistent; Move swaps both @C2/AAE3-AAFC)
- C2/ADD0 = Item display/scroll builder (LDY #00F2; JSR C2/EA81 pagination); item id 0xD6 = Pendant
- ROM $CC region = item-name table (CT encoding 0xA0='A',0xBA='a',0xEF=space,0xFF=term): Pendant $CC/1491(ROM 0xC1491), Tonic $CC/137E, Shelter $CC/13F7
- $7E2980-82 = active party (00 01 80 = Crono,Marle,none); $7E2C23 = 6-byte name buffers; $7E2C53 = gold(24-bit)
- OAM ribbon: idx0-5 icons tiles 0x24/06/08/0A/0C/0E @Y112 X80-160; idx6/7 tile 0x22 = ▲▼ cursor bracket
- Sub-screen logic handlers: Equip C2/9C31/9C39/9C70(+A11A layout,A41A slot); Tech C2/B353; Config C2/BF35; Form C2/C621; Save C2/CED4->D0E5

### Open questions
- Screen 1 (Equip/Status) renders sparsely in --peek (only Crono panel + 'Weapon' slot; Helm/Armor/Accessory + the full stat panel never composite, and no A/Down probe advanced $0068). Need either the ROM render trace of C2/A11A/A170/A71E or a later-game state with swappable gear to (a) confirm the stat-panel layout and (b) byte-pin the 4 equipment-slot offsets within the $2600 record (+0x24..0x2B region) and the derived Attack/Defense computation.
- The A-dispatch arithmetic at C2/9A77 (value stored to $0067) could not be cleanly decoded (M/X flag width garbles the disasm); the cursor->screen relation is only EMPIRICALLY pinned as screen=cursor+1 (cursor0..3->screens1..4 direct; cursor4/5->5/6 transient). The exact table-vs-arithmetic form and the Form/Save task-spawn call site are unconfirmed.
- Config screen (screen4): the persistent settings byte(s) address was not isolated (opening it without toggling changed only the clock and ribbon cursor). Need to toggle a setting under --monitor --diff to locate the config bitfield and map each toggle icon.
- Form (screen5) and Save (screen6) are interactive slide-in overlays spawned as scheduler tasks; they never settled headlessly. Their exact task entry, the reserve-roster address (Form), and the SRAM save-slot layout/summary fields + write path (Save, banks $70/$F0) are inferred, not traced.
- Stat order within the char-sheet +0x0B..0x11 cluster (Power/Stamina/Speed/Magic/Hit/Evade/Mag.Def) follows CT convention but was not individually visually confirmed; +0x28..0x2B (Crono 5B 01 95 14) mixes equipment ids and tech/skill points and needs disambiguation.
- Item-name table exact stride/base: consumables appear fixed-width (~11 bytes from ~$CC/1373) but key items (incl. Pendant id 0xD6 @ $CC/1491) are packed variable-length from ~$CC/1486 — the id->offset indexing (pointer table vs fixed stride, and where the two regions join) is not fully derived.
  [CLOSED 2026-07-24, menu_open_items.md 2: ONE uniform 11-byte-stride table $CC/0B5E..15C4, key
  items included -- builder C2/98ED ($7E7D00[id] = 0x0B5E + id*11, 0xF2 entries), fetch C2/F2E2
  (`LDA #$CC0B`). "Pendant @ $CC/1491" was a text search landing one byte INTO record 214 ($CC/1490);
  records lead with 0xFF (space) or an equip-type icon byte.]
- Tech screen learned-tech bitfield address and the tech name/MP/description ROM tables were not located this pass (only the char-sheet-driven header + a rendered description string were observed).

---

## TRACE: menu_form_party_exchange  (SCREEN 5 = FORM, handler C2/C621)

The FORM screen (ribbon opt4, $0067=5) is the PARTY-ORDER / exchange overlay. It is TASK-SPAWNED: A on
the ribbon sets $0067=5 for one frame then the option spawns a scheduler task that slides the overlay in
atop the main screen (so --peek never settles it — RE is from the ROM disasm; the EFFECT is validated on
the active-party RAM $7E2980-82). Two-substate flow ($0068), disassembled this pass (dis.py 2C621+):

FLOW (substate handlers off the C2/C621 array):
 - INIT (C2/C63D->C2/C649): the picker scans for the first NON-empty panel ($0F02,X AND #C0 == 0), stores
   the slot to $54 (cursor), inits both picks $0F00/$0F01 = FF (none), INC $68.
 - PICKER IDLE (C2/C65D): JSR C2/E984 (A/B decode -> $0D1D); BMI->C2/C674 (A = first pick); BVS->C2/C6B3
   (B = exit); else JSR C2/C805 (cursor move) + redraw. The cursor's current slot value lives in $0F0D.
 - FIRST PICK (C2/C674): store the cursor slot ($0F0D) -> $0F00 (@C2/C6AB), redraw the picked panel, INC $68.
 - SECOND-PICK IDLE (C2/C6B9): A -> C2/C6D0; B -> C2/C6DE (cancel the pending pick, DEC $68 back to picker).
 - SECOND PICK / CONFIRM (C2/C6D0): BIT $0F0D; BVS->cancel (invalid/empty); else store $0F0D -> $0F01
   (@C2/C6D8) then JSR C2/C70C (THE SWAP).
 - B ON PICKER (C2/C6B3): JSR C2/C6E7 (teardown) -> JMP C2/9A98 (STZ $0067 back to the ribbon).

★ THE SWAP — C2/C70C (byte-exact, the ONE intentional RAM write):
    SEP #30; LDX $0F00; LDA $2980,X; STA $00;   ; tmp = party[firstSlot]
    LDY $0F01; LDA $2980,Y; STA $2980,X;         ; party[firstSlot] = party[secondSlot]
    LDA $00; STA $2980,Y;                         ; party[secondSlot] = tmp
    JSR C2/816A ... (the rest is character SPRITE/palette REBUILD: C2/C784 gfx-slot calc, C2/F378, C2/8385,
    C2/CD2B, C2/CDE3 — the on-field PC objects get re-derived from the new order, the ROM's party reconcile).
  => an active-party ORDER exchange is literally: swap the two selected slot BYTES in $7E2980-82. At
     storyline 08 (party 00 01 80, only Crono+Marle) the sole exchange is slot0<->slot1: 00 01 80 -> 01 00 80.

RESERVE ROSTER: the Form screen also manages reserve members (chars recruited but not in the active 3) via
the $0F02+/$0D9C/$0DA8 panel buffers (C2/C674 reads $80/$7F, C2/C728 CMP #03 branch). At storyline 08 there
are NO reserve chars, so the screen only reorders the 2 active members — reserve handling DEFERRED.

PORTED (this pass): src/menu.c CtMenuFormSwap (= C2/C70C) + CtMenuRenderForm (layout-faithful: the 3 party
panels + an "Exchange" action header + the picker/pick hand markers; the exact slide-in animation + icon-grid
pixels are the documented approximation). field.c menuScreen==5 input: Up/Down cycle occupied slots, A picks
two members -> swap, B cancels a pick / exits to the ribbon. Validated: scripted menutest branch (menuform:
screen=5 party 00,01 -> 01,00 swap PASS) + a double-swap-back so the field resume stays byte-identical.

### Key addresses (Form)
- $7E2980-82 = active party (the swap target); C2/C70C swaps $2980[$0F00] <-> $2980[$0F01]
- $0F00 = first-picked slot, $0F01 = second-picked slot, $0F0D = the picker cursor's current slot value
- C2/C621 = screen-5 substate-handler array; C2/C649 init/cursor-scan; C2/C65D picker idle; C2/C674 first pick
- C2/C6B9 second-pick idle; C2/C6D0 confirm (-> C2/C70C swap); C2/C6B3 B-exit (-> C2/9A98); C2/C805 cursor mover
- C2/C70C = THE SWAP (SEP#30; exchange the two $2980 bytes) + character sprite/palette rebuild tail

---

## 2026-09-07 — EVERY X-MENU SCREEN vs THE ORACLE, pixel-differenced

Run after wiring the VWF into the X-menu host (commit 8f87f03). Method: the SAME state on both
sides -- `saves/truce_market.state` for the oracle (`ct.exe --peek`, drive `X, Right x N, A`) and its
own WRAM (`ct.py wram`) for ours (`--menutest` + `CT_MENUSCREEN`), so the DATA is identical and only
the rendering differs. The oracle frame is 256x240 and ours 256x224, so the compare shifts the
oracle down by the 8-line overscan head; a pixel counts as differing at max-channel delta > 48.

| screen | differing px | of frame | where |
|---|---|---|---|
| **Status** | **11** | 0.0% | one region, x82..84 y176..182 -- a single digit of `TIME 00:02` |
| **Item** | 110 | 0.2% | the row hand (x24..39) and the list arrow (x234..239) -- same boxes, different shapes |
| **Tech** | 547 | 1.0% | `Single Tech` (x81..159), something at x24..69 on that same bar, and `Cyclone` (x144..198) |
| **Config** | 966 | 1.7% | the bottom bar (x48..109) and the value strips on rows y24..47 / y72..86 |

Form and Save are NOT in the table: the oracle capture at that point still carries the ribbon box
over the sub-screen (a property of the drive, not of the port), so the numbers are meaningless. The
Save screen was verified separately and exactly -- see the ink boxes in commit 8f87f03.

### What the differences actually are

**⭐ THE VWF IS WIDER-REACHING THAN THE SAVE SCREEN.** The Config bottom bar is the clearest case:
the oracle inks `Stereo` at x48..84 -- 37 px for six characters, ~6.2 px each -- where our 8x8
`mText` inks 48..95. So the ROM renders that bar with the variable-width font, and we render it
fixed. It is NOT the whole screen: the button list (`Confirm A` / `Cancel B` / ...) at x144..215
produces ZERO diff pixels, so those rows are 8x8 on both sides. Each screen therefore needs its own
answer to "which fields are VWF", the way the Save screen just got one.

**The Config bottom-bar VALUE is YELLOW and left-aligned.** Measured in the oracle: the label is
white (231,231,231) at x48..84 and the value `On` is pure yellow (255,255,0) at x96..108. Ours draws
it white (255,255,255) and RIGHT-aligned at x222..236 (`mTextR(h, fb, vl, MI_HX + 230, 188)`). The
engine already has `drawGlyphYellow`.

**The Config value strips are highlighted per selection.** Rows y24..47 (`Fast`/`Slow`, the 1-8
strips) and y72..86 (`Off 1 2`) differ where the ROM marks the current value; ours only dims the
others (`mDim = (bg != n)`).

**Item / Tech are placement, not content**: the same strings in the same panels, a few pixels or a
column apart, plus the hand-cursor and arrow sprites differing in shape inside an identical box.

Ranked backlog, largest first: Config (966 -> **0**, resolved below) -> Tech (547 -> **0**, resolved below) -> Item (110 -> **0**) -> Status (11 -> **0**). ALL FOUR RESOLVED -- see below.

---

## 2026-09-07 — CONFIG SCREEN: 966 differing px -> 0

Pinned by the regression check `menu-config-screen`
(`CT_MENUOPT=4 CT_MENUSCREEN=4 build/Belthasar.exe --assets ct_assets.dat --menutest
saves/battle_re/gato_t0.raw.wram`, `fnv32=87DF54C9`). Oracle side of the comparison:
`build/ct.exe --peek saves/truce_market.state cfg.raw --btnscript` driving `X, Right x3, A`; ours is
the same state's WRAM through `--menutest`. Oracle y - 8 = our y (the 8-line overscan head).

Three defects, each derived from the ROM rather than from the picture. Diff after each:
**966 -> 664 -> 220 -> 0.**

### 1. The bottom bar is VWF, and the value sits on a TILE BOUNDARY past the name

`C2/C4BC` stages the cursor cell into `$0DCE` and the composed value index into `$0DCF`, then
`JSR C2/F943`. F943 renders the NAME through the same variable-width font the Save screen uses
(`JSL C2:0003` -> the C2/5DC4 glyph emitter, per-glyph advance from C2/60E6), and then places the
VALUE:

```
2F9ED  LDA $0DD2 / AND #F8 / LSR A / LSR A / INC A / INC A / STA $0DC1
```

`$0DD2` is the pen the name left. `AND #F8` rounds it DOWN to a tile, `LSR/LSR` converts px -> tile,
two `INC`s skip TWO tiles, and the result is the value's tile column. In pixels that is
`(pen & ~7) + 16`.

MEASURED on four cursor cells of the oracle's own Config screen (label ink span -> value ink start):

| label ink | value ink |
|---|---|
| 48..84 | 96 |
| 48..114 | 128 |
| 48..157 | 168 |
| 48..161 | 176 |

All four are exactly `(pen & ~7) + 16`. (Round-UP-plus-one-tile fits the same four, because the pen
never lands on a boundary in them — so it is the ROM's own `AND #F8` that decides between them, not
the measurement.)

The two runs are DIFFERENT RAMPS, sampled off that capture: the name is the plain menu ramp
(body 231,231,231 / shadow 24,24,24 / mid 49,49,49 = `ctLoaderVwfTone` exactly) and the value is
YELLOW (body 255,255,0 / shadow 33,33,0 / mid 140,74,0) — the ramp `drawMenuGlyphYellow` already
carried, which is where `ctMenuVwfYellow`'s mid entry comes from rather than from a guess.

Before: `mText` at 8x8 for the name and `mTextR(..., MI_HX + 230, 188)` right-aligning the value at
x222..236 — a whole different place on the bar.

### 2. The option strips dim with drawGlyphDim2, not drawGlyphDim

`mCfgStrip` and the `Off / 1 / 2` row used `mDim = 1` (drawGlyphDim: 123/8,16,16). The oracle's
non-selected strip ink is body 140,148,148 / shadow 49,66,74 — `drawGlyphDim2`. Switched both to
`mDim = 2`, and the `Off/1/2` row to `mDim = (bg != n) ? 2 : 0` so the selected entry stays bright.

### 3. The Off/1/2 row is at y80, not y72

Identified as a pure row offset, not a content difference: the differing region had **identical
x-spans and identical per-row pixel counts** to the oracle's, uniformly 8 rows apart. Moving the row
from y72 to y80 zeroed it.

### Verification

```
== Config final: 0 differing px in 0 region(s)
```

over the whole 256x224 frame. Remaining backlog on the other screens is unchanged:
Tech (547 -> **0**, resolved below) -> Item (110 -> **0**) -> Status (11 -> **0**). ALL FOUR RESOLVED -- see below.

---

## 2026-09-07 — TECH SCREEN: 547 differing px -> 0

Pinned by `menu-tech-screen` (`CT_MENUOPT=3 CT_MENUSCREEN=3 build/Belthasar.exe --assets ct_assets.dat
--menutest saves/battle_re/gato_t0.raw.wram`, `fnv32=874F265F`). Oracle side: `build/ct.exe --peek
saves/truce_market.state t2.raw --btnscript` driving `X, Right x2, A`.

### 1. ⭐ THE TEXT-BUFFER CELL IS THE SCREEN COLUMN — a general rule, not a Tech one

Both bottom bars in the menu (Tech and Config) reach the same renderer: a run is staged into a WRAM
text buffer and `JSL C2:0003` rasterises it there through the variable-width font. Two variables
place it:

* `$0DC5` — the byte offset of the run's first cell inside the buffer (**two bytes per 8-px cell**);
  the buffer's own base is `$0DC5 & FFC0` (`C2/FA5F`).
* `$0DD2` — the pen, which `C2/FA81` starts at **8**.

so the ink lands at

```
x = ($0DC5 - ($0DC5 & FFC0)) * 4 + 8
```

CALIBRATED on the oracle's own RAM — read `$0DC5` / `$0DC1` out of a capture's `.wram` and the ink
out of the same capture's pixels — over two screens and four runs:

| run | `$0DC5` | cell | predicted x | oracle ink |
|---|---|---|---|---|
| Tech tab name (`C2/BE16`) | 5356 | 11 | 96 | **96** |
| Tech description (`C2/BE33`) | 5344 | 2 | 24 | **24** |
| Config name (`C2/F9BA`) | 4E0A | 5 | 48 | **48** |
| Config value (`$0DC1` = 0C past 4E0A) | — | 11 | 96 | **96** |

This also settles the Config value's placement arithmetic exactly. `C2/F9ED`

```
LDA $0DD2 / AND #F8 / LSR A / LSR A / INC A / INC A / STA $0DC1
```

converts the name's final pen to a cell offset and adds **2 bytes = ONE cell**; the other 8 px come
from `$0DD2` being reset to 8 for the value run (`C2/FA27`). Net `x_value = 64 + (advance & ~7)`,
which is what `(pen & ~7) + 16` computes when the pen is carried as `48 + vwfWidth(name)` — so the
port's expression is the ROM's, and the earlier note's "the skip is TWO tiles" was a mis-attribution
of where the second 8 px comes from.

**THE DEFECT.** Our Tech bar drew BOTH cases in the 8x8 menu font at the description's column:
`Single Tech` inked x24..109 where the ROM inks 96..158. Tone is `ctLoaderVwfTone` — oracle body
(231,231,231) over shadow (24,24,24).

### 2. The preview row's ramp: attribute 0x0C is drawGlyphDim2

`C2/BDB2` picks each list row's TEXT ATTRIBUTE out of the per-tech flag byte `$7E7700 + techId` and
hands it to the `C2/EF65` blitter in DP `$7E`:

```
2BDC0  LDX #0C / LDA $7700,Y / BIT #40 / BNE ->store    ; bit6 = the NEXT-TO-LEARN tech
2BDC9  LDX #00 / BIT #80 / BEQ ->store                  ; bit7 = usable OUTSIDE battle
2BDCF  BIT $78 / BMI ->store / LDX #08
```

0x0C / 0x08 / 0x00 are BG tilemap palette bits (attr bits 2-3) — three brightnesses.

WHO SETS BIT6: `FF/F813`'s per-character pass. For each of the seven characters it finds the lowest
set bit of the learned copy `$7F00+c`, takes the next one up from `FF/F9BB`, ORs it into the copy so
the screen lists it, and marks the tech:

```
3FF8AE  LDA $7701,X / AND #1E / ORA #40 / STA $7701,X    ; X = c*8 + position
```

That is EXACTLY the tech `CtMenuTechBits` already computes as `nextIdx7[c]` (the same `FF/F876` arm,
same `FF/F951` cap table), so the port's `dim[k]` IS the ROM's bit-6 gate. What was wrong was only
the RAMP: measured on the oracle's own Cyclone row, body (140,148,148) over shadow (49,66,74) =
`drawGlyphDim2`, where we inked (123,123,123) over (8,16,16) = the dark `drawGlyphDim`. Same glyphs
in the same places — 133 body + 95 shadow pixels on BOTH sides, only recoloured. (The same
Dim-vs-Dim2 confusion the Config value strips had.)

WHO SETS BIT7: `FF/F830-F84B`, nine fixed `TSB #80` on ids `09 0C 0F 1A 1D 21 24 27 29` — the techs
usable outside battle (Aura, Cure, Life, Heal, Cure2, Heal Beam, ...). **Attribute 0x08 is
DEFERRED**: none of the nine is Crono's, so no capture here exercises it, and our menu does not build
the `$7700` flag page — porting it belongs with the Item screen, which reads the same page
(`C2/AB1F` / `ABB4` / `ABF5`).

### Verification

```
== screen 3: 0 differing px in 0 region(s)
```

Remaining: Item (110 -> **0**) -> Status (11 -> **0**). ALL FOUR RESOLVED -- see below.

---

## 2026-09-07 — ITEM (110 -> 0) AND STATUS (11 -> 0): the backlog closes

Pinned by `menu-item-screen` (`CT_MENUOPT=1 CT_MENUSCREEN=2`, `fnv32=FCFFB693`) and
`menu-status-screen` (`CT_MENUOPT=0 CT_MENUSCREEN=1`, `fnv32=B2B8628C`), both on
`saves/battle_re/gato_t0.raw.wram`.

### Status: 11 px, and every one of them was ONE GLYPH

`C2/F28E` builds the play clock straight into the tilemap, and writes the middle cell itself:

```
2F2B2  LDA $7D / AND #FF00 / ORA #002D / STA $0000,X
```

so the separator is **font tile 0x2D**, not the charset colon **0xE3** that `mCode(':')` returns.
They are two different glyphs in the same font — 0xE3 is the one the battle submenu strips use
(`field.c`'s own `':' -> 0xE3`, cited to the oracle strip tilemap) — and the oracle inks 0x2D one
pixel right and one down:

| | ours (0xE3) | oracle (0x2D) |
|---|---|---|
| rows | 176, 177, 180, 181 | 177, 178, 180, 181 |
| columns | 82..83 | 83..84 |

`TIME` and all four digits already matched pixel for pixel. The Save screen and the boot Load screen
had always drawn this tile directly (`mGlyph(h, fb, 0x2D, 80, y)`); the main-menu TIME box was the
last straggler.

### Item, defect 1: the list hand was arithmetic, one row high

Same shape as the Config screen's fix. The ROM copies the Item screen's OWN cursor graph
**FF/B53E -> `$7E:9380`** and `C2/E964` (`LDA $9380,X / STA $0770`) puts the sprite at record[cell]'s
first two bytes. Proven on the oracle's capture: its live `$7E:9380` is byte-identical to FF/B53E for
all 15 records, and its cursor OAM `$0770` = (0x18, 0x57) = record 0.

```
rec  0..9   (24, 87 + 12*row)     the ten list rows
rec 10/11   (40, 31) (136, 31)    the Use / Move labels
rec 12..14  (120, 99/139/179)     the target-select party cells
```

We computed `MI_ROW0 + row*MI_ROWH - 2` and inked y89..97 where the oracle inks y90..98 — identical
shape, identical x, off by one. The table is now baked (`ctItemNav`) and both hands come off it (the
Move pin keeps its measured `+2 / -1`).

### Item, defect 2: the scrollbar arrow was the wrong sprite in the wrong place

The oracle's OAM slot 31 is **(X 232, Y 88, tile 022, attr B0)** — a 16x16 OBJ in palette 0, VFLIPPED,
with the up arrow parked at Y 224 because the scroll is 0. That is exactly `$0D93` = 0x50 = 80 for the
up arrow and +8 for the down one. Tile 0x22 is the **ribbon cursor's own graphic** (`menu_arrow.h`,
ROM FF/9D70), an UP triangle — so attr 30 is the up arrow and attr B0 the down one — and OBJ palette 0
resolves to the same three inks palette 7 does (41,41,41 / 106,98,98 / 255,255,255; compare
`itemCursorPal` entries 13/14/15), which is why one baked bitmap serves both places.

We drew an **8x8 crop** of that tile instead, cut out of an oracle frame, which had sliced the tile's
dark OUTLINE away — the crop even kept the outline's colours, with alpha 0 — and placed it with a
`- 8` that confused the peek buffer's 8-line overscan HEAD (our y = buffer row − 8, i.e. **our y IS
the scanline, and OAM Y is our y**) with an OAM offset. Net: a bare white triangle eleven rows too
high. The crop is deleted.

### ⚠ NOT A DEFECT: the scrollbar BLINK PHASE

Against the original `t1` capture the arrow region still differs by 63 px. That is the blink, not the
render: `C2/8BB8` toggles the pair 8 frames on / 8 off (`$0D96` is a 3-bit down counter that flips
`$0D97` bit0) and **parks both sprites at Y 224 while off**. Verified by stepping the oracle 4 / 8 / 12
frames further on:

```
 + 4 f: $0D96=04 $0D97=00   OAM $0778..077F: 00 E0 22 30  00 E0 22 B0     (both parked)
 + 8 f: $0D96=00 $0D97=00   OAM $0778..077F: 00 E0 22 30  00 E0 22 B0     (both parked)
 +12 f: $0D96=04 $0D97=01   OAM $0778..077F: 00 E0 22 30  E8 58 22 B0     (down arrow live)
```

Compared against the `+12` capture — the same blink phase our frame is in — the Item screen is **0
differing px**. A pixel check of this screen must be taken in a matched phase.

### Where the four screens stand

| screen | before | now |
|---|---|---|
| Status | 11 | **0** |
| Item | 110 | **0** (matched blink phase) |
| Tech | 547 | **0** |
| Config | 966 | **0** |

Form and Save are still outside this table: the drive that reaches them in the oracle leaves the
ribbon box over the sub-screen. Save is pinned separately by `menusave-ow-open`.

---

## 2026-09-07 — FORM (screen 5): 617 -> 236 px, and what the last 236 are

Pinned by `menu-form-screen` (`CT_MENUOPT=4 CT_MENUSCREEN=5 build/Belthasar.exe --assets ct_assets.dat
--menutest saves/battle_re/party3_form.raw.wram`, `fnv32=EC77B80C`).

### ⭐ THE GATE IS WHY FORM WAS MISSING FROM THE TABLE AT ALL

`C2/C627` refuses to open screen 5 unless `$0F0B + $0F0C >= 2`. On every two-member state this repo
owns, Crono is locked by `$7F01DF` so exactly ONE member is exchangeable — the ROM bounces to the
ribbon and **there is no entered screen to capture**. The earlier note blamed "the drive still carries
the ribbon box over the sub-screen"; that was the symptom of the refusal, not a property of the drive.

`saves/battle_re/party3_field.state` has THREE members (Crono locked, Lucca and Frog free), so
`$0F0B = 2` and the gate OPENS. Oracle capture: `--peek` driving `X, Right x4, A`.

### Three defects

**1. The hand sat on a panel the ROM skips.** `C2/C649`:

```
2C649  LDA $0F02,X / AND #C0 / BNE ->loop / STX $54
```

over `C2/CD2B`'s compacted panel array — skip LOCKED (0x40) and EMPTY (0x80). With Crono locked the
screen opens on cell 1, and the oracle's cursor OAM `$0770` = (0x38, 0x53) is nav record 1. The port
HAD this scan, but only on the A-handler path, so a forced render opened on cell 0. It is now one
helper, `CtMenuFormEntryCell`, used by all three sites (the A-handler, the `--menutest` self-test, and
the `CT_MENUSCREEN` force).

**2. The hand's row base was one low.** The oracle's live `$7E:9380` for this screen:

```
rec 0..2   (0x38, 0x23) (0x38, 0x53) (0x38, 0x83)   = (56, 35 + 48*r)   the party cells
rec 3..6   (0xE8, 0x23/0x53/0x83/0xB3)              = (232, 35 + 48*r)  the RESERVE column
```

`C2/CB18` (`LDA #38 / STA $9380 / STA $9386 / STA $938C`) is where the x comes from. We used 35+1.
This table is **built, not copied** — the reserve cells' neighbour bytes depend on the roster — so
the base is cited from the oracle's RAM rather than baked, unlike the Item screen's `FF/B53E`.

**3. The Exchange icon was the grey face, sixteen pixels left.** It is the ribbon icon's HIGHLIGHT
variant (base tile + 0x20 = `ctMenuIconSel[4]`), which the oracle inks red/yellow/green/blue, and its
origin is `MI_HX + 80`: identical 14x14 shape, identical rows 177..190, ink x89..102 where we inked
x73..86. It is a BG widget — this screen's OAM carries only the hand and the three party sprites — so
per this file's own rule the position comes off the FRAMEBUFFER (per-window HDMA scroll makes
`tilemap*8 + scroll` meaningless). The `Exchange` run itself was already exact: x104..165, rows
184..191, run for run.

### The 236 px that remain: the cursor member's sprite is ANIMATED

`$7E:1803 + charId*0x40` is the pose byte. For the CURSOR's member it cycles — measured at
+10/+20/+30/+50/+60 idle frames on Lucca: `0B 0C 0E 0B 0C` — while every other member holds a static
idle pose (`00` Crono, `01` Lucca/Frog). Our static pose is **byte-exact for every non-cursor
member**: move the oracle's cursor down one cell and it is FROG's sprite that differs while Lucca's
matches pixel for pixel.

Reproducing it needs the menu sprite animator (still deferred) **and** a matched animation phase —
the same double requirement the Item scrollbar's blink has.

### ⚠ TWO BOUNDARIES THIS SCREEN EXPOSED (measured, not fixed)

**(a) The menu RECOMPUTES attack/defence at open, and we read the sheet raw.** `C2/9137` MVNs 11
bytes from `$7E:9B1C` into `sheet + 0x36` (writer `C2:9160`), and `C2/916E` builds that block out of
the equipment — four passes through `C2/9261`, clamped against `FF/CF33`, then a per-charId pass
through the `C2/91DB` jump table. A raw FIELD capture carries stale values: on `party3_field`, Frog's
`+0x3D` reads **229** in the field where the menu computes **18**. This is the same
`C1/CF15` + `FD/B033` stat engine `battle.c` already names as a boundary. It is why
`menu-form-screen`'s fixture is the oracle's POST-OPEN WRAM: the check grades the render, not the
stat pass, instead of pinning a frame that shows the symptom.

**(b) The chrome tilemaps are CAPTURES, so `C2/CC4F`'s per-panel window dim is frozen into them.**
`C2/CC4F` walks `$0F02,X` and, wherever `entry & 0x00C0` is non-zero (LOCKED **or EMPTY**), rewrites
6 tiles x 2 rows of that panel's attributes (`C2/CCB3` + two `C2/EC93` passes). Our `ctMenuChromeRibbonTM`
/ `ctMenuChromeEnteredTM` are baked from oracle frames, so they carry whatever dim the capture's roster
produced. Visible on the ribbon's opt-4 HOVER with a ONE-member party (`saves/truce_market.state`):
the oracle dims the empty panel's whole window one half-math step and we do not —

```
             y66            y72            y80            y88            y94
  ours   ( 90, 82, 82)  ( 90, 99,107)  ( 74, 82, 90)  ( 41, 49, 57)  ( 41, 49, 57)
  oracle ( 74, 74, 74)  ( 66, 74, 74)  ( 49, 49, 57)  ( 16, 24, 24)  (  8, 16, 16)
```

454 px. With a THREE-member party (no empty panel) that band difference is **gone**, which is what
identifies it as the empty/locked attribute rewrite rather than a gradient bug. Fixing it means
porting `C2/CC4F` over a *derived* chrome instead of a captured one — a separate piece of work.

### Where the six screens stand

| screen | differing px | note |
|---|---|---|
| Status | **0** | |
| Item | **0** | matched scrollbar blink phase |
| Tech | **0** | |
| Config | **0** | |
| Form | **0** | the menu sprite animator is PORTED -- see the 2026-09-07 animator section |
| Save | **0** | whole frame, with a real save file -- see the 2026-09-07 Save section |

---

## 2026-09-07 — THE MENU SPRITE ANIMATOR (C2/F643): PORTED, Form reaches 0 px

`C2/F643` spawns ONE task in slot 0x28 (`LDA #F657 / LDX #0028 / JSR C2/8249`). `C2/F657` walks the
nine sprite blocks at `$7E:1800 + 0x40*n` (D += 0x40 until D == $1A40) once per frame:

```
2F662  LDA $00 / BMI ->next             ; +0x00 = charId, bit7 = no character
2F666  LDA $18 / BMI ->next             ; +0x18 bit7 = block disabled
2F66A  LDA $11 / CMP $12 / BNE ->adv    ; +0x11 = REQUESTED anim, +0x12 = the one PLAYING
2F670  CMP #00 / BEQ ->draw             ; both 0 -> not animating at all
2F674  DEC $13 / BPL ->draw             ; +0x13 = the frame countdown (SIGNED)
2F678  LDA $0D73 / CMP $0D75 / BCS ->draw   ; a per-frame budget on advances
2F680  JSR C2/F69B
```

and `C2/F69B` advances:

```
2F69B  INC $16                          ; +0x16 = frame index in the animation
2F69D  LDA $11 / CMP $12 / STA $12      ; commit; Z = unchanged
2F6A3  BEQ ->2F6F7                      ;   unchanged -> keep the same tables
2F6A5  STZ $16                          ;   CHANGED -> restart at frame 0, re-point the tables
2F6A7  LDA $17 / ASL A / TAX / LDA $14 / JSR ($F6D8,X)  ; kind -> [0, v, 2v, 3v] over +0x14
2F6B4  LDA $11 / AND #007F / ASL A / ASL A / STA $1E    ; animId * 4
2F6BD  ADC $1B / ADC $0A / STA $1B      ; POSE tbl = resolve(+0x14) + animId*4 + word(+0x0A)
2F6C3  LDA $1E / CLC / ADC $0C / STA $1E ; DUR  tbl =                animId*4 + word(+0x0C)
2F6F7  LDY $16 / LDA [$1E],Y / BEQ ->end ; the frame's DURATION, 0 terminates
2F6FD  STA $13
2F6FF  LDA [$1B],Y / CMP $03 / BEQ ->2F750 / STA $03    ; +0x03 = the ASSEMBLY FRAME (+ its DMA)
2F750  SEP #30 / DEC $13 / RTS          ; ⭐ BOTH exits decrement AGAIN
```

⭐ that second `DEC $13` is why a pose lasts exactly `duration` frames and not duration+1 — and it is
invisible in any single settled frame, which is why the check pins a run-length `menuspr:` sequence
and not only a still.

### ⭐ THE TABLES ARE THE CHARACTER'S ORDINARY WALK CYCLE

Read out of the oracle's own live blocks, Lucca's pointers resolve to `$E4:39A0` (poses) and
`$E4:AA24` (durations). Those ROM bytes are

```
E4/39A0: 0A 0B 0C 0D 0E 0F 00      E4/AA24: 07 07 07 07 07 07 00
```

which is **byte for byte** `CT_ASSET_SPRITEANIM[2]` anim 1's D-facing column and its delays. Same for
Crono (`E4/312C` = 04 05 06 07 08 09), Frog (`E4/427C`) and for anim 0x0B (Lucca `E4/39C8` =
01 87 4A / `E4/AA4C` = 0A 05 0F). So the menu plays the character's WALK CYCLE FACING DOWN, block+0x03
is the same assembly-frame index the field sprite compositor already takes, and anim 0 frame 0 IS the
standing frame the host was already baking. Confirmed end to end by forcing our compositor to each
frame in turn (`CT_FORMPOSE`) against the oracle's own captures: a clean 0-px diagonal, ours 0x0B..0x0E
against its RAM poses 0x0B..0x0E.

### WHO ASKS (C2/C973 -> C2/C997) — only the Form screen

```
2C975  LDA #08 / STA $00 / LDY #00 / (loop) LDX $00 / JSR C2/C997 / DEC $00 / BPL   ; everyone STILL
2C984  LDY #01 / LDX $0F0D / JSR C2/C997        ; the CURSOR cell -> anim 1 (walk)
2C98C  LDA $0F00 / BPL / LDY #0B / LDX $0F00 / JSR C2/C997   ; the PICKED cell -> anim 0x0B
2C997  LDA $2980,X / BMI ->rts / ... / STA $1811,X
```

⚠ `C2/C997` indexes `$2980` by the CELL, while the panel row shows `C2/CD2B`'s COMPACTED entry: on a
party with a leading hole the two disagree. The port reproduces the ROM's own indexing rather than
"the character under the hand"; no oracle state this repo owns has such a hole, so that case is
unverified either way.

### THE ONE-FRAME DISPLAY LAG, and how the comparison is anchored

`STA $03` sets the pose and the DMA that follows lands on the NEXT frame, so the oracle's SCREEN lags
its own pose byte by exactly one frame. MEASURED, K = frames after the screen opened:

```
K=14 RAM 0B -> screen 0B     K=15 RAM 0C -> screen 0B     K=16 RAM 0C -> screen 0C
```

Our renderer composes the pose in the same frame, so the phases line up as **oracle frame 81+T against
our menu frame T**. Nothing else in that mapping is free.

### RESULT

**0 differing px over the whole 256x224 at SIX phases spanning a full walk cycle including the wrap**
(T = 20, 27, 34, 41, 48, 55), and 0 px at three phases of the PICKED member's anim 0x0B
(T = 20, 26, 33).

### ⭐ CORRECTION: the picked member's SECOND HAND is real

An earlier note called it invention -- "the ROM marks the pick with the character's SPRITE POSE, not
with a second cursor". The pose part is right; the conclusion was not, because the ROM does BOTH and
the second hand BLINKS, so a capture in the off half shows nothing. With a member picked:

```
OAM slot 28 = (56, 83) tile 020     the nav cursor
OAM slot 29 = (58, 82) tile 020     the PICKED member's hand -- (+2, -1), the offset the old code had
$7E:0774 alternates 3A52 <-> 00E0 (parked at Y=224) every FOUR frames, in step with $7E:0DA0
```

i.e. the same 4-on/4-off counter the Item screen's pinned-row hand already uses. Restored, with the
blink.

### WHERE THE STATE LIVES (a deliberate deviation, stated)

The ROM keeps the blocks in `$7E:1800 + charId*0x40`. The port keeps them host-side: our menu runs
OVER a live field WRAM that has to resume byte-identically, and `$7E:1800` is the FIELD's per-object
array in this engine (`CtObjSceneDefaults` writes `$1800,X`). The machine is the ROM's byte for byte;
only the storage moved.

NOT MODELLED, and named: the per-frame advance BUDGET (`$0D73` vs `$0D75` — with at most three
animating blocks the menu never reaches it); the bit7 loop-point `$21` (only reachable for an anim id
with bit7 set, and the menu's two callers ask for 1 and 0x0B); and the VRAM DMA at `2F707`, which is
how the ROM gets the frame's tiles on screen — we compose the frame instead.

---

## 2026-09-07 — SAVE: the overwrite MESSAGE, and the whole screen at 0 px

User report (translated from Portuguese): "the save-overwrite message is still wrong". It was, and fixing it also opened
the first whole-screen comparison this screen has ever had.

### ⭐ THE OVERWRITE MESSAGE IS FIVE LINES, NOT FOUR

`$FF:C787` reads

```
0A 20 "Erase" 01 "old" 01 "save?" 01 FF FF "Yes" 01 FF FF "No" 0A 00
```

`01` breaks the line, so the question is THREE rows — `Erase` / `old` / `save?` — and the two answers
are indented. The port wrapped it as `Erase` / `old save?`, which ran the question off the window's
right edge. MEASURED off the oracle's own prompt frame (`--peek saves/com_jogo_salvo.state`, a state
that HAS a saved game, driving `X, Right x5, A`, then `A` on the used slot), ink row and first ink
column of every run:

| run | position |
|---|---|
| `Erase` | (188, 28) |
| `old` | (188, 40) |
| `save?` | (188, 52) |
| `Yes` | (200, 64) |
| `No` | (200, 80) |

The question rows are 12 px apart and the answers 16 — the ROM's own spacing.

**THE HAND** comes from the same `$7E:9380` nav graph the slot list uses. The SAVE screen's live
records 3 and 4 are `B8 3F 04 04 03 03` and `B8 4F 03 03 04 04` = **(184, 63)** and **(184, 79)**, and
the oracle's cursor OAM slot 28 on the prompt frame is (184, 63) exactly. We had it at (176, 55).

**THE WINDOW** is now CAPTURED (`src/data/menu_saveprompt.h`, regenerate with
`tools/_gen_saveprompt.py`) instead of a half-math DARKENING of whatever was underneath. The ROM
draws it from the display list `$FF:C16F` (back up the 28x12-tile BG2 region, draw an 8x10-tile window
at tile (22,2) = px (176,16)), which this engine has no interpreter for — so it is captured with ONLY
BG2 composited (`PEEK_ISOBG=1`), which yields the chrome with no BG1 text and no OAM hand, and the
port still draws those itself. Seven colours, opaque: over the whole 64x80 the full oracle frame
differs from the BG2-only one in exactly the **764 px of text and hand**.

**0 differing px over the whole window, in BOTH cursor states.**

### ⭐ AND THEN THE WHOLE SCREEN: 0 px

The Save screen had never been compared whole, because `menusave-ow-open` runs SRAM-less: all three
slots read empty and the row text, era, TIME, gold, Save # and party panels are blank. Handing in
`saves/ct_real_save.srm` — whose slot 0 holds the SAME save `com_jogo_salvo.state` carries — makes the
frames comparable, and left exactly one defect:

**THE SLOT NUMBER WAS AN 8x8 GLYPH AT A FIXED x.** The ROM draws it through the same VWF the row text
uses, CENTRED in the button's first four tiles. Measured per digit, against those digits rendered
through this VWF at a known pen, with the ROM's own `C2/60E6` advances:

```
'1'  advance 4  ->  oracle ink x30..32     16 + (32 - 4) / 2 = 30
'2'  advance 7  ->  oracle ink x28..33     16 + (32 - 7) / 2 = 28
'3'  advance 7  ->  oracle ink x28..33     16 + (32 - 7) / 2 = 28
```

three digits, three exact hits, and the same centring expression the row text already uses in
x[80,208). (The tell was the glyph itself: the menu font's `1` has a base serif and is 7 rows; the
ROM's is a plain 8-row bar.)

With that, **the entire Save screen is 0 differing px against the oracle** — chrome, buttons, numbers,
chapter titles, hand, era, TIME, gold, Save #, and the party panels.

### A verdict that only stated one case

`menusave:`'s self-test asserted `used == 0`, which is only true SRAM-less, so it reported FAIL the
moment a real cart image was handed in — something the port can now produce, since the save-write
works. It now asserts that the reader AGREES with the ROM's own gate (`FF/F9C4`'s signature +
`FF/FBE7` checksum) slot by slot, which is true in both.

### Where the six screens stand

| screen | differing px |
|---|---|
| Status | **0** |
| Item | **0** (matched scrollbar blink phase) |
| Tech | **0** |
| Config | **0** |
| Form | **0** (matched animation phase) |
| Save | **0** (whole frame, with a real save file) + the overwrite prompt **0** |

---

## 2026-09-07 — EVERY MENU SCREEN, INCLUDING THE SIX RIBBON HOVERS, ON A THREE-MEMBER PARTY

The earlier passes compared the six ENTERED screens. This one compares **the ribbon's six hover
previews as well**, and does it on a party of THREE — every menu capture this repo owned before was a
one- or two-member party, so any defect that only shows on a second or third panel had nothing to
show up in. Oracle: `--peek saves/battle_re/party3_field.state` driving `X` + `Right xN` (+ `A` for an
entered screen); ours renders that state's own post-menu-open WRAM.

Starting point of the sweep: opt 0 **0**, opt 1 **63**, opt 2 **881**, opt 3 **600**, opt 4 **154**,
opt 5 **0**; entered 1 **0**, 2 **0**, 3 **1322**, 4 **0**.

### 1. The Tech screen dims NOTHING (881 + 1322 px)

We dimmed every party panel but the lead's, on the hover and on the entered char-select, and dimmed
the non-lead `MP Used` rows the same way. The oracle inks **all three panels at (255,255,255)** on
both, and still does with the cursor moved. The dim belongs to the Equip/Status char-select, which
tones the panel it stands on; on Tech it was an assumption written into the call sites as if derived
("char-select: the cursor member bright, others dim"). A SOLO party — the only one the Tech captures
used to have — has no second panel to give it away. `mPartyPanels` now takes `focusChar < 0` = dim
nothing.

### 2. The Item hover has no scrollbar (63 px)

The scrollbar is the SCREEN's widget (`C2/8BB8`, task slot 4) and `C2/8C0B` parks both sprites when
`$0D94` — the scroll max — is zero. On the oracle's hover `$0D92..$0D9A` are **all zero** and the OAM
carries only the six ribbon icons; on the entered screen the same bytes read `E8 50 E8 00 06 01 72`
and the down arrow is live. Not a blink phase: it stayed wrong at every idle count from +30 to +42.

### 3. The Config hover has no hand and a bare bar (600 px)

We drew cell 0's hand and the bar's icon + `Stereo` + `On`. The bar is `$0417` and its two strings are
staged by `C2/C4BC`, which the SCREEN's handler runs — the ribbon has not entered it. `cell < 0` now
marks the preview, the same convention Item, Form and Save already used.

### 4. ⭐ C2/CC4F's panel dim was FROZEN INTO THE CAPTURED CHROME (154 px, and 454 the other way)

```
2CC78  LDA $0F02,X / AND #00C0 / BEQ ->next     ; LOCKED (0x40) or EMPTY (0x80)
2CC80  TXA / JSR C2/CCB3 / LDX $04 / STX $61
2CC88  LDX #0006 / LDA $02 / JSR C2/EC93        ; SIX tile ROWS (C2/EC93 adds 0x40 per pass)
2CC90  LDA $04 / ADC #1000 / STA $61 / (again)  ; the map's other 32x32 page, which we do not model
2CCA0  LDA $04 / ADC #0180 / STA $04            ; 0x180 bytes = 6 rows = the next panel
```

and `C2/ECC2` writes only the tilemap ATTRIBUTE byte, so a dimmed panel differs from a bright one **by
the palette nibble alone**. That is exactly what our two captures disagree on:

```
rows 18-19   ctMenuChromeRibbonTM[4]   13E2 13EE 13EF ...   palette 4
             ctMenuChromeEnteredTM[4]  03E2 03EE 03EF ...   palette 0
```

same tiles. The ribbon map was captured on a TWO-member party whose panel 2 was empty, so replaying it
dimmed panel 2 for **every** party (154 px wrong with three members) and failed to dim anything with
one (454 px wrong — the earlier note's unexplained "empty panel window" difference, now named). The
dim is re-applied from the LIVE panel array over the captured map, restricted to the window's own
tiles (0x3DC-0x3EF, 0x300-0x313) because the ribbon box and the marble are laid on top afterwards and
keep their palettes. GEOMETRY, confirmed on both rosters: **panel r covers tile rows 2 + 6r .. 7 + 6r,
columns 8..23**.

### Result

| view | opt 0 | opt 1 | opt 2 | opt 3 | opt 4 | opt 5 |
|---|---|---|---|---|---|---|
| ribbon hover | **0** | **0** | **0** | **0** | **0** | **0** |
| entered screen | **0** | **0** | **0** | **0** | **0** | **0** |

Entered 5 (Form) is 0 at six animation phases; entered 6 (Save) is 0 whole-frame with a real save file
and its overwrite prompt is 0 in both cursor states. The Form hover is also 0 on a ONE-member party,
which is the other half of the C2/CC4F mechanism.

Pinned: `menu-hover-item`, `menu-hover-tech`, `menu-hover-config`, `menu-hover-form`,
`menu-tech-screen-party3` — 6 mutations, 6 killed.
