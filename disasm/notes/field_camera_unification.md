# FIELD-CAMERA UNIFICATION (tier b) — the ONE camera: complete derivation

The ROM has exactly ONE field camera: the DP=$1D00 scroll state (`$1D0A/$1D0E` coarse + `$1D93/$1D96`
phase) IS the rendered scroll, maintained per frame by the player-controller chain `C0/881E` and read by
battle staging, the op-0xE7 seek, and the PPU. This dossier derives the COMPLETE law — variables, per-frame
pipeline, scene-entry init, the D8 align machine, every scripted mover, the closed writer list, an oracle
validation corpus, and the migration spec — so the engine can retire its second (player-follow render)
camera. ROM + oracle only, zero invention. HEAD e50593c.

Method: `tools/dis.py` (fileoffset=(bank−0xC0)<<16|addr; ⚠ the linear decoder desyncs across data — the
event-op cluster and `7506` needed explicit `m=1`; absolute-store claims were closed with a raw byte-scan
of the whole ROM for `8D/9C/EE/CE/9D/8E/8C xx 1D`). Oracle: `ct.exe --read/--monitor --diff --writer`
(⚠ `--monitor "ADDR:2"` prints bytes in ADDRESS order; writer PC = the retire PC, usually store+2/+3).
States: `gato_press_action.state`, `saves/base_loc6.state`, `scene439_marle.state`, `fresh439.state`,
`saves/truce_market.state`, `saves/crononame.state`, + 4 minted teleports (repro in PART F).
Builds on: `battle_entry_camera.md` (op-0xE7, arena origin), `battle_entry_cam_triage.md` (two-camera
defect), memory `ct-field-camera-scroll-model` (C0/8833 chain skeleton).

---

## PART A — THE VARIABLE MAP (DP=$1D00; all addresses $7E1Dxx)

| var | meaning | proven by |
|---|---|---|
| `$1D0A` (16-bit) | camera LEFT edge, **8-px units** | every trace; battle staging reads `>>1`=tiles |
| `$1D0C` (16-bit) | camera RIGHT edge = `$1D0A + 0x20` (+256 px) | all 5 states; init `7528/754E`; INC/DEC in lockstep |
| `$1D0E` (16-bit) | camera TOP edge, 8-px units | every trace |
| `$1D10` (16-bit) | camera BOTTOM edge = `$1D0E + 0x1C` (+224 px) | all 5 states; init `7569/759B` |
| `$1D93` | X PHASE = camPxX mod 16 (0..15) | B1/B2 traces; GATO 13 = 29 mod 16 |
| `$1D96` | Y PHASE = camPxY mod 16 | GATO: coarse 3, phase 13 → camPxY 29 = 164(lead)−135(onscreen) |
| **camPx** | **`camPxX = $1D0A*8 + ($1D93 & 7)`**, same Y | the identity every trace satisfies; THE render law |
| `$1D87/$1D89` (16-bit, signed) | X/Y **step counters**, ±1 per committed pixel, **zeroed at scene entry** (NOT absolute position, NOT sub-pixel) | GATO $1D89=0xFFFD=−3 (camY 32→29 since entry); fresh states 0 |
| `$1D3B/$1D3C` | BG1 X/Y **fraction accumulators**, 4.4 fixed (whole px extracted each frame, remainder kept sign-extended) | `9238/92CA-931E` |
| `$1D2E/$1D30` | this frame's scroll REQUEST X/Y, 1/16-px units (±0x10 walk, ±0x20 run) | `88E5/88EE/91AC` |
| `$1D2C/$1D2D` | player intent X/Y (same units; feeds anim/collision) | `8902`-table handlers |
| `$1D32/$1D33` | player APPLIED velocity (post map-bound gate) | `9175`; `--playermove` |
| `$1D2A/$1D2B` | base request seeded into $2E/$30 each frame (`88E5`) | **no writer exists in the ROM** (full byte-scan) — always 0; vestigial |
| `$1D1A/$1D1B/$1D1C/$1D1D` | scroll-mask bounds, 16-px TILES: X min/max, Y min/max | loader; = field.c mx0/mx1/my0/my1 ÷16 |
| `$1D00/$1D01/$1D02/$1D03` | op-0xE7 seek: state (0/1/2) / axis-arrived bits / target camX / target camY (8-px units, operand<<1) | `3E30/91AC`; battle_entry_camera.md |
| `$1D24/$1D25` | BG1(+camera) per-frame extra delta X,Y (byte pair), 1/16-px | op-0xE6 `3E18` |
| `$1D26/$27, $1D28/$29` | same for BG2, BG3 | op-0xE6 `3E21/3E2A` |
| `$1D4E/$1D4F` | auto-scroll per-frame delta X,Y (1/16-px), derived from `$0400` while `$01BC`!=0 | `91FD-9236`; cleared by op-0xF4 arg 0 |
| `$01BC` | auto-scroll mode enable (op-0xF4 operand) | `3D84-3D96` |
| `$0138` | camera ALIGN machine state (0 off / 1 choose / 2 run) — `885A` | `1770` arms at D8 |
| `$1D42/$41/$44/$43` | this frame's whole-px step counts R/L/D/U (extracted from $3B/$3C) | `92CA-931E`, consumed `944B/947D/94AD/94DF` |
| `$1D76/$77/$74/$75` | "coarse moved" flags R/L/D/U → tilemap streamer | `93E1` head, `944B`-family |
| `$1D37/$38, $1D39/$3A` | BG2, BG3 OWN velocity X,Y (parallax) | `9252-92C8`; op-0x29 zeroes BG3's |
| `$1D36` | layer decouple bits (bit1 BG2, bit2 BG3 = ignore camera) | scene header via `A36D` (`$7EB501` bit5) |
| `$1D3D/$3E, $1D3F/$40` | BG2, BG3 fraction accumulators (as $3B/$3C) | `9252-92C8, 9320+` |
| `$1D8B/$8D, $1D8F/$91` | BG2, BG3 step counters (as $87/$89) | `74E8/74F7`; op-0x2C writes BG3's ×8 |
| `$1D12/$14/$16/$18` | alt-map-mode ($0BC9 bit7) streamer window regs | `74B1-74C5` |
| `$0A00,X / $0A80,X` | per-object ON-SCREEN X/Y = world − camPx — the deadzone witness | every trace |

