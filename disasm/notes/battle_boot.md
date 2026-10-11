# BATTLE MODULE BOOT — RE dossier (GATO fight, loc 006, storyline 08)

Every claim below is anchored to a ROM disassembly address (tools/dis.py, file-offset = (bank-0xC0)<<16|addr) or an observed oracle value (ct_probe.exe --peek/--monitor --writer/--trace from gato_press_action.state, btnscript "100:8,0:592"). Writer PCs from --monitor --writer are the PC *after* the store retires (verified: FD:AE83 reported for the STA $0000,Y at FD:AE80). All captures in the scratchpad (p000/p300/p600/p900.raw + .wram/.vram/.oam, p600.state, btl_trace.txt).

## 0. Handoff & boot timeline (frames after the A-press)

CT battles run IN PLACE on the live field scene (loc $0100 stays 006 the whole time; --followscene shows 1 row). The field engine calls the battle module SYNCHRONOUSLY:

- C0:188C..18CD: battle-request path: JSR C0:024C, JSL C0:2C41 (the field's layer scroll: one tick on C0/0918's copy -- see the end of this note), TSB/TRB $53 + JSR C0:00DE + JSR C0:EC60 (fade), then **JSL C1:0000** at C0:18A7 — the only field-side caller (byte-search 22 00 00 C1 → C0:18A7 + EB:F6EB) — and on return re-inits the field (C0:0B64/0B75, D=0100, C0:0283/28E1/E935/00EB).
- C1:0000 = the battle module's public jump table: +0→C1:001B→JML CF:FB65 (BOOT); +6→JML CF:FBE5; +9/+C→JML CF:E300 / CF:E58A (per-frame HUD upkeep blocks); +0F→JML CC:E000; +12→C1:10E3 (per-frame tick).

Observed boot timeline (--monitor "0500:8,AEFF:3,AF10:1,B163:8,9E:1,29C4:4,0D04:4,6130:4,5E30:4" --writer):
- f0: field NMI/IRQ trampolines at $7E0500/$0504 = JML C0:EA63 / JML C0:ECCC; event battle-pack already staged at $7E29C4 (`B3 00 40 02`).
- f221: C0:0504 consumes/clears the $29C4 pack (inside the C0:188C handoff).
- f223: CF:FB65 entered (STZ $9E at CF:FB7D); its JSL CD:0000 → **D1:F1D7 installs the battle interrupt trampolines**: $0500=JML D1:E77C (NMI), $0504=JML D1:FE97 (IRQ).
- f223-283: transition: battle OBJ gfx uploaded to VRAM, HDMA channels armed (CD:09F1), HUD strip map + font uploaded.
- f284: **battle working RAM $7E2D00-$7EACEB cleared** — CF:FB92-FB9C: `REP #20 / LDX #7FEB / STZ $2D00,X / DEX / DEX / BPL` (observed writer CF:FB9A); CF:FB9E-FBA5 also zeroes DP $80-$EA.
- f286-288: stat staging + copies (§2), writers FD:A989 (clear), FD:B274 (PC seats), FD:B2B7 (enemy id), FD:AE83 (PC block), FD:B475 (enemy HP).
- f289: turn queue $B163 FF-init (C1:FBD4).
- f310+: HUD build task loop: $9E set by `INC $9E` at C1:007E, cleared by CF:E77D, one task per ~5 frames; C1:0080 = `JSL CD:0036 / LDA $9E / BNE` pump.
- f374: turn-order shuffle written to $B163 = `05 0A 04 00 09 06 07 08` (C1:808B; a permutation, it does not tick), $B16E gets $AF0D ids (C1:80A1/8090).
- ~f400: first Att./Tech/Item window opens (Crono), ~f500 Marle's; f~510+ fully interactive (cursor responds).

Boot continuation: CF:FB65 reads config $7E2989 (bit1→$EC, bit5→$B31F, bit4|0x20→$A5E3) and $7F00BA/$7F00B4+charid→$A09B (per-PC battle settings), then JML C1:8000 → C1:8006: JSL FD:A982 (staging), turn-queue init C1:8030-80A7 from $AEFF/$AEC6/$AEC7 via C1:AF22.

## 1. VRAM map (diffs p000↔p300↔p600, byte offsets; PPU bases identical field/battle per --ppuregs: BG1 map w6000/chr w1000, BG2 w6800/w1000, BG3 w7000/w5000, OBJ w0000)

