# M3-B: Overworld camera / scroll -- RE COMPLETE, ported, BYTE-EXACT

Validated: `ct.exe <rom> --checkowcam saves/ow_w0.state` -> **4500/4500 shadow words over 750
frames (R/D/L/U + 4 diagonals + idle), 0 mismatches** vs the live oracle. Port: `src/world.c`.
Demo: `build/ct_world.exe` (SDL; arrows pan the real 1000 AD map with the byte-exact camera).

## Scroll shadow map (NMI copy C2:01D8-0211, every frame)
Low WRAM (DP=0) -> PPU: $20/$21 BG1HOFS, $26/$27 BG1VOFS, $22/$23 BG2HOFS, $28/$29 BG2VOFS,
$24/$25 BG3HOFS, $2A/$2B BG3VOFS. Also $2C-$37 Mode7, $38-$40 windows, $41 TM, $42 TS,
$45 CGWSEL, $46 CGADSUB, $1C|$1D INIDISP, $4A HDMAEN.

## The lead object block ($4E -> $0D70 on the overworld; offsets verified live)
- B+$12/$16 = X/Y sub-pixel fraction words; B+$14/$18 = X/Y world pos in PIXELS ($0D84/$0D88)
- B+$1A/$1E = velocity fraction; B+$1C/$20 = velocity integer px/frame ($0D8C/$0D90)
- B+$22 = frame counter; B+$28 = facing/state (#$82/#$83 stored while walking)
- B+$2A/$2C = previous X/Y ($0D9A/$0D9C)
- Player world pos == exit-table dpos on entry (440,280); camera == pos-(128,112) always.

## Per-frame chain (all bank C2, RE'd via savestate + --owwatch writer PCs)
1. INPUT -> velocity: C2/3673 writes velX=+1 etc. (d-pad handler ~C2/3640: reads pad, sets
   facing B+$28, runs the terrain-collision chain JSR $3A56/$3A8F/$39D5/$39C3 -- UNPORTED, M3-C);
   velocity is TRANSIENT: consumed then cleared at C2/36C3 each frame.
2. INTEGRATOR C2/0F38-0F62: 32-bit pos += vel (frac with carry into px). Walk = 1 px/frame
   integer (frac stays 0 -- proven by the byte-exact camera fed with px deltas).
3. CAMERA APPLY C2/36F7 (X=[$4E]): $01=dX=[B+$1C]; $00=1,2 -> JSR $0568 (H dispatcher) for
   BG1,BG2; $01=dY=[B+$20]; $00=1,2 -> JSR $066C (V dispatcher); then $1BF1=dX, $1BF3=dY
   (the BG3 accumulators).
4. SCROLL DISPATCHERS: 0568 (H) / 066C (V): $0E=(BG-1)*2 (+6 for V); per-BG ptr triples
   defs=C2:0556{3000,3800,4000}, grid=C2:055C{4000,5800,7000}, VRAM SC=C2:0562{6000,6800,7000};
   $01 sign -> inc path C2/0607 / dec path C2/059D. Each 1-px step: INC; CMP #$0600; BCC; TDC
   (H wraps at 1536 = map width) / DEC; BPL; LDA #$05FF -- V wraps at 0x400=1024. **The
   overworld is a TORUS: no edge clamps.** Loops |$01| times.
5. STREAMING (hooks confirmed, content port = next milestone): on each 8-px boundary the step
   routine expands one tilemap column/row into the VRAM staging -- cursors $E3,X (wrap 0xC0=192
   8px-cols) / $E7,X (wrap 0x7F=128 rows), expander halves JSR $0773/$07C2 by ($20,X & 8), and
   JSR $090D with $08 = camX+256 (right edge enters) or camX-8 (left). Tilemap is 64x32
   (two 32x32 screens), SNES wrap CMP#$20/ADC#$3E0.
6. BG3 OCEAN ANIMATOR (own object, counter $0D12; C2/75CE-75FC): ctr++; (ctr&3)==0 -> BG3H--;
   (ctr&7)==0 -> BG3V++; then BG3H += $1BF1, BG3V += $1BF3, clear both. The animator object
   ticks BEFORE the camera apply => it consumes the PREVIOUS frame's deltas (1-frame lag --
   caught by the validator, modeled as pendDx/pendDy). BG3H also has a per-world ROM gate at
   $02891B (0 for world 0 = follow camera). Free-running 16-bit (no wrap).

