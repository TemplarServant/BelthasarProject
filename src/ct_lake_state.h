// ct_lake_state.h — wrapped LakeSnes oracle savestate (CTLS).
//
// On-disk formats accepted by the harness:
//
//   1) CTLS (preferred, written by every new save path):
//        magic[4]      = 'C','T','L','S'
//        version u32LE = 1
//        length  u32LE = payload bytes
//        crc32   u32LE = IEEE CRC-32 of the payload
//        payload[...]  = raw snes_handleState stream (same as legacy)
//
//   2) Legacy (all existing pinned .state files): the bare handleState stream
//      with no header. Detected by size + the 11 leading 0/1 bool bytes.
//      Load still works (with a one-line notice) so the regression suite and
//      RE captures keep running without a mass re-encode.
//
// A truncated, foreign, or CRC-mismatched CTLS file FAILS loudly — it never
// feeds silent zeros into snes_handleState. Legacy files that fail the sniff
// also FAIL (instead of loading as an all-zero machine).
//
// NOTE: this is NOT the LakeSnes upstream LSSF format (snes_saveState) and NOT
// Belthasar's CTFS CtScene snapshot. Payload layout is unchanged from the
// historical ct.exe raw stream.
#ifndef CT_LAKE_STATE_H
#define CT_LAKE_STATE_H

#include <stdint.h>

#define CT_LAKE_MAGIC0     'C'
#define CT_LAKE_MAGIC1     'T'
#define CT_LAKE_MAGIC2     'L'
#define CT_LAKE_MAGIC3     'S'
#define CT_LAKE_VERSION    1
#define CT_LAKE_HDR_SIZE   16
/* Minimum raw handleState size: 69-byte snes header + 0x20000 WRAM + 29-byte CPU. */
#define CT_LAKE_MIN_PAYLOAD  (69 + 0x20000 + 29)

enum {
  CT_LAKE_FMT_LEGACY  = 0,
  CT_LAKE_FMT_WRAPPED = 1
};

/* IEEE CRC-32 (poly 0xEDB88320), init/xor 0xFFFFFFFF — matches zlib/Python binascii.crc32. */
uint32_t CtLakeCrc32(const uint8_t* data, int len);

/* Parse a file image into a malloc'd payload (caller frees *out).
   Returns 0 on success; nonzero on hard failure (reason printed to stderr).
   *fmtOut (nullable) receives CT_LAKE_FMT_*. */
int CtLakeStateLoadBytes(const uint8_t* file, int fileLen,
                         uint8_t** out, int* outLen, int* fmtOut);

/* Read path, then CtLakeStateLoadBytes. Returns 0 on success. */
int CtLakeStateLoadPath(const char* path, uint8_t** out, int* outLen, int* fmtOut);

/* Build a CTLS-wrapped file image (malloc; caller frees *outFile). Returns 0. */
int CtLakeStateWrap(const uint8_t* payload, int payloadLen,
                    uint8_t** outFile, int* outFileLen);

/* Write payload as a CTLS-wrapped file. Returns 0 on success. */
int CtLakeStateSavePath(const char* path, const uint8_t* payload, int payloadLen);

#endif