- 0x0000-0x1400 & 0x3000-0x3BFF (p000→p300): OBJ chr uploads = battle poses. OAM (ct.py oam p600.raw.oam): Crono battle pose = 16x16 tiles 008/00A/00C/00E pal5 prio3; **GATO = tiles 020/022/024/026 pal6 prio3, 32x32 assembly at (164..180, 63..95)** (GATO tiles live at bytes 0x400-0x4FF ✓ diff region 0x403-0x4FF); tile 0x199 (byte 0x3320) in the 0x3163-0x337F diff. p300→p600 adds bytes 0x1800-0x2FFE (menu-phase OBJ: effect/digit tiles).
- 0xB018-0xBFEE (p000→p300): BG3 chr tiles 0x100-0x1FF = **the battle HUD font**, uploaded from ROM $FF:8C60 (§4).
- 0xF000-0xF7BF (p000→p300) = words 0x7800-0x7BDF: **the HUD strip tilemap** — a 32x32 map whose rows spell the menu: row2 `1A0 1CD 1CD 1E8`="Att.", row4 `1B3 1BE 1BC 1C1`="Tech", row6 `1A8 1CD 1BE 1C6`="Item" (twice, one per PC window), hand cursor tiles 160-163, name/HP text 15D-17B — all pal2, prio set, tile index = 0x100 + CT text code. In-battle text updates = the small 0xF040-0xF558 diffs (p300→p600).
- 0x6180-0x6793, 0x8A00-0x8DF1 (p300→p600): BG1/2 4bpp chr region touches (window chips/digits). The BG1/BG2/BG3 field tilemaps (0xC000/0xD000/0xE000) are NOT rewritten — battle plays over the live field map.
- CGRAM: BG palette row 1 swapped to the HUD gray/white set (field `0000 0C63 18C6 739C 18E8 10A6 10C6 0843` → battle `0000 1483 4A51 7FFF 0000 2506 35AB 4A51`); OBJ pal5=party, pal6=GATO.

**Which BG holds the HUD windows — the split mechanism (byte-exact):** PEEK_ISOBG isolation at f600: BG1 = the gray window BODIES, BG3 = ALL the text + hand cursor, OBJ = GATO/party only. The trick is HDMA, armed at CD:09F1-0AA0 (7 channels, all indirect, A-bank $7E, header tables at $7E:C867/C894/C8C1/C8EE/C91B/C948/C975, data bands in $7EBC85-$7EC1xx):
  ch0 $4300=44 (indirect, mode 4 = 4 consecutive regs) B=$2107 → rewrites BG1SC/BG2SC/BG3SC/BG4SC; ch1/ch2/ch3 mode 3 B=$0D/$0F/$11 → BG1/BG2/BG3 H+V scrolls; ch4 mode 4 B=$212C → TM/TS/TMW/TSW; ch5 mode 4 B=$2130 → CGWSEL/CGADSUB/COLDATA/SETINI; ch6 mode 1 B=$2105 → BGMODE/MOSAIC.
  Decoded ch0 table from p600.wram: scanlines 0-159 data `61 69 71 00` (field maps), **scanlines 160-207 data `78 78 78 78` — all four map bases → word 0x7800**; ch4: TM 0x13 (BG1+BG2+OBJ) → 0x05 (BG1+BG3 only) in the strip; ch5 streams per-line $2130-33 (`00 01 E8 ..`) = the window gradient; BGMODE stays 0x09 (mode1+BG3prio). So ONE tilemap at w0x7800 is rendered TWICE in the strip: BG1 reads the tile indices as 4bpp window-body chips, BG3 reads the same indices as 2bpp font glyphs — text over body, no map writes to BG1/BG3 field maps at all.
- The battle IRQ (D1:FE97) is NOT the split (HDMA is): it waits H-blank ($4212 bit6), then **forces blank early ($2100=0x80) to extend vblank DMA time**, does frame-parity work ($7C&1: JSR D1:FE4C = a $CA21/$CA24-gated countdown via CE:EE6E, else JSL FD:FFFD), and seeds the mul/div regs $4202-06 from dp $77-$7B (D1:FE71).

## 2. WRAM battle regions (diff p000↔p600 + writer PCs)

