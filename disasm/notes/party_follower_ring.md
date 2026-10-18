# The party conga is a VELOCITY RING at $7F:0C00, not a position trail

Derived 2026-09-03 from the ROM + the oracle only (defect #20 of the 2026-09-02 list: "frog/lucca is
glitching when joining the party and following crono"). This supersedes the port's invented ring-of-positions model
(`CtSceneUpdateFollowers`, `CT_FOL_DELAY 23` / `CT_FOL_CLAMP 15`), which was validated byte-exact on the
two-member scene-8 states and still diverges on a three-member party.

## 1. The divergence that started it

Fixture: `saves/battle_re/party3_field.state` (+ its lakewram) -- scene 130, party Crono/Lucca/Robo, the
three PCs stacked on tile (42,44) as obj1 (lead), obj3 (member 2) and obj4 (member 3).

```
CT_FIELDLOCK=saves/battle_re/party3_field.state build/Belthasar.exe --assets ct_assets.dat --scene 130 \
  --loadwram saves/battle_re/party3_field.lakewram --selftest --frames 140 --walkscript "l:30,d:30,r:30,u:30,n:20"
```

The LEAD stays byte-exact; both followers diverge:

| addr | frame | ours | oracle |
|---|---|---|---|
| $7E1806 (obj3 X lo) | f2  | E0 | F1 |
| $7E1808 (obj4 X lo) | f2  | E0 | F1 |
| $7E1807 (obj3 X hi) | f39 | 2A | 29 |
| $7E1886 (obj3 Y lo) | f55 | 10 | 20 |

Two facts in that first row: the oracle's follower has NOT MOVED at f2 (it still holds its seed value
$2AF1) while ours already stepped a pixel, and the oracle keeps the sub-pixel fraction `1` that our
position-copy model flattens to `0`. By f39 they are a pixel apart, by the end 15 px -- the follower
walking on top of the lead instead of trailing it.

## 2. The ROM

**The push -- C0/9E3F, once per frame the LEAD MOVES** (hooked: `--hookpc C09E3F` fires 1/frame from f2
while walking, 0 while standing):

```
9E42  LDA $AB / INC A / AND #7F / STA $AB / TAX      ; the write head, 0x80 entries, wraps
9E4A  LDY $6D                                        ; the lead's object offset
9E4C  LDA $1D32 / STA $7F0C00,X / STA $1900,Y        ; the lead's velocity X -> the ring AND the lead
9E56  LDA $1D33 / STA $7F0C80,X / STA $1980,Y        ; velocity Y
9E60  LDA $1D2C / STA $7F0D00,X                      ; the pre-adjust velocity pair (C0/895B/8833)
9E67  LDA $1D2D / STA $7F0D80,X
9E6E  LDA $0C00,Y / STA $7F0E00,X                    ; the lead's two sprite-front priority flags
9E75  LDA $0C01,Y / STA $7F0E80,X
9E7C  LDA $1D34 / STA $7F0F00,X                      ; the terrain/shape byte (stairs, ladders)
```

So the ring stores, per lead move: **velocity**, the pre-adjust velocity, both OAM-priority flags and the
shape byte. `$1D32/$1D33` is the lead's velocity in 12.4 (`F0` = -1 px); `$1D34` is the shape byte the
ladder/slope work already uses.

**Follower 1 -- C0/A26B** (`$99` = its object offset, `$AC` = its read cursor):

```
A26D  LDA #00 / STA $1900,Y / STA $1980,Y            ; default: NO velocity this frame (stand still)
A275  LDA $1D32 / ORA $1D33 / BEQ ->A28D             ; is the LEAD moving this frame?
A27D  LDA $AB / SEC / SBC $AC / AND #7F / CMP #18 / BCS ->A29D    ; moving:  gap >= 0x18 (24)?
A288  LDX $AC / BRL ->9F6F                           ;   too close -> the hold path (below)
A28D  LDA $AB / SEC / SBC $AC / AND #7F / CMP #10 / BCS ->A29D    ; stopped: gap >= 0x10 (16)?
A298  LDX $AC / BRL ->9F6F
A29D  LDA $AC / INC A / AND #7F / STA $AC / TAX      ; ADVANCE the cursor one entry
A2A5  LDY $99
A2A7  LDA $7F0C00,X / BEQ ->A2B6 / STA $1900,Y       ; copy that entry's velocity onto the follower
A2B0  LDA $7F0C80,X / BEQ ->A2BD
A2B6  LDA $7F0C80,X / STA $1980,Y
A2BD  LDA $7F0E00,X / STA $0C00,Y                    ; ...and its priority flags
A2C4  LDA $7F0E80,X / STA $0C01,Y
```

