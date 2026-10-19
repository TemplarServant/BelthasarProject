// ============================================================================
// sound/fuzz.c -- `ct.exe --snddrvfuzz <actions> [seed]`: a constructed command stream for the sound driver's
// lockstep (sound/lockstep.h), reaching what no game run does.
//
// The original driver runs on the vendored SPC700 (a CtAudio, its upload from the ROM), the C driver beside it in
// lockstep. A seeded generator then drives the SPC's ports the way the CPU would: the game's own song loads
// (CtAudioLoadSong: every block kind the loader uses), effect starts on the loaded set, random commands with random
// operands (the fades, the modifiers, mono, mute, pause, fast, the echo delay, an S-DSP write, the meter mode),
// cold boots (command FF F0), upload sessions with every block kind (0-6, a block below $1E00, the effect table
// itself), and CONSTRUCTED SONGS: tracks drawn from all 60 sequence events with random operands, the ones that move
// the pointer (loops, their exits, the conditional and the plain jumps, the effect subroutines)
// laid out so that every cycle a track can take contains a note. The CPU's side of the upload protocol here is a
// test harness's, written from the driver's side of it (disasm/notes/audio_spc_driver.md, "The upload session").
// The verdict is the lockstep's report: every tick identical, and what the original ran.
// ============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "sound/lockstep.h"
#include "sound/machine.h"
#include "apu.h"
#include "dsp.h"

static uint32_t g_rs;
static uint32_t rnd(void) { g_rs ^= g_rs << 13; g_rs ^= g_rs >> 17; g_rs ^= g_rs << 5; return g_rs; }
static int rn(int n) { return (int)(rnd() % (uint32_t)n); }
static int validSfx(uint8_t* id);

static CtAudio* g_au;
static Apu* g_apu;
static uint8_t g_wram[0x20000];
static long g_waitMiss;

// CT_SNDDRV_SAVETEST=<n>: every n-th run of the harness's (mid-session, mid-tick, mid-wait alike) the sound machine is
// saved and loaded back in place -- its restore point and the inputs since replayed -- and the lockstep goes on comparing
// it with the original: a savestate that is not exact shows as a mismatch after it.
static long g_saveEvery = -1, g_saveRuns, g_saves, g_saveFails;
static void saveTest(void) {
  SndMachine* m = CtAudioMachine(g_au);
  if (!m || !CtSndLockstepActive(CtAudioApu(g_au))) return;
  uint8_t* b = NULL; long n = 0;
  if (!SndMachineSave(m, &b, &n)) { g_saveFails++; return; }
  if (!SndMachineLoad(m, b, n)) g_saveFails++;
  g_saves++;
  free(b);
}
static void spc(int cycles) {
  CtAudioRunCycles(g_au, cycles);
  if (g_saveEvery < 0) { const char* e = getenv("CT_SNDDRV_SAVETEST"); g_saveEvery = e ? atol(e) : 0; }
  if (g_saveEvery > 0 && ++g_saveRuns % g_saveEvery == 0) saveTest();
}
static void wr(int k, uint8_t v) { CtAudioPortWrite(g_au, k, v); }
static int waitOut(int k, uint8_t v) {   // the driver's echo on out-port k
  for (long i = 0; i < 400000; i += 16) { if (g_apu->outPorts[k] == v) return 1; spc(16); }
  g_waitMiss++;
  return 0;
}
static void frames(int n) { while (n-- > 0) { CtAudioFrame(g_au); if (g_saveEvery > 0 && ++g_saveRuns % g_saveEvery == 0) saveTest(); } }