## Headless access (the M3-A wall is gone)
`ct.exe <rom> --navworld saves/ow_w0.state` walks the oracle out of Crono's house onto the
overworld and savestates there (waypoints + dynamic lead-slot detection -- the slot CHANGES per
scene: idx02 bedroom, idx04 downstairs -- + TALK mode for Mom's allowance event, which
physically blocks the stair corridor and parks the player off-map during the cutscene).
`--from <chk>` resumes from the auto-saved downstairs checkpoint. `--peek <state> <out>`
dumps fb+wram+vram. `--owwatch/--owprobe` = writer-PC watch / per-direction WRAM dumps.

## M3-C: the PLAYER MOVER -- RE COMPLETE, ported, BYTE-EXACT END-TO-END
Validated: `ct.exe <rom> --checkowmove saves/ow_w0.state` -> **11700/11700 values over 900
frames (12 phases incl. blocked dirs, reversals, diagonals, idle), 0 mismatches**: the C chain
input -> mover (pos/vel/probe/phase + terrain collision) -> camera (6 shadows) vs the oracle.
RE'd via trace-from-savestate (`--trace N --from <state>` + tools/disasm65816.py).

- Object loop C2/0553 dispatches per-object handlers each frame: lead player = C2/3404
  (gates $7E2980 sprite-state, $027E/$027F + $0280/$0281 latches, $027C==1 controls-enabled),
  2D dispatch ($345D,X) by $0280*8 + phase([B+$02])*2. Other handlers: 75C3=BG3 ocean,
  0F63 (x9 objects: birds etc), 5628/3264/309E/2F94 = system objects.
- **8-PX QUANTIZED STEPPER**: phase 1 (at boundary) C2/3567: input = the 16-bit word $F8
  (d-pad bits Up=0x0800 Down=0x0400 Left=0x0200 Right=0x0100; low byte 0x80/0x40/0x01/0x04
  = UI buttons -> menus/interact). No input -> idle (clear moving bit $28.7, idle timer
  $02B1++, idle anim C2/3987). CARDINAL PRIORITY Up > Down > Left > Right -- **NO diagonals
  on foot** (verified live: DR/DL move straight down). Setter (Right=C2/3670, Left=364F,
  Down=~3636, Up=~361B): vel int [B+$1C]/[B+$20] = +-1, other axis 0, facing $28=0x80|dir,
  walk anim C2/3953 (facing*2 + step-parity [B+$26]&1 -> table C2:397F).
- PROBE C2/3A56: probe[B+$2A/$2C] = pos +- 8 per nonzero vel axis; wrap C2/3A8F (X by
  compare-1536, Y by AND 0x3FF). PROPS C2/39D5: cx=probeX>>3, cy=probeY>>3; the 2x2 corner
  cells (cx-1,cy-1),(cx,cy-1),(cx-1,cy),(cx,cy) each -> nibble via **C2/62AE**: block =
  BG2grid[$7E5800 + (row>>1)*96 + (col>>1)]; byte = $7E7000[block*2 + (row&1)] (per-block
  props from desc[0x12], 256 blocks x 2 bytes); nibble = high if col even else low ->
  [B+$22..$25]. VERDICT C2/39C3: 16-bit [B+$22] AND #$0303 -> blocked iff
  ((propUL|propUR) & 3) != 0 (only the TOP pair on foot).
- Blocked C2/36B6: clear moving bit + all 4 vel words (the 36C3 STZ), re-read props at POS,
  idle anim, RTS (stays phase 1; held d-pad retries every frame = the set@3673/clear@36C3
  owwatch pattern). Free C2/36E7: STZ $1BF7, phase++ -> 2, fall into the integrate.
- Phase 2 (traveling) entry = C2/36ED, runs EVERY frame (no input read mid-step!):
  integrate C2/0F38 (32-bit pos += vel; fractions stay 0 on foot), anim tick C2/1CB8,
  camera apply (0568 H x BG1,BG2 / 066C V x2), $1BF1/$1BF3 = vel, sprite C2/0E1D,
  $0283/$0285 = pos copy, C2/3ACB: $0102/$0103 = pos>>3 (HUD cell). ARRIVAL (pos==probe,
  C2/3747-3757): step-parity toggle (JSR 3934), phase := 1, **velocity KEPT** (seamless
  continuation while held), $0282 run-bits via 39AE.
- **The auto-joypad is PIPELINED 1 frame**: the $F8 the mover reads in frame N was latched
  the previous vblank (validator feeds prevHeld; same gotcha as the field CtReadJoypad RE).