**Follower 2 -- C0/A2CE** is the same routine with `$9B` / `$AD`, and its gap is measured **against
follower 1's cursor**, not the head: `A2E0 LDA $AC / SEC / SBC $AD / AND #7F`. The conga is a chain --
each member trails the one in front of it by the same gap.

The follower never has its position written directly: it gets a VELOCITY and the ordinary mover
integrator (C0/AB28 / AB42, the `--writer` on `$1806`) moves it, which is why its sub-pixel fraction is
preserved for the whole walk.

**Measured chain** (`--hookpc`, 40 frames of Left on the fixture above):

| PC | what | frames it fires |
|---|---|---|
| C0/9E3F | the push | 1 per frame from f2 |
| C0/A26B | follower 1 entry | every frame from f1 |
| C0/A29D | follower 1 ADVANCE | first at **f25** (the head is 24 entries ahead = 0x18) |
| C0/A2CE | follower 2 entry | every frame |
| C0/A30A | follower 2 ADVANCE | never in 40 frames (its gap is off `$AC`, which only starts at f25) |

and the follower's own position first changes at f24/f25 with velocity `$1906 = F000` written by A2B0 --
matching the advance exactly.

**The hold path C0/9F6F** (taken while the gap is under the threshold) is NOT a plain "stand still": it
reads the ring entry at the CURRENT cursor and computes from `$7F0F00` (the shape byte), `$7F0C00` and
`$7F0D00` -- the stair/ladder alignment for a follower that is holding position. Not yet decoded; the
default `$1900/$1980 = 0` set at A26D is what a plain-ground hold produces.

## 3. What the port became (PORTED 2026-09-04)

`CtSceneUpdateFollowers` now IS that: it pushes `{velX, velY, preX, preY, prio0, prio1, shape}` into the
0x80-entry ring at `$7F:0C00/0C80/0D00/0D80/0E00/0E80/0F00` with the head `$01AB`, and per follower
compares `(prev cursor - my cursor) & 0x7F` against 0x18 (lead moving) or 0x10 (lead stopped), advances
the cursor when it reaches it, and copies that entry's velocity into `$1900/$1980`, integrating it as
C0/AAFD does. Cursors: `$01AC` (member 2) chained off `$01AB`, `$01AD` (member 3) chained off `$01AC`.
The state lives at the ROM's own addresses, so a `--renderwram`/lockstep fixture carries it for free and
`CT_RINGDBG=1` prints head, both cursors, the follower position and its facing per frame.

That retires DELAY/CLAMP/folIdx/folOffX/folOffY/folHFace and the re-anchor and decay heuristics: a
follower left off the path by a script simply resumes with the next velocity it is handed, which is what
the ROM does.

Not yet derived (needed before the port can be byte-exact in every case): the hold path C0/9F6F's
alignment arithmetic, the gate that calls the push (no `JSR C0/9E3F` exists in the image -- it is entered
by a branch), and what makes `$1D32|$1D33` non-zero on a frame the lead is being moved by a SCRIPT rather
than by input.

## 4. Two details closed after the first pass

**The integrate is ticket-gated and sign-extends the byte** -- C0/AAFD, verbatim:

```
AAFF  LDA $1B80,X / BEQ ->AB06 / BRA ->AAC7     ; the $1B80 clamp flag routes elsewhere
AB06  LDA $1A01,X / BNE ->AB0C / RTS            ; NO ticket -> no integrate
AB0C  DEC A / STA $1A01,X                       ; consume one unit
AB10  LDA $1900,X / BPL ->AB1C / REP #20 / ORA #FF00   ; SIGN-EXTEND the velocity BYTE
AB1C  REP #20 / AND #00FF
AB21  CLC / ADC $1800,X / STA $1800,X           ; 16-bit add into the 12.4 position
AB2A  ...the same for $1980 -> $1880
```