// Before each action the original must be in its main loop ($0299-$029F, the PC the harness may look at: it is the
// original's side): a session an echo was missed in is ended (its end marker, or the header token it still waits for),
// a refused song's wait released.
static long g_recover;
static int inLoop(void) { uint16_t pc = g_apu->spc->pc; return pc >= 0x0299 && pc <= 0x029F; }
static void settle(void) {
  for (int tries = 0; tries < 64; tries++) {
    for (long i = 0; i < 60000; i += 16) { if (inLoop()) return; spc(16); }
    g_recover++;
    uint16_t pc = g_apu->spc->pc;
    if (pc >= 0x1014 && pc < 0x1029) { wr(1, 0xE0); wr(0, g_apu->ram[0x9B]); }   // the header's wait: its token, the end
    else if (pc >= 0x0D3E && pc < 0x0D42) wr(0, 0x01);                           // a refused song's wait
    else { wr(1, 0xE0); wr(0, (uint8_t)((tries & 1) ? 0xE0 : 0x55)); }           // a block: a token, then the end marker
  }
}

// A command on the ports, the high port first, and its echo.
static int post(uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3) {
  wr(3, p3); wr(2, p2); wr(1, p1); wr(0, p0);
  int ok = waitOut(0, p0);
  spc(64);
  return ok;
}

// --- the upload session (the CPU's side) -----------------------------------------------------------------------------
typedef struct { uint8_t kind; uint16_t dest; const uint8_t* data; int n; } Block;
static void session(uint8_t tok, const Block* b, int nb) {
  if (nb <= 0) { wr(1, 0xE0); wr(0, tok); waitOut(0, tok); spc(64); return; }
  wr(1, b[0].kind); wr(2, (uint8_t)b[0].dest); wr(3, (uint8_t)(b[0].dest >> 8)); wr(0, tok);
  waitOut(0, tok);
  spc(64);
  for (int i = 0; i < nb; i++) {
    int per = (b[i].kind & 7) == 1 ? 1 : (b[i].kind & 7) == 2 ? 2 : 3;
    int units = (b[i].n + per - 1) / per;
    if (units < 1) units = 1;
    for (int u = 0; u < units; u++) {
      for (int k = 0; k < per; k++) { int at = u * per + k; wr(1 + k, at < b[i].n ? b[i].data[at] : 0); }
      tok = (uint8_t)((tok + 1) & 0x7F);
      wr(0, tok);
      waitOut(0, tok);
      spc((b[i].kind & 7) == 5 ? 64 + rn(4000) : 64);   // kind 5: the music's ticks run between the units
    }
    if (i + 1 < nb) { wr(1, b[i + 1].kind); wr(2, (uint8_t)b[i + 1].dest); wr(3, (uint8_t)(b[i + 1].dest >> 8)); }
    else wr(1, 0xE0);
    tok = 0xE0;
    wr(0, tok);
    waitOut(0, tok);
    spc(64);
  }
}

// --- a constructed song --------------------------------------------------------------------------------------------
enum { SONG_AT = 0x2000 };
// The songs' relocation ($00/$01): the effects' own jumps are relative to it too, so a constructed song keeps the one the
// game's songs set (taken after each real load).
static uint16_t g_base;
static uint16_t rel(int at) { return (uint16_t)(SONG_AT + at - g_base); }
static uint8_t g_song[0x800];
static int g_pos;
typedef struct { int at; int target; } Patch;
static Patch g_patch[512];
static int g_npatch;
static int g_label[2048], g_nlabel;

static void put(uint8_t b) { if (g_pos < (int)sizeof g_song - 16) g_song[g_pos++] = b; }
static int newLabel(void) { g_label[g_nlabel] = -1; return g_nlabel++; }
static void putJump(int label) { if (g_npatch < 512) { g_patch[g_npatch].at = g_pos; g_patch[g_npatch].target = label; g_npatch++; } put(0); put(0); }
static void note(void) {
  int p = rn(16);
  int pitch = p < 12 ? p : p < 14 ? 12 : 13;           // mostly notes, some ties and rests
  put((uint8_t)(pitch * 14 + rn(14)));
}
static const uint8_t kPlain[] = { 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3,
  0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8,
  0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF7, 0xF8, 0xF9, 0xFB, 0xFC, 0xFD };
