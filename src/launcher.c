// launcher.c - see launcher.h. Windows and Linux (macOS has src/macos_paths.m, Android src/android.c).
#include "launcher.h"
#include "rom_verify.h"
#include "assets.h"       // CtAssetsFileRomFree: a ROM-free container needs no dump beside it
#ifdef CT_DATAFREE
#include "datapack.h"     // CtDataPackFileHasTables: the setup writes the tables inside ct_assets.dat
#endif
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define CT_SEP "\\"
#define CT_EXE ".exe"
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#define CT_SEP "/"
#define CT_EXE ""
#endif

static const char* const ROM_NAME = "Chrono_Trigger_USA_SNES.sfc";

static int readable(const char* dir, const char* name) {
  char p[1200];
  snprintf(p, sizeof p, "%s%s", dir, name);
  FILE* f = fopen(p, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

static void setEnv(const char* key, const char* value, int overwrite) {
  if (!overwrite && getenv(key)) return;
#ifdef _WIN32
  _putenv_s(key, value);
#else
  setenv(key, value, 1);
#endif
}

// ct_assets.dat, and the dump only when the container is not a ROM-free one (docs/ROM.md: a ROM-free container is all
// the engine reads; the dump is the setup's input).
static int romFreeAssets(const char* dir) {
  char p[1200];
  snprintf(p, sizeof p, "%sct_assets.dat", dir);
  return CtAssetsFileRomFree(p);
}
#ifdef CT_DATAFREE
static int tablesThere(const char* dir) {          // inside ct_assets.dat (today's setup), or an earlier setup's ct_tables.dat,
  char p[1200];                                    // made for this build: an older version's data runs the setup again
  snprintf(p, sizeof p, "%sct_assets.dat", dir);
  if (!CtDataPackFileHasTables(p)) snprintf(p, sizeof p, "%sct_tables.dat", dir);
  return CtDataPackFileMatches(p);
}
#endif
int CtLauncherReady(const CtLauncher* L) {
  return readable(L->data, "ct_assets.dat") && (romFreeAssets(L->data) || readable(L->data, ROM_NAME))
#ifdef CT_DATAFREE
         && tablesThere(L->data)
#endif
      ;
}

// The data folder: CT_DATA_DIR; else the executable's own folder when it already holds the game's data (a portable
// copy, or a developer's build/ beside the setup's output); else the player's per-user folder (%APPDATA%\Belthasar\,
// ~/.local/share/Belthasar/).
static int dataFolder(CtLauncher* L) {
  const char* env = getenv("CT_DATA_DIR");
  if (env && *env) {
    size_t n = strlen(env);
    snprintf(L->data, sizeof L->data, "%s%s", env, (env[n - 1] == '/' || env[n - 1] == '\\') ? "" : CT_SEP);
  } else {
    char* base = SDL_GetBasePath();
    int portable = 0;
    if (base) {
      snprintf(L->data, sizeof L->data, "%s", base);
      portable = readable(L->data, "ct_assets.dat") && (romFreeAssets(L->data) || readable(L->data, ROM_NAME));
      SDL_free(base);
    }
    if (!portable) {
      char* pref = SDL_GetPrefPath("BelthasarProject", "Belthasar");
      if (!pref) return 0;
      snprintf(L->data, sizeof L->data, "%s", pref);
      SDL_free(pref);
    }
  }
  snprintf(L->rom, sizeof L->rom, "%s%s", L->data, ROM_NAME);
  return 1;
}

int CtLauncherTakeRom(const CtLauncher* L, const char* path, char* msg, size_t msgLen) {
  FILE* f = fopen(path, "rb");
  if (!f) { snprintf(msg, msgLen, "Could not open %s.", path); return 0; }
  long cap = CT_ROM_SIZE + 0x200 + 1;
  uint8_t* buf = (uint8_t*)malloc((size_t)cap);
  long n = buf ? (long)fread(buf, 1, (size_t)cap, f) : 0;
  fclose(f);
  long len = n;
  const uint8_t* img = buf && n < cap ? CtRomVerify(buf, &len, path) : NULL;
  if (!img) {
    snprintf(msg, msgLen, "This file is not the supported ROM (%ld bytes). Belthasar runs only on the Chrono Trigger "
             "(USA) dump: %ld bytes, SHA-1 %s (a 512-byte copier header is accepted).", n, (long)CT_ROM_SIZE, CT_ROM_SHA1);
    free(buf);
    return 0;
  }
  FILE* o = fopen(L->rom, "wb");
  int ok = o && fwrite(img, 1, (size_t)len, o) == (size_t)len;
  if (o && fclose(o) != 0) ok = 0;
  free(buf);
  if (!ok) { snprintf(msg, msgLen, "Could not write %s.", L->rom); return 0; }
  snprintf(msg, msgLen, "ROM verified: Chrono Trigger (USA), SHA-1 %s.", CT_ROM_SHA1);
  return 1;
}

// ---- the setup's child process ----------------------------------------------------------------------------------
//
// The release puts the setup beside the executable: setup/python/ (a Python runtime), setup/kit/ (the repository's
// tools, the same files as the public tree) and setup/ct[.exe] (the oracle, compiled from the shapes). A developer's
// build/ has none of them: the repository is the kit, build/ct.exe the oracle, `python` from the PATH. CT_PYTHON,
// CT_SETUP_KIT and CT_SETUP_ORACLE override each.
struct CtSetupJob {
  SDL_Thread* thread;
  SDL_mutex* lock;
  CtSetupStatus st;
  FILE* log;
#ifdef _WIN32
  HANDLE proc, out;
#else
  pid_t pid;
  int out;
#endif
};

static int fileExists(const char* p) { FILE* f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }

static void setupPaths(char* python, char* kit, char* oracle, size_t n) {
  char* base = SDL_GetBasePath();
  const char* b = base ? base : "";
  const char* e;
  char p[1200];
  if ((e = getenv("CT_SETUP_KIT")) && *e) snprintf(kit, n, "%s", e);
  else {
    snprintf(p, sizeof p, "%ssetup" CT_SEP "kit" CT_SEP "tools" CT_SEP "setup.py", b);
    if (fileExists(p)) snprintf(kit, n, "%ssetup" CT_SEP "kit", b);
    else snprintf(kit, n, "%s..", b);                                       // build/ inside the repository
  }
  if ((e = getenv("CT_SETUP_ORACLE")) && *e) snprintf(oracle, n, "%s", e);
  else {
    snprintf(p, sizeof p, "%ssetup" CT_SEP "ct" CT_EXE, b);
    if (!fileExists(p)) snprintf(p, sizeof p, "%sct" CT_EXE, b);
    snprintf(oracle, n, "%s", p);
  }
  if ((e = getenv("CT_PYTHON")) && *e) snprintf(python, n, "%s", e);
  else {
#ifdef _WIN32
    snprintf(p, sizeof p, "%ssetup\\python\\python.exe", b);
    snprintf(python, n, "%s", fileExists(p) ? p : "python");
#else
    snprintf(p, sizeof p, "%ssetup/python/bin/python3", b);
    snprintf(python, n, "%s", fileExists(p) ? p : "python3");
#endif
  }
  if (base) SDL_free(base);
}

static void takeLine(CtSetupJob* j, const char* line) {
  if (j->log) { fputs(line, j->log); fputc('\n', j->log); fflush(j->log); }
  SDL_LockMutex(j->lock);
  int k, t;
  if (sscanf(line, "setup: progress %d/%d", &k, &t) == 2) { j->st.done = k; j->st.total = t; }
  else if (*line) snprintf(j->st.line, sizeof j->st.line, "%s", line);
  SDL_UnlockMutex(j->lock);
}

static int readLoop(void* arg) {
  CtSetupJob* j = (CtSetupJob*)arg;
  char buf[4096], line[1024];
  size_t ln = 0;
  for (;;) {
#ifdef _WIN32
    DWORD got = 0;
    if (!ReadFile(j->out, buf, sizeof buf, &got, NULL) || got == 0) break;
#else
    ssize_t got = read(j->out, buf, sizeof buf);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) break;
#endif
    for (size_t i = 0; i < (size_t)got; i++) {
      char c = buf[i];
      if (c == '\r') continue;
      if (c == '\n' || ln + 1 == sizeof line) { line[ln] = 0; takeLine(j, line); ln = 0; if (c == '\n') continue; }
      line[ln++] = c;
    }
  }
  if (ln) { line[ln] = 0; takeLine(j, line); }
  int code = -1;
#ifdef _WIN32
  WaitForSingleObject(j->proc, INFINITE);
  DWORD ec = 1;
  if (GetExitCodeProcess(j->proc, &ec)) code = (int)ec;
#else
  int status = 0;
  while (waitpid(j->pid, &status, 0) < 0 && errno == EINTR) {}
  code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
  SDL_LockMutex(j->lock);
  j->st.running = 0;
  j->st.exitCode = code;
  SDL_UnlockMutex(j->lock);
  return 0;
}

#ifdef _WIN32
static void quoteArg(wchar_t* dst, size_t cap, const char* utf8) {       // CommandLineToArgvW's quoting
  wchar_t w[1200];
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 1200);
  size_t n = wcslen(dst);
  if (n && n + 1 < cap) dst[n++] = L' ';
  if (n + 1 < cap) dst[n++] = L'"';
  for (const wchar_t* s = w; *s && n + 4 < cap; s++) {
    size_t bs = 0;
    while (s[0] == L'\\') { bs++; s++; }
    if (!*s || *s == L'"') { size_t k = *s ? bs * 2 + 1 : bs * 2; while (k-- && n + 2 < cap) dst[n++] = L'\\'; }
    else while (bs-- && n + 2 < cap) dst[n++] = L'\\';
    if (!*s) break;
    dst[n++] = *s;
  }
  if (n + 1 < cap) dst[n++] = L'"';
  dst[n] = 0;
}
#endif