Hooked (`--hookpc C0AB0C --hookwatch 7E1A06`): it runs THREE times a frame on the three-member fixture,
`$6D` = 02, 06, 08 (the lead and both followers), with A = 1 at entry every time -- every PC mover is
re-armed with a one-unit ticket each frame, so a follower whose velocity is 0 simply integrates nothing.

**The follower's FACING is derived from its own copied velocity**, by the same setter family that faces
the lead (C0/A080 / A08A / A09E -- the `LDA #nn / STA $1600,X` arms). Measured on the fixture with
Left 40 / Down 40 / Right 40:

| frame | lead $1602 | follower $1606 | follower velocity |
|---|---|---|---|
| f24  | 02 | 02 | $1906 = F0 (left)  |
| f41  | 01 | 02 | still left |
| f64  | 01 | 01 | $1986 = 10 (down) |
| f81  | 03 | 01 | still down |
| f104 | 03 | 03 | $1906 = 10 (right) |

so the facing turns exactly when the copied velocity turns, 23 frames behind the lead, with no separate
facing history: right = 3, left = 2, up = 0, down = 1. The port's `folHFace` ring is unnecessary.

## 5. Two things the first pass did not have, found while porting

### 5a. THE PUSH'S GATE IS "THE LEAD MOVED", NOT "THE VELOCITY IS NONZERO"

Section 3 said the gate was undecoded (no `JSR C0/9E3F` exists; the push is entered by a branch). It
still is, on the ROM side -- but the behaviour is now measured, and it is NOT the `$1D32 | $1D33` test
that C0/A26B uses for its threshold. Those are two different questions, and one fixture cannot tell them
apart. `--monitor "7E01AB:1,7E1D32:2,7E1806:2" --from two_member.state --btnscript 80:12`, walking right:

```
f3   $01AB=03  $1D32=1000  lead $1806=B008
f4   $01AB=03  $1D32=1000  lead $1806=B008     <- velocity still $10, lead did NOT move, NO push
f5   $01AB=04  $1D32=1000  lead $1806=C008
```

One frame in ~24 the lead's position does not change even though its velocity byte is nonzero, and the
ring skips that frame. Gating the push on the velocity instead puts one extra entry in the ring and the
follower starts a frame early and stays 1 px ahead for the rest of the walk -- `--followtest` on
two_member reads `maxerr=1px, framesOff=216` of 240. On `party3_field.state` the lead moves on every
frame, both gates agree, and the run is byte-exact either way: the three-member fixture is blind to it.
That pair of blind spots is why both checks exist (`follower-conga-2member` owns the push gate,
`follower-conga-3member` owns the chain).

### 5b. The facing dispatch, decoded

Section 4 measured the facing empirically. The mechanism is C0/9EFC (reached from the copy path's
`BRL ->9ED1` when the shape byte is 0), and it reads the PRE-ADJUST pair, not the velocity:

```
9EFC  LDA $7F0D00,X / BPL / EOR #FF / CLC / ADC #21     fold the X magnitude...
9F07  LSR A / LSR A / LSR A / STA $DB                   ...and scale it down by 8
9F0C  LDA $7F0D80,X / BPL / EOR #FF / CLC / ADC #21     fold the Y magnitude (NOT shifted)
9F17  ORA $DB / AND #7E / TAX / JSR ($9FF7,X)
```

so the index is `(|preY| folded) | (|preX| folded >> 3)`, and the sign rides in the fold: a positive byte
keeps its magnitude, a negative one becomes `~a + 0x21`. Walk = 0x10, run = 0x20. The table at C0/9FF7
and its four cardinal handlers:

```
idx 02 -> C0/A083  LDA #03 / STA $1600,X   right       idx 04 -> A0AB  right, RUN
idx 06 -> C0/A079  LDA #02 / STA $1600,X   left        idx 08 -> A0A1  left,  RUN
idx 10 -> C0/A097  LDA #01 / STA $1600,X   down        idx 20 -> A0BF  down,  RUN
idx 30 -> C0/A08D  LDA #00 / STA $1600,X   up
```