static void plainEvent(void) {
  uint8_t e = kPlain[rn((int)sizeof kPlain)];
  put(e);
  int ops = e == 0xC9 || e == 0xCB ? 3 : (e == 0xC5 || e == 0xC7 || e == 0xC8 || e == 0xCD || e == 0xF1 || e == 0xF3 ||
                                          e == 0xF7 || e == 0xF8) ? 2 :
            (e == 0xCA || e == 0xCC || e == 0xCE || (e >= 0xD0 && e <= 0xD5) || e == 0xD7 || e == 0xD8 || e == 0xE1 || (e >= 0xE4 && e <= 0xE7) ||
             e == 0xFB || e == 0xFC) ? 0 : 1;
  for (int k = 0; k < ops; k++) {
    uint8_t v = (uint8_t)rnd();
    if ((e == 0xF0 || e == 0xF1) && k == ops - 1) v |= 0x40;        // a tempo that moves
    if (e == 0xE8) v = (uint8_t)(1 + rn(24));
    if (e == 0xDC) v = (uint8_t)rn(0x20);
    if (e == 0xD6) v = (uint8_t)(2 + rn(5));
    put(v);
  }
}
static void items(int depth, int n, int inLoop, int loopExit);
static void item(int depth, int inLoop, int loopExit) {
  int r = rn(100);
  if (r < 45) { note(); return; }
  if (r < 80) { plainEvent(); return; }
  if (r < 86 && depth < 2) {   // a loop: E2 count, a body with a note, maybe its F5 exit, E3
    int out = newLabel();
    put(0xE2); put((uint8_t)(rn(8) ? 1 + rn(3) : 0));
    items(depth + 1, 1 + rn(4), 1, out);
    note();
    put(0xE3);
    g_label[out] = g_pos;
    return;
  }
  if (r < 89 && inLoop) { put(0xF5); put((uint8_t)(1 + rn(3))); putJump(loopExit); return; }
  if (r < 93) {   // FA: on command 88's value, past the next item
    int l = newLabel();
    put(0xFA); put((uint8_t)rn(16)); putJump(l);
    note();
    g_label[l] = g_pos;
    return;
  }
  { int l = newLabel(); put(0xF6); putJump(l); plainEvent(); g_label[l] = g_pos; }   // F6: over an event
}
static void items(int depth, int n, int inLoop, int loopExit) { while (n-- > 0) item(depth, inLoop, loopExit); }

static void makeSong(int tracks) {
  g_pos = 0; g_npatch = 0; g_nlabel = 0;
  memset(g_song, 0, sizeof g_song);
  g_pos = 0x24;                                         // the header: base $2024 (relocation 0), the empty marker $0000
  { uint16_t h = (uint16_t)(0x2024 - g_base); g_song[0] = (uint8_t)h; g_song[1] = (uint8_t)(h >> 8); g_song[2] = 0; g_song[3] = 0; }
  uint16_t starts[16];
  for (int t = 0; t < 16; t++) {
    int on = t < 8 ? t < tracks : rn(2) && (t - 8) < tracks;
    if (!on) { starts[t] = 0; continue; }
    starts[t] = rel(g_pos);
    int top = g_pos;
    put(0xF0); put((uint8_t)(0xC0 + rn(0x40)));   // a fast tempo first (a song starts at 1: a step every 256 ticks)
    note();
    items(0, 4 + rn(20), 0, 0);
    note();
    int end = rn(10);
    if (end < 5) { put(0xF6); put((uint8_t)rel(top)); put((uint8_t)(rel(top) >> 8)); }   // back to the top
    else if (end < 7) { uint8_t s; if (validSfx(&s)) { put(rn(2) ? 0xE9 : 0xEA); put(s); } note(); put(0xEB); }        // an effect's track
    else { static const uint8_t ends[] = { 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xFE, 0xFF }; put(ends[rn(7)]); }   // the end events
  }
  for (int i = 0; i < g_npatch; i++) {
    int at = g_label[g_patch[i].target] >= 0 ? g_label[g_patch[i].target] : g_pos;
    uint16_t a = rel(at);
    g_song[g_patch[i].at] = (uint8_t)a; g_song[g_patch[i].at + 1] = (uint8_t)(a >> 8);
  }
  for (int t = 0; t < 16; t++) { g_song[4 + 2 * t] = (uint8_t)starts[t]; g_song[5 + 2 * t] = (uint8_t)(starts[t] >> 8); }
}

