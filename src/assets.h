// assets.h — loader for ct_assets.dat, the container the build extracts from the
// ROM (assets/extract_assets.py) and the C engine consumes (Python -> C bridge).
//
// Format (little-endian): "CTAS", version u32, entry_count u32,
//   directory[entry_count] = { type u32, id u32, offset u32, length u32 },
//   then asset blobs (offset is absolute from file start).
#ifndef CT_ASSETS_H
#define CT_ASSETS_H

#include <stdint.h>

#define CT_ASSET_SCENE_PALETTE  1   // id = scene palette index; 128 * RGB
#define CT_ASSET_SPRITE_PALETTE 2   // id = sprite palette index; 16 * RGB
#define CT_ASSET_SCENE_HEADER   3   // id = scene index; 14 raw header bytes
#define CT_ASSET_PAL_ANIM       4   // id = palette index; palette-anim descriptor stream
#define CT_ASSET_TILESET12      5   // id = tileset_l12; chips+present+metas+animchips+chipanim
#define CT_ASSET_TILESET3       6   // id = tileset_l3; chips+present+(metas)
#define CT_ASSET_MAP            7   // id = map index; dims+flags+L1/L2/L3 tiles (+256 baked)
#define CT_ASSET_STRINGS        8   // id: 0=dictionary 1=items 2=world 3=locations; string list
#define CT_ASSET_DIALOGUE       9   // id = dialogue table file address; string list
// A string list blob is [count u16][ {u16 byte-len, UTF-8 bytes} x count ].
// --- field assets (a C field engine can run without the ROM) ---
#define CT_ASSET_ACTORS         10  // id = scene; [count u8] {obj u8, sprite u16, x u16, y u16, facing u8}
#define CT_ASSET_COLLISION      11  // id = map; [w u8][h u8] w*h shape bytes, then w*h z bytes
                                    //   (z byte: zplane bits0-1, IGNORE_Z/INV/Z_NEUTRAL/DOOR bits2-5)
#define CT_ASSET_EXITS          12  // id = scene; [count u8] {x,y,w,h u16, kind u8, dest u16, dx,dy i16, facing u8}
#define CT_ASSET_SPRITEANIM     13  // id = sprite; idle then walk: [n u8] {U,D,L,R asm u8, delay u8}
#define CT_ASSET_FONT           14  // id = 0; dialogue 12px glyphs 0..83, raw SNES 2bpp, 24 bytes each
#define CT_ASSET_SPRITE         15  // id = sprite; [size u8][nframes u8][ntiles u16] pal(16*RGB)
                                    //   tiles(ntiles*64) then per frame units*4 chip u16 + units*2 i8
                                    //   offsets. Feed to CtComposeSprite (scene.h).
#define CT_ASSET_SCRIPT         16  // id = script index; decompressed scene-script bytecode
                                    //   ([count u8][count*16 u16 ptrs][body]). Feed to the VM (ct_vm.h).
#define CT_ASSET_WINDOW         19  // id 0: the dialogue window bitmap lifted from the oracle fb:
                                    //   [h u8] + 256*h*3 RGB rows (default grey scheme, 256x85)
#define CT_ASSET_MOVER          20  // id 0: C0/F300 atan(0x400) + C0/F700 facing(0x100) + C0/F800 sine(0x140)
#define CT_ASSET_PORTRAIT       22  // id = char-id 0-6 (Crono..Magus): the 48x48 name-screen portrait,
                                    //   [w u8][h u8] + w*h*3 RGB (ROM gfx $FF:0000 + per-char palette)
#define CT_ASSET_NAMESPRITES    23  // id 0: name-screen grid HAND pointer (16x16; ROM $FF:9CF0).
                                    //   id 1: the name-cursor TRIANGLE (11x12 beveled ▲; ROM $FF:9D70,
                                    //   V-flipped for ▼). Both [w u8][h u8] + w*h*4 RGBA.
#define CT_ASSET_MENUFONT       24  // id 0: the 8x8 menu font, raw 2bpp 256 tiles x 16 B, tile =
                                    //   char code (ROM $FF:8C60); rendered with the menu text palette
#define CT_ASSET_NAMEWIN        25  // id 0: name-screen window background = BG2 rendered in isolation
                                    //   (the real weave + HDMA gradient, byte-exact): [w u16][h u16]+RGB
#define CT_ASSET_NAMEMSG        26  // id 0: "Enter a name..." prompt box (new-game Crono naming), byte-
                                    //   exact: [x0 u16][y0 u16][w u16][h u16] + w*h*3 RGB (overlay)
