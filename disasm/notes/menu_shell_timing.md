# Menu shell: OPEN and CLOSE timing, frame by frame

Scope: every frame between "the player asks for the menu" and "the field/overworld runs again", for the
X menu (mode 1, field and overworld), the shop (mode 2, op `C8 90`) and the load screen (mode 0).
Sources: ROM disassembly (`python tools/dis.py <fileoff> <len> <m> <x>`) and the oracle (`build/ct.exe`,
copied to scratch so a parallel build could not swap it mid-run). Logs: `%TEMP%\claude\...\scratchpad\shell_re\`.
Mechanism notes already derived elsewhere are cited, not repeated: `menu2_shell_exit.md` (scheduler, fade code,
exit, NMI), `menu_entry.md` (field X path), `shop_screen.md`, `menu2_overworld_entry.md`, `field_load_cost.md`,
`boot_loader.md`.

---

## 0. Conventions: read these first

* **fN = oracle harness frame** (`--monitor` row N = WRAM after N+1 `snes_runFrame` calls). A harness frame is
  vblank lines 225-261 **followed by** active lines 0-224. `--peek` with an L-frame script shows the picture of
  f(L-1).
* **Picture latch.** Each brightness shadow is written to INIDISP `$2100` by the NMI at the **start** of the
  next frame: menu `$7E:0D10` by C2/8455-845B (`LDA $10 / BNE / LDA #80 / STA $2100` with DP=$0D00, the whole
  byte, 0 -> force blank), field `$0119` by the field NMI, overworld `$001C`. So **picture(fN) = shadow at
  the end of f(N-1)**. Checked three ways: `--ppuregs` brightness (menu: f42=2, f43=4, f48=14, f49=15 for
  `$0D10` = 02@f41 .. 0F@f48), framebuffer mean luma (ratio to the full menu = 0.126, 0.260 ... 0.927 = 2/15 ..
  14/15), and the overworld's `$001C` (0E@f13 -> picture f14 = 14).
* **Pad latch.** A button held from fN is auto-read at the vblank that ends fN; the first handler that can
  see its edge runs in f(N+1). (Sweep §2.5.)
* **`--hookpc` frames.** `--hookpc` applies script entry H one frame earlier than `--monitor`. For the same
  script a hookpc visit at (frame H, line v) is monitor frame **H if v < 225, H+1 if v >= 225**. Checked on
  C0/1949 (hook f4.239 -> `$0119`=0E at monitor f5), C2/8073 (hook f35.217 -> `$0407` write at f35),
  C2/83C0 (hook f40.243 -> `$0D10`=02 at f41). "Lines" below = 262 x frame + line, from hookpc.
* **Harness frames vs video frames: the menu init loses one.** C2/940D DMAs a 65535-byte VRAM fill
  (C2/94D5-94EE, 524,280 cycles) plus 58,624 bytes of WRAM fill (C2/9512 x3); a single `STA $420B` spans a
  whole vblank, and `snes_runFrame` ends that harness frame at "next frame" instead of at vblank.
  `CT_HOOK_CYCLES=1` totals: X open+close 129 harness frames = 130.0 frames of master cycles; shop open 59 = 60.0;
  overworld open+close 159 = 160.0. The single loss is always C2/940D (harness f20-f22 hold 1,429,466 cycles =
  4.0 frames). **Every table below is in harness frames** (what every check grades); the video-frame count of
  the init chain is one higher (§6, open item).
* **Logical oracle** = `CT_ORACLE_LOGICAL=1` (CLAUDE.md exception). The menu has real lag frames (a screen
  build overrunning the NMI); on the logical oracle they vanish and the fade-in lands **one frame earlier**
  (§2.3). The close is identical on both.

---

## 1. VERDICT

The menu open is five phases and the close is four, all fixed by ROM code except two CPU-cost windows:

| phase | ROM | frames | per-frame picture |
|---|---|---|---|
| O1 caller fade-out | field C0/193E (X) or C0/0CCE (op C8); OW C2/251E fade object | 15 field / 17 OW | shadow 14..1 then black; the world is FROZEN (field) |
| O2 switch | C0/0B4E + C0/011B (save `$7E:0920-1DFF` -> `$7F:2000`) + `JSL C2:8000` | same frame as the first black one | black |
| O3 init chain | C2/800E-808C + C2/8096 (NMI off) | first scan **16 harness frames after the JSL frame** (17 video); 17 (18) on the two saved-game SRAMs measured (com_jogo_salvo, ct_real_save.srm) | black, clock stopped |
| O4 screen substate 0 | the `$0067` screen's handler 0 | mode 1: 1 + builder yields (2 on tab 0); mode 2: 5 (CPU); mode 0: 2 (CPU) — real oracle, see §2.3 | black |
| O5 fade-in | C2/83B2 -> task C2/83BD -> C2/83D0 step +2 | 1 idle + 7 ramp, 15 lands without a yield | 2,4,..,14 then 15 |
| C1 exit | C2/82B2: kill all tasks, C2/834D, C2/83CA step -2 | 8 (from 15) | 13,11,..,1 (frozen image) then black |
| C2 RTL + reload | C0/1964 / C0/0D1C -> C0/01A5 ... C0/286C; OW C2/2556 -> C2/2C93 | location-dependent: 40 (loc $082), 38 (loc $005); OW 37 | black, clock stopped |
| C3 caller fade-in | C0/2824 (field) / C2/2EC4 object (OW) | 15 field / 17 OW | 1..15; field FROZEN |
| C4 resume | field main loop re-entered mid-iteration | — | live |

---

## 2. OPEN

### 2.1 Caller side (before `JSL C2:8000`)

| | X menu (field) | shop / op C8 (field) | X menu (overworld) | load screen (scene 0, `C8 41`) |
|---|---|---|---|---|
| trigger | C0/18D9 -> C0/1920 `$00F6` bit6 (edge), gates C0/192F-193D | op C8 C0/3757: `$0125`=operand, `$0117` \|= 40 (runs inside the scheduler) | C2/3567 `$00F8` bit6 (HELD) -> C2/35F7 `$027C`=5 | op C8, operand odd -> C0/0D00 `LDA #00` |
| fade loop | C0/193E-1956: `$0407`=FF, `DEC $19`, `JSL FD:C2C1`, `JSR C0/EC60` | next main loop: C0/0C76 -> C0/0CC7 -> C0/0CCE-0CE8: `DEC $19`, C0/881E with `$1F`=0, `JSL FD:FFF7`, C0/EC60 | C2/251E: fade object script C2/2EC1 (`28 01 52`, op 0x28 -> C2/20A2), NMI on, `LDX #0011 / JSR C2/0454` | same loop as the shop |
| frames | 15 DECs (0E..00) | 15 DECs | 17 (0E..01, then `$001C`=80, 2 more) | 15 |
| world during the fade | frozen (OAM and `$1800-18FF` identical f5->f18) | frozen (identical f12->f25) | object VM keeps running (menu2_overworld_entry.md) | — |
| play clock `$0400` | stops the frame after X (`$0407`=FF at C0/1943) | **keeps ticking** (no `$0407` write) | ticks until the NMI goes off | — |
| then, frame after the last DEC | C0/1958 `JSR C0/0B4E` (force blank, NMI/DMA off) at line 239, C0/195B `JSR C0/011B` (0x14E0-byte MVN save, 187 lines), C0/1960 `JSL C2:8000` at line 164 | C0/0CEA same pair, C0/0D18 `JSL C2:8000` (A=2, X=shop) | C2/2535 SEI/NMI off, C2/2E23 six-block stash (863 lines), C2/2552 `JSL C2:8000` (A=1 or 5) | C0/0CEA, C0/0D18 (A=0) |
| measured | X held f4 -> `$0119`=0E f5 .. 00 f19 -> `$0A00`=01 f20 (C2:8020) | A held f10 -> `$0125`=90 f11 (C0:376B) -> `$0119`=0E f12 .. 00 f26 -> `$0A00`=02 f27 | X held f10 -> `$027C`=5 f11 (C2:35FA) -> `$001C`=0E f13 .. 01 f26, 80 f27 -> `$0A00`=01 f31 | START f1600 -> ... `$0119`=0E f1669 .. 00 f1683 -> C2/8000 f1684 |