// An effect the loaded set has: its table entry's one or two track pointers inside the set ($2F00-$46FF, 6144 bytes).
static int sfxOk(uint8_t s) {
  const uint8_t* t = g_apu->ram + 0x2F00 + s * 4;
  int a = t[0] | t[1] << 8, b = t[2] | t[3] << 8;
  int okA = a >= 0x2F00 && a < 0x4700, okB = b >= 0x2F00 && b < 0x4700;
  return (okA || okB) && (okA || t[1] == 0) && (okB || t[3] == 0);
}
static int validSfx(uint8_t* id) {
  for (int tries = 0; tries < 64; tries++) {
    uint8_t s = (uint8_t)rn(0x40);
    if (sfxOk(s)) { *id = s; return 1; }
  }
  return 0;
}

static uint8_t g_lastSong = 0xFF;
static void realSong(void) {
  int n = CtAudioSongCount(g_au);
  static const uint8_t cmds[] = { 0x10, 0x11, 0x14, 0x15 };
  uint8_t id = (uint8_t)(1 + rn(n - 1));
  { const char* f = getenv("CT_SNDDRV_FUZZ_SONG"); if (f) id = (uint8_t)strtol(f, NULL, 16); }   // (debugging: one song)
  settle();   // (after a boot the driver still waits out its echo buffer: the loader posts to the main loop)
  uint8_t c = cmds[rn(4)];
  { const char* f = getenv("CT_SNDDRV_FUZZ_CMD"); if (f) c = (uint8_t)strtol(f, NULL, 16); }
  if (getenv("CT_SNDDRV_FUZZ_TRACE")) fprintf(stderr, "real song %02X cmd %02X (pc $%04X)%c", id, c, g_apu->spc->pc, 10);
  CtAudioLoadSong(g_au, g_wram, id, c);
  g_lastSong = id;
  g_base = (uint16_t)(g_apu->ram[0] | g_apu->ram[1] << 8);
}

// A constructed song started with a raw song command, its upload session carrying the song and other blocks.
static void constructedSong(void) {
  static const uint8_t cmds[] = { 0x10, 0x11, 0x14, 0x15 };
  uint8_t c = cmds[rn(4)];
  uint8_t id = (uint8_t)(0x80 + rn(0x7F));
  if (rn(8) == 0) id = g_lastSong;                     // sometimes the song playing: the refusal
  makeSong(1 + rn(8));
  wr(3, (uint8_t)rn(6)); wr(2, (uint8_t)(0x60 + rn(0xA0))); wr(1, id); wr(0, c);
  if (!waitOut(0, c)) return;
  spc(64);
  if (g_apu->outPorts[1] == 0xFF) { wr(0, 0x01); spc(2000); g_lastSong = id; return; }   // refused: release its wait
  g_lastSong = id;
  Block b[6]; int nb = 0;
  static uint8_t junk[64];
  for (int k = 0; k < 64; k++) junk[k] = (uint8_t)rnd();
  b[nb++] = (Block){ 3, SONG_AT, g_song, (int)sizeof g_song };
  // (the bytes stored go to the sequence area $2800-$2EFF, which every song load rewrites: the game's loader keeps its
  // own account of what lies above it, and a block there behind its back would make its defragmenter move garbage)
  if (rn(2)) b[nb++] = (Block){ (uint8_t)(rn(2) ? 0 : 4 + 2 * rn(2)), (uint16_t)(0x3000 + rn(0x1000)), junk, 1 + rn(64) };
  if (rn(3) == 0) b[nb++] = (Block){ (uint8_t)(1 + rn(3)), (uint16_t)(0x2800 + rn(0x600)), junk, 1 + rn(64) };
  if (rn(4) == 0) b[nb++] = (Block){ (uint8_t)(1 + rn(3)), (uint16_t)(rn(2) ? 0x1D00 + rn(0xE0) : 0x0400 + rn(0x1800)), junk, 1 + rn(32) };   // below $1E00: kept out
  if (rn(4) == 0) b[nb++] = (Block){ 3, 0x2F00, g_apu->ram + 0x2F00, 0x100 };   // the effect table, as it is
  session((uint8_t)((c + 1) & 0x7F), b, nb);
}

