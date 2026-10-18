// spc700_lengths.c -- tools/spc700dis.py's opcode lengths against the vendored SPC700 core (snes/spc.c).
//
// Every opcode runs once at $0200 over a zeroed 64 KB and zeroed registers; the PC it leaves is its length. With zero
// operands a relative branch, taken or not, lands on the next instruction, so only the jumps, calls, returns and the
// halts (whose PC goes elsewhere) are left out. Usage: spc700_lengths "<256 lengths from spc700dis.py lengths>"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spc.h"

static uint8_t ram[0x10000];
static uint8_t rd(void* m, uint16_t a) { (void)m; return ram[a]; }
static void wr(void* m, uint16_t a, uint8_t v) { (void)m; (void)a; (void)v; }   // writes dropped: the code stays put
static void idle(void* m, bool w) { (void)m; (void)w; }

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: spc700_lengths \"<256 lengths>\"\n"); return 2; }
  int want[256], n = 0;
  for (char* s = strtok(argv[1], " "); s && n < 256; s = strtok(NULL, " ")) want[n++] = atoi(s);
  if (n != 256) { fprintf(stderr, "need 256 lengths, got %d\n", n); return 2; }
  static const int skip[] = { 0x01, 0x11, 0x21, 0x31, 0x41, 0x51, 0x61, 0x71, 0x81, 0x91, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1,
                              0xF1, 0x0F, 0x1F, 0x3F, 0x4F, 0x5F, 0x6F, 0x7F, 0xEF, 0xFF };
  Spc* spc = spc_init(NULL, rd, wr, idle);
  int bad = 0, tested = 0;
  for (int op = 0; op < 256; op++) {
    int sk = 0;
    for (size_t k = 0; k < sizeof skip / sizeof skip[0]; k++) if (skip[k] == op) sk = 1;
    if (sk) continue;
    memset(ram, 0, sizeof ram);
    ram[0x0200] = (uint8_t)op;
    spc_reset(spc, true);
    spc->resetWanted = false;
    spc->pc = 0x0200; spc->sp = 0xEF;
    spc_runOpcode(spc);
    int got = spc->pc - 0x0200;
    tested++;
    if (got != want[op]) { printf("opcode %02X: table says %d, the core %d\n", op, want[op], got); bad++; }
  }
  printf("spc700-lengths: %d opcodes run, %d differ (%d control-flow skipped)\n", tested, bad, 256 - tested);
  spc_free(spc);
  return bad ? 1 : 0;
}