### 2.2 The init chain C2/8000 -> scheduler (mode-independent, NMI off, force blank)

Leg costs from `--hookpc` (lines; the C2/940D span is corrected +262 for the lost harness frame, §0):

| leg | ROM | party3_field (X) | com_jogo_salvo (X) | shop90 |
|---|---|---|---|---|
| entry, `$2100`=80, `$4200`=01, stack save, sound prelude (5 x `JSL C7:0004`) | C2/800E-80EE | 20 | 37 | 20 |
| PPU init + **64 KB VRAM clear DMA + WRAM fills `$0C00-1DFF`, `$2E00-FFFF`, `$0000-00FF`** | C2/940D | 754 | 754 | 754 |
| graphics build (C2/9542 decompressions, VRAM queue drain C2/8663) | C2/968D | 1494 | 1494 | 1494 |
| **save-slot scan**: per slot `JSL FF:F9C4`, valid -> C2/D19F else C2/D296, C2/D2C4 | C2/D156 | 1096 | **1246** | 1096 |
| OAM clear, queue drain, roster, config unpack | C2/984A, 8663, 816A, 92F4 | 29 | 29 | 29 |
| window objects | C2/F3CA | 219 | 219 | 219 |
| item tables / stat blocks | C2/9875 | 630 | 629 | 629 |
| small fills (C2/80F3-8121), HP-bar scale, `$0407`=0 (C2/8073), `$0067`=C2:808F[mode], NMI on (C2/8085) | C2/80F3-8121, C2/813C-808C | 3 | 3 | 3 |
| **total (video lines)** | | **4245** | **4411** | **4244** |
| harness: C2/8000 frame -> first scan | | f20 -> f36 (16) | f25 -> f42 (17) | f27 -> f43 (16) |

The field_pre_menu2 state repeats party3's numbers to within 4 lines, and the overworld open (C2/8000 f31 L142 ->
C2/8073 f46 L212) matches the field's. Only C2/D156 varies (SRAM contents). The WRAM fill is why the field must
save `$7E:0920-1DFF` first (C0/011B -> C0/0918) and must rebuild everything else on return.

`$0D10` is zeroed by that WRAM fill (shop: `20`->`00` at f28 with no CPU writer = the DMA), so the menu's
`$2100` byte is always the bare brightness nibble.

### 2.3 Screen substate 0 and the lag frames (the only per-mode part of the open)

The scheduler (C2/8190) spawns slot 1 = dispatcher C2/826F and slot 3 = render C2/E91B. Its first scan is the
frame after the one C2/8085 enables the NMI. Slot 1's first run is substate 0 of screen `$0067`:

| mode / screen | substate 0 | ends with | CPU cost | real oracle | logical oracle |
|---|---|---|---|---|---|
| 1 / `$00` ribbon | C2/99CE: `$75`==0 -> exit; `$0D88`==0 -> `$0411-0413,$041A`=0; C2/9AAD; C2/9B54 -> builder (tab 0 = C2/A051, yields at A0AC, A0BD) | `INC $68 / JMP C2/83B2` (C2/9A16) | first call overruns the NMI (the logical oracle holds the beam 91,974 cycles) | S0 f36 (00B8=0300 at the frame end = lag), yields f37, f38, spawn f39 | S0 f36, spawn **f38** (1 hold, 91,974 cycles) |
| 2 / `$0C` shop | C2/D52B: `JSR C2/DACA` = 1388 lines, no yield: C2/ED31 `$FF:C213` 689, `JSL FF:F548` 616, the rest 83 | `INC $68 / JMP C2/83B2` | 5.3 frames | S0 f43, spawn **f48** | 4 frames rolled back (real time) + 1 held (178,084): spawn **f47** |
| 0 / `$0E` load | C2/E613 `JSR C2/E7C3` (slot list) | `INC $68 / JMP C2/83B2` | ~2.8 frames | S0 f1701, spawn f1703 (line 212) **plus** one more lag frame (fade first runs f1705) | spawn = S0+1, no extra lag |

The builder's yield count depends on the tab when `$0D88` (Save Menu Cursor) is set: 2 for tabs 0/1/4/5, 1 for
tabs 2/3 (menu2_shell_exit.md §2.11).

A lag frame in the menu is the NMI finding `$0D01` bit7 still set (the previous tick not consumed): C2/8434
`LDA $01 / BPL` skips the transfers (OAM/CGRAM/VRAM queue/scrolls) but C2/844D-845B still run the pad read, the
clock and the `$2100` write.

### 2.4 Fade-in: C2/83B2 -> slot 0 task C2/83BD -> C2/83D0

```
283B2 REP #30 / LDA #83BD / LDX #0000 / JMP C2/8249   spawn slot 0 (status 1, delay 1); RTS lands in the
                                                       dispatcher, which yields slot 1 (C2/826C)
283BD JSR C2/821E                                      first run: yield, brightness still 0
283C0 LDA #02 / JSR C2/83D0 / JMP C2/8216              step +2, then self-kill
283D0 $0D20 = step, $0D21 = 1 (frames per step)
283DA n = $0D10 & 0F ; TRB #0F ; n += step
283EA n <= 0 -> 28403 JMP C2/821E (nibble left 0, one more yield)
283EE n >= 0F -> 283FD TSB #0F / RTS                    (no yield: 15 lands in the same frame)
283F2 TSB n / JSR C2/822B (1 frame) / BRA 283DA
```

Let **P** = the frame of the spawn. Measured on every state:

| frame | P | P+1 | P+2 | P+3 | P+4 | P+5 | P+6 | P+7 | P+8 | P+9 | P+10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `$0D10` (end of frame) | 00 | 00 (task yields) | 02 | 04 | 06 | 08 | 0A | 0C | 0E | 0F, task dead (C2:821C) | 0F |
| picture brightness | 0 | 0 | 0 | 2 | 4 | 6 | 8 | 10 | 12 | 14 | 15 |

X real P=f39 (02@f41, 0F@f48); X logical P=f38; overworld P=f50 (02@f52, 0F@f59); shop real P=f48
(02@f50, 0F@f57), logical P=f47; load real: the extra lag moves the task's first run to P+2, so 02@P+4 (f1707).

### 2.5 When input is first accepted