// Command FE: a session of its own.
static void uploadCmd(void) {
  if (!post(0xFE, 0, 0, 0)) return;
  static uint8_t junk[96];
  for (int k = 0; k < 96; k++) junk[k] = (uint8_t)rnd();
  Block b[4]; int nb = 0, m = rn(4);
  for (int i = 0; i < m; i++) {
    uint8_t kind = (uint8_t)rn(7);
    uint16_t dest = (uint16_t)(rn(5) == 0 ? (rn(2) ? 0x1D00 + rn(0x100) : 0x0300 + rn(0x1B00)) : 0x2800 + rn(0x600));   // stored: the sequence area only
    if (kind == 0 || kind == 4 || kind == 6) dest = (uint16_t)(0x3000 + rn(0x8000));    // discarded: anywhere
    b[nb++] = (Block){ kind, dest, junk, 1 + rn(96) };
  }
  session(0x7F, b, nb);
}

static void randomCommand(void) {
  static const uint8_t list[] = { 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8E, 0x8F, 0x90, 0xE5, 0xF0, 0xF1,
                                  0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFC, 0xFD, 0xFF, 0x05, 0x12, 0x16, 0x1A, 0x2A };
  uint8_t c = list[rn((int)sizeof list)];
  uint8_t p1 = (uint8_t)rnd(), p2 = (uint8_t)rnd(), p3 = (uint8_t)rnd();
  switch (c) {
  case 0x80: case 0x81: case 0x82: case 0x83: case 0x85: case 0x86: case 0x87: p1 = (uint8_t)(rn(4) ? rn(40) : 0); break;
  case 0x88: p1 = (uint8_t)rn(16); break;
  case 0xF5: case 0xF6: p1 = (uint8_t)(rn(2)); break;
  case 0xFC: p1 = (uint8_t)rn(6); break;
  case 0xFD: {   // an S-DSP write, not to the echo's own registers (the buffer would move under the song)
    static const uint8_t regs[] = { 0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x11, 0x13, 0x25, 0x37, 0x0C, 0x1C, 0x2C, 0x3C,
                                    0x4C, 0x5C, 0x2D, 0x3D, 0x4D, 0x0D, 0x0F, 0x7F, 0x80, 0x95 };
    p1 = regs[rn((int)sizeof regs)];
    break;
  }
  case 0xFF: p1 = 0x01; p2 = (uint8_t)rn(2); break;   // the meter (F0, the reset, is the cold boot's)
  default: break;
  }
  post(c, p1, p2, p3);
  if (c == 0xF6 && p1) { frames(1 + rn(3)); post(0xF6, 0, 0, 0); }   // fast mode, briefly
  if (c == 0xF5 && p1) { frames(1 + rn(20)); post(0xF5, 0, 0, 0); }
  if (c == 0xFF && p2) { frames(1 + rn(30)); post(0xFF, 0x01, 0, 0); }
}

static void coldWram(void) { memset(g_wram, 0, sizeof g_wram); CtAudioSeedMailboxPage(g_au, g_wram); }   // nothing resident

