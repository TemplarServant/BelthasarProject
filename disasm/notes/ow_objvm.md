# The overworld AMBIENT-OBJECT script VM (birds/smoke/boat) -- RE'd + ported + EXACT

Validated: `ct.exe <rom> --checkowobj saves/ow_w0.state` -> **40,740 fields over 300 frames,
0 mismatches, 3 bird deaths matched to the exact frame**. Port: world.c CtWorldAmbientFrame
(runs in place over the wram view; objects + the $7F:0400 script blob packed in T_WORLD fmt 2).
RE method: 5-agent workflow (4 decoders + adversarial script-walk verifier; 100% stream
coverage, 0 misalignments, 7 extra ops pinned by the walk).

## The VM (handler C2/0F63)
Object blocks 0x40 bytes at $0B30-$1B30 (the SAME blocks as the player/system objects; ~60
general slots from $0C30, 4 priority slots at $0B30). Dispatcher: op = blob[[B+$07]] (bank
[B+$09], the blob at $7F:0400+); JSR (C2:0F91 + op*2); returned A: 0 = yield (re-run next
frame), else advance A bytes; [B+$32] is ZEROED on every advance (one-shot gates live there).

## Opcode map (script-relevant subset; full table C2:0F91, 0x53 entries)
- 00 reset state (keeps +07/+09 script ptr + +0A wait) | 01/02 flags-subfield writes to +0F
- 04 spawn NATIVE sprite child (handler C2/1DD4; params -> B+32..37) -- smoke puffs etc
- 05 queue transition command ($0100 block + $027C=2) | 07 plot tile byte into map buffer
- 08 register sub-script ptr by area id ($1B3B/$1B3D/$1B3F/$1B41 vs $7E:2980-2982; id 7 = always)
- 09/43 spawn VM clone child (general/priority slots; MVN-inherits +05..+3F, new script ptr)
- 0A clear block byte | 0D set block byte | 0E |= mask | 0F &= ~mask | 10 zero abs WRAM | 11 inc abs
- 1A jump abs (operand = target-1!) | 1B DJNZ block byte, rel8 from the opcode byte
- 22/23/25 branch rel8 if WRAM byte ==0 / !=0 / ==imm | 26/27 if (byte & mask) !=0 / ==0
- 28 spawn NATIVE child (C2/20A2) | 2C teleport (posX,posY) | 2E/2F set 16.16 velX/velY
- 30 set anim sequence (B+0B = C3:E000[id*2], bank C3 -- the anim sub-VM bytecode)
- 31 n: MOVE+ANIMATE n frames (pos += vel 16.16 via C2/0F38, world wrap C2/1CB8 x<0x600
  y&=0x3FF, anim tick C2/0E1D, yield) -- THE bird integrator ('31 01' = 1 px-step per loop)
- 32 n: CAMERA PAN at the object's velocity for n frames (BG1/2/3 scroll shadows; flag-gated
  BG3) -- the coastal script's auto-pan (currently parked)
- 34 call native C2 routine: 78A1 = ON-SCREEN TEST -> $00 (margin +16px; view 288x272;
  pos - BG2HOFS/BG2VOFS, torus) -- the bird life gate; others (75xx/7Bxx) effects/etc
- 36/37 gosub/return (ONE slot, B+$05) | 38 n wait (no anim; n=0 parks) | 39 n wait + anim tick
  ('39 00' = parked forever; '39 01/1A' = bob-in-place loop -- the coastal boat & sparkle)
- 3F/40 fly to absolute X/Y: one-shot gate B+$32, auto-negate vel toward target (C2/1C84),
  auto-pick anim from 2 operand ids, integrate+wrap+anim per frame until pos == target EXACTLY
- 4C branch rel8 if WRAM byte < imm | 52 DIE (TDC/SEC -> the outer loop frees the block)