CtSetupJob* CtLauncherSetupStart(const CtLauncher* L, char* msg, size_t msgLen) {
  char python[1200], kit[1200], oracle[1200], script[1300], work[1100], out[1100];
  setupPaths(python, kit, oracle, sizeof python);
  snprintf(script, sizeof script, "%s" CT_SEP "tools" CT_SEP "setup.py", kit);
  if (!fileExists(script)) { snprintf(msg, msgLen, "The setup is missing (%s).", script); return NULL; }
  if (!fileExists(oracle)) { snprintf(msg, msgLen, "The setup's oracle is missing (%s).", oracle); return NULL; }
  snprintf(work, sizeof work, "%swork", L->data);
  snprintf(out, sizeof out, "%s", L->data);
  size_t ol = strlen(out);
  if (ol && (out[ol - 1] == '/' || out[ol - 1] == '\\')) out[ol - 1] = 0;  // (a trailing \ would escape the quote)
  CtSetupJob* j = (CtSetupJob*)calloc(1, sizeof *j);
  if (!j) return NULL;
  const char* argv[] = { python, script, "--rom", L->rom, "--no-build", "--oracle", oracle, "--workdir", work,
                         "--out", out, NULL };                 // (execvp's list ends in NULL)
  int argc = (int)(sizeof argv / sizeof argv[0]) - 1;
  char logPath[1200];
  snprintf(logPath, sizeof logPath, "%ssetup.log", L->data);
  j->log = fopen(logPath, "w");
  j->lock = SDL_CreateMutex();
  j->st.running = 1;
  setEnv("PYTHONUNBUFFERED", "1", 1);
  setEnv("PYTHONIOENCODING", "utf-8", 1);
#ifdef _WIN32
  static wchar_t cl[16384];
  cl[0] = 0;
  for (int i = 0; i < argc; i++) quoteArg(cl, sizeof cl / sizeof cl[0], argv[i]);
  SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
  HANDLE rd, wr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) { snprintf(msg, msgLen, "Could not start the setup (pipe)."); free(j); return NULL; }
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si; memset(&si, 0, sizeof si);
  si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi;
  wchar_t wwork[1200];
  MultiByteToWideChar(CP_UTF8, 0, kit, -1, wwork, 1200);
  if (!CreateProcessW(NULL, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, wwork, &si, &pi)) {
    snprintf(msg, msgLen, "Could not start the setup: %s (error %lu).", python, (unsigned long)GetLastError());
    CloseHandle(rd); CloseHandle(wr); free(j); return NULL;
  }
  CloseHandle(wr); CloseHandle(pi.hThread);
  j->proc = pi.hProcess; j->out = rd;