The battle staging law (`battle_entry_camera.md` A.1) reads `oTX=$1D0A>>1`, `camPx=$1D0A*8` — coarse only.
That is EXACT at D8 **because the D8 align machine guarantees phase 0** (PART C.3): the fine component is
always 0 when a battle stages.

## PART B — THE PER-FRAME PIPELINE (player-controller chain C0/881E, DP=$1D00)

Called per frame from the field tick (callers: `00A7/00EB/02B7/02DE/0CDB/2830/2854/3FC3`). Order is
load-bearing: **collision clamps the request BEFORE the camera consumes it; the deadzone/edge gate kills
only the input request, so the op-0xE7 seek and op-0xE6/auto deltas BYPASS deadzone and edges.**

```
881E  $01EB=#80; DP=$1D00
882C  JSR 88E5:  $2E=$2A, $30=$2B                          ; base request (both always 0 — no $2A writer exists)
882F  $2C=$2D=0                                            ; intent
8833  if $0162==0 && $011F!=0:  JSR 88EE                   ; input request (explore only; $0162 = demo-input override)
8840  if $0120!=0:              JSR 8A6D                   ; player collision — CLAMPS $2C/$2D/$2E/$30 (wall ⇒ 0)
884C  JSR 9175                                             ; player velocity $32/$33 = $2E/$30, gated on the LEAD's map bounds
884F  JSR 99DE                                             ; CAMERA GATE: deadzone + camera-edge kill of $2E/$30
8852  JSR 91AC                                             ; op-0xE7 seek override + auto-scroll $4E/$4F derive
8855  JSR 93E1                                             ; accumulate + extract + COMMIT (all layers)
```

**B.1 Input request `88EE`** — skipped while `$0138`!=0 (align machine owns the camera). Direction nibble
`$00F9 & 0x0F` (bit0=Right, bit1=Left, bit2=Down, bit3=Up), jump table `$8902` (16 LE words; invalid
combos → RTS `8924`):
Right `8925`, Left `8944`, Up `8963`, Down `8982`, DR `89A1`, DL `89D2`, UR `8A07`, UL `8A2C`.
Each: run test `$00F8 bit1` → step `0x20` (2 px) else `0x10` (1 px); sets intent `$2C/$2D = ±step` and
**ADDS** `±step` into `$2E/$30`. Diagonals write BOTH axes at full step (no normalisation).

