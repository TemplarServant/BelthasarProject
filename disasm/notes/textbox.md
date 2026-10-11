# The CT field TEXTBOX -- full RE (window engine, position rule, colour schemes)

RE'd via a 3-agent workflow (locate -> decode -> adversarial empirical verify, 9 live oracle
runs) + fb pixel archaeology. All addresses = USA ROM, bank C0 unless noted.

## Opcode handlers (dispatch table C0/5D6E, JSR ($5D6E,X) at C0/5AB1)
0xBB auto -> C0/3570 | 0xC0 choice-auto -> C0/3643 | 0xC1 top -> C0/35B5 | 0xC2 bottom ->
C0/35FC | 0xC3 choice-top -> C0/36AA | 0xC4 choice-bottom -> C0/36DD. Shared skeleton:
first visit sets $7F0A00,X, string idx -> $2A, speaker ($6D) -> $2E, open request $29=1 and
the POSITION REQUEST $30 = 0/1/2 (auto/top/bottom); yields while open; close visit advances
IP (+2, or +3 for the choice ops whose 2nd operand packs first/last option lines -> $64/$65).

## THE AUTO POSITION RULE (C0/1FCD, state 5 of the box machine C0/1F87)
Resolved position lives in $26 (0 none / 1 top / 2 bottom). When $30==0 (auto): X = $2E (THE
SPEAKER, not the player!); LDA $0A80,X (the actor's SCREEN Y in px, written per frame by
C0/AB45 = worldY/16 - cameraYpx); BPL -> bottom else top. I.e. **speaker screenY >= 128 ->
box at TOP; < 128 -> box at BOTTOM** (it never covers the speaker). Verified live 9/9 runs
incl. a same-speaker 127-vs-128 one-pixel experiment; player-keyed and worldY-keyed rules
both falsified.

## Window geometry + the HDMA placement trick
The window is NOT drawn into the map tilemap: a STATIC 32x32 tilemap at VRAM word $1C00
(rows 8-17: frame tiles $0F0-$0FB + weave fill $0FC/$0FD with flips; rows 0-7: the 8 glyph
rows, BG3 2bpp tiles $100+, chars rendered at word $5800+ by JSL $C20003/09). Per-scanline
INDIRECT HDMA bands (ch0 -> $2107-210A) switch BG1SC AND BG3SC to $1C00 inside the band,
ch1/ch3 set in-band scrolls (top: BG1V=0x37, BG3V=-15; bottom: BG1V=-68, BG3V=-138), ch4
sets TM=$01 (frame only) for the first/last 10 lines and $05 (frame+text) for the middle 60.
**The box is 80px tall; TOP = screen y 8..87; BOTTOM = y 131..210** (13 map lines stay
visible below). Open anim: $27 += 5/frame to 0x28, band height = $27*2 (center-out, 10px/f);
close: -4/frame. The vertical gradient is COMPUTED: HDMA ch6 -> CGADSUB/COLDATA, top 40
lines ADD grey +8..+1, bottom 40 SUBTRACT 0..7 (5 lines/step, FD/D52D builds the table).

## Window colour schemes (Options menu)
ROM table FF:B210: 8 records x 16 bytes (8 BGR555 colours). scheme = $7E2991 & 7; loader
C0/70E9 MVNs colours 1-7 into CGRAM row 0 and then FORCES entries 9-11 = 0C63/18C6/739C
(text greys/white) so text is readable in all schemes. Scheme 0 (default) = GREY:
3DCE 73FF 39CD 316B 2507 20E6 1CC5 0843.

## The C port (src/field.c)
The window is blitted from CT_ASSET_WINDOW (a 256x80 bitmap lifted from a text-free oracle
fb capture, text region repaired via the fill's 32px horizontal period; saves/ct_window.bin)
-- verbatim at native height, 9-patched for choice boxes; procedural grey fallback. Speaker-
keyed auto rule via CtSceneTextboxObj; TB_TOPY 8 / TB_BOTY 131; text origin (8, top+10);
center-out open anim (+5/frame). Future: pack FF:B210 for the 8 schemes; close anim.

## The machine ported (2026-09-03; defect #2 of the 2026-09-02 list, 'smash A repeats the text')

Everything the ROM's box machine C0/1F87 does to WRAM now happens in the engine, at the ROM's own frame
slot (C0/00AD: after the A handler, before the VM), so the `CT_PROJECT=box` lockstep can grade it. The
measurement that drove it -- scene 7's obj13 (`BB 3F`, the dance tutorial's two lines), the lead teleported to (9,14) facing up, A every 3 frames, `--monitor 7E0129:1,7E0126:1,7E0134:2,
7E01EB:2,7E119A:2,7E1C1A:1`:

| iteration | oracle |
|---|---|
| f3  | the press: C0/5AC5 accepts (obj13 prio 7 -> 1, ip -> fn1), `BB` runs, `$0129 = 1` |
| f4-f7 | `$0129` 2, 3, 4, 5 (1FB4 then 1FC0, one state per frame; `$0131` 0,1,2,3, `$0136` 1) |
| f8  | state 5's resolve: `$0126 = 1` (top: obj13's `$0A80` screen Y >= 128), `$0127 = 0` |
| f9-f18 | the open ramp 1FF8, `$0127` += 4; at $28 (f18) 2045: `$0129 = 6` + the typewriter's SETUP iteration (no character, `$0136 = 1`) |
| f19-f73 | one character per iteration, the line break costing one of its own (f33, `$0131`++); the LAST character's iteration is the one that leaves: 21D8 `$0129 = 7`, 21C2 `$0134 = FFFF` (f73) |
| f75 | the first press in state 7: 1ABD spends it (`$0134 = 0`), 20A5 -> 2058 -> 2081 `$0129 = 0D` |
| f76-f86 | the close ramp 2032, `$0127` -= 4; at 0 (f86) 204D `$0129 = 0 / $0126 = 0 / TRB $54 #20` |
| f87 | the op's completion visit (3635 clears `$7F0A00,X`), RETURN: prio 7. The press this frame is declined by 5AC5 (`$1C00,X` was still 1 when the A handler ran) |
| f90 | the next accepted press: the second box. 4 boxes in 300 frames. |

What the port did before, on the same fixture and drive: **17 boxes in 300 frames**. Two causes, both
structural:

