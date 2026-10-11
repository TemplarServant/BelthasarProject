# Bank $C7 — SPC700 / Akao sound-driver upload (IPL protocol)

Reached via `JSL $C7:0000` at the end of the `$C0:000E` init. Despite dominating
the boot's instruction count (the per-byte echo wait runs tens of thousands of
times), bank `$C7` is **not** a graphics decompressor — it is the routine that
uploads the **Akao sound driver** into the SPC700's ARAM using the standard IPL
boot-ROM handshake.

## Entry table ($C7:0000-000F)
```
C7:0000  4C 38 00   JMP $0038     ; main upload entry
C7:0004  4C 40 01   JMP $0140
C7:0008  4C 26 03   JMP $0326
C7:000C  4C 7B 03   JMP $037B
```
(`EA` NOP padding between entries.)

## Upload parameters
- `$C7:0010` — table of 2-byte block source pointers (bank `$C7`):
  `$24C3, $4309, $40B4, $42B7, $42DD, $42F3`.
- `$C7:001C` = `$0200` — initial ARAM target address.
- Block 0 @ `$C7:24C3`: leading size word `$1BEF` ⇒ **7151-byte** payload (the
  main sound driver/data), uploaded to ARAM `$0200`.

## IPL handshake (disassembled at $C7:0038)
1. Save context (PHB/PHD/PHP/PHA/PHX/PHY), DBR=$00, D=$1E00.
2. Wait for the SPC700 IPL "ready" signature `$BBAA` on APUIO0/1 (`$2140/$2141`):
   write `$FF`→APUIO0, `$F0`→APUIO1, poll `CPX $2140` until `$BBAA`.
3. Send ARAM target: APUIO2/3 (`$2142/$2143`) ← `$C7:001C` (`$0200`);
   write `$CC`→APUIO0/1 to begin, wait for echo.
4. Per byte: read `[$12],Y` (block data), write to APUIO1 (`$2141`), write the
   running index to APUIO0 (`$2140`) via XBA, then **busy-wait `CMP $2140 / BNE`**
   until the SPC700 echoes the index; increment until the block size (`$0C/$0D`).

The big loops in the trace (`$C7:00AB` ~18.7k, `$C7:00A2` ~2.5k) are exactly this
per-byte echo wait and byte loop.

## Porting implications
- **Comparison surface:** this routine writes the APU I/O ports and ends up
  filling **ARAM** (`apu->ram`), not WRAM. Its WRAM footprint is only scratch in
  low RAM (`$0C/$0D`, `$12/$13/$14`). So for the **WRAM-parity** invariant, bank
  `$C7` is essentially a no-op.
- **This is the "C drives the reused hardware" milestone.** Faithful porting must
  drive the *emulated* SPC700 (the reused APU) through the same IPL handshake so
  the sound driver lands in ARAM identically. Validation: compare the port's
  `apu->ram` against the oracle's after upload (a new ARAM-snapshot check),
  advancing the APU manually between port writes to service the busy-wait.
- Alternative (pragmatic, zelda3-style): keep the SPC700/DSP emulated and perform
  this upload once on the port machine; music then plays via the emulated APU.

This closes the boot map: **reg-init (`$FD:C000`) → WRAM-init (`$C0:000E`) →
sound-driver upload (`$C7`)** → returns to `$C0:0056` and on toward the main loop.