#else
  int fd[2];
  if (pipe(fd) != 0) { snprintf(msg, msgLen, "Could not start the setup (pipe)."); free(j); return NULL; }
  pid_t pid = fork();
  if (pid == 0) {
    dup2(fd[1], 1); dup2(fd[1], 2); close(fd[0]); close(fd[1]);
    if (chdir(kit) != 0) _exit(127);
    execvp(argv[0], (char* const*)argv);
    _exit(127);
  }
  close(fd[1]);
  if (pid < 0) { close(fd[0]); snprintf(msg, msgLen, "Could not start the setup (fork)."); free(j); return NULL; }
  j->pid = pid; j->out = fd[0];
#endif
  if (j->log) { for (int i = 0; i < argc; i++) fprintf(j->log, "%s%s", i ? " " : "$ ", argv[i]); fputc('\n', j->log); }
  j->thread = SDL_CreateThread(readLoop, "setup", j);
  msg[0] = 0;
  return j;
}

void CtLauncherSetupPoll(CtSetupJob* j, CtSetupStatus* st) {
  SDL_LockMutex(j->lock);
  *st = j->st;
  SDL_UnlockMutex(j->lock);
}

void CtLauncherSetupFree(CtSetupJob* j) {
  if (!j) return;
  if (j->st.running) {                                       // the player quit mid-setup: stop it
#ifdef _WIN32
    TerminateProcess(j->proc, 1);
#else
    kill(j->pid, SIGTERM);
#endif
  }
  SDL_WaitThread(j->thread, NULL);
#ifdef _WIN32
  CloseHandle(j->proc); CloseHandle(j->out);
#else
  close(j->out);
#endif
  if (j->log) fclose(j->log);
  SDL_DestroyMutex(j->lock);
  free(j);
}