which reproduces section 4's measured table exactly, and explains why there is no facing history: the
facing is a pure function of the ring entry the follower just consumed. The four cardinals are ported.


### 5c. Op 0xDA (C0/345A) RESETS the ring -- found through the bell (defect #6)

The event's party-regroup op ends with `C0/347D LDA $AB / STA $AC / STA $AD` -- both cursors := the head --
and then zeroes the head entry's velX (`$7F0C00,X`, twice), preX (`$7F0D00,X`, twice) and shape
(`$7F0F00,X`) rows. The port's op 0xDA had skipped those as 'five OAM stores, a render detail', which was
inert while the conga was a position trail and is WRONG now: a follower regrouped onto the lead with a
stale cursor consumes pre-cutscene entries the moment the lead walks (Marle walked north after the bell).
Measured on saves/field_re/s5_bell_marle.state: oracle `$01AB/AC/AD = 48 48 48` at f385, Marle holds on
Crono's tile until the head is 0x18 ahead (f505). Ported; see bell_marle_regroup.md and the check
field-bell-marle-regroup-ring.

## 6. Still not derived

- (CLOSED 2026-09-05, section 8) ~~the sub-threshold HOLD path C0/9F6F with a non-zero shape byte~~.
- (CLOSED 2026-09-05) ~~the DIAGONAL slots of the C0/9FF7 table~~ -- the follower entries now dispatch
  through move.c's `CtAnimDispatch9FF7`, the same table walk the lead's C0/9E84 port uses (cardinals,
  diagonals with their keep-rules, the A171/A18C/A1E9 tails), so nothing is hand-copied any more.
- (CLOSED 2026-09-05) ~~`$1601,Y` / `$1681,Y` / `$1680,Y`~~ -- written by those tails now.
- What makes `$1D32|$1D33` nonzero on a frame the lead is being moved by a SCRIPT rather than by input:
  the push is the LEAD's own op 0xB0 (C0/3047 `JSR C0/9E29`, see section 8), so a lead whose ip a script
  moved off its B0 pushes nothing. The port's push still carries the measured `leadStepped` term on top
  of `$1D32|$1D33` (section 3); on party3 that term is what keeps the head at the oracle's value on the
  frame the lead is blocked by an NPC with a live velocity.

## 7. Grading

