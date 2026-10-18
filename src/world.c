// M3-B: overworld camera/scroll -- C port of the bank-C2 scroll engine (see world.h for the RE map).
#include "world.h"
#include "ct_owobj.h"   // the ambient-object block ABI (offsets/class shared with field.c)
#include "vwf.h"      // C2/57DF / 5823: the proportional text (the world map's era label)
#include "lz.h"       // C6/E028: the world scripts' far call into C3/0002 (ct_lz_decompress_bounded)
#include "data/ow_anim.h"  // ROM $C3:E000..EFFF: the anim sub-VM's pointer table + bytecode
#include <string.h>
#include <stddef.h>   // ptrdiff_t (the draw list maps a block pointer back to its index)
#include <stdio.h>      // CT_MAILTRACE only (env-gated debug trace in CtWorldSoundRequest)
#include <stdlib.h>

// The anim sub-VM reads its bytecode through a (bank, 16-bit pc) long pointer built from the
// object's +0D/+0B. Only bank $C3 is ever stamped (object-VM op 0x30), and the reachable region is
// baked in ow_anim.h -- so this resolves exactly that window and refuses anything else instead of
// reading out of bounds.
// ...and from the ROM itself for another bank (the world map's markers run scripts in bank $C2, C2:6A18 / 6A20).
static const uint8_t* g_owRom; static long g_owRomLen;   // (defined with CtWorldSetRom below)
static const uint8_t* ctWorldAnimRom(uint8_t bank, uint16_t pc) {
  if (bank != 0xC3) {
    long o = ((long)(bank & 0x3F) << 16) + pc;
    return (bank >= 0xC0 && g_owRom && o + 8 <= g_owRomLen) ? g_owRom + o : NULL;
  }
  if (pc < CT_OWANIM_BASE || (uint32_t)pc + 4u > CT_OWANIM_BASE + CT_OWANIM_SIZE) return NULL;
  return ctOwAnimC3 + (pc - CT_OWANIM_BASE);
}
// ...and from WRAM: C2/0E1D reads its script and C2/0D08 the descriptor through a long pointer whose bank is the block's +0D,
// and the place-name banner's are in bank $7E (C2/5656: +0B = $8600, +0D = $7E -- the frame C2/5775 copies there).
static const uint8_t* owAnimPtr(const uint8_t* ram, uint8_t bank, uint16_t pc) {
  if ((bank == 0x7E || bank == 0x7F) && ram) return pc <= 0xFFF8 ? ram + ((bank & 1) << 16) + pc : NULL;
  return ctWorldAnimRom(bank, pc);
}

void CtWorldCamSeed(CtWorldCam* c, const uint8_t* ram) {
  c->bg1h = (uint16_t)(ram[0x20] | (ram[0x21] << 8));
  c->bg2h = (uint16_t)(ram[0x22] | (ram[0x23] << 8));
  c->bg1v = (uint16_t)(ram[0x26] | (ram[0x27] << 8));
  c->bg2v = (uint16_t)(ram[0x28] | (ram[0x29] << 8));
  c->bg3h = (uint16_t)(ram[0x24] | (ram[0x25] << 8));
  c->bg3v = (uint16_t)(ram[0x2A] | (ram[0x2B] << 8));
  c->bg3ctr = ram[0x0D12];
  c->pendDx = (int16_t)(ram[0x1BF1] | (ram[0x1BF2] << 8));
  c->pendDy = (int16_t)(ram[0x1BF3] | (ram[0x1BF4] << 8));
  // A world seeded mid-flight carries its drift and wave objects' state in their blocks (+2 visited, +12/+16 the
  // drift's fractions, +22 C2/7652's phase; C2/7759's phase is $7E:01E2).
  c->bg3drInit = 0; c->bg3fx = c->bg3fy = 0; c->waveInit = 0; c->wavePhase = 0; c->bg3hLineOn = 0;
  c->waveTop = (uint16_t)(ram[0xA916] | (ram[0xA917] << 8));
  if (c->waveTop < 0xA925 || c->waveTop >= 0xA9A5) c->waveTop = 0xA925;
  for (int b = 0x0B30; b < 0x1B30; b += 0x40) {
    uint16_t cls = (uint16_t)(ram[b] | (ram[b + 1] << 8));
    if ((cls == 0x7702 || cls == 0x77F2) && ram[b + 2]) {
      c->bg3drInit = 1;
      c->bg3fx = (uint16_t)(ram[b + 0x12] | (ram[b + 0x13] << 8));
      c->bg3fy = (uint16_t)(ram[b + 0x16] | (ram[b + 0x17] << 8));
    }
    if ((cls == 0x7759 || cls == 0x7652) && ram[b + 2]) {
      c->waveInit = 1;
      c->wavePhase = cls == 0x7759 ? (uint16_t)(ram[0x01E2] | (ram[0x01E3] << 8))
                                   : (uint16_t)(ram[b + 0x22] | (ram[b + 0x23] << 8));
    }
  }
}

// One 1-px scroll step with the engine's wrap (inc path C2/0610: INC; CMP #limit; BCC; TDC --
// dec path C2/05C0: DEC; BPL; LDA #limit-1). The engine loops this |step| times.
static uint16_t stepWrap(uint16_t v, int dir, uint16_t limit) {
  if (dir > 0) { v++; if (v >= limit) v = 0; }
  else { if (v == 0) v = (uint16_t)(limit - 1); else v--; }
  return v;
}

void CtWorldMoverSeed(CtWorldMover* m, const uint8_t* ram) {
  m->posX = (uint16_t)(ram[0x0D84] | (ram[0x0D85] << 8));
  m->posY = (uint16_t)(ram[0x0D88] | (ram[0x0D89] << 8));
  m->velX = (int16_t)(ram[0x0D8C] | (ram[0x0D8D] << 8));
  m->velY = (int16_t)(ram[0x0D90] | (ram[0x0D91] << 8));
  m->probeX = (uint16_t)(ram[0x0D9A] | (ram[0x0D9B] << 8));
  m->probeY = (uint16_t)(ram[0x0D9C] | (ram[0x0D9D] << 8));
  m->phase = ram[0x0D72];
  m->moving = (uint8_t)(ram[0x0D98] & 0x80);
}

uint8_t CtWorldProp(const uint8_t* ram, int col8, int row8) {
  col8 = ((col8 % 192) + 192) % 192;              // map seam (engine path for col<0 unobserved; torus)
  row8 &= 0x7F;
  uint8_t blk = ram[0x5800 + (row8 >> 1) * 96 + (col8 >> 1)];
  uint8_t b = ram[0x7000 + blk * 2 + (row8 & 1)];
  return (col8 & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)(b >> 4);
}

// C2/62ED -- the per-CELL REGION nibble. Same shape as CtWorldProp above, a different grid: a 96x64
// nibble map packed two cells per byte at $7E:7200, 48 bytes per row. It is what zones the overworld,
// and `$1B9B,X` turns a region into a SONG (C2/2F25).
//   62F3: LDA $01 / STA $4202 ; 62F8: LDA #30 / STA $4203   -- row * 48, through the PPU multiplier
//   6300: LDA #7200 / ADC $4216 / STA $10                   -- long pointer = $7E:(7200 + row*48)
//   6308: LDA $00 / AND #00FF / LSR A / TAY                 -- Y = col / 2
//   6311: LDA [$10],Y / 6313: LSR $00 / BCS ->631C          -- col ODD  -> AND #0F   (low nibble)
//   6317: LSR A x4                                          -- col EVEN -> high nibble
// ⚠ `630A AND #00FF` is what makes the caller's `3008 TAX` safe: X is 16-bit there while A is 8-bit,
// so TAX would carry a stale high byte if B were not already zeroed here.
// ⚠ The ROM bounds NOTHING -- a row past 63 reads on into $7E:7E00, the entry records. Reproduced
// rather than clamped; every caller feeds it a coordinate that is already in range.
uint8_t CtWorldRegion(const uint8_t* ram, int col, int row) {
  uint8_t b = ram[0x7200 + row * 48 + (col >> 1)];
  return (col & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)(b >> 4);
}

// C2/2ED9 -- the shared SOUND-REQUEST commit, and the $1BEC/$1BED/$1BEE phase gate every overworld
// requester goes through. A caller stages the four mailbox bytes in $1BE8-$1BEB plus a priority in
// $1BED and calls this; it copies them into the real mailbox $1E00-$1E03 and marks one pending.
//   $1BEC  0 = idle, 1 = staged and not yet posted, $FF = BEING posted (C2/045D)
//   $1BED  the incoming request's priority
//   $1BEE  the pending request's priority
// Returns 1 for the ROM's CLC (accepted), 0 for SEC (refused).
int CtWorldSoundRequest(uint8_t* ram) {
  uint8_t pend = ram[0x1BEC];
  if (pend) {                                     // 2EDC: BEQ -> commit
    if (pend & 0x80) return 0;                    // 2EDE: BMI  -- mid-post, refuse
    if (ram[0x1BEE] < ram[0x1BED]) return 0;      // 2EE0-2EE6: LDA $1BEE / CMP $1BED / BCC
  }
  // TEST HOOK CT_MAILTRACE: who overwrites the $1E00 mailbox, and what was in it. Inert unset.
  { static int mt = -1; if (mt < 0) { const char* e = getenv("CT_MAILTRACE"); mt = (e && *e) ? 1 : 0; }
    if (mt) fprintf(stderr, "mailtrace   OW-REQUEST overwrites $1E00: was %02X %02X %02X -> now %02X %02X %02X\n",
                    ram[0x1E00], ram[0x1E01], ram[0x1E02], ram[0x1BE8], ram[0x1BE9], ram[0x1BEA]); }
  ram[0x1E00] = ram[0x1BE8];                      // 2EE8-2EFD -- COMMAND FIRST here, unlike C2/2F3A
  ram[0x1E01] = ram[0x1BE9];                      //   where it is written last. Kept as the ROM has it.
  ram[0x1E02] = ram[0x1BEA];
  ram[0x1E03] = ram[0x1BEB];
  ram[0x1BEE] = ram[0x1BED];                      // 2F00
  ram[0x1BEC] = 0x01;                             // 2F06
  return 1;                                       // 2F0B: CLC
}

// C2/18F1 -- the ambient script's PLAY SONG op (0x3D), gated on $7F01ED (the same field-song hold
// C2/2FAE uses). ONE body, two callers: the sub-VM's case 0x3D, and the host's new-game intro entry
// -- see the note at that call for why the host has to issue it itself.
// Op 0x4A's handler IS C2/18F9, the body below without the gate: the script plays its song whatever $7F01ED holds
// (overworld 1F4's second visit, `4A 4A`, the song with the hold set by C6/E63D).
static void owPlaySong18F9(uint8_t* ram, uint8_t song);
void CtWorldScriptPlaySong(uint8_t* ram, uint8_t song) {
  if (ram[0x101ED]) return;                         // 218F3 LDA $7F01ED / BNE -> straight to the RTS
  owPlaySong18F9(ram, song);
}
static void owPlaySong18F9(uint8_t* ram, uint8_t song) {
  ram[0x1BE8] = 0x10;                              // 218FB LDA #10 -- cmd $10 = PLAY SONG
  ram[0x1BE9] = song;                               // 21905 STA $1BE9
  ram[0x02AE] = song;                               // 21908 STA $02AE -- the shadow, written here
                                                    //   UNCONDITIONALLY (owMusicPlayRegion commits it
                                                    //   only on acceptance; this handler does not)
  ram[0x1BEA] = 0xFF; ram[0x1BEB] = 0xFF;           // 2190B LDA #FF -> STA $1BEA / STA $1BEB
  ram[0x1BED] = 0;                                  // 21913 STZ $1BED
  CtWorldSoundRequest(ram);                         // 21916 JSR C2/2ED9
}

// C2/3076 -- issue the region's song. $EB holds the region the compare last computed.
static void owMusicPlayRegion(uint8_t* ram) {
  ram[0x1BE9] = ram[0x1B9B + ram[0x00EB]];        // 3077: LDA $EB / TAX / LDA $1B9B,X
  ram[0x1BE8] = 0x10;                             // 3080: cmd $10 = PLAY
  ram[0x1BEA] = 0xFF; ram[0x1BEB] = 0x80;
  ram[0x1BED] = 0x00;                             // 308F: STZ $1BED -- priority 0, always accepted
  if (CtWorldSoundRequest(ram))                   // 3095: BCS -> skip
    ram[0x02AE] = ram[0x1BE9];                    // 3097: the shadow commits ONLY on acceptance
}

// C2/2F94 -- THE OVERWORLD MUSIC-ZONE OBJECT. It is a normal entry in the ambient-object array: the
// generic dispatcher C2/0535 loads the block's CLASS WORD (+0) as a handler address and `JMP ($0050)`s
// to it, so this object is simply the block whose class is $2F94 (block 14, $7E:0EB0, on world 0). The
// dispatcher also owns the counter this uses: C2/054E `INC $0003,X` bumps a 16-bit per-object frame
// counter every frame the handler returns CLC, and a handler returning SEC is KILLED (C2/0547 zeroes
// the class word). Hence every arm below returns 1.
//
// The states live in the block itself, at +2, and are dispatched through the table at C2/2F9D:
//   0 -> C2/2FA5   arm ($EB = FF, no region known yet), state := 1, FALL THROUGH into 1
//   1 -> C2/2FAE   compare the player's region against $02AE; on a change start the fade
//   2 -> C2/303C   wait 60 frames, then PLAY the new song and state := 3
//   3 -> C2/304F   send the stop/prepare and state := 1
// (states 4 and 5 point at C2/4EA6 / C2/02FE and are not part of the music cycle)
//
// `b` = the block base ($4E in the ROM). Returns 1 = keep the object (CLC), 0 = kill (SEC).
int CtWorldMusicObj(uint8_t* ram, int b) {
  int st = ram[b + 2];
  if (st == 0) { ram[b + 2] = 1; ram[0x00EB] = 0xFF; st = 1; }   // 2FA5-2FAC, then FALLS THROUGH
  if (st == 1) {
    if (ram[0x101ED]) {                           // 2FAE: LDA $7F01ED / BNE -- keep the FIELD song
      ram[0x1BE9] = ram[0x29AE];                  // 2FB4-2FC7
      ram[0x1BE8] = 0x10; ram[0x1BEA] = 0xFF; ram[0x1BEB] = 0x80;
      ram[0x1BED] = 0x00;
      if (CtWorldSoundRequest(ram)) ram[0x02AE] = ram[0x1BE9];   // 2FD0-2FD5
      return 1;
    }
    uint8_t mode = ram[0x027C];                   // 2FDA-2FE5: only these three transition modes
    if (!(mode == 0 || mode == 1 || mode == 7)) { ram[0x00EB] = 0xFF; return 1; }   // -> 3036
    if (ram[0x027E]) { ram[0x00EB] = 0xFF; return 1; }                              // 2FE7-2FEA
    int col = (ram[0x0283] | (ram[0x0284] << 8)) >> 4;          // 2FEE-2FF5 (16-bit read, >>4)
    int row = ((ram[0x0285] | (ram[0x0286] << 8)) >> 4) - 1;    // 2FF7-2FFF -- ⚠ MINUS ONE, and the
    uint8_t region = CtWorldRegion(ram, col, row);              //   entry path C2/2F15 does NOT do this
    ram[0x00EB] = region;                         // 3004: STA $EB
    if (ram[0x1B9B + region] == ram[0x02AE]) return 1;          // 300C-300F: unchanged -> nothing
    ram[b + 2] = (uint8_t)(ram[b + 2] + 1);       // 3013: INC $0002,X -> state 2
    ram[b + 3] = 0; ram[b + 4] = 0;               // 3016/3019: restart the frame counter
    ram[0x1BE8] = 0x81; ram[0x1BE9] = 0x0C;       // 301C-3023: cmd $81 param $0C = the FADE OUT
    ram[0x1BEA] = 0x00; ram[0x1BEB] = 0xFF;
    ram[0x1BED] = 0x00;
    CtWorldSoundRequest(ram);                     // 3031 -- the shadow is NOT committed here
    return 1;
  }
  if (st == 2) {
    if (ram[b + 3] >= 0x3C) {                     // 303E-3043: the LOW byte only, 60 frames
      ram[b + 2] = 3;                             // 3045
      owMusicPlayRegion(ram);                     // 304A: JSR C2/3076
    }
    return 1;
  }
  if (st == 3) {
    if (ram[0x1BEC] & 0x80) return 1;             // 3052: BMI -- still posting, hold
    ram[0x1BE8] = 0x81; ram[0x1BE9] = 0x00;       // 3054-3064: the $81 (00,FF,FF) stop/prepare
    ram[0x1BEA] = 0xFF; ram[0x1BEB] = 0xFF;
    ram[0x1BED] = 0x00;
    CtWorldSoundRequest(ram);
    ram[b + 2] = 1;                               // 306F: back to the compare state
  }
  return 1;
}

void CtWorldMoverFrame(CtWorldMover* m, const uint8_t* ram,
                       int heldUp, int heldDown, int heldLeft, int heldRight,
                       int* dxOut, int* dyOut) {
  *dxOut = 0; *dyOut = 0;
  if (m->phase == 1) {                            // at an 8px boundary: read input (C2/3567)
    if (!(heldUp | heldDown | heldLeft | heldRight)) { m->moving = 0; return; }
    if (heldUp)        { m->velX = 0;  m->velY = -1; }   // cardinal priority Up > Down > Left > Right
    else if (heldDown) { m->velX = 0;  m->velY = 1; }
    else if (heldLeft) { m->velX = -1; m->velY = 0; }
    else               { m->velX = 1;  m->velY = 0; }
    m->moving = 0x80;
    int px = m->posX + (m->velX > 0 ? 8 : m->velX < 0 ? -8 : 0);   // probe = pos +- 8 (C2/3A56)
    int py = m->posY + (m->velY > 0 ? 8 : m->velY < 0 ? -8 : 0);
    if (px < 0) px += 0x600; else if (px >= 0x600) px -= 0x600;    // torus wrap (C2/3A8F)
    m->probeX = (uint16_t)px;
    m->probeY = (uint16_t)(py & 0x3FF);
    int cx = m->probeX >> 3, cy = m->probeY >> 3;
    uint8_t c0 = CtWorldProp(ram, cx - 1, cy - 1);  // the 2x2 corner cells (C2/39D5)
    uint8_t c1 = CtWorldProp(ram, cx, cy - 1);
    if (((c0 | c1) & 3) != 0) {                   // verdict C2/39C3: AND #$0303 over the TOP pair
      m->velX = 0; m->velY = 0; m->moving = 0;    // blocked tail C2/36B6
      return;
    }
    m->phase = 2;                                 // INC $0002,X (C2/36EA) -- integrate THIS frame
  }
  if (m->phase != 2 || (m->velX == 0 && m->velY == 0)) return;
  m->posX = (uint16_t)(m->posX + m->velX);        // integrator C2/0F38 (fractions stay 0 on foot)
  m->posY = (uint16_t)(m->posY + m->velY);
  *dxOut = m->velX; *dyOut = m->velY;
  if (m->posX == m->probeX && m->posY == m->probeY)
    m->phase = 1;                                 // arrival (C2/3757): back to boundary, vel KEPT
}

// BG3 ocean animator (C2/75CE-75FC): ambient drift + consume the camera deltas. The animator
// object ticks BEFORE the camera apply, so it sees the PREVIOUS frame's $1BF1/$1BF3.
// ⭐ IT IS AN OBJECT'S HANDLER, NOT A FREE-RUNNING TICK (2026-09-17): C2/75CE belongs to CLASS $75C3, so the drift --
// and the camera-delta consume with it -- happens only while such an object is live. Oracle: the interactive world 0
// (saves/ow_w0.state) carries one at block $0CF0 (`--hookpc C275CE`: every visit with $4E = 0CF0; `--monitor 7E0024:2
// --writer`: -1 every fourth frame by C2:75ED, +1 every eighth by C2:75F5), while the attract's 1F0 leg has no $75C3
// object at all and its $24/$2A stay 0000 for the whole 270-frame visit. The port ticked it in EVERY world, so that
// leg's clouds drifted 49 px left and 24 px down across it -- the survey's worst overworld median (13.0).
// ...and each world has its OWN: world 0 carries class $75C3 (C2/75C3: every fourth frame `DEC $24`, every eighth
// `INC $2A`), world 1 class $75FD (C2/75FD: every fourth frame BOTH), and worlds 3, 6 and 7 carry none at all -- their
// BG3 never drifts. Both handlers skip everything, the consume included, while `$0294 & 10` is set (C2/75C6 / C2/7600).
// ...and worlds 2 and 4 carry a THIRD kind, a 16.16 drift: C2/7702 (world 2, 2300 A.D.'s ash) and C2/77F2 (world 4),
// byte for byte the same routine but for the velocity they arm on the first visit (`INC $0002,X` / `STA $001A..20,X`):
// every frame `$24`/`$2A` into +14/+18, C2/0F38's two 32-bit adds (+12/+14 += +1A/+1C, +16/+18 += +1E/+20), back into
// `$24`/`$2A`, then `$24 += $1BF1` / `$2A += $1BF3` and both STZ. The port had neither since the drift became the
// object's (4dd1cd0): world 2's BG3 stood still where the cartridge's ash moves (-3.46, +2.00) px a frame (user defect
// 2026-09-26 #6, "the 2300 AD smoke regressed, demo screen"; the attract's 1F2 leg, c5848 on: `$24` FFFC, -3/-4 a frame).
static int owBg3AnimatorBlock(const uint8_t* ram) {
  for (int b = OWOBJ_BASE; b < OWOBJ_END; b += OWOBJ_STRIDE) {
    uint16_t cls = (uint16_t)(ram[b] | (ram[b + 1] << 8));
    if (cls == 0x75C3 || cls == 0x75FD || cls == 0x7702 || cls == 0x77F2) return b;
  }
  return 0;
}
static uint16_t owBg3AnimatorClass(const uint8_t* ram) {
  int b = owBg3AnimatorBlock(ram);
  return b ? (uint16_t)(ram[b] | (ram[b + 1] << 8)) : 0;
}
static const uint16_t* owBg3DriftVel(uint16_t cls) {
  static const uint16_t v7702[4] = { 0x8931, 0xFFFC, 0x0000, 0x0002 };   // C2/7715-7727
  static const uint16_t v77F2[4] = { 0xADF4, 0xFFEE, 0x0000, 0xFFF6 };   // C2/7805-7817
  return cls == 0x7702 ? v7702 : cls == 0x77F2 ? v77F2 : NULL;
}
// C2/225E: `AND #03FF / TAX / LDA $C0F900,X / AND #00FF / CPX #0200 / BCC / EOR #FFFF / INC` -- a byte of the sine
// table at C0/F900, negative in the table's upper half.
static const uint8_t* g_owRom;   // (defined with CtWorldSetRom below)
static long g_owRomLen;
static int owSine(int i) {
  i &= 0x3FF;
  int v = (g_owRom && 0xF900 + i < g_owRomLen) ? g_owRom[0xF900 + i] : 0;
  return i >= 0x200 ? -v : v;
}
// THE DRIFT OBJECT'S VISIT (C2/75C3 / C2/75FD / C2/7702 / C2/77F2): the drift, then `$24 += $1BF1` / `$2A += $1BF3` and
// both STZ. Returns 0 when `$0294 & 10` skips the whole visit (C2/75C6 / C2/7600 `BIT #10 / BNE` -> CLC, nothing
// consumed). `ctrW` = the animator block's own +22 word when the RAM is the game's (the object pass); NULL on the
// read-only validators, which count in c->bg3ctr (seeded from $0D12 = world 0's $0CF0 + $22).
static int owBg3Drift(CtWorldCam* c, const uint8_t* ram, uint16_t cls, uint8_t* ctrW) {
  if (ram[0x0294] & 0x10) return 0;
  const uint16_t* vel = owBg3DriftVel(cls);
  if (vel) {                                               // C2/7702 / C2/77F2
    c->bg3drInit = 1;                                      // 770B-7727: the velocity armed on the first visit
    uint32_t x = ((uint32_t)c->bg3h << 16 | c->bg3fx) + ((uint32_t)vel[1] << 16 | vel[0]);   // C2/0F38
    uint32_t y = ((uint32_t)c->bg3v << 16 | c->bg3fy) + ((uint32_t)vel[3] << 16 | vel[2]);
    c->bg3fx = (uint16_t)x; c->bg3fy = (uint16_t)y;
    c->bg3h = (uint16_t)((x >> 16) + c->pendDx);            // 7737-7747: back into $24, + $1BF1
    c->bg3v = (uint16_t)((y >> 16) + c->pendDy);            // 773C-774F: $2A, + $1BF3
    c->pendDx = c->pendDy = 0;                             // 7751-7754 STZ
    return 1;
  }
  // C2/75CE / C2/7608 `INC $0022,X`: the counter is the BLOCK's, and C2/048A's MVN hands a spawned block its PARENT's
  // +05..+3F -- so the drift starts at whatever phase the bootstrap's own +22 had when it ran op $35.
  if (ctrW) { uint16_t k = (uint16_t)((ctrW[0] | (ctrW[1] << 8)) + 1); ctrW[0] = (uint8_t)k; ctrW[1] = (uint8_t)(k >> 8);
              c->bg3ctr = (uint8_t)k; }
  else c->bg3ctr++;
  if (cls == 0x75FD) {
    if ((c->bg3ctr & 3) == 0) { c->bg3h--; c->bg3v++; }   // C2/7611-7615
    // C2/7627-7646: `ASL $1BF3 / LDA $7EA916 / ADC $1BF3`, wrapped back into [A925, A9A5) by one 0x80 -- the wave
    // table's window follows the camera's vertical step, so a band of mist keeps its line offset as it scrolls.
    // MEASURED, the rule's cartridge (saves/field_re/catedral_world.state, Down 40 / Up 40, --writer): `$A916` A92B ->
    // A93B over eight 1-px frames down, A975 -> A965 up (C2:764A). The port held the window at A925 and, walking up or
    // down in 600 A.D., every band of mist changed its offset line by line: the mist ran sideways (user report 2026-10-04).
    { uint16_t t = (uint16_t)(c->waveTop + 2 * c->pendDy);
      if (t < 0xA925) t = (uint16_t)(t + 0x80);
      else if (t >= 0xA9A5) t = (uint16_t)(t - 0x80);
      c->waveTop = t; }
  } else {
    if ((c->bg3ctr & 3) == 0) c->bg3h--;                  // C2/75D7-75D9
    if ((c->bg3ctr & 7) == 0) c->bg3v++;                  // C2/75DB-75E3
  }
  c->bg3h = (uint16_t)(c->bg3h + c->pendDx);              // C2/75E5-75F3 / C2/7617-7625
  c->bg3v = (uint16_t)(c->bg3v + c->pendDy);
  c->pendDx = c->pendDy = 0;                              // C2/75F5-75F8 STZ
  return 1;
}
// The read-only validators' one-call frame (CtWorldCamFrame): the world-0 order, drift block before the lead's, so the
// visit consumes the previous frame's store and this frame's (dx,dy) is what the lead leaves for the next one.
static void owBg3Tick(CtWorldCam* c, const uint8_t* ram, uint16_t cls, int dx, int dy) {
  if (!owBg3Drift(c, ram, cls, NULL)) { c->pendDx = (int16_t)(c->pendDx + dx); c->pendDy = (int16_t)(c->pendDy + dy); return; }
  c->pendDx = (int16_t)dx; c->pendDy = (int16_t)dy;
}

// THE PER-LINE BG3 H SCROLL, an HDMA channel 1 to $2111 (DMAP $42, indirect, bank $7E) that two objects rebuild each
// frame after the drift has moved `$24` (their blocks follow the drift's in the object pass):
//   C2/7759 (world 2, block 0CF0 in the attract's 1F2): table `$7E:8A35` = F0 ->$8B56, F0 ->$8C36, 00 -- scanline L
//     reads the word at $8B56 + 2L; the even lines get `$24` (77B5-77C5), line 2k+1 gets `$24 + S(p + k)`, k = 0..111
//     (77D3-77EE), p = the word at $0022,X with X left at $01C0 by the loop -- i.e. $7E:01E2 -- which 77CC-77D0
//     advances by 4.
//   C2/7652 (world 1, block 0D70): table `$7E:A918` = C0 / C0 / C0 / A0 (64 + 64 + 64 + 32 lines), each entry's
//     address rewritten to $A925 by the NMI (C2/030A-0316), so scanline L reads word L & 63; word k = `$24 +
//     (S(p + 16k) >> 2)` with the sign kept (76DB-76E4 `ORA #C000`), p = the block's +22, advanced by 1.
// Both clear the channel ($4A bit 1, `TRB`) while `$1C` bit 7 (forced blank) is set and change nothing while `$0294 &
// 10`. MEASURED on the oracle (the attract's 1F2 leg, saves/attract_cena_inicial.state from c5800): C2:77B3 arms `$4A`
// at f5852 and C2:77D3 adds 4 to $01E2 every frame; its isolated BG3's even lines match a plain scroll (dx +4, dy -2 =
// `$24`/`$2A`'s step) and the odd ones do not -- the wave. World 1's table: saves/ow_w1.live, `$A918..` = C0 25A9 x3,
// A0 25A9, 00. The port drew every line at the one `$24`.
static void owBg3WaveTick(CtWorldCam* c, const uint8_t* ram) {
  uint16_t cls = 0;
  for (int b = OWOBJ_BASE; b < OWOBJ_END && !cls; b += OWOBJ_STRIDE) {
    uint16_t k = (uint16_t)(ram[b] | (ram[b + 1] << 8));
    if (k == 0x7759 || k == 0x7652) cls = k;
  }
  if (!cls) { c->bg3hLineOn = 0; return; }
  if (ram[0x1C] & 0x80) { c->bg3hLineOn = 0; return; }     // 7759 / 7652: `LDA $1C / BPL` else `TRB $4A #02`
  if (ram[0x0294] & 0x10) return;                          // `$0294 & 10`: nothing
  if (!c->waveInit) {                                      // the first visit: `INC $0002,X` / `STZ $0022,X`
    c->waveInit = 1;
    c->wavePhase = cls == 0x7759 ? (uint16_t)(ram[0x01E2] | (ram[0x01E3] << 8)) : 0;
    if (cls == 0x7652) c->waveTop = 0xA925;                // C2/7673-7678 `LDA #A925 / STA $7EA916`
  }
  uint16_t p = c->wavePhase;
  if (cls == 0x7759) {
    for (int k = 0; k < 112; k++) {
      c->bg3hLine[2 * k] = c->bg3h;
      c->bg3hLine[2 * k + 1] = (uint16_t)(c->bg3h + owSine(p + k));
    }
    for (int l = 224; l < 240; l++) c->bg3hLine[l] = c->bg3h;
    c->wavePhase = (uint16_t)(p + 4);
  } else {
    uint16_t w[64];
    for (int k = 0; k < 64; k++) {                         // 76C9-76F0
      int v = owSine(p + 16 * k);
      uint16_t u = (uint16_t)((uint16_t)v >> 2);           // LSR / LSR
      if (v < 0) u |= 0xC000;                              // 76DB BPL / 76DF ORA #C000
      w[k] = (uint16_t)(c->bg3h + u);                      // 76E6 ADC $24
    }
    // Each 64-line block reads the table from $A916 on (C2/0300-0312's four pointers; the table is the 64 words twice,
    // C2/76F2's MVN, so the read never leaves it).
    int top = (c->waveTop - 0xA925) >> 1;
    for (int l = 0; l < 240; l++) c->bg3hLine[l] = w[(top + (l & 63)) & 63];
    c->wavePhase = (uint16_t)(p + 1);
  }
  c->bg3hLineOn = 1;
}

void CtWorldCamFrame(CtWorldCam* c, const uint8_t* ram, int dx, int dy) {
  int n = dx < 0 ? -dx : dx;
  for (int i = 0; i < n; i++) {                    // H dispatcher C2/0568 for BG1 then BG2
    c->bg1h = stepWrap(c->bg1h, dx, 0x600);
    c->bg2h = stepWrap(c->bg2h, dx, 0x600);
  }
  n = dy < 0 ? -dy : dy;
  for (int i = 0; i < n; i++) {                    // V dispatcher C2/066C for BG1 then BG2
    c->bg1v = stepWrap(c->bg1v, dy, 0x400);
    c->bg2v = stepWrap(c->bg2v, dy, 0x400);
  }
  { uint16_t acls = owBg3AnimatorClass(ram);       // the world's own animator object, or none
    if (acls) owBg3Tick(c, ram, acls, dx, dy); }
  owBg3WaveTick(c, ram);                           // ...then the per-line BG3 H table, from the moved $24
}

// ---------------------------------------------------------------------------
// Tilemap streaming (see world.h). Per-BG WRAM tables:
static const int OW_DEFS[2] = { 0x3000, 0x3800 };  // 256 block defs x 4 chip words [TL,TR,BL,BR]
static const int OW_GRID[2] = { 0x4000, 0x5800 };  // 96x64 block grid

void CtWorldStreamSeed(CtWorldStream* st, const uint8_t* ram, const uint16_t* vram) {
  memcpy(st->tilemap[0], vram + 0x6000, sizeof(st->tilemap[0]));
  memcpy(st->tilemap[1], vram + 0x6800, sizeof(st->tilemap[1]));
  st->e3[0] = (uint16_t)(ram[0xE3] | (ram[0xE4] << 8));
  st->e3[1] = (uint16_t)(ram[0xE5] | (ram[0xE6] << 8));
  st->e7[0] = (uint16_t)(ram[0xE7] | (ram[0xE8] << 8));
  st->e7[1] = (uint16_t)(ram[0xE9] | (ram[0xEA] << 8));
  memcpy(st->staging, ram + 0xF000, sizeof(st->staging));   // the $E0 ring image (stale bytes)
  st->stageOff = 0;
}

static uint16_t owDefWord(const uint8_t* ram, int bg, int block, int off) {
  int a = OW_DEFS[bg] + block * 8 + off;
  return (uint16_t)(ram[a] | (ram[a + 1] << 8));
}

static void owTilemapBuildBg(CtWorldStream* st, const uint8_t* ram, const CtWorldCam* c, int bg);
static void owRebuildScratch(uint8_t* ram, int bg);
void CtWorldTilemapBuild(CtWorldStream* st, uint8_t* ram, const CtWorldCam* c) {
  for (int bg = 0; bg < 2; bg++) owTilemapBuildBg(st, ram, c, bg);
  memset(st->staging, 0, sizeof(st->staging));
  st->stageOff = 0;
  st->nPend = 0;
  // C2/2C7D-2C88 (and 2CB3-2CBE): `LDA #01 / STA $00 / JSR C2/09C5 / LDA #02 / STA $00 / JSR C2/09C5` -- the two
  // rebuilds leave their direct-page words (owRebuildScratch), the second over the first.
  if (ram) { owRebuildScratch(ram, 0); owRebuildScratch(ram, 1); }
}
// The picture's own tilemaps around the camera (the widescreen's sides): the same build, read-only on the RAM.
void CtWorldTilemapView(CtWorldStream* st, const uint8_t* ram, const CtWorldCam* c) {
  if (!ram) return;
  for (int bg = 0; bg < 2; bg++) owTilemapBuildBg(st, ram, c, bg);
}
static void owTilemapBuildBg(CtWorldStream* st, const uint8_t* ram, const CtWorldCam* c, int bg) {
  {
    uint16_t H = bg ? c->bg2h : c->bg1h, V = bg ? c->bg2v : c->bg1v;
    int baseC = (H >> 3) - 15, baseR = (V >> 3) - 2;
    for (int i = 0; i < 64; i++) {
      int wcol = ((baseC + i) % 192 + 192) % 192;
      int tc = (baseC + i) & 0x3F;
      int adj = (tc & 0x1F) + ((tc & 0x20) << 5);
      for (int j = 0; j < 32; j++) {
        int wrow = ((baseR + j) % 128 + 128) % 128;
        int tr = (baseR + j) & 0x1F;
        int block = ram[OW_GRID[bg] + (wrow >> 1) * 96 + (wcol >> 1)];
        st->tilemap[bg][adj + tr * 0x20] =
            owDefWord(ram, bg, block, ((wrow & 1) * 2 + (wcol & 1)) * 2);
      }
    }
    st->e3[bg] = (uint16_t)(H >> 3);
    st->e7[bg] = (uint16_t)(V >> 3);
  }
}

static void owStageWrite(CtWorldStream* st, int off, uint16_t w) {
  st->staging[st->stageOff + off] = (uint8_t)w;
  st->staging[st->stageOff + off + 1] = (uint8_t)(w >> 8);
}
// THE STREAMING'S RAM, AS THE ROM LEAVES IT. The steppers (C2/0568 / C2/066C and their pixel loops), the writers
// (C2/0773 / C2/07C2 / C2/0843 / C2/088F with C2/0811 / C2/08DB) and the queues (C2/090D / C2/0961) work through the
// direct page, write the staged words through `STA ($06),Y` with $06 = $E0 (bank $7E), and append the DMA records to
// the NMI's one queue at $60 + $5F -- the queue anim op 6 (C2/0F00) appends to and C2/0405 (CtWorldNmiVramQueue)
// drains. The port delivers the maps through its own list (st->pend), and ALSO keeps every one of those bytes where
// the ROM keeps them: C3/02DD (the seed) folds $0008 into $0386 when the world's window object calls it later in the
// same pass (sequence_table.md 11.13) -- MEASURED, the attract's 1F5 leg, seed call 10 reads $0008 = $641F, the column
// queue's VRAM word (C2/0921).
static unsigned owRomW(unsigned bankC2off);        // (with g_owRom, below)
static void owW16r(uint8_t* ram, int a, unsigned v) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); }
static unsigned owR16r(const uint8_t* ram, int a) { return (unsigned)(ram[a] | (ram[a + 1] << 8)); }
static void owStageRam(uint8_t* ram, int y, uint16_t w) {           // `STA ($06),Y`, $06 = $E0, DB = $7E
  unsigned a = (owR16r(ram, 0x06) + (unsigned)y) & 0xFFFF;
  ram[a] = (uint8_t)w; ram[(a + 1) & 0xFFFF] = (uint8_t)(w >> 8);
}

// Column writer C2/0773 (left tile-col: chips +0/+4) / C2/07C2 (right: +2/+6) + queue C2/090D.
// 30 words into the 64-byte staging slot at offsets ((bgV-8)/8 + k)*2 mod 64; the DMA then
// uploads the WHOLE slot to VRAM column (pixX/8 mod 64) rows 0-31 (VMAIN 0x81).
static void owStreamCol(CtWorldStream* st, uint8_t* ram, int bg, int mcol, int mrow,
                        int offTop, int offBot, uint16_t vshadow, uint16_t pixX) {
  int gridRow = mrow * 0x60;
  int gridCol = OW_GRID[bg] + mcol;
  int phase = (st->e7[bg] & 1) ? 0 : 1;            // C2/082D: sub-row alignment
  int so = ((vshadow - 8) & 0xF8) >> 2;
  // C2/0811: $19 = $0A << 6, $04 = $0A * 96, $02 = $13 + $08, $06 = $E0, $08 = the phase, $0A = the slot
  owW16r(ram, 0x19, (unsigned)(mrow << 6) & 0xFFFF); owW16r(ram, 0x04, (unsigned)gridRow);
  owW16r(ram, 0x02, (owR16r(ram, 0x13) + (unsigned)mcol) & 0xFFFF); owW16r(ram, 0x06, owR16r(ram, 0xE0));
  owW16r(ram, 0x08, (unsigned)phase); owW16r(ram, 0x0A, (unsigned)so);
  owW16r(ram, 0x19, 0x1E);                         // C2/077F / 07CE LDA #001E / STA $19
  for (int i = 0; i < 30; i++) {
    int block = ram[gridCol + gridRow];
    uint16_t w;
    if ((phase & 1) == 0) w = owDefWord(ram, bg, block, offTop);
    else { w = owDefWord(ram, bg, block, offBot); gridRow += 0x60; if (gridRow >= 0x1800) gridRow = 0; }
    owStageWrite(st, so, w);
    owStageRam(ram, so, w);                        // `STA ($06),Y`
    so = (so + 2) & 0x3F;
    phase++;
  }
  owW16r(ram, 0x04, (unsigned)gridRow); owW16r(ram, 0x0A, (unsigned)so);   // the loop's $04 / $0A / $08 / $19
  owW16r(ram, 0x08, (unsigned)phase); owW16r(ram, 0x19, 0);
  int col = (pixX >> 3) & 0x3F;
  int adj = col < 0x20 ? col : col + 0x3E0;        // second 32x32 screen at +0x400
  { // C2/090D: $08 = the VRAM word (+ $16), then one record at $60 + $5F: source $E0 (bank 7E), the word, $40 bytes, VMAIN $81
    unsigned vw = ((unsigned)adj + owR16r(ram, 0x16)) & 0xFFFF;
    owW16r(ram, 0x08, vw);
    uint8_t x = ram[0x5F];
    ram[(uint8_t)(0x62 + x)] = 0x7E; ram[(uint8_t)(0x67 + x)] = 0x81;
    unsigned e0 = owR16r(ram, 0xE0);
    ram[(uint8_t)(0x60 + x)] = (uint8_t)e0; ram[(uint8_t)(0x61 + x)] = (uint8_t)(e0 >> 8);
    ram[(uint8_t)(0x63 + x)] = (uint8_t)vw; ram[(uint8_t)(0x64 + x)] = (uint8_t)(vw >> 8);
    ram[(uint8_t)(0x65 + x)] = 0x40; ram[(uint8_t)(0x66 + x)] = 0x00;
    ram[0x5F] = (uint8_t)(x + 8); ram[0x5E] = 0;   // 094E / 0951 (the INC $5E at 0925 undone by the STZ)
    owW16r(ram, 0xE0, (e0 + 0x40) & 0xFFFF);        // 0956-095D
  }
  if (st->nPend < 8) {
    st->pend[st->nPend].bg = (uint8_t)bg; st->pend[st->nPend].isRow = 0;
    st->pend[st->nPend].addr = (uint16_t)adj; st->pend[st->nPend].stage = st->stageOff;
    st->nPend++;
  }
  st->stageOff += 0x40;
}

// Row writer C2/0843 (top tile-row: chips +0/+2) / C2/088F (bottom: +4/+6) + queue C2/0961.
// 34 words into the 128-byte slot at offsets ((bgH-8)/8 + k)*2 mod 128; the DMA uploads bytes
// 0-0x3F to the row in screen 0 and 0x40-0x7F to screen 1 (two entries, VMAIN 0x80).
static void owStreamRow(CtWorldStream* st, uint8_t* ram, int bg, int mcolStart, int mrow,
                        int offL, int offR, uint16_t hshadow, uint16_t pixY) {
  int g4 = mcolStart;
  int rowPtr = OW_GRID[bg] + mrow * 0x60;
  int phase = (st->e3[bg] & 1) ? 0 : 1;            // C2/08F7: sub-col alignment
  int so = ((hshadow - 8) & 0x1F8) >> 2;
  // C2/08DB: $04 = $08, $19 = $0A << 6, $02 = $0A * 96 + $13, $06 = $E0, $08 = the phase, $0A = the slot
  owW16r(ram, 0x04, (unsigned)g4); owW16r(ram, 0x19, (unsigned)(mrow << 6) & 0xFFFF);
  owW16r(ram, 0x02, ((unsigned)(mrow * 96) + owR16r(ram, 0x13)) & 0xFFFF); owW16r(ram, 0x06, owR16r(ram, 0xE0));
  owW16r(ram, 0x08, (unsigned)phase); owW16r(ram, 0x0A, (unsigned)so);
  owW16r(ram, 0x19, 0x22);                         // C2/084F / 089B LDA #0022 / STA $19
  for (int i = 0; i < 34; i++) {
    int block = ram[rowPtr + g4];
    uint16_t w;
    if ((phase & 1) == 0) w = owDefWord(ram, bg, block, offL);
    else { w = owDefWord(ram, bg, block, offR); g4++; if (g4 >= 0x60) g4 = 0; }
    owStageWrite(st, so, w);
    owStageRam(ram, so, w);                        // `STA ($06),Y`
    so = (so + 2) & 0x7F;
    phase++;
  }
  owW16r(ram, 0x04, (unsigned)g4); owW16r(ram, 0x0A, (unsigned)so);
  owW16r(ram, 0x08, (unsigned)phase); owW16r(ram, 0x19, 0);
  int base = (pixY & 0xF8) << 2;                   // row*32 words (mod 32 rows)
  { // C2/0961: $08 = the word (+ $16), $0A = $08 + $400; TWO records (the two screens), VMAIN $80, $40 bytes each
    unsigned w0 = ((unsigned)base + owR16r(ram, 0x16)) & 0xFFFF, w1 = (w0 + 0x400) & 0xFFFF;
    owW16r(ram, 0x08, w0); owW16r(ram, 0x0A, w1);
    uint8_t x = ram[0x5F];
    unsigned e0 = owR16r(ram, 0xE0);
    ram[(uint8_t)(0x62 + x)] = 0x7E; ram[(uint8_t)(0x6A + x)] = 0x7E;
    ram[(uint8_t)(0x67 + x)] = 0x80; ram[(uint8_t)(0x6F + x)] = 0x80;
    ram[(uint8_t)(0x65 + x)] = 0x40; ram[(uint8_t)(0x66 + x)] = 0x00;
    ram[(uint8_t)(0x6D + x)] = 0x40; ram[(uint8_t)(0x6E + x)] = 0x00;
    ram[(uint8_t)(0x60 + x)] = (uint8_t)e0; ram[(uint8_t)(0x61 + x)] = (uint8_t)(e0 >> 8);
    ram[(uint8_t)(0x68 + x)] = (uint8_t)(e0 + 0x40); ram[(uint8_t)(0x69 + x)] = (uint8_t)((e0 + 0x40) >> 8);
    ram[(uint8_t)(0x63 + x)] = (uint8_t)w0; ram[(uint8_t)(0x64 + x)] = (uint8_t)(w0 >> 8);
    ram[(uint8_t)(0x6B + x)] = (uint8_t)w1; ram[(uint8_t)(0x6C + x)] = (uint8_t)(w1 >> 8);
    ram[0x5F] = (uint8_t)(x + 0x10); ram[0x5E] = 0;   // 09B2 / 09B5
    owW16r(ram, 0xE0, (e0 + 0x80) & 0xFFFF);          // 09BA-09C1
  }
  if (st->nPend < 8) {
    st->pend[st->nPend].bg = (uint8_t)bg; st->pend[st->nPend].isRow = 1;
    st->pend[st->nPend].addr = (uint16_t)base; st->pend[st->nPend].stage = st->stageOff;
    st->nPend++;
  }
  st->stageOff += 0x80;
}

// The dispatcher's entry, as its callers make it (C2/468A.. / C2/168A.. / C2/3702..: `STA $01` the step, `STA $00` the
// layer, then C2/0568 / C2/066C): $0E = (layer - 1) * 2 and the three table words, every call -- a zero step included.
static void owDispatchScratch(uint8_t* ram, int bg, int step) {
  ram[0x00] = (uint8_t)(bg + 1); ram[0x01] = (uint8_t)step;
  int x = bg * 2;
  owW16r(ram, 0x0E, (unsigned)x);                                    // 0571-0573 DEC / ASL / STA $0E
  owW16r(ram, 0x10, owRomW(0x0556 + x)); owW16r(ram, 0x13, owRomW(0x055C + x)); owW16r(ram, 0x16, owRomW(0x0562 + x));
}
static void owCellPut(uint8_t* ram, int a, unsigned v) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); }
// H scroll-step loop with streaming (C2/0607 inc / C2/059D dec).
static void owStepH(CtWorldStream* st, CtWorldCam* c, uint8_t* ram, int bg, int step) {
  uint16_t* h = bg == 0 ? &c->bg1h : &c->bg2h;
  uint16_t v = bg == 0 ? c->bg1v : c->bg2v;
  int n = step < 0 ? -step : step, x = bg * 2;
  if (n) owW16r(ram, 0x0C, (unsigned)n);           // 05A6 / 060C STA $0C
  for (int i = 0; i < n; i++) {
    if (step > 0) {
      uint16_t val = (uint16_t)(*h + 1); if (val >= 0x600) val = 0;
      *h = val; owCellPut(ram, 0x20 + x, val);     // 0619 STA $20,X
      if ((val & 7) == 0) {                        // C2/0625: cursor++ + stream the right-entering col
        st->e3[bg]++; if (st->e3[bg] >= 0xC0) st->e3[bg] = 0;
        owCellPut(ram, 0xE3 + x, st->e3[bg]);      // 062F STA $00E3,X
        int colpx8 = st->e3[bg] + 0x20; if (colpx8 >= 0xC0) colpx8 -= 0xC0;
        int r = (int)st->e7[bg] - 1; if (r < 0) r = 0x7F;
        owW16r(ram, 0x08, (unsigned)(colpx8 >> 1)); owW16r(ram, 0x0A, (unsigned)(r >> 1));   // 0640 / 064C
        if ((val & 8) == 0) owStreamCol(st, ram, bg, colpx8 >> 1, r >> 1, 0, 4, v, (uint16_t)(val + 0x100));
        else                owStreamCol(st, ram, bg, colpx8 >> 1, r >> 1, 2, 6, v, (uint16_t)(val + 0x100));
      }
    } else {
      if ((*h & 7) == 0) { int e = (int)st->e3[bg] - 1; if (e < 0) e = 0xBF; st->e3[bg] = (uint16_t)e;
                           owCellPut(ram, 0xE3 + x, st->e3[bg]); }            // 05BA STA $00E3,X
      int sval = (int)*h - 1; if (sval < 0) sval = 0x5FF;
      uint16_t val = (uint16_t)sval;
      *h = val; owCellPut(ram, 0x20 + x, val);     // 05C5 STA $20,X
      if ((val & 7) == 0) {                        // C2/05D1: stream the left-entering col
        int cc = (int)st->e3[bg] - 1; if (cc < 0) cc = 0xBF;
        int r = (int)st->e7[bg] - 1; if (r < 0) r = 0x7F;
        owW16r(ram, 0x08, (unsigned)(cc >> 1)); owW16r(ram, 0x0A, (unsigned)(r >> 1));        // 05DB / 05E7
        if ((val & 8) != 0) owStreamCol(st, ram, bg, cc >> 1, r >> 1, 0, 4, v, (uint16_t)(val - 8));
        else                owStreamCol(st, ram, bg, cc >> 1, r >> 1, 2, 6, v, (uint16_t)(val - 8));
      }
    }
    owW16r(ram, 0x0C, (unsigned)(n - 1 - i));      // 05CC / 0620 DEC $0C
  }
}

// V scroll-step loop with streaming (C2/070B inc / C2/06A1 dec).
static void owStepV(CtWorldStream* st, CtWorldCam* c, uint8_t* ram, int bg, int step) {
  uint16_t* v = bg == 0 ? &c->bg1v : &c->bg2v;
  uint16_t h = bg == 0 ? c->bg1h : c->bg2h;
  int n = step < 0 ? -step : step, x = bg * 2;
  if (n) owW16r(ram, 0x0C, (unsigned)n);           // 06AA / 0710 STA $0C
  for (int i = 0; i < n; i++) {
    if (step > 0) {
      uint16_t val = (uint16_t)(*v + 1); if (val >= 0x400) val = 0;
      *v = val; owCellPut(ram, 0x26 + x, val);     // 071D STA $26,X
      if ((val & 7) == 0) {                        // C2/0729: cursor++ + stream the bottom-entering row
        st->e7[bg]++; if (st->e7[bg] >= 0x80) st->e7[bg] = 0;
        owCellPut(ram, 0xE7 + x, st->e7[bg]);      // 0733 STA $00E7,X
        int cc = (int)st->e3[bg] - 1; if (cc < 0) cc = 0xBF;
        int rr = (int)st->e7[bg] + 0x1C; if (rr >= 0x80) rr -= 0x80;
        owW16r(ram, 0x08, (unsigned)(cc >> 1)); owW16r(ram, 0x0A, (unsigned)(rr >> 1));       // 0740 / 0753
        if ((val & 8) == 0) owStreamRow(st, ram, bg, cc >> 1, rr >> 1, 0, 2, h, (uint16_t)(val + 0xE0));
        else                owStreamRow(st, ram, bg, cc >> 1, rr >> 1, 4, 6, h, (uint16_t)(val + 0xE0));
      }
    } else {
      if ((*v & 7) == 0) { int e = (int)st->e7[bg] - 1; if (e < 0) e = 0x7F; st->e7[bg] = (uint16_t)e;
                           owCellPut(ram, 0xE7 + x, st->e7[bg]); }            // 06BE STA $00E7,X
      int sval = (int)*v - 1; if (sval < 0) sval = 0x3FF;
      uint16_t val = (uint16_t)sval;
      *v = val; owCellPut(ram, 0x26 + x, val);     // 06C9 STA $26,X
      if ((val & 7) == 0) {                        // C2/06D5: stream the top-entering row (halves inverted)
        int cc = (int)st->e3[bg] - 1; if (cc < 0) cc = 0xBF;
        int rr = (int)st->e7[bg] - 1; if (rr < 0) rr = 0x7F;
        owW16r(ram, 0x08, (unsigned)(cc >> 1)); owW16r(ram, 0x0A, (unsigned)(rr >> 1));       // 06DF / 06EB
        if ((val & 8) == 0) owStreamRow(st, ram, bg, cc >> 1, rr >> 1, 4, 6, h, (uint16_t)(val - 8));
        else                owStreamRow(st, ram, bg, cc >> 1, rr >> 1, 0, 2, h, (uint16_t)(val - 8));
      }
    }
    owW16r(ram, 0x0C, (unsigned)(n - 1 - i));      // 06D0 / 0724 DEC $0C
  }
}

// ---------------------------------------------------------------------------
// The ambient-object script VM (see world.h). Helpers operate on an object block B (a pointer
// into ram at $0B30 + n*0x40) with the script blob mapped at ram[0x10000 + ptr] (bank $7F).
static uint16_t obW(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void obPutW(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void obAdd32(uint8_t* frac, uint8_t* px, uint16_t vfrac, uint16_t vint) {
  uint32_t f = obW(frac) + vfrac;
  obPutW(frac, (uint16_t)f);
  obPutW(px, (uint16_t)(obW(px) + vint + (f >> 16)));
}
static void obWrapWorld(uint8_t* B) {               // C2/1CB8: x into [0,0x600), y &= 0x3FF
  uint16_t x = obW(B + OWOBJ_XPOS);
  if (x & 0x8000) x = (uint16_t)(x + 0x600);
  else if (x >= 0x600) x = (uint16_t)(x - 0x600);
  obPutW(B + OWOBJ_XPOS, x);
  obPutW(B + OWOBJ_YPOS, obW(B + OWOBJ_YPOS) & 0x3FF);
}
static void obNeg32(uint8_t* frac, uint8_t* iw) {   // C2/1C84/1C9E: 32-bit negate
  uint32_t v = obW(frac) | ((uint32_t)obW(iw) << 16);
  v = 0u - v;
  obPutW(frac, (uint16_t)v); obPutW(iw, (uint16_t)(v >> 16));
}
static void obIntegrate(uint8_t* B) {               // C2/0F38 both axes + world wrap
  obAdd32(B + OWOBJ_XFRAC, B + OWOBJ_XPOS, obW(B + OWOBJ_VELXF), obW(B + OWOBJ_VELXI));
  obAdd32(B + OWOBJ_YFRAC, B + OWOBJ_YPOS, obW(B + OWOBJ_VELYF), obW(B + OWOBJ_VELYI));
  obWrapWorld(B);
}
// C2/0E1D -- the ANIM SUB-VM, ported (this used to be a free-running "flap counter" approximation).
// The interpreter itself (C2/0E1D-0E49): $52/$54 = a long pointer built from the object's own
// +0B/+0C (script) and +0D (bank); fetch the opcode byte, dispatch through the 8-entry table at
// C2/0E4A, and use the handler's RETURN VALUE as the number of bytes to ADVANCE -- a returned ZERO
// stops the pass and PARKS the pointer on that opcode (that is how a frame is held across frames).
// The advanced pointer is written back to +0B on exit.
//
// The eight handlers, byte-for-byte:
//   0 (0E5A) len 2: object byte at [operand] := 0        (TDC / STA ($4E),Y)
//   1 (0E6B) len 2: object byte at [operand] += 1
//   2 (0E7E) len 2: object byte at [operand] -= 1
//   3 (0E91) len = the operand SIGN-EXTENDED (BIT #$0080 / ORA #FF00) -> a relative JUMP; the
//            interpreter's `CLC / ADC $52` makes a negative value loop the sequence
//   4 (0EA3) len 4: SHOW A FRAME. +0F bit6 set = HOLD (park, return 0). Else if the dwell +0E is 0
//            it is LOADED from the operand at +3 and the op parks; each later visit DECs it and
//            parks; when the DEC reaches 0 the op returns 4 and the script advances. The frame
//            itself is the 16-bit param at +1 (a bank-$54 sprite-descriptor pointer staged by
//            C2/0B53) -- see the render note below.
//   5 (0EDC) len 2: WAIT -- same dwell protocol as op 4 without staging a frame.
//   6 (0F00) / 7 (0F35): the particle/child spawners ($5E/$5F staging). Not reached by any sequence
//            in the C3:E000 table this engine runs (gulls/boats/fair objects use 0/1/3/4 only), so
//            they PARK here rather than being guessed at -- an unreached op must not be invented.
//
// RENDER NOTE: the frame pointer op 4 carries (E1FA/E20E/E213... per gull facing) is the canonical
// frame identity, and +26 (op 1/0 around the up-stroke) is the sequence's own flip/attr flag. The
// captured-atlas picker still keys on OWOBJ_ANIMCTR, which is kept ticking for it.
// ===================== THE PER-FRAME DRAW LIST (C2/0C4D -> C2/0B53 -> C2/0CEA) =====================
// The ROM does NOT render an object because it holds an anim pointer -- it renders whatever is on the
// frame's DRAW LIST, and the only thing that puts an object there is the anim sub-VM's op-4 handler:
//     C2/0EA3  ... 0EC9  LDA $54 / STA $57 / REP #20 / LDY #01 / LDA [$52],Y / STA $55
//                        JSR $0B53          <- ENQUEUE (the HOLD path and the dwell PARK path)
//                        TDC / CLC / RTS    <- advance 0 = park on this op
// (the DEC that expires returns 4 at C2/0EC2 WITHOUT enqueuing -- that frame is over). C2/0B53 unlinks
// a node from the free list and copies the object's X (+14), Y (+18), attr (+0F), tile base (+10) and
// the descriptor pointer ($55) into it, Y-sorted; C2/032B empties the list every frame (C2/0C4D: head
// $B500 self-linked, the 64 nodes at $B000 re-threaded from the $B400 template) and C2/0336 drains it
// into the OAM shadow (C2/0CEA -> the decoder C2/0D08).
//
// So VISIBILITY == "did this object's anim sub-VM run this frame". A script yielding on op 0x38 waits
// WITHOUT the C2/0E1D tick (op 0x39 is the same wait WITH it -- C2/188E's extra `JSR C2/0E1D`), so it
// enqueues nothing and emits no OAM, while its +0B pointer keeps naming a perfectly good frame.
// This port kept only the pointer, so every ambient object drew on every frame. Measured on the oracle
// (--peek saves/ow_w0.state): walking, the era-placard block $0D30 is pc=$06A3 anim=$F836 and its three
// OAM entries are ABSENT; after 200 idle frames it is pc=$069E, anim=$F836 UNCHANGED, and OAM carries
// $1AC@252 $1A6@12 $18A@28. Nothing in the block distinguishes the two -- only the enqueue does.
//
// The port keeps the enqueue's RESULT per block rather than the linked list: the renderer reads the
// object's live position out of the block it is already walking (same frame, same values the node
// snapshots), so what the list adds is the visibility gate and the descriptor. The list ORDER (the
// Y-sort at C2/0B98) is not reproduced -- this host has its own draw order and no OAM table.
#define OW_DRAWLIST_N  ((OWOBJ_END - OWOBJ_BASE) / OWOBJ_STRIDE)
static uint16_t g_owShownDesc[OW_DRAWLIST_N];   // the descriptor each block enqueued this frame
static uint8_t  g_owShown[OW_DRAWLIST_N];       // 1 = on the list this frame
// ⭐ ...and the list ITSELF, in WRAM where the ROM keeps it, because its ORDER is the OAM order and the OAM order
// is which sprite is in front. 64 nodes of 16 bytes at $7E:B000 -- +00 prev, +02 next, +04 X, +06 Y, +08 attr
// (the block's +0F), +09 tile base (+10, a word), +0B descriptor (a word), +0D its bank -- the head at $B500
// (self-linked when empty), the free list at $B504, and at $B400 the prev/next TEMPLATE the reset copies back.
#define OWL_HEAD 0xB500
#define OWL_FREE 0xB504
static uint16_t owlW(const uint8_t* ram, uint16_t a) { return (uint16_t)(ram[a] | (ram[a + 1] << 8)); }
static void owlPut(uint8_t* ram, uint16_t a, uint16_t v) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); }
void CtWorldDrawListInit(uint8_t* ram) {        // C2/0BF6, the world load (C2:23AE): the template at $B400
  if (!ram) return;
  owlPut(ram, 0xB400, OWL_FREE);                // 0BFD: node 0's prev is the free-list head
  for (int i = 1; i < 64; i++) owlPut(ram, (uint16_t)(0xB400 + i * 4), (uint16_t)(0xB000 + (i - 1) * 0x10));
  for (int i = 0; i < 63; i++) owlPut(ram, (uint16_t)(0xB402 + i * 4), (uint16_t)(0xB010 + i * 0x10));
  owlPut(ram, (uint16_t)(0xB402 + 63 * 4), OWL_FREE);   // 0C45: the last node's next is the free-list head
}
void CtWorldDrawListReset(uint8_t* ram) {       // C2/0C4D, at C2/032B
  memset(g_owShown, 0, sizeof g_owShown);
  memset(g_owShownDesc, 0, sizeof g_owShownDesc);
  if (!ram) return;
  owlPut(ram, OWL_HEAD, OWL_HEAD); owlPut(ram, OWL_HEAD + 2, OWL_HEAD);   // 0C54: the list, empty
  owlPut(ram, OWL_FREE, 0xB3F0); owlPut(ram, OWL_FREE + 2, 0xB000);       // 0C5E: every node free
  for (int i = 0; i < 64; i++) {                // 0C6D: prev/next of all 64 nodes from the template
    owlPut(ram, (uint16_t)(0xB000 + i * 0x10), owlW(ram, (uint16_t)(0xB400 + i * 4)));
    owlPut(ram, (uint16_t)(0xB002 + i * 0x10), owlW(ram, (uint16_t)(0xB402 + i * 4)));
  }
  ram[0x08] = 0; ram[0x09] = 0;                 // 0C73 `LDA #0008 / STA $08` .. 0CE4 `DEC $08 / BNE`: eight rows, 0000 left
}
static int owBlockIndex(const uint8_t* ram, const uint8_t* B) {
  if (!ram || !B) return -1;
  ptrdiff_t off = B - (ram + OWOBJ_BASE);
  if (off < 0 || off >= (OWOBJ_END - OWOBJ_BASE) || (off % OWOBJ_STRIDE)) return -1;
  return (int)(off / OWOBJ_STRIDE);
}
static int16_t g_owNodeBlk[64];                  // (the picture's record) the object block each list node was filled from
static void owDrawListEnqueue(uint8_t* ram, const uint8_t* B, uint16_t desc) {   // C2/0B53
  int i = owBlockIndex(ram, B);
  if (i < 0) return;
  g_owShown[i] = 1; g_owShownDesc[i] = desc;
  // 0B5C: unlink the free list's first node
  uint16_t x = owlW(ram, OWL_FREE + 2);
  if (x == OWL_FREE) return;                               // all 64 nodes in use (the ROM does not check)
  if (x >= 0xB000 && x < 0xB400) g_owNodeBlk[(x - 0xB000) >> 4] = (int16_t)i;
  uint16_t y = owlW(ram, x);                               // 0B62 LDY $0000,X   (its prev)
  owlPut(ram, (uint16_t)(y + 2), owlW(ram, (uint16_t)(x + 2)));   // 0B65 prev.next = x.next
  y = owlW(ram, (uint16_t)(x + 2));
  owlPut(ram, y, owlW(ram, x));                            // 0B6C next.prev = x.prev
  // 0B72: the object's position, descriptor, tile base, bank and attr into the node
  owlPut(ram, (uint16_t)(x + 4), (uint16_t)(B[OWOBJ_XPOS] | (B[OWOBJ_XPOS + 1] << 8)));
  owlPut(ram, (uint16_t)(x + 6), (uint16_t)(B[OWOBJ_YPOS] | (B[OWOBJ_YPOS + 1] << 8)));
  owlPut(ram, (uint16_t)(x + 0x0B), desc);
  owlPut(ram, (uint16_t)(x + 0x09), (uint16_t)(B[0x10] | (B[0x11] << 8)));
  ram[x + 0x0D] = B[OWOBJ_ANIMBK];                         // 0B8D LDA $57 -- the anim bank C2/0EC9 staged
  ram[x + 0x08] = B[OWOBJ_ATTR];                           // 0B92
  // 0B98: where it goes. Attr bit 0 = a FRONT node: after the other front nodes, before everything else.
  // Otherwise a set bit 7 of the tile base's high byte sends it to the TAIL; the rest are kept in DESCENDING Y
  // (the later of two equal Ys goes behind), skipping front nodes.
  // (the ROM's walks are unbounded; a corrupted list would hang the cartridge -- here they stop after the 64 nodes)
  y = OWL_HEAD;
  if (ram[x + 0x08] & 0x01) {
    for (int g = 0; g < 64; g++) { y = owlW(ram, (uint16_t)(y + 2)); if (y == OWL_HEAD || !(owlW(ram, (uint16_t)(y + 8)) & 1)) break; }
  } else if (!(ram[x + 0x0A] & 0x80)) {
    for (int g = 0; g < 64; g++) {
      y = owlW(ram, (uint16_t)(y + 2));
      if (y == OWL_HEAD) break;
      if (owlW(ram, (uint16_t)(y + 8)) & 1) continue;
      uint16_t ny = owlW(ram, (uint16_t)(x + 6)), oy = owlW(ram, (uint16_t)(y + 6));
      if (ny == oy || ny < oy) continue;                 // 0BD5 BEQ / 0BD7 BCC
      break;
    }
  }
  // 0BD9: insert x before y
  uint16_t p = owlW(ram, y);
  owlPut(ram, (uint16_t)(x + 2), owlW(ram, (uint16_t)(p + 2)));
  owlPut(ram, (uint16_t)(p + 2), x);
  y = owlW(ram, (uint16_t)(x + 2));
  owlPut(ram, x, owlW(ram, y));
  owlPut(ram, y, x);
}

// ===================== THE OAM SHADOW (C2/034D, C2/0CEA -> C2/0D08) =====================
// C2/032E `STZ $4D` and C2/0330 `JSR C2/034D` park every OAM entry (Y = $E0, high table 0); after the object pass
// C2/0336 `JSR C2/0CEA` walks the list from the head and C2/0D08 decodes each node's descriptor into entries at
// $7E:0700 + 4*$4D: a descriptor is [count] then count x {s8 dX, s8 dY, word2}; the sprite is at the node's X/Y plus
// dX/dY -- minus the BG2 scroll ($22/$28, wrapped into -$300..$2FF / -$200..$1FF on the $600 x $400 torus) when attr
// bit 7 says the object lives in world coordinates -- and is dropped when it is not inside x -15..255, y -15..223.
// The OAM word is word2 + the tile base, OR the attr's bits 1-5 (palette, priority) as the high byte's; the high
// table gets the X's bit 8 and the size, which is attr+tile-base bit 14 (the tile base's high byte, bit 6).
// The next NMI DMAs the whole $0220 bytes to OAM (C2/0184), so the screen always shows the PREVIOUS frame's build.
// THE DIRECT PAGE IT LEAVES. The decoder's working words are scratch, but the NEXT reader of one is not the decoder: C3/02DD
// (the seed) folds $0008 into $0386 when the field's window wipe runs after the world (16E, the attract), so they are
// written here as the ROM writes them (all sixteen-bit but $00/$13/$16/$57, and in its order -- `STA $09` overwrites the
// high byte `STA $08` put there, and `LSR $05` halves the X's high byte `STA $04` left).
static void owW16s(uint8_t* ram, int a, unsigned v) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); }
static int g_owOamKey[128];
static int16_t g_owOamPos[128][2];
const int16_t* CtWorldOamObjPos(void) { return &g_owOamPos[0][0]; }
static CtWorldPiece g_owWide[CT_OW_WIDE_MAX];
static int g_owWideN;
const int* CtWorldOamKeys(void) { return g_owOamKey; }
int CtWorldOamWide(const CtWorldPiece** out) { *out = g_owWide; return g_owWideN; }
void CtWorldOamBuild(uint8_t* ram, uint16_t scrollH, uint16_t scrollV) {   // $22 / $28 = CtWorldCam bg2h / bg2v
  if (!ram) return;
  for (int i = 0; i < 128; i++) g_owOamKey[i] = -1;
  g_owWideN = 0;
  ram[0x4D] = 0;                                          // C2/032E STZ $4D (the high byte $4E is the object cursor)
  for (int i = 0; i < 128; i++) ram[0x0701 + i * 4] = 0xE0;   // C2/034D
  memset(ram + 0x0900, 0, 0x20);
  for (uint16_t n = owlW(ram, OWL_HEAD + 2), guard = 0; n != OWL_HEAD && guard < 64; n = owlW(ram, (uint16_t)(n + 2)), guard++) {
    uint16_t desc = owlW(ram, (uint16_t)(n + 0x0B));
    uint8_t bank = ram[n + 0x0D];
    owW16s(ram, 0x10, n);                                  // 0CFA STX $10
    owW16s(ram, 0x55, desc); ram[0x57] = bank;            // 0D08 LDY $000B,X / STY $55 ; 0D0D LDA $000D,X / STA $57
    const uint8_t* d = owAnimPtr(ram, bank, desc);
    if (!d) continue;
    int count = d[0];
    ram[0x00] = (uint8_t)count; ram[0x01] = 0;             // 0D12 LDA [$55] / STA $00 / STZ $01
    uint8_t size = (ram[n + 0x0A] & 0x40) ? 1 : 0;        // 0D1B LDA $000A,X / BIT #40 / INY -> $13
    owW16s(ram, 0x13, size);                               // 0D23 STY $13 (sixteen-bit Y)
    owW16s(ram, 0x55, (uint16_t)(desc + 1));               // 0D27 INC $55
    uint16_t palpri = (uint16_t)((ram[n + 0x08] << 8) & 0x3E00);   // 0D29 LDA $08,X / XBA / AND #3E00
    uint16_t base = (uint16_t)(owlW(ram, (uint16_t)(n + 0x09)) & 0x3FFF);
    owW16s(ram, 0x02, palpri); owW16s(ram, 0x06, base);    // 0D30 STA $02 ; 0D38 STA $06
    int16_t sx0 = (int16_t)owlW(ram, (uint16_t)(n + 4)), sy0 = (int16_t)owlW(ram, (uint16_t)(n + 6));
    uint16_t cx = (uint16_t)sx0, cy = (uint16_t)sy0;
    if (ram[n + 0x08] & 0x80) {                            // 0D44 BIT #0080: world coordinates
      uint16_t v = (uint16_t)(cx - scrollH);
      if (v & 0x8000) { if (v < 0xFD00) v = (uint16_t)(v + 0x600); }     // 0D51 BPL / CMP #FD00 / BCS / ADC #0600
      else if (v >= 0x0300) v = (uint16_t)(v - 0x600);                    // 0D5D CMP #0300 / BCC / SBC #0600
      cx = v;
      v = (uint16_t)(cy - scrollV);
      if (v & 0x8000) { if (v < 0xFE00) v = (uint16_t)(v + 0x400); }
      else if (v >= 0x0200) v = (uint16_t)(v - 0x400);
      cy = v;
    }
    owW16s(ram, 0x0C, cx); owW16s(ram, 0x0E, cy);          // 0D3D STA $0C / 0D42 STA $0E (0D65 / 0D80 when wrapped)
    int nodeIx = (n >= 0xB000 && n < 0xB400) ? (n - 0xB000) >> 4 : -1;
    int blk = nodeIx >= 0 ? g_owNodeBlk[nodeIx] : -1;
    for (int k = 0; k < count; k++) {
      const uint8_t* q = owAnimPtr(ram, bank, (uint16_t)(desc + 1 + k * 4));
      if (!q) break;
      { int key = blk >= 0 ? blk : 64 + (nodeIx & 63);    // (the picture's record: no RAM) the object, not the piece
        int wx = (int16_t)(uint16_t)((int8_t)q[0] + cx), wy = (int16_t)(uint16_t)((int8_t)q[1] + cy);
        if (wx >= -15 - 200 && wx <= 255 + 200 && wy >= -15 && wy <= 223 && g_owWideN < CT_OW_WIDE_MAX) {
          CtWorldPiece* P = &g_owWide[g_owWideN++];
          P->x = (int16_t)wx; P->y = (int16_t)wy; P->size = size; P->key = key; P->ox = (int16_t)cx; P->oy = (int16_t)cy;
          P->word = (uint16_t)(((q[2] | (q[3] << 8)) + base) | ((ram[n + 0x08] << 8) & 0x3E00));
        } }
      uint16_t sx = (uint16_t)((int8_t)q[0] + cx);          // 0D82: sign-extended dX + X -> $04/$08
      owW16s(ram, 0x08, sx); owW16s(ram, 0x04, sx);         // 0D92 STA $08 / 0D94 STA $04
      // 0DFB-0E03 on every piece, drawn or not: `LDA $55 / ADC #0004 / STA $55`, `DEC $00` (sixteen-bit here)
      #define OW_PIECE_TAIL() do { owW16s(ram, 0x55, (uint16_t)(desc + 1 + (k + 1) * 4)); \
                                   owW16s(ram, 0x00, (uint16_t)(count - 1 - k)); } while (0)
      if ((uint16_t)(sx + 0x0F) >= 0x010F) { OW_PIECE_TAIL(); continue; }   // 0D97 CMP #010F / BCS
      uint16_t sy = (uint16_t)((int8_t)q[1] + cy);           // 0DA2 -> $09
      owW16s(ram, 0x09, sy);                                 // 0DB2 STA $09 (over $08's high byte)
      if ((uint16_t)(sy + 0x0F) >= 0x00EF) { OW_PIECE_TAIL(); continue; }   // 0DB5 CMP #00EF / BCS
      uint16_t word = (uint16_t)(((q[2] | (q[3] << 8)) + base) | palpri);   // 0DBE ADC $06 / ORA $02
      int idx = ram[0x4D] & 0x7F;
      g_owOamKey[idx] = blk >= 0 ? blk : 64 + (nodeIx & 63);
      g_owOamPos[idx][0] = (int16_t)cx; g_owOamPos[idx][1] = (int16_t)cy;
      ram[0x0700 + idx * 4] = (uint8_t)sx;                  // 0DD0 STA $0700,X ($08 low = X, $09 = Y)
      ram[0x0701 + idx * 4] = (uint8_t)sy;
      ram[0x0702 + idx * 4] = (uint8_t)word;                // 0DD4 STA $0702,X
      ram[0x0703 + idx * 4] = (uint8_t)(word >> 8);
      int bits = (size << 1) | ((sx >> 8) & 1);             // 0DDD..0DE6: ROL the size, then the X's bit 8
      ram[0x16] = (uint8_t)(size >> 1);                      // 0DD9 LDA $13 / STA $16 ; 0DE1 LSR $16
      ram[0x05] = (uint8_t)(ram[0x05] >> 1);                 // 0DE4 LSR $05 (the X's high byte, from 0D94)
      ram[0x0900 + (ram[0x4D] >> 2)] |= (uint8_t)(bits << ((ram[0x4D] & 3) * 2));   // 0DED LDA $C20E0D,X / ORA
      { uint16_t c4d = (uint16_t)((ram[0x4D] | (ram[0x4E] << 8)) + 1);   // 0DF9 INC $4D (a 16-bit INC)
        ram[0x4D] = (uint8_t)c4d; ram[0x4E] = (uint8_t)(c4d >> 8); }
      OW_PIECE_TAIL();
      #undef OW_PIECE_TAIL
    }
  }
}
int CtWorldObjShown(int blockAddr) {
  int off = blockAddr - OWOBJ_BASE;
  if (off < 0 || off >= (OWOBJ_END - OWOBJ_BASE) || (off % OWOBJ_STRIDE)) return 0;
  return g_owShown[off / OWOBJ_STRIDE];
}
static void obAnimTick(uint8_t* ram, uint8_t* B) {         // C2/0E1D
  B[OWOBJ_ANIMCTR]++;                                      // (kept: the captured-atlas frame picker reads it)
  uint16_t pc = obW(B + OWOBJ_ANIM);
  if (!pc || !B[OWOBJ_ANIMBK]) return;                     // no sequence installed (op 0x30 sets both)
  // ⚠ STILL 64 HERE, ON PURPOSE. This is the ANIM sub-VM (C2/0E1D), a different dispatcher from
  // obRun's, and its loop was never measured -- only obRun's was (C2/0F73). Lifting a budget on the
  // strength of a neighbour's derivation is exactly the kind of guess the mandate forbids.
  for (int guard = 0; guard < 64; guard++) {               // (the ROM loops until a handler returns 0)
    const uint8_t* op = owAnimPtr(ram, B[OWOBJ_ANIMBK], pc);
    if (!op) break;
    int adv;
    switch (op[0]) {
      case 0x00: B[op[1] & 0x3F] = 0; adv = 2; break;                       // C2/0E5A
      case 0x01: B[op[1] & 0x3F]++; adv = 2; break;                         // C2/0E6B
      case 0x02: B[op[1] & 0x3F]--; adv = 2; break;                         // C2/0E7E
      case 0x03: adv = (int8_t)op[1]; break;                                // C2/0E91 (signed jump)
      case 0x04:                                                            // C2/0EA3 SHOW FRAME
        // Every path that PARKS here (adv 0) falls through C2/0EC9 and `JSR C2/0B53` -- the enqueue
        // onto this frame's draw list, which IS the object's visibility. The DEC that reaches 0
        // returns 4 at C2/0EC2 first and enqueues NOTHING.
        if (B[OWOBJ_ATTR] & 0x40) { adv = 0; }                              // 0EAA BIT #40 -> HOLD
        else if (B[OWOBJ_DWELL] == 0) { B[OWOBJ_DWELL] = op[3]; adv = 0; }  // 0EB3 load from +3
        else adv = (--B[OWOBJ_DWELL] == 0) ? 4 : 0;                         // 0EBD DEC / 0EC4 len 4
        if (adv == 0) owDrawListEnqueue(ram, B, (uint16_t)(op[1] | (op[2] << 8)));   // 0ED6 JSR C2/0B53
        break;
      case 0x05:                                                            // C2/0EDC
        if (B[OWOBJ_DWELL] == 0) { B[OWOBJ_DWELL] = op[1]; adv = 0; break; }
        adv = (--B[OWOBJ_DWELL] == 0) ? 2 : 0;
        break;
      case 0x06: {                                                          // C2/0F00: QUEUE A VRAM UPLOAD for the NMI
        // `SEP #30 / LDX $5F / INC $5E / LDA [$52],1 -> $62,X (bank) / LDA #80 -> $67,X (VMAIN) / REP #20 / [$52],2 ->
        // $60,X (source) / [$52],4 -> $63,X (VRAM word) / [$52],6 -> $65,X (bytes) / $5F += 8 / STZ $5E / LDA #0008`.
        // C2/0405 (CtWorldNmiVramQueue) DMAs the queue at the next NMI. The attract 1F0's Epoch starts its anim
        // ($1E -> C3:E582) with it: `$7E:C940` (the loader's C2/27BC packet) to VRAM word $1800. Parking here left
        // the Epoch invisible and its trail drawing tiles $180.. that never arrived.
        uint8_t x = ram[0x5F];
        ram[0x5E]++;
        ram[(uint8_t)(0x62 + x)] = op[1];
        ram[(uint8_t)(0x67 + x)] = 0x80;
        ram[(uint8_t)(0x60 + x)] = op[2]; ram[(uint8_t)(0x61 + x)] = op[3];
        ram[(uint8_t)(0x63 + x)] = op[4]; ram[(uint8_t)(0x64 + x)] = op[5];
        ram[(uint8_t)(0x65 + x)] = op[6]; ram[(uint8_t)(0x66 + x)] = op[7];
        ram[0x5F] = (uint8_t)(x + 8);
        ram[0x5E] = 0;
        adv = 8; break;
      }
      default: adv = 0; break;                                              // op 7 (C2/0F35 `TDC / SEC`): unreached, park
    }
    if (adv == 0) break;                                   // 0E37 BEQ -> park on this opcode
    pc = (uint16_t)(pc + adv);                             // 0E39 CLC / ADC $52
  }
  obPutW(B + OWOBJ_ANIM, pc);                              // 0E42 STA $000B,X
}
// The CURRENT frame of an object's anim sub-VM, decoded from the ROM descriptor its +0B parks on.
// The descriptor format is the decoder C2/0D08's: [u8 pieceCount] then pieceCount x 4 bytes
// { s8 dX, s8 dY, u16 word2 }, where word2 bits0-8 are the tile NAME (added to the object's own
// tile base $10 by C2/0DBE), bits9-13 the palette/priority (zero in ROM data, OR'd in from the
// object's +0F attr), bit14 H-flip and bit15 V-flip. Returns the piece count (0 = the pointer is
// not parked on an op-4 "show frame", e.g. mid-wait), and fills piece 0 -- the whole sprite for the
// 1-piece objects (gulls) whose render anchor this replaces.
int CtWorldAnimDesc(const uint8_t* B) {
  const uint8_t* op = ctWorldAnimRom(B[OWOBJ_ANIMBK], obW(B + OWOBJ_ANIM));
  if (!op || op[0] != 0x04) return -1;
  return op[1] | (op[2] << 8);
}
// A descriptor by ADDRESS (bank $C3), for a consumer that holds a pointer the block has already moved
// past -- the lead's draw is one OAM DMA behind the block (see CtWorldLeadAnimFrame).
int CtWorldAnimDescPiece(int desc, int piece, int* dx, int* dy, int* name, int* flip) {
  if (dx) *dx = 0; if (dy) *dy = 0; if (name) *name = 0; if (flip) *flip = 0;
  if (desc < 0) return 0;
  const uint8_t* d = ctWorldAnimRom(0xC3, (uint16_t)desc);
  if (!d) return 0;
  int n = d[0];
  if (n < 1 || piece < 0 || piece >= n) return n < 1 ? 0 : n;
  const uint8_t* q = ctWorldAnimRom(0xC3, (uint16_t)(desc + 1 + 4 * piece));
  if (!q) return n;
  uint16_t w2 = (uint16_t)(q[2] | (q[3] << 8));
  if (dx) *dx = (int8_t)q[0];
  if (dy) *dy = (int8_t)q[1];
  if (name) *name = w2 & 0x1FF;
  if (flip) *flip = (w2 >> 14) & 3;
  return n;
}

// ===================== THE LEAD BLOCK'S OWN ANIMATION (C2/3404 -> C2/1CDA -> C2/0E1D) =====================
// The overworld player's pose is carried by the LEAD block itself -- the 0x40-byte object whose handler
// word is $3404 (pointer $7E:004E; $0D70 on world 0, $0DF0 on world 1) -- through the same +0B/+0C/+0D
// anim sub-VM pointer every ambient object has, installed by C2/1CDA (`REP #20 / AND #00FF / ASL A / TAX /
// LDA $C3E000,X / LDX $4E / STA $000B,X / SEP #20 / LDA #C3 / STA $000D,X / STZ $000E,X`) from exactly
// five sites of the interactive lead path ($0280 == 0):
//   C2/3549  init (phase 0, C2/34F8): +0F := $A8 (or $B8 when $0100 == $01F5), +10 := 0, +26 := 0,
//            +28 := $0104 & 3 (the arrival facing), then the STAND id;
//   C2/35D7  every no-input frame while $02B1 < $F0: the STAND id again (its script is a single
//            `04 desc 00` = dwell 0 = held forever; the re-install's STZ +0E is invisible);
//   C2/35CD  once, on the frame $02B1 reaches $F0 (240 no-input frames): id $A2, the fidget; $02B1 then
//            parks at $FE (35D0) so the stand re-install stops and the fidget loop runs free;
//   C2/3694  the frame a d-pad direction is pressed at an 8px boundary -- unless (+28 & $7F) == dir AND
//            +28 bit 7 (already walking that way: the walk cycle continues across steps, C2/362A-3630):
//            the WALK id, after +28 := $80 | dir;
//   C2/36DF  the same frame, when the step is BLOCKED (C2/39C3 on the target's corner props): +28 &= $7F
//            and the STAND id again -- so a press into a wall ends the frame on the stand id.
// STAND id = C2:39AA[facing] = {Up 03, Down 00, Left 06, Right 09}; WALK id = C2:397F[facing*2 + (+26 & 1)]
// = {Up 04/05, Down 01/02, Left 07/08, Right 0A/0B}; both +$0C when the lead character $7E2980 is 6 (Magus,
// ids $0C-$17 = the same shape on its own descriptors). The walk scripts INC/ZERO +26 around their two legs
// (`01 26 | leg A 8f | stand 8f | 00 26 | leg B 8f | stand 8f | 03 EC`), which is what makes the NEXT walk
// start on the other leg. The tick is the ordinary C2/0E1D interpreter (obAnimTick), run once per frame by
// the lead handler on every path (35E0 idle, 36E2 blocked, 3731 moving -- after C2/0F38 moved the position).
// A non-interactive module state ($027C != 1: C2/356A) runs only the tick.
// Measured on saves/ow_w0.state (--monitor 7E0D7B:2,7E0D7E:1): idle E14C held; Left held: E18A f11 / E18E
// f19 / E194 f27 / E198 f35 / E18A f43, release -> E184; Right into the coast: E1A4 dwell 8 then E19E held;
// the fidget at f75 ($02B1 $A5 -> $F0): E1B8/E1BC/E1C0/E1C4 (8f each) E1C8 (24f) looping.
// Dossier: disasm/notes/ow_player_sprite.md (rewritten 2026-09-05; commit 99ac511 had taken the coastal
// BOAT block $0F30 for the player).
static const uint8_t kLeadStandId[4] = { 0x03, 0x00, 0x06, 0x09 };                        // C2:39AA
static const uint8_t kLeadWalkId[8]  = { 0x04, 0x05, 0x01, 0x02, 0x07, 0x08, 0x0A, 0x0B };  // C2:397F
static void leadInstall(uint8_t* B, unsigned id) {                     // C2/1CDA
  obPutW(B + OWOBJ_ANIM, CtWorldAnimPtrForId(id));                     // 1CE1 LDA $C3E000,X / STA $000B,X
  B[OWOBJ_ANIMBK] = 0xC3;                                              // 1CEB LDA #C3 / STA $000D,X
  B[OWOBJ_DWELL] = 0;                                                  // 1CF0 STZ $000E,X
}
// ⭐ Is there a lead PC on this overworld AT ALL? On the cartridge the player IS an object --
// class $3404, spawned by the world's own bootstrap on its INTERACTIVE branch -- so a CUTSCENE
// entry has no such block: the attract's world-5 leg holds $0F63 (a script object) at $0DB0
// where interactive play holds $3404, and its mover reads (0,0) (sequence_table.md 9.14).
// ⚠ CtWorldLeadBlock below FALLS BACK to $0D70 when none is tagged, which is right for asking
// "where would the mover seed read" and wrong for "is there a player". Ask this instead.
int CtWorldHasObjClass(const uint8_t* ram, uint16_t cls) {
  for (int b = OWOBJ_BASE; b < OWOBJ_END; b += OWOBJ_STRIDE)
    if ((ram[b] | (ram[b + 1] << 8)) == cls) return 1;
  return 0;
}
int CtWorldHasLead(const uint8_t* ram) { return CtWorldHasObjClass(ram, 0x3404); }

int CtWorldLeadBlock(const uint8_t* ram) {
  // By HANDLER WORD, never by $4E: $4E is only the object-pass cursor (frame-end values $10B0/$7010 on world 0,
  // $1170 on world 1 -- measured), and the ROM's own C2/3404 addresses the lead through it only while it runs.
  for (int b = OWOBJ_BASE; b < OWOBJ_END; b += OWOBJ_STRIDE)
    if ((ram[b] | (ram[b + 1] << 8)) == 0x3404) return b;
  return 0x0D70;                    // world 0's block, and where the mover seed reads ($0D84/$0D88) when none is tagged
}
static void leadInit(uint8_t* ram, uint8_t* B);
static void owDactylProps(uint8_t* ram, uint8_t* B, unsigned px, unsigned py);
void CtWorldLeadAnimFrame(uint8_t* ram, int dir, int phaseBefore, int blocked, int facingInit) {
  uint8_t* B = ram + CtWorldLeadBlock(ram);
  int magus = (ram[0x2980] == 6) ? 0x0C : 0;                           // C2/3971 / 399C: LDA $00 / CMP #06 / ... ADC #0C
  (void)facingInit;
  if (B[OWOBJ_ANIMBK] != 0xC3) leadInit(ram, B);                       // C2/34F8, the init phase, once
  if (dir >= -1 && phaseBefore == 1) {                                 // phase 1 = C2/3567, the input frame
    if (dir < 0) {                                                     // C2/35B3, no button at all
      B[0x28] &= 0x7F;                                                 // 35B5-35BA
      uint8_t c = (uint8_t)(ram[0x02B1] + 1); ram[0x02B1] = c;         // 35BD INC $02B1
      if (c < 0xF0) leadInstall(B, kLeadStandId[B[0x28] & 3] + magus); // 35C3 BCC ->35D7: the STAND, every idle frame
      else { if (c != 0xFF) leadInstall(B, 0xA2); ram[0x02B1] = 0xFE; } // 35C7-35D5: the fidget once, then park at $FE
    } else {
      int cur = B[0x28];
      if (!((cur & 0x7F) == dir && (cur & 0x80))) {                    // 362A CMP #dir / BNE / 362E BMI ->3697
        B[0x28] = (uint8_t)(0x80 | dir);                               // 368B STA $0028,X
        leadInstall(B, kLeadWalkId[dir * 2 + (B[0x26] & 1)] + magus);  // 3694 JSR C2/3953
      }
      ram[0x02B1] = 0;                                                 // 3697 STZ $02B1
      if (blocked) {                                                   // C2/36B6 (C2/39C3 returned CARRY SET:
        B[0x28] &= 0x7F;                                               //  the step was REFUSED -- no STZ $1BF7)
        leadInstall(B, kLeadStandId[B[0x28] & 3] + magus);             // 36DF JSR C2/3987
      } else {
        // 36B4 BCC ->36E7: the step WAS issued, and only then does the ROM zero $1BF7 -- the counter
        // the world's own placard script ramps (op 0x11 at C2/12A1) and gates its SHOW arm on at $78.
        // A refused press does NOT reset it, which is why walking into a cliff still surfaces the
        // placard. Measured: `--monitor 7E1BF7:1 --from saves/ow_w0.state --btnscript 40:80,0:160
        // --diff --writer` -> zeroed by C2:36EA at f1, f9, f17, f25, f33 (the 8-frame step cadence)
        // and INC'd by C2:12A1 on every frame in between, so it never reaches $78 while walking.
        ram[0x1BF7] = 0;                                               // 36E7 STZ $1BF7
      }
    }
  }
  obAnimTick(ram, B);                                                  // 35E0 / 36E2 / 3731 JSR C2/0E1D
}
// C2/34F8 -- the lead's init (its phase 0): on the first frame of a world, and again when the party steps out of a vehicle
// (C2/34AD zeroes the phase; the host calls CtWorldLeadReinit on that edge, field.c).
void CtWorldLeadReinit(uint8_t* ram) { leadInit(ram, ram + CtWorldLeadBlock(ram)); }
static void leadInit(uint8_t* ram, uint8_t* B) {
  int magus = (ram[0x2980] == 6) ? 0x0C : 0;
  {
    int loc = ram[0x0100] | (ram[0x0101] << 8);
    B[OWOBJ_ATTR] = (loc == 0x01F5) ? 0xB8 : 0xA8;                     // 34FD LDY $0100 / CPY #01F5 / LDA #A8|#B8 / STA $000F,X
    B[0x10] = 0; B[0x11] = 0;                                          // 350B STZ $0010,X (tile base 0, 16x16)
    B[0x26] = 0;                                                       // 353A STZ $0026,X
    // 3533 LDA $0104 / AND #03 / STA $0028,X -- the ARRIVAL byte the location commit (C0/0CBC) copied from the exit's
    // $0116, not the host's own facing: `facingInit` was the host's wd.pfac, which worldInit seeds to 1 (down) and which
    // follows +28 only after this init. MEASURED (the user, 2026-10-07: "when leaving the ticket office in porre, crono is
    // facing a different direction"): loc 038's one exit, ROM record E5:06DA `34 2E 01 F0 07 32 77`, stages $0116 =
    // ((07 >> 1) & 0F) = 03; the cartridge's save on the map reads $0104 = 03, +28 = 03, the right stand E19E; the port
    // committed $0104 = 03 and then drew Crono facing down (+28 = 01, E14C) and wrote 01 back into $0104.
    B[0x28] = (uint8_t)(ram[0x0104] & 3);
    // 353B STZ $1B5A / 3540 JSR C2/391C: the lead's TRAIL ring (16 words of X at $1B5B, of Y at $1B7B, cursor $1B5A)
    // starts empty -- every entry the arrival point, `LDA $0283 / STA $1B5B,X / LDA $0285 / STA $1B7B,X` x 16. The init
    // put that point in the block first (3515-351E `LDA $0283 / STA $0014,X`), so the block's position is it.
    ram[0x1B5A] = 0;
    for (int i = 0; i < 16; i++) {
      obPutW(ram + 0x1B5B + i * 2, obW(B + OWOBJ_XPOS));
      obPutW(ram + 0x1B7B + i * 2, obW(B + OWOBJ_YPOS));
    }
    leadInstall(B, kLeadStandId[B[0x28] & 3] + magus);                 // 3549 JSR C2/3987
  }
  // 3550-3564: the four properties about its cell (C2/39D5 into +22..+25) and C2/39AE -- both of the top pair carry bit 2 (an
  // entrance or an event's cell) -- `TSB $0282 #03`: the scanner (class $3264) looks at the cell it arrived on.
  owDactylProps(ram, B, obW(B + OWOBJ_XPOS), obW(B + OWOBJ_YPOS));
  if ((obW(B + 0x22) & 0x0404) == 0x0404) ram[0x0282] |= 0x03;
}
// C2/3934 -- the lead's arrival PUSH onto its trail ring, `JSR C2/3934` at C2/3757: once per completed 8-px step
// (the frame the lead's position equals its step target +2A/+2C, C2/3745-3755), before anything else in the pass reads it.
//   3934 LDA $1B5A / AND #00FF / TAY / LDA $0014,X / STA $1B5B,Y / LDA $0018,X / STA $1B7B,Y
//   3947 INY / INY / TYA / AND #001F / SEP #20 / STA $1B5A
void CtWorldLeadRingPush(uint8_t* ram) {
  const uint8_t* B = ram + CtWorldLeadBlock(ram);
  unsigned y = ram[0x1B5A];
  obPutW(ram + 0x1B5B + y, obW(B + OWOBJ_XPOS));
  obPutW(ram + 0x1B7B + y, obW(B + OWOBJ_YPOS));
  ram[0x1B5A] = (uint8_t)((y + 2) & 0x1F);
}
// C2/375F-376D, right after the push: C2/39AE on the cell the step arrived on -- +22 / +23, the top pair C2/39D5 read for the
// step's target on its input frame (C2/36A4-36AE) -- both with bit 2 (an entrance or an event's cell): `TSB $0282 #03`, the
// scanner (class $3264) looks at the cell; else `TRB $0282 #02`, and the scanner forgets the name ($1B58 = 0). The free-roam
// lead is the host's mover, so the test reads the cell's properties straight (C2/39D5's indices: x - 1 wraps to $BF).
void CtWorldLeadArrive(uint8_t* ram) {
  const uint8_t* B = ram + CtWorldLeadBlock(ram);
  int x = obW(B + OWOBJ_XPOS) >> 3, y = obW(B + OWOBJ_YPOS) >> 3, xm = x - 1 < 0 ? 0xBF : x - 1;
  if (CtWorldProp(ram, xm, (y - 1) & 0x7F) & CtWorldProp(ram, x, (y - 1) & 0x7F) & 0x04) ram[0x0282] |= 0x03;
  else ram[0x0282] &= (uint8_t)~0x02;
}

// ===================== THE PARTY FOLLOWERS (class $3AE2, C2/3AE2) =====================
// The overworld's party members 2 and 3 are objects of their own: the world's bootstrap allocates two blocks of class
// $3AE2 with +24 = the party SLOT (1, 2) -- $0E30 / $0E70 on world 1 after the cathedral (saves/field_re/
// catedral_world.state: Crono at (B8,118), Lucca's block at (C8,118), the third block idle). Each one walks the lead's
// TRAIL: the ring of the lead's last 16 step arrivals (C2/3934 above), slot 1 one entry behind the newest, slot 2 two.
// The handler, mode `$0280 == 0` and module `$027E == 0` (the only ones this port reaches -- it pins $027E to 0):
//   3AE2  `LDA $0024,X / TAX / LDA $7E2980,X / BPL`: an empty slot returns CLC (nothing drawn)
//   3AEF  $027E != $027F or $0280 != $0281 -> phase := 0;  3B07 `JMP ($3B0F,X)` on $027E (0 -> 3B1B; 1-3 -> CLC)
//   3B1B  $027C not 0/1 (a fade, the menu, a ride) -> +28 &= $7F, the STAND id (C2/3987), the tick (C2/0E1D), CLC
//   3B44  `JMP ($3B58,X)` on $0280 * 8 + phase * 2: 3BC2 init / 3C39 decide / 3CDA horizontal / 3D3B vertical
// The anim ids are the LEAD's (C2/3987 STAND, C2/3953 WALK, +$0C for Magus); the block's own +10 tile base ($20 slot
// 1, $40 slot 2) and palette ($0A / $0C) make them that member. Dossier: disasm/notes/ow_party_followers.md.
static void folStand(uint8_t* ram, uint8_t* B) {                       // C2/416C -> C2/3987
  B[0x28] &= 0x7F;                                                     // 416E LDA $0028,X / AND #7F / STA
  int mg = (ram[0x2980 + B[0x24]] == 6) ? 0x0C : 0;                    // 4177 LDA $7E2980,X / STA $00 (3998 CMP #06)
  leadInstall(B, kLeadStandId[B[0x28] & 3] + mg);                      // 3992 LDA $C239AA,X ... JMP C2/1CDA
}
static void folWalkArm(uint8_t* ram, uint8_t* B) {                     // C2/4184
  ram[0x02B1 + B[0x24]] = 0;                                           // 418B STZ $02B1,X (X = the slot)
  if ((B[0x28] & 0x7F) == B[0x27] && (B[0x28] & 0x80)) return;         // 418F-419C: same way, already walking
  B[0x28] |= 0x80;                                                     // 41A1 ORA #80
  int mg = (ram[0x2980 + B[0x24]] == 6) ? 0x0C : 0;
  leadInstall(B, kLeadWalkId[((B[0x28] << 1) & 0xFF) + (B[0x26] & 1)] + mg);   // 41B1 JSR C2/3953 (3958 ASL A)
}
static void folFaceFromVel(uint8_t* B) {                               // C2/40E4
  B[0x27] = B[0x28] & 0x7F;                                            // 40E6-40EB: the old facing
  B[0x28] &= 0x80;                                                     // 40EE-40F3
  uint16_t vx = obW(B + 0x1C), vy = obW(B + 0x20);
  int d;
  if (vx) d = (vx & 0x8000) ? 2 : 3;                                   // 40F8-4107: Left / Right
  else if (vy) d = (vy & 0x8000) ? 0 : 1;                              // 4109-4115: Up / Down
  else return;   // 4123 `BRA ->4123`: the ROM spins forever here; every caller set one velocity (4138 / 4152) first
  B[0x28] |= (uint8_t)d;                                               // 4118 ORA $0028,X / STA
}
static void folVelX(uint8_t* B) {                                      // C2/4138: X axis, sign of +2E (0 -> +1)
  obPutW(B + 0x1C, (obW(B + 0x2E) & 0x8000) ? 0xFFFF : 0x0001);
  obPutW(B + 0x1A, 0); obPutW(B + 0x20, 0); obPutW(B + 0x1E, 0);
}
static void folVelY(uint8_t* B) {                                      // C2/4152: Y axis, sign of +30 (0 -> +1)
  obPutW(B + 0x20, (obW(B + 0x30) & 0x8000) ? 0xFFFF : 0x0001);
  obPutW(B + 0x1E, 0); obPutW(B + 0x1C, 0); obPutW(B + 0x1A, 0);
}
static void folDelta(uint8_t* B) {                                     // C2/3FA7
  obPutW(B + 0x2E, (uint16_t)(obW(B + 0x2A) - obW(B + OWOBJ_XPOS)));
  obPutW(B + 0x30, (uint16_t)(obW(B + 0x2C) - obW(B + OWOBJ_YPOS)));
}
static void folRingTarget(uint8_t* ram, uint8_t* B) {                  // C2/3F6D
  unsigned i = (unsigned)((B[0x24] & 1) ? ram[0x1B5A] - 2 : ram[0x1B5A] - 4) & 0x1F;   // 3F75 DEC x2 / 3F7F SBC #4
  obPutW(B + 0x2A, obW(ram + 0x1B5B + i));
  obPutW(B + 0x2C, obW(ram + 0x1B7B + i));
}
// C2/3EE1 (slot 1, +-$10) and C2/3F27 (slot 2, +-$20): is the LEAD ($0283/$0285) inside the box [p - r, p + r + 1)
// around this follower on both axes? 16-bit unsigned compares, as the ROM's CMP / BCC / BCS.
static int folNearLead(const uint8_t* ram, const uint8_t* B, uint16_t r) {
  uint16_t y = obW(B + OWOBJ_YPOS), x = obW(B + OWOBJ_XPOS);
  uint16_t ly = obW(ram + 0x0285), lx = obW(ram + 0x0283);
  return ly >= (uint16_t)(y - r) && ly < (uint16_t)(y + r + 1) && lx >= (uint16_t)(x - r) && lx < (uint16_t)(x + r + 1);
}
// C2/4205: a ROW scan -- every cell from $05 (exclusive) to $07 (inclusive) along row $06 - 1, its prop & 3 a block.
// 8-bit cell arithmetic, the direction from the sign of the 8-bit difference (4220 BMI). $05 ends on the last cell read.
static int folScanRow(const uint8_t* ram, uint8_t* x, uint8_t row, uint8_t to) {
  uint8_t r = (uint8_t)(row - 1);                                      // 4217 DEC $06
  uint8_t d = (uint8_t)(to - *x);                                      // 4219 SEC / LDA $07 / SBC $05
  if (d == 0) return 0;
  int step = (d & 0x80) ? -1 : 1;
  do {
    *x = (uint8_t)(*x + step);                                         // 4222 INC $05 / 423B DEC $05
    if (CtWorldProp(ram, *x, r) & 3) return 1;                         // 422C JSR C2/62AE / AND #03 / BNE -> SEC
  } while (*x != to);
  return 0;
}
// C2/425A: a COLUMN scan -- every row from $06 - 1 (exclusive) to $07 - 1 (inclusive), the cells at columns $05 - 1 and
// $05 (the first one's prop parked in +22, 4289 / 42B8). $06 ends one row below the last row read (42D5 INC $06).
static int folScanCol(const uint8_t* ram, uint8_t* B, uint8_t col, uint8_t* y, uint8_t to) {
  uint8_t r = (uint8_t)(*y - 1), end = (uint8_t)(to - 1);             // 426C DEC $06 / 426E DEC $07
  uint8_t d = (uint8_t)(end - r);
  int hit = 0;
  if (d != 0) {
    int step = (d & 0x80) ? -1 : 1;
    do {
      r = (uint8_t)(r + step);                                         // 4279 INC $06 / 42A8 DEC $06
      uint8_t p = CtWorldProp(ram, (uint8_t)(col - 1), r);             // 427B LDA $05 / DEC A / JSR C2/62AE
      B[0x22] = p;                                                     // 4289 STA $0022,X
      if ((p | CtWorldProp(ram, col, r)) & 3) { hit = 1; break; }      // 4294 JSR C2/62AE / ORA $0022,X / AND #03
    } while (r != end);
  }
  *y = (uint8_t)(r + 1);                                               // 42D5 / 42D9 INC $06
  return hit;
}
// C2/41B5 -- the two L-shaped paths from the follower's cell (x1, y1) to (x2, y2): bit 0 = the row-first path is
// blocked (row y1 from x1 to x2, then the column at x2 down to y2), bit 1 = the column-first one (column x1, then the
// row at y2). 3 = both.
static int folPaths(const uint8_t* ram, uint8_t* B, uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2) {
  int r = 0; uint8_t x = x1, y = y1;
  if (folScanRow(ram, &x, y, x2)) r |= 1;                              // 41C5 JSR C2/4205 / BCC / TSB $0E #1
  else if (folScanCol(ram, B, x, &y, y2)) r |= 1;                      // 41D4 JSR C2/425A ($05 left on x2)
  x = x1; y = y1;
  if (folScanCol(ram, B, x, &y, y2)) r |= 2;                           // 41E9 JSR C2/425A / TSB $0E #2
  else if (folScanRow(ram, &x, y, x2)) r |= 2;                         // 41F8 JSR C2/4205 ($06 left on y2)
  return r;
}
static void folChooseByDelta(uint8_t* B) {                             // C2/3FF0
  uint16_t dx = obW(B + 0x2E), dy = obW(B + 0x30), v;
  if (dx == 0) v = 0x21;                                               // 3FF7 BEQ -> #0021 (one leg, the column)
  else if (dy == 0) v = 0x11;                                          // 4004 BEQ -> #0011 (one leg, the row)
  else {
    uint16_t ax = (dx & 0x8000) ? (uint16_t)(0u - dx) : dx;            // 3FFB EOR #FFFF / INC A
    uint16_t ay = (dy & 0x8000) ? (uint16_t)(0u - dy) : dy;
    v = (ay >= ax) ? 0x22 : 0x12;                                      // 400C CMP $08 / BCS -> #0022 (column first)
  }
  B[0x29] = (uint8_t)v;                                                // 4046 STA $0029,X (8-bit)
}
static void folDispatchPath(uint8_t* B, int r) {                       // 3FE3 `ASL A / TAX / JMP ($3FE8,X)`
  if (r == 0) folChooseByDelta(B);                                     // 3FF0
  else if (r == 1) B[0x29] = 0x22;                                     // 404C LDA #22: the row path is blocked
  else B[0x29] = 0x12;                                                 // 4056 LDA #12: the column path is blocked
}
static void folDecidePath(uint8_t* ram, uint8_t* B) {                  // C2/3FBC
  uint8_t x1 = (uint8_t)(obW(B + OWOBJ_XPOS) >> 3), x2 = (uint8_t)(obW(ram + 0x0283) >> 3);
  uint8_t y1 = (uint8_t)(obW(B + OWOBJ_YPOS) >> 3), y2 = (uint8_t)(obW(ram + 0x0285) >> 3);
  int r = folPaths(ram, B, x1, x2, y1, y2);                            // to the LEAD's cell, not the trail target
  if (r != 3) { folDispatchPath(B, r); return; }
  // C2/4060, both blocked: walk the ring back from the newest-but-one entry for a point either path reaches.
  unsigned i = (unsigned)(ram[0x1B5A] - 2) & 0x1F;                     // 4064 LDA $1B5A / DEC A x2 / AND #1F
  for (int k = 16; k; k--) {                                           // 4060 LDA #10 / STA $16 ... 40AB DEC $16
    uint16_t tx = obW(ram + 0x1B5B + i), ty = obW(ram + 0x1B7B + i);
    r = folPaths(ram, B, (uint8_t)(obW(B + OWOBJ_XPOS) >> 3), (uint8_t)(tx >> 3),
                 (uint8_t)(obW(B + OWOBJ_YPOS) >> 3), (uint8_t)(ty >> 3));
    if (r != 3) {                                                      // 409C BNE -> 40BF
      obPutW(B + 0x2A, (uint16_t)(tx & 0xFFF8)); obPutW(B + 0x2C, (uint16_t)(ty & 0xFFF8));   // 40C7-40D6
      folDelta(B);                                                     // 40D9 JSR C2/3FA7
      folDispatchPath(B, r);                                           // 40DF LDA $1B / JMP C2/3FE3
      return;
    }
    i = (i - 2) & 0x1F;                                                // 40A0 DEC A x2 / AND #001F
  }
  folRingTarget(ram, B); folDelta(B);                                  // 40B3 JSR C2/3F6D / JSR C2/3FA7
  folChooseByDelta(B);                                                 // 40B9 TDC / JMP C2/3FE3 (index 0)
}
static void folIdle(uint8_t* ram, uint8_t* B) {                        // C2/3C8C
  int s2 = (B[0x24] == 2);                                             // 3C90 LDA $0024,Y / CMP #02
  uint16_t ctr = s2 ? 0x02B3 : 0x02B2;
  uint8_t c = (uint8_t)(ram[ctr] + 1); ram[ctr] = c;                   // 3C97 INC $02B2 / 3CB7 INC $02B3
  if (c < 0xF0) folStand(ram, B);                                      // 3C9F BCC -> 3CB1 JSR C2/416C
  else { if (c != 0xFF) leadInstall(B, s2 ? 0xA4 : 0xA3); ram[ctr] = 0xFE; }   // 3CA5 LDA #A3 / 3CC5 #A4; 3CAA #FE
}
static void folPhase(uint8_t* ram, uint8_t* B, int ph);
static void folDecide(uint8_t* ram, uint8_t* B) {                      // C2/3C39, phase 1
  if (obW(B + 3) < 8 ||                                                // 3C3D LDA $0003,X / CMP #0008 / BCC -> 3C8C
      folNearLead(ram, B, (B[0x24] & 1) ? 0x10 : 0x20)) {              // 3C4D JSR C2/3EE1 / 3C54 JSR C2/3F27 -> 3C87
    if (obW(B + 3) >= 8) obPutW(B + 3, 0);                             // 3C89 STZ $0003,X (the near arm only)
    folIdle(ram, B);
    obAnimTick(ram, B);                                                // 3CD5 JSR C2/0E1D
    return;
  }
  folRingTarget(ram, B);                                               // 3C59 JSR C2/3F6D
  folDelta(B);                                                         // 3C5C JSR C2/3FA7
  folDecidePath(ram, B);                                               // 3C5F JSR C2/3FBC
  if (B[0x29] & 0x20) folVelY(B); else folVelX(B);                     // 3C62 JSR C2/4125 (+29 bit 5: the column)
  folFaceFromVel(B);                                                   // 3C65 JSR C2/40E4
  folWalkArm(ram, B);                                                  // 3C68 JSR C2/4184
  obPutW(B + 3, 0);                                                    // 3C6B STZ $0003,X / STZ $0004,X
  B[2] = (B[0x29] & 0x10) ? 2 : 3;                                     // 3C71 BIT #10: 2 -> 3CDA, else 3 -> 3D3B
  folPhase(ram, B, B[2]);
}
// C2/3CDA (phase 2, the row: X against +2A) and C2/3D3B (phase 3, the column: Y against +2C), the same shape.
static void folPhase(uint8_t* ram, uint8_t* B, int ph) {
  int row = (ph == 2);
  uint16_t pos = obW(B + (row ? OWOBJ_XPOS : OWOBJ_YPOS));
  if (obW(B + 3) >= 8 && (pos & 7) == 0) {                             // 3CDE / 3D3F: 8 frames on, on a cell edge
    B[2] = 1;                                                          // 3CF0 LDA #01 / STA $0002,X
    folDecide(ram, B);                                                 // 3CF7 JMP C2/3C39
    return;
  }
  if (pos != obW(B + (row ? 0x2A : 0x2C))) obIntegrate(B);             // 3D02 JSR C2/0F38 / JSR C2/1CB8
  else {
    uint8_t n = (uint8_t)((B[0x29] & 0x0F) - 1);                       // 3D0A AND #000F / DEC A
    if (n) {
      B[0x29] = n;                                                     // 3D15 STA $0029,X (the count alone)
      B[2] = row ? 3 : 2;                                              // 3D18 LDA #03 / 3D79 LDA #02
      if (row) folVelY(B); else folVelX(B);                            // 3D1F JSR C2/4152 / 3D80 JSR C2/4138
      folFaceFromVel(B);                                               // JSR C2/40E4
      folWalkArm(ram, B);                                              // JSR C2/4184
    } else {
      B[2] = 1;                                                        // 3D2C LDA #01 / STA $0002,X
      folStand(ram, B);                                                // 3D31 JSR C2/416C
    }
  }
  obAnimTick(ram, B);                                                  // 3D36 / 3D97 JSR C2/0E1D
}
// ===================== BOARDING AND LEAVING A VEHICLE ($027E 4 / 5) =====================
// `$028F` names the vehicle: 2 = the Epoch (record $0290 x, $0292 y, $0294 flags -- bits 0-1 the members aboard, 5 the
// wings, 6 riding, 7 present), 3 = the other one ($029A / $029C / $029E, only on loc 1F3). C2/3AB1, one more member aboard:
// `LDA $028F / ASL A / TAX / JMP ($3ABA,X)` -> 3AC3 `INC $0294` / 3AC7 `INC $029E` (0 / 1: nothing).
static void owVehicleAboard(uint8_t* ram) {
  if (ram[0x028F] == 2) ram[0x0294]++;
  else if (ram[0x028F] == 3) ram[0x029E]++;
}
// C2/34AD / 34CD (the lead) and C2/3B8E / 3BA8 (a follower), $027E = 5: the member steps out onto the vehicle's spot and
// one fewer is aboard -- every frame the module holds, so the lead and the followers count $0294 down together and the
// boarding manager (C2/321C) ends the module when it reaches 0. The lead also takes the arrival facing (`$0104 = 1`) and
// C2/3ACB's cells; a follower its position only. No tick: neither is drawn on that frame.
static void owVehicleLeave(uint8_t* ram, uint8_t* B, int lead) {
  int v = ram[0x028F];
  if (v != 2 && v != 3) return;                                        // 34AB / 3B8C CLC
  B[2] = 0;                                                            // 34AF STZ $0002,X
  uint16_t x = obW(ram + (v == 2 ? 0x0290 : 0x029A)), y = obW(ram + (v == 2 ? 0x0292 : 0x029C));
  obPutW(B + OWOBJ_XPOS, x); obPutW(B + OWOBJ_YPOS, y);                // 34B4-34C3 (the lead also into $0283 / $0285)
  if (lead) { obPutW(ram + 0x0283, x); obPutW(ram + 0x0285, y); }
  ram[v == 2 ? 0x0294 : 0x029E]--;                                     // 34C8 DEC $0294 / 34E8 DEC $029E
  if (lead) {
    B[0x26] = 0;                                                       // 34EB STZ $0026,X
    ram[0x0104] = 0x01;                                                // 34EE LDA #01 / STA $0104
    ram[0x0102] = (uint8_t)(x >> 3); ram[0x0103] = (uint8_t)(y >> 3);  // 34F3 JSR C2/3ACB
  }
}
// C2/3B6A -- a follower boarding ($027E = 4), phase table C2/3B74: it walks the lead's cell the way C2/3C39 walks the trail
// (C2/3F94 aims at `$0283 & FFF8` / `$0285 & FFF8`, then the same delta, path and velocity), and on the frame it stands
// exactly on the lead it is aboard (C2/3AB1) and hides (phase 3: no tick).
static int folBoard(uint8_t* ram, uint8_t* B) {
  switch (B[2]) {
  case 0:                                                              // C2/3DE5
    obPutW(B + 0x2A, obW(ram + 0x0283) & 0xFFF8);                      // 3DE9 JSR C2/3F94
    obPutW(B + 0x2C, obW(ram + 0x0285) & 0xFFF8);
    folDelta(B);                                                       // 3DEC JSR C2/3FA7
    folDecidePath(ram, B);                                             // 3DEF JSR C2/3FBC
    if (B[0x29] & 0x20) folVelY(B); else folVelX(B);                   // 3DF2 JSR C2/4125
    folFaceFromVel(B);                                                 // 3DF5 JSR C2/40E4
    folWalkArm(ram, B);                                                // 3DF8 JSR C2/4184
    obPutW(B + 3, 0);                                                  // 3DFB STZ $0003,X / STZ $0004,X
    B[2] = (B[0x29] & 0x10) ? 1 : 2;                                   // 3E01 BIT #10: 1 -> 3E17, else 2 -> 3E7B
    return folBoard(ram, B);
  case 1: case 2: {                                                    // C2/3E17 (the row: X) / C2/3E7B (the column: Y)
    int row = (B[2] == 1);
    if (obW(B + (row ? OWOBJ_XPOS : OWOBJ_YPOS)) != obW(B + (row ? 0x2A : 0x2C))) obIntegrate(B);   // 3E1E / 3E82
    else {
      uint8_t n = (uint8_t)((B[0x29] & 0x0F) - 1);                     // 3E2C AND #000F / DEC A
      if (n) {
        B[0x29] = n;                                                   // 3E37 STA $0029,X
        B[2] = row ? 2 : 1;                                            // 3E3A LDA #02 / 3E9E LDA #01
        if (row) folVelY(B); else folVelX(B);                          // 3E41 JSR C2/4152 / 3EA5 JSR C2/4138
        folFaceFromVel(B);
        folWalkArm(ram, B);
      } else if (obW(B + OWOBJ_XPOS) == obW(ram + 0x0283) && obW(B + OWOBJ_YPOS) == obW(ram + 0x0285)) {
        owVehicleAboard(ram);                                          // 3E5E JSR C2/3AB1
        B[2] = 3;                                                      // 3E63 LDA #03 / STA $0002,X
      } else { B[2] = 0; folStand(ram, B); }                           // 3E6E STZ $0002,X / JSR C2/416C
    }
    obAnimTick(ram, B);                                                // 3E76 / 3EDA JSR C2/0E1D
    return 0;
  }
  }
  return 0;                                                            // 3EDF CLC (aboard: not drawn)
}

// THE EVENT MODE (`$0280 = 1`, an event cell's script running -- C2/3349, the scanner below): the lead and each follower wait
// for the script to name them a sub-script (op 08 -> `$1B3B` + 2 x slot, by the member's id), then run it INLINE on their own
// block (C2/0F63) each frame until its end, their place copied to `$0283` + 4 x slot / `$0285` + 4 x slot. The lead's row is
// C2:345D's second (3772 / 37A1 / 37BB), a follower's C2:3B58's (3D9C / 3DC1 / 3DE3).
static int obRun(uint8_t* ram, uint8_t* B, CtWorldCam* c);
static void owDactylProps(uint8_t* ram, uint8_t* B, unsigned px, unsigned py);
static int obEntryScan(uint8_t* ram, uint8_t* B);
static int obNameBanner(uint8_t* ram, uint8_t* B);
static int owInlineRun(uint8_t* ram, uint8_t* B, CtWorldCam* c) {   // C2/0F63 on a native block: 1 = its script ended (SEC)
  uint16_t cls = obW(B);
  obRun(ram, B, c);
  if (obW(B)) return 0;
  obPutW(B, cls);                                                      // (op 52 ends the script, not the block)
  return 1;
}
static int owEventMember(uint8_t* ram, uint8_t* B, CtWorldCam* c, int slot) {
  switch (B[2]) {
  case 0: {                                                            // C2/3772 / 3D9C
    uint16_t p = obW(ram + 0x1B3B + 2 * slot);
    if (!p) return 0;
    obPutW(B + OWOBJ_PC, p); B[0x09] = 0x7F; B[OWOBJ_WAIT] = 0;        // 377B STA $0007,X / LDA #7F / STA $0009,X / STZ $000A,X
    B[2]++;
    owInlineRun(ram, B, c);                                            // 378B / 3DBA JSR C2/0F63
    if (slot == 0) owDactylProps(ram, B, obW(B + OWOBJ_XPOS), obW(B + OWOBJ_YPOS));   // 3790-379C: C2/39D5 (the lead only)
    return 0;
  }
  case 1:                                                              // C2/37A1 / 3DC1
    if (owInlineRun(ram, B, c)) B[2]++;
    { unsigned o = slot ? (obW(B + 0x24) << 2) & 0xFFFF : 0;            // 3DCF `LDA $0024,X` with M 16: the word +24/+25
      obPutW(ram + ((0x0283 + o) & 0xFFFF), obW(B + OWOBJ_XPOS)); obPutW(ram + ((0x0285 + o) & 0xFFFF), obW(B + OWOBJ_YPOS)); }
    return 0;
  default: return 0;                                                   // C2/37BB / 3DE3 CLC
  }
}

// Returns 0 (CLC: the dispatcher bumps +3), or -1 for a branch this port does not reach ($0280 past 1).
static int obFollower(uint8_t* ram, uint8_t* B, CtWorldCam* c) {
  if (ram[0x2980 + B[0x24]] & 0x80) return 0;                         // 3AEB BPL / 3AED CLC: no member in the slot
  if (ram[0x027E] != ram[0x027F]) B[2] = 0;                            // 3AF1-3AF9
  if (ram[0x0280] != ram[0x0281]) B[2] = 0;                            // 3AFC-3B04
  uint8_t mod = ram[0x027E];
  if (mod >= 1 && mod <= 3) return 0;                                  // 3B68 CLC / RTS
  if (mod == 4) return folBoard(ram, B);                               // 3B6A
  if (mod == 5) { owVehicleLeave(ram, B, 0); return 0; }               // 3B7C (3B8E / 3BA8)
  if (mod != 0) return -1;
  if (ram[0x027C] != 0 && ram[0x027C] != 1) {                          // 3B1B-3B24
    folStand(ram, B);                                                  // 3B28-3B3C (+28 &= 7F, C2/3987)
    obAnimTick(ram, B);                                                // 3B3F JSR C2/0E1D
    return 0;
  }
  if (ram[0x0280] == 1) return owEventMember(ram, B, c, B[0x24]);     // 3B58's second row (3D9C / 3DC1 / 3DE3)
  if (ram[0x0280] != 0) return -1;                                     // past C2:3B58's two rows
  switch (B[2]) {
  case 0: {                                                            // C2/3BC2
    B[2] = 1;                                                          // 3BC6 INC $0002,X
    int loc = ram[0x0100] | (ram[0x0101] << 8);
    B[OWOBJ_ATTR] = (loc == 0x01F5) ? 0xB0 : 0xA0;                     // 3BC9-3BD7
    if (B[0x24] == 1) { B[OWOBJ_ATTR] |= 0x0A; B[0x10] = 0x20; }       // 3BDA CMP #01: palette 5, tiles from $20
    else              { B[OWOBJ_ATTR] |= 0x0C; B[0x10] = 0x40; }       // 3BF3: palette 6, tiles from $40
    B[0x11] = 0;
    obPutW(B + OWOBJ_XPOS, obW(ram + 0x0283)); obPutW(B + OWOBJ_XFRAC, 0);   // 3C05-3C0B: on the lead
    obPutW(B + OWOBJ_YPOS, obW(ram + 0x0285)); obPutW(B + OWOBJ_YFRAC, 0);
    obPutW(B + 0x1A, 0); obPutW(B + 0x1C, 0); obPutW(B + 0x1E, 0); obPutW(B + 0x20, 0);
    obPutW(B + 3, 0);                                                  // 3C23 STZ $0003,X
    B[0x27] = B[0x28] = (uint8_t)(ram[0x0104] & 3);                    // 3C28-3C30
    B[0x26] = 0;                                                       // 3C33
    folStand(ram, B);                                                  // 3C36 JSR C2/416C
    folDecide(ram, B);                                                 //   ...and on into 3C39 the same frame
    return 0;
  }
  case 1: folDecide(ram, B); return 0;
  case 2: case 3: folPhase(ram, B, B[2]); return 0;
  }
  return -1;
}
// The descriptor's PIECE LIST, decoded per piece. Layout (decoder C2/0D08, drain C2/0CEA):
//     +0  u8 N                                   piece count
//     +1  N x { s8 dX, s8 dY, u16 word2 }        word2 bits0-8 = OBJ tile NAME, 14 = H-flip, 15 = V
// CtWorldAnimFrame used to return N but decode only piece 0 -- so a multi-piece object could place
// nothing but its left cap. All seven era placards carry 2, 3 or 4 pieces (ids $75..$7B; e.g. $75 is
// 3 x {-12,$1AC}{+4,$1A6}{+20,$18A} and $78 is 4), and every one of them was silently truncated.
// Piece i is now selectable; `piece` 0 keeps the old meaning so existing callers are unchanged.
int CtWorldAnimPiece(const uint8_t* B, int piece, int* dx, int* dy, int* name, int* flip) {
  if (dx) *dx = 0; if (dy) *dy = 0; if (name) *name = 0; if (flip) *flip = 0;
  const uint8_t* op = ctWorldAnimRom(B[OWOBJ_ANIMBK], obW(B + OWOBJ_ANIM));
  if (!op || op[0] != 0x04) return 0;                      // only an op-4 record names a frame
  const uint8_t* d = ctWorldAnimRom(B[OWOBJ_ANIMBK], (uint16_t)(op[1] | (op[2] << 8)));
  if (!d) return 0;
  int n = d[0];
  if (n < 1 || piece < 0 || piece >= n) return n < 1 ? 0 : n;
  // the whole piece must be inside the baked $C3 window before it is decoded
  const uint8_t* q = ctWorldAnimRom(B[OWOBJ_ANIMBK], (uint16_t)((op[1] | (op[2] << 8)) + 1 + 4 * piece));
  if (!q) return n;
  uint16_t w2 = (uint16_t)(q[2] | (q[3] << 8));
  if (dx) *dx = (int8_t)q[0];
  if (dy) *dy = (int8_t)q[1];
  if (name) *name = w2 & 0x1FF;
  if (flip) *flip = (w2 >> 14) & 3;                        // bit0 = H, bit1 = V
  return n;
}
// The inverse of op 0x30: which $C3:E000 table id produced this anim pointer. The table is the ROM's
// own (ctOwAnimC3), so this asks the ROM which script an object is parked on rather than keying off a
// host-side world number -- which is what lets the era placard read the id its OWN script selected,
// including the two-arm "?"-then-real choice worlds 1 and 2 make at run time. -1 = no entry matches.
int CtWorldAnimIdForPtr(int ptr, int lo, int hi) {
  if (ptr <= 0) return -1;
  if (lo < 0) lo = 0;
  if (hi > 0xA5) hi = 0xA5;
  for (int k = lo; k <= hi; k++)
    if ((unsigned)(k * 2 + 1) < CT_OWANIM_SIZE &&
        (ctOwAnimC3[k * 2] | (ctOwAnimC3[k * 2 + 1] << 8)) == ptr) return k;
  return -1;
}
int CtWorldAnimFrame(const uint8_t* B, int* dx, int* dy, int* name, int* flip) {
  return CtWorldAnimPiece(B, 0, dx, dy, name, flip);
}

static void obSignNorm(uint8_t* B) {                // C2/1759: a NEGATIVE int word -> 0xFFFF if its frac is
  if (obW(B + OWOBJ_XPOS) & 0x8000) obPutW(B + OWOBJ_XPOS, obW(B + OWOBJ_XFRAC) ? 0xFFFF : 0x0000);  // nonzero else 0; non-neg -> 0
  else                        obPutW(B + OWOBJ_XPOS, 0x0000);
  if (obW(B + OWOBJ_YPOS) & 0x8000) obPutW(B + OWOBJ_YPOS, obW(B + OWOBJ_YFRAC) ? 0xFFFF : 0x0000);
  else                        obPutW(B + OWOBJ_YPOS, 0x0000);
}
// op 34 natives. 78A1 = on-screen visibility -> $00 (margin +16px, view 288x272).
// 754D/7598 = the BIRD-SPAWNER aiming pair: re-position to a random X inside the view at
// camY+256 (just below the screen) and pick a launch velocity from the 16-entry 16.16 ROM
// table (all northward headings, 0/30/45 deg at 1-2 px/f). RNG = ROM byte table C0:FE00
// indexed by the free-running $1B30 (C2/2336). The tables are staged in the wram view at
// $7F:E000 (RNG) / $7F:E100 (velocities) by the loaders. Other natives (smoke fx etc) no-op.
static uint8_t obRnd(uint8_t* ram) {               // C2/2336
  uint8_t v = ram[0x1E000 + ram[0x1B30]];
  ram[0x1B30]++;
  return v;
}
// C2/0568 (H) / C2/066C (V), called by an OBJECT: `$00 & 3` picks the layer (1 = BG1 -> X=0, 2 = BG2
// -> X=2) and `$01` is the signed pixel step; each pixel is C2/0607 / C2/059D (H) or C2/070B / C2/06A1
// (V), which move $20,X / $26,X, keep the tile cell $E3,X / $E7,X, and stream the entering column/row.
// Those are owStepH/owStepV -- the SAME steppers the player's camera runs -- and the cells are written
// back to their WRAM addresses as the ROM stores them there. With no stream (the --checkowobj harness)
// only the scroll word moves.
static CtWorldStream* g_obStream;
static void obScroll(uint8_t* ram, CtWorldCam* c, int bg, int vert, int step) {
  if (g_obStream) owDispatchScratch(ram, bg, step);  // the dispatcher runs, and writes its words, on a zero step too
  if (!step) return;
  if (g_obStream) {
    if (vert) owStepV(g_obStream, c, ram, bg, step); else owStepH(g_obStream, c, ram, bg, step);
    uint16_t cell = vert ? g_obStream->e7[bg] : g_obStream->e3[bg];
    ram[(vert ? 0xE7 : 0xE3) + bg * 2] = (uint8_t)cell; ram[(vert ? 0xE8 : 0xE4) + bg * 2] = (uint8_t)(cell >> 8);
  } else {
    uint16_t* w = vert ? (bg ? &c->bg2v : &c->bg1v) : (bg ? &c->bg2h : &c->bg1h);
    for (int n = step < 0 ? -step : step; n > 0; n--) *w = stepWrap(*w, step, vert ? 0x400 : 0x600);
  }
}
// C2/1784 (X, the +12/+14 pair) / C2/17AB (Y, +16/+18): "did the accumulator reach a whole pixel". For a
// NEGATIVE int word it tests the high word of the 32-bit NEGATION (`CLC / LDA frac / EOR #FFFF / ADC #1 /
// LDA int / EOR #FFFF / ADC #0 / BEQ`), otherwise the int word itself; the step handed on is the int
// word's LOW BYTE (`SEP #20 / LDA $0014,X / STA $01`).
static int obWholeStep(const uint8_t* fracp, const uint8_t* intp, int* out) {
  uint16_t i = obW(intp), a = i;
  if (i & 0x8000) {
    uint32_t lo = (uint32_t)(uint16_t)~obW(fracp) + 1u;
    a = (uint16_t)((uint16_t)~i + (lo > 0xFFFF ? 1 : 0));
  }
  if (a == 0) return 0;
  *out = (int8_t)(uint8_t)i;
  return 1;
}
// CLASS $7849 -- a native object (allocated by op 0x35 `35 49 78`, dispatched by C2/0535 on its class
// word every frame) and a PARALLAX NUDGE: `LDX $4E / INC $0022,X / LDA $0022,X / AND #03 / BNE ->785E
// / LDA #01 / STA $01 / STA $00 / JSR C2/0568 / 785E CLC / RTS` -- every fourth frame, scroll BG1 (only)
// one pixel RIGHT; CLC = keep the object (C2/054E bumps its +3 counter). World 5's bootstrap installs it
// (`$7F:0439`), so while the attract's flyover pans BG1 and BG2 left at 1 px/frame with op 0x32, BG1
// gives one pixel back in four. MEASURED (PEEK_SAVE f100 of saves/attract_cena_inicial.state, --monitor
// "7E0020:2,7E0022:2"): $0022 falls 02FF, 02FE, 02FD.. one per frame while $0020 holds every fourth
// (02FD twice, 02FA twice), and --hookpc C20607 visits once per four frames with X = 0000.
static int obClass7849(uint8_t* ram, uint8_t* B, CtWorldCam* c) {
  B[0x22]++;
  if ((B[0x22] & 3) == 0) obScroll(ram, c, 0, 0, 1);
  return 0;                                        // CLC
}
// THE PASS'S OWN JSL C7/0004. The overworld's object pass runs inside its NMI (C2/0333 JSR C2/051D), and a native that posts
// to the sound driver inline -- C2/2F0F, the loader script's zoning arm (`34 0F 2F`, C2/2F49 `JSL C7/0004`) -- keeps the CPU
// in bank C7 for as long as the SPC needs: a song upload. Every NMI met meanwhile finds the pass incomplete (`$4C` bit 0,
// cleared by C2/01CC) and takes C2/0178 `JMP C2/033D` = `INC $4B` and RTI: nothing else of the world runs. When the JSL
// returns, the SAME pass goes on from the next op of the same script and the blocks after it, then C2/0336's OAM build.
// MEASURED (the rule's oracle, saves/field_re/catedral_user.state walked out of the 600 A.D. cathedral, --hookpc C70004 /
// C223D2, --monitor 7E1E00:4 --writer): the post at f73 (vPos 228, `$1E00 = 10 02`), the driver's STZ $1E00 at f107 (C7:0196),
// the lead and the followers allocated by the resumed pass at f107 (C2:04AA), the loader's C2/0346 wait released at f108.
// The host prices the JSL on its own SPC (g_owJsl: NMIs crossed) and holds the world that many frames (field.c).
static CtWorldJslFn g_owJsl = NULL;
static int g_owSuspendNmis = 0;      // NMIs the pass's inline JSL crossed: the pass is suspended until they are spent
static int g_owResumeBlock = -1;     // ...on this block (its script resumes at its saved pc)
void CtWorldSetJsl(CtWorldJslFn fn) { g_owJsl = fn; }
int CtWorldTakePassSuspend(void) { int n = g_owSuspendNmis; g_owSuspendNmis = 0; return n; }
static void owFarLzTo(uint8_t* ram, uint8_t bank, uint16_t dst, uint16_t tbl, uint16_t x);   // (below)
static void owEpochPose(uint8_t* ram, uint8_t* B);   // (with the Epoch, below)
static void obNative(uint8_t* ram, uint8_t* B, uint16_t addr, CtWorldCam* c) {
  if (addr == 0x48A9) { owEpochPose(ram, B); return; }   // C2/48A9: the Epoch's pose for its facing (its trip scripts' `34 A9 48`)
  if (addr == 0x2F0F) {
    // C2/2F0F -- the zoning arm of the loader script: the world's song for the arrival cell, posted inline.
    //   2F0F LDA $7F01ED / BNE ->2F30            a field song held across ($7F01ED) -> 2F30 LDA $7E29AE
    //   2F15 LDA $0102 / LSR A / STA $00 / LDA $0103 / LSR A / STA $01 / JSR C2/62ED / TAX / LDA $1B9B,X
    //   2F28 STA $1E01 / STA $02AE / ... 2F3A LDA #10 / STA $1E00 / LDA #FF / STA $1E02 / LDA #80 / STA $1E03
    //   2F49 JSL C7/0004 / RTS
    uint8_t song;
    if (ram[0x101ED]) song = ram[0x29AE];
    else song = ram[0x1B9B + CtWorldRegion(ram, ram[0x0102] >> 1, ram[0x0103] >> 1)];
    ram[0x1E01] = song; ram[0x02AE] = song;
    ram[0x1E00] = 0x10; ram[0x1E02] = 0xFF; ram[0x1E03] = 0x80;
    if (g_owJsl) { int n = g_owJsl(ram); if (n > 0) g_owSuspendNmis = n; }
    return;
  }
  if (addr == 0x7BC4) {
    // C2/7BC4 -- `STZ $00 / LDA $7F00F7 / BIT #02 / BEQ / INC $00 / RTS`: a story bit into $00 for the script's next test.
    ram[0x00] = (ram[0x100F7] & 0x02) ? 1 : 0;
    return;
  }
  if (addr == 0x7B44) {
    // C2/7B44 -- `LDA $7F00AA / ORA #01 / STA $7F00AA / RTS`: a story bit (1F7, the Day of Lavos: `34 44 7B`).
    ram[0x100AA] |= 0x01;
    return;
  }
  if (addr == 0x7B4F) {
    // C2/7B4F -- `LDA $7F019A / ORA #08 / STA $7F019A / RTS`: its sibling, the same shape on another story byte.
    ram[0x1019A] |= 0x08;
    return;
  }
  if (addr == 0x7B5A) {
    // C2/7B5A -- `LDX #9000 / STX $0303 / LDA #7F / STA $0305 / LDX #0009 / LDA $C6FDF0,X (16) / STA $0300 / LDA $C6FDF2,X /
    // STA $0302 / JSL C3/0002`: the packet at C6:FDF0's entry 3 decompressed into $7F:9000 (C3/0557), the world's picture
    // data (1F6 after the Blackbird's panorama: `34 5A 7B`).
    owFarLzTo(ram, 0x7F, 0x9000, 0xFDF0, 0x0009);
    return;
  }
  if (addr == 0x7B9F) {
    // C2/7B9F -- the same shape into $7E:C600 (`LDA #7E / STA $0305 / LDX #C600 / STX $0303`) from C6:FEA0's entry 18
    // (`LDX #0036`).
    owFarLzTo(ram, 0x7E, 0xC600, 0xFEA0, 0x0036);
    return;
  }
  if (addr == 0x7BD1) {
    // C2/7BD1 -- `LDA $00 / STA $7E29AE / RTS` with M = 8 (C2/180E SEP #20): the song C2/2F0F plays while $7F01ED holds it.
    // The scripts set $00 just before with op 0x13 (1F4: `13 00 00 0D` / `34 D1 7B`).
    ram[0x29AE] = ram[0x00];
    return;
  }
  if (addr == 0x78A1) {
    uint16_t dx = (uint16_t)(obW(B + OWOBJ_XPOS) - c->bg2h + 0x10);
    if (dx >= 0x8000) dx += 0x600;
    uint16_t dy = (uint16_t)(obW(B + OWOBJ_YPOS) - c->bg2v + 0x10);
    if (dy >= 0x8000) dy += 0x400;
    ram[0x00] = (dx < 0x120 && dy < 0x110) ? 1 : 0;
  } else if (addr == 0x7575) {                     // GULL aim (C2/7575): X = 0x178 + FULL-byte RNG (fixed fair
    uint8_t rnd = obRnd(ram);                       // centre, UNmasked), Y = ((camV>>3)+0x20)<<3 just below view.
    obPutW(B + OWOBJ_XPOS, (uint16_t)(0x178 + rnd));       // (the burst master calls THIS, not 0x754D -- without it the
    obPutW(B + OWOBJ_YPOS, (uint16_t)((((c->bg2v >> 3) + 0x20) << 3)));  // gulls aim wrong, fly off-screen + die -> too few)
  } else if (addr == 0x754D) {                     // re-aim position: random X in view, Y below it
    uint8_t rnd = (uint8_t)(obRnd(ram) & 0x7F);
    obPutW(B + OWOBJ_XPOS, (uint16_t)(((c->bg2h >> 3) << 3) + rnd + 0x40));
    obPutW(B + OWOBJ_YPOS, (uint16_t)((((c->bg2v >> 3) + 0x20) << 3)));
  } else if (addr == 0x7598) {                     // launch velocity from the ROM table
    int i = (obRnd(ram) & 0x0F) * 8;
    const uint8_t* t = ram + 0x1E100 + i;
    memcpy(B + OWOBJ_VELXF, t, 8);                        // velXfrac/velXint/velYfrac/velYint
  } else if (addr == 0x79E0) {
    // C2/79E0: a random nudge RIGHT, 0..48 px -- what spreads the attract 1F7's earth fragments (each one's script `34 E0
    // 79`) across the screen instead of stacking them on their birth X. `JSR C2/2336 / STA $4202 / LDA #30 / STA $4203 /
    // 4 NOP / LDA $4216 / BMI -> LDA $4217 / INC A : LDA $4217` (the product's high byte, rounded on the low byte's bit 7)
    // `/ REP #20 / LDX $4E / AND #00FF / CLC / ADC $0014,X / STA $0014,X`. MEASURED (--hookpc C279E0 from the attract
    // state): visits at f2984, f2985 and f3000; the cartridge's fragments sit at X 657..693, the port's all at 648.
    uint8_t rnd = obRnd(ram);
    unsigned p = (unsigned)rnd * 0x30u;
    unsigned add = ((p >> 8) + ((p & 0x80) ? 1u : 0u)) & 0xFFu;
    obPutW(B + OWOBJ_XPOS, (uint16_t)(obW(B + OWOBJ_XPOS) + add));
  } else if (addr == 0x48CD) {
    // C2/48CD: a LEAP of 0x200 px along the X velocity -- `REP #20 / LDX $4E / LDA $001C,X / BMI -> LDA #FE00 : LDA #0200 /
    // CLC / ADC $0014,X / STA $0014,X / JMP C2/1CB8` (the world wrap). The attract 1F0's Epoch calls it (`34 CD 48` at
    // $7F:141C) between the first and second rounds of its flight loop ($7F:1492): cartridge f8749, $0CC4 02A0 -> 04A0 (then
    // the frame's step, 04A8). Skipped, the port's Epoch flew on 0x200 behind and was off screen when it came round.
    uint16_t d = (obW(B + OWOBJ_VELXI) & 0x8000) ? 0xFE00 : 0x0200;
    obPutW(B + OWOBJ_XPOS, (uint16_t)(obW(B + OWOBJ_XPOS) + d));
    obWrapWorld(B);
  } else if (addr == 0x786F) {
    // C2/786F: the PARTY into the overworld's own cells -- `LDA $7E2980 / STA $001BA3`, $2981 -> $1BA4, $2982 -> $1BA5.
    // The attract's 2300 A.D. walkers read them back (`17 22 A4 1B` / `1F 22 03 07`) to pick each member's script:
    // $7F:0ADA/$0AF6/$0B12 for Robo (id 3), $0A4F/$0A6F/$0A8F for the others. This native used to fall through the
    // chain below without a word, so $1BA4 stayed 0 and Robo walked out of the dome on Crono's script.
    ram[0x1BA3] = ram[0x2980];
    ram[0x1BA4] = ram[0x2981];
    ram[0x1BA5] = ram[0x2982];
  } else if (addr == 0x79C8) {
    // C2/79C8: GRAVITY -- the block's vertical velocity (+1E fraction, +20 pixels) gains 0x2000 a call, the carry into the
    // pixels (`CLC / LDA $001E,X / ADC #2000 / STA $001E,X / LDA $0020,X / ADC #0000`): the party's arcs out of Heckran's
    // whirlpool onto the shore by Lucca's house (world 0's event script, $7F:12D2...).
    uint32_t v = (uint32_t)obW(B + 0x1E) + 0x2000u;
    obPutW(B + 0x1E, (uint16_t)v); obPutW(B + 0x20, (uint16_t)(obW(B + 0x20) + (v >> 16)));
  } else if (addr == 0x7AFC || addr == 0x7B20) {
    // THE BLACK OMEN'S PICTURE LOADERS (the 1F6 cutscene, $7F:0668 / $067E): `LDX #9000 / STX $0303 / LDA #7F / STA $0305 /
    // LDX $08 / REP #20 / LDA tbl,X / STA $0300 / SEP #20 / LDA tbl+2,X / STA $0302 / JSL C3/0002` -- the packet the record at
    // tbl + `$08` names (the script's `13 08 00 nn`), C6:FE20 for 7AFC, C6:FF40 for 7B20, into $7F:9000; op 0x03 then sends it.
    owFarLzTo(ram, 0x7F, 0x9000, addr == 0x7AFC ? 0xFE20 : 0xFF40, (uint16_t)(ram[0x08] | (ram[0x09] << 8)));
  } else if (addr == 0x7C56 || addr == 0x7C97) {
    // ...AND ITS WAVE: two indirect HDMA channels on BG2's scroll. 7C56: `$8A35 = $8A38 = F0`, `$8A3B = 00`, channel 1 = DMAP
    // 42 (indirect, two writes) to $210F (BG2HOFS) from $7E:8A35, `TSB $4A` #02; 7C97 the same on channel 2 to $2110 (BG2VOFS)
    // from $7E:8A42, #04. Each zeroes its caller's +26 and fills its two line buffers with 0 (`STA $8B56 / MVN` over 0x460
    // bytes: $8B56-$8FB5; $9176-$95D5). The tables' pointers are the NMI's copies of $8A21/$8A23 and $8A25/$8A27 (C2/02B2).
    int two = (addr == 0x7C97);
    unsigned t = two ? 0x8A42 : 0x8A35, buf = two ? 0x9176 : 0x8B56;
    ram[t] = 0xF0; ram[t + 3] = 0xF0; ram[t + 6] = 0x00;
    CtWorldHdmaSet(two ? 2 : 1, 0x42, two ? 0x10 : 0x0F, (uint16_t)t);
    ram[0x4A] |= (uint8_t)(two ? 0x04 : 0x02);
    B[0x26] = 0;
    memset(ram + buf, 0, 0x460);
  } else if (addr == 0x7CD8 || addr == 0x7D5E) {
    // ...ITS STEP, once a frame from each wave child: `INC $0026,X`; on an even count the table reads buffer A while buffer B,
    // shifted one line (`MVN` 0x1BE bytes from B + 2), becomes A; on an odd count the other way round -- then the newest
    // line, the last word of the buffer just written: `+24 += 10`, C2/225E's sine of it, its magnitude times the amplitude
    // +22 through $4202/$4203, the product's high byte, the sign put back.
    int v = (addr == 0x7D5E);
    unsigned A = v ? 0x9176 : 0x8B56, Bb = v ? 0x9416 : 0x8DF6, ptr = v ? 0x8A25 : 0x8A21;
    B[0x26]++;
    unsigned dst, last;
    if (!(B[0x26] & 1)) { dst = A; memmove(ram + A, ram + Bb + 2, 0x1BE); last = A + 0x1BE; }
    else                { dst = Bb; memmove(ram + Bb, ram + A + 2, 0x1BE); last = Bb + 0x1BE; }
    obPutW(ram + ptr, (uint16_t)dst); obPutW(ram + ptr + 2, (uint16_t)(dst + 0xE0));
    obPutW(ram + 0x10, (uint16_t)last); ram[0x12] = 0x7E;
    obPutW(B + 0x24, (uint16_t)(obW(B + 0x24) + 0x0010));
    int s = owSine(obW(B + 0x24));
    unsigned mag = (unsigned)(s < 0 ? -s : s) & 0xFF;
    unsigned prod = (mag * B[0x22]) >> 8;
    obPutW(ram + last, (uint16_t)(s < 0 ? (0x10000 - prod) & 0xFFFF : prod));
  } else {
    // An op-0x34 target this port does not have: C2/1807 `JMP ($005B)` runs whatever C2 routine the script names,
    // so skipping it silently changes what the script branches on later. Name it (once per address).
    static uint16_t said[32]; static int nsaid = 0;
    int seen = 0;
    for (int i = 0; i < nsaid; i++) if (said[i] == addr) seen = 1;
    if (!seen && nsaid < 32) { said[nsaid++] = addr;
      fprintf(stderr, "owobj: op 34 -> C2/%04X is an UNDERIVED native -- advancing 3 without running it\n", addr); }
  }
}

// op 0x30 (C2/160F -> C2/1CDA) sets the per-object ANIM SUB-SCRIPT pointer +0B/+0C, and that +0B IS the
// canonical class discriminator the renderer reads. The ROM is three instructions:
//     C2/1CDC  AND #$00FF / ASL A / TAX / LDA $C3E000,X / STA $000B,X / LDA #C3 / STA $000D,X
// -- a DIRECT 8-bit index into a 166-entry (ids $00..$A5) word table at $C3:E000, with NO range check.
// This used to be `obAnimTbl[op[1] & 0x3F]`, a hand-written 64-entry stub with FOUR non-zero slots. Both
// halves were wrong: the mask is EIGHT bits (`AND #$00FF` only truncates the 16-bit `LDA [$58],Y` read --
// measured with --callrom C2/1CDA, A=$FF75 and A=$0075 both give $F836 while A=$0035 gives $E66D), and
// the table is the ROM's. The 6-bit mask aliased every id >= $40 onto a zeroed hole: the seven ERA
// PLACARDS $75..$7B all landed on $35..$3B and got anim pointer 0 -- defect #8, "invented 600AD placard".
// Across the eight captured overworlds the live objects use twelve distinct table entries and the stub
// reproduced exactly one of them ($22), so this was never only about the placard.
// The table needs no new data: ctOwAnimC3 is already byte-for-byte ROM $C3:E000..FFFF, and its first
// 0x14C bytes ARE the pointer table. Reproduce the ROM's unguarded read rather than clamping it.
uint16_t CtWorldAnimPtrForId(unsigned id) {                   // C2/1CDA's `LDA $C3E000,X`
  unsigned o = (id & 0xFF) * 2;                            // AND #$00FF / ASL A
  if (o + 1 >= CT_OWANIM_SIZE) return 0;                   // (our blob is only the $E000..FFFF window)
  return (uint16_t)(ctOwAnimC3[o] | (ctOwAnimC3[o + 1] << 8));
}

// ---- C2/1100: object-VM op $05, ENTER A LOCATION ------------------------------------------------
// ⭐ This is what drives the ATTRACT DEMO off the overworld. Its flyover has no player, no mover
// and no input (measured -- sequence_table.md 9.1), so the only thing that can change $0100 is this
// opcode, and it used to be a no-op here. Graded byte-for-byte against the cartridge: all SEVENTEEN
// writes `ct.exe --callrom C21100 --writes` reports are reproduced, in value and in address.
//
// ⚠ Operand byte 2 is read TWICE by the ROM -- 16-bit as the destination's high byte (masked to
// $01FF, so only bit 8 survives) and again 8-bit, shifted right, as the arrival flags.
// ⚠ $0107/$04FE take a SIXTEEN-bit store and $0108/$04FF an eight-bit one right after, so the
// column's high byte is written and then overwritten by the row. Both stores are kept.
// The stores are also the main loop's state 3 (C2/2470-24D2, the location entry's own), over the entrance record
// `$7E:7E03,X` instead of the operands -- the same writes without C2/115E's request: state 3 falls straight into state 2's
// code, so $027C stays 3 (measured on the Truce door from saves/ow_w0.state: $0100 = 011 at f129, $027C = 03 throughout).
void CtWorldLocationStores(uint8_t* ram, const uint8_t* op) {
  uint16_t cur = (uint16_t)(ram[0x0100] | (ram[0x0101] << 8));
  ram[0x0105] = (uint8_t)cur; ram[0x0106] = (uint8_t)(cur >> 8);       // the RETURN location
  ram[0x04FC] = (uint8_t)cur; ram[0x04FD] = (uint8_t)(cur >> 8);
  uint16_t col = (uint16_t)((ram[0xE5] | (ram[0xE6] << 8)) + 0x10);    // ...and the return position
  if (col >= 0xC0) col -= 0xC0;
  ram[0x0107] = (uint8_t)col; ram[0x0108] = (uint8_t)(col >> 8);
  ram[0x04FE] = (uint8_t)col; ram[0x04FF] = (uint8_t)(col >> 8);
  uint8_t row = (uint8_t)(((ram[0xE9] | (ram[0xEA] << 8)) + 0x10) & 0x7F);
  ram[0x0108] = row; ram[0x04FF] = row;
  uint16_t dest = (uint16_t)((op[1] | (op[2] << 8)) & 0x01FF);
  ram[0x0100] = (uint8_t)dest; ram[0x0101] = (uint8_t)(dest >> 8);
  ram[0x0104] = (uint8_t)(op[2] >> 1);                                 // the arrival flags
  ram[0x0102] = op[3];                                                 // the arrival tile
  ram[0x0103] = op[4];
  static const uint8_t FACE[4] = { 0x01, 0x00, 0x03, 0x02 };           // C2/2509
  ram[0x0109] = FACE[ram[0x0104] & 3];
}
void CtWorldObjOp05(uint8_t* ram, const uint8_t* op) {
  CtWorldLocationStores(ram, op);
  ram[0x027C] = 2;                                                     // C2/115E: the request
}

// op 0x4E -- C2/181C: an INDIRECT FAR CALL. The three operand bytes are a 24-bit address; the
// handler loads it into $5B/$5D and `JML [$005B]`s to it, and the target returns with RTL:
//   C2/181C: LDY #0001 / LDA [$58],Y / STA $5B      -- the low WORD (M=16 here)
//            SEP #20 / LDY #0003 / LDA [$58],Y / STA $5D   -- the BANK
//            JSL $C21836   ($C2/1836: JML [$005B])
//            REP #20 / LDA #0004 / RTS              -- the advance, unconditional
// ⭐ The advance is NOT a function of the callee: the handler returns the literal 4 whatever the
// target did, and no target reports anything back to the script (the call is `JML`, the return
// `RTL` -- there is no result register the dispatcher reads). So the SCRIPT's control flow is
// independent of whether a given target is ported, which is why this advances even for a target
// whose body is not.
//
// The port used to have no case at all for 0x4E, so it fell to `default: adv = -1` and PARKED the
// object -- permanently, since a parked object re-reads the same op every frame. That is what froze
// the attract's world-5 flyover: its driver reaches `4E 41 E2 C6` at pc=0562 and never gets past it,
// so the leg never ends and scene 433's chain never advances to its next entry (notes 9.17).
// The host's hook for the window object's two targets (field.c installs CtXlatOwWindowCall on the vendored hardware;
// a host without one -- ct_world, the oracle -- names the call and advances, as before).
static const uint8_t* g_owRom = NULL;   // the host's ROM image (CtWorldSetRom, below)
static long g_owRomLen = 0;
static CtWorldOp4EHook g_owWinCall = NULL;
void CtWorldSetOp4EHook(CtWorldOp4EHook fn) { g_owWinCall = fn; }
// The picture loaders' body (see their cases in CtWorldObjOp4E): $0300-$0305 as the routine leaves them, then C3/0002.
// C3/0557 through the world's caller convention: the source from the table at $C6:<tbl> + x (3 bytes) into $0300-$0302,
// the destination $<bank>:<dst> into $0303-$0305, then the packet decompressed there.
static void owFarLzTo(uint8_t* ram, uint8_t bank, uint16_t dst, uint16_t tbl, uint16_t x) {
  if (!g_owRom || g_owRomLen < 0x60000L + tbl + x + 3) return;
  const uint8_t* p = g_owRom + 0x60000L + tbl + x;
  ram[0x0300] = p[0]; ram[0x0301] = p[1]; ram[0x0302] = p[2];
  ram[0x0303] = (uint8_t)dst; ram[0x0304] = (uint8_t)(dst >> 8); ram[0x0305] = bank;
  uint32_t a = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
  uint8_t* out = ram + (bank == 0x7F ? 0x10000 : 0) + dst;
  if (!ct_lz_decompress_bounded(g_owRom, (size_t)g_owRomLen, out, 0x10000u - dst, ct_lz_hirom_offset(a)))
    fprintf(stderr, "owobj: the packet at $%06X did not decompress inside $%02X:%04X-$FFFF\n", a, bank, dst);
}
static void owFarLz(uint8_t* ram, uint16_t dst, uint16_t tbl, uint16_t x) { owFarLzTo(ram, 0x7F, dst, tbl, x); }
void CtWorldObjOp4E(uint8_t* ram, const uint8_t* op) {
  uint32_t tgt = (uint32_t)op[1] | ((uint32_t)op[2] << 8) | ((uint32_t)op[3] << 16);
  switch (tgt) {
    // $C6:E2AF -- `TDC / STA $7F0062 / RTL`, entered with M = 8 (C2/1823 SEP #20) and D = $0000 (the overworld never
    // moves the direct page off zero: every traced instruction of the leg reads D=0000). So $7F0062 := $00 -- the
    // attract's step counter, reset by the world scripts' "a button was pressed" arm (`23 F0 00 rr` -> ... -> `4E AF E2
    // C6` -> `05 B1 01 ..`): traced on the cartridge from the attract's 1F5 leg with A held (ct.exe --cputrace with
    // CPUTRACE_BTN=100 from f199), C6:E2B0 runs 17 frames after the press and C2:1138 writes $0100 = 1B1 the same frame;
    // loc 1B1 then reads $7F0062 = 0 as "back to the title".
    case 0xC6E2AF:
      ram[0x10062] = 0x00;
      break;
    // $C6:E241 (install + the first C3/01E4 call) and $C6:E27E (a call a frame): TRANSLATED from the ROM (src/xlat/owwin_c6.c,
    // tools/gen/xlat.py from the cartridge's own trace of the attract's 1F5 leg) and run on the vendored hardware over this WRAM
    // through the object VM's far call as C2/181C makes it (seq_xlat.c CtXlatOwWindowCall). E241: HDMA channel 2 over $2127
    // (DMAP $40, table $7E:8A42, bank $7E), `TSB $4A` #04, the object's +$22 -> $0350 and +$24 -> $0352 (bank $7E),
    // JSL C3/000E; both: $7E:8A25 / $8A27 = the table half C3/01E4 wrote (+0 or +$E0 by $0351) and +$70. The routine's
    // seed reads the H/V counters -- the world VM's position in the frame, which this port has no clock for: every call
    // starts on the first call's (CT_XLAT_OWWIN_V/H, field_window_wipe.md sec. 6).
    case 0xC6E241: case 0xC6E27E: {
      if (tgt == 0xC6E241) CtWorldHdmaSet(2, 0x40, 0x27, 0x8A42);   // E241: channel 2 = window 1's right edge
      int r = g_owWinCall ? g_owWinCall(ram, tgt) : -1;
      if (r) { static int said = 0;
        if (!said) { said = 1; fprintf(stderr, "owobj: op 4E -> $%06X not run (%s, r=%d) -- the script advances, the window is not stepped\n",
                                        tgt, g_owWinCall ? "translation stop" : "no host hook", r); } }
      break;
    }
    // THE PICTURE LOADERS -- four routines of one shape, the whole family in bank C6 (a byte-pattern scan of the bank finds
    // these four and no other):
    //   LDX #dst / STX $0303 / LDA #7F / STA $0305 / LDX #x / REP #20 / LDA tbl,X / STA $0300 / SEP #20 / LDA tbl+2,X /
    //   STA $0302 / JSL C3/0002 / RTL
    // -- the packet named by the three-byte record at tbl + x, decompressed by C3/0002 (= C3/0557, the LZ routine; its
    // $0308-$0316 workspace is the routine's own and not kept, as everywhere else in the port). Lavos's rising (overworld
    // 1F4) runs E028 (the picture, then op 0x03 sends it to VRAM) and E04D (its palettes: the three op-0x04 rows after it
    // read $7F:8C00/8C20/8C40).
    case 0xC6E028: owFarLz(ram, 0x9000, 0xFDF0, 0x06); break;
    case 0xC6E04D: owFarLz(ram, 0x8C00, 0xFEA0, 0x03); break;
    case 0xC6E64A: owFarLz(ram, 0x9000, 0xFDF0, 0x27); break;
    case 0xC6E66F: owFarLz(ram, 0x8C00, 0xFEA0, 0x06); break;
    // THE HELD SONG'S FLAG: E63D `LDA #01 / STA $7F01ED / RTL`, E644 `TDC / STA $7F01ED / RTL` (M = 8, D = 0). While it is set
    // the loader's zoning arm C2/2F0F plays $7E:29AE instead of the cell's song (1F4's script stores 0D there just before,
    // `13 00 00 0D` / `34 D1 7B`).
    case 0xC6E63D: ram[0x101ED] = 0x01; break;
    case 0xC6E644: ram[0x101ED] = 0x00; break;
    default: {
      static int said = 0;
      if (!said) { said = 1;
        fprintf(stderr, "owobj: op 4E -> $%06X is an UNDERIVED far target -- advancing 4 per "
                        "C2/1832 without running it\n", tgt); }
      break;
    }
  }
}

// THE SCRIPT'S BANK. C2/0F63 loads `$5A = $0009,X` over the pc and reads `[$58]` long, so a VM object runs from whatever
// bank its +09 names: the world's own scripts are the $7F:0400 blob, but the menu's two fades and the location entry's
// fade-out are scripts in bank $C2 itself ($C2:2EC1 `28 01 52`, $C2:2EC4 `29 01 52`, spawned by C2/04D9 with A = $C2).
// The ROM image is the host's (CtWorldSetRom); without one, and for any bank that is neither $7E nor a ROM bank, the
// read stays where it always was, $7F.
void CtWorldSetRom(const uint8_t* rom, long len) { g_owRom = rom; g_owRomLen = len; }
static const uint8_t* obScript(const uint8_t* ram, uint8_t bank, uint16_t pc, uint8_t* buf) {
  if (bank == 0x7E) return ram + pc;
  if (bank >= 0xC0 && g_owRom) {
    long o = ((long)(bank - 0xC0) << 16) | pc;
    for (int k = 0; k < 8; k++) buf[k] = (o + k < g_owRomLen) ? g_owRom[o + k] : 0;
    return buf;
  }
  return ram + 0x10000 + pc;
}

// C2/09C5's DIRECT-PAGE WORDS -- what the per-layer rebuild leaves in $00-$1B and the scroll shadows once it returns. The
// tilemap it streams is CtWorldTilemapBuild's (graded against the oracle's VRAM by ow-streaming); these are the scratch
// words the routine walks while doing it, and they are not dead: C3/02DD (the seed, `ADC $0008`) folds $0008 into
// $0386 when the world's window object calls it later in the same pass -- MEASURED, the attract's 1F5 leg under the
// rule (CT_ORACLE_VIRTUAL_BEAM=1, CT_ORACLE_LOGICAL=1, from power-on): the first seed call reads $0008 = 41, the last
// word C2/0A55 stored, where the port read 00. All sixteen-bit (09CC REP #20; the tables are bank $C2 words):
//   09DB-09E7  $20,X = $E3,X << 3 ; $26,X = $E7,X << 3                       X = (layer - 1) * 2
//   09E9-09F9  $14 / $16 / $10 = C2:0556 / C2:055C / C2:0562 [X]
//   09FB-0A06  $08 = $E3,X >> 1 when $E3,X is odd, else ($E3,X >> 1) - 1, and -1 -> $5F
//   0A08-0A13  $0A = the same from $E7,X, -1 -> $3F
//   0A15-0A29  $0C = ($20,X - (bit 3 ? 8 : 16)) & $1FF ; 0A2B-0A3F  $0E = ($26,X - (bit 3 ? 8 : 16)) & $FF
//   0A41       $02 = 18, then 18 rows of:
//     0A6C  $00 = 16 ; $18 = $08 + $16 ; $1A = $0A << 6 ; $04 = $0A * 96 ; $06 = ($0E & $F0) >> 2 ; and 16 columns of
//           0A90-0AA7  $04 += $60 (>= $1800 -> 0) ; $06 = ($06 + 4) & $3F ; DEC $00
//     0ADC  $12 = ($0C >> 3, + $3E0 when >= $20) + $10   (then the row's two DMAs)
//     0A4C  $08 += 1 (>= $60 -> 0) ; 0A57  $0C = ($0C + $10) & $1FF ; 0A62  DEC $02
static void owW16(uint8_t* ram, int a, unsigned v) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); }
static unsigned owRomW(unsigned bankC2off) {
  long o = 0x020000L + (long)bankC2off;
  return (g_owRom && o + 1 < g_owRomLen) ? (unsigned)(g_owRom[o] | (g_owRom[o + 1] << 8)) : 0u;
}
static void owRebuildScratch(uint8_t* ram, int bg) {
  int x = bg * 2;
  unsigned e3 = ram[0xE3 + x] | (ram[0xE4 + x] << 8), e7 = ram[0xE7 + x] | (ram[0xE8 + x] << 8);
  unsigned s20 = (e3 << 3) & 0xFFFF, s26 = (e7 << 3) & 0xFFFF;
  owW16(ram, 0x20 + x, s20); owW16(ram, 0x26 + x, s26);
  unsigned w14 = owRomW(0x0556 + x), w16 = owRomW(0x055C + x), w10 = owRomW(0x0562 + x);
  unsigned w08 = (e3 & 1) ? (e3 >> 1) : (((e3 >> 1) - 1) & 0xFFFF);
  if (w08 & 0x8000) w08 = 0x5F;
  unsigned w0A = (e7 & 1) ? (e7 >> 1) : (((e7 >> 1) - 1) & 0xFFFF);
  if (w0A & 0x8000) w0A = 0x3F;
  unsigned w0C = ((s20 - ((s20 & 8) ? 8 : 16)) & 0x1FF), w0E = ((s26 - ((s26 & 8) ? 8 : 16)) & 0xFF);
  unsigned w00 = 0, w04 = 0, w06 = 0, w12 = 0, w18 = 0, w1A = 0;
  for (int row = 0; row < 18; row++) {
    w00 = 16; w18 = (w08 + w16) & 0xFFFF; w1A = (w0A << 6) & 0xFFFF; w04 = (w0A * 96) & 0xFFFF; w06 = (w0E & 0xF0) >> 2;
    for (int col = 0; col < 16; col++) {
      w04 = (w04 + 0x60) & 0xFFFF; if (w04 >= 0x1800) w04 = 0;
      w06 = (w06 + 4) & 0x3F; w00--;
    }
    unsigned t = w0C >> 3; if (t >= 0x20) t += 0x3E0; w12 = (t + w10) & 0xFFFF;
    w08 = (w08 + 1 >= 0x60) ? 0 : w08 + 1; w0C = (w0C + 0x10) & 0x1FF;
  }
  owW16(ram, 0x00, w00); owW16(ram, 0x02, 0); owW16(ram, 0x04, w04); owW16(ram, 0x06, w06); owW16(ram, 0x08, w08);
  owW16(ram, 0x0A, w0A); owW16(ram, 0x0C, w0C); owW16(ram, 0x0E, w0E); owW16(ram, 0x10, w10); owW16(ram, 0x12, w12);
  owW16(ram, 0x14, w14); owW16(ram, 0x16, w16); owW16(ram, 0x18, w18); owW16(ram, 0x1A, w1A);
}

// C2/04D9: `STA $01 / STX $0A / LDX #0F63 / JSR C2/048A`, then +07 = the pc, +09 = the bank, +0A = 0 -- a VM object started
// on a script by code outside the object pass (the main loop's state 3, the menu). C2/048A's MVN copies +05..+3F from the
// block $4E names, which outside the pass is the last one the previous pass visited (C2/053C `STX $4E`).
// C2/04D9 / C2/04FB's own stores, before C2/048A / C2/04CD: `STA $01` (the bank), `STX $0A` (the pc), `LDX #0F63` -> $08.
static void obSpawnScratch(uint8_t* ram, uint8_t bank, uint16_t pc) {
  ram[0x01] = bank; ram[0x0A] = (uint8_t)pc; ram[0x0B] = (uint8_t)(pc >> 8);
  ram[0x08] = 0x63; ram[0x09] = 0x0F;
}
void CtWorldSpawnVm(uint8_t* ram, uint8_t bank, uint16_t pc) {
  obSpawnScratch(ram, bank, pc);
  uint16_t par = (uint16_t)(ram[0x4E] | (ram[0x4F] << 8));
  for (int n = 0; n < 60; n++) {
    uint8_t* C = ram + OWOBJ_CHILD_BASE + n * OWOBJ_STRIDE;
    if (obW(C)) continue;
    obPutW(C, OWOBJ_CLASS_VM); C[2] = 0; C[3] = 0; C[4] = 0;
    if ((long)par + 0x40 <= 0x10000) memmove(C + OWOBJ_GOSUB, ram + par + OWOBJ_GOSUB, 0x3B);
    obPutW(C + OWOBJ_PC, pc); C[0x09] = bank; C[OWOBJ_WAIT] = 0;
    return;
  }
}

// C2/048A for a NATIVE class: the first free block from $0C30 (60 of them), the class word, +2 and the 16-bit +3/+4
// zeroed, then the MVN of the parent's +05..+3F. (Op $09's VM clone and C2/04D9 install a script pointer on top.)
static void obAllocNative(uint8_t* ram, const uint8_t* B, uint16_t cls) {
  ram[0x08] = (uint8_t)cls; ram[0x09] = (uint8_t)(cls >> 8);   // 048A STX $08 -- before the search, full table or not
  for (int n = 0; n < 60; n++) {
    uint8_t* C = ram + OWOBJ_CHILD_BASE + n * OWOBJ_STRIDE;
    if (obW(C)) continue;
    obPutW(C, cls); C[2] = 0; C[3] = 0; C[4] = 0;
    memmove(C + OWOBJ_GOSUB, B + OWOBJ_GOSUB, 0x3B);
    return;
  }
}

// One VM step pass for one object: run ops until a yield. Returns 0 if the object died.
static int g_obTrace = -1;                          // CT_OWOBJTRACE, see obRun's loop
static int obRun(uint8_t* ram, uint8_t* B, CtWorldCam* c) {
  uint16_t pc = obW(B + OWOBJ_PC);
  uint8_t opbuf[8];
  // ⭐ NO OP BUDGET. The ROM's dispatcher is `LDA [$58] / ASL A / TAX / JSR ($0F91,X) / BEQ ->0F8E /
  // CLC / ADC $58 / STA $58 / ... / BRA ->0F73` (C2/0F73): it runs opcodes until a handler returns
  // ZERO, and nothing counts them. This used to stop at 64, which is not a number the cartridge has
  // -- and it showed. A bootstrap needing more than 64 ops in one frame was cut short and resumed
  // next frame, so other objects' spawns interleaved differently and C2/048A's first-fit put
  // everything one slot along: world 4 went from 21 differing slots to 6 the moment the budget was
  // lifted (sequence_table.md 9.10).
  // ⚠ The counter stays only as a RUNAWAY GUARD -- a script that never yields would hang the ROM
  // too, so this is a host safety net, not a modelled limit, and it sits far above anything measured.
  for (int guard = 0; guard < 65536; guard++) {
    const uint8_t* op = obScript(ram, B[0x09], pc, opbuf);   // [$58] long, bank = +09 (C2/0F67)
    int adv = -1;                                   // -1 = yield (keep pc)
    // CT_OWOBJTRACE=1: one line per opcode executed. The overworld's bootstrap runs dozens of
    // ops inside a single frame, so a per-FRAME pc is too coarse to find where the port's path
    // leaves the cartridge's -- this is what makes the two comparable op by op.
    if (g_obTrace < 0) g_obTrace = getenv("CT_OWOBJTRACE") ? 1 : 0;
    if (g_obTrace)
      printf("obrun: pc=%04X op=%02X %02X %02X %02X %02X\n", pc,
             op[0], op[1], op[2], op[3], op[4]);
    switch (op[0]) {
      case 0x00:                                    // reset state (keep +07/+09/+0A)
        obPutW(B + OWOBJ_GOSUB, 0); obPutW(B + OWOBJ_ANIM, 0); obPutW(B + OWOBJ_ANIMBK, 0);
        for (int i = 0x10; i < OWOBJ_STRIDE; i += 2) obPutW(B + i, 0);
        adv = 1; break;
      case 0x01: B[OWOBJ_ATTR] = (uint8_t)((B[OWOBJ_ATTR] & 0xF1) | op[1]); adv = 2; break;
      case 0x02: B[OWOBJ_ATTR] = (uint8_t)((B[OWOBJ_ATTR] & 0x4F) | op[1]); adv = 2; break;
      // C2/10CD -- A PALETTE ROW. `LDX $4E / LDY #01 / LDA [$58],Y / STA $0035,X` (the source word, M = 16) / `SEP #20 /
      // Y = 3 -> +37` (its bank) / `Y = 4 -> +32` (the CGRAM row) / `STZ $0033,X` / `Y = 5 -> +34` / `LDX #1DD4 / JSR C2/048A
      // / LDA #0006`. The operands land in the SCRIPT'S OWN block and reach the child through C2/048A's MVN; the dispatcher's
      // +32 clear after the advance then zeroes the parent's copy. This is the world's palette animation: every world
      // script cycles a row through frames of the $7E:C000 packet the loader decompresses (C2/2930) -- world 0 row 2
      // ($C000/$C020/$C040/$C060, ten frames apart), world 3 rows 3 and 4. Class $1DD4 is obPalRow below.
      case 0x04:
        B[0x35] = op[1]; B[0x36] = op[2]; B[0x37] = op[3];
        B[0x32] = op[4]; B[0x33] = 0; B[0x34] = op[5];
        obAllocNative(ram, B, 0x1DD4);
        adv = 6; break;
      // C2/10BF -- A VRAM UPLOAD. `SEP #20 / LDX #1CF5 / JSR C2/048A / REP #20 / LDA #000A / RTS`: the op only spawns class
      // $1CF5 (obVramUp below) and steps over its nine operand bytes; the child reads them through its copy of the parent's
      // +07 (C2/0F86 stores the pc after every advance -- the port keeps it in a local, so it is stored first, as 0x28/0x29).
      // Lavos's rising (overworld 1F4): `4E 28 E0 C6` decompresses the picture to $7F:9000, then `03 00 90 7F 00 00 00 00 00
      // 01` sends its $100 tiles to VRAM word 0 -- the port PARKED here (no case), and the map stayed black.
      case 0x03:
        obPutW(B + OWOBJ_PC, pc);
        obAllocNative(ram, B, 0x1CF5);
        adv = 10; break;
      case 0x05: CtWorldObjOp05(ram, op); adv = 5; break;   // C2/1100 -- ENTER A LOCATION
      // C2/1B2F -- COPY A RECTANGLE OF THE MAP: `4F sb sx sy db dx dy w h`. Each end is a map buffer of $7E -- $4000 when its
      // first operand is 01, else $5800 -- plus `y * $60` (`STA $4202 / LDA #60 / STA $4203`, the row stride) + x; $10-$12 /
      // $13-$15 hold the two long pointers. Then h rows ($01, `DEC $01 / BNE`) of w bytes ($00 -> $02 each row) through
      // WMADD ($2181 = $13 per row, $2183 = 7E, `LDA [$10],Y / STA $2180`), both pointers + $60 a row. 1BDC LDA #0009.
      case 0x4F: {
        uint16_t src = (uint16_t)((op[1] == 1 ? 0x4000 : 0x5800) + op[3] * 0x60 + op[2]);
        uint16_t dst = (uint16_t)((op[4] == 1 ? 0x4000 : 0x5800) + op[6] * 0x60 + op[5]);
        int w = op[7] ? op[7] : 256, h = op[8] ? op[8] : 256;   // `DEC $02 / BNE` and `DEC $01 / BNE` from a zero: 256
        for (int r = 0; r < h; r++) {
          for (int k = 0; k < w; k++) ram[(uint16_t)(dst + k)] = ram[(uint16_t)(src + k)];
          src = (uint16_t)(src + 0x60); dst = (uint16_t)(dst + 0x60);
        }
        ram[0x00] = op[7]; ram[0x01] = 0; ram[0x02] = 0;
        ram[0x10] = (uint8_t)src; ram[0x11] = (uint8_t)(src >> 8); ram[0x12] = 0x7E;
        ram[0x13] = (uint8_t)dst; ram[0x14] = (uint8_t)(dst >> 8); ram[0x15] = 0x7E;
        adv = 9; break;
      }
      case 0x07: {                                  // plot a tile byte into a map buffer
        static const uint16_t BASES[6] = { 0x4000, 0x5800, 0x7000, 0x6000, 0x6800, 0x7000 };
        int bi = op[1] ? op[1] - 1 : 0;
        ram[BASES[bi % 6] + op[2] + op[3] * 0x60] = op[4];
        adv = 5; break;
      }
      case 0x08: {                                  // register a sub-script ptr by area id
        uint16_t ptr = (uint16_t)(op[1] | (op[2] << 8));
        if (op[3] == 7) obPutW(ram + 0x1B41, ptr);
        else if (op[3] == ram[0x2980]) obPutW(ram + 0x1B3B, ptr);
        else if (op[3] == ram[0x2981]) obPutW(ram + 0x1B3D, ptr);
        else if (op[3] == ram[0x2982]) obPutW(ram + 0x1B3F, ptr);
        adv = 4; break;
      }
      case 0x09: case 0x43: {                       // spawn a VM clone child
        obSpawnScratch(ram, op[3], (uint16_t)(op[1] | (op[2] << 8)));   // C2/11F6 / 1A2D -> C2/04D9 / 04FB
        int base = op[0] == 0x43 ? OWOBJ_BASE : OWOBJ_CHILD_BASE, count = op[0] == 0x43 ? 4 : 60;
        for (int n = 0; n < count; n++) {
          uint8_t* C = ram + base + n * OWOBJ_STRIDE;
          if (obW(C)) continue;
          obPutW(C, OWOBJ_CLASS_VM); C[2] = C[3] = 0;
          memcpy(C + OWOBJ_GOSUB, B + OWOBJ_GOSUB, 0x3B);
          obPutW(C + OWOBJ_PC, (uint16_t)(op[1] | (op[2] << 8)));
          C[0x09] = op[3]; C[OWOBJ_WAIT] = 0;
          break;
        }
        adv = 4; break;
      }
      case 0x35: {                                  // C2/1839: allocate a NATIVE-class object
        // ⭐ `LDY #01 / LDA [$58],Y / TAX / JSR $048A / LDA #0003` -- the operand IS the class word,
        // and unlike op $09 no script pointer is installed: a native class is dispatched by $0000.
        // This is what the overworld's BOOTSTRAP script parks on when it is missing: world 5's
        // $7F:0439 is `35 49 78`, the class-$7849 object, and without it the bootstrap never reaches
        // the ops after it (sequence_table.md 9.5).
        // ⚠ C2/048A zeroes +2 AND the 16-bit pair +3/+4 before the MVN, which copies the PARENT's
        // block from +5 up (0x3B bytes). Op $09 above clears only +2/+3 -- a one-byte difference
        // that predates this and is left alone rather than changed under no check.
        obAllocNative(ram, B, (uint16_t)(op[1] | (op[2] << 8)));
        adv = 3; break;
      }
      // C2/1947 -- `LDA [$58],Y / STA $00 / JSR $09C5`, length 2. ⭐ What $09C5 DOES is rebuild a
      // BG's whole tilemap: micro-executed with `--callrom C209C5 --pokewram 7E0000=01` it makes
      // over 77000 writes, into the $7E:F000 staging area and the scroll shadows ($E3/$E7 scaled by
      // eight into $20/$26). The port already does that work, in CtWorldTilemapBuild, which
      // worldEnterAt runs at every world entry and which `ow-streaming` holds byte-exact against the
      // oracle's live VRAM -- so advancing past this op is NOT dropping the behaviour, it is leaving
      // it with the graded owner.
      // ⚠ This is not the same as op $05's old stub, and the difference is the point: $05's effect
      // (the location change) was something NOTHING else in the port did. Driving a per-BG rebuild
      // from the VM -- $09C5 selects one with `$00 & 3`, and 0 takes a different path at $0A6A --
      // is a real difference from the port's unconditional both-BG rebuild, and it is a camera
      // change, not an opcode one. ⚠ `$00` is deliberately NOT written: the ROM's own routine
      // overwrites it immediately (`C2/0AAB` stores $0F there), so mirroring only the store would
      // leave a value the cartridge never keeps.
      // ⭐ ...BUT THE SCROLL WORDS ARE NOT THE HOST'S TO SET LATER. 09D8-09E7 is `DEC A / ASL A / TAX /
      // LDA $E3,X / ASL x3 / STA $20,X / LDA $E7,X / ASL x3 / STA $26,X` -- the camera word is re-derived
      // from the cells INSIDE the object pass, so an object that runs after this one in the same pass
      // already scrolls from the new place. The port left it to a read-back after the pass, which was
      // invisible until an object could scroll: the attract's flyover driver does `13 E3 00 60 / ... /
      // 3E 01 / 3E 02 / ... / 09 8A 05 7F`, its pan child stepped the OLD camera (tile 185) in that same
      // pass and wrote 184 back over the script's $60. MEASURED: $0020 = 0300 at f3 (C2:09E2), then
      // 02FF at f5. So the camera for layer `$00 & 3` (1 = BG1, 2 = BG2) is set here and that layer's
      // tilemap rebuilt; a column already queued for it this frame is superseded by the rebuild. (`$00`
      // = 0 takes C2/0A6A, not derived here -- it still only advances.)
      case 0x3E:
        if (g_obStream && (op[1] & 3) >= 1 && (op[1] & 3) <= 2) {
          int bg = (op[1] & 3) - 1;
          uint16_t hw = (uint16_t)((ram[0xE3 + bg * 2] | (ram[0xE4 + bg * 2] << 8)) << 3);
          uint16_t vw = (uint16_t)((ram[0xE7 + bg * 2] | (ram[0xE8 + bg * 2] << 8)) << 3);
          if (bg) { c->bg2h = hw; c->bg2v = vw; } else { c->bg1h = hw; c->bg1v = vw; }
          owTilemapBuildBg(g_obStream, ram, c, bg);
          owRebuildScratch(ram, bg);                   // C2/09C5 leaves its direct-page words (C3/02DD reads $0008)
          int k = 0;
          for (int q = 0; q < g_obStream->nPend; q++)
            if (g_obStream->pend[q].bg != bg) g_obStream->pend[k++] = g_obStream->pend[q];
          g_obStream->nPend = k;
        }
        adv = 2; break;
      case 0x0A: B[op[1]] = 0; adv = 2; break;
      case 0x0B: B[op[1]]++; adv = 2; break;        // C2/121D: B[operand]++ (the fair release-3 sub 07FC
                                                    // interleaves 0B between its 3 spawns; parking here gave 1 bunch + a frozen blob)
      case 0x0C: B[op[1]]--; adv = 2; break;        // C2/1230: `LDA ($4E),Y / DEC A / STA ($4E),Y`, Y = the operand
      case 0x0D: B[op[1]] = op[2]; adv = 3; break;
      case 0x0E: B[op[1]] |= op[2]; adv = 3; break;
      case 0x0F: B[op[1]] &= (uint8_t)~op[2]; adv = 3; break;
      case 0x10: ram[op[1] | (op[2] << 8)] = 0; adv = 3; break;
      case 0x11: ram[op[1] | (op[2] << 8)]++; adv = 3; break;
      // C2/12A7 / C2/12B8 / C2/12CE -- the other three ABSOLUTE-WRAM writers, same shape as 0x10/0x11:
      //   `LDY #01 / LDA [$58],Y / TAX / SEP #20 / ... $0000,X / REP #20 / LDA #len / RTS`.
      //   0x12 DEC abs (len 3) | 0x13 abs := imm (len 4, imm read at Y=3) | 0x14 abs |= imm (len 4).
      // ⭐ 0x13 IS HOW A WORLD SETS ITS COLOUR MATH. $0046 is the CGADSUB shadow the overworld NMI
      // pushes to $2131 (C2/0296 `LDA $46 / STA $2131`), and each world's own object script stores it:
      // world 0 (1000 A.D.) 0x44 = half-add on BG3, world 1 (600 A.D.) 0x04 = FULL add on BG3.
      // Measured: `--monitor 7E0046:1 --writer` walking out of loc 112 into world 1 shows
      // `f73 7E0046=04 (C2:12C8)` -- C2/12C8 is the instruction after this handler's `STA $0000,X`.
      // Without the op the shadow kept world 0's 0x44 and 600 A.D.'s mist composited half-bright.
      case 0x12: ram[op[1] | (op[2] << 8)]--; adv = 3; break;
      case 0x13: ram[op[1] | (op[2] << 8)] = op[3]; adv = 4; break;
      case 0x14: ram[op[1] | (op[2] << 8)] |= op[3]; adv = 4; break;
      case 0x1A: pc = (uint16_t)((op[1] | (op[2] << 8)) + 1); obPutW(B + OWOBJ_OPSTATE, 0); continue;
      case 0x1B:
        if (--B[op[1]] != 0) { pc = (uint16_t)(pc + (int8_t)op[2]); obPutW(B + OWOBJ_OPSTATE, 0); continue; }
        adv = 3; break;
      case 0x22: if (ram[op[1] | (op[2] << 8)] == 0) { pc = (uint16_t)(pc + (int8_t)op[3]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 4; break;
      case 0x23: if (ram[op[1] | (op[2] << 8)] != 0) { pc = (uint16_t)(pc + (int8_t)op[3]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 4; break;
      // C2/14AD -- op 0x25's opposite: `CMP $0000,X / BEQ ->14CC` steps 5 when the byte EQUALS the operand, else returns the
      // sign-extended offset (`TDC / INY / LDA [$58],Y / BPL / ORA #FF00`) and the dispatcher jumps by it.
      case 0x24: if (ram[op[1] | (op[2] << 8)] != op[3]) { pc = (uint16_t)(pc + (int8_t)op[4]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 5; break;
      // C2/1A52 (0x44) / C2/1AAC (0x45) -- SET / CLEAR BIT 7 OF A RECORD: `JSR ($1A70,X)` on operand 1 (three real entries --
      // the table's later words are the code after it) points $10 at record operand 2 of one of three $7E arrays:
      //   1A76  $10 = $7E00 + n * 7  (the world's entrance records)   1A8A  $8300 + n * 3   1A9B  $8180 + n * 3
      // (each `STA $00` the 16-bit n first), then $12 = 7E and `LDA [$10] / ORA #80` (0x44) or `AND #7F` (0x45). Three bytes.
      case 0x44: case 0x45: {
        static const uint16_t BASE[3] = { 0x7E00, 0x8300, 0x8180 };
        static const uint8_t SIZE[3] = { 7, 3, 3 };
        if (op[1] > 2) { static int said = 0;
          if (!said) { said = 1; fprintf(stderr, "owobj: op %02X sub %02X is past C2:1A70's three entries -- not run\n", op[0], op[1]); }
          adv = 3; break; }
        uint16_t a = (uint16_t)(BASE[op[1]] + op[2] * SIZE[op[1]]);
        ram[0x00] = op[2]; ram[0x01] = 0;
        ram[0x10] = (uint8_t)a; ram[0x11] = (uint8_t)(a >> 8); ram[0x12] = 0x7E;
        if (op[0] == 0x44) ram[a] |= 0x80; else ram[a] &= 0x7F;
        adv = 3; break;
      }
      case 0x25: if (ram[op[1] | (op[2] << 8)] == op[3]) { pc = (uint16_t)(pc + (int8_t)op[4]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 5; break;
      case 0x26: if ((ram[op[1] | (op[2] << 8)] & op[3]) != 0) { pc = (uint16_t)(pc + (int8_t)op[4]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 5; break;
      case 0x27: if ((ram[op[1] | (op[2] << 8)] & op[3]) == 0) { pc = (uint16_t)(pc + (int8_t)op[4]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 5; break;
      case 0x4C: if (ram[op[1] | (op[2] << 8)] < op[3]) { pc = (uint16_t)(pc + (int8_t)op[4]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 5; break;
      // C2/1566 -- op $4C's MIRROR: `CMP [$58],Y / BCC ->1585` takes the FALL-THROUGH when the cell
      // is below the immediate, so the branch is >=. Same shape otherwise: sign-extended
      // displacement on the taken arm (`BPL / ORA #FF00`), `LDA #0005` on the other.
      // ⭐ World 0's bootstrap parks here without it: `4D A6 1B 51 09` at $7F:0503.
      case 0x4D: if (ram[op[1] | (op[2] << 8)] >= op[3]) { pc = (uint16_t)(pc + (int8_t)op[4]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 5; break;
      // C2/158B (0x28) / C2/1599 (0x29): `LDX #20A2` / `LDX #2105`, `JSR C2/048A`, `LDA #0002` -- allocate the native
      // FADE object: 0x28 fades out, 0x29 fades in (obFade below). C2/048A is op $35's allocator: the first free block from
      // $0C30 (60), the class word, +3/+4 and +2 zeroed, then the MVN of the parent's +05..+3F. The child finds its rate
      // through that copy: the parent's +07 is THIS op's address (C2/0F86 stores the pc after every advance), and the
      // handler reads `[+07],Y` with Y = 1 -- the operand. The port keeps the pc in a local until the visit ends, so it is
      // stored first.
      case 0x28: case 0x29: {
        obPutW(B + OWOBJ_PC, pc);
        obAllocNative(ram, B, op[0] == 0x28 ? 0x20A2 : 0x2105);
        adv = 2; break;
      }
      case 0x2A: case 0x2B: {                       // C2/15A7 / 15B5: the MOSAIC up (class $21F8) / down ($2194), 2 operands
        obPutW(B + OWOBJ_PC, pc);
        obAllocNative(ram, B, op[0] == 0x2A ? 0x21F8 : 0x2194);
        adv = 3; break;
      }
      // C2/15C3: the place, `STA $0014,X / STZ $0012,X` and `STA $0018,X / STZ $0016,X` -- the fractions cleared with it (the
      // 600 A.D. event's second arc lands a pixel lower with the last arc's fraction kept).
      case 0x2C: obPutW(B + OWOBJ_XPOS, (uint16_t)(op[1] | (op[2] << 8))); obPutW(B + OWOBJ_XFRAC, 0);
                 obPutW(B + OWOBJ_YPOS, (uint16_t)(op[3] | (op[4] << 8))); obPutW(B + OWOBJ_YFRAC, 0); adv = 5; break;
      case 0x2E: obPutW(B + OWOBJ_VELXF, (uint16_t)(op[1] | (op[2] << 8))); obPutW(B + OWOBJ_VELXI, (uint16_t)(op[3] | (op[4] << 8))); adv = 5; break;
      case 0x2F: obPutW(B + OWOBJ_VELYF, (uint16_t)(op[1] | (op[2] << 8))); obPutW(B + OWOBJ_VELYI, (uint16_t)(op[3] | (op[4] << 8))); adv = 5; break;
      case 0x30: obPutW(B + OWOBJ_ANIM, CtWorldAnimPtrForId(op[1])); B[OWOBJ_ANIMBK] = 0xC3; B[OWOBJ_DWELL] = 0; adv = 2; break;  // anim ptr (C2/1CDA)
      case 0x31:                                    // move + animate for n frames
        if (B[OWOBJ_WAIT] == 0) { B[OWOBJ_WAIT] = op[1]; }
        else if (--B[OWOBJ_WAIT] == 0) { adv = 2; break; }
        obIntegrate(B); obAnimTick(ram, B);
        adv = -1; break;
      case 0x32:                                    // C2/164B camera-pan-mover: timed normalize+integrate (NO wrap/anim)
        if (B[OWOBJ_WAIT] == 0) {                          // first visit: load timer + ZERO the 4 accumulators
          B[OWOBJ_WAIT] = op[1];
          obPutW(B + OWOBJ_XFRAC, 0); obPutW(B + OWOBJ_XPOS, 0);  // X frac/int
          obPutW(B + OWOBJ_YFRAC, 0); obPutW(B + OWOBJ_YPOS, 0);  // Y frac/int
        } else if (--B[OWOBJ_WAIT] == 0) { adv = 2; break; }
        obSignNorm(B);                               // C2/1759 BEFORE integrate
        obAdd32(B + OWOBJ_XFRAC, B + OWOBJ_XPOS, obW(B + OWOBJ_VELXF), obW(B + OWOBJ_VELXI));   // C2/0F38 X  (NO obWrapWorld, NO obAnimTick)
        obAdd32(B + OWOBJ_YFRAC, B + OWOBJ_YPOS, obW(B + OWOBJ_VELYF), obW(B + OWOBJ_VELYI));   //           Y
        // ...AND THE WHOLE PIXELS ARE THE CAMERA. The position words are only an accumulator here (zeroed
        // on entry, re-normalised by C2/1759 every frame); what the handler does with them is scroll:
        //   167F JSR C2/1784 / BCC / $00 = 1: JSR C2/0568 / $00 = 2: JSR C2/0568      BG1 and BG2 by $01
        //   1694 unless $1BA7 bit4 / $1BAA bit5 / $1BAF bit1: `$24 += $01` (sign-extended)   BG3
        //   16BC JSR C2/17AB / BCC / $00 = 1,2: JSR C2/066C / same gates: `$2A += $01`      the Y twin
        // The port integrated and threw the pixels away, so the attract's flyover never panned. MEASURED
        // (PEEK_SAVE f100, --monitor "7E0020:2,7E0022:2,7E0024:2" --writer): its child `2E 00 00 FF FF /
        // 2F 00 00 00 00 / 32 F6` moves $0022 and $0024 down one per frame from f5 (C2:05C7 / C2:16B8).
        { int d;
          if (obWholeStep(B + OWOBJ_XFRAC, B + OWOBJ_XPOS, &d)) {
            obScroll(ram, c, 0, 0, d); obScroll(ram, c, 1, 0, d);
            if (!(ram[0x1BA7] & 0x10) && !(ram[0x1BAA] & 0x20) && !(ram[0x1BAF] & 0x02)) c->bg3h = (uint16_t)(c->bg3h + d);
          }
          if (obWholeStep(B + OWOBJ_YFRAC, B + OWOBJ_YPOS, &d)) {
            obScroll(ram, c, 0, 1, d); obScroll(ram, c, 1, 1, d);
            if (!(ram[0x1BA7] & 0x10) && !(ram[0x1BAA] & 0x20) && !(ram[0x1BAF] & 0x02)) c->bg3v = (uint16_t)(c->bg3v + d);
          }
        }
        adv = -1; break;
      // C2/16FA -- ONE LAYER'S PAN, op 0x32's twin for the layer the script names: `51 <layer> <frames>`. The timer is
      // operand 2 (16FA-1708: +0A, the four accumulators zeroed, on into the step the same visit; 171B DEC +0A / BNE, and
      // 0 -> LDA #0003), the step is op 0x32's (1726 JSR C2/1759 / JSR C2/0F38), and the whole pixels go to that layer
      // alone: 172E JSR C2/1784 / BCC / `$00 = operand 1 / JSR C2/0568`, 1743 JSR C2/17AB / BCC / `$00 = operand 1 / JSR
      // C2/066C` -- no BG3 arm, no gates. 1754 TDC / CLC / RTS: it yields. C2/0568 / 066C take the layer as `$00 & 3`
      // (1 = BG1, 2 = BG2, 0 = nothing). Lavos's rising pans BG2 with it (`51 02 01` at pc 0721 / 0774).
      case 0x51: {
        if (B[OWOBJ_WAIT] == 0) {
          B[OWOBJ_WAIT] = op[2];
          obPutW(B + OWOBJ_XFRAC, 0); obPutW(B + OWOBJ_XPOS, 0);
          obPutW(B + OWOBJ_YFRAC, 0); obPutW(B + OWOBJ_YPOS, 0);
        } else if (--B[OWOBJ_WAIT] == 0) { adv = 3; break; }
        obSignNorm(B);
        obAdd32(B + OWOBJ_XFRAC, B + OWOBJ_XPOS, obW(B + OWOBJ_VELXF), obW(B + OWOBJ_VELXI));
        obAdd32(B + OWOBJ_YFRAC, B + OWOBJ_YPOS, obW(B + OWOBJ_VELYF), obW(B + OWOBJ_VELYI));
        int bg = (op[1] & 3) - 1, d;
        if (bg > 1) {                               // BG3 through C2/0568's third table row: not a layer obScroll steps
          static int said = 0;
          if (!said) { said = 1; fprintf(stderr, "owobj: op 51 on layer 3 -- the pan is not applied\n"); }
          bg = -1;
        }
        if (obWholeStep(B + OWOBJ_XFRAC, B + OWOBJ_XPOS, &d) && bg >= 0) obScroll(ram, c, bg, 0, d);
        if (obWholeStep(B + OWOBJ_YFRAC, B + OWOBJ_YPOS, &d) && bg >= 0) obScroll(ram, c, bg, 1, d);
        adv = -1; break;
      }
      // C2/1ACA / C2/1AE1 -- A BLOCK BYTE PLUS / MINUS AN IMMEDIATE: `46 off n` = block[off] += n, `47 off n` = block[off]
      // -= n (M = 8, `LDA [$58],Y / TAX` with B = 0 from the dispatcher's AND #00FF, `ADC ($4E),Y` / `SBC [$58],Y`, the carry
      // CLC / SEC first). Three bytes each.
      case 0x46: B[op[1]] = (uint8_t)(B[op[1]] + op[2]); adv = 3; break;
      case 0x47: B[op[1]] = (uint8_t)(B[op[1]] - op[2]); adv = 3; break;
      case 0x34: obNative(ram, B, (uint16_t)(op[1] | (op[2] << 8)), c); adv = 3; break;
      case 0x36: obPutW(B + OWOBJ_GOSUB, (uint16_t)(pc + 3)); pc = (uint16_t)(op[1] | (op[2] << 8)); obPutW(B + OWOBJ_OPSTATE, 0); continue;
      case 0x37: pc = obW(B + OWOBJ_GOSUB); obPutW(B + OWOBJ_OPSTATE, 0); continue;
      case 0x38:
        if (B[OWOBJ_WAIT] == 0) { B[OWOBJ_WAIT] = op[1]; adv = -1; break; }
        if (--B[OWOBJ_WAIT] != 0) { adv = -1; break; }
        adv = 2; break;
      case 0x39:
        if (B[OWOBJ_WAIT] == 0) { B[OWOBJ_WAIT] = op[1]; obAnimTick(ram, B); adv = -1; break; }
        if (--B[OWOBJ_WAIT] != 0) { obAnimTick(ram, B); adv = -1; break; }
        adv = 2; break;
      case 0x3F: case 0x40: {                       // fly to absolute X (3F) / Y (40)
        int isX = op[0] == 0x3F;
        uint8_t* posp = B + (isX ? OWOBJ_XPOS : OWOBJ_YPOS);
        uint8_t* fracp = B + (isX ? OWOBJ_VELXF : OWOBJ_VELYF);
        uint8_t* intp = B + (isX ? OWOBJ_VELXI : OWOBJ_VELYI);
        if (B[OWOBJ_OPSTATE] == 0) {
          B[OWOBJ_OPSTATE] = 1; obPutW(B + OWOBJ_FLYTGT, (uint16_t)(op[1] | (op[2] << 8)));
          int16_t vi = (int16_t)obW(intp);
          if (obW(B + OWOBJ_FLYTGT) < obW(posp)) { if (vi >= 0) obNeg32(fracp, intp); }
          else { if (vi < 0) obNeg32(fracp, intp); }
        }
        if (obW(posp) == obW(B + OWOBJ_FLYTGT)) { adv = 5; break; }
        if (isX) obAdd32(B + OWOBJ_XFRAC, B + OWOBJ_XPOS, obW(B + OWOBJ_VELXF), obW(B + OWOBJ_VELXI));
        else obAdd32(B + OWOBJ_YFRAC, B + OWOBJ_YPOS, obW(B + OWOBJ_VELYF), obW(B + OWOBJ_VELYI));
        obWrapWorld(B); obAnimTick(ram, B);
        adv = -1; break;
      }
      // ---- The sub-VM's SOUND ops (handler table C2/0F91[op*2], verified: op 0x30 -> C2/160F which
      // JSRs C2/1CDA, op 0x32 -> C2/164B, op 0x3B -> C2/18C7). 0x3B was previously labelled a
      // "graphics-LOAD request ... render reads +0B" and stubbed to advance-only; the disassembly of
      // C2/18C7 is a SOUND request (the +0B render class belongs to op 0x30's C2/1CDA, not here). The
      // `adv` values below are the handlers' own `LDA #000n / RTS` returns, which is why the stub kept
      // the scripts in sync while dropping every effect. 0x3C/0x3D/0x4B had no case at all and fell to
      // `default: adv = -1`, which PARKS the object.
      //
      // C2/18C7 (cmd $18) and C2/18D0 (cmd $19) share a tail: stage $1BE8-$1BEA, commit via C2/2ED9.
      // $1BEB is deliberately NOT written -- it keeps the previous request's byte, the same stickiness
      // the field mailbox's $1E03 has (see the 0xE8/0xEA family in ct_vm.c).
      case 0x3B: case 0x3C:
        ram[0x1BE8] = (op[0] == 0x3B) ? 0x18 : 0x19;  // 218C9 LDA #18 / 218D2 LDA #19 -> STA $1BE8
        ram[0x1BE9] = op[1];                          // 218D7 LDY #0001 / LDA [$58],Y / STA $1BE9
        ram[0x1BEA] = op[2];                          // 218DF INY / LDA [$58],Y / STA $1BEA
        ram[0x1BED] = 0;                              // 218E5 STZ $1BED -- priority 0 (always accepted)
        CtWorldSoundRequest(ram);                     // 218E8 JSR C2/2ED9
        adv = 3; break;                               // 218ED LDA #0003 / RTS
      // C2/18F1 -- PLAY SONG, gated on $7F01ED (the same field-song hold C2/2FAE uses at line 124).
      case 0x3D:
        CtWorldScriptPlaySong(ram, op[1]);
        adv = 2; break;                               // 2191B LDA #0002 / RTS
      // C2/18F9 -- PLAY SONG, UNGATED: op 0x3D's body from its gate on (the dispatch word C2:0F91+4A*2 = 18F9).
      case 0x4A:
        owPlaySong18F9(ram, op[1]);
        adv = 2; break;                               // 2191B LDA #0002 / RTS
      // C2/191F -- the RAW command: all four mailbox bytes come from the script.
      case 0x4B:
        ram[0x1BE8] = op[1]; ram[0x1BE9] = op[2];     // 21923-2192C
        ram[0x1BEA] = op[3]; ram[0x1BEB] = op[4];     // 2192F-21938
        ram[0x1BED] = 0;                              // 2193B STZ $1BED
        CtWorldSoundRequest(ram);                     // 2193E JSR C2/2ED9
        adv = 5; break;                               // 21943 LDA #0005 / RTS
      // C2/12E7 -- CLEAR BITS: `mem[addr] &= ~mask`, four bytes.
      //   LDY #0001 / LDA [$58],Y / TAX        -- X = the 16-bit address (operand bytes 1-2)
      //   SEP #20 / LDY #0003 / LDA [$58],Y    -- the mask (operand byte 3)
      //   EOR #FF / AND $0000,X / STA $0000,X  -- clear those bits in place
      //   REP #20 / LDA #0004 / RTS
      // ⭐ This is how the attract's world-5 FLYOVER ENDS. Its driver runs `15 A9 1B 80` --
      // `$1BA9 &= ~$80`, clearing the very story-page bit C2/26D6's MVN brings in to select the
      // attract's cutscene branch -- and the op immediately after it, at pc 0584, is `05` (C2/1100,
      // ENTER A LOCATION), the return to scene 433. With no case here the op fell to `default` and
      // parked the object FOUR BYTES short of the leg's ending, so the flyover ran forever and the
      // 433 chain never reached its next entry (notes 9.17).
      case 0x15: {
        int a = op[1] | (op[2] << 8);
        ram[a] = (uint8_t)(ram[a] & ~op[3]);
        adv = 4; break;
      }
      // C2/1B15 -- SUBTRACT: `mem[addr] -= imm`, four bytes: `LDY #0001 / LDA [$58],Y / TAX` (the 16-bit address), `SEP #20 /
      // LDY #0003 / LDA $0000,X / SEC / SBC [$58],Y / STA $0000,X / REP #20 / LDA #0004 / RTS`. World 1F3 after Lavos's fall
      // (`FF 24`'s reboot) parked an object on it at pc 0825 until this existed.
      case 0x49: {
        int a = op[1] | (op[2] << 8);
        ram[a] = (uint8_t)(ram[a] - op[3]);
        adv = 4; break;
      }
      // THE BYTE-COPY FAMILY C2/1302-1369 (16-bit A, `LDA [$58],Y` reads the op's own bytes, ($4E) = this block):
      //   16 off lo hi   C2/1302  `LDA ($4E),Y / STA $0000,X`   -- $7E:hilo := block[off]           4 bytes
      //   17 off lo hi   C2/131C  `LDA $0000,X / STA ($4E),Y`   -- block[off] := $7E:hilo           4 bytes
      //   18 dst src     C2/1336  `LDA ($4E),Y / STA ($4E),Y`   -- block[dst] := block[src]         3 bytes
      //   19 dlo dhi slo shi  C2/1350  `LDA $0000,X / STA $0000,Y` -- $7E:dst := $7E:src           5 bytes
      // Op 17 is what parked the attract's 2300 A.D. walkers (world 2, blocks 11-13 at pc 0994 / 0A03 / 0A42) at
      // (0,0): oracle attract_cena_inicial f5936 has them at (914,424) (922,428) (914,432) on scripts 09AD/0B27/0AA5.
      case 0x16: ram[op[2] | (op[3] << 8)] = B[op[1]]; adv = 4; break;
      case 0x17: B[op[1]] = ram[op[2] | (op[3] << 8)]; adv = 4; break;
      case 0x18: B[op[1]] = B[op[2]]; adv = 3; break;
      case 0x19: ram[op[1] | (op[2] << 8)] = ram[op[3] | (op[4] << 8)]; adv = 5; break;
      // ...and their BLOCK-byte branches C2/1398-141D, the $22/$23/$25 family's twins on the object's own block:
      // the taken arm returns the sign-extended displacement (`BPL / ORA #FF00 / CLC`), the other the length.
      //   1C off d      C2/1398  `LDA ($4E),Y / BNE ->len`      -- branch if block[off] == 0      3 bytes
      //   1D off d      C2/13B8  `LDA ($4E),Y / BEQ ->len`      -- branch if block[off] != 0      3 bytes
      //   1E off v d    C2/13D8  `CMP ($4E),Y / BEQ ->len`      -- branch if block[off] != v      4 bytes
      //   1F off v d    C2/13FC  `CMP ($4E),Y / BNE ->len`      -- branch if block[off] == v      4 bytes
      // The 2300 A.D. walkers stop on 1E (block 11, pc 0B4E) and 1F (block 12, pc 0A07) once op 17 runs.
      case 0x1C: if (B[op[1]] == 0) { pc = (uint16_t)(pc + (int8_t)op[2]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 3; break;
      case 0x1D: if (B[op[1]] != 0) { pc = (uint16_t)(pc + (int8_t)op[2]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 3; break;
      case 0x1E: if (B[op[1]] != op[2]) { pc = (uint16_t)(pc + (int8_t)op[3]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 4; break;
      case 0x1F: if (B[op[1]] == op[2]) { pc = (uint16_t)(pc + (int8_t)op[3]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 4; break;
      // C2/1420 / C2/1444, the bit-test pair of the same family: `AND ($4E),Y` of the block byte op[1] with the mask op[2],
      // then 0x20 BEQ -> advance 4 (so it branches on ANY bit set) and 0x21 BNE -> advance 4 (it branches on none set).
      // The ferry's bootstrap parked an object on `20 22 80 53` at $7F:0A3D until this existed (the Truce pay, saves/ferry_re).
      case 0x20: if (B[op[1]] & op[2]) { pc = (uint16_t)(pc + (int8_t)op[3]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 4; break;
      case 0x21: if (!(B[op[1]] & op[2])) { pc = (uint16_t)(pc + (int8_t)op[3]); obPutW(B + OWOBJ_OPSTATE, 0); continue; } adv = 4; break;
      case 0x4E: CtWorldObjOp4E(ram, op); adv = 4; break;   // C2/181C -- INDIRECT FAR CALL
      case 0x52: obPutW(B, 0); return 0;            // die: free the block
      // An op with no case PARKS the object, and a parked object re-reads the same byte every frame
      // for the rest of the run -- silently. That silence is what hid ops 0x15 and 0x4E: the attract
      // looked "stuck on a scene" rather than "stopped on an opcode". Name it once per opcode; the
      // handler's real address is the word at C2:0F91[op*2] (self-checked: op 4E -> C2/181C).
      default: {
        static uint8_t said[256];
        if (!said[op[0]]) { said[op[0]] = 1;
          fprintf(stderr, "owobj: op %02X has no case -- object PARKED at pc=%04X (handler is the "
                          "word at C2:0F91+%02X*2)\n", op[0], pc, op[0]); }
        adv = -1; break;
      }
    }
    if (adv < 0) break;
    pc = (uint16_t)(pc + adv);
    obPutW(B + OWOBJ_OPSTATE, 0);                            // the dispatcher zeroes +32 on every advance
    if (g_owSuspendNmis > 0) { obPutW(B + OWOBJ_PC, pc); return 2; }   // an inline JSL holds the pass (see obNative)
  }
  obPutW(B + OWOBJ_PC, pc);
  return 1;
}

// THE TWO FADE OBJECTS (classes $20A2 and $2105, allocated by ops 0x28/0x29). They own the overworld's brightness: the
// NMI writes `$1C | $1D` to INIDISP (C2/031F) and nothing else in bank C2 steps $1D. $1BF6 is their mutex (bit 1 = a
// fade-out is running, bit 0 = a fade-in). The rate is the spawning op's operand, read through the copied pc; a rate of 0
// is a CUT. Their counter is the block's +3, which the object loop bumps after every CLC (C2/054E) -- so a step waits
// until +3 reaches the rate, then clears it. Returns 1 = SEC (the loop frees the block), 0 = CLC.
// M is 8-bit in both handlers (the loop's `SEP #20` at C2/053A), so every +3 store and compare is one byte.
// THE MOSAIC OBJECTS, classes $21F8 (up: ops 0x2A) and $2194 (down: 0x2B), on `$1F` -- the MOSAIC shadow ($2106: the size
// in the high nybble, the layers in the low). Their script operands: +1 the layers OR-ed into `$1F`, +2 the frames a step
// (0: at once -- up to F, or down to 0). A step every +2 frames: up `$1F += 10` until the size is F, down `- 10` to 0.
static int obMosaic(uint8_t* ram, uint8_t* B, uint16_t cls) {
  uint8_t rb[8];
  const uint8_t* op = obScript(ram, B[0x09], obW(B + OWOBJ_PC), rb);   // `LDA $0007,X / STA $10 / LDA $0009,X / STA $12`
  int up = (cls == 0x21F8);
  if (B[2] == 0) {                                                     // C2/2209 / 21A7
    B[2]++;
    ram[0x1F] |= op[1];                                                // 221D / 21BB ORA [$10],Y (1)
    if (!op[2]) { if (up) ram[0x1F] |= 0xF0; else ram[0x1F] &= 0x0F; return 1; }   // 2228 / 21C6 BNE: at once, SEC
    B[OWOBJ_WAIT] = op[2]; B[3] = 0;                                   // 2232 / 21D0 STA $000A,X / STZ $0003,X
  }
  if (B[3] < B[OWOBJ_WAIT]) return 0;                                  // 223C / 21DA CMP $000A,X / BCS
  B[3] = 0;
  if (up) { ram[0x1F] = (uint8_t)(ram[0x1F] + 0x10); return (ram[0x1F] & 0xF0) == 0xF0; }   // 2249 ADC #10 / CMP #F0
  if (!(ram[0x1F] & 0xF0)) return 1;                                   // 21E7 AND #F0 / BEQ -> SEC
  ram[0x1F] = (uint8_t)(ram[0x1F] - 0x10);                             // 21ED SBC #10
  return 0;
}
static int obFade(uint8_t* ram, uint8_t* B, uint16_t cls) {
  uint8_t rb[8];
  uint8_t rate0 = obScript(ram, B[0x09], (uint16_t)(obW(B + OWOBJ_PC) + 1), rb)[0];   // `LDY #0001 / LDA [$10],Y`, $12 = +09
  { static int ft = -1; if (ft < 0) { const char* e = getenv("CT_OWFADETRACE"); ft = (e && *e) ? 1 : 0; }
    if (ft && B[2] == 0) printf("owfade-obj: cls=%04X blk=%04X pc=%02X:%04X rate=%02X $1BF6=%02X $1C=%02X $1D=%02X\n", cls,
                                (unsigned)(B - ram), B[9], obW(B + OWOBJ_PC), rate0, ram[0x1BF6], ram[0x1C], ram[0x1D]); }
  if (cls == 0x20A2) {                                // FADE OUT, C2/20A2 `JMP ($20AB,X)`: 20AF / 20DF
    if (B[2] == 0) {
      if (ram[0x1BF6] & 0x02) return 1;               // 20AF LDA $1BF6 / BIT #02 / BNE ->20D7 (SEC)
      ram[0x1BF6] |= 0x02;                            // 20B6 LDA #02 / TSB $1BF6
      B[2]++;                                         // 20BD INC $0002,X
      if (rate0 == 0) { ram[0x1C] = 0x80; ram[0x1D] = 0; return 1; }   // 20CF BNE / 20D1 $1C = 80 / STZ $1D / SEC
      B[OWOBJ_WAIT] = rate0; B[3] = 0;                // 20D9 STA $000A,X / 20DC STZ $0003,X -- and on into 20DF
    }
    if (B[3] < B[OWOBJ_WAIT]) return 0;               // 20E3 LDA $0003,X / CMP $000A,X / BCS / CLC
    B[3] = 0;                                         // 20ED STZ $0003,X
    if (ram[0x1D] != 0 && --ram[0x1D] != 0) return 0; // 20F0 LDA $1D / BEQ / DEC $1D / BEQ / CLC
    ram[0x1C] = 0x80;                                 // 20FA LDA #80 / STA $1C
    ram[0x1BF6] &= (uint8_t)~0x02;                    // 20FE LDA #02 / TRB $1BF6
    return 1;                                         // 2103 SEC
  }
  switch (B[2]) {                                     // FADE IN, C2/2105 `JMP ($210E,X)`: 2116 / 2152 / 216F / 218D
    case 0:
      if (ram[0x1BF6] != 0) break;                    // 2116 LDA $1BF6 / BNE ->218D
      ram[0x1BF6] |= 0x01;                            // 211B LDA #01 / TSB $1BF6
      if (rate0 == 0) { ram[0x1C] = 0; ram[0x1D] = 0x0F; B[2] = 3; return 0; }   // 2131 BNE / 2133 STZ $1C / $1D = 0F / state 3
      B[OWOBJ_WAIT] = rate0; B[2]++;                  // 2140 STA $000A,X / INC $0002,X
      if (ram[0x1D] != 0) B[2]++;                     // 2146 LDA $1D / BEQ / INC $0002,X
      B[3] = 0; return 0;                             // 214D STZ $0003,X / CLC
    case 1:
      if (ram[0x1BF6] & 0x02) break;                  // 2152 BIT #02 / BNE ->218D
      if (B[3] < B[OWOBJ_WAIT]) return 0;             // 215B CMP $000A,X / BCC ->216D
      B[2]++; B[3] = 0; ram[0x1C] = 0; ram[0x1D]++;   // 2163 INC $0002,X / STZ $0003,X / STZ $1C / INC $1D
      return 0;
    case 2:
      if (ram[0x1BF6] & 0x02) break;                  // 216F BIT #02 / BNE ->218D
      if (B[3] < B[OWOBJ_WAIT]) return 0;             // 217B BCC ->218B
      B[3] = 0;
      if (++ram[0x1D] != 0x0F) return 0;              // 2183 INC $1D / CMP #0F / BEQ ->218D
      break;
    default: break;                                   // state 3 = 218D
  }
  ram[0x1BF6] &= (uint8_t)~0x01;                      // 218D LDA #01 / TRB $1BF6
  return 1;                                           // 2192 SEC
}

// CLASS $1DD4, THE PALETTE ROW (allocated by op 0x04). Two frames of life:
//   1DD4 TDC / LDA $0002,X / BNE ->1E2F / LDA $0034,X / BEQ ->1DE7 / LDX #1E38 / JSR C2/048A / SEC
//   1DE7 TXY / INC $0002,X / LDX $0032,Y / INC $0B20,X / TYX / LDA $0032,X / REP #20 / ASL x5 / ADC #0920 / STA $2181
//        LDA $0035,X / STA $10 / ... / STZ $2183 / $12 = +37 / 16 x `LDA [$10],Y / STA $2180 / INY` twice / TSB $4C #02 / CLC
//   1E2F LDA $0032,X / TAX / DEC $0B20,X / SEC
// The copy goes through WMADD/WMDATA (bank bit 0 = $7E) into the CGRAM SHADOW $7E:0920 + row * 32, and `$4C` bit 1 asks
// the next NMI to upload it (C2/01A6). $0B20 + row counts the live objects on a row. A non-zero +34 hands the row to
// class $1E38, the 32-step fade (obRowFade below) -- the attract's 1000 A.D. leg asks for one on row 8 before its white-out.
static int obPalRow(uint8_t* ram, uint8_t* B) {
  if (B[2] != 0) { ram[0x0B20 + B[0x32]]--; return 1; }                // 1E2F, X = the row (the TDC left B = 0)
  if (B[0x34] != 0) { obAllocNative(ram, B, 0x1E38); return 1; }      // 1DDF LDX #1E38 / JSR C2/048A / SEC
  B[2]++;                                                              // 1DE8
  ram[0x0B20 + (B[0x32] | (B[0x33] << 8))]++;                          // 1DEB LDX $0032,Y (16-bit) / INC $0B20,X
  uint16_t dst = (uint16_t)(0x0920 + B[0x32] * 32);
  uint8_t sb[8];
  for (int k = 0; k < 32; k++) {
    uint16_t sa = (uint16_t)(B[0x35] | (B[0x36] << 8));
    ram[(uint16_t)(dst + k)] = obScript(ram, B[0x37], (uint16_t)(sa + k), sb)[0];   // `LDA [$10],Y`, $12 = +37
  }
  ram[0x4C] |= 0x02;                                                   // 1E29 LDA #02 / TSB $4C
  return 0;
}

// CLASS $1CF5, THE VRAM UPLOAD (op 0x03). `JMP ($1D02,X)` on +02: 1D06 / 1D4C.
//   1D06 INC $0002,X / $10 = +07 (the op's address), $12 = +09 (its bank) / +0D = [op+3] (the source bank, 8-bit) / 16-bit:
//        +0B = [op+1] (the source), +0E = [op+4] (the VRAM word), +12 = [op+6] (tiles a frame), +14 = [op+8] (tiles in all) /
//        +12 == 0 -> +12 = +14 -- and falls into 1D4C the same frame
//   1D4C LDX $4E / $08 = min(+12, +14) / one NMI queue entry at $60 + $5F (INC $5E around the write, like the anim op 6):
//        $62 = +0D, $67 = 80, $60 = +0B, $63 = +0E, $65 = $0A = $08 << 5 (32-byte tiles) / $5F += 8 / STZ $5E /
//        +14 -= $08: zero -> SEC (1DB3, the block is freed) / else +0B += $0A, +0E += $0A >> 1, CLC
static int obVramUp(uint8_t* ram, uint8_t* B) {
  if (B[2] == 0) {
    B[2]++;
    uint8_t ob[16];
    const uint8_t* op = obScript(ram, B[0x09], obW(B + OWOBJ_PC), ob);
    B[0x0D] = op[3];
    B[0x0B] = op[1]; B[0x0C] = op[2];
    B[0x0E] = op[4]; B[0x0F] = op[5];
    B[0x12] = op[6]; B[0x13] = op[7];
    B[0x14] = op[8]; B[0x15] = op[9];
    if (obW(B + 0x12) == 0) obPutW(B + 0x12, obW(B + 0x14));
  }
  uint16_t n = obW(B + 0x12), left = obW(B + 0x14);
  if (n >= left) n = left;                                             // 1D51 CMP $0014,X / BCC / LDA $0014,X
  uint16_t bytes = (uint16_t)(n << 5);
  uint8_t x = ram[0x5F];
  ram[(uint8_t)(0x62 + x)] = B[0x0D]; ram[(uint8_t)(0x67 + x)] = 0x80;
  ram[(uint8_t)(0x60 + x)] = B[0x0B]; ram[(uint8_t)(0x61 + x)] = B[0x0C];
  ram[(uint8_t)(0x63 + x)] = B[0x0E]; ram[(uint8_t)(0x64 + x)] = B[0x0F];
  ram[(uint8_t)(0x65 + x)] = (uint8_t)bytes; ram[(uint8_t)(0x66 + x)] = (uint8_t)(bytes >> 8);
  ram[0x0A] = (uint8_t)bytes; ram[0x0B] = (uint8_t)(bytes >> 8);       // 1D84 STA $0A
  ram[0x08] = (uint8_t)n; ram[0x09] = (uint8_t)(n >> 8);               // 1D59 STA $08
  ram[0x5F] = (uint8_t)(x + 8); ram[0x5E] = 0;
  left = (uint16_t)(left - n);
  if (left == 0) return 1;                                             // 1DB3 SEC
  obPutW(B + 0x14, left);
  obPutW(B + 0x0B, (uint16_t)(obW(B + 0x0B) + bytes));
  obPutW(B + 0x0E, (uint16_t)(obW(B + 0x0E) + (bytes >> 1)));
  return 0;
}

// CLASS $1E38, THE ROW FADE: the shadow row walks to the op's 16 target colours in 32 steps, one step every +09 (the op's
// +34) frames. `JMP ($1E41,X)`: 1E47 / 1E86 / 1EB5.
//   1E47 INC $0002,X / LDY $0032,X / INC $0B20,Y / +09 = +34, +0D = +37, +07 = row, +0B = +35, STZ +03 +05 (16-bit) /
//        +0E = row * 32 + $0920 / JSR C2/1EC0 / CLC
//   1EC0 per colour: $08 = the shadow's colour now, $0A = the target; C2/1F11 stores one DESCRIPTOR per channel into
//        +10+k (red), +20+k (green), +30+k (blue): 0 when equal, the rise when the target is above, $80 | (fall + $1F) below
//   1E86 LDY $0007,X / LDA $0B20,Y / CMP #01 / BNE ->1EB5 (another object took the row) / LDA $0003,X / CMP $0009,X / BCC
//        (wait) / JSR C2/1F85 / STZ $0003,X / TSB $4C #02 / INC $0005,X / CMP #20 / BNE / INC $0002,X
//   1F85 per colour, C2/1FCC per channel: a rising descriptor moves the channel up one when `$C2:2062,d` is set and then
//        counts up; a falling one tests `$C2:2062,(d & 3F)`, moves down and counts down. (The table is 32 zeroes then 32
//        ones, so a rise of n moves in the last n of the 32 steps, a fall of n in the first n.)
//   1EB5 LDA $0007,X / TAX / DEC $0B20,X / SEC
static uint8_t obFadeDesc(int cur, int tgt) {                          // C2/1F11's three arms, one channel
  if (tgt == cur) return 0;                                            // BEQ -> TDC
  if (tgt > cur) return (uint8_t)(tgt - cur);                          // BCS
  return (uint8_t)(0x80 | ((cur - tgt) + 0x1F));                       // EOR #FF / INC A / CLC / ADC #1F / ORA #80
}
static int obFadeStep(uint8_t* d, const uint8_t* T) {                  // C2/1FCC, one channel: -1, 0 or +1
  int m = 0;
  if (*d == 0) return 0;
  if (!(*d & 0x80)) { if (T[*d]) m = 1; (*d)++; }
  else { if (T[*d & 0x3F]) m = -1; (*d)--; }
  return m;
}
static int obRowFade(uint8_t* ram, uint8_t* B) {
  const uint8_t* T = (g_owRom && g_owRomLen >= 0x022062 + 0x40) ? g_owRom + 0x022062 : NULL;   // $C2:2062
  if (!T) {
    static int said = 0;
    if (!said) { said = 1; fprintf(stderr, "owobj: class $1E38 needs the ROM's $C2:2062 step table -- no ROM, not run\n"); }
    return 1;
  }
  switch (B[2]) {
    case 0: {
      B[2]++;                                                          // 1E49
      uint16_t row = obW(B + 0x32);                                    // 1E4C LDY $0032,X (16-bit)
      ram[(uint16_t)(0x0B20 + row)]++;
      B[0x09] = B[0x34]; B[0x0D] = B[0x37];
      obPutW(B + 0x07, row); obPutW(B + 0x0B, obW(B + 0x35));
      obPutW(B + 0x03, 0); obPutW(B + 0x05, 0);
      obPutW(B + 0x0E, (uint16_t)(row * 32 + 0x0920));
      uint16_t dst = obW(B + 0x0E), src = obW(B + 0x0B);               // 1EC0
      uint8_t sb[8];
      for (int k = 0; k < 16; k++) {
        uint16_t cur = (uint16_t)(ram[dst] | (ram[(uint16_t)(dst + 1)] << 8));   // `LDA ($0E)`
        const uint8_t* s = obScript(ram, B[0x0D], src, sb);                         // `LDA [$19]`
        uint16_t tgt = (uint16_t)(s[0] | (s[1] << 8));
        B[0x10 + k] = obFadeDesc(cur & 0x1F, tgt & 0x1F);
        B[0x20 + k] = obFadeDesc((cur >> 5) & 0x1F, (tgt >> 5) & 0x1F);
        B[0x30 + k] = obFadeDesc((cur >> 10) & 0x1F, (tgt >> 10) & 0x1F);
        dst = (uint16_t)(dst + 2); src = (uint16_t)(src + 2);
      }
      return 0;
    }
    case 1: {
      if (ram[(uint16_t)(0x0B20 + obW(B + 0x07))] != 1) break;        // 1E8E CMP #01 / BNE ->1EB5
      if (B[3] < B[0x09]) return 0;                                    // 1E95 BCC ->1EB3
      uint16_t dst = obW(B + 0x0E);                                    // 1F85
      for (int k = 0; k < 16; k++) {
        uint16_t c = (uint16_t)(ram[dst] | (ram[(uint16_t)(dst + 1)] << 8));
        uint8_t lo = (uint8_t)c, hi = (uint8_t)(c >> 8);
        lo = (uint8_t)(lo + obFadeStep(B + 0x10 + k, T));             // INC/DEC $08 (8-bit)
        c = (uint16_t)((hi << 8) | lo);
        c = (uint16_t)(c + 0x20 * obFadeStep(B + 0x20 + k, T));       // $08 +/- #0020 (16-bit)
        hi = (uint8_t)((c >> 8) + 4 * obFadeStep(B + 0x30 + k, T));   // $09 +/- #04 (8-bit)
        c = (uint16_t)((hi << 8) | (c & 0xFF));
        ram[dst] = (uint8_t)c; ram[(uint16_t)(dst + 1)] = (uint8_t)(c >> 8);
        dst = (uint16_t)(dst + 2);
      }
      B[3] = 0;                                                        // 1E9F STZ $0003,X (8-bit)
      ram[0x4C] |= 0x02;                                               // 1EA2
      if (++B[5] == 0x20) B[2]++;                                      // 1EA6 INC $0005,X / CMP #20 / BNE / INC $0002,X
      return 0;
    }
    default: break;
  }
  ram[0x0B20 + B[0x07]]--;                                             // 1EB5 LDA $0007,X / TAX / DEC $0B20,X
  return 1;
}

// ===================== THE EPOCH ON THE MAP: classes $309E, $42DD, $495E and the lead's modules 2/4/5 =====================
// disasm/notes/ow_vehicle.md. `$027E` is the lead's MODULE (C2/3404's table C2:342D): 0 on foot (the host's mover), 1
// nothing, 2 riding the Epoch (C2/346D: `$0283 = $0290`, no tick -- the party is inside), 3 riding vehicle B (C2/347B), 4
// boarding (C2/3489), 5 stepping out (C2/349B). The boarding manager $309E moves it; the Epoch $42DD flies.
static void owUnderway(const char* what) {
  static char seen[8][48]; static int n = 0;
  for (int i = 0; i < n; i++) if (!strncmp(seen[i], what, 47)) return;
  if (n < 8) { strncpy(seen[n], what, 47); seen[n][47] = 0; n++; }
  printf("owvehicle: UNDERIVED %s -- held\n", what);
}
// C2/3ACB, the lead's cells: `$0102 = $0283 >> 3`, `$0103 = $0285 >> 3`.
static void owLeadCells(uint8_t* ram) {
  ram[0x0102] = (uint8_t)(obW(ram + 0x0283) >> 3); ram[0x0103] = (uint8_t)(obW(ram + 0x0285) >> 3);
}
// C2/0568 / C2/066C on BG1 then BG2 (`$00` = 1, 2) by the low byte of a velocity word, and the word into $1BF1 / $1BF3.
static void owScrollBoth(uint8_t* ram, CtWorldCam* c, int vert, uint16_t vel) {
  obScroll(ram, c, 0, vert, (int8_t)(uint8_t)vel);
  obScroll(ram, c, 1, vert, (int8_t)(uint8_t)vel);
}
static void owStoreCamVel(uint8_t* ram, CtWorldCam* c, const uint8_t* B) {   // `LDA $001C,X / STA $1BF1 / LDA $0020,X / STA $1BF3`
  obPutW(ram + 0x1BF1, obW(B + OWOBJ_VELXI)); obPutW(ram + 0x1BF3, obW(B + OWOBJ_VELYI));
  if (c) { c->pendDx = (int16_t)obW(B + OWOBJ_VELXI); c->pendDy = (int16_t)obW(B + OWOBJ_VELYI); }
}
static void owVelZero(uint8_t* B) { obPutW(B + 0x1A, 0); obPutW(B + 0x1C, 0); obPutW(B + 0x1E, 0); obPutW(B + 0x20, 0); }
// C2/2345 -- two boxes overlap: `$08/$0A` and box `[$10]` against `$0C/$0E` and box `[$13]` (four words each: left,
// right, up, down extents). C2/2353 on X, C2/2379 on Y, each `SBC` 16 bits wide and its sign the verdict (`BMI` = hit).
static int owBoxHit(unsigned ax, unsigned ay, unsigned abox, unsigned bx, unsigned by, unsigned bbox) {
  unsigned d = (bx - ax) & 0xFFFF, r;
  if (d) {                                                             // 2359 SBC $08 / BEQ -> hit
    if (bx >= ax) r = d - owRomW(abox + 2) - owRomW(bbox);             // 235F SBC [$10],Y (2) / SBC [$13]
    else r = ((0x10000 - d) & 0xFFFF) - owRomW(bbox + 2) - owRomW(abox);   // 2369 EOR / INC / SBC [$13],Y / SBC [$10]
    if (!(r & 0x8000)) return 0;
  }
  d = (by - ay) & 0xFFFF;
  if (!d) return 1;                                                    // 237E BEQ -> SEC
  if (by >= ay) r = d - owRomW(abox + 6) - owRomW(bbox + 4);
  else r = ((0x10000 - d) & 0xFFFF) - owRomW(bbox + 6) - owRomW(abox + 4);
  return (r & 0x8000) != 0;
}
// THE BOARDING MANAGER, class $309E (block $0E30 on 1F6). Only in free roam or a cutscene ($027C 0 / 1) and with no entry
// pending ($0280 = 0); `$027F = $027E` first (the lead and the followers zero their phase when the two differ), then the
// module's arm (C2:30C1).
static int obBoardManager(uint8_t* ram) {
  if (ram[0x027C] != 0 && ram[0x027C] != 1) return 0;                  // 309E-30A9
  if (ram[0x0280]) return 0;                                           // 30AB-30B1
  ram[0x027F] = ram[0x027E];                                           // 30B2 STA $027F
  int party = 0;
  switch (ram[0x027E]) {
  case 0: {                                                            // C2/30CD: A, standing on a vehicle
    if (!(obW(ram + 0x00F8) & 0x0080)) return 0;                       // 30CF BIT #0080
    obPutW(ram + 0x0008, obW(ram + 0x0283)); obPutW(ram + 0x000A, obW(ram + 0x0285));
    int v = 0;
    if ((ram[0x0294] & 0x80) && (obW(ram + 0x0100) & 0x01FF) == obW(ram + 0x029F)   // 30EF BPL / 30F6 AND #01FF / CMP $029F
        && owBoxHit(obW(ram + 0x0283), obW(ram + 0x0285), 0x324C, obW(ram + 0x0290), obW(ram + 0x0292), 0x3254)) {
      if (ram[0x1BA6] == 0xD2) {                                       // 311F CMP #D2: the story's own boarding
        ram[0x027C] = 0x07;
        CtWorldSpawnVm(ram, 0xC2, 0x31A8);                             // 3154 JSR C2/04D9
        return 0;
      }
      v = 2;
    } else if ((ram[0x029E] & 0x80) && obW(ram + 0x0100) == 0x01F3    // 3159 BPL / 315E LDX $0100 / CPX #01F3
               && owBoxHit(obW(ram + 0x0283), obW(ram + 0x0285), 0x324C, obW(ram + 0x029A), obW(ram + 0x029C), 0x325C))
      v = 3;
    if (!v) return 0;
    ram[0x1B58] = 0; ram[0x1B59] = 0;                                  // 3130 / 3184
    obPutW(ram + 0x1B32, 0xFFFF); obPutW(ram + 0x1B34, 0xFFFF); obPutW(ram + 0x1B36, 0xFFFF);
    ram[0x1B44] = 0; ram[0x0282] = 0;
    ram[0x028F] = (uint8_t)v; ram[0x027E] = 0x04;                      // 3126-312D / 319C-31A3
    return 0;
  }
  case 2: if (!(ram[0x0294] & 0x40)) ram[0x027E] = 0x05; return 0;     // C2/31B4: the Epoch landed
  case 3: if (!(ram[0x029E] & 0x40)) ram[0x027E] = 0x05; return 0;     // C2/31C2
  case 4:                                                              // C2/31D0: everyone aboard
    for (int i = 0; i < 3; i++) if (!(ram[0x2980 + i] & 0x80)) party++;
    if (ram[0x028F] == 2 && (ram[0x0294] & 3) == party) ram[0x027E] = 0x02;
    if (ram[0x028F] == 3 && (ram[0x029E] & 3) == party) ram[0x027E] = 0x03;
    return 0;
  case 5:                                                              // C2/321C: everyone out
    if ((ram[0x028F] == 2 && !(ram[0x0294] & 3)) || (ram[0x028F] == 3 && !(ram[0x029E] & 3))) {
      ram[0x027E] = 0; ram[0x028F] = 0;
    }
    return 0;
  }
  return 0;                                                            // 1: 31B2 CLC
}
// THE LEAD'S MODULES 1-5 (C2/3404 past its module-0 arm, which the host's mover is). Called from the pass on the lead's
// block while `$027E != 0`; returns 0 (CLC).
static int obLeadModule(uint8_t* ram, uint8_t* B, CtWorldCam* c) {
  if (ram[0x2980] & 0x80) return 0;                                    // 3404 LDA $7E2980 / BPL
  if (ram[0x027E] != ram[0x027F]) B[2] = 0;                            // 340E-3416
  if (ram[0x0280] != ram[0x0281]) B[2] = 0;                            // 3419-3421
  int magus = (ram[0x2980] == 6) ? 0x0C : 0;
  switch (ram[0x027E]) {
  case 2: obPutW(ram + 0x0283, obW(ram + 0x0290)); obPutW(ram + 0x0285, obW(ram + 0x0292)); return 0;   // C2/346D
  case 3: obPutW(ram + 0x0283, obW(ram + 0x029A)); obPutW(ram + 0x0285, obW(ram + 0x029C)); return 0;   // C2/347B
  case 5: owVehicleLeave(ram, B, 1); return 0;                         // C2/349B
  case 4: break;
  case 0:                                                              // C2/3439: by `$0280`, C2:345D's rows
    if (ram[0x0280] == 1) return owEventMember(ram, B, c, 0);          // the event's (3772 / 37A1 / 37BB)
    if (ram[0x0280] == 0) {                                            // the walk's: the host's mover -- but the frame the
      if (B[2] == 0) {                                                 // event ends its phase is 0 (3419): C2/34F8 here
        B[2] = 1;                                                      // 34FC INC $0002,X
        obPutW(B + OWOBJ_XPOS, obW(ram + 0x0283)); obPutW(B + OWOBJ_YPOS, obW(ram + 0x0285));   // 3515-351E
        owVelZero(B);                                                  // 3521-352A
        owLeadCells(ram);                                              // 352D JSR C2/3ACB
        leadInit(ram, B);
      }
      return 0;
    }
    { static int said = 0; if (!said) { said = 1; fprintf(stderr, "owlead: UNDERIVED `$0280 = %02X` -- past C2:345D's two rows\n", ram[0x0280]); } }
    return 0;
  default: return 0;                                                   // 1: C2/3479 CLC
  }
  switch (B[2]) {                                                      // C2/3489, the table C2:3493
  case 0: {                                                            // C2/37BD: aim at the vehicle, walk the column first
    if (ram[0x028F] == 2) { obPutW(B + 0x2A, obW(ram + 0x0290)); obPutW(B + 0x2C, obW(ram + 0x0292)); }
    if (ram[0x028F] == 3) { obPutW(B + 0x2A, obW(ram + 0x029A)); obPutW(B + 0x2C, obW(ram + 0x029C)); }
    owVelZero(B);                                                      // 37F6-37FF
    unsigned d = (obW(B + 0x2C) - obW(B + OWOBJ_YPOS)) & 0xFFFF;       // 3802 SEC / SBC $0018,X
    if (d) {
      if (d & 0x8000) { obPutW(B + 0x20, 0xFFFF); B[0x28] = 0; }       // 380F-3819
      else            { obPutW(B + 0x20, 0x0001); B[0x28] = 1; }       // 381E-3828
    }
    B[2]++;                                                            // 382B INC $0002,X
    leadInstall(B, kLeadWalkId[(B[0x28] & 3) * 2 + (B[0x26] & 1)] + magus);   // 382E JSR C2/3953
  }  /* fall through: 3837 is the next instruction */
  /* FALLTHROUGH */
  case 1: {                                                            // C2/3837: the column
    obIntegrate(B);                                                    // 383B JSR C2/0F38 / JSR C2/1CB8
    obPutW(ram + 0x0283, obW(B + OWOBJ_XPOS)); obPutW(ram + 0x0285, obW(B + OWOBJ_YPOS));
    owLeadCells(ram);                                                  // 384F JSR C2/3ACB
    owScrollBoth(ram, c, 1, obW(B + 0x20));                            // 3852-3862 C2/066C x2
    owStoreCamVel(ram, c, B);                                          // 3865-3872
    if (obW(B + OWOBJ_YPOS) == obW(B + 0x2C)) {                        // 3877 CMP $002C,X
      owVelZero(B);
      unsigned d = (obW(B + 0x2A) - obW(B + OWOBJ_XPOS)) & 0xFFFF;     // 388B SEC / SBC $0014,X
      if (d) {
        if (d & 0x8000) { obPutW(B + 0x1C, 0xFFFF); B[0x28] = 2; }     // 3898-38A2
        else            { obPutW(B + 0x1C, 0x0001); B[0x28] = 3; }     // 38A7-38B1
      }
      B[2]++;                                                          // 38B4 INC $0002,X
      leadInstall(B, kLeadWalkId[(B[0x28] & 3) * 2 + (B[0x26] & 1)] + magus);   // 38BD JSR C2/3953
    }
    obAnimTick(ram, B);                                                // 38C0 JSR C2/0E1D
    return 0;
  }
  case 2:                                                              // C2/38C5: the row, then aboard
    obIntegrate(B);
    if (obW(B + OWOBJ_XPOS) == obW(B + 0x2A)) { B[2]++; owVehicleAboard(ram); }   // 38D2 CMP $002A,X / 38D9 / 38DC
    obPutW(ram + 0x0283, obW(B + OWOBJ_XPOS)); obPutW(ram + 0x0285, obW(B + OWOBJ_YPOS));
    owLeadCells(ram);                                                  // 38EF JSR C2/3ACB
    owScrollBoth(ram, c, 0, obW(B + 0x1C));                            // 38F2-3902 C2/0568 x2
    owStoreCamVel(ram, c, B);
    obAnimTick(ram, B);                                                // 3915 JSR C2/0E1D
    return 0;
  }
  return 0;                                                            // 3: C2/391A CLC (aboard: not drawn)
}
// C2/48A9: the Epoch's pose for its facing +28 -- C2:48C5[f] without wings, C2:48C9[f] with ($0294 bit 5).
static void owEpochPose(uint8_t* ram, uint8_t* B) {
  unsigned t = (ram[0x0294] & 0x20) ? 0x48C9 : 0x48C5;
  leadInstall(B, owRomW(t + (B[0x28] & 3)) & 0xFF);
}
// C2/4A2A: the Epoch's song (13 with wings or without), posted at priority 0; taken -> `$02AE`.
static void owEpochSong(uint8_t* ram) {
  ram[0x1BE8] = 0x10; ram[0x1BEA] = 0xFF; ram[0x1BEB] = 0x80; ram[0x1BED] = 0;
  ram[0x1BE9] = 0x13;                                                  // 4A43 / 4A47 LDA #13 (both arms)
  if (CtWorldSoundRequest(ram)) ram[0x02AE] = ram[0x1BE9];             // 4A4C JSR C2/2ED9 / BCS
}
// C2/2F4E: the region's song when the Epoch lands -- the cell under the screen's middle (`($E3 >> 1) + 8`, `(($E7 >> 1)
// + 7) & 3F`, 8-bit), or the field song held in $7E29AE; no priority written (the last one stands).
static void owRegionSong(uint8_t* ram) {
  if (ram[0x101ED]) ram[0x1BE9] = ram[0x29AE];
  else ram[0x1BE9] = ram[0x1B9B + CtWorldRegion(ram, (uint8_t)((ram[0xE3] >> 1) + 8), ((ram[0xE7] >> 1) + 7) & 0x3F)];
  ram[0x1BE8] = 0x10; ram[0x1BEA] = 0xFF; ram[0x1BEB] = 0x80;
  if (CtWorldSoundRequest(ram)) ram[0x02AE] = ram[0x1BE9];
}
// C2/39D5 then C2/4A58: the map properties around the cell of pixel ($08, $0A) -- +22 (x-1, y-1), +23 (x, y-1), +24
// (x-1, y), +25 (x, y), and below it +2E (x-1, y+1), +2F (x, y+1); a column of -1 wraps to BF, rows to 7F. The landing
// reads them. `$08` / `$0A` are left in cells ($0A one row down), as the two routines leave them.
static void owEpochProps(uint8_t* ram, uint8_t* B, unsigned px, unsigned py) {
  int x = (int)(px >> 3), y = (int)(py >> 3);
  int xm = x - 1 < 0 ? 0xBF : x - 1;
  B[0x22] = CtWorldProp(ram, xm, (y - 1) & 0x7F); B[0x23] = CtWorldProp(ram, x, (y - 1) & 0x7F);
  B[0x24] = CtWorldProp(ram, xm, y);             B[0x25] = CtWorldProp(ram, x, y);
  int yp = (y + 1) & 0x7F;                                             // 4A78 LDA $0A / INC A / AND #007F / STA $0A
  B[0x2E] = CtWorldProp(ram, xm, yp);            B[0x2F] = CtWorldProp(ram, x, yp);
  obPutW(ram + 0x0008, (uint16_t)x); obPutW(ram + 0x000A, (uint16_t)yp);
}
// C6/E797: may the Epoch land here? A forbidden rectangle of cells per world (C6:E7BA by `$0100 - 1F0`): inside it -> SEC.
static int owLandForbidden(const uint8_t* ram, const uint8_t* B) {
  static const uint8_t R[8][4] = {                                     // {y lo, y hi, x lo, x hi} (E7CA..E7E4's LDX / LDY)
    { 0x64, 0x7F, 0x41, 0x72 }, { 0x76, 0x7F, 0x60, 0x6E }, { 0 }, { 0x00, 0x34, 0x00, 0x0E }, { 0x60, 0x7F, 0x00, 0x20 } };
  int w = (int)(obW(ram + 0x0100) - 0x01F0);
  if (w < 0 || w > 7 || w == 2 || w > 4) return 0;                     // E7DA CLC
  unsigned x = (obW(B + OWOBJ_XPOS) >> 3) & 0xFF, y = (obW(B + OWOBJ_YPOS) >> 3) & 0xFF;   // C6/E797 LSR x3 (low bytes)
  int out = 0;                                                         // C6/E74E: $06, 0 = inside
  if (x < R[w][2]) out |= 1; else if (x >= R[w][3]) out |= 2;
  if (y < R[w][0]) out |= 4; else if (y >= R[w][1]) out |= 8;
  return out == 0;
}
// THE EPOCH, class $42DD (block $0DB0 on 1F6), states C2:42E6.
static int obEpoch(uint8_t* ram, uint8_t* B, CtWorldCam* c) {
  for (;;) switch (B[2]) {
  case 0:                                                              // C2/42F8: placed on its record
    obPutW(B + OWOBJ_XPOS, obW(ram + 0x0290)); obPutW(B + OWOBJ_YPOS, obW(ram + 0x0292));
    obPutW(B + OWOBJ_XFRAC, 0); obPutW(B + OWOBJ_YFRAC, 0); obPutW(B + 0x10, 0);
    if (!(ram[0x0294] & 0x40)) {                                       // 4316 BIT #40: parked
      B[2]++; B[OWOBJ_ATTR] = 0xA0; B[0x28] = B[0x27] = 0x01; ram[0x1BEF] = 0;
      leadInstall(B, (ram[0x0294] & 0x20) ? 0x18 : 0x2D);              // 432D BIT #20
      obAnimTick(ram, B);
      return 0;
    }
    if (ram[0x0294] & 0x10) {                                          // C2/4387: arrived through time -- script C2:4B8C
      ram[0x1BE8] = 0x86; ram[0x1BE9] = 0; ram[0x1BEA] = 0; ram[0x1BED] = 0;
      CtWorldSoundRequest(ram);                                        // 4395 JSR C2/2ED9
      B[2] = 7; B[OWOBJ_ATTR] = 0xB0;
      B[0x28] = B[0x27] = (uint8_t)(ram[0x0104] & 3);
      ram[0x0294] &= (uint8_t)~0x0C; ram[0x1BEF] = 1;
      obPutW(B + OWOBJ_PC, 0x4B8C); B[0x09] = 0xC2; B[OWOBJ_WAIT] = 0;
      return 0;
    }
    B[2] = 3; B[OWOBJ_ATTR] = 0xB0;                                    // 434C-4353: riding
    B[0x28] = B[0x27] = (uint8_t)(ram[0x0104] & 3); ram[0x1BEF] = 1;
    ram[0x0294] &= (uint8_t)~0x0C;                                     // 4366 TRB $0294
    owEpochPose(ram, B);
    owEpochProps(ram, B, obW(B + OWOBJ_XPOS), obW(B + OWOBJ_YPOS));    // 4372-437F
    obAnimTick(ram, B);
    return 0;
  case 1:                                                              // C2/43CB: parked, waiting for the party
    if (ram[0x027E] != 2) { obAnimTick(ram, B); return 0; }
    ram[0x0294] |= 0x40;                                               // 43D7 TSB $0294
    if (!(ram[0x0294] & 0x20)) {                                       // 43E3: no wings -- the time gauge at once
      ram[0x0294] |= 0x10; ram[0x027C] = 0x08; B[2] = 6; obPutW(ram + 0x02AF, 0);
      obAnimTick(ram, B);
      return 0;
    }
    B[2]++;                                                            // 4401 INC $0002,X
    owEpochSong(ram);                                                  // 4404 JSR C2/4A2A
    owEpochProps(ram, B, obW(B + OWOBJ_XPOS), obW(B + OWOBJ_YPOS));    // 4407-4416
    obPutW(B + 0x1E, 0x8000); obPutW(B + 0x20, 0xFFFF);                // 441B-4424: up, half a pixel a frame
    obPutW(B + 0x1A, 0); obPutW(B + 0x1C, 0); obPutW(B + 3, 0);
    B[0x26] = 0x30;                                                    // 4432
    leadInstall(B, 0x19);                                              // 4437 JSR C2/1CDA
    /* FALLTHROUGH: 443C */
  case 2:                                                              // C2/443C: the lift-off
    if (B[3] == 0x08) B[OWOBJ_ATTR] = 0xB0;
    if (B[3] < B[0x26]) { obIntegrate(B); obAnimTick(ram, B); return 0; }
    if (ram[0x1BA6] == 0xD3) {                                         // 4461: the story's first flight
      if (ram[0x027C] == 1) {
        ram[0x027C] = 0x07;
        CtWorldSpawnVm(ram, 0xC2, 0x4A9B);                             // 4479 JSR C2/04D9
        obAnimTick(ram, B);
      }
      return 0;
    }
    B[2]++;                                                            // 4481 INC $0002,X
    owEpochPose(ram, B);                                               // 4484 JSR C2/48A9
    owEpochSong(ram);                                                  // 4487 JSR C2/4A2A
    ram[0x1BEF] = 1;
    obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(B + OWOBJ_YPOS) + 0x0018));  // 4491 ADC $0018,X
    owVelZero(B); obPutW(B + 3, 0);
    ram[0x0104] = (uint8_t)((ram[0x0104] & 0xFC) | B[0x28]);           // 44AC-44B4
    /* FALLTHROUGH: 44B7 */
  case 3: {                                                            // C2/44B7: in the air, waiting for the pad
    if (ram[0x027C] != 1) { obAnimTick(ram, B); return 0; }
    unsigned pad = obW(ram + 0x00F8);
    if (!pad) { obAnimTick(ram, B); return 0; }
    if (pad & 0x0080) {                                                // C2/4510: A
      if (ram[0x0294] & 0x08) {
        if (!(ram[0x0294] & 0x04)) {
          ram[0x027C] = 0x07; ram[0x0294] |= 0x04;
          CtWorldSpawnVm(ram, 0xC2, 0x4C8F);                           // 452C JSR C2/04D9
        }
        obAnimTick(ram, B);
        return 0;
      }
      int refuse = ((B[0x22] | B[0x23] | B[0x24] | B[0x25] | B[0x2E] | B[0x2F]) & 0x07) != 0;   // 4533-453C AND #0707
      if (!refuse && (ram[0x029E] & 0x80) && obW(ram + 0x0100) == 0x01F3   // C2/4917: not onto vehicle B
          && owBoxHit(obW(ram + 0x0290), obW(ram + 0x0292), 0x4946, obW(ram + 0x029A), obW(ram + 0x029C), 0x494E)) refuse = 1;
      if (!refuse && owLandForbidden(ram, B)) refuse = 1;              // 455E JSL C6/E797
      if (!refuse) {                                                   // C2/4569: down it goes
        obPutW(B + 0x1A, 0); obPutW(B + 0x1C, 0); obPutW(B + 0x1E, 0x8000); obPutW(B + 0x20, 0); obPutW(B + 3, 0);
        obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(B + OWOBJ_YPOS) + 0xFFE8));   // 457D ADC $0018,X
        B[0x28] = 0x01;
        ram[0x0104] = (uint8_t)((ram[0x0104] & 0xFC) | B[0x28]);
        B[0x26] = 0x31;
        leadInstall(B, 0x1A);                                          // 45A0 JSR C2/1CDA
        ram[0x1BEF] = 0;
        owRegionSong(ram);                                             // 45A6 JSR C2/2F4E
        B[2] = 5;
        obAnimTick(ram, B);
        return 0;
      }
    }
    int dir = -1;                                                      // C2/44CC
    if (pad & 0x0001) {                                                // 45C7: the time gauge
      ram[0x0294] |= 0x10; ram[0x027C] = 0x08; B[2] = 6; obPutW(ram + 0x02AF, 0);
      obAnimTick(ram, B);
      return 0;
    }
    if (pad & 0x0040) { ram[0x027C] = 0x05; obAnimTick(ram, B); return 0; }   // 45BD
    if (pad & 0x0004) { ram[0x027C] = 0x06; owUnderway("the Epoch's $027C = 6 (C2/45B3)"); obAnimTick(ram, B); return 0; }   // 45B3
    if (pad & 0x0800) dir = 0; else if (pad & 0x0400) dir = 1;
    else if (pad & 0x0200) dir = 2; else if (pad & 0x0100) dir = 3;
    if (dir < 0) { obAnimTick(ram, B); return 0; }
    B[0x27] = B[0x28]; B[0x28] = (uint8_t)dir;                         // C2/461D
    obPutW(B + 0x1C, (uint16_t)owRomW(0x4899 + dir * 4));              // 4628 LDA $C24899,X
    obPutW(B + 0x20, (uint16_t)owRomW(0x489B + dir * 4));
    obPutW(B + 0x1A, 0); obPutW(B + 0x1E, 0);
    if (B[0x28] != B[0x27]) {
      ram[0x0104] = (uint8_t)((ram[0x0104] & 0xFC) | B[0x28]);
      owEpochPose(ram, B);                                             // 4652 JSR C2/48A9
    }
    {                                                                  // 4659 JSR C2/3A56 / JSR C2/3A8F: the next cell
      unsigned tx = obW(B + OWOBJ_XPOS), ty = obW(B + OWOBJ_YPOS);
      int16_t vx = (int16_t)obW(B + 0x1C), vy = (int16_t)obW(B + 0x20);
      if (vx) tx += vx > 0 ? 8 : 0xFFF8;
      if (vy) ty += vy > 0 ? 8 : 0xFFF8;
      tx &= 0xFFFF; ty &= 0xFFFF;
      if (tx & 0x8000) tx = (tx + 0x600) & 0xFFFF; else if (tx >= 0x600) tx -= 0x600;
      obPutW(B + 0x2A, (uint16_t)tx); obPutW(B + 0x2C, (uint16_t)(ty & 0x3FF));
    }
    owEpochProps(ram, B, obW(B + 0x2A), obW(B + 0x2C));                // 465F-466C
    obPutW(ram + 0x1BF7, 0);                                           // 466F STZ $1BF7 (M = 16)
    B[2]++;                                                            // 4672 INC $0002,X
  }
    /* FALLTHROUGH: 4675 */
  case 4:                                                              // C2/4675: the step
    obIntegrate(B);
    owScrollBoth(ram, c, 0, obW(B + 0x1C));                            // 4681-4691 C2/0568 x2
    owScrollBoth(ram, c, 1, obW(B + 0x20));                            // 4694-46A6 C2/066C x2
    owStoreCamVel(ram, c, B);
    obAnimTick(ram, B);
    obPutW(ram + 0x0290, obW(B + OWOBJ_XPOS)); obPutW(ram + 0x0292, obW(B + OWOBJ_YPOS));
    if (obW(B + OWOBJ_XPOS) == obW(B + 0x2A) && obW(B + OWOBJ_YPOS) == obW(B + 0x2C)) obPutW(B + 2, 0x0003);   // 46DA
    return 0;
  case 5:                                                              // C2/46E2: the landing
    if (B[3] == 0x20) B[OWOBJ_ATTR] = 0xA0;
    if (B[3] < B[0x26]) { obIntegrate(B); obAnimTick(ram, B); return 0; }
    ram[0x0294] &= (uint8_t)~0x40;                                     // 4705 TRB $0294
    B[2] = 0;
    leadInstall(B, 0x18);
    owVelZero(B); obPutW(B + 3, 0);
    obAnimTick(ram, B);
    return 0;
  case 8:                                                              // C2/4888
    if (ram[0x027E] == 5) B[2] = 0;
    obAnimTick(ram, B);
    return 0;
  case 6: {                                                            // C2/4728: waiting on the time gauge's answer
    unsigned y = obW(ram + 0x02AF);
    if (y & 0x8000) {                                                  // 4734: B -- no trip
      ram[0x0294] &= (uint8_t)~0x10;
      if (ram[0x0294] & 0x20) B[2] = 3;                                // 4763: aloft again
      else {
        ram[0x0294] &= (uint8_t)~0x40; leadInstall(B, 0x2D); B[2] = 8;   // 4740: down on its spot
        owVelZero(B); obPutW(B + 3, 0);
      }
    } else if (y) {                                                    // 476B: the trip, script C2:4AAD
      B[2] = 7; B[OWOBJ_WAIT] = 0; B[0x09] = 0xC2; obPutW(B + OWOBJ_PC, 0x4AAD);
      if (y == 0x01D9) {
        obPutW(ram + 0x0102, 0x0807); ram[0x0104] = 0x01; ram[0x100CD] = 0;
      } else if (y == 0x01F7) {                                        // 1999 A.D.: the Day of Lavos, or (wings) the cockpit
        int w = (ram[0x0294] & 0x20) != 0;
        obPutW(ram + 0x02AF, w ? 0x01B0 : 0x01D8); obPutW(ram + 0x0102, w ? 0x1807 : 0x0807);
        ram[0x0104] = 0x01; ram[0x100CD] = 0;
      } else if (y == 0x01F2 && !(ram[0x0294] & 0x20)) {
        obPutW(ram + 0x02AF, 0x00F3); obPutW(ram + 0x0102, 0x0C08);
        ram[0x0104] = 0x01; ram[0x100CD] = 0; ram[0x100CC] |= 0x01;
      } else {                                                         // 47F3: from where it is (16-bit stores, overlapping)
        obPutW(ram + 0x0102, (uint16_t)(obW(B + OWOBJ_XPOS) >> 3));
        obPutW(ram + 0x0103, (uint16_t)(obW(B + OWOBJ_YPOS) >> 3));
        ram[0x0104] = B[0x28];
      }
    }
    obPutW(ram + 0x0290, obW(B + OWOBJ_XPOS)); obPutW(ram + 0x0292, obW(B + OWOBJ_YPOS));   // C2/480F
    ram[0x1BF7] = 0;
    obAnimTick(ram, B);
    return 0;
  }
  case 7: {                                                            // C2/4823: the trip's script, run inline (C2/0F63)
    uint16_t cls = obW(B);
    obRun(ram, B, c);
    if (!obW(B)) {                                                     // the script's end (op 52: SEC) -- not this block's
      obPutW(B, cls);
      ram[0x0294] &= (uint8_t)~0x10;
      if (!(ram[0x0294] & 0x20)) {
        ram[0x0294] &= (uint8_t)~0x40; leadInstall(B, 0x2D); B[2] = 0;
        owVelZero(B); obPutW(B + 3, 0);
        obAnimTick(ram, B);
      } else {
        B[2] = 3;
        owEpochProps(ram, B, obW(B + OWOBJ_XPOS), obW(B + OWOBJ_YPOS));
        obAnimTick(ram, B);
      }
    }
    obPutW(ram + 0x0290, obW(B + OWOBJ_XPOS)); obPutW(ram + 0x0292, obW(B + OWOBJ_YPOS));   // C2/4873
    ram[0x1BF7] = 0;
    return 0;
  }
  }
}
// THE EPOCH'S SHADOW, class $495E: only on the Epoch's world (else SEC, gone); every frame its pose afresh -- 4E above the
// craft, or with wings and aloft ($1BEF) 1F below it.
static int obEpochShadow(uint8_t* ram, uint8_t* B) {
  if (B[2] == 0) {                                                     // C2/496B
    if ((obW(ram + 0x0100) & 0x01FF) != obW(ram + 0x029F)) return 1;   // 4976 BEQ / SEC
    B[2]++; B[OWOBJ_ATTR] = 0xB0; B[0x10] = 0; B[0x11] = 0;
  }
  int up = !((ram[0x0294] & 0x20) && ram[0x1BEF]);                     // C2/498C
  leadInstall(B, up ? 0x4E : 0x1F);
  obPutW(B + OWOBJ_XPOS, obW(ram + 0x0290));
  obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(ram + 0x0292) + (up ? 0xFFE8 : 0x0018)));
  obAnimTick(ram, B);
  return 0;
}

// ===================== VEHICLE B, THE DACTYLS (loc 1F3, 65,000,000 B.C.) =====================
// disasm/notes/ow_vehicle.md sec. 7. Record `$029A/$029C` (position), `$029E` (bits 0-1 aboard, 5 landing, 6 riding, 7 present);
// `$1BF0` aloft. The lead's module 3 (C2/347B) rides it; the boarding manager's vehicle-B arm (box C2:325C) boards it. Classes:
// $4CC5 the lead's Dactyl, $504B its shadow, $5097 x 2 the followers' Dactyls (+24 = the slot), $5494 x 2 their shadows.
static uint8_t owRomC0(unsigned a) { return (g_owRom && a < (unsigned)g_owRomLen) ? g_owRom[a] : 0; }
static unsigned owTrig2277(unsigned a) {                               // C2/2277 (sin); C2/2273 = it of a + 40 (cos)
  a &= 0xFF;
  if (a == 0x40) return 0x0080;
  if (a == 0xC0) return 0xFF80;
  uint8_t v = owRomC0(0xF800 + a);                                     // C0:F800, DB = 00
  return (v & 0x80) ? (0xFF00u | v) : v;
}
// C2/229D: the heading from ($08, $0A) to ($0C, $0E) on the wrapping map, 0..FF (C0:F300's octant table); X = the table index
// (0 when the two coincide).
static unsigned owHeading(uint8_t* ram, unsigned* xOut) {
  unsigned d0 = (obW(ram + 0x0A) - obW(ram + 0x0E)) & 0xFFFF;          // 229D: dy
  unsigned a2 = (d0 & 0x8000) ? (0x10000 - d0) & 0xFFFF : d0;
  if (a2 >= 0x0200) { a2 = (0x10000 - ((a2 - 0x0400) & 0xFFFF)) & 0xFFFF; d0 = (0x10000 - d0) & 0xFFFF; }
  unsigned d4 = (obW(ram + 0x08) - obW(ram + 0x0C)) & 0xFFFF;          // 22C3: dx
  unsigned a6 = (d4 & 0x8000) ? (0x10000 - d4) & 0xFFFF : d4;
  if (a6 >= 0x0300) { a6 = (0x10000 - ((a6 - 0x0600) & 0xFFFF)) & 0xFFFF; d4 = (0x10000 - d4) & 0xFFFF; }
  obPutW(ram + 0x00, (uint16_t)d0); obPutW(ram + 0x02, (uint16_t)a2); obPutW(ram + 0x04, (uint16_t)d4);
  a6 >>= 2;
  unsigned x = (((a2 & 0xFFFC) << 3) + a6) & 0xFFFF;
  unsigned t = owRomC0(0xF300 + x);
  obPutW(ram + 0x06, (uint16_t)t);
  *xOut = x;
  if (!((d0 ^ d4) & 0x8000)) return (d4 & 0x8000) ? t : 0x80 + t;
  return (d4 & 0x8000) ? (0x100 - t) & 0xFFFF : (0x80 - t) & 0xFFFF;
}
static void owDacSaveSlot(uint8_t* ram, const uint8_t* B) {            // C2/5605: where a follower's Dactyl is
  unsigned a = (B[0x24] & 1) ? 0x02A6 : 0x02AA;
  obPutW(ram + a, obW(B + OWOBJ_XPOS)); obPutW(ram + a + 2, obW(B + OWOBJ_YPOS));
}
static void owDacPose(uint8_t* ram, uint8_t* B) {                      // C2/5470: a follower's pose for its facing
  if (B[0x28] == B[0x27]) return;
  leadInstall(B, owRomW(0x548C + (unsigned)((B[0x24] - 1) * 4 + B[0x28])) & 0xFF);
}
// C2/53D3: the velocity that covers the way to +2A/+2C in 0x30 frames (|d| byte-swapped through $4204 / $4206).
static void owDacToward(uint8_t* ram, uint8_t* B) {
  unsigned dx = (obW(B + 0x2A) - obW(B + OWOBJ_XPOS)) & 0xFFFF; obPutW(ram + 0x08, (uint16_t)dx);
  unsigned ax = (dx & 0x8000) ? (0x10000 - dx) & 0xFFFF : dx;
  ax = ((ax & 0xFF) << 8) | (ax >> 8);                                 // 53E6 XBA
  owVelZero(B);
  obPutW(B + 0x1B, (uint16_t)(ax / 0x30));
  unsigned dy = (obW(B + 0x2C) - obW(B + OWOBJ_YPOS)) & 0xFFFF; obPutW(ram + 0x0A, (uint16_t)dy);
  unsigned ay = (dy & 0x8000) ? (0x10000 - dy) & 0xFFFF : dy;
  ay = ((ay & 0xFF) << 8) | (ay >> 8);
  if (dx & 0x8000) obNeg32(B + 0x1A, B + 0x1C);                        // 5429 JSR C2/1C84
  obPutW(B + 0x1F, (uint16_t)(ay / 0x30));
  if (dy & 0x8000) obNeg32(B + 0x1E, B + 0x20);                        // 5436 JSR C2/1C9E
}
// C2/5374: the heading to +2A/+2C and four times its cos / sin as the velocity (8.8, sign-extended); 0 when there.
static int owDacSteer(uint8_t* ram, uint8_t* B) {
  owVelZero(B);
  obPutW(ram + 0x08, obW(B + OWOBJ_XPOS)); obPutW(ram + 0x0A, obW(B + OWOBJ_YPOS));
  obPutW(ram + 0x0C, obW(B + 0x2A)); obPutW(ram + 0x0E, obW(B + 0x2C));
  unsigned x, h = owHeading(ram, &x);
  obPutW(ram + 0x08, (uint16_t)h);
  if (!x) return 0;
  obPutW(B + 0x1B, (uint16_t)(owTrig2277(h + 0x40) << 2));
  obPutW(B + 0x1F, (uint16_t)(owTrig2277(h) << 2));
  if (B[0x1C] & 0x80) B[0x1D] = 0xFF;
  if (B[0x20] & 0x80) B[0x21] = 0xFF;
  return 1;
}
// C2/528A: no step past the target -- each axis's whole part capped at the distance left (the map's wrap folded).
static void owDacCap(uint8_t* ram, uint8_t* B) {
  for (int ax = 0; ax < 2; ax++) {
    uint8_t* fr = B + (ax ? 0x1E : 0x1A); uint8_t* in = B + (ax ? 0x20 : 0x1C);
    int neg = (obW(in) & 0x8000) != 0;
    unsigned whole = obW(in);
    if (neg) { unsigned f = (~obW(fr) & 0xFFFF) + 1; whole = ((~obW(in) & 0xFFFF) + (f >> 16)) & 0xFFFF; }
    obPutW(ram + 0x08, (uint16_t)whole); obPutW(ram + 0x0A, (uint16_t)neg);
    unsigned d = (obW(B + (ax ? 0x2C : 0x2A)) - obW(B + (ax ? OWOBJ_YPOS : OWOBJ_XPOS))) & 0xFFFF;
    if (d & 0x8000) d = (0x10000 - d) & 0xFFFF;
    if (d >= (ax ? 0x0200u : 0x0300u)) d = (0x10000 - ((d - 0x0400) & 0xFFFF)) & 0xFFFF;
    if (d < whole) {
      obPutW(in, (uint16_t)d); obPutW(fr, 0);
      if (neg) obNeg32(fr, in);
    }
  }
}
// C2/39D5 alone (the Dactyl does not take C2/4A58's row below): the four properties about the cell of ($08, $0A).
static void owDactylProps(uint8_t* ram, uint8_t* B, unsigned px, unsigned py) {
  int x = (int)(px >> 3), y = (int)(py >> 3), xm = x - 1 < 0 ? 0xBF : x - 1;
  B[0x22] = CtWorldProp(ram, xm, (y - 1) & 0x7F); B[0x23] = CtWorldProp(ram, x, (y - 1) & 0x7F);
  B[0x24] = CtWorldProp(ram, xm, y);             B[0x25] = CtWorldProp(ram, x, y);
  obPutW(ram + 0x0008, (uint16_t)x); obPutW(ram + 0x000A, (uint16_t)y);
}
// THE LEAD'S DACTYL, class $4CC5 (states C2:4CCE).
static int obDactyl(uint8_t* ram, uint8_t* B, CtWorldCam* c) {
  for (;;) switch (B[2]) {
  case 0:                                                              // C2/4CDA
    obPutW(B + OWOBJ_XPOS, obW(ram + 0x029A)); obPutW(B + OWOBJ_YPOS, obW(ram + 0x029C));
    obPutW(B + OWOBJ_XFRAC, 0); obPutW(B + OWOBJ_YFRAC, 0); obPutW(B + 0x10, 0);
    B[0x28] = B[0x27] = 0x01;
    if (!(ram[0x029E] & 0x40)) { B[2]++; B[OWOBJ_ATTR] = 0xA8; ram[0x1BF0] = 0; leadInstall(B, 0x24); }
    else { B[2] = 3; B[OWOBJ_ATTR] = 0xB8; ram[0x1BF0] = 1; leadInstall(B, owRomW(0x5047 + 1) & 0xFF); }
    obAnimTick(ram, B);
    return 0;
  case 1:                                                              // C2/4D37: parked
    if (ram[0x027E] != 3) { obAnimTick(ram, B); return 0; }
    ram[0x029E] |= 0x40;
    B[2]++;
    owDactylProps(ram, B, obW(B + OWOBJ_XPOS), obW(B + OWOBJ_YPOS));   // 4D59 JSR C2/39D5
    obPutW(B + 0x1E, 0x8000); obPutW(B + 0x20, 0xFFFF);
    obPutW(B + 0x1A, 0); obPutW(B + 0x1C, 0); obPutW(B + 3, 0);
    leadInstall(B, 0x29);
    /* FALLTHROUGH: 4D79 */
  case 2:                                                              // C2/4D79: the lift-off
    if (B[3] == 0x08) B[OWOBJ_ATTR] = 0xB8;
    if (B[3] < 0x30) { obIntegrate(B); obAnimTick(ram, B); return 0; }
    B[2]++; ram[0x1BF0] = 1;
    obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(B + OWOBJ_YPOS) + 0x0018));
    owVelZero(B); obPutW(B + 3, 0);
    leadInstall(B, 0x25);
    ram[0x0104] = (uint8_t)((ram[0x0104] & 0xFC) | B[0x28]);
    /* FALLTHROUGH: 4DCF */
  case 3: {                                                            // C2/4DCF: aloft, the pad
    if (ram[0x027C] != 1) { obAnimTick(ram, B); return 0; }
    unsigned pad = obW(ram + 0x00F8);
    if (!pad) { obAnimTick(ram, B); return 0; }
    if ((pad & 0x0080)                                                 // C2/4E20: A -- down, if it may
        && !((obW(B + 0x22) | obW(B + 0x24) | obW(B + 0x2E)) & 0x0707)
        && (obW(B + 0x22) & 0x0404) != 0x0404                          // C2/39AE: not an entrance
        && !((ram[0x0294] & 0x80) && obW(ram + 0x0100) == obW(ram + 0x029F)   // C2/48E8: not onto the Epoch
             && owBoxHit(obW(ram + 0x029A), obW(ram + 0x029C), 0x494E, obW(ram + 0x0290), obW(ram + 0x0292), 0x4946))) {
      unsigned x = (obW(B + OWOBJ_XPOS) >> 3) & 0xFF, y = (obW(B + OWOBJ_YPOS) >> 3) & 0xFF;   // C6/E74E: x 00-0D, y 00-33
      int inside = x < 0x0E && y < 0x34;
      if (!inside) {
        obPutW(B + 0x1A, 0); obPutW(B + 0x1C, 0); obPutW(B + 0x1E, 0x8000); obPutW(B + 0x20, 0); obPutW(B + 3, 0);
        obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(B + OWOBJ_YPOS) + 0xFFE8));
        leadInstall(B, 0x29);
        B[0x28] = 0x01; ram[0x0104] = (uint8_t)((ram[0x0104] & 0xFC) | 1);
        ram[0x1BF0] = 0; ram[0x029E] |= 0x20;
        B[2] = 5;
        obAnimTick(ram, B);
        return 0;
      }
    }
    if (pad & 0x0040) { ram[0x027C] = 0x05; obAnimTick(ram, B); return 0; }   // C2/4EBE
    if (pad & 0x0004) { ram[0x027C] = 0x06; obAnimTick(ram, B); return 0; }   // C2/4EC8
    int dir = (pad & 0x0800) ? 0 : (pad & 0x0400) ? 1 : (pad & 0x0200) ? 2 : (pad & 0x0100) ? 3 : -1;
    if (dir < 0) { obAnimTick(ram, B); return 0; }
    obPutW(ram + 0x08, (uint16_t)dir);                                 // C2/4EE2
    obPutW(B + 0x1C, (uint16_t)owRomW(0x5037 + 4 * (unsigned)dir)); obPutW(B + 0x20, (uint16_t)owRomW(0x5039 + 4 * (unsigned)dir));
    obPutW(B + 0x1A, 0); obPutW(B + 0x1E, 0);
    if (B[0x28] != dir) {
      B[0x28] = (uint8_t)dir; ram[0x0104] = (uint8_t)((ram[0x0104] & 0xFC) | dir);
      leadInstall(B, owRomW(0x5047 + (unsigned)dir) & 0xFF);
    }
    {                                                                  // 4F21 JSR C2/3A56 / C2/3A8F: the next cell
      unsigned tx = obW(B + OWOBJ_XPOS), ty = obW(B + OWOBJ_YPOS);
      int16_t vx = (int16_t)obW(B + 0x1C), vy = (int16_t)obW(B + 0x20);
      if (vx) tx += vx > 0 ? 8 : 0xFFF8;
      if (vy) ty += vy > 0 ? 8 : 0xFFF8;
      tx &= 0xFFFF;
      if (tx & 0x8000) tx = (tx + 0x600) & 0xFFFF; else if (tx >= 0x600) tx -= 0x600;
      obPutW(B + 0x2A, (uint16_t)tx); obPutW(B + 0x2C, (uint16_t)(ty & 0x3FF));
    }
    owDactylProps(ram, B, obW(B + 0x2A), obW(B + 0x2C));               // 4F31 JSR C2/39D5
    if ((B[0x22] & 3) == 3 || (B[0x23] & 3) == 3 || (B[0x24] & 3) == 3 || (B[0x25] & 3) == 3) {   // 4F34: a cliff
      owVelZero(B);
      owDactylProps(ram, B, obW(B + OWOBJ_XPOS), obW(B + OWOBJ_YPOS));
      obAnimTick(ram, B);
      return 0;
    }
    ram[0x1BF7] = 0;
    B[2]++;
  }
    /* FALLTHROUGH: 4F7E */
  case 4:                                                              // C2/4F7E: the step
    obIntegrate(B);
    owScrollBoth(ram, c, 0, obW(B + 0x1C));
    owScrollBoth(ram, c, 1, obW(B + 0x20));
    owStoreCamVel(ram, c, B);
    obAnimTick(ram, B);
    obPutW(ram + 0x029A, obW(B + OWOBJ_XPOS)); obPutW(ram + 0x029C, obW(B + OWOBJ_YPOS));
    if (obW(B + OWOBJ_XPOS) == obW(B + 0x2A) && obW(B + OWOBJ_YPOS) == obW(B + 0x2C)) obPutW(B + 2, 0x0003);
    return 0;
  case 5:                                                              // C2/4FEB: the landing
    if (B[3] == 0x20) B[OWOBJ_ATTR] = 0xA8;
    if (B[3] < 0x31) { obIntegrate(B); obAnimTick(ram, B); return 0; }
    ram[0x029E] &= (uint8_t)~0x60;                                     // 500D TRB #40 / 5012 TRB #20
    B[2] = 1;
    leadInstall(B, 0x24);
    owVelZero(B); obPutW(B + 3, 0);
    obAnimTick(ram, B);
    return 0;
  default:
    return 0;
  }
}
// THE DACTYL'S SHADOW, class $504B: under it on the ground (4E, 18 above), or (aloft, `$1BF0`) 1F, 18 below.
static int obDactylShadow(uint8_t* ram, uint8_t* B) {
  if (B[2] == 0) { B[2]++; B[OWOBJ_ATTR] = 0xB0; B[0x10] = 0; B[0x11] = 0; }   // C2/5058
  unsigned k = (unsigned)ram[0x1BF0] * 4;                              // C2/5068: C2:508F[$1BF0]
  obPutW(B + OWOBJ_YPOS, (uint16_t)(owRomW(0x508F + k) + obW(ram + 0x029C)));
  obPutW(B + OWOBJ_XPOS, obW(ram + 0x029A));
  leadInstall(B, owRomW(0x5091 + k) & 0xFF);
  obAnimTick(ram, B);
  return 0;
}
// A FOLLOWER'S DACTYL, class $5097 (+24 = the slot, 1 or 2; states C2:50A0).
static int obDactylMate(uint8_t* ram, uint8_t* B) {
  int slot1 = (B[0x24] == 1);
  for (;;) switch (B[2]) {
  case 0:                                                              // C2/50AC
    obPutW(B + OWOBJ_XPOS, obW(ram + 0x029A)); obPutW(B + OWOBJ_YPOS, obW(ram + 0x029C));
    obPutW(B + OWOBJ_XFRAC, 0); obPutW(B + OWOBJ_YFRAC, 0); obPutW(B + 0x10, 0);
    B[0x28] = B[0x27] = 0x01;
    B[OWOBJ_ATTR] = slot1 ? 0xAA : 0xAC;
    if (!(ram[0x029E] & 0x40)) { B[2]++; leadInstall(B, 0x24); return 0; }   // (no tick: under the lead's)
    B[2] = 3; B[OWOBJ_ATTR] |= 0x30; B[0x27] = 0xFF;
    owDacPose(ram, B); owDacSaveSlot(ram, B);
    obAnimTick(ram, B);
    return 0;
  case 1:                                                              // C2/5110
    if (ram[0x027E] != 3) return 0;
    B[2]++; obPutW(B + 3, 0);
    obPutW(B + 0x1C, slot1 ? 0x0000 : 0xFFFF); obPutW(B + 0x1A, 0x8000);   // up and out, half a pixel sideways
    obPutW(B + 0x20, 0xFFFF); obPutW(B + 0x1E, 0);
    leadInstall(B, slot1 ? 0x6F : 0x74);
    /* FALLTHROUGH: 514C */
  case 2:                                                              // C2/514C: the lift-off
    if (B[3] == 0x08) B[OWOBJ_ATTR] |= 0x30;
    if (B[3] < 0x30) { obIntegrate(B); owDacSaveSlot(ram, B); obAnimTick(ram, B); return 0; }
    B[2]++; B[OWOBJ_ATTR] |= 0x30;
    obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(B + OWOBJ_YPOS) + 0x0018));
    owVelZero(B); obPutW(B + 3, 0);
    B[0x27] = 0xFF; owDacPose(ram, B);
    /* FALLTHROUGH: 51A5 */
  case 3:                                                              // C2/51A5: the formation
    if (ram[0x029E] & 0x20) {                                          // the lead's Dactyl is landing: in under it
      B[2] = 5; B[3] = 0; B[0x22] = 0;
      obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(B + OWOBJ_YPOS) + 0xFFE8));
      obPutW(B + 0x2A, obW(ram + 0x029A)); obPutW(B + 0x2C, obW(ram + 0x029C));   // C2/5361
      owDacToward(ram, B);                                             // C2/53D3
      B[0x27] = ram[0x0028];                                           // 51CD LDA $0028 (absolute: the scroll shadow)
      B[0x28] = 0x01;
      leadInstall(B, slot1 ? 0x6F : 0x74);
      obAnimTick(ram, B);
      return 0;
    }
    {                                                                  // C2/531D: its place, from the lead's facing
      unsigned i = ((((unsigned)(ram[0x0104] & 3) << 1) | (unsigned)((B[0x24] - 1) & 1)) << 2);
      unsigned x = (owRomW(0x543C + i) + obW(ram + 0x029A)) & 0xFFF8;
      if (x & 0x8000) x = (x + 0x0600) & 0xFFFF; else if (x >= 0x0600) x -= 0x0600;
      obPutW(B + 0x2A, (uint16_t)x);
      obPutW(B + 0x2C, (uint16_t)((owRomW(0x543E + i) + obW(ram + 0x029C)) & 0x03F8));
    }
    if (owDacSteer(ram, B)) {                                          // C2/5374, then C2/545C: the facing of the heading
      ram[0x09] = 0;
      B[0x27] = B[0x28]; B[0x28] = owRomC0(0xF700 + ram[0x08]);
    } else {
      owVelZero(B);
      B[0x27] = B[0x28]; B[0x28] = (uint8_t)(ram[0x0104] & 3);
    }
    owDacPose(ram, B);
    B[2]++; B[3] = 0;
    /* FALLTHROUGH: 5227 */
  case 4:                                                              // C2/5227: four frames on that heading
    if (B[3] >= 4) { B[2]--; continue; }                               // 5243 DEC / JMP C2/51A5
    owDacCap(ram, B);
    obIntegrate(B); owDacSaveSlot(ram, B); obAnimTick(ram, B);
    return 0;
  case 5:                                                              // C2/5249: the landing
    if (B[3] < 0x31) { obIntegrate(B); owDacSaveSlot(ram, B); obAnimTick(ram, B); return 0; }
    B[2] = 1; B[OWOBJ_ATTR] = (uint8_t)((B[OWOBJ_ATTR] & 0xCF) | 0x20);
    obPutW(B + OWOBJ_XFRAC, 0); obPutW(B + OWOBJ_YFRAC, 0); owVelZero(B);
    obAnimTick(ram, B);
    return 0;
  default:
    return 0;
  }
}
// A FOLLOWER DACTYL'S SHADOW, class $5494 (+24 = the slot; states C2:549D).
static int obDactylMateShadow(uint8_t* ram, uint8_t* B) {
  int slot1 = (B[0x24] == 1);
  for (;;) switch (B[2]) {
  case 0:                                                              // C2/54A7
    B[OWOBJ_ATTR] = 0xB0;
    if (ram[0x029E] & 0x40) {
      B[2] = 3; leadInstall(B, 0x1F);
      obPutW(B + OWOBJ_XPOS, obW(ram + (slot1 ? 0x02A6 : 0x02AA)));
      obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(ram + (slot1 ? 0x02A8 : 0x02AC)) + 0x0030));
      obPutW(B + 0x10, 0);
      return 0;
    }
    B[2]++;
    obPutW(B + OWOBJ_XPOS, obW(ram + 0x029A)); obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(ram + 0x029C) + 0xFFE8));
    obPutW(B + 0x10, 0);
    leadInstall(B, 0x4E);
    /* FALLTHROUGH: 550E */
  case 1:                                                              // C2/550E
    if (ram[0x027E] != 3) return 0;
    B[2]++; B[3] = 0;
    obPutW(B + 0x1C, slot1 ? 0x0000 : 0xFFFF);
    obPutW(B + 0x1A, 0x8000); obPutW(B + 0x1E, 0x8000); obPutW(B + 0x20, 0xFFFF);
    /* FALLTHROUGH: 5542 */
  case 2:                                                              // C2/5542
    if (B[3] < 0x30) { obIntegrate(B); obAnimTick(ram, B); return 0; }
    B[2]++;
    obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(B + OWOBJ_YPOS) + 0x0030));
    leadInstall(B, 0x1F);
    /* FALLTHROUGH: 556D */
  case 3:                                                              // C2/556D
    if (ram[0x029E] & 0x20) {
      B[2] = 4; B[3] = 0;
      obPutW(B + 0x2A, obW(ram + 0x029A)); obPutW(B + 0x2C, (uint16_t)(obW(ram + 0x029C) + 0xFFE8));
      obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(B + OWOBJ_YPOS) + 0xFFD0));
      owDacToward(ram, B);
      leadInstall(B, 0x4E);
      obAnimTick(ram, B);
      return 0;
    }
    obPutW(B + OWOBJ_XPOS, obW(ram + (slot1 ? 0x02A6 : 0x02AA)));     // C2/55A7
    obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(ram + (slot1 ? 0x02A8 : 0x02AC)) + 0x0018));
    obWrapWorld(B);
    obAnimTick(ram, B);
    return 0;
  case 4:                                                              // C2/55D1
    if (B[3] < 0x31) { obIntegrate(B); obAnimTick(ram, B); return 0; }
    B[2] = 1;
    obPutW(B + OWOBJ_XFRAC, 0); obPutW(B + OWOBJ_YFRAC, 0); owVelZero(B);
    obAnimTick(ram, B);
    return 0;
  default:
    return 0;
  }
}

// ===================== THE WORLD MAP ($027C = 6: C2/258D -> C2/631F) =====================
// disasm/notes/ow_world_map.md. Select aboard the Epoch or a Dactyl. A mode-7 picture of the whole world built from its own
// map: one pixel wide and two high for each 16 x 16 cell (the colour of one pixel of the cell's tiles), zoomed out from the
// party's place (class $666C), the party's and the Epoch's markers and the era's label; B zooms back in.
static void owLzTo(uint8_t* ram, uint32_t src, uint8_t bank, uint16_t dst);   // (with the time gauge, below)
static unsigned owRomW6(unsigned off) {                                // a word of bank $C6
  long o = 0x060000L + (long)off;
  return (g_owRom && o + 1 < g_owRomLen) ? (unsigned)(g_owRom[o] | (g_owRom[o + 1] << 8)) : 0u;
}
// C2/658E: the colours the map drops (the sea's, the lava's): 0F, 2F and 24-2B; on 1F3 40-4E instead.
static uint8_t owMapFilter(const uint8_t* ram, uint8_t a) {
  if (obW(ram + 0x0100) == 0x01F3) return (a >= 0x40 && a < 0x4F) ? 0 : a;
  if (a == 0x0F || a == 0x2F || (a >= 0x24 && a < 0x2C)) return 0;
  return a;
}
// C2/653E: one cell's two pixels -- its block definition at `$0E` (8 bytes: four tile words), the tiles of words 1 and 3
// through C2/6400's table at $7E:AB25 (one colour a tile), each OR its palette ((attr & 1C) << 2), filtered, at ($05) and
// ($05) + 8 (the next row of the mode-7 char) unless the tile's colour is 0.
static void owMapCell(uint8_t* ram) {
  unsigned e = obW(ram + 0x0E), at = obW(ram + 0x05);
  ram[0x00] = (uint8_t)((ram[(e + 3) & 0xFFFF] & 0x1C) << 2); ram[0x01] = (uint8_t)((ram[(e + 7) & 0xFFFF] & 0x1C) << 2);
  uint8_t c = ram[0xAB25 + (obW(ram + ((e + 2) & 0xFFFF)) & 0x03FF)];
  if (c) ram[at & 0xFFFF] = owMapFilter(ram, (uint8_t)(c | ram[0x00]));
  c = ram[0xAB25 + (obW(ram + ((e + 6) & 0xFFFF)) & 0x03FF)];
  if (c) ram[(at + 8) & 0xFFFF] = owMapFilter(ram, (uint8_t)(c | ram[0x01]));
}
// C2/6736: the mode-7 matrix of the zoom -- +22 the angle, +24 / +26 the scales; C2/67B3 divides cos x 8 and sin x 8 by them
// (16 by 8 bits through $4204-$4206; a scale of 0 gives FFFF): M7A = cos / sx, M7B = sin / sx, M7C = -(sin / sy), M7D =
// cos / sy, signs put back after.
static unsigned owDiv16x8(unsigned a, unsigned b) { return b ? (a & 0xFFFF) / b : 0xFFFF; }
static void owMapMatrix(uint8_t* ram, uint8_t* B) {
  obPutW(ram + 0x10, 0);
  int s = owSine(obW(B + 0x22)), c = owSine((int)obW(B + 0x22) + 0x100);
  if (s < 0) { s = -s; ram[0x10]++; }
  obPutW(ram + 0x0A, (uint16_t)((s << 3) & 0xFFFF));
  if (c < 0) { c = -c; ram[0x11]++; }
  obPutW(ram + 0x08, (uint16_t)((c << 3) & 0xFFFF));
  obPutW(ram + 0x0C, obW(B + 0x24)); obPutW(ram + 0x0E, obW(B + 0x26));
  unsigned sx = ram[0x0C], sy = ram[0x0E];
  unsigned m00 = owDiv16x8(obW(ram + 0x08), sx), m02 = owDiv16x8(obW(ram + 0x0A), sx);
  unsigned q3 = owDiv16x8(obW(ram + 0x0A), sy), m06 = owDiv16x8(obW(ram + 0x08), sy);
  unsigned m04 = (0x10000 - q3) & 0xFFFF;
  if (ram[0x10]) { m02 = (0x10000 - m02) & 0xFFFF; m04 = (0x10000 - m04) & 0xFFFF; }
  if (ram[0x11]) { m00 = (0x10000 - m00) & 0xFFFF; m06 = (0x10000 - m06) & 0xFFFF; }
  obPutW(ram + 0x00, (uint16_t)m00); obPutW(ram + 0x02, (uint16_t)m02); obPutW(ram + 0x04, (uint16_t)m04); obPutW(ram + 0x06, (uint16_t)m06);
  obPutW(ram + 0x2C, (uint16_t)m00); obPutW(ram + 0x2E, (uint16_t)m02); obPutW(ram + 0x30, (uint16_t)m04); obPutW(ram + 0x32, (uint16_t)m06);
}
// THE CONTROLLER, class $666C (states C2:6675): centre the plane on the party, zoom out (C6:F560 / F5A0, 32 steps), wait for
// B (`$00F6 & 08`), zoom back in, `$027C = 7` and away.
static int obMapCtl(uint8_t* ram, uint8_t* B) {
  for (;;) switch (B[2]) {
  case 0: {                                                            // C2/667D
    B[2]++;
    unsigned x = (obW(ram + 0x0283) >> 4) + 0x10;
    obPutW(ram + 0x34, (uint16_t)x); obPutW(ram + 0x20, (uint16_t)((0x10000 - x) & 0xFFFF));
    obPutW(ram + 0x36, (uint16_t)((obW(ram + 0x0285) >> 3) + 0x30)); obPutW(ram + 0x26, 0);
    obPutW(B + 0x22, 0); obPutW(B + 0x28, 0);
  }  /* FALLTHROUGH */
  case 1: {                                                            // C2/66AB: the zoom out
    unsigned k = obW(B + 0x28);
    obPutW(B + 0x24, (uint16_t)owRomW6(0xF560 + k)); obPutW(B + 0x26, (uint16_t)owRomW6(0xF5A0 + k));
    obPutW(B + 0x28, (uint16_t)(k + 2));
    if (obW(B + 0x28) >= 0x40) { obPutW(B + 0x28, 0x003E); B[2]++; CtWorldSpawnVm(ram, 0xC2, 0x6A0A); }
    owMapMatrix(ram, B);
    return 0;
  }
  case 2:                                                              // C2/66E9: B
    if (obW(ram + 0x00F6) & 0x0008) { B[2]++; CtWorldSpawnVm(ram, 0xC2, 0x69FF); }
    return 0;
  case 3: {                                                            // C2/6704: the zoom back in
    unsigned k = obW(B + 0x28);
    obPutW(B + 0x24, (uint16_t)owRomW6(0xF560 + k)); obPutW(B + 0x26, (uint16_t)owRomW6(0xF5A0 + k));
    obPutW(B + 0x28, (uint16_t)(k - 2));
    owMapMatrix(ram, B);
    if (obW(B + 0x28) == 0) { ram[0x027C] = 0x07; return 1; }         // 6728: SEC
    return 0;
  }
  default: return 0;
  }
}
// THE MARKERS, classes $67FA (the party: $0283/$0285) and $683B (the Epoch, on its world only): C2/688A's map place (x / 8 +
// 20, y / 8 + 30, each with the LSRs' carry), their blink in bank C2 (6A18 / 6A20); gone when `$1B48` (the zoom back) is set.
// $68A3, the era's label: C2:68E4[world], y C0, pose 47.
static void owMapPlace(uint8_t* B) {
  unsigned x = obW(B + OWOBJ_XPOS), y = obW(B + OWOBJ_YPOS);
  obPutW(B + OWOBJ_XPOS, (uint16_t)((x >> 3) + 0x20 + ((x >> 2) & 1)));
  obPutW(B + OWOBJ_YPOS, (uint16_t)((y >> 3) + 0x30 + ((y >> 2) & 1)));
}
static int obMapMarker(uint8_t* ram, uint8_t* B, uint16_t cls) {
  if (B[2] == 0) {
    if (cls == 0x683B && (obW(ram + 0x0100) & 0x01FF) != obW(ram + 0x029F)) return 1;   // 684F BNE -> SEC
    B[2]++; B[OWOBJ_ATTR] = 0x30; obPutW(B + 0x10, 0);
    if (cls == 0x68A3) {
      obPutW(B + OWOBJ_XPOS, (uint16_t)owRomW(0x68E4 + 2 * ((obW(ram + 0x0100) & 0x01FF) - 0x01F0)));
      obPutW(B + OWOBJ_YPOS, 0x00C0);
      leadInstall(B, 0x47);
    } else {
      unsigned a = cls == 0x67FA ? 0x0283 : 0x0290;
      obPutW(B + OWOBJ_XPOS, obW(ram + a)); obPutW(B + OWOBJ_YPOS, obW(ram + a + 2));
      owMapPlace(B);
      obPutW(B + OWOBJ_ANIM, cls == 0x67FA ? 0x6A18 : 0x6A20); B[OWOBJ_ANIMBK] = 0xC2; B[OWOBJ_DWELL] = 0;
    }
  }
  if (ram[0x1B48]) return 1;
  obAnimTick(ram, B);
  return 0;
}
// C2/631F's NMI-off half (the world stashed by C2/258D before it): BGMODE 7, OBSEL 63, CGWSEL 00 / CGADSUB 20 (the fixed colour
// added to the backdrop), VRAM cleared (C2/26A8), C2/03EF, C2/0471; C2/6965 the era's label (its text through the
// proportional-text engine C2/57DF / 5823, vwf.c, into a 4 bpp strip at $7E:8621 for OBJ tiles $6200); C2/68F4 the backdrop's blue ramp (HDMA
// channel 1 to $2132, 16 shades x 7 lines, twice); C2/63DF the map; the markers' tiles C6:F500 / F520 to VRAM $6000 / $6020,
// CGRAM 0 = 0, C6:F540 to $0A20; the controller and script C2:69F8.
void CtWorldMapEnter(uint8_t* ram, uint8_t* vram) {
  ram[0x1E] = 0x07; ram[0x45] = 0x00; ram[0x46] = 0x20;                // 631F; `$2101 = 63` is the host's OBSEL
  if (vram) memset(vram, 0, 0x10000);                                  // C2/26A8
  ram[0x5E] = 0; ram[0x5F] = 0; obPutW(ram + 0xE0, 0xF000); ram[0xE2] = 0x7E; memset(ram + 0x60, 0, 0x80);   // C2/03EF
  obPutW(ram + 0x4E, 0x0B30);                                          // C2/0471
  for (int b = OWOBJ_BASE; b < OWOBJ_END; b += OWOBJ_STRIDE) obPutW(ram + b, 0);
  // C2/6965: the label's parameters and its (empty) buffer to VRAM $6200
  memset(ram + 0x8621, 0, 0x401);                                      // C2/5798
  unsigned w = (obW(ram + 0x0100) & 0x01FF) - 0x01F0;
  uint8_t id = (uint8_t)owRomW(0x69F0 + w);
  if (id == 0x6C && ram[0x1BA6] < 0x36) id = 0x6F;
  else if (id == 0x6B && !(ram[0x1BAD] & 0x01)) id = 0x6F;
  ram[0x020C] = id; obPutW(ram + 0x020D, 0xF400); ram[0x020F] = 0xC6; ram[0x0214] = 0x02;
  obPutW(ram + 0x0210, 0x8621); ram[0x0212] = 0x7E;
  if (g_owRom) {
    CtVwfBegin(ram, g_owRom, g_owRomLen);                              // JSL C2/57DF
    ram[0x0213] = 0x0C; CtVwfRun(ram, g_owRom, g_owRomLen);            // twelve glyphs, JSL C2/5823 (13 would be "65000000 B.C.")
  }
  if (vram) memcpy(vram + 2 * 0x6200, ram + 0x8621, 0x400);
  // C2/68F4
  { unsigned o = 0x8B56;                                               // the second loop runs on from 10 round to 10 again: 256
    for (int v = 0; v < 16; v++) for (int k = 0; k < 7; k++) ram[o++] = (uint8_t)(v | 0x80);   // values (the HDMA reads 224 lines)
    for (int i = 0; i < 256; i++) for (int k = 0; k < 7; k++) ram[o++] = (uint8_t)(((0x10 + i) & 0xFF) | 0x80);
    ram[0x00] = 0x10; }
  ram[0x8A35] = 0xF0; ram[0x8A38] = 0xF0; ram[0x8A3B] = 0;
  obPutW(ram + 0x8A21, 0x8B56); obPutW(ram + 0x8A23, 0x8BC6);
  CtWorldHdmaSet(1, 0x40, 0x32, 0x8A35); ram[0x4A] |= 0x02;
  // C2/63DF -> C2/27DE: the world's seven char packets (C6:FD00[world] -> [$ED], C6:FE20) into $7F:9000 + k x 1000
  { unsigned h = owRomW6(0xFD00 + 2 * w);
    for (int k = 0; k < 7; k++) {
      uint8_t pk = g_owRom ? g_owRom[0x060000 + h + (unsigned)k] : 0x80;
      if (pk & 0x80) continue;
      unsigned t = 0xFE20 + 3u * pk;
      uint32_t src = (uint32_t)owRomW6(t) | ((uint32_t)(owRomW6(t + 2) & 0xFF) << 16);
      owLzTo(ram, src, 0x7F, (uint16_t)(0x9000 + 0x1000 * k));
    } }
  // C2/6400: one colour a tile -- the pixel at column 3 of row 2, its four planes (+04 +05 +14 +15) -- for 0x380 tiles
  for (unsigned t = 0; t < 0x380; t++) {
    const uint8_t* T = ram + 0x10000 + 0x9000 + 32 * t;
    unsigned c = (((T[0x15] >> 4) & 1) << 3) | (((T[0x14] >> 4) & 1) << 2) | (((T[0x05] >> 4) & 1) << 1) | ((T[0x04] >> 4) & 1);
    ram[0xAB25 + t] = (uint8_t)c;
  }
  // C2/645C: the cells, four rows at a time into the staging $7E:F000, sent as 12 chars to the high bytes from VRAM $0040
  { obPutW(ram + 0x10, 0x5800); obPutW(ram + 0x13, 0x4000); obPutW(ram + 0x16, 0x3800); obPutW(ram + 0x19, 0x3000);
    unsigned stg = obW(ram + 0xE0), vw = 0x0040;
    obPutW(ram + 0x05, (uint16_t)stg); memset(ram + stg, 0, 0x400);    // (C = 03FE: B left at 03 by C2/6400's CMP #0380)
    for (unsigned r = 0; r < 0x40; r++) {
      for (unsigned c = 0; c < 0x60; c++) {
        obPutW(ram + 0x0E, (uint16_t)(ram[(obW(ram + 0x10) + c) & 0xFFFF] * 8 + obW(ram + 0x16)));
        owMapCell(ram);
        if (obW(ram + 0x0100) != 0x01F5) {
          obPutW(ram + 0x0E, (uint16_t)(ram[(obW(ram + 0x13) + c) & 0xFFFF] * 8 + obW(ram + 0x19)));
          owMapCell(ram);
        }
        obPutW(ram + 0x05, (uint16_t)(obW(ram + 0x05) + (((c + 1) & 7) ? 1 : 0x39)));
      }
      obPutW(ram + 0x10, (uint16_t)(obW(ram + 0x10) + 0x60)); obPutW(ram + 0x13, (uint16_t)(obW(ram + 0x13) + 0x60));
      obPutW(ram + 0x05, (uint16_t)(obW(ram + 0x05) - 0x2F0));
      if (((r + 1) & 3) == 0) {
        if (vram) for (unsigned i = 0; i < 0x300; i++) vram[2 * ((vw + i) & 0x7FFF) + 1] = ram[(stg + i) & 0xFFFF];
        memset(ram + stg, 0, 0x300); obPutW(ram + 0x05, (uint16_t)stg); vw += 0x300;
      }
    }
    obPutW(ram + 0x08, 0x40); obPutW(ram + 0x0A, 0x60); obPutW(ram + 0x0C, (uint16_t)vw); }
  // C2/65B2: the mode-7 map -- tiles 1..C0, 12 a row, 16 rows, at column 2, row 6 of the 32 x 32 corner (low bytes)
  memset(ram + 0xF000, 0, 0x400);
  for (unsigned r = 0; r < 16; r++) for (unsigned c = 0; c < 12; c++) ram[0xF000 + (6 + r) * 0x20 + 2 + c] = (uint8_t)(1 + r * 12 + c);
  if (vram) for (unsigned r = 0; r < 32; r++) for (unsigned c = 0; c < 32; c++) vram[2 * (r * 0x80 + c)] = ram[0xF000 + r * 0x20 + c];
  ram[0x00] = 0; obPutW(ram + 0x0A, (uint16_t)(0xF000 + 32 * 0x20)); obPutW(ram + 0x0C, 0x1000);
  // the markers' tiles, CGRAM 0, the markers' palette
  if (vram && g_owRom) { memcpy(vram + 2 * 0x6000, g_owRom + 0x06F500, 0x20); memcpy(vram + 2 * 0x6020, g_owRom + 0x06F520, 0x20); }
  obPutW(ram + 0x0920, 0);
  if (g_owRom) memcpy(ram + 0x0A20, g_owRom + 0x06F540, 0x21);
  uint8_t* P = ram + 0x0B30;
  obAllocNative(ram, P, 0x666C);
  CtWorldSpawnVm(ram, 0xC2, 0x69F8);
  ram[0x4C] |= 0x03;
}

// ===================== THE TIME GAUGE ($027C = 8: C2/261D -> C2/6A34) =====================
// disasm/notes/ow_time_gauge.md. The gauge is a screen of the overworld's own engine: its objects run in the same pass,
// the controller $6AC8, the swirl $6F85 (the mode-7 matrix per line), seven era markers $7143 and three palette / fade
// scripts. `+22` of the controller is the era (0..6) over the table C2:6C97 -- or C2:6CA5 once the storyline ($1BA6)
// reaches CC: 1D9 (the End of Time), 1F2, 1F7, 1F0, 1F1, 1F4 / 1F6, 1F3.
static unsigned owEraWorld(const uint8_t* ram, int i) {                // C2/745A
  return owRomW((ram[0x1BA6] >= 0xCC ? 0x6CA5 : 0x6C97) + 2 * (unsigned)(i & 0xFF));
}
static int owEraNow(const uint8_t* ram) {                              // C2/747A: the current world's era, else 6
  unsigned cur = obW(ram + 0x0100) & 0x01FF;
  for (int k = 0; k < 7; k++) if (owEraWorld(ram, k) == cur) return k;
  return 6;
}
// C2/6F33 -- `$19..$1B` = `$0E` (16 bits) x `$07` (8 bits), both signed, through $4202/$4203 twice (the low byte, then the
// high byte shifted in): 24 bits, negated when the signs differ. Leaves `$07` and `$0E` as their magnitudes.
static void owMul6F33(uint8_t* ram) {
  int neg = ((ram[0x0F] ^ ram[0x07]) & 0x80) != 0;                     // 6F35 LDA $0F / EOR $07 / PHP
  if (ram[0x07] & 0x80) ram[0x07] = (uint8_t)(0x100 - ram[0x07]);      // 6F3C EOR #FF / INC A
  unsigned e = obW(ram + 0x0E);
  if (e & 0x8000) { e = (0x10000 - e) & 0xFFFF; obPutW(ram + 0x0E, (uint16_t)e); }
  unsigned f = ram[0x07];
  unsigned p1 = f * (e & 0xFF), p2 = f * (e >> 8);                     // 6F53-6F69
  unsigned r = p1 + (p2 << 8);                                         // $19 = p1, $1A/$1B += p2
  if (neg) r = (0x1000000 - r) & 0xFFFFFF;                             // 6F73-6F80: 0 - $19/$1A, 0 - $1B with the borrow
  ram[0x19] = (uint8_t)r; ram[0x1A] = (uint8_t)(r >> 8); ram[0x1B] = (uint8_t)(r >> 16);
}
// C2/6CC1 -- THE NEEDLE: the triangle C2:6F2D turned by the angle +18 about (84, 70), rasterized into window 1's edges
// for each line (HDMA channel 1, DMAP 41 to WH0 / WH1). The two line buffers $8B56 / $8DF6 swap every call (`$1BF5` bit
// 0); `$1BB8..$1BBD` the turned vertices, `$1BC4..` the edges (active, slope, x 8.8, top, bottom, lines left).
static void owGaugeNeedle(uint8_t* ram, uint8_t* B) {
  unsigned buf;
  if (!(ram[0x1BF5] & 1)) { ram[0x1BF5] |= 1; obPutW(ram + 0x8A21, 0x8DF6); obPutW(ram + 0x8A23, 0x8ED6); buf = 0x8DF6; }
  else { ram[0x1BF5] &= (uint8_t)~1; obPutW(ram + 0x8A21, 0x8B56); obPutW(ram + 0x8A23, 0x8C36); buf = 0x8B56; }
  obPutW(ram + 0x16, (uint16_t)buf);
  ram[0x00] = 0x84; ram[0x01] = 0x70;
  unsigned ang = obW(B + 0x18);
  obPutW(ram + 0x08, (uint16_t)owSine((int)ang + 0x100));             // 6D10 JSL C2/225A
  obPutW(ram + 0x0A, (uint16_t)owSine((int)ang));                     // 6D17 JSL C2/225E
  ram[buf] = 0x01; ram[buf + 1] = 0x00; ram[buf + 2] = 0x00;           // 6D1E-6D25: line 0 empty (WH0 01 > WH1 00)
  for (unsigned i = 0; i < 0x9E; i++) ram[buf + 0x92 + i] = ram[buf + 0x90 + i];   // 6D27 MVN: line 72's pair onward
  obPutW(ram + 0x0C, 0);
  unsigned wp = 0x1BB8;                                                // 6D3A $2181 = 1BB8 (bank 00)
  for (int v = 0; v < 3; v++) {                                        // 6D47 LDA #03 / STA $06
    unsigned c = obW(ram + 0x0C);
    ram[0x02] = (uint8_t)owRomW(0x6F2D + c); ram[0x03] = (uint8_t)owRomW(0x6F2D + c + 1);
    obPutW(ram + 0x0C, (uint16_t)(c + 2));
    obPutW(ram + 0x0E, obW(ram + 0x08)); ram[0x07] = ram[0x02]; owMul6F33(ram);   // cos x vx
    obPutW(ram + 0x04, obW(ram + 0x19));
    obPutW(ram + 0x0E, obW(ram + 0x0A)); ram[0x07] = ram[0x03]; owMul6F33(ram);   // sin x vy
    unsigned s = obW(ram + 0x19) + obW(ram + 0x04);                    // 6D79 CLC / ADC $04 / XBA / ADC $00
    ram[wp++] = (uint8_t)(((s >> 8) & 0xFF) + ram[0x00] + ((s >> 16) & 1));
    obPutW(ram + 0x0E, obW(ram + 0x0A)); ram[0x07] = ram[0x02]; owMul6F33(ram);   // sin x vx
    obPutW(ram + 0x04, obW(ram + 0x19));
    obPutW(ram + 0x0E, obW(ram + 0x08)); ram[0x07] = ram[0x03]; owMul6F33(ram);   // cos x vy
    unsigned a = obW(ram + 0x19), b = obW(ram + 0x04);                 // 6DA2 SEC / SBC $04 / XBA / ADC $01
    unsigned d = (a - b) & 0xFFFF;
    ram[wp++] = (uint8_t)(((d >> 8) & 0xFF) + ram[0x01] + (a >= b ? 1 : 0));
    ram[0x06] = (uint8_t)(2 - v);
  }
  // 6DB3: the three edges, each top (smaller y) to bottom.
  ram[0x00] = 3; obPutW(ram + 0x19, 0); obPutW(ram + 0x10, 0); obPutW(ram + 0x13, 0);
  for (;;) {
    unsigned x10 = obW(ram + 0x10);
    ram[0x08] = ram[0x1BB8 + x10]; ram[0x0A] = ram[0x1BB9 + x10];
    if (ram[0x00] == 1) { ram[0x0C] = ram[0x1BB8]; ram[0x0E] = ram[0x1BB9]; }
    else                { ram[0x0C] = ram[0x1BBA + x10]; ram[0x0E] = ram[0x1BBB + x10]; }
    uint8_t ynext = ram[0x0E];
    if (ynext != ram[0x0A]) {                                          // 6DE8 CMP $0A / BNE
      if (ynext < ram[0x0A]) {                                         // 6DEF BCS: swap the ends
        uint8_t t = ram[0x0A]; ram[0x0E] = t; ram[0x0F] = ram[0x0B]; ram[0x0A] = ynext;   // 6DF1 LDY $0A / STY $0E (16)
        t = ram[0x08]; ram[0x08] = ram[0x0C]; ram[0x09] = ram[0x0D]; ram[0x0C] = t;        // 6DF9 LDY $0C / STY $08 (16)
      }
      ram[0x0E]--;                                                     // 6DFF DEC $0E
      ram[0x04] = (uint8_t)(ram[0x0E] - ram[0x0A]);
      uint8_t dx = (uint8_t)(ram[0x0C] - ram[0x08]); int neg = (dx & 0x80) != 0;
      ram[0x02] = neg ? (uint8_t)(0x100 - dx) : dx;
      unsigned q = ram[0x04] ? ((unsigned)ram[0x02] << 8) / ram[0x04] : 0xFFFF;   // 6E17 $4204-$4206
      unsigned x = obW(ram + 0x13);
      ram[0x1BC4 + x] = 0;
      ram[0x1BDD + x] = ram[0x08]; ram[0x1BDC + x] = 0;
      ram[0x1BD1 + x] = ram[0x0A]; ram[0x1BD0 + x] = 0;
      ram[0x1BE2 + x] = (uint8_t)(ram[0x0E] - ram[0x0A]); ram[0x1BE3 + x] = 0;
      ram[0x1BD7 + x] = ram[0x0E]; ram[0x1BD6 + x] = 0;
      obPutW(ram + 0x1BCA + x, (uint16_t)(neg ? (0x10000 - q) & 0xFFFF : q & 0xFFFF));
      obPutW(ram + 0x19, (uint16_t)(obW(ram + 0x19) + 1));
      obPutW(ram + 0x13, (uint16_t)(x + 2));
    }
    obPutW(ram + 0x10, (uint16_t)(x10 + 2));                           // 6E67
    if (--ram[0x00] == 0) break;
  }
  unsigned n = obW(ram + 0x19) & 0xFF;
  if (!n) return;
  uint8_t ymin = ram[0x1BD1], ymax = ram[0x1BD7];                      // 6E76-6EA6: the lines the edges cover
  for (unsigned k = 1; k < n; k++) {
    if (ram[0x1BD1 + 2 * k] < ymin) ymin = ram[0x1BD1 + 2 * k];
    if (ram[0x1BD7 + 2 * k] >= ymax) ymax = ram[0x1BD7 + 2 * k];
  }
  ram[0x08] = ymin; ram[0x0A] = ymax; ram[0x13] = (uint8_t)(ymax - ymin);
  unsigned out = buf + (unsigned)ymin * 2;                             // 6EAF $2181 = $16 + $08 * 2 (bank $18 = 7E)
  for (;;) {                                                           // 6EC3, a line
    for (unsigned k = 0; k < n; k++) if (ram[0x1BD1 + 2 * k] == ram[0x08]) ram[0x1BC4 + 2 * k]++;
    unsigned y = 0;
    for (unsigned k = 0; k < n; k++) {
      unsigned x = 2 * k;
      if (!ram[0x1BC4 + x]) continue;
      obPutW(ram + y, obW(ram + 0x1BDC + x));                          // 6EE8 STA $0000,Y
      obPutW(ram + 0x1BDC + x, (uint16_t)(obW(ram + 0x1BDC + x) + obW(ram + 0x1BCA + x)));
      y += 2;
      if ((uint8_t)(--ram[0x1BE2 + x]) & 0x80) ram[0x1BC4 + x] = 0;   // 6EF9 DEC / BPL / STZ
    }
    uint8_t l = ram[0x01], r = ram[0x03];
    if (l >= r) { ram[out & 0xFFFF] = r; ram[(out + 1) & 0xFFFF] = l; }
    else        { ram[out & 0xFFFF] = l; ram[(out + 1) & 0xFFFF] = r; }
    out += 2;
    ram[0x08]++;
    if (--ram[0x13] == 0) break;
  }
}
static int g_owGaugeSound = 0;                                         // the controller's error post (see obGaugeCtl)
// THE CONTROLLER, class $6AC8 (states C2:6AD1). Returns 0 (CLC), or 2 when it posted to the sound driver inline.
static int obGaugeCtl(uint8_t* ram, uint8_t* B) {
  int turn = 0;
  if (B[2] == 0) {                                                     // C2/6AD7
    B[2]++;
    B[0x22] = (uint8_t)owEraNow(ram);                                  // 6ADC JSR C2/747A
    ram[0x8A35] = 0xF0; ram[0x8A38] = 0xF0; ram[0x8A3B] = 0x00;
    ram[0x18] = 0x7E;
    CtWorldHdmaSet(1, 0x41, 0x26, 0x8A35);                             // 6AF7 $4310 = 2641: WH0 / WH1 from $8A35
    ram[0x4A] |= 0x02;
    ram[0x00] = 0x84; ram[0x01] = 0x70;
    ram[0x8B56] = 0x01; ram[0x8B57] = 0x00; ram[0x8B58] = 0x00;        // 6B1A: `01 00` ...
    for (unsigned i = 0; i < 0x1BE; i++) ram[0x8B58 + i] = ram[0x8B56 + i];   // 6B26 MVN 8B56 -> 8B58
    memmove(ram + 0x8DF6, ram + 0x8B56, 0x1C0);                        // 6B32 MVN 8B56 -> 8DF6
    unsigned a = owRomW(0x6CB3 + 2 * (unsigned)B[0x22]);               // 6B49 LDA $C26CB3,X
    obPutW(B + 0x18, (uint16_t)a); obPutW(B + 0x14, (uint16_t)a);
    ram[0x1BB7] = B[0x22];
    owGaugeNeedle(ram, B);                                             // 6B5B JSR C2/6CC1, on into 6B5E
  }
  if (B[2] == 1) {                                                     // C2/6B5E: the pad
    unsigned pad = obW(ram + 0x00F8);
    int dir = 0;
    if (pad & 0x0010) dir = -1; else if (pad & 0x0020) dir = 1;
    else if (pad & 0x0100) dir = -1; else if (pad & 0x0200) dir = 1;
    else if (pad & 0x0800) dir = -1; else if (pad & 0x0400) dir = 1;
    else if (pad & 0x0080) {                                           // C2/6BBD: A
      unsigned w = owEraWorld(ram, B[0x22]);
      if (w != (obW(ram + 0x0100) & 0x01FF)) { obPutW(ram + 0x02AF, (uint16_t)w); ram[0x027C] = 0x07; return 0; }
      ram[0x1E00] = 0x18; ram[0x1E01] = 0x01; ram[0x1E02] = 0x80;      // 6BD8: the era you are in -- the buzz
      g_owGaugeSound = 1;
      return 2;
    } else if (pad & 0x0008) { obPutW(ram + 0x02AF, 0xFFFF); ram[0x027C] = 0x07; return 0; }   // C2/6BEF
    if (!dir) return 0;
    if (dir < 0) { B[0x22]--; if (B[0x22] & 0x80) B[0x22] = 0; obPutW(B + 0x20, 0xFFFC); }   // 6B8E
    else { B[0x22]++; if (B[0x22] >= 7) B[0x22] = 6; obPutW(B + 0x20, 0x0004); }            // 6BA2
    obPutW(B + 0x14, (uint16_t)owRomW(0x6CB3 + 2 * (unsigned)B[0x22]));   // 6BFE
    B[2]++;
    turn = 1;
  }
  if (B[2] == 2) {                                                     // C2/6C14: the turn
    (void)turn;
    if (obW(B + 0x18) != obW(B + 0x14)) {                              // 6C86
      obPutW(B + 0x18, (uint16_t)(obW(B + 0x18) + obW(B + 0x20)));
      owGaugeNeedle(ram, B);
      return 0;
    }
    if (owEraWorld(ram, B[0x22]) != (obW(ram + 0x0100) & 0x01FF)) {   // 6C77: arrived
      ram[0x1BB7] = B[0x22]; B[2] = 1;
      return 0;
    }
    if (!(obW(B + 0x20) & 0x8000)) {                                   // the era you are in: on past it, bouncing
      B[0x22]++;
      if (B[0x22] >= 7) { B[0x22] = (uint8_t)(B[0x22] - 2); obPutW(B + 0x20, (uint16_t)(0x10000 - obW(B + 0x20))); }
    } else {
      B[0x22]--;
      if (B[0x22] & 0x80) { B[0x22] = (uint8_t)(B[0x22] + 2); obPutW(B + 0x20, (uint16_t)(0x10000 - obW(B + 0x20))); }
    }
    obPutW(B + 0x14, (uint16_t)owRomW(0x6CB3 + 2 * (unsigned)B[0x22]));
  }
  return 0;
}
// THE SWIRL, class $6F85: the mode-7 matrix a line at a time -- HDMA channels 2 / 5 (M7A / M7D) from $8A42 and 3 / 4 (M7B /
// M7C) from $8A4F, `F0 p1 F0 p2 00` each; A = D = cos(t), B = C = sin(t + 80), t stepping 4 a line, the 224-line buffers
// double ($9176 / $9416 and $9796 / $9A36, `$1BF5` bit 1) and shifted a line a frame with the newest at the bottom.
static int obGaugeSwirl(uint8_t* ram, uint8_t* B) {
  if (B[2] == 0) {                                                     // C2/6F92
    B[2]++;
    obPutW(B + 0x18, 0);
    ram[0x8A42] = ram[0x8A4F] = ram[0x8A45] = ram[0x8A52] = 0xF0;
    ram[0x8A48] = ram[0x8A55] = 0x00;
    CtWorldHdmaSet(2, 0x42, 0x1B, 0x8A42); CtWorldHdmaSet(5, 0x42, 0x1E, 0x8A42);
    CtWorldHdmaSet(3, 0x42, 0x1C, 0x8A4F); CtWorldHdmaSet(4, 0x42, 0x1D, 0x8A4F);
    ram[0x4A] |= 0x3C;
    ram[0x1BF5] &= (uint8_t)~0x02;
    obPutW(ram + 0x8A25, 0x9176); obPutW(ram + 0x8A27, 0x9256);
    obPutW(ram + 0x10, 0x9176);
    ram[0x1BF5] &= (uint8_t)~0x04;
    obPutW(ram + 0x8A29, 0x9796); obPutW(ram + 0x8A2B, 0x9876);
    obPutW(ram + 0x13, 0x9796);
    unsigned t = obW(B + 0x18);
    obPutW(ram + 0x08, (uint16_t)t);
    for (unsigned i = 0; i < 0xE0; i++) {
      obPutW(ram + 0x9176 + 2 * i, (uint16_t)owSine((int)t + 0x100));
      obPutW(ram + 0x9796 + 2 * i, (uint16_t)owSine((int)t + 0x80));
      t = (t + 4) & 0xFFFF;
    }
    obPutW(ram + 0x10, 0x9176 + 2 * 0xE0); obPutW(ram + 0x13, 0x9796 + 2 * 0xE0);
    obPutW(ram + 0x08, (uint16_t)t); obPutW(ram + 0x00, 0);
    obPutW(B + 0x18, (uint16_t)t);
    return 0;
  }
  unsigned ad, bc;                                                     // C2/7087
  if (!(ram[0x1BF5] & 0x02)) {
    ram[0x1BF5] |= 0x02;
    obPutW(ram + 0x8A25, 0x9416); obPutW(ram + 0x8A27, 0x94F6); obPutW(ram + 0x8A29, 0x9A36); obPutW(ram + 0x8A2B, 0x9B16);
    memmove(ram + 0x9416, ram + 0x9178, 0x1BF); memmove(ram + 0x9A36, ram + 0x9798, 0x1BF);
    ad = 0x9416; bc = 0x9A36;
  } else {
    ram[0x1BF5] &= (uint8_t)~0x02;
    obPutW(ram + 0x8A25, 0x9176); obPutW(ram + 0x8A27, 0x9256); obPutW(ram + 0x8A29, 0x9796); obPutW(ram + 0x8A2B, 0x9876);
    memmove(ram + 0x9176, ram + 0x9418, 0x1BF); memmove(ram + 0x9796, ram + 0x9A38, 0x1BF);
    ad = 0x9176; bc = 0x9796;
  }
  obPutW(ram + 0x10, (uint16_t)ad); obPutW(ram + 0x13, (uint16_t)bc);
  unsigned t = obW(B + 0x18);
  obPutW(ram + ad + 0x1BE, (uint16_t)owSine((int)t + 0x100));
  obPutW(ram + bc + 0x1BE, (uint16_t)owSine((int)t + 0x80));
  obPutW(B + 0x18, (uint16_t)(t + 4));
  return 0;
}
// AN ERA MARKER, class $7143 (+22 = its era): placed from C2:71DD; drawn only while its era is the chosen one -- C2:71EB's
// appear script on the choice, C2:71EC's going away when it changes (each held to its end: +0E back to 0).
static int obGaugeMarker(uint8_t* ram, uint8_t* B) {
  unsigned k = B[0x22];
  switch (B[2]) {
  case 0:                                                              // C2/7156
    B[2]++; B[OWOBJ_ATTR] = 0xB0; B[0x10] = 0; B[0x11] = 0;
    B[0x14] = (uint8_t)owRomW(0x71DD + 2 * k); B[0x18] = (uint8_t)owRomW(0x71DE + 2 * k);
    B[0x15] = 0; B[0x19] = 0;
    /* FALLTHROUGH */
  case 1:                                                              // C2/7181
    if (ram[0x1BB7] == k) { B[2]++; leadInstall(B, owRomW(0x71EB + 2 * k) & 0xFF); obAnimTick(ram, B); }
    return 0;
  case 2: if (B[OWOBJ_DWELL] == 0) B[2]++; obAnimTick(ram, B); return 0;   // C2/719F
  case 3:                                                              // C2/71AE
    if (ram[0x1BB7] != k) { B[2]++; leadInstall(B, owRomW(0x71EC + 2 * k) & 0xFF); }
    obAnimTick(ram, B); return 0;
  case 4: if (B[OWOBJ_DWELL] == 0) B[2] = 1; obAnimTick(ram, B); return 0;   // C2/71CC
  }
  return 0;
}
int CtWorldGaugeTakeSound(void) { int s = g_owGaugeSound; g_owGaugeSound = 0; return s; }
// C2/6A34's NMI-off half: the VRAM cleared (C2/26A8), the NMI queue reset (C2/03EF), the object array emptied (C2/0471:
// the class words only), the PPU shadows (mode 7, the windows, the colour math), `$1BF5 = 0`, C2/7389's loads and spawns,
// `TSB $4C #03`. `vram` is the host's 64 KB picture memory.
static void owLzTo(uint8_t* ram, uint32_t src, uint8_t bank, uint16_t dst) {
  uint8_t* out = ram + (bank == 0x7F ? 0x10000 : 0) + dst;
  if (g_owRom && !ct_lz_decompress_bounded(g_owRom, (size_t)g_owRomLen, out, 0x10000u - dst, ct_lz_hirom_offset(src)))
    fprintf(stderr, "owgauge: the packet at $%06X did not decompress inside $%02X:%04X-$FFFF\n", src, bank, dst);
}
static void owGaugePacket(uint8_t* ram, uint8_t bank, int i) {         // C2/71F9: C2:7220[i] = {src, dst} in $7F
  ram[0x0302] = bank;
  unsigned s = owRomW(0x7220 + 4 * (unsigned)i), d = owRomW(0x7222 + 4 * (unsigned)i);
  obPutW(ram + 0x0300, (uint16_t)s); obPutW(ram + 0x0303, (uint16_t)d); ram[0x0305] = 0x7F;
  owLzTo(ram, ((uint32_t)bank << 16) | s, 0x7F, (uint16_t)d);
}
void CtWorldGaugeEnter(uint8_t* ram, uint8_t* vram) {
  if (vram) memset(vram, 0, 0x10000);                                  // C2/26A8: DMA fixed 0 over all of VRAM
  ram[0x5E] = 0; ram[0x5F] = 0; obPutW(ram + 0xE0, 0xF000); ram[0xE2] = 0x7E;   // C2/03EF
  memset(ram + 0x60, 0, 0x80);
  obPutW(ram + 0x4E, 0x0B30);                                          // C2/0471
  for (int b = OWOBJ_BASE; b < OWOBJ_END; b += OWOBJ_STRIDE) obPutW(ram + b, 0);
  ram[0x1E] = 0x07;                                                    // 6A3D: BGMODE 7 ($2101 = 03: OBJ base $6000 words)
  obPutW(ram + 0x20, 0xFF80); obPutW(ram + 0x26, 0xFF90);              // M7HOFS / M7VOFS
  obPutW(ram + 0x22, 0); obPutW(ram + 0x28, 0); obPutW(ram + 0x24, 0); obPutW(ram + 0x2A, 0);
  obPutW(ram + 0x2E, 0); obPutW(ram + 0x30, 0); obPutW(ram + 0x34, 0); obPutW(ram + 0x36, 0);
  ram[0x2D] = 0x01; ram[0x33] = 0x01; ram[0x38] = 0; ram[0x39] = 0; ram[0x3A] = 0x20;
  ram[0x43] = 0x11; ram[0x44] = 0x11; ram[0x45] = 0x10; ram[0x46] = 0x17;
  ram[0x47] = ram[0x48] = ram[0x49] = 0x0F;
  ram[0x1BF5] = 0;
  // C2/7389
  ram[0x08] = 0; owGaugePacket(ram, 0xC3, 0);                          // C3:8000 -> $7F:9000
  if (vram) memcpy(vram + 0xC800, ram + 0x19800, 0x800);               // VRAM $6400 <- $7F:9800 (words, $2118/9)
  obPutW(ram + 0x08, 0x6000); obPutW(ram + 0x10, 0x7321); ram[0x12] = 0xC2; obPutW(ram + 0x13, 0x9000); ram[0x15] = 0x7F;
  for (unsigned j = 0; j < 0x40; j++) {                                // C2/72D8: 64 tiles by C2:7321's list
    unsigned t = owRomW(0x7321 + j) & 0xFF;
    if (vram) memcpy(vram + 2 * (0x6000 + 0x10 * j), ram + 0x19000 + 32 * t, 0x20);
  }
  obPutW(ram + 0x08, 0x6400); obPutW(ram + 0x10, 0x7361); obPutW(ram + 0x0A, (uint16_t)(0x9000 + 32 * (owRomW(0x7360) & 0xFF)));
  ram[0x00] = 0;
  ram[0x08] = 1; owGaugePacket(ram, 0xC6, 1);                          // C6:FB00 -> $7F:8C00, the palettes
  memcpy(ram + 0xC000, ram + 0x18C00, 0x200);                          // 73E2 MVN -> $7E:C000
  ram[0x18DA2] = 0xFF; ram[0x18DA3] = 0xFF;
  memcpy(ram + 0x0920, ram + 0x18C00, 0x200);                          // 73F5 MVN -> $0920 (the CGRAM shadow)
  ram[0x08] = 2; owGaugePacket(ram, 0xC6, 2);                          // C6:2000 -> $7F:9000, the mode-7 map
  for (unsigned k = 0; k < 16; k++) {                                  // C2/7254: the one 32 x 32 block, 16 times over
    unsigned va = owRomW(0x72B8 + 2 * k), src = 0x9000;                //   (27262 `LDX #9000 / STX $0A` inside the loop)
    for (unsigned r = 0; r < 32; r++, va += 0x80, src += 0x40)
      if (vram) memcpy(vram + 2 * (va & 0x7FFF), ram + 0x10000 + src, 0x40);
  }
  obPutW(ram + 0x0C, 0x0020); obPutW(ram + 0x08, 0x4060); obPutW(ram + 0x0A, 0x9800); ram[0x00] = 0;
  for (unsigned i = 0; i < 0x400; i++) {                               // C2/7361: C6:F700's nybbles -> $7F:9000
    uint8_t b = g_owRom && 0x6F700 + i < (unsigned)g_owRomLen ? g_owRom[0x6F700 + i] : 0;
    ram[0x19000 + 2 * i] = (uint8_t)(b >> 4); ram[0x19001 + 2 * i] = (uint8_t)(b & 0x0F);
  }
  if (vram) for (unsigned i = 0; i < 0x800; i++) vram[2 * i + 1] = ram[0x19000 + i];   // C2/722C: $2119 (the high bytes)
  // the spawns: parent block $0B30 (C2/0471 left $4E there), C2/048A from $0C30
  uint8_t* P = ram + 0x0B30;
  obAllocNative(ram, P, 0x6AC8);
  CtWorldSpawnVm(ram, 0xC2, 0x74B8);
  CtWorldSpawnVm(ram, 0xC2, 0x7504);
  obAllocNative(ram, P, 0x6F85);
  uint8_t keep = P[0x22]; P[0x22] = 0;
  for (int k = 0; k < 7; k++) { obAllocNative(ram, P, 0x7143); P[0x22]++; }
  P[0x22] = keep;
  CtWorldSpawnVm(ram, 0xC2, 0x74AE);
  ram[0x4C] |= 0x03;                                                   // 6A8D TSB $4C
}
// The gauge closing (C2/6AA6, the main thread once `$027C != 8`): the fade-out script; after its 18 frames the host puts the
// overworld back (C2/6AB4 `$1E = 09`, then C2/2E72 / C2/2C93).
void CtWorldGaugeLeave(uint8_t* ram) { CtWorldSpawnVm(ram, 0xC2, 0x74B3); }

static uint8_t g_owNmiEn = 0, g_owNmiWh0 = 0, g_owNmiWh1 = 0;   // the NMI's HDMAEN / WH0 / WH1 pushes (see below)
// THE OVERWORLD'S HDMA CHANNELS as the natives program them ($43x0 DMAP, $43x1 B-bus target, $43x2-3 the table in bank
// $7E): channel 2 is window 1's right edge (C6/E241: DMAP 40 to $2127 from $8A42) unless the Black Omen's wave took it
// (C2/7C97: DMAP 42 to $2110); channel 1 is that wave's BG2HOFS (C2/7C56). What the picture reads, with HDMAEN ($4A as
// the NMI pushed it).
static struct { uint8_t dmap, bbad; uint16_t tbl; } g_owHdma[8] = { { 0 }, { 0 }, { 0x40, 0x27, 0x8A42 } };
void CtWorldHdmaSet(int ch, uint8_t dmap, uint8_t bbad, uint16_t tbl) {
  if (ch < 0 || ch > 7) return;
  g_owHdma[ch].dmap = dmap; g_owHdma[ch].bbad = bbad; g_owHdma[ch].tbl = tbl;
}
int CtWorldHdmaGet(int ch, uint8_t* dmap, uint8_t* bbad, uint16_t* tbl) {
  if (ch < 0 || ch > 7) return 0;
  *dmap = g_owHdma[ch].dmap; *bbad = g_owHdma[ch].bbad; *tbl = g_owHdma[ch].tbl;
  return g_owHdma[ch].bbad != 0;
}
void CtWorldNmiLatch(uint8_t* en, uint8_t* wh0, uint8_t* wh1) { *en = g_owNmiEn; *wh0 = g_owNmiWh0; *wh1 = g_owNmiWh1; }
static int g_owTraceFrame = 0;                    // the host's frame number, for CT_OWVEHTRACE
void CtWorldTraceFrame(int f) { g_owTraceFrame = f; }
// CT_OWVEHTRACE=1: the vehicle machine as each pass leaves it, on change -- `$027C $027E $027F $028F $0294`, then every
// block of the lead / Epoch / shadow / boarding-manager / follower classes as `addr:class/state` (the oracle's side:
// `ct.exe --monitor 7E027C:6,7E028F:1,7E0294:1,...`) -- and again as the host frame ends (CtWorldTraceEnd), so a write of the
// main thread after the pass (`$027C = 1` after a load) is in the frame it is made, where the oracle's monitor samples it.
static void owVehTrace(const uint8_t* ram, int isPass) {
  { static int tr = -1; static char last[400]; static int pass = 0;
    if (tr < 0) { const char* e = getenv("CT_OWVEHTRACE"); tr = (e && *e) ? 1 : 0; }
    if (isPass) pass++;
    if (tr) {
      char buf[400]; int n = snprintf(buf, sizeof buf, "027C=%02X 027E=%02X 027F=%02X 028F=%02X 0294=%02X 0290=%04X,%04X 0100=%04X 02AF=%04X 1BB7=%02X 029A=%04X,%04X 029E=%02X 1BF0=%02X 02A6=%04X,%04X,%04X,%04X 0280=%02X 0283=%04X,%04X,%04X,%04X,%04X,%04X 1C=%02X%02X 1F=%02X 1B48=%02X |",
                                      ram[0x027C], ram[0x027E], ram[0x027F], ram[0x028F], ram[0x0294], obW(ram + 0x0290), obW(ram + 0x0292),
                                      obW(ram + 0x0100), obW(ram + 0x02AF), ram[0x1BB7], obW(ram + 0x029A), obW(ram + 0x029C), ram[0x029E], ram[0x1BF0],
                                      obW(ram + 0x02A6), obW(ram + 0x02A8), obW(ram + 0x02AA), obW(ram + 0x02AC), ram[0x0280], obW(ram + 0x0283), obW(ram + 0x0285), obW(ram + 0x0287), obW(ram + 0x0289), obW(ram + 0x028B), obW(ram + 0x028D),
                                      ram[0x1C], ram[0x1D], ram[0x1F], ram[0x1B48]);
      for (int b = OWOBJ_BASE; b < OWOBJ_END && n < 380; b += OWOBJ_STRIDE) {
        uint16_t k = obW(ram + b);
        if (k == 0x3404 || k == 0x42DD || k == 0x495E || k == 0x309E || k == 0x3AE2 || k == 0x4CC5 || k == 0x504B || k == 0x5097 || k == 0x5494 || k == 0x666C)
          n += snprintf(buf + n, sizeof buf - (size_t)n, " %04X:%04X/%02X", b, k, ram[b + 2]);
      }
      if (strcmp(buf, last)) { strcpy(last, buf); printf("owveh: pass %d f%d %s\n", pass, g_owTraceFrame, buf); }
    } }
}
void CtWorldTraceEnd(const uint8_t* ram) { if (ram) owVehTrace(ram, 0); }
void CtWorldAmbientFrame(uint8_t* ram, CtWorldCam* c, CtWorldStream* st) {
  // C2/02C2-02D1, in the NMI that precedes every object pass: `LDA $7E8A25 / STA $7E8A43 / LDA $7E8A27 / STA $7E8A46`
  // (16-bit) -- the window object's HDMA channel 2 (op 4E -> $C6:E241) reads its two indirect table entries from
  // $7E:8A42, and the entries' pointers are refreshed from $8A25/$8A27, where $C6:E28F/E2A2 leave the half C3/01E4
  // wrote LAST frame. So a frame's window comes from the previous call's table (field.c owWin1Line reads the entries).
  // ...and what the NMI pushes to the PPU before the pass: HDMAEN from $4A (C2/02xx), WH0/WH1 from $3B/$3C (C2/025F-0266). A
  // change the pass makes to them (E241's `TSB $4A` #04) reaches the registers at the NEXT NMI: the frame in between
  // draws with these (measured: the install's frame shows W1 = [1, 0], the shadows before the call).
  // A pass the last frame's inline JSL suspended (obNative) RESUMES here: the NMI that runs it is the one that began
  // then, so none of its front half runs again -- the dispatch goes on from the suspended block.
  int b0 = OWOBJ_BASE;
  if (g_owResumeBlock >= 0) { b0 = g_owResumeBlock; g_owResumeBlock = -1; }
  else {
  g_owNmiEn = ram[0x004A]; g_owNmiWh0 = ram[0x003B]; g_owNmiWh1 = ram[0x003C];
  ram[0x8A36] = ram[0x8A21]; ram[0x8A37] = ram[0x8A22];        // C2/02B2-02DE: channel 1's two entries,
  ram[0x8A39] = ram[0x8A23]; ram[0x8A3A] = ram[0x8A24];        //   channel 2's (below) and channel 3's
  ram[0x8A43] = ram[0x8A25]; ram[0x8A44] = ram[0x8A26];
  ram[0x8A46] = ram[0x8A27]; ram[0x8A47] = ram[0x8A28];
  ram[0x8A50] = ram[0x8A29]; ram[0x8A51] = ram[0x8A2A];
  ram[0x8A53] = ram[0x8A2B]; ram[0x8A54] = ram[0x8A2C];
  }

  g_obStream = st;
  // ⚠ The list is NOT reset here any more: C2/032B `JSR C2/0C4D` empties it before the WHOLE object pass, and the
  // lead block is part of that pass -- its op-4 enqueue (CtWorldLeadAnimFrame) runs before this function in the
  // host, so a reset here threw the lead off the list. The caller resets (CtWorldDrawListReset) before both.
  for (int b = b0; b < OWOBJ_END; b += OWOBJ_STRIDE) {
    uint8_t* B = ram + b;
    // C2/0535: the class word at +0 IS the handler address, and the dispatcher `JMP ($0050)`s to it.
    // Dispatching on it is the ROM's own mechanism, not a slot lookup -- the music object is simply
    // whichever block carries $2F94 (block 14 on world 0). C2/0543-054E owns what happens after: a
    // handler returning CLC gets its 16-bit frame counter at +3 bumped, one returning SEC is KILLED.
    uint16_t cls = obW(B);
    if (cls) { ram[0x4E] = (uint8_t)b; ram[0x4F] = (uint8_t)(b >> 8); }   // C2/053C STX $4E (the live block pointer)
    if (cls == 0x2F94) {
      if (CtWorldMusicObj(ram, b)) { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      else obPutW(B, 0);
      continue;
    }
    if (cls == 0x7849) {                            // the parallax nudge (obClass7849), C2/0543-054E's tail
      if (obClass7849(ram, B, c) == 0) { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      else obPutW(B, 0);
      continue;
    }
    if (cls == 0x21F8 || cls == 0x2194) {           // the mosaic objects (ops 0x2A/0x2B), same tail
      if (obMosaic(ram, B, cls) == 0) { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      else obPutW(B, 0);
      continue;
    }
    if (cls == 0x20A2 || cls == 0x2105) {           // the fade objects (ops 0x28/0x29), same tail
      if (obFade(ram, B, cls) == 0) { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      else obPutW(B, 0);
      continue;
    }
    // THE LEAD'S SLOT: C2/3725 `LDA $001C,X / STA $1BF1 / LDA $0020,X / STA $1BF3` -- its velocity, stored on the frames
    // its mover integrates. The mover itself is the host's (CtWorldStreamFrame held the delta); the store lands here.
    if (cls == 0x3404 && c && c->leadStore) { c->pendDx = c->leadDx; c->pendDy = c->leadDy; c->leadStore = 0; }
    // ...EXCEPT IN A VEHICLE'S MODULES ($027E != 0): there the lead's handler is the ROM's own (obLeadModule), not the host's
    // mover, and it runs here in its slot like any block.
    if (cls == 0x3404 && (ram[0x027E] != 0 || ram[0x0280] != 0 || ram[0x0281] != 0)) {
      obLeadModule(ram, B, c);
      int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8);
      continue;
    }
    if (cls == 0x666C || cls == 0x67FA || cls == 0x683B || cls == 0x68A3) {   // the world map's controller and markers
      int r = cls == 0x666C ? obMapCtl(ram, B) : obMapMarker(ram, B, cls);
      if (r == 0) { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      else obPutW(B, 0);
      continue;
    }
    if (cls == 0x4CC5 || cls == 0x504B || cls == 0x5097 || cls == 0x5494) {   // vehicle B: the Dactyls and their shadows
      int r = cls == 0x4CC5 ? obDactyl(ram, B, c) : cls == 0x504B ? obDactylShadow(ram, B)
            : cls == 0x5097 ? obDactylMate(ram, B) : obDactylMateShadow(ram, B);
      if (r == 0) { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      else obPutW(B, 0);
      continue;
    }
    if (cls == 0x6AC8 || cls == 0x6F85 || cls == 0x7143) {      // the time gauge's controller, swirl, era markers
      int r = cls == 0x6AC8 ? obGaugeCtl(ram, B) : cls == 0x6F85 ? obGaugeSwirl(ram, B) : obGaugeMarker(ram, B);
      { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      if (r == 2 && CtWorldGaugeTakeSound() && g_owJsl) {      // C2/6BE9 JSL C7/0004 inside the pass (as C2/2F49)
        int n = g_owJsl(ram);
        if (n > 0) { g_owSuspendNmis = n; g_owResumeBlock = b + OWOBJ_STRIDE; return; }
      }
      continue;
    }
    if (cls == 0x309E || cls == 0x42DD || cls == 0x495E) {      // the boarding manager, the Epoch, its shadow (CLC / SEC)
      int r = cls == 0x309E ? obBoardManager(ram) : cls == 0x42DD ? obEpoch(ram, B, c) : obEpochShadow(ram, B);
      if (r == 0) { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      else obPutW(B, 0);
      continue;
    }
    // THE BG3 OBJECTS, AT THEIR OWN SLOTS: the drift (C2/75C3 / 75FD / 7702 / 77F2) and, after it in block order, the
    // per-line wave (C2/7759 / 7652) that reads the moved `$24`. All return CLC. They were ticked outside the pass, before
    // it -- so on the frame the bootstrap spawns the drift object (op $35, same pass, a later block) the cartridge already
    // stepped its +22 and the port did not: world 0's clouds ran one frame behind for the whole of every entry (the ferry
    // from the Truce pay, saves/ferry_re: every fourth frame one px apart).
    if (cls == 0x75C3 || cls == 0x75FD || cls == 0x7702 || cls == 0x77F2) {
      if (c) owBg3Drift(c, ram, cls, B + 0x22);
      int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8);
      continue;
    }
    if (cls == 0x7759 || cls == 0x7652) {
      if (c) owBg3WaveTick(c, ram);
      int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8);
      continue;
    }
    if (cls == 0x1DD4 || cls == 0x1E38) {           // the palette row (op 0x04) and its fade, same tail
      if ((cls == 0x1DD4 ? obPalRow(ram, B) : obRowFade(ram, B)) == 0) {
        int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8);
      } else obPutW(B, 0);
      continue;
    }
    if (cls == 0x1CF5) {                            // the VRAM upload (op 0x03), same tail
      if (obVramUp(ram, B) == 0) { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      else obPutW(B, 0);
      continue;
    }
    if (cls == OWOBJ_CLASS_ENTRYSCAN) {             // the scanner (obEntryScan: entrances, event cells), the same CLC tail
      obEntryScan(ram, B); { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      continue;
    }
    if (cls == OWOBJ_CLASS_NAMEBANNER) {            // the place-name banner (obNameBanner), the same CLC tail
      obNameBanner(ram, B); { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      continue;
    }
    if (cls == 0x3AE2) {                            // a party follower (obFollower), the same CLC tail
      if (obFollower(ram, B, c) == 0) { int v = (B[3] | (B[4] << 8)) + 1; B[3] = (uint8_t)v; B[4] = (uint8_t)(v >> 8); }
      continue;
    }
    if (cls != OWOBJ_CLASS_VM) continue;            // the other native classes are still not run
    if (obRun(ram, B, c) == 2) { g_owResumeBlock = b; return; }   // its inline JSL holds the pass (obNative)
  }
  owVehTrace(ram, 1);
}

void CtWorldEnter(CtWorldCam* c, CtWorldMover* m, int cell8x, int cell8y) {
  m->posX = (uint16_t)(cell8x * 8);
  m->posY = (uint16_t)(cell8y * 8);
  m->velX = m->velY = 0;
  m->probeX = m->posX;
  m->probeY = m->posY;
  m->phase = 1;
  m->moving = 0;
  int camX = m->posX - 128; if (camX < 0) camX += 0x600;
  int camY = m->posY - 112; if (camY < 0) camY += 0x400;
  c->bg1h = c->bg2h = (uint16_t)camX;
  c->bg1v = c->bg2v = (uint16_t)camY;
  c->pendDx = c->pendDy = 0;
}

uint16_t CtWorldEntryScan(const uint8_t* ram, int posX, int posY) {
  int cell = (((posY >> 4) & 0xFF) << 8) | ((posX >> 4) & 0xFF);   // C2/3286: (row<<8)|col
  int n = ram[0x1B38];
  int x = 0;
  for (int i = 0; i < n; i++, x += 7) {                            // C2/32CD scan
    int w = ram[0x7E00 + x] | (ram[0x7E01 + x] << 8);
    if ((w & 0x80) && (w & 0x3F7F) == cell) return (uint16_t)x;
  }
  return 0xFFFF;
}

// THE SCANNER, class $3264 (C2/3264). `$0281 = $0280`, then by `$0280` (C2:3275):
//   0 (3279): on a cell the lead has just reached (`$0282` bit 0, set with bit 1 by C2/34F8 and C2/3766 when C2/39AE finds
//     bit 2 on the cell's top pair), its 16 x 16 cell into the three tables (C2/32CD: the first record armed -- bit 7 -- whose
//     cell word & 3F7F is it; `$1B32` / `$1B34` / `$1B36`, FFFF for none) and C2/3349 acts on what it found; off such a cell
//     (bit 1 clear) `$1B58 = 0` and the three FFFF.
//   1 (32B7): an event is running -- once its script sets `$1B43`, `$0280 = 0` and the three FFFF.
static void owScanTable(uint8_t* ram, uint16_t base, uint8_t count, unsigned rec, uint16_t cell, uint16_t out) {
  unsigned y = count, x = 0;                                           // 32CD LDA $1B38 / AND #00FF / TAY / LDX #0000
  do {                                                                 // (`DEY / BNE`: a count of 0 is 65536 records)
    unsigned w = obW(ram + ((base + x) & 0xFFFF));
    if ((w & 0x0080) && (w & 0x3F7F) == cell) { obPutW(ram + out, (uint16_t)x); return; }
    x = (x + rec) & 0xFFFF; y = (y - 1) & 0xFFFF;
  } while (y);
  obPutW(ram + out, 0xFFFF);
}
static void owScanName(uint8_t* ram) { ram[0x1B58] = ram[(0x7E02 + obW(ram + 0x1B32)) & 0xFFFF]; }   // C2/6263
// C2/3349: `$1B44` = what the scan found (bit 0 an entrance, bit 1 a transition cell, bit 2 an event cell), and C2:337E by it.
static void owScanAct(uint8_t* ram, uint8_t* B) {
  ram[0x1B58] = 0;                                                     // 334B STZ $1B58
  uint8_t f = 0;
  if (!(ram[0x1B33] & 0x80)) f |= 1;
  if (!(ram[0x1B37] & 0x80)) f |= 2;
  if (!(ram[0x1B35] & 0x80)) f |= 4;
  ram[0x00] = f; ram[0x1B44] = f;
  if (f == 1 || f == 3) { B[2] = 1; owScanName(ram); return; }         // 338F: an entrance -- its name for the banner
  if (f == 2) {                                                        // 339A: a transition cell -- C2/6286, module 4
    B[2] = 1;
    ram[0x1B47] = ram[(0x8183 + obW(ram + 0x1B36)) & 0xFFFF];
    ram[0x027C] = 0x04;
    { static int said = 0; if (!said) { said = 1; fprintf(stderr, "owscan: UNDERIVED a transition cell: `$027C = 4` (C2/250D) is not ported\n"); } }
    return;
  }
  if (f >= 4) {                                                        // 33AA / 33D4: an event cell
    if (f & 1) owScanName(ram);                                        // 33D4 JSR C2/6263
    unsigned i = ((unsigned)ram[(0x8302 + obW(ram + 0x1B34)) & 0xFFFF] << 1) & 0xFF;   // C2/626E (ASL with M 8)
    ram[0x1B45] = ram[0x8480 + i]; ram[0x1B46] = ram[0x8481 + i];
    CtWorldSpawnVm(ram, 0x7F, obW(ram + 0x1B45));                      // 33AD LDA #7F / LDX $1B45 / JSR C2/04D9
    obPutW(ram + 0x1B3B, 0); obPutW(ram + 0x1B3D, 0); obPutW(ram + 0x1B3F, 0);
    ram[(0x8300 + obW(ram + 0x1B34)) & 0xFFFF] &= 0x7F;               // 33C1-33CA: once
    ram[0x0280] = 0x01;
    if (f & 1) ram[0x1B43] = 0;                                        // 3400 STZ $1B43 (33D4's arm only)
  }
}
static int obEntryScan(uint8_t* ram, uint8_t* B) {
  ram[0x0281] = ram[0x0280];                                           // 3266
  if (ram[0x0280] == 0) {
    if (ram[0x0282] & 0x01) {                                          // 3279-329C
      ram[0x0282] &= (uint8_t)~0x01;
      obPutW(ram + 0x00, (uint16_t)(obW(ram + 0x0283) >> 4));          // 3287-3297: `STA $00` then `STA $01` (16 bits each)
      obPutW(ram + 0x01, (uint16_t)(obW(ram + 0x0285) >> 4));
      uint16_t cell = obW(ram + 0x00);
      owScanTable(ram, 0x7E00, ram[0x1B38], 7, cell, 0x1B32);
      owScanTable(ram, 0x8300, ram[0x1B39], 3, cell, 0x1B34);
      owScanTable(ram, 0x8180, ram[0x1B3A], 3, cell, 0x1B36);
      owScanAct(ram, B);
    }
    if (!(ram[0x0282] & 0x02)) {                                       // 329F-32B2
      ram[0x1B58] = 0;
      obPutW(ram + 0x1B32, 0xFFFF); obPutW(ram + 0x1B34, 0xFFFF); obPutW(ram + 0x1B36, 0xFFFF);
    }
  } else if (ram[0x0280] == 1) {
    if (ram[0x1B43]) {                                                 // 32B7-32C8
      ram[0x0280] = 0;
      obPutW(ram + 0x1B32, 0xFFFF); obPutW(ram + 0x1B34, 0xFFFF); obPutW(ram + 0x1B36, 0xFFFF);
    }
  } else {
    static int said = 0;
    if (!said) { said = 1; fprintf(stderr, "owscan: UNDERIVED `$0280 = %02X` -- past C2:3275's two entries\n", ram[0x0280]); }
  }
  return 0;
}
// THE PLACE-NAME BANNER, class $5628 (C2/5628, states C2:5637 = 563F / 5663 / 5699 / 56F9; every one falls into the next and
// every exit is CLC). It writes the name `$1B58` (the scanner's, C2/6263) one glyph a frame into the strip `$7E:8621` with the
// proportional-text engine (vwf.c), queues the strip's two rows to VRAM `$1E00` / `$1F00` (OBJ tiles $1E0-$1EB / $1F0-$1FB) and
// then shows itself every frame through the anim sub-VM: its script is `04 8604 00` at `$7E:8600` (C2/5775), the frame
// `$7E:8604` six 16 x 16 pieces 16 px apart, 48 px above the block. disasm/notes/vwf_engine.md sec. 5.
static void owBannerClear(uint8_t* ram) { memset(ram + 0x8621, 0, 0x400); }   // C2/5798: `STA $8621` + MVN $3FE -> $8621-$8A20
static int obNameBanner(uint8_t* ram, uint8_t* B) {
  switch (B[2]) {
  case 0:                                                               // 563F
    B[2]++; B[OWOBJ_ATTR] = 0xB0; B[0x10] = 0; B[0x11] = 0;
    if (g_owRom) {                                                      // C2/5775: C2:57C2 -> $7E:8600 (4), C2:57C6 -> $8604 (MVN $18)
      memcpy(ram + 0x8600, g_owRom + 0x257C2, 4);
      memcpy(ram + 0x8604, g_owRom + 0x257C6, 0x19);
    }
    obPutW(B + OWOBJ_ANIM, 0x8600); B[OWOBJ_ANIMBK] = 0x7E;             // 5656-5660
    /* fall through */
  case 1:                                                               // 5663
    if (!ram[0x1B58]) return 0;
    B[2]++; B[OWOBJ_DWELL] = 0;                                         // 566C INC / 566F STZ $000E,X
    ram[0x020C] = ram[0x1B58]; obPutW(ram + 0x020D, 0xF400); ram[0x020F] = 0xC6;
    ram[0x0214] = 0x02; obPutW(ram + 0x0210, 0x8621); ram[0x0212] = 0x7E;
    if (g_owRom) CtVwfBegin(ram, g_owRom, g_owRomLen);                  // 5695 JSL C2/57DF
    /* fall through */
  case 2:                                                               // 5699
    ram[0x0213] = 0x01;
    if (g_owRom) CtVwfRun(ram, g_owRom, g_owRomLen);                    // 569E JSL C2/5823: one glyph
    if (ram[0x0215] == 0x10) return 0;
    B[2]++;
    { uint8_t x = ram[0x5F];                                            // 56B2-56F4: two entries on the NMI's queue
      ram[0x5E]++;
      ram[0x62 + x] = 0x7E; ram[0x6A + x] = 0x7E; ram[0x67 + x] = 0x80; ram[0x6F + x] = 0x80;
      obPutW(ram + 0x60 + x, 0x8621); obPutW(ram + 0x68 + x, 0x8821);
      obPutW(ram + 0x63 + x, 0x1E00); obPutW(ram + 0x6B + x, 0x1F00);
      obPutW(ram + 0x65 + x, 0x0180); obPutW(ram + 0x6D + x, 0x0180);
      ram[0x5F] = (uint8_t)(x + 0x10); ram[0x5E] = 0; }
    /* fall through */
  default:                                                              // 56F9
    if (!ram[0x1B58]) { B[2] = 1; owBannerClear(ram); }                // 5700: the name is gone -- still drawn this frame
    else if (ram[0x1B59]) { B[2] = 1; ram[0x1B59] = 0; owBannerClear(ram); return 0; }
    if (ram[0x0294] & 0x10) return 0;                                   // 571E
    if (ram[0x027E] == 2) { obPutW(B + OWOBJ_XPOS, obW(ram + 0x0290)); obPutW(B + OWOBJ_YPOS, (uint16_t)(obW(ram + 0x0292) + 0x40)); }
    else { obPutW(B + OWOBJ_XPOS, obW(ram + 0x0283)); obPutW(B + OWOBJ_YPOS, obW(ram + 0x0285)); }
    ram[0x8604] = 0x06;                                                 // 5754
    obPutW(B + OWOBJ_XPOS, (uint16_t)(obW(B + OWOBJ_XPOS) - (ram[0x0234] >> 1)));   // 575C-576D: x less half the pen
    obAnimTick(ram, B);                                                 // 5770 JSR C2/0E1D
    return 0;
  }
}
// C2/2A44 -- the world's trigger tables: the packet C6:FFC0[header +15] decompressed into `$7F:0400` holds, each list a count
// byte and its records, the entrances ($7E:7E00, 7 bytes, `$1B38`), the event cells ($7E:8300, 3 bytes: the cell word -- bit 7
// of its low byte arms it -- and the script's index, `$1B39`), the transition cells ($7E:8180, 3 bytes, `$1B3A`) and the event
// scripts' pointers ($7E:8480, words). The entrances come with the world's own blob (worldInit); the rest is read here. The ROM
// copies each list with `DEY / BNE`, so a count of 0 would copy 65536 records: no world has one (all eight read 1 or more).
void CtWorldLoadTriggers(uint8_t* ram, int world) {
  static uint8_t pk[0x10000];
  if (!g_owRom || world < 0 || world > 7) return;
  long hp = 0x060000L + 0xFD00 + 2 * world;
  unsigned hdr = (unsigned)(g_owRom[hp] | (g_owRom[hp + 1] << 8));
  uint8_t idx = g_owRom[0x060000L + hdr + 0x15];
  if (idx & 0x80) return;                                              // 2A55 BMI: no packet
  long e = 0x060000L + 0xFFC0 + 3L * idx;
  uint32_t src = (uint32_t)(g_owRom[e] | (g_owRom[e + 1] << 8) | (g_owRom[e + 2] << 16));
  memset(pk, 0, sizeof pk);
  if (!ct_lz_decompress_bounded(g_owRom, (size_t)g_owRomLen, pk, 0xFC00u, ct_lz_hirom_offset(src))) return;
  unsigned x = 1u + 7u * (pk[0] ? pk[0] : 0x10000u);                  // past the entrances (2A8B-2AC7)
  static const struct { uint16_t at, cnt; uint8_t rec; } L[3] = { { 0x8300, 0x1B39, 3 }, { 0x8180, 0x1B3A, 3 }, { 0x8480, 0, 2 } };
  for (int k = 0; k < 3; k++) {
    unsigned n = pk[x & 0xFFFF]; x++;
    if (L[k].cnt) ram[L[k].cnt] = (uint8_t)n;
    unsigned cnt = n ? n : 0x10000u, len = cnt * L[k].rec;
    for (unsigned i = 0; i < len; i++) ram[(L[k].at + i) & 0xFFFF] = pk[(x + i) & 0xFFFF];   // via $2180, bank $7E
    x += len;
  }
}
int CtWorldEntryDecode(const uint8_t* ram, uint16_t off, CtWorldEntry* out) {
  if (off == 0xFFFF) return 0;
  const uint8_t* r = ram + 0x7E00 + off;
  uint16_t w = (uint16_t)(r[3] | (r[4] << 8));
  out->destLoc = w & 0x1FF;
  out->flags = (uint8_t)(w >> 9);
  out->destX = r[5];
  out->destY = r[6];
  out->nameId = r[2];
  return 1;
}

// C2/0405 -- the NMI's VRAM QUEUE: `LDA $5E / BEQ / RTS` (an object is writing it), `LDA $5F / BNE / RTS` (empty), then per
// 8-byte entry at $60+X: DMA channel 0, `$4300 = 01` (two registers, $2118/$2119), `$4302 = $60,X` (source), `$4304 = $62,X`
// (bank), `$4305 = $65,X` (bytes), `$2116 = $63,X` (VRAM word), `$2115 = $67,X` (VMAIN) and `$420B = 01`; X += 8 until X ==
// $5F; `STZ $5F`. `vram` is the world's BYTE image (word w at byte 2w). The port's tilemap streaming keeps its own queue
// (CtWorldStream.pend); this is the rest of the ROM's, the anim op 6 entries.
void CtWorldNmiVramQueue(uint8_t* ram, uint8_t* vram) {
  if (!ram || !vram || ram[0x5E] || !ram[0x5F]) return;
  uint8_t n = ram[0x5F], x = 0;
  do {
    uint16_t src = (uint16_t)(ram[(uint8_t)(0x60 + x)] | (ram[(uint8_t)(0x61 + x)] << 8));
    uint8_t bank = ram[(uint8_t)(0x62 + x)];
    uint16_t w = (uint16_t)(ram[(uint8_t)(0x63 + x)] | (ram[(uint8_t)(0x64 + x)] << 8));
    unsigned len = (unsigned)(ram[(uint8_t)(0x65 + x)] | (ram[(uint8_t)(0x66 + x)] << 8));
    uint8_t vmain = ram[(uint8_t)(0x67 + x)];
    unsigned inc = (vmain & 3) == 0 ? 1 : (vmain & 3) == 1 ? 32 : 128;
    if (!len) len = 0x10000;                                         // a DMA count of 0 is 65536
    for (unsigned i = 0; i < len; i++) {
      uint16_t a = (uint16_t)(src + i);                              // the A-bus address wraps inside its bank
      uint8_t v = 0;
      if (bank == 0x7E || bank == 0x7F) v = ram[((bank & 1) << 16) | a];
      else if ((bank & 0x40) && g_owRom) { long o = ((long)(bank & 0x3F) << 16) | a; if (o < g_owRomLen) v = g_owRom[o]; }
      else if (a < 0x2000) v = ram[a];                               // the low-RAM mirror
      unsigned word = (w + (i >> 1) * inc) & 0x7FFF;                 // VMAIN $80: the word steps after the high byte
      vram[word * 2 + (i & 1)] = v;
    }
    x = (uint8_t)(x + 8);
  } while (x != n);
  ram[0x5F] = 0;                                                     // 0446 STZ $5F
  ram[0xE0] = 0x00; ram[0xE1] = 0xF0; ram[0xE2] = 0x7E;              // 044A-0451 LDX #F000 / STX $E0 / LDA #7E / STA $E2
}
void CtWorldStreamFrame(CtWorldStream* st, CtWorldCam* c, uint8_t* ram, int dx, int dy) {
  // The NMI at the start of THIS frame flushes the queue from LAST frame (staging is untouched
  // in between -- the $E0 reset happens after the flush).
  for (int q = 0; q < st->nPend; q++) {
    int bg = st->pend[q].bg, so = st->pend[q].stage, a = st->pend[q].addr;
    if (!st->pend[q].isRow) {
      for (int j = 0; j < 32; j++)
        st->tilemap[bg][a + j * 0x20] =
            (uint16_t)(st->staging[so + j * 2] | (st->staging[so + j * 2 + 1] << 8));
    } else {
      for (int j = 0; j < 32; j++) {
        st->tilemap[bg][a + j] =
            (uint16_t)(st->staging[so + j * 2] | (st->staging[so + j * 2 + 1] << 8));
        st->tilemap[bg][a + 0x400 + j] =
            (uint16_t)(st->staging[so + 0x40 + j * 2] | (st->staging[so + 0x40 + j * 2 + 1] << 8));
      }
    }
  }
  st->nPend = 0;
  st->stageOff = 0;                                // $E0 := $F000 (reset every NMI)
  // The camera apply (C2/36F7) is the lead's MOVING path -- C2/36E7 `INC $0002,X`, the integrate C2/0F38 and then the
  // four dispatcher calls, a zero component included; an idle lead returns before it (C2/36E5). The port's mover
  // hands a delta exactly on those frames, so the dispatchers' words are written then and only then.
  int apply = dx || dy;
  if (apply) owDispatchScratch(ram, 0, dx);
  owStepH(st, c, ram, 0, dx);                      // the camera apply order (C2/36F7): H BG1, H BG2,
  if (apply) owDispatchScratch(ram, 1, dx);
  owStepH(st, c, ram, 1, dx);                      // then V BG1, V BG2 -- each a C2/0568 / C2/066C call
  if (apply) owDispatchScratch(ram, 0, dy);
  owStepV(st, c, ram, 0, dy);
  if (apply) owDispatchScratch(ram, 1, dy);
  owStepV(st, c, ram, 1, dy);
  // THE LEAD'S DELTA WAITS FOR THE LEAD'S SLOT. The host mover moved the camera above, but on the cartridge the lead is
  // a block of the object pass and stores its velocity into $1BF1/$1BF3 there (C2/3725); the drift object and the
  // per-line wave are blocks of the same pass. CtWorldAmbientFrame runs all three in block order.
  c->leadDx = dx; c->leadDy = dy; c->leadStore = (dx || dy);
}