// `ct.exe --snddrvsongs <cmd> <ids...>` (with CT_AUDIO_SPC=lockstep): the game's loader (CtAudioLoadSong) called directly,
// the fuzz's way -- the mailbox page cold and unposted -- each id on a fresh CtAudio; "24+49" loads 24 then 49 on the same
// one (the song change, the defragmenter's case). After each load: the sequence at $2000 byte for byte against its ROM
// block, the heap packed from its base and every resident instrument there byte for byte, the whole song's set resident;
// then 30 frames, and the driver must be in its main loop playing that id ($80). The loader once posted command $00 for song $00 from such a page (it took the
// command and id from the page, not from its arguments): the driver read the stream as commands and upload headers, and
// its kind-7 mover ran over its own code.
int CtSndSongs(const uint8_t* rom, long romLen, uint8_t cmd, int nspec, char** spec) {
  if (!CtSndLockstepWanted()) { fprintf(stderr, "snddrvsongs: needs CT_AUDIO_SPC=lockstep\n"); return 2; }
  long bad = 0, loads = 0;
  for (int i = 0; i < nspec; i++) {
    g_au = CtAudioInit(rom, romLen);
    if (!g_au) { printf("snddrvsongs: FAIL (CtAudioInit)\n"); return 3; }
    g_apu = CtAudioApu(g_au);
    CtAudioSetRuleOff(g_au, 1);
    coldWram();
    frames(30);   // (after a boot the driver still waits out its echo buffer)
    for (const char* p = spec[i]; *p; ) {
      int id = (int)strtol(p, (char**)&p, 16);
      while (*p == '+') p++;
      settle();
      int rc = CtAudioLoadSong(g_au, g_wram, id, cmd);
      loads++;
      const uint8_t* aram = g_apu->ram;
      int n = 0, seqOk = 0, smpOk = 0, smpN = 0, setN = 0, setRes = 0;
      const uint8_t* seq = CtAudioSeqBlock(g_au, id, &n);
      if (seq) seqOk = !memcmp(aram + 0x2000, seq, (size_t)n);
      // the heap packed from its base: each slot's length its block's, its fence (= the next slot's base) base + length
      // (C7/06A2-06BA `STA $40,X / CLC / ADC $60,X / STA $62,X`), so every instrument sits where the ROM's loader puts it
      unsigned pos = CtAudioHeapBase(g_au);
      int packed = 1;
      for (int s = 0; s < 16; s++) {
        unsigned sid = g_wram[0x1E20 + s * 2] | g_wram[0x1E21 + s * 2] << 8;
        if (!sid) break;
        int len = 0; const uint8_t* blk = CtAudioSampleBlock(g_au, (int)sid, &len);
        if (!blk) { packed = 0; break; }
        smpN++;
        unsigned at = g_wram[0x1E60 + s * 2] | g_wram[0x1E61 + s * 2] << 8;
        unsigned ln = g_wram[0x1E40 + s * 2] | g_wram[0x1E41 + s * 2] << 8;
        unsigned fence = g_wram[0x1E62 + s * 2] | g_wram[0x1E63 + s * 2] << 8;
        if (at != pos || ln != (unsigned)len || fence != pos + (unsigned)len) packed = 0;
        if (pos + (unsigned)len <= 0x10000 && !memcmp(aram + pos, blk, (size_t)len)) smpOk++;
        pos += (unsigned)len;
      }
      const uint8_t* set = CtAudioInstrSet(g_au, id);
      for (int k = 0; k < 16 && set; k++) {
        unsigned want = set[k * 2] | set[k * 2 + 1] << 8;
        if (!want) break;
        setN++;
        for (int s = 0; s < 16; s++) if ((unsigned)(g_wram[0x1E20 + s * 2] | g_wram[0x1E21 + s * 2] << 8) == want) { setRes++; break; }
      }
      frames(30);
      int loop = 0;
      for (long c = 0; c < 60000 && !(loop = inLoop()); c += 16) spc(16);   // (a tick may be under way: its main loop within)
      int playing = aram[0x80] == (uint8_t)id;
      int ok = rc == 1 && seqOk && packed && smpOk == smpN && setRes == setN && playing && loop;
      if (!ok) bad++;
      printf("snddrvsongs: cmd %02X song %02X rc=%d seq %s, heap %s, %d/%d instruments exact, %d/%d of its set resident, driver %s song %02X%s\n",
             cmd, id, rc, seqOk ? "exact" : "WRONG", packed ? "packed" : "NOT PACKED", smpOk, smpN, setRes, setN, loop ? "in its loop," : "OUT OF ITS LOOP,",
             aram[0x80], ok ? "" : "  <-- FAIL");
    }
    CtAudioFree(g_au); g_au = NULL;
  }
  printf("snddrvsongs: %ld loads, %ld wrong\n", loads, bad);
  CtSndLockstepReport();
  return (bad || CtSndLockstepMismatches()) ? 1 : 0;
}