- Port: src/world.c CtWorldMoverSeed/CtWorldMoverFrame/CtWorldProp. ct_world.exe now WALKS
  the player (arrows, real terrain collision, camera follows; C toggles free-cam).
- The other prop reader C2/62ED ($01*0x30 indexing = 48 bytes/row over $7E7200, desc[0x13])
  = the location/exit trigger map -- M3-D.

## TILEMAP STREAMING -- RE COMPLETE, ported, BYTE-EXACT
Validated: `ct.exe <rom> --checkowstream saves/ow_w0.state` -> **3,280,000 words over 800
frames (8 phases), 0 mismatches**: BOTH full 64x32 tilemaps (VRAM $6000 BG1 / $6800 BG2,
2048 words each) compared EVERY frame vs the oracle, plus scrolls. Port: src/world.c
CtWorldStreamSeed/CtWorldStreamFrame. ct_world.exe now composes the view PPU-style from the
STREAMED tilemaps (per-pixel char fetch + palette + flips) -- the final selftest view FNV is
IDENTICAL to the old whole-map prerender crop after an 11-crossing walk.

- Scroll-step loops stream one tile column/row per 8px crossing: H-inc C2/0607 (stream block
  0625), H-dec 059D (cursor pre-dec at old&7==0 @05B1, stream at new&7==0 @05D1), V-inc 070B
  (0729), V-dec 06A1 (06D5). Cursors $E3/$E5 (8px col, wrap 0xC0=192) / $E7/$E9 (row, wrap
  0x80=128) per BG.
- Stream targets: H-inc col = (E3+0x20)%192 (the right edge, pixX=scroll+0x100); H-dec col =
  (E3-1)%192 (left edge, pixX=scroll-8); V-inc row = (E7+0x1C)%128 (bottom, pixY=scroll+0xE0);
  V-dec row = (E7-1)%128 (top, pixY=scroll-8). Start row/col of the strip = (E7-1)>>1 /
  (E3-1)>>1 metatiles.
- WRITERS (per metatile half, alternating chips by sub-row/col parity seeded from the cursor):
  column left C2/0773 (chips +0 TL / +4 BL), right C2/07C2 (+2/+6); row top C2/0843 (+0/+2),
  bottom C2/088F (+4/+6). V-dec INVERTS the half pick (scroll&8==0 -> bottom). Grid walk:
  block = wram[grid + mcol + mrow*0x60], row advance +0x60 wraps at 0x1800 -> 0 (TDC), col
  advance wraps 0x60 -> 0. 30 words per column / 34 per row.
- STAGING RING $7E:F000 ($E0 pointer, +0x40/column, +0x80/row, reset to $F000 every NMI =
  a per-frame bump allocator). Words land at ring offsets ((scrollV-8)/8*2 + k*2) & 0x3F
  (column) / ((scrollH-8)/8*2 + k*2) & 0x7F (row) -- i.e. slot offset == tilemap row/col mod
  32/64. The DMA uploads the WHOLE 64/128-byte slot, so slot bytes NOT freshly written carry
  the ring's previous contents into VRAM (deterministic; the C port keeps the ring image).
- DMA QUEUE at $0060+ (8-byte entries: src,bank=7E,vramWordAddr,len=0x40,mode), queued via
  C2/090D (column: dst = SC + (pixX/8)%64 with +0x3E0 screen-2 adjust, VMAIN 0x81 = +32 col
  mode) / C2/0961 (row: dst = SC + (pixY&0xF8)<<2, TWO entries -- screen 0 then +0x400 --
  VMAIN 0x80). **The queue is flushed by the NEXT frame's NMI** (uploads queued in frame k
  appear in VRAM at the start of k+1) -- modeled as a 1-frame pending queue; staging is
  untouched in between so the data survives.
- The water ANIMATION rewrites char data (vram ~$1F20 region) on its own cycle -- a separate
  system (chip-anim style), untouched by the streamer.

## M3-D: LOCATION ENTRY -- RE COMPLETE, ported, BYTE-EXACT
Validated: `ct.exe <rom> --checkowentry saves/ow_w0.state` -> **253 values, 0 mismatches**:
the $1B32 record latch tracked every frame of a walk crossing several entries, then the A-press
transition matched all 7 load vars. ct_world.exe shows a banner on entries and A reports the
entry (selftest walks to the Truce-market door and 'enters' location 0x011).

