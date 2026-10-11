# Sprite depth: the op-0x8E AUTO switch, the sub-screen foreground mask, and "no PC loaded"

Three independent defects, all reported as things being drawn at the wrong depth or drawn at all.

---

## 1. `$0F80` bit 7 is the AUTO SWITCH — the byte's nibbles are the other source

User report (translated from Portuguese): *"in scene 157, the barman should be on top of the door behind him (Z priority)"*.

`$0C00,X` / `$0C01,X` are the OAM priority bits the assembler ORs into each sprite tile's attr
byte (`oam.c`: tiles with `yoff < 0xE8` take `$0C00` = the TOP half, the rest `$0C01` = the
BOTTOM half). **Every ROM routine that writes the pair branches on `$0F80` bit 7 first:**

| routine | the branch | AUTO arm (bit7 SET) | FIXED arm (bit7 CLEAR) |
|---|---|---|---|
| C0/4C74 — the op-0x8E handler | `STA $0F80,X / BIT #80 / BEQ ->4CB7` | 4C83: foot tile props | 4CB7 |
| C0/4C42 — the loader re-derive | `LDA $0F80,X / BPL ->4C6F` | 4C47: foot tile props | *skips* (keeps the byte's value) |
| C0/AD90 + C0/ADFA — the mover | `LDA $0F80,Y / BPL ->ADD7` | ADBB: foot tile props | ADD7 |

The FIXED arm (C0/4CB7-4CC8, identical at ADD7-ADE8):
```
STA $D9
AND #30       -> STA $0C01,X       ; the BOTTOM half = pv & 0x30
LDA $D9
ASL ASL ASL ASL
AND #30       -> STA $0C00,X       ; the TOP half    = (pv << 4) & 0x30
```
So the LOW nibble of the op-0x8E byte is the TOP half's OAM priority and the HIGH nibble the
BOTTOM half's. 3 (`0x30`) = OAM priority 3 = **in front of** BG prio-1 foreground tiles;
2 (`0x20`) = behind them.

The AUTO arm reads the FOOT tile's property planes: `0x20 | ((prop & 0x40) >> 2)`, prop2
(`$7E7040`) → `$0C00` and prop1 (`$7E7000`) → `$0C01` at 4C8B-4CA8. (AD90's arm assigns the two
the other way round — that asymmetry is in the ROM, not a transcription slip.)

**The defect.** The port read the foot tile *unconditionally* and papered over the gap with a
"both nibbles == 3" override. That agrees wherever the foot tile happens to say the same thing,
which is exactly why the wrong rule survived: scene 18's ticket vendor has `$8E = 0x23` **and**
a foot tile with the head-front bit. Scene 157's barman (obj8, the same `0x23`) sits on a foot
tile with NEITHER bit, so his head drew behind the bar's foreground row.

Oracle `o157.raw.wram` (`--teleport 157 52 10 0`): obj8 `$0F80 = 0x23`, `$0C00 = 0x30`,
`$0C01 = 0x20` — head over the counter, legs behind it. Scene 129 obj9 (also `0x23`) and
obj4/obj12 (`0x33` → `0x30`/`0x30`) confirm the nibble decode across a second scene.

Fix: `src/field.c`, in the actor draw loop —
`if (pv & 0x80) footPrio(...); else { tf = ((pv << 4) & 0x30) == 0x30; bf = (pv & 0x30) == 0x30; }`.
This subsumes the old 0x33 override (0x33 and 0x3B both decode to 0x30/0x30).

⚠ `fieldPrioLayer` decomposes the SAME byte differently (`pv & 0x0C` selects one of C0/B330's
three OAM write groups). Both are real and independent — the group is sprite-vs-sprite, the
nibbles are sprite-vs-foreground.

---

## 2. The BG1/BG2 priority-1 mask must be built on the SUB pass too

User report (translated from Portuguese): *"in scene 129, the nuns should have lower priority than the bench"*.

`CtComposeScene` (`src/scene.c`) draws the scene twice — a MAIN pass over `screen_flags & 7`
and a SUB pass over `(screen_flags >> 4) & 7` — and blends them. It passed `priomask` only to
the MAIN pass.

Scene 129 (Manoria Cathedral) has `screen_flags = 0xB4`: `main_t = 4` (BG3 only),
`sub_t = 0xB` (BG1+BG2). So the whole scene reported `fgpx = 0` and **nothing in it could
occlude a sprite** — the nuns drew over the pews.

The main/sub split in this compose models COLOUR MATH; the SNES priority ladder
(OBJ3 > BG1.1 > BG2.1 > OBJ2 > BG1.0 > BG2.0 > …) is independent of TM/TS, and the compose
already shows sub-screen pixels wherever the main screen is uncovered (`mc == 0 && sc`).

Measured, on the oracle at loc `$081`:
- BG1's live VRAM tilemap has **392** entries with bit 13 (priority) set (BG2 19, BG3 1205).
- The pews are BG1 metatiles at (4..6, 18..19) — tileset 11 metas 213/214/216/229/200/232 —
  and every one of them carries priority corners.
- `PEEK_ISOBG=0` (BG1 alone) renders the pews; `PEEK_ISOBG=1/2` (BG2/BG3) are empty there.
- The nuns are obj10/obj11, `$0F80 = 0x80` (AUTO), `$0C00 = $0C01 = 0x20` = OBJ priority 2 —
  i.e. *behind* BG1 priority-1. The oracle draws them behind the pews.

Fix: pass `priomask` to both `draw_pass` calls.

---

## 3. A location that loads no PC draws no player

User report (translated from Portuguese): *"crono is visible in scenes where he should not be, like in scene 240 or 92"*.

Locations 92 and 240 are pure cutscene maps — every object is `83 xx xx` (LOADMON) plus a
SETCOORD and a `91 HIDE`; not one `57` (LOADPC) or `80 00` between them. The ROM keeps the
lead-PC object offset in `$0197` and leaves the **0x80 "none" sentinel** there:

```
$ ct.exe --teleport 240 7 16 0 --from saves/truce_market.state
teleport[jmp]: from 011 -> requested 0F0, now 0F0  scene=LOADED  player=UNPLACED
  leadPC obj=64 pos=(4080,0) [sentinel] facing=0
  $0197 playerobj = 128
```
and the oracle's frame is entirely black. (Same for loc 92.)

This host renders the lead PC itself, at its own `px/py`, alongside the VM actors — so it needs
that answer explicitly. `CtSceneCreate`'s init scan already knows it: `found` is 0 when no
object's fn0 runs op 0x57 or `80 00`. It is now kept as `sc->playerLoaded` and exposed as
`CtScenePlayerLoaded`; `field.c` skips the player entry in the draw list when it is 0.
`playerObj` still falls back to 1 so every `PC_i = playerObj + i` reference stays in range.

Measured across the scenes the engine already renders (`CT_TRIGDBG=1`): 92 and 240 are the only
two of {1, 2, 5, 8, 92, 113, 129, 157, 240, 439} where the lead-PC object is unplaced.

---

## 4. The draw list IS the ROM's camera window — nothing may narrow it

User report (translated from Portuguese): *"when scrolling the camera, sprites are appearing on screen only when their bottom
part becomes visible (feet)"*.

`CtSceneActor`'s `placed` is already `$0F00,X` — the latch C0/A810 maintains through C0/A88D
(and C0/A947 for class 7). **C0/B127, the ROM's own draw-list gate, runs literally the same four
compares:**

```
B12E: $C3 = $1D0A - 4   (clamped to 0)        B155: LDA $1801,X / ASL A
B13A: $C4 = $1D0C + 4                         B159: CMP $C3 / BCC ->B190     ; out
B142: $C5 = $1D0E       (BPL clamp only)      B15D: CMP $C4 / BCS ->B190     ; out
B14B: $C6 = $1D10 + 5                         B161: LDA $1881,X / ASL A
                                              B165: CMP $C5 / BCC ->B190
                                              B169: CMP $C6 / BCS ->B190
B16D: LDA #80 / STA $0E80,X / $0E81,X / $0F00,X ...
```

The operand is the object's TILE **doubled** (`ASL A`), so in tile units the window is
`left-2 .. right+1` and `top .. bottom+2`; in pixels, `camX-32 .. camX+CAMW+31` and
`camY-16 .. camY+CAMH+47`.

The port had a **second, hand-rolled camera box** ANDed onto `pl`:
`ax >= camx-48 && ax <= camx+CAMW+48 && ay >= camy-64 && ay <= camy+CAMH+16`. Three of its
edges are wider than the ROM's and therefore inert; the fourth, `+16px` below, is **31px
tighter**, and the bottom edge is the one that shows. Scrolling DOWN, an NPC entering from below
should reveal its HEAD first — but it was culled until its FOOT came within 16px of the screen
bottom, by which point ~16px of it was already due on screen. So it popped in whole instead of
sliding in.

**Measured** at scene 5, camera (240,352) — ours `--at 376 488`, the oracle `--teleport 5 23 30 0`
settled 240 frames, both reporting the same camera cells `$1D0A/$1D0C/$1D0E/$1D10 = 1E/3E/2C/48`:

| | oracle `$0F00 != 0` | ours, before | ours, after |
|---|---|---|---|
| active set | 12, 14, 15, 16, 17, 21, 22, 23, 24 | …, 24 and 15 MISSING | identical |

`obj24` foot y 596 (head 12px on screen) and `obj15` foot y 623 were both rejected by the box at
`y > 592` and both latched by the ROM. Fix: `if (pl)` alone.

The blind spot that hid this: `--vmdiff VMDIFF_ACT=1` compares our `$0F00` against the oracle's
and reads CLEAN — but it grades the LATCH, and the defect was a second filter applied downstream
of it, inside the renderer. (The same run excluded class 7 from that compare, on the belief that
A810 never writes it; only class 8 is never written. Both are corrected.)

## 5. An occluder only loses where BG1/BG2 are actually ON SCREEN — a WINDOW can take them off it

Section 1's rule ("`$0F80` prio 0/1 → the object loses to the opaque room BG, so the port draws it as a
see-through-to-the-map STENCIL") is the PPU's mode-1 layer order, and that order is a list of
**(layer, priority) slots**, not of layers. LakeSnes' own table — `snes/ppu.c`
`layersPerMode[1]` / `prioritysPerMode[1]`, the code the oracle renders with — pairs up to:

```
OBJ3, BG1.1, BG2.1, OBJ2, BG1.0, BG2.0, OBJ1, BG3.1, OBJ0, BG3.0, backdrop
```

So an **OBJ priority-0** pixel is beaten by BG1/BG2 at *either* priority and by BG3 priority 1, and by
nothing else. That is why a `8E 00` object is invisible in a room. But `ppu_getPixel` folds the WINDOW
into the same test as the enable bit —

```c
layerActive = ppu->layer[curLayer].mainScreenEnabled && (
  !ppu->layer[curLayer].mainScreenWindowed || !ppu_getWindowState(ppu, curLayer, x));
```

— so a layer a window masks off **this pixel** is not in the list at all. Inside the Telepod iris, where
`W12SEL = 0x22` hands window 1 to BG1 and BG2, the priority-0 swirl therefore **wins**, and the gate is
the visible blue vortex.

**Measured in the oracle.** `saves/field_re/scene8_gate_cutscene.state` driven to the gate with the
cutscene's own A-mashing (`--btnscript "0:15,100:3,0:27,..."`). `$0139` — the iris-ramp flag op-0xFF
0x90 sets — goes `0 → 1` at **f1746** and back to 0 at **f1779**: 33 frames, which is the +1/frame
radius ramp of C0/2260 finishing, *not* the gate closing. At f1790, `--peek` with `PEEK_ISOBG`:

| isolated layer | what it renders |
|---|---|
| BG2 alone (`PEEK_ISOBG=1`) | the room with a **black circular hole** punched at the gate |
| OBJ alone (`PEEK_ISOBG=4`) | the full **64×64 blue swirl disc**, plus the characters and the arcs |

Those are the two halves of what the player sees.

**The defect this found.** The port had the disc pass (it paints BG3/black inside the window) and the
FG pass (it skips the window), but the occluder pass did not: it stencilled obj27/obj28 back to the map
across the whole circle, and all that survived was a thin ring at the rim where the swirl sprite is
transparent — the user's *"the circular portal … is not visible or did not render properly"*. The
occluder now **stencils the pixels the BG still owns and DRAWS the ones the window took from it**,
clipped to that same window, which is why it may fall through into the ordinary sprite path instead of
`continue`-ing out of it.

**The census.** `CT_ACTDBG`'s iris line carries `occl cut=… win=… drawn=…` — pixels stencilled back to
the map, pixels the window took BG1/BG2 away from, and pixels the occluder therefore put on screen
(`drawSprite` returns its plot count). Mid-ramp is the frame that proves the split is per PIXEL and not
per object: at f1860, `r=21`, the *same* object pays both halves.

```
[iris f1860] ... r=(21,21) irisState=1 occl cut=1434 win=1438 drawn=1438
[iris f1890] ... r=(31,31) irisState=1 occl cut=0    win=2872 drawn=2872
```

⚠ The rule is stated generally, but the iris is the **only** BG1/BG2 window this port models, so the
arm is gated on the disc the portal state machine publishes. And the clip the fall-through applies is
forced by `sten[i]`, a term this scene cannot grade: ct_vm.c's emitter scan marks every co-located
effect sprite `colorGated`, so obj27 and obj28 already answer that test and the OR short-circuits
either way (scene 8 reports `nSten=2`, and those two *are* the pair). It stays because it is the rule —
without it a non-colorGated occluder would paint its whole 64×64 square over the room — and the
mutation that would have graded it was removed with the reason recorded. What would grade it is an
`8E 00`/`8E 01` object standing inside an open iris that is neither the effect pair nor a PC sprite.

## 6. The lead PC is not exempt from it either — the gate round trip is one byte

Section 5 fixed the swirl. The same window rule runs the **characters** that pass through the gate, and
the port had a hand-made rule for each end of it:

- `pGateIris` — "a lead PC whose foot sits within 31 px of the iris centre is clipped to it"
- `gatePC` — the same 31 px for a PC-sprite *actor*

Both are inventions. The ROM parks the PC at **OBJ priority 0** and lets the window decide.

**Measured in the oracle**, the whole Telepod round trip from `saves/field_re/scene8_gate_cutscene.state`
driven by the cutscene's own A-mashing (`--monitor "7E0F80:8,7E0C00:8,7E0139:1" --diff`):

| frame | what changes |
|---|---|
| f1893 | obj3 (the lead in loc 008, `$0197 = 06`) `$0F80` 80 → **00**, `$0C00`/`$0C01` 20/20 → **00/00** |
| f2298 | op 0xDD stages `$0100 = 0x0071`; the `FF 80` warp runs |
| f3229 | obj2 (the lead in loc 071, `$0197 = 04`) `$0F80` 80 → **00**, `$0C00`/`$0C01` → **00/00** |
| f3265–f3297 | `$0139 = 1` — the iris ramps open and reveals him as it grows |
| f3302 | `$0F80` → **33**, `$0C00`/`$0C01` → 30/30 — the script promotes him, **four frames after** the open completes |
| f3966 | `$0F80` → 22 |

The pictures agree with the bytes: `--peek` at f3250 and f3266 renders grass and **no Crono**; by f3272
his head is showing through the growing circle. And the census of scene 8 at the open gate is the *same
row* for every party to it — obj3 (the lead), obj27 and obj28 (the swirl pair) all read
`$0F80 = 00 / $0C00 = $0C01 = 00 / $1A81 = 01`. One mechanism, not a character rule.

**Why the distance test could not express it.** The arriving PC stands **38 px below** the iris centre
(foot y 142 against centre 103.5), failed the 31-px test, and was therefore drawn **unclipped** from the
moment loc 071 finished loading — visible for the ~36 frames before the gate opened (user: *"after crono
reappears in the other scene, he is being shown a little before the portal opens"*).

`drawSprite` now takes a `CtStencilSrc`, so it can be the occluder itself: a pixel the window did **not**
take from BG1/BG2 is restored from the map instead of plotted, and with a stencil source but **no**
window the whole sprite is stencilled — which is what an OBJ priority-0 object looks like in an ordinary
room, and exactly what the oracle draws before the gate opens.

**The trace.** `CT_ACTDBG` prints the lead's depth answer whenever it changes, so the round trip reads as
five transitions and nothing else:

```
[pcprio f1986] $0F80=00 occl=1 win=1 vis=1   he steps into the open gate, at priority 0
[pcprio f2366] $0F80=00 occl=1 win=1 vis=0   the closing iris takes him
[pcprio f3373] $0F80=00 occl=1 win=0 vis=0   he ARRIVES parked at priority 0, no window: invisible
[pcprio f3409] $0F80=00 occl=1 win=1 vis=1   the opening gate reveals him
[pcprio f3445] $0F80=33 occl=0 win=1 vis=1   the script's `8E 33` promotes him
```

The port's promotion lands **four frames** after its own open completes (f3441) — the same offset the ROM
has at f3302 against f3298, and nothing was fitted to it.

⚠ The actor-side occluder still has its own pixel loop (the `sten[i]` block in the draw list) while the
PC goes through `drawSprite`'s `CtStencilSrc`. They implement the same rule twice; unifying them means
carrying `CT_STENVIZ`/`CT_STENDX`/`CT_STENDY` into `drawSprite` and is open work.

## Repro / pins

```
ct.exe --teleport 157 52 10 0 --from saves/truce_market.state --out o157.state --frames 240
ct.exe --peek o157.state o157.raw --btnscript "0:60"     # read $0F80/$0C00/$0C01 per object
PEEK_ISOBG=0 ct.exe --peek o129.state o129_L0.raw --btnscript "0:60"    # 0=BG1 .. 3=BG4, 4=OBJ
ct.exe --teleport 240 7 16 0 --from saves/truce_market.state
python tools/dis.py 4C74 0x60 ; python tools/dis.py AD90 0xA0

# section 5 -- the gate, and the window that makes its priority-0 sprite visible
ct.exe --monitor "7E0139:1" --from saves/field_re/scene8_gate_cutscene.state \
       --btnscript "0:15,100:3,0:27,100:3,0:27,..." --diff        # $0139 = 1 for f1746..f1778
PEEK_ISOBG=1 ct.exe --peek saves/field_re/scene8_gate_cutscene.state o.raw --btnscript "<...to f1790>"

# section 6 -- the PC's own priority byte across the gate round trip
ct.exe --monitor "7E0F80:8,7E0C00:8,7E0139:1" --from saves/field_re/scene8_gate_cutscene.state \
       --btnscript "0:15,100:3,0:27,..." --diff          # $0197 names the lead; obj3 in 008, obj2 in 071
```
Regression checks: `field-scene157-fixed-sprite-prio` (fgskip 6 → 102),
`field-scene129-class7-action-probe` (fgpx 0 → 543134), `field-scene240-no-pc-loaded`,
`field-scene5-bottom-edge-draw-window` (9 NPCs, not 7), `vm-activation-pass-0F00`
(now including class 7), `field-scene8-gate-portal-draws-inside-window` (section 5: the occluder
census, two mutations), `field-gate-lead-pc-obeys-its-priority-byte` (section 6: the five transitions of
the round trip, two mutations).

## Open questions

- Our scene-92 render still shows the three LOADMON objects that the oracle keeps hidden
  (`$1A81 = 0`; all three fn0s end `... 91 / 00`). The oracle state for that comparison is a
  `--teleport` forced load, so the negative is not trustworthy yet — re-check from natural play.
- The actor-side occluder and the PC's both implement "stencil outside the window, draw inside" but in
  two different places (the `sten[i]` pixel loop in the draw list, and `drawSprite`'s `CtStencilSrc`).
  Unifying them means moving `CT_STENVIZ`/`CT_STENDX`/`CT_STENDY` into `drawSprite`; until then a change
  to the rule has to be made twice.
- The occluder's window arm is gated on the iris disc because the iris is the only BG1/BG2 window the
  port models. A second windowed effect (or a scene that windows BG1/BG2 for something else) would have
  to reuse this arm rather than add one — and there is no capture of such a scene yet.
- `footPrio` maps zb bit7 → the TOP half and bit6 → the BOTTOM half (the AD90 assignment).
  C0/4C8B assigns them the other way round. Only the AUTO arm is affected and no reported
  symptom depends on it, but the two arms should be measured against each other.