`--followtest` was extended to diff BOTH followers and to hand the ring the oracle's own `$1D32/$1D33`
(it teleports our lead onto the oracle's, so the velocity has to come from the same place). It used to
resolve one move-mode-1 object and report BYTE-EXACT while member 3 was free to be wrong -- the blindness
that let the invented model stand. Both fixtures are now byte-exact over 240 frames of four-direction
walking: `two_member.state` (Crono+Marle, scene 8) and `saves/battle_re/party3_field.state`
(Crono/Lucca/Robo, scene 130), `maxerr=0px framesOff=0`.

The live `CT_FIELDLOCK` run still reports the ring bytes one frame off. That is the harness, not the
model: it injects the button into the oracle for frame N (the SNES auto-joypad delivers it to ROM logic
on N+1) while our controller consumes it on N, and it FORCES our lead onto the oracle's lead mid-frame.
What did change there is the substance -- the follower POSITION divergences went from eight to four and
the first one moved from f2 to f24.

## 8. The anim arm and the gate are the PCs' own op 0xB0 (PORTED 2026-09-05)

The port of section 3 derived the follower's FACING and never wrote `$1680/$1681/$1601`: Marle trailed
Crono in a fixed pose ("marle often walks without animation, follows crono without animation"). Two things
were missing, and both fall out of one xref: **C0/A26B and C0/A2CE have exactly one caller each** --
`3037 JSR C0/A26B` and `3027 JSR C0/A2CE` inside **C0/3002**, which is **op 0xB0** (C0/2FFD, op 0xAF,
is `JSR 3002 / INX`):

```
3002  LDA $38 / BEQ ->3009 / TYX / CLC / RTS          ; $0138 (the camera ALIGN machine) != 0 -> nothing
3009  STY $C7 / SEP #10 / LDX $6D / LDA $1100,X
3012  BEQ ->3040                                       ; state 0 = the LEAD:  $1A01,X := 1 / JSR C0/9E29 (push)
3014  DEC A / BEQ ->3030                               ; state 1 = member 2: $1A01,X := 1 / JSR C0/A26B
3017  DEC A / BEQ ->3020                               ; state 2 = member 3: $1A01,X := 1 / JSR C0/A2CE
301A  REP #10 / LDX $C7 / CLC / RTS                    ; anything else: a bare yield
```

**The gate.** A PC's fn0 is `5C 00 B0` (scene 5 obj2; `ct.py script` stops at the RETURN and hides the
B0). The init RETURN runs at prio 7, and C0/5F74's prio-7 arm is `5FB3 TYX / CLC / RTS` -- the ip steps
PAST the 00 and the object sits on the B0 for the rest of the scene, executing it on every visit
(`--followvm --obj 2 --from s5_bell_marle.state`: ip=05D2, raw `00 B0`, st=01; obj1 ip=0594 `11 B0`
st=00). So the conga runs on exactly the frames the follower's script executes B0 with `$0138 == 0`. A
follower whose ip a CALL or op 0xD9 (C0/32F4) moved off the B0 gets nothing -- the script owns it -- and
one parked on 0xB2 (C0/3543, a bare yield) gets nothing either. That replaces the port's CT_FOL_ROAMS
stand-in (B0 OR B2 OR no script, AND the host's explore/moveLock). Measured consequence: after a touch's
`E3 00` control lock the ROM's followers KEEP consuming the ring until their gap drops below 0x10
(party3, `--monitor 7E011F,7E1806,7E1808 --input 40:200`: `$011F=0` from f123, member 2 walks 586->581
over f124-f129, member 3 609->596 over f124-f137); the explore gate froze them on the lock frame.

**The arm.** All four hold sites (A28A/A29A/A2ED/A307) `BRL ->9F6F` with X = the entry the cursor is
holding on; the two advance tails (A2CB/A338) `BRL ->9ED1` / `->9F20` with X = the entry just copied. The
three entries share one shape -- `STZ $E5 / LDY $6D / LDA $7F0F00,X` -- and end in `JSR ($9FF7,X)`, the
lead's own handler table (every handler begins `LDX $6D`). They differ in the INDEX:

- after an advance (9EFC/9F4B): `fold($7F0D00,X) >> 3 | fold($7F0D80,X)`, the entry's pre-adjust pair =
  the lead's own index at push time -> A171 walk (`$1680 := 1`) or A18C run (`$1680 := 6`; a follower is
  never `$0197`, so never the head-shake);
- on a hold (9F7B-9FCF): per axis `(pre-adjust - adj(velocity))`, adj() moving a byte with bit 3 set by
  8 away from zero, negated `EOR #FF / INC A`, `ADC $7F0D00,X`, negatives folded `(0x20 - v)`; an ordinary
  entry (pre == velocity) gives 0 -> slot 0 = 9FF2 = `LDX $6D / BRL ->A1E9`, the IDLE chain, whose A211
  arm stores `$1680 := 0` -- how a follower that has caught up stops walking;
- the stair shape (`$7F0F00,X` bit 2): after an advance `$7F0D80,X == 0` clears `$1681/$1601` (9EF1) and
  anything else is `BRL ->A254` (mode $1B; the `CMP #18` before it has no consumer); on a hold (9FD3) the
  clear needs `$7F0D80,X == 0` or `$7F0C80,X == 0` or `$7F0C80,X == $7F0D80,X`, else nothing.

