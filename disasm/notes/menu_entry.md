MENU-ENTRY RE DOSSIER (X-button menu) — all claims cite ROM disasm addresses (tools/dis.py, flat offsets) or observed oracle RAM/fb (ct_probe.exe --peek/--monitor/--read). Artifacts in scratchpad = C:/Users/ICAROF~1/AppData/Local/Temp/claude/c--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/0659808f-a8c2-4887-9cf9-157a9540e206/scratchpad/ (SP below).

== 1. BUTTON: X opens the menu (CONFIRMED) ==
--peek gato_press_action.state --btnscript '200:8,0:200' -> SP/mm_menu.png: full menu (Crono LV1 HP 70/70 MP 8/8; Marle LV1 HP 65/65 MP 12/12; portrait top-right; floating icon-selector box; TIME 08:28; 400G). Baseline '0:208' (SP/mm_field.png) stays on the field. Button-layout sweep (--monitor 00F0:8, btnscript Y,Start,Sel,B,A,X): CT $00F0 (edge)/$00F2 (held) layout = Start:0x01 Select:0x02 Y:0x04 B:0x08 X:0x40 A:0x80; $00F6 = REMAPPED edge byte (X->0x40 identity; Y->0x01, Sel->0x04, B->0x0A — the C2/8545 controller-config remap, layout not fully mapped).
ROM check: field system-button handler C0/18D9 (called from the field mainloop C0/0097-00BD at C0/00A1; DP=$0100 set at C0/007F-0082):
- C0/18DC `LDA $00F0; BIT #01` = Start -> PAUSE loop. **CORRECTED 2026-07-24: $0119 is the SCREEN
  BRIGHTNESS shadow, NOT a music volume** (see the `$0119` correction block at the end of
  `battle_menu_synthesis.md`). Pause therefore DIMS THE SCREEN, byte-exact at C0/18E8:
  `LDA $19 / STA $1A` (save) `/ LSR A / STA $19` (halve) `/ LDA #FF / STA $0407`, then spin
  `JSR C0/EC60 / LDA $00F0 / BIT #01`, re-halving from the saved copy each pass
  (`18FE LDA $1A / LSR A / STA $19`), and on the second Start `190F LDA $1A / STA $19` restores
  full brightness. Gates confirmed: `$0111==0 && $011F!=0` (18E0/18E4).
- C0/1916-191D `LDA $00F6; BIT #01` (= Y) -> C0/1985: if $0126!=0 toggle it EOR #03; else if storyline $7F0000 >= 0x49 -> menu MODE 5 (C0/19A2-19CE, `LDA #05; JSL C28000`). Untestable here (storyline=08); mode-5 screen id $67=07 UNIDENTIFIED.
- C0/1920-1925 `LDA $00F6; BIT #40` (= X) -> C0/192F: the X-MENU ENTRY.
Entry gates (C0/192F-193D, DP=$0100): $011F != 0 (explore on) AND $0162 == 0 (no dialogue/event lock) AND $0110 == 0 (unlabeled). Confirmed empirically: candy_shop_lake.state loads with $011F=00 -> X at f0..f97 REFUSED (SP/mm_fair_menu2.png still field); --monitor 011F:1 shows $011F 0->1 at f98; X after 120-frame settle opens the same menu (SP/mm_fair_menu3.png, TIME 01:43).

