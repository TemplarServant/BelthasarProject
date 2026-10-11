// boot.h — Chrono Trigger reset/boot path, reimplemented in C.
#ifndef CT_BOOT_H
#define CT_BOOT_H

#include "snes.h"

// Run one frame of the C port. For M0, logic that has not been ported yet
// falls back to the oracle via a copy-stub; on return the frame's WRAM state
// is in g_ram. As boot routines are ported, the copy-stub shrinks.
void CtBootRunFrame(Snes* oracle);

// Hardware register-init callback: receives a 16-bit register address + value.
typedef void (*CtRegWrite)(void* ctx, uint16_t reg, uint8_t val);

// Port of the CPU/PPU register init at $FD:C000..$C0D3. Emits the exact write
// sequence the original performs, through w(); drives no hardware itself, so the
// same routine can later feed either a validation log or a real port machine.
void CtBootHwInit(CtRegWrite w, void* ctx);

// Port of the WRAM-affecting part of $C0:000E..0053: install the NMI/IRQ handler
// trampolines in low RAM and clear the work-RAM regions (the original clears via
// DMA fill-with-zero through $2DF1). Operates on a 128 KB WRAM image
// (7E:0000.. at ram[0], 7F:0000.. at ram[0x10000]); does not touch the stack.
void CtBootInitMain(uint8_t* ram);

#endif
