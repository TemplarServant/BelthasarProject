// ct_vbeam.h -- THE VIRTUAL BEAM's values for the anim drain C0/CA76 (the zelda3 model, user decision 2026-09-27: the beam
// that ADVANCES with the pass's work). Read by the oracle (snes/snes.c snes_readBBus) and the port (src/move.c
// CtAnimDrainPass) alike: V = CT_DRAIN_VBEAM_ENTRY + visits * CT_DRAIN_VBEAM_STEP, against the ROM's own `CMP $6B`.
// The rule's values, of the cartridge's own order (the attract's 0A5 bats: passes entered at lines 56..141, 31-36 lines
// a bat, 0-4 visits a pass); at $6B = 0x78 a pass makes two visits.
#ifndef CT_VBEAM_H
#define CT_VBEAM_H
#define CT_DRAIN_VBEAM_ENTRY 0x38
#define CT_DRAIN_VBEAM_STEP  0x20
#endif