* The dispatcher calls substate 1 for the first time at **P+1**, in the same scan as the fade's first run
  (P+2 on the load screen's real run, where a lag frame intervenes).
* **C2/E984, the input prologue of every screen, drops A/B/L/R on the first call of a new substate**:
  `LDA $68 / CMP $0D17 / STA $0D17 / BNE ->E9CD` skips the `$0D1D` build. The d-pad (`$5A`, C2/E9E2) is not
  gated. Measured `$0D17`=01 at f40 (C2:E995).
* X menu, real oracle. Right held from f39 -> `$0411`=01 at f40. Held from f38 (edge f39, substate 0 still
  running) -> lost. B held from f39 -> ignored. B held from f40 (edge f41) -> closes. So **d-pad from P+1,
  A/B from P+2, both while the screen is still black** (the first visible frame is P+3).
* Shop: substate 1 C2/D546 first at f49 = P+1. B held from f48 -> ignored; B held from f49 -> closes (RTL f51).
* **B during the fade-in** closes from the current nibble. Slot 0 runs before slot 1 in the scan, so the
  fade-in has already stepped this frame. Then C2/83CA steps -2 from it: from 2 -> 0 at once (1 frame, RTL
  next frame, measured f41 -> f42); from 4 -> 2, 0 (RTL 2 frames later, measured f42 -> f44).

---

## 3. CLOSE

### 3.1 Menu side: C2/82B2 (from C2/9A95 ribbon B, C2/D588 shop B)

```
282B6 LDA #FF / JSR C2/8255     kill all 8 slots (render + blink + fade-in die; the image freezes)
282BB JSR C2/834D               party-gfx epilogue (6 lines measured)
282BE JSR C2/83CA               A=FE -> C2/83D0: 13,11,9,7,5,3,1 one per frame, then 0 + one yield
282C1 $2100=80 / $4200=01 / $420B=$420C=0 / LDX $0920 / TXS / PLP PLB PLD / SEI / RTL
```

The dispatcher runs right after the NMI (C2/82B6 at line 245 of the B-edge frame). Let **B** = the frame
the edge is seen:

| frame | B | B+1 | B+2 | B+3 | B+4 | B+5 | B+6 | B+7 | B+8 |
|---|---|---|---|---|---|---|---|---|---|
| `$0D10` | 0D | 0B | 09 | 07 | 05 | 03 | 01 | 00 (C2:83E5) | RTL at line 233 |
| picture | 15 | 13 | 11 | 9 | 7 | 5 | 3 | 1 | 0 |

The OAM does not change between pictures B and B+7 (render task dead). The menu NMI still runs in B+8,
before the RTL, so the clock ticks that frame. The load screen's A-on-slot takes the hard exit C2/8320 (same
kill, C2/834D, C2/83CA, then `LDX #06FF / TXS / JML C0:0000`). Measured: C2/8324 f1731, `$0D10` 0D..00
f1731-f1738, C2/8349 JML f1739.

### 3.2 Field side: C0/1964 (X) vs C0/0D1C (op C8)

```
X   C0/1964 JSR 0B4E / 0B64 / 0B75 (re-install field NMI/IRQ) / D=$0100 / JSR 01A5 / JSR 1A03 / JSR B192 /
            JSR 2824 / STZ $0407 / RTS  -> back into the main loop at C0/00A4 (then 0C76 .. 00BF .. EC60)
C8  C0/0D1C JSR 0B4E / 0B64 / 0B75 / D=$0100 / JSR 01A5 / JSR B192 / $0117 &= ~40 / $0118 |= 40 /
            BRL 2824  -> 2824's RTS returns to C0/00A7 (after the JSR 0C76), then 881E .. 00BF .. EC60
```

The shop path skips C0/1A03 (the party reconcile) and flips the op-C8 flags so the op completes when the
script re-runs it (C0/375D -> C0/3774).

C0/01A5 = C0/0905 (restore `$7F:2000` -> `$7E:0920`, 0x14E0 bytes), `JSL FD:C2C1`, `JSL FD:C1EE`, then the
field loader's own legs C0/0960, 6DCF, 7084, 7F7E, A33B, 09DD, 0A14, 56D4 (C0/00F4 without 092B/1B53), scroll
restore, C0/595C. C0/2824 = `JSR C0/286C` (one full draw/upload pass ending in C0/0B28 + C0/EC60), then
`INC $19 / C0/881E ($1F=0) / C0/00DE (= FD:C2C1 + FD:FFF7) / C0/EC60` while `$19 < 0F`. There is **no
C0/59D9 scheduler** in the loop (the load-transition fade C0/2848 calls C0/00BF instead). So the field is
frozen during this fade-in: OAM and `$1800-18FF` are identical from f110 to f123, and objects first move at f124.

Legs, measured (lines; NMI off until C0/0B28):

| leg | X (loc $082, party3_field) | shop (loc $005, shop90) |
|---|---|---|
| RTL -> C0/01A5 | f69 L233 -> L234 | f439 L233 -> L234 |
| C0/0905 MVN restore | 186 | 186 |
| FD:C2C1 + FD:C1EE + pointer setup | 4 | 4 |
| C0/0960 tiles (7 LZ packets) | 3830 | 3716 |
| C0/6DCF + 7084 + 7F7E | 377 | 342 |
| C0/A33B map | 3360 | 2760 |
| C0/09DD props | 480 | 496 |
| C0/0A14 | 164 | 265 |
| C0/56D4 script | 290 | 255 |
| scroll restore, C0/595C, (C0/1A03), C0/B192, `JSR C0/286C` | 8 | 3 |
| C0/286C: C0/0A50 | 951 | 949 |
| C0/286C: 6F79 .. 6F5E | 325 | 475 |
| C0/0B28 (FD:C124/C2C1/C1EE, then **`LDA $4210 / BPL` until vblank**, `$4200`=81, `$4209`=00D3, CLI) | 234, exits at L225 | 234, exits at L225 |
| C0/EC60 (waits for the NEXT NMI, a full frame) -> C0/2829 first `INC $19` | 279 | 279 |
| **RTL -> first INC** | **10489 lines = 40.03 frames (f69 -> f109)** | **9965 = 38.03 (f439 -> f477)** |

All 12 decompressions of loc $082 run in the reload (`--hookpc C30557`: f69-f100, the same packet list
`--loadcost 130` prices: 12/12 exact, 36.47 load frames including LOADC_FIRST_PASS). The reload is **the field
load's window**, plus the 186-line MVN restore, minus the song leg. On the Gato fair state the same window was
24 frames (menu2_shell_exit.md §4.2).

Field fade-in and resume (X; the shop is the same shifted to f477):

| frame | f109 | f110 .. f122 | f123 | f124 | f125 |
|---|---|---|---|---|---|
| `$0119` | 01 (C0:282B, line 242) | 02 .. 0E | 0F | loop exits: C0/2842 `STZ $1E`, `STZ $0407`, RTS into the main loop; the rest of that iteration runs (C0/00BF scheduler: objects move) | first full main-loop iteration (C0/0097 pad latch) |
| picture | 0 | 1 .. 13 | 14 | 15 | 15, live |

The clock resumes at f109: C0/0B28 turned the NMI on and the menu had released `$0407` at C2/8073.

### 3.3 Overworld side: C2/2556 (menu2_overworld_entry.md, re-measured)

B edge f91 -> `$0D10` 0D..00 f91-f98 -> RTL f99 L233 -> C2/0043 + C2/0110 (2 lines) -> **C2/2E72 restore**
(f99 L235 -> f102 L50, 777 lines) -> C2/03EF -> **C2/2C93 rebuild** (f102 L55 -> f134 L69, 8398 lines, 20
`C3/0557` decompressions f102-f132) -> C2/2566 spawns the fade-in object C2/2EC4 (`29 01 52` -> C2/2105), NMI on,
`LDX #0011 / JSR C2/0454` -> `$001C` 01 at f136 .. 0F at f150 (pictures f137..f151 = 1..15) -> C2/2585 `$027C`=1
at f151.

