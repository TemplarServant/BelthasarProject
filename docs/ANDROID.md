# Android

The playable host (`Belthasar`: the field, the overworld, battles, menus, sound) builds as an Android app. On-screen
touch controls are drawn over the picture, and a Bluetooth or USB controller works too.

## What the APK does and does not contain

- **No game data.** The APK is built DATA-FREE (`android/CMakeLists.cmake`: `CT_DATAFREE`, always), like the desktop
  release's `Belthasar`: no table made from the ROM is compiled in, so an APK built from the shapes
  (`tools/gen/shapes.py install`) holds no byte of the game, and that APK is the one a release ships
  ([RELEASE.md](RELEASE.md)). On first launch the app asks for the game's data and copies it into its data folder,
  `Android/data/org.belthasar.ct/files/`. Every later launch goes straight to the title screen.
- **The data is made on a computer, from your own dump.** The desktop Belthasar's ROM screen
  ([ROM.md](ROM.md#the-players-start-the-rom-screen)) leaves in its data folder `ct_assets.dat` -- the game's data with
  the tables inside (`tools/gen/pack_tables.py into`) -- and, when it found a Steam copy, `ct_wide.dat` (the Steam
  margins, [WIDESCREEN.md](WIDESCREEN.md)). Those two and the battery save `ct_sram.srm` are all the app takes; it never
  takes the ROM, which only the setup reads. The app and the desktop setup should be the same release: a container
  whose tables are laid out for another version is refused with a message.
- **Each file is known by its content and checked before it is kept** (`SetupActivity`): a ROM-free `ct_assets.dat`
  (the header and directory, as `CtAssetsOpen`; the tables inside, or an earlier setup's `ct_tables.dat` beside it), a
  `ct_wide.dat` (`CTWD`, version 3), a save's 8192 bytes. A file that fails is dropped with the reason, the one already
  there stays, and the game only starts on files that pass; `src/android.c` checks them again before the engine reads
  them (a file replaced by hand) and says so instead of closing.

## Requirements (Windows)

| What | Where it comes from |
|---|---|
| Android SDK, platform 36, build-tools | Android Studio's SDK Manager (`%LOCALAPPDATA%\Android\Sdk`) |
| NDK **28.2.13676358** (r28c) | SDK Manager > SDK Tools > *NDK (Side by side)*, or `android-ndk-r28c-windows.zip` unpacked to `Sdk\ndk\28.2.13676358` |
| CMake **3.22.1** | SDK Manager > SDK Tools > *CMake*, or `cmake-3.22.1-windows.zip` unpacked to `Sdk\cmake\3.22.1` |
| A JDK 17+ | Android Studio's bundled one: `C:\Program Files\Android\Android Studio\jbr` |
| SDL2's **source** (2.32.10) | `powershell -ExecutionPolicy Bypass -File tools\get_sdl2_src.ps1` -> `third_party\SDL2-src` |
| Dear ImGui | `tools\get_imgui.ps1` (it is already there if the desktop build has the F1 overlay) |
| `src/data/*.h` | a tree `tools/setup.py` set up (docs/ROM.md), or, with no ROM, their shapes: `python tools/gen/shapes.py install` (what a release builds from) |

The Gradle wrapper downloads Gradle 8.14.3 and the Android Gradle Plugin 8.13 on first use, unless they are already
in `%USERPROFILE%\.gradle`.

## Build

```bat
cd android
echo sdk.dir=C:\\Users\\<you>\\AppData\\Local\\Android\\Sdk> local.properties
set JAVA_HOME=C:\Program Files\Android\Android Studio\jbr
gradlew.bat assembleDebug
```

The APK is `android\app\build\outputs\apk\debug\app-debug.apk`. A release's APK comes from
`python tools/release/package.py --android` (`assembleRelease`, signed with the release key: `android/keystore.properties`
or the `ANDROID_KEYSTORE_*` variables, android/app/build.gradle) -- in practice from the release workflow
([RELEASE.md](RELEASE.md#the-release-workflow)). It holds `arm64-v8a` (phones) and `x86_64` (the
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
  source list as the desktop `Belthasar` (`CT_FIELD_SRC`), plus `src/android.c` and the data-free loader
  (`src/datapack.c`, `src/tables/tables_storage.c`). The desktop targets are untouched.
- SDL's Java glue (`org.libsdl.app`) is compiled straight from `third_party/SDL2-src/android-project`.
  `GameActivity` is SDL's activity. `SetupActivity` is the first-run file picker.
- `src/android.c` (`CtAndroidConfigure`, called first thing in `main`) makes the data folder the working folder. It
  points `CT_SRAM` and `CT_SETTINGS` there, sends stdout/stderr to `latest.log` in it, and starts the game
  with `--assets ct_assets.dat --boot` (the title screen).
- The picture goes through SDL_Renderer on OpenGL ES. The OpenGL 3.3 and Vulkan backends of the desktop are not
  offered on Android.

## Getting the files onto the device

Copy `ct_assets.dat` from the desktop Belthasar's own folder (beside `Belthasar.exe` / `Belthasar`; versions up to
0.5.0 kept it in `%APPDATA%\BelthasarProject\Belthasar\` or `~/.local/share/BelthasarProject/Belthasar/`; a developer's
`python tools/setup.py` leaves it in `build/player/` -- not the root's `ct_assets.dat`, which has no tables) to the phone -- USB, a cloud drive, ... -- with `ct_wide.dat`
and `ct_sram.srm` if you want them, then press *Choose the game's files* and pick them, all at once or one by one.

Or push them by hand:

```bat
adb push ct_assets.dat /sdcard/Android/data/org.belthasar.ct/files/
adb push ct_wide.dat /sdcard/Android/data/org.belthasar.ct/files/
adb push ct_sram.srm /sdcard/Android/data/org.belthasar.ct/files/
```

To pick other files later, clear the app's storage (Settings > Apps > Belthasar > Storage). Saves live in the same
folder, so back up `ct_sram.srm` before you clear it.

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