== 2. TIMELINE + MODE SWITCH ==
$0100 NEVER changes: 0x0006 pre/in-menu/post (wram reads). THE MENU IS NOT A C0:0066-DISPATCHER MODE — it is a SYNCHRONOUS `JSL C2/8000` from inside the field mainloop (C0/1960). Timeline (X held f1-f8, gato state):
- f1-f15: SCREEN fade-out $0119 0F->00, 1 brightness step/frame, writer C0:194B ($0119 = INIDISP shadow, NOT volume -- the actual music suspend is the JSL C7/0004 command block in section 3) (loop C0/193E-1956: DEC $19, JSL FDC2C1 sound tick, JSR C0/EC60 frame wait), $0407=FF.
- f16: C0/1958 `JSR C0/0B4E` (PPU quiesce: SEI, $2100=80 force blank, $4200=0, $420B/C=0) then `JSR C0/011B` = FIELD-STATE SAVE: $0102/0103/0104 <- lead tile X/Y/facing from $1801,Y/$1881,Y/$1600,Y, Y=$0197 (monitor writer C0:013D; observed 09,0D -> 08,0A); then for the 3 party slots ($0197/99/9B): $1800,Y->$7F1D09+X, $1880,Y->$7F1D0F+X, $0C00,Y->$7F1D15+X (C0/013D-0165); camera $01AB-AD -> $7F1D1B-1D (C0/0167-0177); scroll accumulators $7F3728/3748/3768/3781 -> $7F1D1E-25 (C0/0179-0197); $1DF9 -> $7F1D26 (C0/019D). Observed save block: 60 08 90 08 .. 4F 0A 4F 0A .. 20 20 20 20 .. 68 59 00.
- C0/195E-1960: TDC; XBA (D=$0100 -> A=0x0001); `JSL C28000` = menu program, mode 1.
- Menu fully drawn by f48 (SP/mm_f48.png).
In-menu the field object RAM IS CLOBBERED: PC objs $1800/$1880/$0C00 read garbage ((0001,0101)/(72C0,6B40), prio 2E80/407E) — the $7F1D09 block is the only position truth.