- ENTRY RECORDS: 7 bytes at $7E:7E00 (count $1B38; from desc[0x15]->C6:FFC0; 80 records in
  1000 AD = every enterable location): [0]=cell col|0x80(active) [1]=cell row (METATILE
  coords) [2]=name id [3]/[4]=dest word (&0x1FF = location index; 0x1FF = world-link e.g.
  the ferry; >>9 -> $0104 facing/flags) [5]/[6]=dest field tile X/Y. Multiple records can
  share a dest (multi-cell doors). Two 3-byte sibling tables at $7E:8300/$7E:8180 (counts
  $1B39/$1B3A -> latches $1B34/$1B36) = vehicle/other triggers (unparsed).
- DETECTION (all in the mover's PHASE-1 frames only; everything LATCHES between boundaries):
  the phase-1 head (C2/3550) re-reads the prop corners at POS into [B+$22]; C2/39AE tests
  the ENTRY BIT: carry iff BOTH top corners have prop-nibble bit 2 (AND #$0404 == 0x0404).
  On entry: TSB $0282|=3 (also at arrival C2/375F; TRB #$02 on non-entry arrivals). The
  scanner object (handler C2/3264, every frame): cell word = (posY>>4)<<8 | posX>>4 from
  the $0283/$0285 pos copy (C2/3286); C2/32CD scans records for (rec&0x3F7F)==cell ->
  $1B32 = offset; if $0282 bit1 clear -> $1B32 forced 0xFFFF (C2/32A6).
- ENTERING: A in phase-1 with $1B32 valid (C2/35E5: $F8 low bit 0x80) -> $027C=3 -> the
  loader writes $0100=dest, $0102/$0103=dest tile, $0104=flags, and the RETURN point:
  $0105=old loc (0x1F0), $0107=(BG2 Hcursor $E5 + 0x10) % 0xC0, $0108=($E9+0x10)&0x7F
  (= the player's 8px cell). X button -> $027C=5 (menu), 0x0004 -> 6.
- The A-press paths C2/30DA (gate $0294 bit7, pos $0290/$0292, table C2:324C) and C2/3159
  ($029E/$029A/$029C, C2:325C) = VEHICLE boarding (Epoch/boat), not locations.
- The region-name HUD is separate: trigger map $7E:7200 (48 B/row x 64, nibble per metatile,
  reader C2/62ED row*0x30+col/2) -> nibble -> $1B9B[16] -> name id $02AE (region banner).
- Port: world.c CtWorldEntryScan/CtWorldEntryDecode (+ the latch rule in ct_world/validator).

## FIELD -> OVERWORLD RETURN -- RE COMPLETE, ported, BYTE-EXACT
Validated: `ct.exe <rom> --checkowreturn saves/truce_market.state` -> **1212 values, 0
mismatches**: walking out of Truce market, the landing (pos/camera/cursors/scrolls/phase/vel)
matches CtWorldEnter's predictions from the field-exit cell alone, and the C mover+camera
seeded ONLY from that cell drive a 200-frame d-pad sweep byte-exact (incl. BG3).

- A field 'world' exit (dest 0x1F0+world in the scene-exit table) jumps to the overworld with
  $0100 = 0x1F0+world, $0102/$0103 = the arrival cell = the exit record's dpos>>3 (8px),
  $0104 = facing. The C2 loader places the player at cell*8, camera = pos-(128,112) (torus),
  stream cursors $E3-$EA = camera/8, scrolls = camera; velocity 0, phase 1.
- $0105/$0107/$0108 are the symmetric "where I came from" record, re-saved on EVERY
  transition (field side: source loc + tile; overworld load C2/2475: loc + cursors+0x10 =
  the player cell) -- also mirrored to $04FC/$04FE/$04FF (the save-game data).
- On the overworld $0102/$0103 double as the per-frame pos>>3 tracker (C2/3ACB).
- Port: world.c CtWorldEnter(cam, mover, cellX, cellY).
- Truce market (loc 0x011) exit: rect (336,736)+(32,16) -> world 0 dpos (368,296) f3.
## PACKED INTO ct_assets.dat + SEA PALETTE CYCLING
- CT_ASSET_WORLD (type 17, ~50KB): grids/defs/palette/char/props/trigger map/names/entry
  records + spawn. pack_world (extract_assets.py) reads the headless dumps
  saves/ow_w0.live.wram/.vram (--navworld + --peek), so the pipeline stays ROM-reproducible.
  ct_world --assets = NO ROM, NO savestate (synthetic wram/vram views + CtWorldTilemapBuild,
  the loader's fresh 64x32 expansion) -- selftest FNV identical to the live-dump mode.
- WATER = PALETTE CYCLING (the vram ~$1F20 writes were bird OBJ gfx, NOT water): every 10
  frames CGRAM[24..27] and [28..2B] rotate LEFT one slot, [2D]<->[2E] swap; CGRAM[20] flashes
  0x49B5 one tick in four (colour 0 = transparent on hardware -> backdrop+HDMA; not composed).

## CROSS-ENGINE DONE: the overworld lives INSIDE Belthasar
`Belthasar --assets ct_assets.dat --worldtest --selftest` = the full native cycle headless:
Crono's house (field) -> front-door WORLD exit (kind 0 in CT_ASSET_EXITS) -> the overworld
engine (mover/camera/streaming/cycling/entry latch) at the exit's world pixels -> walk to the
Truce-market door -> talk = ENTER location 0x011 at tile (22,44) (native field load at
destX*16+8/destY*16+15, flags carried both ways) -> out the south exit -> BACK on the
overworld at dpos (368,296). PASS. Interactive: walk out of any door, roam the map, enter
locations with Space/Z.

## Open (M3 continuation)
- Vehicles (Epoch/boat: A-paths C2/30DA/3159 with gates $0294/$029E bit7 + parking pos
  $0290/$029A; the 3-byte tables $7E:8300/$8180; the ferry = entry record #0, dest 0x1FF
  world-link, INACTIVE at game start -- not validatable this early; speeds unknown).
- BG3 COMPOSE DONE (the clouds): desc[0x14] -> VRAM $7800 is the BG3 TILEMAP (64x32; the
  earlier 'second char half' guess was wrong -- 4096B = exactly 2048 map words). BG3 = 2bpp
  char at $7000 (desc[0x08]); palette = CGRAM pal*4+idx; tilemap PRIORITY bit (0x2000) =
  CLOUD tiles -> the TOPMOST layer in mode 1 + BG3prio (394/2048 entries in w0); prio-0 =
  the ocean texture UNDER BG1/BG2 (visible through transparent deep-sea metatiles). Packed
  in T_WORLD fmt 4 (6KB: char+tilemap); field.c worldCompose layers BG1 > BG2 > BG3 >
  backdrop + a final BG3-prio cloud pass above sprites/player. The bg3h/bg3v scroll (drift
  -1px/4f H, +1px/8f V + camera follow) was already byte-exact -- the clouds drift for free.
- CLOUD TRANSPARENCY: CGADSUB ($46) = 0x44 = colour math on BG3, HALF-ADD vs the sub screen
  (TS=$13 = BG1+BG2+OBJ; CGWSEL=$02). The cloud pass blends (cloud + under)/2 -- the sub
  screen under a cloud is exactly the composed BG1/BG2/sprites. The prio-0 ocean texture
  also has math (vs the COLDATA fixed colour = the per-scanline HDMA blue gradient) -- still
  approximated full-bright pending the HDMA table RE.
- **RETRACTED + CLOSED 2026-08-15 (measured, `build/ct.exe --hdma saves/ow_w0.state`).** There is
  NO per-scanline HDMA gradient. The probe traces every visible scanline of a whole frame and the
  colour-math registers never change: `CGADSUB = 0x44` (HALF, ADD, enabled for **BG3 only**),
  `CGWSEL = 0x02` (operand = the SUB screen, `TM=$17`, `TS=$13` = BG1+BG2+OBJ), fixed colour
  `$2132 = (0,0,0)`, and **no HDMA channel is enabled** (the channels are programmed -- ch4 `$212C`,
  ch5 `$2126`, ch6 `$2131`, tables at `$7F:10DC/1133/118A` -- but `HDMAEN` is clear). Two things
  follow, both now implemented in `worldCompose`:
  * the prio-0 ocean texture IS mathed, and since it is only visible where BG1 AND BG2 are
    transparent, the sub screen there is its backdrop = the fixed colour = black, so the half-add
    reduces to **the BG3 colour halved**. It used to render full-bright.
  * the half-add is in **5-BIT space**, not 8-bit. Of the 5022 mathed pixels in the oracle's own
    composed frame (`saves/ow_w0.state.fb.raw` decoded against its CGRAM), **3931 are reachable
    only by `conv5((a5+b5)>>1)` and not one requires the 8-bit average** the cloud pass was doing.
  Pinned by `ow-compose-render` (fnv over the composed view).
- Still open: `ct_world` (standalone) does not compose BG3 yet.
- An overworld Crono sprite (the marker box stands in; the world sprite set is its own
  format), the region-name HUD text, menus.
