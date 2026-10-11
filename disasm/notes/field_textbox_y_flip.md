# The message window moves on a button: C0/1916 -> C0/1985 (Y), and the X menu re-requests the box (C0/011B)

User report (2026-10-09): "ao apertar X com textbox aberta ela muda de posicao". Taken as a pointer, not a spec:
what does the cartridge do with a field message box open when a face button is pressed?

## 1. The ROM

The field main loop C0/0097 calls C0/18D9 (the system-button poll) at C0/00A1, BEFORE C0/1AAC (A) and the box machine
C0/1F87 (C0/00AD). Its legs:

```
18D9 LDA $00F0 / BIT #01 / BEQ ->1916     ; Start (the pause) -- $0111 == 0 && $011F != 0
1916 LDA $00F6 / BIT #01 / BEQ ->1923     ; $00F6 bit 0
191D JSR C0/1985
1920 LDA $00F6
1923 BIT #40 / BNE ->192F                 ; $00F6 bit 6 -> the menu (192F: $011F != 0, $0162 == 0, $0110 == 0)
1927 LDA $62 / BEQ ->192E / JSR C0/1ADF   ; the decision cursor
...
1985 LDA $26 / BEQ ->198E                 ; a window exists ($0126: 1 = top, 2 = bottom, 0 = none)
1989 EOR #03 / STA $26 / RTS              ; -> it goes to the other side. NO other gate.
198E LDA $7F0000 / SEC / SBC #49 / BCS   ; no window: storyline >= 0x49 -> the $011F/$0162/$0110 gates, the fade,
...  19C1 JSR C0/0B4E / JSR C0/011B / ... LDA #05 / JSL C2/8000   ; menu mode 5 (the exchange screen, C2/C7A6)
```

**$00F6 is config-remapped.** The pad latch C2/84D2 -> C2/8545 builds `$F6 / $F8 / $FA` from the new-press / held /
autorepeat words through the button config `$2993-$299A`, one `AND / CMP / ROR` per slot (C2/8555). Eight RORs: the
FIRST test ends in bit 0, the last in bit 7, so bit 0 = slot `$2998`, bit 6 = slot `$2995`, bit 7 = slot `$2993`.
The `$F2` layout is `A=80 X=40 L=20 R=10 B=08 Y=04 Sel=02 St=01` (C2/8502-8517). The default config read from
saves/intro_box.state is `$2993.. = 80 08 40 08 02 04 10 20`: `$2998 = 04` = **Y**, `$2995 = 40` = **X**.