1. **The string table was lost on every WRAM seed.** Op 0xB8 (C0/3557) stores the 24-bit table pointer at
   `$012B/$012C/$012D`, and op 0xBB's first instruction is C0/3570 `LDA $2D / BNE` -- no bank byte, no box.
   The port kept the pointer only in a C field (`sc->tbTable`) filled by a create-time prescan, so a
   `--loadwram` scene (the seed replaces the memory of a fresh scene whose 0xB8 ran long before the capture)
   read -1, `CtAssetString` returned NULL, the box was EMPTY, its reveal was complete at once and the next
   press closed it -- a box every 17 frames. Now the op writes the three bytes and `CtSceneSeedFromWram`
   reads them back (table = `$DE:E300` for scene 7, the dialogue dump's 'table @0x1EE300').
2. **The host's box opened five frames early and could not be graded.** Under `--selftest` the window popped
   fully open (`tbAnim = 40`, no ramp -- a test-mode shortcut the ROM has no counterpart for), and in
   every mode the window started ramping on the request frame while the ROM walks states 1-4 and the
   state-5 resolve first. The port also never wrote `$0129` 2-7, so `$0129` diverged on every frame a box
   was up.

The port now: `tbMachineTick` (ct_vm.c, at the top of `CtSceneTick`) walks 1 -> 2 -> 3 -> 4 -> 5, resolves
`$0126` from `$0130` (the op's immediate: 0 auto / 1 top / 2 bottom) or the speaker's `$0A80` sign, ramps
`$0127` to $28 and sets 6, and drains the close ramp (the code that was already there). The textbox ops
write the request bytes (`$7F0A00,X`, `$012A`, `$012E`, `$0130`, `$0132`, `TRB $54`), the treasure request
writes 1F10-1F1A's. The host's window height IS `$0127` now (its old open/close counters are gone), the
wait state IS `$0129 == 7 && $0134 == FFFF`, the press is spent as 1ABD spends it, a page advance sets 8
(`CtSceneTextboxPage`), and the host's typewriter reports the page's last character through
`CtSceneTextboxRevealDone` (21D8 + 21C2) after modelling 2126's iterations: the setup one, one per
character, one per line break, the last one leaving for 7. `--vmdiff` mirrors the oracle's 6 -> 7 the way
it already mirrored 7 -> 0D (that harness has no string).

Not claimed: `$0131`, `$0133`, `$0136` (written by 2126 as well as by the handlers -- half a writer set
grades worse than none), the `$0216` in-string pause (`$0134 = $0216 * 15`, C0/21A8; no string measured
so far carries it), and the page states' visual: the ROM re-decodes the page over 8-C (5 frames) where
the host still scrolls one line over 8 frames.

Result: `project: box CLEAN over 299 frames`, 4 boxes on both sides; check
`lockstep-talk-box-machine-clean` (fixture saves/field_re/scene7_talk_obj13.*), five mutations.

Tooling caught on the way: `--btnscript`/`--input` masks are HEX (`100` = A, `10/20/40/80` = Up/Down/Left/
Right); `--hookpc` takes `--btnscript`, not `--input`, for its drive; the --loadwram seed now also seeds
the host's facing mirror from `$1600` (the probe C0/1ACA reads that byte -- ours was still the spawn's
'down' and never found the NPC).


### Three more things the port had wrong under the machine (same day)

* **`w8`/`r8` dropped bank `$7F` above `$0400`.** `Mem` carries the whole 128 KB (`hi` = `$7F0400-$7FFFFF`,
  seeded from every state), but the accessors routed only `$7E` + `$7F0000-03FF`; a store to `$7F0A00,X`
  vanished and a load read 0. The textbox latch lives there (C0/3588 sets it, 3635 clears it), so the op is
  now keyed on that byte -- `$0129 != 0` -> park (357A), latch clear -> REQUEST, latch set -> COMPLETE --
  instead of a C flag no seed carries. A state captured mid-box completes the box it was in; the seed
  rebuilds `sc->textbox`/speaker/string/position from `$0129`, `$012E`, `$012A`, `$0130`.
* **Op 0xE3 never wrote `$011F`** (C0/3C0B `STA $1F`; only the C mirror `sc->explore`). The A handler's
  first gate is C0/1AC1 `LDA $1F / BNE`, and the port's A handling had no such gate at all: a press during a
  cutscene's box (scene 120, Lucca's `E3 00 / C1 55`, control LOCKED) reached the probe and re-invoked her
  -- the very "smash A repeats the text" the user described, on a cutscene rather than a talker. Op 0xE3 and
  C0/0359 write the byte now and the host tests it before the probes. Measured on a scene-120 fixture
  (the check's flags grafted into forest_exit + `--teleport 120 37 11 1`): the ROM holds `$011F = 0`, never
  latches `$01EB`, and walks the whole chain box by box under the same 16-frame press cadence.
* **Ops 0x08/0x09 (C0/6240/624B) write `$1C01,X`**, the invoke prologues' first refusal; they were no-ops.

## The typewriter's COST MODEL: the indent belongs to the break TOKEN (2026-09-06)

Reported as *"on selecting the choice I get the impression that another text 'blinks' quickly"*. The
skip itself did not reproduce (see the section below); this is what the hunt for it MEASURED.

### The ROM

CT's line breaks come in **pairs**, and the difference is an indent the token carries itself
(`assets/text.py _NAMES`):

| bare | +indent | meaning |
|------|---------|---------|
| `05` `07` `09` | `06` `08` `0A` | line break (`'\n'` vs `'\n   '`) |
| `0B` | `0C` | page break (`'\f'` vs `'\f   '`) |

The typewriter `C0/2126` runs **one iteration per frame** while `$0129 == 6`. A control code is
handled in that ONE iteration -- `2130-213C` (page-break family) and `213E-214C` (line-break family)
fall into `215B`/`2161`, which set `$0131` (the LINE cursor: `INC A` unless it is already 3, then
wrap + `JSR C0/20B2` to scroll) and `STZ $32`, and then the code falls through to `216D` and calls the
renderer `JSL $C2:0009` once. **The three spaces are inside the token, so they are never typed.**

### The measurement

`build/ct.exe --monitor "7E0126:16" --diff --from saves/field_re/s439_marle_choice_lake.state`
with A at f100/f200/f300/f400. `$0131` (offset 11 in that band) is the ROM's own line cursor, so
every line's duration is directly readable. All **fourteen** line timings across str `0x29`'s and
`0x2B`'s four pages are `visible characters + 1 for the break`, with the token's indent free:

| line | text | chars | ROM frames |
|------|------|-------|-----------|
| 29 p0 l0 | `GIRL: Oh, thank goodness!` | 25 | 26 = 25 + break |
| 29 p0 l1 | `[06]My pendant!` | 11 | 12 = 11 + break |
| 29 p0 l2 | `[06]It has a lot of sentimental value!` | 34 | 34 (last line, no break) |
| 29 p1 l1 | `[06]   Yes.` | 3 + 4 | 8 = 7 + break (3 spaces are LITERAL, 3 are the token's) |
| 2B p0 l0 | `   I came to see the Festival!` | 30 | 31 = 30 + break (leading indent is LITERAL here) |
| 2B p1 l3 | `[06]   Sorry.` | 3 + 6 | 9 (last line) |

The last two rows are the whole point: an indent at the START of a string is real space characters
and IS charged; the same three spaces after a break token are not.

### The fix, and why it had to go in the ASSET

The port flattened `'\n   '` into four characters and charged a frame each, so **every indented line
finished three frames late** -- scene 439's first page 82 frames against the ROM's 72, scene 434's
`C0 40 03` box eleven frames late. The information needed to fix it in the engine alone is gone by
then: a scan of every dialogue table in the ROM (`_scan_indent.py`) found **16 places where a BARE
break token IS followed by literal spaces** (up to 21 of them), so *"the first three spaces after a
break are free"* is a rule that would be wrong there. So `pack_dialogue` now passes `ind='\x02'*3`
and the marker survives into the asset; `TB_IND` in `src/field.c` draws exactly like a space (4px)
and costs no typewriter iteration. Layout, both drawers, `pageChars`, `choiceOptions` and the reveal
loop all read it.

After: all four pages match the oracle's duration exactly. Checked with `CT_FIELDLOCK` +
`CT_PROJECT=box` on two scenes -- 439 went from `apart f91..f718` to `apart f101..f479`, 434 from
`apart f391..f525` to `apart f391..f514`. The residual is the **page ramp only** (`$0129` 8->C, ours
one state ahead): our press is consumed a frame before the SNES auto-joypad would deliver it, a
pre-existing input-pipeline phase left deliberately unpapered. Check
`field-textbox-typewriter-vs-oracle`, one mutation, KILLED.

### What was blind, and why (the honest half)

`field-decision-box-flow-vs-oracle` was built the day before *for this very complaint* and it stayed
green through all of it, because it pins **our own** `[tbop]` transition frames -- it froze the wrong
timing in as expected ([[check-vs-own-reference]]). `CT_PROJECT=box` could see it, but nothing ran it
on a decision box: the `$0129` it reads is written by OUR reveal counter, so a wrong layout stays
self-consistent and only the DURATION leaks -- and a duration needs the oracle running alongside.

Two instrumentation holes found in the same hunt:

* **No headless drive could move a decision cursor** -- CLOSED. `upPressed`/`downPressed` reached the
  choice cursor (`field.c`, the choice-cursor block) only from SDL keys; the drive grammar's `u`/`d`
  set `wsDy`, a LEVEL, which fed them only inside the naming screen. So `choiceSel != 0` -- every
  option but the first -- was structurally unreachable, every decision check picked option 0, and
  `CtSceneChoose`'s option -> line -> result map (`result = optFirst + sel`, ROM C0/36CC) was graded
  for exactly one of its values. `u/d/l/r` now also raise the d-pad EDGE on the token's first frame,
  as `a` raises A; movement is frozen while a choice box is up, so level and edge never contend.
  With it: the oracle, driven `A / Down / A` on `C0 29 06`, writes `$7F0A86 = 02` (the LINE, not the
  index) at f232 and jumps to `060F`, the refuse branch, opening no further box -- and ours completes
  box 41 on the SAME frame and requests nothing after it. Check
  `field-decision-second-option-branch`, mutation `line = option` (drop `optFirst`), KILLED: with it
  the refuse pick falls through to the ACCEPT path and Marle joins however you answer. **That is the
  exact shape of the report, and it is now graded.**
* **`$0131` is not ported** -- CLOSED 2026-09-24, see section 4 at the end. The ROM's own line cursor, the byte that says which line
  is being revealed, has no counterpart in the port, so no check can compare the LAYOUT itself, only
  how long it takes. It is what made all fourteen timings above readable on the oracle side.

### The reported SKIP: not reproduced

`skipped_dialog.state` (loc 439, storyline `06`, obj3 ip `05BD`, `$012A = 2B`, `$0131 = 2`, `$0129 = 7`)
is the ORIGINAL parked on page 1 of `C0 2B 0B` -- *"I came to see the Festival!"* -- the page the user
says our port skips after answering `Yes.` to `C0 29 06`. Rewinding obj3's ip to `05B0` and clearing
the box machine gives a state the ROM replays from the top (verified: it opens box `0x29` with its own
ramp at f4). On that fixture the port shows **the same four boxes in the same order** as the oracle --
`29 2B 2D 2E` -- at **every one of twelve A-press cadences from 8 to 120 frames**, with the same page
count, the same option lines, the same cursor row and the same branch (`$7F0A86 = 02` -> `05D3`). A
real scene load plus `--talk 3` (obj3 fn1 = body `0x019A`) with the capture's own flag page gives the
same four. `--vmdiff` is clean to f515; its one divergence at f516 is the harness having no chooser.
So the skip is NOT in the box sequence, the page split, the option mapping or the branch -- it needs
an F5 state from OUR engine at the moment it happens, the way `marle_no_caminho_bell.state` settled
the bell.

## ⭐ $0162 IS THE DECISION-BOX INPUT MACHINE -- PORTED (2026-09-06)

Found from the user's own F5 snapshot (`build/ct_debug.state`, scene 439, Marle's pendant dialogue,
box `0x29` parked in state 7). Ours reads **`$0162 = 0`**; the ROM, at the same station, reads 1.

`$0162` is `$62` with DP = `$0100` (18 `STA $62` sites, 54 `LDA $62`). The port calls it "the
cutscene lock" (`sc->moveLock = w[0x162] != 0`) and never writes it. It is a **four-state machine
that owns the input frame while a decision box is up**:

| value | meaning | writer |
|-------|---------|--------|
| 0 | no decision box | `C0/3669`, `C0/36D2`, `C0/3706` (`STZ $62`, the completion arms) |
| 1 | decision REQUESTED, cursor not yet armed | `C0/3697` (the op's request) |
| 2 | cursor LIVE | `C0/202F` |
| 3 | option CONFIRMED | `C0/1AFB` (the A handler) |

**The box machine's state-7 handler branches on it FIRST** (`C0/201C`):

    201C  LDA $62
    201E  BEQ ->2056          ; 0 -> the PLAIN box path: the $0134 press latch (2056 -> 20A5)
    2020  CMP #01 / BNE ->20A0
    2024  LDA $0215 / BNE ->2056   ; a control code still pending -> keep waiting
    2029  LDA $64 / STA $63    ; arm the cursor on the FIRST option line
    202D  LDA #02 / STA $62    ; cursor LIVE
    2031  RTL
    20A0  CMP #03 / BEQ ->2058 ; 3 = confirmed -> close the box
    20A4  RTL                  ; 2 = live, not confirmed -> DO NOTHING

⭐ So **on a decision box the A press is never the box's `$0134` latch** -- with `$0162 >= 1` the
state-7 handler never reaches 2056/20A5 at all. The press is taken by the CURSOR ARM instead.

**The cursor arm owns the whole input frame** (`C0/1927`):

    1927  LDA $62 / BEQ ->192E / JSR C0/1ADF / RTS     ; $0162 != 0 -> cursor arm, and RETURN
    1934  LDA $62 / BEQ ->1939 / RTS                   ; the ordinary field input bails too

    C0/1ADF (A = $0162):
      1ADF  CMP #01 / BEQ ->1B0C          ; requested but not armed: only the A-release bookkeeping
      1AE3  CMP #02 / BNE ->1AFE
      1AE7  LDA $00F7 / BIT #04 / BNE ->1B19    ; d-pad DOWN -> cursor down
      1AEE  BIT #08 / BNE ->1B29                ; d-pad UP   -> cursor up
      1AF2  LDA $00F8 / BIT #80 / BEQ ->1B18    ; no A -> nothing
      1AF9  LDA #03 / STA $62 / RTS             ; A -> CONFIRM
      1AFE  LDA $63 / BMI ->1B0C / CMP #04 / BEQ ->1B0C / STA $66 / LDA #04 / STA $63
                                                ; $0166 = the confirmed LINE, $0163 retired
      1B19  LDA $63 / INC A / CMP $65 / BEQ ->1B22 / BCS ->1B25 ...  ; down, wrapping $0165 -> $0164
      1B29  LDA $63 / BEQ ->1B32 / DEC A / CMP $64 / BCS ->1B22 ...  ; up,   wrapping $0164 -> $0165

The byte set: `$0163` = the cursor's current option LINE, `$0164` = first option line
(`(operand2>>2)&3`), `$0165` = last (`operand2&3`), `$0166` = the confirmed line, which the
completion arm stores to `$7F0A80,X` as the script's `result` (C0/365D).

MEASURED, oracle, `--monitor "7E0162:1,7E011F:1,7E0138:1" --diff` on
`saves/field_re/s439_marle_choice_lake.state`, A at f100/f200: `$0162` = **01** at f0 (box `0x29`
requested) -> **02** at f141, one frame after the box reached state 7 at f140 -> **03** at f201, the
press -> **01** at f212, the next decision box `0x2B`. Exactly the four states, in order.

### What the port does instead

None of it. `$0162` stays 0, so by the ROM's own dispatch every decision box in our engine runs down
the **plain-text** path, and the option logic is a parallel HOST implementation: `nopt` counted from
`CtSceneTextboxOptLines`, a `choiceSel` local, and `CtSceneChoose` computing `line = option +
optFirst` in C. It spends the press on `$0134` -- the latch the ROM never reads while `$0162 >= 1` --
and it does not block the rest of the input frame the way `C0/1927`/`C0/1934` do.

It reaches the right option in every case measured so far (both options of both boxes in scene 439
match the ROM, check `field-decision-second-option-branch`), which is why nothing caught it. But it
is a re-implementation standing in for a ROM mechanism, i.e. the thing the mandate forbids, and it is
the machine the user's reports keep landing on ("as if my press of A skipped a dialogue besides
selecting the option").

### PORTED

* the request (op 0xC0/C3/C4) writes `$0165 = operand2 & 3`, `$0164 = (operand2 >> 2) & 3` and
  `$0162 = 1` (C0/3683-3697), next to the stores it already made;
* `tbMachineTick` gains **state 7**, the `C0/201C` dispatch above;
* `CtSceneDecisionInput` is `C0/1ADF`, called every frame from `C0/1927`'s slot in field.c -- cursor
  down/up with the `C0/1B19`/`C0/1B29` wrap, A -> `$0162 = 3`, and the `$0162 == 3` leg publishing
  `$0166 = $0163` and retiring `$0163` to 4 (`C0/1AFE`);
* the completion arm stores `$0166` to `$7F0A80,X` and clears `$0162` (C0/365D-3669, and the
  0xC3/0xC4 twins C0/36C6-36D2 / C0/36FA-3706); `result` is that byte, not a host index;
* `$0215` is written by the host's typewriter when the reveal finishes -- 0 when the string is
  exhausted, the pending break code otherwise. It is the ONE thing `C0/2024` reads, and it is what
  keeps a multi-page decision from arming its cursor on a page that is not the last;
* `choiceSel` is now a VIEW of `$0163`, not a counter the keys move.

**Two things the port did NOT take, deliberately.** `C0/1AAC`'s `$0134` spend is *not* `$0162`-gated
-- it is a different routine from the cursor arm's caller `C0/1927`, and the oracle zeroes `$0134` on
the confirm frame too (f201) -- so the host still spends it; leaving it at `$FFFF` kept our `$0134`
apart from the oracle's from the confirm through the next box's whole open ramp. And state 7's own
`$0134` reader (`C0/2056` -> `20A5` -> `2058`) is still the host's page/close decision, so the arm
must run **after** the press block: running it first let its `1B0C` leg spend the latch under the host
and every multi-page decision silently stopped turning its page.

MEASURED after the port, same fixture and drive: `$0162` 01 -> 02 (f145) -> 03 (f205) -> 01, with
`$0163 = $0164 = 01` while the cursor is live on box `0x29` and `$0164/$0165 = 02/03` on box `0x2B`
(operand2 `0B`), `$0215 = 0B` on `0x2B`'s page 1 and `00` on its option page, and `$0166` ending at
02 -- the oracle's own `$7F0A86`. The whole conversation now runs **frame for frame with the ROM**:
obj3's ip moves at f212 / f512 / f516 / f612 / f664 on both sides. `CT_PROJECT=box` still reports only
the page-ramp phase (`apart f101..f479`). Check `field-decision-cursor-machine-0162`, mutation
(state 7 never arms the cursor), KILLED. Scene 434's `C0 40 03` box runs the same machine
(`$0164/$0165 = 00/03`, `$0166 = 00`) and completes on its pinned frame.

### $0215 IS SEEDED WITH THE WINDOW, not only at the reveal's end (2026-09-06, same day)

Reported again after the port landed: *"the line after selecting 'May I have it back?' -> YES
keeps skipping (shows very quickly) ... automatically, it should be 2 presses of A"*.

`$0215`'s whole life, MEASURED (`--monitor "7E0215:2,7E0129:1,7E0162:1" --diff` on
`saves/field_re/s439_marle_choice_lake.state`, A at f100/f200), for box `0x2B`:

| frame | `$0129` | `$0215` | |
|---|---|---|---|
| f212-f216 | 1..5 | `00` | still the PREVIOUS box's value |
| **f217** | 5 | **`04`** | **the window is created -- the byte is SEEDED** |
| f227 | 6 | `10` | the typewriter's ordinary-character code |
| f257 / f292 | 6 | `06` | each line-break token as it is consumed |
| **f327** | 7 | **`0C`** | the code the page ENDED on: a page break -> the cursor may NOT arm |

and on `0x29`'s option page the state-7 value is `00`, which is what lets `C0/2024` arm the cursor
one frame later (f140 -> f141).

The port wrote the byte at exactly ONE edge, the reveal's end. So **a box inherited the previous
box's value for its whole ramp**, and an inherited `00` reads as "the string is finished". Any path
that reaches state 7 without passing through our typewriter -- a `--loadwram` seed, an F5/F9 restore
taken mid-box, a re-entry -- therefore armed a MULTI-PAGE decision's cursor on **page 0**: the first
press confirmed, the prompt page was never shown, and the script branched on a line the player never
saw. That is the reported symptom exactly.

Fixed in the ROM's own place, `C0/1FCD`'s window creation (`tbMachineTick`'s `st == 5` arm), plus a
recompute in the host whenever it lays out a box that is ALREADY in state 7. Ours stays coarser than
the ROM between the seed and the reveal's end (`04` throughout, where the ROM cycles `10`/`06`) and
that is deliberate: state 7's only read is `C0/2027`'s `BNE`, so any non-zero is equivalent there,
and claiming the ROM's exact per-token value would mean porting the C2 text engine (phase A of
`port_completion_plan.md`). `CT_TBOPDBG` prints `$0215` with the rest of the cursor machine.
Second mutation on `field-decision-cursor-machine-0162`, KILLED.

### THE FLASH WAS IN THE DRAW, NOT THE VM (2026-09-06, third report of the same symptom)

*"on selecting Yes, the message flashes briefly: [the first page of Marle's question about the Festival]"* -- page 0 of the box the player had just answered, appearing AFTER the
confirm.

It survived three rounds of VM work because **the VM was right**. The user's own `CT_TBOPDBG` log
settled that: both of scene 439's decisions took two presses, `$0166` came out 01 then 02, `$0162`
walked 1 -> 2 -> 3 -> 0 in order, and the typewriter spans matched the oracle to the frame (72 for
str 0x29's page 0, 100 for str 0x2B's). Every probe in the project reads the VM, so every probe said
"correct".

The defect is in `field.c`'s dialogue overlay:

    if (tbActive && isChoice) {          ...draw the PAGE model (tbPage)...
    } else if (tbActive || tbClose) {    drawCtWindow(...);
                                         if (tbAnim >= 40) drawLines(... tbLine ...);

`isChoice` is forced to 0 whenever `tbActive` is 0, and **`tbActive` drops on the first frame of the
close ramp**. So a dismissed decision box fell into the second arm, whose drawer works on the LINE
model and starts at `tbLine` -- which a choice box never advances, because it pages with `tbPage`.
With `tbReveal` still full, that paints the string's **first page**, fully revealed, in a still
full-size window. Measured at f502 on `saves/field_re/s439_marle_choice.lakewram` (the confirm lands
at f500; f501 still paints page 1 correctly).

The gate `tbAnim >= 40` was believed to cover it -- its own comment read *"drawLines below is gated
tbAnim>=40, so no stale tbStr"* -- and cannot: the close ramp **starts** at `$0127 = 0x28 = 40` and
only decrements afterwards, so the gate passes on exactly the frames the ramp begins. A guard that is
off by the first value of the counter it guards.

Fix: latch `tbWasChoice` while the box is active, and let a closing decision box keep its own page.
The ROM keeps the page it was showing and lets the shrinking window clip it (measured: `--peek` on
`s439_marle_choice_lake.state`, `--btnscript "0:100,100:4,0:96,100:4"` -- mid-ramp its box still
reads "May I have it back? / Yes. / No." with the top rows cut off), so the text is kept through the
ramp too rather than blanked. Check `field-decision-close-ramp-keeps-its-page` pins the composed
frame -- the only place this was ever visible -- mutation KILLED.

### ...and the band clips its TEXT too (closed the same day)

The follow-up read as "the window does not shrink", and the measurement said otherwise: `$0127` ramps
in our engine exactly as it does in the ROM -- 28, 24, 20, 1C ... on the close and 00, 04 ... 28 on
the open, side by side -- and `drawCtWindow` already honoured `tbAnimBand`, the half-height of the
visible band. What was missing is that **`drawChoiceBox` never set the glyph clip rows**. So the
frame collapsed while all four lines kept painting at full height: text hanging in the air above and
below a box that was no longer there, which is what "does not shrink" actually looked like. The
plain-text drawer has always set the same two fields (`drawTextLines`); only the choice drawer was
missing them.

A/B at the SAME box and the SAME ramp value (`$0127 = 0x0C`; ours f508, the oracle's f508 on
`saves/field_re/s439_marle_choice_lake.state` with `--btnscript
"0:100,100:4,0:96,100:4,0:96,100:4,0:96,100:4,0:96,100:4,0:4"`): the ROM shows only
`"with you for a while? / Sure."` -- the band's own rows, the top line's upper pixels shaved -- and
ours now shows those same two lines instead of all four.

Two details the fix needed: the pointing-hand cursor is drawn with `plot()`, which writes straight to
the framebuffer and does not consult the clip rows, so it needs the band explicitly; and the clip is
released on the way out, as `drawTextLines` does. Check `field-decision-window-band-clips-text`,
mutation KILLED -- the sibling of `field-decision-close-ramp-keeps-its-page`: that one pins WHICH
page a closing decision box draws, this one pins HOW MUCH of it.

⭐ THE LESSON, for the class: three fixes went into the VM for this report before anyone rendered the
frame. `CT_TBOPDBG`, `CT_PROJECT=box` and `--vmdiff` all read the event VM or the box machine's
bytes; **none of them can see what the host paints**. When a report is about something the player
SEES and the VM measures clean, render the frames and look at them -- `--selftest <shot>` + `fb2png`
would have found this in ten minutes at any point.


---

# `C0/2126` TRANSCRIBED IN FULL, AND WHAT ACTUALLY BLOCKS `$0131` (2026-09-11)

Phase A of `port_completion_plan.md` names `$0131` — the ROM's own LINE cursor — as the thing whose
absence means "no check can compare the LAYOUT itself, only how long it takes", and lists the phase
as **blocked on nothing**. The routine is transcribed here so the next pass starts from the bytes,
and the blocker is named, because it is not what the plan assumed.

## 1. The machine — 190 bytes, and it is TWO dispatches around one `JSL`

Read off the raw bytes (`AD 15 02 D0 03 64 36 60 …`), DP = `$0100`:

```
2126  LDA $0215 / BNE ->212E              the control code the decoder last stopped on
212B  STZ $36 / RTS                       code 0 -> nothing to do
      ---- FIRST dispatch, on the code already standing ----
212E  09/0A/0B/0C -> 2161                 a PAGE break
213E  05/06/07/08 -> 215B                 a LINE break
214E  03          -> 2154                 the in-string PAUSE
2152  anything else -> 216D
2154  LDA $0216 / BEQ ->212B / BRA ->216D operand 0 = inert
215B  LDA $31 / CMP #03 / BNE ->2165
2161  LDA #00 / BRA ->2166                page break, or the 4th line: $31 := 0
2165  INC A                               else $31 := $31 + 1
2166  STA $31 / STZ $32 / JSR C0/20B2     ...and RE-DECODE the page
      ---- the decode ----
216D  LDA #01 / STA $0213
2172  JSL $C2:0009                        the text engine proper; it leaves a NEW $0215
      ---- SECOND dispatch, on the code the decode just produced ----
2176  LDA $0215 / BEQ ->21D3
217B  10          -> 21A3
217F  09/0A       -> 21D8
2187  0B/0C       -> 21D3
218F  05/06       -> 21CB
2197  07/08       -> 21C2
219F  03          -> 21A8
21A3  LDA #01 / STA $36 / RTS             keep typing
21A8  LDA $0216 / BEQ ->21D8              the PAUSE: $34 := $0216 * 15 on the hardware multiplier
      ($4202 := $0216, $4203 := #0F, $34 := $4216), $29 := 7, then ->21A3
21C2  LDX #FFFF / STX $34 / LDA #07 / STA $29      ...and FALL INTO 21CB
21CB  LDA $31 / CMP #03 / BNE ->21A3      not the 4th line -> keep typing
21D1  BRA ->21D8
21D3  LDX #FFFF / STX $34
21D8  LDA #07 / STA $29 / LDA #81 / STA $36 / RTS  the page is DONE: wait for the press
```

⭐ **`$0131` IS THE PAGE-VS-LINE DECISION, and it is a counter, not a lookahead.** A line break makes
the box wait for a press only when `$31` has already reached 3 (`21CB`); otherwise the typewriter just
continues on the next line. The engine never asks "is there more text" — it counts lines to four.

⭐ **`$31` is written in the FIRST dispatch and read in the SECOND**, with `JSL $C2:0009` in between,
so within one frame the counter advances on the code that was standing and the page/line verdict is
taken on the code the decode just produced. Collapsing the two into one test would be wrong on
exactly the frame a page ends.

`$34` (= `$0134`) and `$29` (= `$0129`) are bytes this port already models — `21D8`'s
`$29 := 7 / $36 := $81` and `21C2`/`21D3`'s `$34 := FFFF` are literally what
`CtSceneTextboxRevealDone` hand-writes today. So the port already carries this routine's *effects*;
what it does not carry is the routine.

## 2. ⚠ The blocker is NOT "nobody ported 2126" — it is the port's text model

`src/field.c` ends the reveal with

```c
CtSceneTextboxRevealDone(f.vm, tbMore ? (isChoice ? 0x0B : 0x05) : 0x00);
```

— the host DECIDES a control code from its own `tbMore` / `isChoice` state. That is a heuristic
standing in for a ROM byte, of exactly the kind `[[no-invention-rule]]` names, and it is why `$0131`
cannot simply be added: the port's string model has already **normalised the ROM's control codes
away**. The engine's text is carried as `'\n'` (a line break), `'\f'` (a page break) and `TB_IND`
(the break token's own three-space indent), so:

* `05` vs `07` and `06` vs `08` are not recoverable from it — and they are **not
  interchangeable**, because the second dispatch sends `05/06` to `21CB` and `07/08` to `21C2`,
  which first latches `$34 := FFFF` and `$29 := 7`;
* `09/0A` vs `0B/0C` likewise split at `217F`/`2187`.

So Phase A's real first step is **not** `$0131`. It is making the text model carry the ROM's own
control byte at each break, at the point the asset is decoded. `$0131` then falls out of `215B-2166`
in four lines, and the host's synthesised `$0215` deletes itself.

**MEASURED 2026-09-28 -- the split this section fears never happens in the game's text.** Counted over
every dialogue string the packer finds (text.scan_dialogue_tables, 5099 strings, the raw token streams):
`05` x1717, `06` x3984, `0A` x2 (Gaspar's two), `0B` x428, `0C` x497 -- **`07`, `08` and `09` occur
NOWHERE**. And every reader of `$0215` treats each class the text model keeps as one: C0/2024 tests zero
(`LDA $0215 / BNE`), C0/2058 (state 7's advance) sends `09-0C` to state 8 and `05-08` to 6 or 8 by `$31`,
C0/2126's first dispatch splits page (`09-0C`) from line (`05-08`), and its second splits exactly
`{05,06}` / `{07,08}` / `{09,0A}` / `{0B,0C}` -- the packer's `'\n'` / (none) / `TB_AUTOPG` / `'\f'`.
So the host's `05` / `0A` / `0B` / `00` is a class representative and the BEHAVIOUR is the ROM's on every
shipped string; what differs is `$0215`'s VALUE (the cartridge holds `06` where the port writes `05`,
`0C` for `0B`), visible to a WRAM lockstep and to nothing the port runs (C2/0009's own reads of
`$0215` -- C2/56A2, F92C, FAB9 -- are inside the text engine the port does not run). Carrying the byte is
still the right end state; it is no longer a behaviour defect.

## 3. What that unblocks, and the order

1. carry the real control code through the decode (the invention above dies here);
2. `$0131` / `$0132` from `215B-2168`, and `$0136` from the second dispatch;
3. `JSR C0/20B2`'s page re-decode, which `$31`'s write is paired with;
4. only then does `CT_FIELDLOCK` + `CT_PROJECT=box` gain a `$0131` projection — and the LAYOUT
   becomes gradable for the first time, which is what Phase A was after.

`$C2:0009` itself (the decoder the `JSL` calls) is a separate subsystem and is **not** required for
steps 1-4: the port already produces the glyph stream, and what is missing is only which control byte
each break was.

## 4. ⭐ PORTED (2026-09-24): four lines, the line cursor $0131, the line buffer and the NMI's upload

User queue item "port the textbox's 4-line window ($0131)". Everything below is ROM + oracle; checks
`field-textbox-window-vs-oracle` (21 moments at 0 px), `lockstep-textbox-four-lines-tent-keeper`,
`lockstep-decision-box-s434-clean`, `field-textbox-typewriter-vs-oracle` (all CLEAN / 0 px, mutated).

**The text model carries every break the dialogue uses.** Across all 5244 pages of every dialogue table: `05` x1506,
`06` x3543, `0B` x370, `0C` x466, `0A` x2 -- and NO `07`, `08`, `09`. `05`/`06` and `0B`/`0C` differ only by the
token's indent, which `TB_IND` already carries; the two `0A`s (Gaspar's "blah, blah, blah", the End of Time) are the
page break that turns ITSELF (`2181` -> `21D8` without `21D3`'s `$34 := FFFF`, so state 7 finds `$0134 == 0` and `20A5`
advances at once) and are packed as their own marker (`'\v'`, field.c `TB_AUTOPG`). So section 2's blocker is gone
for the ROM's actual strings; the host's `$0215` is the code the window really ended on.

**Four lines a page; a page CLEARS.** 338 pages have four lines, none five. The port held three and SCROLLED the
fourth in after a press (an invention). Measured on the tent keeper (`saves/field_re/tent_keeper_lake.state`, A at
f10, `--monitor 7E0129,7E0131`): the ramp walks `$0131` 0,1,2,3 (`1FB4` then three `1FC0`s), the window's creation
zeroes it (`1FEE`, f16), the breaks step it to 1, 2, 3 at f47 / f66 / f99, state 7 at f127 -- four lines, no press.

**The typewriter renders on EVERY state-6 iteration** (correcting this note's "setup iteration" reading above):
`--monitor` of the string pointer `$0231` and `$0215` shows the tent keeper's first character typed on the open's
frame (f26, `$0215 = 10`, the pointer moves) and s439's page 1 typing its `M` on the ramp's C frame (f106, `1FC5` ->
`JSR C0/2126`). A break costs its OWN iteration (the render returns the code, `$0215 = 05/06` at f46 / f125, nothing
drawn) and the NEXT iteration's head (`215B`) steps `$0131` and clears the line buffer before it types (f47 / f126).
The end of a window costs one more: the render meets the code that ends it and `21C2-21D8` leave for state 7 (the tent
keeper's last character at f126, state 7 at f127). The old "setup iteration + step on the break" model gave the same
`$0129`/`$0131` frames but typed every character one frame late -- invisible to the machine lockstep, visible only
in the picture with the press ALIGNED (the standalone walkscript's one-frame-early press hid it).

**`C0/20B2` clears the LINE BUFFER** (`$7E:F000`, MVN from `$D2` then doubling copies) -- it is not a page
re-decode. **The screen is fed by the field NMI**: `C0/EB79 LDA $36 / BEQ` skip, `BPL` -> `JSR C0/6ECB`, bit 7 ->
`EB7F STZ $36` first; `C0/6ECB` DMAs `$400` bytes of `$7E:F000` to VRAM `$5800 + $31 * $200` on channel 7
(`C0/2DC8`: `$48` dest, `$4A` mode, `$4B` source, `$4D` bank, `$4E` size). `$0136`'s writers: `1FBB` = 1, `1FF2` = 0,
`21A3` = 1, `21DC` = `$81` (one last upload). So the window is FOUR VRAM LINE SLOTS (field.c `drawTbSlots`): a
character typed in frame f is on screen from f+1, and a page's old lines leave ONE PER FRAME while the ramp walks
`$31` 0..3 with a cleared buffer -- measured at 0 px on s439's page turn (f102..f106).

**The text row is window top + 10** (every line, both box positions; the port drew at + 9). **The closing quote is
its own glyph** (`0xE2`; the packer folded it into `0xE1`).

**A pressed page's `1FB4` is the NEXT frame's**: `2086 LDA #08 / STA $29 / RTL` ends the machine's frame, exactly as
`2081`'s `0x0D` does for the close. The host's press used to run `1FB4` in the same frame (ours `$0129 = 09 / $0131 =
00` against the oracle's `08 / 02` on s439's press frame) -- the "page ramp one state ahead" residual that
`field-textbox-typewriter-vs-oracle` had pinned as an input-phase effect. `CtSceneTextboxPage` arms `tbPageArmed` now.

**OPEN (not derived):** the decision box's pointing hand is on screen TWO frames after `$0162 = 2` on the oracle
(s439: `$0162 = 02` at f141, the hand from f143); the port draws it from the arming frame. The routine that puts it
on screen is not the line upload (`$0136` stays 0) and not `C0/F05E` (never runs on the field; its caller `C0/ED15`
neither). Also open: the window's open-ramp picture (`$0127` is identical per frame, the band's pixels are not) and
its left-edge weave (x 5-7).

**The band is this iteration's `$0127` (2026-09-26).** The window's HDMA tables are built by C0/00C7's `JSL FD/C2C1`
-- FD/CFCF or FD/D27E by `$0153`'s table set, both `LDA $0127` -- AFTER C0/00A4's box machine stepped the ramp in the
same main-loop iteration. `--cputrace` on the logical oracle, saves/field_re/loc002_intro_curtain.state: C0/1FFF
`INC $27` (00 -> 04) at f57, then 00C7 -> FD/D27E, then the NMI; the band is on the cartridge's thumbnail 59 = P+2, as
every main-loop product is (memory ct-frame-alignment-p2). field.c read `tbAnim = $0127` BEFORE CtSceneTick ran the
machine, so each ramp was drawn one frame late -- a hidden delay of the kind that memory lists. It re-reads the byte
at the draw now: the intro's box band is on P57 like the cartridge's. What the ramp still does NOT match there is its
bottom edge row (one thumbnail row, 128 px, on each of P58-P65 -- the band's extent is equal) and, once the text is up,
the left-edge weave above: the two open items of this paragraph's first sentence, now separated from the timing.

## The window FROM THE ROM, in the saved colour (PORTED 2026-09-24)

The port's window used to be CT_ASSET_WINDOW, a colour-0 lift of the oracle fb, whatever the Options setting. It is now
built by the location load's own legs (field.c `fieldWindowFromRom`, from `fieldPalettePages` = C0/70E9 and from the
`--loadwram` seed):
* **C0/6E5C** -- tiles FF/(C0:6EA0[$2991 & 7]) = FF/(9E10 + v * 0x280): 0x200 bytes to VRAM $1F00 (tiles 0x0F0-0x0FF of
  the $1000 char base), 0x80 to $4FC0 (tiles 0x3FC-0x3FF). Plane 3 as stored -- zero in all eight sets, so colours 0-7.
  (The MENU's copy of the same tiles, C2/C511, ORs plane 3 = p0|p1|p2 and uses colours 8-15.)
* **C0/70E9** -- FF/B210 + v * 16 + 2 -> CGRAM colours 1-15 of row 0 (9-11 then forced to 0C63/18C6/739C).
* **C0/29F7** -- the ten map rows (through the WRAM port into $7E:F000, uploaded to $1D00 by C0/6EB0): F0 (F1 F2)x15 F3;
  twice [F4 (FC FD FC' FD')x7 FC FD F5 / F6 (FE FF FE' FF')x7 FE FF F7 / F4 (FC' FD' FC FD)x7 FC' FD' F5 /
  F6 (FE' FF' FE FF)x7 FE' FF' F7] with ' = attribute 23 (tile bits 8-9 = 3: the $4FC0 tiles); F8 (F9 FA)x15 FB.
  MEASURED: the oracle's $1D00..$1E3F is this word for word.
* the band's ramp: +8..+1 over its first 40 lines, 0..-7 over the next 39, five lines a step, none on the last.

Colour 0 reproduces the lift on 20439 of 20480 pixels; the 41 others (x 5-7 of the first weave repeat) were the LIFT's
error -- the cartridge's pixels there decode to the ROM tiles' indices (tent keeper and Marle drives, now compared full
width by tools/textbox_window_cmp.py; the old x 8-247 window had hidden them). Graded end to end by
`menu-window-colour-field-textbox-vs-oracle`: colour 3 set in Config, the menu closed (the reload rebuilds the window),
the tent keeper's box frame 0 of 5600 px against the cartridge.
**Open:** drawCtMsgWindow24 (the battle message band) and drawCtWindowRect (other rects) still derive from this picture
with their own de-ramp / resampling; the battle's own window load is not traced.

## The gradient is the PPU's colour math, line by line (PORTED 2026-10-11)

The port's box was the window picture above with the full-open ramp baked into each row. On the cartridge the window is
BG1 and the ramp is the colour math HDMA channel 6 writes per line, so at a ramp's ends the two disagreed on one row.

* **FD/C124's tail** `STZ $27 / JSR FD/D52D` (D = $0100, B = $7F; MEASURED by `--loadcensus`: `$0127 = 00 by FD/C1E9`,
  `$01E1 = E8 E7 .. by FD/D55E`, `$7F:1539 = E8 by FD/D567`) builds two-byte records CGADSUB, COLDATA at $7F:1538:
  $28 / 8 through the divider = five lines a step; ADD (`01`, BG1) at E8..E1, then SUBTRACT (`81`) at E0..E9, five lines
  each (90 records); then `MVN` copies $1538..$1637 onto $1600..$16FF. Ported as field.c `fieldWindowGradientD52D`;
  byte-exact against the cartridge's $7F:1538..$16FF in both lake seeds (tent keeper, Naga-ette).
* **Channel 6** (FD/C1EE: DMAP 41 -> $2131/$2132, bank $7F, table $7F:118A or $7F:1442 by `$0153 & 0F`) points the box's
  lines at those records -- at the FULL-OPEN line's record also while the band ramps (the band only clips) -- and the
  band's LAST line at the map's record (CGADSUB `$0BE0`, no BG1 bit), while channel 4 still gives it TM 01. MEASURED
  (`--hdma` + HDMA_TRACE_BG, m81_naga_after_fight_lake.state): f58 full open, lines 132-210 at 8..1 / 0..7, line 211 math
  24; f57 mid-open, lines 136-206 at the full-open values, line 207 math 24 -- the window's RAW colour there.
* The fixed colour a line starts from is the NMI's (C0/EC3D-EC45: COLDATA `E0`, then `$0121`); CGWSEL is `$0BDE`
  (C0/EC37), WOBJSEL `$0BDB` (C0/EC25).

The port walks channel 6 as it walks channel 4 (field.c `fieldHdmaWalk` / `fieldHdmaMathRows`) and draws the box from the
window's BG1 indices (`g_winIdx`, the same C0/6E5C / C0/29F7 build) in the uploaded CGRAM with ppu_handlePixel's rule
(`drawCtWindowPpu`): a BG1 pixel takes the math when CGADSUB bit 0 is set, the backdrop when bit 5 is; the operand is
the fixed colour (TS 00 on the box's lines), subtracted on bit 7, no half when CGWSEL adds the sub screen. A colour
window with a clip / prevent mode, or a sub screen the math would add, is not modelled: the bitmap is kept and the case
reported once (`tbwindow:`). Graded: tools/textbox_rule_cmp.py 0 frames differ on both drives (was 2 and 8), also at a
0 threshold; on Marle's decision close (s439) each ramp frame changes exactly the band's last row, now equal to the
cartridge's (the bitmap was 256 px off). The menus' 9-patch (drawCtWindowRect) and the battle's message band
(drawCtMsgWindow24) still read the baked picture.