---

## 4. Frame-by-frame tables

### 4.1 X menu: `saves/battle_re/party3_field.state`, `--btnscript "0:4,200:2,0:54,1:2,0:100"`

`$0119` field shadow, `$0D10` menu shadow; "pic" = brightness shown that frame (luma 22.40 = full field, 89.59
= full menu). The logical oracle only differs in the rows marked L.

| f | pad | event (RAM, writer) | pic |
|---|---|---|---|
| 0-3 | — | field main loop | field 15 |
| 4 | X | (auto-read at the end of f4) | field 15 |
| 5 | X | C0/1920 edge -> `$0407`=FF (C0:1943), `$0119`=0E (C0:194B); last clock tick | field 15 |
| 6-19 | — | `$0119` 0D..00 (C0:194B), field frozen | field 14..1 |
| 20 | — | C0/1958 L239 force blank, C0/011B, C0/1960 `JSL C2:8000` L164: `$0A00`=01 (C2:8020) | black |
| 21-34 | — | init chain (§2.2), no NMI, clock stopped | black |
| 35 | — | C2/8073 `$0407`=00 (C2:8076), `$0067`=00 (C2:807F), slots 1+3 spawned (C2:8253), NMI on L217 | black |
| 36 | — | first menu NMI + scan: C2/99CE substate 0; slot 1 still running at the frame end (`00B8`=0300) = LAG; clock ticks again | black |
| 37, 38 | — | builder yields (`00B8`=0201) [L: f36, f37] | black |
| 39 | — | `$00BE`=01, C2/83B2 L170: slot 0 spawned (`00B0`=0101) [L: f38] | black |
| 40 | — | fade task C2/83BD yields; **substate 1 first call, `$0D17`=01: d-pad live, A/B dropped** [L: f39] | black |
| 41 | — | `$0D10`=02 (C2:83F5); A/B live [L: f40] | black |
| 42-47 | — | `$0D10` 04..0E [L: f41-f46] | menu 2..12 [L: one frame earlier, f41-f46] |
| 48 | — | `$0D10`=0F (C2:8402), `00B0`=0000 (C2:821C) [L: f47] | menu 14 |
| 49-60 | 60: B | idle ribbon | menu 15 |
| 61 | B | B edge: C2/9A95 -> C2/82B2 L245, kill (`00B0` C2:8263), C2/834D, `$0D10`=0D | menu 15 |
| 62-67 | — | `$0D10` 0B..01 | menu 13..3 (frozen image) |
| 68 | — | `$0D10`=00 (C2:83E5), last yield | menu 1 |
| 69 | — | menu NMI (clock tick), C2/82C1, RTL L233 -> C0/1964, C0/0905 (`$0A00`=4D C0:0914) | black |
| 70-108 | — | reload (§3.2), no NMI, clock stopped | black |
| 109 | — | C0/2829 `$0119`=01 (C0:282B) | black |
| 110-123 | — | `$0119` 02..0F, field frozen | field 1..14 |
| 124 | — | C0/2824 exits, C0/1981 `STZ $0407`, main loop resumes (scheduler runs, objects move) | field 15 |
| 125 | — | first full main-loop iteration | field 15 |

### 4.2 Shop: `saves/field_re/shop90_lake.state`, `--btnscript "0:10,100:2,0:418,1:2,0:80"`

Luma: full field 96.36, full shop 55.65.

| f | pad | event (RAM, writer) | pic |
|---|---|---|---|
| 10 | A | (auto-read at the end of f10) | field 15 |
| 11 | A | activate -> op C8 90: `$0125`=90 (C0:376B), `$0117` \|= 40 | field 15 |
| 12 | — | C0/0C76 -> C0/0CCE: `$0119`=0E (C0:0CD6); clock keeps ticking | field 15 |
| 13-26 | — | `$0119` 0D..00, field frozen | field 14..1 |
| 27 | — | C0/0CEA L239, C0/0D18 `JSL C2:8000` L164: `$0A00`=02 (C2:8020) | black |
| 28-41 | — | init chain; clock stopped f28-f42 | black |
| 42 | — | `$0067`=0C (C2:807F), slots spawned, NMI on L216 | black |
| 43 | — | C2/D52B -> C2/DACA starts (CPU-bound, lag f43-f47) | black |
| 48 | — | DACA returns L78, `$00BE`=01, C2/83B2 [L: f47] | black |
| 49 | — | fade task yields; C2/D546 first call (A/B dropped) [L: f48] | black |
| 50 | — | `$0D10`=02; A/B live [L: f49] | black |
| 51-57 | — | `$0D10` 04..0F (0F at f57) [L: f50-f56] | shop 2..14 |
| 58 | — | | shop 15 |
| 430 | B | | shop 15 |
| 431 | B | B edge: C2/D553 BVS -> C2/D588 -> C2/82B2 L235, `$0D10`=0D | shop 15 |
| 432-438 | — | `$0D10` 0B..01, then 00 at f438 | shop 13..1 |
| 439 | — | RTL L233 -> C0/0D1C, C0/01A5 (`$0A00`=00 C0:0914) | black |
| 440-476 | — | reload (§3.2) | black |
| 477 | — | `$0119`=01 (C0:282B) | black |
| 478-491 | — | `$0119` 02..0F | field 1..14 |
| 492 | — | C0/2842, RTS -> C0/00A7: main loop resumes | field 15 |

Both tables were measured on the real-time oracle (`--monitor` for the RAM, `--peek` luma per frame for the
pictures). The shop's logical run (`CT_ORACLE_LOGICAL=1`) has 4 rollbacks + 1 hold in DACA. Its close is
byte-identical (0D@f431 .. 00@f438, RTL f439, `$0119` 01@f477 .. 0F@f491).

---

## 5. What differs between the entries

| | X field (mode 1) | shop (mode 2) | X overworld (mode 1) | load (mode 0) |
|---|---|---|---|---|
| caller fade | 15, `$0119`, world frozen, clock paused | 15, `$0119`, frozen, **clock runs**, extra C0/881E + FD:FFF7 per frame | 17, `$001C` object, VM runs | 15, `$0119` |
| JSL frame -> first scan (harness) | 16 (f20->f36) | 16 (f27->f43) | 16 (f31->f47) | 17 (f1684->f1701, saved-game SRAM) |
| substate 0 -> P (real) | +3 (1 lag + 2 yields) | +5 (CPU) | +3 | +2, then +1 lag before the fade's first run |
| fade-in / fade-out | same C2/83D0 | same | same | same |
| exit | C2/82B2 -> RTL C0/1964 | C2/82B2 -> RTL C0/0D1C | C2/82B2 -> RTL C2/2556 | C2/8320 -> JML C0:0000 |
| return | reload + **C0/1A03** + fade-in C0/2824 (field frozen) | reload, no 1A03, `$0117/$0118` flip, C0/2824 | C2/2E72 restore + C2/2C93 rebuild + 17-frame object fade | boot loader (boot_loader.md) |

---

## 6. What the C port needs

