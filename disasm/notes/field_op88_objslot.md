# Op 0x88's palette records in the field -- C0/4892 arms, FD/E39C ticks (2026-09-18)

User item (translated from Portuguese): *"flea, when appearing, should be white and gradually return to normal, but that does not happen"* -- in the attract's
0AD the Flea materialises as a white silhouette and her colours come back over about a second; the port drew her in
her own colours from the first frame.

## The mechanism

Three pieces, all ROM, none of them new to the tree -- the port simply never connected them in the field:

1. **C0/4892, op 0x88** (move.c `CtVmColorFx`, graded by `--sweepops`). Its arms by the operand:
   `00` restore (48C0), `20`/`30` rotate (4919), `4x`/`5x` ramp (4970), `8x` inline palette blob (49F8). The two
   allocating arms take a free record from C0/4B2C (`$0520 + 12k`, k < 8, free = byte 0 is zero; CLC when the
   eight are taken: the op is consumed and the visit yields). The ramp arm fills the record:

   | byte | from | Flea's `88 40 1F F0 02` |
   |---|---|---|
   | +0 mode | operand & F0 | 40 |
   | +6 sub-mode | operand & 0F | 00 |
   | +1 first CGRAM word | `$0F81,X * 8 + $80` + (b2 >> 4) | 91 |
   | +2 count | b2 & 0F | 0F |
   | +3 step | constant 8 (49B7) | 08 |
   | +5 period | b4 | 02 |
   | +7 acc | (b3 & F0) \| (b3 >> 4) | FF |
   | +8 limit | (b3 & 0F) \| (b3 << 4) | 00 |

   Only the rotate arm stores the per-object ref `$7F0B80,X`; the ramp is fire-and-forget. `88 00` frees the
   record the ref names and re-uploads the object's `$E4` record into both pages -- **only if the ref is not
   negative** (48C8 BMI). After a load the ref is 0 (C0/58BC's DMA clear), so an `88 00` on an object that never
   rotated frees record 0 and restores its palette.

2. **FD/E39C** (src/objslot.c, the battle's own body): every record ticks `+4` against `+5`; on the wrap mode 4
   (FD/E82C) walks the acc toward the limit and writes `[dst, dst+cnt)` of the **live** CGRAM page `$7E:2200`
   from the **base** page `$7E:2000` -- sub-mode 0 = FD/E8D7, each 5-bit component `c + (acc * (31 - c)) >> 8`
   (toward white); mode 5 (FD/E5A4) sub-mode 0 = FD/E64F, `(acc * c) >> 8`. A mode-4 walk that borrows below 0
   turns the record off and copies the base page back; a mode-5 walk that reaches its limit stops THERE (the page
   stays scaled until something restores it).
   The field's VM pass calls it at **C0/00CD** (`JSL FD/FFF7`), right after FD/C2C1; C0/00DE (the load-loop
   variant), C0/02BA/02E1, C0/0CE1, C0/1793 and C0/3FC6 are the other field callers.

3. **The NMI uploads the live page.** The renderer used to read the base page `$2100` (it modelled the per-frame
   effects on top); it reads `$2300 + slot*32` now. Every other writer puts the same bytes on both pages (the
   allocator's two MVNs, the blob's two, C0/48C0, C0/70E9's whole-page copy at the load).

## Measured (saves/attract_cena_inicial.state)

Flea = loc 0AD obj12; her function at body+0569 is `8E 00 / AC 00 / 90 / 8E 33 / B4 / 88 40 1F F0 02 / AD 1E ...`.

| cartridge frame | what |
|---|---|
| f1561 | `8E 00`, `AC 00` |
| f1565 | `90` SHOW: `$1A99 = 01` (C0:4CDC), `$0F18 = 80` (C0:A8F5), palette claimed, `$2120`/`$2320` = her colours (C0:7300/730D) |
| f1569 | `8E 33`, `B4`, `88 40 1F F0 02`: `$052C = 40` (C0:4985), acc `$0533 = FF` (C0:49E3) |
| f1570 | FD:E863 acc F7; FD:E92D whitens `$2322..` |
| f1571 | first picture of her -- **in her own colours** (OAM has her 8 cells, CGRAM row $90 = the base) |
| f1572 | white |
| then | acc -8 every second frame; at the borrow the base page comes back |

The port, from `saves/attract_flea_entrance_pre.lakewram` (the cartridge's WRAM at f1540, port f = cart f - 1542):
f29 = cart f1571 2560/2560, f30 = cart f1572 2560/2560, f58 = cart f1600 (mid-fade) 2560/2560. Check
`field-op88-fade-flea-entrance-vs-oracle` (5 mutations). The whole attract (lockstep): port f3640 = cart f1571,
f3641 = cart f1572.

## Census (all 513 scene scripts, assets/script.py walk)

| form | sites | scenes |
|---|---|---|
| `88 00` restore | 89 | 30 |
| `88 20` / `88 30` rotate | 88 | 33 |
| `88 40` white, sub-mode 0 | 359 | 47 |
| `88 41`..`88 44` | 26 | 10 |
| `88 50` scale, sub-mode 0 | 296 | 58 |
| `88 54` | 1 | 1 |
| `88 8x` blob | 27 | 22 |

## Boundaries (named, not ported)

* Mode 4 sub-modes 1-6 (FD/E962 red, E98F green, EA5C, E935 blue, E9CA, EA11) and mode 5's other sub-modes: the
  record walks and restores, the intermediate frames show no effect. One-time `objslot:` line when one runs.
* Modes 1, 2, 3, 6, 7, 8+ (FD/E437, E532, E4E8, E807, E57C, E485) tick only. The rotation (modes 2/3) is still
  drawn by the renderer from its C mirror (ct_vm.c `palRot`), so the Telepod swirl is unchanged.
* Records the ROM arms outside op 0x88: at 0AD's entry the cartridge's record 0 already holds `20 1D 01 00 00 01`
  (a mode-2 rotation over BG palette 1, colours 13-14) that the port never arms -- so the port files Flea's record
  in `$0520` and the cartridge in `$052C`. The picture does not depend on the slot; the BG rotation itself is
  missing. Who arms it at the load is not derived.

## Two scratch stores the delegation exposed (vmdiff-manoria-nuns)

* `4892 STY $C7` spills the ROM's IP, which counts from `$7F:2001` -- the port's body offset **plus the pointer
  table** (`sc->header`). The live VM now hands the shared body `raw + 1` and `addr + header`; `$01C7/$01C8`
  then match the cartridge f1..f116 on the Manoria nuns (CB07/6408, two `88` sites). At f117 the cartridge's
  C0:300B spills there too -- a handler this port does not model.
* `C0/CA26 STA $C1` (the anim reload's base, C0/C98A) was computed and never stored; it is now. Without it the
  blob's `4A23 STA $C1` value survived frames the cartridge overwrites.
