// ct_vbeam.h -- the virtual beam for the anim drain's raster deadline C0/CA76 (the zelda3 timing model; see
// docs/DECISIONS.md, 2026-09-27). Read by the oracle (snes/snes.c snes_readBBus) and the port (src/move.c
// CtAnimDrainPass) alike: V = CT_DRAIN_VBEAM_ENTRY + visits * CT_DRAIN_VBEAM_STEP, against the ROM's `CMP $6B`.
// The values are part of the rule (of the cartridge's order of magnitude); at $6B = 0x78 a pass makes two visits.
// disasm/notes/anim_drain_cad9.md sec. 16.
#ifndef CT_VBEAM_H
#define CT_VBEAM_H
#define CT_DRAIN_VBEAM_ENTRY 0x38
#define CT_DRAIN_VBEAM_STEP  0x20
#endif