Use harness frames and grade against the logical oracle, per the CLAUDE.md lag-frame rule. Let E = the frame
the field handler sees X (or the op-C8 frame + 1 for a shop).

**Open.**
1. E .. E+14: `$0119` = 14..0 (one DEC per frame). The picture shows the previous frame's value, so pictures
   E+1..E+14 = 14..1. Run **no** object code (both loops call only FD:C2C1 / C0/881E + FD:FFF7). Pause the clock
   from E+1 on the X path only.
2. C = E+15: the switch frame. The picture is black from here.
3. C .. C+15: black, no input, no clock (the init chain, NMI off). The first scan is at **C+16**, or C+17 with
   the saved-game SRAM of com_jogo_salvo (C2/D156 +150 lines). The only variable leg is C2/D156. Pricing it needs
   FF:F9C4 / C2/D19F costs per slot, which are not derived here (§7).
4. S0 = C+16: substate 0. P = S0 + (mode 1: builder yields = 2 on tab 0, 1 on tabs 2/3; logical oracle, no lag)
   / (mode 2: 4 on the logical oracle, 5 real; CPU-bound, not derivable without a cost model) / (mode 0: 1 logical).
5. P+1: substate 1 runs. D-pad accepted, A/B/L/R dropped (C2/E984 `$0D17` guard). Brightness 0.
6. P+2 .. P+8: menu brightness 2,4,..,14. P+9: 15. Pictures one frame later (P+3 .. P+10). A/B accepted from P+2,
   and a B before P+9 closes from the current value (§2.5).
7. The clock ticks in every menu frame from S0 on, lag frames included.

**Close** (B = the frame the edge is seen, normally 15 at that point).
8. B .. B+6: 13,11,..,1. B+7: 0. RTL at B+8. Pictures B+1..B+7 = 13..1 of the **frozen** last menu image, B+8
   black. From a partial value n, the close takes ceil(n/2) frames to reach 0.
9. R = B+8. The black window lasts until the first INC at R+W. W = frames to the first line 225 after
   [0x14E0-byte MVN (186 lines) + the field-load legs (`ct_lz_load_cost` + map build) + C0/286C's pass], **+1
   full frame** (C0/0B28 polls `$4210` to that vblank, then C0/EC60 waits one more NMI). Measured W = 40
   (loc $082) and 38 (loc $005). The pass's C0/0A50 arm (951 lines here, vs the 1046-line LOADC_FIRST_PASS for
   the whole pass) is the known unmodelled piece (field_load_cost.md §6).
10. R+W .. R+W+14: `$0119` = 1..15, pictures one frame later. The field is **frozen** (no scheduler) and input
    is dead.
11. R+W+15: the rest of one main-loop iteration runs (scheduler included). R+W+16: the first full iteration.
    The clock runs from R+W. Skip the party reconcile C0/1A03 on the op-C8 path.

---

## 7. Not derived / open

* **Video vs harness frames in the init chain.** The C2/940D DMA makes the oracle harness count one frame fewer
  than a console would show (§0). This note keeps harness numbering (what checks grade). Whether the port
  should also add the missing video frame (e.g. for the audio clock, "frame k due at T0 + k/60") is a user
  decision. The ROM does not settle it.
* **C2/D156's cost as a function of SRAM** (1096 vs 1246 lines measured). It is `JSL FF:F9C4` + C2/D19F or
  C2/D296 + C2/D2C4 per slot; neither routine was priced.
* **Mode 2 (C2/DACA: C2/ED31 command list `$FF:C213`, `JSL FF:F548`) and mode 0 (C2/E7C3) CPU costs.** These are
  measured only (1388 lines; ~2.8 frames). Without a cycle model of C2/ED31 and FF:F548 they are constants of
  these fixtures, not derivations.
* **X menu on the overworld under the logical oracle, modes 3/5/6, C0/19CE (mode 5) and F2/75D0**: not run.
* **The load screen's close** was measured only up to `JML C0:0000`. What follows is boot_loader.md's domain.
* `FD:C1EE`, `FD:C124`, `FD:FFF7`, C0/881E: called in the paths above, semantics not read here.

---

## 8. Repro (repo root; `ct.exe` = build/ct.exe)

```
ct.exe ROM --monitor "7E0119:1,7E0A00:1,7E0067:1,7E00BE:1,7E00B0:2,7E00B8:2,7E0D10:1,7E0407:1" \
  --from saves/battle_re/party3_field.state --btnscript "0:4,200:2,0:54,1:2,0:100" --diff --writer
  (same with CT_ORACLE_LOGICAL=1 CT_ORACLE_LOGICAL_TRACE=1 -> "1 hold(s), 91974 cycles held")
ct.exe ROM --hookpc <PC> --from <same> --btnscript <same> --frames 130      (legs: §2.2, §3.2)
ct.exe ROM --ppuregs saves/battle_re/party3_field.state --btnscript "0:4,200:2,0:37"   -> brightness=2 (picture f42)
CT_HOOK_CYCLES=1 ct.exe ROM --hookpc C281E8 --from <same> ...                (harness vs video frames)
ct.exe ROM --monitor ... --from saves/field_re/shop90_lake.state --btnscript "0:10,100:2,0:418,1:2,0:80"
ct.exe ROM --monitor "7E027C:1,7E001C:2,..." --from saves/ow_w0.state --btnscript "0:10,200:2,0:78,1:2,0:80"
ct.exe ROM --monitor ... --from build/_menuloader_saved.state --btnscript "0:1600,8:6,0:124,100:2,0:150"
  (_menuloader_saved = frame-1 state + saves/ct_real_save.srm, made by tools/gen_menuloader.py)
ct.exe ROM --loadcost 130 --from saves/battle_re/party3_field.state        -> 12/12 packets, 36.47 load frames
```
Per-frame luma: scratchpad `shell_re/lumaseq.py` (one `--peek` per frame, mean Rec.601 luma of the 256x240
image). Outputs are in `x_*_luma.txt` and `shop_*_luma.txt`.

---

## 9. PORTED (2026-09-24) -- src/field.c's menu phases

User queue item "model the menu shell's open/close timing". The port opened every menu at the switch with the screen
live at once (about 20 frames early) and closed it straight into the field's fade-in. Now:

| phase | what | ROM |
|---|---|---|
| 1 | the caller's 15 DECs: `menuT = k` on the k-th DEC, picture = `15 - k` (the frame's own `$0119`: see the correction below); the X path's first DEC is the edge frame, the shop's the next main-loop iteration (`menuT` starts at -1); the switch is `menuT = 16` | C0/193E, C0/0CCE |
| 2, `menuShellF <= P` | black, no input; no clock until S0 = C+16; `$0407 = 0` at C+15 | C2/800E..808C, C2/8073 |
| 2, P+1.. | the screen: first input call at P+1 drops A/B/L/R; `$0D10` (menuBri) 2..14,15 from P+2, the frame's own value. P = S0 + 2 (ribbon tabs 0/1/4/5) / + 1 (tabs 2/3) / + 4 (shop, logical) | C2/E984, C2/83B2/83D0 |
| 4 | C2/83CA over the frozen image: -2 a frame from the reached value (the first step on the B frame), RTL the frame after 0 | C2/82B2..82C1 |
| 5 | the field reload: black for W = ceil((ct_lz_load_cost(loc) + 190 lines) / load frame) | C0/01A5..C0/286C |
| 3 | C0/2824's 15-frame fade-in, field frozen (unchanged) | C0/2824 |

