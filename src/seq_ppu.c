// seq_ppu.c - see seq_ppu.h.
#include <stdlib.h>
#include <string.h>
#include "seq_ppu.h"
#include "snes.h"
#include "ppu.h"

struct CtSeqPpu {
  Snes* shell;           // zeroed: palTiming = NTSC, openBus/hPos/vPos = 0
  Ppu*  ppu;
};

CtSeqPpu* CtSeqPpuCreate(void) {
  CtSeqPpu* p = (CtSeqPpu*)calloc(1, sizeof *p);
  if (!p) return NULL;
  p->shell = (Snes*)calloc(1, sizeof(Snes));
  if (!p->shell) { free(p); return NULL; }
  p->ppu = ppu_init(p->shell);
  if (!p->ppu) { free(p->shell); free(p); return NULL; }
  ppu_reset(p->ppu);
  return p;
}

void CtSeqPpuFree(CtSeqPpu* p) {
  if (!p) return;
  if (p->ppu) ppu_free(p->ppu);
  free(p->shell);
  free(p);
}

Ppu* CtSeqPpuCore(CtSeqPpu* p) { return p ? p->ppu : NULL; }

// snes/dma.c's walk, verbatim in its order and its arithmetic, minus the cycle bookkeeping (the PPU
// has no clock here: a transfer lands between the two lines it separates, which is all a picture sees).
static const int bAdrOffsets[8][4] = {
  {0,0,0,0}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1}, {0,1,2,3}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1}
};
static const int transferLength[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };

typedef struct {
  uint16_t tableAdr, size;
  uint8_t  repCount;
  uint8_t  doTransfer, terminated;
} Walk;

static void writeB(Ppu* ppu, uint8_t bAdr, uint8_t v) {
  if (bAdr < 0x40) ppu_write(ppu, bAdr, v);          // $2100-$213F; nothing else is a stage's HDMA target
}

static void hdmaInit(const CtSeqPpuHdma ch[8], uint8_t enable, Walk* w, CtSeqPpuRead rd, void* ctx) {
  for (int i = 0; i < 8; i++) {                      // dma_initHdma
    w[i].doTransfer = 0; w[i].terminated = 0;
    if (!((enable >> i) & 1)) continue;
    w[i].tableAdr = ch[i].aAdr;
    w[i].repCount = rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].tableAdr++);
    if (w[i].repCount == 0) w[i].terminated = 1;
    if (ch[i].dmap & 0x40) {
      w[i].size = rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].tableAdr++);
      w[i].size |= (uint16_t)(rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].tableAdr++) << 8);
    }
    w[i].doTransfer = 1;
  }
}

static void hdmaRun(Ppu* ppu, const CtSeqPpuHdma ch[8], uint8_t enable, Walk* w, CtSeqPpuRead rd, void* ctx) {
  int any = 0, lastActive = 0;                       // dma_doHdma
  for (int i = 0; i < 8; i++)
    if ((enable >> i) & 1) { any = 1; if (!w[i].terminated) lastActive = i; }
  if (!any) return;
  for (int i = 0; i < 8; i++) {                      // all copies
    if (!((enable >> i) & 1) || w[i].terminated || !w[i].doTransfer) continue;
    int mode = ch[i].dmap & 7;
    for (int j = 0; j < transferLength[mode]; j++) {
      uint8_t v = (ch[i].dmap & 0x40) ? rd(ctx, ((uint32_t)ch[i].indBank << 16) | w[i].size++)
                                      : rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].tableAdr++);
      writeB(ppu, (uint8_t)(ch[i].bAdr + bAdrOffsets[mode][j]), v);
    }
  }
  for (int i = 0; i < 8; i++) {                      // all updates
    if (!((enable >> i) & 1) || w[i].terminated) continue;
    w[i].repCount--;
    w[i].doTransfer = (uint8_t)((w[i].repCount & 0x80) != 0);
    uint8_t next = rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].tableAdr);
    if ((w[i].repCount & 0x7F) == 0) {
      w[i].repCount = next;
      w[i].tableAdr++;
      if (ch[i].dmap & 0x40) {
        if (w[i].repCount == 0 && i == lastActive) w[i].size = 0;
        else w[i].size = rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].tableAdr++);
        w[i].size |= (uint16_t)(rd(ctx, ((uint32_t)ch[i].aBank << 16) | w[i].tableAdr++) << 8);
      }
      if (w[i].repCount == 0) w[i].terminated = 1;
      w[i].doTransfer = 1;
    }
  }
}

void CtSeqPpuFrame(CtSeqPpu* p, const CtSeqPpuHdma ch[8], uint8_t enable,
                   CtSeqPpuRead rd, void* ctx, uint8_t* rgb) {
  if (!p || !p->ppu) return;
  Ppu* ppu = p->ppu;
  Walk w[8];
  memset(w, 0, sizeof w);
  ppu_handleFrameStart(ppu);                         // (0, 0)
  if (rd) hdmaInit(ch, enable, w, rd, ctx);          // (16, 0)
  if (rd) hdmaRun(ppu, ch, enable, w, rd, ctx);      // (1104, 0)
  for (int line = 1; line <= 224; line++) {
    ppu_runLine(ppu, line);                          // (512, line)
    if (rd) hdmaRun(ppu, ch, enable, w, rd, ctx);    // (1104, line)
  }
  if (!rgb) return;
  // ppu_putPixels' row choice without its 512x480 canvas: a non-interlaced frame draws row y from
  // pixelBuffer[(y + (evenFrame ? 0 : 239)) * 2048], pixel x at byte 8*x (+1 B, +2 G, +3 R in format 1).
  for (int y = 0; y < 224; y++) {
    const uint8_t* src = CtSeqPpuLine(p, y);
    for (int x = 0; x < 256; x++) {
      const uint8_t* q = src + x * 8;
      rgb[(y * 256 + x) * 3 + 0] = q[3];
      rgb[(y * 256 + x) * 3 + 1] = q[2];
      rgb[(y * 256 + x) * 3 + 2] = q[1];
    }
  }
}

const uint8_t* CtSeqPpuLine(CtSeqPpu* p, int y) {
  if (!p || !p->ppu || y < 0 || y >= 239) return NULL;
  int y1 = y + (p->ppu->evenFrame ? 0 : 239);
  return &p->ppu->pixelBuffer[y1 * 2048];
}