#define CT_ASSET_WORLDINTROSEED 27  // id = world; the NEW-GAME INTRO object blocks (0x1000 = 64 x 0x40).
                                    //   The BASE CT_ASSET_WORLD seed is the INTERACTIVE snapshot (sparse:
                                    //   no flock+fair spawner master); this is the intro cutscene's seed
                                    //   (with master pc=06CD) that field.c overlays on $0B30 ONLY for the
                                    //   --newgame 496-pan, so the dense flock + Leene fair are intro-only.
// --- the sound (assets/extract_assets.py pack_sound; src/audio.c reads them instead of bank $C7 when present) ---
#define CT_ASSET_SND_BOOT       28  // id = upload block 0-5: [ARAM dest u16] + the block's bytes (0 = the driver itself)
#define CT_ASSET_SND_SEQ        29  // id = song: [addr u16] + its sequence block's bytes (ARAM $2000) + the 3 bytes after
                                    //   them (the upload's last unit reads past the block); addr = the ROM block's size
                                    //   word, low 16 bits (it prices the stream: a bank-crossing block takes the slow loop)
#define CT_ASSET_SND_SAMPLE     30  // id = instrument 1..N: [addr u16] + its BRR sample block's bytes
#define CT_ASSET_SND_INSTR      31  // id = instrument 0..N: [loop u16][$1F40 param u16][$1FC0 param u16] (0 = the
                                    //   empty entry: index -1, the long,X read wrapped into bank $C8)
#define CT_ASSET_SND_SONG       32  // id = song: instrument list (32) + percussion (36, ARAM $F120) + record (2)
#define CT_ASSET_SND_SFXSET     33  // id 0 = set A (field), 1 = set B (battle): the block's bytes
#define CT_ASSET_SND_MISC       34  // id 0: [song count u8] + the song-exception list through its bit-7 end;
                                    // id 1: FD/C1EE's HDMA program for the loader clock: DMAP x8, set A x8, set B x8 (u16)
#define CT_ASSET_SND_TIMING     35  // id 0: the sound machine's cost table (src/sound/timing.h "SNDT": the reachable
                                    //   instructions' cycles and accesses, block 0's code map), made at setup by
                                    //   `ct.exe --sndtiming` (tools/gen/snd_timing.py); block 0 (SND_BOOT 0) then keeps only its data
#define CT_ASSET_LZ             36  // id = an LZ stream's ROM file offset (its size word): the stream DECOMPRESSED, and what
                                    //   C3/0557 spends on it: [out len u32][cycles $7E u32][cycles $7F u32] + the bytes
                                    //   (tools/gen/lz_pack.py: ct.exe --lzpack; src/lz.c serves it for the ROM's stream)
#define CT_ASSET_TABLE          37  // id = a ROM table's start file offset: its bytes (assets/extract_assets.py pack_tables, the
                                    //   extent from the code that reads it); src/romview.h serves reads it covers
#define CT_ASSET_WORLDSPR       18  // id = 0; the overworld player sprite atlas captured from the
                                    //   oracle: [n u8] then {facing,parity,moving,w,h u8, ox,oy i8,
                                    //   w*h*4 RGBA} per frame (keys: facing 0-3 U/D/L/R, step
                                    //   parity, moving flag)
#define CT_ASSET_WORLD          17  // id = world; [fmt u8][world u8][spawnX u16][spawnY u16]
                                    //   grids BG1/BG2 (6144 each), defs BG1/BG2 (2048 each),
                                    //   palette 512, props 512, trigger map 3072, names 16,
                                    //   [nrec u8] records nrec*7, char 28672 (VRAM tiles 512+)

typedef struct { const uint8_t* data; int len; uint32_t version, count; } CtAssets;

// Load the whole file into memory. Returns 1 on success (valid CTAS), else 0.
int CtAssetsOpen(CtAssets* a, const char* path);
// Find asset by (type, id); returns a pointer into the file and sets *outLen, or NULL.
const uint8_t* CtAssetGet(const CtAssets* a, uint32_t type, uint32_t id, uint32_t* outLen);
// Fetch the `index`-th UTF-8 string from a string-list asset (CT_ASSET_STRINGS /
// CT_ASSET_DIALOGUE). Returns a (non-NUL-terminated) pointer + *outLen, or NULL.
const char* CtAssetString(const CtAssets* a, uint32_t type, uint32_t id, int index, int* outLen);
void CtAssetsClose(CtAssets* a);
// 1 when the container is a ROM-free one (it holds the ROM's tables, CT_ASSET_TABLE): the engine then needs no dump at
// all (docs/ROM.md, the ROM-free runtime). CtAssetsFileRomFree opens `path` to ask.
int CtAssetsRomFree(const CtAssets* a);
int CtAssetsFileRomFree(const char* path);

#endif