== 3. MENU MODULE (bank C2) ==
Entry C2/8000: BRA table (+0 full menu; +2 -> C2/84D2; +4 -> C2/8C36 = the party-membership helper already RE'd as op0xD2). C2/800E-2808C: SEI, native mode, PHD/PHB/PHP, DB=$7E, A->$7E0A00 (MODE), X->$0A01; $2100=80 force blank, $4200=01, DMA/HDMA stopped; TSX -> $7E0920 SAVES THE FIELD STACK POINTER (observed 06F6); mode0+$7F0061==0 = title/new-game path (JSR C2/956E, $7F0061=1, clears SRAM $30:7FE1/2, JMP C2/832F); else LDX#09FF TXS = menu's OWN STACK at $7E09FF; C2/8096 init (MVN C2/8406->$0500 8 bytes; music-suspend cmds $1E00-02 + JSL C70004: 70, 18/FF/80, 19, 82/01/FF, 83); init chain C2/940D, 968D, D156, 984A, 8663, 816A, 92F4, F3CA, 9875 (VRAM/CGRAM builds live here); $0071<-$0A01; $0067 <- table C2/808F[mode] = [0E 00 0C 0D 0F 07 0B] (mode1 -> screen 00, observed $7E0067=00); $4200=81, JMP C2/8190.
MAINLOOP C2/8190-8215 = a COOPERATIVE TASK SCHEDULER: 8 slots of 8 bytes at $7E00B0 (status, delay ctr, entry PC $B2, saved SP $B4, saved $68 $B6); per-slot coroutine stacks carved at $0A3F+0x40/slot (observed SPs 0A34/0A76/0AF6); frame tick = $7E0D01 set by the menu's own NMI (spin C2/81CA `LDA $0D01; BEQ`); spawn C2/8249, yields C2/821E (sleep)/C2/822B (delay), kill C2/8216; initial tasks C2/826F (input; joypad read/remap C2/84EC-8545 — the --checkjoy routines) and C2/E91B (render; C2/E93F vsync spin — dominates the instruction trace SP/menu_trace.txt). Menu callers of C2/8000 (ROM scan): C0/0D18 (boot/title, A=0 or ($25 ror)&3), C0/1960 (field X, A=1), C0/19CE (field Y storyline>=0x49, A=5), C2/2552 (battle module: A = ($027C==5) ? 1 : 5 — purpose unconfirmed), F2/75D0 (bytes look like data — likely false positive).
VRAM (diff mm_base16.raw.vram vs mm_menu.raw.vram): the menu REWRITES ESSENTIALLY ALL VRAM (20-97% of every 4KB block; blocks 4000-9FFF byte 87-97%). --ppuregs menu_open_gato2.state: mode 1, BG1 map word 5800 chr 0000, BG2 map 6000 chr 0000, BG3 map 6800 chr 7000 (menu font), OBJ tiles 4000/5000. OAM (ct.py oam): only 8 live sprites — the icon-selector strip (6x 16x16 tiles 024/006/008/00A/00C/00E at Y=112, X=80-160, prio 3) + 2 v-flipped arrow sprites (tile 022). Portraits and all windows are BG tiles, not sprites.
WRAM: menu scratch = $7E0000-1FFF (partial, incl. the $1800 object page), $7E3000-DFFF (heavy), $7F2000-2AFF (CLOBBERS the field event-script image). PRESERVED in place: $7E2000-2FFF (party/stats/inventory RAM — the menu edits it directly, only 4.3% changed), $7F0000-1FFF story flags (0 diffs), $7F3000+ mostly.

== 4. MENU EXIT (B) ==
B closes it; field resumes (SP/mm_exit.png shows the live field again, Gato song textbox running). Exit routine C2/82B4-82E0: LDA #FF JSR C2/8255 (palette fade), JSR C2/834D (rebuild the up-to-3 party members' gfx/stat blocks at $7E21BA+0x1A each from bank $D1 via $7E2980-82), JSR C2/83CA, force blank + NMI/DMA off, `LDX $0920; TXS` (restore FIELD stack — unique in ROM), PLP/PLB/PLD, SEI, RTL -> C0/1964. (ALTERNATE hard exit C2/8320-8349: stack $06FF + `JML C00000` = full re-dispatch through the C0:0066 loader — referenced at C2/E6AB; this is the path where $0100 matters, e.g. load-game.)
Field-side resume C0/1964-1984: C0/0B4E quiesce; C0/0B64 + C0/0B75 re-install the field NMI/IRQ trampolines (`JML` opcodes rebuilt at $7E0500/$7E0504 — the menu had its own handlers); D<-$0100; C0/01A5 = re-run the field LOAD chain (C0/0960, 6DCF, 7084, 7F7E, A33B, 09DD, 0A14, 56D4 = the full loader C0/00F4 MINUS C0/092B+C0/1B53), which REBUILDS all field VRAM and RE-DECOMPRESSES the event script into $7F2000 (LZ: literals via the $2180 WRAM port, back-refs via `MVN $7F,$7F` at C3/087A; observed writer C3:087D at f27 post-B; result byte-identical to pre — head 09 20 01 BA), then restores scrolls $7F3728/48/68/81 <- $7F1D1E-25 and $1DF9 <- $7F1D26 (C0/01DB-0205); C0/1A03 = PARTY RECONCILE: if $7E2980-82 != $0194-96 (party changed in menu) respawn PC objects via C0/597D for each of $018D-$0193, update $0194-96; then ALWAYS restore the 3 PCs' $1800/$1880/$0C00 <- $7F1D09/0F/15 (C0/1A6F-1A96) and camera $01AB-AD <- $7F1D1B-1D (C0/1A98-1AAB); C0/B192; C0/2824 = SCREEN fade-IN ($0119 0->0F, 1 brightness step/frame, observed f33-f47 post-B, writer C0:2836) -- $0119 is INIDISP, see the CORRECTION block in battle_menu_synthesis.md; STZ $0407.
VERIFIED byte-exact restore (pre vs post-exit wram): PC pos (0860,0A4F)/(0890,0A4F) prio 2020/2020; camera $01AB-AD = 68 59 00; loc $0100=0006; $0102-4 keeps the entry-time snapshot 08,0A,00; script head identical; $7E3000/B000/F000 pages 0.0% diff (deterministically rebuilt); residual 2% in $7E1000 page = advanced NPC anim/timer state (NPC/event VM state is NOT snapshotted — objects keep their live RAM, only the 3 PCs + camera + scrolls are restored; menu debris persists in VRAM/WRAM regions the field does not use, e.g. VRAM E000-FFFF).

== 5. REUSABLE STATES (LakeSnes, in SP) ==
field_pre_menu2.state (gato field f2, pre-menu), menu_open_gato2.state (menu open, loc 006 ctx), menu_open_fair2.state (menu open, loc 1B7 ctx), field_post_menu2.state (field resumed after B). Raw dumps: mm_field/mm_menu/mm_exit/mm_base16/mm_fair_menu3 .raw(+.wram/.vram/.oam) + PNGs; pre-WRAM mm_pre.wram.bin.

== STRUCTURAL SUMMARY for the C port ==
The X-menu is NOT a scene/loc transition: the field mainloop itself (C0/18D9 branch) fades the MUSIC only (screen switch is a hard force-blank cut), saves a tiny canonical block ($0102-4 lead tile/facing; $7F1D09-26 = 3x PC pixel pos + prio, camera $01AB-AD, 4 scroll accumulators, $1DF9), swaps stack+NMI ownership to the bank-C2 menu program via JSL C2/8000 (A=mode, 1=X-menu), and on RTL rebuilds the ENTIRE field presentation from ROM (VRAM + $7F2000 script image re-decompressed) then overlays the saved block. Field-state preservation = "reload the level, restore 3 positions + camera", not a RAM snapshot.

## Key addresses
- C0/0097-00BD field mainloop (DP=$0100 via C0/007F TCD)
- C0/18D9 system-button handler: 18DC Start($F0 bit0)=pause, 1916 Y($F6 bit0)->C0/1985, 1923 X($F6 bit6 0x40)->menu
- C0/192F-195B X-menu entry: gates $011F!=0 && $0162==0 && $0110==0; SCREEN fade-out DEC $0119 @C0/1949 (writer C0:194B) -- $0119 = INIDISP brightness, not volume
- C0/1960 JSL C2/8000 with A=1 (TDC;XBA of D=$0100)
- C0/19CE JSL C2/8000 A=5 (Y path, storyline>=0x49)
- C0/011B-01A4 field-state SAVE: $0102-4=lead tile/facing; $7F1D09/0F/15=3x $1800/$1880/$0C00; $7F1D1B-1D=cam $01AB-AD; $7F1D1E-25=$7F3728/48/68/81; $7F1D26=$1DF9
- C0/0B4E PPU quiesce; C0/0B64/C0/0B75 NMI/IRQ JML trampolines at $7E0500/$7E0504
- C2/8000 menu program entry: A->$7E0A00 mode, X->$0A01; field SP saved to $7E0920; menu stack $09FF
- C2/808F mode->initial-screen table [0E 00 0C 0D 0F 07 0B] -> $7E0067 (mode1=screen 00)
- C2/8190-8215 menu mainloop = cooperative task scheduler ($7E00B0 slots, tick $7E0D01, stacks $0A3F+)
- C2/8249 task spawn; C2/821E/822B yield; C2/8216 kill; tasks C2/826F input (joypad C2/84EC-8545) + C2/E91B render (C2/E93F vsync spin)
- C2/82B4-82E0 menu EXIT (B): C2/8255 fade, C2/834D party-gfx rebuild from bank D1, LDX $0920:TXS restore field stack, RTL
- C2/8320-8349 alternate hard exit: stack $06FF + JML C0/0000 (re-dispatch; ref C2/E6AB)
- C0/1964-1984 field resume: 0B4E,0B64,0B75, C0/01A5 reload chain (=C0/00F4 minus 092B/1B53) + scroll restore, C0/1A03 party reconcile + PC pos/cam restore (C0/597D respawn if $7E2980-82!=$0194-96), C0/B192, C0/2824 SCREEN fade-in (writer C0:2836)
- C3/087A MVN $7F,$7F / $2180-port LZ decompressor re-fills $7F2000 script image on exit (writer PC C3:087D)
- RAM: $00F0 edge/$00F2 held/$00F6 remapped-edge joypad (X=0x40, A=0x80, B=0x08, Y=0x04, Sel=0x02, Start=0x01); $0119 SCREEN BRIGHTNESS shadow (NMI C0/EC4E writes it to INIDISP $2100, 0 -> forced blank #80) -- NOT a music volume; $0407 sound mailbox [$0407's own label is UNVERIFIED, inherited from the same pass that mislabelled $0119]; $0100 loc UNCHANGED across menu

== 6. ADDENDUM (C-port pass): LV / TIME / GOLD pinned ==
- CHAR-SHEET LEVEL = sheet +0x12 ($7E2600+id*0x50). Pinned by elimination on the two observed-LV1
  sheets in mm_menu.raw.wram (screen shows Crono LV1 + Marle LV1): bytes ==1 in Crono's sheet =
  {+0x12,+0x29}, in Marle's = {+0x00,+0x12}; intersection = {+0x12} (unique). Cross-checked in
  mm_fair_menu3.raw.wram (both 1). Corroboration: the never-joined Lucca sheet has +0x12 = 02 with
  +0x13 = 0x14 (XP 20) — level-consistent data, not a party byte.
- PLAY CLOCK = $7E0400-0406: [+0 frames][+1 seconds][+2 min units][+3 min tens][+4 hr units]
  [+5 hr tens][+6 overflow]. Writer C2/85AE (monitor writer-PC C2:85BC, 1/frame in the field):
  `PEA $0400/PLD; LDA $07 BNE rts` = PAUSED while $0407 != 0 (the X-entry's $0407=FF halts it;
  C0/2844 STZ $0407 resumes); INC-with-carry through the limit table C2/85D0 = 3C 3C 0A 06 0A 0A
  (60,60,10,6,10,10); all six wrapped -> $0406=1. Verified: mm_pre -> mm_field (+208 frames) =
  55s28f -> 58s56f exactly; in-menu the tick continues after a ~30f pause (26f vs 56f at f208).
  The menu's "TIME hh:mm" = HOURS:MINUTES: C2/F28E defaults $22/$23 = 63/3B (the 99:59 cap, taken
  when $0004,Y = $0406 != 0), else $23 = $0403*10+$0402 and $22 = $0405*10+$0404 via C2/F2CC
  (tens*10+units; Y=$0402, INY INY between calls). $0022/$0023 are only the menu's DISPLAY cells
  (stale 07:00 in mm_field, rewritten 08 1C at menu open, writer C2:F2A7) — not the counter.
- GOLD = $7E2C53, 24-bit LE. ROM-wide operand scan: $2C53 has the full read/modify/write cluster —
  C2/FCD0 LDA (menu display read), C2/FD08 SBC (shop spend), C2/FDA1..FEDB STA writers (add/sub
  gold helpers), C1/D0B3/D0DB/D11B STA (battle reward) — while the OTHER 400-valued candidate
  $7E27F6 has ZERO ROM references (it is inside charid-6's $2600-block sheet image, a save-data
  copy). Both mm_menu (400G on screen) and mm_fair_menu3 read 400 at $2C53.
- C port: src/menu.c (CtMenuPanelData/CtMenuRender/CtMenuClockTick) + the field.c menu phase
  machine (entry gates C0/192F, save C0/011B, restore C0/1A03, both fades frozen-synchronous per
  C0/193E + C0/2824); selfcheck `Belthasar --menutest` (regress: field-menutest-gato).

## Open questions
- Menu mode 5 (initial screen $67=07, Y button + storyline>=0x49; also battle-site C2/2552 A=($027C==5)?1:5): which screen is it (Epoch/world-map? save?) — needs a storyline>=0x49 oracle state to open it
- Which routine inside the C0/01A5 reload chain (C0/0960/6DCF/7084/...) invokes the C3 LZ script re-decompress — writer PC C3:087D observed, the JSR/JSL caller not pinned
- $0110 (third menu gate byte, DP $0100) semantics unlabeled — always 00 in both test states
- The C2/8545 button-config remap table producing $00F6 (Y->0x01, Select->0x04, B->0x0A observed) not fully mapped
- ~~Start-pause path C0/18DC (music volume halve...)~~ CORRECTED: it halves the SCREEN BRIGHTNESS ($19->$1A, LSR), restoring it on the second Start (C0/190F); disassembled 2026-07-24, see the CORRECTION block in battle_menu_synthesis.md
- F2/75D0 'JSL C2/8000' ROM hit sits inside data-looking bytes — probable false positive, unverified
- Menu sub-screens (Item/Tech/Status/save) and their VRAM DMA sources (which ROM banks feed font/chrome/portrait tiles) not traced — only the top-level screen was profiled
- Menu modes 2/3/4/6 (screens 0C/0D/0F/0B) reachable via C0/0D09 (A=($25 rotated)&3) — call contexts unidentified
