# op 0xE0 — ChangeLocation-with-FADE (the "She's Princess Nadia!" loop root cause)

Session 2026-07-24. User report: scene 120, after Lucca's "She's Princess Nadia!" (str92) the
dialogue loops back to "Crono!" (str85) forever. Root cause: our op-0xE0 model (defer-commit-to-
RETURN on cell $7E0017) was derived from a single instance (scene-0 naming) and is NOT the ROM
mechanism. Fixed in ct_vm.c; validated by the full suite (126/0/3 pre-existing + new check) and a
live end-to-end Belthasar run of the whole 120→108→120 cutscene chain.

## 1. The ROM handler — C0/3AB7 (event ops run with D=$0100: DP $xx = WRAM $01xx)

```
3AB7 LDA $17 / BMI 3AEE          ; $0117 bit7 = "fade-change already pending" -> park path
3ABB TYX/INX, REP #20
3ABF LDA $7F2001,X / STA $D9     ; loc word -> $01D9 scratch
3AC5 AND #01FF / STA $12         ; staged loc          -> $0112
3ACA INX INX / LDA $7F2001,X / STA $14   ; tile x,y     -> $0114/$0115
3AD2 SEP #20
3AD4 LDA $DA / LSR / AND #0F / STA $16   ; facing = (hi>>1)&0x0F -> $0116
3ADB LDA $DA / AND #80 / TSB $16         ; hi bit7 OR'd into $0116
3AE1 LDA $17 / ORA #80 / STA $17         ; latch $0117 |= 0x80
3AE7 LDA #0F / STA $19                   ; arm the fade counter $0119 = 0x0F
3AEB TYX / CLC / RTS                     ; X restored to THE OP -> runner parks the ip HERE
3AEE TYX / CLC / RTS                     ; skip path (bit7 set): the same park
```

Contrast 0xDD (C0/3B1B): stages into $0100-family loader slots via DP $00/$02/$04 and returns
**SEC** (op completes). 0xE1 (C0/3B4B) stages into the SAME $12/$14/$16 cells then hard-reloads.
0xDE = 0xDD + `LDA #01 STA $1E` (C0/3B45).

## 2. The runner protocol — C0/5AA3-5AB8 (the budgeted visit loop)

```
5AA3 LDA $7F2001,X               ; op byte at ip
5AA7 TXY                         ; Y = op position (handlers TYX to re-park)
5AB1 JSR ($5D6E,X)               ; dispatch (op*2 into the C0/5D6E table)
5AB4 BCC 5ABA                    ; handler CLC -> VISIT ENDS, ip stored = X (5ABC TXA/STA $1180)
5AB6 DEC $67 / BNE 5AA3          ; handler SEC -> op complete, costs 1 budget, continue
```

So **CLC = park** (ip stays wherever the handler left X — for 0xE0: on the op itself) and
**SEC = complete**. An op-0xE0 therefore NEVER completes: fire once, then every revisit takes the
BMI skip path and parks again. That park IS the replay guard — the code after scene 120's E0
JUMPBACKs into the cutscene branch, and without the park the whole dialogue replays (the bug).

## 3. The commit — C0/0C8A (per field frame) + the load init C0/0B86

```
0C8A LDA $17 / BNE 0C8F / RTS    ; nothing pending
0C8F BPL 0CC7                    ; bit7 clear (other bits) -> the 0xDF/$18-family path
0C91 LDA $19 / BEQ 0C9A / BMI 0C9A
0C97 DEC $19 / RTS               ; fade-out: one DEC per frame
0C9A JSR C0/0B4E                 ; drained ->
0C9D LDX $00 / STX $05           ; remember the old loc word   $0100 -> $0105
0CA1 LDY $97 / LDA $1801,Y / STA $07     ; lead X byte -> $0107
0CA8 LDA $1881,Y / STA $08               ; lead Y byte -> $0108
0CAD LDA $1600,Y / EOR #01 / STA $09     ; lead facing^1 -> $0109
0CB4 LDX $12 / STX $00           ; staged trio -> loader slots $0100/$0102/$0104
0CB8 LDX $14 ... / BRL C0/000E   ; reload (writer PC C0:0CB8 observed on the $0100 flip)
```

The load init (C0/0B86+) copies $00/$02/$04 -> $0A/$0C/$0E and **C0/0B96 `STZ $17`** clears the
latch — the one-shot re-arms on every location load.

## 4. Oracle frame timeline (scene 120, state `s120_end.state`, monitor 11A2/0117/0119/0100)

```
f66   obj17 ip=body 0x06CD (WAIT 30 before the E0)
f126  E0 fires: $0117=80, $0112=006C, $0114=0000, $0116=01, ip PARKS on body 0x06CF; $0119=0F
f127-f141  $0119 0E..00 (one DEC/frame; ip pinned on the E0 the whole time)
f142  $0100 = 006C (scene 108 loaded, writer C0:0CB8); $0117 = 00 (C0/0B96)
f156-f170  $0119 counts 0->0F = scene 108's fade-IN
```

