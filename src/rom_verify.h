// rom_verify.h -- the one ROM this project runs: Chrono Trigger (USA), the No-Intro dump.
//
// BelthasarProject ships none of the game. Every program that reads a ROM checks it here first, so the code only ever runs
// the cartridge its user dumped (docs/ROM.md). A 512-byte copier header is accepted and skipped.
#pragma once
#include <stdint.h>

#define CT_ROM_SHA1 "de5822f4f2f7a55acb8926d4c0eaa63d5d989312"
#define CT_ROM_SIZE 0x400000L

// The headerless image inside `data` (`*len` is updated), or NULL after printing why the file is not the supported
// dump. `what` names the file in the message.
const uint8_t* CtRomVerify(const uint8_t* data, long* len, const char* what);

// SHA-1 of `n` bytes as 40 lowercase hex digits + NUL.
void CtSha1Hex(const uint8_t* data, long n, char out[41]);

// The verified ROM image for the whole process, loaded on first use from $CT_ROM, the path given to CtRomSetPath, or
// Chrono_Trigger_USA_SNES.sfc in the working directory. The game's own tables are read from it (never copied into the
// source). Exits with a message when no supported ROM is found: nothing runs without the user's dump.
const uint8_t* CtRomImage(long* len);
void CtRomSetPath(const char* path);
// The image CtRomImage hands out from now on, in place of a dump it would look for (the ROM-free runtime's empty one).
void CtRomSetImage(const uint8_t* img, long len);

// CT_ROM_MEMPATCH="OFF=HEX,OFF|=HH,...": a measuring tool's instrumentation (tools/battle_status_cmp.py), applied in
// memory to an image that has already passed CtRomVerify; the file on disk stays the user's verified dump.
// OFF is a file offset (hex); `=HEX` writes the bytes, `|=HH` ORs one byte. Returns how many bytes it changed.
int CtRomApplyMemPatch(uint8_t* img, long len);
