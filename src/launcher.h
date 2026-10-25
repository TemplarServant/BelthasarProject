// launcher.h -- the player's start on Windows and Linux: where the game's data lives, and the ROM screen that makes it.
//
// BelthasarProject ships none of the game (docs/ROM.md). A player who opens Belthasar with no arguments (or drops a file
// on it) gets a data folder of their own: their ROM, the ct_assets.dat and ct_tables.dat made from it, their saves and
// options. While that folder lacks any of them, the ROM screen (src/launcher_ui.cpp) asks for the dump, verifies it
// (CtRomVerify) and runs the setup (tools/setup.py) without a compiler: the bundled Python, the bundled oracle.
// A developer's or the suite's run (any argument starting with '-', or CT_NOPACE set) never reaches it.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// main()'s hook (field.c): 0 = run the game (argc/argv may be replaced: --assets, --boot), 1 = an error was shown,
// 2 = the player closed the ROM screen.
int CtLauncherConfigure(int* argc, char*** argv);

// --- for the ROM screen ---
typedef struct CtLauncher {
  char data[1024];          // the data folder (ends with a separator)
  char rom[1100];           // <data>Chrono_Trigger_USA_SNES.sfc
} CtLauncher;

// Verify the file at `path` as the supported dump and write it to the data folder. 1 = done; 0 = not the dump (or not
// readable / writable), with the reason in msg.
int CtLauncherTakeRom(const CtLauncher* L, const char* path, char* msg, size_t msgLen);

// The setup, run as a child process. Start it once; poll it each frame.
typedef struct CtSetupJob CtSetupJob;
CtSetupJob* CtLauncherSetupStart(const CtLauncher* L, char* msg, size_t msgLen);
typedef struct {
  int running, exitCode;    // exitCode valid once running = 0
  int done, total;          // the setup's `setup: progress K/N`
  char line[256];           // its last line
} CtSetupStatus;
void CtLauncherSetupPoll(CtSetupJob* j, CtSetupStatus* st);
void CtLauncherSetupFree(CtSetupJob* j);

// 1 when the data folder holds everything the game needs.
int CtLauncherReady(const CtLauncher* L);

// The system's own file chooser for the dump (Windows: the common Open dialog; Linux: zenity or kdialog when present).
// 1 = a file was chosen (its path, UTF-8, in `out`); 0 = cancelled or no chooser.
int CtLauncherBrowse(char* out, size_t outLen);
// Show a folder or a file with the system (Explorer / xdg-open / open).
void CtLauncherReveal(const char* path);

// The screen itself (launcher_ui.cpp with Dear ImGui; launcher_ui_stub.c without): 1 = the data is ready, 0 = quit.
// `dropped` = a file given on the command line (dropped on the executable), or NULL.
int CtLauncherUi(CtLauncher* L, const char* dropped);

#ifdef __cplusplus
}
#endif