Graded by `menu-shell-open-close-vs-logical-oracle` (`tools/menu_shell_cmp.py`): the brightness level of every frame
on the X menu (party3_field, f3-f70) and the shop (open f9-f62, close f428-f440) against the LOGICAL oracle -- 0
frames differ; five mutations. `shop-screens-vs-oracle` re-aligned (OPEN 1 = the real-time oracle's lag frame in
C2/DACA) and still 0 px on all eleven moments. 46 menu checks re-anchored: their drives' first wait after the menu
opens grew by the shell's delta (19 on the field X path, 18 on the overworld, 22 on the shop), and their frame-only
lines moved with it (pictures unchanged).

**CORRECTED 2026-09-24 (the same day): the picture lag.** This section modelled every brightness as "the previous
frame's value" (menuBriPic), because it was measured with "port frame F = `--peek` F+1". A frame's brightness goes out
in the same NMI as its tiles and sprites, and the cartridge displays a frame's state during the NEXT frame: `--peek`
F+2 is the port's frame F (tools/menu_audit_cmp.py; memory ct-frame-alignment-p2). tools/menu_shell_cmp.py now peeks
F+2 and the port shows each frame's own `$0119` / `$0D10` -- the same 0 frames differ on all three drives, with no lag.

**OPEN, not derived:**
* ~~**W is short**~~ SETTLED 2026-09-25, section 10: the reload is priced cycle for cycle; loc $082 and $005 are
  exact on every frame through the fade-in.
* **The overworld's return** (C2/2E72 restore + C2/2C93 rebuild, ~37 black frames) is not modelled: after the fade-out
  the port goes straight to the overworld's fade-in object.
* The one frame of section 3.2's R+W+15 (the scheduler runs while the fade's loop exits) -- the port resumes the field
  on the next frame.

## 7. PORTED (2026-09-24): the OVERWORLD open on the logical oracle, and the Save list's half colour math

