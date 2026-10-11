# Boot/init structure — Chrono Trigger (USA)

Reconstructed from a real execution trace (`ct.exe --trace`) disassembled by
`tools/disasm65816.py`. Full annotated listing: `boot_init.asm`. Reset details:
`reset.md`.

## Control-flow map

```
$00:FF00  reset stub        SEI; CLC; XCE (native mode); JML $FD:C000
   |
$FD:C000  hardware init      REP#$10/SEP#$20; stack=$06FF; DBR=$00
   |                         D=$4200: MEMSEL=1 (FastROM), clear NMITIMEN/DMA/
   |                         multiply/divide/timers; WRIO=$FF
   |                         D=$2100: INIDISP=$80 (forced blank), OBSEL=$63,
   |                         BGMODE=$09, clear all PPU regs (BG offsets x2)
   |  $FD:C0D3  JML $C0:000E
   |
$C0:000E  main init          SEP/REP, then subsystem init subroutines:
   |                           JSR $C0:0B64
   |                           JSR $C0:0B75
   |                           JSR $C0:0B4E
   |                           JSR $C0:2DF1   (called several times)
   |  $C0:0053  JSL $C7:0000
   |
$C7:0000  heavy init         large fill/clear/decompress loops:
                              - loop @ $C7:00AB / $00AE  (ran ~105k iterations)
                              - loop @ $C7:0A71..0A77    (ran ~14k iterations, D=$1E00)
                              ... this bank dominates boot cycle count.
```

## Observations relevant to porting

- The init reaches the PPU/CPU register clear quickly (~120 instructions) — a
  clean, self-contained first chunk to port to C and validate.
- The `TCD` trick (point direct page at `$4200` then `$2100`) means many stores
  are 2-byte direct-page writes to hardware registers; the disassembler resolves
  these to register names via the traced D value.
- **Bank `$C7` is the heavy initializer** (memory clears + decompression). Its
  loops execute >100k times, so a trace must be large to step past it; reaching
  the title-screen main loop needs millions of steps. Porting C7 will be a
  milestone of its own (it likely contains the Evil-Peer LZ decompressor and the
  initial VRAM/WRAM setup).
- Index width is 16-bit (`REP #$10`) for the whole init; accumulator toggles
  8/16-bit (`SEP/REP #$20`) — the trace records the real M/X at every step so the
  disassembly's operand widths are exact.

## Next steps

1. ~~Port `$FD:C000..$C0D3` (register init) to C in `src/boot.c`.~~ **Done** —
   `CtBootHwInit()` reproduces the exact 76 register writes; validated by
   `ct.exe --portcheck` (oracle vs. port write-log, 0 mismatches).
2. ~~Add a **run-to-PC** primitive + write-logging to validate ported chunks at
   a known boundary.~~ **Done** — `CtOracleRunToPC` + `CtWriteLog*` +
   `CtOracleInstallWriteHook` (overrides `cpu->write`, no core edit).
3. ~~Port the `$C0:000E` init subroutines (`$0B64/$0B75/$0B4E/$2DF1`).~~ **Done** —
   `CtBootInitMain()` (src/boot.c); validated by `ct.exe --bootcheck` (WRAM
   snapshot at `$C0:0053`): **0 non-stack diffs** (only `$7E:06FE`, JSR/RTS
   detritus, differs — expected and ignored).
   - `$0B64`/`$0B75`: relocate NMI/IRQ handlers as JML trampolines —
     `$0500`=`JML $C0EA63` (NMI), `$0504`=`JML $C0ECCC` (IRQ).
   - `$0B4E`: SEI; INIDISP=$80; NMITIMEN/MDMAEN/HDMAEN=0.
   - `$2DF1`: parameterized **WRAM clear via DMA** — DMAs the PPU multiply result
     `$2134` (M7A·M7B = 0) into a WRAM range (params at `$014B`=addr, `$014D`=bank,
     `$014E`=count). Called 3×: clears `$7E:0000-04FF`, `$7E:0700-EFFF`,
     `$7F:5080-A0FF`, preserving `$0500-06FF` (handlers + stack).
4. ~~Bank `$C7` (suspected LZ).~~ **Identified** — it is the **SPC700 Akao
   sound-driver upload** (IPL handshake), *not* a decompressor. Full analysis in
   [`apu_upload.md`](apu_upload.md). It drives the emulated APU and barely touches
   WRAM, so it is the natural "C drives the reused hardware" milestone (validate
   via an ARAM snapshot, not WRAM).

**Boot map complete:** `$FD:C000` reg-init → `$C0:000E` WRAM-init → `$C7` sound
upload → back to `$C0:0056` toward the main loop. Both `$FD:C000` and `$C0:000E`
blocks are ported and validated; `$C7` (APU) is the next milestone.

## Validation methods now available
- **WRAM/VRAM/CGRAM snapshot + VerifyEq** (frame boundaries) — for routines that
  change RAM.
- **Write-log comparison** (`--portcheck` style) — for routines that drive
  hardware registers, where RAM is unchanged.
- **run-to-PC** — stop the oracle at any `bank:pc` to compare at a chosen seam.
