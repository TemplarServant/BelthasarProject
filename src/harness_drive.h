// harness_drive.h -- the shared boot drive for ct.exe selfchecks: lands the oracle in Crono's bedroom
// (loc $0100 == 0x002) through the timed new-game intro and an A-mash.
#ifndef HARNESS_DRIVE_H
#define HARNESS_DRIVE_H

#include "snes.h"

/* 0 = success (loc 002). 1 = SETUP FAIL (printed). DRIVE_CAP skips the assert. */
int HarnessDriveScript(Snes* s);

/* The bedroom's location word after a successful drive. */
#define HARNESS_DRIVE_LOC_BEDROOM  0x002

#endif
