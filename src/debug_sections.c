// debug_sections.c -- see debug_sections.h.
#include "debug_sections.h"
#include <stdio.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int CtDebugDumpWritableSections(const char* path) {
  const uint8_t* base = (const uint8_t*)GetModuleHandleA(NULL);
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  FILE* f = fopen(path, "wb");
  int n = 0;
  if (!f) return 0;
  for (int k = 0; k < nt->FileHeader.NumberOfSections; k++) {
    if (!(sec[k].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
    uint32_t rva = sec[k].VirtualAddress, sz = sec[k].Misc.VirtualSize;
    fwrite(&rva, 4, 1, f); fwrite(&sz, 4, 1, f); fwrite(base + rva, 1, sz, f);
    n++;
  }
  fclose(f);
  return n;
}
#else
int CtDebugDumpWritableSections(const char* path) { (void)path; return 0; }
#endif