## The 1000 AD cast (snapshot in T_WORLD fmt 2)
5x bird A @0E81 ('31 01 / 34 A1 78 / 23 00 00 EB / 52': drift 1 step/frame while on-screen,
die when off), 2x bird B @0E8B (gosub'd wait loop), **2x coastal BOAT @0E04** (+0F=B4, parked
'39 01/1A' bob loop; a 32x32 2-frame ship, OBJ palette 2, tiles 20/22/40/42 <-> 2C/2E/4C/4E; one
docked at the Truce pier world (496,336), one south at (400,960) -- NOT smoke, that was a wrong
guess from the '39' park), **water SPARKLE @06BA** (+0F=B6, '39 00' park; a 16x16 twinkle, tiles
68/6A/6C -- NOT a boat, also a wrong early guess), west-edge @069E (frame counter loop INC $1BF7).
Registered fly-by scripts (0ECA/0F07/0F44/...) = per-region bird behaviors consumed by a native
spawner (UNPORTED -- birds deplete as they die; respawn system is the next layer).
The boat is RENDERED (field.c worldCompose, +0F==0xB4 -> CT_ASSET_WORLDSPR id 2, frame from the
+3E anim counter); sprite captured navigation-free by ct.exe --owboat (anim sub-VM bytecode in
bank C3 + resident OBJ VRAM). Sparkle/west-edge visuals still unported.

