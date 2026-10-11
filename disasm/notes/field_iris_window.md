# The field's iris (the gate's circle) -- C0/21E1 entries 1/2, C3/0077, window 1

User request 2026-09-29: "Has the time flow (the post-portal animation) after scene 8 been ported yet? If not, port it". (translated from Portuguese)
The time tunnel itself (op 0xFF 80, FD/DA00) was already a translated program, 880/880 frames against the cartridge
(tools/gwarp_cmp.py). What followed it -- the gate opening in the 600 A.D. forest (loc 071), Crono falling out, the gate
closing -- ran on a model: a sqrt circle of radius 31 round a centre taken from the swirl's emitter, filled with a capture
of scene 8's BG3 scrolled by the host's frame counter. This is the ROM's mechanism.

## 1. The ops and the machine

Field DP is $0100, so the disassembly's $39..$3D are $0139..$013D.

* `FF 90 cx cy r` = C0/4036: `$3A = cx`, `$3B = cy`, `$3D = r` (the target), `STZ $3C`, `$39 = 1`.
* `FF 91` = C0/4055: `$39 = 2`.
* C0/00B1 `JSR C0/21E1`, at the HEAD of every main-loop iteration (before the object pass): `LDA $39 / BEQ / DEC / ASL /
  TAX / JSR ($21EE,X)` -- table 220E (open), 2269 (close), 22D8 (the window wipe, already ported), 232A (not ported).
* C0/220E: `LDA $3C / CMP $3D / BCS ->2263`; C0/2214 arms the shadows -- `$7F1522/1523 = 17` (TMW/TSW), `$0BD9 = 22`
  (W12SEL: window 1 on BG1 and BG2), `$0BDA = $0BDB = 00`, `$0350/$0352/$0354 = $3A/$3B/$3C`, `TSB $28 #20` (HDMA channel 5
  on), `$0356 = $0153 ? 17C8 : 1AC8`, `$0358 = 7F` -- then `LDA #00 / JSL C3/0008` and `2260 INC $3C`. At the target,
  2263 `STZ $39 / DEC $3C / BRA ->2214`: one last build at target-1 and the INC back.
* C0/2269: `LDA $3C / BEQ ->22BC`, `DEC $3C`, the same arming and build (no INC). 22BC: `STZ $39`, the shadows back to 0,
  `TRB $28 #20`.