Commit = fire+16 frames. Our port: top-of-CtSceneTick drain (fire frame ends with $0119=0x0F,
first DEC next tick, locChange on the tick that finds 0) reproduces this exactly.

## 5. The scene-120 cutscene chain (Guardia Castle 600, "script 240")

- **loc[9]/loc[10] ($7F0212/$7F0214, word-indexed loc vars — op-0x12 handler C0/62DE does
  `idx ASL` before `LDA $7F0200,X`) are NOT script state: obj0's autorun continuation runs
  `22 00 09 0A` @body 0x0028 = GetPcCoords(PC0) -> loc[9]=tileX, loc[10]=tileY EVERY visit** — a
  live mirror of the lead's tile. Scene scripts use them as positional triggers (obj15 checks
  loc[9]==42 @0x057C; obj17 checks loc[10]==11 @0x0682).
- obj17 autorun @0x0648: spins while storyline<0x10; at storyline in [0x10,0x12):
  - glb[169] bit0 (mask 0x01, IFFLAG `16 A9 01 06 2C` @0x0652) **clear** + player tile row Y==11
    -> **branch B @0x0687**: str85 "Crono!" -> obj3(Lucca NPC) fn6 run-in -> str86..91 ->
    str92 "She's Princess Nadia!" -> `E0 6C 02 00 00` @0x06CF = fade-change to **loc 0x6C=108**.
  - bit0 **set** -> **branch A @0x0657**: `66 00 A9` clears bit0, str93/94, SETSTORYLINE=0x12,
    loc[11]=0, CALL obj3 fn15 = `D3 02` JOIN Lucca + LOADPC2 + E3 01.
- **scene 108** = the paradox flashback (table @0x1EE300 str65-68, the Leene lineup): its obj0
  autorun plays the 4 boxes, `65 00 A9` SETS glb[169].bit0, `E0 78 02 25 0A` fade-changes back to
  **loc 0x78=120 tile(37,10)** -> branch A runs -> Lucca joins. (Op-0x65 operand = bit NUMBER in
  the low 3 bits + 0x80 = addr+0x100; the IF op 0x16 operand is a MASK.)
- The vanish itself = scene 122 (`5A 10` SETSTORYLINE=16 @0x05AB, ends E3 01 + B2 — NO changeloc:
  the player walks out through the door and reaches the scene-120 corridor at tile row Y=11).

## 6. What our engine had wrong (ct_vm.c pre-fix)

1. Gate/latch on **$7E0017** — the handler's `$17` is DP under D=$0100 = **$7E0117**.
2. Fire path **advanced the ip** and let the visit continue — the autorun wrapped around and
   re-opened str85 (the visible loop) while the player stayed pinned at Y=11 by E3 00.
3. Commit deferred to the staging object's RETURN/B0/B2 — obj17's autorun never returns, so the
   staged change never fired. (The model came from scene 0's naming flow; under the canonical
   rules scene 0 works naturally: the newgame branch (glb[0x61]) bypasses the demo E0 @0x28
   entirely (JUMPFWD 0x19->0x34), names Crono (C8 @0xC0), then E0 @0xC2 -> loc 0x1F0 parks obj0.)

Fix: canonical stage+latch+arm+park in the op handler (cells $0112-$0119), the C0/0C8A drain at
the top of CtSceneTick (raises locChange at fire+16 and captures the $0105-$0109 return-memory),
memInit's zeroed s[] = the load-time `STZ $17`. lcDeferred/lcDeferredObj deleted.

## 7. Repro / validation commands

```
# arm an oracle state (fresh439 -> storyline 0x10, teleport to scene 120):
python  patch_state.py fresh439.state s120_pre.state 10000=10 100A9="&~01"
ct.exe --teleport 120 37 16 1 --from s120_pre.state --out s120_at.state       # loads scene 120
python  patch_state.py s120_at.state s120_go.state 10214=0B                   # fake "player at Y=11"
ct.exe --monitor "11A2:2,0117:1,0119:1,0100:2" --from s120_go.state --input "100:4,0:16,..." --diff
# ours, end-to-end (walk to Y=11, mash A through the whole chain):
build/Belthasar.exe --assets ct_assets.dat --scene 120 --loadwram s120_go.lakewram --selftest \
  --walkscript "u:95,a:2,i:14,..." --frames 4000       # prints changeloc 120->108, 108->120,
                                                       # storyline 16->18, explore back to 1
# pinned check: python tools/regress.py --only field-scene120-lucca-e0-fadechange
```

## Open questions