Graded by `menu-overworld-save-screen-vs-oracle` (tools/menu_audit_cmp.py's owsave drive on saves/ow_w0.state: X, L to
the Save tab, A, B, B; f100-f300 0 frames differ; four mutations). The port's overworld open was 4 frames early:
* **C2/2535 -> C2/2E23 -> C2/2552** (after the fade object's 17 frames): NMI off and the six-block stash, 0x60F8 bytes by
  MVN at 7 CPU cycles a byte -- three FastROM instruction fetches (6 master cycles), the WRAM read and write (8), two
  internal (6) = 46 master cycles -- 1,141,904 cycles = 3.29 of a load frame (CT_LOAD_FRAME_CYCLES): three black
  frames, C2/8000 in the fourth. MEASURED (logical): `$001C` = 80 at f97, `$0A00` = 01 (C2:8020) at f101, `$0D00` = 1
  at f117, `$0D10` 02 at f121 .. 0F at f128 -- the field's C + 18 shell.
* **The switch's frame**: the overworld block flips to the shell before the menu block's `menuShellF++` of the same
  frame (the field's switch runs after it), so it starts at -1: C2/8000's frame is the shell's C, as on the field.
* **The Save list's dim rows** are the PPU's windowed half colour math (`math=07 half=1`, W1 16..239) in one ALU pass:
  the layer's colour plus the fixed colour, clamped at 0, halved, then capped -- on the chrome AND the text (BG3 is in
  the mask). The port halved the composed, capped pixel: a white border 31 + 8 gave 15 (123) where the PPU gives 19
  (156), "No data" 28 + 3 gave 14 for 15. The boot Load screen had this order already (CtMenuChromeLoaderAt).

* **The overworld's RETURN** (PORTED the same day; `menu-shell-open-close-vs-logical-oracle`'s ow-menu line, the
  brightness of every frame f78-f356 against the logical oracle, 0 differ): C2/2556 -> C2/2E72 (the six blocks MVN'd
  back, 1,141,904 cycles as the stash) -> C2/03EF -> C2/2C93 (the rebuild) -> C2/2566 (the fade-in object). C2/2C93
  runs the entry's decompression loaders minus four: `--cputrace` over the return shows twenty `JSL C3/0002`, from
  27A2 x4, 2B7C, 280C x7, 284F, 287D, 28AB, 28CC, 27D9, 2987, 29E3, 2959 (f302-f332); 29B5 / 2A11 / 2A6D / 2A3F never
  run, their packets surviving the menu. ct_lz_world_return_cost prices the twenty (10,830,114 cycles with the restore
  on world 0); the rest of the window is the measured remainder CT_OW_RETURN_REST (964,078 cycles, 2.78 load frames:
  34 frames from the RTL to the fade-in object's claim -- the entry's CT_OW_LOAD_REST is the same kind of number). The
  world is stopped through it (the port's overworld block skips phase 5; the menu block keeps it black and counts it).
  MEASURED: the restore at f299 (`$001C` = 80, C2:2E81), `$001D` = 01 at f336 (C2:216D) .. 0F at f350, `$027C` = 1
  (C2:258A) at f351.
* The overworld's picture after the menu still differs from the cartridge's, but it differs BEFORE the menu too: the
  port's --loadwram seed enters the world through a 44-frame load the cartridge's saves/ow_w0.state is past, so the
  ambient objects run on different phases (a seeding matter, not the menu's).

(The Save slots' per-save window colours: PORTED 2026-09-25, menu2_save_rest.md section 9.)

* **Settled (2026-09-25): the overworld open's stash phase.** On saves/tech_double.state the cartridge's black stretch
  between the world's fade and the shell's fade-in was a frame longer than the port's (ow_w0 matched): C2/2E23's stash
  runs with the NMI off (0x60F8 bytes of MVN = 3.29 load frames) and how many frame boundaries it crossed depended on
  where in the frame it began. User decision (CLAUDE.md's exception, second rule): the LOGICAL oracle starts NMI-off
  work at phase 0 (snes.c g_ctLogicalLoads). With it both states match the port, and the whole suite read the same.

## 9. The init chain PRICED (2026-09-25): S0 = C + floor(stretch / 346,886)

Under the logical oracle's NMI-off rule (CLAUDE.md) the init chain is one NMI-off stretch, from the caller's C0/0B4E
(C0/0B5D, the `STA $4200` that turns the NMI off -- BEFORE C0/011B's 0x14E0-byte save and the JSL) to C2/8087 (the one
that turns it back on). Its C non-refresh master cycles cross floor(C / 346,886) frame boundaries (346,886 = 357,366 -
262 x 40, the refresh LakeSnes charges at h = 536), and the scheduler's first scan is S0 = C + that many frames, C being
the JSL frame. MEASURED on six traces (tools/menu_init_legs.py, tools/menu_init_cmp.py; refresh counted as the h-536
crossings, tools/ctlib/beam.py): the new game's naming 16.970 -> 16, party3's X 16.977 -> 16, colsave3's load list
17.798 -> 17, ct_real_save 17.542 -> 17, three saves 19.451 -> 19, a corrupt slot 18.139 -> 18.

The legs (colsave3's load list, non-refresh cycles, each with its JSR):

| leg | cycles | depends on | port |
|---|---|---|---|
| C0/0B5D .. C0/011B (+ C0/0918's MVN) .. the JSL | 247,602 | the PC slots `$0197/99/9B` (+300 each present), `$0129`, the caller's arm (X 84, C8 $80+ 268, C8 odd 188 with the JSL) | walked |
| C2/8000 prologue | 580 / 624 | mode 0's `$7F0061` test | walked |
| C2/8096 head + five sound posts | 26,072 | the SPC's echo: +-200 idle, post 3 up to 33,420 with a song playing | the idle driver's constant -- APU phase, NOT derivable |
| C2/940D (VRAM clear DMA, WRAM fills) | 1,018,364 | the DMA sync phase (+-18) | constant |
| fills | 2,072 | -- | constant |
| C2/968D (graphics) | 1,977,700 | DMA sync (+-18) | constant |
| **C2/D156 (the save-slot scan)** | 1,737,858 | the battery | **walked, exact** |
| C2/984A / C2/8663 / C2/92F4 / C2/F3CA | 25,242 / 10,464 / 1,418 / 289,874 | (C2/8663: DMA sync) | constant |
| C2/816A (roster) | 2,374 | `$2980-88` | walked |
| C2/9875 (bag cleanup, item tables, the seven sheets) | 831,482 | the bag (C2/9988), the sheets (C2/9137's C2/91B4 arms), FF/F813 | constant -- NOT WALKED YET (+-1,800) |
| HP-bar scale, C2/806B .. 8087 | 2,630 + 236 | -- | constant |

C2/D156, per slot: FF/F9C4 (778 with no signature; with `E41B` at `$30:7FF8+2s`, FF/FBE7 sums 1280 words -- ONE CLC, the
carry chains through the whole sum -- and compares with `$30:7FF0+2s`: 118,958), then C2/D296 (an empty slot: string
`$1A` of `$FF:D03E`, "No data" behind ten spaces, 441,404 with the pick) or C2/D19F (a save: the three members, the
level, the flag, C2/D266's DEX scan of `$FF:D024` for the chapter, the chapter's title through C2/D296), then C2/D2C4
(string `$0E+s` of `$FF:D3FE` and `$C2:D329[s]` one-pixel shifts of C2/D306). The VWF (C2/F90C -> C2:0003 / C2:0009,
each a vector `JMP` of 18 -> C2/58B2 -> C2/5DC4): per glyph a fixed 5DC4 prologue/epilogue and C2/5E36's 24 row calls,
each C2/5E89 = head 120 + (odd code 120 | even 72) + 176 + the arm of `$6A` = pen & 7 (0: 264; 1-4: 308 + 12 n; 5-7:
408 + 62 (8 - n)); the pen advances by `$C2:60E6`. Every instruction's cost is LakeSnes's, the trace's base per PC
(tools/cycle_costs.py). The port (src/menu_cost.c, `Belthasar --menucost`) and its Python twin (tools/vwf_cost_proto.py)
match the cartridge's C2/D156 to the cycle on five batteries (check menu-open-slot-scan-cycles-vs-oracle).

field.c prices the stretch at C for the field's callers (X, op C8) and uses S0 wherever it used C+16 (C+15: `$0407`,
the roster / config / sheets; S0: the screens' builds; the clocks). The overworld's X keeps 16 (its stretch -- C2/2535
and C2/2E23's stash -- is not priced here). The load list now lights on the cartridge's frame (tools/naming_cmp.py
loadlist: f218-225 identical). STILL OPEN: C2/9875's walk; the posts' APU phase (a decision like the beam's);
the DMA sync jitter (tens of cycles).

## 10. The field's RELOAD after a menu PRICED (2026-09-25): W = floor(C / 346,886) + 1

After the shell's RTL the field reloads with the NMI off: C0/0B4E's C0/0B5D (`STA $4200` = 0) -> C0/01A5 (C0/0905
restores `$7F:2000` -> `$7E:0920`, then the location's own load legs) -> the caller (X: C0/1A03, C0/B192; op C8:
C0/B192, the `$0117/$0118` flags) -> C0/2824 -> C0/286C, the first pipeline pass, which ends in C0/0B28: three FD calls
and the vblank wait C0/0B38 (`LDA $4210 / BPL`), then `$4200` = 81. Under the logical oracle's NMI-off rule the stretch
starts at phase 0, so the wait is entered floor(C / F) frames later and the NMI returns at that frame's end; C0/2824's
first `INC $19` is one full frame after (C0/EC60). The port's black frames after the RTL's: W = floor(C / F) + 1.
MEASURED (logical oracle, tools/reload_cost_cmp.py): loc $082 (party3_field's X) C = 13,245,448 = 38.184 F -> 39,
first INC f109 after the RTL at f69; loc $005 (shop90's shop) 12,552,248 = 36.186 F -> 37. The old estimate (the
location-load model + 190 lines, rounded up) gave 38 / 36: the model's C0/286C constant `LOADC_FIRST_PASS` (1046
lines, measured on door loads) left out the objects C0/B0E6 DRAWS after a menu -- 4 on loc $082, 8 on loc $005,
~52,000-70,000 cycles each -- and C0/0A50's BG3 builder (298,554 + a $1000-byte upload) where the map has one.

The walk (src/field_cost.c, `Belthasar --reloadcost`; its Python twin tools/pass_cost_proto.py; every block's cost
read off the beam traces by tools/pass_cost_consts.py, arms no trace ran priced by the KEY of an executed instruction
-- opcode, widths, the region its operand lands in -- tools/ctlib/costtab.py):

| leg | depends on | walked |
|---|---|---|
| C0/0B5D .. C0/01A5, C0/0905 + FD:C2C1/C1EE | FD:C1EE's table by `$0153 & 0F` after C2C1's flip | 610; 251,060 + 18 / 30 |
| C0/0960 tiles, C0/6DCF BG3 tiles | the tileset list (`$F6:1C00`, `$FF` = none), record byte 2 | per packet: the loader's own (578 / 498 / 484 / 824), `JSL C3:0002` = 18 + lz.c's exact cost, C0/2DC8 = 436 + 8 n |
| C0/7084, C0/7F7E | -- | 10,152, 2,820 |
| C0/A33B map | the map packet, its header, record bytes 2 and 10, the `$E5:0000` list | lz.c's decompression + `ct_map_load_cycles` (walked: the header parse, C0/7399's masks, C0/A521's planes and RLE, C0/A66B; exact on both) |
| C0/09DD, C0/0A14, C0/56D4 | FF arms, `$01BB` | 554 / 586 / 7,186 + the packet |
| C0/595C, object 0's slot 1 | the script (decompressed to `$7F:2000` by C0/56D4) | ops 00 / 12 / 16 and the eight `$6467` comparators (true = CLC, fall through): 206 empty, 5,532 on loc $082 |
| C0/0209-024B, C0/1A03, C0/B192 | `$01AE`, `$7F:03FE`, the party vs `$0194-96`, n = `$7F2000` | 100 + 18 / 42 + 40; 272 (same party); 578 + 80 n |
| C0/286C: C0/28E1 | the opened-door list `$7F:1CC8` (the port keeps it since ade848d: C0/28AA / 28C0 / 28E1, disasm/notes/field_door_list.md) | per door by tile type, over the ROM's rebuilt map |
| C0/0A50 | BG3 (`$0BCA` & !`$01BB`), C0/A712's chests (`$F5:F000`, flags `$7F:0001`), the camera (C0/7506/7553 on `$0102/03`) | walked; the builders C0/75E9 / 78EC are constants (their parity tests run on the camera's even coordinates), C0/7F9A too (C0/74C7 zeroes `$1D99-9E`) |
| C0/A7E9, C0/B204 | n | 326 + 66 n, 11,966 + 78 n |
| **C0/B0E6** | per object: `$1100`, `$1A81`, the camera window on `$1801/$1881`; a drawn one: the palette allocator C0/7170 (share / claim by `$1400`), the VRAM slot C0/6FBA, C0/AB45, C0/A9CD, C0/CAD9 class 0 (C0/CB3A's frame from `$E4`, the 16 pieces: bit 14 -> C0/E534 = 2,800 + 32 reads of the graphics bank `$1200,X`, else C0/E687's arm by that bank; the template's signs and `< E8` arms), C0/B1B2's two DMAs, C0/EA42 | **walked** |
| **C0/B271** | the `$0E00` buckets, C0/B309 -> C0/B8CA's Y class (A >= $100 five checks, B carry / E0, C sign / E0), the group `$0F80 & 0C`, the stale-tail fills to C0/01B4's marks | **walked** |
| the rest of the pass | -- | 874,897 (the three traces' remainder, spread 38 = the DMA sync) |
| C0/0B28's FD:C1EE | `$0153 & 0F` after the pass's C2C1 flip | 18 / 30 |

Graded (field-reload-cost-vs-oracle): the pass alone -17 / +21 cycles (and -5 on a door load's, loc $078), the whole
stretch -19 / +23 -- the DMA legs' sync -- 39 / 37 frames, the cartridge's. (First -1,315 / +1,097: lz.c's map build
MODEL, whose `MBC_ONESHOT` was a median of five locations; C0/A33B is walked now -- `ct.exe --loadcost` reads diff +0
on both -- and every location load, the battle return and this reload use the walk.) The port's OWN WRAM at the RTL
gives the same totals (loc $082 identical, loc $005 +-12: `$0153`). menu-shell-open-close-vs-logical-oracle now runs the
X menu to f127 and the shop close to f499: every frame of the reload and C0/2824's fade-in is the cartridge's.

NOT WALKED (the walk says so, field.c falls back to the load estimate and prints why under CT_MENUSHELL): layer / HDMA
configurations no trace ran (`$1D35` bits 1-2, `$0BC9`, `$0126`, `$1DF9/$1DFD`, `$0153` bit 7, `$013C` bit 0, `$0BE9`,
`$0351` bit 0 -- 450 of 512 locations have neither `$0BC9` nor map byte 1 bit 5, per their map headers); the palette
allocator's C0/72B4 share scan and C0/7199 share; CAD9 classes 1-2 and `$1780` = 2 / 3; an opened chest (C0/A7E6);
an FC door tile; slot-1 ops other than 00 / 12 / 16; a party changed in the menu (C0/1A1C); `$01AE` >= 0 or
`$7F:03FE` = 1-2; a map whose header bytes 2-3 are set (C0/A463 / C0/A4B3: the model stands in). And the DMA legs'
sync (a few cycles each). The door path (every location load) still uses
`LOADC_FIRST_PASS`: its C0/286C draws only the objects the loader has placed, and pricing it from the port's state at
the load's commit is the next step.

### 10.1 The wait falls through inside a vblank (2026-09-26) -- W is not always floor + 1

A third fixture, party3_field's X right after a 16-frame walk to the right (tools/reload_cost_cmp.py `walk`): the
cartridge's stretch is 13,189,458 cycles = 38.022 F and its first `INC $19` is the RTL + 39 (the standing close's
38.184 F is RTL + 40). C0/0B38 `LDA $4210 / BPL` polls RDNMI, which LakeSnes (the PPU) sets at line 225 and clears at
line 0 or on the read (snes.c): the stretch starts at phase 0 -- a vblank's start -- so after floor(C / F) boundaries
its remainder is measured from a vblank's start, and inside the 37 vblank lines (37 x 1,324 non-refresh cycles) the
flag is still up and the wait falls through in that frame. W = floor(C / F) + (rem + 18 >= 48,988 ? 1 : 0)
(src/field_cost.h ct_nmi_wait_frames; +18 the LDA's read after its fetches): 082 38.184 -> 39, 005 36.186 -> 37, the
walk 38.022 -> 38 -- all three the cartridge's.

### 10.2 The fade-in is drawn from the RELOADED field (2026-09-26) -- user defect #21

C0/2824's iterations (`INC $19 / JSR C0/881E / JSR C0/00DE / JSR C0/EC60`) let the NMI show the reloaded state: the map
C0/01A5 rebuilt, the camera C0/7506/7553 put on the arrival tile `$0102/$0103`, the objects C0/286C drew. The port
re-presented the frame its menu froze at the OPEN, then cut to the live field: on a menu opened mid-walk the camera
jumped at the fade's end ("the screen jumps"), and even standing still 16,306 px of every fade frame were the stale picture's.
src/field.c's `fieldResume` now runs the field's own frame for those 15 frames with the scheduler, the controller and
the input frozen and the ramp as the brightness; left: the drawn objects' frames (C0/B127 `$1681 = 0` for the fade),
313-353 px. Check menu-resume-fade-in-vs-oracle (tools/menu_resume_cmp.py).

## 11. Under the rule "NMI-on work never costs frames" (2026-09-30, CT_ORACLE_NMION_HOLD=1 during the migration)

The logical oracle now holds the start of vblank until the main thread waits, with no one-frame valve (CLAUDE.md). Measured
on every open in this note (`--monitor 7E0067:1,7E00BE:1,7E00B0:2,7E0D10:1 --diff --writer`, CT_ORACLE_LOGICAL_TRACE=1):

| open | substate 0 | old valve | the rule |
|---|---|---|---|
| X menu (party3_field), overworld (ow_w0) | the ribbon builder: 1 held frame, then its own yields C2/A0AC / A0BD | spawn S0+2 | the same (one hold, no rollback in either) |
| shop (shop90) | C2/D52B `JSR C2/DACA`, no yield | 4 rollbacks at C2:EF41 / FF:F56x..F5Ex + a hold: spawn S0+4 (f47) | one hold of 1,540,818 cycles at C2:81CE: spawn **S0** (f43), `$0D10` 02 at f45 .. 0F at f52 |
| load list (colsave3's `C8 41`) | C2/E613 `JSR C2/E7C3`, no yield | 1 rollback at C2:E797 + a hold: spawn S0+1 (f21 from the C0/0B5D state) | one hold of 499,892: spawn **S0** (f20) |

The closes, the reloads (NMI off, the phase-0 rule) and the init chain's frames do not move. field.c: `menuShellP = S0` for
the shop and the load list under menuRuleNmiOnHold(); the ▼ task's first step (CtMenuShopSpawnFrame) now follows the build
on P's frame, since P = S0 puts both in one frame (the blink's toggles f51, f59, .. on the cartridge). Two things the old
phase hid: C2/82B2's `LDA #FF / JSR C2/8255` kills the ▼ task and the string task on the B frame itself (menu.c: no step
on a close; shopsell's close f591: `$0DA0` / `$0DA3` frozen at 01 / 00); and the init chain's stretch (section 9) comes out
48 cycles shorter from the rule's boot (the five posts' APU phase -66, the two DMA legs' sync +12 / +6 -- the open legs of
section 9, not the rule's), S0 unchanged. tools/menu_init_cmp.py finds C2/8000's frame (`--hookpc C28000`) instead of
assuming it: the rule's boot reaches loc 0's `C8 41` two frames sooner.