MEASURED (the Telepod warp's far end, `--monitor 7E0139:5 --writer`, the rule): open `01 87 67 00 20` at c965 (C0:4053),
$3C 01..20 over c966-c997 (C0:2262), $39 = 0 at c998; close `02` at c1117 (C0:405C), $3C 1F..00 over c1118-c1149 (C0:226F),
$39 = 0 at c1150 (C0:22BE-ish). The port's run: open f3421, full f3454, close f3573, off f3606 -- the same 33/33 frames.

## 2. C3/0077, the circle

DP = $0300. $0350 centre X, $0352 centre Y, $0354 radius (their high bytes zeroed first), $0356/$0358 the table (bank
$7F if $0358 bit 0). A midpoint circle writes each line's (left, right) word for the top half, copies it mirrored to the
bottom half, and fills the lines above and below with the empty pair (01, 00) through an overlapping MVN; radius 0 fills
the whole $1C1-byte table empty. Several ADC/SBC run on whatever carry the instruction before left. Two of them shape the
table: 00B0's SBC borrows (00AD's ASL leaves the carry clear), so the decision variable starts at 2 - 2r, not 3 - 2r; and
00DD's ADC adds the carry of 00D3's `cx - Y` (set whenever cx >= Y), so the top rows' right edge is one pixel wider than
the left is narrow (the oracle's line 72 at radius 31 round x 135 is 130..141). Two are never observable: 00D3's (00CC's
ASL of `$F6 - X`, and $F6 = cy + base/2 is far above any radius) and 0134's (set only when `Y - X` is -1 or -2, where the
extra 1 cannot flip $F2's sign) -- removing either keeps a sweep over all 256 radii (43008 calls) byte-exact. The first
iteration also writes a width-0 line one row above the circle that the top fill then clears. Ported one instruction at a
time with the carry (ct_vm.c ctIrisCircle), graded by `ct.exe --sweepiris`: the real routine micro-executed over 7 centres
X (0..FF), 8 centres Y (0..DF), 12 radii (0..40) and three tables (both banks) from a poisoned image -- 2016/2016
byte-exact (check sweepiris-c3-0077-byte-exact; its mutations take 00B0, 00DD and 01B0's row-1 skip).

## 3. What is on the screen

HDMA channel 5 (`$7F:1133 = E4 <buf> F0 <buf+200> 00`: 212 lines, then held) hands WH0/WH1 line by line; output row y is
table entry y. The frame shows the table the same iteration wrote: `--hdma` from states at c979..c983, the traced frame's
window top = the newest buffer's (the one $0356 names) at the end of that frame -- 91, 90, 89, 88, 87. (A "the window is
one iteration ahead" reading came from mixing the port's CT_SHOTS numbering with its thumbnails', which run one higher;
full-resolution frames settle it: the objects AND the window line up at port = peek + 2454.)

W12SEL $22 masks BG1 and BG2 with window 1 on the main and sub screens (TMW = TSW = $17): inside it the pixel is what BG3,
the OBJs and the backdrop make. `PEEK_ISOBG` at c1000: BG3 is the loc's own layer (a white cloud texture, palette 7) at
its own scroll (36,18), unchanged in VRAM since before the open; the OBJ layer is the swirl disc (obj9/obj10, sprites
95/94, priority 0) and Crono. The rim the gate shows is BG3 where the window reaches past the swirl.

## 4. Two things the port got wrong on the way

* BG3's palette 7 (colours 29-31: 7FFF 7F50 74AA) is not loc 071's palette record (6489 x3): obj0 fn1 writes it with
  `4E 3A 20 7E 08 00 FF 7F 50 7F AA 74` (op 0x4E, C0/6846 MVN) into the base page $7E:2000, and it reaches the live page
  $7E:2200 only through C0/70E9's copy -- which is C0/286C's (2887 JSR), AFTER the object init. The port did C0/7084 and
  C0/70E9 together at the load. MEASURED (`--monitor 7E203A:6 --writer`): C0:70A9 writes 6489 at f907, C0:689A the three
  colours at f922, C0:712A copies at f927. Now field.c fieldPaletteRows (C0/7084, the C0/00F4 leg) and fieldPaletteText
  (C0/70E9, at each C0/286C site: load, transition, the menu's reload).
* The window wipe's mask (field.c fieldWipeWindow) keyed on TMW alone, which the iris sets too; it read the iris's word
  table as its byte-per-line table and blacked column 0 on every other row. It now requires window 2 ($0BD9 & $88,
  C0/22D8's $88), which the iris never enables.

## 5. The lead after the fall

obj2 (the lead, $0197 = 04) ends its fall with `96 08 0B / A6 00` (C0/5545: facing 0, `STZ $1601`). Its script then parks
on its fn0's `B0`, and op 0xB0 (C0/3002) with `$1100,X = 0` and `$0138 = 0` runs `STA $1A01,X / JSR C0/9E29`, whose head
is `JSR C0/9E84` -- the facing/anim arm -- locked or not. C0/881E zeroes the intent ($1D2C/$1D2D, 882F/8831) every frame
and skips the d-pad (C0/88EE) while $011F = 0, so 9E84's index is 0 -> slot 0 -> the idle chain C0/A1E9 -> `LDA $1F /
BEQ ->A211`: $1680 back to 0. MEASURED: $1684 24 -> 00 at c1113 by C0:A21F with $011F = 00. The port ran 9E84 only in free
control and kept cycling anim 36; ct_vm.c op 0xB0 now runs it for the parked lead while the pad is locked.

## 6. Result and what is left

`python tools/gate_arrival_cmp.py` (check `field-gate-arrival-vs-rule-cartridge`): from the cartridge's first lit
loc-071 frame, 300 frames, 0 differ. (At this dossier's first commit 34 differed by at most 5 px, all during the open, in
Crono's face: the lead was drawn from the host's walk/idle ticker, pPlay. Since 2026-09-29 it is drawn from $0F01,X,
the drain's commit -- C0:CC17 writes the lead's $0F05 = 02/00/03/01 every five frames through the fall exactly as an
NPC's -- and the free-roam C0/9E84 keeps the lead's mode on the VM's own bytes on op 0xB0's visits only: sec. 7.)
The arrival lines up one frame before the warp (+2454 against +2455), and under the project's one alignment that is
one frame LATE (the field's logic P is the cartridge's thumbnail P+2, the stage's frames are the cartridge's own):
the iris's $0139 = 1 comes at the port's label 1065 against 1064 from the same WRAM -- the loc's load is one frame
long (the field load clock). An earlier version of this line said "a frame short"; it read the relation backwards.
The C0/9E29 tail (the trail push on a nonzero $1D32/$1D33, `STA $1A01,X`) is not part of the locked arm.

## 7. The lead is drawn from $0F01,X (2026-09-29)

The ROM draws the lead like every object: the anim drain (C0/C98A steps $1681,X/$1601,X; C0/CA76 -> CAD9 -> CBDC/CC17
commits $0F01,X) and the OAM builder reads $0F01,X. The port drew NPCs from it (field.c, "RENDER FROM $0F01,X") but the lead
from pPlay, its own walk/idle ticker, and kept the lead's $1680,X in the VM only as scripts left it. Measured differences:
the arrival above (34 frames), the tent's decision (scene 434: at $0162 = 2 C0/A1E9 -> A1D2 puts $1682 = 19 and the
drain commits $0F03 = A5 -- the port drew the idle frame), and the first locked frame at the curtain (loc 002: the seeded
$1682 = 0B had stayed through a whole free walk). Now: the lead is drawn from $0F01,X; the host's free-roam C0/9E84
(CtPlayerAnimSelect after C0/881E) runs on the lead's own $1680/$1681/$1601 -- seeded into its page and answered back --
on a frame whose visit ran op 0xB0 (ct_vm.c CtSceneActorAtB0; C0/3002 -> 3040 -> 9E29 is the routine's only caller);
locked, op 0xB0 runs it in the VM (sec. 5). The two debug entries (--reentertest, CT_CHANGELOC) stage their facing in
$0104 as C0/0CBC does, since the lead's facing is now the one the loader's C0/42B8 read from it. pPlay still runs for
its `done` (the lead's anime-once completion).


## 8. The iris has no sound of its own (user question 2026-09-29, and defect #1 of 2026-09-26)

"when the portal (iris) opens the game makes no sound". MEASURED on the rule's cartridge (saves/field_re/scene8_gate_cutscene.state,
the cutscene's own A mash `0:15,100:3,0:27,...`, `PEEK_WRITEWATCH=1E00:4` + `--monitor 7E0139:1`): the scene posts six
SFX, all `18 <id> 80` by op 0xE8 (C0/3C1C-3C26): 52 at c833, 86 at c853, 6C at c1562, 78 at c1818, 88 at c1994, 51 at
c2162. `FF 90` (C0/4053, $0139 = 1) is at c1745 and posts nothing -- no mailbox write in c1563..c1817. The sound the
portal has is 6C, posted 183 frames BEFORE the circle, which keys voices 6/7 (srcn 02/05) and re-keys voice 6 every 120
frames (c1682, c1802: `CT_DSP_KONTRACE`), so it is sounding, never silent (voices 6/7 alone: no frame with an RMS under 20
from its key-on to +255), when the circle opens; 78 (srcn 07/05) follows 73 frames after the open, and 51 four frames
before the close (`FF 91` at c2165).

The port, seeded from the cartridge's WRAM at c1500 and run INTERACTIVELY (paced, the SDL output path, no --selftest):
6C at +61 (c1561's monitor label), the open at +245 (c1745), 78 at +317, 88 at +493 -- the cartridge's frames; its DSP
keys 6C on voices 6/7 with srcn 02/05, re-keys voice 6 at +120 and +240, and 78 with 07/05. Under --selftest with
CT_AUDIO_LOCKSTEP the same, and voices 6/7 alone sound on every frame from the key-on to +255 on both sides (RMS 996-2496 at the
sampled offsets, within a quarter of each other). So the silent open is the ROM's: the circle's op writes $0139-$013D and nothing else (C0/4036).