**B.2 Player velocity gate `9175`** (not the camera's clamp — the LEAD's): `$32=$33=0`; X=lead idx `$0197`.
`$2E<0` → `5B86`: blocked iff `$1D1A >= $1801,X` (lead tile ≤ left bound); `$2E>0` → `5B79`: blocked iff
`$1801,X >= $1D1B`. `$30<0` → `5B71`: blocked iff `$1D1C+1 >= $1881,X`; `$30>0` → `5B63`: blocked iff
`$1881,X >= $1D1D+1`. Unblocked → `$32=$2E / $33=$30`. (`$1801,X` = world px>>4 = tile.) Already ported
(`--playermove` 6-field byte-exact).

**B.3 Camera gate `99DE`** — kills the CAMERA's copy of the request (`STZ $2E`/`$30`), per axis, via
px = `|req|>>4` into the edge/deadzone helpers. **Centre = (128, 136)** on the LEAD's on-screen foot
(`$0A00,X`, `$0A80,X`), one-sided:

| helper | axis/dir | camera-edge tile test | at-edge-tile (==) test | deadzone (inside) test — KILL when |
|---|---|---|---|---|
| `9A60` | X right | `$1D0C>>1` vs `$1D1B` (> ⇒ kill) | kill iff `$1D93+px >= 16` | onscreen X **<= 128** |
| `9A7E` | X left | `$1D0A>>1` vs `$1D1A` (< ⇒ kill) | kill iff `$1D93−px < 0` | onscreen X **>= 128** |
| `9A1F` | Y down | `$1D10>>1` vs `$1D1D` (> ⇒ kill) | kill iff `$1D96+px >= 16` | onscreen Y **<= 136** |
| `9A3D` | Y up | `$1D0E>>1` vs `$1D1C` (< ⇒ kill) | kill iff `$1D96−px < 0` | onscreen Y **>= 136** |

Consequences (all oracle-verified, PART F): right-scroll needs onscreen ≥ 129, left ≤ 127, down ≥ 137,
up ≤ 135; walking locksteps and FREEZES the current offset (no pull to centre); a left/up walk from
centre engages at 127/135 and pins there; scroll-reachable maxima are `mx1−257` / `my1−225`
(edge-tile + phase 15), minima `mx0` / `my0` (phase 0). A camera placed PAST those (entry/seek can) simply
never moves further out. At run (px=2) the last odd pixel before a far pin is unreachable (`14+2>=16`).

**B.4 Seek + auto-scroll `91AC`** — op-0xE7 servo, runs INSTEAD of nothing (overwrites `$2E/$30` AFTER the
gate — so seeks ignore deadzone AND edges):
```
$1D00!=0:  X: $1D02 vs $1D0A:  > → $2E=+0x10 ; < → $2E=−0x10
                               == → if ($1D87 & 0x0F)!=0 → $2E=−0x10  else TSB $1D01 bit0   ; arrive = coarse==target AND 16-aligned
           Y: $1D03 vs $1D0E — same with $1D89, TSB bit1
           $1D01==3 → $1D00=2 (complete), $1D01=0
```
(The alignment test works on the step counters because entry zeroes them at a 16-aligned camera; the E7
targets are always even (operand<<1) hence 16-aligned.) Pace emerges as EXACTLY 1 px/frame.
Then `91F8`: if `$01BC`!=0, derive `$4E/$4F` from the `$0400` bitfield (bit2→X 16/frame=1px, bit1→Y,
bit0→negate both; zeroed per-axis when the coarse var is 0 = parked at origin `922C-9236`). If `$01BC`==0
the block is skipped and `$4E/$4F` persist as-is (op-0xF4 arg 0 clears them).

