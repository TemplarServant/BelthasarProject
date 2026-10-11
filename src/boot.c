// boot.c — Chrono Trigger reset/boot path (C reimplementation).
//
// Reset flow (verified from the ROM; see disasm/notes/reset.md):
//   $00:FF00  78        SEI
//   $00:FF01  18        CLC
//   $00:FF02  FB        XCE              ; enter 65816 native mode
//   $00:FF03  5C 00 C0 FD  JML $FD:C000  ; main init  (ROM file offset 0x3DC000)
//
//   Native vectors: NMI -> $00:FF10 -> JML $00:0500
//                   IRQ -> $00:FF14 -> JML $00:0504   (handlers relocated to RAM)
//
// TODO(M1): disassemble from $FD:C000 and translate the init sequence
// (WRAM clear, PPU/DMA/register setup, initial data load) into native C here,
// replacing the copy-stub one routine at a time and validating WRAM parity
// against the oracle after every frame.
#include "ct_cpu_infra.h"
#include "boot.h"

void CtBootRunFrame(Snes* oracle) {
  // Not yet ported: adopt the oracle's WRAM as the port's authoritative state.
  CtPortCopyWramFromOracle(oracle);
}

// Port of $FD:C000..$C0D3. The original points the direct page at $4200 then
// $2100 and uses short stores; we issue the equivalent absolute register writes
// in the exact same order/values (including the write-twice scroll/Mode-7 regs).
void CtBootHwInit(CtRegWrite w, void* ctx) {
  // --- CPU registers (original D=$4200) ---
  w(ctx, 0x420D, 0x01);                               // MEMSEL: enable FastROM
  w(ctx, 0x4200, 0x00);                               // NMITIMEN: NMI/IRQ/auto-joy off
  w(ctx, 0x420B, 0x00);                               // MDMAEN
  w(ctx, 0x420C, 0x00);                               // HDMAEN
  for (uint16_t r = 0x4202; r <= 0x420A; r++)
    w(ctx, r, 0x00);                                  // WRMPYA..VTIMEH
  w(ctx, 0x4201, 0xFF);                               // WRIO

  // --- PPU registers (original D=$2100) ---
  w(ctx, 0x2100, 0x80);                               // INIDISP: forced blank
  w(ctx, 0x2101, 0x63);                               // OBSEL
  w(ctx, 0x2102, 0x00);                               // OAMADDL
  w(ctx, 0x2103, 0x00);                               // OAMADDH
  w(ctx, 0x2105, 0x09);                               // BGMODE ($2104 OAMDATA skipped)
  w(ctx, 0x2106, 0x00);                               // MOSAIC
  for (uint16_t r = 0x2107; r <= 0x210C; r++)
    w(ctx, r, 0x00);                                  // BG1SC..BG34NBA
  for (uint16_t r = 0x210D; r <= 0x2114; r++) {       // BG scroll offsets:
    w(ctx, r, 0x00); w(ctx, r, 0x00);                 //   write-twice each
  }
  w(ctx, 0x2115, 0x00);                               // VMAIN
  w(ctx, 0x2116, 0x00);                               // VMADDL
  w(ctx, 0x2117, 0x00);                               // VMADDH
  w(ctx, 0x211A, 0x00);                               // M7SEL
  w(ctx, 0x211B, 0x00); w(ctx, 0x211B, 0x01);         // M7A = $0100 (identity)
  w(ctx, 0x211C, 0x00); w(ctx, 0x211C, 0x00);         // M7B = 0
  w(ctx, 0x211D, 0x00); w(ctx, 0x211D, 0x00);         // M7C = 0
  w(ctx, 0x211E, 0x00); w(ctx, 0x211E, 0x01);         // M7D = $0100 (identity)
  w(ctx, 0x211F, 0x00); w(ctx, 0x211F, 0x00);         // M7X = 0
  w(ctx, 0x2120, 0x00); w(ctx, 0x2120, 0x00);         // M7Y = 0
  w(ctx, 0x2121, 0x00);                               // CGADD
  w(ctx, 0x2123, 0x00);                               // W12SEL
  w(ctx, 0x2124, 0x03);                               // W34SEL
  w(ctx, 0x2125, 0x00);                               // WOBJSEL
  for (uint16_t r = 0x2126; r <= 0x212B; r++)
    w(ctx, r, 0x00);                                  // WH0..WOBJLOG
  w(ctx, 0x212E, 0x00);                               // TMW
  w(ctx, 0x212F, 0x00);                               // TSW
  w(ctx, 0x2130, 0x00);                               // CGWSEL
  w(ctx, 0x2131, 0x00);                               // CGADSUB
  w(ctx, 0x212C, 0x1F);                               // TM: all layers on main
  w(ctx, 0x212D, 0x1F);                               // TS
  w(ctx, 0x2132, 0xC0);                               // COLDATA
  w(ctx, 0x2133, 0x00);                               // SETINI
}

// DMA fill-with-zero of a WRAM region — the net effect of the $2DF1 routine,
// which DMAs the PPU multiply result ($2134, with M7A=M7B=0 => 0) into WRAM.
static void ctDmaClear(uint8_t* ram, uint8_t bank, uint16_t adr, uint32_t count) {
  uint32_t base = ((uint32_t)(bank & 1) << 16) | adr;   // 7E -> ram[0..], 7F -> ram[0x10000..]
  for (uint32_t i = 0; i < count; i++) ram[(base + i) & 0x1FFFF] = 0x00;
}

void CtBootInitMain(uint8_t* ram) {
  // $C0:0B64 / $0B75 — relocate the NMI/IRQ handlers as JML trampolines in low RAM
  ram[0x0500] = 0x5C; ram[0x0501] = 0x63; ram[0x0502] = 0xEA; ram[0x0503] = 0xC0; // NMI: JML $C0EA63
  ram[0x0504] = 0x5C; ram[0x0505] = 0xCC; ram[0x0506] = 0xEC; ram[0x0507] = 0xC0; // IRQ: JML $C0ECCC
  // $C0:0B4E — SEI; INIDISP=$80; NMITIMEN/MDMAEN/HDMAEN=0 (registers; handled by reg-init path)
  // $2DF1 x3 — clear the work-RAM regions (params live at $014B-$014F; the first
  // clear wipes them, they are re-set between calls):
  ctDmaClear(ram, 0x7E, 0x0000, 0x0500);  // $7E:0000-04FF
  ctDmaClear(ram, 0x7E, 0x0700, 0xE900);  // $7E:0700-EFFF
  ctDmaClear(ram, 0x7F, 0x5080, 0x5080);  // $7F:5080-A0FF
  // surviving DMA-param scratch (last values written; $7F clear does not touch $7E:$014x)
  ram[0x014B] = 0x80; ram[0x014C] = 0x50; ram[0x014D] = 0x7F;
  ram[0x014E] = 0x80; ram[0x014F] = 0x50;
}
