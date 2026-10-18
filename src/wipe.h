// wipe.h -- the window wipe's routine C3/01E4 with its seed C3/02DD, and the overworld's window object C6/E241 / C6/E27E
// that calls it, native (disasm/notes/field_window_wipe.md sec. 1-6, "The virtual beam").
#ifndef CT_WIPE_H
#define CT_WIPE_H

#include <stdint.h>

// All of them run on the 128 KB WRAM image (`$7E` at offset 0) and write every byte the ROM writes there, the direct-page
// scratch included ($03F0 / $03F2 / $03F4-$03F5), so the WRAM after a call is the cartridge's -- except the stack bytes
// below the caller's S (the routine's pushes and C3/02DD's return address), which the ROM leaves dead. `rom` is the
// headerless HiROM image (bank C0 at offset 0): the run chain at $C0:FE00 is read from it. No hardware register is
// touched: the two tables go to WRAM where the ROM's WMADD/WMDATA ($2181-$2183 / $2180) put them.
//
// The seed's beam reads are the rule's VIRTUAL BEAM (CLAUDE.md, the zelda3 model; snes.c snes_readBBus): `$213C` reads
// `$0351`, both `$213D` reads 00. `CMP $2137` compares with the CPU's open bus -- $2137 drives no data, so the read
// returns the last byte the CPU fetched, the operand's high byte $21.

// C3/01E4 (C3/000E's `JMP`, or a direct `JSL C3/01E4`), D = $0300: `$50` 0 -> nothing; `$51` 0 -> fill [$54:$52 + $1C0]
// ($E0 bytes) with 00 (mode 1) or FF (other modes); `INC $51`, at $20 `$50` = `$51` = 0; else the seed and the 224-line
// step into `$54:$52` (odd `$51`) or `$54:$52 + $E0` (even) and the working copy. The caller's registers the routine
// reads: X (its low byte, C3/02DD's `TXA`, on every call but a fill call -- the fill's MVN leaves X past the copy) and the
// data bank (C3/02DD's absolute `$0386` / `$0008` / `$00F0` / `$00F2` / `$00F4`); every flag it reads it sets first, and
// it restores every register (C3/01BD). The callers: the field's C0/22FA (X = 0004, DB = 00), the world's C6/E27E (below),
// the $C3 stage's task 3D72 and 7E/6844. Returns 0, or -1 without a ROM or with a bank this WRAM image cannot hold.
int CtWipeRoutine(const uint8_t* rom, long romLen, uint8_t* wram128, uint16_t callerX, uint8_t callerDb);
// The field's caller C0/22FA (C0/00B1 -> C0/21E1 -> C0/22D8): X = 0004 from C0/21EA's `DEC A / ASL A / TAX` of state 3,
// the data bank 00 (measured at every field call of the attract: `ct.exe --wipeaudit`).
#define CT_WIPE_FIELD_X   0x0004
#define CT_WIPE_FIELD_DB  0x00
// The same call on an image the caller throws away (field.c fieldWipeMask's look-ahead): not one of the game's calls, so
// it stays out of the CT_XLAT_SEEDTRACE history.
int CtWipePeek(const uint8_t* rom, long romLen, uint8_t* wram128, uint16_t callerX, uint8_t callerDb);

// C3/02DD alone (`JSL C3/02DD`, M = 8): `a` the accumulator's low byte and `carry` the carry at the call, `x` / `db` the
// caller's X and data bank. Stores and returns the new `$0386` (the routine ends `STA $0386 / RTL`).
uint8_t CtWipeSeed(uint8_t* wram128, uint8_t a, int carry, uint16_t x, uint8_t db);

// The overworld's window object, C2/181C's far call (op 4E) to `target` = $C6E241 (the install: channel 2's table header
// at $7E:8A42, `TSB $4A` #04, the object's +$22 -> $0350 and +$24 -> $0352 with bank $7E, then the call) or $C6E27E (the
// call alone, X = $009C -- the op's own `<< 1` from C2's dispatcher), then `$7E:8A25` = +$24 (+$E0 on an even `$0351`)
// and `$8A27` = that + $70. The DMA registers E241 writes ($4320-$4324, $4327) are the host's (world.c CtWorldHdmaSet).
// Returns 0, -1 without a ROM or for another target.
int CtOwWindowObject(const uint8_t* rom, long romLen, uint8_t* wram128, uint32_t target);

#endif