#ifdef _WIN32
#include <commdlg.h>
#include <shellapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#endif
int CtLauncherBrowse(char* out, size_t outLen) {
  wchar_t file[1024] = L"";
  OPENFILENAMEW ofn; memset(&ofn, 0, sizeof ofn);
  ofn.lStructSize = sizeof ofn;
  ofn.lpstrFilter = L"SNES ROM (*.sfc, *.smc)\0*.sfc;*.smc\0All files (*.*)\0*.*\0";
  ofn.lpstrFile = file; ofn.nMaxFile = 1024;
  ofn.lpstrTitle = L"Choose your Chrono Trigger (USA) dump";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn)) return 0;
  return WideCharToMultiByte(CP_UTF8, 0, file, -1, out, (int)outLen, NULL, NULL) > 0;
}
void CtLauncherReveal(const char* path) {
  wchar_t w[1200];
  if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1200) > 0) ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
}
#else
static int chooserRun(const char* cmd, char* out, size_t outLen) {
  FILE* p = popen(cmd, "r");
  if (!p) return 0;
  int ok = fgets(out, (int)outLen, p) != NULL;
  int rc = pclose(p);
  if (!ok || rc != 0) return 0;
  size_t n = strlen(out);
  while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
  return n > 0;
}
int CtLauncherBrowse(char* out, size_t outLen) {
#ifdef __APPLE__
  return chooserRun("osascript -e 'POSIX path of (choose file with prompt \"Choose your Chrono Trigger (USA) dump\")' "
                    "2>/dev/null", out, outLen);
#else
  return chooserRun("zenity --file-selection --title='Choose your Chrono Trigger (USA) dump' "
                    "--file-filter='SNES ROM | *.sfc *.smc *.SFC *.SMC' --file-filter='All files | *' 2>/dev/null",
                    out, outLen)
      || chooserRun("kdialog --getopenfilename . '*.sfc *.smc|SNES ROM' 2>/dev/null", out, outLen);
#endif
}
void CtLauncherReveal(const char* path) {
  pid_t pid = fork();
  if (pid == 0) {
#ifdef __APPLE__
    execlp("open", "open", path, (char*)NULL);
#else
    execlp("xdg-open", "xdg-open", path, (char*)NULL);
#endif
    _exit(127);
  }
}
#endif

int CtLauncherConfigure(int* argc, char*** argv) {
  if (getenv("CT_NOPACE")) return 0;                         // the harness's unpaced run (the suite's selfchecks)
  for (int i = 1; i < *argc; i++)
    if ((*argv)[i][0] == '-') return 0;                      // a developer's or the suite's run: untouched
  if (*argc > 2) return 0;
  const char* dropped = *argc == 2 ? (*argv)[1] : NULL;
  static CtLauncher L;
  if (!dataFolder(&L)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Belthasar", "No folder for the game's data could be found.", NULL);
    return 1;
  }
  if (dropped || !CtLauncherReady(&L)) {
    int r = CtLauncherUi(&L, dropped);
    if (!r) return 2;
  }
  setEnv("CT_ROM", L.rom, 0);
  char p[1200];
  snprintf(p, sizeof p, "%sct_sram.srm", L.data); setEnv("CT_SRAM", p, 0);
  snprintf(p, sizeof p, "%sct_settings.ini", L.data); setEnv("CT_SETTINGS", p, 0);
#ifdef _WIN32
  if (_chdir(L.data) != 0) return 1;
#else
  if (chdir(L.data) != 0) return 1;
#endif
  static char* args[4];
  static char assets[1200];
  snprintf(assets, sizeof assets, "%sct_assets.dat", L.data);
  args[0] = (*argv)[0];
  args[1] = (char*)"--assets";
  args[2] = assets;
  args[3] = (char*)"--boot";                               // the cartridge's cold start: the title, then load or new game
  *argc = 4;
  *argv = args;
  return 0;
}
