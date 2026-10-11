// harness_drive.h — shared ROM-boot drive script for ct.exe selfchecks.
//
// Lands the oracle in Crono's bedroom (loc $0100 == 0x002) via the timed
// new-game intro + A-mash. Extracted from main.c so field/OW runners and any
// future harness unit share one definition.
#ifndef HARNESS_DRIVE_H
#define HARNESS_DRIVE_H

#include "snes.h"

/* 0 = success (loc 002). 1 = SETUP FAIL (printed). DRIVE_CAP skips the assert. */
int HarnessDriveScript(Snes* s);

/* Canonical bedroom location word after a successful drive. */
#define HARNESS_DRIVE_LOC_BEDROOM  0x002

#endif