static const uint8_t* g_rom; static long g_romLen;
int CtSndFuzz(const uint8_t* rom, long romLen, long actions, uint32_t seed) {
  g_rom = rom; g_romLen = romLen;
  g_rs = seed ? seed : 1u;
  if (!CtSndLockstepWanted()) { fprintf(stderr, "snddrvfuzz: needs CT_AUDIO_SPC=lockstep\n"); return 2; }
  g_au = CtAudioInit(rom, romLen);
  if (!g_au) { printf("snddrvfuzz: FAIL (CtAudioInit)\n"); return 3; }
  g_apu = CtAudioApu(g_au);
  CtAudioSetRuleOff(g_au, 1);
  coldWram();
  realSong();
  frames(30);
  long counts[9] = { 0 };
  for (long i = 0; i < actions; i++) {
    settle();
    int r = rn(100);
    if (getenv("CT_SNDDRV_FUZZ_TRACE")) { fprintf(stderr, "action %ld: %d", i, r); fputc(10, stderr); fflush(stderr); }
    if (r < 30) { randomCommand(); counts[0]++; }
    else if (r < 45) { uint8_t s; if (validSfx(&s)) post(rn(2) ? 0x18 : 0x19, s, (uint8_t)rnd(), 0); counts[1]++; }
    else if (r < 70) { constructedSong(); counts[3]++; frames(60 + rn(240)); }
    else if (r < 76) { uploadCmd(); counts[4]++; }
    else if (r < 82) {   // command FF F0 and the driver uploaded again, then the game's own loads of two songs (the change: the defragmenter)
      // (only here: the game's loader keeps its own account of ARAM, which the constructed blocks would have put out of
      // step -- its defragmenter would then move garbage over the driver)
      long long m; int f;
      int ok = CtAudioColdBoot(g_au, &m, &f); counts[5]++;
      if (getenv("CT_SNDDRV_FUZZ_TRACE")) fprintf(stderr, "cold boot %d (pc $%04X)%c", ok, g_apu->spc->pc, 10);
      frames(10);
      // the next song from a fresh sound system: the port's loader also keeps what is resident in CtAudio itself, which a
      // cold boot of the driver alone leaves stale
      CtAudioFree(g_au);
      g_au = CtAudioInit(g_rom, g_romLen);
      if (!g_au) return 3;
      g_apu = CtAudioApu(g_au);
      CtAudioSetRuleOff(g_au, 1);
      coldWram();
      realSong(); counts[2]++;
      frames(1 + rn(60));
      realSong(); counts[2]++;
    }
    else counts[6]++;
    frames(1 + rn(40));
  }
  printf("snddrvfuzz: seed %u, %ld actions (commands %ld, effects %ld, songs %ld, constructed %ld, sessions %ld, boots %ld), "
         "%ld echo waits missed, %ld recoveries\n", seed, actions, counts[0], counts[1], counts[2], counts[3], counts[4], counts[5],
         g_waitMiss, g_recover);
  if (g_saveEvery > 0) printf("snddrvfuzz: %ld savestates saved and loaded back in place, %ld failed%c", g_saves, g_saveFails, 10);
  CtSndLockstepReport();
  return (CtSndLockstepMismatches() || g_saveFails) ? 1 : 0;
}