**B.5 Accumulate + extract + commit `93E1`:**
```
9238  $3B += $2E + $24 + $4E          ; BG1 X fraction (+= request + op-E6 delta + auto delta), 8-bit signed wrap
9245  $3C += $30 + $25 + $4F          ; BG1 Y
9252  BG2: $3D/$3E += ($36 bit1 ? own only : $2E+…) + $37/$38 + $26/$27 (+$4E/$4F)
928E  BG3: $3F/$40 += ($36 bit2 ? own only : $2E+…) + $39/$3A + $28/$29 (+$4E/$4F)
92CA  $42 = ($3B>0? $3B>>4 :0), $41 = (−$3B)>>4 ; $3B &= 0x0F (negatives: |x|>>4 and remainder ORA #F0)
92F5+ $3C → $44/$43 ; $3D → $46/$45 ; $3E → $48/$47 ; $3F/$40 → BG3 counts (9320-93E0)
93CC  $4D (op-E6 duration) != 0 → DEC ; reaching 0 → STZ $24..$29 (the drift stops) ; RTS 93E0
93E1  $76=$77=$74=$75=0 ; JSR 944B($42 X+) 947D($41 X−) 94DF($43 Y−) 94AD($44 Y+) ; then BG2/BG3
      committers (950F/9545/9579/95AF normal vs 95E3/9619/964D/9683 when $0BC9 bit7) ; then streamer
      row/col stagers (96B7/96ED/9721/975F, 97xx-98xx) ; BRL 7F9A
```
The BG1 committers, per whole pixel (loops `$66` times = the step count):
```
944B (X+): $87++ (16-bit) ; old=$93: ==0x0F → INC $0A,INC $0C,$76=1,A=0xFF ; ==0x07 → INC $0A,$0C,$76=1,A=7 ; $93=A+1
947D (X−): $87−− ;          old=$93: ==0    → DEC $0C,DEC $0A,$77=1,A=0x10 ; ==0x08 → DEC $0C,$0A,$77=1,A=8 ; $93=A−1
94AD (Y+): $89++ ; $96 crossings 0x0F/0x07 → INC $0E,INC $10, $74=1
94DF (Y−): $89−− ; $96 crossings 0   /0x08 → DEC $0E,DEC $10, $75=1
```
i.e. phase counts 0..15 within a 16-px tile; the coarse 8-px twins bump TWICE per tile (at 7→8 and
15→0 crossings) — which is precisely why `camPx = coarse*8 + (phase&7)` and why an aligned camera has
phase 0 with even coarse. The `$74-$77` flags drive the VRAM tilemap streamers (`EB91` consumer, `8345`
family) — presentation-side, irrelevant to our compositor. No STA $210D exists in C0; the PPU scroll write
is NMI/DMA plumbing downstream of these vars.

## PART C — SCENE ENTRY, THE ALIGN MACHINE, AND THE D8 HANDOFF

**C.1 Scene-entry init `C0/7506` (X) / `C0/7553` (Y)** — called from `74A6` in the loader path (also
`0A50/2611` sequences; the whole $1D page is block-cleared just before — untracked writer, MVN/fill —
zeroing phases, fractions and step counters). Input = the ARRIVAL TILE `$0102/$0103` (the ChangeLocation
operands), NOT the lead pixel:
```
X (7506):  arrTileX <= $1D1A+8      → $1D0A = $1D1A*2                 (left pin;  BEQ 7512→7522 and BCS-fail both)
           arrTileX >  $1D1B−8      → $1D0C = ($1D1B+1)*2 ; $1D0A = $1D0C−0x20   (right pin, 753E)
           else                     → $1D0A = (arrTileX−8)*2          (752E)
           $1D0C = $1D0A+0x20 (unless pin-right set it first)
Y (7553):  arrTileY <= $1D1C+8      → $1D0E = $1D1C*2                 (top pin)
           arrTileY >  $1D1D−6      → $1D10 = ($1D1D+1)*2 ; $1D0E = $1D10−0x1C   (bottom pin, 758B)
           else                     → $1D0E = (arrTileY−8)*2          (757B)
           $1D10 = $1D0E+0x1C
```
In pixels: **camX = clamp((arrTileX−8)*16, mx0, mx1−256); camY = clamp((arrTileY−8)*16, my0, my1−224)** —
always 16-aligned (phase 0), step counters 0. Since the spawn foot is (tile*16+8, tile*16+15), this equals
`clamp(floor16(leadX−128), …) × clamp(floor16(leadY−136), …)` — the deadzone-centre law, tile-quantized.
NOTE the far pins are `mx1−256`/`my1−224` — ONE PIXEL past the scroll-reachable maxima (`−257`/`−225`):
an entry camera can rest 1 px outside what walking can reach (scene-439 camY=144 vs max 143 — B3a
oracle-verified: it holds there, down-scroll dead, up-scroll normal). 8/8 value samples + the live door
writers (`C0:7534/7568` = normal-branch retires; `C0:7549` = right-pin retire on the naming→intro load)
+ `74AC-74C5`: `$0BC9` bit7 (alt map mode) additionally derives the `$12/$14/$16/$18` streamer window.

