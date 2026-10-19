# Android

The playable host (`Belthasar`: the field, the overworld, battles, menus, sound) builds as an Android app. On-screen
touch controls are drawn over the picture, and a Bluetooth or USB controller works too.

## What the APK does and does not contain

- **No game data.** The ROM and `ct_assets.dat` are never packed into the APK. On first launch the app asks for
  `ct_assets.dat` (the system file picker) and copies it into its data folder, `Android/data/org.belthasar.ct/files/`.
  A ROM-free `ct_assets.dat` -- what the current setup makes -- is all the game reads: the app needs no ROM at all
  ([ROM.md](ROM.md#the-rom-free-runtime)). An older container still needs the ROM beside it, so the ROM button stays for
  that case. Every later
  launch goes straight to the title screen. `ct_assets.dat` is made on a computer: the desktop Belthasar's ROM screen
  leaves it in its data folder ([ROM.md](ROM.md#the-players-start-the-rom-screen)), or `tools/setup.py` at the root.
- **Each file is checked before it is kept** (`SetupActivity`): the ROM's size and SHA-1 (as `CtRomVerify`), the
  container's header and directory (as `CtAssetsOpen`), a save's 8192 bytes. A file that fails is dropped with the
  reason, the one already there stays, and the game only starts on files that pass; `src/android.c` checks them again
  before the engine reads them (a file replaced by hand) and says so instead of closing.
- **It is built from your ROM.** Like the desktop executable, the native library compiles the sources that
  `tools/setup.py` generates from your dump (`src/data/*.h`, `src/xlat/*.c`). An APK you build is for your own devices
  only. Do not distribute it.

## Requirements (Windows)

| What | Where it comes from |
|---|---|
| Android SDK, platform 36, build-tools | Android Studio's SDK Manager (`%LOCALAPPDATA%\Android\Sdk`) |
| NDK **28.2.13676358** (r28c) | SDK Manager > SDK Tools > *NDK (Side by side)*, or `android-ndk-r28c-windows.zip` unpacked to `Sdk\ndk\28.2.13676358` |
| CMake **3.22.1** | SDK Manager > SDK Tools > *CMake*, or `cmake-3.22.1-windows.zip` unpacked to `Sdk\cmake\3.22.1` |
| A JDK 17+ | Android Studio's bundled one: `C:\Program Files\Android\Android Studio\jbr` |
| SDL2's **source** (2.32.10) | `powershell -ExecutionPolicy Bypass -File tools\get_sdl2_src.ps1` -> `third_party\SDL2-src` |
| Dear ImGui | `tools\get_imgui.ps1` (it is already there if the desktop build has the F1 overlay) |
| The generated sources | `python tools/setup.py <your ROM>` (docs/ROM.md), as for the desktop build |

The Gradle wrapper downloads Gradle 8.14.3 and the Android Gradle Plugin 8.13 on first use, unless they are already
in `%USERPROFILE%\.gradle`.

## Build

```bat
cd android
echo sdk.dir=C:\\Users\\<you>\\AppData\\Local\\Android\\Sdk> local.properties
set JAVA_HOME=C:\Program Files\Android\Android Studio\jbr
gradlew.bat assembleDebug
```

The APK is `android\app\build\outputs\apk\debug\app-debug.apk`. It holds `arm64-v8a` (phones) and `x86_64` (the
emulator); `abiFilters` in `android/app/build.gradle` picks them. The native code is compiled with `-O2` even in the
debug build, so it runs at full speed.

You can also open `android/` in Android Studio and press Run.

Install on a phone with USB debugging on:

```bat
"%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe" install -r android\app\build\outputs\apk\debug\app-debug.apk
```

### How it is put together

- `android/app/build.gradle` runs the NDK's CMake on the **root** `CMakeLists.txt`. Its `if(ANDROID)` branch includes
  `android/CMakeLists.cmake`, which builds `libSDL2.so` from `third_party/SDL2-src` and `libmain.so` from the same
  source list as the desktop `Belthasar` (`CT_FIELD_SRC`), plus `src/android.c`. The desktop targets are untouched.
- SDL's Java glue (`org.libsdl.app`) is compiled straight from `third_party/SDL2-src/android-project`.
  `GameActivity` is SDL's activity. `SetupActivity` is the first-run file picker.
- `src/android.c` (`CtAndroidConfigure`, called first thing in `main`) makes the data folder the working folder. It
  points `CT_ROM`, `CT_SRAM` and `CT_SETTINGS` there, sends stdout/stderr to `latest.log` in it, and starts the game
  with `--assets ct_assets.dat --boot` (the title screen).
- The picture goes through SDL_Renderer on OpenGL ES. The OpenGL 3.3 and Vulkan backends of the desktop are not
  offered on Android.

## Getting the files onto the device

The first-run screen has three buttons: `ct_assets.dat` (the one `tools/setup.py` wrote next to the ROM on your
computer), the ROM (`.sfc`, 4 MB; a 512-byte copier header is accepted -- only for an older `ct_assets.dat`), and,
optionally, a battery save (`ct_sram.srm`, 8 KB, from the desktop game). Copy them to the phone first (USB, cloud
drive, ...), then pick them.

Or push them by hand:

```bat
adb push ct_assets.dat /sdcard/Android/data/org.belthasar.ct/files/
adb push ct_sram.srm /sdcard/Android/data/org.belthasar.ct/files/
```

To pick other files later, clear the app's storage (Settings > Apps > Chrono Trigger (C) > Storage). Saves live in
the same folder, so back up `ct_sram.srm` before you clear it.

## Controls

- **On-screen gamepad** (`src/touchpad.c`): the d-pad bottom left (eight directions; a finger may slide). A/B/X/Y
  bottom right, placed as on the SNES pad. L and R in the top corners, with Select under L and Start under R. A finger
  can slide from one button to the next, and two fingers hold two buttons. The buttons feed the same pad word as the
  keyboard and a controller (`field.c` `fieldKeyPad`). Their presses give the same edges a controller button gives.
- **MENU** (under Select) or the phone's **Back** button opens the options (the desktop's F1 overlay). Back closes them
  again. *Controls* has the on-screen gamepad switch, its opacity and its size. They are saved in `ct_settings.ini`
  under `[touch]` (`virtual_pad`, `opacity`, `size`).
- **A controller** (Bluetooth / USB): it works as on the desktop (the *Controls* tab binds it). The on-screen gamepad
  hides as soon as a controller button or stick is used, and comes back at the next touch. It does not hide merely
  because a controller is connected: a phone's own keys (the emulator's keyboard, some remotes) can show up as a
  controller.

The on-screen gamepad is a host option like the keyboard layout: it changes nothing in the game. It is on by default
only on Android. On the desktop it is off (`virtual_pad=0`), and headless runs never read the settings file, so the
desktop game and the regression suite are unchanged.

## Logs

`latest.log` in the data folder holds what the desktop build prints to the console:

```bat
adb shell cat /sdcard/Android/data/org.belthasar.ct/files/latest.log
```
