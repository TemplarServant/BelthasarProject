# The field's BG tilemaps, scroll registers and scroll mask

Derived 2026-09-15 from the attract's loc 1B0 (the time vortex) and 0A5 (the moon), where the port drew the wrong part of a
layer. ROM listings via tools/dis.py; oracle = build/ct.exe from saves/attract_cena_inicial.state.

## 1. Tilemap geometry

FD/C0D7 (the field's PPU init) writes BG1SC/BG2SC/BG3SC = `$69 / $71 / $79`: size bits `01`, so every field BG has a
**64x32** tilemap. LakeSnes (snes/ppu.c ppu_getPixelForBgLayer) addresses a 64-wide map as two 32x32 halves: words
`row*32 + col` for x < 256 and `+$400` for x >= 256. The height is 32 rows (256 px) for every layer.

## 2. The load's tilemap build (C0/0A50)

`$1D7C/$1D7E/$1D80/$1D82 = $6000/$6800/$7000/$7800`, then:

| layer | routine | condition | upload |
|---|---|---|---|
| BG1 | C0/75E9 | always | `$800` bytes (C0/7F62): the 32x32 chip window from the camera, rows `$1D0E..+31`, metatile columns `$1D0A..+15` (C0/7612: `LSR / AND $1F` row, `AND $1E` column, `$7E:3000,X` id, `$7E:30C0,X` bit 8, `$7E:B000 + id*8` the four words) |
| BG2 | C0/78EC | `$1D35 BIT #02` clear | the same camera window (C0/7913), or the half-camera window `$12..$18` when `$0BC9` bit 7 (C0/792F) |
| BG2 | C0/794E | `$1D35 BIT #02` set | `$1000` bytes (C0/7F77): metatiles (0..15, 0..15) into the first half; the second half gets columns 16..31 of rows 0..15 when `$0BCF != $10` (C0/7991), rows 16..31 of columns 0..15 when `$0BD1 != $10` (C0/79AE, `$85 = 2`), else the first block again (C0/7976) |
| BG3 | C0/7CB5 | `$0BCA & 1` and `$01BB == 0` | as BG2 with `$1D35 BIT #04`, `$0BD3/$0BD5` (C0/7CCD camera, C0/7CE7 map, 7D28 / 7D45 / 7D0D) -- no half-camera arm |

`$1D36` = detached (md[1] bit 5 -> BG3, bit 4 -> BG2, C0/A3A7-A3BF); `$1D35` = `$1D36` OR has-own-speed (A4A5 / A4F5 when
md[2] / md[3] is non-zero). Map sizes from the header (C0/A3C1-A408): `$0BCB/$0BCD` BG1, `$0BCF/$0BD1` BG2, `$0BD3/$0BD5` BG3.
The NMI streamer (C0/EB91: `$1D78-$1D7B` bits 1/2/4 -> C0/8445 .. C0/87BF, twelve row/column uploaders) is not derived here.

## 3. The scroll registers are the step counters

The field NMI's HDMA (FD/C1EE): channel 1 `$43` `$0D` (BG1 H/V), channel 2 `$0F` (BG2), channel 3 `$11` (BG3), tables in
bank $7F alternating by `$0153 & $0F` (`$0FD7/$102E/$1085` or `$128F/$12E6/$133D`), each entry indirect to four bytes. Loc 1B0
f8470: BG1 `h0 v65502` = `$1D89`, BG2 `v514 / v517` = `$1D8D` (the two buffers a frame apart); loc 0A5 likewise.
So a layer built from the camera shows map pixel `entry camera + counter`, a map-built layer map pixel `counter`.

## 4. The scroll mask (C0/7477-74A5)

`LDX $01FE / LDA $F6000A,X / BMI ->7495`: bit 7 of the location record's byte 10 selects the whole map (`$1A = $1C = 0`,
`$1B = $0BCB - 1`, `$1D = $0BCD - 1`); otherwise `$1A = byte 10`, `$1C = byte 11`, `$1B = byte 12`, `$1D = byte 13`, unclamped.
Loc 1B0: `00 0F 00 3F` on a 32x32 map; arrival (7, 27) -> C0/7553 `$1D0E = (27 - 8) * 2 = $26` (writer C0:7581). The port
clipped the far bounds to its canvas and pinned the camera to the bottom (`$24`).

## 5. What the port does

- ct_vm.c CtSceneLayerOffsets: the origin is chosen by `$1D35` (map-built -> counter - camera) instead of `$1D36`.
- scene.c draw_layer (`mapRing`): a map-built layer wraps at 256 px down and at the tilemap's two halves across.
- field.c: the mask exactly as C0/7477; the frame crop wraps (fieldBaseAt) instead of clamping to the canvas.

MEASURED: loc 1B0, port f8463 vs cartridge f8455 57144 / 57344 identical (22642 with the old selector); the port's camera and
six counters equal the cartridge's over the visit, 10 frames later. Checks: attract-1b0-vortex-bg2-vs-oracle,
field-scroll-mask-entry-camera-1b0.

## 6. The half-camera map mode (`$0BC9` bit 7) -- PORTED

md[1] bit 6 sets `$0BC9 = 80` (C0/A397). C0/74B1-74C5 give BG2 a window at half the entry camera (`$12 = ($0A >> 1) & FE`,
`$14 = $12 + 20`, `$16 = $0E >> 1`, `$18 = $16 + 1C`, chips) and C0/792F builds BG2 from it. 93F5 `LDA $0BC9 / BMI` swaps BG2's
committers for C0/95E3 (X+) / 9619 (X-) / 964D (Y+) / 9683 (Y-): a 32-step phase in `$94` / `$97`, the window moved a chip at the
0F/10 and 1F/00 crossings, and the counter `$8B` / `$8D` stepped only when the new phase is even -- half the camera. BG3's
committers (96B7..) are the ordinary ones in both modes.

Loc 0A5 (cartridge f3977): camera row 9 phase A, `$1D89 = FECA`, `$1D8D = FF65`. BG2 map pixel = (entry row 48 >> 1) * 8 + counter
= 192 - 155 = 37. Port f5394 vs cartridge f3979: 56654 / 57344 (the bats' phase). Check attract-0a5-halfcamera-bg2-vs-oracle.

OPEN: the NMI streamer (C0/EB91, twelve uploaders) -- the port composes every camera-built layer from the map, which equals the
tilemap wherever the streamed window covers the screen.
