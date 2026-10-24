// rom_census.c - see rom_census.h.
#include "rom_census.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#define CENSUS_PAGE 4096
#define CENSUS_MAX_IMAGES 4

typedef struct {
  uint8_t* base;
  long len;
  const char* name;
  uint8_t* seen;            // per page: read yet
  uintptr_t* code;          // per page: the instruction that first read it
} CensusImage;

static CensusImage g_img[CENSUS_MAX_IMAGES];
static int g_nimg;
static uintptr_t g_exeBase;
static char g_exeName[64] = "?";          // the executable's base name: a link map resolves only its own code

static void censusWrite(void) {
  const char* p = getenv("CT_ROM_CENSUS");
  char path[1024];
  if (!p || !*p) return;
  if (strstr(p, "%d")) { snprintf(path, sizeof path, p, (int)
#ifdef _WIN32
    GetCurrentProcessId()
#else
    getpid()
#endif
    ); p = path; }
  FILE* f = fopen(p, "w");
  if (!f) return;
#ifdef _WIN32
  { extern __declspec(dllimport) char* __stdcall GetCommandLineA(void);
    fprintf(f, "# %s\n", GetCommandLineA()); }               // which run (tools/rom_census.py sets apart the harness's)
#endif
  { extern char** environ; char** e = environ;               // ...and its CT_ switches (an oracle or a reference inside)
    for (; e && *e; e++) if (!strncmp(*e, "CT_", 3)) fprintf(f, "# env %s\n", *e); }
  for (int k = 0; k < g_nimg; k++) {
    CensusImage* c = &g_img[k];
    for (long pg = 0; pg * CENSUS_PAGE < c->len; pg++)
      if (c->seen[pg]) fprintf(f, "%06lX %s %llX %s\n", (unsigned long)(pg * CENSUS_PAGE), c->name,
                               (unsigned long long)(c->code[pg] - g_exeBase), g_exeName);
  }
  fclose(f);
}

static int censusTake(uintptr_t addr, uintptr_t pc) {    // 1 = a guarded page, now open
  for (int k = 0; k < g_nimg; k++) {
    CensusImage* c = &g_img[k];
    if (addr < (uintptr_t)c->base || addr >= (uintptr_t)c->base + (uintptr_t)c->len) continue;
    long pg = (long)((addr - (uintptr_t)c->base) / CENSUS_PAGE);
    c->seen[pg] = 1; c->code[pg] = pc;
    uint8_t* at = c->base + (size_t)pg * CENSUS_PAGE;
#ifdef _WIN32
    DWORD old;
    VirtualProtect(at, CENSUS_PAGE, PAGE_READWRITE, &old);
#else
    mprotect(at, CENSUS_PAGE, PROT_READ | PROT_WRITE);
#endif
    return 1;
  }
  return 0;
}

#ifdef _WIN32
static LONG WINAPI censusHandler(EXCEPTION_POINTERS* e) {
  if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  uintptr_t addr = (uintptr_t)e->ExceptionRecord->ExceptionInformation[1];
  return censusTake(addr, (uintptr_t)e->ExceptionRecord->ExceptionAddress) ? EXCEPTION_CONTINUE_EXECUTION
                                                                          : EXCEPTION_CONTINUE_SEARCH;
}
#else
static struct sigaction g_oldSegv;
static void censusHandler(int sig, siginfo_t* si, void* uc) {
  (void)uc;
  if (censusTake((uintptr_t)si->si_addr, 0)) return;
  sigaction(SIGSEGV, &g_oldSegv, NULL);                  // not ours: the default (a real crash)
  raise(sig);
}
#endif

uint8_t* CtRomCensusWrapMutable(uint8_t* img, long len, const char* name) {
  const char* p = getenv("CT_ROM_CENSUS");
  if (!p || !*p || !img || len <= 0 || g_nimg >= CENSUS_MAX_IMAGES) return img;
  long pages = (len + CENSUS_PAGE - 1) / CENSUS_PAGE;
#ifdef _WIN32
  uint8_t* g = (uint8_t*)VirtualAlloc(NULL, (size_t)pages * CENSUS_PAGE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
  void* m = mmap(NULL, (size_t)pages * CENSUS_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  uint8_t* g = m == MAP_FAILED ? NULL : (uint8_t*)m;
#endif
  if (!g) return img;
  memcpy(g, img, (size_t)len);
  CensusImage* c = &g_img[g_nimg];
  c->base = g; c->len = len; c->name = name;
  c->seen = (uint8_t*)calloc((size_t)pages, 1); c->code = (uintptr_t*)calloc((size_t)pages, sizeof(uintptr_t));
  if (!g_nimg) {
#ifdef _WIN32
    g_exeBase = (uintptr_t)GetModuleHandleA(NULL);
    { char path[512] = ""; GetModuleFileNameA(NULL, path, sizeof path); const char* b = path;
      for (const char* q = path; *q; q++) if (*q == '\\' || *q == '/') b = q + 1;
      snprintf(g_exeName, sizeof g_exeName, "%s", b); }
    AddVectoredExceptionHandler(1, censusHandler);
#else
    { char path[512]; ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);   // the name, as on Windows:
      if (n > 0) { path[n] = 0; const char* b = strrchr(path, '/');                     // tools/rom_census.py counts
        snprintf(g_exeName, sizeof g_exeName, "%.63s", b ? b + 1 : path); } }           // only Belthasar's reads
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = censusHandler; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, &g_oldSegv);
#endif
    atexit(censusWrite);
  }
  g_nimg++;
#ifdef _WIN32
  DWORD old;
  VirtualProtect(g, (size_t)pages * CENSUS_PAGE, PAGE_NOACCESS, &old);
#else
  mprotect(g, (size_t)pages * CENSUS_PAGE, PROT_NONE);
#endif
  fprintf(stderr, "[rom] census: the %s image (%ld bytes) is guarded; pages read go to %s\n", name, len, p);
  return g;
}

const uint8_t* CtRomCensusWrap(const uint8_t* img, long len, const char* name) {
  return CtRomCensusWrapMutable((uint8_t*)img, len, name);
}
