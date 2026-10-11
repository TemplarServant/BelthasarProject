// harness_drive.c — see harness_drive.h.
#include "harness_drive.h"
#include "ct_cpu_infra.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct { uint16_t btn; int frames; } CtScriptStep;

/* Timed input script from boot to 1000 AD free-roam in Crono's bedroom.
   ~60 fps; LakeSnes serial-order button bits (B=0x01, A=0x100, Start=0x08). */
static const CtScriptStep CT_PLAY_SCRIPT[] = {
  {0x000, 240},                          // load + wait 4s
  {0x001, 6}, {0x000, 180},              // B, wait 3s
  {0x100, 6}, {0x000, 120},              // A, wait 2s
  {0x008, 6}, {0x000, 60},               // Start, wait 1s
  {0x008, 6}, {0x000, 2400},             // Start, wait 40s
  {0x100, 6}, {0x000, 60},               // A, wait 1s
  {0x100, 6}, {0x000, 60},               // A, wait 1s
  {0x100, 6}, {0x000, 360},              // A, wait 6s
  {0x100, 6}, {0x000, 300},              // A, wait 5s
  {0x100, 6}, {0x000, 300},              // A, wait 5s
  {0x100, 6}, {0x000, 60},               // A, wait 1s
  {0x100, 6}, {0x000, 300},              // A, wait 5s
  {0x100, 6}, {0x000, 600},              // A, wait 10s -> controllable
};

int HarnessDriveScript(Snes* s) {
  int cap = getenv("DRIVE_CAP") ? atoi(getenv("DRIVE_CAP")) : 0, run = 0;
  for (size_t i = 0; i < sizeof(CT_PLAY_SCRIPT) / sizeof(CT_PLAY_SCRIPT[0]); i++)
    for (int f = 0; f < CT_PLAY_SCRIPT[i].frames; f++) {
      if (cap && run++ >= cap) return 0;             // truncated debug run: no landing assert
      s->input1->currentState = CT_PLAY_SCRIPT[i].btn;
      CtOracleRunFrame(s);
    }
  if (cap) return 0;
  /* intro dialogue can outlast the timed steps; mash A until free-roam */
  for (int k = 0; k < 22; k++) {
    for (int f = 0; f < 6; f++) { s->input1->currentState = 0x100; CtOracleRunFrame(s); }
    for (int f = 0; f < 14; f++) { s->input1->currentState = 0; CtOracleRunFrame(s); }
  }
  s->input1->currentState = 0;
  int loc = s->ram[0x0100] | (s->ram[0x0101] << 8);
  if (loc != HARNESS_DRIVE_LOC_BEDROOM) {
    printf("SETUP FAIL: ctDriveScript landed loc=%03X (expected %03X = Crono's bedroom). "
           "Wrong ROM, timing drift, or incomplete intro clear.\n",
           loc, HARNESS_DRIVE_LOC_BEDROOM);
    return 1;
  }
  return 0;
}