So: **Y moves the window** to the other half of the screen, whatever the box is doing (opening ramp, typing, waiting)
and whatever `$011F` says. **X never touches `$0126`**: it opens the menu when `$011F != 0` (an ambient box, e.g.
Taban's `BB` with no `E3 00`), and the menu's C0/195B `JSR C0/011B` starts with

```
011B LDA $29 / BEQ ->0129                 ; a box is up ...
011F LDA #01 / STA $29                    ; ... back to state 1 (requested)
0123 LDA #00 / STA $26                    ; the window is gone
0127 STZ $27                              ; height 0
```

After the menu C0/1F87 walks states 1..5 again and C0/1FCD creates the window anew: the side is re-resolved (the
op's `$0130`, else the speaker's `$0A80,X` sign -- a Y flip is undone), and C0/1FF4 `JSR C0/20F2` re-initialises the
string decoder (`$2A` the index, `$2B-$2D` the table, `JSL C2/0003`): **the box retypes from its first character**.

## 2. Measured (logical oracle, `CT_ORACLE_LOGICAL=1`)

* Y, saves/intro_box.state (Mom's box, `$011F = 00`, `$0126 = 02`, `$0129 = 07`):
  `--monitor 7E0126:1,7E0127:1,7E0129:1,7E00F6:1 --input "0:10,2:2,0:20,2:1,0:20" --diff --writer`
  -> f11 `$00F6 = 01`, `$0126 = 01` (writer C0:198D); f33 `$0126 = 02` (C0:198D). Two presses, two flips.
* X, same state: f11 `$00F6 = 40`, `$0126` unchanged (the menu refuses: `$011F = 0`).
* X on an ambient box: saves/truce_market.state `--teleport 8 7 6 1` (Taban, `$011F = 01`), A at f5, X at f87, B at
  f188: the screen fades f87-f101 (C0:194B), at f102 `$0129 = 01` (C0:0123), `$0126 = 00` (C0:0127), `$0127 = 00`
  (C0:0129), `$0A00 = 01` (C2:8020); the menu closes f196; the field fades in f219-f233; state 2 at f234, `$0126 = 02`
  at f238 (C0:1FEC), state 6 at f248; `--peek` at f250 shows the window with "T" -- retyped from the start.
* Y then X (Y at f40, X at f87, B at f188): `$0126` 02 -> 01 at f41 (C0:198D), 00 at f103 (C0:0127), 02 at f239
  (C0:1FEC) -- the menu undid the flip.

## 3. The port before

* No C0/1916 leg at all: nothing ever wrote `$0126` but C0/1FCD and the close ramp (ct_vm.c tbMachineTick).
* The host drew the window from its OWN side (`tbTop`, computed once at layout from the speaker actor's screen Y),
  not from `$0126`.
* (Keyboard: main's one-pad table fieldKeyPad maps Z = A, X = B, A = pad X, S = pad Y -- so the key S reaches this
  leg; keyboard X is pad B, which C0/18D9 does not read for the box.)
* The menu's C0/011B save had no head: the box survived the menu as it was and kept typing where it stopped.
* Lockstep (scene 7 obj13 fixture, CT_PROJECT=box, Y at f40/f60): `f41 DIVERGE box ours $0126=01 oracle $0126=02`.

## 4. The fix (src/field.c)

* C0/1916's slot, next to C0/1927's (mlHead, same guards): `$00F6 & 01` and `$0126 != 0` -> `$0126 ^= 3`.
  `$00F6` is the port's own pad latch output (fieldPadNmi -> player.c CtJoypadPoll, the C2/8555 mask order).
* The window is drawn from `$0126` (tbTop is a view of it once the machine has created the window), as tbAnim is a
  view of `$0127`.
* The menu's C0/011B save runs the head: `$0129 = 1`, `$0126 = 0`, `$0127 = 0` when a box is up, and the host lays the
  string out again (its typewriter is C2/0003's model).
* Probe `CT_TBSIDE=1`: `tbside: f<N> $0126=<side> drawn top=<y>` at every change of the side.

## 5. Validation

* Lockstep, scene 7 obj13 (`CT_FIELDLOCK=saves/field_re/scene7_talk_obj13.state CT_PROJECT=box`, Y at f40 and f60):
  `project: box CLEAN over 149 frames` (was DIVERGE at f41). Check `lockstep-box-y-flips-the-window-vs-oracle`.
* Scene 8 (Taban), Y at f40 then X at f70, B at f180: `tbside` f5 02 (bottom), f40 01 (top), f209 00 (C0/011B),
  f228 02 (re-resolved bottom); the box retypes from "T" after the menu (shots), as the oracle's f250 peek.
  Check `field-textbox-y-flip-and-menu-rerequest`.

## 5b. The user's literal report on main 1b28fed (before this change)

Main's keyboard is one pad (cc05d9f, fieldKeyPad): keyboard X = pad B, keyboard A = pad X, keyboard S = pad Y. Main had
no C0/1985 and drew from its own side, so a probe build of main (main's field.c + one printf of the host's window top
beside `$0126` / `$0129`, not committed) answered what moves the box there:

* keyboard X (pad B): scene 8 Taban's box, B at nine timings (frame 1..131, every box state from the request to the
  wait): the final frame identical to the no-press run (fnv32 B5545C35 all ten). On the cartridge B is `$00F6 = 0A`
  (slots `$2994`/`$2996`) and `$0126` does not move either (truce_market `--teleport 8 7 6 1`, B at f46 / f67).
* keyboard A (pad X, the menu) at five timings (f2, f4 -- inside the opening states --, f11, f70, f141): the host's side
  and `$0126` never change, the box keeps typing where it stopped (interactive run too). Scene 7's talker refuses the
  menu (`$0110 = 01`). Scenes 2, 7, 8 and 254: the host's side always equals `$0126`.

So on main **neither key moved the box**; nothing in main's port is a defect that answers to X. What the cartridge
does on those inputs: B nothing; the X menu re-requests the box (C0/011B) and re-resolves its side on the way back --
which changes the position whenever the side it was showing is not the one C0/1FCD would pick now (a Y flip undone,
measured above; or a speaker that moved across the screen's half meanwhile, by the same 1FDD rule) -- and Y moves it.
The likeliest reading of the report is that cartridge behaviour (with the cartridge-side key the user pressed not
known here). With this change the port does all three as the cartridge does (sec. 5).

## 6. Open

* **The mode-5 arm** (C0/198E-19CE, storyline >= 0x49 with no window) is not ported: no repo state reaches it
  (max `$7F0000` in saves/ is 0x15, menu2_form_screen.md AV.3).
* **Scene 7's own Y poll**: its script consumes the Y latch (`$0151` bit 2, C0/675C-6766). With Y at f12 or f120 the
  oracle's NEXT box is requested 3 frames before the port's (f147 vs f150, f234 vs f237) -- identical with the binary
  before this change (the flip is not involved): the event VM's reaction to a polled button, not the window.
* **Menu round trip timing**: from the B press to state 2 the cartridge takes 46 frames (f188 -> f234) and the port 45
  (f180 -> f225) in the drives above (different fixtures, the input conventions of each side not aligned) -- not
  measured further; the menu shell's close is not this change.
