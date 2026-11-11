# A sprite frame's chip word carries an 11-bit tile index (2026-11-11)

Found on playthrough leg 126 (the Blackbird's wing, loc 16B): its end picture is 275 px away, all of it Ayla. The ledger
had it as "Ayla's four-frame cycle, two frames off the lag". It is not timing: over the 67 frames around the leg's end
the port never draws the cartridge's pose, at any shift of the camera (the cartridge's 12x15 patch of her body is at
least 135 of 180 pixels away from every port frame).

## The ROM

A frame's assembly record is, per 16x16 unit, four chip words (TL TR BL BR) and a signed offset pair. The anim drain
(C0/CAD9, disasm/notes/anim_drain_cad9.md) copies each chip's 32 bytes from the sprite's graphics pack; both copiers
open the same way:

```
C0/E687  AND #07FF / ASL x5 / CLC / ADC $CD / TAX        (bit 14 clear: the direct store)
C0/E534  AND #07FF / ASL x5 ...                          (bit 14 set: through $2180)
```

So the tile index is **bits 0-10**: the source is `$CF:($CD + tile*32)`, tiles 0-2047.

## The port

The asset extractor (assets/sprites.py, assets/extract_assets.py `pack_sprites`) read ten bits and capped a playable
character's uncompressed pack at $8000 bytes, 1024 tiles; scene.c's compositor masked the chip with `0x3FF` too.

- Six characters' packs are under 1024 tiles (990, 994, 954, 1014, 858, 918) and every NPC's as well.
- **Ayla's pack is $8C00 bytes, 1120 tiles**, and 29 of her frames use tiles $400-$45F: 1D 1E 2F 3D 3E 42 56 66 84 85
  88 8A 9F A0 C9 CB CD CE CF D1 D3 D4 D5 DB DD E2 E6 F9 FA. The port drew those chips from the tiles 1024 below.

Fixed: eleven bits in the extractor's bound and in the compositor (`chip & 0x7FF`), a character's pack up to the next
graphics pointer (at most $10000 bytes). `ct_assets.dat` changes in one entry, sprite 5's blob (75,788 -> 81,932
bytes: 96 more tiles); a pack extracted before the fix keeps the old picture until the asset step runs again
(`python assets/extract_assets.py <rom> ct_assets.dat`, tools/setup.py's step 4) -- a new binary on an old pack skips
the chips past the pack instead of drawing the wrong tiles.

## Measured

- Leg 126's end picture: 275 -> 0 px (`python tools/playthrough.py cmp 126_wing_dalton`: SAME).
- `python tools/sprite_pack_check.py`: the extractor's sprite 5 has 1120 tiles and no chip past them; the seven
  characters' 1785 frames in ct_assets.dat have none either (133 for sprite 5 in the old pack).

Checks: `sprite-pack-tile-index-is-11-bits` (two mutations: the 10-bit cap, the 10-bit bound) and
`playthrough-leg-126-ayla-frames-vs-cartridge` (the compositor's mask).

## Open

- The port's battle and menu draw the characters from the same blobs (CtComposeSprite), so Ayla's frames there are
  fixed with it; none of her 29 frames has been looked at in a fight against the cartridge.
- Seeding the port inside leg 126's cutscene (tools/field_picture_cmp.py `at=`) is ~20,000 px away with either build:
  the scripted camera is not in the WRAM seed, so the leg's own line is the picture check.