The port: `ctFollowerAnimArm` (ct_vm.c) computes the index and calls `CtAnimDispatch9FF7` (move.c, the
table walk extracted from `CtPlayerAnimSelect`); `CtSceneUpdateFollowers` now runs INSIDE `CtSceneTick`
right after the runner (the op's slot) so C0/A810's integrate and C98A leg and the drain see the bytes
the same frame -- field.c used to call it after the tick. Measured on the bell fixture
(`--monitor "7E1684:2,7E1604:2,7E0F05:1" --input 80:40,00:30`): `$1684 = 01xx` with the phase stepping
2,3,4,5,0,1 every ~7 frames, `0000` from f36; drawn frame `$0F05` = 13,0E,0F,0A,17 then 03. The port
reproduces every transition (draw frame N = oracle N+1); the one residual is the oracle holding 0F one
frame longer at f22, its drain missing the object on that frame (the raster deadline the port's drain
cannot clock). On party3: walk starts f24/f47, member 2's facing turns down at f54, both go idle after
the lock at gap 0F. The first cut of this measurement had the port's lead one pixel further against the
blocking NPC (565 vs 566, head 69 vs 68, rests 580/595 vs 581/596). Two things, neither the ring's:
(a) fieldPlayerCollide fed the C0/9923 scan the NPC's INTEGER position, while C0/9975 `LDA $1800,X` reads
the 12.4 word with its fraction (the left feeler's probe is the tentative x - 0x70, C0/8BD7, and the
Diablos sits at 544.25 on the hit frame: 0x2204 - 0x22E0 - 1 = -0xDD -> 221 < 0xE0 hits, the integer
0x2200 gives 225 and misses) -- it now copies the VM's own words; (b) DRIVE PHASE: the port applies a
--walkscript direction on the frame it is issued, the SNES auto-joypad hands the ROM a press one frame
later, so an unshifted drive walks the lead one frame ahead of the oracle's while the script-driven NPC
keeps its phase, and the feeler meets the NPC at 543.81 (miss) instead of 544.25 (hit). With the drive
shifted by one idle frame (`n:1,l:150`) the port is byte-exact: lead 566, ring 68/59/4A, rests 581/596
(`CT_9923DBG=1` prints the probe census). The check pins that drive.

Not modelled: a visit the runner's raster budget skipped (C0/5A5D) -- the pass runs once per frame; the
lead's push keeps the measured `leadStepped` term (section 3). Checks: `field-fair-follower-anim-arm`
(two mutations: the advance arm's dispatch, the hold arm's idle slot) and
`field-party3-followers-trail-past-lock` (mutation: the explore gate back). `CT_FOLDBG=1` prints the
per-member census.

## 9. THE LOADER EMPTIES THE RING (C0/4356-4386), and that is what a door broke (PORTED 2026-09-06)

Sections 1-8 all measure ONE scene. `--followtest` does too: it drives a single location and never loads
another. The moment a real game crosses a door the picture changes, because the PC loader's slot-1 and
slot-2 arms (C0/4310 / C0/43C3) end in a tail the port had only in the lockstep VM (move.c) and never on
the live path:

```
4356  STZ $AB / 4358 STZ $AC / 435A STZ $AD     ; the head and BOTH follower cursors -- the ring restarts empty
435C  LDA $97 / 435E BMI ->438A                 ; no leader in the party table -> the default placement
4360  REP #20 / AND #00FF / TAX / LDA $1800,X / LDX $99 / STA $1800,X    ; else: the LEAD's position
436E  LDX $97 / LDA $1880,X / LDX $99 / STA $1880,X
4378  STZ $1900,X                               ; and no velocity
437D  LDX $97 / LDA $1600,X / LDX $99 / STA $1600,X                      ; and the LEAD's facing
438A  LDA $02 / STA $1801,X / LDA $03 / STA $1881,X / LDA $04 / AND #03 / STA $1600,X
439B  LDA $04 / BIT #08 -> $1880,X = $7F : $FF / 43AD LDA $04 / BIT #04 -> $1800,X = $00 : $80
```

The no-leader arm at 438A is the same five stores the LEAD's own arm makes (C0/42BA/42C1/42E1/42E8/42FA,
pcArrivalArm), from the location's arrival bytes -- and it is the arm that actually runs on a door, because
$0197/$0199/$019B are refilled as each PC's fn0 executes and a follower whose object index comes first
(scene 439: Marle is obj3, the lead obj1... but the party table is still $80 when her `5C` runs) finds no
leader yet. Either way the follower lands on the arrival tile with the arrival facing.

