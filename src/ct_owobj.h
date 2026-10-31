#pragma once
// Overworld ambient-object block layout (handler C2/0F63; opcode map in disasm/notes/ow_objvm.md).
// 64 blocks of 0x40 bytes at $7E:0B30-$1B30 run bytecode from the $7F:0400 blob. These names cover the
// offsets shared by world.c (obRun and the integrate/anim helpers) and field.c (the ambient renderer and
// the CT_OWBOAT_SHOT/CT_OWRIDE_TEST probes). Offsets are into one block B; words are little-endian.
#define OWOBJ_BASE       0x0B30   // first object block ($7E:0B30)
#define OWOBJ_END        0x1B30   // one past the last block (64 x 0x40)
#define OWOBJ_STRIDE     0x40     // block size
#define OWOBJ_CHILD_BASE 0x0C30   // op-0x09 spawn slots start (blocks 4-63; op-0x43 uses blocks
                                  //   0-3).
#define OWOBJ_CLASS_VM   0x0F63   // +0x00 class word: a live script-VM object (handler C2/0F63);
                                  //   0 = free block (op 0x52 die clears it)
#define OWOBJ_CLASS_ENTRYSCAN 0x3264 // native class: the entrance scanner (C2/3264 -> $1B32/$1B34/$1B36, $1B58)
#define OWOBJ_CLASS_NAMEBANNER 0x5628 // native class: the location-name banner (C2/5628, draws $1B58's name)
                                  //   (both spawned by a world's bootstrap on its interactive branch only)
#define OWOBJ_GOSUB      0x05     // word: one-deep gosub return pc (op 0x36 call / 0x37 return)
#define OWOBJ_PC         0x07     // word: script pc (bank $7F)
#define OWOBJ_WAIT       0x0A     // byte: wait/frame counter (ops 0x31/0x32/0x38/0x39)
#define OWOBJ_ANIM       0x0B     // word (+0B/+0C): anim sub-script ptr, set by op 0x30 from
                                  //   C3/E000[animId*2] -- the render-class discriminator
#define OWOBJ_ANIMBK     0x0D     // byte: anim ptr bank (op 0x30 stamps 0xC3)
#define OWOBJ_DWELL      0x0E     // byte: anim per-frame dwell countdown (~8f, obAnimTick)
#define OWOBJ_ATTR       0x0F     // byte: OBJ attribute (0xB4 boat, 0xBA gull, 0xB0|pal*2 fair
                                  //   balloon; 0 = no sprite; bit6 = hold the anim frame)
#define OWOBJ_XFRAC      0x12     // word: X position fraction (16.16 low half)
#define OWOBJ_XPOS       0x14     // word: X position int (world px, torus 0..0x5FF)
#define OWOBJ_YFRAC      0x16     // word: Y position fraction
#define OWOBJ_YPOS       0x18     // word: Y position int (world px, & 0x3FF)
#define OWOBJ_VELXF      0x1A     // word: X velocity fraction   (16.16 velocity: ops 0x2E/0x2F,
#define OWOBJ_VELXI      0x1C     // word: X velocity int          native 0x7598 loads all 8
#define OWOBJ_VELYF      0x1E     // word: Y velocity fraction     bytes from the ROM table)
#define OWOBJ_VELYI      0x20     // word: Y velocity int
#define OWOBJ_OPSTATE    0x32     // word: per-op state latch (the dispatcher zeroes it on every
                                  //   advance; op 0x3F/0x40 uses it as the "aimed" one-shot flag)
#define OWOBJ_FLYTGT     0x34     // word: fly-to target coordinate (ops 0x3F/0x40)
#define OWOBJ_ANIMCTR    0x3E     // byte: free-running anim counter (gull flap / boat bob frame)