**C.2 The ALIGN machine `C0/885A` (state `$0138`)** — a self-contained DP=$1D00 camera driver:
```
state 1 (choose): $2E=$30=$32=$33=0 ; X=$0197
                  $1D93!=0 → $2E = (onscreenX>=128 ? +0x10 : −0x10)
                  $1D96!=0 → $30 = (onscreenY>=136 ? +0x10 : −0x10)
                  both phases already 0 → $0138=0 (done) ; else JSR 91AC+93E1 ; $0138=2
state 2 (run):    $32=$33=0 ; $1D93==0 → $2E=0 ; $1D96==0 → $30=0
                  both zeroed → $0138=0 ; else JSR 91AC+93E1        ($2E/$30 persist from state 1)
```
= glide the camera 1 px/frame per axis TOWARD THE LEAD's side until BOTH phases are 0 (camera on a 16-px
boundary), ≤15 px per axis. `88EE` is disabled while it runs (input can't fight it).

**C.3 The D8 (battle-entry) arming `C0/1770`**: when `$0117 bit5` (the D8 intro flag) is set: clear it,
**`$0138=1`, then a BLOCKING loop `{ JSR 885A ; JSR 00BF ; until $0138==0 ; JSR EC60 (frame) }`** — the
whole game loop waits until the camera is tile-aligned. Then the intro-style camera (`$0145`-switch,
`17A0-1888` DP=$1D00 blocks — styles 1+ un-RE'd, both corpus fights style 0). **This is why battle staging
may read `$1D0A*8` as camPx**: the align machine has forced phase 0. In both corpus fights the op-0xE7
seek already left the camera aligned, so the D8 align is a no-op — but it is the structural guarantee
(a battle triggered mid-phase WOULD glide up to 15 px here, then stage).

## PART D — SCRIPTED CAMERA OPS (the complete catalog; dispatch table at C0/5D6E, handler = word[base+2*op])

| op | handler | effect |
|---|---|---|
| **0xE7** | `3E30` | camera SEEK to (`op1<<1`,`op2<<1`) 8-px units; `$011F=0`; blocks until arrival (B.4). Bypasses deadzone+edges; target always 16-aligned. |
| **0xE6** | `3DF4` | SCROLL LAYERS: word=(dxlo=X,dhi=Y byte pair 1/16-px/frame), mask byte, byte→`$1D4D`; mask bit0 → `$1D24/25`=pair (BG1 = THE CAMERA, continuous drift), bit1 → `$1D26/27` (BG2), bit2 → `$1D28/29` (BG3). The DURATION byte `$1D4D` is consumed at **C0/93CC-93DE**, the tail of B.5's extraction: `LDA $4D / BEQ ->93E0 / DEC $4D / BNE ->93E0 / STZ $24..$29` -- zero = drift until re-set, N = drift N frames then stop all three layers. Oracle: loc 112 `E6 00 20 07 B0` ($1D4D = B0 at f2473, $1D24..29 zeroed and $1D0E = 36 at f2649); scene 285 `$10` frames. PORTED (camCommit93E1). |
| **0xF4** | `3D84` | AUTO-SCROLL mode: `$01BC=op`; op==0 also zeroes `$1D4E/$4F`. While armed, `91FD` derives `$4E/$4F` (±1px/f) from the `$0400` bitfield each frame, parking per-axis at coarse 0. (`$0400` source un-located — no field-bank writer; unexercised in corpus. OPEN.) |
| **0x29** | `3E84` | BG3 DECOUPLE + reload: `$1D36|=4, $1D35|=4`, `$1D39/$3A=0`, re-seed BG3 map (`6E27`+`75A0` MVN ladder), operand → BG3 anim table select (`$0BE9/$0BE1`, FD/A000 defs). |
| **0x2C** | `3EE6` | BG3 POSITION: `$1D3F/$40=0, $1D92=0`; `$1D8F=op1*8`, `$1D91=op2*8` (BG3 step counters = px). |
| 0x2A/0x2B/0x32 | `3ED6/3EDE/3EE2` | `TSB $0154` bits 4/8/16 (layer-disable family — not camera position). |
| 0x2F | `3F12` | 2 bytes → `$0BE3/$0BE4` (BG3 anim params). |
| 0xF8/F9/FA | `3E5C/3E6E/3E75` | `JSL C2/8004` service #6/#7 (both/one) — scene service calls, not camera. |
| 0xE4 | `3D97` | 3 words + byte → `$013E-$0145` incl. the battle intro STYLE `$0145` — battle-entry params, not live camera. |

Non-writers confirmed for completeness: op-0xE5 = `3DC5` (copy enqueue; C0/1770 aligns the camera with 885A, copies
and holds the main loop in its style branch -- field_tile_copy_e5.md), op-0x88 = `4892` (color). The new-game "1000 A.D. pan" is loc 0x1F0 =
**overworld** (probe: naming → f53 field-load right-pin (writer `C0:7549`) → f90 loc=0x01F0) — the OW
camera system, already byte-exact; NOT a field-camera item.

## PART E — WRITER ENUMERATION (CLOSED)

Full-ROM byte-scan (`8D/9C/EE/CE/9D/8E/8C` + operand ∈ camera set) + oracle `--writer` across the corpus:

- **Commit machine** (DP-relative, `944B/947D/94AD/94DF`): observed retires `C0:9457/9478/945F/9461/946D/946F`
  (X+), `C0:9484-94A6 family` (X−), `C0:94B9/94DA/94C1/94C3` (Y+), `C0:94E6-9508 family` (Y−).
- **Seek arm/clear** (`3E48/3E51/3E54/3E38`): retires `C0:3E4B/3E54/3E57/3E3B`.
- **Entry init** (`7506/7553`): retires `C0:7534/7549/7568` (+pin variants `751E/752A/753E-7547`, `756B/757F/758F-7595/759D`).
- **Align machine** `885A` internal `$0138` writes (`8870/88A1/88BF/88D8`); armed ONLY by `1770` (`STA $38`, DP=$0100).
- **Event ops** (absolute): `3D8E/3D91` ($4E/$4F op-F4-clear), `3E18/3E21/3E2A` ($24/$26/$28 op-E6),
  `3E8D/3E90` ($39/$3A op-29), `3EFC/3F0A` ($8F/$91 op-2C).
- **Scene-load page clear**: one untracked block write (MVN/fill) zeroing the $1D page pre-init (oracle f97).
- **Battle module repurpose** (battlePhase only): `CC/E305 STA $1D00,X` (observed CC:E308/E952),
  `CC/B23F/B82F INC $1D02,X`, `CC/EC1E`, `C1/2C86/2C9C/5631/5645 STA $1D0C,X/$1D23,X`.
- Banks C5+ hits = data false-positives (no instruction boundary). **No other writer of
  $1D0A/$0C/$0E/$10/$87/$89/$93/$96/$3B/$3C exists.** `$1D2A/$2B` have NO writer anywhere → constant 0.

## PART F — VALIDATION CORPUS (oracle traces; the acceptance set for the port)

All commands from the repo root; exe = a scratch copy of `build/ct.exe`. Teleport states minted via
`--teleport <scene> <tx> <ty> 0 --from scene439_marle.state --frames 90 --out <f>.state`.

| # | scenario | command core | expected (byte-exact) |
|---|---|---|---|
| 1 | walk RIGHT lockstep (439) | `--monitor "1D0A:2,1D0C:1,1D93:1,1D87:2,1802:2,0A02:1" --from scene439_marle.state --input "80:40"` | camX 256→…: `$1D87` +1/f (C0:9457), phase +1 (C0:9478), coarse 0x20→0x21@f8 (7→8), →0x22@f16 (F→0); onscreen FROZEN 136 |
| 2 | walk LEFT deadzone crossing | same, `--input "40:30"` | f1-f9 cam frozen (onscreen 136→128 still blocked), engages when onscreen=127 (f10), locksteps pinned 127; DEC coarse @0→F (f10) and @8→7 (f18) |
| 3 | DOWN at the entry overhang | `"1D0E:2,1D96:1,1D89:2,1882:2,0A82:1" --from scene439_marle.state --input "20:12"` | camY=144 (my1−224): ZERO camera motion (edge-tile>bound kill) while the lead descends |
| 4 | UP crossing | same, `--input "10:26"` | blocked until onscreen 135 (f11), then lockstep pinned 135; DEC @0→F f12, @8→7 f20 |
| 5 | RUN right | `"…,00F8:2,…" --input "81:14"` | `$00F8` bit1 set; +2px/f; phase 0,2,4,6,8(coarse++),… — 7-crossing handled inside the 2-step loop |
| 6 | normal DOWN lockstep | from `t439mid` (cam(256,0)), `--input "20:30"` | immediate (onscreen 143>136), +1/f, pin 143, coarse++ @f8 |
| 7 | WALL stop | from `t439mid`, `--input "40:290"` | locksteps at 127 until the lead hits the plaza wall (world X 359) — then camera + lead FREEZE dead (collision zeroed the shared request), input still held |
| 8 | entry law (8 samples) | `--read` on fresh439/scene439_marle/truce_market + t439corner/t439mid/t2bed/t5fair + door | `(arrTile−(8,8))*16` clamped: (256,48) (256,144) (256,544 both-pin) (512,144 far-pin) (256,0) (256,16 near-pin) (208,384) (256,304 door f128 writers C0:7534/7568); phases+counters 0 |
| 9 | GATO op-0xE7 fine | `"1D00:1,1D02:2,1D0E:2,1D96:1,1D89:2,1D10:2" --from gato_press_action.state --input "100:8,0:290"` | REST = coarse 3 phase 13 = **camY 29** (not 24); arm f182 (targets 0,4); +1px f183/f184 (94B9/94DA); f185 phase F→0, coarse 3→4 + $1D10 (94C1/94C3) = **32, aligned**; f186 $1D00→0 (3E3B); f290+ CC:E308/E952 repurpose |
| 10 | forest entry + E7 | battle_entry_camera.md A.3 (cite) | held-LEFT drives camX 3→0 through THIS pipeline (d-pad request + deadzone); E7 camY 60→68 coarse = 480→544 px @1px/f |
| 11 | knockdown fall-pan | memory ct-field-camera-scroll-model (cite; byte-exact 3-way) | lead fn3-frozen: LEFT pans 260→229 (input path cares only about onscreen), RIGHT/idle hold 260 |
| 12 | new-game naming→intro | `--from saves/crononame.state --input "8:2,0:40,8:2,0:40,8:2,0:600"` | f53 field-load with RIGHT-pin entry (`$1D0A=0x60` = mx1−256, writer C0:7549); f90 loc=0x01F0 = OVERWORLD (pan out of field scope) |

## PART G — MIGRATION SPEC (render reads ONE camera; the VM maintains it)

**G.1 ct_vm.c — replace `CtSceneCameraSeekTick` with the full `CtSceneCameraTick`** (still first in
`CtSceneTick`, so every host drives it). All state lives in `mem.s[$1D..]` (already snapshotted by
F5/F9 CtScene states → camera persistence falls out for free; `camSeekFineX/Y` DIE — struct-size change
invalidates old debug .states, the CTFS sizeof guard rejects them cleanly). Per frame, in order:
1. If `$0138`!=0: run the C.2 align machine (states 1/2) INSTEAD of 2-4 (input skipped), then step 5-6.
2. request: `$2E=$2F=0` base (the $2A/$2B pair is constant 0 — port as nothing); if
   `$0162==0 && $011F!=0 && $0138==0`: dir-nibble → ±0x10/±0x20 (run bit) per B.1, diagonals both axes.
   **The request MUST be the post-collision value** (corpus #7): reuse the ported CtPlayerCollide clamp
   output — whatever zeroes the player's applied walk must zero this request identically.
3. camera gate per B.3: needs lead onscreen = leadWorld − camPx (compute from the SAME vars; centres
   128/136 — retiring the py−112 approximation), the edge-tile tests on the twins, the phase tests.
4. seek override per B.4 (op-0xE7 arm handler stays; delete its private fine counters — arrival =
   coarse==target && step-counter&0x0F==0).
5. accumulate `$3B/$3C += $2E/$30 + $24/$25 + $4E/$4F` (8-bit signed), extract whole px → step counts,
   remainder kept.
6. commit loops per B.5 (step counters, phase 0-15, coarse twins bumped at 7/15 crossings both directions).
7. op handlers to add (IP lengths already correct in opLen): 0xE6 (store $4D/$24-$29 per mask),
   0xF4 ($01BC, zero $4E/$4F on 0), 0x29/0x2C (BG3 — may stay visual-stubs but STORE the vars),
   auto-scroll $01BC/$0400 block ported dormant (no known field writer of $0400; flag if ever seen live).
8. scene load (CtSceneCreate + the locChange commit): zero the camera vars, then the C.1 entry law from
   the arrival tile; set both twins. (`--scene`/`--loadwram` seeding: take the oracle's live values as-is.)
9. battle D8: when the engine fires D8, set `$0138=1` and run the align machine TO COMPLETION before
   staging reads the camera (the ROM blocks; staging's coarse-only math requires phase 0).

**G.2 field.c — the render camera becomes a READ:**
`camx = s[$1D0A]*8 + (s[$1D93]&7); camy = s[$1D0E]*8 + (s[$1D96]&7)` every frame, every mode.
DELETE: stateful `camX/camLeadPrevX` + deadzone code, `camy=py−112`, the >32px re-centre heuristic, the
"follow scripted lead 1:1" branch (**invention — the ROM camera NEVER follows a scripted lead move**;
corpus #9: the D9 walk pans nothing; scenes reframe via op-0xE7/E6/entry), `arenaCamState`/`entryCam*`/
`battleCam*` latches (battlePhase holds naturally: CtSceneTick is battle-gated so the vars freeze at the
D8 origin; the CC-bank repurpose is oracle-side only — our mem.s never sees it), the menu save/restore
`mSave.camX` (mem.s carries it). The mask clamps (`mx0/mx1−CAMW−1`) stop being render-side clamps — they
are emergent from the gate; keep only the fb-crop safety clamp.

**G.3 Checks to re-derive/re-pin** (the Y recentre 112→136 and the entry law shift baselines):
`--checkfieldcam` (rewrite its model to this law; add multi-screen + deadzone-crossing scenarios from
PART F), `camera-reenter-scene5` (entry (216,400) → law gives (208,…): re-pin), the tier-(a) glide checks
`field-battle-forest-seek-glide` (render now starts at the TRUE camera — oracle-matching frames; re-pin),
GATO/forest battletest fieldshot fnvs, knockdown 3-way (should become byte-exact INCLUDING Y), every
selftest capture whose fnv embeds a camera position. `CT_PANSHOT` (overworld) unaffected. The staging/
anchors (`$1D0A`-based) are already unified — no change. Regression order: port VM tick → `--vmdiff`
(camera vars now diffable directly — add them to a watch) → field render swap → re-pin the listed checks
→ full suite.

**G.4 Risks** (ranked):
1. The request-source adapter (G.1.2): if our collision clamp diverges from `8A6D` in any corner, the
   camera inherits it (wall-creep or missed scroll). Mitigation: corpus #7 + `--playermove` accX/accY.
2. On-screen-coordinate parity for the deadzone (needs `$0A00/$0A80` semantics — `--checkxform` ported;
   use the same rounding path, not a float recompute).
3. Baseline churn: the Y-centre fix moves tall-scene framing up to 24 px — many pinned fnvs shift at once
   (expected, one-time; each re-pin must be oracle-diffed, not eyeballed).
4. Scripted-follow deletion: any scene that TODAY looks right only because of the invented 1:1 follow will
   reveal a missing op-0xE6/E7 effect — that is the CORRECT failure mode (fix = the op, not a heuristic).
5. Seed states: `--loadwram`/battletest seeds carry live oracle camera vars — our tick must accept
   mid-phase, mid-seek states (no assumption of alignment outside entry/D8).
6. `$1D4D` consumer and the `$0400` auto-scroll source are OPEN — dormant unless a scene arms them
   (none in the 512-scene corpus observed so far); log-and-flag if `$01BC`!=0 ever appears.

## PART H — CORRECTIONS TO PRIOR NOTES (carry into any future edits)

- `battle_entry_camera.md`: the GATO pre-E7 resting camera is **(0, 29)** (coarse 3 + fine 5), not 24; the
  E7 pan is **3 real px over 3 frames** (29→32), not "one 8px step"; "~1 unit/8f (~1px/f)" → exactly
  1 px/frame, the coarse view aliases it. "$1D87=7" in the old scene-13 capture = the PHASE&7 (camX
  175 = 21*8+7), not a separate fine var. The "writer C0:94C1 (STA $0E-family)" = the INC-family retire.
- memory `ct-field-camera-scroll-model`: "Y center (oracle 136)" CONFIRMED (foot-line, one-sided, 9A1F/9A3D);
  "no-input → follow scripted lead 1:1" and the ">32px jump re-centres" are INVENTIONS to retire (real
  mechanisms: entry init C.1 + ops). The far-clamp `mx1−CAMW−1` is now derived (edge-tile phase kill), and
  entry may legally sit at `mx1−CAMW` (one past it).
- The op-0x32 here (field table, `3EE2`, $0154 TSB) is unrelated to the OVERWORLD ambient op-0x32
  (C2/164B) — different VMs.

## Repro artifacts (scratchpad, NOT committed)
`ct.exe` copy; minted states `t439corner/t439mid/t2bed/t5fair.state`; dis dumps `dis_c0.txt/dis_c2.txt`
(linear — desyncs over data; re-dis locally with explicit m/x before citing); byte-scan one-liners in the
session log. All monitor commands inline in PART F.
