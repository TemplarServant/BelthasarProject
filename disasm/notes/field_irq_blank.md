# The field's V-IRQ force blank: scanlines 211..223 are black on hardware

Derived 2026-09-03 from the ROM + the oracle only (defect #1 of the 2026-09-02 list: "in some scenes
there is 1 scanline left over at the bottom (like in the tent of horrors)"). Every claim cites
`python tools/dis.py <fileoffset>` (bank C0 = the file offset; D1/FE97 = 0x11FE97) or an oracle dump.

## 1. The measurement that started it

The tent of horrors is scene 434 (scene 5's exit at (688,464) -> dest 434, tile (8,11) facing up).
`--teleport 434 8 11 0 --from <the user's scene_7 state>` + `--peek ... --frames 30` gives a 256x240
framebuffer whose non-black rows are **8..218**: the SNES's 224 lines sit at rows 8..231 (LakeSnes
`ppu_putPixels`: line y -> row 8 + y), so lines **211..223 are black**. The same 211-line span comes
back from every field state tried (the fair, party3's field, the scene-8 gate cutscene) and from a
battle state (battle592); the overworld (ow_w0) and the menu program (menu_open) show all 224.

The port drew the map, the sprites and the window into those 13 lines. Row-aligned against the
oracle (port row y = oracle row y + 8) the tent differed on rows 211..223 with all 256 pixels each.

## 2. The mechanism

```
0B3C  XCE / LDA #81 / STA $4200 / REP #20 / LDA #00D3 / STA $4209 / SEP #20 / CLI / RTS
```
The field's interrupt setup: NMITIMEN = $81 (NMI + auto-joypad) and **VTIME = $D3 = 211**. The NMI
handler C0/EA63 re-arms the V-IRQ every frame:

```
EA70  LDA $4210 / LDA #00 / STA $420C / LDA #A1 / STA $4200      ; HDMA off, NMI + V-IRQ + joypad
...
EC4E  LDA $0119 / BNE ->EC55 / LDA #80 / EC55 STA $2100          ; INIDISP := brightness (or force blank)
```

The IRQ handler C0/ECCC (the $7E0504 trampoline's target):

```
ECD5  LDA $00010F / BMI ->ED0C            ; $010F bit 7 set -> no blank this frame (0 in every state seen)
ECDB  LDA $004211 / BPL ->ED0C            ; TIMEUP: only a real V-IRQ
ECE1  LDA #81 / STA $004200               ; disarm the IRQ until the next NMI
ECEB  LDA $4212 / BIT #40 / BEQ ->ECEB    ; wait for line 211's H-blank
ECF2  LDA #80 / STA $2100                 ; FORCE BLANK
ECF7  LDA #00 / STA $420C                 ; HDMA off
ECFC  LDA $0153 / AND #01 / BNE ->ED08 / JSR C0/ED15 / BRA ->ED0C / ED08 JSL FD:FFFD   ; the blanked-time work
```

So from line 211's H-blank to the next vblank the screen is force-blanked: lines 212..224 (1-based; 0-based
211..223) show black, and the ROM spends that time on VRAM work. The battle installs its own IRQ,
D1/FE97, which does the same thing (FEA8 `LDA $004211`, FEAC `LDA #81 / STA $004200`, FEB8-FEBD the
H-blank wait, FEBF-FEC1 `LDA #80 / STA $2100`); VTIME is never rewritten in the game proper (the only
other writer is FD/EB87 `STZ $4209`, the $FD sequences).

## 3. The port

`src/field.c`, at the end of the frame's composition and before the overlays that replace the buffer
(the $FD sequences, the loader screen): when the frame is a field or battle frame (not the overworld,
not the C2 program's screens -- naming, the ATB selector, the menu) and `$010F` bit 7 is clear, rows
211..223 are set to black. The selftest hash is computed after that point, so the pinned frame hashes
carry the blank; 34 frame-hash checks were re-pinned with the change verified token by token (only
`fnv32=` moved).

Side finding, not this defect: the port paints a white/blue disc at the tent's top-left (rows 0..35)
that the oracle does not show; the tent runs BG3 on the SUB screen with colour-math ADD (main=3 sub=4
blend=Add), so it is most likely the additive light layer drawn at the wrong place. Open.

### The side finding, resolved the same day

The disc was the port's own Telepod iris. The WRAM seed read the iris machine at `$39..$3D` -- the
disassembly's direct-page addresses, whose page is `$0100` in the field -- and rebuilt the radius from
the `$7F1AC8` window table, where five stray rows (64..195) that every state carries made a full disc
at the origin. The ROM's bytes are `$0139` (state: 1 opening / 2 closing / 0 idle -- C0/2263 `STZ $39`
when the open completes and the radius stays), `$013A/$013B` (the centre in screen pixels, copied to
`$0350/$0352` for the C3/0008 window builder), `$013C` (the radius: INC at C0/2260 opening, DEC at 226D
closing) and `$013D` (the target, $20). Measured on marle_teleportation: f193 `$0139 = 1`, centre
(135,55), target 32; `$013C` 1..32 over f194-f225; f226 `$0139 = 0` with `$013C = 32`. The seed reads
those now; the tent, the fair and the pod states all carry zeros there.
