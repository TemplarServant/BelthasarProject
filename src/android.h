// android.h -- the Android host's start (src/android.c): where the player's files live on the device. docs/ANDROID.md.
#ifndef CT_ANDROID_H
#define CT_ANDROID_H
#ifdef __cplusplus
extern "C" {
#endif

// The game's data folder is the app's external files folder (Android/data/<package>/files): the setup screen
// (android/.../SetupActivity.java) copies the player's ct_assets.dat (the tables inside), ct_wide.dat and save there.
// Sets CT_SRAM / CT_SETTINGS, makes it the working folder, sends stdout/stderr to latest.log there, and adds
// `--assets ct_assets.dat --boot` to a bare start. Returns 0 to go on, else the exit status (the data is missing:
// a message box said so).
int CtAndroidConfigure(int* argc, char*** argv);

#ifdef __cplusplus
}
#endif
#endif