- `--vmdiff s120_end.state` (the 3-ops-before-E0 window) first-diverges at f67 on **obj1's op-B7
  ANIMEREPEAT(33 x2) completion timing** (ours still on the B7 at prio4; oracle already RETURNed
  to its B0 park) — a pre-existing anim-playback-rate residual (same with VMDIFF_ANIM=1), NOT the
  E0 mechanism (pre-fix the same window diverged at the replay itself). It shifts our E0 fire
  frame vs the oracle in this scene; the Belthasar end-to-end chain is otherwise correct.

- The exact ROM routine that (a) block-clears/writes the loc-var page on load and (b) whether any
  loader writes loc[9]/[10] directly (my teleport-time (37,16) reading is fully explained by
  obj0's op-0x22 mirror; no direct loader write was found — `STA $7F0212` absolute does not exist
  in bank C0).
- $0116 bit7 (E0/E1 operand hi bit7, TSB'd into the staged facing) — consumer unidentified; we
  stage the byte faithfully but lcStyle stays 0 for E0.
- The 0C8F BPL 0CC7 path ($17 low bits, set by the C0/3AA6/3BF8 latchers = the 0xDF family) — not
  yet derived; 0xDD/0xDE remain immediate-locChange stagings.

## 4. 0xDF / 0xE1 -- the non-returning pair (PORTED 2026-09-14)

```
3B98 LDA #01 / STA $1E / BRA ->3B4B          ; 0xDF = 0xE1 + the skip-ramp flag
3B4B INY / TYX / REP #20
3B4F LDA $7F2001,X / STA $D9 / AND #01FF / STA $12     ; staged loc  -> $0112/$0113
3B5A INX INX / LDA $7F2001,X / STA $14                 ; tile pair   -> $0114/$0115
3B62 SEP #20 / LDA $DA / LSR / AND #0F / STA $16 / LDA $DA / AND #80 / TSB $16
3B71 LDA $4210 / BPL ->3B71                            ; spin until the vblank's NMI flag
3B76 JSR C0/0B4E                                       ; SEI, INIDISP $80, NMITIMEN 0, HDMAEN 0
3B79 LDX $00 / STX $05 / LDX $02 / STX $07 / LDA $04 / STA $09   ; $0100..$0104 -> $0105..$0109
3B85 LDX $12 / STX $00 / LDX $14 / STX $02 / LDA $16 / STA $04   ; $0112..$0116 -> $0100..$0104
3B91 LDX #06FF / TXS / BRL ->0000                      ; the stack is thrown away: REBOOT
```

It never returns to the runner. The port used to run both as 0xDD (return SEC, keep the visit), and the
attract's hub (loc 1B1, obj0 fn0) is where that showed: state `$7F0062 == 4A` is
`56 4C 62 00 / D4 03 / D3 01 / DF 87 02 1C 08`, and the byte after the DF is state 4C's own test
`16 62 4C 00 11` -- which the fall-through then passed: `$62 = 50`, `E0 F3 03 00 00` staged 1F3 into $0112,
the load's commit (C0/0CB4) copied it to $0100 (loc 087 ran labelled 1F3) and the hub's next visit took
state 50 -> 1B0. The cartridge's chain is 087 -> 1B1 -> 1F3 -> 1B1 -> 1B0; the port skipped 65 M B.C.

Measured (`--monitor` over $0100/$0112/$0117/$011E/$7F0062 from saves/attract_entry3_handoff_f6750.state):
`$62 = 4C` at f325, `$0112 = 0087` + `$011E = 01` at f337, `$0100 = 0087` + `$0119 = 00` at f338.

**The spin's length is the one clock-dependent part.** $4210 bit 7 is set by the vblank and cleared by the
field NMI's own unconditional read (`C0/EA70`), so the loop exits only if the flag rises while the CPU is
between the LDA's opcode fetch and its read -- otherwise the NMI eats it and the loop waits another frame.
The line-211 IRQ (`C0/ECCC`) syncs to an H-blank (`LDA $4212 / BIT #40`) and then does a variable amount of
work, so the CPU's phase at the vblank is set by that work. The ROM forces a floor of ONE frame (this frame's
NMI has already read the flag); the port takes that floor, which is what the one measured site does.

Port: ct_vm.c stages, parks the visit and sets `rebootReq`; CtSceneTick does nothing from then on (the main
loop is dead); field.c 'THE DF/E1 REBOOT' makes the 3B79-3B8F stores on the next frame, before the tick, and
hands over to the shared C0/0000 reboot. The attract now reads 087 -> 1B1 -> 1F3 (ow) -> 1B1 -> 1B0 -> 1F0,
and 1B1 -> 1F3 takes 36 frames against the cartridge's 38.
- The demo-loop at C0/02A0 ($00F6/$00F0 = joypad-press exit -> $19 drain -> reload) is the
  title-attract frame loop, NOT the E0 consumer — noted to avoid re-deriving it wrong.