MEASURED, scene 5's north door into 439 with Marle in the party (`saves/predoor_f2f_s5.state`,
`--btnscript "10:60,0:90"`, `--monitor "7E0100:2,7E01AB:3,7E1806:2,7E1886:2,7E1606:1,7E1106:1"`):

| frame | oracle |
|---|---|
| f6 | loc $0005, ring 1A/09/00, obj3 not loaded ($1106 = 80) |
| f7 | loc $01B7 -- the location flips, the ring is STILL 1A/09/00 |
| f35 | ring 00/00/00, obj3 at (392,335.94) facing 00, mode 01 -- the lead's own tile and facing |
| f114 (walking right from f90) | the head reaches 0x18 and she takes her first step, 1 px/frame along his path |

The port kept the old cursors: in scene 439 the follower spent scene 5's DOWN entries, walked to
(392,347) -- 15 px south of the lead -- and stood there facing DOWN while he faced UP. Both halves of the
user's report of 2026-09-05, "the npcs start the scene facing the wrong way" and "marle moves
badly when following crono", are that one missing tail. Ported as `pcFollowerArm` + `pcPlaceDefault` (ct_vm.c),
called from all four PC-load sites (prescan and live, ops 0x57/0x5C/0x62 and 0x80) exactly where the lead
calls `pcArrivalArm`. Check: `field-door-follower-lands-on-lead`, two mutations (no ring clear -> she
walks south on the stale entries; no default placement -> she arrives facing down).

Residual, NOT the follower's: the port's lead keeps consuming the held d-pad direction across the
transition (it ends 3 px past the arrival tile where the oracle's lead does not move at all, with Up held
for 60 frames), so a walk-on comparison is still one lead-position apart. The ROM ignores input while the
arrival runs -- the gate is not derived yet.

## 10. Op 0xAF is the same gate, one byte further on (PORTED 2026-10-03)

Section 8 named C0/2FFD and the port did not use it: the conga ran only for a follower whose ip sat ON a `B0`. The
user's F5 of 2026-10-03 (loc 081, Crono lead obj1, Lucca follower obj3, "lucca does not follow crono") is the case that
needs it. Scene 129's PCs are not all `00 B0`:

```
obj3 fn0  00AC  80 02                LOADNPC 2 (Lucca)
          00AE  00                   init RETURN
          00AF  12 0C 00 00 04       if loc[0C] == 0 ... else -> 00B7
          00B4  AF                   C0/2FFD = JSR C0/3002 / INX / RTS   (op table C0/5D6E: AF -> 2FFD, B0 -> 3002)
          00B5  10 02                -> 00B8
          00B7  B1                   (loc[0C] != 0: a bare yield -- her fn3 sets it, `75 0C`)
          00B8  11 0A                -> 00AF
```

Every visit runs the jumps, reaches the `AF` and yields one byte past it: the ip a frame ends on is 00B5 (WRAM
`$1186` = 03D5, whose byte at `$7F:2001` + 03D5 is that `10`), never on the op that ran. C0/3002's whole body -- the lead's
9E29, A26B / A2CE for $1100 = 1 / 2, the touch probe -- runs on that visit all the same.

Cartridge (the rule): `--teleport 129 8 25 0 --from saves/battle_re/party3_field.state --method jmp` (party
Crono/Lucca/Robo; `--method req` keeps scene 130's script and does not test this), saved as
saves/field_re/lucca_af_s129.state; walking Up/Left/Right 40 frames each, Lucca's `$1806/$1886` trail Crono through
66/41 values. The port from the same WRAM (`--loadwram saves/field_re/lucca_af_s129.lakewram`) ran the same values
one frame label later (the monitors' f0 differ by a frame); before the fix it held 8008/FF19 for the whole walk.

Port (src/ct_vm.c): op 0xAF takes op 0xB0's branch (lead anim arm, touch probe) and then steps the ip (2FFD's INX);
`atB0[]` is set for both; the follower gate `CT_FOL_ON_B0` accepts an ip on a `B0` or one byte past an `AF` whose visit
just ran it. Pinned by `field-follower-on-op-af-follows-vs-oracle`.
