# Reset / boot entry — Chrono Trigger (USA)

ROM: HiROM + FastROM, 4 MB, no copier header. SHA-1
`DE5822F4F2F7A55ACB8926D4C0EAA63D5D989312`. Interrupt vectors live in bank `$00`
(`$00:FFE0..FFFF` = file `0xFFE0`). Verified by reading the ROM directly.

## Emulation-mode vectors (`$00:FFF4..FFFF`)
| Vector | Addr     | Target  |
|--------|----------|---------|
| RESET  | `$00:FFFC` | **`$FF00`** |
| NMI    | `$00:FFFA` | `$FFFF` (unused in emulation mode) |
| IRQ    | `$00:FFFE` | `$FFFF` |

## Native-mode vectors (`$00:FFE4..FFEF`)
| Vector | Addr     | Target  |
|--------|----------|---------|
| NMI | `$00:FFEA` | `$FF10` |
| IRQ | `$00:FFEE` | `$FF14` |
| BRK | `$00:FFE6` | `$FF18` |
| COP | `$00:FFE4` | `$FFFF` |

## Reset stub @ `$00:FF00` (file `0xFF00`)
```
FF00  78           SEI               ; mask IRQ
FF01  18           CLC
FF02  FB           XCE               ; carry<->emulation: enter NATIVE mode
FF03  5C 00 C0 FD  JML $FD:C000      ; -> main init  (ROM file offset 0x3DC000)
```

## Native interrupt trampolines
```
FF10  5C 00 05 00  JML $00:0500      ; NMI handler (relocated into low RAM $0500)
FF14  5C 04 05 00  JML $00:0504      ; IRQ handler (relocated into low RAM $0504)
FF18  AF EF CD AB  LDA $ABCDEF       ; BRK path
```

## Next porting target
Main initialization begins at **`$FD:C000`** (ROM file offset **`0x3DC000`**).

M1 task: trace + disassemble from `$FDC000`, then translate the init sequence
(WRAM clear, PPU/DMA/register setup, initial data load, jump to the main loop)
into native C in `src/boot.c`, replacing the copy-stub one routine at a time and
keeping WRAM parity against the oracle after every frame.
