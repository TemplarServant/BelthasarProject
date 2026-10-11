# The overworld player sprite: the LEAD block's own anim sub-VM (C2/3404 -> C2/1CDA -> C2/0E1D)

Rewritten 2026-09-05. The 2026-09-03 version of this dossier (commit 99ac511, defect #21) took the coastal
BOAT object -- block $0F30, class $0F63, attr $B4, its sub-VM bobbing C3:F0F2/F0F6 (tiles 020/022/040/042
and 02C/02E/04C/04E) every 12 frames whether or not the player moves -- for "the player's sprite object",
captured the boat's OAM pieces as the player atlas and drew them 40px right / 20px below the anchor. The
user's report ("crono's sprite on the overworld is a boat, and in the wrong position") was exactly that.
What follows is derived from the ROM and measured on `saves/ow_w0.state` (world 0, lead block $0D70) by a
two-agent derive/refute workflow whose corrections are folded in.

## 1. The lead block carries its own pose

The lead is the 0x40-byte object block whose handler word is **$3404** ($0D70 on world 0, $0DF0 on world 1;
find it by the handler word -- `$4E` is only the object-pass cursor, frame-end values $10B0/$7010/$1170).
Like every ambient object it has an anim sub-VM pointer at +0B/+0C (script) / +0D (bank $C3) with a dwell at
+0E, installed by **C2/1CDA**:

    REP #20 / AND #00FF / ASL A / TAX / LDA $C3E000,X / LDX $4E / STA $000B,X
    SEP #20 / LDA #C3 / STA $000D,X / STZ $000E,X / RTS

Other fields the machine uses: +02 phase (1 = at an 8px boundary, reads input; 2 = stepping), +0F attr
(**$A8** = OBJ palette 4, priority 2, bit 7 world-space; $B8 when `$0100 == $01F5`), +10 tile base | size
(0 -> tiles from name 0, 16x16), +14/+18 world X/Y, +1C/+20 velocity (+-1 px/frame), +26 the walk-leg
parity, +28 = facing (0 Up, 1 Down, 2 Left, 3 Right) | $80 while walking, +2A/+2C the step target.

## 2. The five install sites (interactive lead path, `$0280 == 0`, `$027C == 1`)

| site | when | id |
|---|---|---|
| C2/3549 | phase 0 init (C2/34F8): +0F := $A8/$B8, +10 := 0, +26 := 0, +28 := `$0104 & 3` | STAND |
| C2/35D7 | phase 1, no button at all (`$00F8 == 0`, C2/35B3): +28 &= $7F, `INC $02B1`, `$02B1 < $F0` | STAND |
| C2/35CD | the frame `$02B1` reaches $F0 (240 no-input frames); then `$02B1 := $FE` (35D0), so the stand re-install stops and the script loops free | $A2 fidget |
| C2/3694 | a d-pad press at a boundary (priority Up > Down > Left > Right, C2/3593-35B0; a diagonal is its first component), unless `(+28 & $7F) == dir` AND +28 bit 7 (already walking that way, C2/362A-3630: the cycle continues across steps); +28 := $80 \| dir first (C2/368B) | WALK |
| C2/36DF | the same frame, the step BLOCKED by C2/39C3 on the target's corner props: +28 &= $7F, velocities 0 | STAND (of the new facing) |

    STAND id = C2:39AA[+28 & 3]           = 03 00 06 09          (Up Down Left Right)
    WALK  id = C2:397F[facing*2 + (+26&1)] = 04 05 01 02 07 08 0A 0B
    both + $0C when the lead character $7E2980 == 6 (Magus: ids $0C-$17, same shape, own descriptors)

`$02B1` is zeroed by every press (C2/3697 `STZ $02B1`), including a blocked one, which also re-installs the
stand id every held frame. A non-interactive module state (`$027C != 1`, C2/356A: fade, menu, ride) runs
only the tick. Phase 2 (C2/36ED) reads no input: integrate 1 px (C2/0F38), camera, **tick** (C2/3731), then
the arrival test -- a 1-frame tap still completes the whole 8px step.

## 3. The scripts and descriptors (bank $C3, table C3:E000)

    STAND  Down id 00 C3:E14C `04 FA E1 00`  -> E1FA: 1 piece (-8,-17) tile $102
           Up   id 03 C3:E16A `04 FF E1 00`  -> E1FF: (-8,-17) $104
           Left id 06 C3:E184 `04 04 E2 00`  -> E204: (-7,-17) $108
           Right id 09 C3:E19E `04 09 E2 00` -> E209: (-9,-17) $108 H-flip        (dwell 0 = held forever)
    WALK   Down id 01 C3:E150 `01 26 | 04 0E E2 08 | 04 FA E1 08 | 00 26 | 04 13 E2 08 | 04 FA E1 08 | 03 EC`
             E20E (-7,-18) $100 | E1FA | E213 (-8,-18) $100 H | E1FA     -- leg A, stand, leg B, stand, loop
           id 02 = C3:E15A, the same loop entered at the `00 26` half (leg B first)
           Up   04/05 C3:E16E/E178: E218 (-7,-18) $106 | E1FF | E21D (-8,-18) $106 H | E1FF
           Left 07/08 C3:E188/E192: E222 (-8,-18) $10A | E204 | E227 (-8,-18) $10C | E204
           Right 0A/0B C3:E1A2/E1AC: E22C (-8,-18) $10A H | E209 | E231 (-8,-18) $10C H | E209
    FIDGET id A2 C3:E1B8 `04 40 E2 08 / 04 3B E2 08 / 04 40 E2 08 / 04 3B E2 08 / 04 40 E2 18 / 03 EC`
             E240 (-8,-17) $160 | E23B (-8,-17) $10E      8/8/8/8/24 frames, period 56, loops until a press

The `01 26` / `00 26` ops (INC / ZERO the block's +26) around the legs are what make the NEXT walk start
on the other leg: measured, a Right walk that ended with +26 = 1 made the next Up press install id 05
(E178), and the port's walk-stop-walk drive restarts on E227.

The interpreter is the ordinary C2/0E1D (ported as world.c `obAnimTick`): op 04 loads the dwell on the
first visit and parks, DECs on the next ones, and on reaching 0 advances 4 with no enqueue so the next op
04 loads and enqueues in the same tick -- exactly 8 frames per `04 xx xx 08`; dwell 0 never advances.

## 4. Descriptor -> OAM (C2/0B53 enqueue, C2/0CEA drain, C2/0D08 decoder -- the ambient objects' path)

One 16x16 piece per descriptor: `sx = X - $22 + dX`, `sy = Y - $28 + dY` (BG2 scroll shadows; the port's
`sxp/syp`), name = word2 & $1FF in the HIGH OBJ name table (OBSEL $60: byte $2000 + (name & $FF) * 32,
cells n / n+1 / n+$10 / n+$11), pal/prio from `(attr << 8) & $3E00` ($A8 -> palette 4, priority 2), flips =
word2 bits 14/15. The shadow C2/0CEA builds in NMI N is DMA'd to OAM at the top of NMI N+1 (C2/0184, before
the object pass C2/051D), so the pose on the raster is **one frame** behind the block: on the press row
+0B flips to E18A and the OAM shadow already holds tile $10A; the PPU OAM shows it the next row. The PPU
also draws an OBJ one scanline below its OAM Y: the standing tile $102 (OAM `78 5F 02 29`, Y = $5F = 95)
sits 123/123 pixels exact at BG-relative row 96 in the oracle's framebuffer.

## 5. Measured timelines (`build/ct.exe --monitor "7E0D7B:2,7E0D7E:1,7E0D96:1,7E0D98:1,7E02B1:1" --from saves/ow_w0.state --btnscript ...`)

- idle `0:300`: +0B = E14C held, `$02B1` counts $A5..; f75 (= $F0): E1B8 dwell 8, `$02B1 = $FE`; E1BC f83,
  E1C0 f91, E1C4 f99, E1C8 f107 (dwell $18), E1B8 f131 -- the position never changes.
- Left `0:10,40:40,0:30`: f11 E18A (+26 -> 1, +28 = $82, X = $1B7), f19 E18E, f27 E194 (+26 -> 0), f35 E198,
  f43 E18A (+26 -> 1), f51 E184 (stand-Left, +28 = 2).
- Right into the coast `0:10,80:40,0:30`: f11 E1A4 dwell 8, X to $1C0 at f18, f19 E19E held with `$02B1`
  forced 0 every held frame (the blocked path).
- Up+Right = Up, Down+Left = Down (single axis). A 1- or 3-frame tap completes the 8px step.

## 6. What the port does now (world.c `CtWorldLeadAnimFrame`, field.c `worldCompose`, main.c `--owsprite`)

`CtWorldLeadAnimFrame` runs right after `CtWorldMoverFrame` with the same inputs: the init once (attr/+26/
+28/stand), the idle path (`$02B1`, stand or the one-shot $A2), the press (walk id from facing*2 + parity
unless already walking that way; `$02B1 := 0`; the stand again when the mover refused the step), the tick
on every path; `-2` (fade/menu/ride) ticks only. The compose draws piece 0 of the descriptor the block was
parked on ONE compose ago (`CtWorldAnimDescPiece`), from a tile-keyed atlas (`ct.exe --owsprite`: the 16x16
tiles the lead's own scripts name, decoded from the state's VRAM with the attr's palette; frames carry
fac = $FE, par/mov = the name) at `(sxp + dX, syp + dY + 1)` with the descriptor's flips, under BG1's
priority-1 pixels only (the PPU's mode-1 stack: OBJ3 / BG1p1 / OBJ2 / BG2p1 / OBJ1 / BG1p0 / OBJ0 / BG2p0;
the old overlay merged BG2's p1 too). The boat blocks $0F30/$0F70 stay boats. The baked idle-fidget poses
(src/data/ow_idle.h, `--owidle`) and the host's 240-frame timer are gone.

Checks: `ow-player-pose-cycle` (Left 20 / stand 10 / Left 20: f17=E222 f25=E204 f41=E227 f49=E204 f57=E222
f65=E204 -- the restart on leg B is the parity; mutations: no walk install, no parity, no pipeline) and
`ow-player-idle-fidget` (E240/E23B every 8 then the 24-frame hold; mutations: threshold, no install).

## 7. Open

- The port's frame numbers on a `--loadwram` seed run later than the oracle's (the entry fade ticks the
  sub-VM on the non-interactive path and eats the drive's first frames): the walk lands ~6 frames after
  the oracle's press row, the fidget ~16 frames after f75. A lead-block lockstep (`--checkowobj`-style on
  $0D70 +0B/+0E/+26/+28) would pin the phase.
- The BG3 cloud phase on the same seed differs from the oracle's (a cloud over Crono's head at f60 in the
  port, none in the oracle), so a pixel diff of the player box is not exact yet.
- Other lead characters: the tiles $100.. are per-character VRAM uploads whose uploader was not located;
  the atlas holds Crono (world 0). Magus's ids $0C-$17 are wired but its descriptors E2C1.. name the same
  tile slots. `$0100 == $01F5` -> attr $B8 (priority 3) not chased. The scripted-lead branch (`$0280 == 1`,
  cutscenes and the ferry) is outside this port.
- The ambient sprites (gulls, boats, balloons) are drawn without the PPU's +1 scanline; their checks pin
  the port's own frames.

## 2026-09-19: no $3404 block, no pose machine

The lead's pose machine (world.c CtWorldLeadAnimFrame, C2/3404 -> C2/1CDA -> C2/0E1D) ran every frame whether or not a
lead exists, and CtWorldLeadBlock falls back to $0D70 when none is tagged. On a scene world (no $3404 block on any leg
of the attract: cartridge f170 f2975 f5950 f8161 f8731) $0D70 is the scene's own script object ($0F63): the port
installed the stand pose ($C3:E14C) and the idle A2 ($C3:E1B8) on it and ticked it a second time a frame. 1F7 drew a
miniature Crono over the eruption's flames (port f4998-f5029, OAM tile $104/$160 where the cartridge draws the flame
$02A/$02C/$02E), and 1F3's Dactyl (the script object there) flapped at twice the rate ($0D70's dwell 0A,08,06.. against
C2:0E47's 0A,09,08..). field.c gates the whole block -- position, pose machine and the boat ride's tick -- on
CtWorldHasLead, as the draw already was.

**$0104 is the ARRIVAL byte, not the live facing (2026-10-07).** C2/3533 reads it into +28 once, at the init; walking writes
no $0104 (the 13 C2 writers are transitions: the exits' `AND #FC / ORA $0028,X` 44B4 4596 464F 4EAB 4F13 480C, the special
arrivals' `LDA #01` 34F0 478F 47B9, the scripts' 1141 24B6). Measured on saves/ow_w0.state: +28 03/00/01 while $0104 stays 01.
The port's host had written `$0104 = wd.pfac` every frame (and the init took wd.pfac, seeded 1): leaving the Porre ticket office
($0116 = 03) Crono stood facing down. Check ow-lead-faces-the-exit-arrival-byte-porre-office.