## Port approximations (positions/deaths are EXACT; visuals approximated)
- ~~The anim sub-VM (C2/0E1D, C3:E000 bytecode) -> a flap counter (B+$3E spare byte)~~
  **PORTED 2026-08-15.** The interpreter is C2/0E1D: build a long pointer from the object's own
  +0B/+0C (script) and +0D (bank), fetch the opcode, `JSR ($0E4A,X)`, and use the handler's RETURN
  VALUE as the byte-length to ADVANCE -- a returned **0 parks** the pointer on that opcode (that is
  how a frame is held), and the advanced pointer is written back to +0B on exit. The eight handlers:
  | op | addr | len | effect |
  |----|------|-----|--------|
  | 0 | 0E5A | 2 | object byte at [operand] := 0 |
  | 1 | 0E6B | 2 | object byte at [operand] += 1 |
  | 2 | 0E7E | 2 | object byte at [operand] -= 1 |
  | 3 | 0E91 | *signed operand* | relative JUMP (`BIT #$0080 / ORA #FF00`, then the caller's `CLC/ADC`) |
  | 4 | 0EA3 | 4 | SHOW FRAME: `+0F` bit6 = HOLD (park); else dwell `+0E` loads from operand[3] and parks, later visits DEC and park, the DEC that reaches 0 advances 4. Frame = the 16-bit param at +1 (staged by C2/0B53) |
  | 5 | 0EDC | 2 | WAIT: the same dwell protocol without staging a frame |
  | 6/7 | 0F00/0F35 | — | particle/child spawners; **not reached** by any sequence in this engine, so the port parks instead of guessing |
  Bytecode baked from ROM **$C3:E000..FFFF** (`tools/_gen_owanim.py`) -- the reachable span is
  C3:E14C..FF04 and the live gull/boat objects run scripts at **C3:F0F2 / C3:FA6D**, so a first cut
  that baked only E000..EFFF left them unreadable and the port parked forever. Sequences decode
  cleanly (e.g. `04 frame=E20E dwell=08 / 04 frame=E1FA dwell=08 / 00 zero B+26 / ... / 03 jmp-20`),
  and `+26` is the sequence's own flag around the up-stroke -- state the old counter never had.
  **GRADED**: `--checkowobj` now compares +0B/+0C/+0D/+0E/+26 (124740 fields -> 155925),
  **0 mismatches over 1000 frames**.
- **THE FRAME DESCRIPTOR FORMAT (derived 2026-08-15).** Op 4's 16-bit param is a pointer (bank =
  the object's +0D) to a descriptor. `C2/0B53` is only the ENQUEUE half -- it copies the long
  pointer into a 16-byte draw-list node ($7E:B000, 64 nodes; list head $7E:B500, free-list head
  $7E:B504) and Y-sorts it. The DECODER is **`C2/0D08`**, driven by the drain `C2/0CEA`:
  ```
  +0        u8    N = piece count            (0D12/0D14; 0E03 loops it)
  then N x 4 bytes                           (0DFB: CLC / LDA $55 / ADC #0004)
    +0      s8    dX -> added to the object X (0D82-0D92)
    +1      s8    dY -> added to the object Y (0DA2-0DB2)
    +2..3   u16   OAM word2 template          (0DBE-0DC5)
                  bits0-8  tile name, ADC'd with the object's tile base (+10 & $3FFF)
                  bits9-13 palette/priority -- ZERO in ROM data, ORA'd in from +0F (0D29-0D42)
                  bit14 H-flip, bit15 V-flip
  ```
  Total length = `1 + 4*N`. There is NO per-piece size field: 8x8 vs 16x16 is per-OBJECT (+10 bit14).
  Culling is per piece (screenX+$0F >= $010F, screenY+$0F >= $00EF) and a culled piece still
  advances the read pointer. Output goes to the OAM shadow **$7E:0700** (low table) / **$7E:0900**
  (high table, packed via the 16-byte table at C2/0E0D), DMA'd by C2/0184-01A3.
  Validated on real N>1 data: `C3:E32D` has N=4 (a 2x2 of 16x16 pieces, right column H-flipped),
  length 17, and the next script op starts at exactly `C3:E33E`; the 1-piece block C3:E1FA..E258
  is 19 back-to-back 5-byte descriptors.
  **Consumed by the port**: `CtWorldAnimFrame` (world.c) decodes piece 0 from the pointer the ported
  sub-VM parks on. The gull blit now anchors on the descriptor's own dX/dY -- the captured atlas
  carries a flat (-8,-8) hardcoded by the `--owbirds` writer, so gulls had been drawing up to ~24px
  too low (C3:E6AE -> dy = -32). Pinned by the `owframe:` lines of `ow-compose-render`.
  **The frame INDEX -- CLOSED by pixel identity (2026-08-15).** The atlas order is a CAPTURE
  artifact (`--owbirds` dedups by pixel hash in first-sighting order and writes `k,0,0`;
  `ow_gulls.spr` is additionally a hand-filtered 3-frame subset whose filtering step is unrecorded,
  and `saves/` is gitignored), so no ROM datum maps a descriptor to an atlas slot -- but the PIXELS
  do. `tools/_gen_owgullmap.py` decodes each tile the flap sequence names (`C3:E6A2` -> `0x1CA`,
  `0x1CC`, `0x1CE`) straight out of a live overworld state's VRAM with the gull's own palette
  (attr `0xBA` -> OBJ palette 5) and matches it byte-for-byte against the atlas, **asserting the
  match is unique** (one palette, one slot per tile), so a stale atlas or wrong state fails loudly
  instead of baking a guess. Result (`src/data/ow_gullmap.h`): `1CA -> #1`, `1CC -> #0`, `1CE -> #2`.
  The render now picks the slot of the tile the CURRENT descriptor names. The invented `WING[]`
  cycle's ORDER happened to be right; its CADENCE did not -- the ROM runs
  `1CA(4) -> 1CC(6) -> 1CA(4) -> 1CE(6)` with a **30-tick glide hold** every third mid, which the
  ported sub-VM's dwell reproduces. The `+3E` counter survives only as the fallback for a pointer
  not parked on a frame (mid-wait) or an unmapped tile. Pinned by the `slot=` field of the
  `owframe:` lines (`slot=-1` = unmapped, reported honestly).
  The capture RECIPE that was missing is now recorded in the generator's docstring.
- Native children (04/28) skipped -> no smoke puffs yet.
- op 05 no-op'd; op 32 consumes its timer without panning (parked script only).

## THE CANONICAL BIRD SYSTEM -- fully RE'd (supersedes the section below)
The deep investigation found the real thing: a SPAWNER VM OBJECT (parked at camY+256, the
0E46 loop) runs forever: 5 x { native 754D + native 7598 + op 09 spawn child + wait 12f }
then ~4x240f idle, repeat. The earlier 'no respawn' verdict was an artifact: children born
OFF-SCREEN die the SAME frame (78A1 -> 52), invisible to end-of-frame sampling.
- C2/754D: reposition the spawner to X = (camX & ~7) + 64 + rnd(0..127), Y = camY + 256
  (just below the view) -- the spawn point each bird inherits (op 09 MVN clone).
- C2/7598: launch velocity = ROM table C6:F640[rnd & 15] -- 16 vectors of 16.16 px/f, ALL
  NORTHWARD (0/30/45 deg, 1-2 px/f). 'All birds fly north' IS canonical; variety = the
  random angle/speed/start-x + bursts over time.
- RNG C2/2336: A = ROM[C0:FE00 + $1B30]; $1B30++ (a 256-byte table + free-running index).
- Port: obNative 754D/7598 + obRnd in world.c; the two ROM tables packed in T_WORLD fmt 3
  (staged at $7F:E000/$7F:E100 in the wram view). VALIDATED: --checkowobj over 1000 frames =
  124,740 fields, 0 mismatches -- including the spawner's RNG-driven re-aims (f794/830/842)
  and deaths. The viewer-side scatter/revive ambience was REMOVED (canon replaced it).
- The registered $1B3B-$1B41 scripts remain era-ARRIVAL flights (player mode-1 phase-0).

## (superseded) Respawn investigation (the 'all birds fly north and vanish' report)
EMPIRICAL VERDICT (--owspawnwatch, 30s idle + 30s walking): the ENGINE NEVER RESPAWNS birds.
The flock is spawned once by the world-load script; each bird drifts on its spawn velocity and
dies off-screen (78A1 -> op 52). The only runtime spawns are the 1-2-frame native smoke-puff
children (handler 1DD4, scripts 0E13/0E1F/0E2B/0E37, every ~10 frames from the smoke parent).
The registered area scripts ($1B3B-$1B41, consumed by the PLAYER's mode-1 phase-0 handler
C2/3772->3776, dispatch word at C2/3465) are ARRIVAL FLIGHTS (fly-to-target sequences for
era-gate landings), NOT bird respawns -- and they sit zeroed during normal play.
VIEWER AMBIENCE (field.c, deliberately not in the validated VM): each world entry re-seeds the
flock from the packed snapshot SCATTERED around the camera with varied 16.16 drifts
(0.25-0.625 px/f, all headings, xorshift seeded per world), and dead birds revive after a
90-330 frame cooldown as edge-entering flybys. CtWorldAmbientFrame itself stays byte-exact
(--checkowobj 40740/40740).

## The FADE objects (ops 0x28 / 0x29) -- PORTED 2026-09-14

Op 0x28 = C2/158B `LDX #20A2 / JSR C2/048A`, op 0x29 = C2/1599 `LDX #2105 / JSR C2/048A`, both `LDA #0002`. They allocate a
NATIVE object (C2/048A: first free block from $0C30, 60 of them; class word, +3/+4 and +2 zeroed, the parent's +05..+3F copied
by MVN). The child reads its RATE through the copied pc: `LDY $0007,X / STY $10 / LDA $0009,X / STA $12 / LDY #0001 /
LDA [$10],Y` -- the parent's +07 is the op's own address, because C2/0F86 stores the pc after every advance.

They are the ONLY thing that moves the overworld's brightness: the NMI (C2/015E, full arm when $4C bit 0) writes
`$2100 = $1C | $1D` at C2/031F and runs the object pass C2/051D at C2/0333 (a CLC handler gets +3 bumped at C2/054E, an SEC one
is freed). $1BF6 is their mutex: bit 1 = fade-out running, bit 0 = fade-in running.

* C2/20A2 fade OUT (table C2/20AB: 20AF, 20DF). State 0: bit 1 busy -> SEC; else set it, +2++, rate 0 -> `$1C = 80, $1D = 0`
  SEC (a cut); else +0A = rate, +3 = 0 and on into state 1. State 1: wait while +3 < rate; +3 = 0; DEC $1D; at 0 -> `$1C = 80`,
  clear bit 1, SEC.
* C2/2105 fade IN (table C2/210E: 2116, 2152, 216F, 218D). State 0: $1BF6 != 0 -> die; set bit 0; rate 0 -> `$1C = 0, $1D = 0F`,
  state 3 (a cut); else +0A = rate, state 1 (2 if $1D already != 0), +3 = 0. State 1: bit 1 -> die; wait; `$1C = 0`, INC $1D,
  state 2. State 2: bit 1 -> die; wait; INC $1D; at $0F -> die. State 3 / die = clear bit 0, SEC.

Who spawns them: EVERY world's bootstrap runs one `29 xx` (worlds 0-7 on an ordinary entry: `29 02`; world 0's cutscene arm
`29 06`; the attract's flyover `29 00`), the attract's legs end `28 01 / 38 12 / ... / 05`, the menu's two fades are the VM
scripts $C2:2EC1 `28 01 52` / $C2:2EC4 `29 01 52`, and the interactive location entry (main-loop state 3, C2/2402) spawns
$C2:2EC1 and waits 18 NMIs before writing $0100.

The world loader (C2/012F zero-fills $0000-$00EF, C2/0143 `$1C = 80`) runs with NMITIMEN = 0, so no pass happens in its window
and the world opens force-blanked until its own fade-in object lights it.

MEASURED. Forest hop into world 0 (`--monitor 7E0100:2,7E001C:2,7E1BF6:1 --from saves/forest_exit.state --btnscript
"0:10,20:380"`): $0100 = 1F0 at f28 with `$1C = 80 00` written at C2:0147; $1BF6 = 01 at f91 (C2:2120); $1D 01 at f93 (C2:216D),
then +1 every 2 frames (C2:2185) to 0F at f121, $1BF6 = 00. The attract's world 5 (from saves/attract_cena_inicial.state):
$0100 f67, first NMI f103, `29 00` -> `$1D = 0F` + $1BF6 = 01 at f106, 00 at f107; `28 01` at f319, $1D 0E..01 over f320-f333,
blank f334, op $05 at f337. The port: the same shapes to the frame, 2 frames later relative to $0100 (the loader's price for
world 5 is 39 frames against the cartridge's 36).
Truce market -> world 0 shows the first step THREE frames after $1BF6 = 01, not two: the NMI's `$4C` bit-0 arm skips the pass on
a frame the main loop has not released (the entry's song JSL); not modelled.

Port: world.c `case 0x28/0x29` + obFade; field.c holds the loader's whole window with no pass (`owLoading`), takes INIDISP
before the pass, lights nothing itself on entry, and leaves on op $05 at once (state 2 is `JML $C0:0000`).

THE LOCATION ENTRY (state 3) -- PORTED the same day. C2/35E5 `$027C = 3`; C2/2452 `LDA #C2 / LDX #2EC1 / JSR C2/04D9` (a VM
object on a BANK-$C2 script: C2/0F67 takes the bank from +09, so world.c's object VM now reads its scripts through the bank --
`obScript`, the ROM image lent by the host); C2/2461 `LDX #0012` NMI waits; C2/2470-24D2 = C2/1100's stores over `$7E:7E03,X`
WITHOUT C2/115E's `$027C = 2` (state 3 falls into state 2's code). C2/04D9's MVN copies from the block $4E names, which the pass
keeps (C2/053C `STX $4E`). MEASURED from saves/ow_w0.state walking onto the Truce door (`--btnscript
"40:72,20:8,40:8,0:22,100:2,0:80"`): $027C = 03 f111, $1BF6 = 02 f112, $1D 0E..01 f113-f126, blank f127, $0100..$0109 =
`0011 16 2C 04 01F0 2D 26 01` at f129, $027C still 03. The port's --worldtest: +1, +2..+15, +16, +18 from the press, identical
(check ow-location-entry-fade-vs-oracle). Still the host's: the menu's two fades (field.c menuPhase, modelled on these numbers),
and the record's `$1FF` arm (C2/2417, a free-roam re-arm) is not taken.

## The PALETTE ROWS (op 0x04, classes $1DD4 / $1E38) -- PORTED 2026-09-15

The overworld's colour animation (world 0's sea, world 3's rows 3 and 4, the 1000 A.D. attract white-out) is object code
writing a CGRAM SHADOW; nothing in the host rotates colours.

**Loader.** C2/000F runs `JSR C2/1DB5` before the loader: `STZ $0920,X` over $0920-$0B1F and `STZ $0B20,X` over $0B20-$0B2F
(16-bit). C2/2D2D (inside C2/2CC1) copies `$7F:8C00 -> $0920` ($201 bytes, `MVN $00,$7F`) and `$C6:F620 -> $0B00` ($21 bytes).
C2/2930 decompresses the palette-animation packet (`$C6:FEA0` table, id `[$ED+$0B]`, bit 7 = none) to `$7E:C000`. C2/23C8
`LDA #03 / TSB $4C`. (C2/2902, id `[$ED+7]` from `$C6:FE20` to `$7E:B800`, is the same family and is not ported.)

**NMI.** C2/0172 `LDA $4C / BIT #01` -- the full path only when the last pass completed; C2/01A6 `BIT #02` DMAs `$0920` (512
bytes) to CGRAM; C2/01CC `STZ $4C`; the object pass runs after; C2/033B `LDA #01 / TSB $4C`.

**Op 0x04** (C2/10CD, length 6): `04 lo hi bank row rate` -- the operands go into the SCRIPT'S block (+35/+36, +37, +32, STZ +33,
+34) and C2/048A allocates class $1DD4 with the MVN of +05..+3F.

**Class $1DD4** (two frames): state 0 -- a non-zero +34 allocates class $1E38 and dies (SEC); otherwise INC +2, INC `$0B20+row`,
WMADD = `$0920 + row*32`, 32 bytes from `[+35..+37]` through $2180, `TSB $4C #02`, CLC. State 1 -- DEC `$0B20+row`, SEC.

**Class $1E38** (the row fade, `JMP ($1E41,X)` 1E47/1E86/1EB5): state 0 stores +09 = rate, +0D = bank, +07 = row, +0B = source,
+0E = the shadow address, INC `$0B20+row`, and C2/1EC0/1F11 compute one descriptor per channel per colour into +10..+3F
(0 = equal, the rise when the target is above, `$80 | (fall + $1F)` below). State 1: if `$0B20+row != 1` another object took
the row -> state 2; else every `+03 >= +09` frames C2/1F85/1FCC steps each channel through the table `$C2:2062` (32 zeroes, 32
ones: a rising descriptor tests `T[d]` and counts up, a falling one `T[d & $3F]` and counts down), `TSB $4C #02`, and after 32
steps state 2. State 2: DEC `$0B20+row`, SEC.

**Scripts.** World 0 row 2: `04 00 C0 7E 02 00 / 0D 22 0A / 36 8B 0E` x4 over $C000/$C020/$C040/$C060 (a frame every ten).
World 3: row 3 over four frames and row 4 over three, a frame every eight. The attract's 1000 A.D. end: `04 E0 F5 C6 08 01`
(row 8 fades to `$C6:F5E0`) then rows 0-F set to it at once.

**Measured** (saves/attract_cena_inicial.state, `--monitor 7E0A20:32,7E0960:32,7E0980:32 --diff`): row 8 steps at f8826-f8844,
all rows white f8845. PORT (CT_OWPALTRACE): the same 20 row-8 values, each one uploaded at leg +215..+234 against the
cartridge's writes at +212..+231; world 3's row 3 the same four frames on the same 8-frame period. The port used to rotate
colours $24-$2E every ten frames in every world -- world 0's row 2 by luck, world 3's mountains by mistake (the attract survey's
"patches" on 1F3).

A trap found on the way: without C2/1DB5 the port's `$0B28` held $25 from the field, so the row-8 fade saw another owner and
quit on its first step.

## The picture is the NMI's latch -- the SCROLL too (2026-09-15)

The overworld NMI's full path writes BG1/BG2/BG3 HOFS/VOFS from the `$20..$2B` shadows at C2/01DA-0211, uploads the CGRAM
shadow at C2/01A6, pushes INIDISP at C2/031F, and only then runs the object pass (C2/0333) that moves the camera. So the
frame on screen shows the camera, the OAM, the palette and the brightness of the PREVIOUS pass, all four in step. field.c
already took INIDISP, OAM and (now) CGRAM at that point but drew BG with the camera the pass had just written, one pass ahead
of the rest. `World.camShow` / `posShowX/Y` are latched there and worldCompose draws with them. MEASURED (tools/
attract_bg_cmp.py): world 5's flyover port f2240 vs cartridge f206 56100 / 56320 (with the pass's camera the best frame was
f2239 at 55723, its palette rows a pass behind), world 7 port f4520 vs f3076 56267 / 57344 (was 55382). The "one scanline up"
(dy = -1) residual is not this: with the latch dy 0 still reads 39805.