- **$7E2D00-$7EACEB = the battle engine's working RAM**, zeroed at boot (CF:FB97). Matches the giant diff span $2D00-$ACEC.
- **$7E2600 (persists, field): character sheets, 0x50 B/char**: +0 char-id, +1/+2 flags, +3 curHP u16, +5 maxHP, +7 curMP, +9 maxMP, then stats; Crono `00 86 80 46 00 46 00 08 00 08 ...` (HP 70/70), Marle at $2650 (HP 65/65), Lucca $26A0, slot3 $26F0.
- **PC battle blocks $7E5E2D + seat*0x80** (seat 0-2): 0x50-byte copy of the sheet — loader FD:AE52: `LDA $AEFF,Y`(char-id, BMI=empty) → dest ptr from table **FD:A80B = 5E2D 5EAD 5F2D | 5FAD 602D 60AD 612D 61AD 622D 62AD 632D (ELEVEN entries: 3 PC seats then 8 enemy slots -- this line used to list only six)** → copy loop FD:AE7D-AE8B `LDA $2600,X / STA $0000,Y` (writer FD:AE83). So $7E5E30 = Crono curHP (70; dropped to 61 when GATO countered for 9 — observed pre/post attack).
- **Enemy battle blocks $7E5FAD + slot*0x80** (slot **0-7**, not 0-5 -- FD/B2A9's staging loop is `CPX #0060` over 12-byte records = 8 passes, and FD/B869 indexes FD:A811 = FD:A80B+6 by slot; GATO = slot 3 → base $7E612D): loader FD:B438: id from `LDA $AF0D,X`, record offset = id*24-id = **id*23**, then `LDA $CC4700,X`: rec+0/+1 → curHP $5FB0/B1,Y AND maxHP $5FB2/B3,Y (writers FD:B460-B475), rec+2 → $5FBF,Y, rec+3..7 → $5FCF+,Y, rec+8+ → the stat bytes at block+0x37 ($6164). Layout at $612D: +0 id (0x92), +3 curHP u16, +5 maxHP (0x4C/0x4C = **76/76**), +0x37.. stats `01 08 08 64 06 32 07 7F 04 04 04 04`. Empty slots: id FF, HP 1/1.
- **ROM provenance (byte-exact): enemy stats table $CC:4700, 23-byte records.** GATO = id 0x92 → $CC:541E = `4C 00 01 00 FF 00 00 00 01 08 08 64 06 32 07 7F 04 04 04 04 00 00 01` — HP and all 12 stat bytes match WRAM exactly. Id confirmed by the **enemy NAME table $CC:6500 (11 B/record)**: 0x92*11 → $CC:6B46 = "Gato" in CT encoding (A6 BA CD C8).
- **Staging** (built by FD:A982 chain): $7EAEFF+3 = PC seat char-ids (`00 01 FF`) copied from the active party $7E2980 (FD:B26C-B277); $7EAF0D+6 = enemy slot ids (`FF FF FF 92 FF FF`, writer FD:B2B4); counts $AEC5/$AEC6/$AEC7; aux $AF02/$AF15 (bit7=hidden, FD:B2C1-C6). Enemy source = the event-staged pack at **$7E29C4, 12-byte-stride records** (loop `ADC #000C / CPX #60` at FD:B2CD-B2D8; +1 byte BMI = empty slot; +2 read at FD:B2B7). Pack `B3 00 40 02` sat at $29C4 in the base state and is consumed at f221 by C0:0504.
- **Turn order**: $7EB163 (8 bytes) = shuffled order permutation (C1:808B at f374), $7EB16E = actor ids per queue entry (C1:80A1: `LDA $AF0D,X / STA $B16E,X`), $B1BE reverse map (FD:B29A), $B23A init FF (C1:809B).
- **Menu state**: $7E95DC = the Att/Tech/Item cursor row of the active window (0→1 observed on a single Down press — the ONLY WRAM byte >$2000 that changed). $CD02 = pause flag (START toggles it in the NMI, D1:E80C-E839), $CD03 = debounce; when paused CD:3E49 pushes sound cmd $F5 to $1E00/$1E01.
- **Per-frame tickers (1-frame diff pf1/pf2 + --writer)**: $7C = NMI frame counter (INX $7C at D1:E7E7); $CA20 = frame-sync, INC'd by the main thread (CD:3E7E), zeroed at NMI end (D1:E88D STA $7ECA20); $9E = HUD task flag (C1:007E / CF:E77D). D=$0500 dp page: $0520+n*0xC = anim-slot records ticked by FD:E3A9-E3CE (+4 tick INC vs +5 period, +3 counter; observed $0530/$053C cycling 0-7); dp $CE ($05CE) = BG-anim VRAM streamer accumulator (FD:E170-E18B: `ADC $7F04C0,X / STA $CE`, DMA ch7 feed from $7F04D0 table — the battle's chip-animation streamer). HUD upkeep counters (all in CF:E58A block): $A112 blink divider 12→1 (CF:E58D), $A4EF mod-62 (CF:E5AD), $233C = pulsing CGRAM gray ramp for the cursor highlight (0821→...→2104→0000, CF:E5DB), $96F1 (CF:E5F6), $9931 (CF:E73E), $A3D2 (CF:E7B2); $CE14 (CD:0653); joypad block $06B2-$06D5 by C2:84E4/85B3 (battle reuses the field's C2:8500 joypad reader). $7F3800-3DFF also becomes battle data.

## 3. The battle mainloop & interrupts

- **Interrupt vectors**: native NMI=$00:FF10 `JML $000500`, IRQ=$00:FF14 `JML $000504` (ROM 0xFFEA/0xFFEE) — WRAM trampolines. Field: NMI=C0:EA63, IRQ=C0:ECCC. Battle boot REWRITES them (D1:F1D7, from CF:FB65's JSL CD:0000): **NMI=D1:E77C, IRQ=D1:FE97**.
- **NMI D1:E77C**: re-entrancy guard $7F (lag path JMP D1:E891), MVN-saves DP $00-$2A to $7ECC65, force-blank + $2115, **JSL CD:0C95 / CD:0C89 = the vblank DMA queue drains (OAM/VRAM/CGRAM uploads)**, INX $7C, timed-effect counters $CCEB-CCED/$CA24, START→pause toggle ($CD02/$CD03, with SELECT-gated auto-unpause path E85B-E877), JSL D1:FE71 (mul/div seed), MVN-restores DP, clears $7F and $7E:CA20, RTI at D1:E898. ~One IRQ + one NMI per frame observed in the 40k-instruction trace (frames ≈ 10.7-12k instructions).
- **Main thread** (from btl_trace.txt segmentation + disasm): per frame CD:3E75 = `JSR CD:3E7D` (spin `INC $CA20 / LDA $CA20 / BNE` until the NMI zeroes it = wait-frame; also honors pause via CD:3E44's $CD02 gate) then **`JSL C1:0012` → C1:10E3 = the per-frame battle tick**: HUD/menu upkeep CF:E300 + CF:E58A blocks, enemy-AI poll (bank CC: CC:F06E/F071/F080/F11B hot in trace), timers FD:E1xx/E3xx, sprite-anim CD:0Axx-0Cxx + CE:F0xx, joypad C2:8480/8500, sound driver C0:2C00-2D80. Wrapper loop CD:0043: `STA $CD00 / JSR CD:3E75 / JSL CD:04AA / LDA $CCEA / BNE` (CD:04AA also runs CD:3B5F + pause-aware CD:3E44 + CD:04DF). Menu/action tasks run via C1:0080: `JSL CD:0036 (→CD:04A6→CD:3E44) / LDA $9E / BNE` — i.e. the action layer blocks on $9E while the per-frame chain keeps pumping.

## 4. HUD text: "Att./Tech/Item"

- **Strings (ROM)**: fixed-width 14-byte FF-padded records in bank CC: $CC:FA4A "Att.", $CC:FA58 "Tech", $CC:FA66 "Item", $CC:FA74 "Att.", $CC:FA82 "Comb", $CC:FA90 "Item" (CT text encoding: 'A'-'Z'=0xA0-0xB9, 'a'-'z'=0xBA-0xD3, '0'-'9'=0xD4-0xDD, '.'=0xE8, space=0xFF). ("Att." also appears in a menu text blob at $CC:4374.)
- **Renderer**: labels are drawn as map entries tile = 0x100 + text-code, pal2, prio, into the WRAM strip-staging row at $7E0D04+ (observed writers: CC:E4E6 clears at f291, C1:0509 and C1:07D5 write label text), DMA'd to the VRAM strip map w0x7800.
- **Glyph bitmaps (ROM)**: **the 2bpp font at $FF:8C60, 16 bytes/char, indexed by text code** (VRAM tile 0x1A0 'A' found byte-exact at $FF:9660 = base+0xA0*16; verified byte-exact for '.', 'T', 'a', '0'). The whole 0xA0-0xFF block is uploaded to BG3 chr words 0x5800+ (bytes 0xB000+) at boot, so map tile = 0x100+code needs no per-char rendering — it's a direct font, unlike the dialogue engine's variable-width tile compositor.

## 5. Repro commands (all from repo root, oracle copy ct_probe.exe)

- captures: `ct_probe.exe --peek gato_press_action.state out.raw --btnscript "100:8,0:N"` (N=292/392/592...; PEEK_SAVE for states; PEEK_ISOBG=0/1/2/4 for layer isolation)
- stat writers: `--monitor "6130:4,612D:1,5E30:4,5E2D:1" --from gato_press_action.state --input "100:8,0:592" --diff --writer` → f284 CF:FB9A clear, f288 FD:B475/B460/AE83
- boot timeline: `--monitor "0500:8,AEFF:3,AF10:1,B163:8,9E:1" ...` → f223 D1:F1D7, f286-289, f374
- mainloop: `--trace 40000 --from p600.state` (btl_trace.txt)
- damage probe: `--peek p600.state out --btnscript "0:20,100:4,0:476"` → $5E30 70→61 (GATO counter for 9); GATO's own HP unchanged over these runs (attack whiffed or 0-dmg — see open questions).

## Key addresses
- C0:18A7 JSL C1:0000 — the field->battle synchronous handoff (C0:2C41 = one tick of the field's layer scroll, fade C0:EC60 before; field re-init after)
- C1:0000 battle jump table: +0 boot->CF:FB65, +12 per-frame tick->C1:10E3, +9/+C HUD upkeep->CF:E300/CF:E58A
- CF:FB65 battle boot: zeroes DP $80-EA, JSL CD:0000 (transition+gfx, installs handlers), clears $7E2D00-$7EACEB (STZ loop CF:FB97), JML C1:8000
- D1:F1D7 installs battle interrupt trampolines at $7E0500/0504: NMI=D1:E77C, IRQ=D1:FE97 (field: C0:EA63/C0:ECCC; ROM vectors 00:FF10/FF14 JML $000500/$000504)
- D1:E77C battle NMI: DMA drains JSL CD:0C95/CD:0C89, frame ctr $7C, pause toggle $CD02/$CD03, clears frame-sync $7E:CA20, RTI D1:E898
- D1:FE97 battle IRQ: early force-blank $2100=80 (vblank extension), parity work D1:FE4C/FD:FFFD, mul-div seed D1:FE71
- CD:3E7D wait-frame spin on $CA20; CD:3E75 = wait + JSL C1:0012 (per-frame battle tick); C1:0080 = JSL CD:0036 while $9E (HUD task flag, set C1:007E, cleared CF:E77D)
- CD:09F1-0AA0 HDMA setup, 7 indirect channels, headers $7E:C867/C894/C8C1/C8EE/C91B/C948/C975: ch0 mode4 B=$2107 rewrites BG1SC-BG4SC (61/69/71/00 -> 78 78 78 78 at scanlines 160-207 = HUD strip at map word 0x7800 shared by BG1 bodies + BG3 text), ch4 TM 13->05, ch5 per-line $2130-33 gradient, ch6 BGMODE 09
- FD:AE52 PC battle-stat loader: $7E2600+charid*0x50 (0x50B sheet: +3 curHP,+5 maxHP,+7/+9 MP) -> $7E5E2D+seat*0x80 via ptr table FD:A80B {5E2D,5EAD,5F2D,5FAD,602D,60AD}; store FD:AE80
- FD:B438 enemy loader: id from $7E:AF0D+slot, offset id*23 into ROM $CC:4700 enemy-stats table -> $7E5FAD+slot*0x80 (+0 id, +3/+5 HP dup, +0x37 stats); GATO id 0x92 rec $CC:541E = 4C 00 ... (HP 76) byte-exact vs $7E612D block
- $CC:6500 enemy name table (11 B/rec): 0x92 -> CC:6B46 'Gato'
- FD:A982/FD:B24D-B2D8 staging: PC seats $7EAEFF<-party $7E2980 (FD:B274); enemy ids $7EAF0D<-event pack $7E29C4 (12B-stride records, consumed by C0:0504); counts $AEC5-AEC7
- $CC:FA4A/FA58/FA66/FA74/FA82/FA90 battle menu labels 'Att.'/'Tech'/'Item'/'Att.'/'Comb'/'Item' (14-byte FF-padded, CT encoding); drawn as strip-map tiles 0x100+code by C1:0509/C1:07D5 into staging $7E0D04+
- $FF:8C60 battle HUD 2bpp font, 16 B/char indexed by text code ('A'=FF:9660), uploaded to BG3 chr tiles 0x100-0x1FF (VRAM bytes 0xB000+); verified byte-exact A,T,a,0,'.'
- $7E95DC = Att/Tech/Item cursor row; $7EB163/$B16E turn-order arrays (shuffle C1:808B at f374); $233C CGRAM cursor pulse (CF:E5DB); $05xx dp-page (D=$0500) anim slots 0xC-stride ticked FD:E3A9+, dp $CE VRAM-anim streamer FD:E170-E18B

## Open questions
- The true ATB charge counter/decrement site is NOT pinned: $B163 seeds (05 0A 04 ...) are a static shuffle, none of the per-frame tickers reset at an action, and GATO idled 420+ frames with menus open (Wait-mode?); the window-pop cadence at boot is driven by the $9E task chain. Need a longer instrumented fight (hit GATO, watch $96xx-$A0xx per-actor HUD blocks with --writer at his action frame).
- GATO's own HP at $7E6130 never dropped in the attack runs (Crono's confirm-attack landed no damage — whiff or 0 vs his defense?); the damage-apply writer for the enemy block is therefore unobserved. Repro: longer battle with Marle attacks / crits, monitor 6130:4 --writer.
- Which event-VM op stages the $7E29C4 battle pack (B3 00 40 02) and the pack record semantics (+0 B3 = formation/pack id? +2 flags 0x40, +3 0x02) — the pack was already staged in gato_press_action.state and ct.py script 6 didn't surface a labeled battle op; need a pre-GATO state and a monitor on 29C4 during the event, plus the C0:0504 consumer context.
- How enemy id 0x92 reaches $AF10 given $29C4 is cleared at f221 (C0:0504 must latch the pack somewhere first — find the intermediate staging).
- The two-pass HUD label rendering (C1:0509 then C1:07D5 rewriting $0D04, lowercase-then-uppercase 'a'->'A') — window-open animation or draft/final pass?
- 'Comb'/second 'Att.'/'Item' records at CC:FA74-FA9E = dual-tech menu variants (inferred from adjacency only).
- GATO's OBJ graphics ROM source (enemy gfx/pose table feeding the 0x400-0x6FF VRAM upload) unexplored.
- HDMA strip band shows scanlines 160-207 with a 4-line revert at 208-211 in the sampled table, but the visible HUD extends to ~y223 — the header tables are rebuilt per frame (double-buffered bands at $BC85/$BE9B etc.); a per-scanline capture would pin the exact live band.
- $7F3800-3DFF battle region meaning + writer unidentified.

## C0/2C41 is the field's layer scroll, and it keeps running under the fight (2026-09-28)

Not a music cue (audio_commands.md sec. 3 refuted that). C0/024C's `JSR C0/0918` (at C0/0268) copies the field's object page
$0920-$1DFF to $7F:2000 -- over the script image, which C0/0304's `JSR C0/56D4` (032D) decompresses again on the way back --
and C0/2C41 (DP = $0100, DB = $7F) works on that copy: $7F:3415 is the field's $1D35 (bit 1 = BG2 drifts, bit 2 = BG3),
$3417/$3418 the BG2 speed ($1D37/$1D38, 4.4 a frame) added into the fractions $341D/$341E, the whole pixels onto the
position $346B/$346D ($1D8B/$1D8D) and into the battle's BG2 scroll $2C74/$2C76 (CD/0CC2 hands it to the arena); bit 2 the
same for BG3 ($3419/$341A, $341F/$3420, $346F/$3471 -> $2C78/$2C7A). The field side calls it once (C0/1895, right after
C0/024C); the battle's NMI once a frame (D1/E7E3 `JSL CD/0C89` = `JSL C0/0002` -> `BRL C0/2C41`, skipped with $CD02 like the
$7C count). And C0/0283's `JSR C0/0905` puts the copy back, so the field resumes from where the drift left it. MEASURED, the
attract's 0AD (saves/attract_cena_inicial.state, --monitor 7F3415:12,7F3469:10,7E2C70:12 --writer): the copy at f1744
(C0:0927) holds 02 02 20 10 and 025C/012E, then +2/+1 every frame (C0:2CD2/2CD6) -- Zeal's starfield drifting behind Flea.
The port kept the copy aside and never ran the routine (the starfield stood still for ~520 frames, the audit's item 2);
battle.c battleFieldScrollTick is it now, and the arena reads the field's layer offsets from the copy while the fight runs
(field.c, CtSceneLayerOffsetsOf).
